namespace rosetta {

    // A type nanobind can represent in a generated signature. Mirrors
    // PybindVisitor's py_bindable_type: nanobind, like pybind11, has no caster
    // for raw C arrays (e.g. a method returning `double (&)[3][3]`) and needs a
    // complete type (sizeof / typeid) for a class returned/taken by value or
    // reference. `sizeof(T*)` is always valid, so a pointer is judged by its
    // pointee (incomplete pointee ⇒ not bindable); a std::vector is cast
    // element-wise, so it recurses into its element type.
    template <class P>
    concept nb_pointee_ok =
        std::is_void_v<std::remove_cv_t<std::remove_pointer_t<P>>> ||
        requires { sizeof(std::remove_pointer_t<P>); };

    template <class T> struct nb_vector_elem {
        static constexpr bool is = false;
        using type               = void;
    };
    template <class E, class A> struct nb_vector_elem<std::vector<E, A>> {
        static constexpr bool is = true;
        using type               = E;
    };

    template <class T> consteval bool nb_is_bindable() {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
            return false;
        } else if constexpr (std::is_pointer_v<U>) {
            return nb_pointee_ok<U>;
        } else if constexpr (nb_vector_elem<U>::is) {
            return nb_is_bindable<typename nb_vector_elem<U>::type>();
        } else {
            return std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_void_v<U> ||
                   requires { sizeof(U); };
        }
    }

    template <class T>
    concept nb_bindable_type = nb_is_bindable<T>();

    template <std::meta::info Fn, std::size_t... Is>
    consteval bool nb_params_bindable(std::index_sequence<Is...>) {
        constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
        return (nb_bindable_type<typename[:std::meta::type_of(params[Is]):]> && ...);
    }

    template <std::meta::info Fn> consteval bool nb_bindable_fn() {
        constexpr auto arity = std::meta::parameters_of(Fn).size();
        return nb_bindable_type<typename[:std::meta::return_type_of(Fn):]> &&
               nb_params_bindable<Fn>(std::make_index_sequence<arity>{});
    }

    // Mirrors PybindVisitor, against the nanobind API. Bound to nb::class_<T>
    // directly (no trampoline parameterisation yet).
    template <typename T> struct NanobindVisitor {
        nb::class_<T> &cls;
        bool           saw_default_ctor = false;

        template <std::meta::info Ctor, auto... Anns> void constructor() {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            constexpr auto dann   = ann::get_or<doc>(doc{""}, Anns...);
            if constexpr (params.size() == 0)
                saw_default_ctor = true;
            // Skip a constructor nanobind can't represent (e.g. a vector of
            // pointers to an incomplete type) rather than failing the build.
            if constexpr (nb_params_bindable<Ctor>(std::make_index_sequence<params.size()>{}))
                register_init<Ctor>(std::make_index_sequence<params.size()>{}, dann.text);
        }

      private:
        template <std::meta::info Ctor, std::size_t... Is>
        void register_init(std::index_sequence<Is...>, const char *docstr) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            cls.def(nb::init<typename[:std::meta::type_of(params[Is]):]...>(), docstr);
        }

      public:
        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            using F                      = [:std::meta::type_of(Fld):];
            constexpr auto        dann   = ann::get_or<doc>(doc{""}, Anns...);
            constexpr const char *docstr = dann.text;

            if constexpr (!nb_bindable_type<F>) {
                // nanobind can't expose a raw-array or incomplete-type field as a
                // by-value property; drop it rather than failing the build.
            } else if constexpr (ann::has<readonly>(Anns...)) {
                cls.def_prop_ro(name, [](const T &s) -> F { return s.[:Fld:]; }, docstr);
            } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
                constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                cls.def_prop_rw(
                    name, [](const T &s) -> F { return s.[:Fld:]; },
                    [name](T &s, F v) {
                        double d = static_cast<double>(v);
                        if (d < r.min || d > r.max) {
                            throw nb::value_error((std::string(name) + " out of range [" +
                                                   std::to_string(r.min) + ", " +
                                                   std::to_string(r.max) + "]")
                                                      .c_str());
                        }
                        s.[:Fld:] = v;
                    },
                    docstr);
            } else {
                cls.def_prop_rw(
                    name, [](const T &s) -> F { return s.[:Fld:]; },
                    [](T &s, F v) { s.[:Fld:] = v; }, docstr);
            }
        }

        template <std::meta::info Fn, auto... Anns> void method_instance(const char *name) {
            if constexpr (nb_bindable_fn<Fn>()) {
                constexpr auto dann = ann::get_or<doc>(doc{""}, Anns...);
                cls.def(name, &[:Fn:], dann.text);
            }
        }

        template <std::meta::info Fn, auto... Anns> void method_static(const char *name) {
            if constexpr (nb_bindable_fn<Fn>()) {
                constexpr auto dann = ann::get_or<doc>(doc{""}, Anns...);
                cls.def_static(name, &[:Fn:], dann.text);
            }
        }
    };

    template <typename T> inline void bind_nanobind(nb::module_ &m, const char *py_name) {
        nb::class_<T>      cls(m, py_name);
        NanobindVisitor<T> v{cls};
        walk<T>(v);
        // The implicitly-declared default ctor may not be enumerated by
        // reflection; register one so `T()` keeps working. The constructibility
        // test must be `if constexpr`: a plain `if` still instantiates
        // `nb::init<>()` (and nanobind's default-construct) for the discarded
        // branch, which is a hard error for a non-default-constructible type.
        if constexpr (std::is_default_constructible_v<T>) {
            if (!v.saw_default_ctor) {
                cls.def(nb::init<>());
            }
        }
    }

    template <typename T> inline void bind_nanobind_enum(nb::module_ &m, const char *py_name) {
        nb::enum_<T> e(m, py_name);
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^T))) {
            constexpr const char *nm = std::define_static_string(std::meta::identifier_of(en));
            e.value(nm, [:en:]);
        }
    }

} // namespace rosetta
