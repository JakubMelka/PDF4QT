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

# PDF4QT - selection of an OpenSSL 3.x installation
# --------------------------------------------------
# The sources are written against the OpenSSL 3.x API and use interfaces that
# OpenSSL 4.x has removed: the low-level one-shot MD5/SHA/RC4/AES entry points
# and their *_DIGEST_LENGTH constants (<openssl/md5.h>, <openssl/sha.h>,
# <openssl/rc4.h>, <openssl/aes.h>), the low-level RSA interface
# RSA_new/RSA_free/RSA_size/RSA_generate_key_ex/RSA_public_decrypt/RSA_verify
# with EVP_PKEY_set1_RSA/EVP_PKEY_get1_RSA and BN_new/BN_free/BN_set_word
# (<openssl/rsa.h>), the mutable X509_getm_notBefore/X509_getm_notAfter, and
# X509_get_subject_name()/X509_get_serialNumber(), which became const in 4.x -
# that last one is what people hit first, as "invalid conversion from 'const
# X509_NAME*' to 'X509_NAME*'".  Porting all of it to 4.x would mean rewriting
# the PDF security handlers and the signature/timestamping code to EVP_PKEY /
# EVP_Digest, so an OpenSSL 3.x is what this build has to target.
#
# What this file does
# -------------------
# pdf4qt_setup_openssl3() is called from the top level before the subdirectories
# are read.  It resolves the OpenSSL the build will use and puts the dependency
# into PDF4QT_SSL_DEPS, which the link lines of Pdf4QtLibCore and of the unit
# tests use in place of OpenSSL::SSL OpenSSL::Crypto.
#
#   * When the OpenSSL CMake already found is a 3.x - headers and libraries -
#     nothing is searched and nothing is overridden: the usual OpenSSL::SSL /
#     OpenSSL::Crypto targets are used.  A vcpkg installation or a system
#     OpenSSL picked with the standard -DOPENSSL_ROOT_DIR=... of FindOpenSSL
#     therefore keeps working exactly as it did before, and CI links the
#     OpenSSL it was already linking rather than an absolute path found here.
#     The version of the headers decides, because that is what the compiler
#     sees; it is read out of opensslv.h.
#   * Only when those headers are not 3.x does the search run, for an OpenSSL
#     3 kept apart from the system one: what the dynamic loader cache reports
#     (ldconfig -p knows every .so and where it really is), the usual layouts
#     (/usr/include/openssl-3 with /usr/lib*/openssl-3, /usr/local, /opt) and
#     the header directory matching the library directory that was found.  The
#     3.x headers are handed to the compiler with -I, which wins over
#     /usr/include, and the 3.x libraries are linked by absolute path.  A run
#     time search path is added only for a directory the loader does not
#     already know about.
#   * A directory is accepted only when it really holds an OpenSSL 3: the
#     library file name has to be libssl.so / libssl.so.3 / libssl-3-x64.dll,
#     which rejects NSS's libssl3.so and Kerberos's libk5crypto.so.3, and
#     opensslv.h has to report the 3.x series.
#   * When no OpenSSL 3 can be found, configuration stops with a message that
#     lists every directory it tried and why each was rejected, instead of
#     failing later with pages of compile errors.
#
# Cache variables (all optional; the search needs none of them):
#
#   PDF4QT_OPENSSL3_INCLUDE_DIRS   directories holding the 3.x headers
#   PDF4QT_OPENSSL3_LIB_DIRS       directories holding libssl.so.3 / libcrypto.so.3
#   PDF4QT_OPENSSL3_SEARCH_ROOTS   prefixes to search (default /usr /usr/local /opt)
#
# An OpenSSL installation selected with the standard -DOPENSSL_ROOT_DIR=... of
# FindOpenSSL needs none of these: find_package(OpenSSL) honours it and the
# first case above takes it.
#
# Header layouts understood in PDF4QT_OPENSSL3_INCLUDE_DIRS and in the
# directories the search finds:
#
#   <dir>/openssl/*.h            the usual layout, -I <dir>
#   <dir>/include/openssl/*.h    a prefix was given instead of an include dir
#   <dir>/*.h                    headers loose in the directory; a <openssl/...>
#                                compatibility tree is generated in the build
#                                directory so that -I can be used

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# The major version out of a version string ("3.5.6", "OpenSSL 3.5.6 ...") or
# out of a library file name ("libssl.so.3", "libssl-3-x64.dll").  Empty when
# the name carries no version at all.
function(pdf4qt_ssl_major_version _out _value)
    set(_v "${_value}")
    if(_v MATCHES "([0-9]+)\\.([0-9]+)")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    if(_v MATCHES "so\\.([0-9]+)")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    if(_v MATCHES "-([0-9]+)-")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    set(${_out} "" PARENT_SCOPE)
endfunction()

# The major version out of an opensslv.h.  OpenSSL 3.x synthesizes
# OPENSSL_VERSION_NUMBER from OPENSSL_VERSION_MAJOR, sometimes with a
# backslash line continuation, and the file also carries comments that merely
# mention those macro names - so a value has to follow the name for a rule to
# apply, which is what keeps a comment from being read as the definition.
function(pdf4qt_ssl_header_major _out _file)
    if(NOT _file OR NOT EXISTS "${_file}")
        set(${_out} "" PARENT_SCOPE)
        return()
    endif()
    file(READ "${_file}" _content)
    if(_content MATCHES "OPENSSL_VERSION_NUMBER[ \t\r\n\\]*0x([0-9a-fA-F])[0-9a-fA-F]*")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    if(_content MATCHES "OPENSSL_VERSION_MAJOR[ \t\r\n\\]*([0-9]+)")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    if(_content MATCHES "OPENSSL_VERSION_TEXT[ \t\r\n\\]*\"?([0-9]+)\\.([0-9]+)")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    set(${_out} "" PARENT_SCOPE)
endfunction()

# Up to four lines of the file that mention OPENSSL_VERSION, so a version that
# could not be read is shown rather than hidden.
function(pdf4qt_ssl_version_lines _out _file)
    set(_picked "")
    if(EXISTS "${_file}")
        file(READ "${_file}" _c)
        string(REPLACE "\n" ";" _ls "${_c}")
        foreach(_l IN LISTS _ls)
            if(_l MATCHES "OPENSSL_VERSION")
                string(STRIP "${_l}" _l)
                list(APPEND _picked "${_l}")
            endif()
        endforeach()
    endif()
    list(LENGTH _picked _n)
    if(_n GREATER 4)
        list(SUBLIST _picked 0 4 _picked)
    endif()
    set(${_out} "${_picked}" PARENT_SCOPE)
endfunction()

# True only for a real OpenSSL library name: libssl.so, libssl.so.3,
# libssl.so.3.5.6, libssl-3-x64.dll.  NSS's libssl3.so and Kerberos's
# libk5crypto.so.3 are rejected - a loose *ssl*.so* glob used to accept them
# and leave the right choice to the sort order.
function(pdf4qt_ssl_own_library _out _name _file)
    if(_file MATCHES "/([^/]+)$")
        set(_base "${CMAKE_MATCH_1}")
    else()
        set(_base "${_file}")
    endif()
    set(_ok "")
    if(_base MATCHES "^lib${_name}\\.so(\\.[0-9][0-9.]*)?$")
        set(_ok "1")
    elseif(_base MATCHES "^lib${_name}(-[0-9][A-Za-z0-9\\-]*)?\\.(dll|lib)$")
        set(_ok "1")
    endif()
    set(${_out} "${_ok}" PARENT_SCOPE)
endfunction()

# Finds one OpenSSL 3 library among _dirs.  _name is "ssl" or "crypto".
# _out_lib gets the absolute path, _out_dir the directory it came from.  A
# candidate is accepted only when its file name is a genuine OpenSSL library
# (pdf4qt_ssl_own_library) and, when the name carries a version, that version
# is the 3.x series - which is what keeps a libssl.so.4 out.  A name without a
# version is used only as a fallback and reported.
function(pdf4qt_ssl_find_library _out_lib _out_dir _name _dirs)
    set(_chosen "")
    set(_chosen_dir "")
    set(_fallback "")
    set(_fallback_dir "")
    set(_rejected "")

    foreach(_dir IN LISTS _dirs)
        if(NOT IS_DIRECTORY "${_dir}")
            continue()
        endif()
        # Anchored at "lib<name>": NSS's libssl3.so and Kerberos's
        # libk5crypto.so.3 are not even candidates any more.
        file(GLOB _shared "${_dir}/lib${_name}*")
        foreach(_lib IN LISTS _shared)
            pdf4qt_ssl_own_library(_own "${_name}" "${_lib}")
            if(NOT _own)
                list(APPEND _rejected "${_lib}")
                continue()
            endif()
            pdf4qt_ssl_major_version(_major "${_lib}")
            if(_major STREQUAL "3")
                if(NOT _chosen)
                    set(_chosen "${_lib}")
                    set(_chosen_dir "${_dir}")
                endif()
            elseif(_major STREQUAL "")
                if(NOT _fallback)
                    set(_fallback "${_lib}")
                    set(_fallback_dir "${_dir}")
                endif()
            endif()
        endforeach()
    endforeach()

    if(NOT _chosen)
        set(_chosen "${_fallback}")
        set(_chosen_dir "${_fallback_dir}")
        if(_chosen)
            message(WARNING "OpenSSL library '${_chosen}' carries no version in its file name - assuming it is the wanted OpenSSL 3.")
        endif()
    endif()
    if(_chosen AND _rejected)
        list(REMOVE_DUPLICATES _rejected)
        message(STATUS "PDF4QT: OpenSSL '${_chosen}' was chosen; other files matching lib${_name}* were not accepted: ${_rejected}")
    endif()

    set(${_out_lib} "${_chosen}" PARENT_SCOPE)
    set(${_out_dir} "${_chosen_dir}" PARENT_SCOPE)
endfunction()

# The directories the dynamic loader cache reports for the OpenSSL 3
# libraries, plus the usual names under _roots.  _out_dirs receives the
# directories, _out_text the whole ldconfig output, which is later used to
# tell whether a library directory is already searched at run time.
function(pdf4qt_ssl_lib_candidates _out_dirs _out_text _roots)
    set(_c "")
    set(_txt "")
    execute_process(COMMAND ldconfig -p OUTPUT_VARIABLE _txt ERROR_VARIABLE _err TIMEOUT 20)
    if(_txt)
        string(REPLACE "\n" ";" _lines "${_txt}")
        foreach(_line IN LISTS _lines)
            # "libssl.so.3 (libc6,x86-64)  => /usr/lib/x86_64-linux-gnu/libssl.so.3"
            if(_line MATCHES "^[ \t]*(lib(ssl|crypto)\\.so[^ \t]*)[ \t]*[^ \t]*[ \t]*=>[ \t]*(/[^ \t]+)")
                set(_path "${CMAKE_MATCH_3}")
                if(_path MATCHES "(.*)/[^/]+$")
                    list(APPEND _c "${CMAKE_MATCH_1}")
                endif()
            endif()
        endforeach()
    endif()
    foreach(_root IN LISTS _roots)
        file(GLOB _g "${_root}/lib*/openssl*")
        list(APPEND _c ${_g})
    endforeach()
    list(REMOVE_DUPLICATES _c)
    set(${_out_dirs} "${_c}" PARENT_SCOPE)
    set(${_out_text} "${_txt}" PARENT_SCOPE)
endfunction()

# Header directories to try: the directory that pairs with the library
# directory that was found (a package installing its libraries as <p>/lib/<name>
# keeps the headers at <p>/include/<name>), the usual names, and any
# opensslv.h below <root>/include/openssl*.  _skip is the include directory
# CMake already reported: on the systems this is about it holds the OpenSSL 4
# that must not be used, and putting it back would only add noise.
function(pdf4qt_ssl_header_candidates _out _lib_dirs _roots _skip)
    set(_c "")
    foreach(_d IN LISTS _lib_dirs)
        if(_d MATCHES "^(.*)/(lib|lib32|lib64)/([^/]+)/?$")
            list(APPEND _c "${CMAKE_MATCH_1}/include/${CMAKE_MATCH_3}")
        endif()
        if(_d MATCHES "/([^/]+)/?$")
            list(APPEND _c "/usr/include/${CMAKE_MATCH_1}" "/usr/local/include/${CMAKE_MATCH_1}")
        endif()
    endforeach()
    foreach(_root IN LISTS _roots)
        file(GLOB _g "${_root}/include*/openssl*")
        foreach(_d IN LISTS _g)
            if(_d MATCHES "/([^/]+)/?$" AND NOT "${CMAKE_MATCH_1}" STREQUAL "openssl")
                list(APPEND _c "${_d}")
            endif()
        endforeach()
        file(GLOB_RECURSE _deep LIST_DIRECTORIES false "${_root}/include/openssl[0-9a-z\\-]*/opensslv.h")
        foreach(_hdr IN LISTS _deep)
            if(_hdr MATCHES "(.*)/openssl[0-9a-z\\-]*/opensslv\\.h")
                list(APPEND _c "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()
    if(_skip)
        list(REMOVE_ITEM _c "${_skip}")
    endif()
    list(REMOVE_DUPLICATES _c)
    set(${_out} "${_c}" PARENT_SCOPE)
endfunction()

# Picks a header directory out of _candidates.  _out_inc receives what to add
# to the compiler command line; _out_trace gets, for every candidate, what was
# found there, so a failure can be reported usefully.  Headers loose in a
# directory get a generated <openssl/...> tree, because -I of that directory
# alone cannot resolve "#include <openssl/x509.h>".
function(pdf4qt_ssl_find_headers _out_inc _candidates _out_trace)
    set(_trace "")
    foreach(_dir IN LISTS _candidates)
        if(NOT IS_DIRECTORY "${_dir}")
            list(APPEND _trace "${_dir}: not a directory")
            continue()
        endif()
        if(EXISTS "${_dir}/openssl/opensslv.h")
            set(_hdr "${_dir}/openssl/opensslv.h")
            set(_inc "${_dir}")
        elseif(EXISTS "${_dir}/include/openssl/opensslv.h")
            set(_hdr "${_dir}/include/openssl/opensslv.h")
            set(_inc "${_dir}/include")
        elseif(EXISTS "${_dir}/opensslv.h")
            set(_hdr "${_dir}/opensslv.h")
            set(_inc "")
        else()
            list(APPEND _trace "${_dir}: a directory, but no opensslv.h in it (looked for '<dir>/openssl/opensslv.h', '<dir>/include/openssl/opensslv.h' and '<dir>/opensslv.h')")
            continue()
        endif()

        pdf4qt_ssl_header_major(_major "${_hdr}")
        if(NOT _major STREQUAL "3")
            pdf4qt_ssl_version_lines(_vlines "${_hdr}")
            list(APPEND _trace "${_dir}: has headers, but ${_hdr} does not report the 3.x series (version read: '${_major}'; the lines mentioning OPENSSL_VERSION in that file are: ${_vlines})")
            continue()
        endif()

        if(_inc)
            set(${_out_inc} "${_inc}" PARENT_SCOPE)
            set(${_out_trace} "${_trace}" PARENT_SCOPE)
            return()
        endif()

        set(_parent "${CMAKE_BINARY_DIR}/pdf4qt_openssl3_include")
        file(REMOVE_RECURSE "${_parent}")
        file(GLOB _items "${_dir}/*")
        file(COPY ${_items} DESTINATION "${_parent}/openssl")
        message(STATUS "PDF4QT: OpenSSL 3 headers found loose in '${_dir}'; generated a <openssl/...> include tree at '${_parent}/openssl'.")
        set(${_out_inc} "${_parent}" PARENT_SCOPE)
        set(${_out_trace} "${_trace}" PARENT_SCOPE)
        return()
    endforeach()
    set(${_out_inc} "" PARENT_SCOPE)
    set(${_out_trace} "${_trace}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# pdf4qt_setup_openssl3()
# ---------------------------------------------------------------------------
function(pdf4qt_setup_openssl3)
    if(NOT PDF4QT_OPENSSL3_SEARCH_ROOTS)
        set(PDF4QT_OPENSSL3_SEARCH_ROOTS "/usr;/usr/local;/opt")
    endif()

    # What CMake resolved, which is also what the standard -DOPENSSL_ROOT_DIR=...
    # of FindOpenSSL selects.
    find_package(OpenSSL QUIET)
    set(_sys_hdr "${OPENSSL_INCLUDE_DIR}")
    set(_sys_ssl "${OPENSSL_SSL_LIBRARY}")
    set(_sys_crypto "${OPENSSL_CRYPTO_LIBRARY}")

    set(_hdr_major "")
    if(_sys_hdr)
        pdf4qt_ssl_header_major(_hdr_major "${_sys_hdr}/openssl/opensslv.h")
        if(NOT _hdr_major)
            pdf4qt_ssl_header_major(_hdr_major "${_sys_hdr}/opensslv.h")
        endif()
    endif()
    pdf4qt_ssl_major_version(_ssl_major "${_sys_ssl}")
    pdf4qt_ssl_major_version(_crypto_major "${_sys_crypto}")

    # -----------------------------------------------------------------------
    # CMake's OpenSSL is already a 3.x: keep its targets, search nothing.
    # Searching here would replace a vcpkg installation or a system OpenSSL
    # chosen with -DOPENSSL_ROOT_DIR by the system library linked by absolute
    # path plus a run-time path, which is what must not happen.
    # -----------------------------------------------------------------------
    if(_hdr_major STREQUAL "3" AND _ssl_major STREQUAL "3" AND _crypto_major STREQUAL "3")
        message(STATUS "PDF4QT: using the OpenSSL ${OPENSSL_VERSION} CMake found (headers ${_sys_hdr}); nothing is searched or overridden.")
        set(PDF4QT_SSL_DEPS OpenSSL::SSL OpenSSL::Crypto PARENT_SCOPE)
        return()
    endif()

    set(_inc_req ${PDF4QT_OPENSSL3_INCLUDE_DIRS})
    set(_lib_req ${PDF4QT_OPENSSL3_LIB_DIRS})

    set(_inc "")
    set(_ssl_lib "")
    set(_ssl_lib_dir "")
    set(_crypto_lib "")
    set(_crypto_lib_dir "")
    set(_hdr_trace "")
    set(_ldconfig "")

    if(_inc_req OR _lib_req)
        # An explicit request wins over the search.
        if(_inc_req)
            pdf4qt_ssl_find_headers(_inc "${_inc_req}" _hdr_trace)
            if(NOT _inc)
                message(FATAL_ERROR
                    "No OpenSSL 3 headers found in PDF4QT_OPENSSL3_INCLUDE_DIRS (searched: ${_inc_req}). "
                    "What was found in those directories: ${_hdr_trace} ")
            endif()
        endif()
        if(_lib_req)
            pdf4qt_ssl_find_library(_ssl_lib _ssl_lib_dir ssl "${_lib_req}")
            pdf4qt_ssl_find_library(_crypto_lib _crypto_lib_dir crypto "${_lib_req}")
            if(NOT _ssl_lib OR NOT _crypto_lib)
                message(FATAL_ERROR
                    "An OpenSSL 3 library is missing in PDF4QT_OPENSSL3_LIB_DIRS (searched: ${_lib_req}). "
                    "Both libssl and libcrypto are needed; look for libssl.so.3 and libcrypto.so.3 "
                    "(for example with: ls -1 /usr/lib/openssl-3 | grep '\\.so') and pass that directory.")
            endif()
        endif()
    else()
        # -------------------------------------------------------------------
        # The system headers are not 3.x, so look for an OpenSSL 3 kept apart
        # from them: the loader cache first, since it knows every library and
        # where it really is.
        # -------------------------------------------------------------------
        pdf4qt_ssl_lib_candidates(_lib_cands _ldconfig "${PDF4QT_OPENSSL3_SEARCH_ROOTS}")
        pdf4qt_ssl_find_library(_ssl_lib _ssl_lib_dir ssl "${_lib_cands}")
        pdf4qt_ssl_find_library(_crypto_lib _crypto_lib_dir crypto "${_lib_cands}")

        set(_hint_dirs "")
        if(_ssl_lib_dir)
            list(APPEND _hint_dirs "${_ssl_lib_dir}")
        endif()
        if(_crypto_lib_dir)
            list(APPEND _hint_dirs "${_crypto_lib_dir}")
        endif()

        pdf4qt_ssl_header_candidates(_hdr_cands "${_hint_dirs}" "${PDF4QT_OPENSSL3_SEARCH_ROOTS}" "${_sys_hdr}")
        pdf4qt_ssl_find_headers(_inc "${_hdr_cands}" _hdr_trace)
    endif()

    # -----------------------------------------------------------------------
    # Nothing 3.x at all: stop here, with the reason and the whole trace.
    # -----------------------------------------------------------------------
    if(NOT _inc)
        if(_hdr_major AND NOT _hdr_major STREQUAL "3")
            set(_why "the OpenSSL headers CMake found are from the ${_hdr_major}.x series (${_sys_hdr})")
        else()
            set(_why "no OpenSSL headers were found by CMake")
        endif()
        message(FATAL_ERROR
            "PDF4QT needs the OpenSSL 3.x API but no OpenSSL 3 headers could be found (${_why}). "
            "OpenSSL 4.x has removed the low-level MD5/RC4/AES/SHA and RSA interfaces this code uses, "
            "and made X509_get_subject_name() return a const pointer, so the sources cannot be compiled against it. "
            "What the search found in each directory it tried (headers and libraries): ${_hdr_trace} "
            "Point the build at your OpenSSL 3: for a layout /usr/include/openssl-3 + /usr/lib/openssl-3 that means "
            "cmake -DPDF4QT_OPENSSL3_INCLUDE_DIRS=/usr/include/openssl-3 -DPDF4QT_OPENSSL3_LIB_DIRS=/usr/lib/openssl-3 .. "
            "The automatic search looked under ${PDF4QT_OPENSSL3_SEARCH_ROOTS} and in the loader cache; set "
            "PDF4QT_OPENSSL3_SEARCH_ROOTS to a prefix whose include/ or lib/ holds your OpenSSL 3 if it lives elsewhere. "
            "If you passed those options and still get this message, they did not reach this cmake invocation: check "
            "that the configure command, not a wrapper or a stale cache, carries them (cmake -S <source> -B <build> -L -V "
            "| grep PDF4QT_OPENSSL3 should show them).")
    endif()

    # The headers going to the compiler must be 3.x, otherwise the failure
    # would show up much later as a wall of compile errors.
    pdf4qt_ssl_header_major(_inc_major "${_inc}/openssl/opensslv.h")
    if(NOT _inc_major)
        pdf4qt_ssl_header_major(_inc_major "${_inc}/opensslv.h")
    endif()
    if(_inc_major AND NOT _inc_major STREQUAL "3")
        message(FATAL_ERROR
            "The OpenSSL headers selected for the compiler (${_inc}) are from the ${_inc_major}.x series, not 3.x. "
            "PDF4QT cannot be compiled against them; point PDF4QT_OPENSSL3_INCLUDE_DIRS at your OpenSSL 3 headers.")
    endif()

    # -----------------------------------------------------------------------
    # The libraries: CMake's own 3.x ones win, otherwise the ones the search
    # found.
    # -----------------------------------------------------------------------
    if(_ssl_major STREQUAL "3" AND _crypto_major STREQUAL "3" AND NOT _lib_req)
        set(_ssl_lib "${_sys_ssl}")
        set(_crypto_lib "${_sys_crypto}")
        set(_ssl_lib_dir "")
        set(_crypto_lib_dir "")
    endif()

    if(NOT _ssl_lib OR NOT _crypto_lib)
        message(FATAL_ERROR
            "No OpenSSL 3 libraries (libssl.so.3 and libcrypto.so.3) could be found, although the 3.x headers "
            "(${_inc}) were located. Pass the directory that holds them: -DPDF4QT_OPENSSL3_LIB_DIRS=<that directory> "
            "(the search covered ${PDF4QT_OPENSSL3_SEARCH_ROOTS} and the loader cache).")
    endif()

    # -----------------------------------------------------------------------
    # The dependency: -I for the 3.x headers (they win over /usr/include) and
    # the 3.x libraries by absolute path.  A run-time search path is added only
    # for a directory the loader does not already know, which the loader cache
    # says.
    # -----------------------------------------------------------------------
    set(_link_items "${_ssl_lib};${_crypto_lib}")
    set(_rpath "")
    foreach(_d "${_ssl_lib_dir}" "${_crypto_lib_dir}")
        if(_d AND NOT "${_d}" IN_LIST _rpath)
            set(_known "")
            if(_ldconfig)
                string(FIND "${_ldconfig}" "${_d}/" _at)
                if(NOT _at EQUAL -1)
                    set(_known "1")
                endif()
            endif()
            if(NOT _known)
                list(APPEND _rpath "${_d}")
                list(APPEND _link_items "-Wl,-rpath,${_d}")
                message(STATUS "PDF4QT: '${_d}' is not in the loader's search path; adding -Wl,-rpath,${_d}.")
            endif()
        endif()
    endforeach()

    add_library(pdf4qt_openssl3 INTERFACE)
    target_include_directories(pdf4qt_openssl3 INTERFACE ${_inc})
    target_link_libraries(pdf4qt_openssl3 INTERFACE ${_link_items})

    if(_rpath)
        message(STATUS "PDF4QT: OpenSSL 3 headers ${_inc}; linking '${_ssl_lib}' and '${_crypto_lib}', run-time path ${_rpath}.")
    else()
        message(STATUS "PDF4QT: OpenSSL 3 headers ${_inc}; linking '${_ssl_lib}' and '${_crypto_lib}' (already in the loader's search path).")
    endif()
    set(PDF4QT_SSL_DEPS pdf4qt_openssl3 PARENT_SCOPE)
endfunction()
