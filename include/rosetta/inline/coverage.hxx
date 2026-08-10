// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Definitions for <rosetta/coverage.h>. Not a standalone header — it relies on
// the declarations and includes that coverage.h sets up, and is included at its
// bottom.

namespace rosetta::coverage {

    namespace cov_detail {

        // The class's qualified C++ spelling, used as the report's `class` key.
        // Deliberately a local copy of gen_detail::qualified_of's fallback
        // rather than a call to it: this header is included from generate.h
        // BEFORE inline/generate.hxx, so that function does not exist yet.
        inline std::string scope_of(const GenClass &k) {
            if (!k.qualified.empty()) {
                return k.qualified;
            }
            return k.name_space.empty() ? k.name : k.name_space + "::" + k.name;
        }

        // The member's C++ signature, as the report's tie-breaker between two
        // entries that share a name. Exact spellings (ret_cpp / param_cpp) are
        // preferred; an extension method carries none, so the neutral IR
        // spellings stand in.
        inline std::string signature_of(const GenMethod &m) {
            std::string s = m.ret_cpp.empty() ? m.ret.spelling : m.ret_cpp;
            s += " (";
            const bool exact = m.param_cpp.size() == m.params.size();
            for (std::size_t i = 0; i < m.params.size(); ++i) {
                if (i) {
                    s += ", ";
                }
                s += exact ? m.param_cpp[i] : m.params[i].type.spelling;
            }
            s += ")";
            if (m.is_const) {
                s += " const";
            }
            return s;
        }

        inline std::string json_escape(const std::string &in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (const char ch : in) {
                switch (ch) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    // Control characters are not legal raw in a JSON string; a
                    // C++ spelling should never contain one, but the report must
                    // stay parseable even if it does.
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                        out += buf;
                    } else {
                        out += ch;
                    }
                }
            }
            return out;
        }

        inline std::string quoted(const std::string &s) {
            return "\"" + json_escape(s) + "\"";
        }

        // "key": "value" — the only field shape the report emits.
        inline std::string field(const char *key, const std::string &value) {
            return std::string("\"") + key + "\": " + quoted(value);
        }

    } // namespace cov_detail

    inline Log &log() {
        static Log instance;
        return instance;
    }

    inline void reset() {
        log().skips.clear();
        log().bound.clear();
    }

    inline void note_bound(const char *target, const GenClass &k, const GenMethod &m) {
        log().bound.push_back(
            Bound{target, cov_detail::scope_of(k), m.name, cov_detail::signature_of(m)});
    }

    inline void note_bound_field(const char *target, const GenClass &k, const GenField &f) {
        log().bound.push_back(Bound{target, cov_detail::scope_of(k), f.name, f.type.spelling});
    }

    inline void note_skip(const char *target, const GenClass &k, const GenMethod &m,
                          const char *reason, std::string detail) {
        log().skips.push_back(Skip{target, cov_detail::scope_of(k), m.name,
                                   cov_detail::signature_of(m), reason, std::move(detail)});
    }

    inline void note_skip_field(const char *target, const GenClass &k, const GenField &f,
                                const char *reason, std::string detail) {
        log().skips.push_back(Skip{target, cov_detail::scope_of(k), f.name, f.type.spelling,
                                   reason, std::move(detail)});
    }

    inline bool emit_overload(overloads policy, const char *target, const GenClass &k,
                              const GenMethod &m) {
        if (m.overload_count <= 1 || policy == overloads::native) {
            return true;
        }
        if (m.overload_index == 0) {
            return true;
        }
        note_skip(target, k, m, "overload_not_expressible",
                  "the target binds methods by name and " + m.name + " is already registered by "
                  "the first-declared overload; call it through the overload that bound, or give "
                  "this one a distinct name with an extension method");
        return false;
    }

    inline std::string to_json(const std::vector<GenClass> &classes) {
        using cov_detail::field;
        using cov_detail::quoted;

        std::string s = "{\n  \"rosetta_coverage\": 1,\n";

        // ---- reflection stage: what never reached a backend ----
        s += "  \"reflection\": [";
        bool first_class = true;
        for (const GenClass &k : classes) {
            if (k.dropped.empty()) {
                continue;
            }
            s += first_class ? "\n" : ",\n";
            first_class = false;
            s += "    {\n      " + field("class", cov_detail::scope_of(k)) + ",\n";
            s += "      \"dropped\": [";
            for (std::size_t i = 0; i < k.dropped.size(); ++i) {
                const GenDrop &d = k.dropped[i];
                s += i ? ",\n" : "\n";
                s += "        {" + field("member", d.member) + ", " +
                     field("signature", d.signature) + ", " + field("reason", d.reason) + "}";
            }
            s += "\n      ]\n    }";
        }
        s += first_class ? "],\n" : "\n  ],\n";

        // ---- backend stage: one entry per target that recorded anything ----
        // Targets are emitted in first-seen order so the report is stable across
        // runs (a std::map would reorder it into something that reads oddly next
        // to the manifest).
        std::vector<std::string> targets;
        const auto               remember = [&targets](const std::string &t) {
            for (const auto &seen : targets) {
                if (seen == t) {
                    return;
                }
            }
            targets.push_back(t);
        };
        for (const auto &b : log().bound) {
            remember(b.target);
        }
        for (const auto &sk : log().skips) {
            remember(sk.target);
        }

        s += "  \"targets\": [";
        for (std::size_t ti = 0; ti < targets.size(); ++ti) {
            const std::string &target = targets[ti];

            std::size_t n_bound = 0;
            std::size_t n_skip  = 0;
            for (const auto &b : log().bound) {
                n_bound += (b.target == target) ? 1 : 0;
            }
            for (const auto &sk : log().skips) {
                n_skip += (sk.target == target) ? 1 : 0;
            }

            // The classes this target touched, in first-seen order.
            std::vector<std::string> scopes;
            const auto remember_scope = [&scopes](const std::string &sc) {
                for (const auto &seen : scopes) {
                    if (seen == sc) {
                        return;
                    }
                }
                scopes.push_back(sc);
            };
            for (const auto &b : log().bound) {
                if (b.target == target) {
                    remember_scope(b.scope);
                }
            }
            for (const auto &sk : log().skips) {
                if (sk.target == target) {
                    remember_scope(sk.scope);
                }
            }

            s += ti ? ",\n" : "\n";
            s += "    {\n      " + field("target", target) + ",\n";
            s += "      \"bound\": " + std::to_string(n_bound) + ",\n";
            s += "      \"skipped\": " + std::to_string(n_skip) + ",\n";
            s += "      \"classes\": [";
            for (std::size_t si = 0; si < scopes.size(); ++si) {
                const std::string &scope = scopes[si];
                s += si ? ",\n" : "\n";
                s += "        {\n          " + field("class", scope) + ",\n";

                s += "          \"bound\": [";
                bool first = true;
                for (const auto &b : log().bound) {
                    if (b.target != target || b.scope != scope) {
                        continue;
                    }
                    s += first ? "\n" : ",\n";
                    first = false;
                    s += "            {" + field("member", b.member) + ", " +
                         field("signature", b.signature) + "}";
                }
                s += first ? "]," : "\n          ],";

                s += "\n          \"skipped\": [";
                first = true;
                for (const auto &sk : log().skips) {
                    if (sk.target != target || sk.scope != scope) {
                        continue;
                    }
                    s += first ? "\n" : ",\n";
                    first = false;
                    s += "            {" + field("member", sk.member) + ", " +
                         field("signature", sk.signature) + ", " + field("reason", sk.reason) +
                         ", " + field("detail", sk.detail) + "}";
                }
                s += first ? "]" : "\n          ]";

                s += "\n        }";
            }
            s += scopes.empty() ? "]\n    }" : "\n      ]\n    }";
        }
        s += targets.empty() ? "]\n}\n" : "\n  ]\n}\n";

        return s;
    }

} // namespace rosetta::coverage
