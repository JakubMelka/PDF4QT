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

#ifndef PDFAPPPATHS_H
#define PDFAPPPATHS_H

#include "pdfappglobal.h"

#include <QString>

namespace pdf
{

/// Locations of the data of the application. The organization and the application
/// name of QCoreApplication must be set before (main: "MelkaJ", "PDF4QT").
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPaths
{
public:
    PDFAppPaths() = delete;

    /// Local (not roaming) data folder of the user: AppLocalDataLocation
    /// (newapp_storage.md 1). The environment variable PDF4QT_APP_DATA_DIRECTORY
    /// replaces it (tests, a second profile).
    static QString getDataDirectory();

    /// File of the database of the schema version of the application:
    /// <data>/pdf4qt-<schema>.db (owner decision O19, PDFDatabaseFiles). The
    /// storage is opened with the folder (PDFDatabaseOptions::directory), so an
    /// older file is upgraded into it.
    static QString getDatabaseFilePath();

    /// Folder of the logs: Linux (and other Unix systems except macOS) the state
    /// directory of XDG (StateLocation/logs, ~/.local/state/...), elsewhere
    /// <data>/logs. With PDF4QT_APP_DATA_DIRECTORY it is <data>/logs.
    static QString getLogDirectory();
};

}   // namespace pdf

#endif // PDFAPPPATHS_H
