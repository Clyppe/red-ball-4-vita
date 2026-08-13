set(stamp "${CMAKE_BINARY_DIR}/vitagl_legacy.stamp")
set(want "ENABLE_LEGACY_PIPELINE=1")
set(have "")
if (EXISTS "${stamp}")
  file(READ "${stamp}" have)
endif()

if (EXISTS "${VITAGL_LIB}" AND have STREQUAL want)
  message(STATUS "vitaGL ready (legacy pipeline): ${VITAGL_LIB}")
  return()
endif()

message(STATUS "Building vitaGL with ${VITAGL_MAKE_FLAGS} ...")
separate_arguments(args UNIX_COMMAND "${VITAGL_MAKE_FLAGS}")
execute_process(
  COMMAND make -C "${VITAGL_DIR}" -j2 ${args}
  RESULT_VARIABLE rc
)
if (NOT rc EQUAL 0)
  message(FATAL_ERROR "vitaGL build failed: ${rc}")
endif()
file(WRITE "${stamp}" "${want}")
