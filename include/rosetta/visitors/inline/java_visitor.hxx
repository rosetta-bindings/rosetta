namespace rosetta {

    namespace jvm {

        // The Store, registry, JSON marshalling (encode / decode), the
        // {"ok":…}/{"error":…} envelopes and the api_* dispatch all live in
        // <rosetta/backends/java_runtime.h> (pulled in by java_visitor.h). What
        // remains here is the reflective half: the boundary-support predicates,
        // the splice-based invokers, and the walk visitor. It mirrors the C#
        // visitor 1:1 — only the registry it fills differs (rosetta::jvm).

        // ---- Boundary-support predicate (same surface as C# / REST) ----

        template <typename T> struct is_vector : std::false_type {};
        template <typename U, typename A> struct is_vector<std::vector<U, A>> : std::true_type {};

        // Can a value of T be marshalled as JSON across the Java boundary?
        template <typename T> consteval bool supported() {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, bool> ||
                          std::is_arithmetic_v<U>) {
                return true;
            } else if constexpr (is_vector<U>::value) {
                return supported<typename U::value_type>();
            } else if constexpr (std::is_enum_v<U>) {
                return true;
            } else {
                return false;
            }
        }

        // A method / function is bindable only if its return type and every
        // parameter type can cross the boundary.
        template <std::meta::info Fn> consteval bool method_supported() {
            using Ret = [:std::meta::return_type_of(Fn):];
            bool ok   = std::is_void_v<Ret> || supported<std::remove_cvref_t<Ret>>();
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((ok = ok &&
                       supported<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()),
                 ...);
            }(std::make_index_sequence<params.size()>{});
            return ok;
        }

        template <std::meta::info Ctor> consteval bool ctor_supported() {
            bool           ok     = true;
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((ok = ok &&
                       supported<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()),
                 ...);
            }(std::make_index_sequence<params.size()>{});
            return ok;
        }

        // ---- Splice-based invokers (unpack JSON array -> typed args) ----

        template <std::meta::info Fn, typename T, std::size_t... Is>
        json invoke_method(T &self, const json &args, std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (std::is_void_v<R>) {
                (self.[:Fn:])(
                    decode<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return nullptr;
            } else {
                R r = (self.[:Fn:])(
                    decode<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return encode(r);
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        json invoke_static(const json &args, std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (std::is_void_v<R>) {
                ([:Fn:])(decode<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                    args[Is])...);
                return nullptr;
            } else {
                R r = ([:Fn:])(decode<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                    args[Is])...);
                return encode(r);
            }
        }

        template <typename T, std::meta::info Ctor, std::size_t... Is>
        T reflect_construct(const json &args, std::index_sequence<Is...>) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            return T(decode<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                args[Is])...);
        }

        // ---- Walk visitor ----

        template <typename T> struct Visitor {
            TypeOps &ops;

            template <std::meta::info Fld, auto... Anns> void field(const char *name) {
                using Raw = [:std::meta::type_of(Fld):];
                using F   = std::remove_cvref_t<Raw>;
                if constexpr (supported<F>()) {
                    Store<T> &st = store_of<T>();

                    ops.get[name] = [&st](int id) -> json {
                        T *p = st.find(id);
                        if (!p) {
                            throw std::runtime_error("instance not found");
                        }
                        return encode((*p).[:Fld:]);
                    };

                    if constexpr (ann::has<readonly>(Anns...)) {
                        ops.set[name] = [name](int, const json &) {
                            throw std::runtime_error(std::string(name) + " is read-only");
                        };
                    } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
                        constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                        ops.set[name]    = [&st, name](int id, const json &v) {
                            T *p = st.find(id);
                            if (!p) {
                                throw std::runtime_error("instance not found");
                            }
                            F      val = decode<F>(v);
                            double d   = static_cast<double>(val);
                            if (d < r.min || d > r.max) {
                                throw std::runtime_error(std::string(name) + " out of range");
                            }
                            (*p).[:Fld:] = val;
                        };
                    } else {
                        ops.set[name] = [&st](int id, const json &v) {
                            T *p = st.find(id);
                            if (!p) {
                                throw std::runtime_error("instance not found");
                            }
                            (*p).[:Fld:] = decode<F>(v);
                        };
                    }
                }
            }

            // `ops.call` / `ops.scall` are maps keyed by the method name, so a
            // second entry would overwrite the first rather than overload it —
            // only the first-declared entry of a set binds (first_overload).
            template <std::meta::info Fn, auto... /*Anns*/> void method_instance(const char *name) {
                if constexpr (method_supported<Fn>() && first_overload<T, Fn>()) {
                    Store<T> &st   = store_of<T>();
                    ops.call[name] = [&st](int id, const json &args) -> json {
                        T *p = st.find(id);
                        if (!p) {
                            throw std::runtime_error("instance not found");
                        }
                        constexpr auto arity =
                            std::define_static_array(std::meta::parameters_of(Fn)).size();
                        return invoke_method<Fn>(*p, args, std::make_index_sequence<arity>{});
                    };
                }
            }

            template <std::meta::info Fn, auto... /*Anns*/> void method_static(const char *name) {
                if constexpr (method_supported<Fn>() && first_overload<T, Fn>()) {
                    ops.scall[name] = [](const json &args) -> json {
                        constexpr auto arity =
                            std::define_static_array(std::meta::parameters_of(Fn)).size();
                        return invoke_static<Fn>(args, std::make_index_sequence<arity>{});
                    };
                }
            }

            template <std::meta::info Ctor, auto... /*Anns*/> void constructor() {
                if constexpr (ctor_supported<Ctor>()) {
                    constexpr auto arity =
                        std::define_static_array(std::meta::parameters_of(Ctor)).size();
                    if constexpr (arity > 0) { // 0-arg handled by ops.create
                        Store<T> &st     = store_of<T>();
                        ops.ctors[arity] = [&st](const json &args) -> int {
                            return st.insert(
                                reflect_construct<T, Ctor>(args, std::make_index_sequence<arity>{}));
                        };
                    }
                }
            }
        };

    } // namespace jvm

    // ---- Entry points ----

    template <typename T> inline void bind_java(const char *type_name) {
        jvm::TypeOps   ops;
        jvm::Store<T> &st = jvm::store_of<T>();
        if constexpr (std::is_default_constructible_v<T>) {
            ops.create = [&st] { return st.create(); };
        }
        ops.destroy = [&st](int id) { return st.erase(id); };

        jvm::Visitor<T> v{ops};
        walk<T>(v);
        jvm::registry()[type_name] = std::move(ops);
    }

    template <typename T> inline void bind_java_enum(const char *type_name) {
        nlohmann::json j = nlohmann::json::object();
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^T))) {
            constexpr const char *nm = std::define_static_string(std::meta::identifier_of(en));
            j[nm]                    = static_cast<std::underlying_type_t<T>>([:en:]);
        }
        jvm::enums()[type_name] = std::move(j);
    }

    template <std::meta::info F> inline void bind_java_function(const char *name) {
        if constexpr (jvm::method_supported<F>()) {
            jvm::functions()[name] = [](const nlohmann::json &args) -> nlohmann::json {
                constexpr auto arity =
                    std::define_static_array(std::meta::parameters_of(F)).size();
                return jvm::invoke_static<F>(args, std::make_index_sequence<arity>{});
            };
        }
        // else: a parameter / return type isn't JSON-marshalable — not registered.
    }

} // namespace rosetta
