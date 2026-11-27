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
#include <QPushButton>
#include <QScopeGuard>
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
    explicit CanvasTestFixture(pdf::PDFDocumentPointer document)
    {
        CanvasTestRecorder::reset();
        CanvasTestHelper::fillRegistry(&registry);
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
    void toolKeysOnlyWithFocus();
    void canvasShortcutRoutesApplicationCommand();
    void activeToolAnswersApplicationCommandFirst();
    void dropFromOutside();

    // Preview
    void previewWithoutDocumentFails();
    void previewRendersThePage();
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

    // The handle Right of the box 10 10 50 50 moves to x: the anchor is the left edge (x = 10)
    const QPoint from = fixture.pixel(pdf::PDFAppEditContentSelectionItem::getHandlePoint(frame, Handle::Right));
    QCOMPARE(item->getHandleAt(fixture.point(from), fixture.scene()->getPixelSize()), Handle::Right);
    const QPoint to = fixture.pixel(QPointF(110, 35));
    const qreal x = fixture.point(to).x();
    fixture.drag(from, to);
    QCOMPARE(fixture.context->getSession()->getUndoStack()->getCount(), size_t(1));
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, objects[0]), QRectF(10, 10, x - 10, 50)));

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
    const QPoint to = fixture.pixel(QPointF(180, 175));
    const qreal scale = qMax((fixture.point(to).x() - 100.0) / 40.0, (fixture.point(to).y() - 150.0) / 20.0);
    fixture.drag(from, to);
    QVERIFY(CanvasTestHelper::isSame(CanvasTestHelper::box(fixture.context, image), QRectF(100, 150, 40 * scale, 20 * scale), 1e-4));

    // Shift frees the proportions: the corner goes where the pointer is
    QVERIFY(fixture.context->getSession()->undo().has_value());
    fixture.click(fixture.pixel(QPointF(120, 160)));
    const QPoint to2 = fixture.pixel(QPointF(180, 175));
    fixture.drag(from, to2, Qt::ShiftModifier);
    const QPointF corner = fixture.point(to2);
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

void AppToolContentCanvasTest::previewWithoutDocumentFails()
{
    // Preview needs the shared document of the tool (a worker holds it): without a GUI it fails with a text and
    // starts no job; the items are hidden while Preview is on
    CanvasTestFixture fixture(CanvasTestHelper::createDocument(CanvasTestHelper::basicContent()));
    QVERIFY(fixture.showPage(0));
    pdf::PDFAppEditContentPreview* preview = fixture.context->getPreview();
    QSignalSpy failed(preview, &pdf::PDFAppEditContentPreview::failed);
    fixture.context->setPreviewActive(true);
    QCoreApplication::processEvents();
    QVERIFY(failed.count() >= 1);
    QVERIFY(!preview->isRendering());
    QVERIFY(!preview->getErrorText().isEmpty());
    QVERIFY(!fixture.context->isEditingAllowed());
    QVERIFY(fixture.scene()->getNodesAt(QPointF(20, 20), 1.0, false).empty());

    fixture.context->setPreviewActive(false);
    QCOMPARE(fixture.scene()->getNodesAt(QPointF(20, 20), 1.0, false).size(), size_t(1));
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
    if (request.pageRectangle.contains(QPointF(190, 190)))
    {
        QCOMPARE(colorAt(QPointF(190, 190)), QColor(Qt::white));
    }

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

QTEST_MAIN(AppToolContentCanvasTest)

#include "tst_apptoolcontentcanvastest.moc"
