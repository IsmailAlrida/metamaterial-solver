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
message(STATUS
    "Running ${_test_count} registered tests for ${BACKEND} ${BUILD_TYPE}.")

function(json_quote output value)
  set(_value "${value}")
  string(REPLACE "\\" "\\\\" _value "${_value}")
  string(REPLACE "\"" "\\\"" _value "${_value}")
  string(REPLACE "\r" "\\r" _value "${_value}")
  string(REPLACE "\n" "\\n" _value "${_value}")
  string(REPLACE "\t" "\\t" _value "${_value}")
  set(${output} "\"${_value}\"" PARENT_SCOPE)
endfunction()

function(failure_excerpt output value)
  string(REPLACE "\r\n" "\n" _text "${value}")
  string(REPLACE ";" "\\;" _text "${_text}")
  string(REPLACE "\n" ";" _lines "${_text}")
  set(_excerpt "")
  set(_matches 0)
  foreach(_line IN LISTS _lines)
    string(TOLOWER "${_line}" _lower)
    if(_lower MATCHES "\\[fail\\]|\\[diagnostic\\]|failed|error|abort|access violation|suite:")
      string(APPEND _excerpt "${_line}\n")
      math(EXPR _matches "${_matches} + 1")
      if(_matches EQUAL 20)
        break()
      endif()
    endif()
  endforeach()
  string(STRIP "${_excerpt}" _excerpt)
  set(${output} "${_excerpt}" PARENT_SCOPE)
endfunction()

set(_tests "[]")
set(_passed 0)
set(_failed 0)
set(_blocked 0)
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
  if(_result EQUAL 0)
    set(_status passed)
    math(EXPR _passed "${_passed} + 1")
    message(STATUS "[PASS] ${_name} (${_duration}s)")
  elseif(_output MATCHES "Failed test dependencies|\\*\\*\\*Not Run")
    set(_status blocked)
    math(EXPR _blocked "${_blocked} + 1")
    message(STATUS "[BLOCKED] ${_name} (${_duration}s)")
    message("${_output}")
  else()
    set(_status failed)
    math(EXPR _failed "${_failed} + 1")
    message(STATUS "[FAIL] ${_name} (${_duration}s)")
    message("${_output}")
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
  failure_excerpt(_failure_excerpt "${_output}")
  json_quote(_failure_excerpt_json "${_failure_excerpt}")
  set(_entry "{}")
  string(JSON _entry SET "${_entry}" name "${_name_json}")
  string(JSON _entry SET "${_entry}" status "${_status_json}")
  string(JSON _entry SET "${_entry}" exitCode ${_exit_code})
  string(JSON _entry SET "${_entry}" durationSeconds ${_duration})
  string(JSON _entry SET "${_entry}" output "${_output_json}")
  string(JSON _entry SET "${_entry}" failureSummary
      "${_failure_excerpt_json}")
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
string(JSON _run SET "${_run}" blocked ${_blocked})
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

get_filename_component(_report_directory "${REPORT_FILE}" DIRECTORY)
get_filename_component(_report_stem "${REPORT_FILE}" NAME_WE)
set(_markdown_file "${_report_directory}/${_report_stem}.md")
set(_markdown
    "# Test Report\n\nGenerated: ${_timestamp}\n\n")
string(JSON _run_count LENGTH "${_report}" runs)
if(_run_count GREATER 0)
  math(EXPR _last_run "${_run_count} - 1")
  foreach(_run_index RANGE 0 ${_last_run})
    string(JSON _key MEMBER "${_report}" runs ${_run_index})
    string(JSON _run_backend GET "${_report}" runs "${_key}" backend)
    string(JSON _run_build_type GET "${_report}" runs "${_key}" buildType)
    string(JSON _run_timestamp GET "${_report}" runs "${_key}" timestamp)
    string(JSON _run_passed GET "${_report}" runs "${_key}" passed)
    string(JSON _run_failed GET "${_report}" runs "${_key}" failed)
    string(JSON _run_blocked ERROR_VARIABLE _blocked_error GET
        "${_report}" runs "${_key}" blocked)
    if(_blocked_error)
      set(_run_blocked 0)
    endif()
    math(EXPR _run_total
        "${_run_passed} + ${_run_failed} + ${_run_blocked}")
    string(APPEND _markdown
        "## ${_run_backend} ${_run_build_type}\n\n"
        "Run: ${_run_timestamp}  \n"
        "Result: **${_run_passed} passed, ${_run_failed} failed, ${_run_blocked} blocked, ${_run_total} total**\n\n"
        "| Test | Status | Time | Exit code |\n"
        "|---|---:|---:|---:|\n")

    string(JSON _run_test_count LENGTH
        "${_report}" runs "${_key}" tests)
    if(_run_test_count GREATER 0)
      set(_failure_details "")
      math(EXPR _last_run_test "${_run_test_count} - 1")
      foreach(_test_index RANGE 0 ${_last_run_test})
        string(JSON _test_name GET
            "${_report}" runs "${_key}" tests ${_test_index} name)
        string(JSON _test_status GET
            "${_report}" runs "${_key}" tests ${_test_index} status)
        string(JSON _test_duration GET
            "${_report}" runs "${_key}" tests ${_test_index} durationSeconds)
        string(JSON _test_exit_code GET
            "${_report}" runs "${_key}" tests ${_test_index} exitCode)
        string(TOUPPER "${_test_status}" _test_status)
        string(REPLACE "|" "\\|" _test_name "${_test_name}")
        string(APPEND _markdown
            "| ${_test_name} | ${_test_status} | ${_test_duration}s | ${_test_exit_code} |\n")
        if(_test_status STREQUAL "FAILED" OR _test_status STREQUAL "BLOCKED")
          string(JSON _test_failure ERROR_VARIABLE _failure_error GET
              "${_report}" runs "${_key}" tests ${_test_index}
              failureSummary)
          if(_failure_error)
            string(JSON _test_output GET
                "${_report}" runs "${_key}" tests ${_test_index} output)
            failure_excerpt(_test_failure "${_test_output}")
          endif()
          if(_test_failure STREQUAL "")
            set(_test_failure "CTest exited with code ${_test_exit_code}.")
          endif()
          string(APPEND _failure_details
              "### ${_test_name}\n\n```text\n${_test_failure}\n```\n\n")
        endif()
      endforeach()
      if(NOT _failure_details STREQUAL "")
        string(APPEND _markdown
            "\n### Failure and blocked summary\n\n${_failure_details}")
      endif()
    endif()
    string(APPEND _markdown "\n")
  endforeach()
endif()
file(WRITE "${_markdown_file}" "${_markdown}")

math(EXPR _total "${_passed} + ${_failed} + ${_blocked}")
message(STATUS
    "Summary: ${_passed} passed, ${_failed} failed, ${_blocked} blocked, ${_total} total.")
message(STATUS "JSON report: ${REPORT_FILE}")
message(STATUS "Markdown report: ${_markdown_file}")

math(EXPR _unsuccessful "${_failed} + ${_blocked}")
if(_unsuccessful GREATER 0)
  message(FATAL_ERROR
      "${_failed} tests failed and ${_blocked} were blocked out of ${_test_count}.")
endif()
