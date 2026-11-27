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

// Edit Content, stage E0: the tool edit.content of the core, the writer of a snapshot into a
// document and the JSON of the tool (package P05; GUI_overhaul/edit_tool/zadani_jadro.md 4.4, 4.6,
// zadani_overeni.md 6.2, 7.1, chapter 8).
//
// The expected values are written by hand from the content streams of the tests, from the plan
// and from ISO 32000-2, never taken from a run. A written document is checked twice: by its
// objects and bytes against hand-written values, and by building its content again.
//
// The documents have pages [0 0 200 200]. The page of two paths (TWO_PATHS) has six operations
//
//   0  1 0 0 rg          a state run          node 2
//   1  10 10 50 50 re    a path               node 3
//   2  f
//   3  0 0 1 rg          a state run          node 4
//   4  100 100 20 20 re  a path               node 5
//   5  f
//
// separated by one line feed and without one at the end (the tree builder numbers the nodes by a
// depth first walk from the root 1).

#include "contenttestdocuments.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdocumentwriter.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentjson.h"
#include "pdfcontentnode.h"
#include "pdfcontentoperation.h"
#include "pdfcontentresources.h"
#include "pdfcontentstate.h"
#include "pdfcontentstreamwriter.h"
#include "pdfcontentstructure.h"
#include "pdfcontenttreebuilder.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttool_content.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfobject.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfprogress.h"
#include "pdfrenderer.h"
#include "pdfsecurityhandler.h"

#include <QtTest>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTransform>
#include <QVariantMap>

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <vector>

using namespace pdf;

using Documents = pdftest::ContentTestDocuments;
using Tool = PDFDocumentToolContent;
using Writer = PDFContentDocumentWriter;
using Status = PDFContentWriteResult::Status;

/// Cancels after a number of questions (0 = canceled from the beginning)
class ContentToolTestControl : public PDFOperationControl
{
public:
    explicit ContentToolTestControl(int questions) : m_remaining(questions) { }

    virtual bool isOperationCanceled() const override { return m_remaining.fetch_sub(1) <= 0; }

private:
    mutable std::atomic<int> m_remaining;
};

/// Counts the calls of a progress
class ContentToolTestProgress : public PDFProgressSink
{
public:
    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) override { Q_UNUSED(startupInfo); ++starts; steps = stepCount; }
    virtual void step() override { ++stepsDone; }
    virtual void finish() override { ++finishes; }

    int starts = 0;
    int finishes = 0;
    size_t steps = 0;
    size_t stepsDone = 0;
};

/// A command of a test: it replaces the version of a container by a given one (a session takes
/// a new version of a container from a command only)
class ContentToolTestReplaceCommand : public PDFContentCommand
{
public:
    explicit ContentToolTestReplaceCommand(PDFContentContainerPointer container) : m_container(std::move(container)) { }

    virtual bool apply(PDFContentEditSession* session, QString* errorText) override
    {
        Q_UNUSED(errorText);
        m_previous = session->getContainer(m_container->getKey());
        session->setContainer(m_container);

        PDFContentChange change;
        change.containers = { m_container->getKey() };
        setChange(std::move(change));
        return true;
    }

    virtual void revert(PDFContentEditSession* session) override { session->setContainer(m_previous); }
    virtual QString getText() const override { return QStringLiteral("Replace"); }
    virtual PDFInteger getPageIndex() const override { return -1; }
    virtual QByteArray getType() const override { return QByteArray("test_replace"); }
    virtual QJsonObject toJson() const override { return QJsonObject(); }

private:
    PDFContentContainerPointer m_container;
    PDFContentContainerPointer m_previous;
};

class DocumentToolContentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- JSON values ----
    void matrixValue();
    void matrixInvalid_data();
    void matrixInvalid();
    void rectValue();
    void rectInvalid_data();
    void rectInvalid();
    void colorValue();
    void colorInvalid_data();
    void colorInvalid();
    void nodeKindNames();
    void nodeIdText();

    // ---- script ----
    void scriptRead();
    void scriptRoundTrip();
    void scriptErrors_data();
    void scriptErrors();
    void scriptNames_data();
    void scriptNames();
    void scriptLimits();
    void nodeIdSyntax_data();
    void nodeIdSyntax();

    // ---- document writer ----
    void writerUnchanged();
    void writerDeletesNode();
    void writerForcedRewrite();
    void writerContentsMissing();
    void writerContentsArray();
    void writerContentsShared();
    void writerReadOnly();
    void writerResources_data();
    void writerResources();
    void writerNewObjects();
    void writerNewObjectsCollision();
    void writerFingerprint();
    void writerErrorsAndSkippedPages();
    void writerForm();
    void writerFormErrors();
    void writerFormData();
    void writerFormWithoutResources();
    void writerFormResourcesNull_data();
    void writerFormResourcesNull();
    void writerFormInheritedContexts();
    void writerFormPageResourcesRendered();
    void writerFormInheritedWrittenNames_data();
    void writerFormInheritedWrittenNames();
    void writerFormInheritedChain();
    void writerFormInheritedTwoCallers();
    void writerFormBehindWalkLimit();
    void writerBatchTwoPages();
    void writerStructure();
    void writerStructureSkippedPages();
    void writerStructureSkippedTaggedPage();
    void writerStructureSkippedParagraphAcrossPages();
    void writerStructureOutdated_data();
    void writerStructureOutdated();
    void writerStructureTwice();
    void writerStructureParentTreeWalk();
    void writerCancel();
    void writerProgress();
    void writerPreview();
    void writerFacts();
    void writerHostile();

    // ---- tool ----
    void toolDeclaration();
    void toolDisplayOptions();
    void toolModes();
    void toolCheckGivenParameters_data();
    void toolCheckGivenParameters();
    void toolValidate();
    void toolApplySnapshot();
    void toolDocumentChanged();
    void toolPageErrors();
    void toolNothingToDo();
    void toolCancel();
    void toolCertified();
    void toolConformance();
    void toolNotTagged();
    void toolSkippedPageForm();

    // ---- identifiers and the listing against a session (package P03) ----
    void nodeIdResolves();
    void listPage();
    void listPaging();
    void listTextAndTags();
    void listTextLimit();
    void listForms();
    void listFormSpace();
    void pageRevision();
    void toolList();
    void toolListTextLimit();
    void toolListForms();
    void toolListHostileForms();

    // ---- a script replayed by the tool (package P03) ----
    void toolApplyScript();
    void toolApplyScriptErrors();
    void toolApplyScriptNames();
    void toolApplyScriptLimits();
    void toolApplyScriptFormRevision();
    void toolApplyScriptRevisionOfLimitedWalk_data();
    void toolApplyScriptRevisionOfLimitedWalk();
    void toolApplyScriptWithoutEffect();
    void toolApplyScriptClearPageTwice();
    void toolApplyScriptGrowth();

private:
    static constexpr const char* TWO_PATHS = "1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg\n100 100 20 20 re\nf";
    static constexpr const char* ONE_PATH = "0 1 0 rg\n20 20 30 30 re\nf";
    static constexpr quint64 FIRST_NEW_NODE = PDFContentEditSession::FIRST_SESSION_NODE_ID;

    /// << /ExtGState << /GP << /Type /ExtGState /LW 6 >> >> /ProcSet [/PDF] >>
    static PDFObject resourcesObject();
    static PDFObject graphicStateObject(PDFReal lineWidth);

    /// A document of pages, each with one content stream and its own direct resourcesObject
    static PDFDocument createPagesDocument(const QList<QByteArray>& contents, PDFReal lineWidth = 6.0);

    /// The document of the forms: two pages, which both draw /Fm1; the page 1 draws also /Fm2
    ///   page 1: "/Fm1 Do /Fm2 Do", /XObject << /Fm1 /Fm2 >>, /ExtGState << /GP >>
    ///   page 2: "/Fm1 Do", /XObject << /Fm1 >>
    ///   /Fm1: Documents::addFormObject - "/GF gs 0 1 0 rg 5 5 20 10 re f", /Resources << /ExtGState << /GF << /Type /ExtGState /LW 4 >> >> >>
    ///   /Fm2: Documents::addFormWithoutResourcesObject - "/GP gs 1 0 0 rg 0 0 10 10 re f", no /Resources
    static PDFDocument createFormsDocument(PDFObjectReference* form1, PDFObjectReference* form2);

    static PDFContentContainerKey pageKey(const PDFDocument& document, PDFInteger pageIndex);
    static PDFContentContainerPointer buildContainer(const PDFDocument& document, const PDFContentContainerKey& key, PDFInteger pageIndex);

    /// A form without resources of its own built as drawn by a content with the resources
    /// \p callerResources (PDFContentBuildContext::callerResources, as the session gives them)
    static PDFContentContainerPointer buildContainer(const PDFDocument& document, const PDFContentContainerKey& key, PDFInteger pageIndex, const PDFObject& callerResources);
    static PDFContentContainerPointer buildPage(const PDFDocument& document, PDFInteger pageIndex) { return buildContainer(document, pageKey(document, pageIndex), pageIndex); }

    /// The nodes of a kind in the order of the content
    static std::vector<PDFContentNodePointer> nodesOfKind(const PDFContentContainer& container, PDFContentNodeKind kind);

    static PDFContentContainerPointer removeNode(const PDFContentContainerPointer& container, PDFContentNodeId id);
    static PDFContentContainerPointer setOverride(const PDFContentContainerPointer& container, PDFContentNodeId id, const PDFContentStateOverride& override);

    /// A new filled rectangle "x y w h re", "f" at the end of the root: a path node of the given
    /// identifier with two generated operations
    static PDFContentContainerPointer appendRectangle(const PDFContentContainerPointer& container, const QByteArray& rectangleOperands, quint64 nodeId);

    /// Adds an object to a category of the resources of a container, returns the new container
    static PDFContentContainerPointer addResource(const PDFContentContainerPointer& container, PDFContentResourceCategory category, const QByteArray& prefix, const PDFObject& object, QByteArray* name);

    static PDFContentStateOverride alphaOverride();
    static PDFContentStateOverride brokenOverride();

    static PDFContentEditSnapshot snapshotOf(const std::vector<PDFContentContainerPointer>& containers);
    static PDFContentEditSnapshotPointer snapshotPointerOf(const std::vector<PDFContentContainerPointer>& containers);

    static const PDFDictionary* pageDictionary(const PDFDocument& document, PDFInteger pageIndex);

    /// The decoded data of the stream, which /Contents of the page refers to; a text starting
    /// with "!" says, why there is none
    static QByteArray pageContent(const PDFDocument& document, PDFInteger pageIndex);

    static PDFDocumentToolContext contextOf(const PDFDocument& document);
    static QVariantMap applyParameters(const PDFContentEditSnapshotPointer& snapshot);

    /// The parameters of a run with a script
    static QVariantMap scriptParameters(const QString& script);

    /// The parameters of a listing; a negative number = the default of the tool
    static QVariantMap listParameters(const QString& pages, int depth = -1, qint64 offset = -1, int maximumObjects = -1);

    /// The revision of a page as the mode list gives it (PDFContentJson::getPageRevision of a
    /// session, which has loaded the page and the forms it draws, as the tool does) in
    /// hexadecimal digits
    static QString revisionOf(const PDFDocument& document, PDFInteger pageIndex);

    /// Loads a page and every form its content and the content of its forms draw into a session
    static bool loadPageWithForms(PDFContentEditSession* session, PDFInteger pageIndex);

    /// The tagged document of Documents::createTaggedDocument with a second page, which has no
    /// tags: the content ONE_PATH, no resources
    static PDFDocument createTaggedTwoPagesDocument();

    /// The structure model of a document with the sequence of an identifier of the page with
    /// the key 0 deleted
    static PDFContentStructureModelPointer modelWithoutSequence(const PDFDocument& document, PDFInteger mcid);

    /// The container of the tagged page without the marked content sequence of an identifier
    static PDFContentContainerPointer taggedPageWithoutSequence(const PDFDocument& document, PDFInteger mcid);

    /// The object of a listing at a path of indices through "children"
    static QJsonObject listedObject(const QJsonArray& objects, std::initializer_list<int> path);

    /// The identifiers of the objects of a listing, which are listed whole, in their order
    static QStringList listedIds(const QJsonArray& objects);

    /// The objects of a kind of a listing in their order (an object before the objects inside of it)
    static QList<QJsonObject> listedOfKind(const QJsonArray& objects, const QString& kind);

    /// A page of marked content sequences "/Span << /ActualText (...) >> BDC 0 0 5 5 re f EMC",
    /// each with a replacement text of 60 000 characters: two nodes a sequence (the sequence
    /// and its path). With \p inGroups each sequence is inside of "q ... Q": three nodes (the
    /// group, the sequence, the path).
    static QByteArray actualTextContent(int sequenceCount, bool inGroups = false);

    /// A resource dictionary with the forms by their names: << /XObject << ... >> >>
    static PDFObject formResources(Documents::DictionaryEntries forms);

    /// A page drawing /Fm1 a thousand times; /Fm1 draws /Fm2 a thousand times, /Fm2 draws /Fm3 a
    /// thousand times, /Fm3 is "0 0 1 1 re f". Four containers of a thousand nodes each list as
    /// two billion objects.
    static PDFDocument createNestedFormsDocument();

    /// A page drawing /Fm1, which draws itself: "/Fm1 Do 0 0 1 1 re f"
    static PDFDocument createSelfDrawingFormDocument();
};

PDFObject DocumentToolContentTest::graphicStateObject(PDFReal lineWidth)
{
    return Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "LW", PDFObject::createReal(lineWidth) } });
}

PDFObject DocumentToolContentTest::resourcesObject()
{
    return Documents::createDictionaryObject({ { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }) },
                                               { "ProcSet", Documents::createArrayObject({ PDFObject::createName("PDF") }) } });
}

PDFDocument DocumentToolContentTest::createPagesDocument(const QList<QByteArray>& contents, PDFReal lineWidth)
{
    PDFDocumentBuilder builder;
    for (const QByteArray& content : contents)
    {
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        PDFObject resources = Documents::createDictionaryObject({ { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(lineWidth) } }) },
                                                                  { "ProcSet", Documents::createArrayObject({ PDFObject::createName("PDF") }) } });
        builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", std::move(resources) },
                                                                  { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), content) } }));
    }
    return builder.build();
}

PDFDocument DocumentToolContentTest::createFormsDocument(PDFObjectReference* form1, PDFObjectReference* form2)
{
    PDFDocumentBuilder builder;
    const PDFObject formObject1 = Documents::addFormObject(&builder);
    const PDFObject formObject2 = Documents::addFormWithoutResourcesObject(&builder);
    *form1 = formObject1.getReference();
    *form2 = formObject2.getReference();

    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    builder.mergeTo(page1, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", formObject1 }, { "Fm2", formObject2 } }) },
                                                                                                                     { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }) } }) },
                                                               { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Fm1 Do /Fm2 Do") } }));

    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    builder.mergeTo(page2, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", formObject1 } }) } }) },
                                                               { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Fm1 Do") } }));
    return builder.build();
}

PDFContentContainerKey DocumentToolContentTest::pageKey(const PDFDocument& document, PDFInteger pageIndex)
{
    return PDFContentContainerKey::createPage(pageIndex, document.getCatalog()->getPage(size_t(pageIndex))->getPageReference());
}

PDFContentContainerPointer DocumentToolContentTest::buildContainer(const PDFDocument& document, const PDFContentContainerKey& key, PDFInteger pageIndex)
{
    return buildContainer(document, key, pageIndex, PDFObject());
}

PDFContentContainerPointer DocumentToolContentTest::buildContainer(const PDFDocument& document, const PDFContentContainerKey& key, PDFInteger pageIndex, const PDFObject& callerResources)
{
    // The caches live only for the call: a container does not need them afterwards
    PDFCMSGeneric cms;
    PDFFontCache fontCache(32, 32);
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));

    PDFContentBuildContext context;
    context.fontCache = &fontCache;
    context.cms = &cms;
    context.pageIndex = pageIndex;
    context.callerResources = callerResources;

    return PDFContentTreeBuilder::build(&document, key, nullptr, context, PDFContentBuildOptions(), nullptr, nullptr).container;
}

std::vector<PDFContentNodePointer> DocumentToolContentTest::nodesOfKind(const PDFContentContainer& container, PDFContentNodeKind kind)
{
    std::vector<PDFContentNodePointer> result;
    std::vector<PDFContentNodePointer> stack;
    stack.push_back(container.getRoot());
    while (!stack.empty())
    {
        const PDFContentNodePointer node = stack.back();
        stack.pop_back();
        if (!node)
        {
            continue;
        }

        if (node->getKind() == kind)
        {
            result.push_back(node);
        }

        const std::vector<PDFContentNodePointer>& children = node->getChildren();
        stack.insert(stack.end(), children.crbegin(), children.crend());
    }
    return result;
}

PDFContentContainerPointer DocumentToolContentTest::removeNode(const PDFContentContainerPointer& container, PDFContentNodeId id)
{
    PDFContentContainerEditor editor(container);
    return editor.removeNode(id) ? editor.build() : PDFContentContainerPointer();
}

PDFContentContainerPointer DocumentToolContentTest::setOverride(const PDFContentContainerPointer& container, PDFContentNodeId id, const PDFContentStateOverride& override)
{
    PDFContentContainerEditor editor(container);
    const PDFContentNodePointer node = editor.getNode(id);
    if (!node)
    {
        return PDFContentContainerPointer();
    }

    PDFContentNode::Data data = node->getData();
    data.override = override;
    return editor.setNodeData(id, std::move(data)) ? editor.build() : PDFContentContainerPointer();
}

PDFContentContainerPointer DocumentToolContentTest::appendRectangle(const PDFContentContainerPointer& container, const QByteArray& rectangleOperands, quint64 nodeId)
{
    PDFContentContainerEditor editor(container);

    PDFContentNode::Data data;
    data.kind = PDFContentNodeKind::Path;
    data.id = PDFContentNodeId{ nodeId };
    data.prefix.first = editor.getOperations().append(PDFContentOperation::createGenerated(PDFContentOperator::Rectangle, rectangleOperands + " re", PDFContentOperation::Operands(), { }));
    editor.getOperations().append(PDFContentOperation::createGenerated(PDFContentOperator::PathFillWinding, "f", PDFContentOperation::Operands(), { }));
    data.prefix.count = 2;
    data.paint.fill = true;

    const PDFContentNodeId rootId = container->getRoot()->getId();
    return editor.insertNode(std::move(data), rootId, container->getAppendIndex(rootId)) ? editor.build() : PDFContentContainerPointer();
}

PDFContentContainerPointer DocumentToolContentTest::addResource(const PDFContentContainerPointer& container, PDFContentResourceCategory category, const QByteArray& prefix, const PDFObject& object, QByteArray* name)
{
    PDFContentContainerEditor editor(container);
    *name = editor.getResources().addObject(category, prefix, object);
    return editor.build();
}

PDFContentStateOverride DocumentToolContentTest::alphaOverride()
{
    PDFContentStateValue value;
    value.kind = PDFContentStateValue::Kind::Real;
    value.real = 0.5;

    PDFContentStateOverride override;
    override.properties[PDFContentStateProperty::AlphaFilling] = value;
    return override;
}

PDFContentStateOverride DocumentToolContentTest::brokenOverride()
{
    // A singular matrix cannot be written (zadani_overeni.md 6.2: an error of the node)
    PDFContentStateOverride override;
    override.transform = QTransform(0, 0, 0, 0, 0, 0);
    return override;
}

PDFContentEditSnapshot DocumentToolContentTest::snapshotOf(const std::vector<PDFContentContainerPointer>& containers)
{
    PDFContentEditSnapshot snapshot;
    snapshot.revision = 1;
    for (const PDFContentContainerPointer& container : containers)
    {
        snapshot.containers[container->getKey()] = container;
    }
    return snapshot;
}

PDFContentEditSnapshotPointer DocumentToolContentTest::snapshotPointerOf(const std::vector<PDFContentContainerPointer>& containers)
{
    return std::make_shared<const PDFContentEditSnapshot>(snapshotOf(containers));
}

const PDFDictionary* DocumentToolContentTest::pageDictionary(const PDFDocument& document, PDFInteger pageIndex)
{
    return document.getDictionaryFromObject(document.getObjectByReference(document.getCatalog()->getPage(size_t(pageIndex))->getPageReference()));
}

QByteArray DocumentToolContentTest::pageContent(const PDFDocument& document, PDFInteger pageIndex)
{
    const PDFDictionary* page = pageDictionary(document, pageIndex);
    if (!page || !page->get("Contents").isReference())
    {
        return QByteArray("!/Contents is not a reference");
    }

    const PDFObject& stream = document.getObject(page->get("Contents"));
    if (!stream.isStream())
    {
        return QByteArray("!/Contents is not a stream");
    }

    return document.getDecodedStream(stream.getStream());
}

PDFDocumentToolContext DocumentToolContentTest::contextOf(const PDFDocument& document)
{
    return PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("C:/files/Report.pdf"), true));
}

QVariantMap DocumentToolContentTest::applyParameters(const PDFContentEditSnapshotPointer& snapshot)
{
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_MODE), QString::fromLatin1(Tool::MODE_APPLY));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_SESSION), QVariant::fromValue(snapshot));
    return parameters;
}

// ---------------------------------------------------------------------------------------------
//  JSON values (zadani_overeni.md 7.1: a matrix is six numbers, a rectangle { left, bottom, right,
//  top }, a color { space, values } or "#rrggbb")
// ---------------------------------------------------------------------------------------------

void DocumentToolContentTest::matrixValue()
{
    // ISO 32000-2, 8.3.3: the matrix [a b c d e f]; QTransform(m11, m12, m21, m22, dx, dy) has the
    // same order
    const QTransform matrix(1.5, 2.0, -3.0, 4.25, 50.0, -60.5);
    const QJsonArray written = PDFContentJson::writeMatrix(matrix);
    QCOMPARE(written, QJsonArray({ 1.5, 2.0, -3.0, 4.25, 50.0, -60.5 }));

    const std::optional<QTransform> read = PDFContentJson::readMatrix(QJsonArray({ 1.5, 2.0, -3.0, 4.25, 50.0, -60.5 }));
    QVERIFY(read.has_value());
    QCOMPARE(read->m11(), 1.5);
    QCOMPARE(read->m12(), 2.0);
    QCOMPARE(read->m21(), -3.0);
    QCOMPARE(read->m22(), 4.25);
    QCOMPARE(read->dx(), 50.0);
    QCOMPARE(read->dy(), -60.5);

    // The largest real number of a PDF file (Annex C.2) is still a value
    const double largest = double(std::numeric_limits<float>::max());
    QVERIFY(PDFContentJson::readMatrix(QJsonArray({ largest, 0, 0, -largest, 0, 0 })).has_value());
}

void DocumentToolContentTest::matrixInvalid_data()
{
    QTest::addColumn<QJsonValue>("value");

    QTest::newRow("five numbers") << QJsonValue(QJsonArray({ 1, 0, 0, 1, 0 }));
    QTest::newRow("seven numbers") << QJsonValue(QJsonArray({ 1, 0, 0, 1, 0, 0, 0 }));
    QTest::newRow("a text among the numbers") << QJsonValue(QJsonArray({ 1, 0, 0, 1, QStringLiteral("5"), 0 }));
    QTest::newRow("null among the numbers") << QJsonValue(QJsonArray({ 1, 0, 0, 1, QJsonValue(), 0 }));
    QTest::newRow("an object") << QJsonValue(QJsonObject({ { QStringLiteral("a"), 1 } }));
    QTest::newRow("a number") << QJsonValue(1.0);
    QTest::newRow("no value") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("infinity") << QJsonValue(QJsonArray({ 1, 0, 0, 1, std::numeric_limits<double>::infinity(), 0 }));
    QTest::newRow("not a number") << QJsonValue(QJsonArray({ std::numeric_limits<double>::quiet_NaN(), 0, 0, 1, 0, 0 }));

    // Above the largest real number of a PDF file (ISO 32000-2, Annex C.2: about 3.403e38)
    QTest::newRow("above the largest real") << QJsonValue(QJsonArray({ 1, 0, 0, 1, 0, 1e39 }));
    QTest::newRow("below the smallest real") << QJsonValue(QJsonArray({ 1, 0, 0, 1, -1e300, 0 }));
}

void DocumentToolContentTest::matrixInvalid()
{
    QFETCH(QJsonValue, value);
    QVERIFY(!PDFContentJson::readMatrix(value).has_value());
}

void DocumentToolContentTest::rectValue()
{
    // The rectangle x 10 ... 40, y 20 ... 60 of the user space, whose y axis points up
    const QJsonObject written = PDFContentJson::writeRect(QRectF(10, 20, 30, 40));
    QCOMPARE(written.size(), 4);
    QCOMPARE(written.value(QStringLiteral("left")).toDouble(), 10.0);
    QCOMPARE(written.value(QStringLiteral("bottom")).toDouble(), 20.0);
    QCOMPARE(written.value(QStringLiteral("right")).toDouble(), 40.0);
    QCOMPARE(written.value(QStringLiteral("top")).toDouble(), 60.0);

    // A rectangle with a negative size names the same sides
    QCOMPARE(PDFContentJson::writeRect(QRectF(40, 60, -30, -40)), written);

    const std::optional<QRectF> read = PDFContentJson::readRect(written);
    QVERIFY(read.has_value());
    QCOMPARE(*read, QRectF(10, 20, 30, 40));

    // An empty rectangle (a point) is a rectangle
    const std::optional<QRectF> point = PDFContentJson::readRect(QJsonObject({ { QStringLiteral("left"), 5 }, { QStringLiteral("bottom"), 6 }, { QStringLiteral("right"), 5 }, { QStringLiteral("top"), 6 } }));
    QVERIFY(point.has_value());
    QCOMPARE(*point, QRectF(5, 6, 0, 0));
}

void DocumentToolContentTest::rectInvalid_data()
{
    QTest::addColumn<QJsonValue>("value");

    const auto rect = [](QJsonValue left, QJsonValue bottom, QJsonValue right, QJsonValue top)
    {
        return QJsonObject({ { QStringLiteral("left"), left }, { QStringLiteral("bottom"), bottom }, { QStringLiteral("right"), right }, { QStringLiteral("top"), top } });
    };

    QJsonObject missing = rect(1, 2, 3, 4);
    missing.remove(QStringLiteral("top"));
    QJsonObject additional = rect(1, 2, 3, 4);
    additional.insert(QStringLiteral("width"), 2);

    QTest::newRow("a side is missing") << QJsonValue(missing);
    QTest::newRow("a key more") << QJsonValue(additional);
    QTest::newRow("a text as a side") << QJsonValue(rect(1, QStringLiteral("2"), 3, 4));
    QTest::newRow("left is right of right") << QJsonValue(rect(3, 2, 1, 4));
    QTest::newRow("bottom is above top") << QJsonValue(rect(1, 4, 3, 2));
    QTest::newRow("infinity") << QJsonValue(rect(1, 2, std::numeric_limits<double>::infinity(), 4));
    QTest::newRow("above the largest real") << QJsonValue(rect(1, 2, 3, 1e39));
    QTest::newRow("an array") << QJsonValue(QJsonArray({ 1, 2, 3, 4 }));
    QTest::newRow("no value") << QJsonValue(QJsonValue::Undefined);
}

void DocumentToolContentTest::rectInvalid()
{
    QFETCH(QJsonValue, value);
    QVERIFY(!PDFContentJson::readRect(value).has_value());
}

void DocumentToolContentTest::colorValue()
{
    QString errorText = QStringLiteral("x");

    // "#rrggbb" is DeviceRGB: 0xff = 1, 0x80 = 128 / 255, 0x00 = 0
    std::optional<PDFContentColorValue> color = PDFContentJson::readColor(QJsonValue(QStringLiteral("#ff8000")), &errorText);
    QVERIFY(color.has_value());
    QVERIFY(errorText.isEmpty());
    QCOMPARE(color->deviceSpace, QByteArray("DeviceRGB"));
    QCOMPARE(color->components.size(), size_t(3));
    QCOMPARE(color->components[0], 1.0);
    QCOMPARE(color->components[1], 128.0 / 255.0);
    QCOMPARE(color->components[2], 0.0);
    QVERIFY(!color->isPattern());

    color = PDFContentJson::readColor(QJsonValue(QStringLiteral("#0A0b0C")), &errorText);
    QVERIFY(color.has_value());
    QCOMPARE(color->components[0], 10.0 / 255.0);
    QCOMPARE(color->components[1], 11.0 / 255.0);
    QCOMPARE(color->components[2], 12.0 / 255.0);

    // ISO 32000-2, 8.6.4.2 - 8.6.4.4: DeviceGray has one component, DeviceRGB three, DeviceCMYK four
    const auto object = [](const QString& space, const QJsonArray& values)
    {
        return QJsonValue(QJsonObject({ { QStringLiteral("space"), space }, { QStringLiteral("values"), values } }));
    };

    color = PDFContentJson::readColor(object(QStringLiteral("DeviceGray"), { 0.25 }), &errorText);
    QVERIFY(color.has_value());
    QCOMPARE(color->deviceSpace, QByteArray("DeviceGray"));
    QCOMPARE(color->components, std::vector<PDFReal>({ 0.25 }));

    color = PDFContentJson::readColor(object(QStringLiteral("DeviceRGB"), { 0, 0.5, 1 }), &errorText);
    QVERIFY(color.has_value());
    QCOMPARE(color->deviceSpace, QByteArray("DeviceRGB"));
    QCOMPARE(color->components, std::vector<PDFReal>({ 0.0, 0.5, 1.0 }));

    color = PDFContentJson::readColor(object(QStringLiteral("DeviceCMYK"), { 0.1, 0.2, 0.3, 0.4 }), &errorText);
    QVERIFY(color.has_value());
    QCOMPARE(color->deviceSpace, QByteArray("DeviceCMYK"));
    QCOMPARE(color->components, std::vector<PDFReal>({ 0.1, 0.2, 0.3, 0.4 }));

    // Writing: the space and the values as the content has them
    QCOMPARE(PDFContentJson::writeColor(*color), object(QStringLiteral("DeviceCMYK"), { 0.1, 0.2, 0.3, 0.4 }));

    // A color of another space names the family of its space; a pattern says so
    PDFContentColorValue iccColor;
    iccColor.colorSpaceObject = Documents::createArrayObject({ PDFObject::createName("ICCBased"), PDFObject::createReference(PDFObjectReference(7, 0)) });
    iccColor.components = { 0.5, 0.5, 0.5 };
    QCOMPARE(PDFContentJson::writeColor(iccColor), object(QStringLiteral("ICCBased"), { 0.5, 0.5, 0.5 }));

    PDFContentColorValue patternColor;
    patternColor.colorSpaceObject = PDFObject::createName("Pattern");
    patternColor.patternObject = PDFObject::createReference(PDFObjectReference(9, 0));
    QJsonObject expectedPattern = object(QStringLiteral("Pattern"), { }).toObject();
    expectedPattern.insert(QStringLiteral("pattern"), true);
    QCOMPARE(PDFContentJson::writeColor(patternColor), QJsonValue(expectedPattern));
}

void DocumentToolContentTest::colorInvalid_data()
{
    QTest::addColumn<QJsonValue>("value");

    const auto object = [](const QJsonValue& space, const QJsonValue& values)
    {
        return QJsonValue(QJsonObject({ { QStringLiteral("space"), space }, { QStringLiteral("values"), values } }));
    };

    QTest::newRow("five digits") << QJsonValue(QStringLiteral("#ff800"));
    QTest::newRow("seven digits") << QJsonValue(QStringLiteral("#ff80000"));
    QTest::newRow("no number sign") << QJsonValue(QStringLiteral("ff8000a"));
    QTest::newRow("not a hexadecimal digit") << QJsonValue(QStringLiteral("#gg8000"));
    QTest::newRow("a sign in a component") << QJsonValue(QStringLiteral("#+f8000"));
    QTest::newRow("a space in a component") << QJsonValue(QStringLiteral("#ff 800"));
    QTest::newRow("a name of a color") << QJsonValue(QStringLiteral("red"));
    QTest::newRow("a number") << QJsonValue(255);
    QTest::newRow("no value") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("two values of DeviceRGB") << object(QStringLiteral("DeviceRGB"), QJsonArray({ 0, 0 }));
    QTest::newRow("three values of DeviceGray") << object(QStringLiteral("DeviceGray"), QJsonArray({ 0, 0, 0 }));
    QTest::newRow("three values of DeviceCMYK") << object(QStringLiteral("DeviceCMYK"), QJsonArray({ 0, 0, 0 }));
    QTest::newRow("a value above 1") << object(QStringLiteral("DeviceGray"), QJsonArray({ 1.5 }));
    QTest::newRow("a value below 0") << object(QStringLiteral("DeviceRGB"), QJsonArray({ 0, -0.1, 0 }));
    QTest::newRow("a text as a value") << object(QStringLiteral("DeviceGray"), QJsonArray({ QStringLiteral("0.5") }));
    QTest::newRow("a space, which is not a device space") << object(QStringLiteral("ICCBased"), QJsonArray({ 0, 0, 0 }));
    QTest::newRow("a space in lower case") << object(QStringLiteral("devicergb"), QJsonArray({ 0, 0, 0 }));
    QTest::newRow("the space is a number") << object(3, QJsonArray({ 0, 0, 0 }));
    QTest::newRow("the values are a number") << object(QStringLiteral("DeviceGray"), 0.5);
    QTest::newRow("no values") << QJsonValue(QJsonObject({ { QStringLiteral("space"), QStringLiteral("DeviceGray") } }));
    QTest::newRow("a key more") << QJsonValue(QJsonObject({ { QStringLiteral("space"), QStringLiteral("DeviceGray") }, { QStringLiteral("values"), QJsonArray({ 0.5 }) }, { QStringLiteral("alpha"), 1 } }));
}

void DocumentToolContentTest::colorInvalid()
{
    QFETCH(QJsonValue, value);

    QString errorText;
    QVERIFY(!PDFContentJson::readColor(value, &errorText).has_value());
    QVERIFY(!errorText.isEmpty());

    // The reason is optional
    QVERIFY(!PDFContentJson::readColor(value, nullptr).has_value());
}

void DocumentToolContentTest::nodeKindNames()
{
    // The names of the header: "text", "path", "image", "form", "group"
    QCOMPARE(PDFContentJson::writeNodeKind(PDFContentNodeKind::TextObject), QStringLiteral("text"));
    QCOMPARE(PDFContentJson::writeNodeKind(PDFContentNodeKind::Path), QStringLiteral("path"));
    QCOMPARE(PDFContentJson::writeNodeKind(PDFContentNodeKind::Image), QStringLiteral("image"));
    QCOMPARE(PDFContentJson::writeNodeKind(PDFContentNodeKind::FormUse), QStringLiteral("form"));
    QCOMPARE(PDFContentJson::writeNodeKind(PDFContentNodeKind::Group), QStringLiteral("group"));

    // Every kind has its own name of lower case letters and underscores
    QSet<QString> names;
    for (const PDFContentNodeKind kind : { PDFContentNodeKind::Root, PDFContentNodeKind::Group, PDFContentNodeKind::MarkedContent, PDFContentNodeKind::MarkedPoint,
                                           PDFContentNodeKind::TextObject, PDFContentNodeKind::TextShow, PDFContentNodeKind::Path, PDFContentNodeKind::Image,
                                           PDFContentNodeKind::FormUse, PDFContentNodeKind::Shading, PDFContentNodeKind::StateRun, PDFContentNodeKind::Opaque })
    {
        const QString name = PDFContentJson::writeNodeKind(kind);
        QVERIFY(!name.isEmpty());
        for (const QChar character : name)
        {
            QVERIFY2((character >= QLatin1Char('a') && character <= QLatin1Char('z')) || character == QLatin1Char('_'), qPrintable(name));
        }
        names.insert(name);
    }
    QCOMPARE(names.size(), 12);
}

void DocumentToolContentTest::nodeIdText()
{
    // zadani_overeni.md 7.1: "p3:57" is the node 57 of the page 3 (numbers from 1; the index 2),
    // "p3:57/12" the node 12 of the form, which the node 57 draws
    QCOMPARE(PDFContentJson::writeNodeId(2, { PDFContentNodeId{ 57 } }), QStringLiteral("p3:57"));
    QCOMPARE(PDFContentJson::writeNodeId(2, { PDFContentNodeId{ 57 }, PDFContentNodeId{ 12 } }), QStringLiteral("p3:57/12"));
    QCOMPARE(PDFContentJson::writeNodeId(0, { PDFContentNodeId{ 1 }, PDFContentNodeId{ 2 }, PDFContentNodeId{ 3 } }), QStringLiteral("p1:1/2/3"));

    // A node of a command has an identifier from 2^32
    QCOMPARE(PDFContentJson::writeNodeId(0, { PDFContentNodeId{ quint64(1) << 32 } }), QStringLiteral("p1:4294967296"));
    QCOMPARE(PDFContentJson::writeNodeId(0, { PDFContentNodeId{ std::numeric_limits<quint64>::max() } }), QStringLiteral("p1:18446744073709551615"));

    // Nothing to name
    QCOMPARE(PDFContentJson::writeNodeId(0, { }), QString());
    QCOMPARE(PDFContentJson::writeNodeId(-1, { PDFContentNodeId{ 5 } }), QString());
    QCOMPARE(PDFContentJson::writeNodeId(0, { PDFContentNodeId{ 5 }, PDFContentNodeId() }), QString());
}

// ---------------------------------------------------------------------------------------------
//  The script (zadani_jadro.md 4.6, zadani_overeni.md 6.2, 7.1)
// ---------------------------------------------------------------------------------------------

void DocumentToolContentTest::scriptRead()
{
    // The revision of a page is its fingerprint in hexadecimal digits (32 bytes of SHA-256; here
    // two short ones)
    const QString text = QStringLiteral("{ \"revisions\": { \"3\": \"00ff10\", \"12\": \"ABcd\" },"
                                        "  \"items\": [ { \"type\": \"delete\", \"ids\": [ \"p3:57\" ] },"
                                        "               { \"type\": \"copy\", \"ids\": [ \"p3:5\" ], \"parent\": \"p12:1\", \"index\": -1, \"as\": \"t1\" },"
                                        "               { \"type\": \"transform\", \"ids\": [ \"t1\" ], \"matrix\": [ 1, 0, 0, 1, 10, 20 ] } ] }");

    std::vector<QString> errors;
    const std::optional<PDFContentScript> script = PDFContentJson::readScript(text, &errors);
    QVERIFY2(script.has_value(), qPrintable(errors.empty() ? QString() : errors.front()));
    QVERIFY(errors.empty());

    QCOMPARE(script->revisions.size(), size_t(2));
    QCOMPARE(script->revisions.at(3), QByteArray::fromHex("00ff10"));
    QCOMPARE(script->revisions.at(12), QByteArray::fromHex("abcd"));

    QCOMPARE(script->items.size(), size_t(3));
    QCOMPARE(script->items[0].value(QStringLiteral("type")).toString(), QStringLiteral("delete"));
    QCOMPARE(script->items[1].value(QStringLiteral("as")).toString(), QStringLiteral("t1"));
    QCOMPARE(script->items[1].value(QStringLiteral("index")).toInt(), -1);
    QCOMPARE(script->items[2].value(QStringLiteral("matrix")).toArray(), QJsonArray({ 1, 0, 0, 1, 10, 20 }));

    // The revisions are optional (a script, which names no existing node, needs none); the
    // errors are optional too
    const std::optional<PDFContentScript> withoutRevisions = PDFContentJson::readScript(QStringLiteral("{\"items\":[{\"type\":\"clear_page\",\"page\":1}]}"), nullptr);
    QVERIFY(withoutRevisions.has_value());
    QVERIFY(withoutRevisions->revisions.empty());
    QCOMPARE(withoutRevisions->items.size(), size_t(1));

    // 500 operations are the most of one call
    QJsonArray items;
    for (int i = 0; i < 500; ++i)
    {
        items.append(QJsonObject({ { QStringLiteral("type"), QStringLiteral("delete") } }));
    }
    const QString largest = QString::fromUtf8(QJsonDocument(QJsonObject({ { QStringLiteral("items"), items } })).toJson(QJsonDocument::Compact));
    const std::optional<PDFContentScript> largestScript = PDFContentJson::readScript(largest, &errors);
    QVERIFY(largestScript.has_value());
    QCOMPARE(largestScript->items.size(), size_t(500));
}

void DocumentToolContentTest::scriptRoundTrip()
{
    PDFContentScript script;
    script.revisions[1] = QByteArray::fromHex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    script.revisions[20] = QByteArray::fromHex("ff");
    script.items.push_back(QJsonObject({ { QStringLiteral("type"), QStringLiteral("delete") }, { QStringLiteral("ids"), QJsonArray({ QStringLiteral("p1:5") }) } }));
    script.items.push_back(QJsonObject({ { QStringLiteral("type"), QStringLiteral("clear_page") }, { QStringLiteral("page"), 20 } }));

    const QString text = PDFContentJson::writeScript(script);

    // The keys of a script and the page numbers as the keys of the revisions
    const QJsonObject object = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(object.keys(), QStringList({ QStringLiteral("items"), QStringLiteral("revisions") }));
    QCOMPARE(object.value(QStringLiteral("revisions")).toObject().value(QStringLiteral("1")).toString(), QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    QCOMPARE(object.value(QStringLiteral("revisions")).toObject().value(QStringLiteral("20")).toString(), QStringLiteral("ff"));
    QCOMPARE(object.value(QStringLiteral("items")).toArray().size(), 2);

    std::vector<QString> errors;
    const std::optional<PDFContentScript> read = PDFContentJson::readScript(text, &errors);
    QVERIFY(read.has_value());
    QVERIFY(read->revisions == script.revisions);
    QVERIFY(read->items == script.items);
}

void DocumentToolContentTest::scriptErrors_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("path");      // The beginning of the first error: the place of the mistake

    const QString item = QStringLiteral("{\"type\":\"delete\"}");

    QTest::newRow("not JSON") << QStringLiteral("{ items: [ ] }") << QStringLiteral("operations: ");
    QTest::newRow("empty text") << QString() << QStringLiteral("operations: ");
    QTest::newRow("an array") << QStringLiteral("[ %1 ]").arg(item) << QStringLiteral("operations: ");
    QTest::newRow("an unknown key") << QStringLiteral("{ \"items\": [ %1 ], \"mode\": \"x\" }").arg(item) << QStringLiteral("operations: ");
    QTest::newRow("no items") << QStringLiteral("{ \"revisions\": { } }") << QStringLiteral("operations.items: ");
    QTest::newRow("items are an object") << QStringLiteral("{ \"items\": { } }") << QStringLiteral("operations.items: ");
    QTest::newRow("no operation") << QStringLiteral("{ \"items\": [ ] }") << QStringLiteral("operations.items: ");
    QTest::newRow("an operation is a text") << QStringLiteral("{ \"items\": [ %1, \"delete\" ] }").arg(item) << QStringLiteral("operations.items[1]: ");
    QTest::newRow("an operation is an array") << QStringLiteral("{ \"items\": [ %1, %1, %1, [ ] ] }").arg(item) << QStringLiteral("operations.items[3]: ");
    QTest::newRow("no type") << QStringLiteral("{ \"items\": [ %1, { \"ids\": [ ] } ] }").arg(item) << QStringLiteral("operations.items[1].type: ");
    QTest::newRow("the type is a number") << QStringLiteral("{ \"items\": [ { \"type\": 5 } ] }") << QStringLiteral("operations.items[0].type: ");
    QTest::newRow("the type is empty") << QStringLiteral("{ \"items\": [ { \"type\": \"\" } ] }") << QStringLiteral("operations.items[0].type: ");
    QTest::newRow("revisions are an array") << QStringLiteral("{ \"revisions\": [ ], \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions: ");
    QTest::newRow("revisions are a text") << QStringLiteral("{ \"revisions\": \"x\", \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions: ");
    QTest::newRow("the revision of the page 0") << QStringLiteral("{ \"revisions\": { \"0\": \"ab\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.0: ");
    QTest::newRow("a page number with a zero in front") << QStringLiteral("{ \"revisions\": { \"03\": \"ab\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.03: ");
    QTest::newRow("a negative page number") << QStringLiteral("{ \"revisions\": { \"-1\": \"ab\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.-1: ");
    QTest::newRow("a page number above a billion") << QStringLiteral("{ \"revisions\": { \"1000000001\": \"ab\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.1000000001: ");
    QTest::newRow("a page, which is no number") << QStringLiteral("{ \"revisions\": { \"p3\": \"ab\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.p3: ");
    QTest::newRow("a revision, which is a number") << QStringLiteral("{ \"revisions\": { \"3\": 171 }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.3: ");
    QTest::newRow("an empty revision") << QStringLiteral("{ \"revisions\": { \"3\": \"\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.3: ");
    QTest::newRow("a revision of three digits") << QStringLiteral("{ \"revisions\": { \"3\": \"abc\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.3: ");
    QTest::newRow("a revision, which is not hexadecimal") << QStringLiteral("{ \"revisions\": { \"3\": \"abxy\" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.3: ");
    QTest::newRow("a revision with a space") << QStringLiteral("{ \"revisions\": { \"3\": \"ab cd \" }, \"items\": [ %1 ] }").arg(item) << QStringLiteral("operations.revisions.3: ");
    QTest::newRow("a revision of 65 bytes") << QStringLiteral("{ \"revisions\": { \"3\": \"%1\" }, \"items\": [ %2 ] }").arg(QString(130, QLatin1Char('a')), item) << QStringLiteral("operations.revisions.3: ");

    // 501 operations (zadani_overeni.md 6.2: 500 operations a call)
    QStringList items;
    for (int i = 0; i < 501; ++i)
    {
        items << item;
    }
    QTest::newRow("501 operations") << QStringLiteral("{ \"items\": [ %1 ] }").arg(items.join(QLatin1Char(','))) << QStringLiteral("operations.items: ");

    // 501 revisions
    QStringList revisions;
    for (int i = 1; i <= 501; ++i)
    {
        revisions << QStringLiteral("\"%1\": \"ab\"").arg(i);
    }
    QTest::newRow("501 revisions") << QStringLiteral("{ \"revisions\": { %1 }, \"items\": [ %2 ] }").arg(revisions.join(QLatin1Char(',')), item) << QStringLiteral("operations.revisions: ");

    // The values of an operation nested 33 levels deep: the object of the operation is the level
    // 1, then 32 arrays inside of each other
    QTest::newRow("values nested 33 levels") << QStringLiteral("{ \"items\": [ %1, { \"type\": \"delete\", \"ids\": %2%3 } ] }").arg(item, QString(32, QLatin1Char('[')), QString(32, QLatin1Char(']')))
                                             << QStringLiteral("operations.items[1]: ");
}

void DocumentToolContentTest::scriptErrors()
{
    QFETCH(QString, text);
    QFETCH(QString, path);

    std::vector<QString> errors;
    QVERIFY(!PDFContentJson::readScript(text, &errors).has_value());
    QVERIFY(!errors.empty());
    QVERIFY2(errors.front().startsWith(path), qPrintable(errors.front()));
    QVERIFY(errors.front().size() > path.size());

    // The errors are added to those of the caller and are optional
    std::vector<QString> more = { QStringLiteral("before") };
    QVERIFY(!PDFContentJson::readScript(text, &more).has_value());
    QCOMPARE(more.front(), QStringLiteral("before"));
    QCOMPARE(more.size(), errors.size() + 1);
    QVERIFY(!PDFContentJson::readScript(text, nullptr).has_value());
}

void DocumentToolContentTest::scriptNames_data()
{
    QTest::addColumn<QString>("name");      // The JSON value of "as"
    QTest::addColumn<bool>("isValid");

    // A name of a new node for the later operations of the script
    QTest::newRow("t1") << QStringLiteral("\"t1\"") << true;
    QTest::newRow("a name with a dot and a dash") << QStringLiteral("\"logo.copy-2\"") << true;
    QTest::newRow("a name of 64 characters") << QStringLiteral("\"%1\"").arg(QString(64, QLatin1Char('n'))) << true;

    // A name, which only begins like an identifier, is a name: p without digits, digits without
    // a colon, a colon without digits, another first letter
    QTest::newRow("p") << QStringLiteral("\"p\"") << true;
    QTest::newRow("p3") << QStringLiteral("\"p3\"") << true;
    QTest::newRow("page:1") << QStringLiteral("\"page:1\"") << true;
    QTest::newRow("p:5") << QStringLiteral("\"p:5\"") << true;
    QTest::newRow("q3:57") << QStringLiteral("\"q3:57\"") << true;
    QTest::newRow("P3:57") << QStringLiteral("\"P3:57\"") << true;

    // Decision 17 of the notes of the skeleton: a name never has the form of an identifier
    // ("p", digits, a colon), whatever follows - the reader of identifiers never has to choose
    QTest::newRow("p3:57") << QStringLiteral("\"p3:57\"") << false;
    QTest::newRow("p3:57/12") << QStringLiteral("\"p3:57/12\"") << false;
    QTest::newRow("p1:x") << QStringLiteral("\"p1:x\"") << false;
    QTest::newRow("p0:1") << QStringLiteral("\"p0:1\"") << false;
    QTest::newRow("p12:") << QStringLiteral("\"p12:\"") << false;
    QTest::newRow("p007:name") << QStringLiteral("\"p007:name\"") << false;

    QTest::newRow("an empty name") << QStringLiteral("\"\"") << false;
    QTest::newRow("a name of 65 characters") << QStringLiteral("\"%1\"").arg(QString(65, QLatin1Char('n'))) << false;
    QTest::newRow("a name with a space") << QStringLiteral("\"new logo\"") << false;
    QTest::newRow("a name with a line feed") << QStringLiteral("\"a\\nb\"") << false;
    QTest::newRow("a name with a format character") << QStringLiteral("\"a\\u202eb\"") << false;
    QTest::newRow("a number") << QStringLiteral("5") << false;
    QTest::newRow("null") << QStringLiteral("null") << false;
    QTest::newRow("an array") << QStringLiteral("[\"t1\"]") << false;
}

void DocumentToolContentTest::scriptNames()
{
    QFETCH(QString, name);
    QFETCH(bool, isValid);

    const QString text = QStringLiteral("{ \"items\": [ { \"type\": \"delete\" }, { \"type\": \"copy\", \"as\": %1 } ] }").arg(name);

    std::vector<QString> errors;
    const std::optional<PDFContentScript> script = PDFContentJson::readScript(text, &errors);
    QCOMPARE(script.has_value(), isValid);
    if (isValid)
    {
        QVERIFY(errors.empty());
    }
    else
    {
        QCOMPARE(errors.size(), size_t(1));
        QVERIFY2(errors.front().startsWith(QStringLiteral("operations.items[1].as: ")), qPrintable(errors.front()));
    }
}

void DocumentToolContentTest::scriptLimits()
{
    // ---- A script of 4 MiB is read, one character more is refused before it is parsed ----
    const int maximumLength = 4 * 1024 * 1024;
    const QString begin = QStringLiteral("{\"items\":[{\"type\":\"delete\",\"note\":\"");
    const QString end = QStringLiteral("\"}]}");

    const QString longest = begin + QString(maximumLength - begin.size() - end.size(), QLatin1Char('x')) + end;
    QCOMPARE(longest.size(), maximumLength);

    std::vector<QString> errors;
    const std::optional<PDFContentScript> longestScript = PDFContentJson::readScript(longest, &errors);
    QVERIFY(longestScript.has_value());
    QCOMPARE(longestScript->items.size(), size_t(1));
    QCOMPARE(longestScript->items[0].value(QStringLiteral("note")).toString().size(), maximumLength - begin.size() - end.size());

    const QString tooLong = begin + QString(maximumLength + 1 - begin.size() - end.size(), QLatin1Char('x')) + end;
    QCOMPARE(tooLong.size(), maximumLength + 1);
    QVERIFY(!PDFContentJson::readScript(tooLong, &errors).has_value());
    QCOMPARE(errors.size(), size_t(1));
    QVERIFY2(errors.front().startsWith(QStringLiteral("operations: ")), qPrintable(errors.front()));
    QVERIFY2(errors.front().contains(QStringLiteral("4194304")), qPrintable(errors.front()));

    // ---- Deep JSON: 100 000 arrays inside of each other are refused, nothing crashes ----
    errors.clear();
    const QString deep = QStringLiteral("{\"items\":[{\"type\":\"delete\",\"ids\":") + QString(100000, QLatin1Char('[')) + QString(100000, QLatin1Char(']')) + QStringLiteral("}]}");
    QVERIFY(!PDFContentJson::readScript(deep, &errors).has_value());
    QCOMPARE(errors.size(), size_t(1));

    // 31 arrays inside of the object of an operation are 32 levels: still read
    errors.clear();
    const QString allowed = QStringLiteral("{\"items\":[{\"type\":\"delete\",\"ids\":") + QString(31, QLatin1Char('[')) + QString(31, QLatin1Char(']')) + QStringLiteral("}]}");
    QVERIFY(PDFContentJson::readScript(allowed, &errors).has_value());

    // ---- Many wrong operations: the errors are bounded (100 of 500) ----
    errors.clear();
    QStringList wrongItems;
    for (int i = 0; i < 500; ++i)
    {
        wrongItems << QStringLiteral("5");
    }
    QVERIFY(!PDFContentJson::readScript(QStringLiteral("{\"items\":[%1]}").arg(wrongItems.join(QLatin1Char(','))), &errors).has_value());
    QCOMPARE(errors.size(), size_t(100));
    QVERIFY(errors.back().startsWith(QStringLiteral("operations.items[99]: ")));

    // ---- A huge number: the script is refused, or the number is no value of a matrix ----
    errors.clear();
    const std::optional<PDFContentScript> huge = PDFContentJson::readScript(QStringLiteral("{\"items\":[{\"type\":\"transform\",\"matrix\":[1,0,0,1,1e999,0]}]}"), &errors);
    QVERIFY(!huge.has_value() || !PDFContentJson::readMatrix(huge->items[0].value(QStringLiteral("matrix"))).has_value());

    const std::optional<PDFContentScript> large = PDFContentJson::readScript(QStringLiteral("{\"items\":[{\"type\":\"transform\",\"matrix\":[1,0,0,1,1e300,0]}]}"), &errors);
    QVERIFY(large.has_value());
    QVERIFY(!PDFContentJson::readMatrix(large->items[0].value(QStringLiteral("matrix"))).has_value());
}

void DocumentToolContentTest::nodeIdSyntax_data()
{
    QTest::addColumn<QString>("text");

    // The document of the test has two pages. Every text here has the form of an identifier
    // ("p", digits, a colon), so it is never looked up as a name.
    QTest::newRow("the page 0") << QStringLiteral("p0:5");
    QTest::newRow("the page 3 of 2") << QStringLiteral("p3:5");
    QTest::newRow("a page number with a zero in front") << QStringLiteral("p01:5");
    QTest::newRow("a page number above a billion") << QStringLiteral("p1000000001:5");
    QTest::newRow("a page number of 30 digits") << QStringLiteral("p111111111111111111111111111111:5");
    QTest::newRow("no node") << QStringLiteral("p1:");
    QTest::newRow("the node 0") << QStringLiteral("p1:0");
    QTest::newRow("a node number with a zero in front") << QStringLiteral("p1:05");
    QTest::newRow("a negative node") << QStringLiteral("p1:-5");
    QTest::newRow("a node with a sign") << QStringLiteral("p1:+5");
    QTest::newRow("a node, which is no number") << QStringLiteral("p1:x");
    QTest::newRow("a node with a space") << QStringLiteral("p1: 5");
    QTest::newRow("a node above 64 bits") << QStringLiteral("p1:18446744073709551616");
    QTest::newRow("a node of 30 digits") << QStringLiteral("p1:111111111111111111111111111111");
    QTest::newRow("a path ending with a solidus") << QStringLiteral("p1:5/");
    QTest::newRow("an empty item of the path") << QStringLiteral("p1:5//6");
    QTest::newRow("a path with a name") << QStringLiteral("p1:5/x");
    QTest::newRow("digits of another script") << QString::fromUtf8("p1:\xd9\xa5");     // U+0665 ARABIC-INDIC DIGIT FIVE

    // A path of 34 nodes: a page and 33 forms inside of each other (the session nests 32)
    QStringList path;
    for (int i = 0; i < 34; ++i)
    {
        path << QStringLiteral("5");
    }
    QTest::newRow("a path of 34 nodes") << QStringLiteral("p1:") + path.join(QLatin1Char('/'));

    // A megabyte of digits
    QTest::newRow("a megabyte of digits") << QStringLiteral("p1:") + QString(1024 * 1024, QLatin1Char('7'));
}

void DocumentToolContentTest::nodeIdSyntax()
{
    QFETCH(QString, text);

    // No container is loaded and none is needed: a wrong text is refused before the session is
    // asked for a container
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);

    QString errorText;
    const PDFContentNodeReference reference = PDFContentJson::readNodeId(text, &session, &errorText);
    QVERIFY(!reference.isValid());
    QVERIFY(!errorText.isEmpty());

    // The text of the caller is never put whole into the message
    QVERIFY(errorText.size() < 300);

    // No session and no reason
    QVERIFY(!PDFContentJson::readNodeId(text, nullptr, &errorText).isValid());
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!PDFContentJson::readNodeId(text, &session, nullptr).isValid());
}

// ---------------------------------------------------------------------------------------------
//  The document writer (zadani_jadro.md 4.4, 4.6; zadani.md RE-15, KE.21)
// ---------------------------------------------------------------------------------------------

void DocumentToolContentTest::writerUnchanged()
{
    // A container, which is unchanged, is not written at all: its page keeps its objects (the
    // first part of the gate of stage E0)
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFContentEditSnapshot snapshot = snapshotOf({ buildPage(document, 0), buildPage(document, 1) });
    QVERIFY(snapshot.containers.begin()->second->isUnchanged());

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QVERIFY(result.isSuccess());
    QVERIFY(result.errors.empty());
    QVERIFY(result.changedContainers.empty());
    QVERIFY(result.pagesChanged.empty());
    QCOMPARE(result.objectsAdded, PDFInteger(0));
    QCOMPARE(result.objectsEdited, PDFInteger(0));
    QCOMPARE(result.objectsRemoved, PDFInteger(0));
    QCOMPARE(result.fontsAdded, PDFInteger(0));
    QVERIFY(!result.isStructureChanged);
    QVERIFY(*builder.getStorage() == document.getStorage());

    // An empty snapshot writes nothing either
    const PDFContentWriteResult emptyResult = Writer::write(&document, PDFContentEditSnapshot(), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(emptyResult.status == Status::Success);
    QVERIFY(emptyResult.pagesChanged.empty());
    QVERIFY(*builder.getStorage() == document.getStorage());
}

void DocumentToolContentTest::writerDeletesNode()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const size_t objectCount = document.getStorage().getObjects().size();

    // The second path (the operations 4 and 5) is removed; the page 2 is loaded and not changed
    const PDFContentContainerPointer original = buildPage(document, 0);
    const std::vector<PDFContentNodePointer> paths = nodesOfKind(*original, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(2));
    const PDFContentContainerPointer edited = removeNode(original, paths[1]->getId());
    QVERIFY(edited != nullptr);
    QVERIFY(!edited->isUnchanged());

    const PDFContentEditSnapshot snapshot = snapshotOf({ edited, buildPage(document, 1) });

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QVERIFY(result.errors.empty());

    // The facts: one page, one object removed
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    QCOMPARE(result.objectsRemoved, PDFInteger(1));
    QCOMPARE(result.objectsAdded, PDFInteger(0));
    QCOMPARE(result.objectsEdited, PDFInteger(0));
    QCOMPARE(result.fontsAdded, PDFInteger(0));
    QVERIFY(!result.isStructureChanged);

    const PDFDocument written = builder.build();

    // ---- The changed page: one new content stream compressed by Flate; the operations, which
    //      stay, are the bytes of the document (the first four operations with their line feeds) ----
    const PDFDictionary* page = pageDictionary(written, 0);
    const PDFDictionary* originalPage = pageDictionary(document, 0);
    QVERIFY(page != nullptr);
    QVERIFY(page->get("Contents").isReference());
    QVERIFY(page->get("Contents") != originalPage->get("Contents"));

    // The new stream is the only new object
    QCOMPARE(written.getStorage().getObjects().size(), objectCount + 1);
    QCOMPARE(page->get("Contents").getReference(), PDFObjectReference(PDFInteger(objectCount), 0));

    const PDFObject& stream = written.getObject(page->get("Contents"));
    QVERIFY(stream.isStream());
    const PDFDictionary* streamDictionary = stream.getStream()->getDictionary();
    QCOMPARE(streamDictionary->getCount(), size_t(2));
    QVERIFY(streamDictionary->get("Filter") == PDFObject::createName("FlateDecode"));
    QCOMPARE(streamDictionary->get("Length").getInteger(), PDFInteger(stream.getStream()->getContent()->size()));
    QCOMPARE(pageContent(written, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));

    // Its own /Resources: a direct dictionary with everything it had
    QVERIFY(page->get("Resources").isDictionary());
    QVERIFY(page->get("Resources") == resourcesObject());

    // The other keys of the page are as they were
    QVERIFY(page->get("MediaBox") == originalPage->get("MediaBox"));
    QVERIFY(page->get("Parent") == originalPage->get("Parent"));
    QVERIFY(page->get("Type") == originalPage->get("Type"));
    QCOMPARE(page->getCount(), originalPage->getCount());

    // The old content stream stays in the document as it was
    const PDFObjectReference oldContents = originalPage->get("Contents").getReference();
    QVERIFY(written.getObjectByReference(oldContents) == document.getObjectByReference(oldContents));

    // ---- The page nobody changed: the page object and the objects of its /Contents and
    //      /Resources are the same objects ----
    const PDFObjectReference otherPage = document.getCatalog()->getPage(1)->getPageReference();
    QVERIFY(written.getObjectByReference(otherPage) == document.getObjectByReference(otherPage));
    const PDFObjectReference otherContents = pageDictionary(document, 1)->get("Contents").getReference();
    QVERIFY(pageDictionary(written, 1)->get("Contents") == PDFObject::createReference(otherContents));
    QVERIFY(written.getObjectByReference(otherContents) == document.getObjectByReference(otherContents));
    QVERIFY(pageDictionary(written, 1)->get("Resources") == pageDictionary(document, 1)->get("Resources"));

    // ---- Read back: one path is left after the two state runs ----
    const PDFContentContainerPointer readBack = buildPage(written, 0);
    QVERIFY(readBack != nullptr);
    QCOMPARE(nodesOfKind(*readBack, PDFContentNodeKind::Path).size(), size_t(1));
    QCOMPARE(nodesOfKind(*readBack, PDFContentNodeKind::StateRun).size(), size_t(2));
    QCOMPARE(readBack->getOperations().getCount(), size_t(4));
    QVERIFY(readBack->getFingerprint() != original->getFingerprint());
}

void DocumentToolContentTest::writerForcedRewrite()
{
    // The second part of the gate of stage E0: a forced rewrite without edits gives one stream
    // equal to the streams of /Contents joined by one line feed (ISO 32000-2, 7.8.2)
    const PDFDocument document = Documents::createDocument(QList<QByteArray>({ QByteArray("q 1 0 0 rg"), QByteArray("5 5 10 10 re f Q") }), &Documents::createNoResources);
    const PDFContentContainerPointer container = buildPage(document, 0);
    QVERIFY(container->isUnchanged());

    Writer::Options options;
    options.forceRewrite = true;

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ container }), &builder, options, nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    QCOMPARE(result.objectsAdded, PDFInteger(0));
    QCOMPARE(result.objectsEdited, PDFInteger(0));
    QCOMPARE(result.objectsRemoved, PDFInteger(0));

    const PDFDocument written = builder.build();
    QCOMPARE(pageContent(written, 0), QByteArray("q 1 0 0 rg\n5 5 10 10 re f Q"));

    // Without the option nothing is written
    PDFDocumentBuilder plainBuilder(&document);
    const PDFContentWriteResult plainResult = Writer::write(&document, snapshotOf({ container }), &plainBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(plainResult.status == Status::Success);
    QVERIFY(plainResult.pagesChanged.empty());
    QVERIFY(*plainBuilder.getStorage() == document.getStorage());
}

void DocumentToolContentTest::writerContentsMissing()
{
    // A page without /Contents: new content gives it one
    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference pageReference = documentBuilder.appendPage(QRectF(0, 0, 200, 200));
    const PDFDocument document = documentBuilder.build();
    QVERIFY(!pageDictionary(document, 0)->hasKey("Contents"));

    const PDFContentContainerPointer original = buildPage(document, 0);
    QVERIFY(original != nullptr);
    QVERIFY(!original->isReadOnly());
    QCOMPARE(original->getOperations().getCount(), size_t(0));

    const PDFContentContainerPointer edited = appendRectangle(original, "0 0 5 5", FIRST_NEW_NODE);
    QVERIFY(edited != nullptr);

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    QCOMPARE(result.objectsAdded, PDFInteger(1));
    QCOMPARE(result.objectsEdited, PDFInteger(0));
    QCOMPARE(result.objectsRemoved, PDFInteger(0));

    const PDFDocument written = builder.build();
    QCOMPARE(written.getCatalog()->getPage(0)->getPageReference(), pageReference);

    // The stream holds the bytes of the stream writer: the two new operations, each on its line
    const QByteArray content = pageContent(written, 0);
    QCOMPARE(content, PDFContentStreamWriter::write(*edited).content);
    QVERIFY2(content.contains("0 0 5 5 re\nf"), content.constData());
    QCOMPARE(content.trimmed(), QByteArray("0 0 5 5 re\nf"));

    // The page had no resources: it has an empty dictionary of its own now
    QVERIFY(pageDictionary(written, 0)->get("Resources").isDictionary());

    const PDFContentContainerPointer readBack = buildPage(written, 0);
    QCOMPARE(nodesOfKind(*readBack, PDFContentNodeKind::Path).size(), size_t(1));
}

void DocumentToolContentTest::writerContentsArray()
{
    // /Contents is an array of two streams; the first operation of the second stream begins at
    // the end of the first one, so its bytes are the line feed, which joins the streams, and
    // "0 0 1 rg" (ISO 32000-2, 7.8.2)
    const PDFDocument document = Documents::createDocument(QList<QByteArray>({ QByteArray("1 0 0 rg 10 10 50 50 re f"), QByteArray("0 0 1 rg 100 100 20 20 re f") }), &Documents::createNoResources);
    const PDFObject originalContents = pageDictionary(document, 0)->get("Contents");
    QVERIFY(originalContents.isArray());

    const PDFContentContainerPointer original = buildPage(document, 0);
    const std::vector<PDFContentNodePointer> paths = nodesOfKind(*original, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(2));
    const PDFContentContainerPointer edited = removeNode(original, paths[1]->getId());

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.objectsRemoved, PDFInteger(1));

    // One stream replaces the whole array
    const PDFDocument written = builder.build();
    QVERIFY(pageDictionary(written, 0)->get("Contents").isReference());
    QCOMPARE(pageContent(written, 0), QByteArray("1 0 0 rg 10 10 50 50 re f\n0 0 1 rg"));

    // The two old streams are untouched
    for (size_t i = 0; i < 2; ++i)
    {
        const PDFObjectReference reference = originalContents.getArray()->getItem(i).getReference();
        QVERIFY(written.getObjectByReference(reference) == document.getObjectByReference(reference));
    }
}

void DocumentToolContentTest::writerContentsShared()
{
    // Two pages share one content stream: the change of one page must not reach the other
    PDFDocumentBuilder documentBuilder;
    const PDFObject sharedContents = Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), TWO_PATHS);
    for (int i = 0; i < 2; ++i)
    {
        const PDFObjectReference page = documentBuilder.appendPage(QRectF(0, 0, 200, 200));
        documentBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", resourcesObject() }, { "Contents", sharedContents } }));
    }
    const PDFDocument document = documentBuilder.build();

    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId());

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);

    // Only the page of the container is changed
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));

    const PDFDocument written = builder.build();
    QCOMPARE(pageContent(written, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QVERIFY(pageDictionary(written, 0)->get("Contents") != sharedContents);

    // The other page still has the shared stream with all its bytes
    QVERIFY(pageDictionary(written, 1)->get("Contents") == sharedContents);
    QCOMPARE(pageContent(written, 1), QByteArray(TWO_PATHS));
    QVERIFY(written.getObjectByReference(sharedContents.getReference()) == document.getObjectByReference(sharedContents.getReference()));
    const PDFObjectReference otherPage = document.getCatalog()->getPage(1)->getPageReference();
    QVERIFY(written.getObjectByReference(otherPage) == document.getObjectByReference(otherPage));
}

void DocumentToolContentTest::writerReadOnly()
{
    // A page, whose /Contents cannot be read whole (an item, which is no stream, and a stream,
    // which cannot be decoded), is read-only: it is never written, not even by a forced rewrite
    const PDFDocument broken = Documents::createBrokenContentsDocument("1 0 0 rg", "0 0 5 5 re f", &Documents::createNoResources);
    const PDFContentContainerPointer brokenContainer = buildPage(broken, 0);
    QVERIFY(brokenContainer != nullptr);
    QVERIFY(brokenContainer->isReadOnly());

    // /Contents, which is neither a stream nor an array (a dictionary)
    const PDFDocument wrong = Documents::createDocumentFromObjects([](PDFDocumentBuilder* builder) { return Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("NotAStream") } })); },
                                                                   &Documents::createNoResources);
    const PDFContentContainerPointer wrongContainer = buildPage(wrong, 0);
    QVERIFY(wrongContainer != nullptr);
    QVERIFY(wrongContainer->isReadOnly());

    Writer::Options options;
    options.forceRewrite = true;

    for (const std::pair<const PDFDocument*, PDFContentContainerPointer>& item : { std::make_pair(&broken, brokenContainer), std::make_pair(&wrong, wrongContainer) })
    {
        PDFDocumentBuilder builder(item.first);
        const PDFContentWriteResult result = Writer::write(item.first, snapshotOf({ item.second }), &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.pagesChanged.empty());
        QVERIFY(*builder.getStorage() == item.first->getStorage());
    }
}

void DocumentToolContentTest::writerResources_data()
{
    QTest::addColumn<QString>("layout");

    QTest::newRow("a direct dictionary of each page") << QStringLiteral("direct");
    QTest::newRow("one indirect dictionary shared by the pages") << QStringLiteral("shared");
    QTest::newRow("inherited from the page tree") << QStringLiteral("inherited");
    QTest::newRow("a shared indirect dictionary of the category") << QStringLiteral("category");
}

void DocumentToolContentTest::writerResources()
{
    QFETCH(QString, layout);

    // R-4: a changed page gets a /Resources dictionary of its own; a dictionary shared with
    // another page or inherited from the page tree is copied, never changed in place - and so is
    // the dictionary of a category, to which a name is added
    PDFDocumentBuilder documentBuilder;
    PDFObject shared;       // The shared object: /Resources, or the dictionary of /ExtGState
    if (layout == QLatin1String("shared"))
    {
        shared = Documents::addObject(&documentBuilder, resourcesObject());
    }
    else if (layout == QLatin1String("category"))
    {
        shared = Documents::addObject(&documentBuilder, Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }));
    }

    PDFObjectReference pageReferences[2];
    for (int i = 0; i < 2; ++i)
    {
        pageReferences[i] = documentBuilder.appendPage(QRectF(0, 0, 200, 200));

        Documents::DictionaryEntries entries = { { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), ONE_PATH) } };
        if (layout == QLatin1String("direct"))
        {
            entries.push_back({ "Resources", resourcesObject() });
        }
        else if (layout == QLatin1String("shared"))
        {
            entries.push_back({ "Resources", shared });
        }
        else if (layout == QLatin1String("category"))
        {
            entries.push_back({ "Resources", Documents::createDictionaryObject({ { "ExtGState", shared }, { "ProcSet", Documents::createArrayObject({ PDFObject::createName("PDF") }) } }) });
        }
        documentBuilder.mergeTo(pageReferences[i], Documents::createDictionaryObject(std::move(entries)));
    }

    // The node of the page tree (ISO 32000-2, 7.7.3.4: /Resources is inheritable)
    const PDFObjectReference pagesReference = documentBuilder.getDictionaryFromObject(documentBuilder.getObjectByReference(documentBuilder.getCatalogReference()))->get("Pages").getReference();
    if (layout == QLatin1String("inherited"))
    {
        documentBuilder.mergeTo(pagesReference, Documents::createDictionaryObject({ { "Resources", resourcesObject() } }));
    }
    const PDFDocument document = documentBuilder.build();

    // The path of the page 1 gets the alpha 0.5: the stream writer needs a new graphic state
    // /GS1 << /Type /ExtGState /ca 0.5 >> in /ExtGState
    const PDFContentContainerPointer original = buildPage(document, 0);
    QVERIFY(original != nullptr);
    const PDFContentContainerPointer edited = setOverride(original, nodesOfKind(*original, PDFContentNodeKind::Path)[0]->getId(), alphaOverride());
    QVERIFY(edited != nullptr);

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    QCOMPARE(result.objectsEdited, PDFInteger(1));
    QCOMPARE(result.objectsAdded, PDFInteger(0));
    QCOMPARE(result.objectsRemoved, PDFInteger(0));

    const PDFDocument written = builder.build();

    // ---- The changed page: a direct dictionary with the old name /GP, the new /GS1 and the
    //      other keys of the resources ----
    const PDFObject expectedGraphicStates = Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) },
                                                                                { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }) } });
    const PDFDictionary* page = pageDictionary(written, 0);
    QVERIFY(page->get("Resources").isDictionary());
    const PDFDictionary* resources = page->get("Resources").getDictionary();
    QCOMPARE(resources->getCount(), size_t(2));
    QVERIFY(resources->get("ExtGState").isDictionary());
    QVERIFY(resources->get("ExtGState") == expectedGraphicStates);
    QVERIFY(resources->get("ProcSet") == Documents::createArrayObject({ PDFObject::createName("PDF") }));

    // The content names the new graphic state inside of a wrap q ... Q around the path
    const QByteArray content = pageContent(written, 0);
    QCOMPARE(content, PDFContentStreamWriter::write(*edited).content);
    const qsizetype save = content.indexOf("q\n");
    const qsizetype state = content.indexOf("/GS1 gs");
    const qsizetype path = content.indexOf("20 20 30 30 re");
    const qsizetype restore = content.lastIndexOf("Q");
    QVERIFY2(save >= 0 && save < state && state < path && path < restore, content.constData());

    // ---- What is shared stays as it was, and the other page still uses it ----
    if (!shared.isNull())
    {
        QVERIFY(written.getObjectByReference(shared.getReference()) == document.getObjectByReference(shared.getReference()));
    }
    QVERIFY(written.getObjectByReference(pagesReference) == document.getObjectByReference(pagesReference));
    QVERIFY(written.getObjectByReference(pageReferences[1]) == document.getObjectByReference(pageReferences[1]));
    QCOMPARE(pageDictionary(written, 1)->hasKey("Resources"), layout != QLatin1String("inherited"));

    // Read back: the other page still finds only /GP, the changed one both names
    const PDFContentContainerPointer otherPage = buildPage(written, 1);
    QCOMPARE(otherPage->getResources().getEntries(PDFContentResourceCategory::ExtGState).size(), size_t(1));
    QCOMPARE(otherPage->getResources().getEntries(PDFContentResourceCategory::ExtGState)[0].name, QByteArray("GP"));
    QVERIFY(otherPage->getFingerprint() == buildPage(document, 1)->getFingerprint());

    const PDFContentContainerPointer readBack = buildPage(written, 0);
    QCOMPARE(readBack->getResources().getEntries(PDFContentResourceCategory::ExtGState).size(), size_t(2));
    QVERIFY(readBack->getIssues().empty());
}

void DocumentToolContentTest::writerNewObjects()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });

    // The provisional references of a session have the numbers 1, 2, 3 ... and the reserved
    // generation 65535 (the generation of the head of the free list of a cross-reference table,
    // ISO 32000-2, 7.5.4: no object in use has it). A builder made from the document gives its
    // first new object the number of the objects of the storage.
    QCOMPARE(PDFContentEditSession::NEW_OBJECT_GENERATION, PDFInteger(65535));
    const PDFInteger first = PDFInteger(document.getStorage().getObjects().size());
    const PDFObjectReference unused(1, 65535);
    const PDFObjectReference pattern(2, 65535);
    const PDFObjectReference shading(3, 65535);

    const PDFObject unusedObject = Documents::createDictionaryObject({ { "Unused", PDFObject::createBool(true) } });
    const PDFObject shadingObject = Documents::createDictionaryObject({ { "ShadingType", PDFObject::createInteger(2) },
                                                                        { "ColorSpace", PDFObject::createName("DeviceRGB") },
                                                                        { "Coords", Documents::createIntegerArrayObject({ 0, 0, 100, 0 }) } });

    // A new object referring to another new object (the pattern to its shading), and a new
    // object nothing refers to
    PDFContentEditSnapshot snapshot;
    snapshot.newObjects[unused] = unusedObject;
    snapshot.newObjects[pattern] = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Pattern") },
                                                                       { "PatternType", PDFObject::createInteger(2) },
                                                                       { "Shading", PDFObject::createReference(shading) } });
    snapshot.newObjects[shading] = shadingObject;

    // The page names the pattern in its resources (as a command does for the copy of a pattern)
    QByteArray name;
    const PDFContentContainerPointer edited = addResource(buildPage(document, 0), PDFContentResourceCategory::Pattern, "P", PDFObject::createReference(pattern), &name);
    QCOMPARE(name, QByteArray("P1"));
    QVERIFY(!edited->isUnchanged());
    snapshot.containers[edited->getKey()] = edited;

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.fontsAdded, PDFInteger(0));

    const PDFDocument written = builder.build();

    // The reachable new objects are the first new objects of the builder, in the order of their
    // provisional references: the pattern (2) is "first", its shading (3) "first + 1"; the object
    // 1, which nothing refers to, takes no number
    const PDFObjectReference writtenPattern(first, 0);
    const PDFObjectReference writtenShading(first + 1, 0);
    QVERIFY(written.getObjectByReference(writtenPattern) == Documents::createDictionaryObject({ { "Type", PDFObject::createName("Pattern") },
                                                                                               { "PatternType", PDFObject::createInteger(2) },
                                                                                               { "Shading", PDFObject::createReference(writtenShading) } }));
    QVERIFY(written.getObjectByReference(writtenShading) == shadingObject);

    // The resources of the page name the real reference
    const PDFDictionary* resources = pageDictionary(written, 0)->get("Resources").getDictionary();
    QVERIFY(resources->get("Pattern") == Documents::createDictionaryObject({ { "P1", PDFObject::createReference(writtenPattern) } }));

    // Two new objects and the content stream; the object nothing refers to is not written
    QCOMPARE(PDFInteger(written.getStorage().getObjects().size()), first + 3);
    QVERIFY(pageDictionary(written, 0)->get("Contents") == PDFObject::createReference(PDFObjectReference(first + 2, 0)));
    for (const auto& entry : written.getStorage().getObjects())
    {
        QVERIFY(entry.object != unusedObject);
    }

    // The content itself did not change
    QCOMPARE(pageContent(written, 0), QByteArray(TWO_PATHS));

    // ---- A builder, which has two objects more than the document (as after the write of
    //      another page): the real references of three new objects are "first + 2" ... "first +
    //      4" in the order of the provisional ones. The pattern (2) refers to its shading (1) ->
    //      "first + 2" and to its function (3) -> "first + 4". ----
    {
        const PDFObjectReference shiftedShading(1, 65535);
        const PDFObjectReference shiftedPattern(2, 65535);
        const PDFObjectReference shiftedFunction(3, 65535);
        const PDFObject functionObject = Documents::createDictionaryObject({ { "FunctionType", PDFObject::createInteger(2) }, { "N", PDFObject::createInteger(1) } });

        PDFContentEditSnapshot shifted;
        shifted.newObjects[shiftedShading] = shadingObject;
        shifted.newObjects[shiftedPattern] = Documents::createDictionaryObject({ { "Shading", PDFObject::createReference(shiftedShading) }, { "Function", PDFObject::createReference(shiftedFunction) } });
        shifted.newObjects[shiftedFunction] = functionObject;
        const PDFContentContainerPointer shiftedPage = addResource(buildPage(document, 0), PDFContentResourceCategory::Pattern, "P", PDFObject::createReference(shiftedPattern), &name);
        shifted.containers[shiftedPage->getKey()] = shiftedPage;

        PDFDocumentBuilder shiftedBuilder(&document);
        shiftedBuilder.addObject(PDFObject::createInteger(1));
        shiftedBuilder.addObject(PDFObject::createInteger(2));
        QVERIFY(Writer::write(&document, shifted, &shiftedBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);
        const PDFDocument shiftedWritten = shiftedBuilder.build();

        QVERIFY(shiftedWritten.getObjectByReference(PDFObjectReference(first, 0)) == PDFObject::createInteger(1));
        QVERIFY(shiftedWritten.getObjectByReference(PDFObjectReference(first + 1, 0)) == PDFObject::createInteger(2));
        QVERIFY(shiftedWritten.getObjectByReference(PDFObjectReference(first + 2, 0)) == shadingObject);
        QVERIFY(shiftedWritten.getObjectByReference(PDFObjectReference(first + 3, 0)) == Documents::createDictionaryObject({ { "Shading", PDFObject::createReference(PDFObjectReference(first + 2, 0)) },
                                                                                                                         { "Function", PDFObject::createReference(PDFObjectReference(first + 4, 0)) } }));
        QVERIFY(shiftedWritten.getObjectByReference(PDFObjectReference(first + 4, 0)) == functionObject);
        QVERIFY(pageDictionary(shiftedWritten, 0)->get("Resources").getDictionary()->get("Pattern") == Documents::createDictionaryObject({ { "P1", PDFObject::createReference(PDFObjectReference(first + 3, 0)) } }));
    }

    // ---- A new font named by the resources is a fact of the result ----
    PDFContentEditSnapshot fontSnapshot;
    fontSnapshot.newObjects[unused] = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Font") }, { "Subtype", PDFObject::createName("Type1") }, { "BaseFont", PDFObject::createName("Courier") } });
    const PDFContentContainerPointer withFont = addResource(buildPage(document, 1), PDFContentResourceCategory::Font, "F", PDFObject::createReference(unused), &name);
    QCOMPARE(name, QByteArray("F1"));
    fontSnapshot.containers[withFont->getKey()] = withFont;

    PDFDocumentBuilder fontBuilder(&document);
    const PDFContentWriteResult fontResult = Writer::write(&document, fontSnapshot, &fontBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(fontResult.status == Status::Success);
    QCOMPARE(fontResult.fontsAdded, PDFInteger(1));
    QCOMPARE(fontResult.pagesChanged, std::vector<PDFInteger>({ 1 }));
}

void DocumentToolContentTest::writerNewObjectsCollision()
{
    // A provisional reference is a reference with the generation 65535, which is a key of the
    // new objects of the snapshot: exactly these are replaced, in the entries the editor has
    // added. The document has references to an object, which does not exist (ISO 32000-2, 7.3.10:
    // such a reference is the null object), with the number of the new object 900:
    //   page 1: /Font << /F1 900 0 R >>            the number with another generation
    //   page 2: /Shading << /S9 900 65535 R >>     the number and the reserved generation
    const PDFObject dangling = PDFObject::createReference(PDFObjectReference(900, 0));
    const PDFObject danglingReserved = PDFObject::createReference(PDFObjectReference(900, 65535));

    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference page1 = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    documentBuilder.mergeTo(page1, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "Font", Documents::createDictionaryObject({ { "F1", dangling } }) } }) },
                                                                       { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), TWO_PATHS) } }));
    const PDFObjectReference page2 = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    documentBuilder.mergeTo(page2, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "Shading", Documents::createDictionaryObject({ { "S9", danglingReserved } }) } }) },
                                                                       { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), ONE_PATH) } }));
    const PDFDocument document = documentBuilder.build();
    const PDFInteger first = PDFInteger(document.getStorage().getObjects().size());
    QVERIFY(first < 900);
    QVERIFY(document.getObject(dangling).isNull());

    const PDFObjectReference provisional(900, 65535);
    const PDFObject patternObject = Documents::createDictionaryObject({ { "Type", PDFObject::createName("Pattern") }, { "PatternType", PDFObject::createInteger(2) } });

    PDFContentEditSnapshot snapshot;
    snapshot.newObjects[provisional] = patternObject;

    // The page 2 gets two entries: /F1 of the page 1 as it is (a copy of an object to another
    // page adds the entry of its resource verbatim) and the new pattern
    QByteArray name;
    PDFContentContainerPointer edited = addResource(buildPage(document, 1), PDFContentResourceCategory::Font, "F", dangling, &name);
    QCOMPARE(name, QByteArray("F1"));
    edited = addResource(edited, PDFContentResourceCategory::Pattern, "P", PDFObject::createReference(provisional), &name);
    QCOMPARE(name, QByteArray("P1"));
    snapshot.containers[edited->getKey()] = edited;

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);

    const PDFDocument written = builder.build();
    const PDFDictionary* resources = pageDictionary(written, 1)->get("Resources").getDictionary();

    // The added entry with the number of the new object and the generation 0 is a reference of
    // the document: it still names nothing
    QVERIFY(resources->get("Font") == Documents::createDictionaryObject({ { "F1", dangling } }));

    // The entry of the document with the reserved generation is never touched
    QVERIFY(resources->get("Shading") == Documents::createDictionaryObject({ { "S9", danglingReserved } }));

    // The added entry with the provisional reference names the new object: the first new object
    // of the builder, the content stream is the second one
    QVERIFY(resources->get("Pattern") == Documents::createDictionaryObject({ { "P1", PDFObject::createReference(PDFObjectReference(first, 0)) } }));
    QVERIFY(written.getObjectByReference(PDFObjectReference(first, 0)) == patternObject);
    QCOMPARE(PDFInteger(written.getStorage().getObjects().size()), first + 2);
    QVERIFY(written.getObject(dangling).isNull());

    // The new object is written, because an entry of the editor names it - never because of
    // the entry /S9 of the document, which has its number and its generation: with the path of
    // the page 2 removed and no entry added, only the content stream is new
    {
        PDFContentEditSnapshot onlyOriginal;
        onlyOriginal.newObjects[provisional] = patternObject;
        const PDFContentContainerPointer secondPage = buildPage(document, 1);
        const PDFContentContainerPointer withoutPath = removeNode(secondPage, nodesOfKind(*secondPage, PDFContentNodeKind::Path)[0]->getId());
        onlyOriginal.containers[withoutPath->getKey()] = withoutPath;

        PDFDocumentBuilder onlyOriginalBuilder(&document);
        QVERIFY(Writer::write(&document, onlyOriginal, &onlyOriginalBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);
        QCOMPARE(PDFInteger(onlyOriginalBuilder.getStorage()->getObjects().size()), first + 1);
        QVERIFY(onlyOriginalBuilder.getObjectByReference(PDFObjectReference(first, 0)).isStream());
        QVERIFY(onlyOriginalBuilder.getDictionaryFromObject(onlyOriginalBuilder.getObjectByReference(page2))->get("Resources").getDictionary()->get("Shading") == Documents::createDictionaryObject({ { "S9", danglingReserved } }));
    }

    // ---- A key of the new objects without the reserved generation is no provisional
    //      reference. Here it is the reference of a font of the document, and an added entry
    //      names that font: the entry keeps naming the font, nothing is added but the content
    //      stream (the wrong new object is not written) ----
    PDFObject font;
    const PDFDocument fontDocument = Documents::createDocument(QByteArray("BT /F1 12 Tf (A) Tj ET"), [&font](PDFDocumentBuilder* fontBuilder)
    {
        font = Documents::addStandardFontObject(fontBuilder, "Helvetica");
        PDFDictionaryBuilder fontResources;
        fontResources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", font } }));
        return fontResources;
    });
    const PDFInteger fontFirst = PDFInteger(fontDocument.getStorage().getObjects().size());
    const PDFObject fontObject = fontDocument.getObjectByReference(font.getReference());
    QVERIFY(fontObject.isDictionary());
    QCOMPARE(font.getReference().generation, PDFInteger(0));

    PDFContentEditSnapshot wrongKey;
    wrongKey.newObjects[font.getReference()] = patternObject;
    const PDFContentContainerPointer withPattern = addResource(buildPage(fontDocument, 0), PDFContentResourceCategory::Pattern, "P", font, &name);
    QCOMPARE(name, QByteArray("P1"));
    wrongKey.containers[withPattern->getKey()] = withPattern;

    PDFDocumentBuilder wrongKeyBuilder(&fontDocument);
    QVERIFY(Writer::write(&fontDocument, wrongKey, &wrongKeyBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);
    const PDFDocument wrongKeyWritten = wrongKeyBuilder.build();
    const PDFDictionary* wrongKeyResources = pageDictionary(wrongKeyWritten, 0)->get("Resources").getDictionary();
    QVERIFY(wrongKeyResources->get("Font") == Documents::createDictionaryObject({ { "F1", font } }));
    QVERIFY(wrongKeyResources->get("Pattern") == Documents::createDictionaryObject({ { "P1", font } }));
    QVERIFY(wrongKeyWritten.getObjectByReference(font.getReference()) == fontObject);
    QCOMPARE(PDFInteger(wrongKeyWritten.getStorage().getObjects().size()), fontFirst + 1);
    QVERIFY(wrongKeyWritten.getObjectByReference(PDFObjectReference(fontFirst, 0)).isStream());

    // ---- A new object, which nothing of the editor names, is not written: only the new content
    //      stream is added (the text is removed, so the page is changed) ----
    PDFContentEditSnapshot unreached;
    unreached.newObjects[PDFObjectReference(1, 65535)] = patternObject;
    const PDFContentContainerPointer page = buildPage(fontDocument, 0);
    const PDFContentContainerPointer withoutText = removeNode(page, nodesOfKind(*page, PDFContentNodeKind::TextObject)[0]->getId());
    unreached.containers[withoutText->getKey()] = withoutText;

    PDFDocumentBuilder unreachedBuilder(&fontDocument);
    QVERIFY(Writer::write(&fontDocument, unreached, &unreachedBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);
    const PDFDocument unreachedWritten = unreachedBuilder.build();
    QCOMPARE(PDFInteger(unreachedWritten.getStorage().getObjects().size()), fontFirst + 1);
    QVERIFY(unreachedWritten.getObjectByReference(PDFObjectReference(fontFirst, 0)).isStream());
    QVERIFY(pageDictionary(unreachedWritten, 0)->get("Resources").getDictionary()->get("Font") == Documents::createDictionaryObject({ { "F1", font } }));
    QVERIFY(unreachedWritten.getObjectByReference(font.getReference()) == fontObject);
}

void DocumentToolContentTest::writerFingerprint()
{
    // RE-15: a container is written only into the document it was built from. The second
    // document has the same objects with one more byte in the content of the page 1; the third
    // one another value in the resources of the page 1.
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocument otherContent = createPagesDocument({ QByteArray(TWO_PATHS) + "\n", ONE_PATH });
    const PDFDocument otherResources = createPagesDocument({ TWO_PATHS, ONE_PATH }, 7.0);

    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId());
    const PDFContentContainerPointer unchanged = buildPage(document, 1);
    const PDFContentEditSnapshot snapshot = snapshotOf({ edited, unchanged });
    const PDFContentContainerKey key = edited->getKey();
    QVERIFY(key == pageKey(otherContent, 0));

    // The fingerprint stays with the container through the edit
    QCOMPARE(edited->getFingerprint(), original->getFingerprint());
    QCOMPARE(edited->getFingerprint(), PDFContentTreeBuilder::computeFingerprint(&document, key));

    for (const PDFDocument* changedDocument : { &otherContent, &otherResources })
    {
        PDFDocumentBuilder builder(changedDocument);
        const PDFContentWriteResult result = Writer::write(changedDocument, snapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::DocumentChanged);
        QVERIFY(!result.isSuccess());

        // Only the containers, which differ, are named: the content differs on the page 1 only,
        // the resources on both pages (also on the page 2, whose container is unchanged)
        if (changedDocument == &otherContent)
        {
            QCOMPARE(result.changedContainers, std::vector<PDFContentContainerKey>({ key }));
        }
        else
        {
            QCOMPARE(result.changedContainers, std::vector<PDFContentContainerKey>({ key, unchanged->getKey() }));
        }
        QVERIFY(result.pagesChanged.empty());
        QVERIFY(result.errors.empty());
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
        QVERIFY(*builder.getStorage() == changedDocument->getStorage());
    }

    // An unchanged container is checked too: its identifiers are as stale as those of a changed one
    {
        PDFDocumentBuilder builder(&otherContent);
        const PDFContentWriteResult result = Writer::write(&otherContent, snapshotOf({ original }), &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::DocumentChanged);
        QCOMPARE(result.changedContainers, std::vector<PDFContentContainerKey>({ key }));
    }

    // The same snapshot over its own document is written
    {
        PDFDocumentBuilder builder(&document);
        QVERIFY(Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr).status == Status::Success);
    }

    // ---- A snapshot, which does not hold together, is refused the same way ----
    const PDFContentContainerKey otherKey = unchanged->getKey();

    // A container under the key of another page
    PDFContentEditSnapshot wrongKey;
    wrongKey.containers[otherKey] = edited;

    // No container
    PDFContentEditSnapshot noContainer;
    noContainer.containers[key] = nullptr;

    // A container made by hand without a fingerprint
    PDFContentEditSnapshot noFingerprint;
    PDFContentContainer::Data data = edited->getData();
    data.fingerprint.clear();
    noFingerprint.containers[key] = std::make_shared<const PDFContentContainer>(std::move(data));

    // A page, which the document does not have
    PDFContentEditSnapshot noPage;
    PDFContentContainer::Data missingData = edited->getData();
    missingData.key = PDFContentContainerKey::createPage(7, key.reference);
    noPage.containers[missingData.key] = std::make_shared<const PDFContentContainer>(PDFContentContainer::Data(missingData));

    // A page, which the document does not have, and a container without a fingerprint: nothing
    // to compare is no match
    PDFContentEditSnapshot nothingNamed;
    PDFContentContainer::Data emptyData = edited->getData();
    emptyData.key = PDFContentContainerKey::createPage(7, key.reference);
    emptyData.fingerprint.clear();
    nothingNamed.containers[emptyData.key] = std::make_shared<const PDFContentContainer>(PDFContentContainer::Data(emptyData));
    QVERIFY(PDFContentTreeBuilder::computeFingerprint(&document, emptyData.key).isEmpty());

    for (const PDFContentEditSnapshot* wrongSnapshot : { &wrongKey, &noContainer, &noFingerprint, &noPage, &nothingNamed })
    {
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, *wrongSnapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::DocumentChanged);
        QCOMPARE(result.changedContainers.size(), size_t(1));
        QVERIFY(result.changedContainers[0] == wrongSnapshot->containers.begin()->first);
        QVERIFY(*builder.getStorage() == document.getStorage());
    }

    // Two pages with the same content and equal resources have the same fingerprint: the
    // container of the page 1 under the key of the page 2 is refused all the same (it would be
    // written into the page 1, which nobody has checked)
    const PDFDocument twins = createPagesDocument({ ONE_PATH, ONE_PATH });
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&twins, pageKey(twins, 0)), PDFContentTreeBuilder::computeFingerprint(&twins, pageKey(twins, 1)));
    const PDFContentContainerPointer twin = buildPage(twins, 0);
    PDFContentEditSnapshot twinSnapshot;
    twinSnapshot.containers[pageKey(twins, 1)] = removeNode(twin, nodesOfKind(*twin, PDFContentNodeKind::Path)[0]->getId());

    PDFDocumentBuilder twinBuilder(&twins);
    const PDFContentWriteResult twinResult = Writer::write(&twins, twinSnapshot, &twinBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(twinResult.status == Status::DocumentChanged);
    QCOMPARE(twinResult.changedContainers, std::vector<PDFContentContainerKey>({ pageKey(twins, 1) }));
    QVERIFY(*twinBuilder.getStorage() == twins.getStorage());
}

void DocumentToolContentTest::writerErrorsAndSkippedPages()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });

    // The page 1 has a change, which can be written; the path of the page 2 has an override,
    // which cannot (a singular matrix)
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId());
    const PDFContentContainerPointer otherOriginal = buildPage(document, 1);
    const PDFContentNodeId brokenNode = nodesOfKind(*otherOriginal, PDFContentNodeKind::Path)[0]->getId();
    const PDFContentContainerPointer broken = setOverride(otherOriginal, brokenNode, brokenOverride());
    QVERIFY(broken != nullptr);
    const PDFContentEditSnapshot snapshot = snapshotOf({ edited, broken });

    // ---- An error of any page writes nothing: also the page, which could be written, stays ----
    {
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::PageErrors);
        QVERIFY(!result.isSuccess());
        QVERIFY(*builder.getStorage() == document.getStorage());

        // The error is listed under its page with its container and its node
        QCOMPARE(result.errors.size(), size_t(1));
        QCOMPARE(result.errors.begin()->first, PDFInteger(1));
        const std::vector<PDFContentWriteError>& errors = result.errors.begin()->second;
        QCOMPARE(errors.size(), size_t(1));
        QVERIFY(errors[0].container == broken->getKey());
        QVERIFY(errors[0].nodeId == brokenNode);
        QVERIFY(!errors[0].text.isEmpty());

        // No facts of something, which was not written
        QVERIFY(result.pagesChanged.empty());
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
        QCOMPARE(result.objectsEdited, PDFInteger(0));
    }

    // ---- The page with the error is skipped: the other one is written ----
    {
        Writer::Options options;
        options.skippedPages = { 1 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.errors.empty());
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QCOMPARE(result.objectsRemoved, PDFInteger(1));
        QCOMPARE(result.objectsEdited, PDFInteger(0));

        const PDFDocument written = builder.build();
        QCOMPARE(pageContent(written, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
        const PDFObjectReference skippedPage = document.getCatalog()->getPage(1)->getPageReference();
        QVERIFY(written.getObjectByReference(skippedPage) == document.getObjectByReference(skippedPage));
        QCOMPARE(pageContent(written, 1), QByteArray(ONE_PATH));
    }

    // ---- The page, which can be written, is skipped: the error of the other one stays ----
    {
        Writer::Options options;
        options.skippedPages = { 0 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::PageErrors);
        QCOMPARE(result.errors.size(), size_t(1));
        QCOMPARE(result.errors.begin()->first, PDFInteger(1));
        QVERIFY(*builder.getStorage() == document.getStorage());
    }

    // ---- Both pages are skipped: nothing is written and nothing fails ----
    {
        Writer::Options options;
        options.skippedPages = { 0, 1, 5 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.pagesChanged.empty());
        QVERIFY(*builder.getStorage() == document.getStorage());
    }
}

void DocumentToolContentTest::writerForm()
{
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);
    const size_t objectCount = document.getStorage().getObjects().size();

    // The rectangle of the form /Fm1 is removed: "/GF gs", " 0 1 0 rg" stay of "/GF gs 0 1 0 rg 5 5 20 10 re f"
    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(form1);
    const PDFContentContainerPointer original = buildContainer(document, formKey, 0);
    QVERIFY(original != nullptr);
    const std::vector<PDFContentNodePointer> paths = nodesOfKind(*original, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(1));
    const PDFContentContainerPointer edited = removeNode(original, paths[0]->getId());

    // Only the form is in the snapshot: no page of it was changed (or even loaded)
    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.objectsRemoved, PDFInteger(1));

    // Both pages draw the form: both are changed, though no container of a page was written
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));

    const PDFDocument written = builder.build();

    // ---- The form keeps its object: the same reference, new data, its own keys ----
    QCOMPARE(written.getStorage().getObjects().size(), objectCount);
    const PDFObject& stream = written.getObjectByReference(form1);
    QVERIFY(stream.isStream());
    QCOMPARE(written.getDecodedStream(stream.getStream()), QByteArray("/GF gs 0 1 0 rg"));

    const PDFDictionary* dictionary = stream.getStream()->getDictionary();
    const PDFDictionary* originalDictionary = document.getObjectByReference(form1).getStream()->getDictionary();
    QVERIFY(dictionary->get("Type") == PDFObject::createName("XObject"));
    QVERIFY(dictionary->get("Subtype") == PDFObject::createName("Form"));
    QVERIFY(dictionary->get("BBox") == originalDictionary->get("BBox"));
    QVERIFY(dictionary->get("Matrix") == originalDictionary->get("Matrix"));
    QVERIFY(dictionary->get("Filter") == PDFObject::createName("FlateDecode"));
    QCOMPARE(dictionary->get("Length").getInteger(), PDFInteger(stream.getStream()->getContent()->size()));

    // Its own resources with the name its content still uses: /ExtGState << /GF << /Type /ExtGState /LW 4 >> >>
    QVERIFY(dictionary->get("Resources").isDictionary());
    const PDFObject formGraphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "LW", PDFObject::createInteger(4) } });
    QVERIFY(dictionary->get("Resources") == Documents::createDictionaryObject({ { "ExtGState", Documents::createDictionaryObject({ { "GF", formGraphicState } }) } }));

    // No other key than those of the form and of its new data
    QCOMPARE(dictionary->getCount(), size_t(7));

    // ---- The pages and the other form are untouched ----
    for (const PDFObjectReference reference : { document.getCatalog()->getPage(0)->getPageReference(), document.getCatalog()->getPage(1)->getPageReference(), form2 })
    {
        QVERIFY(written.getObjectByReference(reference) == document.getObjectByReference(reference));
    }

    // Read back
    const PDFContentContainerPointer readBack = buildContainer(written, formKey, 0);
    QVERIFY(readBack != nullptr);
    QVERIFY(nodesOfKind(*readBack, PDFContentNodeKind::Path).empty());
    QCOMPARE(readBack->getOperations().getCount(), size_t(2));
}

void DocumentToolContentTest::writerFormErrors()
{
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);

    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(form1);
    const PDFContentContainerPointer original = buildContainer(document, formKey, 0);
    const PDFContentNodeId brokenNode = nodesOfKind(*original, PDFContentNodeKind::Path)[0]->getId();
    const PDFContentEditSnapshot snapshot = snapshotOf({ setOverride(original, brokenNode, brokenOverride()) });

    // An error of a form is listed under every written page, which uses it
    {
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::PageErrors);
        QCOMPARE(result.errors.size(), size_t(2));
        for (const PDFInteger pageIndex : { 0, 1 })
        {
            QVERIFY(result.errors.count(pageIndex));
            const std::vector<PDFContentWriteError>& errors = result.errors.at(pageIndex);
            QCOMPARE(errors.size(), size_t(1));
            QVERIFY(errors[0].container == formKey);
            QVERIFY(errors[0].nodeId == brokenNode);
            QVERIFY(!errors[0].text.isEmpty());
        }
        QVERIFY(*builder.getStorage() == document.getStorage());
    }

    // One of its pages is skipped: a skipped page stays as it is, and the form is one object -
    // writing it for the other page would change the skipped one. The form is left out (so it
    // has no error either) and the result says, which skipped page keeps it out.
    {
        Writer::Options options;
        options.skippedPages = { 1 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.errors.empty());
        QVERIFY(result.pagesChanged.empty());
        QCOMPARE(result.skippedContainers.size(), size_t(1));
        QVERIFY(result.skippedContainers.begin()->first == formKey);
        QCOMPARE(result.skippedContainers.begin()->second, std::vector<PDFInteger>({ 1 }));
        QVERIFY(*builder.getStorage() == document.getStorage());
    }

    // Skipping all its pages skips the form too
    {
        Writer::Options options;
        options.skippedPages = { 0, 1 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.pagesChanged.empty());
        QCOMPARE(result.skippedContainers.size(), size_t(1));
        QCOMPARE(result.skippedContainers.at(formKey), std::vector<PDFInteger>({ 0, 1 }));
        QVERIFY(*builder.getStorage() == document.getStorage());
    }

    // A skipped page, which does not use the form, does not keep it out
    {
        Writer::Options options;
        options.skippedPages = { 7 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::PageErrors);
        QCOMPARE(result.errors.size(), size_t(2));
        QVERIFY(result.skippedContainers.empty());
    }

    // A changed form, which can be written, used by a written page and by a skipped one (owner
    // 2026-11-26): the form is not written, so the skipped page 2 is not among the changed pages
    // and looks as before; the page 1 is written with its own change (its second form use is
    // removed: "/Fm1 Do" stays of "/Fm1 Do /Fm2 Do")
    {
        const PDFContentContainerPointer page = buildPage(document, 0);
        const std::vector<PDFContentNodePointer> uses = nodesOfKind(*page, PDFContentNodeKind::FormUse);
        QCOMPARE(uses.size(), size_t(2));
        const PDFContentEditSnapshot validSnapshot = snapshotOf({ removeNode(original, brokenNode), removeNode(page, uses[1]->getId()) });
        Writer::Options options;
        options.skippedPages = { 1 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, validSnapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QCOMPARE(result.objectsRemoved, PDFInteger(1));
        QCOMPARE(result.skippedContainers.size(), size_t(1));
        QCOMPARE(result.skippedContainers.at(formKey), std::vector<PDFInteger>({ 1 }));

        const PDFDocument written = builder.build();
        QVERIFY(written.getObjectByReference(form1) == document.getObjectByReference(form1));
        const PDFObjectReference skippedPage = document.getCatalog()->getPage(1)->getPageReference();
        QVERIFY(written.getObjectByReference(skippedPage) == document.getObjectByReference(skippedPage));
        QCOMPARE(pageContent(written, 0), QByteArray("/Fm1 Do"));

        // Without the skipped page the same snapshot writes the form, and both pages change
        PDFDocumentBuilder allBuilder(&document);
        const PDFContentWriteResult allResult = Writer::write(&document, validSnapshot, &allBuilder, Writer::Options(), nullptr, nullptr);
        QVERIFY(allResult.status == Status::Success);
        QCOMPARE(allResult.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));
        QVERIFY(allResult.skippedContainers.empty());
        QCOMPARE(allResult.objectsRemoved, PDFInteger(2));
    }

    // A form, which no page draws: its error belongs to no page (the key -1); a change of it is
    // written and changes no page
    {
        PDFDocumentBuilder documentBuilder(&document);
        const PDFObject unusedForm = Documents::addFormObject(&documentBuilder);
        const PDFDocument withUnusedForm = documentBuilder.build();
        const PDFContentContainerPointer unused = buildContainer(withUnusedForm, PDFContentContainerKey::createForm(unusedForm.getReference()), 0);
        QVERIFY(unused != nullptr);
        const PDFContentNodeId unusedPath = nodesOfKind(*unused, PDFContentNodeKind::Path)[0]->getId();

        PDFDocumentBuilder builder(&withUnusedForm);
        PDFContentWriteResult result = Writer::write(&withUnusedForm, snapshotOf({ setOverride(unused, unusedPath, brokenOverride()) }), &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::PageErrors);
        QCOMPARE(result.errors.size(), size_t(1));
        QCOMPARE(result.errors.begin()->first, PDFInteger(-1));
        QVERIFY(result.errors.begin()->second[0].container == unused->getKey());

        result = Writer::write(&withUnusedForm, snapshotOf({ removeNode(unused, unusedPath) }), &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.pagesChanged.empty());
        QCOMPARE(result.objectsRemoved, PDFInteger(1));
        const PDFDocument written = builder.build();
        QCOMPARE(written.getDecodedStream(written.getObjectByReference(unusedForm.getReference()).getStream()), QByteArray("/GF gs 0 1 0 rg"));
    }
}

void DocumentToolContentTest::writerFormData()
{
    // A form, whose data have another filter and keys of their own (ISO 32000-2, 7.3.8.2, Table
    // 5): /Filter /ASCIIHexDecode, /DecodeParms, /DL (the length of the decoded data). The new
    // data are compressed by Flate: no key of the old data stays, every other key does.
    const QByteArray content("1 g 0 0 5 5 re f");
    PDFDocumentBuilder documentBuilder;
    const PDFObject form = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") },
                                                                          { "Subtype", PDFObject::createName("Form") },
                                                                          { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                          { "Filter", PDFObject::createName("ASCIIHexDecode") },
                                                                          { "DecodeParms", Documents::createDictionaryObject({ { "Unused", PDFObject::createInteger(1) } }) },
                                                                          { "DL", PDFObject::createInteger(content.size()) },
                                                                          { "Resources", Documents::createDictionaryObject({ }) } }, content.toHex() + ">");
    const PDFObjectReference page = documentBuilder.appendPage(QRectF(0, 0, 200, 200));
    documentBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", formResources({ { "Fm1", form } }) },
                                                                      { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), "/Fm1 Do") } }));
    const PDFDocument document = documentBuilder.build();
    QCOMPARE(document.getDecodedStream(document.getObjectByReference(form.getReference()).getStream()), content);

    // The path is removed: "1 g" stays
    const PDFContentContainerPointer original = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), 0);
    QVERIFY(original != nullptr);
    QVERIFY(!original->isReadOnly());
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[0]->getId());

    PDFDocumentBuilder builder(&document);
    QVERIFY(Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr).status == Status::Success);
    const PDFDocument written = builder.build();

    const PDFObject& stream = written.getObjectByReference(form.getReference());
    QVERIFY(stream.isStream());
    QCOMPARE(written.getDecodedStream(stream.getStream()), QByteArray("1 g"));

    const PDFDictionary* dictionary = stream.getStream()->getDictionary();
    QVERIFY(dictionary->get("Filter") == PDFObject::createName("FlateDecode"));
    QVERIFY(!dictionary->hasKey("DecodeParms"));
    QVERIFY(!dictionary->hasKey("DL"));
    QCOMPARE(dictionary->get("Length").getInteger(), PDFInteger(stream.getStream()->getContent()->size()));
    QVERIFY(dictionary->get("Type") == PDFObject::createName("XObject"));
    QVERIFY(dictionary->get("Subtype") == PDFObject::createName("Form"));
    QVERIFY(dictionary->get("BBox") == Documents::createIntegerArrayObject({ 0, 0, 10, 10 }));
    QVERIFY(dictionary->get("Resources").isDictionary());
    QCOMPARE(dictionary->getCount(), size_t(6));

    // ---- R-4 for forms: two forms share one indirect /Resources << /ExtGState << /GF >> >>. A
    //      changed form gets a dictionary of its own (with the added /GS1, when the alpha of its
    //      path is set); the shared dictionary and the other form stay as they were ----
    PDFDocumentBuilder sharedBuilder;
    const PDFObject graphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "LW", PDFObject::createInteger(4) } });
    const PDFObject sharedResources = Documents::addObject(&sharedBuilder, Documents::createDictionaryObject({ { "ExtGState", Documents::createDictionaryObject({ { "GF", graphicState } }) } }));
    PDFObject forms[2];
    for (PDFObject& sharedForm : forms)
    {
        sharedForm = Documents::addStreamObject(&sharedBuilder, { { "Type", PDFObject::createName("XObject") },
                                                                   { "Subtype", PDFObject::createName("Form") },
                                                                   { "BBox", Documents::createIntegerArrayObject({ 0, 0, 40, 30 }) },
                                                                   { "Resources", sharedResources } }, Documents::formContent());
    }
    const PDFObjectReference sharedPage = sharedBuilder.appendPage(QRectF(0, 0, 200, 200));
    sharedBuilder.mergeTo(sharedPage, Documents::createDictionaryObject({ { "Resources", formResources({ { "FmA", forms[0] }, { "FmB", forms[1] } }) },
                                                                          { "Contents", Documents::addStreamObject(&sharedBuilder, Documents::DictionaryEntries(), "/FmA Do /FmB Do") } }));
    const PDFDocument sharedDocument = sharedBuilder.build();

    const PDFContentContainerPointer sharedOriginal = buildContainer(sharedDocument, PDFContentContainerKey::createForm(forms[0].getReference()), 0);
    QVERIFY(sharedOriginal != nullptr);
    const PDFContentNodeId sharedPath = nodesOfKind(*sharedOriginal, PDFContentNodeKind::Path)[0]->getId();

    for (const bool isNameAdded : { true, false })
    {
        const PDFContentContainerPointer changed = isNameAdded ? setOverride(sharedOriginal, sharedPath, alphaOverride()) : removeNode(sharedOriginal, sharedPath);

        PDFDocumentBuilder writeBuilder(&sharedDocument);
        QVERIFY(Writer::write(&sharedDocument, snapshotOf({ changed }), &writeBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);
        const PDFDocument sharedWritten = writeBuilder.build();

        PDFObject expectedStates = Documents::createDictionaryObject({ { "GF", graphicState } });
        if (isNameAdded)
        {
            expectedStates = Documents::createDictionaryObject({ { "GF", graphicState },
                                                                 { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }) } });
        }

        const PDFDictionary* changedDictionary = sharedWritten.getObjectByReference(forms[0].getReference()).getStream()->getDictionary();
        QVERIFY(changedDictionary->get("Resources").isDictionary());
        QVERIFY(changedDictionary->get("Resources") == Documents::createDictionaryObject({ { "ExtGState", expectedStates } }));

        QVERIFY(sharedWritten.getObjectByReference(sharedResources.getReference()) == sharedDocument.getObjectByReference(sharedResources.getReference()));
        QVERIFY(sharedWritten.getObjectByReference(forms[1].getReference()) == sharedDocument.getObjectByReference(forms[1].getReference()));
        QVERIFY(sharedWritten.getObjectByReference(forms[1].getReference()).getStream()->getDictionary()->get("Resources") == sharedResources);
    }
}

void DocumentToolContentTest::writerFormWithoutResources()
{
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);

    // /Fm2 has no /Resources: the name /GP of its content "/GP gs 1 0 0 rg 0 0 10 10 re f" is a
    // resource of the page, which draws it (the page 1)
    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(form2);
    const PDFContentContainerPointer original = buildContainer(document, formKey, 0);
    QVERIFY(original != nullptr);
    QVERIFY(!document.getObjectByReference(form2).getStream()->getDictionary()->hasKey("Resources"));
    const PDFContentNodeId path = nodesOfKind(*original, PDFContentNodeKind::Path)[0]->getId();

    // ---- A change, which adds no name: the form still has no resources of its own ----
    {
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ removeNode(original, path) }), &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));

        const PDFDocument written = builder.build();
        const PDFObject& stream = written.getObjectByReference(form2);
        QCOMPARE(written.getDecodedStream(stream.getStream()), QByteArray("/GP gs 1 0 0 rg"));
        QVERIFY(!stream.getStream()->getDictionary()->hasKey("Resources"));
    }

    // ---- A change, which adds a name (the alpha 0.5 needs /GS1): R-4 - the form gets a copy of
    //      the resources it has inherited from its page, with the new name in a copy of the
    //      category; the names its content uses (/GP) still resolve ----
    {
        const PDFContentContainerPointer edited = setOverride(original, path, alphaOverride());

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);

        const PDFDocument written = builder.build();
        const PDFDictionary* dictionary = written.getObjectByReference(form2).getStream()->getDictionary();
        const PDFDictionary* pageResources = pageDictionary(document, 0)->get("Resources").getDictionary();

        QVERIFY(dictionary->get("Resources").isDictionary());
        const PDFDictionary* resources = dictionary->get("Resources").getDictionary();
        QCOMPARE(resources->getCount(), size_t(2));
        QVERIFY(resources->get("XObject") == pageResources->get("XObject"));
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) },
                                                                                   { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }) } }));

        // The resources of the page itself are not changed
        const PDFObjectReference pageReference = document.getCatalog()->getPage(0)->getPageReference();
        QVERIFY(written.getObjectByReference(pageReference) == document.getObjectByReference(pageReference));

        // Read back: the form has both names now and no name without a definition
        const PDFContentContainerPointer readBack = buildContainer(written, formKey, 0);
        QCOMPARE(readBack->getResources().getEntries(PDFContentResourceCategory::ExtGState).size(), size_t(2));
        QVERIFY(readBack->getResources().getUndefinedNames(PDFContentResourceCategory::ExtGState).empty());
    }

    // ---- Two pages draw the same form without /Resources (ISO 32000-2, 7.8.3: on each page its
    //      names are those of that page). Its container is built from the second page and gets
    //      an added name. A copy of the resources of one page would take the place of the
    //      resources of both, so it is right only when both pages give the names the form uses
    //      (/GP of "/GP gs 1 0 0 rg 0 0 10 10 re f") the same object. ----
    const PDFObject addedGraphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });

    // Builds two pages "/Fm2 Do" with the given /ExtGState dictionaries, the form edited from
    // the page of the given index, and writes it
    struct TwoPages
    {
        PDFDocument document;
        PDFObject form;
        PDFContentWriteResult result;
        PDFDocument written;

        /// The builder is compared before build(): build() writes /ModDate (seconds) into the
        /// information dictionary, a later second would differ from the source document
        bool isBuilderUntouched = false;
    };
    const auto writeTwoPages = [&](const PDFObject& states1, const PDFObject& states2, PDFInteger builtFromPage, TwoPages* data)
    {
        PDFDocumentBuilder twoPagesBuilder;
        data->form = Documents::addFormWithoutResourcesObject(&twoPagesBuilder);
        const PDFObject pageResources[2] = { Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", data->form } }) }, { "ExtGState", states1 } }),
                                             Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", data->form } }) }, { "ExtGState", states2 },
                                                                                 { "ProcSet", Documents::createArrayObject({ PDFObject::createName("PDF") }) } }) };
        for (const PDFObject& resources : pageResources)
        {
            const PDFObjectReference page = twoPagesBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            twoPagesBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", resources },
                                                                              { "Contents", Documents::addStreamObject(&twoPagesBuilder, Documents::DictionaryEntries(), "/Fm2 Do") } }));
        }
        data->document = twoPagesBuilder.build();

        const PDFContentContainerPointer sharedForm = buildContainer(data->document, PDFContentContainerKey::createForm(data->form.getReference()), builtFromPage);
        QVERIFY(sharedForm != nullptr);
        QCOMPARE(sharedForm->getInheritedResourcesPageIndex(), builtFromPage);
        const PDFContentContainerPointer edited = setOverride(sharedForm, nodesOfKind(*sharedForm, PDFContentNodeKind::Path)[0]->getId(), alphaOverride());
        QVERIFY(edited != nullptr);

        PDFDocumentBuilder builder(&data->document);
        data->result = Writer::write(&data->document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
        data->isBuilderUntouched = *builder.getStorage() == data->document.getStorage();
        QVERIFY(data->isBuilderUntouched || data->result.isSuccess());
        data->written = builder.build();
    };

    // The pages disagree about /GP (the line width 6 on the page 1, 3 on the page 2): the form
    // is refused with an error under both pages, which names the other page (the page 1) and
    // the page it was read with (the page 2); nothing is written
    {
        TwoPages data;
        writeTwoPages(Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }),
                      Documents::createDictionaryObject({ { "GP", graphicStateObject(3.0) }, { "GQ", graphicStateObject(9.0) } }), 1, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(data.result.status == Status::PageErrors);
        QCOMPARE(data.result.errors.size(), size_t(2));
        for (const PDFInteger pageIndex : { 0, 1 })
        {
            const std::vector<PDFContentWriteError>& errors = data.result.errors.at(pageIndex);
            QCOMPARE(errors.size(), size_t(1));
            QVERIFY(errors[0].container == PDFContentContainerKey::createForm(data.form.getReference()));
            QVERIFY(!errors[0].nodeId.isValid());
            QVERIFY2(errors[0].text.contains(QStringLiteral("pages 1 give it other resources than the page 2")), qPrintable(errors[0].text));
        }
        QVERIFY(data.result.pagesChanged.empty());
        QVERIFY(data.isBuilderUntouched);
    }

    // The pages agree about /GP - two equal dictionaries, the page 2 has one more name the form
    // does not use (/GQ): the form gets a copy of the resources of the page it was read with
    // (the page 2: its /GQ and its /ProcSet show it), with the new name in a copy of the category
    {
        TwoPages data;
        writeTwoPages(Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }),
                      Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GQ", graphicStateObject(9.0) } }), 1, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(data.result.status == Status::Success);
        QCOMPARE(data.result.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));

        const PDFDictionary* dictionary = data.written.getObjectByReference(data.form.getReference()).getStream()->getDictionary();
        QVERIFY(dictionary->get("Resources").isDictionary());
        const PDFDictionary* resources = dictionary->get("Resources").getDictionary();
        QCOMPARE(resources->getCount(), size_t(3));
        QVERIFY(resources->get("XObject") == Documents::createDictionaryObject({ { "Fm2", data.form } }));
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GQ", graphicStateObject(9.0) }, { "GS1", addedGraphicState } }));
        QVERIFY(resources->get("ProcSet") == Documents::createArrayObject({ PDFObject::createName("PDF") }));

        // The resources of both pages are not changed
        for (PDFInteger pageIndex = 0; pageIndex < 2; ++pageIndex)
        {
            const PDFObjectReference pageReference = data.document.getCatalog()->getPage(size_t(pageIndex))->getPageReference();
            QVERIFY(data.written.getObjectByReference(pageReference) == data.document.getObjectByReference(pageReference));
        }
    }

    // The same from the page 1: the copy is the one of the page 1 (no /GQ, no /ProcSet)
    {
        TwoPages data;
        writeTwoPages(Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }),
                      Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GQ", graphicStateObject(9.0) } }), 0, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(data.result.status == Status::Success);
        const PDFDictionary* resources = data.written.getObjectByReference(data.form.getReference()).getStream()->getDictionary()->get("Resources").getDictionary();
        QCOMPARE(resources->getCount(), size_t(2));
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", addedGraphicState } }));
    }

    // "The same object" is the same value, or equal objects behind two references: the page 1
    // has the state of /GP in its dictionary, the page 2 in an object of its own - what the form
    // draws with is the same
    {
        PDFDocumentBuilder twoPagesBuilder;
        const PDFObject sharedForm = Documents::addFormWithoutResourcesObject(&twoPagesBuilder);
        const PDFObject indirectState = Documents::addObject(&twoPagesBuilder, graphicStateObject(6.0));
        for (const PDFObject& state : { graphicStateObject(6.0), indirectState })
        {
            const PDFObjectReference page = twoPagesBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            twoPagesBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", sharedForm } }) },
                                                                                                                                    { "ExtGState", Documents::createDictionaryObject({ { "GP", state } }) } }) },
                                                                              { "Contents", Documents::addStreamObject(&twoPagesBuilder, Documents::DictionaryEntries(), "/Fm2 Do") } }));
        }
        const PDFDocument twoPagesDocument = twoPagesBuilder.build();

        const PDFContentContainerPointer sharedContainer = buildContainer(twoPagesDocument, PDFContentContainerKey::createForm(sharedForm.getReference()), 1);
        const PDFContentContainerPointer edited = setOverride(sharedContainer, nodesOfKind(*sharedContainer, PDFContentNodeKind::Path)[0]->getId(), alphaOverride());
        PDFDocumentBuilder builder(&twoPagesDocument);
        QVERIFY(Writer::write(&twoPagesDocument, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr).status == Status::Success);
        QVERIFY(builder.getObjectByReference(sharedForm.getReference()).getStream()->getDictionary()->get("Resources").getDictionary()->get("ExtGState") ==
                Documents::createDictionaryObject({ { "GP", indirectState }, { "GS1", addedGraphicState } }));
    }

    // The new name of the form (/GS1) is a name of the page 2 and no name of the page 1. The
    // form defines it itself, so what the pages give it does not matter: they agree in the
    // one name the form takes from them (/GP).
    {
        TwoPages data;
        writeTwoPages(Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }),
                      Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", graphicStateObject(9.0) } }), 0, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(data.result.status == Status::Success);
        const PDFDictionary* resources = data.written.getObjectByReference(data.form.getReference()).getStream()->getDictionary()->get("Resources").getDictionary();
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", addedGraphicState } }));
    }

    // The same with an operation of the form, which uses the new name (as after a copy of an
    // object with its resource into the form): "/GS1 gs" is added to the content, /GS1 to the
    // resources of the form. The pages still need not agree about /GS1.
    {
        PDFDocumentBuilder twoPagesBuilder;
        const PDFObject sharedForm = Documents::addFormWithoutResourcesObject(&twoPagesBuilder);
        for (const PDFObject& states : { Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }),
                                         Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", graphicStateObject(9.0) } }) })
        {
            const PDFObjectReference page = twoPagesBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            twoPagesBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", sharedForm } }) }, { "ExtGState", states } }) },
                                                                              { "Contents", Documents::addStreamObject(&twoPagesBuilder, Documents::DictionaryEntries(), "/Fm2 Do") } }));
        }
        const PDFDocument twoPagesDocument = twoPagesBuilder.build();

        const PDFContentContainerPointer sharedContainer = buildContainer(twoPagesDocument, PDFContentContainerKey::createForm(sharedForm.getReference()), 0);
        PDFContentContainerEditor editor(sharedContainer);
        QCOMPARE(editor.getResources().addObject(PDFContentResourceCategory::ExtGState, "GS", addedGraphicState), QByteArray("GS1"));

        PDFContentNode::Data data;
        data.kind = PDFContentNodeKind::StateRun;
        data.id = PDFContentNodeId{ FIRST_NEW_NODE };
        data.prefix.first = editor.getOperations().append(PDFContentOperation::createGenerated(PDFContentOperator::SetGraphicState, "/GS1 gs", PDFContentOperation::Operands(),
                                                                                              { PDFContentResourceName{ PDFContentResourceCategory::ExtGState, QByteArray("GS1") } }));
        data.prefix.count = 1;
        const PDFContentNodeId rootId = sharedContainer->getRoot()->getId();
        QVERIFY(editor.insertNode(std::move(data), rootId, sharedContainer->getAppendIndex(rootId)));

        PDFDocumentBuilder builder(&twoPagesDocument);
        QVERIFY(Writer::write(&twoPagesDocument, snapshotOf({ editor.build() }), &builder, Writer::Options(), nullptr, nullptr).status == Status::Success);
        const PDFDocument written = builder.build();
        const PDFObject& stream = written.getObjectByReference(sharedForm.getReference());
        QVERIFY(stream.getStream()->getDictionary()->get("Resources").getDictionary()->get("ExtGState") == Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", addedGraphicState } }));
        QCOMPARE(written.getDecodedStream(stream.getStream()), Documents::formWithoutResourcesContent() + "\n/GS1 gs");
    }

    // One page defines /GP, the other one does not: they disagree too
    {
        TwoPages data;
        writeTwoPages(Documents::createDictionaryObject({ { "GX", graphicStateObject(6.0) } }),
                      Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }), 1, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(data.result.status == Status::PageErrors);
        QCOMPARE(data.result.errors.size(), size_t(2));
        QVERIFY(data.isBuilderUntouched);
    }
}

void DocumentToolContentTest::writerFormResourcesNull_data()
{
    QTest::addColumn<int>("variant");

    QTest::newRow("null") << 0;
    QTest::newRow("a reference to a missing object") << 1;
    QTest::newRow("a number") << 2;
}

void DocumentToolContentTest::writerFormResourcesNull()
{
    QFETCH(int, variant);

    // ISO 32000-2, 7.3.7: "A dictionary entry whose value is null shall be treated the same as if
    // the entry does not exist"; 7.3.10: an indirect reference to an undefined object is the null
    // object. A form with such a /Resources has no resources of its own: its names are those of
    // the page (7.8.3) - here /GP of the page and the form /Fm1 itself.
    //   page: "/Fm1 Do", /XObject << /Fm1 >> /ExtGState << /GP << /LW 6 >> >>
    //   /Fm1: "/GP gs 1 0 0 rg 0 0 10 10 re f", /Resources null or 999 0 R
    // The third row is a value, which is neither a dictionary nor nothing (the number 5): the
    // standard does not say, what it means; this library gives such a form no resources at all.
    // The entry is nothing the user touched: it stays as it is.
    const bool isInherited = variant != 2;
    const PDFObject noResources = variant == 0 ? PDFObject::createNull() : (variant == 1 ? PDFObject::createReference(PDFObjectReference(999, 0)) : PDFObject::createInteger(5));

    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference formReference = documentBuilder.addObject(PDFObject());
    {
        // setObject keeps the entry with the null value (addObject would drop it)
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
        dictionary.addEntry(PDFInplaceOrMemoryString("BBox"), Documents::createIntegerArrayObject({ 0, 0, 10, 10 }));
        dictionary.addEntry(PDFInplaceOrMemoryString("Resources"), noResources);
        documentBuilder.setObject(formReference, PDFObject::createStream(PDFStream(std::move(dictionary), Documents::formWithoutResourcesContent())));
    }
    const PDFObject form = PDFObject::createReference(formReference);
    const PDFObjectReference pageReference = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    documentBuilder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", form } }) },
                                                                                                                                     { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }) } }) },
                                                                               { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), "/Fm1 Do") } }));
    const PDFDocument document = documentBuilder.build();

    // The document really has the entry (the test would prove nothing otherwise)
    const PDFDictionary* originalDictionary = document.getObjectByReference(formReference).getStream()->getDictionary();
    QVERIFY(originalDictionary->hasKey("Resources"));
    QVERIFY(originalDictionary->get("Resources") == noResources);
    QCOMPARE(document.getObject(originalDictionary->get("Resources")).isNull(), isInherited);
    QVERIFY(PDFInteger(document.getStorage().getObjects().size()) < 999);

    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(formReference);
    const PDFContentContainerPointer original = buildContainer(document, formKey, 0);
    QVERIFY(original != nullptr);
    const std::vector<PDFContentNodePointer> paths = nodesOfKind(*original, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(1));

    // ---- A change, which adds no name (the path is removed): the form must not get an empty
    //      resource dictionary of its own - /GP would be undefined on every page drawing it. It
    //      is written without /Resources. ----
    {
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ removeNode(original, paths[0]->getId()) }), &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));

        const PDFDocument written = builder.build();
        const PDFObject& stream = written.getObjectByReference(formReference);
        QVERIFY(stream.isStream());
        QCOMPARE(written.getDecodedStream(stream.getStream()), QByteArray("/GP gs 1 0 0 rg"));

        const PDFDictionary* dictionary = stream.getStream()->getDictionary();
        if (isInherited)
        {
            // /Type, /Subtype, /BBox, /Length, /Filter and nothing else
            QVERIFY(!dictionary->hasKey("Resources"));
            QCOMPARE(dictionary->getCount(), size_t(5));
        }
        else
        {
            QVERIFY(dictionary->get("Resources") == noResources);
            QCOMPARE(dictionary->getCount(), size_t(6));
        }
    }

    // ---- The form is written again without any change (a forced rewrite): the same bytes, and
    //      the name /GP of the form is still the graphic state of the page - read back, the path
    //      of the form is drawn with the line width 6 (ISO 32000-2, Table 57: /LW; with an empty
    //      resource dictionary of its own the name would be undefined and the width 1) ----
    if (isInherited)
    {
        Writer::Options options;
        options.forceRewrite = true;

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ original }), &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);

        const PDFDocument written = builder.build();
        const PDFObject& stream = written.getObjectByReference(formReference);
        QCOMPARE(written.getDecodedStream(stream.getStream()), Documents::formWithoutResourcesContent());
        QVERIFY(!stream.getStream()->getDictionary()->hasKey("Resources"));

        const PDFContentContainerPointer readBack = buildContainer(written, formKey, 0);
        QVERIFY(readBack != nullptr);
        QCOMPARE(readBack->getInheritedResourcesPageIndex(), PDFInteger(0));
        const std::vector<PDFContentNodePointer> readBackPaths = nodesOfKind(*readBack, PDFContentNodeKind::Path);
        QCOMPARE(readBackPaths.size(), size_t(1));
        QVERIFY(readBackPaths[0]->getState() != nullptr);
        QCOMPARE(readBackPaths[0]->getState()->getState().getLineWidth(), 6.0);
    }

    // ---- A change, which adds a name (the alpha 0.5 needs /GS1) ----
    const PDFContentContainerPointer withAlpha = setOverride(original, paths[0]->getId(), alphaOverride());
    QVERIFY(withAlpha != nullptr);
    const PDFObject addedGraphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ withAlpha }), &builder, Writer::Options(), nullptr, nullptr);
    const PDFObject expectedInherited = Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", form } }) },
                                                                            { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", addedGraphicState } }) } });
    switch (variant)
    {
        case 0:
        case 1:
        {
            // The form inherits: it gets a copy of the resources of its page with the new name
            QVERIFY(result.status == Status::Success);
            const PDFDocument written = builder.build();
            QVERIFY(written.getObjectByReference(formReference).getStream()->getDictionary()->get("Resources") == expectedInherited);
            break;
        }

        default:
        {
            // The form has no resources and does not inherit: its new dictionary has the new name only
            QVERIFY(result.status == Status::Success);
            const PDFDocument written = builder.build();
            QVERIFY(written.getObjectByReference(formReference).getStream()->getDictionary()->get("Resources") ==
                    Documents::createDictionaryObject({ { "ExtGState", Documents::createDictionaryObject({ { "GS1", addedGraphicState } }) } }));
            break;
        }
    }
}

void DocumentToolContentTest::writerFormInheritedContexts()
{
    // A form without /Resources, which gets a name (the alpha 0.5 of its path needs /GS1), gets a
    // copy of the resources it was read with. The copy takes the place of what the form was drawn
    // with before at every place drawing it: the resources of the content drawing it, and a name
    // that content does not define from the page (NOTES.txt 7.8.3). Each case below is one place,
    // where the copy would change, what somebody sees; the writer refuses it there, and writes
    // it, where nothing changes.
    const PDFObject addedGraphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });

    const auto addForm = [](PDFDocumentBuilder* builder, const QByteArray& content, Documents::DictionaryEntries resources, bool hasResources)
    {
        Documents::DictionaryEntries entries = { { "Type", PDFObject::createName("XObject") },
                                                 { "Subtype", PDFObject::createName("Form") },
                                                 { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } };
        if (hasResources)
        {
            entries.push_back({ "Resources", Documents::createDictionaryObject(std::move(resources)) });
        }
        return Documents::addStreamObject(builder, std::move(entries), content);
    };

    const auto addPage = [](PDFDocumentBuilder* builder, const QByteArray& content, Documents::DictionaryEntries resources)
    {
        const PDFObjectReference page = builder->appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder->mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject(std::move(resources)) },
                                                                   { "Contents", Documents::addStreamObject(builder, Documents::DictionaryEntries(), content) } }));
    };

    // Edits the form from a page (the alpha of its only path) and writes it
    struct Written
    {
        PDFContentWriteResult result;
        bool isBuilderUntouched = false;
        PDFObject resources;    ///< /Resources of the written form
    };
    const auto writeForm = [](const PDFDocument& document, const PDFObject& form, PDFInteger builtFromPage, Written* written)
    {
        const PDFContentContainerPointer container = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), builtFromPage);
        QVERIFY(container != nullptr);
        QCOMPARE(container->getInheritedResourcesPageIndex(), builtFromPage);
        const std::vector<PDFContentNodePointer> paths = nodesOfKind(*container, PDFContentNodeKind::Path);
        QCOMPARE(paths.size(), size_t(1));
        const PDFContentContainerPointer edited = setOverride(container, paths[0]->getId(), alphaOverride());
        QVERIFY(edited != nullptr);

        PDFDocumentBuilder builder(&document);
        written->result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
        written->isBuilderUntouched = *builder.getStorage() == document.getStorage();
        written->resources = builder.getObjectByReference(form.getReference()).getStream()->getDictionary()->get("Resources");
    };

    const auto formErrorText = [](const Written& written, PDFInteger pageIndex)
    {
        auto it = written.result.errors.find(pageIndex);
        return (it != written.result.errors.cend() && it->second.size() == 1) ? it->second[0].text : QString();
    };

    // ---- 1. The page, whose resources the container was read with, does not draw the form (the
    //      form /Fm2 of the document of the forms is drawn by the page 1 only; here it is read
    //      with the page 2): its states and its names are not those the document draws it with ----
    {
        PDFObjectReference form1;
        PDFObjectReference form2;
        const PDFDocument document = createFormsDocument(&form1, &form2);

        Written written;
        writeForm(document, PDFObject::createReference(form2), 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(written.result.status == Status::PageErrors);
        QCOMPARE(written.result.errors.size(), size_t(1));
        QVERIFY2(formErrorText(written, 0).contains(QStringLiteral("does not draw it")), qPrintable(formErrorText(written, 0)));
        QVERIFY(written.isBuilderUntouched);
    }

    // ---- 2. The form draws another form without /Resources. Two pages draw it:
    //        /Fm2 "/Fm3 Do 0 0 10 10 re f"    (edited)        /Fm3 "/GQ gs 0 0 5 5 re f"
    //        page 1, page 2: "/Fm2 Do", /XObject << /Fm2 /Fm3 >> /ExtGState << /GQ << /LW ... >> >>
    //      /Fm2 itself uses the name /Fm3 only, and both pages give it the same form. /Fm3, drawn
    //      by /Fm2, takes its names from /Fm2 first (NOTES.txt 7.8.3): today /Fm2 inherits them
    //      from the page drawing it, after the write it has the copy of the page 2 - the page 1
    //      would draw /Fm3 with the /GQ of the page 2. Nobody knows here, which names /Fm3 uses,
    //      so every name of the page 2 must be the same object on the page 1. ----
    const auto nestedDocument = [&](PDFReal lineWidth1, PDFReal lineWidth2, PDFObject* outerForm, PDFObject* innerForm)
    {
        PDFDocumentBuilder builder;
        *innerForm = addForm(&builder, "/GQ gs 0 0 5 5 re f", { }, false);
        *outerForm = addForm(&builder, "/Fm3 Do 0 0 10 10 re f", { }, false);
        for (const PDFReal lineWidth : { lineWidth1, lineWidth2 })
        {
            // A line width 0: the page has no /ExtGState
            Documents::DictionaryEntries resources = { { "XObject", Documents::createDictionaryObject({ { "Fm2", *outerForm }, { "Fm3", *innerForm } }) } };
            if (lineWidth > 0.0)
            {
                resources.push_back({ "ExtGState", Documents::createDictionaryObject({ { "GQ", graphicStateObject(lineWidth) } }) });
            }
            addPage(&builder, "/Fm2 Do", std::move(resources));
        }
        return builder.build();
    };

    {
        // The pages give /GQ other values: /Fm3 would take the /GQ of the page 2 from the copy
        // on the page 1 too - refused under both pages
        PDFObject outerForm;
        PDFObject innerForm;
        const PDFDocument document = nestedDocument(6.0, 3.0, &outerForm, &innerForm);

        Written written;
        writeForm(document, outerForm, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(written.result.status == Status::PageErrors);
        QCOMPARE(written.result.errors.size(), size_t(2));
        QVERIFY2(formErrorText(written, 0).contains(QStringLiteral("pages 1 give it other resources than the page 2")), qPrintable(formErrorText(written, 0)));
        QCOMPARE(formErrorText(written, 1), formErrorText(written, 0));
        QVERIFY(written.isBuilderUntouched);
    }

    {
        // The page 1 has no /GQ: /Fm3 would take the /GQ of the page 2 from the copy there too
        PDFObject outerForm;
        PDFObject innerForm;
        const PDFDocument document = nestedDocument(0.0, 3.0, &outerForm, &innerForm);

        Written written;
        writeForm(document, outerForm, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(written.result.status == Status::PageErrors);
        QCOMPARE(written.result.errors.size(), size_t(2));
        QVERIFY2(formErrorText(written, 0).contains(QStringLiteral("pages 1 give it other resources than the page 2")), qPrintable(formErrorText(written, 0)));
        QCOMPARE(formErrorText(written, 1), formErrorText(written, 0));
        QVERIFY(written.isBuilderUntouched);
    }

    // The pages agree in every name (two equal dictionaries): the copy changes nothing
    {
        PDFObject outerForm;
        PDFObject innerForm;
        const PDFDocument document = nestedDocument(6.0, 6.0, &outerForm, &innerForm);

        Written written;
        writeForm(document, outerForm, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(written.result.status == Status::Success);
        QVERIFY(written.resources == Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", outerForm }, { "Fm3", innerForm } }) },
                                                                         { "ExtGState", Documents::createDictionaryObject({ { "GQ", graphicStateObject(6.0) }, { "GS1", addedGraphicState } }) } }));
    }

    {
        // The same, the page 1 has one more name /GS1, which the page 2 (read with) does not
        // have: the new state gets /GS1 there, and /Fm3 would take it from the copy on the page 1
        // in place of the /GS1 of the page 1 - refused
        PDFDocumentBuilder builder;
        const PDFObject innerForm = addForm(&builder, "/GQ gs 0 0 5 5 re f", { }, false);
        const PDFObject outerForm = addForm(&builder, "/Fm3 Do 0 0 10 10 re f", { }, false);
        for (int pageIndex = 0; pageIndex < 2; ++pageIndex)
        {
            Documents::DictionaryEntries states = { { "GQ", graphicStateObject(6.0) } };
            if (pageIndex == 0)
            {
                states.push_back({ "GS1", graphicStateObject(9.0) });
            }
            addPage(&builder, "/Fm2 Do", { { "XObject", Documents::createDictionaryObject({ { "Fm2", outerForm }, { "Fm3", innerForm } }) },
                                           { "ExtGState", Documents::createDictionaryObject(std::move(states)) } });
        }
        const PDFDocument document = builder.build();

        Written written;
        writeForm(document, outerForm, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(written.result.status == Status::PageErrors);
        QCOMPARE(written.result.errors.size(), size_t(2));
        QVERIFY2(formErrorText(written, 0).contains(QStringLiteral("pages 1 give it other resources than the page 2")), qPrintable(formErrorText(written, 0)));
        QVERIFY(written.isBuilderUntouched);
    }

    // ---- 3. The page defines /GS1, which the content of the form does not use. The tree builder
    //      reserves every name of the resources a form without resources of its own was read
    //      with (the copy holds them all), so the new state gets /GS2 and the copy keeps the /GS1
    //      of the page. A form inside, which has no resources either, takes /GS1 from /Fm2 first
    //      (NOTES.txt 7.8.3) - from the copy after the write, where it is still the state of the
    //      page. ----
    {
        // One page; /Fm2 draws no other content
        PDFDocumentBuilder builder;
        const PDFObject form = Documents::addFormWithoutResourcesObject(&builder);
        addPage(&builder, "/Fm2 Do", { { "XObject", Documents::createDictionaryObject({ { "Fm2", form } }) },
                                       { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", graphicStateObject(9.0) } }) } });
        const PDFDocument document = builder.build();

        Written written;
        writeForm(document, form, 0, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY(written.result.status == Status::Success);
        QVERIFY(written.resources == Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", form } }) },
                                                                         { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", graphicStateObject(9.0) },
                                                                                                                             { "GS2", addedGraphicState } }) } }));
    }

    {
        // One page; /Fm2 draws /Fm3, which has no resources
        PDFDocumentBuilder builder;
        const PDFObject innerForm = addForm(&builder, "/GS1 gs 0 0 5 5 re f", { }, false);
        const PDFObject outerForm = addForm(&builder, "/Fm3 Do 0 0 10 10 re f", { }, false);
        addPage(&builder, "/Fm2 Do", { { "XObject", Documents::createDictionaryObject({ { "Fm2", outerForm }, { "Fm3", innerForm } }) },
                                       { "ExtGState", Documents::createDictionaryObject({ { "GS1", graphicStateObject(9.0) } }) } });
        const PDFDocument document = builder.build();

        Written written;
        writeForm(document, outerForm, 0, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }

        QVERIFY2(written.result.status == Status::Success, qPrintable(formErrorText(written, 0)));
        QVERIFY(written.resources == Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", outerForm }, { "Fm3", innerForm } }) },
                                                                         { "ExtGState", Documents::createDictionaryObject({ { "GS1", graphicStateObject(9.0) }, { "GS2", addedGraphicState } }) } }));

        // An added name, which is a name of the resources of the build (here forced: the reserved
        // names are cleared, so the allocator gives /GS1), would hide the /GS1 of the page from
        // /Fm3 in the copy: refused
        const PDFContentContainerPointer container = buildContainer(document, PDFContentContainerKey::createForm(outerForm.getReference()), 0);
        PDFContentContainerEditor editor(container);
        editor.getResources().setReservedNames(PDFContentResourceCategory::ExtGState, std::set<QByteArray>());
        QCOMPARE(editor.getResources().addObject(PDFContentResourceCategory::ExtGState, "GS", addedGraphicState), QByteArray("GS1"));
        PDFDocumentBuilder hidingBuilder(&document);
        const PDFContentWriteResult hiding = Writer::write(&document, snapshotOf({ editor.build() }), &hidingBuilder, Writer::Options(), nullptr, nullptr);
        QVERIFY(hiding.status == Status::PageErrors);
        QVERIFY2(hiding.errors.at(0).front().text.contains(QStringLiteral("the new resource “GS1” would hide a resource of the same name")), qPrintable(hiding.errors.at(0).front().text));
        QVERIFY(*hidingBuilder.getStorage() == document.getStorage());
    }

    // ---- 4. The form is drawn by another form, which has resources of its own:
    //        page "/FmH Do", /XObject << /FmH /Fx >> /ExtGState << /GP << /LW 6 >> >>
    //        /FmH "/Fm2 Do", /Resources << /XObject << /Fm2 >> /ExtGState << /GP << /LW w >> [/GX << /LW 2 >>] >> >>
    //        /Fm2 "/GP gs 1 0 0 rg 0 0 10 10 re f" (or "/GX gs ...", "/Fx Do ..."), no /Resources
    //        /Fx  "0 0 1 1 re f", no /Resources
    //      /Fm2 takes its names from /FmH first, a name /FmH lacks (/Fx, /FmH) from the page
    //      (NOTES.txt 7.8.3). Read as the session reads it (the caller resources of the build are
    //      those of /FmH), the tree has the objects of /FmH and the copy holds them: written. Read
    //      with the page alone, the tree has the objects of the page: written only where /FmH
    //      gives the used names the same objects (w = 6), else refused. The copy: the dictionaries
    //      of /FmH with the entries of the page /FmH lacks behind them, and the new state. ----
    const auto outerFormDocument = [&](PDFReal outerLineWidth, const QByteArray& content, bool hasExtra, PDFObject* form, PDFObject* outerForm, PDFObject* innerForm)
    {
        PDFDocumentBuilder builder;
        *form = addForm(&builder, content, { }, false);
        *innerForm = addForm(&builder, "0 0 1 1 re f", { }, false);
        Documents::DictionaryEntries states = { { "GP", graphicStateObject(outerLineWidth) } };
        if (hasExtra)
        {
            states.push_back({ "GX", graphicStateObject(2.0) });
        }
        *outerForm = addForm(&builder, "/Fm2 Do", { { "XObject", Documents::createDictionaryObject({ { "Fm2", *form } }) },
                                                  { "ExtGState", Documents::createDictionaryObject(std::move(states)) } }, true);
        addPage(&builder, "/FmH Do", { { "XObject", Documents::createDictionaryObject({ { "FmH", *outerForm }, { "Fx", *innerForm } }) },
                                       { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) } }) } });
        return builder.build();
    };

    struct OuterRow
    {
        const char* name;
        QByteArray content;
        bool hasExtra;
        PDFReal outerLineWidth;
        bool isReadWithCaller;
        bool isRefused;
    };
    for (const OuterRow& row : { OuterRow{ "read with the outer form: its /GP", QByteArray("/GP gs 1 0 0 rg 0 0 10 10 re f"), false, 3.0, true, false },
                                 OuterRow{ "read with the outer form: a name only of the outer form", QByteArray("/GX gs 1 0 0 rg 0 0 10 10 re f"), true, 3.0, true, false },
                                 OuterRow{ "read with the outer form: a form without resources inside", QByteArray("/Fx Do 1 0 0 rg 0 0 10 10 re f"), false, 3.0, true, false },
                                 OuterRow{ "read with the page: the outer form gives /GP another object", QByteArray("/GP gs 1 0 0 rg 0 0 10 10 re f"), false, 3.0, false, true },
                                 OuterRow{ "read with the page: a name only of the outer form", QByteArray("/GX gs 1 0 0 rg 0 0 10 10 re f"), true, 3.0, false, true },
                                 OuterRow{ "read with the page: the outer form gives /GP an equal object", QByteArray("/GP gs 1 0 0 rg 0 0 10 10 re f"), false, 6.0, false, false },
                                 // /Fx would take a name from the copy of the page first, where
                                 // today it has those of /FmH: /FmH defines /Fm2, the page does not
                                 OuterRow{ "read with the page: a form without resources inside", QByteArray("/Fx Do 1 0 0 rg 0 0 10 10 re f"), false, 6.0, false, true } })
    {
        PDFObject form;
        PDFObject outerForm;
        PDFObject innerForm;
        const PDFDocument document = outerFormDocument(row.outerLineWidth, row.content, row.hasExtra, &form, &outerForm, &innerForm);
        const PDFObject outerResources = document.getObject(outerForm).getStream()->getDictionary()->get("Resources");

        const PDFContentContainerPointer container = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), 0, row.isReadWithCaller ? outerResources : PDFObject());
        QVERIFY2(container != nullptr, row.name);
        const PDFContentContainerPointer edited = setOverride(container, nodesOfKind(*container, PDFContentNodeKind::Path).back()->getId(), alphaOverride());
        QVERIFY2(edited != nullptr, row.name);

        Written written;
        PDFDocumentBuilder builder(&document);
        written.result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
        written.isBuilderUntouched = *builder.getStorage() == document.getStorage();
        written.resources = builder.getObjectByReference(form.getReference()).getStream()->getDictionary()->get("Resources");

        if (row.isRefused)
        {
            QVERIFY2(written.result.status == Status::PageErrors, row.name);
            QVERIFY2(written.result.errors.size() == 1, row.name);
            QVERIFY2(formErrorText(written, 0).contains(QStringLiteral("a form or an annotation of the page 1, which draws it, gives it other resources than those it was read with")), qPrintable(formErrorText(written, 0)));
            QVERIFY2(written.isBuilderUntouched, row.name);
            continue;
        }

        QVERIFY2(written.result.status == Status::Success, qPrintable(QString::fromLatin1(row.name) + QStringLiteral(": ") + formErrorText(written, 0)));
        if (row.isReadWithCaller)
        {
            Documents::DictionaryEntries states = { { "GP", graphicStateObject(row.outerLineWidth) } };
            if (row.hasExtra)
            {
                states.push_back({ "GX", graphicStateObject(2.0) });
            }
            states.push_back({ "GS1", addedGraphicState });
            QVERIFY2(written.resources == Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", form }, { "FmH", outerForm }, { "Fx", innerForm } }) },
                                                                              { "ExtGState", Documents::createDictionaryObject(std::move(states)) } }), row.name);
        }
        else
        {
            QVERIFY2(written.resources == Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "FmH", outerForm }, { "Fx", innerForm } }) },
                                                                              { "ExtGState", Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", addedGraphicState } }) } }), row.name);
        }
    }

    // ---- 5. The fallback of the outer form differs between two pages:
    //        page 1, page 2: "/FmH Do", /XObject << /FmH /Fx >> /ExtGState << /GZ << /LW 2 >> >>
    //                        (the page 2: /GZ << /LW z >>, and maybe /GS1 << /LW 9 >>)
    //        /FmH "/Fm2 Do", /Resources << /XObject << /Fm2 >> >> (no /ExtGState)
    //        /Fm2 "/GZ gs 0 0 10 10 re f" or "/Fx Do 0 0 10 10 re f", no /Resources
    //      /Fm2 is read with /FmH on the page 1. /FmH lacks /GZ: on each page /Fm2 takes it from
    //      that page, so z = 9 changes the look on the page 2 (refused under both pages, which
    //      name the page 2). With /Fx inside, /Fx would take the new /GS1 from the copy on the
    //      page 2, where today it takes the /GS1 of the page: refused too. ----
    struct FallbackRow
    {
        const char* name;
        QByteArray content;
        PDFReal secondLineWidth;
        bool hasSecondName;
        bool isRefused;
    };
    for (const FallbackRow& row : { FallbackRow{ "the pages give the fallback other objects", QByteArray("/GZ gs 0 0 10 10 re f"), 9.0, false, true },
                                    FallbackRow{ "the pages give the fallback equal objects", QByteArray("/GZ gs 0 0 10 10 re f"), 2.0, false, false },
                                    FallbackRow{ "a form without resources inside, the page 2 defines the new name", QByteArray("/Fx Do 0 0 10 10 re f"), 2.0, true, true },
                                    FallbackRow{ "a form without resources inside, no page defines the new name", QByteArray("/Fx Do 0 0 10 10 re f"), 2.0, false, false } })
    {
        PDFDocumentBuilder builder;
        const PDFObject form = addForm(&builder, row.content, { }, false);
        const PDFObject innerForm = addForm(&builder, "0 0 1 1 re f", { }, false);
        const PDFObject outerForm = addForm(&builder, "/Fm2 Do", { { "XObject", Documents::createDictionaryObject({ { "Fm2", form } }) } }, true);
        for (int pageIndex = 0; pageIndex < 2; ++pageIndex)
        {
            Documents::DictionaryEntries states = { { "GZ", graphicStateObject(pageIndex == 0 ? 2.0 : row.secondLineWidth) } };
            if (row.hasSecondName && pageIndex == 1)
            {
                states.push_back({ "GS1", graphicStateObject(9.0) });
            }
            addPage(&builder, "/FmH Do", { { "XObject", Documents::createDictionaryObject({ { "FmH", outerForm }, { "Fx", innerForm } }) },
                                           { "ExtGState", Documents::createDictionaryObject(std::move(states)) } });
        }
        const PDFDocument document = builder.build();
        QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));

        const PDFObject outerResources = document.getObject(outerForm).getStream()->getDictionary()->get("Resources");
        const PDFContentContainerPointer container = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), 0, outerResources);
        QVERIFY2(container != nullptr, row.name);
        const PDFContentContainerPointer edited = setOverride(container, nodesOfKind(*container, PDFContentNodeKind::Path).back()->getId(), alphaOverride());
        QVERIFY2(edited != nullptr, row.name);

        Written written;
        PDFDocumentBuilder writtenBuilder(&document);
        written.result = Writer::write(&document, snapshotOf({ edited }), &writtenBuilder, Writer::Options(), nullptr, nullptr);
        written.isBuilderUntouched = *writtenBuilder.getStorage() == document.getStorage();
        if (row.isRefused)
        {
            QVERIFY2(written.result.status == Status::PageErrors, row.name);
            QVERIFY2(written.result.errors.size() == 2, row.name);
            QVERIFY2(formErrorText(written, 0).contains(QStringLiteral("pages 2 give it other resources than the page 1")), qPrintable(formErrorText(written, 0)));
            QVERIFY2(written.isBuilderUntouched, row.name);
        }
        else
        {
            QVERIFY2(written.result.status == Status::Success, qPrintable(QString::fromLatin1(row.name) + QStringLiteral(": ") + formErrorText(written, 0)));
            QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));
        }
    }
}

void DocumentToolContentTest::writerFormPageResourcesRendered()
{
    // The reading, the writing and the rendering give a name of a form without /Resources the
    // same object: the one of the form drawing it, and a name that form lacks the one of the page
    // (NOTES.txt 7.8.3, the owner's decision of 2026-11-27).
    //   page   "/FmH Do", /XObject << /FmH >> /ExtGState << /GA << /ca 0.2 >> /GB << /ca 0.5 >> >>
    //   /FmH   "/Fm2 Do", /Resources << /XObject << /Fm2 >> /ExtGState << /GA << /ca 1 >> >> >>
    //   /Fm2   "/GA gs 1 0 0 rg 0 0 50 50 re f /GB gs 0 1 0 rg 100 0 50 50 re f
    //           /GA gs 0 0 1 rg 50 50 50 50 re f", no /Resources
    // The red square [0 50] x [0 50] is drawn with the /GA of /FmH: opaque red (the page-first
    // order would make it pale, 0.2). The green square [100 150] x [0 50] takes /GB from the page
    // (the fallback: /FmH has no /GB): green with the alpha 0.5 over white, (128, 255, 128) - the
    // rule of before 2026-11-27 left /GB undefined and the green opaque. The blue square
    // [50 100] x [50 100] (opaque) gets the alpha 0.5 by the editor (a new name /GS1): /Fm2 gets a
    // copy of the resources it was read with - the dictionaries of /FmH with the entries of the
    // page it lacks - and /GS1. After the write red and green are as before, blue is (128, 128, 255).
    const auto alphaState = [](PDFReal alpha)
    {
        return Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(alpha) } });
    };

    PDFDocumentBuilder documentBuilder;
    const PDFObject form = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                          { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) } },
                                                      "/GA gs 1 0 0 rg 0 0 50 50 re f /GB gs 0 1 0 rg 100 0 50 50 re f /GA gs 0 0 1 rg 50 50 50 50 re f");
    const PDFObject outerResources = Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", form } }) },
                                                                         { "ExtGState", Documents::createDictionaryObject({ { "GA", alphaState(1.0) } }) } });
    const PDFObject outerForm = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                                               { "Resources", outerResources } },
                                                           "/Fm2 Do");
    const PDFObjectReference page = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    documentBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "FmH", outerForm } }) },
                                                                                                                         { "ExtGState", Documents::createDictionaryObject({ { "GA", alphaState(0.2) },
                                                                                                                                                                            { "GB", alphaState(0.5) } }) } }) },
                                                                      { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), "/FmH Do") } }));
    const PDFDocument document = documentBuilder.build();

    // Renders the page 200 x 200 into 200 x 200 pixels; the color at a point of the page
    const auto render = [](const PDFDocument& renderedDocument)
    {
        PDFCMSGeneric cms;
        PDFFontCache fontCache(32, 32);
        PDFICCProfileCache iccProfileCache;
        PDFOptionalContentActivity activity(&renderedDocument, OCUsage::View, nullptr);
        fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&renderedDocument), &activity));
        iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&renderedDocument), &activity));

        PDFRenderer renderer(&renderedDocument, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::Features(), PDFMeshQualitySettings());
        QImage image(Documents::PAGE_SIZE, Documents::PAGE_SIZE, QImage::Format_RGB888);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            renderer.render(&painter, QRectF(0, 0, image.width(), image.height()), 0);
        }
        return image;
    };
    const auto colorAt = [](const QImage& image, int x, int y) { return QColor(image.pixel(x, image.height() - 1 - y)); };
    const auto isOpaque = [](const QColor& color, int red, int green, int blue)
    {
        return qAbs(color.red() - red) < 20 && qAbs(color.green() - green) < 20 && qAbs(color.blue() - blue) < 20;
    };

    // Before: red and blue opaque, green half transparent
    const QImage before = render(document);
    QVERIFY2(isOpaque(colorAt(before, 25, 25), 255, 0, 0), qPrintable(colorAt(before, 25, 25).name()));
    QVERIFY2(isOpaque(colorAt(before, 125, 25), 128, 255, 128), qPrintable(colorAt(before, 125, 25).name()));
    QVERIFY2(isOpaque(colorAt(before, 75, 75), 0, 0, 255), qPrintable(colorAt(before, 75, 75).name()));

    // The reading, as the session reads it: drawn by /FmH (its resources are the caller resources
    // of the build). The red path has the alpha 1 of /FmH, the green one the 0.5 of the page.
    const PDFContentContainerPointer container = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), 0, outerResources);
    QVERIFY(container != nullptr);
    QCOMPARE(container->getInheritedResourcesPageIndex(), PDFInteger(0));
    const std::vector<PDFContentNodePointer> paths = nodesOfKind(*container, PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(3));
    QCOMPARE(paths[0]->getState()->getState().getAlphaFilling(), 1.0);
    QCOMPARE(paths[1]->getState()->getState().getAlphaFilling(), 0.5);
    QCOMPARE(paths[2]->getState()->getState().getAlphaFilling(), 1.0);
    const PDFContentContainerPointer edited = setOverride(container, paths[2]->getId(), alphaOverride());
    QVERIFY(edited != nullptr);

    // The writing: a copy of the resources of the build with the new name
    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY2(result.status == Status::Success, qPrintable(result.errors.empty() ? QString() : result.errors.cbegin()->second.front().text));
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    QVERIFY(!result.isPagesChangedIncomplete);
    const PDFDocument written = builder.build();
    const PDFObject writtenResources = written.getObject(written.getObjectByReference(form.getReference()).getStream()->getDictionary()->get("Resources"));
    const PDFObject addedState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });
    QVERIFY(writtenResources == Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm2", form }, { "FmH", outerForm } }) },
                                                                    { "ExtGState", Documents::createDictionaryObject({ { "GA", alphaState(1.0) }, { "GB", alphaState(0.5) }, { "GS1", addedState } }) } }));

    // After: red and green as before, blue blended with the white page by 0.5
    const QImage after = render(written);
    QVERIFY2(isOpaque(colorAt(after, 25, 25), 255, 0, 0), qPrintable(colorAt(after, 25, 25).name()));
    QVERIFY2(isOpaque(colorAt(after, 125, 25), 128, 255, 128), qPrintable(colorAt(after, 125, 25).name()));
    QVERIFY2(isOpaque(colorAt(after, 75, 75), 128, 128, 255), qPrintable(colorAt(after, 75, 75).name()));

    // Read with the page alone (the tree has the alpha 0.2 of the page for /GA): the write would
    // draw the red square pale under /FmH - refused, nothing written
    const PDFContentContainerPointer pageRead = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), 0);
    QVERIFY(pageRead != nullptr);
    QCOMPARE(nodesOfKind(*pageRead, PDFContentNodeKind::Path)[0]->getState()->getState().getAlphaFilling(), 0.2);
    PDFDocumentBuilder refusedBuilder(&document);
    const PDFContentWriteResult refused = Writer::write(&document, snapshotOf({ setOverride(pageRead, nodesOfKind(*pageRead, PDFContentNodeKind::Path)[2]->getId(), alphaOverride()) }),
                                                       &refusedBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(refused.status == Status::PageErrors);
    QVERIFY(*refusedBuilder.getStorage() == document.getStorage());
}

void DocumentToolContentTest::writerFormInheritedWrittenNames_data()
{
    QTest::addColumn<bool>("isFormUse");
    QTest::addColumn<bool>("isSuppressed");

    QTest::newRow("a deleted text") << false << false;
    QTest::newRow("a deleted use of a form without resources") << true << false;
    QTest::newRow("a suppressed operation of a text") << false << true;
}

void DocumentToolContentTest::writerFormInheritedWrittenNames()
{
    // A4L2-2 (G2-9): a form without /Resources, to which a name is added, is refused only when
    // the content, which is written, would see other resources on another page. The operations
    // of a deleted object stay in the operation list of the container (it only grows), but they
    // are not written: their names do not count. Two pages draw /Fm2 by "/Fm2 Do"; its container
    // is read with the page 1; the path of the form gets the alpha 0.5 (the new name /GS1, so the
    // form gets a copy of the resources of the page 1). A suppressed operation (R-1: the editor
    // overrides the property it sets) is not written either.
    QFETCH(bool, isFormUse);
    QFETCH(bool, isSuppressed);
    const PDFObject addedGraphicState = Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } });

    struct Written
    {
        PDFContentWriteResult result;
        PDFDocument document;
        PDFDocument written;
        PDFObject form;

        /// The builder is compared before build(): build() writes /ModDate (seconds) into the
        /// information dictionary, a later second would differ from the source document
        bool isBuilderUntouched = false;
    };

    // Builds the two pages "/Fm2 Do" with /Fm2 of the content and the resources the factory gives
    // each page (it gets /Fm2 and the index of the page), deletes the first object of the kind
    // (when given), suppresses every operation with a resource name (when asked), sets the alpha
    // of the path and writes the form
    using ResourcesOfPage = std::function<Documents::DictionaryEntries(PDFDocumentBuilder*, const PDFObject&, int)>;
    const auto write = [&](const QByteArray& content, const ResourcesOfPage& resourcesOfPage, std::optional<PDFContentNodeKind> deletedKind, bool suppressNames, Written* data)
    {
        PDFDocumentBuilder documentBuilder;
        data->form = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                    { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, content);
        for (int pageIndex = 0; pageIndex < 2; ++pageIndex)
        {
            Documents::DictionaryEntries resources = resourcesOfPage(&documentBuilder, data->form, pageIndex);
            const PDFObjectReference page = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            documentBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject(std::move(resources)) },
                                                                              { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), "/Fm2 Do") } }));
        }
        data->document = documentBuilder.build();

        PDFContentContainerPointer container = buildContainer(data->document, PDFContentContainerKey::createForm(data->form.getReference()), 0);
        QVERIFY(container != nullptr);
        QCOMPARE(container->getInheritedResourcesPageIndex(), PDFInteger(0));
        if (deletedKind)
        {
            const std::vector<PDFContentNodePointer> deleted = nodesOfKind(*container, *deletedKind);
            QVERIFY(!deleted.empty());
            container = removeNode(container, deleted.front()->getId());
            QVERIFY(container != nullptr);
        }
        if (suppressNames)
        {
            PDFContentContainerEditor editor(container);
            PDFContentOperationList& operations = editor.getOperations();
            for (size_t i = 0; i < operations.getCount(); ++i)
            {
                if (!operations.getOperation(i).getResourceNames().empty())
                {
                    PDFContentOperation::Flags flags = operations.getOperation(i).getFlags();
                    flags.setFlag(PDFContentOperation::Suppressed, true);
                    operations.setFlags(i, flags);
                }
            }
            container = editor.build();
            QVERIFY(container != nullptr);
        }
        container = setOverride(container, nodesOfKind(*container, PDFContentNodeKind::Path).front()->getId(), alphaOverride());
        QVERIFY(container != nullptr);

        PDFDocumentBuilder builder(&data->document);
        data->result = Writer::write(&data->document, snapshotOf({ container }), &builder, Writer::Options(), nullptr, nullptr);
        data->isBuilderUntouched = *builder.getStorage() == data->document.getStorage();
        data->written = builder.build();
    };

    const auto isRefusedForPage2 = [](const Written& data)
    {
        return data.result.status == Status::PageErrors && data.result.errors.size() == 2 && data.result.errors.at(0).size() == 1 &&
               data.result.errors.at(0)[0].text.contains(QStringLiteral("pages 2 give it other resources than the page 1")) &&
               data.isBuilderUntouched;
    };

    // ---- A text "BT /F1 9 Tf ... ET" before the path; /F1 is Helvetica on the page 1 and
    //      Courier on the page 2 ----
    const QByteArray textAndPath = "BT /F1 9 Tf 1 1 Td (A) Tj ET 0 0 10 10 re f";
    const auto fontOf = [](int pageIndex)
    {
        return Documents::createDictionaryObject({ { "F1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("Font") }, { "Subtype", PDFObject::createName("Type1") },
                                                                                                { "BaseFont", PDFObject::createName(pageIndex == 0 ? "Helvetica" : "Courier") } }) } });
    };
    const ResourcesOfPage fonts = [&fontOf](PDFDocumentBuilder*, const PDFObject& form, int pageIndex)
    {
        return Documents::DictionaryEntries({ { "XObject", Documents::createDictionaryObject({ { "Fm2", form } }) }, { "Font", fontOf(pageIndex) } });
    };

    // The text is written: its /F1 differs on the page 2, the form is refused
    if (!isFormUse)
    {
        Written data;
        write(textAndPath, fonts, std::nullopt, false, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(isRefusedForPage2(data), qPrintable(data.result.errors.empty() ? QString() : data.result.errors.cbegin()->second.front().text));
    }

    // The text is deleted (or its "/F1 9 Tf" suppressed): the written form uses no /F1; it gets
    // the resources of the page 1 with /GS1 and both pages are changed
    if (!isFormUse)
    {
        Written data;
        write(textAndPath, fonts, isSuppressed ? std::nullopt : std::optional<PDFContentNodeKind>(PDFContentNodeKind::TextObject), isSuppressed, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(data.result.status == Status::Success, qPrintable(data.result.errors.empty() ? QString() : data.result.errors.cbegin()->second.front().text));
        QCOMPARE(data.result.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));

        const PDFObject& stream = data.written.getObjectByReference(data.form.getReference());
        QVERIFY(!data.written.getDecodedStream(stream.getStream()).contains("F1"));
        const PDFDictionary* resources = stream.getStream()->getDictionary()->get("Resources").getDictionary();
        QVERIFY(resources != nullptr);
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GS1", addedGraphicState } }));
        QVERIFY(resources->get("Font") == fontOf(0));
    }

    // ---- A form without resources /Fx ("0 0 1 1 re f", the same object on both pages) drawn
    //      before the path: the content of /Fm2 draws content, which inherits too, so every name
    //      of the recorded page must be a name of the other page (the strict comparison: /Fx
    //      would take a name its page does not define from the copy in /Fm2, NOTES.txt); the page
    //      1 has /ExtGState << /GQ >>, the page 2 has no /ExtGState. The form /Fm2 uses no other
    //      name. ----
    const QByteArray formAndPath = "/Fx Do 0 0 10 10 re f";
    PDFObject innerForm;
    const ResourcesOfPage forms = [&innerForm](PDFDocumentBuilder* builder, const PDFObject& form, int pageIndex)
    {
        if (pageIndex == 0)
        {
            innerForm = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                              { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, "0 0 1 1 re f");
        }

        Documents::DictionaryEntries entries = { { "XObject", Documents::createDictionaryObject({ { "Fm2", form }, { "Fx", innerForm } }) } };
        if (pageIndex == 0)
        {
            entries.emplace_back("ExtGState", Documents::createDictionaryObject({ { "GQ", graphicStateObject(9.0) } }));
        }
        return entries;
    };

    // The use of /Fx is written: the page 2 does not define the /GQ of the page 1, the form is refused
    if (isFormUse)
    {
        Written data;
        write(formAndPath, forms, std::nullopt, false, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(isRefusedForPage2(data), qPrintable(data.result.errors.empty() ? QString() : data.result.errors.cbegin()->second.front().text));
    }

    // The use of /Fx is deleted: nothing of the written form inherits, it uses no name of the
    // pages; it gets the resources of the page 1 with /GS1
    if (isFormUse)
    {
        Written data;
        write(formAndPath, forms, PDFContentNodeKind::FormUse, false, &data);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(data.result.status == Status::Success, qPrintable(data.result.errors.empty() ? QString() : data.result.errors.cbegin()->second.front().text));
        QCOMPARE(data.result.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));

        const PDFObject& stream = data.written.getObjectByReference(data.form.getReference());
        QVERIFY(!data.written.getDecodedStream(stream.getStream()).contains("Fx"));
        const PDFDictionary* resources = stream.getStream()->getDictionary()->get("Resources").getDictionary();
        QVERIFY(resources != nullptr);
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GQ", graphicStateObject(9.0) }, { "GS1", addedGraphicState } }));
    }
}

void DocumentToolContentTest::writerFormInheritedChain()
{
    // Review R2-1: a form without /Resources reached through a chain of content without resources
    // has the caller of the chain as its caller (NOTES.txt 7.8.3: each content without resources
    // passes the resources of the content drawing it on, with the page as the fallback):
    //   page   /XObject << /F [/FmA] >> [/ExtGState << /GX << /LW g >> >>], content "/FmA Do"
    //   A      resources << /ExtGState << /GX << /LW 2 >> >> + the intermediary I >>
    //   I      without resources: a form "/F Do", a tiling pattern cell "/F Do", a Type 3 glyph
    //          procedure "1000 0 d0 /F Do", or A itself is the appearance stream of an
    //          annotation of the page, which draws the form F' "/F Do"
    //   F      without resources: "/GX gs 0 0 10 10 re S 0 0 5 5 re f"
    // The renderer draws F with /GX of A (2): I takes /F from the page (A has no /F) and passes
    // the resources of A on. The walk over the resources knows no edge from I to F; the page and
    // A are the places drawing F. F is read with the page (as a page drawing it directly would
    // give it), its filled path gets the alpha 0.5 (a new name). With g = 9 the tree has the
    // width 9, the copy of the page would give F the width 9 under A too: refused, nothing
    // written. With g = 2 (an equal state) both places agree: written with the copy of the page.
    // Read with A as the caller (the session does so), g = 9 is refused by the page: it names F
    // and might draw it directly with its /GX.
    enum class Intermediary
    {
        Form,
        Pattern,
        Type3,
        Appearance
    };

    struct ChainDocument
    {
        PDFDocument document;
        PDFObject form;             ///< F
        PDFObject callerResources;  ///< The resources of A
    };

    const auto createChainDocument = [](Intermediary intermediary, PDFReal pageLineWidth)
    {
        PDFDocumentBuilder builder;
        ChainDocument result;
        result.form = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                             { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, "/GX gs 0 0 10 10 re S 0 0 5 5 re f");

        Documents::DictionaryEntries callerEntries = { { "ExtGState", Documents::createDictionaryObject({ { "GX", graphicStateObject(2.0) } }) } };
        QByteArray callerContent;
        switch (intermediary)
        {
            case Intermediary::Form:
            case Intermediary::Appearance:
            {
                const PDFObject inner = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, "/F Do");
                callerEntries.push_back({ "XObject", Documents::createDictionaryObject({ { "Fp", inner } }) });
                callerContent = "/Fp Do";
                break;
            }

            case Intermediary::Pattern:
            {
                const PDFObject cell = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("Pattern") }, { "PatternType", PDFObject::createInteger(1) },
                                                                              { "PaintType", PDFObject::createInteger(1) }, { "TilingType", PDFObject::createInteger(1) },
                                                                              { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                              { "XStep", PDFObject::createInteger(10) }, { "YStep", PDFObject::createInteger(10) } }, "/F Do");
                callerEntries.push_back({ "Pattern", Documents::createDictionaryObject({ { "Pc", cell } }) });
                callerContent = "/Pattern cs /Pc scn 0 0 10 10 re f";
                break;
            }

            case Intermediary::Type3:
            {
                const PDFObject procedure = Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "1000 0 d0 /F Do");
                const PDFObject font = Documents::addObject(&builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Font") }, { "Subtype", PDFObject::createName("Type3") },
                                                                                                        { "FontBBox", Documents::createIntegerArrayObject({ 0, 0, 1000, 1000 }) },
                                                                                                        { "FontMatrix", Documents::createArrayObject({ PDFObject::createReal(0.001), PDFObject::createInteger(0), PDFObject::createInteger(0),
                                                                                                                                                       PDFObject::createReal(0.001), PDFObject::createInteger(0), PDFObject::createInteger(0) }) },
                                                                                                        { "CharProcs", Documents::createDictionaryObject({ { "g", procedure } }) },
                                                                                                        { "Encoding", Documents::createDictionaryObject({ { "Differences", Documents::createArrayObject({ PDFObject::createInteger(65), PDFObject::createName("g") }) } }) },
                                                                                                        { "FirstChar", PDFObject::createInteger(65) },
                                                                                                        { "LastChar", PDFObject::createInteger(65) },
                                                                                                        { "Widths", Documents::createIntegerArrayObject({ 1000 }) } }));
                callerEntries.push_back({ "Font", Documents::createDictionaryObject({ { "T3", font } }) });
                callerContent = "BT /T3 1 Tf (A) Tj ET";
                break;
            }
        }

        result.callerResources = Documents::createDictionaryObject(callerEntries);
        const PDFObject caller = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                        { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                        { "Resources", result.callerResources } }, callerContent);

        Documents::DictionaryEntries forms = { { "F", result.form } };
        if (intermediary != Intermediary::Appearance)
        {
            forms.push_back({ "FmA", caller });
        }
        Documents::DictionaryEntries pageEntries = { { "XObject", Documents::createDictionaryObject(std::move(forms)) } };
        if (pageLineWidth > 0.0)
        {
            pageEntries.push_back({ "ExtGState", Documents::createDictionaryObject({ { "GX", graphicStateObject(pageLineWidth) } }) });
        }

        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        Documents::DictionaryEntries pageDictionary = { { "Resources", Documents::createDictionaryObject(std::move(pageEntries)) },
                                                        { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), intermediary == Intermediary::Appearance ? "" : "/FmA Do") } };
        if (intermediary == Intermediary::Appearance)
        {
            const PDFObject annotation = Documents::addObject(&builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Annot") }, { "Subtype", PDFObject::createName("Square") },
                                                                                                         { "Rect", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                                                         { "AP", Documents::createDictionaryObject({ { "N", caller } }) } }));
            pageDictionary.push_back({ "Annots", Documents::createArrayObject({ annotation }) });
        }
        builder.mergeTo(page, Documents::createDictionaryObject(std::move(pageDictionary)));
        result.document = builder.build();
        return result;
    };

    struct Row
    {
        const char* name;
        Intermediary intermediary;
        PDFReal pageLineWidth;
        bool isReadWithCaller;
        bool isRefused;
        const char* errorText;
    };
    const char* const callerError = "a form or an annotation of the page 1, which draws it, gives it other resources than those it was read with";
    const char* const pageError = "pages 1 give it other resources than the page 1";
    for (const Row& row : { Row{ "a form in the chain, the page has another /GX", Intermediary::Form, 9.0, false, true, callerError },
                            Row{ "a pattern cell in the chain, the page has another /GX", Intermediary::Pattern, 9.0, false, true, callerError },
                            Row{ "a glyph procedure in the chain, the page has another /GX", Intermediary::Type3, 9.0, false, true, callerError },
                            Row{ "an appearance with a form in the chain, the page has another /GX", Intermediary::Appearance, 9.0, false, true, callerError },
                            Row{ "a form in the chain, the page agrees", Intermediary::Form, 2.0, false, false, "" },
                            Row{ "a pattern cell in the chain, the page agrees", Intermediary::Pattern, 2.0, false, false, "" },
                            Row{ "a glyph procedure in the chain, the page agrees", Intermediary::Type3, 2.0, false, false, "" },
                            Row{ "an appearance with a form in the chain, the page agrees", Intermediary::Appearance, 2.0, false, false, "" },
                            Row{ "read with the caller of the chain, the page has another /GX", Intermediary::Form, 9.0, true, true, pageError },
                            Row{ "read with the caller of the chain, the page has no /GX", Intermediary::Form, 0.0, true, true, pageError },
                            Row{ "read with the caller of the chain, the page agrees", Intermediary::Form, 2.0, true, false, "" } })
    {
        const ChainDocument chain = createChainDocument(row.intermediary, row.pageLineWidth);
        const PDFContentContainerPointer container = buildContainer(chain.document, PDFContentContainerKey::createForm(chain.form.getReference()), 0,
                                                                    row.isReadWithCaller ? chain.callerResources : PDFObject());
        QVERIFY2(container != nullptr, row.name);
        const std::vector<PDFContentNodePointer> paths = nodesOfKind(*container, PDFContentNodeKind::Path);
        QVERIFY2(paths.size() == 2, row.name);
        QVERIFY2(paths[0]->getState()->getState().getLineWidth() == (row.isReadWithCaller ? 2.0 : (row.pageLineWidth > 0.0 ? row.pageLineWidth : 1.0)), row.name);
        const PDFContentContainerPointer edited = setOverride(container, paths[1]->getId(), alphaOverride());
        QVERIFY2(edited != nullptr, row.name);

        PDFDocumentBuilder builder(&chain.document);
        const PDFContentWriteResult result = Writer::write(&chain.document, snapshotOf({ edited }), &builder, Writer::Options(), nullptr, nullptr);
        const QString errorText = result.errors.empty() ? QString() : result.errors.cbegin()->second.front().text;
        if (row.isRefused)
        {
            QVERIFY2(result.status == Status::PageErrors, row.name);
            QVERIFY2(result.errors.size() == 1 && result.errors.count(0), row.name);
            QVERIFY2(errorText.contains(QString::fromLatin1(row.errorText)), qPrintable(QString::fromLatin1(row.name) + QStringLiteral(": ") + errorText));
            QVERIFY2(*builder.getStorage() == chain.document.getStorage(), row.name);
            continue;
        }

        QVERIFY2(result.status == Status::Success, qPrintable(QString::fromLatin1(row.name) + QStringLiteral(": ") + errorText));
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));

        // The copy holds /GX << /LW 2 >> (of the page or of A, equal objects) and the new state
        const PDFObject writtenResources = builder.getObject(builder.getObjectByReference(chain.form.getReference()).getStream()->getDictionary()->get("Resources"));
        QVERIFY2(writtenResources.isDictionary(), row.name);
        const PDFObject states = builder.getObject(writtenResources.getDictionary()->get("ExtGState"));
        QVERIFY2(states == Documents::createDictionaryObject({ { "GX", graphicStateObject(2.0) },
                                                                { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "ca", PDFObject::createReal(0.5) } }) } }), row.name);
    }
}

void DocumentToolContentTest::writerFormInheritedTwoCallers()
{
    // Review R2-2: a form without /Resources drawn by two forms with resources of their own,
    // which give one name two objects - a file of the kind the rule of NOTES.txt 7.8.3 is for:
    //   page   /XObject << /FmA /FmB >>, content "/FmA Do /FmB Do" (no /ExtGState)
    //   A      "/F Do", /Resources << /XObject << /F >> /ExtGState << /GX << /LW 2 >> >> >>
    //   B      "/F Do", /Resources << /XObject << /F >> /ExtGState << /GX << /LW b >> >> >>
    //   F      "/GX gs 0 0 10 10 re S 5 5 m 6 6 l S", no /Resources
    // F is read as drawn by A (the session takes the first use in the order of the document): its
    // paths have the width 2. The written content is right under A only, unless B gives /GX the
    // same object (b = 2). Checked also when no name is added (the form keeps no /Resources):
    //  - the second path is deleted: the written content uses /GX;
    //  - the state run "/GX gs" is deleted and the second path keeps its width by an override of
    //    the width 2 (what R-2 / R-3 write): the written content no longer names /GX, but holds
    //    a value the build took from it (the source of the property in the state of the path).
    // b = 9: refused under the page, nothing written (in B the paths had the width 9, the second
    // one would get 2); b = 2: written, without /Resources.
    for (const PDFReal secondWidth : { 9.0, 2.0 })
    {
        for (const bool isValueKept : { false, true })
        {
            PDFDocumentBuilder documentBuilder;
            const PDFObject form = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                                  { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) } }, "/GX gs 0 0 10 10 re S 5 5 m 6 6 l S");
            const auto callerResources = [&form](PDFReal lineWidth)
            {
                return Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "F", form } }) },
                                                           { "ExtGState", Documents::createDictionaryObject({ { "GX", graphicStateObject(lineWidth) } }) } });
            };
            const PDFObject resourcesA = callerResources(2.0);
            const PDFObject formA = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                                   { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) }, { "Resources", resourcesA } }, "/F Do");
            const PDFObject formB = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                                   { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) }, { "Resources", callerResources(secondWidth) } }, "/F Do");
            const PDFObjectReference page = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            documentBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "FmA", formA }, { "FmB", formB } }) } }) },
                                                                              { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), "/FmA Do /FmB Do") } }));
            const PDFDocument document = documentBuilder.build();

            PDFContentContainerPointer container = buildContainer(document, PDFContentContainerKey::createForm(form.getReference()), 0, resourcesA);
            QVERIFY(container != nullptr);
            const std::vector<PDFContentNodePointer> paths = nodesOfKind(*container, PDFContentNodeKind::Path);
            QCOMPARE(paths.size(), size_t(2));
            QCOMPARE(paths[1]->getState()->getState().getLineWidth(), 2.0);

            QByteArray expectedContent;
            if (isValueKept)
            {
                const std::vector<PDFContentNodePointer> runs = nodesOfKind(*container, PDFContentNodeKind::StateRun);
                QCOMPARE(runs.size(), size_t(1));
                container = removeNode(container, runs[0]->getId());
                QVERIFY(container != nullptr);

                PDFContentStateValue width;
                width.kind = PDFContentStateValue::Kind::Real;
                width.real = 2.0;
                PDFContentStateOverride override;
                override.properties[PDFContentStateProperty::LineWidth] = width;
                container = setOverride(container, paths[1]->getId(), override);
            }
            else
            {
                container = removeNode(container, paths[1]->getId());
                expectedContent = "/GX gs 0 0 10 10 re S";
            }
            QVERIFY(container != nullptr);

            PDFDocumentBuilder builder(&document);
            const PDFContentWriteResult result = Writer::write(&document, snapshotOf({ container }), &builder, Writer::Options(), nullptr, nullptr);
            const QString errorText = result.errors.empty() ? QString() : result.errors.cbegin()->second.front().text;
            if (secondWidth != 2.0)
            {
                QVERIFY2(result.status == Status::PageErrors, qPrintable(QString::number(int(isValueKept))));
                QCOMPARE(result.errors.size(), size_t(1));
                QVERIFY2(errorText.contains(QStringLiteral("a form or an annotation of the page 1, which draws it, gives it other resources than those it was read with")), qPrintable(errorText));
                QVERIFY(*builder.getStorage() == document.getStorage());
                continue;
            }

            QVERIFY2(result.status == Status::Success, qPrintable(errorText));
            const PDFDocument writtenDocument = builder.build();
            const PDFObject& stream = writtenDocument.getObjectByReference(form.getReference());
            QVERIFY(!stream.getStream()->getDictionary()->hasKey("Resources"));
            const QByteArray written = writtenDocument.getDecodedStream(stream.getStream());
            if (isValueKept)
            {
                QVERIFY2(!written.contains("GX") && written.contains(" w"), written.constData());
            }
            else
            {
                QCOMPARE(written, expectedContent);
            }
        }
    }
}

void DocumentToolContentTest::writerFormBehindWalkLimit()
{
    // A skipped page stays as it is - also when nothing can tell, that it does not use a changed
    // form. The pages of a form are found by the walk over the resources of the pages
    // (PDFResourceWalk), which enters owners nested at most 256 deep. The form /Fm1 is drawn by
    // the page 1; the resources of the page 2 reach it only through a chain of 260 forms, each
    // naming the next one - the walk stops inside of the chain and does not know the page 2 as a
    // page of the form.
    PDFDocumentBuilder documentBuilder;
    const PDFObject form = Documents::addFormObject(&documentBuilder);
    PDFObject chain = form;
    for (int i = 0; i < 260; ++i)
    {
        chain = Documents::addStreamObject(&documentBuilder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                               { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                               { "Resources", formResources({ { "Fn", chain } }) } }, "/Fn Do");
    }
    for (const auto& [name, object] : { std::pair<const char*, PDFObject>("Fm1", form), std::pair<const char*, PDFObject>("Fc", chain) })
    {
        const PDFObjectReference page = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        documentBuilder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", formResources({ { name, object } }) },
                                                                          { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), QByteArray("/") + name + " Do") } }));
    }
    const PDFDocument document = documentBuilder.build();

    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(form.getReference());
    const PDFContentContainerPointer original = buildContainer(document, formKey, 0);
    QVERIFY(original != nullptr);
    const PDFContentEditSnapshot snapshot = snapshotOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[0]->getId()) });

    // No page is skipped: the form is written. The result says, that the page 2 may use it too
    // (the walk did not reach every page): the page 1 is the only page found
    {
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.skippedContainers.empty());
        QCOMPARE(result.objectsRemoved, PDFInteger(1));
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QVERIFY(result.isPagesChangedIncomplete);

        // The tool says it: the data key and the text of the fact of the changed pages
        Tool tool;
        const PDFDocumentToolResult toolResult = tool.run(contextOf(document), applyParameters(std::make_shared<const PDFContentEditSnapshot>(snapshot)), nullptr, nullptr);
        QVERIFY2(toolResult.isSuccess(), qPrintable(toolResult.message));
        QCOMPARE(QByteArray(Tool::DATA_PAGES_CHANGED_INCOMPLETE), QByteArray("pagesChangedIncomplete"));
        QCOMPARE(toolResult.data.value(QStringLiteral("pagesChangedIncomplete")).toBool(false), true);
        const auto isPagesFact = [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("pagesChanged"); };
        const auto fact = std::find_if(toolResult.facts.cbegin(), toolResult.facts.cend(), isPagesFact);
        QVERIFY(fact != toolResult.facts.cend());
        QVERIFY2(fact->value.contains(QStringLiteral("possibly other pages")), qPrintable(fact->value));
    }

    // A write of the page 1 alone needs no walk: complete
    {
        const PDFContentContainerPointer page = buildPage(document, 0);
        const PDFContentEditSnapshot pageSnapshot = snapshotOf({ appendRectangle(page, "0 0 5 5", FIRST_NEW_NODE) });
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, pageSnapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QVERIFY(!result.isPagesChangedIncomplete);
    }

    // The page 2 is skipped: the walk was not complete, so the form may be one of its forms -
    // it is left out
    {
        Writer::Options options;
        options.skippedPages = { 1 };

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.pagesChanged.empty());
        QCOMPARE(result.skippedContainers.size(), size_t(1));
        QCOMPARE(result.skippedContainers.at(formKey), std::vector<PDFInteger>({ 1 }));
        QVERIFY(*builder.getStorage() == document.getStorage());
    }
}

void DocumentToolContentTest::writerBatchTwoPages()
{
    // zadani.md 12.7: a tool without a canvas adds content to the pages of a document one after
    // the other. Plain code: for each page its container is built (no other page is loaded), a
    // rectangle is appended, a snapshot of that one container is written into the builder of
    // the whole run and the container is dropped.
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFInteger first = PDFInteger(document.getStorage().getObjects().size());

    const QByteArray rectangles[2] = { QByteArray("0 0 5 5"), QByteArray("190 190 10 10") };
    const PDFObject graphicStates[2] = { graphicStateObject(1.0), graphicStateObject(2.0) };
    QByteArray expectedContents[2];

    PDFDocumentBuilder builder(&document);
    for (PDFInteger pageIndex = 0; pageIndex < 2; ++pageIndex)
    {
        PDFContentContainerPointer container = buildPage(document, pageIndex);
        container = appendRectangle(container, rectangles[pageIndex], FIRST_NEW_NODE);
        QVERIFY(container != nullptr);

        // Each page brings one new indirect object. Its provisional reference is the first one
        // of a session (the number 1 with the reserved generation) - for both pages the same
        // one: each write maps it to a real object of its own.
        QByteArray name;
        container = addResource(container, PDFContentResourceCategory::ExtGState, "GS", PDFObject::createReference(PDFObjectReference(1, 65535)), &name);
        QCOMPARE(name, QByteArray("GS1"));

        PDFContentEditSnapshot snapshot = snapshotOf({ container });
        snapshot.newObjects[PDFObjectReference(1, 65535)] = graphicStates[pageIndex];
        expectedContents[pageIndex] = PDFContentStreamWriter::write(*container).content;

        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ pageIndex }));
        QCOMPARE(result.objectsAdded, PDFInteger(1));
        QCOMPARE(result.objectsEdited, PDFInteger(0));
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
    }

    const PDFDocument written = builder.build();

    // Each page: its old bytes, then the new rectangle on lines of its own
    QCOMPARE(pageContent(written, 0), expectedContents[0]);
    QCOMPARE(pageContent(written, 1), expectedContents[1]);
    QVERIFY2(pageContent(written, 0).startsWith(QByteArray(TWO_PATHS) + "\n0 0 5 5 re\nf"), pageContent(written, 0).constData());
    QVERIFY2(pageContent(written, 1).startsWith(QByteArray(ONE_PATH) + "\n190 190 10 10 re\nf"), pageContent(written, 1).constData());
    QCOMPARE(pageContent(written, 0).trimmed(), QByteArray(TWO_PATHS) + "\n0 0 5 5 re\nf");

    // Each page names its own new object: the first write made the objects "first" (the graphic
    // state) and "first + 1" (the content), the second one "first + 2" and "first + 3"
    QCOMPARE(PDFInteger(written.getStorage().getObjects().size()), first + 4);
    for (PDFInteger pageIndex = 0; pageIndex < 2; ++pageIndex)
    {
        const PDFObjectReference graphicState(first + 2 * pageIndex, 0);
        const PDFDictionary* resources = pageDictionary(written, pageIndex)->get("Resources").getDictionary();
        QVERIFY(resources->get("ExtGState") == Documents::createDictionaryObject({ { "GP", graphicStateObject(6.0) }, { "GS1", PDFObject::createReference(graphicState) } }));
        QVERIFY(written.getObjectByReference(graphicState) == graphicStates[pageIndex]);
        QVERIFY(pageDictionary(written, pageIndex)->get("Contents") == PDFObject::createReference(PDFObjectReference(first + 2 * pageIndex + 1, 0)));
    }

    // Read back
    QCOMPARE(nodesOfKind(*buildPage(written, 0), PDFContentNodeKind::Path).size(), size_t(3));
    QCOMPARE(nodesOfKind(*buildPage(written, 1), PDFContentNodeKind::Path).size(), size_t(2));
}

void DocumentToolContentTest::writerStructure()
{
    // The tagged page: the sequence /Span << /MCID 1 >> with the text (Second) is deleted. The
    // structure follows (zadani_jadro.md 4.12): the item 1 of the array of the page in the parent
    // tree becomes null, the array keeps its length.
    const PDFDocument document = Documents::createTaggedDocument();
    const PDFInteger first = PDFInteger(document.getStorage().getObjects().size());

    const PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(!model->isModified());
    const PDFContentStructureElementId spanElement = model->getElementOfMarkedContent(0, 1);
    QVERIFY(spanElement >= 0);

    const PDFContentStructureChange change = model->getChangeForDelete(0, 1, true);
    QVERIFY(change.action == PDFContentStructureChange::Action::RemoveSequence);
    const PDFContentStructureModelPointer changedModel = model->applied({ change });
    QVERIFY(changedModel->isModified());

    const PDFContentContainerPointer original = buildPage(document, 0);
    PDFContentNodeId sequence;
    for (const PDFContentNodePointer& node : nodesOfKind(*original, PDFContentNodeKind::MarkedContent))
    {
        if (node->getData().mcid == PDFInteger(1))
        {
            sequence = node->getId();
        }
    }
    QVERIFY(sequence.isValid());

    // The page also names a new object, so the order of the writing shows
    QByteArray name;
    const PDFObject graphicState = graphicStateObject(3.0);
    const PDFContentContainerPointer edited = addResource(removeNode(original, sequence), PDFContentResourceCategory::ExtGState, "GS", PDFObject::createReference(PDFObjectReference(1, 65535)), &name);

    PDFContentEditSnapshot snapshot = snapshotOf({ edited });
    snapshot.structure = changedModel;
    snapshot.newObjects[PDFObjectReference(1, 65535)] = graphicState;

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(result.status == Status::Success);
    QVERIFY(result.isStructureChanged);
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    QCOMPARE(result.objectsRemoved, PDFInteger(1));

    const PDFDocument written = builder.build();

    // The new object of the snapshot is the first new object of the builder: the content stream
    // and the objects of the structure come after it
    QVERIFY(written.getObjectByReference(PDFObjectReference(first, 0)) == graphicState);
    QVERIFY(pageDictionary(written, 0)->get("Resources").getDictionary()->get("ExtGState") == Documents::createDictionaryObject({ { "GS1", PDFObject::createReference(PDFObjectReference(first, 0)) } }));
    QVERIFY(pageDictionary(written, 0)->get("Contents").getReference().objectNumber > first);

    // The content has lost the sequence
    const QByteArray content = pageContent(written, 0);
    QVERIFY(!content.contains("/Span"));
    QVERIFY(!content.contains("(Second)"));
    QVERIFY(content.contains("(First)"));

    // The parent tree: /Nums [ 0 [P null null Figure] 1 Link ]
    const PDFDictionary* catalog = written.getDictionaryFromObject(written.getTrailerDictionary()->get("Root"));
    const PDFDictionary* root = written.getDictionaryFromObject(catalog->get("StructTreeRoot"));
    const PDFDictionary* originalRoot = document.getDictionaryFromObject(document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"))->get("StructTreeRoot"));
    QVERIFY(root != nullptr);
    const PDFDictionary* parentTree = written.getDictionaryFromObject(root->get("ParentTree"));
    QVERIFY(parentTree != nullptr);
    QVERIFY(root->get("ParentTree").getReference().objectNumber > first);
    const PDFObject& numbers = written.getObject(parentTree->get("Nums"));
    QVERIFY(numbers.isArray());
    QCOMPARE(numbers.getArray()->getCount(), size_t(4));
    QCOMPARE(numbers.getArray()->getItem(0).getInteger(), PDFInteger(0));

    const PDFObject& originalNumbers = document.getObject(document.getDictionaryFromObject(originalRoot->get("ParentTree"))->get("Nums"));
    const PDFArray* originalItems = document.getObject(originalNumbers.getArray()->getItem(1)).getArray();
    const PDFObject& items = written.getObject(numbers.getArray()->getItem(1));
    QVERIFY(items.isArray());
    QCOMPARE(items.getArray()->getCount(), size_t(4));
    QVERIFY(items.getArray()->getItem(0) == originalItems->getItem(0));
    QVERIFY(items.getArray()->getItem(1).isNull());
    QVERIFY(items.getArray()->getItem(2).isNull());
    QVERIFY(items.getArray()->getItem(3) == originalItems->getItem(3));

    // ---- A structure, which is not modified, is not written ----
    PDFContentEditSnapshot unmodified = snapshotOf({ removeNode(original, sequence) });
    unmodified.structure = model;
    PDFDocumentBuilder otherBuilder(&document);
    const PDFContentWriteResult otherResult = Writer::write(&document, unmodified, &otherBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(otherResult.status == Status::Success);
    QVERIFY(!otherResult.isStructureChanged);
    QCOMPARE(PDFInteger(otherBuilder.getStorage()->getObjects().size()), first + 1);

    // ---- The structure alone: no container, a changed model ----
    PDFContentEditSnapshot structureOnly;
    structureOnly.structure = changedModel;
    QVERIFY(!structureOnly.isEmpty());
    PDFDocumentBuilder structureBuilder(&document);
    const PDFContentWriteResult structureResult = Writer::write(&document, structureOnly, &structureBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(structureResult.status == Status::Success);
    QVERIFY(structureResult.isStructureChanged);
    QVERIFY(structureResult.pagesChanged.empty());
}

void DocumentToolContentTest::writerStructureSkippedPages()
{
    // The tagged page and a page without tags. On the tagged page the sequence /Span << /MCID 1 >>
    // is deleted: the model loses the child of the element and the item 1 of the array of the
    // page in the parent tree (zadani_jadro.md 4.12). The model of a snapshot is one for the
    // whole document; the changes of a skipped page are taken out of it (owner 2026-11-26,
    // step B0; until then a skipped page with changed tags was refused as a whole).
    const PDFDocument document = createTaggedTwoPagesDocument();
    const PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    const PDFContentStructureModelPointer changedModel = modelWithoutSequence(document, 1);
    QVERIFY(model != nullptr);
    QVERIFY(changedModel != nullptr);
    QVERIFY(changedModel->isModified());

    const PDFContentContainerPointer taggedPage = taggedPageWithoutSequence(document, 1);
    QVERIFY(taggedPage != nullptr);
    const PDFContentContainerPointer plainOriginal = buildPage(document, 1);
    const PDFContentNodeId plainPath = nodesOfKind(*plainOriginal, PDFContentNodeKind::Path)[0]->getId();
    const PDFContentContainerPointer plainBroken = setOverride(plainOriginal, plainPath, brokenOverride());
    const PDFContentContainerPointer plainValid = removeNode(plainOriginal, plainPath);
    QVERIFY(plainBroken != nullptr);

    const auto write = [&document](const std::vector<PDFContentContainerPointer>& containers, const PDFContentStructureModelPointer& structure, std::set<PDFInteger> skippedPages, bool* isBuilderUntouched)
    {
        PDFContentEditSnapshot snapshot = snapshotOf(containers);
        snapshot.structure = structure;
        Writer::Options options;
        options.skippedPages = std::move(skippedPages);

        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        *isBuilderUntouched = *builder.getStorage() == document.getStorage();
        return result;
    };
    bool isBuilderUntouched = false;

    // ---- An error of a page: nothing is written, the structure neither ----
    {
        const PDFContentWriteResult result = write({ taggedPage, plainBroken }, changedModel, { }, &isBuilderUntouched);
        QVERIFY(result.status == Status::PageErrors);
        QCOMPARE(result.errors.size(), size_t(1));
        QCOMPARE(result.errors.begin()->first, PDFInteger(1));
        QVERIFY(result.errors.begin()->second[0].nodeId == plainPath);
        QVERIFY(!result.isStructureChanged);
        QVERIFY(isBuilderUntouched);
    }

    // ---- The tagged page is skipped (the reason of a skip does not matter here). Its content
    //      stays as it is - with /MCID 1 and the text (Second) - so its tags stay too: the
    //      change of the key 0 (/StructParents of the page) is taken out of the model, which has
    //      no other change; no structure is written (ISO 32000-2, 14.7.5.4: the parent tree
    //      still names the Span for the identifier 1). The other page is written. ----
    {
        const PDFContentWriteResult result = write({ taggedPage, plainValid }, changedModel, { 0 }, &isBuilderUntouched);
        QVERIFY2(result.status == Status::Success, qPrintable(result.errors.empty() ? QString() : result.errors.cbegin()->second.front().text));
        QVERIFY(!result.isStructureChanged);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 1 }));
        QVERIFY(!isBuilderUntouched);
    }

    // ---- The page without tags is skipped: it has no key (/StructParents), so no change of the
    //      structure is its; the structure and the tagged page are written ----
    {
        const PDFContentWriteResult result = write({ taggedPage, plainBroken }, changedModel, { 1 }, &isBuilderUntouched);
        QVERIFY2(result.status == Status::Success, qPrintable(result.errors.empty() ? QString() : result.errors.cbegin()->second.front().text));
        QVERIFY(result.isStructureChanged);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    }

    // ---- A skipped page, which leaves no change out, refuses nothing: a page the snapshot has
    //      no container of, and a page, whose container is unchanged ----
    for (const PDFInteger skippedPage : { 5, 1 })
    {
        const PDFContentWriteResult result = write({ taggedPage, plainOriginal }, changedModel, { skippedPage }, &isBuilderUntouched);
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.isStructureChanged);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QVERIFY(!isBuilderUntouched);
    }

    // ---- Without a change of the structure a page is skipped as ever ----
    {
        const PDFContentWriteResult result = write({ taggedPage, plainBroken }, model, { 1 }, &isBuilderUntouched);
        QVERIFY(result.status == Status::Success);
        QVERIFY(!result.isStructureChanged);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
    }
}

void DocumentToolContentTest::writerStructureSkippedTaggedPage()
{
    // Two tagged pages (ISO 32000-2, 14.7): each has the content
    //   "/P << /MCID 0 >> BDC 0 0 10 10 re f EMC /P << /MCID 1 >> BDC 20 20 10 10 re f EMC
    //    /Span << /MCID 2 >> BDC 40 40 10 10 re f EMC"
    // and the structure
    //   root  /K [A B C]  /ParentTree << /Nums [ 0 [A A C]  1 [B B C] ] >>   (page 1 /StructParents 0, page 2 1)
    //   A     /S /P /Pg page1 /K [0 1]
    //   B     /S /P /Pg page2 /K [0 1]
    //   C     /S /Span /K [ << /Type /MCR /Pg page1 /MCID 2 >> << /Type /MCR /Pg page2 /MCID 2 >> ]
    // Sequences are deleted on both pages and the page 2 is skipped: its content stays, so its
    // tags stay (the parent tree item 1 as in the document, B not written); the changes of the
    // page 1 are written. C lists content of both pages: when the deletions change it for both
    // pages, they are separated child by child (writerStructureSkippedParagraphAcrossPages).
    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference page1 = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    const PDFObjectReference page2 = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    const QByteArray content = "/P << /MCID 0 >> BDC 0 0 10 10 re f EMC /P << /MCID 1 >> BDC 20 20 10 10 re f EMC /Span << /MCID 2 >> BDC 40 40 10 10 re f EMC";
    const PDFObjectReference root = documentBuilder.addObject(PDFObject::createInteger(0));
    const PDFObjectReference a = documentBuilder.addObject(PDFObject::createInteger(0));
    const PDFObjectReference b = documentBuilder.addObject(PDFObject::createInteger(0));
    const PDFObjectReference c = documentBuilder.addObject(PDFObject::createInteger(0));
    const auto ref = [](PDFObjectReference reference) { return PDFObject::createReference(reference); };
    const auto element = [&ref](const char* type, PDFObjectReference parent, PDFObjectReference page, PDFObject kids)
    {
        Documents::DictionaryEntries entries = { { "Type", PDFObject::createName("StructElem") }, { "S", PDFObject::createName(type) }, { "P", ref(parent) } };
        if (page.isValid())
        {
            entries.push_back({ "Pg", ref(page) });
        }
        entries.push_back({ "K", std::move(kids) });
        return Documents::createDictionaryObject(std::move(entries));
    };
    const auto reference2 = [&ref](PDFObjectReference page)
    {
        return Documents::createDictionaryObject({ { "Type", PDFObject::createName("MCR") }, { "Pg", ref(page) }, { "MCID", PDFObject::createInteger(2) } });
    };
    documentBuilder.setObject(a, element("P", root, page1, Documents::createIntegerArrayObject({ 0, 1 })));
    documentBuilder.setObject(b, element("P", root, page2, Documents::createIntegerArrayObject({ 0, 1 })));
    documentBuilder.setObject(c, element("Span", root, PDFObjectReference(), Documents::createArrayObject({ reference2(page1), reference2(page2) })));
    documentBuilder.setObject(root, Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructTreeRoot") },
                                                                         { "K", Documents::createArrayObject({ ref(a), ref(b), ref(c) }) },
                                                                         { "ParentTree", Documents::createDictionaryObject({ { "Nums", Documents::createArrayObject({
                                                                             PDFObject::createInteger(0), Documents::createArrayObject({ ref(a), ref(a), ref(c) }),
                                                                             PDFObject::createInteger(1), Documents::createArrayObject({ ref(b), ref(b), ref(c) }) }) } }) },
                                                                         { "ParentTreeNextKey", PDFObject::createInteger(2) } }));
    for (const auto& page : { std::make_pair(page1, 0), std::make_pair(page2, 1) })
    {
        documentBuilder.mergeTo(page.first, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ }) },
                                                                                { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), content) },
                                                                                { "StructParents", PDFObject::createInteger(page.second) } }));
    }
    documentBuilder.mergeTo(documentBuilder.getCatalogReference(), Documents::createDictionaryObject({ { "StructTreeRoot", ref(root) } }));
    const PDFDocument document = documentBuilder.build();

    const PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(model->getIssues().empty());

    // The page without the sequences of the identifiers
    const auto pageWithout = [&document](PDFInteger pageIndex, std::set<PDFInteger> mcids)
    {
        PDFContentContainerPointer container = buildPage(document, pageIndex);
        for (const PDFContentNodePointer& node : nodesOfKind(*container, PDFContentNodeKind::MarkedContent))
        {
            if (node->getData().mcid && mcids.count(*node->getData().mcid))
            {
                container = removeNode(container, node->getId());
            }
        }
        return container;
    };

    struct Written
    {
        PDFContentWriteResult result;
        PDFDocument document;
    };
    const auto write = [&](const std::vector<std::pair<PDFInteger, PDFInteger>>& deleted, PDFInteger skippedPage, Written* written)
    {
        std::vector<PDFContentStructureChange> changes;
        std::map<PDFInteger, std::set<PDFInteger>> mcids;
        for (const auto& sequence : deleted)
        {
            changes.push_back(model->getChangeForDelete(sequence.first, sequence.second, true));
            QVERIFY(changes.back().action == PDFContentStructureChange::Action::RemoveSequence);
            mcids[sequence.first].insert(sequence.second);
        }

        std::vector<PDFContentContainerPointer> containers;
        for (const auto& page : mcids)
        {
            containers.push_back(pageWithout(page.first, page.second));
        }
        PDFContentEditSnapshot snapshot = snapshotOf(containers);
        snapshot.structure = model->applied(changes);
        Writer::Options options;
        options.skippedPages = { skippedPage };

        PDFDocumentBuilder builder(&document);
        written->result = Writer::write(&document, snapshot, &builder, options, nullptr, nullptr);
        written->document = builder.build();
    };

    const auto nums = [](const PDFDocument& writtenDocument, int index)
    {
        const PDFDictionary* catalog = writtenDocument.getDictionaryFromObject(writtenDocument.getTrailerDictionary()->get("Root"));
        const PDFDictionary* structureRoot = writtenDocument.getDictionaryFromObject(catalog->get("StructTreeRoot"));
        const PDFObject& numbers = writtenDocument.getObject(writtenDocument.getDictionaryFromObject(structureRoot->get("ParentTree"))->get("Nums"));
        QStringList items;
        for (const PDFObject& item : *writtenDocument.getObject(numbers.getArray()->getItem(size_t(2 * index + 1))).getArray())
        {
            items << (item.isReference() ? QString("%1 R").arg(item.getReference().objectNumber) : item.isNull() ? QString("null") : QString("?"));
        }
        return items;
    };
    const auto kids = [](const PDFDocument& writtenDocument, PDFObjectReference reference)
    {
        QStringList items;
        for (const PDFObject& item : *writtenDocument.getObject(writtenDocument.getDictionaryFromObject(PDFObject::createReference(reference))->get("K")).getArray())
        {
            items << (item.isInt() ? QString::number(item.getInteger()) : item.isDictionary() ? QString("MCR %1").arg(item.getDictionary()->get("Pg").getReference().objectNumber) : QString("?"));
        }
        return items;
    };
    const QString A = QString("%1 R").arg(a.objectNumber);
    const QString B = QString("%1 R").arg(b.objectNumber);
    const QString C = QString("%1 R").arg(c.objectNumber);
    const QString mcr2 = QString("MCR %1").arg(page2.objectNumber);

    // ---- The sequence 0 of both pages; the page 2 is skipped: A loses 0, B and the item 1 stay ----
    {
        Written written;
        write({ { 0, 0 }, { 1, 0 } }, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(written.result.errors.empty() ? QString() : written.result.errors.cbegin()->second.front().text));
        QVERIFY(written.result.isStructureChanged);
        QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QCOMPARE(kids(written.document, a), QStringList({ "1" }));
        QVERIFY(written.document.getObjectByReference(b) == document.getObjectByReference(b));
        QVERIFY(written.document.getObjectByReference(c) == document.getObjectByReference(c));
        QCOMPARE(nums(written.document, 0), QStringList({ "null", A, C }));
        QCOMPARE(nums(written.document, 1), QStringList({ B, B, C }));
        QCOMPARE(pageContent(written.document, 1), content);
    }

    // ---- The sequence 2 of the page 1 and 0 of the page 2; the page 2 is skipped: C is
    //      changed by the page 1 only and keeps its child of the page 2 ----
    {
        Written written;
        write({ { 0, 2 }, { 1, 0 } }, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(written.result.errors.empty() ? QString() : written.result.errors.cbegin()->second.front().text));
        QCOMPARE(kids(written.document, c), QStringList({ mcr2 }));
        QVERIFY(written.document.getObjectByReference(a) == document.getObjectByReference(a));
        QVERIFY(written.document.getObjectByReference(b) == document.getObjectByReference(b));
        QCOMPARE(nums(written.document, 0), QStringList({ A, A, "null" }));
        QCOMPARE(nums(written.document, 1), QStringList({ B, B, C }));
    }

    // ---- The sequence 2 of both pages: C is changed by both; the changes are separated child by
    //      child (review R2-3): C keeps the child of the skipped page and loses the other one ----
    const QString mcr1 = QString("MCR %1").arg(page1.objectNumber);
    for (const PDFInteger skippedPage : { 0, 1 })
    {
        Written written;
        write({ { 0, 2 }, { 1, 2 } }, skippedPage, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(written.result.errors.empty() ? QString() : written.result.errors.cbegin()->second.front().text));
        QVERIFY(written.result.isStructureChanged);
        QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 1 - skippedPage }));
        QCOMPARE(kids(written.document, c), QStringList({ skippedPage == 0 ? mcr1 : mcr2 }));
        QCOMPARE(nums(written.document, 0), skippedPage == 0 ? QStringList({ A, A, C }) : QStringList({ A, A, "null" }));
        QCOMPARE(nums(written.document, 1), skippedPage == 0 ? QStringList({ B, B, "null" }) : QStringList({ B, B, C }));
        QVERIFY(written.document.getObjectByReference(a) == document.getObjectByReference(a));
        QVERIFY(written.document.getObjectByReference(b) == document.getObjectByReference(b));
        QCOMPARE(pageContent(written.document, skippedPage), content);
    }

    // The same without a skipped page: C loses both children
    {
        Written written;
        write({ { 0, 2 }, { 1, 2 } }, 5, &written);
        QVERIFY(written.result.status == Status::Success);
        QVERIFY(written.document.getDictionaryFromObject(PDFObject::createReference(c))->get("K").isNull());
        QCOMPARE(nums(written.document, 0), QStringList({ A, A, "null" }));
        QCOMPARE(nums(written.document, 1), QStringList({ B, B, "null" }));
    }
}

void DocumentToolContentTest::writerStructureSkippedParagraphAcrossPages()
{
    // Review R2-3: a paragraph across a page break is one element, whose /K lists tagged content
    // of both pages (ISO 32000-2, 14.7.5). The edits are made by the commands of a session, the
    // page skipped keeps its content and its tags, the written page gets its changes, /K keeps
    // the reading order. The document (written by hand):
    //   page 1 (/StructParents 0): /P << /MCID 0 >> BDC 0 0 10 10 re f 2 2 5 5 re f EMC
    //                              /P << /MCID 1 >> BDC 20 20 10 10 re f EMC
    //                              /P << /MCID 2 >> BDC 40 40 10 10 re f EMC
    //   page 2 (/StructParents 1): the same and /P << /MCID 3 >> BDC 60 60 10 10 re f 62 62 5 5 re f EMC
    //   root /K [P Q] /ParentTree << /Nums [ 0 [P P Q]  1 [P P Q Q] ] >> /ParentTreeNextKey 2
    //   P    /S /P /Pg page1 /K [0 1 R(p2,0) R(p2,1)]       (R(p,m) = << /Type /MCR /Pg p /MCID m >>)
    //   Q    /S /P /Pg page1 /K [R(p2,2) 2 R(p2,3)]         (content of the page 1 between content of the page 2)
    // A split (Bring to Front of the first path of a sequence of two paths) gives the rest the
    // identifier above the array of the page (3 on the page 1, 4 on the page 2), right behind the
    // original in /K and in the content.
    const QByteArray content1 = "/P << /MCID 0 >> BDC 0 0 10 10 re f 2 2 5 5 re f EMC /P << /MCID 1 >> BDC 20 20 10 10 re f EMC /P << /MCID 2 >> BDC 40 40 10 10 re f EMC";
    const QByteArray content2 = content1 + " /P << /MCID 3 >> BDC 60 60 10 10 re f 62 62 5 5 re f EMC";

    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference page1 = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    const PDFObjectReference page2 = documentBuilder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    const PDFObjectReference root = documentBuilder.addObject(PDFObject::createInteger(0));
    const PDFObjectReference p = documentBuilder.addObject(PDFObject::createInteger(0));
    const PDFObjectReference q = documentBuilder.addObject(PDFObject::createInteger(0));
    const auto ref = [](PDFObjectReference reference) { return PDFObject::createReference(reference); };
    const auto mcr = [&ref](PDFObjectReference page, PDFInteger mcid)
    {
        return Documents::createDictionaryObject({ { "Type", PDFObject::createName("MCR") }, { "Pg", ref(page) }, { "MCID", PDFObject::createInteger(mcid) } });
    };
    const auto element = [&ref](PDFObjectReference parent, PDFObjectReference page, PDFObject kids)
    {
        return Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructElem") }, { "S", PDFObject::createName("P") }, { "P", ref(parent) },
                                                   { "Pg", ref(page) }, { "K", std::move(kids) } });
    };
    documentBuilder.setObject(p, element(root, page1, Documents::createArrayObject({ PDFObject::createInteger(0), PDFObject::createInteger(1), mcr(page2, 0), mcr(page2, 1) })));
    documentBuilder.setObject(q, element(root, page1, Documents::createArrayObject({ mcr(page2, 2), PDFObject::createInteger(2), mcr(page2, 3) })));
    documentBuilder.setObject(root, Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructTreeRoot") },
                                                                         { "K", Documents::createArrayObject({ ref(p), ref(q) }) },
                                                                         { "ParentTree", Documents::createDictionaryObject({ { "Nums", Documents::createArrayObject({
                                                                             PDFObject::createInteger(0), Documents::createArrayObject({ ref(p), ref(p), ref(q) }),
                                                                             PDFObject::createInteger(1), Documents::createArrayObject({ ref(p), ref(p), ref(q), ref(q) }) }) } }) },
                                                                         { "ParentTreeNextKey", PDFObject::createInteger(2) } }));
    documentBuilder.mergeTo(page1, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ }) },
                                                                       { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), content1) },
                                                                       { "StructParents", PDFObject::createInteger(0) } }));
    documentBuilder.mergeTo(page2, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ }) },
                                                                       { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), content2) },
                                                                       { "StructParents", PDFObject::createInteger(1) } }));
    documentBuilder.mergeTo(documentBuilder.getCatalogReference(), Documents::createDictionaryObject({ { "StructTreeRoot", ref(root) } }));
    const PDFDocument document = documentBuilder.build();
    QVERIFY(PDFContentStructureModel::read(&document, nullptr)->getIssues().empty());

    // The edits of one case: a delete of a sequence (its marked content), a split of a sequence
    enum class Edit { Delete, Split };
    struct Step
    {
        Edit edit;
        PDFInteger pageIndex;
        PDFInteger mcid;
    };

    struct Written
    {
        PDFContentWriteResult result;
        PDFDocument document;
    };
    const auto write = [&document](const std::vector<Step>& steps, PDFInteger skippedPage, Written* written)
    {
        PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
        QVERIFY(session.loadStructure(nullptr));
        for (const Step& step : steps)
        {
            QString errorText;
            const PDFContentContainerKey key = session.getPageKey(step.pageIndex);
            QVERIFY(key.isValid() && (session.getContainer(key) || session.loadContainer(key, nullptr, &errorText)));
            const PDFContentContainerPointer container = session.getContainer(key);

            PDFContentNodePointer sequence;
            for (const PDFContentNodePointer& node : nodesOfKind(*container, PDFContentNodeKind::MarkedContent))
            {
                if (node->getData().mcid == step.mcid)
                {
                    sequence = node;
                }
            }
            QVERIFY(sequence != nullptr);

            PDFContentCommandPointer command;
            if (step.edit == Edit::Delete)
            {
                command = PDFContentCommandFactory::createDelete({ PDFContentNodeReference{ key, sequence->getId() } });
            }
            else
            {
                QCOMPARE(sequence->getChildren().size(), size_t(2));
                command = PDFContentCommandFactory::createReorder({ PDFContentNodeReference{ key, sequence->getChildren().front()->getId() } },
                                                                  PDFContentNodeReference{ key, container->getRoot()->getId() }, -1,
                                                                  PDFContentCommandFactory::ReorderMode::KeepLayerAndTag);
            }
            QVERIFY2(session.execute(command, &errorText), qPrintable(errorText));
        }

        const PDFContentEditSnapshotPointer snapshot = session.createSnapshot();
        QVERIFY(snapshot != nullptr);
        Writer::Options options;
        options.skippedPages = { skippedPage };

        PDFDocumentBuilder builder(&document);
        written->result = Writer::write(&document, *snapshot, &builder, options, nullptr, nullptr);
        written->document = builder.build();
    };

    // /K of an element: "<integer>", "MCR <page object> <MCID>"
    const auto kids = [](const PDFDocument& writtenDocument, PDFObjectReference reference)
    {
        QStringList items;
        for (const PDFObject& item : *writtenDocument.getObject(writtenDocument.getDictionaryFromObject(PDFObject::createReference(reference))->get("K")).getArray())
        {
            const PDFDictionary* dictionary = item.isDictionary() ? item.getDictionary() : nullptr;
            items << (item.isInt() ? QString::number(item.getInteger()) : dictionary ? QString("MCR %1 %2").arg(dictionary->get("Pg").getReference().objectNumber).arg(dictionary->get("MCID").getInteger()) : QString("?"));
        }
        return items;
    };
    const auto nums = [](const PDFDocument& writtenDocument, int index)
    {
        const PDFDictionary* catalog = writtenDocument.getDictionaryFromObject(writtenDocument.getTrailerDictionary()->get("Root"));
        const PDFDictionary* structureRoot = writtenDocument.getDictionaryFromObject(catalog->get("StructTreeRoot"));
        const PDFObject& numbers = writtenDocument.getObject(writtenDocument.getDictionaryFromObject(structureRoot->get("ParentTree"))->get("Nums"));
        QStringList items;
        for (const PDFObject& item : *writtenDocument.getObject(numbers.getArray()->getItem(size_t(2 * index + 1))).getArray())
        {
            items << (item.isReference() ? QString("%1 R").arg(item.getReference().objectNumber) : item.isNull() ? QString("null") : QString("?"));
        }
        return items;
    };
    const QString P = QString("%1 R").arg(p.objectNumber);
    const QString Q = QString("%1 R").arg(q.objectNumber);
    const auto r2 = [&page2](int mcid) { return QString("MCR %1 %2").arg(page2.objectNumber).arg(mcid); };
    const auto errorOf = [](const Written& written) { return written.result.errors.empty() ? QString() : written.result.errors.cbegin()->second.front().text; };

    // ---- A removal on each page: the page 2 skipped, then the page 1 skipped ----
    {
        Written written;
        write({ { Edit::Delete, 0, 1 }, { Edit::Delete, 1, 0 } }, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(errorOf(written)));
        QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QCOMPARE(kids(written.document, p), QStringList({ "0", r2(0), r2(1) }));
        QCOMPARE(nums(written.document, 0), QStringList({ P, "null", Q }));
        QCOMPARE(nums(written.document, 1), QStringList({ P, P, Q, Q }));
        QCOMPARE(pageContent(written.document, 1), content2);
        QVERIFY(written.document.getObjectByReference(q) == document.getObjectByReference(q));
    }
    {
        Written written;
        write({ { Edit::Delete, 0, 1 }, { Edit::Delete, 1, 0 } }, 0, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(errorOf(written)));
        QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 1 }));
        QCOMPARE(kids(written.document, p), QStringList({ "0", "1", r2(1) }));
        QCOMPARE(nums(written.document, 0), QStringList({ P, P, Q }));
        QCOMPARE(nums(written.document, 1), QStringList({ "null", P, Q, Q }));
        QCOMPARE(pageContent(written.document, 0), content1);
    }

    // ---- A split on the written page 2, a removal on the skipped page 1: the new part right
    //      behind its original, the 0 of the page 1 stays ----
    {
        Written written;
        write({ { Edit::Split, 1, 0 }, { Edit::Delete, 0, 0 } }, 0, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(errorOf(written)));
        QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 1 }));
        QCOMPARE(kids(written.document, p), QStringList({ "0", "1", r2(0), r2(4), r2(1) }));
        QCOMPARE(nums(written.document, 0), QStringList({ P, P, Q }));
        QCOMPARE(nums(written.document, 1), QStringList({ P, P, Q, Q, P }));
        QCOMPARE(pageContent(written.document, 0), content1);
        QVERIFY(pageContent(written.document, 1).contains("/MCID 4"));
    }

    // ---- A split on the written page 1, a removal on the skipped page 2 ----
    {
        Written written;
        write({ { Edit::Split, 0, 0 }, { Edit::Delete, 1, 1 } }, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(errorOf(written)));
        QCOMPARE(written.result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QCOMPARE(kids(written.document, p), QStringList({ "0", "3", "1", r2(0), r2(1) }));
        QCOMPARE(nums(written.document, 0), QStringList({ P, P, Q, P }));
        QCOMPARE(nums(written.document, 1), QStringList({ P, P, Q, Q }));
        QCOMPARE(pageContent(written.document, 1), content2);
    }

    // ---- Still refused: on the page 2 the sequence 3 is split (the rest gets 4) and its
    //      original deleted, on the page 1 the 2 is deleted: Q is [R(p2,2) R(p2,4)]. Without the
    //      page 1 the restored 2 belongs in front of R(p2,4), if it came from R(p2,3), behind it,
    //      if it came from R(p2,2): the page 1 skipped is refused under the page 1, nothing is
    //      written. Without the page 2 it is separable. ----
    {
        Written written;
        write({ { Edit::Split, 1, 3 }, { Edit::Delete, 1, 3 }, { Edit::Delete, 0, 2 } }, 0, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY(written.result.status == Status::PageErrors);
        QCOMPARE(written.result.errors.size(), size_t(1));
        QCOMPARE(written.result.errors.begin()->first, PDFInteger(0));
        QVERIFY2(errorOf(written).contains(QStringLiteral("cannot be left out alone")), qPrintable(errorOf(written)));
        QVERIFY(!written.result.isStructureChanged);
    }
    {
        Written written;
        write({ { Edit::Split, 1, 3 }, { Edit::Delete, 1, 3 }, { Edit::Delete, 0, 2 } }, 1, &written);
        if (QTest::currentTestFailed())
        {
            return;
        }
        QVERIFY2(written.result.status == Status::Success, qPrintable(errorOf(written)));
        QCOMPARE(kids(written.document, q), QStringList({ r2(2), r2(3) }));
        QCOMPARE(nums(written.document, 0), QStringList({ P, P, "null" }));
        QCOMPARE(nums(written.document, 1), QStringList({ P, P, Q, Q }));
    }
}

void DocumentToolContentTest::writerStructureOutdated_data()
{
    QTest::addColumn<int>("variant");

    QTest::newRow("the same document") << 0;
    QTest::newRow("another text of an element") << 1;
    QTest::newRow("an element without its child") << 2;
    QTest::newRow("another array of the page in the parent tree") << 3;
    QTest::newRow("one key more in the parent tree") << 4;
    QTest::newRow("a larger next key of the parent tree") << 5;
    QTest::newRow("one element more") << 6;
    QTest::newRow("another object of the root") << 7;
    QTest::newRow("one key more below the next key") << 8;
    QTest::newRow("another attribute of the changed element") << 9;
    QTest::newRow("another content of an array the model has not changed") << 10;
}

void DocumentToolContentTest::writerStructureOutdated()
{
    QFETCH(int, variant);

    // The model of a snapshot is read from the tagged document and changed (the sequence 1 is
    // deleted). Meanwhile the structure of the document changes - by another run - while the
    // content of the page stays: the fingerprint of the page still matches. The writer would
    // write the changed elements and the whole parent tree from the old model and take the
    // other change back. It reads the structure again and compares.
    //
    // The row 8 starts from a document, whose /ParentTreeNextKey is 5 (the keys 2, 3 and 4 are
    // free): another run takes the key 3, and the next key stays. The row 10 starts from a
    // document with the key 3 already there: its value is an array [P], an object of its own,
    // which the model does not change - another run changes the array at its reference to
    // [Figure], so the value of the key (the reference) stays.
    PDFDocument original = Documents::createTaggedDocument();
    PDFObject otherArray;
    if (variant == 8 || variant == 10)
    {
        PDFDocumentBuilder baseBuilder(&original);
        const PDFObjectReference rootReference = original.getDictionaryFromObject(original.getTrailerDictionary()->get("Root"))->get("StructTreeRoot").getReference();
        PDFDictionaryBuilder rootDictionary(*original.getObjectByReference(rootReference).getDictionary());
        rootDictionary.setEntry(PDFInplaceOrMemoryString("ParentTreeNextKey"), PDFObject::createInteger(5));
        baseBuilder.setObject(rootReference, PDFObject::createDictionary(std::move(rootDictionary)));

        if (variant == 10)
        {
            const PDFObject parentTreeReference = original.getObjectByReference(rootReference).getDictionary()->get("ParentTree");
            const PDFObject documentReference = original.getObjectByReference(rootReference).getDictionary()->get("K").getArray()->getItem(0);
            const PDFArray* documentElements = original.getDictionaryFromObject(documentReference)->get("K").getArray();
            otherArray = Documents::addObject(&baseBuilder, Documents::createArrayObject({ documentElements->getItem(0) }));
            baseBuilder.setObject(parentTreeReference.getReference(), Documents::createDictionaryObject({ { "Nums", Documents::createArrayObject({
                PDFObject::createInteger(0), Documents::createArrayObject({ documentElements->getItem(0), documentElements->getItem(1), PDFObject::createNull(), documentElements->getItem(2) }),
                PDFObject::createInteger(1), documentElements->getItem(3),
                PDFObject::createInteger(3), otherArray }) } }));
        }
        original = baseBuilder.build();
    }
    const PDFContentStructureModelPointer changedModel = modelWithoutSequence(original, 1);
    const PDFContentContainerPointer taggedPage = taggedPageWithoutSequence(original, 1);
    QVERIFY(changedModel != nullptr);
    QVERIFY(taggedPage != nullptr);

    // The objects of the structure (Documents::createTaggedDocument): the root with /K [Document],
    // Document with /K [P Span Figure Link], the parent tree /Nums [ 0 [P Span null Figure] 1 Link ]
    const PDFDictionary* catalog = original.getDictionaryFromObject(original.getTrailerDictionary()->get("Root"));
    const PDFObject rootValue = catalog->get("StructTreeRoot");
    const PDFDictionary* root = original.getDictionaryFromObject(rootValue);
    const PDFObject parentTreeValue = root->get("ParentTree");
    const PDFObject documentElement = root->get("K").getArray()->getItem(0);
    const PDFArray* elements = original.getDictionaryFromObject(documentElement)->get("K").getArray();
    const PDFObject paragraph = elements->getItem(0);
    const PDFObject span = elements->getItem(1);
    const PDFObject figure = elements->getItem(2);
    const PDFObject link = elements->getItem(3);
    QVERIFY(rootValue.isReference() && parentTreeValue.isReference() && paragraph.isReference() && figure.isReference());

    PDFDocumentBuilder documentBuilder(&original);
    const auto setEntry = [&documentBuilder](const PDFObject& reference, const char* key, PDFObject value)
    {
        PDFDictionaryBuilder dictionary(*documentBuilder.getObjectByReference(reference.getReference()).getDictionary());
        if (value.isNull())
        {
            dictionary.removeEntry(key);
        }
        else
        {
            dictionary.setEntry(PDFInplaceOrMemoryString(key), std::move(value));
        }
        documentBuilder.setObject(reference.getReference(), PDFObject::createDictionary(std::move(dictionary)));
    };

    switch (variant)
    {
        case 1:
            setEntry(figure, "Alt", PDFObject::createString("Circle"));
            break;

        case 2:
            // As after another run has deleted the text (First): /P has no /K any more
            setEntry(paragraph, "K", PDFObject());
            break;

        case 3:
            setEntry(parentTreeValue, "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), Documents::createArrayObject({ paragraph, span, figure, figure }),
                                                                             PDFObject::createInteger(1), link }));
            break;

        case 4:
            setEntry(parentTreeValue, "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), Documents::createArrayObject({ paragraph, span, PDFObject::createNull(), figure }),
                                                                             PDFObject::createInteger(1), link, PDFObject::createInteger(2), link }));
            setEntry(rootValue, "ParentTreeNextKey", PDFObject::createInteger(3));
            break;

        case 5:
            // Another run has given the keys 2 ... 6 away: a key allocated from the old model
            // would be given twice
            setEntry(rootValue, "ParentTreeNextKey", PDFObject::createInteger(7));
            break;

        case 6:
        {
            const PDFObject section = Documents::addObject(&documentBuilder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructElem") },
                                                                                                                 { "S", PDFObject::createName("Sect") },
                                                                                                                 { "P", documentElement } }));
            setEntry(documentElement, "K", Documents::createArrayObject({ paragraph, span, figure, link, section }));
            break;
        }

        case 7:
        {
            const PDFObject rootCopy = Documents::addObject(&documentBuilder, original.getObject(rootValue));
            setEntry(PDFObject::createReference(documentBuilder.getCatalogReference()), "StructTreeRoot", rootCopy);
            break;
        }

        case 8:
            setEntry(parentTreeValue, "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), Documents::createArrayObject({ paragraph, span, PDFObject::createNull(), figure }),
                                                                             PDFObject::createInteger(1), link, PDFObject::createInteger(3), link }));
            break;

        case 9:
            // /Span is the element the model has changed (it has lost its child): its children
            // cannot be compared any more, but the keys the editor never changes can
            // (here an attribute object, ISO 32000-2, 14.7.6)
            setEntry(span, "A", Documents::createDictionaryObject({ { "O", PDFObject::createName("Layout") } }));
            break;

        case 10:
            documentBuilder.setObject(otherArray.getReference(), Documents::createArrayObject({ figure }));
            break;

        default:
            break;
    }
    const PDFDocument document = documentBuilder.build();

    // The content of the page is the same in both documents: the fingerprint of the container matches
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&document, taggedPage->getKey()), taggedPage->getFingerprint());

    PDFContentEditSnapshot snapshot = snapshotOf({ taggedPage });
    snapshot.structure = changedModel;

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    if (variant == 0)
    {
        QVERIFY(result.status == Status::Success);
        QVERIFY(result.isStructureChanged);
        QVERIFY(!result.isStructureOutdated);
        return;
    }

    QVERIFY(result.status == Status::DocumentChanged);
    QVERIFY(result.isStructureOutdated);
    QVERIFY(result.changedContainers.empty());
    QVERIFY(result.errors.empty());
    QVERIFY(result.pagesChanged.empty());
    QVERIFY(!result.isStructureChanged);
    QVERIFY(*builder.getStorage() == document.getStorage());

    // The content alone (no change of the structure) is still written into this document
    PDFDocumentBuilder contentBuilder(&document);
    QVERIFY(Writer::write(&document, snapshotOf({ taggedPage }), &contentBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);

    // The tool says, what it says for a changed page
    Tool tool;
    const PDFDocumentToolResult toolResult = tool.run(contextOf(document), applyParameters(std::make_shared<const PDFContentEditSnapshot>(snapshot)), nullptr, nullptr);
    QVERIFY(toolResult.status == PDFDocumentToolResultStatus::Failed);
    QCOMPARE(toolResult.message, QStringLiteral("The document changed in the meantime."));
    QVERIFY(toolResult.documents.empty());
}

void DocumentToolContentTest::writerStructureTwice()
{
    // zadani.md 12.7: a tool writes its pages in parts, each part a snapshot into the same
    // builder. Two parts change the structure - each with a model read from the document, each
    // with one sequence deleted: the first one /Span << /MCID 1 >> of the tagged page, the second
    // one /Figure << /MCID 3 >>. The second model does not know the first change: written, it
    // would build the parent tree from the document again and the item 1 would be back. One
    // builder takes one change of the structure; the second one is refused before anything of
    // its snapshot is written.
    const PDFDocument document = createTaggedTwoPagesDocument();

    PDFContentEditSnapshot first = snapshotOf({ taggedPageWithoutSequence(document, 1) });
    first.structure = modelWithoutSequence(document, 1);
    QVERIFY(first.structure != nullptr);

    const PDFContentContainerPointer plainPage = buildPage(document, 1);
    const PDFContentContainerPointer plainEdited = removeNode(plainPage, nodesOfKind(*plainPage, PDFContentNodeKind::Path)[0]->getId());
    PDFContentEditSnapshot second = snapshotOf({ plainEdited });
    second.structure = modelWithoutSequence(document, 3);
    QVERIFY(second.structure != nullptr);
    QVERIFY(second.structure->isModified());

    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult firstResult = Writer::write(&document, first, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(firstResult.status == Status::Success);
    QVERIFY(firstResult.isStructureChanged);
    const PDFObjectStorage afterFirst = *builder.getStorage();

    const PDFContentWriteResult secondResult = Writer::write(&document, second, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(secondResult.status == Status::PageErrors);
    QCOMPARE(secondResult.errors.size(), size_t(1));
    QCOMPARE(secondResult.errors.begin()->first, PDFInteger(-1));
    QCOMPARE(secondResult.errors.begin()->second.size(), size_t(1));
    QCOMPARE(secondResult.errors.begin()->second[0].text, QStringLiteral("The tags were changed in the document, which is being written."));
    QVERIFY(!secondResult.isStructureChanged);
    QVERIFY(secondResult.pagesChanged.empty());

    // Nothing of the second snapshot is in the builder: neither its structure nor its page
    QVERIFY(*builder.getStorage() == afterFirst);

    // The same page without a change of the structure goes into the same builder
    const PDFContentWriteResult contentResult = Writer::write(&document, snapshotOf({ plainEdited }), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(contentResult.status == Status::Success);
    QCOMPARE(contentResult.pagesChanged, std::vector<PDFInteger>({ 1 }));

    // The written document has the first change of the structure: /Nums [ 0 [P null null Figure] 1 Link ]
    const PDFDocument written = builder.build();
    const PDFDictionary* originalRoot = document.getDictionaryFromObject(document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"))->get("StructTreeRoot"));
    const PDFArray* originalItems = document.getObject(document.getObject(document.getDictionaryFromObject(originalRoot->get("ParentTree"))->get("Nums")).getArray()->getItem(1)).getArray();
    const PDFDictionary* root = written.getDictionaryFromObject(written.getDictionaryFromObject(written.getTrailerDictionary()->get("Root"))->get("StructTreeRoot"));
    const PDFObject& numbers = written.getObject(written.getDictionaryFromObject(root->get("ParentTree"))->get("Nums"));
    QVERIFY(numbers.isArray());
    const PDFObject& items = written.getObject(numbers.getArray()->getItem(1));
    QVERIFY(items.isArray());
    QCOMPARE(items.getArray()->getCount(), size_t(4));
    QVERIFY(items.getArray()->getItem(0) == originalItems->getItem(0));
    QVERIFY(items.getArray()->getItem(1).isNull());
    QVERIFY(items.getArray()->getItem(2).isNull());
    QVERIFY(items.getArray()->getItem(3) == originalItems->getItem(3));
    QCOMPARE(pageContent(written, 1), QByteArray("0 1 0 rg"));

    // The second snapshot alone is written into a builder of its own
    PDFDocumentBuilder otherBuilder(&document);
    const PDFContentWriteResult otherResult = Writer::write(&document, second, &otherBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(otherResult.status == Status::Success);
    QVERIFY(otherResult.isStructureChanged);
}

void DocumentToolContentTest::writerStructureParentTreeWalk()
{
    // The structure of the builder must be the one the model was read from. The root and the
    // elements are compared by their objects; the parent tree is a number tree (ISO 32000-2,
    // 7.9.7), whose nodes and arrays are objects of their own: the writer walks them. Here only
    // such an object differs in the builder (somebody has written it before), the root and
    // every element are the same.
    const auto write = [](const PDFDocument& document, const PDFObject& changedObject, const PDFObject& newValue, Status* status, QString* errorText, bool* isBuilderUntouched)
    {
        PDFContentEditSnapshot snapshot = snapshotOf({ taggedPageWithoutSequence(document, 1) });
        snapshot.structure = modelWithoutSequence(document, 1);
        QVERIFY(snapshot.structure != nullptr);
        QVERIFY(snapshot.structure->isModified());

        PDFDocumentBuilder builder(&document);
        if (changedObject.isReference())
        {
            builder.setObject(changedObject.getReference(), newValue);
        }
        const PDFObjectStorage before = *builder.getStorage();

        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, nullptr);
        *status = result.status;
        *errorText = (result.errors.size() == 1 && result.errors.begin()->first == -1 && result.errors.begin()->second.size() == 1) ? result.errors.begin()->second[0].text : QString();
        *isBuilderUntouched = *builder.getStorage() == before;
    };

    const QString changedText = QStringLiteral("The tags were changed in the document, which is being written.");
    Status status = Status::Canceled;
    QString errorText;
    bool isBuilderUntouched = false;

    // The objects of the tagged document
    const PDFDocument tagged = Documents::createTaggedDocument();
    const PDFDictionary* root = tagged.getDictionaryFromObject(tagged.getDictionaryFromObject(tagged.getTrailerDictionary()->get("Root"))->get("StructTreeRoot"));
    const PDFObject parentTree = root->get("ParentTree");
    const PDFArray* elements = tagged.getDictionaryFromObject(root->get("K").getArray()->getItem(0))->get("K").getArray();
    const PDFObject paragraph = elements->getItem(0);
    const PDFObject span = elements->getItem(1);
    const PDFObject figure = elements->getItem(2);
    const PDFObject link = elements->getItem(3);
    const PDFObject pageArray = Documents::createArrayObject({ paragraph, span, PDFObject::createNull(), figure });
    const PDFObject otherPageArray = Documents::createArrayObject({ paragraph, span, PDFObject::createNull(), paragraph });

    // ---- The only node of the parent tree: /Nums [ 0 [P Span null Figure] 1 Link ] ----
    write(tagged, PDFObject(), PDFObject(), &status, &errorText, &isBuilderUntouched);
    QVERIFY(status == Status::Success);

    write(tagged, parentTree, Documents::createDictionaryObject({ { "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), otherPageArray, PDFObject::createInteger(1), link }) } }),
          &status, &errorText, &isBuilderUntouched);
    QVERIFY(status == Status::PageErrors);
    QCOMPARE(errorText, changedText);
    QVERIFY(isBuilderUntouched);

    // ---- Only an element differs (the Figure with another replacement text); only the root
    //      (one key more); only the entry of the catalog (it names another dictionary) ----
    {
        PDFDictionaryBuilder otherFigure(*tagged.getObject(figure).getDictionary());
        otherFigure.setEntry(PDFInplaceOrMemoryString("Alt"), PDFObject::createString("Circle"));
        write(tagged, figure, PDFObject::createDictionary(std::move(otherFigure)), &status, &errorText, &isBuilderUntouched);
        QVERIFY(status == Status::PageErrors);
        QCOMPARE(errorText, changedText);
        QVERIFY(isBuilderUntouched);

        const PDFObject catalogValue = tagged.getTrailerDictionary()->get("Root");
        const PDFObject rootValue = tagged.getDictionaryFromObject(catalogValue)->get("StructTreeRoot");
        PDFDictionaryBuilder otherRoot(*root);
        otherRoot.setEntry(PDFInplaceOrMemoryString("Lang"), PDFObject::createString("en"));
        write(tagged, rootValue, PDFObject::createDictionary(std::move(otherRoot)), &status, &errorText, &isBuilderUntouched);
        QVERIFY(status == Status::PageErrors);
        QCOMPARE(errorText, changedText);
        QVERIFY(isBuilderUntouched);

        PDFDictionaryBuilder otherCatalog(*tagged.getDictionaryFromObject(catalogValue));
        otherCatalog.setEntry(PDFInplaceOrMemoryString("StructTreeRoot"), root->get("K").getArray()->getItem(0));
        write(tagged, catalogValue, PDFObject::createDictionary(std::move(otherCatalog)), &status, &errorText, &isBuilderUntouched);
        QVERIFY(status == Status::PageErrors);
        QCOMPARE(errorText, changedText);
        QVERIFY(isBuilderUntouched);
    }

    // ---- A tree of three nodes, hostile: the root node lists the leaf twice (a node shared by
    //      two paths) and a node, whose only kid is the root node again (a cycle); the array of
    //      the page is an object of its own:
    //        N0  /Kids [N1 N1 N2]
    //        N1  /Limits [0 1] /Nums [ 0 A 1 Link ]        A  [P Span null Figure]
    //        N2  /Kids [N0]
    PDFDocumentBuilder hostileBuilder(&tagged);
    const PDFObject array = Documents::addObject(&hostileBuilder, pageArray);
    const PDFObject leaf = Documents::addObject(&hostileBuilder, Documents::createDictionaryObject({ { "Limits", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                                                    { "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), array, PDFObject::createInteger(1), link }) } }));
    const PDFObject back = Documents::addObject(&hostileBuilder, Documents::createDictionaryObject({ { "Kids", Documents::createArrayObject({ parentTree }) } }));
    hostileBuilder.setObject(parentTree.getReference(), Documents::createDictionaryObject({ { "Kids", Documents::createArrayObject({ leaf, leaf, back }) } }));
    const PDFDocument hostile = hostileBuilder.build();

    // The walk ends, and the unchanged builder is written
    write(hostile, PDFObject(), PDFObject(), &status, &errorText, &isBuilderUntouched);
    if (QTest::currentTestFailed())
    {
        return;
    }
    QVERIFY(status == Status::Success);

    // The leaf differs in the builder: it is reached through /Kids of the root node
    write(hostile, leaf, Documents::createDictionaryObject({ { "Limits", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                             { "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), array, PDFObject::createInteger(1), paragraph }) } }),
          &status, &errorText, &isBuilderUntouched);
    QVERIFY(status == Status::PageErrors);
    QCOMPARE(errorText, changedText);
    QVERIFY(isBuilderUntouched);

    // The array of the page differs: it is reached as a value of /Nums of the leaf
    write(hostile, array, otherPageArray, &status, &errorText, &isBuilderUntouched);
    QVERIFY(status == Status::PageErrors);
    QCOMPARE(errorText, changedText);
    QVERIFY(isBuilderUntouched);

    // The node behind the cycle differs
    write(hostile, back, Documents::createDictionaryObject({ { "Kids", Documents::createArrayObject({ parentTree, parentTree }) } }), &status, &errorText, &isBuilderUntouched);
    QVERIFY(status == Status::PageErrors);
    QCOMPARE(errorText, changedText);
    QVERIFY(isBuilderUntouched);

    // ---- Every node shared by two paths: a chain of 28 nodes, each listing the next one twice,
    //      above the leaf. Walked path by path it is 2^28 visits of the leaf; with the visited
    //      set of the walk each node is entered once. ----
    PDFDocumentBuilder chainBuilder(&tagged);
    PDFObject node = Documents::addObject(&chainBuilder, Documents::createDictionaryObject({ { "Limits", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                                             { "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), pageArray, PDFObject::createInteger(1), link }) } }));
    const PDFObject chainLeaf = node;
    for (int i = 0; i < 27; ++i)
    {
        node = Documents::addObject(&chainBuilder, Documents::createDictionaryObject({ { "Limits", Documents::createIntegerArrayObject({ 0, 1 }) }, { "Kids", Documents::createArrayObject({ node, node }) } }));
    }
    chainBuilder.setObject(parentTree.getReference(), Documents::createDictionaryObject({ { "Kids", Documents::createArrayObject({ node, node }) } }));
    const PDFDocument chained = chainBuilder.build();

    QElapsedTimer timer;
    timer.start();
    write(chained, PDFObject(), PDFObject(), &status, &errorText, &isBuilderUntouched);
    if (QTest::currentTestFailed())
    {
        return;
    }
    QVERIFY(status == Status::Success);

    write(chained, chainLeaf, Documents::createDictionaryObject({ { "Limits", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                  { "Nums", Documents::createArrayObject({ PDFObject::createInteger(0), pageArray, PDFObject::createInteger(1), paragraph }) } }),
          &status, &errorText, &isBuilderUntouched);
    QVERIFY(status == Status::PageErrors);
    QCOMPARE(errorText, changedText);
    const qint64 limit = qint64(20000 * qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR", QStringLiteral("1")).toDouble()));
    QVERIFY2(timer.elapsed() < limit, qPrintable(QString::number(timer.elapsed())));
}

void DocumentToolContentTest::writerCancel()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer otherOriginal = buildPage(document, 1);
    const PDFContentEditSnapshot snapshot = snapshotOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()),
                                                         removeNode(otherOriginal, nodesOfKind(*otherOriginal, PDFContentNodeKind::Path)[0]->getId()) });

    // The cancellation is asked after each container: twice for the fingerprints, twice for the
    // bytes. A cancellation at any of the four questions writes nothing.
    for (int questions = 0; questions < 4; ++questions)
    {
        const ContentToolTestControl control(questions);
        PDFDocumentBuilder builder(&document);
        const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), &control, nullptr);
        QVERIFY2(result.status == Status::Canceled, qPrintable(QString::number(questions)));
        QVERIFY(!result.isSuccess());
        QVERIFY(result.pagesChanged.empty());
        QVERIFY(result.errors.empty());
        QVERIFY(result.changedContainers.empty());
        QVERIFY(*builder.getStorage() == document.getStorage());
    }

    // Not canceled at the four questions: written
    const ContentToolTestControl control(4);
    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult result = Writer::write(&document, snapshot, &builder, Writer::Options(), &control, nullptr);
    QVERIFY(result.status == Status::Success);
    QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0, 1 }));
}

void DocumentToolContentTest::writerProgress()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocument otherDocument = createPagesDocument({ QByteArray(TWO_PATHS) + "\n", ONE_PATH });
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshot snapshot = snapshotOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()), buildPage(document, 1) });

    // One part of the progress: a step for the fingerprint of each container and a step for the
    // bytes of each written one; the part is ended on every way out
    ContentToolTestProgress progress;
    PDFDocumentBuilder builder(&document);
    QVERIFY(Writer::write(&document, snapshot, &builder, Writer::Options(), nullptr, &progress).status == Status::Success);
    QCOMPARE(progress.starts, 1);
    QCOMPARE(progress.finishes, 1);
    QCOMPARE(progress.steps, size_t(4));
    QCOMPARE(progress.stepsDone, size_t(3));    // Two fingerprints, the bytes of the one changed page

    ContentToolTestProgress changedProgress;
    PDFDocumentBuilder otherBuilder(&otherDocument);
    QVERIFY(Writer::write(&otherDocument, snapshot, &otherBuilder, Writer::Options(), nullptr, &changedProgress).status == Status::DocumentChanged);
    QCOMPARE(changedProgress.starts, 1);
    QCOMPARE(changedProgress.finishes, 1);
    QCOMPARE(changedProgress.stepsDone, size_t(2));

    ContentToolTestProgress canceledProgress;
    const ContentToolTestControl control(0);
    PDFDocumentBuilder canceledBuilder(&document);
    QVERIFY(Writer::write(&document, snapshot, &canceledBuilder, Writer::Options(), &control, &canceledProgress).status == Status::Canceled);
    QCOMPARE(canceledProgress.starts, 1);
    QCOMPARE(canceledProgress.finishes, 1);
}

void DocumentToolContentTest::writerPreview()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer otherOriginal = buildPage(document, 1);
    const PDFContentNodeId otherPath = nodesOfKind(*otherOriginal, PDFContentNodeKind::Path)[0]->getId();
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId());

    // ---- The preview of the page 1: the page 1 with its changes, the page 2 as in the document,
    //      though it is changed in the snapshot too ----
    {
        const PDFContentEditSnapshot snapshot = snapshotOf({ edited, removeNode(otherOriginal, otherPath) });
        PDFContentWriteResult result;
        result.status = Status::Canceled;
        const PDFDocumentPointer preview = Writer::writePagePreview(&document, snapshot, 0, nullptr, &result);
        QVERIFY(!preview.isNull());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 0 }));
        QCOMPARE(preview->getCatalog()->getPageCount(), size_t(2));
        QCOMPARE(pageContent(*preview, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
        QCOMPARE(pageContent(*preview, 1), QByteArray(ONE_PATH));
        const PDFObjectReference otherPage = document.getCatalog()->getPage(1)->getPageReference();
        QVERIFY(preview->getObjectByReference(otherPage) == document.getObjectByReference(otherPage));

        // The other page
        const PDFDocumentPointer otherPreview = Writer::writePagePreview(&document, snapshot, 1, nullptr, nullptr);
        QVERIFY(!otherPreview.isNull());
        QCOMPARE(pageContent(*otherPreview, 0), QByteArray(TWO_PATHS));
        QCOMPARE(pageContent(*otherPreview, 1), QByteArray("0 1 0 rg"));

        // The document of the preview is not the document itself
        QCOMPARE(pageContent(document, 0), QByteArray(TWO_PATHS));
    }

    // ---- An error of another page does not stop the preview of this one; an error of this
    //      page gives no document and the errors ----
    {
        const PDFContentContainerPointer broken = setOverride(otherOriginal, otherPath, brokenOverride());
        const PDFContentEditSnapshot snapshot = snapshotOf({ edited, broken });

        PDFContentWriteResult result;
        QVERIFY(!Writer::writePagePreview(&document, snapshot, 0, nullptr, &result).isNull());
        QVERIFY(result.status == Status::Success);

        QVERIFY(Writer::writePagePreview(&document, snapshot, 1, nullptr, &result).isNull());
        QVERIFY(result.status == Status::PageErrors);
        QCOMPARE(result.errors.size(), size_t(1));
        QCOMPARE(result.errors.begin()->first, PDFInteger(1));
        QVERIFY(result.errors.begin()->second[0].nodeId == otherPath);
        QVERIFY(Writer::writePagePreview(&document, snapshot, 1, nullptr, nullptr).isNull());
    }

    // ---- A page, which does not exist; no document; a canceled preview ----
    {
        const PDFContentEditSnapshot snapshot = snapshotOf({ edited });
        PDFContentWriteResult result;
        for (const PDFInteger pageIndex : { PDFInteger(-1), PDFInteger(2), PDFInteger(1000000) })
        {
            QVERIFY(Writer::writePagePreview(&document, snapshot, pageIndex, nullptr, &result).isNull());
            QVERIFY(result.status == Status::PageErrors);
            QCOMPARE(result.errors.size(), size_t(1));
        }

        QVERIFY(Writer::writePagePreview(nullptr, snapshot, 0, nullptr, &result).isNull());
        QVERIFY(result.status == Status::PageErrors);

        const ContentToolTestControl control(0);
        QVERIFY(Writer::writePagePreview(&document, snapshot, 0, &control, &result).isNull());
        QVERIFY(result.status == Status::Canceled);
    }

    // ---- The preview of a page shows the changed form it draws ----
    {
        PDFObjectReference form1;
        PDFObjectReference form2;
        const PDFDocument formsDocument = createFormsDocument(&form1, &form2);
        const PDFContentContainerPointer form = buildContainer(formsDocument, PDFContentContainerKey::createForm(form2), 0);
        const PDFContentEditSnapshot snapshot = snapshotOf({ removeNode(form, nodesOfKind(*form, PDFContentNodeKind::Path)[0]->getId()) });

        // The page 1 draws /Fm2, the page 2 does not
        PDFContentWriteResult result;
        const PDFDocumentPointer preview = Writer::writePagePreview(&formsDocument, snapshot, 0, nullptr, &result);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview->getDecodedStream(preview->getObjectByReference(form2).getStream()), QByteArray("/GP gs 1 0 0 rg"));

        const PDFDocumentPointer otherPreview = Writer::writePagePreview(&formsDocument, snapshot, 1, nullptr, &result);
        QVERIFY(!otherPreview.isNull());
        QVERIFY(otherPreview->getObjectByReference(form2) == formsDocument.getObjectByReference(form2));
    }
}

void DocumentToolContentTest::writerFacts()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, QByteArray("BT /F1 12 Tf (A) Tj (B) Tj ET\n5 5 10 10 re\nS\n/Fm1 Do") });
    const PDFContentContainerPointer original = buildPage(document, 0);
    const std::vector<PDFContentNodePointer> paths = nodesOfKind(*original, PDFContentNodeKind::Path);
    const PDFContentNodeId rootId = original->getRoot()->getId();

    const auto write = [&document](const PDFContentContainerPointer& container)
    {
        PDFDocumentBuilder builder(&document);
        return Writer::write(&document, snapshotOf({ container }), &builder, Writer::Options(), nullptr, nullptr);
    };

    // ---- An object with an override is edited ----
    {
        const PDFContentWriteResult result = write(setOverride(original, paths[0]->getId(), alphaOverride()));
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsEdited, PDFInteger(1));
        QCOMPARE(result.objectsAdded, PDFInteger(0));
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
    }

    // ---- An object written canonically is edited ----
    {
        PDFContentContainerEditor editor(original);
        PDFContentNode::Data data = paths[0]->getData();
        data.isModified = true;
        QVERIFY(editor.setNodeData(paths[0]->getId(), std::move(data)));
        const PDFContentWriteResult result = write(editor.build());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsEdited, PDFInteger(1));
        QCOMPARE(result.objectsAdded, PDFInteger(0));
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
    }

    // ---- An object moved to another place of the order is edited: the first path goes behind
    //      the second one - one object has moved, not two ----
    {
        PDFContentContainerEditor editor(original);
        QVERIFY(editor.moveNode(paths[0]->getId(), rootId, -1));
        const PDFContentWriteResult result = write(editor.build());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsEdited, PDFInteger(1));
        QCOMPARE(result.objectsAdded, PDFInteger(0));
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
    }

    // ---- The last path goes to the front: again one object has moved ----
    {
        PDFContentContainerEditor editor(original);
        QVERIFY(editor.moveNode(paths[1]->getId(), rootId, 0));
        const PDFContentWriteResult result = write(editor.build());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsEdited, PDFInteger(1));
    }

    // ---- Three paths: the first one goes to the end, or the last one to the front - in both
    //      cases one object has moved and two have kept their order ----
    {
        const PDFDocument three = createPagesDocument({ QByteArray("0 0 1 1 re\nf\n2 2 1 1 re\nf\n4 4 1 1 re\nf") });
        const PDFContentContainerPointer page = buildPage(three, 0);
        const std::vector<PDFContentNodePointer> threePaths = nodesOfKind(*page, PDFContentNodeKind::Path);
        QCOMPARE(threePaths.size(), size_t(3));

        for (const bool isFirstMoved : { true, false })
        {
            PDFContentContainerEditor editor(page);
            QVERIFY(isFirstMoved ? editor.moveNode(threePaths[0]->getId(), page->getRoot()->getId(), -1) : editor.moveNode(threePaths[2]->getId(), page->getRoot()->getId(), 0));
            PDFDocumentBuilder builder(&three);
            const PDFContentWriteResult result = Writer::write(&three, snapshotOf({ editor.build() }), &builder, Writer::Options(), nullptr, nullptr);
            QVERIFY(result.status == Status::Success);
            QCOMPARE(result.objectsEdited, PDFInteger(1));
            QCOMPARE(result.objectsAdded, PDFInteger(0));
            QCOMPARE(result.objectsRemoved, PDFInteger(0));

            // The bytes: the three paths in their new order, each with its own bytes
            const QByteArray content = pageContent(builder.build(), 0);
            const qsizetype positions[3] = { content.indexOf("0 0 1 1 re"), content.indexOf("2 2 1 1 re"), content.indexOf("4 4 1 1 re") };
            QVERIFY(positions[0] >= 0 && positions[1] >= 0 && positions[2] >= 0);
            if (isFirstMoved)
            {
                QVERIFY2(positions[1] < positions[2] && positions[2] < positions[0], content.constData());
            }
            else
            {
                QVERIFY2(positions[2] < positions[0] && positions[0] < positions[1], content.constData());
            }
        }
    }

    // ---- A moved object with an override is one edited object, not two ----
    {
        PDFContentContainerEditor editor(original);
        PDFContentNode::Data data = paths[0]->getData();
        data.override = alphaOverride();
        QVERIFY(editor.setNodeData(paths[0]->getId(), std::move(data)));
        QVERIFY(editor.moveNode(paths[0]->getId(), rootId, -1));
        const PDFContentWriteResult result = write(editor.build());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsEdited, PDFInteger(1));
    }

    // ---- An object, one of whose operations is suppressed, is edited (R-1: the editor overrides
    //      what the operation sets). The second page begins "BT /F1 12 Tf": the operation 1 is Tf,
    //      a part of the text object ----
    {
        const PDFContentContainerPointer page = buildPage(document, 1);
        QCOMPARE(nodesOfKind(*page, PDFContentNodeKind::TextObject).size(), size_t(1));
        PDFContentContainerEditor editor(page);
        PDFContentOperation::Flags flags = editor.getOperations().getOperation(1).getFlags();
        flags |= PDFContentOperation::Suppressed;
        editor.getOperations().setFlags(1, flags);
        const PDFContentWriteResult result = write(editor.build());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsEdited, PDFInteger(1));
        QCOMPARE(result.objectsAdded, PDFInteger(0));
        QCOMPARE(result.objectsRemoved, PDFInteger(0));
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 1 }));
    }

    // ---- New objects are added; a removed and an added one are counted apart ----
    {
        PDFContentContainerPointer container = appendRectangle(original, "0 0 5 5", FIRST_NEW_NODE);
        container = appendRectangle(container, "5 5 5 5", FIRST_NEW_NODE + 1);
        container = removeNode(container, paths[0]->getId());
        const PDFContentWriteResult result = write(container);
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsAdded, PDFInteger(2));
        QCOMPARE(result.objectsRemoved, PDFInteger(1));
        QCOMPARE(result.objectsEdited, PDFInteger(0));
    }

    // ---- The second page: a text object with two show operators, a stroked path and a form.
    //      Each one is one object; everything is removed ----
    {
        const PDFContentContainerPointer page = buildPage(document, 1);
        PDFContentContainerEditor editor(page);
        for (const PDFContentNodePointer& child : page->getRoot()->getChildren())
        {
            QVERIFY(editor.removeNode(child->getId()));
        }
        const PDFContentWriteResult result = write(editor.build());
        QVERIFY(result.status == Status::Success);
        QCOMPARE(result.objectsRemoved, PDFInteger(3));
        QCOMPARE(result.objectsAdded, PDFInteger(0));
        QCOMPARE(result.objectsEdited, PDFInteger(0));
        QCOMPARE(result.pagesChanged, std::vector<PDFInteger>({ 1 }));
    }
}

void DocumentToolContentTest::writerHostile()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshot snapshot = snapshotOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()) });

    // No builder: an error, which belongs to no page
    const PDFContentWriteResult noBuilder = Writer::write(&document, snapshot, nullptr, Writer::Options(), nullptr, nullptr);
    QVERIFY(noBuilder.status == Status::PageErrors);
    QCOMPARE(noBuilder.errors.size(), size_t(1));
    QCOMPARE(noBuilder.errors.begin()->first, PDFInteger(-1));

    // No document: nothing matches it
    PDFDocumentBuilder builder(&document);
    const PDFContentWriteResult noDocument = Writer::write(nullptr, snapshot, &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(noDocument.status == Status::DocumentChanged);
    QCOMPARE(noDocument.changedContainers.size(), size_t(1));
    QVERIFY(*builder.getStorage() == document.getStorage());

    const PDFContentWriteResult nothing = Writer::write(nullptr, PDFContentEditSnapshot(), &builder, Writer::Options(), nullptr, nullptr);
    QVERIFY(nothing.status == Status::DocumentChanged);

    // A builder of another document (a blank one and one with other pages): nothing is written
    // into a foreign object
    const PDFDocument foreignDocument = createPagesDocument({ ONE_PATH, TWO_PATHS });
    PDFDocumentBuilder blankBuilder;
    PDFDocumentBuilder otherBuilder(&foreignDocument);
    for (PDFDocumentBuilder* foreignBuilder : { &blankBuilder, &otherBuilder })
    {
        for (int i = 0; i < 40; ++i)
        {
            foreignBuilder->addObject(PDFObject::createInteger(i));
        }
        const PDFObjectStorage foreignStorage = *foreignBuilder->getStorage();
        const PDFContentWriteResult foreign = Writer::write(&document, snapshot, foreignBuilder, Writer::Options(), nullptr, nullptr);
        QVERIFY(foreign.status == Status::PageErrors);
        QCOMPARE(foreign.errors.size(), size_t(1));
        QCOMPARE(foreign.errors.begin()->first, PDFInteger(0));
        QCOMPARE(foreign.errors.begin()->second.size(), size_t(1));
        QVERIFY(foreign.errors.begin()->second[0].container == snapshot.containers.begin()->first);
        QVERIFY(!foreign.errors.begin()->second[0].nodeId.isValid());
        QVERIFY(*foreignBuilder->getStorage() == foreignStorage);
    }

    // The same page written twice into one builder: the second container was built over the
    // document, not over what the first write left in the builder - it would silently drop the
    // first change. It is refused, the first write stays.
    {
        const PDFContentEditSnapshot otherSnapshot = snapshotOf({ appendRectangle(original, "0 0 5 5", FIRST_NEW_NODE) });
        PDFDocumentBuilder twiceBuilder(&document);
        QVERIFY(Writer::write(&document, snapshot, &twiceBuilder, Writer::Options(), nullptr, nullptr).status == Status::Success);
        const PDFObjectStorage afterFirst = *twiceBuilder.getStorage();
        const PDFContentWriteResult second = Writer::write(&document, otherSnapshot, &twiceBuilder, Writer::Options(), nullptr, nullptr);
        QVERIFY(second.status == Status::PageErrors);
        QCOMPARE(second.errors.size(), size_t(1));
        QCOMPARE(second.errors.begin()->first, PDFInteger(0));
        QVERIFY(*twiceBuilder.getStorage() == afterFirst);
        QCOMPARE(pageContent(twiceBuilder.build(), 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    }

    // A new object, which refers to itself and to an object, which is no new object: the walk
    // over the new objects ends
    PDFContentEditSnapshot cyclic = snapshot;
    const PDFObjectReference provisionalSelf(1, 65535);
    const PDFObjectReference self(PDFInteger(document.getStorage().getObjects().size()), 0);    // The first new object of the builder
    cyclic.newObjects[provisionalSelf] = Documents::createDictionaryObject({ { "Self", PDFObject::createReference(provisionalSelf) }, { "Other", PDFObject::createReference(PDFObjectReference(999999, 0)) } });
    QByteArray name;
    const PDFContentContainerPointer withCycle = addResource(snapshot.containers.begin()->second, PDFContentResourceCategory::ExtGState, "GS", PDFObject::createReference(provisionalSelf), &name);
    cyclic.containers[withCycle->getKey()] = withCycle;

    PDFDocumentBuilder cyclicBuilder(&document);
    const PDFContentWriteResult cyclicResult = Writer::write(&document, cyclic, &cyclicBuilder, Writer::Options(), nullptr, nullptr);
    QVERIFY(cyclicResult.status == Status::Success);
    const PDFDocument written = cyclicBuilder.build();
    QVERIFY(written.getObjectByReference(self) == Documents::createDictionaryObject({ { "Self", PDFObject::createReference(self) }, { "Other", PDFObject::createReference(PDFObjectReference(999999, 0)) } }));
}

// ---------------------------------------------------------------------------------------------
//  The tool (zadani_jadro.md 4.6; zadani.md KE.6, KE.8, KE.9, QE.2, QE.3; zadani_overeni.md 7.1)
// ---------------------------------------------------------------------------------------------

void DocumentToolContentTest::toolDeclaration()
{
    using Permissions = std::vector<PDFSecurityHandler::Permission>;

    const QString toolId = QStringLiteral("edit.content");

    // ---- The tool and its row of the catalog (zadani.md KE.6, QE.3, QE.4) ----
    Tool tool;
    QCOMPARE(tool.getId(), toolId);
    QCOMPARE(QString::fromLatin1(Tool::TOOL_ID), toolId);

    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(toolId);
    QVERIFY(entry != nullptr);
    QCOMPARE(tool.getInfo(), entry);
    QCOMPARE(entry->getRequiredPermissions(), Permissions({ PDFSecurityHandler::Permission::Modify }));
    QVERIFY(entry->isPageChange());
    QVERIFY(entry->isAlwaysConfirmedByMcp());

    // ---- The tool is not registered in stage E0: its page stays Page Not Found and MCP does
    //      not offer it (the core registers nothing by itself) ----
    QVERIFY(!PDFDocumentToolRegistry::isImplemented(toolId));
    QVERIFY(PDFDocumentToolRegistry::getTool(toolId) == nullptr);
    QVERIFY(!PDFDocumentToolRegistry::getImplementedTools().contains(toolId));

    // ---- The parameters (the table of zadani_jadro.md 4.6) ----
    const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString errorMessage;
    QVERIFY2(PDFDocumentToolParameters::isValid(definitions, &errorMessage), qPrintable(errorMessage));

    QStringList names;
    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        names << definition.getName();

        // Every parameter has an English title and a description for the schema of the assistant
        QVERIFY2(!definition.getSourceTitle().isEmpty(), qPrintable(definition.getName()));
        QVERIFY2(!definition.getSourceDescription().isEmpty(), qPrintable(definition.getName()));

        // Nothing of a run is a last value of the tool: the values belong to one document. The
        // mode and the options of the display of the editor are (zadani_gui.md 4.26; the test
        // toolDisplayOptions).
        if (definition.getName() != QLatin1String("mode") && names.size() <= 8)
        {
            QVERIFY2(!definition.isStored(), qPrintable(definition.getName()));
        }
    }
    QCOMPARE(names.mid(0, 8), QStringList({ QStringLiteral("mode"), QStringLiteral("session"), QStringLiteral("operations"), QStringLiteral("pages"),
                                            QStringLiteral("depth"), QStringLiteral("offset"), QStringLiteral("maximumObjects"), QStringLiteral("skipPages") }));
    QCOMPARE(names.size(), 8 + 16);

    const auto find = [&definitions](const char* name) { return PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name)); };

    const PDFDocumentToolParameterDefinition* mode = find(Tool::PARAMETER_MODE);
    QVERIFY(mode->getType() == PDFDocumentToolParameterType::Enum);
    QCOMPARE(mode->getOptions().size(), size_t(2));
    QCOMPARE(mode->getOptions()[0].id, QStringLiteral("apply"));
    QCOMPARE(mode->getOptions()[1].id, QStringLiteral("list"));
    QCOMPARE(mode->getDefaultValue().toString(), QStringLiteral("apply"));

    // The work of the panels: a value, which is never stored and never goes through MCP
    const PDFDocumentToolParameterDefinition* session = find(Tool::PARAMETER_SESSION);
    QVERIFY(session->getType() == PDFDocumentToolParameterType::Value);
    QVERIFY(session->getDefaultValue().canConvert<PDFContentEditSnapshotPointer>());
    QVERIFY(session->getDefaultValue().value<PDFContentEditSnapshotPointer>() == nullptr);

    // KE.8: the operations are structured data of at most 4 MiB
    const PDFDocumentToolParameterDefinition* operations = find(Tool::PARAMETER_OPERATIONS);
    QVERIFY(operations->getType() == PDFDocumentToolParameterType::String);
    QVERIFY(operations->isStructuredData());
    QCOMPARE(operations->getMaximumLength(), 4 * 1024 * 1024);
    QCOMPARE(Tool::MAXIMUM_OPERATIONS_LENGTH, 4 * 1024 * 1024);
    QCOMPARE(operations->getDefaultValue().toString(), QString());

    // zadani_overeni.md 7.1: the listing of 1 to 16 levels (4 by default), of 1 to 2 000
    // objects a call (500 by default)
    QVERIFY(find(Tool::PARAMETER_PAGES)->getType() == PDFDocumentToolParameterType::PageSelection);
    const PDFDocumentToolParameterDefinition* depth = find(Tool::PARAMETER_DEPTH);
    QVERIFY(depth->getType() == PDFDocumentToolParameterType::Integer);
    QCOMPARE(depth->getDefaultValue().toLongLong(), qint64(4));
    QCOMPARE(depth->getIntegerMinimum(), qint64(1));
    QCOMPARE(depth->getIntegerMaximum(), qint64(16));
    const PDFDocumentToolParameterDefinition* offset = find(Tool::PARAMETER_OFFSET);
    QCOMPARE(offset->getDefaultValue().toLongLong(), qint64(0));
    QCOMPARE(offset->getIntegerMinimum(), qint64(0));
    const PDFDocumentToolParameterDefinition* maximumObjects = find(Tool::PARAMETER_MAXIMUM_OBJECTS);
    QCOMPARE(maximumObjects->getDefaultValue().toLongLong(), qint64(500));
    QCOMPARE(maximumObjects->getIntegerMinimum(), qint64(1));
    QCOMPARE(maximumObjects->getIntegerMaximum(), qint64(2000));

    // The skipped pages: only the panels give them, no page by default
    const PDFDocumentToolParameterDefinition* skipPages = find(Tool::PARAMETER_SKIP_PAGES);
    QVERIFY(skipPages->getType() == PDFDocumentToolParameterType::PageSelection);
    QVERIFY(skipPages->isPanelOnly());
    QString selectionError;
    const std::optional<PDFDocumentToolPageSelection> noPage = PDFDocumentToolPageSelection::fromVariant(skipPages->getDefaultValue(), &selectionError);
    QVERIFY(noPage.has_value());
    QVERIFY(noPage->isNone);

    // ---- The schema of the assistant: the mode and the parameters of the listing. The value of
    //      the panels and the skipped pages are never there; the operations stay out until the
    //      actions of the assistant are written (zadani_overeni.md 7.1: until stage E6 an
    //      assistant may only read) ----
    const QJsonObject schema = PDFDocumentToolParameters::toJsonSchema(definitions);
    const QStringList properties = schema.value(QStringLiteral("properties")).toObject().keys();
    QCOMPARE(properties, QStringList({ QStringLiteral("depth"), QStringLiteral("maximumObjects"), QStringLiteral("mode"), QStringLiteral("offset"), QStringLiteral("pages") }));
    QVERIFY(operations->isMcpExcluded());

    // ---- The names of the data and of the facts (zadani_jadro.md 4.6) ----
    QCOMPARE(QByteArray(Tool::DATA_PAGES_CHANGED), QByteArray("pagesChanged"));
    QCOMPARE(QByteArray(Tool::DATA_OBJECTS_ADDED), QByteArray("objectsAdded"));
    QCOMPARE(QByteArray(Tool::DATA_OBJECTS_EDITED), QByteArray("objectsEdited"));
    QCOMPARE(QByteArray(Tool::DATA_OBJECTS_REMOVED), QByteArray("objectsRemoved"));
    QCOMPARE(QByteArray(Tool::DATA_LAYERS), QByteArray("layers"));
    QCOMPARE(QByteArray(Tool::DATA_STRUCTURE), QByteArray("structure"));
    QCOMPARE(QByteArray(Tool::DATA_FONTS), QByteArray("fonts"));
    QCOMPARE(QByteArray(Tool::DATA_PAGE_ERRORS), QByteArray("pageErrors"));
    QCOMPARE(QByteArray(Tool::DATA_PAGES), QByteArray("pages"));
    QCOMPARE(QByteArray(Tool::FACT_CONFORMANCE), QByteArray("conformance"));
    QCOMPARE(QByteArray(Tool::FACT_NOT_TAGGED), QByteArray("notTagged"));

    // ---- The limits of a script and of a listing (zadani_overeni.md 6.2, 7.1) ----
    QCOMPARE(PDFContentJson::MAXIMUM_OPERATION_COUNT, 500);
    QCOMPARE(PDFContentJson::MAXIMUM_CHANGED_PAGE_COUNT, 20);
    QCOMPARE(PDFContentJson::MAXIMUM_TEXT_LENGTH, 65536);
    QCOMPARE(PDFContentJson::DEFAULT_LIST_OBJECT_COUNT, 500);
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_OBJECT_COUNT, 2000);
    QCOMPARE(PDFContentJson::DEFAULT_LIST_DEPTH, 4);
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_DEPTH, 16);
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_PAGE_COUNT, 20);

    PDFContentJson::ListOptions listOptions;
    QCOMPARE(listOptions.depth, 4);
    QCOMPARE(listOptions.offset, PDFInteger(0));
    QCOMPARE(listOptions.maximumObjects, 500);

    QCOMPARE(QByteArray(PDFContentJson::KEY_REVISIONS), QByteArray("revisions"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_ITEMS), QByteArray("items"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_TYPE), QByteArray("type"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_ID), QByteArray("id"));
    QCOMPARE(QByteArray(PDFContentJson::KEY_AS), QByteArray("as"));
}

void DocumentToolContentTest::toolDisplayOptions()
{
    using Type = PDFDocumentToolParameterType;

    // zadani_gui.md 4.26, zadani.md 9.3: the options of the display of the editor are stored
    // parameters of the tool, which only the panels give and which never go through MCP; a run
    // does not read them. Names, types and defaults by hand from PDFAppEditContentOptions
    // (pe1s4_state.md, request 2): the menu View, the cards Grid and units and Preview, the left
    // panel. The second value is another valid value, which is stored and restored.
    struct Option
    {
        const char* name;
        Type type;
        QVariant defaultValue;
        QVariant otherValue;
    };
    const std::vector<Option> expected = {
        { "showRulers", Type::Bool, false, true },
        { "showGrid", Type::Bool, false, true },
        { "showGuides", Type::Bool, true, false },
        { "showClippingPaths", Type::Bool, false, true },
        { "showInvisibleText", Type::Bool, false, true },
        { "showAnnotations", Type::Bool, true, false },
        { "showReadingOrder", Type::Bool, false, true },
        { "snapToObjects", Type::Bool, true, false },
        { "snapToGrid", Type::Bool, true, false },
        { "units", Type::Enum, QStringLiteral("pt"), QStringLiteral("mm") },
        { "gridSpacing", Type::Real, 36.0, 12.5 },
        { "gridOrigin", Type::Enum, QStringLiteral("bottom-left"), QStringLiteral("top-left") },
        { "previewOverprint", Type::Bool, true, false },
        { "previewAnnotations", Type::Bool, true, false },
        { "previewPaperColor", Type::String, QStringLiteral("#ffffff"), QStringLiteral("#336699") },
        { "leftTab", Type::String, QStringLiteral("objects"), QStringLiteral("layers") },
    };

    Tool tool;
    const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString errorMessage;
    QVERIFY2(PDFDocumentToolParameters::isValid(definitions, &errorMessage), qPrintable(errorMessage));

    // The options follow the parameters of a run, in the order above
    QStringList names;
    for (size_t i = 8; i < definitions.size(); ++i)
    {
        names << definitions[i].getName();
    }
    QStringList expectedNames;
    for (const Option& option : expected)
    {
        expectedNames << QString::fromLatin1(option.name);
    }
    QCOMPARE(names, expectedNames);

    QVariantMap otherValues;
    for (const Option& option : expected)
    {
        const QString name = QString::fromLatin1(option.name);
        const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(definitions, name);
        QVERIFY2(definition != nullptr, option.name);
        QVERIFY2(definition->getType() == option.type, option.name);
        QVERIFY2(definition->getDefaultValue() == option.defaultValue, option.name);

        // Stored as the last value of the tool, given only by the panels, never in MCP
        QVERIFY2(definition->isStored(), option.name);
        QVERIFY2(definition->isPanelOnly(), option.name);
        QVERIFY2(definition->isMcpExcluded(), option.name);
        QVERIFY2(!definition->isRequired(), option.name);

        // Stored and restored: the value written into the database of the last values and read
        // back (PDFToolParameterRepository::setValues / getValues) is the value
        QVariant normalized;
        QCOMPARE(definition->normalize(option.otherValue, &normalized), QString());
        QVERIFY2(normalized == option.otherValue, option.name);
        const std::optional<QVariant> restored = definition->fromStorageValue(definition->toStorageValue(normalized));
        QVERIFY2(restored.has_value() && *restored == option.otherValue, option.name);
        const std::optional<QVariant> restoredDefault = definition->fromStorageValue(definition->toStorageValue(option.defaultValue));
        QVERIFY2(restoredDefault.has_value() && *restoredDefault == option.defaultValue, option.name);

        otherValues.insert(name, option.otherValue);
    }

    // The values of the enumerations and the range of the grid (points)
    const auto find = [&definitions](const char* name) { return PDFDocumentToolParameters::find(definitions, QString::fromLatin1(name)); };
    QStringList units;
    for (const PDFDocumentToolParameterOption& option : find(Tool::PARAMETER_UNITS)->getOptions())
    {
        units << option.id;
    }
    QCOMPARE(units, QStringList({ QStringLiteral("pt"), QStringLiteral("mm"), QStringLiteral("in") }));
    QStringList origins;
    for (const PDFDocumentToolParameterOption& option : find(Tool::PARAMETER_GRID_ORIGIN)->getOptions())
    {
        origins << option.id;
    }
    QCOMPARE(origins, QStringList({ QStringLiteral("bottom-left"), QStringLiteral("top-left") }));

    QVariant normalized;
    QVERIFY(!find(Tool::PARAMETER_UNITS)->normalize(QStringLiteral("km"), &normalized).isEmpty());
    QVERIFY(!find(Tool::PARAMETER_GRID_SPACING)->normalize(0.5, &normalized).isEmpty());
    QVERIFY(!find(Tool::PARAMETER_GRID_SPACING)->normalize(1000.5, &normalized).isEmpty());
    QCOMPARE(find(Tool::PARAMETER_GRID_SPACING)->normalize(1000.0, &normalized), QString());
    QVERIFY(!find(Tool::PARAMETER_PREVIEW_PAPER_COLOR)->normalize(QStringLiteral("#0123456789"), &normalized).isEmpty());

    // ---- MCP is unchanged: the schema has the five parameters of the listing and the mode
    //      only (toolDeclaration), and the JSON of the values (the results and the activity of
    //      MCP) has none of the options ----
    const QJsonObject schema = PDFDocumentToolParameters::toJsonSchema(definitions);
    QCOMPARE(schema.value(QStringLiteral("properties")).toObject().keys(),
             QStringList({ QStringLiteral("depth"), QStringLiteral("maximumObjects"), QStringLiteral("mode"), QStringLiteral("offset"), QStringLiteral("pages") }));
    QVariantMap values = PDFDocumentToolParameters::getDefaultValues(definitions);
    for (auto it = otherValues.cbegin(); it != otherValues.cend(); ++it)
    {
        values.insert(it.key(), it.value());
    }
    const QJsonObject json = PDFDocumentToolParameters::toJson(definitions, values);
    for (const QString& name : std::as_const(expectedNames))
    {
        QVERIFY2(!json.contains(name), qPrintable(name));
    }

    // ---- A run does not read them: apply and list give the same result with other values ----
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshotPointer snapshot = snapshotPointerOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()) });

    QVariantMap applyWithOptions = applyParameters(snapshot);
    QVariantMap listWithOptions = listParameters(QStringLiteral("1-2"));
    for (auto it = otherValues.cbegin(); it != otherValues.cend(); ++it)
    {
        applyWithOptions.insert(it.key(), it.value());
        listWithOptions.insert(it.key(), it.value());
    }

    const PDFDocumentToolResult applied = tool.run(context, applyParameters(snapshot), nullptr, nullptr);
    const PDFDocumentToolResult appliedWithOptions = tool.run(context, applyWithOptions, nullptr, nullptr);
    QVERIFY2(applied.isSuccess(), qPrintable(applied.message));
    QVERIFY2(appliedWithOptions.isSuccess(), qPrintable(appliedWithOptions.message));
    QCOMPARE(appliedWithOptions.data, applied.data);
    QCOMPARE(appliedWithOptions.documents.size(), size_t(1));
    QCOMPARE(pageContent(*appliedWithOptions.documents[0].document, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QCOMPARE(pageContent(*appliedWithOptions.documents[0].document, 1), QByteArray(ONE_PATH));

    const PDFDocumentToolResult listed = tool.run(context, listParameters(QStringLiteral("1-2")), nullptr, nullptr);
    const PDFDocumentToolResult listedWithOptions = tool.run(context, listWithOptions, nullptr, nullptr);
    QVERIFY2(listed.isSuccess(), qPrintable(listed.message));
    QVERIFY2(listedWithOptions.isSuccess(), qPrintable(listedWithOptions.message));
    QCOMPARE(listedWithOptions.data, listed.data);
    QCOMPARE(listedWithOptions.data.value(QStringLiteral("pages")).toArray().size(), 2);
}

void DocumentToolContentTest::toolModes()
{
    using Permissions = std::vector<PDFSecurityHandler::Permission>;

    Tool tool;
    const QVariantMap apply = { { QStringLiteral("mode"), QStringLiteral("apply") } };
    const QVariantMap list = { { QStringLiteral("mode"), QStringLiteral("list") } };
    const QVariantMap noMode;

    // apply changes the document, list only reads (zadani_jadro.md 4.6); without a mode the tool
    // is what its row of the catalog says
    QVERIFY(tool.getResultKind(apply) == PDFDocumentToolResultKind::ChangeDocument);
    QVERIFY(tool.getResultKind(noMode) == PDFDocumentToolResultKind::ChangeDocument);
    QVERIFY(tool.getResultKind(list) == PDFDocumentToolResultKind::Analysis);
    QVERIFY(tool.getPossibleResultKinds() == std::vector<PDFDocumentToolResultKind>({ PDFDocumentToolResultKind::ChangeDocument, PDFDocumentToolResultKind::Analysis }));

    // The row of the catalog declares both kinds itself (TOOL_OTHER_RESULT_KINDS): the kind of the
    // row first, the analysis of list second
    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(QStringLiteral("edit.content"));
    QVERIFY(entry != nullptr);
    QVERIFY(entry->getPossibleResultKinds() == std::vector<PDFDocumentToolResultKind>({ PDFDocumentToolResultKind::ChangeDocument, PDFDocumentToolResultKind::Analysis }));

    // apply (and no mode) keeps the row of the catalog: Keep, and a signed file is saved
    // incrementally (QE.3); list is an analysis, no document is produced: no preselected result
    // and the signatures are untouched
    QVERIFY(entry->getPreselectedResult() == PDFDocumentToolPreselectedResult::Keep);
    QVERIFY(entry->getSignatureImpact() == PDFDocumentToolSignatureImpact::IncrementalSave);
    QVERIFY(tool.getPreselectedResult(apply) == PDFDocumentToolPreselectedResult::Keep);
    QVERIFY(tool.getPreselectedResult(noMode) == PDFDocumentToolPreselectedResult::Keep);
    QVERIFY(tool.getPreselectedResult(list) == PDFDocumentToolPreselectedResult::None);
    QVERIFY(tool.getSignatureImpact(apply) == PDFDocumentToolSignatureImpact::IncrementalSave);
    QVERIFY(tool.getSignatureImpact(noMode) == PDFDocumentToolSignatureImpact::IncrementalSave);
    QVERIFY(tool.getSignatureImpact(list) == PDFDocumentToolSignatureImpact::None);

    // apply changes the pages: a certified document loses its certification (QE.3); list does not
    QVERIFY(tool.isPageChange(apply));
    QVERIFY(tool.isPageChange(noMode));
    QVERIFY(!tool.isPageChange(list));

    // ISO 32000-2, Table 22: apply needs bit 4 "modify the contents of the document"; list gives
    // the text of the pages, which is bit 5 "copy or otherwise extract text and graphics"
    QCOMPARE(tool.getRequiredPermissions(apply), Permissions({ PDFSecurityHandler::Permission::Modify }));
    QCOMPARE(tool.getRequiredPermissions(noMode), Permissions({ PDFSecurityHandler::Permission::Modify }));
    QCOMPARE(tool.getRequiredPermissions(list), Permissions({ PDFSecurityHandler::Permission::CopyContent }));
    QCOMPARE(uint32_t(PDFSecurityHandler::Permission::Modify), uint32_t(1) << 3);
    QCOMPARE(uint32_t(PDFSecurityHandler::Permission::CopyContent), uint32_t(1) << 4);
}

void DocumentToolContentTest::toolCheckGivenParameters_data()
{
    QTest::addColumn<QVariantMap>("given");
    QTest::addColumn<QStringList>("parameters");    // The parameters of the errors, in their order

    const PDFDocument document = createPagesDocument({ TWO_PATHS });
    const QVariant snapshot = QVariant::fromValue(snapshotPointerOf({ buildPage(document, 0) }));
    const QVariant noSnapshot = QVariant::fromValue(PDFContentEditSnapshotPointer());
    const QString script = QStringLiteral("{\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}");

    const QString mode = QStringLiteral("mode");
    const QString session = QStringLiteral("session");
    const QString operations = QStringLiteral("operations");

    // KE.8: apply has exactly one of the changes of the editor and the operations
    QTest::newRow("apply with nothing") << QVariantMap({ { mode, QStringLiteral("apply") } }) << QStringList({ operations });
    QTest::newRow("nothing at all") << QVariantMap() << QStringList({ operations });
    QTest::newRow("apply with an empty script") << QVariantMap({ { mode, QStringLiteral("apply") }, { operations, QString() } }) << QStringList({ operations });
    QTest::newRow("apply with no snapshot") << QVariantMap({ { session, noSnapshot } }) << QStringList({ operations });
    QTest::newRow("apply with both") << QVariantMap({ { session, snapshot }, { operations, script } }) << QStringList({ operations });
    QTest::newRow("apply with the changes of the editor") << QVariantMap({ { mode, QStringLiteral("apply") }, { session, snapshot } }) << QStringList();
    QTest::newRow("apply with a script") << QVariantMap({ { operations, script } }) << QStringList();

    // A script, which cannot be read: one error for each mistake, with its path
    QTest::newRow("apply with a wrong script") << QVariantMap({ { operations, QStringLiteral("{\"items\":[5,6]}") } }) << QStringList({ operations, operations });

    // A value of the other mode would be silently ignored
    QTest::newRow("apply with the pages of a listing") << QVariantMap({ { operations, script }, { QStringLiteral("pages"), QStringLiteral("1") } }) << QStringList({ QStringLiteral("pages") });
    QTest::newRow("apply with the paging of a listing") << QVariantMap({ { operations, script }, { QStringLiteral("depth"), 2 }, { QStringLiteral("offset"), 10 }, { QStringLiteral("maximumObjects"), 5 } })
                                                        << QStringList({ QStringLiteral("depth"), QStringLiteral("offset"), QStringLiteral("maximumObjects") });
    QTest::newRow("list") << QVariantMap({ { mode, QStringLiteral("list") } }) << QStringList();
    QTest::newRow("list with its parameters") << QVariantMap({ { mode, QStringLiteral("list") }, { QStringLiteral("pages"), QStringLiteral("1") }, { QStringLiteral("depth"), 2 }, { QStringLiteral("offset"), 10 }, { QStringLiteral("maximumObjects"), 5 } }) << QStringList();
    QTest::newRow("list with a script") << QVariantMap({ { mode, QStringLiteral("list") }, { operations, script } }) << QStringList({ operations });
    QTest::newRow("list with the changes of the editor") << QVariantMap({ { mode, QStringLiteral("list") }, { session, snapshot } }) << QStringList({ session });
    QTest::newRow("list with skipped pages") << QVariantMap({ { mode, QStringLiteral("list") }, { QStringLiteral("skipPages"), QStringLiteral("1") } }) << QStringList({ QStringLiteral("skipPages") });
}

void DocumentToolContentTest::toolCheckGivenParameters()
{
    QFETCH(QVariantMap, given);
    QFETCH(QStringList, parameters);

    Tool tool;
    const std::vector<PDFDocumentToolParameterError> errors = tool.checkGivenParameters(given);

    QStringList errorParameters;
    for (const PDFDocumentToolParameterError& error : errors)
    {
        errorParameters << error.parameter;
        QVERIFY(!error.message.isEmpty());
    }
    QCOMPARE(errorParameters, parameters);

    if (QByteArray(QTest::currentDataTag()) == "apply with a wrong script")
    {
        QVERIFY2(errors[0].message.startsWith(QStringLiteral("operations.items[0]: ")), qPrintable(errors[0].message));
        QVERIFY2(errors[1].message.startsWith(QStringLiteral("operations.items[1]: ")), qPrintable(errors[1].message));
    }
}

void DocumentToolContentTest::toolValidate()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);
    const QVariantMap defaults = PDFDocumentToolParameters::getDefaultValues(tool.getParameters());
    const QVariant snapshot = QVariant::fromValue(snapshotPointerOf({ buildPage(document, 0) }));
    const QString script = QStringLiteral("{\"revisions\":{\"%1\":\"abcd\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}");

    const auto validate = [&tool, &context, &defaults](const QVariantMap& values)
    {
        QVariantMap parameters = defaults;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
        {
            parameters.insert(it.key(), it.value());
        }

        QStringList errorParameters;
        for (const PDFDocumentToolParameterError& error : tool.validate(context, parameters))
        {
            errorParameters << error.parameter;
        }
        return errorParameters;
    };

    // The panels hold every value: the rule of the two sources is checked here too
    QCOMPARE(validate({ }), QStringList({ QStringLiteral("operations") }));
    QCOMPARE(validate({ { QStringLiteral("session"), snapshot } }), QStringList());
    QCOMPARE(validate({ { QStringLiteral("operations"), script.arg(2) } }), QStringList());
    QCOMPARE(validate({ { QStringLiteral("session"), snapshot }, { QStringLiteral("operations"), script.arg(2) } }), QStringList({ QStringLiteral("operations") }));
    QCOMPARE(validate({ { QStringLiteral("operations"), QStringLiteral("[]") } }), QStringList({ QStringLiteral("operations") }));

    // A revision of a page, which the document does not have
    QCOMPARE(validate({ { QStringLiteral("operations"), script.arg(3) } }), QStringList({ QStringLiteral("operations") }));

    // The skipped pages must be pages of the document
    QCOMPARE(validate({ { QStringLiteral("session"), snapshot }, { QStringLiteral("skipPages"), QStringLiteral("2") } }), QStringList());
    QCOMPARE(validate({ { QStringLiteral("session"), snapshot }, { QStringLiteral("skipPages"), QStringLiteral("3") } }), QStringList({ QStringLiteral("skipPages") }));

    // list: the pages of the document, at most 20 of them a call (zadani_overeni.md 7.1)
    QCOMPARE(validate({ { QStringLiteral("mode"), QStringLiteral("list") } }), QStringList());
    QCOMPARE(validate({ { QStringLiteral("mode"), QStringLiteral("list") }, { QStringLiteral("pages"), QStringLiteral("2") } }), QStringList());
    QCOMPARE(validate({ { QStringLiteral("mode"), QStringLiteral("list") }, { QStringLiteral("pages"), QStringLiteral("3") } }), QStringList({ QStringLiteral("pages") }));

    QList<QByteArray> manyContents;
    for (int i = 0; i < 21; ++i)
    {
        manyContents << QByteArray(ONE_PATH);
    }
    const PDFDocumentToolContext manyContext = contextOf(createPagesDocument(manyContents));
    QVariantMap listParameters = defaults;
    listParameters.insert(QStringLiteral("mode"), QStringLiteral("list"));
    std::vector<PDFDocumentToolParameterError> errors = tool.validate(manyContext, listParameters);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors[0].parameter, QStringLiteral("pages"));
    listParameters.insert(QStringLiteral("pages"), QStringLiteral("2-21"));
    QVERIFY(tool.validate(manyContext, listParameters).empty());
    listParameters.insert(QStringLiteral("pages"), QStringLiteral("1-21"));
    QCOMPARE(tool.validate(manyContext, listParameters).size(), size_t(1));

    // No document
    QVERIFY(!tool.validate(PDFDocumentToolContext(), defaults).empty());
}

void DocumentToolContentTest::toolApplySnapshot()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);

    // The work of the panels: the second path of the page 1 removed, a rectangle added to the
    // page 2, the path of the page 2 with the alpha 0.5
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId());
    const PDFContentContainerPointer otherOriginal = buildPage(document, 1);
    const PDFContentContainerPointer otherEdited = appendRectangle(setOverride(otherOriginal, nodesOfKind(*otherOriginal, PDFContentNodeKind::Path)[0]->getId(), alphaOverride()), "0 0 5 5", FIRST_NEW_NODE);

    const PDFDocumentToolResult result = tool.run(context, applyParameters(snapshotPointerOf({ edited, otherEdited })), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));

    // KE.9: the changed document with the flag Reset
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocumentToolOutputDocument& output = result.documents[0];
    QVERIFY(output.kind == PDFDocumentToolOutputDocument::Kind::Changed);
    QCOMPARE(output.inputIndex, 0);
    QVERIFY(output.flags == PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Reset));
    QCOMPARE(output.suggestedFileName, QStringLiteral("Report_edited.pdf"));
    QVERIFY(result.outputs.empty());

    // The document of the result has the changes; the input document does not
    QCOMPARE(pageContent(*output.document, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QVERIFY(pageContent(*output.document, 1).contains("0 0 5 5 re\nf"));
    QVERIFY(pageContent(*output.document, 1).contains("/GS1 gs"));
    QCOMPARE(pageContent(*context.documents[0].document, 0), QByteArray(TWO_PATHS));
    QCOMPARE(pageContent(*context.documents[0].document, 1), QByteArray(ONE_PATH));

    // The data of MCP (zadani_overeni.md 7.1): the numbers of the pages from 1 and the counts
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1, 2 }));
    QCOMPARE(result.data.value(QStringLiteral("objectsAdded")).toInt(-1), 1);
    QCOMPARE(result.data.value(QStringLiteral("objectsEdited")).toInt(-1), 1);
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 1);
    QCOMPARE(result.data.value(QStringLiteral("structure")), QJsonValue(false));
    QCOMPARE(result.data.value(QStringLiteral("fonts")).toInt(-1), 0);
    QCOMPARE(result.data.value(QStringLiteral("layers")).toInt(-1), 0);
    QVERIFY(!result.data.contains(QStringLiteral("pageErrors")));
    QVERIFY(!result.data.contains(QStringLiteral("notTagged")));

    // A signed document is saved incrementally (QE.3); this one is neither signed nor certified
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));
    QCOMPARE(result.data.value(QStringLiteral("signatureCount")).toInt(-1), 0);
    QCOMPARE(result.data.value(QStringLiteral("certified")), QJsonValue(false));

    // The facts of the result dialog: the pages and the objects; nothing about the structure,
    // the fonts or a standard, when nothing of them changed
    QStringList factIds;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        factIds << fact.id;
        QVERIFY(!fact.title.isEmpty());
        QVERIFY(!fact.value.isEmpty());
    }
    QCOMPARE(factIds, QStringList({ QStringLiteral("pagesChanged"), QStringLiteral("objects") }));
    QVERIFY2(result.facts[0].value.contains(QStringLiteral("2")), qPrintable(result.facts[0].value));
    QCOMPARE(result.facts[1].value, QStringLiteral("1 added, 1 edited, 1 removed"));
}

void DocumentToolContentTest::toolDocumentChanged()
{
    // RE-15: the changes were made over a document, which is not the document of the run
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocument changedDocument = createPagesDocument({ QByteArray("0.5 g\n") + TWO_PATHS, ONE_PATH });

    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshotPointer snapshot = snapshotPointerOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()) });

    const PDFDocumentToolResult result = tool.run(contextOf(changedDocument), applyParameters(snapshot), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The document changed in the meantime."));
    QVERIFY(result.documents.empty());
    QVERIFY(!result.isUse());

    // Over its own document the same snapshot runs
    QVERIFY(tool.run(contextOf(document), applyParameters(snapshot), nullptr, nullptr).isSuccess());
}

void DocumentToolContentTest::toolPageErrors()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);

    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentContainerPointer edited = removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId());
    const PDFContentContainerPointer otherOriginal = buildPage(document, 1);

    // The path of the page 2 is the node 3 (the root 1, the state run 2, the path 3)
    const PDFContentNodeId brokenNode = nodesOfKind(*otherOriginal, PDFContentNodeKind::Path)[0]->getId();
    QCOMPARE(brokenNode.value, quint64(3));
    const PDFContentEditSnapshotPointer snapshot = snapshotPointerOf({ edited, setOverride(otherOriginal, brokenNode, brokenOverride()) });

    // ---- A page, which cannot be written: the run fails with the errors of the pages as data
    //      (zadani_jadro.md 4.4) and with no document ----
    const PDFDocumentToolResult result = tool.run(context, applyParameters(snapshot), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(!result.message.isEmpty());
    QVERIFY(result.documents.empty());

    const QJsonArray pageErrors = result.data.value(QStringLiteral("pageErrors")).toArray();
    QCOMPARE(pageErrors.size(), 1);
    const QJsonObject page = pageErrors[0].toObject();
    QCOMPARE(page.keys(), QStringList({ QStringLiteral("errors"), QStringLiteral("page") }));
    QCOMPARE(page.value(QStringLiteral("page")).toInt(), 2);
    const QJsonArray errors = page.value(QStringLiteral("errors")).toArray();
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors[0].toObject().keys(), QStringList({ QStringLiteral("id"), QStringLiteral("text") }));
    QCOMPARE(errors[0].toObject().value(QStringLiteral("id")).toString(), QStringLiteral("p2:3"));
    QVERIFY(!errors[0].toObject().value(QStringLiteral("text")).toString().isEmpty());

    // ---- Skip Page 2: a new run with the page in skipPages writes the rest ----
    QVariantMap parameters = applyParameters(snapshot);
    parameters.insert(QStringLiteral("skipPages"), QStringLiteral("2"));
    const PDFDocumentToolResult skipped = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(skipped.status == PDFDocumentToolResultStatus::Success, qPrintable(skipped.message));
    QCOMPARE(skipped.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1 }));
    QCOMPARE(skipped.documents.size(), size_t(1));
    QCOMPARE(pageContent(*skipped.documents[0].document, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QCOMPARE(pageContent(*skipped.documents[0].document, 1), QByteArray(ONE_PATH));

    // Every changed page skipped: nothing is left to apply
    parameters.insert(QStringLiteral("skipPages"), QStringLiteral("1-2"));
    const PDFDocumentToolResult nothing = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY(nothing.status == PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(nothing.documents.empty());

    // ---- An error inside of a form is named through the node of the page, which draws the form ----
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument formsDocument = createFormsDocument(&form1, &form2);
    const PDFContentContainerPointer form = buildContainer(formsDocument, PDFContentContainerKey::createForm(form2), 0);
    const PDFContentContainerPointer formsPage = buildPage(formsDocument, 0);

    // The page 1 "/Fm1 Do /Fm2 Do": the root 1, the forms 2 and 3; the form "/GP gs 1 0 0 rg
    // 0 0 10 10 re f": the root 1, the state run 2, the path 3
    const std::vector<PDFContentNodePointer> uses = nodesOfKind(*formsPage, PDFContentNodeKind::FormUse);
    QCOMPARE(uses.size(), size_t(2));
    QCOMPARE(uses[1]->getId().value, quint64(3));
    const PDFContentNodeId formPath = nodesOfKind(*form, PDFContentNodeKind::Path)[0]->getId();
    QCOMPARE(formPath.value, quint64(3));

    const PDFDocumentToolResult formResult = tool.run(contextOf(formsDocument), applyParameters(snapshotPointerOf({ formsPage, setOverride(form, formPath, brokenOverride()) })), nullptr, nullptr);
    QVERIFY(formResult.status == PDFDocumentToolResultStatus::Failed);
    const QJsonArray formPageErrors = formResult.data.value(QStringLiteral("pageErrors")).toArray();
    QCOMPARE(formPageErrors.size(), 1);
    QCOMPARE(formPageErrors[0].toObject().value(QStringLiteral("page")).toInt(), 1);
    const QJsonObject formError = formPageErrors[0].toObject().value(QStringLiteral("errors")).toArray()[0].toObject();
    QCOMPARE(formError.value(QStringLiteral("id")).toString(), QStringLiteral("p1:3/3"));
    QCOMPARE(formError.value(QStringLiteral("form")).toInt(), int(form2.objectNumber));
}

void DocumentToolContentTest::toolNothingToDo()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);

    // A snapshot without a change; a snapshot with containers, which were not changed
    for (const PDFContentEditSnapshotPointer& snapshot : { std::make_shared<const PDFContentEditSnapshot>(), snapshotPointerOf({ buildPage(document, 0) }) })
    {
        const PDFDocumentToolResult result = tool.run(context, applyParameters(snapshot), nullptr, nullptr);
        QVERIFY(result.status == PDFDocumentToolResultStatus::NothingToDo);
        QVERIFY(!result.message.isEmpty());
        QVERIFY(result.documents.empty());
    }

    // A direct call without a document or without a source fails, it never crashes
    QVERIFY(tool.execute(PDFDocumentToolContext(), applyParameters(snapshotPointerOf({ buildPage(document, 0) })), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(tool.execute(context, QVariantMap(), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(tool.execute(context, QVariantMap({ { QStringLiteral("session"), 5 } }), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
}

void DocumentToolContentTest::toolCancel()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshotPointer snapshot = snapshotPointerOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()) });

    // Canceled during the writing (the first question is the one of the writer)
    const ContentToolTestControl control(0);
    const PDFDocumentToolResult result = tool.execute(contextOf(document), applyParameters(snapshot), &control, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.documents.empty());

    // The same through the checked run
    const ContentToolTestControl runControl(0);
    QVERIFY(tool.run(contextOf(document), applyParameters(snapshot), &runControl, nullptr).status == PDFDocumentToolResultStatus::Stopped);
}

void DocumentToolContentTest::toolCertified()
{
    // A certified document (ISO 32000-2, 12.8.2.2: /Perms /DocMDP refers to a signature with the
    // transform method DocMDP; /P 2 allows form filling and signing). No level allows a change
    // of the content of a page (Table 257): the certification is removed and the result can only
    // be saved as a copy (QE.3).
    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference pageReference = documentBuilder.appendPage(QRectF(0, 0, 200, 200));
    documentBuilder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", resourcesObject() },
                                                                             { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), TWO_PATHS) } }));

    const PDFObject transformParameters = Documents::createDictionaryObject({ { "Type", PDFObject::createName("TransformParams") }, { "P", PDFObject::createInteger(2) }, { "V", PDFObject::createName("1.2") } });
    const PDFObject signatureReference = Documents::createDictionaryObject({ { "Type", PDFObject::createName("SigRef") }, { "TransformMethod", PDFObject::createName("DocMDP") }, { "TransformParams", transformParameters } });
    const PDFObject signature = Documents::addObject(&documentBuilder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Sig") },
                                                                                                         { "Filter", PDFObject::createName("Adobe.PPKLite") },
                                                                                                         { "SubFilter", PDFObject::createName("adbe.pkcs7.detached") },
                                                                                                         { "ByteRange", Documents::createIntegerArrayObject({ 0, 0, 0, 0 }) },
                                                                                                         { "Contents", PDFObject::createString(QByteArray(1, '\0')) },
                                                                                                         { "Reference", Documents::createArrayObject({ signatureReference }) } }));
    const PDFObject field = Documents::addObject(&documentBuilder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Annot") },
                                                                                                     { "Subtype", PDFObject::createName("Widget") },
                                                                                                     { "FT", PDFObject::createName("Sig") },
                                                                                                     { "T", PDFObject::createString("cert") },
                                                                                                     { "Rect", Documents::createIntegerArrayObject({ 0, 0, 0, 0 }) },
                                                                                                     { "P", PDFObject::createReference(pageReference) },
                                                                                                     { "V", signature } }));
    documentBuilder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Annots", Documents::createArrayObject({ field }) } }));
    documentBuilder.mergeTo(documentBuilder.getCatalogReference(), Documents::createDictionaryObject({ { "AcroForm", Documents::createDictionaryObject({ { "Fields", Documents::createArrayObject({ field }) }, { "SigFlags", PDFObject::createInteger(3) } }) },
                                                                                                         { "Perms", Documents::createDictionaryObject({ { "DocMDP", signature } }) } }));
    const PDFDocument document = documentBuilder.build();

    Tool tool;
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshotPointer snapshot = snapshotPointerOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()) });
    const PDFDocumentToolResult result = tool.run(contextOf(document), applyParameters(snapshot), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));

    QCOMPARE(result.data.value(QStringLiteral("certified")), QJsonValue(true));
    QCOMPARE(result.data.value(QStringLiteral("certificationRemoved")), QJsonValue(true));
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QCOMPARE(result.data.value(QStringLiteral("signatureCount")).toInt(-1), 1);

    // The document of the result has the change and no certification
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocument& output = *result.documents[0].document;
    QCOMPARE(pageContent(output, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    const PDFDictionary* catalog = output.getDictionaryFromObject(output.getTrailerDictionary()->get("Root"));
    const PDFDictionary* permissions = output.getDictionaryFromObject(catalog->get("Perms"));
    QVERIFY(!permissions || !permissions->hasKey("DocMDP"));

    // The mode list changes no page: it is no reason to remove a certification
    QVERIFY(!tool.getOutputTraits({ { QStringLiteral("mode"), QStringLiteral("list") } }).isPageChange);
    QVERIFY(tool.getOutputTraits({ { QStringLiteral("mode"), QStringLiteral("apply") } }).isPageChange);
}

void DocumentToolContentTest::toolConformance()
{
    // QE.2 (a): the document declares PDF/A-2b and PDF/UA-1 in its XMP metadata. The content of
    // the result is not checked against them, so the declarations are removed and the result
    // says so.
    PDFDocumentBuilder documentBuilder;
    const PDFObjectReference pageReference = documentBuilder.appendPage(QRectF(0, 0, 200, 200));
    documentBuilder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", resourcesObject() },
                                                                             { "Contents", Documents::addStreamObject(&documentBuilder, Documents::DictionaryEntries(), TWO_PATHS) } }));
    documentBuilder.setCatalogMetadata(QByteArray("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
                                                  "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                                                  "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\">"
                                                  "<pdfaid:part>2</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance><pdfuaid:part>1</pdfuaid:part>"
                                                  "</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>"));
    const PDFDocument document = documentBuilder.build();
    QCOMPARE(PDFDocumentTool::getConformanceDeclarations(&document), QStringList({ QStringLiteral("PDF/A"), QStringLiteral("PDF/UA") }));

    Tool tool;
    const PDFContentContainerPointer original = buildPage(document, 0);
    const PDFContentEditSnapshotPointer snapshot = snapshotPointerOf({ removeNode(original, nodesOfKind(*original, PDFContentNodeKind::Path)[1]->getId()) });
    const PDFDocumentToolResult result = tool.run(contextOf(document), applyParameters(snapshot), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));

    QCOMPARE(result.data.value(QStringLiteral("conformanceRemoved")).toArray(), QJsonArray({ QStringLiteral("PDF/A"), QStringLiteral("PDF/UA") }));
    QStringList factIds;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        factIds << fact.id;
    }
    QCOMPARE(factIds, QStringList({ QStringLiteral("pagesChanged"), QStringLiteral("objects"), QStringLiteral("conformance") }));

    // The document of the result has the change and declares no standard
    const PDFDocument& output = *result.documents[0].document;
    QCOMPARE(pageContent(output, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QCOMPARE(PDFDocumentTool::getConformanceDeclarations(&output), QStringList());
}

void DocumentToolContentTest::toolNotTagged()
{
    // QE.2: the result of a tagged document says, how many objects of its written pages have no
    // tag. The tagged page has four objects, all inside of marked content sequences (two texts,
    // the rectangle of the artifact, the square of the figure).
    Tool tool;
    const PDFDocument document = Documents::createTaggedDocument();
    const PDFContentContainerPointer original = buildPage(document, 0);

    // A new rectangle at the end of the page is outside of every sequence
    {
        const PDFDocumentToolResult result = tool.run(contextOf(document), applyParameters(snapshotPointerOf({ appendRectangle(original, "0 0 5 5", FIRST_NEW_NODE) })), nullptr, nullptr);
        QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));
        QCOMPARE(result.data.value(QStringLiteral("notTagged")).toInt(-1), 1);
        QCOMPARE(result.data.value(QStringLiteral("objectsAdded")).toInt(-1), 1);

        QStringList factIds;
        for (const PDFDocumentToolFact& fact : result.facts)
        {
            factIds << fact.id;
        }
        QCOMPARE(factIds, QStringList({ QStringLiteral("pagesChanged"), QStringLiteral("objects"), QStringLiteral("notTagged") }));
    }

    // A change, which leaves every object tagged: the count is 0 and the fact is left out
    {
        PDFContentNodeId figure;
        for (const PDFContentNodePointer& node : nodesOfKind(*original, PDFContentNodeKind::Path))
        {
            figure = node->getId();     // The last path is the square of the figure
        }
        const PDFDocumentToolResult result = tool.run(contextOf(document), applyParameters(snapshotPointerOf({ setOverride(original, figure, alphaOverride()) })), nullptr, nullptr);
        QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));
        QCOMPARE(result.data.value(QStringLiteral("notTagged")).toInt(-1), 0);
        QCOMPARE(result.data.value(QStringLiteral("objectsEdited")).toInt(-1), 1);
        for (const PDFDocumentToolFact& fact : result.facts)
        {
            QVERIFY(fact.id != QLatin1String("notTagged"));
        }
    }
}

// ---------------------------------------------------------------------------------------------
//  Identifiers and the listing against a session (zadani_overeni.md 7.1)
// ---------------------------------------------------------------------------------------------

void DocumentToolContentTest::toolSkippedPageForm()
{
    // Owner 2026-11-26: a skipped page stays as it is. The form /Fm1 is drawn by both pages; its
    // path is removed, and on the page 1 the use of /Fm2 is removed. With the page 2 skipped
    // the form is not written (it is one object: the page 2 would change with it); the page 1
    // is written with its own change, and the result says, what was left as it is and why.
    Tool tool;
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);
    const PDFDocumentToolContext context = contextOf(document);

    const PDFContentContainerPointer form = buildContainer(document, PDFContentContainerKey::createForm(form1), 0);
    const PDFContentContainerPointer formEdited = removeNode(form, nodesOfKind(*form, PDFContentNodeKind::Path)[0]->getId());
    const PDFContentContainerPointer page = buildPage(document, 0);
    const PDFContentContainerPointer pageEdited = removeNode(page, nodesOfKind(*page, PDFContentNodeKind::FormUse)[1]->getId());

    QVariantMap parameters = applyParameters(snapshotPointerOf({ formEdited, pageEdited }));
    parameters.insert(QStringLiteral("skipPages"), QStringLiteral("2"));
    PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));

    // The page 2 is not among the changed pages; one object was removed (the use of /Fm2), not two
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1 }));
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 1);

    // The form is named with the skipped page, which keeps it as it is
    QCOMPARE(QByteArray(Tool::DATA_FORMS_NOT_WRITTEN), QByteArray("formsNotWritten"));
    const QJsonArray notWritten = result.data.value(QStringLiteral("formsNotWritten")).toArray();
    QCOMPARE(notWritten.size(), 1);
    QCOMPARE(notWritten[0].toObject(), QJsonObject({ { QStringLiteral("object"), qint64(form1.objectNumber) },
                                                     { QStringLiteral("kind"), QStringLiteral("form") },
                                                     { QStringLiteral("skippedPages"), QJsonArray({ 2 }) } }));
    const auto isNotWrittenFact = [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("formsNotWritten"); };
    QCOMPARE(std::count_if(result.facts.cbegin(), result.facts.cend(), isNotWrittenFact), 1);

    // The document: the form and the page 2 as they were, the page 1 without its second form
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocument& output = *result.documents[0].document;
    QVERIFY(output.getObjectByReference(form1) == document.getObjectByReference(form1));
    const PDFObjectReference skippedPage = document.getCatalog()->getPage(1)->getPageReference();
    QVERIFY(output.getObjectByReference(skippedPage) == document.getObjectByReference(skippedPage));
    QCOMPARE(pageContent(output, 0), QByteArray("/Fm1 Do"));

    // ---- Without a skipped page nothing is left: no such data, no such fact ----
    result = tool.run(context, applyParameters(snapshotPointerOf({ formEdited, pageEdited })), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1, 2 }));
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 2);
    QVERIFY(!result.data.contains(QStringLiteral("formsNotWritten")));
    QVERIFY(!result.data.contains(QStringLiteral("pagesChangedIncomplete")));
    QCOMPARE(std::count_if(result.facts.cbegin(), result.facts.cend(), isNotWrittenFact), 0);

    // ---- The form is the only change: with its page skipped nothing is written, and the
    //      result still says, that the form was left ----
    parameters = applyParameters(snapshotPointerOf({ formEdited }));
    parameters.insert(QStringLiteral("skipPages"), QStringLiteral("2"));
    result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.documents.empty());
    QCOMPARE(result.data.value(QStringLiteral("formsNotWritten")).toArray().size(), 1);
    QCOMPARE(result.data.value(QStringLiteral("formsNotWritten")).toArray()[0].toObject().value(QStringLiteral("skippedPages")).toArray(), QJsonArray({ 2 }));
}

QVariantMap DocumentToolContentTest::scriptParameters(const QString& script)
{
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_MODE), QString::fromLatin1(Tool::MODE_APPLY));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_OPERATIONS), script);
    return parameters;
}

QVariantMap DocumentToolContentTest::listParameters(const QString& pages, int depth, qint64 offset, int maximumObjects)
{
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_MODE), QString::fromLatin1(Tool::MODE_LIST));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_PAGES), pages);
    if (depth >= 0)
    {
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_DEPTH), depth);
    }
    if (offset >= 0)
    {
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_OFFSET), offset);
    }
    if (maximumObjects >= 0)
    {
        parameters.insert(QString::fromLatin1(Tool::PARAMETER_MAXIMUM_OBJECTS), maximumObjects);
    }
    return parameters;
}

bool DocumentToolContentTest::loadPageWithForms(PDFContentEditSession* session, PDFInteger pageIndex)
{
    QString errorText;
    const PDFContentContainerKey page = session->getPageKey(pageIndex);
    if (!page.isValid() || !session->loadContainer(page, nullptr, &errorText))
    {
        return false;
    }

    // Each container once (a form drawing itself ends)
    std::set<PDFContentContainerKey> visited = { page };
    std::vector<PDFContentContainerKey> queue = { page };
    for (size_t i = 0; i < queue.size(); ++i)
    {
        const PDFContentContainerPointer container = session->getContainer(queue[i]);
        for (const PDFContentNodePointer& use : nodesOfKind(*container, PDFContentNodeKind::FormUse))
        {
            const PDFContentContainerKey form = PDFContentContainerKey::createForm(use->getData().xobject);
            if (form.isValid() && visited.insert(form).second && session->loadContainer(form, nullptr, &errorText))
            {
                queue.push_back(form);
            }
        }
    }

    return true;
}

QString DocumentToolContentTest::revisionOf(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
    return loadPageWithForms(&session, pageIndex) ? QString::fromLatin1(PDFContentJson::getPageRevision(&session, pageIndex).toHex()) : QString();
}

PDFDocument DocumentToolContentTest::createTaggedTwoPagesDocument()
{
    const PDFDocument tagged = Documents::createTaggedDocument();
    PDFDocumentBuilder builder(&tagged);
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    builder.mergeTo(page, Documents::createDictionaryObject({ { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), ONE_PATH) } }));
    return builder.build();
}

PDFContentStructureModelPointer DocumentToolContentTest::modelWithoutSequence(const PDFDocument& document, PDFInteger mcid)
{
    const PDFContentStructureModelPointer model = PDFContentStructureModel::read(&document, nullptr);
    if (!model)
    {
        return PDFContentStructureModelPointer();
    }

    const PDFContentStructureChange change = model->getChangeForDelete(0, mcid, true);
    return change.action == PDFContentStructureChange::Action::RemoveSequence ? model->applied({ change }) : PDFContentStructureModelPointer();
}

PDFContentContainerPointer DocumentToolContentTest::taggedPageWithoutSequence(const PDFDocument& document, PDFInteger mcid)
{
    const PDFContentContainerPointer page = buildPage(document, 0);
    for (const PDFContentNodePointer& node : nodesOfKind(*page, PDFContentNodeKind::MarkedContent))
    {
        if (node->getData().mcid == mcid)
        {
            return removeNode(page, node->getId());
        }
    }
    return PDFContentContainerPointer();
}

QJsonObject DocumentToolContentTest::listedObject(const QJsonArray& objects, std::initializer_list<int> path)
{
    QJsonArray current = objects;
    QJsonObject object;
    for (const int index : path)
    {
        object = current.at(index).toObject();
        current = object.value(QStringLiteral("children")).toArray();
    }
    return object;
}

QStringList DocumentToolContentTest::listedIds(const QJsonArray& objects)
{
    QStringList ids;
    for (const QJsonValueConstRef value : objects)
    {
        const QJsonObject object = value.toObject();
        if (!object.value(QStringLiteral("partial")).toBool())
        {
            ids << object.value(QStringLiteral("id")).toString();
        }
        ids << listedIds(object.value(QStringLiteral("children")).toArray());
    }
    return ids;
}

QList<QJsonObject> DocumentToolContentTest::listedOfKind(const QJsonArray& objects, const QString& kind)
{
    QList<QJsonObject> result;
    for (const QJsonValueConstRef value : objects)
    {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString() == kind)
        {
            result << object;
        }
        result << listedOfKind(object.value(QStringLiteral("children")).toArray(), kind);
    }
    return result;
}

QByteArray DocumentToolContentTest::actualTextContent(int sequenceCount, bool inGroups)
{
    const QByteArray text(60000, 'A');
    QByteArray content;
    for (int i = 0; i < sequenceCount; ++i)
    {
        content += QByteArray(inGroups ? "q " : "") + "/Span << /ActualText (" + text + ") >> BDC 0 0 5 5 re f EMC" + QByteArray(inGroups ? " Q\n" : "\n");
    }
    return content;
}

PDFObject DocumentToolContentTest::formResources(Documents::DictionaryEntries forms)
{
    return Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject(std::move(forms)) } });
}

PDFDocument DocumentToolContentTest::createNestedFormsDocument()
{
    const auto repeat = [](const QByteArray& line)
    {
        QByteArray content;
        for (int i = 0; i < 1000; ++i)
        {
            content += line;
        }
        return content;
    };

    const auto addForm = [](PDFDocumentBuilder* builder, const QByteArray& content, PDFObject resources)
    {
        return Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                     { "Subtype", PDFObject::createName("Form") },
                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) },
                                                     { "Resources", std::move(resources) } }, content);
    };

    PDFDocumentBuilder builder;
    const PDFObject form3 = addForm(&builder, "0 0 1 1 re f", Documents::createDictionaryObject({ }));
    const PDFObject form2 = addForm(&builder, repeat("/Fm3 Do\n"), formResources({ { "Fm3", form3 } }));
    const PDFObject form1 = addForm(&builder, repeat("/Fm2 Do\n"), formResources({ { "Fm2", form2 } }));

    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", formResources({ { "Fm1", form1 } }) },
                                                              { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), repeat("/Fm1 Do\n")) } }));
    return builder.build();
}

PDFDocument DocumentToolContentTest::createSelfDrawingFormDocument()
{
    PDFDocumentBuilder builder;

    // The form refers to itself: its reference first
    const PDFObject form = PDFObject::createReference(builder.addObject(PDFObject()));
    PDFDictionaryBuilder dictionary;
    dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    dictionary.addEntry(PDFInplaceOrMemoryString("BBox"), Documents::createIntegerArrayObject({ 0, 0, 100, 100 }));
    dictionary.addEntry(PDFInplaceOrMemoryString("Resources"), formResources({ { "Fm1", form } }));
    builder.setObject(form.getReference(), PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray("/Fm1 Do 0 0 1 1 re f"))));

    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
    builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", formResources({ { "Fm1", form } }) },
                                                              { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Fm1 Do") } }));
    return builder.build();
}

void DocumentToolContentTest::nodeIdResolves()
{
    // The page 1 "/Fm1 Do /Fm2 Do": the root 1, the forms 2 and 3. The form /Fm1 "/GF gs 0 1 0 rg
    // 5 5 20 10 re f": the root 1, the state run 2, the path 3.
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
    const PDFContentContainerKey page = session.getPageKey(0);
    const PDFContentContainerKey form = PDFContentContainerKey::createForm(form1);

    const auto read = [&session](const char* text, bool* hasError)
    {
        QString errorText;
        const PDFContentNodeReference reference = PDFContentJson::readNodeId(QString::fromLatin1(text), &session, &errorText);
        *hasError = !errorText.isEmpty();
        return reference;
    };
    bool hasError = false;

    // ---- Nothing is loaded: no identifier names a node ----
    QVERIFY(!read("p1:2", &hasError).isValid());
    QVERIFY(hasError);

    // ---- The page is loaded ----
    QString errorText;
    QVERIFY2(session.loadContainer(page, nullptr, &errorText), qPrintable(errorText));
    QVERIFY(read("p1:1", &hasError) == (PDFContentNodeReference{ page, PDFContentNodeId{ 1 } }));
    QVERIFY(!hasError);
    QVERIFY(read("p1:2", &hasError) == (PDFContentNodeReference{ page, PDFContentNodeId{ 2 } }));
    QVERIFY(!hasError);
    QVERIFY(read("p1:3", &hasError) == (PDFContentNodeReference{ page, PDFContentNodeId{ 3 } }));
    QVERIFY(!hasError);

    // A node, which does not exist; a page, which is not loaded; a form, which is not loaded
    for (const char* text : { "p1:4", "p1:4294967296", "p2:2", "p1:2/3" })
    {
        QVERIFY2(!read(text, &hasError).isValid(), text);
        QVERIFY2(hasError, text);
    }

    // One text for one node: another way to write the numbers of "p1:2" is no identifier (a
    // zero in front, a sign, a space, a solidus at the end)
    for (const char* text : { "p01:2", "p1:02", "p1:+2", "p1: 2", "p1:2 ", "p1:2/", " p1:2" })
    {
        QVERIFY2(!read(text, &hasError).isValid(), text);
        QVERIFY2(hasError, text);
    }

    // ---- The form /Fm1 is loaded: the path leads through the node, which draws it ----
    QVERIFY2(session.loadContainer(form, nullptr, &errorText), qPrintable(errorText));
    QVERIFY(read("p1:2/3", &hasError) == (PDFContentNodeReference{ form, PDFContentNodeId{ 3 } }));
    QVERIFY(!hasError);
    QVERIFY(read("p1:2/1", &hasError) == (PDFContentNodeReference{ form, PDFContentNodeId{ 1 } }));
    QVERIFY(!hasError);

    // No such node in the form; the other form is not loaded; a node, which is no form, has
    // nothing inside (the root of the page, a path of the form)
    for (const char* text : { "p1:2/4", "p1:3/3", "p1:1/2", "p1:2/3/1" })
    {
        QVERIFY2(!read(text, &hasError).isValid(), text);
        QVERIFY2(hasError, text);
    }

    // ---- Temporary names of a script ----
    const PDFContentNodeReference named{ page, PDFContentNodeId{ 3 } };
    QVERIFY(!read("t1", &hasError).isValid());
    QVERIFY(hasError);
    session.setScriptName(QStringLiteral("t1"), named);
    QVERIFY(read("t1", &hasError) == named);
    QVERIFY(!hasError);
    QVERIFY(!read("t2", &hasError).isValid());
    QVERIFY(hasError);

    // A text, which only resembles an identifier, is a name: it is looked up and never parsed
    session.setScriptName(QStringLiteral("P1:2"), named);
    QVERIFY(read("P1:2", &hasError) == named);
    QVERIFY(!hasError);

    // A text of the form of an identifier is never looked up as a name, also when the session
    // has such a name (the script reader refuses it; here the session is told directly)
    session.setScriptName(QStringLiteral("p1:9"), named);
    QVERIFY(!read("p1:9", &hasError).isValid());
    QVERIFY(hasError);

    // The names are gone with the end of a replay
    session.clearScriptNames();
    QVERIFY(!read("t1", &hasError).isValid());
    QVERIFY(hasError);

    // An empty text
    QVERIFY(!read("", &hasError).isValid());
    QVERIFY(hasError);
}

void DocumentToolContentTest::listPage()
{
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);

    // A page, which is not loaded, has no listing (the listing never loads)
    QVERIFY(PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions()).isEmpty());

    QString errorText;
    QVERIFY2(session.loadContainer(session.getPageKey(0), nullptr, &errorText), qPrintable(errorText));
    const QJsonObject page = PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions());

    // ---- The page: its number from 1, its revision (PDFContentJson::getPageRevision: 32 bytes
    //      of SHA-256 as 64 hexadecimal digits), the media box [0 0 200 200], no rotation, the
    //      user unit 1 ----
    QCOMPARE(page.value(QStringLiteral("page")).toInt(), 1);
    const QString revision = page.value(QStringLiteral("revision")).toString();
    QCOMPARE(revision.size(), 64);
    QCOMPARE(revision, revisionOf(document, 0));
    QCOMPARE(page.value(QStringLiteral("width")).toDouble(), 200.0);
    QCOMPARE(page.value(QStringLiteral("height")).toDouble(), 200.0);
    QCOMPARE(page.value(QStringLiteral("rotation")).toInt(-1), 0);
    QCOMPARE(page.value(QStringLiteral("userUnit")).toDouble(), 1.0);
    QCOMPARE(page.value(QStringLiteral("mediaBox")).toObject(), PDFContentJson::writeRect(QRectF(0, 0, 200, 200)));
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));
    QVERIFY(!page.contains(QStringLiteral("readOnly")));

    // ---- The objects: the four nodes of the page in the order of the content. A state run has
    //      no rectangle; a path has its box and the color it is filled with ----
    const QJsonArray objects = page.value(QStringLiteral("objects")).toArray();
    QCOMPARE(objects.size(), 4);

    const auto state = [](const QString& id) { return QJsonObject({ { QStringLiteral("id"), id }, { QStringLiteral("kind"), QStringLiteral("state") } }); };
    const auto path = [](const QString& id, const QRectF& rect, const QJsonArray& fill)
    {
        const QJsonObject color({ { QStringLiteral("space"), QStringLiteral("DeviceRGB") }, { QStringLiteral("values"), fill } });
        return QJsonObject({ { QStringLiteral("id"), id },
                             { QStringLiteral("kind"), QStringLiteral("path") },
                             { QStringLiteral("rect"), PDFContentJson::writeRect(rect) },
                             { QStringLiteral("appearance"), QJsonObject({ { QStringLiteral("fill"), color } }) } });
    };

    QCOMPARE(objects[0].toObject(), state(QStringLiteral("p1:2")));
    QCOMPARE(objects[1].toObject(), path(QStringLiteral("p1:3"), QRectF(10, 10, 50, 50), { 1, 0, 0 }));
    QCOMPARE(objects[2].toObject(), state(QStringLiteral("p1:4")));
    QCOMPARE(objects[3].toObject(), path(QStringLiteral("p1:5"), QRectF(100, 100, 20, 20), { 0, 0, 1 }));

    // The identifiers of the listing are the identifiers, which the reader resolves
    for (const QJsonValueConstRef object : objects)
    {
        const QString id = object.toObject().value(QStringLiteral("id")).toString();
        QVERIFY2(PDFContentJson::readNodeId(id, &session, &errorText).isValid(), qPrintable(id));
    }

    // ---- The second page is still not loaded; a page, which does not exist; no session ----
    QVERIFY(PDFContentJson::writePage(&session, 1, PDFContentJson::ListOptions()).isEmpty());
    QVERIFY(PDFContentJson::writePage(&session, 2, PDFContentJson::ListOptions()).isEmpty());
    QVERIFY(PDFContentJson::writePage(&session, -1, PDFContentJson::ListOptions()).isEmpty());
    QVERIFY(PDFContentJson::writePage(nullptr, 0, PDFContentJson::ListOptions()).isEmpty());

    // ---- A rotated page with a user unit and a media box, which does not begin at the origin ----
    PDFDocumentBuilder builder;
    const PDFObjectReference pageReference = builder.appendPage(QRectF(10, 20, 300, 400));
    builder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Rotate", PDFObject::createInteger(90) }, { "UserUnit", PDFObject::createReal(2.5) },
                                                                      { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), ONE_PATH) } }));
    const PDFDocument rotated = builder.build();
    PDFContentEditSession rotatedSession(PDFDocumentPointer(new PDFDocument(rotated)), PDFCMSPointer(), nullptr);
    QVERIFY(rotatedSession.loadContainer(rotatedSession.getPageKey(0), nullptr, &errorText));
    const QJsonObject rotatedPage = PDFContentJson::writePage(&rotatedSession, 0, PDFContentJson::ListOptions());
    QCOMPARE(rotatedPage.value(QStringLiteral("width")).toDouble(), 300.0);
    QCOMPARE(rotatedPage.value(QStringLiteral("height")).toDouble(), 400.0);
    QCOMPARE(rotatedPage.value(QStringLiteral("rotation")).toInt(), 90);
    QCOMPARE(rotatedPage.value(QStringLiteral("userUnit")).toDouble(), 2.5);
    QCOMPARE(rotatedPage.value(QStringLiteral("mediaBox")).toObject(), PDFContentJson::writeRect(QRectF(10, 20, 300, 400)));

    // The coordinates are those of the unrotated user space: the path is where the content puts it
    QCOMPARE(rotatedPage.value(QStringLiteral("objects")).toArray()[1].toObject().value(QStringLiteral("rect")).toObject(), PDFContentJson::writeRect(QRectF(20, 20, 30, 30)));
}

void DocumentToolContentTest::listPaging()
{
    // "q 1 0 0 rg 10 10 50 50 re f Q 0 0 5 5 re f": the root 1, the group 2 with the state run 3
    // and the path 4, the path 5. In the order of the content: p1:2, p1:3, p1:4, p1:5.
    const PDFDocument document = createPagesDocument({ QByteArray("q 1 0 0 rg 10 10 50 50 re f Q 0 0 5 5 re f") });
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
    QString errorText;
    QVERIFY2(session.loadContainer(session.getPageKey(0), nullptr, &errorText), qPrintable(errorText));

    const auto list = [&session](int depth, PDFInteger offset, int maximumObjects)
    {
        PDFContentJson::ListOptions options;
        options.depth = depth;
        options.offset = offset;
        options.maximumObjects = maximumObjects;
        return PDFContentJson::writePage(&session, 0, options);
    };
    const QString objectsKey = QStringLiteral("objects");
    const QString nextKey = QStringLiteral("nextOffset");
    const QString childrenKey = QStringLiteral("children");

    // ---- The depth 1: the two objects of the page; the group says it has two objects inside ----
    QJsonObject page = list(1, 0, 500);
    QJsonArray objects = page.value(objectsKey).toArray();
    QCOMPARE(objects.size(), 2);
    QCOMPARE(objects[0].toObject().value(QStringLiteral("id")).toString(), QStringLiteral("p1:2"));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("childCount")).toInt(), 2);
    QVERIFY(!objects[0].toObject().contains(childrenKey));
    QCOMPARE(objects[1].toObject().value(QStringLiteral("id")).toString(), QStringLiteral("p1:5"));
    QVERIFY(!objects[1].toObject().contains(QStringLiteral("childCount")));
    QVERIFY(!page.contains(nextKey));

    // ---- The depth 2: the group with its two objects ----
    page = list(2, 0, 500);
    objects = page.value(objectsKey).toArray();
    QCOMPARE(objects.size(), 2);
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:3"), QStringLiteral("p1:4"), QStringLiteral("p1:5") }));
    QCOMPARE(objects[0].toObject().value(childrenKey).toArray().size(), 2);
    QCOMPARE(listedObject(objects, { 0, 1 }).value(QStringLiteral("kind")).toString(), QStringLiteral("path"));
    QVERIFY(!page.contains(nextKey));

    // A deeper listing has nothing more to show; the depth is limited to 1 ... 16
    QCOMPARE(list(16, 0, 500), page);
    QCOMPARE(list(1000, 0, 500), page);
    QCOMPARE(list(0, 0, 500), list(1, 0, 500));
    QCOMPARE(list(-7, 0, 500), list(1, 0, 500));

    // ---- A window of one object: the group alone, the rest follows from the offset 1 ----
    page = list(2, 0, 1);
    objects = page.value(objectsKey).toArray();
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2") }));
    QVERIFY(!objects[0].toObject().contains(childrenKey));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("childCount")).toInt(), 2);
    QCOMPARE(page.value(nextKey).toInt(), 1);

    // ---- The window begins inside of the group: the group is only the place of its objects ----
    page = list(2, 1, 2);
    objects = page.value(objectsKey).toArray();
    QCOMPARE(objects.size(), 1);
    QCOMPARE(objects[0].toObject(), QJsonObject({ { QStringLiteral("id"), QStringLiteral("p1:2") }, { QStringLiteral("kind"), QStringLiteral("group") }, { QStringLiteral("partial"), true },
                                                 { childrenKey, objects[0].toObject().value(childrenKey) } }));
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:3"), QStringLiteral("p1:4") }));
    QCOMPARE(page.value(nextKey).toInt(), 3);

    page = list(2, 2, 500);
    objects = page.value(objectsKey).toArray();
    QCOMPARE(objects.size(), 2);
    QVERIFY(objects[0].toObject().value(QStringLiteral("partial")).toBool());
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:4"), QStringLiteral("p1:5") }));
    QVERIFY(!page.contains(nextKey));

    // ---- The last object, and nothing behind it ----
    page = list(2, 3, 500);
    QCOMPARE(listedIds(page.value(objectsKey).toArray()), QStringList({ QStringLiteral("p1:5") }));
    QCOMPARE(page.value(objectsKey).toArray().size(), 1);
    QVERIFY(!page.contains(nextKey));

    for (const PDFInteger offset : { PDFInteger(4), PDFInteger(5), PDFInteger(1000000000000) })
    {
        page = list(2, offset, 500);
        QVERIFY(page.value(objectsKey).toArray().isEmpty());
        QVERIFY(!page.contains(nextKey));
        QCOMPARE(page.value(QStringLiteral("page")).toInt(), 1);
    }

    // A negative offset is the beginning; no object and too many objects are 1 and 2 000
    QCOMPARE(list(2, -5, 500), list(2, 0, 500));
    QCOMPARE(list(2, 0, 0), list(2, 0, 1));
    QCOMPARE(list(2, 0, -3), list(2, 0, 1));
    QCOMPARE(list(2, 0, 1000000), list(2, 0, 2000));

    // ---- Every window size gives every object exactly once ----
    for (int size = 1; size <= 5; ++size)
    {
        QStringList ids;
        PDFInteger offset = 0;
        for (int call = 0; call < 10; ++call)
        {
            page = list(2, offset, size);
            const QStringList callIds = listedIds(page.value(objectsKey).toArray());
            QVERIFY(callIds.size() <= size);
            ids << callIds;
            if (!page.contains(nextKey))
            {
                break;
            }
            QCOMPARE(page.value(nextKey).toInteger(), qint64(offset + callIds.size()));
            offset = page.value(nextKey).toInteger();
        }
        QCOMPARE(ids, QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:3"), QStringLiteral("p1:4"), QStringLiteral("p1:5") }));
    }
}

void DocumentToolContentTest::listTextAndTags()
{
    // The tagged page: /P << /MCID 0 >> BDC BT /F1 12 Tf 20 150 Td (First) Tj ET EMC, the same
    // for /Span with the identifier 1 and (Second), /Artifact BMC 0 0 200 10 re f EMC and
    // /Figure << /MCID 3 >> BDC 20 20 50 50 re f EMC
    const PDFDocument document = Documents::createTaggedDocument();
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
    QString errorText;
    QVERIFY2(session.loadContainer(session.getPageKey(0), nullptr, &errorText), qPrintable(errorText));

    const QJsonArray objects = PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions()).value(QStringLiteral("objects")).toArray();
    QCOMPARE(objects.size(), 4);

    // ---- The tags: the name of the tag, its identifier, an artifact; they are data of the document ----
    const QString tagKey = QStringLiteral("tag");
    for (const QJsonValueConstRef object : objects)
    {
        QCOMPARE(object.toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("marked_content"));
    }
    QCOMPARE(objects[0].toObject().value(tagKey).toObject(), QJsonObject({ { QStringLiteral("name"), QStringLiteral("P") }, { QStringLiteral("origin"), QStringLiteral("document") }, { QStringLiteral("mcid"), 0 } }));
    QCOMPARE(objects[1].toObject().value(tagKey).toObject(), QJsonObject({ { QStringLiteral("name"), QStringLiteral("Span") }, { QStringLiteral("origin"), QStringLiteral("document") }, { QStringLiteral("mcid"), 1 } }));
    QCOMPARE(objects[2].toObject().value(tagKey).toObject(), QJsonObject({ { QStringLiteral("name"), QStringLiteral("Artifact") }, { QStringLiteral("origin"), QStringLiteral("document") }, { QStringLiteral("artifact"), true } }));
    QCOMPARE(objects[3].toObject().value(tagKey).toObject(), QJsonObject({ { QStringLiteral("name"), QStringLiteral("Figure") }, { QStringLiteral("origin"), QStringLiteral("document") }, { QStringLiteral("mcid"), 3 } }));

    // ---- The text of the document carries "origin": "document" (zadani_overeni.md 7.1): the
    //      text object and its show operator ----
    const QJsonObject expectedText({ { QStringLiteral("value"), QStringLiteral("First") }, { QStringLiteral("origin"), QStringLiteral("document") } });
    const QJsonObject textObject = listedObject(objects, { 0, 0 });
    QCOMPARE(textObject.value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
    QCOMPARE(textObject.value(QStringLiteral("text")).toObject(), expectedText);

    // BT /F1 12 Tf 20 150 Td (First) Tj ET: a state run (Tf, Td), then the show operator
    const QJsonArray parts = textObject.value(QStringLiteral("children")).toArray();
    QCOMPARE(parts.size(), 2);
    QCOMPARE(parts[0].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("state"));
    QVERIFY(!parts[0].toObject().contains(QStringLiteral("text")));
    QCOMPARE(parts[1].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("text_run"));
    QCOMPARE(parts[1].toObject().value(QStringLiteral("text")).toObject(), expectedText);

    QCOMPARE(listedObject(objects, { 1, 0 }).value(QStringLiteral("text")).toObject().value(QStringLiteral("value")).toString(), QStringLiteral("Second"));

    // A path has no text
    QCOMPARE(listedObject(objects, { 3, 0 }).value(QStringLiteral("kind")).toString(), QStringLiteral("path"));
    QVERIFY(!listedObject(objects, { 3, 0 }).contains(QStringLiteral("text")));
}

void DocumentToolContentTest::listTextLimit()
{
    // Three text objects of ten characters. Each is three nodes - the text object, the state run
    // (Tf, Td) and the show operator - and its text is listed twice, by the text object and by
    // its run:
    //   p1:2 text (10)  p1:3 state  p1:4 text_run (10)     p1:5, p1:6, p1:7     p1:8, p1:9, p1:10
    QCOMPARE(PDFContentJson::MAXIMUM_LIST_TEXT_LENGTH, qint64(1048576));
    QCOMPARE(PDFContentJson::ListOptions().maximumTextLength, qint64(1048576));

    const QByteArray content("BT /F1 12 Tf 20 150 Td (AAAAAAAAAA) Tj ET\nBT /F1 12 Tf 20 130 Td (BBBBBBBBBB) Tj ET\nBT /F1 12 Tf 20 110 Td (CCCCCCCCCC) Tj ET");
    const PDFDocument document = Documents::createDocument(content, [](PDFDocumentBuilder* builder)
    {
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", Documents::addStandardFontObject(builder, "Helvetica") } }));
        return resources;
    });
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
    QString errorText;
    QVERIFY2(session.loadContainer(session.getPageKey(0), nullptr, &errorText), qPrintable(errorText));

    const auto list = [&session](PDFInteger offset, qint64 maximumTextLength)
    {
        PDFContentJson::ListOptions options;
        options.offset = offset;
        options.maximumTextLength = maximumTextLength;
        return PDFContentJson::writePage(&session, 0, options);
    };
    const auto ids = [](const QJsonObject& page) { return listedIds(page.value(QStringLiteral("objects")).toArray()); };
    const auto id = [](int node) { return QStringLiteral("p1:%1").arg(node); };

    // ---- No limit in the way: nine objects, sixty characters ----
    QJsonObject page = list(0, 60);
    QCOMPARE(ids(page), QStringList({ id(2), id(3), id(4), id(5), id(6), id(7), id(8), id(9), id(10) }));
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));
    QCOMPARE(listedObject(page.value(QStringLiteral("objects")).toArray(), { 1, 1 }).value(QStringLiteral("text")).toObject().value(QStringLiteral("value")).toString(), QStringLiteral("BBBBBBBBBB"));

    // ---- 25 characters: the first text object (10) and its run (10) fit, the second text
    //      object (10 more) does not - the window ends before it and the next call goes on there ----
    page = list(0, 25);
    QCOMPARE(ids(page), QStringList({ id(2), id(3), id(4) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 3);

    page = list(3, 25);
    QCOMPARE(ids(page), QStringList({ id(5), id(6), id(7) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 6);

    page = list(6, 25);
    QCOMPARE(ids(page), QStringList({ id(8), id(9), id(10) }));
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));

    // ---- Exactly the texts of two text objects fit 40 characters; with 39 the run of the
    //      second one is left for the next call, where its text object is only its place ----
    page = list(0, 40);
    QCOMPARE(ids(page), QStringList({ id(2), id(3), id(4), id(5), id(6), id(7) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 6);

    page = list(0, 39);
    QCOMPARE(ids(page), QStringList({ id(2), id(3), id(4), id(5), id(6) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 5);

    page = list(5, 39);
    QCOMPARE(ids(page), QStringList({ id(7), id(8), id(9), id(10) }));
    QVERIFY(page.value(QStringLiteral("objects")).toArray()[0].toObject().value(QStringLiteral("partial")).toBool());
    QVERIFY(!page.value(QStringLiteral("objects")).toArray()[0].toObject().contains(QStringLiteral("text")));
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));

    // ---- The first object of a call is always listed, so every call gets on: with no
    //      character left the text object is listed, and its state run (it has no text); its
    //      run is the first object of the next call ----
    page = list(0, 0);
    QCOMPARE(ids(page), QStringList({ id(2), id(3) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 2);

    page = list(2, 0);
    QCOMPARE(ids(page), QStringList({ id(4) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 3);

    // A limit out of the range is the nearest one of the range
    QCOMPARE(ids(list(0, -5)), QStringList({ id(2), id(3) }));
    QCOMPARE(ids(list(0, std::numeric_limits<qint64>::max())).size(), 9);

    // ---- The window ends, where the limit stops it - nothing behind that place is listed,
    //      also when it would fit. A text object and a path: the text (the node 2, ten
    //      characters), its state run (3), its run (4, ten more) and the path (5). With 15
    //      characters the run does not fit; the path, which has no text, is not listed in its
    //      place (the next call begins with the run). ----
    const PDFDocument mixed = Documents::createDocument(QByteArray("BT /F1 12 Tf 20 150 Td (AAAAAAAAAA) Tj ET\n0 0 5 5 re f"), [](PDFDocumentBuilder* builder)
    {
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", Documents::addStandardFontObject(builder, "Helvetica") } }));
        return resources;
    });
    PDFContentEditSession mixedSession(PDFDocumentPointer(new PDFDocument(mixed)), PDFCMSPointer(), nullptr);
    QVERIFY2(mixedSession.loadContainer(mixedSession.getPageKey(0), nullptr, &errorText), qPrintable(errorText));
    PDFContentJson::ListOptions mixedOptions;
    mixedOptions.maximumTextLength = 15;
    page = PDFContentJson::writePage(&mixedSession, 0, mixedOptions);
    QCOMPARE(ids(page), QStringList({ id(2), id(3) }));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInt(), 2);

    mixedOptions.offset = 2;
    page = PDFContentJson::writePage(&mixedSession, 0, mixedOptions);
    QCOMPARE(ids(page), QStringList({ id(4), id(5) }));
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));
}

void DocumentToolContentTest::listForms()
{
    // The page 1 "/Fm1 Do /Fm2 Do"; /Fm1 "/GF gs 0 1 0 rg 5 5 20 10 re f" is the state run 2 and
    // the path 3 of its container
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);
    PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
    QString errorText;
    QVERIFY2(session.loadContainer(session.getPageKey(0), nullptr, &errorText), qPrintable(errorText));

    // ---- No form is loaded: the forms are listed without their objects ----
    QJsonArray objects = PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions()).value(QStringLiteral("objects")).toArray();
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:3") }));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("form"));
    QVERIFY(!objects[0].toObject().contains(QStringLiteral("children")));
    QVERIFY(!objects[0].toObject().contains(QStringLiteral("childCount")));

    // ---- /Fm1 is loaded: its objects are listed below the node, which draws it, with the
    //      identifiers of the form ("p1:2/3" is the node 3 of the form drawn by the node 2) ----
    QVERIFY2(session.loadContainer(PDFContentContainerKey::createForm(form1), nullptr, &errorText), qPrintable(errorText));
    objects = PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions()).value(QStringLiteral("objects")).toArray();
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:2/2"), QStringLiteral("p1:2/3"), QStringLiteral("p1:3") }));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("childCount")).toInt(), 2);
    QCOMPARE(listedObject(objects, { 0, 0 }).value(QStringLiteral("kind")).toString(), QStringLiteral("state"));
    QCOMPARE(listedObject(objects, { 0, 1 }).value(QStringLiteral("kind")).toString(), QStringLiteral("path"));

    // The rectangle of an object of a form is in the space of the page (zadani_overeni.md 7.1):
    // "5 5 20 10 re" of the form with /Matrix [2 0 0 2 10 20], drawn with the identity as the
    // current transformation matrix, is x 2 * 5 + 10 = 20 ... 2 * 25 + 10 = 60 and y 2 * 5 + 20 =
    // 30 ... 2 * 15 + 20 = 50. The use says, how the space of the form lies in the page.
    QCOMPARE(listedObject(objects, { 0, 1 }).value(QStringLiteral("rect")).toObject(), PDFContentJson::writeRect(QRectF(20, 30, 40, 20)));
    QVERIFY(!listedObject(objects, { 0, 1 }).contains(QStringLiteral("space")));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("matrix")).toArray(), QJsonArray({ 2, 0, 0, 2, 10, 20 }));
    QVERIFY(!objects[1].toObject().contains(QStringLiteral("children")));

    // The identifiers resolve to the nodes of the container of the form
    const PDFContentNodeReference reference = PDFContentJson::readNodeId(QStringLiteral("p1:2/3"), &session, &errorText);
    QVERIFY(reference == (PDFContentNodeReference{ PDFContentContainerKey::createForm(form1), PDFContentNodeId{ 3 } }));

    // The depth 1 lists the forms only
    PDFContentJson::ListOptions options;
    options.depth = 1;
    objects = PDFContentJson::writePage(&session, 0, options).value(QStringLiteral("objects")).toArray();
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:3") }));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("childCount")).toInt(), 2);
}

void DocumentToolContentTest::listFormSpace()
{
    // zadani_overeni.md 7.1: the coordinates of a listing are of the unrotated default user
    // space of the page. An object of a form is listed below one use of the form, so its box of
    // the form space is mapped by the matrix of that use. Documents::createFormDocument:
    //   "q /Fm1 Do Q   q 1 0 0 1 100 0 cm /Fm1 Do Q   /Fm2 Do"
    //   /Fm1  /Matrix [2 0 0 2 10 20], a rectangle "5 5 20 10 re": on the page x 20 ... 60, y 30 ... 50
    //         and, moved by 100, x 120 ... 160, y 30 ... 50
    //   /Fm2  no /Matrix, a square "0 0 10 10 re": on the page x 0 ... 10, y 0 ... 10
    const auto listUses = [](const PDFDocument& document, QList<QJsonObject>* uses)
    {
        PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
        QVERIFY(loadPageWithForms(&session, 0));
        PDFContentJson::ListOptions options;
        options.depth = 8;
        *uses = listedOfKind(PDFContentJson::writePage(&session, 0, options).value(QStringLiteral("objects")).toArray(), QStringLiteral("form"));
    };
    const auto pathOf = [](const QJsonObject& use)
    {
        const QList<QJsonObject> paths = listedOfKind(use.value(QStringLiteral("children")).toArray(), QStringLiteral("path"));
        return paths.isEmpty() ? QJsonObject() : paths.front();
    };
    const QString rectKey = QStringLiteral("rect");
    const QString matrixKey = QStringLiteral("matrix");

    QList<QJsonObject> uses;
    listUses(Documents::createFormDocument(), &uses);
    QCOMPARE(uses.size(), 3);

    // The use says, how the space of its form lies in the page: /Matrix of the form, then the
    // current transformation matrix of the use (ISO 32000-2, 8.10.1)
    QCOMPARE(uses[0].value(matrixKey).toArray(), QJsonArray({ 2, 0, 0, 2, 10, 20 }));
    QCOMPARE(uses[1].value(matrixKey).toArray(), QJsonArray({ 2, 0, 0, 2, 110, 20 }));
    QCOMPARE(uses[2].value(matrixKey).toArray(), QJsonArray({ 1, 0, 0, 1, 0, 0 }));

    // The same node of the same form has another rectangle below each of its uses
    QCOMPARE(pathOf(uses[0]).value(QStringLiteral("id")).toString().section(QLatin1Char('/'), 1), pathOf(uses[1]).value(QStringLiteral("id")).toString().section(QLatin1Char('/'), 1));
    QCOMPARE(pathOf(uses[0]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(20, 30, 40, 20)));
    QCOMPARE(pathOf(uses[1]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(120, 30, 40, 20)));
    QCOMPARE(pathOf(uses[2]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(0, 0, 10, 10)));
    for (const QJsonObject& use : std::as_const(uses))
    {
        QVERIFY(!pathOf(use).contains(QStringLiteral("space")));
        QVERIFY(!pathOf(use).contains(matrixKey));
    }

    // ---- A form in a form, and a rotated use:
    //   page  "q 1 0 0 1 0 7 cm /Fo Do Q   q 0 1 -1 0 50 0 cm /Fi Do Q"
    //   /Fo   /Matrix [1 0 0 1 5 0], "q 2 0 0 2 0 0 cm /Fi Do Q"
    //   /Fi   "1 1 2 2 re f"
    // The square x 1 ... 3, y 1 ... 3 of /Fi inside of /Fo: doubled (2 ... 6), moved by /Matrix of
    // /Fo (x 7 ... 11) and by the page (y 9 ... 13); its use maps (x, y) to (2 x + 5, 2 y + 7).
    // Drawn by the page itself, rotated by 90 degrees: (x, y) is (50 - y, x), so the square is
    // x 47 ... 49, y 1 ... 3.
    PDFDocumentBuilder builder;
    const PDFObject inner = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                   { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                   { "Resources", Documents::createDictionaryObject({ }) } }, "1 1 2 2 re f");
    const PDFObject outer = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                   { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) },
                                                                   { "Matrix", Documents::createIntegerArrayObject({ 1, 0, 0, 1, 5, 0 }) },
                                                                   { "Resources", formResources({ { "Fi", inner } }) } }, "q 2 0 0 2 0 0 cm /Fi Do Q");
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", formResources({ { "Fo", outer }, { "Fi", inner } }) },
                                                              { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "q 1 0 0 1 0 7 cm /Fo Do Q q 0 1 -1 0 50 0 cm /Fi Do Q") } }));

    listUses(builder.build(), &uses);
    QCOMPARE(uses.size(), 3);
    QCOMPARE(uses[0].value(matrixKey).toArray(), QJsonArray({ 1, 0, 0, 1, 5, 7 }));
    QCOMPARE(uses[1].value(matrixKey).toArray(), QJsonArray({ 2, 0, 0, 2, 5, 7 }));
    QCOMPARE(uses[2].value(matrixKey).toArray(), QJsonArray({ 0, 1, -1, 0, 50, 0 }));
    QCOMPARE(pathOf(uses[1]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(7, 9, 4, 4)));
    QCOMPARE(pathOf(uses[2]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(47, 1, 2, 2)));

    // ---- A use, whose state is not known (a node made by a command, which has given it none):
    //      its matrix cannot be computed. The listing does not guess: the use has no "matrix"
    //      and the objects of its form say, that their rectangles are of the space of the form
    //      ("5 5 20 10 re" of /Fm1 as it is written). The other use is listed as ever. ----
    {
        PDFObjectReference form1;
        PDFObjectReference form2;
        const PDFDocument document = createFormsDocument(&form1, &form2);
        PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
        QVERIFY(loadPageWithForms(&session, 0));

        const PDFContentContainerPointer pageContainer = session.getContainer(session.getPageKey(0));
        const PDFContentNodePointer use = nodesOfKind(*pageContainer, PDFContentNodeKind::FormUse)[0];
        PDFContentContainerEditor editor(pageContainer);
        PDFContentNode::Data data = use->getData();
        data.state = PDFContentResolvedStatePointer();
        QVERIFY(editor.setNodeData(use->getId(), std::move(data)));
        QString errorText;
        QVERIFY2(session.execute(std::make_shared<ContentToolTestReplaceCommand>(editor.build()), &errorText), qPrintable(errorText));

        uses = listedOfKind(PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions()).value(QStringLiteral("objects")).toArray(), QStringLiteral("form"));
        QCOMPARE(uses.size(), 2);
        QVERIFY(!uses[0].contains(matrixKey));
        QCOMPARE(pathOf(uses[0]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(5, 5, 20, 10)));
        QCOMPARE(pathOf(uses[0]).value(QStringLiteral("space")).toString(), QStringLiteral("form"));
        QCOMPARE(uses[1].value(matrixKey).toArray(), QJsonArray({ 1, 0, 0, 1, 0, 0 }));
        QCOMPARE(pathOf(uses[1]).value(rectKey).toObject(), PDFContentJson::writeRect(QRectF(0, 0, 10, 10)));
        QVERIFY(!pathOf(uses[1]).contains(QStringLiteral("space")));
    }
}

void DocumentToolContentTest::pageRevision()
{
    // The revision of a page is what the mode list gives and a script gives back: it must change
    // with everything an identifier of the page can name. "p1:2/3" names a node of the form
    // drawn by the node 2 of the page - the content of the form is in no fingerprint of the page
    // (a resource, which is a reference, is hashed by its number).
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);

    // The same document with other content in the stream of /Fm1 (as after a run, which has
    // written the form at its reference), and with other content in /Fm2
    const auto withFormContent = [&document](PDFObjectReference form, const QByteArray& content)
    {
        PDFDocumentBuilder builder(&document);
        const PDFObject stream = document.getObjectByReference(form);
        PDFDictionaryBuilder dictionary(*stream.getStream()->getDictionary());
        dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
        builder.setObject(form, PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(content))));
        return builder.build();
    };
    const PDFDocument otherForm1 = withFormContent(form1, "/GF gs 0 1 0 rg 5 5 20 11 re f");
    const PDFDocument otherForm2 = withFormContent(form2, "/GP gs 1 0 0 rg 0 0 10 11 re f");

    // Nothing of the pages themselves differs: their fingerprints are the same
    for (const PDFDocument* other : { &otherForm1, &otherForm2 })
    {
        for (const PDFInteger pageIndex : { 0, 1 })
        {
            QCOMPARE(PDFContentTreeBuilder::computeFingerprint(other, pageKey(*other, pageIndex)), PDFContentTreeBuilder::computeFingerprint(&document, pageKey(document, pageIndex)));
        }
    }

    // The revision is 32 bytes, and the same for the same document
    const QString revision1 = revisionOf(document, 0);
    const QString revision2 = revisionOf(document, 1);
    QCOMPARE(revision1.size(), 64);
    QCOMPARE(revisionOf(document, 0), revision1);
    QCOMPARE(revisionOf(PDFDocument(document), 0), revision1);
    QVERIFY(revision1 != revision2);

    // /Fm1 is drawn by both pages: both revisions change with it. /Fm2 is drawn by the page 1
    // only: the revision of the page 2 stays.
    QVERIFY(revisionOf(otherForm1, 0) != revision1);
    QVERIFY(revisionOf(otherForm1, 1) != revision2);
    QVERIFY(revisionOf(otherForm2, 0) != revision1);
    QCOMPARE(revisionOf(otherForm2, 1), revision2);

    // ---- The revision covers the forms, which are loaded (only their nodes have identifiers):
    //      with the page alone it is another one than with its forms, and it does not see the
    //      change of a form ----
    {
        PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(document)), PDFCMSPointer(), nullptr);
        QVERIFY(PDFContentJson::getPageRevision(&session, 0).isEmpty());
        QVERIFY(PDFContentJson::getPageRevision(nullptr, 0).isEmpty());

        QString errorText;
        QVERIFY(session.loadContainer(session.getPageKey(0), nullptr, &errorText));
        const QByteArray pageOnly = PDFContentJson::getPageRevision(&session, 0);
        QCOMPARE(pageOnly.size(), qsizetype(32));
        QVERIFY(QString::fromLatin1(pageOnly.toHex()) != revision1);
        QVERIFY(PDFContentJson::getPageRevision(&session, 1).isEmpty());
        QVERIFY(PDFContentJson::getPageRevision(&session, 7).isEmpty());

        PDFContentEditSession otherSession(PDFDocumentPointer(new PDFDocument(otherForm1)), PDFCMSPointer(), nullptr);
        QVERIFY(otherSession.loadContainer(otherSession.getPageKey(0), nullptr, &errorText));
        QCOMPARE(PDFContentJson::getPageRevision(&otherSession, 0), pageOnly);

        // The listing gives the same revision
        QVERIFY(loadPageWithForms(&session, 0));
        QCOMPARE(PDFContentJson::writePage(&session, 0, PDFContentJson::ListOptions()).value(QStringLiteral("revision")).toString(), revision1);
    }

    // ---- Forms in forms: the page draws /Fm1, /Fm1 draws /Fm2, /Fm2 draws /Fm3. A change of the
    //      innermost form changes the revision of the page. A form drawn a thousand times (a
    //      node shared by many paths) is hashed once: the revision is there at once. ----
    {
        const PDFDocument nested = createNestedFormsDocument();
        PDFContentEditSession session(PDFDocumentPointer(new PDFDocument(nested)), PDFCMSPointer(), nullptr);
        QVERIFY(loadPageWithForms(&session, 0));
        const PDFContentContainerPointer page = session.getContainer(session.getPageKey(0));
        const PDFObjectReference nestedForm1 = nodesOfKind(*page, PDFContentNodeKind::FormUse)[0]->getData().xobject;
        const PDFObjectReference nestedForm2 = nodesOfKind(*session.getContainer(PDFContentContainerKey::createForm(nestedForm1)), PDFContentNodeKind::FormUse)[0]->getData().xobject;
        const PDFObjectReference nestedForm3 = nodesOfKind(*session.getContainer(PDFContentContainerKey::createForm(nestedForm2)), PDFContentNodeKind::FormUse)[0]->getData().xobject;
        QVERIFY(session.getContainer(PDFContentContainerKey::createForm(nestedForm3)) != nullptr);

        PDFDocumentBuilder builder(&nested);
        PDFDictionaryBuilder dictionary(*nested.getObjectByReference(nestedForm3).getStream()->getDictionary());
        dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(12));
        builder.setObject(nestedForm3, PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray("0 0 2 2 re f"))));
        const PDFDocument otherNested = builder.build();

        QElapsedTimer timer;
        timer.start();
        const QString nestedRevision = revisionOf(nested, 0);
        QCOMPARE(nestedRevision.size(), 64);
        QVERIFY(revisionOf(otherNested, 0) != nestedRevision);
        const qint64 limit = qint64(20000 * qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR", QStringLiteral("1")).toDouble()));
        QVERIFY2(timer.elapsed() < limit, qPrintable(QString::number(timer.elapsed())));
    }

    // ---- A form, which draws itself (a cycle): the walk ends ----
    QCOMPARE(revisionOf(createSelfDrawingFormDocument(), 0).size(), 64);

    // ---- The tiling pattern an object of the page is painted with: its cell is a content of
    //      its own, which the fingerprint of the page does not cover either ----
    {
        const QByteArray cells[2] = { QByteArray("1 0 0 rg 0 0 10 10 re f"), QByteArray("0 0 1 rg 0 0 10 10 re f") };
        PDFDocument patterns[2];
        for (int i = 0; i < 2; ++i)
        {
            PDFDocumentBuilder builder;
            const PDFObject pattern = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("Pattern") }, { "PatternType", PDFObject::createInteger(1) },
                                                                             { "PaintType", PDFObject::createInteger(1) }, { "TilingType", PDFObject::createInteger(1) },
                                                                             { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                             { "XStep", PDFObject::createInteger(20) }, { "YStep", PDFObject::createInteger(20) },
                                                                             { "Resources", Documents::createDictionaryObject({ }) } }, cells[i]);
            const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "Pattern", Documents::createDictionaryObject({ { "P1", pattern } }) } }) },
                                                                      { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Pattern cs /P1 scn 25 25 40 40 re f") } }));
            patterns[i] = builder.build();
        }

        QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&patterns[0], pageKey(patterns[0], 0)), PDFContentTreeBuilder::computeFingerprint(&patterns[1], pageKey(patterns[1], 0)));
        QCOMPARE(revisionOf(patterns[0], 0).size(), 64);
        QVERIFY(revisionOf(patterns[0], 0) != revisionOf(patterns[1], 0));
    }
}

void DocumentToolContentTest::toolList()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH, TWO_PATHS });
    const PDFDocumentToolContext context = contextOf(document);

    // ---- One page: the listing of the page as the data of the result; nothing is changed ----
    PDFDocumentToolResult result = tool.run(context, listParameters(QStringLiteral("2")), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));
    QVERIFY(result.documents.empty());
    QVERIFY(result.outputs.empty());
    QVERIFY(result.writtenFiles.isEmpty());
    QVERIFY(result.facts.empty());
    QVERIFY(*context.documents[0].document == document);

    QCOMPARE(result.data.keys(), QStringList({ QStringLiteral("pages") }));
    QJsonArray pages = result.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 1);
    QCOMPARE(pages[0].toObject().value(QStringLiteral("page")).toInt(), 2);
    QCOMPARE(pages[0].toObject().value(QStringLiteral("revision")).toString(), revisionOf(document, 1));
    QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p2:2"), QStringLiteral("p2:3") }));

    // ---- All pages (the default); each page has its own revision, equal contents have equal ones ----
    result = tool.run(context, { { QStringLiteral("mode"), QStringLiteral("list") } }, nullptr, nullptr);
    QVERIFY(result.isSuccess());
    pages = result.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 3);
    for (int i = 0; i < 3; ++i)
    {
        QCOMPARE(pages[i].toObject().value(QStringLiteral("page")).toInt(), i + 1);
        QCOMPARE(pages[i].toObject().value(QStringLiteral("revision")).toString(), revisionOf(document, i));
    }
    QVERIFY(pages[0].toObject().value(QStringLiteral("revision")) != pages[1].toObject().value(QStringLiteral("revision")));
    QCOMPARE(listedIds(pages[2].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p3:2"), QStringLiteral("p3:3"), QStringLiteral("p3:4"), QStringLiteral("p3:5") }));

    // ---- The limit of the objects is the limit of the call (zadani_overeni.md 6.2: at most
    //      2 000 objects a call): 4 + 2 + 4 objects with the limit 5 - the page 1 whole, one
    //      object of the page 2 and its nextOffset, the page 3 not listed ----
    result = tool.run(context, listParameters(QStringLiteral("1-3"), -1, -1, 5), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    pages = result.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 2);
    QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()).size(), 4);
    QVERIFY(!pages[0].toObject().contains(QStringLiteral("nextOffset")));
    QCOMPARE(listedIds(pages[1].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p2:2") }));
    QCOMPARE(pages[1].toObject().value(QStringLiteral("nextOffset")).toInt(), 1);
    QCOMPARE(result.data.value(QStringLiteral("pagesNotListed")).toArray(), QJsonArray({ 3 }));

    // The next call: the rest of the page 2 from its offset, then the page 3 from its beginning
    result = tool.run(context, listParameters(QStringLiteral("2-3"), -1, 1, 5), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    pages = result.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 2);
    QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p2:3") }));
    QCOMPARE(listedIds(pages[1].toObject().value(QStringLiteral("objects")).toArray()).size(), 4);
    QVERIFY(!result.data.contains(QStringLiteral("pagesNotListed")));

    // The limit ends exactly with a page: the next page is not listed, though no object is cut
    result = tool.run(context, listParameters(QStringLiteral("1-2"), -1, -1, 4), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.data.value(QStringLiteral("pages")).toArray().size(), 1);
    QCOMPARE(result.data.value(QStringLiteral("pagesNotListed")).toArray(), QJsonArray({ 2 }));

    // ---- A parent, which is only the place of its objects, is no object of the limit. The page
    //      "q 1 0 0 rg 10 10 50 50 re f Q 0 0 5 5 re f" has the group 2 with the nodes 3 and 4, and
    //      the path 5; from the offset 1 the group is partial and three objects are listed. With
    //      the limit 4 one object is left for the next page. ----
    const PDFDocument groups = createPagesDocument({ QByteArray("q 1 0 0 rg 10 10 50 50 re f Q 0 0 5 5 re f"), ONE_PATH });
    result = tool.run(contextOf(groups), listParameters(QStringLiteral("1-2"), 2, 1, 4), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    pages = result.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 2);
    QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p1:3"), QStringLiteral("p1:4"), QStringLiteral("p1:5") }));
    QVERIFY(pages[0].toObject().value(QStringLiteral("objects")).toArray()[0].toObject().value(QStringLiteral("partial")).toBool());
    QCOMPARE(listedIds(pages[1].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p2:2") }));
    QCOMPARE(pages[1].toObject().value(QStringLiteral("nextOffset")).toInt(), 1);

    // ---- The parameters of the listing are checked: a page out of the document, a depth of 17
    //      levels, 2 001 objects ----
    QVERIFY(tool.run(context, listParameters(QStringLiteral("4")), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(tool.run(context, listParameters(QStringLiteral("1"), 17), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(tool.run(context, listParameters(QStringLiteral("1"), 0), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(tool.run(context, listParameters(QStringLiteral("1"), -1, -1, 2001), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(tool.run(context, listParameters(QStringLiteral("1"), 16, -1, 2000), nullptr, nullptr).isSuccess());

    // A canceled listing is stopped
    const ContentToolTestControl control(0);
    QVERIFY(tool.execute(context, listParameters(QStringLiteral("1")), &control, nullptr).status == PDFDocumentToolResultStatus::Stopped);

    // A page, which is read-only, is listed and says so
    const PDFDocument broken = Documents::createBrokenContentsDocument("1 0 0 rg", "0 0 5 5 re f", &Documents::createNoResources);
    result = tool.run(contextOf(broken), listParameters(QStringLiteral("1")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("pages")).toArray()[0].toObject().value(QStringLiteral("readOnly")), QJsonValue(true));
}

void DocumentToolContentTest::toolListTextLimit()
{
    // One call of the mode list returns at most 1 048 576 characters of the texts of the
    // document (PDFContentJson::MAXIMUM_LIST_TEXT_LENGTH). A page of sequences with a
    // replacement text of 60 000 characters each (two objects a sequence: the sequence and its
    // path): 17 of them are 1 020 000 characters, the 18th does not fit the 28 576 left.
    Tool tool;
    const auto createDocument = [](int sequenceCount, int secondSequenceCount = 0)
    {
        PDFDocumentBuilder builder;
        // With two pages of sequences those of the first one are inside of groups
        for (const QByteArray& content : { actualTextContent(sequenceCount, secondSequenceCount > 0), secondSequenceCount > 0 ? actualTextContent(secondSequenceCount) : QByteArray(ONE_PATH) })
        {
            const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
            builder.mergeTo(page, Documents::createDictionaryObject({ { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), content) } }));
        }
        return builder.build();
    };
    const QString notListedKey = QStringLiteral("pagesNotListed");
    QCOMPARE(QByteArray(Tool::DATA_PAGES_NOT_LISTED), QByteArray("pagesNotListed"));

    {
        const PDFDocumentToolContext context = contextOf(createDocument(20));
        PDFDocumentToolResult result = tool.run(context, listParameters(QStringLiteral("1-2")), nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QJsonArray pages = result.data.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 1);

        // The objects of 17 sequences: the nodes 2 ... 35 of the page
        const QJsonArray objects = pages[0].toObject().value(QStringLiteral("objects")).toArray();
        const QStringList ids = listedIds(objects);
        QCOMPARE(ids.size(), 34);
        QCOMPARE(ids.front(), QStringLiteral("p1:2"));
        QCOMPARE(ids.back(), QStringLiteral("p1:35"));
        QCOMPARE(objects[0].toObject().value(QStringLiteral("tag")).toObject().value(QStringLiteral("actualText")).toString().size(), 60000);
        QCOMPARE(pages[0].toObject().value(QStringLiteral("nextOffset")).toInt(), 34);

        // The page 2 is not started in this call
        QCOMPARE(result.data.value(notListedKey).toArray(), QJsonArray({ 2 }));

        // The limit of a call is the largest one a caller of the listing can ask for
        PDFContentEditSession session(context.documents.front().document, PDFCMSPointer(), nullptr);
        QString errorText;
        QVERIFY2(session.loadContainer(session.getPageKey(0), nullptr, &errorText), qPrintable(errorText));
        PDFContentJson::ListOptions options;
        options.maximumTextLength = std::numeric_limits<qint64>::max();
        QCOMPARE(listedIds(PDFContentJson::writePage(&session, 0, options).value(QStringLiteral("objects")).toArray()).size(), 34);

        // The next call: the three sequences left, then the page 2
        result = tool.run(context, listParameters(QStringLiteral("1-2"), -1, 34), nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        pages = result.data.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 2);
        QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()).size(), 6);
        QVERIFY(!pages[0].toObject().contains(QStringLiteral("nextOffset")));
        QCOMPARE(listedIds(pages[1].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p2:2"), QStringLiteral("p2:3") }));
        QVERIFY(!result.data.contains(notListedKey));
    }

    // ---- A page is started only when the longest texts of one object still fit (2 x 65 536
    //      characters: its text and its replacement text), so the limit never cuts its first
    //      object off. 16 sequences leave 88 576 characters: the page 2 waits for the next call,
    //      though the page 1 is listed whole. 15 sequences leave 148 576: the page 2 is listed. ----
    {
        const PDFDocumentToolResult result = tool.run(contextOf(createDocument(16)), listParameters(QStringLiteral("1-2")), nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const QJsonArray pages = result.data.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 1);
        QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()).size(), 32);
        QVERIFY(!pages[0].toObject().contains(QStringLiteral("nextOffset")));
        QCOMPARE(result.data.value(notListedKey).toArray(), QJsonArray({ 2 }));
    }

    {
        const PDFDocumentToolResult result = tool.run(contextOf(createDocument(15)), listParameters(QStringLiteral("1-2")), nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(result.data.value(QStringLiteral("pages")).toArray().size(), 2);
        QVERIFY(!result.data.contains(notListedKey));
    }

    // ---- The limit is the one of the call: the pages share it. Ten sequences on each of two
    //      pages (those of the page 1 inside of groups: three objects a sequence, the texts are
    //      not at the top of the listing): the page 1 takes 600 000 characters, the 448 576 left
    //      are seven sequences of the page 2 (420 000) - the eighth one waits for the next call ----
    {
        const PDFDocumentToolResult result = tool.run(contextOf(createDocument(10, 10)), listParameters(QStringLiteral("1-2")), nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const QJsonArray pages = result.data.value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 2);
        QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()).size(), 30);
        QVERIFY(!pages[0].toObject().contains(QStringLiteral("nextOffset")));
        QCOMPARE(listedIds(pages[1].toObject().value(QStringLiteral("objects")).toArray()).size(), 14);
        QCOMPARE(pages[1].toObject().value(QStringLiteral("nextOffset")).toInt(), 14);
    }
}

void DocumentToolContentTest::toolListForms()
{
    // The tool loads the forms of a listed page: the page 1 "/Fm1 Do /Fm2 Do" with the objects of
    // both forms (/Fm2 "/GP gs 1 0 0 rg 0 0 10 10 re f": the state run 2 and the path 3)
    Tool tool;
    PDFObjectReference form1;
    PDFObjectReference form2;
    const PDFDocument document = createFormsDocument(&form1, &form2);

    const PDFDocumentToolResult result = tool.run(contextOf(document), listParameters(QStringLiteral("1-2")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QJsonArray pages = result.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(pages.size(), 2);
    QCOMPARE(listedIds(pages[0].toObject().value(QStringLiteral("objects")).toArray()),
             QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:2/2"), QStringLiteral("p1:2/3"), QStringLiteral("p1:3"), QStringLiteral("p1:3/2"), QStringLiteral("p1:3/3") }));
    QCOMPARE(listedIds(pages[1].toObject().value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p2:2"), QStringLiteral("p2:2/2"), QStringLiteral("p2:2/3") }));
}

void DocumentToolContentTest::toolListHostileForms()
{
    Tool tool;

    // ---- A form drawn by a thousand nodes (a node shared by many paths), three levels deep.
    //      With four levels listed the page has
    //        a use of /Fm3: itself and its path                    2 objects
    //        a use of /Fm2: itself and a thousand uses of /Fm3     1 + 1000 x 2 = 2 001
    //        a use of /Fm1: itself and a thousand uses of /Fm2     1 + 1000 x 2 001 = 2 001 001
    //        the page: a thousand uses of /Fm1                     2 001 001 000 objects
    //      A window anywhere in them is listed without walking the objects before it. The uses
    //      are the nodes 2 ... 1001 of their containers, the path of /Fm3 is its node 2. ----
    const PDFDocument document = createNestedFormsDocument();
    const PDFDocumentToolContext context = contextOf(document);
    const qint64 useSize = 2001001;
    const qint64 total = 1000 * useSize;

    QElapsedTimer timer;
    timer.start();

    const auto list = [&tool, &context](qint64 offset, int maximumObjects)
    {
        return tool.run(context, listParameters(QStringLiteral("1"), 4, offset, maximumObjects), nullptr, nullptr).data.value(QStringLiteral("pages")).toArray()[0].toObject();
    };

    // The beginning: three objects, each inside of the one before
    QJsonObject page = list(0, 3);
    QJsonArray objects = page.value(QStringLiteral("objects")).toArray();
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:2/2"), QStringLiteral("p1:2/2/2") }));
    QCOMPARE(objects[0].toObject().value(QStringLiteral("childCount")).toInt(), 1000);
    QCOMPARE(listedObject(objects, { 0, 0, 0 }).value(QStringLiteral("childCount")).toInt(), 1);
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInteger(), qint64(3));

    // The next two: the path of the first use of /Fm3 and the second use of /Fm3; their parents
    // are only the places of them
    page = list(3, 2);
    objects = page.value(QStringLiteral("objects")).toArray();
    QCOMPARE(listedIds(objects), QStringList({ QStringLiteral("p1:2/2/2/2"), QStringLiteral("p1:2/2/3") }));
    QVERIFY(objects[0].toObject().value(QStringLiteral("partial")).toBool());
    QVERIFY(listedObject(objects, { 0, 0 }).value(QStringLiteral("partial")).toBool());
    QVERIFY(listedObject(objects, { 0, 0, 0 }).value(QStringLiteral("partial")).toBool());
    QCOMPARE(listedObject(objects, { 0, 0, 0, 0 }).value(QStringLiteral("kind")).toString(), QStringLiteral("path"));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInteger(), qint64(5));

    // The last use of /Fm1 of the page begins after 999 whole uses
    page = list(999 * useSize, 1);
    QCOMPARE(listedIds(page.value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p1:1001") }));
    QVERIFY(!page.value(QStringLiteral("objects")).toArray()[0].toObject().contains(QStringLiteral("partial")));
    QCOMPARE(page.value(QStringLiteral("nextOffset")).toInteger(), qint64(999 * useSize + 1));

    // The second use of /Fm2 inside of the 500th use of /Fm1 (the node 501): 499 uses of /Fm1,
    // the use itself and one whole use of /Fm2 before it
    page = list(499 * useSize + 1 + 2001, 1);
    QCOMPARE(listedIds(page.value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p1:501/3") }));

    // The very last object: the path of the last use of /Fm3; nothing follows
    page = list(total - 1, 5);
    QCOMPARE(listedIds(page.value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p1:1001/1001/1001/2") }));
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));

    page = list(total, 5);
    QVERIFY(page.value(QStringLiteral("objects")).toArray().isEmpty());
    QVERIFY(!page.contains(QStringLiteral("nextOffset")));
    QCOMPARE(page.value(QStringLiteral("page")).toInt(), 1);

    // Seven listings over two billion objects: none of them walks the objects one by one
    const qint64 limit = qint64(20000 * qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR", QStringLiteral("1")).toDouble()));
    QVERIFY2(timer.elapsed() < limit, qPrintable(QString::number(timer.elapsed())));

    // ---- A form, which draws itself (a cycle): "/Fm1 Do 0 0 1 1 re f". The listing follows it
    //      down to its depth and ends: the use and, below each use, the next use and the path -
    //      1 + 2 x 15 = 31 objects at 16 levels, 1 + 2 x 3 = 7 at 4 levels ----
    const PDFDocument cyclic = createSelfDrawingFormDocument();
    const PDFDocumentToolContext cyclicContext = contextOf(cyclic);

    PDFDocumentToolResult result = tool.run(cyclicContext, listParameters(QStringLiteral("1"), 16, -1, 2000), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QStringList ids = listedIds(result.data.value(QStringLiteral("pages")).toArray()[0].toObject().value(QStringLiteral("objects")).toArray());
    QCOMPARE(ids.size(), 31);
    QCOMPARE(ids[0], QStringLiteral("p1:2"));
    QCOMPARE(ids[1], QStringLiteral("p1:2/2"));
    QVERIFY(ids.contains(QStringLiteral("p1:2/3")));
    QVERIFY(ids.contains(QStringLiteral("p1:") + QStringList(QList<QString>(16, QStringLiteral("2"))).join(QLatin1Char('/'))));
    QVERIFY(!ids.contains(QStringLiteral("p1:") + QStringList(QList<QString>(17, QStringLiteral("2"))).join(QLatin1Char('/'))));

    // The listing itself never goes deeper than 16 levels, whatever depth it is asked for
    PDFContentEditSession cyclicSession(PDFDocumentPointer(new PDFDocument(cyclic)), PDFCMSPointer(), nullptr);
    QString errorText;
    QVERIFY(cyclicSession.loadContainer(cyclicSession.getPageKey(0), nullptr, &errorText));
    const PDFContentNodePointer use = nodesOfKind(*cyclicSession.getContainer(cyclicSession.getPageKey(0)), PDFContentNodeKind::FormUse)[0];
    QVERIFY2(cyclicSession.loadContainer(PDFContentContainerKey::createForm(use->getData().xobject), nullptr, &errorText), qPrintable(errorText));
    PDFContentJson::ListOptions deepOptions;
    deepOptions.depth = 1000;
    deepOptions.maximumObjects = 2000;
    QCOMPARE(listedIds(PDFContentJson::writePage(&cyclicSession, 0, deepOptions).value(QStringLiteral("objects")).toArray()), ids);

    result = tool.run(cyclicContext, listParameters(QStringLiteral("1"), 4), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    ids = listedIds(result.data.value(QStringLiteral("pages")).toArray()[0].toObject().value(QStringLiteral("objects")).toArray());
    QCOMPARE(ids, QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:2/2"), QStringLiteral("p1:2/2/2"), QStringLiteral("p1:2/2/2/2"), QStringLiteral("p1:2/2/2/3"), QStringLiteral("p1:2/2/3"), QStringLiteral("p1:2/3") }));
}

// ---------------------------------------------------------------------------------------------
//  A script replayed by the tool (zadani_jadro.md 4.6, zadani_overeni.md 7.1)
// ---------------------------------------------------------------------------------------------

void DocumentToolContentTest::toolApplyScript()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);

    // ---- First list, then apply with the revision of the listing: the second path of the page
    //      1 (the node 5) is deleted ----
    const PDFDocumentToolResult listed = tool.run(context, listParameters(QStringLiteral("1")), nullptr, nullptr);
    QVERIFY(listed.isSuccess());
    const QJsonObject listedPage = listed.data.value(QStringLiteral("pages")).toArray()[0].toObject();
    const QString revision = listedPage.value(QStringLiteral("revision")).toString();
    const QString pathId = listedPage.value(QStringLiteral("objects")).toArray()[3].toObject().value(QStringLiteral("id")).toString();
    QCOMPARE(pathId, QStringLiteral("p1:5"));

    const QString script = QStringLiteral("{\"revisions\":{\"1\":\"%1\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"%2\"]}]}").arg(revision, pathId);
    const PDFDocumentToolResult result = tool.run(context, scriptParameters(script), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));

    // The result is the one of the panels: the changed document with the flag Reset and the facts
    QCOMPARE(result.documents.size(), size_t(1));
    QVERIFY(result.documents[0].kind == PDFDocumentToolOutputDocument::Kind::Changed);
    QVERIFY(result.documents[0].flags == PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::Reset));
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1 }));
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 1);
    QCOMPARE(result.data.value(QStringLiteral("objectsAdded")).toInt(-1), 0);
    QCOMPARE(result.data.value(QStringLiteral("objectsEdited")).toInt(-1), 0);

    // The bytes of the page: the first four operations of the document
    const PDFDocument& output = *result.documents[0].document;
    QCOMPARE(pageContent(output, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QCOMPARE(pageContent(output, 1), QByteArray(ONE_PATH));
    QCOMPARE(pageContent(*context.documents[0].document, 0), QByteArray(TWO_PATHS));

    // ---- The revision of the listing is stale for the result: the same script over the new
    //      document is refused (RE-15: the identifiers are sequence numbers of one content) ----
    const PDFDocumentToolResult stale = tool.run(contextOf(output), scriptParameters(script), nullptr, nullptr);
    QVERIFY(stale.status == PDFDocumentToolResultStatus::Failed);
    QCOMPARE(stale.message, QStringLiteral("The document changed in the meantime."));
    QVERIFY(stale.documents.empty());
    QCOMPARE(stale.data.value(QStringLiteral("changedPage")).toInt(), 1);

    // A new listing gives the revision, with which the next script runs
    const QString nextScript = QStringLiteral("{\"revisions\":{\"1\":\"%1\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:3\"]}]}").arg(revisionOf(output, 0));
    const PDFDocumentToolResult next = tool.run(contextOf(output), scriptParameters(nextScript), nullptr, nullptr);
    QVERIFY2(next.isSuccess(), qPrintable(next.message));
    QCOMPARE(pageContent(*next.documents[0].document, 0), QByteArray("1 0 0 rg\n0 0 1 rg"));

    // ---- Two pages in one script ----
    const QString twoPages = QStringLiteral("{\"revisions\":{\"1\":\"%1\",\"2\":\"%2\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]},{\"type\":\"delete\",\"id\":\"p2:3\"}]}").arg(revisionOf(document, 0), revisionOf(document, 1));
    const PDFDocumentToolResult both = tool.run(context, scriptParameters(twoPages), nullptr, nullptr);
    QVERIFY2(both.isSuccess(), qPrintable(both.message));
    QCOMPARE(both.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1, 2 }));
    QCOMPARE(both.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 2);
    QCOMPARE(pageContent(*both.documents[0].document, 0), QByteArray("1 0 0 rg\n10 10 50 50 re\nf\n0 0 1 rg"));
    QCOMPARE(pageContent(*both.documents[0].document, 1), QByteArray("0 1 0 rg"));
}

void DocumentToolContentTest::toolApplyScriptErrors()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);
    const QString revisions = QStringLiteral("\"revisions\":{\"1\":\"%1\"}").arg(revisionOf(document, 0));

    const auto run = [&tool, &context](const QString& script) { return tool.run(context, scriptParameters(script), nullptr, nullptr); };

    // ---- An operation over a node of a page without its revision is refused, with its path
    //      (zadani_overeni.md 7.1) ----
    PDFDocumentToolResult result = run(QStringLiteral("{\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}"));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));
    QVERIFY(result.documents.empty());

    // The revision of the page 1 does not open the page 2
    result = run(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]},{\"type\":\"delete\",\"ids\":[\"p2:3\"]}]}").arg(revisions));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[1]")), qPrintable(result.message));

    // ---- The operations are a transaction: the first one is right, the second one names a node,
    //      which does not exist - nothing is written ----
    result = run(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]},{\"type\":\"delete\",\"ids\":[\"p1:77\"]}]}").arg(revisions));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[1]")), qPrintable(result.message));
    QVERIFY(result.documents.empty());

    // A node deleted by an earlier operation of the script is gone for the later ones
    result = run(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]},{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}").arg(revisions));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[1]")), qPrintable(result.message));

    // ---- An operation, which can be read and cannot be done (a path is no group to dissolve),
    //      stops the script too, though a right operation follows ----
    result = run(QStringLiteral("{%1,\"items\":[{\"type\":\"ungroup\",\"id\":\"p1:5\"},{\"type\":\"delete\",\"ids\":[\"p1:3\"]}]}").arg(revisions));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));
    QVERIFY(result.documents.empty());

    // ---- An unknown operation; an operation with a wrong value ----
    result = run(QStringLiteral("{%1,\"items\":[{\"type\":\"explode\",\"ids\":[\"p1:5\"]}]}").arg(revisions));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));

    result = run(QStringLiteral("{%1,\"items\":[{\"type\":\"transform\",\"ids\":[\"p1:5\"],\"matrix\":[1,0,0,1,1e300,0]}]}").arg(revisions));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));

    // ---- A script, which cannot be read; a revision of a page out of the document; a stale
    //      revision ----
    result = run(QStringLiteral("{\"items\":[]}"));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.contains(QStringLiteral("operations.items: ")), qPrintable(result.message));

    result = run(QStringLiteral("{\"revisions\":{\"3\":\"abcd\"},\"items\":[{\"type\":\"clear_page\",\"page\":1}]}"));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.contains(QStringLiteral("operations.revisions.3: ")), qPrintable(result.message));

    result = run(QStringLiteral("{\"revisions\":{\"1\":\"%1\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}").arg(revisionOf(document, 1)));
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QCOMPARE(result.message, QStringLiteral("The document changed in the meantime."));

    // A direct call of execute checks the script itself
    QVERIFY(tool.execute(context, scriptParameters(QStringLiteral("not a script")), nullptr, nullptr).status == PDFDocumentToolResultStatus::Failed);

    // A canceled replay is stopped
    const ContentToolTestControl control(0);
    QVERIFY(tool.execute(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}").arg(revisions)), &control, nullptr).status == PDFDocumentToolResultStatus::Stopped);
}

void DocumentToolContentTest::toolApplyScriptNames()
{
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);
    const QString revisions = QStringLiteral("\"revisions\":{\"1\":\"%1\"}").arg(revisionOf(document, 0));

    // ---- A new node gets a name for the later operations of the script: the first path is
    //      copied to the end of the page as "c1", and "c1" is deleted again - the page has its
    //      two paths as before ----
    PDFDocumentToolResult result = tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"copy\",\"ids\":[\"p1:3\"],\"parent\":\"p1:1\",\"as\":\"c1\"},"
                                                                                     "{\"type\":\"delete\",\"ids\":[\"c1\"]}]}").arg(revisions)), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success || result.status == PDFDocumentToolResultStatus::NothingToDo, qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("objectsAdded")).toInt(0), 0);
    if (result.isSuccess())
    {
        const PDFContentContainerPointer readBack = buildPage(*result.documents[0].document, 0);
        QCOMPARE(nodesOfKind(*readBack, PDFContentNodeKind::Path).size(), size_t(2));
    }

    // The copy stays: three paths, one object added
    result = tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"copy\",\"ids\":[\"p1:3\"],\"parent\":\"p1:1\",\"as\":\"c1\"}]}").arg(revisions)), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("objectsAdded")).toInt(-1), 1);
    QCOMPARE(nodesOfKind(*buildPage(*result.documents[0].document, 0), PDFContentNodeKind::Path).size(), size_t(3));

    // ---- A name, which no operation has given; a name used before the operation, which gives it ----
    result = tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"c1\"]}]}").arg(revisions)), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));

    result = tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"c1\"]},{\"type\":\"copy\",\"ids\":[\"p1:3\"],\"parent\":\"p1:1\",\"as\":\"c1\"}]}").arg(revisions)), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));

    // ---- A name for an operation, which adds no node ----
    result = tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:5\"],\"as\":\"gone\"}]}").arg(revisions)), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0].as: ")), qPrintable(result.message));

    // ---- A name of the form of an identifier is refused, when the script is read: the later
    //      "p1:5" is the node 5 and never the copy ----
    result = tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[{\"type\":\"copy\",\"ids\":[\"p1:3\"],\"parent\":\"p1:1\",\"as\":\"p1:5\"},{\"type\":\"delete\",\"ids\":[\"p1:5\"]}]}").arg(revisions)), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.contains(QStringLiteral("operations.items[0].as: ")), qPrintable(result.message));
    QVERIFY(result.documents.empty());
}

void DocumentToolContentTest::toolApplyScriptLimits()
{
    // zadani_overeni.md 6.2, 7.1: a call of an assistant changes at most 20 pages (the user
    // confirms each of them). 21 pages, each cleared by one operation.
    Tool tool;
    QList<QByteArray> contents;
    for (int i = 0; i < 21; ++i)
    {
        contents << QByteArray(ONE_PATH);
    }
    const PDFDocument document = createPagesDocument(contents);
    const PDFDocumentToolContext context = contextOf(document);

    const auto script = [&document](int pageCount)
    {
        QStringList revisions;
        QStringList items;
        for (int i = 0; i < pageCount; ++i)
        {
            revisions << QStringLiteral("\"%1\":\"%2\"").arg(i + 1).arg(revisionOf(document, i));
            items << QStringLiteral("{\"type\":\"clear_page\",\"page\":%1}").arg(i + 1);
        }
        return QStringLiteral("{\"revisions\":{%1},\"items\":[%2]}").arg(revisions.join(QLatin1Char(',')), items.join(QLatin1Char(',')));
    };

    PDFDocumentToolResult result = tool.run(context, scriptParameters(script(20)), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray().size(), 20);
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 20);
    QCOMPARE(pageContent(*result.documents[0].document, 0).trimmed(), QByteArray());
    QCOMPARE(pageContent(*result.documents[0].document, 20), QByteArray(ONE_PATH));

    result = tool.run(context, scriptParameters(script(21)), nullptr, nullptr);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());
    QVERIFY2(result.message.contains(QStringLiteral("20")), qPrintable(result.message));

    // The panels have no such limit: their snapshot of 21 changed pages is written
    std::vector<PDFContentContainerPointer> containers;
    for (int i = 0; i < 21; ++i)
    {
        const PDFContentContainerPointer page = buildPage(document, i);
        containers.push_back(removeNode(page, nodesOfKind(*page, PDFContentNodeKind::Path)[0]->getId()));
    }
    result = tool.run(context, applyParameters(snapshotPointerOf(containers)), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray().size(), 21);
}

void DocumentToolContentTest::toolApplyScriptFormRevision()
{
    // zadani_overeni.md 7.1: "without a revision an operation over an existing node is refused
    // (identifiers are ordinal numbers and would silently point to other nodes after a change)".
    // Two pages draw the form /Fm1 of three squares - the nodes 2, 3 and 4 of its container:
    //   page 1, page 2: "/Fm1 Do"        /Fm1: "0 0 1 1 re f 2 2 1 1 re f 4 4 1 1 re f"
    // An assistant lists the page 1 and keeps "p1:2/3", the second square. Then the first
    // square is deleted through the page 2: the form is rewritten at its reference, its squares
    // are the nodes 2 and 3 now, and "p1:2/3" would name the third square. Nothing of the page 1
    // itself has changed - its revision must have changed all the same.
    Tool tool;
    PDFDocumentBuilder builder;
    const PDFObject form = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                                  { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                  { "Resources", Documents::createDictionaryObject({ }) } }, "0 0 1 1 re f 2 2 1 1 re f 4 4 1 1 re f");
    for (int i = 0; i < 2; ++i)
    {
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder.mergeTo(page, Documents::createDictionaryObject({ { "Resources", formResources({ { "Fm1", form } }) },
                                                                  { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/Fm1 Do") } }));
    }
    const PDFDocument document = builder.build();
    const PDFDocumentToolContext context = contextOf(document);

    const auto formContent = [&form](const PDFDocument& someDocument) { return someDocument.getDecodedStream(someDocument.getObjectByReference(form.getReference()).getStream()); };
    const auto listRevision = [&tool](const PDFDocumentToolContext& someContext, const QString& page)
    {
        const PDFDocumentToolResult listed = tool.run(someContext, listParameters(page), nullptr, nullptr);
        return listed.data.value(QStringLiteral("pages")).toArray()[0].toObject().value(QStringLiteral("revision")).toString();
    };
    const auto script = [](int page, const QString& revision, const QString& id)
    {
        return scriptParameters(QStringLiteral("{\"revisions\":{\"%1\":\"%2\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"%3\"]}]}").arg(page).arg(revision, id));
    };

    // ---- The listing of the page 1: the form with its three squares ----
    const PDFDocumentToolResult listed = tool.run(context, listParameters(QStringLiteral("1")), nullptr, nullptr);
    QVERIFY2(listed.isSuccess(), qPrintable(listed.message));
    const QJsonObject listedPage = listed.data.value(QStringLiteral("pages")).toArray()[0].toObject();
    QCOMPARE(listedIds(listedPage.value(QStringLiteral("objects")).toArray()), QStringList({ QStringLiteral("p1:2"), QStringLiteral("p1:2/2"), QStringLiteral("p1:2/3"), QStringLiteral("p1:2/4") }));
    const QString revision1 = listedPage.value(QStringLiteral("revision")).toString();
    QCOMPARE(revision1.size(), 64);

    // ---- The first square is deleted through the page 2 ----
    const PDFDocumentToolResult first = tool.run(context, script(2, listRevision(context, QStringLiteral("2")), QStringLiteral("p2:2/2")), nullptr, nullptr);
    QVERIFY2(first.isSuccess(), qPrintable(first.message));
    QCOMPARE(first.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1, 2 }));
    const PDFDocument& changed = *first.documents[0].document;
    QCOMPARE(formContent(changed), QByteArray(" 2 2 1 1 re f 4 4 1 1 re f"));

    // The page 1 itself is what it was: its object and the fingerprint of its content
    const PDFObjectReference page1 = document.getCatalog()->getPage(0)->getPageReference();
    QVERIFY(changed.getObjectByReference(page1) == document.getObjectByReference(page1));
    QCOMPARE(PDFContentTreeBuilder::computeFingerprint(&changed, pageKey(changed, 0)), PDFContentTreeBuilder::computeFingerprint(&document, pageKey(document, 0)));

    // ---- The script of the old listing over the changed document is refused: the third
    //      square is not deleted in place of the second one ----
    const PDFDocumentToolContext changedContext = contextOf(changed);
    const PDFDocumentToolResult stale = tool.run(changedContext, script(1, revision1, QStringLiteral("p1:2/3")), nullptr, nullptr);
    QVERIFY(stale.status == PDFDocumentToolResultStatus::Failed);
    QCOMPARE(stale.message, QStringLiteral("The document changed in the meantime."));
    QCOMPARE(stale.data.value(QStringLiteral("changedPage")).toInt(), 1);
    QVERIFY(stale.documents.empty());

    // ---- A new listing gives the revision of the changed form; "p1:2/3" is the last square now ----
    const QString revision2 = listRevision(changedContext, QStringLiteral("1"));
    QVERIFY(revision2 != revision1);
    const PDFDocumentToolResult next = tool.run(changedContext, script(1, revision2, QStringLiteral("p1:2/3")), nullptr, nullptr);
    QVERIFY2(next.isSuccess(), qPrintable(next.message));
    QCOMPARE(formContent(*next.documents[0].document), QByteArray(" 2 2 1 1 re f"));

    // Over the unchanged document the first revision still opens the page 1
    const PDFDocumentToolResult direct = tool.run(context, script(1, revision1, QStringLiteral("p1:2/3")), nullptr, nullptr);
    QVERIFY2(direct.isSuccess(), qPrintable(direct.message));
    QCOMPARE(formContent(*direct.documents[0].document), QByteArray("0 0 1 1 re f 4 4 1 1 re f"));
}

void DocumentToolContentTest::toolApplyScriptRevisionOfLimitedWalk_data()
{
    QTest::addColumn<bool>("isNesting");
    QTest::addColumn<QByteArray>("expectedContent");

    QTest::newRow("1 100 forms") << false << QByteArray("\n/F1099 Do");
    QTest::newRow("a chain of 32 forms") << true << QByteArray();
}

void DocumentToolContentTest::toolApplyScriptRevisionOfLimitedWalk()
{
    // zadani_overeni.md 7.1 (RE-15): the revision of a page, which the mode list gives, opens the
    // page in a script - also when the script names other pages, which draw the same forms, and
    // the walk of the page over its forms is cut by a limit (1 000 forms a page; the nesting 32
    // of the session). The script deletes the first object of the page 1 under the revisions
    // of the pages 1 and 2; the document is not changed between the listing and the script.
    QFETCH(bool, isNesting);
    QFETCH(QByteArray, expectedContent);

    const PDFObject formResourcesNone = Documents::createDictionaryObject({ });
    const auto addForm = [&formResourcesNone](PDFDocumentBuilder* builder, const QByteArray& content, const PDFObject& resources)
    {
        return Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") }, { "Subtype", PDFObject::createName("Form") },
                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                     { "Resources", resources.isNull() ? formResourcesNone : resources } }, content);
    };

    // ---- 1 100 distinct forms /F0 ... /F1099 ("0 0 1 1 re f" each). The page 2 draws all of
    //      them, so its walk loads 1 000 and leaves 100 out (in the order of the content /F1000
    //      ... /F1099, in the opposite order /F0 ... /F99). The page 1 draws /F0 and /F1099: one
    //      of them is a form the page 2 has left out, whichever order the walk takes. ----
    PDFDocumentBuilder builder;
    std::vector<std::pair<PDFObject, QByteArray>> pages;    // The resources and the content of the pages 1 and 2
    if (!isNesting)
    {
        Documents::DictionaryEntries allForms;
        QByteArray page2Content;
        for (int i = 0; i < 1100; ++i)
        {
            const QByteArray name = "F" + QByteArray::number(i);
            allForms.emplace_back(name, addForm(&builder, "0 0 1 1 re f", PDFObject()));
            page2Content += '/' + name + " Do\n";
        }

        const PDFObject page1Resources = formResources({ allForms.front(), allForms.back() });
        const PDFObject page2Resources = formResources(allForms);
        pages.push_back({ page1Resources, QByteArray("/F0 Do\n/F1099 Do") });
        pages.push_back({ page2Resources, page2Content });
    }
    else
    {
        // ---- The nesting: the page 1 draws a chain of 32 forms /C1 ... /C32 ("/C<k+1> Do",
        //      the last "/G Do"), so it reaches /G at the nesting 33, which the session does not
        //      load; the page 2 draws /G directly ("0 0 1 1 re f"). ----
        const PDFObject formG = addForm(&builder, "0 0 1 1 re f", PDFObject());
        PDFObject next = formG;
        QByteArray nextName = "G";
        for (int k = 32; k >= 1; --k)
        {
            next = addForm(&builder, '/' + nextName + " Do", formResources({ { nextName, next } }));
            nextName = "C" + QByteArray::number(k);
        }

        pages.push_back({ formResources({ { "C1", next } }), QByteArray("/C1 Do") });
        pages.push_back({ formResources({ { "G", formG } }), QByteArray("/G Do") });
    }

    for (const auto& page : pages)
    {
        const PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", page.first },
                                                                           { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), page.second) } }));
    }
    const PDFDocument document = builder.build();
    const PDFDocumentToolContext context = contextOf(document);

    // The listing of the pages 1 and 2 (a session of each page)
    Tool tool;
    const PDFDocumentToolResult listed = tool.run(context, listParameters(QStringLiteral("1-2"), 1), nullptr, nullptr);
    QVERIFY2(listed.isSuccess(), qPrintable(listed.message));
    const QJsonArray listedPages = listed.data.value(QStringLiteral("pages")).toArray();
    QCOMPARE(listedPages.size(), 2);

    // The script under both revisions
    const QString script = QStringLiteral("{\"revisions\":{\"1\":\"%1\",\"2\":\"%2\"},\"items\":[{\"type\":\"delete\",\"ids\":[\"p1:2\"]}]}")
                               .arg(listedPages[0].toObject().value(QStringLiteral("revision")).toString(), listedPages[1].toObject().value(QStringLiteral("revision")).toString());
    const PDFDocumentToolResult result = tool.run(context, scriptParameters(script), nullptr, nullptr);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1 }));
    QCOMPARE(pageContent(*result.documents[0].document, 0), expectedContent);
}

void DocumentToolContentTest::toolApplyScriptWithoutEffect()
{
    // Owner 2026-11-26: an operation of a script, which changes nothing, is a success without an
    // effect - the script goes on and the result lists it. The page of two paths: the second
    // path (the node 5) is the last object, so its reorder to the end of the page changes
    // nothing (PDFContentCommand::isRefusedAsUnchanged).
    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS, ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);
    const QString revisions = QStringLiteral("\"revisions\":{\"1\":\"%1\"}").arg(revisionOf(document, 0));
    const QString toEnd = QStringLiteral("{\"type\":\"reorder\",\"id\":\"p1:5\",\"parent\":\"p1:1\"}");
    const QString key = QStringLiteral("operationsWithoutEffect");
    QCOMPARE(QByteArray(Tool::DATA_OPERATIONS_WITHOUT_EFFECT), QByteArray("operationsWithoutEffect"));

    const auto run = [&tool, &context, &revisions](const QStringList& items)
    {
        return tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[%2]}").arg(revisions, items.join(QLatin1Char(',')))), nullptr, nullptr);
    };
    const auto operation = [](int index) { return QJsonObject({ { QStringLiteral("index"), index }, { QStringLiteral("type"), QStringLiteral("reorder") } }); };

    // ---- Between two such operations the first path is deleted: the script is applied, the
    //      result names the operations 0 and 2 ----
    PDFDocumentToolResult result = run({ toEnd, QStringLiteral("{\"type\":\"delete\",\"ids\":[\"p1:3\"]}"), toEnd });
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));
    QCOMPARE(result.data.value(key).toArray(), QJsonArray({ operation(0), operation(2) }));
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 1);
    QCOMPARE(result.data.value(QStringLiteral("objectsEdited")).toInt(-1), 0);
    QCOMPARE(pageContent(*result.documents[0].document, 0), QByteArray("1 0 0 rg\n0 0 1 rg\n100 100 20 20 re\nf"));

    // ---- A script of only such operations: nothing to do, and the result says, which ----
    result = run({ toEnd, toEnd });
    QVERIFY(result.status == PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.documents.empty());
    QCOMPARE(result.data.value(key).toArray(), QJsonArray({ operation(0), operation(1) }));

    // ---- A script without such an operation has no such data ----
    result = run({ QStringLiteral("{\"type\":\"delete\",\"ids\":[\"p1:3\"]}") });
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(!result.data.contains(key));

    // ---- Every other refusal fails the script as before: after such an operation too ----
    result = run({ toEnd, QStringLiteral("{\"type\":\"delete\",\"ids\":[\"p1:77\"]}") });
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[1]")), qPrintable(result.message));
    QVERIFY(result.documents.empty());

    result = run({ QStringLiteral("{\"type\":\"reorder\",\"id\":\"p1:5\",\"parent\":\"p1:1\",\"index\":99}") });
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0]")), qPrintable(result.message));

    // ---- Such an operation adds nothing: its name of "as" names nothing for the later ones ----
    result = run({ QStringLiteral("{\"type\":\"reorder\",\"id\":\"p1:5\",\"parent\":\"p1:1\",\"as\":\"moved\"}"), QStringLiteral("{\"type\":\"delete\",\"ids\":[\"moved\"]}") });
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[1]")), qPrintable(result.message));
}

void DocumentToolContentTest::toolApplyScriptClearPageTwice()
{
    // Owner 2026-11-26 (decision 7 of pe0_fixes.md, G2-4): an assistant, which repeats its
    // cleaning, clears a page twice. The second clear_page finds a page without content: the
    // model is what it asks for already, so it is an operation without an effect, not an error
    // of the script. The pages "0 1 0 rg 20 20 30 30 re f" (a state and a path) and "" (none).
    Tool tool;
    const PDFDocument document = createPagesDocument({ ONE_PATH, QByteArray(), ONE_PATH });
    const PDFDocumentToolContext context = contextOf(document);
    const QString key = QStringLiteral("operationsWithoutEffect");
    const auto operation = [](int index) { return QJsonObject({ { QStringLiteral("index"), index }, { QStringLiteral("type"), QStringLiteral("clear_page") } }); };
    const auto run = [&tool, &context, &document](int page, int count)
    {
        QStringList items;
        for (int i = 0; i < count; ++i)
        {
            items << QStringLiteral("{\"type\":\"clear_page\",\"page\":%1}").arg(page);
        }
        const QString script = QStringLiteral("{\"revisions\":{\"%1\":\"%2\"},\"items\":[%3]}").arg(page).arg(revisionOf(document, page - 1), items.join(QLatin1Char(',')));
        return tool.run(context, scriptParameters(script), nullptr, nullptr);
    };

    // ---- The page 1 cleared twice: the first operation removes the path, the second one
    //      changes nothing and is listed; the script succeeds ----
    PDFDocumentToolResult result = run(1, 2);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::Success, qPrintable(result.message));
    QCOMPARE(result.data.value(key).toArray(), QJsonArray({ operation(1) }));
    QCOMPARE(result.data.value(QStringLiteral("pagesChanged")).toArray(), QJsonArray({ 1 }));
    QCOMPARE(result.data.value(QStringLiteral("objectsRemoved")).toInt(-1), 1);
    QCOMPARE(pageContent(*result.documents[0].document, 0).trimmed(), QByteArray());
    QCOMPARE(pageContent(*result.documents[0].document, 2), QByteArray(ONE_PATH));

    // ---- The page 2 has no content: clearing it is nothing to do, and the result says, which
    //      operations changed nothing ----
    result = run(2, 1);
    QVERIFY2(result.status == PDFDocumentToolResultStatus::NothingToDo, qPrintable(result.message));
    QVERIFY(result.documents.empty());
    QCOMPARE(result.data.value(key).toArray(), QJsonArray({ operation(0) }));
}

void DocumentToolContentTest::toolApplyScriptGrowth()
{
    // zadani_overeni.md 6.2: a script must not grow a page without a bound (the limits are those
    // of the commands: PDFContentCommand::MAXIMUM_NODES_OF_COMMAND = 100 000 nodes created by one
    // command). The tool has the limits of the count of the operations (500) and of the changed
    // pages only, so the commands must stop it.
    QCOMPARE(PDFContentCommand::MAXIMUM_NODES_OF_COMMAND, size_t(100000));

    Tool tool;
    const PDFDocument document = createPagesDocument({ TWO_PATHS });
    const PDFDocumentToolContext context = contextOf(document);
    const QString revisions = QStringLiteral("\"revisions\":{\"1\":\"%1\"}").arg(revisionOf(document, 0));
    const auto run = [&tool, &context, &revisions](const QStringList& items)
    {
        return tool.run(context, scriptParameters(QStringLiteral("{%1,\"items\":[%2]}").arg(revisions, items.join(QLatin1Char(',')))), nullptr, nullptr);
    };

    // ---- An object named twice by one operation is an error of the operation (a copy of "the
    //      same object twice" has no single meaning) ----
    PDFDocumentToolResult result = run({ QStringLiteral("{\"type\":\"copy\",\"ids\":[\"p1:3\",\"p1:3\"],\"parent\":\"p1:1\"}") });
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[0].ids[1]: ")), qPrintable(result.message));
    QVERIFY(result.documents.empty());

    // ---- Doubling: the last path is copied and grouped with its copy, then the group is
    //      copied and grouped with its copy, again and again - 60 operations:
    //        0   copy p1:5 as a                 1   group p1:5, a as g       g has 3 nodes
    //        2k  copy g as h                    2k + 1   group g, h as g     g has 2^(k + 1) - 1 nodes ... 2^(k + 2) - 1
    //      The copy of the operation 2k creates as many nodes as the group has: 3, 7, 15 ...
    //      2^(k + 1) - 1. The first number above 100 000 is 2^17 - 1 = 131 071, for k = 16: the
    //      operation 32 is refused, with its path, and nothing is written. Without the limit the
    //      60 operations would make 2^30 nodes. ----
    QStringList items = { QStringLiteral("{\"type\":\"copy\",\"ids\":[\"p1:5\"],\"parent\":\"p1:1\",\"as\":\"a\"}"),
                          QStringLiteral("{\"type\":\"group\",\"ids\":[\"p1:5\",\"a\"],\"as\":\"g\"}") };
    for (int k = 1; k < 30; ++k)
    {
        items << QStringLiteral("{\"type\":\"copy\",\"ids\":[\"g\"],\"parent\":\"p1:1\",\"as\":\"h\"}");
        items << QStringLiteral("{\"type\":\"group\",\"ids\":[\"g\",\"h\"],\"as\":\"g\"}");
    }
    QCOMPARE(items.size(), 60);

    QElapsedTimer timer;
    timer.start();
    result = run(items);
    QVERIFY(result.status == PDFDocumentToolResultStatus::Failed);
    QVERIFY2(result.message.startsWith(QStringLiteral("operations.items[32]: ")), qPrintable(result.message));
    QVERIFY(result.documents.empty());
    const qint64 limit = qint64(120000 * qMax(1.0, qEnvironmentVariable("PDF4QT_TEST_TIMEOUT_FACTOR", QStringLiteral("1")).toDouble()));
    QVERIFY2(timer.elapsed() < limit, qPrintable(QString::number(timer.elapsed())));

    // The same script cut before the refused operation is applied: the page has the two paths
    // of the document and 2^16 copies of the second one less one - 2^16 + 1 paths
    result = run(items.mid(0, 32));
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(nodesOfKind(*buildPage(*result.documents[0].document, 0), PDFContentNodeKind::Path).size(), size_t(65537));
}

QTEST_MAIN(DocumentToolContentTest)

#include "tst_documenttoolcontenttest.moc"
