// Copyright (c) fmaerten@gmail.com
// License: MIT

// ParaView backend — declaration. Emits a ParaView *Server Manager XML* proxy
// definition for each class: the property panel (one <…VectorProperty> per
// public field, with range / enumeration / boolean domains and documentation
// from the same annotations every other backend understands). The proxy's
// class="…"/group come from the rosetta::paraview_proxy class annotation (else
// vtk<Name>/filters).
//
// Scope: the *property half* — fields -> property widgets. VTK pipeline concepts
// (input ports, array-selection domains) are not generated here.
//
// Part of the generate pipeline (included by inline/generate.hxx after the
// shared render helpers); the implementation lives in inline/paraview.hxx.
// Not a standalone header — it relies on Backend / GenContext from
// <rosetta/generate.h> being already visible.

#pragma once

#include <rosetta/paraview.h> // the ParaView annotation types this backend reads

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct ParaView : Backend {
            void        emit(const GenContext &c) const override;   // writes <out>/paraview/<lib>.xml
            std::string render(const GenContext &c) const override; // the same XML, as a string
        };

    } // namespace backend

    /// Reflection-driven ParaView Server Manager XML for one or more types, as a
    /// string (no files written). The pipeline-free entry point.
    template <typename... Ts> std::string to_paraview_xml(std::string lib = "doc");

} // namespace rosetta

#include "inline/paraview.hxx"
