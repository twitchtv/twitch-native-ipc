#adds a test with the postfix _coverage that will generate coverage
#must be run in the same location as the test cmake file
#assumes that tests are a subdir of the main component
#uses ENABLE_CODE_COVERAGE as an option to enable/disable
function(setup_code_coverage testname target)
    if(NOT ${ENABLE_CODE_COVERAGE})
        return()
    endif()

    if(NOT MSVC)
        message(FATAL_ERROR "Only MSVC supported right now")
        #[TODO] add support for other platforms
    endif()

    find_program(coveragepath opencppcoverage)

    if(NOT coveragepath)
        message(WARNING "Could not find opencppcoverage")
        return()
    endif()

    #get find a source filter by getter to parent directories and finding the relative name between them
    get_directory_property(parentdir PARENT_DIRECTORY)
    get_directory_property(grandparentdir DIRECTORY ${parentdir} PARENT_DIRECTORY)
    file(RELATIVE_PATH srcfilter ${grandparentdir} ${parentdir})

    add_test(NAME ${testname}_coverage
        COMMAND ${coveragepath} --sources=${srcfilter} --modules=$<TARGET_FILE_NAME:${target}> --export_type=html:${CMAKE_BINARY_DIR}/coverage/${target} -- $<TARGET_FILE:${target}>
        )
    set_tests_properties(${testname}_coverage PROPERTIES LABELS "coverage")
endfunction()

#adds unit test according to spotlight guidelines
#adds single test run under label "unit"
#adds a stress test run of x1000  under label "stress_unit"
function(googletest_add_test testname target)
    add_test(NAME ${testname}
        COMMAND $<TARGET_FILE:${target}> --gtest_shuffle --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results/${target}.xml)
    set_tests_properties(${testname} PROPERTIES LABELS "unit")

    add_test(NAME ${testname}_skipGraphics
        COMMAND $<TARGET_FILE:${target}> --gtest_shuffle --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results/${target}_skipGraphics.xml --gtest_filter=-*Graphics*)
    set_tests_properties(${testname}_skipGraphics PROPERTIES LABELS "automation")

    add_test(NAME ${testname}_stress
        COMMAND $<TARGET_FILE:${target}> --gtest_shuffle --gtest_repeat=300 --gtest_filter=-*Slow*:*Graphics*)
    set_tests_properties(${testname}_stress PROPERTIES LABELS "stress")

    #add code coverage support if enabled
    setup_code_coverage(${testname} ${target})
endfunction()

function(setup_doxypress)
    set(toolsdir "${PROJECT_SOURCE_DIR}/cmakeUtils")
    #setup doxygen scripts
    if(NOT EXISTS ${CMAKE_BINARY_DIR}/doxypress.json)
        set(DOXY_EXCLUDE_FILES "\"${CMAKE_BINARY_DIR}\",\"external\"")
        set(DOXY_EXCLUDE_PATTERNS "\"*/tests/*\"")
        set(DOXY_XML_OUTPUT_DIR ${CMAKE_BINARY_DIR}/documentation/xml)
        set(DOXY_HTML_OUTPUT_DIR ${CMAKE_BINARY_DIR}/documentation/html)
        configure_file("${toolsdir}/doxypress.json.in"
            "${CMAKE_BINARY_DIR}/doxypress.json")
    endif()

    if(NOT EXISTS ${CMAKE_BINARY_DIR}/run_doxypress.sh)
        set(DOXY_SOURCE_DIR ${PROJECT_SOURCE_DIR})
        set(DOXY_FILE ${CMAKE_BINARY_DIR}/doxypress.json)
        configure_file("${toolsdir}/run_doxypress.sh.in"
            "${CMAKE_BINARY_DIR}/run_doxypress.sh"
            )
    endif()
endfunction()

function(documentation_target)
    if(NOT ENABLE_CODE_DOCUMENTATION)
        return()
    endif()

    find_program(doxypresspath doxypress)

    if(NOT doxypresspath)
        message(WARNING "Could not find doxypress")
        return()
    endif()

    setup_doxypress()

    add_custom_target(generate_docs
        COMMAND ${CMAKE_BINARY_DIR}/run_doxypress.sh
        )
endfunction()
