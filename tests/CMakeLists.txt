# In-tree tests.
#
# The real corpus/differential/fuzz harness lives outside the checkout in
# tests/ at the work folder root. This directory only holds what CI can run on a
# bare runner: a smoke target that exercises the low-level IO, the LZX decoder's
# error paths and the image openers, plus (optionally) libFuzzer targets.

add_executable(xbi_smoke smoke.cpp)
target_link_libraries(xbi_smoke PRIVATE XboxInternals velocity_compiler_flags)
add_test(NAME xbi_smoke COMMAND xbi_smoke)
set_tests_properties(xbi_smoke PROPERTIES TIMEOUT 120)

add_executable(device_policy_test device_policy_test.cpp)
target_link_libraries(device_policy_test PRIVATE device_validation velocity_compiler_flags)
add_test(NAME device_policy_test COMMAND device_policy_test)
set_tests_properties(device_policy_test PROPERTIES TIMEOUT 30)

# Argument hard-fail tests for the privileged helper. These only assert that
# malformed invocations exit before binding/waiting; they do not need root.
# The wrapper checks both the documented exit code (2) and the diagnostic, so
# a helper that prints the message and then crashes still fails.
set(HELPER_ARG_TEST ${CMAKE_CURRENT_SOURCE_DIR}/expect_helper_exit.cmake)
add_test(NAME device_helper_rejects_missing_pid
         COMMAND ${CMAKE_COMMAND}
                 "-DHELPER=$<TARGET_FILE:velocity-device-helper>"
                 "-DARGS=--endpoint;test"
                 "-DEXPECT=usage:"
                 -P ${HELPER_ARG_TEST})
set_tests_properties(device_helper_rejects_missing_pid PROPERTIES TIMEOUT 10)
add_test(NAME device_helper_rejects_zero_pid
         COMMAND ${CMAKE_COMMAND}
                 "-DHELPER=$<TARGET_FILE:velocity-device-helper>"
                 "-DARGS=--endpoint;test;--client-pid;0"
                 "-DEXPECT=invalid --client-pid"
                 -P ${HELPER_ARG_TEST})
set_tests_properties(device_helper_rejects_zero_pid PROPERTIES TIMEOUT 10)
if(UNIX)
    add_test(NAME device_helper_rejects_bad_endpoint
             COMMAND ${CMAKE_COMMAND}
                     "-DHELPER=$<TARGET_FILE:velocity-device-helper>"
                     "-DARGS=--endpoint;/tmp/velocity-bad.sock;--client-pid;123"
                     "-DEXPECT=invalid endpoint:"
                     -P ${HELPER_ARG_TEST})
    set_tests_properties(device_helper_rejects_bad_endpoint PROPERTIES TIMEOUT 10)
endif()

if(BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "BUILD_FUZZERS requires Clang (libFuzzer runtime)")
    endif()

    # The library should be built with -fsanitize=fuzzer-no-link (plus
    # address,undefined) so libFuzzer gets coverage from it; these targets add the
    # fuzzer runtime itself.
    function(add_fuzz_target name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE XboxInternals)
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    endfunction()

    add_fuzz_target(lzx_fuzz fuzz/lzx_fuzz.cpp)
    add_fuzz_target(xex_fuzz fuzz/xex_fuzz.cpp)
endif()
