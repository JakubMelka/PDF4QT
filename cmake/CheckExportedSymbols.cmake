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

# Checks, that the shared libraries (ELF) do not export the symbols of the statically
# linked dependencies (issue #436). An exported symbol of a dependency interposes the
# same symbol of the system library loaded into the process: the system libssl loaded
# by the TLS backend of Qt called the functions of the OpenSSL linked into
# Pdf4QtLibCore and the TLS handshake crashed. Only the C interfaces are checked, the
# symbols of C++ are mangled and cannot be confused with the system libraries.
#
#   cmake -DNM=<nm> -DFILES=<file1;file2;...> -P CheckExportedSymbols.cmake

if(NOT NM)
    find_program(NM nm)
endif()
if(NOT NM)
    message(FATAL_ERROR "The program nm was not found, set the variable NM.")
endif()
if(NOT FILES)
    message(FATAL_ERROR "No file to check, set the variable FILES.")
endif()

# OpenSSL, freetype, libpng, libjpeg, openjpeg, lcms2, zlib, libtiff, leptonica, blend2d
set(PDF4QT_FORBIDDEN_SYMBOLS "^(OPENSSL_|SSL_|SSL_CTX_|TLS_|DTLS_|EVP_|X509_|X509|CRYPTO_|ERR_|BIO_|PEM_|OSSL_|i2d_|d2i_|FT_|png_|jpeg_|opj_|cms[A-Z]|inflate|deflate|crc32|adler32|TIFF|pix[A-Z]|box[A-Z]|lept_|bl[A-Z])")

set(PDF4QT_CHECK_FAILED OFF)
foreach(PDF4QT_FILE IN LISTS FILES)
    if(NOT EXISTS "${PDF4QT_FILE}")
        message(FATAL_ERROR "File ${PDF4QT_FILE} does not exist.")
    endif()

    execute_process(COMMAND "${NM}" -D --defined-only --format=posix "${PDF4QT_FILE}"
                    OUTPUT_VARIABLE PDF4QT_NM_OUTPUT
                    ERROR_VARIABLE PDF4QT_NM_ERROR
                    RESULT_VARIABLE PDF4QT_NM_RESULT)
    if(NOT PDF4QT_NM_RESULT EQUAL 0)
        message(FATAL_ERROR "${NM} failed on ${PDF4QT_FILE}: ${PDF4QT_NM_ERROR}")
    endif()

    # Format posix: "<name> <type> <value> [<size>]", the name can carry the version (name@@VERSION)
    string(REPLACE "\n" ";" PDF4QT_NM_LINES "${PDF4QT_NM_OUTPUT}")
    set(PDF4QT_EXPORTED "")
    set(PDF4QT_EXPORTED_COUNT 0)
    foreach(PDF4QT_LINE IN LISTS PDF4QT_NM_LINES)
        string(REGEX MATCH "^[^ @]+" PDF4QT_SYMBOL "${PDF4QT_LINE}")
        if(PDF4QT_SYMBOL MATCHES "${PDF4QT_FORBIDDEN_SYMBOLS}")
            math(EXPR PDF4QT_EXPORTED_COUNT "${PDF4QT_EXPORTED_COUNT} + 1")
            list(LENGTH PDF4QT_EXPORTED PDF4QT_EXPORTED_LISTED)
            if(PDF4QT_EXPORTED_LISTED LESS 10)
                list(APPEND PDF4QT_EXPORTED ${PDF4QT_SYMBOL})
            endif()
        endif()
    endforeach()

    if(PDF4QT_EXPORTED_COUNT GREATER 0)
        list(JOIN PDF4QT_EXPORTED ", " PDF4QT_EXPORTED_TEXT)
        message(SEND_ERROR "${PDF4QT_FILE} exports ${PDF4QT_EXPORTED_COUNT} symbols of the statically linked dependencies (${PDF4QT_EXPORTED_TEXT}, ...). Link it with -Wl,--exclude-libs,ALL.")
        set(PDF4QT_CHECK_FAILED ON)
    else()
        message(STATUS "${PDF4QT_FILE}: no symbol of the dependencies is exported")
    endif()
endforeach()

if(PDF4QT_CHECK_FAILED)
    message(FATAL_ERROR "Shared libraries export the symbols of the statically linked dependencies.")
endif()
