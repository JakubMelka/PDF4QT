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

// The text stream of the documents (PDFDocumentTextFlow, step 4 of phase 2, package P4; plan 8 row
// Textový proud, R4-13): B73, B74, B75, the reading stream of the structure (ActualText, E, Alt,
// Lang, artifacts - ISO 32000-2 14.9.2 - 14.9.5, 14.8.2.2), a cycle and a shared node in the
// structure tree.

#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttextflow.h"
#include "pdfdocumenttextfloweditormodel.h"
#include "pdfdocumenttool_audiobook.h"
#include "pdfdocumentwriter.h"

#include <QBuffer>
#include <QFontDatabase>
#include <QPainter>
#include <QSignalSpy>
#include <QtTest>

#include <functional>

class DocumentTextFlowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void editedFlowTakesItsOriginalItem();
    void movedItemsKeepTheirPages();
    void newFlowReplacesTheItems();
    void editorModelSignals();
    void actualTextReplacesTheText();
    void expandedFormAndAlternativeDescription();
    void languageOfTheItems();
    void oldStreamUnchanged();
    void artifactsOfLayout();
    void artifactsOfContent();
    void cycleAndSharedNode();
    void readingStreamOfSomePages();
    void languageOfMarkedContent();
    void languageOfMarkedContentInStructure();
    void pageMarksOfTheStructure();
    void defaultVoiceKeepsItsLanguage();
    void editorCopiesShareTheOriginal();
};

/// Documents of the test: one page, each part of the text drawn in its own content stream, which
/// marked content operators may wrap; a structure tree is added by the caller
class TextFlowTestDocument
{
public:
    struct Part
    {
        QString text;
        qreal y = 0.0;
        QByteArray before;  ///< Operators before the stream of the part ("/P <</MCID 0>> BDC")
        QByteArray after;   ///< Operators after it ("EMC")
    };

    using StructureFunction = std::function<void(pdf::PDFDocumentBuilder&, pdf::PDFObjectReference)>;

    static pdf::PDFDocument create(const std::vector<Part>& parts, const StructureFunction& addStructure)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 600, 800));

        // Each part is a content stream of its own (the order of /Contents is the order of the parts)
        for (const Part& part : parts)
        {
            // Each part comes after the parts before it (the first one after the empty page)
            pdf::PDFPageContentStreamBuilder content(&builder, pdf::PDFContentStreamBuilder::CoordinateSystem::Qt, pdf::PDFPageContentStreamBuilder::Mode::PlaceAfter);
            QPainter* painter = content.begin(page);
            QFont font = painter->font();
            font.setPixelSize(12);
            painter->setFont(font);
            painter->drawText(QPointF(40, part.y), part.text);
            content.end(painter);
        }

        // The markers around the streams of the parts
        const pdf::PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(page));
        const pdf::PDFObject contents = pageDictionary->get("Contents");
        std::vector<pdf::PDFObject> streams;
        if (contents.isArray())
        {
            for (size_t i = 0; i < contents.getArray()->getCount(); ++i)
            {
                streams.push_back(contents.getArray()->getItem(i));
            }
        }
        else
        {
            streams.push_back(contents);
        }

        pdf::PDFArrayBuilder contentArray;
        for (size_t i = 0; i < streams.size(); ++i)
        {
            const Part* part = i < parts.size() ? &parts[i] : nullptr;
            if (part && !part->before.isEmpty())
            {
                contentArray.appendItem(pdf::PDFObject::createReference(builder.addObject(createStream(part->before + "\n"))));
            }
            contentArray.appendItem(streams[i]);
            if (part && !part->after.isEmpty())
            {
                contentArray.appendItem(pdf::PDFObject::createReference(builder.addObject(createStream("\n" + part->after + "\n"))));
            }
        }
        pdf::PDFDictionaryBuilder pageChange;
        pageChange.setEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createArray(std::move(contentArray)));
        pageChange.setEntry(pdf::PDFInplaceOrMemoryString("StructParents"), pdf::PDFObject::createInteger(0));
        builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageChange)));

        if (addStructure)
        {
            addStructure(builder, page);
        }

        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        pdf::PDFDocument built = builder.build();
        pdf::PDFDocumentWriter(nullptr).write(&buffer, &built);
        pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
        return reader.readFromBuffer(buffer.data());
    }

    static pdf::PDFObject createStream(const QByteArray& data)
    {
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        QByteArray content = data;
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
    }

    /// A structure element (ISO 32000-2 14.7.2, Table 355); \p entries are further keys
    static pdf::PDFObjectReference addElement(pdf::PDFDocumentBuilder& builder, const QByteArray& type, pdf::PDFObjectReference page, pdf::PDFObject kids, const std::vector<std::pair<QByteArray, pdf::PDFObject>>& entries = {})
    {
        pdf::PDFDictionaryBuilder element;
        element.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructElem"));
        element.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName(type));
        if (page.isValid())
        {
            element.setEntry(pdf::PDFInplaceOrMemoryString("Pg"), pdf::PDFObject::createReference(page));
        }
        if (!kids.isNull())
        {
            element.setEntry(pdf::PDFInplaceOrMemoryString("K"), kids);
        }
        for (const auto& entry : entries)
        {
            element.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return builder.addObject(pdf::PDFObject::createDictionary(std::move(element)));
    }

    static pdf::PDFObject createReferences(const std::vector<pdf::PDFObjectReference>& references)
    {
        pdf::PDFArrayBuilder array;
        for (const pdf::PDFObjectReference& reference : references)
        {
            array.appendItem(pdf::PDFObject::createReference(reference));
        }
        return pdf::PDFObject::createArray(std::move(array));
    }

    /// The root of the tree, the parent tree of the MCIDs of the page (14.7.5.4) and the catalog
    /// (/MarkInfo /Marked true, /Lang)
    static void finishTree(pdf::PDFDocumentBuilder& builder, const std::vector<pdf::PDFObjectReference>& rootKids, const std::vector<pdf::PDFObjectReference>& elementOfMcid, const QByteArray& language)
    {
        pdf::PDFArrayBuilder numbers;
        numbers.appendItem(pdf::PDFObject::createInteger(0));
        numbers.appendItem(createReferences(elementOfMcid));
        pdf::PDFDictionaryBuilder parentTree;
        parentTree.setEntry(pdf::PDFInplaceOrMemoryString("Nums"), pdf::PDFObject::createArray(std::move(numbers)));

        pdf::PDFDictionaryBuilder root;
        root.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructTreeRoot"));
        root.setEntry(pdf::PDFInplaceOrMemoryString("K"), createReferences(rootKids));
        root.setEntry(pdf::PDFInplaceOrMemoryString("ParentTree"), pdf::PDFObject::createDictionary(std::move(parentTree)));
        const pdf::PDFObjectReference rootReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(root)));

        pdf::PDFDictionaryBuilder markInfo;
        markInfo.setEntry(pdf::PDFInplaceOrMemoryString("Marked"), pdf::PDFObject::createBool(true));
        pdf::PDFDictionaryBuilder catalogChange;
        catalogChange.setEntry(pdf::PDFInplaceOrMemoryString("StructTreeRoot"), pdf::PDFObject::createReference(rootReference));
        catalogChange.setEntry(pdf::PDFInplaceOrMemoryString("MarkInfo"), pdf::PDFObject::createDictionary(std::move(markInfo)));
        if (!language.isEmpty())
        {
            catalogChange.setEntry(pdf::PDFInplaceOrMemoryString("Lang"), pdf::PDFObject::createString(language));
        }
        builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalogChange)));
    }

    /// The reading stream of the document by the structure
    static pdf::PDFDocumentTextFlow createReadingStream(const pdf::PDFDocument& document, pdf::PDFDocumentTextFlowFactory::Algorithm algorithm, bool isReadingStream = true)
    {
        pdf::PDFDocumentTextFlowFactory factory;
        factory.setCalculateBoundingBoxes(true);
        factory.setReadingStream(isReadingStream);
        return factory.create(&document, algorithm);
    }

    /// The items with a text (trimmed and simplified texts)
    static std::vector<pdf::PDFDocumentTextFlow::Item> getTextItems(const pdf::PDFDocumentTextFlow& flow)
    {
        std::vector<pdf::PDFDocumentTextFlow::Item> items;
        for (pdf::PDFDocumentTextFlow::Item item : flow.getItems())
        {
            item.text = item.text.simplified();
            if (!item.text.isEmpty())
            {
                items.push_back(std::move(item));
            }
        }
        return items;
    }

    static const pdf::PDFDocumentTextFlow::Item* find(const std::vector<pdf::PDFDocumentTextFlow::Item>& items, const QString& text)
    {
        for (const pdf::PDFDocumentTextFlow::Item& item : items)
        {
            if (item.text.contains(text))
            {
                return &item;
            }
        }
        return nullptr;
    }

    /// The flow of the editor tests: a white space item (left out of the editor) and two texts
    static pdf::PDFDocumentTextFlow createEditorFlow()
    {
        pdf::PDFDocumentTextFlow::Items items;
        items.push_back({ QRectF(1, 1, 1, 1), 9, QStringLiteral("   "), pdf::PDFDocumentTextFlow::Text, {} });
        items.push_back({ QRectF(10, 20, 30, 40), 1, QStringLiteral("Alpha"), pdf::PDFDocumentTextFlow::Text, {} });
        items.push_back({ QRectF(50, 60, 70, 80), 2, QStringLiteral("Beta"), pdf::PDFDocumentTextFlow::Text, {} });
        return pdf::PDFDocumentTextFlow(std::move(items));
    }
};

void DocumentTextFlowTest::initTestCase()
{
    // The offscreen platform has no fonts: the bundled Inter font of Pdf4QtLibFluent
    const QDir fonts(QStringLiteral(PDF4QT_TEST_FONT_DIRECTORY));
    for (const QString& file : fonts.entryList({ QStringLiteral("*.ttf") }, QDir::Files))
    {
        QVERIFY(QFontDatabase::addApplicationFont(fonts.absoluteFilePath(file)) >= 0);
    }
    QGuiApplication::setFont(QFont(QStringLiteral("Inter")));
}

void DocumentTextFlowTest::editedFlowTakesItsOriginalItem()
{
    // B73: the edited flow leaves the white space item out; an edited item is the original item
    // of its originalIndex (its page, rectangle and flags), not the item of its own index
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(TextFlowTestDocument::createEditorFlow());
    QCOMPARE(editor.getItemCount(), size_t(2));

    editor.setText(QStringLiteral("Alpha!"), 0);
    QVERIFY(editor.isModified(0));

    // The text of the original item of its own index is no modification
    editor.setText(QStringLiteral("Beta"), 1);
    QVERIFY(!editor.isModified(1));

    const pdf::PDFDocumentTextFlow flow = editor.createEditedTextFlow();
    QCOMPARE(flow.getSize(), size_t(2));
    QCOMPARE(flow.getItem(0)->text, QStringLiteral("Alpha!"));
    QCOMPARE(flow.getItem(0)->pageIndex, pdf::PDFInteger(1));
    QCOMPARE(flow.getItem(0)->boundingRect, QRectF(10, 20, 30, 40));
    QCOMPARE(flow.getItem(1)->text, QStringLiteral("Beta"));
    QCOMPARE(flow.getItem(1)->pageIndex, pdf::PDFInteger(2));
}

void DocumentTextFlowTest::movedItemsKeepTheirPages()
{
    // B73 after a move and B74: the moved item keeps its page and rectangle, the map of the pages
    // gives the new indices
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(TextFlowTestDocument::createEditorFlow());
    editor.select(1, true);
    editor.moveSelectionUp();
    editor.deselect();

    QCOMPARE(editor.getText(0), QStringLiteral("Beta"));
    QCOMPARE(editor.getText(1), QStringLiteral("Alpha"));
    QVERIFY(!editor.isModified(0));
    QVERIFY(!editor.isModified(1));

    const pdf::PDFDocumentTextFlow flow = editor.createEditedTextFlow();
    QCOMPARE(flow.getItem(0)->text, QStringLiteral("Beta"));
    QCOMPARE(flow.getItem(0)->pageIndex, pdf::PDFInteger(2));
    QCOMPARE(flow.getItem(0)->boundingRect, QRectF(50, 60, 70, 80));
    QCOMPARE(flow.getItem(1)->pageIndex, pdf::PDFInteger(1));

    const auto pageTwo = editor.getItemsForPageIndex(2);
    QCOMPARE(std::distance(pageTwo.first, pageTwo.second), std::ptrdiff_t(1));
    QCOMPARE(pageTwo.first->second, size_t(0));
    const auto pageOne = editor.getItemsForPageIndex(1);
    QCOMPARE(std::distance(pageOne.first, pageOne.second), std::ptrdiff_t(1));
    QCOMPARE(pageOne.first->second, size_t(1));

    // Down again: the order and the map of the pages as at the start
    editor.select(0, true);
    editor.moveSelectionDown();
    editor.deselect();
    QCOMPARE(editor.getText(0), QStringLiteral("Alpha"));
    QCOMPARE(editor.getItemsForPageIndex(1).first->second, size_t(0));
}

void DocumentTextFlowTest::newFlowReplacesTheItems()
{
    // A second flow replaces the items of the first one (they are not appended)
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(TextFlowTestDocument::createEditorFlow());
    editor.setTextFlow(TextFlowTestDocument::createEditorFlow());
    QCOMPARE(editor.getItemCount(), size_t(2));
    QCOMPARE(editor.createEditedTextFlow().getSize(), size_t(2));
}

void DocumentTextFlowTest::editorModelSignals()
{
    // B75: dataChanged names valid indices (the last column is ColumnLast - 1); setData says it
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(TextFlowTestDocument::createEditorFlow());
    pdf::PDFDocumentTextFlowEditorModel model(nullptr);
    model.setEditor(&editor);
    QCOMPARE(model.rowCount(QModelIndex()), 2);

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    model.selectByContainedText(QStringLiteral("Beta"));
    QCOMPARE(spy.count(), 1);
    QModelIndex topLeft = spy.front().at(0).value<QModelIndex>();
    QModelIndex bottomRight = spy.front().at(1).value<QModelIndex>();
    QVERIFY(topLeft.isValid());
    QVERIFY(bottomRight.isValid());
    QCOMPARE(bottomRight.row(), 1);
    QCOMPARE(bottomRight.column(), int(pdf::PDFDocumentTextFlowEditorModel::ColumnLast) - 1);

    spy.clear();
    QVERIFY(model.setData(model.index(1, pdf::PDFDocumentTextFlowEditorModel::ColumnText), QStringLiteral("Gamma"), Qt::EditRole));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.front().at(0).value<QModelIndex>().row(), 1);
    QVERIFY(spy.front().at(1).value<QModelIndex>().isValid());
    QCOMPARE(model.data(model.index(1, pdf::PDFDocumentTextFlowEditorModel::ColumnState), Qt::DisplayRole).toString(), QStringLiteral("Modified"));
}

/// The tagged document of the reading stream: a paragraph with /ActualText, a span with /E, a
/// figure with /Alt and no content, a paragraph in a division of /Lang cs-CZ; the catalog /Lang en-US
static pdf::PDFDocument createReadingDocument()
{
    using D = TextFlowTestDocument;
    std::vector<D::Part> parts = {
        { QStringLiteral("Revenue grew by 18 %"), 100, "/P <</MCID 0>> BDC", "EMC" },
        { QStringLiteral("ISO"), 200, "/Span <</MCID 1>> BDC", "EMC" },
        { QStringLiteral("Plain paragraph"), 300, "/P <</MCID 2>> BDC", "EMC" },
    };
    return D::create(parts, [](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page)
    {
        const pdf::PDFObjectReference revenue = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(0),
                                                              { { "ActualText", pdf::PDFObject::createString("Revenue grew by eighteen percent") } });
        const pdf::PDFObjectReference iso = D::addElement(builder, "Span", page, pdf::PDFObject::createInteger(1),
                                                          { { "E", pdf::PDFObject::createString("International Organization for Standardization") } });
        const pdf::PDFObjectReference figure = D::addElement(builder, "Figure", page, pdf::PDFObject(),
                                                             { { "Alt", pdf::PDFObject::createString("Chart of the revenue") } });
        const pdf::PDFObjectReference plain = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(2));
        const pdf::PDFObjectReference division = D::addElement(builder, "Div", page, D::createReferences({ plain }),
                                                               { { "Lang", pdf::PDFObject::createString("cs-CZ") } });
        D::finishTree(builder, { revenue, iso, figure, division }, { revenue, iso, plain }, "en-US");
    });
}

void DocumentTextFlowTest::actualTextReplacesTheText()
{
    using D = TextFlowTestDocument;
    using Flag = pdf::PDFDocumentTextFlow::Flag;
    using Tool = pdf::PDFDocumentToolAudioBook;

    // ISO 32000-2 14.9.4: /ActualText replaces the content of the element - the replacement is
    // read, the text it replaces is not; once, at the place of the element
    const pdf::PDFDocument document = createReadingDocument();
    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure));

    const pdf::PDFDocumentTextFlow::Item* replacement = D::find(items, QStringLiteral("eighteen percent"));
    const pdf::PDFDocumentTextFlow::Item* replaced = D::find(items, QStringLiteral("Revenue grew by 18"));
    QVERIFY(replacement);
    QVERIFY(replaced);
    QVERIFY(replacement < replaced);
    QVERIFY(replacement->flags.testFlag(Flag::StructureActualText));
    QVERIFY(!replacement->isText());
    QVERIFY(replaced->isText());
    QVERIFY(replaced->flags.testFlag(Flag::ReplacedText));
    QVERIFY(replaced->flags.testFlag(Flag::StructureActualText));
    QCOMPARE(replacement->pageIndex, pdf::PDFInteger(0));
    QVERIFY(!replacement->boundingRect.isEmpty());

    // The defaults of the tool read the replacement; without actual-text the text of the page
    const QStringList defaults = { QStringLiteral("actual-text"), QStringLiteral("expanded") };
    QVERIFY(Tool::isReadByDefault(*replacement, defaults));
    QVERIFY(!Tool::isReadByDefault(*replaced, defaults));
    QVERIFY(!Tool::isReadByDefault(*replacement, QStringList()));
    QVERIFY(Tool::isReadByDefault(*replaced, QStringList()));

    // Read once: no other item has the replacement
    int count = 0;
    for (const pdf::PDFDocumentTextFlow::Item& item : items)
    {
        count += item.text.contains(QStringLiteral("eighteen percent")) ? 1 : 0;
    }
    QCOMPARE(count, 1);
}

void DocumentTextFlowTest::expandedFormAndAlternativeDescription()
{
    using D = TextFlowTestDocument;
    using Flag = pdf::PDFDocumentTextFlow::Flag;
    using Tool = pdf::PDFDocumentToolAudioBook;

    const pdf::PDFDocument document = createReadingDocument();
    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure));

    // 14.9.5: /E is read instead of the abbreviation
    const pdf::PDFDocumentTextFlow::Item* expanded = D::find(items, QStringLiteral("International Organization"));
    const pdf::PDFDocumentTextFlow::Item* abbreviation = nullptr;
    for (const pdf::PDFDocumentTextFlow::Item& item : items)
    {
        if (item.text == QStringLiteral("ISO"))
        {
            abbreviation = &item;
        }
    }
    QVERIFY(expanded);
    QVERIFY(abbreviation);
    QVERIFY(expanded->flags.testFlag(Flag::StructureExpandedForm));
    QVERIFY(abbreviation->flags.testFlag(Flag::ReplacedText));
    QVERIFY(abbreviation->flags.testFlag(Flag::StructureExpandedForm));
    QVERIFY(Tool::isReadByDefault(*expanded, { QStringLiteral("expanded") }));
    QVERIFY(!Tool::isReadByDefault(*abbreviation, { QStringLiteral("expanded") }));

    // 14.9.3: a figure without any text is read by its description: a text item
    const pdf::PDFDocumentTextFlow::Item* alternative = D::find(items, QStringLiteral("Chart of the revenue"));
    QVERIFY(alternative);
    QVERIFY(alternative->isText());
    QVERIFY(alternative->flags.testFlag(Flag::StructureAlternativeDescription));
    QCOMPARE(alternative->pageIndex, pdf::PDFInteger(0));
    QVERIFY(Tool::isReadByDefault(*alternative, QStringList()));
}

void DocumentTextFlowTest::languageOfTheItems()
{
    using D = TextFlowTestDocument;

    // 14.9.2: /Lang of the element or of a parent, else /Lang of the catalog
    const pdf::PDFDocument document = createReadingDocument();
    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure));
    const pdf::PDFDocumentTextFlow::Item* czech = D::find(items, QStringLiteral("Plain paragraph"));
    const pdf::PDFDocumentTextFlow::Item* english = D::find(items, QStringLiteral("eighteen percent"));
    QVERIFY(czech);
    QVERIFY(english);
    QCOMPARE(czech->language, QStringLiteral("cs-CZ"));
    QCOMPARE(english->language, QStringLiteral("en-US"));

    // The languages are no items of the reading stream
    for (const pdf::PDFDocumentTextFlow::Item& item : items)
    {
        QVERIFY(!item.isLanguage());
    }

    // The voice of the language: the region first, else the language, else the chosen voice
    const std::vector<pdf::PDFSpeechVoice> voices = { { QStringLiteral("Zira"), QStringLiteral("en-US") }, { QStringLiteral("Hazel"), QStringLiteral("en-GB") }, { QStringLiteral("Jakub"), QStringLiteral("cs-CZ") } };
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getVoiceForLanguage(voices, QStringLiteral("cs-CZ"), QStringLiteral("Zira"), QString()), QStringLiteral("Jakub"));
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getVoiceForLanguage(voices, QStringLiteral("cs"), QStringLiteral("Zira"), QString()), QStringLiteral("Jakub"));
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getVoiceForLanguage(voices, QStringLiteral("en-GB"), QStringLiteral("Jakub"), QString()), QStringLiteral("Hazel"));
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getVoiceForLanguage(voices, QStringLiteral("en-GB"), QStringLiteral("Zira"), QString()), QStringLiteral("Zira"));
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getVoiceForLanguage(voices, QStringLiteral("de-DE"), QStringLiteral("Zira"), QString()), QStringLiteral("Zira"));
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getVoiceForLanguage(voices, QString(), QStringLiteral("Hazel"), QString()), QStringLiteral("Hazel"));
}

void DocumentTextFlowTest::oldStreamUnchanged()
{
    using D = TextFlowTestDocument;
    using Flag = pdf::PDFDocumentTextFlow::Flag;

    // The old callers (the plugin, PdfTool --say-*): /ActualText is a special item of its own and
    // the text of the page is a plain text item, as before
    const pdf::PDFDocument document = createReadingDocument();
    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure, false));
    const pdf::PDFDocumentTextFlow::Item* actualText = D::find(items, QStringLiteral("eighteen percent"));
    const pdf::PDFDocumentTextFlow::Item* text = D::find(items, QStringLiteral("Revenue grew by 18"));
    QVERIFY(actualText);
    QVERIFY(text);
    QCOMPARE(actualText->flags, pdf::PDFDocumentTextFlow::Flags(Flag::StructureActualText));
    QCOMPARE(text->flags, pdf::PDFDocumentTextFlow::Flags(Flag::Text));
    QVERIFY(text->language.isEmpty());
}

/// A page of a running head marked as an artifact (14.8.2.2) and a text of the body. The head is
/// drawn after the body, but above it: the parts are drawn in the coordinates of Qt (y downwards),
/// the head at 100, the body at 300.
static pdf::PDFDocument createArtifactDocument()
{
    using D = TextFlowTestDocument;
    std::vector<D::Part> parts = {
        { QStringLiteral("Market Analysis"), 300, QByteArray(), QByteArray() },
        { QStringLiteral("PROJECT REPORT 2026"), 100, "/Artifact <</Type /Pagination /Subtype /Header>> BDC", "EMC" },
    };
    return D::create(parts, D::StructureFunction());
}

void DocumentTextFlowTest::artifactsOfLayout()
{
    using D = TextFlowTestDocument;
    using Flag = pdf::PDFDocumentTextFlow::Flag;

    // R4-13: the artifact is a special item by its marked content, at its place on the page (above
    // the body); the body stays a text
    const pdf::PDFDocument document = createArtifactDocument();
    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Layout));
    const pdf::PDFDocumentTextFlow::Item* head = D::find(items, QStringLiteral("PROJECT REPORT"));
    const pdf::PDFDocumentTextFlow::Item* body = D::find(items, QStringLiteral("Market Analysis"));
    QVERIFY(head);
    QVERIFY(body);
    QVERIFY(head->flags.testFlag(Flag::Artifact));
    QVERIFY(!head->isText());
    QVERIFY(body->isText());
    QVERIFY(head < body);
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getSpecialKind(*head), QStringLiteral("artifacts"));
    QVERIFY(!pdf::PDFDocumentToolAudioBook::isReadByDefault(*head, { QStringLiteral("actual-text") }));
    QVERIFY(pdf::PDFDocumentToolAudioBook::isReadByDefault(*head, { QStringLiteral("artifacts") }));

    // The old stream does not know the artifacts
    const std::vector<pdf::PDFDocumentTextFlow::Item> oldItems = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Layout, false));
    const pdf::PDFDocumentTextFlow::Item* oldHead = D::find(oldItems, QStringLiteral("PROJECT REPORT"));
    QVERIFY(oldHead);
    QVERIFY(oldHead->isText());
}

void DocumentTextFlowTest::artifactsOfContent()
{
    using D = TextFlowTestDocument;
    using Flag = pdf::PDFDocumentTextFlow::Flag;

    // The order of the content stream: the artifact (no MCID) is a special item of its own
    const pdf::PDFDocument document = createArtifactDocument();
    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Content));
    const pdf::PDFDocumentTextFlow::Item* head = D::find(items, QStringLiteral("PROJECT REPORT"));
    const pdf::PDFDocumentTextFlow::Item* body = D::find(items, QStringLiteral("Market Analysis"));
    QVERIFY(head);
    QVERIFY(body);
    QVERIFY(head->flags.testFlag(Flag::Artifact));
    QVERIFY(!head->text.contains(QStringLiteral("Market")));
    QVERIFY(body->isText());
    QVERIFY(!body->text.contains(QStringLiteral("PROJECT")));
}

void DocumentTextFlowTest::cycleAndSharedNode()
{
    using D = TextFlowTestDocument;

    // A cycle (A -> B -> A) and an element shared by two paths (the root and A): the walk ends and
    // the shared text is read once (PDFStructureTree::parse reads every element once)
    std::vector<D::Part> parts = { { QStringLiteral("Shared text"), 100, "/P <</MCID 0>> BDC", "EMC" } };
    const pdf::PDFDocument document = D::create(parts, [](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page)
    {
        const pdf::PDFObjectReference shared = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(0));
        const pdf::PDFObjectReference a = builder.addObject(pdf::PDFObject());
        const pdf::PDFObjectReference b = D::addElement(builder, "Div", page, D::createReferences({ a }), { { "ActualText", pdf::PDFObject::createString("Loop") } });
        pdf::PDFDictionaryBuilder element;
        element.setEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("StructElem"));
        element.setEntry(pdf::PDFInplaceOrMemoryString("S"), pdf::PDFObject::createName("Sect"));
        element.setEntry(pdf::PDFInplaceOrMemoryString("K"), D::createReferences({ b, shared }));
        builder.setObject(a, pdf::PDFObject::createDictionary(std::move(element)));
        D::finishTree(builder, { a, shared }, { shared }, QByteArray());
    });

    for (pdf::PDFDocumentTextFlowFactory::Algorithm algorithm : { pdf::PDFDocumentTextFlowFactory::Algorithm::Structure, pdf::PDFDocumentTextFlowFactory::Algorithm::Auto })
    {
        const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, algorithm));
        int sharedCount = 0;
        for (const pdf::PDFDocumentTextFlow::Item& item : items)
        {
            sharedCount += item.text.contains(QStringLiteral("Shared text")) && item.isText() ? 1 : 0;
        }
        QCOMPARE(sharedCount, 1);
    }
}

void DocumentTextFlowTest::readingStreamOfSomePages()
{
    using D = TextFlowTestDocument;

    // The audio book of some pages reads only them: a figure of page 2 (/Pg, ISO 32000-2 14.7.2,
    // Table 355) with /Alt (14.9.3) and a title (/T) and an element of page 2 with /ActualText
    // (14.9.4) are not read on page 1, and they are on page 2
    std::vector<D::Part> parts = { { QStringLiteral("Text of page one"), 100, "/P <</MCID 0>> BDC", "EMC" } };
    const pdf::PDFDocument document = D::create(parts, [](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page)
    {
        const pdf::PDFObjectReference secondPage = builder.appendPage(QRectF(0, 0, 600, 800));
        const pdf::PDFObjectReference paragraph = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(0));
        const pdf::PDFObjectReference figure = D::addElement(builder, "Figure", secondPage, pdf::PDFObject(),
                                                             { { "Alt", pdf::PDFObject::createString("Picture of page two") },
                                                               { "T", pdf::PDFObject::createString("Title of page two") } });
        const pdf::PDFObjectReference replaced = D::addElement(builder, "Span", secondPage, pdf::PDFObject(),
                                                               { { "ActualText", pdf::PDFObject::createString("Replacement of page two") } });
        D::finishTree(builder, { paragraph, figure, replaced }, { paragraph }, QByteArray());
    });
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));

    for (const pdf::PDFInteger pageIndex : { pdf::PDFInteger(0), pdf::PDFInteger(1) })
    {
        pdf::PDFDocumentTextFlowFactory factory;
        factory.setReadingStream(true);
        const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(factory.create(&document, { pageIndex }, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure));
        const bool isSecondPage = pageIndex == 1;
        QCOMPARE(D::find(items, QStringLiteral("Text of page one")) != nullptr, !isSecondPage);
        QCOMPARE(D::find(items, QStringLiteral("Picture of page two")) != nullptr, isSecondPage);
        QCOMPARE(D::find(items, QStringLiteral("Title of page two")) != nullptr, isSecondPage);
        QCOMPARE(D::find(items, QStringLiteral("Replacement of page two")) != nullptr, isSecondPage);
    }
}

/// A page of three sentences: one without a marked content, one in a marked content with an inline
/// property list /Lang (cs-CZ), one in a marked content with a named property list of the resource
/// /Properties (14.6.2) /Lang (de-DE); the catalog /Lang en-US (ISO 32000-2 14.9.2)
static pdf::PDFDocument createMarkedLanguageDocument()
{
    using D = TextFlowTestDocument;
    std::vector<D::Part> parts = {
        { QStringLiteral("English sentence here"), 100, QByteArray(), QByteArray() },
        { QStringLiteral("Czech sentence here"), 300, "/Span <</Lang (cs-CZ)>> BDC", "EMC" },
        { QStringLiteral("German sentence here"), 500, "/Span /Lang0 BDC", "EMC" },
    };
    return D::create(parts, [](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page)
    {
        pdf::PDFDictionaryBuilder language;
        language.setEntry(pdf::PDFInplaceOrMemoryString("Lang"), pdf::PDFObject::createString("de-DE"));
        pdf::PDFDictionaryBuilder properties;
        properties.setEntry(pdf::PDFInplaceOrMemoryString("Lang0"), pdf::PDFObject::createDictionary(std::move(language)));
        pdf::PDFDictionaryBuilder resources;
        resources.setEntry(pdf::PDFInplaceOrMemoryString("Properties"), pdf::PDFObject::createDictionary(std::move(properties)));

        // The resources of the page are merged where they are (an object of their own keeps its fonts)
        const pdf::PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(page));
        const pdf::PDFObject pageResources = pageDictionary ? pageDictionary->get("Resources") : pdf::PDFObject();
        if (pageResources.isReference())
        {
            builder.mergeTo(pageResources.getReference(), pdf::PDFObject::createDictionary(std::move(resources)));
        }
        else
        {
            pdf::PDFDictionaryBuilder pageChange;
            pageChange.setEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
            builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageChange)));
        }

        pdf::PDFDictionaryBuilder catalogChange;
        catalogChange.setEntry(pdf::PDFInplaceOrMemoryString("Lang"), pdf::PDFObject::createString("en-US"));
        builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalogChange)));
    });
}

void DocumentTextFlowTest::languageOfMarkedContent()
{
    using D = TextFlowTestDocument;
    using Algorithm = pdf::PDFDocumentTextFlowFactory::Algorithm;

    // ISO 32000-2 14.9.2: /Lang of the property list of a marked content is the language of its
    // text (more specific than the catalog), inline or named; the orders Layout and Content
    const pdf::PDFDocument document = createMarkedLanguageDocument();
    for (Algorithm algorithm : { Algorithm::Layout, Algorithm::Content })
    {
        const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, algorithm));
        const pdf::PDFDocumentTextFlow::Item* english = D::find(items, QStringLiteral("English sentence"));
        const pdf::PDFDocumentTextFlow::Item* czech = D::find(items, QStringLiteral("Czech sentence"));
        const pdf::PDFDocumentTextFlow::Item* german = D::find(items, QStringLiteral("German sentence"));
        QStringList texts;
        for (const pdf::PDFDocumentTextFlow::Item& item : items)
        {
            texts << QStringLiteral("[%1|%2]").arg(item.text, item.language);
        }
        QVERIFY2(english && czech && german, qPrintable(texts.join(QStringLiteral(" "))));
        QVERIFY(!english->text.contains(QStringLiteral("Czech")));
        QVERIFY(!czech->text.contains(QStringLiteral("German")));
        QCOMPARE(english->language, QStringLiteral("en-US"));
        QCOMPARE(czech->language, QStringLiteral("cs-CZ"));
        QCOMPARE(german->language, QStringLiteral("de-DE"));
    }

    // The old stream (the plugin) knows no languages
    const std::vector<pdf::PDFDocumentTextFlow::Item> oldItems = D::getTextItems(D::createReadingStream(document, Algorithm::Layout, false));
    const pdf::PDFDocumentTextFlow::Item* oldCzech = D::find(oldItems, QStringLiteral("Czech sentence"));
    QVERIFY(oldCzech);
    QVERIFY(oldCzech->language.isEmpty());
}

void DocumentTextFlowTest::languageOfMarkedContentInStructure()
{
    using D = TextFlowTestDocument;

    // 14.9.2: the marked content inside of the content of an element of /Lang de-DE has /Lang
    // cs-CZ - the marked content is the more specific; the other paragraph has /Lang of the catalog
    std::vector<D::Part> parts = {
        { QStringLiteral("Plain paragraph"), 100, "/P <</MCID 0>> BDC", "EMC" },
        { QStringLiteral("Czech words inside"), 300, "/P <</MCID 1>> BDC /Span <</Lang (cs-CZ)>> BDC", "EMC EMC" },
    };
    const pdf::PDFDocument document = D::create(parts, [](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page)
    {
        const pdf::PDFObjectReference plain = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(0));
        const pdf::PDFObjectReference german = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(1), { { "Lang", pdf::PDFObject::createString("de-DE") } });
        D::finishTree(builder, { plain, german }, { plain, german }, "en-US");
    });

    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure));
    const pdf::PDFDocumentTextFlow::Item* plain = D::find(items, QStringLiteral("Plain paragraph"));
    const pdf::PDFDocumentTextFlow::Item* czech = D::find(items, QStringLiteral("Czech words"));
    QVERIFY(plain);
    QVERIFY(czech);
    QCOMPARE(plain->language, QStringLiteral("en-US"));
    QCOMPARE(czech->language, QStringLiteral("cs-CZ"));
}

void DocumentTextFlowTest::pageMarksOfTheStructure()
{
    using D = TextFlowTestDocument;
    using Flag = pdf::PDFDocumentTextFlow::Flag;

    // B76: the order of the structure has the marks of the pages as Layout and Content, so
    // readSpecial page-numbers (PdfTool --say-page-numbers) reads them: "Page 1" before the text of
    // page 1, "Page 2" before the figure of page 2 (/Pg, 14.7.2)
    std::vector<D::Part> parts = { { QStringLiteral("Text of page one"), 100, "/P <</MCID 0>> BDC", "EMC" } };
    const pdf::PDFDocument document = D::create(parts, [](pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page)
    {
        const pdf::PDFObjectReference secondPage = builder.appendPage(QRectF(0, 0, 600, 800));
        const pdf::PDFObjectReference paragraph = D::addElement(builder, "P", page, pdf::PDFObject::createInteger(0));
        const pdf::PDFObjectReference figure = D::addElement(builder, "Figure", secondPage, pdf::PDFObject(),
                                                             { { "Alt", pdf::PDFObject::createString("Picture of page two") } });
        D::finishTree(builder, { paragraph, figure }, { paragraph }, QByteArray());
    });

    const std::vector<pdf::PDFDocumentTextFlow::Item> items = D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure));
    std::vector<const pdf::PDFDocumentTextFlow::Item*> marks;
    for (const pdf::PDFDocumentTextFlow::Item& item : items)
    {
        if (item.flags.testFlag(Flag::PageStart))
        {
            marks.push_back(&item);
        }
    }
    QCOMPARE(int(marks.size()), 2);
    QCOMPARE(marks[0]->text, QStringLiteral("Page 1"));
    QCOMPARE(marks[0]->pageIndex, pdf::PDFInteger(0));
    QCOMPARE(marks[1]->text, QStringLiteral("Page 2"));
    QCOMPARE(marks[1]->pageIndex, pdf::PDFInteger(1));

    const pdf::PDFDocumentTextFlow::Item* text = D::find(items, QStringLiteral("Text of page one"));
    const pdf::PDFDocumentTextFlow::Item* figure = D::find(items, QStringLiteral("Picture of page two"));
    QVERIFY(text);
    QVERIFY(figure);
    QVERIFY(marks[0] < text);
    QVERIFY(text < marks[1]);
    QVERIFY(marks[1] < figure);
    QCOMPARE(pdf::PDFDocumentToolAudioBook::getSpecialKind(*marks[0]), QStringLiteral("page-numbers"));
    QVERIFY(pdf::PDFDocumentToolAudioBook::isReadByDefault(*marks[0], { QStringLiteral("page-numbers") }));
    QVERIFY(!pdf::PDFDocumentToolAudioBook::isReadByDefault(*marks[0], { QStringLiteral("actual-text") }));

    // The old stream of the structure stays without them
    for (const pdf::PDFDocumentTextFlow::Item& item : D::getTextItems(D::createReadingStream(document, pdf::PDFDocumentTextFlowFactory::Algorithm::Structure, false)))
    {
        QVERIFY(!item.flags.testFlag(Flag::PageStart));
    }
}

void DocumentTextFlowTest::defaultVoiceKeepsItsLanguage()
{
    // Coordinator 14. 11.: no chosen voice is the default voice of the engine; it stays (an empty
    // name) for a language it speaks, another voice is taken only for a language it does not speak
    using Tool = pdf::PDFDocumentToolAudioBook;
    const std::vector<pdf::PDFSpeechVoice> voices = { { QStringLiteral("Zira"), QStringLiteral("en-US") }, { QStringLiteral("Hazel"), QStringLiteral("en-GB") }, { QStringLiteral("Jakub"), QStringLiteral("cs-CZ") } };
    QCOMPARE(Tool::getVoiceForLanguage(voices, QStringLiteral("en-GB"), QString(), QStringLiteral("Zira")), QString());
    QCOMPARE(Tool::getVoiceForLanguage(voices, QStringLiteral("cs-CZ"), QString(), QStringLiteral("Zira")), QStringLiteral("Jakub"));
    QCOMPARE(Tool::getVoiceForLanguage(voices, QStringLiteral("de-DE"), QString(), QStringLiteral("Zira")), QString());

    // An unknown default voice: the voice of the language, as before
    QCOMPARE(Tool::getVoiceForLanguage(voices, QStringLiteral("en-GB"), QString(), QString()), QStringLiteral("Hazel"));

    // A chosen voice wins over the default voice
    QCOMPARE(Tool::getVoiceForLanguage(voices, QStringLiteral("en-GB"), QStringLiteral("Jakub"), QStringLiteral("Zira")), QStringLiteral("Hazel"));
}

void DocumentTextFlowTest::editorCopiesShareTheOriginal()
{
    // RS4AUDIO Low: every step of Undo of Audio Book is a copy of the editor - the copies share the
    // original flow, the value of a step keeps the editor only (no edited flow beside it)
    pdf::PDFDocumentTextFlowEditor editor;
    editor.setTextFlow(TextFlowTestDocument::createEditorFlow());
    QVERIFY(editor.getOriginalTextFlow());

    pdf::PDFDocumentTextFlowEditor copy = editor;
    copy.setText(QStringLiteral("Alpha, edited"), 0);
    QCOMPARE(copy.getOriginalTextFlow(), editor.getOriginalTextFlow());
    QCOMPARE(editor.getText(0), QStringLiteral("Alpha"));
    QVERIFY(copy.isModified(0));

    const pdf::PDFDocumentToolAudioBookStream stream(copy);
    QVERIFY(!stream.isEmpty());
    QVERIFY(!stream.getFlow());
    QVERIFY(stream.getEditor());
    QCOMPARE(stream.getEditor()->getOriginalTextFlow(), editor.getOriginalTextFlow());
    QCOMPARE(stream.getItemCount(), qint64(2));

    // The run gets the edited flow
    const pdf::PDFDocumentTextFlow flow = stream.createFlow();
    QCOMPARE(flow.getSize(), size_t(2));
    QCOMPARE(flow.getItem(0)->text, QStringLiteral("Alpha, edited"));
    QCOMPARE(flow.getItem(1)->text, QStringLiteral("Beta"));

    // Two values of one editor are two steps (their identity differs), a copy of a value is equal
    const pdf::PDFDocumentToolAudioBookStream other(copy);
    QVERIFY(!(other == stream));
    const pdf::PDFDocumentToolAudioBookStream same = stream;
    QVERIFY(same == stream);
}

QTEST_MAIN(DocumentTextFlowTest)

#include "tst_documenttextflowtest.moc"
