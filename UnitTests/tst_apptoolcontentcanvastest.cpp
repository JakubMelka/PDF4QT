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

// Edit Content, stage E1, package P11: the canvas (view, scene, items, selection item, tools, Preview;
// zadani_gui.md 4.15, 4.16; zadani_overeni.md 8, the row of UnitTestsAppToolContentCanvas). Expected values from
// hand-written content and the plan (the boxes, the hit, the matrices of the gestures), never from a run. The canvas
// runs over a context of its own registry (the entries of P11 and test entries) without a GUI, except Preview, which
// needs the document of the tool (the real tool behind the developer switch).

#include "appdocumentfixture.h"
#include "apptoolshellfixture.h"
#include "contenttestdocuments.h"

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentitems.h"
#include "pdfappeditcontentkeyboard.h"
#include "pdfappeditcontentpreview.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentview.h"
#include "pdfapptoolgui_content.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolshell.h"
#include "pdfsettingdefinitions.h"

#include "pdfcatalog.h"
#include "pdfcontentundostack.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

#include <QAction>
#include <QApplication>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>

#include <memory>

using Context = pdf::PDFAppEditContentContext;
using Registry = pdf::PDFAppEditContentRegistry;
using SelectTool = pdf::PDFAppEditContentSelectTool;
using Handle = pdf::PDFAppEditContentSelectionItem::Handle;
using NodeReference = pdf::PDFContentNodeReference;

/// What the test entries saw (one test at a time)
class CanvasTestRecorder
{
public:
    static int entryCommandCount;       ///< The action entry of edit.selectAll was triggered
    static int toolCommandCount;        ///< The probe tool handled edit.selectAll
    static bool isToolAnswering;        ///< The probe tool answers edit.selectAll
    static int dropCount;
    static QPointF dropPoint;

    static void reset()
    {
        entryCommandCount = 0;
        toolCommandCount = 0;
        isToolAnswering = true;
        dropCount = 0;
        dropPoint = QPointF();
    }
};

int CanvasTestRecorder::entryCommandCount = 0;
int CanvasTestRecorder::toolCommandCount = 0;
bool CanvasTestRecorder::isToolAnswering = true;
int CanvasTestRecorder::dropCount = 0;
QPointF CanvasTestRecorder::dropPoint;

/// A tool of the test (key R), which answers the command edit.selectAll of the application before the entries
class CanvasProbeTool : public pdf::PDFAppEditContentTool
{
public:
    using pdf::PDFAppEditContentTool::PDFAppEditContentTool;

    virtual pdf::PDFAppToolCommandState getApplicationCommandState(const QString& commandId) const override
    {
        return commandId == pdf::PDFAppCommands::selectAll() && CanvasTestRecorder::isToolAnswering ? pdf::PDFAppToolCommandState::Enabled : pdf::PDFAppToolCommandState::NotHandled;
    }

    virtual void handleApplicationCommand(const QString& commandId) override
    {
        if (commandId == pdf::PDFAppCommands::selectAll())
        {
            ++CanvasTestRecorder::toolCommandCount;
        }
    }
};

class CanvasProbeToolEntry : public pdf::PDFAppEditContentToolEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("probe"); }
    virtual Place getPlace() const override { return Place::Toolbar; }
    virtual int getOrder() const override { return 900; }
    virtual QString getTitle() const override { return QStringLiteral("Probe"); }
    virtual QKeySequence getKey() const override { return QKeySequence(Qt::Key_R); }
    virtual std::unique_ptr<pdf::PDFAppEditContentTool> createTool(Context* context) const override { return std::make_unique<CanvasProbeTool>(context); }
};

/// The action entry of the test, which answers edit.selectAll (as P12's SELECT_ALL does)
class CanvasSelectAllEntry : public pdf::PDFAppEditContentActionEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("test.select-all"); }
    virtual QString getTitle(const Context*) const override { return QStringLiteral("Select All"); }
    virtual QStringList getApplicationCommandIds() const override { return { pdf::PDFAppCommands::selectAll() }; }
    virtual void trigger(Context*) const override { ++CanvasTestRecorder::entryCommandCount; }
};

/// The drop entry of the test: takes a plain text at the point of the page
class CanvasTextDropEntry : public pdf::PDFAppEditContentDropEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("test.text"); }
    virtual bool canDrop(const QMimeData* mimeData) const override { return mimeData->hasText(); }

    virtual bool drop(Context*, const QMimeData* mimeData, const QPointF& pagePoint) const override
    {
        Q_UNUSED(mimeData);
        ++CanvasTestRecorder::dropCount;
        CanvasTestRecorder::dropPoint = pagePoint;
        return true;
    }
};

/// Helpers of the test
class CanvasTestHelper
{
public:
    CanvasTestHelper() = delete;

    using D = pdftest::ContentTestDocuments;

    /// The page [0 0 200 200] of the basic tests: A = the red square 10 10 50 50 (filled), B = the blue line from 100 100
    /// to 180 100 (stroked, 4 pt wide), C = the green square 30 30 40 40 over A, D = a filled rectangle 110 10 60 60 under
    /// the clip 120 20 20 20 of its group, E = an inline image 40 x 20 at 100 150
    static QByteArray basicContent()
    {
        return QByteArrayLiteral("1 0 0 rg 10 10 50 50 re f\n"
                                 "0 0 1 RG 4 w 100 100 m 180 100 l S\n"
                                 "0 1 0 rg 30 30 40 40 re f\n"
                                 "q 120 20 20 20 re W n 0 0 1 rg 110 10 60 60 re f Q\n"
                                 "q 40 0 0 20 100 150 cm BI /W 1 /H 1 /BPC 8 /CS /G ID x EI Q\n");
    }

    static pdf::PDFDocumentPointer createDocument(const QByteArray& content, bool isRotated = false)
    {
        pdf::PDFDocument document = D::createDocumentFromObjects([&content](pdf::PDFDocumentBuilder* builder) { return D::addStreamObject(builder, D::DictionaryEntries(), content); },
                                                                 &D::createNoResources,
                                                                 [isRotated](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference page)
        {
            if (isRotated)
            {
                builder->mergeTo(page, D::createDictionaryObject({ { "Rotate", pdf::PDFObject::createInteger(90) } }));
            }
        });
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document)));
    }

    /// A document of two pages [0 0 200 200] with the basic content each
    static pdf::PDFDocumentPointer createTwoPageDocument()
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 2; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
            const pdf::PDFObject contents = D::addStreamObject(&builder, D::DictionaryEntries(), basicContent());
            builder.mergeTo(page, D::createDictionaryObject({ { "Resources", D::createDictionaryObject({ }) }, { "Contents", contents } }));
        }
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
    }

    /// The registry of P11 with the test entries
    static void fillRegistry(Registry* registry)
    {
        pdf::PDFAppEditContentTools::registerParts(registry);
        pdf::PDFAppEditContentItems::registerParts(registry);
        registry->addTool(std::make_unique<CanvasProbeToolEntry>());
        registry->addAction(std::make_unique<CanvasSelectAllEntry>());
        registry->addDrop(std::make_unique<CanvasTextDropEntry>());
    }

    static int timeout(int milliseconds)
    {
        return milliseconds * pdftest::AppDocumentFixture::getTimeoutFactor();
    }

    /// The children of the root of the page container in the order of the content
    static std::vector<NodeReference> getRootChildren(const Context* context)
    {
        std::vector<NodeReference> result;
        const pdf::PDFContentContainerPointer container = context->getContainer();
        for (const pdf::PDFContentNodePointer& child : container ? container->getRoot()->getChildren() : std::vector<pdf::PDFContentNodePointer>())
        {
            result.push_back(NodeReference{ container->getKey(), child->getId() });
        }
        return result;
    }

    /// The children of the root of the kinds, which are objects (no state runs)
    static std::vector<NodeReference> getObjects(const Context* context)
    {
        std::vector<NodeReference> result;
        const pdf::PDFContentContainerPointer container = context->getContainer();
        for (const NodeReference& node : getRootChildren(context))
        {
            if (container->getNode(node.node)->getKind() != pdf::PDFContentNodeKind::StateRun)
            {
                result.push_back(node);
            }
        }
        return result;
    }

    static QRectF box(const Context* context, const NodeReference& node)
    {
        return pdf::PDFAppEditContentGeometryHelper::getPageBoundingBox(context, node);
    }

    static bool isSame(const QRectF& actual, const QRectF& expected, qreal epsilon = 1e-6)
    {
        const bool isSame = qAbs(actual.left() - expected.left()) < epsilon && qAbs(actual.top() - expected.top()) < epsilon &&
                            qAbs(actual.width() - expected.width()) < epsilon && qAbs(actual.height() - expected.height()) < epsilon;
        if (!isSame)
        {
            qWarning() << "rectangle" << actual << "expected" << expected;
        }
        return isSame;
    }

    static bool isSame(const QPointF& actual, const QPointF& expected, qreal epsilon = 1e-6)
    {
        const bool isSame = qAbs(actual.x() - expected.x()) < epsilon && qAbs(actual.y() - expected.y()) < epsilon;
        if (!isSame)
        {
            qWarning() << "point" << actual << "expected" << expected;
        }
        return isSame;
    }
};

/// A canvas over a context of the test registry in a window with a button (another widget for the focus)
class CanvasTestFixture
{
public:
    /// \param addParts More entries of the registry (the real ones of another package, a widget of the test)
    explicit CanvasTestFixture(pdf::PDFDocumentPointer document, void (*addParts)(Registry*) = nullptr)
    {
        CanvasTestRecorder::reset();
        CanvasTestHelper::fillRegistry(&registry);
        if (addParts)
        {
            addParts(&registry);
        }
        context = new Context(std::move(document), pdf::PDFCMSPointer(), &registry, nullptr);

        window = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(window);
        button = new QPushButton(QStringLiteral("Other"), window);
        layout->addWidget(button);
        canvas = new pdf::PDFAppEditContentCanvas(context, window);
        layout->addWidget(canvas, 1);
        window->resize(800, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }

    ~CanvasTestFixture()
    {
        // The window with the canvas first (a part), then the context (order 2 of zadani_gui.md 4.14)
        pdf::PDFDeferredDeletion::retire(window.data());
        pdf::PDFDeferredDeletion::retire(context.data());
        pdf::PDFDeferredDeletion::flush();
    }

    /// Shows the page with a zoom of one pixel per point and waits for its container
    bool showPage(pdf::PDFInteger pageIndex)
    {
        QSignalSpy loaded(context.data(), &Context::containerChanged);
        context->setPage(pageIndex);
        if (!context->getContainer() && !loaded.wait(CanvasTestHelper::timeout(15000)))
        {
            return false;
        }
        QCoreApplication::processEvents();
        pdf::PDFAppEditContentView* view = canvas->getView();
        view->setZoom(72.0 / view->logicalDpiX());
        view->centerOn(QPointF(100.0, 100.0));
        view->setFocus();
        QCoreApplication::processEvents();
        return context->getContainer() && context->isEditingAllowed();
    }

    pdf::PDFAppEditContentView* view() const { return canvas->getView(); }
    pdf::PDFAppEditContentScene* scene() const { return canvas->getScene(); }
    pdf::PDFAppEditContentSelection* selection() const { return context->getSelection(); }

    /// The pixel of the viewport of a point of the page
    QPoint pixel(const QPointF& scenePoint) const
    {
        return view()->mapFromScene(scenePoint);
    }

    /// The point of the page of a pixel of the viewport (the expected values of a gesture: the pointer moves by pixels)
    QPointF point(const QPoint& pixel) const
    {
        return view()->mapToScene(pixel);
    }

    void sendMouse(QEvent::Type type, const QPoint& position, Qt::MouseButton mouseButton, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWidget* viewport = view()->viewport();
        QMouseEvent event(type, QPointF(position), viewport->mapToGlobal(QPointF(position)), mouseButton, buttons, modifiers);
        QApplication::sendEvent(viewport, &event);
    }

    void click(const QPoint& position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        sendMouse(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
        sendMouse(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
        QCoreApplication::processEvents();
    }

    /// A drag by the left button in steps (behind the distance of a drag of the system)
    void drag(const QPoint& from, const QPoint& to, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool isReleased = true)
    {
        sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
        constexpr int steps = 5;
        for (int i = 1; i <= steps; ++i)
        {
            const QPoint position = from + (to - from) * i / steps;
            sendMouse(QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton, modifiers);
        }
        if (isReleased)
        {
            sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
        }
        QCoreApplication::processEvents();
    }

    Registry registry;
    QPointer<Context> context;
    QPointer<QWidget> window;
    QPushButton* button = nullptr;
    pdf::PDFAppEditContentCanvas* canvas = nullptr;
};

class AppToolContentCanvasTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // items and scene
    void itemsOfTheKinds();
    void itemNotDeletedInsideOfAnEvent();
    void changeRebuildsOnlyItsItems();
    void clipHidesContent();

    // hit and selection
    void hitByShape();
    void clickSelects();
    void altClickWalksDown();
    void rubberBandSelects();
    void lockedObjectClickGoesThrough();

    // gestures: one command each
    void moveIssuesTransform();
    void shiftMoveKeepsDirection();
    void ctrlDragCopies();
    void resizeByHandle();
    void resizeImageKeepsProportions();
    void rotateByHandle();
    void arrowKeysMoveInScreenDirections();
    void escapeCancelsDrag();
    void complexPageDragsOutline();

    // view, keys, commands, drops
    void zoomAndPageMemory();
    void fitWaitsForTheShownView();
    void toolKeysOnlyWithFocus();
    void canvasShortcutRoutesApplicationCommand();
    void activeToolAnswersApplicationCommandFirst();
    void dropFromOutside();

    // Preview
    void previewWithoutGuiRenders();
    void previewOfMissingPageFails();
    void previewRendersThePage();

    // FX2 view, tools, Select, Preview
    void keyPTogglesPreviewOnTheCanvas();
    void previewZoomWithoutScrollRendersAgain();
    void rubberBandPreviewRectangle();
    void selectionBoxesComputedOncePerChange();
    void gestureEndsWhenItsNodesChange();
    void gesturesLeaveLockedNodes();
    void selectionAnnouncedOncePerTurn();
    void scrollRangeCoversOverlays();

    // FX2 scene and items
    void formsSharedAndNestedUnderBudget();
    void editorStateIsOneWalkPerTurn();
    void clipChainsSharedAndKept();
    void longClipChainIsLinear();
    void selectionSetsAreLogLinear();
    void hitReturnsEachObjectOnce();
    void imagePicturesSharedPerObject();
    void imagePicturesWithinBudget();
    void formUseClippedByPageClips();
    void hiddenFormUseHidesContent();
    void imageHitByItsShapeAndClip();
    void hiddenObjectHitByOutline();
    void dimmingKeepsConstantAlpha();
    void shadingAndImageAlphaAndBlend();
    void shadingBoxFollowsItsClip();
    void textClipApplied();
    void patternFillsDrawn();
    void functionShadingDrawnAsPicture();
    void annotationsUnderlay();
    void paintSwitchedOffHasNoShape();
    void sessionReplacedReconnects();
    void toolPreviewCoversItsRectangle();
    void sceneRectangleBounded();
};

void AppToolContentCanvasTest::initTestCase()
{
    qRegisterMetaType<pdf::PDFAppEditContentNoticeRequest>();
    qRegisterMetaType<pdf::PDFContentNodeReference>();
}

void AppToolContentCanvasTest::itemsOfTheKinds()
{
    // zadani_gui.md 4.15, the table of the items: every kind of node gets the item of its entry, no item is a QObject
    // (4.14), a form use draws the content of its form as its children
    using D = pdftest::ContentTestDocuments;
    const QByteArray content = QByteArrayLiteral("1 0 0 rg 10 10 20 20 re f\n"
                                                 "q 50 50 20 20 re W n Q\n"
                                                 "BT /F1 12 Tf 20 150 Td (AB) Tj ET\n"
                                                 "q 20 0 0 10 150 150 cm BI /W 1 /H 1 /BPC 8 /CS /G ID x EI Q\n"
                                                 "q 1 0 0 1 50 0 cm /Fm1 Do Q\n"
                                                 "/Sh1 sh\n");
    pdf::PDFDocument document = D::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
    {
        const pdf::PDFObject form = D::addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") }, { "Subtype", pdf::PDFObject::createName("Form") },
                                                                  { "BBox", D::createNumberArrayObject({ 0, 0, 40, 40 }) }, { "Resources", D::createDictionaryObject({ }) } },
                                                       QByteArrayLiteral("0 0 10 10 re f 20 20 10 10 re f"));
        const pdf::PDFObject function = D::createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) }, { "Domain", D::createNumberArrayObject({ 0, 1 }) },
                                                                    { "C0", D::createNumberArrayObject({ 1, 0, 0 }) }, { "C1", D::createNumberArrayObject({ 0, 0, 1 }) },
                                                                    { "N", pdf::PDFObject::createInteger(1) } });
        const pdf::PDFObject shading = D::createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) }, { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                                   { "Coords", D::createNumberArrayObject({ 0, 0, 100, 0 }) }, { "Function", function } });
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), D::createDictionaryObject({ { "F1", D::addSimpleFontObject(builder) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), D::createDictionaryObject({ { "Fm1", form } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Shading"), D::createDictionaryObject({ { "Sh1", shading } }));
        return resources;
    });

    CanvasTestFixture fixture(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))));
    QVERIFY(fixture.showPage(0));
    const pdf::PDFContentContainerPointer container = fixture.context->getContainer();

    // The item of each object of the root by its kind (the groups q ... Q are marker items, their children have items)
    int formCount = 0;
    std::vector<pdf::PDFAppEditContentItem*> items;
    std::vector<const pdf::PDFContentNode*> stack{ container->getRoot().get() };
    while (!stack.empty())
    {
        const pdf::PDFContentNode* node = stack.back();
        stack.pop_back();
        for (const pdf::PDFContentNodePointer& child : node->getChildren())
        {
            stack.push_back(child.get());
        }
        if (node->getKind() == pdf::PDFContentNodeKind::Root || node->getKind() == pdf::PDFContentNodeKind::StateRun || node->getKind() == pdf::PDFContentNodeKind::TextShow)
        {
            continue;
        }

        pdf::PDFAppEditContentItem* item = fixture.scene()->findItem(NodeReference{ container->getKey(), node->getId() });
        QVERIFY2(item, qPrintable(QString::number(int(node->getKind()))));
        items.push_back(item);
        QCOMPARE(item->getNodeId(), node->getId());
        switch (node->getKind())
        {
            case pdf::PDFContentNodeKind::Path:
                QVERIFY(node->getData().paint.isClippingPath() ? dynamic_cast<pdf::PDFAppEditContentClipPathItem*>(item) != nullptr : dynamic_cast<pdf::PDFAppEditContentPathItem*>(item) != nullptr);
                break;
            case pdf::PDFContentNodeKind::TextObject:
            {
                pdf::PDFAppEditContentTextItem* text = dynamic_cast<pdf::PDFAppEditContentTextItem*>(item);
                QVERIFY(text);
                QVERIFY(text->getText());
                QCOMPARE(text->getText()->glyphs.size(), size_t(2));
                break;
            }
            case pdf::PDFContentNodeKind::Image:
                QVERIFY(dynamic_cast<pdf::PDFAppEditContentImageItem*>(item));
                break;
            case pdf::PDFContentNodeKind::FormUse:
                QVERIFY(dynamic_cast<pdf::PDFAppEditContentFormItem*>(item));
                ++formCount;
                break;
            case pdf::PDFContentNodeKind::Shading:
                QVERIFY(dynamic_cast<pdf::PDFAppEditContentShadingItem*>(item));
                break;
            case pdf::PDFContentNodeKind::Group:
            case pdf::PDFContentNodeKind::MarkedContent:
                QVERIFY(dynamic_cast<pdf::PDFAppEditContentGroupItem*>(item));
                break;
            default:
                break;
        }
    }
    QCOMPARE(formCount, 1);

    // Not a QObject (zadani_gui.md 4.14: no QGraphicsObject anywhere)
    for (pdf::PDFAppEditContentItem* item : items)
    {
        QVERIFY(!item->toGraphicsObject());
        QCOMPARE(item->type(), int(pdf::PDFAppEditContentItem::Type));
        QVERIFY(item->acceptedMouseButtons() == Qt::NoButton);
    }

    // The form use: its two paths are its children, under /BBox and the move of the use
    pdf::PDFAppEditContentFormItem* formItem = nullptr;
    for (pdf::PDFAppEditContentItem* item : items)
    {
        formItem = formItem ? formItem : dynamic_cast<pdf::PDFAppEditContentFormItem*>(item);
    }
    QTRY_VERIFY_WITH_TIMEOUT(formItem->getFormVersion() != 0, CanvasTestHelper::timeout(10000));
    QCOMPARE(formItem->childItems().size(), qsizetype(2));
    QVERIFY(formItem->flags().testFlag(QGraphicsItem::ItemClipsChildrenToShape));
    QVERIFY(CanvasTestHelper::isSame(formItem->sceneBoundingRect(), QRectF(50, 0, 40, 40)));
}

void AppToolContentCanvasTest::itemNotDeletedInsideOfAnEvent()
{
    // zadani_gui.md 4.14: a replaced item is hidden and deleted from the event loop, never inside of the processing of
    // the change (here the slot of modelChanged)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const int itemCount = fixture.scene()->getContentItemCount();

    // A delete of A: its item goes
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createDelete({ objects[0] })));
    QCOMPARE(fixture.scene()->getRetiredItemCount(), 1);
    QVERIFY(!fixture.scene()->findItem(objects[0]));
    QCOMPARE(fixture.scene()->getContentItemCount(), itemCount - 1);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.scene()->getRetiredItemCount(), 0);
}

void AppToolContentCanvasTest::changeRebuildsOnlyItsItems()
{
    // zadani.md 9.3 updateNodes, zadani_gui.md 4.24: after a command only the items of its nodes change; the item of
    // a moved node takes its new data
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    pdf::PDFAppEditContentItem* itemA = fixture.scene()->findItem(objects[0]);
    pdf::PDFAppEditContentItem* itemB = fixture.scene()->findItem(objects[1]);
    pdf::PDFAppEditContentItem* itemC = fixture.scene()->findItem(objects[2]);
    QVERIFY(itemA && itemB && itemC);
    const QRectF boxB = itemB->sceneBoundingRect();

    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createTransform({ objects[0] }, QTransform::fromTranslate(100, 0))));
    QCOMPARE(fixture.scene()->findItem(objects[1]), itemB);
    QCOMPARE(fixture.scene()->findItem(objects[2]), itemC);
    QCOMPARE(fixture.scene()->getRetiredItemCount(), 0);
    QVERIFY(CanvasTestHelper::isSame(itemB->sceneBoundingRect(), boxB));

    // A: 10 10 50 50 moved by 100 (the bounding rectangle has the margin of the item)
    pdf::PDFAppEditContentItem* movedA = fixture.scene()->findItem(objects[0]);
    QVERIFY(movedA);
    QVERIFY(movedA->sceneBoundingRect().contains(QRectF(110, 10, 50, 50)));
    QVERIFY(!movedA->sceneBoundingRect().intersects(QRectF(0, 0, 100, 100)));
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(115, 15), 1.0, false), std::vector<NodeReference>{ objects[0] });

    // Undo brings A back
    QVERIFY(fixture.context->getSession()->undo().has_value());
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(20, 20), 1.0, false), std::vector<NodeReference>{ objects[0] });
}

void AppToolContentCanvasTest::clipHidesContent()
{
    // ISO 32000-2, 8.5.4: the clip W n of the group applies to the rectangle behind it - outside of 120 20 20 20 the
    // rectangle D (110 10 60 60) is neither drawn nor hit
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const NodeReference groupD = objects[3];
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(130, 30), 0.5, false), std::vector<NodeReference>{ groupD });
    QVERIFY(fixture.scene()->getNodesAt(QPointF(115, 15), 0.5, false).empty());
    QVERIFY(fixture.scene()->getNodesAt(QPointF(160, 50), 0.5, false).empty());

    // The clipping path itself is hit only while View > Clipping paths shows it (its top edge y = 40; the point is
    // outside of the clip, so D does not take it)
    QVERIFY(fixture.scene()->getNodesAt(QPointF(130, 40.5), 1.0, false).empty());
    fixture.context->getOptions()->setValue(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_CLIPPING_PATHS), true);
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(130, 40.5), 1.0, false), std::vector<NodeReference>{ groupD });
}

void AppToolContentCanvasTest::hitByShape()
{
    // zadani_gui.md 4.15 "hit": the topmost object by its shape - a fill by its area, a stroke by a band of at least the
    // tolerance around its outline; children of a group give the child of the edited root
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    QCOMPARE(objects.size(), size_t(5));
    const NodeReference a = objects[0];
    const NodeReference b = objects[1];
    const NodeReference c = objects[2];
    pdf::PDFAppEditContentScene* scene = fixture.scene();

    QCOMPARE(scene->getNodesAt(QPointF(20, 20), 1.0, false), std::vector<NodeReference>{ a });
    QCOMPARE(scene->getNodesAt(QPointF(40, 40), 1.0, false), (std::vector<NodeReference>{ c, a }));
    QCOMPARE(scene->getNodesAt(QPointF(65, 65), 1.0, false), std::vector<NodeReference>{ c });
    QVERIFY(scene->getNodesAt(QPointF(80, 80), 1.0, false).empty());

    // The line B (4 pt wide): its band is the larger one of the line and of the tolerance (2 x 3 pt here)
    QCOMPARE(scene->getNodesAt(QPointF(140, 102.5), 3.0, false), std::vector<NodeReference>{ b });
    QCOMPARE(scene->getNodesAt(QPointF(140, 97.5), 3.0, false), std::vector<NodeReference>{ b });
    QVERIFY(scene->getNodesAt(QPointF(140, 103.5), 3.0, false).empty());
    QCOMPARE(scene->getNodesAt(QPointF(140, 104.5), 6.0, false), std::vector<NodeReference>{ b });

    // A locked object is left out, unless asked for (the card Locked object)
    fixture.context->getSession()->setNodeLocked(c, true);
    QCOMPARE(scene->getNodesAt(QPointF(40, 40), 1.0, false), std::vector<NodeReference>{ a });
    QCOMPARE(scene->getNodesAt(QPointF(40, 40), 1.0, true), (std::vector<NodeReference>{ c, a }));
}

void AppToolContentCanvasTest::clickSelects()
{
    // zadani_gui.md 4.16, Select: a click selects, Shift + click adds (the key object is the last one), Ctrl + click on a
    // selected object removes it, a click on nothing clears
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    pdf::PDFAppEditContentSelection* selection = fixture.selection();
    QCOMPARE(fixture.context->getToolController()->getActiveToolId(), QStringLiteral("select"));

    fixture.click(fixture.pixel(QPointF(20, 20)));
    QCOMPARE(selection->getNodes(), std::vector<NodeReference>{ objects[0] });

    fixture.click(fixture.pixel(QPointF(140, 100)), Qt::ShiftModifier);
    QCOMPARE(selection->getNodes(), (std::vector<NodeReference>{ objects[0], objects[1] }));
    QCOMPARE(selection->getKeyNode(), objects[1]);

    fixture.click(fixture.pixel(QPointF(20, 20)), Qt::ControlModifier);
    QCOMPARE(selection->getNodes(), std::vector<NodeReference>{ objects[1] });

    fixture.click(fixture.pixel(QPointF(190, 190)));
    QVERIFY(selection->isEmpty());

    // The frame of the selection is the box of the node (the one policy of the box) with its handles
    fixture.click(fixture.pixel(QPointF(20, 20)));
    QVERIFY(CanvasTestHelper::isSame(fixture.scene()->getSelectionItem()->getHandleBox(), QRectF(10, 10, 50, 50)));
}

void AppToolContentCanvasTest::altClickWalksDown()
{
    // zadani_gui.md 4.15: Alt + click walks through the objects under each other (C over A)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const QPoint position = fixture.pixel(QPointF(40, 40));

    fixture.click(position, Qt::AltModifier);
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ objects[2] });
    fixture.click(position, Qt::AltModifier);
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ objects[0] });
    fixture.click(position, Qt::AltModifier);
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ objects[2] });
}

void AppToolContentCanvasTest::rubberBandSelects()
{
    // zadani_gui.md 4.16: a drag over nothing selects the objects inside of the rectangle (A 10..60 and C 30..70 lie in
    // 5..75; B, D, E do not)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);

    fixture.drag(fixture.pixel(QPointF(5, 5)), fixture.pixel(QPointF(75, 75)), Qt::NoModifier, false);
    auto* tool = static_cast<SelectTool*>(fixture.context->getToolController()->getActiveTool());
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::RubberBand);
    fixture.sendMouse(QEvent::MouseButtonRelease, fixture.pixel(QPointF(75, 75)), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(fixture.selection()->getNodes(), (std::vector<NodeReference>{ objects[0], objects[2] }));
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::None);

    // With Shift the rectangle adds (B lies in 95..185 x 95..105)
    fixture.drag(fixture.pixel(QPointF(95, 95)), fixture.pixel(QPointF(185, 105)), Qt::ShiftModifier);
    QCOMPARE(fixture.selection()->getNodes(), (std::vector<NodeReference>{ objects[0], objects[2], objects[1] }));
}

void AppToolContentCanvasTest::lockedObjectClickGoesThrough()
{
    // -locked-click: a click on a locked object does not select it - it goes through to the object below; the frame of
    // the locked object and its card stay until the selection changes
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    fixture.context->getSession()->setNodeLocked(objects[2], true);
    const QString lockedCard = QString::fromLatin1(pdf::PDFAppEditContentCardIds::LOCKED_OBJECT);

    fixture.click(fixture.pixel(QPointF(40, 40)));
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ objects[0] });
    QCOMPARE(fixture.context->getLockedClickNode(), objects[2]);
    QVERIFY(fixture.context->isCardRequested(lockedCard));

    // The next change of the selection ends it
    fixture.click(fixture.pixel(QPointF(140, 100)));
    QVERIFY(!fixture.context->getLockedClickNode().isValid());
    QVERIFY(!fixture.context->isCardRequested(lockedCard));

    // A locked object alone: nothing is selected
    fixture.click(fixture.pixel(QPointF(65, 65)));
    QVERIFY(fixture.selection()->isEmpty());
    QCOMPARE(fixture.context->getLockedClickNode(), objects[2]);
}

void AppToolContentCanvasTest::moveIssuesTransform()
{
    // zadani_gui.md 4.16: a drag of an object moves it by the move of the pointer in the page space - one command
    // Transform at the release; during the drag the item is drawn moved
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const QPoint from = fixture.pixel(QPointF(20, 20));
    const QPoint to = from + QPoint(40, -30);
    const QPointF delta = fixture.point(to) - fixture.point(from);

    fixture.drag(from, to, Qt::NoModifier, false);
    pdf::PDFAppEditContentItem* item = fixture.scene()->findItem(objects[0]);
    QVERIFY(CanvasTestHelper::isSame(item->sceneTransform().map(QPointF(0, 0)), delta));
    QVERIFY(fixture.context->getSession()->getUndoStack()->isClean());

    fixture.sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50, 50).translated(delta)));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[2]), QRectF(30, 30, 40, 40)));
    QVERIFY(fixture.scene()->findItem(objects[0])->transform().isIdentity());
}

void AppToolContentCanvasTest::shiftMoveKeepsDirection()
{
    // Shift: only horizontally or vertically (the axis of the larger move)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    fixture.click(fixture.pixel(QPointF(20, 20)));
    const QPoint from = fixture.pixel(QPointF(20, 20));
    const QPoint to = from + QPoint(50, 8);
    const QPointF delta = fixture.point(to) - fixture.point(from);

    fixture.drag(from, to, Qt::ShiftModifier);
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10 + delta.x(), 10, 50, 50)));
}

void AppToolContentCanvasTest::ctrlDragCopies()
{
    // zadani_gui.md 4.16: Ctrl + drag makes a copy at the new place (Copy), the original stays, the copy is selected
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    fixture.click(fixture.pixel(QPointF(20, 20)));
    const QPoint from = fixture.pixel(QPointF(20, 20));
    const QPoint to = from + QPoint(0, -120);
    const QPointF delta = fixture.point(to) - fixture.point(from);

    fixture.drag(from, to, Qt::ControlModifier);
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));
    const std::vector<NodeReference> after = CanvasTestHelper::getObjects(fixture.context);
    QCOMPARE(after.size(), objects.size() + 1);
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50, 50)));
    QCOMPARE(fixture.selection()->getCount(), 1);
    const NodeReference copy = fixture.selection()->getKeyNode();
    QVERIFY(std::find(objects.cbegin(), objects.cend(), copy) == objects.cend());
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, copy), QRectF(10, 10, 50, 50).translated(delta)));
}

void AppToolContentCanvasTest::resizeByHandle()
{
    // zadani_gui.md 4.16: a handle resizes around the opposite handle; another object than an image keeps no proportions
    // without Shift (Right: only the width), a corner with Shift keeps them
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    fixture.click(fixture.pixel(QPointF(20, 20)));
    pdf::PDFAppEditContentSelectionItem* item = fixture.scene()->getSelectionItem();
    const qreal offset = pdf::PDFAppEditContentSelectionItem::FRAME_OFFSET * fixture.scene()->getPixelSize();
    const QRectF frame = item->getHandleBox().adjusted(-offset, -offset, offset, offset);

    // R6-10 (changed expectation: the edge followed the absolute pointer, a jump by the distance of the grab point from
    // the edge): a press on the handle Right (drawn 3 px outside of the box) 4 px right of its center, a drag by dx -
    // the right edge of the box 10 10 50 50 moves by dx (the anchor is the left edge x = 10), no jump
    const QPoint center = fixture.pixel(pdf::PDFAppEditContentSelectionItem::getHandlePoint(frame, Handle::Right));
    const QPoint from = center + QPoint(4, 0);
    QCOMPARE(item->getHandleAt(fixture.point(from), fixture.scene()->getPixelSize()), Handle::Right);

    // A press with a jitter of 1 px below the distance of a drag of the system: no resize, no command
    fixture.sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    fixture.sendMouse(QEvent::MouseMove, from + QPoint(1, 0), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(static_cast<SelectTool*>(fixture.context->getToolController()->getActiveTool())->getGesture(), SelectTool::Gesture::Pending);
    fixture.sendMouse(QEvent::MouseButtonRelease, from + QPoint(1, 0), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QVERIFY(fixture.context->getSession()->getUndoStack()->isClean());
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50, 50)));

    const QPoint to = from + QPoint(50, 12);
    const qreal dx = fixture.point(to).x() - fixture.point(from).x();
    fixture.drag(from, to);
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50 + dx, 50)));

    // The pure function: a corner with the proportions kept takes the larger scale for both axes
    const QTransform corner = SelectTool::getResizeTransform(QRectF(10, 10, 50, 50), int(Handle::TopRight), QPointF(110, 80), true);
    QVERIFY(CanvasTestHelper::isSame(corner.mapRect(QRectF(10, 10, 50, 50)), QRectF(10, 10, 100, 100)));
    const QTransform free = SelectTool::getResizeTransform(QRectF(10, 10, 50, 50), int(Handle::TopRight), QPointF(110, 80), false);
    QVERIFY(CanvasTestHelper::isSame(free.mapRect(QRectF(10, 10, 50, 50)), QRectF(10, 10, 100, 70)));
}

void AppToolContentCanvasTest::resizeImageKeepsProportions()
{
    // zadani_gui.md 4.16: an image keeps its proportions, Shift frees them - the corner TopRight of the image 100 150
    // 40 20 (20 pt high: below 24 px only the corners have handles) scales both sides by the larger scale around the
    // corner BottomLeft (100, 150)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const NodeReference image = objects[4];
    fixture.click(fixture.pixel(QPointF(120, 160)));
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ image });
    QVERIFY(CanvasTestHelper::isSame(fixture.scene()->getSelectionItem()->getHandleBox(), QRectF(100, 150, 40, 20)));

    const qreal offset = pdf::PDFAppEditContentSelectionItem::FRAME_OFFSET * fixture.scene()->getPixelSize();
    const QRectF frame = fixture.scene()->getSelectionItem()->getHandleBox().adjusted(-offset, -offset, offset, offset);
    QVERIFY(!fixture.scene()->getSelectionItem()->isHandleShown(Handle::Right));
    const QPoint from = fixture.pixel(pdf::PDFAppEditContentSelectionItem::getHandlePoint(frame, Handle::TopRight));
    QCOMPARE(fixture.scene()->getSelectionItem()->getHandleAt(fixture.point(from), fixture.scene()->getPixelSize()), Handle::TopRight);
    // R6-10 (changed expectation: the corner followed the absolute pointer): the corner (140, 170) of the box moves by
    // the move of the pointer, the grab offset of the handle (3 px outside) is kept
    const QPoint to = fixture.pixel(QPointF(180, 175));
    const QPointF moved = QPointF(140, 170) + (fixture.point(to) - fixture.point(from));
    const qreal scale = qMax((moved.x() - 100.0) / 40.0, (moved.y() - 150.0) / 20.0);
    fixture.drag(from, to);
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, image), QRectF(100, 150, 40 * scale, 20 * scale), 1e-4));

    // Shift frees the proportions: the corner moves by the move of the pointer in both axes
    QVERIFY(fixture.context->getSession()->undo().has_value());
    fixture.click(fixture.pixel(QPointF(120, 160)));
    const QPoint to2 = fixture.pixel(QPointF(180, 175));
    fixture.drag(from, to2, Qt::ShiftModifier);
    const QPointF corner = QPointF(140, 170) + (fixture.point(to2) - fixture.point(from));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, image), QRectF(100, 150, corner.x() - 100, corner.y() - 150), 1e-4));
}

void AppToolContentCanvasTest::rotateByHandle()
{
    // zadani_gui.md 4.16: the round handle rotates around the center of the box, Shift by 15 degrees - the line B
    // (100 100 - 180 100) by 90 degrees counterclockwise in the page space around (140, 100): the point (180, 100)
    // goes to (140, 140)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    fixture.click(fixture.pixel(QPointF(140, 100)));
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ objects[1] });

    pdf::PDFAppEditContentSelectionItem* item = fixture.scene()->getSelectionItem();
    const QPointF handle = item->getRotationHandlePoint();
    const QPointF center(140, 100);
    const QPointF target = center + QPointF(-(handle.y() - center.y()), handle.x() - center.x());
    const QPoint from = fixture.pixel(handle);
    QCOMPARE(item->getHandleAt(fixture.point(from), fixture.scene()->getPixelSize()), Handle::Rotation);
    fixture.drag(from, fixture.pixel(target), Qt::ShiftModifier);
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));

    const pdf::PDFContentContainerPointer container = fixture.context->getContainer();
    const std::optional<pdf::PDFContentStateOverride> override = container->getEffectiveOverride(objects[1].node);
    QVERIFY(override);
    QVERIFY(CanvasTestHelper::isSame(override->transform.map(QPointF(180, 100)), QPointF(140, 140), 1e-6));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[1]), QRectF(140, 60, 0, 80), 1e-6));
}

void AppToolContentCanvasTest::arrowKeysMoveInScreenDirections()
{
    // zadani_gui.md 4.15: the arrows move by 1 pt (Shift 10 pt) in the directions of the screen; /Rotate 90 turns the
    // page clockwise, so the right of the screen is +y of the page
    for (const bool isRotated : { false, true })
    {
        CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent(), isRotated));
        QVERIFY(fixture.showPage(0));
        const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
        fixture.selection()->setNode(objects[0]);

        QTest::keyClick(fixture.view(), Qt::Key_Right);
        QTest::keyClick(fixture.view(), Qt::Key_Up, Qt::ShiftModifier);
        const QRectF expected = isRotated ? QRectF(10 - 10, 10 + 1, 50, 50) : QRectF(10 + 1, 10 + 10, 50, 50);
        QVERIFY2(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), expected), isRotated ? "rotated" : "not rotated");
    }
}

void AppToolContentCanvasTest::escapeCancelsDrag()
{
    // KE.16: Esc ends a drag in progress without a command (the controller answers true); without a drag Select has
    // nothing to cancel
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const QPoint from = fixture.pixel(QPointF(20, 20));
    fixture.drag(from, from + QPoint(40, 0), Qt::NoModifier, false);
    pdf::PDFAppEditContentToolController* controller = fixture.context->getToolController();
    QVERIFY(controller->isDragActive());
    QVERIFY(controller->cancel());
    QVERIFY(!controller->isDragActive());
    QVERIFY(fixture.scene()->findItem(objects[0])->transform().isIdentity());
    fixture.sendMouse(QEvent::MouseButtonRelease, from + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
    QVERIFY(fixture.context->getSession()->getUndoStack()->isClean());
    QVERIFY(!controller->cancel());
}

void AppToolContentCanvasTest::complexPageDragsOutline()
{
    // zadani_gui.md 4.24 and QE.10: on a page of more than 20 000 objects a drag moves the common outline (the frames),
    // not the items; on a small page the items move
    QByteArray content;
    for (int i = 0; i <= int(pdf::PDFAppEditContentScene::COMPLEX_PAGE_OBJECTS); ++i)
    {
        content.append(QByteArrayLiteral("150 150 2 2 re f\n"));
    }
    content.append(QByteArrayLiteral("10 10 50 50 re f\n"));

    CanvasTestFixture fixture(CanvasTestHelper::createDocument(content));
    QVERIFY(fixture.showPage(0));
    QVERIFY(fixture.scene()->isComplexPage());
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const NodeReference last = objects.back();
    const QPoint from = fixture.pixel(QPointF(20, 20));
    fixture.click(from);
    QCOMPARE(fixture.selection()->getNodes(), std::vector<NodeReference>{ last });
    fixture.drag(from, from + QPoint(30, 0), Qt::NoModifier, false);
    QVERIFY(fixture.scene()->isGestureActive());
    QVERIFY(fixture.scene()->isGestureOutlineOnly());
    QVERIFY(fixture.scene()->findItem(last)->transform().isIdentity());
    QVERIFY(fixture.scene()->getSelectionItem()->getHandleBox().left() > 10.0 + 1.0);
    fixture.sendMouse(QEvent::MouseButtonRelease, from + QPoint(30, 0), Qt::LeftButton, Qt::NoButton);

    CanvasTestFixture small(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(small.showPage(0));
    QVERIFY(!small.scene()->isComplexPage());
}

void AppToolContentCanvasTest::zoomAndPageMemory()
{
    // zadani_gui.md 4.15: the zoom and the position are kept per page until the tool closes; the zoom is bounded; the
    // text of the zoom of the status bar
    CanvasTestFixture fixture(CanvasTestHelper::createTwoPageDocument());
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentView* view = fixture.view();
    view->setZoom(2.0);
    QCOMPARE(view->getZoom(), 2.0);
    QCOMPARE(view->getZoomText(), QStringLiteral("200 %"));
    view->setZoom(1000.0);
    QCOMPARE(view->getZoom(), pdf::PDFAppEditContentView::MAXIMUM_ZOOM);
    view->setZoom(0.0001);
    QCOMPARE(view->getZoom(), pdf::PDFAppEditContentView::MINIMUM_ZOOM);
    view->setZoom(2.0);
    view->zoomIn();
    QVERIFY(qFuzzyCompare(view->getZoom(), 2.0 * pdf::PDFAppEditContentView::ZOOM_STEP));
    view->zoomActualSize();
    QCOMPARE(view->getZoom(), 1.0);
    view->setZoom(3.0);

    QSignalSpy loaded(fixture.context.data(), &Context::containerChanged);
    fixture.context->setPage(1);
    QVERIFY(fixture.context->getContainer() || loaded.wait(CanvasTestHelper::timeout(15000)));
    QVERIFY(!qFuzzyCompare(view->getZoom(), 3.0));
    fixture.context->setPage(0);
    QCOMPARE(view->getZoom(), 3.0);

    // A page switch returns to Select and leaves no selection of the other page
    QCOMPARE(fixture.context->getToolController()->getActiveToolId(), QStringLiteral("select"));
}

void AppToolContentCanvasTest::fitWaitsForTheShownView()
{
    // zadani_gui.md 4.15: a page without a view of its own is fitted into the view. The tool builds its canvas and loads
    // the page before its GUI is shown: the fit uses the size of the shown view, not the one of a new widget (the page
    // stayed at the smallest zoom). A zoom, which was asked for before the show, holds.
    for (const bool isZoomAsked : { false, true })
    {
        Registry registry;
        CanvasTestHelper::fillRegistry(&registry);
        QPointer<Context> context = new Context(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()), pdf::PDFCMSPointer(), &registry, nullptr);
        QPointer<QWidget> window = new QWidget();
        const auto cleanup = qScopeGuard([&window, &context]()
        {
            pdf::PDFDeferredDeletion::retire(window.data());
            pdf::PDFDeferredDeletion::retire(context.data());
            pdf::PDFDeferredDeletion::flush();
        });
        QVBoxLayout* layout = new QVBoxLayout(window);
        pdf::PDFAppEditContentCanvas* canvas = new pdf::PDFAppEditContentCanvas(context, window);
        layout->addWidget(canvas);

        QSignalSpy loaded(context.data(), &Context::containerChanged);
        context->setPage(0);
        QVERIFY(context->getContainer() || loaded.wait(CanvasTestHelper::timeout(15000)));
        QCoreApplication::processEvents();

        pdf::PDFAppEditContentView* view = canvas->getView();
        if (isZoomAsked)
        {
            view->setZoom(2.0);
        }

        window->resize(800, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCoreApplication::processEvents();

        // The page [0 0 200 200] in the viewport without the margins of the fit (1.0 = one point is 1/72 inch)
        const qreal pagePixels = 200.0 * view->logicalDpiX() / 72.0;
        const int margins = 2 * pdf::PDFAppEditContentView::FIT_MARGIN;
        const qreal fitted = qMin((view->viewport()->width() - margins) / pagePixels, (view->viewport()->height() - margins) / pagePixels);
        QVERIFY(view->viewport()->width() > 400 && view->viewport()->height() > 400);
        QVERIFY(!qFuzzyCompare(fitted, 2.0));
        QVERIFY2(qFuzzyCompare(view->getZoom(), isZoomAsked ? 2.0 : fitted), qPrintable(QString::number(view->getZoom())));

        // A later change of the size keeps the zoom (the fit is done once per page)
        const qreal zoom = view->getZoom();
        window->resize(600, 700);
        QCoreApplication::processEvents();
        QCOMPARE(view->getZoom(), zoom);
    }

    // A page, which is shown first while the view is hidden with its size (no change of the size comes at the show): it
    // is fitted at the show, and once (a later change of the size keeps the zoom)
    CanvasTestFixture fixture(CanvasTestHelper::createTwoPageDocument());
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentView* view = fixture.view();
    fixture.window->hide();
    QSignalSpy loaded(fixture.context.data(), &Context::containerChanged);
    fixture.context->setPage(1);
    QVERIFY(loaded.count() > 0 || loaded.wait(CanvasTestHelper::timeout(15000)));
    QCoreApplication::processEvents();
    fixture.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    QCoreApplication::processEvents();

    const qreal pagePixels = 200.0 * view->logicalDpiX() / 72.0;
    const int margins = 2 * pdf::PDFAppEditContentView::FIT_MARGIN;
    const qreal fitted = qMin((view->viewport()->width() - margins) / pagePixels, (view->viewport()->height() - margins) / pagePixels);
    QVERIFY2(qFuzzyCompare(view->getZoom(), fitted), qPrintable(QString::number(view->getZoom())));
    fixture.window->resize(600, 700);
    QCoreApplication::processEvents();
    QVERIFY2(qFuzzyCompare(view->getZoom(), fitted), qPrintable(QString::number(view->getZoom())));
}

void AppToolContentCanvasTest::toolKeysOnlyWithFocus()
{
    // zadani_gui.md 4.20, way 1: the unmodified keys of the tools act only while the canvas has the focus; the view takes
    // the shortcut override, so a shortcut of the window on the same bare key does not fire then
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    int windowShortcutCount = 0;
    QAction* action = new QAction(fixture.window);
    action->setShortcut(QKeySequence(Qt::Key_R));
    action->setShortcutContext(Qt::WindowShortcut);
    fixture.window->addAction(action);
    QObject::connect(action, &QAction::triggered, action, [&windowShortcutCount]() { ++windowShortcutCount; });
    pdf::PDFAppEditContentToolController* controller = fixture.context->getToolController();

    fixture.button->setFocus();
    QTest::keyClick(fixture.button, Qt::Key_R);
    QCOMPARE(controller->getActiveToolId(), QStringLiteral("select"));
    QCOMPARE(windowShortcutCount, 1);

    fixture.view()->setFocus();
    QTest::keyClick(fixture.view(), Qt::Key_R);
    QCOMPARE(controller->getActiveToolId(), QStringLiteral("probe"));
    QCOMPARE(windowShortcutCount, 1);

    QTest::keyClick(fixture.view(), Qt::Key_V);
    QCOMPARE(controller->getActiveToolId(), QStringLiteral("select"));
}

void AppToolContentCanvasTest::canvasShortcutRoutesApplicationCommand()
{
    // pe1_skeleton_notes.md 8.2, 11 (R3-1): Ctrl+A (the Canvas-scope shortcut of edit.selectAll) never reaches the tool
    // canvas as a shortcut - the view turns the key press into the routed command, which the entry answers (from the
    // event loop)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    QCOMPARE(fixture.view()->getCanvasCommand(std::make_unique<QKeyEvent>(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier).get()), pdf::PDFAppCommands::selectAll());

    QTest::keyClick(fixture.view(), Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(CanvasTestRecorder::entryCommandCount, 0);
    QCoreApplication::processEvents();
    QCOMPARE(CanvasTestRecorder::entryCommandCount, 1);

    // In Preview the entry is disabled (the default isEnabled): nothing
    fixture.context->setPreviewActive(true);
    QTest::keyClick(fixture.view(), Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(CanvasTestRecorder::entryCommandCount, 1);
}

void AppToolContentCanvasTest::activeToolAnswersApplicationCommandFirst()
{
    // pe1_skeleton_notes.md 11 (R3-1): the active tool is asked before the action entry of a routed command
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentToolController* controller = fixture.context->getToolController();
    QVERIFY(controller->activateTool(QStringLiteral("probe")));
    QCOMPARE(fixture.context->getApplicationCommandState(pdf::PDFAppCommands::selectAll()), pdf::PDFAppToolCommandState::Enabled);

    QVERIFY(fixture.context->handleApplicationCommand(pdf::PDFAppCommands::selectAll()));
    QCoreApplication::processEvents();
    QCOMPARE(CanvasTestRecorder::toolCommandCount, 1);
    QCOMPARE(CanvasTestRecorder::entryCommandCount, 0);

    // Select does not answer it: the entry does
    controller->activateSelectTool();
    QVERIFY(fixture.context->handleApplicationCommand(pdf::PDFAppCommands::selectAll()));
    QCoreApplication::processEvents();
    QCOMPARE(CanvasTestRecorder::toolCommandCount, 1);
    QCOMPARE(CanvasTestRecorder::entryCommandCount, 1);
}

void AppToolContentCanvasTest::dropFromOutside()
{
    // zadani_gui.md 4.16: a drop from outside goes to the first drop entry, which can take it, at the point of the page
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    QWidget* viewport = fixture.view()->viewport();
    const QPoint position = fixture.pixel(QPointF(120, 80));

    QMimeData image;
    image.setImageData(QImage(2, 2, QImage::Format_RGB32));
    QDragEnterEvent refused(position, Qt::CopyAction, &image, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &refused);
    QVERIFY(!refused.isAccepted());

    QMimeData text;
    text.setText(QStringLiteral("Dropped"));
    QDragEnterEvent enter(position, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(position), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &drop);
    QCOMPARE(CanvasTestRecorder::dropCount, 1);
    QVERIFY(CanvasTestHelper::isSame(CanvasTestRecorder::dropPoint, fixture.point(position)));
}

void AppToolContentCanvasTest::previewWithoutGuiRenders()
{
    // Item 6 of FX2: the worker holds the shared document of the context (getDocumentPointer), so Preview renders also
    // without a GUI of the tool (it failed before: the document came from the GUI only); the items are hidden while
    // Preview is on
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentPreview* preview = fixture.context->getPreview();
    QSignalSpy failed(preview, &pdf::PDFAppEditContentPreview::failed);
    QSignalSpy imageChanged(preview, &pdf::PDFAppEditContentPreview::imageChanged);
    fixture.context->setPreviewActive(true);
    QVERIFY(imageChanged.wait(CanvasTestHelper::timeout(20000)));
    QCOMPARE(failed.count(), 0);
    QVERIFY(!preview->getImage().isNull());
    QVERIFY(!fixture.context->isEditingAllowed());
    QVERIFY(fixture.scene()->getNodesAt(QPointF(20, 20), 1.0, false).empty());

    // A (1 0 0 rg, 10 10 50 50) is red in the picture (the generic color management without a GUI: exactly)
    const pdf::PDFAppEditContentPreviewRequest request = preview->getImageRequest();
    const QPoint pixel = pdf::PDFAppEditContentPreview::getPageToImageMatrix(request, pdf::PageRotation::None).map(QPointF(20, 20)).toPoint();
    QVERIFY(preview->getImage().rect().contains(pixel));
    const QColor red = preview->getImage().pixelColor(pixel);
    QVERIFY2(red.red() > 200 && red.green() < 60 && red.blue() < 60, qPrintable(red.name()));

    fixture.context->setPreviewActive(false);
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(20, 20), 1.0, false).size(), size_t(1));
}

void AppToolContentCanvasTest::previewOfMissingPageFails()
{
    // What still fails with the shared document (item 6): a request of a page the document does not have - the worker
    // cannot write it, the preview reports a text and keeps no picture
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentPreview* preview = fixture.context->getPreview();
    fixture.context->setPreviewActive(true);
    QSignalSpy failed(preview, &pdf::PDFAppEditContentPreview::failed);

    pdf::PDFAppEditContentPreviewRequest request;
    request.pageIndex = 7;
    request.imageSize = QSize(100, 100);
    request.pageRectangle = QRectF(0, 0, 200, 200);
    preview->render(request);
    QVERIFY(failed.wait(CanvasTestHelper::timeout(20000)));
    QVERIFY(!preview->isRendering());
    QVERIFY(!preview->getErrorText().isEmpty());
    QVERIFY(!(preview->getImageRequest() == request));
    fixture.context->setPreviewActive(false);
}

void AppToolContentCanvasTest::previewRendersThePage()
{
    // zadani_gui.md 4.15, KE.11: the exact rendering of the page of the snapshot for the visible paper; the paper color
    // of the card Preview renders again. The real tool (the developer switch) over a written document.
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError());
    QVERIFY(pdftest::AppToolShellFixture::waitFor([]() { return pdf::PDFAppToolGUIContent::isContentToolRegistered(); }));

    const QString path = fixture.path(QStringLiteral("canvas-preview.pdf"));
    const pdf::PDFDocumentPointer document = CanvasTestHelper::createDocument(CanvasTestHelper::basicContent());
    QVERIFY(bool(pdf::PDFDocumentWriter(nullptr).write(path, document.data(), false)));
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    QVERIFY(tab);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QStringLiteral("edit.content"));
    QVERIFY(shell);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui);
    QPointer<Context> context = gui->getEditContext();
    QVERIFY(context);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&context]() { return !context || context->getContainer(); }));
    QVERIFY(context && context->getView());

    // The image E is decoded in a worker at its first paint (the document of the tool holds it): its picture comes
    pdf::PDFAppEditContentImageItem* imageItem = nullptr;
    for (QGraphicsItem* item : context->getScene()->items())
    {
        imageItem = imageItem ? imageItem : dynamic_cast<pdf::PDFAppEditContentImageItem*>(item);
    }
    QVERIFY(imageItem);
    context->getView()->viewport()->repaint();
    QTRY_VERIFY_WITH_TIMEOUT(imageItem->hasPicture(), CanvasTestHelper::timeout(20000));
    QCOMPARE(context->getScene()->getDecodingCount(), 0);

    pdf::PDFAppEditContentPreview* preview = context->getPreview();
    QSignalSpy imageChanged(preview, &pdf::PDFAppEditContentPreview::imageChanged);
    context->setPreviewActive(true);
    QVERIFY(imageChanged.wait(CanvasTestHelper::timeout(20000)));
    QVERIFY(!preview->getImage().isNull());

    // A point of A alone (20, 20) is red (1 0 0 rg), a point of C (50, 50; over A) green, a point of the paper without
    // content (190, 190) white
    const pdf::PDFAppEditContentPreviewRequest request = preview->getImageRequest();
    QCOMPARE(request.pageIndex, pdf::PDFInteger(0));
    const QTransform pageToImage = pdf::PDFAppEditContentPreview::getPageToImageMatrix(request, pdf::PageRotation::None);
    const QImage& image = preview->getImage();
    const auto colorAt = [&image, &pageToImage](const QPointF& point)
    {
        const QPoint pixel = pageToImage.map(point).toPoint();
        return image.rect().contains(pixel) ? image.pixelColor(pixel) : QColor();
    };
    QVERIFY(request.pageRectangle.contains(QPointF(20, 20)));
    const QColor red = colorAt(QPointF(20, 20));
    // (through the color management of the application: the red and the green of DeviceRGB, not exactly 255)
    QVERIFY2(red.red() > 150 && red.red() > red.green() + 100 && red.red() > red.blue() + 100, qPrintable(red.name()));
    const QColor green = colorAt(QPointF(50, 50));
    QVERIFY2(green.green() > 150 && green.green() > green.red() + 60 && green.green() > green.blue() + 60, qPrintable(green.name()));
    // R6-23: the tool shows the whole page (fitted), so the paper point is always in the picture
    QVERIFY(request.pageRectangle.contains(QPointF(190, 190)));
    QCOMPARE(colorAt(QPointF(190, 190)), QColor(Qt::white));

    // The paper color of the card: a new picture
    imageChanged.clear();
    context->getOptions()->setValue(QString::fromLatin1(pdf::PDFAppEditContentOptions::PREVIEW_PAPER_COLOR), QStringLiteral("#00ff00"));
    QVERIFY(imageChanged.wait(CanvasTestHelper::timeout(20000)));
    QCOMPARE(preview->getImageRequest().paperColor, QColor(0, 255, 0));
    if (preview->getImageRequest().pageRectangle.contains(QPointF(190, 190)))
    {
        const QTransform greenMatrix = pdf::PDFAppEditContentPreview::getPageToImageMatrix(preview->getImageRequest(), pdf::PageRotation::None);
        const QColor paper = preview->getImage().pixelColor(greenMatrix.map(QPointF(190, 190)).toPoint());
        QCOMPARE(paper, QColor(0, 255, 0));
    }

    // Switched off: the picture goes
    context->setPreviewActive(false);
    QVERIFY(preview->getImage().isNull());
    QVERIFY(!preview->isRendering());
}

// ---------------------------------------------------------------- FX2: view, tools, Select, Preview

/// A widget of a fixed size over the canvas (a floating panel of the test, a notice)
class CanvasOverlayWidget : public QWidget
{
public:
    CanvasOverlayWidget(QWidget* parent, QSize size) : QWidget(parent), m_size(size) { }
    virtual QSize sizeHint() const override { return m_size; }

private:
    QSize m_size;
};

/// A floating panel of the test (the place of the panel Find and Replace): 200 x 150, starts hidden
class CanvasFloatingPanelEntry : public pdf::PDFAppEditContentCanvasWidgetEntry
{
public:
    static constexpr int HEIGHT = 150;

    virtual QString getId() const override { return QStringLiteral("test.floating"); }
    virtual Place getPlace() const override { return Place::FloatingPanel; }

    virtual QWidget* createWidget(Context*, pdf::PDFAppEditContentCanvas* canvas) const override
    {
        QWidget* widget = new CanvasOverlayWidget(canvas, QSize(200, HEIGHT));
        widget->hide();
        return widget;
    }

    static void addParts(Registry* registry)
    {
        registry->addCanvasWidget(std::make_unique<CanvasFloatingPanelEntry>());
    }
};

/// The real entries of the keyboard (Preview with the canvas key P, the routed view.fitPage with the shortcut P)
class CanvasKeyboardParts
{
public:
    static void addParts(Registry* registry)
    {
        pdf::PDFAppEditContentKeyboard::registerParts(registry);
    }
};

void AppToolContentCanvasTest::keyPTogglesPreviewOnTheCanvas()
{
    // R6-6 (zadani_gui.md 4.15, 4.20, KE.11): P on the canvas of the tool switches Preview on and off; the Canvas-scope
    // shortcut P of view.fitPage, which an entry answers, does not take it (the zoom stays)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()), &CanvasKeyboardParts::addParts);
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentView* view = fixture.view();
    QCOMPARE(view->getCanvasCommand(std::make_unique<QKeyEvent>(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier).get()), pdf::PDFAppCommands::fitPage());
    QCOMPARE(fixture.context->getApplicationCommandState(pdf::PDFAppCommands::fitPage()), pdf::PDFAppToolCommandState::Enabled);
    view->setZoom(2.0);

    QTest::keyClick(view, Qt::Key_P);
    QCoreApplication::processEvents();
    QVERIFY(fixture.context->isPreviewActive());
    QCOMPARE(view->getZoom(), 2.0);

    // P also ends Preview (the editing is paused, the action is not)
    QTest::keyClick(view, Qt::Key_P);
    QCoreApplication::processEvents();
    QVERIFY(!fixture.context->isPreviewActive());
    QCOMPARE(view->getZoom(), 2.0);
}

void AppToolContentCanvasTest::previewZoomWithoutScrollRendersAgain()
{
    // R6-18 (zadani_gui.md 4.15 "a change of the zoom renders again"): a change of the zoom, which moves no scroll bar,
    // renders a new picture of Preview for the new visible part. The real tool (its document holds the picture).
    pdftest::AppToolShellFixture fixture;
    QVERIFY(fixture.window);
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError());
    QVERIFY(pdftest::AppToolShellFixture::waitFor([]() { return pdf::PDFAppToolGUIContent::isContentToolRegistered(); }));

    const QString path = fixture.path(QStringLiteral("canvas-preview-zoom.pdf"));
    const pdf::PDFDocumentPointer document = CanvasTestHelper::createDocument(CanvasTestHelper::basicContent());
    QVERIFY(bool(pdf::PDFDocumentWriter(nullptr).write(path, document.data(), false)));
    pdf::PDFAppDocumentTab* tab = fixture.open(path);
    QVERIFY(tab);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QStringLiteral("edit.content"));
    QVERIFY(shell);
    pdf::PDFAppToolGUIContent* gui = qobject_cast<pdf::PDFAppToolGUIContent*>(shell->getGUI());
    QVERIFY(gui);
    QPointer<Context> context = gui->getEditContext();
    QVERIFY(context);
    QVERIFY(pdftest::AppToolShellFixture::waitFor([&context]() { return !context || context->getContainer(); }));
    QVERIFY(context && context->getView());

    pdf::PDFAppEditContentView* view = context->getView();
    pdf::PDFAppEditContentPreview* preview = context->getPreview();
    QSignalSpy imageChanged(preview, &pdf::PDFAppEditContentPreview::imageChanged);
    context->setPreviewActive(true);
    QVERIFY(imageChanged.wait(CanvasTestHelper::timeout(20000)));
    QVERIFY(!preview->getImage().isNull());

    // The view centered on the origin of the page (its lower left corner): a scroll value is the position of the left
    // / top edge of the viewport in the pixels of the scene, here minus half of the viewport at any zoom - a zoom around
    // this center moves no scroll bar (the visible part of the paper or, when all of it is visible, its pixels change)
    view->centerOn(QPointF(0, 0));

    // Settled: the picture shown is the one of the view as it is, nothing renders
    QTRY_VERIFY_WITH_TIMEOUT(!preview->isRendering() && preview->getImageRequest() == view->getPreviewRequest(), CanvasTestHelper::timeout(20000));
    QTest::qWait(3 * pdf::PDFAppEditContentPreview::RENDER_DELAY);
    QVERIFY(!preview->isRendering());
    QVERIFY(preview->getImageRequest() == view->getPreviewRequest());
    const pdf::PDFAppEditContentPreviewRequest before = preview->getImageRequest();

    // A step of the zoom: no scroll value changes
    const int horizontal = view->horizontalScrollBar()->value();
    const int vertical = view->verticalScrollBar()->value();
    imageChanged.clear();
    view->zoomIn();
    QCOMPARE(view->horizontalScrollBar()->value(), horizontal);
    QCOMPARE(view->verticalScrollBar()->value(), vertical);
    QVERIFY(imageChanged.wait(CanvasTestHelper::timeout(20000)));
    QVERIFY(preview->getImageRequest() != before);
    QVERIFY(preview->getImageRequest() == view->getPreviewRequest());
    if (before.pageRectangle == QRectF(0, 0, 200, 200))
    {
        // The whole paper was visible: the picture has the pixels of the new zoom
        QVERIFY(preview->getImageRequest().imageSize.width() > before.imageSize.width());
    }
}

void AppToolContentCanvasTest::rubberBandPreviewRectangle()
{
    // R6-11: the tool states the rectangle it paints - the rubber band with its pen (1 px, antialiased) during the
    // drag; nothing before the drag, on an object, and after the release
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    auto* tool = static_cast<SelectTool*>(fixture.context->getToolController()->getActiveTool());
    const qreal pixelSize = fixture.scene()->getPixelSize();
    QVERIFY(tool->getPreviewRectangle(pixelSize).isNull());

    const QPoint from = fixture.pixel(QPointF(5, 5));
    const QPoint to = fixture.pixel(QPointF(75, 75));
    fixture.drag(from, to, Qt::NoModifier, false);
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::RubberBand);
    const QRectF band = QRectF(fixture.point(from), fixture.point(to)).normalized();
    const QRectF rectangle = tool->getPreviewRectangle(pixelSize);
    QVERIFY(rectangle.contains(band.adjusted(-pixelSize, -pixelSize, pixelSize, pixelSize)));
    QVERIFY(band.adjusted(-3 * pixelSize, -3 * pixelSize, 3 * pixelSize, 3 * pixelSize).contains(rectangle));

    fixture.sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QVERIFY(tool->getPreviewRectangle(pixelSize).isNull());

    // A drag of an object paints no preview of the tool (the items and the frame move)
    fixture.drag(fixture.pixel(QPointF(20, 20)), fixture.pixel(QPointF(20, 20)) + QPoint(30, 0), Qt::NoModifier, false);
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::Move);
    QVERIFY(tool->getPreviewRectangle(pixelSize).isNull());
    fixture.sendMouse(QEvent::MouseButtonRelease, fixture.pixel(QPointF(20, 20)) + QPoint(30, 0), Qt::LeftButton, Qt::NoButton);
}

void AppToolContentCanvasTest::selectionBoxesComputedOncePerChange()
{
    // R6-13 (zadani_gui.md 4.24): the boxes of the selected nodes are computed once per change of the selection or of
    // the model - a drag of N moves, a zoom step and a change of the focus compute none; above
    // PDFAppEditContentScene::GESTURE_ITEM_LIMIT nodes only the common frame is painted
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    pdf::PDFAppEditContentSelectionItem* item = fixture.scene()->getSelectionItem();
    fixture.selection()->setNodes({ objects[2], objects[0] });
    QCoreApplication::processEvents();
    QCOMPARE(item->getFrameCount(), 3);
    const int computed = item->getBoxComputationCount();
    QVERIFY(computed >= 1);

    // A drag of A (with C) by 5 moves: the frames follow by the matrix of the gesture, no box is computed
    const QPoint from = fixture.pixel(QPointF(20, 20));
    const QPoint to = from + QPoint(40, -30);
    const QPointF delta = fixture.point(to) - fixture.point(from);
    fixture.drag(from, to, Qt::NoModifier, false);
    QVERIFY(fixture.scene()->isGestureActive());
    QCOMPARE(item->getBoxComputationCount(), computed);
    QVERIFY(CanvasTestHelper::isSame(item->getHandleBox(), QRectF(10, 10, 60, 60).translated(delta)));

    // The release executes the command: the model changed, the boxes once again
    fixture.sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));
    QCOMPARE(item->getBoxComputationCount(), computed + 1);
    QVERIFY(CanvasTestHelper::isSame(item->getHandleBox(), QRectF(10, 10, 60, 60).translated(delta)));

    // A zoom step and the focus: none
    fixture.view()->zoomIn();
    fixture.button->setFocus();
    fixture.view()->setFocus();
    QCoreApplication::processEvents();
    QCOMPARE(item->getBoxComputationCount(), computed + 1);

    // 1000 selected objects: one frame (the common one), the handles on the union
    QByteArray content;
    for (int i = 0; i < 1000; ++i)
    {
        content.append(QByteArray::number(10 + (i % 40) * 4) + " " + QByteArray::number(10 + (i / 40) * 4) + " 2 2 re f\n");
    }
    CanvasTestFixture many(CanvasTestHelper::createDocument(content));
    QVERIFY(many.showPage(0));
    const std::vector<NodeReference> all = CanvasTestHelper::getObjects(many.context);
    QCOMPARE(all.size(), size_t(1000));
    many.selection()->setNodes(all);
    QCoreApplication::processEvents();
    pdf::PDFAppEditContentSelectionItem* manyItem = many.scene()->getSelectionItem();
    QCOMPARE(manyItem->getFrameCount(), 1);
    QVERIFY(CanvasTestHelper::isSame(manyItem->getHandleBox(), QRectF(10, 10, 39 * 4 + 2, 24 * 4 + 2)));
}

void AppToolContentCanvasTest::gestureEndsWhenItsNodesChange()
{
    // R6-21: a command (or an undo) changes the dragged node during the drag - the gesture ends without a command, the
    // release executes nothing; a change of another node leaves the gesture alone
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    auto* tool = static_cast<SelectTool*>(fixture.context->getToolController()->getActiveTool());
    const pdf::PDFContentUndoStack* undoStack = fixture.context->getSession()->getUndoStack();

    const QPoint from = fixture.pixel(QPointF(20, 20));
    const QPoint to = from + QPoint(40, 0);
    fixture.drag(from, to, Qt::NoModifier, false);
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::Move);
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createTransform({ objects[0] }, QTransform::fromTranslate(5, 0))));
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::None);
    QVERIFY(!fixture.scene()->isGestureActive());
    fixture.sendMouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QCOMPARE(undoStack->getCount(), size_t(1));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(15, 10, 50, 50)));

    // An undo under the drag (Ctrl+Z fires with the button held): the same
    fixture.drag(fixture.pixel(QPointF(20, 20)), fixture.pixel(QPointF(20, 20)) + QPoint(40, 0), Qt::NoModifier, false);
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::Move);
    QVERIFY(fixture.context->getSession()->undo().has_value());
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::None);
    fixture.sendMouse(QEvent::MouseButtonRelease, fixture.pixel(QPointF(20, 20)) + QPoint(40, 0), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QVERIFY(undoStack->canRedo());
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50, 50)));

    // A command of another node (B) during a drag of A: the drag goes on and moves A at the release
    const QPoint from2 = fixture.pixel(QPointF(20, 20));
    const QPoint to2 = from2 + QPoint(40, 0);
    const QPointF delta = fixture.point(to2) - fixture.point(from2);
    fixture.drag(from2, to2, Qt::NoModifier, false);
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createTransform({ objects[1] }, QTransform::fromTranslate(0, 5))));
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::Move);
    fixture.sendMouse(QEvent::MouseButtonRelease, to2, Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QCOMPARE(undoStack->getCount(), size_t(2));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50, 50).translated(delta)));
}

void AppToolContentCanvasTest::gesturesLeaveLockedNodes()
{
    // R7-4 (QE.8, RE-16): a gesture of the canvas never changes a locked object - one selected directly locked (C), one
    // inside of a locked group (the rectangle of D); the unlocked one (A) is moved by the drag and by the arrows
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const pdf::PDFContentContainerPointer container = fixture.context->getContainer();
    NodeReference rectangle;
    for (const pdf::PDFContentNodePointer& child : container->getNode(objects[3].node)->getChildren())
    {
        if (child->getKind() == pdf::PDFContentNodeKind::Path && !child->getData().paint.isClippingPath())
        {
            rectangle = NodeReference{ container->getKey(), child->getId() };
        }
    }
    QVERIFY(rectangle.isValid());
    fixture.context->getSession()->setNodeLocked(objects[3], true);
    fixture.context->getSession()->setNodeLocked(objects[2], true);
    fixture.selection()->setNodes({ rectangle, objects[2], objects[0] });
    QCoreApplication::processEvents();
    QCOMPARE(fixture.selection()->getCount(), 3);

    const QPoint from = fixture.pixel(QPointF(20, 20));
    const QPoint to = from + QPoint(30, 0);
    const QPointF delta = fixture.point(to) - fixture.point(from);
    fixture.drag(from, to);
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, 50, 50).translated(delta)));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[2]), QRectF(30, 30, 40, 40)));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, rectangle), QRectF(110, 10, 60, 60)));

    fixture.selection()->setNodes({ rectangle, objects[2], objects[0] });
    // (a Transform of the same nodes merges with the Transform of the drag: one undo step)
    QTest::keyClick(fixture.view(), Qt::Key_Right);
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(11, 10, 50, 50).translated(delta)));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[2]), QRectF(30, 30, 40, 40)));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, rectangle), QRectF(110, 10, 60, 60)));
}

void AppToolContentCanvasTest::selectionAnnouncedOncePerTurn()
{
    // zadani_gui.md 4.23 (item 4 of FX2): the view announces the selection as the card Selection titles it - the kind of
    // one object, the count of more - once per change, from the event loop; five changes in one turn are one announcement
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    pdf::PDFAppEditContentView* view = fixture.view();
    pdf::PDFAppEditContentSelection* selection = fixture.selection();
    QCoreApplication::processEvents();
    const int count = view->getAnnouncementCount();

    selection->setNode(objects[0]);
    QCOMPARE(view->getAnnouncementCount(), count);
    QCoreApplication::processEvents();
    QCOMPARE(view->getAnnouncementCount(), count + 1);
    QCOMPARE(view->getAnnouncementText(), QStringLiteral("Path"));

    selection->setNode(objects[4]);
    selection->addNode(objects[1]);
    selection->addNode(objects[2]);
    selection->removeNode(objects[4]);
    selection->addNode(objects[0]);
    QCOMPARE(view->getAnnouncementCount(), count + 1);
    QCoreApplication::processEvents();
    QCOMPARE(view->getAnnouncementCount(), count + 2);
    QVERIFY2(view->getAnnouncementText().startsWith(QStringLiteral("3 object")), qPrintable(view->getAnnouncementText()));

    // D (q 120 20 20 20 re W n ... Q): a group, whose first object is a clipping path
    selection->setNode(objects[3]);
    QCoreApplication::processEvents();
    QCOMPARE(view->getAnnouncementText(), QStringLiteral("Clipping group"));
    selection->clear();
    QCoreApplication::processEvents();
    QCOMPARE(view->getAnnouncementCount(), count + 4);
    QVERIFY(!view->getAnnouncementText().isEmpty());
}

void AppToolContentCanvasTest::scrollRangeCoversOverlays()
{
    // zadani_gui.md 4.15 (item 5 of FX2): the scroll range grows by the overlays over the view - a floating panel of the
    // height h at the top (12 px from the edge) lets the top edge of the paper go h + 24 px lower, a notice of the height
    // h at the bottom lets its bottom edge go h + 24 px higher; without overlays the edge reaches the middle of the view
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()), &CanvasFloatingPanelEntry::addParts);
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentView* view = fixture.view();
    view->setZoom(8.0 * 72.0 / view->logicalDpiX());     // the paper (200 pt) is 1600 px: larger than the view
    QCoreApplication::processEvents();
    const QRectF paper = fixture.scene()->getPaperRectangle();
    QCOMPARE(paper, QRectF(0, 0, 200, 200));

    // The top edge of the paper (y = 200 of the page) with the vertical scroll bar at its minimum, the bottom edge (y = 0)
    // at its maximum: their distances from the top / bottom edge of the viewport
    const auto topGap = [view, &paper]()
    {
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->minimum());
        QCoreApplication::processEvents();
        return view->mapFromScene(QPointF(paper.center().x(), paper.bottom())).y();
    };
    const auto bottomGap = [view, &paper]()
    {
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        return view->viewport()->height() - view->mapFromScene(QPointF(paper.center().x(), paper.top())).y();
    };

    const int half = view->viewport()->height() / 2;
    const int top0 = topGap();
    const int bottom0 = bottomGap();
    QVERIFY2(top0 >= half - 2, qPrintable(QStringLiteral("%1 %2").arg(top0).arg(half)));
    QVERIFY2(bottom0 >= half - 2, qPrintable(QStringLiteral("%1 %2").arg(bottom0).arg(half)));

    // The floating panel shows itself
    QWidget* panel = fixture.canvas->getEntryWidget(QStringLiteral("test.floating"));
    QVERIFY(panel);
    panel->show();
    QCoreApplication::processEvents();
    const int margin = pdf::PDFAppEditContentCanvas::OVERLAY_MARGIN;
    const int top1 = topGap();
    QVERIFY2(top1 - top0 >= CanvasFloatingPanelEntry::HEIGHT + 2 * margin - 2, qPrintable(QStringLiteral("%1 %2").arg(top0).arg(top1)));
    QVERIFY(top1 >= CanvasFloatingPanelEntry::HEIGHT + margin);

    // The notice at the bottom edge
    constexpr int noticeHeight = 100;
    fixture.canvas->setNoticeWidget(new CanvasOverlayWidget(nullptr, QSize(300, noticeHeight)));
    QCoreApplication::processEvents();
    const int bottom1 = bottomGap();
    QVERIFY2(bottom1 - bottom0 >= noticeHeight + 2 * margin - 2, qPrintable(QStringLiteral("%1 %2").arg(bottom0).arg(bottom1)));

    // Hidden again: the range shrinks back
    panel->hide();
    QCoreApplication::processEvents();
    QVERIFY(qAbs(topGap() - top0) <= 2);
}

// ---------------------------------------------------------------- FX2: the scene and the items

/// Helpers of the tests of the scene and the items (fix package FX2)
class CanvasSceneTestHelper
{
public:
    CanvasSceneTestHelper() = delete;

    using D = pdftest::ContentTestDocuments;
    using FormItem = pdf::PDFAppEditContentFormItem;

    static pdf::PDFDocumentPointer createDocument(const QByteArray& content, const D::ResourcesFactory& resources)
    {
        pdf::PDFDocument document = D::createDocument(content, resources);
        return pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document)));
    }

    /// A stream object with the entries (and /Length), not added to the document
    static pdf::PDFObject createStream(D::DictionaryEntries entries, QByteArray content)
    {
        pdf::PDFDictionaryBuilder dictionary;
        for (auto& entry : entries)
        {
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString(entry.first), std::move(entry.second));
        }
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(content.size()));
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(content)));
    }

    /// A form [0 0 size size] with the /XObject resources and the content
    static pdf::PDFObject createForm(D::DictionaryEntries xobjects, QByteArray content, pdf::PDFInteger size = 10)
    {
        return createStream({ { "Type", pdf::PDFObject::createName("XObject") }, { "Subtype", pdf::PDFObject::createName("Form") },
                              { "BBox", D::createIntegerArrayObject({ 0, 0, size, size }) },
                              { "Resources", D::createDictionaryObject({ { "XObject", D::createDictionaryObject(std::move(xobjects)) } }) } },
                            std::move(content));
    }

    static pdf::PDFObject addForm(pdf::PDFDocumentBuilder* builder, D::DictionaryEntries xobjects, QByteArray content, pdf::PDFInteger size = 10)
    {
        return D::addObject(builder, createForm(std::move(xobjects), std::move(content), size));
    }

    /// An image XObject width x height of one DeviceRGB color
    static pdf::PDFObject addImage(pdf::PDFDocumentBuilder* builder, int width, int height, const QColor& color)
    {
        QByteArray data;
        for (int i = 0; i < width * height; ++i)
        {
            data.append(char(color.red()));
            data.append(char(color.green()));
            data.append(char(color.blue()));
        }
        return D::addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") }, { "Subtype", pdf::PDFObject::createName("Image") },
                                             { "Width", pdf::PDFObject::createInteger(width) }, { "Height", pdf::PDFObject::createInteger(height) },
                                             { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") }, { "BitsPerComponent", pdf::PDFObject::createInteger(8) } },
                                  data);
    }

    /// An axial shading of one color (DeviceRGB) along x 0 ... 100
    static pdf::PDFObject createAxialShading(const QColor& color)
    {
        const std::vector<pdf::PDFReal> components{ color.redF(), color.greenF(), color.blueF() };
        const pdf::PDFObject function = D::createDictionaryObject({ { "FunctionType", pdf::PDFObject::createInteger(2) }, { "Domain", D::createNumberArrayObject({ 0, 1 }) },
                                                                    { "C0", D::createNumberArrayObject(components) }, { "C1", D::createNumberArrayObject(components) },
                                                                    { "N", pdf::PDFObject::createInteger(1) } });
        return D::createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(2) }, { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                           { "Coords", D::createNumberArrayObject({ 0, 0, 100, 0 }) }, { "Function", function },
                                           { "Extend", D::createArrayObject({ pdf::PDFObject::createBool(true), pdf::PDFObject::createBool(true) }) } });
    }

    /// The scene drawn into a picture of the page [0 0 200 200]: the pixel (x, y) shows the point (x, y) of the page
    static QImage render(pdf::PDFAppEditContentScene* scene)
    {
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        scene->render(&painter, QRectF(0, 0, 200, 200), QRectF(0, 0, 200, 200));
        painter.end();
        return image;
    }

    /// The scene drawn after every job of a worker ended (the first drawing asks for the pictures)
    static QImage renderFinished(pdf::PDFAppEditContentScene* scene)
    {
        render(scene);
        if (!QTest::qWaitFor([scene]() { return scene->getDecodingCount() == 0; }, CanvasTestHelper::timeout(20000)))
        {
            qWarning() << "the jobs of the scene did not end";
        }
        QCoreApplication::processEvents();
        return render(scene);
    }

    /// The nodes of a kind of the page container, depth first in the order of the content
    static std::vector<NodeReference> findNodes(const Context* context, pdf::PDFContentNodeKind kind)
    {
        std::vector<NodeReference> result;
        const pdf::PDFContentContainerPointer container = context->getContainer();
        std::vector<const pdf::PDFContentNode*> stack{ container->getRoot().get() };
        while (!stack.empty())
        {
            const pdf::PDFContentNode* node = stack.back();
            stack.pop_back();
            if (node->getKind() == kind)
            {
                result.push_back(NodeReference{ container->getKey(), node->getId() });
            }
            for (auto it = node->getChildren().crbegin(); it != node->getChildren().crend(); ++it)
            {
                stack.push_back(it->get());
            }
        }
        return result;
    }

    /// The path node, whose box of the builder is \p box
    static NodeReference findPath(const Context* context, const QRectF& box)
    {
        const pdf::PDFContentContainerPointer container = context->getContainer();
        for (const NodeReference& node : findNodes(context, pdf::PDFContentNodeKind::Path))
        {
            const QRectF nodeBox = container->getNode(node.node)->getBoundingBox();
            if (qAbs(nodeBox.left() - box.left()) < 1e-6 && qAbs(nodeBox.top() - box.top()) < 1e-6 && qAbs(nodeBox.width() - box.width()) < 1e-6 && qAbs(nodeBox.height() - box.height()) < 1e-6)
            {
                return node;
            }
        }
        return NodeReference();
    }

    static bool isColor(const QColor& actual, const QColor& expected, int tolerance = 16)
    {
        const bool isSame = qAbs(actual.red() - expected.red()) <= tolerance && qAbs(actual.green() - expected.green()) <= tolerance && qAbs(actual.blue() - expected.blue()) <= tolerance;
        if (!isSame)
        {
            qWarning() << "color" << actual.name() << "expected" << expected.name();
        }
        return isSame;
    }
};

void AppToolContentCanvasTest::formsSharedAndNestedUnderBudget()
{
    // R6-1: Fm1 is used twice - both uses draw it; each use has 4 x 4 x 4 nested uses (k uses over d levels); the
    // items of all forms are built breadth first under one budget of the page; Fm5 draws itself - the inner use is not
    // built (the cycle by the path of the uses)
    using H = CanvasSceneTestHelper;
    const QByteArray content = QByteArrayLiteral("q 1 0 0 1 0 0 cm /Fm1 Do Q q 1 0 0 1 100 0 cm /Fm1 Do Q /Fm5 Do\n");
    CanvasTestFixture fixture(H::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
    {
        const pdf::PDFObject form4 = H::addForm(builder, { }, QByteArrayLiteral("0 0 5 5 re f"));
        const pdf::PDFObject form3 = H::addForm(builder, { { "Fm4", form4 } }, QByteArrayLiteral("/Fm4 Do /Fm4 Do /Fm4 Do /Fm4 Do"));
        const pdf::PDFObject form2 = H::addForm(builder, { { "Fm3", form3 } }, QByteArrayLiteral("/Fm3 Do /Fm3 Do /Fm3 Do /Fm3 Do"));
        const pdf::PDFObject form1 = H::addForm(builder, { { "Fm2", form2 } }, QByteArrayLiteral("/Fm2 Do /Fm2 Do /Fm2 Do /Fm2 Do"));
        const pdf::PDFObjectReference form5 = builder->addObject(pdf::PDFObject());
        builder->setObject(form5, H::createForm({ { "Fm5", pdf::PDFObject::createReference(form5) } }, QByteArrayLiteral("0 0 5 5 re f /Fm5 Do")));
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Fm1", form1 }, { "Fm5", pdf::PDFObject::createReference(form5) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();

    // Each use of Fm1: 4 + 16 + 64 uses and 64 paths = 148 items; Fm5: its path and its own use = 2 items
    QTRY_COMPARE_WITH_TIMEOUT(scene->getFormItemCount(), size_t(2 * 148 + 2), CanvasTestHelper::timeout(15000));
    const std::vector<NodeReference> uses = H::findNodes(fixture.context, pdf::PDFContentNodeKind::FormUse);
    QCOMPARE(uses.size(), size_t(3));
    for (size_t i = 0; i < 2; ++i)
    {
        H::FormItem* item = dynamic_cast<H::FormItem*>(scene->findItem(uses[i]));
        QVERIFY(item && item->getFormVersion() != 0);
        QCOMPARE(item->childItems().size(), qsizetype(4));
    }
    H::FormItem* selfUse = dynamic_cast<H::FormItem*>(scene->findItem(uses[2]));
    QVERIFY(selfUse && selfUse->getFormVersion() != 0);
    QCOMPARE(selfUse->childItems().size(), qsizetype(2));
    H::FormItem* inner = nullptr;
    for (QGraphicsItem* child : selfUse->childItems())
    {
        inner = inner ? inner : dynamic_cast<H::FormItem*>(child);
    }
    QVERIFY(inner && inner->getFormVersion() == 0 && inner->childItems().isEmpty());

    // A budget of 100 items: never more; the outer levels first (both uses of Fm1 and all 8 uses of Fm2 are built, some
    // uses of Fm3 are not)
    scene->setFormItemBudget(100);
    QVERIFY2(scene->getFormItemCount() <= size_t(100), qPrintable(QString::number(scene->getFormItemCount())));
    int builtSecondLevel = 0;
    int unbuiltThirdLevel = 0;
    for (size_t i = 0; i < 2; ++i)
    {
        H::FormItem* item = dynamic_cast<H::FormItem*>(scene->findItem(uses[i]));
        QVERIFY(item && item->getFormVersion() != 0);
        for (QGraphicsItem* child : item->childItems())
        {
            H::FormItem* second = dynamic_cast<H::FormItem*>(child);
            QVERIFY(second);
            builtSecondLevel += second->getFormVersion() != 0 ? 1 : 0;
            for (QGraphicsItem* grandChild : second->childItems())
            {
                H::FormItem* third = dynamic_cast<H::FormItem*>(grandChild);
                unbuiltThirdLevel += third && third->getFormVersion() == 0 ? 1 : 0;
            }
        }
    }
    QCOMPARE(builtSecondLevel, 8);
    QVERIFY(unbuiltThirdLevel > 0);
}

void AppToolContentCanvasTest::editorStateIsOneWalkPerTurn()
{
    // R6-2: Lock of many objects emits editorStateChanged once per node; the scene walks the page once, from the event
    // loop
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const int walks = scene->getWalkCount();
    for (const NodeReference& node : objects)
    {
        fixture.context->getSession()->setNodeLocked(node, true);
    }
    QCOMPARE(scene->getWalkCount(), walks);
    QCoreApplication::processEvents();
    QCOMPARE(scene->getWalkCount(), walks + 1);
    for (const NodeReference& node : objects)
    {
        QVERIFY(scene->findItem(node)->isLockedInEditor());
    }
}

void AppToolContentCanvasTest::clipChainsSharedAndKept()
{
    // R6-3, R6-12: the items of one clip scope share one chain, an inner clip holds the chain of the page clip; after a
    // command on another object the chains of the unchanged items are the same objects (no repaint, no new paths)
    using H = CanvasSceneTestHelper;
    const QByteArray content = QByteArrayLiteral("0 0 200 200 re W n\n"
                                                 "q 10 10 100 100 re W n 1 0 0 rg 0 0 50 50 re f 0 1 0 rg 20 20 50 50 re f Q\n"
                                                 "0 0 1 rg 150 150 20 20 re f\n");
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(content));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference red = H::findPath(fixture.context, QRectF(0, 0, 50, 50));
    const NodeReference green = H::findPath(fixture.context, QRectF(20, 20, 50, 50));
    const NodeReference blue = H::findPath(fixture.context, QRectF(150, 150, 20, 20));
    QVERIFY(red.isValid() && green.isValid() && blue.isValid());

    const pdf::PDFAppEditContentClipPathsPointer redChain = scene->findItem(red)->getClipPaths();
    const pdf::PDFAppEditContentClipPathsPointer blueChain = scene->findItem(blue)->getClipPaths();
    QVERIFY(redChain && blueChain);
    QVERIFY(scene->findItem(green)->getClipPaths() == redChain);
    QCOMPARE(redChain->getLength(), size_t(2));
    QVERIFY(redChain->getParent() == blueChain);
    QCOMPARE(blueChain->getLength(), size_t(1));

    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createTransform({ blue }, QTransform::fromTranslate(1, 0))));
    QVERIFY(scene->findItem(red)->getClipPaths() == redChain);
    QVERIFY(scene->findItem(green)->getClipPaths() == redChain);
    QVERIFY(scene->findItem(blue)->getClipPaths() == blueChain);
}

void AppToolContentCanvasTest::longClipChainIsLinear()
{
    // R6-3 (hostile input): 30 000 clips in one scope - each clip is O(1) (the chain of the path behind them holds the
    // chain of the last clip), the hit intersects a bounded number of paths exactly, the chain is released without a
    // recursion over its length (the end of the test)
    using H = CanvasSceneTestHelper;
    constexpr int clipCount = 30000;
    QByteArray content;
    for (int i = 0; i < clipCount; ++i)
    {
        content.append("0 0 200 200 re W n\n");
    }
    content.append("1 0 0 rg 10 10 50 50 re f\n");
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(content));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference path = H::findPath(fixture.context, QRectF(10, 10, 50, 50));
    QVERIFY(path.isValid());
    const pdf::PDFAppEditContentClipPathsPointer chain = scene->findItem(path)->getClipPaths();
    QVERIFY(chain);
    QCOMPARE(chain->getLength(), size_t(clipCount));

    const std::vector<NodeReference> paths = H::findNodes(fixture.context, pdf::PDFContentNodeKind::Path);
    QCOMPARE(paths.size(), size_t(clipCount + 1));
    QVERIFY(chain->getParent() == scene->findItem(paths[clipCount - 1])->getClipPaths());
    QCOMPARE(scene->getNodesAt(QPointF(20, 20), 1.0, false), std::vector<NodeReference>{ path });
    QVERIFY(scene->getNodesAt(QPointF(100, 100), 1.0, false).empty());
}

void AppToolContentCanvasTest::selectionSetsAreLogLinear()
{
    // R6-4, R7-3: the selection deduplicates and prunes by sets - 100 000 nodes (with duplicates) keep their order, the
    // key object is the last one; a delete prunes the removed nodes and keeps the key object
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentSelection* selection = fixture.selection();
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    const pdf::PDFContentContainerKey key = objects.front().container;

    std::vector<NodeReference> nodes;
    for (quint64 i = 1; i <= 100000; ++i)
    {
        nodes.push_back(NodeReference{ key, pdf::PDFContentNodeId{ 1000000 + i } });
        if (i % 10 == 0)
        {
            nodes.push_back(NodeReference{ key, pdf::PDFContentNodeId{ 1000000 + i - 5 } });
        }
    }
    selection->setNodes(nodes);
    QCOMPARE(selection->getCount(), 100000);
    QCOMPARE(selection->getNodes()[10].node.value, quint64(1000011));
    QCOMPARE(selection->getKeyNode().node.value, quint64(1100000));
    QVERIFY(selection->contains(NodeReference{ key, pdf::PDFContentNodeId{ 1050000 } }));
    QVERIFY(!selection->contains(NodeReference{ key, pdf::PDFContentNodeId{ 1200000 } }));

    selection->setNodes(objects);
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createDelete({ objects[1], objects[3] })));
    QCOMPARE(selection->getNodes(), (std::vector<NodeReference>{ objects[0], objects[2], objects[4] }));
    QCOMPARE(selection->getKeyNode(), objects[4]);
}

void AppToolContentCanvasTest::hitReturnsEachObjectOnce()
{
    // R6-4: two items of one group under the point give the group once
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(QByteArrayLiteral("q 1 0 0 rg 10 10 50 50 re f 20 20 10 10 re f Q\n")));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    QCOMPARE(objects.size(), size_t(1));
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(25, 25), 1.0, false), objects);
}

void AppToolContentCanvasTest::imagePicturesSharedPerObject()
{
    // R6-5, R6-14: three uses of one image XObject share one decoding (one job per image object and size step); an
    // inline image has its own; a move keeps the picture and decodes nothing
    using H = CanvasSceneTestHelper;
    const QByteArray content = QByteArrayLiteral("q 40 0 0 40 10 10 cm /Im1 Do Q q 40 0 0 40 60 10 cm /Im1 Do Q q 40 0 0 40 110 10 cm /Im1 Do Q "
                                                 "q 40 0 0 40 10 100 cm BI /W 1 /H 1 /BPC 8 /CS /G ID x EI Q\n");
    CanvasTestFixture fixture(H::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Im1", H::addImage(builder, 2, 2, Qt::red) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const std::vector<NodeReference> images = H::findNodes(fixture.context, pdf::PDFContentNodeKind::Image);
    QCOMPARE(images.size(), size_t(4));
    const auto hasPictures = [scene, &images]()
    {
        for (const NodeReference& image : images)
        {
            const pdf::PDFAppEditContentImageItem* item = dynamic_cast<const pdf::PDFAppEditContentImageItem*>(scene->findItem(image));
            if (!item || !item->hasPicture())
            {
                return false;
            }
        }
        return true;
    };
    fixture.view()->viewport()->repaint();
    QVERIFY(QTest::qWaitFor(hasPictures, CanvasTestHelper::timeout(20000)));
    QCOMPARE(scene->getStartedJobCount(), 2);
    QCOMPARE(scene->getDecodingCount(), 0);

    const std::vector<NodeReference> groups = CanvasTestHelper::getObjects(fixture.context);
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createTransform({ groups[0] }, QTransform::fromTranslate(5, 0))));
    QVERIFY(hasPictures());
    fixture.view()->viewport()->repaint();
    QCoreApplication::processEvents();
    QCOMPARE(scene->getStartedJobCount(), 2);
}

void AppToolContentCanvasTest::imagePicturesWithinBudget()
{
    // R6-5: the pictures of a scene stay within its budget of pixels; over it the placeholder (three images of 64 x 64
    // pixels, each decoded whole at the size step 4096, a budget of two of them)
    using H = CanvasSceneTestHelper;
    const QByteArray content = QByteArrayLiteral("q 40 0 0 40 10 10 cm /Im1 Do Q q 40 0 0 40 60 10 cm /Im2 Do Q q 40 0 0 40 110 10 cm /Im3 Do Q\n");
    CanvasTestFixture fixture(H::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Im1", H::addImage(builder, 64, 64, Qt::red) },
                                                                                                     { "Im2", H::addImage(builder, 64, 64, Qt::green) },
                                                                                                     { "Im3", H::addImage(builder, 64, 64, Qt::blue) } }));
        return resources;
    }));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    scene->setPictureBudget(2 * 64 * 64);
    QVERIFY(fixture.showPage(0));
    fixture.view()->viewport()->repaint();
    QVERIFY(QTest::qWaitFor([scene]() { return scene->getStartedJobCount() >= 3 && scene->getDecodingCount() == 0; }, CanvasTestHelper::timeout(20000)));
    QCOMPARE(scene->getPicturePixels(), qint64(2 * 64 * 64));
    int pictureCount = 0;
    for (const NodeReference& image : H::findNodes(fixture.context, pdf::PDFContentNodeKind::Image))
    {
        const pdf::PDFAppEditContentImageItem* item = dynamic_cast<const pdf::PDFAppEditContentImageItem*>(scene->findItem(image));
        pictureCount += item && item->hasPicture() ? 1 : 0;
    }
    QCOMPARE(pictureCount, 2);
}

void AppToolContentCanvasTest::formUseClippedByPageClips()
{
    // R6-7 (ISO 32000-2, 8.5.4): the clip of the page applies to the content of a form use, also of a scaled one -
    // drawn and hit only inside of it; the form 0 0 200 200 at 0.5 from (100, 100) covers 100 ... 200, the clip 50 ... 150
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("q 50 50 100 100 re W n 0.5 0 0 0.5 100 100 cm /Fm1 Do Q\n"), [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Fm1", H::addForm(builder, { }, QByteArrayLiteral("1 0 0 rg 0 0 200 200 re f"), 200) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference use = H::findNodes(fixture.context, pdf::PDFContentNodeKind::FormUse).front();
    QVERIFY(QTest::qWaitFor([scene, use]() { const H::FormItem* item = dynamic_cast<const H::FormItem*>(scene->findItem(use)); return item && item->getFormVersion() != 0; },
                            CanvasTestHelper::timeout(10000)));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    QCOMPARE(scene->getNodesAt(QPointF(120, 120), 1.0, false), objects);
    QVERIFY(scene->getNodesAt(QPointF(170, 170), 1.0, false).empty());

    const QImage image = H::render(scene);
    QVERIFY(H::isColor(image.pixelColor(120, 120), Qt::red));
    QVERIFY(H::isColor(image.pixelColor(170, 170), Qt::white));
}

void AppToolContentCanvasTest::hiddenFormUseHidesContent()
{
    // R6-8 (zadani_gui.md 4.15): a hidden form use draws its outline only, its content is hidden
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("/Fm1 Do\n"), [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Fm1", H::addForm(builder, { }, QByteArrayLiteral("1 0 0 rg 0 0 200 200 re f"), 200) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference use = H::findNodes(fixture.context, pdf::PDFContentNodeKind::FormUse).front();
    QVERIFY(QTest::qWaitFor([scene, use]() { const H::FormItem* item = dynamic_cast<const H::FormItem*>(scene->findItem(use)); return item && item->getFormVersion() != 0; },
                            CanvasTestHelper::timeout(10000)));
    QVERIFY(H::isColor(H::render(scene).pixelColor(100, 100), Qt::red));

    fixture.context->getSession()->setNodeHidden(use, true);
    QCoreApplication::processEvents();
    QVERIFY(H::isColor(H::render(scene).pixelColor(100, 100), Qt::white));
    for (QGraphicsItem* child : scene->findItem(use)->childItems())
    {
        QVERIFY(!child->isVisible());
    }
}

void AppToolContentCanvasTest::imageHitByItsShapeAndClip()
{
    // R6-9: an image is hit by its unit square under its matrix and by the clips in effect - not by its bounding
    // rectangle (a clipped photo in a frame, a rotated image)
    using H = CanvasSceneTestHelper;
    const H::D::ResourcesFactory resources = [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder dictionary;
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Im1", H::addImage(builder, 2, 2, Qt::blue) } }));
        return dictionary;
    };
    {
        CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("0 1 0 rg 0 0 200 200 re f q 100 100 50 50 re W n q 100 0 0 100 50 50 cm /Im1 Do Q Q\n"), resources));
        QVERIFY(fixture.showPage(0));
        const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
        QCOMPARE(objects.size(), size_t(2));
        QCOMPARE(fixture.scene()->getNodesAt(QPointF(75, 75), 1.0, false), std::vector<NodeReference>{ objects[0] });
        QCOMPARE(fixture.scene()->getNodesAt(QPointF(125, 125), 1.0, false), (std::vector<NodeReference>{ objects[1], objects[0] }));
    }
    {
        // The unit square turned by 45 degrees: the edge from (100, 50) to (29.3, 120.7) is the line x + y = 150
        CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("0 1 0 rg 0 0 200 200 re f q 70.7107 70.7107 -70.7107 70.7107 100 50 cm /Im1 Do Q\n"), resources));
        QVERIFY(fixture.showPage(0));
        const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
        QCOMPARE(objects.size(), size_t(2));
        QCOMPARE(fixture.scene()->getNodesAt(QPointF(40, 60), 1.0, false), std::vector<NodeReference>{ objects[0] });
        QCOMPARE(fixture.scene()->getNodesAt(QPointF(100, 120), 1.0, false), (std::vector<NodeReference>{ objects[1], objects[0] }));
    }
}

void AppToolContentCanvasTest::hiddenObjectHitByOutline()
{
    // R6-17: a hidden object is hit only by a band around its outline - its area lets the click through (A 10 10 50 50)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(15, 15), 1.0, false), std::vector<NodeReference>{ objects[0] });
    fixture.context->getSession()->setNodeHidden(objects[0], true);
    QCoreApplication::processEvents();
    QVERIFY(fixture.scene()->getNodesAt(QPointF(15, 15), 1.0, false).empty());
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(10.5, 15), 1.0, false), std::vector<NodeReference>{ objects[0] });
}

void AppToolContentCanvasTest::dimmingKeepsConstantAlpha()
{
    // R6-15: the dimming multiplies the constant alpha of an image (ca 0.5), it never replaces it
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("/GS1 gs q 40 0 0 40 10 10 cm /Im1 Do Q\n"), [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Im1", H::addImage(builder, 2, 2, Qt::blue) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), H::D::createDictionaryObject({ { "GS1", H::D::createDictionaryObject({ { "ca", pdf::PDFObject::createReal(0.5) } }) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentItem* item = fixture.scene()->findItem(H::findNodes(fixture.context, pdf::PDFContentNodeKind::Image).front());
    QVERIFY(item);
    QVERIFY(qFuzzyCompare(item->opacity(), 0.5));
    item->setDimmed(true);
    QVERIFY(qFuzzyCompare(item->opacity(), 0.175));
    item->setDimmed(false);
    QVERIFY(qFuzzyCompare(item->opacity(), 0.5));
}

void AppToolContentCanvasTest::shadingAndImageAlphaAndBlend()
{
    // R6-16 (zadani_gui.md 4.15): a shading with ca 0.5 is drawn half transparent (red over the white paper: 255 128
    // 128); an image with /BM /Multiply multiplies (blue x green = black)
    using H = CanvasSceneTestHelper;
    const QByteArray content = QByteArrayLiteral("/GS1 gs q 0 0 100 100 re W n /Sh1 sh Q /GS0 gs 0 1 0 rg 100 100 100 100 re f /GS2 gs q 100 0 0 100 100 100 cm /Im1 Do Q\n");
    CanvasTestFixture fixture(H::createDocument(content, [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), H::D::createDictionaryObject({ { "Im1", H::addImage(builder, 2, 2, Qt::blue) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Shading"), H::D::createDictionaryObject({ { "Sh1", H::createAxialShading(Qt::red) } }));
        resources.addEntry(pdf::PDFInplaceOrMemoryString("ExtGState"), H::D::createDictionaryObject({ { "GS0", H::D::createDictionaryObject({ { "ca", pdf::PDFObject::createReal(1.0) } }) },
                                                                                                       { "GS1", H::D::createDictionaryObject({ { "ca", pdf::PDFObject::createReal(0.5) } }) },
                                                                                                       { "GS2", H::D::createDictionaryObject({ { "BM", pdf::PDFObject::createName("Multiply") } }) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    const QImage image = H::renderFinished(fixture.scene());
    QVERIFY(H::isColor(image.pixelColor(50, 50), QColor(255, 128, 128)));
    QVERIFY(H::isColor(image.pixelColor(150, 150), QColor(0, 0, 0)));
}

void AppToolContentCanvasTest::shadingBoxFollowsItsClip()
{
    // R6-19: the box of a shading (sh) is cut by the clips in effect - a moved clip gives the shading its new box
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("q 50 50 50 50 re W n /Sh1 sh Q\n"), [](pdf::PDFDocumentBuilder*)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Shading"), H::D::createDictionaryObject({ { "Sh1", H::createAxialShading(Qt::red) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference shading = H::findNodes(fixture.context, pdf::PDFContentNodeKind::Shading).front();
    QVERIFY(CanvasTestHelper::isSame(scene->findItem(shading)->boundingRect(), QRectF(50, 50, 50, 50)));

    const NodeReference clip = H::findPath(fixture.context, QRectF(50, 50, 50, 50));
    QVERIFY(clip.isValid());
    if (!fixture.context->execute(pdf::PDFContentCommandFactory::createTransform({ clip }, QTransform::fromTranslate(20, 0))))
    {
        QSKIP("the core refuses to move a clipping path alone");
    }
    QVERIFY(CanvasTestHelper::isSame(scene->findItem(shading)->boundingRect(), QRectF(70, 50, 50, 50)));
}

void AppToolContentCanvasTest::textClipApplied()
{
    // Item 3 of FX2 (ISO 32000-2, 9.3.6): a text of the rendering mode 7 adds its glyphs to the clip at ET - the red
    // rectangle behind it is drawn and hit only inside of the glyphs
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("BT 7 Tr /F1 80 Tf 20 100 Td (AB) Tj ET 1 0 0 rg 0 0 200 200 re f\n"), [](pdf::PDFDocumentBuilder* builder)
    {
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), H::D::createDictionaryObject({ { "F1", H::D::addSimpleFontObject(builder) } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference text = H::findNodes(fixture.context, pdf::PDFContentNodeKind::TextObject).front();
    const pdf::PDFAppEditContentTextItem* textItem = dynamic_cast<const pdf::PDFAppEditContentTextItem*>(scene->findItem(text));
    QVERIFY(textItem);
    const std::optional<QPainterPath> clip = textItem->getTextClip();
    QVERIFY(clip && !clip->isEmpty());

    // A point inside of a glyph (a search over a grid of the box of the glyphs)
    QPointF inside;
    bool isFound = false;
    const QRectF box = clip->boundingRect();
    for (qreal y = box.top() + 1.0; y < box.bottom() && !isFound; y += 1.0)
    {
        for (qreal x = box.left() + 1.0; x < box.right() && !isFound; x += 1.0)
        {
            if (clip->contains(QPointF(x, y)) && clip->contains(QPointF(x - 1.5, y)) && clip->contains(QPointF(x + 1.5, y)) && clip->contains(QPointF(x, y - 1.5)) && clip->contains(QPointF(x, y + 1.5)))
            {
                inside = QPointF(x, y);
                isFound = true;
            }
        }
    }
    QVERIFY(isFound);

    const NodeReference rectangle = H::findPath(fixture.context, QRectF(0, 0, 200, 200));
    // (the text object itself, a clip only, is hit below the rectangle by the rectangles of its lines)
    QCOMPARE(scene->getNodesAt(inside, 0.5, false), (std::vector<NodeReference>{ rectangle, text }));
    QVERIFY(scene->getNodesAt(QPointF(190, 10), 0.5, false).empty());
    const QImage image = H::render(scene);
    QVERIFY(H::isColor(image.pixelColor(inside.toPoint()), Qt::red, 40));
    QVERIFY(H::isColor(image.pixelColor(190, 10), Qt::white));
}

void AppToolContentCanvasTest::patternFillsDrawn()
{
    // Item 2 of FX2: a tiling pattern is drawn by its cell repeated (/P1 of the test documents: red cells 10 x 10 at
    // (5 + 20 i, 5 + 20 j), the square 25 ... 65 shows four of them); a shading pattern by its gradient
    using H = CanvasSceneTestHelper;
    {
        CanvasTestFixture fixture(pdf::PDFDocumentPointer(new pdf::PDFDocument(H::D::createTilingPatternDocument())));
        QVERIFY(fixture.showPage(0));
        const QImage image = H::renderFinished(fixture.scene());
        QVERIFY(H::isColor(image.pixelColor(30, 30), Qt::red));
        QVERIFY(H::isColor(image.pixelColor(50, 50), Qt::red));
        QVERIFY(H::isColor(image.pixelColor(40, 40), Qt::white));
        QVERIFY(H::isColor(image.pixelColor(80, 80), Qt::white));
    }
    {
        CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("/Pattern cs /SP scn 100 100 50 50 re f\n"), [](pdf::PDFDocumentBuilder*)
        {
            const pdf::PDFObject pattern = H::D::createDictionaryObject({ { "Type", pdf::PDFObject::createName("Pattern") }, { "PatternType", pdf::PDFObject::createInteger(2) },
                                                                          { "Shading", H::createAxialShading(QColor(0, 0, 255)) } });
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("Pattern"), H::D::createDictionaryObject({ { "SP", pattern } }));
            return resources;
        }));
        QVERIFY(fixture.showPage(0));
        const QImage image = H::renderFinished(fixture.scene());
        QVERIFY(H::isColor(image.pixelColor(125, 125), Qt::blue));
        QVERIFY(H::isColor(image.pixelColor(90, 90), Qt::white));
    }
}

void AppToolContentCanvasTest::functionShadingDrawnAsPicture()
{
    // Item 2 of FX2 with FX1 item 2: a shading of the type 1 is the picture of the core (no placeholder) - a function of
    // one color red over the domain 0 ... 1 mapped onto 0 ... 100
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(H::createDocument(QByteArrayLiteral("q 0 0 100 100 re W n /Sh1 sh Q\n"), [](pdf::PDFDocumentBuilder* builder)
    {
        const pdf::PDFObject function = H::D::addStreamObject(builder, { { "FunctionType", pdf::PDFObject::createInteger(4) }, { "Domain", H::D::createNumberArrayObject({ 0, 1, 0, 1 }) },
                                                                         { "Range", H::D::createNumberArrayObject({ 0, 1, 0, 1, 0, 1 }) } },
                                                              QByteArrayLiteral("{ pop pop 1 0 0 }"));
        const pdf::PDFObject shading = H::D::createDictionaryObject({ { "ShadingType", pdf::PDFObject::createInteger(1) }, { "ColorSpace", pdf::PDFObject::createName("DeviceRGB") },
                                                                      { "Domain", H::D::createNumberArrayObject({ 0, 1, 0, 1 }) },
                                                                      { "Matrix", H::D::createNumberArrayObject({ 100, 0, 0, 100, 0, 0 }) }, { "Function", function } });
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Shading"), H::D::createDictionaryObject({ { "Sh1", shading } }));
        return resources;
    }));
    QVERIFY(fixture.showPage(0));
    const QImage image = H::renderFinished(fixture.scene());
    const pdf::PDFAppEditContentShadingItem* item = dynamic_cast<const pdf::PDFAppEditContentShadingItem*>(fixture.scene()->findItem(H::findNodes(fixture.context, pdf::PDFContentNodeKind::Shading).front()));
    QVERIFY(item && item->getShading());
    QCOMPARE(item->getShading()->status, pdf::PDFContentDisplayShading::Status::Done);
    QCOMPARE(item->getShading()->kind, pdf::PDFContentDisplayShading::Kind::Picture);
    QVERIFY(H::isColor(image.pixelColor(50, 50), Qt::red));
}

void AppToolContentCanvasTest::annotationsUnderlay()
{
    // Item 1 of FX2 (zadani_gui.md 4.15, -annotations-underlay / -annotations-hidden): the annotations of the page are
    // drawn dimmed under the content (a red square annotation 100 ... 150: 0.35 of red over white = 255 166 166), never
    // selectable, hidden by the option; rendered in a worker once per page
    using H = CanvasSceneTestHelper;
    pdf::PDFDocument document = H::D::createDocumentFromObjects([](pdf::PDFDocumentBuilder* builder) { return H::D::addStreamObject(builder, { }, QByteArrayLiteral("0 0 1 rg 0 0 50 50 re f\n")); },
                                                                &H::D::createNoResources,
                                                                [](pdf::PDFDocumentBuilder* builder, pdf::PDFObjectReference page)
    {
        const pdf::PDFObject appearance = H::D::addStreamObject(builder, { { "Type", pdf::PDFObject::createName("XObject") }, { "Subtype", pdf::PDFObject::createName("Form") },
                                                                           { "BBox", H::D::createIntegerArrayObject({ 0, 0, 50, 50 }) }, { "Resources", H::D::createDictionaryObject({ }) } },
                                                                QByteArrayLiteral("1 0 0 rg 0 0 50 50 re f"));
        const pdf::PDFObject annotation = H::D::addObject(builder, H::D::createDictionaryObject({ { "Type", pdf::PDFObject::createName("Annot") }, { "Subtype", pdf::PDFObject::createName("Square") },
                                                                                                    { "Rect", H::D::createIntegerArrayObject({ 100, 100, 150, 150 }) },
                                                                                                    { "F", pdf::PDFObject::createInteger(4) },
                                                                                                    { "AP", H::D::createDictionaryObject({ { "N", appearance } }) } }));
        builder->mergeTo(page, H::D::createDictionaryObject({ { "Annots", H::D::createArrayObject({ annotation }) } }));
    });
    CanvasTestFixture fixture(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    pdf::PDFAppEditContentUnderlayItem* underlay = scene->getUnderlayItem();
    QVERIFY(underlay);
    QVERIFY(QTest::qWaitFor([underlay]() { return !underlay->getPicture().isNull(); }, CanvasTestHelper::timeout(20000)));
    QCOMPARE(underlay->getPageIndex(), pdf::PDFInteger(0));
    QVERIFY(underlay->isVisible());
    QVERIFY(underlay->zValue() < pdf::PDFAppEditContentScene::getLayerZ(pdf::PDFAppEditContentSceneLayer::Content));
    QImage image = H::render(scene);
    QVERIFY(H::isColor(image.pixelColor(125, 125), QColor(255, 166, 166), 24));
    QVERIFY(H::isColor(image.pixelColor(75, 75), Qt::white));
    QVERIFY(scene->getNodesAt(QPointF(125, 125), 1.0, true).empty());

    // The option hides it; on again: no new rendering (the same page)
    const int jobs = scene->getStartedJobCount();
    fixture.context->getOptions()->setValue(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_ANNOTATIONS), false);
    QVERIFY(!underlay->isVisible());
    image = H::render(scene);
    QVERIFY(H::isColor(image.pixelColor(125, 125), Qt::white));
    fixture.context->getOptions()->setValue(QString::fromLatin1(pdf::PDFAppEditContentOptions::SHOW_ANNOTATIONS), true);
    QVERIFY(underlay->isVisible());
    QCOMPARE(scene->getStartedJobCount(), jobs);
}

void AppToolContentCanvasTest::paintSwitchedOffHasNoShape()
{
    // Item 8 of FX2 (FX1 item 1): a path with its fill switched off draws nothing and is not hit - it stays selectable
    // from the tree and shows its frame; switched on again it is drawn and hit
    using H = CanvasSceneTestHelper;
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    const NodeReference a = CanvasTestHelper::getObjects(fixture.context).front();

    pdf::PDFContentPaintSwitches switches;
    switches.fill = false;
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createSetAppearance({ a }, { }, switches)));
    const pdf::PDFAppEditContentPathItem* item = dynamic_cast<const pdf::PDFAppEditContentPathItem*>(scene->findItem(a));
    QVERIFY(item && !item->isPainted());
    QVERIFY(H::isColor(H::render(scene).pixelColor(15, 15), Qt::white));
    QVERIFY(scene->getNodesAt(QPointF(15, 15), 1.0, false).empty());
    QVERIFY(scene->getNodesAt(QPointF(10.5, 30), 1.0, false).empty());
    fixture.selection()->setNode(a);
    QVERIFY(CanvasTestHelper::isSame(scene->getSelectionItem()->getHandleBox(), QRectF(10, 10, 50, 50)));

    switches.fill = true;
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createSetAppearance({ a }, { }, switches)));
    QVERIFY(H::isColor(H::render(scene).pixelColor(15, 15), Qt::red));
    QCOMPARE(scene->getNodesAt(QPointF(15, 15), 1.0, false), std::vector<NodeReference>{ a });
}

void AppToolContentCanvasTest::sessionReplacedReconnects()
{
    // FX3 request 1 (PDFAppEditContentContext::reloadDocument): the scene and the selection follow the new session - a
    // command of the new session rebuilds the items and prunes the selection
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    const pdf::PDFDocumentPointer document = CanvasTestHelper::createDocument(CanvasTestHelper::basicContent());
    fixture.context->reloadDocument(document);
    QVERIFY(QTest::qWaitFor([&fixture, &document]() { return fixture.context->getSession()->getDocument() == document.data() && fixture.context->getContainer(); },
                            CanvasTestHelper::timeout(15000)));
    QCoreApplication::processEvents();

    const std::vector<NodeReference> objects = CanvasTestHelper::getObjects(fixture.context);
    QVERIFY(fixture.scene()->findItem(objects[0]));
    fixture.selection()->setNode(objects[0]);
    QVERIFY(fixture.context->execute(pdf::PDFContentCommandFactory::createDelete({ objects[0] })));
    QVERIFY(!fixture.scene()->findItem(objects[0]));
    QVERIFY(fixture.selection()->isEmpty());
}

void AppToolContentCanvasTest::toolPreviewCoversItsRectangle()
{
    // R6-11: the item of the tool preview covers what the tool paints (the rubber band), not the scene - a move of the
    // drag repaints that rectangle only; nothing after the release
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentScene* scene = fixture.scene();
    QGraphicsItem* toolPreview = nullptr;
    for (QGraphicsItem* item : scene->items())
    {
        if (!item->parentItem() && qFuzzyCompare(item->zValue(), pdf::PDFAppEditContentScene::getLayerZ(pdf::PDFAppEditContentSceneLayer::ToolPreview)))
        {
            toolPreview = item;
        }
    }
    QVERIFY(toolPreview);
    QVERIFY(toolPreview->boundingRect().isEmpty());

    fixture.drag(fixture.pixel(QPointF(5, 190)), fixture.pixel(QPointF(80, 120)), Qt::NoModifier, false);
    const auto* tool = static_cast<const SelectTool*>(fixture.context->getToolController()->getActiveTool());
    QCOMPARE(tool->getGesture(), SelectTool::Gesture::RubberBand);
    const QRectF expected = tool->getPreviewRectangle(scene->getPixelSize());
    QVERIFY(!expected.isEmpty());
    QVERIFY(CanvasTestHelper::isSame(toolPreview->boundingRect(), expected));
    QVERIFY(toolPreview->boundingRect().width() < scene->sceneRect().width());

    fixture.sendMouse(QEvent::MouseButtonRelease, fixture.pixel(QPointF(80, 120)), Qt::LeftButton, Qt::NoButton);
    QCoreApplication::processEvents();
    QVERIFY(toolPreview->boundingRect().isEmpty());
}

void AppToolContentCanvasTest::sceneRectangleBounded()
{
    // R6-22: a path at 10^9 does not stretch the scene - the content is bounded to the paper grown by CONTENT_REACH
    // page sizes (200 x 2 = 400 on every side); the scene has no margin of its own (the view adds them)
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(QByteArrayLiteral("0 0 1 rg 10 10 10 10 re f 1000000000 1000000000 10 10 re f\n")));
    QVERIFY(fixture.showPage(0));
    const QRectF rectangle = fixture.scene()->sceneRect();
    QVERIFY2(rectangle.left() >= -400.0 - 1e-6 && rectangle.top() >= -400.0 - 1e-6 && rectangle.right() <= 600.0 + 1e-6 && rectangle.bottom() <= 600.0 + 1e-6,
             qPrintable(QStringLiteral("%1 %2 %3 %4").arg(rectangle.left()).arg(rectangle.top()).arg(rectangle.right()).arg(rectangle.bottom())));
    QVERIFY(rectangle.contains(QRectF(0, 0, 200, 200)));
}

QTEST_MAIN(AppToolContentCanvasTest)

#include "tst_apptoolcontentcanvastest.moc"
