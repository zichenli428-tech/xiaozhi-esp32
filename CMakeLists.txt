# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Force Ninja to use response files. The xiaozhi build passes hundreds of -I
# include flags to some compile/link steps, which exceeds the Windows command
# line length limit and makes ninja fail with "CreateProcess: The parameter is
# incorrect." Response files are supported on all platforms.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.4.2")
project(xiaozhi)
