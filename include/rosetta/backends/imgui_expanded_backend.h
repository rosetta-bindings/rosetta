// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Dear ImGui generation backend — expanded / reflection-free, the immediate-
// mode counterpart of the qt-expanded inspector.
//
// Emits a self-contained desktop app (GLFW + OpenGL 3 + Dear ImGui, both
// fetched automatically at configure time) with one inspector tab per
// default-constructible bound class:
//   - numeric fields   -> drag boxes, or clamping sliders when a `range`
//                         annotation bounds them (widget::slider / widget::spin
//                         hints honored);
//   - bool fields      -> checkboxes;  string fields -> text inputs
//                         (`combobox` choices become a combo);
//   - enum fields      -> a combo over the reflected enumerators;
//   - numeric vectors  -> a collapsible per-element editor (+ / - resize);
//   - readonly         -> greyed out;  `doc` -> a "(?)" hover tooltip;
//   - methods          -> a tree node with one input per scalar parameter and
//                         a call button showing the stringified result
//                         (`button` annotation labels it).
//
// Because ImGui is immediate-mode, the generated draw_<Class>() functions run
// every frame and bind widgets DIRECTLY to the live members — the object is
// the single source of truth, no copies, no signal plumbing. The widget kit
// lives in the header-only, reflection-free
// <rosetta/visitors/imgui_runtime.h> (the ImGui qt_widgets_runtime.h).
//
// Registered under the "imgui-expanded" target; builds with a stock C++17
// compiler (the usual expanded caveat applies: bound headers must be stock
// C++, i.e. annotations supplied out of line). ROSETTA_IMGUI_FRAMES=N in the
// environment auto-exits after N frames (headless smoke tests / CI).
//
// Part of the generate pipeline (included by inline/generate.hxx after the
// qt-expanded backend, whose qtx_display()/qtx_method_ok() helpers it
// reuses). The emit() implementation lives in
// inline/imgui_expanded_backend.hxx.

#pragma once

namespace rosetta {
    namespace gen_detail {

        struct ImGuiExpandedBackend : Backend {
            void emit(const GenContext &c) const override;
        };

    } // namespace gen_detail
} // namespace rosetta

#include "inline/imgui_expanded_backend.hxx"
