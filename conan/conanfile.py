from conans import ConanFile, CMake, tools
from conans.errors import ConanInvalidConfiguration
from conans.tools import Version
import os


# this internal conanfile a modified version of the one used in the conan-center-index repo
# differences from the opensource are as follows:
# * source exported with the recipe
# * Release is changed to RelWithDebInfo
# * PDBs are not deleted
class TwitchNativeIpcConan(ConanFile):
    name = "twitch-native-ipc"
    license = "MIT"
    homepage = "https://github.com/twitchtv/twitch-native-ipc"
    url = "https://github.com/twitchtv/twitch-native-ipc"
    description = "Twitch natve ipc library"
    topics = ("twitch", "ipc")
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    generators = "cmake"
    exports = ["CMakeLists.txt"]
    requires = "libuv/1.40.0"
    build_requires = "gtest/1.10.0"

    _cmake = None

    @property
    def _source_subfolder(self):
        return "source_subfolder"

    @property
    def _build_subfolder(self):
        return "build_subfolder"

    @property
    def _compilers_min_version(self):
        return {
            "gcc": "8",
            "clang": "8",
            "apple-clang": "10",
            "Visual Studio": "15",
        }

    def export_sources(self):
        # copy the source into the subfolder dir, so things match the expectations of the opensource recipe
        dest = os.path.join(self.export_sources_folder, self._source_subfolder)
        self.copy(os.path.join("..", "libnativeipc*"), dst=dest)
        self.copy(os.path.join("..", "CMakeLists.txt"), dst=dest)
        self.copy(os.path.join("..", "cmakeUtils*"), dst=dest)
        self.copy(os.path.join("..", "LICENSE"), dst=dest)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.settings.compiler.cppstd:
            tools.check_min_cppstd(self, 17)

        min_version = self._compilers_min_version.get(str(self.settings.compiler), False)
        if min_version:
            if tools.Version(self.settings.compiler.version) < min_version:
                raise ConanInvalidConfiguration("twitch-native-ipc requires C++17")
        else:
            self.output.warn("unknown compiler, assuming C++17 support")

        if self.options.shared:
            del self.options.fPIC

    def _configure_cmake(self):
        if self._cmake:
            return self._cmake

        cmake_build_type = self.settings.build_type
        if cmake_build_type == "Release":
            cmake_build_type = "RelWithDebInfo"
        self._cmake = CMake(self, build_type=cmake_build_type)
        self._cmake.definitions["ENABLE_CODE_FORMATTING"] = False
        self._cmake.definitions["BUILD_TESTING"] = False

        # enable when automated builds have fixed the issue on windows
        #if self.settings.build_type == "Release":
        #    cmake.definitions["CMAKE_INTERPROCEDURAL_OPTIMIZATION"] = True        

        if self.settings.os == "Windows":
            self._cmake.definitions["MSVC_DYNAMIC_RUNTIME"] = self.settings.compiler.runtime in ("MD", "MDd")

        self._cmake.configure(build_folder=self._build_subfolder)
        return self._cmake

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        self.copy("LICENSE", dst="licenses", src=self._source_subfolder)
        cmake = self._configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["nativeipc"]
        if self.settings.os == "Windows" and self.options.shared:
            self.cpp_info.defines = ["NATIVEIPC_IMPORT"]

        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["m"]

        if tools.stdcpp_library(self):
            self.cpp_info.system_libs.append(tools.stdcpp_library(self))
