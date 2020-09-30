# CMake Utilities
Crossplatform cmake scripts to use for a new library or application.  
Provides scripts to setup these utilities:
- Conan
- clang-format
- VS analyzer
- Doxypress
- opencpp converage
- google test

## Basic Setup
1. Using submodule or subrepo clone this repo into the repo of the project
1. Add **cmakeUtils** to the **CMAKE_MODULE_PATH**
   ```CMake
   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmakeUtils")
   include(setup)
   ```
1. perform the initial setup
   ```CMake
   project(PROJECT_NAME)
   
   #after a CMake project is define, calling `setup_project()` to
   setup_project()      
   ```
1. Run cmake as normal  
    **Note:** Due to how things are packaged in Conan, multi-config (MSVC, XCode) isn't fully supported, so it is safer if CMAKE_BUILD_TYPE is defined.

## Tooling Specifics

### CMake flags
- **ENABLE_CODE_COVERAGE** defaults to **OFF**, Generates code coverage test running. Test under TEST_NAME_coverage in CTest, outputs html in coverage directory.
- **ENABLE_CODE_FORMATTING** defaults to **ON**, Disables code formatting
- **ENABLE_CODE_DOCUMENTATION** defaults to **OFF**, Generates documentation
- **MSVC_DYNAMIC_RUNTIME** defaults to **ON**, sets the MSVC runtime to /MD if **On** and to /MT if **Off**, only applicable to Windows 

### Clang-format
- use ```format_code(TARGET)``` to enable code formatting on a specific target

### Google Test
- use ```googletest_add_test("TEST_NAME" TARGET)``` to add a test to CTest under the name **TEST_NAME**

