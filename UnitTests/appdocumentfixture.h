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

#ifndef APPDOCUMENTFIXTURE_H
#define APPDOCUMENTFIXTURE_H

#include "pdfappcontext.h"
#include "pdfappmainwindow.h"

#include <QPointer>
#include <QSize>
#include <QString>
#include <QTemporaryDir>

#include <memory>

namespace pdf
{
class PDFAppDocumentTab;
class PDFDocument;
}

namespace pdftest
{

/// Sample PDF files of the tests of the document tab (step 3). Every function
/// writes one file into the folder and returns its absolute path (an empty text,
/// when the file cannot be written). The files are made by PDFDocumentBuilder;
/// the text of the pages is real text (QPdfWriter fonts), so it can be searched.
class AppDocumentSamples
{
public:
    AppDocumentSamples() = delete;

    /// Password of the user password sample and of the owner password sample
    static QString getUserPassword() { return QStringLiteral("user-secret"); }
    static QString getOwnerPassword() { return QStringLiteral("owner-secret"); }

    /// Password of the PKCS#12 file of the signed sample
    static QString getCertificatePassword() { return QStringLiteral("test-password"); }

    /// Text drawn on the page (index from 0) of the simple samples: "Page N" and a
    /// sentence with the words "alpha", "beta" and the number of the page
    static QString getPageText(int pageIndex);

    /// \p pageCount pages 300 x 400 with the text of getPageText
    static QString simple(const QString& folder, const QString& name, int pageCount);

    /// 10 pages with page labels: i, ii, iii (lowercase roman), 1 - 5 (decimal),
    /// A-1, A-2 (prefix "A-", decimal)
    static QString pageLabels(const QString& folder);

    /// 5 pages with an outline: "Introduction" (page 1), "Chapter 1" (page 2) with
    /// the child "Section 1.1" (page 3), "Appendix" (page 5)
    static QString outline(const QString& folder);

    /// 4 pages, page 1 has link annotations (in this order): GoTo page 3, GoTo the
    /// named destination "chapter2" (page 4), URI https://example.com/, Launch
    /// "notepad.exe", GoToR "other.pdf", JavaScript "app.alert('x')"; the named
    /// destination is in the /Dests name tree
    static QString links(const QString& folder);

    /// 2 pages with two embedded files ("notes.txt", "data.csv") in the
    /// /EmbeddedFiles name tree and a file attachment annotation on page 1
    static QString attachments(const QString& folder);

    /// 1 page with two optional content groups "Layer A" (on) and "Layer B" (off)
    static QString layers(const QString& folder);

    /// 1 page with an AcroForm text field "name" (value "John") and its widget
    static QString form(const QString& folder);

    /// 2 pages encrypted by AES-256 with the user password getUserPassword
    static QString userPassword(const QString& folder);

    /// 2 pages encrypted by AES-256 with the owner password only (opens without
    /// a password, the permissions are limited)
    static QString ownerPassword(const QString& folder);

    /// 2 pages signed by an invisible signature of a self-signed test certificate,
    /// which is written to \p certificatePath (PKCS#12, getCertificatePassword)
    static QString signedDocument(const QString& folder, QString* certificatePath = nullptr);

    /// \p pageCount pages (2000 by default), text on every page (large documents, D11)
    static QString large(const QString& folder, int pageCount = 2000);

    /// Writes the document. \returns false on an error.
    static bool write(pdf::PDFDocument& document, const QString& filePath);
};

/// Fixture of the tests of the document tab: a context with a portable memory
/// database (a unique name per process and instance), the real main window shown
/// offscreen and a temporary folder for the samples. The destructor deletes the
/// window, closes the context and restores the global handlers of the dialogs.
class AppDocumentFixture
{
public:
    explicit AppDocumentFixture(QSize size = QSize(1440, 960));
    ~AppDocumentFixture();

    AppDocumentFixture(const AppDocumentFixture&) = delete;
    AppDocumentFixture& operator=(const AppDocumentFixture&) = delete;

    /// Absolute path of a file in the temporary folder
    QString path(const QString& name) const;

    /// Folder of the samples
    QString folder() const { return directory.path(); }

    /// Opens the file by PDFAppMainWindow::openDocuments. \returns the document tab
    /// of the file (nullptr, when it was not opened).
    pdf::PDFAppDocumentTab* open(const QString& filePath, int page = 0);

    /// Current tab as a document tab (nullptr for another tab)
    pdf::PDFAppDocumentTab* currentDocumentTab() const;

    /// Processes the events (deferred deletions too)
    static void settle();

    /// Closes the message boxes of the window. \returns the number of closed boxes.
    int closeMessageBoxes();

    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;
    QTemporaryDir directory;

private:
    static int s_counter;
};

}   // namespace pdftest

#endif // APPDOCUMENTFIXTURE_H
