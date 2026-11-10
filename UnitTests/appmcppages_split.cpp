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

// The MCP cases of package A of step 3a of phase 2: pages.split and pages.extract over HTTP - the
// result save_files (R17: the files written to the folder of the parameters, existingFiles
// without ask), the result of the parameters of Extract (4.2), the permission Open and save files

#include "appmcppages.h"
#include "mergetestpdf.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpcatalogtools.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolregistry.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

/// Helpers of the cases of package A
class AppMcpPagesSplit
{
public:
    AppMcpPagesSplit() = delete;

    static pdf::PDFAppDocumentTab* open(pdftest::AppMcpFixture& fixture, const QString& name, int pageCount)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), name, pageCount));
        // The fixture of the tool shell is not linked into this test: QTest waits
        const int timeout = 30000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
        if (tab && QTest::qWaitFor([tab]() { return tab->getDocument()->isReady(); }, timeout))
        {
            return tab;
        }
        return nullptr;
    }

    /// The structured content of the action of a result
    static QJsonObject content(const pdf::PDFMcpCallResult& result, const QString& action)
    {
        return QJsonDocument::fromJson(result.result).object().value(QStringLiteral("structuredContent")).toObject().value(action).toObject();
    }

    /// A folder for the files of a case
    static QString createFolder(pdftest::AppMcpFixture& fixture, const QString& name)
    {
        const QString folder = QDir(fixture.folder()).filePath(name);
        QDir().mkpath(folder);
        return folder;
    }

    /// Waits until the document of the tab is ready
    static pdf::PDFAppDocumentTab* openFile(pdftest::AppMcpFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        const int timeout = 30000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
        return tab && QTest::qWaitFor([tab]() { return tab->getDocument()->isReady(); }, timeout) ? tab : nullptr;
    }

    /// A certified document of 3 pages (ISO 32000-2, 12.8.2.2: /Perms /DocMDP refers to the
    /// signature of the field "cert" with the transform method DocMDP and /P 2)
    static QString writeCertified(pdftest::AppMcpFixture& fixture, const QString& name)
    {
        mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("C", 3, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> /Perms << /DocMDP 61 0 R >>", { { 1, "/Annots [60 0 R]" } });
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (cert) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 2 >> >>] >>");
        const QString filePath = fixture.path(name);
        QFile file(filePath);
        return file.open(QFile::WriteOnly) && file.write(pdf.write()) > 0 ? filePath : QString();
    }

    /// A PNG image of the size (a plain color)
    static QString writeImage(pdftest::AppMcpFixture& fixture, const QString& name, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(30, 120, 200));
        const QString filePath = fixture.path(name);
        return image.save(filePath, "PNG") ? filePath : QString();
    }

    /// A direct call of an action of a catalog tool with its parameters and the common arguments
    static std::optional<pdf::PDFMcpCallResult> call(pdftest::AppMcpFixture& fixture, const QString& tool, const QString& action, const QJsonObject& parameters, const QJsonObject& common)
    {
        QJsonObject arguments = common;
        arguments.insert(QStringLiteral("action"), action);
        arguments.insert(action, parameters);
        return pdftest::AppMcpFixture::waitResult(fixture.startCall(tool, arguments));
    }

    /// Count of the end markers of the file: 1 = written whole, more = incremental updates
    static int countEndMarkers(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QFile::ReadOnly) ? int(file.readAll().count("%%EOF")) : -1;
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
    }
};

void AppMcpPagesPackages::registerSplitCases(std::vector<AppMcpPagesCase>* cases)
{
    using Split = AppMcpPagesSplit;

    // pages.split: 6 pages, 2 per file - three files Report_1.pdf ... Report_3.pdf in the folder;
    // the files of a second call get " (2)" (existingFiles rename, the default of an assistant)
    cases->push_back({ QStringLiteral("split-save-files"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        pdf::PDFAppDocumentTab* tab = Split::open(fixture, QStringLiteral("Report.pdf"), 6);
        QVERIFY(tab);
        const QString folder = Split::createFolder(fixture, QStringLiteral("split"));

        QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("split") }, { QStringLiteral("documentId"), tab->getDocument()->getId() },
                               { QStringLiteral("split"), QJsonObject({ { QStringLiteral("rule"), QStringLiteral("every-n-pages") }, { QStringLiteral("pagesPerFile"), 2 },
                                                                        { QStringLiteral("folder"), folder } }) } };
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY2(!result->isError, result->result.constData());
        const QJsonArray files = Split::content(*result, QStringLiteral("split")).value(QStringLiteral("files")).toArray();
        QCOMPARE(files.size(), 3);
        for (int i = 1; i <= 3; ++i)
        {
            QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Report_%1.pdf").arg(i))));
        }

        result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY2(!result->isError, result->result.constData());
        QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Report_1 (2).pdf"))));
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(6));
    } });

    // R17: existingFiles has no ask; save_files needs an existing absolute folder; the result of
    // the parameters is checked (Split has only save_files)
    cases->push_back({ QStringLiteral("split-refusals"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        pdf::PDFAppDocumentTab* tab = Split::open(fixture, QStringLiteral("Report.pdf"), 4);
        QVERIFY(tab);
        const QString folder = Split::createFolder(fixture, QStringLiteral("refusals"));
        auto call = [&](const QJsonObject& parameters, const QString& resultValue)
        {
            QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("split") }, { QStringLiteral("documentId"), tab->getDocument()->getId() },
                                   { QStringLiteral("split"), parameters } };
            if (!resultValue.isEmpty())
            {
                arguments.insert(QStringLiteral("result"), resultValue);
            }
            return fixture.callTool(QStringLiteral("pages"), arguments);
        };

        std::optional<pdf::PDFMcpCallResult> result = call({ { QStringLiteral("folder"), folder }, { QStringLiteral("existingFiles"), QStringLiteral("ask") } }, QString());
        QVERIFY(result && result->isError);
        result = call({ { QStringLiteral("folder"), QStringLiteral("relative/folder") } }, QString());
        QVERIFY(result && result->isError);
        result = call({ { QStringLiteral("folder"), QDir(folder).filePath(QStringLiteral("missing")) } }, QString());
        QVERIFY(result && result->isError);
        result = call({ { QStringLiteral("folder"), folder } }, QStringLiteral("save_file"));
        QVERIFY(result && result->isError);
        QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());

        // The schema of an assistant: Existing files without Ask, Rename by default (the tool
        // pages of tools/list: the values of existingFiles of Split and Extract)
        const pdf::PDFAppMcpPermissions all = pdf::PDFAppMcpPermissions::createAll(pdf::PDFAppMcpPermission::Allow);
        bool isPagesListed = false;
        for (const pdf::PDFMcpToolDefinition& definition : pdf::PDFAppMcpCatalogTools::createDefinitions(all))
        {
            if (definition.name == QLatin1String("pages"))
            {
                isPagesListed = true;
                QVERIFY(definition.inputSchema.contains("\"existingFiles\""));
                QVERIFY(definition.inputSchema.contains("\"rename\""));
                QVERIFY(!definition.inputSchema.contains("\"ask\""));
            }
        }
        QVERIFY(isPagesListed);
        const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.split"));
        QVERIFY(tool);
        QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*tool->getInfo(), tool.get()), QStringList({ QStringLiteral("save_files") }));
    } });

    // pages.extract: one file = open_new_tab (a new document), one file per page = save_files
    // (plan 4.2); a mismatch is refused
    cases->push_back({ QStringLiteral("extract-result-kinds"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        pdf::PDFAppDocumentTab* tab = Split::open(fixture, QStringLiteral("Handbook.pdf"), 5);
        QVERIFY(tab);
        const QString folder = Split::createFolder(fixture, QStringLiteral("extract"));

        const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.extract"));
        QVERIFY(tool);
        QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*tool->getInfo(), tool.get()), QStringList({ QStringLiteral("open_new_tab"), QStringLiteral("save_file"), QStringLiteral("save_files") }));

        const QJsonObject pages{ { QStringLiteral("pages"), QStringLiteral("2-3") } };
        QJsonObject perPage{ { QStringLiteral("pages"), pages },{ QStringLiteral("output"), QStringLiteral("one-file-per-page") }, { QStringLiteral("folder"), folder } };
        QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("extract") }, { QStringLiteral("documentId"), tab->getDocument()->getId() },
                               { QStringLiteral("extract"), perPage }, { QStringLiteral("result"), QStringLiteral("open_new_tab") } };
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result && result->isError);

        arguments.insert(QStringLiteral("result"), QStringLiteral("save_files"));
        result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY2(!result->isError, result->result.constData());
        QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_p2.pdf"))));
        QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Handbook_p3.pdf"))));

        // One file: a new document of 2 pages, nothing written
        arguments.insert(QStringLiteral("extract"), QJsonObject({ { QStringLiteral("pages"), pages } }));
        arguments.insert(QStringLiteral("result"), QStringLiteral("save_files"));
        result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result && result->isError);
    } });

    // K41: documentIds within the bounds of the tool (getInputCountRange) - Interleave takes
    // exactly two documents, a third one is refused before the run
    cases->push_back({ QStringLiteral("documentids-bounds"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        QStringList ids;
        for (const QString& name : { QStringLiteral("Front.pdf"), QStringLiteral("Back.pdf"), QStringLiteral("Third.pdf") })
        {
            pdf::PDFAppDocumentTab* tab = Split::open(fixture, name, 2);
            QVERIFY(tab);
            ids << tab->getDocument()->getId();
        }
        const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QStringLiteral("pages.interleave"));
        QVERIFY(tool);
        QCOMPARE(tool->getInputCountRange().maximum, 2);

        QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("interleave") }, { QStringLiteral("documentIds"), QJsonArray::fromStringList(ids) } };
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result && result->isError);
        QVERIFY2(result->result.contains("exactly 2"), result->result.constData());
    } });

    // Open and save files set to Deny: save_files is refused, nothing is written
    cases->push_back({ QStringLiteral("split-denied"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Deny));
        QVERIFY(fixture.startServer());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        pdf::PDFAppDocumentTab* tab = Split::open(fixture, QStringLiteral("Report.pdf"), 2);
        QVERIFY(tab);
        const QString folder = Split::createFolder(fixture, QStringLiteral("denied"));
        QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("split") }, { QStringLiteral("documentId"), tab->getDocument()->getId() },
                               { QStringLiteral("split"), QJsonObject({ { QStringLiteral("rule"), QStringLiteral("every-page") }, { QStringLiteral("folder"), folder } }) } };
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY2(result->kind != pdf::PDFMcpCallResult::Kind::Complete || result->isError, result->result.constData());
        QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());
    } });

    // R17: existingFiles overwrite replaces the existing files of the run, always confirmed as an
    // overwrite of save_file (also with every permission Allow); the question says Replace
    cases->push_back({ QStringLiteral("split-overwrite-confirmed"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = Split::open(fixture, QStringLiteral("Report.pdf"), 2);
        QVERIFY(tab);
        const QString folder = Split::createFolder(fixture, QStringLiteral("overwrite"));
        QFile existing(QDir(folder).filePath(QStringLiteral("Report_1.pdf")));
        QVERIFY(existing.open(QFile::WriteOnly) && existing.write("OLDCONTENT") > 0);
        existing.close();

        const QJsonObject parameters({ { QStringLiteral("rule"), QStringLiteral("every-page") }, { QStringLiteral("folder"), folder },
                                       { QStringLiteral("existingFiles"), QStringLiteral("overwrite") } });
        QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("split") }, { QStringLiteral("documentId"), tab->getDocument()->getId() },
                                { QStringLiteral("split"), parameters } });
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("pages"), arguments);
        const std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QVERIFY(confirmation->isAlwaysConfirmed);
        QVERIFY(confirmation->details.value(QStringLiteral("overwrite")).toBool());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QVERIFY(Split::readFile(QDir(folder).filePath(QStringLiteral("Report_1.pdf"))).startsWith("%PDF-"));
        QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Report_2.pdf"))));
        QVERIFY(!QFile::exists(QDir(folder).filePath(QStringLiteral("Report_1 (2).pdf"))));

        // Denied by the user: nothing is written
        QVERIFY(QFile::remove(QDir(folder).filePath(QStringLiteral("Report_2.pdf"))));
        const std::shared_ptr<pdf::PDFMcpCall> deniedCall = fixture.startCall(QStringLiteral("pages"), arguments);
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        const std::optional<pdf::PDFMcpCallResult> denied = pdftest::AppMcpFixture::waitResult(deniedCall);
        QVERIFY(denied && denied->isError);
        QVERIFY(!QFile::exists(QDir(folder).filePath(QStringLiteral("Report_2.pdf"))));
    } });

    // Q12, R10: a certified document changed by a page tool - keep and overwrite are refused with
    // the reason, save_copy writes the copy whole (one end of the file, no incremental update);
    // Rotate, Remove and Insert remove the certification of the copy (/Perms), the open document
    // and its file stay as they are
    cases->push_back({ QStringLiteral("certified-only-copy"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString sourcePath = Split::writeCertified(fixture, QStringLiteral("Contract.pdf"));
        QVERIFY(!sourcePath.isEmpty());
        const QByteArray original = Split::readFile(sourcePath);
        pdf::PDFAppDocumentTab* tab = Split::openFile(fixture, sourcePath);
        QVERIFY(tab);
        const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() } });
        const QJsonObject firstPage({ { QStringLiteral("pages"), QStringLiteral("1") } });

        struct Action
        {
            QString action;
            QJsonObject parameters;
            bool isCertificationRemoved;    ///< The core removes the certification (plan R10)
        };
        const std::vector<Action> actions = {
            { QStringLiteral("rotate"), QJsonObject({ { QStringLiteral("pages"), firstPage }, { QStringLiteral("rotation"), QStringLiteral("right") } }), true },
            { QStringLiteral("remove"), QJsonObject({ { QStringLiteral("pages"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("3") } }) } }), true },
            { QStringLiteral("insert"), QJsonObject({ { QStringLiteral("position"), QStringLiteral("end") }, { QStringLiteral("blankPages"), 1 } }), true },
            { QStringLiteral("crop"), QJsonObject({ { QStringLiteral("pages"), firstPage }, { QStringLiteral("mode"), QStringLiteral("equal-margins") }, { QStringLiteral("margin"), 10 } }), true },
        };
        for (const Action& action : actions)
        {
            QJsonObject keep = common;
            keep.insert(QStringLiteral("result"), QStringLiteral("keep"));
            std::optional<pdf::PDFMcpCallResult> result = Split::call(fixture, QStringLiteral("pages"), action.action, action.parameters, keep);
            QVERIFY(result);
            QVERIFY2(result->isError, qPrintable(action.action));
            QVERIFY2(result->getFirstText().contains(QStringLiteral("The document is certified")), qPrintable(action.action + QStringLiteral(": ") + result->getFirstText()));
            QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(3));

            const QString copyPath = fixture.path(QStringLiteral("Contract_%1.pdf").arg(action.action));
            QJsonObject copy = common;
            copy.insert(QStringLiteral("result"), QStringLiteral("save_copy"));
            copy.insert(QStringLiteral("path"), copyPath);
            result = Split::call(fixture, QStringLiteral("pages"), action.action, action.parameters, copy);
            QVERIFY(result);
            QVERIFY2(!result->isError, qPrintable(action.action + QStringLiteral(": ") + result->getFirstText()));
            QCOMPARE(Split::countEndMarkers(copyPath), 1);
            const QByteArray written = Split::readFile(copyPath);
            QVERIFY2(!action.isCertificationRemoved || !written.contains("/Perms"), qPrintable(action.action));
        }
        QCOMPARE(Split::readFile(sourcePath), original);
    } });

    // Q10: imagePaths - pages.add-image reads the images of the disk (keep: one page per image),
    // convert.images-to-pdf makes a new document of them; R15 / Q11 of step 2: a PDF file, a
    // relative path, a tool without images, more than 1 000 paths and Open and save files = Deny
    // are refused, nothing changes
    cases->push_back({ QStringLiteral("image-paths"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = Split::open(fixture, QStringLiteral("Album.pdf"), 2);
        QVERIFY(tab);
        const QString wide = Split::writeImage(fixture, QStringLiteral("wide.png"), QSize(400, 200));
        const QString tall = Split::writeImage(fixture, QStringLiteral("tall.png"), QSize(200, 400));
        QVERIFY(!wide.isEmpty() && !tall.isEmpty());
        QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") },
                             { QStringLiteral("imagePaths"), QJsonArray({ wide, tall }) } });
        std::optional<pdf::PDFMcpCallResult> result = Split::call(fixture, QStringLiteral("pages"), QStringLiteral("add-image"),
                                                                  QJsonObject({ { QStringLiteral("position"), QStringLiteral("end") } }), common);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(4));

        // Images to PDF: a new document of the two images (one page each)
        const size_t tabCount = fixture.window->getTabs().size();
        result = Split::call(fixture, QStringLiteral("convert"), QStringLiteral("images-to-pdf"), QJsonObject(),
                             QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") }, { QStringLiteral("imagePaths"), QJsonArray({ wide, tall }) } }));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(fixture.window->getTabs().size(), tabCount + 1);

        // Refusals: nothing changes
        const auto refused = [&](const QString& action, const QJsonValue& paths, const QString& expected)
        {
            QJsonObject arguments = common;
            arguments.insert(QStringLiteral("imagePaths"), paths);
            const std::optional<pdf::PDFMcpCallResult> refusal = Split::call(fixture, QStringLiteral("pages"), action, action == QLatin1String("rotate") ? QJsonObject() : QJsonObject({ { QStringLiteral("position"), QStringLiteral("end") } }), arguments);
            QVERIFY(refusal);
            QVERIFY2(refusal->isError, qPrintable(action));
            QVERIFY2(refusal->getFirstText().contains(expected), qPrintable(refusal->getFirstText()));
        };
        refused(QStringLiteral("add-image"), QJsonArray({ pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("NotAnImage.pdf"), 1) }), QStringLiteral("the file is no image"));
        refused(QStringLiteral("add-image"), QJsonArray({ QStringLiteral("relative/wide.png") }), QStringLiteral("imagePaths[0]"));
        refused(QStringLiteral("rotate"), QJsonArray({ wide }), QStringLiteral("takes no images"));
        QJsonArray many;
        for (int i = 0; i <= 1000; ++i)
        {
            many.append(wide);
        }
        refused(QStringLiteral("add-image"), many, QStringLiteral("imagePaths"));
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(4));

        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Deny));
        result = Split::call(fixture, QStringLiteral("pages"), QStringLiteral("add-image"), QJsonObject({ { QStringLiteral("position"), QStringLiteral("end") } }), common);
        QVERIFY(result && result->isError);
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(4));
    } });

    // Plan 7: pages.insert takes the pages of open documents by sourceDocumentIds (context.documents[1..])
    // in their order; an unknown id and a tool without sources are refused
    cases->push_back({ QStringLiteral("insert-source-document-ids"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* target = Split::open(fixture, QStringLiteral("Target.pdf"), 2);
        pdf::PDFAppDocumentTab* source = Split::open(fixture, QStringLiteral("Annex.pdf"), 3);
        QVERIFY(target && source);
        QJsonObject common({ { QStringLiteral("documentId"), target->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") },
                             { QStringLiteral("sourceDocumentIds"), QJsonArray({ source->getDocument()->getId() }) } });
        std::optional<pdf::PDFMcpCallResult> result = Split::call(fixture, QStringLiteral("pages"), QStringLiteral("insert"),
                                                                  QJsonObject({ { QStringLiteral("position"), QStringLiteral("after") }, { QStringLiteral("page"), 1 } }), common);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(target->getDocument()->getPageCount(), pdf::PDFInteger(5));
        QCOMPARE(source->getDocument()->getPageCount(), pdf::PDFInteger(3));

        common.insert(QStringLiteral("sourceDocumentIds"), QJsonArray({ QStringLiteral("unknown-id") }));
        result = Split::call(fixture, QStringLiteral("pages"), QStringLiteral("insert"), QJsonObject({ { QStringLiteral("position"), QStringLiteral("end") } }), common);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("sourceDocumentIds[0]")), qPrintable(result->getFirstText()));

        common.insert(QStringLiteral("sourceDocumentIds"), QJsonArray({ source->getDocument()->getId() }));
        result = Split::call(fixture, QStringLiteral("pages"), QStringLiteral("rotate"), QJsonObject(), common);
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("takes no further documents")), qPrintable(result->getFirstText()));
        QCOMPARE(target->getDocument()->getPageCount(), pdf::PDFInteger(5));
    } });
}
