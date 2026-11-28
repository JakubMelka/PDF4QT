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

// Edit Content, stage E0: the editing session, the undo stack and the basic commands (package
// P03; GUI_overhaul/edit_tool/zadani_jadro.md 4.4 - 4.6, 4.12, zadani_overeni.md 6.1 - 6.3,
// chapter 8). The packages of the later stages add their own source files to this program.
//
// Every command is checked by the bytes the stream writer writes for the changed container and
// by undo and redo giving the same versions of the container. The expected contents are written
// by hand from the rules R-1 ... R-6 of the plan and from ISO 32000-2, never taken from a run.
// They are compared as sequences of tokens (white space is collapsed): where the line feeds of
// the stream writer go is the business of package P01 and of its tests.
//
// Matrices are written as in PDF: [a b c d e f] maps (x, y) to (a x + c y + e, b x + d y + f),
// and "M cm" makes the new matrix M x CTM (the point goes through M first).

#include "contenttestdocuments.h"

#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentjson.h"
#include "pdfcontentstreamwriter.h"
#include "pdfcontentstructure.h"
#include "pdfcontentundostack.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QThreadPool>
#include <QWeakPointer>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

using namespace pdf;

using Documents = pdftest::ContentTestDocuments;
using Factory = PDFContentCommandFactory;
using Key = PDFContentContainerKey;
using Kind = PDFContentNodeKind;
using Property = PDFContentStateProperty;

/// A command of the tests, which changes no model: it shows what the undo stack and the session
/// do with a command
class ContentCommandsTestCommand : public PDFContentCommand
{
public:
    explicit ContentCommandsTestCommand(QString text, int* destroyedCount = nullptr) :
        m_text(std::move(text)),
        m_destroyedCount(destroyedCount)
    {

    }

    virtual ~ContentCommandsTestCommand() override
    {
        if (m_destroyedCount)
        {
            ++*m_destroyedCount;
        }
    }

    virtual bool apply(PDFContentEditSession* session, QString* errorText) override
    {
        ++applyCount;

        if (onApply)
        {
            onApply(session);
        }

        if (isRefused)
        {
            if (errorText)
            {
                *errorText = QStringLiteral("refused by the test");
            }
            return false;
        }

        PDFContentChange change;
        change.isStructureChanged = true;
        setChange(change);
        return true;
    }

    virtual void revert(PDFContentEditSession* session) override
    {
        Q_UNUSED(session);
        ++revertCount;

        PDFContentChange change;
        change.isStructureChanged = true;
        setChange(change);
    }

    virtual bool mergeWith(const PDFContentCommand& next) override
    {
        const ContentCommandsTestCommand* other = dynamic_cast<const ContentCommandsTestCommand*>(&next);
        if (!isMergeable || !other || !other->isMergeable)
        {
            return false;
        }

        m_text += QLatin1Char('+') + other->m_text;
        return true;
    }

    virtual QString getText() const override { return m_text; }
    virtual PDFInteger getPageIndex() const override { return pageIndex; }
    virtual QByteArray getType() const override { return QByteArray("test_command"); }
    virtual QJsonObject toJson() const override { return QJsonObject(); }

    bool isRefused = false;
    bool isMergeable = false;
    PDFInteger pageIndex = -1;
    int applyCount = 0;
    int revertCount = 0;
    std::function<void(PDFContentEditSession*)> onApply;

private:
    QString m_text;
    int* m_destroyedCount = nullptr;
};

/// A session over a document of the test
class ContentCommandsFixture
{
public:
    explicit ContentCommandsFixture(const PDFDocument& document) :
        m_document(new PDFDocument(document)),
        m_session(new PDFContentEditSession(m_document, PDFCMSPointer(), nullptr))
    {

    }

    PDFContentEditSession* session() const { return m_session.get(); }
    const PDFDocumentPointer& document() const { return m_document; }

    /// The key of a page, whose container is loaded (not valid, when it cannot be loaded)
    Key page(PDFInteger pageIndex = 0)
    {
        const Key key = m_session->getPageKey(pageIndex);
        QString errorText;
        return m_session->loadContainer(key, nullptr, &errorText) ? key : Key();
    }

    /// The nodes of a kind of the current version of a container, in the order of the tree
    std::vector<PDFContentNodeId> nodes(const Key& key, Kind kind) const
    {
        std::vector<PDFContentNodeId> result;
        const PDFContentContainerPointer container = m_session->getContainer(key);
        if (container && container->getRoot())
        {
            collect(container->getRoot(), kind, &result);
        }
        return result;
    }

    /// The node of a kind by its order in the tree (not valid, when there is none)
    PDFContentNodeReference node(const Key& key, Kind kind, size_t index = 0) const
    {
        const std::vector<PDFContentNodeId> ids = nodes(key, kind);
        return index < ids.size() ? PDFContentNodeReference{ key, ids[index] } : PDFContentNodeReference();
    }

    PDFContentNodeReference root(const Key& key) const { return node(key, Kind::Root); }

    PDFContentNodePointer get(const PDFContentNodeReference& reference) const
    {
        const PDFContentContainerPointer container = m_session->getContainer(reference.container);
        return container ? container->getNode(reference.node) : nullptr;
    }

    /// The written content of the current version of a container, white space collapsed; the
    /// errors of the writer instead, when it fails
    QByteArray written(const Key& key) const
    {
        const PDFContentContainerPointer container = m_session->getContainer(key);
        if (!container)
        {
            return QByteArray("<not loaded>");
        }

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(*container);
        if (!result.isSuccess())
        {
            QByteArray text("<error>");
            for (const PDFContentWriteError& error : result.errors)
            {
                text += ' ' + error.text.toUtf8();
            }
            return text;
        }

        return tokens(result.content);
    }

    /// A content as its tokens joined by one space: white space is collapsed, and the
    /// delimiters, which need no white space around them (ISO 32000-2, 7.2.3: the solidus of
    /// a name, the brackets of an array and of a dictionary), stand apart - "EMC/P" and
    /// "EMC /P" are the same content
    static QByteArray tokens(const QByteArray& content)
    {
        QByteArray result;
        for (qsizetype i = 0; i < content.size(); ++i)
        {
            const char character = content[i];
            if ((character == '<' || character == '>') && i + 1 < content.size() && content[i + 1] == character)
            {
                result += ' ';
                result += character;
                result += character;
                result += ' ';
                ++i;
            }
            else if (character == '[' || character == ']')
            {
                result += ' ';
                result += character;
                result += ' ';
            }
            else if (character == '/')
            {
                result += ' ';
                result += character;
            }
            else
            {
                result += character;
            }
        }
        return result.simplified();
    }

    /// The resources the writer gives with the content of a container
    PDFContentResources resources(const Key& key) const
    {
        const PDFContentContainerPointer container = m_session->getContainer(key);
        return container ? PDFContentStreamWriter::write(*container).resources : PDFContentResources();
    }

    /// The counter of the identifiers of the session before the last command of execute
    quint64 counterBeforeCommand() const { return m_counterBeforeCommand; }

    bool execute(const PDFContentCommandPointer& command, QString* errorText = nullptr)
    {
        m_counterBeforeCommand = m_session->getNodeIdCounter();

        QString text;
        const bool result = m_session->execute(command, &text);
        if (errorText)
        {
            *errorText = text;
        }
        return result;
    }

private:
    static void collect(const PDFContentNodePointer& node, Kind kind, std::vector<PDFContentNodeId>* result)
    {
        if (node->getKind() == kind)
        {
            result->push_back(node->getId());
        }
        for (const PDFContentNodePointer& child : node->getChildren())
        {
            collect(child, kind, result);
        }
    }

    PDFDocumentPointer m_document;
    std::unique_ptr<PDFContentEditSession> m_session;
    quint64 m_counterBeforeCommand = 0;
};

class ContentCommandsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The undo stack
    void undoStackSteps();
    void undoStackCleanPoint();
    void undoStackMacro();
    void undoStackMerge();
    void undoStackLimit();

    // The session
    void sessionKeysAndLoading();
    void sessionIdentifiersAndObjects();
    void sessionEditorState();
    void sessionExecuteGuards();
    void sessionUndoRedoOfSteps();
    void sessionDeletedFromSlots();
    void sessionAsynchronousLoading();
    void sessionStructure();
    void sessionTwoPagesFromPlainCode();
    void sessionDirtyPageOfForm();

    // Delete
    void deletePath();
    void deleteKeepsLeak_data();
    void deleteKeepsLeak();
    void deleteRefusals_data();
    void deleteRefusals();

    // Transform
    void transformComposes();
    void transformUnderMatrix_data();
    void transformUnderMatrix();
    void transformTextKeepsLeak();
    void transformClipNodes_data();
    void transformClipNodes();
    void transformGroup();
    void transformRefusals_data();
    void transformRefusals();
    void transformPattern();
    void transformSoftMask();

    // Set Appearance, Set Transparency
    void setAppearancePath();
    void setAppearanceText();
    void setAppearanceRefusals_data();
    void setAppearanceRefusals();
    void setTransparencyKeepsSoftMask();
    void setTransparencyValues_data();
    void setTransparencyValues();

    // Reorder
    void reorderAcrossStateChange_data();
    void reorderAcrossStateChange();
    void reorderSoftMask();
    void reorderExtGStateByReference();
    void reorderInForm();
    void reorderPlaces_data();
    void reorderPlaces();
    void reorderRefusals();

    // Group, Ungroup
    void group();
    void groupRefusals_data();
    void groupRefusals();
    void ungroup_data();
    void ungroup();
    void ungroupRefusals_data();
    void ungroupRefusals();

    // Copy
    void copyInContainer_data();
    void copyInContainer();
    void copyToOtherPage();
    void copyRefusals();

    // The structure (zadani_jadro.md 4.12, the table of the consistency)
    void structureDelete();
    void structureReorder_data();
    void structureReorder();
    void structureCopyAndClear();
    void clearPage();

    // Merging, JSON, the registry, hostile input, threads
    void mergeCommands();
    void jsonCommands();
    void jsonReplay();
    void jsonErrors_data();
    void jsonErrors();
    void registerType();
    void nestingLimit();
    void formsAreQueuedOnce();
    void sessionsInThreads();

    // Details of the rules
    void sessionRollsBackRefusedCommand();
    void sessionUndoesMacroInReverseOrder();
    void sessionAnswersRequestsOnce();
    void stateRunsAreNotSelected();
    void reorderKeepsOverride();
    void copyKeepsSuppressedOperations();
    void refusedCommandGivesIdentifiersBack();
    void partsOfTextAreRefused();
    void clearPageRefusals();
    void mergedTransformWritesItsMatrix();
    void jsonDefaults();
    void structureIdentifiersOfOneCommand();
    void reorderKeepsLayer_data();
    void reorderKeepsLayer();
    void transformPatternInForm();
    void structureMoveIntoOtherSequence();
    void reorderLeavesNodeAtItsPlace();

    // The fixes after the reviews of stage E0 (GUI_overhaul/phase2_briefs/pe0_fixes.md, package FB)
    void reorderKeepsPlaceWhenAnchorIsDissolved();
    void commandNamesObjectOnce();
    void commandCreatesLimitedNodes();
    void containerHasLimitedNodes();
    void refusedAsUnchanged();
    void commandIsExecutedOnce();
    void refusedCommandDropsNewObjects();
    void reorderKeepsTagThroughGroup();
    void copyKeepsLayer();
    void copyIntoTaggedContent_data();
    void copyIntoTaggedContent();
    void copyIntoSequenceWhichCannotBeSplit();
    void copyIntoTaggedLayer();
    void transformTextWithPatternInside();
    void copyAndStrokeWithPattern();
    void copyGroupWithPatternToOtherPage();
    void copyNeverMakesFormDrawItself();
    void groupKeepsClip_data();
    void groupKeepsClip();
    void copyFromFormWritesIdentifiers();
    void setAppearanceDeviceColorsOnly();
    void transformManyObjects();
    void fontOfGraphicsStateDictionary();
    void unclosedNodes();
    void copyStaysInFrontOfPinnedContent();
    void sessionDirtyPagesOfSharedForms();

    // ---- fixes of the second review round (pe0_fixes2.md, package H2) ----
    void orderOfContentAtItsPlace_data();
    void orderOfContentAtItsPlace();
    void copyNeverMakesPatternPaintItself();
    void pathOfFormBehindDeepPath();

    // ---- block B, step B0, package S1 (pb0_core.md): the debts of block A ----
    void undoRedoMakesVersionsAgain();
    void undoStepKeepsDifference();
    void manySiblings_data();
    void manySiblings();
    void newObjectsFollowUndoStack();
    void structureUndoKeepsDifference();
    void commandWithoutEffectIsRefusedAsUnchanged();
    void stateValuesAreCompared();
    void copyNamesResourcesOfTarget();
    void nodesInsideUnclosedNode_data();
    void nodesInsideUnclosedNode();
    void placeBehindUnclosedClippingText();

    // ---- fixes of the review R1 of step B0 (GUI_overhaul/review/pb0_review_R1.md) ----
    void undoDropsOperationsOfCommand();
    void setAppearanceOfInheritedValueInForm();
    void reorderInOrderOfFixedPriorities();
    void commandOnStructureReadAgain();

    // ---- stage E1, fix package FX1 (pe1_fixes.md item 1): the painting switched on and off ----
    void paintSwitchesOfPath_data();
    void paintSwitchesOfPath();
    void paintSwitchesUndoAndGroup();
    void paintSwitchesOfText();
    void paintSwitchesRefusals();
    void paintSwitchesMergeAndJson();

private:
    /// A page with a content, the font /F1 and the tiling pattern /P1 with /Matrix [2 0 0 2 3 4]
    /// \param paintType 1: a colored pattern; 2: an uncolored one, with the color space
    ///        /CS1 [/Pattern /DeviceRGB] for it (ISO 32000-2, 8.7.3.3)
    static PDFDocument patternDocument(const QByteArray& content, PDFObjectReference* patternReference, int paintType = 1)
    {
        return Documents::createDocument(content, [patternReference, paintType](PDFDocumentBuilder* builder)
        {
            const PDFObject pattern = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("Pattern") },
                                                                            { "PatternType", PDFObject::createInteger(1) },
                                                                            { "PaintType", PDFObject::createInteger(paintType) },
                                                                            { "TilingType", PDFObject::createInteger(1) },
                                                                            { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                            { "XStep", PDFObject::createInteger(10) },
                                                                            { "YStep", PDFObject::createInteger(10) },
                                                                            { "Matrix", Documents::createIntegerArrayObject({ 2, 0, 0, 2, 3, 4 }) },
                                                                            { "Resources", Documents::createDictionaryObject({ }) } },
                                                                 paintType == 1 ? "1 0 0 rg 0 0 5 5 re f" : "0 0 5 5 re f");
            if (patternReference)
            {
                *patternReference = pattern.getReference();
            }

            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "P1", pattern } }));
            resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", Documents::addStandardFontObject(builder, "Helvetica") } }));
            if (paintType == 2)
            {
                resources.addEntry(PDFInplaceOrMemoryString("ColorSpace"), Documents::createDictionaryObject({ { "CS1", Documents::createArrayObject({ PDFObject::createName("Pattern"), PDFObject::createName("DeviceRGB") }) } }));
            }
            return resources;
        });
    }

    /// A page with a content and the layer /L1 (an optional content group named in /Properties)
    static PDFDocument layerDocument(const QByteArray& content, PDFObject* layer)
    {
        return Documents::createDocument(content, [layer](PDFDocumentBuilder* builder)
        {
            *layer = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("OCG") }, { "Name", PDFObject::createString("Layer") } }));
            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("Properties"), Documents::createDictionaryObject({ { "L1", *layer } }));
            return resources;
        });
    }

    /// The matrix of the pattern, which a node has as its fill or stroke color in its override
    static PDFObject overridePatternMatrix(const ContentCommandsFixture& fixture, const PDFContentNodeReference& node, Property property)
    {
        const PDFContentNodePointer pointer = fixture.get(node);
        if (!pointer || !pointer->getOverride() || !pointer->getOverride()->properties.count(property))
        {
            return PDFObject();
        }

        const PDFObject pattern = pointer->getOverride()->properties.at(property).color.patternObject;
        const PDFObject copy = pattern.isReference() ? fixture.session()->getNewObject(pattern.getReference()) : PDFObject();
        return copy.isStream() ? copy.getStream()->getDictionary()->get("Matrix") : PDFObject();
    }

    /// A page with the content of a tagged document: the sequence 0 (/P) of three squares, the
    /// sequence 1 (/Span) of one square, and a square without a tag. The page has /StructParents
    /// 0, the parent tree is /Nums [ 0 [P Span] ], the element P has /K 0 and the element Span /K 1.
    static QByteArray taggedContent()
    {
        return QByteArray("/P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC /Span << /MCID 1 >> BDC 60 0 10 10 re f EMC 80 0 10 10 re f");
    }

    /// \param spanChild The marked content identifier the element Span lists as its child (1;
    ///        with 0 two elements list the sequence 0, which the parent tree gives to P alone)
    static PDFDocument taggedDocument(const QByteArray& content = taggedContent(),
                                      const Documents::ResourcesFactory& createResources = &Documents::createNoResources,
                                      PDFInteger spanChild = 1)
    {
        return Documents::createDocumentFromObjects([content](PDFDocumentBuilder* builder) { return Documents::addStreamObject(builder, Documents::DictionaryEntries(), content); },
                                                    createResources,
                                                    [spanChild](PDFDocumentBuilder* builder, PDFObjectReference pageReference)
        {
            const PDFObject page = PDFObject::createReference(pageReference);
            const PDFObject root = PDFObject::createReference(builder->addObject(PDFObject()));
            const PDFObject documentElement = PDFObject::createReference(builder->addObject(PDFObject()));
            const PDFObject paragraphElement = PDFObject::createReference(builder->addObject(PDFObject()));
            const PDFObject spanElement = PDFObject::createReference(builder->addObject(PDFObject()));

            const PDFObject parentTree = Documents::addObject(builder, Documents::createDictionaryObject({ { "Nums", Documents::createArrayObject({ PDFObject::createInteger(0),
                                                                                                                                                  Documents::createArrayObject({ paragraphElement, spanElement }) }) } }));
            builder->setObject(root.getReference(), Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructTreeRoot") },
                                                                                        { "K", Documents::createArrayObject({ documentElement }) },
                                                                                        { "ParentTree", parentTree },
                                                                                        { "ParentTreeNextKey", PDFObject::createInteger(1) } }));
            builder->setObject(documentElement.getReference(), Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructElem") },
                                                                                                   { "S", PDFObject::createName("Document") },
                                                                                                   { "P", root },
                                                                                                   { "K", Documents::createArrayObject({ paragraphElement, spanElement }) } }));
            builder->setObject(paragraphElement.getReference(), Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructElem") },
                                                                                                    { "S", PDFObject::createName("P") },
                                                                                                    { "P", documentElement },
                                                                                                    { "Pg", page },
                                                                                                    { "K", PDFObject::createInteger(0) } }));
            builder->setObject(spanElement.getReference(), Documents::createDictionaryObject({ { "Type", PDFObject::createName("StructElem") },
                                                                                               { "S", PDFObject::createName("Span") },
                                                                                               { "P", documentElement },
                                                                                               { "Pg", page },
                                                                                               { "K", PDFObject::createInteger(spanChild) } }));
            builder->mergeTo(pageReference, Documents::createDictionaryObject({ { "StructParents", PDFObject::createInteger(0) } }));
            builder->mergeTo(builder->getCatalogReference(), Documents::createDictionaryObject({ { "MarkInfo", Documents::createDictionaryObject({ { "Marked", PDFObject::createBool(true) } }) },
                                                                                                 { "StructTreeRoot", root } }));
        });
    }

    /// The type of the element of a sequence of the page with /StructParents 0 (empty: none)
    static QByteArray elementType(const PDFContentStructureModelPointer& model, PDFInteger mcid)
    {
        const PDFContentStructureElement* element = model ? model->getElement(model->getElementOfMarkedContent(0, mcid)) : nullptr;
        return element ? element->type : QByteArray();
    }

    /// The marked content identifiers, which the element of a type has as its children, in their order
    static std::vector<PDFInteger> elementContent(const PDFContentStructureModelPointer& model, const QByteArray& type)
    {
        std::vector<PDFInteger> result;
        for (const PDFContentStructureElement& element : model->getElements())
        {
            if (element.type == type)
            {
                for (const PDFContentStructureChild& child : element.children)
                {
                    if (child.kind == PDFContentStructureChild::Kind::MarkedContent || child.kind == PDFContentStructureChild::Kind::MarkedContentReference)
                    {
                        result.push_back(child.mcid);
                    }
                }
            }
        }
        return result;
    }

    /// The left edges of the squares of a page in the reading order of the element of a type:
    /// its marked content identifiers in the order of /K, each with the squares of its sequence
    /// in the order of the content (ISO 32000-2, 14.7.2)
    static QList<int> readingOrder(const ContentCommandsFixture& fixture, const Key& key, const QByteArray& type)
    {
        QList<int> result;
        for (const PDFInteger mcid : elementContent(fixture.session()->getStructure(), type))
        {
            for (const PDFContentNodeId sequenceId : fixture.nodes(key, Kind::MarkedContent))
            {
                const PDFContentNodePointer sequence = fixture.get(PDFContentNodeReference{ key, sequenceId });
                if (sequence->getData().mcid == std::optional<PDFInteger>(mcid))
                {
                    collectSquares(sequence, &result);
                }
            }
        }
        return result;
    }

    static void collectSquares(const PDFContentNodePointer& node, QList<int>* result)
    {
        QRectF rectangle;
        if (node->getKind() == Kind::Path && node->getData().path.isRectangle(&rectangle))
        {
            result->push_back(int(rectangle.x()));
        }
        for (const PDFContentNodePointer& child : node->getChildren())
        {
            collectSquares(child, result);
        }
    }

    /// A page with the soft mask /GS1 (a graphics state dictionary with /SMask only; the mask
    /// dictionary is an indirect object)
    static PDFDocument softMaskDocument(const QByteArray& content, PDFObjectReference* maskReference)
    {
        return Documents::createDocument(content, [maskReference](PDFDocumentBuilder* builder)
        {
            const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                         { "Subtype", PDFObject::createName("Form") },
                                                                         { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                                         { "Group", Documents::createDictionaryObject({ { "S", PDFObject::createName("Transparency") } }) } }, "0.5 g 0 0 100 200 re f");
            const PDFObject mask = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Mask") }, { "S", PDFObject::createName("Luminosity") }, { "G", form } }));
            *maskReference = mask.getReference();

            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "SMask", mask } }) } }));
            return resources;
        });
    }

    /// A page drawing the form /Fm1 with the line width 3 ("3 w /Fm1 Do"); the form has no
    /// resources of its own
    static PDFDocument formDocument(const QByteArray& formContent)
    {
        return Documents::createDocument("3 w /Fm1 Do", [formContent](PDFDocumentBuilder* builder)
        {
            const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                         { "Subtype", PDFObject::createName("Form") },
                                                                         { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) } }, formContent);
            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm1", form } }));
            return resources;
        });
    }

    /// Adds a form XObject with the content and the forms it names; \p reference is its object
    /// (reserved before, so forms can name each other and themselves)
    static void setFormObject(PDFDocumentBuilder* builder, PDFObjectReference reference, const QByteArray& content, Documents::DictionaryEntries forms)
    {
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        dictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
        dictionary.addEntry(PDFInplaceOrMemoryString("BBox"), Documents::createIntegerArrayObject({ 0, 0, 100, 100 }));
        dictionary.addEntry(PDFInplaceOrMemoryString("Resources"), Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject(std::move(forms)) } }));
        dictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
        builder->setObject(reference, PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(content))));
    }

    /// A time limit in milliseconds scaled by PDF4QT_TEST_TIMEOUT_FACTOR
    static int timeout(int milliseconds)
    {
        bool isValid = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &isValid);
        return milliseconds * (isValid && factor > 1 ? factor : 1);
    }

    /// A page with a content and the font /F1 (Helvetica)
    static PDFDocument fontDocument(const QByteArray& content)
    {
        return Documents::createDocument(content, [](PDFDocumentBuilder* builder)
        {
            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", Documents::addStandardFontObject(builder, "Helvetica") } }));
            return resources;
        });
    }

    /// A page with a content and no resources
    static PDFDocument plainDocument(const QByteArray& content)
    {
        return Documents::createDocument(content, &Documents::createNoResources);
    }

    /// Pages with the given contents; each has the font /F1 (Helvetica for the first page,
    /// Courier for the others: the same name, another object)
    static PDFDocument pagesDocument(const std::vector<QByteArray>& contents)
    {
        PDFDocumentBuilder builder;
        for (size_t i = 0; i < contents.size(); ++i)
        {
            const PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));

            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", Documents::addStandardFontObject(&builder, i == 0 ? "Helvetica" : "Courier") } }));

            PDFDictionaryBuilder pageUpdate;
            pageUpdate.addEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resources)));
            pageUpdate.addEntry(PDFInplaceOrMemoryString("Contents"), Documents::addStreamObject(&builder, Documents::DictionaryEntries(), contents[i]));
            builder.mergeTo(pageReference, PDFObject::createDictionary(std::move(pageUpdate)));
        }
        return builder.build();
    }

    /// The matrix [a b c d e f]
    static QTransform matrix(qreal a, qreal b, qreal c, qreal d, qreal e, qreal f) { return QTransform(a, b, c, d, e, f); }

    static PDFContentStateValue realValue(PDFReal real)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Real;
        value.real = real;
        return value;
    }

    static PDFContentStateValue deviceColor(const char* space, std::vector<PDFReal> components)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Color;
        value.color.deviceSpace = space;
        value.color.components = std::move(components);
        return value;
    }

    /// The keys of a dictionary, sorted
    static QList<QByteArray> keys(const PDFObject& dictionaryObject)
    {
        QList<QByteArray> result;
        if (dictionaryObject.isDictionary())
        {
            const PDFDictionary* dictionary = dictionaryObject.getDictionary();
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                result.push_back(dictionary->getKey(i).getString());
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    /// Undo gives the version of the container before the command, redo the version after it:
    /// the same objects, and the same counter of the identifiers
    static bool isUndoRedoExact(ContentCommandsFixture& fixture, const Key& key, const PDFContentContainerPointer& before)
    {
        PDFContentEditSession* session = fixture.session();
        const PDFContentContainerPointer after = session->getContainer(key);
        const quint64 counterAfter = session->getNodeIdCounter();
        const QByteArray writtenAfter = fixture.written(key);

        if (after == before || !session->undo().has_value() || session->getContainer(key) != before)
        {
            return false;
        }

        // The counter of the identifiers is a part of the undo state: it is back, where it was
        // before the command (the last command of the fixture)
        const quint64 counterBefore = session->getNodeIdCounter();
        if (counterBefore != fixture.counterBeforeCommand())
        {
            return false;
        }

        if (!session->redo().has_value() || session->getContainer(key) != after || session->getNodeIdCounter() != counterAfter || fixture.written(key) != writtenAfter)
        {
            return false;
        }

        // Once more: the counter of the identifiers goes back with the model
        return session->undo().has_value() && session->getNodeIdCounter() == counterBefore && session->redo().has_value() && session->getContainer(key) == after;
    }
};

// -------------------------------------------------------------------------------------------------
//  The undo stack
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::undoStackSteps()
{
    int destroyedCount = 0;
    PDFContentUndoStack stack;

    // A new stack: nothing to undo or redo
    QCOMPARE(stack.getCount(), size_t(0));
    QCOMPARE(stack.getPosition(), size_t(0));
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    QVERIFY(stack.getUndoStep() == nullptr);
    QVERIFY(stack.getRedoStep() == nullptr);
    QCOMPARE(stack.getUndoText(), QString());
    QCOMPARE(stack.getRedoText(), QString());

    const PDFContentCommandPointer first = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("First"), &destroyedCount);
    const PDFContentCommandPointer second = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Second"), &destroyedCount);
    stack.push(first);
    stack.push(second);
    stack.push(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Third"), &destroyedCount));
    stack.push(nullptr);    // Nothing

    QCOMPARE(stack.getCount(), size_t(3));
    QCOMPARE(stack.getPosition(), size_t(3));
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());
    QCOMPARE(stack.getUndoText(), QStringLiteral("Third"));
    QCOMPARE(stack.getRedoText(), QString());
    QCOMPARE(stack.getUndoStep()->commands.size(), size_t(1));

    // The stack only stores: the position moves, when the session says it has reverted the step
    stack.moveToPreviousStep();
    stack.moveToPreviousStep();
    QCOMPARE(stack.getPosition(), size_t(1));
    QCOMPARE(stack.getCount(), size_t(3));
    QCOMPARE(stack.getUndoText(), QStringLiteral("First"));
    QCOMPARE(stack.getRedoText(), QStringLiteral("Second"));
    QCOMPARE(stack.getUndoStep()->commands.front(), first);
    QCOMPARE(stack.getRedoStep()->commands.front(), second);

    stack.moveToNextStep();
    QCOMPARE(stack.getPosition(), size_t(2));
    QCOMPARE(stack.getRedoText(), QStringLiteral("Third"));
    stack.moveToPreviousStep();

    // A new command drops the steps, which were undone: their commands are destroyed, unless
    // somebody else holds them (the test holds "Second")
    QCOMPARE(destroyedCount, 0);
    stack.push(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Fourth"), &destroyedCount));
    QCOMPARE(destroyedCount, 1);
    QCOMPARE(stack.getCount(), size_t(2));
    QCOMPARE(stack.getPosition(), size_t(2));
    QCOMPARE(stack.getUndoText(), QStringLiteral("Fourth"));
    QVERIFY(!stack.canRedo());

    // The ends of the stack
    stack.moveToNextStep();
    QCOMPARE(stack.getPosition(), size_t(2));
    stack.moveToPreviousStep();
    stack.moveToPreviousStep();
    stack.moveToPreviousStep();
    QCOMPARE(stack.getPosition(), size_t(0));
    QVERIFY(!stack.canUndo());
    QVERIFY(stack.canRedo());

    stack.clear();
    QCOMPARE(stack.getCount(), size_t(0));
    QCOMPARE(stack.getPosition(), size_t(0));
    QCOMPARE(destroyedCount, 2);    // "Fourth" (the test still holds "First" and "Second")
    QVERIFY(stack.isClean());
}

void ContentCommandsTest::undoStackCleanPoint()
{
    // The tool has unsaved work exactly when the stack is not at its clean point (RE-16)
    PDFContentUndoStack stack;
    QVERIFY(stack.isClean());

    stack.push(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("A")));
    QVERIFY(!stack.isClean());
    stack.moveToPreviousStep();
    QVERIFY(stack.isClean());
    stack.moveToNextStep();
    QVERIFY(!stack.isClean());

    stack.setClean();
    QVERIFY(stack.isClean());
    stack.push(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("B")));
    QVERIFY(!stack.isClean());
    stack.moveToPreviousStep();
    QVERIFY(stack.isClean());
    stack.moveToPreviousStep();
    QVERIFY(!stack.isClean());

    // The clean state was behind the position and a new command dropped it: it cannot be
    // reached any more, wherever the position goes
    stack.push(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("C")));
    QVERIFY(!stack.isClean());
    QCOMPARE(stack.getCount(), size_t(1));
    stack.moveToPreviousStep();
    QVERIFY(!stack.isClean());
    stack.moveToNextStep();
    QVERIFY(!stack.isClean());

    stack.setClean();
    QVERIFY(stack.isClean());
}

void ContentCommandsTest::undoStackMacro()
{
    PDFContentUndoStack stack;
    QVERIFY(!stack.isMacroOpen());

    const PDFContentCommandPointer first = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("First"));
    const PDFContentCommandPointer second = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Second"));
    const PDFContentCommandPointer third = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Third"));

    // One gesture = one step, with the commands in the order they were applied. Macros nest:
    // the outermost one is the step.
    stack.beginMacro(QStringLiteral("Align Objects"));
    QVERIFY(stack.isMacroOpen());
    stack.push(first);
    stack.beginMacro(QStringLiteral("Inner"));
    stack.push(second);
    stack.endMacro();
    QVERIFY(stack.isMacroOpen());

    // An open macro: nothing can be undone, and the work in it is not clean
    QVERIFY(!stack.canUndo());
    QVERIFY(stack.getUndoStep() == nullptr);
    QCOMPARE(stack.getCount(), size_t(0));
    QVERIFY(!stack.isClean());

    stack.push(third);
    stack.endMacro();
    QVERIFY(!stack.isMacroOpen());

    QCOMPARE(stack.getCount(), size_t(1));
    QCOMPARE(stack.getPosition(), size_t(1));
    QCOMPARE(stack.getUndoText(), QStringLiteral("Align Objects"));
    const PDFContentUndoStack::Step* step = stack.getUndoStep();
    QVERIFY(step != nullptr);
    QCOMPARE(step->commands.size(), size_t(3));
    QCOMPARE(step->commands[0], first);
    QCOMPARE(step->commands[1], second);
    QCOMPARE(step->commands[2], third);

    // A macro without a command adds no step; an end without a beginning does nothing
    stack.beginMacro(QStringLiteral("Nothing"));
    stack.endMacro();
    stack.endMacro();
    QCOMPARE(stack.getCount(), size_t(1));
    QVERIFY(!stack.isMacroOpen());
}

void ContentCommandsTest::undoStackMerge()
{
    auto create = [](const QString& text, bool isMergeable)
    {
        std::shared_ptr<ContentCommandsTestCommand> command = std::make_shared<ContentCommandsTestCommand>(text);
        command->isMergeable = isMergeable;
        return command;
    };

    PDFContentUndoStack stack;

    // At the clean point nothing is merged: the first command is a step of its own, also when a
    // stack, which is not empty, stands at its clean point
    stack.push(create(QStringLiteral("a"), true));
    QCOMPARE(stack.getCount(), size_t(1));

    // The following command is offered to the last one
    stack.push(create(QStringLiteral("b"), true));
    QCOMPARE(stack.getCount(), size_t(1));
    QCOMPARE(stack.getUndoText(), QStringLiteral("a+b"));

    // A command, which does not merge, is a new step
    stack.push(create(QStringLiteral("c"), false));
    QCOMPARE(stack.getCount(), size_t(2));
    stack.push(create(QStringLiteral("d"), true));
    QCOMPARE(stack.getCount(), size_t(3));

    // The clean point stands between two commands, which would merge: they stay two steps (the
    // merged step would change the state the clean point stands for)
    stack.setClean();
    stack.push(create(QStringLiteral("e"), true));
    QCOMPARE(stack.getCount(), size_t(4));
    QCOMPARE(stack.getUndoText(), QStringLiteral("e"));
    stack.moveToPreviousStep();
    QVERIFY(stack.isClean());
    stack.moveToNextStep();

    // Inside of a macro nothing is merged, and nothing is merged into a macro
    stack.beginMacro(QStringLiteral("macro"));
    stack.push(create(QStringLiteral("f"), true));
    stack.push(create(QStringLiteral("g"), true));
    stack.endMacro();
    QCOMPARE(stack.getCount(), size_t(5));
    QCOMPARE(stack.getUndoStep()->commands.size(), size_t(2));
    stack.push(create(QStringLiteral("h"), true));
    QCOMPARE(stack.getCount(), size_t(6));
    QCOMPARE(stack.getUndoText(), QStringLiteral("h"));
}

void ContentCommandsTest::undoStackLimit()
{
    // The safety limit: 10 000 steps (owner QE.12; a macro is one step). The oldest step is
    // dropped then.
    int destroyedCount = 0;
    PDFContentUndoStack stack;

    for (int i = 0; i < 5; ++i)
    {
        stack.push(std::make_shared<ContentCommandsTestCommand>(QString::number(i), &destroyedCount));
    }
    stack.setClean();   // The clean point is behind five steps

    for (int i = 5; i < 10000; ++i)
    {
        stack.push(std::make_shared<ContentCommandsTestCommand>(QString::number(i), &destroyedCount));
    }
    QCOMPARE(stack.getCount(), size_t(10000));
    QCOMPARE(destroyedCount, 0);

    // Three more: the three oldest are gone
    for (int i = 10000; i < 10003; ++i)
    {
        stack.push(std::make_shared<ContentCommandsTestCommand>(QString::number(i), &destroyedCount));
    }
    QCOMPARE(stack.getCount(), size_t(10000));
    QCOMPARE(stack.getPosition(), size_t(10000));
    QCOMPARE(destroyedCount, 3);
    QCOMPARE(stack.getUndoText(), QStringLiteral("10002"));

    // The clean point moved with the steps: it is behind the commands "3" and "4" now
    for (int i = 0; i < 10000 - 2; ++i)
    {
        QVERIFY(!stack.isClean());
        stack.moveToPreviousStep();
    }
    QCOMPARE(stack.getPosition(), size_t(2));
    QCOMPARE(stack.getUndoText(), QStringLiteral("4"));
    QVERIFY(stack.isClean());

    // The oldest step now is "3"; a clean point at the very beginning is lost with the next drop
    stack.moveToPreviousStep();
    stack.moveToPreviousStep();
    QCOMPARE(stack.getRedoText(), QStringLiteral("3"));
    stack.setClean();
    for (int i = 0; i < 10000; ++i)
    {
        stack.moveToNextStep();
    }
    stack.push(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("last"), &destroyedCount));
    QCOMPARE(stack.getCount(), size_t(10000));
    for (int i = 0; i < 10000; ++i)
    {
        stack.moveToPreviousStep();
    }
    QCOMPARE(stack.getPosition(), size_t(0));
    QVERIFY(!stack.isClean());
}

// -------------------------------------------------------------------------------------------------
//  The session
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::sessionKeysAndLoading()
{
    ContentCommandsFixture fixture(Documents::createFormDocument());
    PDFContentEditSession* session = fixture.session();
    const PDFObjectReference pageReference = fixture.document()->getCatalog()->getPage(0)->getPageReference();

    // The key of a page: its index and its object; nothing for an index out of range
    QCOMPARE(session->getPageKey(0), Key::createPage(0, pageReference));
    QVERIFY(!session->getPageKey(1).isValid());
    QVERIFY(!session->getPageKey(-1).isValid());

    // Nothing is loaded, until somebody asks
    const Key pageKey = session->getPageKey(0);
    QVERIFY(session->getContainer(pageKey) == nullptr);
    QVERIFY(session->getDirtyPages().empty());

    QString errorText;
    QVERIFY(session->loadContainer(pageKey, nullptr, &errorText));
    QVERIFY(errorText.isEmpty());
    const PDFContentContainerPointer pageContainer = session->getContainer(pageKey);
    QVERIFY(pageContainer != nullptr);
    QCOMPARE(pageContainer->getKey(), pageKey);
    QVERIFY(pageContainer->isUnchanged());

    // Loading again builds nothing: the container may hold work
    QVERIFY(session->loadContainer(pageKey, nullptr, &errorText));
    QCOMPARE(session->getContainer(pageKey), pageContainer);

    // The synchronous way loads the named container only: the forms of the page are not loaded
    const std::vector<PDFContentNodeId> formUses = fixture.nodes(pageKey, Kind::FormUse);
    QCOMPARE(formUses.size(), size_t(3));   // /Fm1 twice, /Fm2 once
    const Key formKey = Key::createForm(pageContainer->getNode(formUses.front())->getData().xobject);
    QVERIFY(formKey.isValid());
    QVERIFY(session->getContainer(formKey) == nullptr);

    QVERIFY(session->loadContainer(formKey, nullptr, &errorText));
    const PDFContentContainerPointer formContainer = session->getContainer(formKey);
    QVERIFY(formContainer != nullptr);
    QCOMPARE(formContainer->getKey(), formKey);

    // The content of the form is the stream of the form, byte for byte
    QCOMPARE(PDFContentStreamWriter::write(*formContainer).content, Documents::formContent());

    // Keys, which name nothing: no container, a reason
    for (const Key& key : { Key(), Key::createPage(5, pageReference), Key::createPage(0, PDFObjectReference(9999, 0)), Key::createForm(PDFObjectReference(9999, 0)) })
    {
        QString text;
        QVERIFY(!session->loadContainer(key, nullptr, &text));
        QVERIFY(!text.isEmpty());
        QVERIFY(session->getContainer(key) == nullptr);
    }

    // A form of a session, which has not loaded the page yet
    {
        ContentCommandsFixture other(Documents::createFormDocument());
        QString text;
        QVERIFY(!other.session()->loadContainer(formKey, nullptr, &text));
        QVERIFY(!text.isEmpty());
        QVERIFY(other.session()->loadContainer(other.session()->getPageKey(0), nullptr, &text));
        QVERIFY(other.session()->loadContainer(formKey, nullptr, &text));
    }

    // A session without a document loads nothing and does not fail
    {
        PDFContentEditSession empty(PDFDocumentPointer(), PDFCMSPointer(), nullptr);
        QString text;
        QVERIFY(!empty.getPageKey(0).isValid());
        QVERIFY(!empty.loadContainer(pageKey, nullptr, &text));
        QVERIFY(!text.isEmpty());
        QVERIFY(empty.createSnapshot()->isEmpty());
    }
}

void ContentCommandsTest::sessionIdentifiersAndObjects()
{
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();

    // The identifiers of the session start at 2^32 and grow by one; loading a container does
    // not move them (decision 10 of the skeleton)
    QCOMPARE(session->getNodeIdCounter(), Q_UINT64_C(4294967296));
    QCOMPARE(session->allocateNodeId().value, Q_UINT64_C(4294967296));
    QCOMPARE(session->allocateNodeId().value, Q_UINT64_C(4294967297));
    QVERIFY(fixture.page().isValid());
    QCOMPARE(session->getNodeIdCounter(), Q_UINT64_C(4294967298));

    // The counter is a part of the undo state: a command puts it back
    session->setNodeIdCounter(Q_UINT64_C(4294967296));
    QCOMPARE(session->allocateNodeId().value, Q_UINT64_C(4294967296));

    // It never goes below the identifiers of the builder
    session->setNodeIdCounter(5);
    QCOMPARE(session->getNodeIdCounter(), Q_UINT64_C(4294967296));

    // The new objects get provisional references with the reserved generation 65535 - the
    // generation of the head of the free list of a cross-reference table (ISO 32000-2, 7.5.4),
    // which no object in use has - and the numbers 1, 2, ... So no reference of the document
    // can be taken for one of them: not a reference to an object, which exists (generation 0
    // here), and not a dangling one with any number.
    QCOMPARE(PDFContentEditSession::NEW_OBJECT_GENERATION, PDFInteger(65535));
    const PDFInteger objectCount = PDFInteger(fixture.document()->getStorage().getObjects().size());
    QVERIFY(objectCount > 2);

    const PDFObjectReference first = session->addNewObject(PDFObject::createInteger(7));
    const PDFObjectReference second = session->addNewObject(PDFObject::createName("Second"));
    QCOMPARE(first, PDFObjectReference(1, 65535));
    QCOMPARE(second, PDFObjectReference(2, 65535));
    QCOMPARE(session->getNewObject(first), PDFObject::createInteger(7));
    QCOMPARE(session->getNewObject(second), PDFObject::createName("Second"));
    QVERIFY(session->getNewObject(PDFObjectReference(3, 65535)).isNull());

    // The same numbers with the generation of the objects of the document are no new objects,
    // and neither is the first number behind the objects of the document (a reference to it
    // in the document is a reference to nothing, ISO 32000-2, 7.3.10)
    QVERIFY(session->getNewObject(PDFObjectReference(1, 0)).isNull());
    QVERIFY(session->getNewObject(PDFObjectReference(2, 0)).isNull());
    QVERIFY(session->getNewObject(PDFObjectReference(objectCount, 0)).isNull());

    // The snapshot carries them, also when no container is changed (isEmpty ignores them)
    const PDFContentEditSnapshotPointer snapshot = session->createSnapshot();
    QCOMPARE(snapshot->newObjects.size(), size_t(2));
    QCOMPARE(snapshot->newObjects.at(first), PDFObject::createInteger(7));
    QVERIFY(snapshot->isEmpty());
}

void ContentCommandsTest::sessionEditorState()
{
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f 20 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);
    QVERIFY(first.isValid() && second.isValid());

    QList<PDFContentNodeReference> changed;
    connect(session, &PDFContentEditSession::editorStateChanged, this, [&changed](const PDFContentNodeReference& node) { changed.push_back(node); });

    // The lock and the "hidden" flag are the state of the editor: no command, no undo step, no
    // work (RE-16)
    QVERIFY(!session->isNodeLocked(first));
    QVERIFY(!session->isNodeHidden(first));
    session->setNodeLocked(first, true);
    session->setNodeHidden(second, true);
    QVERIFY(session->isNodeLocked(first));
    QVERIFY(!session->isNodeLocked(second));
    QVERIFY(!session->isNodeHidden(first));
    QVERIFY(session->isNodeHidden(second));
    QCOMPARE(changed, QList<PDFContentNodeReference>({ first, second }));

    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));
    QVERIFY(session->getUndoStack()->isClean());
    QVERIFY(session->getDirtyPages().empty());
    QVERIFY(session->createSnapshot()->isEmpty());
    QVERIFY(session->getContainer(key)->isUnchanged());

    // Setting the same value again tells nothing; a node, which is not valid, is ignored
    session->setNodeLocked(first, true);
    session->setNodeHidden(first, false);
    session->setNodeLocked(PDFContentNodeReference(), true);
    QCOMPARE(changed.size(), qsizetype(2));
    QVERIFY(!session->isNodeLocked(PDFContentNodeReference()));

    session->setNodeLocked(first, false);
    session->setNodeHidden(second, false);
    QVERIFY(!session->isNodeLocked(first));
    QVERIFY(!session->isNodeHidden(second));
    QCOMPARE(changed.size(), qsizetype(4));

    // The temporary names of a replayed script: a name used again gets the new node
    QVERIFY(!session->getScriptName(QStringLiteral("t1")).isValid());
    session->setScriptName(QStringLiteral("t1"), first);
    session->setScriptName(QStringLiteral("t2"), second);
    QCOMPARE(session->getScriptName(QStringLiteral("t1")), first);
    session->setScriptName(QStringLiteral("t1"), second);
    QCOMPARE(session->getScriptName(QStringLiteral("t1")), second);
    session->clearScriptNames();
    QVERIFY(!session->getScriptName(QStringLiteral("t1")).isValid());
    QVERIFY(!session->getScriptName(QStringLiteral("t2")).isValid());
}

void ContentCommandsTest::sessionExecuteGuards()
{
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f 20 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer original = session->getContainer(key);

    int changeCount = 0;
    bool isExecutingInSlot = false;
    bool isExecutedInSlot = true;
    QString slotErrorText;
    bool isUndoneInSlot = true;
    connect(session, &PDFContentEditSession::modelChanged, this, [&](const PDFContentChange& change)
    {
        Q_UNUSED(change);
        ++changeCount;

        if (changeCount > 3)
        {
            // Only without the guard of the session: a command executed here would tell its
            // change to this slot again, and so on
            return;
        }

        // The model is in its new state, but no command runs while the change is being told
        isExecutingInSlot = session->isExecuting();
        isExecutedInSlot = session->execute(Factory::createDelete({ fixture.node(key, Kind::Path, 0) }), &slotErrorText);
        isUndoneInSlot = session->undo().has_value();
    });

    // Nothing to execute
    QString errorText;
    QVERIFY(!session->execute(nullptr, &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!session->isExecuting());

    // A refused command: the reason, no step, no signal
    std::shared_ptr<ContentCommandsTestCommand> refused = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Refused"));
    refused->isRefused = true;
    errorText.clear();
    QVERIFY(!session->execute(refused, &errorText));
    QCOMPARE(errorText, QStringLiteral("refused by the test"));
    QCOMPARE(refused->applyCount, 1);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));
    QCOMPARE(changeCount, 0);
    QVERIFY(!session->isExecuting());

    // A command inside of a command is refused, and so is undo
    bool isExecutingInCommand = false;
    bool isExecutedInCommand = true;
    bool isUndoneInCommand = true;
    QString innerErrorText;
    std::shared_ptr<ContentCommandsTestCommand> outer = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Outer"));
    outer->onApply = [&](PDFContentEditSession* current)
    {
        isExecutingInCommand = current->isExecuting();
        isExecutedInCommand = current->execute(std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Inner")), &innerErrorText);
        isUndoneInCommand = current->undo().has_value();
    };

    QVERIFY(session->execute(outer, &errorText));
    QVERIFY(isExecutingInCommand);
    QVERIFY(!isExecutedInCommand);
    QVERIFY(!innerErrorText.isEmpty());
    QVERIFY(!isUndoneInCommand);

    // ... and inside of the emission of modelChanged (the slot above tried both)
    QCOMPARE(changeCount, 1);
    QVERIFY(isExecutingInSlot);
    QVERIFY(!isExecutedInSlot);
    QVERIFY(!slotErrorText.isEmpty());
    QVERIFY(!isUndoneInSlot);
    QVERIFY(!session->isExecuting());

    // Only the outer command is a step; the model is as it was
    QCOMPARE(session->getUndoStack()->getCount(), size_t(1));
    QCOMPARE(session->getUndoStack()->getUndoText(), QStringLiteral("Outer"));
    QCOMPARE(session->getContainer(key), original);
}

void ContentCommandsTest::sessionUndoRedoOfSteps()
{
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();

    int changeCount = 0;
    connect(session, &PDFContentEditSession::modelChanged, this, [&changeCount](const PDFContentChange& change)
    {
        ++changeCount;
        QVERIFY(change.isStructureChanged);     // The change of the test command, also for a macro
    });

    // Nothing to undo or redo
    QVERIFY(!session->undo().has_value());
    QVERIFY(!session->redo().has_value());

    std::vector<std::shared_ptr<ContentCommandsTestCommand>> commands;
    for (int i = 0; i < 3; ++i)
    {
        commands.push_back(std::make_shared<ContentCommandsTestCommand>(QString::number(i)));
        commands.back()->pageIndex = 10 + i;
    }

    // A single command, then a macro of two
    QVERIFY(session->execute(commands[0], nullptr));
    session->getUndoStack()->beginMacro(QStringLiteral("Macro"));
    QVERIFY(session->execute(commands[1], nullptr));
    QVERIFY(session->execute(commands[2], nullptr));

    // An open macro cannot be undone
    QVERIFY(!session->undo().has_value());
    session->getUndoStack()->endMacro();
    QCOMPARE(session->getUndoStack()->getCount(), size_t(2));
    QCOMPARE(changeCount, 3);

    // Undo of the macro: its commands are reverted in the reverse order, one signal for the
    // step, the page is the page of its first command
    std::optional<PDFInteger> page = session->undo();
    QVERIFY(page.has_value());
    QCOMPARE(*page, PDFInteger(11));
    QCOMPARE(commands[1]->revertCount, 1);
    QCOMPARE(commands[2]->revertCount, 1);
    QCOMPARE(commands[0]->revertCount, 0);
    QCOMPARE(changeCount, 4);
    QCOMPARE(session->getUndoStack()->getPosition(), size_t(1));

    page = session->undo();
    QCOMPARE(*page, PDFInteger(10));
    QCOMPARE(commands[0]->revertCount, 1);
    QVERIFY(!session->undo().has_value());
    QCOMPARE(changeCount, 5);

    // Redo applies the commands again in their order
    page = session->redo();
    QCOMPARE(*page, PDFInteger(10));
    QCOMPARE(commands[0]->applyCount, 2);
    page = session->redo();
    QCOMPARE(*page, PDFInteger(11));
    QCOMPARE(commands[1]->applyCount, 2);
    QCOMPARE(commands[2]->applyCount, 2);
    QVERIFY(!session->redo().has_value());
    QCOMPARE(changeCount, 7);
    QCOMPARE(session->getUndoStack()->getPosition(), size_t(2));
}

void ContentCommandsTest::sessionDeletedFromSlots()
{
    // The session emits last: a slot of any of its signals may delete it (CLAUDE.md, lifetime
    // of Qt objects). The checks: no use of the freed session (ASan), and the functions return.
    const PDFDocument document = plainDocument("0 0 10 10 re f 20 0 10 10 re f");
    QObject context;

    // modelChanged of execute
    {
        ContentCommandsFixture* fixture = new ContentCommandsFixture(document);
        const Key key = fixture->page();
        QPointer<PDFContentEditSession> session = fixture->session();
        connect(session, &PDFContentEditSession::modelChanged, &context, [fixture]() { delete fixture; });
        QVERIFY(session->execute(Factory::createDelete({ fixture->node(key, Kind::Path, 0) }), nullptr));
        QVERIFY(session.isNull());
    }

    // modelChanged of undo and of redo
    for (const bool isRedo : { false, true })
    {
        ContentCommandsFixture* fixture = new ContentCommandsFixture(document);
        const Key key = fixture->page();
        QPointer<PDFContentEditSession> session = fixture->session();
        QVERIFY(session->execute(Factory::createDelete({ fixture->node(key, Kind::Path, 0) }), nullptr));
        if (isRedo)
        {
            QVERIFY(session->undo().has_value());
        }

        connect(session, &PDFContentEditSession::modelChanged, &context, [fixture]() { delete fixture; });
        const std::optional<PDFInteger> page = isRedo ? session->redo() : session->undo();
        QVERIFY(page.has_value());
        QCOMPARE(*page, PDFInteger(0));
        QVERIFY(session.isNull());
    }

    // selectionChangeRequested (a group is an added object: the views are asked to select it)
    {
        ContentCommandsFixture* fixture = new ContentCommandsFixture(document);
        const Key key = fixture->page();
        QPointer<PDFContentEditSession> session = fixture->session();
        int selectionCount = 0;
        connect(session, &PDFContentEditSession::selectionChangeRequested, &context, [fixture, &selectionCount](const QList<PDFContentNodeReference>& nodes)
        {
            selectionCount += int(nodes.size());
            delete fixture;
        });
        QVERIFY(session->execute(Factory::createGroup({ fixture->node(key, Kind::Path, 0) }), nullptr));
        QCOMPARE(selectionCount, 1);
        QVERIFY(session.isNull());
    }

    // editorStateChanged
    {
        ContentCommandsFixture* fixture = new ContentCommandsFixture(document);
        const Key key = fixture->page();
        QPointer<PDFContentEditSession> session = fixture->session();
        const PDFContentNodeReference node = fixture->node(key, Kind::Path, 0);
        connect(session, &PDFContentEditSession::editorStateChanged, &context, [fixture]() { delete fixture; });
        session->setNodeLocked(node, true);
        QVERIFY(session.isNull());
    }

    // containerLoaded and containerFailed (from the event loop)
    for (const bool isFailure : { false, true })
    {
        ContentCommandsFixture* fixture = new ContentCommandsFixture(document);
        QPointer<PDFContentEditSession> session = fixture->session();
        connect(session, &PDFContentEditSession::containerLoaded, &context, [fixture]() { delete fixture; });
        connect(session, &PDFContentEditSession::containerFailed, &context, [fixture]() { delete fixture; });
        session->requestContainer(isFailure ? Key::createForm(PDFObjectReference(9999, 0)) : session->getPageKey(0));
        QVERIFY(!session.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(session.isNull(), timeout(30000));
    }

    // structureLoaded
    {
        ContentCommandsFixture* fixture = new ContentCommandsFixture(document);
        QPointer<PDFContentEditSession> session = fixture->session();
        connect(session, &PDFContentEditSession::structureLoaded, &context, [fixture]() { delete fixture; });
        session->requestStructure();
        QVERIFY(!session.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(session.isNull(), timeout(30000));
    }
}

void ContentCommandsTest::sessionAsynchronousLoading()
{
    ContentCommandsFixture fixture(Documents::createFormDocument());
    PDFContentEditSession* session = fixture.session();
    const Key pageKey = session->getPageKey(0);

    QList<Key> loaded;
    QList<Key> failed;
    QStringList failures;
    connect(session, &PDFContentEditSession::containerLoaded, this, [&loaded](const Key& key) { loaded.push_back(key); });
    connect(session, &PDFContentEditSession::containerFailed, this, [&failed, &failures](const Key& key, const QString& errorText)
    {
        failed.push_back(key);
        failures.push_back(errorText);
    });

    // The answer comes from the event loop, never from inside of the call
    session->requestContainer(pageKey);
    session->requestContainer(pageKey);     // The same request again: one build, one answer
    QVERIFY(loaded.isEmpty());
    QVERIFY(session->getContainer(pageKey) == nullptr);

    // The page and its two forms, each once (the page draws /Fm1 twice)
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), qsizetype(3), timeout(30000));
    QCOMPARE(loaded.front(), pageKey);
    const PDFContentContainerPointer pageContainer = session->getContainer(pageKey);
    QVERIFY(pageContainer != nullptr);

    QSet<PDFInteger> formObjects;
    for (const PDFContentNodeId id : fixture.nodes(pageKey, Kind::FormUse))
    {
        const Key formKey = Key::createForm(pageContainer->getNode(id)->getData().xobject);
        QVERIFY(session->getContainer(formKey) != nullptr);
        QVERIFY(loaded.contains(formKey));
        formObjects.insert(formKey.reference.objectNumber);
    }
    QCOMPARE(formObjects.size(), qsizetype(2));

    // The content built by the worker is the content of the page
    QCOMPARE(PDFContentStreamWriter::write(*pageContainer).content, Documents::formPageContent());

    // A container, which is loaded already: the answer still comes from the event loop, and the
    // container stays the same object
    loaded.clear();
    session->requestContainer(pageKey);
    QVERIFY(loaded.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), qsizetype(1), timeout(30000));
    QCOMPARE(session->getContainer(pageKey), pageContainer);

    // A key, which names nothing
    const Key missing = Key::createPage(0, PDFObjectReference(9999, 0));
    session->requestContainer(missing);
    QVERIFY(failed.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), qsizetype(1), timeout(30000));
    QCOMPARE(failed.front(), missing);
    QVERIFY(!failures.front().isEmpty());
    QVERIFY(session->getContainer(missing) == nullptr);

    // A container loaded in this thread while the worker builds it stays (it may hold work);
    // the request is still answered
    {
        ContentCommandsFixture other(plainDocument("0 0 10 10 re f 20 0 10 10 re f"));
        const Key key = other.session()->getPageKey(0);
        int answerCount = 0;
        connect(other.session(), &PDFContentEditSession::containerLoaded, this, [&answerCount]() { ++answerCount; });

        other.session()->requestContainer(key);
        QVERIFY(other.page().isValid());
        QVERIFY(other.execute(Factory::createDelete({ other.node(key, Kind::Path, 0) })));
        const PDFContentContainerPointer changed = other.session()->getContainer(key);

        QTRY_COMPARE_WITH_TIMEOUT(answerCount, 1, timeout(30000));
        QCOMPARE(other.session()->getContainer(key), changed);
        QCOMPARE(other.written(key), QByteArray("20 0 10 10 re f"));
    }

    // A session destroyed while its workers run: the builds are canceled and not waited for,
    // nothing comes back
    for (int i = 0; i < 20; ++i)
    {
        ContentCommandsFixture other(Documents::createFormDocument());
        other.session()->requestContainer(other.session()->getPageKey(0));
        other.session()->requestStructure();
    }
    QVERIFY(QThreadPool::globalInstance()->waitForDone(timeout(30000)));
    QCoreApplication::processEvents();
}

void ContentCommandsTest::sessionStructure()
{
    // A tagged document: the model is read once, by a worker or in this thread
    {
        ContentCommandsFixture fixture(Documents::createTaggedDocument());
        PDFContentEditSession* session = fixture.session();
        QVERIFY(session->getStructure() == nullptr);

        int loadedCount = 0;
        connect(session, &PDFContentEditSession::structureLoaded, this, [&loadedCount]() { ++loadedCount; });
        session->requestStructure();
        session->requestStructure();
        QCOMPARE(loadedCount, 0);
        QTRY_COMPARE_WITH_TIMEOUT(loadedCount, 1, timeout(30000));

        const PDFContentStructureModelPointer model = session->getStructure();
        QVERIFY(model != nullptr);
        QVERIFY(!model->isEmpty());
        QVERIFY(!model->isModified());

        // The page has /StructParents 0; the identifier 3 is the element Figure, 2 is null
        QVERIFY(model->getElementOfMarkedContent(0, 3) >= 0);
        QCOMPARE(model->getElement(model->getElementOfMarkedContent(0, 3))->type, QByteArray("Figure"));
        QCOMPARE(model->getElementOfMarkedContent(0, 2), PDFContentStructureElementId(-1));

        // Read already: the same model stays, the request is still answered from the event loop
        QVERIFY(session->loadStructure(nullptr));
        QCOMPARE(session->getStructure(), model);
        session->requestStructure();
        QCOMPARE(loadedCount, 1);
        QTRY_COMPARE_WITH_TIMEOUT(loadedCount, 2, timeout(30000));
        QCOMPARE(session->getStructure(), model);

        // A model, which nothing changed, is not in a snapshot
        QVERIFY(session->createSnapshot()->structure == nullptr);
    }

    // A document without a structure tree: an empty model, in this thread without an event loop
    {
        ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
        QVERIFY(fixture.session()->loadStructure(nullptr));
        QVERIFY(fixture.session()->getStructure() != nullptr);
        QVERIFY(fixture.session()->getStructure()->isEmpty());
    }
}

void ContentCommandsTest::sessionTwoPagesFromPlainCode()
{
    // A tool without a canvas (zadani.md 12.7): a session created from plain code, commands on
    // two different pages one after the other, only the touched pages loaded, no event loop.
    // The third page is never loaded.
    ContentCommandsFixture fixture(pagesDocument({ "0 0 10 10 re f 20 0 10 10 re f", "1 0 0 rg 0 0 30 30 re f", "0 0 5 5 re f" }));
    PDFContentEditSession* session = fixture.session();

    int signalCount = 0;
    connect(session, &PDFContentEditSession::containerLoaded, this, [&signalCount]() { ++signalCount; });

    // The first page
    const Key first = fixture.page(0);
    QVERIFY(first.isValid());
    QVERIFY(fixture.execute(Factory::createDelete({ fixture.node(first, Kind::Path, 0) })));
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0 }));

    // The second page, loaded only now
    const Key second = session->getPageKey(1);
    QVERIFY(session->getContainer(second) == nullptr);
    QVERIFY(fixture.page(1).isValid());
    QVERIFY(fixture.execute(Factory::createTransform({ fixture.node(second, Kind::Path, 0) }, matrix(1, 0, 0, 1, 7, 9))));
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0, 1 }));
    QVERIFY(session->getContainer(session->getPageKey(2)) == nullptr);

    // The page of an undo step is the page of its command
    QCOMPARE(session->getUndoStack()->getCount(), size_t(2));

    // The snapshot: the changed containers only, immutable, with the revision of the session
    const PDFContentEditSnapshotPointer snapshot = session->createSnapshot();
    QCOMPARE(snapshot->revision, quint64(1));
    QVERIFY(!snapshot->isEmpty());
    QCOMPARE(snapshot->containers.size(), size_t(2));
    QCOMPARE(snapshot->dirtyPages, std::vector<PDFInteger>({ 0, 1 }));
    QVERIFY(snapshot->structure == nullptr);
    QVERIFY(!snapshot->containers.at(first)->getFingerprint().isEmpty());
    QCOMPARE(snapshot->containers.at(first)->getFingerprint(), session->getContainer(first)->getFingerprint());

    // Undo of the second page: it leaves the work; the old snapshot is not touched
    std::optional<PDFInteger> page = session->undo();
    QVERIFY(page.has_value());
    QCOMPARE(*page, PDFInteger(1));
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0 }));
    const PDFContentEditSnapshotPointer later = session->createSnapshot();
    QCOMPARE(later->revision, quint64(2));
    QCOMPARE(later->containers.size(), size_t(1));
    QCOMPARE(later->dirtyPages, std::vector<PDFInteger>({ 0 }));
    QCOMPARE(snapshot->containers.size(), size_t(2));

    page = session->undo();
    QCOMPARE(*page, PDFInteger(0));
    QVERIFY(session->getDirtyPages().empty());
    QVERIFY(session->createSnapshot()->isEmpty());

    // No event loop ran and the synchronous way emits no loading signal
    QCOMPARE(signalCount, 0);

    // The snapshot outlives the session: the writer reads it alone
    QVERIFY(session->redo().has_value());
    QVERIFY(session->redo().has_value());
    ContentCommandsFixture* temporary = new ContentCommandsFixture(pagesDocument({ "0 0 10 10 re f 20 0 10 10 re f", "1 0 0 rg 0 0 30 30 re f" }));
    QVERIFY(temporary->page(0).isValid() && temporary->page(1).isValid());
    QVERIFY(temporary->execute(Factory::createDelete({ temporary->node(temporary->session()->getPageKey(0), Kind::Path, 0) })));
    QVERIFY(temporary->execute(Factory::createTransform({ temporary->node(temporary->session()->getPageKey(1), Kind::Path, 0) }, matrix(1, 0, 0, 1, 7, 9))));
    const PDFContentEditSnapshotPointer orphan = temporary->session()->createSnapshot();
    delete temporary;

    QCOMPARE(orphan->containers.size(), size_t(2));
    std::vector<QByteArray> contents;
    for (const auto& item : orphan->containers)
    {
        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(*item.second);
        QVERIFY(result.isSuccess());
        contents.push_back(result.content.simplified());
    }

    // Page 1: the first square is gone. Page 2: the square moved by (7, 9) in a wrap of its own;
    // the state run in front of it is not touched.
    QCOMPARE(contents[0], QByteArray("20 0 10 10 re f"));
    QCOMPARE(contents[1], QByteArray("1 0 0 rg q 1 0 0 1 7 9 cm 0 0 30 30 re f Q"));
    QCOMPARE(fixture.written(first), contents[0]);
    QCOMPARE(fixture.written(second), contents[1]);
}

void ContentCommandsTest::sessionDirtyPageOfForm()
{
    // A changed form makes the loaded pages using it dirty, though their own content is not
    // changed (the document writer finds the pages, which are not loaded, itself)
    ContentCommandsFixture fixture(Documents::createFormDocument());
    PDFContentEditSession* session = fixture.session();
    const Key pageKey = fixture.page();
    const Key formKey = Key::createForm(fixture.get(fixture.node(pageKey, Kind::FormUse, 0))->getData().xobject);
    QString errorText;
    QVERIFY(session->loadContainer(formKey, nullptr, &errorText));
    QVERIFY(session->getDirtyPages().empty());

    // The form: "/GF gs 0 1 0 rg 5 5 20 10 re f". Its rectangle moves by (1, 2) in the space of
    // the form.
    QVERIFY2(fixture.execute(Factory::createTransform({ fixture.node(formKey, Kind::Path, 0) }, matrix(1, 0, 0, 1, 1, 2)), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(formKey), QByteArray("/GF gs 0 1 0 rg q 1 0 0 1 1 2 cm 5 5 20 10 re f Q"));

    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0 }));
    QVERIFY(session->getContainer(pageKey)->isUnchanged());

    const PDFContentEditSnapshotPointer snapshot = session->createSnapshot();
    QCOMPARE(snapshot->containers.size(), size_t(1));
    QVERIFY(snapshot->containers.count(formKey) == 1);
    QCOMPARE(snapshot->dirtyPages, std::vector<PDFInteger>({ 0 }));

    // The page of the command over a form is the page, which uses the form
    QCOMPARE(session->undo(), std::optional<PDFInteger>(0));
    QVERIFY(session->getDirtyPages().empty());
}

// -------------------------------------------------------------------------------------------------
//  Delete
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::deletePath()
{
    ContentCommandsFixture fixture(plainDocument("1 0 0 rg 0 0 10 10 re f 20 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);

    QList<PDFContentChange> changes;
    connect(session, &PDFContentEditSession::modelChanged, this, [&changes](const PDFContentChange& change) { changes.push_back(change); });

    const PDFContentCommandPointer command = Factory::createDelete({ first });
    QCOMPARE(command->getType(), QByteArray("delete"));
    QCOMPARE(command->getPageIndex(), PDFInteger(0));
    QVERIFY(!command->getText().isEmpty());

    QVERIFY(fixture.execute(command));

    // A path has no state of its own: nothing else changes
    QCOMPARE(fixture.written(key), QByteArray("1 0 0 rg 20 0 10 10 re f"));

    // The change: the container and the removed node; the other path is the same node as before
    QCOMPARE(changes.size(), qsizetype(1));
    QCOMPARE(changes[0].containers, std::vector<Key>({ key }));
    QCOMPARE(changes[0].removedNodes, std::vector<PDFContentNodeReference>({ first }));
    QVERIFY(changes[0].addedNodes.empty());
    QVERIFY(changes[0].changedNodes.empty());
    QVERIFY(!changes[0].isStructureChanged);
    QVERIFY(fixture.get(first) == nullptr);
    QCOMPARE(fixture.get(second), before->getNode(second.node));

    // The old version is not touched (undo keeps it)
    QCOMPARE(PDFContentStreamWriter::write(*before).content, QByteArray("1 0 0 rg 0 0 10 10 re f 20 0 10 10 re f"));
    QVERIFY(before->isUnchanged());
    QVERIFY(!session->getContainer(key)->isUnchanged());

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // The change of undo is the reverse one
    QVERIFY(session->undo().has_value());
    QCOMPARE(changes.back().addedNodes, std::vector<PDFContentNodeReference>({ first }));
    QVERIFY(changes.back().removedNodes.empty());
    QCOMPARE(PDFContentStreamWriter::write(*session->getContainer(key)).content, QByteArray("1 0 0 rg 0 0 10 10 re f 20 0 10 10 re f"));

    // Two nodes at once; a group goes with everything in it
    ContentCommandsFixture nested(plainDocument("0 0 1 1 re f q 1 0 0 rg 2 2 1 1 re f q 4 4 1 1 re f Q Q 6 6 1 1 re f 8 8 1 1 re f"));
    const Key nestedKey = nested.page();
    QVERIFY(nested.execute(Factory::createDelete({ nested.node(nestedKey, Kind::Group, 0), nested.node(nestedKey, Kind::Path, 4), nested.node(nestedKey, Kind::Path, 2) })));
    QCOMPARE(nested.written(nestedKey), QByteArray("0 0 1 1 re f 6 6 1 1 re f"));
}

void ContentCommandsTest::deleteKeepsLeak_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("index");
    QTest::addColumn<QByteArray>("expected");

    // R-3: whenever the leak of a node stops reaching the nodes behind it, a state run at the
    // place of its end sets it. The second text has no Tf of its own: it is written in the font
    // the first text has set (ISO 32000-2, 9.3.1: the text state lives outside of BT ... ET too).
    QTest::newRow("text with Tf") << QByteArray("BT /F1 12 Tf 10 20 Td (A) Tj ET BT 10 40 Td (B) Tj ET") << int(Kind::TextObject) << 0
                                  << QByteArray("/F1 12 Tf BT 10 40 Td (B) Tj ET");

    // The last text leaks too; nothing reads it, but the rule does not look ahead
    QTest::newRow("text without a leak") << QByteArray("/F1 12 Tf BT 10 20 Td (A) Tj ET BT 10 40 Td (B) Tj ET") << int(Kind::TextObject) << 0
                                         << QByteArray("/F1 12 Tf BT 10 40 Td (B) Tj ET");

    // BDC ... rg ... EMC: the marked content leaks the color of its state run
    QTest::newRow("marked content with rg") << QByteArray("/Span BMC 1 0 0 rg 0 0 10 10 re f EMC 20 0 10 10 re f") << int(Kind::MarkedContent) << 0
                                            << QByteArray("1 0 0 rg 20 0 10 10 re f");

    // Only the path inside: the wrap and its state run stay (no sequence of the structure)
    QTest::newRow("path inside of marked content") << QByteArray("/Span BMC 1 0 0 rg 0 0 10 10 re f EMC 20 0 10 10 re f") << int(Kind::Path) << 0
                                                   << QByteArray("/Span BMC 1 0 0 rg EMC 20 0 10 10 re f");

    // Several properties: the color of stroking, the line width, the dash, the text state. The
    // order of the operators of a state run is the order of the stream writer: colors, then
    // w J j M d ri i, then the text state.
    QTest::newRow("marked content with more") << QByteArray("/Span BMC 0 0 1 RG 3 w [ 2 1 ] 0 d 1 J 5 Tc 0 0 10 10 re S EMC 20 0 10 10 re S") << int(Kind::MarkedContent) << 0
                                              << QByteArray("0 0 1 RG 3 w 1 J [ 2 1 ] 0 d 5 Tc 20 0 10 10 re S");

    // cm outside of q ... Q: the matrix is a part of the leak. The run sets M with
    // M x CTM of the beginning = CTM of the end; the beginning is the identity here.
    QTest::newRow("marked content with cm") << QByteArray("/Span BMC 2 0 0 2 10 20 cm 0 0 10 10 re f EMC 20 0 10 10 re f") << int(Kind::MarkedContent) << 0
                                            << QByteArray("2 0 0 2 10 20 cm 20 0 10 10 re f");

    // A group keeps everything inside (q ... Q): nothing to keep
    QTest::newRow("group") << QByteArray("q 1 0 0 rg 0 0 10 10 re f Q 20 0 10 10 re f") << int(Kind::Group) << 0
                           << QByteArray("20 0 10 10 re f");

    // The clipping path is never set again: deleting the clip is the meaning of the action
    QTest::newRow("clipping path") << QByteArray("0 0 50 50 re W n 0 0 100 100 re f") << int(Kind::Path) << 0
                                   << QByteArray("0 0 100 100 re f");
}

void ContentCommandsTest::deleteKeepsLeak()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);
    QFETCH(int, index);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind), size_t(index));
    QVERIFY(node.isValid());

    // The node behind the deleted one, which must see the same state as before
    const PDFContentNodePointer parent = before->getParent(node.node);
    const PDFInteger nodeIndex = before->getIndexInParent(node.node);
    const PDFContentNodePointer follower = nodeIndex + 1 < PDFInteger(parent->getChildren().size()) ? parent->getChildren()[size_t(nodeIndex + 1)] : nullptr;

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createDelete({ node }), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    if (follower && Kind(kind) != Kind::Path)
    {
        // The follower is the very same node (nothing of it was touched), so its state is the
        // state the builder gave it
        QCOMPARE(fixture.get(PDFContentNodeReference{ key, follower->getId() }), follower);
    }

    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::deleteRefusals_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");

    // What is no object, or cannot be taken out alone
    QTest::newRow("root") << QByteArray("0 0 10 10 re f") << int(Kind::Root);
    QTest::newRow("state run") << QByteArray("1 0 0 rg 0 0 10 10 re f") << int(Kind::StateRun);
    QTest::newRow("text show") << QByteArray("BT /F1 12 Tf (A) Tj ET") << int(Kind::TextShow);
    QTest::newRow("opaque") << QByteArray("0 0 10 10 re f unknownoperator 20 0 10 10 re f") << int(Kind::Opaque);
}

void ContentCommandsTest::deleteRefusals()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind));
    QVERIFY(node.isValid());

    int changeCount = 0;
    connect(fixture.session(), &PDFContentEditSession::modelChanged, this, [&changeCount]() { ++changeCount; });

    // Refused with a reason; the model, the undo stack and the counter are as before
    const quint64 counter = fixture.session()->getNodeIdCounter();
    QString errorText;
    QVERIFY(!fixture.execute(Factory::createDelete({ fixture.node(key, Kind::Path, 0), node }), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    QCOMPARE(fixture.session()->getNodeIdCounter(), counter);
    QCOMPARE(changeCount, 0);

    // The same for a node, which does not exist, a reference, which is not valid, an empty
    // list, and a container, which is not loaded
    const PDFContentNodeReference missing{ key, PDFContentNodeId{ 999999 } };
    const PDFContentNodeReference foreign{ Key::createPage(3, PDFObjectReference(4242, 0)), PDFContentNodeId{ 2 } };
    for (const std::vector<PDFContentNodeReference>& nodes : { std::vector<PDFContentNodeReference>{ missing }, std::vector<PDFContentNodeReference>{ PDFContentNodeReference() },
                                                               std::vector<PDFContentNodeReference>{ }, std::vector<PDFContentNodeReference>{ foreign } })
    {
        errorText.clear();
        QVERIFY(!fixture.execute(Factory::createDelete(nodes), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(key), before);
    }
    QCOMPARE(changeCount, 0);

    // A page with a stream, which cannot be decoded, is read-only: shown, never changed
    ContentCommandsFixture broken(Documents::createBrokenContentsDocument("0 0 10 10 re f", "20 0 10 10 re f", &Documents::createNoResources));
    const Key brokenKey = broken.page();
    QVERIFY(brokenKey.isValid());
    QVERIFY(broken.session()->getContainer(brokenKey)->isReadOnly());
    const PDFContentNodeReference path = broken.node(brokenKey, Kind::Path, 0);
    if (path.isValid())
    {
        errorText.clear();
        QVERIFY(!broken.execute(Factory::createDelete({ path }), &errorText));
        QVERIFY(!errorText.isEmpty());
    }
    QVERIFY(broken.session()->getDirtyPages().empty());
    QVERIFY(broken.session()->createSnapshot()->isEmpty());
}

// -------------------------------------------------------------------------------------------------
//  Transform
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::transformComposes()
{
    ContentCommandsFixture fixture(plainDocument("1 0 0 rg 0 0 10 10 re f 20 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);

    const PDFContentCommandPointer command = Factory::createTransform({ second }, matrix(1, 0, 0, 1, 5, 7));
    QCOMPARE(command->getType(), QByteArray("transform"));
    QCOMPARE(command->getPageIndex(), PDFInteger(0));
    QVERIFY(fixture.execute(command));

    // R-1: the override is the wrap q, cm, the node, Q; the neighbor is not touched
    QCOMPARE(fixture.written(key), QByteArray("1 0 0 rg 0 0 10 10 re f q 1 0 0 1 5 7 cm 20 0 10 10 re f Q"));
    QCOMPARE(fixture.get(first), before->getNode(first.node));
    QCOMPARE(command->getChange().changedNodes, std::vector<PDFContentNodeReference>({ second }));
    QVERIFY(command->getChange().addedNodes.empty());

    // The node keeps the state of its place: the override is no part of it
    QCOMPARE(fixture.get(second)->getState(), before->getNode(second.node)->getState());
    QVERIFY(fixture.get(second)->getOverride() != nullptr);
    QCOMPARE(fixture.get(second)->getOverride()->transform, matrix(1, 0, 0, 1, 5, 7));

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // Repeated changes are one wrap. The square was moved by (5, 7), now it is scaled by (2, 3)
    // around the origin of the page: (x, y) -> (x + 5, y + 7) -> (2 x + 10, 3 y + 21), the matrix
    // [2 0 0 3 10 21]. The other order of the multiplication would give [2 0 0 3 5 7].
    session->getUndoStack()->setClean();    // Nothing is merged at the clean point: two steps
    QVERIFY(fixture.execute(Factory::createTransform({ second }, matrix(2, 0, 0, 3, 0, 0))));
    QCOMPARE(fixture.written(key), QByteArray("1 0 0 rg 0 0 10 10 re f q 2 0 0 3 10 21 cm 20 0 10 10 re f Q"));
    QCOMPARE(session->getUndoStack()->getCount(), size_t(2));

    QVERIFY(session->undo().has_value());
    QCOMPARE(fixture.written(key), QByteArray("1 0 0 rg 0 0 10 10 re f q 1 0 0 1 5 7 cm 20 0 10 10 re f Q"));
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
}

void ContentCommandsTest::transformUnderMatrix_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QTransform>("displacement");
    QTest::addColumn<QByteArray>("expected");

    // The matrix of the command is of the default space of the container; the wrap is written in
    // the space of the node. Inside of the wrap the matrix is T x CTM and the object must be at
    // CTM x D, so T = CTM x D x CTM^-1.
    //
    // CTM [2 0 0 2 10 20]: (x, y) -> (2 x + 10, 2 y + 20) -> by D (2 x + 16, 2 y + 28)
    // -> by CTM^-1 ((X - 10) / 2, (Y - 20) / 2) = (x + 3, y + 4): T = [1 0 0 1 3 4]
    QTest::newRow("scaled") << QByteArray("2 0 0 2 10 20 cm 0 0 10 10 re f") << matrix(1, 0, 0, 1, 6, 8)
                            << QByteArray("2 0 0 2 10 20 cm q 1 0 0 1 3 4 cm 0 0 10 10 re f Q");

    // CTM [0 1 -1 0 0 0] (a quarter turn): (x, y) -> (-y, x) -> by D (-y + 6, x + 8)
    // -> by CTM^-1 (X, Y) -> (Y, -X): (x + 8, y - 6): T = [1 0 0 1 8 -6].
    // CTM^-1 x D x CTM would give [1 0 0 1 -8 6].
    QTest::newRow("rotated") << QByteArray("0 1 -1 0 0 0 cm 0 0 10 10 re f") << matrix(1, 0, 0, 1, 6, 8)
                             << QByteArray("0 1 -1 0 0 0 cm q 1 0 0 1 8 -6 cm 0 0 10 10 re f Q");

    // A rotation of the page space by a quarter turn under the scaled matrix:
    // (x, y) -> (2 x + 10, 2 y + 20) -> by D (X, Y) -> (-Y, X): (-2 y - 20, 2 x + 10)
    // -> by CTM^-1: ((-2 y - 20 - 10) / 2, (2 x + 10 - 20) / 2) = (-y - 15, x - 5):
    // T = [0 1 -1 0 -15 -5]
    QTest::newRow("rotation under a scale") << QByteArray("2 0 0 2 10 20 cm 0 0 10 10 re f") << matrix(0, 1, -1, 0, 0, 0)
                                            << QByteArray("2 0 0 2 10 20 cm q 0 1 -1 0 -15 -5 cm 0 0 10 10 re f Q");

    // The matrix of a group q ... cm ... Q counts as well
    QTest::newRow("in a group") << QByteArray("q 2 0 0 2 10 20 cm 0 0 10 10 re f Q") << matrix(1, 0, 0, 1, 6, 8)
                                << QByteArray("q 2 0 0 2 10 20 cm q 1 0 0 1 3 4 cm 0 0 10 10 re f Q Q");
}

void ContentCommandsTest::transformUnderMatrix()
{
    QFETCH(QByteArray, content);
    QFETCH(QTransform, displacement);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(plainDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createTransform({ fixture.node(key, Kind::Path, 0) }, displacement), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::transformTextKeepsLeak()
{
    // A wrap closes the leak of the node inside (R-3): the font the first text has set would be
    // lost for the second one, so a state run behind the wrap sets it
    ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf 10 20 Td (A) Tj ET BT 10 40 Td (B) Tj ET"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::TextObject, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::TextObject, 1);

    const PDFContentCommandPointer command = Factory::createTransform({ first }, matrix(1, 0, 0, 1, 5, 0));
    QVERIFY(fixture.execute(command));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 0 cm BT /F1 12 Tf 10 20 Td (A) Tj ET Q /F1 12 Tf BT 10 40 Td (B) Tj ET"));

    // The second text is the same node: it sees the state it saw
    QCOMPARE(fixture.get(second), before->getNode(second.node));

    // The new state run is told as an added node; its identifier is of the session, and it
    // stands in the state the text began with and ends in the state the text ended with
    QCOMPARE(command->getChange().addedNodes.size(), size_t(1));
    const PDFContentNodePointer run = fixture.get(command->getChange().addedNodes.front());
    QVERIFY(run != nullptr);
    QCOMPARE(run->getKind(), Kind::StateRun);
    QCOMPARE(run->getId().value, PDFContentEditSession::FIRST_SESSION_NODE_ID);
    QCOMPARE(run->getState(), before->getNode(first.node)->getState());
    QCOMPARE(run->getLeak().endState, before->getNode(second.node)->getState());
    QCOMPARE(session->getContainer(key)->getIndexInParent(run->getId()), PDFInteger(1));

    // The wrapped text does not leak any more
    QVERIFY(fixture.get(first)->getLeak().isEmpty());
    QVERIFY(!before->getNode(first.node)->getLeak().isEmpty());

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // The second move changes the wrap; the state run is there already
    session->getUndoStack()->setClean();
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 0, 3))));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 3 cm BT /F1 12 Tf 10 20 Td (A) Tj ET Q /F1 12 Tf BT 10 40 Td (B) Tj ET"));
}

void ContentCommandsTest::transformClipNodes_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QByteArray>("expected");

    // RE-4: a node with a clipping effect has no wrap - the wrap would end the clip and the
    // nodes behind would lose it. A clipping path is written again with its points moved (re
    // stays re for a move).
    QTest::newRow("clipping path") << QByteArray("0 0 50 50 re W n 0 0 100 100 re f")
                                   << QByteArray("10 5 50 50 re W n 0 0 100 100 re f");

    // A path, which paints and clips: the painting goes into a wrap without W, the clip follows
    // with its points moved
    QTest::newRow("path which paints and clips") << QByteArray("0 0 50 50 re W f 0 0 100 100 re f")
                                                 << QByteArray("q 1 0 0 1 10 5 cm 0 0 50 50 re f Q 10 5 50 50 re W n 0 0 100 100 re f");
}

void ContentCommandsTest::transformClipNodes()
{
    QFETCH(QByteArray, content);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(plainDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference follower = fixture.node(key, Kind::Path, 1);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createTransform({ fixture.node(key, Kind::Path, 0) }, matrix(1, 0, 0, 1, 10, 5)), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);
    QCOMPARE(fixture.get(follower), before->getNode(follower.node));
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::transformGroup()
{
    // A group has no wrap of its own (R-1): the move goes to its leaves; state runs have no
    // geometry and stay
    ContentCommandsFixture fixture(plainDocument("q 1 0 0 rg 0 0 10 10 re f /Span BMC 20 0 10 10 re f EMC Q 40 0 10 10 re f"));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createTransform({ fixture.node(key, Kind::Group, 0) }, matrix(1, 0, 0, 1, 1, 2)), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 rg q 1 0 0 1 1 2 cm 0 0 10 10 re f Q /Span BMC q 1 0 0 1 1 2 cm 20 0 10 10 re f Q EMC Q 40 0 10 10 re f"));
    QVERIFY(isUndoRedoExact(fixture, key, before));

    // A node named with the group it is in moves once (the selection of the tree may hold
    // a group and an object inside of it)
    ContentCommandsFixture twice(plainDocument("q 0 0 10 10 re f Q"));
    const Key twiceKey = twice.page();
    QVERIFY(twice.execute(Factory::createTransform({ twice.node(twiceKey, Kind::Path, 0), twice.node(twiceKey, Kind::Group, 0) }, matrix(1, 0, 0, 1, 1, 2))));
    QCOMPARE(twice.written(twiceKey), QByteArray("q q 1 0 0 1 1 2 cm 0 0 10 10 re f Q Q"));
}

void ContentCommandsTest::transformRefusals_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QTransform>("displacement");

    const QTransform move = matrix(1, 0, 0, 1, 5, 5);

    // RE-4: a text with a rendering mode 4 to 7 adds to the clipping path at ET; it is neither
    // wrapped nor written again before stage E2
    QTest::newRow("text with Tr 7") << QByteArray("BT /F1 12 Tf 7 Tr (A) Tj ET 0 0 10 10 re f") << int(Kind::TextObject) << move;
    QTest::newRow("group with such a text") << QByteArray("q BT /F1 12 Tf 7 Tr (A) Tj ET Q 0 0 10 10 re f") << int(Kind::Group) << move;

    QTest::newRow("state run") << QByteArray("1 0 0 rg 0 0 10 10 re f") << int(Kind::StateRun) << move;
    QTest::newRow("text show") << QByteArray("BT /F1 12 Tf (A) Tj ET") << int(Kind::TextShow) << move;
    QTest::newRow("opaque") << QByteArray("0 0 10 10 re f unknownoperator") << int(Kind::Opaque) << move;
    QTest::newRow("group with an opaque part") << QByteArray("q 0 0 10 10 re f unknownoperator Q") << int(Kind::Group) << move;
    QTest::newRow("root") << QByteArray("0 0 10 10 re f") << int(Kind::Root) << move;

    // A singular or infinite matrix is an error of the command (zadani_overeni.md 6.2)
    QTest::newRow("singular matrix") << QByteArray("0 0 10 10 re f") << int(Kind::Path) << matrix(1, 2, 2, 4, 0, 0);
    QTest::newRow("zero matrix") << QByteArray("0 0 10 10 re f") << int(Kind::Path) << matrix(0, 0, 0, 0, 0, 0);
    QTest::newRow("infinite matrix") << QByteArray("0 0 10 10 re f") << int(Kind::Path) << matrix(1, 0, 0, 1, qInf(), 0);
    QTest::newRow("matrix with NaN") << QByteArray("0 0 10 10 re f") << int(Kind::Path) << matrix(qQNaN(), 0, 0, 1, 0, 0);
}

void ContentCommandsTest::transformRefusals()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);
    QFETCH(QTransform, displacement);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind));
    QVERIFY(node.isValid());

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createTransform({ node }, displacement), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    QCOMPARE(fixture.session()->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
}

void ContentCommandsTest::transformPattern()
{
    // R-5 (ISO 32000-2, 8.7.3.1): the matrix of a pattern maps to the default space of the
    // content, in which the pattern is a resource, and cm does not act on it. A moved object
    // would leave its pattern behind, so the wrap sets a copy of the pattern with
    // /Matrix' = /Matrix x D.
    //
    // The pattern /P1: /Matrix [2 0 0 2 3 4]. D = [1 0 0 1 30 40]:
    // (x, y) -> (2 x + 3, 2 y + 4) -> (2 x + 33, 2 y + 44): /Matrix' = [2 0 0 2 33 44]
    // (D x /Matrix would be [2 0 0 2 63 84]).
    PDFObjectReference patternReference;
    const PDFDocument document = Documents::createDocument("/Pattern cs /P1 scn 25 25 40 40 re f 0 0 5 5 re f", [&patternReference](PDFDocumentBuilder* builder)
    {
        const PDFObject pattern = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("Pattern") },
                                                                        { "PatternType", PDFObject::createInteger(1) },
                                                                        { "PaintType", PDFObject::createInteger(1) },
                                                                        { "TilingType", PDFObject::createInteger(1) },
                                                                        { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                        { "XStep", PDFObject::createInteger(10) },
                                                                        { "YStep", PDFObject::createInteger(10) },
                                                                        { "Matrix", Documents::createIntegerArrayObject({ 2, 0, 0, 2, 3, 4 }) },
                                                                        { "Resources", Documents::createDictionaryObject({ }) } }, "1 0 0 rg 0 0 5 5 re f");
        patternReference = pattern.getReference();

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "P1", pattern } }));
        return resources;
    });

    ContentCommandsFixture fixture(document);
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 30, 40)), &errorText), qPrintable(errorText));

    // The wrap sets the color again with the copy of the pattern: the first free name is /P2
    QCOMPARE(fixture.written(key), QByteArray("/Pattern cs /P1 scn q 1 0 0 1 30 40 cm /Pattern cs /P2 scn 25 25 40 40 re f Q 0 0 5 5 re f"));

    // The copy is a new object of the session under a provisional reference; the resources of
    // the written content name it, the original entry stays (R-4)
    const PDFContentResources resources = fixture.resources(key);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::Pattern, "P1"), PDFObject::createReference(patternReference));
    const PDFObject copyReference = resources.getObject(PDFContentResourceCategory::Pattern, "P2");
    QVERIFY(copyReference.isReference());
    QCOMPARE(copyReference.getReference(), PDFObjectReference(1, PDFContentEditSession::NEW_OBJECT_GENERATION));

    const PDFObject copy = session->getNewObject(copyReference.getReference());
    const PDFObject& original = fixture.document()->getObjectByReference(patternReference);
    QVERIFY(copy.isStream() && original.isStream());
    QCOMPARE(copy.getStream()->getDictionary()->get("Matrix"), Documents::createNumberArrayObject({ 2, 0, 0, 2, 33, 44 }));

    // Everything else of the pattern is as it was: the keys, the other values and the data
    QCOMPARE(keys(PDFObject::createDictionary(PDFDictionaryBuilder(*copy.getStream()->getDictionary()))), keys(PDFObject::createDictionary(PDFDictionaryBuilder(*original.getStream()->getDictionary()))));
    for (const char* name : { "Type", "PatternType", "PaintType", "TilingType", "BBox", "XStep", "YStep", "Resources", "Length" })
    {
        QCOMPARE(copy.getStream()->getDictionary()->get(name), original.getStream()->getDictionary()->get(name));
    }
    QCOMPARE(*copy.getStream()->getContent(), QByteArray("1 0 0 rg 0 0 5 5 re f"));
    QCOMPARE(original.getStream()->getDictionary()->get("Matrix"), Documents::createIntegerArrayObject({ 2, 0, 0, 2, 3, 4 }));

    // The second square is not touched: it keeps the original pattern at its place
    QCOMPARE(fixture.get(second), before->getNode(second.node));
    QVERIFY(isUndoRedoExact(fixture, key, before));

    // A second move: another copy from the first one, /Matrix x D1 x D2 with D2 = [0 1 -1 0 0 0]:
    // (2 x + 33, 2 y + 44) -> (-2 y - 44, 2 x + 33): [0 2 -2 0 -44 33]
    session->getUndoStack()->setClean();
    QVERIFY2(fixture.execute(Factory::createTransform({ first }, matrix(0, 1, -1, 0, 0, 0)), &errorText), qPrintable(errorText));
    const PDFObject secondCopyReference = fixture.get(first)->getOverride()->properties.at(Property::FillColor).color.patternObject;
    QVERIFY(secondCopyReference.isReference());
    QVERIFY(secondCopyReference != copyReference);
    QCOMPARE(session->getNewObject(secondCopyReference.getReference()).getStream()->getDictionary()->get("Matrix"), Documents::createNumberArrayObject({ 0, 2, -2, 0, -44, 33 }));

    // The first copy is not changed (undo needs it), and the snapshot carries both
    QCOMPARE(session->getNewObject(copyReference.getReference()).getStream()->getDictionary()->get("Matrix"), Documents::createNumberArrayObject({ 2, 0, 0, 2, 33, 44 }));
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(2));

    // An object, which is not painted by a pattern, gets no new object
    ContentCommandsFixture other(plainDocument("1 0 0 rg 0 0 5 5 re f"));
    const Key otherKey = other.page();
    QVERIFY(other.execute(Factory::createTransform({ other.node(otherKey, Kind::Path, 0) }, matrix(1, 0, 0, 1, 30, 40))));
    QVERIFY(other.session()->createSnapshot()->newObjects.empty());
}

void ContentCommandsTest::transformSoftMask()
{
    // R-6 (ISO 32000-2, 11.6.5.1): the space of a soft mask is the matrix of its group x the CTM
    // at the gs, which set it. The example of the plan: "/GS1 gs q 200 0 0 100 50 60 cm /Im1 Do Q".
    // The image is moved by D = [1 0 0 1 10 20]; the mask has to move with it.
    PDFObjectReference maskReference;
    const PDFDocument document = Documents::createDocument("/GS1 gs q 200 0 0 100 50 60 cm /Im1 Do Q", [&maskReference](PDFDocumentBuilder* builder)
    {
        const PDFObject group = Documents::createDictionaryObject({ { "S", PDFObject::createName("Transparency") }, { "CS", PDFObject::createName("DeviceGray") } });
        const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                     { "Subtype", PDFObject::createName("Form") },
                                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                                     { "Group", group } }, "0.5 g 0 0 100 200 re f");
        const PDFObject mask = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Mask") },
                                                                                                 { "S", PDFObject::createName("Luminosity") },
                                                                                                 { "G", form } }));
        maskReference = mask.getReference();

        const PDFObject image = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                      { "Subtype", PDFObject::createName("Image") },
                                                                      { "Width", PDFObject::createInteger(1) },
                                                                      { "Height", PDFObject::createInteger(1) },
                                                                      { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                                      { "BitsPerComponent", PDFObject::createInteger(8) } }, QByteArray(1, '\x40'));

        PDFDictionaryBuilder resources;
        // /GS1 sets the mask together with another key: a dictionary, which sets the mask
        // again, must not be /GS1 itself
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") },
                                                                                                                                                 { "SMask", mask },
                                                                                                                                                 { "SA", PDFObject::createBool(true) } }) } }));
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Im1", image } }));
        return resources;
    });

    ContentCommandsFixture fixture(document);
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference image = fixture.node(key, Kind::Image, 0);
    QVERIFY(image.isValid());
    QVERIFY(fixture.get(image)->getState()->hasSoftMask());

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createTransform({ image }, matrix(1, 0, 0, 1, 10, 20)), &errorText), qPrintable(errorText));

    // The wrap of the image. CTM = [200 0 0 100 50 60]:
    //   T = CTM x D x CTM^-1: (x, y) -> (200 x + 50, 100 y + 60) -> (200 x + 60, 100 y + 80)
    //       -> ((X - 50) / 200, (Y - 60) / 100) = (x + 0.05, y + 0.2): T = [1 0 0 1 0.05 0.2]
    //   The mask was set under the identity, so it must be set again under the matrix D. The
    //   matrix in effect behind "T cm" is T x CTM = CTM x D; X x CTM x D = D gives
    //   X = CTM^-1 = [0.005 0 0 0.01 -0.25 -0.6], and behind the gs X^-1 = CTM puts the matrix back.
    // The mask is set by a dictionary with the single key /SMask: the first free name is /GS2.
    QCOMPARE(fixture.written(key), QByteArray("/GS1 gs q 200 0 0 100 50 60 cm q 1 0 0 1 0.05 0.2 cm 0.005 0 0 0.01 -0.25 -0.6 cm /GS2 gs 200 0 0 100 50 60 cm /Im1 Do Q Q"));

    // /GS2 has the single key /SMask and its value is the original mask dictionary by reference
    const PDFObject graphicState = fixture.resources(key).getObject(PDFContentResourceCategory::ExtGState, "GS2");
    QCOMPARE(keys(graphicState), QList<QByteArray>({ "SMask" }));
    QCOMPARE(graphicState.getDictionary()->get("SMask"), PDFObject::createReference(maskReference));

    QVERIFY(isUndoRedoExact(fixture, key, before));
}

// -------------------------------------------------------------------------------------------------
//  Set Appearance, Set Transparency
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::setAppearancePath()
{
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f 20 0 10 10 re B"));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);

    PDFContentStateValue dash;
    dash.kind = PDFContentStateValue::Kind::Dash;
    dash.dash.setDashArray({ 3, 1 });
    dash.dash.setDashOffset(2);

    PDFContentStateValue cap;
    cap.kind = PDFContentStateValue::Kind::Integer;
    cap.integer = 1;

    PDFContentStateValue join = cap;
    join.integer = 2;

    // R-1: the changed properties are set in the wrap, and only they. The order of the stream
    // writer: the colors (stroking first), then w J j M d.
    const PDFContentCommandPointer command = Factory::createSetAppearance({ second }, { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 0 }) },
                                                                                       { Property::StrokeColor, deviceColor("DeviceCMYK", { 0, 0, 0, 1 }) },
                                                                                       { Property::LineWidth, realValue(2.5) },
                                                                                       { Property::LineCap, cap },
                                                                                       { Property::LineJoin, join },
                                                                                       { Property::MiterLimit, realValue(4) },
                                                                                       { Property::DashPattern, dash } });
    QCOMPARE(command->getType(), QByteArray("set_appearance"));

    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("0 0 10 10 re f q 0 0 0 1 K 1 0 0 rg 2.5 w 1 J 2 j 4 M [ 3 1 ] 2 d 20 0 10 10 re B Q"));
    QCOMPARE(fixture.get(first), before->getNode(first.node));
    QVERIFY(isUndoRedoExact(fixture, key, before));

    // A later change of one property keeps the others of the wrap
    fixture.session()->getUndoStack()->setClean();
    QVERIFY(fixture.execute(Factory::createSetAppearance({ second }, { { Property::LineWidth, realValue(1) } })));
    QCOMPARE(fixture.written(key), QByteArray("0 0 10 10 re f q 0 0 0 1 K 1 0 0 rg 1 w 1 J 2 j 4 M [ 3 1 ] 2 d 20 0 10 10 re B Q"));

    // A group gives the change to its leaves (one command, one step)
    ContentCommandsFixture group(plainDocument("q 0 0 10 10 re f /Span BMC 20 0 10 10 re f EMC Q"));
    const Key groupKey = group.page();
    QVERIFY(group.execute(Factory::createSetAppearance({ group.node(groupKey, Kind::Group, 0) }, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } })));
    QCOMPARE(group.written(groupKey), QByteArray("q q 0.5 g 0 0 10 10 re f Q /Span BMC q 0.5 g 20 0 10 10 re f Q EMC Q"));
    QCOMPARE(group.session()->getUndoStack()->getCount(), size_t(1));
}

void ContentCommandsTest::setAppearanceText()
{
    // RE-5: a text sets its colors inside of BT ... ET, so the color of the wrap alone would
    // have no effect. The operators of the text, which set the overridden property, are
    // suppressed; the others stay.
    ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf 0 0 1 rg 0 1 0 RG 2 w (A) Tj 0.5 g (B) Tj ET 0 0 10 10 re f"));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference path = fixture.node(key, Kind::Path, 0);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::TextObject, 0) }, { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 0 }) } }), &errorText), qPrintable(errorText));

    // Both operators of the fill color of the text are gone, RG and w stay. The square behind
    // the text was filled by the last color of the text (the gray 0.5), stroked green with the
    // width 2 and written in /F1: the state run behind the wrap keeps all of it (R-3).
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 rg BT /F1 12 Tf 0 1 0 RG 2 w (A) Tj (B) Tj ET Q 0 1 0 RG 0.5 g 2 w /F1 12 Tf 0 0 10 10 re f"));
    QCOMPARE(fixture.get(path), before->getNode(path.node));

    // The suppressed operations are flags of the new version only: the old one writes itself
    // as it was
    QCOMPARE(PDFContentStreamWriter::write(*before).content, QByteArray("BT /F1 12 Tf 0 0 1 rg 0 1 0 RG 2 w (A) Tj 0.5 g (B) Tj ET 0 0 10 10 re f"));
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::setAppearanceRefusals_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("property");
    QTest::addColumn<PDFReal>("value");

    // A text with a rendering mode 4 to 7 cannot be wrapped (RE-4)
    QTest::newRow("text with Tr 5") << QByteArray("BT /F1 12 Tf 5 Tr (A) Tj ET") << int(Kind::TextObject) << int(Property::LineWidth) << PDFReal(2);

    // The appearance of a form is changed inside of the form; an image and a shading have none
    QTest::newRow("state run") << QByteArray("1 0 0 rg 0 0 10 10 re f") << int(Kind::StateRun) << int(Property::LineWidth) << PDFReal(2);
    QTest::newRow("clipping path") << QByteArray("0 0 10 10 re W n") << int(Kind::Path) << int(Property::LineWidth) << PDFReal(2);
    QTest::newRow("group without a leaf") << QByteArray("q 1 0 0 rg Q 0 0 10 10 re f") << int(Kind::Group) << int(Property::LineWidth) << PDFReal(2);

    // Values out of their tables (ISO 32000-2, 8.4.3)
    QTest::newRow("negative line width") << QByteArray("0 0 10 10 re S") << int(Kind::Path) << int(Property::LineWidth) << PDFReal(-1);
    QTest::newRow("infinite line width") << QByteArray("0 0 10 10 re S") << int(Kind::Path) << int(Property::LineWidth) << PDFReal(qInf());
    QTest::newRow("zero miter limit") << QByteArray("0 0 10 10 re S") << int(Kind::Path) << int(Property::MiterLimit) << PDFReal(0);

    // A property of another command
    QTest::newRow("alpha") << QByteArray("0 0 10 10 re S") << int(Kind::Path) << int(Property::AlphaFilling) << PDFReal(0.5);
    QTest::newRow("character spacing") << QByteArray("0 0 10 10 re S") << int(Kind::Path) << int(Property::CharacterSpacing) << PDFReal(0.5);
}

void ContentCommandsTest::setAppearanceRefusals()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);
    QFETCH(int, property);
    QFETCH(PDFReal, value);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind));
    QVERIFY(node.isValid());

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createSetAppearance({ node }, { { Property(property), realValue(value) } }), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));

    // Nothing to set, wrong kinds of values, colors, which do not fit their space
    PDFContentStateValue integerWidth;
    integerWidth.kind = PDFContentStateValue::Kind::Integer;
    integerWidth.integer = 2;

    PDFContentStateValue cap;
    cap.kind = PDFContentStateValue::Kind::Integer;
    cap.integer = 3;

    PDFContentStateValue dash;
    dash.kind = PDFContentStateValue::Kind::Dash;
    dash.dash.setDashArray({ 0, 0 });

    ContentCommandsFixture path(plainDocument("0 0 10 10 re B"));
    const Key pathKey = path.page();
    const PDFContentNodeReference pathNode = path.node(pathKey, Kind::Path);
    const std::vector<std::map<Property, PDFContentStateValue>> invalid = { { },
                                                                             { { Property::LineWidth, integerWidth } },
                                                                             { { Property::LineCap, cap } },
                                                                             { { Property::DashPattern, dash } },
                                                                             { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0 }) } },
                                                                             { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 2 }) } },
                                                                             { { Property::FillColor, deviceColor("DeviceGray", { qQNaN() }) } },
                                                                             { { Property::StrokeColor, deviceColor("DeviceLab", { 1, 0, 0 }) } } };
    for (const std::map<Property, PDFContentStateValue>& properties : invalid)
    {
        errorText.clear();
        QVERIFY(!path.execute(Factory::createSetAppearance({ pathNode }, properties), &errorText));
        QVERIFY(!errorText.isEmpty());
        QVERIFY(path.session()->getContainer(pathKey)->isUnchanged());
    }
}

void ContentCommandsTest::setTransparencyKeepsSoftMask()
{
    // RE-2 / R-6: the graphics state dictionary of the alpha has exactly the changed keys - gs
    // sets only the keys, which are present, so the soft mask of /GS1 stays as it is and where
    // it is. The mask is not set again.
    const PDFDocument document = Documents::createDocument("/GS1 gs q 200 0 0 100 50 60 cm /Im1 Do Q 0 0 10 10 re f", [](PDFDocumentBuilder* builder)
    {
        const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                     { "Subtype", PDFObject::createName("Form") },
                                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 200, 200 }) },
                                                                     { "Group", Documents::createDictionaryObject({ { "S", PDFObject::createName("Transparency") } }) } }, "0.5 g 0 0 100 200 re f");
        const PDFObject mask = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Mask") }, { "S", PDFObject::createName("Luminosity") }, { "G", form } }));
        const PDFObject image = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                      { "Subtype", PDFObject::createName("Image") },
                                                                      { "Width", PDFObject::createInteger(1) },
                                                                      { "Height", PDFObject::createInteger(1) },
                                                                      { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                                      { "BitsPerComponent", PDFObject::createInteger(8) } }, QByteArray(1, '\x40'));

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") }, { "SMask", mask } }) } }));
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Im1", image } }));
        return resources;
    });

    ContentCommandsFixture fixture(document);
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference image = fixture.node(key, Kind::Image, 0);

    const PDFContentCommandPointer command = Factory::createSetTransparency({ image }, 0.25, 0.5, QByteArray("Multiply"));
    QCOMPARE(command->getType(), QByteArray("set_transparency"));

    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));

    // One gs in the wrap, no matrices around it
    QCOMPARE(fixture.written(key), QByteArray("/GS1 gs q 200 0 0 100 50 60 cm q /GS2 gs /Im1 Do Q Q 0 0 10 10 re f"));

    // Exactly the three keys and /Type (ISO 32000-2, Table 57): no /SMask, nothing else
    const PDFContentResources resources = fixture.resources(key);
    const PDFObject graphicState = resources.getObject(PDFContentResourceCategory::ExtGState, "GS2");
    QCOMPARE(keys(graphicState), QList<QByteArray>({ "BM", "CA", "Type", "ca" }));
    QCOMPARE(graphicState.getDictionary()->get("Type"), PDFObject::createName("ExtGState"));
    QCOMPARE(graphicState.getDictionary()->get("CA"), PDFObject::createReal(0.25));
    QCOMPARE(graphicState.getDictionary()->get("ca"), PDFObject::createReal(0.5));
    QCOMPARE(graphicState.getDictionary()->get("BM"), PDFObject::createName("Multiply"));
    QCOMPARE(keys(resources.getObject(PDFContentResourceCategory::ExtGState, "GS1")), QList<QByteArray>({ "SMask", "Type" }));

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // Only one of the values: only its key
    fixture.session()->getUndoStack()->setClean();
    QVERIFY(fixture.execute(Factory::createSetTransparency({ fixture.node(key, Kind::Path, 0) }, std::nullopt, 0.75, std::nullopt)));
    QCOMPARE(fixture.written(key), QByteArray("/GS1 gs q 200 0 0 100 50 60 cm q /GS2 gs /Im1 Do Q Q q /GS3 gs 0 0 10 10 re f Q"));
    QCOMPARE(keys(fixture.resources(key).getObject(PDFContentResourceCategory::ExtGState, "GS3")), QList<QByteArray>({ "Type", "ca" }));
}

void ContentCommandsTest::setTransparencyValues_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");
    QTest::addColumn<PDFReal>("alpha");
    QTest::addColumn<QByteArray>("blendMode");
    QTest::addColumn<bool>("isAccepted");

    const QByteArray path("0 0 10 10 re f");

    // The alphas are numbers from 0 to 1, the blend mode one of the standard names (ISO 32000-2,
    // 11.3.5, Tables 134 and 135)
    QTest::newRow("alpha 0") << path << int(Kind::Path) << PDFReal(0) << QByteArray("Normal") << true;
    QTest::newRow("alpha 1") << path << int(Kind::Path) << PDFReal(1) << QByteArray("Luminosity") << true;
    QTest::newRow("alpha above 1") << path << int(Kind::Path) << PDFReal(1.5) << QByteArray("Normal") << false;
    QTest::newRow("negative alpha") << path << int(Kind::Path) << PDFReal(-0.1) << QByteArray("Normal") << false;
    QTest::newRow("alpha NaN") << path << int(Kind::Path) << PDFReal(qQNaN()) << QByteArray("Normal") << false;
    QTest::newRow("unknown blend mode") << path << int(Kind::Path) << PDFReal(0.5) << QByteArray("Shiny") << false;
    QTest::newRow("empty blend mode") << path << int(Kind::Path) << PDFReal(0.5) << QByteArray("") << false;

    // What paints nothing has no transparency
    QTest::newRow("state run") << QByteArray("1 0 0 rg 0 0 10 10 re f") << int(Kind::StateRun) << PDFReal(0.5) << QByteArray("Normal") << false;
    QTest::newRow("clipping path") << QByteArray("0 0 10 10 re W n") << int(Kind::Path) << PDFReal(0.5) << QByteArray("Normal") << false;
    QTest::newRow("text with Tr 4") << QByteArray("BT /F1 12 Tf 4 Tr (A) Tj ET") << int(Kind::TextObject) << PDFReal(0.5) << QByteArray("Normal") << false;
    QTest::newRow("text") << QByteArray("BT /F1 12 Tf (A) Tj ET") << int(Kind::TextObject) << PDFReal(0.5) << QByteArray("Normal") << true;
}

void ContentCommandsTest::setTransparencyValues()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);
    QFETCH(PDFReal, alpha);
    QFETCH(QByteArray, blendMode);
    QFETCH(bool, isAccepted);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind));
    QVERIFY(node.isValid());

    QString errorText;
    QCOMPARE(fixture.execute(Factory::createSetTransparency({ node }, alpha, alpha, blendMode), &errorText), isAccepted);
    QCOMPARE(errorText.isEmpty(), isAccepted);
    QCOMPARE(fixture.session()->getContainer(key) != before, isAccepted);

    if (isAccepted)
    {
        // The dictionary carries the values as they were given
        const PDFObject graphicState = fixture.resources(key).getObject(PDFContentResourceCategory::ExtGState, "GS1");
        QCOMPARE(graphicState.getDictionary()->get("CA"), PDFObject::createReal(alpha));
        QCOMPARE(graphicState.getDictionary()->get("ca"), PDFObject::createReal(alpha));
        QCOMPARE(graphicState.getDictionary()->get("BM"), PDFObject::createName(blendMode));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }
    else
    {
        // Nothing to set at all is refused too
        QVERIFY(!fixture.execute(Factory::createSetTransparency({ node }, std::nullopt, std::nullopt, std::nullopt), &errorText));
        QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    }
}

// -------------------------------------------------------------------------------------------------
//  Reorder
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::reorderAcrossStateChange_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("index");
    QTest::addColumn<int>("parentKind");    // The parent: the first node of this kind
    QTest::addColumn<int>("place");
    QTest::addColumn<QByteArray>("expected");

    const int root = int(Kind::Root);
    const int group = int(Kind::Group);

    // R-2: a node at another place of the content sees another state, so its wrap sets every
    // property, which differs and which the node reads.
    //
    // The first square was filled black (the initial color of a page: 0 in DeviceGray, ISO
    // 32000-2, 8.4.1); behind the state run the fill is red. The line width differs too, but
    // a path, which is only filled, does not read it.
    QTest::newRow("behind a state run") << QByteArray("0 0 10 10 re f 1 0 0 rg 2 w 20 0 10 10 re B") << int(Kind::Path) << 0 << root << -1
                                        << QByteArray("1 0 0 rg 2 w 20 0 10 10 re B q 0 g 0 0 10 10 re f Q");

    // The other way: the second square is filled and stroked, it takes its red fill and its
    // width 2 along (the stroking color is black at both places)
    QTest::newRow("in front of a state run") << QByteArray("0 0 10 10 re f 1 0 0 rg 2 w 20 0 10 10 re B") << int(Kind::Path) << 1 << root << 0
                                             << QByteArray("q 1 0 0 rg 2 w 20 0 10 10 re B Q 0 0 10 10 re f 1 0 0 rg 2 w");

    // A path does not read the text state: the font size differs at its new place (10 at the
    // old one, 12 behind the text), but there is no Tf in its wrap, and no wrap at all
    QTest::newRow("path behind a text") << QByteArray("/F1 10 Tf 0 0 10 10 re f BT /F1 12 Tf (A) Tj ET") << int(Kind::Path) << 0 << root << -1
                                        << QByteArray("/F1 10 Tf BT /F1 12 Tf (A) Tj ET 0 0 10 10 re f");

    // R-3 and R-2 together: the text leaves its font at its old place for the second text (a
    // state run) and is wrapped at the new place, though no property differs for it there
    // (it began without a font: there is nothing to go back to), so nothing leaks there
    QTest::newRow("text with Tf") << QByteArray("BT /F1 12 Tf (A) Tj ET BT (B) Tj ET 0 0 10 10 re f") << int(Kind::TextObject) << 0 << root << -1
                                  << QByteArray("/F1 12 Tf BT (B) Tj ET 0 0 10 10 re f q BT /F1 12 Tf (A) Tj ET Q");

    // A text written in a font set outside takes the font along
    QTest::newRow("text in front of its Tf") << QByteArray("/F1 12 Tf 3 Tc BT (A) Tj ET") << int(Kind::TextObject) << 0 << root << 0
                                             << QByteArray("q /F1 12 Tf 3 Tc BT (A) Tj ET Q /F1 12 Tf 3 Tc");

    // The matrix: M = CTM of the source x (CTM of the target)^-1, so that M x CTM of the target
    // is the matrix the node had. The source is [2 0 0 2 0 0], the target [0 1 -1 0 30 40]:
    //   (x, y) -> (2 x, 2 y) -> by the inverse of the target (X, Y) -> (Y - 40, 30 - X):
    //   (2 y - 40, 30 - 2 x), the matrix [0 -2 2 0 -40 30].
    // The other order, (CTM of the target)^-1 x CTM of the source, would give [0 -2 2 0 -80 60];
    // moves alone hide that mistake.
    // A marked content has no wrap of its own: at its new place a new group q ... Q stands around
    // it. The group begins with what the marked content saw (the black fill; the place is red
    // by the state run, which was left at the old place for the two squares), and it keeps the
    // red of the marked content from the nodes behind the new place.
    QTest::newRow("marked content with a state run") << QByteArray("/Span BMC 1 0 0 rg 0 0 10 10 re f EMC 20 0 10 10 re f 40 0 10 10 re f") << int(Kind::MarkedContent) << 0 << root << -1
                                                     << QByteArray("1 0 0 rg 20 0 10 10 re f 40 0 10 10 re f q 0 g /Span BMC 1 0 0 rg 0 0 10 10 re f EMC Q");

    // (The parent of this row is the second group.)
    QTest::newRow("between two matrices") << QByteArray("q 2 0 0 2 0 0 cm 0 0 10 10 re f Q q 0 1 -1 0 30 40 cm 0 0 5 5 re f Q") << int(Kind::Path) << 0 << group << -1
                                          << QByteArray("q 2 0 0 2 0 0 cm Q q 0 1 -1 0 30 40 cm 0 0 5 5 re f q 0 -2 2 0 -40 30 cm 0 0 10 10 re f Q Q");
}

void ContentCommandsTest::reorderAcrossStateChange()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);
    QFETCH(int, index);
    QFETCH(int, parentKind);
    QFETCH(int, place);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind), size_t(index));

    // The row "between two matrices" moves into the second group
    const bool isMatrixRow = Kind(parentKind) == Kind::Group;
    const PDFContentNodeReference parent = fixture.node(key, Kind(parentKind), isMatrixRow ? 1 : 0);

    const PDFContentCommandPointer command = Factory::createReorder({ node }, parent, place, Factory::ReorderMode::FollowParent);
    QCOMPARE(command->getType(), QByteArray("reorder"));

    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    // The node has the state of the place, where it is written: the state of its new neighbor
    // (the node in front of it, or the node behind it at the beginning of the parent)
    // (for a marked content in a new group: the group)
    const PDFContentContainerPointer after = fixture.session()->getContainer(key);
    PDFContentNodePointer moved = after->getNode(node.node);
    if (moved->getParentId() != parent.node)
    {
        moved = after->getParent(node.node);
        QCOMPARE(moved->getKind(), Kind::Group);
        QVERIFY(moved->getId().value >= PDFContentEditSession::FIRST_SESSION_NODE_ID);
    }
    const PDFContentNodePointer newParent = after->getParent(moved->getId());
    QCOMPARE(newParent->getId(), parent.node);
    const PDFInteger newIndex = after->getIndexInParent(moved->getId());
    if (newIndex + 1 < PDFInteger(newParent->getChildren().size()))
    {
        QCOMPARE(moved->getState(), newParent->getChildren()[size_t(newIndex + 1)]->getState());
    }
    else
    {
        const PDFContentNodePointer previous = newParent->getChildren()[size_t(newIndex - 1)];
        QCOMPARE(moved->getState(), previous->getLeak().endState ? previous->getLeak().endState : previous->getState());
    }

    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::reorderSoftMask()
{
    PDFObjectReference maskReference;

    // A node under a soft mask moved in front of the gs, which sets the mask: the wrap sets the
    // mask again, by a dictionary with the single key /SMask and the original mask by reference
    // (/GS1 is such a dictionary, so its name is used again)
    {
        ContentCommandsFixture fixture(softMaskDocument("/GS1 gs 0 0 10 10 re f 20 0 10 10 re f", &maskReference));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference node = fixture.node(key, Kind::Path, 0);
        QVERIFY(fixture.get(node)->getState()->hasSoftMask());

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ node }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q /GS1 gs 0 0 10 10 re f Q /GS1 gs 20 0 10 10 re f"));

        // The state of the node is the state of its new place (no mask there); the mask is a
        // value of its override, with the matrix of the space it was set in
        const PDFContentNodePointer moved = fixture.get(node);
        QVERIFY(!moved->getState()->hasSoftMask());
        const PDFContentStateValue& value = moved->getOverride()->properties.at(Property::SoftMask);
        QCOMPARE(value.kind, PDFContentStateValue::Kind::Object);
        QCOMPARE(value.object, PDFObject::createReference(maskReference));
        QCOMPARE(value.matrix, QTransform());
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The other way: the node had no mask and the place has one - the wrap takes the mask away
    // by << /SMask /None >> (R-6)
    {
        ContentCommandsFixture fixture(softMaskDocument("0 0 10 10 re f /GS1 gs 20 0 10 10 re f", &maskReference));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference node = fixture.node(key, Kind::Path, 0);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ node }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/GS1 gs 20 0 10 10 re f q /GS2 gs 0 0 10 10 re f Q"));

        const PDFObject graphicState = fixture.resources(key).getObject(PDFContentResourceCategory::ExtGState, "GS2");
        QCOMPARE(keys(graphicState), QList<QByteArray>({ "SMask" }));
        QCOMPARE(graphicState.getDictionary()->get("SMask"), PDFObject::createName("None"));
        QVERIFY(fixture.get(node)->getState()->hasSoftMask());
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The space of the mask is the CTM of the gs, which set it (the identity here), not the CTM
    // of the node. The square is under [2 0 0 2 0 0]; in front of the gs its wrap sets M =
    // [2 0 0 2 0 0] and then the mask: the matrix in effect is M x I, the mask needs the
    // identity, so X = [0.5 0 0 0.5 0 0] in front of the gs and X^-1 = [2 0 0 2 0 0] behind it.
    // (Setting the mask right behind "M cm" would scale the mask with the square.)
    {
        ContentCommandsFixture fixture(softMaskDocument("/GS1 gs q 2 0 0 2 0 0 cm 0 0 10 10 re f Q", &maskReference));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 2 0 0 2 0 0 cm 0.5 0 0 0.5 0 0 cm /GS1 gs 2 0 0 2 0 0 cm 0 0 10 10 re f Q /GS1 gs q 2 0 0 2 0 0 cm Q"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }
}

void ContentCommandsTest::reorderExtGStateByReference()
{
    // R-2: the properties without an operator of their own go through a new graphics state
    // dictionary with exactly their keys; the values are the objects of the source by
    // reference, never copies. /GS1 sets the transfer function and the halftone (and the line
    // width, which has an operator).
    PDFObject transferReference;
    PDFObject halftoneReference;
    const auto createResources = [&transferReference, &halftoneReference](PDFDocumentBuilder* builder)
    {
        transferReference = Documents::addObject(builder, Documents::createDictionaryObject({ { "FunctionType", PDFObject::createInteger(2) },
                                                                                             { "Domain", Documents::createIntegerArrayObject({ 0, 1 }) },
                                                                                             { "C0", Documents::createIntegerArrayObject({ 0 }) },
                                                                                             { "C1", Documents::createIntegerArrayObject({ 1 }) },
                                                                                             { "N", PDFObject::createInteger(1) } }));
        halftoneReference = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("Halftone") },
                                                                                             { "HalftoneType", PDFObject::createInteger(1) },
                                                                                             { "Frequency", PDFObject::createInteger(60) },
                                                                                             { "Angle", PDFObject::createInteger(45) },
                                                                                             { "SpotFunction", PDFObject::createName("Round") } }));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "GS1", Documents::createDictionaryObject({ { "Type", PDFObject::createName("ExtGState") },
                                                                                                                                                 { "TR", transferReference },
                                                                                                                                                 { "HT", halftoneReference },
                                                                                                                                                 { "LW", PDFObject::createInteger(3) } }) } }));
        return resources;
    };

    // The stroked square behind the gs goes in front of it
    {
        ContentCommandsFixture fixture(Documents::createDocument("0 0 10 10 re S /GS1 gs 20 0 10 10 re S", createResources));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 1) }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q /GS2 gs 3 w 20 0 10 10 re S Q 0 0 10 10 re S /GS1 gs"));

        const PDFObject graphicState = fixture.resources(key).getObject(PDFContentResourceCategory::ExtGState, "GS2");
        QCOMPARE(keys(graphicState), QList<QByteArray>({ "HT", "TR", "Type" }));
        QCOMPARE(graphicState.getDictionary()->get("TR"), transferReference);
        QCOMPARE(graphicState.getDictionary()->get("HT"), halftoneReference);
        QVERIFY(transferReference.isReference() && halftoneReference.isReference());
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The square in front of the gs goes behind it: it goes back to the defaults by their name
    // (ISO 32000-2, Table 57: /TR2 /Default, /HT /Default) and to the line width 1
    {
        ContentCommandsFixture fixture(Documents::createDocument("0 0 10 10 re S /GS1 gs 20 0 10 10 re S", createResources));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/GS1 gs 20 0 10 10 re S q /GS2 gs 1 w 0 0 10 10 re S Q"));

        const PDFObject graphicState = fixture.resources(key).getObject(PDFContentResourceCategory::ExtGState, "GS2");
        QCOMPARE(keys(graphicState), QList<QByteArray>({ "HT", "TR2", "Type" }));
        QCOMPARE(graphicState.getDictionary()->get("TR2"), PDFObject::createName("Default"));
        QCOMPARE(graphicState.getDictionary()->get("HT"), PDFObject::createName("Default"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }
}

void ContentCommandsTest::reorderInForm()
{
    // R-2 in the container of a form: a property the form does not set is inherited from the
    // place of its use - one value per use, so it is unknown. The form: a square stroked with
    // the inherited line width, "3 w", a square stroked with the width 3. The one page, which
    // draws the form, happens to set the width 3 too: the two widths are equal numbers, and
    // still not the same thing.
    const QByteArray formContent("0 0 10 10 re S 3 w 20 0 10 10 re S");

    auto load = [](ContentCommandsFixture& fixture) -> Key
    {
        const Key pageKey = fixture.page();
        const Key formKey = Key::createForm(fixture.get(fixture.node(pageKey, Kind::FormUse, 0))->getData().xobject);
        QString errorText;
        return fixture.session()->loadContainer(formKey, nullptr, &errorText) ? formKey : Key();
    };

    // The second square goes to the beginning, where the width is the inherited one: its width
    // 3 is set explicitly, whatever the width of a use happens to be
    {
        ContentCommandsFixture fixture(formDocument(formContent));
        const Key key = load(fixture);
        QVERIFY(key.isValid());
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 1) }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 3 w 20 0 10 10 re S Q 0 0 10 10 re S 3 w"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The first square goes behind "3 w": it would need "the width of the use", which no
    // operator can name. Refused with a reason.
    {
        ContentCommandsFixture fixture(formDocument(formContent));
        const Key key = load(fixture);
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY(!fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(key), before);
    }

    // The same content as a page: the state at its beginning is the known initial state
    {
        ContentCommandsFixture fixture(plainDocument(formContent));
        const Key key = fixture.page();
        QVERIFY(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, Factory::ReorderMode::FollowParent)));
        QCOMPARE(fixture.written(key), QByteArray("3 w 20 0 10 10 re S q 1 w 0 0 10 10 re S Q"));
    }
}

void ContentCommandsTest::reorderPlaces_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QList<int>>("paths");      // The moved paths, in the order of the command
    QTest::addColumn<int>("parentKind");
    QTest::addColumn<int>("place");
    QTest::addColumn<QByteArray>("expected");   // Empty: the command is refused

    const QByteArray three("0 0 1 1 re f 2 2 1 1 re f 4 4 1 1 re f");
    const int root = int(Kind::Root);

    // The index counts the children of the parent as they are before the command
    QTest::newRow("to the end by index") << three << QList<int>({ 0 }) << root << 3 << QByteArray("2 2 1 1 re f 4 4 1 1 re f 0 0 1 1 re f");
    QTest::newRow("to the end") << three << QList<int>({ 0 }) << root << -1 << QByteArray("2 2 1 1 re f 4 4 1 1 re f 0 0 1 1 re f");
    QTest::newRow("in front of the last") << three << QList<int>({ 0 }) << root << 2 << QByteArray("2 2 1 1 re f 0 0 1 1 re f 4 4 1 1 re f");
    QTest::newRow("to the beginning") << three << QList<int>({ 2 }) << root << 0 << QByteArray("4 4 1 1 re f 0 0 1 1 re f 2 2 1 1 re f");

    // Several nodes keep the order of the command, also when it is not the order of the tree
    QTest::newRow("two in another order") << three << QList<int>({ 2, 0 }) << root << 3 << QByteArray("2 2 1 1 re f 4 4 1 1 re f 0 0 1 1 re f");
    QTest::newRow("two to the beginning") << three << QList<int>({ 2, 1 }) << root << 0 << QByteArray("4 4 1 1 re f 2 2 1 1 re f 0 0 1 1 re f");

    // A place, where the node stands already, changes nothing: refused
    QTest::newRow("its own place") << three << QList<int>({ 0 }) << root << 0 << QByteArray();
    QTest::newRow("in front of its neighbor") << three << QList<int>({ 0 }) << root << 1 << QByteArray();
    QTest::newRow("the last to the end") << three << QList<int>({ 2 }) << root << -1 << QByteArray();
    QTest::newRow("the last two to the end") << three << QList<int>({ 1, 2 }) << root << -1 << QByteArray();
    QTest::newRow("the first two in front of the last") << three << QList<int>({ 0, 1 }) << root << 2 << QByteArray();

    // ... but the other order of the two is a change
    QTest::newRow("the last two swapped") << three << QList<int>({ 2, 1 }) << root << -1 << QByteArray("0 0 1 1 re f 4 4 1 1 re f 2 2 1 1 re f");

    // No such place
    QTest::newRow("index too large") << three << QList<int>({ 0 }) << root << 4 << QByteArray();
    QTest::newRow("negative index") << three << QList<int>({ 0 }) << root << -2 << QByteArray();

    // The tail of a content (bytes, which are no operations) stays last: "at the end" is in
    // front of it, and so is an index behind it
    const QByteArray tail("0 0 1 1 re f 2 2 1 1 re f (unfinished");
    QTest::newRow("in front of the tail") << tail << QList<int>({ 0 }) << root << -1 << QByteArray("2 2 1 1 re f 0 0 1 1 re f (unfinished");
    QTest::newRow("index behind the tail") << tail << QList<int>({ 0 }) << root << 3 << QByteArray("2 2 1 1 re f 0 0 1 1 re f (unfinished");

    // The rest of a content behind an inline image without an end is read as image data,
    // whatever is written there: "at the end" is in front of it
    const QByteArray openImage("0 0 1 1 re f 2 2 1 1 re f BI /W 1 /H 1 ID xx");
    QTest::newRow("in front of an open inline image") << openImage << QList<int>({ 0 }) << root << -1 << QByteArray("2 2 1 1 re f 0 0 1 1 re f BI /W 1 /H 1 ID xx");
    QTest::newRow("index behind an open inline image") << openImage << QList<int>({ 0 }) << root << 3 << QByteArray("2 2 1 1 re f 0 0 1 1 re f BI /W 1 /H 1 ID xx");

    // Into a group, and out of it; the clipping path is not a part of the state (R-2: an object
    // moved into a clipping group is clipped, moved out it loses the clip)
    const QByteArray clip("q 0 0 50 50 re W n 0 0 100 100 re f Q 20 0 10 10 re f");
    QTest::newRow("into a clipping group") << clip << QList<int>({ 2 }) << int(Kind::Group) << -1 << QByteArray("q 0 0 50 50 re W n 0 0 100 100 re f 20 0 10 10 re f Q");
    QTest::newRow("out of a clipping group") << clip << QList<int>({ 1 }) << root << -1 << QByteArray("q 0 0 50 50 re W n Q 20 0 10 10 re f 0 0 100 100 re f");

    // Into a path, into a text
    QTest::newRow("into a leaf") << three << QList<int>({ 0 }) << int(Kind::Path) << -1 << QByteArray();
    QTest::newRow("into a text") << QByteArray("0 0 1 1 re f BT /F1 12 Tf (A) Tj ET") << QList<int>({ 0 }) << int(Kind::TextObject) << -1 << QByteArray();
}

void ContentCommandsTest::reorderPlaces()
{
    QFETCH(QByteArray, content);
    QFETCH(QList<int>, paths);
    QFETCH(int, parentKind);
    QFETCH(int, place);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);

    std::vector<PDFContentNodeReference> nodes;
    for (const int path : paths)
    {
        nodes.push_back(fixture.node(key, Kind::Path, size_t(path)));
        QVERIFY(nodes.back().isValid());
    }

    // The parent "path" is the last path (never one of the moved ones in the rows)
    const PDFContentNodeReference parent = Kind(parentKind) == Kind::Path ? fixture.node(key, Kind::Path, 2) : fixture.node(key, Kind(parentKind));
    QVERIFY(parent.isValid());

    QString errorText;
    const bool isExecuted = fixture.execute(Factory::createReorder(nodes, parent, place, Factory::ReorderMode::FollowParent), &errorText);

    if (expected.isEmpty())
    {
        QVERIFY(!isExecuted);
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(key), before);
        QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
        QCOMPARE(fixture.session()->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
    }
    else
    {
        QVERIFY2(isExecuted, qPrintable(errorText));
        QCOMPARE(fixture.written(key), expected);
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }
}

void ContentCommandsTest::reorderRefusals()
{
    ContentCommandsFixture fixture(pagesDocument({ "q 0 0 1 1 re f q 2 2 1 1 re f Q Q 1 0 0 rg 4 4 1 1 re f BT /F1 12 Tf (A) Tj ET unknownoperator", "0 0 5 5 re f" }));
    const Key key = fixture.page(0);
    const Key otherKey = fixture.page(1);
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference root = fixture.root(key);
    const PDFContentNodeReference outerGroup = fixture.node(key, Kind::Group, 0);
    const PDFContentNodeReference innerGroup = fixture.node(key, Kind::Group, 1);

    const std::vector<std::pair<std::vector<PDFContentNodeReference>, PDFContentNodeReference>> refused =
    {
        { { outerGroup }, outerGroup },                                         // Into itself
        { { outerGroup }, innerGroup },                                         // Into its own subtree
        { { outerGroup, fixture.node(key, Kind::Path, 0) }, root },             // A node and a node inside of it
        { { fixture.node(key, Kind::StateRun, 0) }, outerGroup },               // A state run never moves
        { { fixture.node(key, Kind::TextShow, 0) }, root },                     // A part of a text
        { { fixture.node(key, Kind::Opaque, 0) }, outerGroup },                 // What cannot be read
        { { root }, outerGroup },                                               // The root
        { { fixture.node(otherKey, Kind::Path, 0) }, root },                    // A node of another content
        { { fixture.node(key, Kind::Path, 2) }, fixture.root(otherKey) },       // A parent of another content
        { { }, root },                                                          // Nothing
        { { PDFContentNodeReference{ key, PDFContentNodeId{ 424242 } } }, root }
    };

    for (const auto& item : refused)
    {
        QString errorText;
        QVERIFY(!fixture.execute(Factory::createReorder(item.first, item.second, -1, Factory::ReorderMode::FollowParent), &errorText));
        QVERIFY(!errorText.isEmpty());
        QVERIFY(!fixture.execute(Factory::createReorder(item.first, item.second, -1, Factory::ReorderMode::KeepLayerAndTag), &errorText));
    }

    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    QVERIFY(fixture.session()->getDirtyPages().empty());
}

// -------------------------------------------------------------------------------------------------
//  Group, Ungroup
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::group()
{
    // The first and the third square go into a new group at the place of the first one. The
    // state run between them stays, where it is, and still reaches the second square; the third
    // square was behind it, so its wrap sets the red fill (R-2).
    ContentCommandsFixture fixture(fontDocument("0 0 10 10 re f 1 0 0 rg 20 0 10 10 re f 40 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);
    const PDFContentNodeReference third = fixture.node(key, Kind::Path, 2);

    QList<PDFContentNodeReference> selection;
    connect(session, &PDFContentEditSession::selectionChangeRequested, this, [&selection](const QList<PDFContentNodeReference>& nodes) { selection = nodes; });

    // The order of the command plays no role: the group keeps the order of the tree
    const PDFContentCommandPointer command = Factory::createGroup({ third, first });
    QCOMPARE(command->getType(), QByteArray("group"));

    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("q 0 0 10 10 re f q 1 0 0 rg 40 0 10 10 re f Q Q 1 0 0 rg 20 0 10 10 re f"));

    // The group is the first added node of the change (a script names it by "as"), it has an
    // identifier of the session, and the views are asked to select it
    QVERIFY(!command->getChange().addedNodes.empty());
    const PDFContentNodeReference groupReference = command->getChange().addedNodes.front();
    const PDFContentNodePointer groupNode = fixture.get(groupReference);
    QVERIFY(groupNode != nullptr);
    QCOMPARE(groupNode->getKind(), Kind::Group);
    QCOMPARE(groupNode->getId().value, PDFContentEditSession::FIRST_SESSION_NODE_ID);
    QCOMPARE(groupNode->getChildren().size(), size_t(2));
    QCOMPARE(groupNode->getChildren()[0]->getId(), first.node);
    QCOMPARE(groupNode->getChildren()[1]->getId(), third.node);
    QCOMPARE(selection, QList<PDFContentNodeReference>({ groupReference }));

    // The states: the group and its children stand in the state of the place of the first
    // square; the second square is not touched
    QCOMPARE(groupNode->getState(), before->getNode(first.node)->getState());
    QCOMPARE(fixture.get(third)->getState(), before->getNode(first.node)->getState());
    QCOMPARE(fixture.get(second), before->getNode(second.node));

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // After undo the views are asked to select the objects of the step, which exist again
    selection.clear();
    QVERIFY(session->undo().has_value());
    QVERIFY(selection.contains(first) && selection.contains(third));
    QVERIFY(!selection.contains(groupReference));

    // R-3: a text taken into a group leaves its font behind for the next text, and does not
    // leak inside of the group either (it has a wrap of its own)
    ContentCommandsFixture text(fontDocument("BT /F1 12 Tf (A) Tj ET BT (B) Tj ET"));
    const Key textKey = text.page();
    QVERIFY(text.execute(Factory::createGroup({ text.node(textKey, Kind::TextObject, 0) })));
    QCOMPARE(text.written(textKey), QByteArray("q q BT /F1 12 Tf (A) Tj ET Q Q /F1 12 Tf BT (B) Tj ET"));
}

void ContentCommandsTest::groupRefusals_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QList<int>>("kinds");
    QTest::addColumn<QList<int>>("indices");

    QTest::newRow("nothing") << QByteArray("0 0 1 1 re f") << QList<int>() << QList<int>();
    QTest::newRow("two parents") << QByteArray("q 0 0 1 1 re f Q 2 2 1 1 re f") << QList<int>({ int(Kind::Path), int(Kind::Path) }) << QList<int>({ 0, 1 });
    QTest::newRow("state run") << QByteArray("1 0 0 rg 2 2 1 1 re f") << QList<int>({ int(Kind::StateRun), int(Kind::Path) }) << QList<int>({ 0, 0 });
    QTest::newRow("text show") << QByteArray("BT /F1 12 Tf (A) Tj ET") << QList<int>({ int(Kind::TextShow) }) << QList<int>({ 0 });
    QTest::newRow("opaque") << QByteArray("0 0 1 1 re f unknownoperator") << QList<int>({ int(Kind::Path), int(Kind::Opaque) }) << QList<int>({ 0, 0 });
    QTest::newRow("root") << QByteArray("0 0 1 1 re f") << QList<int>({ int(Kind::Root) }) << QList<int>({ 0 });
}

void ContentCommandsTest::groupRefusals()
{
    QFETCH(QByteArray, content);
    QFETCH(QList<int>, kinds);
    QFETCH(QList<int>, indices);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);

    std::vector<PDFContentNodeReference> nodes;
    for (qsizetype i = 0; i < kinds.size(); ++i)
    {
        nodes.push_back(fixture.node(key, Kind(kinds[i]), size_t(indices[i])));
        QVERIFY(nodes.back().isValid());
    }

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createGroup(nodes), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
}

void ContentCommandsTest::ungroup_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QByteArray>("expected");

    // R-3: the state runs of the group are deleted, and each child goes out with the difference
    // between the state it saw inside and the state outside (R-2). The first square was filled
    // red; the second one is only stroked, with the width 2 (the red fill is not read by it).
    // The square behind the group was never reached by the runs of the group: it is not touched.
    QTest::newRow("state runs") << QByteArray("q 1 0 0 rg 0 0 10 10 re f 2 w 20 0 10 10 re S Q 40 0 10 10 re f")
                                << QByteArray("q 1 0 0 rg 0 0 10 10 re f Q q 2 w 20 0 10 10 re S Q 40 0 10 10 re f");

    // The matrix of the group goes into the wrap of the child (M = CTM inside x identity^-1)
    QTest::newRow("matrix") << QByteArray("q 1 0 0 1 5 5 cm 0 0 10 10 re f Q 40 0 10 10 re f")
                            << QByteArray("q 1 0 0 1 5 5 cm 0 0 10 10 re f Q 40 0 10 10 re f");

    // Nothing differs: the children are written as they were
    QTest::newRow("plain") << QByteArray("1 0 0 rg q 0 0 10 10 re f 20 0 10 10 re f Q 40 0 10 10 re f")
                           << QByteArray("1 0 0 rg 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f");

    // A chain q q ... Q Q is one group
    QTest::newRow("chain") << QByteArray("q q 0 0 10 10 re f Q Q 40 0 10 10 re f")
                           << QByteArray("0 0 10 10 re f 40 0 10 10 re f");

    // An inner group keeps its content; the state of the outer group goes to its beginning
    QTest::newRow("inner group") << QByteArray("q 1 0 0 rg q 0 0 10 10 re f Q Q 40 0 10 10 re f")
                                 << QByteArray("q 1 0 0 rg 0 0 10 10 re f Q 40 0 10 10 re f");

    // A text, which leaks its font: outside it must not leak (it never reached the square)
    QTest::newRow("text") << QByteArray("q BT /F1 12 Tf (A) Tj ET Q 40 0 10 10 re f")
                          << QByteArray("q BT /F1 12 Tf (A) Tj ET Q 40 0 10 10 re f");
}

void ContentCommandsTest::ungroup()
{
    QFETCH(QByteArray, content);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference groupReference = fixture.node(key, Kind::Group, 0);
    const PDFContentNodePointer groupNode = fixture.get(groupReference);
    const std::vector<PDFContentNodeId> paths = fixture.nodes(key, Kind::Path);

    const PDFContentCommandPointer command = Factory::createUngroup(groupReference);
    QCOMPARE(command->getType(), QByteArray("ungroup"));

    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    // The group is gone from the tree; its former children stand in the state of the place of
    // the group, and the last square is the very same node
    const PDFContentContainerPointer after = fixture.session()->getContainer(key);
    QVERIFY(after->getNode(groupReference.node) == nullptr);
    for (const PDFContentNodePointer& child : groupNode->getChildren())
    {
        if (child->getKind() != Kind::StateRun)
        {
            const PDFContentNodePointer moved = after->getNode(child->getId());
            QVERIFY(moved != nullptr);
            QCOMPARE(moved->getParentId(), groupNode->getParentId());
            QCOMPARE(moved->getState(), groupNode->getState());
        }
        else
        {
            QVERIFY(after->getNode(child->getId()) == nullptr);
        }
    }
    QCOMPARE(after->getNode(paths.back()), before->getNode(paths.back()));

    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::ungroupRefusals_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");

    // A group, which carries a clip, is not removed: outside of q ... Q the clip would reach the
    // rest of the parent (release the clipping first)
    QTest::newRow("clipping path") << QByteArray("q 0 0 5 5 re W n 0 0 10 10 re f Q 20 0 1 1 re f") << int(Kind::Group);
    QTest::newRow("path which paints and clips") << QByteArray("q 0 0 5 5 re W f 0 0 10 10 re f Q") << int(Kind::Group);
    QTest::newRow("text with Tr 7") << QByteArray("q BT /F1 12 Tf 7 Tr (A) Tj ET 0 0 10 10 re f Q") << int(Kind::Group);
    QTest::newRow("marked content with a clip") << QByteArray("q /Span BMC 0 0 5 5 re W n EMC 0 0 10 10 re f Q") << int(Kind::Group);

    QTest::newRow("opaque part") << QByteArray("q 0 0 10 10 re f unknownoperator Q") << int(Kind::Group);
    QTest::newRow("not a group") << QByteArray("/Span BMC 0 0 10 10 re f EMC") << int(Kind::MarkedContent);
    QTest::newRow("path") << QByteArray("0 0 10 10 re f") << int(Kind::Path);

    // A group, which the content does not close
    QTest::newRow("open group") << QByteArray("0 0 1 1 re f q 0 0 10 10 re f") << int(Kind::Group);
}

void ContentCommandsTest::ungroupRefusals()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind));
    QVERIFY(node.isValid());

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createUngroup(node), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
}

// -------------------------------------------------------------------------------------------------
//  Copy
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::copyInContainer_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("place");
    QTest::addColumn<QTransform>("displacement");
    QTest::addColumn<QByteArray>("expected");

    const QByteArray square("1 0 0 rg 0 0 10 10 re f");

    // Duplicate: the copy behind the original, moved by (20, 0) in a wrap (R-1); the state of
    // the place is the state the original saw, so nothing else is set
    QTest::newRow("moved copy") << square << int(Kind::Path) << -1 << matrix(1, 0, 0, 1, 20, 0)
                                << QByteArray("1 0 0 rg 0 0 10 10 re f q 1 0 0 1 20 0 cm 0 0 10 10 re f Q");

    QTest::newRow("copy in place") << square << int(Kind::Path) << -1 << QTransform()
                                   << QByteArray("1 0 0 rg 0 0 10 10 re f 0 0 10 10 re f");

    // In front of the state run the copy sets the red fill itself (R-2)
    QTest::newRow("copy in front of the state run") << square << int(Kind::Path) << 0 << QTransform()
                                                    << QByteArray("q 1 0 0 rg 0 0 10 10 re f Q 1 0 0 rg 0 0 10 10 re f");

    // A group is copied with everything in it; its move goes to its leaves
    QTest::newRow("group") << QByteArray("q 1 0 0 rg 0 0 10 10 re f Q") << int(Kind::Group) << -1 << matrix(1, 0, 0, 1, 0, 30)
                           << QByteArray("q 1 0 0 rg 0 0 10 10 re f Q q 1 0 0 rg q 1 0 0 1 0 30 cm 0 0 10 10 re f Q Q");

    // A text: its copy leaks nothing at its place (a wrap), the original is not touched
    QTest::newRow("text") << QByteArray("BT /F1 12 Tf (A) Tj ET") << int(Kind::TextObject) << -1 << QTransform()
                          << QByteArray("BT /F1 12 Tf (A) Tj ET q BT /F1 12 Tf (A) Tj ET Q");
}

void ContentCommandsTest::copyInContainer()
{
    QFETCH(QByteArray, content);
    QFETCH(int, kind);
    QFETCH(int, place);
    QFETCH(QTransform, displacement);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference node = fixture.node(key, Kind(kind));

    const PDFContentCommandPointer command = Factory::createCopy({ node }, fixture.root(key), place, displacement);
    QCOMPARE(command->getType(), QByteArray("copy"));

    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    // The copy is the first added node; it is a new node of the kind of the original with an
    // identifier of the session. The original is the very same node.
    QVERIFY(!command->getChange().addedNodes.empty());
    const PDFContentNodePointer copy = fixture.get(command->getChange().addedNodes.front());
    QVERIFY(copy != nullptr);
    QCOMPARE(copy->getKind(), Kind(kind));
    QCOMPARE(copy->getId().value, PDFContentEditSession::FIRST_SESSION_NODE_ID);
    QCOMPARE(fixture.get(node), before->getNode(node.node));
    QVERIFY(command->getChange().removedNodes.empty());
    QVERIFY(command->getChange().changedNodes.empty());

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // A second copy gets the following identifiers
    fixture.session()->getUndoStack()->setClean();
    const quint64 counter = fixture.session()->getNodeIdCounter();
    const PDFContentCommandPointer second = Factory::createCopy({ node }, fixture.root(key), -1, QTransform());
    QVERIFY(fixture.execute(second));
    QCOMPARE(second->getChange().addedNodes.front().node.value, counter);
}

void ContentCommandsTest::copyToOtherPage()
{
    // Paste on another page. Both pages name a font /F1, but another one: the font of the copy
    // gets a new name in the resources of the target (R-4), the entry, which was there, stays.
    ContentCommandsFixture fixture(pagesDocument({ "BT /F1 12 Tf (A) Tj ET 1 0 0 rg 0 0 10 10 re f", "0 0 5 5 re f" }));
    PDFContentEditSession* session = fixture.session();
    const Key source = fixture.page(0);
    const Key target = fixture.page(1);
    const PDFContentContainerPointer before = session->getContainer(target);

    const PDFObject sourceFont = session->getContainer(source)->getResources().getObject(PDFContentResourceCategory::Font, "F1");
    const PDFObject targetFont = before->getResources().getObject(PDFContentResourceCategory::Font, "F1");
    QVERIFY(sourceFont.isReference() && targetFont.isReference());
    QVERIFY(sourceFont != targetFont);

    const PDFContentCommandPointer command = Factory::createCopy({ fixture.node(source, Kind::TextObject, 0) }, fixture.root(target), -1, QTransform());
    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));

    // The page of the command is the page of the copies
    QCOMPARE(command->getPageIndex(), PDFInteger(1));
    QCOMPARE(command->getChange().containers, std::vector<Key>({ target }));

    // The text is wrapped (its font must not leak at the new place). Both places are in the
    // initial state of a page, so the wrap sets nothing.
    QCOMPARE(fixture.written(target), QByteArray("0 0 5 5 re f q BT /F2 12 Tf (A) Tj ET Q"));

    const PDFContentResources resources = fixture.resources(target);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::Font, "F1"), targetFont);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::Font, "F2"), sourceFont);

    // The source page is not touched
    QVERIFY(session->getContainer(source)->isUnchanged());
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 1 }));
    QVERIFY(isUndoRedoExact(fixture, target, before));

    // The red square to the beginning of the other page, moved by (3, 4): its fill differs
    // from the initial one there
    session->getUndoStack()->setClean();
    QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(source, Kind::Path, 0) }, fixture.root(target), 0, matrix(1, 0, 0, 1, 3, 4)), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(target), QByteArray("q 1 0 0 1 3 4 cm 1 0 0 rg 0 0 10 10 re f Q 0 0 5 5 re f q BT /F2 12 Tf (A) Tj ET Q"));

    // The same font again: the name it got is used again, nothing is added twice
    session->getUndoStack()->setClean();
    QVERIFY(fixture.execute(Factory::createCopy({ fixture.node(source, Kind::TextObject, 0) }, fixture.root(target), -1, QTransform())));
    QVERIFY(fixture.written(target).endsWith("q BT /F2 12 Tf (A) Tj ET Q q BT /F2 12 Tf (A) Tj ET Q"));
    QCOMPARE(fixture.resources(target).getEntries(PDFContentResourceCategory::Font).size(), size_t(2));
}

void ContentCommandsTest::copyRefusals()
{
    ContentCommandsFixture fixture(pagesDocument({ "1 0 0 rg 0 0 1 1 re f BT /F1 12 Tf (A) Tj ET q 2 2 1 1 re f unknownoperator Q", "0 0 5 5 re f" }));
    const Key key = fixture.page(0);
    const Key otherKey = fixture.page(1);
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference root = fixture.root(key);
    const PDFContentNodeReference path = fixture.node(key, Kind::Path, 0);

    const std::vector<std::pair<std::vector<PDFContentNodeReference>, PDFContentNodeReference>> refused =
    {
        { { fixture.node(key, Kind::StateRun, 0) }, root },                     // A state run is no object
        { { fixture.node(key, Kind::TextShow, 0) }, root },                     // A part of a text
        { { fixture.node(key, Kind::Group, 0) }, root },                        // A group with a part, which cannot be read
        { { root }, root },
        { { path }, fixture.node(key, Kind::TextObject, 0) },                   // Into a text
        { { path }, path },                                                     // Into a leaf
        { { path, fixture.node(otherKey, Kind::Path, 0) }, root },              // Sources of two contents
        { { }, root },
        { { path }, PDFContentNodeReference() },
        { { PDFContentNodeReference{ key, PDFContentNodeId{ 424242 } } }, root }
    };

    for (const auto& item : refused)
    {
        QString errorText;
        QVERIFY(!fixture.execute(Factory::createCopy(item.first, item.second, -1, QTransform()), &errorText));
        QVERIFY(!errorText.isEmpty());
    }

    // A matrix, which is not regular; a place, which does not exist
    QString errorText;
    QVERIFY(!fixture.execute(Factory::createCopy({ path }, root, -1, matrix(0, 0, 0, 0, 1, 1)), &errorText));
    QVERIFY(!fixture.execute(Factory::createCopy({ path }, root, 99, QTransform()), &errorText));

    QCOMPARE(fixture.session()->getContainer(key), before);
    QVERIFY(fixture.session()->getContainer(otherKey)->isUnchanged());
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    QCOMPARE(fixture.session()->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
}

// -------------------------------------------------------------------------------------------------
//  The structure
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::structureDelete()
{
    ContentCommandsFixture fixture(taggedDocument());
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    QCOMPARE(before->getStructuralParentKey(), PDFInteger(0));

    // Nobody has read the structure: the first command, which needs it, reads it
    QVERIFY(session->getStructure() == nullptr);

    // A content of a sequence, which stays not empty: nothing happens to the structure
    const PDFContentCommandPointer first = Factory::createDelete({ fixture.node(key, Kind::Path, 0) });
    QVERIFY(fixture.execute(first));
    QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC 20 0 10 10 re f 40 0 10 10 re f EMC /Span << /MCID 1 >> BDC 60 0 10 10 re f EMC 80 0 10 10 re f"));
    const PDFContentStructureModelPointer original = session->getStructure();
    QVERIFY(original != nullptr);
    QVERIFY(!original->isModified());
    QVERIFY(!first->getChange().isStructureChanged);
    QVERIFY(session->createSnapshot()->structure == nullptr);

    // The last content of a sequence: the wrap is removed, the child of the element removed,
    // the item of the parent tree is null; the element stays, also without children
    session->getUndoStack()->setClean();
    const PDFContentContainerPointer middle = session->getContainer(key);
    const PDFContentCommandPointer second = Factory::createDelete({ fixture.node(key, Kind::Path, 2) });
    QVERIFY(fixture.execute(second));
    QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC 20 0 10 10 re f 40 0 10 10 re f EMC 80 0 10 10 re f"));
    QVERIFY(second->getChange().isStructureChanged);

    const PDFContentStructureModelPointer changed = session->getStructure();
    QVERIFY(changed != original);
    QVERIFY(changed->isModified());
    QCOMPARE(elementType(changed, 0), QByteArray("P"));
    QCOMPARE(elementType(changed, 1), QByteArray());
    QCOMPARE(elementContent(changed, "Span"), std::vector<PDFInteger>());
    QCOMPARE(elementContent(changed, "P"), std::vector<PDFInteger>({ 0 }));
    QCOMPARE(changed->getParentTree().at(0).elements.size(), size_t(2));    // The array keeps its length
    QCOMPARE(session->createSnapshot()->structure, changed);

    // The model the document was read into is not touched (undo keeps it)
    QCOMPARE(elementType(original, 1), QByteArray("Span"));

    // Undo and redo: the versions of the container and of the model, the same objects
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), middle);
    QCOMPARE(session->getStructure(), original);
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getStructure(), changed);
    QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC 20 0 10 10 re f 40 0 10 10 re f EMC 80 0 10 10 re f"));

    // A whole sequence deleted with its wrap
    session->getUndoStack()->setClean();
    QVERIFY(fixture.execute(Factory::createDelete({ fixture.node(key, Kind::MarkedContent, 0) })));
    QCOMPARE(fixture.written(key), QByteArray("80 0 10 10 re f"));
    QCOMPARE(elementType(session->getStructure(), 0), QByteArray());
    QCOMPARE(elementContent(session->getStructure(), "P"), std::vector<PDFInteger>());

    // All the way back
    while (session->undo().has_value()) { }
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getStructure(), original);
    QVERIFY(session->getDirtyPages().empty());
}

void ContentCommandsTest::structureReorder_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("index");
    QTest::addColumn<int>("parentKind");
    QTest::addColumn<int>("place");
    QTest::addColumn<bool>("isKeepingTag");
    QTest::addColumn<QByteArray>("expected");   // Empty: refused
    QTest::addColumn<QList<int>>("paragraph");  // The identifiers of the children of the element P afterwards
    QTest::addColumn<QList<int>>("span");

    const int root = int(Kind::Root);
    const int path = int(Kind::Path);
    const QByteArray p("/P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC");
    const QByteArray span("/Span << /MCID 1 >> BDC 60 0 10 10 re f EMC");

    // ---- An order command (Bring to Front ...): the tag stays ----

    // The only content of a sequence moves with its wrap; the identifier stays, nothing changes
    // in the structure
    QTest::newRow("order: the only content") << path << 3 << root << 0 << true
        << (span + " " + p + " 80 0 10 10 re f") << QList<int>({ 0 }) << QList<int>({ 1 });

    // One of several: an order command changes the painting order only, never the reading order
    // (owner 2026-11-26; ISO 32000-2, 14.7.2: the order of /K is the logical order). The
    // sequence is split around the node (zadani_jadro.md 4.12): each piece is a sequence of the
    // same element, and the children of the element stand in the order the content had - the
    // squares at 0, 20, 40 are read in this order afterwards too. The first piece keeps the
    // identifier 0; the identifiers 0 and 1 are used, so the moved middle square gets 2 and
    // the rest behind it 3: /K 0, 2, 3 reads 0, 20, 40.
    QTest::newRow("order: one of three") << path << 1 << root << -1 << true
        << QByteArray("/P << /MCID 0 >> BDC 0 0 10 10 re f EMC /P << /MCID 3 >> BDC 40 0 10 10 re f EMC " + span + " 80 0 10 10 re f /P << /MCID 2 >> BDC 20 0 10 10 re f EMC") << QList<int>({ 0, 2, 3 }) << QList<int>({ 1 });

    // The first of three to the front: it is read first, so it keeps the identifier 0 with
    // its wrap, and the rest gets 2 behind it in /K
    QTest::newRow("order: the first of three") << path << 0 << root << -1 << true
        << QByteArray("/P << /MCID 2 >> BDC 20 0 10 10 re f 40 0 10 10 re f EMC " + span + " 80 0 10 10 re f /P << /MCID 0 >> BDC 0 0 10 10 re f EMC") << QList<int>({ 0, 2 }) << QList<int>({ 1 });

    // The last of three to the back: it is read last, so it gets the new identifier 2
    QTest::newRow("order: the last of three") << path << 2 << root << 0 << true
        << QByteArray("/P << /MCID 2 >> BDC 40 0 10 10 re f EMC /P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f EMC " + span + " 80 0 10 10 re f") << QList<int>({ 0, 2 }) << QList<int>({ 1 });

    // The first content of the first sequence is painted at the back already: nothing moves,
    // nothing is split (a command, which changes nothing, is refused)
    QTest::newRow("order: at the back already") << path << 0 << root << 0 << true
        << QByteArray() << QList<int>({ 0 }) << QList<int>({ 1 });

    // A square without a tag has nothing to keep
    QTest::newRow("order: no tag") << path << 4 << root << 0 << true
        << QByteArray("80 0 10 10 re f " + p + " " + span) << QList<int>({ 0 }) << QList<int>({ 1 });

    // ---- A drag of a row of the tree: the node belongs to its new parent ----

    // The first content of a sequence leaves it: it is not tagged any more, nothing changes
    QTest::newRow("drag: the first of three") << path << 0 << root << -1 << false
        << QByteArray("/P << /MCID 0 >> BDC 20 0 10 10 re f 40 0 10 10 re f EMC " + span + " 80 0 10 10 re f 0 0 10 10 re f") << QList<int>({ 0 }) << QList<int>({ 1 });

    // From the middle: the sequence is split - the rest behind the node gets the new identifier
    // 2 of the same element, right behind the sequence
    QTest::newRow("drag: the middle of three") << path << 1 << root << -1 << false
        << QByteArray("/P << /MCID 0 >> BDC 0 0 10 10 re f EMC /P << /MCID 2 >> BDC 40 0 10 10 re f EMC " + span + " 80 0 10 10 re f 20 0 10 10 re f") << QList<int>({ 0, 2 }) << QList<int>({ 1 });

    // The only content: the wrap goes, the child of the element goes, the parent tree item is null
    QTest::newRow("drag: the only content") << path << 3 << root << -1 << false
        << QByteArray(p + " 80 0 10 10 re f 60 0 10 10 re f") << QList<int>({ 0 }) << QList<int>();

    // Inside of its sequence the node keeps its tag
    QTest::newRow("drag: inside of the sequence") << path << 0 << int(Kind::MarkedContent) << -1 << false
        << QByteArray("/P << /MCID 0 >> BDC 20 0 10 10 re f 40 0 10 10 re f 0 0 10 10 re f EMC " + span + " 80 0 10 10 re f") << QList<int>({ 0 }) << QList<int>({ 1 });

    // A sequence as a whole keeps its identifier at another place of the page
    QTest::newRow("drag: a whole sequence") << int(Kind::MarkedContent) << 1 << root << 0 << false
        << QByteArray(span + " " + p + " 80 0 10 10 re f") << QList<int>({ 0 }) << QList<int>({ 1 });

    // The sequences do not nest: tagged content does not go into other tagged content
    QTest::newRow("drag: a sequence into a sequence") << int(Kind::MarkedContent) << 1 << int(Kind::MarkedContent) << -1 << false
        << QByteArray() << QList<int>({ 0 }) << QList<int>({ 1 });
}

void ContentCommandsTest::structureReorder()
{
    QFETCH(int, kind);
    QFETCH(int, index);
    QFETCH(int, parentKind);
    QFETCH(int, place);
    QFETCH(bool, isKeepingTag);
    QFETCH(QByteArray, expected);
    QFETCH(QList<int>, paragraph);
    QFETCH(QList<int>, span);

    ContentCommandsFixture fixture(taggedDocument());
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentStructureModelPointer original = session->getStructure();

    const PDFContentNodeReference node = fixture.node(key, Kind(kind), size_t(index));
    const PDFContentNodeReference parent = fixture.node(key, Kind(parentKind), 0);
    const Factory::ReorderMode mode = isKeepingTag ? Factory::ReorderMode::KeepLayerAndTag : Factory::ReorderMode::FollowParent;

    QString errorText;
    const bool isExecuted = fixture.execute(Factory::createReorder({ node }, parent, place, mode), &errorText);

    if (expected.isEmpty())
    {
        QVERIFY(!isExecuted);
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(session->getContainer(key), before);
        QCOMPARE(session->getStructure(), original);
        return;
    }

    QVERIFY2(isExecuted, qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected.simplified());

    // The structure: the children of the two elements, and every identifier leads to its element
    const PDFContentStructureModelPointer model = session->getStructure();
    const std::vector<PDFInteger> paragraphContent = elementContent(model, "P");
    const std::vector<PDFInteger> spanContent = elementContent(model, "Span");
    QCOMPARE(QList<int>(paragraphContent.cbegin(), paragraphContent.cend()), paragraph);
    QCOMPARE(QList<int>(spanContent.cbegin(), spanContent.cend()), span);
    for (const int mcid : paragraph)
    {
        QCOMPARE(elementType(model, mcid), QByteArray("P"));
    }
    QCOMPARE(elementType(model, 1), span.isEmpty() ? QByteArray() : QByteArray("Span"));

    // The reading order of the paragraph: an order command never changes it - the squares at
    // 0, 20 and 40 are read in the order the content had (a drag takes a square out of the
    // paragraph, the others keep their order)
    if (isKeepingTag)
    {
        QCOMPARE(readingOrder(fixture, key, "P"), QList<int>({ 0, 20, 40 }));
    }

    // The model changes exactly when its content does
    const bool isStructureChanged = paragraph != QList<int>({ 0 }) || span != QList<int>({ 1 });
    QCOMPARE(model != original, isStructureChanged);
    QCOMPARE(model->isModified(), isStructureChanged);

    // Undo and redo give the same versions of the container and of the model
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getStructure(), original);
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getStructure(), model);
    QCOMPARE(fixture.written(key), expected.simplified());
}

void ContentCommandsTest::structureCopyAndClear()
{
    // A copy has no tag (zadani_jadro.md 4.12): the content of a copied sequence stands without
    // its wrap, and nothing happens to the structure
    ContentCommandsFixture fixture(taggedDocument());
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentStructureModelPointer original = session->getStructure();

    const PDFContentCommandPointer copy = Factory::createCopy({ fixture.node(key, Kind::MarkedContent, 1), fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, QTransform());
    QVERIFY(fixture.execute(copy));
    QCOMPARE(fixture.written(key), taggedContent().simplified() + QByteArray(" 60 0 10 10 re f 0 0 10 10 re f"));
    QCOMPARE(session->getStructure(), original);
    QVERIFY(!copy->getChange().isStructureChanged);
    QCOMPARE(fixture.nodes(key, Kind::MarkedContent).size(), size_t(2));
    for (const PDFContentNodeReference& added : copy->getChange().addedNodes)
    {
        QCOMPARE(fixture.get(added)->getParentId(), fixture.root(key).node);
    }

    // Clear Page: every sequence of the page leaves the structure as by a deletion
    session->getUndoStack()->setClean();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentCommandPointer clear = Factory::createClearPage(key);
    QVERIFY(fixture.execute(clear));
    QCOMPARE(fixture.written(key), QByteArray());
    QVERIFY(clear->getChange().isStructureChanged);
    QCOMPARE(elementType(session->getStructure(), 0), QByteArray());
    QCOMPARE(elementType(session->getStructure(), 1), QByteArray());
    QCOMPARE(elementContent(session->getStructure(), "P"), std::vector<PDFInteger>());
    QCOMPARE(elementContent(session->getStructure(), "Span"), std::vector<PDFInteger>());

    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getStructure(), original);
}

void ContentCommandsTest::clearPage()
{
    ContentCommandsFixture fixture(pagesDocument({ "1 0 0 rg 0 0 1 1 re f q BT /F1 12 Tf (A) Tj ET Q unknownoperator 2 2 1 1 re f", "0 0 5 5 re f (unfinished", "" }));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page(0);
    const PDFContentContainerPointer before = session->getContainer(key);
    const size_t nodeCount = before->getNodeCount();

    const PDFContentCommandPointer command = Factory::createClearPage(key);
    QCOMPARE(command->getType(), QByteArray("clear_page"));
    QCOMPARE(command->getPageIndex(), PDFInteger(0));

    // Everything goes, also the state runs and what cannot be read; only the root stays
    QVERIFY(fixture.execute(command));
    QCOMPARE(fixture.written(key), QByteArray());
    QCOMPARE(session->getContainer(key)->getNodeCount(), size_t(1));
    QCOMPARE(command->getChange().removedNodes.size(), nodeCount - 1);
    QVERIFY(command->getChange().addedNodes.empty());
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0 }));
    QVERIFY(isUndoRedoExact(fixture, key, before));

    // A page, which is empty, has nothing to clear
    QString errorText;
    QVERIFY(!fixture.execute(Factory::createClearPage(key), &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!fixture.execute(Factory::createClearPage(fixture.page(2)), &errorText));
    QVERIFY(session->getContainer(fixture.page(2))->isUnchanged());

    // The bytes behind the last operation are no operations of a node: they stay
    const Key tailKey = fixture.page(1);
    QVERIFY(fixture.execute(Factory::createClearPage(tailKey)));
    QCOMPARE(fixture.written(tailKey), QByteArray("(unfinished"));

    // Not a page, not loaded, not valid
    for (const Key& refusedKey : { Key(), Key::createForm(PDFObjectReference(3, 0)), Key::createPage(7, PDFObjectReference(4242, 0)) })
    {
        errorText.clear();
        QVERIFY(!fixture.execute(Factory::createClearPage(refusedKey), &errorText));
        QVERIFY(!errorText.isEmpty());
    }
}

// -------------------------------------------------------------------------------------------------
//  Merging, JSON, the registry, hostile input, threads
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::mergeCommands()
{
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re S 20 0 10 10 re S"));
    PDFContentEditSession* session = fixture.session();
    PDFContentUndoStack* stack = session->getUndoStack();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);

    // Moves by the arrow keys: the same objects moved again right away are one step. The first
    // command stands behind the clean point of the new stack, so it is a step of its own.
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 1, 0))));
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 1, 0))));
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 0, 5))));
    QCOMPARE(stack->getCount(), size_t(1));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 2 5 cm 0 0 10 10 re S Q 20 0 10 10 re S"));
    const PDFContentContainerPointer merged = session->getContainer(key);

    // One undo takes all three back, one redo gives the merged result again
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
    QVERIFY(!session->undo().has_value());
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getContainer(key), merged);

    // Other objects: another step
    QVERIFY(fixture.execute(Factory::createTransform({ second }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(2));
    QVERIFY(fixture.execute(Factory::createTransform({ first, second }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(3));

    // A dragged value: the same properties of the same objects
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::LineWidth, realValue(2) } })));
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::LineWidth, realValue(3) } })));
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::LineWidth, realValue(4) } })));
    QCOMPARE(stack->getCount(), size_t(4));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 3 5 cm 4 w 0 0 10 10 re S Q q 1 0 0 1 2 0 cm 20 0 10 10 re S Q"));
    QVERIFY(session->undo().has_value());
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 3 5 cm 0 0 10 10 re S Q q 1 0 0 1 2 0 cm 20 0 10 10 re S Q"));
    QVERIFY(session->redo().has_value());

    // Another property, another kind of command: new steps
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::MiterLimit, realValue(4) } })));
    QCOMPARE(stack->getCount(), size_t(5));
    QVERIFY(fixture.execute(Factory::createSetTransparency({ first }, 0.5, std::nullopt, std::nullopt)));
    QVERIFY(fixture.execute(Factory::createSetTransparency({ first }, 0.25, std::nullopt, std::nullopt)));
    QCOMPARE(stack->getCount(), size_t(6));
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(7));

    // At the clean point nothing is merged (the saved state must stay reachable)
    stack->setClean();
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(8));
    QVERIFY(session->undo().has_value());
    QVERIFY(stack->isClean());
    QVERIFY(session->redo().has_value());

    // Later than a second: a new step
    QTest::qSleep(1200);
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(9));
    QVERIFY(fixture.execute(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(9));
}

void ContentCommandsTest::jsonCommands()
{
    // The operations of a script (zadani_overeni.md 7.1): an identifier is "p<page>:<node>" with
    // the page from 1; the builder numbers the nodes depth first from 1 (the root), so the two
    // squares of this page are the nodes 2 and 3.
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f 2 2 1 1 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentNodeReference root{ key, PDFContentNodeId{ 1 } };
    const PDFContentNodeReference first{ key, PDFContentNodeId{ 2 } };
    const PDFContentNodeReference second{ key, PDFContentNodeId{ 3 } };
    QCOMPARE(fixture.node(key, Kind::Path, 0), first);
    QCOMPARE(fixture.node(key, Kind::Path, 1), second);

    const QJsonArray both({ QStringLiteral("p1:2"), QStringLiteral("p1:3") });
    const QJsonArray one({ QStringLiteral("p1:2") });

    // What a command writes
    QCOMPARE(Factory::createDelete({ first, second })->toJson(), QJsonObject({ { "type", "delete" }, { "ids", both } }));
    QCOMPARE(Factory::createTransform({ first }, matrix(1, 0, 0, 1, 5, 7))->toJson(), QJsonObject({ { "type", "transform" }, { "ids", one }, { "matrix", QJsonArray({ 1, 0, 0, 1, 5, 7 }) } }));
    QCOMPARE(Factory::createReorder({ first }, root, -1, Factory::ReorderMode::KeepLayerAndTag)->toJson(),
             QJsonObject({ { "type", "reorder" }, { "ids", one }, { "parent", "p1:1" }, { "index", -1 }, { "mode", "keep_layer_and_tag" } }));
    QCOMPARE(Factory::createReorder({ first }, root, 2, Factory::ReorderMode::FollowParent)->toJson().value("mode").toString(), QStringLiteral("follow_parent"));
    QCOMPARE(Factory::createCopy({ first }, root, 1, matrix(1, 0, 0, 1, 2, 3))->toJson(),
             QJsonObject({ { "type", "copy" }, { "ids", one }, { "parent", "p1:1" }, { "index", 1 }, { "matrix", QJsonArray({ 1, 0, 0, 1, 2, 3 }) } }));
    QCOMPARE(Factory::createGroup({ first, second })->toJson(), QJsonObject({ { "type", "group" }, { "ids", both } }));
    QCOMPARE(Factory::createUngroup(first)->toJson(), QJsonObject({ { "type", "ungroup" }, { "id", "p1:2" } }));
    QCOMPARE(Factory::createClearPage(key)->toJson(), QJsonObject({ { "type", "clear_page" }, { "page", 1 } }));
    QCOMPARE(Factory::createSetTransparency({ first }, 0.25, 0.5, QByteArray("Multiply"))->toJson(),
             QJsonObject({ { "type", "set_transparency" }, { "ids", one }, { "strokeAlpha", 0.25 }, { "fillAlpha", 0.5 }, { "blendMode", "Multiply" } }));

    PDFContentStateValue cap;
    cap.kind = PDFContentStateValue::Kind::Integer;
    cap.integer = 1;
    PDFContentStateValue dash;
    dash.kind = PDFContentStateValue::Kind::Dash;
    dash.dash.setDashArray({ 3, 1 });
    dash.dash.setDashOffset(2);
    const QJsonObject appearance = Factory::createSetAppearance({ first }, { { Property::LineWidth, realValue(2.5) }, { Property::LineCap, cap }, { Property::DashPattern, dash },
                                                                             { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 0.5 }) } })->toJson();
    QCOMPARE(appearance.value("type").toString(), QStringLiteral("set_appearance"));
    QCOMPARE(appearance.value("lineWidth").toDouble(), 2.5);
    QCOMPARE(appearance.value("lineCap").toInt(), 1);
    QCOMPARE(appearance.value("dash").toObject(), QJsonObject({ { "array", QJsonArray({ 3, 1 }) }, { "phase", 2 } }));
    QCOMPARE(appearance.value("fill").toObject().value("space").toString(), QStringLiteral("DeviceRGB"));
    QCOMPARE(appearance.value("fill").toObject().value("values").toArray(), QJsonArray({ 1, 0, 0.5 }));
    QVERIFY(!appearance.contains("stroke"));

    // What an operation written by hand does
    auto run = [&fixture, session](const QJsonObject& operation) -> QString
    {
        std::vector<QString> errors;
        const PDFContentCommandPointer command = Factory::fromJson(operation, session, QStringLiteral("operations.items[0]"), &errors);
        if (!command || !errors.empty())
        {
            return QStringLiteral("not read: ") + (errors.empty() ? QString() : errors.front());
        }

        QString errorText;
        return fixture.execute(command, &errorText) ? QString() : errorText;
    };

    QCOMPARE(run(QJsonObject({ { "type", "transform" }, { "id", "p1:3" }, { "matrix", QJsonArray({ 1, 0, 0, 1, 5, 7 }) } })), QString());
    QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re f q 1 0 0 1 5 7 cm 2 2 1 1 re f Q"));

    QCOMPARE(run(QJsonObject({ { "type", "set_appearance" }, { "ids", one }, { "fill", "#ff0000" }, { "as", "ignored here" } })), QString());
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 rg 0 0 1 1 re f Q q 1 0 0 1 5 7 cm 2 2 1 1 re f Q"));

    QCOMPARE(run(QJsonObject({ { "type", "set_transparency" }, { "ids", one }, { "fillAlpha", 0.5 } })), QString());
    QCOMPARE(fixture.written(key), QByteArray("q /GS1 gs 1 0 0 rg 0 0 1 1 re f Q q 1 0 0 1 5 7 cm 2 2 1 1 re f Q"));

    // The index and the mode of a reorder are optional: at the end, by the new parent
    QCOMPARE(run(QJsonObject({ { "type", "reorder" }, { "ids", one }, { "parent", "p1:1" } })), QString());
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 7 cm 2 2 1 1 re f Q q /GS1 gs 1 0 0 rg 0 0 1 1 re f Q"));

    QCOMPARE(run(QJsonObject({ { "type", "copy" }, { "id", "p1:3" }, { "parent", "p1:1" }, { "index", 0 } })), QString());
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 7 cm 2 2 1 1 re f Q q 1 0 0 1 5 7 cm 2 2 1 1 re f Q q /GS1 gs 1 0 0 rg 0 0 1 1 re f Q"));

    QCOMPARE(run(QJsonObject({ { "type", "delete" }, { "ids", both } })), QString());
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 7 cm 2 2 1 1 re f Q"));

    QCOMPARE(run(QJsonObject({ { "type", "clear_page" }, { "page", 1 } })), QString());
    QCOMPARE(fixture.written(key), QByteArray());
    QCOMPARE(session->getUndoStack()->getCount(), size_t(7));
}

void ContentCommandsTest::jsonReplay()
{
    // "Replaying the script gives the live model": the commands of one session, written as
    // operations and read by another session of the same document, give the same content.
    // The clean point between the commands keeps them from merging.
    const PDFDocument document = fontDocument("0 0 1 1 re f 1 0 0 rg 2 2 1 1 re f BT /F1 12 Tf (A) Tj ET 4 4 1 1 re B");
    ContentCommandsFixture live(document);
    const Key key = live.page();

    std::vector<QJsonObject> operations;
    auto perform = [&live, &operations](const PDFContentCommandPointer& command) -> bool
    {
        QString errorText;
        if (!live.execute(command, &errorText))
        {
            qWarning("%s", qPrintable(errorText));
            return false;
        }

        operations.push_back(command->toJson());
        live.session()->getUndoStack()->setClean();
        return true;
    };

    PDFContentStateValue join;
    join.kind = PDFContentStateValue::Kind::Integer;
    join.integer = 1;

    QVERIFY(perform(Factory::createTransform({ live.node(key, Kind::Path, 1) }, matrix(0, 1, -1, 0, 30, 0))));
    QVERIFY(perform(Factory::createSetAppearance({ live.node(key, Kind::Path, 2), live.node(key, Kind::TextObject, 0) }, { { Property::StrokeColor, deviceColor("DeviceCMYK", { 0, 1, 0, 0 }) }, { Property::LineJoin, join } })));
    QVERIFY(perform(Factory::createSetTransparency({ live.node(key, Kind::Path, 0) }, std::nullopt, 0.5, QByteArray("Screen"))));
    const PDFContentCommandPointer groupCommand = Factory::createGroup({ live.node(key, Kind::Path, 0), live.node(key, Kind::Path, 2) });
    QVERIFY(perform(groupCommand));
    const PDFContentNodeReference groupReference = groupCommand->getChange().addedNodes.front();
    QVERIFY(perform(Factory::createCopy({ groupReference }, live.root(key), 0, matrix(1, 0, 0, 1, 0, 50))));
    QVERIFY(perform(Factory::createReorder({ live.node(key, Kind::TextObject, 0) }, groupReference, 0, Factory::ReorderMode::FollowParent)));
    QVERIFY(perform(Factory::createUngroup(groupReference)));
    QVERIFY(perform(Factory::createDelete({ live.node(key, Kind::Path, 0) })));
    QCOMPARE(operations.size(), size_t(8));

    // The replay: one operation after the other is read against the model the earlier ones left
    ContentCommandsFixture replay(document);
    QVERIFY(replay.page().isValid());
    for (size_t i = 0; i < operations.size(); ++i)
    {
        std::vector<QString> errors;
        const PDFContentCommandPointer command = Factory::fromJson(operations[i], replay.session(), QStringLiteral("operations.items[%1]").arg(i), &errors);
        QVERIFY2(command && errors.empty(), errors.empty() ? "no command" : qPrintable(errors.front()));
        QCOMPARE(command->toJson(), operations[i]);

        QString errorText;
        QVERIFY2(replay.execute(command, &errorText), qPrintable(errorText));
        replay.session()->getUndoStack()->setClean();
    }

    QCOMPARE(replay.written(key), live.written(key));
    QVERIFY(!live.written(key).startsWith("<"));
    QCOMPARE(replay.session()->getNodeIdCounter(), live.session()->getNodeIdCounter());
    QCOMPARE(replay.session()->getContainer(key)->getNodeCount(), live.session()->getContainer(key)->getNodeCount());

    // The first operation alone, against a result written by hand: the red square under the
    // quarter turn [0 1 -1 0 30 0] (CTM is the identity, so the wrap has the matrix itself)
    ContentCommandsFixture single(document);
    QVERIFY(single.page().isValid());
    std::vector<QString> errors;
    QVERIFY(single.execute(Factory::fromJson(operations.front(), single.session(), QStringLiteral("operations.items[0]"), &errors)));
    QCOMPARE(single.written(key), QByteArray("0 0 1 1 re f 1 0 0 rg q 0 1 -1 0 30 0 cm 2 2 1 1 re f Q BT /F1 12 Tf (A) Tj ET 4 4 1 1 re B"));
}

void ContentCommandsTest::jsonErrors_data()
{
    QTest::addColumn<QJsonObject>("operation");
    QTest::addColumn<QString>("prefix");    // What the first error starts with

    const QJsonArray ids({ QStringLiteral("p1:2") });
    const QString path = QStringLiteral("operations.items[3]");

    QTest::newRow("no type") << QJsonObject({ { "ids", ids } }) << path + ".type: ";
    QTest::newRow("type is a number") << QJsonObject({ { "type", 5 }, { "ids", ids } }) << path + ".type: ";
    QTest::newRow("unknown type") << QJsonObject({ { "type", "explode" }, { "ids", ids } }) << path + ".type: ";
    QTest::newRow("type of a later stage") << QJsonObject({ { "type", "add_text" }, { "ids", ids } }) << path + ".type: ";
    QTest::newRow("unknown key") << QJsonObject({ { "type", "delete" }, { "ids", ids }, { "force", true } }) << path + ": ";
    QTest::newRow("no object") << QJsonObject({ { "type", "delete" } }) << path + ": ";
    QTest::newRow("id and ids") << QJsonObject({ { "type", "delete" }, { "ids", ids }, { "id", "p1:2" } }) << path + ": ";
    QTest::newRow("ids is no array") << QJsonObject({ { "type", "delete" }, { "ids", "p1:2" } }) << path + ".ids: ";
    QTest::newRow("empty ids") << QJsonObject({ { "type", "delete" }, { "ids", QJsonArray() } }) << path + ".ids: ";
    QTest::newRow("node which does not exist") << QJsonObject({ { "type", "delete" }, { "ids", QJsonArray({ QStringLiteral("p1:2"), QStringLiteral("p1:77") }) } }) << path + ".ids[1]: ";
    QTest::newRow("page which is not loaded") << QJsonObject({ { "type", "delete" }, { "id", "p2:2" } }) << path + ".id: ";
    QTest::newRow("identifier is a number") << QJsonObject({ { "type", "delete" }, { "id", 2 } }) << path + ".id: ";
    QTest::newRow("no matrix") << QJsonObject({ { "type", "transform" }, { "ids", ids } }) << path + ".matrix: ";
    QTest::newRow("matrix of five") << QJsonObject({ { "type", "transform" }, { "ids", ids }, { "matrix", QJsonArray({ 1, 0, 0, 1, 0 }) } }) << path + ".matrix: ";
    QTest::newRow("matrix of texts") << QJsonObject({ { "type", "copy" }, { "ids", ids }, { "parent", "p1:1" }, { "matrix", QJsonArray({ "1", "0", "0", "1", "0", "0" }) } }) << path + ".matrix: ";
    QTest::newRow("no parent") << QJsonObject({ { "type", "reorder" }, { "ids", ids } }) << path + ".parent: ";
    QTest::newRow("index is a text") << QJsonObject({ { "type", "reorder" }, { "ids", ids }, { "parent", "p1:1" }, { "index", "last" } }) << path + ".index: ";
    QTest::newRow("index is a fraction") << QJsonObject({ { "type", "copy" }, { "ids", ids }, { "parent", "p1:1" }, { "index", 1.5 } }) << path + ".index: ";
    QTest::newRow("index below -1") << QJsonObject({ { "type", "copy" }, { "ids", ids }, { "parent", "p1:1" }, { "index", -2 } }) << path + ".index: ";
    QTest::newRow("unknown mode") << QJsonObject({ { "type", "reorder" }, { "ids", ids }, { "parent", "p1:1" }, { "mode", "keep" } }) << path + ".mode: ";
    QTest::newRow("line width is a text") << QJsonObject({ { "type", "set_appearance" }, { "ids", ids }, { "lineWidth", "2" } }) << path + ".lineWidth: ";
    QTest::newRow("line cap 3") << QJsonObject({ { "type", "set_appearance" }, { "ids", ids }, { "lineCap", 3 } }) << path + ".lineCap: ";
    QTest::newRow("line join 0.5") << QJsonObject({ { "type", "set_appearance" }, { "ids", ids }, { "lineJoin", 0.5 } }) << path + ".lineJoin: ";
    QTest::newRow("color is a number") << QJsonObject({ { "type", "set_appearance" }, { "ids", ids }, { "fill", 5 } }) << path + ".fill: ";
    QTest::newRow("dash is an array") << QJsonObject({ { "type", "set_appearance" }, { "ids", ids }, { "dash", QJsonArray({ 3, 1 }) } }) << path + ".dash: ";
    QTest::newRow("dash with an unknown key") << QJsonObject({ { "type", "set_appearance" }, { "ids", ids }, { "dash", QJsonObject({ { "array", QJsonArray({ 3 }) }, { "offset", 1 } }) } }) << path + ".dash: ";
    QTest::newRow("alpha is a text") << QJsonObject({ { "type", "set_transparency" }, { "ids", ids }, { "fillAlpha", "half" } }) << path + ".fillAlpha: ";
    QTest::newRow("blend mode is a number") << QJsonObject({ { "type", "set_transparency" }, { "ids", ids }, { "blendMode", 1 } }) << path + ".blendMode: ";
    QTest::newRow("ungroup without id") << QJsonObject({ { "type", "ungroup" } }) << path + ".id: ";
    QTest::newRow("ungroup with ids") << QJsonObject({ { "type", "ungroup" }, { "ids", ids } }) << path + ": ";
    QTest::newRow("clear_page without page") << QJsonObject({ { "type", "clear_page" } }) << path + ".page: ";
    QTest::newRow("clear_page of the page 0") << QJsonObject({ { "type", "clear_page" }, { "page", 0 } }) << path + ".page: ";
    QTest::newRow("clear_page of a missing page") << QJsonObject({ { "type", "clear_page" }, { "page", 9 } }) << path + ".page: ";
}

void ContentCommandsTest::jsonErrors()
{
    QFETCH(QJsonObject, operation);
    QFETCH(QString, prefix);

    ContentCommandsFixture fixture(pagesDocument({ "0 0 1 1 re f 2 2 1 1 re f", "0 0 5 5 re f" }));
    const Key key = fixture.page(0);
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);

    // No command, at least one error with the path of the operation and of the field
    std::vector<QString> errors;
    QVERIFY(Factory::fromJson(operation, fixture.session(), QStringLiteral("operations.items[3]"), &errors) == nullptr);
    QVERIFY(!errors.empty());
    QVERIFY2(errors.front().startsWith(prefix), qPrintable(errors.front()));
    QVERIFY(errors.front().size() > prefix.size());

    // Reading an operation never changes the model
    QCOMPARE(fixture.session()->getContainer(key), before);
    QCOMPARE(fixture.session()->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);

    // No list for the errors, no session: no command and no failure
    QVERIFY(Factory::fromJson(operation, fixture.session(), QStringLiteral("operations.items[3]"), nullptr) == nullptr);
    QVERIFY(Factory::fromJson(operation, nullptr, QStringLiteral("operations.items[3]"), &errors) == nullptr);
}

/// The creators of the command types of the test registerType (what a command file of a later
/// stage gives to PDFContentCommandFactory::registerType)
class ContentCommandsTestCreators
{
public:
    ContentCommandsTestCreators() = delete;

    /// Creates the command of the tests from { "type": "test_custom", "text": ... }
    static PDFContentCommandPointer create(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        Q_UNUSED(session);

        if (!object.contains(QStringLiteral("text")))
        {
            if (errors)
            {
                errors->push_back(path + QStringLiteral(".text: missing"));
            }
            return nullptr;
        }

        return std::make_shared<ContentCommandsTestCommand>(object.value(QStringLiteral("text")).toString());
    }

    /// A creator, which refuses without an error
    static PDFContentCommandPointer createNothing(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        Q_UNUSED(object);
        Q_UNUSED(session);
        Q_UNUSED(path);
        Q_UNUSED(errors);
        return nullptr;
    }
};

void ContentCommandsTest::registerType()
{
    const Factory::Creator contentCommandsTestCreator = &ContentCommandsTestCreators::create;
    const Factory::Creator contentCommandsSilentCreator = &ContentCommandsTestCreators::createNothing;

    // The commands of the core are registered without anybody asking - the basic ones and the
    // ones of the later command files (stage E1: align, distribute, match_size of
    // pdfcontentarrange.cpp); the types are sorted
    const std::vector<QByteArray> basicTypes = { "align", "clear_page", "copy", "delete", "distribute", "group", "match_size", "reorder", "set_appearance", "set_transparency", "transform", "ungroup" };
    QCOMPARE(Factory::getTypes(), basicTypes);

    // A command file of a later stage registers its types the same way: no change of the base
    // file. A type is registered once.
    QVERIFY(!Factory::registerType(Factory::TYPE_DELETE, contentCommandsTestCreator));
    QVERIFY(!Factory::registerType(nullptr, contentCommandsTestCreator));
    QVERIFY(!Factory::registerType("", contentCommandsTestCreator));
    QVERIFY(!Factory::registerType("test_null", nullptr));
    QCOMPARE(Factory::getTypes(), basicTypes);

    QVERIFY(Factory::registerType("test_custom", contentCommandsTestCreator));
    QVERIFY(!Factory::registerType("test_custom", contentCommandsSilentCreator));
    QVERIFY(Factory::registerType("test_silent", contentCommandsSilentCreator));

    const std::vector<QByteArray> allTypes = { "align", "clear_page", "copy", "delete", "distribute", "group", "match_size", "reorder", "set_appearance", "set_transparency", "test_custom", "test_silent", "transform", "ungroup" };
    QCOMPARE(Factory::getTypes(), allTypes);

    // The factory gives the operation to the creator of its type
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f"));
    std::vector<QString> errors;
    const PDFContentCommandPointer command = Factory::fromJson(QJsonObject({ { "type", "test_custom" }, { "text", "Custom" } }), fixture.session(), QStringLiteral("operations.items[0]"), &errors);
    QVERIFY(command != nullptr);
    QVERIFY(errors.empty());
    QVERIFY(dynamic_cast<ContentCommandsTestCommand*>(command.get()) != nullptr);
    QCOMPARE(command->getText(), QStringLiteral("Custom"));
    QVERIFY(fixture.execute(command));
    QCOMPARE(fixture.session()->getUndoStack()->getUndoText(), QStringLiteral("Custom"));

    // The errors of the creator come through; a creator without a word still gives an error
    QVERIFY(Factory::fromJson(QJsonObject({ { "type", "test_custom" } }), fixture.session(), QStringLiteral("operations.items[1]"), &errors) == nullptr);
    QCOMPARE(errors, std::vector<QString>({ QStringLiteral("operations.items[1].text: missing") }));
    errors.clear();
    QVERIFY(Factory::fromJson(QJsonObject({ { "type", "test_silent" } }), fixture.session(), QStringLiteral("operations.items[2]"), &errors) == nullptr);
    QCOMPARE(errors.size(), size_t(1));
    QVERIFY(errors.front().startsWith(QStringLiteral("operations.items[2]: ")));
}

void ContentCommandsTest::nestingLimit()
{
    // zadani_overeni.md 6.2: the tree is at most 256 nodes with children deep (and their
    // leaves). 256 groups, each with a square and the next group: a new group around the
    // innermost square would be the level 257.
    QByteArray content;
    for (int i = 0; i < PDFContentContainer::MAXIMUM_DEPTH; ++i)
    {
        content += "q 0 0 1 1 re f ";
    }
    for (int i = 0; i < PDFContentContainer::MAXIMUM_DEPTH; ++i)
    {
        content += "Q ";
    }

    ContentCommandsFixture fixture(plainDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const std::vector<PDFContentNodeId> groups = fixture.nodes(key, Kind::Group);
    const std::vector<PDFContentNodeId> paths = fixture.nodes(key, Kind::Path);
    QCOMPARE(groups.size(), size_t(PDFContentContainer::MAXIMUM_DEPTH));
    QCOMPARE(paths.size(), size_t(PDFContentContainer::MAXIMUM_DEPTH));

    const PDFContentNodeReference innermostPath{ key, paths.back() };
    const PDFContentNodeReference innermostGroup{ key, groups.back() };
    const PDFContentNodeReference outermostGroup{ key, groups.front() };

    // Too deep: refused with a reason, nothing changes, the identifiers taken on the way are
    // given back
    QString errorText;
    QVERIFY(!fixture.execute(Factory::createGroup({ innermostPath }), &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!fixture.execute(Factory::createCopy({ PDFContentNodeReference{ key, groups[groups.size() - 2] } }, innermostGroup, -1, QTransform()), &errorText));
    QVERIFY(!fixture.execute(Factory::createReorder({ PDFContentNodeReference{ key, groups[1] } }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText) ||
            session->getContainer(key) != before);
    while (session->undo().has_value()) { }
    QCOMPARE(session->getContainer(key), before);

    // One level higher it fits; and the deep tree is deleted, moved and cleared without trouble
    QVERIFY(fixture.execute(Factory::createGroup({ PDFContentNodeReference{ key, paths[paths.size() - 2] } }), &errorText));
    QVERIFY(session->undo().has_value());
    QVERIFY2(fixture.execute(Factory::createTransform({ outermostGroup }, matrix(1, 0, 0, 1, 1, 1)), &errorText), qPrintable(errorText));
    QVERIFY(session->undo().has_value());
    QVERIFY(fixture.execute(Factory::createDelete({ outermostGroup })));
    QCOMPARE(fixture.written(key), QByteArray());
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);

    // More objects than a command takes
    std::vector<PDFContentNodeReference> many(100001, innermostPath);
    QVERIFY(!fixture.execute(Factory::createDelete(many), &errorText));
    QVERIFY(!fixture.execute(Factory::createTransform(many, matrix(1, 0, 0, 1, 1, 1)), &errorText));
    QVERIFY(!fixture.execute(Factory::createGroup(many), &errorText));
    QCOMPARE(session->getContainer(key), before);
}

void ContentCommandsTest::formsAreQueuedOnce()
{
    // zadani_overeni.md 6.1: the forms of a content are queued by the session, each once (one
    // map key -> container for the whole session): a form, which draws itself, ends, and a form
    // drawn through two other forms is built once.
    //
    //   page: /A Do /B Do      A: /A Do /C Do (itself and the shared one)      B: /C Do      C: a square
    PDFObjectReference references[3];
    const PDFDocument document = Documents::createDocument("/A Do /B Do", [&references](PDFDocumentBuilder* builder)
    {
        for (PDFObjectReference& reference : references)
        {
            reference = builder->addObject(PDFObject());
        }

        const PDFObject a = PDFObject::createReference(references[0]);
        const PDFObject b = PDFObject::createReference(references[1]);
        const PDFObject c = PDFObject::createReference(references[2]);
        setFormObject(builder, references[0], "/A Do /C Do", { { "A", a }, { "C", c } });
        setFormObject(builder, references[1], "/C Do", { { "C", c } });
        setFormObject(builder, references[2], "0 0 10 10 re f", { });

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "A", a }, { "B", b } }));
        return resources;
    });

    {
        ContentCommandsFixture fixture(document);
        PDFContentEditSession* session = fixture.session();
        QList<Key> loaded;
        int failedCount = 0;
        connect(session, &PDFContentEditSession::containerLoaded, this, [&loaded](const Key& key) { loaded.push_back(key); });
        connect(session, &PDFContentEditSession::containerFailed, this, [&failedCount]() { ++failedCount; });

        session->requestContainer(session->getPageKey(0));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), qsizetype(4), timeout(30000));

        // Nothing more comes: no second build of a key
        QVERIFY(QThreadPool::globalInstance()->waitForDone(timeout(30000)));
        QTest::qWait(100);
        QCOMPARE(loaded.size(), qsizetype(4));
        QCOMPARE(failedCount, 0);
        QCOMPARE(std::set<Key>(loaded.cbegin(), loaded.cend()).size(), size_t(4));
        for (const PDFObjectReference& reference : references)
        {
            QVERIFY(loaded.contains(Key::createForm(reference)));
            QVERIFY(session->getContainer(Key::createForm(reference)) != nullptr);
        }

        // A command over the shared form: the page is dirty through both paths, once
        const Key shared = Key::createForm(references[2]);
        QVERIFY(fixture.execute(Factory::createTransform({ fixture.node(shared, Kind::Path, 0) }, matrix(1, 0, 0, 1, 1, 1))));
        QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0 }));
        QCOMPARE(session->createSnapshot()->containers.size(), size_t(1));

        // The identifier of a node of a form: the path of the form uses from its page
        const PDFContentCommandPointer command = Factory::createDelete({ fixture.node(shared, Kind::Path, 0) });
        QVERIFY(fixture.execute(command));
        QCOMPARE(command->getPageIndex(), PDFInteger(0));
        const QString id = command->toJson().value("ids").toArray().first().toString();
        QVERIFY2(id.startsWith(QStringLiteral("p1:")) && id.count(QLatin1Char('/')) == 2, qPrintable(id));
        QCOMPARE(PDFContentJson::readNodeId(id, session, nullptr), PDFContentNodeReference());   // The node is deleted
    }

    // The nesting limit: a chain of 40 forms, each drawing the next. The forms up to the level
    // 32 are loaded, the deeper ones are not built - neither by the queue nor in this thread.
    const int chainLength = 40;
    std::vector<PDFObjectReference> chain(chainLength);
    const PDFDocument deep = Documents::createDocument("/F0 Do", [&chain, chainLength](PDFDocumentBuilder* builder)
    {
        for (PDFObjectReference& reference : chain)
        {
            reference = builder->addObject(PDFObject());
        }

        for (int i = 0; i < chainLength; ++i)
        {
            if (i + 1 < chainLength)
            {
                const QByteArray name = "F" + QByteArray::number(i + 1);
                setFormObject(builder, chain[size_t(i)], "/" + name + " Do", { { name, PDFObject::createReference(chain[size_t(i + 1)]) } });
            }
            else
            {
                setFormObject(builder, chain[size_t(i)], "0 0 10 10 re f", { });
            }
        }

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "F0", PDFObject::createReference(chain[0]) } }));
        return resources;
    });

    ContentCommandsFixture fixture(deep);
    PDFContentEditSession* session = fixture.session();
    int loadedCount = 0;
    connect(session, &PDFContentEditSession::containerLoaded, this, [&loadedCount]() { ++loadedCount; });
    session->requestContainer(session->getPageKey(0));
    QTRY_COMPARE_WITH_TIMEOUT(loadedCount, 1 + PDFContentEditSession::MAXIMUM_CONTAINER_NESTING, timeout(60000));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(timeout(30000)));
    QTest::qWait(100);
    QCOMPARE(loadedCount, 33);

    QVERIFY(session->getContainer(Key::createForm(chain[31])) != nullptr);
    QVERIFY(session->getContainer(Key::createForm(chain[32])) == nullptr);
    QString errorText;
    QVERIFY(!session->loadContainer(Key::createForm(chain[32]), nullptr, &errorText));
    QVERIFY(!errorText.isEmpty());
}

void ContentCommandsTest::sessionsInThreads()
{
    // The run of a tool has a session of its own in its own thread (zadani_overeni.md 6.3):
    // several sessions work at once and share nothing mutable - the registry of the types, the
    // commands and the builds are reentrant. Each thread loads, changes, undoes and reads a
    // script; all must give the content written by hand below.
    const PDFDocument document = fontDocument("0 0 1 1 re f 1 0 0 rg 2 2 1 1 re f BT /F1 12 Tf (A) Tj ET");
    const QByteArray expected("1 0 0 rg q 1 0 0 1 5 7 cm 2 2 1 1 re f Q q 0 g BT /F1 12 Tf (A) Tj ET Q /F1 12 Tf");

    const int threadCount = 8;
    const int repeatCount = 20;
    std::vector<QByteArray> results(threadCount);
    std::atomic_int failureCount = 0;

    auto work = [&document, &results, &failureCount, repeatCount](int threadIndex)
    {
        for (int i = 0; i < repeatCount; ++i)
        {
            ContentCommandsFixture fixture(document);
            const Key key = fixture.page();

            // Delete the first square; move the second one (through a script); fill the text black;
            // one more command, which is undone
            std::vector<QString> errors;
            const QJsonObject operation({ { "type", "transform" }, { "id", "p1:4" }, { "matrix", QJsonArray({ 1, 0, 0, 1, 5, 7 }) } });
            const bool isSuccess = key.isValid() &&
                                   fixture.execute(Factory::createDelete({ fixture.node(key, Kind::Path, 0) })) &&
                                   fixture.execute(Factory::fromJson(operation, fixture.session(), QStringLiteral("operations.items[0]"), &errors)) &&
                                   fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::TextObject, 0) }, { { Property::FillColor, deviceColor("DeviceGray", { 0 }) } })) &&
                                   fixture.execute(Factory::createClearPage(key)) &&
                                   fixture.session()->undo().has_value() &&
                                   Factory::getTypes().size() >= 9;
            if (!isSuccess)
            {
                ++failureCount;
            }

            results[size_t(threadIndex)] = fixture.written(key);
            if (results[size_t(threadIndex)] != fixture.written(key))
            {
                ++failureCount;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(work, i);
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    QCOMPARE(failureCount.load(), 0);
    for (const QByteArray& result : results)
    {
        QCOMPARE(result, expected);
    }
}

// -------------------------------------------------------------------------------------------------
//  Details of the rules
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::sessionRollsBackRefusedCommand()
{
    // "The model is unchanged then": whatever a refused command has set is put back by the
    // session - the version of the container and the counter of the identifiers
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f 20 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer original = session->getContainer(key);

    // Another version of the container, made by hand
    PDFContentContainerEditor editor(original);
    QVERIFY(editor.removeNode(fixture.node(key, Kind::Path, 0).node));
    const PDFContentContainerPointer changed = editor.build();
    QVERIFY(changed != nullptr && changed != original);

    auto change = [&changed](PDFContentEditSession* current)
    {
        current->setContainer(changed);
        current->allocateNodeId();
        current->allocateNodeId();
    };

    std::shared_ptr<ContentCommandsTestCommand> refused = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Refused"));
    refused->isRefused = true;
    refused->onApply = change;

    QString errorText;
    QVERIFY(!session->execute(refused, &errorText));
    QCOMPARE(session->getContainer(key), original);
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));
    QVERIFY(session->getDirtyPages().empty());

    // The same change of a command, which is accepted, stays
    std::shared_ptr<ContentCommandsTestCommand> accepted = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Accepted"));
    accepted->onApply = change;
    QVERIFY(session->execute(accepted, &errorText));
    QCOMPARE(session->getContainer(key), changed);
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID + 2);
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0 }));
}

void ContentCommandsTest::sessionUndoesMacroInReverseOrder()
{
    // The second command of a macro was applied to the model the first one left, so undo must
    // revert the second one first
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f 20 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer original = session->getContainer(key);

    session->getUndoStack()->beginMacro(QStringLiteral("Two changes"));
    QVERIFY(fixture.execute(Factory::createDelete({ fixture.node(key, Kind::Path, 0) })));
    QVERIFY(fixture.execute(Factory::createTransform({ fixture.node(key, Kind::Path, 0) }, matrix(1, 0, 0, 1, 1, 2))));
    session->getUndoStack()->endMacro();
    QCOMPARE(session->getUndoStack()->getCount(), size_t(1));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 1 2 cm 20 0 10 10 re f Q"));
    const PDFContentContainerPointer changed = session->getContainer(key);

    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), original);
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getContainer(key), changed);
    QCOMPARE(session->getUndoStack()->getUndoText(), QStringLiteral("Two changes"));
}

void ContentCommandsTest::sessionAnswersRequestsOnce()
{
    // Three requests of one container while its worker runs: one build, one answer
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();

    int loadedCount = 0;
    connect(session, &PDFContentEditSession::containerLoaded, this, [&loadedCount]() { ++loadedCount; });

    const Key key = session->getPageKey(0);
    session->requestContainer(key);
    session->requestContainer(key);
    session->requestContainer(key);

    QTRY_VERIFY_WITH_TIMEOUT(loadedCount >= 1, timeout(30000));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(timeout(30000)));
    QTest::qWait(100);
    QCOMPARE(loadedCount, 1);
    QVERIFY(session->getContainer(key) != nullptr);
}

void ContentCommandsTest::stateRunsAreNotSelected()
{
    // The views are asked to select the objects a command added; the state run of R-3 is none
    ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf (A) Tj ET BT (B) Tj ET"));
    const Key key = fixture.page();

    int selectionCount = 0;
    connect(fixture.session(), &PDFContentEditSession::selectionChangeRequested, this, [&selectionCount]() { ++selectionCount; });

    const PDFContentCommandPointer command = Factory::createTransform({ fixture.node(key, Kind::TextObject, 0) }, matrix(1, 0, 0, 1, 5, 0));
    QVERIFY(fixture.execute(command));
    QCOMPARE(command->getChange().addedNodes.size(), size_t(1));
    QCOMPARE(fixture.get(command->getChange().addedNodes.front())->getKind(), Kind::StateRun);
    QCOMPARE(selectionCount, 0);
}

void ContentCommandsTest::reorderKeepsOverride()
{
    // A node, which has a wrap already, keeps what the wrap does at its new place (R-2).
    //
    // The matrix: the square under [2 0 0 2 0 0] was moved by (6, 8) on the page, so its wrap
    // has T = [1 0 0 1 3 4] (the square is drawn under T x CTM: (x, y) -> (2 x + 6, 2 y + 8)).
    // Out of the group the place has the identity, so the wrap must hold the whole matrix
    // [2 0 0 2 6 8] = T x M with M = [2 0 0 2 0 0]; M x T would be [2 0 0 2 3 4].
    {
        ContentCommandsFixture fixture(plainDocument("q 2 0 0 2 0 0 cm 0 0 10 10 re f Q"));
        const Key key = fixture.page();
        const PDFContentNodeReference path = fixture.node(key, Kind::Path, 0);

        QVERIFY(fixture.execute(Factory::createTransform({ path }, matrix(1, 0, 0, 1, 6, 8))));
        QCOMPARE(fixture.written(key), QByteArray("q 2 0 0 2 0 0 cm q 1 0 0 1 3 4 cm 0 0 10 10 re f Q Q"));
        fixture.session()->getUndoStack()->setClean();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ path }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 2 0 0 2 0 0 cm Q q 2 0 0 2 6 8 cm 0 0 10 10 re f Q"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The properties: the square was given a red fill; behind the blue state run it stays red
    // (the color it saw at its old place, black, is not what it shows)
    {
        ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f 0 0 1 rg 20 0 10 10 re f"));
        const Key key = fixture.page();
        const PDFContentNodeReference path = fixture.node(key, Kind::Path, 0);

        QVERIFY(fixture.execute(Factory::createSetAppearance({ path }, { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 0 }) } })));
        QVERIFY(fixture.execute(Factory::createReorder({ path }, fixture.root(key), -1, Factory::ReorderMode::FollowParent)));
        QCOMPARE(fixture.written(key), QByteArray("0 0 1 rg 20 0 10 10 re f q 1 0 0 rg 0 0 10 10 re f Q"));
    }
}

void ContentCommandsTest::copyKeepsSuppressedOperations()
{
    // The copy of a text, whose own color operator is suppressed by an override (RE-5): the
    // operator of the copy is suppressed too (a copy of an operation is an entry of its own),
    // so the copy shows the red of the wrap, not the blue of the text
    ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf 0 0 1 rg (A) Tj ET"));
    const Key key = fixture.page();
    const PDFContentNodeReference text = fixture.node(key, Kind::TextObject, 0);

    QVERIFY(fixture.execute(Factory::createSetAppearance({ text }, { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 0 }) } })));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 rg BT /F1 12 Tf (A) Tj ET Q 0 0 1 rg /F1 12 Tf"));

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createCopy({ text }, fixture.root(key), -1, QTransform()), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 rg BT /F1 12 Tf (A) Tj ET Q 0 0 1 rg /F1 12 Tf q 1 0 0 rg BT /F1 12 Tf (A) Tj ET Q"));
}

void ContentCommandsTest::refusedCommandGivesIdentifiersBack()
{
    // A command refused after it has taken identifiers (the first copy is made, the second
    // object cannot be copied): the counter is back, so the next command gets the first
    // identifier of the session
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f q 2 2 1 1 re f unknownoperator Q"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0), fixture.node(key, Kind::Group, 0) }, fixture.root(key), -1, QTransform()), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);

    const PDFContentCommandPointer command = Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, QTransform());
    QVERIFY(fixture.execute(command));
    QCOMPARE(command->getChange().addedNodes.front().node.value, PDFContentEditSession::FIRST_SESSION_NODE_ID);
}

void ContentCommandsTest::partsOfTextAreRefused()
{
    // Between BT and ET only text operators are allowed, and a part of a text has no place of
    // its own before stage E2: a marked content inside of a text object is not deleted, moved,
    // restyled, grouped or copied, and nothing is placed into it
    ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf /Span BMC (A) Tj EMC ET 0 0 1 1 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference inner = fixture.node(key, Kind::MarkedContent, 0);
    const PDFContentNodeReference path = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference root = fixture.root(key);
    QVERIFY(inner.isValid());
    QCOMPARE(before->getParent(inner.node)->getKind(), Kind::TextObject);

    const std::vector<PDFContentCommandPointer> commands = { Factory::createDelete({ inner }),
                                                             Factory::createTransform({ inner }, matrix(1, 0, 0, 1, 1, 1)),
                                                             Factory::createSetAppearance({ inner }, { { Property::LineWidth, realValue(2) } }),
                                                             Factory::createSetTransparency({ inner }, 0.5, 0.5, std::nullopt),
                                                             Factory::createGroup({ inner }),
                                                             Factory::createReorder({ inner }, root, -1, Factory::ReorderMode::FollowParent),
                                                             Factory::createReorder({ path }, inner, -1, Factory::ReorderMode::FollowParent),
                                                             Factory::createCopy({ inner }, root, -1, QTransform()),
                                                             Factory::createCopy({ path }, inner, -1, QTransform()) };
    for (const PDFContentCommandPointer& command : commands)
    {
        QString errorText;
        QVERIFY2(!fixture.execute(command, &errorText), command->getType().constData());
        QVERIFY(!errorText.isEmpty());
    }

    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));
}

void ContentCommandsTest::clearPageRefusals()
{
    // A page with nothing but bytes, which are no operations: there is no content to clear
    ContentCommandsFixture tail(plainDocument("(unfinished"));
    const Key tailKey = tail.page();
    QVERIFY(tailKey.isValid());
    QString errorText;
    QVERIFY(!tail.execute(Factory::createClearPage(tailKey), &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(tail.session()->getContainer(tailKey)->isUnchanged());

    // Only the content of a page is cleared: a loaded form is refused
    ContentCommandsFixture form(Documents::createFormDocument());
    const Key pageKey = form.page();
    const Key formKey = Key::createForm(form.get(form.node(pageKey, Kind::FormUse, 0))->getData().xobject);
    QVERIFY(form.session()->loadContainer(formKey, nullptr, &errorText));
    errorText.clear();
    QVERIFY(!form.execute(Factory::createClearPage(formKey), &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(form.session()->getContainer(formKey)->isUnchanged());
}

void ContentCommandsTest::mergedTransformWritesItsMatrix()
{
    // The merged command does both moves: its operation of a script carries the product of the
    // two matrices in the order they were applied. (x, y) -> (x + 1, y) -> (2 x + 2, 2 y):
    // [2 0 0 2 2 0]; the other order would be [2 0 0 2 1 0].
    ContentCommandsFixture fixture(plainDocument("5 5 10 10 re f 0 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentNodeReference path = fixture.node(key, Kind::Path, 1);

    // The first command of a new stack stands at the clean point: one command in front
    QVERIFY(fixture.execute(Factory::createDelete({ fixture.node(key, Kind::Path, 0) })));
    QVERIFY(fixture.execute(Factory::createTransform({ path }, matrix(1, 0, 0, 1, 1, 0))));
    QVERIFY(fixture.execute(Factory::createTransform({ path }, matrix(2, 0, 0, 2, 0, 0))));
    QCOMPARE(session->getUndoStack()->getCount(), size_t(2));
    QCOMPARE(fixture.written(key), QByteArray("q 2 0 0 2 2 0 cm 0 0 10 10 re f Q"));

    const PDFContentUndoStack::Step* step = session->getUndoStack()->getUndoStep();
    QVERIFY(step != nullptr);
    QCOMPARE(step->commands.size(), size_t(1));
    QCOMPARE(step->commands.front()->toJson().value("matrix").toArray(), QJsonArray({ 2, 0, 0, 2, 2, 0 }));

    // The merged step is undone as one
    QVERIFY(session->undo().has_value());
    QCOMPARE(fixture.written(key), QByteArray("0 0 10 10 re f"));
}

void ContentCommandsTest::jsonDefaults()
{
    // The fields an operation may leave out: the place is the end, the mode follows the new
    // parent (a drag of a row of the tree), the copy is not moved
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f 2 2 1 1 re f"));
    QVERIFY(fixture.page().isValid());

    std::vector<QString> errors;
    const PDFContentCommandPointer reorder = Factory::fromJson(QJsonObject({ { "type", "reorder" }, { "id", "p1:2" }, { "parent", "p1:1" } }), fixture.session(), QStringLiteral("operations.items[0]"), &errors);
    QVERIFY(reorder != nullptr);
    QCOMPARE(reorder->toJson().value("mode").toString(), QStringLiteral("follow_parent"));
    QCOMPARE(reorder->toJson().value("index").toInt(99), -1);

    const PDFContentCommandPointer copy = Factory::fromJson(QJsonObject({ { "type", "copy" }, { "id", "p1:2" }, { "parent", "p1:1" } }), fixture.session(), QStringLiteral("operations.items[1]"), &errors);
    QVERIFY(copy != nullptr);
    QCOMPARE(copy->toJson().value("index").toInt(99), -1);
    QCOMPARE(copy->toJson().value("matrix").toArray(), QJsonArray({ 1, 0, 0, 1, 0, 0 }));
    QVERIFY(errors.empty());

    const PDFContentCommandPointer keep = Factory::fromJson(QJsonObject({ { "type", "reorder" }, { "id", "p1:2" }, { "parent", "p1:1" }, { "mode", "keep_layer_and_tag" } }), fixture.session(), QStringLiteral("operations.items[2]"), &errors);
    QVERIFY(keep != nullptr);
    QCOMPARE(keep->toJson().value("mode").toString(), QStringLiteral("keep_layer_and_tag"));
}

void ContentCommandsTest::structureIdentifiersOfOneCommand()
{
    // A new marked content identifier is above every identifier of the content of the page,
    // also above one the structure does not know (the sequence 5 here is in no element), and
    // two new sequences of one command get two identifiers: 6 and 7.
    const QByteArray content = taggedContent() + " /Span << /MCID 5 >> BDC 90 0 1 1 re f EMC";
    ContentCommandsFixture fixture(taggedDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);

    // An order command over the first two of the three squares (at 0, 20, 40) of the sequence 0:
    // both go to the end of the page, each in a sequence of its own, and the paragraph still
    // reads 0, 20, 40 (owner 2026-11-26: the reading order never changes; never 40, 20, 0).
    //  - the square at 0 is read first: it keeps the identifier 0 with its wrap, the rest
    //    behind it (20, 40) gets the identifier 6: /K 0, 6;
    //  - the square at 20 is the first of the sequence 6 now: it keeps 6, the rest (40) gets 7
    //    behind it: /K 0, 6, 7.
    // The squares stand at the end of the page in the order of the command (0, then 20).
    QString errorText;
    QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0), fixture.node(key, Kind::Path, 1) }, fixture.root(key), -1, Factory::ReorderMode::KeepLayerAndTag), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 7 >> BDC 40 0 10 10 re f EMC /Span << /MCID 1 >> BDC 60 0 10 10 re f EMC 80 0 10 10 re f /Span << /MCID 5 >> BDC 90 0 1 1 re f EMC "
                                              "/P << /MCID 0 >> BDC 0 0 10 10 re f EMC /P << /MCID 6 >> BDC 20 0 10 10 re f EMC"));

    const PDFContentStructureModelPointer model = session->getStructure();
    QCOMPARE(elementType(model, 6), QByteArray("P"));
    QCOMPARE(elementType(model, 7), QByteArray("P"));
    QCOMPARE(elementType(model, 5), QByteArray());
    QCOMPARE(elementContent(model, "P"), std::vector<PDFInteger>({ 0, 6, 7 }));
    QCOMPARE(elementContent(model, "Span"), std::vector<PDFInteger>({ 1 }));
    QCOMPARE(readingOrder(fixture, key, "P"), QList<int>({ 0, 20, 40 }));

    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
    QVERIFY(!session->getStructure()->isModified());
}

void ContentCommandsTest::reorderKeepsLayer_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<bool>("isKeepingLayer");
    QTest::addColumn<QByteArray>("expected");

    // RE-11: the order commands (Bring to Front ...) change neither the layer nor the tag; a drag
    // of a row of the tree follows the new parent. The layer of a content is its marked content
    // "/OC /L1 BDC ... EMC" (ISO 32000-2, 8.11.3.2).
    const QByteArray two("/OC /L1 BDC 0 0 10 10 re f 20 0 10 10 re f EMC 40 0 10 10 re f");
    const QByteArray one("/OC /L1 BDC 0 0 10 10 re f EMC 40 0 10 10 re f");

    // One of two objects of the layer: at its new place it gets a wrap of the same layer
    QTest::newRow("order: one of two") << two << true << QByteArray("/OC /L1 BDC 20 0 10 10 re f EMC 40 0 10 10 re f /OC /L1 BDC 0 0 10 10 re f EMC");

    // The only content of the wrap moves with it
    QTest::newRow("order: the only content") << one << true << QByteArray("40 0 10 10 re f /OC /L1 BDC 0 0 10 10 re f EMC");

    // A drag out of the layer: the node leaves it
    QTest::newRow("drag: one of two") << two << false << QByteArray("/OC /L1 BDC 20 0 10 10 re f EMC 40 0 10 10 re f 0 0 10 10 re f");
    QTest::newRow("drag: the only content") << one << false << QByteArray("/OC /L1 BDC EMC 40 0 10 10 re f 0 0 10 10 re f");
}

void ContentCommandsTest::reorderKeepsLayer()
{
    QFETCH(QByteArray, content);
    QFETCH(bool, isKeepingLayer);
    QFETCH(QByteArray, expected);

    PDFObject layer;
    ContentCommandsFixture fixture(Documents::createDocument(content, [&layer](PDFDocumentBuilder* builder)
    {
        layer = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("OCG") }, { "Name", PDFObject::createString("Layer") } }));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Properties"), Documents::createDictionaryObject({ { "L1", layer } }));
        return resources;
    }));

    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference path = fixture.node(key, Kind::Path, 0);
    const Factory::ReorderMode mode = isKeepingLayer ? Factory::ReorderMode::KeepLayerAndTag : Factory::ReorderMode::FollowParent;

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createReorder({ path }, fixture.root(key), -1, mode), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    // The resources still name the one layer once: the new wrap uses the name, which is there
    const PDFContentResources resources = fixture.resources(key);
    QCOMPARE(resources.getEntries(PDFContentResourceCategory::Properties).size(), size_t(1));
    QCOMPARE(resources.getObject(PDFContentResourceCategory::Properties, "L1"), layer);

    // The moved square is in a marked content of the layer exactly when the layer is kept
    const PDFContentNodePointer parent = fixture.session()->getContainer(key)->getParent(path.node);
    QCOMPARE(parent->getKind() == Kind::MarkedContent, isKeepingLayer);
    if (isKeepingLayer)
    {
        QCOMPARE(parent->getData().tag, QByteArray("OC"));
        QCOMPARE(parent->getData().propertiesName, QByteArray("L1"));
    }

    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::transformPatternInForm()
{
    // R-5 inside of a form: the matrix of a pattern maps to the space of the content stream, in
    // which the pattern is a resource - here the form (ISO 32000-2, 8.7.3.1). The copy of the
    // pattern is a resource of the form, and D is of the space of the form.
    //
    // The pattern /P1 of the form: /Matrix [1 0 0 1 3 4]; D = [2 0 0 2 5 6]:
    // (x, y) -> (x + 3, y + 4) -> (2 x + 11, 2 y + 14): /Matrix' = [2 0 0 2 11 14]
    auto createPattern = [](PDFDocumentBuilder* builder)
    {
        return Documents::addStreamObject(builder, { { "Type", PDFObject::createName("Pattern") },
                                                     { "PatternType", PDFObject::createInteger(1) },
                                                     { "PaintType", PDFObject::createInteger(1) },
                                                     { "TilingType", PDFObject::createInteger(1) },
                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                     { "XStep", PDFObject::createInteger(10) },
                                                     { "YStep", PDFObject::createInteger(10) },
                                                     { "Matrix", Documents::createIntegerArrayObject({ 1, 0, 0, 1, 3, 4 }) },
                                                     { "Resources", Documents::createDictionaryObject({ }) } }, "1 0 0 rg 0 0 5 5 re f");
    };

    auto loadForm = [](ContentCommandsFixture& fixture) -> Key
    {
        const Key pageKey = fixture.page();
        const Key formKey = Key::createForm(fixture.get(fixture.node(pageKey, Kind::FormUse, 0))->getData().xobject);
        QString errorText;
        return fixture.session()->loadContainer(formKey, nullptr, &errorText) ? formKey : Key();
    };

    // The form sets the pattern itself
    {
        ContentCommandsFixture fixture(Documents::createDocument("/Fm1 Do", [&createPattern](PDFDocumentBuilder* builder)
        {
            const PDFObject formResources = Documents::createDictionaryObject({ { "Pattern", Documents::createDictionaryObject({ { "P1", createPattern(builder) } }) } });
            const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                         { "Subtype", PDFObject::createName("Form") },
                                                                         { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) },
                                                                         { "Resources", formResources } }, "/Pattern cs /P1 scn 0 0 10 10 re f");
            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm1", form } }));
            return resources;
        }));

        const Key formKey = loadForm(fixture);
        QVERIFY(formKey.isValid());
        const PDFContentContainerPointer before = fixture.session()->getContainer(formKey);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createTransform({ fixture.node(formKey, Kind::Path, 0) }, matrix(2, 0, 0, 2, 5, 6)), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(formKey), QByteArray("/Pattern cs /P1 scn q 2 0 0 2 5 6 cm /Pattern cs /P2 scn 0 0 10 10 re f Q"));

        const PDFObject copyReference = fixture.resources(formKey).getObject(PDFContentResourceCategory::Pattern, "P2");
        QVERIFY(copyReference.isReference());
        QCOMPARE(fixture.session()->getNewObject(copyReference.getReference()).getStream()->getDictionary()->get("Matrix"), Documents::createNumberArrayObject({ 2, 0, 0, 2, 11, 14 }));

        // The page, which draws the form, is not changed itself, but it is dirty
        QVERIFY(fixture.session()->getContainer(fixture.session()->getPageKey(0))->isUnchanged());
        QCOMPARE(fixture.session()->getDirtyPages(), std::vector<PDFInteger>({ 0 }));
        QVERIFY(isUndoRedoExact(fixture, formKey, before));
    }

    // The form is painted by the pattern of the place of its use: inside of the form nothing
    // says, which pattern it is (another use may have another one), so the move is refused
    {
        ContentCommandsFixture fixture(Documents::createDocument("/Pattern cs /P1 scn /Fm1 Do", [&createPattern](PDFDocumentBuilder* builder)
        {
            const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                         { "Subtype", PDFObject::createName("Form") },
                                                                         { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) } }, "0 0 10 10 re f");
            PDFDictionaryBuilder resources;
            resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm1", form } }));
            resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "P1", createPattern(builder) } }));
            return resources;
        }));

        const Key formKey = loadForm(fixture);
        QVERIFY(formKey.isValid());
        const PDFContentContainerPointer before = fixture.session()->getContainer(formKey);

        QString errorText;
        QVERIFY(!fixture.execute(Factory::createTransform({ fixture.node(formKey, Kind::Path, 0) }, matrix(2, 0, 0, 2, 5, 6)), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(formKey), before);
        QVERIFY(fixture.session()->createSnapshot()->newObjects.empty());
    }
}

void ContentCommandsTest::structureMoveIntoOtherSequence()
{
    // The middle square of the sequence 0 (/P) goes into the sequence 1 (/Span)
    auto move = [](ContentCommandsFixture& fixture, Factory::ReorderMode mode, QString* errorText) -> bool
    {
        const Key key = fixture.page();
        return fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 1) }, fixture.node(key, Kind::MarkedContent, 1), -1, mode), errorText);
    };

    // An order command keeps the tag - but a sequence cannot stand inside of another one
    // (zadani_jadro.md 4.12: the sequences do not nest), so there is no way to keep it: refused
    {
        ContentCommandsFixture fixture(taggedDocument());
        const PDFContentContainerPointer before = fixture.session()->getContainer(fixture.page());
        QString errorText;
        QVERIFY(!move(fixture, Factory::ReorderMode::KeepLayerAndTag, &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(fixture.page()), before);
    }

    // A drag of a row of the tree: the node belongs to its new parent. It leaves the sequence 0
    // from its middle (the rest gets the identifier 2 of the element P) and is content of the
    // sequence 1 from now on - no child of an element changes for that.
    {
        ContentCommandsFixture fixture(taggedDocument());
        const Key key = fixture.page();
        QString errorText;
        QVERIFY2(move(fixture, Factory::ReorderMode::FollowParent, &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC 0 0 10 10 re f EMC /P << /MCID 2 >> BDC 40 0 10 10 re f EMC "
                                                  "/Span << /MCID 1 >> BDC 60 0 10 10 re f 20 0 10 10 re f EMC 80 0 10 10 re f"));
        QCOMPARE(elementContent(fixture.session()->getStructure(), "P"), std::vector<PDFInteger>({ 0, 2 }));
        QCOMPARE(elementContent(fixture.session()->getStructure(), "Span"), std::vector<PDFInteger>({ 1 }));
    }
}

void ContentCommandsTest::reorderLeavesNodeAtItsPlace()
{
    // Two texts go in front of the square; the first one stands there already. It is not
    // touched: no wrap and no state run for it, though it leaks its font (a node, which does
    // not move, keeps reaching the nodes behind it). The other text comes from the end: it is
    // wrapped at its new place and leaves its font at the old one.
    ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf (A) Tj ET 0 0 1 1 re f BT /F1 10 Tf (C) Tj ET"));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::TextObject, 0);
    const PDFContentNodeReference last = fixture.node(key, Kind::TextObject, 1);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createReorder({ first, last }, fixture.root(key), 1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("BT /F1 12 Tf (A) Tj ET q BT /F1 10 Tf (C) Tj ET Q 0 0 1 1 re f /F1 10 Tf"));
    QCOMPARE(fixture.get(first), before->getNode(first.node));
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

// -------------------------------------------------------------------------------------------------
//  The fixes after the reviews of stage E0
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::reorderKeepsPlaceWhenAnchorIsDissolved()
{
    // Two squares are dragged to the place in front of the sequence /Span: the first one is the
    // only content of that very sequence, the second one stands behind "2 0 0 2 0 0 cm".
    //
    // The first square leaves the sequence, which loses its wrap and its place in the structure
    // (zadani_jadro.md 4.12). The second square still goes to the place the command names: right
    // behind the first one, in front of the state run - never to the end of the page. It saw
    // the matrix [2 0 0 2 0 0] and stands in the initial state now, so its wrap sets
    // M = CTM of the source x (CTM of the target)^-1 = [2 0 0 2 0 0] (R-2).
    ContentCommandsFixture fixture(taggedDocument("/Span << /MCID 1 >> BDC 60 0 10 10 re f EMC 2 0 0 2 0 0 cm 80 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createReorder({ first, second }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("60 0 10 10 re f q 2 0 0 2 0 0 cm 80 0 10 10 re f Q 2 0 0 2 0 0 cm"));

    // Both squares are children of the root, in the order of the command, in front of the rest
    const PDFContentNodePointer root = fixture.get(fixture.root(key));
    QVERIFY(root->getChildren().size() >= 2);
    QCOMPARE(root->getChildren()[0]->getId(), first.node);
    QCOMPARE(root->getChildren()[1]->getId(), second.node);

    // The second square has the state of its new place: the matrix of the beginning of the page
    QCOMPARE(fixture.get(second)->getState()->getCurrentTransformationMatrix(), QTransform());

    // The sequence is gone from the content and from the structure
    QVERIFY(fixture.nodes(key, Kind::MarkedContent).empty());
    QCOMPARE(elementContent(session->getStructure(), "Span"), std::vector<PDFInteger>());
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::commandNamesObjectOnce()
{
    // The objects of a command are distinct (pe0_fixes.md, decision 4): a repeated one is
    // refused by every command - the selection of the GUI is a set, and "the same object twice"
    // has no single meaning for a copy
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f 2 2 1 1 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);
    const PDFContentNodeReference root = fixture.root(key);

    const std::vector<PDFContentNodeReference> twice = { first, second, first };
    const std::vector<PDFContentCommandPointer> commands =
    {
        Factory::createDelete(twice),
        Factory::createTransform(twice, matrix(1, 0, 0, 1, 1, 1)),
        Factory::createReorder(twice, root, 0, Factory::ReorderMode::FollowParent),
        Factory::createReorder(twice, root, 0, Factory::ReorderMode::KeepLayerAndTag),
        Factory::createCopy(twice, root, -1, QTransform()),
        Factory::createSetAppearance(twice, { { Property::LineWidth, realValue(2) } }),
        Factory::createSetTransparency(twice, 0.5, std::nullopt, std::nullopt),
        Factory::createGroup(twice)
    };

    for (const PDFContentCommandPointer& command : commands)
    {
        QString errorText;
        QVERIFY2(!fixture.execute(command, &errorText), command->getType().constData());
        QVERIFY(!errorText.isEmpty());
        QVERIFY(!command->isRefusedAsUnchanged());
        QCOMPARE(session->getContainer(key), before);
    }
    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);

    // A script: the error names the place of the first repeat (the squares are the nodes 2 and 3)
    std::vector<QString> errors;
    const QJsonObject operation({ { "type", "copy" }, { "ids", QJsonArray({ "p1:2", "p1:3", "p1:2", "p1:2" }) }, { "parent", "p1:1" } });
    QVERIFY(Factory::fromJson(operation, session, QStringLiteral("operations.items[4]"), &errors) == nullptr);
    QCOMPARE(errors.size(), size_t(1));
    QVERIFY2(errors.front().startsWith(QStringLiteral("operations.items[4].ids[2]: ")), qPrintable(errors.front()));

    // The same objects named once each are copied
    QVERIFY(fixture.execute(Factory::createCopy({ first, second }, root, -1, QTransform())));
    QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re f 2 2 1 1 re f 0 0 1 1 re f 2 2 1 1 re f"));
}

void ContentCommandsTest::commandCreatesLimitedNodes()
{
    // PDFContentCommand::MAXIMUM_NODES_OF_COMMAND = 100 000 bounds the nodes one command creates
    // (pe0_fixes.md, decision 4). A page with 100 groups of 1 000 squares: a copy of a group
    // creates 1 001 nodes. 100 groups are 100 100 nodes - refused; 99 groups are 99 099 - copied.
    QCOMPARE(PDFContentCommand::MAXIMUM_NODES_OF_COMMAND, size_t(100000));

    QByteArray group("q ");
    for (int i = 0; i < 1000; ++i)
    {
        group += "0 0 1 1 re f ";
    }
    group += "Q ";

    QByteArray content;
    for (int i = 0; i < 100; ++i)
    {
        content += group;
    }

    ContentCommandsFixture fixture(plainDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    QCOMPARE(before->getNodeCount(), size_t(1 + 100 * 1001));

    std::vector<PDFContentNodeReference> groups;
    for (const PDFContentNodeId id : fixture.nodes(key, Kind::Group))
    {
        groups.push_back(PDFContentNodeReference{ key, id });
    }
    QCOMPARE(groups.size(), size_t(100));

    QString errorText;
    const PDFContentCommandPointer tooMany = Factory::createCopy(groups, fixture.root(key), -1, QTransform());
    QVERIFY(!fixture.execute(tooMany, &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!tooMany->isRefusedAsUnchanged());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));

    groups.pop_back();
    QVERIFY2(fixture.execute(Factory::createCopy(groups, fixture.root(key), -1, QTransform()), &errorText), qPrintable(errorText));
    QCOMPARE(session->getContainer(key)->getNodeCount(), before->getNodeCount() + size_t(99 * 1001));
}

void ContentCommandsTest::containerHasLimitedNodes()
{
    // PDFContentCommand::MAXIMUM_NODES_OF_CONTAINER = 1 000 000 refuses a command, after which
    // a container would have more nodes than the limit and more than before the command
    // (pe0_fixes2.md, decision 4). The page is above the limit when it is loaded (the tree
    // builder takes up to 5 000 000 operations): a marked content with a state run ("2 w") and
    // 1 000 000 marked points - with the root 1 000 003 nodes.
    //
    // A copy or a group of one point adds a node - refused. A delete, which leaves a state run
    // behind, is not refused although the page stays above the limit (G2-2): the marked
    // content and its state run go (two nodes), the line width they leave behind is kept by a
    // new state run at their place (R-3; one node) - 1 000 002 nodes, fewer than before. Three
    // deletes of points more bring the page to 999 999 nodes: that makes room for one copy.
    QCOMPARE(PDFContentCommand::MAXIMUM_NODES_OF_CONTAINER, size_t(1000000));

    QByteArray content("/A BMC 2 w EMC ");
    content.reserve(6 * 1000000 + 16);
    for (int i = 0; i < 1000000; ++i)
    {
        content += "/A MP ";
    }

    ContentCommandsFixture fixture(plainDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    QCOMPARE(before->getNodeCount(), size_t(1000003));

    const PDFContentNodePointer root = before->getRoot();
    QCOMPARE(root->getChildren()[0]->getKind(), Kind::MarkedContent);
    const PDFContentNodeReference markedContent{ key, root->getChildren()[0]->getId() };
    const PDFContentNodeReference first{ key, root->getChildren()[1]->getId() };
    const PDFContentNodeReference rootReference{ key, root->getId() };
    std::vector<PDFContentNodeReference> lastPoints;
    for (size_t i = 1; i <= 3; ++i)
    {
        lastPoints.push_back(PDFContentNodeReference{ key, root->getChildren()[root->getChildren().size() - i]->getId() });
    }

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createCopy({ first }, rootReference, 0, QTransform()), &errorText));
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(session->getContainer(key), before);
    QVERIFY(!fixture.execute(Factory::createGroup({ first }), &errorText));
    QCOMPARE(session->getContainer(key), before);

    QVERIFY2(fixture.execute(Factory::createDelete({ markedContent }), &errorText), qPrintable(errorText));
    QCOMPARE(session->getContainer(key)->getNodeCount(), size_t(1000002));
    QCOMPARE(fixture.get(rootReference)->getChildren()[0]->getKind(), Kind::StateRun);

    // Still above the limit: a copy is refused
    const PDFContentContainerPointer deleted = session->getContainer(key);
    QVERIFY(!fixture.execute(Factory::createCopy({ first }, rootReference, 0, QTransform()), &errorText));
    QCOMPARE(session->getContainer(key), deleted);

    QVERIFY2(fixture.execute(Factory::createDelete(lastPoints), &errorText), qPrintable(errorText));
    QCOMPARE(session->getContainer(key)->getNodeCount(), size_t(999999));

    QVERIFY2(fixture.execute(Factory::createCopy({ first }, rootReference, 0, QTransform()), &errorText), qPrintable(errorText));
    QCOMPARE(session->getContainer(key)->getNodeCount(), size_t(1000000));

    QVERIFY(!fixture.execute(Factory::createCopy({ first }, rootReference, 0, QTransform()), &errorText));
    QCOMPARE(session->getContainer(key)->getNodeCount(), size_t(1000000));
}

void ContentCommandsTest::refusedAsUnchanged()
{
    // A command, which changes nothing, is refused with its text, and isRefusedAsUnchanged
    // tells this refusal from every other one (pe0_fixes.md, decision 7: the replay of a script
    // takes it for a success without an effect)
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f 2 2 1 1 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);
    const PDFContentNodeReference root = fixture.root(key);

    // The last square to the end: it stands there
    QString errorText;
    const PDFContentCommandPointer same = Factory::createReorder({ second }, root, -1, Factory::ReorderMode::FollowParent);
    QVERIFY(!same->isRefusedAsUnchanged());
    QVERIFY(!fixture.execute(same, &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(same->isRefusedAsUnchanged());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(0));

    // Every other refusal is not this one: an object, which does not exist; an object, which
    // cannot be deleted; a place, which does not exist; a command of another class
    const std::vector<PDFContentCommandPointer> refused =
    {
        Factory::createDelete({ PDFContentNodeReference{ key, PDFContentNodeId{ 424242 } } }),
        Factory::createDelete({ root }),
        Factory::createReorder({ second }, root, 99, Factory::ReorderMode::FollowParent),
        Factory::createTransform({ second }, matrix(0, 0, 0, 0, 0, 0))
    };
    for (const PDFContentCommandPointer& command : refused)
    {
        QVERIFY(!fixture.execute(command, &errorText));
        QVERIFY(!command->isRefusedAsUnchanged());
    }

    const std::shared_ptr<ContentCommandsTestCommand> testCommand = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("refused"));
    testCommand->isRefused = true;
    QVERIFY(!fixture.execute(testCommand, &errorText));
    QVERIFY(!testCommand->isRefusedAsUnchanged());

    // A command, which is applied, is not refused at all
    const PDFContentCommandPointer moved = Factory::createReorder({ second }, root, 0, Factory::ReorderMode::FollowParent);
    QVERIFY(fixture.execute(moved));
    QVERIFY(!moved->isRefusedAsUnchanged());
    QCOMPARE(fixture.written(key), QByteArray("2 2 1 1 re f 0 0 1 1 re f"));

    // The answer is the one of the last apply: the command refused before changes the model now
    QVERIFY(fixture.execute(same));
    QVERIFY(!same->isRefusedAsUnchanged());
    QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re f 2 2 1 1 re f"));

    // An operation of a script (the second square is the node 3)
    std::vector<QString> errors;
    const PDFContentCommandPointer operation = Factory::fromJson(QJsonObject({ { "type", "reorder" }, { "id", "p1:3" }, { "parent", "p1:1" } }), session, QStringLiteral("operations.items[0]"), &errors);
    QVERIFY(operation != nullptr);
    const size_t stepCount = session->getUndoStack()->getCount();
    QVERIFY(!fixture.execute(operation, &errorText));
    QVERIFY(operation->isRefusedAsUnchanged());
    QCOMPARE(session->getUndoStack()->getCount(), stepCount);

    // Clear Page of a page without content changes nothing (pe0_fixes2.md, decision 5): the
    // second of two Clear Page in a row, and a page, whose content is empty
    {
        ContentCommandsFixture pages(pagesDocument({ "0 0 1 1 re f", "" }));
        PDFContentEditSession* pagesSession = pages.session();
        QVERIFY(pages.execute(Factory::createClearPage(pages.page(0))));
        const size_t steps = pagesSession->getUndoStack()->getCount();

        for (const Key& pageKey : { pages.page(0), pages.page(1) })
        {
            const PDFContentContainerPointer empty = pagesSession->getContainer(pageKey);
            const PDFContentCommandPointer clear = Factory::createClearPage(pageKey);
            errorText.clear();
            QVERIFY(!pages.execute(clear, &errorText));
            QVERIFY(!errorText.isEmpty());
            QVERIFY(clear->isRefusedAsUnchanged());
            QCOMPARE(pagesSession->getContainer(pageKey), empty);
        }
        QCOMPARE(pagesSession->getUndoStack()->getCount(), steps);
    }

    // A transform, set_appearance or set_transparency of objects, which exist but hold nothing
    // the command could change: an empty group, an empty marked content, a group and a marked
    // content with a marked point only (a marked point is neither moved nor restyled)
    {
        ContentCommandsFixture empty(plainDocument("q Q /A BMC EMC q /A MP Q /B BMC /A MP EMC 0 0 1 1 re f"));
        PDFContentEditSession* emptySession = empty.session();
        const Key emptyKey = empty.page();
        const PDFContentContainerPointer emptyBefore = emptySession->getContainer(emptyKey);
        const PDFContentNodeReference emptyGroup = empty.node(emptyKey, Kind::Group, 0);
        const PDFContentNodeReference emptyMarkedContent = empty.node(emptyKey, Kind::MarkedContent, 0);
        const PDFContentNodeReference pointGroup = empty.node(emptyKey, Kind::Group, 1);
        const PDFContentNodeReference pointMarkedContent = empty.node(emptyKey, Kind::MarkedContent, 1);
        const PDFContentNodeReference point = empty.node(emptyKey, Kind::MarkedPoint, 0);
        QVERIFY(emptyGroup.node.isValid() && emptyMarkedContent.node.isValid() && pointGroup.node.isValid() && pointMarkedContent.node.isValid() && point.node.isValid());

        const std::map<Property, PDFContentStateValue> red = { { Property::FillColor, deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 }) } };
        const std::vector<PDFContentCommandPointer> unchanged =
        {
            Factory::createTransform({ emptyGroup }, matrix(1, 0, 0, 1, 5, 5)),
            Factory::createTransform({ emptyMarkedContent, pointGroup, pointMarkedContent }, matrix(2, 0, 0, 2, 0, 0)),
            Factory::createSetAppearance({ emptyGroup, pointMarkedContent }, red),
            Factory::createSetTransparency({ emptyMarkedContent, pointGroup }, 0.5, 0.5, std::nullopt)
        };
        for (const PDFContentCommandPointer& command : unchanged)
        {
            errorText.clear();
            QVERIFY(!empty.execute(command, &errorText));
            QVERIFY(!errorText.isEmpty());
            QVERIFY2(command->isRefusedAsUnchanged(), qPrintable(errorText));
            QCOMPARE(emptySession->getContainer(emptyKey), emptyBefore);
        }

        // A rule of the content or of the command stays an ordinary refusal: a marked point
        // named itself cannot be moved or restyled; a singular matrix; an object, which does not
        // exist, together with an empty group
        const std::vector<PDFContentCommandPointer> ordinary =
        {
            Factory::createTransform({ point }, matrix(1, 0, 0, 1, 5, 5)),
            Factory::createSetAppearance({ point }, red),
            Factory::createTransform({ emptyGroup }, matrix(0, 0, 0, 0, 0, 0)),
            Factory::createTransform({ emptyGroup, PDFContentNodeReference{ emptyKey, PDFContentNodeId{ 424242 } } }, matrix(1, 0, 0, 1, 5, 5)),
            Factory::createSetTransparency({ PDFContentNodeReference{ emptyKey, PDFContentNodeId{ 424242 } }, emptyGroup }, 0.5, 0.5, std::nullopt)
        };
        for (const PDFContentCommandPointer& command : ordinary)
        {
            errorText.clear();
            QVERIFY(!empty.execute(command, &errorText));
            QVERIFY(!errorText.isEmpty());
            QVERIFY2(!command->isRefusedAsUnchanged(), qPrintable(errorText));
            QCOMPARE(emptySession->getContainer(emptyKey), emptyBefore);
        }
        QCOMPARE(emptySession->getUndoStack()->getCount(), size_t(0));
    }
}

void ContentCommandsTest::commandIsExecutedOnce()
{
    // A command object is executed once. Executed a second time it would compute its change
    // again over the changed model and forget the versions of its first execution, which the
    // undo stack needs: the first step could never be undone.
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);

    const PDFContentCommandPointer command = Factory::createTransform({ fixture.node(key, Kind::Path, 0) }, matrix(1, 0, 0, 1, 5, 0));
    QVERIFY(fixture.execute(command));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 0 cm 0 0 10 10 re f Q"));
    const PDFContentContainerPointer after = session->getContainer(key);

    QString errorText;
    QVERIFY(!fixture.execute(command, &errorText));
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!command->isRefusedAsUnchanged());
    QCOMPARE(session->getContainer(key), after);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(1));

    // One undo gives the page as it was
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
    QVERIFY(!session->undo().has_value());

    // The model is the one the command was applied to again: the same change, the same versions
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(session->getContainer(key), after);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(1));
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
}

void ContentCommandsTest::refusedCommandDropsNewObjects()
{
    // A move of a square filled by a pattern makes a copy of the pattern (R-5), a new object of
    // the session. The same command also names a text, which adds to the clipping path (Tr 7)
    // and cannot be moved yet: the command is refused, and the copy of the pattern goes with it.
    PDFObjectReference patternReference;
    ContentCommandsFixture fixture(patternDocument("/Pattern cs /P1 scn 25 25 40 40 re f BT /F1 12 Tf 7 Tr (A) Tj ET", &patternReference));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference text = fixture.node(key, Kind::TextObject, 0);

    QString errorText;
    QVERIFY(!fixture.execute(Factory::createTransform({ square, text }, matrix(1, 0, 0, 1, 30, 40)), &errorText));
    QCOMPARE(session->getContainer(key), before);
    QVERIFY(session->createSnapshot()->newObjects.empty());
    QVERIFY(session->getNewObject(PDFObjectReference(1, PDFContentEditSession::NEW_OBJECT_GENERATION)).isNull());

    // The next new object gets the first number
    QVERIFY2(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 30, 40)), &errorText), qPrintable(errorText));
    const PDFObject copyReference = fixture.get(square)->getOverride()->properties.at(Property::FillColor).color.patternObject;
    QCOMPARE(copyReference, PDFObject::createReference(PDFObjectReference(1, PDFContentEditSession::NEW_OBJECT_GENERATION)));
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(1));

    // An object of an executed command stays also after an undo (redo needs it)
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(1));
}

void ContentCommandsTest::reorderKeepsTagThroughGroup()
{
    const Factory::ReorderMode keep = Factory::ReorderMode::KeepLayerAndTag;

    // The common tagged content "BDC q ... Q EMC": the square is the only content of its group,
    // the group the only content of the sequence. An order command moves the square with both
    // (zadani_jadro.md 4.4 R-2: "the node moves with its wraps, when it is their only content");
    // the identifier stays, nothing changes in the structure.
    {
        ContentCommandsFixture fixture(taggedDocument("/P << /MCID 0 >> BDC q 0 0 10 10 re f Q EMC 20 0 10 10 re f"));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        QVERIFY(session->loadStructure(nullptr));
        const PDFContentContainerPointer before = session->getContainer(key);
        const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ square }, fixture.root(key), -1, keep), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("20 0 10 10 re f /P << /MCID 0 >> BDC q 0 0 10 10 re f Q EMC"));
        QCOMPARE(elementContent(session->getStructure(), "P"), std::vector<PDFInteger>({ 0 }));
        QVERIFY(!session->getStructure()->isModified());
        QCOMPARE(readingOrder(fixture, key, "P"), QList<int>({ 0 }));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The group holds two squares (at 0 and at 5). The sequence cannot be split around a square
    // inside of the group, so the moved square gets a new sequence of the same element, whose
    // child stands behind the old one in /K.
    //  - The last square (5): read last before and after - moved, the new identifier is 2
    //    (0 and 1 are used by the parent tree), /K 0, 2 reads 0, 5.
    //  - The first square (0): it would be read behind the square at 5 - refused.
    {
        const QByteArray content("/P << /MCID 0 >> BDC q 0 0 10 10 re f 5 5 1 1 re f Q EMC 20 0 10 10 re f");

        ContentCommandsFixture fixture(taggedDocument(content));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        QVERIFY(session->loadStructure(nullptr));
        const PDFContentContainerPointer before = session->getContainer(key);
        const PDFContentStructureModelPointer original = session->getStructure();

        QString errorText;
        QVERIFY(!fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, keep), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(session->getContainer(key), before);
        QCOMPARE(session->getStructure(), original);

        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 1) }, fixture.root(key), -1, keep), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC q 0 0 10 10 re f Q EMC 20 0 10 10 re f /P << /MCID 2 >> BDC 5 5 1 1 re f EMC"));
        QCOMPARE(elementContent(session->getStructure(), "P"), std::vector<PDFInteger>({ 0, 2 }));
        QCOMPARE(readingOrder(fixture, key, "P"), QList<int>({ 0, 5 }));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The same with a layer: the square keeps its layer through the group
    {
        PDFObject layer;
        ContentCommandsFixture fixture(layerDocument("/OC /L1 BDC q 0 0 10 10 re f Q EMC 20 0 10 10 re f", &layer));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, keep), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("20 0 10 10 re f /OC /L1 BDC q 0 0 10 10 re f Q EMC"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // One of two squares of a group inside of a layer: a new wrap of the same layer at the new place
    {
        PDFObject layer;
        ContentCommandsFixture fixture(layerDocument("/OC /L1 BDC q 0 0 10 10 re f 5 5 1 1 re f Q EMC 20 0 10 10 re f", &layer));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, keep), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/OC /L1 BDC q 5 5 1 1 re f Q EMC 20 0 10 10 re f /OC /L1 BDC 0 0 10 10 re f EMC"));
        QCOMPARE(fixture.resources(key).getEntries(PDFContentResourceCategory::Properties).size(), size_t(1));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }
}

void ContentCommandsTest::copyKeepsLayer()
{
    // zadani_jadro.md 4.12: "a copy ... keeps its layer inside of the same document". The layer
    // of a content is the marked content "/OC /name BDC ... EMC" around it (ISO 32000-2, 8.11.3.2).

    // A copy to the root of the page: it gets a wrap of the same layer
    {
        PDFObject layer;
        ContentCommandsFixture fixture(layerDocument("/OC /L1 BDC 0 0 10 10 re f EMC 20 0 10 10 re f", &layer));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        const PDFContentCommandPointer command = Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, QTransform());
        QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/OC /L1 BDC 0 0 10 10 re f EMC 20 0 10 10 re f /OC /L1 BDC 0 0 10 10 re f EMC"));

        // The first added node is the copy itself (a script names it by "as"); its parent is
        // the new wrap of the layer
        QVERIFY(!command->getChange().addedNodes.empty());
        const PDFContentNodeReference copy = command->getChange().addedNodes.front();
        QCOMPARE(fixture.get(copy)->getKind(), Kind::Path);
        const PDFContentNodePointer wrap = fixture.session()->getContainer(key)->getParent(copy.node);
        QCOMPARE(wrap->getKind(), Kind::MarkedContent);
        QCOMPARE(wrap->getData().tag, QByteArray("OC"));
        QCOMPARE(wrap->getData().propertiesName, QByteArray("L1"));
        QCOMPARE(wrap->getData().optionalContent, layer.getReference());

        const PDFContentResources resources = fixture.resources(key);
        QCOMPARE(resources.getEntries(PDFContentResourceCategory::Properties).size(), size_t(1));
        QCOMPARE(resources.getObject(PDFContentResourceCategory::Properties, "L1"), layer);
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // A copy into the wrap of its layer is in the layer already: no second wrap
    {
        PDFObject layer;
        ContentCommandsFixture fixture(layerDocument("/OC /L1 BDC 0 0 10 10 re f EMC 20 0 10 10 re f", &layer));
        const Key key = fixture.page();

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.node(key, Kind::MarkedContent, 0), -1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/OC /L1 BDC 0 0 10 10 re f 0 0 10 10 re f EMC 20 0 10 10 re f"));
    }

    // A copy to another page, which does not name the layer: the wrap is copied with the layer
    // as a new entry of /Properties of that page (R-4: the prefix MC, the first free number)
    {
        PDFObject layer;
        PDFDocumentBuilder builder;
        const PDFObjectReference firstPage = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        const PDFObjectReference secondPage = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        layer = Documents::addObject(&builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("OCG") }, { "Name", PDFObject::createString("Layer") } }));
        builder.mergeTo(firstPage, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "Properties", Documents::createDictionaryObject({ { "L1", layer } }) } }) },
                                                                       { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/OC /L1 BDC 0 0 10 10 re f EMC") } }));
        builder.mergeTo(secondPage, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ }) },
                                                                        { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "5 5 1 1 re f") } }));

        ContentCommandsFixture fixture(builder.build());
        const Key key = fixture.page(0);
        const Key otherKey = fixture.page(1);
        QVERIFY(key.isValid() && otherKey.isValid());

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(otherKey), -1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(otherKey), QByteArray("5 5 1 1 re f /OC /MC1 BDC 0 0 10 10 re f EMC"));
        QCOMPARE(fixture.resources(otherKey).getObject(PDFContentResourceCategory::Properties, "MC1"), layer);
        QCOMPARE(fixture.get(fixture.node(otherKey, Kind::MarkedContent, 0))->getData().propertiesName, QByteArray("MC1"));
        QVERIFY(fixture.session()->getContainer(key)->isUnchanged());
    }
}

void ContentCommandsTest::copyIntoTaggedContent_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("sourceKind");
    QTest::addColumn<int>("sourceIndex");
    QTest::addColumn<int>("parentKind");
    QTest::addColumn<int>("parentIndex");
    QTest::addColumn<int>("place");
    QTest::addColumn<QByteArray>("expected");      // Empty: the copy is refused
    QTest::addColumn<QList<int>>("paragraph");     // /K of the element P afterwards

    const int path = int(Kind::Path);
    const int marked = int(Kind::MarkedContent);

    const QByteArray a("0 0 10 10 re f");
    const QByteArray b("20 0 10 10 re f");
    const QByteArray c("40 0 10 10 re f");
    const QByteArray d("60 0 10 10 re f");
    const QByteArray e("80 0 10 10 re f");
    const QByteArray span("/Span << /MCID 1 >> BDC " + d + " EMC");
    const QByteArray p0("/P << /MCID 0 >> BDC ");
    const QByteArray p2("/P << /MCID 2 >> BDC ");

    // The page of taggedDocument: [P 0: A B C] [Span 1: D] E. The identifiers 0 and 1 are used,
    // so the piece cut off a sequence gets 2, and its child of the element P stands behind the
    // child 0: /K 0, 2 - the paragraph is read A, B, C as before (ISO 32000-2, 14.7.2).

    // The untagged square E into the middle of the paragraph, between A and B: the sequence
    // ends in front of the copy, the copy has no tag, the rest is the sequence 2
    QTest::newRow("middle") << taggedContent() << path << 4 << marked << 0 << 1
        << QByteArray(p0 + a + " EMC " + e + ' ' + p2 + b + ' ' + c + " EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    QTest::newRow("middle, behind the second") << taggedContent() << path << 4 << marked << 0 << 2
        << QByteArray(p0 + a + ' ' + b + " EMC " + e + ' ' + p2 + c + " EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    // At the beginning and at the end of the sequence nothing is split: the copy stands in
    // front of the sequence or behind it
    QTest::newRow("start") << taggedContent() << path << 4 << marked << 0 << 0
        << QByteArray(e + ' ' + p0 + a + ' ' + b + ' ' + c + " EMC " + span + ' ' + e) << QList<int>({ 0 });

    QTest::newRow("end") << taggedContent() << path << 4 << marked << 0 << -1
        << QByteArray(p0 + a + ' ' + b + ' ' + c + " EMC " + e + ' ' + span + ' ' + e) << QList<int>({ 0 });

    QTest::newRow("end by its index") << taggedContent() << path << 4 << marked << 0 << 3
        << QByteArray(p0 + a + ' ' + b + ' ' + c + " EMC " + e + ' ' + span + ' ' + e) << QList<int>({ 0 });

    // Duplicate of a tagged square behind itself: the copy of A has no tag, the paragraph is
    // split behind A
    QTest::newRow("duplicate of tagged content") << taggedContent() << path << 0 << marked << 0 << 1
        << QByteArray(p0 + a + " EMC " + a + ' ' + p2 + b + ' ' + c + " EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    // A copy of another sequence is its content without the wrap (the copy has no tag)
    QTest::newRow("copy of a sequence") << taggedContent() << marked << 1 << marked << 0 << 1
        << QByteArray(p0 + a + " EMC " + d + ' ' + p2 + b + ' ' + c + " EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    // The sequence stands in another marked content (an artifact around it): the pieces and
    // the copy stay inside of that one - it is no sequence of the structure
    QTest::newRow("sequence in a marked content") << QByteArray("/Artifact BMC " + p0 + a + ' ' + b + " EMC EMC " + span + ' ' + e) << path << 3 << marked << 1 << 1
        << QByteArray("/Artifact BMC " + p0 + a + " EMC " + e + ' ' + p2 + b + " EMC EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    // A marked content without an identifier inside of the sequence: both are split around the
    // place between B and C - the inner one by a copy of its operator, the sequence with the
    // new identifier
    QTest::newRow("marked content in the sequence") << QByteArray(p0 + a + " /Em BMC " + b + ' ' + c + " EMC 50 0 10 10 re f EMC " + span + ' ' + e) << path << 5 << marked << 1 << 1
        << QByteArray(p0 + a + " /Em BMC " + b + " EMC EMC " + e + ' ' + p2 + "/Em BMC " + c + " EMC 50 0 10 10 re f EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    // At the end of the inner marked content, with content of the sequence behind it: the inner
    // one is not split (the place is behind it), the sequence is
    QTest::newRow("end of a marked content in the sequence") << QByteArray(p0 + a + " /Em BMC " + b + ' ' + c + " EMC 50 0 10 10 re f EMC " + span + ' ' + e) << path << 5 << marked << 1 << -1
        << QByteArray(p0 + a + " /Em BMC " + b + ' ' + c + " EMC EMC " + e + ' ' + p2 + "50 0 10 10 re f EMC " + span + ' ' + e) << QList<int>({ 0, 2 });

    // A place in a group inside of the sequence: q ... Q is not split - refused
    QTest::newRow("group in the sequence") << QByteArray(p0 + "q " + a + ' ' + b + " Q EMC " + span + ' ' + e) << path << 3 << int(Kind::Group) << 0 << 1
        << QByteArray() << QList<int>({ 0 });
}

void ContentCommandsTest::copyIntoTaggedContent()
{
    // zadani_jadro.md 4.12: "a copy is without an MCID". A copy placed into a sequence of the
    // structure would be content of that sequence - the element would read it. The owner's
    // decision of 2026-11-26: the sequence is split around the copy.
    QFETCH(QByteArray, content);
    QFETCH(int, sourceKind);
    QFETCH(int, sourceIndex);
    QFETCH(int, parentKind);
    QFETCH(int, parentIndex);
    QFETCH(int, place);
    QFETCH(QByteArray, expected);
    QFETCH(QList<int>, paragraph);

    ContentCommandsFixture fixture(taggedDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentStructureModelPointer original = session->getStructure();

    const PDFContentNodeReference source = fixture.node(key, Kind(sourceKind), size_t(sourceIndex));
    const PDFContentNodeReference parent = fixture.node(key, Kind(parentKind), size_t(parentIndex));
    QVERIFY(source.isValid() && parent.isValid());

    const PDFContentCommandPointer command = Factory::createCopy({ source }, parent, place, QTransform());
    QString errorText;
    const bool isExecuted = fixture.execute(command, &errorText);

    if (expected.isEmpty())
    {
        QVERIFY(!isExecuted);
        QVERIFY(!errorText.isEmpty());
        QVERIFY(!command->isRefusedAsUnchanged());
        QCOMPARE(session->getContainer(key), before);
        QCOMPARE(session->getStructure(), original);
        QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
        return;
    }

    QVERIFY2(isExecuted, qPrintable(errorText));
    QCOMPARE(fixture.written(key), ContentCommandsFixture::tokens(expected));

    // The copy (a square in every row) is the first added node, also when a piece of the
    // sequence was added for its place; no sequence of the structure is above it
    const PDFContentNodeReference copy = command->getChange().addedNodes.front();
    const PDFContentContainerPointer container = session->getContainer(key);
    QCOMPARE(fixture.get(copy)->getKind(), Kind::Path);
    for (PDFContentNodePointer node = container->getParent(copy.node); node; node = container->getParent(node->getId()))
    {
        QVERIFY(!node->getData().mcid.has_value());
    }

    // The structure: the children of the paragraph in their order, each identifier leads to
    // its element, the other element is untouched
    const PDFContentStructureModelPointer model = session->getStructure();
    const std::vector<PDFInteger> paragraphContent = elementContent(model, "P");
    QCOMPARE(QList<int>(paragraphContent.cbegin(), paragraphContent.cend()), paragraph);
    for (const int mcid : paragraph)
    {
        QCOMPARE(elementType(model, mcid), QByteArray("P"));
    }
    QVERIFY(elementContent(model, "Span") == std::vector<PDFInteger>({ 1 }));
    QCOMPARE(elementType(model, 1), QByteArray("Span"));

    // The model changes exactly when a sequence was split
    const bool isSplit = paragraph.size() > 1;
    QCOMPARE(model != original, isSplit);
    QCOMPARE(model->isModified(), isSplit);
    QCOMPARE(command->getChange().isStructureChanged, isSplit);

    // Undo and redo
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getStructure(), original);
    QVERIFY(session->redo().has_value());
    QCOMPARE(fixture.written(key), ContentCommandsFixture::tokens(expected));
    QCOMPARE(session->getStructure(), model);
}

void ContentCommandsTest::copyIntoSequenceWhichCannotBeSplit()
{
    // A sequence the structure model refuses to split keeps its refusal with the text of the
    // model: here two elements (P and Span) list the sequence 0 as their child, so no element
    // could take the rest of it (zadani_overeni.md 6.2). A place, which needs no split - the
    // end of the sequence - is still a place for a copy.
    ContentCommandsFixture fixture(taggedDocument(taggedContent(), &Documents::createNoResources, 0));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentStructureModelPointer original = session->getStructure();
    const PDFContentNodeReference sequence = fixture.node(key, Kind::MarkedContent, 0);
    const PDFContentNodeReference square = fixture.node(key, Kind::Path, 4);

    const PDFContentStructureChange refusal = original->getChangeForMoveOut(0, 0, false, true, 1);
    QCOMPARE(refusal.action, PDFContentStructureChange::Action::Refused);
    QVERIFY(!refusal.text.isEmpty());

    const PDFContentCommandPointer command = Factory::createCopy({ square }, sequence, 1, QTransform());
    QString errorText;
    QVERIFY(!fixture.execute(command, &errorText));
    QCOMPARE(errorText, refusal.text);
    QVERIFY(!command->isRefusedAsUnchanged());
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(session->getStructure(), original);
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);

    QVERIFY2(fixture.execute(Factory::createCopy({ square }, sequence, -1, QTransform()), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC 80 0 10 10 re f /Span << /MCID 1 >> BDC 60 0 10 10 re f EMC 80 0 10 10 re f"));
    QCOMPARE(session->getStructure(), original);

    // A sequence, which ends with an inline image without its end: whatever is written behind
    // the image is image data, so neither the EMC of the first part nor the wrap of the rest
    // could be written - the place between the square and the image is refused
    {
        ContentCommandsFixture imageFixture(taggedDocument("0 0 1 1 re f /P << /MCID 0 >> BDC 0 0 10 10 re f BI /W 2 /H 2 ID abcd"));
        PDFContentEditSession* imageSession = imageFixture.session();
        const Key imageKey = imageFixture.page();
        QVERIFY(imageSession->loadStructure(nullptr));
        const PDFContentContainerPointer imageBefore = imageSession->getContainer(imageKey);
        const PDFContentNodeReference imageSequence = imageFixture.node(imageKey, Kind::MarkedContent, 0);
        QVERIFY(imageBefore->isUnclosed(imageSequence.node));
        QCOMPARE(imageFixture.get(imageSequence)->getChildren().size(), size_t(2));

        QVERIFY(!imageFixture.execute(Factory::createCopy({ imageFixture.node(imageKey, Kind::Path, 0) }, imageSequence, 1, QTransform()), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(imageSession->getContainer(imageKey), imageBefore);
        QVERIFY(!imageSession->getStructure()->isModified());
    }
}

void ContentCommandsTest::copyIntoTaggedLayer()
{
    // The place is in a layer inside of a sequence of the structure: /P [MCID 0] { /OC /L1 { A B } }.
    // The copy leaves the tag, not the layer: the layer wrap and the sequence are split around
    // the place between A and B, and the copy gets a wrap of the layer of its own.
    PDFObject layer;
    auto createResources = [&layer](PDFDocumentBuilder* builder)
    {
        layer = Documents::addObject(builder, Documents::createDictionaryObject({ { "Type", PDFObject::createName("OCG") }, { "Name", PDFObject::createString("Layer") } }));
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Properties"), Documents::createDictionaryObject({ { "L1", layer } }));
        return resources;
    };

    const QByteArray span("/Span << /MCID 1 >> BDC 60 0 10 10 re f EMC");
    ContentCommandsFixture fixture(taggedDocument("/P << /MCID 0 >> BDC /OC /L1 BDC 0 0 10 10 re f 20 0 10 10 re f EMC EMC " + span + " 80 0 10 10 re f", createResources));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentContainerPointer before = session->getContainer(key);

    const PDFContentNodeReference layerWrap = fixture.node(key, Kind::MarkedContent, 1);
    QCOMPARE(fixture.get(layerWrap)->getData().tag, QByteArray("OC"));

    const PDFContentCommandPointer command = Factory::createCopy({ fixture.node(key, Kind::Path, 3) }, layerWrap, 1, QTransform());
    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), ContentCommandsFixture::tokens("/P << /MCID 0 >> BDC /OC /L1 BDC 0 0 10 10 re f EMC EMC "
                                                                  "/OC /L1 BDC 80 0 10 10 re f EMC "
                                                                  "/P << /MCID 2 >> BDC /OC /L1 BDC 20 0 10 10 re f EMC EMC " + span + " 80 0 10 10 re f"));

    // The wrap of the copy is a layer wrap of the same layer, outside of every sequence
    const PDFContentContainerPointer container = session->getContainer(key);
    const PDFContentNodePointer copyWrap = container->getParent(command->getChange().addedNodes.front().node);
    QVERIFY(copyWrap != nullptr);
    QCOMPARE(copyWrap->getData().tag, QByteArray("OC"));
    QCOMPARE(copyWrap->getData().optionalContent, layer.getReference());
    QVERIFY(container->getParent(copyWrap->getId())->getKind() == Kind::Root);
    QVERIFY(elementContent(session->getStructure(), "P") == std::vector<PDFInteger>({ 0, 2 }));

    QVERIFY(isUndoRedoExact(fixture, key, before));

    // Duplicate of the square A of the layer behind itself: the copy keeps the layer of its
    // source, which is the layer of its place - one wrap of the layer, not two
    QVERIFY(session->undo().has_value());
    QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, layerWrap, 1, QTransform()), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), ContentCommandsFixture::tokens("/P << /MCID 0 >> BDC /OC /L1 BDC 0 0 10 10 re f EMC EMC "
                                                                  "/OC /L1 BDC 0 0 10 10 re f EMC "
                                                                  "/P << /MCID 2 >> BDC /OC /L1 BDC 20 0 10 10 re f EMC EMC " + span + " 80 0 10 10 re f"));
}

void ContentCommandsTest::transformTextWithPatternInside()
{
    const QTransform move = matrix(1, 0, 0, 1, 30, 40);

    // R-5 (ISO 32000-2, 8.7.3.1): cm does not move a pattern. A text, which sets the pattern of
    // its glyphs between BT and ET, would move over a pattern, which stays: the wrap cannot set
    // a moved copy, because the operator of the text inside sets the original again. Refused
    // until texts are written canonically.
    {
        ContentCommandsFixture fixture(patternDocument("BT /F1 24 Tf /Pattern cs /P1 scn 10 10 Td (ABC) Tj ET", nullptr));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY(!fixture.execute(Factory::createTransform({ fixture.node(key, Kind::TextObject, 0) }, move), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(key), before);
        QVERIFY(fixture.session()->createSnapshot()->newObjects.empty());

        // A copy with a move is the same
        QVERIFY(!fixture.execute(Factory::createCopy({ fixture.node(key, Kind::TextObject, 0) }, fixture.root(key), -1, move), &errorText));
        QCOMPARE(fixture.session()->getContainer(key), before);
        QVERIFY(fixture.session()->createSnapshot()->newObjects.empty());
    }

    // The pattern set in front of the text is in the state of the text at BT: the wrap sets
    // the copy with /Matrix x D = [2 0 0 2 3 4] x [1 0 0 1 30 40] = [2 0 0 2 33 44], and the font
    // the text leaves behind is set again behind the wrap (R-3)
    {
        ContentCommandsFixture fixture(patternDocument("/Pattern cs /P1 scn BT /F1 24 Tf 10 10 Td (ABC) Tj ET", nullptr));
        const Key key = fixture.page();
        const PDFContentNodeReference text = fixture.node(key, Kind::TextObject, 0);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createTransform({ text }, move), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/Pattern cs /P1 scn q 1 0 0 1 30 40 cm /Pattern cs /P2 scn BT /F1 24 Tf 10 10 Td (ABC) Tj ET Q /F1 24 Tf"));
        QCOMPARE(overridePatternMatrix(fixture, text, Property::FillColor), Documents::createNumberArrayObject({ 2, 0, 0, 2, 33, 44 }));
    }

    // A pattern set behind the last glyphs paints nothing of the text: the text moves, and the
    // pattern it leaves behind stays the original one for the content behind it
    {
        ContentCommandsFixture fixture(patternDocument("BT /F1 24 Tf 10 10 Td (ABC) Tj /Pattern cs /P1 scn ET 0 0 5 5 re f", nullptr));
        const Key key = fixture.page();

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createTransform({ fixture.node(key, Kind::TextObject, 0) }, move), &errorText), qPrintable(errorText));
        const QByteArray written = fixture.written(key);
        QVERIFY2(written.startsWith("q 1 0 0 1 30 40 cm BT /F1 24 Tf 10 10 Td (ABC) Tj /Pattern cs /P1 scn ET Q "), written.constData());
        QVERIFY2(written.endsWith(" 0 0 5 5 re f"), written.constData());
        QVERIFY2(written.mid(written.indexOf(" ET Q ")).contains("/Pattern cs /P1 scn"), written.constData());
        QVERIFY(fixture.session()->createSnapshot()->newObjects.empty());
    }
}

void ContentCommandsTest::copyAndStrokeWithPattern()
{
    // R-5 for a copy (zadani_jadro.md 4.5: Duplicate / Paste carry R-5): the copy moved by D
    // gets a copy of the pattern with /Matrix x D = [2 0 0 2 3 4] x [1 0 0 1 30 40] = [2 0 0 2 33 44]
    {
        ContentCommandsFixture fixture(patternDocument("/Pattern cs /P1 scn 25 25 40 40 re f", nullptr));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        const PDFContentCommandPointer command = Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, matrix(1, 0, 0, 1, 30, 40));
        QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/Pattern cs /P1 scn 25 25 40 40 re f q 1 0 0 1 30 40 cm /Pattern cs /P2 scn 25 25 40 40 re f Q"));
        QCOMPARE(overridePatternMatrix(fixture, command->getChange().addedNodes.front(), Property::FillColor), Documents::createNumberArrayObject({ 2, 0, 0, 2, 33, 44 }));

        // The original keeps its node and the original pattern
        QCOMPARE(fixture.get(fixture.node(key, Kind::Path, 0)), before->getNode(fixture.node(key, Kind::Path, 0).node));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // R-5 for a stroked pattern: the color of stroking is copied, the color of filling is not touched
    {
        ContentCommandsFixture fixture(patternDocument("/Pattern CS /P1 SCN 25 25 40 40 re S", nullptr));
        const Key key = fixture.page();
        const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 30, 40)), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/Pattern CS /P1 SCN q 1 0 0 1 30 40 cm /Pattern CS /P2 SCN 25 25 40 40 re S Q"));
        QCOMPARE(overridePatternMatrix(fixture, square, Property::StrokeColor), Documents::createNumberArrayObject({ 2, 0, 0, 2, 33, 44 }));
        QVERIFY(!fixture.get(square)->getOverride()->properties.count(Property::FillColor));
    }
}

void ContentCommandsTest::copyGroupWithPatternToOtherPage()
{
    // A group with a square filled by an uncolored pattern is copied to another page and moved
    // by D = [1 0 0 1 5 5]. The square inside of the copy gets a copy of the pattern with
    // /Matrix x D = [1 0 0 1 3 4] x [1 0 0 1 5 5] = [1 0 0 1 8 9] and the tint components of its
    // source, 0.2 0.4 0.6 (R-5: "with the tint components"; ISO 32000-2, 8.7.3.3: they are the
    // operands in front of the name of the pattern).
    //
    // The components are the operands of the operation "0.2 0.4 0.6 /P1 scn" of the first page,
    // its operation 2. The operation 2 of the second page is "5 6 7 8 re": numbers, which must
    // never be read as a color.
    PDFDocumentBuilder builder;
    const PDFObjectReference firstPage = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    const PDFObjectReference secondPage = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
    const PDFObject pattern = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("Pattern") },
                                                                     { "PatternType", PDFObject::createInteger(1) },
                                                                     { "PaintType", PDFObject::createInteger(2) },
                                                                     { "TilingType", PDFObject::createInteger(1) },
                                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                                     { "XStep", PDFObject::createInteger(10) },
                                                                     { "YStep", PDFObject::createInteger(10) },
                                                                     { "Matrix", Documents::createIntegerArrayObject({ 1, 0, 0, 1, 3, 4 }) },
                                                                     { "Resources", Documents::createDictionaryObject({ }) } }, "0 0 5 5 re f");
    const PDFObject colorSpace = Documents::createArrayObject({ PDFObject::createName("Pattern"), PDFObject::createName("DeviceRGB") });
    builder.mergeTo(firstPage, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "Pattern", Documents::createDictionaryObject({ { "P1", pattern } }) },
                                                                                                                  { "ColorSpace", Documents::createDictionaryObject({ { "CS1", colorSpace } }) } }) },
                                                                   { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "q /CS1 cs 0.2 0.4 0.6 /P1 scn 0 0 10 10 re f Q") } }));
    builder.mergeTo(secondPage, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ }) },
                                                                    { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "7 8 9 11 re f 5 6 7 8 re f") } }));

    ContentCommandsFixture fixture(builder.build());
    const Key key = fixture.page(0);
    const Key otherKey = fixture.page(1);
    QVERIFY(key.isValid() && otherKey.isValid());
    QCOMPARE(fixture.session()->getContainer(key)->getOperations().getOperation(2).getOperator(), PDFContentOperator::ColorSetFillingColorN);
    QCOMPARE(fixture.session()->getContainer(otherKey)->getOperations().getOperation(2).getOperator(), PDFContentOperator::Rectangle);

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Group, 0) }, fixture.root(otherKey), -1, matrix(1, 0, 0, 1, 5, 5)), &errorText), qPrintable(errorText));

    // The resources of the copy are named in the second page (R-4): its first color space and
    // its first pattern are CS1 and P1 there too, the copy of the pattern is P2
    QCOMPARE(fixture.written(otherKey), QByteArray("7 8 9 11 re f 5 6 7 8 re f q /CS1 cs 0.2 0.4 0.6 /P1 scn q 1 0 0 1 5 5 cm /CS1 cs 0.2 0.4 0.6 /P2 scn 0 0 10 10 re f Q Q"));

    const PDFContentNodeReference copiedSquare = fixture.node(otherKey, Kind::Path, 2);
    QCOMPARE(fixture.get(copiedSquare)->getOverride()->properties.at(Property::FillColor).color.components, std::vector<PDFReal>({ 0.2, 0.4, 0.6 }));
    QCOMPARE(overridePatternMatrix(fixture, copiedSquare, Property::FillColor), Documents::createNumberArrayObject({ 1, 0, 0, 1, 8, 9 }));
    QVERIFY(fixture.session()->getContainer(key)->isUnchanged());
}

void ContentCommandsTest::copyNeverMakesFormDrawItself()
{
    // The editor never creates a form, which draws itself, also not through other forms
    // (zadani_overeni.md 6.1). The forms of the page:
    //
    //   F: a square        G: /F Do        H: a square
    //   K: /K2 Do          K2: /K Do /H Do      (K and K2 draw each other; H is drawn by the page and by K2)
    //
    // Into the container of F: the use of F itself and the use of G (it draws F) are refused;
    // the use of H is copied, and the use of K too - the walk over K and K2 ends, though they
    // draw each other, and finds no F.
    PDFObjectReference references[5];
    const PDFDocument document = Documents::createDocument("/F Do /G Do /H Do /K Do", [&references](PDFDocumentBuilder* builder)
    {
        for (PDFObjectReference& reference : references)
        {
            reference = builder->addObject(PDFObject());
        }

        const PDFObject f = PDFObject::createReference(references[0]);
        const PDFObject g = PDFObject::createReference(references[1]);
        const PDFObject h = PDFObject::createReference(references[2]);
        const PDFObject k = PDFObject::createReference(references[3]);
        const PDFObject k2 = PDFObject::createReference(references[4]);
        setFormObject(builder, references[0], "0 0 10 10 re f", { });
        setFormObject(builder, references[1], "/F Do", { { "F", f } });
        setFormObject(builder, references[2], "0 0 1 1 re f", { });
        setFormObject(builder, references[3], "/K2 Do", { { "K2", k2 } });
        setFormObject(builder, references[4], "/K Do /H Do", { { "K", k }, { "H", h } });

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "F", f }, { "G", g }, { "H", h }, { "K", k } }));
        return resources;
    });

    ContentCommandsFixture fixture(document);
    PDFContentEditSession* session = fixture.session();
    const Key pageKey = fixture.page();
    const Key formKey = Key::createForm(references[0]);
    QString errorText;
    QVERIFY2(session->loadContainer(formKey, nullptr, &errorText), qPrintable(errorText));
    const PDFContentContainerPointer before = session->getContainer(formKey);
    const PDFContentNodeReference formRoot = fixture.root(formKey);

    const PDFContentNodeReference useOfF = fixture.node(pageKey, Kind::FormUse, 0);
    const PDFContentNodeReference useOfG = fixture.node(pageKey, Kind::FormUse, 1);
    const PDFContentNodeReference useOfH = fixture.node(pageKey, Kind::FormUse, 2);
    const PDFContentNodeReference useOfK = fixture.node(pageKey, Kind::FormUse, 3);
    QCOMPARE(fixture.get(useOfG)->getData().xobject, references[1]);

    // G is not loaded: it is read from the document; then loaded: read from its container
    for (const bool isLoaded : { false, true })
    {
        if (isLoaded)
        {
            QVERIFY2(session->loadContainer(Key::createForm(references[1]), nullptr, &errorText), qPrintable(errorText));
        }

        for (const PDFContentNodeReference& use : { useOfF, useOfG })
        {
            QVERIFY(!fixture.execute(Factory::createCopy({ use }, formRoot, -1, QTransform()), &errorText));
            QVERIFY(!errorText.isEmpty());
            QCOMPARE(session->getContainer(formKey), before);
        }
    }

    QVERIFY2(fixture.execute(Factory::createCopy({ useOfH }, formRoot, -1, QTransform()), &errorText), qPrintable(errorText));
    QVERIFY2(fixture.execute(Factory::createCopy({ useOfK }, formRoot, -1, QTransform()), &errorText), qPrintable(errorText));

    const std::vector<PDFContentNodeId> uses = fixture.nodes(formKey, Kind::FormUse);
    QCOMPARE(uses.size(), size_t(2));
    QCOMPARE(fixture.get(PDFContentNodeReference{ formKey, uses[0] })->getData().xobject, references[2]);
    QCOMPARE(fixture.get(PDFContentNodeReference{ formKey, uses[1] })->getData().xobject, references[3]);

    // A copy to the page, which no form is drawn by, is never refused for this
    QVERIFY2(fixture.execute(Factory::createCopy({ useOfG }, fixture.root(pageKey), -1, QTransform()), &errorText), qPrintable(errorText));
}

void ContentCommandsTest::groupKeepsClip_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QList<int>>("kinds");
    QTest::addColumn<QList<int>>("indices");
    QTest::addColumn<QByteArray>("expected");   // Empty: refused

    const int path = int(Kind::Path);

    // zadani_jadro.md 4.4 R-1: "a node with a clipping effect is never wrapped - the wrap would
    // end the clip and the following siblings would lose it". q ... Q of a new group is such
    // a wrap (ISO 32000-2, 8.4.2: Q restores the clipping path).
    QTest::newRow("a clipping path alone") << QByteArray("0 0 50 50 re W n 0 0 100 100 re f") << QList<int>({ path }) << QList<int>({ 0 }) << QByteArray();
    QTest::newRow("a path, which paints and clips") << QByteArray("0 0 50 50 re W f 0 0 100 100 re f") << QList<int>({ path }) << QList<int>({ 0 }) << QByteArray();
    QTest::newRow("a text, which clips") << QByteArray("BT /F1 12 Tf 7 Tr (A) Tj ET 0 0 100 100 re f") << QList<int>({ int(Kind::TextObject) }) << QList<int>({ 0 }) << QByteArray();

    // A marked content does not end a clipping path: the square behind it is clipped too
    QTest::newRow("a clipping path in a marked content") << QByteArray("/A BMC 0 0 50 50 re W n EMC 0 0 100 100 re f") << QList<int>({ path }) << QList<int>({ 0 }) << QByteArray();

    // Of three: the square between the grouped ones stays outside and would lose the clip
    QTest::newRow("a clipped square stays outside") << QByteArray("0 0 50 50 re W n 0 0 100 100 re f 5 5 1 1 re f") << QList<int>({ path, path }) << QList<int>({ 0, 2 }) << QByteArray();

    // Everything the clipping path clips comes into the group with it: nothing loses the clip
    QTest::newRow("with everything it clips") << QByteArray("0 0 50 50 re W n 0 0 100 100 re f") << QList<int>({ path, path }) << QList<int>({ 0, 1 })
        << QByteArray("q 0 0 50 50 re W n 0 0 100 100 re f Q");
    QTest::newRow("nothing behind it") << QByteArray("0 0 100 100 re f 0 0 50 50 re W n") << QList<int>({ path }) << QList<int>({ 1 })
        << QByteArray("0 0 100 100 re f q 0 0 50 50 re W n Q");
}

void ContentCommandsTest::groupKeepsClip()
{
    QFETCH(QByteArray, content);
    QFETCH(QList<int>, kinds);
    QFETCH(QList<int>, indices);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(fontDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);

    std::vector<PDFContentNodeReference> nodes;
    for (qsizetype i = 0; i < kinds.size(); ++i)
    {
        nodes.push_back(fixture.node(key, Kind(kinds[i]), size_t(indices[i])));
        QVERIFY(nodes.back().isValid());
    }

    QString errorText;
    const bool isExecuted = fixture.execute(Factory::createGroup(nodes), &errorText);

    if (expected.isEmpty())
    {
        QVERIFY(!isExecuted);
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(key), before);
        QCOMPARE(fixture.session()->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
        return;
    }

    QVERIFY2(isExecuted, qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::copyFromFormWritesIdentifiers()
{
    // The script form of a copy from the container of a form: its objects are written with
    // the path of the uses of the form ("p1:3/2": the node 2 of the form drawn by the node 3 of
    // the first page), and the operation can be read back.
    //
    // The page "3 w /Fm1 Do": the root 1, the state run 2, the use of the form 3. The form
    // "5 w 0 0 10 10 re S": the root 1, the state run 2, the square 3.
    ContentCommandsFixture fixture(formDocument("5 w 0 0 10 10 re S"));
    PDFContentEditSession* session = fixture.session();
    const Key pageKey = fixture.page();
    const PDFContentNodeReference use = fixture.node(pageKey, Kind::FormUse, 0);
    QCOMPARE(use.node.value, quint64(3));
    const Key formKey = Key::createForm(fixture.get(use)->getData().xobject);
    QString errorText;
    QVERIFY2(session->loadContainer(formKey, nullptr, &errorText), qPrintable(errorText));
    const PDFContentNodeReference square = fixture.node(formKey, Kind::Path, 0);
    QCOMPARE(square.node.value, quint64(3));

    const PDFContentCommandPointer command = Factory::createCopy({ square }, fixture.root(pageKey), 0, QTransform());
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));

    const QJsonObject json = command->toJson();
    QCOMPARE(json.value("ids").toArray(), QJsonArray({ "p1:3/3" }));
    QCOMPARE(json.value("parent").toString(), QStringLiteral("p1:1"));

    std::vector<QString> errors;
    const PDFContentCommandPointer read = Factory::fromJson(json, session, QStringLiteral("operations.items[0]"), &errors);
    QVERIFY2(read != nullptr, errors.empty() ? "" : qPrintable(errors.front()));
    QVERIFY(errors.empty());

    // The operation read back does the same: one more copy of the square of the form (a command
    // knows the path of the uses of a form once it was applied)
    const size_t squareCount = fixture.nodes(pageKey, Kind::Path).size();
    QVERIFY2(fixture.execute(read, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.nodes(pageKey, Kind::Path).size(), squareCount + 1);
    QCOMPARE(read->toJson(), json);
}

void ContentCommandsTest::setAppearanceDeviceColorsOnly()
{
    // Stage E0 sets colors of the device spaces only (zadani_jadro.md 4.5). A color of another
    // space needs as many components as its space has, and a pattern its object (ISO 32000-2,
    // 8.6.8): nothing checks them yet, so they are refused - "/CS1 cs 0.5 scn" for a space of
    // three components would be a broken content.
    ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);

    PDFContentStateValue otherSpace;
    otherSpace.kind = PDFContentStateValue::Kind::Color;
    otherSpace.color.colorSpaceObject = Documents::createArrayObject({ PDFObject::createName("CalRGB"), Documents::createDictionaryObject({ { "WhitePoint", Documents::createNumberArrayObject({ 0.95, 1.0, 1.09 }) } }) });
    otherSpace.color.components = { 0.5 };

    PDFContentStateValue patternWithoutObject;
    patternWithoutObject.kind = PDFContentStateValue::Kind::Color;
    patternWithoutObject.color.colorSpaceObject = PDFObject::createName("Pattern");
    patternWithoutObject.color.components = { 0.5 };

    PDFContentStateValue deviceAndSpace = deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 });
    deviceAndSpace.color.colorSpaceObject = PDFObject::createName("DeviceGray");

    PDFContentStateValue pattern;
    pattern.kind = PDFContentStateValue::Kind::Color;
    pattern.color.patternObject = PDFObject::createReference(PDFObjectReference(1, 0));

    for (const PDFContentStateValue& value : { otherSpace, patternWithoutObject, deviceAndSpace, pattern, deviceColor("DeviceRGB", { 1.0, 0.0 }), deviceColor("Lab", { 1.0, 0.0, 0.0 }) })
    {
        for (const Property property : { Property::FillColor, Property::StrokeColor })
        {
            QString errorText;
            QVERIFY(!fixture.execute(Factory::createSetAppearance({ square }, { { property, value } }), &errorText));
            QVERIFY(!errorText.isEmpty());
            QCOMPARE(fixture.session()->getContainer(key), before);
        }
    }

    QVERIFY(fixture.execute(Factory::createSetAppearance({ square }, { { Property::FillColor, deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 }) } })));
    QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 rg 0 0 10 10 re f Q"));
}

void ContentCommandsTest::transformManyObjects()
{
    // Select All and a press of an arrow key: a command is linear in the objects it names
    // (pe0_fixes.md, decision 5). The commands, which change the objects in their place
    // (transform, set_appearance, set_transparency), had a loop over all pairs of the named
    // objects, each pair walking up the tree being changed: for 20 000 objects that are
    // 400 000 000 walks for each command.
    auto createPage = [](int count)
    {
        QByteArray content;
        for (int i = 0; i < count; ++i)
        {
            content += QByteArray::number(i) + " 0 1 1 re f ";
        }
        return plainDocument(content);
    };

    auto getPaths = [](const ContentCommandsFixture& fixture, const Key& key)
    {
        std::vector<PDFContentNodeReference> all;
        for (const PDFContentNodeId id : fixture.nodes(key, Kind::Path))
        {
            all.push_back(PDFContentNodeReference{ key, id });
        }
        return all;
    };

    {
        constexpr int COUNT = 20000;

        ContentCommandsFixture fixture(createPage(COUNT));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentContainerPointer before = session->getContainer(key);
        const std::vector<PDFContentNodeReference> all = getPaths(fixture, key);
        QCOMPARE(all.size(), size_t(COUNT));

        QElapsedTimer timer;
        timer.start();

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createTransform(all, matrix(1, 0, 0, 1, 1, 0)), &errorText), qPrintable(errorText));
        session->getUndoStack()->setClean();
        QVERIFY2(fixture.execute(Factory::createSetAppearance(all, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } }), &errorText), qPrintable(errorText));
        QVERIFY2(fixture.execute(Factory::createSetTransparency(all, std::nullopt, 0.5, std::nullopt), &errorText), qPrintable(errorText));

        // Half a second now; 17 to 30 seconds with the loop over the pairs
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < timeout(5000), qPrintable(QStringLiteral("%1 ms").arg(elapsed)));

        // The commands did their work on every square: the first and the last one are moved
        // and restyled
        for (const PDFContentNodeReference& square : { all.front(), all.back() })
        {
            const PDFContentNodePointer node = fixture.get(square);
            QVERIFY(node->getOverride() != nullptr);
            QCOMPARE(node->getOverride()->transform, matrix(1, 0, 0, 1, 1, 0));
            QCOMPARE(node->getOverride()->properties.size(), size_t(2));
        }
        while (session->undo().has_value()) { }
        QCOMPARE(session->getContainer(key), before);
    }

    // The commands, which change the order of the children of a parent or remove them, on
    // a page of 2 000 objects: the squares in the reverse order to the end of the page, then
    // all of them deleted
    {
        constexpr int COUNT = 2000;

        ContentCommandsFixture fixture(createPage(COUNT));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentContainerPointer before = session->getContainer(key);
        const std::vector<PDFContentNodeReference> all = getPaths(fixture, key);
        QCOMPARE(all.size(), size_t(COUNT));

        QElapsedTimer timer;
        timer.start();

        QString errorText;
        const std::vector<PDFContentNodeReference> reversed(all.crbegin(), all.crend());
        QVERIFY2(fixture.execute(Factory::createReorder(reversed, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.get(fixture.root(key))->getChildren().front()->getId(), all.back().node);
        QVERIFY2(fixture.execute(Factory::createGroup(all), &errorText), qPrintable(errorText));
        QVERIFY2(fixture.execute(Factory::createDelete(all), &errorText), qPrintable(errorText));

        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < timeout(5000), qPrintable(QStringLiteral("%1 ms").arg(elapsed)));

        QVERIFY(fixture.nodes(key, Kind::Path).empty());
        while (session->undo().has_value()) { }
        QCOMPARE(session->getContainer(key), before);
    }
}

void ContentCommandsTest::fontOfGraphicsStateDictionary()
{
    // A font set by a graphics state dictionary: /Font [font size] (ISO 32000-2, Table 57).
    // When a command has to set such a font again (R-2, R-3), it writes "/name size Tf" with
    // a resource name of the font itself (9.3.1: the operand of Tf names a font dictionary) -
    // never the array [font size] as a font resource (pe0_fixes.md, decision 2).
    //
    // /GSF << /Font [F1 9] >>: the font is the one the resources name /F1.
    PDFObject firstFont;
    auto createResources = [&firstFont](PDFDocumentBuilder* builder)
    {
        firstFont = Documents::addStandardFontObject(builder, "Helvetica");
        const PDFObject secondFont = Documents::addStandardFontObject(builder, "Courier");

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", firstFont }, { "F2", secondFont } }));
        resources.addEntry(PDFInplaceOrMemoryString("ExtGState"), Documents::createDictionaryObject({ { "GSF", Documents::createDictionaryObject({ { "Font", Documents::createArrayObject({ firstFont, PDFObject::createInteger(9) }) } }) } }));
        return resources;
    };

    // R-2: the first text goes behind the second one, where the font is /F2 12: its wrap sets
    // the font it saw, /F1 9
    {
        ContentCommandsFixture fixture(Documents::createDocument("/GSF gs BT (a) Tj ET /F2 12 Tf BT (b) Tj ET", createResources));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::TextObject, 0) }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/GSF gs /F2 12 Tf BT (b) Tj ET q /F1 9 Tf BT (a) Tj ET Q"));

        // No new font resource: the two fonts, /F1 is the font of the array
        const PDFContentResources resources = fixture.resources(key);
        QCOMPARE(resources.getEntries(PDFContentResourceCategory::Font).size(), size_t(2));
        QCOMPARE(resources.getObject(PDFContentResourceCategory::Font, "F1"), firstFont);
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // R-3: the marked content, which sets the font by gs, is deleted: a state run at its place
    // keeps the font for the text behind it
    {
        ContentCommandsFixture fixture(Documents::createDocument("/A BMC /GSF gs 0 0 5 5 re f EMC BT (x) Tj ET", createResources));
        const Key key = fixture.page();

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createDelete({ fixture.node(key, Kind::MarkedContent, 0) }), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/F1 9 Tf BT (x) Tj ET"));
        QCOMPARE(fixture.resources(key).getEntries(PDFContentResourceCategory::Font).size(), size_t(2));
    }
}

void ContentCommandsTest::unclosedNodes()
{
    // A node the content never closes (BT without ET, q without Q at the end of the content)
    // reaches to the end of the content with everything inside of it. It and the nodes inside
    // of it are not moved, transformed or reordered (pe0_fixes.md, decision 1; ISO 32000-2,
    // 8.2: no q, Q, cm or path between BT and ET). The place behind it - the end of its parent -
    // is a place as any other: content put there is written behind the closing operator the
    // content lacks, which the model adds.
    {
        ContentCommandsFixture fixture(fontDocument("0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj"));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentContainerPointer before = session->getContainer(key);
        const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);
        const PDFContentNodeReference text = fixture.node(key, Kind::TextObject, 0);
        const PDFContentNodeReference root = fixture.root(key);
        QVERIFY(before->isUnclosed(text.node));
        QVERIFY(!before->isUnclosed(square.node));

        const std::vector<PDFContentCommandPointer> refused =
        {
            Factory::createTransform({ text }, matrix(1, 0, 0, 1, 5, 5)),
            Factory::createReorder({ text }, root, 0, Factory::ReorderMode::FollowParent),
            Factory::createReorder({ text }, root, 0, Factory::ReorderMode::KeepLayerAndTag),
            Factory::createGroup({ text }),
            Factory::createGroup({ square, text }),
            Factory::createCopy({ text }, root, 0, QTransform())
        };
        for (size_t i = 0; i < refused.size(); ++i)
        {
            QString errorText;
            QVERIFY2(!fixture.execute(refused[i], &errorText), qPrintable(QString::number(i)));
            QVERIFY(!errorText.isEmpty());
            QVERIFY(!refused[i]->isRefusedAsUnchanged());
            QCOMPARE(session->getContainer(key), before);
        }

        // In front of the text everything is as on any page
        QString errorText;
        QVERIFY2(fixture.execute(Factory::createCopy({ square }, root, 1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f 0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj"));
        QVERIFY2(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 5, 5)), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 5 cm 0 0 5 5 re f Q 0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj"));
    }

    // The end of the page behind a text without its ET: a copy placed there (by -1 and by the
    // index of the end) stands behind the ET the model adds. A square reads no text state, so
    // it needs nothing of the font the text leaves behind.
    for (const PDFInteger index : { PDFInteger(-1), PDFInteger(2) })
    {
        ContentCommandsFixture fixture(fontDocument("0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        QCOMPARE(before->getAppendIndex(before->getRoot()->getId()), PDFInteger(2));

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), index, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj ET 0 0 5 5 re f"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // A move to the end of the page behind such a text
    {
        ContentCommandsFixture fixture(fontDocument("0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("BT /F1 12 Tf 10 10 Td (x) Tj ET 0 0 5 5 re f"));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }

    // The end of the page behind a group without its Q: "q 1 0 0 1 5 5 cm <square>". The state
    // behind the group is the state in front of its q (the model writes the Q), so
    //  - a square moved there from the beginning of the page saw the same state: no wrap;
    //  - a copy of the square inside of the group saw CTM [1 0 0 1 5 5] and stands in the
    //    identity now: its wrap sets M = CTM of the source x (CTM of the target)^-1 = [1 0 0 1 5 5]
    //    (R-2). The copy of a closed object inside of an unclosed one is no move of it.
    {
        ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f q 1 0 0 1 5 5 cm 0 0 10 10 re f"));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentContainerPointer before = session->getContainer(key);
        const PDFContentNodeReference group = fixture.node(key, Kind::Group, 0);
        QVERIFY(before->isUnclosed(group.node));

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createReorder({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 1 0 0 1 5 5 cm 0 0 10 10 re f Q 0 0 1 1 re f"));
        QVERIFY(isUndoRedoExact(fixture, key, before));

        // The group is closed in the new version of the page
        QVERIFY(!session->getContainer(key)->isUnclosed(group.node));

        QVERIFY(session->undo().has_value());
        QCOMPARE(session->getContainer(key), before);
        QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 1) }, fixture.root(key), -1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re f q 1 0 0 1 5 5 cm 0 0 10 10 re f Q q 1 0 0 1 5 5 cm 0 0 10 10 re f Q"));
    }

    // A group without its Q: the group itself is not moved, transformed or removed
    {
        ContentCommandsFixture fixture(plainDocument("q 1 0 0 rg 0 0 5 5 re f 9 9 1 1 re f"));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentContainerPointer before = session->getContainer(key);
        const PDFContentNodeReference group = fixture.node(key, Kind::Group, 0);
        QVERIFY(before->isUnclosed(group.node));

        const std::vector<PDFContentCommandPointer> refused =
        {
            Factory::createTransform({ group }, matrix(1, 0, 0, 1, 5, 5)),
            Factory::createReorder({ group }, fixture.root(key), 0, Factory::ReorderMode::FollowParent),
            Factory::createGroup({ group }),
            Factory::createCopy({ group }, fixture.root(key), 0, QTransform()),
            Factory::createUngroup(group)
        };
        for (size_t i = 0; i < refused.size(); ++i)
        {
            QString errorText;
            QVERIFY2(!fixture.execute(refused[i], &errorText), qPrintable(QString::number(i)));
            QVERIFY(!errorText.isEmpty());
            QVERIFY(!refused[i]->isRefusedAsUnchanged());
            QCOMPARE(session->getContainer(key), before);
        }
    }
}

void ContentCommandsTest::nodesInsideUnclosedNode_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("command");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("isGroupClosed");

    // A page, whose content stands in a q without its Q (a producer, which leaves the last Q
    // out): a state run (red fill), the square A at 0 and the square B at 9. The squares are
    // closed nodes; what a command makes of them stands inside of the group, which stays as it
    // is - no Q is written, as long as nothing is written behind the group.
    const QByteArray content("q 1 0 0 rg 0 0 5 5 re f 9 9 1 1 re f");

    // A move of A: its wrap (R-1) inside of the group
    QTest::newRow("transform") << content << 0 << QByteArray("q 1 0 0 rg q 1 0 0 1 5 5 cm 0 0 5 5 re f Q 9 9 1 1 re f") << false;

    // A restyle of B
    QTest::newRow("set appearance") << content << 1 << QByteArray("q 1 0 0 rg 0 0 5 5 re f q 0.5 g 9 9 1 1 re f Q") << false;

    // A delete of A
    QTest::newRow("delete") << content << 2 << QByteArray("q 1 0 0 rg 9 9 1 1 re f") << false;

    // B in front of A, among the closed children of the group
    QTest::newRow("reorder among the siblings") << content << 3 << QByteArray("q 1 0 0 rg 9 9 1 1 re f 0 0 5 5 re f") << false;

    // B to the beginning of the group, in front of the state run: it sets the red it saw (R-2)
    QTest::newRow("reorder in front of the state run") << content << 4 << QByteArray("q q 1 0 0 rg 9 9 1 1 re f Q 1 0 0 rg 0 0 5 5 re f") << false;

    // B out of the group to the beginning of the page: the same wrap, the group is untouched
    QTest::newRow("reorder in front of the group") << content << 5 << QByteArray("q 1 0 0 rg 9 9 1 1 re f Q q 1 0 0 rg 0 0 5 5 re f") << false;

    // A out of the group to the end of the page, behind the group: the model writes the Q of
    // the group in front of it, and A sets the red it saw inside
    QTest::newRow("reorder behind the group") << content << 6 << QByteArray("q 1 0 0 rg 9 9 1 1 re f Q q 1 0 0 rg 0 0 5 5 re f Q") << true;

    // A group of A and B is a closed group inside of the unclosed one; so is a group of A alone
    QTest::newRow("group") << content << 7 << QByteArray("q 1 0 0 rg q 0 0 5 5 re f 9 9 1 1 re f Q") << false;
    QTest::newRow("group of one") << content << 8 << QByteArray("q 1 0 0 rg q 0 0 5 5 re f Q 9 9 1 1 re f") << false;

    // A copy of A at the end of the group: inside of it, in the state A saw
    QTest::newRow("copy at the end") << content << 9 << QByteArray("q 1 0 0 rg 0 0 5 5 re f 9 9 1 1 re f 0 0 5 5 re f") << false;

    // A closed group inside of the unclosed one: it is removed (its square stays at its place),
    // and it is moved through its square
    const QByteArray nested("q 1 0 0 rg q 0 0 5 5 re f Q 9 9 1 1 re f");
    QTest::newRow("ungroup of a closed group") << nested << 10 << QByteArray("q 1 0 0 rg 0 0 5 5 re f 9 9 1 1 re f") << false;
    QTest::newRow("transform of a closed group") << nested << 11 << QByteArray("q 1 0 0 rg q q 1 0 0 1 5 5 cm 0 0 5 5 re f Q Q 9 9 1 1 re f") << false;
}

void ContentCommandsTest::nodesInsideUnclosedNode()
{
    // The nodes inside of a node the content does not close are nodes as any other
    // (pb0_core.md, S1 item 5): only the unclosed node itself is not moved (unclosedNodes).
    QFETCH(QByteArray, content);
    QFETCH(int, command);
    QFETCH(QByteArray, expected);
    QFETCH(bool, isGroupClosed);

    ContentCommandsFixture fixture(plainDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference root = fixture.root(key);
    const PDFContentNodeReference group = fixture.node(key, Kind::Group, 0);
    const PDFContentNodeReference inner = fixture.node(key, Kind::Group, 1);
    const PDFContentNodeReference first = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference second = fixture.node(key, Kind::Path, 1);
    QVERIFY(before->isUnclosed(group.node));
    QVERIFY(!before->isUnclosed(first.node));

    const Factory::ReorderMode follow = Factory::ReorderMode::FollowParent;
    const QTransform move = matrix(1, 0, 0, 1, 5, 5);

    PDFContentCommandPointer commandObject;
    switch (command)
    {
        case 0: commandObject = Factory::createTransform({ first }, move); break;
        case 1: commandObject = Factory::createSetAppearance({ second }, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } }); break;
        case 2: commandObject = Factory::createDelete({ first }); break;
        case 3: commandObject = Factory::createReorder({ second }, group, 1, follow); break;
        case 4: commandObject = Factory::createReorder({ second }, group, 0, follow); break;
        case 5: commandObject = Factory::createReorder({ second }, root, 0, follow); break;
        case 6: commandObject = Factory::createReorder({ first }, root, -1, follow); break;
        case 7: commandObject = Factory::createGroup({ first, second }); break;
        case 8: commandObject = Factory::createGroup({ first }); break;
        case 9: commandObject = Factory::createCopy({ first }, group, -1, QTransform()); break;
        case 10: commandObject = Factory::createUngroup(inner); break;
        case 11: commandObject = Factory::createTransform({ inner }, move); break;
        default: QFAIL("An unknown command of the test"); break;
    }

    QString errorText;
    QVERIFY2(fixture.execute(commandObject, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    // The group has its Q only when something is written behind it
    QCOMPARE(session->getContainer(key)->isUnclosed(group.node), !isGroupClosed);
    QVERIFY(isUndoRedoExact(fixture, key, before));
}

void ContentCommandsTest::placeBehindUnclosedClippingText()
{
    // A text with the text rendering mode 7 adds its glyphs to the clipping path at the end of
    // its text object (ISO 32000-2, 9.3.6). In "BT ... 7 Tr (A) Tj" without ET the text object
    // never ends, so nothing is clipped. A node placed behind the text would stand behind the
    // ET the model writes - and be clipped by glyphs, which clip nothing in the document. Such
    // a text is an open end (PDFContentNode::isOpenEnd), which stays the last child of its
    // parent, with a marked content, which ends with it (PDFContentContainer::getAppendIndex):
    // the end of the root is in front of it. So a copy to the end, or to any index behind the
    // square, lands in front of the text (the root is [square, text] or [square, marked
    // content]: the copy is the second child), and an order command, which puts the square to
    // the end, finds it there already - refused as unchanged.
    for (const QByteArray& content : { QByteArray("0 0 5 5 re f BT /F1 12 Tf 7 Tr (A) Tj"), QByteArray("0 0 5 5 re f /Artifact BMC BT /F1 12 Tf 7 Tr (A) Tj") })
    {
        ContentCommandsFixture fixture(fontDocument(content));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentContainerPointer before = session->getContainer(key);
        const PDFContentNodeReference root = fixture.root(key);
        const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);
        QVERIFY(before->isUnclosed(fixture.node(key, Kind::TextObject, 0).node));
        QCOMPARE(before->getRoot()->getChildren().size(), size_t(2));
        QCOMPARE(before->getAppendIndex(root.node), PDFInteger(1));

        for (const PDFContentCommandPointer& command : { Factory::createReorder({ square }, root, -1, Factory::ReorderMode::FollowParent),
                                                         Factory::createReorder({ square }, root, -1, Factory::ReorderMode::KeepLayerAndTag) })
        {
            QString errorText;
            QVERIFY(!fixture.execute(command, &errorText));
            QVERIFY(!errorText.isEmpty());
            QVERIFY(command->isRefusedAsUnchanged());
            QCOMPARE(session->getContainer(key), before);
        }

        // Nothing is written behind the text: it stays without its ET
        for (const PDFInteger index : { PDFInteger(-1), PDFInteger(1), PDFInteger(2) })
        {
            QString errorText;
            QVERIFY2(fixture.execute(Factory::createCopy({ square }, root, index, QTransform()), &errorText), qPrintable(errorText));
            QCOMPARE(fixture.written(key), ContentCommandsFixture::tokens("0 0 5 5 re f " + content));
            QVERIFY(session->undo().has_value());
            QCOMPARE(session->getContainer(key)->getVersion(), before->getVersion());
        }
    }

    // A group above the text takes the clip away with its Q: the place behind the group is
    // a place as any other ("q BT ... 7 Tr (A) Tj" - the model writes ET and Q)
    {
        ContentCommandsFixture fixture(fontDocument("0 0 5 5 re f q BT /F1 12 Tf 7 Tr (A) Tj"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), -1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f q BT /F1 12 Tf 7 Tr (A) Tj ET Q 0 0 5 5 re f"));
        QVERIFY(isUndoRedoExact(fixture, key, before));

        // Inside of the group, behind the text, the clip would be real: the text is the open end
        // of the group, the end of the group is in front of it. The copy lands there; nothing is
        // written behind the text, the group and the text stay without their Q and ET.
        fixture.session()->undo();
        QVERIFY2(fixture.execute(Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.node(key, Kind::Group, 0), -1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f q 0 0 5 5 re f BT /F1 12 Tf 7 Tr (A) Tj"));
    }

    // A container, in which the text is no open end: made by an editor, which leaves the mark
    // away (PDFContentNode::isOpenEnd is set by the tree builder only; a container comes into
    // the session from any command, PDFContentEditSession::setContainer). The end of the root
    // is behind the text then, and the commands refuse that place themselves
    // (PDFContentPlaceCommand::checkPlace): a copy and an order command to the end, not as
    // unchanged. In front of the text the copy is placed as above.
    {
        ContentCommandsFixture fixture(fontDocument("0 0 5 5 re f BT /F1 12 Tf 7 Tr (A) Tj"));
        PDFContentEditSession* session = fixture.session();
        const Key key = fixture.page();
        const PDFContentNodeReference root = fixture.root(key);
        const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);
        const PDFContentNodeReference text = fixture.node(key, Kind::TextObject, 0);

        PDFContentContainerEditor editor(session->getContainer(key));
        PDFContentNode::Data data = *editor.getNodeData(text.node);
        QVERIFY(data.isOpenEnd);
        data.isOpenEnd = false;
        QVERIFY(editor.setNodeData(text.node, std::move(data)));
        const PDFContentContainerPointer unmarked = editor.build();
        QVERIFY(unmarked->isUnclosed(text.node));
        QCOMPARE(unmarked->getAppendIndex(root.node), PDFInteger(2));

        std::shared_ptr<ContentCommandsTestCommand> setUnmarked = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Unmarked"));
        setUnmarked->onApply = [&unmarked](PDFContentEditSession* current) { current->setContainer(unmarked); };
        QVERIFY(fixture.execute(setUnmarked));
        QCOMPARE(session->getContainer(key), unmarked);

        for (const PDFContentCommandPointer& command : { Factory::createCopy({ square }, root, -1, QTransform()),
                                                         Factory::createCopy({ square }, root, 2, QTransform()),
                                                         Factory::createReorder({ square }, root, -1, Factory::ReorderMode::FollowParent),
                                                         Factory::createReorder({ square }, root, -1, Factory::ReorderMode::KeepLayerAndTag) })
        {
            QString errorText;
            QVERIFY(!fixture.execute(command, &errorText));
            QVERIFY(!errorText.isEmpty());
            QVERIFY(!command->isRefusedAsUnchanged());
            QCOMPARE(session->getContainer(key), unmarked);
        }

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createCopy({ square }, root, 1, QTransform()), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f 0 0 5 5 re f BT /F1 12 Tf 7 Tr (A) Tj"));
    }

    // An object, which is the only content of an unclosed marked content, does not take that
    // wrap along (an order command with the layer and the tag): refused; a drag takes it out
    {
        ContentCommandsFixture fixture(plainDocument("0 0 1 1 re f /Artifact BMC 0 0 5 5 re f"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference square = fixture.node(key, Kind::Path, 1);
        QVERIFY(before->isUnclosed(fixture.node(key, Kind::MarkedContent, 0).node));

        QString errorText;
        QVERIFY(!fixture.execute(Factory::createReorder({ square }, fixture.root(key), 0, Factory::ReorderMode::KeepLayerAndTag), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(fixture.session()->getContainer(key), before);

        QVERIFY2(fixture.execute(Factory::createReorder({ square }, fixture.root(key), 0, Factory::ReorderMode::FollowParent), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f 0 0 1 1 re f /Artifact BMC"));
    }
}

void ContentCommandsTest::copyStaysInFrontOfPinnedContent()
{
    // The content ends with an inline image without its end (everything behind BI ... ID is
    // image data for a reader) and with bytes, which are no operation ("(x": a string without
    // its end). Both must stay the last content (zadani_jadro.md 4.2: "new content goes in front
    // of it"): the root has two children, which stay last, and a copy placed at the end, behind
    // the first of them or behind both stands in front of both.
    const QByteArray content("0 0 1 1 re f BI /W 2 /H 2 ID abcd 2 w (x");

    for (const PDFInteger index : { PDFInteger(-1), PDFInteger(1), PDFInteger(2), PDFInteger(3) })
    {
        ContentCommandsFixture fixture(plainDocument(content));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodePointer root = before->getRoot();
        QCOMPARE(root->getChildren().size(), size_t(3));
        QCOMPARE(root->getChildren()[1]->getKind(), Kind::Opaque);
        QCOMPARE(root->getChildren()[2]->getKind(), Kind::Opaque);
        QCOMPARE(before->getAppendIndex(root->getId()), PDFInteger(1));

        QString errorText;
        const PDFContentCommandPointer command = Factory::createCopy({ fixture.node(key, Kind::Path, 0) }, fixture.root(key), index, QTransform());
        QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));

        const PDFContentNodePointer changedRoot = fixture.get(fixture.root(key));
        QCOMPARE(changedRoot->getChildren().size(), size_t(4));
        QCOMPARE(changedRoot->getChildren()[1]->getId(), command->getChange().addedNodes.front().node);
        QCOMPARE(changedRoot->getChildren()[2], root->getChildren()[1]);
        QCOMPARE(changedRoot->getChildren()[3], root->getChildren()[2]);
        QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re f 0 0 1 1 re f BI /W 2 /H 2 ID abcd 2 w (x"));
    }
}

void ContentCommandsTest::sessionDirtyPagesOfSharedForms()
{
    // Three pages:  page 1: /A Do    page 2: /B Do    page 3: a square
    // The forms:    A: /C Do         B: /C Do /B Do (it draws itself too)       C: a square
    //
    // A change of C makes the pages 1 and 2 dirty (each reaches C through its form), not the
    // page 3. The walk over the uses ends, though B draws itself.
    PDFObjectReference references[3];
    PDFDocumentBuilder builder;
    for (PDFObjectReference& reference : references)
    {
        reference = builder.addObject(PDFObject());
    }
    const PDFObject a = PDFObject::createReference(references[0]);
    const PDFObject b = PDFObject::createReference(references[1]);
    const PDFObject c = PDFObject::createReference(references[2]);
    setFormObject(&builder, references[0], "/C Do", { { "C", c } });
    setFormObject(&builder, references[1], "/C Do /B Do", { { "C", c }, { "B", b } });
    setFormObject(&builder, references[2], "0 0 10 10 re f", { });

    const std::vector<std::pair<QByteArray, Documents::DictionaryEntries>> pages = { { "/A Do", { { "A", a } } }, { "/B Do", { { "B", b } } }, { "0 0 1 1 re f", { } } };
    for (const auto& page : pages)
    {
        const PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject(page.second) } }) },
                                                                           { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), page.first) } }));
    }

    ContentCommandsFixture fixture(builder.build());
    PDFContentEditSession* session = fixture.session();
    QString errorText;
    for (PDFInteger i = 0; i < 3; ++i)
    {
        QVERIFY(fixture.page(i).isValid());
    }
    for (const PDFObjectReference& reference : references)
    {
        QVERIFY2(session->loadContainer(Key::createForm(reference), nullptr, &errorText), qPrintable(errorText));
    }
    QVERIFY(session->getDirtyPages().empty());

    const Key shared = Key::createForm(references[2]);
    const PDFContentCommandPointer command = Factory::createTransform({ fixture.node(shared, Kind::Path, 0) }, matrix(1, 0, 0, 1, 1, 1));
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0, 1 }));
    QCOMPARE(session->createSnapshot()->dirtyPages, std::vector<PDFInteger>({ 0, 1 }));

    // The place of the command is the first page, which reaches the form: the square is the
    // node 2 of C, drawn by the node 2 of A, drawn by the node 2 of the first page
    QCOMPARE(command->getPageIndex(), PDFInteger(0));
    QCOMPARE(command->toJson().value("ids").toArray(), QJsonArray({ "p1:2/2/2" }));

    // A page changed itself is dirty with them
    QVERIFY2(fixture.execute(Factory::createDelete({ fixture.node(session->getPageKey(2), Kind::Path, 0) }), &errorText), qPrintable(errorText));
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 0, 1, 2 }));

    // A change of B alone: the page 2 only
    while (session->undo().has_value()) { }
    QVERIFY(session->getDirtyPages().empty());
    const Key second = Key::createForm(references[1]);
    QVERIFY2(fixture.execute(Factory::createDelete({ fixture.node(second, Kind::FormUse, 0) }), &errorText), qPrintable(errorText));
    QCOMPARE(session->getDirtyPages(), std::vector<PDFInteger>({ 1 }));
}

// -------------------------------------------------------------------------------------------------
//  Fixes of the second review round (pe0_fixes2.md, package H2)
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::orderOfContentAtItsPlace_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QList<int>>("paths");      // The squares the command names (indices of the paths in the tree), in its order
    QTest::addColumn<int>("place");             // Index in the root: -1 Bring to Front, 0 Send to Back
    QTest::addColumn<bool>("isUnchanged");
    QTest::addColumn<QList<int>>("painted");    // The left edges of the squares in the order of the content afterwards

    // The squares 80, 60 (Span, MCID 1) and the paragraph P (MCID 0) of the squares 0, 20, 40
    // as the last content of the page (taggedDocument: P has /K 0)
    const QByteArray paragraphLast("80 0 10 10 re f /Span << /MCID 1 >> BDC 60 0 10 10 re f EMC /P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC");

    // G2-6: the squares 20 and 40 are painted last already, in the order of the command - an
    // order command changes nothing: no split of the sequence, no change of the structure
    QTest::newRow("front: the last two of a sequence") << paragraphLast << QList<int>({ 3, 4 }) << -1 << true << QList<int>({ 80, 60, 0, 20, 40 });
    QTest::newRow("front: all content of a sequence") << paragraphLast << QList<int>({ 2, 3, 4 }) << -1 << true << QList<int>({ 80, 60, 0, 20, 40 });

    // The first two squares of P at the beginning of the page are painted first already
    // (taggedContent: P of 0, 20, 40, Span of 60, the square 80)
    QTest::newRow("back: the first two of a sequence") << taggedContent() << QList<int>({ 0, 1 }) << 0 << true << QList<int>({ 0, 20, 40, 60, 80 });

    // The two squares of a group at the end are painted last already: they stay in the group
    QTest::newRow("front: the content of a group") << QByteArray("80 0 10 10 re f q 1 0 0 rg 0 0 10 10 re f 20 0 10 10 re f Q") << QList<int>({ 1, 2 }) << -1 << true << QList<int>({ 80, 0, 20 });

    // The order of the command differs from the order of the content: 40 stays the last
    // content, 20 is taken out of P behind it
    QTest::newRow("front: in the other order") << paragraphLast << QList<int>({ 4, 3 }) << -1 << false << QList<int>({ 80, 60, 0, 40, 20 });

    // The first square of P is painted first already, the square 80 is not: 80 goes behind 0,
    // in the order of the command (0 is not left in front of the place)
    QTest::newRow("back: the first of a sequence and another one") << taggedContent() << QList<int>({ 0, 4 }) << 0 << false << QList<int>({ 0, 80, 20, 40, 60 });

    // The same with the only content of the first sequence (Span of 60): its wrap is the place
    QTest::newRow("back: the only content of a sequence and another one")
        << QByteArray("/Span << /MCID 1 >> BDC 60 0 10 10 re f EMC /P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC 80 0 10 10 re f")
        << QList<int>({ 0, 4 }) << 0 << false << QList<int>({ 60, 80, 0, 20, 40 });

    // Content behind them: they move
    QTest::newRow("front: content behind") << (paragraphLast + " 90 0 10 10 re f") << QList<int>({ 3, 4 }) << -1 << false << QList<int>({ 80, 60, 0, 90, 20, 40 });
}

void ContentCommandsTest::orderOfContentAtItsPlace()
{
    QFETCH(QByteArray, content);
    QFETCH(QList<int>, paths);
    QFETCH(int, place);
    QFETCH(bool, isUnchanged);
    QFETCH(QList<int>, painted);

    ContentCommandsFixture fixture(taggedDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentStructureModelPointer original = session->getStructure();

    std::vector<PDFContentNodeReference> nodes;
    for (const int index : paths)
    {
        nodes.push_back(fixture.node(key, Kind::Path, size_t(index)));
        QVERIFY(nodes.back().node.isValid());
    }

    QString errorText;
    const PDFContentCommandPointer command = Factory::createReorder(nodes, fixture.root(key), place, Factory::ReorderMode::KeepLayerAndTag);
    const bool isExecuted = fixture.execute(command, &errorText);

    QList<int> squares;
    collectSquares(session->getContainer(key)->getRoot(), &squares);
    QCOMPARE(squares, painted);

    if (isUnchanged)
    {
        QVERIFY(!isExecuted);
        QVERIFY(!errorText.isEmpty());
        QVERIFY(command->isRefusedAsUnchanged());
        QCOMPARE(session->getContainer(key), before);
        QCOMPARE(session->getStructure(), original);
        QCOMPARE(session->getUndoStack()->getCount(), size_t(0));
        return;
    }

    QVERIFY2(isExecuted, qPrintable(errorText));

    // The reading order of the paragraph never changes by an order command
    QCOMPARE(readingOrder(fixture, key, "P"), QList<int>({ 0, 20, 40 }));
}

void ContentCommandsTest::copyNeverMakesPatternPaintItself()
{
    // G2-5: the editor never creates a form or a tiling pattern, which paints itself, also not
    // through a pattern (zadani_overeni.md 6.1; SECURITY_TODO.md item 19: a cell, which paints
    // itself, recurses without an end). The objects of the page:
    //
    //   Fm1: a square           P1: a tiling pattern, whose cell draws /Fm1 Do
    //   P2: a tiling pattern, whose cell is a square
    //   G: a form, which fills a square with P1
    //
    // The page: a square filled with P1, a square filled with P2, /Fm1 Do, /G Do. Nothing of it
    // paints itself. Refused: the square of P1 into Fm1 (Fm1 would paint P1, whose cell paints
    // Fm1) and into the cell of P1 (P1 would paint itself); the use of G into Fm1 and into the
    // cell of P1 (G paints P1). Copied: the square of P2 and the use of Fm1 into the cell of P1
    // (no way leads back to P1), the square of P2 into Fm1.
    PDFObjectReference formReference;
    PDFObjectReference patternReference;
    PDFObjectReference formOfPatternReference;
    const PDFDocument document = Documents::createDocument("/Pattern cs /P1 scn 0 0 10 10 re f /Pattern cs /P2 scn 20 0 10 10 re f 0 g /Fm1 Do /G Do BT /Pattern cs /P1 scn /F1 9 Tf (x) Tj ET",
                                                           [&formReference, &patternReference, &formOfPatternReference](PDFDocumentBuilder* builder)
    {
        const Documents::DictionaryEntries formEntries = { { "Type", PDFObject::createName("XObject") },
                                                           { "Subtype", PDFObject::createName("Form") },
                                                           { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) } };
        const Documents::DictionaryEntries patternEntries = { { "Type", PDFObject::createName("Pattern") },
                                                              { "PatternType", PDFObject::createInteger(1) },
                                                              { "PaintType", PDFObject::createInteger(1) },
                                                              { "TilingType", PDFObject::createInteger(1) },
                                                              { "BBox", Documents::createIntegerArrayObject({ 0, 0, 10, 10 }) },
                                                              { "XStep", PDFObject::createInteger(10) },
                                                              { "YStep", PDFObject::createInteger(10) } };

        Documents::DictionaryEntries entries = formEntries;
        entries.push_back({ "Resources", Documents::createDictionaryObject({ }) });
        const PDFObject form = Documents::addStreamObject(builder, entries, "0 0 5 5 re f");

        entries = patternEntries;
        entries.push_back({ "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "Fm1", form } }) } }) });
        const PDFObject pattern = Documents::addStreamObject(builder, entries, "/Fm1 Do");

        entries = patternEntries;
        entries.push_back({ "Resources", Documents::createDictionaryObject({ }) });
        const PDFObject squarePattern = Documents::addStreamObject(builder, entries, "1 0 0 rg 0 0 5 5 re f");

        entries = formEntries;
        entries.push_back({ "Resources", Documents::createDictionaryObject({ { "Pattern", Documents::createDictionaryObject({ { "P1", pattern } }) } }) });
        const PDFObject formOfPattern = Documents::addStreamObject(builder, entries, "/Pattern cs /P1 scn 0 0 1 1 re f");

        formReference = form.getReference();
        patternReference = pattern.getReference();
        formOfPatternReference = formOfPattern.getReference();

        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm1", form }, { "G", formOfPattern } }));
        resources.addEntry(PDFInplaceOrMemoryString("Pattern"), Documents::createDictionaryObject({ { "P1", pattern }, { "P2", squarePattern } }));
        resources.addEntry(PDFInplaceOrMemoryString("Font"), Documents::createDictionaryObject({ { "F1", Documents::addStandardFontObject(builder, "Helvetica") } }));
        return resources;
    });

    ContentCommandsFixture fixture(document);
    PDFContentEditSession* session = fixture.session();
    const Key pageKey = fixture.page();
    const Key formKey = Key::createForm(formReference);
    const Key cellKey = Key::createPatternCell(patternReference);
    QString errorText;
    QVERIFY2(session->loadContainer(formKey, nullptr, &errorText), qPrintable(errorText));

    const PDFContentNodeReference squareOfP1 = fixture.node(pageKey, Kind::Path, 0);
    const PDFContentNodeReference squareOfP2 = fixture.node(pageKey, Kind::Path, 1);
    const PDFContentNodeReference useOfFm1 = fixture.node(pageKey, Kind::FormUse, 0);
    const PDFContentNodeReference useOfG = fixture.node(pageKey, Kind::FormUse, 1);
    const PDFContentNodeReference textOfP1 = fixture.node(pageKey, Kind::TextObject, 0);
    QCOMPARE(fixture.get(useOfG)->getData().xobject, formOfPatternReference);
    QVERIFY(textOfP1.node.isValid());

    // The cell of P1 and the form G are not loaded first (read by their /Resources), then
    // loaded (read from their containers)
    for (const bool isLoaded : { false, true })
    {
        if (isLoaded)
        {
            QVERIFY2(session->loadContainer(cellKey, nullptr, &errorText), qPrintable(errorText));
            QVERIFY2(session->loadContainer(Key::createForm(formOfPatternReference), nullptr, &errorText), qPrintable(errorText));
        }

        for (const PDFContentNodeReference& node : { squareOfP1, useOfG, textOfP1 })
        {
            const PDFContentContainerPointer formBefore = session->getContainer(formKey);
            QVERIFY(!fixture.execute(Factory::createCopy({ node }, fixture.root(formKey), -1, QTransform()), &errorText));
            QVERIFY(!errorText.isEmpty());
            QCOMPARE(session->getContainer(formKey), formBefore);
        }
    }

    for (const PDFContentNodeReference& node : { squareOfP1, useOfG })
    {
        const PDFContentContainerPointer cellBefore = session->getContainer(cellKey);
        QVERIFY(!fixture.execute(Factory::createCopy({ node }, fixture.root(cellKey), -1, QTransform()), &errorText));
        QVERIFY(!errorText.isEmpty());
        QCOMPARE(session->getContainer(cellKey), cellBefore);
    }

    QVERIFY2(fixture.execute(Factory::createCopy({ squareOfP2 }, fixture.root(cellKey), -1, QTransform()), &errorText), qPrintable(errorText));
    QVERIFY2(fixture.execute(Factory::createCopy({ useOfFm1 }, fixture.root(cellKey), -1, QTransform()), &errorText), qPrintable(errorText));
    QVERIFY2(fixture.execute(Factory::createCopy({ squareOfP2 }, fixture.root(formKey), -1, QTransform()), &errorText), qPrintable(errorText));
}

void ContentCommandsTest::pathOfFormBehindDeepPath()
{
    // G2-8: the path of a form for the identifiers of a command (findPath). Two pages:
    //
    //   page 1: /C1 Do - the chain C1 -> C2 -> ... -> C31 -> G: G at the depth 32, the limit of
    //           the nesting (PDFContentEditSession::MAXIMUM_CONTAINER_NESTING), so nothing
    //           below G is reached from page 1
    //   page 2: /G Do - G at the depth 1, T (a square) at the depth 2
    //
    // A command over the square of T has its place on page 2: the square is the node 2 of T,
    // drawn by the node 2 of G, drawn by the node 2 of page 2. The walk of page 1 must not hide
    // G from page 2.
    constexpr int CHAIN_LENGTH = 31;
    QCOMPARE(PDFContentEditSession::MAXIMUM_CONTAINER_NESTING, CHAIN_LENGTH + 1);

    std::vector<PDFObjectReference> chain(CHAIN_LENGTH);
    PDFDocumentBuilder builder;
    for (PDFObjectReference& reference : chain)
    {
        reference = builder.addObject(PDFObject());
    }
    const PDFObjectReference g = builder.addObject(PDFObject());
    const PDFObjectReference t = builder.addObject(PDFObject());

    for (int i = 0; i < CHAIN_LENGTH; ++i)
    {
        const PDFObjectReference next = i + 1 < CHAIN_LENGTH ? chain[size_t(i + 1)] : g;
        setFormObject(&builder, chain[size_t(i)], "/N Do", { { "N", PDFObject::createReference(next) } });
    }
    setFormObject(&builder, g, "/T Do", { { "T", PDFObject::createReference(t) } });
    setFormObject(&builder, t, "0 0 10 10 re f", { });

    const std::vector<std::pair<QByteArray, Documents::DictionaryEntries>> pages = { { "/N Do", { { "N", PDFObject::createReference(chain.front()) } } },
                                                                                     { "/G Do", { { "G", PDFObject::createReference(g) } } } };
    for (const auto& page : pages)
    {
        const PDFObjectReference pageReference = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder.mergeTo(pageReference, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject(page.second) } }) },
                                                                           { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), page.first) } }));
    }

    ContentCommandsFixture fixture(builder.build());
    PDFContentEditSession* session = fixture.session();
    QString errorText;
    QVERIFY(fixture.page(0).isValid());
    QVERIFY(fixture.page(1).isValid());

    // G and T first (their first use is the one of page 2), then the chain of page 1
    QVERIFY2(session->loadContainer(Key::createForm(g), nullptr, &errorText), qPrintable(errorText));
    QVERIFY2(session->loadContainer(Key::createForm(t), nullptr, &errorText), qPrintable(errorText));
    for (const PDFObjectReference& reference : chain)
    {
        QVERIFY2(session->loadContainer(Key::createForm(reference), nullptr, &errorText), qPrintable(errorText));
    }

    const Key form = Key::createForm(t);
    const PDFContentCommandPointer command = Factory::createTransform({ fixture.node(form, Kind::Path, 0) }, matrix(1, 0, 0, 1, 1, 1));
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(command->getPageIndex(), PDFInteger(1));
    QCOMPARE(command->toJson().value("ids").toArray(), QJsonArray({ "p2:2/2/2" }));
}

// -------------------------------------------------------------------------------------------------
//  Block B, step B0, package S1: an undo step keeps the difference; many siblings; new objects
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::undoRedoMakesVersionsAgain()
{
    // A command keeps the difference of a container, not its versions (zadani_jadro.md 4.5).
    // Here nobody holds a version, which the session has replaced, so every undo and redo
    // makes its version again from the current one - and the page is written as it was before
    // and after every command. Nine commands, one of each kind, on three stroked squares A, B, C;
    // the contents are written by hand from the rules of the commands:
    //  - a move and a restyle are a wrap q ... Q with the matrix and the line width (R-1), the
    //    alpha is a graphics state dictionary the stream writer names /GS1 (the only one);
    //  - a reorder to the end moves the square with its wrap; a group is q ... Q around its
    //    objects; a copy of the moved A moved by (0, 30) has the matrix of both moves (5, 30).
    const QByteArray a("0 0 10 10 re S");
    const QByteArray b("20 0 10 10 re S");
    const QByteArray c("40 0 10 10 re S");
    const QByteArray movedA("q 1 0 0 1 5 0 cm 0 0 10 10 re S Q");
    const QByteArray wideB("q 4 w 20 0 10 10 re S Q");
    const QByteArray alphaC("q /GS1 gs 40 0 10 10 re S Q");
    const QByteArray copyOfA("q 1 0 0 1 5 30 cm 0 0 10 10 re S Q");

    const QList<QByteArray> contents = {
        a + ' ' + b + ' ' + c,                                                      // The page
        movedA + ' ' + b + ' ' + c,                                                 // 1: transform A
        movedA + ' ' + wideB + ' ' + c,                                             // 2: set_appearance B
        movedA + ' ' + wideB + ' ' + alphaC,                                        // 3: set_transparency C
        wideB + ' ' + alphaC + ' ' + movedA,                                        // 4: reorder A to the end
        "q " + wideB + ' ' + alphaC + " Q " + movedA,                               // 5: group B and C
        "q " + wideB + ' ' + alphaC + " Q " + movedA + ' ' + copyOfA,               // 6: copy A
        wideB + ' ' + alphaC + ' ' + movedA + ' ' + copyOfA,                        // 7: ungroup
        alphaC + ' ' + movedA + ' ' + copyOfA,                                      // 8: delete B
        QByteArray()                                                                // 9: clear_page
    };

    ContentCommandsFixture fixture(plainDocument(contents[0]));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentNodeReference root = fixture.root(key);
    const PDFContentNodeReference squareA = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference squareB = fixture.node(key, Kind::Path, 1);
    const PDFContentNodeReference squareC = fixture.node(key, Kind::Path, 2);
    QCOMPARE(fixture.written(key), contents[0]);

    // The versions are known by their numbers; the objects are watched, never held
    std::vector<quint64> versions = { session->getContainer(key)->getVersion() };
    std::vector<std::weak_ptr<const PDFContentContainer>> objects = { session->getContainer(key) };
    std::vector<size_t> operationCounts = { session->getContainer(key)->getOperations().getCount() };

    auto run = [&](const PDFContentCommandPointer& command, int step)
    {
        QString errorText;
        const bool isExecuted = fixture.execute(command, &errorText);
        if (!isExecuted || fixture.written(key) != contents[step])
        {
            qWarning("step %d: %s / %s", step, qPrintable(errorText), fixture.written(key).constData());
            return false;
        }

        versions.push_back(session->getContainer(key)->getVersion());
        objects.push_back(session->getContainer(key));
        operationCounts.push_back(session->getContainer(key)->getOperations().getCount());
        return true;
    };

    std::map<Property, PDFContentStateValue> lineWidth = { { Property::LineWidth, realValue(4) } };
    QVERIFY(run(Factory::createTransform({ squareA }, matrix(1, 0, 0, 1, 5, 0)), 1));
    QVERIFY(run(Factory::createSetAppearance({ squareB }, lineWidth), 2));
    QVERIFY(run(Factory::createSetTransparency({ squareC }, 0.5, std::nullopt, std::nullopt), 3));
    QVERIFY(run(Factory::createReorder({ squareA }, root, -1, Factory::ReorderMode::FollowParent), 4));
    QVERIFY(run(Factory::createGroup({ squareB, squareC }), 5));
    const PDFContentNodeReference group = fixture.node(key, Kind::Group, 0);
    QVERIFY(run(Factory::createCopy({ squareA }, root, -1, matrix(1, 0, 0, 1, 0, 30)), 6));
    QVERIFY(run(Factory::createUngroup(group), 7));
    QVERIFY(run(Factory::createDelete({ squareB }), 8));
    QVERIFY(run(Factory::createClearPage(key), 9));

    PDFContentUndoStack* stack = session->getUndoStack();
    QCOMPARE(stack->getCount(), size_t(9));
    const quint64 counterAfter = session->getNodeIdCounter();

    // Every replaced version is gone: the nine steps keep none of them alive
    for (size_t i = 0; i + 1 < objects.size(); ++i)
    {
        QVERIFY2(objects[i].expired(), qPrintable(QString::number(i)));
    }
    QVERIFY(!objects.back().expired());

    // Undo by undo: the page as it was before the command, and the same number of the version.
    // The version an undo has replaced is gone too: neither the step nor the session keeps it.
    for (int step = 9; step > 0; --step)
    {
        std::weak_ptr<const PDFContentContainer> replaced = session->getContainer(key);
        QVERIFY(session->undo().has_value());
        QCOMPARE(fixture.written(key), contents[step - 1]);
        QCOMPARE(session->getContainer(key)->getVersion(), versions[size_t(step - 1)]);
        QVERIFY2(replaced.expired(), qPrintable(QString::number(step)));
    }
    QVERIFY(!session->undo().has_value());

    // The page is the unchanged content again: nothing to write, no dirty page, the first
    // identifier of the session is free again, the three squares are the nodes they were
    QVERIFY(session->getContainer(key)->isUnchanged());
    QVERIFY(session->createSnapshot()->containers.empty());
    QVERIFY(session->getDirtyPages().empty());
    QCOMPARE(session->getNodeIdCounter(), PDFContentEditSession::FIRST_SESSION_NODE_ID);
    QCOMPARE(fixture.node(key, Kind::Path, 0), squareA);
    QCOMPARE(fixture.node(key, Kind::Path, 2), squareC);
    QCOMPARE(session->getContainer(key)->getNodeCount(), size_t(4));
    QVERIFY(fixture.get(squareA)->getOverride() == nullptr);

    // Redo by redo: the page as it was after the command
    for (int step = 1; step <= 9; ++step)
    {
        std::weak_ptr<const PDFContentContainer> replaced = session->getContainer(key);
        QVERIFY(session->redo().has_value());
        QCOMPARE(fixture.written(key), contents[step]);
        QCOMPARE(session->getContainer(key)->getVersion(), versions[size_t(step)]);
        QVERIFY2(replaced.expired(), qPrintable(QString::number(step)));
    }
    QVERIFY(!session->redo().has_value());
    QCOMPARE(session->getNodeIdCounter(), counterAfter);

    // The operations of a redo are the ones of the first run at their indices: the pool of
    // the last version has as many operations as it had (nothing was appended twice)
    QCOMPARE(session->getContainer(key)->getOperations().getCount(), operationCounts.back());

    // Back to the group (step 5) and a new command from there, over a version, which was made
    // again: C keeps its alpha and gets the matrix; one undo gives the group back
    for (int step = 9; step > 5; --step)
    {
        QVERIFY(session->undo().has_value());
    }
    QCOMPARE(fixture.written(key), contents[5]);
    QVERIFY(fixture.execute(Factory::createTransform({ squareC }, matrix(1, 0, 0, 1, 0, 7))));
    QCOMPARE(stack->getCount(), size_t(6));
    QCOMPARE(fixture.written(key), QByteArray("q " + wideB + " q 1 0 0 1 0 7 cm /GS1 gs 40 0 10 10 re S Q Q " + movedA));
    QVERIFY(session->undo().has_value());
    QCOMPARE(fixture.written(key), contents[5]);
    QVERIFY(session->redo().has_value());
    while (session->undo().has_value()) { }
    QCOMPARE(fixture.written(key), contents[0]);
    QVERIFY(session->getContainer(key)->isUnchanged());
}

void ContentCommandsTest::undoStepKeepsDifference()
{
    // The memory of an undo step is proportional to what its command changed, not to the page
    // (A1L1-2, A3L1-4: every step kept a version of the container with an index of all its
    // nodes and a root with all its children, so the limit of 10 000 steps bounded nothing).
    //
    // Forty commands, each over one square of a flat page, on a page of 1 000 squares and on
    // one of 20 000: (1) no step keeps a version of the container or its root alive; (2) the
    // items the steps keep (PDFContentUndoStack::getStoredItemCount) are the same number on both
    // pages. By hand, for a child of the root: a move or a restyle keeps the root (the node and
    // its data before and after: 3 items), the old square taken away and the new one put in -
    // 5 items; a reorder to the end the same 5; a delete the root and the square - 4 items.
    // Ten rounds of the four commands: 10 x (5 + 5 + 4 + 5) = 190 items.
    auto createPage = [](int count)
    {
        QByteArray content;
        for (int i = 0; i < count; ++i)
        {
            content += QByteArray::number(i) + " 0 1 1 re f ";
        }
        return plainDocument(content);
    };

    for (const int count : { 1000, 20000 })
    {
        ContentCommandsFixture fixture(createPage(count));
        PDFContentEditSession* session = fixture.session();
        PDFContentUndoStack* stack = session->getUndoStack();
        const Key key = fixture.page();
        const PDFContentNodeReference root = fixture.root(key);
        const std::vector<PDFContentNodeId> paths = fixture.nodes(key, Kind::Path);
        QCOMPARE(paths.size(), size_t(count));
        QCOMPARE(stack->getStoredItemCount(), size_t(0));

        const QByteArray original = fixture.written(key);
        std::weak_ptr<const PDFContentContainer> firstVersion = session->getContainer(key);

        for (int i = 0; i < 40; ++i)
        {
            std::weak_ptr<const PDFContentContainer> version = session->getContainer(key);
            std::weak_ptr<const PDFContentNode> versionRoot = session->getContainer(key)->getRoot();
            const size_t storedBefore = stack->getStoredItemCount();

            // A square from the middle of the page, another one for every command
            const PDFContentNodeReference square{ key, paths[size_t(count / 2 + i)] };
            PDFContentCommandPointer command;
            size_t expectedItems = 5;
            switch (i % 4)
            {
                case 0:
                    command = Factory::createTransform({ square }, matrix(1, 0, 0, 1, 0, 3));
                    break;

                case 1:
                    command = Factory::createSetAppearance({ square }, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } });
                    break;

                case 2:
                    command = Factory::createDelete({ square });
                    expectedItems = 4;
                    break;

                default:
                    command = Factory::createReorder({ square }, root, -1, Factory::ReorderMode::FollowParent);
                    break;
            }

            QString errorText;
            QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
            QCOMPARE(command->getStoredItemCount(), expectedItems);
            QCOMPARE(stack->getStoredItemCount(), storedBefore + expectedItems);

            // The version the command has replaced, and its root, are gone
            QVERIFY2(version.expired(), qPrintable(QString::number(i)));
            QVERIFY2(versionRoot.expired(), qPrintable(QString::number(i)));
        }

        QCOMPARE(stack->getCount(), size_t(40));
        QCOMPARE(stack->getStoredItemCount(), size_t(190));
        QVERIFY(firstVersion.expired());

        // The steps still undo everything
        while (session->undo().has_value()) { }
        QVERIFY(session->getContainer(key)->isUnchanged());
        QCOMPARE(fixture.written(key), original);
        QVERIFY(fixture.nodes(key, Kind::Path) == paths);

        // The undone steps keep what they kept (redo needs it); a stack without steps keeps nothing
        QCOMPARE(stack->getStoredItemCount(), size_t(190));
        stack->clear();
        QCOMPARE(stack->getStoredItemCount(), size_t(0));
    }
}

void ContentCommandsTest::manySiblings_data()
{
    QTest::addColumn<QByteArray>("prefix");
    QTest::addColumn<QByteArray>("suffix");
    QTest::addColumn<int>("parentKind");
    QTest::addColumn<bool>("isTagged");

    // The squares are children of the root (a flat page), of a group (a page wrapped in q ... Q
    // by its producer), of a marked content without a tag (an artifact, a layer), and of
    // a sequence of the structure (a tagged figure of many paths)
    QTest::newRow("root") << QByteArray() << QByteArray() << int(Kind::Root) << false;
    QTest::newRow("group") << QByteArray("q ") << QByteArray("Q") << int(Kind::Group) << false;
    QTest::newRow("marked content") << QByteArray("/Artifact BMC ") << QByteArray("EMC") << int(Kind::MarkedContent) << false;
    QTest::newRow("tagged sequence") << QByteArray("/P << /MCID 0 >> BDC ") << QByteArray("EMC /Span << /MCID 1 >> BDC 0 0 1 1 re f EMC") << int(Kind::MarkedContent) << true;
}

void ContentCommandsTest::manySiblings()
{
    // Select All on a page of 100 000 objects, then Bring to Front (here: all of them in the
    // reverse order to the end of their parent), Group and Delete (G2-3 of the second review
    // of block A). A command over K children of a parent with N children cost K x N: every
    // change made the node of the parent again (a copy of N pointers) and looked the index of
    // the child up by a scan - 10^10 steps for K = N = 100 000, minutes for every command.
    // The commands read the tree being changed through the queries of the editor now, which
    // cost the logarithm of N.
    //
    // The page takes about 250 MB with the versions the test holds (below 1 GB).
    QFETCH(QByteArray, prefix);
    QFETCH(QByteArray, suffix);
    QFETCH(int, parentKind);
    QFETCH(bool, isTagged);

    constexpr int COUNT = 100000;

    // The limit of each command in milliseconds (a command takes some tenths of a second)
    constexpr int LIMIT = 5000;

    QByteArray content = prefix;
    content.reserve(prefix.size() + suffix.size() + 20 * COUNT);
    for (int i = 0; i < COUNT; ++i)
    {
        content += QByteArray::number(i) + " 0 1 1 re f ";
    }
    content += suffix;

    ContentCommandsFixture fixture(isTagged ? taggedDocument(content) : plainDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(key.isValid());
    if (isTagged)
    {
        QVERIFY(session->loadStructure(nullptr));
    }

    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference parent = fixture.node(key, Kind(parentKind), 0);
    QVERIFY(parent.isValid());

    // The squares of the parent, in their order (the tagged page has one more in its second sequence)
    std::vector<PDFContentNodeReference> all;
    for (const PDFContentNodePointer& child : fixture.get(parent)->getChildren())
    {
        QCOMPARE(child->getKind(), Kind::Path);
        all.push_back(PDFContentNodeReference{ key, child->getId() });
    }
    QCOMPARE(all.size(), size_t(COUNT));

    QElapsedTimer timer;
    QString errorText;

    if (!isTagged)
    {
        // Every object to the front, the first one last: the order of the parent is reversed.
        // (In a tagged sequence an order command gives every object a sequence of its own:
        // that stays proportional to the sequence for each object and is not measured here.)
        const std::vector<PDFContentNodeReference> reversed(all.crbegin(), all.crend());
        timer.start();
        QVERIFY2(fixture.execute(Factory::createReorder(reversed, parent, -1, Factory::ReorderMode::KeepLayerAndTag), &errorText), qPrintable(errorText));
        const qint64 reorderTime = timer.elapsed();
        QVERIFY2(reorderTime < timeout(LIMIT), qPrintable(QStringLiteral("reorder: %1 ms").arg(reorderTime)));

        const std::vector<PDFContentNodePointer>& children = fixture.get(parent)->getChildren();
        QCOMPARE(children.size(), size_t(COUNT));
        QCOMPARE(children.front()->getId(), all.back().node);
        QCOMPARE(children[1]->getId(), all[all.size() - 2].node);
        QCOMPARE(children.back()->getId(), all.front().node);

        // All of them into one group, which stands in their parent alone
        timer.start();
        const PDFContentCommandPointer group = Factory::createGroup(all);
        QVERIFY2(fixture.execute(group, &errorText), qPrintable(errorText));
        const qint64 groupTime = timer.elapsed();
        QVERIFY2(groupTime < timeout(LIMIT), qPrintable(QStringLiteral("group: %1 ms").arg(groupTime)));

        QCOMPARE(fixture.get(parent)->getChildren().size(), size_t(1));
        const PDFContentNodePointer groupNode = fixture.get(group->getChange().addedNodes.front());
        QCOMPARE(groupNode->getKind(), Kind::Group);
        QCOMPARE(groupNode->getChildren().size(), size_t(COUNT));
        QCOMPARE(groupNode->getChildren().front()->getId(), all.back().node);

        // The group is undone: the objects are children of their parent again
        timer.start();
        QVERIFY(session->undo().has_value());
        const qint64 undoTime = timer.elapsed();
        QVERIFY2(undoTime < timeout(LIMIT), qPrintable(QStringLiteral("undo: %1 ms").arg(undoTime)));
        QCOMPARE(fixture.get(parent)->getChildren().size(), size_t(COUNT));
    }

    // All of them deleted
    timer.start();
    QVERIFY2(fixture.execute(Factory::createDelete(all), &errorText), qPrintable(errorText));
    const qint64 deleteTime = timer.elapsed();
    QVERIFY2(deleteTime < timeout(LIMIT), qPrintable(QStringLiteral("delete: %1 ms").arg(deleteTime)));

    if (isTagged)
    {
        // The sequence lost its last content: its wrap is gone, the other sequence is there
        QCOMPARE(fixture.nodes(key, Kind::Path).size(), size_t(1));
        QCOMPARE(fixture.nodes(key, Kind::MarkedContent).size(), size_t(1));
        QCOMPARE(fixture.written(key), QByteArray("/Span << /MCID 1 >> BDC 0 0 1 1 re f EMC"));
        QVERIFY(elementContent(session->getStructure(), "P").empty());
    }
    else
    {
        QVERIFY(fixture.nodes(key, Kind::Path).empty());
        QCOMPARE(fixture.written(key), QByteArray(prefix + suffix).simplified());
    }

    // Everything undone: the page as it was
    timer.start();
    while (session->undo().has_value()) { }
    const qint64 undoAllTime = timer.elapsed();
    QVERIFY2(undoAllTime < timeout(LIMIT), qPrintable(QStringLiteral("undo of all: %1 ms").arg(undoAllTime)));
    QCOMPARE(session->getContainer(key), before);
}

void ContentCommandsTest::newObjectsFollowUndoStack()
{
    // A new object of the session, which no command of the undo stack names any more, is
    // dropped (it would never be written, and the list would grow with the session). A move of
    // a square filled by a pattern copies the pattern (R-5: the copy has the matrix of the
    // pattern moved with the square), one new object for every move.
    PDFObjectReference patternReference;
    ContentCommandsFixture fixture(patternDocument("/Pattern cs /P1 scn 25 25 40 40 re f", &patternReference));
    PDFContentEditSession* session = fixture.session();
    PDFContentUndoStack* stack = session->getUndoStack();
    const Key key = fixture.page();
    const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);

    auto newObject = [](PDFInteger number) { return PDFObjectReference(number, PDFContentEditSession::NEW_OBJECT_GENERATION); };
    auto patternOfSquare = [&fixture, &square]() { return fixture.get(square)->getOverride()->properties.at(Property::FillColor).color.patternObject; };

    // Three presses of an arrow key are one step. Each press has copied the pattern of the
    // press before: the first two copies are objects of versions, which are gone - only the
    // third one is named, and it is the only new object of the session.
    QVERIFY(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(patternOfSquare(), PDFObject::createReference(newObject(1)));
    QVERIFY(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 1, 0))));
    QVERIFY(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 1, 0))));
    QCOMPARE(stack->getCount(), size_t(1));
    QCOMPARE(patternOfSquare(), PDFObject::createReference(newObject(3)));

    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(1));
    QVERIFY(session->getNewObject(newObject(1)).isNull());
    QVERIFY(session->getNewObject(newObject(2)).isNull());
    QVERIFY(session->getNewObject(newObject(3)).isStream());

    // The copy has the matrix of the pattern [2 0 0 2 3 4] moved by the three presses: (3, 0)
    QCOMPARE(overridePatternMatrix(fixture, square, Property::FillColor), Documents::createNumberArrayObject({ 2, 0, 0, 2, 6, 4 }));

    // A number is never given again: the next copy is the object 4
    stack->setClean();
    QVERIFY(fixture.execute(Factory::createTransform({ square }, matrix(1, 0, 0, 1, 0, 5))));
    QCOMPARE(stack->getCount(), size_t(2));
    QCOMPARE(patternOfSquare(), PDFObject::createReference(newObject(4)));
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(2));

    // An undone step keeps its object: redo needs it
    QVERIFY(session->undo().has_value());
    QCOMPARE(patternOfSquare(), PDFObject::createReference(newObject(3)));
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(2));
    QVERIFY(session->redo().has_value());
    QCOMPARE(patternOfSquare(), PDFObject::createReference(newObject(4)));
    QVERIFY(session->getNewObject(newObject(4)).isStream());

    // The undone step is dropped by a new command: its object goes with it. The object of the
    // step, which is still applied, stays.
    QVERIFY(session->undo().has_value());
    QVERIFY(fixture.execute(Factory::createSetTransparency({ square }, std::nullopt, 0.5, std::nullopt)));
    QCOMPARE(stack->getCount(), size_t(2));
    QVERIFY(session->getNewObject(newObject(4)).isNull());
    QVERIFY(session->getNewObject(newObject(3)).isStream());
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(1));
    QCOMPARE(patternOfSquare(), PDFObject::createReference(newObject(3)));

    // Both steps undone and dropped by a delete of the square: no new object is left
    while (session->undo().has_value()) { }
    QCOMPARE(session->createSnapshot()->newObjects.size(), size_t(1));
    QVERIFY(fixture.execute(Factory::createDelete({ square })));
    QCOMPARE(stack->getCount(), size_t(1));
    QVERIFY(session->createSnapshot()->newObjects.empty());
}

void ContentCommandsTest::structureUndoKeepsDifference()
{
    // A command, which changes the structure, keeps what the change touched in the model
    // (PDFContentStructureDifference), not the model before and the model after (A3L1-4: every
    // tagged delete kept a copy of the model of the whole document). Undo and redo make the
    // model again from the current one.
    //
    // The page of taggedDocument: the sequence 0 (/P) of three squares, the sequence 1 (/Span) of
    // one square, a square without a tag; the element P has /K 0, the element Span /K 1.
    ContentCommandsFixture fixture(taggedDocument());
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));

    // A model read from the same document: what the model of the session must equal after undo
    const PDFContentStructureModelPointer reference = PDFContentStructureModel::read(fixture.document().get(), nullptr);
    QVERIFY(reference != nullptr);
    QVERIFY(session->getStructure()->isEqual(*reference));

    std::weak_ptr<const PDFContentStructureModel> loadedModel = session->getStructure();
    const std::vector<PDFContentNodeId> squares = fixture.nodes(key, Kind::Path);
    QCOMPARE(squares.size(), size_t(5));

    // The only square of the sequence 1 is deleted: the sequence loses its wrap and its place
    // in the element Span (zadani_jadro.md 4.12)
    const PDFContentCommandPointer deleteSpan = Factory::createDelete({ PDFContentNodeReference{ key, squares[3] } });
    QVERIFY(fixture.execute(deleteSpan));
    QVERIFY(elementContent(session->getStructure(), "Span").empty());
    QVERIFY(elementContent(session->getStructure(), "P") == std::vector<PDFInteger>({ 0 }));
    QVERIFY(session->getStructure()->isModified());
    QCOMPARE(fixture.written(key), QByteArray("/P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC 80 0 10 10 re f"));

    // The step keeps no model: the model before is gone with the session's pointer to it
    QVERIFY(loadedModel.expired());

    // What it keeps, by hand: of the page the root (3 items) and the wrap taken away with its
    // square (1); of the structure the element Span (1) and the item of the parent tree of the
    // page (1)
    QCOMPARE(deleteSpan->getStoredItemCount(), size_t(6));

    // The three squares of the sequence 0 are deleted too: the element P has no content either
    std::weak_ptr<const PDFContentStructureModel> middleModel = session->getStructure();
    const PDFContentCommandPointer deleteParagraph = Factory::createDelete({ PDFContentNodeReference{ key, squares[0] }, PDFContentNodeReference{ key, squares[1] }, PDFContentNodeReference{ key, squares[2] } });
    QVERIFY(fixture.execute(deleteParagraph));
    QVERIFY(elementContent(session->getStructure(), "P").empty());
    QCOMPARE(fixture.written(key), QByteArray("80 0 10 10 re f"));
    QVERIFY(middleModel.expired());

    // Undo of both: the model is equal to the one read from the document, and not modified
    QVERIFY(session->undo().has_value());
    QVERIFY(elementContent(session->getStructure(), "P") == std::vector<PDFInteger>({ 0 }));
    QVERIFY(elementContent(session->getStructure(), "Span").empty());
    QVERIFY(session->undo().has_value());
    QVERIFY(elementContent(session->getStructure(), "Span") == std::vector<PDFInteger>({ 1 }));
    QVERIFY(session->getStructure()->isEqual(*reference));
    QVERIFY(!session->getStructure()->isModified());
    QVERIFY(session->createSnapshot()->isEmpty());
    QCOMPARE(fixture.written(key), ContentCommandsFixture::tokens(taggedContent()));

    // Redo of both, over models, which were made again: the second command finds the model
    // the first one has made
    QVERIFY(session->redo().has_value());
    QVERIFY(elementContent(session->getStructure(), "Span").empty());
    QVERIFY(elementContent(session->getStructure(), "P") == std::vector<PDFInteger>({ 0 }));
    QVERIFY(session->redo().has_value());
    QVERIFY(elementContent(session->getStructure(), "P").empty());
    QVERIFY(session->getStructure()->isModified());
    QCOMPARE(fixture.written(key), QByteArray("80 0 10 10 re f"));
    QVERIFY(!session->redo().has_value());

    // A model somebody holds is set as the object it is (a view, a snapshot of the work)
    const PDFContentStructureModelPointer held = session->getStructure();
    QVERIFY(session->undo().has_value());
    QVERIFY(session->getStructure() != held);
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getStructure(), held);
}

void ContentCommandsTest::commandWithoutEffectIsRefusedAsUnchanged()
{
    // A command, which would set what its objects already have, changes nothing: it is refused
    // as unchanged (PDFContentCommand::isRefusedAsUnchanged), the model stays, and there is no
    // undo step - a move by the identity matrix, a color, a line width or an alpha the object has.
    //
    // A gray square with a line 2 wide, and a text, which begins gray and sets red for its
    // second letter.
    ContentCommandsFixture fixture(fontDocument("0.5 g 2 w 0 0 10 10 re B BT /F1 12 Tf (A) Tj 1 0 0 rg (B) Tj ET"));
    PDFContentEditSession* session = fixture.session();
    PDFContentUndoStack* stack = session->getUndoStack();
    const Key key = fixture.page();
    const PDFContentContainerPointer before = session->getContainer(key);
    const PDFContentNodeReference square = fixture.node(key, Kind::Path, 0);
    const PDFContentNodeReference text = fixture.node(key, Kind::TextObject, 0);
    const PDFContentNodeReference letter = fixture.node(key, Kind::TextShow, 0);
    const PDFContentStateValue gray = deviceColor("DeviceGray", { 0.5 });
    const PDFContentStateValue darkGray = deviceColor("DeviceGray", { 0.25 });

    // The command is refused with a text, as unchanged, and the model and the undo stack stay
    auto isUnchanged = [&fixture, &key](const PDFContentCommandPointer& command)
    {
        const PDFContentContainerPointer container = fixture.session()->getContainer(key);
        const size_t count = fixture.session()->getUndoStack()->getCount();
        QString errorText;
        return !fixture.execute(command, &errorText) && !errorText.isEmpty() && command->isRefusedAsUnchanged() &&
               fixture.session()->getContainer(key) == container && fixture.session()->getUndoStack()->getCount() == count;
    };

    // The identity matrix: of the square, of the text, of both
    QVERIFY(isUnchanged(Factory::createTransform({ square }, QTransform())));
    QVERIFY(isUnchanged(Factory::createTransform({ text }, QTransform())));
    QVERIFY(isUnchanged(Factory::createTransform({ square, text }, matrix(1, 0, 0, 1, 0, 0))));
    QCOMPARE(session->getContainer(key), before);
    QCOMPARE(stack->getCount(), size_t(0));

    // The values of the state the square stands in: its gray, its line width, the solid line,
    // the alpha 1 and the blend mode Normal of a page (ISO 32000-2, 8.4.1, Table 51)
    PDFContentStateValue solid;
    solid.kind = PDFContentStateValue::Kind::Dash;
    PDFContentStateValue normal;
    normal.kind = PDFContentStateValue::Kind::Name;
    normal.name = "Normal";

    QVERIFY(isUnchanged(Factory::createSetAppearance({ square }, { { Property::FillColor, gray } })));
    QVERIFY(isUnchanged(Factory::createSetAppearance({ square }, { { Property::FillColor, gray }, { Property::StrokeColor, deviceColor("DeviceGray", { 0.0 }) }, { Property::LineWidth, realValue(2) } })));
    QVERIFY(isUnchanged(Factory::createSetAppearance({ square }, { { Property::DashPattern, solid } })));
    QVERIFY(isUnchanged(Factory::createSetTransparency({ square }, 1.0, 1.0, std::nullopt)));
    QVERIFY(isUnchanged(Factory::createSetTransparency({ square }, std::nullopt, std::nullopt, QByteArray("Normal"))));
    QCOMPARE(session->getContainer(key), before);

    // One value of several differs: the command is a change (the wrap sets all of them)
    QVERIFY(fixture.execute(Factory::createSetAppearance({ square }, { { Property::FillColor, darkGray }, { Property::LineWidth, realValue(2) } })));
    QCOMPARE(stack->getCount(), size_t(1));
    QCOMPARE(fixture.get(square)->getOverride()->properties.size(), size_t(2));

    // The values of its override now; the gray of the state is another color than the object has
    QVERIFY(isUnchanged(Factory::createSetAppearance({ square }, { { Property::FillColor, darkGray } })));
    QVERIFY(isUnchanged(Factory::createSetAppearance({ square }, { { Property::LineWidth, realValue(2) }, { Property::FillColor, darkGray } })));
    QVERIFY(fixture.execute(Factory::createSetAppearance({ square }, { { Property::FillColor, gray } })));
    QVERIFY(isUnchanged(Factory::createSetAppearance({ square }, { { Property::FillColor, gray } })));
    QVERIFY(session->undo().has_value());

    QVERIFY(fixture.execute(Factory::createSetTransparency({ square }, std::nullopt, 0.5, std::nullopt)));
    QVERIFY(isUnchanged(Factory::createSetTransparency({ square }, std::nullopt, 0.5, std::nullopt)));
    QVERIFY(session->undo().has_value());
    QCOMPARE(stack->getPosition(), size_t(1));

    // The text begins in the gray of the page, but sets red inside: the gray for the whole text
    // is a change (the red operator is suppressed, R-1) - once
    const PDFContentCommandPointer grayText = Factory::createSetAppearance({ text }, { { Property::FillColor, gray } });
    QString errorText;
    QVERIFY2(fixture.execute(grayText, &errorText), qPrintable(errorText));
    QVERIFY(fixture.get(text)->getOverride() != nullptr);
    QVERIFY(fixture.written(key).contains("q 0.5 g BT /F1 12 Tf (A) Tj (B) Tj ET Q"));
    QVERIFY(isUnchanged(Factory::createSetAppearance({ text }, { { Property::FillColor, gray } })));

    // Two objects, one of which has the value: the command changes the other one only
    const PDFContentNodePointer squareNode = fixture.get(square);
    QVERIFY2(fixture.execute(Factory::createSetAppearance({ square, text }, { { Property::FillColor, darkGray } }), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.get(square), squareNode);
    QCOMPARE(fixture.get(text)->getOverride()->properties.at(Property::FillColor).color.components, std::vector<PDFReal>({ 0.25 }));
    QVERIFY(isUnchanged(Factory::createSetAppearance({ square, text }, { { Property::FillColor, darkGray } })));

    // A rule of the content stays an ordinary refusal, also for a command without an effect:
    // a part of a text, an object, which does not exist
    for (const PDFContentCommandPointer& command : { Factory::createTransform({ letter }, QTransform()),
                                                     Factory::createTransform({ PDFContentNodeReference{ key, PDFContentNodeId{ 4242 } } }, QTransform()),
                                                     Factory::createSetAppearance({ letter }, { { Property::FillColor, gray } }) })
    {
        QVERIFY(!fixture.execute(command, &errorText));
        QVERIFY(!errorText.isEmpty());
        QVERIFY(!command->isRefusedAsUnchanged());
    }

    // Everything undone: the page as it was
    while (session->undo().has_value()) { }
    QCOMPARE(session->getContainer(key), before);
}

void ContentCommandsTest::stateValuesAreCompared()
{
    // PDFContentStateValue::isEqual: two values set a property to the same thing - the same
    // kind and the same value of that kind; the fields of the other kinds do not count.
    using Value = PDFContentStateValue;
    using ValueKind = PDFContentStateValue::Kind;

    QVERIFY(Value::isEqual(Value(), Value()));

    // A real number; the name of another kind in the same value is not looked at
    Value half = realValue(0.5);
    Value halfWithName = realValue(0.5);
    halfWithName.name = "Multiply";
    halfWithName.integer = 7;
    QVERIFY(Value::isEqual(half, halfWithName));
    QVERIFY(!Value::isEqual(half, realValue(0.25)));
    QVERIFY(!Value::isEqual(half, Value()));

    // The same number as another kind is another value
    Value integer;
    integer.kind = ValueKind::Integer;
    integer.integer = 1;
    Value one = realValue(1);
    one.integer = 1;
    QVERIFY(!Value::isEqual(integer, one));
    Value two = integer;
    two.integer = 2;
    QVERIFY(!Value::isEqual(integer, two));
    QVERIFY(Value::isEqual(integer, integer));

    Value yes;
    yes.kind = ValueKind::Boolean;
    yes.boolean = true;
    Value no = yes;
    no.boolean = false;
    QVERIFY(Value::isEqual(yes, yes));
    QVERIFY(!Value::isEqual(yes, no));

    Value multiply;
    multiply.kind = ValueKind::Name;
    multiply.name = "Multiply";
    Value screen = multiply;
    screen.name = "Screen";
    QVERIFY(Value::isEqual(multiply, multiply));
    QVERIFY(!Value::isEqual(multiply, screen));

    // A dash pattern: the array and the phase (ISO 32000-2, 8.4.3.6)
    auto createDash = [](const std::vector<PDFReal>& array, PDFReal phase)
    {
        PDFLineDashPattern pattern;
        pattern.setDashArray(array);
        pattern.setDashOffset(phase);
        return pattern;
    };

    Value dash;
    dash.kind = ValueKind::Dash;
    dash.dash = createDash({ 3.0, 2.0 }, 1.0);
    Value sameDash = dash;
    Value otherArray = dash;
    otherArray.dash = createDash({ 3.0, 2.5 }, 1.0);
    Value otherPhase = dash;
    otherPhase.dash = createDash({ 3.0, 2.0 }, 0.0);
    Value longerArray = dash;
    longerArray.dash = createDash({ 3.0, 2.0, 3.0 }, 1.0);
    QVERIFY(Value::isEqual(dash, sameDash));
    QVERIFY(!Value::isEqual(dash, otherArray));
    QVERIFY(!Value::isEqual(dash, otherPhase));
    QVERIFY(!Value::isEqual(dash, longerArray));

    // A color: the device space and the components; a color space object; a pattern
    QVERIFY(Value::isEqual(deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 }), deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 })));
    QVERIFY(!Value::isEqual(deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 }), deviceColor("DeviceRGB", { 1.0, 0.0, 0.5 })));
    QVERIFY(!Value::isEqual(deviceColor("DeviceGray", { 0.0 }), deviceColor("DeviceRGB", { 0.0, 0.0, 0.0 })));
    QVERIFY(!Value::isEqual(deviceColor("DeviceCMYK", { 0.0, 0.0, 0.0, 1.0 }), deviceColor("DeviceCMYK", { 0.0, 0.0, 0.0 })));

    Value spaceColor;
    spaceColor.kind = ValueKind::Color;
    spaceColor.color.colorSpaceObject = PDFObject::createReference(PDFObjectReference(7, 0));
    spaceColor.color.components = { 0.5 };
    Value otherSpaceColor = spaceColor;
    otherSpaceColor.color.colorSpaceObject = PDFObject::createReference(PDFObjectReference(8, 0));
    Value patternColor = spaceColor;
    patternColor.color.patternObject = PDFObject::createReference(PDFObjectReference(9, 0));
    QVERIFY(Value::isEqual(spaceColor, spaceColor));
    QVERIFY(!Value::isEqual(spaceColor, otherSpaceColor));
    QVERIFY(!Value::isEqual(spaceColor, patternColor));
    QVERIFY(Value::isEqual(patternColor, patternColor));

    // Two direct objects with the same small content are equal (a color space array written twice)
    Value directSpace = spaceColor;
    directSpace.color.colorSpaceObject = Documents::createArrayObject({ PDFObject::createName("CalGray"), Documents::createDictionaryObject({ { "Gamma", PDFObject::createReal(2.2) } }) });
    Value sameDirectSpace = spaceColor;
    sameDirectSpace.color.colorSpaceObject = Documents::createArrayObject({ PDFObject::createName("CalGray"), Documents::createDictionaryObject({ { "Gamma", PDFObject::createReal(2.2) } }) });
    Value otherDirectSpace = spaceColor;
    otherDirectSpace.color.colorSpaceObject = Documents::createArrayObject({ PDFObject::createName("CalGray"), Documents::createDictionaryObject({ { "Gamma", PDFObject::createReal(1.8) } }) });
    QVERIFY(Value::isEqual(directSpace, sameDirectSpace));
    QVERIFY(!Value::isEqual(directSpace, otherDirectSpace));

    // A font: the object and the size
    Value font;
    font.kind = ValueKind::Font;
    font.object = PDFObject::createReference(PDFObjectReference(11, 0));
    font.real = 12.0;
    Value otherSize = font;
    otherSize.real = 10.0;
    Value otherFont = font;
    otherFont.object = PDFObject::createReference(PDFObjectReference(12, 0));
    QVERIFY(Value::isEqual(font, font));
    QVERIFY(!Value::isEqual(font, otherSize));
    QVERIFY(!Value::isEqual(font, otherFont));

    // An object written by reference; a soft mask also by the matrix of its space (R-6)
    Value mask;
    mask.kind = ValueKind::Object;
    mask.object = PDFObject::createReference(PDFObjectReference(13, 0));
    Value movedMask = mask;
    movedMask.matrix = matrix(1, 0, 0, 1, 5, 0);
    Value otherMask = mask;
    otherMask.object = PDFObject::createReference(PDFObjectReference(14, 0));
    QVERIFY(Value::isEqual(mask, mask));
    QVERIFY(!Value::isEqual(mask, movedMask));
    QVERIFY(!Value::isEqual(mask, otherMask));

    // A document chooses the size of a direct object: two different arrays over the budget of
    // the comparison (64 values) count as different, the same shared array is equal at once
    const std::vector<PDFObject> items(100, PDFObject::createInteger(1));
    Value large = mask;
    large.object = Documents::createArrayObject(items);
    Value sameShared = large;
    Value equalLarge = mask;
    equalLarge.object = Documents::createArrayObject(items);
    QVERIFY(Value::isEqual(large, sameShared));
    QVERIFY(!Value::isEqual(large, equalLarge));
}

void ContentCommandsTest::copyNamesResourcesOfTarget()
{
    // A copy to another content names its resources there (zadani_jadro.md 4.4, R-4): a new
    // name is its prefix and the smallest free number - Fm for a form XObject, Im for an image,
    // Sh for a shading, MC for a property list. The data of the copied nodes say the names of
    // the target, as their operations do.
    //
    // Page 1: the form /X1, the image /X2, the shading /S1, the property list /PL1 of a marked
    // content. Page 2 has no resources.
    PDFObject form;
    PDFObject image;
    PDFObject shading;
    PDFObject propertyList;

    PDFDocumentBuilder builder;
    {
        form = Documents::addFormObject(&builder);
        image = Documents::addStreamObject(&builder, { { "Type", PDFObject::createName("XObject") },
                                                      { "Subtype", PDFObject::createName("Image") },
                                                      { "Width", PDFObject::createInteger(1) },
                                                      { "Height", PDFObject::createInteger(1) },
                                                      { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                      { "BitsPerComponent", PDFObject::createInteger(8) } }, QByteArray(1, 'x'));
        shading = Documents::addObject(&builder, Documents::createDictionaryObject({ { "ShadingType", PDFObject::createInteger(2) },
                                                                                     { "ColorSpace", PDFObject::createName("DeviceGray") },
                                                                                     { "Coords", Documents::createNumberArrayObject({ 0, 0, 1, 0 }) },
                                                                                     { "Function", Documents::createDictionaryObject({ { "FunctionType", PDFObject::createInteger(2) },
                                                                                                                                       { "Domain", Documents::createNumberArrayObject({ 0, 1 }) },
                                                                                                                                       { "N", PDFObject::createInteger(1) } }) } }));
        propertyList = Documents::addObject(&builder, Documents::createDictionaryObject({ { "Kind", PDFObject::createName("Sample") } }));

        const PDFObject sourceResources = Documents::createDictionaryObject({ { "XObject", Documents::createDictionaryObject({ { "X1", form }, { "X2", image } }) },
                                                                              { "Shading", Documents::createDictionaryObject({ { "S1", shading } }) },
                                                                              { "Properties", Documents::createDictionaryObject({ { "PL1", propertyList } }) } });

        const PDFObjectReference sourcePage = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder.mergeTo(sourcePage, Documents::createDictionaryObject({ { "Resources", sourceResources },
                                                                        { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "/X1 Do /X2 Do /S1 sh /Tag /PL1 BDC 0 0 1 1 re f EMC") } }));

        const PDFObjectReference targetPage = builder.appendPage(QRectF(0, 0, Documents::PAGE_SIZE, Documents::PAGE_SIZE));
        builder.mergeTo(targetPage, Documents::createDictionaryObject({ { "Resources", Documents::createDictionaryObject({ }) },
                                                                        { "Contents", Documents::addStreamObject(&builder, Documents::DictionaryEntries(), "0 0 5 5 re f") } }));
    }

    ContentCommandsFixture fixture(builder.build());
    PDFContentEditSession* session = fixture.session();
    const Key source = fixture.page(0);
    const Key target = fixture.page(1);
    QVERIFY(source.isValid() && target.isValid());

    const PDFContentNodeReference formUse = fixture.node(source, Kind::FormUse, 0);
    const PDFContentNodeReference imageUse = fixture.node(source, Kind::Image, 0);
    const PDFContentNodeReference shadingUse = fixture.node(source, Kind::Shading, 0);
    const PDFContentNodeReference markedContent = fixture.node(source, Kind::MarkedContent, 0);
    QVERIFY(formUse.isValid() && imageUse.isValid() && shadingUse.isValid() && markedContent.isValid());
    QCOMPARE(fixture.get(formUse)->getData().resourceName, QByteArray("X1"));
    QCOMPARE(fixture.get(markedContent)->getData().propertiesName, QByteArray("PL1"));

    const PDFContentCommandPointer command = Factory::createCopy({ formUse, imageUse, shadingUse, markedContent }, fixture.root(target), -1, QTransform());
    QString errorText;
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));

    // The page: the form is /Fm1 and the image /Im1 (each the first name of its prefix)
    QCOMPARE(fixture.written(target), QByteArray("0 0 5 5 re f /Fm1 Do /Im1 Do /Sh1 sh /Tag /MC1 BDC 0 0 1 1 re f EMC"));

    const PDFContentResources resources = fixture.resources(target);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::XObject, "Fm1"), form);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::XObject, "Im1"), image);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::Shading, "Sh1"), shading);
    QCOMPARE(resources.getObject(PDFContentResourceCategory::Properties, "MC1"), propertyList);
    QCOMPARE(resources.getEntries(PDFContentResourceCategory::XObject).size(), size_t(2));

    // The nodes of the copies name the resources of their page
    const PDFContentNodePointer formCopy = fixture.get(fixture.node(target, Kind::FormUse, 0));
    const PDFContentNodePointer imageCopy = fixture.get(fixture.node(target, Kind::Image, 0));
    const PDFContentNodePointer shadingCopy = fixture.get(fixture.node(target, Kind::Shading, 0));
    const PDFContentNodePointer markedCopy = fixture.get(fixture.node(target, Kind::MarkedContent, 0));
    QVERIFY(formCopy && imageCopy && shadingCopy && markedCopy);
    QCOMPARE(formCopy->getData().resourceName, QByteArray("Fm1"));
    QCOMPARE(formCopy->getData().xobject, form.getReference());
    QCOMPARE(imageCopy->getData().resourceName, QByteArray("Im1"));
    QCOMPARE(shadingCopy->getData().resourceName, QByteArray("Sh1"));
    QCOMPARE(markedCopy->getData().propertiesName, QByteArray("MC1"));
    QCOMPARE(markedCopy->getData().tag, QByteArray("Tag"));

    // The source keeps its names
    QVERIFY(session->getContainer(source)->isUnchanged());
    QCOMPARE(fixture.get(formUse)->getData().resourceName, QByteArray("X1"));

    // A second form copy uses the name the form has on the page; a copy inside of the source
    // page keeps the name of the page
    session->getUndoStack()->setClean();
    QVERIFY(fixture.execute(Factory::createCopy({ formUse }, fixture.root(target), 0, QTransform())));
    QVERIFY(fixture.written(target).startsWith("/Fm1 Do 0 0 5 5 re f"));
    QVERIFY(fixture.execute(Factory::createCopy({ formUse }, fixture.root(source), -1, QTransform())));
    QVERIFY(fixture.written(source).endsWith("EMC /X1 Do"));
    QCOMPARE(fixture.get(fixture.node(source, Kind::FormUse, 1))->getData().resourceName, QByteArray("X1"));

    // Undo takes the resources of the copies away again
    while (session->undo().has_value()) { }
    QVERIFY(session->getContainer(target)->isUnchanged());
    QVERIFY(session->getContainer(target)->getResources().getEntries(PDFContentResourceCategory::XObject).empty());
}

void ContentCommandsTest::undoDropsOperationsOfCommand()
{
    // R1-1 of the review of step B0: an undo, which makes the version before a command again
    // (nobody holds the old object any more), gives that version its own operations only. It
    // kept the operations of the undone command in its pool: the next command copied them into
    // its step, every Duplicate appended behind them, and the limit of the operations of a page
    // (MAXIMUM_OPERATIONS_OF_CONTAINER) was reached by "Duplicate, Undo" alone - while with
    // a snapshot holding the old version nothing of this happened.
    //
    // A page of 200 squares "i 0 1 1 re f": 400 operations (the operations re and f of each
    // square). Five rounds of: Duplicate (a copy of all squares to the end, with the identity
    // matrix: the 400 operations appended, no wrap), Undo, Redo, Undo, a move of one square, Undo.
    // Expected in every round, by hand:
    //  - after the Duplicate 800 operations and the content twice; after its Undo 400 again;
    //  - the Duplicate keeps the root (the node and its data before and after: 3 items), the
    //    200 places of the copies put in and the 400 operations appended: 603 items;
    //  - the move keeps 5 items (the root, the square taken away and put in -
    //    undoStepKeepsDifference) and appends no operation (its matrix is a wrap of the writer);
    //  - the undo stack keeps the step of the last command only (a command drops the undone one).
    // The same numbers, when a snapshot holds every version (an undo gives the old object back).
    constexpr int COUNT = 200;

    QByteArray content;
    for (int i = 0; i < COUNT; ++i)
    {
        content += QByteArray::number(i) + " 0 1 1 re f ";
    }

    for (const bool isHeld : { false, true })
    {
        ContentCommandsFixture fixture(plainDocument(content));
        PDFContentEditSession* session = fixture.session();
        PDFContentUndoStack* stack = session->getUndoStack();
        const Key key = fixture.page();
        const PDFContentNodeReference root = fixture.root(key);

        std::vector<PDFContentNodeReference> squares;
        for (const PDFContentNodeId id : fixture.nodes(key, Kind::Path))
        {
            squares.push_back(PDFContentNodeReference{ key, id });
        }
        QCOMPARE(squares.size(), size_t(COUNT));

        const QByteArray original = fixture.written(key);
        QCOMPARE(original, ContentCommandsFixture::tokens(content));
        const QByteArray duplicated = ContentCommandsFixture::tokens(content + content);

        std::vector<PDFContentContainerPointer> snapshot;
        auto hold = [&]()
        {
            if (isHeld)
            {
                snapshot.push_back(session->getContainer(key));
            }
        };
        auto operationCount = [&]() { return session->getContainer(key)->getOperations().getCount(); };
        QCOMPARE(operationCount(), size_t(2 * COUNT));

        for (int round = 0; round < 5; ++round)
        {
            const QByteArray text = QStringLiteral("held %1, round %2").arg(isHeld ? 1 : 0).arg(round).toLatin1();
            hold();

            const PDFContentCommandPointer duplicate = Factory::createCopy(squares, root, -1, QTransform());
            QString errorText;
            QVERIFY2(fixture.execute(duplicate, &errorText), qPrintable(errorText));
            hold();
            QVERIFY2(operationCount() == size_t(4 * COUNT), text.constData());
            QVERIFY2(duplicate->getStoredItemCount() == size_t(3 + 3 * COUNT), text.constData());
            QVERIFY2(stack->getStoredItemCount() == size_t(3 + 3 * COUNT), text.constData());
            QCOMPARE(fixture.written(key), duplicated);

            QVERIFY(session->undo().has_value());
            QVERIFY2(operationCount() == size_t(2 * COUNT), text.constData());
            QCOMPARE(fixture.written(key), original);

            QVERIFY(session->redo().has_value());
            QVERIFY2(operationCount() == size_t(4 * COUNT), text.constData());
            QCOMPARE(fixture.written(key), duplicated);
            QVERIFY(session->undo().has_value());
            QVERIFY2(operationCount() == size_t(2 * COUNT), text.constData());

            const PDFContentCommandPointer move = Factory::createTransform({ squares[100] }, matrix(1, 0, 0, 1, 0, 3));
            QVERIFY2(fixture.execute(move, &errorText), qPrintable(errorText));
            hold();
            QVERIFY2(move->getStoredItemCount() == size_t(5), text.constData());
            QVERIFY2(stack->getStoredItemCount() == size_t(5), text.constData());
            QVERIFY2(operationCount() == size_t(2 * COUNT), text.constData());

            QVERIFY(session->undo().has_value());
            QVERIFY2(operationCount() == size_t(2 * COUNT), text.constData());
            QCOMPARE(stack->getCount(), size_t(1));
        }

        // The page is the content of the document again: unchanged, written byte for byte
        QVERIFY(session->getContainer(key)->isUnchanged());
        QCOMPARE(fixture.written(key), original);
        QVERIFY(session->createSnapshot()->containers.empty());
    }
}

void ContentCommandsTest::setAppearanceOfInheritedValueInForm()
{
    // R1-2 of the review of step B0: a form inherits the graphics state of the place, where it
    // is drawn (ISO 32000-2, 8.10.1). The container of a form is built with the state of its
    // first use, so a property the form does not set itself has the value of that use in the
    // states of its nodes - a value the object has in that use only, not a value the object has
    // (the engine's rule: in a form an inherited property is unknown).
    //
    // The page draws the form /Fm1 twice, red and then blue moved by (100, 0); in front of it
    // a square in the black of the page. The form "0 0 10 10 re f 0 1 0 rg 20 0 10 10 re f":
    // a square A in the color of its use and a square B, which the form makes green.
    //  - A red: a change (the second use draws A blue) - the wrap q 1 0 0 rg ... Q (R-1);
    //  - B green: the value B has (the form's own operator sets it) - refused as unchanged;
    //  - A and B red: both change, A is not left out;
    //  - the square of the page black: the initial fill color of a page, DeviceGray 0 (8.4.1,
    //    Table 51), is a value the square has - refused as unchanged.
    const PDFDocument document = Documents::createDocument("0 0 20 20 re f 1 0 0 rg /Fm1 Do 0 0 1 rg 1 0 0 1 100 0 cm /Fm1 Do", [](PDFDocumentBuilder* builder)
    {
        const PDFObject form = Documents::addStreamObject(builder, { { "Type", PDFObject::createName("XObject") },
                                                                     { "Subtype", PDFObject::createName("Form") },
                                                                     { "BBox", Documents::createIntegerArrayObject({ 0, 0, 100, 100 }) } }, "0 0 10 10 re f 0 1 0 rg 20 0 10 10 re f");
        PDFDictionaryBuilder resources;
        resources.addEntry(PDFInplaceOrMemoryString("XObject"), Documents::createDictionaryObject({ { "Fm1", form } }));
        return resources;
    });

    ContentCommandsFixture fixture(document);
    PDFContentEditSession* session = fixture.session();
    const Key pageKey = fixture.page();
    const PDFContentNodeReference use = fixture.node(pageKey, Kind::FormUse, 0);
    QVERIFY(use.isValid());
    const Key formKey = Key::createForm(fixture.get(use)->getData().xobject);
    QString errorText;
    QVERIFY2(session->loadContainer(formKey, nullptr, &errorText), qPrintable(errorText));

    const PDFContentNodeReference squareA = fixture.node(formKey, Kind::Path, 0);
    const PDFContentNodeReference squareB = fixture.node(formKey, Kind::Path, 1);
    QVERIFY(fixture.get(squareA)->getSymbolicState()->isInherited(Property::FillColor));
    QVERIFY(!fixture.get(squareB)->getSymbolicState()->isInherited(Property::FillColor));
    QCOMPARE(fixture.written(formKey), QByteArray("0 0 10 10 re f 0 1 0 rg 20 0 10 10 re f"));

    const PDFContentStateValue red = deviceColor("DeviceRGB", { 1.0, 0.0, 0.0 });
    const PDFContentStateValue green = deviceColor("DeviceRGB", { 0.0, 1.0, 0.0 });

    const PDFContentCommandPointer redA = Factory::createSetAppearance({ squareA }, { { Property::FillColor, red } });
    QVERIFY2(fixture.execute(redA, &errorText), qPrintable(errorText));
    QVERIFY(!redA->isRefusedAsUnchanged());
    QCOMPARE(fixture.written(formKey), QByteArray("q 1 0 0 rg 0 0 10 10 re f Q 0 1 0 rg 20 0 10 10 re f"));
    QVERIFY(session->undo().has_value());

    const PDFContentContainerPointer formBefore = session->getContainer(formKey);
    const PDFContentCommandPointer greenB = Factory::createSetAppearance({ squareB }, { { Property::FillColor, green } });
    QVERIFY(!fixture.execute(greenB, &errorText));
    QVERIFY(greenB->isRefusedAsUnchanged());
    QCOMPARE(session->getContainer(formKey), formBefore);

    const PDFContentCommandPointer redBoth = Factory::createSetAppearance({ squareA, squareB }, { { Property::FillColor, red } });
    QVERIFY2(fixture.execute(redBoth, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(formKey), QByteArray("q 1 0 0 rg 0 0 10 10 re f Q 0 1 0 rg q 1 0 0 rg 20 0 10 10 re f Q"));
    QVERIFY(session->undo().has_value());

    // The page: its initial state is known
    const PDFContentContainerPointer pageBefore = session->getContainer(pageKey);
    const PDFContentCommandPointer blackOnPage = Factory::createSetAppearance({ fixture.node(pageKey, Kind::Path, 0) }, { { Property::FillColor, deviceColor("DeviceGray", { 0.0 }) } });
    QVERIFY(!fixture.execute(blackOnPage, &errorText));
    QVERIFY(blackOnPage->isRefusedAsUnchanged());
    QCOMPARE(session->getContainer(pageKey), pageBefore);
    QCOMPARE(session->getContainer(formKey), formBefore);
}

void ContentCommandsTest::reorderInOrderOfFixedPriorities()
{
    // R1-3 of the review of step B0: the children of a node in the editor are a treap, whose
    // priority was a fixed function of the identifier (the finalizer of SplitMix64). The
    // identifiers of a page are known (the builder numbers its nodes depth first), so one order
    // command - a script naming all children - put them in the order of decreasing priority and
    // made the treap one chain: that command and every later one on the page built the children
    // in N^2 / 2 steps (minutes for 100 000 children). The priority has a seed chosen once per
    // process now, so no order of the identifiers is known to be bad.
    //
    // A flat page of 100 000 squares: an order command naming all of them in the order of the
    // fixed priority, then ordinary commands. Each within the limit of manySiblings.
    constexpr int COUNT = 100000;
    constexpr int LIMIT = 5000;

    QByteArray content;
    content.reserve(20 * COUNT);
    for (int i = 0; i < COUNT; ++i)
    {
        content += QByteArray::number(i) + " 0 1 1 re f ";
    }

    ContentCommandsFixture fixture(plainDocument(content));
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    const PDFContentNodeReference root = fixture.root(key);

    std::vector<PDFContentNodeReference> squares;
    for (const PDFContentNodeId id : fixture.nodes(key, Kind::Path))
    {
        squares.push_back(PDFContentNodeReference{ key, id });
    }
    QCOMPARE(squares.size(), size_t(COUNT));

    // The priority without a seed, as the editor had it: the largest first
    auto fixedPriority = [](quint64 id)
    {
        quint64 value = id + 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    };
    std::vector<PDFContentNodeReference> ordered = squares;
    std::sort(ordered.begin(), ordered.end(), [&fixedPriority](const PDFContentNodeReference& left, const PDFContentNodeReference& right) { return fixedPriority(left.node.value) > fixedPriority(right.node.value); });

    QElapsedTimer timer;
    QString errorText;
    auto run = [&](const char* name, const PDFContentCommandPointer& command)
    {
        timer.start();
        const bool isExecuted = fixture.execute(command, &errorText);
        const qint64 time = timer.elapsed();
        if (!isExecuted || time >= timeout(LIMIT))
        {
            qWarning("%s: %lld ms, %s", name, time, qPrintable(errorText));
            return false;
        }
        return true;
    };

    QVERIFY(run("reorder", Factory::createReorder(ordered, root, -1, Factory::ReorderMode::KeepLayerAndTag)));
    {
        // The children are in the order of the command
        const std::vector<PDFContentNodePointer>& children = fixture.get(root)->getChildren();
        QCOMPARE(children.size(), size_t(COUNT));
        bool isInOrder = true;
        for (size_t i = 0; i < children.size(); ++i)
        {
            isInOrder = isInOrder && children[i]->getId() == ordered[i].node;
        }
        QVERIFY(isInOrder);
    }

    QVERIFY(run("transform", Factory::createTransform({ squares[10] }, matrix(1, 0, 0, 1, 0, 3))));
    QVERIFY(run("set_appearance", Factory::createSetAppearance({ squares[20] }, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } })));
    QVERIFY(run("delete", Factory::createDelete({ squares[30] })));
    QCOMPARE(fixture.get(root)->getChildren().size(), size_t(COUNT - 1));

    timer.start();
    QVERIFY(session->undo().has_value());
    QVERIFY(session->redo().has_value());
    const qint64 undoRedoTime = timer.elapsed();
    QVERIFY2(undoRedoTime < timeout(LIMIT), qPrintable(QStringLiteral("undo and redo: %1 ms").arg(undoRedoTime)));
}

void ContentCommandsTest::commandOnStructureReadAgain()
{
    // Request 1 of pb0s7_state.md: a command knows the structure model it was applied to by the
    // number of its version (PDFContentStructureModel::getVersion; reverted / reapplied give the
    // number of the version they make again), not by the object, which lives as long as somebody
    // holds it. The page of taggedDocument; the only square of the sequence 1 is deleted (the
    // sequence loses its wrap and its place in the element Span).
    ContentCommandsFixture fixture(taggedDocument());
    PDFContentEditSession* session = fixture.session();
    const Key key = fixture.page();
    QVERIFY(session->loadStructure(nullptr));

    const std::vector<PDFContentNodeId> squares = fixture.nodes(key, Kind::Path);
    QCOMPARE(squares.size(), size_t(5));
    const QByteArray original = fixture.written(key);
    const QByteArray deleted("/P << /MCID 0 >> BDC 0 0 10 10 re f 20 0 10 10 re f 40 0 10 10 re f EMC 80 0 10 10 re f");
    const quint64 versionLoaded = session->getStructure()->getVersion();

    const PDFContentCommandPointer deleteSpan = Factory::createDelete({ PDFContentNodeReference{ key, squares[3] } });
    QVERIFY(fixture.execute(deleteSpan));
    QCOMPARE(fixture.written(key), deleted);
    const quint64 versionDeleted = session->getStructure()->getVersion();
    QVERIFY(versionDeleted != versionLoaded);

    // Undo and redo, nobody holds a model: the models made again have the numbers of the
    // versions they stand for, the redo takes the stored versions
    std::weak_ptr<const PDFContentStructureModel> deletedModel = session->getStructure();
    QVERIFY(session->undo().has_value());
    QVERIFY(deletedModel.expired());
    QCOMPARE(session->getStructure()->getVersion(), versionLoaded);
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getStructure()->getVersion(), versionDeleted);
    QCOMPARE(fixture.written(key), deleted);
    QVERIFY(elementContent(session->getStructure(), "Span").empty());
    QVERIFY(session->undo().has_value());
    QCOMPARE(fixture.written(key), original);

    // A model read anew from the document: the same content, another version, and the model,
    // to which the command was applied, is gone. The command executed a second time does not
    // take the versions it stored for the other model: it is refused, nothing changes.
    std::weak_ptr<const PDFContentStructureModel> revertedModel = session->getStructure();
    std::shared_ptr<ContentCommandsTestCommand> readAgain = std::make_shared<ContentCommandsTestCommand>(QStringLiteral("Read again"));
    readAgain->onApply = [&fixture](PDFContentEditSession* current) { current->setStructure(PDFContentStructureModel::read(fixture.document().get(), nullptr)); };
    QVERIFY(fixture.execute(readAgain));
    const PDFContentStructureModelPointer readModel = session->getStructure();
    QVERIFY(readModel != nullptr);
    QVERIFY(readModel->getVersion() != versionLoaded);
    QVERIFY(elementContent(readModel, "Span") == std::vector<PDFInteger>({ 1 }));

    QString errorText;
    QVERIFY(!fixture.execute(deleteSpan, &errorText));
    QVERIFY(revertedModel.expired());
    QVERIFY(!errorText.isEmpty());
    QVERIFY(!deleteSpan->isRefusedAsUnchanged());
    QCOMPARE(session->getStructure(), readModel);
    QCOMPARE(fixture.written(key), original);
}

// -------------------------------------------------------------------------------------------------
//  Stage E1, fix package FX1: the painting of a path or of a text switched on and off
//  (set_appearance with "fillEnabled" / "strokeEnabled")
// -------------------------------------------------------------------------------------------------

void ContentCommandsTest::paintSwitchesOfPath_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<int>("fill");
    QTest::addColumn<int>("stroke");
    QTest::addColumn<QByteArray>("expected");

    // -1: the switch is not given; 0: off; 1: on. The painting operators of ISO 32000-2, 8.5.3.1,
    // Table 59, written by hand: f / f* fill, S strokes, B / B* both, n neither; s, b, b* close the
    // subpath first; W / W* (8.5.4) stand in front of the painting operator and stay.
    QTest::newRow("f, stroke on") << QByteArray("0 0 10 10 re f") << -1 << 1 << QByteArray("0 0 10 10 re B");
    QTest::newRow("f*, stroke on: even-odd kept") << QByteArray("0 0 10 10 re f*") << -1 << 1 << QByteArray("0 0 10 10 re B*");
    QTest::newRow("S, fill on: nonzero") << QByteArray("0 0 10 10 re S") << 1 << -1 << QByteArray("0 0 10 10 re B");
    QTest::newRow("B, fill off") << QByteArray("0 0 10 10 re B") << 0 << -1 << QByteArray("0 0 10 10 re S");
    QTest::newRow("B*, stroke off: even-odd kept") << QByteArray("0 0 10 10 re B*") << -1 << 0 << QByteArray("0 0 10 10 re f*");
    QTest::newRow("B, both off") << QByteArray("0 0 10 10 re B") << 0 << 0 << QByteArray("0 0 10 10 re n");
    QTest::newRow("n, fill on") << QByteArray("0 0 10 10 re n") << 1 << -1 << QByteArray("0 0 10 10 re f");
    QTest::newRow("n, both on") << QByteArray("0 0 10 10 re n") << 1 << 1 << QByteArray("0 0 10 10 re B");
    QTest::newRow("s, fill on: closed") << QByteArray("0 0 m 10 0 l 10 10 l s") << 1 << -1 << QByteArray("0 0 m 10 0 l 10 10 l b");
    QTest::newRow("b, fill off: closed") << QByteArray("0 0 m 10 0 l 10 10 l b") << 0 << -1 << QByteArray("0 0 m 10 0 l 10 10 l s");
    QTest::newRow("b*, stroke off") << QByteArray("0 0 m 10 0 l 10 10 l b*") << -1 << 0 << QByteArray("0 0 m 10 0 l 10 10 l f*");
    QTest::newRow("W f, stroke on: clip kept") << QByteArray("0 0 10 10 re W f") << -1 << 1 << QByteArray("0 0 10 10 re W B");
    QTest::newRow("W* B, fill off: clip kept") << QByteArray("0 0 10 10 re W* B") << 0 << -1 << QByteArray("0 0 10 10 re W* S");
    QTest::newRow("W f, fill off: a clipping path") << QByteArray("0 0 10 10 re W f") << 0 << -1 << QByteArray("0 0 10 10 re W n");
    QTest::newRow("W n named, fill on") << QByteArray("0 0 10 10 re W n") << 1 << -1 << QByteArray("0 0 10 10 re W f");
}

void ContentCommandsTest::paintSwitchesOfPath()
{
    QFETCH(QByteArray, content);
    QFETCH(int, fill);
    QFETCH(int, stroke);
    QFETCH(QByteArray, expected);

    ContentCommandsFixture fixture(plainDocument(content));
    const Key key = fixture.page();
    const PDFContentContainerPointer before = fixture.session()->getContainer(key);
    const PDFContentNodeReference path = fixture.node(key, Kind::Path);

    PDFContentPaintSwitches switches;
    if (fill >= 0)
    {
        switches.fill = fill > 0;
    }
    if (stroke >= 0)
    {
        switches.stroke = stroke > 0;
    }

    QString errorText;
    QVERIFY2(fixture.execute(Factory::createSetAppearance({ path }, { }, switches), &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(key), expected);

    // The node: the new painting, the fill rule, the closing and the clip of the source
    const PDFContentPathPaint& oldPaint = before->getNode(path.node)->getData().paint;
    const PDFContentPathPaint& newPaint = fixture.get(path)->getData().paint;
    QCOMPARE(newPaint.fill, fill >= 0 ? fill > 0 : oldPaint.fill);
    QCOMPARE(newPaint.stroke, stroke >= 0 ? stroke > 0 : oldPaint.stroke);
    QCOMPARE(newPaint.fillEvenOdd, oldPaint.fillEvenOdd);
    QCOMPARE(newPaint.closeBeforePaint, oldPaint.closeBeforePaint);
    QCOMPARE(newPaint.clip, oldPaint.clip);
    QVERIFY(fixture.get(path)->getOverride() == nullptr);

    // One step; undo gives the source, byte for byte
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(1));
    QVERIFY(isUndoRedoExact(fixture, key, before));
    QVERIFY(fixture.session()->undo().has_value());
    QCOMPARE(PDFContentStreamWriter::write(*fixture.session()->getContainer(key)).content, content);
}

void ContentCommandsTest::paintSwitchesUndoAndGroup()
{
    // A switch, which changes nothing, is refused as unchanged: the content stays byte for byte
    {
        ContentCommandsFixture fixture(plainDocument("0 0 10 10 re B"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        PDFContentPaintSwitches switches;
        switches.fill = true;
        switches.stroke = true;

        const PDFContentCommandPointer command = Factory::createSetAppearance({ fixture.node(key, Kind::Path) }, { }, switches);
        QString errorText;
        QVERIFY(!fixture.execute(command, &errorText));
        QVERIFY(command->isRefusedAsUnchanged());
        QCOMPARE(fixture.session()->getContainer(key), before);
        QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    }

    // Switched on with a color in one command: the painting operator changes, the color goes
    // into the wrap (R-1); without the color the fill is the one of the state of the place
    {
        ContentCommandsFixture fixture(plainDocument("0 0 1 rg 0 0 10 10 re S 20 0 10 10 re S"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        PDFContentPaintSwitches switches;
        switches.fill = true;

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::Path, 0) }, { { Property::FillColor, deviceColor("DeviceRGB", { 1, 0, 0 }) } }, switches), &errorText), qPrintable(errorText));
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::Path, 1) }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 1 rg q 1 0 0 rg 0 0 10 10 re B Q 20 0 10 10 re B"));
        QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(2));

        // Switched off again: the painting of the source, the path written by its geometry. The
        // same switch of the same object right after the last one is merged into its step.
        switches.fill = false;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::Path, 1) }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 1 rg q 1 0 0 rg 0 0 10 10 re B Q 20 0 10 10 re S"));
        QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(2));
        QCOMPARE(fixture.session()->getUndoStack()->getUndoStep()->commands.front()->toJson().value("fillEnabled"), QJsonValue(false));

        QVERIFY(fixture.session()->undo().has_value());
        QCOMPARE(fixture.written(key), QByteArray("0 0 1 rg q 1 0 0 rg 0 0 10 10 re B Q 20 0 10 10 re S"));
        QVERIFY(fixture.session()->undo().has_value());
        QVERIFY(!fixture.session()->undo().has_value());
        QCOMPARE(fixture.session()->getContainer(key), before);
        QCOMPARE(PDFContentStreamWriter::write(*before).content, QByteArray("0 0 1 rg 0 0 10 10 re S 20 0 10 10 re S"));
    }

    // A group gives the switch to its paths and texts (one command, one step); a clipping path
    // inside of it is left alone, an image too
    {
        ContentCommandsFixture fixture(fontDocument("q 0 0 10 10 re S 20 0 10 10 re W n BT /F1 12 Tf (A) Tj ET Q"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        PDFContentPaintSwitches switches;
        switches.stroke = false;
        switches.fill = true;

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::Group) }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 0 0 10 10 re f 20 0 10 10 re W n BT /F1 12 Tf (A) Tj ET Q"));
        QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(1));
        QVERIFY(isUndoRedoExact(fixture, key, before));
    }
}

void ContentCommandsTest::paintSwitchesOfText()
{
    // ISO 32000-2, 9.3.6, Table 104: 0 fill, 1 stroke, 2 fill and stroke, 3 neither. The mode goes
    // into the wrap of the text, its own Tr is suppressed (R-1). The text state outlives ET (9.3.1),
    // so the text behind it keeps the mode the first text leaves behind: the residual state run
    // sets it again (R-3), with the font the text has set (in the order of the properties).
    {
        ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf 1 Tr (A) Tj ET BT (B) Tj ET"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference first = fixture.node(key, Kind::TextObject, 0);
        PDFContentPaintSwitches switches;
        switches.fill = true;

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ first }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 2 Tr BT /F1 12 Tf (A) Tj ET Q /F1 12 Tf 1 Tr BT (B) Tj ET"));
        QCOMPARE(fixture.get(first)->getOverride()->properties.at(Property::TextRenderingMode).integer, PDFInteger(2));
        QVERIFY(isUndoRedoExact(fixture, key, before));

        // The stroke off: the mode of the wrap changes (the text paints the fill only)
        switches = PDFContentPaintSwitches();
        switches.stroke = false;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ first }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 0 Tr BT /F1 12 Tf (A) Tj ET Q /F1 12 Tf 1 Tr BT (B) Tj ET"));

        // The fill off too: neither (the invisible text of an OCR layer)
        switches = PDFContentPaintSwitches();
        switches.fill = false;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ first }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 3 Tr BT /F1 12 Tf (A) Tj ET Q /F1 12 Tf 1 Tr BT (B) Tj ET"));

        // Undo to the source, byte for byte
        while (fixture.session()->undo().has_value())
        {
        }
        QCOMPARE(fixture.session()->getContainer(key), before);
        QCOMPARE(PDFContentStreamWriter::write(*fixture.session()->getContainer(key)).content, QByteArray("BT /F1 12 Tf 1 Tr (A) Tj ET BT (B) Tj ET"));
    }

    // A text without its own Tr: the wrap ends the mode with Q, nothing is set again behind it
    {
        ContentCommandsFixture fixture(fontDocument("/F1 12 Tf BT (A) Tj ET BT (B) Tj ET"));
        const Key key = fixture.page();
        PDFContentPaintSwitches switches;
        switches.fill = false;
        switches.stroke = true;

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::TextObject, 0) }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("/F1 12 Tf q 1 Tr BT (A) Tj ET Q BT (B) Tj ET"));
    }

    // Shows of different modes, which get one mode: allowed; which keep different modes: refused
    {
        ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf (A) Tj 1 Tr (B) Tj ET"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference text = fixture.node(key, Kind::TextObject);

        PDFContentPaintSwitches switches;
        switches.fill = false;
        QString errorText;
        const PDFContentCommandPointer refused = Factory::createSetAppearance({ text }, { }, switches);
        QVERIFY(!fixture.execute(refused, &errorText));
        QVERIFY(!errorText.isEmpty());
        QVERIFY(!refused->isRefusedAsUnchanged());
        QCOMPARE(fixture.session()->getContainer(key), before);

        switches.fill = true;
        switches.stroke = true;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ text }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("q 2 Tr BT /F1 12 Tf (A) Tj (B) Tj ET Q /F1 12 Tf 1 Tr"));
    }

    // A text, which paints so already: refused as unchanged
    {
        ContentCommandsFixture fixture(fontDocument("BT /F1 12 Tf (A) Tj ET"));
        const Key key = fixture.page();
        PDFContentPaintSwitches switches;
        switches.fill = true;
        switches.stroke = false;

        const PDFContentCommandPointer command = Factory::createSetAppearance({ fixture.node(key, Kind::TextObject) }, { }, switches);
        QString errorText;
        QVERIFY(!fixture.execute(command, &errorText));
        QVERIFY(command->isRefusedAsUnchanged());
    }
}

void ContentCommandsTest::paintSwitchesRefusals()
{
    PDFContentPaintSwitches switches;
    switches.stroke = true;

    // A text, which adds to the clipping path (a change of its appearance, RE-4); a part of a text;
    // an Opaque node; an image (no painting to switch)
    const std::vector<std::pair<QByteArray, Kind>> refused = { { QByteArray("BT /F1 12 Tf 5 Tr (A) Tj ET"), Kind::TextObject },
                                                               { QByteArray("BT /F1 12 Tf (A) Tj ET"), Kind::TextShow },
                                                               { QByteArray("0 0 10 10 re f 1 2 xyz"), Kind::Opaque },
                                                               { QByteArray("q 0 0 10 10 re W n Q"), Kind::Group } };
    for (const auto& item : refused)
    {
        ContentCommandsFixture fixture(fontDocument(item.first));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference node = fixture.node(key, item.second);
        QVERIFY2(node.isValid(), item.first.constData());

        QString errorText;
        const PDFContentCommandPointer command = Factory::createSetAppearance({ node }, { }, switches);
        const bool isExecuted = fixture.execute(command, &errorText);
        QVERIFY2(!isExecuted, item.first.constData());
        QVERIFY2(!errorText.isEmpty(), item.first.constData());
        QCOMPARE(fixture.session()->getContainer(key), before);
    }

    // A text without its ET (an unclosed node): the switch goes the way of every other change of its
    // appearance - set_appearance does not refuse such a text (stage E0), the model writes the ET
    // it lacks inside of the wrap, and the residual state run keeps the font behind it (R-3).
    // (The comment of the engine on unclosed nodes, R-7, says no command gives one a wrap: the
    // refusal is a question to the coordinator, pe1fx1_state.md.)
    {
        ContentCommandsFixture fixture(fontDocument("0 0 5 5 re f BT /F1 12 Tf 10 10 Td (x) Tj"));
        const Key key = fixture.page();
        const PDFContentContainerPointer before = fixture.session()->getContainer(key);
        const PDFContentNodeReference text = fixture.node(key, Kind::TextObject);
        QVERIFY(before->isUnclosed(text.node));

        QString errorText;
        QVERIFY2(fixture.execute(Factory::createSetAppearance({ text }, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } }), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f q 0.5 g BT /F1 12 Tf 10 10 Td (x) Tj ET Q /F1 12 Tf"));
        QVERIFY(fixture.session()->undo().has_value());

        QVERIFY2(fixture.execute(Factory::createSetAppearance({ text }, { }, switches), &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(key), QByteArray("0 0 5 5 re f q 2 Tr BT /F1 12 Tf 10 10 Td (x) Tj ET Q /F1 12 Tf"));
    }

    // No property and no switch; a switch of the transparency does not exist (only the appearance)
    {
        ContentCommandsFixture fixture(plainDocument("0 0 10 10 re f"));
        const Key key = fixture.page();
        QString errorText;
        QVERIFY(!fixture.execute(Factory::createSetAppearance({ fixture.node(key, Kind::Path) }, { }, PDFContentPaintSwitches()), &errorText));
        QVERIFY(!errorText.isEmpty());
    }
}

void ContentCommandsTest::paintSwitchesMergeAndJson()
{
    ContentCommandsFixture fixture(plainDocument("0 0 1 1 re S 2 2 1 1 re S"));
    PDFContentEditSession* session = fixture.session();
    PDFContentUndoStack* stack = session->getUndoStack();
    const Key key = fixture.page();
    const PDFContentNodeReference first{ key, PDFContentNodeId{ 2 } };
    const PDFContentNodeReference second{ key, PDFContentNodeId{ 3 } };
    QCOMPARE(fixture.node(key, Kind::Path, 0), first);

    // A switch with a dragged color: the same fields of the same objects merge into one step,
    // the last values win; a command with other fields (the color alone) is a new step
    PDFContentPaintSwitches on;
    on.fill = true;
    QVERIFY(fixture.execute(Factory::createTransform({ second }, matrix(1, 0, 0, 1, 1, 0))));
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::FillColor, deviceColor("DeviceGray", { 0.25 }) } }, on)));
    const size_t stepCount = stack->getCount();
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::FillColor, deviceColor("DeviceGray", { 0.5 }) } }, on)));
    QCOMPARE(stack->getCount(), stepCount);
    QCOMPARE(fixture.written(key), QByteArray("q 0.5 g 0 0 1 1 re B Q q 1 0 0 1 1 0 cm 2 2 1 1 re S Q"));
    QCOMPARE(stack->getUndoStep()->commands.front()->toJson().value("fillEnabled"), QJsonValue(true));
    QCOMPARE(stack->getUndoStep()->commands.front()->toJson().value("fill").toObject().value("values").toArray(), QJsonArray({ 0.5 }));

    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { { Property::FillColor, deviceColor("DeviceGray", { 0.75 }) } })));
    QCOMPARE(stack->getCount(), stepCount + 1);
    PDFContentPaintSwitches off;
    off.fill = false;
    QVERIFY(fixture.execute(Factory::createSetAppearance({ first }, { }, off)));
    QCOMPARE(stack->getCount(), stepCount + 2);
    QCOMPARE(fixture.written(key), QByteArray("q 0.75 g 0 0 1 1 re S Q q 1 0 0 1 1 0 cm 2 2 1 1 re S Q"));

    // One undo takes the merged step back: the stroke of the source
    QVERIFY(session->undo().has_value());
    QVERIFY(session->undo().has_value());
    QVERIFY(session->undo().has_value());
    QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re S q 1 0 0 1 1 0 cm 2 2 1 1 re S Q"));

    // JSON: what a command writes, and an operation written by hand
    PDFContentPaintSwitches both;
    both.fill = true;
    both.stroke = false;
    const QJsonArray one({ QStringLiteral("p1:2") });
    QCOMPARE(Factory::createSetAppearance({ first }, { }, both)->toJson(), QJsonObject({ { "type", "set_appearance" }, { "ids", one }, { "fillEnabled", true }, { "strokeEnabled", false } }));
    QCOMPARE(Factory::createSetAppearance({ first }, { }, off)->toJson(), QJsonObject({ { "type", "set_appearance" }, { "ids", one }, { "fillEnabled", false } }));

    auto read = [session](const QJsonObject& operation, std::vector<QString>* errors) -> PDFContentCommandPointer
    {
        return Factory::fromJson(operation, session, QStringLiteral("operations.items[0]"), errors);
    };

    std::vector<QString> errors;
    const QJsonObject operation({ { "type", "set_appearance" }, { "ids", one }, { "fillEnabled", true }, { "strokeEnabled", false } });
    const PDFContentCommandPointer command = read(operation, &errors);
    QVERIFY(command && errors.empty());
    QCOMPARE(command->toJson(), operation);
    QVERIFY(fixture.execute(command));
    QCOMPARE(fixture.written(key), QByteArray("0 0 1 1 re f q 1 0 0 1 1 0 cm 2 2 1 1 re S Q"));

    // A switch, which is no boolean, is an error of its field
    for (const QJsonValue& value : { QJsonValue(1), QJsonValue(QStringLiteral("true")), QJsonValue(QJsonValue::Null) })
    {
        errors.clear();
        QVERIFY(!read(QJsonObject({ { "type", "set_appearance" }, { "ids", one }, { "strokeEnabled", value } }), &errors));
        QCOMPARE(errors.size(), size_t(1));
        QVERIFY2(errors.front().startsWith(QStringLiteral("operations.items[0].strokeEnabled: ")), qPrintable(errors.front()));
    }
}

QTEST_MAIN(ContentCommandsTest)

#include "tst_contentcommandstest.moc"
