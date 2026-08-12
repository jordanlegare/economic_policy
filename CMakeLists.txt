cmake_minimum_required(VERSION 3.16)
project(CanadaPolicyStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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

add_library(policy_engine
  src/policy_engine.cpp
  src/trade_network.cpp
  src/decision_robustness.cpp
  src/welfare_sensitivity.cpp
  src/model_evidence.cpp)
target_include_directories(policy_engine PUBLIC include)
cad_enable_warnings(policy_engine)

add_executable(cad-policy-studio src/main.cpp)
target_link_libraries(cad-policy-studio PRIVATE policy_engine)
cad_enable_warnings(cad-policy-studio)

if(WIN32)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(CAD_EMBEDDED_FILES
    web/index.html
    web/app.css
    web/app.js
    web/evaluation-controller.js
    web/trade-incidence.js
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
    data/calibration/structural_parameter_registry.csv
    data/calibration/empirical_structural_evidence.csv
    data/calibration/quarterly_structural_estimates.csv
    data/calibration/quarterly_residual_covariance.csv
    data/calibration/state_measurement_registry.csv
    data/backtests/2015-01-20-oil-shock.csv
    data/backtests/2020-03-03-pandemic-onset.csv
    data/backtests/2022-07-12-inflation-tightening.csv
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
    COMMENT "Embedding browser assets and V2 model-evidence data"
    VERBATIM
  )
  add_custom_target(cad_embedded_assets DEPENDS "${CAD_EMBEDDED_HEADER}")
  add_dependencies(cad-policy-studio cad_embedded_assets)
  target_include_directories(cad-policy-studio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(cad-policy-studio PRIVATE CAD_EMBEDDED_ASSETS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
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

function(cad_keep_test_assertions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

function(cad_add_test target source test_name)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE policy_engine)
  cad_enable_warnings(${target})
  cad_keep_test_assertions(${target})
  add_test(NAME ${test_name} COMMAND ${target})
endfunction()

cad_add_test(policy_tests tests/policy_engine_test.cpp policy_engine)
cad_add_test(user_anchor_selection_tests tests/user_anchor_selection_test.cpp user_anchor_selection)
cad_add_test(negotiation_support_tests tests/negotiation_support_test.cpp negotiation_support)
cad_add_test(win_win_optimality_tests tests/win_win_optimality_test.cpp win_win_optimality)
cad_add_test(calibration_tests tests/calibration_test.cpp calibration_integrity)
set_tests_properties(calibration_integrity PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(robust_recommendation_tests tests/robust_recommendation_test.cpp robust_recommendation)
set_tests_properties(robust_recommendation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(negotiation_room_tests tests/negotiation_room_test.cpp negotiation_room)
set_tests_properties(negotiation_room PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(trade_diplomacy_platform_tests tests/trade_diplomacy_platform_test.cpp trade_diplomacy_platform)
cad_add_test(trade_network_tests tests/trade_network_test.cpp trade_network)
cad_add_test(trade_episode_validation_tests tests/trade_episode_validation_test.cpp trade_episode_validation)
set_tests_properties(trade_episode_validation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(final_realism_tests tests/final_realism_test.cpp final_realism)
cad_add_test(robustness_sampler_tests tests/robustness_test.cpp robustness_sampler)
cad_add_test(robustness_integration_tests tests/robustness_integration_test.cpp robustness_integration)
cad_add_test(backtest_tests tests/backtest_test.cpp historical_backtest)
set_tests_properties(historical_backtest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(welfare_sensitivity_tests tests/welfare_sensitivity_test.cpp welfare_sensitivity)
cad_add_test(model_evidence_tests tests/model_evidence_test.cpp model_evidence)
set_tests_properties(model_evidence PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(state_measurement_tests tests/state_measurement_test.cpp state_measurement_contract)
set_tests_properties(state_measurement_contract PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(empirical_calibration_tests tests/empirical_calibration_test.cpp empirical_structural_calibration)
set_tests_properties(empirical_structural_calibration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
cad_add_test(quarterly_empirical_calibration_tests tests/quarterly_empirical_calibration_test.cpp quarterly_empirical_calibration)
set_tests_properties(quarterly_empirical_calibration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(release_assertions_tests tests/release_assertions_test.cpp)
cad_enable_warnings(release_assertions_tests)
cad_keep_test_assertions(release_assertions_tests)
add_test(NAME release_assertions_enabled COMMAND release_assertions_tests)
