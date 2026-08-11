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

# A downloaded Windows executable should not require the Visual C++ runtime to
# be installed separately. Keep the CRT selection consistent across the app,
# static library and native tests so MSVC does not mix /MT and /MD objects.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(cad_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(policy_engine src/policy_engine.cpp)
target_include_directories(policy_engine PUBLIC include)
cad_enable_warnings(policy_engine)

add_executable(cad-policy-studio src/main.cpp)
target_link_libraries(cad-policy-studio PRIVATE policy_engine)
cad_enable_warnings(cad-policy-studio)

if(WIN32)
  # The Windows release is deliberately self-contained: all browser assets and
  # the current calibration snapshot are compiled into the executable. Runtime
  # negotiation state remains writable data under the user's local app-data
  # directory and is never embedded back into a later build.
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(CAD_EMBEDDED_FILES
    web/index.html
    web/app.css
    web/app.js
    web/evaluation-controller.js
    web/diplomat.css
    web/diplomat.js
    web/negotiation-model.css
    web/negotiation-model.js
    web/trade-diplomacy.css
    web/trade-diplomacy.js
    web/calibration.css
    web/calibration.js
    web/robust-room.css
    web/robust-room.js
    web/principal-briefing.css
    web/principal-briefing.js
    data/calibration/current.snapshot.csv
  )
  set(CAD_EMBEDDED_DEPENDS)
  foreach(asset IN LISTS CAD_EMBEDDED_FILES)
    list(APPEND CAD_EMBEDDED_DEPENDS "${CMAKE_SOURCE_DIR}/${asset}")
  endforeach()
  set(CAD_EMBEDDED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_assets.hpp")
  add_custom_command(
    OUTPUT "${CAD_EMBEDDED_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/embed_assets.py"
            --root "${CMAKE_SOURCE_DIR}"
            --output "${CAD_EMBEDDED_HEADER}"
            ${CAD_EMBEDDED_FILES}
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/embed_assets.py" ${CAD_EMBEDDED_DEPENDS}
    COMMENT "Embedding browser assets and calibration snapshot"
    VERBATIM
  )
  add_custom_target(cad_embedded_assets DEPENDS "${CAD_EMBEDDED_HEADER}")
  add_dependencies(cad-policy-studio cad_embedded_assets)
  target_include_directories(cad-policy-studio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(cad-policy-studio PRIVATE
    CAD_EMBEDDED_ASSETS=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  if(MSVC)
    target_compile_definitions(cad-policy-studio PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  target_link_libraries(cad-policy-studio PRIVATE ws2_32 shell32)
  set_target_properties(cad-policy-studio PROPERTIES OUTPUT_NAME "Canada-US-Diplomatic-Policy-Studio")

  if(MINGW)
    target_link_options(cad-policy-studio PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
else()
  target_link_libraries(cad-policy-studio PRIVATE pthread)
endif()

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
cad_enable_warnings(policy_tests)
cad_keep_test_assertions(policy_tests)
add_test(NAME policy_engine COMMAND policy_tests)

add_executable(negotiation_support_tests tests/negotiation_support_test.cpp)
target_link_libraries(negotiation_support_tests PRIVATE policy_engine)
cad_enable_warnings(negotiation_support_tests)
cad_keep_test_assertions(negotiation_support_tests)
add_test(NAME negotiation_support COMMAND negotiation_support_tests)

add_executable(win_win_optimality_tests tests/win_win_optimality_test.cpp)
target_link_libraries(win_win_optimality_tests PRIVATE policy_engine)
cad_enable_warnings(win_win_optimality_tests)
cad_keep_test_assertions(win_win_optimality_tests)
add_test(NAME win_win_optimality COMMAND win_win_optimality_tests)

add_executable(calibration_tests tests/calibration_test.cpp)
target_link_libraries(calibration_tests PRIVATE policy_engine)
cad_enable_warnings(calibration_tests)
cad_keep_test_assertions(calibration_tests)
add_test(NAME calibration_integrity COMMAND calibration_tests)
set_tests_properties(calibration_integrity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(robust_recommendation_tests tests/robust_recommendation_test.cpp)
target_link_libraries(robust_recommendation_tests PRIVATE policy_engine)
cad_enable_warnings(robust_recommendation_tests)
cad_keep_test_assertions(robust_recommendation_tests)
add_test(NAME robust_recommendation COMMAND robust_recommendation_tests)
set_tests_properties(robust_recommendation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(negotiation_room_tests tests/negotiation_room_test.cpp)
target_link_libraries(negotiation_room_tests PRIVATE policy_engine)
cad_enable_warnings(negotiation_room_tests)
cad_keep_test_assertions(negotiation_room_tests)
add_test(NAME negotiation_room COMMAND negotiation_room_tests)
set_tests_properties(negotiation_room PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(trade_diplomacy_platform_tests tests/trade_diplomacy_platform_test.cpp)
target_link_libraries(trade_diplomacy_platform_tests PRIVATE policy_engine)
cad_enable_warnings(trade_diplomacy_platform_tests)
cad_keep_test_assertions(trade_diplomacy_platform_tests)
add_test(NAME trade_diplomacy_platform COMMAND trade_diplomacy_platform_tests)

add_executable(release_assertions_tests tests/release_assertions_test.cpp)
cad_enable_warnings(release_assertions_tests)
cad_keep_test_assertions(release_assertions_tests)
add_test(NAME release_assertions_enabled COMMAND release_assertions_tests)
