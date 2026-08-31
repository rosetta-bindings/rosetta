// Copyright (c) fmaerten@gmail.com
// License: MIT

// Reflection-free C# runtime — the stock-C++ core shared by both C# backends.
//
// It holds everything that does NOT need reflection: the per-type instance
// Store, the runtime registry, JSON value marshalling (plain nlohmann — scalars,
// bool, std::string, enums-as-integer, and std::vector of those), the
// {"ok":…,"value":…} / {"ok":false,"error":…} envelopes, and the `api_*`
// dispatch the exported C ABI forwards to.
//
// The registry is populated by the C# backend's generated auto_csharp.cpp,
// which fills TypeOps with the member-pointer dispatch templates below
// (get_field<&T::f>, call_method<&T::m>, construct<T, Args...>, …). These
// deduce field / parameter / return types from the pointer type via fn_traits —
// no reflection, so it builds with an ordinary C++17 compiler. (A second,
// reflection-driven backend used to fill the same registry with splice-based
// handlers; it was removed.)
//
// The two never share a translation unit, so the names never collide.

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Exported-symbol marker for the flat C ABI the generated auto_csharp.cpp
// defines. Default visibility (or dllexport on Windows) so P/Invoke can find the
// unmangled names in the shared library's dynamic symbol table.
#if defined(_WIN32)
#define ROSETTA_CS_EXPORT __declspec(dllexport)
#else
#define ROSETTA_CS_EXPORT __attribute__((visibility("default")))
#endif

namespace rosetta {
    namespace cs {

        using json = nlohmann::json;

        // ---- Value marshalling ----
        //
        // nlohmann handles scalars, bool, std::string, enums (as their underlying
        // integer) and std::vector of those out of the box, so encode / decode
        // need no reflection. The C# side uses the same contract (enums as their
        // numeric value — System.Text.Json's default).
        template <typename T> inline json encode(const T &v) { return json(v); }
        template <typename T> inline T decode(const json &j) { return j.template get<T>(); }

        // ---- Instance store (one per bound type) ----
        template <typename T> class Store {
        public:
            int create() {
                std::lock_guard lk(m_);
                int             id = next_id_++;
                map_.emplace(id, T{});
                return id;
            }
            int insert(T v) {
                std::lock_guard lk(m_);
                int             id = next_id_++;
                map_.emplace(id, std::move(v));
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
            int                        next_id_{1};
        };

        template <typename T> Store<T> &store_of() {
            static Store<T> s;
            return s;
        }

        // ---- Runtime registry ----
        struct TypeOps {
            std::function<int()>                                          create;  // default ctor
            std::function<bool(int)>                                      destroy;
            std::map<std::string, std::function<json(int)>>              get;    // field read
            std::map<std::string, std::function<void(int, const json &)>> set;   // field write
            std::map<std::string, std::function<json(int, const json &)>> call;  // instance method
            std::map<std::string, std::function<json(const json &)>>      scall; // static method
            std::map<std::size_t, std::function<int(const json &)>>       ctors; // by arity
        };

        inline std::map<std::string, TypeOps> &registry() {
            static std::map<std::string, TypeOps> r;
            return r;
        }
        inline std::map<std::string, json> &enums() {
            static std::map<std::string, json> e;
            return e;
        }
        inline std::map<std::string, std::function<json(const json &)>> &functions() {
            static std::map<std::string, std::function<json(const json &)>> f;
            return f;
        }

        // ---- JSON result envelopes ----
        inline std::string ok_env(const json &v) {
            return json{{"ok", true}, {"value", v}}.dump();
        }
        inline std::string err_env(const std::string &e) {
            return json{{"ok", false}, {"error", e}}.dump();
        }

        // ---- Dispatch entry points (the exported C ABI forwards here) ----

        inline int api_create(const char *type, const char *args_json) {
            auto it = registry().find(type);
            if (it == registry().end()) {
                return -1;
            }
            TypeOps &ops = it->second;
            try {
                json args = (!args_json || !*args_json) ? json::array() : json::parse(args_json);
                if (!args.is_array()) {
                    return -1;
                }
                if (args.empty() && ops.create) {
                    return ops.create();
                }
                auto cit = ops.ctors.find(args.size());
                if (cit != ops.ctors.end()) {
                    return cit->second(args);
                }
                if (args.empty() && ops.create) {
                    return ops.create();
                }
                return -1;
            } catch (...) {
                return -1;
            }
        }

        inline bool api_destroy(const char *type, int id) {
            auto it = registry().find(type);
            return it != registry().end() && it->second.destroy(id);
        }

        inline std::string api_get(const char *type, int id, const char *field) {
            try {
                auto it = registry().find(type);
                if (it == registry().end()) {
                    return err_env("unknown type");
                }
                auto fit = it->second.get.find(field);
                if (fit == it->second.get.end()) {
                    return err_env("unknown field");
                }
                return ok_env(fit->second(id));
            } catch (const std::exception &e) {
                return err_env(e.what());
            }
        }

        inline std::string api_set(const char *type, int id, const char *field,
                                   const char *value_json) {
            try {
                auto it = registry().find(type);
                if (it == registry().end()) {
                    return err_env("unknown type");
                }
                auto fit = it->second.set.find(field);
                if (fit == it->second.set.end()) {
                    return err_env("unknown field");
                }
                fit->second(id, json::parse(value_json));
                return ok_env(nullptr);
            } catch (const std::exception &e) {
                return err_env(e.what());
            }
        }

        inline std::string api_call(const char *type, int id, const char *method,
                                    const char *args_json) {
            try {
                auto it = registry().find(type);
                if (it == registry().end()) {
                    return err_env("unknown type");
                }
                auto mit = it->second.call.find(method);
                if (mit == it->second.call.end()) {
                    return err_env("unknown method");
                }
                json args = (!args_json || !*args_json) ? json::array() : json::parse(args_json);
                return ok_env(mit->second(id, args));
            } catch (const std::exception &e) {
                return err_env(e.what());
            }
        }

        inline std::string api_scall(const char *type, const char *method, const char *args_json) {
            try {
                auto it = registry().find(type);
                if (it == registry().end()) {
                    return err_env("unknown type");
                }
                auto mit = it->second.scall.find(method);
                if (mit == it->second.scall.end()) {
                    return err_env("unknown static method");
                }
                json args = (!args_json || !*args_json) ? json::array() : json::parse(args_json);
                return ok_env(mit->second(args));
            } catch (const std::exception &e) {
                return err_env(e.what());
            }
        }

        inline std::string api_function(const char *name, const char *args_json) {
            try {
                auto it = functions().find(name);
                if (it == functions().end()) {
                    return err_env("unknown function");
                }
                json args = (!args_json || !*args_json) ? json::array() : json::parse(args_json);
                return ok_env(it->second(args));
            } catch (const std::exception &e) {
                return err_env(e.what());
            }
        }

        inline std::string api_enum(const char *type) {
            auto it = enums().find(type);
            if (it == enums().end()) {
                return err_env("unknown enum");
            }
            return ok_env(it->second);
        }

        // ---- Reflection-free member-pointer dispatch (for the expanded backend) ----
        //
        // The generated auto_csharp.cpp assigns these template instantiations
        // (plain function pointers) into a TypeOps, e.g.
        //   ops.get["x"]       = &get_field<&Point::x>;
        //   ops.call["length"] = &call_method<&Point::length>;
        //   ops.ctors[2]       = &construct<Point, double, double>;
        // Field / parameter / return types are deduced from the pointer type, so
        // no reflection is involved.

        // A string usable as a non-type template parameter (C++20), so a field's
        // name rides into the read-only / range error message.
        template <std::size_t N> struct fixed_str {
            char data[N]{};
            constexpr fixed_str(const char (&s)[N]) {
                for (std::size_t i = 0; i < N; ++i) {
                    data[i] = s[i];
                }
            }
        };

        // Signature traits for member- and free-function pointers: owning class
        // (members only), return type, arity, and the I-th parameter type.
        template <typename F> struct fn_traits;
        template <typename R, typename C, typename... A> struct fn_traits<R (C::*)(A...)> {
            using cls                          = C;
            using ret                          = R;
            static constexpr std::size_t arity = sizeof...(A);
            template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
        };
        template <typename R, typename C, typename... A> struct fn_traits<R (C::*)(A...) const> {
            using cls                          = C;
            using ret                          = R;
            static constexpr std::size_t arity = sizeof...(A);
            template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
        };
        template <typename R, typename C, typename... A>
        struct fn_traits<R (C::*)(A...) noexcept> {
            using cls                          = C;
            using ret                          = R;
            static constexpr std::size_t arity = sizeof...(A);
            template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
        };
        template <typename R, typename C, typename... A>
        struct fn_traits<R (C::*)(A...) const noexcept> {
            using cls                          = C;
            using ret                          = R;
            static constexpr std::size_t arity = sizeof...(A);
            template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
        };
        template <typename R, typename... A> struct fn_traits<R (*)(A...)> {
            using ret                          = R;
            static constexpr std::size_t arity = sizeof...(A);
            template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
        };
        template <typename R, typename... A> struct fn_traits<R (*)(A...) noexcept> {
            using ret                          = R;
            static constexpr std::size_t arity = sizeof...(A);
            template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
        };

        // Owning class / value type of a pointer-to-data-member.
        template <typename M> struct mem_obj_traits;
        template <typename C, typename F> struct mem_obj_traits<F C::*> {
            using cls  = C;
            using type = F;
        };

        template <typename T> inline int create_default() { return store_of<T>().create(); }
        template <typename T> inline bool destroy_inst(int id) { return store_of<T>().erase(id); }

        template <auto MO> inline json get_field(int id) {
            using C = typename mem_obj_traits<decltype(MO)>::cls;
            auto *p = store_of<C>().find(id);
            if (!p) {
                throw std::runtime_error("instance not found");
            }
            return encode(p->*MO);
        }

        template <auto MO> inline void set_field(int id, const json &v) {
            using C = typename mem_obj_traits<decltype(MO)>::cls;
            using F = std::remove_cvref_t<typename mem_obj_traits<decltype(MO)>::type>;
            auto *p = store_of<C>().find(id);
            if (!p) {
                throw std::runtime_error("instance not found");
            }
            p->*MO = decode<F>(v);
        }

        template <auto MO, fixed_str Name> inline void set_field_readonly(int, const json &) {
            throw std::runtime_error(std::string(Name.data) + " is read-only");
        }

        template <auto MO, fixed_str Name, double Lo, double Hi>
        inline void set_field_ranged(int id, const json &v) {
            using C = typename mem_obj_traits<decltype(MO)>::cls;
            using F = std::remove_cvref_t<typename mem_obj_traits<decltype(MO)>::type>;
            auto *p = store_of<C>().find(id);
            if (!p) {
                throw std::runtime_error("instance not found");
            }
            F      val = decode<F>(v);
            double d   = static_cast<double>(val);
            if (d < Lo || d > Hi) {
                throw std::runtime_error(std::string(Name.data) + " out of range");
            }
            p->*MO = val;
        }

        template <auto M, std::size_t... I>
        inline json call_method_impl(int id, const json &args, std::index_sequence<I...>) {
            using Tr = fn_traits<decltype(M)>;
            using C  = typename Tr::cls;
            using R  = typename Tr::ret;
            auto *p  = store_of<C>().find(id);
            if (!p) {
                throw std::runtime_error("instance not found");
            }
            if constexpr (std::is_void_v<R>) {
                (p->*M)(decode<std::remove_cvref_t<typename Tr::template arg<I>>>(args.at(I))...);
                return nullptr;
            } else {
                return encode(
                    (p->*M)(decode<std::remove_cvref_t<typename Tr::template arg<I>>>(args.at(I))...));
            }
        }
        template <auto M> inline json call_method(int id, const json &args) {
            return call_method_impl<M>(id, args,
                                       std::make_index_sequence<fn_traits<decltype(M)>::arity>{});
        }

        template <auto F, std::size_t... I>
        inline json call_function_impl(const json &args, std::index_sequence<I...>) {
            using Tr = fn_traits<decltype(F)>;
            using R  = typename Tr::ret;
            if constexpr (std::is_void_v<R>) {
                (*F)(decode<std::remove_cvref_t<typename Tr::template arg<I>>>(args.at(I))...);
                return nullptr;
            } else {
                return encode(
                    (*F)(decode<std::remove_cvref_t<typename Tr::template arg<I>>>(args.at(I))...));
            }
        }
        // Used for both free functions and static member functions (both are
        // plain function pointers).
        template <auto F> inline json call_function(const json &args) {
            return call_function_impl<F>(args,
                                         std::make_index_sequence<fn_traits<decltype(F)>::arity>{});
        }

        template <typename T, typename... A, std::size_t... I>
        inline int construct_impl(const json &args, std::index_sequence<I...>) {
            return store_of<T>().insert(T(decode<std::remove_cvref_t<A>>(args.at(I))...));
        }
        template <typename T, typename... A> inline int construct(const json &args) {
            return construct_impl<T, A...>(args, std::make_index_sequence<sizeof...(A)>{});
        }

    } // namespace cs
} // namespace rosetta
