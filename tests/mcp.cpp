// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the MCP server (<rosetta/runtime/mcp.h>).
//
// The server is driven through Server::handle() — one JSON-RPC message in,
// one reply out — so the protocol is tested without a pipe; one test goes
// through serve() on string streams to cover the line transport.
//
// The bound library and its hand-built metadata live in mcp_fixture.h, shared
// with the scripting suite (mcp_scripting.cpp). Stock C++20.

#include "mcp_fixture.h"

TEST_F(Mcp, InitializeNegotiatesAndAdvertisesTools) {
    json r = rpc("initialize", {{"protocolVersion", "2024-11-05"}});
    EXPECT_EQ(r["result"]["protocolVersion"], "2024-11-05");
    EXPECT_TRUE(r["result"]["capabilities"].contains("tools"));
    EXPECT_EQ(r["result"]["serverInfo"]["name"], "demo");

    json u = rpc("initialize", {{"protocolVersion", "1999-01-01"}});
    EXPECT_EQ(u["result"]["protocolVersion"], "2025-06-18");
}

TEST_F(Mcp, NotificationsGetNoReplyAndUnknownMethodsAnError) {
    EXPECT_FALSE(server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
    EXPECT_EQ(rpc("nope")["error"]["code"], -32601);
    EXPECT_EQ(rpc("tools/call", {{"name", "nope"}})["error"]["code"], -32602);
}

TEST_F(Mcp, ToolListIsFixedAndOmitsCallFunctionWithoutFunctions) {
    std::vector<std::string> names;
    for (const json &t : rpc("tools/list")["result"]["tools"]) {
        names.push_back(t["name"]);
        EXPECT_EQ(t["inputSchema"]["type"], "object");
    }
    EXPECT_EQ(names, (std::vector<std::string>{"list_classes", "describe_class", "create_object",
                                               "call_method", "inspect_object", "set_fields",
                                               "list_objects", "release_object"}));
}

TEST_F(Mcp, DescribeClassCarriesTheMetadataAnAgentNeeds) {
    auto [err, d] = tool("describe_class", {{"class", "Person"}});
    ASSERT_FALSE(err) << d;
    EXPECT_EQ(d["constructors"].size(), 2u);
    EXPECT_EQ(d["constructors"][1]["params"][0]["name"], "name");
    bool saw_age = false, saw_colour = false;
    for (const json &f : d["fields"]) {
        if (f["name"] == "age") {
            saw_age = true;
            EXPECT_EQ(f["range"], json::array({0.0, 150.0}));
        }
        if (f["name"] == "colour") {
            saw_colour = true;
            EXPECT_EQ(f["values"], json::array({"Red", "Green", "Blue"}));
        }
        if (f["name"] == "id") {
            EXPECT_TRUE(f["readonly"].get<bool>());
        }
    }
    EXPECT_TRUE(saw_age && saw_colour);
}

TEST_F(Mcp, CreateInspectAndSetFields) {
    auto [err, o] = tool("create_object", {{"class", "Person"}, {"args", {"Ada", 36}}, {"name", "ada"}});
    ASSERT_FALSE(err) << o;
    EXPECT_EQ(o["handle"], "ada");
    EXPECT_EQ(o["fields"]["age"], 36);
    EXPECT_EQ(o["fields"]["colour"], "Red");

    // Enumerator by name; a range violation and a read-only write are both
    // refused with the core's own message, and the valid write still lands.
    auto [serr, msg] = tool("set_fields", {{"object", "ada"},
                                           {"values", {{"colour", "Blue"}, {"age", 400}, {"id", "x"}}}});
    EXPECT_TRUE(serr);
    EXPECT_NE(msg.get<std::string>().find("outside [0, 150]"), std::string::npos) << msg;
    EXPECT_NE(msg.get<std::string>().find("read-only"), std::string::npos) << msg;

    auto [ierr, after] = tool("inspect_object", {{"object", "ada"}});
    ASSERT_FALSE(ierr);
    EXPECT_EQ(after["fields"]["colour"], "Blue");
    EXPECT_EQ(after["fields"]["age"], 36);
}

TEST_F(Mcp, NamedArgumentsPickTheConstructor) {
    auto [err, o] = tool("create_object", {{"class", "Person"}, {"args", {{"age", 7}, {"name", "Bo"}}}});
    ASSERT_FALSE(err) << o;
    EXPECT_EQ(o["fields"]["name"], "Bo");
    EXPECT_EQ(o["handle"].get<std::string>().rfind("Person#", 0), 0u);
}

TEST_F(Mcp, OverloadsStaticFactoriesAndObjectArguments) {
    tool("create_object", {{"class", "Person"}, {"args", {"Ada", 36}}, {"name", "ada"}});

    EXPECT_EQ(tool("call_method", {{"target", "ada"}, {"method", "at"}, {"args", {3}}}).second["result"], 3.0);
    EXPECT_EQ(tool("call_method", {{"target", "ada"}, {"method", "at"}, {"args", {3, 4}}}).second["result"], 12.0);

    auto [ferr, f] = tool("call_method", {{"target", "Person"}, {"method", "make"},
                                          {"args", {"Kid"}}, {"store_as", "kid"}});
    ASSERT_FALSE(ferr) << f;
    EXPECT_EQ(f["result"]["$ref"], "kid");

    // A handle as a bare string (the parameter's type is known) and as $ref.
    EXPECT_EQ(tool("call_method", {{"target", "ada"}, {"method", "olderThan"}, {"args", {"kid"}}})
                  .second["result"], true);
    EXPECT_EQ(tool("call_method", {{"target", "kid"}, {"method", "olderThan"},
                                   {"args", {{{"$ref", "ada"}}}}}).second["result"], false);

    auto [berr, bad] = tool("call_method", {{"target", "ada"}, {"method", "at"}, {"args", {"x"}}});
    EXPECT_TRUE(berr);
    EXPECT_NE(bad.get<std::string>().find("matched no overload"), std::string::npos);
}

TEST_F(Mcp, LargeVectorsAreSummarized) {
    tool("create_object", {{"class", "Person"}, {"name", "p"}});
    auto [err, o] = tool("inspect_object", {{"object", "p"}});
    ASSERT_FALSE(err);
    const json &s = o["fields"]["scores"]; // 3 elements, max_list_items == 2
    EXPECT_TRUE(s["$summary"].get<bool>());
    EXPECT_EQ(s["length"], 3);
    EXPECT_EQ(s["min"], 1.0);
    EXPECT_EQ(s["max"], 3.0);
    EXPECT_EQ(s["mean"], 2.0);
}

TEST_F(Mcp, ReleaseDropsTheHandle) {
    tool("create_object", {{"class", "Person"}, {"name", "tmp"}});
    EXPECT_FALSE(tool("release_object", {{"object", "tmp"}}).first);
    EXPECT_TRUE(tool("inspect_object", {{"object", "tmp"}}).first);
    EXPECT_TRUE(tool("release_object", {{"object", "tmp"}}).first);
}

TEST_F(Mcp, ServeSpeaksNewlineDelimitedJsonRpc) {
    std::istringstream in(
        R"({"jsonrpc":"2.0","id":1,"method":"ping"})"
        "\n\n"
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
        "\nnot json\n");
    std::ostringstream out;
    server.serve(in, out);

    std::istringstream lines(out.str());
    std::string        l1, l2, l3;
    std::getline(lines, l1);
    std::getline(lines, l2);
    EXPECT_FALSE(std::getline(lines, l3)); // the notification got no reply
    EXPECT_EQ(json::parse(l1)["id"], 1);
    EXPECT_EQ(json::parse(l2)["error"]["code"], -32700);
}
