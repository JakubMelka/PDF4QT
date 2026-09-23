# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# The OCR components of the installer are generated from the built tree, so the
# installer always matches the configuration: with PDF4QT_ENABLE_OCR=OFF (or when the
# engine was not found) the preprocessor constant PDF4QT_ENABLE_OCR of Product.wxs is 0
# and the OCR block is left out, otherwise the components are generated for the engine
# adapter, the runtime libraries of the engine found in the vcpkg tree and the
# distributed OCR data files (PDF4QT_OCR_DISTRIBUTED_FILES, set by the top level
# CMakeLists.txt from the manifests of the built-in model sets).

# Namespace of the deterministic (version 5) GUIDs of the generated components. The GUID
# of a component is derived from the path of its file, so it is stable between builds.
set(PDF4QT_WIX_OCR_GUID_NAMESPACE "3C6B0F0E-6E2A-4B7B-9C2D-5E4F8A1D7B21")

# Appends a component with a single file to PDF4QT_WIX_OCR_COMPONENTS.
#   RELATIVE_PATH - path of the file relative to the installation directory, with '/'
#   DIRECTORY_ID  - WiX identifier of the directory of the file
function(pdf4qt_wix_add_ocr_component RELATIVE_PATH DIRECTORY_ID)
    string(MAKE_C_IDENTIFIER "${RELATIVE_PATH}" IDENTIFIER)
    # A WiX identifier has at most 72 characters
    string(LENGTH "${IDENTIFIER}" IDENTIFIER_LENGTH)
    if(IDENTIFIER_LENGTH GREATER 60)
        string(SHA1 IDENTIFIER_HASH "${RELATIVE_PATH}")
        string(SUBSTRING "${IDENTIFIER_HASH}" 0 16 IDENTIFIER_HASH)
        string(SUBSTRING "${IDENTIFIER}" 0 40 IDENTIFIER)
        set(IDENTIFIER "${IDENTIFIER}_${IDENTIFIER_HASH}")
    endif()
    string(UUID GUID NAMESPACE ${PDF4QT_WIX_OCR_GUID_NAMESPACE} NAME "${RELATIVE_PATH}" TYPE SHA1 UPPER)
    string(REPLACE "/" "\\" NATIVE_PATH "${RELATIVE_PATH}")
    string(APPEND PDF4QT_WIX_OCR_COMPONENTS
           "      <Component Id=\"cmpocr_${IDENTIFIER}\" Directory=\"${DIRECTORY_ID}\" Guid=\"{${GUID}}\">\n"
           "        <File Id=\"filocr_${IDENTIFIER}\" KeyPath=\"yes\" Source=\"$(var.MyInstallDir)\\${NATIVE_PATH}\" />\n"
           "      </Component>\n")
    set(PDF4QT_WIX_OCR_COMPONENTS "${PDF4QT_WIX_OCR_COMPONENTS}" PARENT_SCOPE)
endfunction()

# Fills PDF4QT_WIX_OCR_COMPONENTS and PDF4QT_WIX_OCR_DIRECTORIES
function(pdf4qt_wix_generate_ocr_components)
    set(PDF4QT_WIX_OCR_COMPONENTS "")
    set(PDF4QT_WIX_OCR_DIRECTORIES "")
    set(PDF4QT_WIX_OCR_RUNTIME_LIBRARIES "")

    # Engine adapter built by this project
    pdf4qt_wix_add_ocr_component("Pdf4QtOcrTesseract.dll" INSTALLFOLDER)

    # Runtime libraries of the engine: Tesseract, Leptonica and the libraries they load
    # which are not carried by the installer without OCR (libarchive, curl and their
    # compression libraries, the image formats of Leptonica). The names carry version
    # numbers, so they are taken from the built vcpkg tree.
    set(PDF4QT_WIX_OCR_VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    file(GLOB PDF4QT_WIX_OCR_RUNTIME_LIBRARIES RELATIVE "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/tesseract*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/leptonica*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/archive*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/libcurl*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/gif*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/tiff*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/libwebp.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/libwebpmux.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/libsharpyuv*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/liblzma*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/lz4*.dll"
         "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/zstd*.dll")
    list(SORT PDF4QT_WIX_OCR_RUNTIME_LIBRARIES)

    set(PDF4QT_WIX_OCR_ENGINE_LIBRARIES "${PDF4QT_WIX_OCR_RUNTIME_LIBRARIES}")
    list(FILTER PDF4QT_WIX_OCR_ENGINE_LIBRARIES INCLUDE REGEX "^(tesseract|leptonica)")
    if(NOT PDF4QT_WIX_OCR_ENGINE_LIBRARIES)
        message(${PDF4QT_OCR_PROBLEM_SEVERITY} "The runtime libraries of Tesseract and Leptonica were not found in ${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}, the installer would not carry the OCR engine.")
    endif()

    foreach(PDF4QT_WIX_OCR_LIBRARY IN LISTS PDF4QT_WIX_OCR_RUNTIME_LIBRARIES)
        pdf4qt_wix_add_ocr_component("${PDF4QT_WIX_OCR_LIBRARY}" INSTALLFOLDER)
    endforeach()

    # Verification against the import tables: every library loaded by the engine from
    # the vcpkg tree must be carried by the installer, either by the OCR block or by
    # the components of the template. The names are compared without case.
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH PDF4QT_WIX_COMPILER_DIR)
    find_program(PDF4QT_WIX_DUMPBIN dumpbin HINTS "${PDF4QT_WIX_COMPILER_DIR}")
    if(PDF4QT_WIX_DUMPBIN AND PDF4QT_WIX_OCR_ENGINE_LIBRARIES)
        file(GLOB PDF4QT_WIX_VCPKG_LIBRARIES RELATIVE "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}" "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/*.dll")
        string(TOLOWER "${PDF4QT_WIX_VCPKG_LIBRARIES}" PDF4QT_WIX_VCPKG_LIBRARIES_LOWER)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Product.wxs.in" PDF4QT_WIX_TEMPLATE)
        string(TOLOWER "${PDF4QT_WIX_TEMPLATE}" PDF4QT_WIX_TEMPLATE)
        string(TOLOWER "${PDF4QT_WIX_OCR_RUNTIME_LIBRARIES}" PDF4QT_WIX_OCR_RUNTIME_LIBRARIES_LOWER)

        set(PDF4QT_WIX_OCR_QUEUE "${PDF4QT_WIX_OCR_ENGINE_LIBRARIES}")
        set(PDF4QT_WIX_OCR_CLOSURE "")
        set(PDF4QT_WIX_OCR_NOT_CARRIED "")
        while(PDF4QT_WIX_OCR_QUEUE)
            list(POP_FRONT PDF4QT_WIX_OCR_QUEUE PDF4QT_WIX_OCR_LIBRARY)
            string(TOLOWER "${PDF4QT_WIX_OCR_LIBRARY}" PDF4QT_WIX_OCR_LIBRARY_LOWER)
            if(PDF4QT_WIX_OCR_LIBRARY_LOWER IN_LIST PDF4QT_WIX_OCR_CLOSURE)
                continue()
            endif()
            list(APPEND PDF4QT_WIX_OCR_CLOSURE "${PDF4QT_WIX_OCR_LIBRARY_LOWER}")

            if(NOT PDF4QT_WIX_OCR_LIBRARY_LOWER IN_LIST PDF4QT_WIX_OCR_RUNTIME_LIBRARIES_LOWER)
                string(FIND "${PDF4QT_WIX_TEMPLATE}" "\\${PDF4QT_WIX_OCR_LIBRARY_LOWER}\"" PDF4QT_WIX_OCR_TEMPLATE_POSITION)
                if(PDF4QT_WIX_OCR_TEMPLATE_POSITION EQUAL -1)
                    list(APPEND PDF4QT_WIX_OCR_NOT_CARRIED "${PDF4QT_WIX_OCR_LIBRARY}")
                endif()
            endif()

            execute_process(COMMAND "${PDF4QT_WIX_DUMPBIN}" /nologo /dependents "${PDF4QT_WIX_OCR_VCPKG_BIN_DIR}/${PDF4QT_WIX_OCR_LIBRARY}"
                            OUTPUT_VARIABLE PDF4QT_WIX_OCR_IMPORTS
                            ERROR_QUIET
                            RESULT_VARIABLE PDF4QT_WIX_OCR_DUMPBIN_RESULT)
            if(NOT PDF4QT_WIX_OCR_DUMPBIN_RESULT EQUAL 0)
                message(WARNING "dumpbin failed for ${PDF4QT_WIX_OCR_LIBRARY}, the runtime libraries of the OCR engine in the installer are not verified.")
                break()
            endif()
            string(REGEX MATCHALL "[A-Za-z0-9_.+-]+\\.[Dd][Ll][Ll]" PDF4QT_WIX_OCR_IMPORTS "${PDF4QT_WIX_OCR_IMPORTS}")
            foreach(PDF4QT_WIX_OCR_IMPORT IN LISTS PDF4QT_WIX_OCR_IMPORTS)
                string(TOLOWER "${PDF4QT_WIX_OCR_IMPORT}" PDF4QT_WIX_OCR_IMPORT_LOWER)
                if(PDF4QT_WIX_OCR_IMPORT_LOWER IN_LIST PDF4QT_WIX_VCPKG_LIBRARIES_LOWER)
                    list(APPEND PDF4QT_WIX_OCR_QUEUE "${PDF4QT_WIX_OCR_IMPORT}")
                endif()
            endforeach()
        endwhile()

        if(PDF4QT_WIX_OCR_NOT_CARRIED)
            list(JOIN PDF4QT_WIX_OCR_NOT_CARRIED ", " PDF4QT_WIX_OCR_NOT_CARRIED_TEXT)
            message(${PDF4QT_OCR_PROBLEM_SEVERITY} "The OCR engine loads libraries which are not carried by the installer: ${PDF4QT_WIX_OCR_NOT_CARRIED_TEXT}. Add them to the OCR runtime libraries in WixInstaller/CMakeLists.txt.")
        endif()
    elseif(NOT PDF4QT_WIX_DUMPBIN)
        message(STATUS "dumpbin was not found, the runtime libraries of the OCR engine in the installer are not verified against the import tables.")
    endif()

    # Distributed OCR data files (manifests, license texts, models); the directories
    # are derived from the paths and declared each in its own fragment
    set(PDF4QT_WIX_OCR_DATA_DIR "${PDF4QT_OCR_DATA_RELATIVE_PATH}")
    set(PDF4QT_WIX_OCR_DECLARED_DIRECTORIES "")
    foreach(PDF4QT_WIX_OCR_FILE IN LISTS PDF4QT_OCR_DISTRIBUTED_FILES)
        set(PDF4QT_WIX_OCR_FILE_PATH "${PDF4QT_WIX_OCR_DATA_DIR}/${PDF4QT_WIX_OCR_FILE}")
        cmake_path(GET PDF4QT_WIX_OCR_FILE_PATH PARENT_PATH PDF4QT_WIX_OCR_FILE_DIRECTORY)

        # Declare the directory and its parents up to the installation directory
        set(PDF4QT_WIX_OCR_DIRECTORY "${PDF4QT_WIX_OCR_FILE_DIRECTORY}")
        set(PDF4QT_WIX_OCR_PENDING_DIRECTORIES "")
        while(NOT PDF4QT_WIX_OCR_DIRECTORY STREQUAL "" AND NOT PDF4QT_WIX_OCR_DIRECTORY IN_LIST PDF4QT_WIX_OCR_DECLARED_DIRECTORIES)
            list(PREPEND PDF4QT_WIX_OCR_PENDING_DIRECTORIES "${PDF4QT_WIX_OCR_DIRECTORY}")
            cmake_path(GET PDF4QT_WIX_OCR_DIRECTORY PARENT_PATH PDF4QT_WIX_OCR_DIRECTORY)
        endwhile()
        foreach(PDF4QT_WIX_OCR_DIRECTORY IN LISTS PDF4QT_WIX_OCR_PENDING_DIRECTORIES)
            cmake_path(GET PDF4QT_WIX_OCR_DIRECTORY PARENT_PATH PDF4QT_WIX_OCR_PARENT)
            cmake_path(GET PDF4QT_WIX_OCR_DIRECTORY FILENAME PDF4QT_WIX_OCR_DIRECTORY_NAME)
            string(MAKE_C_IDENTIFIER "${PDF4QT_WIX_OCR_DIRECTORY}" PDF4QT_WIX_OCR_DIRECTORY_ID)
            if(PDF4QT_WIX_OCR_PARENT STREQUAL "")
                set(PDF4QT_WIX_OCR_PARENT_ID INSTALLFOLDER)
            else()
                string(MAKE_C_IDENTIFIER "${PDF4QT_WIX_OCR_PARENT}" PDF4QT_WIX_OCR_PARENT_ID)
                set(PDF4QT_WIX_OCR_PARENT_ID "dir_${PDF4QT_WIX_OCR_PARENT_ID}")
            endif()
            string(APPEND PDF4QT_WIX_OCR_DIRECTORIES
                   "  <Fragment>\n"
                   "    <DirectoryRef Id=\"${PDF4QT_WIX_OCR_PARENT_ID}\">\n"
                   "      <Directory Id=\"dir_${PDF4QT_WIX_OCR_DIRECTORY_ID}\" Name=\"${PDF4QT_WIX_OCR_DIRECTORY_NAME}\" />\n"
                   "    </DirectoryRef>\n"
                   "  </Fragment>\n")
            list(APPEND PDF4QT_WIX_OCR_DECLARED_DIRECTORIES "${PDF4QT_WIX_OCR_DIRECTORY}")
        endforeach()

        string(MAKE_C_IDENTIFIER "${PDF4QT_WIX_OCR_FILE_DIRECTORY}" PDF4QT_WIX_OCR_DIRECTORY_ID)
        pdf4qt_wix_add_ocr_component("${PDF4QT_WIX_OCR_FILE_PATH}" "dir_${PDF4QT_WIX_OCR_DIRECTORY_ID}")
    endforeach()

    set(PDF4QT_WIX_OCR_COMPONENTS "${PDF4QT_WIX_OCR_COMPONENTS}" PARENT_SCOPE)
    set(PDF4QT_WIX_OCR_DIRECTORIES "${PDF4QT_WIX_OCR_DIRECTORIES}" PARENT_SCOPE)
endfunction()

if(WIN32 AND MSVC AND PDF4QT_INSTALL_PREPARE_WIX_INSTALLER)
    if(PDF4QT_INSTALL_TO_USR)
        file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}/usr/bin" WIX_INSTALL_PREFIX)
    else()
        file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}/bin" WIX_INSTALL_PREFIX)
    endif()
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/WixInstaller" PDF4QT_WIX_DIR)

    set(PDF4QT_WIX_OCR 0)
    set(PDF4QT_WIX_OCR_COMPONENTS "")
    set(PDF4QT_WIX_OCR_DIRECTORIES "")
    if(PDF4QT_ENABLE_OCR)
        set(PDF4QT_WIX_OCR 1)
        pdf4qt_wix_generate_ocr_components()
    endif()

    configure_file(PDF4QT.sln.in PDF4QT.sln)
    configure_file(PDF4QT.wixproj.in PDF4QT.wixproj)
    configure_file(Product.wxs.in Product.wxs)
    configure_file(createInstaller.ps1.in createInstaller.ps1)
    configure_file(explic.rtf explic.rtf)
endif()
