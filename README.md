# Karna

AI coding harness for the terminal.

## Build Status

TODO

## Library Decisions

| Category | Choice | Rationale |
|---|---|---|
| TUI Framework | [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | Modern, declarative DOM-like API, cross-platform, mouse support |
| HTTP Client | [libcurl](https://curl.se/libcurl/) | Battle-tested SSE streaming, HTTP/2, TLS, proxies -- ships on every platform |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | Header-only, intuitive API, JSON Schema support for LLM response parsing and config |
| Event Loop | [libuv](https://github.com/libuv/libuv) | Node.js's event loop -- drives TUI rendering + SSE streaming + child process I/O concurrently |
| Process Spawning | libuv's built-in Process API | Already have libuv; handles pipes, exit codes, signals cross-platform |
| Markdown Parsing | [md4c](https://github.com/mity/md4c) | Small (~2k LOC), CommonMark + GFM extensions, event-stream API maps naturally to TUI rendering |
| CLI Argument Parsing | [CLI11](https://github.com/CLIUtils/CLI11) | Header-only, subcommands, auto-help generation |
| Logging | [spdlog](https://github.com/gabime/spdlog) | Header-only, extremely fast, zero-cost when compiled out |
| Configuration | [toml++](https://github.com/marzer/tomlplusplus) | Header-only, TOML is the most human-editable config format |
| Diff/Edit Application | [diff-match-patch](https://github.com/leongold/diff-match-patch) | Precise text-level diff for applying AI-suggested edits |

## Build System

TODO

## Six Core Interface Classes

### 1. Provider
Abstract LLM backend. One implementation per model family.
```
send(history) -> stream<Delta>
count_tokens(text) -> int
```

### 2. Tool
Functions the LLM can invoke autonomously via tool-use. Registered in a global registry.
```
name() -> string
description() -> string
parameters() -> json_schema
execute(json_params) -> json_result
```
Implementations: Read, Write, Edit, Run, Grep, Glob, Search.

### 3. Skill
A **type of Tool** (inherits Tool interface). Higher-level, user-facing workflows that compose multiple tools and prompts.
```
Same Tool interface, but semantically represents a task like "explain this code" or "fix this bug."
```
Implementations: Explain, FixBug, AddFeature.

### 4. Command (Slash Commands)
TUI-internal commands the user types in the chat input bar, prefixed with `/`. These are **not** CLI flags. They modify session state or display info.
```
name() -> string           // e.g. "help", "clear", "model"
description() -> string
execute(args, context) -> void
```
Registered commands: `/help`, `/clear`, `/model`, `/tokens`, `/skills`, `/cost`, `/export`, `/session`.

### 5. Session
Owns conversation history, manages context window, holds the active Provider and ToolRegistry binding.
```
add_message(msg)
truncate_to_limit()
get_history() -> vector<Message>
bind_provider(Provider*)
```

### 6. MCP (Model Context Protocol)
Protocol layer for tool discovery and invocation across process boundaries.
```
class Client { connect(endpoint); list_tools(); call_tool(); }
class Server { expose(tool_registry); listen(transport); }
class Transport { virtual send/receive } -> StdioTransport, SSETransport
```

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                   CLI11 Entry                        │
│  karna init  |  karna chat  |  karna run            │
└──────────────────────┬──────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────┐
│                   Controller                         │
│  Mediator: ties Session, ToolRegistry, CommandReg,  │
│  MCPClient pool, and TUI together                   │
└───┬────────┬────────┬────────┬────────┬─────────────┘
    │        │        │        │        │
┌───▼──┐ ┌──▼───┐ ┌──▼───┐ ┌──▼───┐ ┌──▼──────────┐
│TUI   │ │Session│ │Tool  │ │Command│ │MCP          │
│FTXUI │ │       │ │Reg   │ │Reg   │ │Client/Server │
└──────┘ └──┬────┘ └──┬───┘ └──────┘ └─────────────┘
            │         │
       ┌────▼──┐  ┌───▼──────────┐
       │Provider│  │Tool impls    │
       │(LLM)  │  │Read/Write/.. │
       └────────┘  │Skill impls   │
                   │Explain/...   │
                   └──────────────┘
```

The **event loop (libuv)** is the architectural backbone:
- TUI render loop (FTXUI screen repaints on timer + events)
- SSE streaming from Provider (non-blocking HTTP transfers via libcurl multi-handle on libuv socket)
- Child process I/O (stdout/stderr pipes)
- File watcher notifications

## Project Structure

```
karna/
├── CMakeLists.txt
├── README.md
├── CONTRIBUTING.md
├── src/
│   ├── main.cpp                   # Entry point, init libuv loop, CLI11 dispatch
│   ├── controller.h/cpp           # Mediator: TUI + Session + Tools + MCP
│   │
│   ├── cli/                       # CLI entry commands (CLI11)
│   │   ├── init.h/cpp
│   │   ├── chat.h/cpp
│   │   ├── run.h/cpp
│   │   ├── config_cmd.h/cpp
│   │   └── mcp_cmd.h/cpp
│   │
│   ├── config/
│   │   └── config.h/cpp           # toml++ config read/write
│   │
│   ├── core/                      # Core interfaces & types
│   │   ├── provider.h             # Abstract Provider
│   │   ├── tool.h                 # Abstract Tool (base for tools + skills)
│   │   ├── skill.h                # Inherits Tool, semantic marker
│   │   ├── command.h              # Abstract slash command
│   │   ├── session.h/cpp          # Conversation state, context window
│   │   ├── message.h              # Message / Delta / ToolCall types
│   │   └── mcp.h                  # MCP protocol types & interfaces
│   │
│   ├── providers/                 # LLM backend implementations
│   │   ├── anthropic.h/cpp
│   │   ├── openai.h/cpp
│   │   └── google.h/cpp
│   │
│   ├── tools/                     # Tool implementations
│   │   ├── registry.h/cpp
│   │   ├── read.h/cpp
│   │   ├── write.h/cpp
│   │   ├── edit.h/cpp
│   │   ├── run.h/cpp
│   │   ├── search.h/cpp
│   │   ├── glob.h/cpp
│   │   └── grep.h/cpp
│   │
│   ├── skills/                    # Skill implementations
│   │   ├── registry.h/cpp
│   │   ├── explain.h/cpp
│   │   ├── fix_bug.h/cpp
│   │   └── add_feature.h/cpp
│   │
│   ├── commands/                  # Slash command implementations
│   │   ├── registry.h/cpp
│   │   ├── help.h/cpp
│   │   ├── clear.h/cpp
│   │   ├── model.h/cpp
│   │   ├── tokens.h/cpp
│   │   ├── skills_list.h/cpp
│   │   ├── cost.h/cpp
│   │   ├── export_cmd.h/cpp
│   │   └── session_cmd.h/cpp
│   │
│   ├── mcp/                       # MCP protocol layer
│   │   ├── client.h/cpp
│   │   ├── server.h/cpp
│   │   └── transport.h/cpp
│   │
│   ├── tui/                       # FTXUI components
│   │   ├── app.h/cpp
│   │   ├── chat_view.h/cpp
│   │   ├── input_bar.h/cpp
│   │   └── status_bar.h/cpp
│   │
│   ├── io/                        # File system operations
│   │   ├── file_ops.h/cpp
│   │   └── watcher.h/cpp
│   │
│   ├── process/
│   │   └── runner.h/cpp           # libuv child process runner
│   │
│   ├── project/
│   │   ├── context.h/cpp          # .karna-ignore, git state, file tree
│   │   └── ignore.h/cpp
│   │
│   └── token/
│       └── counter.h/cpp          # Token counting per model
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_config.cpp
│   ├── test_tools.cpp
│   ├── test_file_ops.cpp
│   └── test_process.cpp
│
└── external/                      # Vendored dependencies
    ├── FTXUI/
    ├── CLI11/
    ├── nlohmann/
    ├── tomlplusplus/
    ├── md4c/
    ├── diff-match-patch/
    └── Catch2/
```

## Control Flow: Chat Session

```
1. User runs: karna chat
2. CLI dispatches -> chat.h/cpp -> Controller::run_chat()
3. Controller initializes:
   - TUI app (FTXUI screen)
   - Session (loads config, creates Provider)
   - ToolRegistry (registers all built-in tools)
   - SkillRegistry (registers all skills)
   - CommandRegistry (registers all slash commands)
   - MCP::Client pool (connects to configured external MCP servers)
4. libuv event loop starts
5. User types question in input bar
6. Controller adds user message to Session
7. Controller sends session history to Provider (SSE stream)
8. FTXUI renders tokens as they arrive (streaming)
9. If Provider returns tool_call:
   a. Controller dispatches to ToolRegistry (or SkillRegistry)
   b. Tool/Skill executes, returns result
   c. Controller adds tool_result to Session
   d. Controller calls Provider again with updated history
   e. Repeat from step 8
10. If user types /command:
    a. Controller intercepts, routes to CommandRegistry
    b. Command executes (e.g., /clear resets session, /model switches provider)
11. Loop until user exits (Ctrl+C or /exit)
```

## Key Decisions

- TUI + async event loop + HTTP client + JSON + markdown rendering is the core stack.
- All libraries apart from libcurl are header-only or single-C-file; libcurl is the only traditional build dependency.
- Event loop (libuv) is the architectural backbone: it manages the TUI render loop, SSE streaming from LLM providers, and child process I/O in a single thread.
- Skills are a subclass of Tool (same interface, semantic distinction).
- Commands are `/`-prefixed TUI-internal commands, distinct from CLI entry commands.
- tool_call loop runs inside libuv's event loop (non-blocking, no busy-wait).
