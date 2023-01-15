
# PDF4QT
**(c) Jakub Melka 2018-2022**

**Mgr.Jakub.Melka@gmail.com**

**https://jakubmelka.github.io/**

This software is consisting of PDF rendering library, and several
applications, such as advanced document viewer, command line tool,
and document page manipulator application. Software is implementing PDF
functionality based on PDF Reference 2.0. It is written and maintained
by Jakub Melka.

*Software works on Microsoft Windows / Linux.*

Software is provided without any warranty of any kind.

## 1. ACKNOWLEDGEMENTS

This software is based in part on the work of the Independent JPEG Group.

Portions of this software are copyright © 2019 The FreeType
Project (www.freetype.org). All rights reserved.

## 2. LEGAL ISSUES

Both library and viewer uses more benevolent LGPL license, so it is more
usable in commercial software, than GPL code only. Please see attached
file - LICENSE.txt to see details. This software also uses several
third party software, and user of this software must also respect licenses
of third party libraries.

## 3. FEATURES

Software have following features (the list is not complete):

- [x] multithreading support
- [x] hardware accelerated rendering
- [x] encryption
- [x] color management
- [x] optional content handling
- [x] text layout analysis
- [x] signature validation
- [x] annotations
- [x] form filling
- [x] text to speech capability
- [x] editation
- [x] file attachments
- [x] optimalization (compressing documents)
- [x] command line tool
- [x] audio book conversion
- [x] internal structure inspector
- [x] compare documents
- [x] static XFA support (readonly, simple XFA only)
- [x] electronically/digitally sign documents
- [x] public key security encryption
- [ ] 3D PDF support *(planned in year 2022)*
- [ ] create fillable forms *(planned in year 2023)*
- [ ] watermarks / headers / footers *(planned in year 2023)*
- [ ] presentation application *(planned in year 2023)*

## 4. THIRD PARTY LIBRARIES

Several third-party libraries are used.

1. libjpeg, see https://www.ijg.org/
2. FreeType, see https://www.freetype.org/index.html, FTL license used
3. OpenJPEG, implementing Jpeg2000, see https://www.openjpeg.org/, 2-clause MIT license
4. Qt, https://www.qt.io/, LGPL license used
5. OpenSSL, https://www.openssl.org/, Apache 2.0 license
6. LittleCMS, http://www.littlecms.com/
7. zlib, https://zlib.net/

## 5. CONTRIBUTIONS

If you want to contribute to this project, it is required, that you (contributor)
fill and digitally sign document [Contributor License Agreement](CLA/Contributor_License_Agreement.pdf),
because I want to have a freedom to do whatever I want with my library, without obligation
to someone else. But I would strongly prefer, if you want to contribute, to contribute
in a form of testing, consultation, giving advices etc. I would like to write this library
entirely by myself.

## 6. COMPILING

Compilation on Windows and Linux is available. To compile this project, compiler supporting C++20 is needed.
On Windows, you can use Visual Studio 2022 or mingw. On linux, only GCC 11.2.0 was tested.

### Compilation instructions (both Windows/Linux)
1. Download Qt 6.4 or higher, and VCPKG package manager (https://vcpkg.io/en/index.html)
2. Open Qt Creator and configure the project
3. Build
 
### CMAKE Compilation Options

Several important compilation options is available and should be set before building. On Windows,
CMake can prepare Wix project to create *.msi installer package.

|                  Option                | Platform |     Description                                          |
| ------------------------------------   | ---------|--------------------------------------------------------- |
| `PDF4QT_INSTALL_MSVC_REDISTRIBUTABLE`  | Windows  |Includes MSVC redistributable in installation             |
| `PDF4QT_INSTALL_PREPARE_WIX_INSTALLER` | Windows  |Prepare .msi installator using Wix installer              |
| `PDF4QT_INSTALL_DEPENDENCIES`          | Any      |Install dependent libraries into installation directory   |
| `PDF4QT_INSTALL_QT_DEPENDENCIES`       | Any      |Install Qt dependent libraries into installation directory|

Following important variables should be set or checked before any attempt to compile this project:

|                  Variable              | Platform |     Description                                          |
| ------------------------------------   | ---------|--------------------------------------------------------- |
| `PDF4QT_QT_ROOT`                       | Any      |Qt installation directory                                 |
| `QT_CREATOR_SKIP_VCPKG_SETUP`          | Any      |Enable or disable automatic vcpkg setup                   |
| `CMAKE_PROJECT_INCLUDE_BEFORE`         | Any      |Should be set to package manager auto setup               |
| `CMAKE_TOOLCHAIN_FILE`                 | Any      |Should be set to toolchain                                |

#### Sample setup on Windows

Following set of variables gives sample setup for MS Windows. It is minimal initial configuration
to be able to built Debug build on MS Windows.

| Key                             | Value                                                        |
| ------------------------------- | -------------------------------------------------------------|
| `CMAKE_BUILD_TYPE`              | Debug                                                        |
| `CMAKE_CXX_COMPILER`            | %{Compiler:Executable:Cxx}                                   |
| `CMAKE_C_COMPILER`              | %{Compiler:Executable:C}                                     |
| `CMAKE_GENERATOR`               | Ninja                                                        |
| `CMAKE_PREFIX_PATH`             | %{Qt:QT_INSTALL_PREFIX}                                      |
| `CMAKE_PROJECT_INCLUDE_BEFORE`  | %{IDE:ResourcePath}/package-manager/auto-setup.cmake         |
| `CMAKE_TOOLCHAIN_FILE`          | %{Qt:QT_INSTALL_PREFIX}/lib/cmake/Qt6/qt.toolchain.cmake     |
| `PDF4QT_QT_ROOT`                | C:/Programming/Qt/6.4.0/msvc2019_64                          |
| `QT_QMAKE_EXECUTABLE`           | %{Qt:qmakeExecutable}                                        |


### Tested Compilers - Windows
 - Visual Studio 2022 (Microsoft Visual C++ Compiler 17.1)
 - MinGW 11.2.0
 
### Tested Compilers - Linux
 - GCC 11.2.0

## 7. DISCLAIMER

I wrote this project in my free time. I hope you will find it useful!
