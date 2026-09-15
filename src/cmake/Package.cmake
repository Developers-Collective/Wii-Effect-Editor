# Install only the editor component, excluding dependency SDKs and examples.
set_target_properties(breff_editor_native PROPERTIES OUTPUT_NAME "EffectEditor")
if(APPLE)
    set_target_properties(breff_editor_native PROPERTIES
        MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME "Effect Editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.effect-editor.app"
        OUTPUT_NAME "Effect Editor")
    install(TARGETS breff_editor_native BUNDLE DESTINATION . COMPONENT EffectEditor)
    install(CODE [[
        include(BundleUtilities)
        # --prefix may be relative. BundleUtilities compares this directory
        # with resolved absolute executable paths, so normalize it first.
        get_filename_component(_editor_bundle
            "${CMAKE_INSTALL_PREFIX}/Effect Editor.app" REALPATH)
        fixup_bundle("${_editor_bundle}" "" "")
        execute_process(COMMAND codesign --force --deep --sign -
            "${_editor_bundle}" COMMAND_ERROR_IS_FATAL ANY)
    ]] COMPONENT EffectEditor)
elseif(WIN32)
    install(TARGETS breff_editor_native RUNTIME DESTINATION . COMPONENT EffectEditor)
    install(CODE "
        file(GLOB _dlls \"$<TARGET_FILE_DIR:breff_editor_native>/*.dll\")
        file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\" TYPE FILE FILES \${_dlls})
    " COMPONENT EffectEditor)
else()
    set_target_properties(breff_editor_native PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
    install(TARGETS breff_editor_native
        RUNTIME_DEPENDENCY_SET effect_editor_deps
        RUNTIME DESTINATION . COMPONENT EffectEditor)
    install(RUNTIME_DEPENDENCY_SET effect_editor_deps
        PRE_EXCLUDE_REGEXES "^linux-vdso" "^ld-linux"
        POST_EXCLUDE_REGEXES "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/"
        LIBRARY DESTINATION lib COMPONENT EffectEditor)
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/../logo.png" DESTINATION . COMPONENT EffectEditor)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/nw4r/LICENSE"
    DESTINATION licenses/nw4r COMPONENT EffectEditor)
foreach(_dependency aurora json fmt sdl3 sdl3_prebuilt dawn_prebuilt png zlib freetype imgui xxhash zstd sqlite3 tracy abseil)
    if(DEFINED ${_dependency}_SOURCE_DIR)
        file(GLOB _licenses LIST_DIRECTORIES FALSE "${${_dependency}_SOURCE_DIR}/LICENSE*" "${${_dependency}_SOURCE_DIR}/COPYING*")
        if(_licenses)
            install(FILES ${_licenses} DESTINATION "licenses/${_dependency}" COMPONENT EffectEditor)
        endif()
    endif()
endforeach()
# Dependencies declared inside Aurora have directory-scoped variables. Collect
# their license files from FetchContent's source directories as well.
file(GLOB _dependency_sources LIST_DIRECTORIES TRUE "${FETCHCONTENT_BASE_DIR}/*-src")
foreach(_source IN LISTS _dependency_sources)
    get_filename_component(_name "${_source}" NAME)
    string(REGEX REPLACE "-src$" "" _name "${_name}")
    file(GLOB _licenses LIST_DIRECTORIES FALSE "${_source}/LICENSE*" "${_source}/COPYING*" "${_source}/COPYRIGHT*")
    if(_licenses)
        install(FILES ${_licenses} DESTINATION "licenses/${_name}" COMPONENT EffectEditor)
    endif()
    if(IS_DIRECTORY "${_source}/licenses")
        install(DIRECTORY "${_source}/licenses/" DESTINATION "licenses/${_name}" COMPONENT EffectEditor)
    endif()
endforeach()

