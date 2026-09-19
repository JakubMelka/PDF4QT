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

#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfdrawwidget.h"
#include "pdfpagecontenteditortools.h"
#include "pdfpagecontenteditorwidget.h"
#include "pdfpagecontentelements.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QAction>
#include <QPainter>
#include <QToolButton>

class PageContentEditorToolsTest : public QObject
{
    Q_OBJECT

private slots:
    void toolIsDeactivatedAfterSingleElement();
    void toolStaysActiveWhenCreatingMultipleElements();
    void sizeOfLastElementIsReusedBySingleClick();
    void shiftDefinesNewSizeOfElement();
    void sizeOfLastElementIsNotReusedInNextSession();
    void dotToolStaysActiveWhenCreatingMultipleElements();
    void lineToolForgetsStartPointOfUnfinishedLine();
    void previewIsDrawnWhilePageIsNotCompiled();
    void pageContentIsDrawnWhenPageIsCompiled();
    void toolButtonHasNoShortcutOfItsAction();
};

/// Widget with a single page document, on which the page content editing tools
/// are tested. The page is large enough to be entirely visible in the widget.
struct EditorToolsFixture
{
    pdf::PDFDocument document;
    pdf::PDFCMSManager cms{nullptr};
    pdf::PDFProgress progress{nullptr};
    pdf::PDFWidget widget{&cms, pdf::RendererEngine::QPainter, nullptr};
    pdf::PDFPageContentScene scene{nullptr};

    explicit EditorToolsFixture(pdf::PDFDocument doc) : document(std::move(doc))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        pdf::PDFModifiedDocument modified(&document, nullptr);
        widget.setDocument(modified, {});
        scene.setWidget(&widget);
        scene.setActive(true);
    }

    ~EditorToolsFixture()
    {
        scene.setActive(false);
        scene.setWidget(nullptr);
        widget.setDocument(pdf::PDFModifiedDocument(), {});
    }

    /// Returns position in the widget, on which the given point of the first page lies.
    QPoint getDevicePoint(QPointF pagePoint) const
    {
        const auto snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        Q_ASSERT(!snapshot.items.empty());
        return snapshot.items.front().pageToDeviceMatrix.map(pagePoint).toPoint();
    }

    /// Returns area of the widget, which is covered by the given rectangle of the first page.
    QRectF getDeviceRect(QRectF pageRect) const
    {
        const auto snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        Q_ASSERT(!snapshot.items.empty());
        return snapshot.items.front().pageToDeviceMatrix.mapRect(pageRect);
    }

    /// Sends a left button click to the tool.
    void click(pdf::PDFWidgetTool* tool, QPointF pagePoint, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QPoint position = getDevicePoint(pagePoint);
        QMouseEvent move(QEvent::MouseMove, position, position, Qt::NoButton, Qt::NoButton, modifiers);
        QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton, Qt::LeftButton, modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton, Qt::NoButton, modifiers);
        tool->mouseMoveEvent(&widget, &move);
        tool->mousePressEvent(&widget, &press);
        tool->mouseReleaseEvent(&widget, &release);
    }
};

static pdf::PDFDocument createSinglePageDocument()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 400, 400));
    return builder.build();
}

/// Returns the single rectangle element stored in the scene.
static const pdf::PDFPageContentElementRectangle* getSingleRectangle(const pdf::PDFPageContentScene& scene)
{
    const auto elementsByPage = scene.getElementsByPage();
    if (elementsByPage.size() != 1 || elementsByPage.begin()->second.size() != 1)
    {
        return nullptr;
    }

    return elementsByPage.begin()->second.front()->asElementRectangle();
}

void PageContentEditorToolsTest::toolIsDeactivatedAfterSingleElement()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementRectangleTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action, false, nullptr);
    tool.setActive(true);

    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 100));

    QCOMPARE(fixture.scene.getElementIds().size(), size_t(1));
    QVERIFY(!tool.isActive());
}

void PageContentEditorToolsTest::toolStaysActiveWhenCreatingMultipleElements()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementRectangleTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action, false, nullptr);
    tool.setMultipleElementCreationEnabled(true);
    tool.setActive(true);

    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 100));

    QCOMPARE(fixture.scene.getElementIds().size(), size_t(1));
    QVERIFY(tool.isActive());
}

void PageContentEditorToolsTest::sizeOfLastElementIsReusedBySingleClick()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementRectangleTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action, false, nullptr);
    tool.setMultipleElementCreationEnabled(true);
    tool.setActive(true);

    // The first element defines the size, which is reused by the next elements
    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 100));
    const pdf::PDFPageContentElementRectangle* firstElement = getSingleRectangle(fixture.scene);
    QVERIFY(firstElement);
    const QSizeF size = firstElement->getRectangle().size();

    fixture.scene.clear();

    // A single click creates the next element of the same size, centered at the point
    fixture.click(&tool, QPointF(250, 250));

    const pdf::PDFPageContentElementRectangle* secondElement = getSingleRectangle(fixture.scene);
    QVERIFY(secondElement);
    const QRectF rectangle = secondElement->getRectangle();
    QVERIFY(qFuzzyCompare(rectangle.width(), size.width()));
    QVERIFY(qFuzzyCompare(rectangle.height(), size.height()));

    // The element lies in the bottom right quadrant of the cross, which marks the
    // picked point, so the picked point is its top left corner in the widget.
    const QPoint pickedPoint = fixture.getDevicePoint(QPointF(250, 250));
    const QRectF deviceRectangle = fixture.getDeviceRect(rectangle);
    QVERIFY(qAbs(deviceRectangle.left() - pickedPoint.x()) < 2.0);
    QVERIFY(qAbs(deviceRectangle.top() - pickedPoint.y()) < 2.0);
    QVERIFY(tool.isActive());
}

void PageContentEditorToolsTest::shiftDefinesNewSizeOfElement()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementRectangleTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action, false, nullptr);
    tool.setMultipleElementCreationEnabled(true);
    tool.setActive(true);

    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 100));
    fixture.scene.clear();

    // The size of the last element is not reused, the user defines a new one
    fixture.click(&tool, QPointF(200, 200), Qt::ShiftModifier);
    QVERIFY(fixture.scene.isEmpty());

    fixture.click(&tool, QPointF(380, 300));

    const pdf::PDFPageContentElementRectangle* element = getSingleRectangle(fixture.scene);
    QVERIFY(element);
    // The picked points are rounded to the pixels of the widget, so the size of
    // the rectangle is not exact.
    const QRectF rectangle = element->getRectangle();
    QVERIFY(qAbs(rectangle.width() - 180.0) < 2.0);
    QVERIFY(qAbs(rectangle.height() - 100.0) < 2.0);
}

void PageContentEditorToolsTest::sizeOfLastElementIsNotReusedInNextSession()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementRectangleTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action, false, nullptr);
    tool.setMultipleElementCreationEnabled(true);
    tool.setActive(true);

    fixture.click(&tool, QPointF(50, 50));
    fixture.click(&tool, QPointF(150, 100));
    fixture.scene.clear();

    tool.setActive(false);
    tool.setActive(true);

    // An accidental single click must not create an element of the size used
    // in the previous session of the tool
    fixture.click(&tool, QPointF(250, 250));
    QVERIFY(fixture.scene.isEmpty());
}

void PageContentEditorToolsTest::dotToolStaysActiveWhenCreatingMultipleElements()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementDotTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action, nullptr);
    tool.setMultipleElementCreationEnabled(true);
    tool.setActive(true);

    fixture.click(&tool, QPointF(50, 50));
    QVERIFY(tool.isActive());
    fixture.click(&tool, QPointF(100, 100));
    QVERIFY(tool.isActive());

    QCOMPARE(fixture.scene.getElementIds().size(), size_t(2));
}

void PageContentEditorToolsTest::lineToolForgetsStartPointOfUnfinishedLine()
{
    EditorToolsFixture fixture(createSinglePageDocument());
    QAction action;
    pdf::PDFCreatePCElementLineTool tool(fixture.widget.getDrawWidgetProxy(), &fixture.scene, &action,
                                         true, false, nullptr);

    // The user picks the start point of a horizontal line and leaves the tool
    tool.setActive(true);
    fixture.click(&tool, QPointF(50, 50));
    QVERIFY(fixture.scene.isEmpty());
    tool.setActive(false);

    // The start point of the unfinished line must not be used in the next session,
    // the first picked point is the start point of a new line
    tool.setActive(true);
    fixture.click(&tool, QPointF(100, 300));
    QVERIFY(fixture.scene.isEmpty());

    fixture.click(&tool, QPointF(300, 300));

    const auto elementsByPage = fixture.scene.getElementsByPage();
    QCOMPARE(elementsByPage.size(), size_t(1));
    QCOMPARE(elementsByPage.begin()->second.size(), size_t(1));
    const pdf::PDFPageContentElementLine* element = elementsByPage.begin()->second.front()->asElementLine();
    QVERIFY(element);

    const QLineF line = element->getLine();
    QVERIFY(qAbs(line.p1().x() - 100.0) < 2.0);
    QVERIFY(qAbs(line.p1().y() - 300.0) < 2.0);
    QVERIFY(qAbs(line.p2().x() - 300.0) < 2.0);
    QVERIFY(qAbs(line.p2().y() - 300.0) < 2.0);
}

/// Counts, how many times the draw interfaces were asked to draw a page.
struct CountingDrawInterface : public pdf::IDocumentDrawInterface
{
    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const pdf::PDFColorConvertor& convertor,
                          QList<pdf::PDFRenderError>& errors) const override
    {
        Q_UNUSED(painter);
        Q_UNUSED(pageIndex);
        Q_UNUSED(compiledPage);
        Q_UNUSED(layoutGetter);
        Q_UNUSED(pagePointToDevicePointMatrix);
        Q_UNUSED(convertor);
        Q_UNUSED(errors);

        ++drawPageCount;
    }

    mutable int drawPageCount = 0;
};

void PageContentEditorToolsTest::previewIsDrawnWhilePageIsNotCompiled()
{
    // The preview of the created element is drawn by a draw interface of the tool.
    // The draw interfaces must be asked to draw even when the compiled page is not
    // available yet, otherwise the preview disappears while the page is compiled.
    EditorToolsFixture fixture(createSinglePageDocument());
    auto* proxy = fixture.widget.getDrawWidgetProxy();

    CountingDrawInterface drawInterface;
    proxy->registerDrawInterface(&drawInterface);

    // The compiled page is not available while the page is being compiled - the
    // stopped compiler returns no compiled page in the same way.
    if (proxy->getCompiler()->getState() == pdf::PDFAsynchronousPageCompiler::State::Active)
    {
        proxy->getCompiler()->stop(true);
    }

    QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    {
        QPainter painter(&image);
        proxy->draw(&painter, QRect(0, 0, 800, 800));
    }

    proxy->unregisterDrawInterface(&drawInterface);
    QVERIFY(drawInterface.drawPageCount > 0);
}

void PageContentEditorToolsTest::pageContentIsDrawnWhenPageIsCompiled()
{
    // Counterpart of previewIsDrawnWhilePageIsNotCompiled - the content of the compiled
    // page must still be drawn, the draw interfaces must not have replaced it.
    pdf::PDFDocumentBuilder builder;
    const auto page = builder.appendPage(QRectF(0, 0, 400, 400));
    builder.createAnnotationSquare(page, QRectF(50, 50, 300, 300), 5.0, Qt::black, Qt::black, QString(), QString(), QString());

    EditorToolsFixture fixture(builder.build());
    auto* proxy = fixture.widget.getDrawWidgetProxy();

    QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);

    auto drawAndCountPaperPixels = [&]() -> int
    {
        image.fill(Qt::transparent);
        QPainter painter(&image);
        proxy->draw(&painter, QRect(0, 0, 800, 800));
        painter.end();

        int paperPixels = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                if (image.pixelColor(x, y) == QColor(Qt::white))
                {
                    ++paperPixels;
                }
            }
        }

        return paperPixels;
    };

    // The page is compiled asynchronously, the paper of the page appears when it is done
    QTRY_VERIFY_WITH_TIMEOUT(drawAndCountPaperPixels() > 10000, 10000);
}

void PageContentEditorToolsTest::toolButtonHasNoShortcutOfItsAction()
{
    // Two shortcuts of the same key sequence in a single window are ambiguous
    // and neither of them works, so the button of the action must not have one.
    pdf::PDFPageContentEditorWidget editorWidget(nullptr);
    QAction action(QString("Create Rectangle"));
    action.setShortcut(QKeySequence("R"));
    editorWidget.addAction(&action);

    const QList<QToolButton*> buttons = editorWidget.findChildren<QToolButton*>();
    const auto it = std::find_if(buttons.cbegin(), buttons.cend(), [](const QToolButton* button) { return button->text() == QString("Create Rectangle"); });
    QVERIFY(it != buttons.cend());
    QVERIFY((*it)->shortcut().isEmpty());
    QVERIFY((*it)->toolTip().contains(QString("R")));
}

QTEST_MAIN(PageContentEditorToolsTest)

#include "tst_pagecontenteditortoolstest.moc"
