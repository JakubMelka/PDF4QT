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

#ifndef APPMCPSTEP4_H
#define APPMCPSTEP4_H

// The shared helpers of the MCP tests of step 4 of phase 2 (UnitTestsAppMcpOptimize,
// UnitTestsAppMcpConvert): documents of images written from objects as text, the calls of the
// actions over HTTP and the reading of their results.

#include "appmcpfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmcpcatalogtools.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumentwriter.h"
#include "pdfparser.h"
#include "pdfsecurityhandler.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <optional>
#include <vector>

namespace pdftest
{

/// A document of images: image XObjects drawn by known matrices on pages of 300 x 400 pt
class AppMcpImageDocument
{
public:
    static pdf::PDFObject parse(const QByteArray& text)
    {
        pdf::PDFParser parser(text, nullptr, pdf::PDFParser::None);
        return parser.getObject();
    }

    static QByteArray ref(pdf::PDFObjectReference reference)
    {
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R";
    }

    /// The id of an image XObject in MCP (PDFDocumentImageList::Key::toString, "148 0")
    static QString id(pdf::PDFObjectReference reference)
    {
        return QString::number(reference.objectNumber) + QStringLiteral(" ") + QString::number(reference.generation);
    }

    pdf::PDFObjectReference addStream(const QByteArray& dictionaryText, QByteArray data)
    {
        const pdf::PDFObject dictionaryObject = parse(dictionaryText);
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data))));
    }

    /// An uncompressed DeviceRGB image with a gradient (its colors are not gray)
    pdf::PDFObjectReference addColorImage(int width, int height)
    {
        QByteArray data;
        data.reserve(width * height * 3);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                data += char(uchar(x * 255 / qMax(1, width - 1)));
                data += char(uchar(y * 255 / qMax(1, height - 1)));
                data += char(uchar(40));
            }
        }
        return addStream("<< /Type /XObject /Subtype /Image /Width " + QByteArray::number(width) + " /Height " + QByteArray::number(height) +
                         " /ColorSpace /DeviceRGB /BitsPerComponent 8 >>", data);
    }

    /// An uncompressed DeviceGray image of 32 x 32 pixels
    pdf::PDFObjectReference addGrayImage()
    {
        return addStream("<< /Type /XObject /Subtype /Image /Width 32 /Height 32 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(32 * 32, '\x80'));
    }

    /// A page of 300 x 400 pt with the resources and the content
    pdf::PDFObjectReference addPage(const QByteArray& resources, const QByteArray& content)
    {
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference contents = addStream("<< >>", content);
        builder.mergeTo(page, parse("<< /Resources " + resources + " /Contents " + ref(contents) + " >>"));
        return page;
    }

    /// A page drawing the images by their matrices ("a 0 0 d e f")
    pdf::PDFObjectReference addImagePage(const std::vector<std::pair<pdf::PDFObjectReference, QByteArray>>& images)
    {
        QByteArray xobjects;
        QByteArray content;
        for (size_t i = 0; i < images.size(); ++i)
        {
            xobjects += "/Im" + QByteArray::number(qint64(i)) + " " + ref(images[i].first) + " ";
            content += "q " + images[i].second + " cm /Im" + QByteArray::number(qint64(i)) + " Do Q\n";
        }
        return addPage("<< /XObject << " + xobjects + " >> >>", content);
    }

    /// The document is encrypted by the owner password only with the permissions (ISO 32000-2, 7.6.4.2, Table 22)
    void encrypt(uint32_t permissions)
    {
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.ownerPassword = QStringLiteral("owner-secret");
        settings.permissions = permissions;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    }

    /// Writes the document into a file; empty = an error
    QString write(const QString& filePath)
    {
        pdf::PDFDocument document = builder.build();
        return pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false) ? filePath : QString();
    }

    pdf::PDFDocumentBuilder builder;
};

/// Calls and results of the MCP actions of step 4
class AppMcpStep4
{
public:
    AppMcpStep4() = delete;

    /// Every bit of Table 22 of ISO 32000-2 allowed but \p bit (bit 1 = the lowest)
    static uint32_t allowedWithout(int bit)
    {
        return 0xFFFFFFFCu & ~(uint32_t(1) << (bit - 1));
    }

    /// Opens the file and waits until its document is ready
    static pdf::PDFAppDocumentTab* open(AppMcpFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = filePath.isEmpty() ? nullptr : fixture.open(filePath);
        const int timeout = 30000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
        return tab && QTest::qWaitFor([tab]() { return tab->getDocument()->isReady(); }, timeout) ? tab : nullptr;
    }

    /// A call of an action of a tool: the parameters of the action and the common arguments
    static std::optional<pdf::PDFMcpCallResult> call(AppMcpFixture& fixture, const QString& tool, const QString& action, const QJsonObject& parameters, const QJsonObject& common)
    {
        QJsonObject arguments = common;
        arguments.insert(QStringLiteral("action"), action);
        if (!parameters.isEmpty())
        {
            arguments.insert(action, parameters);
        }
        return fixture.callTool(tool, arguments);
    }

    /// The structured content of the action of a result
    static QJsonObject content(const pdf::PDFMcpCallResult& result, const QString& action)
    {
        return QJsonDocument::fromJson(result.result).object().value(QStringLiteral("structuredContent")).toObject().value(action).toObject();
    }

    /// document.list_images of the document of the tab
    static QJsonObject listImages(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* tab, const QJsonObject& parameters = QJsonObject())
    {
        const std::optional<pdf::PDFMcpCallResult> result = callListImages(fixture, tab, parameters);
        return result && !result->isError ? content(*result, QStringLiteral("list_images")) : QJsonObject();
    }

    /// The call of document.list_images with the parameters of the action
    static std::optional<pdf::PDFMcpCallResult> callListImages(AppMcpFixture& fixture, pdf::PDFAppDocumentTab* tab, const QJsonObject& parameters)
    {
        const QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("list_images") }, { QStringLiteral("documentId"), tab->getDocument()->getId() },
                                      { QStringLiteral("list_images"), parameters } });
        return fixture.callTool(QStringLiteral("document"), arguments);
    }

    /// The input schema of the action of a catalog tool (the object of the action)
    static QJsonObject actionSchema(const QString& toolId)
    {
        const qsizetype dot = toolId.indexOf(QLatin1Char('.'));
        const QString toolName = toolId.left(dot);
        const QString actionName = toolId.mid(dot + 1);
        const pdf::PDFAppMcpPermissions all = pdf::PDFAppMcpPermissions::createAll(pdf::PDFAppMcpPermission::Allow);
        for (const pdf::PDFMcpToolDefinition& definition : pdf::PDFAppMcpCatalogTools::createDefinitions(all))
        {
            if (definition.name == toolName)
            {
                const QJsonObject properties = QJsonDocument::fromJson(definition.inputSchema).object().value(QStringLiteral("properties")).toObject();
                return properties.value(actionName).toObject();
            }
        }
        return QJsonObject();
    }

    /// The names of the parameters of the tool, which an assistant gives (not panel-only)
    static QStringList getAssistantParameters(const QString& toolId)
    {
        QStringList names;
        const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(toolId);
        for (const pdf::PDFDocumentToolParameterDefinition& definition : tool ? tool->getParameters() : pdf::PDFDocumentToolParameterDefinitions())
        {
            if (!definition.isPanelOnly())
            {
                names << definition.getName();
            }
        }
        return names;
    }

    /// The object of the document by its reference
    static const pdf::PDFDictionary* imageDictionary(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFObject& object = document->getObjectByReference(reference);
        return object.isStream() ? object.getStream()->getDictionary() : nullptr;
    }

    /// The name of the color space of an image XObject of the document (empty = none or no name)
    static QByteArray colorSpace(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFDictionary* dictionary = imageDictionary(document, reference);
        const pdf::PDFObject& colorSpaceObject = dictionary ? document->getObject(dictionary->get("ColorSpace")) : pdf::PDFObject();
        return colorSpaceObject.isName() ? colorSpaceObject.getString() : QByteArray();
    }

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
    }
};

}   // namespace pdftest

#endif // APPMCPSTEP4_H
