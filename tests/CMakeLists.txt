set(TEST_SOURCES
  test_config.cpp
  test_tools.cpp
  test_file_ops.cpp
  test_process.cpp
)

add_executable(karna_tests ${TEST_SOURCES})

target_include_directories(karna_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(karna_tests PRIVATE
  Catch2::Catch2WithMain
  nlohmann_json::nlohmann_json
  tomlplusplus::tomlplusplus
  CURL::libcurl
)

catch_discover_tests(karna_tests)
