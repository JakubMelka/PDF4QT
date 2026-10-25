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

#include <QApplication>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>

namespace pdf
{
class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppDocumentView;
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

    /// 3 pages with annotations (step 4). Page 1 has one annotation of every kind,
    /// identified by its subject (Subj): "square", "circle", "line", "polyline",
    /// "polygon", "ink", "highlight", "underline", "strikeout", "squiggly",
    /// "freetext", "callout" (free text with a callout line), "stamp", "caret",
    /// "text" (sticky note and two replies - subject "reply", IRT; every sticky
    /// note has its popup annotation),
    /// "fileattachment", "measurement" (line with /Measure, IT LineDimension, Cap),
    /// "hidden" (flag Hidden), "locked" (Locked), "readonly" (ReadOnly), "nozoom"
    /// (NoZoom + NoRotate). Page 2: "overlapA" and "overlapB" (two overlapping
    /// rectangles) and "highlight2". Page 3 has no annotations. The author is
    /// "Tester" (replies "Replier 1", "Replier 2").
    static QString annotations(const QString& folder);

    /// 2 pages encrypted with the owner password only, which do not allow the
    /// modification of annotations (bit 6) and the filling of the form (bit 9);
    /// page 1 has a rectangle (subject "square") and the text field "name"
    static QString restricted(const QString& folder);

    /// 1 page with an AcroForm of every kind of field: text "name" (John), multi
    /// line "notes", comb "code" (MaxLen 6), password "secret", read-only "fixed",
    /// required "required", calculated read-only "total" (JavaScript, in /CO),
    /// check box "agree" (Yes / Off), radio group "choice" (A / B), editable combo
    /// box "fruit" (Apple, Banana, Cherry), multi-select list box "colors" (Red,
    /// Green, Blue), push button "reset" (ResetForm) and the unsigned signature
    /// field "signature"
    static QString fullForm(const QString& folder);

    /// \p pageCount pages with \p annotationsPerPage annotations each (rectangles,
    /// sticky notes and highlights; subjects "large-<page>-<index>"); every sticky
    /// note has its popup annotation (PDFDocumentBuilder::createAnnotationText)
    static QString annotatedLarge(const QString& folder, int pageCount = 2000, int annotationsPerPage = 5);

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

    /// Multiplier of the time limits: PDF4QT_TEST_TIMEOUT_FACTOR (at most 1000), or 20
    /// under valgrind
    static int getTimeoutFactor();

    /// Closes the message boxes of the window. \returns the number of closed boxes.
    int closeMessageBoxes();

    /// Point of the page (page coordinates of the page index \p pageIndex) in the
    /// coordinates of the draw widget of the canvas (\p ok false, when the page is
    /// not shown)
    static QPoint pageToWidget(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint, bool* ok);

    /// Draw widget of the canvas of the view (the target of the mouse events)
    static QWidget* canvasWidget(pdf::PDFAppDocumentView* view);

    /// Clicks to the point of the page (press and release sent to the draw widget).
    /// \returns false, when the page is not shown.
    static bool clickPage(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButton button = Qt::LeftButton);

    /// Drags by the left button from one point of the page to another one in
    /// \p steps moves. \returns false, when the page is not shown.
    static bool dragPage(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& from, const QPointF& to,
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier, int steps = 4);

    /// Number of the undo steps of the document (-1 without a document)
    static int undoCount(const pdf::PDFAppDocument* document);

    /// Visible widget of the type: a top-level widget or a child of one (a dialog,
    /// a popup, a message box); nullptr, when there is none
    template<typename T>
    static T* findDialog()
    {
        for (QWidget* topLevel : QApplication::topLevelWidgets())
        {
            if (T* widget = qobject_cast<T*>(topLevel); widget && widget->isVisible())
            {
                return widget;
            }
            for (T* widget : topLevel->findChildren<T*>())
            {
                if (widget->isVisible())
                {
                    return widget;
                }
            }
        }
        return nullptr;
    }

    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;
    QTemporaryDir directory;

private:
    static int s_counter;
};

}   // namespace pdftest

#endif // APPDOCUMENTFIXTURE_H
