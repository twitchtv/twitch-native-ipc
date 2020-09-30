function(compile_d3dshaders targetname)
    find_program(fxcexe fxc)
    if(NOT fxcexe)
        message(FATAL_ERROR "Could not find fxc exe")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL Debug)
        set(shaderdebug /Zi)
    endif()

    set(oneValueArgs ENTRY VARIABLE_NAME SHADER_PROFILE HEADER_PATH SOURCE)
    cmake_parse_arguments(D3DCOMPILE "" "${oneValueArgs}" "" ${ARGN})


    set(compilecmd ${fxcexe} ${shaderdebug} /E ${D3DCOMPILE_ENTRY} /Vn ${D3DCOMPILE_VARIABLE_NAME} /T ${D3DCOMPILE_SHADER_PROFILE} /Fh ${D3DCOMPILE_HEADER_PATH} /WX /nologo ${D3DCOMPILE_SOURCE})

    add_custom_command(OUTPUT ${D3DCOMPILE_HEADER_PATH}
        COMMAND ${compilecmd}
        MAIN_DEPENDENCY ${D3DCOMPILE_SOURCE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        VERBATIM
        COMMENT "Compiling shaders"
        )

    add_custom_target(${targetname}
        SOURCES ${D3DCOMPILE_HEADER_PATH}
        )
endfunction()


#Enables VS code analyzer support globally for all add_subdirectory after this call
#Cannot be run in parallel, if running with cmake needs -j 1 option set otherwise may fail to write to the log
function(setup_msvc_code_analyzer logpath rulesetpath)
    if(NOT ${ENABLE_VS_ANALYZER})
        return()
    endif()

    if(NOT MSVC)
        message(FATAL_ERROR "MSVC code analyzer only works for MSVC builds")
    endif()

    if(NOT DISABLE_VS_ANALYZER_LOG)
        set(logoptions
            /analyze:quiet
            /analyze:log ${logpath}
            )
    else()
        set(logoptions)
    endif()

    add_compile_options(
        /analyze
        "${logoptions}"
        /analyze:pluginEspXEngine.dll
        /analyze:pluginlocalespc.dll
        /analyze:ruleset${rulesetpath}
        )
endfunction()


# update defaults for windows platform
macro(setup_default_cxx_compile_options)
    message("Setting up Windows default cxx settings")

    #can remove most of these with cmake 3.15 and above with policy CMP0091, CMP0092
    #remove certain settings from the defaults, to set our own
    string(REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    string(REPLACE "/W3" "" CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
    update_default_compile_options(UPDATEFROM "/MTd" UPDATETO "")
    update_default_compile_options(UPDATEFROM "/MDd" UPDATETO "")
    update_default_compile_options(UPDATEFROM "/MD" UPDATETO "")
    update_default_compile_options(UPDATEFROM "/MT" UPDATETO "")

    update_default_compile_options(UPDATEFROM "/EHsc" UPDATETO "")

    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

    #enable stricter warnings and warnings as errors
    #disables warning on anonymous structs because windows uses them intrinsically
    #and using /W4 flags them as warnings
    #use parallel builds
    #prettier error messages
    add_compile_options(/W4 /WX /EHa /wd4201 /wd4127 /MP /diagnostics:caret /bigobj)

    if(NOT MSVC_DYNAMIC_RUNTIME)
        if(runtimesettingsupport)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            add_compile_options("/MT$<$<CONFIG:Debug>:d>")
        endif()
    else()
        if(runtimesettingsupport)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        else()
            #default to dynamic linking which is cmake's default
            add_compile_options("/MD$<$<CONFIG:Debug>:d>")
        endif()
    endif()

    #disable warnings in VS2017 for google mock headers in Release/RelWithDebInfo builds
    # i.e. unreachable code warning in gmock-generated-actions.h
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(/wd4702)
    endif()
endmacro()


function(setup_windows_manifest appname)
    set(WINDOWSMANIFESTDESC ${appname})
    configure_file(${CMAKE_CURRENT_LIST_DIR}/cmakeUtils/windows.manifest.in ${CMAKE_BINARY_DIR}/windows.manifest)
endfunction()
