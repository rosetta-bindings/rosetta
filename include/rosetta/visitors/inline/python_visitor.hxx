namespace rosetta {

    // A type pybind11 can actually represent in a generated signature.
    //
    // Signatures that slip through reflection but have no pybind11 type-caster
    // and abort the build deep inside <pybind11/cast.h>:
    //   * raw C arrays, e.g. a method returning `double (&)[3][3]` — pybind11
    //     only casts scalars / registered classes / pointers, never `T[N]`;
    //   * a class type that is only forward-declared in this translation unit —
    //     pybind11 needs `sizeof` / `typeid` (is_base_of, is_polymorphic, the
    //     ReadableFunctionSignature) on the complete type;
    //   * a pointer to such an incomplete type — registering the conversion
    //     still needs `typeid`/`sizeof` of the *pointee*. Note `sizeof(T*)` is
    //     always valid (pointer size is known), so a pointer must be checked via
    //     its pointee, not the generic completeness clause below.
    template <class P>
    concept py_pointee_ok =
        std::is_void_v<std::remove_cv_t<std::remove_pointer_t<P>>> ||
        requires { sizeof(std::remove_pointer_t<P>); };

    // std::vector<E> is cast element-wise by <pybind11/stl.h>, so it is only
    // representable when its element type is — recurse so e.g.
    // `std::vector<Triangle*>` (pointer to an incomplete type) is rejected just
    // like a bare `Triangle*` would be.
    template <class T> struct py_vector_elem {
        static constexpr bool is = false;
        using type               = void;
    };
    template <class E, class A> struct py_vector_elem<std::vector<E, A>> {
        static constexpr bool is = true;
        using type               = E;
    };

    // The recursion lives in a consteval function (a concept may not refer to
    // itself); `py_bindable_type` is the concept wrapper used in constraints.
    template <class T> consteval bool py_is_bindable() {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
            return false;
        } else if constexpr (std::is_pointer_v<U>) {
            return py_pointee_ok<U>;
        } else if constexpr (py_vector_elem<U>::is) {
            return py_is_bindable<typename py_vector_elem<U>::type>();
        } else {
            return std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_void_v<U> ||
                   requires { sizeof(U); };
        }
    }

    template <class T>
    concept py_bindable_type = py_is_bindable<T>();

    // True when a member function's return type and every parameter type are
    // representable by pybind11; methods that fail this are silently dropped
    // from the binding rather than breaking the whole module.
    template <std::meta::info Fn, std::size_t... Is>
    consteval bool py_params_bindable(std::index_sequence<Is...>) {
        constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
        return (py_bindable_type<typename[:std::meta::type_of(params[Is]):]> && ...);
    }

    template <std::meta::info Fn> consteval bool py_bindable_fn() {
        constexpr auto arity = std::meta::parameters_of(Fn).size();
        return py_bindable_type<typename[:std::meta::return_type_of(Fn):]> &&
               py_params_bindable<Fn>(std::make_index_sequence<arity>{});
    }

    // Templated on the py::class_ instantiation (Cls) rather than py::class_<T>
    // directly, so the same visitor drives both a plain py::class_<T> and a
    // trampoline-backed py::class_<T, Trampoline> — the pybind member API
    // (def / def_property / def_static / def(py::init<>)) is identical for both.
    template <typename T, typename Cls> struct PybindVisitor {
        Cls &cls;
        bool saw_default_ctor = false;

        template <std::meta::info Ctor, auto... Anns> void constructor() {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            constexpr auto dann   = ann::get_or<doc>(doc{""}, Anns...);
            if constexpr (params.size() == 0)
                saw_default_ctor = true;
            // Skip a constructor pybind11 can't represent (a parameter type with no
            // type-caster, e.g. a vector of pointers to an incomplete type) rather
            // than aborting the build inside py::init<>.
            if constexpr (py_params_bindable<Ctor>(std::make_index_sequence<params.size()>{}))
                register_init<Ctor>(std::make_index_sequence<params.size()>{}, dann.text);
        }

      private:
        template <std::meta::info Ctor, std::size_t... Is>
        void register_init(std::index_sequence<Is...>, const char *docstr) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            cls.def(py::init<typename[:std::meta::type_of(params[Is]):]...>(), docstr);
        }

      public:

        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            using F                      = [:std::meta::type_of(Fld):];
            constexpr auto        dann   = ann::get_or<doc>(doc{""}, Anns...);
            constexpr const char *docstr = dann.text;

            if constexpr (!py_bindable_type<F>) {
                // pybind11 can't expose a raw-array or incomplete-type field as a
                // by-value property; drop it rather than failing the build.
            } else if constexpr (ann::has<readonly>(Anns...)) {
                cls.def_property_readonly(name, [](const T &s) -> F { return s.[:Fld:]; }, docstr);
            } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
                constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                cls.def_property(
                    name, [](const T &s) -> F { return s.[:Fld:]; },
                    [name](T &s, F v) {
                        double d = static_cast<double>(v);
                        if (d < r.min || d > r.max) {
                            throw py::value_error(std::string(name) + " out of range [" +
                                                  std::to_string(r.min) + ", " +
                                                  std::to_string(r.max) + "]");
                        }
                        s.[:Fld:] = v;
                    },
                    docstr);
            } else {
                cls.def_property(
                    name, [](const T &s) -> F { return s.[:Fld:]; },
                    [](T &s, F v) { s.[:Fld:] = v; }, docstr);
            }
        }

        template <std::meta::info Fn, auto... Anns> void method_instance(const char *name) {
            if constexpr (py_bindable_fn<Fn>()) {
                constexpr auto dann = ann::get_or<doc>(doc{""}, Anns...);
                cls.def(name, &[:Fn:], dann.text);
            }
        }

        template <std::meta::info Fn, auto... Anns> void method_static(const char *name) {
            if constexpr (py_bindable_fn<Fn>()) {
                constexpr auto dann = ann::get_or<doc>(doc{""}, Anns...);
                cls.def_static(name, &[:Fn:], dann.text);
            }
        }
    };

    // Run the reflection walk against an already-declared class_ binding.
    // `Trampoline` is the type pybind actually instantiates (the trampoline when
    // one is supplied, else T), so the default-ctor probe checks the right type.
    template <typename T, typename Trampoline, typename Cls>
    inline void bind_pybind_into(Cls &cls) {
        PybindVisitor<T, Cls> v{cls};
        walk<T>(v);
        // The implicitly-declared default ctor may not be enumerated by
        // reflection; register one so `T()` keeps working. The constructibility
        // test must be `if constexpr`: a plain `if` still instantiates
        // `py::init<>()` (and pybind's `new Trampoline{}`) for the discarded
        // branch, which is a hard error for a non-default-constructible type.
        if constexpr (std::is_default_constructible_v<Trampoline>) {
            if (!v.saw_default_ctor) {
                cls.def(py::init<>());
            }
        }
    }

    // Bind T. When a `Trampoline` type is supplied (a subclass of T with
    // PYBIND11_OVERRIDE shims for T's virtuals — emitted by the python backend),
    // the class is registered as py::class_<T, Trampoline> so Python subclasses
    // can override those virtuals and C++ dispatches back into Python. With no
    // trampoline (the default) this is the plain py::class_<T> binding as before.
    template <typename T, typename Trampoline, typename... Bases> // default `= T` on the declaration
    inline void bind_pybind(py::module_ &m, const char *py_name) {
        // pybind11 sorts the extra py::class_ template args itself: a base of T is
        // registered as a base, the trampoline (a subclass of T) as the alias — so
        // `Bases...` and `Trampoline` may be listed together in any order.
        if constexpr (std::is_same_v<Trampoline, T>) {
            py::class_<T, Bases...> cls(m, py_name);
            bind_pybind_into<T, T>(cls);
        } else {
            py::class_<T, Bases..., Trampoline> cls(m, py_name);
            bind_pybind_into<T, Trampoline>(cls);
        }
    }

    template <typename T> inline void bind_pybind_enum(py::module_ &m, const char *py_name) {
        // No export_values(): enumerators are accessed as `Name.Value`, matching
        // scoped-enum semantics and avoiding polluting the module namespace
        // (an enumerator could otherwise collide with a bound class name).
        py::enum_<T> e(m, py_name);
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^T))) {
            constexpr const char *nm = std::define_static_string(std::meta::identifier_of(en));
            e.value(nm, [:en:]);
        }
    }

} // namespace rosetta
