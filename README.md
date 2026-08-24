<p align="center">
  <img src="assets/Karna.jpg" alt="Karna archer illustration" width="520">
</p>

<h1 align="center">Karna</h1>

<p align="center">A focused AI coding assistant for the terminal.</p>

<p align="center">
  <a href="https://github.com/TS47Andres/karna"><img src="https://img.shields.io/github/stars/TS47Andres/karna?style=flat-square&color=111111" alt="GitHub stars"></a>
  <a href="https://github.com/TS47Andres/karna/commits/master"><img src="https://img.shields.io/github/last-commit/TS47Andres/karna?style=flat-square&color=111111" alt="Last commit"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-111111?style=flat-square" alt="C++20">
  <img src="https://img.shields.io/badge/CMake-3.22%2B-111111?style=flat-square" alt="CMake 3.22 or newer">
  <img src="https://img.shields.io/badge/UI-FTXUI-111111?style=flat-square" alt="FTXUI">
</p>

<p align="center">
  <code>c++</code> <code>cmake</code> <code>terminal-ui</code>
  <code>coding-agent</code> <code>openrouter</code> <code>exa</code>
</p>

Karna is an interactive terminal coding assistant built with C++20 and FTXUI. It streams model responses, works with project files and shell commands, keeps durable conversation sessions, and provides a compact interface for inspecting tool activity.

## Highlights

- Streaming chat responses in a keyboard-first terminal interface.
- Persistent sessions stored locally in .karna/sessions.
- Session switching, creation, deletion, and automatic titles.
- History restoration that preserves tool cards, diffs, Bash/search panels, and sub-agent transcripts.
- Built-in tools for reading, writing, editing, searching, and inspecting a project.
- OpenRouter model support with configurable model selection.
- Exa-powered web search.
- Smooth scrolling, tool focus, expandable output, and direct navigation to the latest chat content.

## Quick start

### Requirements

- CMake 3.22 or newer.
- A C++20-compatible compiler.
- libcurl and its development files.
- Internet access during the first configure because dependencies are fetched with CMake FetchContent.

### Build

<pre><code>git clone https://github.com/TS47Andres/karna.git
Set-Location karna

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release</code></pre>

For the existing MinGW/Ninja setup:

<pre><code>cmake -S . -B build-msys -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-msys --config Release</code></pre>

### Run

<pre><code>.\build-msys\karna.exe chat</code></pre>

Running the executable without a subcommand also starts chat mode.

## Configure providers

Configure keys inside the chat interface:

<pre><code>/connect &lt;openrouter-api-key&gt;
/connect-exa &lt;exa-api-key&gt;</code></pre>

Or initialize them from environment variables:

<pre><code>$env:OPENROUTER_API_KEY = "your-openrouter-key"
$env:EXA_API_KEY = "your-exa-key"
.\build-msys\karna.exe init</code></pre>

View the saved configuration:

<pre><code>.\build-msys\karna.exe config</code></pre>

Karna stores configuration, the active session ID, and session files in a project-local .karna directory. Add it to .gitignore:

<pre><code>.karna/</code></pre>

## CLI commands

| Command | Description |
| --- | --- |
| init | Initialize configuration from environment variables |
| chat | Start an interactive chat session |
| config | Display configuration and masked API keys |

## Chat commands

| Command | Description |
| --- | --- |
| /help | Show available commands and tools |
| /clear | Clear the current conversation |
| /model &lt;name&gt; | Change the active model |
| /cost | Show estimated cost for the current session |
| /export [path] | Export the conversation to Markdown |
| /session | Show model, usage, and message information |
| /sessions | List saved and running sessions |
| /sessions &lt;id&gt; | Switch to a session by ID |
| /sessions next | Switch to the next session |
| /sessions prev | Switch to the previous session |
| /sessions &lt;number&gt; | Switch using the session list number |
| /new | Create and switch to a new session |
| /delete | Stop work and delete the current session |
| /connect &lt;key&gt; | Save the OpenRouter API key |
| /connect-exa &lt;key&gt; | Save the Exa API key |

## Built-in tools

| Tool | Purpose |
| --- | --- |
| read | Read a file with optional offset and line limit |
| write | Create or overwrite a file |
| edit | Apply an exact search-and-replace edit |
| bash | Execute a shell command and stream its output |
| glob | Find files recursively using a glob pattern |
| grep | Search file contents with a regular expression |
| search | Search the web using Exa |
| sub_agent | Delegate an independent read-only or read/write task |

On Windows, bash uses native PowerShell syntax. On Unix-like systems, it uses the platform shell.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| Enter | Send a message or open a focused tool |
| Up / Down | Scroll the chat |
| Ctrl+Up / Ctrl+Down | Navigate input history |
| Ctrl+T | Focus the next tool result |
| Esc twice | Abort the active request |

## Session storage

Each project gets an independent .karna directory:

<pre><code>.karna/
|-- config.toml
|-- active-session
+-- sessions/
    +-- session-*.json</code></pre>

Session files contain conversation messages, usage totals, model information, context estimates, and display metadata required to restore the same tool cards shown during a live chat. New sessions open at the bottom of the conversation. If the previous session contains history, Karna starts a fresh session on launch.

## Architecture

<pre><code>CLI
 |
 v
Controller ---- SessionStore ---- .karna/
 |
 +---- Provider ---- OpenRouter
 |
 +---- ToolRegistry ---- read / write / edit / bash / glob / grep / search / sub_agent
 |
 +---- TUI ---- FTXUI chat, sidebar, input, and status bar</code></pre>

## Project layout

<pre><code>src/
|-- cli/          CLI entry points
|-- commands/     Interactive chat commands
|-- config/       TOML configuration
|-- core/         Sessions, messages, providers, and tool interfaces
|-- io/           File operations
|-- net/          Network setup
|-- process/      Shell process execution
|-- project/      Project and Git context
|-- providers/    Model provider implementations
|-- tools/        Built-in coding tools
+-- tui/          FTXUI interface and widgets</code></pre>

## Security and privacy

- API keys are stored in the project-local .karna/config.toml.
- API-key commands are not added to input history.
- Shell commands run with the permissions of the current user.
- Review tool calls before allowing changes in sensitive repositories.
- Never commit .karna/ or share its contents publicly.

## Troubleshooting

### The API key is missing

Run /connect &lt;key&gt; inside chat or initialize OPENROUTER_API_KEY and EXA_API_KEY.

### A session cannot be saved on Windows

Close any running Karna executable before rebuilding. The executable cannot be relinked while Windows has it open. Session files are written under the current working directory's .karna/sessions directory.

### The model does not respond

Check the OpenRouter key, selected model, network connection, and status line. Press Esc twice to cancel a request that is taking too long.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. Keep changes focused, use Conventional Commit messages, and avoid committing local .karna state.

## License

No license file is currently included. All rights remain with the repository author until a license is added.

## Repository tags

c++ | c++20 | cmake | ftxui | terminal | tui | ai-assistant | coding-agent | developer-tools | openrouter | exa | session-management
