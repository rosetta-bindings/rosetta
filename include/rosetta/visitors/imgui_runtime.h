// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Header-only, reflection-free Dear ImGui widget kit used by the generated
// imgui-expanded inspector (the ImGui counterpart of qt_widgets_runtime.h).
// NO C++26, NO rosetta reflection — just <imgui.h> and the standard library,
// so the generated inspector builds with a stock C++17 compiler.
//
// Dear ImGui is immediate-mode: the generated draw_<Class>() functions call
// these helpers EVERY FRAME; a helper draws one widget row bound directly to
// the live member (no copies, no signals — the member IS the widget state).

#pragma once

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(std::string*) — imgui/misc/cpp
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace rosetta {
    namespace imgw {

        // Widget hints (rosetta::widget::* annotations). H_NONE picks the
        // default per type: slider when ranged, drag box otherwise.
        enum Hint { H_NONE = 0, H_SLIDER = 1, H_SPIN = 2 };

        namespace detail {
            template <typename T> constexpr ImGuiDataType data_type() {
                if constexpr (std::is_same_v<T, float>) {
                    return ImGuiDataType_Float;
                } else if constexpr (std::is_same_v<T, double>) {
                    return ImGuiDataType_Double;
                } else if constexpr (std::is_signed_v<T>) {
                    return sizeof(T) == 1   ? ImGuiDataType_S8
                           : sizeof(T) == 2 ? ImGuiDataType_S16
                           : sizeof(T) == 4 ? ImGuiDataType_S32
                                            : ImGuiDataType_S64;
                } else {
                    return sizeof(T) == 1   ? ImGuiDataType_U8
                           : sizeof(T) == 2 ? ImGuiDataType_U16
                           : sizeof(T) == 4 ? ImGuiDataType_U32
                                            : ImGuiDataType_U64;
                }
            }
        } // namespace detail

        // "(?)" marker after the current widget; hovering shows the doc text.
        inline void help_tip(const char *doc) {
            if (!doc || !doc[0]) {
                return;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                ImGui::TextUnformatted(doc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        // Arithmetic member: slider when a range annotation bounds it (the
        // slider clamps, so the range is enforced by construction), drag box
        // otherwise; greyed out when readonly.
        template <typename T>
        bool scalar(const char *label, T &v, bool readonly, bool has_range, double lo, double hi,
                    const char *doc, int hint = H_NONE) {
            ImGui::BeginDisabled(readonly);
            bool       changed = false;
            const bool slider  = has_range && hint != H_SPIN;
            if (slider) {
                T tlo = static_cast<T>(lo);
                T thi = static_cast<T>(hi);
                changed = ImGui::SliderScalar(label, detail::data_type<T>(), &v, &tlo, &thi);
            } else {
                changed = ImGui::DragScalar(label, detail::data_type<T>(), &v, 0.1f);
                if (changed && has_range) { // spin hint on a ranged field: clamp
                    if (static_cast<double>(v) < lo) v = static_cast<T>(lo);
                    if (static_cast<double>(v) > hi) v = static_cast<T>(hi);
                }
            }
            ImGui::EndDisabled();
            help_tip(doc);
            return changed;
        }

        inline bool checkbox(const char *label, bool &v, bool readonly, const char *doc) {
            ImGui::BeginDisabled(readonly);
            const bool changed = ImGui::Checkbox(label, &v);
            ImGui::EndDisabled();
            help_tip(doc);
            return changed;
        }

        inline bool text(const char *label, std::string &v, bool readonly, const char *doc) {
            ImGui::BeginDisabled(readonly);
            const bool changed = ImGui::InputText(label, &v);
            ImGui::EndDisabled();
            help_tip(doc);
            return changed;
        }

        // String member with combobox{...} choices.
        inline bool combo(const char *label, std::string &v,
                          std::initializer_list<const char *> choices, bool readonly,
                          const char *doc) {
            ImGui::BeginDisabled(readonly);
            bool changed = false;
            if (ImGui::BeginCombo(label, v.c_str())) {
                for (const char *choice : choices) {
                    if (ImGui::Selectable(choice, v == choice)) {
                        v       = choice;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            help_tip(doc);
            return changed;
        }

        // Enum member: a combo over the reflected enumerators.
        template <typename E>
        bool enum_combo(const char *label, E &v,
                        std::initializer_list<std::pair<const char *, E>> items, bool readonly,
                        const char *doc) {
            const char *current = "?";
            for (const auto &it : items) {
                if (it.second == v) {
                    current = it.first;
                }
            }
            ImGui::BeginDisabled(readonly);
            bool changed = false;
            if (ImGui::BeginCombo(label, current)) {
                for (const auto &it : items) {
                    if (ImGui::Selectable(it.first, it.second == v)) {
                        v       = it.second;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            help_tip(doc);
            return changed;
        }

        // Numeric std::vector member: a collapsible per-element editor with
        // append / drop-last buttons (disabled when readonly).
        template <typename T>
        bool scalar_vector(const char *label, std::vector<T> &v, bool readonly, const char *doc) {
            bool              changed = false;
            const std::string head    = std::string(label) + " [" + std::to_string(v.size()) + "]";
            if (ImGui::TreeNode(label, "%s", head.c_str())) {
                ImGui::BeginDisabled(readonly);
                for (std::size_t i = 0; i < v.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    changed |= ImGui::DragScalar("##el", detail::data_type<T>(), &v[i], 0.1f);
                    ImGui::SameLine();
                    ImGui::Text("[%zu]", i);
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("+")) {
                    v.push_back(T{});
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("-") && !v.empty()) {
                    v.pop_back();
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            help_tip(doc);
            return changed;
        }

        // Placeholder for a member type the inspector can't edit.
        inline void unsupported(const char *label, const char *doc) {
            ImGui::TextDisabled("%s : (unsupported type)", label);
            help_tip(doc);
        }

    } // namespace imgw
} // namespace rosetta
