include(CMakeParseArguments)

set(AMNEZIA_TEST_LABELS unit contract qml integration remote)
set(AMNEZIA_TEST_DEFAULT_TIMEOUT 120 CACHE STRING "Default timeout for first-party CTest tests")

# Register a Qt Test executable and attach it to the first-party aggregate.
#
#   amnezia_add_qt_test(
#       NAME my_test
#       SOURCES my_test.cpp
#       LABELS unit contract
#       LIBRARIES production_target
#       INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/client
#       TEST_ARGS -platform offscreen
#       ENVIRONMENT QT_QPA_PLATFORM=offscreen
#       TIMEOUT 60)
function(amnezia_add_qt_test)
    set(_options)
    set(_one_value_args NAME TIMEOUT WORKING_DIRECTORY)
    set(_multi_value_args
        SOURCES
        LABELS
        LIBRARIES
        INCLUDE_DIRECTORIES
        DEFINITIONS
        TEST_ARGS
        ENVIRONMENT
    )
    cmake_parse_arguments(TEST "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT TEST_NAME)
        message(FATAL_ERROR "amnezia_add_qt_test requires NAME")
    endif()
    if(NOT TEST_SOURCES)
        message(FATAL_ERROR "amnezia_add_qt_test(${TEST_NAME}) requires SOURCES")
    endif()
    if(NOT TEST_LABELS)
        set(TEST_LABELS unit)
    endif()

    foreach(_label IN LISTS TEST_LABELS)
        if(NOT _label IN_LIST AMNEZIA_TEST_LABELS)
            message(FATAL_ERROR
                "Unknown label '${_label}' for ${TEST_NAME}; expected one of ${AMNEZIA_TEST_LABELS}")
        endif()
    endforeach()

    if(TARGET "${TEST_NAME}")
        message(FATAL_ERROR "First-party test target '${TEST_NAME}' is already defined")
    endif()

    add_executable("${TEST_NAME}" ${TEST_SOURCES})
    set_target_properties("${TEST_NAME}" PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
    )
    target_link_libraries("${TEST_NAME}" PRIVATE Qt6::Core Qt6::Test ${TEST_LIBRARIES})

    if(TEST_INCLUDE_DIRECTORIES)
        target_include_directories("${TEST_NAME}" PRIVATE ${TEST_INCLUDE_DIRECTORIES})
    endif()
    if(TEST_DEFINITIONS)
        target_compile_definitions("${TEST_NAME}" PRIVATE ${TEST_DEFINITIONS})
    endif()

    add_dependencies(amnezia-tests "${TEST_NAME}")

    if(NOT TEST_TIMEOUT)
        set(TEST_TIMEOUT ${AMNEZIA_TEST_DEFAULT_TIMEOUT})
    endif()

    add_test(
        NAME "${TEST_NAME}"
        COMMAND "$<TARGET_FILE:${TEST_NAME}>" ${TEST_TEST_ARGS}
    )
    set(_test_properties
        LABELS "${TEST_LABELS}"
        TIMEOUT "${TEST_TIMEOUT}"
    )
    if(TEST_WORKING_DIRECTORY)
        list(APPEND _test_properties WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}")
    endif()
    set_tests_properties("${TEST_NAME}" PROPERTIES ${_test_properties})
    if(TEST_ENVIRONMENT)
        set_tests_properties("${TEST_NAME}" PROPERTIES ENVIRONMENT "${TEST_ENVIRONMENT}")
    endif()
endfunction()

# Short compatibility alias for Qt-based first-party tests. Keep the Qt
# spelling in new suites so it is obvious that Qt Test is linked.
function(amnezia_add_test)
    amnezia_add_qt_test(${ARGN})
endfunction()
