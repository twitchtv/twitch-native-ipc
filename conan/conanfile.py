import os, json
from conans import ConanFile, CMake, tools

class TwitchnativeipcConan(ConanFile):
    name = "twitch-native-ipc"
    license = "None"
    url = "https://github.com/twitchtv/twitch-native-ipc"
    description = "Twitch Native IPC"
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = "libuv/1.38.1"
    build_requires = "gtest/1.10.0"
    options = {"shared": [True, False]}
    default_options = "shared=False", "libuv:shared=False"
    exports_sources = "../libnativeipc*", "../CMakeLists.txt", "../cmakeUtils*"

    def _configure_cmake(self):
        cmake_build_type = self.settings.build_type
        if cmake_build_type == "Release":
            cmake_build_type = "RelWithDebInfo"
        cmake = CMake(self, build_type=cmake_build_type)
        cmake.definitions["ENABLE_CODE_FORMATTING"] = False
        cmake.definitions["BUILD_TESTING"] = False
        cmake.definitions["BUILD_SHARED_LIBS"] = self.options.shared

        # enable when automated builds have fixed the issue on windows
        #if self.settings.build_type == "Release":
        #    cmake.definitions["CMAKE_INTERPROCEDURAL_OPTIMIZATION"] = True

        if self.settings.os == "Windows":
            cmake.definitions["MSVC_DYNAMIC_RUNTIME"] = True
            if self.settings.compiler.runtime in ["MT", "MTd"]:
                cmake.definitions["MSVC_DYNAMIC_RUNTIME"] = False
        
        cmake.configure()
        return cmake

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        cmake = self._configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)

            


