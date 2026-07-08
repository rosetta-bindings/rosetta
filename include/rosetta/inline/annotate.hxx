// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Definitions for <rosetta/annotate.h>. Not a standalone header — it relies on
// the declarations and includes that annotate.h sets up, and is included at
// its bottom.

namespace rosetta {

    namespace ann_json {

        // ---- minimal recursive-descent parser over a string_view ----

        consteval double to_double(std::string_view t) {
            std::size_t i    = 0;
            double      sign = 1;
            if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
                if (t[i] == '-') sign = -1;
                ++i;
            }
            double v = 0;
            for (; i < t.size() && t[i] >= '0' && t[i] <= '9'; ++i) v = v * 10 + (t[i] - '0');
            if (i < t.size() && t[i] == '.') {
                ++i;
                double scale = 0.1;
                for (; i < t.size() && t[i] >= '0' && t[i] <= '9'; ++i) {
                    v += (t[i] - '0') * scale;
                    scale *= 0.1;
                }
            }
            // Scientific notation (a range like [1e-10, 1e-6] is routine for a
            // solver tolerance).
            if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
                ++i;
                bool eneg = false;
                if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
                    eneg = (t[i] == '-');
                    ++i;
                }
                int exp = 0;
                for (; i < t.size() && t[i] >= '0' && t[i] <= '9'; ++i) exp = exp * 10 + (t[i] - '0');
                for (; exp > 0; --exp) v = eneg ? v / 10 : v * 10;
            }
            return sign * v;
        }

        struct parser {
            std::string_view s;
            std::size_t      i = 0;

            consteval void ws() {
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
                    ++i;
            }
            consteval bool eat(char c) {
                ws();
                if (i < s.size() && s[i] == c) {
                    ++i;
                    return true;
                }
                return false;
            }
            consteval bool keyword(std::string_view kw) {
                ws();
                if (s.substr(i, kw.size()) == kw) {
                    i += kw.size();
                    return true;
                }
                return false;
            }
            // String value -- returned verbatim (escapes are not unescaped, but
            // an escaped quote does not terminate the string).
            consteval std::string_view str() {
                ws();
                if (i >= s.size() || s[i] != '"') return {};
                ++i;
                std::size_t start = i;
                while (i < s.size() && s[i] != '"') {
                    if (s[i] == '\\' && i + 1 < s.size()) ++i;
                    ++i;
                }
                std::string_view r = s.substr(start, i - start);
                if (i < s.size()) ++i; // closing quote
                return r;
            }
            consteval double number() {
                ws();
                std::size_t start = i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) ++i;
                // exponent part (1e-10, 2.5E+3) — must be consumed here or the
                // stray 'e…' derails every later key in the side-car.
                if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                    ++i;
                    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
                }
                return to_double(s.substr(start, i - start));
            }
            // Skip one value of an unrecognized key (string / number / bool /
            // null / nested array / object), so unknown keys don't derail parse.
            consteval void skip_value() {
                ws();
                if (i >= s.size()) return;
                char c = s[i];
                if (c == '"') {
                    str();
                } else if (c == '[' || c == '{') {
                    char open = c, close = (c == '[') ? ']' : '}';
                    int  depth = 0;
                    for (; i < s.size(); ++i) {
                        if (s[i] == '"') {
                            str();
                            --i; // str() advanced past; loop will ++i
                            continue;
                        }
                        if (s[i] == open) ++depth;
                        else if (s[i] == close && --depth == 0) {
                            ++i;
                            break;
                        }
                    }
                } else if (keyword("true") || keyword("false") || keyword("null")) {
                    // consumed
                } else {
                    number();
                }
            }
        };

        consteval table parse(std::string_view src) {
            table  t;
            parser p{src};
            p.ws();
            if (src.empty() || p.i >= src.size()) return t; // no file -> empty table
            if (!p.eat('{')) {
                t.ok = false;
                return t;
            }
            if (p.eat('}')) return t; // {}
            do {
                member_ann m;
                m.name = p.str();
                if (m.name.empty() || !p.eat(':') || !p.eat('{')) {
                    t.ok = false;
                    break;
                }
                if (!p.eat('}')) {
                    do {
                        std::string_view k = p.str();
                        if (!p.eat(':')) {
                            t.ok = false;
                            break;
                        }
                        if (k == "doc") {
                            m.has_doc = true;
                            m.doc     = p.str();
                        } else if (k == "range") {
                            m.has_range = true;
                            p.eat('[');
                            m.rmin = p.number();
                            p.eat(',');
                            m.rmax = p.number();
                            p.eat(']');
                        } else if (k == "readonly") {
                            m.has_readonly = true;
                            if (p.keyword("true")) m.readonly_on = true;
                            else if (p.keyword("false")) m.readonly_on = false;
                            else m.readonly_on = (p.number() != 0);
                        } else if (k == "combobox") {
                            m.has_combobox = true;
                            p.eat('[');
                            if (!p.eat(']')) {
                                do {
                                    std::string_view c = p.str();
                                    if (m.combo.count < combobox::MAX)
                                        m.combo.items[m.combo.count++] = c;
                                } while (p.eat(','));
                                p.eat(']');
                            }
                        } else if (k == "label") {
                            m.has_label = true;
                            m.label     = p.str();
                        } else if (k == "button") {
                            m.has_button = true;
                            m.button     = p.str();
                        } else if (k == "widget") {
                            m.has_widget = true;
                            m.widget     = p.str();
                        } else {
                            p.skip_value();
                        }
                    } while (p.eat(','));
                    p.eat('}');
                }
                if (t.count < MAX_MEMBERS) t.items[t.count++] = m;
            } while (p.eat(','));
            p.eat('}');
            return t;
        }

    } // namespace ann_json

    namespace detail {

        template <class T>
        consteval std::vector<std::meta::info> json_annotations_for(std::string_view member) {
            std::vector<std::meta::info> out;
            auto                         t = ann_json::parse(rosetta::ann_json_source<T>);
            for (std::size_t k = 0; k < t.count; ++k) {
                const auto &m = t.items[k];
                if (m.name != member) continue;
                if (m.has_doc)
                    out.push_back(
                        std::meta::reflect_constant(rosetta::doc{std::define_static_string(m.doc)}));
                if (m.has_range)
                    out.push_back(std::meta::reflect_constant(rosetta::range{m.rmin, m.rmax}));
                if (m.has_readonly && m.readonly_on)
                    out.push_back(std::meta::reflect_constant(rosetta::readonly{}));
                if (m.has_combobox) {
                    rosetta::combobox cb{};
                    cb.count = m.combo.count;
                    for (std::size_t j = 0; j < m.combo.count; ++j)
                        cb.choices[j] = std::define_static_string(m.combo.items[j]);
                    out.push_back(std::meta::reflect_constant(cb));
                }
                if (m.has_label)
                    out.push_back(std::meta::reflect_constant(
                        rosetta::label{std::define_static_string(m.label)}));
                if (m.has_button)
                    out.push_back(std::meta::reflect_constant(
                        rosetta::button{std::define_static_string(m.button)}));
                if (m.has_widget) {
                    // UI hint tags, matched by name (an unknown value is ignored
                    // rather than fatal, matching skip_value()'s tolerance).
                    if (m.widget == "slider")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::slider));
                    else if (m.widget == "spin")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::spin));
                    else if (m.widget == "checkbox")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::checkbox));
                    else if (m.widget == "textfield")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::textfield));
                    else if (m.widget == "color")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::color));
                    else if (m.widget == "multiline")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::multiline));
                    else if (m.widget == "radio")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::radio));
                    else if (m.widget == "file")
                        out.push_back(std::meta::reflect_constant(rosetta::widget::file));
                }
            }
            return out;
        }

        template <class T>
        consteval std::vector<std::meta::info> merged_annotations(std::meta::info member) {
            std::vector<std::meta::info> v;
            for (auto a : std::meta::annotations_of(member)) v.push_back(std::meta::constant_of(a));
            for (auto e : json_annotations_for<T>(std::meta::identifier_of(member))) v.push_back(e);
            return v;
        }

        template <class T> consteval std::string ann_keys_error() {
            constexpr auto ctx = std::meta::access_context::current();
            auto           t   = ann_json::parse(rosetta::ann_json_source<T>);
            if (!t.ok)
                return std::string("rosetta out-of-line annotations: malformed JSON side-car");
            for (std::size_t k = 0; k < t.count; ++k) {
                bool found = false;
                for (auto m : std::meta::nonstatic_data_members_of(^^T, ctx))
                    if (std::meta::identifier_of(m) == t.items[k].name) {
                        found = true;
                        break;
                    }
                if (!found)
                    for (auto m : std::meta::members_of(^^T, ctx))
                        if (std::meta::is_function(m) && std::meta::has_identifier(m) &&
                            std::meta::identifier_of(m) == t.items[k].name) {
                            found = true;
                            break;
                        }
                if (!found)
                    return std::string("rosetta out-of-line annotations: JSON key \"") +
                           std::string(t.items[k].name) +
                           "\" does not name any member of the annotated type "
                           "(renamed/typo'd field?)";
            }
            return std::string{};
        }

    } // namespace detail

} // namespace rosetta
