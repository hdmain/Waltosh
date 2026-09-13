# Apply qlementine patch, or succeed if it is already applied.
# Invoked as FetchContent PATCH_COMMAND from the qlementine source tree.
if(NOT DEFINED QLEMENTINE_PATCH OR NOT EXISTS "${QLEMENTINE_PATCH}")
  message(FATAL_ERROR "QLEMENTINE_PATCH not set or missing: ${QLEMENTINE_PATCH}")
endif()

execute_process(
  COMMAND git apply --ignore-whitespace "${QLEMENTINE_PATCH}"
  RESULT_VARIABLE _apply_rc
  ERROR_VARIABLE _apply_err
  OUTPUT_VARIABLE _apply_out
)

if(_apply_rc EQUAL 0)
  message(STATUS "Applied qlementine patch")
  return()
endif()

execute_process(
  COMMAND git apply --ignore-whitespace --reverse --check "${QLEMENTINE_PATCH}"
  RESULT_VARIABLE _already_rc
)

if(_already_rc EQUAL 0)
  message(STATUS "qlementine patch already applied — skipping")
  return()
endif()

message(FATAL_ERROR
  "Failed to apply qlementine patch:\n${_apply_out}${_apply_err}")
