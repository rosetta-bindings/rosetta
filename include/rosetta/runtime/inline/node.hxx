// Copyright (c) fmaerten@gmail.com
// License: MIT

// Definitions for <rosetta/runtime/node.h>. Not a standalone header —
// it relies on the declarations and includes that runtime/node.h sets up, and
// is included at its bottom.

namespace rosetta {

    // ---- Per-class static registries ----

    template <typename T>
    inline std::unordered_map<std::string, Napi::FunctionReference> &napi_override_guard() {
        static std::unordered_map<std::string, Napi::FunctionReference> m;
        return m;
    }

    template <typename T>
    inline std::unordered_map<std::size_t, std::function<T(const Napi::CallbackInfo &)>> &
    ctor_table() {
        static std::unordered_map<std::size_t, std::function<T(const Napi::CallbackInfo &)>> table;
        return table;
    }

    template <typename T> inline Napi::FunctionReference &ctor_ref() {
        static Napi::FunctionReference ref;
        return ref;
    }

    // ---- Type conversion helpers (verbatim from node_visitor) ----

    template <typename T> Napi::Value to_napi(Napi::Env env, const T &v) {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, std::string>) {
            return Napi::String::New(env, v);
        } else if constexpr (std::is_same_v<U, bool>) {
            return Napi::Boolean::New(env, v);
        } else if constexpr (std::is_floating_point_v<U> || std::is_integral_v<U>) {
            return Napi::Number::New(env, static_cast<double>(v));
        } else if constexpr (is_std_vector<U>::value) {
            Napi::Array arr = Napi::Array::New(env, v.size());
            for (std::size_t i = 0; i < v.size(); ++i) {
                arr.Set(static_cast<uint32_t>(i), to_napi(env, v[i]));
            }
            return arr;
        } else if constexpr (std::is_enum_v<U>) {
            return Napi::Number::New(
                env, static_cast<double>(static_cast<std::underlying_type_t<U>>(v)));
        } else if constexpr (is_std_tuple<U>::value) {
            // The shape an out-parameter adapter returns: the return value (when
            // there is one) followed by the out-parameters. JS has no tuple, so
            // it arrives as an array — `const [ok, uv, dim] = attrs.get_doubles(…)`
            // destructures it the way the C++ reads.
            Napi::Array arr = Napi::Array::New(env, std::tuple_size_v<U>);
            std::apply(
                [&](const auto &...xs) {
                    std::uint32_t i = 0;
                    ((arr.Set(i++, to_napi(env, xs))), ...);
                },
                v);
            return arr;
        } else if constexpr (is_shared_ptr<U>::value) {
            // Hand the OWNERSHIP across, not a copy: the JS object adopts the
            // shared_ptr and keeps the C++ object alive for as long as it lives.
            // The generic class branch below could not serve here — it
            // copy-assigns into a default-constructed instance, which is wrong
            // for a factory's result and does not even compile for the
            // non-copyable classes shared_ptr factories tend to hand out.
            using P = typename U::element_type;
            auto *holder = new std::shared_ptr<P>(v); // adopted (and deleted) by Wrap
            return ctor_ref<P>().New({Napi::External<void>::New(env, holder)});
        } else if constexpr (std::is_class_v<U>) {
            Napi::Object obj = ctor_ref<U>().New({});
            Wrap<U>::Unwrap(obj)->inner() = v;
            return obj;
        } else {
            static_assert(sizeof(T) == 0, "to_napi: unsupported type");
        }
    }

    template <typename T> decltype(auto) from_napi(const Napi::Value &v) {
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
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(
                static_cast<std::underlying_type_t<T>>(v.As<Napi::Number>().Int64Value()));
        } else if constexpr (is_std_function<T>::value) {
            // Must precede the generic class branch: a std::function IS a class,
            // and unwrapping a JS function as a bound object would reinterpret
            // unrelated memory.
            return napi_make_fn<T>(v.As<Napi::Function>());
        } else if constexpr (std::is_class_v<T>) {
            return static_cast<T &>(Wrap<T>::Unwrap(v.As<Napi::Object>())->inner());
        } else {
            static_assert(sizeof(T) == 0, "from_napi: unsupported type");
        }
    }

    // ---- Callbacks ----

    template <typename R, typename... A>
    std::function<R(A...)> napi_fn_wrap<std::function<R(A...)>>::make(Napi::Function f) {
        // Persist: a Napi::Function handle dies with the enclosing HandleScope,
        // and a bound class may keep the callback and fire it after the call that
        // supplied it has returned. shared_ptr because a std::function must be
        // copyable and a FunctionReference is move-only; the last copy releases.
        auto ref = std::make_shared<Napi::FunctionReference>(Napi::Persistent(f));
        return [ref](A... args) -> R {
            const Napi::Env env = ref->Env();
            Napi::Value     r   = ref->Call({to_napi(env, args)...});
            if constexpr (std::is_void_v<R>) {
                (void)r;
            } else {
                return from_napi<std::remove_cvref_t<R>>(r);
            }
        };
    }

    // ---- Virtual-method override plumbing ----

    template <typename T> inline bool napi_is_overridden(Napi::Object self, const char *name) {
        Napi::Value f = self.Get(name);
        if (!f.IsFunction()) {
            return false;
        }
        auto &guard = napi_override_guard<T>();
        auto  it    = guard.find(name);
        if (it == guard.end()) {
            return false;
        }
        return !f.StrictEquals(it->second.Value());
    }

    template <typename T, typename Ret, typename Base, typename... Args>
    inline Ret napi_call_override(const NapiTrampoline &self, const char *name, Base base,
                                  const Args &...args) {
        if (self.__rosetta_has_self()) {
            Napi::Object obj = self.__rosetta_self();
            if (napi_is_overridden<T>(obj, name)) {
                Napi::Value r = obj.Get(name).template As<Napi::Function>().Call(
                    obj, {to_napi(obj.Env(), args)...});
                if constexpr (std::is_void_v<Ret>) {
                    return;
                } else {
                    return from_napi<Ret>(r);
                }
            }
        }
        return base();
    }

    template <typename T, typename Ret, typename... Args>
    inline Ret napi_call_override_pure(const NapiTrampoline &self, const char *name,
                                       const Args &...args) {
        if (self.__rosetta_has_self()) {
            Napi::Object obj = self.__rosetta_self();
            if (napi_is_overridden<T>(obj, name)) {
                Napi::Value r = obj.Get(name).template As<Napi::Function>().Call(
                    obj, {to_napi(obj.Env(), args)...});
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

    // ---- Exception translation at the boundary ----

    template <typename F> decltype(auto) guard(Napi::Env env, F &&body) {
        try {
            return body();
        } catch (const Napi::Error &) {
            throw; // already a JS error (range / read-only setters): let it pass
        } catch (const std::out_of_range &e) {
            throw Napi::RangeError::New(env, e.what());
        } catch (const std::invalid_argument &e) {
            throw Napi::TypeError::New(env, e.what());
        } catch (const std::domain_error &e) {
            throw Napi::TypeError::New(env, e.what());
        } catch (const std::exception &e) {
            throw Napi::Error::New(env, e.what());
        } catch (...) {
            // A throw of something that is not a std::exception. There is
            // nothing to report but the fact, which still beats terminate().
            throw Napi::Error::New(env, "unknown C++ exception");
        }
    }

    // ---- Wrap: construction / destruction ----

    template <typename T, typename Tramp>
    Wrap<T, Tramp>::Wrap(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<Wrap<T, Tramp>>(info) {
        // The body lives in construct() so a throwing user constructor becomes
        // a JS exception rather than a terminate() — see guard().
        guard(info.Env(), [&] { construct(info); });
    }

    template <typename T, typename Tramp>
    void Wrap<T, Tramp>::construct(const Napi::CallbackInfo &info) {
        // Alias construction: (External<void> = address of the member object,
        // parent JS object). Reachable only from get_member_object — JS code
        // cannot forge an External. Restricted to untrampolined classes: the
        // external is a T*, and reading it as a Js_T* would be undefined
        // behavior.
        if constexpr (std::is_same_v<T, Tramp>) {
            if (info.Length() == 2 && info[0].IsExternal()) {
                ptr_    = static_cast<Tramp *>(info[0].As<Napi::External<void>>().Data());
                owned_  = false;
                parent_ = Napi::Persistent(info[1].As<Napi::Object>());
                return;
            }
            // Adopt construction: (External<void> = a heap std::shared_ptr<T>
            // to take over). One argument, where aliasing takes two — and, like
            // aliasing, reachable only from rosetta's own code, since JS cannot
            // forge an External. The holder is deleted here; the count it
            // carried lives on in shared_.
            if (info.Length() == 1 && info[0].IsExternal()) {
                auto *holder =
                    static_cast<std::shared_ptr<Tramp> *>(info[0].As<Napi::External<void>>().Data());
                shared_ = std::move(*holder);
                delete holder;
                ptr_   = shared_.get();
                owned_ = false;
                return;
            }
        } else if (info.Length() == 1 && info[0].IsExternal()) {
            // A trampolined class reached through the adopt path: the external
            // holds a shared_ptr<T>, and storing it where a Js_T* is expected
            // would be undefined behavior. The emitter's gate (nx_ret_ok) keeps
            // this unreachable; say so plainly rather than corrupt memory if it
            // ever stops holding.
            throw Napi::TypeError::New(info.Env(),
                                       "rosetta: cannot adopt a shared_ptr for a class with "
                                       "virtual methods (its wrapper holds a trampoline)");
        }
        if constexpr (std::is_default_constructible_v<Tramp>) {
            ptr_   = new Tramp();
            owned_ = true;
            if constexpr (!std::is_same_v<T, Tramp>) {
                inner().__rosetta_set_self(this->Value());
            }
            // The parameterized-constructor path ASSIGNS the freshly built
            // object into the storage. For a non-assignable class (GEO::Mesh)
            // the statement itself would not compile — the emitter registers
            // no ctor_table entries for such a class, so compile the whole
            // path out and keep only the default ctor.
            if constexpr (std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>) {
                auto &tbl = ctor_table<Tramp>();
                auto  it  = tbl.find(info.Length());
                if (it != tbl.end()) {
                    static_cast<T &>(inner()) = it->second(info);
                    return;
                }
            }
            if (info.Length() > 0) {
                throw Napi::TypeError::New(info.Env(), "no matching constructor for " +
                                                           std::to_string(info.Length()) +
                                                           " argument(s)");
            }
        } else if constexpr (std::is_copy_constructible_v<Tramp> ||
                             std::is_move_constructible_v<Tramp>) {
            // Not default-constructible, but a ctor_table entry can build the
            // object straight into fresh storage (a data class whose only
            // constructor is parameterized — Joint, StressDomain...).
            auto &tbl = ctor_table<Tramp>();
            auto  it  = tbl.find(info.Length());
            if (it != tbl.end()) {
                ptr_   = new Tramp(it->second(info));
                owned_ = true;
                if constexpr (!std::is_same_v<T, Tramp>) {
                    inner().__rosetta_set_self(this->Value());
                }
                return;
            }
            throw Napi::TypeError::New(info.Env(), "no matching constructor for " +
                                                       std::to_string(info.Length()) +
                                                       " argument(s)");
        } else {
            // Not default-constructible and not aliased: this class only
            // exists inside another object (a member-object store).
            throw Napi::TypeError::New(info.Env(),
                                       "this class cannot be constructed directly; it is "
                                       "reached as a member of another object");
        }
    }

    template <typename T, typename Tramp> Wrap<T, Tramp>::~Wrap() {
        if (owned_) {
            delete ptr_;
        }
    }

    // ---- Wrap: field accessors ----

    template <typename T, typename Tramp>
    template <auto MemPtr>
    Napi::Value Wrap<T, Tramp>::get_field(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] { return to_napi(info.Env(), inner().*MemPtr); });
    }

    template <typename T, typename Tramp>
    template <auto MemPtr>
    Napi::Value Wrap<T, Tramp>::get_member_object(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] {
            using FieldT = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
            FieldT *p    = &(static_cast<T &>(inner()).*MemPtr);
            return ctor_ref<FieldT>().New(
                {Napi::External<void>::New(info.Env(), static_cast<void *>(p)), this->Value()});
        });
    }

    template <typename T, typename Tramp>
    template <auto MemPtr>
    void Wrap<T, Tramp>::set_field(const Napi::CallbackInfo &info, const Napi::Value &v) {
        guard(info.Env(), [&] {
            using FieldT    = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
            inner().*MemPtr = from_napi<FieldT>(v);
        });
    }

    template <typename T, typename Tramp>
    template <auto MemPtr, fixed_str Name, double Lo, double Hi>
    void Wrap<T, Tramp>::set_field_ranged(const Napi::CallbackInfo &info, const Napi::Value &v) {
        guard(info.Env(), [&] {
            using FieldT = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
            FieldT val   = from_napi<FieldT>(v);
            double d     = static_cast<double>(val);
            if (d < Lo || d > Hi) {
                throw Napi::RangeError::New(info.Env(), std::string(Name.data) + " out of range");
            }
            inner().*MemPtr = val;
        });
    }

    template <typename T, typename Tramp>
    template <auto /*MemPtr*/, fixed_str Name>
    void Wrap<T, Tramp>::set_field_readonly(const Napi::CallbackInfo &info,
                                            const Napi::Value & /*v*/) {
        throw Napi::TypeError::New(info.Env(), std::string(Name.data) + " is read-only");
    }

    // ---- Wrap: method thunks ----

    template <typename T, typename Tramp>
    template <auto MFP>
    Napi::Value Wrap<T, Tramp>::call_method(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] {
            return call_method_impl<MFP>(
                info, std::make_index_sequence<fn_traits<decltype(MFP)>::arity>{});
        });
    }

    template <typename T, typename Tramp>
    template <auto FP>
    Napi::Value Wrap<T, Tramp>::ext_method(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] {
            return ext_method_impl<FP>(
                info, std::make_index_sequence<fn_traits<decltype(FP)>::arity - 1>{});
        });
    }

    template <typename T, typename Tramp>
    template <auto FP>
    Napi::Value Wrap<T, Tramp>::call_static(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] {
            return call_static_impl<FP>(
                info, std::make_index_sequence<fn_traits<decltype(FP)>::arity>{});
        });
    }

    template <typename T, typename Tramp>
    template <auto FP, std::size_t... Is>
    Napi::Value Wrap<T, Tramp>::ext_method_impl(const Napi::CallbackInfo &info,
                                                std::index_sequence<Is...>) {
        using FT = fn_traits<decltype(FP)>;
        using R  = typename FT::ret;
        T &self  = static_cast<T &>(inner());
        if constexpr (std::is_void_v<R>) {
            (*FP)(self,
                  from_napi<std::remove_cvref_t<typename FT::template arg<Is + 1>>>(info[Is])...);
            return info.Env().Undefined();
        } else {
            R r = (*FP)(self, from_napi<std::remove_cvref_t<typename FT::template arg<Is + 1>>>(
                                  info[Is])...);
            return to_napi(info.Env(), r);
        }
    }

    template <typename T, typename Tramp>
    template <auto MFP, std::size_t... Is>
    Napi::Value Wrap<T, Tramp>::call_method_impl(const Napi::CallbackInfo &info,
                                                 std::index_sequence<Is...>) {
        using FT = fn_traits<decltype(MFP)>;
        using R  = typename FT::ret;
        if constexpr (std::is_void_v<R>) {
            (inner().*MFP)(
                from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return info.Env().Undefined();
        } else {
            R r = (inner().*MFP)(
                from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return to_napi(info.Env(), r);
        }
    }

    template <typename T, typename Tramp>
    template <auto MFP, std::size_t... Is>
    Napi::Value Wrap<T, Tramp>::call_method_alias_impl(const Napi::CallbackInfo &info,
                                                       std::index_sequence<Is...>) {
        using FT     = fn_traits<decltype(MFP)>;
        using RefT   = typename FT::ret;                  // an lvalue reference
        using Held   = std::remove_reference_t<RefT>;
        Held &r      = (inner().*MFP)(
            from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
        // Same aliasing construction get_member_object uses: wrap the ADDRESS,
        // do not own it, and pin this object as the parent so the referent
        // outlives every handle onto it.
        return ctor_ref<std::remove_cv_t<Held>>().New(
            {Napi::External<void>::New(info.Env(), static_cast<void *>(const_cast<std::remove_cv_t<Held> *>(&r))),
             this->Value()});
    }

    template <typename T, typename Tramp>
    template <auto MFP> Napi::Value Wrap<T, Tramp>::call_method_alias(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] {
            using FT = fn_traits<decltype(MFP)>;
            return call_method_alias_impl<MFP>(info, std::make_index_sequence<FT::arity>{});
        });
    }

    template <typename T, typename Tramp>
    template <auto FP, std::size_t... Is>
    Napi::Value Wrap<T, Tramp>::call_static_impl(const Napi::CallbackInfo &info,
                                                 std::index_sequence<Is...>) {
        using FT = fn_traits<decltype(FP)>;
        using R  = typename FT::ret;
        if constexpr (std::is_void_v<R>) {
            (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return info.Env().Undefined();
        } else {
            R r = (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return to_napi(info.Env(), r);
        }
    }

    // ---- Free-function entry, keyed on the function pointer ----

    template <auto FP, std::size_t... Is>
    inline Napi::Value napi_free_call(const Napi::CallbackInfo &info, std::index_sequence<Is...>) {
        using FT = fn_traits<decltype(FP)>;
        using R  = typename FT::ret;
        if constexpr (std::is_void_v<R>) {
            (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return info.Env().Undefined();
        } else {
            R r =
                (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return to_napi(info.Env(), r);
        }
    }

    template <auto FP> inline Napi::Value napi_free_entry(const Napi::CallbackInfo &info) {
        return guard(info.Env(), [&] {
            return napi_free_call<FP>(
                info, std::make_index_sequence<fn_traits<decltype(FP)>::arity>{});
        });
    }

    // ---- Enum object from an explicit name/value list (no reflection) ----

    inline Napi::Object
    make_enum(Napi::Env env,
              std::initializer_list<std::pair<const char *, long long>> values) {
        Napi::Object obj = Napi::Object::New(env);
        for (const auto &[name, value] : values) {
            obj.Set(name, Napi::Number::New(env, static_cast<double>(value)));
        }
        obj.Freeze();
        return obj;
    }

} // namespace rosetta
