# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# PDF4QT - selection of an OpenSSL 3.x installation.
#
# Why this file exists
# --------------------
# The PDF4QT sources are written against the OpenSSL 3.x API and they use
# interfaces that OpenSSL 4.x has *removed* (they are only "deprecated" in
# 3.x, which is why they still compile there):
#
#   * the low-level one-shot algorithm interfaces: MD5(), MD5_Init/Update/
#     Final, SHA1/SHA256/SHA384/SHA512(), RC4/RC4_set_key,
#     AES_set_encrypt_key/AES_set_decrypt_key/AES_cbc_encrypt/AES_ecb_encrypt,
#     and the *_DIGEST_LENGTH constants (<openssl/md5.h>, <openssl/sha.h>,
#     <openssl/rc4.h>, <openssl/aes.h>);
#   * the low-level RSA interface: RSA_new/RSA_free/RSA_size/RSA_generate_key_ex/
#     RSA_public_decrypt/RSA_verify/RSA_F4, EVP_PKEY_set1_RSA/EVP_PKEY_get1_RSA
#     and BN_new/BN_free/BN_set_word (<openssl/rsa.h>);
#   * the mutable X509 accessors X509_getm_notBefore/X509_getm_notAfter, and
#     the non-const return of X509_get_subject_name()/X509_get_serialNumber(),
#     which became const in OpenSSL 4.x (this is the error people see first:
#     "invalid conversion from 'const X509_NAME*' to 'X509_NAME*'").
#
# Porting all of that to OpenSSL 4 would mean rewriting the PDF security
# handlers and the signature/timestamping code to EVP_PKEY/EVP_Digest, so the
# supported way to build PDF4QT is against an OpenSSL 3.x.
#
# Many systems now ship OpenSSL 4 as the system default and keep an OpenSSL 3
# in a separate location.  find_package(OpenSSL) cannot express what is needed
# here: even when it locates 3.x libraries, the compiler still takes the 4.x
# headers from /usr/include, and the build fails with the error above.
#
# This file therefore resolves the OpenSSL 3 headers and libraries itself:
#
#   * it works without any extra command line option - it auto-detects an
#     OpenSSL 3 that sits next to the system OpenSSL 4 (the usual layouts,
#     plus what ldconfig reports);
#   * it hands the compiler -I for the 3.x headers, which wins over
#     /usr/include, and links the 3.x libssl/libcrypto by absolute path,
#     adding a run-time search path so the built binaries can be run;
#   * if the headers it selected are not 3.x, or no 3.x can be found,
#     configuration stops with a message that explains what to do, instead
#     of failing later with pages of compile errors.
#
# Optional cache variables (normally not needed):
#
#   PDF4QT_OPENSSL3_INCLUDE_DIRS   directories holding the 3.x headers
#   PDF4QT_OPENSSL3_LIB_DIRS       directories holding libssl.so.3 / libcrypto.so.3
#   PDF4QT_OPENSSL3_ROOT_DIR       a prefix that has include/ and lib/ below it
#   PDF4QT_OPENSSL3_SEARCH_ROOTS   where to look for a separate OpenSSL 3
#                                  (default: /usr /usr/local /opt)
#
# Header layouts understood in PDF4QT_OPENSSL3_INCLUDE_DIRS and in the
# directories found by the search below:
#
#   <dir>/openssl/*.h        usual layout, adds -I <dir>
#   <dir>/include/openssl/*.h  a prefix was given instead of an include dir
#   <dir>/*.h                headers loose in the directory (e.g. /usr/include/openssl-3);
#                           a <build>/pdf4qt_openssl3_include_N/openssl tree is generated

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Determines the OpenSSL major version out of a version string ("3.5.6",
# "OpenSSL 3.5.6 ...") or out of a library file name ("libssl.so.3",
# "libssl-3-x64.dll").  Returns empty when it cannot be determined.
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

# Reads the OpenSSL major version out of an opensslv.h.  Two forms occur in
# practice: a literal number (OPENSSL_VERSION_NUMBER 0x30506000L) and, since
# OpenSSL 3.x, a number synthesized from OPENSSL_VERSION_MAJOR/MINOR/... with
# the definition possibly continued onto the next line with a backslash.  A
# comment that merely mentions the macro names must not be mistaken for the
# definition, which is why each rule requires the value right after the name.
function(pdf4qt_ssl_header_major _out _file)
    if(NOT _file OR NOT EXISTS "${_file}")
        set(${_out} "" PARENT_SCOPE)
        return()
    endif()
    file(READ "${_file}" _content)
    # A literal 0xMNN00PP0L, possibly after a backslash line continuation.
    if(_content MATCHES "OPENSSL_VERSION_NUMBER[ \t\r\n\\]*0x([0-9a-fA-F])[0-9a-fA-F]*")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    # Synthesised: OPENSSL_VERSION_MAJOR is the major version on its own.
    if(_content MATCHES "OPENSSL_VERSION_MAJOR[ \t\r\n\\]*([0-9]+)")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    # Last resort: the version text, "OpenSSL 3.5.6 ...".
    if(_content MATCHES "OPENSSL_VERSION_TEXT[ \t\r\n\\]*\"?([0-9]+)\\.([0-9]+)")
        set(${_out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()
    set(${_out} "" PARENT_SCOPE)
endfunction()

# Returns up to four lines of the file that mention OPENSSL_VERSION, so a
# version that could not be read can be shown instead of hidden.
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

# Finds one OpenSSL 3 library.  _name is "ssl" or "crypto"; _dirs is a list of
# directories to search.  _out_lib receives the absolute path of the chosen
# library, _out_dir the directory it was found in (used for the run-time
# search path).  Candidates whose file name reveals another major version
# than 3 are rejected, which is what keeps a libssl.so.4 from being picked up.
function(pdf4qt_ssl_find_library _out_lib _out_dir _name _dirs)
    set(_chosen "")
    set(_chosen_dir "")
    set(_fallback "")
    set(_fallback_dir "")

    foreach(_dir IN LISTS _dirs)
        if(NOT IS_DIRECTORY "${_dir}")
            continue()
        endif()
        # A leading * is deliberate: some packagers spell the library without
        # the "lib" prefix (for example ".../openssl.so.3").
        file(GLOB _shared "${_dir}/*${_name}*.so*")
        list(SORT _shared)
        foreach(_lib IN LISTS _shared)
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

    set(${_out_lib} "${_chosen}" PARENT_SCOPE)
    set(${_out_dir} "${_chosen_dir}" PARENT_SCOPE)
endfunction()

# Builds the list of directories that may hold the OpenSSL 3 headers, derived
# from the directories that hold the OpenSSL 3 libraries (a package that
# installs the libraries in /usr/lib/openssl-3 keeps the headers in
# /usr/include/openssl-3) and from the usual names.  The bare
# "<root>/include/openssl" is left out: that is the system default, which on
# the affected systems is the OpenSSL 4 we must not use.
function(pdf4qt_ssl_header_candidates _out _lib_dirs _roots)
    set(_c "")
    foreach(_d IN LISTS _lib_dirs)
        # A package that installs the libraries as <p>/lib/<name> keeps the
        # headers as <p>/include/<name>, so derive that sibling directory.
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
        # Catch-all: an opensslv.h anywhere below <root>/include/openssl...  The
        # search is bounded to that subtree, it does not walk the whole prefix.
        file(GLOB_RECURSE _deep LIST_DIRECTORIES false "${_root}/include/openssl*/opensslv.h")
        foreach(_hdr IN LISTS _deep)
            if(_hdr MATCHES "(.*)/openssl/opensslv\\.h")
                list(APPEND _c "${CMAKE_MATCH_1}")
            elseif(_hdr MATCHES "(.*)/opensslv\\.h")
                list(APPEND _c "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _c)
    set(${_out} "${_c}" PARENT_SCOPE)
endfunction()

# Finds an OpenSSL 3 header directory among _candidates and returns something
# usable as -I in _out_inc.  A layout whose headers are loose in the directory
# gets a <openssl/...> compatibility tree generated in the build directory.
# _out_trace receives, for every candidate, what was found there, so a failure
# can be reported usefully instead of a bare "nothing found".
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

        # The headers are loose in the directory, so "#include <openssl/x509.h>"
        # cannot be resolved by -I <dir>: generate a compatibility tree.
        set(_parent "${CMAKE_BINARY_DIR}/pdf4qt_openssl3_include_0")
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

# Collects the directories that may hold the OpenSSL 3 libraries: what the
# dynamic loader cache reports (the most reliable source, it knows every
# .so and where it really is), the usual names and, as a last resort, a
# bounded recursive search.
function(pdf4qt_ssl_lib_candidates _out _roots)
    set(_c "")
    execute_process(COMMAND ldconfig -p OUTPUT_VARIABLE _out_txt ERROR_VARIABLE _err)
    if(_out_txt)
        string(REPLACE "\n" ";" _lines "${_out_txt}")
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "=>[ \t]*(/[^ \t]*ssl[^ \t]*\\.so\\.3[^ \t]*)")
                set(_path "${CMAKE_MATCH_1}")
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
    set(${_out} "${_c}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# pdf4qt_setup_openssl3()
#
# Resolves the OpenSSL 3 headers and libraries and puts the target(s) that
# carry them into PDF4QT_SSL_DEPS.
# ---------------------------------------------------------------------------
function(pdf4qt_setup_openssl3)
    if(NOT PDF4QT_OPENSSL3_SEARCH_ROOTS)
        set(PDF4QT_OPENSSL3_SEARCH_ROOTS "/usr;/usr/local;/opt")
    endif()

    # -----------------------------------------------------------------------
    # Step 1: what does CMake's own discovery report?  Not REQUIRED: when it
    # finds nothing we resolve everything ourselves below.
    # -----------------------------------------------------------------------
    find_package(OpenSSL QUIET)

    set(_sys_hdr "${OPENSSL_INCLUDE_DIR}")
    set(_sys_ssl "${OPENSSL_SSL_LIBRARY}")
    set(_sys_crypto "${OPENSSL_CRYPTO_LIBRARY}")

    # The version of the headers the compiler would see, that is what decides
    # whether the build can work at all.
    pdf4qt_ssl_header_major(_hdr_major "${_sys_hdr}/openssl/opensslv.h")
    if(NOT _hdr_major)
        pdf4qt_ssl_header_major(_hdr_major "${_sys_hdr}/opensslv.h")
    endif()

    # -----------------------------------------------------------------------
    # Step 2: an explicit request wins over the search.
    # -----------------------------------------------------------------------
    set(_inc_req ${PDF4QT_OPENSSL3_INCLUDE_DIRS})
    set(_lib_req ${PDF4QT_OPENSSL3_LIB_DIRS})
    if(PDF4QT_OPENSSL3_ROOT_DIR)
        list(APPEND _inc_req "${PDF4QT_OPENSSL3_ROOT_DIR}/include")
        list(APPEND _lib_req "${PDF4QT_OPENSSL3_ROOT_DIR}/lib")
    endif()

    if(_inc_req OR _lib_req)
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
        # Step 3: no request - auto-detect an OpenSSL 3 that lives apart from
        # the system one.  First find the 3.x libraries (ldconfig knows them
        # all), then derive where their headers are.
        # -------------------------------------------------------------------
        pdf4qt_ssl_lib_candidates(_lib_cands "${PDF4QT_OPENSSL3_SEARCH_ROOTS}")
        pdf4qt_ssl_find_library(_ssl_lib _ssl_lib_dir ssl "${_lib_cands}")
        pdf4qt_ssl_find_library(_crypto_lib _crypto_lib_dir crypto "${_lib_cands}")

        set(_lib_cands2 "")
        if(_ssl_lib_dir)
            list(APPEND _lib_cands2 "${_ssl_lib_dir}")
        endif()
        if(_crypto_lib_dir)
            list(APPEND _lib_cands2 "${_crypto_lib_dir}")
        endif()

        pdf4qt_ssl_header_candidates(_hdr_cands "${_lib_cands2}" "${PDF4QT_OPENSSL3_SEARCH_ROOTS}")
        pdf4qt_ssl_find_headers(_inc "${_hdr_cands}" _hdr_trace)
    endif()

    # -----------------------------------------------------------------------
    # Step 4: if neither the system headers nor a separate OpenSSL 3 headers
    # set is 3.x, stop here with a clear message.
    # -----------------------------------------------------------------------
    if(_hdr_major STREQUAL "3" AND NOT _inc)
        # The system headers are already 3.x: nothing to override.
        set(_inc "${_sys_hdr}")
    endif()

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
            "If your system keeps an OpenSSL 3 apart from the default one, point the build at it, for a layout "
            "/usr/include/openssl-3 + /usr/lib/openssl-3 that means: "
            "cmake -DPDF4QT_OPENSSL3_INCLUDE_DIRS=/usr/include/openssl-3 -DPDF4QT_OPENSSL3_LIB_DIRS=/usr/lib/openssl-3 .. "
            "The automatic search looked under ${PDF4QT_OPENSSL3_SEARCH_ROOTS} (set PDF4QT_OPENSSL3_SEARCH_ROOTS to a prefix "
            "whose include/ or lib/ holds your OpenSSL 3 if it lives elsewhere). If you passed those options and still get "
            "this message, they did not reach this cmake invocation: check that the configure command, not a wrapper or a "
            "stale cache, carries them (cmake -S <source> -B <build> -L -V | grep PDF4QT_OPENSSL3 should show them).")
    endif()

    # The headers that will be handed to the compiler must be 3.x, otherwise
    # the failure would show up much later as a wall of compile errors.
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
    # Step 5: the libraries.  Prefer the 3.x ones found above; fall back to
    # what CMake found when it is 3.x as well.
    # -----------------------------------------------------------------------
    if(NOT _ssl_lib)
        pdf4qt_ssl_major_version(_ssl_major "${_sys_ssl}")
        pdf4qt_ssl_major_version(_crypto_major "${_sys_crypto}")
        if(_ssl_major STREQUAL "3" AND _crypto_major STREQUAL "3")
            set(_ssl_lib "${_sys_ssl}")
            set(_crypto_lib "${_sys_crypto}")
        endif()
    endif()

    # -----------------------------------------------------------------------
    # Step 6: when everything CMake found is already 3.x, keep the usual
    # OpenSSL::SSL / OpenSSL::Crypto targets, so nothing else changes.
    # -----------------------------------------------------------------------
    if(NOT _ssl_lib AND NOT _crypto_lib AND _inc STREQUAL _sys_hdr)
        message(STATUS "PDF4QT: using the OpenSSL found by CMake (${OPENSSL_VERSION}, headers ${OPENSSL_INCLUDE_DIR}).")
        set(PDF4QT_SSL_DEPS OpenSSL::SSL OpenSSL::Crypto PARENT_SCOPE)
        return()
    endif()

    if(NOT _ssl_lib OR NOT _crypto_lib)
        message(FATAL_ERROR
            "No OpenSSL 3 libraries (libssl.so.3 and libcrypto.so.3) could be found, although the 3.x headers "
            "(${_inc}) were located. Pass the directory that holds them: -DPDF4QT_OPENSSL3_LIB_DIRS=<that directory> "
            "(the search covered ${PDF4QT_OPENSSL3_SEARCH_ROOTS} and the ldconfig cache).")
    endif()

    message(STATUS "PDF4QT: OpenSSL 3 headers ${_inc}; linking '${_ssl_lib}' and '${_crypto_lib}'.")

    add_library(pdf4qt_openssl3 INTERFACE)
    # -I wins over the system /usr/include, which is what makes the 3.x
    # headers be used instead of the 4.x ones.
    target_include_directories(pdf4qt_openssl3 INTERFACE ${_inc})

    set(_link_items "${_ssl_lib};${_crypto_lib}")
    # The libraries live outside the default linker search path, so make the
    # dynamic loader find them at run time as well.
    set(_rpath_done "")
    foreach(_d "${_ssl_lib_dir}" "${_crypto_lib_dir}")
        if(_d AND NOT "${_d}" IN_LIST _rpath_done)
            list(APPEND _rpath_done "${_d}")
            list(APPEND _link_items "-Wl,-rpath,${_d}")
        endif()
    endforeach()

    target_link_libraries(pdf4qt_openssl3 INTERFACE ${_link_items})
    set(PDF4QT_SSL_DEPS pdf4qt_openssl3 PARENT_SCOPE)
endfunction()
