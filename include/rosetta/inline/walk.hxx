// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Definitions for <rosetta/walk.h>. Not a standalone header — it relies on the
// declarations and includes that walk.h sets up, and is included at its bottom.

namespace rosetta {

    namespace ann {

        template <typename A> consteval bool has(auto... anns) {
            return (std::same_as<std::remove_cvref_t<decltype(anns)>, A> || ...);
        }

        template <typename A> consteval A get_or(A fallback, auto... anns) {
            A result = fallback;
            (
                [&] {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(anns)>, A>) {
                        result = anns;
                    }
                }(),
                ...);
            return result;
        }

    } // namespace ann

    consteval bool is_exportable_member_function(std::meta::info fn) {
        // Operator overloads and conversion functions have no plain identifier
        // (operator==, operator[], operator T) — they can't be bound by name to a
        // target language, and identifier_of() on them is a hard error, so the
        // has_identifier() guard both filters them and keeps the later walk safe.
        return std::meta::is_function(fn) && std::meta::has_identifier(fn) &&
               !std::meta::is_constructor(fn) && !std::meta::is_destructor(fn) &&
               !std::meta::is_special_member_function(fn);
    }

    consteval bool is_exportable_constructor(std::meta::info fn) {
        if (!std::meta::is_constructor(fn) || std::meta::is_copy_constructor(fn) ||
            std::meta::is_move_constructor(fn) || std::meta::is_constructor_template(fn) ||
            std::meta::is_deleted(fn)) {
            return false;
        }
        // A std::initializer_list parameter is a C++-source-only affordance: no
        // target language can produce one (no marshaller in any backend), and
        // such a constructor always shadows an equivalent std::vector overload.
        for (std::meta::info p : std::meta::parameters_of(fn)) {
            const std::meta::info t =
                std::meta::dealias(std::meta::remove_cvref(std::meta::type_of(p)));
            if (std::meta::has_template_arguments(t) &&
                std::meta::template_of(t) == ^^std::initializer_list) {
                return false;
            }
        }
        return true;
    }

    namespace detail {

        // Collect exportable fields and methods across `type` and all its public
        // bases. Members are deduped by identifier so that a derived declaration
        // shadows a base one (most-derived wins) and a diamond-shared base is
        // emitted exactly once. `seen_types` guards against re-walking a shared
        // (e.g. virtual) base subobject, so a diamond lattice stays linear.
        //
        // Own members are processed before bases, so on a name clash the
        // most-derived declaration is the one that survives the dedup.
        consteval void collect_members(std::meta::info type,
                                       std::vector<std::meta::info> &fields,
                                       std::vector<std::meta::info> &methods,
                                       std::vector<std::string_view> &seen_fields,
                                       std::vector<std::string_view> &seen_methods,
                                       std::vector<std::meta::info> &seen_types) {
            auto ctx = std::meta::access_context::current();
            auto canon = std::meta::dealias(type);

            for (auto t : seen_types)
                if (t == canon)
                    return;
            seen_types.push_back(canon);

            auto contains = [](std::vector<std::string_view> &xs, std::string_view n) {
                for (auto x : xs)
                    if (x == n)
                        return true;
                return false;
            };

            // Own data members (declaration order), then own methods.
            for (auto f : std::meta::nonstatic_data_members_of(canon, ctx)) {
                auto id = std::meta::identifier_of(f);
                if (!contains(seen_fields, id)) {
                    seen_fields.push_back(id);
                    fields.push_back(f);
                }
            }
            for (auto m : std::meta::members_of(canon, ctx)) {
                if (is_exportable_member_function(m)) {
                    auto id = std::meta::identifier_of(m);
                    if (!contains(seen_methods, id)) {
                        seen_methods.push_back(id);
                        methods.push_back(m);
                    }
                }
            }

            // Then recurse into public bases, depth-first.
            for (auto base : std::meta::bases_of(canon, ctx)) {
                if (std::meta::is_public(base))
                    collect_members(std::meta::type_of(base), fields, methods,
                                    seen_fields, seen_methods, seen_types);
            }
        }

        // Flattened, deduped member list: all fields first (mirroring the
        // original fields-then-methods visitation order), then all methods.
        consteval std::vector<std::meta::info> flattened_members(std::meta::info type) {
            std::vector<std::meta::info> fields, methods, seen_types;
            std::vector<std::string_view> seen_fields, seen_methods;
            collect_members(type, fields, methods, seen_fields, seen_methods, seen_types);
            fields.insert(fields.end(), methods.begin(), methods.end());
            return fields;
        }

        // Annotation pack for one member: the user-authored / JSON side-car
        // annotations from merged_annotations<Owner>, plus any modifiers walk
        // synthesizes from the reflection itself. Currently injects
        // rosetta::virtual_spec for virtual methods so backends can distinguish
        // a virtual / overriding method from a plain one. (is_virtual is also
        // true for virtual bases, but only member functions reach here.)
        template <class Owner>
        consteval std::vector<std::meta::info> member_annotations(std::meta::info m) {
            std::vector<std::meta::info> anns = merged_annotations<Owner>(m);
            if (is_exportable_member_function(m) && std::meta::is_virtual(m)) {
                anns.push_back(std::meta::reflect_constant(
                    rosetta::virtual_spec{std::meta::is_pure_virtual(m),
                                          std::meta::is_override(m)}));
            }
            return anns;
        }

    } // namespace detail

    template <typename T, typename Visitor> void walk(Visitor &v) {
        // -------- fields + methods, flattened across public bases --------
        // Inherited members are included and deduped by name (most-derived wins);
        // a diamond collapses to a single emission. Each member is emitted with
        // its *declaring* class as the annotation key, so a base member's inline
        // P3394 annotations and out-of-line JSON side-car (ann_json_source<Base>,
        // see <rosetta/annotate.h>) are honoured rather than T's.
        template for (constexpr auto m :
                      std::define_static_array(detail::flattened_members(^^T))) {
            constexpr auto name = std::define_static_string(std::meta::identifier_of(m));
            using Owner = [:std::meta::parent_of(m):];
            constexpr auto anns = std::define_static_array(detail::member_annotations<Owner>(m));

            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                if constexpr (std::meta::is_static_member(m)) {
                    v.template method_static<m, ([:anns[Is]:])...>(name);
                } else if constexpr (is_exportable_member_function(m)) {
                    v.template method_instance<m, ([:anns[Is]:])...>(name);
                } else {
                    v.template field<m, ([:anns[Is]:])...>(name);
                }
            }(std::make_index_sequence<anns.size()>{});
        }

        // -------- constructors (most-derived only; constructors aren't inherited) --------
        constexpr auto ctx = std::meta::access_context::current();
        template for (constexpr auto ctor :
                      std::define_static_array(std::meta::members_of(^^T, ctx))) {
            if constexpr (is_exportable_constructor(ctor)) {
                constexpr auto anns = std::define_static_array(std::meta::annotations_of(ctor));

                [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    if constexpr (requires {
                                      v.template constructor<ctor,
                                                             ([:std::meta::constant_of(anns[Is]):])...>();
                                  }) {
                        v.template constructor<ctor, ([:std::meta::constant_of(anns[Is]):])...>();
                    }
                }(std::make_index_sequence<anns.size()>{});
            }
        }
    }

} // namespace rosetta
