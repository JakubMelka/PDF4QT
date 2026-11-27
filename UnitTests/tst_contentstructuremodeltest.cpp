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

// Edit Content, stage E0, package P04: the structure tree as the editor reads it, the consistency
// of the structure with the edits of the content and the writers of the number tree and of the
// structure (GUI_overhaul/edit_tool/zadani_jadro.md 4.12, zadani_overeni.md 6.1, 6.2, chapter 8).
//
// The documents are built in memory from hand-written objects; every expected value is written by
// hand from the input of the test and from ISO 32000-2 (14.7.2 Tables 354 and 355, 14.7.3 role map,
// 14.7.5.2 Table 357, 14.7.5.3 Table 358, 14.7.5.4 parent tree and Table 359, 7.9.7 Table 37) or from
// the consistency table of zadani_jadro.md 4.12, never taken from a run. A written document is
// checked key by key against such expectations and read back by the model.

#include "contenttestdocuments.h"
#include "pdfcontentstructure.h"
#include "pdfnumbertreewriter.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfstructuretreewriter.h"

#include <QtTest>

#include <array>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <map>
#include <vector>

using namespace pdf;

/// Short names of the hand-written objects
class Obj
{
public:
    Obj() = delete;

    using Entries = pdftest::ContentTestDocuments::DictionaryEntries;

    static PDFObject name(const char* value) { return PDFObject::createName(QByteArray(value)); }
    static PDFObject integer(PDFInteger value) { return PDFObject::createInteger(value); }
    static PDFObject string(const QByteArray& value) { return PDFObject::createString(QByteArray(value)); }
    static PDFObject reference(PDFObjectReference value) { return PDFObject::createReference(value); }
    static PDFObject null() { return PDFObject(); }
    static PDFObject dictionary(Entries entries) { return pdftest::ContentTestDocuments::createDictionaryObject(std::move(entries)); }
    static PDFObject array(std::vector<PDFObject> items) { return pdftest::ContentTestDocuments::createArrayObject(std::move(items)); }

    /// A stream without data, with the entries of its dictionary
    static PDFObject stream(Entries entries)
    {
        const PDFObject streamDictionary = dictionary(std::move(entries));
        return PDFObject::createStream(PDFStream(PDFDictionaryBuilder(*streamDictionary.getDictionary()), QByteArray()));
    }

    /// << /Type /MCR /Pg page /MCID mcid >> (ISO 32000-2, Table 357)
    static PDFObject markedContentReference(PDFObjectReference page, PDFInteger mcid)
    {
        return dictionary({ { "Type", name("MCR") }, { "Pg", reference(page) }, { "MCID", integer(mcid) } });
    }

    /// << /Type /StructElem /S type /P parent [/Pg page] more... [/K kids] >> (Table 355)
    static PDFObject element(const char* type, PDFObjectReference parent, PDFObjectReference page, PDFObject kids, Entries more = Entries())
    {
        Entries entries = { { "Type", name("StructElem") }, { "S", name(type) }, { "P", reference(parent) } };
        if (page.isValid())
        {
            entries.emplace_back("Pg", reference(page));
        }
        for (auto& entry : more)
        {
            entries.push_back(std::move(entry));
        }
        if (!kids.isNull())
        {
            entries.emplace_back("K", std::move(kids));
        }
        return dictionary(std::move(entries));
    }
};

/// A document being written by hand: one page [0 0 200 200] and the objects of the test. An object
/// is stored exactly as it is given (PDFDocumentBuilder::addObject would remove null entries).
class StructureDocument
{
public:
    StructureDocument() { m_page = m_builder.appendPage(QRectF(0, 0, 200, 200)); }

    PDFObjectReference page() const { return m_page; }
    PDFObjectReference addPage() { return m_builder.appendPage(QRectF(0, 0, 200, 200)); }
    PDFObjectReference reserve() { return m_builder.addObject(PDFObject()); }
    void set(PDFObjectReference reference, PDFObject object) { m_builder.setObject(reference, std::move(object)); }

    PDFObjectReference add(PDFObject object)
    {
        const PDFObjectReference reference = reserve();
        set(reference, std::move(object));
        return reference;
    }

    /// /StructParents of a page (Table 359)
    void setStructuralParents(PDFObjectReference page, PDFInteger key)
    {
        m_builder.mergeTo(page, Obj::dictionary({ { "StructParents", Obj::integer(key) } }));
    }

    /// /StructTreeRoot of the catalog
    void setRoot(PDFObject root)
    {
        m_builder.mergeTo(m_builder.getCatalogReference(), Obj::dictionary({ { "StructTreeRoot", std::move(root) } }));
    }

    PDFDocument build() { return m_builder.build(); }

private:
    PDFDocumentBuilder m_builder;
    PDFObjectReference m_page;
};

/// The tagged document of most tests, see ContentStructureTestHelper::createTagged
struct TaggedDocument
{
    PDFDocument document;
    PDFObjectReference page;
    PDFObjectReference annotation;
    PDFObjectReference root;
    PDFObjectReference parentTree;
    PDFObjectReference pageArray;       ///< Valid, when the array of the page is an indirect object
    PDFObjectReference documentElement;
    PDFObjectReference paragraph;
    PDFObjectReference span;
    PDFObjectReference figure;
    PDFObjectReference link;
};

/// A control, which cancels the operation at its n-th question
class CancelAfter : public PDFOperationControl
{
public:
    explicit CancelAfter(int questions) : m_remaining(questions) { }

    virtual bool isOperationCanceled() const override { return m_remaining.fetch_sub(1) <= 0; }

private:
    mutable std::atomic<int> m_remaining;
};

class ContentStructureTestHelper
{
public:
    ContentStructureTestHelper() = delete;

    /// The tagged document (ISO 32000-2, 14.7): one page with /StructParents 0 and a link annotation
    /// with /StructParent 1.
    ///
    ///   StructTreeRoot  /K [Document] /ParentTree (an indirect object) /ParentTreeNextKey 2
    ///                   /IDTree << /Names [(fig) Figure] >> /RoleMap << /Para /P >>
    ///     Document      /S /Document /K [P Span Figure Link]
    ///       P           /S /P /Pg page /K 0                                      (an integer, not in an array)
    ///       Span        /S /Span /Pg page /K << /Type /MCR /Pg page /MCID 1 >>   (a marked-content reference)
    ///       Figure      /S /Figure /Pg page /Alt (Square) /ID (fig) /K [3]       (an integer in an array)
    ///       Link        /S /Link /Pg page /K << /Type /OBJR /Obj annotation >>   (an object reference)
    ///
    /// The parent tree is one node << /Nums [ 0 [P Span null Figure] 1 Link ] >>: the identifier 2 is
    /// not used, so the array has a null at the index 2 (14.7.5.4). The array is a direct object, or
    /// an indirect one (\p isPageArrayIndirect).
    static TaggedDocument createTagged(bool isPageArrayIndirect = false)
    {
        StructureDocument d;
        TaggedDocument t;
        t.page = d.page();
        t.root = d.reserve();
        t.documentElement = d.reserve();
        t.paragraph = d.reserve();
        t.span = d.reserve();
        t.figure = d.reserve();
        t.link = d.reserve();

        t.annotation = d.add(Obj::dictionary({ { "Type", Obj::name("Annot") },
                                               { "Subtype", Obj::name("Link") },
                                               { "Rect", pdftest::ContentTestDocuments::createIntegerArrayObject({ 20, 148, 60, 162 }) },
                                               { "StructParent", Obj::integer(1) } }));

        PDFObject pageArray = Obj::array({ Obj::reference(t.paragraph), Obj::reference(t.span), Obj::null(), Obj::reference(t.figure) });
        if (isPageArrayIndirect)
        {
            t.pageArray = d.add(pageArray);
            pageArray = Obj::reference(t.pageArray);
        }

        t.parentTree = d.add(Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), pageArray, Obj::integer(1), Obj::reference(t.link) }) } }));

        d.set(t.root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                        { "K", Obj::array({ Obj::reference(t.documentElement) }) },
                                        { "ParentTree", Obj::reference(t.parentTree) },
                                        { "ParentTreeNextKey", Obj::integer(2) },
                                        { "IDTree", Obj::dictionary({ { "Names", Obj::array({ Obj::string("fig"), Obj::reference(t.figure) }) } }) },
                                        { "RoleMap", Obj::dictionary({ { "Para", Obj::name("P") } }) } }));

        d.set(t.documentElement, Obj::element("Document", t.root, PDFObjectReference(),
                                              Obj::array({ Obj::reference(t.paragraph), Obj::reference(t.span), Obj::reference(t.figure), Obj::reference(t.link) })));
        d.set(t.paragraph, Obj::element("P", t.documentElement, t.page, Obj::integer(0)));
        d.set(t.span, Obj::element("Span", t.documentElement, t.page, Obj::markedContentReference(t.page, 1)));
        d.set(t.figure, Obj::element("Figure", t.documentElement, t.page, Obj::array({ Obj::integer(3) }),
                                     { { "Alt", Obj::string("Square") }, { "ID", Obj::string("fig") } }));
        d.set(t.link, Obj::element("Link", t.documentElement, t.page,
                                   Obj::dictionary({ { "Type", Obj::name("OBJR") }, { "Obj", Obj::reference(t.annotation) } })));

        d.setStructuralParents(t.page, 0);
        d.setRoot(Obj::reference(t.root));
        t.document = d.build();
        return t;
    }

    static const PDFContentStructureElement* findElement(const PDFContentStructureModel& model, PDFObjectReference reference)
    {
        for (const PDFContentStructureElement& element : model.getElements())
        {
            if (element.reference == reference)
            {
                return &element;
            }
        }
        return nullptr;
    }

    static const PDFContentStructureElement* findElementOfType(const PDFContentStructureModel& model, const char* type)
    {
        for (const PDFContentStructureElement& element : model.getElements())
        {
            if (element.type == type)
            {
                return &element;
            }
        }
        return nullptr;
    }

    static PDFContentStructureElementId idOf(const PDFContentStructureModel& model, PDFObjectReference reference)
    {
        const PDFContentStructureElement* element = findElement(model, reference);
        return element ? element->id : -2;
    }

    /// The children of an element as texts: "E<object number>" an element, "M<mcid>" an integer
    /// identifier, "R<mcid>@<page>" or "R<mcid>@<page>/<stream>" a marked-content reference,
    /// "O<object number>" an object reference
    static QStringList childrenOf(const PDFContentStructureModel& model, PDFObjectReference reference)
    {
        QStringList result;
        const PDFContentStructureElement* element = findElement(model, reference);
        if (!element)
        {
            return { QString("no element") };
        }

        for (const PDFContentStructureChild& child : element->children)
        {
            switch (child.kind)
            {
                case PDFContentStructureChild::Kind::Element:
                {
                    const PDFContentStructureElement* childElement = model.getElement(child.element);
                    result << QString("E%1").arg(childElement ? childElement->reference.objectNumber : -1);
                    break;
                }
                case PDFContentStructureChild::Kind::MarkedContent:
                    result << QString("M%1").arg(child.mcid);
                    break;
                case PDFContentStructureChild::Kind::MarkedContentReference:
                    result << (child.stream.isValid() ? QString("R%1@%2/%3").arg(child.mcid).arg(child.page.objectNumber).arg(child.stream.objectNumber)
                                                      : QString("R%1@%2").arg(child.mcid).arg(child.page.objectNumber));
                    break;
                case PDFContentStructureChild::Kind::ObjectReference:
                    result << QString("O%1").arg(child.object.objectNumber);
                    break;
            }
        }
        return result;
    }

    static QString elementText(PDFObjectReference reference) { return QString("E%1").arg(reference.objectNumber); }

    /// The elements of a key of the parent tree as the object numbers of the elements, 0 for none
    static QList<PDFInteger> parentTreeOf(const PDFContentStructureModel& model, PDFInteger key)
    {
        QList<PDFInteger> result;
        auto it = model.getParentTree().find(key);
        if (it == model.getParentTree().end())
        {
            return { PDFInteger(-999) };
        }

        for (PDFContentStructureElementId id : it->second.elements)
        {
            const PDFContentStructureElement* element = model.getElement(id);
            result << (element ? element->reference.objectNumber : 0);
        }
        return result;
    }

    static QList<PDFInteger> numbers(std::initializer_list<PDFObjectReference> references)
    {
        QList<PDFInteger> result;
        for (const PDFObjectReference& reference : references)
        {
            result << reference.objectNumber;
        }
        return result;
    }

    /// The items of a written array as texts: "<object number> R" a reference, "null", an integer,
    /// "(string)" a string, "/Name" a name, "<<...>>" a dictionary, "[...]" an array, "?" else
    static QStringList itemsOf(const PDFObject& array)
    {
        QStringList result;
        if (!array.isArray())
        {
            return { QString("not an array") };
        }

        for (const PDFObject& item : *array.getArray())
        {
            if (item.isReference())
            {
                result << QString("%1 R").arg(item.getReference().objectNumber);
            }
            else if (item.isNull())
            {
                result << QString("null");
            }
            else if (item.isInt())
            {
                result << QString::number(item.getInteger());
            }
            else if (item.isString())
            {
                result << QString("(%1)").arg(QString::fromLatin1(item.getString()));
            }
            else if (item.isName())
            {
                result << QString("/%1").arg(QString::fromLatin1(item.getString()));
            }
            else if (item.isDictionary())
            {
                result << QString("<<...>>");
            }
            else if (item.isArray())
            {
                result << QString("[...]");
            }
            else
            {
                result << QString("?");
            }
        }
        return result;
    }

    static QString referenceText(PDFObjectReference reference) { return QString("%1 R").arg(reference.objectNumber); }

    /// The keys of a dictionary in their order
    static QStringList keysOf(const PDFDictionary* dictionary)
    {
        QStringList result;
        for (size_t i = 0; dictionary && i < dictionary->getCount(); ++i)
        {
            result << QString::fromLatin1(dictionary->getKey(i).getString());
        }
        return result;
    }

    static const PDFDictionary* dictionaryOf(const PDFDocumentBuilder& builder, PDFObjectReference reference)
    {
        const PDFObject& object = builder.getObjectByReference(reference);
        return object.isDictionary() ? object.getDictionary() : nullptr;
    }

    /// The structure tree root of the document of the builder
    static const PDFDictionary* rootOf(const PDFDocumentBuilder& builder)
    {
        const PDFDictionary* catalog = dictionaryOf(builder, builder.getCatalogReference());
        const PDFObject& root = catalog ? builder.getObject(catalog->get("StructTreeRoot")) : PDFObject();
        return root.isDictionary() ? root.getDictionary() : nullptr;
    }

    /// /Nums of the root node of the parent tree of the document of the builder
    static PDFObject parentTreeNumbers(const PDFDocumentBuilder& builder)
    {
        const PDFDictionary* root = rootOf(builder);
        const PDFDictionary* tree = root ? builder.getDictionaryFromObject(root->get("ParentTree")) : nullptr;
        return tree ? builder.getObject(tree->get("Nums")) : PDFObject();
    }

    static PDFContentStructureChange removal(PDFInteger key, PDFInteger mcid)
    {
        PDFContentStructureChange change;
        change.action = PDFContentStructureChange::Action::RemoveSequence;
        change.structuralParentKey = key;
        change.mcid = mcid;
        return change;
    }

    static PDFContentStructureChange split(PDFInteger key, PDFInteger mcid, PDFInteger newMcid)
    {
        PDFContentStructureChange change;
        change.action = PDFContentStructureChange::Action::SplitSequence;
        change.structuralParentKey = key;
        change.mcid = mcid;
        change.newMcid = newMcid;
        return change;
    }

    /// The items of a number tree: \p count keys 0, step, 2 * step, ..., the value of the i-th key is
    /// the integer 1000 + i
    static std::map<PDFInteger, PDFObject> numberTreeItems(int count, int step)
    {
        std::map<PDFInteger, PDFObject> items;
        for (int i = 0; i < count; ++i)
        {
            items[PDFInteger(i) * step] = PDFObject::createInteger(1000 + i);
        }
        return items;
    }

    static size_t objectCount(const PDFDocumentBuilder& builder) { return builder.getStorage()->getObjects().size(); }
};

using Helper = ContentStructureTestHelper;

class ContentStructureModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Reading
    void readSharedTaggedDocument();
    void readTaggedDocument();
    void readDocumentWithoutStructure();
    void readElementEntries();
    void readRoleMap_data();
    void readRoleMap();
    void readChildren();
    void readForeignChildren();
    void readParentTreeItems();
    void readNestedParentTree();
    void readNextParentTreeKey_data();
    void readNextParentTreeKey();
    void readCycleAndSharedElement();
    void readElementSharedByManyPaths();
    void readDepthLimit_data();
    void readDepthLimit();
    void readParentTreeDepthLimit_data();
    void readParentTreeDepthLimit();
    void readParentTreeCycle();
    void readBudgetOfStoredItems_data();
    void readBudgetOfStoredItems();
    void readIrregularContent();
    void readCanceled();
    void readingOrder();
    void contentOfStream();
    void allocateMarkedContentId_data();
    void allocateMarkedContentId();
    void allocateAboveChildren();

    // Consistency (the table of zadani_jadro.md 4.12, row by row)
    void deleteInsideSequence();
    void deleteLastContent();
    void moveOutOfSequence();
    void reorderInSequence();
    void moveWholeSequenceAndCopy();
    void splitSequence();
    void splitKeepsForeignChildren();
    void splitTwiceInOneCommand();
    void splitRefused();
    void splitOfIrregularContent_data();
    void splitOfIrregularContent();
    void removeSequenceOfForeignChild();
    void splitOfChildWithoutPage();
    void appliedIgnoresWrongChange();
    void clearPageKeepsObjectItems();
    void clearPageOfManySequences();
    void documentWithoutStructureChanges();

    // Differences of the changes (undo keeps a difference, zadani_jadro.md 4.5)
    void differenceOfEachAction();
    void differenceRevertedInReverseOrder();
    void differenceIsProportional();
    void versionOfModel();

    // The source of the model and the separation of the changes of a page
    void sourceFingerprint();
    void sourceFingerprintWalk();
    void withoutChangesOfKeys();
    void withoutChangesOfUnknownContent();
    void withoutChangesOfKeysChildByChild();

    // Number tree writer
    void numberTreeEmpty();
    void numberTreeRootOnly();
    void numberTreeTwoLeaves();
    void numberTreeFullRoot();
    void numberTreeThreeLevels();
    void numberTreeValuesAndNoBuilder();

    // Structure tree writer
    void writeUnmodified();
    void writeRemovedSequence();
    void writeSplitSequence();
    void writeOnlyModifiedElements();
    void writeSingleChild();
    void writeKeepsUnknownEntries();
    void writeKeepsForeignItems();
    void writeIndirectArray();
    void writeBalancedParentTree();
    void writeSharedElementOnce();
    void writeDirectRoot();
    void writeErrors();
    void writeCheck();
};

// ---------------------------------------------------------------------------------------------
//  Reading
// ---------------------------------------------------------------------------------------------

void ContentStructureModelTest::readSharedTaggedDocument()
{
    // The tagged document of contenttestdocuments.h, by its comment:
    //   root /K [Document]; Document /K [P Span Figure Link]; P /K 0; Span /K << /MCR /Pg page /MCID 1 >>;
    //   Figure /Alt (Square) /K [3]; Link /K << /OBJR /Obj annotation >>;
    //   /ParentTree /Nums [ 0 [P Span null Figure] 1 Link ], /ParentTreeNextKey 2
    const PDFDocument document = pdftest::ContentTestDocuments::createTaggedDocument();
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QVERIFY(!model->isEmpty());
    QVERIFY(!model->isModified());
    QVERIFY2(model->getIssues().empty(), qPrintable(model->getIssues().empty() ? QString() : model->getIssues().front()));

    // The root and five elements; the element 0 is the root
    const std::vector<PDFContentStructureElement>& elements = model->getElements();
    QCOMPARE(elements.size(), size_t(6));
    QCOMPARE(elements.front().id, PDFContentStructureElementId(0));
    QCOMPARE(elements.front().parent, PDFContentStructureElementId(-1));
    QVERIFY(elements.front().type.isEmpty());

    const PDFContentStructureElement* documentElement = Helper::findElementOfType(*model, "Document");
    const PDFContentStructureElement* paragraph = Helper::findElementOfType(*model, "P");
    const PDFContentStructureElement* span = Helper::findElementOfType(*model, "Span");
    const PDFContentStructureElement* figure = Helper::findElementOfType(*model, "Figure");
    const PDFContentStructureElement* link = Helper::findElementOfType(*model, "Link");
    QVERIFY(documentElement && paragraph && span && figure && link);
    QCOMPARE(documentElement->parent, PDFContentStructureElementId(0));
    for (const PDFContentStructureElement* element : { paragraph, span, figure, link })
    {
        QCOMPARE(element->parent, documentElement->id);
        QCOMPARE(element->standardType, element->type);
        QVERIFY(element->reference.isValid());
        QVERIFY(!element->isModified);
        QCOMPARE(model->getElement(element->id), element);
    }

    // P: the integer child of /K is a child (the old parser of the structure dropped it, KE.17)
    QCOMPARE(paragraph->children.size(), size_t(1));
    QVERIFY(paragraph->children[0].kind == PDFContentStructureChild::Kind::MarkedContent);
    QCOMPARE(paragraph->children[0].mcid, PDFInteger(0));
    QVERIFY(!paragraph->children[0].page.isValid());
    QCOMPARE(paragraph->page, page);

    // Span: the marked-content reference with its own /Pg
    QCOMPARE(span->children.size(), size_t(1));
    QVERIFY(span->children[0].kind == PDFContentStructureChild::Kind::MarkedContentReference);
    QCOMPARE(span->children[0].mcid, PDFInteger(1));
    QCOMPARE(span->children[0].page, page);

    // Figure: the integer in an array, /Alt
    QCOMPARE(figure->children.size(), size_t(1));
    QVERIFY(figure->children[0].kind == PDFContentStructureChild::Kind::MarkedContent);
    QCOMPARE(figure->children[0].mcid, PDFInteger(3));
    QCOMPARE(figure->alternateText.value_or(QString()), QString("Square"));
    QVERIFY(!figure->title.has_value());

    // Link: the object reference names the annotation of the page
    QCOMPARE(link->children.size(), size_t(1));
    QVERIFY(link->children[0].kind == PDFContentStructureChild::Kind::ObjectReference);
    QCOMPARE(document.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
    QCOMPARE(link->children[0].object, document.getCatalog()->getPage(0)->getAnnotations().front());

    // The parent tree: the array of the page keeps its null at the index 2, so the identifier 3
    // is the Figure (14.7.5.4: the identifier is the index)
    QCOMPARE(model->getParentTree().size(), size_t(2));
    const PDFContentParentTreeItem& pageItem = model->getParentTree().at(0);
    QVERIFY(pageItem.kind == PDFContentParentTreeItem::Kind::MarkedContentArray);
    QCOMPARE(pageItem.elements, (std::vector<PDFContentStructureElementId>{ paragraph->id, span->id, -1, figure->id }));
    const PDFContentParentTreeItem& annotationItem = model->getParentTree().at(1);
    QVERIFY(annotationItem.kind == PDFContentParentTreeItem::Kind::Element);
    QCOMPARE(annotationItem.elements, (std::vector<PDFContentStructureElementId>{ link->id }));

    QCOMPARE(model->getElementOfMarkedContent(0, 0), paragraph->id);
    QCOMPARE(model->getElementOfMarkedContent(0, 1), span->id);
    QCOMPARE(model->getElementOfMarkedContent(0, 2), PDFContentStructureElementId(-1));
    QCOMPARE(model->getElementOfMarkedContent(0, 3), figure->id);
    QCOMPARE(model->getNextParentTreeKey(), PDFInteger(2));
    QCOMPARE(model->getReadingOrder(0), (std::vector<PDFInteger>{ 0, 1, 3 }));
}

void ContentStructureModelTest::readTaggedDocument()
{
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    QVERIFY(model->getIssues().empty());
    QCOMPARE(model->getElements().size(), size_t(6));

    // The element 0 is the structure tree root; an identifier outside of the model names nothing
    QCOMPARE(model->getElements().front().reference, t.root);
    QCOMPARE(model->getElements().front().parent, PDFContentStructureElementId(-1));
    QVERIFY(model->getElement(-1) == nullptr);
    QVERIFY(model->getElement(6) == nullptr);
    QCOMPARE(model->getElement(5), &model->getElements()[5]);

    QCOMPARE(Helper::childrenOf(*model, t.root), QStringList({ Helper::elementText(t.documentElement) }));
    QCOMPARE(Helper::childrenOf(*model, t.documentElement), QStringList({ Helper::elementText(t.paragraph), Helper::elementText(t.span), Helper::elementText(t.figure), Helper::elementText(t.link) }));
    QCOMPARE(Helper::childrenOf(*model, t.paragraph), QStringList({ "M0" }));
    QCOMPARE(Helper::childrenOf(*model, t.span), QStringList({ QString("R1@%1").arg(t.page.objectNumber) }));
    QCOMPARE(Helper::childrenOf(*model, t.figure), QStringList({ "M3" }));
    QCOMPARE(Helper::childrenOf(*model, t.link), QStringList({ QString("O%1").arg(t.annotation.objectNumber) }));

    QCOMPARE(Helper::findElement(*model, t.figure)->elementId.value_or(QByteArray()), QByteArray("fig"));
    QCOMPARE(Helper::findElement(*model, t.span)->parent, Helper::idOf(*model, t.documentElement));

    QCOMPARE(Helper::parentTreeOf(*model, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure }));
    QCOMPARE(Helper::parentTreeOf(*model, 1), Helper::numbers({ t.link }));

    // The value of a key as it is in the document
    QVERIFY(model->getParentTree().at(1).raw == Obj::reference(t.link));
    QVERIFY(model->getParentTree().at(0).raw.isArray());

    // A key or an identifier, which is not there: no element. The key 1 is an object, not a
    // page: it has no identifiers.
    QCOMPARE(model->getElementOfMarkedContent(0, 4), PDFContentStructureElementId(-1));
    QCOMPARE(model->getElementOfMarkedContent(0, -1), PDFContentStructureElementId(-1));
    QCOMPARE(model->getElementOfMarkedContent(1, 0), PDFContentStructureElementId(-1));
    QCOMPARE(model->getElementOfMarkedContent(7, 0), PDFContentStructureElementId(-1));
    QCOMPARE(model->getElementOfMarkedContent(-1, 0), PDFContentStructureElementId(-1));
}

void ContentStructureModelTest::readDocumentWithoutStructure()
{
    // No /StructTreeRoot: the empty model, which is valid (zadani_jadro.md 4.12)
    {
        StructureDocument d;
        const PDFDocument document = d.build();
        PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
        QVERIFY(model);
        QVERIFY(model->isEmpty());
        QVERIFY(model->getElements().empty());
        QVERIFY(model->getParentTree().empty());
        QVERIFY(model->getIssues().empty());
        QVERIFY(!model->isModified());
        QCOMPARE(model->getNextParentTreeKey(), PDFInteger(0));
        QCOMPARE(model->getElementOfMarkedContent(0, 0), PDFContentStructureElementId(-1));
        QVERIFY(model->getReadingOrder(0).empty());
        QVERIFY(model->getElement(0) == nullptr);
    }

    // /StructTreeRoot, which is not a dictionary (Table 354), is no structure
    for (const PDFObject& root : { Obj::integer(5), Obj::array({ Obj::integer(1) }), Obj::name("Root"), Obj::reference(PDFObjectReference(9999, 0)) })
    {
        StructureDocument d;
        d.setRoot(root);
        const PDFDocument document = d.build();
        PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
        QVERIFY(model);
        QVERIFY(model->isEmpty());
    }

    // No document
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(nullptr, nullptr);
    QVERIFY(model);
    QVERIFY(model->isEmpty());
}

void ContentStructureModelTest::readElementEntries()
{
    // Q has every entry of Table 355 the model keeps as a value, and others it must only keep:
    //   /A as an array of two attribute objects, the first one with its revision number 3
    //   (14.7.6.3), /C as an array of two class names, the first one with the revision 2, /R,
    //   /Custom. W has the values of wrong types: they are no values of the model, but stay.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference q = d.reserve();
    const PDFObjectReference w = d.reserve();

    const PDFObject attributes = Obj::array({ Obj::dictionary({ { "O", Obj::name("Table") }, { "Scope", Obj::name("Row") } }),
                                              Obj::integer(3),
                                              Obj::dictionary({ { "O", Obj::name("Layout") }, { "Placement", Obj::name("Block") } }) });
    const PDFObject classes = Obj::array({ Obj::name("Cls1"), Obj::integer(2), Obj::name("Cls2") });
    const PDFObject custom = Obj::dictionary({ { "X", Obj::integer(1) }, { "Y", Obj::null() } });

    // <FEFF 0048 0069> is "Hi" in UTF-16BE with the byte order mark (7.9.2.2)
    const QByteArray titleBytes = QByteArray::fromHex("FEFF00480069");

    d.set(q, Obj::element("Para", root, d.page(), Obj::integer(0),
                          { { "T", Obj::string(titleBytes) },
                            { "Lang", Obj::string("cs-CZ") },
                            { "Alt", Obj::string("alternate") },
                            { "E", Obj::string("expanded") },
                            { "ActualText", Obj::string("actual") },
                            { "ID", Obj::string("id-1") },
                            { "A", attributes },
                            { "C", classes },
                            { "R", Obj::integer(5) },
                            { "Custom", custom } }));

    d.set(w, Obj::dictionary({ { "Type", Obj::name("StructElem") },
                               { "S", Obj::integer(12) },
                               { "P", Obj::reference(root) },
                               { "T", Obj::integer(5) },
                               { "Pg", Obj::string("page") },
                               { "ID", Obj::name("Name") },
                               { "C", Obj::name("Cls1") } }));

    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(q), Obj::reference(w) }) },
                                  { "RoleMap", Obj::dictionary({ { "Para", Obj::name("P") } }) },
                                  { "ClassMap", Obj::dictionary({ { "Cls1", Obj::dictionary({ { "O", Obj::name("Layout") } }) } }) } }));
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);

    const PDFContentStructureElement* elementQ = Helper::findElement(*model, q);
    QVERIFY(elementQ);
    QCOMPARE(elementQ->type, QByteArray("Para"));
    QCOMPARE(elementQ->standardType, QByteArray("P"));
    QCOMPARE(elementQ->title.value_or(QString()), QString("Hi"));
    QCOMPARE(elementQ->language.value_or(QString()), QString("cs-CZ"));
    QCOMPARE(elementQ->alternateText.value_or(QString()), QString("alternate"));
    QCOMPARE(elementQ->expandedText.value_or(QString()), QString("expanded"));
    QCOMPARE(elementQ->actualText.value_or(QString()), QString("actual"));
    QCOMPARE(elementQ->elementId.value_or(QByteArray()), QByteArray("id-1"));
    QCOMPARE(elementQ->page, d.page());

    // Every other key as it is: /Type, /A, /C, /R, /Custom - and nothing else
    QVERIFY(elementQ->otherEntries.isDictionary());
    const PDFDictionary* otherQ = elementQ->otherEntries.getDictionary();
    QCOMPARE(otherQ->getCount(), size_t(5));
    QVERIFY(otherQ->get("Type") == Obj::name("StructElem"));
    QVERIFY(otherQ->get("A") == attributes);
    QVERIFY(otherQ->get("C") == classes);
    QVERIFY(otherQ->get("R") == Obj::integer(5));
    QVERIFY(otherQ->get("Custom") == custom);
    QVERIFY(otherQ->hasKey("Custom") && otherQ->get("Custom").getDictionary()->hasKey("Y"));

    // W: a value of a wrong type is no value of the model, and it is not lost
    const PDFContentStructureElement* elementW = Helper::findElement(*model, w);
    QVERIFY(elementW);
    QVERIFY(elementW->type.isEmpty());
    QVERIFY(!elementW->title.has_value());
    QVERIFY(!elementW->elementId.has_value());
    QVERIFY(!elementW->page.isValid());
    const PDFDictionary* otherW = elementW->otherEntries.getDictionary();
    QCOMPARE(otherW->getCount(), size_t(6));
    QVERIFY(otherW->get("S") == Obj::integer(12));
    QVERIFY(otherW->get("T") == Obj::integer(5));
    QVERIFY(otherW->get("Pg") == Obj::string("page"));
    QVERIFY(otherW->get("ID") == Obj::name("Name"));
    QVERIFY(otherW->get("C") == Obj::name("Cls1"));

    // The root keeps all its keys except /K
    const PDFDictionary* otherRoot = model->getElements().front().otherEntries.getDictionary();
    QCOMPARE(otherRoot->getCount(), size_t(3));
    QVERIFY(otherRoot->hasKey("Type") && otherRoot->hasKey("RoleMap") && otherRoot->hasKey("ClassMap"));
}

void ContentStructureModelTest::readRoleMap_data()
{
    QTest::addColumn<QByteArray>("type");
    QTest::addColumn<bool>("hasNamespace");
    QTest::addColumn<QByteArray>("standardType");

    // /RoleMap << /Para /P  /X /Y  /Y /H1  /A /B  /B /A  /Figure /Div  /U /V  /Q /Q >>
    // ISO 32000-2, 14.7.3: a type is always mapped to its name in the role map, "even if the
    // original name is one of the standard types"; NOTE 2: the chain is followed, until it finds
    // a type it recognizes or returns to one it has met.
    QTest::newRow("mapped to a standard type") << QByteArray("Para") << false << QByteArray("P");
    QTest::newRow("not in the role map") << QByteArray("Zzz") << false << QByteArray("Zzz");
    QTest::newRow("standard type not in the role map") << QByteArray("Table") << false << QByteArray("Table");
    QTest::newRow("chain of two steps") << QByteArray("X") << false << QByteArray("H1");
    QTest::newRow("second step of the chain") << QByteArray("Y") << false << QByteArray("H1");
    QTest::newRow("circle without a standard type") << QByteArray("A") << false << QByteArray("A");
    QTest::newRow("standard type remapped") << QByteArray("Figure") << false << QByteArray("Div");
    QTest::newRow("chain ends at an unknown type") << QByteArray("U") << false << QByteArray("V");
    QTest::newRow("mapped to itself") << QByteArray("Q") << false << QByteArray("Q");
    QTest::newRow("another namespace is not mapped by /RoleMap") << QByteArray("Para") << true << QByteArray("Para");

    // The chain is followed 32 steps at most (a limit of the model against a role map, which is one
    // long chain): /L0 /L1 ... /L30 /L31, /L31 /P are 32 steps from L0 to P; /M0 /M1 ... /M32 /P are
    // 33 steps from M0 (it stays as it is written) and 32 steps from M1
    QTest::newRow("chain of 32 steps") << QByteArray("L0") << false << QByteArray("P");
    QTest::newRow("chain of 33 steps") << QByteArray("M0") << false << QByteArray("M0");
    QTest::newRow("chain of 33 steps from its second type") << QByteArray("M1") << false << QByteArray("P");
}

void ContentStructureModelTest::readRoleMap()
{
    QFETCH(QByteArray, type);
    QFETCH(bool, hasNamespace);
    QFETCH(QByteArray, standardType);

    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference nameSpace = d.add(Obj::dictionary({ { "Type", Obj::name("Namespace") }, { "NS", Obj::string("http://example.com/ns") } }));
    Obj::Entries more;
    if (hasNamespace)
    {
        more.emplace_back("NS", Obj::reference(nameSpace));
    }
    const PDFObjectReference element = d.add(Obj::element(type.constData(), root, PDFObjectReference(), PDFObject(), more));

    Obj::Entries roleMap = { { "Para", Obj::name("P") },
                             { "X", Obj::name("Y") },
                             { "Y", Obj::name("H1") },
                             { "A", Obj::name("B") },
                             { "B", Obj::name("A") },
                             { "Figure", Obj::name("Div") },
                             { "U", Obj::name("V") },
                             { "Q", Obj::name("Q") } };
    for (int i = 0; i < 32; ++i)
    {
        roleMap.emplace_back("L" + QByteArray::number(i), i < 31 ? PDFObject::createName("L" + QByteArray::number(i + 1)) : Obj::name("P"));
    }
    for (int i = 0; i < 33; ++i)
    {
        roleMap.emplace_back("M" + QByteArray::number(i), i < 32 ? PDFObject::createName("M" + QByteArray::number(i + 1)) : Obj::name("P"));
    }

    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(element) },
                                  { "RoleMap", Obj::dictionary(std::move(roleMap)) } }));
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    const PDFContentStructureElement* read = Helper::findElement(*model, element);
    QVERIFY(read);
    QCOMPARE(read->type, type);
    QCOMPARE(read->standardType, standardType);
    QCOMPARE(read->otherEntries.getDictionary()->hasKey("NS"), hasNamespace);
}

void ContentStructureModelTest::readChildren()
{
    // E /Pg page /K [ 7  5 0 R (an indirect integer 8)  << /MCR /Pg page2 /Stm form /StmOwn annotation /MCID 2 >>
    //                 << /OBJR /Pg page2 /Obj annotation >>  F ]      F has no /Type (Table 355: a dictionary
    //                                                                  without /Type is a structure element)
    // G /K F2 where F2 is one element (not an array); H has /K null; I has no /K
    StructureDocument d;
    const PDFObjectReference page2 = d.addPage();
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference e = d.reserve();
    const PDFObjectReference f = d.reserve();
    const PDFObjectReference g = d.reserve();
    const PDFObjectReference f2 = d.reserve();
    const PDFObjectReference h = d.reserve();
    const PDFObjectReference i = d.reserve();
    const PDFObjectReference annotation = d.add(Obj::dictionary({ { "Type", Obj::name("Annot") } }));
    const PDFObjectReference form = d.add(Obj::stream({ { "Type", Obj::name("XObject") }, { "Subtype", Obj::name("Form") }, { "Length", Obj::integer(0) } }));
    const PDFObjectReference indirectInteger = d.add(Obj::integer(8));

    d.set(e, Obj::element("Sect", root, d.page(),
                          Obj::array({ Obj::integer(7),
                                       Obj::reference(indirectInteger),
                                       Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(page2) }, { "Stm", Obj::reference(form) }, { "StmOwn", Obj::reference(annotation) }, { "MCID", Obj::integer(2) } }),
                                       Obj::dictionary({ { "Type", Obj::name("OBJR") }, { "Pg", Obj::reference(page2) }, { "Obj", Obj::reference(annotation) } }),
                                       Obj::reference(f) })));
    d.set(f, Obj::dictionary({ { "S", Obj::name("P") }, { "P", Obj::reference(e) } }));
    d.set(g, Obj::element("Sect", root, PDFObjectReference(), Obj::reference(f2)));
    d.set(f2, Obj::element("P", g, PDFObjectReference(), PDFObject()));
    d.set(h, Obj::dictionary({ { "Type", Obj::name("StructElem") }, { "S", Obj::name("P") }, { "P", Obj::reference(root) }, { "K", Obj::null() } }));
    d.set(i, Obj::element("P", root, PDFObjectReference(), PDFObject()));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::array({ Obj::reference(e), Obj::reference(g), Obj::reference(h), Obj::reference(i) }) } }));
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QVERIFY2(model->getIssues().empty(), qPrintable(model->getIssues().empty() ? QString() : model->getIssues().front()));
    QCOMPARE(model->getElements().size(), size_t(7));

    QCOMPARE(Helper::childrenOf(*model, e), QStringList({ "M7", "M8",
                                                          QString("R2@%1/%2").arg(page2.objectNumber).arg(form.objectNumber),
                                                          QString("O%1").arg(annotation.objectNumber),
                                                          Helper::elementText(f) }));

    const PDFContentStructureElement* elementE = Helper::findElement(*model, e);
    QCOMPARE(elementE->children[2].streamOwner, annotation);
    QCOMPARE(elementE->children[3].page, page2);
    QCOMPARE(elementE->children[3].object, annotation);

    QCOMPARE(Helper::findElement(*model, f)->type, QByteArray("P"));
    QCOMPARE(Helper::findElement(*model, f)->parent, Helper::idOf(*model, e));
    QCOMPARE(Helper::childrenOf(*model, g), QStringList({ Helper::elementText(f2) }));
    QVERIFY(Helper::childrenOf(*model, h).isEmpty());
    QVERIFY(Helper::childrenOf(*model, i).isEmpty());

    // The identifier of an element is its index in the model
    for (size_t index = 0; index < model->getElements().size(); ++index)
    {
        QCOMPARE(model->getElements()[index].id, PDFContentStructureElementId(index));
    }
}

void ContentStructureModelTest::readForeignChildren()
{
    // X /Pg page /K [ null  0  (text)  << /Type /MCR /Pg page /MCID 1 /Extra 1 >>  << /Type /MCR /Pg page >>
    //                 << /Type /OBJR >>  << /Type /Page >>  << /S /P >> (a direct structure element)
    //                 99 0 R (nothing)  -4  2147483648  2147483647 ]
    // The model reads the integers (0, -4, 2147483648, 2147483647) as children; the two outside of
    // 0 ... 2^31 - 1 are issues (zadani_overeni.md 6.2). The other eight items are not children.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference x = d.reserve();
    d.set(x, Obj::element("Sect", root, d.page(),
                          Obj::array({ Obj::null(),
                                       Obj::integer(0),
                                       Obj::string("text"),
                                       Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(d.page()) }, { "MCID", Obj::integer(1) }, { "Extra", Obj::integer(1) } }),
                                       Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(d.page()) } }),
                                       Obj::dictionary({ { "Type", Obj::name("OBJR") } }),
                                       Obj::dictionary({ { "Type", Obj::name("Page") } }),
                                       Obj::dictionary({ { "S", Obj::name("P") } }),
                                       Obj::reference(PDFObjectReference(9999, 0)),
                                       Obj::integer(-4),
                                       Obj::integer(PDFInteger(2147483648LL)),
                                       Obj::integer(2147483647) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::reference(x) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(2));
    QCOMPARE(Helper::childrenOf(*model, x), QStringList({ "M0", "M-4", "M2147483648", "M2147483647" }));

    // Eight items, which are no children, and two identifiers out of range; the identifiers 0 and
    // 2147483647 have no element in the parent tree (there is none): two more issues
    QCOMPARE(model->getIssues().size(), size_t(12));

    // Identifiers out of range are never the content of a page
    QCOMPARE(model->getReadingOrder(0), (std::vector<PDFInteger>{ 0, 2147483647 }));
}

void ContentStructureModelTest::readParentTreeItems()
{
    // /Nums [ 0 [A other null A (junk) root]  1 annotation  2 << /Weird true >>  3 A  5 (text)  6 root  0 [B] ]
    //   0: the array of the page; "other" is a dictionary, which is no structure element, (junk)
    //      is a string and the structure tree root is no structure element either - all three
    //      keep their indices (the identifier 3 is A)
    //   1: a reference to an object, which is no element: not of the model
    //   2, 5: not an array and not a reference
    //   3: a reference to the element A: the parent of an object with /StructParent 3
    //   6: the structure tree root is no parent element (14.7.5.4: "the parent structure element")
    //   0 again: a key has one value (7.9.7), the first one is kept
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.reserve();
    const PDFObjectReference b = d.reserve();
    const PDFObjectReference other = d.add(Obj::dictionary({ { "Type", Obj::name("Other") } }));
    const PDFObjectReference annotation = d.add(Obj::dictionary({ { "Type", Obj::name("Annot") } }));
    const PDFObject weird = Obj::dictionary({ { "Weird", PDFObject::createBool(true) } });

    d.set(a, Obj::element("P", root, d.page(), Obj::array({ Obj::integer(0), Obj::integer(3) })));
    d.set(b, Obj::element("P", root, d.page(), PDFObject()));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(a), Obj::reference(b) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(other), Obj::null(), Obj::reference(a), Obj::string("junk"), Obj::reference(root) }),
                                                                                           Obj::integer(1), Obj::reference(annotation),
                                                                                           Obj::integer(2), weird,
                                                                                           Obj::integer(3), Obj::reference(a),
                                                                                           Obj::integer(5), Obj::string("text"),
                                                                                           Obj::integer(6), Obj::reference(root),
                                                                                           Obj::integer(0), Obj::array({ Obj::reference(b) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);

    const std::map<PDFInteger, PDFContentParentTreeItem>& tree = model->getParentTree();
    QCOMPARE(tree.size(), size_t(6));

    QVERIFY(tree.at(0).kind == PDFContentParentTreeItem::Kind::MarkedContentArray);
    QCOMPARE(tree.at(0).elements, (std::vector<PDFContentStructureElementId>{ Helper::idOf(*model, a), -1, -1, Helper::idOf(*model, a), -1, -1 }));
    QCOMPARE(model->getElementOfMarkedContent(0, 0), Helper::idOf(*model, a));
    QCOMPARE(model->getElementOfMarkedContent(0, 1), PDFContentStructureElementId(-1));
    QCOMPARE(model->getElementOfMarkedContent(0, 3), Helper::idOf(*model, a));
    QCOMPARE(model->getElementOfMarkedContent(0, 5), PDFContentStructureElementId(-1));
    QVERIFY(tree.at(6).kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(tree.at(6).raw == Obj::reference(root));
    QVERIFY(tree.at(6).elements.empty());

    QVERIFY(tree.at(1).kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(tree.at(1).raw == Obj::reference(annotation));
    QVERIFY(tree.at(1).elements.empty());
    QVERIFY(tree.at(2).kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(tree.at(2).raw == weird);
    QVERIFY(tree.at(3).kind == PDFContentParentTreeItem::Kind::Element);
    QCOMPARE(Helper::parentTreeOf(*model, 3), Helper::numbers({ a }));
    QVERIFY(tree.at(5).kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(tree.at(5).raw == Obj::string("text"));

    // One issue: the key 0 is there twice
    QCOMPARE(model->getIssues().size(), size_t(1));
    QVERIFY(!tree.at(0).isModified);
}

void ContentStructureModelTest::readNestedParentTree()
{
    // The parent tree as a tree of two levels (Table 37): the root has /Kids [n1 n2], n1 has
    // /Nums [0 [A] 1 [null A]], n2 has /Nums [7 A]
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.reserve();
    const PDFObjectReference n1 = d.add(Obj::dictionary({ { "Limits", pdftest::ContentTestDocuments::createIntegerArrayObject({ 0, 1 }) },
                                                          { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a) }),
                                                                                 Obj::integer(1), Obj::array({ Obj::null(), Obj::reference(a) }) }) } }));
    const PDFObjectReference n2 = d.add(Obj::dictionary({ { "Limits", pdftest::ContentTestDocuments::createIntegerArrayObject({ 7, 7 }) },
                                                          { "Nums", Obj::array({ Obj::integer(7), Obj::reference(a) }) } }));
    d.set(a, Obj::element("P", root, d.page(), Obj::integer(0)));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(a) },
                                  { "ParentTree", Obj::dictionary({ { "Kids", Obj::array({ Obj::reference(n1), Obj::reference(n2) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QVERIFY(model->getIssues().empty());
    QCOMPARE(model->getParentTree().size(), size_t(3));
    QCOMPARE(Helper::parentTreeOf(*model, 0), Helper::numbers({ a }));
    QCOMPARE(Helper::parentTreeOf(*model, 1), Helper::numbers({ PDFObjectReference(), a }));
    QCOMPARE(Helper::parentTreeOf(*model, 7), Helper::numbers({ a }));
    QVERIFY(model->getParentTree().at(7).kind == PDFContentParentTreeItem::Kind::Element);
    QCOMPARE(model->getElementOfMarkedContent(1, 1), Helper::idOf(*model, a));
}

void ContentStructureModelTest::readNextParentTreeKey_data()
{
    QTest::addColumn<int>("largestKey");            // -1 = no parent tree
    QTest::addColumn<int>("nextKey");               // -1 = no /ParentTreeNextKey
    QTest::addColumn<PDFInteger>("expected");

    // Table 354: /ParentTreeNextKey is "an integer greater than any key in the parent tree"; a
    // missing or too small one is replaced by one above the largest key
    QTest::newRow("as written") << 1 << 2 << PDFInteger(2);
    QTest::newRow("larger than needed") << 1 << 50 << PDFInteger(50);
    QTest::newRow("missing") << 7 << -1 << PDFInteger(8);
    QTest::newRow("too small") << 7 << 3 << PDFInteger(8);
    QTest::newRow("equal to the largest key") << 7 << 7 << PDFInteger(8);
    QTest::newRow("no tree and no key") << -1 << -1 << PDFInteger(0);
    QTest::newRow("no tree") << -1 << 4 << PDFInteger(4);
}

void ContentStructureModelTest::readNextParentTreeKey()
{
    QFETCH(int, largestKey);
    QFETCH(int, nextKey);
    QFETCH(PDFInteger, expected);

    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, d.page(), PDFObject()));

    Obj::Entries entries = { { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::reference(a) } };
    if (largestKey >= 0)
    {
        entries.emplace_back("ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a) }),
                                                                                    Obj::integer(largestKey), Obj::reference(a) }) } }));
    }
    if (nextKey >= 0)
    {
        entries.emplace_back("ParentTreeNextKey", Obj::integer(nextKey));
    }
    d.set(root, Obj::dictionary(std::move(entries)));
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getNextParentTreeKey(), expected);
}

void ContentStructureModelTest::readCycleAndSharedElement()
{
    // root /K [A D];  A /K [B];  B /K [A C]  (A is an ancestor of B: a cycle);
    // D /K [C E]  (C is a child of B already: shared by two parents);  E /K [E]  (its own child)
    // Each element is in the model once, at its first parent in the order of the document
    // (zadani_overeni.md 6.1): root, A, D, B, C, E - six elements.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.reserve();
    const PDFObjectReference b = d.reserve();
    const PDFObjectReference c = d.reserve();
    const PDFObjectReference dd = d.reserve();
    const PDFObjectReference e = d.reserve();
    d.set(a, Obj::element("Sect", root, PDFObjectReference(), Obj::array({ Obj::reference(b) })));
    d.set(b, Obj::element("Sect", a, PDFObjectReference(), Obj::array({ Obj::reference(a), Obj::reference(c) })));
    d.set(c, Obj::element("P", b, PDFObjectReference(), PDFObject()));
    d.set(dd, Obj::element("Sect", root, PDFObjectReference(), Obj::array({ Obj::reference(c), Obj::reference(e) })));
    d.set(e, Obj::element("P", dd, PDFObjectReference(), Obj::array({ Obj::reference(e) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::array({ Obj::reference(a), Obj::reference(dd) }) } }));
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(6));

    QCOMPARE(Helper::childrenOf(*model, root), QStringList({ Helper::elementText(a), Helper::elementText(dd) }));
    QCOMPARE(Helper::childrenOf(*model, a), QStringList({ Helper::elementText(b) }));
    QCOMPARE(Helper::childrenOf(*model, b), QStringList({ Helper::elementText(c) }));
    QCOMPARE(Helper::childrenOf(*model, dd), QStringList({ Helper::elementText(e) }));
    QVERIFY(Helper::childrenOf(*model, e).isEmpty());

    QCOMPARE(Helper::findElement(*model, c)->parent, Helper::idOf(*model, b));
    QCOMPARE(Helper::findElement(*model, a)->parent, PDFContentStructureElementId(0));
    QCOMPARE(Helper::findElement(*model, e)->parent, Helper::idOf(*model, dd));

    // Three refused children: A in B, C in D, E in E
    QCOMPARE(model->getIssues().size(), size_t(3));
}

void ContentStructureModelTest::readElementSharedByManyPaths()
{
    // 16 levels of two elements; each element lists both elements of the next level, the root
    // lists both elements of the first one. Every element of the last level is reached by 2^15
    // paths; a walk without one visited set for the whole walk reads 2^17 - 2 elements, the
    // model has 2 * 16 elements and the root.
    constexpr int LEVELS = 16;
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    std::vector<std::array<PDFObjectReference, 2>> levels(LEVELS);
    for (auto& level : levels)
    {
        level = { d.reserve(), d.reserve() };
    }
    for (int i = 0; i < LEVELS; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            const PDFObject kids = i + 1 < LEVELS ? Obj::array({ Obj::reference(levels[size_t(i) + 1][0]), Obj::reference(levels[size_t(i) + 1][1]) }) : PDFObject();
            d.set(levels[size_t(i)][size_t(j)], Obj::element("Sect", i == 0 ? root : levels[size_t(i) - 1][0], PDFObjectReference(), kids));
        }
    }
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::array({ Obj::reference(levels[0][0]), Obj::reference(levels[0][1]) }) } }));
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(1 + 2 * LEVELS));

    // The first element of a level has both elements of the next level, the second one none
    // (they belong to the first one)
    QCOMPARE(Helper::childrenOf(*model, levels[3][0]), QStringList({ Helper::elementText(levels[4][0]), Helper::elementText(levels[4][1]) }));
    QVERIFY(Helper::childrenOf(*model, levels[3][1]).isEmpty());
}

void ContentStructureModelTest::readDepthLimit_data()
{
    QTest::addColumn<int>("depth");
    QTest::addColumn<int>("elementCount");  // With the root
    QTest::addColumn<bool>("isComplete");

    // PDFContentStructureModel::MAXIMUM_DEPTH is 256: the root has the depth 0, a chain of 256
    // nested elements is read whole, of a chain of 257 the last element is not read
    QTest::newRow("at the limit") << 256 << 257 << true;
    QTest::newRow("one over the limit") << 257 << 257 << false;
    QTest::newRow("far over the limit") << 2000 << 257 << false;
}

void ContentStructureModelTest::readDepthLimit()
{
    QFETCH(int, depth);
    QFETCH(int, elementCount);
    QFETCH(bool, isComplete);

    QCOMPARE(PDFContentStructureModel::MAXIMUM_DEPTH, 256);

    // root /K e1;  e1 /K e2;  ...;  the last element has /Pg page /K 0;  /ParentTree /Nums [0 [last]]
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    std::vector<PDFObjectReference> chain;
    for (int i = 0; i < depth; ++i)
    {
        chain.push_back(d.reserve());
    }
    for (int i = 0; i < depth; ++i)
    {
        const bool isLast = i + 1 == depth;
        d.set(chain[size_t(i)], Obj::element("Sect", i == 0 ? root : chain[size_t(i) - 1], isLast ? d.page() : PDFObjectReference(),
                                             isLast ? Obj::integer(0) : Obj::reference(chain[size_t(i) + 1])));
    }
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(chain.front()) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(chain.back()) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(elementCount));
    QCOMPARE(model->getIssues().empty(), isComplete);

    // A model, which did not read the whole tree, refuses to change the structure: what it would
    // write, would not know the unread elements
    const PDFContentStructureChange change = model->getChangeForDelete(0, 0, true);
    if (isComplete)
    {
        QVERIFY(change.action == PDFContentStructureChange::Action::RemoveSequence);
        QCOMPARE(change.element, Helper::idOf(*model, chain.back()));
        QVERIFY(model->applied({ change })->isModified());
    }
    else
    {
        QVERIFY(change.action == PDFContentStructureChange::Action::Refused);
        QVERIFY(!change.text.isEmpty());
        QVERIFY(model->getChangeForReorder(0, 0, false, 0).action == PDFContentStructureChange::Action::Refused);
        QVERIFY(model->getChangeForMoveOut(0, 0, false, true, 0).action == PDFContentStructureChange::Action::Refused);
        QVERIFY(!model->applied({ Helper::removal(0, 0) })->isModified());

        // What needs no change of the structure is not refused
        QVERIFY(model->getChangeForDelete(0, 0, false).action == PDFContentStructureChange::Action::None);
        QVERIFY(model->getChangeForReorder(0, 0, true, 0).action == PDFContentStructureChange::Action::KeepWithWrap);
    }
}

void ContentStructureModelTest::readParentTreeDepthLimit_data()
{
    QTest::addColumn<int>("depth");
    QTest::addColumn<bool>("isComplete");

    // PDFContentStructureModel::MAXIMUM_NUMBER_TREE_DEPTH is 32 (zadani_overeni.md 6.1): the root
    // node has the depth 0
    QTest::newRow("leaf at the depth 32") << 32 << true;
    QTest::newRow("leaf at the depth 33") << 33 << false;
}

void ContentStructureModelTest::readParentTreeDepthLimit()
{
    QFETCH(int, depth);
    QFETCH(bool, isComplete);

    QCOMPARE(PDFContentStructureModel::MAXIMUM_NUMBER_TREE_DEPTH, 32);

    // The parent tree is a chain of nodes with one kid each; the node at \p depth is the leaf
    // /Nums [0 [A]]
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, d.page(), Obj::integer(0)));
    PDFObject node = Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a) }) }) } });
    for (int i = 0; i < depth; ++i)
    {
        node = Obj::dictionary({ { "Kids", Obj::array({ Obj::reference(d.add(node)) }) } });
    }
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::reference(a) }, { "ParentTree", node } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getParentTree().size(), size_t(isComplete ? 1 : 0));
    QCOMPARE(model->getElementOfMarkedContent(0, 0), isComplete ? Helper::idOf(*model, a) : PDFContentStructureElementId(-1));
    QVERIFY((model->getChangeForDelete(0, 0, true).action == PDFContentStructureChange::Action::Refused) == !isComplete);
    QVERIFY(model->getIssues().empty() == isComplete);
}

void ContentStructureModelTest::readParentTreeCycle()
{
    // The root node of the parent tree lists itself and its leaf twice: /Kids [tree leaf leaf tree],
    // the leaf has /Nums [0 [A]]. Each node is read once: one key, no second value of the key 0.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference tree = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, d.page(), Obj::integer(0)));
    const PDFObjectReference leaf = d.add(Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a) }) }) } }));
    d.set(tree, Obj::dictionary({ { "Kids", Obj::array({ Obj::reference(tree), Obj::reference(leaf), Obj::reference(leaf), Obj::reference(tree) }) } }));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::reference(a) }, { "ParentTree", Obj::reference(tree) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getParentTree().size(), size_t(1));
    QCOMPARE(Helper::parentTreeOf(*model, 0), Helper::numbers({ a }));
    QVERIFY2(model->getIssues().empty(), "a node read once gives no key twice");
    QVERIFY(model->getChangeForDelete(0, 0, true).action == PDFContentStructureChange::Action::RemoveSequence);
}

void ContentStructureModelTest::readBudgetOfStoredItems_data()
{
    QTest::addColumn<int>("elementCount");          // Elements listed by the root, all with /K kids
    QTest::addColumn<int>("kidCount");              // Integers of the one array kids
    QTest::addColumn<int>("keyCount");              // Keys 0 ... keyCount - 1 of the parent tree, all with the value nulls
    QTest::addColumn<int>("nullCount");             // Nulls of the one array nulls
    QTest::addColumn<bool>("isComplete");

    // zadani_overeni.md 6.2: the structure is read up to PDFObjectWalk::DEFAULT_MAXIMUM_NODES
    // (2^24 = 16 777 216). One budget counts every stored child of an element (the root lists the
    // elements: elementCount children, each element kidCount children) and every stored item of a
    // parent tree array (keyCount * nullCount), so an array shared by many elements or keys costs
    // what the model stores, not what the document holds.
    //   both parts: 1 001 000 + 15 000 000 = 16 001 000           <= 2^24: complete
    //   both parts: 1 001 000 + 3 852 * 4 096 = 16 778 792        >  2^24, each part alone below it
    //   parent tree: 2 + 4 097 * 4 096 = 16 781 314                >  2^24
    QTest::newRow("within the budget") << 1000 << 1000 << 3000 << 5000 << true;
    QTest::newRow("children and parent tree over the budget") << 1000 << 1000 << 3852 << 4096 << false;
    QTest::newRow("shared parent tree array over the budget") << 1 << 1 << 4097 << 4096 << false;
    QTest::newRow("shared kids array over the budget") << 4097 << 4096 << 1 << 1 << false;
}

void ContentStructureModelTest::readBudgetOfStoredItems()
{
    QFETCH(int, elementCount);
    QFETCH(int, kidCount);
    QFETCH(int, keyCount);
    QFETCH(int, nullCount);
    QFETCH(bool, isComplete);

    QCOMPARE(PDFObjectWalk::DEFAULT_MAXIMUM_NODES, qint64(16777216));

    // root /K [e1 ... eN];  each e /K kids (no /Pg: the integers are no content of a known page);
    // kids = [0 1 ... kidCount - 1];  /ParentTree /Nums [0 nulls 1 nulls ...];  nulls = [null ...]
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    std::vector<PDFObject> kidItems;
    for (int i = 0; i < kidCount; ++i)
    {
        kidItems.push_back(Obj::integer(i));
    }
    const PDFObjectReference kids = d.add(Obj::array(std::move(kidItems)));
    const PDFObjectReference nulls = d.add(Obj::array(std::vector<PDFObject>(size_t(nullCount), Obj::null())));

    std::vector<PDFObject> elements;
    for (int i = 0; i < elementCount; ++i)
    {
        elements.push_back(Obj::reference(d.add(Obj::element("P", root, PDFObjectReference(), Obj::reference(kids)))));
    }

    std::vector<PDFObject> numbers;
    for (int i = 0; i < keyCount; ++i)
    {
        numbers.push_back(Obj::integer(i));
        numbers.push_back(Obj::reference(nulls));
    }

    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array(std::move(elements)) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array(std::move(numbers)) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    // The time is that of storing at most 2^24 items: seconds even in a debug build
    const int timeoutFactor = qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QElapsedTimer timer;
    timer.start();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QVERIFY2(timer.elapsed() < qint64(20000) * timeoutFactor, qPrintable(QString("read in %1 ms").arg(timer.elapsed())));

    // A model, which did not store the whole structure, says so and refuses to change it
    QCOMPARE(model->getIssues().empty(), isComplete);
    const PDFContentStructureChange change = model->getChangeForDelete(0, 0, true);
    if (isComplete)
    {
        QCOMPARE(model->getElements().size(), size_t(1 + elementCount));
        QCOMPARE(model->getParentTree().size(), size_t(keyCount));
        QVERIFY(change.action == PDFContentStructureChange::Action::RemoveSequence);
    }
    else
    {
        QVERIFY(change.action == PDFContentStructureChange::Action::Refused);
        QVERIFY(!change.text.isEmpty());
        QVERIFY(model->getChangeForReorder(0, 0, false, 0).action == PDFContentStructureChange::Action::Refused);
    }
}

void ContentStructureModelTest::readIrregularContent()
{
    // A /Pg page /K [0 1];  B /Pg page /K [1 2];  C /Pg page /K 3;  /Nums [0 [A A B A]]
    //   identifier 1: a child of A and of B (zadani_overeni.md 6.2: a duplicate identifier is a
    //                 problem of the reading)
    //   identifier 3: a child of C, but the parent tree says A
    // Both are issues. When their content is deleted, every child, which names it, goes.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.reserve();
    const PDFObjectReference b = d.reserve();
    const PDFObjectReference c = d.reserve();
    d.set(a, Obj::element("P", root, d.page(), Obj::array({ Obj::integer(0), Obj::integer(1) })));
    d.set(b, Obj::element("P", root, d.page(), Obj::array({ Obj::integer(1), Obj::integer(2) })));
    d.set(c, Obj::element("P", root, d.page(), Obj::integer(3)));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(a), Obj::reference(b), Obj::reference(c) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(a), Obj::reference(b), Obj::reference(a) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getIssues().size(), size_t(2));

    // The parent tree is what finds the element of a content (14.7.5.4)
    QCOMPARE(model->getElementOfMarkedContent(0, 1), Helper::idOf(*model, a));
    QCOMPARE(model->getElementOfMarkedContent(0, 3), Helper::idOf(*model, a));

    // The identifier 1 is listed twice: it is in the reading order once, at its first place
    QCOMPARE(model->getReadingOrder(0), (std::vector<PDFInteger>{ 0, 1, 2, 3 }));

    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 1, true), model->getChangeForDelete(0, 3, true) });
    QCOMPARE(Helper::childrenOf(*changed, a), QStringList({ "M0" }));
    QCOMPARE(Helper::childrenOf(*changed, b), QStringList({ "M2" }));
    QVERIFY(Helper::childrenOf(*changed, c).isEmpty());
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ a, PDFObjectReference(), b, PDFObjectReference() }));
    QVERIFY(Helper::findElement(*changed, a)->isModified);
    QVERIFY(Helper::findElement(*changed, b)->isModified);
    QVERIFY(Helper::findElement(*changed, c)->isModified);

    // The regular identifier 2: only its element changes
    PDFContentStructureModelPointer regular = model->applied({ model->getChangeForDelete(0, 2, true) });
    QCOMPARE(Helper::childrenOf(*regular, b), QStringList({ "M1" }));
    QCOMPARE(Helper::childrenOf(*regular, a), QStringList({ "M0", "M1" }));
    QVERIFY(!Helper::findElement(*regular, a)->isModified);
}

void ContentStructureModelTest::readCanceled()
{
    const TaggedDocument t = Helper::createTagged();

    // Canceled before the start, and in the middle (the document has six elements and two keys of
    // the parent tree: the reading asks more than three times)
    CancelAfter canceledAtOnce(0);
    QVERIFY(PDFContentStructureModel::read(&t.document, &canceledAtOnce) == nullptr);

    CancelAfter canceledInTheMiddle(3);
    QVERIFY(PDFContentStructureModel::read(&t.document, &canceledInTheMiddle) == nullptr);

    // A control, which never cancels, changes nothing
    CancelAfter notCanceled(1000000);
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, &notCanceled);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(6));
}

void ContentStructureModelTest::readingOrder()
{
    // Two pages: page (/StructParents 0) and page2 (/StructParents 5).
    //   root /K [Doc];  Doc /K [F P T]
    //   F /Pg page /K [3]
    //   P /Pg page /K [ 0  S  2  << /MCR /Pg page2 /MCID 7 >> ]
    //   S /Pg page /K [1]
    //   T /Pg page2 /K [ 0  << /MCR /Pg page /MCID 4 >> ]
    // The reading order of a page is the order of its identifiers in the tree, depth first:
    //   page:  3 (F), 0 (P), 1 (S inside P), 2 (P), 4 (T)        page2:  7 (P), 0 (T)
    StructureDocument d;
    const PDFObjectReference page2 = d.addPage();
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference doc = d.reserve();
    const PDFObjectReference f = d.reserve();
    const PDFObjectReference p = d.reserve();
    const PDFObjectReference s = d.reserve();
    const PDFObjectReference t = d.reserve();
    d.set(doc, Obj::element("Document", root, PDFObjectReference(), Obj::array({ Obj::reference(f), Obj::reference(p), Obj::reference(t) })));
    d.set(f, Obj::element("Figure", doc, d.page(), Obj::array({ Obj::integer(3) })));
    d.set(p, Obj::element("P", doc, d.page(), Obj::array({ Obj::integer(0), Obj::reference(s), Obj::integer(2), Obj::markedContentReference(page2, 7) })));
    d.set(s, Obj::element("Span", p, d.page(), Obj::array({ Obj::integer(1) })));
    d.set(t, Obj::element("P", doc, page2, Obj::array({ Obj::integer(0), Obj::markedContentReference(d.page(), 4) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::array({ Obj::reference(doc) }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setStructuralParents(page2, 5);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getReadingOrder(0), (std::vector<PDFInteger>{ 3, 0, 1, 2, 4 }));
    QCOMPARE(model->getReadingOrder(5), (std::vector<PDFInteger>{ 7, 0 }));
    QVERIFY(model->getReadingOrder(1).empty());
    QVERIFY(model->getReadingOrder(-1).empty());
}

void ContentStructureModelTest::contentOfStream()
{
    // A form XObject with marked content of its own has /StructParents itself (14.7.5.4, Table 359);
    // a marked-content reference names it by /Stm (Table 357): "the content stream containing the
    // marked-content sequence". The identifier 0 of the form (key 2) and the identifier 0 of the
    // page (key 0) are two contents.
    //   X /Pg page /K [ 0  << /MCR /Pg page /Stm form /StmOwn owner /MCID 0 >>  << /OBJR /Pg page2 /Obj owner >> ]
    //   /Nums [0 [X] 2 [X]]
    StructureDocument d;
    const PDFObjectReference page2 = d.addPage();
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference x = d.reserve();
    const PDFObjectReference owner = d.add(Obj::dictionary({ { "Type", Obj::name("Annot") } }));
    const PDFObjectReference form = d.add(Obj::stream({ { "Type", Obj::name("XObject") }, { "Subtype", Obj::name("Form") }, { "StructParents", Obj::integer(2) }, { "Length", Obj::integer(0) } }));
    d.set(x, Obj::element("P", root, d.page(),
                          Obj::array({ Obj::integer(0),
                                       Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(d.page()) }, { "Stm", Obj::reference(form) }, { "StmOwn", Obj::reference(owner) }, { "MCID", Obj::integer(0) } }),
                                       Obj::dictionary({ { "Type", Obj::name("OBJR") }, { "Pg", Obj::reference(page2) }, { "Obj", Obj::reference(owner) } }) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(x) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(x) }),
                                                                                           Obj::integer(2), Obj::array({ Obj::reference(x) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QVERIFY2(model->getIssues().empty(), qPrintable(model->getIssues().empty() ? QString() : model->getIssues().front()));
    QCOMPARE(model->getReadingOrder(0), (std::vector<PDFInteger>{ 0 }));
    QCOMPARE(model->getReadingOrder(2), (std::vector<PDFInteger>{ 0 }));

    // The content of the form is deleted: the reference goes, the identifier of the page stays
    const QString objectText = QString("O%1").arg(owner.objectNumber);
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(2, 0, true) });
    QCOMPARE(Helper::childrenOf(*changed, x), QStringList({ "M0", objectText }));
    QCOMPARE(Helper::parentTreeOf(*changed, 2), Helper::numbers({ PDFObjectReference() }));
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ x }));
    QVERIFY(!changed->getParentTree().at(0).isModified);

    // And the other way round
    PDFContentStructureModelPointer changedPage = model->applied({ model->getChangeForDelete(0, 0, true) });
    QCOMPARE(Helper::childrenOf(*changedPage, x), QStringList({ QString("R0@%1/%2").arg(d.page().objectNumber).arg(form.objectNumber), objectText }));
    QCOMPARE(Helper::parentTreeOf(*changedPage, 0), Helper::numbers({ PDFObjectReference() }));
    QCOMPARE(Helper::parentTreeOf(*changedPage, 2), Helper::numbers({ x }));

    // Written: /K [ << /Type /MCR /Pg page /Stm form /StmOwn owner /MCID 0 >> << /Type /OBJR /Pg page2 /Obj owner >> ]
    // - every key of Table 357 and of Table 358 the references had
    PDFDocumentBuilder builder(&document);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changedPage).isWritten);
    const PDFObject kids = Helper::dictionaryOf(builder, x)->get("K");
    QCOMPARE(Helper::itemsOf(kids), QStringList({ "<<...>>", "<<...>>" }));

    const PDFDictionary* contentReference = kids.getArray()->getItem(0).getDictionary();
    QCOMPARE(Helper::keysOf(contentReference), QStringList({ "Type", "Pg", "Stm", "StmOwn", "MCID" }));
    QVERIFY(contentReference->get("Type") == Obj::name("MCR"));
    QVERIFY(contentReference->get("Pg") == Obj::reference(d.page()));
    QVERIFY(contentReference->get("Stm") == Obj::reference(form));
    QVERIFY(contentReference->get("StmOwn") == Obj::reference(owner));
    QVERIFY(contentReference->get("MCID") == Obj::integer(0));

    const PDFDictionary* objectReference = kids.getArray()->getItem(1).getDictionary();
    QCOMPARE(Helper::keysOf(objectReference), QStringList({ "Type", "Pg", "Obj" }));
    QVERIFY(objectReference->get("Type") == Obj::name("OBJR"));
    QVERIFY(objectReference->get("Pg") == Obj::reference(page2));
    QVERIFY(objectReference->get("Obj") == Obj::reference(owner));
}

void ContentStructureModelTest::allocateMarkedContentId_data()
{
    QTest::addColumn<PDFInteger>("key");
    QTest::addColumn<PDFInteger>("largestUsedInContent");
    QTest::addColumn<PDFInteger>("expected");

    // The document of createTagged: the array of the key 0 is [P Span null Figure] - four indices
    // are used, also the one of the null (the identifier is the index, 14.7.5.4); the key 1 is an
    // object and has no identifiers; the key 7 is not in the tree.
    QTest::newRow("above the array") << PDFInteger(0) << PDFInteger(-1) << PDFInteger(4);
    QTest::newRow("content as large as the array") << PDFInteger(0) << PDFInteger(3) << PDFInteger(4);
    QTest::newRow("above the content") << PDFInteger(0) << PDFInteger(10) << PDFInteger(11);
    QTest::newRow("key of an object") << PDFInteger(1) << PDFInteger(-1) << PDFInteger(0);
    QTest::newRow("unknown key") << PDFInteger(7) << PDFInteger(-1) << PDFInteger(0);
    QTest::newRow("unknown key with content") << PDFInteger(7) << PDFInteger(5) << PDFInteger(6);
    QTest::newRow("no key") << PDFInteger(-1) << PDFInteger(3) << PDFInteger(4);

    // The largest identifier is 2^31 - 1 (MAXIMUM_MARKED_CONTENT_ID): nothing is above it
    QTest::newRow("last identifier") << PDFInteger(7) << PDFInteger(2147483646) << PDFInteger(2147483647);
    QTest::newRow("no identifier left") << PDFInteger(7) << PDFInteger(2147483647) << PDFInteger(-1);
    QTest::newRow("content out of range") << PDFInteger(7) << PDFInteger(4294967296LL) << PDFInteger(-1);

    // The array would get nulls up to the new identifier: at most 65 536 of them. The array has
    // 4 items, so the largest new identifier is 4 + 65 536 = 65 540.
    QTest::newRow("largest gap") << PDFInteger(0) << PDFInteger(65539) << PDFInteger(65540);
    QTest::newRow("gap too large") << PDFInteger(0) << PDFInteger(65540) << PDFInteger(-1);
    QTest::newRow("far too sparse") << PDFInteger(0) << PDFInteger(2147483646) << PDFInteger(-1);
}

void ContentStructureModelTest::allocateMarkedContentId()
{
    QFETCH(PDFInteger, key);
    QFETCH(PDFInteger, largestUsedInContent);
    QFETCH(PDFInteger, expected);

    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->allocateMarkedContentId(key, largestUsedInContent), expected);
}

void ContentStructureModelTest::allocateAboveChildren()
{
    // A /Pg page /K [0 9];  /Nums [0 [A]]: the identifier 9 is named by a child, though the array
    // has one item and the content of the page may not use it. A new sequence with the identifier
    // 1 ... 9 could become a content of A by that child, so the new identifier is 10.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, d.page(), Obj::array({ Obj::integer(0), Obj::integer(9) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(a) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->allocateMarkedContentId(0, 0), PDFInteger(10));
    QCOMPARE(model->getChangeForReorder(0, 0, false, 0).newMcid, PDFInteger(10));

    // B /Pg page /K 0;  /Nums [0 [B null null]]: no child names the identifiers 1 and 2 and the
    // content may not use them, but they are indices of the array (the header: "its null items
    // count as used indices up to its length") - the new identifier is 3
    StructureDocument d2;
    const PDFObjectReference root2 = d2.reserve();
    const PDFObjectReference b = d2.add(Obj::element("P", root2, d2.page(), Obj::integer(0)));
    d2.set(root2, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                    { "K", Obj::reference(b) },
                                    { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(b), Obj::null(), Obj::null() }) }) } }) } }));
    d2.setStructuralParents(d2.page(), 0);
    d2.setRoot(Obj::reference(root2));
    const PDFDocument document2 = d2.build();

    PDFContentStructureModelPointer model2 = PDFContentStructureModel::read(&document2, nullptr);
    QVERIFY(model2);
    QCOMPARE(model2->allocateMarkedContentId(0, 0), PDFInteger(3));
    QCOMPARE(model2->getChangeForMoveOut(0, 0, false, true, 0).newMcid, PDFInteger(3));
}

// ---------------------------------------------------------------------------------------------
//  Consistency
// ---------------------------------------------------------------------------------------------

void ContentStructureModelTest::deleteInsideSequence()
{
    // Row 1: content inside a sequence, which stays not empty, is deleted - nothing happens
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    const PDFContentStructureChange change = model->getChangeForDelete(0, 0, false);
    QVERIFY(change.action == PDFContentStructureChange::Action::None);

    PDFContentStructureModelPointer changed = model->applied({ change });
    QVERIFY(changed);
    QVERIFY(changed != model);
    QVERIFY(!changed->isModified());
    QCOMPARE(Helper::childrenOf(*changed, t.paragraph), QStringList({ "M0" }));
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure }));
}

void ContentStructureModelTest::deleteLastContent()
{
    // Row 2: the last content of a sequence is deleted - the child of the element is removed,
    // ParentTree[k][n] = null, the element without children stays
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    const PDFContentStructureChange change = model->getChangeForDelete(0, 0, true);
    QVERIFY(change.action == PDFContentStructureChange::Action::RemoveSequence);
    QCOMPARE(change.structuralParentKey, PDFInteger(0));
    QCOMPARE(change.mcid, PDFInteger(0));
    QCOMPARE(change.newMcid, PDFInteger(-1));
    QCOMPARE(change.element, Helper::idOf(*model, t.paragraph));
    QVERIFY(change.text.isEmpty());

    PDFContentStructureModelPointer changed = model->applied({ change });
    QVERIFY(changed->isModified());
    QCOMPARE(changed->getElements().size(), size_t(6));
    QVERIFY(Helper::childrenOf(*changed, t.paragraph).isEmpty());
    QVERIFY(Helper::findElement(*changed, t.paragraph)->isModified);
    QCOMPARE(Helper::childrenOf(*changed, t.documentElement).size(), qsizetype(4));

    // The array keeps its length: the null is at the index of the identifier
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ PDFObjectReference(), t.span, PDFObjectReference(), t.figure }));
    QVERIFY(changed->getParentTree().at(0).isModified);
    QCOMPARE(changed->getElementOfMarkedContent(0, 0), PDFContentStructureElementId(-1));
    QCOMPARE(changed->getElementOfMarkedContent(0, 1), Helper::idOf(*changed, t.span));

    // Nothing else is touched
    for (const PDFObjectReference& reference : { t.root, t.documentElement, t.span, t.figure, t.link })
    {
        QVERIFY(!Helper::findElement(*changed, reference)->isModified);
    }
    QVERIFY(!changed->getParentTree().at(1).isModified);

    // The identifier stays used: a new one is above the array
    QCOMPARE(changed->allocateMarkedContentId(0, -1), PDFInteger(4));

    // The model is immutable: the old one is as it was (it is the state of Undo)
    QVERIFY(!model->isModified());
    QCOMPARE(Helper::childrenOf(*model, t.paragraph), QStringList({ "M0" }));
    QCOMPARE(Helper::parentTreeOf(*model, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure }));
    QVERIFY(!model->getParentTree().at(0).isModified);

    // The same for the content named by a marked-content reference (Span) and by an integer in an
    // array (Figure)
    PDFContentStructureModelPointer changedSpan = model->applied({ model->getChangeForDelete(0, 1, true), model->getChangeForDelete(0, 3, true) });
    QVERIFY(Helper::childrenOf(*changedSpan, t.span).isEmpty());
    QVERIFY(Helper::childrenOf(*changedSpan, t.figure).isEmpty());
    QCOMPARE(Helper::childrenOf(*changedSpan, t.paragraph), QStringList({ "M0" }));
    QCOMPARE(Helper::parentTreeOf(*changedSpan, 0), Helper::numbers({ t.paragraph, PDFObjectReference(), PDFObjectReference(), PDFObjectReference() }));
}

void ContentStructureModelTest::moveOutOfSequence()
{
    // Row 3: a node is dragged out of its sequence in the tree - it is not tagged any more; in
    // the middle of the sequence the sequence is split
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    // The first or the last node of a sequence with more content: the sequence only loses it
    const PDFContentStructureChange untag = model->getChangeForMoveOut(0, 0, false, false, 3);
    QVERIFY(untag.action == PDFContentStructureChange::Action::Untag);
    QCOMPARE(untag.mcid, PDFInteger(0));
    QCOMPARE(untag.element, Helper::idOf(*model, t.paragraph));
    QVERIFY(!model->applied({ untag })->isModified());

    // The only content of the sequence: the sequence ends as with a deletion
    const PDFContentStructureChange removal = model->getChangeForMoveOut(0, 0, true, false, 3);
    QVERIFY(removal.action == PDFContentStructureChange::Action::RemoveSequence);
    QCOMPARE(removal.element, Helper::idOf(*model, t.paragraph));
    PDFContentStructureModelPointer removed = model->applied({ removal });
    QVERIFY(Helper::childrenOf(*removed, t.paragraph).isEmpty());
    QCOMPARE(Helper::parentTreeOf(*removed, 0), Helper::numbers({ PDFObjectReference(), t.span, PDFObjectReference(), t.figure }));

    // In the middle: the rest behind the node gets a new identifier of the same element. The
    // array has four items and the content uses the identifiers up to 3: the new one is 4.
    const PDFContentStructureChange split = model->getChangeForMoveOut(0, 0, false, true, 3);
    QVERIFY(split.action == PDFContentStructureChange::Action::SplitSequence);
    QCOMPARE(split.structuralParentKey, PDFInteger(0));
    QCOMPARE(split.mcid, PDFInteger(0));
    QCOMPARE(split.newMcid, PDFInteger(4));
    QCOMPARE(split.element, Helper::idOf(*model, t.paragraph));

    PDFContentStructureModelPointer splitModel = model->applied({ split });
    QVERIFY(splitModel->isModified());
    QCOMPARE(Helper::childrenOf(*splitModel, t.paragraph), QStringList({ "M0", "M4" }));
    QCOMPARE(Helper::parentTreeOf(*splitModel, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure, t.paragraph }));
    QCOMPARE(splitModel->getElementOfMarkedContent(0, 4), Helper::idOf(*splitModel, t.paragraph));
    QCOMPARE(splitModel->getReadingOrder(0), (std::vector<PDFInteger>{ 0, 4, 1, 3 }));
}

void ContentStructureModelTest::reorderInSequence()
{
    // Row 4: an order or alignment command over a node of a sequence - the tag stays: the only
    // content moves with its wrap (the same identifier), else the sequence is split and the node
    // gets a new identifier of the same element
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    const PDFContentStructureChange keep = model->getChangeForReorder(0, 3, true, 3);
    QVERIFY(keep.action == PDFContentStructureChange::Action::KeepWithWrap);
    QCOMPARE(keep.mcid, PDFInteger(3));
    QCOMPARE(keep.newMcid, PDFInteger(-1));
    QCOMPARE(keep.element, Helper::idOf(*model, t.figure));
    PDFContentStructureModelPointer kept = model->applied({ keep });
    QVERIFY(!kept->isModified());
    QCOMPARE(Helper::childrenOf(*kept, t.figure), QStringList({ "M3" }));

    // The content of the page uses identifiers up to 9 (a sequence, which is no content of the
    // structure): the new identifier is 10 and the array gets nulls for 4 ... 9
    const PDFContentStructureChange split = model->getChangeForReorder(0, 3, false, 9);
    QVERIFY(split.action == PDFContentStructureChange::Action::SplitSequence);
    QCOMPARE(split.newMcid, PDFInteger(10));
    QCOMPARE(split.element, Helper::idOf(*model, t.figure));

    PDFContentStructureModelPointer splitModel = model->applied({ split });
    QCOMPARE(Helper::childrenOf(*splitModel, t.figure), QStringList({ "M3", "M10" }));
    QCOMPARE(Helper::parentTreeOf(*splitModel, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure,
                                                                     PDFObjectReference(), PDFObjectReference(), PDFObjectReference(),
                                                                     PDFObjectReference(), PDFObjectReference(), PDFObjectReference(), t.figure }));
}

void ContentStructureModelTest::moveWholeSequenceAndCopy()
{
    // Row 5: a whole sequence moves in the order on its page - the identifier does not change.
    // Row 6: a copy or a paste - the copy has no identifier, the structure is not asked.
    // Row 9: a use of an XObject with /StructParent is deleted or copied - nothing.
    // So no change of the structure exists for them, and a command without changes gives a
    // model, which is not modified and equal in everything a reader can ask.
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    PDFContentStructureChange refused;
    refused.action = PDFContentStructureChange::Action::Refused;
    refused.structuralParentKey = 0;
    refused.mcid = 0;

    PDFContentStructureModelPointer same = model->applied({ });
    PDFContentStructureModelPointer alsoSame = model->applied({ model->getChangeForReorder(0, 0, true, 3), model->getChangeForMoveOut(0, 1, false, false, 3),
                                                                model->getChangeForDelete(0, 3, false), refused });
    for (const PDFContentStructureModelPointer& result : { same, alsoSame })
    {
        QVERIFY(result);
        QVERIFY(!result->isModified());
        QCOMPARE(result->getElements().size(), size_t(6));
        QCOMPARE(Helper::childrenOf(*result, t.paragraph), QStringList({ "M0" }));
        QCOMPARE(Helper::childrenOf(*result, t.link), QStringList({ QString("O%1").arg(t.annotation.objectNumber) }));
        QCOMPARE(Helper::parentTreeOf(*result, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure }));
        QCOMPARE(Helper::parentTreeOf(*result, 1), Helper::numbers({ t.link }));
        QCOMPARE(result->allocateMarkedContentId(0, -1), PDFInteger(4));
    }
}

void ContentStructureModelTest::splitSequence()
{
    // A sequence named by a marked-content reference is split: the new child is a reference of
    // the same page with the new identifier, right behind the child of the sequence
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    const PDFContentStructureChange change = model->getChangeForReorder(0, 1, false, 3);
    QVERIFY(change.action == PDFContentStructureChange::Action::SplitSequence);
    QCOMPARE(change.newMcid, PDFInteger(4));
    QCOMPARE(change.element, Helper::idOf(*model, t.span));

    PDFContentStructureModelPointer changed = model->applied({ change });
    QCOMPARE(Helper::childrenOf(*changed, t.span), QStringList({ QString("R1@%1").arg(t.page.objectNumber), QString("R4@%1").arg(t.page.objectNumber) }));
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure, t.span }));
    QVERIFY(Helper::findElement(*changed, t.span)->isModified);
    QVERIFY(!Helper::findElement(*changed, t.paragraph)->isModified);

    // The identifier 2 has no element (the null of the array): its sequence is no content of the
    // structure, so its part gets a new identifier, and the structure does not change
    const PDFContentStructureChange stray = model->getChangeForReorder(0, 2, false, 3);
    QVERIFY(stray.action == PDFContentStructureChange::Action::SplitSequence);
    QCOMPARE(stray.newMcid, PDFInteger(4));
    QCOMPARE(stray.element, PDFContentStructureElementId(-1));
    QVERIFY(!model->applied({ stray })->isModified());
}

void ContentStructureModelTest::splitKeepsForeignChildren()
{
    // X /Pg page /K [ null  0  (junk)  1  Y  3.5 ]      /Nums [0 [X X]]
    // The items, which are no children (null, a string, a real number), keep their places among
    // the children, when a child is added or removed:
    //   the sequence 0 is split (new identifier 2)  ->  [ null 0 2 (junk) 1 Y 3.5 ]
    //   then the sequence 1 is removed              ->  [ null 0 2 (junk) Y 3.5 ]
    //   or the sequence 0 is removed instead        ->  [ null 2 (junk) 1 Y 3.5 ]
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference x = d.reserve();
    const PDFObjectReference y = d.reserve();
    d.set(x, Obj::element("Sect", root, d.page(), Obj::array({ Obj::null(), Obj::integer(0), Obj::string("junk"), Obj::integer(1), Obj::reference(y), PDFObject::createReal(3.5) })));
    d.set(y, Obj::element("P", x, PDFObjectReference(), PDFObject()));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(x) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(x), Obj::reference(x) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(Helper::childrenOf(*model, x), QStringList({ "M0", "M1", Helper::elementText(y) }));

    const PDFContentStructureChange split = model->getChangeForReorder(0, 0, false, 1);
    QCOMPARE(split.newMcid, PDFInteger(2));
    PDFContentStructureModelPointer splitModel = model->applied({ split });
    QCOMPARE(Helper::childrenOf(*splitModel, x), QStringList({ "M0", "M2", "M1", Helper::elementText(y) }));

    PDFContentStructureModelPointer removedModel = splitModel->applied({ splitModel->getChangeForDelete(0, 1, true) });
    QCOMPARE(Helper::childrenOf(*removedModel, x), QStringList({ "M0", "M2", Helper::elementText(y) }));

    // The written /K of both states
    {
        PDFDocumentBuilder builder(&document);
        QVERIFY(PDFStructureTreeWriter::write(&builder, *splitModel).isWritten);
        const PDFObject kids = Helper::dictionaryOf(builder, x)->get("K");
        QCOMPARE(Helper::itemsOf(kids), QStringList({ "null", "0", "2", "(junk)", "1", Helper::referenceText(y), "?" }));
        QVERIFY(kids.getArray()->getItem(6) == PDFObject::createReal(3.5));
    }
    {
        PDFDocumentBuilder builder(&document);
        QVERIFY(PDFStructureTreeWriter::write(&builder, *removedModel).isWritten);
        const PDFObject kids = Helper::dictionaryOf(builder, x)->get("K");
        QCOMPARE(Helper::itemsOf(kids), QStringList({ "null", "0", "2", "(junk)", Helper::referenceText(y), "?" }));
        QCOMPARE(Helper::itemsOf(Helper::parentTreeNumbers(builder).getArray()->getItem(1)), QStringList({ Helper::referenceText(x), "null", Helper::referenceText(x) }));
    }
    {
        // The removed child is in front of the string: the string stays between 2 and 1
        PDFContentStructureModelPointer removedFirst = splitModel->applied({ splitModel->getChangeForDelete(0, 0, true) });
        QCOMPARE(Helper::childrenOf(*removedFirst, x), QStringList({ "M2", "M1", Helper::elementText(y) }));

        PDFDocumentBuilder builder(&document);
        QVERIFY(PDFStructureTreeWriter::write(&builder, *removedFirst).isWritten);
        const PDFObject kids = Helper::dictionaryOf(builder, x)->get("K");
        QCOMPARE(Helper::itemsOf(kids), QStringList({ "null", "2", "(junk)", "1", Helper::referenceText(y), "?" }));
        QCOMPARE(Helper::itemsOf(Helper::parentTreeNumbers(builder).getArray()->getItem(1)), QStringList({ "null", Helper::referenceText(x), Helper::referenceText(x) }));
    }
}

void ContentStructureModelTest::splitTwiceInOneCommand()
{
    // One command splits two sequences of a page. Both changes come from the same model, so the
    // caller counts the first new identifier among the used ones of the second (it is in the
    // content by then): 4, then 5.
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    const PDFContentStructureChange first = model->getChangeForReorder(0, 0, false, 3);
    QCOMPARE(first.newMcid, PDFInteger(4));
    const PDFContentStructureChange second = model->getChangeForReorder(0, 3, false, first.newMcid);
    QCOMPARE(second.newMcid, PDFInteger(5));

    PDFContentStructureModelPointer changed = model->applied({ first, second });
    QCOMPARE(Helper::childrenOf(*changed, t.paragraph), QStringList({ "M0", "M4" }));
    QCOMPARE(Helper::childrenOf(*changed, t.figure), QStringList({ "M3", "M5" }));
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure, t.paragraph, t.figure }));

    // The model after the first change gives the same second identifier
    QCOMPARE(model->applied({ first })->getChangeForReorder(0, 3, false, 3).newMcid, PDFInteger(5));
}

void ContentStructureModelTest::splitRefused()
{
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    // The content uses the largest identifier 2^31 - 1: no new one exists
    for (const PDFContentStructureChange& change : { model->getChangeForReorder(0, 0, false, 2147483647), model->getChangeForMoveOut(0, 0, false, true, 2147483647),
                                                     model->getChangeForReorder(0, 0, false, 70000) })
    {
        QVERIFY(change.action == PDFContentStructureChange::Action::Refused);
        QVERIFY(!change.text.isEmpty());
        QCOMPARE(change.newMcid, PDFInteger(-1));
        QCOMPARE(change.structuralParentKey, PDFInteger(0));
        QCOMPARE(change.mcid, PDFInteger(0));
        QVERIFY(!model->applied({ change })->isModified());
    }

    // The other changes of the same content are not refused
    QVERIFY(model->getChangeForReorder(0, 0, true, 2147483647).action == PDFContentStructureChange::Action::KeepWithWrap);
    QVERIFY(model->getChangeForDelete(0, 0, true).action == PDFContentStructureChange::Action::RemoveSequence);
}

void ContentStructureModelTest::splitOfIrregularContent_data()
{
    // Three elements A, B, C with /Pg page (/StructParents 0); each row gives their /K, the array
    // of the key 0 of the parent tree (-1 = no key 0 at all) and the split sequence. A split gives
    // the rest of a sequence a new identifier of the element the parent tree names, as a child
    // right behind the child of the sequence (zadani_jadro.md 4.12). When the parent tree does not
    // tie the sequence to the one element, which lists it, no element would list the rest, or the
    // parent tree would name an element without such a child; such content cannot be tagged
    // (zadani_overeni.md 6.2): the split is refused, and a split given to applied changes nothing.
    QTest::addColumn<QList<int>>("kidsA");
    QTest::addColumn<QList<int>>("kidsB");
    QTest::addColumn<QList<int>>("kidsC");
    QTest::addColumn<QString>("parentArray");   // "A A B" ...; "-" = no key 0
    QTest::addColumn<int>("mcid");
    QTest::addColumn<bool>("isRefused");

    QTest::newRow("no item of the key") << QList<int>({ 5 }) << QList<int>() << QList<int>() << QString("-") << 5 << true;
    QTest::newRow("short array") << QList<int>({ 0, 5 }) << QList<int>() << QList<int>() << QString("A") << 5 << true;
    QTest::newRow("listed by another element") << QList<int>({ 0, 1 }) << QList<int>({ 1, 2 }) << QList<int>({ 3 }) << QString("A A B A") << 3 << true;
    QTest::newRow("listed by two elements") << QList<int>({ 0, 1 }) << QList<int>({ 1, 2 }) << QList<int>({ 3 }) << QString("A A B A") << 1 << true;
    QTest::newRow("listed by no element") << QList<int>({ 0 }) << QList<int>() << QList<int>() << QString("A A") << 1 << true;
    QTest::newRow("regular") << QList<int>({ 0, 1 }) << QList<int>({ 1, 2 }) << QList<int>({ 3 }) << QString("A A B A") << 2 << false;
}

void ContentStructureModelTest::splitOfIrregularContent()
{
    QFETCH(QList<int>, kidsA);
    QFETCH(QList<int>, kidsB);
    QFETCH(QList<int>, kidsC);
    QFETCH(QString, parentArray);
    QFETCH(int, mcid);
    QFETCH(bool, isRefused);

    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const std::array<PDFObjectReference, 3> elements = { d.reserve(), d.reserve(), d.reserve() };
    const std::array<QList<int>, 3> kids = { kidsA, kidsB, kidsC };
    for (size_t i = 0; i < elements.size(); ++i)
    {
        std::vector<PDFObject> items;
        for (int kid : kids[i])
        {
            items.push_back(Obj::integer(kid));
        }
        d.set(elements[i], Obj::element("P", root, d.page(), Obj::array(std::move(items))));
    }

    QList<PDFInteger> parentTree;
    std::vector<PDFObject> numbers;
    if (parentArray != "-")
    {
        std::vector<PDFObject> items;
        for (const QString& name : parentArray.split(' '))
        {
            const PDFObjectReference element = elements[size_t(name.at(0).unicode() - 'A')];
            items.push_back(Obj::reference(element));
            parentTree << element.objectNumber;
        }
        numbers = { Obj::integer(0), Obj::array(std::move(items)) };
    }

    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(elements[0]), Obj::reference(elements[1]), Obj::reference(elements[2]) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array(std::move(numbers)) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);

    // The largest identifier of the rows is 5: the new one is 6
    const std::array<PDFContentStructureChange, 2> changes = { model->getChangeForReorder(0, mcid, false, 5), model->getChangeForMoveOut(0, mcid, false, true, 5) };
    for (const PDFContentStructureChange& change : changes)
    {
        QCOMPARE(change.action == PDFContentStructureChange::Action::Refused, isRefused);
        QCOMPARE(change.text.isEmpty(), !isRefused);
        QCOMPARE(change.newMcid, PDFInteger(isRefused ? -1 : 6));
    }

    // What needs no split is not refused
    QVERIFY(model->getChangeForMoveOut(0, mcid, false, false, 5).action == PDFContentStructureChange::Action::Untag);
    QVERIFY(model->getChangeForReorder(0, mcid, true, 5).action == PDFContentStructureChange::Action::KeepWithWrap);
    QVERIFY(model->getChangeForDelete(0, mcid, true).action == PDFContentStructureChange::Action::RemoveSequence);

    // The split applied: the children of every element and the array of the parent tree
    PDFContentStructureModelPointer changed = model->applied({ Helper::split(0, mcid, 6) });
    QCOMPARE(changed->isModified(), !isRefused);
    std::array<QStringList, 3> expectedChildren;
    for (size_t i = 0; i < elements.size(); ++i)
    {
        for (int kid : kids[i])
        {
            expectedChildren[i] << QString("M%1").arg(kid);
            if (!isRefused && kid == mcid)
            {
                expectedChildren[i] << QString("M6");
            }
        }
        QCOMPARE(Helper::childrenOf(*changed, elements[i]), expectedChildren[i]);
    }

    if (!isRefused)
    {
        // The rest belongs to the element of the sequence: B (the array gets a null for 4 and 5)
        parentTree << 0 << 0 << elements[1].objectNumber;
    }
    QCOMPARE(Helper::parentTreeOf(*changed, 0), parentArray == "-" ? QList<PDFInteger>({ PDFInteger(-999) }) : parentTree);
}

void ContentStructureModelTest::splitOfChildWithoutPage()
{
    // A /K [0 1] without /Pg (the page of its children is not known);  /Nums [0 [A A]] of the page
    // with /StructParents 0. The parent tree ties both identifiers to A, and A lists them: the
    // element the parent tree names takes its child of an unknown page for the content, so the
    // content is regular and can be split (new identifier 2, its child behind the child 0).
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, PDFObjectReference(), Obj::array({ Obj::integer(0), Obj::integer(1) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(a) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(a) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);

    const PDFContentStructureChange change = model->getChangeForReorder(0, 0, false, 1);
    QVERIFY(change.action == PDFContentStructureChange::Action::SplitSequence);
    QCOMPARE(change.newMcid, PDFInteger(2));
    QCOMPARE(change.element, Helper::idOf(*model, a));

    PDFContentStructureModelPointer changed = model->applied({ change });
    QCOMPARE(Helper::childrenOf(*changed, a), QStringList({ "M0", "M2", "M1" }));
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ a, a, a }));
}

void ContentStructureModelTest::removeSequenceOfForeignChild()
{
    // Items of /K, which the model does not read as children, but which name one marked content
    // of a page (row 2 of zadani_jadro.md 4.12: the child of the element is removed with the last
    // content of its sequence; page /StructParents 0, page2 /StructParents 5):
    //   X /Pg page /K [ 0
    //     a  << /Type /MCR /Pg page /MCID 1 /Extra 1 >>      (0, 1): an MCR with a key of no Table 357
    //     b  << /Type /MCR /Pg (page) /MCID 1 >>              its page is not a reference: not known
    //     c  << /Type /MCR /Pg page2 /MCID 1 /Extra 1 >>     (5, 1): another page
    //     d  << /S /Span /Pg page /Alt (two) /K 2 >>          (0, 2): a direct element, its /K
    //     e  << /S /Span /Pg page /K [3] >>                   (0, 3): the same in an array
    //     f  << /Type /MCR /Pg page /MCID 4 /Extra 1 >>      (0, 4): the parent tree has no item for it
    //     g  << /S /Span /Pg page /K [5 6] >>                 names two contents, not exactly one
    //   ]
    //   /Nums [ 0 [X X X X]  5 [null X] ]
    // A removed sequence takes the item, which names exactly it (a direct element stays, without
    // /K, as an element of the model stays without children); b names no known content and g
    // more than one: they stay (the model does not rewrite an item it does not read).
    // The rest of such a sequence would stay in the item after a split: the split is refused.
    StructureDocument d;
    const PDFObjectReference page2 = d.addPage();
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference x = d.reserve();

    const PDFObject a = Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(d.page()) }, { "MCID", Obj::integer(1) }, { "Extra", Obj::integer(1) } });
    const PDFObject b = Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::string("page") }, { "MCID", Obj::integer(1) } });
    const PDFObject c = Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(page2) }, { "MCID", Obj::integer(1) }, { "Extra", Obj::integer(1) } });
    const PDFObject dd = Obj::dictionary({ { "S", Obj::name("Span") }, { "Pg", Obj::reference(d.page()) }, { "Alt", Obj::string("two") }, { "K", Obj::integer(2) } });
    const PDFObject e = Obj::dictionary({ { "S", Obj::name("Span") }, { "Pg", Obj::reference(d.page()) }, { "K", Obj::array({ Obj::integer(3) }) } });
    const PDFObject f = Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(d.page()) }, { "MCID", Obj::integer(4) }, { "Extra", Obj::integer(1) } });
    const PDFObject g = Obj::dictionary({ { "S", Obj::name("Span") }, { "Pg", Obj::reference(d.page()) }, { "K", Obj::array({ Obj::integer(5), Obj::integer(6) }) } });
    const PDFObject dWithoutKids = Obj::dictionary({ { "S", Obj::name("Span") }, { "Pg", Obj::reference(d.page()) }, { "Alt", Obj::string("two") } });
    const PDFObject eWithoutKids = Obj::dictionary({ { "S", Obj::name("Span") }, { "Pg", Obj::reference(d.page()) } });

    d.set(x, Obj::element("Sect", root, d.page(), Obj::array({ Obj::integer(0), a, b, c, dd, e, f, g })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(x) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(x), Obj::reference(x), Obj::reference(x), Obj::reference(x) }),
                                                                                           Obj::integer(5), Obj::array({ Obj::null(), Obj::reference(x) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setStructuralParents(page2, 5);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(Helper::childrenOf(*model, x), QStringList({ "M0" }));

    // The written /K of a model
    auto writtenKids = [&document, x](const PDFContentStructureModel& changed) -> std::vector<PDFObject>
    {
        PDFDocumentBuilder builder(&document);
        if (!PDFStructureTreeWriter::write(&builder, changed).isWritten)
        {
            return { };
        }
        const PDFObject kids = Helper::dictionaryOf(builder, x)->get("K");
        return kids.isArray() ? std::vector<PDFObject>(kids.getArray()->begin(), kids.getArray()->end()) : std::vector<PDFObject>{ kids };
    };

    struct Row
    {
        const char* name;
        std::vector<std::pair<PDFInteger, PDFInteger>> removed;     // (key, mcid)
        std::vector<PDFObject> kids;
        QList<PDFInteger> parentTree0;
        QList<PDFInteger> parentTree5;
    };
    const PDFInteger X = x.objectNumber;
    const std::vector<Row> rows = {
        { "sequence 1 of page",  { { 0, 1 } },                                 { Obj::integer(0), b, c, dd, e, f, g },              { X, 0, X, X }, { 0, X } },
        { "sequence 1 of page2", { { 5, 1 } },                                 { Obj::integer(0), a, b, dd, e, f, g },              { X, X, X, X }, { 0, 0 } },
        { "sequence 2",          { { 0, 2 } },                                 { Obj::integer(0), a, b, c, dWithoutKids, e, f, g }, { X, X, 0, X }, { 0, X } },
        { "sequence 4",          { { 0, 4 } },                                 { Obj::integer(0), a, b, c, dd, e, g },              { X, X, X, X }, { 0, X } },
        { "sequence 5",          { { 0, 5 } },                                 { },                                                 { X, X, X, X }, { 0, X } },
        { "clear page",          { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 } }, { b, c, dWithoutKids, eWithoutKids, g },        { 0, 0, 0, 0 }, { 0, X } },
    };

    for (const Row& row : rows)
    {
        std::vector<PDFContentStructureChange> changes;
        for (const auto& removed : row.removed)
        {
            changes.push_back(model->getChangeForDelete(removed.first, removed.second, true));
            QVERIFY(changes.back().action == PDFContentStructureChange::Action::RemoveSequence);
        }

        // "sequence 5" names nothing the model can change: nothing is modified, nothing written
        PDFContentStructureModelPointer changed = model->applied(changes);
        QVERIFY2(changed->isModified() == !row.kids.empty(), row.name);
        QVERIFY2(writtenKids(*changed) == row.kids, row.name);
        QVERIFY2(Helper::parentTreeOf(*changed, 0) == row.parentTree0, row.name);
        QVERIFY2(Helper::parentTreeOf(*changed, 5) == row.parentTree5, row.name);
    }

    // The sequences an item names cannot be split; the sequence 0 of the child can
    for (const auto& named : std::vector<std::pair<PDFInteger, PDFInteger>>{ { 0, 1 }, { 5, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 } })
    {
        QVERIFY(model->getChangeForReorder(named.first, named.second, false, 9).action == PDFContentStructureChange::Action::Refused);
    }
    QVERIFY(model->getChangeForReorder(0, 0, false, 9).action == PDFContentStructureChange::Action::SplitSequence);
}

void ContentStructureModelTest::appliedIgnoresWrongChange()
{
    // A change, which does not fit the model, changes nothing (it cannot come from this model)
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    const std::vector<PDFContentStructureChange> wrongChanges = {
        Helper::split(0, 0, 1),             // The new identifier belongs to the Span
        Helper::split(0, 0, 2),             // The new identifier is an index of the array (its null): used
        Helper::split(0, 0, 0),             // The new identifier is the old one
        Helper::split(0, 0, -1),            // No new identifier
        Helper::split(0, 2, 4),             // The sequence has no element (the null of the array)
        Helper::split(0, 9, 10),            // The sequence is not in the array
        Helper::split(1, 0, 4),             // The key of an object
        Helper::split(7, 0, 4),             // An unknown key
        Helper::split(0, 0, 100000),        // More than 65 536 nulls would be added
        Helper::removal(0, 2),              // Nothing to remove: the null of the array
        Helper::removal(0, 9),              // Outside of the array
        Helper::removal(1, 0),              // The key of an object
        Helper::removal(7, 0),              // An unknown key
        Helper::removal(-1, 0),
        Helper::removal(0, -1)
    };

    for (size_t i = 0; i < wrongChanges.size(); ++i)
    {
        PDFContentStructureModelPointer changed = model->applied({ wrongChanges[i] });
        QVERIFY2(!changed->isModified(), qPrintable(QString("change %1").arg(i)));
        QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure }));
        QCOMPARE(Helper::childrenOf(*changed, t.paragraph), QStringList({ "M0" }));
    }

    // A wrong change among right ones does not stop them
    PDFContentStructureModelPointer mixed = model->applied({ Helper::split(0, 0, 1), Helper::removal(0, 3), Helper::split(7, 0, 4) });
    QVERIFY(mixed->isModified());
    QVERIFY(Helper::childrenOf(*mixed, t.figure).isEmpty());
    QCOMPARE(Helper::parentTreeOf(*mixed, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), PDFObjectReference() }));

    // A /Pg page /K 0;  /Nums [0 [A A other]]: the items 1 and 2 of the array have no child in the
    // tree, but they are not free - an element and an object, which is nothing of the model
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, d.page(), Obj::integer(0)));
    const PDFObjectReference other = d.add(Obj::dictionary({ { "Type", Obj::name("Other") } }));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(a) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(a), Obj::reference(other) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer second = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(second);
    QCOMPARE(second->getChangeForReorder(0, 0, false, 0).newMcid, PDFInteger(3));
    for (PDFInteger newMcid : { 1, 2 })
    {
        PDFContentStructureModelPointer changed = second->applied({ Helper::split(0, 0, newMcid) });
        QVERIFY(!changed->isModified());
        QCOMPARE(Helper::childrenOf(*changed, a), QStringList({ "M0" }));
        QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ a, a, PDFObjectReference() }));
    }
}

void ContentStructureModelTest::clearPageKeepsObjectItems()
{
    // Row 11: Clear Page - every identifier of the page ends as with a deletion, in one new model.
    // Row 9: the annotation with /StructParent 1 is no content of the page: its object reference
    // and its key of the parent tree stay.
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    std::vector<PDFContentStructureChange> changes;
    for (PDFInteger mcid : model->getReadingOrder(0))
    {
        changes.push_back(model->getChangeForDelete(0, mcid, true));
    }
    QCOMPARE(changes.size(), size_t(3));

    PDFContentStructureModelPointer changed = model->applied(changes);
    QVERIFY(changed->isModified());
    QCOMPARE(changed->getElements().size(), size_t(6));
    QVERIFY(Helper::childrenOf(*changed, t.paragraph).isEmpty());
    QVERIFY(Helper::childrenOf(*changed, t.span).isEmpty());
    QVERIFY(Helper::childrenOf(*changed, t.figure).isEmpty());
    QCOMPARE(Helper::parentTreeOf(*changed, 0), Helper::numbers({ PDFObjectReference(), PDFObjectReference(), PDFObjectReference(), PDFObjectReference() }));
    QVERIFY(changed->getReadingOrder(0).empty());

    QCOMPARE(Helper::childrenOf(*changed, t.link), QStringList({ QString("O%1").arg(t.annotation.objectNumber) }));
    QCOMPARE(Helper::parentTreeOf(*changed, 1), Helper::numbers({ t.link }));
    QVERIFY(!changed->getParentTree().at(1).isModified);
    QVERIFY(!Helper::findElement(*changed, t.link)->isModified);
    QCOMPARE(Helper::childrenOf(*changed, t.documentElement).size(), qsizetype(4));

    // Written: /Nums [ 0 [null null null null] 1 Link ]
    PDFDocumentBuilder builder(&t.document);
    const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
    QVERIFY(result.isWritten);
    QVERIFY(result.errors.empty());
    const PDFObject numbers = Helper::parentTreeNumbers(builder);
    QCOMPARE(Helper::itemsOf(numbers), QStringList({ "0", "[...]", "1", Helper::referenceText(t.link) }));
    QCOMPARE(Helper::itemsOf(numbers.getArray()->getItem(1)), QStringList({ "null", "null", "null", "null" }));
    QVERIFY(builder.getObjectByReference(t.link) == t.document.getObjectByReference(t.link));
}

void ContentStructureModelTest::clearPageOfManySequences()
{
    // Row 11 for a page of 20 000 sequences of one element:
    //   X /Pg page /K [ (head) 0 1 ... 9999 (middle) 10000 ... 19999 (tail) ]     /Nums [0 [X X ... X]]
    // Clear Page gives one change per sequence in one command; the model applies them in one
    // pass per element (the cost does not grow with the product of the changes and the children).
    //   all removed:          X /K [ (head) (middle) (tail) ],        the array 20 000 nulls
    //   the even ones only:   X /K [ (head) 1 3 ... 9999 (middle) 10001 ... 19999 (tail) ],
    //                         the array [null X null X ...]
    // The three strings are no children: they keep their places among the children that stay.
    constexpr int COUNT = 20000;
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference x = d.reserve();

    std::vector<PDFObject> kids = { Obj::string("head") };
    std::vector<PDFObject> parentArray;
    for (int i = 0; i < COUNT; ++i)
    {
        if (i == COUNT / 2)
        {
            kids.push_back(Obj::string("middle"));
        }
        kids.push_back(Obj::integer(i));
        parentArray.push_back(Obj::reference(x));
    }
    kids.push_back(Obj::string("tail"));

    d.set(x, Obj::element("P", root, d.page(), Obj::array(std::move(kids))));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(x) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array(std::move(parentArray)) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(Helper::findElement(*model, x)->children.size(), size_t(COUNT));

    std::vector<PDFContentStructureChange> all;
    std::vector<PDFContentStructureChange> even;
    for (PDFInteger mcid : model->getReadingOrder(0))
    {
        all.push_back(model->getChangeForDelete(0, mcid, true));
        if (mcid % 2 == 0)
        {
            even.push_back(all.back());
        }
    }
    QCOMPARE(all.size(), size_t(COUNT));
    QVERIFY(all.back().action == PDFContentStructureChange::Action::RemoveSequence);

    // Linear work is milliseconds (6 ms in a release build); one pass over the children for each
    // change is 4 * 10^8 steps and 2 * 10^8 moved children (600 ms)
    const int timeoutFactor = qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QElapsedTimer timer;
    timer.start();
    PDFContentStructureModelPointer cleared = model->applied(all);
    PDFContentStructureModelPointer halved = model->applied(even);
    QVERIFY2(timer.elapsed() < qint64(150) * timeoutFactor, qPrintable(QString("applied in %1 ms").arg(timer.elapsed())));

    QVERIFY(cleared->isModified());
    QVERIFY(Helper::childrenOf(*cleared, x).isEmpty());
    QVERIFY(Helper::findElement(*cleared, x)->isModified);
    QCOMPARE(Helper::parentTreeOf(*cleared, 0), QList<PDFInteger>(COUNT, PDFInteger(0)));
    QVERIFY(cleared->getReadingOrder(0).empty());

    QStringList halvedChildren;
    QStringList halvedKids = { "(head)" };
    QList<PDFInteger> halvedParentTree;
    for (int i = 0; i < COUNT; ++i)
    {
        if (i == COUNT / 2)
        {
            halvedKids << "(middle)";
        }
        if (i % 2 == 1)
        {
            halvedChildren << QString("M%1").arg(i);
            halvedKids << QString::number(i);
        }
        halvedParentTree << (i % 2 == 1 ? x.objectNumber : PDFInteger(0));
    }
    halvedKids << "(tail)";
    QCOMPARE(Helper::childrenOf(*halved, x), halvedChildren);
    QCOMPARE(Helper::parentTreeOf(*halved, 0), halvedParentTree);

    // The written /K: the foreign items at their places
    {
        PDFDocumentBuilder builder(&document);
        QVERIFY(PDFStructureTreeWriter::write(&builder, *cleared).isWritten);
        QCOMPARE(Helper::itemsOf(Helper::dictionaryOf(builder, x)->get("K")), QStringList({ "(head)", "(middle)", "(tail)" }));
    }
    {
        PDFDocumentBuilder builder(&document);
        QVERIFY(PDFStructureTreeWriter::write(&builder, *halved).isWritten);
        QCOMPARE(Helper::itemsOf(Helper::dictionaryOf(builder, x)->get("K")), halvedKids);
    }
}

void ContentStructureModelTest::documentWithoutStructureChanges()
{
    // A document without a structure may have marked content with identifiers. Its sequences
    // are handled the same way; the structure has nothing to change.
    StructureDocument d;
    const PDFDocument document = d.build();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model && model->isEmpty());

    const PDFContentStructureChange removal = model->getChangeForDelete(0, 5, true);
    QVERIFY(removal.action == PDFContentStructureChange::Action::RemoveSequence);
    QCOMPARE(removal.element, PDFContentStructureElementId(-1));

    const PDFContentStructureChange split = model->getChangeForReorder(-1, 5, false, 9);
    QVERIFY(split.action == PDFContentStructureChange::Action::SplitSequence);
    QCOMPARE(split.newMcid, PDFInteger(10));
    QCOMPARE(split.element, PDFContentStructureElementId(-1));

    PDFContentStructureModelPointer changed = model->applied({ removal, split });
    QVERIFY(changed->isEmpty());
    QVERIFY(!changed->isModified());

    PDFDocumentBuilder builder(&document);
    const PDFObjectStorage before = *builder.getStorage();
    const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
    QVERIFY(!result.isWritten);
    QVERIFY(result.errors.empty());
    QVERIFY(*builder.getStorage() == before);
}

// ---------------------------------------------------------------------------------------------
//  Differences of the changes (zadani_jadro.md 4.5: an undo step keeps the difference)
// ---------------------------------------------------------------------------------------------

void ContentStructureModelTest::differenceOfEachAction()
{
    // Each action of the consistency table on the tagged document (P /K 0, Span 1, Figure 3; the
    // array of the page [P Span null Figure]), applied with a difference, reverted from the model
    // after it and reapplied to the model before it. The actions, which change no structure, give
    // an empty difference; a removal and a split touch the element of the sequence and the array
    // of the page (ISO 32000-2, 14.7.5.4), nothing else.
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    struct Row
    {
        const char* name;
        PDFContentStructureChange change;
        PDFContentStructureChange::Action action;
        QStringList paragraphChildren;
        QList<PDFInteger> pageArray;
        size_t elementCount;
        size_t itemCount;
    };

    const QList<PDFInteger> original = Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure });
    const std::vector<Row> rows = {
        { "none", model->getChangeForDelete(0, 0, false), PDFContentStructureChange::Action::None, { "M0" }, original, 0, 0 },
        { "untag", model->getChangeForMoveOut(0, 0, false, false, 3), PDFContentStructureChange::Action::Untag, { "M0" }, original, 0, 0 },
        { "keep with wrap", model->getChangeForReorder(0, 0, true, 3), PDFContentStructureChange::Action::KeepWithWrap, { "M0" }, original, 0, 0 },
        { "refused", model->getChangeForReorder(0, 0, false, 2147483647), PDFContentStructureChange::Action::Refused, { "M0" }, original, 0, 0 },
        { "remove sequence", model->getChangeForDelete(0, 0, true), PDFContentStructureChange::Action::RemoveSequence, { },
          Helper::numbers({ PDFObjectReference(), t.span, PDFObjectReference(), t.figure }), 1, 1 },
        { "split sequence", model->getChangeForReorder(0, 0, false, 3), PDFContentStructureChange::Action::SplitSequence, { "M0", "M4" },
          Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure, t.paragraph }), 1, 1 },
    };

    for (const Row& row : rows)
    {
        QVERIFY2(row.change.action == row.action, row.name);

        PDFContentStructureDifference difference;
        PDFContentStructureModelPointer changed = model->applied({ row.change }, &difference);
        QVERIFY2(changed, row.name);
        QVERIFY2(changed->isEqual(*model->applied({ row.change })), row.name);
        QVERIFY2(Helper::childrenOf(*changed, t.paragraph) == row.paragraphChildren, row.name);
        QVERIFY2(Helper::parentTreeOf(*changed, 0) == row.pageArray, row.name);
        QVERIFY2(changed->isEqual(*model) == (row.elementCount == 0), row.name);

        QVERIFY2(difference.isEmpty() == (row.elementCount == 0), row.name);
        QVERIFY2(difference.getElementCount() == row.elementCount, row.name);
        QVERIFY2(difference.getParentTreeItemCount() == row.itemCount, row.name);

        // Undo: the model before, unmodified again
        PDFContentStructureModelPointer reverted = changed->reverted(difference);
        QVERIFY2(reverted, row.name);
        QVERIFY2(reverted->isEqual(*model), row.name);
        QVERIFY2(!reverted->isModified(), row.name);
        QVERIFY2(Helper::childrenOf(*reverted, t.paragraph) == QStringList({ "M0" }), row.name);
        QVERIFY2(Helper::parentTreeOf(*reverted, 0) == original, row.name);

        // Redo
        PDFContentStructureModelPointer reapplied = model->reapplied(difference);
        QVERIFY2(reapplied, row.name);
        QVERIFY2(reapplied->isEqual(*changed), row.name);
        QVERIFY2(reapplied->isModified() == (row.elementCount > 0), row.name);
        QVERIFY2(Helper::childrenOf(*reapplied, t.paragraph) == row.paragraphChildren, row.name);
    }

    // A difference cannot be applied to a model with another count of elements
    PDFContentStructureDifference difference;
    model->applied({ model->getChangeForDelete(0, 0, true) }, &difference);
    const PDFContentStructureModel emptyModel;
    QVERIFY(emptyModel.reverted(difference) == nullptr);
    QVERIFY(emptyModel.reapplied(difference) == nullptr);
}

void ContentStructureModelTest::differenceRevertedInReverseOrder()
{
    // Three commands on the private parts of the model, undone in the reverse order and redone:
    //   X /Pg page /K [ null 0 (junk) 1 << /Type /MCR /Pg page /MCID 2 /Extra 1 >> ]
    //   /Nums [ 0 [X X X (junk)] ]
    // null and (junk) are foreign children; the reference with /Extra is a foreign child, which
    // names the content 2 (irregular: it goes with the content); the string at the index 3 of the
    // array is a foreign item of the parent tree.
    //   A: split of 0 (new identifier 4)    -> /K [ null 0 4 (junk) 1 MCR ]   array [X X X (junk) X]
    //   B: delete of 2 and 3 (one command)   -> /K [ null 0 4 (junk) 1 ]       array [X X null null X]
    //   C: delete of 4 (the new sequence of the split) and 1
    //                                        -> /K [ null 0 (junk) ]           array [X null null null null]
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference x = d.reserve();
    const PDFObject reference2 = Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(d.page()) }, { "MCID", Obj::integer(2) }, { "Extra", Obj::integer(1) } });
    d.set(x, Obj::element("Sect", root, d.page(), Obj::array({ Obj::null(), Obj::integer(0), Obj::string("junk"), Obj::integer(1), reference2 })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(x) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(x), Obj::reference(x), Obj::reference(x), Obj::string("junk") }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model0 = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model0);
    QCOMPARE(Helper::childrenOf(*model0, x), QStringList({ "M0", "M1" }));

    const QString X = Helper::referenceText(x);
    const QString junk("(junk)");
    const QString null("null");

    // The written /K of X and the written array of the page; nothing written for an unmodified model
    auto written = [&document, x](const PDFContentStructureModel& model) -> std::pair<QStringList, QStringList>
    {
        PDFDocumentBuilder builder(&document);
        if (!PDFStructureTreeWriter::write(&builder, model).isWritten)
        {
            return { };
        }
        const PDFObject numbers = Helper::parentTreeNumbers(builder);
        return { Helper::itemsOf(Helper::dictionaryOf(builder, x)->get("K")), Helper::itemsOf(builder.getObject(numbers.getArray()->getItem(1))) };
    };

    // A
    PDFContentStructureDifference differenceA;
    const PDFContentStructureChange split = model0->getChangeForReorder(0, 0, false, 3);
    QCOMPARE(split.newMcid, PDFInteger(4));
    PDFContentStructureModelPointer model1 = model0->applied({ split }, &differenceA);
    QCOMPARE(written(*model1), std::make_pair(QStringList({ null, "0", "4", junk, "1", "<<...>>" }), QStringList({ X, X, X, junk, X })));

    // B
    PDFContentStructureDifference differenceB;
    PDFContentStructureModelPointer model2 = model1->applied({ model1->getChangeForDelete(0, 2, true), model1->getChangeForDelete(0, 3, true) }, &differenceB);
    QCOMPARE(written(*model2), std::make_pair(QStringList({ null, "0", "4", junk, "1" }), QStringList({ X, X, null, null, X })));

    // C
    PDFContentStructureDifference differenceC;
    PDFContentStructureModelPointer model3 = model2->applied({ model2->getChangeForDelete(0, 4, true), model2->getChangeForDelete(0, 1, true) }, &differenceC);
    QCOMPARE(written(*model3), std::make_pair(QStringList({ null, "0", junk }), QStringList({ X, null, null, null, null })));
    QCOMPARE(differenceC.getElementCount(), size_t(1));
    QCOMPARE(differenceC.getParentTreeItemCount(), size_t(1));
    QVERIFY(!differenceC.isEmpty());

    // A change, which changes nothing, on a modified model
    PDFContentStructureDifference differenceNone;
    QVERIFY(model3->applied({ model3->getChangeForDelete(0, 0, false) }, &differenceNone)->isEqual(*model3));
    QVERIFY(differenceNone.isEmpty());

    // Undo C, B, A
    PDFContentStructureModelPointer undoneC = model3->reverted(differenceC);
    QVERIFY(undoneC && undoneC->isEqual(*model2) && !undoneC->isEqual(*model3));
    QCOMPARE(written(*undoneC), written(*model2));

    PDFContentStructureModelPointer undoneB = undoneC->reverted(differenceB);
    QVERIFY(undoneB && undoneB->isEqual(*model1) && !undoneB->isEqual(*model2));
    QCOMPARE(written(*undoneB), written(*model1));

    // The content 2 is irregular again: its split is refused, as in the model read
    QVERIFY(undoneB->getChangeForReorder(0, 2, false, 4).action == PDFContentStructureChange::Action::Refused);
    QVERIFY(model2->getChangeForReorder(0, 1, false, 4).action == PDFContentStructureChange::Action::SplitSequence);

    PDFContentStructureModelPointer undoneA = undoneB->reverted(differenceA);
    QVERIFY(undoneA && undoneA->isEqual(*model0) && !undoneA->isEqual(*model1));
    QVERIFY(!undoneA->isModified());
    QCOMPARE(written(*undoneA), (std::pair<QStringList, QStringList>()));
    QCOMPARE(Helper::childrenOf(*undoneA, x), QStringList({ "M0", "M1" }));

    // The same split gives the same identifier as before (the largest identifier in use is back)
    QCOMPARE(undoneA->getChangeForReorder(0, 0, false, 3).newMcid, PDFInteger(4));

    // Redo A, B, C
    PDFContentStructureModelPointer redoneA = undoneA->reapplied(differenceA);
    QVERIFY(redoneA && redoneA->isEqual(*model1));
    PDFContentStructureModelPointer redoneB = redoneA->reapplied(differenceB);
    QVERIFY(redoneB && redoneB->isEqual(*model2));
    PDFContentStructureModelPointer redoneC = redoneB->reapplied(differenceC);
    QVERIFY(redoneC && redoneC->isEqual(*model3));
    QCOMPARE(written(*redoneC), written(*model3));
}

void ContentStructureModelTest::differenceIsProportional()
{
    // 2000 elements P_i /K i on one page with the array [P_0 ... P_1999]: a split touches one element
    // and the array, a deletion of ten sequences ten elements and the array, whatever the size of
    // the model
    const int count = 2000;
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    std::vector<PDFObject> kids;
    std::vector<PDFObject> pageArray;
    for (int i = 0; i < count; ++i)
    {
        const PDFObjectReference element = d.reserve();
        d.set(element, Obj::element("P", root, d.page(), Obj::integer(i)));
        kids.push_back(Obj::reference(element));
        pageArray.push_back(Obj::reference(element));
    }
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array(std::move(kids)) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array(std::move(pageArray)) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(count + 1));

    PDFContentStructureDifference splitDifference;
    PDFContentStructureModelPointer split = model->applied({ model->getChangeForReorder(0, 700, false, count - 1) }, &splitDifference);
    QCOMPARE(splitDifference.getElementCount(), size_t(1));
    QCOMPARE(splitDifference.getParentTreeItemCount(), size_t(1));
    QVERIFY(split->reverted(splitDifference)->isEqual(*model));

    std::vector<PDFContentStructureChange> removals;
    for (int i = 0; i < 10; ++i)
    {
        removals.push_back(model->getChangeForDelete(0, 100 * i, true));
    }
    PDFContentStructureDifference removalDifference;
    PDFContentStructureModelPointer removed = model->applied(removals, &removalDifference);
    QCOMPARE(removalDifference.getElementCount(), size_t(10));
    QCOMPARE(removalDifference.getParentTreeItemCount(), size_t(1));
    QVERIFY(removed->reverted(removalDifference)->isEqual(*model));
}

void ContentStructureModelTest::versionOfModel()
{
    // The number of the version (request 3 of pb0s1_state.md, as PDFContentContainer::getVersion):
    // a new one for every model made by the default constructor, read, applied and
    // withoutChangesOfKeys (when it leaves something out); reverted and reapplied give the number
    // of the version they reproduce, so a command knows the model it was applied to also when the
    // object is another one. Three elements P_i /K i on one page, the array [P_0 P_1 P_2].
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    std::vector<PDFObject> kids;
    std::vector<PDFObject> pageArray;
    for (int i = 0; i < 3; ++i)
    {
        const PDFObjectReference element = d.reserve();
        d.set(element, Obj::element("P", root, d.page(), Obj::integer(i)));
        kids.push_back(Obj::reference(element));
        pageArray.push_back(Obj::reference(element));
    }
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array(std::move(kids)) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array(std::move(pageArray)) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    // ---- read and the default constructor: a new number each time, never 0; a copy keeps it ----
    const PDFContentStructureModelPointer model0 = PDFContentStructureModel::read(&document, nullptr);
    const PDFContentStructureModelPointer readAgain = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model0 && readAgain);
    QVERIFY(model0->isEqual(*readAgain));
    QVERIFY(model0->getVersion() != 0);
    QVERIFY(readAgain->getVersion() != 0);
    QVERIFY(model0->getVersion() != readAgain->getVersion());

    const PDFContentStructureModel empty;
    const PDFContentStructureModel otherEmpty;
    QVERIFY(empty.getVersion() != 0);
    QVERIFY(empty.getVersion() != otherEmpty.getVersion());
    QVERIFY(empty.getVersion() != model0->getVersion() && empty.getVersion() != readAgain->getVersion());

    const PDFContentStructureModel copy = *model0;
    QCOMPARE(copy.getVersion(), model0->getVersion());

    // ---- applied: a new number, which the difference names with the number before ----
    PDFContentStructureDifference differenceA;
    const PDFContentStructureChange split = model0->getChangeForReorder(0, 1, false, 2);
    QVERIFY(split.action == PDFContentStructureChange::Action::SplitSequence);
    const PDFContentStructureModelPointer model1 = model0->applied({ split }, &differenceA);
    QVERIFY(model1 && !model1->isEqual(*model0));
    QVERIFY(model1->getVersion() != 0);
    QVERIFY(model1->getVersion() != model0->getVersion() && model1->getVersion() != readAgain->getVersion());
    QCOMPARE(differenceA.getVersionBefore(), model0->getVersion());
    QCOMPARE(differenceA.getVersionAfter(), model1->getVersion());

    PDFContentStructureDifference differenceB;
    const PDFContentStructureModelPointer model2 = model1->applied({ model1->getChangeForDelete(0, 0, true) }, &differenceB);
    QVERIFY(model2 && !model2->isEqual(*model1));
    QVERIFY(model2->getVersion() != model1->getVersion() && model2->getVersion() != model0->getVersion());
    QCOMPARE(differenceB.getVersionBefore(), model1->getVersion());
    QCOMPARE(differenceB.getVersionAfter(), model2->getVersion());

    // Changes, which change nothing, still make a model of its own (with and without a difference)
    PDFContentStructureDifference differenceNone;
    const PDFContentStructureModelPointer unchanged = model2->applied({ model2->getChangeForDelete(0, 2, false) }, &differenceNone);
    QVERIFY(differenceNone.isEmpty() && unchanged->isEqual(*model2));
    QVERIFY(unchanged->getVersion() != model2->getVersion());
    QCOMPARE(differenceNone.getVersionBefore(), model2->getVersion());
    QCOMPARE(differenceNone.getVersionAfter(), unchanged->getVersion());
    const PDFContentStructureModelPointer unchangedWithoutDifference = model2->applied({ });
    QVERIFY(unchangedWithoutDifference->getVersion() != model2->getVersion() && unchangedWithoutDifference->getVersion() != unchanged->getVersion());

    // ---- reverted and reapplied: other objects, the numbers of the versions they reproduce ----
    const PDFContentStructureModelPointer undoneB = model2->reverted(differenceB);
    QVERIFY(undoneB && undoneB != model1 && undoneB->isEqual(*model1));
    QCOMPARE(undoneB->getVersion(), model1->getVersion());
    const PDFContentStructureModelPointer undoneA = undoneB->reverted(differenceA);
    QVERIFY(undoneA && undoneA != model0 && undoneA->isEqual(*model0));
    QCOMPARE(undoneA->getVersion(), model0->getVersion());

    const PDFContentStructureModelPointer redoneA = undoneA->reapplied(differenceA);
    QVERIFY(redoneA && redoneA != model1 && redoneA->isEqual(*model1));
    QCOMPARE(redoneA->getVersion(), model1->getVersion());
    const PDFContentStructureModelPointer redoneB = redoneA->reapplied(differenceB);
    QVERIFY(redoneB && redoneB->isEqual(*model2));
    QCOMPARE(redoneB->getVersion(), model2->getVersion());

    // A difference, which applied did not make, names no version: the model gets a new number
    const PDFContentStructureDifference noDifference;
    QCOMPARE(noDifference.getVersionBefore(), quint64(0));
    QCOMPARE(noDifference.getVersionAfter(), quint64(0));
    const PDFContentStructureModelPointer emptyReverted = empty.reverted(noDifference);
    const PDFContentStructureModelPointer emptyReapplied = empty.reapplied(noDifference);
    QVERIFY(emptyReverted && emptyReapplied);
    QVERIFY(emptyReverted->getVersion() != 0 && emptyReverted->getVersion() != empty.getVersion());
    QVERIFY(emptyReapplied->getVersion() != 0 && emptyReapplied->getVersion() != empty.getVersion() && emptyReapplied->getVersion() != emptyReverted->getVersion());

    // ---- withoutChangesOfKeys: a new number, when it leaves a change out; the same model, when
    //      it leaves nothing out ----
    QString errorText;
    const PDFContentStructureModelPointer withoutPage = model1->withoutChangesOfKeys(*model0, { 0 }, &errorText);
    QVERIFY2(withoutPage, qPrintable(errorText));
    QVERIFY(withoutPage->isEqual(*model0));
    QVERIFY(withoutPage->getVersion() != model0->getVersion() && withoutPage->getVersion() != model1->getVersion());
    const PDFContentStructureModelPointer withEverything = model1->withoutChangesOfKeys(*model0, { }, &errorText);
    QVERIFY(withEverything && withEverything->isEqual(*model1));
    QCOMPARE(withEverything->getVersion(), model1->getVersion());
}

// ---------------------------------------------------------------------------------------------
//  Number tree writer (ISO 32000-2, 7.9.7, Table 37)
// ---------------------------------------------------------------------------------------------

void ContentStructureModelTest::numberTreeEmpty()
{
    // No key: the root with an empty /Nums (the root has /Nums, when it has no /Kids)
    PDFDocumentBuilder builder;
    const size_t objectCount = Helper::objectCount(builder);
    const PDFObject root = PDFNumberTreeWriter::write(&builder, { });
    QVERIFY(root.isDictionary());
    QCOMPARE(root.getDictionary()->getCount(), size_t(1));
    QVERIFY(root.getDictionary()->get("Nums").isArray());
    QCOMPARE(root.getDictionary()->get("Nums").getArray()->getCount(), size_t(0));
    QCOMPARE(Helper::objectCount(builder), objectCount);
}

void ContentStructureModelTest::numberTreeRootOnly()
{
    // 64 keys 0, 3, 6, ..., 189 fit into one leaf (KEYS_PER_LEAF): the root holds /Nums itself, has
    // no /Kids and no /Limits ("shall be present in intermediate and leaf nodes only"), and no
    // object is added to the document
    QCOMPARE(PDFNumberTreeWriter::KEYS_PER_LEAF, size_t(64));
    QCOMPARE(PDFNumberTreeWriter::KIDS_PER_NODE, size_t(64));

    PDFDocumentBuilder builder;
    const size_t objectCount = Helper::objectCount(builder);
    const PDFObject root = PDFNumberTreeWriter::write(&builder, Helper::numberTreeItems(64, 3));
    QVERIFY(root.isDictionary());
    QCOMPARE(root.getDictionary()->getCount(), size_t(1));
    QVERIFY(!root.getDictionary()->hasKey("Kids"));
    QVERIFY(!root.getDictionary()->hasKey("Limits"));

    const PDFObject& numbers = root.getDictionary()->get("Nums");
    QVERIFY(numbers.isArray());
    QCOMPARE(numbers.getArray()->getCount(), size_t(128));
    for (size_t i = 0; i < 64; ++i)
    {
        // [key1 value1 key2 value2 ...], the keys ascending
        QVERIFY(numbers.getArray()->getItem(2 * i) == PDFObject::createInteger(PDFInteger(3 * i)));
        QVERIFY(numbers.getArray()->getItem(2 * i + 1) == PDFObject::createInteger(PDFInteger(1000 + i)));
    }
    QCOMPARE(Helper::objectCount(builder), objectCount);
}

void ContentStructureModelTest::numberTreeTwoLeaves()
{
    // 65 keys 0, 3, ..., 192: two leaves. The first one has the 64 keys 0 ... 189, the second one
    // the key 192. The root has /Kids with the two leaves, no /Nums and no /Limits; each leaf has
    // /Limits [least greatest] and /Nums, no /Kids.
    PDFDocumentBuilder builder;
    const size_t objectCount = Helper::objectCount(builder);
    const PDFObject root = PDFNumberTreeWriter::write(&builder, Helper::numberTreeItems(65, 3));
    QVERIFY(root.isDictionary());
    QCOMPARE(root.getDictionary()->getCount(), size_t(1));
    QVERIFY(!root.getDictionary()->hasKey("Nums"));
    QVERIFY(!root.getDictionary()->hasKey("Limits"));
    QCOMPARE(Helper::objectCount(builder), objectCount + 2);

    const PDFObject& kids = root.getDictionary()->get("Kids");
    QVERIFY(kids.isArray());
    QCOMPARE(kids.getArray()->getCount(), size_t(2));
    QVERIFY(kids.getArray()->getItem(0).isReference());
    QVERIFY(kids.getArray()->getItem(1).isReference());

    const PDFDictionary* first = Helper::dictionaryOf(builder, kids.getArray()->getItem(0).getReference());
    QVERIFY(first);
    QCOMPARE(first->getCount(), size_t(2));
    QCOMPARE(Helper::itemsOf(first->get("Limits")), QStringList({ "0", "189" }));
    QCOMPARE(first->get("Nums").getArray()->getCount(), size_t(128));
    QVERIFY(first->get("Nums").getArray()->getItem(0) == PDFObject::createInteger(0));
    QVERIFY(first->get("Nums").getArray()->getItem(126) == PDFObject::createInteger(189));
    QVERIFY(first->get("Nums").getArray()->getItem(127) == PDFObject::createInteger(1063));

    const PDFDictionary* second = Helper::dictionaryOf(builder, kids.getArray()->getItem(1).getReference());
    QVERIFY(second);
    QCOMPARE(second->getCount(), size_t(2));
    QCOMPARE(Helper::itemsOf(second->get("Limits")), QStringList({ "192", "192" }));
    QCOMPARE(Helper::itemsOf(second->get("Nums")), QStringList({ "192", "1064" }));
}

void ContentStructureModelTest::numberTreeFullRoot()
{
    // 4096 keys = 64 full leaves: they are the kids of the root, no intermediate node is needed
    PDFDocumentBuilder builder;
    const size_t objectCount = Helper::objectCount(builder);
    const PDFObject root = PDFNumberTreeWriter::write(&builder, Helper::numberTreeItems(4096, 1));
    QCOMPARE(Helper::objectCount(builder), objectCount + 64);

    const PDFObject& kids = root.getDictionary()->get("Kids");
    QVERIFY(kids.isArray());
    QCOMPARE(kids.getArray()->getCount(), size_t(64));

    // The last leaf: the keys 4032 ... 4095
    const PDFDictionary* last = Helper::dictionaryOf(builder, kids.getArray()->getItem(63).getReference());
    QVERIFY(last);
    QVERIFY(!last->hasKey("Kids"));
    QCOMPARE(Helper::itemsOf(last->get("Limits")), QStringList({ "4032", "4095" }));
    QCOMPARE(last->get("Nums").getArray()->getCount(), size_t(128));
}

void ContentStructureModelTest::numberTreeThreeLevels()
{
    // 4097 keys 0 ... 4096: 65 leaves (64 full ones and one with the key 4096), which is more than
    // KIDS_PER_NODE, so they get two intermediate nodes: the first one with the 64 full leaves
    // (/Limits [0 4095]) and the second one with the last leaf (/Limits [4096 4096]). The root has
    // the two intermediate nodes. Every leaf is at the depth 2: the tree is balanced.
    PDFDocumentBuilder builder;
    const size_t objectCount = Helper::objectCount(builder);
    const std::map<PDFInteger, PDFObject> items = Helper::numberTreeItems(4097, 1);
    const PDFObject root = PDFNumberTreeWriter::write(&builder, items);
    QCOMPARE(Helper::objectCount(builder), objectCount + 65 + 2);

    QVERIFY(root.isDictionary());
    QCOMPARE(root.getDictionary()->getCount(), size_t(1));
    const PDFObject& kids = root.getDictionary()->get("Kids");
    QVERIFY(kids.isArray());
    QCOMPARE(kids.getArray()->getCount(), size_t(2));

    const PDFDictionary* first = Helper::dictionaryOf(builder, kids.getArray()->getItem(0).getReference());
    QVERIFY(first);
    QCOMPARE(first->getCount(), size_t(2));
    QVERIFY(!first->hasKey("Nums"));
    QCOMPARE(Helper::itemsOf(first->get("Limits")), QStringList({ "0", "4095" }));
    QCOMPARE(first->get("Kids").getArray()->getCount(), size_t(64));

    // The sixth leaf of the first node: the keys 320 ... 383
    const PDFDictionary* leaf = Helper::dictionaryOf(builder, first->get("Kids").getArray()->getItem(5).getReference());
    QVERIFY(leaf);
    QVERIFY(!leaf->hasKey("Kids"));
    QCOMPARE(Helper::itemsOf(leaf->get("Limits")), QStringList({ "320", "383" }));
    QCOMPARE(leaf->get("Nums").getArray()->getCount(), size_t(128));
    QVERIFY(leaf->get("Nums").getArray()->getItem(0) == PDFObject::createInteger(320));
    QVERIFY(leaf->get("Nums").getArray()->getItem(1) == PDFObject::createInteger(1320));

    const PDFDictionary* second = Helper::dictionaryOf(builder, kids.getArray()->getItem(1).getReference());
    QVERIFY(second);
    QCOMPARE(Helper::itemsOf(second->get("Limits")), QStringList({ "4096", "4096" }));
    QCOMPARE(second->get("Kids").getArray()->getCount(), size_t(1));
    const PDFDictionary* lastLeaf = Helper::dictionaryOf(builder, second->get("Kids").getArray()->getItem(0).getReference());
    QVERIFY(lastLeaf);
    QCOMPARE(Helper::itemsOf(lastLeaf->get("Limits")), QStringList({ "4096", "4096" }));
    QCOMPARE(Helper::itemsOf(lastLeaf->get("Nums")), QStringList({ "4096", "5096" }));

    // The reader of number trees of the library gives the keys back, ascending
    const std::vector<std::pair<PDFInteger, PDFObject>> entries = PDFObjectUtils::readNumberTree(builder.getStorage(), root);
    QCOMPARE(entries.size(), size_t(4097));
    for (size_t i = 0; i < entries.size(); ++i)
    {
        QCOMPARE(entries[i].first, PDFInteger(i));
        QVERIFY(entries[i].second == PDFObject::createInteger(PDFInteger(1000 + i)));
    }
}

void ContentStructureModelTest::numberTreeValuesAndNoBuilder()
{
    // A value is written as it is: a reference stays a reference, a direct array keeps its null,
    // a negative key is a key as any other and comes first (the keys ascend numerically)
    PDFDocumentBuilder builder;
    const PDFObject array = Obj::array({ Obj::null(), Obj::reference(PDFObjectReference(5, 0)) });
    std::map<PDFInteger, PDFObject> items;
    items[4] = Obj::reference(PDFObjectReference(7, 0));
    items[-2] = array;
    items[9] = Obj::null();

    const PDFObject root = PDFNumberTreeWriter::write(&builder, items);
    const PDFObject& numbers = root.getDictionary()->get("Nums");
    QCOMPARE(Helper::itemsOf(numbers), QStringList({ "-2", "[...]", "4", "7 R", "9", "null" }));
    QVERIFY(numbers.getArray()->getItem(1) == array);
    QCOMPARE(Helper::itemsOf(numbers.getArray()->getItem(1)), QStringList({ "null", "5 R" }));

    // Without a document there is nowhere to put the leaves: the root holds all keys, which
    // Table 37 allows
    const PDFObject flat = PDFNumberTreeWriter::write(nullptr, Helper::numberTreeItems(100, 1));
    QVERIFY(flat.isDictionary());
    QVERIFY(!flat.getDictionary()->hasKey("Kids"));
    QCOMPARE(flat.getDictionary()->get("Nums").getArray()->getCount(), size_t(200));
}

// ---------------------------------------------------------------------------------------------
//  Structure tree writer
// ---------------------------------------------------------------------------------------------

void ContentStructureModelTest::writeUnmodified()
{
    // A model, which is not modified, writes nothing and touches nothing
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    for (const PDFContentStructureModelPointer& unmodified : { model, model->applied({ }), model->applied({ model->getChangeForDelete(0, 0, false) }) })
    {
        PDFDocumentBuilder builder(&t.document);
        const PDFObjectStorage before = *builder.getStorage();
        const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *unmodified);
        QVERIFY(!result.isWritten);
        QVERIFY(result.errors.empty());
        QVERIFY(*builder.getStorage() == before);
    }
}

void ContentStructureModelTest::writeRemovedSequence()
{
    // The last content of the sequence 0 (the element P) is deleted. Written:
    //   P            no /K any more; /Type /StructElem, /S /P, /P Document, /Pg page as before
    //   /ParentTree  a new node << /Nums [ 0 [null Span null Figure] 1 Link ] >>
    //   root         /ParentTree is the new node, /ParentTreeNextKey 2; /K, /IDTree, /RoleMap, /Type as before
    //   everything else as it was
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) });

    PDFDocumentBuilder builder(&t.document);
    const size_t objectCount = Helper::objectCount(builder);
    const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
    QVERIFY(result.isWritten);
    QVERIFY(result.errors.empty());

    // One new object: the node of the parent tree (its array of the page was direct and stays so)
    QCOMPARE(Helper::objectCount(builder), objectCount + 1);

    // The keys of a written object stay in their order: it differs only in what changed
    const PDFDictionary* paragraph = Helper::dictionaryOf(builder, t.paragraph);
    QVERIFY(paragraph);
    QCOMPARE(Helper::keysOf(paragraph), QStringList({ "Type", "S", "P", "Pg" }));
    QVERIFY(!paragraph->hasKey("K"));
    QVERIFY(paragraph->get("Type") == Obj::name("StructElem"));
    QVERIFY(paragraph->get("S") == Obj::name("P"));
    QVERIFY(paragraph->get("P") == Obj::reference(t.documentElement));
    QVERIFY(paragraph->get("Pg") == Obj::reference(t.page));

    for (const PDFObjectReference& reference : { t.documentElement, t.span, t.figure, t.link, t.annotation, t.page, t.parentTree })
    {
        QVERIFY(builder.getObjectByReference(reference) == t.document.getObjectByReference(reference));
    }

    const PDFDictionary* originalRoot = t.document.getDictionaryFromObject(t.document.getObjectByReference(t.root));
    const PDFDictionary* root = Helper::rootOf(builder);
    QVERIFY(root);
    QCOMPARE(Helper::keysOf(root), QStringList({ "Type", "K", "ParentTree", "ParentTreeNextKey", "IDTree", "RoleMap" }));
    QVERIFY(root->get("Type") == Obj::name("StructTreeRoot"));
    QVERIFY(root->get("K") == originalRoot->get("K"));
    QVERIFY(root->get("IDTree") == originalRoot->get("IDTree"));
    QVERIFY(root->get("RoleMap") == originalRoot->get("RoleMap"));
    QVERIFY(root->get("ParentTreeNextKey") == Obj::integer(2));
    QVERIFY(root->get("ParentTree").isReference());
    QVERIFY(root->get("ParentTree").getReference() != t.parentTree);

    const PDFDictionary* tree = builder.getDictionaryFromObject(root->get("ParentTree"));
    QVERIFY(tree);
    QCOMPARE(tree->getCount(), size_t(1));
    const PDFObject numbers = tree->get("Nums");
    QCOMPARE(Helper::itemsOf(numbers), QStringList({ "0", "[...]", "1", Helper::referenceText(t.link) }));
    QCOMPARE(Helper::itemsOf(numbers.getArray()->getItem(1)), QStringList({ "null", Helper::referenceText(t.span), "null", Helper::referenceText(t.figure) }));

    // Read back
    const PDFDocument written = builder.build();
    PDFContentStructureModelPointer readBack = PDFContentStructureModel::read(&written, nullptr);
    QVERIFY(readBack);
    QVERIFY(readBack->getIssues().empty());
    QVERIFY(!readBack->isModified());
    QCOMPARE(readBack->getElements().size(), size_t(6));
    QVERIFY(Helper::childrenOf(*readBack, t.paragraph).isEmpty());
    QCOMPARE(Helper::findElement(*readBack, t.paragraph)->type, QByteArray("P"));
    QCOMPARE(Helper::findElement(*readBack, t.paragraph)->page, t.page);
    QCOMPARE(Helper::parentTreeOf(*readBack, 0), Helper::numbers({ PDFObjectReference(), t.span, PDFObjectReference(), t.figure }));
    QCOMPARE(Helper::parentTreeOf(*readBack, 1), Helper::numbers({ t.link }));
    QCOMPARE(readBack->getNextParentTreeKey(), PDFInteger(2));
    QCOMPARE(readBack->getReadingOrder(0), (std::vector<PDFInteger>{ 1, 3 }));
}

void ContentStructureModelTest::writeSplitSequence()
{
    // The sequence 0 of P (/K 0) and the sequence 1 of Span (/K << /MCR >>) are split in one
    // command, the new identifiers are 4 and 5. Written:
    //   P     /K [0 4]                                                    (one child was not an array)
    //   Span  /K [ << /Type /MCR /Pg page /MCID 1 >> << /Type /MCR /Pg page /MCID 5 >> ]
    //   /Nums [ 0 [P Span null Figure P Span] 1 Link ]
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    const PDFContentStructureChange first = model->getChangeForMoveOut(0, 0, false, true, 3);
    const PDFContentStructureChange second = model->getChangeForReorder(0, 1, false, first.newMcid);
    PDFContentStructureModelPointer changed = model->applied({ first, second });

    PDFDocumentBuilder builder(&t.document);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);

    const PDFDictionary* paragraph = Helper::dictionaryOf(builder, t.paragraph);
    QCOMPARE(Helper::itemsOf(paragraph->get("K")), QStringList({ "0", "4" }));
    QVERIFY(paragraph->get("P") == Obj::reference(t.documentElement));

    const PDFDictionary* span = Helper::dictionaryOf(builder, t.span);
    QCOMPARE(span->getCount(), size_t(5));
    const PDFObject spanKids = span->get("K");
    QCOMPARE(Helper::itemsOf(spanKids), QStringList({ "<<...>>", "<<...>>" }));
    const QList<PDFInteger> expectedIdentifiers = { 1, 5 };
    for (size_t i = 0; i < 2; ++i)
    {
        const PDFDictionary* reference = spanKids.getArray()->getItem(i).getDictionary();
        QCOMPARE(reference->getCount(), size_t(3));
        QVERIFY(reference->get("Type") == Obj::name("MCR"));
        QVERIFY(reference->get("Pg") == Obj::reference(t.page));
        QVERIFY(reference->get("MCID") == Obj::integer(expectedIdentifiers[qsizetype(i)]));
    }

    const PDFObject numbers = Helper::parentTreeNumbers(builder);
    QCOMPARE(Helper::itemsOf(numbers), QStringList({ "0", "[...]", "1", Helper::referenceText(t.link) }));
    QCOMPARE(Helper::itemsOf(numbers.getArray()->getItem(1)), QStringList({ Helper::referenceText(t.paragraph), Helper::referenceText(t.span), "null", Helper::referenceText(t.figure),
                                                                           Helper::referenceText(t.paragraph), Helper::referenceText(t.span) }));

    // The elements, which did not change, are not written
    QVERIFY(builder.getObjectByReference(t.figure) == t.document.getObjectByReference(t.figure));
    QVERIFY(builder.getObjectByReference(t.documentElement) == t.document.getObjectByReference(t.documentElement));

    const PDFDocument written = builder.build();
    PDFContentStructureModelPointer readBack = PDFContentStructureModel::read(&written, nullptr);
    QVERIFY(readBack);
    QVERIFY(readBack->getIssues().empty());
    QCOMPARE(Helper::childrenOf(*readBack, t.paragraph), QStringList({ "M0", "M4" }));
    QCOMPARE(Helper::childrenOf(*readBack, t.span), QStringList({ QString("R1@%1").arg(t.page.objectNumber), QString("R5@%1").arg(t.page.objectNumber) }));
    QCOMPARE(readBack->getElementOfMarkedContent(0, 4), Helper::idOf(*readBack, t.paragraph));
    QCOMPARE(readBack->getElementOfMarkedContent(0, 5), Helper::idOf(*readBack, t.span));
    QCOMPARE(readBack->getReadingOrder(0), (std::vector<PDFInteger>{ 0, 4, 1, 5, 3 }));
}

void ContentStructureModelTest::writeOnlyModifiedElements()
{
    // U /Pg page /K [ n 0 R  << /Type /MCR /MCID 2 /Pg page >> ]  where the object n is the integer 1;
    // M /Pg page /K 0;  /Nums [0 [M U U]  3 a 0 R]  where the object a is the array [U null]
    // The content 0 of M is deleted. U did not change: its object is not written, so its /K keeps
    // the reference to the integer and the order of the keys of its marked-content reference (the
    // writer would write the integer 1 itself and /Type /Pg /MCID). The key 3 did not change
    // either: its value is the same reference to the same array.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference u = d.reserve();
    const PDFObjectReference m = d.reserve();
    const PDFObjectReference number = d.add(Obj::integer(1));
    const PDFObjectReference otherArray = d.add(Obj::array({ Obj::reference(u), Obj::null() }));
    d.set(u, Obj::element("P", root, d.page(),
                          Obj::array({ Obj::reference(number),
                                       Obj::dictionary({ { "Type", Obj::name("MCR") }, { "MCID", Obj::integer(2) }, { "Pg", Obj::reference(d.page()) } }) })));
    d.set(m, Obj::element("P", root, d.page(), Obj::integer(0)));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(u), Obj::reference(m) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(m), Obj::reference(u), Obj::reference(u) }),
                                                                                           Obj::integer(3), Obj::reference(otherArray) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QVERIFY(model->getIssues().empty());
    QCOMPARE(Helper::childrenOf(*model, u), QStringList({ "M1", QString("R2@%1").arg(d.page().objectNumber) }));

    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) });
    QVERIFY(!Helper::findElement(*changed, u)->isModified);

    PDFDocumentBuilder builder(&document);
    const size_t objectCount = Helper::objectCount(builder);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);
    QCOMPARE(Helper::objectCount(builder), objectCount + 1);
    QVERIFY(builder.getObjectByReference(u) == document.getObjectByReference(u));
    QCOMPARE(Helper::itemsOf(Helper::dictionaryOf(builder, u)->get("K")), QStringList({ Helper::referenceText(number), "<<...>>" }));
    QVERIFY(!Helper::dictionaryOf(builder, m)->hasKey("K"));

    const PDFObject numbers = Helper::parentTreeNumbers(builder);
    QCOMPARE(Helper::itemsOf(numbers), QStringList({ "0", "[...]", "3", Helper::referenceText(otherArray) }));
    QCOMPARE(Helper::itemsOf(numbers.getArray()->getItem(1)), QStringList({ "null", Helper::referenceText(u), Helper::referenceText(u) }));
    QVERIFY(builder.getObjectByReference(otherArray) == document.getObjectByReference(otherArray));
}

void ContentStructureModelTest::writeSingleChild()
{
    // P has /K 0: one object, not an array (Table 355 allows both). Its sequence is split (the new
    // identifier 4) and the content of the sequence 0 is deleted then: one child is left, and /K
    // is one object again - /K 4. The Figure has /K [3], an array: split (5) and with 3 deleted it
    // stays an array - /K [5].
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);

    PDFContentStructureModelPointer split = model->applied({ model->getChangeForReorder(0, 0, false, 3), model->getChangeForReorder(0, 3, false, 4) });
    PDFContentStructureModelPointer changed = split->applied({ split->getChangeForDelete(0, 0, true), split->getChangeForDelete(0, 3, true) });
    QCOMPARE(Helper::childrenOf(*changed, t.paragraph), QStringList({ "M4" }));
    QCOMPARE(Helper::childrenOf(*changed, t.figure), QStringList({ "M5" }));

    PDFDocumentBuilder builder(&t.document);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);

    QVERIFY(Helper::dictionaryOf(builder, t.paragraph)->get("K") == Obj::integer(4));
    QCOMPARE(Helper::itemsOf(Helper::dictionaryOf(builder, t.figure)->get("K")), QStringList({ "5" }));
    QCOMPARE(Helper::keysOf(Helper::dictionaryOf(builder, t.figure)), QStringList({ "Type", "S", "P", "Pg", "Alt", "ID", "K" }));
    QCOMPARE(Helper::itemsOf(Helper::parentTreeNumbers(builder).getArray()->getItem(1)),
             QStringList({ "null", Helper::referenceText(t.span), "null", "null", Helper::referenceText(t.paragraph), Helper::referenceText(t.figure) }));
}

void ContentStructureModelTest::writeKeepsUnknownEntries()
{
    // The element Q of readElementEntries loses its content. Every key of it, which the editor
    // did not change, is written as it was: /A (an array with a revision number), /C, /R, /Custom
    // with its null entry, /ID, and /T in its bytes (UTF-16BE; the writer of text strings of
    // the library would write the same text "Hi" in PDFDocEncoding as two bytes). /IDTree of
    // the root is not touched. W has values of wrong types: they stay too.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference q = d.reserve();
    const PDFObjectReference w = d.reserve();
    const PDFObjectReference attributeStream = d.add(Obj::stream({ { "O", Obj::name("Layout") }, { "Length", Obj::integer(0) } }));

    const PDFObject attributes = Obj::array({ Obj::dictionary({ { "O", Obj::name("Table") }, { "Scope", Obj::name("Row") }, { "Headers", Obj::array({ Obj::string("id-2") }) } }),
                                              Obj::integer(3),
                                              Obj::reference(attributeStream) });
    const PDFObject classes = Obj::array({ Obj::name("Cls1"), Obj::integer(2), Obj::name("Cls2") });
    const PDFObject custom = Obj::dictionary({ { "X", Obj::integer(1) }, { "Y", Obj::null() } });
    const QByteArray titleBytes = QByteArray::fromHex("FEFF00480069");
    const PDFObject idTree = Obj::dictionary({ { "Names", Obj::array({ Obj::string("id-1"), Obj::reference(q) }) } });

    d.set(q, Obj::element("Para", root, d.page(), Obj::integer(0),
                          { { "T", Obj::string(titleBytes) },
                            { "Lang", Obj::string("cs-CZ") },
                            { "Alt", Obj::string("alternate") },
                            { "E", Obj::string("expanded") },
                            { "ActualText", Obj::string("actual") },
                            { "ID", Obj::string("id-1") },
                            { "A", attributes },
                            { "C", classes },
                            { "R", Obj::integer(5) },
                            { "Custom", custom } }));
    d.set(w, Obj::dictionary({ { "Type", Obj::name("StructElem") },
                               { "S", Obj::integer(12) },
                               { "P", Obj::reference(root) },
                               { "T", Obj::integer(5) },
                               { "Pg", Obj::string("page") },
                               { "ID", Obj::name("Name") },
                               { "K", Obj::integer(1) } }));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(q), Obj::reference(w) }) },
                                  { "IDTree", idTree },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(q), Obj::reference(w) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true), model->getChangeForDelete(0, 1, true) });
    QVERIFY(Helper::findElement(*changed, q)->isModified);
    QVERIFY(Helper::findElement(*changed, w)->isModified);

    PDFDocumentBuilder builder(&document);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);

    // Q: the keys of the document in their order, without /K
    const PDFDictionary* writtenQ = Helper::dictionaryOf(builder, q);
    QVERIFY(writtenQ);
    QCOMPARE(Helper::keysOf(writtenQ), QStringList({ "Type", "S", "P", "Pg", "T", "Lang", "Alt", "E", "ActualText", "ID", "A", "C", "R", "Custom" }));
    QVERIFY(!writtenQ->hasKey("K"));
    QVERIFY(writtenQ->get("Alt") == Obj::string("alternate"));
    QVERIFY(writtenQ->get("E") == Obj::string("expanded"));
    QVERIFY(writtenQ->get("ActualText") == Obj::string("actual"));
    QVERIFY(writtenQ->get("Type") == Obj::name("StructElem"));
    QVERIFY(writtenQ->get("S") == Obj::name("Para"));
    QVERIFY(writtenQ->get("P") == Obj::reference(root));
    QVERIFY(writtenQ->get("Pg") == Obj::reference(d.page()));
    QVERIFY(writtenQ->get("T").isString());
    QCOMPARE(writtenQ->get("T").getString(), titleBytes);
    QVERIFY(writtenQ->get("Lang") == Obj::string("cs-CZ"));
    QVERIFY(writtenQ->get("ID") == Obj::string("id-1"));
    QVERIFY(writtenQ->get("A") == attributes);
    QVERIFY(writtenQ->get("C") == classes);
    QVERIFY(writtenQ->get("R") == Obj::integer(5));
    QVERIFY(writtenQ->get("Custom") == custom);
    QVERIFY(writtenQ->get("Custom").getDictionary()->hasKey("Y"));

    // W: the values of wrong types as they were, no /K
    const PDFDictionary* writtenW = Helper::dictionaryOf(builder, w);
    QVERIFY(writtenW);
    QCOMPARE(writtenW->getCount(), size_t(6));
    QVERIFY(writtenW->get("S") == Obj::integer(12));
    QVERIFY(writtenW->get("T") == Obj::integer(5));
    QVERIFY(writtenW->get("Pg") == Obj::string("page"));
    QVERIFY(writtenW->get("ID") == Obj::name("Name"));
    QVERIFY(writtenW->get("P") == Obj::reference(root));

    const PDFDictionary* writtenRoot = Helper::rootOf(builder);
    QVERIFY(writtenRoot->get("IDTree") == idTree);
    QVERIFY(builder.getObjectByReference(attributeStream) == document.getObjectByReference(attributeStream));
    QCOMPARE(Helper::itemsOf(Helper::parentTreeNumbers(builder).getArray()->getItem(1)), QStringList({ "null", "null" }));
}

void ContentStructureModelTest::writeKeepsForeignItems()
{
    // The document of readParentTreeItems without the second key 0. The content 3 (the element A)
    // is deleted, and the content 4, whose item of the array is a string. Written:
    //   /Nums [ 0 [A other null null null]  1 annotation  2 << /Weird true >>  3 A  5 (text) ]
    // "other" (no element of the model) keeps its index 1; the keys, which are not of the model,
    // are written as they were (RE-12).
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.reserve();
    const PDFObjectReference other = d.add(Obj::dictionary({ { "Type", Obj::name("Other") } }));
    const PDFObjectReference annotation = d.add(Obj::dictionary({ { "Type", Obj::name("Annot") } }));
    const PDFObject weird = Obj::dictionary({ { "Weird", PDFObject::createBool(true) } });

    d.set(a, Obj::element("P", root, d.page(), Obj::array({ Obj::integer(0), Obj::integer(3) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(a) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(other), Obj::null(), Obj::reference(a), Obj::string("junk") }),
                                                                                           Obj::integer(1), Obj::reference(annotation),
                                                                                           Obj::integer(2), weird,
                                                                                           Obj::integer(3), Obj::reference(a),
                                                                                           Obj::integer(5), Obj::string("text") }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 3, true), model->getChangeForDelete(0, 4, true) });
    QVERIFY(changed->isModified());
    QCOMPARE(Helper::childrenOf(*changed, a), QStringList({ "M0" }));

    PDFDocumentBuilder builder(&document);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);

    const PDFObject numbers = Helper::parentTreeNumbers(builder);
    QCOMPARE(Helper::itemsOf(numbers), QStringList({ "0", "[...]", "1", Helper::referenceText(annotation), "2", "<<...>>", "3", Helper::referenceText(a), "5", "(text)" }));
    QCOMPARE(Helper::itemsOf(numbers.getArray()->getItem(1)), QStringList({ Helper::referenceText(a), Helper::referenceText(other), "null", "null", "null" }));
    QVERIFY(numbers.getArray()->getItem(5) == weird);

    // /ParentTreeNextKey was missing: it is written, one above the largest key (Table 354)
    QVERIFY(Helper::rootOf(builder)->get("ParentTreeNextKey") == Obj::integer(6));
    QCOMPARE(Helper::keysOf(Helper::rootOf(builder)), QStringList({ "Type", "K", "ParentTree", "ParentTreeNextKey" }));

    // A (integer children in an array): /K [0]
    QCOMPARE(Helper::itemsOf(Helper::dictionaryOf(builder, a)->get("K")), QStringList({ "0" }));

    // Read back: the same kinds
    const PDFDocument written = builder.build();
    PDFContentStructureModelPointer readBack = PDFContentStructureModel::read(&written, nullptr);
    QVERIFY(readBack);
    QCOMPARE(readBack->getParentTree().size(), size_t(5));
    QVERIFY(readBack->getParentTree().at(1).kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(readBack->getParentTree().at(2).kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(readBack->getParentTree().at(3).kind == PDFContentParentTreeItem::Kind::Element);
    QCOMPARE(Helper::parentTreeOf(*readBack, 0), Helper::numbers({ a, PDFObjectReference(), PDFObjectReference(), PDFObjectReference(), PDFObjectReference() }));
}

void ContentStructureModelTest::writeIndirectArray()
{
    // The array of the page is an indirect object. The changed array is a new object: the old
    // one is not overwritten (a damaged document may use it for something else), and the tree
    // refers to the new one. Two new objects: the array and the node of the tree.
    const TaggedDocument t = Helper::createTagged(true);
    QVERIFY(t.pageArray.isValid());
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    QVERIFY(model->getParentTree().at(0).raw == Obj::reference(t.pageArray));
    QCOMPARE(Helper::parentTreeOf(*model, 0), Helper::numbers({ t.paragraph, t.span, PDFObjectReference(), t.figure }));

    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 3, true) });

    PDFDocumentBuilder builder(&t.document);
    const size_t objectCount = Helper::objectCount(builder);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);
    QCOMPARE(Helper::objectCount(builder), objectCount + 2);

    const PDFObject numbers = Helper::parentTreeNumbers(builder);
    QCOMPARE(numbers.getArray()->getCount(), size_t(4));
    const PDFObject& arrayValue = numbers.getArray()->getItem(1);
    QVERIFY(arrayValue.isReference());
    QVERIFY(arrayValue.getReference() != t.pageArray);
    QCOMPARE(Helper::itemsOf(builder.getObject(arrayValue)), QStringList({ Helper::referenceText(t.paragraph), Helper::referenceText(t.span), "null", "null" }));

    // The old array is as it was
    QCOMPARE(Helper::itemsOf(builder.getObjectByReference(t.pageArray)), QStringList({ Helper::referenceText(t.paragraph), Helper::referenceText(t.span), "null", Helper::referenceText(t.figure) }));
    QVERIFY(!Helper::dictionaryOf(builder, t.figure)->hasKey("K"));
}

void ContentStructureModelTest::writeBalancedParentTree()
{
    // A parent tree of 100 keys in two nodes: n1 has the keys 0 ... 49, n2 the keys 50 ... 99. The
    // key 0 is the page ([A]); the key i > 0 has the integer 10 * i, which is nothing of the model.
    // When the content 0 is deleted, the tree is written again whole: 100 keys are two leaves of
    // 64 and 36 keys (/Limits [0 63] and [64 99]) under a root with /Kids; the 99 other values
    // are as they were, and /ParentTreeNextKey is 100.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, d.page(), Obj::integer(0)));

    std::vector<PDFObject> firstNumbers = { Obj::integer(0), Obj::array({ Obj::reference(a) }) };
    std::vector<PDFObject> secondNumbers;
    for (int i = 1; i < 100; ++i)
    {
        std::vector<PDFObject>& numbers = i < 50 ? firstNumbers : secondNumbers;
        numbers.push_back(Obj::integer(i));
        numbers.push_back(Obj::integer(10 * i));
    }
    const PDFObjectReference n1 = d.add(Obj::dictionary({ { "Limits", pdftest::ContentTestDocuments::createIntegerArrayObject({ 0, 49 }) }, { "Nums", Obj::array(firstNumbers) } }));
    const PDFObjectReference n2 = d.add(Obj::dictionary({ { "Limits", pdftest::ContentTestDocuments::createIntegerArrayObject({ 50, 99 }) }, { "Nums", Obj::array(secondNumbers) } }));
    const PDFObjectReference tree = d.add(Obj::dictionary({ { "Kids", Obj::array({ Obj::reference(n1), Obj::reference(n2) }) } }));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::reference(a) }, { "ParentTree", Obj::reference(tree) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getParentTree().size(), size_t(100));
    QCOMPARE(model->getNextParentTreeKey(), PDFInteger(100));
    QVERIFY(model->getParentTree().at(73).raw == Obj::integer(730));

    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) });
    PDFDocumentBuilder builder(&document);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);

    const PDFDictionary* writtenRoot = Helper::rootOf(builder);
    QVERIFY(writtenRoot->get("ParentTreeNextKey") == Obj::integer(100));
    QVERIFY(writtenRoot->get("ParentTree").isReference());
    QVERIFY(writtenRoot->get("ParentTree").getReference() != tree);

    const PDFDictionary* writtenTree = builder.getDictionaryFromObject(writtenRoot->get("ParentTree"));
    QVERIFY(writtenTree);
    QCOMPARE(writtenTree->getCount(), size_t(1));
    const PDFObject kids = writtenTree->get("Kids");
    QVERIFY(kids.isArray());
    QCOMPARE(kids.getArray()->getCount(), size_t(2));

    const PDFDictionary* first = builder.getDictionaryFromObject(kids.getArray()->getItem(0));
    const PDFDictionary* second = builder.getDictionaryFromObject(kids.getArray()->getItem(1));
    QVERIFY(first && second);
    QCOMPARE(Helper::itemsOf(first->get("Limits")), QStringList({ "0", "63" }));
    QCOMPARE(Helper::itemsOf(second->get("Limits")), QStringList({ "64", "99" }));
    QCOMPARE(first->get("Nums").getArray()->getCount(), size_t(128));
    QCOMPARE(second->get("Nums").getArray()->getCount(), size_t(72));

    // The key 0: [null]; the key 63: 630; the key 64: 640; the key 99: 990
    QVERIFY(first->get("Nums").getArray()->getItem(0) == Obj::integer(0));
    QCOMPARE(Helper::itemsOf(first->get("Nums").getArray()->getItem(1)), QStringList({ "null" }));
    QVERIFY(first->get("Nums").getArray()->getItem(126) == Obj::integer(63));
    QVERIFY(first->get("Nums").getArray()->getItem(127) == Obj::integer(630));
    QVERIFY(second->get("Nums").getArray()->getItem(0) == Obj::integer(64));
    QVERIFY(second->get("Nums").getArray()->getItem(1) == Obj::integer(640));
    QVERIFY(second->get("Nums").getArray()->getItem(70) == Obj::integer(99));
    QVERIFY(second->get("Nums").getArray()->getItem(71) == Obj::integer(990));

    // The old nodes are not overwritten
    QVERIFY(builder.getObjectByReference(tree) == document.getObjectByReference(tree));
    QVERIFY(builder.getObjectByReference(n1) == document.getObjectByReference(n1));
}

void ContentStructureModelTest::writeSharedElementOnce()
{
    // root /K [B D];  B /K [C];  D /Pg page /K [C E 1];  C /Pg page /K 0;  /Nums [0 [C D]]
    // C is listed by B and by D: it is an element of B. The content 1 of D is deleted, so D is
    // written: its /K is [C E] - the reference to C, which is no child of D in the model, stays
    // at its place; C is not written and no copy of it appears (one new object: the tree node).
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference b = d.reserve();
    const PDFObjectReference c = d.reserve();
    const PDFObjectReference dd = d.reserve();
    const PDFObjectReference e = d.reserve();
    d.set(b, Obj::element("Sect", root, PDFObjectReference(), Obj::array({ Obj::reference(c) })));
    d.set(c, Obj::element("P", b, d.page(), Obj::integer(0)));
    d.set(dd, Obj::element("Sect", root, d.page(), Obj::array({ Obj::reference(c), Obj::reference(e), Obj::integer(1) })));
    d.set(e, Obj::element("P", dd, PDFObjectReference(), PDFObject()));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(b), Obj::reference(dd) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(c), Obj::reference(dd) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(5));
    QCOMPARE(Helper::findElement(*model, c)->parent, Helper::idOf(*model, b));
    QCOMPARE(Helper::childrenOf(*model, dd), QStringList({ Helper::elementText(e), "M1" }));

    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 1, true) });
    PDFDocumentBuilder builder(&document);
    const size_t objectCount = Helper::objectCount(builder);
    QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);
    QCOMPARE(Helper::objectCount(builder), objectCount + 1);

    QCOMPARE(Helper::itemsOf(Helper::dictionaryOf(builder, dd)->get("K")), QStringList({ Helper::referenceText(c), Helper::referenceText(e) }));
    QVERIFY(Helper::dictionaryOf(builder, dd)->get("P") == Obj::reference(root));
    QVERIFY(builder.getObjectByReference(c) == document.getObjectByReference(c));
    QVERIFY(builder.getObjectByReference(b) == document.getObjectByReference(b));
    QCOMPARE(Helper::itemsOf(Helper::parentTreeNumbers(builder).getArray()->getItem(1)), QStringList({ Helper::referenceText(c), "null" }));
}

void ContentStructureModelTest::writeDirectRoot()
{
    // The structure tree root is a direct dictionary of the catalog. /P of an element "shall be
    // an indirect reference" (Table 355), so a written root is an object:
    //   catalog /StructTreeRoot R;  R << /Type /StructTreeRoot /K [A] /ParentTree ... /ParentTreeNextKey 1 >>
    //   A: /P R, no /K
    StructureDocument d;
    const PDFObjectReference a = d.reserve();
    d.set(a, Obj::dictionary({ { "Type", Obj::name("StructElem") }, { "S", Obj::name("P") }, { "Pg", Obj::reference(d.page()) }, { "K", Obj::integer(0) } }));
    d.setRoot(Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                { "K", Obj::array({ Obj::reference(a) }) },
                                { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getElements().size(), size_t(2));
    QVERIFY(!model->getElements().front().reference.isValid());
    QCOMPARE(model->getElementOfMarkedContent(0, 0), Helper::idOf(*model, a));

    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) });
    PDFDocumentBuilder builder(&document);
    const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
    QVERIFY(result.isWritten);
    QVERIFY(result.errors.empty());

    const PDFDictionary* catalog = Helper::dictionaryOf(builder, builder.getCatalogReference());
    QVERIFY(catalog->get("StructTreeRoot").isReference());
    const PDFObjectReference rootReference = catalog->get("StructTreeRoot").getReference();
    const PDFDictionary* root = Helper::dictionaryOf(builder, rootReference);
    QVERIFY(root);
    QVERIFY(root->get("Type") == Obj::name("StructTreeRoot"));
    QCOMPARE(Helper::itemsOf(root->get("K")), QStringList({ Helper::referenceText(a) }));
    QVERIFY(root->get("ParentTreeNextKey") == Obj::integer(1));

    const PDFDictionary* writtenA = Helper::dictionaryOf(builder, a);
    QVERIFY(writtenA->get("P") == Obj::reference(rootReference));
    QVERIFY(!writtenA->hasKey("K"));
    QCOMPARE(Helper::itemsOf(Helper::parentTreeNumbers(builder).getArray()->getItem(1)), QStringList({ "null" }));

    const PDFDocument written = builder.build();
    PDFContentStructureModelPointer readBack = PDFContentStructureModel::read(&written, nullptr);
    QVERIFY(readBack);
    QCOMPARE(readBack->getElements().front().reference, rootReference);
    QVERIFY(Helper::childrenOf(*readBack, a).isEmpty());
}

void ContentStructureModelTest::writeErrors()
{
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) });

    // No builder
    {
        const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(nullptr, *changed);
        QVERIFY(!result.isWritten);
        QCOMPARE(result.errors.size(), size_t(1));
    }

    // Another document: one without a structure, and one with another structure tree root. With
    // an error nothing is written.
    {
        StructureDocument d;
        const PDFDocument other = d.build();
        PDFDocumentBuilder builder(&other);
        const PDFObjectStorage before = *builder.getStorage();
        const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
        QVERIFY(!result.isWritten);
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(!result.errors.front().isEmpty());
        QVERIFY(*builder.getStorage() == before);
    }
    {
        StructureDocument d;
        d.reserve();
        d.reserve();
        const PDFObjectReference root = d.add(Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") } }));
        d.setRoot(Obj::reference(root));
        const PDFDocument other = d.build();
        QVERIFY(root != t.root);

        PDFDocumentBuilder builder(&other);
        const PDFObjectStorage before = *builder.getStorage();
        const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
        QVERIFY(!result.isWritten);
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(*builder.getStorage() == before);
    }

    // The changed element is not a dictionary in the document any more
    {
        PDFDocumentBuilder builder(&t.document);
        builder.setObject(t.paragraph, Obj::integer(1));
        const PDFObjectStorage before = *builder.getStorage();
        const PDFStructureTreeWriteResult result = PDFStructureTreeWriter::write(&builder, *changed);
        QVERIFY(!result.isWritten);
        QCOMPARE(result.errors.size(), size_t(1));
        QVERIFY(*builder.getStorage() == before);
    }
}

void ContentStructureModelTest::writeCheck()
{
    // PDFStructureTreeWriter::check gives the error write would give, and writes nothing
    const TaggedDocument t = Helper::createTagged();
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) });

    // No builder: an unmodified model needs none, a modified one does
    QVERIFY(PDFStructureTreeWriter::check(nullptr, *model).isEmpty());
    QCOMPARE(PDFStructureTreeWriter::check(nullptr, *changed), PDFStructureTreeWriter::write(nullptr, *changed).errors.front());

    // The document of the model: no error, nothing written by the check, the write passes
    {
        PDFDocumentBuilder builder(&t.document);
        const PDFObjectStorage before = *builder.getStorage();
        QVERIFY(PDFStructureTreeWriter::check(&builder, *changed).isEmpty());
        QVERIFY(*builder.getStorage() == before);
        QVERIFY(PDFStructureTreeWriter::write(&builder, *changed).isWritten);
    }

    // Another document, and the changed element is not a dictionary any more: the errors of write
    {
        StructureDocument d;
        const PDFDocument other = d.build();
        PDFDocumentBuilder builder(&other);
        const QString error = PDFStructureTreeWriter::check(&builder, *changed);
        QVERIFY(!error.isEmpty());
        QCOMPARE(error, PDFStructureTreeWriter::write(&builder, *changed).errors.front());
    }
    {
        PDFDocumentBuilder builder(&t.document);
        builder.setObject(t.paragraph, Obj::integer(1));
        const QString error = PDFStructureTreeWriter::check(&builder, *changed);
        QVERIFY(!error.isEmpty());
        QCOMPARE(error, PDFStructureTreeWriter::write(&builder, *changed).errors.front());
    }
}

// ---------------------------------------------------------------------------------------------
//  The source of the model and the separation of the changes of a page
// ---------------------------------------------------------------------------------------------

void ContentStructureModelTest::sourceFingerprint()
{
    // The source fingerprint covers everything the reading takes from the document: the entry
    // /StructTreeRoot of the catalog, the root and every object reachable from it (the elements,
    // the arrays of /K, the nodes and arrays of /ParentTree, the role map), and /StructParents of
    // the pages and streams the children name. It does not enter a page, a content stream or an
    // annotation (/Pg, /Stm, /StmOwn, /Obj).
    const TaggedDocument t = Helper::createTagged(true);
    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&t.document, nullptr);
    QVERIFY(model);
    const QByteArray fingerprint = model->getSourceFingerprint();
    QVERIFY(!fingerprint.isEmpty());
    QCOMPARE(PDFContentStructureModel::computeSourceFingerprint(&t.document, nullptr), fingerprint);
    QCOMPARE(PDFContentStructureModel::read(&t.document, nullptr)->getSourceFingerprint(), fingerprint);

    // A change of the model keeps it: it says, where the model came from
    PDFContentStructureDifference difference;
    PDFContentStructureModelPointer changed = model->applied({ model->getChangeForDelete(0, 0, true) }, &difference);
    QCOMPARE(changed->getSourceFingerprint(), fingerprint);
    QCOMPARE(changed->reverted(difference)->getSourceFingerprint(), fingerprint);

    // Documents changed in one object
    struct Row
    {
        const char* name;
        std::function<void(PDFDocumentBuilder&)> change;
        bool isChanged;
    };
    const std::vector<Row> rows = {
        { "another /Alt of an element", [&t](PDFDocumentBuilder& b) { b.mergeTo(t.figure, Obj::dictionary({ { "Alt", Obj::string("Circle") } })); }, true },
        { "another /K of an element", [&t](PDFDocumentBuilder& b) { b.mergeTo(t.paragraph, Obj::dictionary({ { "K", Obj::integer(2) } })); }, true },
        { "another array of the page (an object of its own)", [&t](PDFDocumentBuilder& b) { b.setObject(t.pageArray, Obj::array({ Obj::reference(t.paragraph), Obj::null(), Obj::null(), Obj::reference(t.figure) })); }, true },
        { "another role map", [&t](PDFDocumentBuilder& b) { b.mergeTo(t.root, Obj::dictionary({ { "RoleMap", Obj::dictionary({ { "Para", Obj::name("H1") } }) } })); }, true },
        { "another /StructParents of the page", [&t](PDFDocumentBuilder& b) { b.mergeTo(t.page, Obj::dictionary({ { "StructParents", Obj::integer(5) } })); }, true },
        { "another rectangle of the annotation", [&t](PDFDocumentBuilder& b) { b.mergeTo(t.annotation, Obj::dictionary({ { "Rect", Obj::array({ Obj::integer(0), Obj::integer(0), Obj::integer(5), Obj::integer(5) }) } })); }, false },
        { "another rotation of the page", [&t](PDFDocumentBuilder& b) { b.mergeTo(t.page, Obj::dictionary({ { "Rotate", Obj::integer(90) } })); }, false },
    };
    for (const Row& row : rows)
    {
        PDFDocumentBuilder builder(&t.document);
        row.change(builder);
        const PDFDocument other = builder.build();
        QVERIFY2((PDFContentStructureModel::computeSourceFingerprint(&other, nullptr) != fingerprint) == row.isChanged, row.name);
    }

    // Documents without a structure: one fingerprint, not the one of a structure
    StructureDocument d1;
    StructureDocument d2;
    d2.addPage();
    const PDFDocument plain1 = d1.build();
    const PDFDocument plain2 = d2.build();
    const QByteArray plainFingerprint = PDFContentStructureModel::read(&plain1, nullptr)->getSourceFingerprint();
    QVERIFY(!plainFingerprint.isEmpty());
    QCOMPARE(PDFContentStructureModel::computeSourceFingerprint(&plain2, nullptr), plainFingerprint);
    QVERIFY(plainFingerprint != fingerprint);

    // Canceled: no fingerprint
    CancelAfter canceled(0);
    QVERIFY(PDFContentStructureModel::computeSourceFingerprint(&t.document, &canceled).isEmpty());
}

void ContentStructureModelTest::sourceFingerprintWalk()
{
    // The walk of the fingerprint has one visited set: a cycle ends, a node shared by two paths
    // is hashed once - and a change of it is seen
    //   root /K [X];  X /K [Y Z];  Y /K [X Z];  Z /S /Figure /Alt (a)
    const auto create = [](const QByteArray& alt)
    {
        StructureDocument d;
        const PDFObjectReference root = d.reserve();
        const PDFObjectReference x = d.reserve();
        const PDFObjectReference y = d.reserve();
        const PDFObjectReference z = d.reserve();
        d.set(x, Obj::element("Sect", root, PDFObjectReference(), Obj::array({ Obj::reference(y), Obj::reference(z) })));
        d.set(y, Obj::element("Sect", x, PDFObjectReference(), Obj::array({ Obj::reference(x), Obj::reference(z) })));
        d.set(z, Obj::element("Figure", x, PDFObjectReference(), PDFObject(), { { "Alt", Obj::string(alt) } }));
        d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") }, { "K", Obj::array({ Obj::reference(x) }) } }));
        d.setRoot(Obj::reference(root));
        return d.build();
    };

    const PDFDocument first = create("a");
    const PDFDocument second = create("b");
    const QByteArray fingerprint = PDFContentStructureModel::computeSourceFingerprint(&first, nullptr);
    QVERIFY(!fingerprint.isEmpty());
    QCOMPARE(PDFContentStructureModel::computeSourceFingerprint(&first, nullptr), fingerprint);
    QVERIFY(PDFContentStructureModel::computeSourceFingerprint(&second, nullptr) != fingerprint);

    PDFContentStructureModelPointer model = PDFContentStructureModel::read(&first, nullptr);
    QVERIFY(model);
    QCOMPARE(model->getSourceFingerprint(), fingerprint);
}

void ContentStructureModelTest::withoutChangesOfKeys()
{
    // Two tagged pages (page 1 /StructParents 0, page 2 1):
    //   root /K [A B C];  A /Pg page1 /K [0 1];  B /Pg page2 /K [0 1];
    //   C /K [ << /Type /MCR /Pg page1 /MCID 2 >> << /Type /MCR /Pg page2 /MCID 2 >> ]
    //   D /K << /Type /MCR /Pg page2 /MCID 3 /Extra 1 >>   (a foreign child, which names the content 3 of the page 2)
    //   /Nums [ 0 [A A C]  1 [B B C D] ]
    // The changes of the key 1 taken out of a model give the model of the other changes alone.
    StructureDocument d;
    const PDFObjectReference page2 = d.addPage();
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.reserve();
    const PDFObjectReference b = d.reserve();
    const PDFObjectReference c = d.reserve();
    const PDFObjectReference dd = d.reserve();
    d.set(a, Obj::element("P", root, d.page(), Obj::array({ Obj::integer(0), Obj::integer(1) })));
    d.set(b, Obj::element("P", root, page2, Obj::array({ Obj::integer(0), Obj::integer(1) })));
    d.set(c, Obj::element("Span", root, PDFObjectReference(), Obj::array({ Obj::markedContentReference(d.page(), 2), Obj::markedContentReference(page2, 2) })));
    d.set(dd, Obj::element("Span", root, PDFObjectReference(), Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(page2) }, { "MCID", Obj::integer(3) }, { "Extra", Obj::integer(1) } })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(a), Obj::reference(b), Obj::reference(c), Obj::reference(dd) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(a), Obj::reference(c) }),
                                                                                           Obj::integer(1), Obj::array({ Obj::reference(b), Obj::reference(b), Obj::reference(c), Obj::reference(dd) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setStructuralParents(page2, 1);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer original = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(original);
    QCOMPARE(Helper::childrenOf(*original, dd), QStringList());

    // The sequence 0 of both pages deleted: without the key 1 = the deletion of the page 1 alone
    PDFContentStructureModelPointer both = original->applied({ Helper::removal(0, 0), Helper::removal(1, 0) });
    QString errorText;
    PDFContentStructureModelPointer separated = both->withoutChangesOfKeys(*original, { 1 }, &errorText);
    QVERIFY2(separated, qPrintable(errorText));
    QVERIFY(separated->isEqual(*original->applied({ Helper::removal(0, 0) })));
    QCOMPARE(Helper::childrenOf(*separated, b), QStringList({ "M0", "M1" }));
    QCOMPARE(Helper::parentTreeOf(*separated, 1), Helper::numbers({ b, b, c, dd }));

    // The content 3 of the page 2 deleted with the page 1's 0: D loses its foreign child (the
    // content is irregular); without the key 1 it has it again
    PDFContentStructureModelPointer foreign = original->applied({ Helper::removal(0, 0), Helper::removal(1, 3) });
    QVERIFY(foreign->getElement(Helper::idOf(*original, dd))->isModified);
    PDFContentStructureModelPointer foreignSeparated = foreign->withoutChangesOfKeys(*original, { 1 }, nullptr);
    QVERIFY(foreignSeparated && foreignSeparated->isEqual(*original->applied({ Helper::removal(0, 0) })));
    QVERIFY(!separated->getElement(Helper::idOf(*separated, b))->isModified);
    QVERIFY(!separated->getParentTree().at(1).isModified);
    QVERIFY(separated->isModified());

    // No key, a key without a change: the model as it is
    QVERIFY(both->withoutChangesOfKeys(*original, { }, nullptr)->isEqual(*both));
    QVERIFY(both->withoutChangesOfKeys(*original, { 7 }, nullptr)->isEqual(*both));

    // Both keys: the original, not modified
    PDFContentStructureModelPointer none = both->withoutChangesOfKeys(*original, { 0, 1 }, nullptr);
    QVERIFY(none && none->isEqual(*original) && !none->isModified());

    // A split of the page 2 (the new identifier 4: the array of the key 1 has four items): its
    // key taken out gives the original, the identifiers in use included
    PDFContentStructureModelPointer split = original->applied({ original->getChangeForReorder(1, 0, false, 3) });
    QCOMPARE(Helper::childrenOf(*split, b), QStringList({ "M0", "M4", "M1" }));
    PDFContentStructureModelPointer splitSeparated = split->withoutChangesOfKeys(*original, { 1 }, nullptr);
    QVERIFY(splitSeparated && splitSeparated->isEqual(*original));

    // C lists content of both pages: the deletion of its two sequences is separated child by
    // child (step B0, review R2-3): C keeps the child of the key left out, at its place
    PDFContentStructureModelPointer shared = original->applied({ Helper::removal(0, 2), Helper::removal(1, 2) });
    errorText.clear();
    PDFContentStructureModelPointer sharedWithout1 = shared->withoutChangesOfKeys(*original, { 1 }, &errorText);
    QVERIFY2(sharedWithout1, qPrintable(errorText));
    QVERIFY(sharedWithout1->isEqual(*original->applied({ Helper::removal(0, 2) })));
    QCOMPARE(Helper::childrenOf(*sharedWithout1, c), QStringList({ QString("R2@%1").arg(page2.objectNumber) }));
    PDFContentStructureModelPointer sharedWithout0 = shared->withoutChangesOfKeys(*original, { 0 }, nullptr);
    QVERIFY(sharedWithout0 && sharedWithout0->isEqual(*original->applied({ Helper::removal(1, 2) })));
    QCOMPARE(Helper::childrenOf(*sharedWithout0, c), QStringList({ QString("R2@%1").arg(d.page().objectNumber) }));
    QVERIFY(shared->withoutChangesOfKeys(*original, { 0, 1 }, nullptr)->isEqual(*original));

    PDFContentStructureModelPointer one = original->applied({ Helper::removal(0, 2), Helper::removal(1, 0) });
    PDFContentStructureModelPointer oneSeparated = one->withoutChangesOfKeys(*original, { 1 }, nullptr);
    QVERIFY(oneSeparated && oneSeparated->isEqual(*original->applied({ Helper::removal(0, 2) })));
    QCOMPARE(Helper::childrenOf(*oneSeparated, c), QStringList({ QString("R2@%1").arg(page2.objectNumber) }));
}

void ContentStructureModelTest::withoutChangesOfUnknownContent()
{
    // A /K [0 1] without /Pg (the page of its children is not known); /Nums [0 [A A]]: the
    // deletion of the sequence 0 removes the child of A, which the parent tree names - a change,
    // whose page the model does not know. It cannot be taken apart from a change of the key 0.
    StructureDocument d;
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference a = d.add(Obj::element("P", root, PDFObjectReference(), Obj::array({ Obj::integer(0), Obj::integer(1) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::reference(a) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(a), Obj::reference(a) }) }) } }) } }));
    d.setStructuralParents(d.page(), 0);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    PDFContentStructureModelPointer original = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(original);
    PDFContentStructureModelPointer changed = original->applied({ original->getChangeForDelete(0, 0, true) });
    QCOMPARE(Helper::childrenOf(*changed, a), QStringList({ "M1" }));

    QString errorText;
    QVERIFY(changed->withoutChangesOfKeys(*original, { 0 }, &errorText) == nullptr);
    QVERIFY(!errorText.isEmpty());

    // A key, which has no change, takes nothing out
    QVERIFY(changed->withoutChangesOfKeys(*original, { 3 }, nullptr)->isEqual(*changed));
}

void ContentStructureModelTest::withoutChangesOfKeysChildByChild()
{
    // Review R2-3: an element, which lists tagged content of a skipped page and of a written page
    // (a paragraph across a page break), keeps the children of the skipped page as the document
    // has them and gets the changes of the written page; /K keeps the reading order. Two pages
    // (page 1 /StructParents 0, page 2 1):
    //   root /K [P Q F]
    //   P /Pg page1 /K [0 1 R(p2,0) R(p2,1)]                     a paragraph across the page break
    //   Q /Pg page1 /K [R(p2,2) 2 R(p2,3)]                       content of the page 1 between content of the page 2
    //   F /Pg page1 /K [3 R(p2,4) << /Type /MCR /Pg page2 /MCID 5 /Extra 1 >>]   (a foreign child)
    //   /Nums [ 0 [P P Q F]  1 [P P Q Q F F] ]
    // R(p,m) = << /Type /MCR /Pg p /MCID m >>. A split gives the rest of a sequence the identifier
    // above the array of its key (4 on the page 1, 6 on the page 2) right behind its original.
    StructureDocument d;
    const PDFObjectReference page1 = d.page();
    const PDFObjectReference page2 = d.addPage();
    const PDFObjectReference root = d.reserve();
    const PDFObjectReference p = d.reserve();
    const PDFObjectReference q = d.reserve();
    const PDFObjectReference f = d.reserve();
    d.set(p, Obj::element("P", root, page1, Obj::array({ Obj::integer(0), Obj::integer(1), Obj::markedContentReference(page2, 0), Obj::markedContentReference(page2, 1) })));
    d.set(q, Obj::element("P", root, page1, Obj::array({ Obj::markedContentReference(page2, 2), Obj::integer(2), Obj::markedContentReference(page2, 3) })));
    d.set(f, Obj::element("P", root, page1, Obj::array({ Obj::integer(3), Obj::markedContentReference(page2, 4),
                                                          Obj::dictionary({ { "Type", Obj::name("MCR") }, { "Pg", Obj::reference(page2) }, { "MCID", Obj::integer(5) }, { "Extra", Obj::integer(1) } }) })));
    d.set(root, Obj::dictionary({ { "Type", Obj::name("StructTreeRoot") },
                                  { "K", Obj::array({ Obj::reference(p), Obj::reference(q), Obj::reference(f) }) },
                                  { "ParentTree", Obj::dictionary({ { "Nums", Obj::array({ Obj::integer(0), Obj::array({ Obj::reference(p), Obj::reference(p), Obj::reference(q), Obj::reference(f) }),
                                                                                           Obj::integer(1), Obj::array({ Obj::reference(p), Obj::reference(p), Obj::reference(q), Obj::reference(q), Obj::reference(f), Obj::reference(f) }) }) } }) } }));
    d.setStructuralParents(page1, 0);
    d.setStructuralParents(page2, 1);
    d.setRoot(Obj::reference(root));
    const PDFDocument document = d.build();

    const PDFContentStructureModelPointer original = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(original);
    const QString n2 = QString::number(page2.objectNumber);
    QCOMPARE(Helper::childrenOf(*original, p), QStringList({ "M0", "M1", "R0@" + n2, "R1@" + n2 }));
    QCOMPARE(Helper::childrenOf(*original, q), QStringList({ "R2@" + n2, "M2", "R3@" + n2 }));
    QCOMPARE(Helper::childrenOf(*original, f), QStringList({ "M3", "R4@" + n2 }));

    // The changed model without the changes of the keys
    const auto separate = [&original](const PDFContentStructureModelPointer& changed, std::set<PDFInteger> keys, QString* errorText)
    {
        return changed->withoutChangesOfKeys(*original, keys, errorText);
    };

    // The splits (a sequence split in the middle) of each page
    const PDFContentStructureChange splitP1 = original->getChangeForReorder(0, 0, false, 3);
    const PDFContentStructureChange splitP2 = original->getChangeForReorder(1, 0, false, 5);
    QVERIFY(splitP1.action == PDFContentStructureChange::Action::SplitSequence && splitP1.newMcid == 4);
    QVERIFY(splitP2.action == PDFContentStructureChange::Action::SplitSequence && splitP2.newMcid == 6);

    // ---- A removal on each page ----
    {
        PDFContentStructureModelPointer changed = original->applied({ Helper::removal(0, 1), Helper::removal(1, 0) });
        QCOMPARE(Helper::childrenOf(*changed, p), QStringList({ "M0", "R1@" + n2 }));

        // The page 2 skipped: P keeps R(p2,0) at its place, loses 1 of the page 1
        QString errorText;
        PDFContentStructureModelPointer without1 = separate(changed, { 1 }, &errorText);
        QVERIFY2(without1, qPrintable(errorText));
        QCOMPARE(Helper::childrenOf(*without1, p), QStringList({ "M0", "R0@" + n2, "R1@" + n2 }));
        QCOMPARE(Helper::parentTreeOf(*without1, 0), QList<PDFInteger>({ p.objectNumber, 0, q.objectNumber, f.objectNumber }));
        QCOMPARE(Helper::parentTreeOf(*without1, 1), Helper::numbers({ p, p, q, q, f, f }));
        QVERIFY(without1->isEqual(*original->applied({ Helper::removal(0, 1) })));
        QVERIFY(without1->getElement(Helper::idOf(*without1, p))->isModified);

        // The page 1 skipped: P keeps 1, loses R(p2,0)
        PDFContentStructureModelPointer without0 = separate(changed, { 0 }, nullptr);
        QVERIFY(without0);
        QCOMPARE(Helper::childrenOf(*without0, p), QStringList({ "M0", "M1", "R1@" + n2 }));
        QVERIFY(without0->isEqual(*original->applied({ Helper::removal(1, 0) })));
    }

    // ---- A split on the page 2 and a removal on the page 1 ----
    {
        PDFContentStructureModelPointer changed = original->applied({ splitP2 })->applied({ Helper::removal(0, 0) });
        QCOMPARE(Helper::childrenOf(*changed, p), QStringList({ "M1", "R0@" + n2, "R6@" + n2, "R1@" + n2 }));

        // The page 1 skipped (the page 2 with the split written): 0 at its place in front
        PDFContentStructureModelPointer without0 = separate(changed, { 0 }, nullptr);
        QVERIFY(without0);
        QCOMPARE(Helper::childrenOf(*without0, p), QStringList({ "M0", "M1", "R0@" + n2, "R6@" + n2, "R1@" + n2 }));
        QCOMPARE(Helper::parentTreeOf(*without0, 1), Helper::numbers({ p, p, q, q, f, f, p }));
        QVERIFY(without0->isEqual(*original->applied({ splitP2 })));

        // The page 2 skipped (the page 1 with the removal written): no new identifier of the page 2
        PDFContentStructureModelPointer without1 = separate(changed, { 1 }, nullptr);
        QVERIFY(without1);
        QCOMPARE(Helper::childrenOf(*without1, p), QStringList({ "M1", "R0@" + n2, "R1@" + n2 }));
        QCOMPARE(Helper::parentTreeOf(*without1, 1), Helper::numbers({ p, p, q, q, f, f }));
        QVERIFY(without1->isEqual(*original->applied({ Helper::removal(0, 0) })));
    }

    // ---- A split on the page 1 and a removal on the page 2 (the changed page skipped, the other
    //      one written, and the other way round) ----
    {
        PDFContentStructureModelPointer changed = original->applied({ splitP1 })->applied({ Helper::removal(1, 1) });
        QCOMPARE(Helper::childrenOf(*changed, p), QStringList({ "M0", "M4", "M1", "R0@" + n2 }));

        PDFContentStructureModelPointer without1 = separate(changed, { 1 }, nullptr);
        QVERIFY(without1);
        QCOMPARE(Helper::childrenOf(*without1, p), QStringList({ "M0", "M4", "M1", "R0@" + n2, "R1@" + n2 }));
        QVERIFY(without1->isEqual(*original->applied({ splitP1 })));

        PDFContentStructureModelPointer without0 = separate(changed, { 0 }, nullptr);
        QVERIFY(without0);
        QCOMPARE(Helper::childrenOf(*without0, p), QStringList({ "M0", "M1", "R0@" + n2 }));
        QVERIFY(without0->isEqual(*original->applied({ Helper::removal(1, 1) })));
    }

    // ---- A split on the page 2, whose original is removed afterwards, and a removal on the
    //      page 1: the new part goes behind the place of its original (an MCR of the page 2,
    //      never one of the integers of the page 1), so the restored 1 comes in front of it ----
    {
        PDFContentStructureModelPointer changed = original->applied({ splitP2 })->applied({ Helper::removal(1, 0), Helper::removal(0, 1) });
        QCOMPARE(Helper::childrenOf(*changed, p), QStringList({ "M0", "R6@" + n2, "R1@" + n2 }));

        PDFContentStructureModelPointer without0 = separate(changed, { 0 }, nullptr);
        QVERIFY(without0);
        QCOMPARE(Helper::childrenOf(*without0, p), QStringList({ "M0", "M1", "R6@" + n2, "R1@" + n2 }));
        QVERIFY(without0->isEqual(*original->applied({ splitP2 })->applied({ Helper::removal(1, 0) })));

        PDFContentStructureModelPointer without1 = separate(changed, { 1 }, nullptr);
        QVERIFY(without1);
        QCOMPARE(Helper::childrenOf(*without1, p), QStringList({ "M0", "R0@" + n2, "R1@" + n2 }));
        QVERIFY(without1->isEqual(*original->applied({ Helper::removal(0, 1) })));
    }

    // ---- Still refused: Q lists 2 of the page 1 between R(p2,2) and R(p2,3). R(p2,3) is split
    //      (the new part R(p2,6) right behind it), then R(p2,3) and 2 are removed: Q is
    //      [R(p2,2) R(p2,6)]. Without the page 1 the restored 2 belongs in front of R(p2,6), when
    //      R(p2,6) came from R(p2,3), but behind it, when it came from R(p2,2) - the two models do
    //      not tell, so the skipped page 1 is refused. Without the page 2 nothing of the page 2 is
    //      placed anew: separable. ----
    {
        const PDFContentStructureChange splitQ = original->getChangeForReorder(1, 3, false, 5);
        QVERIFY(splitQ.action == PDFContentStructureChange::Action::SplitSequence && splitQ.newMcid == 6);
        PDFContentStructureModelPointer changed = original->applied({ splitQ })->applied({ Helper::removal(1, 3), Helper::removal(0, 2) });
        QCOMPARE(Helper::childrenOf(*changed, q), QStringList({ "R2@" + n2, "R6@" + n2 }));

        QString errorText;
        QVERIFY(separate(changed, { 0 }, &errorText) == nullptr);
        QVERIFY2(errorText.contains(QStringLiteral("cannot be left out alone")), qPrintable(errorText));
        QVERIFY2(errorText.contains(QStringLiteral("split")), qPrintable(errorText));

        PDFContentStructureModelPointer without1 = separate(changed, { 1 }, nullptr);
        QVERIFY(without1);
        QCOMPARE(Helper::childrenOf(*without1, q), QStringList({ "R2@" + n2, "R3@" + n2 }));
        QVERIFY(without1->isEqual(*original->applied({ Helper::removal(0, 2) })));

        // The same split with its original kept: R(p2,6) is right behind R(p2,3), separable
        PDFContentStructureModelPointer kept = original->applied({ splitQ })->applied({ Helper::removal(0, 2) });
        PDFContentStructureModelPointer keptWithout0 = separate(kept, { 0 }, nullptr);
        QVERIFY(keptWithout0);
        QCOMPARE(Helper::childrenOf(*keptWithout0, q), QStringList({ "R2@" + n2, "M2", "R3@" + n2, "R6@" + n2 }));
        QVERIFY(keptWithout0->isEqual(*original->applied({ splitQ })));
    }

    // ---- Still refused: F has a foreign child (an item of /K the model does not read); its
    //      place among the children of the two pages is not separated ----
    {
        PDFContentStructureModelPointer changed = original->applied({ Helper::removal(0, 3), Helper::removal(1, 4) });
        QString errorText;
        QVERIFY(separate(changed, { 1 }, &errorText) == nullptr);
        QVERIFY2(errorText.contains(QStringLiteral("cannot be left out alone")), qPrintable(errorText));
        QVERIFY2(errorText.contains(QStringLiteral("does not read")), qPrintable(errorText));

        // A change of the page 1 alone in F is no mixture
        PDFContentStructureModelPointer one = original->applied({ Helper::removal(0, 3) });
        QVERIFY(separate(one, { 1 }, nullptr)->isEqual(*one));
        QVERIFY(separate(one, { 0 }, nullptr)->isEqual(*original));
    }

    // ---- Every page of P changed on both, more keys left out at once: the original ----
    {
        PDFContentStructureModelPointer changed = original->applied({ splitP2 })->applied({ Helper::removal(0, 0), Helper::removal(1, 1) });
        PDFContentStructureModelPointer none = separate(changed, { 0, 1 }, nullptr);
        QVERIFY(none && none->isEqual(*original) && !none->isModified());
    }
}

QTEST_MAIN(ContentStructureModelTest)

#include "tst_contentstructuremodeltest.moc"
