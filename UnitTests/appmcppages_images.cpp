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

// The MCP cases of package D of step 3a of phase 2: pages.interleave over HTTP (documentIds, the
// two scans joined by the plan of the core, no outline - Q8; a third document refused, K41) and
// the schema of convert.images-to-pdf (the parameters of the panel are no parameters of an
// assistant; imagePaths is the bridge of package A, Q10)

#include "appmcppages.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfcatalog.h"
#include "pdfdocumenttool_images.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfoutline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

/// Helpers of the cases of package D
class AppMcpPagesImages
{
public:
    AppMcpPagesImages() = delete;

    /// Opens a document of the pages and waits until it is ready
    static pdf::PDFAppDocumentTab* open(pdftest::AppMcpFixture& fixture, const QString& name, int pageCount)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), name, pageCount));
        if (tab && QTest::qWaitFor([tab]() { return tab->getDocument()->isReady(); }, 15000))
        {
            return tab;
        }
        return nullptr;
    }

    /// The new document of the result of a call (its id in the structured content of the action)
    static pdf::PDFAppDocumentTab* findResultTab(pdftest::AppMcpFixture& fixture, const QJsonObject& answer, const QString& action)
    {
        const QString newId = answer.value(QStringLiteral("structuredContent")).toObject().value(action).toObject().value(QStringLiteral("documentId")).toString();
        for (pdf::PDFAppDocumentTab* tab : fixture.window->getDocumentTabs())
        {
            if (!newId.isEmpty() && tab->getDocument() && tab->getDocument()->getId() == newId)
            {
                return tab;
            }
        }
        return nullptr;
    }
};

void AppMcpPagesPackages::registerImageCases(std::vector<AppMcpPagesCase>* cases)
{
    using Images = AppMcpPagesImages;

    // pages.interleave: 3 + 2 pages, back sides reversed - 5 pages, the front side without a pair
    // at the end (5.7); the result has no outline (Q8)
    cases->push_back({ QStringLiteral("interleave-document-ids"), [](pdftest::AppMcpFixture& fixture)
    {
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        pdf::PDFAppDocumentTab* front = Images::open(fixture, QStringLiteral("front.pdf"), 3);
        pdf::PDFAppDocumentTab* back = Images::open(fixture, QStringLiteral("back.pdf"), 2);
        QVERIFY(front && back);

        QJsonObject arguments{ { QStringLiteral("action"), QStringLiteral("interleave") }, { QStringLiteral("result"), QStringLiteral("open_new_tab") },
                               { QStringLiteral("documentIds"), QJsonArray({ front->getDocument()->getId(), back->getDocument()->getId() }) },
                               { QStringLiteral("interleave"), QJsonObject({ { QStringLiteral("backSides"), QStringLiteral("reversed") } }) } };
        std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY2(!result->isError, result->result.constData());
        pdf::PDFAppDocumentTab* resultTab = Images::findResultTab(fixture, QJsonDocument::fromJson(result->result).object(), QStringLiteral("interleave"));
        QVERIFY(resultTab);
        QVERIFY(QTest::qWaitFor([resultTab]() { return resultTab->getDocument()->isReady(); }, 15000));
        QCOMPARE(resultTab->getDocument()->getPageCount(), pdf::PDFInteger(5));
        const pdf::PDFCatalog* catalog = resultTab->getDocument()->getDocument()->getCatalog();
        QVERIFY(!catalog->getOutlineRootPtr() || catalog->getOutlineRootPtr()->getChildCount() == 0);

        // K41: exactly two documents - a third one is refused
        pdf::PDFAppDocumentTab* third = Images::open(fixture, QStringLiteral("third.pdf"), 1);
        QVERIFY(third);
        arguments.insert(QStringLiteral("documentIds"), QJsonArray({ front->getDocument()->getId(), back->getDocument()->getId(), third->getDocument()->getId() }));
        result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY(result->isError);

        // The order of the panel is no parameter of an assistant
        arguments.insert(QStringLiteral("documentIds"), QJsonArray({ front->getDocument()->getId(), back->getDocument()->getId() }));
        arguments.insert(QStringLiteral("interleave"), QJsonObject({ { QStringLiteral("inputs"), QStringLiteral("a,b") } }));
        result = fixture.callTool(QStringLiteral("pages"), arguments);
        QVERIFY(result);
        QVERIFY(result->isError);
    } });

    // convert.images-to-pdf: the parameters of the panel (the order, the turn and the crop) are
    // never parameters of an assistant; the page parameters are
    cases->push_back({ QStringLiteral("images-to-pdf-parameters"), [](pdftest::AppMcpFixture& fixture)
    {
        Q_UNUSED(fixture);
        const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(pdf::PDFDocumentToolImagesToPdf::TOOL_ID));
        QVERIFY(tool);
        QStringList assistantParameters;
        for (const pdf::PDFDocumentToolParameterDefinition& definition : tool->getParameters())
        {
            if (!definition.isPanelOnly())
            {
                assistantParameters << definition.getName();
            }
        }
        QCOMPARE(assistantParameters, QStringList({ "pageSize", "width", "height", "orientation", "margins", "imageScaling", "anchor", "optimizeImages" }));
    } });
}
