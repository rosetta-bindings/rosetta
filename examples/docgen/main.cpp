// Copyright (c) fmaerten@gmail.com
// License: MIT

// Auto-docgen demo: renders reflection-driven documentation for the annotated
// Algo struct (../Algo.h) in Markdown or HTML, via rosetta::to_markdown<T>() /
// rosetta::to_html<T>(). No external dependencies — just libc++.
//
// Build flags: -freflection -freflection-latest -fannotation-attributes
// See CMakeLists.txt in this folder.
//
// One document goes to stdout, so the usual `> file` redirect still works:
//   ./build/auto_docgen                 # Markdown to stdout (default)
//   ./build/auto_docgen > Algo.md       # capture Markdown
//   ./build/auto_docgen html > Algo.html# capture HTML
//   ./build/auto_docgen md   > Algo.md  # explicit Markdown

#include "../Algo.h"
#include <cstdio>
#include <rosetta/generate.h>
#include <string_view>

int main(int argc, char **argv) {
    const std::string_view fmt = argc > 1 ? argv[1] : "md";

    std::string out;
    if (fmt == "html") {
        out = rosetta::to_html<Algo>("Algo");
    } else if (fmt == "md" || fmt == "markdown") {
        out = rosetta::to_markdown<Algo>("Algo");
    } else {
        std::fprintf(stderr, "usage: %s [md|html]   (default: md)\n", argv[0]);
        return 1;
    }

    std::fputs(out.c_str(), stdout);
    return 0;
}
