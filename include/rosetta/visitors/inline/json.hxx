namespace rosetta {

    namespace json_detail {

        template <typename T> struct is_vector : std::false_type {};
        template <typename U, typename A> struct is_vector<std::vector<U, A>> : std::true_type {};

        // Enum <-> enumerator name, via reflection.
        template <typename E> inline std::string enum_to_name(E v) {
            std::string out;
            template for (constexpr auto e :
                          std::define_static_array(std::meta::enumerators_of(^^E))) {
                if (v == [:e:]) {
                    out = std::define_static_string(std::meta::identifier_of(e));
                }
            }
            return out;
        }

        template <typename E> inline E enum_from_name(const std::string &name) {
            E out{};
            template for (constexpr auto e :
                          std::define_static_array(std::meta::enumerators_of(^^E))) {
                constexpr const char *nm = std::define_static_string(std::meta::identifier_of(e));
                if (name == nm) {
                    out = [:e:];
                }
            }
            return out;
        }

        // Encode any field value to JSON. Strings/scalars go straight through
        // nlohmann; enums become names; vectors and nested reflected classes
        // recurse.
        template <typename F> inline nlohmann::json encode(const F &v) {
            using U = std::remove_cvref_t<F>;
            if constexpr (std::is_enum_v<U>) {
                return enum_to_name(v);
            } else if constexpr (std::is_same_v<U, std::string>) {
                return v;
            } else if constexpr (is_vector<U>::value) {
                nlohmann::json a = nlohmann::json::array();
                for (const auto &e : v) {
                    a.push_back(encode(e));
                }
                return a;
            } else if constexpr (std::is_class_v<U>) {
                return rosetta::to_json(v); // nested reflected class
            } else {
                return nlohmann::json(v); // arithmetic / bool
            }
        }

        template <typename U> inline U decode(const nlohmann::json &j) {
            if constexpr (std::is_enum_v<U>) {
                return enum_from_name<U>(j.template get<std::string>());
            } else if constexpr (std::is_same_v<U, std::string>) {
                return j.template get<std::string>();
            } else if constexpr (is_vector<U>::value) {
                U out;
                out.reserve(j.size());
                for (const auto &e : j) {
                    out.push_back(decode<typename U::value_type>(e));
                }
                return out;
            } else if constexpr (std::is_class_v<U>) {
                return rosetta::from_json<U>(j); // nested reflected class
            } else {
                return j.template get<U>(); // arithmetic / bool
            }
        }

    } // namespace json_detail

    // Walk visitor: reads each public field off the live instance into `j`.
    template <typename T> struct JsonWriter {
        const T        &obj;
        nlohmann::json &j;

        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            j[name] = json_detail::encode(obj.[:Fld:]);
        }
        template <std::meta::info, auto...> void method_instance(const char *) {}
        template <std::meta::info, auto...> void method_static(const char *) {}
    };

    // Walk visitor: writes each present JSON member back into the instance.
    template <typename T> struct JsonReader {
        T                    &obj;
        const nlohmann::json &j;

        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            using Raw = [:std::meta::type_of(Fld):];
            using F   = std::remove_cvref_t<Raw>;
            if (j.contains(name)) {
                obj.[:Fld:] = json_detail::decode<F>(j.at(name));
            }
        }
        template <std::meta::info, auto...> void method_instance(const char *) {}
        template <std::meta::info, auto...> void method_static(const char *) {}
    };

    template <typename T> inline nlohmann::json to_json(const T &v) {
        if constexpr (std::is_enum_v<T>) {
            return json_detail::enum_to_name(v);
        } else {
            nlohmann::json j = nlohmann::json::object();
            JsonWriter<T>  w{v, j};
            walk<T>(w);
            return j;
        }
    }

    template <typename T> inline T from_json(const nlohmann::json &j) {
        if constexpr (std::is_enum_v<T>) {
            return json_detail::enum_from_name<T>(j.template get<std::string>());
        } else {
            T             obj{};
            JsonReader<T> r{obj, j};
            walk<T>(r);
            return obj;
        }
    }

} // namespace rosetta
