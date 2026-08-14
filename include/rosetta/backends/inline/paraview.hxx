// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// ParaView backend — implementation. A *pure-data* backend: it renders ParaView
// Server Manager XML from the erased IR (GenClass fields + annotations).
//
// Each public field becomes a <…VectorProperty> bound to a Set<Name> command,
// with a domain derived from its type/annotations:
//   number          -> Int/DoubleVectorProperty   (+ Int/DoubleRangeDomain from range{})
//   bool            -> IntVectorProperty           (+ BooleanDomain)
//   string          -> StringVectorProperty        (+ StringListDomain from combobox{})
//   enum            -> IntVectorProperty           (+ EnumerationDomain from the enumerators)
//   readonly field  -> information_only="1" (Get<Name>)
//   paraview_array  -> StringVectorProperty bound to SetInputArrayToProcess (+ ArrayListDomain)
// Default member initializers become default_values; doc{} becomes <Documentation>.
// A paraview_input class annotation adds an <InputProperty> (pipeline input).
// Included by inline/generate.hxx (via the .h).

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        // Minimal XML escaping for text and attribute values.
        inline std::string xml_escape(std::string_view s) {
            std::string o;
            o.reserve(s.size());
            for (char ch : s) {
                switch (ch) {
                case '&': o += "&amp;"; break;
                case '<': o += "&lt;"; break;
                case '>': o += "&gt;"; break;
                case '"': o += "&quot;"; break;
                default:  o += ch; break;
                }
            }
            return o;
        }

        // VTK setter/getter command from a field name: threshold -> SetThreshold.
        inline std::string pv_command(std::string_view name, bool getter) {
            std::string s = getter ? "Get" : "Set";
            if (!name.empty()) {
                char c0 = name[0];
                if (c0 >= 'a' && c0 <= 'z') {
                    c0 = static_cast<char>(c0 - ('a' - 'A'));
                }
                s += c0;
                s += name.substr(1);
            }
            return s;
        }

        // One field -> a property element (or "" if the type isn't a simple
        // ParaView property: vectors, nested objects, void, unknown).
        inline std::string pv_property(const GenField &f) {
            // Data-array selector -> SetInputArrayToProcess + ArrayListDomain.
            if (const auto *arr = find_annotation<paraview_array>(f.annotations)) {
                std::string out = "      <StringVectorProperty name=\"" + xml_escape(f.name) +
                                  "\" command=\"SetInputArrayToProcess\""
                                  " number_of_elements=\"5\" element_types=\"0 0 0 0 2\">\n";
                out += "        <ArrayListDomain name=\"array_list\" attribute_type=\"" +
                       xml_escape(arr->attribute_type) + "\" input_domain_name=\"input_array\">\n";
                out += "          <RequiredProperties>\n";
                out += "            <Property name=\"" + xml_escape(arr->input) +
                       "\" function=\"Input\"/>\n";
                out += "          </RequiredProperties>\n";
                out += "        </ArrayListDomain>\n";
                if (!f.doc.empty()) {
                    out += "        <Documentation>" + xml_escape(f.doc) + "</Documentation>\n";
                }
                out += "      </StringVectorProperty>\n";
                return out;
            }

            const bool         info    = f.is_readonly;
            const std::string  command = pv_command(f.name, info);
            const std::string &k       = f.type.kind;

            std::string elem;
            std::string domain;
            if (k == "boolean") {
                elem   = "IntVectorProperty";
                domain = "        <BooleanDomain name=\"bool\"/>\n";
            } else if (k == "number") {
                elem = f.type.integer ? "IntVectorProperty" : "DoubleVectorProperty";
                if (f.range.has) {
                    const std::string dom = f.type.integer ? "IntRangeDomain" : "DoubleRangeDomain";
                    domain = "        <" + dom + " name=\"range\" min=\"" + num_str(f.range.min) +
                             "\" max=\"" + num_str(f.range.max) + "\"/>\n";
                }
            } else if (k == "string") {
                elem = "StringVectorProperty";
                if (!f.choices.empty()) {
                    domain = "        <StringListDomain name=\"list\">\n";
                    for (const auto &c : f.choices) {
                        domain += "          <String value=\"" + xml_escape(c) + "\"/>\n";
                    }
                    domain += "        </StringListDomain>\n";
                }
            } else if (k == "enum") {
                elem = "IntVectorProperty";
                if (!f.type.enumerators.empty()) {
                    domain = "        <EnumerationDomain name=\"enum\">\n";
                    for (const auto &ev : f.type.enumerators) {
                        domain += "          <Entry value=\"" + std::to_string(ev.value) +
                                  "\" text=\"" + xml_escape(ev.name) + "\"/>\n";
                    }
                    domain += "        </EnumerationDomain>\n";
                }
            } else {
                return {}; // vector / object / void / unknown — not a scalar property
            }

            std::string out = "      <" + elem + " name=\"" + xml_escape(f.name) +
                              "\" command=\"" + command + "\" number_of_elements=\"1\"";
            if (!info && !f.default_value.empty()) {
                out += " default_values=\"" + xml_escape(f.default_value) + "\"";
            }
            if (info) {
                out += " information_only=\"1\"";
            }
            out += ">\n";
            out += domain;
            if (!f.doc.empty()) {
                out += "        <Documentation>" + xml_escape(f.doc) + "</Documentation>\n";
            }
            out += "      </" + elem + ">\n";
            return out;
        }

        // The pipeline <InputProperty> for a filter, from paraview_input.
        inline std::string pv_input_property(const paraview_input &in) {
            const std::string name = in.name;
            const std::string type = in.data_type;
            const std::string cmd  = in.command;
            std::string out = "      <InputProperty name=\"" + xml_escape(name) + "\" command=\"" +
                              xml_escape(cmd) + "\">\n";
            out += "        <ProxyGroupDomain name=\"groups\">\n";
            out += "          <Group name=\"sources\"/>\n";
            out += "          <Group name=\"filters\"/>\n";
            out += "        </ProxyGroupDomain>\n";
            out += "        <DataTypeDomain name=\"input_type\">\n";
            out += "          <DataType value=\"" + xml_escape(type) + "\"/>\n";
            out += "        </DataTypeDomain>\n";
            out += "        <InputArrayDomain name=\"input_array\"/>\n";
            out += "      </InputProperty>\n";
            return out;
        }

        inline std::string ParaView::render(const GenContext &c) const {
            std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            out += "<!-- Generated by rosetta::generate — do not edit by hand. -->\n";
            out += "<ServerManagerConfiguration>\n";
            for (const auto &k : c.classes) {
                const auto       *proxy = find_annotation<paraview_proxy>(k.annotations);
                const std::string cls =
                    proxy ? std::string(proxy->vtk_class) : ("vtk" + exposed_of(k));
                const std::string group = proxy ? std::string(proxy->group) : std::string("filters");
                const std::string label =
                    (proxy && proxy->label[0]) ? std::string(proxy->label) : exposed_of(k);

                out += "  <ProxyGroup name=\"" + xml_escape(group) + "\">\n";
                out += "    <SourceProxy name=\"" + xml_escape(exposed_of(k)) + "\" class=\"" +
                       xml_escape(cls) + "\" label=\"" + xml_escape(label) + "\">\n";
                if (const auto *in = find_annotation<paraview_input>(k.annotations)) {
                    out += pv_input_property(*in);
                }
                for (const auto &f : k.fields) {
                    out += pv_property(f);
                }
                out += "    </SourceProxy>\n";
                out += "  </ProxyGroup>\n";
            }
            out += "</ServerManagerConfiguration>\n";
            return out;
        }

        inline void ParaView::emit(const GenContext &c) const {
            write_file(c.out_dir / "paraview" / (c.lib + ".xml"), render(c));
        }

    } // namespace backend

    template <typename... Ts> inline std::string to_paraview_xml(std::string lib) {
        return backend::ParaView{}.render(gen_detail::make_context<Ts...>(std::move(lib)));
    }

} // namespace rosetta
