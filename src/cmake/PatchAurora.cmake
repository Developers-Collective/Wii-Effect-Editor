find_package(Git REQUIRED)

foreach(patch aurora-headless.patch aurora-renderer.patch aurora-device-sync.patch)
    set(patch_path "${CMAKE_CURRENT_LIST_DIR}/${patch}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check --ignore-whitespace "${patch_path}"
        RESULT_VARIABLE applicable OUTPUT_QUIET ERROR_QUIET)
    if(applicable EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${patch_path}"
            COMMAND_ERROR_IS_FATAL ANY)
    else()
        # Reconfiguring an existing build must also accept already applied patches.
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-whitespace "${patch_path}"
            RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "Cannot apply Aurora patch: ${patch}")
        endif()
    endif()
endforeach()
