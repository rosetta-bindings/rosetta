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

        // Working state for one collect_members recursion. Bundled into a struct
        // because the walk now tracks five parallel things and threading them as
        // separate out-parameters made the recursive call unreadable.
        struct collect_state {
            std::vector<std::meta::info>  fields;
            std::vector<std::meta::info>  methods;
            std::vector<std::string_view> seen_fields;

            // Method names introduced by an ALREADY-processed (i.e. more-derived)
            // class. A base declaration of such a name is hidden — see the
            // name-hiding note in collect_members.
            std::vector<std::string_view> hidden_methods;

            // The (name, function-type) pairs already emitted, as parallel
            // vectors. Deduping on the PAIR is what lets an overload set survive
            // while a diamond-shared base member is still emitted once.
            std::vector<std::string_view> sig_names;
            std::vector<std::meta::info>  sig_types;

            std::vector<member_drop>      drops;
            std::vector<std::meta::info>  seen_types;
        };

        // Collect exportable fields and methods across `type` and all its public
        // bases.
        //
        // Methods are deduped by (identifier, function type), so a class's whole
        // OVERLOAD SET reaches the visitor — `at(int)` and `at(int, int)` are
        // distinct entries, not one survivor. Two situations still collapse to a
        // single emission:
        //
        //   * a diamond-shared base member, reached along two paths, whose name
        //     AND signature are identical (also guarded structurally by
        //     `seen_types`);
        //   * a derived override of a base virtual — same name, same signature,
        //     and the derived one is seen first, so it wins.
        //
        // Base declarations are subject to C++ NAME HIDING: if a more-derived
        // class declares `f` at all, every base `f` is dropped, whatever its
        // signature — the same thing a C++ caller sees without a `using Base::f`.
        // Binding the hidden base overloads would hand scripts a call the C++ API
        // does not offer. Each one is recorded in `drops` so the coverage report
        // can name it rather than leaving it to be discovered by its absence.
        //
        // A NON-PUBLIC derived declaration hides just as hard. `class D : public B`
        // that re-declares B's public `f` under `protected:` (a protected override
        // of a public virtual is the usual way this shows up) leaves every outside
        // caller — the emitted binding included — unable to name `&D::f` at all.
        // So the members are enumerated through an unchecked access context: a
        // private / protected declaration is never bound, but its name is still
        // recorded as hiding the base's, which is what keeps the emitted
        // `&D::f` from being a line that cannot compile.
        //
        // Two DIFFERENT bases declaring the same name is left alone: both sets
        // are emitted. Such a call is ambiguous in C++ without qualification, but
        // the binding has no ambiguity to resolve — each entry is spliced from
        // its own reflection — so binding both beats binding neither.
        //
        // Own members are processed before bases, so on a name clash the
        // most-derived declaration is the one that survives.
        consteval void collect_members(std::meta::info type, collect_state &st) {
            auto ctx = std::meta::access_context::current();
            auto canon = std::meta::dealias(type);

            for (auto t : st.seen_types)
                if (t == canon)
                    return;
            st.seen_types.push_back(canon);

            auto contains = [](const std::vector<std::string_view> &xs, std::string_view n) {
                for (auto x : xs)
                    if (x == n)
                        return true;
                return false;
            };
            auto contains_sig = [&](std::string_view n, std::meta::info t) {
                for (std::size_t i = 0; i < st.sig_names.size(); ++i)
                    if (st.sig_names[i] == n && st.sig_types[i] == t)
                        return true;
                return false;
            };

            // Own data members (declaration order), then own methods. Fields
            // cannot overload, so they keep the plain by-name dedup. A
            // non-public one takes its name out of circulation without being
            // emitted, for the same reason a non-public method does.
            for (auto f :
                 std::meta::nonstatic_data_members_of(canon, std::meta::access_context::unchecked())) {
                if (!std::meta::has_identifier(f)) {
                    continue; // unnamed bit-field: nothing to bind or to hide
                }
                auto id = std::meta::identifier_of(f);
                if (!contains(st.seen_fields, id)) {
                    st.seen_fields.push_back(id);
                    if (std::meta::is_public(f)) {
                        st.fields.push_back(f);
                    }
                }
            }

            // Names this class introduces. Collected into a local list and only
            // merged into `hidden_methods` AFTER the loop: a class's own
            // overloads must not hide each other.
            std::vector<std::string_view> own_names;
            for (auto m : std::meta::members_of(canon, std::meta::access_context::unchecked())) {
                if (!std::meta::is_public(m)) {
                    // Not bindable — but it hides the base declaration of its
                    // name from outside the class, the emitted `&D::name`
                    // included. Record the name; the base's own entry is
                    // reported as hidden_by_derived when the recursion gets
                    // there, so the coverage report still accounts for it.
                    if (std::meta::is_function(m) && std::meta::has_identifier(m) &&
                        !std::meta::is_constructor(m) && !std::meta::is_destructor(m) &&
                        !std::meta::is_special_member_function(m)) {
                        own_names.push_back(std::meta::identifier_of(m));
                    }
                    continue;
                }
                if (std::meta::is_function_template(m)) {
                    // A member template has no fixed parameter pack to splice;
                    // only an explicit instantiation could be bound.
                    st.drops.push_back({m, drop_reason::function_template});
                    continue;
                }
                if (!std::meta::is_function(m) || std::meta::is_constructor(m) ||
                    std::meta::is_destructor(m) || std::meta::is_special_member_function(m)) {
                    continue; // not a member function, or a copy/move special
                }
                if (!std::meta::has_identifier(m)) {
                    // operator==, operator[], operator T — no plain identifier to
                    // bind to a target-language name.
                    st.drops.push_back({m, drop_reason::no_identifier});
                    continue;
                }
                auto id = std::meta::identifier_of(m);
                if (contains(st.hidden_methods, id)) {
                    st.drops.push_back({m, drop_reason::hidden_by_derived});
                    continue;
                }
                auto ty = std::meta::type_of(m);
                if (contains_sig(id, ty)) {
                    continue; // diamond duplicate / already-seen override
                }
                st.sig_names.push_back(id);
                st.sig_types.push_back(ty);
                st.methods.push_back(m);
                own_names.push_back(id);
            }
            for (auto n : own_names) {
                if (!contains(st.hidden_methods, n)) {
                    st.hidden_methods.push_back(n);
                }
            }

            // Then recurse into public bases, depth-first.
            for (auto base : std::meta::bases_of(canon, ctx)) {
                if (std::meta::is_public(base))
                    collect_members(std::meta::type_of(base), st);
            }
        }

        // Flattened, deduped member list: all fields first (mirroring the
        // original fields-then-methods visitation order), then all methods.
        consteval std::vector<std::meta::info> flattened_members(std::meta::info type) {
            collect_state st;
            collect_members(type, st);
            st.fields.insert(st.fields.end(), st.methods.begin(), st.methods.end());
            return st.fields;
        }

        // The members the walk did NOT hand to the visitor, with the reason. Fed
        // to the coverage report by gen_detail::describe<T>(); nothing in the
        // binding path consults it.
        consteval std::vector<member_drop> member_drops(std::meta::info type) {
            collect_state st;
            collect_members(type, st);
            return st.drops;
        }

        // The same list with every name resolved to a static string, ready to be
        // copied into GenClass::dropped by ordinary runtime code. Note
        // display_string_of rather than identifier_of: a dropped operator has no
        // identifier at all (asking for one is a hard error), and its display
        // spelling — "operator==" — is exactly what the report should show.
        consteval std::vector<drop_text> member_drop_texts(std::meta::info type) {
            std::vector<drop_text> out;
            for (const member_drop &d : member_drops(type)) {
                // A function TEMPLATE has no type to ask for — type_of() on one
                // is ill-formed — so it reports an empty signature. Everything
                // else (operators included) has one.
                const char *sig =
                    (d.reason == drop_reason::function_template)
                        ? std::define_static_string("")
                        : std::define_static_string(
                              std::meta::display_string_of(std::meta::type_of(d.member)));
                // Every string here must go through define_static_string: these
                // become template arguments via define_static_array below, and a
                // pointer into a string literal is not a valid one.
                out.push_back(drop_text{
                    std::define_static_string(std::meta::display_string_of(d.member)), sig,
                    std::define_static_string(drop_reason_name(d.reason))});
            }
            return out;
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

    template <typename T, std::meta::info Fn> consteval bool first_overload() {
        // Scan the same flattened list walk<T> visits, in the same order, and
        // report whether Fn is the first entry carrying its name. Deriving it
        // from that list rather than from members_of(parent) is what makes it
        // agree with the walk in the cases that differ: an inherited method, a
        // derived declaration that hides a base set, a diamond-shared base.
        for (std::meta::info m : detail::flattened_members(^^T)) {
            if (!is_exportable_member_function(m)) {
                continue; // a field: same list, fields first
            }
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Fn)) {
                return m == Fn;
            }
        }
        return true; // not found (a synthesized entry) — do not suppress it
    }

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
