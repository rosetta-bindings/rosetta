// Copyright (c) fmaerten@gmail.com
// License: MIT

// Public ParaView annotations. Annotate a class with these to describe the
// ParaView plugin proxy the `paraview` backend should emit. They live here, not
// in <rosetta/annotations.h>, so the generic core (walk / GenClass / generate)
// carries no ParaView knowledge — it stores every annotation type-erased and the
// ParaView backend queries the ones below. Include this where you annotate, e.g.:
//
//   #include <rosetta/paraview.h>
//   struct [[= rosetta::paraview_proxy{"vtkClip"},
//           = rosetta::paraview_input{"Input", "vtkDataSet"}]] Clip {
//       [[= rosetta::range{0,1}]]                      double value = 0.5;
//       [[= rosetta::paraview_array{"Scalars","Input"}]] std::string scalars;
//   };

#pragma once

#include <cstddef>
#include <experimental/meta>

namespace rosetta {

    /**
     * @brief Class-level: the C++ vtkAlgorithm subclass the generated Server
     * Manager XML proxy wraps (`class="..."`), its proxy group, and an optional
     * human label. Absent -> the backend falls back to `vtk<ClassName>` /
     * `filters` / the identifier.
     */
    struct paraview_proxy {
        const char *vtk_class; // C++ vtkAlgorithm subclass spelled in class="..."
        const char *group;     // "filters" / "sources" / "readers" / "writers"
        const char *label;     // human-facing label ("" -> the identifier)
        consteval paraview_proxy(const char *c, const char *g = "filters", const char *l = "")
            : vtk_class(std::define_static_string(c)), group(std::define_static_string(g)),
              label(std::define_static_string(l)) {}
        bool operator==(const paraview_proxy &) const = default;
    };

    /**
     * @brief Class-level: declares a pipeline input port, emitted as an
     * <InputProperty> (with ProxyGroup / DataType / InputArray domains). Present
     * => the proxy is a filter that takes input.
     */
    struct paraview_input {
        const char *name;       // <InputProperty name="...">
        const char *data_type;  // accepted vtkDataObject subclass (e.g. "vtkDataSet")
        const char *command;    // setter command
        consteval paraview_input(const char *n = "Input", const char *dt = "vtkDataSet",
                                 const char *cmd = "SetInputConnection")
            : name(std::define_static_string(n)), data_type(std::define_static_string(dt)),
              command(std::define_static_string(cmd)) {}
        bool operator==(const paraview_input &) const = default;
    };

    /**
     * @brief Field-level: marks a string field as a data-array selector, emitted
     * as a StringVectorProperty bound to SetInputArrayToProcess with an
     * <ArrayListDomain> tied to an input port.
     */
    struct paraview_array {
        const char *attribute_type; // "Scalars" / "Vectors" / "Tensors" / ...
        const char *input;          // input port name the arrays come from
        consteval paraview_array(const char *at = "Scalars", const char *in = "Input")
            : attribute_type(std::define_static_string(at)), input(std::define_static_string(in)) {}
        bool operator==(const paraview_array &) const = default;
    };

} // namespace rosetta
