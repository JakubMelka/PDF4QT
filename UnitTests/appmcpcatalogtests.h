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

#ifndef APPMCPCATALOGTESTS_H
#define APPMCPCATALOGTESTS_H

#include "appmcpfixture.h"
#include "mcptestclient.h"
#include "testdocumenttool.h"
#include "xfatestsamples.h"

#include "pdfappcommand.h"
#include "pdfappautorefresh.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcpcatalogtools.h"
#include "pdfappmcpconfirmation.h"
#include "pdfappmcphost.h"
#include "pdfapptooloutofmemorydialog.h"
#include "pdfapptoolresultwriter.h"
#include "pdfappundostack.h"

#include "pdfdocumentreader.h"
#include "pdfdocumenttool_compress.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfexecutionpolicy.h"
#include "pdfform.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcplimits.h"
#include "pdfsignaturehandler.h"

#include "mcpshape.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStringDecoder>
#include <QTranslator>
#include <QtTest>

#include <array>
#include <optional>

// The bridge of the tools of the catalog of phase 2 (plan of the first batch, chapter 6,
// the table Bridge): every behavior on the test tool under the id of a real tool of
// the catalog (it takes the row of that tool: the result kind, the preselected result, the
// signature impact, the locked access, the permissions), and the real Compress end to end.

/// Tests of the bridge of the catalog tools, called by the slots of AppMcpTest
class AppMcpCatalogTests
{
public:
    AppMcpCatalogTests() = delete;

    /// The test tool under the id of a tool of the catalog: a built-in tool of
    /// that id comes back at the end (the registry is never left without it)
    class ToolReplacement
    {
    public:
        ToolReplacement(pdf::PDFAppMcpHost* host, const QString& id) :
            m_host(host),
            m_id(id),
            m_previous(pdf::PDFDocumentToolRegistry::getTool(id)),
            tool(std::make_shared<pdftest::TestDocumentTool>(id))
        {
            pdf::PDFDocumentToolRegistry::unregisterTool(m_id);
            tool->setResultKindModes(true);
            pdf::PDFDocumentToolRegistry::registerTool(tool);
            if (m_host)
            {
                m_host->checkToolRegistry();
            }
        }

        ~ToolReplacement()
        {
            pdf::PDFDocumentToolRegistry::unregisterTool(m_id);
            if (m_previous)
            {
                pdf::PDFDocumentToolRegistry::registerTool(m_previous);
            }
            if (m_host)
            {
                m_host->checkToolRegistry();
            }
        }

    private:
        QPointer<pdf::PDFAppMcpHost> m_host;
        QString m_id;
        std::shared_ptr<const pdf::PDFDocumentTool> m_previous;

    public:
        std::shared_ptr<pdftest::TestDocumentTool> tool;
    };

    static QString toolName(const QString& id) { return id.section(QLatin1Char('.'), 0, 0); }
    static QString actionName(const QString& id) { return id.section(QLatin1Char('.'), 1); }

    /// The arguments of a call of a tool of the catalog
    static QJsonObject arguments(const QString& id, const QString& mode, const QString& result = QString(), const QString& path = QString(), const QString& title = QStringLiteral("New title"))
    {
        QJsonObject object;
        object.insert(QStringLiteral("action"), actionName(id));
        if (!result.isEmpty())
        {
            object.insert(QStringLiteral("result"), result);
        }
        if (!path.isEmpty())
        {
            object.insert(QStringLiteral("path"), path);
        }
        object.insert(actionName(id), QJsonObject({ { QStringLiteral("mode"), mode }, { QStringLiteral("title"), title } }));
        return object;
    }

    static QJsonObject output(const QString& id, const pdf::PDFMcpCallResult& result)
    {
        return result.toQJson().value(QStringLiteral("structuredContent")).toObject().value(actionName(id)).toObject();
    }

    static QByteArray readFile(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QString titleOf(const QString& path)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(path);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? document.getInfo()->title : QString();
    }

    /// The rows of an action
    static std::vector<pdf::PDFMcpActivityRow> rows(pdftest::AppMcpFixture& fixture, const QString& action)
    {
        std::vector<pdf::PDFMcpActivityRow> result;
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            if (row.action == action)
            {
                result.push_back(row);
            }
        }
        return result;
    }

    /// The value of a row of the question
    static QString findFact(const pdf::PDFAppMcpConfirmation& confirmation, const QString& key)
    {
        for (const QJsonValue& value : confirmation.details.value(QStringLiteral("facts")).toArray())
        {
            if (value.toObject().value(QStringLiteral("key")).toString() == key)
            {
                return value.toObject().value(QStringLiteral("value")).toString();
            }
        }
        return QString();
    }

    /// The first signature of the bytes of a file verifies (its bytes are unchanged)
    static bool isFirstSignatureValid(const QByteArray& data)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromBuffer(data);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            return false;
        }

        const pdf::PDFForm form = pdf::PDFForm::parse(&document, document.getCatalog()->getFormObject());
        pdf::PDFSignatureHandler::Parameters parameters;
        parameters.useSystemCertificateStore = false;
        const std::vector<pdf::PDFSignatureVerificationResult> results = pdf::PDFSignatureHandler::verifySignatures(form, data, parameters);
        return !results.empty() && results.front().isSignatureValid();
    }

    /// A document, whose image Compress reduces: an image of 1024 x 1024 pixels on a square
    /// of one inch of a page
    static QString createImageDocument(const QString& folder, const QString& name)
    {
        const QString path = folder + QLatin1Char('/') + name;
        QPdfWriter writer(path);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setTitle(QStringLiteral("Images"));

        QImage image(1024, 1024, QImage::Format_RGB32);
        QRandomGenerator generator(42);
        for (int y = 0; y < image.height(); ++y)
        {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                const int noise = generator.bounded(40);
                line[x] = qRgb((x * 200) / image.width() + noise, (y * 200) / image.height() + noise, ((x + y) * 100) / image.width() + noise);
            }
        }

        QPainter painter(&writer);
        painter.drawImage(QRectF(72.0, 72.0, 72.0, 72.0), image);
        painter.end();
        return path;
    }

    /// Default of "result": the choice of the result dialog, not the first value of a kind
    static void defaultResults()
    {
        // The table of chapter 6 of the plan, bold values
        const std::vector<std::pair<const char*, const char*>> expected = {
            { "optimize.compress", "save_copy" }, { "optimize.structure", "keep" }, { "pages.rotate", "keep" }, { "pages.remove", "keep" },
            { "protect.remove-links", "keep" }, { "protect.sanitize", "save_copy" }, { "convert.extract-text", "return_text" },
            { "review.statistics", "keep" }
        };
        for (const auto& item : expected)
        {
            const pdf::PDFDocumentToolCatalogEntry* entry = pdf::PDFDocumentToolCatalog::findTool(QString::fromLatin1(item.first));
            QVERIFY2(entry, item.first);
            QCOMPARE(pdf::PDFAppMcpCatalogTools::getDefaultResult(*entry), QString::fromLatin1(item.second));
            QVERIFY2(pdf::PDFAppMcpCatalogTools::getResultValues(entry->getResultKind()).contains(QString::fromLatin1(item.second)), item.first);
        }

        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("default.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();

        // Compress without a result is a copy, so it needs a path; nothing runs
        {
            const QString id = QStringLiteral("optimize.compress");
            ToolReplacement replacement(fixture.host(), id);
            std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title")));
            QVERIFY(result && result->isError);
            QVERIFY2(result->getFirstText().contains(QStringLiteral("save_copy needs an absolute path")), qPrintable(result->getFirstText()));
            QCOMPARE(replacement.tool->getExecuteCount(), 0);
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        }

        // Rotate without a result stays in the tab
        {
            const QString id = QStringLiteral("pages.rotate");
            ToolReplacement replacement(fixture.host(), id);
            std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title")));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        }

        // The default is said to the model in the object of its action
        {
            const QString id = QStringLiteral("protect.sanitize");
            ToolReplacement replacement(fixture.host(), id);
            const pdf::PDFMcpToolDefinition* definition = fixture.host()->getCatalog()->findTool(toolName(id));
            QVERIFY(definition);
            const QJsonObject properties = QJsonDocument::fromJson(definition->inputSchema).object().value(QStringLiteral("properties")).toObject();
            QVERIFY(properties.value(actionName(id)).toObject().value(QStringLiteral("description")).toString().contains(QStringLiteral("the default is save_copy")));
        }
    }

    /// result = overwrite: confirmed always, the file first, then the tab (F6)
    static void overwrite()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("overwrite.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QByteArray original = readFile(file);
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        // The confirmation also with every permission Allow, of the kind of a save
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Denied")));
        std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::Save);
        QVERIFY(confirmation->isAlwaysConfirmed);
        QVERIFY(!confirmation->permissions);
        QCOMPARE(QDir::cleanPath(confirmation->details.value(QStringLiteral("path")).toString()), QDir::cleanPath(file));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QCOMPARE(readFile(file), original);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));

        // Allow: the file has the result, when the document announces it; one undo step, saved
        QByteArray fileAtCommit;
        QObject receiver;
        QObject::connect(document, &pdf::PDFAppDocument::documentChanged, &receiver, [&fileAtCommit, &file]()
        {
            if (fileAtCommit.isEmpty())
            {
                fileAtCommit = readFile(file);
            }
        });

        // F6: while the worker writes the file, the operation of the tool holds the document: a change
        // meanwhile would move the version, the file would have a result the tab refuses
        pdf::PDFAppDocumentSaver* saver = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getSaver();
        bool wasWriting = false;
        bool wasBlocked = false;
        bool wasChangeCommitted = false;
        QObject::connect(saver, &pdf::PDFAppDocumentSaver::stateChanged, &receiver, [&, saver, document]()
        {
            if (saver->isRunning() && !wasWriting)
            {
                wasWriting = true;
                wasBlocked = document->isBlockedByOperation();
                pdf::PDFDocumentBuilder builder(document->getDocument().data());
                builder.addObject(pdf::PDFObject::createInteger(1));
                const pdf::PDFModifiedDocument change(pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build())), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::None);
                wasChangeCommitted = document->commit(change, QStringLiteral("A change during the write"), document->getVersion(), pdf::PDFAppChangeSource::User);
            }
        });

        const quint64 versionBefore = document->getVersion();
        call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Overwritten")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);

        QVERIFY(wasWriting);
        QVERIFY2(wasBlocked, "The operation holds the document during the write");
        QVERIFY2(!wasChangeCommitted, "A change is refused during the write");

        // The order of F6 itself, before anything else of the result: at the first announcement
        // of the new version the file has the result
        QVERIFY(!fileAtCommit.isEmpty());
        QVERIFY2(fileAtCommit != original, "The commit came before the write");
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(titleOf(file), QStringLiteral("Overwritten"));
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Overwritten"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QVERIFY(document->getVersion() > versionBefore);
        QVERIFY(!document->isModified());
        QVERIFY(!document->isBlockedByOperation());
        QCOMPARE(QDir::cleanPath(output(id, *result).value(QStringLiteral("path")).toString()), QDir::cleanPath(QFileInfo(file).canonicalFilePath()));

        // One row of the call: the question, which ended allowed
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Allowed));
        int allowedRows = 0;
        for (const pdf::PDFMcpActivityRow& row : rows(fixture, id))
        {
            allowedRows += row.status == pdf::PDFMcpActivityStatus::Allowed ? 1 : 0;
        }
        QCOMPARE(allowedRows, 1);

        // The automatic refresh knows the write: the document is not read again
        QTest::qWait(300);
        pdftest::AppDocumentFixture::settle();
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

        // Undo keeps the file: the document is modified against it
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
        QVERIFY(document->isModified());
        QCOMPARE(titleOf(file), QStringLiteral("Overwritten"));
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::redo()));
        QVERIFY(!document->isModified());
    }

    /// A refused overwrite changes nothing (F6, the permissions R + E + F)
    static void overwriteRefused()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("refused.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QByteArray original = readFile(file);
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);
        const QJsonObject call = arguments(id, QStringLiteral("title"), QStringLiteral("overwrite"));

        // Edit or Files set to Deny: before a run
        for (pdf::PDFAppMcpPermissionKind kind : { pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermissionKind::Files })
        {
            QVERIFY(fixture.setPermission(kind, pdf::PDFAppMcpPermission::Deny));
            std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), call);
            QVERIFY(result && result->isError);
            QVERIFY2(result->getFirstText().contains(QStringLiteral("overwrite")), qPrintable(result->getFirstText()));
            QCOMPARE(replacement.tool->getExecuteCount(), 0);
            QVERIFY(fixture.setPermission(kind, pdf::PDFAppMcpPermission::Allow));
        }

        // A path does not say another file
        {
            QJsonObject withPath = call;
            withPath.insert(QStringLiteral("path"), fixture.path(QStringLiteral("other.pdf")));
            std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), withPath);
            QVERIFY(result && result->isError);
            QVERIFY2(result->getFirstText().contains(QStringLiteral("only the results save_copy and save_file")), qPrintable(result->getFirstText()));
            QVERIFY(!QFile::exists(fixture.path(QStringLiteral("other.pdf"))));
        }

        // A file, which cannot be written, before the run
        QVERIFY(QFile::setPermissions(file, QFile::ReadOwner | QFile::ReadUser));
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), call);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("read only")), qPrintable(result->getFirstText()));
        QCOMPARE(replacement.tool->getExecuteCount(), 0);
        QVERIFY(QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser));

        // ... and one, which has become read-only while the question waited: the saver
        // refuses it, the tab is unchanged and free
        std::shared_ptr<pdf::PDFMcpCall> started = fixture.startCall(toolName(id), call);
        QVERIFY(fixture.confirmDialog());
        QVERIFY(QFile::setPermissions(file, QFile::ReadOwner | QFile::ReadUser));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(started);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("cannot be written")), qPrintable(result->getFirstText()));
        QCOMPARE(readFile(file), original);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(!document->isModified());
        QVERIFY(!document->isBlockedByOperation());
        QVERIFY(QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser));
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Failed));

        // Saved as another file during the question: the question has named the first file, the
        // new file is not written either
        const QString movedFile = fixture.path(QStringLiteral("refused-moved.pdf"));
        QVERIFY(QFile::copy(file, movedFile));
        QVERIFY(QFile::setPermissions(movedFile, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser));
        started = fixture.startCall(toolName(id), call);
        QVERIFY(fixture.confirmDialog());
        document->setSavedAs(document->getVersion(), movedFile, original);
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(started);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("another file")), qPrintable(result->getFirstText()));
        QCOMPARE(readFile(movedFile), original);
        QCOMPARE(readFile(file), original);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        // The tab closed during the question ends the call, the file is not touched
        started = fixture.startCall(toolName(id), call);
        QVERIFY(fixture.confirmDialog());
        delete tab;
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(started);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("closed")), qPrintable(result->getFirstText()));
        QCOMPARE(readFile(file), original);
    }

    /// F5: a document without a file has nothing to overwrite, and nothing signed by O31
    static void documentWithoutFile()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* signedTab = fixture.open(pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
        QVERIFY(signedTab);
        QTRY_VERIFY(signedTab->getDocument()->isReady());

        // Edit a Copy: the signed fields stay, the file does not
        signedTab->executeCommand(pdf::PDFAppCommands::editCopy());
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppDocumentTab* copyTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
        QVERIFY(copyTab && copyTab != signedTab);
        QTRY_VERIFY(copyTab->getDocument()->isReady());
        pdf::PDFAppDocument* copy = copyTab->getDocument();
        QVERIFY(copy->getFilePath().isEmpty());
        QVERIFY(pdf::PDFForm::getSignatureCount(copy->getDocument().data()) > 0);

        const QString id = QStringLiteral("optimize.structure");
        ToolReplacement replacement(fixture.host(), id);
        QJsonObject overwriteCall = arguments(id, QStringLiteral("title"), QStringLiteral("overwrite"));
        overwriteCall.insert(QStringLiteral("documentId"), copy->getId());
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), overwriteCall);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("has no file")), qPrintable(result->getFirstText()));

        const int undoBefore = pdftest::AppDocumentFixture::undoCount(copy);
        QJsonObject keepCall = arguments(id, QStringLiteral("title"), QStringLiteral("keep"));
        keepCall.insert(QStringLiteral("documentId"), copy->getId());
        result = fixture.callTool(toolName(id), keepCall);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(copy), undoBefore + 1);

        // Owner decision 4: no forced question with Edit = Allow, the result says it clearly, never "may"
        const QString note = output(id, *result).value(QStringLiteral("note")).toString();
        QVERIFY2(note.contains(QStringLiteral("will no longer be valid")), qPrintable(result->getFirstText()));
        QVERIFY(!note.contains(QStringLiteral("may")));

        // Owner decision 3: get_signatures of this version
        QJsonObject signaturesCall;
        signaturesCall.insert(QStringLiteral("action"), QStringLiteral("get_signatures"));
        signaturesCall.insert(QStringLiteral("documentId"), copy->getId());
        result = fixture.callTool(QStringLiteral("document"), signaturesCall);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY2(result->getFirstText().contains(QStringLiteral("\"invalidAfterSaving\":true")) || result->getFirstText().contains(QStringLiteral("\"invalidAfterSaving\": true")), qPrintable(result->getFirstText()));
        QVERIFY(copy->getUndoStack()->undo());
        result = fixture.callTool(QStringLiteral("document"), signaturesCall);
        QVERIFY(result);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("\"invalidAfterSaving\":false")) || result->getFirstText().contains(QStringLiteral("\"invalidAfterSaving\": false")), qPrintable(result->getFirstText()));

        // Edit = Ask: the question says the same, it is not the "may" of a change of another kind
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        std::shared_ptr<pdf::PDFMcpCall> askCall = fixture.startCall(toolName(id), keepCall);
        std::shared_ptr<pdf::PDFAppMcpConfirmation> question = fixture.confirmDialog();
        QVERIFY(question);
        QVERIFY(question->details.value(QStringLiteral("signatureLost")).toBool());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(askCall);
        QVERIFY(result && !result->isError);
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));

        // O31: the kept result of a tool rewriting the file is written whole, without
        // the signed revision of the original bytes it does not build on
        const QString target = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("kept-result.pdf"));
        QJsonObject copyPath;
        copyPath.insert(QStringLiteral("path"), target);
        QJsonObject copyCall;
        copyCall.insert(QStringLiteral("action"), QStringLiteral("save_copy"));
        copyCall.insert(QStringLiteral("documentId"), copy->getId());
        copyCall.insert(QStringLiteral("save_copy"), copyPath);
        result = fixture.callTool(QStringLiteral("files"), copyCall);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));

        const QByteArray written = readFile(target);
        QVERIFY(!written.isEmpty());
        QVERIFY(!copy->getOriginalData().isEmpty());
        QVERIFY(!written.startsWith(copy->getOriginalData()));
        QVERIFY(!written.contains("/Prev"));
        QCOMPARE(written.count("%%EOF"), qsizetype(1));
    }

    /// Owner decision 5: a value of Custom without the preset custom is an error, which names what to
    /// set; decision 6: a copy of an assistant leaves its sign in the tab as files.save_copy
    static void customValuesAndCopyMessage()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = createImageDocument(fixture.folder(), QStringLiteral("custom.pdf"));
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const quint64 version = document->getVersion();
        const QString target = fixture.path(QStringLiteral("custom-copy.pdf"));

        const auto createCall = [&target](const QJsonObject& parameters)
        {
            QJsonObject call;
            call.insert(QStringLiteral("action"), QStringLiteral("compress"));
            call.insert(QStringLiteral("path"), target);
            call.insert(QStringLiteral("compress"), parameters);
            return call;
        };

        QJsonObject parameters;
        parameters.insert(QStringLiteral("targetDpi"), 36);
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("optimize"), createCall(parameters));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("targetDpi")) && result->getFirstText().contains(QStringLiteral("preset to custom")), qPrintable(result->getFirstText()));
        QVERIFY2(!QFileInfo::exists(target), "Nothing ran");

        parameters.insert(QStringLiteral("preset"), QStringLiteral("balanced"));
        result = fixture.callTool(QStringLiteral("optimize"), createCall(parameters));
        QVERIFY(result && result->isError);
        QVERIFY(!QFileInfo::exists(target));

        // With the preset custom the value is used: the copy is written, the tab says it
        parameters.insert(QStringLiteral("preset"), QStringLiteral("custom"));
        result = fixture.callTool(QStringLiteral("optimize"), createCall(parameters));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY(QFileInfo::exists(target));
        QCOMPARE(document->getVersion(), version);
        QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("custom-copy.pdf saved"));
    }

    /// O31: a tool rewriting the file refuses keep and overwrite of a signed document, F7:
    /// the copy of each tool follows its signature impact
    static void signedDocuments()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QByteArray original = readFile(file);
        QVERIFY(original.count("%%EOF") >= 1);

        const QString o31 = QStringLiteral("The document is signed: this tool rewrites the file, so its signatures would stop being valid. Use result save_copy with a path.");
        for (const QString& id : { QStringLiteral("optimize.compress"), QStringLiteral("optimize.structure"), QStringLiteral("protect.sanitize") })
        {
            ToolReplacement replacement(fixture.host(), id);
            for (const QString& value : { QStringLiteral("keep"), QStringLiteral("overwrite") })
            {
                const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), value));
                QVERIFY(result && result->isError);
                QCOMPARE(result->getFirstText(), o31);
                QCOMPARE(replacement.tool->getExecuteCount(), 0);
            }

            const std::vector<pdf::PDFMcpActivityRow> failedRows = rows(fixture, id);
            QVERIFY(!failedRows.empty());
            QCOMPARE(failedRows.front().status, pdf::PDFMcpActivityStatus::Failed);
            QCOMPARE(failedRows.front().detail.value(QStringLiteral("reason")).toString(), o31);
        }
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        // The copy of such a tool is written whole: no old revision, no /Prev
        {
            const QString id = QStringLiteral("optimize.compress");
            ToolReplacement replacement(fixture.host(), id);
            const QString copy = fixture.path(QStringLiteral("whole.pdf"));
            const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
            const QByteArray written = readFile(copy);
            QCOMPARE(written.count("%%EOF"), qsizetype(1));
            QVERIFY(!written.contains("/Prev"));
            QVERIFY(!written.startsWith(original));
            QCOMPARE(titleOf(copy), QStringLiteral("New title"));
            QVERIFY2(!isFirstSignatureValid(written), "A rewritten file has no signed revision, its fields are visibly no longer valid (chapter 4)");

            // The question of the copy says it (Files = Ask)
            QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
            std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), fixture.path(QStringLiteral("whole-asked.pdf"))));
            std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
            QVERIFY(confirmation);
            QVERIFY2(findFact(*confirmation, QStringLiteral("Signatures")).contains(QStringLiteral("no longer valid")), qPrintable(findFact(*confirmation, QStringLiteral("Signatures"))));
            QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
            QVERIFY(pdftest::AppMcpFixture::waitResult(call));
            QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Allow));
        }

        // F7: a tool of the impact IncrementalSave appends to the signed bytes; keep goes through
        {
            const QString id = QStringLiteral("pages.rotate");
            ToolReplacement replacement(fixture.host(), id);
            const QString copy = fixture.path(QStringLiteral("incremental.pdf"));
            std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
            const QByteArray written = readFile(copy);
            QVERIFY2(written.startsWith(original), "The signed revision is not kept");
            QVERIFY(written.size() > original.size());
            QVERIFY2(isFirstSignatureValid(written), "The signature of the kept revision must verify");
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

            // An overwrite of such a tool says that the signatures stay; Deny writes nothing
            std::shared_ptr<pdf::PDFMcpCall> overwriteCall = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("overwrite")));
            std::shared_ptr<pdf::PDFAppMcpConfirmation> overwriteQuestion = fixture.confirmDialog();
            QVERIFY(overwriteQuestion);
            QVERIFY2(findFact(*overwriteQuestion, QStringLiteral("Signatures")).contains(QStringLiteral("kept")), qPrintable(findFact(*overwriteQuestion, QStringLiteral("Signatures"))));
            QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
            QVERIFY(pdftest::AppMcpFixture::waitResult(overwriteCall));
            QCOMPARE(readFile(file), original);

            // The question of keep warns of the signature (958:27533)
            QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
            std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep")));
            std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
            QVERIFY(confirmation);
            QVERIFY(confirmation->details.value(QStringLiteral("signatureCount")).toInt() > 0);
            QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
            result = pdftest::AppMcpFixture::waitResult(call);
            QVERIFY(result && !result->isError);
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        }
    }

    /// F8: the rule of O31 is repeated on the version a rerun works on
    static void signedAfterRerun()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("unsigned.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();

        const QString signedFile = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocumentPointer signedDocument(new pdf::PDFDocument(reader.readFromFile(signedFile)));
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

        const QString id = QStringLiteral("optimize.structure");
        ToolReplacement replacement(fixture.host(), id);
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep")));
        QVERIFY(fixture.confirmDialog());

        // The document has got a signed field while the question waited
        QVERIFY(document->commit(pdf::PDFModifiedDocument(signedDocument, document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset), QStringLiteral("Signed"), document->getVersion(), pdf::PDFAppChangeSource::User));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("The document is signed")), qPrintable(result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("New title"));

        // The row of the question says the failure
        bool isFailed = false;
        for (const pdf::PDFMcpActivityRow& row : rows(fixture, id))
        {
            isFailed = isFailed || (row.status == pdf::PDFMcpActivityStatus::Failed && row.detail.value(QStringLiteral("reason")).toString().startsWith(QStringLiteral("The document is signed")));
        }
        QVERIFY(isFailed);
    }

    /// The lock: Unavailable for every result, a change of any tool (O20); F5 of a display version
    static void lockAndDisplayVersion()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("locked.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        document->setLocked(true);
        const QByteArray original = readFile(file);

        {
            const QString id = QStringLiteral("pages.rotate");
            QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(id)->getLockedAccess(), pdf::PDFDocumentToolLockedAccess::Unavailable);
            ToolReplacement replacement(fixture.host(), id);
            const QString copy = fixture.path(QStringLiteral("locked-copy.pdf"));
            const std::vector<QJsonObject> calls = { arguments(id, QStringLiteral("title"), QStringLiteral("keep")),
                                                     arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy),
                                                     arguments(id, QStringLiteral("title"), QStringLiteral("overwrite")) };
            for (const QJsonObject& call : calls)
            {
                const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), call);
                QVERIFY(result && result->isError);
                QVERIFY2(result->getFirstText().contains(QStringLiteral("locked")), qPrintable(result->getFirstText()));
                QCOMPARE(replacement.tool->getExecuteCount(), 0);
            }
            QVERIFY(!QFile::exists(copy));
            QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedLocked));
        }

        // A tool, which a locked document may use, writes a copy; its keep is refused
        {
            const QString id = QStringLiteral("review.statistics");
            QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(id)->getLockedAccess(), pdf::PDFDocumentToolLockedAccess::Available);
            ToolReplacement replacement(fixture.host(), id);
            std::optional<pdf::PDFMcpCallResult> kept = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep")));
            QVERIFY(kept && kept->isError);
            QVERIFY2(kept->getFirstText().contains(QStringLiteral("locked")), qPrintable(kept->getFirstText()));
            const QString copy = fixture.path(QStringLiteral("locked-statistics.pdf"));
            const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
            QCOMPARE(titleOf(copy), QStringLiteral("New title"));
        }
        QCOMPARE(readFile(file), original);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        // The one rule of the shell and of MCP
        QCOMPARE(pdf::PDFAppToolResultWriter::getDocumentRefusal(pdf::PDFDocumentToolCatalog::findTool(u"pages.rotate"), document), pdf::PDFAppToolResultWriter::DocumentRefusal::Locked);
        QCOMPARE(pdf::PDFAppToolResultWriter::getDocumentRefusal(pdf::PDFDocumentToolCatalog::findTool(u"review.statistics"), document), pdf::PDFAppToolResultWriter::DocumentRefusal::None);
        document->setLocked(false);

        // A display version of an XFA form: changing tools refused before a run, an export runs
        const QString xfaFile = fixture.path(QStringLiteral("xfa.pdf"));
        {
            QFile xfa(xfaFile);
            QVERIFY(xfa.open(QIODevice::WriteOnly));
            xfa.write(pdftest::XFATestSamples::write(pdftest::XFATestSamples::createDocument({ pdftest::XFATestSamples::letter() }, pdftest::XFATestSamples::pagedTemplate(3))));
        }
        pdf::PDFAppDocumentTab* xfaTab = fixture.open(xfaFile);
        QVERIFY(xfaTab);
        QTRY_VERIFY(xfaTab->getDocument()->isReady());
        QVERIFY(xfaTab->getDocument()->isPagedXFA());
        const QString xfaId = xfaTab->getDocument()->getId();
        {
            const QString id = QStringLiteral("optimize.compress");
            ToolReplacement replacement(fixture.host(), id);
            QJsonObject call = arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), fixture.path(QStringLiteral("xfa-copy.pdf")));
            call.insert(QStringLiteral("documentId"), xfaId);
            const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), call);
            QVERIFY(result && result->isError);
            QVERIFY2(result->getFirstText().contains(QStringLiteral("XFA")), qPrintable(result->getFirstText()));
            QCOMPARE(replacement.tool->getExecuteCount(), 0);
        }
        {
            const QString id = QStringLiteral("convert.extract-text");
            ToolReplacement replacement(fixture.host(), id);
            QJsonObject call = arguments(id, QStringLiteral("files"));
            call.insert(QStringLiteral("documentId"), xfaId);
            const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), call);
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
            QCOMPARE(replacement.tool->getExecuteCount(), 1);
        }
    }

    /// The permissions of an encrypted document: one check of the core with the shell
    static void documentPermissions()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();

        // The sample does not allow changes of annotations: Remove External Links needs them
        const QString id = QStringLiteral("protect.remove-links");
        QVERIFY(!pdf::PDFDocumentToolCatalog::findTool(id)->isPermitted(document->getDocument().data()));
        ToolReplacement replacement(fixture.host(), id);
        const QString copy = fixture.path(QStringLiteral("restricted-copy.pdf"));
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
        QVERIFY(result && result->isError);
        QCOMPARE(replacement.tool->getExecuteCount(), 0);
        QVERIFY(!QFile::exists(copy));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Failed));

        // A tool, which asks for no permission, runs on the same document
        const QString statisticsId = QStringLiteral("review.statistics");
        ToolReplacement statistics(fixture.host(), statisticsId);
        const std::optional<pdf::PDFMcpCallResult> allowed = fixture.callTool(toolName(statisticsId), arguments(statisticsId, QStringLiteral("count")));
        QVERIFY(allowed);
        QVERIFY2(!allowed->isError, qPrintable(allowed->getFirstText()));
    }

    /// The offer of a tool by the permissions (chapter 6: R + E / R + F / R + E + F, return_text R)
    static void permissionsOfTheOffer()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("offer.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();

        using P = pdf::PDFAppMcpPermission;
        using K = pdf::PDFAppMcpPermissionKind;
        ToolReplacement convert(fixture.host(), QStringLiteral("convert.extract-text"));
        ToolReplacement rotate(fixture.host(), QStringLiteral("pages.rotate"));

        const std::vector<std::tuple<P, P, P, bool, bool>> table = {
            // Read, Edit, Files, convert offered, pages offered
            { P::Allow, P::Allow, P::Allow, true, true },
            { P::Allow, P::Allow, P::Deny, true, true },
            { P::Allow, P::Deny, P::Allow, true, true },
            { P::Allow, P::Deny, P::Deny, true, false },
            { P::Deny, P::Allow, P::Allow, false, false },
        };
        for (const auto& row : table)
        {
            QVERIFY(fixture.setPermission(K::Read, std::get<0>(row)));
            QVERIFY(fixture.setPermission(K::Edit, std::get<1>(row)));
            QVERIFY(fixture.setPermission(K::Files, std::get<2>(row)));
            QCOMPARE(fixture.host()->getCatalog()->findTool(u"convert") != nullptr, std::get<3>(row));
            QCOMPARE(fixture.host()->getCatalog()->findTool(u"pages") != nullptr, std::get<4>(row));
        }
        QVERIFY(fixture.setPermission(K::Read, P::Allow));

        // Edit = Deny: save_copy of a changing tool goes on, keep is refused before a run
        QVERIFY(fixture.setPermission(K::Edit, P::Deny));
        QVERIFY(fixture.setPermission(K::Files, P::Allow));
        const QString copy = fixture.path(QStringLiteral("offer-copy.pdf"));
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments(QStringLiteral("pages.rotate"), QStringLiteral("title"), QStringLiteral("save_copy"), copy));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY(QFile::exists(copy));
        const int runs = rotate.tool->getExecuteCount();
        result = fixture.callTool(QStringLiteral("pages"), arguments(QStringLiteral("pages.rotate"), QStringLiteral("title"), QStringLiteral("keep")));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("Edit documents")), qPrintable(result->getFirstText()));
        QCOMPARE(rotate.tool->getExecuteCount(), runs);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedPermission));

        // Both denied: a call of the tool, which is not offered, is an audited denial
        QVERIFY(fixture.setPermission(K::Files, P::Deny));
        result = fixture.callTool(QStringLiteral("pages"), arguments(QStringLiteral("pages.rotate"), QStringLiteral("title")));
        QVERIFY(result);
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedPermission));

        // Read = Ask: the approval of the reads comes first, also for a copy with Edit = Deny
        QVERIFY(fixture.setPermission(K::Read, P::Ask));
        QVERIFY(fixture.setPermission(K::Files, P::Allow));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("pages"), arguments(QStringLiteral("pages.rotate"), QStringLiteral("title"), QStringLiteral("save_copy"), fixture.path(QStringLiteral("asked.pdf"))));
        std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::ReadDocuments);
        QCOMPARE(rotate.tool->getExecuteCount(), runs);
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        QVERIFY(pdftest::AppMcpFixture::waitResult(call));
        QCOMPARE(rotate.tool->getExecuteCount(), runs);
    }

    /// return_text: the text of the output, only Read, data of the document with its origin
    static void returnText()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("text.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QString id = QStringLiteral("convert.extract-text");
        ToolReplacement replacement(fixture.host(), id);

        const pdf::PDFMcpToolDefinition* definition = fixture.host()->getCatalog()->findTool(u"convert");
        QVERIFY(definition);
        const QJsonObject properties = QJsonDocument::fromJson(definition->inputSchema).object().value(QStringLiteral("properties")).toObject();
        QCOMPARE(properties.value(QStringLiteral("result")).toObject().value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("return_text"), QStringLiteral("save_file") }));

        // Files = Deny and Edit = Deny do not touch it
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Deny));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Deny));
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QString(), QString(), QStringLiteral("The text of the page")));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        const QString block = result->getFirstText();
        QVERIFY2(block.startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()), qPrintable(block));
        QVERIFY2(block.endsWith(QStringLiteral("The text of the page")), qPrintable(block));
        QJsonObject data = output(id, *result);
        QCOMPARE(data.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
        QCOMPARE(data.value(QStringLiteral("mimeType")).toString(), QStringLiteral("text/plain"));
        QCOMPARE(data.value(QStringLiteral("size")).toInteger(), qint64(QByteArray("The text of the page").size()));
        QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), false);
        QCOMPARE(data.value(QStringLiteral("documentId")).toString(), document->getId());
        QCOMPARE(data.value(QStringLiteral("outputs")).toArray().first().toObject().value(QStringLiteral("suggestedFileName")).toString(), QStringLiteral("extracted.txt"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.confirmations()->shown.empty());

        // A path is no part of it
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("return_text"), fixture.path(QStringLiteral("no.txt"))));
        QVERIFY(result && result->isError);
        QVERIFY(!QFile::exists(fixture.path(QStringLiteral("no.txt"))));

        // At most 8 MiB, ended at a whole character, the whole size told
        const auto files = [](QByteArray data, const QString& mimeType)
        {
            pdf::PDFDocumentToolOutputFile file;
            file.suggestedFileName = QStringLiteral("extracted.txt");
            file.data = std::move(data);
            file.mimeType = mimeType;
            return std::vector<pdf::PDFDocumentToolOutputFile>({ file });
        };
        const qint64 maximum = pdf::PDFAppMcpToolSet::MAXIMUM_ATTACHMENT_BYTES;
        replacement.tool->setOutputFiles(files(QByteArray("a") + QByteArray("\xC5\xBE").repeated(maximum / 2 + 8), QStringLiteral("text/plain")));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText().left(200)));
        data = output(id, *result);
        QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), true);
        QCOMPARE(data.value(QStringLiteral("size")).toInteger(), 1 + 2 * (maximum / 2 + 8));
        QVERIFY(result->getFirstText().contains(QStringLiteral("save_file")));
        QVERIFY(result->getFirstText().endsWith(QChar(0x017E)));
        QVERIFY(!result->getFirstText().contains(QChar(0xFFFD)));

        // The limit exact: 8 MiB are a whole text, one byte more is cut
        replacement.tool->setOutputFiles(files(QByteArray(maximum, 'x'), QStringLiteral("text/plain")));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result && !result->isError);
        QCOMPARE(output(id, *result).value(QStringLiteral("truncated")).toBool(), false);
        QVERIFY(!result->getFirstText().contains(QStringLiteral("save_file")));
        replacement.tool->setOutputFiles(files(QByteArray(maximum + 1, 'x'), QStringLiteral("text/plain")));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result && !result->isError);
        QCOMPARE(output(id, *result).value(QStringLiteral("truncated")).toBool(), true);
        QCOMPARE(output(id, *result).value(QStringLiteral("size")).toInteger(), maximum + 1);
        QVERIFY(result->getFirstText().contains(QStringLiteral("save_file")));

        // A hostile text stays below the line of the untrusted content, without its controls;
        // a bidirectional mark of the text is content, as of document.read_attachment
        replacement.tool->setOutputFiles(files(QByteArray("before\x01\x1B" "after"), QStringLiteral("text/plain")));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result && !result->isError);
        QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()));
        QVERIFY2(result->getFirstText().endsWith(QStringLiteral("beforeafter")), qPrintable(result->getFirstText()));

        // A charset of the type
        replacement.tool->setOutputFiles(files(QByteArray("\xE9"), QStringLiteral("text/plain; charset=ISO-8859-1")));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY(result->getFirstText().endsWith(QChar(0x00E9)));

        // Bytes, which are no text of the type, and more files are said
        replacement.tool->setOutputFiles(files(QByteArray("\xFF\xFE\x00\x01", 4), QStringLiteral("text/plain")));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("not a text")), qPrintable(result->getFirstText()));

        std::vector<pdf::PDFDocumentToolOutputFile> twoFiles = files(QByteArray("a"), QStringLiteral("text/plain"));
        twoFiles.push_back(twoFiles.front());
        replacement.tool->setOutputFiles(twoFiles);
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files")));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("2 files")), qPrintable(result->getFirstText()));
        replacement.tool->setOutputFiles({ });

        // A tool of the kind, which changes the document instead, is not delivered silently
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("title")));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("return_text")), qPrintable(result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    }

    /// save_file: the bytes of the output, T1, T2 and the extensions of F9
    static void saveFile()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("file.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QString id = QStringLiteral("convert.extract-text");
        ToolReplacement replacement(fixture.host(), id);

        // The bytes, also of another case of the extension; the path comes back
        const QString target = fixture.path(QStringLiteral("extracted.TXT"));
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), target, QStringLiteral("Saved text")));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(readFile(target), QByteArray("Saved text"));
        QVERIFY(output(id, *result).value(QStringLiteral("path")).toString().endsWith(QStringLiteral("extracted.TXT")));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        // Never over a file
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), target, QStringLiteral("Second")));
        QVERIFY(result && result->isError);
        QCOMPARE(readFile(target), QByteArray("Saved text"));

        // F9: a text of a document never becomes a program, nor a PDF; the check is the
        // extension of the output, not a list of the bad ones
        const QStringList refusedExtensions = { QStringLiteral("bat"), QStringLiteral("exe"), QStringLiteral("cmd"), QStringLiteral("ps1"), QStringLiteral("pdf"), QStringLiteral("txt.bat"), QStringLiteral("html") };
        for (const QString& extension : refusedExtensions)
        {
            const QString path = fixture.path(QStringLiteral("refused.") + extension);
            result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), path));
            QVERIFY(result);
            QVERIFY2(result->isError, qPrintable(path));
            QVERIFY2(result->getFirstText().contains(QStringLiteral("extension")), qPrintable(result->getFirstText()));
            QVERIFY2(!QFile::exists(path), qPrintable(path));
        }

        // The extension follows the output (format html of the tool)
        pdf::PDFDocumentToolOutputFile html;
        html.suggestedFileName = QStringLiteral("extracted.html");
        html.data = QByteArray("<p>x</p>");
        html.mimeType = QStringLiteral("text/html");
        replacement.tool->setOutputFiles({ html });
        const QString htmlPath = fixture.path(QStringLiteral("page.htm"));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), htmlPath));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(readFile(htmlPath), html.data);
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), fixture.path(QStringLiteral("page.txt"))));
        QVERIFY(result && result->isError);
        replacement.tool->setOutputFiles({ });

        // T1, T2 and the other rules of the paths, before a run
        const int runs = replacement.tool->getExecuteCount();
        QStringList refusedPaths = { QStringLiteral("relative.txt"), fixture.path(QStringLiteral("missing/folder/x.txt")), fixture.path(QStringLiteral("NUL.txt")),
                                     QStringLiteral("//server/share/x.txt"), fixture.path(QString::fromUtf8("name\xE2\x80\xAEtxt.txt")) };
#ifdef Q_OS_WIN
        refusedPaths << fixture.path(QStringLiteral("a.txt:stream")) << fixture.path(QStringLiteral("a.txt."));
#endif
        for (const QString& path : refusedPaths)
        {
            result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), path));
            QVERIFY(result);
            QVERIFY2(result->isError, qPrintable(path));
            QVERIFY2(result->getFirstText().startsWith(QStringLiteral("path:")) || result->getFirstText().contains(QStringLiteral("path")), qPrintable(result->getFirstText()));
        }
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file")));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("save_file needs an absolute path")), qPrintable(result->getFirstText()));
        QCOMPARE(replacement.tool->getExecuteCount(), runs);

        // Files = Deny: before a run; Ask: the question of a file, Deny writes nothing
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Deny));
        const QString deniedPath = fixture.path(QStringLiteral("denied.txt"));
        result = fixture.callTool(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), deniedPath));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("Open and save files")), qPrintable(result->getFirstText()));
        QCOMPARE(replacement.tool->getExecuteCount(), runs);

        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), deniedPath));
        std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QCOMPARE(confirmation->kind, pdf::PDFAppMcpConfirmationKind::SaveCopy);
        QVERIFY(confirmation->permissions.testFlag(pdf::PDFAppMcpPermissionKind::Files));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY(!QFile::exists(deniedPath));

        // Allow; a file, which appeared during the question, is not replaced
        call = fixture.startCall(QStringLiteral("convert"), arguments(id, QStringLiteral("files"), QStringLiteral("save_file"), deniedPath));
        QVERIFY(fixture.confirmDialog());
        {
            QFile appeared(deniedPath);
            QVERIFY(appeared.open(QIODevice::WriteOnly));
            appeared.write("appeared");
        }
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("exists")), qPrintable(result->getFirstText()));
        QCOMPARE(readFile(deniedPath), QByteArray("appeared"));
    }

    /// Cancellation and the progress of a run
    static void cancelAndProgress()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("cancel.pdf"), 5));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        // A closed stream stops a run: nothing committed, nothing written
        replacement.tool->setHold(true);
        const QString copy = fixture.path(QStringLiteral("canceled.pdf"));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
        QTRY_VERIFY(replacement.tool->getHeldCount() > 0);
        call->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        replacement.tool->setHold(false);
        QVERIFY(!QFile::exists(copy));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));

        // A tool, which ends with a result although the call was canceled during its run
        // (it has not looked at the cancellation): the result is dropped
        std::shared_ptr<pdf::PDFMcpCall> lateCall;
        QPointer<pdf::PDFAppDocument> documentGuard(document);
        replacement.tool->setExecuteCallback([&lateCall, documentGuard]()
        {
            QMetaObject::invokeMethod(documentGuard.data(), [&lateCall]() { lateCall->cancel(pdf::PDFMcpCancelReason::StreamClosed); }, Qt::BlockingQueuedConnection);
        });
        lateCall = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Late")));
        result = pdftest::AppMcpFixture::waitResult(lateCall);
        QVERIFY(result && result->isError);
        replacement.tool->setExecuteCallback(nullptr);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("Late"));

        // The progress grows strictly and ends by the result
        QJsonObject counting = arguments(id, QStringLiteral("count"));
        QJsonObject parameters = counting.value(actionName(id)).toObject();
        parameters.insert(QStringLiteral("repeat"), 50);
        counting.insert(actionName(id), parameters);
        call = fixture.startCall(toolName(id), counting, true);
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        double last = -1.0;
        int progressEvents = 0;
        for (const pdf::PDFMcpCallEvent& event : call->takeEvents())
        {
            if (event.type == pdf::PDFMcpCallEvent::Type::Progress)
            {
                QVERIFY(event.progress > last);
                last = event.progress;
                ++progressEvents;
            }
        }
        QVERIFY(progressEvents > 1);
    }

    /// A new version of the document: the tool runs again, at most 3 times (plan 2.5)
    static void rerunsOnNewVersions()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("rerun.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        // A change of the document in the thread of the user interface at the start of the
        // first runs of the tool, while the run works on the older snapshot
        int changesLeft = 2;
        QPointer<pdf::PDFAppDocument> documentGuard(document);
        replacement.tool->setExecuteCallback([&changesLeft, documentGuard]()
        {
            QMetaObject::invokeMethod(documentGuard.data(), [&changesLeft, documentGuard]()
            {
                if (changesLeft > 0 && documentGuard)
                {
                    --changesLeft;
                    documentGuard->commit(pdf::PDFModifiedDocument(documentGuard->getDocument(), documentGuard->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset),
                                          QStringLiteral("User change"), documentGuard->getVersion(), pdf::PDFAppChangeSource::User);
                }
            }, Qt::BlockingQueuedConnection);
        });

        // Two changes under the runs: the third run commits on the current version
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Third")));
        std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(replacement.tool->getExecuteCount(), 3);
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Third"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);

        // A document, which changes under every run: the third one ends by an error
        changesLeft = 3;
        call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Never")));
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("changed in the meantime")), qPrintable(result->getFirstText()));
        QCOMPARE(replacement.tool->getExecuteCount(), 6);
        QCOMPARE(changesLeft, 0);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 6);
        QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("Never"));
        replacement.tool->setExecuteCallback(nullptr);
    }

    /// M3: the row of a call, which has not ended, does not age; the host reports the end
    static void activityOfLongCall()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("long.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);
        pdf::PDFAppMcpActivityLog* log = fixture.host()->getActivityLog();
        QVERIFY(log);
        const qint64 start = QDateTime::currentMSecsSinceEpoch();
        log->setCurrentTime(start);

        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        replacement.tool->setHold(false);
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep")));
        std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);

        // Allowed, and the document changes: the tool runs again for more than KEEP_TIME
        replacement.tool->setHold(true);
        QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset),
                                 QStringLiteral("User change"), document->getVersion(), pdf::PDFAppChangeSource::User));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        QTRY_VERIFY(replacement.tool->getHeldCount() > 0);
        log->flush();

        // Other rows of an assistant arrive meanwhile, which forget the old rows
        const qint64 later = start + pdf::PDFAppMcpActivityLog::KEEP_TIME + 60000;
        log->setCurrentTime(later);
        pdf::PDFAppMcpActivityEntry other;
        other.assistantKey = QStringLiteral("a:2");
        other.client = QStringLiteral("Other");
        other.action = QStringLiteral("files.save_copy");
        other.status = pdf::PDFMcpActivityStatus::Ok;
        QVERIFY(log->record(other) > 0);
        log->flush();

        // The long call fails at the end: its row of the question says so
        pdftest::AppDocumentFixture::settle();
        document->setLocked(true);
        replacement.tool->setHold(false);
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);

        log->flush();
        bool isDeniedLocked = false;
        for (const pdf::PDFMcpActivityRow& row : rows(fixture, id))
        {
            isDeniedLocked = isDeniedLocked || row.status == pdf::PDFMcpActivityStatus::DeniedLocked;
            QVERIFY2(row.status != pdf::PDFMcpActivityStatus::Allowed, "The row of a long call kept its old status");
        }
        QVERIFY(isDeniedLocked);

        // The host reports the end of the call: the row ages from now on. KEEP_TIME later a new
        // row is the only one left
        pdftest::AppDocumentFixture::settle();
        log->flush();
        log->setCurrentTime(later + pdf::PDFAppMcpActivityLog::KEEP_TIME + 60000);
        QVERIFY(log->record(other) > 0);
        QCOMPARE(log->getRowCount(), size_t(1));

        log->setCurrentTime(0);
        document->setLocked(false);
    }

    /// Compress itself end to end over HTTP in both eras: a smaller file, the document of
    /// the tab unchanged; one undo step with keep
    static void compressOverHttp()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        if (!fixture.startServer())
        {
            QSKIP("The MCP server does not start");
        }

        const QString file = createImageDocument(fixture.folder(), QStringLiteral("images.pdf"));
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QByteArray original = readFile(file);
        const qint64 originalSize = original.size();

        // The tool of the catalog is real
        QVERIFY(std::dynamic_pointer_cast<const pdf::PDFDocumentToolCompress>(pdf::PDFDocumentToolRegistry::getTool(u"optimize.compress")));

        int fileIndex = 0;
        for (pdftest::McpTestEra era : { pdftest::McpTestEra::Modern, pdftest::McpTestEra::Legacy20251125, pdftest::McpTestEra::Legacy20250618 })
        {
            std::unique_ptr<pdftest::McpTestClient> client = fixture.client(era);
            if (era != pdftest::McpTestEra::Modern)
            {
                QVERIFY(!client->initialize().isEmpty());
            }

            // Only the categories with an implementation are listed, closed schemas
            QStringList listed;
            QJsonObject optimize;
            for (const QJsonValue& value : client->listTools().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray())
            {
                listed << value.toObject().value(QStringLiteral("name")).toString();
                if (listed.back() == QLatin1String("optimize"))
                {
                    optimize = value.toObject();
                }
            }
            for (const pdf::PDFDocumentToolCategoryInfo& category : pdf::PDFDocumentToolCatalog::getCategories())
            {
                QCOMPARE(listed.contains(category.getId()), !pdf::PDFAppMcpCatalogTools::getImplementedTools(category.getCategory()).empty());
            }
            const QJsonObject properties = optimize.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("properties")).toObject();
            QVERIFY(properties.value(QStringLiteral("action")).toObject().value(QStringLiteral("enum")).toArray().contains(QStringLiteral("compress")));
            QCOMPARE(properties.value(QStringLiteral("result")).toObject().value(QStringLiteral("enum")).toArray(), QJsonArray({ QStringLiteral("keep"), QStringLiteral("save_copy"), QStringLiteral("overwrite") }));
            QCOMPARE(properties.value(QStringLiteral("compress")).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("preset")).toObject().value(QStringLiteral("enum")).toArray(),
                     QJsonArray({ QStringLiteral("smaller-file"), QStringLiteral("balanced"), QStringLiteral("best-quality"), QStringLiteral("custom") }));
            QVERIFY(optimize.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("destructiveHint")).toBool());
            QCOMPARE(optimize.contains(QStringLiteral("outputSchema")), true);

            // save_copy, the default: smaller, the tab unchanged, the structure of the result
            const QString copy = fixture.path(QStringLiteral("smaller-%1.pdf").arg(++fileIndex));
            const QJsonObject response = client->callTool(QStringLiteral("optimize"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("compress") },
                                                                                                    { QStringLiteral("documentId"), document->getId() },
                                                                                                    { QStringLiteral("path"), copy },
                                                                                                    { QStringLiteral("compress"), QJsonObject({ { QStringLiteral("preset"), QStringLiteral("smaller-file") } }) } }));
            const QJsonObject callResult = response.value(QStringLiteral("result")).toObject();
            QVERIFY2(!callResult.value(QStringLiteral("isError")).toBool(), QJsonDocument(response).toJson(QJsonDocument::Compact).left(600).constData());
            const QJsonObject compress = callResult.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("compress")).toObject();
            QCOMPARE(compress.value(QStringLiteral("status")).toString(), QStringLiteral("success"));
            // The image of 1024 dpi goes to 110 dpi: the file is the result, not the input
            // written again; the file has the size, which the tool has measured
            const qint64 written = QFileInfo(copy).size();
            QVERIFY2(written > 0 && written * 2 < originalSize, qPrintable(QStringLiteral("%1 -> %2").arg(originalSize).arg(written)));
            QCOMPARE(compress.value(QStringLiteral("data")).toObject().value(QStringLiteral("sizeAfter")).toInteger(), written);
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
            QVERIFY(!document->isModified());
            QCOMPARE(readFile(file), original);
            pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
            const pdf::PDFDocument copied = reader.readFromFile(copy);
            QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
            QCOMPARE(copied.getCatalog()->getPageCount(), document->getDocument()->getCatalog()->getPageCount());
        }

        // keep: one undo step of the assistant, smaller in the memory, the file unchanged
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("optimize"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("compress") }, { QStringLiteral("result"), QStringLiteral("keep") },
                                                                                                                  { QStringLiteral("compress"), QJsonObject({ { QStringLiteral("preset"), QStringLiteral("smaller-file") } }) } }));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QVERIFY(document->isModified());
        QCOMPARE(readFile(file), original);
        QVERIFY(document->getUndoStack()->getUndoDescription().contains(pdf::PDFDocumentToolCatalog::findTool(u"optimize.compress")->getTitle()));

        // overwrite over HTTP: the question answered, the file is the result
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
        QVERIFY(!document->isModified());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
        result = fixture.callTool(QStringLiteral("optimize"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("compress") }, { QStringLiteral("result"), QStringLiteral("overwrite") },
                                                                             { QStringLiteral("compress"), QJsonObject({ { QStringLiteral("preset"), QStringLiteral("smaller-file") } }) } }));
        QVERIFY(result);
        QVERIFY2(result->kind == pdf::PDFMcpCallResult::Kind::Complete && !result->isError, qPrintable(result->getFirstText()));
        QVERIFY(QFileInfo(file).size() < originalSize);
        QVERIFY(!document->isModified());
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    }

    /// A tool out of memory over HTTP in the three eras: the assistant gets the error text only -
    /// the answer has the members of any other failure -, no dialog opens in the application, and
    /// the text does not advise a setting, which is on already (owner 2026-11-04)
    static void outOfMemoryOverHttp()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        if (!fixture.startServer())
        {
            QSKIP("The MCP server does not start");
        }

        const QString rotateId = QStringLiteral("pages.rotate");
        ToolReplacement rotate(fixture.host(), rotateId);
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("memory.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const quint64 version = document->getVersion();
        const auto restore = qScopeGuard([]() { pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded); });

        const auto call = [&](pdftest::McpTestClient* client, const QString& mode)
        {
            QJsonObject callArguments = arguments(rotateId, mode, QStringLiteral("keep"));
            callArguments.insert(QStringLiteral("documentId"), document->getId());
            return client->callTool(toolName(rotateId), callArguments).value(QStringLiteral("result")).toObject();
        };

        for (pdftest::McpTestEra era : { pdftest::McpTestEra::Modern, pdftest::McpTestEra::Legacy20251125, pdftest::McpTestEra::Legacy20250618 })
        {
            std::unique_ptr<pdftest::McpTestClient> client = fixture.client(era);
            if (era != pdftest::McpTestEra::Modern)
            {
                QVERIFY(!client->initialize().isEmpty());
            }

            pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded);
            const QJsonObject otherFailure = call(client.get(), QStringLiteral("fail"));
            QVERIFY(otherFailure.value(QStringLiteral("isError")).toBool());
            const QJsonObject failure = call(client.get(), QStringLiteral("throw-memory"));
            const QByteArray bytes = QJsonDocument(failure).toJson(QJsonDocument::Compact);
            QVERIFY2(failure.value(QStringLiteral("isError")).toBool(), bytes.constData());

            // The same members, the text is the only difference
            QCOMPARE(failure.keys(), otherFailure.keys());
            const QJsonArray content = failure.value(QStringLiteral("content")).toArray();
            QCOMPARE(content.size(), otherFailure.value(QStringLiteral("content")).toArray().size());
            QCOMPARE(content.first().toObject().keys(), otherFailure.value(QStringLiteral("content")).toArray().first().toObject().keys());
            const QString text = content.first().toObject().value(QStringLiteral("text")).toString();
            QVERIFY2(text.contains(QStringLiteral("not enough memory")) && text.contains(QStringLiteral("Multithreading strategy")), qPrintable(text));
            QVERIFY(!bytes.contains("failureKind"));
            QVERIFY(!bytes.contains("outOfMemory"));

            // Single thread: no advice
            pdf::PDFExecutionPolicy::setStrategy(pdf::PDFExecutionPolicy::Strategy::SingleThreaded);
            const QString singleText = call(client.get(), QStringLiteral("throw-memory")).value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString();
            QVERIFY2(singleText.contains(QStringLiteral("not enough memory")), qPrintable(singleText));
            QVERIFY2(!singleText.contains(QStringLiteral("Multithreading strategy")) && !singleText.contains(QStringLiteral("Single thread")), qPrintable(singleText));

            // Nothing in the application: no dialog, the document as it was
            pdftest::AppDocumentFixture::settle();
            QVERIFY(!fixture.window->findChild<pdf::PDFAppToolOutOfMemoryDialog*>());
            QVERIFY(!QApplication::activeModalWidget());
            QCOMPARE(document->getVersion(), version);
        }
    }

    /// Every result and every refusal over HTTP in both eras, the question answered,
    /// the progress growing on the stream, a closed stream stopping the run
    static void bridgeOverHttp()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        if (!fixture.startServer())
        {
            QSKIP("The MCP server does not start");
        }
        fixture.setTransport(pdftest::AppMcpTransport::Http);

        const QString rotateId = QStringLiteral("pages.rotate");
        const QString compressId = QStringLiteral("optimize.compress");
        const QString linksId = QStringLiteral("protect.remove-links");
        const QString extractId = QStringLiteral("convert.extract-text");
        ToolReplacement rotate(fixture.host(), rotateId);
        ToolReplacement compress(fixture.host(), compressId);
        ToolReplacement links(fixture.host(), linksId);
        ToolReplacement extract(fixture.host(), extractId);

        const auto withDocument = [](QJsonObject call, const pdf::PDFAppDocument* document)
        {
            call.insert(QStringLiteral("documentId"), document->getId());
            return call;
        };
        const auto text = [](const std::optional<pdf::PDFMcpCallResult>& result) { return result ? QString::fromUtf8(result->toQJson().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString().toUtf8()) : QString(); };

        int index = 0;
        for (const pdf::PDFMcpEra era : { pdf::PDFMcpEra::Modern, pdf::PDFMcpEra::Legacy20251125 })
        {
            ++index;
            fixture.setClient(QStringLiteral("claude-code"), era);
            const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("http-%1.pdf").arg(index), 5);
            pdf::PDFAppDocumentTab* tab = fixture.open(file);
            QVERIFY(tab);
            QTRY_VERIFY(tab->getDocument()->isReady());
            pdf::PDFAppDocument* document = tab->getDocument();
            const QByteArray original = readFile(file);
            const QString eraName = era == pdf::PDFMcpEra::Modern ? QStringLiteral("modern") : QStringLiteral("legacy");

            // keep
            std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Kept")), document));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(eraName + text(result)));
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
            QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Kept"));
            QCOMPARE(readFile(file), original);

            // save_copy: the tab is unchanged
            const QString copy = fixture.path(QStringLiteral("http-copy-%1.pdf").arg(index));
            result = fixture.callTool(toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("save_copy"), copy, QStringLiteral("Copied")), document));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(eraName + text(result)));
            QCOMPARE(titleOf(copy), QStringLiteral("Copied"));
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

            // overwrite: the question, Deny and Allow
            fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);
            result = fixture.callTool(toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Denied")), document));
            QVERIFY(result && result->isError);
            QCOMPARE(readFile(file), original);
            fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
            result = fixture.callTool(toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Overwritten")), document));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(eraName + text(result)));
            QCOMPARE(titleOf(file), QStringLiteral("Overwritten"));
            QVERIFY(!document->isModified());
            fixture.confirmations()->setAutoAnswer(std::nullopt);

            // The lock
            document->setLocked(true);
            result = fixture.callTool(toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("keep")), document));
            QVERIFY(result && result->isError);
            QVERIFY2(text(result).contains(QStringLiteral("locked")), qPrintable(text(result)));
            document->setLocked(false);

            // return_text and save_file of an output, F9
            result = fixture.callTool(toolName(extractId), withDocument(arguments(extractId, QStringLiteral("files"), QString(), QString(), QStringLiteral("Extracted text")), document));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(eraName + text(result)));
            QVERIFY2(text(result).endsWith(QStringLiteral("Extracted text")), qPrintable(text(result)));
            const QString textFile = fixture.path(QStringLiteral("http-%1.txt").arg(index));
            result = fixture.callTool(toolName(extractId), withDocument(arguments(extractId, QStringLiteral("files"), QStringLiteral("save_file"), textFile, QStringLiteral("Saved")), document));
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(eraName + text(result)));
            QCOMPARE(readFile(textFile), QByteArray("Saved"));
            result = fixture.callTool(toolName(extractId), withDocument(arguments(extractId, QStringLiteral("files"), QStringLiteral("save_file"), fixture.path(QStringLiteral("http-%1.bat").arg(index))), document));
            QVERIFY(result && result->isError);
            QVERIFY(!QFile::exists(fixture.path(QStringLiteral("http-%1.bat").arg(index))));

            // The progress on the stream of the call, growing
            std::unique_ptr<pdftest::McpTestClient> client = fixture.client(era == pdf::PDFMcpEra::Modern ? pdftest::McpTestEra::Modern : pdftest::McpTestEra::Legacy20251125);
            if (era != pdf::PDFMcpEra::Modern)
            {
                QVERIFY(!client->initialize().isEmpty());
            }
            QJsonObject counting = arguments(rotateId, QStringLiteral("count"));
            QJsonObject parameters = counting.value(actionName(rotateId)).toObject();
            parameters.insert(QStringLiteral("repeat"), 100);
            counting.insert(actionName(rotateId), parameters);
            const QJsonObject response = client->callTool(toolName(rotateId), withDocument(counting, document), QJsonObject({ { QStringLiteral("progressToken"), QStringLiteral("p") } }));
            QVERIFY2(!response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool(), QJsonDocument(response).toJson(QJsonDocument::Compact).left(400).constData());
            double last = -1.0;
            int progressMessages = 0;
            for (const QJsonObject& message : client->lastResponse().messages())
            {
                if (message.value(QStringLiteral("method")).toString() == QLatin1String("notifications/progress"))
                {
                    const double progress = message.value(QStringLiteral("params")).toObject().value(QStringLiteral("progress")).toDouble();
                    QVERIFY(progress > last);
                    last = progress;
                    ++progressMessages;
                }
            }
            QVERIFY2(progressMessages >= 1, qPrintable(eraName));

            // A closed stream stops the run: nothing written, nothing committed
            rotate.tool->setHold(true);
            const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
            const QString canceledCopy = fixture.path(QStringLiteral("http-canceled-%1.pdf").arg(index));
            std::unique_ptr<pdftest::McpTestStream> stream = client->openStream(client->createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), toolName(rotateId) },
                { QStringLiteral("arguments"), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("save_copy"), canceledCopy), document) } })));
            QVERIFY(stream);
            QTRY_VERIFY(rotate.tool->getHeldCount() > 0);
            stream->close();
            QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));
            QTRY_COMPARE(rotate.tool->getHeldCount(), 0);
            rotate.tool->setHold(false);
            QVERIFY(!QFile::exists(canceledCopy));
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore);
        }

        // O31 and the permissions of a document over HTTP (both are errors of the tool, in one era each)
        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Modern);
        pdf::PDFAppDocumentTab* signedTab = fixture.open(pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
        QVERIFY(signedTab);
        QTRY_VERIFY(signedTab->getDocument()->isReady());
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(compressId), withDocument(arguments(compressId, QStringLiteral("title"), QStringLiteral("keep")), signedTab->getDocument()));
        QVERIFY(result && result->isError);
        QVERIFY2(text(result).startsWith(QStringLiteral("The document is signed")), qPrintable(text(result)));

        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Legacy20251125);
        pdf::PDFAppDocumentTab* restrictedTab = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
        QVERIFY(restrictedTab);
        QTRY_VERIFY(restrictedTab->getDocument()->isReady());
        result = fixture.callTool(toolName(linksId), withDocument(arguments(linksId, QStringLiteral("title"), QStringLiteral("keep")), restrictedTab->getDocument()));
        QVERIFY(result && result->isError);
        QCOMPARE(links.tool->getExecuteCount(), 0);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(restrictedTab->getDocument()), 0);
    }

    /// A written file takes over the Mark of the Web of the document (O18)
    static void markOfTheWeb()
    {
#ifdef Q_OS_WIN
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("marked.pdf"), 2);
        const QByteArray mark("[ZoneTransfer]\r\nZoneId=3\r\n");
        {
            QFile stream(file + QStringLiteral(":Zone.Identifier"));
            if (!stream.open(QIODevice::WriteOnly))
            {
                QSKIP("The file system has no alternate data streams");
            }
            stream.write(mark);
        }
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());

        const QString rotateId = QStringLiteral("pages.rotate");
        const QString extractId = QStringLiteral("convert.extract-text");
        ToolReplacement rotate(fixture.host(), rotateId);
        ToolReplacement extract(fixture.host(), extractId);

        const QString copy = fixture.path(QStringLiteral("marked-copy.pdf"));
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(rotateId), arguments(rotateId, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
        QVERIFY(result && !result->isError);
        QCOMPARE(readFile(copy + QStringLiteral(":Zone.Identifier")), mark);

        const QString text = fixture.path(QStringLiteral("marked.txt"));
        result = fixture.callTool(toolName(extractId), arguments(extractId, QStringLiteral("files"), QStringLiteral("save_file"), text));
        QVERIFY(result && !result->isError);
        QCOMPARE(readFile(text + QStringLiteral(":Zone.Identifier")), mark);

        // The mark comes from the origin of the document, not from its own file: Edit a
        // Copy has no file, its origin is the marked file (O18)
        QVERIFY(tab->executeCommand(pdf::PDFAppCommands::editCopy()));
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppDocumentTab* copyTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
        QVERIFY(copyTab && copyTab != tab);
        QTRY_VERIFY(copyTab->getDocument()->isReady());
        QVERIFY(copyTab->getDocument()->getFilePath().isEmpty());
        QJsonObject copyCall = arguments(rotateId, QStringLiteral("title"), QStringLiteral("save_copy"), fixture.path(QStringLiteral("marked-copy-of-copy.pdf")));
        copyCall.insert(QStringLiteral("documentId"), copyTab->getDocument()->getId());
        result = fixture.callTool(toolName(rotateId), copyCall);
        QVERIFY(result && !result->isError);
        QCOMPARE(readFile(fixture.path(QStringLiteral("marked-copy-of-copy.pdf")) + QStringLiteral(":Zone.Identifier")), mark);

        // Overwrite: the replaced file keeps its mark (a new file has no stream)
        QJsonObject overwriteCall = arguments(rotateId, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Overwritten"));
        overwriteCall.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
        fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
        result = fixture.callTool(toolName(rotateId), overwriteCall);
        fixture.confirmations()->setAutoAnswer(std::nullopt);
        QVERIFY(result && !result->isError);
        QCOMPARE(titleOf(file), QStringLiteral("Overwritten"));
        QCOMPARE(readFile(file + QStringLiteral(":Zone.Identifier")), mark);
#else
        QSKIP("The Mark of the Web exists only on Windows");
#endif
    }

    /// An encrypted input: the copy asks for its password and opens by it
    static void encryptedCopy()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::userPassword(fixture.folder()));
        QVERIFY(tab);
        QTRY_COMPARE(tab->getDocument()->getState(), pdf::PDFAppDocumentState::PasswordRequired);
        QVERIFY(tab->getLoader()->setPassword(pdftest::AppDocumentSamples::getUserPassword()));
        QTRY_VERIFY(tab->getDocument()->isReady());

        // The bridge writes the copy by the handler of the document (the test tool always has a result)
        const QString copy = fixture.path(QStringLiteral("encrypted-copy.pdf"));
        const QString id = QStringLiteral("optimize.compress");
        ToolReplacement replacement(fixture.host(), id);
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY(readFile(copy).contains("/Encrypt"));

        const auto noPassword = [](bool* ok)
        {
            *ok = false;
            return QString();
        };
        pdf::PDFDocumentReader withoutPassword(nullptr, noPassword, false, false);
        withoutPassword.readFromFile(copy);
        QVERIFY(withoutPassword.getReadingResult() != pdf::PDFDocumentReader::Result::OK);

        const auto password = [](bool* ok)
        {
            *ok = true;
            return pdftest::AppDocumentSamples::getUserPassword();
        };
        pdf::PDFDocumentReader withPassword(nullptr, password, false, false);
        const pdf::PDFDocument opened = withPassword.readFromFile(copy);
        QCOMPARE(withPassword.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        QVERIFY(opened.getCatalog()->getPageCount() > 0);
        QCOMPARE(opened.getInfo()->title, QStringLiteral("New title"));
    }

    /// The security boundary: every combination of Read, Edit and Files for every value of
    /// result against the table of chapter 6 (R + E, R + F, R + E + F always confirmed; R;
    /// R + F): what is offered, what a call does, which questions it asks, what it touches
    static void permissionMatrix()
    {
        using P = pdf::PDFAppMcpPermission;
        using K = pdf::PDFAppMcpPermissionKind;
        using C = pdf::PDFAppMcpConfirmationKind;

        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(P::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("matrix.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        pdftest::TestConfirmationProvider* confirmations = fixture.confirmations();
        confirmations->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);

        const QString rotateId = QStringLiteral("pages.rotate");
        const QString extractId = QStringLiteral("convert.extract-text");
        ToolReplacement rotate(fixture.host(), rotateId);
        ToolReplacement extract(fixture.host(), extractId);

        const QString assistantKey = fixture.createRequest(QStringLiteral("pages"), QJsonObject()).client.assistantKey;
        pdf::PDFMcpAssistantEvent forget;
        forget.kind = pdf::PDFMcpAssistantEvent::Kind::Idle;
        forget.key = assistantKey;

        class Case
        {
        public:
            const char* value;
            bool isExport;
            bool needsEdit;
            bool needsFiles;
            bool isPath;
        };
        const std::vector<Case> cases = { { "keep", false, true, false, false }, { "save_copy", false, false, true, true }, { "overwrite", false, true, true, false },
                                          { "return_text", true, false, false, false }, { "save_file", true, false, true, true } };

        int index = 0;
        for (const P read : { P::Allow, P::Ask, P::Deny })
        {
            for (const P edit : { P::Allow, P::Ask, P::Deny })
            {
                for (const P files : { P::Allow, P::Ask, P::Deny })
                {
                    // Set to Allow first: the steps never leave the tool without a catalog
                    QVERIFY(fixture.setPermission(K::Read, read));
                    QVERIFY(fixture.setPermission(K::Edit, edit));
                    QVERIFY(fixture.setPermission(K::Files, files));
                    const QString name = QStringLiteral("R%1 E%2 F%3").arg(int(read)).arg(int(edit)).arg(int(files));

                    // The offer of tools/list
                    const bool isChangeOffered = read != P::Deny && (edit != P::Deny || files != P::Deny);
                    QVERIFY2((fixture.host()->getCatalog()->findTool(u"pages") != nullptr) == isChangeOffered, qPrintable(name));
                    QVERIFY2((fixture.host()->getCatalog()->findTool(u"convert") != nullptr) == (read != P::Deny), qPrintable(name));

                    for (const Case& testCase : cases)
                    {
                        const QString value = QString::fromLatin1(testCase.value);
                        const QString context = name + QLatin1Char(' ') + value;
                        ++index;
                        fixture.host()->assistantEvent(forget);

                        const QString id = testCase.isExport ? extractId : rotateId;
                        const std::shared_ptr<pdftest::TestDocumentTool>& tool = testCase.isExport ? extract.tool : rotate.tool;
                        const QString title = QStringLiteral("Title %1").arg(index);
                        const QString path = testCase.isPath ? fixture.path(QStringLiteral("matrix-%1.%2").arg(index).arg(testCase.isExport ? QStringLiteral("txt") : QStringLiteral("pdf"))) : QString();

                        const int runsBefore = tool->getExecuteCount();
                        const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
                        const QByteArray fileBefore = readFile(file);
                        const size_t shownBefore = confirmations->shown.size();
                        const QString titleBefore = document->getDocument()->getInfo()->title;

                        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, testCase.isExport ? QStringLiteral("files") : QStringLiteral("title"), value, path, title));
                        QVERIFY2(result, qPrintable(context));

                        const bool isDenied = read == P::Deny || (testCase.needsEdit && edit == P::Deny) || (testCase.needsFiles && files == P::Deny);
                        std::vector<C> asked;
                        for (size_t i = shownBefore; i < confirmations->shown.size(); ++i)
                        {
                            asked.push_back(confirmations->shown[i]->kind);
                        }

                        if (isDenied)
                        {
                            // Nothing runs, nothing is written, nothing changes; at most the read approval is asked
                            QVERIFY2(result->kind == pdf::PDFMcpCallResult::Kind::Error || result->isError, qPrintable(context));
                            QVERIFY2(tool->getExecuteCount() == runsBefore, qPrintable(context));
                            QVERIFY2(pdftest::AppDocumentFixture::undoCount(document) == undoBefore, qPrintable(context));
                            QVERIFY2(document->getDocument()->getInfo()->title == titleBefore, qPrintable(context));
                            QVERIFY2(readFile(file) == fileBefore, qPrintable(context));
                            QVERIFY2(path.isEmpty() || !QFile::exists(path), qPrintable(context));
                            QVERIFY2(asked.size() <= 1 && (asked.empty() || asked.front() == C::ReadDocuments), qPrintable(context));
                            continue;
                        }

                        QVERIFY2(!result->isError, qPrintable(context + QStringLiteral(": ") + result->getFirstText()));
                        QVERIFY2(tool->getExecuteCount() == runsBefore + 1, qPrintable(context));

                        // The questions: the read approval first, then the one of the result
                        std::vector<C> expected;
                        pdf::PDFAppMcpPermissionKinds expectedKinds;
                        if (read == P::Ask)
                        {
                            expected.push_back(C::ReadDocuments);
                        }
                        if (testCase.needsEdit && edit == P::Ask)
                        {
                            expectedKinds |= K::Edit;
                        }
                        if (testCase.needsFiles && files == P::Ask)
                        {
                            expectedKinds |= K::Files;
                        }
                        const bool isAlways = value == QLatin1String("overwrite");
                        if (isAlways)
                        {
                            expected.push_back(C::Save);
                        }
                        else if (expectedKinds)
                        {
                            expected.push_back(value == QLatin1String("keep") ? C::Change : C::SaveCopy);
                        }
                        QVERIFY2(asked == expected, qPrintable(context + QStringLiteral(": %1 questions").arg(asked.size())));
                        if (isAlways || expectedKinds)
                        {
                            const pdf::PDFAppMcpConfirmation& last = *confirmations->shown.back();
                            QVERIFY2(last.permissions == expectedKinds, qPrintable(context));
                            QVERIFY2(last.isAlwaysConfirmed == isAlways, qPrintable(context));
                        }

                        // What the result touches
                        const bool isKeep = value == QLatin1String("keep");
                        const bool isOverwrite = value == QLatin1String("overwrite");
                        QVERIFY2(pdftest::AppDocumentFixture::undoCount(document) == undoBefore + ((isKeep || isOverwrite) ? 1 : 0), qPrintable(context));
                        QVERIFY2(document->getDocument()->getInfo()->title == ((isKeep || isOverwrite) ? title : titleBefore), qPrintable(context));
                        if (isOverwrite)
                        {
                            QVERIFY2(titleOf(file) == title, qPrintable(context));
                        }
                        else
                        {
                            QVERIFY2(readFile(file) == fileBefore, qPrintable(context));
                        }
                        if (value == QLatin1String("save_copy"))
                        {
                            QVERIFY2(titleOf(path) == title, qPrintable(context));
                        }
                        if (value == QLatin1String("save_file"))
                        {
                            QVERIFY2(readFile(path) == title.toUtf8(), qPrintable(context));
                        }
                        if (value == QLatin1String("return_text"))
                        {
                            QVERIFY2(result->getFirstText().endsWith(title), qPrintable(context));
                        }
                    }
                }
            }
        }
        confirmations->setAutoAnswer(std::nullopt);
    }

    /// A commit under a modal dialog of the application is refused as of the changes of
    /// phase 1 (plan 2.5, denied_dialog); a copy does not change the document
    static void modalDialog()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("modal.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QByteArray original = readFile(file);
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        QDialog* modal = new QDialog(fixture.window);
        modal->setModal(true);
        modal->show();
        QTRY_VERIFY(QApplication::activeModalWidget() == modal);

        for (const QString& value : { QStringLiteral("keep"), QStringLiteral("overwrite") })
        {
            const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), value));
            QVERIFY(result && result->isError);
            QVERIFY2(result->getFirstText().contains(QStringLiteral("dialog")), qPrintable(result->getFirstText()));
            QCOMPARE(replacement.tool->getExecuteCount(), 0);
        }
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedDialog));

        const QString copy = fixture.path(QStringLiteral("modal-copy.pdf"));
        const std::optional<pdf::PDFMcpCallResult> copied = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy));
        QVERIFY(copied);
        QVERIFY2(!copied->isError, qPrintable(copied->getFirstText()));
        QCOMPARE(titleOf(copy), QStringLiteral("New title"));
        delete modal;
        pdftest::AppDocumentFixture::settle();

        // A dialog opened while the tool runs: the result is not committed under it
        replacement.tool->setHold(true);
        const int runs = replacement.tool->getExecuteCount();
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Under a dialog")));
        QTRY_VERIFY(replacement.tool->getHeldCount() > 0);
        modal = new QDialog(fixture.window);
        modal->setModal(true);
        modal->show();
        QTRY_VERIFY(QApplication::activeModalWidget() == modal);
        replacement.tool->setHold(false);
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("dialog")), qPrintable(result->getFirstText()));
        QCOMPARE(replacement.tool->getExecuteCount(), runs + 1);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("Under a dialog"));
        delete modal;
        pdftest::AppDocumentFixture::settle();

        // No dialog: the change goes on
        const std::optional<pdf::PDFMcpCallResult> kept = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Kept")));
        QVERIFY(kept);
        QVERIFY2(!kept->isError, qPrintable(kept->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QCOMPARE(readFile(file), original);
    }

    /// Plan 7: a permission set to Deny while the question waits or while an analysis runs
    /// is an error of the call, audited as the permission (never "denied by you")
    static void permissionChangedDuringCall()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("changed.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Deny));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("Edit documents")), qPrintable(result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedPermission));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));

        // An analysis, which has made a changed document, with Edit = Deny: raised and refused
        const QString analysisId = QStringLiteral("review.statistics");
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Deny));
        ToolReplacement analysis(fixture.host(), analysisId);
        result = fixture.callTool(toolName(analysisId), arguments(analysisId, QStringLiteral("title"), QStringLiteral("keep")));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("Edit documents")), qPrintable(result->getFirstText()));
        QCOMPARE(analysis.tool->getExecuteCount(), 1);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        bool isPermissionRow = false;
        for (const pdf::PDFMcpActivityRow& row : rows(fixture, analysisId))
        {
            isPermissionRow = isPermissionRow || row.status == pdf::PDFMcpActivityStatus::DeniedPermission;
            QVERIFY2(row.status != pdf::PDFMcpActivityStatus::DeniedUser, "The user was asked nothing");
        }
        QVERIFY(isPermissionRow);
    }

    /// Marks every text of every context: what follows the language has the mark
    class MarkingTranslator : public QTranslator
    {
    public:
        bool isEmpty() const override { return false; }

        QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
        {
            Q_UNUSED(context);
            Q_UNUSED(disambiguation);
            Q_UNUSED(n);
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
    };

    /// The owner, 2026-11-02: what only the assistant reads is English in every language. The
    /// bytes of tools/list of the tools of the catalog do not follow the language, and a change of
    /// the language is no change of the list (no new generation, no list_changed).
    static void schemaInEnglish()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        if (!fixture.startServer())
        {
            QSKIP("The MCP server does not start");
        }

        // The real Compress and the test tool of every type of a parameter
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);
        QVERIFY(pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(pdf::PDFDocumentToolCompress::TOOL_ID)));

        std::unique_ptr<pdftest::McpTestClient> listening = fixture.client(pdftest::McpTestEra::Modern);
        std::unique_ptr<pdftest::McpTestStream> stream = listening->openStream(listening->createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("toolsListChanged"), true } }) } })));
        QVERIFY(stream);
        QVERIFY(stream->waitForMessages(1, 10000));
        QTest::qWait(500);
        pdftest::AppDocumentFixture::settle();
        const int messagesBefore = int(stream->messages().size());

        const std::array<pdf::PDFMcpEra, 3> eras = { pdf::PDFMcpEra::Modern, pdf::PDFMcpEra::Legacy20251125, pdf::PDFMcpEra::Legacy20250618 };
        std::vector<QByteArray> before;
        for (const pdf::PDFMcpEra era : eras)
        {
            before.push_back(fixture.host()->getCatalog()->getToolsJson(era));
        }
        const QJsonObject listedBefore = fixture.client(pdftest::McpTestEra::Modern)->listTools().value(QStringLiteral("result")).toObject();
        const quint64 generationBefore = fixture.host()->getCatalogGeneration();
        QVERIFY(before.front().contains("Downsample to"));
        QVERIFY(before.front().contains("Count pages"));

        MarkingTranslator translator;
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto removeTranslator = qScopeGuard([&translator]() { QCoreApplication::removeTranslator(&translator); });
        QCOMPARE(pdf::PDFDocumentToolCatalog::findTool(id)->getTitle().left(4), QStringLiteral("[T] "));

        // The change of the language itself: nothing is published
        pdftest::AppDocumentFixture::settle();
        QTest::qWait(500);
        QCOMPARE(fixture.host()->getCatalogGeneration(), generationBefore);
        QCOMPARE(int(stream->messages().size()), messagesBefore);

        // A catalog built in the other language (another reason - the permissions) has the same bytes
        fixture.host()->rebuildCatalog();
        QCOMPARE(fixture.host()->getCatalogGeneration(), generationBefore + 1);
        for (size_t i = 0; i < eras.size(); ++i)
        {
            const QByteArray after = fixture.host()->getCatalog()->getToolsJson(eras[i]);
            QVERIFY2(!after.contains("[T]"), after.mid(qMax(0, int(after.indexOf("[T]")) - 80), 200).constData());
            QCOMPARE(after, before[i]);
        }

        // Over HTTP too
        const QJsonObject listedAfter = fixture.client(pdftest::McpTestEra::Modern)->listTools().value(QStringLiteral("result")).toObject();
        QCOMPARE(listedAfter.value(QStringLiteral("tools")), listedBefore.value(QStringLiteral("tools")));
    }

    /// F6: only the application itself locks the document during the write. The file is
    /// ahead of the tab then: the assistant, the activity and the automatic refresh say so
    static void overwriteCommitRefused()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("refused-commit.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
        refresh->setEnabled(true);
        const QString titleBefore = document->getDocument()->getInfo()->title;
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        // The lock comes with the operation of the write: the worker writes anyway
        QObject receiver;
        QObject::connect(document, &pdf::PDFAppDocument::operationsChanged, &receiver, [document]()
        {
            for (const pdf::PDFAppDocumentOperation* operation : document->getOperations())
            {
                if (operation->getKind() == pdf::PDFAppDocumentOperationKind::Save && !document->isLocked())
                {
                    document->setLocked(true);
                }
            }
        });

        fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Ahead of the tab")));
        fixture.confirmations()->setAutoAnswer(std::nullopt);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("file was written")) && result->getFirstText().contains(QStringLiteral("previous version")), qPrintable(result->getFirstText()));

        // The file has the result, the tab has none of it and is free
        QCOMPARE(titleOf(file), QStringLiteral("Ahead of the tab"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(!document->isBlockedByOperation());
        bool isFailedRow = false;
        for (const pdf::PDFMcpActivityRow& row : rows(fixture, id))
        {
            isFailedRow = isFailedRow || (row.status == pdf::PDFMcpActivityStatus::Failed && row.detail.value(QStringLiteral("reason")).toString().contains(QStringLiteral("file was written")));
        }
        QVERIFY(isFailedRow);

        // The automatic refresh reads the file by itself: nobody asks it (the watch of the
        // file was replaced by the write)
        QTRY_VERIFY_WITH_TIMEOUT(refresh->getReloadCount() == 1 && document->isReady(), 10000 * pdftest::McpTestClient::getTimeoutFactor());
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("Ahead of the tab"));
        QVERIFY(titleBefore != QStringLiteral("Ahead of the tab"));
    }

    /// A heavy job of a canceled call ends at once also while it waits for a place: the two
    /// places may be taken by tools for minutes (MAXIMUM_HEAVY_JOBS)
    static void heavyJobCanceledWhileWaiting()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("heavy.pdf"), 2));
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);
        pdf::PDFAppMcpHost* host = fixture.host();

        replacement.tool->setHold(true);
        std::vector<std::shared_ptr<pdf::PDFMcpCall>> running;
        for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS; ++i)
        {
            running.push_back(fixture.startCall(toolName(id), arguments(id, QStringLiteral("count"))));
        }
        QTRY_COMPARE(replacement.tool->getHeldCount(), pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS);

        // The next two wait in the host; a heavy read waits behind them, no worker is blocked
        std::shared_ptr<pdf::PDFMcpCall> canceled = fixture.startCall(toolName(id), arguments(id, QStringLiteral("count")));
        std::shared_ptr<pdf::PDFMcpCall> waiting = fixture.startCall(toolName(id), arguments(id, QStringLiteral("count")));
        QCOMPARE(host->getWaitingHeavyJobCount(), 2);
        const int jobsBefore = host->getRunningJobCount();

        canceled->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(canceled, 10000 * pdftest::McpTestClient::getTimeoutFactor());
        QVERIFY2(result, "A canceled call waits for a place of the heavy jobs");
        QVERIFY(result->isError);
        QCOMPARE(host->getWaitingHeavyJobCount(), 1);
        QCOMPARE(host->getRunningJobCount(), jobsBefore - 1);
        QCOMPARE(replacement.tool->getHeldCount(), pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS);
        QCOMPARE(host->getRunningHeavyJobCount(), pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));

        // The others are not touched: each runs once, the canceled one never
        replacement.tool->setHold(false);
        for (const std::shared_ptr<pdf::PDFMcpCall>& call : running)
        {
            const std::optional<pdf::PDFMcpCallResult> finished = pdftest::AppMcpFixture::waitResult(call);
            QVERIFY(finished && !finished->isError);
        }
        const std::optional<pdf::PDFMcpCallResult> finished = pdftest::AppMcpFixture::waitResult(waiting);
        QVERIFY(finished && !finished->isError);
        QCOMPARE(replacement.tool->getExecuteCount(), pdf::PDFMcpLimits::MAXIMUM_HEAVY_JOBS + 1);
        QTRY_COMPARE(host->getRunningJobCount(), 0);
    }

    /// M3: the bound of the rows of the activity, also with every row open; the rows of the
    /// finished calls go first, a finished row ages from the end of its call
    static void activityBound()
    {
        pdftest::AppMcpFixture fixture;
        pdf::PDFAppMcpActivityLog* log = fixture.host()->getActivityLog();
        QVERIFY(log);
        const qint64 start = QDateTime::currentMSecsSinceEpoch();
        log->setCurrentTime(start);
        const size_t maximum = pdf::PDFAppMcpActivityLog::MAXIMUM_KEPT_ROWS;

        const auto entry = [](const QString& document, const QString& callId)
        {
            pdf::PDFAppMcpActivityEntry row;
            row.assistantKey = QStringLiteral("a:1");
            row.client = QStringLiteral("Client");
            row.action = QStringLiteral("files.save_copy");
            row.document = document;
            row.openCallId = callId;
            row.status = pdf::PDFMcpActivityStatus::Ok;
            return row;
        };

        // The oldest row belongs to a call, which has not ended; then rows of ended calls
        const qint64 openRow = log->record(entry(QStringLiteral("open"), QStringLiteral("call-open")));
        QVERIFY(openRow > 0);
        for (size_t i = 0; i < maximum + 50; ++i)
        {
            QVERIFY(log->record(entry(QStringLiteral("finished-%1").arg(i), QString())) > 0);
        }
        log->flush();
        QVERIFY(log->record(entry(QStringLiteral("trigger"), QString())) > 0);
        QVERIFY2(log->getRowCount() <= maximum + 1, qPrintable(QString::number(log->getRowCount())));

        // The open row has stayed: its status reaches the table
        log->update(openRow, pdf::PDFMcpActivityStatus::Failed, QJsonObject({ { QStringLiteral("reason"), QStringLiteral("late") } }));
        log->flush();
        bool isUpdated = false;
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            isUpdated = isUpdated || (row.document == QLatin1String("open") && row.status == pdf::PDFMcpActivityStatus::Failed);
        }
        QVERIFY2(isUpdated, "The row of a running call was forgotten before the rows of the ended calls");

        // It does not age, the others do; after the end of its call it ages too
        const qint64 later = start + pdf::PDFAppMcpActivityLog::KEEP_TIME + 1000;
        log->setCurrentTime(later);
        log->flush();
        QVERIFY(log->record(entry(QStringLiteral("later"), QString())) > 0);
        QCOMPARE(log->getRowCount(), size_t(2));
        log->callFinished(QStringLiteral("call-open"));
        log->flush();
        log->setCurrentTime(later + pdf::PDFAppMcpActivityLog::KEEP_TIME + 1000);
        QVERIFY(log->record(entry(QStringLiteral("latest"), QString())) > 0);
        QCOMPARE(log->getRowCount(), size_t(1));

        // Every row open: the limit still holds
        for (size_t i = 0; i < maximum + 50; ++i)
        {
            QVERIFY(log->record(entry(QStringLiteral("open-%1").arg(i), QStringLiteral("call-%1").arg(i))) > 0);
        }
        log->flush();
        QVERIFY(log->record(entry(QStringLiteral("trigger"), QString())) > 0);
        QVERIFY2(log->getRowCount() <= maximum + 1, qPrintable(QString::number(log->getRowCount())));
        log->setCurrentTime(0);
    }

    /// The answers of the real tools over HTTP against the closed shapes of schema.ts of each
    /// era (McpShape of UnitTestsMcp, whose FakeMcpHost has no catalog) and against the
    /// outputSchema of the tool
    static void schemaShapes()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        if (!fixture.startServer())
        {
            QSKIP("The MCP server does not start");
        }
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("shapes.pdf"), 3);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        const QString documentId = tab->getDocument()->getId();
        fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::Allowed);

        const QString rotateId = QStringLiteral("pages.rotate");
        const QString extractId = QStringLiteral("convert.extract-text");
        ToolReplacement rotate(fixture.host(), rotateId);
        ToolReplacement extract(fixture.host(), extractId);

        const auto withDocument = [&documentId](QJsonObject call)
        {
            call.insert(QStringLiteral("documentId"), documentId);
            return call;
        };

        int index = 0;
        for (const pdftest::McpTestEra era : { pdftest::McpTestEra::Modern, pdftest::McpTestEra::Legacy20251125, pdftest::McpTestEra::Legacy20250618 })
        {
            ++index;
            const QString eraName = QString::number(index);
            std::unique_ptr<pdftest::McpTestClient> client = fixture.client(era);
            if (era != pdftest::McpTestEra::Modern)
            {
                QVERIFY(!client->initialize().isEmpty());
            }

            QStringList errors;
            const QJsonArray tools = McpShape::checkList(client->listTools(), era, QStringLiteral("tools"), &McpShape::checkTool, &errors);
            QJsonObject pages;
            for (const QJsonValue& value : tools)
            {
                if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("pages"))
                {
                    pages = value.toObject();
                }
            }
            QVERIFY2(!pages.isEmpty(), qPrintable(eraName));

            // The closed outputSchema: at the root and at the object of the action
            const QJsonObject outputSchema = pages.value(QStringLiteral("outputSchema")).toObject();
            QCOMPARE(outputSchema.value(QStringLiteral("additionalProperties")).toBool(true), false);
            QVERIFY(outputSchema.value(QStringLiteral("required")).toArray().contains(QStringLiteral("action")));
            const QJsonObject actionOutput = outputSchema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("rotate")).toObject();
            QCOMPARE(actionOutput.value(QStringLiteral("additionalProperties")).toBool(true), false);
            QCOMPARE(pages.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("additionalProperties")).toBool(true), false);

            // Every result value, and an error of the tool
            const std::vector<std::pair<QString, QJsonObject>> calls = {
                { toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("keep"))) },
                { toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("save_copy"), fixture.path(QStringLiteral("shapes-%1.pdf").arg(index)))) },
                { toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("title"), QStringLiteral("overwrite"))) },
                { toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("count"), QStringLiteral("keep"))) },
                { toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("nothing"), QStringLiteral("keep"))) },
                { toolName(rotateId), withDocument(arguments(rotateId, QStringLiteral("fail"), QStringLiteral("keep"))) },
                { toolName(extractId), withDocument(arguments(extractId, QStringLiteral("files"), QStringLiteral("return_text"))) },
                { toolName(extractId), withDocument(arguments(extractId, QStringLiteral("files"), QStringLiteral("save_file"), fixture.path(QStringLiteral("shapes-%1.txt").arg(index)))) },
            };
            int errorCount = 0;
            for (const auto& call : calls)
            {
                const QJsonObject response = client->callTool(call.first, call.second, QJsonObject({ { QStringLiteral("progressToken"), QStringLiteral("p") } }));
                const QString path = eraName + QStringLiteral(" ") + call.second.value(call.second.value(QStringLiteral("action")).toString()).toObject().value(QStringLiteral("mode")).toString();
                const QJsonObject result = McpShape::checkResponse(response, path, &errors);
                McpShape::checkCallToolResult(result, era, path, &errors);
                errorCount += result.value(QStringLiteral("isError")).toBool() ? 1 : 0;

                if (!result.value(QStringLiteral("isError")).toBool())
                {
                    const pdf::PDFMcpCallResult callResult = pdf::PDFMcpCallResult::createComplete(QJsonDocument(result).toJson(QJsonDocument::Compact), false);
                    errors.append(fixture.checkOutputSchema(call.first, callResult));
                }

                // The notifications of the stream of the call
                double last = -1.0;
                for (const QJsonObject& message : client->lastResponse().messages())
                {
                    if (message.value(QStringLiteral("method")).toString() == QLatin1String("notifications/progress"))
                    {
                        const QJsonObject params = McpShape::checkNotification(message, QStringLiteral("notifications/progress"), QStringLiteral("progressToken:I progress:n total?:n message?:s _meta?:o"), &errors);
                        if (params.value(QStringLiteral("progress")).toDouble() <= last)
                        {
                            errors.append(path + QStringLiteral(": the progress does not grow"));
                        }
                        last = params.value(QStringLiteral("progress")).toDouble();
                    }
                }
            }
            QCOMPARE(errorCount, 1);
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        }
        fixture.confirmations()->setAutoAnswer(std::nullopt);
    }

    /// A canceled call with a result never commits: the cancellation after the last check of
    /// the run (PDFDocumentTool::run has stopped a run canceled before it) and after the
    /// decision of the user, before its delivery
    static void canceledWithResult()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("canceled.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();
        const QByteArray original = readFile(file);
        const QString id = QStringLiteral("pages.rotate");
        ToolReplacement replacement(fixture.host(), id);

        // The worker has ended, its result waits for the event loop: canceled now. Also with
        // Edit = Ask: nothing is asked for a canceled call
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Canceled")));
        QVERIFY(fixture.host()->getThreadPool()->waitForDone(30000 * pdftest::McpTestClient::getTimeoutFactor()));
        QCOMPARE(replacement.tool->getExecuteCount(), 1);
        call->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QVERIFY(fixture.confirmations()->shown.empty());
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(document->getDocument()->getInfo()->title != QStringLiteral("Canceled"));

        // ... and with every permission Allow, nothing is committed
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));
        call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Canceled")));
        QVERIFY(fixture.host()->getThreadPool()->waitForDone(30000 * pdftest::McpTestClient::getTimeoutFactor()));
        call->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        // Canceled after Allow, before the decision is delivered: no commit, no file
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        call = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("Canceled")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        call->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result && result->isError);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

        std::shared_ptr<pdf::PDFMcpCall> overwriteCall = fixture.startCall(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("overwrite"), QString(), QStringLiteral("Canceled")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        overwriteCall->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        result = pdftest::AppMcpFixture::waitResult(overwriteCall);
        QVERIFY(result && result->isError);
        QCOMPARE(readFile(file), original);
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));
    }

    /// The tasks extension and tools/list_changed with the tools of the catalog
    static void tasksAndListChanged()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        if (!fixture.startServer())
        {
            QSKIP("The MCP server does not start");
        }
        const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("tasks.pdf"), 2);
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QTRY_VERIFY(tab->getDocument()->isReady());
        pdf::PDFAppDocument* document = tab->getDocument();

        // A listen stream gets list_changed, when a tool is registered: the timer of the host
        // sees the new generation of the registry
        std::unique_ptr<pdftest::McpTestClient> listening = fixture.client(pdftest::McpTestEra::Modern);
        std::unique_ptr<pdftest::McpTestStream> stream = listening->openStream(listening->createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("toolsListChanged"), true } }) } })));
        QVERIFY(stream);
        QVERIFY(stream->waitForMessages(1, 10000));

        // The list_changed of the opened document (its resources) has passed: only the new tool
        // gives the next one
        QTest::qWait(500);
        pdftest::AppDocumentFixture::settle();
        const int messagesBefore = int(stream->messages().size());
        QVERIFY(!fixture.host()->getCatalog()->findTool(u"pages"));

        const QString id = QStringLiteral("pages.rotate");
        const std::shared_ptr<pdftest::TestDocumentTool> tool = std::make_shared<pdftest::TestDocumentTool>(id);
        tool->setResultKindModes(true);
        const auto unregister = qScopeGuard([&id]() { pdf::PDFDocumentToolRegistry::unregisterTool(id); });
        pdf::PDFDocumentToolRegistry::registerTool(tool);
        const int factor = pdftest::McpTestClient::getTimeoutFactor();
        QVERIFY(stream->waitForMessages(messagesBefore + 1, 10000 * factor));
        QCOMPARE(stream->messages().back().value(QStringLiteral("method")).toString(), QStringLiteral("notifications/tools/list_changed"));
        bool isListed = false;
        for (const QJsonValue& value : listening->listTools().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray())
        {
            isListed = isListed || value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("pages");
        }
        QVERIFY(isListed);

        // The question of Edit = Ask makes the call a task; the answer of the user ends it
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        std::unique_ptr<pdftest::McpTestClient> client = fixture.client(pdftest::McpTestEra::Modern);
        client->setCapabilities(QJsonDocument::fromJson(R"({"extensions":{"io.modelcontextprotocol/tasks":{}}})").object());
        QJsonObject created = client->callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("keep"), QString(), QStringLiteral("By a task"))).value(QStringLiteral("result")).toObject();
        QString taskId = created.value(QStringLiteral("taskId")).toString();
        QVERIFY2(!taskId.isEmpty(), QJsonDocument(created).toJson(QJsonDocument::Compact).left(400).constData());
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        QJsonObject task = client->pollTask(taskId).value(QStringLiteral("result")).toObject();
        QCOMPARE(task.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
        QCOMPARE(document->getDocument()->getInfo()->title, QStringLiteral("By a task"));

        // A task canceled while the tool runs stops it: nothing is written
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));
        tool->setHold(true);
        const QString copy = fixture.path(QStringLiteral("task-canceled.pdf"));
        created = client->callTool(toolName(id), arguments(id, QStringLiteral("title"), QStringLiteral("save_copy"), copy)).value(QStringLiteral("result")).toObject();
        taskId = created.value(QStringLiteral("taskId")).toString();
        QVERIFY2(!taskId.isEmpty(), QJsonDocument(created).toJson(QJsonDocument::Compact).left(400).constData());
        QTRY_VERIFY(tool->getHeldCount() > 0);
        client->request(QStringLiteral("tasks/cancel"), QJsonObject({ { QStringLiteral("taskId"), taskId } }));
        task = client->pollTask(taskId).value(QStringLiteral("result")).toObject();
        QCOMPARE(task.value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
        QTRY_COMPARE(tool->getHeldCount(), 0);
        tool->setHold(false);
        QVERIFY(!QFile::exists(copy));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    }
};

#endif // APPMCPCATALOGTESTS_H
