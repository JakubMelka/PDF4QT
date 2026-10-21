// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The versions of the third-party libraries are read from their headers: this
// file is compiled with the include directories of the libraries, which
// Pdf4QtLibCore links (pages.cmake), so the table of About shows the versions
// the application was really built with. The licenses are those of the
// copyright files of the libraries.

#include "pdfappaboutinfo.h"

#include <sqlite3.h>

#if __has_include(<openssl/opensslv.h>)
#include <openssl/opensslv.h>
#endif

#if __has_include(<zlib.h>)
#include <zlib.h>
#endif

#if __has_include(<ft2build.h>)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#if __has_include(<openjpeg.h>)
#include <openjpeg.h>
#endif

#if __has_include(<jconfig.h>)
#include <cstdio>
#include <jconfig.h>
#endif

#if __has_include(<lcms2.h>)
#include <lcms2.h>
#endif

#if __has_include(<png.h>)
#include <png.h>
#endif

#if __has_include(<clipper2/clipper.version.h>)
#include <clipper2/clipper.version.h>
#endif

#if __has_include(<blend2d/blend2d.h>)
#include <blend2d/blend2d.h>
#elif __has_include(<blend2d.h>)
#include <blend2d.h>
#endif

#if __has_include(<asmjit/core/api-config.h>)
#include <asmjit/core/api-config.h>
#endif

#if defined(PDF4QT_APP_LINKS_TBB) && __has_include(<oneapi/tbb/version.h>)
#include <oneapi/tbb/version.h>
#endif

namespace pdf
{

/// Helpers of the table of the libraries
class PDFAppThirdPartyHelper
{
public:
    PDFAppThirdPartyHelper() = delete;

    static PDFAppThirdPartyLibrary create(const char* name, const QString& version, const char* purpose, const char* license, const char* homepage)
    {
        PDFAppThirdPartyLibrary library;
        library.name = QString::fromLatin1(name);
        library.version = version;
        library.purpose = QCoreApplication::translate("pdf::PDFAppAboutInfo", purpose);
        library.license = QString::fromLatin1(license);
        library.homepage = QUrl(QString::fromLatin1(homepage));
        return library;
    }

    /// Version encoded as major * 1000000 + minor * 1000 + patch (libjpeg-turbo)
    static QString fromMillions(long long number)
    {
        return QStringLiteral("%1.%2.%3").arg(number / 1000000).arg((number / 1000) % 1000).arg(number % 1000);
    }
};

std::vector<PDFAppThirdPartyLibrary> PDFAppAboutInfo::getLinkedLibraries()
{
    using Helper = PDFAppThirdPartyHelper;
    std::vector<PDFAppThirdPartyLibrary> libraries;

    libraries.push_back(Helper::create("Qt", QString::fromLatin1(qVersion()), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Application framework"), "LGPL-3.0", "https://www.qt.io/"));

#if defined(FREETYPE_MAJOR)
    PDFAppThirdPartyLibrary freetype = Helper::create("FreeType", QStringLiteral("%1.%2.%3").arg(FREETYPE_MAJOR).arg(FREETYPE_MINOR).arg(FREETYPE_PATCH),
                                                      QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Font rendering"), "FTL", "https://freetype.org/");
    freetype.licenseDetails = QStringLiteral("FTL or GPL-2.0-or-later");
    libraries.push_back(freetype);
#endif

#if defined(OPENSSL_VERSION_STR)
    libraries.push_back(Helper::create("OpenSSL", QString::fromLatin1(OPENSSL_VERSION_STR), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Cryptography and signatures"),
                                       "Apache-2.0", "https://www.openssl.org/"));
#endif

#if defined(LCMS_VERSION)
    libraries.push_back(Helper::create("Little CMS", QStringLiteral("%1.%2").arg(LCMS_VERSION / 1000).arg((LCMS_VERSION / 10) % 100),
                                       QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Color management"), "MIT", "https://www.littlecms.com/"));
#endif

#if defined(ZLIB_VERSION)
    libraries.push_back(Helper::create("zlib", QString::fromLatin1(ZLIB_VERSION), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Compression"), "Zlib", "https://zlib.net/"));
#endif

#if defined(FT_CONFIG_OPTION_USE_PNG) && defined(PNG_LIBPNG_VER_STRING)
    libraries.push_back(Helper::create("libpng", QString::fromLatin1(PNG_LIBPNG_VER_STRING), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Color glyphs of fonts"),
                                       "libpng-2.0", "https://www.libpng.org/pub/png/libpng.html"));
#endif

#if defined(LIBJPEG_TURBO_VERSION_NUMBER)
    PDFAppThirdPartyLibrary libjpeg = Helper::create("libjpeg-turbo", Helper::fromMillions(LIBJPEG_TURBO_VERSION_NUMBER), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "JPEG images"),
                                                     "BSD-3-Clause", "https://libjpeg-turbo.org/");
    libjpeg.licenseDetails = QStringLiteral("IJG, BSD-3-Clause, Zlib");
    libraries.push_back(libjpeg);
#endif

#if defined(OPJ_VERSION_MAJOR)
    libraries.push_back(Helper::create("OpenJPEG", QStringLiteral("%1.%2.%3").arg(OPJ_VERSION_MAJOR).arg(OPJ_VERSION_MINOR).arg(OPJ_VERSION_BUILD),
                                       QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "JPEG 2000 images"), "BSD-2-Clause", "https://www.openjpeg.org/"));
#endif

#if defined(BL_VERSION)
    libraries.push_back(Helper::create("Blend2D", QStringLiteral("%1.%2.%3").arg((BL_VERSION >> 16) & 0xFF).arg((BL_VERSION >> 8) & 0xFF).arg(BL_VERSION & 0xFF),
                                       QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Vector rasterizer"), "Zlib", "https://blend2d.com/"));
#endif

#if defined(ASMJIT_LIBRARY_VERSION)
    libraries.push_back(Helper::create("AsmJit", QStringLiteral("%1.%2.%3").arg((ASMJIT_LIBRARY_VERSION >> 16) & 0xFF).arg((ASMJIT_LIBRARY_VERSION >> 8) & 0xFF).arg(ASMJIT_LIBRARY_VERSION & 0xFF),
                                       QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Code generator of Blend2D"), "Zlib", "https://asmjit.com/"));
#endif

#if __has_include(<clipper2/clipper.version.h>)
    libraries.push_back(Helper::create("Clipper2", QString::fromLatin1(CLIPPER2_VERSION), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Polygon clipping"),
                                       "BSL-1.0", "https://www.angusj.com/clipper2/Docs/Overview.htm"));
#endif

#if defined(PDF4QT_APP_LINKS_TBB) && defined(TBB_VERSION_MAJOR)
    libraries.push_back(Helper::create("oneTBB", QStringLiteral("%1.%2").arg(TBB_VERSION_MAJOR).arg(TBB_VERSION_MINOR), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Parallel execution"),
                                       "Apache-2.0", "https://github.com/uxlfoundation/oneTBB"));
#endif

    libraries.push_back(Helper::create("SQLite", QString::fromLatin1(sqlite3_libversion()), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Storage of the settings and of the history"),
                                       "blessing", "https://sqlite.org/"));

#if defined(FT_CONFIG_OPTION_USE_BROTLI)
    libraries.push_back(Helper::create("Brotli", QString(), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "WOFF2 fonts"), "MIT", "https://github.com/google/brotli"));
#endif

#if defined(FT_CONFIG_OPTION_USE_BZIP2)
    libraries.push_back(Helper::create("bzip2", QString(), QT_TRANSLATE_NOOP("pdf::PDFAppAboutInfo", "Compressed fonts"), "bzip2-1.0.6", "https://sourceware.org/bzip2/"));
#endif

    return libraries;
}

}   // namespace pdf
