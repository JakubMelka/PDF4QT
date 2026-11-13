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

// The MCP cases of Manage Pages (step 3b of phase 2, package M4; plan 7, 8, QB4 with RB9, Q9 b,
// RB8): documentId with pages "3, 1-2" (keep: one undo step), a repetition is a clone, pages
// missing a page (the error lists it, nothing is removed - RB9), save_copy and overwrite (always
// confirmed), a locked document, a certified one (keep refused, Q12); assemblyPath with a source,
// which is not open (the error lists it), with a source of another computer (RB8), without a
// result, with open sources (open_new_tab in the order of the assembly), save_files with
// existingFiles overwrite (confirmed, R17); the two modes and documentIds refused.

#include "appmcppages.h"
#include "mergetestpdf.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpchange.h"

#include "pdfcatalog.h"
#include "pdfdocumentassemblyfile.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttoolregistry.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QtTest>

/// Helpers of the cases of Manage Pages
class AppMcpPagesManageHelper
{
public:
    AppMcpPagesManageHelper() = delete;

    /// The tool is in the registry (the GUI registers it with registerAll, plan 9.1)
    static bool ensureTool()
    {
        const QString toolId = QString::fromLatin1(pdf::PDFDocumentToolManage::TOOL_ID);
        if (!pdf::PDFDocumentToolRegistry::getTool(toolId))
        {
            pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<pdf::PDFDocumentToolManage>());
        }
        return pdf::PDFDocumentToolRegistry::getTool(toolId) != nullptr;
    }

    /// A document, whose page n (from 1) is 100 + n points wide (the width names the page)
    static QString writeNamedPages(const QString& filePath, int pageCount, int firstWidth = 101)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, firstWidth + i, 842));
        }
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// The widths of the pages of the document ("101 102 103")
    static QString widths(const pdf::PDFDocument* document)
    {
        QStringList widths;
        for (size_t i = 0; document && i < document->getCatalog()->getPageCount(); ++i)
        {
            widths << QString::number(qRound(document->getCatalog()->getPage(i)->getMediaBox().width()));
        }
        return widths.join(QLatin1Char(' '));
    }

    /// The widths of the pages of a file
    static QString fileWidths(const QString& filePath)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        const pdf::PDFDocument document = reader.readFromFile(filePath);
        return reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK ? widths(&document) : QString();
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppMcpFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        const int timeout = 30000 * pdftest::AppDocumentFixture::getTimeoutFactor();
        return tab && QTest::qWaitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }, timeout) ? tab : nullptr;
    }

    /// The call of pages.manage with the parameters of the action and the common arguments
    static std::optional<pdf::PDFMcpCallResult> call(pdftest::AppMcpFixture& fixture, const QJsonObject& parameters, const QJsonObject& common)
    {
        QJsonObject arguments = common;
        arguments.insert(QStringLiteral("action"), QStringLiteral("manage"));
        arguments.insert(QStringLiteral("manage"), parameters);
        return pdftest::AppMcpFixture::waitResult(fixture.startCall(QStringLiteral("pages"), arguments));
    }

    /// documentId with pages and the result
    static std::optional<pdf::PDFMcpCallResult> callPages(pdftest::AppMcpFixture& fixture, const pdf::PDFAppDocumentTab* tab, const QString& pages, const QString& result,
                                                          const QString& path = QString())
    {
        QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() } });
        if (!result.isEmpty())
        {
            common.insert(QStringLiteral("result"), result);
        }
        if (!path.isEmpty())
        {
            common.insert(QStringLiteral("path"), path);
        }
        return call(fixture, QJsonObject({ { QStringLiteral("pages"), pages } }), common);
    }

    /// A PDF source of the file as Manage Pages records it
    static pdf::PDFDocumentAssembly::Source createSource(const QString& id, const QString& filePath, pdf::PDFInteger pageCount)
    {
        QFile file(filePath);
        const QByteArray data = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        const QFileInfo info(filePath);
        pdf::PDFDocumentAssembly::Source source;
        source.id = id;
        source.name = info.fileName();
        source.path = info.absoluteFilePath();
        source.size = info.size();
        source.modified = info.lastModified(QTimeZone::UTC);
        source.sha256 = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        source.pageCount = pageCount;
        return source;
    }

    /// An item of the pages (from 0) of the source of the index \p input
    static pdf::PDFDocumentAssembly::Item createItem(const QString& id, int input, const std::vector<pdf::PDFInteger>& pageIndices, const QString& name = QString())
    {
        pdf::PDFDocumentAssembly::Item item;
        item.id = id;
        item.name = name;
        item.isCreatedGroup = !name.isEmpty();
        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            pdf::PDFDocumentAssembly::Page page;
            page.plan.input = input;
            page.plan.pageIndex = pageIndex;
            item.pages.push_back(page);
        }
        return item;
    }

    /// Writes an assembly file of the sources and items
    static QString writeAssembly(const QString& filePath, std::vector<pdf::PDFDocumentAssembly::Source> sources, std::vector<pdf::PDFDocumentAssembly::Item> items)
    {
        QString error;
        pdf::PDFDocumentAssemblyFile::Content content;
        content.assembly = pdf::PDFDocumentAssembly::create(std::move(sources), std::move(items), { }, &error);
        if (!error.isEmpty())
        {
            return QString();
        }
        QFile file(filePath);
        const QByteArray data = pdf::PDFDocumentAssemblyFile::write(content, QFileInfo(filePath).absolutePath());
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() ? filePath : QString();
    }

    /// A certified document of 3 pages (ISO 32000-2, 12.8.2.2: /Perms /DocMDP refers to the
    /// signature of the field "cert" with the transform method DocMDP and /P 2)
    static QString writeCertified(const QString& filePath)
    {
        mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("C", 3, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> /Perms << /DocMDP 61 0 R >>", { { 1, "/Annots [60 0 R]" } });
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (cert) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 2 >> >>] >>");
        QFile file(filePath);
        return file.open(QFile::WriteOnly) && file.write(pdf.write()) > 0 ? filePath : QString();
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// The document of the newest document tab
    static pdf::PDFAppDocument* newestDocument(pdftest::AppMcpFixture& fixture)
    {
        const QList<pdf::PDFAppDocumentTab*> tabs = fixture.window->getDocumentTabs();
        pdf::PDFAppDocument* newest = nullptr;
        for (pdf::PDFAppDocumentTab* tab : tabs)
        {
            if (tab->getDocument() && tab->getDocument()->getFilePath().isEmpty())
            {
                newest = tab->getDocument();
            }
        }
        return newest;
    }
};

void AppMcpPagesPackages::registerManageCases(std::vector<AppMcpPagesCase>* cases)
{
    using H = AppMcpPagesManageHelper;

    // QB4: documentId with pages "3, 1-2" (an ordered PDFPageRangeList) and keep - the document of
    // the tab has the pages in the new order, one undo step (4.2); a repetition is a clone
    // ("1-3, 2": the second occurrence of page 2 is another copy of it, the document has 4 pages)
    cases->push_back({ QStringLiteral("manage-pages-order"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 3));
        QVERIFY(tab);
        const int undoCount = pdftest::AppDocumentFixture::undoCount(tab->getDocument());

        std::optional<pdf::PDFMcpCallResult> result = H::callPages(fixture, tab, QStringLiteral("3, 1-2"), QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("103 101 102"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoCount + 1);

        result = H::callPages(fixture, tab, QStringLiteral("1-3, 2"), QString());
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("103 101 102 101"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoCount + 2);
    } });

    // RB9: pages must name every page - a page, which is left out, is an error listing the missing
    // pages, nothing is removed and nothing is run (removal is only pages.remove); the syntax of
    // PDFPageRangeList and the range of the document are checked
    cases->push_back({ QStringLiteral("manage-pages-missing"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 10));
        QVERIFY(tab);
        const int undoCount = pdftest::AppDocumentFixture::undoCount(tab->getDocument());

        // Edit documents = Ask: the refusal comes before any question of the user
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        fixture.confirmations()->setAutoAnswer(pdf::PDFAppMcpDecision::DeniedUser);

        std::optional<pdf::PDFMcpCallResult> result = H::callPages(fixture, tab, QStringLiteral("3, 1, 10, 5"), QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("pages: ")), qPrintable(result->getFirstText()));
        QVERIFY2(result->getFirstText().contains(QStringLiteral("missing: 2, 4, 6–9.")), qPrintable(result->getFirstText()));
        QVERIFY2(result->getFirstText().contains(QStringLiteral("pages.remove")), qPrintable(result->getFirstText()));
        QVERIFY(fixture.confirmations()->shown.empty());

        result = H::callPages(fixture, tab, QStringLiteral("1-11"), QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("pages: ")), qPrintable(result->getFirstText()));
        QVERIFY(fixture.confirmations()->shown.empty());

        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("101 102 103 104 105 106 107 108 109 110"));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoCount);
    } });

    // save_copy writes the copy in the new order, the document and its file stay; overwrite rewrites
    // the file of the document - always confirmed (an overwrite of the original, newapp_mcp.md 4)
    cases->push_back({ QStringLiteral("manage-pages-copy-overwrite"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString filePath = H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 3);
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
        QVERIFY(tab);
        const QByteArray original = H::readFile(filePath);

        const QString copyPath = fixture.path(QStringLiteral("Contract reordered.pdf"));
        std::optional<pdf::PDFMcpCallResult> result = H::callPages(fixture, tab, QStringLiteral("2, 3, 1"), QStringLiteral("save_copy"), copyPath);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(H::fileWidths(copyPath), QStringLiteral("102 103 101"));
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("101 102 103"));
        QCOMPARE(H::readFile(filePath), original);

        QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("overwrite") } });
        QJsonObject arguments = common;
        arguments.insert(QStringLiteral("action"), QStringLiteral("manage"));
        arguments.insert(QStringLiteral("manage"), QJsonObject({ { QStringLiteral("pages"), QStringLiteral("3-1") } }));
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("pages"), arguments);
        const std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QVERIFY(confirmation->isAlwaysConfirmed);
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(H::fileWidths(filePath), QStringLiteral("103 102 101"));
    } });

    // A locked document is not changed by an assistant (newapp_mcp.md 4) and This document is not
    // available for it (plan 4.5) - keep and save_copy are refused, nothing changes
    cases->push_back({ QStringLiteral("manage-pages-locked"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 3));
        QVERIFY(tab);
        tab->getDocument()->setLocked(true);

        std::optional<pdf::PDFMcpCallResult> result = H::callPages(fixture, tab, QStringLiteral("3, 2, 1"), QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY(result->isError);
        QCOMPARE(result->getFirstText(), pdf::PDFAppMcpChangeJob::getLockedText());
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedLocked));

        const QString copyPath = fixture.path(QStringLiteral("Locked copy.pdf"));
        result = H::callPages(fixture, tab, QStringLiteral("3, 2, 1"), QStringLiteral("save_copy"), copyPath);
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY(!QFile::exists(copyPath));
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("101 102 103"));
    } });

    // Q12: a certified document - keep is refused with the reason (the certification allows no
    // change of the pages), save_copy writes the copy whole without /Perms; the file stays
    cases->push_back({ QStringLiteral("manage-pages-certified"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString filePath = H::writeCertified(fixture.path(QStringLiteral("Certified.pdf")));
        QVERIFY(!filePath.isEmpty());
        const QByteArray original = H::readFile(filePath);
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, filePath);
        QVERIFY(tab);

        std::optional<pdf::PDFMcpCallResult> result = H::callPages(fixture, tab, QStringLiteral("3, 1-2"), QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("The document is certified")), qPrintable(result->getFirstText()));
        QCOMPARE(tab->getDocument()->getPageCount(), pdf::PDFInteger(3));

        const QString copyPath = fixture.path(QStringLiteral("Certified copy.pdf"));
        result = H::callPages(fixture, tab, QStringLiteral("3, 1-2"), QStringLiteral("save_copy"), copyPath);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        const QByteArray written = H::readFile(copyPath);
        QCOMPARE(written.count("%%EOF"), qsizetype(1));
        QVERIFY(!written.contains("/Perms"));
        QCOMPARE(H::readFile(filePath), original);
    } });

    // Q9 b: assemblyPath - each PDF source must be an open document, else the error lists the
    // paths (no PDF is read from the disk for an assistant); a source of another computer is
    // refused without being contacted (RB8); the new documents need an explicit result
    cases->push_back({ QStringLiteral("manage-assembly-refusals"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString contract = H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 2);
        const QString annex = H::writeNamedPages(fixture.path(QStringLiteral("Annex_A.pdf")), 2, 201);
        QVERIFY(H::openReady(fixture, contract));
        const QString assemblyPath = H::writeAssembly(fixture.path(QStringLiteral("Tender.pdf4qtassembly")),
                                                      { H::createSource(QStringLiteral("s1"), contract, 2), H::createSource(QStringLiteral("s2"), annex, 2) },
                                                      { H::createItem(QStringLiteral("a"), 1, { 0 }), H::createItem(QStringLiteral("b"), 0, { 0, 1 }, QStringLiteral("Contract")) });
        QVERIFY(!assemblyPath.isEmpty());

        const QJsonObject onePdf({ { QStringLiteral("assemblyPath"), assemblyPath }, { QStringLiteral("output"), QStringLiteral("one-pdf") } });
        std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, onePdf, QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") } }));
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("files.open")), qPrintable(result->getFirstText()));
        QVERIFY2(result->getFirstText().contains(QDir::toNativeSeparators(QFileInfo(annex).absoluteFilePath())), qPrintable(result->getFirstText()));
        QVERIFY2(!result->getFirstText().contains(QStringLiteral("Contract.pdf")), qPrintable(result->getFirstText()));

        // Without a result (the default keep is the result of This document)
        result = H::call(fixture, onePdf, QJsonObject());
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().startsWith(QStringLiteral("result: ")), qPrintable(result->getFirstText()));

        // A source of another computer: refused, never contacted. The network path of the system
        // (PDFAppNetworkPaths): a UNC path of Windows; elsewhere a backslash is a character of a
        // relative file name, a share of another computer is a mount of GVFS
        pdf::PDFDocumentAssembly::Source remote = H::createSource(QStringLiteral("s1"), contract, 2);
#ifdef Q_OS_WIN
        remote.path = QStringLiteral("\\\\server\\share\\Contract.pdf");
#else
        remote.path = QStringLiteral("/run/user/1000/gvfs/smb-share:server=server,share=share/Contract.pdf");
#endif
        const QString remotePath = H::writeAssembly(fixture.path(QStringLiteral("Remote.pdf4qtassembly")), { remote }, { H::createItem(QStringLiteral("a"), 0, { 0 }) });
        QVERIFY(!remotePath.isEmpty());
        result = H::call(fixture, QJsonObject({ { QStringLiteral("assemblyPath"), remotePath }, { QStringLiteral("output"), QStringLiteral("one-pdf") } }),
                         QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") } }));
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("another computer")), qPrintable(result->getFirstText()));

        // Both modes, documentIds, a file of another suffix: refused
        result = H::call(fixture, QJsonObject({ { QStringLiteral("assemblyPath"), assemblyPath }, { QStringLiteral("pages"), QStringLiteral("1") } }),
                         QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") } }));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("Give either assemblyPath")), qPrintable(result->getFirstText()));
        result = H::call(fixture, QJsonObject({ { QStringLiteral("assemblyPath"), contract }, { QStringLiteral("output"), QStringLiteral("one-pdf") } }),
                         QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") } }));
        QVERIFY(result && result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral(".pdf4qtassembly")), qPrintable(result->getFirstText()));
        QCOMPARE(fixture.window->getDocumentTabs().size(), qsizetype(1));
    } });

    // Q9 b: open sources - open_new_tab makes one PDF in the order of the assembly (the annex page,
    // then the group of the contract); the open documents stay unchanged
    cases->push_back({ QStringLiteral("manage-assembly-new-tab"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString contract = H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 2);
        const QString annex = H::writeNamedPages(fixture.path(QStringLiteral("Annex_A.pdf")), 2, 201);
        pdf::PDFAppDocumentTab* contractTab = H::openReady(fixture, contract);
        pdf::PDFAppDocumentTab* annexTab = H::openReady(fixture, annex);
        QVERIFY(contractTab && annexTab);
        const QString assemblyPath = H::writeAssembly(fixture.path(QStringLiteral("Tender.pdf4qtassembly")),
                                                      { H::createSource(QStringLiteral("s1"), contract, 2), H::createSource(QStringLiteral("s2"), annex, 2) },
                                                      { H::createItem(QStringLiteral("a"), 1, { 1 }), H::createItem(QStringLiteral("b"), 0, { 1, 0 }, QStringLiteral("Contract")) });
        QVERIFY(!assemblyPath.isEmpty());

        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, QJsonObject({ { QStringLiteral("assemblyPath"), assemblyPath }, { QStringLiteral("output"), QStringLiteral("one-pdf") } }),
                                                                    QJsonObject({ { QStringLiteral("result"), QStringLiteral("open_new_tab") } }));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        pdf::PDFAppDocument* created = H::newestDocument(fixture);
        QVERIFY(created);
        QVERIFY(QTest::qWaitFor([created]() { return created->isReady(); }, 30000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
        QCOMPARE(H::widths(created->getDocument().data()), QStringLiteral("202 102 101"));
        QCOMPARE(H::widths(contractTab->getDocument()->getDocument().data()), QStringLiteral("101 102"));
        QCOMPARE(H::widths(annexTab->getDocument()->getDocument().data()), QStringLiteral("201 202"));
    } });

    // Plan 7 (RS3BAPP): the assembly file is read with Open and save files - set to Ask, the user is
    // asked also for open_new_tab (whose own permission is Edit documents), the question names the
    // file; a refusal makes no tab
    cases->push_back({ QStringLiteral("manage-assembly-files-ask"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString contract = H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 2);
        QVERIFY(H::openReady(fixture, contract));
        const QString assemblyPath = H::writeAssembly(fixture.path(QStringLiteral("Tender.pdf4qtassembly")), { H::createSource(QStringLiteral("s1"), contract, 2) },
                                                      { H::createItem(QStringLiteral("a"), 0, { 1, 0 }) });
        QVERIFY(!assemblyPath.isEmpty());
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
        const qsizetype tabCount = fixture.window->getDocumentTabs().size();

        QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("manage") }, { QStringLiteral("result"), QStringLiteral("open_new_tab") },
                                { QStringLiteral("manage"), QJsonObject({ { QStringLiteral("assemblyPath"), assemblyPath }, { QStringLiteral("output"), QStringLiteral("one-pdf") } }) } });
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("pages"), arguments);
        const std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QVERIFY(confirmation->permissions.testFlag(pdf::PDFAppMcpPermissionKind::Files));
        QVERIFY(!confirmation->permissions.testFlag(pdf::PDFAppMcpPermissionKind::Edit));
        bool isAssemblyNamed = false;
        for (const QJsonValue& fact : confirmation->details.value(QStringLiteral("facts")).toArray())
        {
            isAssemblyNamed = isAssemblyNamed || (fact.toObject().value(QStringLiteral("key")).toString() == QStringLiteral("Assembly read") &&
                                                  fact.toObject().value(QStringLiteral("value")).toString() == QDir::toNativeSeparators(assemblyPath));
        }
        QVERIFY(isAssemblyNamed);
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY(result->isError);
        QCOMPARE(fixture.window->getDocumentTabs().size(), tabCount);
    } });

    // R17: save_files of per-page with existingFiles overwrite - always confirmed, the files are
    // written in the order of the assembly
    cases->push_back({ QStringLiteral("manage-assembly-save-files"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(H::ensureTool());
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const QString contract = H::writeNamedPages(fixture.path(QStringLiteral("Contract.pdf")), 3);
        QVERIFY(H::openReady(fixture, contract));
        const QString assemblyPath = H::writeAssembly(fixture.path(QStringLiteral("Pages.pdf4qtassembly")), { H::createSource(QStringLiteral("s1"), contract, 3) },
                                                      { H::createItem(QStringLiteral("a"), 0, { 2 }), H::createItem(QStringLiteral("b"), 0, { 0 }) });
        QVERIFY(!assemblyPath.isEmpty());
        const QString folder = QDir(fixture.folder()).filePath(QStringLiteral("files"));
        QVERIFY(QDir().mkpath(folder));

        const QJsonObject parameters({ { QStringLiteral("assemblyPath"), assemblyPath }, { QStringLiteral("output"), QStringLiteral("per-page") }, { QStringLiteral("folder"), folder },
                                       { QStringLiteral("fileName"), QStringLiteral("page-#") }, { QStringLiteral("existingFiles"), QStringLiteral("overwrite") } });
        QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("manage") }, { QStringLiteral("result"), QStringLiteral("save_files") }, { QStringLiteral("manage"), parameters } });
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("pages"), arguments);
        const std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = fixture.confirmDialog();
        QVERIFY(confirmation);
        QVERIFY(confirmation->isAlwaysConfirmed);
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        const std::optional<pdf::PDFMcpCallResult> result = pdftest::AppMcpFixture::waitResult(call);
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(H::fileWidths(QDir(folder).filePath(QStringLiteral("page-1.pdf"))), QStringLiteral("103"));
        QCOMPARE(H::fileWidths(QDir(folder).filePath(QStringLiteral("page-2.pdf"))), QStringLiteral("101"));
    } });
}
