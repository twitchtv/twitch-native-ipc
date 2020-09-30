#generates a target that will format all code that was added with the generate_format_target function
function(generate_formatall_target targetname property)
    add_custom_target(${targetname})

    get_property(alltargets GLOBAL PROPERTY ${property})

    foreach(target ${alltargets})
        add_dependencies(${targetname} ${target})
    endforeach()
endfunction()

# creates a format_<target> target that will format all code in the library/executable
# for header only libraries a list of all header files need to be passed in the filelist argument
# only modified files will get reformatted
# modified from:
# https://www.linkedin.com/pulse/simple-elegant-wrong-how-integrate-clang-format-friends-brendan-drew/
function(generate_format_target targetname property filelist)
    #find clang_format
    find_program(clangformatpath NAMES "clang-format" PATHS ${CMAKE_BINARY_DIR} NO_DEFAULT_PATH)
    find_program(clangformatpath NAMES "clang-format")
    if(NOT clangformatpath)
        message(FATAL_ERROR "Could not find clang format")
    endif()

    if(filelist)
        message(WARNING "filelist is not empty ${filelist}")
    endif()

    get_target_property(type ${targetname} TYPE)

    if(${type} STREQUAL OBJECT_LIBRARY)
        message(FATAL_ERROR "object libraries not supported for target ${targetname}")
        return()
    elseif(${type} STREQUAL INTERFACE_LIBRARY)
        get_target_property(sources ${targetname} INTERFACE_SOURCES)
    else()
        get_target_property(sources ${targetname} SOURCES)
    endif()

    set(formatfilelist "")
    foreach(source ${sources})
        if(NOT TARGET ${source})
            get_source_file_property(fullpath ${source} LOCATION)

            string(REPLACE ${CMAKE_SOURCE_DIR}/ "" stripped ${fullpath})
            set(formatfile ${CMAKE_BINARY_DIR}/format/${stripped}.format)

            get_filename_component(outputdir ${formatfile} DIRECTORY)

            add_custom_command(OUTPUT ${formatfile}
                DEPENDS ${source}
                COMMENT "Format ${source}"
                COMMAND ${clangformatpath} -style=file -i ${fullpath}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${outputdir}
                COMMAND ${CMAKE_COMMAND} -E touch ${formatfile}
                )

            list(APPEND formatfilelist ${formatfile})
        endif()
    endforeach()

    if(formatfilelist)
        set(formattarget format_${targetname})

        add_custom_target(${formattarget}
            SOURCES ${formatfilelist}
            COMMENT "Format target ${targetname}"
            )

        add_dependencies(${targetname} ${formattarget})

        get_property(propertydefined GLOBAL PROPERTY ${property} DEFINED)
        if(NOT propertydefined)
            define_property(GLOBAL PROPERTY ${property} BRIEF_DOCS "Format Targets" FULL_DOCS "Format Targets")
        endif()

        get_property(formattargets GLOBAL PROPERTY ${property})

        list(APPEND formattargets ${formattarget})
        set_property(GLOBAL PROPERTY ${property} ${formattargets})
    endif()
endfunction()


# used to create a target that will format all code using the spotlight_format_code function
function(format_all targetname)
    if(ENABLE_CODE_FORMATTING)
        generate_formatall_target(${targetname} ${CMAKE_PROJECT_NAME}_FORMAT_TARGETS)
    endif()
endfunction()


# used to create a format_<targetname> that will format all code in the exe/library
# for header only add 'SOURCES <sourcelist>' in order to pass the header files
function(format_code targetname)
    if(ENABLE_CODE_FORMATTING)
        set(multiValueArgs SOURCES)
        cmake_parse_arguments(FORMAT_OPTIONS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
        generate_format_target(${targetname} ${CMAKE_PROJECT_NAME}_FORMAT_TARGETS
            "${FORMAT_OPTIONS_SOURCES}")
    endif()
endfunction()
