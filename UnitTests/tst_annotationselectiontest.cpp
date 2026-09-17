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

#include "pdfannotationmanipulator.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"
#include "pdfwidgetutils.h"
#include "pdfcms.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QAction>
#include <QClipboard>
#include <QMimeData>

using namespace pdf;

/// Tests of the annotation selection and of the interactive editing of the
/// selected annotations in the widget annotation manager (mouse selection,
/// rubber band, keyboard, clipboard, handles of the selection frame).
class AnnotationSelectionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void clickSelectsAnnotation();
    void modifierClickTogglesSelection();
    void rubberBandSelectsContainedAnnotations();
    void deleteKeyRemovesSelection();
    void escapeClearsSelection();
    void selectAllShortcut();
    void arrowKeysNudgeSelection();
    void clipboardRoundTrip();
    void cutRemovesAnnotations();
    void resizeHandleScalesSelection();
    void rotationHandleRotatesLine();
    void menuRotationAndFlip();
    void pointHandleMovesPolygonVertex();
    void shiftConstrainsPointDirection();
    void insertAndRemovePoints();
    void pointHandlesNeedSingleSelection();
    void lockedAnnotationIsNotTransformed();
    void dropFromAnotherDocumentInsertsCopy();
    void selectionSurvivesDocumentModification();
    void selectionApiIgnoresUnselectableAnnotations();

private:
    QTemporaryDir m_settingsDirectory;
};

/// Widget with the annotation manager and a document. Modified documents emitted
/// by the manager are applied the same way as the application does it - the new
/// document replaces the old one and it is set to all components.
struct SelectionFixture
{
    PDFDocumentPointer document;
    PDFCMSManager cms{ nullptr };
    PDFProgress progress{ nullptr };
    PDFWidget widget{ &cms, RendererEngine::QPainter, nullptr };
    PDFWidgetAnnotationManager annotations{ widget.getDrawWidgetProxy(), nullptr };
    std::array<QAction, 10> actions;
    PDFToolManager tools{ widget.getDrawWidgetProxy(),
        { &actions[0], &actions[1], &actions[2], &actions[3], &actions[4], &actions[5],
          &actions[6], &actions[7], &actions[8], &actions[9] }, nullptr, &widget };
    int modificationCount = 0;

    explicit SelectionFixture(PDFDocument initialDocument) :
        document(new PDFDocument(std::move(initialDocument)))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.setAnnotationManager(&annotations);
        widget.setToolManager(&tools);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        apply(PDFModifiedDocument(document, nullptr));

        QObject::connect(&annotations, &PDFWidgetAnnotationManager::documentModified, &annotations, [this](PDFModifiedDocument modified)
        {
            ++modificationCount;
            PDFDocumentPointer oldDocument = document;
            document = modified;
            apply(PDFModifiedDocument(document, nullptr, modified.getFlags()));
        });
    }

    ~SelectionFixture()
    {
        tools.setDocument(PDFModifiedDocument());
        annotations.setDocument(PDFModifiedDocument());
        widget.setDocument(PDFModifiedDocument(), {});
        widget.setAnnotationManager(nullptr);
        widget.setToolManager(nullptr);
    }

    void apply(PDFModifiedDocument modified)
    {
        widget.setDocument(modified, {});
        annotations.setDocument(modified);
        tools.setDocument(modified);
    }

    QTransform pageToDevice() const
    {
        const PDFWidgetSnapshot snapshot = widget.getDrawWidgetProxy()->getSnapshot();
        return snapshot.items.empty() ? QTransform() : snapshot.items.front().pageToDeviceMatrix;
    }

    QPoint device(const QPointF& pagePoint) const
    {
        return pageToDevice().map(pagePoint).toPoint();
    }

    bool press(QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButton button = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseButtonPress, position, widget.mapToGlobal(position), button, button, modifiers);
        event.ignore();
        annotations.mousePressEvent(&widget, &event);
        return event.isAccepted();
    }

    bool move(QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseMove, position, widget.mapToGlobal(position), Qt::NoButton, buttons, modifiers);
        event.ignore();
        annotations.mouseMoveEvent(&widget, &event);
        return event.isAccepted();
    }

    bool release(QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(QEvent::MouseButtonRelease, position, widget.mapToGlobal(position), Qt::LeftButton, Qt::NoButton, modifiers);
        event.ignore();
        annotations.mouseReleaseEvent(&widget, &event);
        return event.isAccepted();
    }

    bool click(QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const bool accepted = press(position, modifiers);
        release(position, modifiers);
        return accepted;
    }

    bool key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        event.ignore();
        annotations.keyPressEvent(&widget, &event);
        return event.isAccepted();
    }

    QRectF rectangle(PDFObjectReference annotation) const
    {
        PDFDocumentDataLoaderDecorator loader(document.data());
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation));
        return dictionary ? loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized() : QRectF();
    }

    std::vector<PDFReal> numbers(PDFObjectReference annotation, const char* key) const
    {
        PDFDocumentDataLoaderDecorator loader(document.data());
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation));
        return dictionary ? loader.readNumberArrayFromDictionary(dictionary, key) : std::vector<PDFReal>();
    }

    std::vector<PDFObjectReference> pageAnnotations() const
    {
        return document->getCatalog()->getPage(0)->getAnnotations();
    }

    static bool fuzzyCompare(const QRectF& left, const QRectF& right, qreal tolerance = 0.5)
    {
        const bool result = std::abs(left.left() - right.left()) <= tolerance &&
                            std::abs(left.top() - right.top()) <= tolerance &&
                            std::abs(left.width() - right.width()) <= tolerance &&
                            std::abs(left.height() - right.height()) <= tolerance;
        if (!result)
        {
            qWarning() << "Rectangles differ:" << left << right;
        }
        return result;
    }
};

/// Document with two squares, the first one at (50, 50, 60, 40), the second one at (150, 150, 40, 40)
struct TwoSquares
{
    PDFObjectReference page;
    PDFObjectReference first;
    PDFObjectReference second;
    PDFDocument document;

    TwoSquares()
    {
        PDFDocumentBuilder builder;
        page = builder.appendPage(QRectF(0, 0, 300, 300));
        first = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "First", "Subject", "First contents");
        second = builder.createAnnotationSquare(page, QRectF(150, 150, 40, 40), 1.0, Qt::green, Qt::black, "Second", "Subject", "Second contents");
        document = builder.build();
    }
};

void AnnotationSelectionTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("PDF4QT-Tests");
    QCoreApplication::setApplicationName("AnnotationSelection");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void AnnotationSelectionTest::clickSelectsAnnotation()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);
    QSignalSpy selectionChanges(&fixture.annotations, &PDFWidgetAnnotationManager::selectionChanged);

    QVERIFY(!fixture.annotations.hasSelection());

    // Click on the first square selects it
    QVERIFY(fixture.click(fixture.device(QPointF(80, 70))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.first });
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.first));
    QCOMPARE(selectionChanges.count(), 1);

    // Click on the second square replaces the selection
    QVERIFY(fixture.click(fixture.device(QPointF(170, 170))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.second });

    // Click on an empty area clears the selection, but the event is not accepted,
    // so the widget can still scroll the document by dragging
    QVERIFY(!fixture.click(fixture.device(QPointF(250, 20))));
    QVERIFY(!fixture.annotations.hasSelection());
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationSelectionTest::modifierClickTogglesSelection()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    QVERIFY(fixture.click(fixture.device(QPointF(80, 70))));
    QVERIFY(fixture.click(fixture.device(QPointF(170, 170)), Qt::ControlModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == (std::vector<PDFObjectReference>{ squares.first, squares.second }));

    // Shift works the same way as Ctrl
    QVERIFY(fixture.click(fixture.device(QPointF(80, 70)), Qt::ShiftModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.second });

    // Modifier click on an empty area keeps the selection
    QVERIFY(fixture.click(fixture.device(QPointF(250, 20)), Qt::ControlModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.second });
}

void AnnotationSelectionTest::rubberBandSelectsContainedAnnotations()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    // Rectangle containing only the first square
    QVERIFY(fixture.press(fixture.device(QPointF(40, 40)), Qt::ControlModifier));
    QVERIFY(fixture.move(fixture.device(QPointF(120, 100)), Qt::ControlModifier));
    QVERIFY(fixture.release(fixture.device(QPointF(120, 100)), Qt::ControlModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.first });

    // Rectangle, which intersects the second square only partially, does not select it,
    // the selection is additive
    QVERIFY(fixture.press(fixture.device(QPointF(140, 140)), Qt::ControlModifier));
    QVERIFY(fixture.move(fixture.device(QPointF(170, 170)), Qt::ControlModifier));
    QVERIFY(fixture.release(fixture.device(QPointF(170, 170)), Qt::ControlModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.first });

    // Rectangle containing both squares
    QVERIFY(fixture.press(fixture.device(QPointF(10, 10)), Qt::ShiftModifier));
    QVERIFY(fixture.move(fixture.device(QPointF(250, 250)), Qt::ShiftModifier));
    QVERIFY(fixture.release(fixture.device(QPointF(250, 250)), Qt::ShiftModifier));
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.first));
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.second));
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationSelectionTest::deleteKeyRemovesSelection()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    // Nothing is selected and nothing is under the cursor - the key is not consumed
    QVERIFY(!fixture.key(Qt::Key_Delete));
    QCOMPARE(fixture.modificationCount, 0);

    fixture.annotations.setSelectedAnnotations({ squares.first, squares.second });
    QVERIFY(fixture.key(Qt::Key_Delete));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.pageAnnotations().empty());
    QVERIFY(!fixture.annotations.hasSelection());
}

void AnnotationSelectionTest::escapeClearsSelection()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    QVERIFY(!fixture.key(Qt::Key_Escape));
    fixture.annotations.setSelectedAnnotations({ squares.first });
    QVERIFY(fixture.key(Qt::Key_Escape));
    QVERIFY(!fixture.annotations.hasSelection());
}

void AnnotationSelectionTest::selectAllShortcut()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    QKeyEvent shortcutOverride(QEvent::ShortcutOverride, Qt::Key_A, Qt::ControlModifier);
    shortcutOverride.ignore();
    fixture.annotations.shortcutOverrideEvent(&fixture.widget, &shortcutOverride);
    QVERIFY(shortcutOverride.isAccepted());

    QVERIFY(fixture.key(Qt::Key_A, Qt::ControlModifier));
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));

    fixture.annotations.clearSelection();
    QVERIFY(!fixture.annotations.hasSelection());
}

void AnnotationSelectionTest::arrowKeysNudgeSelection()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);
    const QRectF original = fixture.rectangle(squares.first);

    fixture.annotations.setSelectedAnnotations({ squares.first });

    // Right arrow moves by one point to the right, up arrow moves up on the screen
    // (which is the positive direction of the y axis of the page)
    QVERIFY(fixture.key(Qt::Key_Right));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), original.translated(1.0, 0.0), 0.05));
    QVERIFY(fixture.key(Qt::Key_Up));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), original.translated(1.0, 1.0), 0.05));
    QVERIFY(fixture.key(Qt::Key_Left, Qt::ShiftModifier));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), original.translated(-9.0, 1.0), 0.05));
    QVERIFY(fixture.key(Qt::Key_Down, Qt::ShiftModifier));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), original.translated(-9.0, -9.0), 0.05));
    QCOMPARE(fixture.modificationCount, 4);

    // Selection survives the modifications
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.first });

    // The second square is untouched
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.second), QRectF(150, 150, 40, 40)));
}

void AnnotationSelectionTest::clipboardRoundTrip()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    QApplication::clipboard()->clear(QClipboard::Clipboard);
    QVERIFY(!fixture.annotations.canPasteAnnotations());

    // Copy shortcut is claimed only when something is selected
    QKeyEvent copyOverride(QEvent::ShortcutOverride, Qt::Key_C, Qt::ControlModifier);
    copyOverride.ignore();
    fixture.annotations.shortcutOverrideEvent(&fixture.widget, &copyOverride);
    QVERIFY(!copyOverride.isAccepted());

    fixture.annotations.setSelectedAnnotations({ squares.first });
    copyOverride.ignore();
    fixture.annotations.shortcutOverrideEvent(&fixture.widget, &copyOverride);
    QVERIFY(copyOverride.isAccepted());

    QVERIFY(fixture.key(Qt::Key_C, Qt::ControlModifier));
    QVERIFY(fixture.annotations.canPasteAnnotations());
    const QMimeData* mimeData = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
    QVERIFY(mimeData);
    QVERIFY(mimeData->hasFormat(PDFAnnotationManipulator::getMimeType()));
    QCOMPARE(mimeData->text(), QString("First contents"));
    QCOMPARE(fixture.modificationCount, 0);

    // Paste at a position - the pasted copy is centered at the position and selected
    const QRectF original = fixture.rectangle(squares.first);
    fixture.annotations.pasteAnnotations(fixture.device(QPointF(200, 60)));
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(fixture.pageAnnotations().size(), size_t(3));

    const std::vector<PDFObjectReference> selection = fixture.annotations.getSelectedAnnotations();
    QCOMPARE(selection.size(), size_t(1));
    QVERIFY(selection.front() != squares.first);
    QVERIFY(selection.front() != squares.second);
    QRectF expected = original;
    expected.moveCenter(QPointF(200, 60));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(selection.front()), expected));

    // Paste without a position keeps the original coordinates
    fixture.annotations.pasteAnnotations(std::nullopt);
    QCOMPARE(fixture.pageAnnotations().size(), size_t(4));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(fixture.annotations.getSelectedAnnotations().front()), original));

    // Paste near the page border keeps the annotation inside the page
    fixture.annotations.pasteAnnotations(fixture.device(QPointF(298, 2)));
    const QRectF clamped = fixture.rectangle(fixture.annotations.getSelectedAnnotations().front());
    QVERIFY(clamped.right() <= 300.0 + 0.01);
    QVERIFY(clamped.top() >= -0.01);
    QVERIFY(SelectionFixture::fuzzyCompare(QRectF(QPointF(), clamped.size()), QRectF(QPointF(), original.size())));

    // The original annotation is untouched
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), original));
    QApplication::clipboard()->clear(QClipboard::Clipboard);
}

void AnnotationSelectionTest::cutRemovesAnnotations()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);
    QApplication::clipboard()->clear(QClipboard::Clipboard);

    fixture.annotations.setSelectedAnnotations({ squares.second });
    QVERIFY(fixture.key(Qt::Key_X, Qt::ControlModifier));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.pageAnnotations() == std::vector<PDFObjectReference>{ squares.first });
    QVERIFY(!fixture.annotations.hasSelection());
    QVERIFY(fixture.annotations.canPasteAnnotations());

    // Paste the annotation back using the keyboard
    QVERIFY(fixture.key(Qt::Key_V, Qt::ControlModifier));
    QCOMPARE(fixture.modificationCount, 2);
    QCOMPARE(fixture.pageAnnotations().size(), size_t(2));
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(1));
    QApplication::clipboard()->clear(QClipboard::Clipboard);
}

void AnnotationSelectionTest::resizeHandleScalesSelection()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);
    const QRectF original = fixture.rectangle(squares.first);
    fixture.annotations.setSelectedAnnotations({ squares.first });

    // Bottom right handle of the selection frame (on the screen)
    const QTransform pageToDevice = fixture.pageToDevice();
    const QRectF frame = pageToDevice.mapRect(original);
    const QPoint handle = frame.bottomRight().toPoint();
    const QPoint target = handle + QPoint(40, 20);

    QVERIFY(fixture.press(handle));
    QVERIFY(fixture.move(target));
    QVERIFY(fixture.release(target));
    QCOMPARE(fixture.modificationCount, 1);

    // New rectangle corresponds to the new frame
    bool invertible = false;
    const QTransform deviceToPage = pageToDevice.inverted(&invertible);
    QVERIFY(invertible);
    const QRectF expected = deviceToPage.mapRect(QRectF(frame.topLeft(), QPointF(target))).normalized();
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), expected, 1.0));
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.first));

    // Handle cannot be dragged over the opposite side of the frame
    const QRectF resizedFrame = pageToDevice.mapRect(fixture.rectangle(squares.first));
    const QPoint topLeftHandle = resizedFrame.topLeft().toPoint();
    const QPoint beyond = resizedFrame.bottomRight().toPoint() + QPoint(30, 30);
    QVERIFY(fixture.press(topLeftHandle));
    QVERIFY(fixture.move(beyond));
    QVERIFY(fixture.release(beyond));
    QCOMPARE(fixture.modificationCount, 2);
    const QRectF minimal = fixture.rectangle(squares.first);
    QVERIFY(minimal.isValid());
    QVERIFY(minimal.width() < 10.0);
    QVERIFY(minimal.height() < 10.0);
}

void AnnotationSelectionTest::rotationHandleRotatesLine()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(100, 150), QPointF(200, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ line });

    const QTransform pageToDevice = fixture.pageToDevice();
    const QRectF frame = pageToDevice.mapRect(fixture.rectangle(line));
    const int rotationHandleDistance = PDFWidgetUtils::scaleDPI_x(&fixture.widget, 24);
    const QPointF center = frame.center();
    const QPoint handle = QPointF(center.x(), frame.top() - rotationHandleDistance).toPoint();

    // Rotate by 90 degrees clockwise - the handle above the frame is dragged to the
    // right of the frame (with the same distance from the center)
    const qreal radius = center.y() - handle.y();
    const QPoint target = QPointF(center.x() + radius, center.y()).toPoint();

    QVERIFY(fixture.press(handle));
    QVERIFY(fixture.move(target, Qt::ShiftModifier));
    QVERIFY(fixture.release(target, Qt::ShiftModifier));
    QCOMPARE(fixture.modificationCount, 1);

    // Horizontal line becomes vertical, the left end point goes up
    const std::vector<PDFReal> numbers = fixture.numbers(line, "L");
    QCOMPARE(numbers.size(), size_t(4));
    QVERIFY(std::abs(numbers[0] - numbers[2]) < 0.5);
    QVERIFY(std::abs(numbers[1] - 200.0) < 0.5);
    QVERIFY(std::abs(numbers[3] - 100.0) < 0.5);
    QVERIFY(std::abs(numbers[0] - 150.0) < 0.5);
}

void AnnotationSelectionTest::menuRotationAndFlip()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(10, 20), QPointF(60, 20), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());

    // Nothing selected - nothing happens
    fixture.annotations.rotateSelectedAnnotations(90.0);
    QCOMPARE(fixture.modificationCount, 0);

    // Rotation of the square around its center swaps the size
    fixture.annotations.setSelectedAnnotations({ square });
    fixture.annotations.rotateSelectedAnnotations(90.0);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(square), QRectF(110, 90, 30, 50)));

    // Clockwise rotation of the line: the left end point goes to the top
    fixture.annotations.setSelectedAnnotations({ line });
    fixture.annotations.rotateSelectedAnnotations(90.0);
    std::vector<PDFReal> numbers = fixture.numbers(line, "L");
    QCOMPARE(numbers.size(), size_t(4));
    QVERIFY(std::abs(numbers[0] - 35.0) < 0.01);
    QVERIFY(std::abs(numbers[1] - 45.0) < 0.01);
    QVERIFY(std::abs(numbers[2] - 35.0) < 0.01);
    QVERIFY(std::abs(numbers[3] + 5.0) < 0.01);

    // Vertical flip on the screen mirrors the y coordinates around the center
    fixture.annotations.flipSelectedAnnotations(Qt::Vertical);
    numbers = fixture.numbers(line, "L");
    QVERIFY(std::abs(numbers[1] + 5.0) < 0.01);
    QVERIFY(std::abs(numbers[3] - 45.0) < 0.01);

    // Horizontal flip does not change a vertical line
    fixture.annotations.flipSelectedAnnotations(Qt::Horizontal);
    const std::vector<PDFReal> flipped = fixture.numbers(line, "L");
    for (size_t i = 0; i < 4; ++i)
    {
        QVERIFY(std::abs(flipped[i] - numbers[i]) < 0.01);
    }
    QCOMPARE(fixture.modificationCount, 4);
}

void AnnotationSelectionTest::pointHandleMovesPolygonVertex()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ polygon });

    // The vertex is close to the corner of the selection frame, the point handle wins
    const QPoint handle = fixture.device(QPointF(150, 50));
    const QPoint target = fixture.device(QPointF(220, 90));
    QVERIFY(fixture.press(handle));
    QVERIFY(fixture.move(target));
    QVERIFY(fixture.release(target));
    QCOMPARE(fixture.modificationCount, 1);

    std::vector<PDFReal> vertices = fixture.numbers(polygon, "Vertices");
    QCOMPARE(vertices.size(), size_t(6));
    QVERIFY(std::abs(vertices[0] - 50.0) < 0.01);
    QVERIFY(std::abs(vertices[1] - 50.0) < 0.01);
    QVERIFY(std::abs(vertices[2] - 220.0) < 0.5);
    QVERIFY(std::abs(vertices[3] - 90.0) < 0.5);
    QVERIFY(std::abs(vertices[4] - 100.0) < 0.01);
    QVERIFY(std::abs(vertices[5] - 150.0) < 0.01);
    QVERIFY(fixture.rectangle(polygon).contains(QPointF(219, 90)));
    QVERIFY(fixture.annotations.isAnnotationSelected(polygon));

    // A click on the handle without a movement does not modify the document
    QVERIFY(fixture.click(fixture.device(QPointF(100, 150))));
    QCOMPARE(fixture.modificationCount, 1);

    // Escape cancels the dragging
    QVERIFY(fixture.press(fixture.device(QPointF(100, 150))));
    QVERIFY(fixture.move(fixture.device(QPointF(10, 10))));
    QVERIFY(fixture.key(Qt::Key_Escape));
    fixture.release(fixture.device(QPointF(10, 10)));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.annotations.isAnnotationSelected(polygon));
}

void AnnotationSelectionTest::shiftConstrainsPointDirection()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(100, 150), QPointF(200, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ line });

    // The end point is dragged roughly in the diagonal direction, Shift makes it exact
    const QPoint handle = fixture.device(QPointF(200, 150));
    const QPoint target = fixture.device(QPointF(180, 226));
    QVERIFY(fixture.press(handle));
    QVERIFY(fixture.move(target, Qt::ShiftModifier));
    QVERIFY(fixture.release(target, Qt::ShiftModifier));
    QCOMPARE(fixture.modificationCount, 1);

    const std::vector<PDFReal> numbers = fixture.numbers(line, "L");
    QCOMPARE(numbers.size(), size_t(4));
    QVERIFY(std::abs(numbers[0] - 100.0) < 0.01);
    QVERIFY(std::abs(numbers[1] - 150.0) < 0.01);
    QVERIFY(std::abs((numbers[2] - 100.0) - (numbers[3] - 150.0)) < 0.01);
    QVERIFY(std::abs(numbers[2] - 178.0) < 1.5);
}

void AnnotationSelectionTest::insertAndRemovePoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(100, 250), QPointF(200, 250), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());

    // Triangle cannot lose a point
    QVERIFY(!fixture.annotations.removeAnnotationPoint(polygon, 0));
    QCOMPARE(fixture.modificationCount, 0);

    // Insert a point into the last segment (from the last point to the first one)
    QVERIFY(fixture.annotations.insertAnnotationPoint(polygon, 2, QPointF(60, 110)));
    QVERIFY(!fixture.annotations.insertAnnotationPoint(polygon, 4, QPointF(60, 110)));
    QCOMPARE(fixture.modificationCount, 1);
    std::vector<PDFReal> vertices = fixture.numbers(polygon, "Vertices");
    QCOMPARE(vertices.size(), size_t(8));
    QVERIFY(std::abs(vertices[6] - 60.0) < 0.01);
    QVERIFY(std::abs(vertices[7] - 110.0) < 0.01);

    QVERIFY(fixture.annotations.removeAnnotationPoint(polygon, 1));
    QVERIFY(!fixture.annotations.removeAnnotationPoint(polygon, 7));
    QCOMPARE(fixture.modificationCount, 2);
    vertices = fixture.numbers(polygon, "Vertices");
    QCOMPARE(vertices.size(), size_t(6));
    QVERIFY(std::abs(vertices[2] - 100.0) < 0.01);
    QVERIFY(std::abs(vertices[3] - 150.0) < 0.01);

    QVERIFY(fixture.annotations.moveAnnotationPoint(polygon, 0, QPointF(40, 45)));
    QVERIFY(!fixture.annotations.moveAnnotationPoint(polygon, 3, QPointF(40, 45)));
    vertices = fixture.numbers(polygon, "Vertices");
    QVERIFY(std::abs(vertices[0] - 40.0) < 0.01);
    QVERIFY(std::abs(vertices[1] - 45.0) < 0.01);
    QCOMPARE(fixture.modificationCount, 3);

    // Number of points of a line is fixed
    QVERIFY(!fixture.annotations.insertAnnotationPoint(line, 0, QPointF(150, 260)));
    QVERIFY(!fixture.annotations.removeAnnotationPoint(line, 0));
    QVERIFY(fixture.annotations.moveAnnotationPoint(line, 1, QPointF(250, 280)));
    QCOMPARE(fixture.modificationCount, 4);
}

void AnnotationSelectionTest::pointHandlesNeedSingleSelection()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(200, 200, 40, 40), 1.0, Qt::green, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ polygon, square });

    // With two selected annotations, there are no point handles - the press in the
    // top vertex of the triangle (far from the handles of the common frame) is
    // an ordinary press on the annotation
    const QPoint vertex = fixture.device(QPointF(100, 149));
    QVERIFY(fixture.press(vertex));
    fixture.release(vertex);
    QCOMPARE(fixture.modificationCount, 0);
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));
    QCOMPARE(fixture.numbers(polygon, "Vertices").size(), size_t(6));
}

void AnnotationSelectionTest::lockedAnnotationIsNotTransformed()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("F");
    factory << PDFInteger(PDFAnnotation::Locked);
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(square, factory.takeObject());
    SelectionFixture fixture(builder.build());

    // Locked annotation can be selected (and copied), but not moved or deleted
    QVERIFY(fixture.click(fixture.device(QPointF(80, 70))));
    QVERIFY(fixture.annotations.isAnnotationSelected(square));
    const PDFAnnotationManager::PageAnnotations& pageAnnotations = fixture.annotations.getPageAnnotations(0);
    QCOMPARE(pageAnnotations.annotations.size(), size_t(1));
    QVERIFY(fixture.annotations.isAnnotationSelectable(pageAnnotations.annotations.front()));
    QVERIFY(!fixture.annotations.canTransformAnnotation(pageAnnotations.annotations.front()));
    QVERIFY(!fixture.annotations.canDeleteAnnotation(pageAnnotations.annotations.front()));

    fixture.annotations.rotateSelectedAnnotations(90.0);
    QVERIFY(!fixture.key(Qt::Key_Right));
    QVERIFY(fixture.key(Qt::Key_Delete));
    QCOMPARE(fixture.modificationCount, 0);
    QCOMPARE(fixture.pageAnnotations().size(), size_t(1));

    // There is no selection frame, so a press at the corner of the annotation
    // selects it instead of starting the resize
    const QPoint corner = fixture.pageToDevice().mapRect(fixture.rectangle(square)).bottomRight().toPoint() - QPoint(2, 2);
    QVERIFY(fixture.press(corner));
    QVERIFY(fixture.annotations.isAnnotationSelected(square));
    fixture.move(corner + QPoint(30, 30));
    fixture.release(corner + QPoint(30, 30));
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationSelectionTest::dropFromAnotherDocumentInsertsCopy()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);

    // Another document with a stamp
    PDFDocumentBuilder otherBuilder;
    const PDFObjectReference otherPage = otherBuilder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference stamp = otherBuilder.createAnnotationStamp(otherPage, QRectF(20, 20, 120, 40), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFDocument other = otherBuilder.build();

    QMimeData mimeData;
    QVERIFY(!fixture.annotations.canAcceptAnnotationDrag(&mimeData));
    mimeData.setData(PDFAnnotationManipulator::getMimeType(), PDFAnnotationManipulator::serializeAnnotations(&other, { stamp }));
    QVERIFY(fixture.annotations.canAcceptAnnotationDrag(&mimeData));

    const QPoint dropPosition = fixture.device(QPointF(200, 250));
    QVERIFY(fixture.annotations.handleAnnotationDrop(&mimeData, dropPosition, Qt::MoveAction));
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(fixture.pageAnnotations().size(), size_t(3));

    const std::vector<PDFObjectReference> selection = fixture.annotations.getSelectedAnnotations();
    QCOMPARE(selection.size(), size_t(1));

    PDFDocumentDataLoaderDecorator otherLoader(&other);
    const QRectF stampRectangle = otherLoader.readRectangle(other.getDictionaryFromObject(other.getObjectByReference(stamp))->get("Rect"), QRectF()).normalized();
    QRectF expected = stampRectangle;
    expected.moveCenter(QPointF(200, 250));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(selection.front()), expected));

    // Drop outside of the pages is refused
    QVERIFY(!fixture.annotations.handleAnnotationDrop(&mimeData, QPoint(-100, -100), Qt::CopyAction));
    QCOMPARE(fixture.modificationCount, 1);
}

void AnnotationSelectionTest::selectionSurvivesDocumentModification()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);
    fixture.annotations.setSelectedAnnotations({ squares.first, squares.second });

    // Modification of the annotations keeps the selection
    fixture.annotations.translateSelectedAnnotations(QPointF(5.0, 5.0));
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.second), QRectF(155, 155, 40, 40)));

    // Annotation removed by somebody else disappears from the selection
    QSignalSpy selectionChanges(&fixture.annotations, &PDFWidgetAnnotationManager::selectionChanged);
    PDFDocumentModifier modifier(fixture.document.data());
    modifier.markAnnotationsChanged();
    modifier.getBuilder()->removeAnnotation(squares.page, squares.second);
    QVERIFY(modifier.finalize());
    fixture.document = modifier.getDocument();
    fixture.apply(PDFModifiedDocument(fixture.document, nullptr, modifier.getFlags()));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ squares.first });
    QCOMPARE(selectionChanges.count(), 1);

    // New document resets the selection
    fixture.apply(PDFModifiedDocument(fixture.document, nullptr));
    QVERIFY(!fixture.annotations.hasSelection());
}

void AnnotationSelectionTest::selectionApiIgnoresUnselectableAnnotations()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference link = builder.createAnnotationLink(page, QRectF(100, 100, 50, 20), QString("https://example.com"), LinkHighlightMode::Invert);
    SelectionFixture fixture(builder.build());

    PDFDocumentDataLoaderDecorator loader(fixture.document.data());
    const PDFObjectReference popup = loader.readReferenceFromDictionary(fixture.document->getDictionaryFromObject(fixture.document->getObjectByReference(note)), "Popup");
    QVERIFY(popup.isValid());

    // Links, popups and invalid references cannot be selected, duplicates are ignored
    fixture.annotations.setSelectedAnnotations({ link, popup, PDFObjectReference(), note, note });
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ note });

    fixture.annotations.selectAnnotation(link, false);
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ note });

    fixture.annotations.deselectAnnotation(note);
    QVERIFY(!fixture.annotations.hasSelection());

    // Select all selects only the note
    fixture.annotations.selectAllAnnotations();
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ note });
}

QTEST_MAIN(AnnotationSelectionTest)

#include "tst_annotationselectiontest.moc"
