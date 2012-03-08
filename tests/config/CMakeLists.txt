function(compile_common_test executable source)
    # create executable
    ADD_EXECUTABLE(${executable} ${source})

    TARGET_LINK_LIBRARIES(${executable} ${OONF_LIBPREFIX}_common)
    TARGET_LINK_LIBRARIES(${executable} ${OONF_LIBPREFIX}_config)

    # link extra win32 libs
    IF(WIN32)
        SET_TARGET_PROPERTIES(${executable} PROPERTIES ENABLE_EXPORTS true)
        TARGET_LINK_LIBRARIES(${executable} ws2_32 iphlpapi)
    ENDIF(WIN32)
endfunction(add_compiler_flag)

set(TESTS test_config_list
          test_config_validation
          test_config_mapping
          test_config_cmd
          test_config_default
          test_config_delta)

foreach(TEST ${TESTS})
    compile_common_test(${TEST} ${TEST}.c)
    ADD_TEST(NAME ${TEST} COMMAND ${TEST})
endforeach(OONF_EXE)
