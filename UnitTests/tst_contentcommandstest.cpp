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
// P03; GUI_overhaul/edit_tool/zadani_jadro.md 4.5, 4.6, zadani_overeni.md 6.1 - 6.3, chapter 8).
// The packages of the later stages add their own source files to this program.
//
// Placeholder of the skeleton: the single function checks only what has real code in the
// skeleton - the JSON type names, the plain types, the limits, the base class of the commands
// and the lifetime of a session - and never calls a function of the package (they are stubs
// ending the process). Package P03 replaces it by its tests. The expected values are written by
// hand from the plan, never taken from a run.

#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentundostack.h"
#include "pdfdocument.h"

#include <QtTest>
#include <QJsonObject>
#include <QWeakPointer>

#include <memory>

using namespace pdf;

/// A command of the test: it does nothing with a session, it only shows what the base class does
class ContentCommandsTestCommand : public PDFContentCommand
{
public:
    explicit ContentCommandsTestCommand(bool* isDestroyed) :
        m_isDestroyed(isDestroyed)
    {

    }

    virtual ~ContentCommandsTestCommand() override
    {
        *m_isDestroyed = true;
    }

    virtual bool apply(PDFContentEditSession* session, QString* errorText) override
    {
        Q_UNUSED(session);
        Q_UNUSED(errorText);

        PDFContentChange change;
        change.isStructureChanged = true;
        setChange(change);
        return true;
    }

    virtual void revert(PDFContentEditSession* session) override
    {
        Q_UNUSED(session);
        setChange(PDFContentChange());
    }

    virtual QString getText() const override { return QStringLiteral("Test"); }
    virtual PDFInteger getPageIndex() const override { return -1; }
    virtual QByteArray getType() const override { return QByteArray(PDFContentCommandFactory::TYPE_DELETE); }
    virtual QJsonObject toJson() const override { return QJsonObject(); }

private:
    bool* m_isDestroyed = nullptr;
};

class ContentCommandsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();
};

void ContentCommandsTest::skeletonPlainTypes()
{
    using Factory = PDFContentCommandFactory;
    using Key = PDFContentContainerKey;

    // ---- The JSON types of the commands of stage E0: the column "Typ JSON" of the table of
    //      zadani_jadro.md 4.5 (the names of the operations of a script of MCP) ----
    QCOMPARE(QByteArray(Factory::TYPE_DELETE), QByteArray("delete"));
    QCOMPARE(QByteArray(Factory::TYPE_TRANSFORM), QByteArray("transform"));
    QCOMPARE(QByteArray(Factory::TYPE_REORDER), QByteArray("reorder"));
    QCOMPARE(QByteArray(Factory::TYPE_COPY), QByteArray("copy"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_APPEARANCE), QByteArray("set_appearance"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_TRANSPARENCY), QByteArray("set_transparency"));
    QCOMPARE(QByteArray(Factory::TYPE_GROUP), QByteArray("group"));
    QCOMPARE(QByteArray(Factory::TYPE_UNGROUP), QByteArray("ungroup"));
    QCOMPARE(QByteArray(Factory::TYPE_CLEAR_PAGE), QByteArray("clear_page"));

    // The types of the later stages, which the table writes out (their files register them, the
    // names are fixed by the skeleton)
    QCOMPARE(QByteArray(Factory::TYPE_MAKE_CLIPPING_GROUP), QByteArray("make_clipping_group"));
    QCOMPARE(QByteArray(Factory::TYPE_RELEASE_CLIPPING_GROUP), QByteArray("release_clipping_group"));
    QCOMPARE(QByteArray(Factory::TYPE_ADD_PATH), QByteArray("add_path"));
    QCOMPARE(QByteArray(Factory::TYPE_EDIT_PATH), QByteArray("edit_path"));
    QCOMPARE(QByteArray(Factory::TYPE_PATH_OPERATION), QByteArray("path_operation"));
    QCOMPARE(QByteArray(Factory::TYPE_ADD_IMAGE), QByteArray("add_image"));
    QCOMPARE(QByteArray(Factory::TYPE_REPLACE_IMAGE), QByteArray("replace_image"));
    QCOMPARE(QByteArray(Factory::TYPE_CROP_IMAGE), QByteArray("crop_image"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_IMAGE_PROPERTIES), QByteArray("set_image_properties"));
    QCOMPARE(QByteArray(Factory::TYPE_ADD_TEXT), QByteArray("add_text"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_TEXT), QByteArray("set_text"));
    QCOMPARE(QByteArray(Factory::TYPE_REPLACE_TEXT), QByteArray("replace_text"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_TEXT_PROPERTIES), QByteArray("set_text_properties"));
    QCOMPARE(QByteArray(Factory::TYPE_MERGE_TEXTS), QByteArray("merge_texts"));
    QCOMPARE(QByteArray(Factory::TYPE_SPLIT_TEXT), QByteArray("split_text"));
    QCOMPARE(QByteArray(Factory::TYPE_CONVERT_TEXT_TO_PATHS), QByteArray("convert_text_to_paths"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_GLYPH_UNICODE), QByteArray("set_glyph_unicode"));
    QCOMPARE(QByteArray(Factory::TYPE_CONVERT_COLOR), QByteArray("convert_color"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_PRODUCTION), QByteArray("set_production"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_PATTERN_FILL), QByteArray("set_pattern_fill"));
    QCOMPARE(QByteArray(Factory::TYPE_EDIT_SHADING), QByteArray("edit_shading"));
    QCOMPARE(QByteArray(Factory::TYPE_MAKE_FORM_UNIQUE), QByteArray("make_form_unique"));
    QCOMPARE(QByteArray(Factory::TYPE_ADD_LAYER), QByteArray("add_layer"));
    QCOMPARE(QByteArray(Factory::TYPE_ASSIGN_LAYER), QByteArray("assign_layer"));
    QCOMPARE(QByteArray(Factory::TYPE_TAG), QByteArray("tag"));
    QCOMPARE(QByteArray(Factory::TYPE_SET_TAG_PROPERTIES), QByteArray("set_tag_properties"));

    // ---- A node of a container: valid only with a valid container and a valid node ----
    const Key page = Key::createPage(2, PDFObjectReference(12, 0));
    const Key form = Key::createForm(PDFObjectReference(5, 0));
    QVERIFY(!PDFContentNodeReference().isValid());
    QVERIFY((PDFContentNodeReference{ page, PDFContentNodeId{ 57 } }).isValid());
    QVERIFY(!(PDFContentNodeReference{ page, PDFContentNodeId{ 0 } }).isValid());
    QVERIFY(!(PDFContentNodeReference{ Key(), PDFContentNodeId{ 57 } }).isValid());

    // The order: the container first (pages before forms), the node then
    QVERIFY((PDFContentNodeReference{ page, PDFContentNodeId{ 99 } }) < (PDFContentNodeReference{ form, PDFContentNodeId{ 1 } }));
    QVERIFY((PDFContentNodeReference{ page, PDFContentNodeId{ 1 } }) < (PDFContentNodeReference{ page, PDFContentNodeId{ 2 } }));
    QVERIFY((PDFContentNodeReference{ form, PDFContentNodeId{ 2 } }) > (PDFContentNodeReference{ form, PDFContentNodeId{ 1 } }));
    QVERIFY((PDFContentNodeReference{ page, PDFContentNodeId{ 7 } }) == (PDFContentNodeReference{ page, PDFContentNodeId{ 7 } }));
    QVERIFY((PDFContentNodeReference{ page, PDFContentNodeId{ 7 } }) != (PDFContentNodeReference{ form, PDFContentNodeId{ 7 } }));

    // ---- A change: empty = no container, no node, no layers, no structure ----
    QVERIFY(PDFContentChange().isEmpty());
    PDFContentChange containerChange;
    containerChange.containers.push_back(page);
    QVERIFY(!containerChange.isEmpty());
    PDFContentChange addedChange;
    addedChange.addedNodes.push_back(PDFContentNodeReference{ page, PDFContentNodeId{ 57 } });
    QVERIFY(!addedChange.isEmpty());
    PDFContentChange removedChange;
    removedChange.removedNodes.push_back(PDFContentNodeReference{ page, PDFContentNodeId{ 57 } });
    QVERIFY(!removedChange.isEmpty());
    PDFContentChange changedChange;
    changedChange.changedNodes.push_back(PDFContentNodeReference{ page, PDFContentNodeId{ 57 } });
    QVERIFY(!changedChange.isEmpty());
    PDFContentChange layersChange;
    layersChange.isLayersChanged = true;
    QVERIFY(!layersChange.isEmpty());
    PDFContentChange structureChange;
    structureChange.isStructureChanged = true;
    QVERIFY(!structureChange.isEmpty());

    // ---- The limits: the safety limit of the undo stack is 10 000 commands (owner QE.12,
    //      zadani_overeni.md 6.2); forms inside of forms are loaded to the depth 32 (6.1) ----
    QCOMPARE(PDFContentUndoStack::MAXIMUM_COMMAND_COUNT, size_t(10000));
    QCOMPARE(PDFContentEditSession::MAXIMUM_CONTAINER_NESTING, 32);

    // The identifiers of the nodes created by commands start at 2^32: above every identifier of
    // the builder of any container, whenever it is loaded (the operations bound its nodes)
    QCOMPARE(PDFContentEditSession::FIRST_SESSION_NODE_ID, quint64(1) << 32);
    QCOMPARE(PDFContentEditSession::FIRST_SESSION_NODE_ID, Q_UINT64_C(4294967296));
    QVERIFY(PDFContentEditSession::FIRST_SESSION_NODE_ID > quint64(PDFContentContainer::MAXIMUM_OPERATION_COUNT));

    // ---- The base class of the commands: a command is not merged unless it says so, it tells
    //      the change of its last apply or revert, and it dies through the pointer to its base ----
    bool isFirstDestroyed = false;
    bool isSecondDestroyed = false;
    {
        PDFContentCommandPointer first = std::make_shared<ContentCommandsTestCommand>(&isFirstDestroyed);
        std::unique_ptr<PDFContentCommand> second(new ContentCommandsTestCommand(&isSecondDestroyed));
        QVERIFY(first->getChange().isEmpty());
        QVERIFY(!first->mergeWith(*second));
        QVERIFY(!second->mergeWith(*first));

        QVERIFY(first->apply(nullptr, nullptr));
        QVERIFY(!first->getChange().isEmpty());
        QVERIFY(first->getChange().isStructureChanged);
        first->revert(nullptr);
        QVERIFY(first->getChange().isEmpty());

        second.reset();
        QVERIFY(isSecondDestroyed);
        QVERIFY(!isFirstDestroyed);
    }
    QVERIFY(isFirstDestroyed);

    // ---- A session holds its document (zadani_overeni.md 6.3: the session holds the document
    //      by a shared pointer) and owns one undo stack for the whole tool ----
    PDFDocumentPointer document(new PDFDocument());
    const PDFDocument* documentAddress = document.data();
    QWeakPointer<PDFDocument> weakDocument = document;
    {
        QObject parent;
        PDFContentEditSession* session = new PDFContentEditSession(document, PDFCMSPointer(), &parent);
        QCOMPARE(session->parent(), &parent);
        QCOMPARE(session->getDocument(), documentAddress);

        PDFContentUndoStack* undoStack = session->getUndoStack();
        QVERIFY(undoStack != nullptr);
        QCOMPARE(session->getUndoStack(), undoStack);
        QCOMPARE(static_cast<const PDFContentEditSession*>(session)->getUndoStack(), static_cast<const PDFContentUndoStack*>(undoStack));

        // The caller drops its pointer: the document lives as long as the session
        document.clear();
        QVERIFY(!weakDocument.isNull());
        QCOMPARE(session->getDocument(), documentAddress);
    }
    QVERIFY(weakDocument.isNull());

    // A session without a parent and without color management is destroyed by its owner
    {
        PDFContentEditSession session(PDFDocumentPointer(new PDFDocument()), PDFCMSPointer(), nullptr);
        QVERIFY(session.getDocument() != nullptr);
        QVERIFY(session.parent() == nullptr);
    }
}

QTEST_MAIN(ContentCommandsTest)

#include "tst_contentcommandstest.moc"
