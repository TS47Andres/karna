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

# Patch FTXUI screen cursor handling for redraws:
# 1. Start with a deterministic hidden cursor instead of an uninitialized shape
# 2. Hide the terminal cursor before moving to the frame origin and keep it
#    hidden while the complete frame is written. Reveal it only at the final
#    input position. This prevents origin/end cursor flashes.
set(_ftxui_screen_header "${ftxui_SOURCE_DIR}/include/ftxui/screen/screen.hpp")
if(EXISTS "${_ftxui_screen_header}")
  file(READ "${_ftxui_screen_header}" _screen_header_src)
  string(REPLACE "    Shape shape;" "    Shape shape = Hidden;" _screen_header_src "${_screen_header_src}")
  file(WRITE "${_ftxui_screen_header}" "${_screen_header_src}")
endif()
unset(_ftxui_screen_header)

set(_ftxui_screen_impl "${ftxui_SOURCE_DIR}/src/ftxui/component/screen_interactive.cpp")
if(EXISTS "${_ftxui_screen_impl}")
  file(READ "${_ftxui_screen_impl}" _screen_impl_src)
  string(ASCII 92 _backslash)
  # Hide before ResetCursorPosition()/ResetPosition(). Those operations move
  # the real terminal cursor through the origin and bottom-right of the frame.
  set(_cursor_hide "  std::cout << \"${_backslash}033[?25l\";")
  set(_cursor_hide_before_reset "${_cursor_hide}\n  ResetCursorPosition();\n  std::cout << ResetPosition")
  string(FIND "${_screen_impl_src}" "${_cursor_hide_before_reset}" _cursor_hide_pos)
  if(_cursor_hide_pos EQUAL -1)
    string(REPLACE "  ResetCursorPosition();\n  std::cout << ResetPosition" "${_cursor_hide_before_reset}" _screen_impl_src "${_screen_impl_src}")
  endif()

  # Keep the write itself free of cursor visibility changes. The final
  # set_cursor_position sequence already reveals the cursor at its destination.
  set(_cursor_safe_write "  std::cout << ToString() << set_cursor_position;")
  set(_cursor_bad_write "  std::cout << \"${_backslash}${_backslash}x1B[?25l\" << ToString() << set_cursor_position;")
  string(REPLACE "${_cursor_bad_write}" "${_cursor_safe_write}" _screen_impl_src "${_screen_impl_src}")
  string(REPLACE "  std::cout << \"${_backslash}033[?25l\" << ToString() << set_cursor_position;" "${_cursor_safe_write}" _screen_impl_src "${_screen_impl_src}")
  unset(_backslash)
  unset(_cursor_hide)
  unset(_cursor_hide_before_reset)
  unset(_cursor_hide_pos)
  unset(_cursor_bad_write)
  unset(_cursor_safe_write)
  file(WRITE "${_ftxui_screen_impl}" "${_screen_impl_src}")
endif()
unset(_ftxui_screen_impl)

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
  src/tools/bash.cpp
  src/tools/sub_agent.cpp
  src/tools/search.cpp
  src/net/curl_setup.cpp
  src/tools/glob.cpp
  src/tools/grep.cpp
  src/commands/registry.cpp
  src/commands/help.cpp
  src/commands/clear.cpp
  src/commands/model.cpp
  src/commands/cost.cpp
  src/commands/export_cmd.cpp
  src/commands/session_cmd.cpp
  src/commands/sessions_cmd.cpp
  src/commands/new_cmd.cpp
  src/commands/delete_cmd.cpp
  src/core/session_store.cpp
  src/tui/app.cpp
  src/tui/sidebar.cpp
  src/tui/chat_view.cpp
  src/tui/input_bar.cpp
  src/tui/markdown_renderer.cpp
  src/tui/status_bar.cpp
  src/process/runner.cpp
  src/io/file_ops.cpp
  src/project/context.cpp
  src/token/counter.cpp
  src/cli/init.cpp
  src/cli/chat.cpp
  src/cli/config_cmd.cpp
)

add_library(karna_lib STATIC ${LIB_SOURCES})
target_include_directories(karna_lib PUBLIC src)
target_include_directories(karna_lib PRIVATE ${md4c_SOURCE_DIR}/src)
target_compile_definitions(karna_lib PRIVATE
  KARNA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(karna_lib PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  CLI11::CLI11
  nlohmann_json::nlohmann_json
  tomlplusplus::tomlplusplus
  md4c
  CURL::libcurl
)

if(WIN32)
  target_link_libraries(karna_lib PUBLIC ws2_32 crypt32 bcrypt gdiplus)
endif()

add_executable(karna src/main.cpp)
target_link_libraries(karna PRIVATE karna_lib)

add_custom_command(TARGET karna POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/resources/cacert.pem"
    "$<TARGET_FILE_DIR:karna>/cacert.pem"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:karna>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/Karna.txt"
    "$<TARGET_FILE_DIR:karna>/assets/Karna.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/assets/Karna.jpg"
    "$<TARGET_FILE_DIR:karna>/assets/Karna.jpg"
)
