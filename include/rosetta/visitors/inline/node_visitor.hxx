namespace rosetta {

    // ---- Type classification ----

    template <typename T> struct is_std_vector : std::false_type {};
    template <typename U, typename A> struct is_std_vector<std::vector<U, A>> : std::true_type {};

    template <typename T> struct is_std_function : std::false_type {};
    template <typename R, typename... A>
    struct is_std_function<std::function<R(A...)>> : std::true_type {};

    template <typename T> struct is_std_array : std::false_type {};
    template <typename U, std::size_t N> struct is_std_array<std::array<U, N>> : std::true_type {};

    // Is a std::function<R(A...)> marshalable as a JS callback? Forward-declared
    // here so napi_supported can consult it; defined just below (it recurses back
    // into napi_supported to check the return and argument types).
    template <typename T> consteval bool napi_callback_marshalable();

    // A raw pointer to a class that is *complete* in this TU — marshalled as a
    // non-owning JS handle to the pointed-to object. Completeness is required so
    // Wrap<pointee> can be instantiated (unique_ptr member, ObjectWrap machinery);
    // a pointer to a merely forward-declared class (e.g. Triangle here) is left
    // unsupported. The pointee may be abstract (a referenced-only base such as
    // BaseRemote) — that is fine, it is never constructed on this path.
    template <class U>
    concept napi_class_ptr =
        std::is_pointer_v<U> && std::is_class_v<std::remove_cv_t<std::remove_pointer_t<U>>> &&
        requires { sizeof(std::remove_cv_t<std::remove_pointer_t<U>>); };

    // A class marshalled BY VALUE (wrapped Napi object). The conversion
    // default-constructs a wrapper and copy-assigns the value in/out, so the
    // class must be complete, default-constructible and copy-assignable. The
    // `sizeof` clause comes first so the later traits are never evaluated on an
    // incomplete type (e.g. a forward-declared ModelRegions returned by value),
    // which would be a hard error rather than "unsupported".
    template <class U>
    concept napi_value_class = std::is_class_v<U> && requires { sizeof(U); } &&
                               std::is_default_constructible_v<U> && std::is_copy_assignable_v<U>;

    // Can a value of T cross the N-API boundary? Scalars, strings, vectors and
    // fixed arrays of supported types, JS-callback-shaped std::function params,
    // and user class types (marshalled as wrapped objects) are convertible;
    // other types are not.
    template <typename T> consteval bool napi_supported() {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, bool> ||
                      std::is_arithmetic_v<U>) {
            return true;
        } else if constexpr (is_std_function<U>::value) {
            // A callback param: bindable when its whole signature marshals (a JS
            // function is wrapped into the std::function by from_napi).
            return napi_callback_marshalable<U>();
        } else if constexpr (is_std_vector<U>::value) {
            return napi_supported<typename U::value_type>();
        } else if constexpr (is_std_array<U>::value) {
            return napi_supported<typename U::value_type>();
        } else if constexpr (std::is_enum_v<U>) {
            return true; // marshalled as its underlying integer
        } else if constexpr (napi_class_ptr<U>) {
            return true; // raw pointer to a bound class → non-owning JS handle
        } else if constexpr (std::is_class_v<U>) {
            // By-value user type — supported only when it can actually round-trip
            // (a non-copyable/incomplete helper like ModelRegions is skipped;
            // pass such types by pointer instead).
            return napi_value_class<U>;
        } else {
            return false;
        }
    }

    // A std::function<R(A...)> crosses the boundary iff its return type is void or
    // marshalable and every argument type is marshalable. The pointer parameter
    // is only a compile-time pattern to recover R and A... (never dereferenced).
    template <typename R, typename... A>
    consteval bool napi_callback_marshalable_impl(std::function<R(A...)> *) {
        bool ok = std::is_void_v<R> || napi_supported<std::remove_cvref_t<R>>();
        ((ok = ok && napi_supported<std::remove_cvref_t<A>>()), ...);
        return ok;
    }
    template <typename T> consteval bool napi_callback_marshalable() {
        return napi_callback_marshalable_impl(static_cast<T *>(nullptr));
    }

    // ---- Forward declarations (mutually recursive with conversions) ----

    template <typename T, typename Tramp = T> class Wrap;
    template <typename T> Napi::FunctionReference &ctor_ref();

    // Wrap a JS function value into an owning std::function of the requested
    // signature (defined after from_napi, which it uses to convert the callback's
    // return value). Used by from_napi for std::function parameters.
    template <typename Fn> Fn make_js_callback(const Napi::Value &v);

    // ---- Virtual-method trampoline support (JS subclasses override C++) ----

    // Base mixin for a generated trampoline subclass (`class Js_T : public T,
    // public NapiTrampoline`). Holds a *weak* handle to the JS object that wraps
    // this C++ instance — weak so it doesn't keep the object alive (the JS object
    // already owns this C++ instance, so a strong ref would be a cycle). Wrap<>
    // sets it right after construction.
    class NapiTrampoline {
      public:
        void __rosetta_set_self(Napi::Object self) {
            self_     = Napi::Weak(self);
            has_self_ = true;
        }
        bool         __rosetta_has_self() const { return has_self_ && !self_.IsEmpty(); }
        Napi::Object __rosetta_self() const { return self_.Value(); }

      private:
        Napi::ObjectReference self_;
        bool                  has_self_ = false;
    };

    // name -> the bound C++ prototype function for T's virtual methods. Used to
    // tell "JS subclass overrode this" from "JS inherited our bound method":
    // strict-equality against this entry. Populated by bind_napi after DefineClass.
    template <typename T>
    inline std::unordered_map<std::string, Napi::FunctionReference> &napi_override_guard() {
        static std::unordered_map<std::string, Napi::FunctionReference> m;
        return m;
    }

    // Constructor dispatch table for T, keyed by argument count. Populated by
    // bind_napi<T> (via the walk) and consulted by Wrap<T>'s constructor.
    // NOTE: dispatch is by arity only — two constructors with the same number
    // of parameters but different types cannot be told apart here.
    template <typename T>
    std::unordered_map<std::size_t, std::function<T *(const Napi::CallbackInfo &)>> &ctor_table() {
        // Heap-constructs T from the JS args (returns an owning raw pointer the
        // caller adopts). Building on the heap — rather than returning by value —
        // avoids requiring T be movable, and lets Wrap hold it in a unique_ptr
        // whose element type may be abstract (a referenced-only base like
        // BaseRemote) without instantiating an impossible value member.
        static std::unordered_map<std::size_t, std::function<T *(const Napi::CallbackInfo &)>> table;
        return table;
    }

    // ---- Type conversion helpers ----

    template <typename T> Napi::Value to_napi(Napi::Env env, const T &v) {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, std::string>) {
            return Napi::String::New(env, v);
        } else if constexpr (std::is_same_v<U, bool>) {
            return Napi::Boolean::New(env, v);
        } else if constexpr (std::is_floating_point_v<U> || std::is_integral_v<U>) {
            return Napi::Number::New(env, static_cast<double>(v));
        } else if constexpr (is_std_vector<U>::value || is_std_array<U>::value) {
            Napi::Array arr = Napi::Array::New(env, v.size());
            for (std::size_t i = 0; i < v.size(); ++i) {
                arr.Set(static_cast<uint32_t>(i), to_napi(env, v[i]));
            }
            return arr;
        } else if constexpr (is_std_function<U>::value) {
            static_assert(sizeof(U) == 0,
                          "to_napi: cannot marshal a std::function out to JS (callbacks "
                          "are only supported as inbound parameters)");
        } else if constexpr (std::is_enum_v<U>) {
            return Napi::Number::New(
                env, static_cast<double>(static_cast<std::underlying_type_t<U>>(v)));
        } else if constexpr (napi_class_ptr<U>) {
            // Raw pointer to a bound class: wrap the EXISTING object non-owningly
            // (the C++ side keeps ownership). A fresh JS wrapper is made, then its
            // `self_` is retargeted at `v` so method/field access operates on the
            // pointed-to object instead of the wrapper's own `inner`.
            using P = std::remove_cv_t<std::remove_pointer_t<U>>;
            if (v == nullptr) {
                return env.Null();
            }
            Napi::Object obj              = ctor_ref<P>().New({});
            Wrap<P>::Unwrap(obj)->self_ = const_cast<P *>(v);
            return obj;
        } else if constexpr (napi_value_class<U>) {
            // User type: build a fresh wrapped object and copy the value in.
            Napi::Object obj = ctor_ref<U>().New({});
            Wrap<U>::Unwrap(obj)->obj() = v;
            return obj;
        } else {
            static_assert(sizeof(T) == 0, "to_napi: unsupported type");
        }
    }

    template <typename T> T from_napi(const Napi::Value &v) {
        if constexpr (std::is_same_v<T, std::string>) {
            return v.As<Napi::String>().Utf8Value();
        } else if constexpr (std::is_same_v<T, bool>) {
            return v.As<Napi::Boolean>().Value();
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(v.As<Napi::Number>().DoubleValue());
        } else if constexpr (std::is_integral_v<T>) {
            return static_cast<T>(v.As<Napi::Number>().Int64Value());
        } else if constexpr (is_std_vector<T>::value) {
            using Elem      = typename T::value_type;
            Napi::Array arr = v.As<Napi::Array>();
            T           out;
            out.reserve(arr.Length());
            for (uint32_t i = 0; i < arr.Length(); ++i) {
                out.push_back(from_napi<Elem>(arr.Get(i)));
            }
            return out;
        } else if constexpr (is_std_array<T>::value) {
            Napi::Array arr = v.As<Napi::Array>();
            T           out{};
            for (std::size_t i = 0; i < out.size(); ++i) {
                out[i] = from_napi<typename T::value_type>(arr.Get(static_cast<uint32_t>(i)));
            }
            return out;
        } else if constexpr (is_std_function<T>::value) {
            return make_js_callback<T>(v);
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(
                static_cast<std::underlying_type_t<T>>(v.As<Napi::Number>().Int64Value()));
        } else if constexpr (napi_class_ptr<T>) {
            // Raw pointer param: hand back the address of the wrapped object (or
            // null). Non-owning — the JS side keeps owning its wrapper.
            using P = std::remove_cv_t<std::remove_pointer_t<T>>;
            if (v.IsNull() || v.IsUndefined()) {
                return static_cast<T>(nullptr);
            }
            return static_cast<T>(&Wrap<P>::Unwrap(v.As<Napi::Object>())->obj());
        } else if constexpr (napi_value_class<T>) {
            // User type: unwrap the wrapped object and copy the value out.
            return Wrap<T>::Unwrap(v.As<Napi::Object>())->obj();
        } else {
            static_assert(sizeof(T) == 0, "from_napi: unsupported type");
        }
    }

    // Materialize a std::function<R(A...)> that forwards to a JS callback. The JS
    // function is kept alive by a persistent reference (shared so the resulting
    // std::function stays copyable); each C++ invocation marshals the arguments
    // out with to_napi and the return value back with from_napi. Calls run
    // synchronously on the JS thread — valid because bound C++ methods invoke the
    // callback during a JS-initiated call (e.g. a solver.run()), not from another
    // thread. The pointer parameter only carries R and A... (never dereferenced).
    template <typename R, typename... A>
    std::function<R(A...)> make_js_callback_impl(const Napi::Value &v, std::function<R(A...)> *) {
        Napi::Env env = v.Env();
        auto      ref = std::make_shared<Napi::FunctionReference>(
            Napi::Persistent(v.As<Napi::Function>()));
        return [ref, env](A... args) -> R {
            Napi::HandleScope scope(env);
            Napi::Value       r = ref->Call({to_napi(env, args)...});
            if constexpr (std::is_void_v<R>) {
                (void)r;
            } else {
                return from_napi<std::remove_cvref_t<R>>(r);
            }
        };
    }
    template <typename Fn> Fn make_js_callback(const Napi::Value &v) {
        return make_js_callback_impl(v, static_cast<Fn *>(nullptr));
    }

    // A wrapped user class type — i.e. one marshalled as a JS object holding an
    // `inner` C++ value (everything class-like except the strings/containers we
    // convert by value).
    template <typename T> consteval bool is_napi_user_class() {
        using U = std::remove_cvref_t<T>;
        return std::is_class_v<U> && !std::is_same_v<U, std::string> &&
               !is_std_vector<U>::value && !is_std_array<U>::value &&
               !is_std_function<U>::value;
    }

    // Materialize a call argument from a JS value for a reflected parameter of
    // declared type P (which keeps its cv-ref qualifiers). For an lvalue-reference
    // parameter of a wrapped user type, bind directly to the persistent `inner`
    // object held by the JS wrapper: this lets functions that take the object by
    // reference (e.g. PMP algorithms taking `SurfaceMesh&`) mutate the caller's
    // object in place, and avoids copying for `const T&`. Everything else is
    // produced by value via from_napi (a temporary, which still binds to by-value
    // and `const T&` parameters).
    template <typename P> decltype(auto) arg_from_napi(const Napi::Value &v) {
        using U = std::remove_cvref_t<P>;
        if constexpr (std::is_lvalue_reference_v<P> && is_napi_user_class<U>()) {
            return (Wrap<U>::Unwrap(v.As<Napi::Object>())->obj()); // -> U& into the wrapper
        } else {
            return from_napi<U>(v);
        }
    }

    // Has the JS object overridden `name`, rather than inheriting T's bound C++
    // method? True iff the function it would call differs from the one we bound
    // onto the prototype. This is the recursion guard: without it, a non-override
    // would bounce C++ -> JS -> bound C++ -> trampoline -> JS -> ... forever.
    template <typename T> inline bool napi_is_overridden(Napi::Object self, const char *name) {
        Napi::Value f = self.Get(name);
        if (!f.IsFunction()) {
            return false;
        }
        auto &guard = napi_override_guard<T>();
        auto  it    = guard.find(name);
        if (it == guard.end()) {
            return false; // not a tracked virtual — treat as not overridden
        }
        return !f.StrictEquals(it->second.Value());
    }

    // C++ virtual call landed in the trampoline: route to the JS override if the
    // JS subclass defines one, otherwise fall back to the C++ base via `base`.
    template <typename T, typename Ret, typename Base, typename... Args>
    inline Ret napi_call_override(const NapiTrampoline &self, const char *name, Base base,
                                  const Args &...args) {
        if (self.__rosetta_has_self()) {
            Napi::Object obj = self.__rosetta_self();
            if (napi_is_overridden<T>(obj, name)) {
                Napi::Value r =
                    obj.Get(name).template As<Napi::Function>().Call(obj, {to_napi(obj.Env(),
                                                                                   args)...});
                if constexpr (std::is_void_v<Ret>) {
                    return;
                } else {
                    return from_napi<Ret>(r);
                }
            }
        }
        return base();
    }

    // Same, for a pure virtual: there is no C++ base to fall back to, so a
    // missing JS override is an error rather than a silent no-op.
    template <typename T, typename Ret, typename... Args>
    inline Ret napi_call_override_pure(const NapiTrampoline &self, const char *name,
                                       const Args &...args) {
        if (self.__rosetta_has_self()) {
            Napi::Object obj = self.__rosetta_self();
            if (napi_is_overridden<T>(obj, name)) {
                Napi::Value r =
                    obj.Get(name).template As<Napi::Function>().Call(obj, {to_napi(obj.Env(),
                                                                                   args)...});
                if constexpr (std::is_void_v<Ret>) {
                    return;
                } else {
                    return from_napi<Ret>(r);
                }
            }
            throw Napi::Error::New(obj.Env(), std::string("rosetta: pure virtual '") + name +
                                                  "' is not overridden in JS");
        }
        throw std::runtime_error(std::string("rosetta: pure virtual '") + name +
                                 "' called before the JS object was bound");
    }

    // ---- CRTP wrapper template ----

    template <typename T, typename Tramp> class Wrap : public Napi::ObjectWrap<Wrap<T, Tramp>> {
    public:
        // `self_` is the object all field/method access goes through, via obj().
        // It is the FIRST data member so its offset is independent of `Tramp` —
        // to_napi<T*> retargets it through a possibly-mismatched Wrap<T,T> view
        // (it only knows T, not the trampoline), which is only sound at a fixed
        // offset. Normally self_ == &inner (this wrapper owns the object); for a
        // wrapper produced from a raw T* it points at the external, non-owned
        // object instead, and `inner` stays an unused default-constructed slot.
        T *self_ = nullptr;

        // Owned storage, engaged only when this wrapper owns the object. It holds
        // the trampoline subclass when one was supplied (so its vtable routes
        // virtuals back into JS), otherwise plain T; either way it IS-A T. Access
        // is always through obj() (never this directly), so a non-owning wrapper
        // works too. A unique_ptr (rather than a bare `Tramp inner` value member)
        // lets Tramp be non-default-constructible (built from the JS ctor args)
        // or an abstract referenced-only base (held but never constructed here).
        std::unique_ptr<Tramp> owned_;

        T       &obj() { return *self_; }
        const T &obj() const { return *self_; }

        Wrap(const Napi::CallbackInfo &info) : Napi::ObjectWrap<Wrap<T, Tramp>>(info) {
            // Build the concrete held type (Tramp) from the JS arguments when the
            // arity matches a registered constructor; fall back to a default ctor
            // for a 0-arg call. A raw-pointer wrapper (to_napi<T*>) is made with 0
            // args and then retargets self_ at the external object, so it is fine
            // for owned_ to stay null here (e.g. a non-constructible/abstract T).
            auto &tbl = ctor_table<Tramp>();
            auto  it  = tbl.find(info.Length());
            if (it != tbl.end()) {
                owned_.reset(it->second(info));
            } else if (info.Length() == 0) {
                if constexpr (std::is_default_constructible_v<Tramp> &&
                              !std::is_abstract_v<Tramp>) {
                    owned_ = std::make_unique<Tramp>();
                }
            } else {
                throw Napi::TypeError::New(info.Env(),
                                           "no matching constructor for " +
                                               std::to_string(info.Length()) + " argument(s)");
            }
            if (owned_) {
                self_ = static_cast<T *>(owned_.get());
                // Give the trampoline a handle to its JS object so virtual
                // overrides can dispatch into JS (no-op when Tramp == T).
                if constexpr (!std::is_same_v<T, Tramp>) {
                    owned_->__rosetta_set_self(this->Value());
                }
            }
        }

        template <std::meta::info Fld>
        Napi::Value get_field(const Napi::CallbackInfo &info) {
            return to_napi(info.Env(), obj().[:Fld:]);
        }

        template <std::meta::info Fld>
        void set_field(const Napi::CallbackInfo &info, const Napi::Value &v) {
            using FieldT  = [:std::meta::type_of(Fld):];
            obj().[:Fld:] = from_napi<FieldT>(v);
        }

        template <std::meta::info Fld, rosetta::range R>
        void set_field_ranged(const Napi::CallbackInfo &info, const Napi::Value &v) {
            using FieldT        = [:std::meta::type_of(Fld):];
            constexpr auto name = std::define_static_string(std::meta::identifier_of(Fld));
            FieldT         val  = from_napi<FieldT>(v);
            double         d    = static_cast<double>(val);
            if (d < R.min || d > R.max) {
                throw Napi::RangeError::New(info.Env(), std::string(name) + " out of range");
            }
            obj().[:Fld:] = val;
        }

        template <std::meta::info Fld>
        void set_field_readonly(const Napi::CallbackInfo &info, const Napi::Value & /*v*/) {
            constexpr auto name = std::define_static_string(std::meta::identifier_of(Fld));
            throw Napi::TypeError::New(info.Env(), std::string(name) + " is read-only");
        }

        template <std::meta::info Fn>
        Napi::Value call_method(const Napi::CallbackInfo &info) {
            return call_method_impl<Fn>(
                info, std::make_index_sequence<
                          std::define_static_array(std::meta::parameters_of(Fn)).size()>{});
        }

        template <std::meta::info Fn>
        static Napi::Value call_static(const Napi::CallbackInfo &info) {
            return call_static_impl<Fn>(
                info, std::make_index_sequence<
                          std::define_static_array(std::meta::parameters_of(Fn)).size()>{});
        }

    private:
        template <std::meta::info Fn, std::size_t... Is>
        Napi::Value call_method_impl(const Napi::CallbackInfo &info,
                                     std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (std::is_void_v<R>) {
                (obj().[:Fn:])(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
                return info.Env().Undefined();
            } else {
                R r = (obj().[:Fn:])(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
                return to_napi(info.Env(), r);
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        static Napi::Value call_static_impl(const Napi::CallbackInfo &info,
                                            std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (std::is_void_v<R>) {
                ([:Fn:])(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
                return info.Env().Undefined();
            } else {
                R r = ([:Fn:])(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
                return to_napi(info.Env(), r);
            }
        }
    };

    // Per-type persistent constructor, set by bind_napi<T> and used by
    // to_napi to build fresh wrapped instances of user types.
    template <typename T> Napi::FunctionReference &ctor_ref() {
        static Napi::FunctionReference ref;
        return ref;
    }

    // ---- Member support predicate ----

    // A method is bindable only if its return type and every parameter type
    // can cross the N-API boundary; otherwise the walk skips it.
    template <std::meta::info Fn> consteval bool method_supported() {
        using Ret = [:std::meta::return_type_of(Fn):];
        bool ok   = std::is_void_v<Ret> || napi_supported<std::remove_cvref_t<Ret>>();
        constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((ok = ok &&
                   napi_supported<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()),
             ...);
        }(std::make_index_sequence<params.size()>{});
        return ok;
    }

    // A constructor is bindable only if every parameter type can cross the
    // N-API boundary.
    template <std::meta::info Ctor> consteval bool ctor_supported() {
        bool           ok     = true;
        constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((ok = ok &&
                   napi_supported<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>()),
             ...);
        }(std::make_index_sequence<params.size()>{});
        return ok;
    }

    // ---- Visitor ----

    template <typename T, typename Tramp = T> struct NapiVisitor {
        using This = Wrap<T, Tramp>;
        std::vector<Napi::ClassPropertyDescriptor<This>> &props;
        // Names of the virtual instance methods bound on this class — used by
        // bind_napi to populate the override guard. Null when no trampoline.
        std::vector<std::string> *virtual_names = nullptr;

        template <std::meta::info Ctor, auto... /*Anns*/> void constructor() {
            if constexpr (ctor_supported<Ctor>()) {
                register_ctor<Ctor>(
                    std::make_index_sequence<
                        std::define_static_array(std::meta::parameters_of(Ctor)).size()>{});
            }
        }

        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            using F = [:std::meta::type_of(Fld):];

            if constexpr (!napi_supported<std::remove_cvref_t<F>>()) {
                // Field type cannot cross the boundary — skip it.
            } else if constexpr (ann::has<readonly>(Anns...)) {
                props.push_back(
                    This::template InstanceAccessor<&This::template get_field<Fld>,
                                                    &This::template set_field_readonly<Fld>>(
                        name));
            } else if constexpr (ann::has<range>(Anns...) && std::is_arithmetic_v<F>) {
                constexpr auto r = ann::get_or<range>(range{0, 0}, Anns...);
                props.push_back(
                    This::template InstanceAccessor<&This::template get_field<Fld>,
                                                    &This::template set_field_ranged<Fld, r>>(
                        name));
            } else {
                props.push_back(
                    This::template InstanceAccessor<&This::template get_field<Fld>,
                                                    &This::template set_field<Fld>>(name));
            }
        }

        template <std::meta::info Fn, auto... Anns> void method_instance(const char *name) {
            if constexpr (method_supported<Fn>()) {
                props.push_back(
                    This::template InstanceMethod<&This::template call_method<Fn>>(name));
                if constexpr (ann::has<virtual_spec>(Anns...)) {
                    if (virtual_names) {
                        virtual_names->push_back(name);
                    }
                }
            }
        }

        template <std::meta::info Fn, auto... /*Anns*/> void method_static(const char *name) {
            if constexpr (method_supported<Fn>()) {
                props.push_back(
                    This::template StaticMethod<&This::template call_static<Fn>>(name));
            }
        }

      private:
        // Build the concrete held type (Tramp, which equals T when there's no
        // trampoline) from the JS arguments. Building Tramp rather than T means
        // this works for an abstract T, and gives the instance the trampoline's
        // vtable. Tramp inherits T's constructors (`using T::T`). A named static
        // template (not a lambda) so the std::function stores a plain function
        // pointer — capturing the consteval-only param pack would be ill-formed.
        template <std::meta::info Ctor, std::size_t... Is>
        static Tramp *construct(const Napi::CallbackInfo &info) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Ctor));
            return new Tramp(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
        }

        template <std::meta::info Ctor, std::size_t... Is>
        void register_ctor(std::index_sequence<Is...>) {
            ctor_table<Tramp>()[sizeof...(Is)] = &construct<Ctor, Is...>;
        }
    };

    // Bind T. When a `Trampoline` subclass is supplied (generated by the node
    // backend for classes with virtual methods), the wrapper holds it instead of
    // a plain T, so a JS subclass can override T's virtuals and C++ dispatches
    // back into JS. With no trampoline (the default) this is the prior behaviour.
    template <typename T, typename Trampoline> // default `= T` is on the declaration
    inline Napi::Function bind_napi(Napi::Env env, const char *class_name) {
        using This = Wrap<T, Trampoline>;
        std::vector<Napi::ClassPropertyDescriptor<This>> props;
        std::vector<std::string>                         vnames;
        NapiVisitor<T, Trampoline> v{props, &vnames};
        walk<T>(v);
        Napi::Function ctor = This::DefineClass(env, class_name, props);
        // Persist the constructor so to_napi can build instances of T later.
        ctor_ref<T>() = Napi::Persistent(ctor);
        ctor_ref<T>().SuppressDestruct();
        // Record each virtual method's bound prototype function so the trampoline
        // can distinguish a JS override from the inherited C++ binding.
        if constexpr (!std::is_same_v<T, Trampoline>) {
            Napi::Object proto = ctor.Get("prototype").As<Napi::Object>();
            auto        &guard = napi_override_guard<T>();
            for (const auto &n : vnames) {
                Napi::Value f = proto.Get(n);
                if (f.IsFunction()) {
                    guard[n] = Napi::Persistent(f.As<Napi::Function>());
                    guard[n].SuppressDestruct();
                }
            }
        }
        return ctor;
    }

    template <typename T> inline Napi::Object bind_napi_enum(Napi::Env env) {
        Napi::Object obj = Napi::Object::New(env);
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^T))) {
            constexpr const char *nm = std::define_static_string(std::meta::identifier_of(en));
            obj.Set(nm, Napi::Number::New(
                            env, static_cast<double>(static_cast<std::underlying_type_t<T>>(
                                     [:en:]))));
        }
        obj.Freeze();
        return obj;
    }

    // ---- Free function support ----

    template <std::meta::info F, std::size_t... Is>
    inline Napi::Value napi_free_call(const Napi::CallbackInfo &info, std::index_sequence<Is...>) {
        using R               = [:std::meta::return_type_of(F):];
        constexpr auto params = std::define_static_array(std::meta::parameters_of(F));
        if constexpr (std::is_void_v<R>) {
            ([:F:])(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
            return info.Env().Undefined();
        } else {
            R r = ([:F:])(arg_from_napi<typename[:std::meta::type_of(params[Is]):]>(info[Is])...);
            return to_napi(info.Env(), r);
        }
    }

    template <std::meta::info F> inline Napi::Value napi_free_entry(const Napi::CallbackInfo &info) {
        constexpr auto arity = std::define_static_array(std::meta::parameters_of(F)).size();
        return napi_free_call<F>(info, std::make_index_sequence<arity>{});
    }

    // Can this free function's whole signature cross the N-API boundary? True iff
    // the return type is void or marshalable and every parameter type is
    // marshalable. Functions with e.g. pointer parameters (raw out-params),
    // std::function callbacks, or other unsupported types are not bindable and are
    // skipped rather than breaking the build (see bind_napi_function).
    template <std::meta::info F> consteval bool napi_free_supported() {
        using R = [:std::meta::return_type_of(F):];
        if (!(std::is_void_v<R> || napi_supported<R>())) {
            return false;
        }
        bool ok = true;
        template for (constexpr auto p :
                      std::define_static_array(std::meta::parameters_of(F))) {
            if (!napi_supported<typename[:std::meta::type_of(p):]>()) {
                ok = false;
            }
        }
        return ok;
    }

    template <std::meta::info F>
    inline Napi::Function bind_napi_function(Napi::Env env, const char *name) {
        if constexpr (napi_free_supported<F>()) {
            return Napi::Function::New(env, &napi_free_entry<F>, name);
        } else {
            // Unmarshalable signature: bind a stub that throws if called, so the
            // module still loads and every other function stays usable.
            return Napi::Function::New(
                env,
                [](const Napi::CallbackInfo &info) -> Napi::Value {
                    Napi::Error::New(
                        info.Env(),
                        "rosetta: this function is not bindable for the Node backend "
                        "(a parameter or return type cannot cross the N-API boundary — "
                        "e.g. a pointer out-parameter, std::function, or unregistered type)")
                        .ThrowAsJavaScriptException();
                    return info.Env().Undefined();
                },
                name);
        }
    }

} // namespace rosetta
