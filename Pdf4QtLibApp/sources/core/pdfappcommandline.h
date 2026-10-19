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

#ifndef PDFAPPCOMMANDLINE_H
#define PDFAPPCOMMANDLINE_H

#include "pdfappglobal.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// Error of the command line. It is shown in the window (never a crash, never a
/// message box before the window exists).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommandLineError
{
public:
    enum class Kind
    {
        UnknownArgument,    ///< Unknown option
        MissingValue,       ///< --page without the value
        InvalidPage,        ///< --page with a value, which is not a page number
        FileNotFound,       ///< The file does not exist (it is still kept in getFiles)
        NotAFile,           ///< The path is a folder
        TooManyFiles,       ///< More files than MAXIMUM_FILES
        InvalidArgument     ///< Empty, too long or not representable path
    };

    Kind kind = Kind::UnknownArgument;

    /// Argument, which caused the error
    QString argument;

    /// Translated text of the error
    QString getMessage() const;
};

/// Command line of PDF4QT (newapplication.md 3, Prostředí aplikace):
/// PDF4QT [file…] [--page <n>] [--portable] [--new-window]
///
/// The parser is a pure function of the arguments and of the working directory,
/// it never fails: every problem is an entry of getErrors(). Files are absolute,
/// cleaned paths; a missing file is reported, but it is kept in getFiles(), so the
/// running instance, which opens it, shows the error in its window. Page numbers
/// are from 1 and they apply to every file of the command line. "--" ends the
/// options. Arguments file:// are converted to local files.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommandLine
{
public:
    /// Maximal number of files of one command line
    static constexpr int MAXIMUM_FILES = 500;

    /// Maximal page number
    static constexpr int MAXIMUM_PAGE = 10000000;

    /// Maximal length of an argument
    static constexpr int MAXIMUM_ARGUMENT_LENGTH = 32 * 1024;

    PDFAppCommandLine() = default;

    /// Parses \p arguments (QCoreApplication::arguments, the first one is the program),
    /// relative paths are resolved against \p workingDirectory
    static PDFAppCommandLine parse(const QStringList& arguments, const QString& workingDirectory);

    /// Files to open (absolute paths, in the order of the command line, without duplicates)
    const QStringList& getFiles() const { return m_files; }

    /// Page to show (from 1), 0 = not given
    int getPage() const { return m_page; }

    /// --portable: database in memory, no logs on the disk, no forwarding
    bool isPortable() const { return m_portable; }

    /// --new-window: a separate instance, no forwarding, no listening
    bool isNewWindow() const { return m_newWindow; }

    /// The files are forwarded to the running instance, which holds the name of the
    /// forwarding (decision 11): not portable, not --new-window and some files. A start
    /// without files is a new window of its own.
    bool isForwardingAllowed() const { return !m_portable && !m_newWindow && !m_files.isEmpty(); }

    /// The instance may take the name of the forwarding, when it is free (every
    /// instance except the portable one)
    bool isListeningAllowed() const { return !m_portable; }

    const std::vector<PDFAppCommandLineError>& getErrors() const { return m_errors; }
    bool hasErrors() const { return !m_errors.empty(); }

    /// Translated texts of the errors
    QStringList getErrorMessages() const;

    /// Text of the usage (for --help and for the errors in the log)
    static QString getUsage();

private:
    void addError(PDFAppCommandLineError::Kind kind, const QString& argument);
    void addFile(const QString& argument, const QString& workingDirectory);
    void parsePage(const QString& value, const QString& argument);

    QStringList m_files;
    int m_page = 0;
    bool m_portable = false;
    bool m_newWindow = false;
    std::vector<PDFAppCommandLineError> m_errors;
};

}   // namespace pdf

#endif // PDFAPPCOMMANDLINE_H
