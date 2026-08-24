set(TEST_SOURCES
  test_config.cpp
  test_session_store.cpp
  test_tools.cpp
  test_file_ops.cpp
  test_process.cpp
  test_provider.cpp
)

add_executable(karna_tests ${TEST_SOURCES})

target_include_directories(karna_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(karna_tests PRIVATE
  karna_lib
  Catch2::Catch2WithMain
)

add_executable(karna_gtests test_e2e.cpp)
target_include_directories(karna_gtests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(karna_gtests PRIVATE
  karna_lib
  gtest_main
)
