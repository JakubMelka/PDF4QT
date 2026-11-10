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

// The MCP cases of package B of step 3a of phase 2: pages.crop, pages.resize, pages.boxes
// (plan 7): the actions over HTTP keep the result in the tab, a value of the other mode is an
// error, which names what to set

#include "appmcppages.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmcppermissions.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfmcpcall.h"

#include <QJsonDocument>
#include <QtTest>

/// Helpers of the cases of package B
class AppMcpGeometryHelper
{
public:
    AppMcpGeometryHelper() = delete;

    static bool isError(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return !result || result->kind != pdf::PDFMcpCallResult::Kind::Complete || result->isError;
    }

    static QString text(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return result ? (result->kind == pdf::PDFMcpCallResult::Kind::Error ? result->errorMessage : result->getFirstText()) : QString();
    }

    /// A document of two pages of the size, open and ready, the server over HTTP running
    static pdf::PDFAppDocument* open(pdftest::AppMcpFixture& fixture, const QSizeF& size, const QString& name)
    {
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(QRectF(QPointF(0, 0), size));
        builder.appendPage(QRectF(QPointF(0, 0), size));
        const QString filePath = fixture.folder() + QStringLiteral("/") + name;
        pdf::PDFDocument written = builder.build();
        if (!pdftest::AppDocumentSamples::write(written, filePath))
        {
            return nullptr;
        }
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !QTest::qWaitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }, 15000))
        {
            return nullptr;
        }
        if (!fixture.setPermissions(pdf::PDFAppMcpPermission::Allow))
        {
            return nullptr;
        }
        fixture.setTransport(pdftest::AppMcpTransport::Http);
        return fixture.startServer() ? tab->getDocument() : nullptr;
    }

    static std::optional<pdf::PDFMcpCallResult> call(pdftest::AppMcpFixture& fixture, pdf::PDFAppDocument* document, const QString& action, const QJsonObject& parameters)
    {
        QJsonObject arguments;
        arguments.insert(QStringLiteral("action"), action);
        arguments.insert(QStringLiteral("documentId"), document->getId());
        arguments.insert(QStringLiteral("result"), QStringLiteral("keep"));
        arguments.insert(action, parameters);
        return fixture.callTool(QStringLiteral("pages"), arguments);
    }

    static bool isNear(const QRectF& a, const QRectF& b)
    {
        return std::abs(a.left() - b.left()) < 0.01 && std::abs(a.top() - b.top()) < 0.01 && std::abs(a.right() - b.right()) < 0.01 && std::abs(a.bottom() - b.bottom()) < 0.01;
    }

    static double mm(double millimeters) { return millimeters * 72.0 / 25.4; }
};

void AppMcpPagesPackages::registerGeometryCases(std::vector<AppMcpPagesCase>* cases)
{
    using H = AppMcpGeometryHelper;

    // pages.crop with 10 mm of equal margins: kept in the tab, every page cropped (5.3)
    cases->push_back({ QStringLiteral("crop-keep"), [](pdftest::AppMcpFixture& fixture)
    {
        pdf::PDFAppDocument* document = H::open(fixture, QSizeF(595.2756, 841.8898), QStringLiteral("crop.pdf"));
        QVERIFY(document);
        const quint64 version = document->getVersion();
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, document, QStringLiteral("crop"), QJsonDocument::fromJson(R"({"mode":"equal-margins","margin":10})").object());
        QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
        QCOMPARE(document->getVersion(), version + 1);
        for (size_t i = 0; i < 2; ++i)
        {
            QVERIFY(H::isNear(document->getDocument()->getCatalog()->getPage(i)->getCropBox(), QRectF(QPointF(28.3465, 28.3465), QPointF(566.9291, 813.5433))));
        }
    } });

    // A value of the other mode would be ignored: an error, which says what to set (checkGivenParameters)
    cases->push_back({ QStringLiteral("crop-other-mode"), [](pdftest::AppMcpFixture& fixture)
    {
        pdf::PDFAppDocument* document = H::open(fixture, QSizeF(612, 792), QStringLiteral("other.pdf"));
        QVERIFY(document);
        const quint64 version = document->getVersion();
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, document, QStringLiteral("crop"), QJsonDocument::fromJson(R"({"mode":"equal-margins","left":20})").object());
        QVERIFY(H::isError(result));
        QVERIFY2(H::text(result).contains(QStringLiteral("custom-box")), qPrintable(H::text(result)));
        QCOMPARE(document->getVersion(), version);
    } });

    // pages.resize Letter to A4 (5.4): the media box of the sheet A4
    cases->push_back({ QStringLiteral("resize-keep"), [](pdftest::AppMcpFixture& fixture)
    {
        pdf::PDFAppDocument* document = H::open(fixture, QSizeF(612, 792), QStringLiteral("resize.pdf"));
        QVERIFY(document);
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, document, QStringLiteral("resize"), QJsonDocument::fromJson(R"({"pageSize":"a4","marginLeft":15,"marginTop":15,"marginRight":15,"marginBottom":15})").object());
        QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
        QVERIFY(H::isNear(document->getDocument()->getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 595.2756, 841.8898)));
        QVERIFY(H::isNear(document->getDocument()->getCatalog()->getPage(1)->getCropBox(), QRectF(0, 0, 595.2756, 841.8898)));
    } });

    // pages.boxes: two boxes from the crop box - 5 mm (5.5)
    cases->push_back({ QStringLiteral("boxes-keep"), [](pdftest::AppMcpFixture& fixture)
    {
        pdf::PDFAppDocument* document = H::open(fixture, QSizeF(600, 800), QStringLiteral("boxes.pdf"));
        QVERIFY(document);
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, document, QStringLiteral("boxes"),
                                                                    QJsonDocument::fromJson(R"({"basedOn":"crop","writeTo":"trim,art","marginLeft":-5,"marginTop":-5,"marginRight":-5,"marginBottom":-5})").object());
        QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
        const pdf::PDFPage* page = document->getDocument()->getCatalog()->getPage(1);
        const QRectF expected(QPointF(H::mm(5), H::mm(5)), QPointF(600 - H::mm(5), 800 - H::mm(5)));
        QVERIFY(H::isNear(page->getTrimBox(), expected));
        QVERIFY(H::isNear(page->getArtBox(), expected));
        QVERIFY(H::isNear(page->getBleedBox(), QRectF(0, 0, 600, 800)));
    } });

    // An unknown box of writeTo is refused with the names of the boxes
    cases->push_back({ QStringLiteral("boxes-unknown"), [](pdftest::AppMcpFixture& fixture)
    {
        pdf::PDFAppDocument* document = H::open(fixture, QSizeF(600, 800), QStringLiteral("unknown.pdf"));
        QVERIFY(document);
        const std::optional<pdf::PDFMcpCallResult> result = H::call(fixture, document, QStringLiteral("boxes"), QJsonDocument::fromJson(R"({"writeTo":"page"})").object());
        QVERIFY(H::isError(result));
        QVERIFY2(H::text(result).contains(QStringLiteral("media, crop, bleed, trim, art")), qPrintable(H::text(result)));
    } });
}
