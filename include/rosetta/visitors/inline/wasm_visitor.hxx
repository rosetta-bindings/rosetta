namespace rosetta {

    // A type embind can represent in a generated signature. Raw C arrays
    // (e.g. a method returning `double (&)[3][3]`) have no embind binding, and a
    // class returned/taken by value or reference must be complete (sizeof).
    // A pointer is judged by its pointee — `sizeof(T*)` is always valid, so the
    // generic clause would wrongly accept a pointer to an incomplete type; embind
    // still needs the pointed-to class registered. A std::vector is marshalled
    // element-wise, so it recurses into its element type.
    template <class P>
    concept wasm_pointee_ok =
        std::is_void_v<std::remove_cv_t<std::remove_pointer_t<P>>> ||
        requires { sizeof(std::remove_pointer_t<P>); };

    template <class T> struct wasm_vector_elem {
        static constexpr bool is = false;
        using type               = void;
    };
    template <class E, class A> struct wasm_vector_elem<std::vector<E, A>> {
        static constexpr bool is = true;
        using type               = E;
    };

    template <class T> consteval bool wasm_is_bindable() {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
            return false;
        } else if constexpr (std::is_pointer_v<U>) {
            return wasm_pointee_ok<U>;
        } else if constexpr (wasm_vector_elem<U>::is) {
            return wasm_is_bindable<typename wasm_vector_elem<U>::type>();
        } else {
            return std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_void_v<U> ||
                   requires { sizeof(U); };
        }
    }

    template <class T>
    concept wasm_bindable_type = wasm_is_bindable<T>();

    template <std::meta::info Fn, std::size_t... Is>
    consteval bool wasm_params_bindable(std::index_sequence<Is...>) {
        constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
        return (wasm_bindable_type<typename[:std::meta::type_of(params[Is]):]> && ...);
    }

    template <std::meta::info Fn> consteval bool wasm_bindable_fn() {
        constexpr auto arity = std::meta::parameters_of(Fn).size();
        return wasm_bindable_type<typename[:std::meta::return_type_of(Fn):]> &&
               wasm_params_bindable<Fn>(std::make_index_sequence<arity>{});
    }

    template <typename T> struct EmscriptenVisitor {
        emscripten::class_<T> &cls;
        bool                   saw_default_ctor = false;

        template <std::meta::info Ctor, auto... /*Anns*/> void constructor() {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            if constexpr (params.size() == 0)
                saw_default_ctor = true;
            // Skip a constructor embind can't represent (a parameter with no
            // marshalling, e.g. a vector of pointers to an incomplete type).
            if constexpr (wasm_params_bindable<Ctor>(std::make_index_sequence<params.size()>{}))
                register_ctor<Ctor>(std::make_index_sequence<params.size()>{});
        }

        template <std::meta::info Fld, auto... Anns> void field(const char * name) {
            using F = [:std::meta::type_of(Fld):];

            if constexpr (!wasm_bindable_type<F>) {
                // embind can't expose a raw-array or incomplete-type field as a
                // by-value property; drop it rather than failing the build.
            } else if constexpr (ann::has<readonly>(Anns...)) {
                // Pair the getter with a throwing setter so JS-side assignment
                // surfaces a clear error (embind silently no-ops a getter-only
                // accessor in non-strict mode).
                cls.property(
                    name, +[](const T &s) -> F { return s.[:Fld:]; },
                    +[](T &, F) {
                        constexpr auto fname =
                            std::define_static_string(std::meta::identifier_of(Fld));
                        throw std::runtime_error(std::string(fname) + " is read-only");
                    });
            } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
                constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                cls.property(
                    name, +[](const T &s) -> F { return s.[:Fld:]; },
                    +[](T &s, F v) {
                        constexpr auto fname =
                            std::define_static_string(std::meta::identifier_of(Fld));
                        double d = static_cast<double>(v);
                        if (d < r.min || d > r.max) {
                            throw std::runtime_error(std::string(fname) + " out of range");
                        }
                        s.[:Fld:] = v;
                    });
            } else {
                cls.property(
                    name, +[](const T &s) -> F { return s.[:Fld:]; },
                    +[](T &s, F v) { s.[:Fld:] = v; });
            }
        }

        template <std::meta::info Fn, auto... /*Anns*/> void method_instance(const char *name) {
            if constexpr (wasm_bindable_fn<Fn>()) {
                cls.function(name, &[:Fn:]);
            }
        }

        template <std::meta::info Fn, auto... /*Anns*/> void method_static(const char *name) {
            if constexpr (wasm_bindable_fn<Fn>()) {
                cls.class_function(name, &[:Fn:]);
            }
        }

      private:
        template <std::meta::info Ctor, std::size_t... Is>
        void register_ctor(std::index_sequence<Is...>) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            cls.template constructor<typename[:std::meta::type_of(params[Is]):]...>();
        }
    };

    template <typename T> void bind_wasm(const char *class_name) {
        emscripten::class_<T> cls(class_name);
        EmscriptenVisitor<T>  v{cls};
        walk<T>(v);
        // The implicitly-declared default ctor may not be enumerated by
        // reflection; register one so `new Module.T()` keeps working. The
        // constructibility test must be `if constexpr`: a plain `if` still
        // instantiates `constructor<>()` (embind's default-construct) for the
        // discarded branch, a hard error for a non-default-constructible type.
        if constexpr (std::is_default_constructible_v<T>) {
            if (!v.saw_default_ctor) {
                cls.template constructor<>();
            }
        }
    }

    template <typename T> void bind_wasm_enum(const char *enum_name) {
        emscripten::enum_<T> e(enum_name);
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^T))) {
            constexpr const char *nm = std::define_static_string(std::meta::identifier_of(en));
            e.value(nm, [:en:]);
        }
    }

} // namespace rosetta
