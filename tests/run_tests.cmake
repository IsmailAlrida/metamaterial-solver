cmake_minimum_required(VERSION 3.25)

foreach(_required BUILD_DIR BACKEND BUILD_TYPE REPORT_FILE)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required.")
  endif()
endforeach()

set(CTEST_COMMAND "${CMAKE_CTEST_COMMAND}")
if(NOT CTEST_COMMAND)
  find_program(CTEST_COMMAND ctest REQUIRED)
endif()
if(NOT EXISTS "${BUILD_DIR}/CTestTestfile.cmake")
  message(FATAL_ERROR
      "No configured tests exist in ${BUILD_DIR}. Run the matching build command first.")
endif()

execute_process(
    COMMAND "${CTEST_COMMAND}" --test-dir "${BUILD_DIR}" --show-only=json-v1
    RESULT_VARIABLE _discovery_result
    OUTPUT_VARIABLE _catalog
    ERROR_VARIABLE _discovery_error)
if(NOT _discovery_result EQUAL 0)
  message(FATAL_ERROR "CTest discovery failed:\n${_discovery_error}")
endif()

string(JSON _test_count LENGTH "${_catalog}" tests)
if(_test_count EQUAL 0)
  message(FATAL_ERROR "No tests are registered in ${BUILD_DIR}.")
endif()

function(json_quote output value)
  set(_value "${value}")
  string(REPLACE "\\" "\\\\" _value "${_value}")
  string(REPLACE "\"" "\\\"" _value "${_value}")
  string(REPLACE "\r" "\\r" _value "${_value}")
  string(REPLACE "\n" "\\n" _value "${_value}")
  string(REPLACE "\t" "\\t" _value "${_value}")
  set(${output} "\"${_value}\"" PARENT_SCOPE)
endfunction()

set(_tests "[]")
set(_passed 0)
set(_failed 0)
math(EXPR _last_test "${_test_count} - 1")
foreach(_index RANGE 0 ${_last_test})
  string(JSON _name GET "${_catalog}" tests ${_index} name)
  string(TIMESTAMP _started_epoch "%s" UTC)
  execute_process(
      COMMAND "${CTEST_COMMAND}" --test-dir "${BUILD_DIR}"
              --output-on-failure --no-tests=error -R "^${_name}$"
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _stdout
      ERROR_VARIABLE _stderr)
  string(TIMESTAMP _finished_epoch "%s" UTC)
  math(EXPR _duration "${_finished_epoch} - ${_started_epoch}")

  set(_output "${_stdout}${_stderr}")
  message("${_output}")
  if(_result EQUAL 0)
    set(_status passed)
    math(EXPR _passed "${_passed} + 1")
  else()
    set(_status failed)
    math(EXPR _failed "${_failed} + 1")
  endif()

  if(_result MATCHES "^-?[0-9]+$")
    set(_exit_code ${_result})
  else()
    set(_exit_code -1)
    set(_output "${_output}\nCTest process result: ${_result}")
  endif()

  json_quote(_name_json "${_name}")
  json_quote(_status_json "${_status}")
  json_quote(_output_json "${_output}")
  set(_entry "{}")
  string(JSON _entry SET "${_entry}" name "${_name_json}")
  string(JSON _entry SET "${_entry}" status "${_status_json}")
  string(JSON _entry SET "${_entry}" exitCode ${_exit_code})
  string(JSON _entry SET "${_entry}" durationSeconds ${_duration})
  string(JSON _entry SET "${_entry}" output "${_output_json}")
  string(JSON _tests SET "${_tests}" ${_index} "${_entry}")
endforeach()

string(TIMESTAMP _timestamp "%Y-%m-%dT%H:%M:%SZ" UTC)
json_quote(_backend_json "${BACKEND}")
json_quote(_build_type_json "${BUILD_TYPE}")
json_quote(_timestamp_json "${_timestamp}")
set(_run "{}")
string(JSON _run SET "${_run}" backend "${_backend_json}")
string(JSON _run SET "${_run}" buildType "${_build_type_json}")
string(JSON _run SET "${_run}" timestamp "${_timestamp_json}")
string(JSON _run SET "${_run}" passed ${_passed})
string(JSON _run SET "${_run}" failed ${_failed})
string(JSON _run SET "${_run}" tests "${_tests}")

get_filename_component(_report_directory "${REPORT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_report_directory}")
set(_report "{\"runs\":{}}")
if(EXISTS "${REPORT_FILE}")
  file(READ "${REPORT_FILE}" _existing_report)
  string(JSON _report_type ERROR_VARIABLE _report_error
      TYPE "${_existing_report}")
  if(NOT _report_error AND _report_type STREQUAL OBJECT)
    set(_report "${_existing_report}")
    string(JSON _runs_type ERROR_VARIABLE _runs_error
        TYPE "${_report}" runs)
    if(_runs_error OR NOT _runs_type STREQUAL OBJECT)
      string(JSON _report SET "${_report}" runs "{}")
    endif()
  endif()
endif()

string(TOLOWER "${BUILD_TYPE}" _build_type_key)
set(_run_key "${BACKEND}-${_build_type_key}")
string(JSON _report SET "${_report}" runs "${_run_key}" "${_run}")
file(WRITE "${REPORT_FILE}" "${_report}\n")
message("Wrote ${REPORT_FILE}")

if(_failed GREATER 0)
  message(FATAL_ERROR "${_failed} of ${_test_count} tests failed.")
endif()
