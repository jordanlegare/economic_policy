cmake_minimum_required(VERSION 3.16)
project(CanadaPolicyStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This application performs exact Pareto sector searches plus Monte Carlo
# verification. An unspecified single-config build otherwise runs without the
# optimizer flags expected for interactive use and can make the initial browser
# evaluation appear frozen. Respect every explicitly selected build type, but
# make the ordinary `cmake -S . -B build` path production-speed by default.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(policy_engine src/policy_engine.cpp)
target_include_directories(policy_engine PUBLIC include)
target_compile_options(policy_engine PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cad-policy-studio src/main.cpp)
target_link_libraries(cad-policy-studio PRIVATE policy_engine pthread)
target_compile_options(cad-policy-studio PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

# Release builds define NDEBUG, which disables the standard assert() checks used
# throughout the native regression tests. Keep assertions enabled for test
# translation units in every build configuration; otherwise a fast Release
# build can report green tests after compiling away the checks themselves.
function(cad_keep_test_assertions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

add_executable(policy_tests tests/policy_engine_test.cpp)
target_link_libraries(policy_tests PRIVATE policy_engine)
cad_keep_test_assertions(policy_tests)
add_test(NAME policy_engine COMMAND policy_tests)

add_executable(negotiation_support_tests tests/negotiation_support_test.cpp)
target_link_libraries(negotiation_support_tests PRIVATE policy_engine)
target_compile_options(negotiation_support_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(negotiation_support_tests)
add_test(NAME negotiation_support COMMAND negotiation_support_tests)

add_executable(win_win_optimality_tests tests/win_win_optimality_test.cpp)
target_link_libraries(win_win_optimality_tests PRIVATE policy_engine)
target_compile_options(win_win_optimality_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(win_win_optimality_tests)
add_test(NAME win_win_optimality COMMAND win_win_optimality_tests)

add_executable(calibration_tests tests/calibration_test.cpp)
target_link_libraries(calibration_tests PRIVATE policy_engine)
target_compile_options(calibration_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(calibration_tests)
add_test(NAME calibration_integrity COMMAND calibration_tests)
set_tests_properties(calibration_integrity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(robust_recommendation_tests tests/robust_recommendation_test.cpp)
target_link_libraries(robust_recommendation_tests PRIVATE policy_engine)
target_compile_options(robust_recommendation_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(robust_recommendation_tests)
add_test(NAME robust_recommendation COMMAND robust_recommendation_tests)
set_tests_properties(robust_recommendation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(negotiation_room_tests tests/negotiation_room_test.cpp)
target_link_libraries(negotiation_room_tests PRIVATE policy_engine)
target_compile_options(negotiation_room_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(negotiation_room_tests)
add_test(NAME negotiation_room COMMAND negotiation_room_tests)
set_tests_properties(negotiation_room PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(trade_diplomacy_platform_tests tests/trade_diplomacy_platform_test.cpp)
target_link_libraries(trade_diplomacy_platform_tests PRIVATE policy_engine)
target_compile_options(trade_diplomacy_platform_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(trade_diplomacy_platform_tests)
add_test(NAME trade_diplomacy_platform COMMAND trade_diplomacy_platform_tests)

add_executable(release_assertions_tests tests/release_assertions_test.cpp)
target_compile_options(release_assertions_tests PRIVATE -Wall -Wextra -Wpedantic)
cad_keep_test_assertions(release_assertions_tests)
add_test(NAME release_assertions_enabled COMMAND release_assertions_tests)
