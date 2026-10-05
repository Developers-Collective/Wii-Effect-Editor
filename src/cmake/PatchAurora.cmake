find_package(Git REQUIRED)

set(patch_directory "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/aurora-patches")
file(MAKE_DIRECTORY "${patch_directory}")

foreach(patch aurora-headless.patch aurora-renderer.patch aurora-device-sync.patch)
    # Windows checkouts may use CRLF. Git requires LF for these patch files.
    file(READ "${CMAKE_CURRENT_LIST_DIR}/${patch}" patch_content)
    string(REPLACE "\r\n" "\n" patch_content "${patch_content}")
    set(patch_path "${patch_directory}/${patch}")
    file(WRITE "${patch_path}" "${patch_content}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check --ignore-whitespace "${patch_path}"
        RESULT_VARIABLE applicable OUTPUT_QUIET ERROR_VARIABLE patch_error)
    if(applicable EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${patch_path}"
            COMMAND_ERROR_IS_FATAL ANY)
    else()
        # Reconfiguring an existing build must also accept already applied patches.
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-whitespace "${patch_path}"
            RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "Cannot apply Aurora patch: ${patch}\n${patch_error}")
        endif()
    endif()
endforeach()
