include(utilities)
include(conan)
include(conanutils)

macro(setup_project)
    if(NOT CMAKE_BUILD_TYPE)
        #we don't support complete multi-config, limitations of conan
        message(WARNING "CMAKE_BUILD_TYPE should be defined")
    endif()

    option(ENABLE_CODE_COVERAGE "enable code coverage support" OFF)
    option(ENABLE_CODE_FORMATTING "enable code formatting support" ON)
    option(ENABLE_CODE_DOCUMENTATION "enable code documenation generation" OFF)
    option(MSVC_DYNAMIC_RUNTIME "change default runtime" ON)

    conan_check(VERSION 1.22.2 REQUIRED)

    setup_default_cxx_compile_options()

    set_output_directories(${CMAKE_BINARY_DIR}/bin)

    run_conan()

    include(CTest)
    if(BUILD_TESTING)
        enable_testing()
    endif()
endmacro()
