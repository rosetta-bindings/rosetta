namespace rosetta {

    // Thread-safe in-memory store of T instances, keyed by integer ID.
    template <typename T> class Store {
    public:
        int create() {
            std::lock_guard lk(m_);
            int             id = next_id_++;
            map_.emplace(id, T{});
            return id;
        }
        T *find(int id) {
            std::lock_guard lk(m_);
            auto            it = map_.find(id);
            return it == map_.end() ? nullptr : &it->second;
        }
        bool erase(int id) {
            std::lock_guard lk(m_);
            return map_.erase(id) > 0;
        }

    private:
        std::mutex                 m_;
        std::unordered_map<int, T> map_;
        std::atomic<int>           next_id_{1};
    };

    namespace detail {

        // 404 helper. Returns the resolved pointer or nullptr (after writing res).
        template <typename T>
        T *resolve_id(Store<T> *sp, const httplib::Request &req, httplib::Response &res) {
            int id = std::stoi(req.matches[1]);
            T  *p  = sp->find(id);
            if (!p) {
                res.status = 404;
                res.set_content(nlohmann::json{{"error", "not found"}}.dump(), "application/json");
            }
            return p;
        }

        // Generic instance method dispatch: unpack JSON array -> typed args.
        template <std::meta::info Fn, typename T, std::size_t... Is>
        nlohmann::json invoke_method_impl(T &self, const nlohmann::json &args,
                                          std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (std::is_void_v<R>) {
                (self.[:Fn:])(
                    args[Is]
                        .template get<
                            std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()...);
                return nullptr;
            } else {
                R r = (self.[:Fn:])(
                    args[Is]
                        .template get<
                            std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()...);
                return nlohmann::json(r);
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        nlohmann::json invoke_static_impl(const nlohmann::json &args, std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (std::is_void_v<R>) {
                ([:Fn:])(
                    args[Is]
                        .template get<
                            std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()...);
                return nullptr;
            } else {
                R r = ([:Fn:])(
                    args[Is]
                        .template get<
                            std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()...);
                return nlohmann::json(r);
            }
        }

        // ---- Skip predicate ----
        //
        // nlohmann::json can (de)serialize scalars, strings, and vectors of
        // those, but not arbitrary user types or std::function. Members using
        // an unsupported type are skipped rather than failing to compile.
        template <typename T> struct is_vector : std::false_type {};
        template <typename U, typename A> struct is_vector<std::vector<U, A>> : std::true_type {};

        template <typename T> consteval bool rest_supported() {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, bool> ||
                          std::is_arithmetic_v<U>) {
                return true;
            } else if constexpr (is_vector<U>::value) {
                return rest_supported<typename U::value_type>();
            } else if constexpr (std::is_enum_v<U>) {
                return true; // nlohmann (de)serializes enums as underlying int
            } else {
                return false;
            }
        }

        // A method is bindable only if its return type and every parameter
        // type can be JSON-(de)serialized.
        template <std::meta::info Fn> consteval bool method_supported() {
            using Ret = [:std::meta::return_type_of(Fn):];
            bool ok   = std::is_void_v<Ret> || rest_supported<std::remove_cvref_t<Ret>>();
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((ok = ok &&
                       rest_supported<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()),
                 ...);
            }(std::make_index_sequence<params.size()>{});
            return ok;
        }

    } // namespace detail

    // ---- Visitor ----
    //
    // Captures only the store pointer (and string-literal-backed names) into
    // each handler, so the visitor object is safe to destroy after walk().

    template <typename T> struct RestVisitor {
        using json = nlohmann::json;

        httplib::Server &server;
        std::string      base; // e.g. "/Person"
        Store<T>        *store_ptr;

        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            using F = [:std::meta::type_of(Fld):];
            if constexpr (detail::rest_supported<std::remove_cvref_t<F>>()) {
            auto *sp  = store_ptr;
            auto  url = base + R"(/(\d+)/)" + name;

            server.Get(url, [sp](const httplib::Request &req, httplib::Response &res) {
                T *p = detail::resolve_id(sp, req, res);
                if (!p) {
                    return;
                }
                res.set_content(json(p->[:Fld:]).dump(), "application/json");
            });

            if constexpr (ann::has<readonly>(Anns...)) {
                server.Put(url, [name](const httplib::Request &, httplib::Response &res) {
                    res.status = 403;
                    res.set_content(json{{"error", std::string(name) + " is read-only"}}.dump(),
                                    "application/json");
                });
            } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
                constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                server.Put(url, [sp, name](const httplib::Request &req, httplib::Response &res) {
                    T *p = detail::resolve_id(sp, req, res);
                    if (!p) {
                        return;
                    }
                    try {
                        F      val = json::parse(req.body).template get<F>();
                        double d   = static_cast<double>(val);
                        if (d < r.min || d > r.max) {
                            res.status = 400;
                            res.set_content(
                                json{{"error", std::string(name) + " out of range"}}.dump(),
                                "application/json");
                            return;
                        }
                        p->[:Fld:] = val;
                        res.status = 204;
                    } catch (const std::exception &e) {
                        res.status = 400;
                        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                    }
                });
            } else {
                server.Put(url, [sp](const httplib::Request &req, httplib::Response &res) {
                    T *p = detail::resolve_id(sp, req, res);
                    if (!p) {
                        return;
                    }
                    try {
                        p->[:Fld:] = json::parse(req.body).template get<F>();
                        res.status = 204;
                    } catch (const std::exception &e) {
                        res.status = 400;
                        res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                    }
                });
            }
            } // rest_supported<F>
        }

        // A route is a URL path, and two overloads would register the same
        // path twice — only the first-declared entry of a set binds.
        template <std::meta::info Fn, auto... /*Anns*/> void method_instance(const char *name) {
            if constexpr (detail::method_supported<Fn>() && first_overload<T, Fn>()) {
            auto *sp  = store_ptr;
            auto  url = base + R"(/(\d+)/)" + name;

            server.Post(url, [sp](const httplib::Request &req, httplib::Response &res) {
                T *p = detail::resolve_id(sp, req, res);
                if (!p) {
                    return;
                }
                try {
                    json           args = req.body.empty() ? json::array() : json::parse(req.body);
                    constexpr auto arity =
                        std::define_static_array(std::meta::parameters_of(Fn)).size();
                    json result =
                        detail::invoke_method_impl<Fn>(*p, args, std::make_index_sequence<arity>{});
                    res.set_content(result.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                }
            });
            } // method_supported<Fn>
        }

        template <std::meta::info Fn, auto... /*Anns*/> void method_static(const char *name) {
            if constexpr (detail::method_supported<Fn>() && first_overload<T, Fn>()) {
            auto url = base + "/" + name;

            server.Post(url, [](const httplib::Request &req, httplib::Response &res) {
                try {
                    json           args = req.body.empty() ? json::array() : json::parse(req.body);
                    constexpr auto arity =
                        std::define_static_array(std::meta::parameters_of(Fn)).size();
                    json result =
                        detail::invoke_static_impl<Fn>(args, std::make_index_sequence<arity>{});
                    res.set_content(result.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                }
            });
            } // method_supported<Fn>
        }
    };

    // Registers CRUD endpoints + the walk-emitted routes for T.
    template <typename T>
    inline void bind_rest(httplib::Server &server, const std::string &base_path, Store<T> &store) {
        using json = nlohmann::json;
        auto *sp   = &store;

        // POST /<base>  -> create new instance, return {"id": N}
        server.Post(base_path, [sp](const httplib::Request &, httplib::Response &res) {
            int id = sp->create();
            res.set_content(json{{"id", id}}.dump(), "application/json");
        });

        // DELETE /<base>/{id}
        server.Delete(base_path + R"(/(\d+))",
                      [sp](const httplib::Request &req, httplib::Response &res) {
                          int id     = std::stoi(req.matches[1]);
                          res.status = sp->erase(id) ? 204 : 404;
                      });

        RestVisitor<T> v{server, base_path, sp};
        walk<T>(v);
    }

    template <typename T>
    inline void bind_rest_enum(httplib::Server &server, const std::string &base) {
        nlohmann::json j;
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^T))) {
            constexpr const char *nm = std::define_static_string(std::meta::identifier_of(en));
            j[nm] = static_cast<std::underlying_type_t<T>>([:en:]);
        }
        server.Get(base, [j](const httplib::Request &, httplib::Response &res) {
            res.set_content(j.dump(), "application/json");
        });
    }

    template <std::meta::info F>
    inline void bind_rest_function(httplib::Server &server, const std::string &base) {
        using json = nlohmann::json;
        if constexpr (detail::method_supported<F>()) {
            server.Post(base, [](const httplib::Request &req, httplib::Response &res) {
                try {
                    json           args = req.body.empty() ? json::array() : json::parse(req.body);
                    constexpr auto arity =
                        std::define_static_array(std::meta::parameters_of(F)).size();
                    json result =
                        detail::invoke_static_impl<F>(args, std::make_index_sequence<arity>{});
                    res.set_content(result.dump(), "application/json");
                } catch (const std::exception &e) {
                    res.status = 500;
                    res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                }
            });
        }
        // else: a parameter/return type isn't JSON-serializable — no route.
    }

} // namespace rosetta
