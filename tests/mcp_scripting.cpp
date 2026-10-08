// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the MCP scripting tools: `run_script` (Lua,
// <rosetta/runtime/mcp_lua.h>) and `run_python` (CPython,
// <rosetta/runtime/mcp_python.h>), over the same hand-built metadata as
// tests/mcp.cpp.
//
// Built only when CMake finds Lua 5.3+; the Python half additionally needs
// Python 3.10+ and pybind11 (ROSETTA_TEST_MCP_PYTHON).

#include "mcp_fixture.h"

#include <rosetta/runtime/mcp_lua.h>
#if ROSETTA_TEST_MCP_PYTHON
#include <rosetta/runtime/mcp_python.h>
#endif

namespace {

    struct McpLua : Mcp {
        void SetUp() override { rosetta::mcp::enable_lua(server); }

        std::pair<bool, json> lua(const std::string &code, double timeout_s = 5) {
            return tool("run_script", {{"code", code}, {"timeout_s", timeout_s}});
        }
    };

    std::string text(const json &j) { return j.is_string() ? j.get<std::string>() : j.dump(); }

} // namespace

// ---------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------

TEST_F(McpLua, ToolIsListed) {
    bool found = false;
    for (const json &t : rpc("tools/list")["result"]["tools"]) {
        found = found || t["name"] == "run_script";
    }
    EXPECT_TRUE(found);
}

TEST_F(McpLua, DrivesObjectsAndPublishesHandles) {
    auto [err, r] = lua(R"(
        local ada = new("Person", "Ada", 36)
        keep("ada", ada)
        for i = 1, 3 do ada.age = ada.age + 1 end
        local kid = classes.Person.make("Kid")
        print("made", kid.name)
        return {age = ada.age, prod = ada:at(3, 4), one = ada:at(3),
                older = ada:olderThan(kid), kid = kid}
    )");
    ASSERT_FALSE(err) << r;
    EXPECT_EQ(r["result"]["age"], 39);
    EXPECT_EQ(r["result"]["prod"], 12.0);
    EXPECT_EQ(r["result"]["one"], 3.0);
    EXPECT_EQ(r["result"]["older"], true);
    EXPECT_EQ(r["output"], "made\tKid\n");
    // keep() and a returned object both land in the handle table.
    EXPECT_NE(server.find("ada"), nullptr);
    ASSERT_TRUE(r["result"]["kid"].contains("$ref"));
    EXPECT_NE(server.find(r["result"]["kid"]["$ref"].get<std::string>()), nullptr);
    // ...and later tool calls see them.
    EXPECT_EQ(tool("inspect_object", {{"object", "ada"}}).second["fields"]["age"], 39);
}

TEST_F(McpLua, EnumsAreNamesAndHandlesAreShared) {
    tool("create_object", {{"class", "Person"}, {"args", {"Bo", 5}}, {"name", "bo"}});
    auto [err, r] = lua(R"(
        local bo = obj("bo")
        bo.colour = "Green"
        return bo.colour
    )");
    ASSERT_FALSE(err) << r;
    EXPECT_EQ(r["result"], "Green");
    EXPECT_EQ(tool("inspect_object", {{"object", "bo"}}).second["fields"]["colour"], "Green");
}

TEST_F(McpLua, CoreRejectionsAreLuaErrors) {
    auto [err, msg] = lua(R"(local p = new("Person"); p.age = 400)");
    EXPECT_TRUE(err);
    EXPECT_NE(text(msg).find("outside [0, 150]"), std::string::npos) << msg;
    EXPECT_NE(text(msg).find("stack traceback"), std::string::npos) << msg;

    auto [err2, r] = lua(R"(
        local ok, e = pcall(function() new("Person").id = "x" end)
        return {ok = ok, e = e}
    )");
    ASSERT_FALSE(err2) << r;
    EXPECT_EQ(r["result"]["ok"], false);
    EXPECT_NE(text(r["result"]["e"]).find("read-only"), std::string::npos);

    auto [err3, m3] = lua(R"(return new("Person").at(1))");
    EXPECT_TRUE(err3);
    EXPECT_NE(text(m3).find("call methods with ':'"), std::string::npos) << m3;
}

TEST_F(McpLua, Sandboxed) {
    auto [err, r] = lua("return {io = io == nil, os = os == nil, require = require == nil, "
                        "load = load == nil, dofile = dofile == nil}");
    ASSERT_FALSE(err) << r;
    for (const char *k : {"io", "os", "require", "load", "dofile"}) {
        EXPECT_EQ(r["result"][k], true) << k;
    }
}

TEST_F(McpLua, TimeAndMemoryLimits) {
    auto [err, msg] = lua("while true do end", 0.2);
    EXPECT_TRUE(err);
    EXPECT_NE(text(msg).find("time limit"), std::string::npos) << msg;

    rosetta::mcp::LuaOptions small;
    small.max_memory = 4u << 20;
    EXPECT_THROW(
        {
            try {
                rosetta::mcp::run_lua(server, "local t = {} for i = 1, 1e8 do t[i] = i end", 10,
                                      small);
            } catch (const Error &e) {
                EXPECT_NE(std::string(e.what()).find("not enough memory"), std::string::npos);
                throw;
            }
        },
        Error);
}

TEST_F(McpLua, VectorsArriveWholeButResultsAreSummarized) {
    // max_list_items is 2 in the fixture: the script sees all 3 scores, the
    // agent gets a summary of a 3-element return.
    auto [err, r] = lua(R"(
        local p = new("Person")
        local s = p.scores
        p.scores = {4, 5, 6, 7}
        return {n = #s, back = p.scores}
    )");
    ASSERT_FALSE(err) << r;
    EXPECT_EQ(r["result"]["n"], 3);
    EXPECT_TRUE(r["result"]["back"]["$summary"].get<bool>());
    EXPECT_EQ(r["result"]["back"]["length"], 4);
}

// ---------------------------------------------------------------------------
// Python
// ---------------------------------------------------------------------------

#if ROSETTA_TEST_MCP_PYTHON

namespace {
    struct McpPython : Mcp {
        void SetUp() override { rosetta::mcp::enable_python(server); }

        std::pair<bool, json> py(const std::string &code, double timeout_s = 5) {
            return tool("run_python", {{"code", code}, {"timeout_s", timeout_s}});
        }
    };
} // namespace

TEST_F(McpPython, LastExpressionIsTheResult) {
    auto [err, r] = py(R"(
ada = new("Person", "Ada", 36)
keep("ada", ada)
for _ in range(3):
    ada.age += 1
kid = classes.Person.make("Kid")
print("made", kid.name)
{"age": ada.age, "prod": ada.at(i=3, j=4), "older": ada.olderThan(kid), "kid": kid}
)");
    ASSERT_FALSE(err) << r;
    EXPECT_EQ(r["result"]["age"], 39);
    EXPECT_EQ(r["result"]["prod"], 12.0);
    EXPECT_EQ(r["result"]["older"], true);
    EXPECT_EQ(r["output"], "made Kid\n");
    EXPECT_NE(server.find("ada"), nullptr);
    EXPECT_TRUE(r["result"]["kid"].contains("$ref"));
}

TEST_F(McpPython, CoreRejectionsAreExceptions) {
    auto [err, msg] = py(R"(p = new("Person"); p.age = 400)");
    EXPECT_TRUE(err);
    EXPECT_NE(text(msg).find("outside [0, 150]"), std::string::npos) << msg;

    auto [err2, r] = py(R"(
try:
    new("Person").id = "x"
    result = "accepted"
except RuntimeError as e:
    result = str(e)
)");
    ASSERT_FALSE(err2) << r;
    EXPECT_NE(text(r["result"]).find("read-only"), std::string::npos);

    auto [err3, m3] = py(R"(new("Person").nope)");
    EXPECT_TRUE(err3);
    EXPECT_NE(text(m3).find("AttributeError"), std::string::npos) << m3;
}

TEST_F(McpPython, TimeoutDoesNotLeakIntoTheNextRun) {
    auto [err, msg] = py("while True: pass", 0.2);
    EXPECT_TRUE(err);
    EXPECT_NE(text(msg).find("TimeoutError"), std::string::npos) << msg;

    auto [err2, r] = py("sum(range(100000))");
    ASSERT_FALSE(err2) << r;
    EXPECT_EQ(r["result"], 4999950000LL);
}

TEST_F(McpPython, SharesHandlesWithLuaAndTheJsonTools) {
    rosetta::mcp::enable_lua(server);
    tool("create_object", {{"class", "Person"}, {"args", {"Bo", 5}}, {"name", "bo"}});
    ASSERT_FALSE(py(R"(obj("bo").colour = "Blue")").first);
    EXPECT_EQ(tool("run_script", {{"code", "return obj('bo').colour"}}).second["result"], "Blue");
}

#endif
