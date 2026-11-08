// Copyright (c) fmaerten@gmail.com
// License: MIT

// ---------------------------------------------------------------------------
// --api-diff: the command-line face of <rosetta/api_diff.h>. Reads two
// api.json snapshots (or the folders holding them), prints the classified
// changes, and turns "is this release breaking?" into an exit status a CI job
// can gate on.
// ---------------------------------------------------------------------------

#include "apidiff.h"

#include <rosetta/api_diff.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const char *kApiDiffUsage =
    "usage: rosetta_gen --api-diff <old> <new> [--json] [--breaking-only] [--strict]\n"
    "  <old>, <new>     an api.json, or a folder containing one (e.g. the bindings dir)\n"
    "  --json           machine-readable report on stdout\n"
    "  --breaking-only  leave the compatible additions out of the text report\n"
    "  --strict         exit 1 on warnings too, not only on breaking changes\n";

// An api.json, or the api.json inside a folder. Throws with a readable message.
static nlohmann::json load_snapshot(const std::string &arg) {
    fs::path p = arg;
    if (fs::is_directory(p)) {
        p /= "api.json";
    }
    std::ifstream in(p);
    if (!in) {
        throw std::runtime_error("cannot read " + p.string());
    }
    try {
        return nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception &e) {
        throw std::runtime_error(p.string() + ": " + e.what());
    }
}

int api_diff_main(int argc, char **argv) {
    std::vector<std::string> paths;
    bool                     as_json = false, breaking_only = false, strict = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") {
            as_json = true;
        } else if (a == "--breaking-only") {
            breaking_only = true;
        } else if (a == "--strict") {
            strict = true;
        } else if (a == "-h" || a == "--help") {
            std::fputs(kApiDiffUsage, stdout);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "rosetta_gen --api-diff: unknown option %s\n%s", a.c_str(),
                         kApiDiffUsage);
            return 2;
        } else {
            paths.push_back(a);
        }
    }
    if (paths.size() != 2) {
        std::fputs(kApiDiffUsage, stderr);
        return 2;
    }

    std::vector<rosetta::api::Change> changes;
    std::string                       from, to;
    try {
        const nlohmann::json before = load_snapshot(paths[0]);
        const nlohmann::json after  = load_snapshot(paths[1]);
        from    = before.value("version", std::string());
        to      = after.value("version", std::string());
        changes = rosetta::api::diff(before, after);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rosetta_gen --api-diff: %s\n", e.what());
        return 2;
    }

    if (as_json) {
        std::printf("%s\n", rosetta::api::report_json(changes, from, to).dump(2).c_str());
    } else {
        std::fputs(rosetta::api::report_text(changes, from, to, !breaking_only).c_str(), stdout);
    }

    const bool fail = rosetta::api::count(changes, rosetta::api::severity::breaking) > 0 ||
                      (strict && rosetta::api::count(changes, rosetta::api::severity::warning) > 0);
    return fail ? 1 : 0;
}
