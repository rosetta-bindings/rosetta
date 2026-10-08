// Copyright (c) fmaerten@gmail.com
// License: MIT

// Reflection-free Model Context Protocol (MCP) server for the "mcp" backend.
//
// An LLM agent (Claude Code, Claude Desktop, any MCP client) connects over
// stdio, sees the bound library as a handful of TOOLS, and drives live C++
// objects with them: create a Mesh, read its fields, call relax(), read the
// result, decide what to do next.
//
// The server knows nothing about the bound library. It is written entirely
// against the dynamic object model (<rosetta/dynamic.h>): the `mcp` backend
// emits the same TypeDesc / MetaClass tables as the `dynamic` backend, and this
// header turns registry() into MCP. So:
//
//   * The tool set is FIXED and small (list_classes, describe_class,
//     create_object, call_method, inspect_object, set_fields, list_objects,
//     release_object, and call_function when free functions are bound). It
//     does not grow with the library — an agent picks tools worse when there
//     are hundreds of them, and a 500-class library would produce thousands.
//     The agent discovers the API by calling describe_class instead.
//
//   * Everything the core already enforces stays enforced: range{lo,hi} and
//     readonly in Object::set, overload resolution in resolve(), lifetime
//     pinning in ObjectRef::owner. A rejected call comes back as a tool error
//     with the core's message ("opacity = 7 is outside [0, 1]"), which is
//     exactly what lets an agent correct itself on the next turn.
//
//   * Live objects are kept in a HANDLE TABLE ("Mesh#1"). MCP tool calls are
//     stateless; the handles are the state. A handle is passed back as
//     {"$ref": "Mesh#1"} — or as the bare string "Mesh#1" wherever the
//     parameter's type is known to be a bound class.
//
//   * Bulk data is SUMMARIZED, not shipped. A vector longer than
//     Options::max_list_items comes back as {length, head, min, max, mean}:
//     an agent needs to know a mesh has 13442 vertices, not to read them.
//
// Wire format: JSON-RPC 2.0, one message per line (the MCP stdio transport).
// stdout carries the protocol and nothing else; serve_stdio() re-points file
// descriptor 1 at stderr so a stray std::cout in the bound library cannot
// corrupt the stream.
//
// Depends on nlohmann/json (header-only) and <rosetta/dynamic.h>. Stock C++20 —
// no reflection, no <experimental/meta>.
//
// Declarations only; the bodies live in inline/mcp.hxx.

#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <rosetta/dynamic.h>
#include <string>
#include <string_view>
#include <vector>

namespace rosetta::mcp {

    using json = nlohmann::json;

    /** @brief How the server presents itself, and its bulk-data policy. */
    struct Options {
        std::string name    = "rosetta"; // serverInfo.name
        std::string version = "0.1.0";   // serverInfo.version

        // Appended to the generic usage text in the `initialize` reply — a
        // place to tell the agent what the library is FOR.
        std::string instructions;

        // A vector longer than this is summarized instead of returned whole.
        std::size_t max_list_items = 64;
        // How many leading elements a summary still carries.
        std::size_t summary_head = 8;
    };

    /**
     * @brief The MCP server: protocol handling, tools, and the handle table.
     *
     * handle() and call_tool() are pure functions of (message, server state) —
     * no I/O — so the whole protocol is testable without a pipe. serve() and
     * serve_stdio() are the thin transport loop around them.
     */
    class Server {
    public:
        explicit Server(Options opts = {}, dyn::Registry &reg = dyn::registry());

        /** @brief One JSON-RPC message in, its reply out (nullopt for a notification). */
        std::optional<json> handle(const json &msg);

        /** @brief The `tools/list` payload. */
        json tools() const;

        /** @brief Run one tool; returns an MCP CallToolResult (isError set on failure). */
        json call_tool(std::string_view name, const json &args);

        /** @brief Read newline-delimited JSON-RPC from `in` until EOF, reply on `out`. */
        int serve(std::istream &in, std::ostream &out);

        /** @brief serve() on stdin/stdout, with fd 1 diverted to stderr first. */
        int serve_stdio();

        // ---- the handle table, for an embedding application -------------

        /** @brief Publish an object under `name` (or <Class>#<n> when empty);
         *  returns the handle. An existing handle of that name is replaced. */
        std::string        store(dyn::Object obj, const std::string &name = {});
        void               put(const std::string &name, dyn::Object obj) { store(std::move(obj), name); }
        const dyn::Object *find(std::string_view handle) const;
        bool               release(const std::string &handle) { return objects_.erase(handle) > 0; }

        /** @brief Every live handle — for a host that mirrors them (a viewer's object list). */
        const std::map<std::string, dyn::Object> &objects() const { return objects_; }
        void                                      clear_objects() { objects_.clear(); }

        // ---- extension points ------------------------------------------
        //
        // How an optional tool (run_script, in runtime/mcp_lua.h) plugs in
        // without the core server depending on it.

        /** @brief A tool body: arguments in, structured result out. Throw
         *  (any std::exception) to report a tool error to the agent. */
        using ToolFn = std::function<json(const json &args)>;

        /** @brief Add a tool. `definition` is its tools/list entry
         *  ({name, description, inputSchema}). */
        void add_tool(json definition, ToolFn fn);

        /** @brief One callable's parameter list — a ctor, a method overload or
         *  a free function — so argument conversion is written once for all. */
        struct Signature {
            const dyn::MetaParam *params   = nullptr;
            std::size_t           n_params = 0;
        };
        static std::vector<Signature> ctor_signatures(const dyn::MetaClass &k);
        static std::vector<Signature> method_signatures(const dyn::MetaClass &k,
                                                        std::string_view method);

        /** @brief Convert a loosely-typed value toward a known parameter type:
         *  enumerator name -> enum, handle name -> object, integer -> double,
         *  element-wise for a sequence. Anything else is returned unchanged
         *  for the core's match() / resolve() to judge. */
        dyn::Any coerce(dyn::Any v, const dyn::TypeDesc *want) const;

        /** @brief coerce() each positional argument wherever every candidate
         *  of that arity agrees on the parameter's type. */
        dyn::ArgList coerce_args(std::vector<dyn::Any> raw, const std::vector<Signature> &cands) const;

        /** @brief A dyn value as tool-result JSON: enums by name, objects as
         *  new or existing handles, long vectors summarized. */
        json encode(const dyn::Any &v);

        /** @brief Apply the long-sequence summary policy to already-encoded items. */
        json encode_list(std::vector<json> items) const;

        /** @brief The usage text sent as `instructions` in the initialize
         *  reply — also what an in-process client puts in its system prompt. */
        std::string instructions() const;

        const Options &options() const { return opts_; }
        dyn::Registry &registry() const { return reg_; }

    private:
        // tools — each returns the structured result, throws dyn::Error on failure
        json list_classes() const;
        json describe_class(const json &a) const;
        json create_object(const json &a);
        json call_method(const json &a);
        json inspect_object(const json &a);
        json set_fields(const json &a);
        json list_objects();
        json release_object(const json &a);
        json call_function(const json &a);

        // marshalling
        dyn::Any           from_json(const json &v) const; // untyped; resolves {"$ref"}
        dyn::ArgList       args_for(const json &args, const std::vector<Signature> &cands) const;
        const dyn::Object &object_arg(const json &a, const char *key) const;

        Options                            opts_;
        dyn::Registry                     &reg_;
        std::map<std::string, dyn::Object> objects_;
        std::map<std::string, std::size_t> counters_; // per class, for "Mesh#3"
        std::vector<std::pair<json, ToolFn>> extra_tools_;
    };

} // namespace rosetta::mcp

#include "inline/mcp.hxx"
