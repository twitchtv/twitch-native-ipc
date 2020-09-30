include(formatting)
include(tooling)

if(WIN32)
    include(windows)
elseif(UNIX)
    include(unix)
else()
    message(FATAL_ERROR "Unknown os type")
endif()

#general utility macros and functions for CMake
include(FetchContent)


# loops through the default CXX flags and replaces the defaults
macro(update_default_compile_options)
    set(oneValueArgs UPDATEFROM UPDATETO)
    cmake_parse_arguments(UPDATE_OPTIONS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    #message("options ${UPDATE_OPTIONS_UPDATEFROM} ${UPDATE_OPTIONTS_UPDATETO}")
    foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES ${UPDATE_OPTIONS_UPDATEFROM})
            string(REGEX REPLACE "${UPDATE_OPTIONS_UPDATEFROM}" "${UPDATE_OPTIONS_UPDATETO}" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
endmacro()

macro(set_output_directories output_dir)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${output_dir})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${output_dir})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${output_dir})

    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config} ${output_dir})
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${output_dir})
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config} ${output_dir})
    endforeach()
endmacro()

# print default compile and linker options
function(print_default_options)
    message("types ${CMAKE_CONFIGURATION_TYPES}")
    message("build type: ${CMAKE_BUILD_TYPE}")
    message("architecture: ${CMAKE_SIZEOF_VOID_P}")
    message("Compile Options:")
    foreach(flag_var
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
        message("${flag_var} is ${${flag_var}}")
    endforeach()

    if(MSVC)
        message("Runtime setting: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
    endif()

    message("Linker Options:")
    foreach(flag_var
            CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_RELEASE
            CMAKE_EXE_LINKER_FLAGS_MINSIZEREL CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
            CMAKE_SHARED_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS_DEBUG
            CMAKE_SHARED_LINKER_FLAGS_RELEASE
            CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO
            CMAKE_STATIC_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS_DEBUG
            CMAKE_STATIC_LINKER_FLAGS_RELEASE
            CMAKE_STATIC_LINKER_FLAGS_MINSIZEREL CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO
            CMAKE_MODULE_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS_DEBUG
            CMAKE_MODULE_LINKER_FLAGS_RELEASE
            CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO)
        message("${flag_var} is ${${flag_var}}")
    endforeach()
endfunction()

#clone repositories or download data during config step, uses FetchContent module in cmake 3.11
#replaces the use of the DownloadProject cmake file
function(fetch_dependencies projname downloaddir skippopulate)
    #set(FETCHCONTENT_QUIET OFF)
    set(srcdir ${downloaddir}/${projname})

    FetchContent_Declare(${projname}
        ${ARGN}
        SOURCE_DIR ${srcdir}
        )

    #needed to define the source dir cached variable correctly
    string(TOUPPER ${projname} projnameupper)

    #automatically skip clone/update steps if checked out folder exists
    #NOTE: this disables updates, so if a change is made to the tag in cmake it won't update
    if(NOT FETCHCONTENT_SOURCE_DIR_${projnameupper})
        if(EXISTS ${srcdir})
            message("Assuming ${projname} has been checked out in ${srcdir}")
            set(FETCHCONTENT_SOURCE_DIR_${projnameupper} ${srcdir} CACHE FILEPATH "" FORCE)
        endif()
    endif()

    if(NOT ${projname}_POPULATED)
        FetchContent_Populate(${projname})

        if(NOT skippopulate)
            add_subdirectory(${${projname}_SOURCE_DIR} ${${projname}_BINARY_DIR})
        endif()
    endif()
endfunction()


# Copies a directory using configure_file
function(copy_directory srcPath destPath)
    message(STATUS "Copying ${srcPath} to ${destPath}")

    make_directory(${destPath})

    file(GLOB filesToCopy RELATIVE "${srcPath}" "${srcPath}/*")
    foreach(file ${filesToCopy})
        configure_file("${srcPath}/${file}" "${destPath}/${file}" COPYONLY)
    endforeach()
endfunction()
