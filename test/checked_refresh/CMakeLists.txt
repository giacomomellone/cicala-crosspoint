set(DISPLAY_SOURCE ${REPO_ROOT}/freeink-sdk/libs/display/FreeInkDisplay)
set(DISPLAY_COPY ${CMAKE_CURRENT_BINARY_DIR}/display)
file(REMOVE_RECURSE ${DISPLAY_COPY})
file(COPY ${DISPLAY_SOURCE}/ DESTINATION ${DISPLAY_COPY})
execute_process(COMMAND git apply --check ${REPO_ROOT}/patches/freeink-checked-refresh.patch
  WORKING_DIRECTORY ${DISPLAY_COPY} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git apply ${REPO_ROOT}/patches/freeink-checked-refresh.patch
  WORKING_DIRECTORY ${DISPLAY_COPY} COMMAND_ERROR_IS_FATAL ANY)
add_executable(CheckedRefreshTest CheckedRefreshTest.cpp ${DISPLAY_COPY}/src/bus/EpdBus.cpp)
target_include_directories(CheckedRefreshTest PRIVATE stubs ${DISPLAY_COPY}/src/bus)
target_link_libraries(CheckedRefreshTest PRIVATE GTest::gtest_main)
gtest_discover_tests(CheckedRefreshTest)
