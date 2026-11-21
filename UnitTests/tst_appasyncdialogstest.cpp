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

// The skeleton of A0 (no nested event loop in Pdf4QtLibApp and Pdf4QtLibFluent, newapp_robustness.md 14-16): the
// asynchronous file dialogs (PDFAppFileDialogRequest; the answer and a test handler never inside the call, a requester
// gone gets nothing, the dialog belongs to the window), the choice dialog answered by a signal (also when it is deleted without
// an answer), the leave flow (a second request refused, a question or a reporter gone ends the step Refused, the
// accepted pages forget their choices at a canceled end), the default leave functions of the page and of the GUI of a
// tool, and the guard, which fails a test entering a nested event loop.

#include "appasyncdialogs.h"
#include "appdocumentfixture.h"

#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappfiledrop.h"
#include "pdfappleaveflow.h"
#include "pdfapppage.h"
#include "pdfdatabase.h"
#include "pdfdocumenttoolgui.h"

#include <QDialog>
#include <QDrag>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

/// What the receivers got (it outlives a receiver deleted by the test)
class AsyncDeliveries
{
public:
    int filesCount = 0;
    QStringList files;
    int fileCount = 0;
    QString file;
    int choiceCount = 0;
    pdf::PDFAppDocumentChoice choice = pdf::PDFAppDocumentChoice::Keep;
    int codeCount = 0;
    int code = -1;
    int stepCount = 0;
    pdf::PDFAppLeaveResult step = pdf::PDFAppLeaveResult::Pending;
};

/// The requester of the tests: member slots, which record the answers
class AsyncReceiver : public QObject
{
public:
    explicit AsyncReceiver(AsyncDeliveries* deliveries) :
        m_deliveries(deliveries)
    {

    }

    void onFiles(const QStringList& files)
    {
        ++m_deliveries->filesCount;
        m_deliveries->files = files;
    }

    void onFile(const QString& file)
    {
        ++m_deliveries->fileCount;
        m_deliveries->file = file;
    }

    void onChoice(pdf::PDFAppDocumentChoice choice)
    {
        ++m_deliveries->choiceCount;
        m_deliveries->choice = choice;
    }

    void onCode(int code)
    {
        ++m_deliveries->codeCount;
        m_deliveries->code = code;
    }

    void onStep(pdf::PDFAppLeaveResult result)
    {
        ++m_deliveries->stepCount;
        m_deliveries->step = result;
    }

private:
    AsyncDeliveries* m_deliveries;
};

/// A requester as the callers of the application: it keeps the returned request in a member and writes its state
/// after the request call (the window of Open…, the start page of a tool)
class AsyncSelfRequester : public QObject
{
public:
    explicit AsyncSelfRequester(AsyncDeliveries* deliveries) :
        m_deliveries(deliveries)
    {

    }

    void requestSave(QWidget* parent)
    {
        m_request = pdf::PDFAppFileDialogs::requestSaveFileName(parent, QStringLiteral("C:/Docs/r.pdf"), this, &AsyncSelfRequester::onFile);
        m_isRequesting = true;
    }

    void requestPanelOpen(QWidget* parent)
    {
        m_request = pdf::PDFAppPanelFileDialogs::requestOpenFileName(parent, QStringLiteral("Import"), QStringLiteral("C:/In"), QString(), this, &AsyncSelfRequester::onFile);
        m_isRequesting = true;
    }

    void onFile(const QString& file)
    {
        m_isRequesting = false;
        ++m_deliveries->fileCount;
        m_deliveries->file = file;
    }

    pdf::PDFAppFileDialogRequest* getRequest() const { return m_request.data(); }
    bool isRequesting() const { return m_isRequesting; }

private:
    AsyncDeliveries* m_deliveries;
    QPointer<pdf::PDFAppFileDialogRequest> m_request;
    bool m_isRequesting = false;
};

/// A page, a shell: it waits for its question and reports its step to the flow (Accepted = Done)
class AsyncFlowReporter : public QObject
{
public:
    explicit AsyncFlowReporter(pdf::PDFAppLeaveFlow* flow) :
        m_flow(flow)
    {

    }

    void onQuestionEnded(int code)
    {
        ++questionCount;
        if (m_flow)
        {
            m_flow->finishStep(this, code == QDialog::Accepted ? pdf::PDFAppLeaveResult::Done : pdf::PDFAppLeaveResult::Refused);
        }
    }

    int questionCount = 0;

private:
    QPointer<pdf::PDFAppLeaveFlow> m_flow;
};

/// A page with the default leave functions and a record of finishLeave
class AsyncTestPage : public pdf::PDFAppPage
{
public:
    explicit AsyncTestPage(QWidget* parent = nullptr) :
        pdf::PDFAppPage(nullptr, parent)
    {

    }

    virtual QString getTitle() const override { return QStringLiteral("Test"); }

    virtual bool finishLeave(bool isProceeding) override
    {
        finishes.push_back(isProceeding);
        return true;
    }

    std::vector<bool> finishes;
};

class AppAsyncDialogsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup();

    void fileRequestHandlerAnswerQueued();
    void fileRequestSingleFileHandlers();
    void fileRequestDialogSettings();
    void fileRequestDialogAccepted();
    void fileRequestRequesterDeleted();
    void fileRequestHandlerDeletesRequester();
    void fileRequestDialogDeletedWithWindow();
    void panelFileRequestHandler();
    void choiceDialogAnswered();
    void choiceDialogDeletedWithoutFinished();
    void choiceDialogReceiverDeleted();
    void dialogAnswerShowModal();
    void flowSecondRequestRefused();
    void flowQuestionAnswered();
    void flowQuestionDeletedEndsRefused();
    void flowReporterDeletedEndsRefused();
    void flowAcceptedPagesFinished();
    void flowEndsSilentlyWithOwner();
    void pageRequestDefaults();
    void toolOwnTabCloseDefault();
    void nestedLoopGuardCatchesExec();
};

void AppAsyncDialogsTest::cleanup()
{
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
    pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppFileDialogs::setSaveBackupHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppFileDialogs::setOpenBackupHandler(pdf::PDFAppFileDialogs::FolderHandler());
    pdf::PDFAppFileDialogs::setPrintFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
    pdf::PDFAppPanelFileDialogs::setSaveHandler(pdf::PDFAppPanelFileDialogs::FileHandler());
    pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler());

    // Nothing of a test stays open for the next one
    for (QDialog* dialog : pdftest::AppAsyncDialogs::getOpenDialogs())
    {
        dialog->reject();
    }
    pdftest::AppAsyncDialogs::settle();
    QVERIFY2(pdftest::AppAsyncDialogs::isNothingOpen(), qPrintable(pdftest::AppAsyncDialogs::describeOpen().join(QStringLiteral(", "))));
}

void AppAsyncDialogsTest::fileRequestHandlerAnswerQueued()
{
    // The handler of a test answers as the user of a real dialog: never inside the call (the requester connects and
    // keeps the request after it), from the event loop with the arguments of the call; its answer comes right after
    // it, exactly once
    pdftest::NestedEventLoopGuard guard;
    QWidget window;
    QWidget* child = new QWidget(&window);
    QPointer<QWidget> handlerParent;
    QString handlerFolder;
    int handlerCount = 0;
    pdf::PDFAppFileDialogs::setOpenFilesHandler([&](QWidget* parent, const QString& folder)
    {
        ++handlerCount;
        handlerParent = parent;
        handlerFolder = folder;
        return QStringList{ QStringLiteral("C:/Docs/a.pdf"), QStringLiteral("C:/Docs/b.pdf") };
    });

    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QPointer<pdf::PDFAppFileDialogRequest> request = pdf::PDFAppFileDialogs::requestOpenFileNames(child, QStringLiteral("C:/Docs"), &receiver, &AsyncReceiver::onFiles);
    QVERIFY(request);
    QCOMPARE(handlerCount, 0);
    QVERIFY(request->isHandlerWaiting());
    QCOMPARE(deliveries.filesCount, 0);
    QCOMPARE(pdftest::AppAsyncDialogs::findFileRequest(), request.data());
    QCOMPARE(request->getDialog(), nullptr);
    QCOMPARE(request->parent(), &receiver);

    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(handlerCount, 1);
    QCOMPARE(handlerParent.data(), child);
    QCOMPARE(handlerFolder, QStringLiteral("C:/Docs"));
    QCOMPARE(deliveries.filesCount, 1);
    QCOMPARE(deliveries.files, QStringList({ QStringLiteral("C:/Docs/a.pdf"), QStringLiteral("C:/Docs/b.pdf") }));
    QCOMPARE(deliveries.fileCount, 0);
    QVERIFY(!request);
    QCOMPARE(pdftest::AppAsyncDialogs::findFileRequest(), nullptr);

    // Documents and inputs use the same handler (as the synchronous functions)
    pdf::PDFAppFileDialogs::requestOpenDocumentFileNames(child, QStringLiteral("C:/Other"), &receiver, &AsyncReceiver::onFiles);
    pdf::PDFAppFileDialogs::requestOpenInputFileNames(child, QStringLiteral("C:/Inputs"), &receiver, &AsyncReceiver::onFiles);
    QCOMPARE(handlerCount, 1);
    QCOMPARE(deliveries.filesCount, 1);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(handlerCount, 3);
    QCOMPARE(handlerFolder, QStringLiteral("C:/Inputs"));
    QCOMPARE(deliveries.filesCount, 3);
    QCOMPARE(guard.getViolations().size(), 0);
}

void AppAsyncDialogsTest::fileRequestSingleFileHandlers()
{
    // A single file: the text of the handler, empty = canceled (fileChosen with an empty text)
    QWidget window;
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QStringList calls;
    pdf::PDFAppFileDialogs::setFolderHandler([&](QWidget*, const QString& folder) { calls << QStringLiteral("folder ") + folder; return QStringLiteral("C:/Pinned"); });
    pdf::PDFAppFileDialogs::setSaveFileHandler([&](QWidget*, const QString& path) { calls << QStringLiteral("save ") + path; return QString(); });
    pdf::PDFAppFileDialogs::setSaveBackupHandler([&](QWidget*, const QString& path) { calls << QStringLiteral("backup ") + path; return QStringLiteral("C:/b.pdf4qt-backup"); });
    pdf::PDFAppFileDialogs::setOpenBackupHandler([&](QWidget*, const QString& folder) { calls << QStringLiteral("import ") + folder; return QStringLiteral("C:/i.pdf4qt-backup"); });
    pdf::PDFAppFileDialogs::setPrintFileHandler([&](QWidget*, const QString& path) { calls << QStringLiteral("print ") + path; return QStringLiteral("C:/p.xps"); });

    pdf::PDFAppFileDialogs::requestExistingDirectory(&window, QStringLiteral("C:/A"), &receiver, &AsyncReceiver::onFile);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.fileCount, 1);
    QCOMPARE(deliveries.file, QStringLiteral("C:/Pinned"));

    pdf::PDFAppFileDialogs::requestSaveFileName(&window, QStringLiteral("C:/A/r.pdf"), &receiver, &AsyncReceiver::onFile);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.fileCount, 2);
    QCOMPARE(deliveries.file, QString());

    pdf::PDFAppFileDialogs::requestSaveBackupFileName(&window, QStringLiteral("C:/A/s.pdf4qt-backup"), &receiver, &AsyncReceiver::onFile);
    pdf::PDFAppFileDialogs::requestOpenBackupFileName(&window, QStringLiteral("C:/B"), &receiver, &AsyncReceiver::onFile);
    pdf::PDFAppFileDialogs::requestPrintFileName(&window, QStringLiteral("C:/A/r.xps"), QStringLiteral("xps"), &receiver, &AsyncReceiver::onFile);
    pdf::PDFAppFileDialogs::requestExistingDirectory(&window, QStringLiteral("C:/C"), QStringLiteral("Choose"), &receiver, &AsyncReceiver::onFile);
    QCOMPARE(deliveries.fileCount, 2);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.fileCount, 6);
    QCOMPARE(deliveries.file, QStringLiteral("C:/Pinned"));
    QCOMPARE(deliveries.filesCount, 0);
    QCOMPARE(calls, QStringList({ QStringLiteral("folder C:/A"), QStringLiteral("save C:/A/r.pdf"), QStringLiteral("backup C:/A/s.pdf4qt-backup"),
                                  QStringLiteral("import C:/B"), QStringLiteral("print C:/A/r.xps"), QStringLiteral("folder C:/C") }));
}

void AppAsyncDialogsTest::fileRequestDialogSettings()
{
    // Without a handler the dialog of the static functions is shown modal over the window (no event loop): Export
    // Settings - the folder of the proposed file, its name selected, Save, the filter and the suffix of a backup
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdftest::NestedEventLoopGuard guard;
    QWidget window;
    window.resize(400, 300);
    QWidget* child = new QWidget(&window);
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);

    const QString proposed = QDir(directory.path()).absoluteFilePath(QStringLiteral("Settings 2026.pdf4qt-backup"));
    QPointer<pdf::PDFAppFileDialogRequest> request = pdf::PDFAppFileDialogs::requestSaveBackupFileName(child, proposed, &receiver, &AsyncReceiver::onFile);
    QVERIFY(request);
    QPointer<QFileDialog> dialog = request->getDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->parentWidget(), &window);
    QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
    QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose));
    QCOMPARE(dialog->windowTitle(), QStringLiteral("Export Settings"));
    QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptSave);
    QCOMPARE(dialog->fileMode(), QFileDialog::AnyFile);
    QCOMPARE(dialog->defaultSuffix(), QLatin1String(pdf::PDFDatabaseBackup::FILE_SUFFIX));
    QCOMPARE(dialog->nameFilters(), QStringList(QStringLiteral("PDF4QT settings backups (*.pdf4qt-backup)")));
    QCOMPARE(QFileInfo(dialog->directory().absolutePath()), QFileInfo(directory.path()));
    QCOMPARE(dialog->selectedFiles().value(0), proposed);
    QCOMPARE(dialog->supportedSchemes(), QStringList(QStringLiteral("file")));
    QVERIFY(pdftest::AppAsyncDialogs::describeOpen().contains(QStringLiteral("fileRequest:Export Settings")));
    QCOMPARE(deliveries.fileCount, 0);

    // A name typed without the suffix gets it (the dialog asks before an existing backup is replaced)
    dialog->selectFile(QStringLiteral("Typed"));
    static_cast<QDialog*>(dialog.data())->accept();
    QCOMPARE(deliveries.fileCount, 1);
    QCOMPARE(deliveries.file, QDir(directory.path()).absoluteFilePath(QStringLiteral("Typed.pdf4qt-backup")));
    pdftest::AppAsyncDialogs::settle();
    QVERIFY(!dialog);
    QVERIFY(!request);

    // A folder: the folder mode with the folders only, canceled by the dialog
    QPointer<pdf::PDFAppFileDialogRequest> folderRequest = pdf::PDFAppFileDialogs::requestExistingDirectory(child, directory.path(), &receiver, &AsyncReceiver::onFile);
    QVERIFY(folderRequest && folderRequest->getDialog());
    QCOMPARE(folderRequest->getDialog()->fileMode(), QFileDialog::Directory);
    QVERIFY(folderRequest->getDialog()->testOption(QFileDialog::ShowDirsOnly));
    QCOMPARE(folderRequest->getDialog()->windowTitle(), QStringLiteral("Pin a Location"));
    folderRequest->getDialog()->reject();
    QCOMPARE(deliveries.fileCount, 2);
    QCOMPARE(deliveries.file, QString());
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(guard.getViolations().size(), 0);
}

void AppAsyncDialogsTest::fileRequestDialogAccepted()
{
    // OpenFile of a panel: the chosen file is the local path of the static function; the fixture answers a request
    // as the user would (the dialog closes without its own answer)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = QDir(directory.path()).absoluteFilePath(QStringLiteral("notes.txt"));
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("notes");
    }

    pdftest::NestedEventLoopGuard guard;
    QWidget window;
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QPointer<pdf::PDFAppFileDialogRequest> request = pdf::PDFAppPanelFileDialogs::requestOpenFileName(&window, QStringLiteral("Import Bookmarks"), directory.path(),
                                                                                                       QStringLiteral("Text files (*.txt)"), &receiver, &AsyncReceiver::onFile);
    QVERIFY(request);
    QPointer<QFileDialog> dialog = request->getDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->windowTitle(), QStringLiteral("Import Bookmarks"));
    QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFile);
    QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptOpen);
    QCOMPARE(dialog->nameFilters(), QStringList(QStringLiteral("Text files (*.txt)")));

    dialog->selectFile(filePath);
    static_cast<QDialog*>(dialog.data())->accept();
    QCOMPARE(deliveries.fileCount, 1);
    QCOMPARE(QFileInfo(deliveries.file), QFileInfo(filePath));

    // The fixture: OpenFiles answered by two files, the dialog closed, the answer given once
    QPointer<pdf::PDFAppFileDialogRequest> filesRequest = pdf::PDFAppFileDialogs::requestOpenFileNames(&window, directory.path(), &receiver, &AsyncReceiver::onFiles);
    QVERIFY(filesRequest);
    QPointer<QFileDialog> filesDialog = filesRequest->getDialog();
    QVERIFY(filesDialog && filesDialog->isVisible());
    QCOMPARE(filesDialog->fileMode(), QFileDialog::ExistingFiles);
    QCOMPARE(filesDialog->windowTitle(), QStringLiteral("Open PDF Files"));
    QVERIFY(pdftest::AppAsyncDialogs::answerFiles({ QStringLiteral("C:/x.pdf"), QStringLiteral("C:/y.pdf") }));
    QCOMPARE(deliveries.filesCount, 1);
    QCOMPARE(deliveries.files, QStringList({ QStringLiteral("C:/x.pdf"), QStringLiteral("C:/y.pdf") }));
    QVERIFY(!filesDialog);
    QVERIFY(!filesRequest);
    QVERIFY(!pdftest::AppAsyncDialogs::answerFiles({ QStringLiteral("C:/z.pdf") }));
    QCOMPARE(deliveries.filesCount, 1);
    QCOMPARE(guard.getViolations().size(), 0);
}

void AppAsyncDialogsTest::fileRequestRequesterDeleted()
{
    // QR3.7: the requester owns only its request - deleted while the dialog is open, it gets nothing; the dialog of
    // the window stays until the user closes it
    QWidget window;
    AsyncDeliveries deliveries;
    AsyncReceiver* receiver = new AsyncReceiver(&deliveries);
    QPointer<pdf::PDFAppFileDialogRequest> request = pdf::PDFAppFileDialogs::requestSaveFileName(&window, QStringLiteral("C:/Docs/r.pdf"), receiver, &AsyncReceiver::onFile);
    QVERIFY(request);
    QPointer<QFileDialog> dialog = request->getDialog();
    QVERIFY(dialog && dialog->isVisible());

    delete receiver;
    QVERIFY(!request);
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(pdftest::AppAsyncDialogs::findFileRequest(), nullptr);
    dialog->reject();
    pdftest::AppAsyncDialogs::settle();
    QVERIFY(!dialog);
    QCOMPARE(deliveries.fileCount, 0);

    // A handler: the answer on its way is dropped with the requester
    pdf::PDFAppFileDialogs::setSaveFileHandler([](QWidget*, const QString&) { return QStringLiteral("C:/Docs/s.pdf"); });
    receiver = new AsyncReceiver(&deliveries);
    request = pdf::PDFAppFileDialogs::requestSaveFileName(&window, QStringLiteral("C:/Docs/r.pdf"), receiver, &AsyncReceiver::onFile);
    QVERIFY(request);
    delete receiver;
    QVERIFY(!request);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.fileCount, 0);

    // Without a requester nothing is asked
    QCOMPARE(pdf::PDFAppFileDialogs::requestSaveFileName<AsyncReceiver>(&window, QStringLiteral("C:/Docs/r.pdf"), nullptr, &AsyncReceiver::onFile), nullptr);
}

void AppAsyncDialogsTest::fileRequestHandlerDeletesRequester()
{
    // The handler of a test is foreign code, which may delete the requester (a test closes the window in it). It
    // never runs inside the request call: the requester keeps the returned request and writes its state after the
    // call; from the event loop the handler deletes the requester (the request with it) and nothing is delivered
    pdftest::NestedEventLoopGuard guard;
    QWidget window;
    AsyncDeliveries deliveries;
    QPointer<AsyncSelfRequester> requester = new AsyncSelfRequester(&deliveries);
    int handlerCount = 0;
    QWidget* handlerParent = nullptr;
    QString handlerPath;
    pdf::PDFAppFileDialogs::setSaveFileHandler([&](QWidget* parent, const QString& path)
    {
        ++handlerCount;
        handlerParent = parent;
        handlerPath = path;
        delete requester.data();
        return QStringLiteral("C:/Docs/s.pdf");
    });

    requester->requestSave(&window);
    QCOMPARE(handlerCount, 0);
    QVERIFY(requester);
    QVERIFY(requester->isRequesting());
    QPointer<pdf::PDFAppFileDialogRequest> request = requester->getRequest();
    QVERIFY(request);
    QVERIFY(request->isHandlerWaiting());
    QCOMPARE(request->parent(), requester.data());

    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(handlerCount, 1);
    QCOMPARE(handlerParent, &window);
    QCOMPARE(handlerPath, QStringLiteral("C:/Docs/r.pdf"));
    QVERIFY(!requester);
    QVERIFY(!request);
    QCOMPARE(deliveries.fileCount, 0);
    QCOMPARE(pdftest::AppAsyncDialogs::findFileRequest(), nullptr);

    // The handler of a panel the same way
    int panelCount = 0;
    pdf::PDFAppPanelFileDialogs::setOpenHandler([&](QWidget*, const QString&, const QString&, const QString&)
    {
        ++panelCount;
        delete requester.data();
        return QStringLiteral("C:/In/b.json");
    });
    requester = new AsyncSelfRequester(&deliveries);
    requester->requestPanelOpen(&window);
    QCOMPARE(panelCount, 0);
    QVERIFY(requester && requester->isRequesting() && requester->getRequest());
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(panelCount, 1);
    QVERIFY(!requester);
    QCOMPARE(deliveries.fileCount, 0);
    QCOMPARE(pdftest::AppAsyncDialogs::findFileRequest(), nullptr);
    QCOMPARE(guard.getViolations().size(), 0);
}

void AppAsyncDialogsTest::fileRequestDialogDeletedWithWindow()
{
    // The window goes while its dialog is open (no finished): the request is canceled from the event loop, never
    // inside the destructor of the window
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QWidget* window = new QWidget();
    QPointer<pdf::PDFAppFileDialogRequest> request = pdf::PDFAppFileDialogs::requestOpenFileNames(window, QStringLiteral("C:/Docs"), &receiver, &AsyncReceiver::onFiles);
    QVERIFY(request && request->getDialog());
    delete window;
    QCOMPARE(deliveries.filesCount, 0);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.filesCount, 1);
    QCOMPARE(deliveries.files, QStringList());
    QVERIFY(!request);
}

void AppAsyncDialogsTest::panelFileRequestHandler()
{
    // The handlers of the panels get the four arguments of the call from the event loop (never inside the call); the
    // answer comes right after them
    QWidget window;
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QStringList arguments;
    pdf::PDFAppPanelFileDialogs::setSaveHandler([&](QWidget* parent, const QString& title, const QString& path, const QString& filter)
    {
        arguments = QStringList{ parent == &window ? QStringLiteral("window") : QStringLiteral("other"), title, path, filter };
        return QStringLiteral("C:/Out/attachment.xlsx");
    });
    pdf::PDFAppPanelFileDialogs::setOpenHandler([](QWidget*, const QString&, const QString&, const QString&) { return QString(); });

    pdf::PDFAppPanelFileDialogs::requestSaveFileName(&window, QStringLiteral("Save Attachment"), QStringLiteral("C:/Out/a.xlsx"), QStringLiteral("All files (*)"),
                                                     &receiver, &AsyncReceiver::onFile);
    QVERIFY(arguments.isEmpty());
    QCOMPARE(deliveries.fileCount, 0);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(arguments, QStringList({ QStringLiteral("window"), QStringLiteral("Save Attachment"), QStringLiteral("C:/Out/a.xlsx"), QStringLiteral("All files (*)") }));
    QCOMPARE(deliveries.fileCount, 1);
    QCOMPARE(deliveries.file, QStringLiteral("C:/Out/attachment.xlsx"));

    pdf::PDFAppPanelFileDialogs::requestOpenFileName(&window, QStringLiteral("Import"), QStringLiteral("C:/In"), QString(), &receiver, &AsyncReceiver::onFile);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.fileCount, 2);
    QCOMPARE(deliveries.file, QString());
}

void AppAsyncDialogsTest::choiceDialogAnswered()
{
    // ask shows the dialog modal as run did (application modal, no event loop); the clicked choice comes in the
    // click, exactly once, and the dialog is deleted after it; Esc is Cancel
    pdftest::NestedEventLoopGuard guard;
    QWidget host;
    host.resize(600, 400);
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);

    pdf::PDFAppDocumentChoiceDialog* dialog = new pdf::PDFAppDocumentChoiceDialog(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Save the changes?"),
                                                                                  QStringLiteral("Text"), 440, &host);
    dialog->setObjectName(QStringLiteral("testChoiceDialog"));
    dialog->addChoice(QStringLiteral("Don't Save"), pdf::PDFAppDocumentChoice::Discard, false, true, QStringLiteral("discardButton"));
    dialog->addChoice(QStringLiteral("Cancel"), pdf::PDFAppDocumentChoice::Cancel, false, false, QStringLiteral("cancelButton"));
    dialog->addChoice(QStringLiteral("Save"), pdf::PDFAppDocumentChoice::Save, true, false, QStringLiteral("saveButton"));
    QPointer<QDialog> pointer(dialog);

    QVERIFY(pdf::PDFAppDocumentChoiceDialog::ask(dialog, &receiver, &AsyncReceiver::onChoice));
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
    QCOMPARE(QApplication::activeModalWidget(), dialog);
    QCOMPARE(deliveries.choiceCount, 0);
    QCOMPARE(pdftest::AppAsyncDialogs::findOpenDialog(QStringLiteral("testChoiceDialog")), dialog);

    QVERIFY(pdftest::AppAsyncDialogs::answer(QStringLiteral("testChoiceDialog"), QStringLiteral("saveButton")));
    QCOMPARE(deliveries.choiceCount, 1);
    QCOMPARE(deliveries.choice, pdf::PDFAppDocumentChoice::Save);
    QVERIFY(!pointer);

    pdf::PDFAppDocumentChoiceDialog* second = new pdf::PDFAppDocumentChoiceDialog(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Stop?"), QStringLiteral("Text"), 440, &host);
    second->setObjectName(QStringLiteral("secondChoiceDialog"));
    second->addChoice(QStringLiteral("Stop"), pdf::PDFAppDocumentChoice::Stop, false, true, QStringLiteral("stopButton"));
    second->addChoice(QStringLiteral("Keep"), pdf::PDFAppDocumentChoice::Keep, true, false, QStringLiteral("keepButton"));
    pdf::PDFAppDocumentChoiceDialog::ask(second, &receiver, &AsyncReceiver::onChoice);
    QVERIFY(pdftest::AppAsyncDialogs::reject(QStringLiteral("secondChoiceDialog")));
    QCOMPARE(deliveries.choiceCount, 2);
    QCOMPARE(deliveries.choice, pdf::PDFAppDocumentChoice::Cancel);

    // No dialog: Cancel from the event loop
    pdf::PDFAppDocumentChoiceDialog::ask(nullptr, &receiver, &AsyncReceiver::onChoice);
    QCOMPARE(deliveries.choiceCount, 2);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.choiceCount, 3);
    QCOMPARE(deliveries.choice, pdf::PDFAppDocumentChoice::Cancel);
    QCOMPARE(guard.getViolations().size(), 0);
}

void AppAsyncDialogsTest::choiceDialogDeletedWithoutFinished()
{
    // A question deleted with its parent never emits finished: the answer is Cancel from the event loop (never
    // inside the deletion), exactly once
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QWidget* host = new QWidget();
    pdf::PDFAppDocumentChoiceDialog* dialog = new pdf::PDFAppDocumentChoiceDialog(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Close?"), QStringLiteral("Text"), 440, host);
    dialog->addChoice(QStringLiteral("Close"), pdf::PDFAppDocumentChoice::Discard, false, true, QStringLiteral("closeTabButton"));
    QVERIFY(pdf::PDFAppDocumentChoiceDialog::ask(dialog, &receiver, &AsyncReceiver::onChoice));
    delete host;
    QCOMPARE(deliveries.choiceCount, 0);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.choiceCount, 1);
    QCOMPARE(deliveries.choice, pdf::PDFAppDocumentChoice::Cancel);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.choiceCount, 1);
}

void AppAsyncDialogsTest::choiceDialogReceiverDeleted()
{
    // The receiver gone: nothing is delivered, the dialog answers and goes without it
    QWidget host;
    AsyncDeliveries deliveries;
    AsyncReceiver* receiver = new AsyncReceiver(&deliveries);
    pdf::PDFAppDocumentChoiceDialog* dialog = new pdf::PDFAppDocumentChoiceDialog(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Close?"), QStringLiteral("Text"), 440, &host);
    dialog->setObjectName(QStringLiteral("orphanDialog"));
    dialog->addChoice(QStringLiteral("Close"), pdf::PDFAppDocumentChoice::Discard, false, true, QStringLiteral("closeTabButton"));
    QPointer<QDialog> pointer(dialog);
    pdf::PDFAppDocumentChoiceDialog::ask(dialog, receiver, &AsyncReceiver::onChoice);
    delete receiver;
    QVERIFY(pdftest::AppAsyncDialogs::answer(QStringLiteral("orphanDialog"), QStringLiteral("closeTabButton")));
    QCOMPARE(deliveries.choiceCount, 0);

    // Without a receiver the dialog is not shown and goes
    pdf::PDFAppDocumentChoiceDialog* unshown = new pdf::PDFAppDocumentChoiceDialog(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Close?"), QStringLiteral("Text"), 440, &host);
    QPointer<QDialog> unshownPointer(unshown);
    QCOMPARE(pdf::PDFAppDocumentChoiceDialog::ask<AsyncReceiver>(unshown, nullptr, &AsyncReceiver::onChoice), nullptr);
    QVERIFY(!unshown->isVisible());
    pdftest::AppAsyncDialogs::settle();
    QVERIFY(!unshownPointer);
}

void AppAsyncDialogsTest::dialogAnswerShowModal()
{
    // The generic answer: the code of finished in the emission; a window modality of the dialog is kept
    QWidget host;
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QDialog* dialog = new QDialog(&host);
    dialog->setWindowModality(Qt::WindowModal);
    QPointer<QDialog> pointer(dialog);
    QVERIFY(pdf::PDFAppDialogAnswer::show(dialog, &receiver, &AsyncReceiver::onCode));
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->windowModality(), Qt::WindowModal);
    dialog->done(7);
    QCOMPARE(deliveries.codeCount, 1);
    QCOMPARE(deliveries.code, 7);
    pdftest::AppAsyncDialogs::settle();
    QVERIFY(pointer);
    QCOMPARE(deliveries.codeCount, 1);
    delete dialog;
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.codeCount, 1);
}

void AppAsyncDialogsTest::flowSecondRequestRefused()
{
    // One flow per owner: a second request is refused while the first runs; every end clears it
    QObject owner;
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(&owner);
    QCOMPARE(pdf::PDFAppLeaveFlow::find(&owner), flow);
    QVERIFY(!pdf::PDFAppLeaveFlow::isRunningIn(&owner));
    QVERIFY(!flow->start(pdf::PDFAppLeaveFlow::Goal::None));

    QSignalSpy finished(flow, &pdf::PDFAppLeaveFlow::finished);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::CloseTab));
    flow->setStep(3);
    flow->setTarget(pdf::PDFAppRoutes::tools(), pdf::PDFAppNavigationMode::Replace);
    QVERIFY(flow->getTargetRoute() == pdf::PDFAppRoutes::tools());
    QCOMPARE(flow->getTargetMode(), pdf::PDFAppNavigationMode::Replace);
    flow->setValue(42);
    QVERIFY(!flow->start(pdf::PDFAppLeaveFlow::Goal::QuitWindow));
    QCOMPARE(flow->getGoal(), pdf::PDFAppLeaveFlow::Goal::CloseTab);
    QCOMPARE(flow->getStep(), 3);
    QCOMPARE(pdf::PDFAppLeaveFlow::findRunning(&owner), flow);

    flow->finish(true);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toBool(), true);
    QVERIFY(!flow->isRunning());
    QCOMPARE(flow->getStep(), 0);
    QCOMPARE(flow->getValue(), qint64(0));
    QVERIFY(flow->getTargetRoute() == pdf::PDFAppRoute());
    QCOMPARE(flow->getTargetMode(), pdf::PDFAppNavigationMode::Push);
    flow->finish(false);
    QCOMPARE(finished.count(), 1);

    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::QuitWindow));
    QCOMPARE(flow->getGoal(), pdf::PDFAppLeaveFlow::Goal::QuitWindow);
    flow->finish(false);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(finished.at(1).at(0).toBool(), false);

    // The queue gives the objects, which still exist, in their order
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::QuitWindow));
    std::unique_ptr<QObject> first = std::make_unique<QObject>();
    std::unique_ptr<QObject> second = std::make_unique<QObject>();
    std::unique_ptr<QObject> third = std::make_unique<QObject>();
    flow->setQueue({ QPointer<QObject>(first.get()), QPointer<QObject>(second.get()), QPointer<QObject>(third.get()) });
    second.reset();
    QCOMPARE(flow->getQueueSize(), 2);
    QCOMPARE(flow->takeNext(), first.get());
    QCOMPARE(flow->takeNext(), third.get());
    QCOMPARE(flow->takeNext(), nullptr);
    flow->finish(false);
}

void AppAsyncDialogsTest::flowQuestionAnswered()
{
    // A question of a step answered: the reporter continues in the emission of finished and reports its step, the
    // owner gets it in the same call (no moment without the modal dialog)
    QObject owner;
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(&owner);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::CloseTab));
    AsyncFlowReporter reporter(flow);
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);

    QWidget host;
    QDialog* question = new QDialog(&host);
    pdf::PDFAppDialogAnswer::showModal(question);
    QVERIFY(flow->waitForQuestion(question, &reporter, &AsyncFlowReporter::onQuestionEnded));
    QVERIFY(flow->waitForStep(&reporter, &receiver, &AsyncReceiver::onStep));
    QVERIFY(!flow->waitForStep(&reporter, &receiver, &AsyncReceiver::onStep));
    QCOMPARE(flow->getQuestion(), question);
    QVERIFY(flow->isWaitingFor(&reporter));

    question->accept();
    QCOMPARE(reporter.questionCount, 1);
    QCOMPARE(deliveries.stepCount, 1);
    QCOMPARE(deliveries.step, pdf::PDFAppLeaveResult::Done);
    QVERIFY(!flow->isQuestionOpen());
    QVERIFY(!flow->hasPendingSteps());
    QVERIFY(!flow->finishStep(&reporter, pdf::PDFAppLeaveResult::Done));
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.stepCount, 1);

    // Pending is no result; a flow, which does not run, takes no wait
    QVERIFY(flow->waitForStep(&reporter, &receiver, &AsyncReceiver::onStep));
    QVERIFY(!flow->finishStep(&reporter, pdf::PDFAppLeaveResult::Pending));
    QVERIFY(flow->finishStep(&reporter, pdf::PDFAppLeaveResult::Refused));
    QCOMPARE(deliveries.stepCount, 2);
    QCOMPARE(deliveries.step, pdf::PDFAppLeaveResult::Refused);
    flow->finish(false);
    QVERIFY(!flow->waitForStep(&reporter, &receiver, &AsyncReceiver::onStep));
    QCOMPARE(flow->waitForQuestion(new QDialog(&host), &reporter, &AsyncFlowReporter::onQuestionEnded), nullptr);
}

void AppAsyncDialogsTest::flowQuestionDeletedEndsRefused()
{
    // RR3-H2: a question deleted with its parent (no finished) ends its step Refused from the event loop - the flow is
    // never stuck
    QObject owner;
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(&owner);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::QuitWindow));
    AsyncFlowReporter reporter(flow);
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);

    QWidget* host = new QWidget();
    QDialog* question = new QDialog(host);
    pdf::PDFAppDialogAnswer::showModal(question);
    QVERIFY(flow->waitForQuestion(question, &reporter, &AsyncFlowReporter::onQuestionEnded));
    QVERIFY(flow->waitForStep(&reporter, &receiver, &AsyncReceiver::onStep));
    QVERIFY(flow->isQuestionOpen());

    delete host;
    QCOMPARE(reporter.questionCount, 0);
    QCOMPARE(deliveries.stepCount, 0);
    QVERIFY(!flow->isQuestionOpen());
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(reporter.questionCount, 1);
    QCOMPARE(deliveries.stepCount, 1);
    QCOMPARE(deliveries.step, pdf::PDFAppLeaveResult::Refused);
    QVERIFY(!flow->hasPendingSteps());
    flow->finish(false);
    QVERIFY(!flow->isRunning());
}

void AppAsyncDialogsTest::flowReporterDeletedEndsRefused()
{
    // A reporter deleted before it reports (its tab, its page): Refused from the event loop, exactly once
    QObject owner;
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(&owner);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::Navigate));
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QObject* reporter = new QObject();
    QVERIFY(flow->waitForStep(reporter, &receiver, &AsyncReceiver::onStep));
    delete reporter;
    QCOMPARE(deliveries.stepCount, 0);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.stepCount, 1);
    QCOMPARE(deliveries.step, pdf::PDFAppLeaveResult::Refused);
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.stepCount, 1);

    // A waiter at the end of the flow gets nothing
    QObject laterReporter;
    QVERIFY(flow->waitForStep(&laterReporter, &receiver, &AsyncReceiver::onStep));
    flow->finish(false);
    QVERIFY(!flow->finishStep(&laterReporter, pdf::PDFAppLeaveResult::Done));
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.stepCount, 1);
}

void AppAsyncDialogsTest::flowAcceptedPagesFinished()
{
    // Decision 8: the pages, which accepted, wait for finishLeave - requestFinishPages(true) at the transaction end; a
    // canceled end gives finishLeave(false) to the pages not finished yet, each once
    QWidget host;
    AsyncTestPage* first = new AsyncTestPage(&host);
    AsyncTestPage* second = new AsyncTestPage(&host);
    QObject owner;
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(&owner);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::QuitWindow));
    flow->addAcceptedPage(first);
    flow->addAcceptedPage(first);
    flow->addAcceptedPage(second);
    QCOMPARE(flow->getAcceptedPages().size(), size_t(2));
    flow->finish(false);
    QCOMPARE(first->finishes, std::vector<bool>({ false }));
    QCOMPARE(second->finishes, std::vector<bool>({ false }));

    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::CloseTab));
    flow->addAcceptedPage(first);
    QCOMPARE(flow->requestFinishPages(true), pdf::PDFAppLeaveResult::Done);
    QCOMPARE(first->finishes, std::vector<bool>({ false, true }));
    QVERIFY(flow->getAcceptedPages().empty());
    flow->finish(true);
    QCOMPARE(first->finishes, std::vector<bool>({ false, true }));
    QCOMPARE(second->finishes, std::vector<bool>({ false }));
}

void AppAsyncDialogsTest::flowEndsSilentlyWithOwner()
{
    // The owner (a tab, a window) retired with its flow: no waiter of the flow runs, no crash
    AsyncDeliveries deliveries;
    AsyncReceiver receiver(&deliveries);
    QObject* owner = new QObject();
    pdf::PDFAppLeaveFlow* flow = new pdf::PDFAppLeaveFlow(owner);
    QSignalSpy finished(flow, &pdf::PDFAppLeaveFlow::finished);
    QVERIFY(flow->start(pdf::PDFAppLeaveFlow::Goal::CloseTab));
    QObject* reporter = new QObject();
    QVERIFY(flow->waitForStep(reporter, &receiver, &AsyncReceiver::onStep));
    QVERIFY(pdf::PDFAppLeaveFlow::isRunningIn(owner));
    delete owner;
    delete reporter;
    pdftest::AppAsyncDialogs::settle();
    QCOMPARE(deliveries.stepCount, 0);
    QCOMPARE(finished.count(), 0);
}

void AppAsyncDialogsTest::pageRequestDefaults()
{
    // A page without a question of its own lets every leave go at once and ends nothing itself (the caller ends
    // the accepted page by finishLeave at the end of the transaction)
    QWidget host;
    AsyncTestPage* page = new AsyncTestPage(&host);
    QObject owner;
    pdf::PDFAppLeaveFlow flow(&owner);
    const pdf::PDFAppRoute route = pdf::PDFAppRoutes::tools();
    QCOMPARE(page->requestLeave(route, &flow), pdf::PDFAppLeaveResult::Done);
    QCOMPARE(page->requestClose(&flow), pdf::PDFAppLeaveResult::Done);
    QCOMPARE(page->requestDocumentLock(&flow), pdf::PDFAppLeaveResult::Done);
    QVERIFY(page->finishes.empty());
}

void AppAsyncDialogsTest::toolOwnTabCloseDefault()
{
    // A tool without its own question of the own tab: the generic question of the loss text follows (K16), the
    // tool itself lets the tab go
    pdf::PDFDocumentToolGUI gui;
    QObject owner;
    pdf::PDFAppLeaveFlow flow(&owner);
    QVERIFY(!gui.hasOwnTabCloseQuestion());
    QCOMPARE(gui.requestOwnTabClose(&flow), pdf::PDFAppLeaveResult::Done);
}

void AppAsyncDialogsTest::nestedLoopGuardCatchesExec()
{
    const int timeout = 10000 * pdftest::AppDocumentFixture::getTimeoutFactor();

    // QDialog::exec: caught, and the guard closes the dialog (the test does not hang)
    {
        pdftest::NestedEventLoopGuard guard;
        QDialog dialog;
        QTimer::singleShot(timeout, &dialog, &QDialog::reject);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^NestedEventLoopGuard: a nested event loop was entered")));
        dialog.exec();
        QCOMPARE(guard.takeViolations().size(), 1);
    }

    // An own QEventLoop
    {
        pdftest::NestedEventLoopGuard guard;
        QEventLoop loop;
        QTimer::singleShot(0, &loop, &QEventLoop::quit);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^NestedEventLoopGuard: a nested event loop was entered")));
        loop.exec();
        QCOMPARE(guard.takeViolations().size(), 1);
        QCOMPARE(guard.getExceptionCount(), 0);
    }

    // Events processed without an event loop are no violation
    {
        pdftest::NestedEventLoopGuard guard;
        QTimer::singleShot(0, &guard, [] () {});
        pdftest::AppAsyncDialogs::settle();
        QTest::qWait(20);
        QCOMPARE(guard.getViolations().size(), 0);
    }

    // The named exception: a loop while a drag exists
    {
        QWidget source;
        QDrag* drag = new QDrag(&source);
        pdftest::NestedEventLoopGuard guard;
        QEventLoop loop;
        QTimer::singleShot(0, &loop, &QEventLoop::quit);
        loop.exec();
        QCOMPARE(guard.getViolations().size(), 0);
        QCOMPARE(guard.getExceptionCount(), 1);
        delete drag;
    }
}

QTEST_MAIN(AppAsyncDialogsTest)

#include "tst_appasyncdialogstest.moc"
