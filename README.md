# Karna

Karna is an AI coding harness for the terminal. It provides an interactive FTXUI chat interface, streaming model responses, filesystem tools, shell execution, and web search.

## Features

- Interactive terminal chat with streaming responses.
- Markdown rendering, conversation history, token usage, and model information.
- Persistent multi-session chat with live switching between historical and running sessions.
- Interactive `/sessions` picker with automatic updates; `CURRENT` is green and background `RUNNING` sessions are cyan.
- Session names use the first 20 characters of the first user message.
- Built-in coding tools: `read`, `write`, `edit`, `bash`, `glob`, `grep`, and `search`.
- `sub_agent` support for read-only investigation or delegated read/write work.
- Exa-powered web search.
- Compact Bash and search result panels. Five lines are shown by default; press `Ctrl+T` to expand the selected tool result.
- Line-based chat scrolling with smooth animation.

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
| `config` | Display current configuration and masked API keys |

## Configure API Keys

The recommended method is directly inside the chat UI:

```text
/connect <openrouter-api-key>
/connect-exa <exa-api-key>
```

API-key commands are treated as sensitive input and are not added to command history.

Keys can also be initialized from environment variables:

```powershell
$env:OPENROUTER_API_KEY = "your-openrouter-key"
$env:EXA_API_KEY = "your-exa-key"
.\build-msys\karna.exe init
```

Karna stores project-local state in a hidden `.karna` directory wherever it is launched:

```text
.karna/
├── config.toml       API keys and provider settings
├── active-session    the last selected session
└── sessions/         one durable JSON file per chat session
```

This keeps keys, model choices, and conversations together for the current project. Add `.karna/` to your VCS ignore rules if it should remain local.

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
| `/cost` | Show estimated session cost |
| `/export [path]` | Export the conversation to Markdown |
| `/session` | Show model, message, and token information |
| `/sessions` | List historical and currently running sessions |
| `/sessions <id>` | Switch to a session without stopping its work |
| `/sessions next` / `/sessions prev` | Cycle through saved sessions |
| `/sessions <number>` | Switch using the number shown by the session list |
| `/new` | Create and switch to a new session |
| `/delete` | Delete the current session |
| `/connect <key>` | Save the OpenRouter API key |
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

## Project Layout

```text
src/
├── cli/          CLI entry points
├── commands/     Interactive slash commands
├── config/       TOML configuration loading and saving
├── core/         Sessions, messages, providers, and tool interfaces
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
