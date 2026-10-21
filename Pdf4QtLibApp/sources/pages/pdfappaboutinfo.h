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

#ifndef PDFAPPABOUTINFO_H
#define PDFAPPABOUTINFO_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QUrl>

#include <vector>

namespace pdf
{

class PDFAppContext;

/// Third-party library, which the application PDF4QT links (or bundles, as the
/// font Inter), as the table Third-party libraries of About (110:2158) shows it
class PDF4QTLIBAPPSHARED_EXPORT PDFAppThirdPartyLibrary
{
public:
    /// Name of the library ("FreeType")
    QString name;

    /// Version of the headers the application was built with (empty = the
    /// library has no version in its headers, the table shows a dash)
    QString version;

    /// Translated purpose of the library in PDF4QT
    QString purpose;

    /// License (SPDX like short name, as the copyright file of the library says)
    QString license;

    /// All licenses of a library, which has more of them (the tooltip of the license)
    QString licenseDetails;

    /// Home page of the library (https)
    QUrl homepage;
};

/// Row of the build information (label and value)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBuildInfoRow
{
public:
    QString key;
    QString value;
};

/// Information about the running build for the page About (Build information
/// 111:2368): only what is really known - the version of the project, the type
/// of the build, the compiler and the architecture of the build, Qt at build and
/// at run time, the system, the place of the settings database and of the logs.
/// The build date and the commit are shown only, when the build defines them
/// (PDF4QT_BUILD_DATE, PDF4QT_BUILD_COMMIT); nothing is invented.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAboutInfo
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppAboutInfo)

public:
    PDFAppAboutInfo() = delete;

    /// Version as the user sees it (three parts, PDFAppFooter::getDisplayVersion)
    static QString getVersion();

    /// "Release" or "Debug" (NDEBUG of the build)
    static QString getBuildType();

    /// Value of the row Version ("1.6.0 (Release)")
    static QString getVersionText();

    /// Build date and commit of the build (empty, when the build does not define them)
    static QString getBuildDate();
    static QString getCommit();

    /// Compiler of the build ("MSVC 19.50", "GCC 15.1.0", "Clang 19.1.0")
    static QString getCompiler();

    /// Value of the row Compiler: the compiler and the architecture of the build
    static QString getCompilerText();

    /// Value of the row Qt: the version at run time, with the version of the build
    /// in parentheses, when they differ
    static QString getQtVersionText(const QString& runTimeVersion, const QString& buildTimeVersion);
    static QString getQtVersionText();

    /// Operating system (QSysInfo::prettyProductName)
    static QString getPlatform();

    /// Value of the row Settings: the file of the database, or the statement, that
    /// nothing is written (portable mode, memory fallback, read only copy)
    static QString getSettingsText(const PDFDatabaseState& state);

    /// Value of the row Log folder (portable mode: the logs go to stderr only;
    /// an empty folder: no log files)
    static QString getLogText(const QString& logDirectory, bool isPortable);

    /// All rows of the build information of the context (nullptr = no storage)
    static QList<PDFAppBuildInfoRow> getBuildRows(const PDFAppContext* context);

    /// Text copied by Copy build info ("Key: value" lines)
    static QString toText(const QList<PDFAppBuildInfoRow>& rows);

    /// Libraries of the table Third-party libraries in the order of the table
    static std::vector<PDFAppThirdPartyLibrary> getLibraries();

    /// Version of a TrueType / OpenType font from its name table (name id 5,
    /// "Version 4.001;git..." gives "4.001"); empty for damaged data
    static QString getFontVersion(const QByteArray& fontData);

private:
    /// Versions from the headers of the libraries (pdfappthirdparty.cpp)
    static std::vector<PDFAppThirdPartyLibrary> getLinkedLibraries();
};

}   // namespace pdf

#endif // PDFAPPABOUTINFO_H
