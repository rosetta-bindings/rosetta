// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the external-library link block (manifest "user_lib").
//
// `user_lib` takes one library or a LIST of them — the bound library plus the
// pre-built ones it depends on — and gen_detail::user_lib_block() emits one
// resolve-and-link stanza per entry into every compiled backend's CMakeLists:
// per-entry variables suffixed _0, _1, … (so several coexist), the preferred
// form (shared / static) tried first with a fallback to whichever is on disk,
// and one de-duplicated rpath list covering every library directory.
//
// Verifies the generated CMake text, not a live build — mirroring
// member_object.cpp / sequence.cpp.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

struct UlPoint {
    double x = 0;
    double y = 0;
};

template <> struct rosetta::binding_info<UlPoint> {
    static constexpr const char *header = "ul.h";
};

static std::size_t count_of(const std::string &hay, const std::string &needle) {
    std::size_t n = 0;
    for (std::size_t at = hay.find(needle); at != std::string::npos;
         at             = hay.find(needle, at + 1)) {
        ++n;
    }
    return n;
}

static rosetta::GenContext ctx_with(std::vector<rosetta::UserLib> libs) {
    auto c      = rosetta::gen_detail::make_context<UlPoint>("ultest");
    c.user_libs = std::move(libs);
    return c;
}

// A manifest with no user_lib (and no sources / defs / link flags) leaves the
// backend CMakeLists exactly as it was before the field existed.
TEST(UserLib, NoLibraryEmitsNothing) {
    EXPECT_EQ(rosetta::gen_detail::user_lib_block(ctx_with({})), "");
}

TEST(UserLib, SingleLibraryEmitsOneStanza) {
    const std::string s =
        rosetta::gen_detail::user_lib_block(ctx_with({{"space", "/opt/space/bin", "shared"}}));
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_0 \"space\")"), std::string::npos);
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_DIR_0 \"/opt/space/bin\")"), std::string::npos);
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_LINK_0 \"shared\")"), std::string::npos);
    EXPECT_EQ(s.find("ROSETTA_USER_LIB_1"), std::string::npos);
    // rpath covers the one directory.
    EXPECT_NE(s.find("list(APPEND _rosetta_rpath \"${ROSETTA_USER_LIB_DIR_0}\")"),
              std::string::npos);
    EXPECT_NE(s.find("BUILD_RPATH \"${_rosetta_rpath}\""), std::string::npos);
    EXPECT_NE(s.find("INSTALL_RPATH \"${_rosetta_rpath}\""), std::string::npos);
}

// An omitted `link` means "shared" — the resolver must still get a concrete
// preference, since it decides which form is tried first.
TEST(UserLib, EmptyLinkDefaultsToShared) {
    const std::string s = rosetta::gen_detail::user_lib_block(ctx_with({{"foo", "/opt/foo", ""}}));
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_LINK_0 \"shared\")"), std::string::npos);
}

// The dependency case: several libraries, each with its own preferred form,
// linked in the manifest's order.
TEST(UserLib, SeveralLibrariesEachGetTheirOwnStanza) {
    const std::string s = rosetta::gen_detail::user_lib_block(
        ctx_with({{"mylib", "/opt/mylib/bin", "shared"},
                  {"foo", "/opt/third/lib", "static"},
                  {"bar", "/opt/third/lib", "shared"}}));

    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_0 \"mylib\")"), std::string::npos);
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_1 \"foo\")"), std::string::npos);
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_2 \"bar\")"), std::string::npos);
    EXPECT_NE(s.find("set(ROSETTA_USER_LIB_LINK_1 \"static\")"), std::string::npos);

    // Link order follows the manifest order (static archives require it).
    const auto p0 = s.find("\"mylib\"");
    const auto p1 = s.find("\"foo\"");
    const auto p2 = s.find("\"bar\"");
    EXPECT_LT(p0, p1);
    EXPECT_LT(p1, p2);

    // Every entry is actually linked, and each directory searched.
    EXPECT_EQ(3u, count_of(s, "target_link_libraries(${ROSETTA_BINDING_TARGET} PRIVATE "
                              "\"${_rosetta_lib}\")"));
    EXPECT_NE(s.find("target_link_directories(${ROSETTA_BINDING_TARGET} PRIVATE "
                     "${ROSETTA_USER_LIB_DIR_2})"),
              std::string::npos);

    // One rpath list for all of them, with the duplicate directory collapsed.
    EXPECT_NE(s.find("list(REMOVE_DUPLICATES _rosetta_rpath)"), std::string::npos);
    EXPECT_EQ(1u, count_of(s, "BUILD_RPATH"));
    EXPECT_EQ(3u, count_of(s, "list(APPEND _rosetta_rpath"));
}

// The static preference must flip the candidate order for THAT entry only.
TEST(UserLib, StaticPreferenceIsPerEntry) {
    const std::string s = rosetta::gen_detail::user_lib_block(
        ctx_with({{"a", "/opt/a", "static"}, {"b", "/opt/b", "shared"}}));
    EXPECT_NE(s.find("if(ROSETTA_USER_LIB_LINK_0 STREQUAL \"static\")"), std::string::npos);
    EXPECT_NE(s.find("if(ROSETTA_USER_LIB_LINK_1 STREQUAL \"static\")"), std::string::npos);
    // Each stanza resolves against its own suffixed variables — never entry 0's.
    EXPECT_NE(s.find("${ROSETTA_USER_LIB_DIR_1}/${CMAKE_STATIC_LIBRARY_PREFIX}"
                     "${ROSETTA_USER_LIB_1}${CMAKE_STATIC_LIBRARY_SUFFIX}"),
              std::string::npos);
}

// A library that isn't built yet at configure time falls back to -l<name>,
// resolved at build time from the link directory.
TEST(UserLib, UnbuiltLibraryFallsBackToDashL) {
    const std::string s = rosetta::gen_detail::user_lib_block(
        ctx_with({{"space", "/opt/space/bin", "shared"}, {"z", "/usr/lib", "shared"}}));
    EXPECT_NE(s.find("set(_rosetta_lib \"-l${ROSETTA_USER_LIB_0}\")"), std::string::npos);
    EXPECT_NE(s.find("set(_rosetta_lib \"-l${ROSETTA_USER_LIB_1}\")"), std::string::npos);
}
