# CMakeList.txt : CMake project for tflow-vstream, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.0)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("tflow-vstream")

find_package(PkgConfig REQUIRED)

# Add source to this project's executable.
add_executable (${PROJECT_NAME}
    "main.cpp"
    "tflow-vstream.cpp"
    "tflow-vstream.h"
    "tflow-buf.h"
    "tflow-buf-cli.cpp"
    "tflow-buf-cli.h"
    "tflow-ctrl-vstream.cpp"
    "tflow-ctrl-vstream.h"
    "tflow-ctrl.h"
    "tflow-ctrl.cpp"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
endif()

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET 
    glib-2.0
    gmodule-2.0
    gio-2.0
    gio-unix-2.0
)

pkg_check_modules(GLIBMM REQUIRED IMPORTED_TARGET 
    glibmm-2.4
    giomm-2.4
)

pkg_check_modules(OTHERS REQUIRED IMPORTED_TARGET
    json11
    libjpeg
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0") # Add debug information

target_include_directories(${PROJECT_NAME} PRIVATE ${GLIB_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PRIVATE ${GLIBMM_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PRIVATE ${OTHERS_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PkgConfig::GLIB)
target_link_libraries(${PROJECT_NAME} PkgConfig::GLIBMM)
target_link_libraries(${PROJECT_NAME} PkgConfig::OTHERS)

target_link_libraries(${PROJECT_NAME} -lrt)

# TODO: Add tests and install targets if needed.
