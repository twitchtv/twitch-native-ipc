# generate a conan profile to match the build requirements
function(generate_windows_conan_profile)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(NATIVETEMPLATEARCH x86_64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(NATIVETEMPLATEARCH x86)
    else()
        message(FATAL "Unsupported architecture ${CMAKE_SIZEOF_VOID_P}")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        #In general most external packages don't support RelWithDebInfo very well
        #So we default to release here
        set(NATIVETEMPLATEBUILDTYPE "Release")
    else()
        set(NATIVETEMPLATEBUILDTYPE ${CMAKE_BUILD_TYPE})
    endif()

    if(NOT MSVC_VERSION VERSION_LESS 1910 AND MSVC_VERSION VERSION_LESS 1920)
        set(NATIVETEMPLATECOMPILERVERSION 15)
    elseif(NOT MSVC_VERSION VERSION_LESS 1920 AND MSVC_VERSION VERSION_LESS 1930)
        set(NATIVETEMPLATECOMPILERVERSION 16)
    else()
        message(FATAL "Unsupported MSVC compiler ${MSVC_VERSION}")
    endif()

    if(NOT MSVC_DYNAMIC_RUNTIME)
        set(NATIVETEMPLATECOMPILERRUNTIME "MT")
    else()
        #default to MD
        set(NATIVETEMPLATECOMPILERRUNTIME "MD")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        string(APPEND NATIVETEMPLATECOMPILERRUNTIME "d")
    endif()

    configure_file(${PROJECT_SOURCE_DIR}/cmakeUtils/conanprofiles/windows_profile.txt.in
        ${CMAKE_BINARY_DIR}/conan_profile.txt
        )
endfunction()

# make the call to conan to deal with external dependencies
macro(run_conan)
    if(MSVC)
        #can't just force BUILD_TYPE for windows, due to the way we wipe out the build settings
        #from CMAKE_CXX_FLAGS and conan can't correctly detect it
        generate_windows_conan_profile()
        set(_profile ${CMAKE_BINARY_DIR}/conan_profile.txt)
    else()
        #only windows needs to deal with runtime issues, just force the build for other platforms
        set(_buildtype ${CMAKE_BUILD_TYPE})
        if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            set(_buildtype "Release")
        endif()
    endif()

    if(APPLE)
      set(_keeprpath "KEEP_RPATHS")
    endif()

    conan_cmake_run(CONANFILE conan/conanfile.py
        BASIC_SETUP
        CMAKE_TARGETS
        NO_OUTPUT_DIRS
        BUILD missing
        ${_keeprpath}
        BUILD_TYPE ${_buildtype}
        DEBUG_PROFILE ${_profile}
        RELEASE_PROFILE ${_profile}
        RELWITHDEBINFO_PROFILE ${_profile}
        ENV "CONAN_CMAKE_BINARY_DIR_PATH=${CMAKE_BINARY_DIR}"
        )
endmacro()
