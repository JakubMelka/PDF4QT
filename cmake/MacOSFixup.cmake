# macdeployqt can leave build-tree LC_RPATH entries on copied non-Qt libraries.
# Remove them after deployment and refresh the affected ad-hoc signatures.
function(pdf4qt_fixup_macos_bundle bundle)
    set(bundle "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${bundle}")
    find_program(otool otool REQUIRED)
    find_program(install_name_tool install_name_tool REQUIRED)
    find_program(codesign codesign REQUIRED)
    file(GLOB_RECURSE libraries "${bundle}/Contents/Frameworks/*.dylib"
                                "${bundle}/Contents/PlugIns/*.dylib")
    foreach(library IN LISTS libraries)
        execute_process(COMMAND "${otool}" -l "${library}"
            OUTPUT_VARIABLE load_commands COMMAND_ERROR_IS_FATAL ANY)
        string(REGEX MATCHALL "cmd LC_RPATH\n[^\n]*\n[^\n]*" entries "${load_commands}")
        set(changed FALSE)
        foreach(entry IN LISTS entries)
            string(REGEX REPLACE ".*\n[ \t]*path (.*) \\(offset [0-9]+\\)" "\\1" rpath "${entry}")
            if(IS_ABSOLUTE "${rpath}")
                execute_process(COMMAND "${install_name_tool}" -delete_rpath "${rpath}" "${library}"
                    COMMAND_ERROR_IS_FATAL ANY)
                set(changed TRUE)
            endif()
        endforeach()
        if(changed)
            execute_process(COMMAND "${codesign}" --force --sign - "${library}"
                COMMAND_ERROR_IS_FATAL ANY)
        endif()
    endforeach()
    execute_process(COMMAND "${codesign}" --force --sign - "${bundle}"
        COMMAND_ERROR_IS_FATAL ANY)
endfunction()
