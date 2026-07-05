set(TEST_SOURCES
  test_config.cpp
  test_tools.cpp
  test_file_ops.cpp
  test_process.cpp
)

add_executable(karna_tests ${TEST_SOURCES})

target_include_directories(karna_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(karna_tests PRIVATE
  karna_lib
  Catch2::Catch2WithMain
)
