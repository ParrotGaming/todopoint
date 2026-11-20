add_executable(TodoFileTest
  TodoFileTest.cpp
  ${REPO_ROOT}/src/util/TodoFile.cpp
)

target_include_directories(TodoFileTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(TodoFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TodoFileTest)
