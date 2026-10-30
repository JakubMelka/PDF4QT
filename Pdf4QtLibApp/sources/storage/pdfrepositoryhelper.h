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

#ifndef PDFREPOSITORYHELPER_H
#define PDFREPOSITORYHELPER_H

#include "pdfappglobal.h"

#include <QString>

namespace pdf
{

class PDFDatabaseStatement;

/// Common functions of the repositories of step 2 (recent files, workspaces,
/// tools): validation of the paths and the names, which come from the user, from
/// another process or from a row of the database written by another version, and
/// defensive reading of the columns. Rows of the database are untrusted input: a
/// value of a wrong type, out of the range or too long is replaced by the default
/// or the row is skipped, a reader never crashes.
class PDF4QTLIBAPPSHARED_EXPORT PDFRepositoryHelper
{
public:
    PDFRepositoryHelper() = delete;

    /// Longest path, which is stored or read (the limit of Windows long paths)
    static constexpr int MAXIMUM_PATH_LENGTH = 32767;

    /// Longest name of a workspace or of a pinned location
    static constexpr int MAXIMUM_NAME_LENGTH = 256;

    /// Path of a file or a folder as it is stored in the database: absolute, clean,
    /// with '/' separators; canonical (symbolic links resolved, the case of the file
    /// system) when the file exists. Empty for an invalid path: empty, relative,
    /// with a NUL character or longer than MAXIMUM_PATH_LENGTH - 3. It reads the file
    /// system (QFileInfo::canonicalFilePath) - a network path can block.
    static QString normalizePath(const QString& path);

    /// Path read from the database is usable: not empty, absolute, without a NUL
    /// character, at most MAXIMUM_PATH_LENGTH characters, not a device path
    /// (isDevicePath). It does not touch the file system.
    static bool isValidStoredPath(const QString& path);

    /// The path lies in a device namespace of Windows (\\.\ or \\?\, any separators):
    /// a disk, a pipe, a device - never a document or a folder of the user
    static bool isDevicePath(const QString& path);

    /// Two stored paths denote the same file (case insensitive on Windows and macOS)
    static bool isSamePath(const QString& first, const QString& second);

    /// The paths of this system have no case (isSamePath of "/a" and "/A"): a key of
    /// a path in the database (UNIQUE, an index) compares the bytes, so a repository
    /// finds a row of the same file in another case by isSamePath
    static bool isPathCaseInsensitive();

    /// Name entered by the user (workspace, pinned location): white space at the
    /// ends removed, control characters replaced by spaces. Empty, when nothing
    /// remains or the name is longer than MAXIMUM_NAME_LENGTH (refused).
    static QString normalizeName(const QString& name);

    /// Name read from the database for the user interface: like normalizeName,
    /// but a too long name is shortened instead of refused
    static QString sanitizeStoredName(const QString& name);

    /// Text of a name entered by the user or read from the database without the
    /// invisible characters, which may make two names look the same or reorder a
    /// text: lone surrogates and the format characters of whole code points
    /// (bidirectional controls, zero width characters, the tag characters outside
    /// of the BMP) are removed, the joiners ZWNJ / ZWJ stay (emoji sequences, some
    /// scripts). With \p isControlRemoved also the control characters, the
    /// unassigned code points and the line / paragraph separators are removed.
    /// At most \p maximumLength UTF-16 units are kept (a surrogate pair is never
    /// cut); the text is not trimmed. Used by the author name of the annotations
    /// and the names of the tool collections.
    static QString removeInvisibleCharacters(const QString& text, bool isControlRemoved, qsizetype maximumLength);

    /// Last part of a path ("Report.pdf", "Documents"); the path itself for a root
    static QString getFileName(const QString& path);

    /// Folder of a path ("C:/Users/Jakub/Documents" for ".../Documents/Report.pdf")
    static QString getFolder(const QString& path);

    /// Integer column of the row: the value, when the column holds an integer in the
    /// range [\p minimum, \p maximum], otherwise \p defaultValue (NULL, text, real, blob)
    static qint64 readInteger(const PDFDatabaseStatement& statement, int column, qint64 defaultValue, qint64 minimum, qint64 maximum);

    /// Text column of the row: the text, when the column holds a text of at most
    /// \p maximumLength characters, otherwise an empty string
    static QString readText(const PDFDatabaseStatement& statement, int column, int maximumLength);

    /// The text contains \p query (case insensitive); an empty query matches everything
    static bool containsText(const QString& text, const QString& query);
};

}   // namespace pdf

#endif // PDFREPOSITORYHELPER_H
