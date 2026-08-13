# Windows-friendly archive step: vita-libs-gen's @response file breaks arm-vita-eabi-ar here.
set(stub_dir "${STUBS_DIR}")
if (NOT stub_dir)
  message(FATAL_ERROR "STUBS_DIR not set")
endif()

# Assemble any missing .o from .S via make (ignore ar failure).
execute_process(
  COMMAND make -C "${stub_dir}"
  RESULT_VARIABLE make_rc
)
file(GLOB objs "${stub_dir}/*.o")
list(LENGTH objs n)
if (n EQUAL 0)
  message(FATAL_ERROR "No stub objects in ${stub_dir} (make rc=${make_rc})")
endif()

find_program(AR_BIN arm-vita-eabi-ar REQUIRED)
set(out "${stub_dir}/libSceLibcBridge_stub.a")
file(REMOVE "${out}")
execute_process(
  COMMAND "${AR_BIN}" cru "${out}" ${objs}
  WORKING_DIRECTORY "${stub_dir}"
  RESULT_VARIABLE ar_rc
)
if (NOT ar_rc EQUAL 0)
  message(FATAL_ERROR "arm-vita-eabi-ar failed: ${ar_rc}")
endif()
message(STATUS "Packed ${n} stub objects -> ${out}")
