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

#include "appdocumentfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappundostack.h"
#include "pdfappfiledrop.h"
#include "pdfappnavigator.h"
#include "pdfdatabasenotifier.h"

#include "pdfcertificatemanager.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetsnapshot.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumentwriter.h"
#include "pdfsecurityhandler.h"

#include "pdfmessagebox.h"
#include "pdfthememanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QTest>

namespace pdftest
{

/// Building blocks of the samples
class AppDocumentSamplesHelper
{
public:
    AppDocumentSamplesHelper() = delete;

    static pdf::PDFObject name(const char* value)
    {
        return pdf::PDFObject::createName(QByteArray(value));
    }

    static pdf::PDFObject text(const QString& value)
    {
        return pdf::PDFObjectFactory::createTextString(value);
    }

    static pdf::PDFObject reference(pdf::PDFObjectReference value)
    {
        return pdf::PDFObject::createReference(value);
    }

    static pdf::PDFObject integer(pdf::PDFInteger value)
    {
        return pdf::PDFObject::createInteger(value);
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

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    static pdf::PDFObject stream(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries, QByteArray data)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        builder.setEntry(pdf::PDFInplaceOrMemoryString("Length"), integer(data.size()));
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(builder), std::move(data)));
    }

    /// Destination [page /Fit]
    static pdf::PDFObject fit(pdf::PDFObjectReference page)
    {
        return array({ reference(page), name("Fit") });
    }

    /// Appends the pages with the text of AppDocumentSamples::getPageText
    static std::vector<pdf::PDFObjectReference> appendPages(pdf::PDFDocumentBuilder& builder, int pageCount)
    {
        std::vector<pdf::PDFObjectReference> pages;
        pages.reserve(size_t(qMax(pageCount, 0)));
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
            const QStringList lines = AppDocumentSamples::getPageText(i).split(QLatin1Char('\n'));
            for (int line = 0; line < lines.size(); ++line)
            {
                painter->drawText(QPointF(30, 40 + 20 * line), lines[line]);
            }
            content.end(painter);
        }
        return pages;
    }

    static QString write(pdf::PDFDocumentBuilder& builder, const QString& folder, const QString& fileName)
    {
        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(fileName);
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static void encrypt(pdf::PDFDocumentBuilder& builder, const QString& userPassword, const QString& ownerPassword, uint32_t permissions)
    {
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = userPassword;
        settings.ownerPassword = ownerPassword;
        settings.permissions = permissions;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    }
};

QString AppDocumentSamples::getPageText(int pageIndex)
{
    return QStringLiteral("Page %1\nalpha beta gamma %1\nThe quick brown fox jumps over the lazy dog.").arg(pageIndex + 1);
}

bool AppDocumentSamples::write(pdf::PDFDocument& document, const QString& filePath)
{
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    return bool(pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false));
}

QString AppDocumentSamples::simple(const QString& folder, const QString& name, int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, pageCount);
    return AppDocumentSamplesHelper::write(builder, folder, name);
}

QString AppDocumentSamples::pageLabels(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    H::appendPages(builder, 10);

    const pdf::PDFObject labels = H::dictionary({ { "Nums", H::array({ H::integer(0), H::dictionary({ { "S", H::name("r") } }),
                                                                       H::integer(3), H::dictionary({ { "S", H::name("D") } }),
                                                                       H::integer(8), H::dictionary({ { "S", H::name("D") }, { "P", H::text(QStringLiteral("A-")) } }) }) } });
    builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "PageLabels", labels } }));
    return H::write(builder, folder, QStringLiteral("page-labels.pdf"));
}

QString AppDocumentSamples::outline(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 5);

    // References first (the items refer to each other)
    const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference introduction = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference chapter = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference section = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference appendix = builder.addObject(pdf::PDFObject());

    builder.setObject(root, H::dictionary({ { "Type", H::name("Outlines") }, { "First", H::reference(introduction) }, { "Last", H::reference(appendix) }, { "Count", H::integer(4) } }));
    builder.setObject(introduction, H::dictionary({ { "Title", H::text(QStringLiteral("Introduction")) }, { "Parent", H::reference(root) }, { "Next", H::reference(chapter) }, { "Dest", H::fit(pages[0]) } }));
    builder.setObject(chapter, H::dictionary({ { "Title", H::text(QStringLiteral("Chapter 1")) }, { "Parent", H::reference(root) }, { "Prev", H::reference(introduction) }, { "Next", H::reference(appendix) },
                                               { "First", H::reference(section) }, { "Last", H::reference(section) }, { "Count", H::integer(1) }, { "Dest", H::fit(pages[1]) } }));
    builder.setObject(section, H::dictionary({ { "Title", H::text(QStringLiteral("Section 1.1")) }, { "Parent", H::reference(chapter) }, { "Dest", H::fit(pages[2]) } }));
    builder.setObject(appendix, H::dictionary({ { "Title", H::text(QStringLiteral("Appendix")) }, { "Parent", H::reference(root) }, { "Prev", H::reference(chapter) }, { "Dest", H::fit(pages[4]) } }));
    builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "Outlines", H::reference(root) } }));
    return H::write(builder, folder, QStringLiteral("outline.pdf"));
}

QString AppDocumentSamples::links(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 4);
    const pdf::PDFObjectReference page = pages[0];

    const pdf::PDFObjectReference goTo = builder.createActionGoTo(pdf::PDFDestination::createFit(pages[2]));
    const pdf::PDFObjectReference goToNamed = builder.addObject(H::dictionary({ { "Type", H::name("Action") }, { "S", H::name("GoTo") }, { "D", pdf::PDFObject::createString(QByteArray("chapter2")) } }));
    const pdf::PDFObjectReference launch = builder.createActionLaunch(builder.createFileSpecification(QStringLiteral("notepad.exe")), false);
    const pdf::PDFObjectReference goToRemote = builder.createActionGoToRemote(builder.createFileSpecification(QStringLiteral("other.pdf")), pdf::PDFDestination::createNamed(QByteArray("start")), true);
    const pdf::PDFObjectReference javaScript = builder.createActionJavaScript(QStringLiteral("app.alert('x')"));

    builder.createAnnotationLink(page, QRectF(30, 100, 200, 16), goTo, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 120, 200, 16), goToNamed, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 140, 200, 16), QStringLiteral("https://example.com/"), pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 160, 200, 16), launch, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 180, 200, 16), goToRemote, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 200, 200, 16), javaScript, pdf::LinkHighlightMode::Invert);

    // Named destination "chapter2" (page 4) in the /Dests name tree
    const pdf::PDFObject dests = H::dictionary({ { "Names", H::array({ pdf::PDFObject::createString(QByteArray("chapter2")), H::fit(pages[3]) }) } });
    const pdf::PDFObjectReference names = builder.addObject(H::dictionary({ { "Dests", dests } }));
    builder.setCatalogNames(names);
    return H::write(builder, folder, QStringLiteral("links.pdf"));
}

QString AppDocumentSamples::attachments(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 2);

    auto createFile = [&](const QString& fileName, const QByteArray& data)
    {
        const pdf::PDFObjectReference stream = builder.addObject(H::stream({ { "Type", H::name("EmbeddedFile") } }, data));
        return builder.addObject(H::dictionary({ { "Type", H::name("Filespec") }, { "F", H::text(fileName) }, { "UF", H::text(fileName) },
                                                 { "EF", H::dictionary({ { "F", H::reference(stream) } }) } }));
    };

    const pdf::PDFObjectReference notes = createFile(QStringLiteral("notes.txt"), QByteArray("Notes of the sample\n"));
    const pdf::PDFObjectReference data = createFile(QStringLiteral("data.csv"), QByteArray("a,b\n1,2\n"));
    builder.createAnnotationFileAttachment(pages[0], QPointF(250, 50), notes, pdf::FileAttachmentIcon::Paperclip, QStringLiteral("Author"), QStringLiteral("Notes"));

    const pdf::PDFObject embeddedFiles = H::dictionary({ { "Names", H::array({ H::text(QStringLiteral("data.csv")), H::reference(data),
                                                                               H::text(QStringLiteral("notes.txt")), H::reference(notes) }) } });
    const pdf::PDFObjectReference names = builder.addObject(H::dictionary({ { "EmbeddedFiles", embeddedFiles } }));
    builder.setCatalogNames(names);
    return H::write(builder, folder, QStringLiteral("attachments.pdf"));
}

QString AppDocumentSamples::layers(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 1);

    const pdf::PDFObjectReference layerA = builder.addObject(H::dictionary({ { "Type", H::name("OCG") }, { "Name", H::text(QStringLiteral("Layer A")) } }));
    const pdf::PDFObjectReference layerB = builder.addObject(H::dictionary({ { "Type", H::name("OCG") }, { "Name", H::text(QStringLiteral("Layer B")) } }));
    const pdf::PDFObject properties = H::dictionary({ { "OCGs", H::array({ H::reference(layerA), H::reference(layerB) }) },
                                                      { "D", H::dictionary({ { "Order", H::array({ H::reference(layerA), H::reference(layerB) }) },
                                                                             { "OFF", H::array({ H::reference(layerB) }) } }) } });
    builder.setCatalogOptionalContentProperties(builder.addObject(properties));

    // A second content stream of the page draws a rectangle in each layer
    const pdf::PDFObjectReference content = builder.addObject(H::stream({ }, QByteArray("/OC /LA BDC 0 0 1 rg 10 10 50 50 re f EMC /OC /LB BDC 1 0 0 rg 70 10 50 50 re f EMC\n")));
    const pdf::PDFObject& pageObject = builder.getObjectByReference(pages[0]);
    const pdf::PDFDictionary* pageDictionary = pageObject.isDictionary() ? pageObject.getDictionary() : nullptr;
    if (pageDictionary)
    {
        pdf::PDFArrayBuilder contents;
        const pdf::PDFObject& existing = pageDictionary->get("Contents");
        if (existing.isArray())
        {
            for (size_t i = 0; i < existing.getArray()->getCount(); ++i)
            {
                contents.appendItem(existing.getArray()->getItem(i));
            }
        }
        else if (!existing.isNull())
        {
            contents.appendItem(existing);
        }
        contents.appendItem(H::reference(content));
        builder.mergeTo(pages[0], H::dictionary({ { "Contents", pdf::PDFObject::createArray(std::move(contents)) },
                                                  { "Resources", H::dictionary({ { "Properties", H::dictionary({ { "LA", H::reference(layerA) }, { "LB", H::reference(layerB) } }) } }) } }));
    }
    return H::write(builder, folder, QStringLiteral("layers.pdf"));
}

QString AppDocumentSamples::form(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 1);

    const pdf::PDFObjectReference field = builder.addObject(H::dictionary({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("name")) }, { "V", H::text(QStringLiteral("John")) },
                                                                           { "DA", pdf::PDFObject::createString(QByteArray("/Helv 12 Tf 0 g")) } }));
    builder.createFormFieldWidget(field, pages[0], pdf::PDFObjectReference(), QRectF(30, 250, 200, 24));
    const pdf::PDFObjectReference acroForm = builder.createAcroForm({ field });
    builder.setCatalogAcroForm(acroForm);
    return H::write(builder, folder, QStringLiteral("form.pdf"));
}

QString AppDocumentSamples::userPassword(const QString& folder)
{
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, 2);
    AppDocumentSamplesHelper::encrypt(builder, getUserPassword(), getOwnerPassword(), 0xFFFFFFFCu);
    return AppDocumentSamplesHelper::write(builder, folder, QStringLiteral("user-password.pdf"));
}

QString AppDocumentSamples::ownerPassword(const QString& folder)
{
    // Printing and copying are not allowed (bits 3 and 5 cleared)
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, 2);
    AppDocumentSamplesHelper::encrypt(builder, QString(), getOwnerPassword(), 0xFFFFFFFCu & ~0x0014u);
    return AppDocumentSamplesHelper::write(builder, folder, QStringLiteral("owner-password.pdf"));
}

QString AppDocumentSamples::signedDocument(const QString& folder, QString* certificatePath)
{
    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.fileName = QDir(folder).absoluteFilePath(QStringLiteral("signature-test.p12"));
    info.privateKeyPasword = getCertificatePassword();
    info.certCommonName = QStringLiteral("PDF4QT document tab test");
    info.rsaKeyLength = 2048;
    pdf::PDFCertificateManager().createCertificate(info);

    QFile certificateFile(info.fileName);
    if (!certificateFile.open(QIODevice::ReadOnly))
    {
        return QString();
    }
    pdf::PDFCertificateEntry certificate;
    certificate.pkcs12 = certificateFile.readAll();
    certificateFile.close();
    if (certificatePath)
    {
        *certificatePath = info.fileName;
    }

    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, 2);
    const pdf::PDFDocument document = builder.build();

    QByteArray signedData;
    pdf::PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return pdf::PDFSignatureFactory::sign(certificate, getCertificatePassword(), data, signature);
    };
    parameters.createSignatureFieldFunction = [&](pdf::PDFDocumentBuilder& signatureBuilder, pdf::PDFObjectReference signatureDictionary)
    {
        return signatureBuilder.createSignatureField(QStringLiteral("Signature1"), signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };
    if (pdf::PDFDocumentSigner::sign(parameters, signedData) != pdf::PDFDocumentSigner::Result::OK)
    {
        return QString();
    }

    const QString filePath = QDir(folder).absoluteFilePath(QStringLiteral("signed.pdf"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(signedData) != signedData.size())
    {
        return QString();
    }
    return filePath;
}

QString AppDocumentSamples::large(const QString& folder, int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, pageCount);
    return AppDocumentSamplesHelper::write(builder, folder, QStringLiteral("large-%1.pdf").arg(pageCount));
}

QString AppDocumentSamples::annotations(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 3);
    const pdf::PDFObjectReference page = pages[0];
    const QString author = QStringLiteral("Tester");
    const QColor red(Qt::red);
    const QColor blue(Qt::blue);

    // Grid of cells 66 x 50 below the text of the page (x 12 + 70 * column, y 100 + 54 * row)
    auto cell = [](int column, int row) { return QRectF(12 + 70 * column, 100 + 54 * row, 60, 44); };
    auto withFlags = [&builder](pdf::PDFObjectReference annotation, pdf::PDFInteger flags)
    {
        builder.mergeTo(annotation, H::dictionary({ { "F", H::integer(flags) } }));
    };

    // Shapes
    builder.createAnnotationSquare(page, cell(0, 0), 2.0, QColor(), red, author, QStringLiteral("square"), QStringLiteral("Square"));
    builder.createAnnotationCircle(page, cell(1, 0), 2.0, QColor(Qt::yellow), blue, author, QStringLiteral("circle"), QStringLiteral("Circle"));
    builder.createAnnotationLine(page, cell(2, 0), cell(2, 0).bottomLeft(), cell(2, 0).topRight(), 2.0, QColor(), red, author, QStringLiteral("line"), QStringLiteral("Line"),
                                 pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::OpenArrow);
    const QRectF polylineCell = cell(3, 0);
    builder.createAnnotationPolyline(page, QPolygonF({ polylineCell.topLeft(), polylineCell.center(), polylineCell.bottomRight() }), 2.0, QColor(), blue, author,
                                     QStringLiteral("polyline"), QStringLiteral("Polyline"), pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::None);
    const QRectF polygonCell = cell(0, 1);
    builder.createAnnotationPolygon(page, QPolygonF({ polygonCell.topLeft(), polygonCell.topRight(), polygonCell.bottomRight(), polygonCell.topLeft() }), 2.0,
                                    QColor(Qt::green), red, author, QStringLiteral("polygon"), QStringLiteral("Polygon"));
    const QRectF inkCell = cell(1, 1);
    builder.createAnnotationInk(page, QPolygonF({ inkCell.topLeft(), inkCell.center(), inkCell.topRight(), inkCell.bottomRight() }), 2.0, blue, author,
                                QStringLiteral("ink"), QStringLiteral("Ink"));

    // Text markups (over the text lines of the page: y 30 - 85)
    builder.createAnnotationHighlight(page, QRectF(28, 28, 60, 16), QColor(Qt::yellow), author, QStringLiteral("highlight"), QStringLiteral("Highlight"));
    const pdf::PDFObjectReference underline = builder.createAnnotationUnderline(page, QRectF(28, 48, 60, 16), blue);
    builder.setAnnotationTitle(underline, author);
    builder.setAnnotationSubject(underline, QStringLiteral("underline"));
    builder.createAnnotationStrikeout(page, QRectF(100, 48, 60, 16), red, author, QStringLiteral("strikeout"), QStringLiteral("Strikeout"));
    const pdf::PDFObjectReference squiggly = builder.createAnnotationSquiggly(page, QRectF(28, 68, 60, 16), QColor(Qt::darkGreen));
    builder.setAnnotationTitle(squiggly, author);
    builder.setAnnotationSubject(squiggly, QStringLiteral("squiggly"));

    // Free texts (plain and with a callout)
    builder.createAnnotationFreeText(page, cell(2, 1), cell(2, 1), author, QStringLiteral("freetext"), QStringLiteral("Free text"), pdf::TextAlignment(Qt::AlignLeft | Qt::AlignTop),
                                     QPointF(), QPointF(), QPointF(), pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::None);
    const QRectF calloutCell = cell(3, 1).united(cell(3, 2));
    const QRectF calloutText(calloutCell.left() + 10, calloutCell.top(), 50, 30);
    builder.createAnnotationFreeText(page, calloutCell, calloutText, author, QStringLiteral("callout"), QStringLiteral("Callout"), pdf::TextAlignment(Qt::AlignLeft | Qt::AlignTop),
                                     calloutCell.bottomLeft() + QPointF(2, -2), QPointF(calloutText.left(), calloutText.bottom() + 6), QPointF(calloutText.left(), calloutText.bottom()),
                                     pdf::AnnotationLineEnding::OpenArrow, pdf::AnnotationLineEnding::None);

    // Stamp, caret
    builder.createAnnotationStamp(page, cell(0, 2), pdf::Stamp::Approved, author, QStringLiteral("stamp"), QStringLiteral("Stamp"));
    builder.createAnnotationCaret(page, QRectF(cell(1, 2).topLeft(), QSizeF(16, 16)), 1.0, blue, author, QStringLiteral("caret"), QStringLiteral("Caret"));

    // Sticky note with its popup and a thread of two replies
    const QRectF noteRectangle(cell(2, 2).topLeft(), QSizeF(20, 20));
    const pdf::PDFObjectReference note = builder.createAnnotationText(page, noteRectangle, pdf::TextAnnotationIcon::Comment, author, QStringLiteral("text"),
                                                                      QStringLiteral("Note with replies"), false);
    for (int i = 1; i <= 2; ++i)
    {
        const pdf::PDFObjectReference reply = builder.createAnnotationText(page, noteRectangle, pdf::TextAnnotationIcon::Comment, QStringLiteral("Replier %1").arg(i),
                                                                           QStringLiteral("reply"), QStringLiteral("Reply %1").arg(i), false);
        builder.mergeTo(reply, H::dictionary({ { "IRT", H::reference(note) }, { "RT", H::name("R") } }));
    }

    // File attachment
    const pdf::PDFObjectReference stream = builder.addObject(H::stream({ { "Type", H::name("EmbeddedFile") } }, QByteArray("Attached text\n")));
    const pdf::PDFObjectReference file = builder.addObject(H::dictionary({ { "Type", H::name("Filespec") }, { "F", H::text(QStringLiteral("attached.txt")) },
                                                                           { "UF", H::text(QStringLiteral("attached.txt")) }, { "EF", H::dictionary({ { "F", H::reference(stream) } }) } }));
    const pdf::PDFObjectReference attachment = builder.createAnnotationFileAttachment(page, cell(3, 3).topLeft() + QPointF(4, 4), file, pdf::FileAttachmentIcon::Paperclip,
                                                                                      author, QStringLiteral("Attachment"));
    builder.setAnnotationSubject(attachment, QStringLiteral("fileattachment"));

    // Measurement line (a distance with a caption and leader lines)
    const QRectF measureCell = cell(0, 3).united(cell(1, 3));
    const pdf::PDFObjectReference measurement = builder.createAnnotationLine(page, measureCell, measureCell.bottomLeft() + QPointF(4, -10), measureCell.bottomRight() + QPointF(-4, -10),
                                                                             1.0, QColor(), red, author, QStringLiteral("measurement"), QStringLiteral("1.67 in"),
                                                                             pdf::AnnotationLineEnding::OpenArrow, pdf::AnnotationLineEnding::OpenArrow);
    const pdf::PDFObject numberFormat = H::array({ H::dictionary({ { "Type", H::name("NumberFormat") }, { "U", H::text(QStringLiteral("in")) },
                                                                   { "C", pdf::PDFObject::createReal(1.0 / 72.0) }, { "D", H::integer(100) } }) });
    builder.mergeTo(measurement, H::dictionary({ { "IT", H::name("LineDimension") }, { "Cap", pdf::PDFObject::createBool(true) }, { "LL", pdf::PDFObject::createReal(6.0) },
                                                 { "Measure", H::dictionary({ { "Type", H::name("Measure") }, { "Subtype", H::name("RL") }, { "R", H::text(QStringLiteral("1 in = 1 in")) },
                                                                              { "X", numberFormat }, { "D", numberFormat }, { "A", numberFormat } }) } }));

    // Flags: hidden, locked, read only, no zoom + no rotate
    const pdf::PDFObjectReference hidden = builder.createAnnotationSquare(page, cell(2, 3), 1.0, QColor(), red, author, QStringLiteral("hidden"), QStringLiteral("Hidden"));
    withFlags(hidden, 2 | 4);
    const pdf::PDFObjectReference locked = builder.createAnnotationSquare(page, cell(0, 4), 1.0, QColor(), blue, author, QStringLiteral("locked"), QStringLiteral("Locked"));
    withFlags(locked, 4 | 128);
    const pdf::PDFObjectReference readOnly = builder.createAnnotationSquare(page, cell(1, 4), 1.0, QColor(), blue, author, QStringLiteral("readonly"), QStringLiteral("Read only"));
    withFlags(readOnly, 4 | 64);
    const pdf::PDFObjectReference noZoom = builder.createAnnotationText(page, QRectF(cell(2, 4).topLeft(), QSizeF(20, 20)), pdf::TextAnnotationIcon::Key, author,
                                                                        QStringLiteral("nozoom"), QStringLiteral("No zoom, no rotate"), false);
    withFlags(noZoom, 4 | 8 | 16);

    // Page 2: two overlapping rectangles (the chooser of overlapping annotations) and a highlight
    builder.createAnnotationSquare(pages[1], QRectF(50, 150, 100, 80), 2.0, QColor(), red, author, QStringLiteral("overlapA"), QStringLiteral("Overlap A"));
    builder.createAnnotationSquare(pages[1], QRectF(90, 180, 100, 80), 2.0, QColor(), blue, author, QStringLiteral("overlapB"), QStringLiteral("Overlap B"));
    builder.createAnnotationHighlight(pages[1], QRectF(28, 28, 60, 16), QColor(Qt::yellow), author, QStringLiteral("highlight2"), QStringLiteral("Highlight on page 2"));

    return H::write(builder, folder, QStringLiteral("annotations.pdf"));
}

QString AppDocumentSamples::restricted(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    // Owner password only: opens without a password, annotations (bit 6) and the
    // filling of the form (bit 9) are not allowed
    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 2);
    builder.createAnnotationSquare(pages[0], QRectF(40, 150, 100, 80), 2.0, QColor(), QColor(Qt::red), QStringLiteral("Tester"), QStringLiteral("square"), QStringLiteral("Square"));
    const pdf::PDFObjectReference field = builder.addObject(H::dictionary({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("name")) }, { "V", H::text(QStringLiteral("John")) },
                                                                           { "DA", pdf::PDFObject::createString(QByteArray("/Helv 12 Tf 0 g")) } }));
    builder.createFormFieldWidget(field, pages[0], pdf::PDFObjectReference(), QRectF(40, 260, 200, 24));
    builder.setCatalogAcroForm(builder.createAcroForm({ field }));
    H::encrypt(builder, QString(), getOwnerPassword(), 0xFFFFFFFCu & ~(0x0020u | 0x0100u));
    return H::write(builder, folder, QStringLiteral("restricted.pdf"));
}

QString AppDocumentSamples::fullForm(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 1);
    const pdf::PDFObjectReference page = pages[0];
    const pdf::PDFObject da = pdf::PDFObject::createString(QByteArray("/Helv 10 Tf 0 g"));
    pdf::PDFObjectReferenceVector fields;

    // A field, which is its own widget
    auto addField = [&](std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries, const QRectF& rectangle)
    {
        const pdf::PDFObjectReference field = builder.addObject(H::dictionary(entries));
        builder.createFormFieldWidget(field, page, pdf::PDFObjectReference(), rectangle);
        fields.push_back(field);
        return field;
    };

    // Appearance stream of a state of a check box / radio button
    auto appearance = [&builder](bool isOn)
    {
        const QByteArray content = isOn ? QByteArray("0 g 3 3 8 8 re f\n") : QByteArray("0 G 0.5 0.5 13 13 re S\n");
        return builder.addObject(H::stream({ { "Type", H::name("XObject") }, { "Subtype", H::name("Form") },
                                             { "BBox", H::array({ H::integer(0), H::integer(0), H::integer(14), H::integer(14) }) } }, content));
    };

    auto pdfReal = [](double value) { return pdf::PDFObject::createReal(value); };
    auto rectangle = [&pdfReal](const QRectF& rect) { return H::array({ pdfReal(rect.left()), pdfReal(rect.top()), pdfReal(rect.right()), pdfReal(rect.bottom()) }); };

    // Text fields: single line, multi line, comb, password, read only, required
    addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("name")) }, { "V", H::text(QStringLiteral("John")) }, { "DA", da } }, QRectF(20, 100, 160, 18));
    addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("notes")) }, { "Ff", H::integer(1 << 12) }, { "DA", da } }, QRectF(20, 122, 160, 40));
    addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("code")) }, { "Ff", H::integer(1 << 24) }, { "MaxLen", H::integer(6) }, { "DA", da } }, QRectF(20, 166, 120, 18));
    addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("secret")) }, { "Ff", H::integer(1 << 13) }, { "DA", da } }, QRectF(20, 188, 120, 18));
    addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("fixed")) }, { "Ff", H::integer(1) }, { "V", H::text(QStringLiteral("Read only")) }, { "DA", da } }, QRectF(20, 210, 120, 18));
    addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("required")) }, { "Ff", H::integer(2) }, { "DA", da } }, QRectF(20, 232, 120, 18));

    // Calculated read-only field (JavaScript is not run, O16)
    const pdf::PDFObjectReference calculate = builder.createActionJavaScript(QStringLiteral("event.value = 1 + 1;"));
    const pdf::PDFObjectReference total = addField({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("total")) }, { "Ff", H::integer(1) }, { "DA", da },
                                                     { "AA", H::dictionary({ { "C", H::reference(calculate) } }) } }, QRectF(20, 254, 120, 18));

    // Check box with the states Yes / Off
    const pdf::PDFObject checkBoxStates = H::dictionary({ { "N", H::dictionary({ { "Yes", H::reference(appearance(true)) }, { "Off", H::reference(appearance(false)) } }) } });
    const pdf::PDFObjectReference checkBox = builder.addObject(H::dictionary({ { "FT", H::name("Btn") }, { "T", H::text(QStringLiteral("agree")) }, { "V", H::name("Off") },
                                                                              { "Type", H::name("Annot") }, { "Subtype", H::name("Widget") }, { "P", H::reference(page) },
                                                                              { "Rect", rectangle(QRectF(200, 100, 14, 14)) }, { "AS", H::name("Off") }, { "AP", checkBoxStates } }));
    builder.appendTo(page, H::dictionary({ { "Annots", H::array({ H::reference(checkBox) }) } }));
    fields.push_back(checkBox);

    // Radio group "choice" with two buttons A / B (NoToggleToOff, Radio)
    const pdf::PDFObjectReference radio = builder.addObject(H::dictionary({ { "FT", H::name("Btn") }, { "T", H::text(QStringLiteral("choice")) }, { "Ff", H::integer((1 << 14) | (1 << 15)) },
                                                                           { "V", H::name("A") } }));
    pdf::PDFArrayBuilder kids;
    int index = 0;
    for (const char* state : { "A", "B" })
    {
        const pdf::PDFObject states = H::dictionary({ { "N", H::dictionary({ { state, H::reference(appearance(true)) }, { "Off", H::reference(appearance(false)) } }) } });
        const pdf::PDFObjectReference kid = builder.addObject(H::dictionary({ { "Type", H::name("Annot") }, { "Subtype", H::name("Widget") }, { "Parent", H::reference(radio) },
                                                                             { "P", H::reference(page) }, { "Rect", rectangle(QRectF(200, 122 + 22 * index, 14, 14)) },
                                                                             { "AS", H::name(index == 0 ? "A" : "Off") }, { "AP", states } }));
        builder.appendTo(page, H::dictionary({ { "Annots", H::array({ H::reference(kid) }) } }));
        kids.appendItem(H::reference(kid));
        ++index;
    }
    builder.mergeTo(radio, H::dictionary({ { "Kids", pdf::PDFObject::createArray(std::move(kids)) } }));
    fields.push_back(radio);

    // Editable combo box and multi-select list box
    addField({ { "FT", H::name("Ch") }, { "T", H::text(QStringLiteral("fruit")) }, { "Ff", H::integer((1 << 17) | (1 << 18)) }, { "DA", da },
               { "Opt", H::array({ H::text(QStringLiteral("Apple")), H::text(QStringLiteral("Banana")), H::text(QStringLiteral("Cherry")) }) },
               { "V", H::text(QStringLiteral("Apple")) } }, QRectF(200, 180, 90, 18));
    addField({ { "FT", H::name("Ch") }, { "T", H::text(QStringLiteral("colors")) }, { "Ff", H::integer(1 << 21) }, { "DA", da },
               { "Opt", H::array({ H::text(QStringLiteral("Red")), H::text(QStringLiteral("Green")), H::text(QStringLiteral("Blue")) }) } }, QRectF(200, 202, 90, 50));

    // Push button resetting the form
    const pdf::PDFObjectReference reset = builder.createActionResetForm();
    addField({ { "FT", H::name("Btn") }, { "T", H::text(QStringLiteral("reset")) }, { "Ff", H::integer(1 << 16) }, { "A", H::reference(reset) },
               { "MK", H::dictionary({ { "CA", H::text(QStringLiteral("Reset")) } }) }, { "DA", da } }, QRectF(200, 260, 60, 20));

    // Unsigned signature field
    addField({ { "FT", H::name("Sig") }, { "T", H::text(QStringLiteral("signature")) } }, QRectF(20, 300, 160, 40));

    const pdf::PDFObjectReference acroForm = builder.createAcroForm(fields);
    builder.mergeTo(acroForm, H::dictionary({ { "CO", H::array({ H::reference(total) }) }, { "DA", da } }));
    builder.setCatalogAcroForm(acroForm);
    return H::write(builder, folder, QStringLiteral("full-form.pdf"));
}

QString AppDocumentSamples::annotatedLarge(const QString& folder, int pageCount, int annotationsPerPage)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, qBound(1, pageCount, 100000));
    const int count = qBound(0, annotationsPerPage, 50);
    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        for (int i = 0; i < count; ++i)
        {
            const QRectF rectangle(20 + 50 * (i % 5), 120 + 60 * (i / 5), 40, 40);
            const QString subject = QStringLiteral("large-%1-%2").arg(pageIndex).arg(i);
            switch (i % 3)
            {
                case 0:
                    builder.createAnnotationSquare(pages[pageIndex], rectangle, 1.0, QColor(), QColor(Qt::red), QStringLiteral("Tester"), subject, subject);
                    break;
                case 1:
                    builder.createAnnotationText(pages[pageIndex], QRectF(rectangle.topLeft(), QSizeF(20, 20)), pdf::TextAnnotationIcon::Note, QStringLiteral("Tester"), subject, subject, false);
                    break;
                default:
                    builder.createAnnotationHighlight(pages[pageIndex], QRectF(28 + 10 * i, 28, 40, 16), QColor(Qt::yellow), QStringLiteral("Tester"), subject, subject);
                    break;
            }
        }
    }
    return H::write(builder, folder, QStringLiteral("annotated-large-%1.pdf").arg(pageCount));
}

QPoint AppDocumentFixture::pageToWidget(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint, bool* ok)
{
    if (ok)
    {
        *ok = false;
    }

    pdf::PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    if (!proxy)
    {
        return QPoint();
    }

    const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const pdf::PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
    if (!item)
    {
        return QPoint();
    }

    if (ok)
    {
        *ok = true;
    }
    return item->pageToDeviceMatrix.map(pagePoint).toPoint();
}

QWidget* AppDocumentFixture::canvasWidget(pdf::PDFAppDocumentView* view)
{
    pdf::PDFWidget* widget = view ? view->getPdfWidget() : nullptr;
    return widget && widget->getDrawWidget() ? widget->getDrawWidget()->getWidget() : nullptr;
}

bool AppDocumentFixture::clickPage(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint, Qt::KeyboardModifiers modifiers, Qt::MouseButton button)
{
    bool ok = false;
    const QPoint position = pageToWidget(view, pageIndex, pagePoint, &ok);
    QPointer<QWidget> target = canvasWidget(view);
    if (!ok || !target)
    {
        return false;
    }

    QMouseEvent press(QEvent::MouseButtonPress, position, target->mapToGlobal(position), button, button, modifiers);
    QCoreApplication::sendEvent(target, &press);
    if (target)
    {
        QMouseEvent release(QEvent::MouseButtonRelease, position, target->mapToGlobal(position), button, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(target, &release);
    }
    return true;
}

bool AppDocumentFixture::dragPage(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& from, const QPointF& to, Qt::KeyboardModifiers modifiers, int steps)
{
    bool okFrom = false;
    bool okTo = false;
    const QPoint start = pageToWidget(view, pageIndex, from, &okFrom);
    const QPoint end = pageToWidget(view, pageIndex, to, &okTo);
    QPointer<QWidget> target = canvasWidget(view);
    if (!okFrom || !okTo || !target)
    {
        return false;
    }

    QMouseEvent press(QEvent::MouseButtonPress, start, target->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(target, &press);
    const int count = qBound(1, steps, 100);
    for (int i = 1; i <= count && target; ++i)
    {
        const QPointF position = QPointF(start) + (QPointF(end) - QPointF(start)) * (qreal(i) / qreal(count));
        QMouseEvent move(QEvent::MouseMove, position, target->mapToGlobal(position), Qt::NoButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(target, &move);
    }
    if (target)
    {
        QMouseEvent release(QEvent::MouseButtonRelease, end, target->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(target, &release);
    }
    return true;
}

int AppDocumentFixture::undoCount(const pdf::PDFAppDocument* document)
{
    const pdf::PDFAppUndoStack* stack = document ? document->getUndoStack() : nullptr;
    return stack ? int(stack->getUndoCount()) : -1;
}

int AppDocumentFixture::s_counter = 0;

AppDocumentFixture::AppDocumentFixture(QSize size)
{
    const QStringList arguments = { QStringLiteral("PDF4QT"), QStringLiteral("--portable") };
    context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));

    pdf::PDFDatabaseOptions options;
    options.memoryName = QStringLiteral("/pdf4qt-document-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
    context->open(options);
    if (context->getNotifier())
    {
        context->getNotifier()->setPollInterval(20);
    }

    window = new pdf::PDFAppMainWindow(context.get());
    window->getNavigator()->setExternalUrlsEnabled(false);
    window->resize(size);
    window->show();
    if (!QTest::qWaitForWindowExposed(window.data()))
    {
        qWarning("The window was not exposed");
    }
    settle();
    closeMessageBoxes();
}

AppDocumentFixture::~AppDocumentFixture()
{
    delete window.data();
    settle();
    context.reset();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
    pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
}

QString AppDocumentFixture::path(const QString& name) const
{
    return QDir(directory.path()).absoluteFilePath(name);
}

pdf::PDFAppDocumentTab* AppDocumentFixture::open(const QString& filePath, int page)
{
    if (!window)
    {
        return nullptr;
    }

    window->openDocuments(QStringList({ filePath }), page);
    settle();

    // The file is read in a worker thread (package A): the tab is returned, when its
    // document has left the state Loading (shown, a password, or failed and closed)
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    QElapsedTimer timer;
    timer.start();
    while (window && timer.elapsed() < 30000)
    {
        pdf::PDFAppDocumentTab* found = nullptr;
        for (pdf::PDFAppDocumentTab* tab : window->getDocumentTabs())
        {
            if (tab->getDocument() && !canonicalPath.isEmpty() && tab->getDocument()->getFilePath() == canonicalPath)
            {
                found = tab;
            }
        }

        if (!found)
        {
            return nullptr;
        }
        if (found->getDocument()->getState() != pdf::PDFAppDocumentState::Loading)
        {
            settle();
            return found;
        }
        QTest::qWait(5);
    }
    return nullptr;
}

pdf::PDFAppDocumentTab* AppDocumentFixture::currentDocumentTab() const
{
    return window ? qobject_cast<pdf::PDFAppDocumentTab*>(window->getCurrentTab()) : nullptr;
}

void AppDocumentFixture::settle()
{
    for (int i = 0; i < 3; ++i)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

int AppDocumentFixture::closeMessageBoxes()
{
    int count = 0;
    if (!window)
    {
        return count;
    }

    for (pdf::PDFMessageBox* messageBox : window->findChildren<pdf::PDFMessageBox*>())
    {
        if (messageBox->isVisible())
        {
            messageBox->close();
            ++count;
        }
    }
    settle();
    return count;
}

}   // namespace pdftest
