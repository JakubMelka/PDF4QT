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

#ifndef APPASYNCDIALOGS_H
#define APPASYNCDIALOGS_H

#include <QObject>
#include <QString>
#include <QStringList>

class QDialog;

namespace pdf
{
class PDFAppFileDialogRequest;
}

namespace pdftest
{

/// Answering the dialogs of the application without a nested event loop (A0, arch/tests.md): a test starts the flow
/// (a click, a key, a call), the flow shows its question and returns; the test finds the open dialog, answers it as
/// the user would (a button, a file, Esc), settles and checks the continuation - never a timer answering inside an
/// exec. Part of the fixture library UnitTestsAppDocumentFixture (GUI thread only).
class AppAsyncDialogs
{
public:
    AppAsyncDialogs() = delete;

    /// Delivers the posted events, the deferred deletions too, a few rounds (AppDocumentFixture::settle; no event loop)
    static void settle();

    /// The visible dialogs (QDialog windows), the active modal dialog first
    static QList<QDialog*> getOpenDialogs();

    /// The visible dialog of the object name (any visible dialog for an empty name), the active modal dialog first;
    /// nullptr = none
    static QDialog* findOpenDialog(const QString& objectName = QString());

    /// Clicks the visible, enabled button of the object name inside \p dialog as the user does (QAbstractButton::click);
    /// the continuation of the flow runs in this call. \returns false without such a button.
    static bool clickButton(QDialog* dialog, const QString& buttonName);

    /// findOpenDialog + clickButton + settle. \returns false, when the dialog or the button is missing.
    static bool answer(const QString& dialogName, const QString& buttonName);

    /// Esc of the open dialog (QDialog::reject, as the key of the user) + settle. \returns false without the dialog.
    static bool reject(const QString& dialogName);

    /// The file dialog request, which waits for its answer (the oldest one), nullptr = none
    static pdf::PDFAppFileDialogRequest* findFileRequest();

    /// Answers the waiting file dialog request by \p files (OpenFiles) or by one file + settle (the dialog closes
    /// without its own answer). \returns false without a request.
    static bool answerFiles(const QStringList& files);
    static bool answerFile(const QString& file);

    /// Cancels the waiting file dialog request + settle. \returns false without a request.
    static bool cancelFile();

    /// What is open: the visible dialogs ("dialog:<objectName or class>"), the active popup ("popup:..."), the
    /// waiting file dialog requests ("fileRequest:<title>")
    static QStringList describeOpen();

    /// Nothing is open (describeOpen is empty): use QVERIFY2(isNothingOpen(), qPrintable(describeOpen().join(...)))
    static bool isNothingOpen() { return describeOpen().isEmpty(); }
};

/// Fails a test, which enters a nested event loop (A0): an application event filter watches the level of the event
/// loops of the GUI thread (QThread::loopLevel) and records every new level above the level of its construction -
/// QDialog::exec, QMenu::exec, QEventLoop::exec, a static dialog of Qt - unless a QDrag exists (the named exception
/// QDrag::exec, also inside QAbstractItemView::startDrag and text drags). On a violation it writes a warning and
/// closes the active popup and the active modal widget (from the event loop), so the loop ends and the test does not
/// hang; the destructor fails the test (QTest::qFail) for violations not taken by takeViolations.
///
/// Inside the guard the test waits only by settle, QTRY_* or QTest::qWait (they process events without an event loop);
/// QSignalSpy::wait runs a QEventLoop and is counted.
class NestedEventLoopGuard : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit NestedEventLoopGuard();
    virtual ~NestedEventLoopGuard() override;

    /// The recorded violations (the class of the receiver and the type of the first event at the new level)
    const QStringList& getViolations() const { return m_violations; }

    /// The violations, which the test has seen (the destructor does not fail for them)
    QStringList takeViolations();

    /// Entries into a nested event loop excused by the named exception (a QDrag existed)
    int getExceptionCount() const { return m_exceptionCount; }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static bool isDragActive();
    void breakNestedLoop();

    int m_baseLevel = 0;
    int m_reportedLevel = 0;
    int m_exceptionCount = 0;
    QStringList m_violations;
};

}   // namespace pdftest

#endif // APPASYNCDIALOGS_H
