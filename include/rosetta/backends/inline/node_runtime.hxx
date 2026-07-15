// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Definitions for <rosetta/backends/node_runtime.h>. Not a standalone header —
// it relies on the declarations and includes that node_runtime.h sets up, and
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
        } else if constexpr (std::is_class_v<T>) {
            return static_cast<T &>(Wrap<T>::Unwrap(v.As<Napi::Object>())->inner());
        } else {
            static_assert(sizeof(T) == 0, "from_napi: unsupported type");
        }
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

    // ---- Wrap: construction / destruction ----

    template <typename T, typename Tramp>
    Wrap<T, Tramp>::Wrap(const Napi::CallbackInfo &info)
        : Napi::ObjectWrap<Wrap<T, Tramp>>(info) {
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
        return to_napi(info.Env(), inner().*MemPtr);
    }

    template <typename T, typename Tramp>
    template <auto MemPtr>
    Napi::Value Wrap<T, Tramp>::get_member_object(const Napi::CallbackInfo &info) {
        using FieldT = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
        FieldT *p    = &(static_cast<T &>(inner()).*MemPtr);
        return ctor_ref<FieldT>().New(
            {Napi::External<void>::New(info.Env(), static_cast<void *>(p)), this->Value()});
    }

    template <typename T, typename Tramp>
    template <auto MemPtr>
    void Wrap<T, Tramp>::set_field(const Napi::CallbackInfo & /*info*/, const Napi::Value &v) {
        using FieldT    = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
        inner().*MemPtr = from_napi<FieldT>(v);
    }

    template <typename T, typename Tramp>
    template <auto MemPtr, fixed_str Name, double Lo, double Hi>
    void Wrap<T, Tramp>::set_field_ranged(const Napi::CallbackInfo &info, const Napi::Value &v) {
        using FieldT = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
        FieldT val   = from_napi<FieldT>(v);
        double d     = static_cast<double>(val);
        if (d < Lo || d > Hi) {
            throw Napi::RangeError::New(info.Env(), std::string(Name.data) + " out of range");
        }
        inner().*MemPtr = val;
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
        return call_method_impl<MFP>(info,
                                     std::make_index_sequence<fn_traits<decltype(MFP)>::arity>{});
    }

    template <typename T, typename Tramp>
    template <auto FP>
    Napi::Value Wrap<T, Tramp>::ext_method(const Napi::CallbackInfo &info) {
        return ext_method_impl<FP>(
            info, std::make_index_sequence<fn_traits<decltype(FP)>::arity - 1>{});
    }

    template <typename T, typename Tramp>
    template <auto FP>
    Napi::Value Wrap<T, Tramp>::call_static(const Napi::CallbackInfo &info) {
        return call_static_impl<FP>(info,
                                    std::make_index_sequence<fn_traits<decltype(FP)>::arity>{});
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
        return napi_free_call<FP>(info, std::make_index_sequence<fn_traits<decltype(FP)>::arity>{});
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
