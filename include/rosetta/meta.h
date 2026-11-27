#pragma once
// The one place rosetta includes the reflection library.
//
// C++26 ships it as <meta> (GCC 16+ with -freflection). The Bloomberg
// clang-p2996 fork predates that and ships <experimental/meta>. Everything else
// in rosetta includes this header instead of naming either one.
#if __has_include(<meta>)
#include <meta>
#elif __has_include(<experimental/meta>)
#include <experimental/meta>
#else
#error "rosetta: no reflection library — need GCC 16+ (-freflection) or clang-p2996"
#endif

// The only compiler-specific switch rosetta's reflection code needs: GCC
// implements the C++26 consteval rules strictly, so a few walk-time constructs
// take a spelling both compilers accept (see the uses of this macro).
#if defined(__GNUC__) && !defined(__clang__)
#define ROSETTA_REFLECTION_GCC 1
#else
#define ROSETTA_REFLECTION_GCC 0
#endif

#include <string_view>

namespace rosetta {
    /// std::define_static_string for anything string-like. The standard
    /// overload takes a range only, so a bare `const char *` is rejected by
    /// GCC (clang-p2996 also has a pointer overload); routing every call
    /// through std::string_view is accepted by both.
    consteval const char *static_string(std::string_view s) {
        return std::define_static_string(s);
    }
} // namespace rosetta

#include <optional>

namespace rosetta {
    /// The first annotation of type A on `r`, if any. clang-p2996 ships this as
    /// std::meta::annotation_of_type<A>; it is not in C++26, so rosetta spells
    /// it with the standard annotations_of / constant_of / extract.
    template <class A> consteval std::optional<A> annotation_of_type(std::meta::info r) {
        for (std::meta::info a : std::meta::annotations_of(r)) {
            if (std::meta::remove_cvref(std::meta::type_of(a)) == std::meta::dealias(^^A)) {
                return std::meta::extract<A>(std::meta::constant_of(a));
            }
        }
        return std::nullopt;
    }
} // namespace rosetta

#include <string>

namespace rosetta {
    namespace detail {
        consteval bool display_ident_char(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_';
        }

        // Replace every whole-token occurrence of `from` in `s` by `to`.
        consteval void display_replace_token(std::string &s, std::string_view from,
                                             std::string_view to) {
            std::string::size_type pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                const auto end   = pos + from.size();
                const bool left  = pos == 0 || !display_ident_char(s[pos - 1]);
                const bool right = end >= s.size() || !display_ident_char(s[end]);
                if (left && right) {
                    s.replace(pos, from.size(), to);
                    pos += to.size();
                } else {
                    pos = end;
                }
            }
        }

        // GCC's display_string_of is diagnostic text, not always C++:
        //   std::string {aka std::__cxx11::basic_string<char>}   (alias + aka)
        //   std::vector<std::__cxx11::basic_string<char> >       (ABI namespace)
        //   long long unsigned int, const Foo&                   (spelling style)
        // Bring it to the clang-p2996 conventions rosetta's emitters (and
        // their tests) were written against, and to code that compiles on
        // every standard library.
        consteval std::string normalize_gcc_display(std::string s) {
            // An alias reports "Alias {aka Canonical}": keep the canonical
            // type, as clang does for the dealiased reflection.
            if (const auto p = s.find(" {aka "); p != std::string::npos && s.ends_with('}')) {
                s = s.substr(p + 6, s.size() - p - 7);
            }
            // libstdc++'s inline ABI namespace — valid only against libstdc++.
            for (std::string::size_type p; (p = s.find("__cxx11::")) != std::string::npos;) {
                s.erase(p, 9);
            }
            display_replace_token(s, "std::basic_string<char>", "std::string");
            display_replace_token(s, "std::basic_string_view<char>", "std::string_view");
            // Builtin integer names, longest first.
            display_replace_token(s, "long long unsigned int", "unsigned long long");
            display_replace_token(s, "long long int", "long long");
            display_replace_token(s, "long unsigned int", "unsigned long");
            display_replace_token(s, "short unsigned int", "unsigned short");
            display_replace_token(s, "long int", "long");
            display_replace_token(s, "short int", "short");
            // "> >" -> ">>", and "T*" / "T&" -> "T *" / "T &".
            std::string out;
            for (std::string::size_type i = 0; i < s.size(); ++i) {
                const char c = s[i];
                if (c == ' ' && !out.empty() && out.back() == '>' && i + 1 < s.size() &&
                    s[i + 1] == '>') {
                    continue;
                }
                if ((c == '*' || c == '&') && !out.empty() &&
                    (display_ident_char(out.back()) || out.back() == '>')) {
                    out += ' ';
                }
                out += c;
            }
            return out;
        }
    } // namespace detail

    /// std::meta::display_string_of, normalized across compilers. clang-p2996:
    /// verbatim. GCC: see detail::normalize_gcc_display. Use this (not the raw
    /// call) wherever the text can end up in generated code or reports.
    consteval std::string display_type(std::meta::info r) {
#if ROSETTA_REFLECTION_GCC
        return detail::normalize_gcc_display(std::string(std::meta::display_string_of(r)));
#else
        return std::string(std::meta::display_string_of(r));
#endif
    }
} // namespace rosetta
