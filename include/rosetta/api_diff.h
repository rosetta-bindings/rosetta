// Copyright (c) fmaerten@gmail.com
// License: MIT

// Compare two public-API snapshots (the api.json generate() writes, see
// <rosetta/api.h>) and classify every difference by what it does to existing
// callers.
//
//   breaking    — code written against `before` stops working against `after`:
//                 a removed or renamed class / member / enumerator, a changed
//                 signature, a field that became read-only, a narrowed range.
//   warning     — existing calls still compile and bind but may behave
//                 differently: a renamed parameter (keyword-argument callers
//                 in Python break), a changed default value, a changed
//                 enumeration underlying type.
//   compatible  — additions: new classes and members, new overloads, trailing
//                 parameters added WITH defaults, a widened range.
//
// Renames are detected heuristically, never assumed: a member that disappeared
// and a member that appeared in the same scope, with the same signature, and
// with no other candidate on either side, is reported as one rename instead of
// a removal plus an addition. It is still breaking — callers spell the old name.
//
// Standalone: C++17 and nlohmann/json only, no reflection, so rosetta_gen (which
// is built with the system compiler) can include it. Used by
// `rosetta_gen --api-diff old.json new.json`; see docs/API_DIFF.md.

#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace rosetta {
    namespace api {

        enum class severity { breaking, warning, compatible };

        inline const char *severity_name(severity s) {
            switch (s) {
            case severity::breaking:
                return "breaking";
            case severity::warning:
                return "warning";
            default:
                return "compatible";
            }
        }

        /**
         * @brief One difference between two snapshots.
         *
         * `kind` is a stable slug for tooling ("method_removed",
         * "signature_changed", …); `detail` is the human sentence. `scope` is the
         * class or enumeration ("" for a free function or a top-level entity),
         * `member` the member it concerns ("" when the change is to the scope
         * itself). `before` / `after` carry the two spellings when there are two.
         */
        struct Change {
            severity    level = severity::breaking;
            std::string kind;
            std::string scope;
            std::string member;
            std::string detail;
            std::string before;
            std::string after;
        };

        namespace diff_detail {

            using json = nlohmann::json;

            inline std::string str(const json &j, const char *key) {
                auto it = j.find(key);
                return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
            }

            inline bool flag(const json &j, const char *key) {
                auto it = j.find(key);
                return it != j.end() && it->is_boolean() && it->get<bool>();
            }

            inline const json &arr(const json &j, const char *key) {
                static const json empty = json::array();
                auto              it    = j.find(key);
                return (it != j.end() && it->is_array()) ? *it : empty;
            }

            inline bool has_default(const json &p) { return p.contains("default"); }

            // The parameter TYPES of a callable, joined — the overload key.
            inline std::string type_key(const json &params) {
                std::string s;
                for (const auto &p : params) {
                    s += str(p, "type") + "\x1f";
                }
                return s;
            }

            // "ret name(type a, type b = 1) const" — for before/after lines.
            inline std::string spell(const json &c, const std::string &name) {
                std::string s = str(c, "returns");
                s += (s.empty() ? "" : " ") + name + "(";
                bool first = true;
                for (const auto &p : arr(c, "params")) {
                    s += first ? "" : ", ";
                    first = false;
                    s += str(p, "type");
                    if (!str(p, "name").empty()) {
                        s += " " + str(p, "name");
                    }
                    if (has_default(p)) {
                        const std::string d = str(p, "default");
                        s += " = " + (d.empty() ? std::string("…") : d);
                    }
                }
                s += ")";
                if (flag(c, "const")) {
                    s += " const";
                }
                if (flag(c, "static")) {
                    s = "static " + s;
                }
                return s;
            }

            inline std::string qualify(const std::string &scope, const std::string &member) {
                if (scope.empty()) {
                    return member;
                }
                return member.empty() ? scope : scope + "::" + member;
            }

            struct Differ {
                std::vector<Change> out;

                void add(severity lv, const char *kind, const std::string &scope,
                         const std::string &member, std::string detail, std::string before = {},
                         std::string after = {}) {
                    out.push_back(Change{lv, kind, scope, member, std::move(detail),
                                         std::move(before), std::move(after)});
                }

                // ---- callables (methods, constructors, free functions) -----------

                // Same parameter types: compare everything else about the pair.
                void compare_callable(const std::string &scope, const std::string &name,
                                      const json &a, const json &b, const char *what) {
                    const std::string sa = spell(a, name), sb = spell(b, name);
                    if (str(a, "returns") != str(b, "returns")) {
                        add(severity::breaking, "return_type_changed", scope, name,
                            std::string("the ") + what + "'s return type changed from " +
                                str(a, "returns") + " to " + str(b, "returns"),
                            sa, sb);
                    }
                    if (flag(a, "static") != flag(b, "static")) {
                        add(severity::breaking, "static_changed", scope, name,
                            flag(b, "static") ? "became static: it is now called on the class"
                                              : "is no longer static: it now needs an instance",
                            sa, sb);
                    }
                    if (flag(a, "const") && !flag(b, "const")) {
                        add(severity::breaking, "const_dropped", scope, name,
                            "is no longer const: C++ callers holding a const object cannot call it",
                            sa, sb);
                    } else if (!flag(a, "const") && flag(b, "const")) {
                        add(severity::compatible, "const_added", scope, name, "became const", sa,
                            sb);
                    }
                    if (flag(a, "virtual") && !flag(b, "virtual")) {
                        add(severity::breaking, "virtual_dropped", scope, name,
                            "is no longer virtual: overrides in derived classes (C++ or script) "
                            "are no longer called",
                            sa, sb);
                    }
                    if (!flag(a, "pure") && flag(b, "pure")) {
                        add(severity::breaking, "became_pure", scope, name,
                            "became pure virtual: every derived class must now override it", sa,
                            sb);
                    }
                    compare_params(scope, name, arr(a, "params"), arr(b, "params"), sa, sb);
                }

                // Parameter by parameter, over the common prefix.
                void compare_params(const std::string &scope, const std::string &name,
                                    const json &pa, const json &pb, const std::string &sa,
                                    const std::string &sb) {
                    const std::size_t n = std::min(pa.size(), pb.size());
                    for (std::size_t i = 0; i < n; ++i) {
                        const json       &a  = pa[i];
                        const json       &b  = pb[i];
                        const std::string na = str(a, "name"), nb = str(b, "name");
                        const std::string pos = "parameter " + std::to_string(i + 1);
                        if (!na.empty() && !nb.empty() && na != nb) {
                            add(severity::warning, "param_renamed", scope, name,
                                pos + " renamed from '" + na + "' to '" + nb +
                                    "': callers passing it by keyword break",
                                sa, sb);
                        }
                        if (has_default(a) && !has_default(b)) {
                            add(severity::breaking, "default_removed", scope, name,
                                pos + " ('" + nb + "') lost its default: callers omitting it break",
                                sa, sb);
                        } else if (!has_default(a) && has_default(b)) {
                            add(severity::compatible, "default_added", scope, name,
                                pos + " ('" + nb + "') gained a default", sa, sb);
                        } else if (has_default(a) && has_default(b)) {
                            const std::string da = str(a, "default"), db = str(b, "default");
                            if (!da.empty() && !db.empty() && da != db) {
                                add(severity::warning, "default_changed", scope, name,
                                    pos + " ('" + nb + "') default changed from " + da + " to " +
                                        db + ": callers omitting it get a different value",
                                    sa, sb);
                            }
                        }
                        if (flag(a, "out") != flag(b, "out")) {
                            add(severity::breaking, "out_param_changed", scope, name,
                                pos + " ('" + nb + "') " +
                                    (flag(b, "out") ? "became an output parameter"
                                                    : "is no longer an output parameter") +
                                    ": the script-side call shape changed",
                                sa, sb);
                        }
                    }
                }

                // Does `b` accept every call `a` accepted, by appending
                // defaulted parameters?
                static bool extends(const json &a, const json &b) {
                    const json &pa = arr(a, "params");
                    const json &pb = arr(b, "params");
                    if (pb.size() <= pa.size()) {
                        return false;
                    }
                    for (std::size_t i = 0; i < pa.size(); ++i) {
                        if (str(pa[i], "type") != str(pb[i], "type")) {
                            return false;
                        }
                    }
                    for (std::size_t i = pa.size(); i < pb.size(); ++i) {
                        if (!has_default(pb[i])) {
                            return false;
                        }
                    }
                    return true;
                }

                // One overload set (all callables sharing `name` in `scope`),
                // present on both sides.
                void diff_overloads(const std::string &scope, const std::string &name,
                                    const std::vector<json> &olds, const std::vector<json> &news,
                                    const char *what) {
                    std::vector<bool> used_old(olds.size(), false), used_new(news.size(), false);

                    // 1. Same parameter types.
                    for (std::size_t i = 0; i < olds.size(); ++i) {
                        for (std::size_t j = 0; j < news.size(); ++j) {
                            if (!used_new[j] && type_key(arr(olds[i], "params")) ==
                                                    type_key(arr(news[j], "params"))) {
                                used_old[i] = used_new[j] = true;
                                compare_callable(scope, name, olds[i], news[j], what);
                                break;
                            }
                        }
                    }
                    // 2. Trailing parameters added with defaults: every old call
                    //    still resolves.
                    for (std::size_t i = 0; i < olds.size(); ++i) {
                        for (std::size_t j = 0; !used_old[i] && j < news.size(); ++j) {
                            if (!used_new[j] && extends(olds[i], news[j])) {
                                used_old[i] = used_new[j] = true;
                                add(severity::compatible, "params_appended", scope, name,
                                    std::string("the ") + what +
                                        " gained trailing parameters with defaults",
                                    spell(olds[i], name), spell(news[j], name));
                                json head    = news[j];
                                head["params"] = json::array();
                                for (std::size_t k = 0; k < arr(olds[i], "params").size(); ++k) {
                                    head["params"].push_back(arr(news[j], "params")[k]);
                                }
                                compare_callable(scope, name, olds[i], head, what);
                            }
                        }
                    }
                    // 3. One left on each side: the same entity with a new
                    //    signature, not an unrelated removal and addition.
                    std::vector<std::size_t> lo, ln;
                    for (std::size_t i = 0; i < olds.size(); ++i) {
                        if (!used_old[i]) {
                            lo.push_back(i);
                        }
                    }
                    for (std::size_t j = 0; j < news.size(); ++j) {
                        if (!used_new[j]) {
                            ln.push_back(j);
                        }
                    }
                    if (lo.size() == 1 && ln.size() == 1) {
                        add(severity::breaking, "signature_changed", scope, name,
                            std::string("the ") + what + "'s parameters changed",
                            spell(olds[lo[0]], name), spell(news[ln[0]], name));
                        return;
                    }
                    for (std::size_t i : lo) {
                        add(severity::breaking, "overload_removed", scope, name,
                            std::string("an overload of the ") + what + " was removed",
                            spell(olds[i], name), {});
                    }
                    for (std::size_t j : ln) {
                        add(severity::compatible, "overload_added", scope, name,
                            std::string("a new overload of the ") + what + " was added", {},
                            spell(news[j], name));
                    }
                }

                // A whole family of callables keyed by name: methods of a class,
                // or the free functions.
                void diff_callables(const std::string &scope, const json &olds, const json &news,
                                    const char *what, const char *removed, const char *added,
                                    const char *renamed) {
                    std::map<std::string, std::vector<json>> a, b;
                    std::vector<std::string>                 order_a, order_b;
                    for (const auto &m : olds) {
                        const std::string n = str(m, "name");
                        if (!a.count(n)) {
                            order_a.push_back(n);
                        }
                        a[n].push_back(m);
                    }
                    for (const auto &m : news) {
                        const std::string n = str(m, "name");
                        if (!b.count(n)) {
                            order_b.push_back(n);
                        }
                        b[n].push_back(m);
                    }

                    std::vector<std::string> gone, fresh;
                    for (const auto &n : order_a) {
                        if (b.count(n)) {
                            diff_overloads(scope, n, a[n], b[n], what);
                        } else {
                            gone.push_back(n);
                        }
                    }
                    for (const auto &n : order_b) {
                        if (!a.count(n)) {
                            fresh.push_back(n);
                        }
                    }

                    // Rename candidates: a single-overload name that vanished
                    // and a single-overload name that appeared, same signature,
                    // and nothing else on either side matching it.
                    const auto sig_of = [](const json &m) {
                        return str(m, "returns") + "|" + type_key(arr(m, "params")) + "|" +
                               (flag(m, "static") ? "s" : "");
                    };
                    std::set<std::string> renamed_from, renamed_to;
                    for (const auto &g : gone) {
                        if (a[g].size() != 1) {
                            continue;
                        }
                        std::vector<std::string> hits;
                        for (const auto &f : fresh) {
                            if (b[f].size() == 1 && !renamed_to.count(f) &&
                                sig_of(a[g][0]) == sig_of(b[f][0])) {
                                hits.push_back(f);
                            }
                        }
                        if (hits.size() != 1) {
                            continue;
                        }
                        std::size_t rivals = 0;
                        for (const auto &g2 : gone) {
                            rivals += (a[g2].size() == 1 && sig_of(a[g2][0]) == sig_of(a[g][0]));
                        }
                        if (rivals != 1) {
                            continue;
                        }
                        renamed_from.insert(g);
                        renamed_to.insert(hits[0]);
                        add(severity::breaking, renamed, scope, g,
                            std::string("the ") + what + " was renamed to '" + hits[0] +
                                "' (same signature); callers spelling '" + g + "' break",
                            spell(a[g][0], g), spell(b[hits[0]][0], hits[0]));
                    }

                    for (const auto &g : gone) {
                        if (renamed_from.count(g)) {
                            continue;
                        }
                        for (const auto &m : a[g]) {
                            add(severity::breaking, removed, scope, g,
                                std::string("the ") + what + " was removed", spell(m, g), {});
                        }
                    }
                    for (const auto &f : fresh) {
                        if (renamed_to.count(f)) {
                            continue;
                        }
                        for (const auto &m : b[f]) {
                            add(severity::compatible, added, scope, f,
                                std::string("new ") + what, {}, spell(m, f));
                        }
                    }
                }

                // ---- fields ------------------------------------------------------

                static std::string field_spell(const json &f) {
                    std::string s = str(f, "type") + " " + str(f, "name");
                    if (flag(f, "readonly")) {
                        s += " (read-only)";
                    }
                    if (f.contains("range")) {
                        s += " in [" + f["range"].value("min", json()).dump() + ", " +
                             f["range"].value("max", json()).dump() + "]";
                    }
                    return s;
                }

                void compare_field(const std::string &scope, const json &a, const json &b) {
                    const std::string n  = str(b, "name");
                    const std::string sa = field_spell(a), sb = field_spell(b);
                    if (str(a, "type") != str(b, "type")) {
                        add(severity::breaking, "field_type_changed", scope, n,
                            "the field's type changed from " + str(a, "type") + " to " +
                                str(b, "type"),
                            sa, sb);
                    }
                    if (!flag(a, "readonly") && flag(b, "readonly")) {
                        add(severity::breaking, "became_readonly", scope, n,
                            "the field became read-only: assignments to it now fail", sa, sb);
                    } else if (flag(a, "readonly") && !flag(b, "readonly")) {
                        add(severity::compatible, "became_writable", scope, n,
                            "the field became writable", sa, sb);
                    }

                    const bool ra = a.contains("range"), rb = b.contains("range");
                    if (!ra && rb) {
                        add(severity::breaking, "range_added", scope, n,
                            "the field gained a range: values outside it are now rejected", sa,
                            sb);
                    } else if (ra && !rb) {
                        add(severity::compatible, "range_removed", scope, n,
                            "the field's range was removed", sa, sb);
                    } else if (ra && rb) {
                        const double amin = a["range"].value("min", 0.0);
                        const double amax = a["range"].value("max", 0.0);
                        const double bmin = b["range"].value("min", 0.0);
                        const double bmax = b["range"].value("max", 0.0);
                        if (bmin > amin || bmax < amax) {
                            add(severity::breaking, "range_narrowed", scope, n,
                                "the field's range narrowed: values valid before are now "
                                "rejected",
                                sa, sb);
                        } else if (bmin < amin || bmax > amax) {
                            add(severity::compatible, "range_widened", scope, n,
                                "the field's range widened", sa, sb);
                        }
                    }

                    std::set<std::string> ca, cb;
                    for (const auto &c : arr(a, "choices")) {
                        ca.insert(c.get<std::string>());
                    }
                    for (const auto &c : arr(b, "choices")) {
                        cb.insert(c.get<std::string>());
                    }
                    if (!ca.empty() && !cb.empty()) {
                        for (const auto &c : ca) {
                            if (!cb.count(c)) {
                                add(severity::breaking, "choice_removed", scope, n,
                                    "the choice '" + c + "' is no longer accepted", sa, sb);
                            }
                        }
                        for (const auto &c : cb) {
                            if (!ca.count(c)) {
                                add(severity::compatible, "choice_added", scope, n,
                                    "new choice '" + c + "'", sa, sb);
                            }
                        }
                    }
                }

                void diff_fields(const std::string &scope, const json &olds, const json &news) {
                    std::map<std::string, json> a, b;
                    for (const auto &f : olds) {
                        a[str(f, "name")] = f;
                    }
                    for (const auto &f : news) {
                        b[str(f, "name")] = f;
                    }
                    std::vector<std::string> gone, fresh;
                    for (const auto &f : olds) {
                        const std::string n = str(f, "name");
                        if (b.count(n)) {
                            compare_field(scope, f, b[n]);
                        } else {
                            gone.push_back(n);
                        }
                    }
                    for (const auto &f : news) {
                        if (!a.count(str(f, "name"))) {
                            fresh.push_back(str(f, "name"));
                        }
                    }
                    // Rename: the only vanished field of a type and the only
                    // new field of that same type.
                    const auto count_type = [](const std::vector<std::string> &names,
                                               std::map<std::string, json> &m,
                                               const std::string           &type) {
                        std::size_t k = 0;
                        for (const auto &n : names) {
                            k += str(m[n], "type") == type;
                        }
                        return k;
                    };
                    std::set<std::string> renamed_to;
                    for (const auto &g : gone) {
                        const std::string t = str(a[g], "type");
                        if (count_type(gone, a, t) == 1 && count_type(fresh, b, t) == 1) {
                            for (const auto &f : fresh) {
                                if (str(b[f], "type") == t) {
                                    renamed_to.insert(f);
                                    add(severity::breaking, "field_renamed", scope, g,
                                        "the field was renamed to '" + f + "' (same type)",
                                        field_spell(a[g]), field_spell(b[f]));
                                }
                            }
                        } else {
                            add(severity::breaking, "field_removed", scope, g,
                                "the field was removed", field_spell(a[g]), {});
                        }
                    }
                    for (const auto &f : fresh) {
                        if (!renamed_to.count(f)) {
                            add(severity::compatible, "field_added", scope, f, "new field", {},
                                field_spell(b[f]));
                        }
                    }
                }

                // ---- classes -----------------------------------------------------

                void diff_constructors(const std::string &scope, const json &olds,
                                       const json &news) {
                    std::vector<json> a(olds.begin(), olds.end()), b(news.begin(), news.end());
                    for (auto &c : a) {
                        c["returns"] = "";
                    }
                    for (auto &c : b) {
                        c["returns"] = "";
                    }
                    // A constructor is spelled as C++ spells it: `ns::Mesh::Mesh`.
                    const std::size_t colon = scope.rfind("::");
                    const std::string ctor =
                        colon == std::string::npos ? scope : scope.substr(colon + 2);
                    if (a.empty() || b.empty()) {
                        for (const auto &c : a) {
                            add(severity::breaking, "constructor_removed", scope, ctor,
                                "a constructor was removed", spell(c, ctor), {});
                        }
                        for (const auto &c : b) {
                            add(severity::compatible, "constructor_added", scope, ctor,
                                "new constructor", {}, spell(c, ctor));
                        }
                        return;
                    }
                    const std::size_t before = out.size();
                    diff_overloads(scope, ctor, a, b, "constructor");
                    // The overload walk's generic slugs, made specific.
                    for (std::size_t i = before; i < out.size(); ++i) {
                        Change &c = out[i];
                        if (c.kind == std::string("overload_removed")) {
                            c.kind = "constructor_removed";
                        } else if (c.kind == std::string("overload_added")) {
                            c.kind = "constructor_added";
                        }
                    }
                }

                void diff_class(const json &a, const json &b) {
                    const std::string scope = str(b, "name");
                    if (str(a, "expose") != str(b, "expose")) {
                        add(severity::breaking, "exposed_name_changed", scope, "",
                            "scripts now see the class as '" + str(b, "expose") + "' instead of '" +
                                str(a, "expose") + "'",
                            str(a, "expose"), str(b, "expose"));
                    }
                    std::set<std::string> ba, bb;
                    for (const auto &x : arr(a, "bases")) {
                        ba.insert(x.get<std::string>());
                    }
                    for (const auto &x : arr(b, "bases")) {
                        bb.insert(x.get<std::string>());
                    }
                    for (const auto &x : ba) {
                        if (!bb.count(x)) {
                            add(severity::breaking, "base_removed", scope, "",
                                "no longer derives from " + x +
                                    ": its inherited members and up-casts are gone",
                                x, {});
                        }
                    }
                    for (const auto &x : bb) {
                        if (!ba.count(x)) {
                            add(severity::compatible, "base_added", scope, "",
                                "now also derives from " + x, {}, x);
                        }
                    }
                    if (!flag(a, "abstract") && flag(b, "abstract")) {
                        add(severity::breaking, "became_abstract", scope, "",
                            "became abstract: it can no longer be constructed");
                    }
                    if (!flag(a, "final") && flag(b, "final")) {
                        add(severity::breaking, "became_final", scope, "",
                            "became final: it can no longer be subclassed (from C++ or script)");
                    }
                    if (flag(a, "default_constructible") && !flag(b, "default_constructible")) {
                        add(severity::breaking, "lost_default_constructor", scope, "",
                            "is no longer default-constructible");
                    }
                    diff_constructors(scope, arr(a, "constructors"), arr(b, "constructors"));
                    diff_fields(scope, arr(a, "fields"), arr(b, "fields"));
                    diff_callables(scope, arr(a, "methods"), arr(b, "methods"), "method",
                                   "method_removed", "method_added", "method_renamed");
                }

                // A class's shape without its name, for rename detection.
                static std::string class_fingerprint(const json &k) {
                    std::vector<std::string> parts;
                    for (const auto &f : arr(k, "fields")) {
                        parts.push_back("f" + str(f, "name") + ":" + str(f, "type"));
                    }
                    for (const auto &m : arr(k, "methods")) {
                        parts.push_back("m" + str(m, "name") + ":" + type_key(arr(m, "params")));
                    }
                    if (parts.empty()) {
                        return {}; // nothing to recognise it by
                    }
                    std::sort(parts.begin(), parts.end());
                    std::string s;
                    for (const auto &p : parts) {
                        s += p + "\n";
                    }
                    return s;
                }

                // ---- enumerations ------------------------------------------------

                void diff_enum(const json &a, const json &b) {
                    const std::string scope = str(b, "name");
                    if (str(a, "expose") != str(b, "expose")) {
                        add(severity::breaking, "exposed_name_changed", scope, "",
                            "scripts now see the enumeration as '" + str(b, "expose") +
                                "' instead of '" + str(a, "expose") + "'",
                            str(a, "expose"), str(b, "expose"));
                    }
                    if (str(a, "underlying") != str(b, "underlying")) {
                        add(severity::warning, "underlying_changed", scope, "",
                            "the underlying type changed from " + str(a, "underlying") + " to " +
                                str(b, "underlying"),
                            str(a, "underlying"), str(b, "underlying"));
                    }
                    std::map<std::string, long long> va, vb;
                    std::vector<std::string>         order_a, order_b;
                    for (const auto &v : arr(a, "values")) {
                        va[str(v, "name")] = v.value("value", 0LL);
                        order_a.push_back(str(v, "name"));
                    }
                    for (const auto &v : arr(b, "values")) {
                        vb[str(v, "name")] = v.value("value", 0LL);
                        order_b.push_back(str(v, "name"));
                    }
                    std::set<std::string> renamed_to;
                    for (const auto &n : order_a) {
                        if (vb.count(n)) {
                            if (va[n] != vb[n]) {
                                add(severity::breaking, "enumerator_value_changed", scope, n,
                                    "the enumerator's value changed from " +
                                        std::to_string(va[n]) + " to " + std::to_string(vb[n]) +
                                        ": stored or serialised values now mean something else",
                                    std::to_string(va[n]), std::to_string(vb[n]));
                            }
                            continue;
                        }
                        // Renamed: a new enumerator carries the old value, and
                        // no surviving one does.
                        std::string heir;
                        for (const auto &m : order_b) {
                            if (!va.count(m) && vb[m] == va[n] && !renamed_to.count(m)) {
                                heir = m;
                                break;
                            }
                        }
                        if (!heir.empty()) {
                            renamed_to.insert(heir);
                            add(severity::breaking, "enumerator_renamed", scope, n,
                                "the enumerator was renamed to '" + heir + "' (same value)", n,
                                heir);
                        } else {
                            add(severity::breaking, "enumerator_removed", scope, n,
                                "the enumerator was removed", n + " = " + std::to_string(va[n]),
                                {});
                        }
                    }
                    for (const auto &n : order_b) {
                        if (!va.count(n) && !renamed_to.count(n)) {
                            add(severity::compatible, "enumerator_added", scope, n,
                                "new enumerator", {}, n + " = " + std::to_string(vb[n]));
                        }
                    }
                }

                static std::string enum_fingerprint(const json &e) {
                    std::string s;
                    for (const auto &v : arr(e, "values")) {
                        s += str(v, "name") + "=" + std::to_string(v.value("value", 0LL)) + "\n";
                    }
                    return s;
                }

                // ---- top-level entities (classes, enumerations) ------------------

                template <typename Same, typename Print>
                void diff_entities(const json &olds, const json &news, Same same,
                                   std::string (*fingerprint)(const json &), const char *noun,
                                   const char *removed, const char *added, const char *renamed,
                                   Print print) {
                    std::map<std::string, json> a, b;
                    for (const auto &k : olds) {
                        a[str(k, "name")] = k;
                    }
                    for (const auto &k : news) {
                        b[str(k, "name")] = k;
                    }
                    std::vector<std::string> gone, fresh;
                    for (const auto &k : olds) {
                        const std::string n = str(k, "name");
                        if (b.count(n)) {
                            same(k, b[n]);
                        } else {
                            gone.push_back(n);
                        }
                    }
                    for (const auto &k : news) {
                        if (!a.count(str(k, "name"))) {
                            fresh.push_back(str(k, "name"));
                        }
                    }
                    std::set<std::string> renamed_to;
                    for (const auto &g : gone) {
                        const std::string fp = fingerprint(a[g]);
                        std::vector<std::string> hits;
                        if (!fp.empty()) {
                            for (const auto &f : fresh) {
                                if (!renamed_to.count(f) && fingerprint(b[f]) == fp) {
                                    hits.push_back(f);
                                }
                            }
                        }
                        if (hits.size() == 1) {
                            renamed_to.insert(hits[0]);
                            add(severity::breaking, renamed, g, "",
                                std::string("the ") + noun + " was renamed to " + hits[0] +
                                    " (same members)",
                                g, hits[0]);
                        } else {
                            add(severity::breaking, removed, g, "",
                                std::string("the ") + noun + " was removed", print(a[g]), {});
                        }
                    }
                    for (const auto &f : fresh) {
                        if (!renamed_to.count(f)) {
                            add(severity::compatible, added, f, "", std::string("new ") + noun,
                                {}, print(b[f]));
                        }
                    }
                }
            };

            inline void check_schema(const json &j, const char *which) {
                if (!j.is_object() || !j.contains("rosetta_api")) {
                    throw std::runtime_error(std::string(which) +
                                             " is not a rosetta api.json (no \"rosetta_api\" key)");
                }
                if (j["rosetta_api"] != 1) {
                    throw std::runtime_error(std::string(which) +
                                             ": unsupported api.json schema version " +
                                             j["rosetta_api"].dump());
                }
            }

        } // namespace diff_detail

        /**
         * @brief Every difference between two api.json snapshots, in a stable
         * order: classes, then enumerations, then free functions; within each,
         * the order of `before` followed by additions from `after`.
         * @throws std::runtime_error when either document is not a supported
         * api.json.
         */
        inline std::vector<Change> diff(const nlohmann::json &before,
                                        const nlohmann::json &after) {
            using namespace diff_detail;
            check_schema(before, "the old snapshot");
            check_schema(after, "the new snapshot");

            Differ d;
            d.diff_entities(
                arr(before, "classes"), arr(after, "classes"),
                [&d](const json &a, const json &b) { d.diff_class(a, b); },
                &Differ::class_fingerprint, "class", "class_removed", "class_added",
                "class_renamed", [](const json &k) { return str(k, "name"); });
            d.diff_entities(
                arr(before, "enums"), arr(after, "enums"),
                [&d](const json &a, const json &b) { d.diff_enum(a, b); },
                &Differ::enum_fingerprint, "enumeration", "enum_removed", "enum_added",
                "enum_renamed", [](const json &e) { return str(e, "name"); });
            d.diff_callables("", arr(before, "functions"), arr(after, "functions"), "function",
                             "function_removed", "function_added", "function_renamed");
            return d.out;
        }

        inline std::size_t count(const std::vector<Change> &changes, severity s) {
            return static_cast<std::size_t>(std::count_if(
                changes.begin(), changes.end(), [s](const Change &c) { return c.level == s; }));
        }

        /**
         * @brief The changes as JSON, for CI tooling:
         * { "rosetta_api_diff": 1, "from": v, "to": v,
         *   "breaking": N, "warning": N, "compatible": N,
         *   "changes": [ {"severity","kind","scope","member","detail","before"?,"after"?} ] }
         */
        inline nlohmann::json report_json(const std::vector<Change> &changes,
                                          const std::string &from = {},
                                          const std::string &to   = {}) {
            nlohmann::json j;
            j["rosetta_api_diff"] = 1;
            j["from"]             = from;
            j["to"]               = to;
            j["breaking"]         = count(changes, severity::breaking);
            j["warning"]          = count(changes, severity::warning);
            j["compatible"]       = count(changes, severity::compatible);
            j["changes"]          = nlohmann::json::array();
            for (const Change &c : changes) {
                nlohmann::json e = {{"severity", severity_name(c.level)},
                                    {"kind", c.kind},
                                    {"scope", c.scope},
                                    {"member", c.member},
                                    {"detail", c.detail}};
                if (!c.before.empty()) {
                    e["before"] = c.before;
                }
                if (!c.after.empty()) {
                    e["after"] = c.after;
                }
                j["changes"].push_back(std::move(e));
            }
            return j;
        }

        /**
         * @brief The changes as a human report, grouped by severity (breaking
         * first). `show_compatible` = false leaves additions out.
         */
        inline std::string report_text(const std::vector<Change> &changes,
                                       const std::string &from = {}, const std::string &to = {},
                                       bool show_compatible = true) {
            const std::size_t nb = count(changes, severity::breaking);
            const std::size_t nw = count(changes, severity::warning);
            const std::size_t nc = count(changes, severity::compatible);

            std::string s = "API diff";
            if (!from.empty() || !to.empty()) {
                s += " " + (from.empty() ? std::string("?") : from) + " -> " +
                     (to.empty() ? std::string("?") : to);
            }
            s += ": " + std::to_string(nb) + " breaking, " + std::to_string(nw) + " warning" +
                 (nw == 1 ? "" : "s") + ", " + std::to_string(nc) + " compatible\n";
            if (changes.empty()) {
                return s + "\nNo changes.\n";
            }

            const struct {
                severity    lv;
                const char *title;
                const char *mark;
            } groups[] = {{severity::breaking, "BREAKING", "x"},
                          {severity::warning, "WARNINGS", "!"},
                          {severity::compatible, "COMPATIBLE", "+"}};
            for (const auto &g : groups) {
                if (count(changes, g.lv) == 0 ||
                    (g.lv == severity::compatible && !show_compatible)) {
                    continue;
                }
                s += std::string("\n") + g.title + "\n";
                for (const Change &c : changes) {
                    if (c.level != g.lv) {
                        continue;
                    }
                    s += std::string("  ") + g.mark + " " +
                         diff_detail::qualify(c.scope, c.member) + ": " + c.detail + "  [" +
                         c.kind + "]\n";
                    // A removal has only a `before`, an addition only an
                    // `after`; a rename or a change has both.
                    if (!c.before.empty() && c.before != c.after) {
                        s += "      was: " + c.before + "\n";
                    }
                    if (!c.after.empty() && c.before != c.after) {
                        s += "      now: " + c.after + "\n";
                    }
                }
            }
            return s;
        }

    } // namespace api
} // namespace rosetta
