# Run the privileged helper with malformed arguments and require both the
# documented exit code (2) and the diagnostic. PASS_REGULAR_EXPRESSION alone
# would accept a helper that prints the message and then crashes.
if(NOT DEFINED HELPER OR NOT DEFINED ARGS OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "expect_helper_exit.cmake needs HELPER, ARGS and EXPECT")
endif()

execute_process(
    COMMAND "${HELPER}" ${ARGS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)

if(NOT result EQUAL 2)
    message(FATAL_ERROR "helper exited '${result}', expected 2\n${out}${err}")
endif()

string(FIND "${out}${err}" "${EXPECT}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "helper output did not contain '${EXPECT}'\n${out}${err}")
endif()
