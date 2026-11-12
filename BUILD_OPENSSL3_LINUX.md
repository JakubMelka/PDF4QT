# OpenSSL and PDF4QT

PDF4QT must be built against **OpenSSL 3.x**. The code uses interfaces that
OpenSSL 4.x removed - the low-level `MD5`, `RC4`, `AES_*`, `SHA*` one-shots and
their `*_DIGEST_LENGTH` constants, the low-level `RSA_*`/`BN_*` interface,
`EVP_PKEY_set1_RSA`/`EVP_PKEY_get1_RSA`, the mutable `X509_getm_notBefore`/
`X509_getm_notAfter`, and it relies on `X509_get_subject_name()` returning a
non-const pointer. Porting that to OpenSSL 4 would mean rewriting the PDF
security handlers and the signature/timestamping code to `EVP_Digest`/`EVP_PKEY`.

On a system whose default OpenSSL is 4.x and that keeps a 3.x elsewhere, the
symptom is a compile error such as:

```
pdfcertificatemanager.cpp:99:52: error: invalid conversion from
'const X509_NAME*' to 'X509_NAME*'
```

`find_package(OpenSSL)` alone cannot fix this: even when it finds the 3.x
libraries, the compiler still takes the 4.x headers from `/usr/include`.

## What the build does

`cmake/Pdf4QtOpenSSL3.cmake` (called from the top-level `CMakeLists.txt` through
`pdf4qt_setup_openssl3()`) resolves the OpenSSL 3 itself:

* it reads the version out of `opensslv.h`, so it knows the version of the
  headers the compiler would actually see;
* if those are not 3.x, it looks for a separate OpenSSL 3 - what `ldconfig`
  reports for `libssl.so.3`/`libcrypto.so.3`, the usual layouts
  (`/usr/include/openssl-3`, `/usr/lib*/openssl-3`, `/usr/local`, `/opt`) and
  the header directory matching the library directory it found;
* it adds `-I` for the 3.x headers (which wins over `/usr/include`), links the
  3.x libraries by absolute path and adds a run-time search path, so the built
  binaries keep working when they are run;
* the three `target_link_libraries` that used `OpenSSL::SSL OpenSSL::Crypto`
  now use `${PDF4QT_SSL_DEPS}`.

Nothing has to be passed. The configure log always prints one of:

```
PDF4QT: using the OpenSSL 3.5.6 found by CMake (headers ...)
PDF4QT: OpenSSL 3 headers ...; linking '.../libssl.so.3' and '.../libcrypto.so.3'.
```

## If it cannot find your OpenSSL 3

Configuration stops with a message that lists every directory it tried and why
each was rejected (`not a directory`, `no opensslv.h in it`, `has headers, but
... does not report the 3.x series`). Read that list; it usually names the
directory you meant.

To point the build at it explicitly:

```sh
cmake -S . -B build \
      -DPDF4QT_OPENSSL3_INCLUDE_DIRS=/usr/include/openssl-3 \
      -DPDF4QT_OPENSSL3_LIB_DIRS=/usr/lib/openssl-3
```

* `PDF4QT_OPENSSL3_INCLUDE_DIRS` accepts `<dir>/openssl/*.h`, `<dir>/include/openssl/*.h`
  or headers loose in the directory (a `<openssl/...>` tree is then generated in
  the build directory);
* `PDF4QT_OPENSSL3_LIB_DIRS` must contain both `libssl` and `libcrypto` of the
  3.x series;
* `PDF4QT_OPENSSL3_ROOT_DIR=<prefix>` is shorthand for `<prefix>/include` plus
  `<prefix>/lib`;
* `PDF4QT_OPENSSL3_SEARCH_ROOTS` extends the automatic search to other prefixes.

If you pass those options and the message still says the search ran, they did
not reach this `cmake` invocation - a wrapper script, a line continuation eaten
by the shell, or a stale `CMakeCache.txt`. Check with
`cmake -S <source> -B <build> -L -V | grep PDF4QT_OPENSSL3`.
