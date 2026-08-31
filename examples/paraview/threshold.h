// Copyright (c) fmaerten@gmail.com
// License: MIT

// A rosetta "spec" for VTK's vtkThreshold filter. This struct is NOT the filter
// itself — it is the annotated description rosetta reflects to emit the ParaView
// Server Manager XML that wires vtkThreshold's setters to a property panel. Each
// public field maps to a `Set<Name>` command; the annotations choose the widget
// / domain. It exercises *every* capability of the ParaView backend:
//
//   paraview_proxy   -> <SourceProxy class="vtkThreshold" ...>
//   paraview_input   -> <InputProperty> (pipeline input port)
//   paraview_array   -> <StringVectorProperty SetInputArrayToProcess> + ArrayListDomain
//   enum field       -> <IntVectorProperty> + EnumerationDomain
//   range{} (double) -> DoubleRangeDomain
//   range{} (int)    -> IntRangeDomain
//   bool             -> BooleanDomain
//   combobox{}       -> StringListDomain
//   readonly         -> information_only="1" (Get<Name>)
//   member defaults  -> default_values="..."
//   doc{}            -> <Documentation>

#pragma once

#include <rosetta/annotations.h> // doc / range / combobox / readonly
#include <rosetta/paraview.h>    // paraview_proxy / paraview_input / paraview_array
#include <string>

// How the lower/upper bounds are combined (maps to vtkThreshold::SetThresholdFunction).
enum class ThresholdFunction {
    Between = 0, // THRESHOLD_BETWEEN
    Upper   = 1, // THRESHOLD_UPPER
    Lower   = 2, // THRESHOLD_LOWER
};

struct [[= rosetta::paraview_proxy{"vtkThreshold", "filters", "Threshold (rosetta)"},
        = rosetta::paraview_input{"Input", "vtkDataSet"}]] Threshold {

    // Data array the criterion is computed from — a pipeline array selector.
    [[= rosetta::doc{"Data array the threshold is computed from"},
       = rosetta::paraview_array{"Scalars", "Input"}]]
    std::string scalars;

    // Enumerated mode -> EnumerationDomain (Between / Upper / Lower).
    [[= rosetta::doc{"Keep cells between the bounds, or only above / below one bound"}]]
    ThresholdFunction thresholdFunction = ThresholdFunction::Between;

    [[= rosetta::doc{"Lower bound of the kept range"}, = rosetta::range{0.0, 1.0}]]
    double lowerThreshold = 0.0;

    [[= rosetta::doc{"Upper bound of the kept range"}, = rosetta::range{0.0, 1.0}]]
    double upperThreshold = 1.0;

    [[= rosetta::doc{"Require every selected component to satisfy the criterion"}]]
    bool allScalars = true;

    [[= rosetta::doc{"Component index used in 'Selected' component mode"},
       = rosetta::range{0, 8}]]
    int selectedComponent = 0;

    [[= rosetta::doc{"How multi-component arrays are tested"},
       = rosetta::combobox{{"Selected", "All", "Any"}}]]
    std::string componentMode = "Selected";

    // Read back from the server after execution -> information_only.
    [[= rosetta::doc{"Number of cells kept after thresholding"}, = rosetta::readonly{}]]
    int outputCellCount = 0;
};
