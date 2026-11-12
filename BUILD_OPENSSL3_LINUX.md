# OpenSSL and PDF4QT

PDF4QT must be built against **OpenSSL 3.x**. The code uses interfaces that
OpenSSL 4.x removed - the low-level one-shot `MD5`, `SHA*`, `RC4`, `AES_*`
entry points and their `*_DIGEST_LENGTH` constants, the low-level `RSA_*` /
`BN_*` interface together with `EVP_PKEY_set1_RSA` / `EVP_PKEY_get1_RSA`, the
mutable `X509_getm_notBefore` / `notAfter`, and it relies on
`X509_get_subject_name()` returning a non-const pointer, which 4.x made const.
On a system whose default OpenSSL is 4.x the symptom is a compile error such as:

```
pdfcertificatemanager.cpp:99:52: error: invalid conversion from
'const X509_NAME*' {aka 'const X509_name_st*'} to 'X509_NAME*'
```

Porting that to OpenSSL 4 would mean rewriting the PDF security handlers and
the signature/timestamping code to `EVP_PKEY` / `EVP_Digest`, so an OpenSSL 3
is what the build has to target.

## What the build does

`cmake/Pdf4QtOpenSSL3.cmake` is included from the top-level `CMakeLists.txt`
and called through `pdf4qt_setup_openssl3()` before the subdirectories are
read. It resolves the OpenSSL the build will use and puts the dependency into
`PDF4QT_SSL_DEPS`, which the link lines of `Pdf4QtLibCore` and of the unit
tests use in place of `OpenSSL::SSL OpenSSL::Crypto`.

* **If the OpenSSL CMake already found is a 3.x - headers and libraries -
  nothing is searched and nothing is overridden**: the usual
  `OpenSSL::SSL OpenSSL::Crypto` targets are used. A vcpkg installation, or a
  system OpenSSL selected with the standard `-DOPENSSL_ROOT_DIR=...` of
  FindOpenSSL, therefore keeps working exactly as before, and CI links the
  OpenSSL it was already linking rather than a library found here. The version
  of the *headers* decides, because that is what the compiler sees; it is read
  out of `opensslv.h`.
* **Only when those headers are not 3.x** does the search run, for an OpenSSL
  3 kept apart from the system one: what the dynamic loader cache reports
  (`ldconfig -p` knows every `.so` and where it really is), the usual layouts
  (`/usr/include/openssl-3` with `/usr/lib*/openssl-3`, `/usr/local`, `/opt`)
  and the header directory matching the library directory that was found. The
  3.x headers are handed to the compiler with `-I`, which wins over
  `/usr/include`, and the 3.x libraries are linked by absolute path. A run-time
  search path is added only for a directory the loader does not already know
  about.
* A directory is accepted only when it really holds an OpenSSL 3: the library
  file name has to be `libssl.so`, `libssl.so.3` or `libssl-3-x64.dll` - which
  rejects NSS's `libssl3.so` and Kerberos's `libk5crypto.so.3` - and
  `opensslv.h` has to report the 3.x series.
* When no OpenSSL 3 can be found, configuration stops with a message that
  lists every directory it tried and why each was rejected, instead of failing
  later with pages of compile errors.

Nothing has to be passed. The configure log always prints one of:

```
PDF4QT: using the OpenSSL 3.5.6 CMake found (headers ...); nothing is searched or overridden.
PDF4QT: OpenSSL 3 headers ...; linking '.../libssl.so.3' and '.../libcrypto.so.3' (already in the loader's search path).
PDF4QT: OpenSSL 3 headers ...; linking '...' and '...', run-time path ....
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

* `PDF4QT_OPENSSL3_INCLUDE_DIRS` accepts `<dir>/openssl/*.h`,
  `<dir>/include/openssl/*.h`, or headers loose in the directory (a
  `<openssl/...>` tree is then generated in the build directory);
* `PDF4QT_OPENSSL3_LIB_DIRS` must contain both `libssl` and `libcrypto` of the
  3.x series;
* `PDF4QT_OPENSSL3_SEARCH_ROOTS` extends the automatic search to other prefixes.

An OpenSSL installation that is a whole prefix is selected with the standard
`-DOPENSSL_ROOT_DIR=...` of FindOpenSSL, which `find_package(OpenSSL)` honours
and which the first case above then keeps; there is no separate switch for it.

If you pass those options and the message still says the search ran, they did
not reach this `cmake` invocation - a wrapper script, a line continuation eaten
by the shell, or a stale `CMakeCache.txt`. Check with
`cmake -S <source> -B <build> -L -V | grep PDF4QT_OPENSSL3`.
