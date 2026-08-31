// Copyright (c) fmaerten@gmail.com
// License: MIT

// Smoke test for the reflection-driven Markdown reference (rosetta::to_markdown).
// Renders Person and prints its docs to stdout.

#include "../examples/person.h"
#include <cstdio>
#include <rosetta/generate.h>

int main() {
    const auto md = rosetta::to_markdown<Person>();
    std::fputs(md.c_str(), stdout);
    return 0;
}
