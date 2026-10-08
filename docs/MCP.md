# MCP backend — letting an LLM agent drive a C++ library

**Status:** shipped: stdio transport, generic tools, the `run_script` (Lua)
and `run_python` scripting tools, an HTTP transport hosted inside a running Qt
application, and an in-app Claude chat panel in the Qt viewer of
`examples/dynamic`. The open questions and next steps are listed at
the end.

## What it is

The `mcp` target generates a **Model Context Protocol server**: a small
executable that an MCP client (Claude Code, Claude Desktop, any agent framework
that speaks MCP) launches and talks to over stdin/stdout. The bound classes show
up as **tools**, so the agent can create C++ objects, read and set their fields,
and call their methods, reacting to each result:

> *"Load the bunny, relax it until the worst triangle is above 0.1, and tell me
> how many passes that took."*

```
call_method   {target: "Mesh", method: "bunny", store_as: "bunny"}   → {$ref: "bunny"}
call_method   {target: "Relaxer", method: "minQuality", args: ["bunny"]} → 0.014
create_object {class: "Relaxer", name: "r"}                          → fields, defaults
set_fields    {object: "r", values: {iterations: 50, step: 0.6}}
call_method   {target: "r", method: "run", args: [{"$ref": "bunny"}]}
inspect_object{object: "r"}                                          → worstQuality: 0.0996
...
```

The user's headers don't change. The agent reads the API documentation that
rosetta already carries (`doc{}`, `///` comments, parameter names) and stays
within the constraints rosetta already enforces (`range{}`, `readonly`).

```json
"targets": [ { "lang": "mcp", "name": "scene_mcp" } ]
```

```sh
cmake -S bindings/mcp -B bindings/mcp/build && cmake --build bindings/mcp/build
claude mcp add scene -- "$PWD/bindings/mcp/build/scene_mcp"
```

## Architecture: an MCP front end for the dynamic object model

The `mcp` backend adds no new marshalling layer. It sits on top of the
[dynamic object model](../include/rosetta/dynamic.h):

```
manifest.json ─(rosetta_gen + generator)→ bindings/mcp/
                                            auto_dynamic.{h,cpp}   ← same tables as the `dynamic` target
                                            mcp_server.cpp         ← register_all(); Server{}.serve_stdio();
                                            CMakeLists.txt         ← stock C++20 + nlohmann/json

process:  MCP client ⇄ stdio JSON-RPC ⇄ rosetta::mcp::Server ⇄ rosetta::dyn::registry() ⇄ your classes
```

| Layer | File | Per-library code? |
|---|---|---|
| Metadata + thunks | generated `auto_dynamic.cpp` (shared with the `dynamic` backend) | yes: data only |
| Protocol, tools, handle table, JSON ⇄ `Any` | [`runtime/mcp.h`](../include/rosetta/runtime/mcp.h) | **none** |
| Entry point | generated `mcp_server.cpp` (about 10 lines) | name only |

Building on `dyn` means the MCP server gets the following without writing any
of it:

- **Overloads.** `resolve()` scores the whole overload set at call time. The
  name-keyed backends (REST, node, …) keep only the first overload.
- **Constraint enforcement.** `Object::set` rejects out-of-range values and
  writes to read-only fields, with a message the agent can act on
  (`opacity = 7 is outside [0, 1]`).
- **Lifetime.** A `T&` return comes back as a handle that *pins* its parent
  (`ObjectRef::owner`). If the agent releases the mesh, a handle to its
  `origin` stays valid.
- **Stock-compiler targets.** The metadata is emitted as aggregate
  initializers, so the server builds without clang-p2996, like every other
  expanded backend.

## Design decisions

### 1. A fixed, small tool set, not one tool per method

| Tool | Purpose |
|---|---|
| `list_classes` | Classes (with static factories), enums, free functions. The agent starts here. |
| `describe_class` | Constructors, fields (type, doc, range, choices, enum values, read-only), methods (signature, doc, static/const, callable or not and why). Inherited members are included. |
| `create_object` | Best-matching constructor; returns the handle plus the new object's fields. |
| `call_method` | Instance method on a handle, or static method when `target` is a class name. `store_as` names a returned object. |
| `inspect_object` | All readable fields at once. |
| `set_fields` | Several assignments in one call; each is validated. |
| `list_objects` / `release_object` | Handle-table housekeeping. |
| `call_function` | Free functions. Listed only when the module binds some. |
| `run_script` | A Lua script run next to the live objects. Present when built with Lua 5.3+ (the default when CMake finds it). See *Scripting tools*. |
| `run_python` | The same with Python. Opt-in (`-DROSETTA_MCP_PYTHON=ON`) because it is not sandboxed. |

Generating one tool per method (`Mesh_subdivide`, `Relaxer_run`, …) would look
more direct, but it scales badly. Agents choose tools less reliably as the list
grows, every tool's schema costs context on every turn, and a 500-class library
would produce thousands of tools. With the generic set, discovery is
`describe_class`, and it is paid only for the classes the agent actually uses.
The tool list never changes when the library grows.

Promoting a few chosen methods to dedicated tools could come later as a manifest
option (see *Next steps*). It would be an addition on top of the generic set,
not a replacement.

### 2. Handles are the state

MCP tool calls are stateless; the server process is not. Live objects sit in a
handle table (`std::map<std::string, dyn::Object>`):

- Names are `<Class>#<n>` by default. The agent can choose one (`name` on
  `create_object`, `store_as` on `call_method`), which makes transcripts easier
  to read: `bunny`, not `Mesh#4`.
- A handle is passed as `{"$ref": "bunny"}` anywhere. Where the parameter type
  is known to be a bound class, the bare string `"bunny"` also works.
- Reaching the same sub-object twice (calling a `T&` getter again) reuses its
  handle, so `inspect_object` does not create a new handle on every call.
- Each handle reports its ownership: `owned` (the handle keeps the object
  alive), `pinned` (it keeps its parent alive), or `borrowed` (someone else owns
  it).

Objects live as long as the server process does. For a stdio server that is the
client session, which is the lifetime an agent expects.

### 3. JSON ⇄ `Any`, type-directed where possible

JSON has booleans, numbers, strings and arrays. C++ also has enums, bound
classes, and the difference between `int` and `double`. Arguments are converted
against the parameter type wherever that type can be determined:

- **Keyword form** (`"args": {"m": "bunny"}`): the overload whose parameter names
  are exactly the given keys is chosen, then each value is converted against its
  parameter. This works because parameter names are reflected now.
- **Positional form** (`"args": ["bunny"]`): the candidates are narrowed to the
  given arity. At each position, if all remaining candidates agree on the
  parameter type, the value is converted against it. Otherwise the value keeps
  its plain JSON type and `resolve()` chooses the overload.

The type-directed conversions are enumerator name → enum, bare string → handle,
number → `double` for a floating-point parameter, and arrays converted element by
element. Anything else keeps its JSON type, and a mismatch produces the core's
overload error, which names every candidate and why it was rejected.

Enumerators are resolved by name in the host layer, not in `dyn::match()`, for
the reason `examples/dynamic/interp.h` gives: if the core treated a string as
convertible to an enum, `f(std::string)` and `f(Shading)` would tie.

### 4. Bulk data is summarized

A vector longer than `Options::max_list_items` (default 64) comes back as

```json
{"$summary": true, "length": 107841, "head": [...8], "min": -0.5, "max": 0.5, "mean": -0.03}
```

An agent needs to know that a mesh has 35 947 vertices and roughly where they
are, not to read 107 841 doubles into its context. The statistics are included
when every element is a number. Reading the full data is a job for the expanded
bindings (Python, …). An export-to-file tool is listed under *Next steps*.

### 5. Errors are tool results, not protocol errors

A failed call (bad overload, range violation, unknown handle, C++ exception)
returns `isError: true` with the explanation as text. The agent reads it and
corrects its next call. JSON-RPC errors are kept for real protocol faults:
malformed JSON (-32700), unknown method (-32601), unknown tool (-32602).

### 6. stdout is reserved for the protocol

The stdio transport breaks if anything else writes to stdout, and a numerical
library can easily `std::cout` some progress output. `serve_stdio()` duplicates
fd 1 for the protocol, then points fd 1 at stderr. The library's output ends up
in the client's log instead of in the middle of a JSON-RPC message. This uses
POSIX `dup`/`dup2`, and `_dup`/`_dup2` on Windows.

### 7. The protocol core is pure

`Server::handle(json) → optional<json>` and `call_tool(name, args)` do no I/O.
`serve()` is a ten-line `getline` loop around them. This is why the test suite
([`tests/mcp.cpp`](../tests/mcp.cpp)) drives the protocol without a pipe, and
why other transports can be added without touching the tools.

## Scripting tools: `run_script` (Lua) and `run_python`

Every tool call is a round trip through the model. "Relax until the worst
triangle passes 0.1" done as individual calls (run, measure, decide, repeat)
takes dozens of round trips, and each one makes the model re-read the
conversation. As a script it is one call:

```lua
local m = obj("bunny")
local r = new("Relaxer")
r.iterations, r.step = 1, 0.7
local passes = 0
while classes.Relaxer.minQuality(m) <= 0.1 do r:run(m); passes = passes + r.lastPasses end
return {passes = passes, worst = classes.Relaxer.minQuality(m)}
```

Both languages see the same small API, which the tool description spells out
for the agent:

| | Lua (`run_script`) | Python (`run_python`) |
|---|---|---|
| fetch a handle | `obj("bunny")` | `obj("bunny")` |
| construct | `new("Relaxer", ...)`, `classes.Relaxer(...)` | `new("Relaxer", ...)`, `classes.Relaxer(...)` |
| field | `o.step`, `o.step = 0.7` | `o.step`, `o.step = 0.7` |
| method | `o:run(m)` | `o.run(m)`, keyword arguments by parameter name |
| static method | `classes.Relaxer.minQuality(m)` | same |
| free function | `call("name", ...)` | same |
| publish a handle | `keep("name", o)` | same |
| result | `return` value | value of the last expression (as in a notebook cell), else `result` |

Both go through the same core as the JSON tools: overloads, `range`,
`readonly` and pinning are enforced by `rosetta::dyn`, and a rejection becomes
a Lua error (catchable with `pcall`) or a Python `RuntimeError`. They share the
handle table, so objects move freely between the JSON tools, Lua and Python.
Unlike the JSON tools, scripts receive vectors whole: the point of running next
to the data is that bulk data never travels. Only the returned value is
summarized. `print()` is captured into `output`.

Both are opt-in extensions registered through `Server::add_tool()`. The core
server has no dependency on either language.

**Lua** ([`runtime/mcp_lua.h`](../include/rosetta/runtime/mcp_lua.h)) is a
direct binding over the Lua C API, about 500 lines, with no sol2 and no second
generation stage. It is sandboxed: each call gets a fresh state with base,
table, string, math, utf8 and coroutine only (no `io`, `os`, `require`, `load`,
`dofile`). A wall-clock limit is checked every 10k VM instructions, and a cap
on the Lua heap (512 MB by default) fails allocations cleanly. One rule governs
the binding: Lua reports errors with `longjmp`, which skips C++ destructors.
Every C function therefore runs its body inside `try`, and calls `lua_error()`
only after that frame and its C++ locals are gone.

**Python** ([`runtime/mcp_python.h`](../include/rosetta/runtime/mcp_python.h))
embeds CPython through pybind11. It brings numpy and the rest of the
environment, and **it is not sandboxed**: CPython cannot be meaningfully
restricted from inside. The tool description says so, and every build has it
off by default. The interpreter starts on first use and is never finalized.
Each call gets fresh globals. The timeout raises `TimeoutError` in the script
via `Py_AddPendingCall`. On CPython 3.12 a pending call scheduled from a thread
that never holds the GIL does not wake the running loop, so the watchdog takes
the GIL once to force a switch; it releases the GIL while being joined. The
header also hides Qt's `slots` macro while Python's headers are read, so it can
be included after Qt.

Neither language can interrupt a long call into C++, by design: that is the
library's own code.

## In-app chat: the Qt viewer's Claude panel

The panel has two backends, chosen from a selector next to the input:

- **Claude Code (your login).** This is what the VS Code extension does. For
  each message the panel runs the Claude Code CLI (`claude -p` with
  `--output-format stream-json`), with `ANTHROPIC_API_KEY` removed from its
  environment, so your Claude Code login and subscription are used and no API
  credits are needed. The CLI is given exactly one MCP server, the viewer's own
  HTTP host (`--mcp-config` + `--strict-mcp-config`), so its tool calls come
  back into the running viewer. It gets **no built-in tools** (`--tools ""`:
  no shell, no file edits) and may use the `scene-viewer` tools without
  prompting (`--allowedTools mcp__scene-viewer`, `--permission-mode dontAsk`).
  Follow-up messages continue the session with `--resume`, and **New chat**
  starts a fresh one. The stream (`system/init`, `assistant`, `user`
  tool results, `result`) feeds the same transcript. This is the default when
  `claude` is on `PATH`; set `ROSETTA_CLAUDE_CLI` to point elsewhere.
- **Anthropic API (API key).** The panel calls the Messages API itself, as
  described below.


`examples/dynamic`'s Qt viewer has a **Claude** tab next to its console
([`qt/claudepanel.h`](../examples/dynamic/qt/claudepanel.h)). No MCP transport
is involved. The panel holds an in-process `rosetta::mcp::Server`, offers its
tools to the Claude Messages API, and runs each tool call on the GUI thread
against the live scene. The 3D view and the property panel refresh after every
call, so you watch the bunny change while Claude works.

- **One set of objects.** Before each tool call the server's handle table is
  rebuilt from the console interpreter's variables, so Claude sees `bunny`,
  `cube`, …. After the call the variables are rebuilt from the table, so
  whatever Claude creates or `keep()`s shows up in the object list and is drawn.
  Both sides hold `dyn::Object` handles that share ownership.
- **The API call.** Raw HTTPS through `QNetworkAccessManager`, since there is no
  official C++ SDK. It is a manual tool loop: the assistant content is echoed
  back verbatim (thinking blocks included), and all `tool_result`s go back in
  one user message. Defaults: `claude-opus-5`, adaptive thinking, automatic
  prompt caching, and `fallbacks: "default"` (beta
  `server-side-fallback-2026-07-01`), so a turn declined by a safety classifier
  is re-run on Anthropic's recommended fallback model rather than returned as a
  refusal. A turn truncated at `max_tokens` runs none of its tool calls. There
  is a 40-round cap per message, with Stop and Retry buttons.
- **Configuration.** `ANTHROPIC_API_KEY` (or `ANTHROPIC_AUTH_TOKEN`),
  optionally `ANTHROPIC_BASE_URL` and `ROSETTA_CLAUDE_MODEL`.
- **Smoke test without credits.** `viewer --claude "<prompt>" --shot out.png`
  sends one message at startup. Pointed at a local mock of `/v1/messages`
  through `ANTHROPIC_BASE_URL`, it exercises the whole loop offline. This is
  how both scripting tools were checked end to end in the viewer.

## Hosting the server inside a running application (HTTP)

The stdio server is a process the MCP client launches, so it owns its own
objects, and nothing it does shows up in an application that is already
running. [`runtime/mcp_qt_http.h`](../include/rosetta/runtime/mcp_qt_http.h)
reverses that: a Qt application listens on `127.0.0.1` and MCP clients
connect to it, so the agent works on the objects the application is showing.
The Qt viewer does this at startup:

```sh
./run.sh viewer          # console prints: MCP server for this scene: http://127.0.0.1:8770/mcp
claude mcp add --transport http scene-viewer http://127.0.0.1:8770/mcp
```

Chat in Claude Code, and the viewer's 3D view, object list and property panel
update after each call, while the console logs every outside call
(`[mcp] set_fields {...}`). This path runs on a Claude Code subscription; the
in-app panel needs API credits.

- **Transport.** It is the Streamable HTTP transport in its simplest legal
  form. Each JSON-RPC message (or batch) is POSTed to `/mcp` and answered with
  one `application/json` body; notifications get `202 Accepted`. The server
  never initiates messages, so the optional GET event stream gets
  `405 Method Not Allowed`, which the transport permits. Keep-alive is
  supported.
- **Thread.** It is built on `QTcpServer`, so requests are handled on the GUI
  thread, between two frames, and need no locking against the code that draws
  the same objects. The viewer wraps `Server::handle` to sync the scene around
  each `tools/call` (`qt/scenesync.h`, shared with the chat panel).
- **Security.** It binds to loopback only and refuses a request whose `Origin`
  is not localhost, which closes off DNS rebinding from a web page. There is
  no authentication: any local process can connect, just as any local process
  can run the stdio binary.
- **Port.** `ROSETTA_MCP_PORT` (default 8770; `0` disables it). A port already
  in use is reported in the console and the viewer runs without the host.

## What is checked

- `tests/mcp_scripting.cpp`: 11 cases. Lua: driving objects and publishing
  handles, enums by name, core rejections as Lua errors (with traceback, and
  `pcall`), the sandbox, the time and memory limits, and whole vectors in versus
  summaries out. Python: last-expression results and keyword arguments,
  rejections as exceptions, a timeout that does not leak into the next run, and
  handles shared with Lua and the JSON tools. Built when Lua is found; the
  Python half when Python 3.10+ and pybind11 are.

- `tests/mcp.cpp`: 10 cases over hand-built metadata, stock C++20. Covers
  initialize/version negotiation, notifications, the tool list,
  `describe_class` content, create/inspect/set (range, read-only, enum by name),
  named arguments selecting a constructor, overloads, a static factory with
  `store_as`, object arguments as a bare handle and as `$ref`, vector summaries,
  release, and the newline-delimited transport including a parse error.
- `examples/dynamic`: the `scene` library generated with the new `mcp` target,
  built with stock Apple clang, and driven through a scripted stdio session.
  The session loaded the bunny, rejected `opacity: 7` and a write to the
  read-only `id`, ran the Relaxer (mean quality 0.834 → 0.919), resolved the
  `at` overloads, summarized `positions()` (107 841 doubles), returned
  `originRef()` as a pinned handle that outlived `release_object("bunny")`, and
  reported `onProgress(std::function)` as not callable along with the reason.

## Limits, stated plainly

- **The agent orchestrates; it does not compute.** Each tool call costs a model
  round trip. Loops belong in `run_script` / `run_python`, and heavy numerical
  work belongs in C++ methods.
- **Positional enum names with ambiguous overloads.** When overloads disagree on
  a parameter's type, an enumerator name stays a string and `resolve()` will not
  match it to an enum parameter. Use the integer value or named arguments.
- **No callbacks.** `std::function` parameters are listed with `callable: false`
  and the reason, as in every dynamic consumer.
- **No persistence.** Handles disappear when the server process exits; the Qt
  host shares the application's lifetime.
- **No authorization model.** Anything bound can be called. Bind only what an
  agent should be able to reach. A `destructive` annotation is proposed below.

## Next steps (in rough order of value)

1. **Resources.** Expose live objects as MCP resources (`rosetta://object/bunny`)
   and the class documentation as `rosetta://class/scene::Mesh`, so a client can
   attach them to context without spending tool calls.
2. **An export tool for bulk data.** Write a method's result to a file (`.json`,
   `.csv`, `.npy`) and return the path, as the complement to summaries.
3. **Manifest options on the target:** `instructions` (what the library is
   *for*; this already exists as `Options::instructions`), `max_list_items`, and
   `expose_tools: ["Relaxer.run", ...]` to promote chosen methods to dedicated
   tools with full JSON Schemas built from `range` / `combobox`.
4. **A `destructive` / `confirm` annotation** mapped to MCP tool annotations
   (`destructiveHint`), so clients can ask the user before running it.
