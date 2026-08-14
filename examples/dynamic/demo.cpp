// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// A property sheet and a command interpreter for a library this file has never
// heard of.
//
// Read the includes: <rosetta/dynamic.h>, the shared interpreter, and the
// generated "auto_dynamic.h". NOT scene.h. Nothing below names Mesh, Vec3 or
// Shading, and nothing below changes when the bound library grows a class.
// Every widget, every label, every range check and every call is produced by
// querying the metadata that the `dynamic` backend emitted.
//
// The interpreter itself lives in interp.h, shared verbatim with the Qt viewer
// in qt/ — this file is only the terminal presentation of it.
//
// Stock C++20 — no reflection, no rosetta generator, no C++26 toolchain.

#include <rosetta/dynamic.h>

#include <iomanip>
#include <iostream>
#include <string>

#include "auto_dynamic.h"
#include "interp.h"

namespace rd = rosetta::dyn;

// ---------------------------------------------------------------------------
// The property sheet — this is the "menus without codegen" claim
// ---------------------------------------------------------------------------

static void property_sheet(const rd::Object &obj) {
    const rd::MetaClass &k = *obj.meta();
    std::cout << "\n+-- " << k.name << " "
              << std::string(58 - std::string(k.name).size(), '-') << "+\n";
    if (*k.doc) {
        std::cout << "|  " << k.doc << "\n";
    }

    for (std::size_t i = 0; i < k.n_fields; ++i) {
        const rd::MetaField &f = k.fields[i];

        rd::Result  v     = obj.get(f.name);
        std::string shown = v.ok() ? v.value.to_string() : "<" + v.error + ">";
        if (f.type->kind == rd::Kind::enum_ && v.ok()) {
            shown = dynui::enumerator_name(f.type, v.value.as_int());
        }

        std::cout << "|  " << std::left << std::setw(12) << dynui::label_of(f) << std::setw(11)
                  << (f.readonly ? "(readonly)" : dynui::widget_for(f)) << std::setw(22) << shown;

        if (f.range.has) {
            std::cout << std::noshowpoint << "[" << f.range.lo << ".." << f.range.hi << "] ";
        }
        const auto ch = dynui::choices_for(f);
        for (std::size_t j = 0; j < ch.size(); ++j) {
            std::cout << (j ? "|" : "{") << ch[j];
        }
        if (!ch.empty()) {
            std::cout << "} ";
        }
        std::cout << "\n";
        if (*f.doc) {
            std::cout << "|  " << std::string(23, ' ') << "\033[2m" << f.doc << "\033[0m\n";
        }
    }

    // Methods annotated `button` become the action row; the rest are callable
    // but not offered as one-click actions.
    std::string actions;
    for (std::size_t i = 0; i < k.n_methods; ++i) {
        const rd::MetaMethod &m = k.methods[i];
        const std::string     b = dynui::ann(m.annotations, m.n_annotations, "button");
        if (!b.empty() && m.invoke) {
            actions += " [" + b + "]";
        }
    }
    if (!actions.empty()) {
        std::cout << "|  Actions:" << actions << "\n";
    }
    std::cout << "+" << std::string(60, '-') << "+\n";
}

// ---------------------------------------------------------------------------

static dynui::Interp interp;

static void run(const std::string &line) {
    if (line.empty() || line[0] == '#') {
        if (!line.empty()) {
            std::cout << "\033[36m> " << line << "\033[0m\n";
        }
        return;
    }
    std::cout << "\033[36m> " << line << "\033[0m\n";

    // `sheet` is a presentation command, so it belongs to this front-end rather
    // than to the shared interpreter.
    if (line.rfind("sheet ", 0) == 0) {
        const std::string name = line.substr(6);
        auto              it   = interp.vars.find(name);
        if (it == interp.vars.end()) {
            std::cout << "  \033[31m! no such variable: " << name << "\033[0m\n";
        } else {
            property_sheet(it->second);
        }
        return;
    }
    interp.run(line);
}

// A canned session, so `./demo` is reproducible. Pass -i to type your own.
static const char *SCRIPT[] = {
    "classes",
    "static m scene::Mesh cube",
    "sheet m",
    "methods scene::Mesh",

    "# --- annotations enforced once, in the core, for every host ---",
    "set m opacity 0.4",
    "set m opacity 7",
    "set m id hacked",
    "set m preset glossy",

    "# --- overload resolution: both at() survive, and a miss explains itself ---",
    "call m at 1",
    "call m at 1 2",
    "call m at nope",

    "# --- one bound class passed to another's method ---",
    "new v scene::Vec3 1 2 2",
    "call v norm",
    "call m translate $v",
    "get m origin",

    "# --- a T& return is PINNED to its parent, and writes through to it ---",
    "call m originRef",
    "set _ x 10",
    "call m originRef",
    "get _ x",

    "# --- an enumerator by NAME, resolved against the TypeDesc by the host ---",
    "set m shading Wireframe",
    "get m shading",
    "set m shading Nonsense",

    "# --- geometry, and a mutation the Qt viewer redraws live ---",
    "call m vertexCount",
    "call m triangleCount",
    "call m subdivide",
    "call m describe",

    "# --- static factory, and a method the model cannot marshal ---",
    "static s scene::Mesh sphere 8 12",
    "call s describe",
    "call m onProgress 1",

    "sheet m",
};

int main(int argc, char **argv) {
    // The one line that mentions the generated module at all.
    scene::register_all();

    interp.out = [](const std::string &s, bool is_error) {
        if (is_error) {
            std::cout << "  \033[31m! " << s << "\033[0m\n";
        } else {
            std::cout << "  " << s << "\n";
        }
    };

    if (argc > 1 && std::string(argv[1]) == "-i") {
        std::cout << "dynamic> type `classes` to start, Ctrl-D to quit\n";
        for (std::string line; std::getline(std::cin, line);) {
            run(line);
        }
        return 0;
    }
    for (const char *line : SCRIPT) {
        run(line);
    }
    return 0;
}
