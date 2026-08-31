// Copyright (c) fmaerten@gmail.com
// License: MIT

// Emits the ParaView Server Manager XML for the annotated Threshold spec
// (threshold.h) via rosetta::to_paraview_xml<T>(). The XML goes to stdout, so:
//
//   ./build/paraview_xml                      # print to stdout
//   ./build/paraview_xml > Threshold.xml      # capture the plugin XML
//
// Build flags: -freflection -freflection-latest -fannotation-attributes.

#include "threshold.h"
#include <cstdio>
#include <rosetta/generate.h>

int main() {
    std::fputs(rosetta::to_paraview_xml<Threshold>("RosettaThreshold").c_str(), stdout);
    return 0;
}
