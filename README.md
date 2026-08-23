# Karna

Karna is an AI coding harness for the terminal. It provides an interactive FTXUI chat interface, streaming model responses, filesystem tools, shell execution, web search, and MCP tool serving.

## Features

- Interactive terminal chat with streaming responses.
- Markdown rendering, conversation history, token usage, and model information.
- Built-in coding tools: `read`, `write`, `edit`, `bash`, `glob`, `grep`, and `search`.
- `sub_agent` support for read-only investigation or delegated read/write work.
- Exa-powered web search.
- Compact Bash and search result panels. Five lines are shown by default; press `Ctrl+T` to expand the selected tool result.
- Line-based chat scrolling with smooth animation.
- MCP server mode for exposing the built-in tools over stdio.

## Requirements

- CMake 3.22 or newer.
- A C++20 compiler.
- libcurl and its development files.
- Internet access during the first configure, because dependencies are fetched with CMake `FetchContent`.

## Build

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is written to `build/karna` or `build/Release/karna.exe`, depending on the generator. For the existing MinGW/Ninja setup, the executable is `build-msys/karna.exe`.

## Run

```powershell
.\build-msys\karna.exe chat
```

Running Karna without a subcommand also starts chat mode.

Available CLI subcommands:

| Command | Description |
|---|---|
| `init` | Initialize configuration from environment variables |
| `chat` | Start an interactive chat session |
| `run` | One-shot mode; currently not implemented |
| `config` | Display current configuration and masked API keys |
| `mcp` | Start the stdio MCP server |

## Configure API Keys

The recommended method is directly inside the chat UI:

```text
/connect <openrouter-api-key>
/connect-exa <exa-api-key>
```

`/setup <openrouter-api-key>` is retained as an alias for `/connect`. API-key commands are treated as sensitive input and are not added to command history.

Keys can also be initialized from environment variables:

```powershell
$env:OPENROUTER_API_KEY = "your-openrouter-key"
$env:EXA_API_KEY = "your-exa-key"
.\build-msys\karna.exe init
```

On Windows, configuration is stored at `%APPDATA%\karna\config.toml`. On Unix-like systems, it is stored at `~/.config/karna/config.toml`.

Check the saved configuration with:

```powershell
.\build-msys\karna.exe config
```

Keys are displayed in masked form.

## Slash Commands

Slash commands are entered in the chat input bar.

| Command | Description |
|---|---|
| `/help` | Show available commands and tools |
| `/clear` | Clear the conversation history |
| `/model <name>` | Change the active model |
| `/tokens` | Show session token usage |
| `/cost` | Show estimated session cost |
| `/export [path]` | Export the conversation to Markdown |
| `/session` | Show model, message, and token information |
| `/connect <key>` | Save the OpenRouter API key |
| `/setup <key>` | Alias for `/connect` |
| `/connect-exa <key>` | Save the Exa AI API key |

## Agent Tools

| Tool | Purpose |
|---|---|
| `read` | Read a file, optionally using an offset and line limit |
| `write` | Create or overwrite a file |
| `edit` | Apply an exact search-and-replace edit |
| `bash` | Execute a shell command and stream its output |
| `glob` | Find files recursively using a glob pattern |
| `grep` | Search file contents using a regular expression |
| `search` | Search the web using Exa AI |
| `sub_agent` | Delegate an independent task in read-only or read/write mode |

On Windows, `bash` commands use native PowerShell syntax. On Unix-like systems, they use the platform shell.

## MCP Server

Start Karna as an MCP stdio server with:

```powershell
.\build-msys\karna.exe mcp
```

The server exposes the built-in tools through MCP discovery and tool calls.

## Tests

Configure with tests enabled, build, and run CTest:

```powershell
cmake -S . -B build-tests -DKARNA_BUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests --output-on-failure
```

## Project Layout

```text
src/
├── cli/          CLI entry points
├── commands/     Interactive slash commands
├── config/       TOML configuration loading and saving
├── core/         Sessions, messages, providers, and tool interfaces
├── mcp/          MCP transport and server implementation
├── providers/    Model provider implementations
├── tools/        Built-in agent tools
├── tui/          FTXUI chat interface and widgets
├── process/      Shell process execution
└── project/      Project context and repository information
```

The main runtime flow is:

```text
CLI → Controller → Session + Provider + ToolRegistry + TUI
                         ↓
                    tool execution
                         ↓
                 streamed result to chat
```
