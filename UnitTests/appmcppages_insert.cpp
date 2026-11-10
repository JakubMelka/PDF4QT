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

// The MCP cases of package C of step 3a of phase 2: pages.insert, pages.add-image (imagePaths,
// plan 7, Q10). The sources of pages.insert (sourceDocumentIds) and the images of
// pages.add-image (imagePaths) come with the bridge of package A; the cases here need only the
// document of the call.

#include "appmcppages.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmcppermissions.h"
#include "pdfmcpcall.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

/// Helpers of the cases of Insert Pages and Add Image as Page
class AppMcpPagesInsertHelper
{
public:
    AppMcpPagesInsertHelper() = delete;

    /// A document, whose page n (from 1) is 100 + n points wide (the width names the page)
    static QString writeNamedPages(const QString& filePath, int pageCount)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 1; i <= pageCount; ++i)
        {
            builder.appendPage(QRectF(0, 0, 100 + i, 842));
        }
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// The call of the action of the tool "pages" with its parameters
    static std::optional<pdf::PDFMcpCallResult> call(pdftest::AppMcpFixture& fixture, const QString& action, const QString& documentId, const QJsonObject& parameters, const QString& result)
    {
        QJsonObject arguments;
        arguments.insert(QStringLiteral("action"), action);
        arguments.insert(QStringLiteral("documentId"), documentId);
        if (!result.isEmpty())
        {
            arguments.insert(QStringLiteral("result"), result);
        }
        arguments.insert(action, parameters);
        return pdftest::AppMcpFixture::waitResult(fixture.startCall(QStringLiteral("pages"), arguments));
    }

    /// The widths of the pages of the document ("101 101 101 102 103")
    static QString widths(const pdf::PDFDocument* document)
    {
        QStringList widths;
        for (size_t i = 0; i < document->getCatalog()->getPageCount(); ++i)
        {
            widths << QString::number(qRound(document->getCatalog()->getPage(i)->getMediaBox().width()));
        }
        return widths.join(QLatin1Char(' '));
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppMcpFixture& fixture, int pageCount)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(writeNamedPages(fixture.path(QStringLiteral("target.pdf")), pageCount));
        if (!tab || !QTest::qWaitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }, 30000))
        {
            return nullptr;
        }
        return tab;
    }
};

void AppMcpPagesPackages::registerInsertCases(std::vector<AppMcpPagesCase>* cases)
{
    using H = AppMcpPagesInsertHelper;

    // pages.insert with blank pages after page 1 (plan 7: needs only documentId), result keep:
    // the blank pages have the size of page 1 (388:16811 "As page N"), the document of the tab is
    // changed, the data name the inserted pages (from 1)
    cases->push_back({ QStringLiteral("insert-blank-pages"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 3);
        QVERIFY(tab);
        const QJsonObject parameters({ { QStringLiteral("position"), QStringLiteral("after") }, { QStringLiteral("page"), 1 }, { QStringLiteral("blankPages"), 2 } });
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, QStringLiteral("insert"), tab->getDocument()->getId(), parameters, QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("101 101 101 102 103"));
        const QJsonObject data = result->toQJson().value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("insert")).toObject().value(QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("insertedPages")).toArray(), QJsonArray({ 2, 3 }));
        QCOMPARE(data.value(QStringLiteral("pageCount")).toInt(), 5);
        QCOMPARE(data.value(QStringLiteral("pageCountBefore")).toInt(), 3);
    } });

    // pages.insert at the start with blank pages of a named size and orientation (Letter landscape)
    cases->push_back({ QStringLiteral("insert-blank-letter-at-start"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 2);
        QVERIFY(tab);
        const QJsonObject parameters({ { QStringLiteral("position"), QStringLiteral("start") }, { QStringLiteral("blankPages"), 1 },
                                       { QStringLiteral("blankSize"), QStringLiteral("letter") }, { QStringLiteral("blankOrientation"), QStringLiteral("landscape") } });
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, QStringLiteral("insert"), tab->getDocument()->getId(), parameters, QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
        // Letter is 612 × 792 points; landscape 792 wide
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("792 101 102"));
    } });

    // A value, which would be silently ignored, is refused with what to set (checkGivenParameters):
    // the position after without its page; nothing changes
    cases->push_back({ QStringLiteral("insert-position-needs-page"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 3);
        QVERIFY(tab);
        const QJsonObject parameters({ { QStringLiteral("position"), QStringLiteral("after") }, { QStringLiteral("blankPages"), 1 } });
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, QStringLiteral("insert"), tab->getDocument()->getId(), parameters, QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY(result->isError);
        QVERIFY2(result->getFirstText().contains(QStringLiteral("Set page (from 1) with position after.")), qPrintable(result->getFirstText()));
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("101 102 103"));
    } });

    // pages.add-image without an image (imagePaths is the bridge of package A, Q10): refused, the
    // document stays
    cases->push_back({ QStringLiteral("addimage-without-images"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, 2);
        QVERIFY(tab);
        const QJsonObject parameters({ { QStringLiteral("position"), QStringLiteral("end") } });
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, QStringLiteral("add-image"), tab->getDocument()->getId(), parameters, QStringLiteral("keep"));
        QVERIFY(result);
        QVERIFY(result->isError);
        QCOMPARE(H::widths(tab->getDocument()->getDocument().data()), QStringLiteral("101 102"));
    } });
}
