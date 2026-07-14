cmake_minimum_required(VERSION 3.22)
project(karna VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

set(CURL_NO_CURL_CMAKE TRUE)
find_package(CURL REQUIRED MODULE)

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# Patch FTXUI input component:
# 1. Use block blinking cursor instead of bar cursor
# 2. Show cursor only on actual focus (click), not on mouse hover
# 3. Always use block blinking cursor (even in placeholder / empty state)
set(_ftxui_input "${ftxui_SOURCE_DIR}/src/ftxui/component/input.cpp")
if(EXISTS "${_ftxui_input}")
  file(READ "${_ftxui_input}" _input_src)
  string(REPLACE "(is_focused || hovered_) ? focusCursorBarBlinking : select" "is_focused ? ftxui::focusCursorBlockBlinking : ftxui::select" _input_src "${_input_src}")
  string(REPLACE "      element |= focus;" "      element = ftxui::focusCursorBlockBlinking(std::move(element));" _input_src "${_input_src}")
  string(REPLACE "    cursor_position() = start;\n    return true;" "    cursor_position() = start;\n    on_change();\n    return true;" _input_src "${_input_src}")
  string(REPLACE "    content->erase(start, end - start);\n    return true;" "    content->erase(start, end - start);\n    on_change();\n    return true;" _input_src "${_input_src}")
  file(WRITE "${_ftxui_input}" "${_input_src}")
endif()
unset(_ftxui_input)

# Patch FTXUI screen component:
# Disable DECMode::kMouseAnyEvent to prevent flooding mouse move events and cursor flickering
set(_ftxui_screen "${ftxui_SOURCE_DIR}/src/ftxui/component/screen_interactive.cpp")
if(EXISTS "${_ftxui_screen}")
  file(READ "${_ftxui_screen}" _screen_src)
  string(REPLACE "enable({DECMode::kMouseAnyEvent});" "// enable({DECMode::kMouseAnyEvent});" _screen_src "${_screen_src}")
  file(WRITE "${_ftxui_screen}" "${_screen_src}")
endif()
unset(_ftxui_screen)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

FetchContent_Declare(
  md4c
  GIT_REPOSITORY https://github.com/mity/md4c.git
  GIT_TAG release-0.5.2
)
FetchContent_MakeAvailable(md4c)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

if(KARNA_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.0
  )
  FetchContent_MakeAvailable(Catch2)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  # Prevent gtest from overriding our compiler settings on Windows
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

set(LIB_SOURCES
  src/controller.cpp
  src/config/config.cpp
  src/core/session.cpp
  src/providers/openrouter.cpp
  src/commands/connect.cpp
  src/tools/registry.cpp
  src/tools/read.cpp
  src/tools/write.cpp
  src/tools/edit.cpp
  src/tools/run.cpp
  src/tools/search.cpp
  src/tools/glob.cpp
  src/tools/grep.cpp
  src/skills/registry.cpp
  src/skills/explain.cpp
  src/skills/fix_bug.cpp
  src/skills/add_feature.cpp
  src/commands/registry.cpp
  src/commands/help.cpp
  src/commands/clear.cpp
  src/commands/model.cpp
  src/commands/tokens.cpp
  src/commands/skills_list.cpp
  src/commands/cost.cpp
  src/commands/export_cmd.cpp
  src/commands/session_cmd.cpp
  src/mcp/client.cpp
  src/mcp/server.cpp
  src/mcp/transport.cpp
  src/tui/app.cpp
  src/tui/sidebar.cpp
  src/tui/chat_view.cpp
  src/tui/input_bar.cpp
  src/tui/markdown_renderer.cpp
  src/tui/status_bar.cpp
  src/process/runner.cpp
  src/io/file_ops.cpp
  src/io/watcher.cpp
  src/project/context.cpp
  src/project/ignore.cpp
  src/token/counter.cpp
  src/cli/init.cpp
  src/cli/chat.cpp
  src/cli/run.cpp
  src/cli/config_cmd.cpp
  src/cli/mcp_cmd.cpp
)

add_library(karna_lib STATIC ${LIB_SOURCES})
target_include_directories(karna_lib PUBLIC src)
target_include_directories(karna_lib PRIVATE ${md4c_SOURCE_DIR}/src)

target_link_libraries(karna_lib PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  CLI11::CLI11
  nlohmann_json::nlohmann_json
  tomlplusplus::tomlplusplus
  md4c
  spdlog::spdlog
  CURL::libcurl
)

if(WIN32)
  target_link_libraries(karna_lib PUBLIC ws2_32 crypt32 bcrypt)
endif()

add_executable(karna src/main.cpp)
target_link_libraries(karna PRIVATE karna_lib)

option(KARNA_BUILD_TESTS "Build tests" OFF)

if(KARNA_BUILD_TESTS)
  add_subdirectory(tests)
endif()
