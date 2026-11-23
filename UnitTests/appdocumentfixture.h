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
#include "pdfdeferreddeletion.h"

#include <QApplication>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
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
    static QString getAttachmentsPassword() { return QStringLiteral("attachments-secret"); }

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

    /// attachments() encrypted in its embedded files only (step 5 of phase 2, Q5.9 c): the password
    /// getAttachmentsPassword() opens the attachments, the document opens without it
    static QString attachmentsOnly(const QString& folder);

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

    /// signedDocument written again with an outline (one item), a layer (OCG), an
    /// embedded file and its file attachment annotation (a note): every panel of the
    /// sidebar has content, so all nine tabs are shown (the signature itself is not
    /// valid any more - the file is written whole - but the field is signed)
    static QString allPanels(const QString& folder);

    /// The file of RestoredSignedFile signed by a new certificate: a signature over the
    /// whole file, which the application has to restore from the bytes
    static QString restoredSignedDocument(const QString& folder);

    /// The file of restoredSignedDocument with a later revision, whose page tree has one page twice
    /// (Signature/invalid/pdf-wrapping-6.pdf): the reader shows the oldest content of the file, which
    /// has no signature field, the signature is in the bytes of the file
    static QString oldestContentSignedDocument(const QString& folder);

    /// A copy \p name of the file \p source with a wrong offset of its table (startxref): the reader
    /// restores its objects from the bytes (PDFDocumentReader::isDocumentRestored), the content is the same
    static QString damagedCopy(const QString& source, const QString& name);

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

/// The synchronous deletion detector of step A in a fixture of the App tests, which owns a PDFAppMainWindow (every
/// such fixture has one, RB-H2). The constructor resets the count of the detector (in the outermost fixture only)
/// and, when the switch of lifetimedeletionmode.h is on, turns its reports on and fails the current test function
/// on them. closeWindow ends the fixture as main.cpp ends the application. At the end the reports go back to their
/// state before the fixture: a fixture inside another one keeps the detector of the outer one on (RB-L8).
class AppDeletionCheck
{
public:
    /// \p isTestFunction false: a process outside of a test function (a child instance of a test), only the
    /// reports are turned on (the process forwards them to its test)
    explicit AppDeletionCheck(bool isTestFunction = true);
    ~AppDeletionCheck();

    AppDeletionCheck(const AppDeletionCheck&) = delete;
    AppDeletionCheck& operator=(const AppDeletionCheck&) = delete;

    /// The end of the fixture, only at the event loop level 0: \p window (may be nullptr) is deleted at once - a
    /// deliberate synchronous deletion inside a SynchronousDeletionScope, as main.cpp at the end of the application -
    /// and the objects retired before or left for later by the deletion are flushed. Then the queued work of the
    /// application runs outside of any scope, so the detector sees a synchronous deletion there (RB-L1), and what
    /// it retired is flushed too. The reports go back to their state before the fixture.
    void closeWindow(QWidget* window);

private:
    /// The reports back to their state before the fixture (once)
    void restore();

    bool m_wasReported = false;
    bool m_isRestored = false;

    /// Number of the living checks (nested fixtures)
    static int s_depth;
};

/// Fixture of the tests of the document tab: a context with a portable memory
/// database (a unique name per process and instance), the real main window shown
/// offscreen and a temporary folder for the samples. The destructor deletes the
/// window (a deliberate synchronous deletion, as main.cpp), flushes the retired
/// objects (PDFDeferredDeletion::flush, AppDeletionCheck::closeWindow), closes the
/// context and restores the global handlers of the dialogs. The synchronous deletion
/// detector is on during the whole fixture, when its switch is on
/// (lifetimedeletionmode.h).
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

    /// Processes the events (deferred deletions too). Only from the body of a test (the event
    /// loop level 0); inside an event loop it only processes the events (a deferred deletion
    /// there could delete an object, whose code is on the stack).
    static void settle();

    /// Closes \p object by the real single place of the application (step A), never settles:
    /// a tab by PDFAppMainWindow::closeTab (false, when a question of the tab stays open; the
    /// window retires the tab once the package of the window switches its close to retire), a
    /// page by the navigation of its tab back (false, when its tab has no previous entry or the
    /// page is not shown), a tool shell by the close of its page (an own tab of a tool: the tab),
    /// anything else by PDFDeferredDeletion::retire. The object stays alive (inert) until the
    /// event loop: a test settles right after the call, before anything opens a loop (RR2-L6).
    bool retire(QObject* object);

    /// Answers the open questions of a close (a PDFAppDocumentChoiceDialog: unsaved changes, a running operation) as
    /// the user, who closes the tab, does: Don't Save, else Stop, and settles after each answer (at most four
    /// questions). A test calls it from its body after a retire, which a question has left pending (a close from
    /// an own signal, RB-M6). \returns the number of the answered questions.
    static int answerCloseQuestions();

    /// Deletes \p object at once on purpose (a destructor order test, a deletion matrix, a window or a
    /// standalone page of a test): inside PDFDeferredDeletion::SynchronousDeletionScope, so the synchronous
    /// deletion detector of step A is quiet for it. A close of the application goes through retire instead.
    template<typename T>
    static void deleteNow(T* object)
    {
        pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
        delete object;
    }

    /// The deleter of a std::unique_ptr of a test, which owns a window, a page or a view: deleteNow
    struct DeleteNow
    {
        template<typename T>
        void operator()(T* object) const { deleteNow(object); }
    };

    /// deleteNow of \p object from the event loop (the deleteLater of a test, a window deleted later on purpose;
    /// QObject::deleteLater would delete it outside of the scope of deleteNow)
    static void deleteLater(QObject* object)
    {
        QPointer<QObject> guard(object);
        QTimer::singleShot(0, QCoreApplication::instance(), [guard]() { deleteNow(guard.data()); });
    }

    /// Multiplier of the time limits: PDF4QT_TEST_TIMEOUT_FACTOR (at most 1000), or 20
    /// under valgrind
    static int getTimeoutFactor();

    /// Closes the message boxes of the window. \returns the number of closed boxes.
    int closeMessageBoxes();

    /// F1 pressed as the user does: the key goes to the open popup or the focused
    /// widget, the shortcut map of Qt decides, whether it reaches the command
    void pressHelpKey();

    /// Topic of the Help tab, when it is the current tab of the window (empty otherwise)
    QString getShownHelpTopic() const;

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

    /// The detector (the first member: on before the context and the window exist)
    AppDeletionCheck deletionCheck;
    std::unique_ptr<pdf::PDFAppContext> context;
    QPointer<pdf::PDFAppMainWindow> window;
    QTemporaryDir directory;

private:
    static int s_counter;
};

}   // namespace pdftest

#endif // APPDOCUMENTFIXTURE_H
