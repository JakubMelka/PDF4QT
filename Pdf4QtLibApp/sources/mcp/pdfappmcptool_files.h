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

#ifndef PDFAPPMCPTOOL_FILES_H
#define PDFAPPMCPTOOL_FILES_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

#include <QString>

namespace pdf
{

class PDFAppContext;

/// Handler of the tool "files" (plan of step 7, 3.9 and 7; package C): open a
/// PDF file (a file open already is only activated - Read only, no question),
/// save the document over its own file (always confirmed, 940:134248; a signed
/// document incrementally) and save a copy (958:27539; overwriting an existing
/// file only with overwrite and always confirmed). The writes go through the
/// writer of step 3 in a worker (QSaveFile, the Mark of the Web); a password is
/// typed only in the password screen of the tab.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpFilesTool : public PDFAppMcpToolHandler
{
public:
    /// The kind of an access of checkFilePath
    enum class Access
    {
        Open,       ///< An existing PDF file (the %PDF header)
        Write       ///< A new or existing .pdf file in an existing folder
    };

    /// Longest wait of open for the password typed in the tab (2 min)
    static constexpr int DEFAULT_PASSWORD_TIMEOUT = 120000;

    virtual QString getToolName() const override { return QStringLiteral("files"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// Rules of the paths of the assistants (plan 7): absolute, canonical; UNC,
    /// device paths (\\?\, \\.\), reserved device names, NTFS streams, network
    /// locations, directories, missing folders and the folders of the database
    /// and of the logs of PDF4QT are refused; open needs the %PDF header, write
    /// the extension .pdf. Returns the canonical path or an empty string and the
    /// reason for the model.
    static QString checkFilePath(const QString& path, Access access, const PDFAppContext* context, QString* errorMessage);

    /// The wait for the password of open (tests)
    static void setPasswordTimeout(int timeout);
    static int getPasswordTimeout();
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_FILES_H
