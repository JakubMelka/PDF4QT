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

#ifndef APPMCPREADTESTS_H
#define APPMCPREADTESTS_H

// Tests of package B of step 7 (the read tools, the document resources, the
// prompts at run time, the completion, the elicitation of the documents and the
// page labels) - called by the slots of tst_appmcptest.cpp (UnitTestsAppMcp)

#include "appmcpfixture.h"
#include "testdocumenttool.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcpareahighlight.h"
#include "pdfappmcpcompletion.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcpdocumentreader.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcpelicitation.h"
#include "pdfappmcphost.h"
#include "pdfappmcpjob.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcprenderer.h"
#include "pdfappmcptextservice.h"
#include "pdfappmcptool_document.h"
#include "pdfappmcptoolset.h"
#include "pdfbookmarkrepository.h"
#include "pdfdatabase.h"
#include "pdfdatabasestatement.h"

#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttextindex.h"
#include "pdfexception.h"
#include "pdfmcpcatalog.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfmcprepository.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QtTest>

#include <atomic>
#include <functional>
#include <optional>
#include <stdexcept>

// moc does not read the raw string literals of the tests
#ifndef Q_MOC_RUN

class AppMcpReadTests
{
public:
    AppMcpReadTests() = delete;

    using Result = std::optional<pdf::PDFMcpCallResult>;
    using PageModifier = std::function<void(pdf::PDFDocumentBuilder&, const std::vector<pdf::PDFObjectReference>&)>;

    // Helpers ---------------------------------------------------------------

    static QJsonObject json(const QString& text)
    {
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }

    static QJsonObject structured(const pdf::PDFMcpCallResult& result)
    {
        return result.toQJson().value(QStringLiteral("structuredContent")).toObject();
    }

    static QJsonObject actionObject(const Result& result, const QString& action)
    {
        return result ? structured(*result).value(action).toObject() : QJsonObject();
    }

    static bool isSuccess(const Result& result)
    {
        return result && result->kind == pdf::PDFMcpCallResult::Kind::Complete && !result->isError;
    }

    static bool isToolError(const Result& result)
    {
        return result && result->kind == pdf::PDFMcpCallResult::Kind::Complete && result->isError;
    }

    static QString describe(const Result& result)
    {
        if (!result)
        {
            return QStringLiteral("no result");
        }
        switch (result->kind)
        {
            case pdf::PDFMcpCallResult::Kind::Error:
                return QStringLiteral("error %1: %2").arg(result->errorCode).arg(result->errorMessage);
            case pdf::PDFMcpCallResult::Kind::InputRequired:
                return QStringLiteral("input required");
            case pdf::PDFMcpCallResult::Kind::Complete:
                break;
        }
        return result->getFirstText().left(500);
    }

    static Result call(pdftest::AppMcpFixture& fixture, const QString& tool, const QString& arguments)
    {
        return fixture.callTool(tool, json(arguments));
    }

    /// Arguments of a call of a document action
    static QString args(const QString& action, const QString& documentId, const QString& actionObject = QStringLiteral("{}"))
    {
        return QStringLiteral(R"({"action":"%1","documentId":"%2","%1":%3})").arg(action, documentId, actionObject);
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppMcpFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        QElapsedTimer timer;
        timer.start();
        while (tab && tab->getDocument() && !tab->getDocument()->isReady() && timer.elapsed() < 20000 * fixture.getTimeoutFactor())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        }
        return tab && tab->getDocument() && tab->getDocument()->isReady() ? tab : nullptr;
    }

    /// A document of pages 300 x 400 with the text of AppDocumentSamples::getPageText;
    /// the modifier changes the pages (rotation, boxes) or the catalog
    static QString makeSample(const QString& folder, const QString& name, int pageCount, const PageModifier& modifier = PageModifier())
    {
        pdf::PDFDocumentBuilder builder;
        std::vector<pdf::PDFObjectReference> pages;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
            pages.push_back(page);

            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            if (!painter)
            {
                continue;
            }
            QFont font = painter->font();
            font.setPixelSize(12);
            painter->setFont(font);
            painter->setPen(Qt::black);
            const QStringList lines = pdftest::AppDocumentSamples::getPageText(i).split(QLatin1Char('\n'));
            for (int line = 0; line < lines.size(); ++line)
            {
                painter->drawText(QPointF(30, 40 + 20 * line), lines[line]);
            }
            content.end(painter);
        }

        if (modifier)
        {
            modifier(builder, pages);
        }

        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(name);
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    static pdf::PDFObject array(std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder builder;
        for (const pdf::PDFObject& item : items)
        {
            builder.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::move(builder));
    }

    static pdf::PDFObject number(double value) { return pdf::PDFObject::createReal(value); }
    static pdf::PDFObject integer(pdf::PDFInteger value) { return pdf::PDFObject::createInteger(value); }
    static pdf::PDFObject name(const char* value) { return pdf::PDFObject::createName(QByteArray(value)); }

    /// The image content block of a result
    static QImage image(const pdf::PDFMcpCallResult& result, QString* mimeType = nullptr)
    {
        for (const QJsonValue& block : result.toQJson().value(QStringLiteral("content")).toArray())
        {
            const QJsonObject object = block.toObject();
            if (object.value(QStringLiteral("type")).toString() == QLatin1String("image"))
            {
                if (mimeType)
                {
                    *mimeType = object.value(QStringLiteral("mimeType")).toString();
                }
                return QImage::fromData(QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1()));
            }
        }
        return QImage();
    }

    /// Content blocks of a type
    static QJsonArray blocks(const pdf::PDFMcpCallResult& result, const QString& type)
    {
        QJsonArray found;
        for (const QJsonValue& block : result.toQJson().value(QStringLiteral("content")).toArray())
        {
            if (block.toObject().value(QStringLiteral("type")).toString() == type)
            {
                found.append(block);
            }
        }
        return found;
    }

    /// Maps a page rectangle by the matrix [a, b, c, d, e, f] of a result
    static QRectF mapRect(const QJsonArray& matrix, const QRectF& rect)
    {
        const QTransform transform(matrix[0].toDouble(), matrix[1].toDouble(), matrix[2].toDouble(), matrix[3].toDouble(), matrix[4].toDouble(), matrix[5].toDouble());
        return transform.mapRect(rect);
    }

    static QRectF rect(const QJsonObject& object)
    {
        return QRectF(QPointF(object.value(QStringLiteral("left")).toDouble(), object.value(QStringLiteral("bottom")).toDouble()),
                      QPointF(object.value(QStringLiteral("right")).toDouble(), object.value(QStringLiteral("top")).toDouble())).normalized();
    }

    /// Dark pixels of the image inside of the rectangle
    static int darkPixels(const QImage& image, const QRect& rect)
    {
        int count = 0;
        const QRect bounded = rect.intersected(image.rect());
        for (int y = bounded.top(); y <= bounded.bottom(); ++y)
        {
            for (int x = bounded.left(); x <= bounded.right(); ++x)
            {
                count += qGray(image.pixel(x, y)) < 128 ? 1 : 0;
            }
        }
        return count;
    }

    /// The rectangle of the first word of page 1 equal to \p word (get_text with words)
    static QRectF findWord(pdftest::AppMcpFixture& fixture, const QString& documentId, const QString& word, int page = 1)
    {
        const Result result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), documentId,
                                                                          QStringLiteral(R"({"pages":"%1","geometry":"words"})").arg(page)));
        for (const QJsonValue& pageValue : actionObject(result, QStringLiteral("get_text")).value(QStringLiteral("pages")).toArray())
        {
            for (const QJsonValue& value : pageValue.toObject().value(QStringLiteral("words")).toArray())
            {
                if (value.toObject().value(QStringLiteral("text")).toString() == word)
                {
                    return rect(value.toObject().value(QStringLiteral("rect")).toObject());
                }
            }
        }
        return QRectF();
    }

    /// Starts a request on the host directly
    static std::shared_ptr<pdf::PDFMcpCall> start(pdftest::AppMcpFixture& fixture, const pdf::PDFMcpCallRequest& request)
    {
        std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(request);
        fixture.host()->handleCall(call);
        return call;
    }

    /// A retried request with the answers of an input request (the state of the
    /// dispatcher: the allowed values of the sealed requestState)
    static Result retry(pdftest::AppMcpFixture& fixture, pdf::PDFMcpCallRequest request, const pdf::PDFMcpCallResult& inputRequired, const QJsonObject& responses)
    {
        request.inputResponses = responses;
        request.requestState = QJsonObject({ { QStringLiteral("allowedValues"), inputRequired.inputRequests.allowedValues } });
        return pdftest::AppMcpFixture::waitResult(start(fixture, request));
    }

    static QJsonObject accept(const QString& key, const QString& property, const QString& value)
    {
        return QJsonObject({ { key, QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") },
                                                  { QStringLiteral("content"), QJsonObject({ { property, value } }) } }) } });
    }

    static pdf::PDFMcpCallRequest resourceRequest(pdftest::AppMcpFixture& fixture, const QString& uri)
    {
        pdf::PDFMcpCallRequest request = fixture.createRequest(QString(), QJsonObject());
        request.kind = pdf::PDFMcpCallKind::ResourceRead;
        request.method = QStringLiteral("resources/read");
        request.uri = uri;
        return request;
    }

    static Result readResource(pdftest::AppMcpFixture& fixture, const QString& uri)
    {
        return pdftest::AppMcpFixture::waitResult(start(fixture, resourceRequest(fixture, uri)));
    }

    /// contents[0] of a resource result
    static QJsonObject resourceContent(const Result& result)
    {
        return result ? result->toQJson().value(QStringLiteral("contents")).toArray().first().toObject() : QJsonObject();
    }

    static pdf::PDFMcpCallRequest promptRequest(pdftest::AppMcpFixture& fixture, const QString& name, const QJsonObject& arguments)
    {
        pdf::PDFMcpCallRequest request = fixture.createRequest(name, arguments);
        request.kind = pdf::PDFMcpCallKind::PromptGet;
        request.method = QStringLiteral("prompts/get");
        return request;
    }

    static Result complete(pdftest::AppMcpFixture& fixture, const QString& params)
    {
        pdf::PDFMcpCallRequest request = fixture.createRequest(QString(), json(params));
        request.kind = pdf::PDFMcpCallKind::Completion;
        request.method = QStringLiteral("completion/complete");
        return pdftest::AppMcpFixture::waitResult(start(fixture, request));
    }

    static QStringList completionValues(const Result& result)
    {
        QStringList values;
        for (const QJsonValue& value : result ? result->toQJson().value(QStringLiteral("completion")).toObject().value(QStringLiteral("values")).toArray() : QJsonArray())
        {
            values << value.toString();
        }
        return values;
    }

    static void activateHome(pdftest::AppMcpFixture& fixture)
    {
        fixture.window->activateTab(fixture.window->getHomeTab());
        QCoreApplication::processEvents();
    }

    // Tests -----------------------------------------------------------------

    static void documentActions()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

        // Information and pages
        pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        Result result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_info"), id));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject info = actionObject(result, QStringLiteral("get_info"));
        QCOMPARE(info.value(QStringLiteral("documentId")).toString(), id);
        QCOMPARE(info.value(QStringLiteral("pageCount")).toInt(), 3);
        QCOMPARE(info.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
        QCOMPARE(info.value(QStringLiteral("form")).toString(), QStringLiteral("none"));
        QVERIFY(info.value(QStringLiteral("metadata")).isObject());
        QVERIFY(!info.contains(QStringLiteral("encryption")));
        QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()));
        QCOMPARE(structured(*result).value(QStringLiteral("action")).toString(), QStringLiteral("get_info"));

        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_pages"), id));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonArray pages = actionObject(result, QStringLiteral("get_pages")).value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 3);

        // Review RS7: the page labels are texts of the document
        QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()));
        const QJsonObject firstPage = pages.first().toObject();
        QCOMPARE(firstPage.value(QStringLiteral("page")).toInt(), 1);
        QCOMPARE(firstPage.value(QStringLiteral("label")).toString(), QStringLiteral("1"));
        QCOMPARE(rect(firstPage.value(QStringLiteral("mediaBox")).toObject()), QRectF(0, 0, 300, 400));
        QCOMPARE(firstPage.value(QStringLiteral("rotation")).toInt(), 0);
        QCOMPARE(firstPage.value(QStringLiteral("widthPt")).toDouble(), 300.0);
        QVERIFY(firstPage.value(QStringLiteral("annotationCount")).toInt() > 10);
        QVERIFY(!actionObject(result, QStringLiteral("get_pages")).contains(QStringLiteral("nextPage")));

        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_pages"), id, QStringLiteral(R"({"pages":"2-3"})")));
        pages = actionObject(result, QStringLiteral("get_pages")).value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages.first().toObject().value(QStringLiteral("page")).toInt(), 2);
        for (const char* hostile : { R"({"pages":"1-99"})", R"({"pages":"x"})", R"({"pages":"0"})", R"({"pages":"3-1"})", R"({"pages":"1-99999999999999999999"})" })
        {
            result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_pages"), id, QString::fromLatin1(hostile)));
            QVERIFY2(isToolError(result), hostile);
            QVERIFY2(result->getFirstText().contains(QStringLiteral("pages")), qPrintable(result->getFirstText()));
        }

        // Annotations: types, replies, the cursor
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_annotations"), id));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject annotations = actionObject(result, QStringLiteral("get_annotations"));
        QCOMPARE(annotations.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
        bool hasHighlight = false;
        bool hasReplies = false;
        bool hasInReplyTo = false;
        const QRegularExpression idPattern(QStringLiteral("^\\d+:\\d+$"));
        for (const QJsonValue& value : annotations.value(QStringLiteral("annotations")).toArray())
        {
            const QJsonObject annotation = value.toObject();
            QVERIFY(idPattern.match(annotation.value(QStringLiteral("id")).toString()).hasMatch());
            QVERIFY(annotation.value(QStringLiteral("flags")).isObject());
            if (annotation.value(QStringLiteral("subject")).toString() == QLatin1String("highlight"))
            {
                hasHighlight = true;
                QCOMPARE(annotation.value(QStringLiteral("type")).toString(), QStringLiteral("highlight"));
                const QJsonArray quads = annotation.value(QStringLiteral("quads")).toArray();
                QVERIFY(!quads.isEmpty());
                QCOMPARE(quads.first().toArray().size(), 8);
                QCOMPARE(annotation.value(QStringLiteral("author")).toString(), QStringLiteral("Tester"));
            }
            hasReplies = hasReplies || annotation.value(QStringLiteral("replyIds")).toArray().size() == 2;
            hasInReplyTo = hasInReplyTo || annotation.contains(QStringLiteral("inReplyTo"));
        }
        QVERIFY(hasHighlight);
        QVERIFY(hasReplies);
        QVERIFY(hasInReplyTo);

        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_annotations"), id, QStringLiteral(R"({"types":["highlight"],"includeReplies":false})")));
        for (const QJsonValue& value : actionObject(result, QStringLiteral("get_annotations")).value(QStringLiteral("annotations")).toArray())
        {
            QCOMPARE(value.toObject().value(QStringLiteral("type")).toString(), QStringLiteral("highlight"));
            QVERIFY(!value.toObject().contains(QStringLiteral("inReplyTo")));
        }

        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_annotations"), id, QStringLiteral(R"({"maxResults":2})")));
        annotations = actionObject(result, QStringLiteral("get_annotations"));
        QCOMPARE(annotations.value(QStringLiteral("annotations")).toArray().size(), 2);
        const QString cursor = annotations.value(QStringLiteral("nextCursor")).toString();
        QVERIFY(!cursor.isEmpty());
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_annotations"), id, QStringLiteral(R"({"maxResults":2,"cursor":"%1"})").arg(cursor)));
        const QJsonArray nextAnnotations = actionObject(result, QStringLiteral("get_annotations")).value(QStringLiteral("annotations")).toArray();
        QCOMPARE(nextAnnotations.size(), 2);
        QVERIFY(nextAnnotations.first().toObject().value(QStringLiteral("id")) != annotations.value(QStringLiteral("annotations")).toArray().first().toObject().value(QStringLiteral("id")));
        for (const char* badCursor : { "!!!", "YTox", "eDox", "YToteHg" })
        {
            result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_annotations"), id, QStringLiteral(R"({"cursor":"%1"})").arg(QString::fromLatin1(badCursor))));
            QVERIFY2(isToolError(result) || isSuccess(result), badCursor);
        }
        QVERIFY(!pdf::PDFAppMcpDocumentReader::parseCursor(QStringLiteral("!!!")));
        QVERIFY(!pdf::PDFAppMcpDocumentReader::parseCursor(QString(65, QLatin1Char('A'))));
        QCOMPARE(*pdf::PDFAppMcpDocumentReader::parseCursor(pdf::PDFAppMcpDocumentReader::createCursor(42)), qint64(42));

        // The outline
        tab = openReady(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
        QVERIFY(tab);
        const QString outlineId = tab->getDocument()->getId();
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_outline"), outlineId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonArray items = actionObject(result, QStringLiteral("get_outline")).value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 4);
        QCOMPARE(items[2].toObject().value(QStringLiteral("item")).toString(), QStringLiteral("2.1"));
        QCOMPARE(items[2].toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Section 1.1"));
        QCOMPARE(items[2].toObject().value(QStringLiteral("level")).toInt(), 2);
        QCOMPARE(items[2].toObject().value(QStringLiteral("page")).toInt(), 3);
        QCOMPARE(items[1].toObject().value(QStringLiteral("childCount")).toInt(), 1);
        QCOMPARE(items[3].toObject().value(QStringLiteral("item")).toString(), QStringLiteral("3"));
        QCOMPARE(items[3].toObject().value(QStringLiteral("page")).toInt(), 5);
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_outline"), outlineId, QStringLiteral(R"({"maxDepth":1})")));
        QCOMPARE(actionObject(result, QStringLiteral("get_outline")).value(QStringLiteral("items")).toArray().size(), 3);
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_outline"), outlineId, QStringLiteral(R"({"maxItems":2})")));
        QCOMPARE(actionObject(result, QStringLiteral("get_outline")).value(QStringLiteral("items")).toArray().size(), 2);
        QVERIFY(actionObject(result, QStringLiteral("get_outline")).value(QStringLiteral("truncated")).toBool());
        QVERIFY(pdf::PDFAppMcpDocumentReader::findOutlineItem(tab->getDocument()->getDocument().data(), QStringLiteral("2.1")));
        for (const char* badItem : { "", "0", "2.0", "9", "2.1.1", "02", "+1", "a", "1..2", "99999999999999999999" })
        {
            QVERIFY2(!pdf::PDFAppMcpDocumentReader::findOutlineItem(tab->getDocument()->getDocument().data(), QString::fromLatin1(badItem)), badItem);
        }

        // Form fields
        tab = openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        const QString formId = tab->getDocument()->getId();
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_form_fields"), formId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        std::map<QString, QJsonObject> fields;
        for (const QJsonValue& value : actionObject(result, QStringLiteral("get_form_fields")).value(QStringLiteral("fields")).toArray())
        {
            fields[value.toObject().value(QStringLiteral("name")).toString()] = value.toObject();
        }
        QCOMPARE(fields[QStringLiteral("name")].value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        QCOMPARE(fields[QStringLiteral("name")].value(QStringLiteral("value")).toString(), QStringLiteral("John"));
        QCOMPARE(fields[QStringLiteral("name")].value(QStringLiteral("page")).toInt(), 1);
        QVERIFY(fields[QStringLiteral("name")].contains(QStringLiteral("rect")));
        QCOMPARE(fields[QStringLiteral("agree")].value(QStringLiteral("type")).toString(), QStringLiteral("checkbox"));
        QVERIFY(fields[QStringLiteral("agree")].contains(QStringLiteral("checked")));
        QCOMPARE(fields[QStringLiteral("choice")].value(QStringLiteral("type")).toString(), QStringLiteral("radio"));
        QCOMPARE(fields[QStringLiteral("choice")].value(QStringLiteral("options")).toArray().size(), 2);
        QCOMPARE(fields[QStringLiteral("fruit")].value(QStringLiteral("type")).toString(), QStringLiteral("combo"));
        QCOMPARE(fields[QStringLiteral("fruit")].value(QStringLiteral("options")).toArray().size(), 3);
        QCOMPARE(fields[QStringLiteral("colors")].value(QStringLiteral("type")).toString(), QStringLiteral("list"));
        QVERIFY(fields[QStringLiteral("colors")].value(QStringLiteral("selected")).isArray());
        QVERIFY(!fields[QStringLiteral("secret")].contains(QStringLiteral("value")));
        QVERIFY(fields[QStringLiteral("fixed")].value(QStringLiteral("readOnly")).toBool());
        QVERIFY(fields[QStringLiteral("required")].value(QStringLiteral("required")).toBool());
        QCOMPARE(fields[QStringLiteral("code")].value(QStringLiteral("maxLength")).toInt(), 6);
        QVERIFY(fields[QStringLiteral("notes")].value(QStringLiteral("multiline")).toBool());
        QCOMPARE(fields[QStringLiteral("signature")].value(QStringLiteral("type")).toString(), QStringLiteral("signature"));
        QCOMPARE(fields[QStringLiteral("reset")].value(QStringLiteral("type")).toString(), QStringLiteral("button"));
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_form_fields"), formId, QStringLiteral(R"({"names":["name","nothing"],"includeValues":false})")));
        const QJsonArray filtered = actionObject(result, QStringLiteral("get_form_fields")).value(QStringLiteral("fields")).toArray();
        QCOMPARE(filtered.size(), 1);
        QVERIFY(!filtered.first().toObject().contains(QStringLiteral("value")));
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_info"), formId));
        QCOMPARE(actionObject(result, QStringLiteral("get_info")).value(QStringLiteral("form")).toString(), QStringLiteral("acro"));

        // Attachments and their contents (nothing is opened)
        tab = openReady(fixture, pdftest::AppDocumentSamples::attachments(fixture.folder()));
        QVERIFY(tab);
        const QString attachmentsId = tab->getDocument()->getId();
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_attachments"), attachmentsId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        const QJsonArray attachments = actionObject(result, QStringLiteral("get_attachments")).value(QStringLiteral("attachments")).toArray();
        QVERIFY(attachments.size() >= 2);
        QCOMPARE(blocks(*result, QStringLiteral("resource_link")).size(), attachments.size());
        int notesIndex = -1;
        for (const QJsonValue& value : attachments)
        {
            if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("notes.txt"))
            {
                notesIndex = value.toObject().value(QStringLiteral("index")).toInt();
            }
        }
        QVERIFY(notesIndex >= 0);
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("read_attachment"), attachmentsId, QStringLiteral(R"({"index":%1})").arg(notesIndex)));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject attachment = actionObject(result, QStringLiteral("read_attachment"));
        QCOMPARE(attachment.value(QStringLiteral("encoding")).toString(), QStringLiteral("text"));
        QVERIFY(!attachment.value(QStringLiteral("truncated")).toBool());
        const QJsonArray embedded = blocks(*result, QStringLiteral("resource"));
        QCOMPARE(embedded.size(), 1);
        QCOMPARE(embedded.first().toObject().value(QStringLiteral("resource")).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Notes of the sample\n"));
        QVERIFY(result->getFirstText().contains(QStringLiteral("Notes of the sample")));
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("read_attachment"), attachmentsId, QStringLiteral(R"({"index":%1,"maxBytes":5})").arg(notesIndex)));
        attachment = actionObject(result, QStringLiteral("read_attachment"));
        QVERIFY(attachment.value(QStringLiteral("truncated")).toBool());
        QCOMPARE(blocks(*result, QStringLiteral("resource")).first().toObject().value(QStringLiteral("resource")).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Notes"));
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("read_attachment"), attachmentsId, QStringLiteral(R"({"index":99})")));
        QVERIFY(isToolError(result));

        // Binary data is a blob; a cut UTF-8 sequence is dropped
        QString text;
        QVERIFY(!pdf::PDFAppMcpDocumentJob::decodeText(QByteArray("a\0b", 3), &text));
        QVERIFY(!pdf::PDFAppMcpDocumentJob::decodeText(QByteArray("\xff\xfe"), &text));
        QVERIFY(pdf::PDFAppMcpDocumentJob::decodeText(QByteArray("\xef\xbb\xbfplain"), &text));
        QCOMPARE(text, QStringLiteral("plain"));

        // Layers and the bookmarks of the user
        tab = openReady(fixture, pdftest::AppDocumentSamples::layers(fixture.folder()));
        QVERIFY(tab);
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_layers"), tab->getDocument()->getId()));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        const QJsonArray layers = actionObject(result, QStringLiteral("get_layers")).value(QStringLiteral("layers")).toArray();
        QCOMPARE(layers.size(), 2);
        for (const QJsonValue& value : layers)
        {
            const QJsonObject layer = value.toObject();
            QCOMPARE(layer.value(QStringLiteral("visible")).toBool(), layer.value(QStringLiteral("name")).toString() == QLatin1String("Layer A"));
        }

        QVERIFY(!pdf::PDFBookmarkRepository(fixture.context->getDatabase()).addBookmark(tab->getDocument()->getFilePath(), 0, QStringLiteral("Mine")).isError());
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_bookmarks"), tab->getDocument()->getId()));
        const QJsonArray bookmarks = actionObject(result, QStringLiteral("get_bookmarks")).value(QStringLiteral("bookmarks")).toArray();
        QCOMPARE(bookmarks.size(), 1);
        QCOMPARE(bookmarks.first().toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Mine"));
        QCOMPARE(bookmarks.first().toObject().value(QStringLiteral("page")).toInt(), 1);

        // The selection of the user
        tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("selection.pdf"), 2));
        QVERIFY(tab);
        const QString selectionId = tab->getDocument()->getId();
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_selection"), selectionId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QVERIFY(!actionObject(result, QStringLiteral("get_selection")).contains(QStringLiteral("selection")));
        tab->getView()->selectAllText();
        QTRY_VERIFY_WITH_TIMEOUT(tab->getView()->hasTextSelection(), 20000 * fixture.getTimeoutFactor());
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_selection"), selectionId));
        const QJsonObject selection = actionObject(result, QStringLiteral("get_selection")).value(QStringLiteral("selection")).toObject();
        QVERIFY2(selection.value(QStringLiteral("text")).toString().contains(QStringLiteral("Page 1")), qPrintable(describe(result)));
        QCOMPARE(selection.value(QStringLiteral("page")).toInt(), 1);
        QVERIFY(!selection.value(QStringLiteral("quads")).toArray().isEmpty());

        // Signatures: an unsigned document has none; the signed sample after its verification
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_signatures"), selectionId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QVERIFY(actionObject(result, QStringLiteral("get_signatures")).value(QStringLiteral("signatures")).toArray().isEmpty());
        tab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
        QVERIFY(tab);
        const QString signedId = tab->getDocument()->getId();
        QJsonArray signatures;
        QElapsedTimer timer;
        timer.start();
        do
        {
            result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_signatures"), signedId));
            QVERIFY2(isSuccess(result), qPrintable(describe(result)));
            signatures = actionObject(result, QStringLiteral("get_signatures")).value(QStringLiteral("signatures")).toArray();
        } while ((signatures.isEmpty() || signatures.first().toObject().value(QStringLiteral("status")).toString() == QLatin1String("pending")) &&
                 timer.elapsed() < 30000 * fixture.getTimeoutFactor());
        QCOMPARE(signatures.size(), 1);
        QVERIFY(signatures.first().toObject().contains(QStringLiteral("field")));
        QVERIFY(QStringList({ QStringLiteral("valid"), QStringLiteral("invalid"), QStringLiteral("unknown") }).contains(signatures.first().toObject().value(QStringLiteral("status")).toString()));

        // Activity: reads are logged (coalesced), get_state is not
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Ok));
        bool hasInfoRow = false;
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            hasInfoRow = hasInfoRow || row.action == QLatin1String("document.get_info");
            QVERIFY(row.action != QLatin1String("application.get_state"));
        }
        QVERIFY(hasInfoRow);
    }

    static void applicationState()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

        // No document
        Result result = call(fixture, QStringLiteral("application"), QStringLiteral(R"({"action":"get_state"})"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject state = actionObject(result, QStringLiteral("get_state"));
        QVERIFY(state.value(QStringLiteral("documents")).toArray().isEmpty());
        QVERIFY(!state.contains(QStringLiteral("activeDocumentId")));
        QCOMPARE(state.value(QStringLiteral("permissions")).toObject().value(QStringLiteral("read")).toString(), QStringLiteral("allow"));
        QCOMPARE(state.value(QStringLiteral("docs")).toString(), QStringLiteral("pdf4qt://docs/index.md"));
        QCOMPARE(state.value(QStringLiteral("limits")).toObject().value(QStringLiteral("maxRenderDpi")).toInt(), 600);
        QVERIFY(!state.value(QStringLiteral("version")).toString().isEmpty());
        QCOMPARE(state.value(QStringLiteral("era")).toString(), QStringLiteral("2026-07-28"));

        pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
        QVERIFY(tab);
        result = call(fixture, QStringLiteral("application"), QStringLiteral(R"({"action":"get_state"})"));
        state = actionObject(result, QStringLiteral("get_state"));
        QCOMPARE(state.value(QStringLiteral("activeDocumentId")).toString(), tab->getDocument()->getId());
        QVERIFY(state.value(QStringLiteral("activeRoute")).toString().startsWith(QStringLiteral("/document/")));
        const QJsonObject document = state.value(QStringLiteral("documents")).toArray().first().toObject();
        QCOMPARE(document.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QVERIFY(document.value(QStringLiteral("isActive")).toBool());
        QVERIFY(document.value(QStringLiteral("isSigned")).toBool());
        QVERIFY(document.value(QStringLiteral("hasForm")).toBool());
        QCOMPARE(document.value(QStringLiteral("currentPage")).toInt(), 1);
        QVERIFY(document.contains(QStringLiteral("path")));
        QVERIFY(!document.value(QStringLiteral("fileName")).toString().isEmpty());

        // The path only when Files is not Deny; no document list with Read = Deny
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Deny));
        result = call(fixture, QStringLiteral("application"), QStringLiteral(R"({"action":"get_state"})"));
        QVERIFY(!actionObject(result, QStringLiteral("get_state")).value(QStringLiteral("documents")).toArray().first().toObject().contains(QStringLiteral("path")));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
        result = call(fixture, QStringLiteral("application"), QStringLiteral(R"({"action":"get_state"})"));
        state = actionObject(result, QStringLiteral("get_state"));
        QVERIFY(!state.contains(QStringLiteral("documents")));
        QVERIFY(!state.contains(QStringLiteral("activeDocumentId")));
        QVERIFY(!state.value(QStringLiteral("note")).toString().isEmpty());
        QCOMPARE(state.value(QStringLiteral("permissions")).toObject().value(QStringLiteral("read")).toString(), QStringLiteral("deny"));

        // Not logged
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            QVERIFY(row.action != QLatin1String("application.get_state"));
        }
    }

    static void textActions()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("text.pdf"), 3));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();

        Result result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), id, QStringLiteral(R"({"pages":"1-2"})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject text = actionObject(result, QStringLiteral("get_text"));
        QJsonArray pages = text.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 2);
        QVERIFY2(pages[0].toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("Page 1")), qPrintable(pages[0].toObject().value(QStringLiteral("text")).toString()));
        QVERIFY(pages[1].toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("alpha beta gamma 2")));
        QVERIFY(!text.value(QStringLiteral("truncated")).toBool());
        QVERIFY(!text.contains(QStringLiteral("nextPage")));
        QCOMPARE(text.value(QStringLiteral("origin")).toString(), QStringLiteral("document"));
        QVERIFY(result->getFirstText().startsWith(pdf::PDFAppMcpToolSet::getUntrustedContentLine()));
        QVERIFY(result->getFirstText().contains(QStringLiteral("--- Page 2 (label 2) ---")));

        // Geometry of words and lines
        const QRectF word = findWord(fixture, id, QStringLiteral("alpha"));
        QVERIFY(word.isValid());
        QVERIFY(word.left() > 20 && word.right() < 150 && word.top() > 300 && word.bottom() < 400);
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), id, QStringLiteral(R"({"pages":"1","geometry":"lines"})")));
        QCOMPARE(actionObject(result, QStringLiteral("get_text")).value(QStringLiteral("pages")).toArray().first().toObject().value(QStringLiteral("lines")).toArray().size(), 3);

        // Labels and the argument rules
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), id, QStringLiteral(R"({"pageLabel":"3"})")));
        QCOMPARE(actionObject(result, QStringLiteral("get_text")).value(QStringLiteral("pages")).toArray().first().toObject().value(QStringLiteral("page")).toInt(), 3);
        for (const char* invalid : { R"({})", R"({"pages":"1","pageLabel":"1"})", R"({"pageLabel":"zz"})", R"({"pages":"9"})", R"({"pages":"1-x"})" })
        {
            result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), id, QString::fromLatin1(invalid)));
            QVERIFY2(isToolError(result), invalid);
        }

        // Search: matches with quads, filters, limits, invalid expressions
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"alpha"})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonArray matches = actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray();
        QCOMPARE(matches.size(), 3);
        const QJsonObject match = matches.first().toObject();
        QCOMPARE(match.value(QStringLiteral("page")).toInt(), 1);
        QCOMPARE(match.value(QStringLiteral("text")).toString(), QStringLiteral("alpha"));
        QCOMPARE(match.value(QStringLiteral("quads")).toArray().first().toArray().size(), 8);
        QVERIFY(match.value(QStringLiteral("context")).toString().contains(QStringLiteral("alpha")));
        QVERIFY(rect(match.value(QStringLiteral("rect")).toObject()).intersects(word));
        QVERIFY(result->getFirstText().contains(QStringLiteral("3 matches")));

        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"alpha","pages":"2"})")));
        matches = actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray();
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.first().toObject().value(QStringLiteral("page")).toInt(), 2);
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"alpha","maxResults":1})")));
        QCOMPARE(actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray().size(), 1);
        QVERIFY(actionObject(result, QStringLiteral("search")).value(QStringLiteral("truncated")).toBool());
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"ALPHA","matchCase":true})")));
        QVERIFY(actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray().isEmpty());
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"al*a","wildcards":true})")));
        QCOMPARE(actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray().size(), 3);
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"gam+a \\d","regularExpression":true})")));
        QCOMPARE(actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray().size(), 3);
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"(","regularExpression":true})")));
        QVERIFY(isToolError(result));
        QVERIFY2(result->getFirstText().contains(QStringLiteral("regular expression")), qPrintable(result->getFirstText()));
        for (const char* invalid : { R"({"query":"a","regularExpression":true,"wildcards":true})", R"({"query":""})", R"({"query":"a","pages":"7"})" })
        {
            result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QString::fromLatin1(invalid)));
            QVERIFY2(isToolError(result), invalid);
        }

        // The text of an area
        const QRectF pageWord = findWord(fixture, id, QStringLiteral("Page"));
        QVERIFY(pageWord.isValid());
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text_in_area"), id,
                                                            QStringLiteral(R"({"page":1,"rect":{"left":%1,"bottom":%2,"right":%3,"top":%4}})")
                                                                .arg(pageWord.left() - 2).arg(pageWord.top() - 2).arg(pageWord.right() + 25).arg(pageWord.bottom() + 2)));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("get_text_in_area")).value(QStringLiteral("text")).toString(), QStringLiteral("Page 1"));
        QCOMPARE(actionObject(result, QStringLiteral("get_text_in_area")).value(QStringLiteral("words")).toArray().size(), 2);
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text_in_area"), id, QStringLiteral(R"({"page":1,"rect":{"left":0,"bottom":0,"right":1,"top":1}})")));
        QVERIFY(actionObject(result, QStringLiteral("get_text_in_area")).value(QStringLiteral("text")).toString().isEmpty());
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text_in_area"), id, QStringLiteral(R"({"page":9,"rect":{"left":0,"bottom":0,"right":1,"top":1}})")));
        QVERIFY(isToolError(result));

        // More than 50 pages per call are refused, nextPage continues
        pdf::PDFAppDocumentTab* large = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 60));
        QVERIFY(large);
        const QString largeId = large->getDocument()->getId();
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), largeId, QStringLiteral(R"({"pages":"1-51"})")));
        QVERIFY(isToolError(result));
        result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), largeId, QStringLiteral(R"({"pages":"1-50","maxCharacters":1000})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        text = actionObject(result, QStringLiteral("get_text"));
        QVERIFY(text.value(QStringLiteral("pages")).toArray().size() < 50);
        QVERIFY(text.value(QStringLiteral("nextPage")).toInt() > 1);
        qint64 characters = 0;
        for (const QJsonValue& page : text.value(QStringLiteral("pages")).toArray())
        {
            characters += page.toObject().value(QStringLiteral("text")).toString().size();
        }
        QVERIFY(characters <= 1000);
    }

    static void textGeometryAndSearch()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("geometry.pdf"), 4));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();

        // The quads of the search equal the quads of PDFDocumentTextSearch of the
        // index of the document (they feed annotations.add unchanged)
        Result result = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"quick brown"})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        std::shared_ptr<const pdf::PDFDocumentTextIndex> index = tab->getDocument()->getTextIndex();
        QVERIFY(index);
        const pdf::PDFDocumentTextSearchResult expected = pdf::PDFDocumentTextSearch::find(*index, QStringLiteral("quick brown"), pdf::PDFDocumentTextSearchOptions(), 100, nullptr);
        const QJsonArray matches = actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray();
        QCOMPARE(matches.size(), int(expected.results.size()));
        for (int i = 0; i < matches.size(); ++i)
        {
            const QJsonArray quads = matches[i].toObject().value(QStringLiteral("quads")).toArray();
            const QPolygonF& polygon = expected.results[size_t(i)].quadrilaterals;
            QCOMPARE(qsizetype(quads.size()) * 4, polygon.size());
            for (int q = 0; q < quads.size(); ++q)
            {
                const QJsonArray quad = quads[q].toArray();
                for (int point = 0; point < 4; ++point)
                {
                    QVERIFY(qAbs(quad[2 * point].toDouble() - polygon[4 * q + point].x()) < 0.001);
                    QVERIFY(qAbs(quad[2 * point + 1].toDouble() - polygon[4 * q + point].y()) < 0.001);
                }
            }
            QCOMPARE(matches[i].toObject().value(QStringLiteral("page")).toInt(), int(expected.results[size_t(i)].pageIndex + 1));
        }

        // The words of the text with geometry lie inside of the page and do not overlap the next line
        const QRectF quick = findWord(fixture, id, QStringLiteral("quick"), 2);
        const QRectF alpha = findWord(fixture, id, QStringLiteral("alpha"), 2);
        QVERIFY(quick.isValid() && alpha.isValid());
        QVERIFY(quick.bottom() <= alpha.top() + 1.0);

        // The words of a single page layout (no index) equal those of the index
        const pdf::PDFTextLayout layout = pdf::PDFAppMcpTextService::createPageLayout(tab->getDocument()->getDocument().data(), 1);
        const std::vector<std::pair<QString, QRectF>> words = pdf::PDFAppMcpTextService::getWords(layout);
        const std::vector<std::pair<QString, QRectF>> indexWords = pdf::PDFAppMcpTextService::getWords(index->getTextLayout(1));
        QCOMPARE(words.size(), indexWords.size());
        QVERIFY(pdf::PDFAppMcpTextService::createPageLayout(nullptr, 0).getTextBlocks().empty());
        QVERIFY(pdf::PDFAppMcpTextService::createPageLayout(tab->getDocument()->getDocument().data(), 99).getTextBlocks().empty());
    }

    static void indexBuildProgress()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 300));
        QVERIFY(tab);
        QVERIFY(!tab->getDocument()->getTextIndex());

        // The search waits for the index of the tab and reports its progress
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("text"),
                                                                  json(args(QStringLiteral("search"), tab->getDocument()->getId(), QStringLiteral(R"({"query":"lazy dog","maxResults":500})"))), true);
        const Result result = pdftest::AppMcpFixture::waitResult(call, 120000);
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("search")).value(QStringLiteral("matches")).toArray().size(), 300);
        QVERIFY(tab->getDocument()->getTextIndex());
        bool hasIndexingProgress = false;
        for (const pdf::PDFMcpCallEvent& event : call->takeEvents())
        {
            hasIndexingProgress = hasIndexingProgress || (event.type == pdf::PDFMcpCallEvent::Type::Progress && event.message.startsWith(QStringLiteral("Indexing text")));
        }
        QVERIFY(hasIndexingProgress || call->getLastProgress() >= 0.0);

        // A search of a ready index starts no build
        const Result second = call_(fixture, tab->getDocument()->getId());
        QVERIFY2(isSuccess(second), qPrintable(describe(second)));

        // A document without the indexer of a tab builds its index in the job
        pdf::PDFAppDocumentTab* other = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("index.pdf"), 5));
        QVERIFY(other);
        pdf::PDFAppDocumentSearchCommandHandler* handler = pdf::PDFAppDocumentSearchCommandHandler::findHandler(other);
        QVERIFY(handler && handler->getIndexer());
        delete handler->getIndexer();
        QVERIFY(!pdf::PDFAppMcpTextService::findIndexer(fixture.host(), other->getDocument()));
        const Result third = call_(fixture, other->getDocument()->getId());
        QVERIFY2(isSuccess(third), qPrintable(describe(third)));
        QTRY_VERIFY(other->getDocument()->getTextIndex());
    }

    static Result call_(pdftest::AppMcpFixture& fixture, const QString& id)
    {
        return call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"alpha"})")));
    }

    static void renderGeometry()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

        // A plain page at 72 dpi: 1 px = 1 pt, the y axis flipped
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("render.pdf"), 2));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        Result result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), id, QStringLiteral(R"({"page":1,"dpi":72})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject page = actionObject(result, QStringLiteral("page"));
        QCOMPARE(page.value(QStringLiteral("widthPx")).toInt(), 300);
        QCOMPARE(page.value(QStringLiteral("heightPx")).toInt(), 400);
        QCOMPARE(page.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
        QJsonArray matrix = page.value(QStringLiteral("pageToImage")).toArray();
        QCOMPARE(matrix.size(), 6);
        QCOMPARE(mapRect(matrix, QRectF(0, 0, 300, 400)), QRectF(0, 0, 300, 400));
        QCOMPARE(mapRect(matrix, QRectF(10, 390, 0, 0)).topLeft(), QPointF(10, 10));
        const QRectF back = mapRect(page.value(QStringLiteral("imageToPage")).toArray(), QRectF(10, 10, 0, 0));
        QCOMPARE(back.topLeft(), QPointF(10, 390));
        QString mimeType;
        QImage image = AppMcpReadTests::image(*result, &mimeType);
        QCOMPARE(mimeType, QStringLiteral("image/png"));
        QCOMPARE(image.size(), QSize(300, 400));
        QCOMPARE(blocks(*result, QStringLiteral("resource_link")).first().toObject().value(QStringLiteral("uri")).toString(),
                 QStringLiteral("pdf4qt://documents/%1/pages/1/image/72").arg(id));

        // The text of a word maps onto dark pixels, an empty corner is white
        const QRectF word = findWord(fixture, id, QStringLiteral("Page"));
        QVERIFY(word.isValid());
        QVERIFY(darkPixels(image, mapRect(matrix, word).toAlignedRect()) > 5);
        QCOMPARE(darkPixels(image, QRect(200, 300, 90, 90)), 0);

        // 150 dpi and an area
        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), id, QStringLiteral(R"({"page":1,"dpi":144,"area":{"left":0,"bottom":300,"right":150,"top":400}})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        page = actionObject(result, QStringLiteral("page"));
        QCOMPARE(page.value(QStringLiteral("widthPx")).toInt(), 300);
        QCOMPARE(page.value(QStringLiteral("heightPx")).toInt(), 200);
        QCOMPARE(rect(page.value(QStringLiteral("area")).toObject()), QRectF(0, 300, 150, 100));
        image = AppMcpReadTests::image(*result);
        QVERIFY(darkPixels(image, mapRect(page.value(QStringLiteral("pageToImage")).toArray(), word).toAlignedRect()) > 5);
        for (const char* invalid : { R"({"page":1,"area":{"left":400,"bottom":500,"right":600,"top":700}})", R"({"page":1,"area":{"left":0,"bottom":0,"right":0,"top":0}})",
                                     R"({"page":3})", R"({"page":1,"pageLabel":"1"})", R"({"pageLabel":"x"})", R"({"page":1,"dpi":601})", R"({"page":1,"dpi":17})" })
        {
            result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), id, QString::fromLatin1(invalid)));
            QVERIFY2(isToolError(result), invalid);
        }

        // JPEG
        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), id, QStringLiteral(R"({"pageLabel":"2","format":"jpeg","jpegQuality":30})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QVERIFY(!AppMcpReadTests::image(*result, &mimeType).isNull());
        QCOMPARE(mimeType, QStringLiteral("image/jpeg"));
        QCOMPARE(actionObject(result, QStringLiteral("page")).value(QStringLiteral("page")).toInt(), 2);

        // A rotated page, a crop box and the user unit
        const QString geometryFile = makeSample(fixture.folder(), QStringLiteral("rotated.pdf"), 3, [](pdf::PDFDocumentBuilder& builder, const std::vector<pdf::PDFObjectReference>& pages)
        {
            builder.mergeTo(pages[0], dictionary({ { "Rotate", integer(90) } }));
            builder.mergeTo(pages[1], dictionary({ { "CropBox", array({ integer(20), integer(250), integer(220), integer(400) }) } }));
            builder.mergeTo(pages[2], dictionary({ { "UserUnit", number(2.0) } }));
        });
        tab = openReady(fixture, geometryFile);
        QVERIFY(tab);
        const QString rotatedId = tab->getDocument()->getId();

        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), rotatedId, QStringLiteral(R"({"page":1,"dpi":72})")));
        page = actionObject(result, QStringLiteral("page"));
        QCOMPARE(page.value(QStringLiteral("widthPx")).toInt(), 400);
        QCOMPARE(page.value(QStringLiteral("heightPx")).toInt(), 300);
        image = AppMcpReadTests::image(*result);
        const QRectF rotatedWord = findWord(fixture, rotatedId, QStringLiteral("Page"), 1);
        QVERIFY(rotatedWord.isValid());
        QVERIFY(darkPixels(image, mapRect(page.value(QStringLiteral("pageToImage")).toArray(), rotatedWord).toAlignedRect()) > 5);
        result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_pages"), rotatedId));
        const QJsonArray pages = actionObject(result, QStringLiteral("get_pages")).value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages[0].toObject().value(QStringLiteral("rotation")).toInt(), 90);
        QCOMPARE(pages[0].toObject().value(QStringLiteral("widthPt")).toDouble(), 400.0);
        QCOMPARE(rect(pages[1].toObject().value(QStringLiteral("cropBox")).toObject()), QRectF(20, 250, 200, 150));
        QCOMPARE(pages[2].toObject().value(QStringLiteral("userUnit")).toDouble(), 2.0);

        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), rotatedId, QStringLiteral(R"({"page":2,"dpi":72})")));
        page = actionObject(result, QStringLiteral("page"));
        QCOMPARE(page.value(QStringLiteral("widthPx")).toInt(), 200);
        QCOMPARE(page.value(QStringLiteral("heightPx")).toInt(), 150);
        QCOMPARE(mapRect(page.value(QStringLiteral("pageToImage")).toArray(), QRectF(20, 400, 0, 0)).topLeft(), QPointF(0, 0));
        image = AppMcpReadTests::image(*result);
        const QRectF croppedWord = findWord(fixture, rotatedId, QStringLiteral("Page"), 2);
        QVERIFY(darkPixels(image, mapRect(page.value(QStringLiteral("pageToImage")).toArray(), croppedWord).toAlignedRect()) > 5);

        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), rotatedId, QStringLiteral(R"({"page":3,"dpi":72})")));
        page = actionObject(result, QStringLiteral("page"));
        QCOMPARE(page.value(QStringLiteral("widthPx")).toInt(), 600);
        QCOMPARE(page.value(QStringLiteral("heightPx")).toInt(), 800);

        // Too many pixels: an execution error before any work
        pdf::PDFAppMcpRenderGeometry geometry;
        QString error;
        const pdf::PDFPage* hugePage = tab->getDocument()->getDocument()->getCatalog()->getPage(2);
        geometry = pdf::PDFAppMcpRenderer::getGeometry(hugePage, 600, std::nullopt, &error);
        QVERIFY(geometry.isValid());
        geometry = pdf::PDFAppMcpRenderer::getGeometry(nullptr, 600, std::nullopt, &error);
        QVERIFY(!geometry.isValid());
        const QString hugeFile = makeSample(fixture.folder(), QStringLiteral("huge.pdf"), 1, [](pdf::PDFDocumentBuilder& builder, const std::vector<pdf::PDFObjectReference>& pages)
        {
            builder.mergeTo(pages[0], dictionary({ { "UserUnit", number(50.0) } }));
        });
        pdf::PDFAppDocumentTab* hugeTab = openReady(fixture, hugeFile);
        QVERIFY(hugeTab);
        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), hugeTab->getDocument()->getId(), QStringLiteral(R"({"page":1,"dpi":600})")));
        QVERIFY(isToolError(result));
        QVERIFY2(result->getFirstText().contains(QStringLiteral("megapixels")), qPrintable(result->getFirstText()));

        // Annotations shown and hidden
        pdf::PDFAppDocumentTab* annotationsTab = openReady(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
        QVERIFY(annotationsTab);
        const QString annotationsId = annotationsTab->getDocument()->getId();
        const Result shown = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), annotationsId, QStringLiteral(R"({"page":1,"dpi":72})")));
        const Result hidden = call(fixture, QStringLiteral("render"), args(QStringLiteral("page"), annotationsId, QStringLiteral(R"({"page":1,"dpi":72,"annotations":"hide"})")));
        QVERIFY(isSuccess(shown) && isSuccess(hidden));
        QVERIFY(AppMcpReadTests::image(*shown) != AppMcpReadTests::image(*hidden));

        // The overview sheet
        result = call(fixture, QStringLiteral("render"), args(QStringLiteral("overview"), rotatedId, QStringLiteral(R"({"pages":"1-3","maxSize":64})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        const QJsonObject overview = actionObject(result, QStringLiteral("overview"));
        QCOMPARE(overview.value(QStringLiteral("cells")).toArray().size(), 3);
        image = AppMcpReadTests::image(*result);
        QCOMPARE(image.width(), overview.value(QStringLiteral("widthPx")).toInt());
        for (const QJsonValue& cell : overview.value(QStringLiteral("cells")).toArray())
        {
            const QJsonObject object = cell.toObject();
            QVERIFY(object.value(QStringLiteral("width")).toInt() <= 64 && object.value(QStringLiteral("height")).toInt() <= 64);
            QVERIFY(QRect(object.value(QStringLiteral("x")).toInt(), object.value(QStringLiteral("y")).toInt(), object.value(QStringLiteral("width")).toInt(),
                          object.value(QStringLiteral("height")).toInt()).intersects(image.rect()));
        }
        for (const char* invalid : { R"({"pages":"1-25"})", R"({"pages":""})", R"({"pages":"4"})" })
        {
            result = call(fixture, QStringLiteral("render"), args(QStringLiteral("overview"), rotatedId, QString::fromLatin1(invalid)));
            QVERIFY2(isToolError(result), invalid);
        }
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Ok));
    }

    static void documentResolution()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

        // One document: no question
        pdf::PDFAppDocumentTab* first = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("first.pdf"), 2));
        QVERIFY(first);
        activateHome(fixture);
        Result result = call(fixture, QStringLiteral("document"), QStringLiteral(R"({"action":"get_info"})"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));

        // A second document with labels 1, 2, 3, 1, 2, 3
        const QString labelsFile = makeSample(fixture.folder(), QStringLiteral("labels.pdf"), 6, [](pdf::PDFDocumentBuilder& builder, const std::vector<pdf::PDFObjectReference>&)
        {
            const pdf::PDFObject labels = dictionary({ { "Nums", array({ integer(0), dictionary({ { "S", name("D") } }), integer(3), dictionary({ { "S", name("D") } }) }) } });
            builder.mergeTo(builder.getCatalogReference(), dictionary({ { "PageLabels", labels } }));
        });
        pdf::PDFAppDocumentTab* second = openReady(fixture, labelsFile);
        QVERIFY(second);
        const QString firstId = first->getDocument()->getId();
        const QString secondId = second->getDocument()->getId();

        // The active document tab is chosen without a question
        result = call(fixture, QStringLiteral("document"), QStringLiteral(R"({"action":"get_info"})"));
        QCOMPARE(actionObject(result, QStringLiteral("get_info")).value(QStringLiteral("documentId")).toString(), secondId);

        // Several documents, no active document tab, no elicitation: the fallback text
        activateHome(fixture);
        result = call(fixture, QStringLiteral("document"), QStringLiteral(R"({"action":"get_info"})"));
        QVERIFY(isToolError(result));
        QVERIFY(result->getFirstText().contains(QStringLiteral("Pass documentId, one of:")));
        QVERIFY(result->getFirstText().contains(firstId) && result->getFirstText().contains(secondId));

        // Modern elicitation: the choice, the retry with the answer
        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Modern, json(QStringLiteral(R"({"elicitation":{}})")));
        pdf::PDFMcpCallRequest request = fixture.createRequest(QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_info"})")));
        result = pdftest::AppMcpFixture::waitResult(start(fixture, request));
        QVERIFY(result);
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        QCOMPARE(result->inputRequests.requests.size(), size_t(1));
        const pdf::PDFMcpInputRequest& question = result->inputRequests.requests.front();
        QCOMPARE(question.key, pdf::PDFAppMcpElicitation::getDocumentKey());
        const QJsonObject schema = QJsonDocument::fromJson(question.requestedSchema).object();
        const QJsonObject property = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("documentId")).toObject();
        QCOMPARE(property.value(QStringLiteral("oneOf")).toArray().size(), 2);
        QCOMPARE(schema.value(QStringLiteral("required")).toArray().first().toString(), QStringLiteral("documentId"));
        QVERIFY(!question.message.isEmpty());
        const pdf::PDFMcpCallResult documentQuestion = *result;

        result = retry(fixture, request, documentQuestion, accept(pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"), firstId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("get_info")).value(QStringLiteral("documentId")).toString(), firstId);

        // A declined question, a forged answer, an answer without its state
        QJsonObject declined({ { pdf::PDFAppMcpElicitation::getDocumentKey(), QJsonObject({ { QStringLiteral("action"), QStringLiteral("decline") } }) } });
        result = retry(fixture, request, documentQuestion, declined);
        QVERIFY(isToolError(result));
        QVERIFY(result->getFirstText().contains(QStringLiteral("did not choose")));
        result = retry(fixture, request, documentQuestion, accept(pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"), QString(26, QLatin1Char('a'))));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        pdf::PDFMcpCallRequest stateless = request;
        stateless.inputResponses = accept(pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"), firstId);
        result = pdftest::AppMcpFixture::waitResult(start(fixture, stateless));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);

        // The legacy era 2025-06-18: enum with enumNames
        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Legacy20250618, json(QStringLiteral(R"({"elicitation":{}})")));
        result = call(fixture, QStringLiteral("text"), QStringLiteral(R"({"action":"search","search":{"query":"alpha"}})"));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        const QJsonObject legacyProperty = QJsonDocument::fromJson(result->inputRequests.requests.front().requestedSchema).object()
                                               .value(QStringLiteral("properties")).toObject().value(QStringLiteral("documentId")).toObject();
        QCOMPARE(legacyProperty.value(QStringLiteral("enum")).toArray().size(), 2);
        QCOMPARE(legacyProperty.value(QStringLiteral("enumNames")).toArray().size(), 2);
        QVERIFY(!legacyProperty.contains(QStringLiteral("oneOf")));

        // Two rounds: the document, then the page of an ambiguous label; the first
        // answer is carried in the state of the second round
        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Modern, json(QStringLiteral(R"({"elicitation":{"form":{}}})")));
        request = fixture.createRequest(QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pageLabel":"2"}})")));
        result = pdftest::AppMcpFixture::waitResult(start(fixture, request));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        result = retry(fixture, request, *result, accept(pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"), secondId));
        QVERIFY(result);
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        QCOMPARE(result->inputRequests.requests.front().key, pdf::PDFAppMcpElicitation::getPageKey());
        QVERIFY(result->inputRequests.allowedValues.contains(pdf::PDFAppMcpElicitation::getCarriedPrefix() + pdf::PDFAppMcpElicitation::getDocumentKey()));
        const QJsonObject pageProperty = QJsonDocument::fromJson(result->inputRequests.requests.front().requestedSchema).object()
                                             .value(QStringLiteral("properties")).toObject().value(QStringLiteral("page")).toObject();
        QCOMPARE(pageProperty.value(QStringLiteral("oneOf")).toArray().size(), 2);
        result = retry(fixture, request, *result, accept(pdf::PDFAppMcpElicitation::getPageKey(), QStringLiteral("page"), QStringLiteral("5")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        const QJsonObject text = actionObject(result, QStringLiteral("get_text"));
        QCOMPARE(text.value(QStringLiteral("documentId")).toString(), secondId);
        QCOMPARE(text.value(QStringLiteral("pages")).toArray().first().toObject().value(QStringLiteral("page")).toInt(), 5);
        QCOMPARE(text.value(QStringLiteral("pages")).toArray().first().toObject().value(QStringLiteral("label")).toString(), QStringLiteral("2"));

        // Without the capability: the page of the label is the fallback text
        fixture.setClient(QStringLiteral("gemini-cli-mcp-client"));
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), secondId, QStringLiteral(R"({"pageLabel":"2"})")));
        QVERIFY(isToolError(result));
        QVERIFY2(result->getFirstText().contains(QStringLiteral("physical pages 2, 5")), qPrintable(result->getFirstText()));

        // The elicitation helpers with hostile states
        pdf::PDFMcpCallRequest hostile = fixture.createRequest(QStringLiteral("text"), QJsonObject());
        hostile.requestState = json(QStringLiteral(R"({"allowedValues":{"carried:pdf4qt.document":{"documentId":["x","y"]}}})"));
        QCOMPARE(pdf::PDFAppMcpElicitation::getAnswer(hostile, pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId")).status,
                 pdf::PDFAppMcpElicitationAnswer::Status::None);
        hostile.inputResponses = json(QStringLiteral(R"({"pdf4qt.document":"accept"})"));
        QCOMPARE(pdf::PDFAppMcpElicitation::getAnswer(hostile, pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId")).status,
                 pdf::PDFAppMcpElicitationAnswer::Status::None);
        hostile.inputResponses = json(QStringLiteral(R"({"pdf4qt.document":{"action":"accept","content":{"documentId":5}}})"));
        QCOMPARE(pdf::PDFAppMcpElicitation::getAnswer(hostile, pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId")).status,
                 pdf::PDFAppMcpElicitationAnswer::Status::Invalid);
        hostile.inputResponses = json(QStringLiteral(R"({"pdf4qt.document":{"action":"cancel"}})"));
        QCOMPARE(pdf::PDFAppMcpElicitation::getAnswer(hostile, pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId")).status,
                 pdf::PDFAppMcpElicitationAnswer::Status::Canceled);
        hostile.requestState = json(QStringLiteral(R"({"carried:pdf4qt.document":{"documentId":["abc"]}})"));
        hostile.inputResponses = QJsonObject();
        QCOMPARE(pdf::PDFAppMcpElicitation::getAnswer(hostile, pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId")).value.toString(), QStringLiteral("abc"));
        QVERIFY(!pdf::PDFAppMcpElicitation::isSupported(fixture.createRequest(QString(), QJsonObject())));
    }

    static void readPermissionMatrix()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("permissions.pdf"), 2));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();

        // Allow: no question
        QVERIFY(isSuccess(call(fixture, QStringLiteral("application"), QStringLiteral(R"({"action":"get_state"})"))));
        QVERIFY(isSuccess(readResource(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("info"))));
        QVERIFY(fixture.confirmations()->shown.empty());

        // Deny: the actions are not offered, the resources and the completion refuse
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
        Result result = call(fixture, QStringLiteral("document"), args(QStringLiteral("get_info"), id));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        result = readResource(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("info"));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QVERIFY(result->errorMessage.contains(QStringLiteral("Deny")));
        QVERIFY(completionValues(complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/info"},"argument":{"name":"documentId","value":""}})"))).isEmpty());
        QCOMPARE(pdf::PDFAppMcpDocumentResources::createListed(fixture.host()).size(), size_t(1));

        // Ask: one approval per assistant, the next reads do not ask, idle forgets
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
        std::shared_ptr<pdf::PDFMcpCall> pending = fixture.startCall(QStringLiteral("application"), json(QStringLiteral(R"({"action":"get_state"})")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        result = pdftest::AppMcpFixture::waitResult(pending);
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QVERIFY(!actionObject(result, QStringLiteral("get_state")).contains(QStringLiteral("documents")));
        QVERIFY(!actionObject(result, QStringLiteral("get_state")).value(QStringLiteral("note")).toString().isEmpty());

        // The completion discloses nothing before the approval
        QVERIFY(completionValues(complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/info"},"argument":{"name":"documentId","value":""}})"))).isEmpty());

        // A resource asks; Allow covers the next reads and the documents opened later
        std::shared_ptr<pdf::PDFMcpCall> resourceCall = start(fixture, resourceRequest(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("info")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(resourceCall);
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        const size_t shown = fixture.confirmations()->shown.size();
        QVERIFY(isSuccess(call(fixture, QStringLiteral("document"), args(QStringLiteral("get_info"), id))));
        pdf::PDFAppDocumentTab* later = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("later.pdf"), 1));
        QVERIFY(later);
        QVERIFY(isSuccess(call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), later->getDocument()->getId(), QStringLiteral(R"({"pages":"1"})")))));
        QCOMPARE(fixture.confirmations()->shown.size(), shown);
        QCOMPARE(completionValues(complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/info"},"argument":{"name":"documentId","value":""}})"))).size(), 2);

        // A refused resource read: a protocol error with the URI
        fixture.setClient(QStringLiteral("codex-mcp-client"));
        resourceCall = start(fixture, resourceRequest(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("outline")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedTimeout));
        result = pdftest::AppMcpFixture::waitResult(resourceCall);
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QVERIFY(result->errorMessage.contains(QStringLiteral("2 minutes")));

        // A prompt naming a document asks too; a refused prompt does not name it
        std::shared_ptr<pdf::PDFMcpCall> promptCall = start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), QJsonObject()));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        result = pdftest::AppMcpFixture::waitResult(promptCall);
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Complete, qPrintable(describe(result)));
        QVERIFY(!QString::fromUtf8(result->result).contains(id));

        // Idle forgets the approval of the first assistant
        fixture.setClient(QStringLiteral("claude-code"));
        pdf::PDFMcpAssistantEvent event;
        event.kind = pdf::PDFMcpAssistantEvent::Kind::Idle;
        event.key = fixture.createRequest(QString(), QJsonObject()).client.assistantKey;
        fixture.host()->assistantEvent(event);
        pending = fixture.startCall(QStringLiteral("application"), json(QStringLiteral(R"({"action":"get_state"})")));
        QVERIFY(fixture.confirmDialog());
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        result = pdftest::AppMcpFixture::waitResult(pending);
        QCOMPARE(actionObject(result, QStringLiteral("get_state")).value(QStringLiteral("documents")).toArray().size(), 2);
        QVERIFY(actionObject(result, QStringLiteral("get_state")).value(QStringLiteral("readApproved")).toBool());

        // A call canceled while its approval waits ends without reading
        fixture.setClient(QStringLiteral("cursor"));
        pending = start(fixture, resourceRequest(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("text")));
        QVERIFY(fixture.confirmDialog());
        pending->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        fixture.host()->callCanceled(pending);
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));
    }

    static void resourcesDocument()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        const quint64 generation = fixture.host()->getCatalogGeneration();
        pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::attachments(fixture.folder()));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        const QString prefix = pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id);

        // The listed resources follow the documents (a new catalog - list_changed)
        QTRY_VERIFY(fixture.host()->getCatalogGeneration() > generation);
        std::shared_ptr<const pdf::PDFMcpCatalog> catalog = fixture.host()->getCatalog();
        QVERIFY(catalog->findResource(pdf::PDFAppMcpDocumentResources::getStateUri()));
        QVERIFY(catalog->findResource(prefix + QStringLiteral("info")));
        QVERIFY(catalog->findResource(prefix + QStringLiteral("outline")));
        QVERIFY(catalog->findResource(prefix + QStringLiteral("annotations")));
        QMap<QString, QString> values;
        QVERIFY(catalog->matchTemplate(prefix + QStringLiteral("pages/1/image/150"), &values));
        QCOMPARE(values.value(QStringLiteral("dpi")), QStringLiteral("150"));

        // Every kind of resource
        Result result = readResource(fixture, prefix + QStringLiteral("info"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QJsonObject content = resourceContent(result);
        QCOMPARE(content.value(QStringLiteral("uri")).toString(), prefix + QStringLiteral("info"));
        QCOMPARE(content.value(QStringLiteral("mimeType")).toString(), QStringLiteral("application/json"));
        QCOMPARE(json(content.value(QStringLiteral("text")).toString()).value(QStringLiteral("documentId")).toString(), id);

        for (const char* path : { "pages", "outline", "annotations", "pages/1/annotations", "form" })
        {
            result = readResource(fixture, prefix + QString::fromLatin1(path));
            QVERIFY2(isSuccess(result), path);
            QVERIFY2(!json(resourceContent(result).value(QStringLiteral("text")).toString()).isEmpty(), path);
        }
        result = readResource(fixture, prefix + QStringLiteral("pages"));
        QCOMPARE(json(resourceContent(result).value(QStringLiteral("text")).toString()).value(QStringLiteral("pages")).toArray().size(), 2);

        result = readResource(fixture, prefix + QStringLiteral("text"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(resourceContent(result).value(QStringLiteral("mimeType")).toString(), QStringLiteral("text/plain"));
        QVERIFY(resourceContent(result).value(QStringLiteral("text")).toString().contains(QStringLiteral("--- Page 2")));
        result = readResource(fixture, prefix + QStringLiteral("pages/2/text"));
        QVERIFY(resourceContent(result).value(QStringLiteral("text")).toString().contains(QStringLiteral("alpha beta gamma 2")));

        result = readResource(fixture, prefix + QStringLiteral("pages/1/image/72"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        content = resourceContent(result);
        QCOMPARE(content.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
        QCOMPARE(QImage::fromData(QByteArray::fromBase64(content.value(QStringLiteral("blob")).toString().toLatin1())).size(), QSize(300, 400));

        result = readResource(fixture, prefix + QStringLiteral("attachments/0"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QVERIFY(resourceContent(result).contains(QStringLiteral("text")) || resourceContent(result).contains(QStringLiteral("blob")));

        result = readResource(fixture, pdf::PDFAppMcpDocumentResources::getStateUri());
        QCOMPARE(json(resourceContent(result).value(QStringLiteral("text")).toString()).value(QStringLiteral("documents")).toArray().size(), 1);

        // Not found: -32602 with data.uri (modern), -32002 (legacy)
        const QString closedId(26, QLatin1Char('b'));
        for (const QString& uri : { pdf::PDFAppMcpDocumentResources::getDocumentPrefix(closedId) + QStringLiteral("info"), prefix + QStringLiteral("pages/0/text"),
                                    prefix + QStringLiteral("pages/3/text"), prefix + QStringLiteral("pages/01/text"), prefix + QStringLiteral("pages/1/image/17"),
                                    prefix + QStringLiteral("pages/1/image/601"), prefix + QStringLiteral("attachments/99"), prefix + QStringLiteral("attachments/-1"),
                                    prefix + QStringLiteral("nothing"), QStringLiteral("pdf4qt://documents/%00/info"), QStringLiteral("pdf4qt://documents/") + QString(3000, QLatin1Char('x')),
                                    QStringLiteral("https://example.com/") })
        {
            result = readResource(fixture, uri);
            QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Error, qPrintable(uri.left(80)));
            QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
            QCOMPARE(QJsonDocument::fromJson(result->errorData).object().value(QStringLiteral("uri")).toString().left(40), uri.left(40));
        }
        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Legacy20251125);
        result = readResource(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(closedId) + QStringLiteral("info"));
        QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::RESOURCE_NOT_FOUND_LEGACY);

        // A closed document leaves the list
        const quint64 beforeClose = fixture.host()->getCatalogGeneration();
        QVERIFY(fixture.window->closeTab(tab));
        QTRY_VERIFY(fixture.host()->getCatalogGeneration() > beforeClose);
        QVERIFY(!fixture.host()->getCatalog()->findResource(prefix + QStringLiteral("info")));
        pdf::PDFAppMcpDocumentWatcher* watcher = pdf::PDFAppMcpDocumentWatcher::find(fixture.host());
        QVERIFY(watcher);
        QVERIFY(watcher->getRebuildCount() >= 2);
        QCOMPARE(pdf::PDFAppMcpDocumentWatcher::ensure(fixture.host()), watcher);

        // The URI parser
        QCOMPARE(pdf::PDFAppMcpDocumentResources::parseUri(prefix + QStringLiteral("pages/7/image/96")).kind, pdf::PDFAppMcpResourceKind::PageImage);
        QCOMPARE(pdf::PDFAppMcpDocumentResources::parseUri(prefix + QStringLiteral("pages/7/image/96")).page, QStringLiteral("7"));
        QVERIFY(!pdf::PDFAppMcpDocumentResources::parseUri(prefix + QStringLiteral("pages/7/image")).isValid());
        QVERIFY(!pdf::PDFAppMcpDocumentResources::parseUri(prefix + QStringLiteral("pages/a/b/text")).isValid());
    }

    static void completionDisclosure()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("completion.pdf"), 12));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();

        Result result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/prompt","name":"summarize-document"},"argument":{"name":"document","value":"comp"}})"));
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Complete, qPrintable(describe(result)));
        QCOMPARE(completionValues(result), QStringList({ tab->getDocument()->getTitle() }));
        result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/prompt","name":"summarize-document"},"argument":{"name":"pages","value":"1"},"context":{"arguments":{"document":"%1"}}})").arg(id));
        QVERIFY(completionValues(result).contains(QStringLiteral("10")));
        QVERIFY(!completionValues(result).contains(QStringLiteral("2")));
        result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}"},"argument":{"name":"dpi","value":"1"}})"));
        QCOMPARE(completionValues(result), QStringList({ QStringLiteral("150") }));
        result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/pages/{page}/text"},"argument":{"name":"page","value":"1"},"context":{"arguments":{"documentId":"%1"}}})").arg(id));
        QCOMPARE(completionValues(result), QStringList({ QStringLiteral("1"), QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12") }));
        QCOMPARE(result->toQJson().value(QStringLiteral("completion")).toObject().value(QStringLiteral("total")).toInt(), 4);
        result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/info"},"argument":{"name":"documentId","value":""}})"));
        QCOMPARE(completionValues(result), QStringList({ id }));
        result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/prompt","name":"review-and-comment"},"argument":{"name":"criteria","value":""}})"));
        QVERIFY(!completionValues(result).isEmpty());
        result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/prompt","name":"find-and-highlight"},"argument":{"name":"query","value":"x"}})"));
        QVERIFY(completionValues(result).isEmpty());

        // Invalid requests
        for (const char* invalid : { R"({})", R"({"ref":{"type":"ref/prompt","name":"nothing"},"argument":{"name":"x","value":""}})",
                                     R"({"ref":{"type":"ref/other"},"argument":{"name":"x","value":""}})", R"({"ref":{"type":"ref/prompt","name":"summarize-document"}})" })
        {
            result = complete(fixture, QString::fromLatin1(invalid));
            QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Error, invalid);
            QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        }

        // At most 100 values with total and hasMore
        QStringList many;
        for (int i = 0; i < 150; ++i)
        {
            many << QString::number(i);
        }
        const QJsonObject completion = pdf::PDFAppMcpCompletion::createResult(many, 150, true).toQJson().value(QStringLiteral("completion")).toObject();
        QCOMPARE(completion.value(QStringLiteral("values")).toArray().size(), 100);
        QVERIFY(completion.value(QStringLiteral("hasMore")).toBool());

        // 20 requests per second of an assistant
        pdf::PDFAppMcpCompletionLimiter* limiter = pdf::PDFAppMcpCompletionLimiter::get(fixture.host());
        limiter->setCurrentTime(1000000);
        int allowed = 0;
        for (int i = 0; i < 25; ++i)
        {
            result = complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}"},"argument":{"name":"dpi","value":""}})"));
            if (result->kind == pdf::PDFMcpCallResult::Kind::Complete)
            {
                ++allowed;
            }
            else
            {
                QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::RATE_LIMITED);
                QVERIFY(QJsonDocument::fromJson(result->errorData).object().value(QStringLiteral("retryAfterMs")).toInt() > 0);
            }
        }
        QVERIFY(allowed <= 20);
        limiter->setCurrentTime(1002000);
        QCOMPARE(complete(fixture, QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}"},"argument":{"name":"dpi","value":""}})"))->kind,
                 pdf::PDFMcpCallResult::Kind::Complete);
    }

    static void promptsGet()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));

        Result result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("nothing"), QJsonObject())));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("find-and-highlight"), QJsonObject())));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QVERIFY(result->errorMessage.contains(QStringLiteral("query")));
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), json(QStringLiteral(R"({"focus":5})")))));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"),
                                                                                 QJsonObject({ { QStringLiteral("focus"), QString(20000, QLatin1Char('x')) } }))));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);

        // No document: the prompt without a document id
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), QJsonObject())));
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Complete, qPrintable(describe(result)));
        QJsonArray messages = result->toQJson().value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        QCOMPARE(messages[0].toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        const QJsonObject resource = messages[1].toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("resource")).toObject();
        QCOMPARE(resource.value(QStringLiteral("uri")).toString(), QStringLiteral("pdf4qt://workflows/summarize-document.md"));
        QVERIFY(!resource.value(QStringLiteral("text")).toString().isEmpty());

        // One document: its id; a title or an id names it
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("prompt.pdf"), 2));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), QJsonObject())));
        QVERIFY(result->toQJson().value(QStringLiteral("messages")).toArray()[0].toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("text")).toString().contains(id));
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("find-and-highlight"),
                                                                                 QJsonObject({ { QStringLiteral("query"), QStringLiteral("alpha") }, { QStringLiteral("document"), tab->getDocument()->getTitle().toUpper() } }))));
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Complete, qPrintable(describe(result)));
        QVERIFY(QString::fromUtf8(result->result).contains(id));
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), QJsonObject({ { QStringLiteral("document"), QStringLiteral("unknown title") } }))));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);

        // Two documents of one title: the choice of the client (and the fallback)
        QDir().mkpath(fixture.folder() + QStringLiteral("/other"));
        pdf::PDFAppDocumentTab* twin = openReady(fixture, makeSample(fixture.folder() + QStringLiteral("/other"), QStringLiteral("prompt.pdf"), 1));
        QVERIFY(twin);
        QCOMPARE(twin->getDocument()->getTitle(), tab->getDocument()->getTitle());
        const QJsonObject byTitle({ { QStringLiteral("document"), tab->getDocument()->getTitle() } });
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), byTitle)));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Error);
        QVERIFY(result->errorMessage.contains(QStringLiteral("Pass documentId")));
        fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Modern, json(QStringLiteral(R"({"elicitation":{}})")));
        const pdf::PDFMcpCallRequest request = promptRequest(fixture, QStringLiteral("summarize-document"), byTitle);
        result = pdftest::AppMcpFixture::waitResult(start(fixture, request));
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        result = retry(fixture, request, *result, accept(pdf::PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"), twin->getDocument()->getId()));
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Complete, qPrintable(describe(result)));
        QVERIFY(QString::fromUtf8(result->result).contains(twin->getDocument()->getId()));

        // Read documents = Deny: the document is not named
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
        result = pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, QStringLiteral("summarize-document"), QJsonObject())));
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Complete, qPrintable(describe(result)));
        QVERIFY(!QString::fromUtf8(result->result).contains(id));
    }

    static void viewActions()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* outline = openReady(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
        pdf::PDFAppDocumentTab* links = openReady(fixture, pdftest::AppDocumentSamples::links(fixture.folder()));
        QVERIFY(outline && links);
        const QString outlineId = outline->getDocument()->getId();
        const QString linksId = links->getDocument()->getId();

        // activate needs its document; it switches the tab
        Result result = call(fixture, QStringLiteral("view"), QStringLiteral(R"({"action":"activate"})"));
        QVERIFY(isToolError(result));
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("activate"), outlineId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("activate")).value(QStringLiteral("activeDocumentId")).toString(), outlineId);
        QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(outline));

        // go_to: page, label, outline item, named destination; fit and zoom
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), outlineId, QStringLiteral(R"({"page":4})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("go_to")).value(QStringLiteral("page")).toInt(), 4);
        QTRY_COMPARE(outline->getView()->getCurrentPageIndex(), pdf::PDFInteger(3));
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), outlineId, QStringLiteral(R"({"outlineItem":"2.1","zoom":200})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("go_to")).value(QStringLiteral("page")).toInt(), 3);
        QTRY_VERIFY(qAbs(outline->getView()->getZoom() - 2.0) < 0.01);
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), outlineId, QStringLiteral(R"({"pageLabel":"5","fit":"page"})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), linksId, QStringLiteral(R"({"namedDestination":"chapter2","activate":false})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QCOMPARE(actionObject(result, QStringLiteral("go_to")).value(QStringLiteral("page")).toInt(), 4);
        QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(outline));
        for (const char* invalid : { R"({})", R"({"page":1,"pageLabel":"1"})", R"({"page":1,"fit":"page","zoom":100})", R"({"page":99})", R"({"outlineItem":"7"})",
                                     R"({"outlineItem":"0.1"})", R"({"namedDestination":"nowhere"})", R"({"pageLabel":"x"})" })
        {
            result = call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), outlineId, QString::fromLatin1(invalid)));
            QVERIFY2(isToolError(result), invalid);
        }

        // show_area: scrolled to and outlined for 3 s
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("show_area"), outlineId, QStringLiteral(R"({"page":2,"rect":{"left":10,"bottom":10,"right":100,"top":60}})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        pdf::PDFAppMcpAreaHighlight* highlight = pdf::PDFAppMcpAreaHighlight::find(outline->getView());
        QVERIFY(highlight);
        QCOMPARE(highlight->getPageIndex(), pdf::PDFInteger(1));
        QCOMPARE(highlight->getArea(), QRectF(10, 10, 90, 50));
        QVERIFY(highlight->getOpacity() > 0.0);
        QImage canvas(200, 200, QImage::Format_ARGB32);
        canvas.fill(Qt::white);
        {
            QPainter painter(&canvas);
            pdf::PDFTextLayoutGetter getter(nullptr, 1);
            QList<pdf::PDFRenderError> errors;
            highlight->drawPage(&painter, 1, nullptr, getter, QTransform(), pdf::PDFColorConvertor(), errors);
        }
        QVERIFY(canvas.pixel(10, 10) != qRgb(255, 255, 255) || canvas.pixel(100, 60) != qRgb(255, 255, 255));
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("show_area"), outlineId, QStringLiteral(R"({"page":3,"rect":{"left":0,"bottom":0,"right":5,"top":5}})")));
        QVERIFY(isSuccess(result));
        QCOMPARE(outline->getView()->findChildren<pdf::PDFAppMcpAreaHighlight*>().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!pdf::PDFAppMcpAreaHighlight::find(outline->getView()), 10000 * fixture.getTimeoutFactor());

        // show_search: the search panel of the tab with the query
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("show_search"), outlineId, QStringLiteral(R"({"query":"alpha","matchCase":true})")));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        pdf::PDFAppDocumentSearchCommandHandler* handler = pdf::PDFAppDocumentSearchCommandHandler::findHandler(outline);
        QVERIFY(handler && handler->getSearchController());
        QCOMPARE(handler->getSearchController()->getQuery(), QStringLiteral("alpha"));
        QVERIFY(handler->getSearchController()->getOptions().caseSensitive);
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("show_search"), outlineId, QStringLiteral(R"({"query":"a","regularExpression":true,"wildcards":true})")));
        QVERIFY(isToolError(result));

        // navigate: routes only
        result = call(fixture, QStringLiteral("view"), QStringLiteral(R"({"action":"navigate","navigate":{"route":"/help"}})"));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        for (const char* invalid : { "https://example.com", "relative", "//server/share", "file:///c:/a.pdf", "javascript:alert(1)", "" })
        {
            result = call(fixture, QStringLiteral("view"), QStringLiteral(R"({"action":"navigate","navigate":{"route":"%1"}})").arg(QString::fromLatin1(invalid)));
            QVERIFY2(isToolError(result), invalid);
        }

        // close: refused with unsaved changes, a running operation or a confirmation
        // of another assistant; else the tab is closed
        result = call(fixture, QStringLiteral("view"), QStringLiteral(R"({"action":"close"})"));
        QVERIFY(isToolError(result));
        links->getDocument()->setPendingEdits(true);
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("close"), linksId));
        QVERIFY(isToolError(result));
        QVERIFY(result->getFirstText().contains(QStringLiteral("unsaved")));
        links->getDocument()->setPendingEdits(false);

        QPointer<pdf::PDFAppDocumentOperation> operation = links->getDocument()->beginOperation(pdf::PDFAppDocumentOperationKind::Save, QStringLiteral("Saving"), true);
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("close"), linksId));
        QVERIFY(isToolError(result));
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedBusy));
        operation->finish();

        fixture.setClient(QStringLiteral("codex-mcp-client"));
        std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = std::make_shared<pdf::PDFAppMcpConfirmation>();
        confirmation->kind = pdf::PDFAppMcpConfirmationKind::Change;
        confirmation->permissions = pdf::PDFAppMcpPermissionKind::Edit;
        confirmation->call = pdf::PDFMcpCall::create(fixture.createRequest(QStringLiteral("annotations"), QJsonObject()));
        confirmation->document = links->getDocument();
        confirmation->actionId = QStringLiteral("annotations.add");
        fixture.host()->requestConfirmation(confirmation);
        fixture.setClient(QStringLiteral("claude-code"));
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("close"), linksId));
        QVERIFY(isToolError(result));
        QVERIFY(result->getFirstText().contains(QStringLiteral("Another assistant")));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));

        QPointer<pdf::PDFAppDocumentTab> linksGuard(links);
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("close"), linksId));
        QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        QVERIFY(actionObject(result, QStringLiteral("close")).value(QStringLiteral("closed")).toBool());
        QTRY_VERIFY(!linksGuard);
        result = call(fixture, QStringLiteral("view"), args(QStringLiteral("activate"), linksId));
        QVERIFY(isToolError(result));
        QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Ok));
    }

    static void fastPath()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();

        // The reads of the GUI thread stay below 100 ms on 2000 pages (printed)
        const std::vector<std::pair<QString, QString>> calls =
        {
            { QStringLiteral("application"), QStringLiteral(R"({"action":"get_state"})") },
            { QStringLiteral("document"), args(QStringLiteral("get_info"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_pages"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_outline"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_selection"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_layers"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_bookmarks"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_form_fields"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_attachments"), id) },
            { QStringLiteral("document"), args(QStringLiteral("get_annotations"), id, QStringLiteral(R"({"pages":"1-50"})")) },
            { QStringLiteral("view"), args(QStringLiteral("go_to"), id, QStringLiteral(R"({"page":1500})")) },
        };

        // The first call of each fills the caches of the document (attachments, labels)
        for (const auto& item : calls)
        {
            QVERIFY(isSuccess(call(fixture, item.first, item.second)));
        }

        for (const auto& item : calls)
        {
            QElapsedTimer timer;
            timer.start();
            std::shared_ptr<pdf::PDFMcpCall> started = fixture.startCall(item.first, json(item.second));
            const qint64 elapsed = timer.elapsed();
            QVERIFY2(started->isFinished(), qPrintable(item.second));
            QVERIFY2(isSuccess(started->getResult()), qPrintable(item.second));
            qInfo("%s: %lld ms", qPrintable(item.second.left(60)), elapsed);
            QVERIFY2(elapsed < 100 * fixture.getTimeoutFactor(), qPrintable(item.second));
        }
    }

    static void concurrentReads()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("concurrent.pdf"), 6));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();

        // 16 assistants: text, search, render and annotations at once
        std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
        for (int i = 0; i < 16; ++i)
        {
            fixture.setClient(QStringLiteral("assistant-%1").arg(i));
            calls.push_back(fixture.startCall(QStringLiteral("text"), json(args(QStringLiteral("get_text"), id, QStringLiteral(R"({"pages":"%1"})").arg(i % 6 + 1)))));
            calls.push_back(fixture.startCall(QStringLiteral("text"), json(args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"gamma %1"})").arg(i % 6 + 1)))));
            calls.push_back(fixture.startCall(QStringLiteral("render"), json(args(QStringLiteral("page"), id, QStringLiteral(R"({"page":%1,"dpi":36})").arg(i % 6 + 1)))));
            calls.push_back(fixture.startCall(QStringLiteral("document"), json(args(QStringLiteral("get_annotations"), id))));
        }
        for (const std::shared_ptr<pdf::PDFMcpCall>& started : calls)
        {
            const Result result = pdftest::AppMcpFixture::waitResult(started, 60000);
            QVERIFY2(isSuccess(result), qPrintable(describe(result)));
        }
        // A job ends in the GUI thread after its call has finished
        QTRY_COMPARE(fixture.host()->getRunningJobCount(), 0);
    }

    static void hostileInput()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("hostile.pdf"), 2));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        const QString huge(1024 * 1024, QLatin1Char('x'));

        // Wrong types, huge strings and numbers are validation errors of the schema
        const std::vector<std::pair<QString, QJsonObject>> invalid =
        {
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"search","search":{"query":"%1"}})").arg(huge)) },
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pages":5}})")) },
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pages":"1","maxCharacters":1e300}})")) },
            { QStringLiteral("render"), json(QStringLiteral(R"({"action":"page","page":{"page":1.5}})")) },
            { QStringLiteral("render"), json(QStringLiteral(R"({"action":"page","page":{"area":{"left":"a","bottom":0,"right":1,"top":1}}})")) },
            { QStringLiteral("view"), json(QStringLiteral(R"({"action":"go_to","go_to":{"zoom":-5}})")) },
            { QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_annotations","get_annotations":{"cursor":"%1"}})").arg(huge)) },
            { QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_info","documentId":"short"})")) },
            { QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_info","documentId":"%1"})").arg(QString(26, QLatin1Char('c')))) },
            { QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_info","documentId":"ABCDEFGHIJKLMNOPQRSTUVWXYZ"})")) },
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pages":"1-99999999999999999999999"}})")) },
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pages":",,,"}})")) },
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pages":"-5"}})")) },
            { QStringLiteral("text"), json(QStringLiteral(R"({"action":"get_text","get_text":{"pageLabel":"\u202E1"}})")) },
        };
        for (const auto& item : invalid)
        {
            QJsonObject arguments = item.second;
            if (!arguments.contains(QStringLiteral("documentId")))
            {
                arguments.insert(QStringLiteral("documentId"), id);
            }
            const Result result = fixture.callTool(item.first, arguments);
            QVERIFY2(isToolError(result), qPrintable(QJsonDocument(item.second).toJson(QJsonDocument::Compact).left(120)));
        }

        // Hostile but valid texts are searched as they are
        for (const QString& query : { QStringLiteral("\u202Ealpha"), QStringLiteral("a\u0000b"), QString(1024, QLatin1Char('(')), QStringLiteral("\U0001F600") })
        {
            QJsonObject search({ { QStringLiteral("query"), query } });
            const Result result = fixture.callTool(QStringLiteral("text"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("search") },
                                                                                         { QStringLiteral("documentId"), id }, { QStringLiteral("search"), search } }));
            QVERIFY(result);
        }
        const Result regex = call(fixture, QStringLiteral("text"), args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"(a+)+$","regularExpression":true})")));
        QVERIFY(regex);

        // Hostile documents: an outline cycle and hostile annotations do not hang or crash
        const QString hostileFile = makeSample(fixture.folder(), QStringLiteral("hostile-structure.pdf"), 2, [](pdf::PDFDocumentBuilder& builder, const std::vector<pdf::PDFObjectReference>& pages)
        {
            const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
            const pdf::PDFObjectReference item = builder.addObject(pdf::PDFObject());
            builder.setObject(root, dictionary({ { "Type", name("Outlines") }, { "First", pdf::PDFObject::createReference(item) }, { "Last", pdf::PDFObject::createReference(item) } }));
            builder.setObject(item, dictionary({ { "Title", pdf::PDFObjectFactory::createTextString(QStringLiteral("Loop\u202E")) }, { "Parent", pdf::PDFObject::createReference(root) },
                                                 { "Next", pdf::PDFObject::createReference(item) }, { "First", pdf::PDFObject::createReference(item) },
                                                 { "Dest", array({ pdf::PDFObject::createReference(pages[0]), name("XYZ"), number(1e308), number(-1e308), number(1e308) }) } }));
            builder.mergeTo(builder.getCatalogReference(), dictionary({ { "Outlines", pdf::PDFObject::createReference(root) },
                                                                        { "PageLabels", dictionary({ { "Nums", array({ integer(-5), dictionary({ { "S", name("Q") } }) }) } }) } }));
            const pdf::PDFObjectReference annotation = builder.addObject(dictionary({ { "Type", name("Annot") }, { "Subtype", name("Highlight") },
                                                                                       { "Rect", array({ number(1e308), number(-1e308), number(0), number(0) }) },
                                                                                       { "QuadPoints", array({ number(1), number(2), number(3) }) },
                                                                                       { "C", array({ number(5), number(-1), number(0.5) }) },
                                                                                       { "IRT", pdf::PDFObject::createReference(pdf::PDFObjectReference(99999, 0)) } }));
            builder.mergeTo(pages[0], dictionary({ { "Annots", array({ pdf::PDFObject::createReference(annotation), pdf::PDFObject::createReference(annotation), integer(5) }) } }));
        });
        pdf::PDFAppDocumentTab* hostileTab = openReady(fixture, hostileFile);
        QVERIFY(hostileTab);
        const QString hostileId = hostileTab->getDocument()->getId();
        for (const QString& action : { QStringLiteral("get_info"), QStringLiteral("get_pages"), QStringLiteral("get_outline"), QStringLiteral("get_annotations"), QStringLiteral("get_form_fields") })
        {
            const Result result = call(fixture, QStringLiteral("document"), args(action, hostileId));
            QVERIFY2(isSuccess(result), qPrintable(action + describe(result)));
            QVERIFY(!result->getFirstText().contains(QChar(0x202E)));
        }
        QVERIFY(isSuccess(call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), hostileId, QStringLiteral(R"({"outlineItem":"1"})")))) ||
                isToolError(call(fixture, QStringLiteral("view"), args(QStringLiteral("go_to"), hostileId, QStringLiteral(R"({"outlineItem":"1"})")))));
        QVERIFY(pdf::PDFAppMcpDocumentReader::formatColor({ 5.0, -1.0, 0.5 }).isEmpty());
        QCOMPARE(pdf::PDFAppMcpDocumentReader::formatColor({ 1.0, 0.0, 0.0 }), QStringLiteral("#ff0000"));
        QCOMPARE(pdf::PDFAppMcpDocumentReader::formatColor({ 0.0, 0.0, 0.0, 1.0 }), QStringLiteral("#000000"));
        QVERIFY(pdf::PDFAppMcpDocumentReader::formatColor({}).isEmpty());
        QCOMPARE(pdf::PDFAppMcpReadSupport::round(std::numeric_limits<double>::quiet_NaN()), 0.0);
        QCOMPARE(pdf::PDFAppMcpReadSupport::round(-0.0001), 0.0);
    }

    /// Answer of an elicitation of the client: the first option of the first property
    static QJsonObject chooseFirst(const QJsonObject& params)
    {
        const QJsonObject properties = params.value(QStringLiteral("requestedSchema")).toObject().value(QStringLiteral("properties")).toObject();
        if (properties.isEmpty())
        {
            return QJsonObject({ { QStringLiteral("action"), QStringLiteral("decline") } });
        }

        const QString property = properties.constBegin().key();
        const QJsonObject schema = properties.constBegin().value().toObject();
        QJsonValue value = schema.value(QStringLiteral("enum")).toArray().first();
        if (schema.contains(QStringLiteral("oneOf")))
        {
            value = schema.value(QStringLiteral("oneOf")).toArray().first().toObject().value(QStringLiteral("const"));
        }
        return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { property, value } }) } });
    }

    static QJsonObject httpResult(const QJsonObject& response)
    {
        return response.value(QStringLiteral("result")).toObject();
    }

    static QJsonObject httpAction(const QJsonObject& response, const QString& action)
    {
        return httpResult(response).value(QStringLiteral("structuredContent")).toObject().value(action).toObject();
    }

    static bool isHttpSuccess(const QJsonObject& response)
    {
        return response.contains(QStringLiteral("result")) && !httpResult(response).value(QStringLiteral("isError")).toBool();
    }

    // Review RW7-0 (items handed to package B) -----------------------------

    /// A completion with a fresh second of the rate limit (20 per second)
    static Result completeFresh(pdftest::AppMcpFixture& fixture, const QString& params)
    {
        pdf::PDFAppMcpCompletionLimiter* limiter = pdf::PDFAppMcpCompletionLimiter::get(fixture.host());
        limiter->setCurrentTime(limiter->getCurrentTime() + 2000);
        return complete(fixture, params);
    }

    /// Every URI of the document resources (one per template of the catalog)
    static QStringList documentResourceUris(const QString& documentId)
    {
        const QString prefix = pdf::PDFAppMcpDocumentResources::getDocumentPrefix(documentId);
        QStringList uris;
        for (const char* path : { "info", "pages", "outline", "text", "pages/1/text", "pages/1/image/72", "annotations", "pages/1/annotations", "form", "attachments/0" })
        {
            uris << prefix + QString::fromLatin1(path);
        }
        return uris;
    }

    /// Completions of the values derived from the documents: the document
    /// variables of every document template and the document arguments of every
    /// prompt (documentId / document / page / index / pages)
    static QStringList documentCompletions(pdftest::AppMcpFixture& fixture, const QString& documentId)
    {
        QStringList params;
        const QString context = QStringLiteral(R"("context":{"arguments":{"documentId":"%1"}})").arg(documentId);
        std::shared_ptr<const pdf::PDFMcpCatalog> catalog = fixture.host()->getCatalog();
        for (const pdf::PDFMcpResourceTemplateDefinition& definition : catalog->getTemplates())
        {
            const QString uriTemplate = definition.uriTemplate.getText();
            if (!uriTemplate.startsWith(QLatin1String("pdf4qt://documents/")))
            {
                continue;
            }
            for (const char* variable : { "documentId", "page", "index" })
            {
                if (uriTemplate.contains(QStringLiteral("{%1}").arg(QString::fromLatin1(variable))))
                {
                    params << QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"%1"},"argument":{"name":"%2","value":""},%3})")
                                  .arg(uriTemplate, QString::fromLatin1(variable), context);
                }
            }
        }
        for (const pdf::PDFMcpPromptDefinition& prompt : catalog->getPrompts())
        {
            for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
            {
                if (argument.name == QLatin1String("document") || argument.name == QLatin1String("pages"))
                {
                    params << QStringLiteral(R"({"ref":{"type":"ref/prompt","name":"%1"},"argument":{"name":"%2","value":""},%3})")
                                  .arg(prompt.name, argument.name, context);
                }
            }
        }
        return params;
    }

    /// Prompts with a document argument, with their arguments naming \p title
    static std::vector<std::pair<QString, QJsonObject>> documentPrompts(pdftest::AppMcpFixture& fixture, const QString& title)
    {
        std::vector<std::pair<QString, QJsonObject>> prompts;
        for (const pdf::PDFMcpPromptDefinition& prompt : fixture.host()->getCatalog()->getPrompts())
        {
            bool hasDocument = false;
            QJsonObject arguments;
            for (const pdf::PDFMcpPromptArgument& argument : prompt.arguments)
            {
                if (argument.name == QLatin1String("document"))
                {
                    hasDocument = true;
                    arguments.insert(argument.name, title);
                }
                else if (argument.required)
                {
                    arguments.insert(argument.name, QStringLiteral("1"));
                }
            }
            if (hasDocument)
            {
                prompts.emplace_back(prompt.name, arguments);
            }
        }
        return prompts;
    }

    static Result getPrompt(pdftest::AppMcpFixture& fixture, const QString& name, const QJsonObject& arguments)
    {
        return pdftest::AppMcpFixture::waitResult(start(fixture, promptRequest(fixture, name, arguments)));
    }

    static void readGateEverySurface()
    {
        // Review RW7-0: resources/read, prompts/get and completion bypass the
        // gate of the tools in the host - each of them applies Read = Deny / Ask
        // (one approval per assistant, shared with the tools) / Allow itself
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::attachments(fixture.folder()));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        const QString title = tab->getDocument()->getTitle();
        const QString unknownUri = pdf::PDFAppMcpDocumentResources::getDocumentPrefix(QString(26, QLatin1Char('a'))) + QStringLiteral("info");

        // Every document template of the catalog is covered by a tested URI
        const QStringList uris = documentResourceUris(id);
        std::shared_ptr<const pdf::PDFMcpCatalog> catalog = fixture.host()->getCatalog();
        int documentTemplates = 0;
        for (const pdf::PDFMcpResourceTemplateDefinition& definition : catalog->getTemplates())
        {
            if (!definition.uriTemplate.getText().startsWith(QLatin1String("pdf4qt://documents/")))
            {
                continue;
            }
            ++documentTemplates;
            bool isCovered = false;
            for (const QString& uri : uris)
            {
                isCovered = isCovered || definition.uriTemplate.match(uri).has_value();
            }
            QVERIFY2(isCovered, qPrintable(definition.uriTemplate.getText()));
        }
        QCOMPARE(documentTemplates, 10);
        const std::vector<std::pair<QString, QJsonObject>> prompts = documentPrompts(fixture, title);
        QVERIFY(!prompts.empty());
        const QStringList completions = documentCompletions(fixture, id);
        QVERIFY(completions.size() >= 4);

        // Allow: every surface gives the document without a question
        for (const QString& uri : uris)
        {
            const Result result = readResource(fixture, uri);
            QVERIFY2(isSuccess(result), qPrintable(uri + QStringLiteral(": ") + describe(result)));
        }
        for (const auto& prompt : prompts)
        {
            const Result result = getPrompt(fixture, prompt.first, prompt.second);
            QVERIFY2(isSuccess(result) && QString::fromUtf8(result->result).contains(id), qPrintable(prompt.first + QStringLiteral(": ") + describe(result)));
        }
        int nonEmpty = 0;
        for (const QString& params : completions)
        {
            const Result result = completeFresh(fixture, params);
            QVERIFY2(isSuccess(result), qPrintable(params));
            nonEmpty += completionValues(result).isEmpty() ? 0 : 1;
        }
        QCOMPARE(nonEmpty, completions.size());
        QVERIFY(fixture.confirmations()->shown.empty());

        // Deny: every resource refused - an open document answers as an unknown
        // id (nothing disclosed) and the refusal is audited; the prompts do not
        // name the document; the completions are empty
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
        const Result unknown = readResource(fixture, unknownUri);
        QVERIFY(unknown && unknown->kind == pdf::PDFMcpCallResult::Kind::Error);
        for (const QString& uri : uris)
        {
            const Result result = readResource(fixture, uri);
            QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Error, qPrintable(uri));
            QCOMPARE(result->errorCode, unknown->errorCode);
            QCOMPARE(result->errorMessage, unknown->errorMessage);
            QVERIFY(result->errorMessage.contains(QStringLiteral("Deny")));
        }
        for (const auto& prompt : prompts)
        {
            const Result result = getPrompt(fixture, prompt.first, prompt.second);
            QVERIFY2(isSuccess(result), qPrintable(prompt.first + QStringLiteral(": ") + describe(result)));
            QVERIFY2(!QString::fromUtf8(result->result).contains(id), qPrintable(prompt.first));
        }
        for (const QString& params : completions)
        {
            QVERIFY2(completionValues(completeFresh(fixture, params)).isEmpty(), qPrintable(params));
        }
        QVERIFY(fixture.confirmations()->shown.empty());
        int deniedRows = 0;
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            deniedRows += row.status == pdf::PDFMcpActivityStatus::DeniedPermission ? 1 : 0;
        }
        QVERIFY(deniedRows >= 1);

        // Ask: nothing before the approval (completion cannot ask)
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
        for (const QString& params : completions)
        {
            QVERIFY2(completionValues(completeFresh(fixture, params)).isEmpty(), qPrintable(params));
        }

        // The first resource asks once; then every surface of this assistant
        // (resources, prompts, completions, tools) goes without a question
        std::shared_ptr<pdf::PDFMcpCall> first = start(fixture, resourceRequest(fixture, uris.front()));
        QVERIFY(fixture.confirmDialog());
        QCOMPARE(fixture.confirmations()->shown.size(), size_t(1));
        QCOMPARE(fixture.confirmations()->shown.back()->actionId, QStringLiteral("document.get_info"));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::Allowed));
        QVERIFY2(isSuccess(pdftest::AppMcpFixture::waitResult(first)), "first read");
        for (const QString& uri : uris)
        {
            QVERIFY2(isSuccess(readResource(fixture, uri)), qPrintable(uri));
        }
        for (const auto& prompt : prompts)
        {
            const Result result = getPrompt(fixture, prompt.first, prompt.second);
            QVERIFY2(isSuccess(result) && QString::fromUtf8(result->result).contains(id), qPrintable(prompt.first));
        }
        for (const QString& params : completions)
        {
            QVERIFY2(!completionValues(completeFresh(fixture, params)).isEmpty(), qPrintable(params));
        }
        QVERIFY(isSuccess(call(fixture, QStringLiteral("document"), args(QStringLiteral("get_info"), id))));
        QCOMPARE(fixture.confirmations()->shown.size(), size_t(1));

        // Another assistant: a prompt asks (its own approval), refused = the
        // prompt without the document; its completions stay empty; a resource
        // asks again with the action of its template
        fixture.setClient(QStringLiteral("codex-mcp-client"));
        std::shared_ptr<pdf::PDFMcpCall> prompt = start(fixture, promptRequest(fixture, prompts.front().first, prompts.front().second));
        QVERIFY(fixture.confirmDialog());
        QCOMPARE(fixture.confirmations()->shown.size(), size_t(2));
        QVERIFY(fixture.answer(pdf::PDFAppMcpDecision::DeniedUser));
        Result result = pdftest::AppMcpFixture::waitResult(prompt);
        QVERIFY(isSuccess(result) && !QString::fromUtf8(result->result).contains(id));
        for (const QString& params : completions)
        {
            QVERIFY2(completionValues(completeFresh(fixture, params)).isEmpty(), qPrintable(params));
        }
        std::shared_ptr<pdf::PDFMcpCall> image = start(fixture, resourceRequest(fixture, uris.at(5)));
        QVERIFY(fixture.confirmDialog());
        QCOMPARE(fixture.confirmations()->shown.back()->actionId, QStringLiteral("render.page"));

        // Deny set while the question waits: the approval does not read
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));
        fixture.answer(pdf::PDFAppMcpDecision::Allowed);
        result = pdftest::AppMcpFixture::waitResult(image);
        QVERIFY2(result && result->kind == pdf::PDFMcpCallResult::Kind::Error, qPrintable(describe(result)));
        QVERIFY(blocks(*result, QStringLiteral("image")).isEmpty());
    }

    /// A job of the test: counts its runs, finishes or throws
    class TestJob : public pdf::PDFAppMcpJob
    {
    public:
        enum class Mode
        {
            Finish,
            ThrowPdf,
            ThrowStandard,
            ThrowOther,
            ThrowBadAlloc
        };

        TestJob(std::shared_ptr<pdf::PDFMcpCall> call, Mode mode, std::shared_ptr<std::atomic<int>> runs) :
            pdf::PDFAppMcpJob(std::move(call), pdf::PDFAppDocumentSnapshot()),
            m_mode(mode),
            m_runs(std::move(runs))
        {

        }

        virtual void execute() override
        {
            ++(*m_runs);
            switch (m_mode)
            {
                case Mode::Finish:
                    getCall()->finish(pdf::PDFMcpCallResult::createToolError(QStringLiteral("finished by the job")));
                    break;
                case Mode::ThrowPdf:
                    throw pdf::PDFException(QStringLiteral("Broken object ") + QChar(0x202E) + QStringLiteral("evil") + QChar(0x0007) + QString(5000, QLatin1Char('x')));
                case Mode::ThrowStandard:
                    throw std::runtime_error("secret text of the document");
                case Mode::ThrowOther:
                    throw 42;
                case Mode::ThrowBadAlloc:
                    throw std::bad_alloc();
            }
        }

    private:
        Mode m_mode;
        std::shared_ptr<std::atomic<int>> m_runs;
    };

    static Result runJob(pdftest::AppMcpFixture& fixture, pdf::PDFMcpCallKind kind, TestJob::Mode mode, bool isCanceledBefore, int* runs)
    {
        pdf::PDFMcpCallRequest request = fixture.createRequest(QStringLiteral("document"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("get_info") } }));
        request.kind = kind;
        std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(request);
        if (isCanceledBefore)
        {
            call->cancel(pdf::PDFMcpCancelReason::StreamClosed);
        }

        std::shared_ptr<std::atomic<int>> counter = std::make_shared<std::atomic<int>>(0);
        fixture.host()->startJob(std::make_shared<TestJob>(call, mode, counter));
        const Result result = pdftest::AppMcpFixture::waitResult(call, 10000);
        QElapsedTimer timer;
        timer.start();
        while (fixture.host()->getRunningJobCount() > 0 && timer.elapsed() < 10000 * fixture.getTimeoutFactor())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        }
        *runs = fixture.host()->getRunningJobCount() == 0 ? counter->load() : -1;
        return result;
    }

    static void jobCancelAndExceptions()
    {
        // Review RW7-0: a job canceled before it ran never finished its call (the
        // dispatcher waited its grace time); run() had no catch (...)
        pdftest::AppMcpFixture fixture;
        int runs = -1;

        // Canceled before the run: never executed, finished by the host (the job
        // left the call open) with its activity row canceled
        Result result = runJob(fixture, pdf::PDFMcpCallKind::ToolCall, TestJob::Mode::Finish, true, &runs);
        QVERIFY2(isToolError(result), qPrintable(describe(result)));
        QCOMPARE(result->getFirstText(), QStringLiteral("The request was canceled."));
        QCOMPARE(runs, 0);
        bool hasCanceledRow = false;
        for (const pdf::PDFMcpActivityRow& row : fixture.activity())
        {
            hasCanceledRow = hasCanceledRow || (row.status == pdf::PDFMcpActivityStatus::Canceled && row.action == QLatin1String("document.get_info"));
        }
        QVERIFY(hasCanceledRow);
        result = runJob(fixture, pdf::PDFMcpCallKind::ResourceRead, TestJob::Mode::Finish, true, &runs);
        QVERIFY(result && result->kind == pdf::PDFMcpCallResult::Kind::Error);
        QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INTERNAL_ERROR);
        QCOMPARE(runs, 0);

        // A normal run keeps the result of the job
        result = runJob(fixture, pdf::PDFMcpCallKind::ToolCall, TestJob::Mode::Finish, false, &runs);
        QCOMPARE(result->getFirstText(), QStringLiteral("finished by the job"));
        QCOMPARE(runs, 1);

        // A PDFException: one sanitized, bounded line
        result = runJob(fixture, pdf::PDFMcpCallKind::ToolCall, TestJob::Mode::ThrowPdf, false, &runs);
        QVERIFY2(isToolError(result), qPrintable(describe(result)));
        const QString text = result->getFirstText();
        QVERIFY(text.startsWith(QStringLiteral("PDF4QT could not process the document: Broken object")));
        QVERIFY(!text.contains(QChar(0x202E)));
        QVERIFY(!text.contains(QChar(0x0007)));
        QVERIFY(text.size() < pdf::PDFAppMcpJobRunnable::MAXIMUM_MESSAGE_LENGTH + 64);

        // Other exceptions: a generic text, never their own
        for (TestJob::Mode mode : { TestJob::Mode::ThrowStandard, TestJob::Mode::ThrowOther })
        {
            result = runJob(fixture, pdf::PDFMcpCallKind::ToolCall, mode, false, &runs);
            QVERIFY2(isToolError(result), qPrintable(describe(result)));
            QCOMPARE(result->getFirstText(), QStringLiteral("PDF4QT could not process the request because of an internal error."));
            QVERIFY(!result->getFirstText().contains(QStringLiteral("secret")));
        }
        result = runJob(fixture, pdf::PDFMcpCallKind::ResourceRead, TestJob::Mode::ThrowOther, false, &runs);
        QVERIFY(result && result->kind == pdf::PDFMcpCallResult::Kind::Error);
        QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INTERNAL_ERROR);
        result = runJob(fixture, pdf::PDFMcpCallKind::ToolCall, TestJob::Mode::ThrowBadAlloc, false, &runs);
        QVERIFY(result->getFirstText().contains(QStringLiteral("out of memory")));
    }

    static int countStatus(const std::vector<pdf::PDFMcpActivityRow>& rows, pdf::PDFMcpActivityStatus status)
    {
        int count = 0;
        for (const pdf::PDFMcpActivityRow& row : rows)
        {
            count += row.status == status ? 1 : 0;
        }
        return count;
    }

    static void denialAudit()
    {
        // Review RW7-0: a call refused by a Deny permission wrote no activity row;
        // now one row denied_permission, repeated denials coalesced within 60 s
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("denied.pdf"), 1));
        QVERIFY(tab);
        const QString id = tab->getDocument()->getId();
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny));

        // A looping client: the tool text is not offered with Read = Deny (all its
        // actions read); 50 calls answered as an unknown tool = one audited row
        for (int i = 0; i < 50; ++i)
        {
            const Result result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), id, QStringLiteral(R"({"pages":"1"})")));
            QVERIFY(result && result->kind == pdf::PDFMcpCallResult::Kind::Error);
            QCOMPARE(result->errorCode, pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        }
        std::vector<pdf::PDFMcpActivityRow> rows = fixture.activity();
        QCOMPARE(countStatus(rows, pdf::PDFMcpActivityStatus::DeniedPermission), 1);

        // An unknown tool or action is no denial (nothing recorded)
        QVERIFY(!isSuccess(call(fixture, QStringLiteral("nothing"), QStringLiteral(R"({"action":"x"})"))));
        QVERIFY(!isSuccess(call(fixture, QStringLiteral("text"), QStringLiteral(R"({"action":"nothing"})"))));
        QCOMPARE(countStatus(fixture.activity(), pdf::PDFMcpActivityStatus::DeniedPermission), 1);

        // The resources: 30 refused reads of one URI = one row, a resource of
        // another action is its own row (no document named - nothing disclosed)
        const QString prefix = pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id);
        for (int i = 0; i < 30; ++i)
        {
            QVERIFY(!isSuccess(readResource(fixture, prefix + QStringLiteral("info"))));
        }
        QVERIFY(!isSuccess(readResource(fixture, prefix + QStringLiteral("pages/1/image/72"))));
        rows = fixture.activity();
        QCOMPARE(countStatus(rows, pdf::PDFMcpActivityStatus::DeniedPermission), 3);
        for (const pdf::PDFMcpActivityRow& row : rows)
        {
            if (row.status == pdf::PDFMcpActivityStatus::DeniedPermission && row.action == QLatin1String("document.get_info"))
            {
                QCOMPARE(row.detail.value(QStringLiteral("attempts")).toInt(), 30);
                QVERIFY(row.document.isEmpty());
                QCOMPARE(pdf::PDFAppMcpActivity::getStatusText(row.status, row.client, row.detail), QStringLiteral("Denied · the permission is set to Deny"));
            }
        }

        // Another assistant is its own row
        fixture.setClient(QStringLiteral("cursor"));
        QVERIFY(!isSuccess(readResource(fixture, prefix + QStringLiteral("info"))));
        QCOMPARE(countStatus(fixture.activity(), pdf::PDFMcpActivityStatus::DeniedPermission), 4);

        // The gate of the host: a catalog older than the permission still offers
        // the action - the call is an execution error, coalesced into the row of
        // the same assistant / action / document
        fixture.setClient(QStringLiteral("claude-code"));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.host()->getCatalog()->findTool(QStringLiteral("text")));
        pdf::PDFAppMcpPermissions permissions = fixture.host()->getPolicy().getPermissions();
        permissions.set(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Deny);
        fixture.host()->getPolicy().setPermissions(permissions);
        for (int i = 0; i < 5; ++i)
        {
            const Result result = call(fixture, QStringLiteral("text"), args(QStringLiteral("get_text"), id, QStringLiteral(R"({"pages":"1"})")));
            QVERIFY2(isToolError(result), qPrintable(describe(result)));
            QVERIFY(result->getFirstText().contains(QStringLiteral("Deny")));
        }
        rows = fixture.activity();
        QCOMPARE(countStatus(rows, pdf::PDFMcpActivityStatus::DeniedPermission), 4);
        bool hasToolRow = false;
        for (const pdf::PDFMcpActivityRow& row : rows)
        {
            if (row.status == pdf::PDFMcpActivityStatus::DeniedPermission && row.action == QLatin1String("text.get_text"))
            {
                hasToolRow = true;
                QCOMPARE(row.document, tab->getDocument()->getTitle());
                QCOMPARE(row.detail.value(QStringLiteral("attempts")).toInt(), 55);
            }
        }
        QVERIFY(hasToolRow);

        // The window of the coalescing: a denial after 60 s is a new row
        pdf::PDFAppMcpActivityLog log(fixture.context->getDatabase());
        log.setCurrentTime(5000000);
        pdf::PDFAppMcpActivityEntry entry;
        entry.assistantKey = QStringLiteral("a:1");
        entry.client = QStringLiteral("Claude Code");
        entry.action = QStringLiteral("annotations.add");
        entry.documentId = QStringLiteral("doc");
        entry.status = pdf::PDFMcpActivityStatus::DeniedPermission;
        const qint64 firstId = log.record(entry);
        log.setCurrentTime(5000000 + pdf::PDFAppMcpActivityLog::COALESCE_TIME - 1);
        QCOMPARE(log.record(entry), firstId);
        log.flush();
        log.setCurrentTime(5000000 + 3 * pdf::PDFAppMcpActivityLog::COALESCE_TIME);
        QVERIFY(log.record(entry) != firstId);
    }

    static void activityRefusedWrite()
    {
        // Review RW7-0: the rows of a refused batch were dropped, waiting rows
        // included, so their later decisions were lost
        pdftest::AppMcpFixture fixture;
        pdf::PDFDatabase* database = fixture.context->getDatabase();
        pdf::PDFAppMcpActivityLog log(database);
        log.setCurrentTime(3000000);

        pdf::PDFAppMcpActivityEntry entry;
        entry.assistantKey = QStringLiteral("a:1");
        entry.client = QStringLiteral("Claude Code");
        entry.document = QStringLiteral("Report.pdf");
        entry.action = QStringLiteral("annotations.add");
        entry.documentId = QStringLiteral("doc");
        entry.status = pdf::PDFMcpActivityStatus::Waiting;

        QVERIFY(pdf::PDFDatabaseStatement(database->getConnection(), "CREATE TEMP TRIGGER review_refuse_activity BEFORE INSERT ON main.mcp_activity "
                                                                     "BEGIN SELECT RAISE(ABORT, 'injected fault'); END").execute());
        const qint64 waiting = log.record(entry);
        log.flush();
        QCOMPARE(log.getPendingCount(), 1);
        QVERIFY(pdf::PDFMcpRepository(database).getActivity(10).empty());

        // The decision of the unwritten waiting row is kept
        log.update(waiting, pdf::PDFMcpActivityStatus::Allowed, QJsonObject({ { QStringLiteral("count"), 2 } }));
        log.flush();
        QCOMPARE(log.getPendingCount(), 1);

        // Many refused reads: at most MAXIMUM_UNWRITTEN_ROWS, the oldest reads
        // dropped first, the waiting row of another request never
        entry.status = pdf::PDFMcpActivityStatus::Waiting;
        entry.action = QStringLiteral("annotations.delete");
        const qint64 secondWaiting = log.record(entry);
        entry.status = pdf::PDFMcpActivityStatus::Ok;
        entry.action = QStringLiteral("text.get_text");
        const int reads = int(pdf::PDFAppMcpActivityLog::MAXIMUM_UNWRITTEN_ROWS) + 200;
        for (int i = 0; i < reads; ++i)
        {
            log.setCurrentTime(3000000 + 1 + i);
            entry.documentId = QStringLiteral("doc-%1").arg(i);
            entry.pages = { i + 1 };
            log.record(entry);
        }
        log.flush();
        QCOMPARE(size_t(log.getPendingCount()), pdf::PDFAppMcpActivityLog::MAXIMUM_UNWRITTEN_ROWS);

        // The database accepts again: the kept rows are written with their decisions
        QVERIFY(pdf::PDFDatabaseStatement(database->getConnection(), "DROP TRIGGER temp.review_refuse_activity").execute());
        log.update(secondWaiting, pdf::PDFMcpActivityStatus::DeniedUser);
        log.flush();
        QCOMPARE(log.getPendingCount(), 0);
        const std::vector<pdf::PDFMcpActivityRow> rows = pdf::PDFMcpRepository(database).getActivity(5000);
        QCOMPARE(rows.size(), pdf::PDFAppMcpActivityLog::MAXIMUM_UNWRITTEN_ROWS);
        int allowed = 0;
        int deniedUser = 0;
        std::set<int> pages;
        for (const pdf::PDFMcpActivityRow& row : rows)
        {
            allowed += (row.status == pdf::PDFMcpActivityStatus::Allowed && row.action == QLatin1String("annotations.add")) ? 1 : 0;
            deniedUser += (row.status == pdf::PDFMcpActivityStatus::DeniedUser && row.action == QLatin1String("annotations.delete")) ? 1 : 0;
            if (row.action == QLatin1String("text.get_text"))
            {
                const std::set<int> rowPages = pdf::PDFAppMcpActivity::parsePages(row.detail.value(QStringLiteral("pages")).toString());
                pages.insert(rowPages.cbegin(), rowPages.cend());
            }
        }
        QCOMPARE(allowed, 1);
        QCOMPARE(deniedUser, 1);

        // The newest reads were kept (the oldest dropped)
        QCOMPARE(int(pages.size()), int(pdf::PDFAppMcpActivityLog::MAXIMUM_UNWRITTEN_ROWS) - 2);
        QVERIFY(pages.count(reads));
        QVERIFY(!pages.count(1));
    }

    /// The methods of the messages of a listen stream
    static QStringList streamMethods(const pdftest::McpTestStream& stream)
    {
        QStringList methods;
        for (const QJsonObject& message : stream.messages())
        {
            const QString method = message.value(QStringLiteral("method")).toString();
            if (method == QLatin1String("notifications/resources/updated"))
            {
                methods << method + QStringLiteral(" ") + message.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString();
            }
            else
            {
                methods << method;
            }
        }
        return methods;
    }

    /// Waits until a message of the stream contains \p text (in its method / uri)
    static bool waitForStream(pdftest::McpTestStream& stream, const QString& text)
    {
        QElapsedTimer timer;
        timer.start();
        const qint64 limit = 10000 * pdftest::AppDocumentFixture::getTimeoutFactor();
        while (timer.elapsed() < limit)
        {
            if (streamMethods(stream).filter(text).size() > 0)
            {
                return true;
            }
            stream.waitForMessages(int(stream.messages().size()) + 1, 100);
            pdftest::AppDocumentFixture::settle();
        }
        return false;
    }

    static qsizetype countListChanged(const pdftest::McpTestStream& stream)
    {
        return streamMethods(stream).filter(QStringLiteral("notifications/resources/list_changed")).size();
    }

    /// Waits until the watcher has no coalesced change pending and every catalog it
    /// has rebuilt since \p baseRebuilds (taken, when it had nothing pending, before
    /// the stream was opened) reached the stream as one list_changed. A change of
    /// the documents within the 50 ms of the coalescing, which gives the same list
    /// again, is no list_changed at all, so the test compares the rebuilds with the
    /// notifications - never a count it expects from its own steps.
    static bool waitListSettled(pdftest::McpTestStream& stream, pdf::PDFAppMcpHost* host, int baseRebuilds)
    {
        QElapsedTimer timer;
        timer.start();
        const qint64 limit = 10000 * pdftest::AppDocumentFixture::getTimeoutFactor();
        while (timer.elapsed() < limit)
        {
            pdftest::AppDocumentFixture::settle();
            pdf::PDFAppMcpDocumentWatcher* watcher = pdf::PDFAppMcpDocumentWatcher::find(host);
            if (watcher && !watcher->isRebuildPending() && countListChanged(stream) == watcher->getRebuildCount() - baseRebuilds)
            {
                return true;
            }
            stream.waitForMessages(int(stream.messages().size()) + 1, 50);
        }
        return false;
    }

    /// The listed resources of the document are in resources/list
    static bool isListed(pdftest::McpTestClient& client, const QString& documentId)
    {
        const QJsonObject listed = client.request(QStringLiteral("resources/list"));
        const QJsonArray resources = listed.value(QStringLiteral("result")).toObject().value(QStringLiteral("resources")).toArray();
        for (const QJsonValue& resource : resources)
        {
            if (resource.toObject().value(QStringLiteral("uri")).toString().startsWith(QStringLiteral("pdf4qt://documents/%1/").arg(documentId)))
            {
                return true;
            }
        }
        return false;
    }

    static void resourceSubscriptionsHttp()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());

        pdf::PDFAppDocumentTab* first = openReady(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
        QVERIFY(first);
        const QString firstId = first->getDocument()->getId();
        const QString infoUri = QStringLiteral("pdf4qt://documents/%1/info").arg(firstId);
        const QString stateUri = QStringLiteral("pdf4qt://application/state");

        // The listing of the resources (the watcher exists since the first catalog);
        // the rebuilds are counted from a moment without a pending change, before
        // the stream is opened, so every later rebuild is one list_changed of it
        std::unique_ptr<pdftest::McpTestClient> client = fixture.client(pdftest::McpTestEra::Modern);
        const QJsonObject listed = client->request(QStringLiteral("resources/list"));
        QVERIFY2(listed.contains(QStringLiteral("result")), QJsonDocument(listed).toJson(QJsonDocument::Compact).left(500).constData());
        pdf::PDFAppMcpDocumentWatcher* watcher = pdf::PDFAppMcpDocumentWatcher::find(fixture.host());
        QVERIFY(watcher);
        QTRY_VERIFY_WITH_TIMEOUT(!watcher->isRebuildPending(), 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        const int baseRebuilds = watcher->getRebuildCount();

        QJsonObject message = client->createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({
            { QStringLiteral("resourcesListChanged"), true },
            { QStringLiteral("resourceSubscriptions"), QJsonArray({ infoUri, stateUri }) } }) } }));
        message.insert(QStringLiteral("id"), QStringLiteral("listen-b"));
        std::unique_ptr<pdftest::McpTestStream> stream = client->openStream(message);
        QVERIFY(stream);
        QVERIFY(stream->waitForMessages(1, 5000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
        QCOMPARE(stream->messages().front().value(QStringLiteral("method")).toString(), QStringLiteral("notifications/subscriptions/acknowledged"));

        // A change of the content updates the resources of the document and the
        // state (the new title of the document is also a new list)
        pdf::PDFAppDocument* document = first->getDocument();
        pdf::PDFDocumentBuilder builder(document->getDocument().data());
        builder.setDocumentTitle(QStringLiteral("Subscribed"));
        pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
        QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("Title"), document->getVersion(), pdf::PDFAppChangeSource::User));
        QVERIFY2(waitForStream(*stream, infoUri), qPrintable(streamMethods(*stream).join(QStringLiteral(", "))));
        QVERIFY2(waitListSettled(*stream, fixture.host(), baseRebuilds), qPrintable(streamMethods(*stream).join(QStringLiteral(", "))));

        // A new document changes the list and the state. Its resources are listed
        // and its rebuild reached the stream before the close - a close within the
        // 50 ms of the coalescing of the opening would give the same list as before
        // and no list_changed at all (the cause of a failure seen once by RW7-0)
        const int openRebuilds = watcher->getRebuildCount();
        const qsizetype openListChanged = countListChanged(*stream);
        const qsizetype openStateUpdates = streamMethods(*stream).filter(stateUri).size();
        pdf::PDFAppDocumentTab* second = openReady(fixture, pdftest::AppDocumentSamples::attachments(fixture.folder()));
        QVERIFY(second);
        const QString secondId = second->getDocument()->getId();
        QTRY_VERIFY_WITH_TIMEOUT(isListed(*client, secondId), 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        QVERIFY2(waitListSettled(*stream, fixture.host(), baseRebuilds), qPrintable(streamMethods(*stream).join(QStringLiteral(", "))));
        QVERIFY(watcher->getRebuildCount() > openRebuilds);
        QVERIFY(countListChanged(*stream) > openListChanged);
        QTRY_VERIFY_WITH_TIMEOUT(streamMethods(*stream).filter(stateUri).size() > openStateUpdates, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());

        // Closing of a document changes the list again
        const int closeRebuilds = watcher->getRebuildCount();
        const qsizetype closeListChanged = countListChanged(*stream);
        QVERIFY(fixture.window->closeTab(second));
        pdftest::AppDocumentFixture::settle();
        QTRY_VERIFY_WITH_TIMEOUT(!isListed(*client, secondId), 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        QVERIFY2(waitListSettled(*stream, fixture.host(), baseRebuilds), qPrintable(streamMethods(*stream).join(QStringLiteral(", "))));
        QVERIFY(watcher->getRebuildCount() > closeRebuilds);
        QVERIFY(countListChanged(*stream) > closeListChanged);

        // The subscription id is on every notification
        for (const QJsonObject& item : stream->messages())
        {
            QCOMPARE(item.value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")).toString(), QStringLiteral("listen-b"));
        }
    }

    static void httpEndToEnd()
    {
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());

        // Every action of the read tools over HTTP (modern era)
        pdf::PDFAppDocumentTab* outline = openReady(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
        pdf::PDFAppDocumentTab* attachments = openReady(fixture, pdftest::AppDocumentSamples::attachments(fixture.folder()));
        QVERIFY(outline && attachments);
        const QString outlineId = outline->getDocument()->getId();
        const QString attachmentsId = attachments->getDocument()->getId();

        std::unique_ptr<pdftest::McpTestClient> client = fixture.client(pdftest::McpTestEra::Modern);
        QJsonObject response = client->callTool(QStringLiteral("application"), json(QStringLiteral(R"({"action":"get_state"})")));
        QVERIFY2(isHttpSuccess(response), QJsonDocument(response).toJson(QJsonDocument::Compact).left(500).constData());
        QCOMPARE(httpAction(response, QStringLiteral("get_state")).value(QStringLiteral("documents")).toArray().size(), 2);

        const std::vector<std::pair<QString, QString>> calls =
        {
            { QStringLiteral("document"), args(QStringLiteral("get_info"), outlineId) },
            { QStringLiteral("document"), args(QStringLiteral("get_pages"), outlineId, QStringLiteral(R"({"pages":"1-2"})")) },
            { QStringLiteral("document"), args(QStringLiteral("get_outline"), outlineId) },
            { QStringLiteral("document"), args(QStringLiteral("get_annotations"), attachmentsId) },
            { QStringLiteral("document"), args(QStringLiteral("get_form_fields"), outlineId) },
            { QStringLiteral("document"), args(QStringLiteral("get_attachments"), attachmentsId) },
            { QStringLiteral("document"), args(QStringLiteral("read_attachment"), attachmentsId, QStringLiteral(R"({"index":0})")) },
            { QStringLiteral("document"), args(QStringLiteral("get_signatures"), outlineId) },
            { QStringLiteral("document"), args(QStringLiteral("get_selection"), outlineId) },
            { QStringLiteral("document"), args(QStringLiteral("get_layers"), outlineId) },
            { QStringLiteral("document"), args(QStringLiteral("get_bookmarks"), outlineId) },
            { QStringLiteral("text"), args(QStringLiteral("get_text"), outlineId, QStringLiteral(R"({"pages":"1","geometry":"words"})")) },
            { QStringLiteral("text"), args(QStringLiteral("search"), outlineId, QStringLiteral(R"({"query":"alpha"})")) },
            { QStringLiteral("text"), args(QStringLiteral("get_text_in_area"), outlineId, QStringLiteral(R"({"page":1,"rect":{"left":0,"bottom":300,"right":300,"top":400}})")) },
            { QStringLiteral("render"), args(QStringLiteral("page"), outlineId, QStringLiteral(R"({"page":1,"dpi":36})")) },
            { QStringLiteral("render"), args(QStringLiteral("overview"), outlineId, QStringLiteral(R"({"pages":"1-3","maxSize":64})")) },
            { QStringLiteral("view"), args(QStringLiteral("activate"), outlineId) },
            { QStringLiteral("view"), args(QStringLiteral("go_to"), outlineId, QStringLiteral(R"({"outlineItem":"2.1"})")) },
            { QStringLiteral("view"), args(QStringLiteral("show_area"), outlineId, QStringLiteral(R"({"page":1,"rect":{"left":0,"bottom":0,"right":10,"top":10}})")) },
            { QStringLiteral("view"), args(QStringLiteral("show_search"), outlineId, QStringLiteral(R"({"query":"alpha"})")) },
            { QStringLiteral("view"), QStringLiteral(R"({"action":"navigate","navigate":{"route":"/help"}})") },
        };
        for (const auto& item : calls)
        {
            response = client->callTool(item.first, json(item.second));
            QVERIFY2(isHttpSuccess(response), qPrintable(item.second + QStringLiteral(" -> ") + QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact).left(400))));
            QVERIFY(!httpResult(response).value(QStringLiteral("content")).toArray().isEmpty());
        }

        // The documents resources and the completion
        response = client->request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), pdf::PDFAppMcpDocumentResources::getDocumentPrefix(outlineId) + QStringLiteral("info") } }));
        QVERIFY2(httpResult(response).value(QStringLiteral("contents")).toArray().size() == 1, QJsonDocument(response).toJson(QJsonDocument::Compact).left(400).constData());
        response = client->request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), pdf::PDFAppMcpDocumentResources::getDocumentPrefix(QString(26, QLatin1Char('d'))) + QStringLiteral("info") } }));
        QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        response = client->request(QStringLiteral("completion/complete"), json(QStringLiteral(R"({"ref":{"type":"ref/resource","uri":"pdf4qt://documents/{documentId}/info"},"argument":{"name":"documentId","value":""}})")));
        QCOMPARE(httpResult(response).value(QStringLiteral("completion")).toObject().value(QStringLiteral("values")).toArray().size(), 2);

        // MRTR of the modern era: the document chosen in the client, then the page of a label
        activateHome(fixture);
        client->setCapabilities(json(QStringLiteral(R"({"elicitation":{}})")));
        client->setElicitationResponder(&AppMcpReadTests::chooseFirst);
        client->setInputRetryRounds(3);
        response = client->callTool(QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_info"})")));
        QVERIFY2(isHttpSuccess(response), QJsonDocument(response).toJson(QJsonDocument::Compact).left(500).constData());
        QVERIFY(!httpAction(response, QStringLiteral("get_info")).value(QStringLiteral("documentId")).toString().isEmpty());
        response = client->getPrompt(QStringLiteral("summarize-document"));
        QVERIFY2(!httpResult(response).value(QStringLiteral("messages")).toArray().isEmpty(), QJsonDocument(response).toJson(QJsonDocument::Compact).left(500).constData());

        // A modern client without the capability: the fallback text
        std::unique_ptr<pdftest::McpTestClient> plain = fixture.client(pdftest::McpTestEra::Modern);
        response = plain->callTool(QStringLiteral("document"), json(QStringLiteral(R"({"action":"get_info"})")));
        QVERIFY(httpResult(response).value(QStringLiteral("isError")).toBool());
        QVERIFY(QJsonDocument(response).toJson().contains("Pass documentId"));

        // Legacy 2025-11-25: elicitation/create on the stream of the call
        std::unique_ptr<pdftest::McpTestClient> legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
        legacy->setCapabilities(json(QStringLiteral(R"({"elicitation":{}})")));
        legacy->setElicitationResponder(&AppMcpReadTests::chooseFirst);
        QVERIFY(!legacy->initialize().isEmpty());
        response = legacy->callTool(QStringLiteral("text"), json(QStringLiteral(R"({"action":"search","search":{"query":"alpha"}})")));
        QVERIFY2(isHttpSuccess(response), QJsonDocument(response).toJson(QJsonDocument::Compact).left(500).constData());
        QVERIFY(!httpAction(response, QStringLiteral("search")).value(QStringLiteral("matches")).toArray().isEmpty());
        QVERIFY(!httpResult(response).contains(QStringLiteral("resultType")));

        // Legacy 2025-06-18 without elicitation: the fallback text
        std::unique_ptr<pdftest::McpTestClient> old = fixture.client(pdftest::McpTestEra::Legacy20250618);
        QVERIFY(!old->initialize().isEmpty());
        response = old->callTool(QStringLiteral("text"), json(QStringLiteral(R"({"action":"search","search":{"query":"alpha"}})")));
        QVERIFY(httpResult(response).value(QStringLiteral("isError")).toBool());

        // view.close over HTTP
        response = client->callTool(QStringLiteral("view"), json(args(QStringLiteral("close"), attachmentsId)));
        QVERIFY2(isHttpSuccess(response), QJsonDocument(response).toJson(QJsonDocument::Compact).left(500).constData());
    }

    static void catalogToolBridgeHttp()
    {
        // The bridge of the tools of the catalog end to end (plan 3.10): the test
        // tool review.statistics over HTTP in both eras
        pdftest::AppMcpFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QVERIFY(fixture.startServer());
        pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("bridge.pdf"), 3));
        QVERIFY(tab);
        const std::shared_ptr<pdftest::TestDocumentTool> tool = std::make_shared<pdftest::TestDocumentTool>();
        pdf::PDFDocumentToolRegistry::registerTool(tool);
        fixture.host()->checkToolRegistry();

        for (pdftest::McpTestEra era : { pdftest::McpTestEra::Modern, pdftest::McpTestEra::Legacy20251125 })
        {
            std::unique_ptr<pdftest::McpTestClient> client = fixture.client(era);
            if (era != pdftest::McpTestEra::Modern)
            {
                QVERIFY(!client->initialize().isEmpty());
            }
            const QJsonObject list = client->listTools();
            bool hasReview = false;
            for (const QJsonValue& value : list.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray())
            {
                hasReview = hasReview || value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("review");
            }
            QVERIFY(hasReview);
            const QJsonObject response = client->callTool(QStringLiteral("review"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("statistics") },
                                                                                                    { QStringLiteral("documentId"), tab->getDocument()->getId() },
                                                                                                    { QStringLiteral("statistics"), json(QStringLiteral(R"({"mode":"count"})")) } }));
            QVERIFY2(isHttpSuccess(response), QJsonDocument(response).toJson(QJsonDocument::Compact).left(500).constData());
        }
        pdf::PDFDocumentToolRegistry::unregisterTool(QString::fromLatin1(pdftest::TestDocumentTool::TOOL_ID));
    }

    static void deletionMatrix()
    {
        // A tab closed while its render and its index wait
        {
            pdftest::AppMcpFixture fixture;
            QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
            pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 400));
            QVERIFY(tab);
            const QString id = tab->getDocument()->getId();
            std::shared_ptr<pdf::PDFMcpCall> render = fixture.startCall(QStringLiteral("render"), json(args(QStringLiteral("page"), id, QStringLiteral(R"({"page":1,"dpi":300})"))));
            std::shared_ptr<pdf::PDFMcpCall> search = fixture.startCall(QStringLiteral("text"), json(args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"dog"})"))));
            std::shared_ptr<pdf::PDFMcpCall> resource = start(fixture, resourceRequest(fixture, pdf::PDFAppMcpDocumentResources::getDocumentPrefix(id) + QStringLiteral("text")));
            QVERIFY(fixture.window->closeTab(tab));
            QVERIFY(pdftest::AppMcpFixture::waitResult(render, 60000));
            const Result searchResult = pdftest::AppMcpFixture::waitResult(search, 60000);
            QVERIFY(searchResult);
            QVERIFY(pdftest::AppMcpFixture::waitResult(resource, 60000));
        }

        // The window deleted while a search waits for the index; the host shut down
        // with jobs and waiters
        {
            pdftest::AppMcpFixture fixture;
            QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
            pdf::PDFAppDocumentTab* tab = openReady(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 400));
            QVERIFY(tab);
            const QString id = tab->getDocument()->getId();
            std::shared_ptr<pdf::PDFMcpCall> search = fixture.startCall(QStringLiteral("text"), json(args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"dog"})"))));
            std::shared_ptr<pdf::PDFMcpCall> render = fixture.startCall(QStringLiteral("render"), json(args(QStringLiteral("overview"), id, QStringLiteral(R"({"pages":"1-24"})"))));
            delete fixture.window.data();
            QVERIFY(pdftest::AppMcpFixture::waitResult(search, 60000));
            fixture.controller()->shutdown();
            QVERIFY(pdftest::AppMcpFixture::waitResult(render, 60000));
        }

        // A view deleted with its area outline; the host shut down with a signature wait
        {
            pdftest::AppMcpFixture fixture;
            QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
            pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("deleted.pdf"), 2));
            QVERIFY(tab);
            QVERIFY(isSuccess(call(fixture, QStringLiteral("view"), args(QStringLiteral("show_area"), tab->getDocument()->getId(), QStringLiteral(R"({"page":1,"rect":{"left":0,"bottom":0,"right":50,"top":50}})")))));
            QPointer<pdf::PDFAppMcpAreaHighlight> highlight = pdf::PDFAppMcpAreaHighlight::find(tab->getView());
            QVERIFY(highlight);
            QVERIFY(fixture.window->closeTab(tab));
            QTRY_VERIFY(!highlight);

            pdf::PDFAppDocumentTab* signedTab = openReady(fixture, pdftest::AppDocumentSamples::signedDocument(fixture.folder()));
            QVERIFY(signedTab);
            std::shared_ptr<pdf::PDFMcpCall> signatures = fixture.startCall(QStringLiteral("document"), json(args(QStringLiteral("get_signatures"), signedTab->getDocument()->getId())));
            fixture.controller()->shutdown();
            QVERIFY(pdftest::AppMcpFixture::waitResult(signatures, 30000));
        }

        // Calls started from the signals of the registry and of the document
        {
            pdftest::AppMcpFixture fixture;
            QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
            pdf::PDFAppDocumentTab* tab = openReady(fixture, makeSample(fixture.folder(), QStringLiteral("signals.pdf"), 2));
            QVERIFY(tab);
            std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
            QObject context;
            const QString id = tab->getDocument()->getId();
            QObject::connect(fixture.context->getDocumentRegistry(), &pdf::PDFAppDocumentRegistry::documentRemoved, &context, [&](const QString&)
            {
                calls.push_back(fixture.startCall(QStringLiteral("application"), json(QStringLiteral(R"({"action":"get_state"})"))));
                calls.push_back(fixture.startCall(QStringLiteral("document"), json(args(QStringLiteral("get_info"), id))));
            });
            QObject::connect(tab->getDocument(), &pdf::PDFAppDocument::aboutToBeDestroyed, &context, [&]()
            {
                calls.push_back(fixture.startCall(QStringLiteral("text"), json(args(QStringLiteral("search"), id, QStringLiteral(R"({"query":"alpha"})")))));
            });
            QVERIFY(fixture.window->closeTab(tab));
            QTRY_VERIFY(calls.size() >= 3);
            for (const std::shared_ptr<pdf::PDFMcpCall>& started : calls)
            {
                QVERIFY(pdftest::AppMcpFixture::waitResult(started, 30000));
            }
        }
    }
};

#endif // Q_MOC_RUN

#endif // APPMCPREADTESTS_H
