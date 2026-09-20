cmake_minimum_required(VERSION 3.25.0)

# First-party tests are deliberately kept outside the application target. The
# aggregate target is the only target that quality-test builds, while CTest
# discovers only tests registered by this directory and its suites.
add_custom_target(amnezia-tests)

if(IOS OR ANDROID OR MACOS_NE)
    message(STATUS "First-party self-hosted tests are desktop-only on this build")
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Test)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/test_helpers.cmake)

# Suites are optional so the harness can land before their component-owned
# CMakeLists files. Component agents add tests only in their owned directories.
foreach(_suite IN ITEMS
    selfhosted/contracts
    selfhosted/installers
    selfhosted/ssh
    selfhosted/install_controller
    selfhosted/ui
)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_suite}/CMakeLists.txt")
        add_subdirectory("${_suite}")

        # Component suites may use the shared helper or register their target
        # directly. Make every target declared by that suite part of the
        # first-party aggregate built by `just quality test`.
        get_property(_suite_targets
            DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${_suite}"
            PROPERTY BUILDSYSTEM_TARGETS
        )
        foreach(_suite_target IN LISTS _suite_targets)
            add_dependencies(amnezia-tests "${_suite_target}")
        endforeach()
    endif()
endforeach()
