// Copyright (c) fmaerten@gmail.com
// License: MIT

// Definitions for <rosetta/api.h>. Not a standalone header — included at the
// bottom of <rosetta/generate.h>, after inline/generate.hxx, and reusing the
// JSON helpers of inline/coverage.hxx.

namespace rosetta::api {

    namespace api_detail {

        using coverage::cov_detail::field;
        using coverage::cov_detail::quoted;

        inline std::string flag(const char *key, bool v) {
            return std::string("\"") + key + "\": " + (v ? "true" : "false");
        }

        inline std::string number(double v) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", v);
            return buf;
        }

        inline std::string string_array(const std::vector<std::string> &v) {
            std::string s = "[";
            for (std::size_t i = 0; i < v.size(); ++i) {
                s += (i ? ", " : "") + quoted(v[i]);
            }
            return s + "]";
        }

        // One parameter. `cpp` is the exact C++ spelling when the IR has it
        // (methods, constructors), else the neutral one, so the type string is
        // the same one coverage.json prints for the member.
        inline std::string param(const GenParam &p, const std::string &cpp) {
            std::string s = "{" + field("name", p.name) + ", " +
                            field("type", cpp.empty() ? p.type.spelling : cpp);
            if (p.has_default) {
                s += ", " + field("default", p.default_text);
            }
            return s + ", " + flag("out", p.is_out) + "}";
        }

        inline std::string params(const std::vector<GenParam>        &ps,
                                  const std::vector<std::string> &cpp) {
            const bool  exact = cpp.size() == ps.size();
            std::string s     = "[";
            for (std::size_t i = 0; i < ps.size(); ++i) {
                s += (i ? ", " : "") + param(ps[i], exact ? cpp[i] : std::string());
            }
            return s + "]";
        }

        inline std::string method(const GenMethod &m) {
            return "{" + field("name", m.name) + ", " +
                   field("returns", m.ret_cpp.empty() ? m.ret.spelling : m.ret_cpp) +
                   ", \"params\": " + params(m.params, m.param_cpp) + ", " +
                   flag("static", m.is_static) + ", " + flag("const", m.is_const) + ", " +
                   flag("virtual", m.is_virtual) + ", " + flag("pure", m.is_pure) + ", " +
                   flag("extension", m.is_extension) + "}";
        }

        inline std::string field_entry(const GenField &f) {
            std::string s = "{" + field("name", f.name) + ", " + field("type", f.type.spelling) +
                            ", " + flag("readonly", f.is_readonly);
            if (f.range.has) {
                s += ", \"range\": {\"min\": " + number(f.range.min) +
                     ", \"max\": " + number(f.range.max) + "}";
            }
            if (!f.choices.empty()) {
                s += ", \"choices\": " + string_array(f.choices);
            }
            return s + "}";
        }

        inline std::string class_entry(const GenClass &k) {
            std::string s = "    {" + field("name", qualified_of(k)) + ", " +
                            field("expose", exposed_of(k)) +
                            ", \"bases\": " + string_array(k.bases) + ", " +
                            flag("abstract", k.is_abstract) + ", " + flag("final", k.is_final) +
                            ", " + flag("default_constructible", k.is_default_constructible);

            s += ",\n     \"constructors\": [";
            for (std::size_t i = 0; i < k.ctors.size(); ++i) {
                const std::vector<std::string> none;
                const auto &cpp = i < k.ctor_param_cpp.size() ? k.ctor_param_cpp[i] : none;
                s += std::string(i ? ",\n       " : "\n       ") + "{\"params\": " +
                     params(k.ctors[i], cpp) + "}";
            }
            s += k.ctors.empty() ? "]" : "\n     ]";

            s += ",\n     \"fields\": [";
            for (std::size_t i = 0; i < k.fields.size(); ++i) {
                s += std::string(i ? ",\n       " : "\n       ") + field_entry(k.fields[i]);
            }
            s += k.fields.empty() ? "]" : "\n     ]";

            s += ",\n     \"methods\": [";
            for (std::size_t i = 0; i < k.methods.size(); ++i) {
                s += std::string(i ? ",\n       " : "\n       ") + method(k.methods[i]);
            }
            s += k.methods.empty() ? "]" : "\n     ]";
            return s + "}";
        }

        inline std::string enum_entry(const GenEnum &e) {
            std::string s = "    {" + field("name", qualified_of(e)) + ", " +
                            field("expose", exposed_of(e)) + ", " +
                            field("underlying", e.underlying) + ", \"values\": [";
            for (std::size_t i = 0; i < e.values.size(); ++i) {
                s += std::string(i ? ", " : "") + "{" + field("name", e.values[i].name) +
                     ", \"value\": " + std::to_string(e.values[i].value) + "}";
            }
            return s + "]}";
        }

        inline std::string function_entry(const GenFunction &f) {
            return "    {" + field("name", f.name) + ", " + field("qualified", f.qualified) +
                   ", " + field("returns", f.ret.spelling) +
                   ", \"params\": " + params(f.params, {}) + "}";
        }

    } // namespace api_detail

    inline std::string to_json(const std::vector<GenClass> &classes, const std::vector<GenEnum> &enums,
                               const std::vector<GenFunction> &functions,
                               const std::string              &version) {
        using namespace api_detail;
        std::string s = "{\n  \"rosetta_api\": 1,\n  " + field("version", version) + ",\n";

        s += "  \"classes\": [";
        for (std::size_t i = 0; i < classes.size(); ++i) {
            s += (i ? ",\n" : "\n") + class_entry(classes[i]);
        }
        s += classes.empty() ? "],\n" : "\n  ],\n";

        s += "  \"enums\": [";
        for (std::size_t i = 0; i < enums.size(); ++i) {
            s += (i ? ",\n" : "\n") + enum_entry(enums[i]);
        }
        s += enums.empty() ? "],\n" : "\n  ],\n";

        s += "  \"functions\": [";
        for (std::size_t i = 0; i < functions.size(); ++i) {
            s += (i ? ",\n" : "\n") + function_entry(functions[i]);
        }
        s += functions.empty() ? "]\n" : "\n  ]\n";
        return s + "}\n";
    }

} // namespace rosetta::api
