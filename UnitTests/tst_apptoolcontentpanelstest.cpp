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

// Edit Content, stage E1, package P12: the tabs Objects and Pages and the menus of the tool (zadani_gui.md 4.18,
// 4.20; zadani_overeni.md 8: the tree <-> the selection, dropMimeData returns false and the command comes from the event
// loop, the filter, the counts). Every test gives the context its own registry with the parts of P12 only. The expected
// rows, orders and boxes are read from the hand-written content of each test (ISO 32000-2, 8.2 - 8.5, 14.6, 14.7),
// never from a run.

#include "appdocumentfixture.h"
#include "apptoolshellfixture.h"
#include "contenttestdocuments.h"

#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentmenus.h"
#include "pdfappeditcontentpanels_objects.h"
#include "pdfappeditcontentpanels_pages.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolshell.h"
#include "pdfsettingdefinitions.h"
#include "pdfshortcutrepository.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include "pdfcontenteditsession.h"
#include "pdfcontentundostack.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_content.h"

#include <QAction>
#include <QElapsedTimer>
#include <QImage>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMimeData>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <memory>

using Context = pdf::PDFAppEditContentContext;
using Model = pdf::PDFAppEditContentObjectsModel;
using Registry = pdf::PDFAppEditContentRegistry;
using Ids = pdf::PDFAppEditContentActionIds;

/// Helpers of the test
class ContentPanelsHelper
{
public:
    ContentPanelsHelper() = delete;

    static int timeout(int milliseconds)
    {
        return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor();
    }

    /// A registry with the parts of P12: the tabs Objects and Pages, the actions of the menus
    static std::unique_ptr<Registry> createRegistry()
    {
        std::unique_ptr<Registry> registry = std::make_unique<Registry>();
        pdf::PDFAppEditContentObjectsPanel::registerParts(registry.get());
        pdf::PDFAppEditContentPagesPanel::registerParts(registry.get());
        pdf::PDFAppEditContentMenus::registerParts(registry.get());
        return registry;
    }

    /// The page of the rows (page [0 0 200 200]):
    ///  0 Group (q ... Q) of two rectangles; a state run "0 0 1 rg" (no row)
    ///  1 Text "AB" (the simple font /F1: the codes 65 and 66 are A and B of WinAnsiEncoding)
    ///  2 Path, a rectangle
    ///  3 Clipping group: the clipping path "re W n" and a rectangle
    ///  4 Text "BA" in the tagged sequence /P with the MCID 0 (one object: no row of the sequence, the badge P)
    ///  5 Tagged content /Table with the MCID 1: two paths of 2 and 3 segments
    ///  6 Layer content of the OCG "Layer 1" (/OC /L1): a rectangle
    ///  7 Text of two lines (two baselines: Td 0 -14)
    ///  8 Unknown content: the unknown operator "foo"
    static QByteArray objectsContent()
    {
        return QByteArrayLiteral("q 1 0 0 1 5 5 cm 10 10 20 20 re f 40 10 20 20 re f Q\n"
                                 "0 0 1 rg\n"
                                 "BT /F1 12 Tf 10 100 Td (AB) Tj ET\n"
                                 "100 100 20 20 re f\n"
                                 "q 0 0 50 50 re W n 10 10 30 30 re f Q\n"
                                 "/P <</MCID 0>> BDC BT /F1 12 Tf 10 150 Td (BA) Tj ET EMC\n"
                                 "/Table <</MCID 1>> BDC 1 1 m 50 1 l 60 1 l S 1 5 m 50 5 l 60 5 l 70 5 l S EMC\n"
                                 "/OC /L1 BDC 150 150 10 10 re f EMC\n"
                                 "BT /F1 12 Tf 10 180 Td (AB) Tj 0 -14 Td (BA) Tj ET\n"
                                 "1 2 foo\n");
    }

    static pdf::PDFDocumentPointer createObjectsDocument()
    {
        using D = pdftest::ContentTestDocuments;
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocument(objectsContent(), [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), D::createDictionaryObject({ { "F1", D::addSimpleFontObject(builder) } }));
            const pdf::PDFObject layer = D::addObject(builder, D::createDictionaryObject({ { "Type", pdf::PDFObject::createName("OCG") },
                                                                                          { "Name", pdf::PDFObject::createString("Layer 1") } }));
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Properties"), D::createDictionaryObject({ { "L1", layer } }));
            return resources;
        })));
    }

    /// The page of the orders: four squares 5 x 5 at x = y = 10, 20, 30, 40 and a state run "0 g" between the first two
    static pdf::PDFDocumentPointer createSquaresDocument()
    {
        using D = pdftest::ContentTestDocuments;
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocument(QByteArrayLiteral("10 10 5 5 re f 0 g 20 20 5 5 re f 30 30 5 5 re f 40 40 5 5 re f"),
                                                                              &D::createNoResources)));
    }

    /// A page of \p count squares 1 x 1 (one Path each)
    static pdf::PDFDocumentPointer createManyDocument(int count)
    {
        using D = pdftest::ContentTestDocuments;
        QByteArray content;
        content.reserve(count * 20);
        for (int i = 0; i < count; ++i)
        {
            content += QByteArray::number(i % 199) + ' ' + QByteArray::number((i / 199) % 199) + " 1 1 re f\n";
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocument(content, &D::createNoResources)));
    }

    /// \p pageCount pages [0 0 200 200], each with two rectangles
    static pdf::PDFDocumentPointer createPagesDocument(int pageCount)
    {
        using D = pdftest::ContentTestDocuments;
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            const pdf::PDFObject contents = D::addStreamObject(&builder, D::DictionaryEntries(), QByteArrayLiteral("10 10 50 50 re f 100 100 20 20 re f"));
            builder.mergeTo(page, D::createDictionaryObject({ { "Resources", D::createDictionaryObject({ }) }, { "Contents", contents } }));
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
    }

    static Model* model(const Context* context)
    {
        return qobject_cast<Model*>(context->getTabState(QString::fromLatin1(pdf::PDFAppEditContentTabEntry::TAB_OBJECTS)));
    }

    /// The model follows the version of the container of the context (its refresh runs from the event loop)
    static bool waitForModel(const Context* context)
    {
        const Model* objects = model(context);
        QElapsedTimer timer;
        timer.start();
        while (objects && context->getContainer() && objects->getContainer() != context->getContainer() && timer.elapsed() < timeout(30000))
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        return objects && context->getContainer() && objects->getContainer() == context->getContainer();
    }

    /// Shows the page, waits for its container and for the rows
    static bool showPage(Context* context, pdf::PDFInteger pageIndex)
    {
        QSignalSpy loaded(context, &Context::containerChanged);
        context->setPage(pageIndex);
        if (!context->getContainer() && !loaded.wait(timeout(30000)))
        {
            return false;
        }
        return waitForModel(context);
    }

    /// "Kind|Description|Badge" of the rows under \p parent
    static QStringList rows(const Model* model, const QModelIndex& parent = QModelIndex())
    {
        QStringList result;
        for (int row = 0; row < model->rowCount(parent); ++row)
        {
            const QModelIndex index = model->index(row, 0, parent);
            result << index.data(Model::KindTextRole).toString() + QLatin1Char('|') + index.data(Model::DescriptionRole).toString() + QLatin1Char('|') +
                      index.data(Model::BadgeRole).toString();
        }
        return result;
    }

    static pdf::PDFContentNodeReference node(const Model* model, int row, const QModelIndex& parent = QModelIndex())
    {
        return model->getNode(model->index(row, 0, parent));
    }

    /// The left edges of the boxes of the top rows (the squares of createSquaresDocument by their x)
    static QList<int> lefts(const Context* context)
    {
        QList<int> result;
        const Model* objects = model(context);
        const pdf::PDFContentContainerPointer container = context->getContainer();
        for (int row = 0; container && row < objects->rowCount(); ++row)
        {
            const QRectF box = pdf::PDFAppEditContentGeometryHelper::getNodeBoundingBox(*container, node(objects, row).node);
            result << qRound(box.left());
        }
        return result;
    }

    /// Runs the action and waits until the model follows the command (the action runs from the event loop)
    static bool runAction(Context* context, const char* actionId)
    {
        const quint64 version = context->getContainer() ? context->getContainer()->getVersion() : 0;
        if (!context->triggerAction(QString::fromLatin1(actionId)))
        {
            return false;
        }
        QElapsedTimer timer;
        timer.start();
        while (context->getContainer() && context->getContainer()->getVersion() == version && timer.elapsed() < timeout(5000))
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        const bool isChanged = waitForModel(context) && context->getContainer()->getVersion() != version;
        if (!isChanged)
        {
            qWarning() << "the action" << actionId << "changed nothing:" << context->getLastRefusalText();
        }
        return isChanged;
    }

    static void settle()
    {
        for (int i = 0; i < 5; ++i)
        {
            QCoreApplication::processEvents();
        }
    }

    /// The deliberate end of the objects of a test (retire, then the deferred deletion now)
    static void finish(QPointer<Context>& context, QObject* other = nullptr)
    {
        if (other)
        {
            pdf::PDFDeferredDeletion::retire(other);
        }
        if (context)
        {
            pdf::PDFDeferredDeletion::retire(context.data());
        }
        pdf::PDFDeferredDeletion::flush();
    }

    /// The ids of the actions of the menu in their order, "|" for a divider
    static QStringList menuRows(const QMenu* menu)
    {
        QStringList result;
        for (const QAction* action : menu->actions())
        {
            result << (action->isSeparator() ? QStringLiteral("|") : action->property(pdf::PDFAppEditContentMenus::ACTION_PROPERTY).toString());
        }
        return result;
    }

    static QAction* findMenuAction(const QMenu* menu, const char* actionId)
    {
        for (QAction* action : menu->actions())
        {
            if (action->property(pdf::PDFAppEditContentMenus::ACTION_PROPERTY).toString() == QLatin1String(actionId))
            {
                return action;
            }
        }
        return nullptr;
    }

    /// The page of the kinds and the counts of R7-10 (page [0 0 200 200]; ISO 32000-2, 8.5.2, 9.4, 14.11.3.2):
    ///  0 a closed triangle "m l l h": 3 drawn segments (h draws the third side)
    ///  1 a square, whose last l returns to the start: 3 + 0 (h draws nothing, the subpath is closed)
    ///  2 one curve "m c": 1 segment
    ///  3 a text rotated by Tm (0 1 -1 0) and "0 -14 Td": the second show is moved across the writing direction, 2 lines
    ///  4 two shows on one baseline ("100 0 Td" along the writing direction): 1 line
    ///  5 /OC with a name /Properties does not have: optional content all the same - Layer content, its name
    ///  6 a group, whose first object is a clipping path: a clipping group of 2 objects
    ///  7 a group, whose second object fills and clips (re W f): a clipping group too (the clip holds up to its Q)
    static QByteArray kindsContent()
    {
        return QByteArrayLiteral("0 0 m 50 0 l 25 40 l h f\n"
                                 "0 0 m 10 0 l 10 10 l 0 0 l h S\n"
                                 "0 0 m 10 10 20 10 30 0 c S\n"
                                 "BT /F1 12 Tf 0 1 -1 0 100 100 Tm (AB) Tj 0 -14 Td (BA) Tj ET\n"
                                 "BT /F1 12 Tf 10 150 Td (AB) Tj 100 0 Td (BA) Tj ET\n"
                                 "/OC /Missing BDC 10 10 5 5 re f EMC\n"
                                 "q 20 20 m 40 20 l 30 40 l h W n 0 0 100 100 re f Q\n"
                                 "q 0 0 10 10 re f 5 5 10 10 re W f Q\n");
    }

    static pdf::PDFDocumentPointer createDocumentWithFont(const QByteArray& content)
    {
        using D = pdftest::ContentTestDocuments;
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(D::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
        {
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), D::createDictionaryObject({ { "F1", D::addSimpleFontObject(builder) } }));
            return resources;
        })));
    }

    /// The developer switch of the application (the tool is registered only with it, zadani.md 11.2)
    static bool setDeveloperMode(pdf::PDFAppContext* context, bool isOn)
    {
        if (context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), isOn).isError())
        {
            return false;
        }
        return pdftest::AppToolShellFixture::waitFor([isOn]() { return pdf::PDFAppToolGUIContent::isContentToolRegistered() == isOn; });
    }

    /// The image of a row of the tree as the view paints it
    static QImage rowImage(pdf::PDFTreeView* tree, const QModelIndex& index)
    {
        return tree->viewport()->grab(tree->visualRect(index)).toImage().convertToFormat(QImage::Format_ARGB32);
    }

    /// The selected nodes of the rows of the root (in the order of the rows)
    static std::vector<pdf::PDFContentNodeReference> topNodes(const Model* model)
    {
        std::vector<pdf::PDFContentNodeReference> nodes;
        nodes.reserve(size_t(model->rowCount()));
        for (int row = 0; row < model->rowCount(); ++row)
        {
            nodes.push_back(node(model, row));
        }
        return nodes;
    }
};

class AppToolContentPanelsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // the model of the tree
    void rowsFollowTheContent();
    void rowsOfNestedGroupsAndClipping();
    void incrementalRemoveWithoutReset();
    void incrementalMoveAndChange();
    void largeChangeResets();
    void dropReturnsFalseAndCommandFollows();
    void dropRefusesInvalidTargets();
    void filterKeepsGroupsOfMatches();
    void lockAndHideAreNoCommands();
    void hundredThousandObjects();
    void kindsAndCountsOneRule();
    void rowTextsComputedOncePerNode();
    void transformOfManyRowsOneRange();
    void lockOfManyObjectsBoundedRows();
    void lockedObjectsLeftAlone();
    void sessionReplacedReconnects();

    // the panel Objects
    void treeFollowsSelection();
    void selectionFollowsTree();
    void filterFieldWaitsForTyping();
    void treeKeysAndToggles();
    void clippingBadgePaintedAndTold();
    void filteredTreeKeepsHiddenSelection();
    void selectAllOfHundredThousand();

    // the actions
    void deleteSelection();
    void selectAll();
    void orderActions();
    void groupAndUngroup();
    void hideAndLockTitles();
    void duplicate();
    void viewOptions();

    // the menus
    void contextMenuRows();
    void contextMenuQueuesAndCloses();
    void viewPanelRows();
    void shownKeyFollowsRebinding();
    void contextMenuOfManyObjects();

    // the panel Pages
    void pagesTilesAndRequest();
};

void AppToolContentPanelsTest::initTestCase()
{
    qRegisterMetaType<pdf::PDFContentNodeReference>();
    qRegisterMetaType<pdf::PDFAppEditContentNoticeRequest>();
}

void AppToolContentPanelsTest::rowsFollowTheContent()
{
    // zadani_gui.md 4.18: the rows in the order of the content stream, the kinds of the table, no row of a state run,
    // the tagged sequence of one object is the object with the tag as its badge
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    const Model* model = H::model(context);
    QVERIFY(model);

    QCOMPARE(H::rows(model).mid(0, 8), QStringList({ QStringLiteral("Group|2 objects|"),
                                                     QStringLiteral("Text|“AB”|"),
                                                     QStringLiteral("Path|Rectangle|"),
                                                     QStringLiteral("Clipping group|2 objects|"),
                                                     QStringLiteral("Text|“BA”|P"),
                                                     QStringLiteral("Tagged content|Table|"),
                                                     QStringLiteral("Layer content|Layer 1|"),
                                                     QStringLiteral("Text|Paragraph · 2 lines|") }));
    QCOMPARE(model->rowCount(), 9);
    QCOMPARE(model->index(8, 0).data(Model::KindTextRole).toString(), QStringLiteral("Unknown content"));

    // Every row without the filter: 9 top rows, 2 + 2 + 2 + 1 children
    QCOMPARE(model->getObjectCount(), 16);
    QCOMPARE(model->getNewObjectCount(), 0);
    QVERIFY(!model->isFiltered());

    // The row of the tagged text is the text object; its place is the marked content
    const pdf::PDFContentContainerPointer container = context->getContainer();
    const pdf::PDFContentNodeReference text = H::node(model, 4);
    QCOMPARE(container->getNode(text.node)->getKind(), pdf::PDFContentNodeKind::TextObject);
    const pdf::PDFContentNodeId place = Model::getPlaceId(*container, text.node);
    QCOMPARE(container->getNode(place)->getKind(), pdf::PDFContentNodeKind::MarkedContent);
    QCOMPARE(model->getIndex(pdf::PDFContentNodeReference{ text.container, place }), model->index(4, 0));
    QCOMPARE(model->getIndex(text), model->index(4, 0));

    // Unknown content cannot be selected; the others can
    QVERIFY(!model->index(8, 0).data(Model::IsSelectableRole).toBool());
    QVERIFY(!(model->flags(model->index(8, 0)) & Qt::ItemIsSelectable));
    QVERIFY(model->flags(model->index(2, 0)) & Qt::ItemIsSelectable);

    // A node of a state run has no row
    const pdf::PDFContentNodePointer root = container->getRoot();
    bool hasStateRun = false;
    for (const pdf::PDFContentNodePointer& child : root->getChildren())
    {
        if (child->getKind() == pdf::PDFContentNodeKind::StateRun)
        {
            hasStateRun = true;
            QVERIFY(!model->getIndex(pdf::PDFContentNodeReference{ text.container, child->getId() }).isValid());
        }
    }
    QVERIFY(hasStateRun);
    H::finish(context);
}

void AppToolContentPanelsTest::rowsOfNestedGroupsAndClipping()
{
    // The children of the groups; the clipping path (re W n) is the only row with the clipping effect
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    const Model* model = H::model(context);

    QCOMPARE(H::rows(model, model->index(0, 0)), QStringList({ QStringLiteral("Path|Rectangle|"), QStringLiteral("Path|Rectangle|") }));
    QCOMPARE(H::rows(model, model->index(3, 0)), QStringList({ QStringLiteral("Clipping path|Rectangle|"), QStringLiteral("Path|Rectangle|") }));
    QCOMPARE(H::rows(model, model->index(5, 0)), QStringList({ QStringLiteral("Path|2 segments|"), QStringLiteral("Path|3 segments|") }));
    QCOMPARE(H::rows(model, model->index(6, 0)), QStringList({ QStringLiteral("Path|Rectangle|") }));
    QCOMPARE(model->rowCount(model->index(1, 0)), 0);

    QVERIFY(model->index(0, 0, model->index(3, 0)).data(Model::IsClippingRole).toBool());
    QVERIFY(!model->index(1, 0, model->index(3, 0)).data(Model::IsClippingRole).toBool());
    QVERIFY(!model->index(2, 0).data(Model::IsClippingRole).toBool());

    // The parent of a child row is the group row; a group takes drops, a leaf does not
    const QModelIndex child = model->index(1, 0, model->index(3, 0));
    QCOMPARE(model->parent(child), model->index(3, 0));
    QVERIFY(model->flags(model->index(3, 0)) & Qt::ItemIsDropEnabled);
    QVERIFY(!(model->flags(model->index(2, 0)) & Qt::ItemIsDropEnabled));
    H::finish(context);
}

void AppToolContentPanelsTest::incrementalRemoveWithoutReset()
{
    // One command, one change of the rows: the deleted rectangle (row 2) is one rowsRemoved, never a reset
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    const pdf::PDFContentNodeReference path = H::node(model, 2);
    const pdf::PDFContentNodeReference childOfGroup = H::node(model, 1, model->index(0, 0));
    QSignalSpy reset(model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy counts(model, &Model::countsChanged);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete({ path, childOfGroup })));

    // The rows change from the event loop, not inside of the command
    QCOMPARE(removed.count(), 0);
    QVERIFY(H::waitForModel(context));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(removed.count(), 2);
    QCOMPARE(counts.count(), 1);
    QCOMPARE(model->getObjectCount(), 14);
    QCOMPARE(model->rowCount(), 8);
    QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    QCOMPARE(H::rows(model).at(2), QStringLiteral("Clipping group|2 objects|"));

    // Undo brings the rows back, again without a reset
    QVERIFY(context->getSession()->undo().has_value());
    QVERIFY(H::waitForModel(context));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(model->rowCount(), 9);
    QCOMPARE(model->getObjectCount(), 16);
    QCOMPARE(H::rows(model).at(2), QStringLiteral("Path|Rectangle|"));
    H::finish(context);
}

void AppToolContentPanelsTest::incrementalMoveAndChange()
{
    // A Reorder is rowsMoved, a Transform of a row is dataChanged of that row only
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(H::lefts(context), QList<int>({ 10, 20, 30, 40 }));

    QSignalSpy reset(model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    const pdf::PDFContentNodeReference last = H::node(model, 3);
    const pdf::PDFContentNodeReference root{ last.container, context->getEditedRoot().node };
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createReorder({ last }, root, 0, pdf::PDFContentCommandFactory::ReorderMode::KeepLayerAndTag)));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(H::lefts(context), QList<int>({ 40, 10, 20, 30 }));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(model->getIndex(last).row(), 0);

    changed.clear();
    const pdf::PDFContentNodeReference second = H::node(model, 2);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createTransform({ second }, QTransform::fromTranslate(1, 0))));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.front().at(0).value<QModelIndex>(), model->index(2, 0));
    QCOMPARE(H::lefts(context), QList<int>({ 40, 10, 21, 30 }));
    H::finish(context);
}

void AppToolContentPanelsTest::largeChangeResets()
{
    // More than LARGE_CHANGE removed rows of one command: one reset instead of hundreds of signals
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createManyDocument(Model::LARGE_CHANGE + 50), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(model->rowCount(), Model::LARGE_CHANGE + 50);

    std::vector<pdf::PDFContentNodeReference> nodes;
    for (int row = 0; row < Model::LARGE_CHANGE + 1; ++row)
    {
        nodes.push_back(H::node(model, row));
    }
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete(nodes)));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(reset.count(), 1);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(model->rowCount(), 49);
    H::finish(context);
}

void AppToolContentPanelsTest::dropReturnsFalseAndCommandFollows()
{
    // RE-21: dropMimeData only queues the Reorder and returns false; the rows change, when the command came
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    // The text "AB" (row 1) in front of the group (row 0)
    const pdf::PDFContentNodeReference text = H::node(model, 1);
    std::unique_ptr<QMimeData> data(model->mimeData({ model->index(1, 0) }));
    QVERIFY(data);
    QVERIFY(model->canDropMimeData(data.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    const quint64 version = context->getContainer()->getVersion();
    QVERIFY(!model->dropMimeData(data.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    QCOMPARE(context->getContainer()->getVersion(), version);
    QCOMPARE(model->getIndex(text).row(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(context->getContainer()->getVersion() != version, H::timeout(5000));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(model->getIndex(text).row(), 0);
    QCOMPARE(model->getIndex(text).parent(), QModelIndex());

    // On the clipping group (row 3): inside of it, at its end
    const pdf::PDFContentNodeReference path = H::node(model, 2);
    data.reset(model->mimeData({ model->index(2, 0) }));
    QVERIFY(!model->dropMimeData(data.get(), Qt::MoveAction, -1, -1, model->index(3, 0)));
    QTRY_VERIFY_WITH_TIMEOUT(model->getIndex(path).parent().isValid(), H::timeout(5000));
    QCOMPARE(model->getIndex(path).parent(), model->getIndex(H::node(model, 2)));
    QCOMPARE(model->rowCount(model->getIndex(path).parent()), 3);
    QCOMPARE(model->getIndex(path).row(), 2);
    QCOMPARE(model->rowCount(), 8);

    // Between two rows further down: the text (row 0) in front of the tagged text "BA" (row 3), behind the clipping
    // group - the index of the command is the index of the node of that row (the moved node is skipped there)
    data.reset(model->mimeData({ model->index(0, 0) }));
    QVERIFY(!model->dropMimeData(data.get(), Qt::MoveAction, 3, 0, QModelIndex()));
    QTRY_COMPARE_WITH_TIMEOUT(model->getIndex(text).row(), 2, H::timeout(5000));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(H::rows(model).mid(0, 4), QStringList({ QStringLiteral("Group|2 objects|"), QStringLiteral("Clipping group|3 objects|"),
                                                     QStringLiteral("Text|“AB”|"), QStringLiteral("Text|“BA”|P") }));
    H::finish(context);
}

void AppToolContentPanelsTest::dropRefusesInvalidTargets()
{
    // Invalid targets are not offered (zadani_gui.md 4.18): a leaf, the moved group itself, rows of an old version,
    // a drop while the editing is paused
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    std::unique_ptr<QMimeData> group(model->mimeData({ model->index(3, 0) }));
    QVERIFY(!model->canDropMimeData(group.get(), Qt::MoveAction, -1, -1, model->index(3, 0)));
    QVERIFY(!model->canDropMimeData(group.get(), Qt::MoveAction, -1, -1, model->index(2, 0)));
    QVERIFY(!model->canDropMimeData(group.get(), Qt::CopyAction, 0, 0, QModelIndex()));
    QVERIFY(model->canDropMimeData(group.get(), Qt::MoveAction, 0, 0, model->index(0, 0)));

    // Unknown content is not dragged
    QVERIFY(!model->mimeData({ model->index(8, 0) }));

    // A run of the tool pauses the editing: no drop
    context->setRunning(true);
    QVERIFY(!model->canDropMimeData(group.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    context->setRunning(false);

    // Rows of the version before a command
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete({ H::node(model, 2) })));
    QVERIFY(H::waitForModel(context));
    QVERIFY(!model->canDropMimeData(group.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    QVERIFY(!model->dropMimeData(group.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    H::settle();
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(1));
    H::finish(context);
}

void AppToolContentPanelsTest::filterKeepsGroupsOfMatches()
{
    // The filter: the kind or the description contains the text (case insensitive), a group stays when a row inside
    // of it stays; the counts of the header stay the counts of every row
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    const Model::FilterKinds all = model->getFilterKinds();

    model->setFilter(QStringLiteral("clipping PATH"), all);
    QVERIFY(model->isFiltered());
    QCOMPARE(H::rows(model), QStringList({ QStringLiteral("Clipping group|2 objects|") }));
    QCOMPARE(H::rows(model, model->index(0, 0)), QStringList({ QStringLiteral("Clipping path|Rectangle|") }));
    QCOMPARE(model->getMatchCount(), 1);
    QCOMPARE(model->getObjectCount(), 16);

    model->setFilter(QStringLiteral("ab"), all);
    QCOMPARE(H::rows(model), QStringList({ QStringLiteral("Text|“AB”|"), QStringLiteral("Tagged content|Table|") }));
    QCOMPARE(model->rowCount(model->index(1, 0)), 0);

    // The kinds: texts only
    model->setFilter(QString(), Model::FilterKinds(Model::FilterKind::Text));
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->getMatchCount(), 3);

    model->setFilter(QString(), all);
    QVERIFY(!model->isFiltered());
    QCOMPARE(model->rowCount(), 9);
    QCOMPARE(model->getMatchCount(), 0);
    H::finish(context);
}

void AppToolContentPanelsTest::lockAndHideAreNoCommands()
{
    // zadani_gui.md 4.27, RE-16: the lock and the hidden flag are the state of the session - no command, no undo step
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    const QModelIndex index = model->index(1, 0);
    const pdf::PDFContentNodeReference node = model->getNode(index);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QVERIFY(model->setData(index, true, Model::IsLockedRole));
    QVERIFY(context->getSession()->isNodeLocked(node));
    QVERIFY(index.data(Model::IsLockedRole).toBool());
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.front().at(0).value<QModelIndex>(), index);

    QVERIFY(model->setData(index, true, Model::IsHiddenRole));
    QVERIFY(context->getSession()->isNodeHidden(node));
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(0));
    QVERIFY(context->getSession()->getUndoStack()->isClean());

    QVERIFY(model->setData(index, false, Model::IsLockedRole));
    QVERIFY(!context->getSession()->isNodeLocked(node));
    QVERIFY(!model->setData(index, true, Qt::EditRole));
    H::finish(context);
}

void AppToolContentPanelsTest::hundredThousandObjects()
{
    // zadani_gui.md 4.24: a page of 100 000 objects stays usable - one command changes one row (no reset), the rows
    // follow within the time limit
    using H = ContentPanelsHelper;
    constexpr int COUNT = 100000;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createManyDocument(COUNT), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(model->rowCount(), COUNT);
    QCOMPARE(model->getObjectCount(), COUNT);

    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    const pdf::PDFContentNodeReference node = H::node(model, COUNT / 2);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete({ node })));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(H::waitForModel(context));
    QVERIFY2(timer.elapsed() < H::timeout(3000), qPrintable(QStringLiteral("%1 ms").arg(timer.elapsed())));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.front().at(1).toInt(), COUNT / 2);
    QCOMPARE(model->rowCount(), COUNT - 1);
    QVERIFY(!model->getIndex(node).isValid());

    // A filter, which matches nothing, and back
    model->setFilter(QStringLiteral("no such object"), model->getFilterKinds());
    QCOMPARE(model->rowCount(), 0);
    model->setFilter(QString(), model->getFilterKinds());
    QCOMPARE(model->rowCount(), COUNT - 1);
    H::finish(context);
}

void AppToolContentPanelsTest::treeFollowsSelection()
{
    // The selection is the one source of truth: a node selected elsewhere (the canvas) is the selected row, its group
    // is expanded
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    pdf::PDFTreeView* tree = panel->getTree();

    const QModelIndex group = model->index(3, 0);
    const pdf::PDFContentNodeReference inner = H::node(model, 1, group);
    const pdf::PDFContentNodeReference text = H::node(model, 1);
    QVERIFY(!tree->isExpanded(group));
    context->getSelection()->setNodes({ text, inner });
    QCOMPARE(tree->selectionModel()->selectedRows().size(), 2);
    QVERIFY(tree->selectionModel()->isRowSelected(1, QModelIndex()));
    QVERIFY(tree->selectionModel()->isRowSelected(1, group));
    QVERIFY(tree->isExpanded(group));
    QCOMPARE(tree->currentIndex(), model->getIndex(inner));
    QVERIFY(model->isNodeExpanded(model->getNode(group)));

    // The relayed selection of the tree does not change the selection of the tool back (the same rows)
    H::settle();
    QCOMPARE(context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ text, inner }));

    // The subtitle of the header
    QCOMPARE(panel->getSubtitle()->text(), QStringLiteral("Page 1 · 16 objects"));
    H::finish(context, host);
}

void AppToolContentPanelsTest::selectionFollowsTree()
{
    // A row selected in the tree is the selection of the tool, from the event loop (PDFAppItemViewSignalRelay); the
    // current row is the key object
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    pdf::PDFTreeView* tree = panel->getTree();

    tree->selectionModel()->select(model->index(2, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QVERIFY(context->getSelection()->isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ H::node(model, 2) }), H::timeout(5000));

    tree->selectionModel()->select(model->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    tree->selectionModel()->setCurrentIndex(model->index(0, 0), QItemSelectionModel::NoUpdate);
    QTRY_COMPARE_WITH_TIMEOUT(context->getSelection()->getCount(), 2, H::timeout(5000));
    QCOMPARE(context->getSelection()->getNodes().front(), H::node(model, 2));
    QCOMPARE(context->getSelection()->getKeyNode(), H::node(model, 0));
    H::finish(context, host);
}

void AppToolContentPanelsTest::filterFieldWaitsForTyping()
{
    // The filter does not rebuild the rows at every key: the model gets the text, when the typing pauses
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    host->show();

    QVERIFY(!panel->getFilterField()->isVisible());
    QTest::mouseClick(panel->getFilterButton(), Qt::LeftButton);
    QVERIFY(panel->getFilterField()->isVisible());
    QVERIFY(model->isFilterShown());

    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QTest::keyClicks(panel->getFilterField(), QStringLiteral("layer"));
    QCOMPARE(reset.count(), 0);
    QVERIFY(!model->isFiltered());
    QTRY_VERIFY_WITH_TIMEOUT(model->isFiltered(), H::timeout(5000));
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model->getFilterText(), QStringLiteral("layer"));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(panel->getSubtitle()->text(), QStringLiteral("Page 1 · 1 of 16 objects match"));

    // A rebuilt panel (a change of the language) shows the filter of the model
    pdf::PDFDeferredDeletion::retire(panel);
    pdf::PDFAppEditContentObjectsPanel* rebuilt = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    rebuilt->show();
    QCOMPARE(rebuilt->getFilterField()->text(), QStringLiteral("layer"));
    QVERIFY(rebuilt->getFilterField()->isVisibleTo(host));

    // The button closes the field and ends the filter
    QTest::mouseClick(rebuilt->getFilterButton(), Qt::LeftButton);
    QVERIFY(!model->isFiltered());
    QCOMPARE(model->rowCount(), 9);
    H::finish(context, host);
}

void AppToolContentPanelsTest::treeKeysAndToggles()
{
    // Delete in the tree runs DELETE_SELECTION (from the event loop); the toggles of the row write the lock and the
    // hidden flag (PDFRowDelegate, edit-content-v2-objects-nested)
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    pdf::PDFTreeView* tree = panel->getTree();
    host->resize(300, 400);
    host->show();

    pdf::PDFRowDelegate* delegate = qobject_cast<pdf::PDFRowDelegate*>(tree->itemDelegate());
    QVERIFY(delegate);
    const QModelIndex first = model->index(0, 0);
    QStyleOptionViewItem option;
    option.rect = tree->visualRect(first);
    const QList<pdf::PDFRowDelegate::Action> actions = delegate->getActions(option, first);
    QCOMPARE(actions.size(), 2);
    QVERIFY(actions[0].checkable && actions[1].checkable);
    QCOMPARE(actions[0].role, int(Model::IsLockedRole));
    QCOMPARE(actions[1].role, int(Model::IsHiddenRole));
    delegate->triggerAction(tree, first, actions[0].id);
    QVERIFY(context->getSession()->isNodeLocked(model->getNode(first)));
    delegate->triggerAction(tree, first, actions[1].id);
    QVERIFY(context->getSession()->isNodeHidden(model->getNode(first)));
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(0));

    // Delete: the selected square 30 (row 2) goes
    context->getSelection()->setNode(H::node(model, 2));
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_Delete);
    QCOMPARE(H::lefts(context), QList<int>({ 10, 20, 30, 40 }));
    QTRY_COMPARE_WITH_TIMEOUT(H::lefts(context), QList<int>({ 10, 20, 40 }), H::timeout(5000));
    H::finish(context, host);
}

void AppToolContentPanelsTest::deleteSelection()
{
    // DELETE_SELECTION deletes the selected objects: not the children of a selected group again, not a locked object,
    // not unknown content; the application command edit.delete is the same entry
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    QCOMPARE(context->getApplicationCommandState(pdf::PDFAppCommands::deleteSelection()), pdf::PDFAppToolCommandState::Disabled);
    const pdf::PDFContentNodeReference text = H::node(model, 1);
    context->getSession()->setNodeLocked(text, true);
    context->getSelection()->setNodes({ H::node(model, 0), H::node(model, 0, model->index(0, 0)), text, H::node(model, 2), H::node(model, 8) });
    QCOMPARE(context->getApplicationCommandState(pdf::PDFAppCommands::deleteSelection()), pdf::PDFAppToolCommandState::Enabled);

    QSignalSpy refused(context.data(), &Context::commandRefused);
    QVERIFY(context->handleApplicationCommand(pdf::PDFAppCommands::deleteSelection()));
    QTRY_COMPARE_WITH_TIMEOUT(context->getSession()->getUndoStack()->getCount(), size_t(1), H::timeout(5000));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(refused.count(), 0);
    QCOMPARE(H::rows(model).mid(0, 2), QStringList({ QStringLiteral("Text|“AB”|"), QStringLiteral("Clipping group|2 objects|") }));
    QCOMPARE(model->rowCount(), 7);
    QCOMPARE(model->getObjectCount(), 12);

    // Only locked or unknown content left in the selection: nothing to delete
    context->getSelection()->setNodes({ text, H::node(model, 6) });
    QCOMPARE(context->getApplicationCommandState(pdf::PDFAppCommands::deleteSelection()), pdf::PDFAppToolCommandState::Disabled);
    H::finish(context);
}

void AppToolContentPanelsTest::selectAll()
{
    // Select All (edit.selectAll): the objects of the edited root, which can be selected - no unknown content, no locked
    // or hidden object; the tagged text is selected as its text object
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    context->getSession()->setNodeLocked(H::node(model, 1), true);
    context->getSession()->setNodeHidden(H::node(model, 2), true);

    QCOMPARE(context->getApplicationCommandState(pdf::PDFAppCommands::selectAll()), pdf::PDFAppToolCommandState::Enabled);
    QVERIFY(context->handleApplicationCommand(pdf::PDFAppCommands::selectAll()));
    QTRY_VERIFY_WITH_TIMEOUT(!context->getSelection()->isEmpty(), H::timeout(5000));
    QCOMPARE(context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ H::node(model, 0), H::node(model, 3), H::node(model, 4),
                                                                                              H::node(model, 5), H::node(model, 6), H::node(model, 7) }));
    H::finish(context);
}

void AppToolContentPanelsTest::orderActions()
{
    // The four order actions over the squares 10, 20, 30, 40 (the state run "0 g" is no object to step over)
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    const Registry* actions = registry.get();
    const pdf::PDFContentNodeReference square = H::node(model, 1);
    context->getSelection()->setNode(square);

    QVERIFY(H::runAction(context, Ids::ORDER_BRING_FORWARD));
    QCOMPARE(H::lefts(context), QList<int>({ 10, 30, 20, 40 }));
    QVERIFY(H::runAction(context, Ids::ORDER_SEND_BACKWARD));
    QCOMPARE(H::lefts(context), QList<int>({ 10, 20, 30, 40 }));
    QVERIFY(H::runAction(context, Ids::ORDER_BRING_TO_FRONT));
    QCOMPARE(H::lefts(context), QList<int>({ 10, 30, 40, 20 }));
    QVERIFY(!actions->findAction(QString::fromLatin1(Ids::ORDER_BRING_TO_FRONT))->isEnabled(context));
    QVERIFY(!actions->findAction(QString::fromLatin1(Ids::ORDER_BRING_FORWARD))->isEnabled(context));
    QVERIFY(H::runAction(context, Ids::ORDER_SEND_TO_BACK));
    QCOMPARE(H::lefts(context), QList<int>({ 20, 10, 30, 40 }));
    QVERIFY(!actions->findAction(QString::fromLatin1(Ids::ORDER_SEND_TO_BACK))->isEnabled(context));
    QVERIFY(!actions->findAction(QString::fromLatin1(Ids::ORDER_SEND_BACKWARD))->isEnabled(context));
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(4));

    // Two squares at once keep their order
    context->getSelection()->setNodes({ H::node(model, 2), H::node(model, 0) });
    QVERIFY(H::runAction(context, Ids::ORDER_BRING_TO_FRONT));
    QCOMPARE(H::lefts(context), QList<int>({ 10, 40, 20, 30 }));
    H::finish(context);
}

void AppToolContentPanelsTest::groupAndUngroup()
{
    // Group needs two objects of one parent; Ungroup is a row only for a selected group
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    const pdf::PDFAppEditContentActionEntry* group = registry->findAction(QString::fromLatin1(Ids::GROUP));
    const pdf::PDFAppEditContentActionEntry* ungroup = registry->findAction(QString::fromLatin1(Ids::UNGROUP));

    context->getSelection()->setNode(H::node(model, 0));
    QVERIFY(!group->isEnabled(context));
    QVERIFY(!ungroup->isVisible(context));
    context->getSelection()->setNodes({ H::node(model, 0), H::node(model, 1) });
    QVERIFY(group->isEnabled(context));
    QVERIFY(H::runAction(context, Ids::GROUP));
    // The group is content created in this session (zadani_gui.md 4.27: New)
    QCOMPARE(H::rows(model), QStringList({ QStringLiteral("Group|2 objects|New"), QStringLiteral("Path|Rectangle|"), QStringLiteral("Path|Rectangle|") }));

    context->getSelection()->setNode(H::node(model, 0));
    QVERIFY(ungroup->isVisible(context));
    QVERIFY(H::runAction(context, Ids::UNGROUP));
    QCOMPARE(H::lefts(context), QList<int>({ 10, 20, 30, 40 }));
    H::finish(context);
}

void AppToolContentPanelsTest::hideAndLockTitles()
{
    // Hide / Show and Lock / Unlock follow the selected objects; no undo step
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    const pdf::PDFAppEditContentActionEntry* hide = registry->findAction(QString::fromLatin1(Ids::HIDE));
    const pdf::PDFAppEditContentActionEntry* lock = registry->findAction(QString::fromLatin1(Ids::LOCK));

    context->getSelection()->setNodes({ H::node(model, 0), H::node(model, 1) });
    QCOMPARE(hide->getTitle(context), QStringLiteral("Hide"));
    QCOMPARE(lock->getTitle(context), QStringLiteral("Lock"));
    context->getSession()->setNodeHidden(H::node(model, 0), true);
    QCOMPARE(hide->getTitle(context), QStringLiteral("Hide"));

    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::HIDE)));
    QTRY_VERIFY_WITH_TIMEOUT(context->getSession()->isNodeHidden(H::node(model, 1)), H::timeout(5000));
    QVERIFY(context->getSession()->isNodeHidden(H::node(model, 0)));
    QCOMPARE(hide->getTitle(context), QStringLiteral("Show"));
    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::HIDE)));
    QTRY_VERIFY_WITH_TIMEOUT(!context->getSession()->isNodeHidden(H::node(model, 0)), H::timeout(5000));
    QVERIFY(!context->getSession()->isNodeHidden(H::node(model, 1)));

    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::LOCK)));
    QTRY_COMPARE_WITH_TIMEOUT(lock->getTitle(context), QStringLiteral("Unlock"), H::timeout(5000));
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(0));
    H::finish(context);
}

void AppToolContentPanelsTest::duplicate()
{
    // Duplicate: the copy right after the original, 10 to the right and 10 down, the badge New
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    context->getSelection()->setNode(H::node(model, 1));

    QVERIFY(H::runAction(context, Ids::DUPLICATE));
    QCOMPARE(H::lefts(context), QList<int>({ 10, 20, 30, 30, 40 }));
    const QRectF copy = pdf::PDFAppEditContentGeometryHelper::getNodeBoundingBox(*context->getContainer(), H::node(model, 2).node);
    QCOMPARE(copy, QRectF(30, 10, 5, 5));
    QCOMPARE(model->index(2, 0).data(Model::BadgeRole).toString(), QStringLiteral("New"));
    QVERIFY(model->index(2, 0).data(Model::IsNewRole).toBool());
    QVERIFY(!model->index(1, 0).data(Model::IsNewRole).toBool());
    QCOMPARE(model->getNewObjectCount(), 1);
    QCOMPARE(model->getObjectCount(), 5);
    H::finish(context);
}

void AppToolContentPanelsTest::viewOptions()
{
    // The check rows of View are the options of the display (zadani_gui.md 4.26), also in Preview
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    const pdf::PDFAppEditContentActionEntry* clipping = registry->findAction(QString::fromLatin1(Ids::VIEW_CLIPPING_PATHS));
    const pdf::PDFAppEditContentActionEntry* annotations = registry->findAction(QString::fromLatin1(Ids::VIEW_ANNOTATIONS));
    pdf::PDFAppEditContentOptions* options = context->getOptions();

    QVERIFY(clipping->isCheckable());
    QVERIFY(!clipping->isChecked(context));
    QVERIFY(annotations->isChecked(context));
    context->setRunning(true);
    QVERIFY(!context->isEditingAllowed());
    QVERIFY(clipping->isEnabled(context));
    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::VIEW_CLIPPING_PATHS)));
    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::VIEW_ANNOTATIONS)));
    QTRY_VERIFY_WITH_TIMEOUT(options->getBool(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_CLIPPING_PATHS)), H::timeout(5000));
    QTRY_VERIFY_WITH_TIMEOUT(!options->getBool(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_ANNOTATIONS)), H::timeout(5000));
    QVERIFY(!options->getBool(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_INVISIBLE_TEXT)));
    QVERIFY(clipping->isChecked(context));
    H::finish(context);
}

void AppToolContentPanelsTest::contextMenuRows()
{
    // edit-content-v2-objects-context-menu: the rows of the registry in groups with dividers, the keys, a row of a later
    // stage or a row, which does not apply, is hidden; one object: Group is disabled, Ungroup is not there
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    pdf::PDFAppEditContentMenus* menus = new pdf::PDFAppEditContentMenus(context, nullptr);
    context->getSelection()->setNode(H::node(model, 1));

    context->requestContextMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100));
    QMenu* menu = menus->getOpenMenu();
    QVERIFY(menu);
    QCOMPARE(H::menuRows(menu), QStringList({ "delete", "|", "order.bring-to-front", "order.send-to-back", "|", "group", "|", "hide", "lock" }));
    QVERIFY(!H::findMenuAction(menu, Ids::GROUP)->isEnabled());
    QVERIFY(H::findMenuAction(menu, Ids::HIDE)->isEnabled());
    QCOMPARE(H::findMenuAction(menu, Ids::HIDE)->text(), QStringLiteral("Hide"));
    QCOMPARE(H::findMenuAction(menu, Ids::ORDER_BRING_TO_FRONT)->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight));
    QCOMPARE(H::findMenuAction(menu, Ids::LOCK)->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L));
    QCOMPARE(H::findMenuAction(menu, Ids::DELETE_SELECTION)->shortcut(), QKeySequence(QKeySequence::Delete));

    // A second request replaces the menu; a menu of no visible row is not shown
    QPointer<QMenu> first(menu);
    context->requestContextMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(120, 120));
    QVERIFY(menus->getOpenMenu());
    QVERIFY(menus->getOpenMenu() != first || !first);
    QVERIFY(!first || pdf::PDFDeferredDeletion::isRetired(first));
    QVERIFY(!menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::TAG_ROW), QPoint(0, 0)));
    H::finish(context, menus);
}

void AppToolContentPanelsTest::contextMenuQueuesAndCloses()
{
    // A row only asks for its action (from the event loop: Qt reads the menu after triggered); a change of the language
    // and the end of the tool close the menu
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QPointer<pdf::PDFAppEditContentMenus> menus = new pdf::PDFAppEditContentMenus(context, nullptr);
    const pdf::PDFContentNodeReference square = H::node(model, 1);
    context->getSelection()->setNode(square);

    QPointer<QMenu> menu = menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100));
    QVERIFY(menu);
    H::findMenuAction(menu, Ids::HIDE)->trigger();
    QVERIFY(!context->getSession()->isNodeHidden(square));
    QTRY_VERIFY_WITH_TIMEOUT(context->getSession()->isNodeHidden(square), H::timeout(5000));

    menu = menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100));
    QVERIFY(menu);
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(menu, &languageChange);
    QTRY_VERIFY_WITH_TIMEOUT(!menus->getOpenMenu(), H::timeout(5000));
    QVERIFY(!menu || pdf::PDFDeferredDeletion::isRetired(menu));

    menu = menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100));
    QVERIFY(menu);
    pdf::PDFDeferredDeletion::retire(menus);
    QVERIFY(!menu || pdf::PDFDeferredDeletion::isRetired(menu));
    H::finish(context);
    QVERIFY(!menu);
}

void AppToolContentPanelsTest::viewPanelRows()
{
    // fillPanel: the rows of View with the caption SHOW, the descriptions of -menus-view; a check row stays open, its
    // check follows the option; a second fill replaces the rows
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    pdf::PDFAppEditContentMenus* menus = new pdf::PDFAppEditContentMenus(context, nullptr);
    pdf::PDFAppToolPanelPopup* panel = new pdf::PDFAppToolPanelPopup(nullptr, 320, nullptr);

    menus->fillPanel(panel, QString::fromLatin1(pdf::PDFAppEditContentMenuIds::VIEW));
    std::vector<pdf::PDFTileRow*> rows;
    for (const QPointer<pdf::PDFTileRow>& row : panel->getRows())
    {
        if (row && !pdf::PDFDeferredDeletion::isRetired(row))
        {
            rows.push_back(row);
        }
    }
    QCOMPARE(rows.size(), size_t(3));
    QCOMPARE(rows[0]->getTitle(), QStringLiteral("Clipping paths"));
    QCOMPARE(rows[0]->getDescription(), QStringLiteral("Blue dashed outlines"));
    QCOMPARE(rows[1]->getTitle(), QStringLiteral("Invisible text"));
    QCOMPARE(rows[2]->getTitle(), QStringLiteral("Annotations and fields"));
    QCOMPARE(rows[2]->getDescription(), QStringLiteral("Dimmed underlay, cannot be edited here"));
    QVERIFY(!rows[0]->isChecked());
    QVERIFY(rows[2]->isChecked());
    QVERIFY(panel->findChild<pdf::PDFLabel*>());

    rows[0]->click();
    QTRY_VERIFY_WITH_TIMEOUT(context->getOptions()->getBool(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_CLIPPING_PATHS)), H::timeout(5000));
    QVERIFY(rows[0]->isChecked());

    // The option changed elsewhere: the row follows
    context->getOptions()->setValue(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_ANNOTATIONS), false);
    QVERIFY(!rows[2]->isChecked());

    QPointer<pdf::PDFTileRow> old(rows[0]);
    menus->fillPanel(panel, QString::fromLatin1(pdf::PDFAppEditContentMenuIds::VIEW));
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(old));
    int liveRows = 0;
    for (const QPointer<pdf::PDFTileRow>& row : panel->getRows())
    {
        liveRows += (row && !pdf::PDFDeferredDeletion::isRetired(row)) ? 1 : 0;
    }
    QCOMPARE(liveRows, 3);
    pdf::PDFDeferredDeletion::retire(panel);
    H::finish(context, menus);
}

void AppToolContentPanelsTest::pagesTilesAndRequest()
{
    // edit-content-v2-pages: the current page is the selected tile, a page with changes not applied has the blue dot; a
    // click only asks for the page (the GUI of the tool switches it)
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    const pdf::PDFDocumentPointer document = H::createPagesDocument(3);
    QPointer<Context> context = new Context(document, pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 1));
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentPagesPanel* panel = new pdf::PDFAppEditContentPagesPanel(context, host);
    panel->setDocument(document, QStringLiteral("test"), nullptr);
    pdf::PDFAppToolPageGridModel* grid = panel->getGrid()->getModel();
    QCOMPARE(grid->rowCount(), 3);
    QCOMPARE(grid->getTile(1).state, pdf::PDFComponentPainter::PageTileState::Selected);
    QCOMPARE(grid->getTile(0).state, pdf::PDFComponentPainter::PageTileState::Normal);
    QVERIFY(!grid->getTile(1).dotColor.has_value());
    QCOMPARE(panel->getSubtitle()->text(), QStringLiteral("Page 2 of 3"));

    Model* model = H::model(context);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete({ H::node(model, 0) })));
    QCOMPARE(grid->getTile(1).dotColor, std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Blue));
    QVERIFY(!grid->getTile(0).dotColor.has_value());
    QCOMPARE(panel->getSubtitle()->text(), QStringLiteral("Page 2 of 3 · 1 pages edited"));

    QSignalSpy requested(context.data(), &Context::pageRequested);
    Q_EMIT panel->getGrid()->pageClicked(2, Qt::NoModifier);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.front().at(0).value<pdf::PDFInteger>(), pdf::PDFInteger(2));
    QCOMPARE(panel->getActivatedPage(), pdf::PDFInteger(2));
    QCOMPARE(context->getPageIndex(), pdf::PDFInteger(1));

    // The GUI of the tool switches the page: the tiles follow
    context->setPage(2);
    QCOMPARE(grid->getTile(2).state, pdf::PDFComponentPainter::PageTileState::Selected);
    QCOMPARE(grid->getTile(1).state, pdf::PDFComponentPainter::PageTileState::Normal);
    QCOMPARE(grid->getTile(1).dotColor, std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Blue));
    H::finish(context, host);
}

void AppToolContentPanelsTest::kindsAndCountsOneRule()
{
    // R7-10: one rule of the kind and of the counts, which the row and the card Selection show (the public statics of
    // the model); the expected values from the hand-written content of kindsContent
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createDocumentWithFont(H::kindsContent()), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    const Model* model = H::model(context);
    const pdf::PDFContentContainerPointer container = context->getContainer();

    QCOMPARE(H::rows(model), QStringList({ QStringLiteral("Path|3 segments|"),
                                           QStringLiteral("Path|3 segments|"),
                                           QStringLiteral("Path|1 segments|"),
                                           QStringLiteral("Text|Paragraph · 2 lines|"),
                                           QStringLiteral("Text|“ABBA”|"),
                                           QStringLiteral("Layer content|Missing|"),
                                           QStringLiteral("Clipping group|2 objects|"),
                                           QStringLiteral("Clipping group|2 objects|") }));

    const auto contentNode = [&](int row) -> const pdf::PDFContentNode& { return *container->getNode(H::node(model, row).node); };
    QCOMPARE(Model::getSegmentCount(contentNode(0)), 3);
    QCOMPARE(Model::getSegmentCount(contentNode(1)), 3);
    QCOMPARE(Model::getSegmentCount(contentNode(2)), 1);
    QCOMPARE(Model::getSegmentCount(contentNode(3)), 0);
    QCOMPARE(Model::getLineCount(contentNode(3)), 2);
    QCOMPARE(Model::getLineCount(contentNode(4)), 1);
    QCOMPARE(Model::getLineCount(contentNode(0)), 0);
    QVERIFY(Model::isLayerContent(contentNode(5)));
    QVERIFY(!Model::isLayerContent(contentNode(6)));
    QVERIFY(Model::isClippingGroup(contentNode(6)));
    QVERIFY(Model::isClippingGroup(contentNode(7)));
    QVERIFY(!Model::isClippingGroup(contentNode(5)));
    QCOMPARE(Model::getChildRowCount(contentNode(6)), 2);

    // A rectangle of re: 4 drawn segments (8.5.2.1: three l and the h back to x y)
    const pdf::PDFContentNode& rectangle = *container->getNode(H::node(model, 0, model->index(5, 0)).node);
    QCOMPARE(Model::getSegmentCount(rectangle), 4);
    H::finish(context);
}

void AppToolContentPanelsTest::rowTextsComputedOncePerNode()
{
    // R7-9: the kind and the description of a row are computed once per node object - a paint or an accessibility
    // query reads them again from the cache; a command computes only the rows of the nodes it changed
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createSquaresDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(model->getTextComputationCount(), 0);

    const QStringList before = H::rows(model);
    for (int row = 0; row < model->rowCount(); ++row)
    {
        QCOMPARE(model->index(row, 0).data(Qt::DisplayRole).toString(), QStringLiteral("Path Rectangle"));
        QCOMPARE(model->index(row, 0).data(Qt::AccessibleTextRole).toString(), QStringLiteral("Path Rectangle"));
    }
    QCOMPARE(model->getTextComputationCount(), 4);

    // A Transform of the square 30 (row 2): only its row is computed again
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createTransform({ H::node(model, 2) }, QTransform::fromTranslate(1, 0))));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(H::rows(model), before);
    QCOMPARE(model->getTextComputationCount(), 5);

    // Another language: all again
    model->clearTextCache();
    QCOMPARE(H::rows(model), before);
    QCOMPARE(model->getTextComputationCount(), 9);
    H::finish(context);
}

void AppToolContentPanelsTest::transformOfManyRowsOneRange()
{
    // R7-9: a Transform of more than LARGE_CHANGE objects (an arrow key on a selection of a large page) changes no row's
    // place: one dataChanged of the range of the changed rows, no reset, no dataChanged per row; a Transform of two
    // objects is a dataChanged of each of them
    using H = ContentPanelsHelper;
    constexpr int COUNT = Model::LARGE_CHANGE + 50;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createManyDocument(COUNT), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(model->rowCount(), COUNT);

    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createTransform(H::topNodes(model), QTransform::fromTranslate(0, -1))));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(moved.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.front().at(0).value<QModelIndex>(), model->index(0, 0));
    QCOMPARE(changed.front().at(1).value<QModelIndex>(), model->index(COUNT - 1, 0));

    changed.clear();
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createTransform({ H::node(model, 3), H::node(model, 7) }, QTransform::fromTranslate(0, 1))));
    QVERIFY(H::waitForModel(context));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>(), model->index(3, 0));
    QCOMPARE(changed.at(1).at(0).value<QModelIndex>(), model->index(7, 0));
    QCOMPARE(reset.count(), 0);
    H::finish(context);
}

void AppToolContentPanelsTest::lockOfManyObjectsBoundedRows()
{
    // R6-2: the session tells a lock of every node by itself (no batch call); the tree answers each with one dataChanged
    // of its row up to LARGE_CHANGE rows in a turn of the event loop, then with one dataChanged of all rows - no reset,
    // no rebuild
    using H = ContentPanelsHelper;
    constexpr int COUNT = 1000;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createManyDocument(COUNT), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    context->getSelection()->setNodes(H::topNodes(model));
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy refreshed(model, &Model::refreshed);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::LOCK)));
    QTRY_VERIFY_WITH_TIMEOUT(context->getSession()->isNodeLocked(H::node(model, COUNT - 1)), H::timeout(5000));
    H::settle();
    QCOMPARE(reset.count(), 0);
    QCOMPARE(refreshed.count(), 0);
    QCOMPARE(changed.count(), Model::LARGE_CHANGE + 1);
    for (int i = 0; i < Model::LARGE_CHANGE; ++i)
    {
        QCOMPARE(changed.at(i).at(0).value<QModelIndex>(), model->index(i, 0));
        QCOMPARE(changed.at(i).at(1).value<QModelIndex>(), model->index(i, 0));
    }
    QCOMPARE(changed.back().at(0).value<QModelIndex>(), model->index(0, 0));
    QCOMPARE(changed.back().at(1).value<QModelIndex>(), model->index(COUNT - 1, 0));
    QVERIFY(model->index(COUNT - 1, 0).data(Model::IsLockedRole).toBool());

    // The next turn starts counting again: Unlock of three objects is three rows
    changed.clear();
    context->getSelection()->setNodes({ H::node(model, 5), H::node(model, 6), H::node(model, 900) });
    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::LOCK)));
    QTRY_VERIFY_WITH_TIMEOUT(!context->getSession()->isNodeLocked(H::node(model, 900)), H::timeout(5000));
    H::settle();
    QCOMPARE(changed.count(), 3);
    QCOMPARE(changed.at(2).at(0).value<QModelIndex>(), model->index(900, 0));
    H::finish(context);
}

void AppToolContentPanelsTest::lockedObjectsLeftAlone()
{
    // R7-4, one rule of the lock (PDFAppEditContentContext::isNodeChangeable): a locked row and a row inside of a locked
    // group are not dragged, nothing is dropped into a locked group, a row locked after its drag started is not dropped,
    // and the actions which change the content (Delete) leave them alone; Lock itself acts on them
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);

    const QModelIndex groupIndex = model->index(0, 0);
    const pdf::PDFContentNodeReference group = model->getNode(groupIndex);
    const pdf::PDFContentNodeReference child = H::node(model, 1, groupIndex);
    const pdf::PDFContentNodeReference path = H::node(model, 2);
    context->getSession()->setNodeLocked(group, true);

    QVERIFY(!(model->flags(groupIndex) & Qt::ItemIsDragEnabled));
    QVERIFY(!(model->flags(groupIndex) & Qt::ItemIsDropEnabled));
    QVERIFY(!(model->flags(model->index(1, 0, groupIndex)) & Qt::ItemIsDragEnabled));
    QVERIFY(model->flags(model->index(1, 0, groupIndex)) & Qt::ItemIsSelectable);
    QVERIFY(model->flags(model->index(2, 0)) & Qt::ItemIsDragEnabled);
    QVERIFY(model->flags(model->index(3, 0)) & Qt::ItemIsDropEnabled);
    QVERIFY(!model->mimeData({ model->index(1, 0, groupIndex) }));

    // The path: not into the locked group, into the clipping group (row 3) and between the top rows yes
    std::unique_ptr<QMimeData> data(model->mimeData({ model->index(2, 0) }));
    QVERIFY(data);
    QVERIFY(!model->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, groupIndex));
    QVERIFY(model->canDropMimeData(data.get(), Qt::MoveAction, -1, -1, model->index(3, 0)));
    QVERIFY(model->canDropMimeData(data.get(), Qt::MoveAction, 0, 0, QModelIndex()));

    // Locked while it is dragged: the drop is refused, no command
    context->getSession()->setNodeLocked(path, true);
    QVERIFY(!model->canDropMimeData(data.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    QVERIFY(!model->dropMimeData(data.get(), Qt::MoveAction, 0, 0, QModelIndex()));
    H::settle();
    QCOMPARE(context->getSession()->getUndoStack()->getCount(), size_t(0));

    // Delete of the child of the locked group, the locked path and the text: only the text goes
    const pdf::PDFContentNodeReference text = H::node(model, 1);
    context->getSelection()->setNodes({ child, path, text });
    QVERIFY(H::runAction(context, Ids::DELETE_SELECTION));
    QVERIFY(model->getIndex(child).isValid());
    QVERIFY(model->getIndex(path).isValid());
    QVERIFY(!model->getIndex(text).isValid());

    // Only locked objects selected: Delete is disabled, Lock (Unlock) is not
    context->getSelection()->setNodes({ child, path });
    QVERIFY(!registry->findAction(QString::fromLatin1(Ids::DELETE_SELECTION))->isEnabled(context));
    QVERIFY(registry->findAction(QString::fromLatin1(Ids::LOCK))->isEnabled(context));
    H::finish(context);
}

void AppToolContentPanelsTest::sessionReplacedReconnects()
{
    // PDFAppEditContentContext::reloadDocument (zadani_gui.md 4.21, a session without work is loaded again): the tree and
    // the tab Pages follow the new session - its rows, its commands, its lock; the old session reaches nothing
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    const pdf::PDFDocumentPointer first = H::createPagesDocument(3);
    QPointer<Context> context = new Context(first, pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(model->rowCount(), 2);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentPagesPanel* pages = new pdf::PDFAppEditContentPagesPanel(context, host);
    pages->setDocument(first, QStringLiteral("test"), nullptr);
    QCOMPARE(pages->getGrid()->getModel()->rowCount(), 3);

    // The new version: one page of three squares
    QPointer<pdf::PDFContentEditSession> oldSession = context->getSession();
    const pdf::PDFContentNodeReference oldNode = H::node(model, 0);
    context->reloadDocument(H::createManyDocument(3));
    QVERIFY(context->getSession() != oldSession.data());

    // The old session (deleted from the event loop) reaches nothing
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    if (oldSession)
    {
        Q_EMIT oldSession->editorStateChanged(oldNode);
    }
    QCOMPARE(changed.count(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(context->getContainer() && model->getContainer() == context->getContainer(), H::timeout(10000));
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(pages->getGrid()->getModel()->rowCount(), 1);
    QCOMPARE(pages->getSubtitle()->text(), QStringLiteral("Page 1 of 1"));

    // A command of the new session: the rows follow (incrementally), the page has the blue dot
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(context->execute(pdf::PDFContentCommandFactory::createDelete({ H::node(model, 1) })));
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, H::timeout(5000));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(pages->getGrid()->getModel()->getTile(0).dotColor, std::optional<pdf::PDFAccentHue>(pdf::PDFAccentHue::Blue));
    QCOMPARE(pages->getSubtitle()->text(), QStringLiteral("Page 1 of 1 · 1 pages edited"));

    // The lock of the new session: the row follows
    changed.clear();
    context->getSession()->setNodeLocked(H::node(model, 0), true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(model->index(0, 0).data(Model::IsLockedRole).toBool());
    H::finish(context, host);
}

void AppToolContentPanelsTest::clippingBadgePaintedAndTold()
{
    // R7-7, zadani_gui.md 4.18: a path, which paints and clips (re W f), has the clipping badge in its row - painted at
    // the trailing side and told by the accessible description; the same path without W has none
    using H = ContentPanelsHelper;
    using D = pdftest::ContentTestDocuments;
    std::unique_ptr<Registry> registry = H::createRegistry();
    const pdf::PDFDocumentPointer document(new pdf::PDFDocument(D::createDocument(QByteArrayLiteral("10 10 20 20 re W f\n40 10 20 20 re f\n"), &D::createNoResources)));
    QPointer<Context> context = new Context(document, pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QCOMPARE(H::rows(model), QStringList({ QStringLiteral("Path|Rectangle|"), QStringLiteral("Path|Rectangle|") }));

    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    pdf::PDFTreeView* tree = panel->getTree();
    host->resize(320, 400);
    host->show();
    QVERIFY(QTest::qWaitForWindowExposed(host));
    tree->selectionModel()->clearCurrentIndex();
    H::settle();

    const QImage clipping = H::rowImage(tree, model->index(0, 0));
    const QImage plain = H::rowImage(tree, model->index(1, 0));
    QCOMPARE(clipping.size(), plain.size());
    QVERIFY(!clipping.isNull());

    // The rows differ only at the trailing side (the badge left of the toggles), the kind and the description are the same
    int firstDifferent = clipping.width();
    int differentPixels = 0;
    for (int y = 0; y < clipping.height(); ++y)
    {
        for (int x = 0; x < clipping.width(); ++x)
        {
            if (clipping.pixel(x, y) != plain.pixel(x, y))
            {
                firstDifferent = qMin(firstDifferent, x);
                ++differentPixels;
            }
        }
    }
    QVERIFY(differentPixels > 10);
    QVERIFY2(firstDifferent > clipping.width() / 2, qPrintable(QStringLiteral("first different column %1 of %2").arg(firstDifferent).arg(clipping.width())));

    QVERIFY(model->index(0, 0).data(Qt::AccessibleDescriptionRole).toString().contains(QStringLiteral("Clipping")));
    QVERIFY(!model->index(1, 0).data(Qt::AccessibleDescriptionRole).toString().contains(QStringLiteral("Clipping")));
    H::finish(context, host);
}

void AppToolContentPanelsTest::filteredTreeKeepsHiddenSelection()
{
    // R7-12: a row added in a filtered tree keeps the selected objects the filter hides (Ctrl+click means "add")
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createObjectsDocument(), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    pdf::PDFTreeView* tree = panel->getTree();

    // The path (row 2) and the text "AB" (row 1); the filter "text" shows the three texts only
    const pdf::PDFContentNodeReference path = H::node(model, 2);
    const pdf::PDFContentNodeReference first = H::node(model, 1);
    const pdf::PDFContentNodeReference second = H::node(model, 4);
    context->getSelection()->setNodes({ path, first });
    model->setFilter(QStringLiteral("text"), model->getFilterKinds());
    H::settle();
    QCOMPARE(H::rows(model), QStringList({ QStringLiteral("Text|“AB”|"), QStringLiteral("Text|“BA”|P"), QStringLiteral("Text|Paragraph · 2 lines|") }));
    QVERIFY(!model->getIndex(path).isValid());
    QVERIFY(tree->selectionModel()->isRowSelected(0, QModelIndex()));
    QCOMPARE(context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ path, first }));

    // Ctrl+click of the second text
    tree->selectionModel()->select(model->getIndex(second), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QTRY_COMPARE_WITH_TIMEOUT(context->getSelection()->getCount(), 3, H::timeout(5000));
    QCOMPARE(context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ path, first, second }));

    // A click of the second text alone replaces only the shown rows: the path stays
    tree->selectionModel()->select(model->getIndex(second), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_COMPARE_WITH_TIMEOUT(context->getSelection()->getCount(), 2, H::timeout(5000));
    QCOMPARE(context->getSelection()->getNodes(), std::vector<pdf::PDFContentNodeReference>({ path, second }));
    H::finish(context, host);
}

void AppToolContentPanelsTest::selectAllOfHundredThousand()
{
    // R7-3, R7-9 (zadani_gui.md 4.24: the page is never refused): Ctrl+A in the tree and Select All of the action over a
    // page of 100 000 objects, then Ctrl+click and the context menu - every step does O(n log n) work, counted (the rows
    // and the nodes read, the comparisons of the sets of nodes; a quadratic step would count n^2 / 2 = 5 * 10^9). The
    // bound by hand: per row at most six operations on sets of n nodes (three inserts, three lookups), each at most the
    // height of a red-black tree + 1 comparisons (height <= 2 log2(n + 1) = 34 for n = 100 000), and five reads:
    // n * (6 * 35 + 5) < 256 n.
    using H = ContentPanelsHelper;
    constexpr int COUNT = 100000;
    constexpr quint64 BOUND = quint64(COUNT) * 256;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createManyDocument(COUNT), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    QWidget* host = new QWidget();
    pdf::PDFAppEditContentObjectsPanel* panel = new pdf::PDFAppEditContentObjectsPanel(context, model, host);
    pdf::PDFTreeView* tree = panel->getTree();
    pdf::PDFAppEditContentSelection* selection = context->getSelection();

    // Ctrl+A in the tree (Qt's select all of an ExtendedSelection view)
    QSignalSpy selectionChanged(selection, &pdf::PDFAppEditContentSelection::changed);
    QTest::keyClick(tree, Qt::Key_A, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(selection->getCount(), COUNT, H::timeout(30000));
    QCOMPARE(selectionChanged.count(), 1);
    QVERIFY2(panel->getLastTreeSelectionWork() <= BOUND, qPrintable(QString::number(panel->getLastTreeSelectionWork())));
    QVERIFY(panel->getLastTreeSelectionWork() >= quint64(COUNT));

    // Delete is enabled by the first object it finds: 1 node inspected; with the first ten locked: 11
    const pdf::PDFAppEditContentActionEntry* deleteEntry = registry->findAction(QString::fromLatin1(Ids::DELETE_SELECTION));
    quint64 work = pdf::PDFAppEditContentMenus::getWorkCount();
    QVERIFY(deleteEntry->isEnabled(context));
    QCOMPARE(pdf::PDFAppEditContentMenus::getWorkCount() - work, quint64(1));
    for (int row = 0; row < 10; ++row)
    {
        context->getSession()->setNodeLocked(H::node(model, row), true);
    }
    work = pdf::PDFAppEditContentMenus::getWorkCount();
    QVERIFY(deleteEntry->isEnabled(context));
    QCOMPARE(pdf::PDFAppEditContentMenus::getWorkCount() - work, quint64(11));

    // Select All of the action (no locked object): the tree takes the selection, its relayed change finds no change
    selection->clear();
    H::settle();
    selectionChanged.clear();
    QVERIFY(context->triggerAction(QString::fromLatin1(Ids::SELECT_ALL)));
    QTRY_COMPARE_WITH_TIMEOUT(selection->getCount(), COUNT - 10, H::timeout(30000));
    H::settle();
    QCOMPARE(selectionChanged.count(), 1);
    QCOMPARE(tree->selectionModel()->selection().size(), 1);
    QVERIFY2(panel->getLastTreeSelectionWork() <= BOUND, qPrintable(QString::number(panel->getLastTreeSelectionWork())));

    // Ctrl+click of a selected row: it leaves the selection
    tree->selectionModel()->select(model->index(500, 0), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    QTRY_COMPARE_WITH_TIMEOUT(selection->getCount(), COUNT - 11, H::timeout(30000));
    QVERIFY(!selection->contains(H::node(model, 500)));
    QVERIFY2(panel->getLastTreeSelectionWork() <= BOUND, qPrintable(QString::number(panel->getLastTreeSelectionWork())));

    // The context menu over the selection: its rows ask Group, Ungroup, the orders, Hide and Lock
    pdf::PDFAppEditContentMenus* menus = new pdf::PDFAppEditContentMenus(context, nullptr);
    work = pdf::PDFAppEditContentMenus::getWorkCount();
    QVERIFY(menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100)));
    QVERIFY2(pdf::PDFAppEditContentMenus::getWorkCount() - work <= 2 * BOUND, qPrintable(QString::number(pdf::PDFAppEditContentMenus::getWorkCount() - work)));
    H::finish(context, menus);
    pdf::PDFDeferredDeletion::retire(host);
    pdf::PDFDeferredDeletion::flush();
}

void AppToolContentPanelsTest::shownKeyFollowsRebinding()
{
    // Integration row 20 / FX3 item 3: the row of an entry, which answers an application command, shows the key of the
    // command as the user has it (Settings), not its default: the Delete row of the context menu
    using H = ContentPanelsHelper;
    std::unique_ptr<Registry> registry = H::createRegistry();
    pdf::PDFAppToolGUIContent::setRegistry(registry.get());
    auto resetRegistry = qScopeGuard([]() { pdf::PDFAppToolGUIContent::setRegistry(nullptr); });

    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(H::setDeveloperMode(fixture.context.get(), true));
    pdf::PDFAppDocumentTab* tab = fixture.openDocument(3);
    QVERIFY(tab);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID));
    QVERIFY(shell);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui && gui->getEditContext() && gui->getMenus());
    Context* context = gui->getEditContext();
    QVERIFY(pdftest::AppToolShellFixture::waitFor([context]() { return context->isEditingAllowed(); }));
    const pdf::PDFAppEditContentActionEntry* deleteEntry = registry->findAction(QString::fromLatin1(Ids::DELETE_SELECTION));

    // The default key of edit.delete (pdfappcommand.cpp: Qt::Key_Delete)
    QCOMPARE(pdf::PDFAppEditContentMenus::getShownKey(deleteEntry, context), QKeySequence(Qt::Key_Delete));

    // Rebound by the user: the row shows the new key
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::deleteSelection(), QStringLiteral("Ctrl+Shift+F7")).isError());
    QCOMPARE(pdf::PDFAppEditContentMenus::getShownKey(deleteEntry, context), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F7));
    const Model* objects = H::model(context);
    QVERIFY(objects && objects->rowCount() > 0);
    context->getSelection()->setNode(objects->getNode(objects->index(0, 0)));
    QPointer<QMenu> menu = gui->getMenus()->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100));
    QVERIFY(menu);
    QCOMPARE(H::findMenuAction(menu, Ids::DELETE_SELECTION)->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F7));
    menu->close();

    // Removed by the user: no key
    QVERIFY(!fixture.context->getShortcuts().setShortcut(pdf::PDFAppCommands::deleteSelection(), QString()).isError());
    QVERIFY(pdf::PDFAppEditContentMenus::getShownKey(deleteEntry, context).isEmpty());

    shell->cancel();
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&shell]() { return !shell; }));
    pdftest::AppToolShellFixture::settle();
}

void AppToolContentPanelsTest::contextMenuOfManyObjects()
{
    // R7-3, R7-9: the context menu over a selection of n = 2000 objects of one parent asks Delete, the two orders, Group,
    // Ungroup, Hide and Lock - linear work up to the logarithm (counted: the selected nodes inspected, the comparisons of
    // the set of the places). By hand: Delete, Hide and Lock stop at the first object; Bring to Front, Send to Back and
    // Group inspect every node once and insert its place into a set (<= 2 log2(n + 1) + 1 = 23 comparisons); Ungroup
    // inspects every node once: <= 3 * 24 n + n + 8 < 128 n (a de-duplication of the places by a search of a vector
    // would compare 3 * n^2 = 1.2 * 10^7 times)
    using H = ContentPanelsHelper;
    constexpr int COUNT = 2000;
    std::unique_ptr<Registry> registry = H::createRegistry();
    QPointer<Context> context = new Context(H::createManyDocument(COUNT), pdf::PDFCMSPointer(), registry.get(), nullptr);
    QVERIFY(H::showPage(context, 0));
    Model* model = H::model(context);
    context->getSelection()->setNodes(H::topNodes(model));
    pdf::PDFAppEditContentMenus* menus = new pdf::PDFAppEditContentMenus(context, nullptr);

    const quint64 work = pdf::PDFAppEditContentMenus::getWorkCount();
    QMenu* menu = menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(100, 100));
    QVERIFY(menu);
    const quint64 done = pdf::PDFAppEditContentMenus::getWorkCount() - work;
    QVERIFY2(done <= quint64(COUNT) * 128, qPrintable(QString::number(done)));
    QVERIFY(done >= quint64(COUNT) * 4);

    // The rows of a selection of every square of the root: Delete and Group enabled; Bring to Front and Send to Back have
    // no other object to step over (disabled); no Ungroup (no group selected)
    QCOMPARE(H::menuRows(menu), QStringList({ "delete", "|", "order.bring-to-front", "order.send-to-back", "|", "group", "|", "hide", "lock" }));
    QVERIFY(H::findMenuAction(menu, Ids::DELETE_SELECTION)->isEnabled());
    QVERIFY(H::findMenuAction(menu, Ids::GROUP)->isEnabled());
    QVERIFY(!H::findMenuAction(menu, Ids::ORDER_BRING_TO_FRONT)->isEnabled());
    QVERIFY(!H::findMenuAction(menu, Ids::ORDER_SEND_TO_BACK)->isEnabled());
    H::finish(context, menus);
}

QTEST_MAIN(AppToolContentPanelsTest)

#include "tst_apptoolcontentpanelstest.moc"
