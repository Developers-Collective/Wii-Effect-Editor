cmake_minimum_required(VERSION 3.25)
project(Wii-Effect-Editor LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Aurora requires C++20's cooperative thread cancellation support. Check it
# before downloading/building dependencies so older libc++ gives a clear error.
include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
find_package(Threads REQUIRED)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
check_cxx_source_compiles([[
    #include <thread>
    #include <stop_token>
    int main() {
        std::jthread worker([](std::stop_token) {});
        worker.request_stop();
    }
]] EFFECT_EDITOR_HAS_JTHREAD)
cmake_pop_check_state()
if(NOT EFFECT_EDITOR_HAS_JTHREAD)
    message(FATAL_ERROR "Aurora requires std::jthread and std::stop_token. On macOS, select Xcode 26 or newer; on other platforms, use a C++20 standard library with jthread support.")
endif()

include(FetchContent)

set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)
FetchContent_Declare(aurora
    GIT_REPOSITORY https://github.com/encounter/aurora.git
    GIT_TAG d0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(aurora json)
add_subdirectory(codec)
add_subdirectory(runtime)

add_executable(breff_editor_native editor/main.cpp editor/preview_view.cpp editor/document.cpp)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../logo.png" EDITOR_LOGO_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," EDITOR_LOGO_BYTES "${EDITOR_LOGO_HEX}")
configure_file(cmake/logo.h.in generated/logo.h @ONLY)
target_include_directories(breff_editor_native PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(breff_editor_native PRIVATE breff_codec breff_runtime aurora::core aurora::gx aurora::main aurora::vi aurora::mtx nlohmann_json::nlohmann_json PNG::PNG)
if(WIN32)
    target_link_libraries(breff_editor_native PRIVATE windowscodecs ole32)
endif()

include(cmake/Package.cmake)
if(WIN32)
    include("${aurora_SOURCE_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
    aurora_copy_runtime_dlls(breff_editor_native)
    # Ship the C++ runtime with the application. A newer MSVC mutex ABI can
    # crash in an older system MSVCP140.dll before the first frame is drawn.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)
    include(InstallRequiredSystemLibraries)
    if(MSVC AND NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        # Portable MSVC layouts place the target CRT beside cl.exe.
        get_filename_component(_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(_crt IN ITEMS msvcp140.dll msvcp140_1.dll msvcp140_2.dll
                msvcp140_atomic_wait.dll msvcp140_codecvt_ids.dll
                vcruntime140.dll vcruntime140_1.dll vcruntime140_threads.dll)
            if(EXISTS "${_compiler_directory}/${_crt}")
                list(APPEND CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS "${_compiler_directory}/${_crt}")
            endif()
        endforeach()
    endif()
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET breff_editor_native POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:breff_editor_native>
            COMMAND_EXPAND_LISTS)
    endif()
endif()

