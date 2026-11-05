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

#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttool_extracttext.h"
#include "pdfdocumentwriter.h"
#include "pdfoperationcontrol.h"
#include "pdfpagecontentprocessor.h"
#include "pdfsecurityhandler.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QPainter>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtTest>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

/// The tool Extract Text of the core (convert.extract-text) and its exporter: the expected
/// texts come from the documents the test draws, the frames of Figma (339:8765 the menus,
/// 339:9601 the facts) and ISO 32000-2 (14.7 the structure, 14.9.3 alternate descriptions,
/// Table 22 the permission to copy)
class DocumentToolExtractTextTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void plainTextOfPages();
    void pageSelection();
    void pageNumbers();
    void htmlAndXml();
    void markupSanitized();
    void encodings();
    void systemEncodingDecoder();
    void noTextIsNothingToDo();
    void copyPermission();
    void structureOrder();
    void structureItemsOfSelectedPages();
    void structureTitlesOfOldCallers();
    void fetchTextTitlesAndUnmarkedText();
    void untaggedStructureRefused();
    void previewEstimate();
    void pageScanStopsAtText();
    void pagePreview();
    void canceled();
    void parametersOfTheMenus();
};

/// Documents and contexts of the test
class ExtractTextTestHelper
{
public:
    /// A document, whose pages draw the lines of their texts (an empty text is a blank page)
    static pdf::PDFDocument createDocument(const QStringList& pageTexts, int pixelSize = 12)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QString& pageText : pageTexts)
        {
            const QStringList lines = pageText.split(QLatin1Char('\n'));
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 600, qMax(400, 40 + 20 * int(lines.size()))));
            if (pageText.isEmpty())
            {
                continue;
            }

            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            QFont font = painter->font();
            font.setPixelSize(pixelSize);
            painter->setFont(font);
            for (int line = 0; line < lines.size(); ++line)
            {
                painter->drawText(QPointF(30, 40 + 20 * line), lines[line]);
            }
            content.end(painter);
        }
        return read(write(builder.build()));
    }

    static QByteArray write(const pdf::PDFDocument& document)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocumentWriter(nullptr).write(&buffer, &document);
        return buffer.data();
    }

    static pdf::PDFDocument read(const QByteArray& data)
    {
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        return reader.readFromBuffer(data);
    }

    /// A tagged document (ISO 32000-2 14.7): each page is one marked content /P <</MCID 0>> of
    /// an element /P; page 2 has also a /Figure with /Alt and /T and a /P with /T of page 1 is
    /// titled. The texts drawn by the pages are wrapped by BDC / EMC streams.
    static pdf::PDFDocument createTaggedDocument(const QStringList& pageTexts, bool isMarked = true)
    {
        const pdf::PDFDocument drawn = createDocument(pageTexts);
        pdf::PDFDocumentBuilder builder(&drawn);
        const pdf::PDFCatalog* catalog = drawn.getCatalog();

        pdf::PDFArrayBuilder kids;
        pdf::PDFArrayBuilder parentTreeNumbers;
        std::vector<pdf::PDFObjectReference> paragraphs;
        const pdf::PDFObjectReference structureRoot = builder.addObject(pdf::PDFObject());
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            const pdf::PDFObjectReference pageReference = catalog->getPage(i)->getPageReference();
            const pdf::PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(pageReference));
            const pdf::PDFObject contents = pageDictionary->get("Contents");

            const pdf::PDFObjectReference begin = builder.addObject(createStream("/P <</MCID 0>> BDC\n"));
            const pdf::PDFObjectReference end = builder.addObject(createStream("\nEMC\n"));
            pdf::PDFArrayBuilder contentArray;
            contentArray.appendItem(pdf::PDFObject::createReference(begin));
            if (contents.isReference())
            {
                contentArray.appendItem(contents);
            }
            else if (contents.isArray())
            {
                for (size_t item = 0; item < contents.getArray()->getCount(); ++item)
                {
                    contentArray.appendItem(contents.getArray()->getItem(item));
                }
            }
            contentArray.appendItem(pdf::PDFObject::createReference(end));

            pdf::PDFDictionaryBuilder pageChange;
            pageChange.setEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createArray(std::move(contentArray)));
            pageChange.setEntry(pdf::PDFInplaceOrMemoryString("StructParents"), pdf::PDFObject::createInteger(pdf::PDFInteger(i)));
            builder.mergeTo(pageReference, pdf::PDFObject::createDictionary(std::move(pageChange)));

            pdf::PDFDictionaryBuilder paragraph;
            paragraph.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructElem"));
            paragraph.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName("P"));
            paragraph.setEntry(pdf::PDFInplaceOrMemoryString("P"), pdf::PDFObject::createReference(structureRoot));
            paragraph.setEntry(pdf::PDFInplaceOrMemoryString("Pg"), pdf::PDFObject::createReference(pageReference));
            paragraph.setEntry(pdf::PDFInplaceOrMemoryString("K"), pdf::PDFObject::createInteger(0));
            if (i == 0)
            {
                paragraph.setEntry(pdf::PDFInplaceOrMemoryString("T"), pdf::PDFObject::createString("Opening part"));
            }
            const pdf::PDFObjectReference paragraphReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(paragraph)));
            paragraphs.push_back(paragraphReference);
            kids.appendItem(pdf::PDFObject::createReference(paragraphReference));

            pdf::PDFArrayBuilder pageParents;
            pageParents.appendItem(pdf::PDFObject::createReference(paragraphReference));
            parentTreeNumbers.appendItem(pdf::PDFObject::createInteger(pdf::PDFInteger(i)));
            parentTreeNumbers.appendItem(pdf::PDFObject::createArray(std::move(pageParents)));

            if (i == 1)
            {
                // A picture of page 2 with its description (14.9.3)
                pdf::PDFDictionaryBuilder figure;
                figure.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructElem"));
                figure.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName("Figure"));
                figure.setEntry(pdf::PDFInplaceOrMemoryString("P"), pdf::PDFObject::createReference(structureRoot));
                figure.setEntry(pdf::PDFInplaceOrMemoryString("Pg"), pdf::PDFObject::createReference(pageReference));
                figure.setEntry(pdf::PDFInplaceOrMemoryString("Alt"), pdf::PDFObject::createString("Chart of the revenue"));
                kids.appendItem(pdf::PDFObject::createReference(builder.addObject(pdf::PDFObject::createDictionary(std::move(figure)))));
            }
        }

        pdf::PDFDictionaryBuilder parentTree;
        parentTree.setEntry(pdf::PDFInplaceOrMemoryString("Nums"), pdf::PDFObject::createArray(std::move(parentTreeNumbers)));
        pdf::PDFDictionaryBuilder root;
        root.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructTreeRoot"));
        root.setEntry(pdf::PDFInplaceOrMemoryString("K"), pdf::PDFObject::createArray(std::move(kids)));
        root.setEntry(pdf::PDFInplaceOrMemoryString("ParentTree"), pdf::PDFObject::createDictionary(std::move(parentTree)));
        builder.setObject(structureRoot, pdf::PDFObject::createDictionary(std::move(root)));

        pdf::PDFDictionaryBuilder markInfo;
        markInfo.setEntry(pdf::PDFInplaceOrMemoryString("Marked"), pdf::PDFObject::createBool(isMarked));
        pdf::PDFDictionaryBuilder catalogChange;
        catalogChange.setEntry(pdf::PDFInplaceOrMemoryString("StructTreeRoot"), pdf::PDFObject::createReference(structureRoot));
        catalogChange.setEntry(pdf::PDFInplaceOrMemoryString("MarkInfo"), pdf::PDFObject::createDictionary(std::move(markInfo)));
        builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalogChange)));
        return read(write(builder.build()));
    }

    static pdf::PDFObject createStream(const QByteArray& data)
    {
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        QByteArray content = data;
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
    }

    static pdf::PDFDocumentToolContext createContext(const pdf::PDFDocument& document, const QString& filePath = QStringLiteral("C:/Reports/Project_Report.pdf"))
    {
        return pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), filePath, false));
    }

    /// Text of the only output file, decoded by its charset
    static QString outputText(const pdf::PDFDocumentToolResult& result)
    {
        if (result.outputs.size() != 1)
        {
            return QString();
        }
        QStringDecoder decoder = pdf::PDFDocumentTextExporter::createDecoder(result.outputs.front().mimeType);
        return decoder.decode(result.outputs.front().data);
    }

    static QString factValue(const pdf::PDFDocumentToolResult& result, const QString& id)
    {
        for (const pdf::PDFDocumentToolFact& fact : result.facts)
        {
            if (fact.id == id)
            {
                return fact.value;
            }
        }
        return QString();
    }

    /// The tool, its run with checked parameters
    static pdf::PDFDocumentToolResult run(const pdf::PDFDocument& document, const QVariantMap& parameters = QVariantMap())
    {
        pdf::PDFDocumentToolExtractText tool;
        return tool.run(createContext(document), parameters, nullptr, nullptr);
    }
};

/// A control, which is canceled from the start
class ExtractTextCanceledControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

void DocumentToolExtractTextTest::initTestCase()
{
    // The offscreen platform has no fonts: the bundled Inter font of Pdf4QtLibFluent
    const QDir fonts(QStringLiteral(PDF4QT_TEST_FONT_DIRECTORY));
    for (const QString& file : fonts.entryList({ QStringLiteral("*.ttf") }, QDir::Files))
    {
        QVERIFY(QFontDatabase::addApplicationFont(fonts.absoluteFilePath(file)) >= 0);
    }
    QGuiApplication::setFont(QFont(QStringLiteral("Inter")));
}

void DocumentToolExtractTextTest::plainTextOfPages()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("Market Analysis"), QStringLiteral("Regional revenue grew"), QStringLiteral("Customer segments") });
    const pdf::PDFDocumentToolResult result = H::run(document);

    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.outputs.size(), size_t(1));
    QCOMPARE(result.outputs.front().suggestedFileName, QStringLiteral("Project_Report.txt"));
    QCOMPARE(result.outputs.front().mimeType, QStringLiteral("text/plain"));

    // A plain text (339:8711): the texts in the order of the pages, each paragraph followed by an
    // empty line, no indentation of a structure, no empty first line
    const QString text = H::outputText(result);
    QCOMPARE(text, QStringLiteral("Market Analysis\n\nRegional revenue grew\n\nCustomer segments\n\n"));

    // 339:9601: Pages, Words, Size
    QCOMPARE(H::factValue(result, QStringLiteral("pages")), QStringLiteral("3 with text · 0 without"));
    QCOMPARE(H::factValue(result, QStringLiteral("words")), QStringLiteral("7"));
    QCOMPARE(result.data.value(QStringLiteral("size")).toInteger(), qint64(result.outputs.front().data.size()));
    QCOMPARE(result.data.value(QStringLiteral("order")).toString(), QStringLiteral("layout"));
    QVERIFY(result.documents.empty());
}

void DocumentToolExtractTextTest::pageSelection()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("first page"), QStringLiteral("second page"), QStringLiteral("third page") });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), QStringLiteral("2-3"));
    const pdf::PDFDocumentToolResult result = H::run(document, parameters);
    QCOMPARE(H::outputText(result), QStringLiteral("second page\n\nthird page\n\n"));
    QCOMPARE(H::factValue(result, QStringLiteral("pages")), QStringLiteral("2 with text · 0 without"));

    // A range outside of the document is refused before the run
    parameters.insert(QStringLiteral("pages"), QStringLiteral("7"));
    const pdf::PDFDocumentToolResult refused = H::run(document, parameters);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.outputs.empty());
}

void DocumentToolExtractTextTest::pageNumbers()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("alpha"), QStringLiteral("beta") });

    // 339:9499: "A line “Page N” starts every page"
    QVariantMap parameters;
    parameters.insert(QStringLiteral("includePageNumbers"), true);
    parameters.insert(QStringLiteral("order"), QStringLiteral("layout"));
    const QString text = H::outputText(H::run(document, parameters));
    QCOMPARE(text, QStringLiteral("Page 1\n\nalpha\n\nPage 2\n\nbeta\n\n"));
}

void DocumentToolExtractTextTest::htmlAndXml()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("Price <net> & tax") });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("format"), QStringLiteral("html"));
    const pdf::PDFDocumentToolResult html = H::run(document, parameters);
    QCOMPARE(html.outputs.front().suggestedFileName, QStringLiteral("Project_Report.html"));
    QCOMPARE(html.outputs.front().mimeType, QStringLiteral("text/html"));

    // A browser decodes it by its meta element; the text is escaped, so the file is well formed
    const QString htmlText = H::outputText(html);
    QVERIFY(htmlText.contains(QStringLiteral("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>")));
    QVERIFY(htmlText.contains(QStringLiteral("<p>Price &lt;net&gt; &amp; tax</p>")));
    QVERIFY(htmlText.contains(QStringLiteral("<title>Project_Report.pdf - Processed by")));
    QXmlStreamReader htmlReader(html.outputs.front().data);
    QStringList paragraphs;
    while (!htmlReader.atEnd())
    {
        if (htmlReader.readNext() == QXmlStreamReader::StartElement && htmlReader.name() == QLatin1String("p"))
        {
            paragraphs << htmlReader.readElementText(QXmlStreamReader::IncludeChildElements);
        }
    }
    QVERIFY2(!htmlReader.hasError(), qPrintable(htmlReader.errorString()));
    QVERIFY(paragraphs.contains(QStringLiteral("Price <net> & tax")));

    parameters.insert(QStringLiteral("format"), QStringLiteral("xml"));
    const pdf::PDFDocumentToolResult xml = H::run(document, parameters);
    QCOMPARE(xml.outputs.front().suggestedFileName, QStringLiteral("Project_Report.xml"));
    QCOMPARE(xml.outputs.front().mimeType, QStringLiteral("text/xml"));
    QXmlStreamReader xmlReader(xml.outputs.front().data);
    QStringList texts;
    while (!xmlReader.atEnd())
    {
        if (xmlReader.readNext() == QXmlStreamReader::StartElement && xmlReader.name() == QLatin1String("text"))
        {
            texts << xmlReader.readElementText();
        }
    }
    QVERIFY2(!xmlReader.hasError(), qPrintable(xmlReader.errorString()));
    QCOMPARE(texts, QStringList({ QStringLiteral("Price <net> & tax") }));
}

void DocumentToolExtractTextTest::markupSanitized()
{
    // A document can hold any character: the XML of the tool stays well formed (XML 1.0, 2.2)
    pdf::PDFDocumentTextFlow flow;
    flow.addItem({ QRectF(), 0, QStringLiteral("Page 1"), pdf::PDFDocumentTextFlow::PageStart, {} });
    flow.addItem({ QRectF(), 0, QStringLiteral("bell\x07 and ") + QChar(0xD800) + QStringLiteral(" lone"), pdf::PDFDocumentTextFlow::Text, {} });
    flow.addItem({ QRectF(), 0, QString(), pdf::PDFDocumentTextFlow::PageEnd, {} });

    pdf::PDFDocumentTextExporter::Options options;
    options.sanitizeMarkup = true;
    pdf::PDFOutputFormatter formatter(pdf::PDFOutputFormatter::Style::Xml);
    pdf::PDFDocumentTextExporter::write(flow, options, formatter);
    const pdf::PDFDocumentTextExporter::EncodedText encoded = pdf::PDFDocumentTextExporter::encode(formatter.getString(), pdf::PDFOutputFormatter::Style::Xml, pdf::PDFDocumentTextExporter::Encoding::Utf8);

    QXmlStreamReader reader(encoded.data);
    QString text;
    while (!reader.atEnd())
    {
        if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == QLatin1String("text"))
        {
            text = reader.readElementText();
        }
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    QCOMPARE(text, QStringLiteral("bell") + QChar(QChar::ReplacementCharacter) + QStringLiteral(" and ") + QChar(QChar::ReplacementCharacter) + QStringLiteral(" lone"));
}

void DocumentToolExtractTextTest::encodings()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("café Kč") });

    // Latin-1 (ISO-8859-1): é is 0xE9, č is not in it - a question mark, counted
    QVariantMap parameters;
    parameters.insert(QStringLiteral("encoding"), QStringLiteral("latin-1"));
    const pdf::PDFDocumentToolResult latin = H::run(document, parameters);
    QCOMPARE(latin.outputs.front().mimeType, QStringLiteral("text/plain; charset=ISO-8859-1"));
    QCOMPARE(latin.outputs.front().data, QByteArray("caf\xE9 K?\n\n"));
    QCOMPARE(latin.data.value(QStringLiteral("lostCharacters")).toInteger(), qint64(1));
    QCOMPARE(H::factValue(latin, QStringLiteral("encoding")), QStringLiteral("1 character replaced by ?"));

    // UTF-8 keeps everything and says no charset
    parameters.insert(QStringLiteral("encoding"), QStringLiteral("utf-8"));
    const pdf::PDFDocumentToolResult utf8 = H::run(document, parameters);
    QCOMPARE(utf8.outputs.front().data, QStringLiteral("café Kč\n\n").toUtf8());
    QCOMPARE(utf8.data.value(QStringLiteral("lostCharacters")).toInteger(), qint64(0));
    QVERIFY(H::factValue(utf8, QStringLiteral("encoding")).isEmpty());

    // An XML file in Latin-1 declares it, a reader gets the characters back
    parameters.insert(QStringLiteral("encoding"), QStringLiteral("latin-1"));
    parameters.insert(QStringLiteral("format"), QStringLiteral("xml"));
    const pdf::PDFDocumentToolResult xml = H::run(document, parameters);
    QVERIFY(xml.outputs.front().data.startsWith("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>"));
    QXmlStreamReader reader(xml.outputs.front().data);
    QString text;
    while (!reader.atEnd())
    {
        if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == QLatin1String("text"))
        {
            text = reader.readElementText();
        }
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    QCOMPARE(text, QStringLiteral("café K?"));
}

void DocumentToolExtractTextTest::systemEncodingDecoder()
{
    // The encoding of the system: Windows writes its ANSI code page (the charset says it,
    // "windows-1250"); a Qt without ICU decodes the code page of the system only as the
    // encoding System - createDecoder gives it for that name, also to return_text of MCP
    const QString charset = pdf::PDFDocumentTextExporter::getSystemCharset();
#ifdef Q_OS_WIN
    const UINT codePage = GetACP();
    QCOMPARE(charset, codePage == CP_UTF8 ? QStringLiteral("UTF-8") : QStringLiteral("windows-%1").arg(codePage));
#else
    QCOMPARE(charset, QStringLiteral("UTF-8"));
#endif

    const QString text = QStringLiteral("Příliš žluťoučký kůň café");
    const pdf::PDFDocumentTextExporter::EncodedText encoded = pdf::PDFDocumentTextExporter::encode(text, pdf::PDFOutputFormatter::Style::Text, pdf::PDFDocumentTextExporter::Encoding::System);
    QCOMPARE(encoded.mimeType, charset == QLatin1String("UTF-8") ? QStringLiteral("text/plain") : QStringLiteral("text/plain; charset=%1").arg(charset));

    QStringDecoder decoder = pdf::PDFDocumentTextExporter::createDecoder(encoded.mimeType);
    QVERIFY(decoder.isValid());

    // Said for the report of the platform: a Qt without ICU knows the code page by its name or not
    qInfo("System charset %s, known by its name: %s", qPrintable(charset), QStringDecoder(charset.toLatin1().constData()).isValid() ? "yes" : "no");
    const QString decoded = decoder.decode(encoded.data);

    // Every character, which the code page has, comes back; the others are the counted '?'
    QCOMPARE(decoded.size(), text.size());
    qint64 differences = 0;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (decoded.at(i) != text.at(i))
        {
            QCOMPARE(decoded.at(i), QLatin1Char('?'));
            ++differences;
        }
    }
    QCOMPARE(differences, encoded.lostCharacters);
    QVERIFY(decoded.startsWith(QLatin1Char('P')));

    // A charset Qt does not know is no text (return_text says so)
    QVERIFY(!pdf::PDFDocumentTextExporter::createDecoder(QStringLiteral("text/plain; charset=x-unknown-charset")).isValid());
    QVERIFY(pdf::PDFDocumentTextExporter::createDecoder(QStringLiteral("text/plain; charset=ISO-8859-1")).isValid());
}

void DocumentToolExtractTextTest::noTextIsNothingToDo()
{
    using H = ExtractTextTestHelper;

    // Never an empty file: the scanned pages have no text, also with the page numbers on
    const pdf::PDFDocument document = H::createDocument({ QString(), QString() });
    QVariantMap parameters;
    parameters.insert(QStringLiteral("includePageNumbers"), true);
    const pdf::PDFDocumentToolResult result = H::run(document, parameters);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.isUse());
    QVERIFY(result.outputs.empty());
    QCOMPARE(H::factValue(result, QStringLiteral("pages")), QStringLiteral("0 with text · 2 without"));
}

void DocumentToolExtractTextTest::copyPermission()
{
    using H = ExtractTextTestHelper;

    // ISO 32000-2 Table 22, bit 5: copy or otherwise extract text - the tool needs it
    const pdf::PDFDocument plain = H::createDocument({ QStringLiteral("secret words") });
    pdf::PDFDocumentBuilder builder(&plain);
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
    settings.ownerPassword = QStringLiteral("owner-secret");
    settings.permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::CopyContent);
    builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    const pdf::PDFDocument restricted = H::read(H::write(builder.build()));
    QVERIFY(!restricted.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent));

    const pdf::PDFDocumentToolResult refused = H::run(restricted);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.outputs.empty());

    pdf::PDFDocumentToolExtractText tool;
    QCOMPARE(tool.runPreview(H::createContext(restricted), QVariantMap(), nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);

    // The same document without the restriction gives its text
    QCOMPARE(H::outputText(H::run(plain)), QStringLiteral("secret words\n\n"));
}

void DocumentToolExtractTextTest::structureOrder()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createTaggedDocument({ QStringLiteral("first tagged"), QStringLiteral("second tagged") });

    // Auto follows the tags of a marked document (339:9468)
    const pdf::PDFDocumentToolResult automatic = H::run(document);
    QCOMPARE(automatic.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(automatic.data.value(QStringLiteral("order")).toString(), QStringLiteral("structure"));

    // The default includes the alternative descriptions (339:9516 checked): the text of the
    // picture is in the file; titles are off
    // (the structure reads each run of glyphs drawn apart as its own text: white space is compared as one space)
    const QString text = H::outputText(automatic).simplified();
    QVERIFY2(text.contains(QStringLiteral("first tagged")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("second tagged")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Chart of the revenue")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("Opening part")), qPrintable(text));

    // Structure titles (/T of the element, 14.7.2 Table 355) and no descriptions
    QVariantMap parameters;
    parameters.insert(QStringLiteral("includeStructureTitles"), true);
    parameters.insert(QStringLiteral("includeAlternativeDescriptions"), false);
    const QString titled = H::outputText(H::run(document, parameters)).simplified();
    QVERIFY2(titled.contains(QStringLiteral("Opening part")), qPrintable(titled));
    QVERIFY2(!titled.contains(QStringLiteral("Chart of the revenue")), qPrintable(titled));

    // A document, which is not marked (MarkInfo /Marked false), is read by the layout
    const pdf::PDFDocument unmarked = H::createTaggedDocument({ QStringLiteral("plain") }, false);
    QCOMPARE(H::run(unmarked).data.value(QStringLiteral("order")).toString(), QStringLiteral("layout"));
}

void DocumentToolExtractTextTest::structureItemsOfSelectedPages()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createTaggedDocument({ QStringLiteral("first tagged"), QStringLiteral("second tagged") });

    // The description of the picture of page 2 is no text of page 1
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), QStringLiteral("1"));
    parameters.insert(QStringLiteral("includeStructureTitles"), true);
    const QString first = H::outputText(H::run(document, parameters)).simplified();
    QVERIFY2(first.contains(QStringLiteral("first tagged")), qPrintable(first));
    QVERIFY2(first.contains(QStringLiteral("Opening part")), qPrintable(first));
    QVERIFY2(!first.contains(QStringLiteral("second tagged")), qPrintable(first));
    QVERIFY2(!first.contains(QStringLiteral("Chart of the revenue")), qPrintable(first));

    parameters.insert(QStringLiteral("pages"), QStringLiteral("2"));
    const QString second = H::outputText(H::run(document, parameters)).simplified();
    QVERIFY2(second.contains(QStringLiteral("Chart of the revenue")), qPrintable(second));
    QVERIFY2(!second.contains(QStringLiteral("Opening part")), qPrintable(second));
}

void DocumentToolExtractTextTest::structureTitlesOfOldCallers()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createTaggedDocument({ QStringLiteral("first tagged"), QStringLiteral("second tagged") });

    // fetch-text and the audio book keep their output: their title item stays empty, the
    // description of page 2 stays in the text of page 1
    pdf::PDFDocumentTextFlowFactory factory;
    const pdf::PDFDocumentTextFlow flow = factory.create(&document, { 0 }, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure);
    bool hasTitleItem = false;
    bool hasDescription = false;
    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.isTitle())
        {
            hasTitleItem = true;
            QVERIFY(item.text.isEmpty());
        }
        hasDescription = hasDescription || item.text == QLatin1String("Chart of the revenue");
    }
    QVERIFY(hasTitleItem);
    QVERIFY(hasDescription);
}

void DocumentToolExtractTextTest::fetchTextTitlesAndUnmarkedText()
{
    using H = ExtractTextTestHelper;

    // PdfTool fetch-text (owner 2026-11-05): --text-show-struct-title prints the title (/T, ISO
    // 32000-2 14.7.2 Table 355) - the factory of fetch-text with setStructureTitleText, written
    // by the exporter as fetch-text writes it; no page filter of the export
    const pdf::PDFDocument tagged = H::createTaggedDocument({ QStringLiteral("first tagged"), QStringLiteral("second tagged") });
    pdf::PDFDocumentTextFlowFactory titleFactory;
    titleFactory.setStructureTitleText(true);
    titleFactory.setUnmarkedText(true);
    const pdf::PDFDocumentTextFlow titled = titleFactory.create(&tagged, { 0 }, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure);
    pdf::PDFDocumentTextExporter::Options options;
    options.showStructureTitles = true;
    options.showStructureAlternativeDescription = true;
    pdf::PDFOutputFormatter formatter(pdf::PDFOutputFormatter::Style::Text);
    pdf::PDFDocumentTextExporter::write(titled, options, formatter);
    const QString output = formatter.getString();
    QVERIFY2(output.contains(QStringLiteral("Opening part")), qPrintable(output));
    QVERIFY2(output.contains(QStringLiteral("Chart of the revenue")), qPrintable(output));

    // The order of the content stream of an untagged page: its text, which is in no marked content
    const pdf::PDFDocument untagged = H::createDocument({ QStringLiteral("plain words") });
    pdf::PDFDocumentTextFlowFactory contentFactory;
    contentFactory.setUnmarkedText(true);
    QCOMPARE(contentFactory.create(&untagged, { 0 }, pdf::PDFDocumentTextFlowFactory::Algorithm::Content).getText().simplified(), QStringLiteral("Page 1 plain words"));

    // Without the switch (the audio book) only the page marker, as before
    pdf::PDFDocumentTextFlowFactory oldFactory;
    QCOMPARE(oldFactory.create(&untagged, { 0 }, pdf::PDFDocumentTextFlowFactory::Algorithm::Content).getText().simplified(), QStringLiteral("Page 1"));
}

void DocumentToolExtractTextTest::untaggedStructureRefused()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("no tags") });

    // 339:9491: Structure "needs a tagged document" - an error, which says what to choose
    QVariantMap parameters;
    parameters.insert(QStringLiteral("order"), QStringLiteral("structure"));
    const pdf::PDFDocumentToolResult refused = H::run(document, parameters);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.message.contains(QStringLiteral("not tagged")));

    parameters.insert(QStringLiteral("order"), QStringLiteral("content-stream"));
    const pdf::PDFDocumentToolResult content = H::run(document, parameters);
    QCOMPARE(content.message, QString());
    QCOMPARE(content.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(H::outputText(content).simplified(), QStringLiteral("no tags"));
}

void DocumentToolExtractTextTest::previewEstimate()
{
    using H = ExtractTextTestHelper;

    // 60 pages of 4 words and 12 blank pages (339:8759 "12 pages have no text")
    QStringList pages;
    for (int i = 0; i < 72; ++i)
    {
        pages << (i % 6 == 5 ? QString() : QStringLiteral("one two three four"));
    }
    const pdf::PDFDocument document = H::createDocument(pages);

    pdf::PDFDocumentToolExtractText tool;
    const pdf::PDFDocumentToolResult preview = tool.runPreview(H::createContext(document), QVariantMap(), nullptr);
    QCOMPARE(preview.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(preview.data.value(QStringLiteral("pageCount")).toInteger(), qint64(72));
    QCOMPARE(preview.data.value(QStringLiteral("pagesWithText")).toInteger(), qint64(60));
    QCOMPARE(preview.data.value(QStringLiteral("pagesWithoutText")).toInteger(), qint64(12));
    QCOMPARE(preview.data.value(QStringLiteral("tagged")).toBool(), false);

    // A sample of PREVIEW_SAMPLE_PAGES pages, scaled: the pages are alike, so the estimate is exact
    QVERIFY(preview.data.value(QStringLiteral("isEstimate")).toBool());
    QCOMPARE(preview.data.value(QStringLiteral("words")).toInteger(), qint64(240));
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document), QVariantMap(), nullptr, nullptr);
    QCOMPARE(result.data.value(QStringLiteral("words")).toInteger(), qint64(240));
    QCOMPARE(preview.data.value(QStringLiteral("characters")).toInteger(), result.data.value(QStringLiteral("characters")).toInteger());
    QVERIFY(qAbs(preview.data.value(QStringLiteral("size")).toInteger() - result.data.value(QStringLiteral("size")).toInteger()) <= 4);

    // The preview has no file
    QVERIFY(preview.outputs.empty());
}

void DocumentToolExtractTextTest::pageScanStopsAtText()
{
    using H = ExtractTextTestHelper;

    // The pages with text are found also among pages of graphics only and blank pages
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference graphics = builder.appendPage(QRectF(0, 0, 200, 200));
    {
        pdf::PDFPageContentStreamBuilder content(&builder);
        QPainter* painter = content.begin(graphics);
        painter->fillRect(QRectF(10, 10, 100, 100), Qt::red);
        content.end(painter);
    }
    builder.appendPage(QRectF(0, 0, 200, 200));
    const pdf::PDFDocument drawn = H::read(H::write(builder.build()));
    const pdf::PDFDocument text = H::createDocument({ QStringLiteral("x"), QStringLiteral(" "), QStringLiteral("y") });

    QCOMPARE(pdf::PDFDocumentTextExporter::findPagesWithText(&drawn, { 0, 1 }, nullptr, nullptr), std::vector<pdf::PDFInteger>());
    QCOMPARE(pdf::PDFDocumentTextExporter::findPagesWithText(&text, { 0, 1, 2 }, nullptr, nullptr), std::vector<pdf::PDFInteger>({ 0, 2 }));
    QCOMPARE(pdf::PDFDocumentTextExporter::findPagesWithText(&text, { 2 }, nullptr, nullptr), std::vector<pdf::PDFInteger>({ 2 }));
}

void DocumentToolExtractTextTest::pagePreview()
{
    using H = ExtractTextTestHelper;

    QString longText;
    for (int line = 0; line < 600; ++line)
    {
        longText += QStringLiteral("line %1 of a long page with many words in it\n").arg(line);
    }
    longText.chop(1);
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("first page"), QStringLiteral("second page"), longText, QString() });
    pdf::PDFDocumentToolExtractTextPage tool;

    // The pane EXTRACTED TEXT: the text of the shown page only, by the selection or not
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), QStringLiteral("1"));
    parameters.insert(QStringLiteral("previewPage"), 1);
    const pdf::PDFDocumentToolResult second = tool.runPreview(H::createContext(document), parameters, nullptr);
    QCOMPARE(second.status, pdf::PDFDocumentToolResultStatus::Success);
    QCOMPARE(second.data.value(QStringLiteral("text")).toString(), QStringLiteral("second page\n\n"));
    QVERIFY(second.data.value(QStringLiteral("hasText")).toBool());
    QVERIFY(!second.data.value(QStringLiteral("isTruncated")).toBool());

    // Bounded: a long page gives its beginning
    parameters.insert(QStringLiteral("previewPage"), 2);
    const pdf::PDFDocumentToolResult longPage = tool.runPreview(H::createContext(document), parameters, nullptr);
    QVERIFY(longPage.data.value(QStringLiteral("isTruncated")).toBool());
    QCOMPARE(longPage.data.value(QStringLiteral("text")).toString().size(), qsizetype(pdf::PDFDocumentToolExtractTextPage::PREVIEW_TEXT_LIMIT));
    QVERIFY(longPage.data.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("line 0 of a long page")));

    parameters.insert(QStringLiteral("previewPage"), 3);
    QVERIFY(!tool.runPreview(H::createContext(document), parameters, nullptr).data.value(QStringLiteral("hasText")).toBool());

    // A page, which does not exist
    parameters.insert(QStringLiteral("previewPage"), 9);
    QCOMPARE(tool.runPreview(H::createContext(document), parameters, nullptr).status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolExtractTextTest::canceled()
{
    using H = ExtractTextTestHelper;
    const pdf::PDFDocument document = H::createDocument({ QStringLiteral("some text") });
    pdf::PDFDocumentToolExtractText tool;
    ExtractTextCanceledControl control;
    const pdf::PDFDocumentToolResult result = tool.run(H::createContext(document), QVariantMap(), &control, nullptr);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.outputs.empty());
    QCOMPARE(tool.runPreview(H::createContext(document), QVariantMap(), &control).status, pdf::PDFDocumentToolResultStatus::Stopped);
}

void DocumentToolExtractTextTest::parametersOfTheMenus()
{
    // 339:8765: four orders, seven items to include (only the descriptions of the pictures
    // checked), three formats, three encodings; chapter 6 of the plan names them for MCP
    pdf::PDFDocumentToolExtractText tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QStringList names;
    for (const pdf::PDFDocumentToolParameterDefinition& definition : definitions)
    {
        names << definition.getName();
    }
    QCOMPARE(names, QStringList({ "pages", "order", "includePageNumbers", "includeStructureTitles", "includeLanguages", "includeAlternativeDescriptions",
                                  "includeExpandedForms", "includeActualText", "includePhonemes", "format", "encoding" }));

    const pdf::PDFDocumentToolParameterDefinition* order = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("order"));
    QStringList orderIds;
    QStringList orderTitles;
    for (const pdf::PDFDocumentToolParameterOption& option : order->getOptions())
    {
        orderIds << option.id;
        orderTitles << option.getTitle();
    }
    QCOMPARE(orderIds, QStringList({ "auto", "layout", "content-stream", "structure" }));
    QCOMPARE(orderTitles, QStringList({ "Auto", "Layout", "Content stream", "Structure" }));

    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, QVariantMap(), &normalized).empty());
    QCOMPARE(normalized.value(QStringLiteral("order")).toString(), QStringLiteral("auto"));
    QCOMPARE(normalized.value(QStringLiteral("format")).toString(), QStringLiteral("text"));
    QCOMPARE(normalized.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf-8"));
    int included = 0;
    for (const char* parameter : pdf::PDFDocumentToolExtractText::getIncludeParameters())
    {
        included += normalized.value(QString::fromLatin1(parameter)).toBool() ? 1 : 0;
    }
    QCOMPARE(included, 1);
    QVERIFY(normalized.value(QStringLiteral("includeAlternativeDescriptions")).toBool());

    // The pages of one document are no last value
    QVERIFY(!pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("pages"))->isStored());
}

QTEST_MAIN(DocumentToolExtractTextTest)

#include "tst_documenttoolextracttexttest.moc"
