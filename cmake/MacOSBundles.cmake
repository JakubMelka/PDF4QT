# Keep the application bundles usable both in the build tree and after installation.
set(PDF4QT_MACOS_APPLICATIONS Pdf4QtEditor Pdf4QtViewer Pdf4QtPageMaster Pdf4QtDiff Pdf4QtLaunchPad)
set(PDF4QT_MACOS_PLUGINS AudioBookPlugin DimensionsPlugin ObjectInspectorPlugin
    OutputPreviewPlugin RedactPlugin SignaturePlugin SoftProofingPlugin EditorPlugin ScannerPlugin)

find_program(PDF4QT_ICONUTIL iconutil REQUIRED)

foreach(application IN LISTS PDF4QT_MACOS_APPLICATIONS)
    set(icon_name "io.github.JakubMelka.Pdf4qt.${application}")
    if(application STREQUAL "Pdf4QtLaunchPad")
        set(icon_name "io.github.JakubMelka.Pdf4qt")
    endif()
    set(icon_dir "${CMAKE_CURRENT_BINARY_DIR}/icons/${application}.iconset")
    set(icon_file "${CMAKE_CURRENT_BINARY_DIR}/icons/${application}.icns")
    add_custom_command(OUTPUT "${icon_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${icon_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/Desktop/128x128/${icon_name}.png"
            "${icon_dir}/icon_128x128.png"
        COMMAND "${PDF4QT_ICONUTIL}" -c icns -o "${icon_file}" "${icon_dir}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Desktop/128x128/${icon_name}.png"
        VERBATIM)
    add_custom_target(${application}Icon DEPENDS "${icon_file}")
    add_dependencies(${application} ${application}Icon)
    set_source_files_properties("${icon_file}" TARGET_DIRECTORY ${application}
        PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${application} PRIVATE "${icon_file}")
    set_target_properties(${application} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.JakubMelka.Pdf4qt.${application}"
        MACOSX_BUNDLE_BUNDLE_NAME "${application}"
        MACOSX_BUNDLE_ICON_FILE "${application}.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

    set(bundle "$<TARGET_BUNDLE_DIR:${application}>")
    add_custom_target(${application}Resources ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}/Contents/Resources/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PDF4QT_QM_FILES} "${bundle}/Contents/Resources/translations"
        DEPENDS ${application} ${PDF4QT_QM_FILES}
        VERBATIM)

    set(plugin_arguments "")
    # Explicitly include plugins that macdeployqt can miss when their Qt modules
    # are used through PDF4QT's shared libraries rather than the executable.
    set(qt_plugins Qt6::QSvgPlugin)
    if(application STREQUAL "Pdf4QtEditor" OR application STREQUAL "Pdf4QtViewer")
        list(APPEND qt_plugins Qt6::QTextToSpeechDarwinPlugin)
    endif()
    foreach(plugin IN LISTS qt_plugins)
        get_target_property(plugin_type ${plugin} QT_PLUGIN_TYPE)
        add_custom_command(TARGET ${application}Resources POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}/Contents/PlugIns/${plugin_type}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${plugin}>"
                "${bundle}/Contents/PlugIns/${plugin_type}/$<TARGET_FILE_NAME:${plugin}>"
            VERBATIM)
        string(APPEND plugin_arguments
            " \"${application}.app/Contents/PlugIns/${plugin_type}/$<TARGET_FILE_NAME:${plugin}>\"")
    endforeach()
    if(application STREQUAL "Pdf4QtEditor")
        foreach(plugin IN LISTS PDF4QT_MACOS_PLUGINS)
            add_dependencies(${application}Resources ${plugin})
            add_custom_command(TARGET ${application}Resources POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}/Contents/PlugIns/pdf4qt"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${plugin}>"
                    "${bundle}/Contents/PlugIns/pdf4qt/$<TARGET_FILE_NAME:${plugin}>"
                VERBATIM)
            string(APPEND plugin_arguments
                " \"${application}.app/Contents/PlugIns/pdf4qt/$<TARGET_FILE_NAME:${plugin}>\"")
        endforeach()
    endif()

    if(PDF4QT_INSTALL_DEPENDENCIES AND PDF4QT_INSTALL_QT_DEPENDENCIES)
        # macdeployqt resolves @rpath dependencies from the installed executable.
        # These staging paths are replaced with bundle-relative paths by deployment.
        set_target_properties(${application} PROPERTIES
            INSTALL_RPATH "$<TARGET_FILE_DIR:Pdf4QtLibCore>;$<TARGET_FILE_DIR:Qt6::Core>")
        qt_generate_deploy_script(TARGET ${application} OUTPUT_SCRIPT deploy_script
            CONTENT "qt_deploy_runtime_dependencies(
                EXECUTABLE \"${application}.app\"
                ADDITIONAL_MODULES ${plugin_arguments}
                DEPLOY_TOOL_OPTIONS -codesign=-
                    \"-libpath=${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}\"
                    \"-libpath=$<TARGET_FILE_DIR:Qt6::Core>\"
            )
            include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSFixup.cmake\")
            pdf4qt_fixup_macos_bundle(\"${application}.app\")")
        install(SCRIPT "${deploy_script}")
    endif()
endforeach()
