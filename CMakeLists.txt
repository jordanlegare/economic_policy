cmake_minimum_required(VERSION 3.16)
project(CanadaPolicyStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(policy_engine src/policy_engine.cpp)
target_include_directories(policy_engine PUBLIC include)
target_compile_options(policy_engine PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cad-policy-studio src/main.cpp)
target_link_libraries(cad-policy-studio PRIVATE policy_engine pthread)
target_compile_options(cad-policy-studio PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(policy_tests tests/policy_engine_test.cpp)
target_link_libraries(policy_tests PRIVATE policy_engine)
add_test(NAME policy_engine COMMAND policy_tests)
