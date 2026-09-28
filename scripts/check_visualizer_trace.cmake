if(NOT DEFINED REPLAY OR NOT DEFINED EVENTS OR NOT DEFINED EXPECTED)
  message(FATAL_ERROR "REPLAY, EVENTS, and EXPECTED are required")
endif()

execute_process(
  COMMAND "${REPLAY}" --trace-json "${EVENTS}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE actual
  ERROR_VARIABLE error_output
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "orda_replay trace export failed: ${error_output}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT "${actual}" STREQUAL "${expected}")
  message(FATAL_ERROR
    "The visualizer trace is stale. Regenerate it with:\n"
    "  ./build/orda_replay --trace-json visualizer/scenarios/price-time-priority.events > visualizer/data/price-time-priority.json"
  )
endif()

# The export cap keeps one frame per event bounded. Exporting one event more
# than the cap must fail loudly instead of writing a partial trace.
set(over_limit_events "${CMAKE_CURRENT_BINARY_DIR}/trace-cap-events.txt")
set(over_limit_content "")
foreach(index RANGE 1 501)
  string(APPEND over_limit_content "ADD ${index} BUY 100 1\n")
endforeach()
file(WRITE "${over_limit_events}" "${over_limit_content}")

execute_process(
  COMMAND "${REPLAY}" --trace-json "${over_limit_events}"
  RESULT_VARIABLE cap_result
  OUTPUT_VARIABLE cap_output
  ERROR_VARIABLE cap_error
)
if(cap_result EQUAL 0)
  message(FATAL_ERROR "orda_replay exported more than 500 events without failing")
endif()
if(NOT cap_error MATCHES "limited to 500 events")
  message(FATAL_ERROR "Unclear error when the trace cap is exceeded: ${cap_error}")
endif()
if(NOT cap_output STREQUAL "")
  message(FATAL_ERROR "A rejected over-cap export must not write a trace to stdout")
endif()
