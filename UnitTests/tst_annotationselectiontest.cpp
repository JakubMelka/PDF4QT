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
#include "pdfannotationgeometrydialog.h"
#include "pdfwidgettool.h"
#include "pdfwidgetutils.h"
#include "pdfcms.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QAction>
#include <QClipboard>
#include <QMimeData>
#include <QMenu>
#include <QTimer>

#include <map>
#include <functional>

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
    void pointEditWithoutPageEntry();
    void selectingAnnotationOverLinkDoesNotActivateLink();
    void hiddenAnnotationAndReplyHaveMenu();
    void flipKeepsSideOfLeaderLines();
    void handleHasPrecedenceOverModifiers();
    void controlPressKeepsSelectionForDragging();
    void flipAndNudgeAfterViewRotation();
    void cutPreservesReplies();
    void cutSkipsLockedAnnotations();
    void measurementFollowsGeometry();
    void pointContextMenuHasPrecedence();
    void handlesFollowCapabilities();
    void rotationHandleOfSquareSnapsToRightAngle();
    void frameOfNoRotateAnnotation();
    void nudgeOnDifferentlyRotatedPages();
    void thinShapeDoesNotBlockAnnotationsInside();
    void altClickCyclesOverlappingAnnotations();
    void textBoxOfCalloutIsResizedAlone();
    void calloutLineCanBeAddedAndRemoved();
    void markedLineEndsAndParts();
    void pointSnapsToOtherAnnotation();
    void alignAndDistribute();
    void keyboardEditsPoints();
    void copyToPagesAndPasteInPlace();
    void commonProperties();
    void annotationRectangleTransformsGeometry();
    void geometryDialog();
    void selectionIsDrawn();

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

    std::vector<PDFObjectReference> pageAnnotations(size_t pageIndex = 0) const
    {
        return document->getCatalog()->getPage(pageIndex)->getAnnotations();
    }

    PDFObject entry(PDFObjectReference annotation, const char* key) const
    {
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation));
        return dictionary ? dictionary->get(key) : PDFObject();
    }

    QString contents(PDFObjectReference annotation) const
    {
        const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(&document->getStorage(), annotation);
        return parsedAnnotation ? parsedAnnotation->getContents() : QString();
    }

    /// Rectangle, in which the annotation is displayed (device coordinates). It respects
    /// the flags of the annotation the same way, as the renderer does.
    QRectF displayedRectangle(PDFObjectReference annotation, size_t pageIndex = 0) const
    {
        const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(&document->getStorage(), annotation);
        QRectF annotationRectangle = parsedAnnotation->getRectangle();
        const QTransform matrix = annotations.prepareTransformations(pageToDevice(), const_cast<PDFWidget*>(&widget), parsedAnnotation->getEffectiveFlags(),
                                                                     document->getCatalog()->getPage(pageIndex), annotationRectangle);
        return matrix.mapRect(annotationRectangle).normalized();
    }

    /// Calls the function, which shows a context menu, and returns the actions of the
    /// menu (text and enabled state). The menu is closed at once. If no menu is shown,
    /// then empty map is returned.
    static std::map<QString, bool> menuActions(const std::function<void()>& showMenu)
    {
        std::map<QString, bool> result;

        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&result]()
        {
            if (QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
            {
                for (const QAction* action : menu->actions())
                {
                    if (!action->isSeparator())
                    {
                        result[action->text()] = action->isEnabled();
                    }
                }
                menu->close();
            }
        });
        timer.start(1);
        showMenu();
        timer.stop();

        return result;
    }

    static void setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem(key);
        factory << value;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(reference, factory.takeObject());
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

void AnnotationSelectionTest::pointEditWithoutPageEntry()
{
    // The entry P of an annotation is optional. The appearance must follow
    // the geometry also for the annotations, which do not have it.
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture::setEntry(builder, polygon, "P", PDFObject());
    SelectionFixture fixture(builder.build());

    const PDFObject oldAppearance = fixture.entry(polygon, "AP");
    QVERIFY(fixture.annotations.moveAnnotationPoint(polygon, 2, QPointF(180, 150)));
    QVERIFY(fixture.entry(polygon, "AP") != oldAppearance);

    const PDFObject movedAppearance = fixture.entry(polygon, "AP");
    fixture.annotations.setSelectedAnnotations({ polygon });
    fixture.annotations.rotateSelectedAnnotations(90.0);
    QVERIFY(fixture.entry(polygon, "AP") != movedAppearance);
}

void AnnotationSelectionTest::selectingAnnotationOverLinkDoesNotActivateLink()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    builder.createAnnotationLink(page, QRectF(50, 50, 60, 40), "https://example.com", LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(150, 150, 60, 40), "https://example.org", LinkHighlightMode::Invert);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    int actionCount = 0;
    QObject::connect(&fixture.annotations, &PDFWidgetAnnotationManager::actionTriggered, [&actionCount]() { ++actionCount; });

    // The click selects the annotation, which lies over the link, it must not navigate
    fixture.move(fixture.device(QPointF(80, 70)), Qt::NoModifier, Qt::NoButton);
    QVERIFY(fixture.click(fixture.device(QPointF(80, 70))));
    QVERIFY(fixture.annotations.isAnnotationSelected(square));
    QCOMPARE(actionCount, 0);

    // The same for a click with a modifier
    QVERIFY(fixture.click(fixture.device(QPointF(80, 70)), Qt::ControlModifier));
    QVERIFY(!fixture.annotations.isAnnotationSelected(square));
    QCOMPARE(actionCount, 0);

    // The link itself is activated by a click
    fixture.move(fixture.device(QPointF(180, 170)), Qt::NoModifier, Qt::NoButton);
    QVERIFY(fixture.click(fixture.device(QPointF(180, 170))));
    QCOMPARE(actionCount, 1);

    // The button pressed over the link and released elsewhere does nothing,
    // and neither does the button pressed elsewhere and released over the link
    QVERIFY(fixture.press(fixture.device(QPointF(180, 170))));
    fixture.move(fixture.device(QPointF(250, 20)));
    fixture.release(fixture.device(QPointF(250, 20)));
    QCOMPARE(actionCount, 1);

    fixture.press(fixture.device(QPointF(250, 20)));
    fixture.move(fixture.device(QPointF(180, 170)));
    fixture.release(fixture.device(QPointF(180, 170)));
    QCOMPARE(actionCount, 1);
}

void AnnotationSelectionTest::hiddenAnnotationAndReplyHaveMenu()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference hidden = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Hidden", "Subject", "Contents");
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(150, 150, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    const PDFObjectReference reply = builder.createAnnotationText(page, QRectF(150, 150, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Reply", false);
    const PDFObjectReference lockedReply = builder.createAnnotationText(page, QRectF(150, 150, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Locked", false);
    SelectionFixture::setEntry(builder, hidden, "F", PDFObject::createInteger(PDFAnnotation::Hidden));
    SelectionFixture::setEntry(builder, reply, "IRT", PDFObject::createReference(note));
    SelectionFixture::setEntry(builder, lockedReply, "IRT", PDFObject::createReference(note));
    SelectionFixture::setEntry(builder, lockedReply, "F", PDFObject::createInteger(PDFAnnotation::Locked));
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ note });

    // The sidebar lists hidden annotations and replies. They cannot be selected on the page,
    // but the user must be able to manage them - for example to make the annotation visible again.
    std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(hidden, page, QPoint(100, 100)); });
    QVERIFY(actions.count("Edit...") && actions["Edit..."]);
    QVERIFY(actions.count("Delete") && actions["Delete"]);
    QVERIFY(!actions.count("Rotate 90° Clockwise"));
    QVERIFY(!fixture.annotations.hasSelection());

    actions = SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(reply, page, QPoint(100, 100)); });
    QVERIFY(actions.count("Edit...") && actions["Edit..."]);
    QVERIFY(actions.count("Delete") && actions["Delete"]);
    QVERIFY(!actions.count("Show Popup Window"));

    actions = SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(lockedReply, page, QPoint(100, 100)); });
    QVERIFY(actions.count("Delete") && !actions["Delete"]);

    // Annotation, which can be selected, gets the menu of the selection
    actions = SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(note, page, QPoint(100, 100)); });
    QVERIFY(actions.count("Copy") && actions["Copy"]);
    QVERIFY(fixture.annotations.isAnnotationSelected(note));

    // Invalid annotation or page
    QVERIFY(SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(PDFObjectReference(), page, QPoint(100, 100)); }).empty());
    QVERIFY(SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(note, PDFObjectReference(), QPoint(100, 100)); }).empty());
}

void AnnotationSelectionTest::flipKeepsSideOfLeaderLines()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None, 20.0, 0.0, 0.0, false, false);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ line });

    // Mirroring of the left and the right side must not move the leader lines vertically
    const QRectF original = fixture.rectangle(line);
    fixture.annotations.flipSelectedAnnotations(Qt::Horizontal);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(line), original, 0.01));

    // Mirroring of the top and the bottom moves them below the line
    fixture.annotations.flipSelectedAnnotations(Qt::Vertical);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(line), original, 0.01));
    QVERIFY(fixture.numbers(line, "L")[1] > 165.0);
}

void AnnotationSelectionTest::handleHasPrecedenceOverModifiers()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(100, 150), QPointF(200, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 30, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    // Shift is held before the point is pressed - the point is dragged with the
    // constrained direction, the annotation is not removed from the selection
    fixture.annotations.setSelectedAnnotations({ line });
    QVERIFY(fixture.press(fixture.device(QPointF(200, 150)), Qt::ShiftModifier));
    QVERIFY(fixture.move(fixture.device(QPointF(180, 226)), Qt::ShiftModifier));
    QVERIFY(fixture.release(fixture.device(QPointF(180, 226)), Qt::ShiftModifier));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.annotations.isAnnotationSelected(line));

    const std::vector<PDFReal> points = fixture.numbers(line, "L");
    QCOMPARE(points.size(), size_t(4));
    QVERIFY(std::abs((points[2] - points[0]) - (points[3] - points[1])) < 0.01);
    QVERIFY(points[2] - points[0] > 70.0);

    // Shift is held before the corner of the frame is pressed - the aspect ratio is kept
    fixture.annotations.setSelectedAnnotations({ square });
    const QRectF frame = fixture.pageToDevice().mapRect(fixture.rectangle(square)).normalized();
    QVERIFY(fixture.press(frame.bottomRight().toPoint(), Qt::ShiftModifier));
    QVERIFY(fixture.move(frame.bottomRight().toPoint() + QPoint(60, 5), Qt::ShiftModifier));
    QVERIFY(fixture.release(frame.bottomRight().toPoint() + QPoint(60, 5), Qt::ShiftModifier));
    QCOMPARE(fixture.modificationCount, 2);
    QVERIFY(fixture.annotations.isAnnotationSelected(square));

    const QRectF resized = fixture.rectangle(square);
    QVERIFY(resized.width() > 70.0);
    QVERIFY(std::abs(resized.width() / resized.height() - 1.5) < 0.02);
}

void AnnotationSelectionTest::controlPressKeepsSelectionForDragging()
{
    TwoSquares squares;
    SelectionFixture fixture(squares.document);
    fixture.annotations.setSelectedAnnotations({ squares.first });

    // Ctrl + drag copies the selection, so the pressed annotation must stay
    // selected. It is removed from the selection, if it is not dragged.
    QVERIFY(fixture.press(fixture.device(QPointF(80, 70)), Qt::ControlModifier));
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.first));
    QVERIFY(fixture.release(fixture.device(QPointF(80, 70)), Qt::ControlModifier));
    QVERIFY(!fixture.annotations.isAnnotationSelected(squares.first));

    // Annotation, which is not selected, is added to the selection at once
    fixture.annotations.setSelectedAnnotations({ squares.first });
    QVERIFY(fixture.press(fixture.device(QPointF(170, 170)), Qt::ShiftModifier));
    QVERIFY(fixture.annotations.isAnnotationSelected(squares.second));
    fixture.release(fixture.device(QPointF(170, 170)), Qt::ShiftModifier);
    QVERIFY(fixture.annotations.getSelectedAnnotations() == (std::vector<PDFObjectReference>{ squares.first, squares.second }));
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationSelectionTest::flipAndNudgeAfterViewRotation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(100, 150), QPointF(200, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());

    // The view is rotated by the user, so the line is vertical on the screen
    fixture.widget.getDrawWidgetProxy()->performOperation(PDFDrawWidgetProxy::RotateRight);
    fixture.annotations.setSelectedAnnotations({ line });

    // Returns the end of the line with the arrow and the end without it (device coordinates).
    // Mirroring can swap the end points of the line together with the line endings.
    auto getDevicePoints = [&fixture, line]()
    {
        PDFDocumentDataLoaderDecorator loader(fixture.document.data());
        const std::vector<QByteArray> lineEndings = loader.readNameArrayFromDictionary(fixture.document->getDictionaryFromObject(fixture.document->getObjectByReference(line)), "LE");
        const std::vector<PDFReal> points = fixture.numbers(line, "L");
        const QPointF start = fixture.pageToDevice().map(QPointF(points[0], points[1]));
        const QPointF end = fixture.pageToDevice().map(QPointF(points[2], points[3]));
        return (lineEndings.front() == "OpenArrow") ? std::make_pair(start, end) : std::make_pair(end, start);
    };

    const auto original = getDevicePoints();
    QVERIFY(std::abs(original.first.x() - original.second.x()) < 0.5);

    // Mirroring of the left and the right side does not change a vertical line
    fixture.annotations.flipSelectedAnnotations(Qt::Horizontal);
    auto current = getDevicePoints();
    QVERIFY(QLineF(original.first, current.first).length() < 0.5);
    QVERIFY(QLineF(original.second, current.second).length() < 0.5);

    // Mirroring of the top and the bottom swaps its end points
    fixture.annotations.flipSelectedAnnotations(Qt::Vertical);
    current = getDevicePoints();
    QVERIFY(QLineF(original.first, current.second).length() < 0.5);
    QVERIFY(QLineF(original.second, current.first).length() < 0.5);

    // Arrows move the annotation in the direction given on the screen
    const QPointF beforeNudge = getDevicePoints().first;
    QVERIFY(fixture.key(Qt::Key_Right, Qt::ShiftModifier));
    const QPointF afterNudge = getDevicePoints().first;
    QVERIFY(afterNudge.x() > beforeNudge.x() + 1.0);
    QVERIFY(std::abs(afterNudge.y() - beforeNudge.y()) < 0.5);
}

void AnnotationSelectionTest::cutPreservesReplies()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference parent = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Parent", "Subject", "Parent text");
    const PDFObjectReference reply = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Reply", "Subject", "Important reply");
    const PDFObjectReference nestedReply = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Reply", "Subject", "Reply to the reply");
    SelectionFixture::setEntry(builder, reply, "IRT", PDFObject::createReference(parent));
    SelectionFixture::setEntry(builder, nestedReply, "IRT", PDFObject::createReference(reply));
    SelectionFixture fixture(builder.build());

    fixture.annotations.setSelectedAnnotations({ parent });
    fixture.annotations.cutSelectedAnnotations();
    QVERIFY(fixture.pageAnnotations().empty());
    QCOMPARE(fixture.modificationCount, 1);

    // The whole discussion is pasted, the replies are linked to the pasted annotations
    fixture.annotations.pasteAnnotations(std::nullopt);
    QCOMPARE(fixture.modificationCount, 2);
    const std::vector<PDFObjectReference> pasted = fixture.pageAnnotations();
    QCOMPARE(pasted.size(), size_t(3));
    QCOMPARE(fixture.contents(pasted[0]), QString("Parent text"));
    QCOMPARE(fixture.contents(pasted[1]), QString("Important reply"));
    QCOMPARE(fixture.contents(pasted[2]), QString("Reply to the reply"));
    QCOMPARE(fixture.entry(pasted[1], "IRT").getReference(), pasted[0]);
    QCOMPARE(fixture.entry(pasted[2], "IRT").getReference(), pasted[1]);

    // Only the annotation is selected, the replies are not selectable
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ pasted[0] });
}

void AnnotationSelectionTest::cutSkipsLockedAnnotations()
{
    TwoSquares squares;
    PDFDocumentBuilder builder(&squares.document);
    SelectionFixture::setEntry(builder, squares.second, "F", PDFObject::createInteger(PDFAnnotation::Locked));
    SelectionFixture fixture(builder.build());

    // Locked annotation cannot be deleted, so it must not get into the clipboard -
    // otherwise pasting would create its copy
    fixture.annotations.setSelectedAnnotations({ squares.first, squares.second });
    fixture.annotations.cutSelectedAnnotations();
    QVERIFY(fixture.pageAnnotations() == std::vector<PDFObjectReference>{ squares.second });

    fixture.annotations.pasteAnnotations(std::nullopt);
    QCOMPARE(fixture.pageAnnotations().size(), size_t(2));
    QCOMPARE(fixture.contents(fixture.pageAnnotations().back()), QString("First contents"));

    // Nothing can be deleted, so the clipboard is not touched
    QApplication::clipboard()->clear();
    fixture.annotations.setSelectedAnnotations({ squares.second });
    fixture.annotations.cutSelectedAnnotations();
    QVERIFY(!fixture.annotations.canPasteAnnotations());
    QCOMPARE(fixture.pageAnnotations().size(), size_t(2));
}

void AnnotationSelectionTest::measurementFollowsGeometry()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "100 pt", AnnotationLineEnding::None, AnnotationLineEnding::None, 0.0, 0.0, 0.0, true, true);
    SelectionFixture::setEntry(builder, line, "IT", PDFObject::createName("LineDimension"));
    SelectionFixture fixture(builder.build());

    // The end point of the dimension line is dragged, the displayed value must follow
    fixture.annotations.setSelectedAnnotations({ line });
    QVERIFY(fixture.press(fixture.device(QPointF(150, 150))));
    QVERIFY(fixture.move(fixture.device(QPointF(250, 150))));
    QVERIFY(fixture.release(fixture.device(QPointF(250, 150))));

    const std::vector<PDFReal> points = fixture.numbers(line, "L");
    const PDFReal length = std::hypot(points[2] - points[0], points[3] - points[1]);
    QVERIFY(std::abs(length - 200.0) < 1.0);
    QCOMPARE(fixture.contents(line), QString("%1 pt").arg(qRound(length)));
}

void AnnotationSelectionTest::pointContextMenuHasPrecedence()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(150, 150), QPointF(50, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(130, 130, 60, 60), 1.0, Qt::green, Qt::black, "Overlap", "Subject", "Overlap");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ polygon });

    // The vertex of the selected polygon is covered by another annotation. The right
    // click must offer the operations of the vertex and keep the selection.
    std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(150, 150)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Delete Point"));
    QVERIFY(fixture.annotations.isAnnotationSelected(polygon));

    // The same for a segment of the polygon
    actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(150, 140)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Insert Point"));
    QVERIFY(fixture.annotations.isAnnotationSelected(polygon));

    // Elsewhere, the right click selects the annotation under the cursor
    actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(180, 180)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(!actions.count("Insert Point"));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ square });
}

void AnnotationSelectionTest::handlesFollowCapabilities()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 200, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(150, 200, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    auto getMenuActions = [&fixture](PDFObjectReference annotation)
    {
        return SelectionFixture::menuActions([&]() { fixture.annotations.showAnnotationMenu(annotation, fixture.document->getCatalog()->getPage(0)->getPageReference(), QPoint(100, 100)); });
    };

    // A sticky note can only be moved
    std::map<QString, bool> actions = getMenuActions(note);
    QVERIFY(actions.count("Rotate 90° Clockwise") && !actions["Rotate 90° Clockwise"]);
    QVERIFY(actions.count("Flip Horizontal") && !actions["Flip Horizontal"]);
    QVERIFY(actions.count("This annotation can only be moved, its size is fixed."));

    // A text box can be resized, but it cannot be rotated (the text would not rotate)
    actions = getMenuActions(freeText);
    QVERIFY(!actions["Rotate 90° Clockwise"]);
    QVERIFY(!actions["Flip Horizontal"]);
    QVERIFY(actions.count("This annotation can be moved and resized, it cannot be rotated."));

    // A square can be rotated by the right angle, mirroring does not change it
    actions = getMenuActions(square);
    QVERIFY(actions["Rotate 90° Clockwise"]);
    QVERIFY(!actions["Flip Horizontal"]);
    QVERIFY(actions.count("This annotation can be rotated only in steps of 90°."));

    // The layout of a group can be always transformed
    fixture.annotations.setSelectedAnnotations({ note, freeText });
    actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(160, 210)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions["Rotate 90° Clockwise"]);
    QVERIFY(actions["Flip Horizontal"]);
    QVERIFY(actions.count("Selected annotations: 2, pages: 1"));

    // The text box has no rotation handle - a press at its position hits an empty
    // area, so it is not accepted and the selection is cleared
    fixture.annotations.setSelectedAnnotations({ freeText });
    const QRectF textFrame = fixture.pageToDevice().mapRect(fixture.rectangle(freeText)).normalized();
    const QPoint rotationHandle = QPointF(textFrame.center().x(), textFrame.top() - PDFWidgetUtils::scaleDPI_x(&fixture.widget, 24)).toPoint();
    QVERIFY(!fixture.press(rotationHandle));
    fixture.release(rotationHandle);
    QVERIFY(!fixture.annotations.hasSelection());

    // It has the resize handles
    fixture.annotations.setSelectedAnnotations({ freeText });
    QVERIFY(fixture.press(textFrame.bottomRight().toPoint()));
    QVERIFY(fixture.move(textFrame.bottomRight().toPoint() + QPoint(20, 20)));
    QVERIFY(fixture.release(textFrame.bottomRight().toPoint() + QPoint(20, 20)));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(fixture.rectangle(freeText).width() > 105.0);

    // The note has no handles at all - the rotation handle is missing...
    fixture.annotations.setSelectedAnnotations({ note });
    const QRectF noteFrame = fixture.displayedRectangle(note);
    const QPoint noteRotationHandle = QPointF(noteFrame.center().x(), noteFrame.top() - PDFWidgetUtils::scaleDPI_x(&fixture.widget, 24)).toPoint();
    QVERIFY(!fixture.press(noteRotationHandle));
    fixture.release(noteRotationHandle);
    QVERIFY(!fixture.annotations.hasSelection());

    // ...and a press just outside of its corner (where the resize handle would be) too
    fixture.annotations.setSelectedAnnotations({ note });
    const QPoint noteCorner = noteFrame.bottomRight().toPoint() + QPoint(3, 3);
    QVERIFY(!fixture.press(noteCorner));
    fixture.release(noteCorner);
    QCOMPARE(fixture.modificationCount, 1);
}

void AnnotationSelectionTest::rotationHandleOfSquareSnapsToRightAngle()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 80, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ square });

    const QRectF frame = fixture.pageToDevice().mapRect(fixture.rectangle(square)).normalized();
    const QPointF center = frame.center();
    const QPointF handle(center.x(), frame.top() - PDFWidgetUtils::scaleDPI_x(&fixture.widget, 24));
    const qreal radius = center.y() - handle.y();

    auto rotatedHandle = [&](qreal degrees)
    {
        const qreal radians = qDegreesToRadians(degrees);
        return QPointF(center.x() + radius * std::sin(radians), center.y() - radius * std::cos(radians)).toPoint();
    };

    // The shape of a square cannot be rotated by an arbitrary angle, so the handle
    // snaps to the right angles - the preview shows exactly, what is going to happen
    QVERIFY(fixture.press(handle.toPoint()));
    QVERIFY(fixture.move(rotatedHandle(30.0)));
    QVERIFY(fixture.release(rotatedHandle(30.0)));
    QCOMPARE(fixture.modificationCount, 0);

    QVERIFY(fixture.press(handle.toPoint()));
    QVERIFY(fixture.move(rotatedHandle(80.0)));
    QVERIFY(fixture.release(rotatedHandle(80.0)));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(square), QRectF(120, 80, 40, 80)));
}

void AnnotationSelectionTest::frameOfNoRotateAnnotation()
{
    // The page is rotated, but the annotation is not rotated with it (flag NoRotate),
    // so it is not displayed by the matrix of the page
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(60, 100, 80, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture::setEntry(builder, page, "Rotate", PDFObject::createInteger(90));
    SelectionFixture::setEntry(builder, square, "F", PDFObject::createInteger(PDFAnnotation::NoRotate));
    SelectionFixture fixture(builder.build());

    const QRectF displayed = fixture.displayedRectangle(square);
    const QRectF rotated = fixture.pageToDevice().mapRect(fixture.rectangle(square)).normalized();
    QVERIFY(!SelectionFixture::fuzzyCompare(displayed, rotated, 2.0));

    // The annotation is selected, where it is displayed
    QPoint insideDisplayed;
    for (const QPointF& candidate : { QPointF(displayed.right() - 3.0, displayed.center().y()), QPointF(displayed.left() + 3.0, displayed.center().y()),
                                      QPointF(displayed.center().x(), displayed.top() + 3.0), QPointF(displayed.center().x(), displayed.bottom() - 3.0) })
    {
        if (!rotated.contains(candidate))
        {
            insideDisplayed = candidate.toPoint();
            break;
        }
    }
    QVERIFY(!insideDisplayed.isNull());
    QVERIFY(fixture.click(insideDisplayed));
    QVERIFY(fixture.annotations.isAnnotationSelected(square));

    // The handles are at the corners of the displayed annotation and they
    // resize it exactly the way, the user sees it
    QVERIFY(fixture.press(displayed.bottomRight().toPoint()));
    QVERIFY(fixture.move(displayed.bottomRight().toPoint() + QPoint(30, 20)));
    QVERIFY(fixture.release(displayed.bottomRight().toPoint() + QPoint(30, 20)));
    QCOMPARE(fixture.modificationCount, 1);

    QRectF expected = displayed;
    expected.setBottomRight(displayed.bottomRight() + QPointF(30, 20));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.displayedRectangle(square), expected, 2.0));

    // The corner, to which the displayed annotation is anchored, is moved
    const QRectF resized = fixture.displayedRectangle(square);
    QVERIFY(fixture.press(resized.topLeft().toPoint()));
    QVERIFY(fixture.move(resized.topLeft().toPoint() - QPoint(20, 10)));
    QVERIFY(fixture.release(resized.topLeft().toPoint() - QPoint(20, 10)));
    QCOMPARE(fixture.modificationCount, 2);

    expected = resized;
    expected.setTopLeft(resized.topLeft() - QPointF(20, 10));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.displayedRectangle(square), expected, 2.0));
}

void AnnotationSelectionTest::nudgeOnDifferentlyRotatedPages()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference first = builder.createAnnotationSquare(page1, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference second = builder.createAnnotationSquare(page2, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture::setEntry(builder, page2, "Rotate", PDFObject::createInteger(90));
    SelectionFixture fixture(builder.build());

    fixture.annotations.setSelectedAnnotations({ first, second });
    QCOMPARE(fixture.annotations.getSelectedAnnotations().size(), size_t(2));

    // The right arrow moves both annotations to the right on the screen. The second page
    // is rotated, so it is another direction in its coordinate system (the top of the page
    // is on the right side of the screen, so the annotation moves up in the page coordinates).
    // Both pages are modified in a single step.
    QVERIFY(fixture.key(Qt::Key_Right, Qt::ShiftModifier));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(first), QRectF(60, 50, 60, 40), 0.01));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(second), QRectF(50, 60, 60, 40), 0.01));
}

void AnnotationSelectionTest::thinShapeDoesNotBlockAnnotationsInside()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(120, 120, 40, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(250, 50), QPointF(250, 250), QPointF(50, 250) }, 1.0, QColor(), Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(60, 60), QPointF(240, 240), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());

    // The polygon is not filled and it is over the square, the diagonal line is over both.
    // A click inside the square selects the square, not the rectangle of the polygon (of the line).
    QVERIFY(fixture.click(fixture.device(QPointF(150, 130))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ square });

    // A click on the outline of the polygon selects the polygon, a click on the line selects the line
    QVERIFY(fixture.click(fixture.device(QPointF(250, 200))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ polygon });
    QVERIFY(fixture.click(fixture.device(QPointF(200, 200))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ line });

    // The rectangle of a thin shape still selects it, if nothing else is there
    fixture.annotations.clearSelection();
    QVERIFY(fixture.click(fixture.device(QPointF(80, 220))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ line });

    // The rectangle of the selected annotation is the target for moving it
    QVERIFY(fixture.click(fixture.device(QPointF(150, 130))));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ line });
}

void AnnotationSelectionTest::altClickCyclesOverlappingAnnotations()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference bottom = builder.createAnnotationSquare(page, QRectF(50, 50, 100, 100), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference middle = builder.createAnnotationSquare(page, QRectF(60, 60, 100, 100), 1.0, Qt::green, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference top = builder.createAnnotationSquare(page, QRectF(70, 70, 100, 100), 1.0, Qt::red, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    const QPoint position = fixture.device(QPointF(100, 100));

    QVERIFY(fixture.click(position));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ top });

    // Alt + click selects the annotation below the selected one
    QVERIFY(fixture.click(position, Qt::AltModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ middle });
    QVERIFY(fixture.click(position, Qt::AltModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ bottom });
    QVERIFY(fixture.click(position, Qt::AltModifier));
    QVERIFY(fixture.annotations.getSelectedAnnotations() == std::vector<PDFObjectReference>{ top });

    // The same is offered by the context menu
    const std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(position, Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Select Next Annotation at This Place"));
    QCOMPARE(fixture.modificationCount, 0);
}

void AnnotationSelectionTest::textBoxOfCalloutIsResizedAlone()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ callout });

    // The frame of the selection is the text box, not the rectangle of the annotation (which
    // covers also the callout line). So there is no handle at the corner of the rectangle...
    const QRectF annotationFrame = fixture.pageToDevice().mapRect(fixture.rectangle(callout)).normalized();
    const QRectF textFrame = fixture.pageToDevice().mapRect(QRectF(110, 110, 80, 30)).normalized();
    QVERIFY(fixture.press(annotationFrame.bottomLeft().toPoint() + QPoint(2, -2)));
    fixture.release(annotationFrame.bottomLeft().toPoint() + QPoint(2, -2));
    QCOMPARE(fixture.modificationCount, 0);

    // ...and the handle at the corner of the text box resizes just the text box. The
    // callout line still points to the same place.
    QVERIFY(fixture.press(textFrame.bottomRight().toPoint()));
    QVERIFY(fixture.move(textFrame.bottomRight().toPoint() + QPoint(40, 20)));
    QVERIFY(fixture.release(textFrame.bottomRight().toPoint() + QPoint(40, 20)));
    QCOMPARE(fixture.modificationCount, 1);

    const QRectF textRectangle = PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout);
    QVERIFY(textRectangle.width() > 90.0);
    QVERIFY(textRectangle.height() > 35.0);
    QVERIFY(std::abs(textRectangle.left() - 110.0) < 0.5);
    QVERIFY(std::abs(textRectangle.bottom() - 140.0) < 0.5);

    const std::vector<PDFReal> calloutLine = fixture.numbers(callout, "CL");
    QCOMPARE(calloutLine.size(), size_t(4));
    QVERIFY(std::abs(calloutLine[0] - 20.0) < 0.01 && std::abs(calloutLine[1] - 20.0) < 0.01);
    QVERIFY(std::abs(calloutLine[2] - textRectangle.left()) < 0.01);

    // The text box can be set also directly
    QVERIFY(fixture.annotations.setAnnotationTextRectangle(callout, QRectF(150, 200, 100, 40)));
    QVERIFY(SelectionFixture::fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout), QRectF(150, 200, 100, 40), 0.01));
    QVERIFY(!fixture.annotations.setAnnotationTextRectangle(PDFObjectReference(), QRectF(150, 200, 100, 40)));
}

void AnnotationSelectionTest::calloutLineCanBeAddedAndRemoved()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(150, 200, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ freeText });

    std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(200, 225)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Add Callout Line"));
    QVERIFY(!actions.count("Remove Callout Line"));

    // The callout line ends at the nearest edge of the text box
    QVERIFY(!fixture.annotations.addAnnotationCalloutLine(freeText, QPointF(200, 225)));
    QVERIFY(fixture.annotations.addAnnotationCalloutLine(freeText, QPointF(50, 100)));
    QVERIFY(SelectionFixture::fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), freeText), QRectF(150, 200, 100, 50), 0.01));

    std::vector<PDFReal> calloutLine = fixture.numbers(freeText, "CL");
    QCOMPARE(calloutLine.size(), size_t(4));
    QCOMPARE(calloutLine[0], 50.0);
    QCOMPARE(calloutLine[1], 100.0);
    QVERIFY((calloutLine[2] == 150.0 && calloutLine[3] == 225.0) || (calloutLine[2] == 200.0 && calloutLine[3] == 200.0));

    actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(200, 225)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Remove Callout Line"));

    // The knee of the callout line is a point, which can be inserted and removed
    QVERIFY(fixture.annotations.insertAnnotationPoint(freeText, 0, QPointF(100, 225)));
    QCOMPARE(fixture.numbers(freeText, "CL").size(), size_t(6));
    QVERIFY(!fixture.annotations.insertAnnotationPoint(freeText, 0, QPointF(80, 200)));
    QVERIFY(!fixture.annotations.removeAnnotationPoint(freeText, 0));
    QVERIFY(fixture.annotations.removeAnnotationPoint(freeText, 1));
    QCOMPARE(fixture.numbers(freeText, "CL").size(), size_t(4));

    QVERIFY(fixture.annotations.removeAnnotationCalloutLine(freeText));
    QVERIFY(fixture.numbers(freeText, "CL").empty());
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(freeText), QRectF(150, 200, 100, 50), 0.01));
    QVERIFY(!fixture.annotations.removeAnnotationCalloutLine(PDFObjectReference()));
    QVERIFY(!fixture.annotations.addAnnotationCalloutLine(PDFObjectReference(), QPointF(50, 100)));
}

void AnnotationSelectionTest::markedLineEndsAndParts()
{
    const QPolygonF lines = { QPointF(50, 212), QPointF(250, 212), QPointF(50, 200), QPointF(250, 200),
                              QPointF(50, 192), QPointF(150, 192), QPointF(50, 180), QPointF(150, 180) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, lines, Qt::yellow);
    SelectionFixture fixture(builder.build());

    QVERIFY(fixture.click(fixture.device(QPointF(100, 206))));
    QVERIFY(fixture.annotations.isAnnotationSelected(highlight));

    // The end of the second marked line is dragged, so the line is longer. It keeps its height.
    QVERIFY(fixture.press(fixture.device(QPointF(150, 186))));
    QVERIFY(fixture.move(fixture.device(QPointF(200, 170))));
    QVERIFY(fixture.release(fixture.device(QPointF(200, 170))));
    QCOMPARE(fixture.modificationCount, 1);

    std::vector<PDFReal> quadPoints = fixture.numbers(highlight, "QuadPoints");
    QCOMPARE(quadPoints.size(), size_t(16));
    QVERIFY(std::abs(quadPoints[10] - 200.0) < 1.0);
    QCOMPARE(quadPoints[11], 192.0);
    QVERIFY(std::abs(quadPoints[14] - 200.0) < 1.0);
    QCOMPARE(quadPoints[15], 180.0);
    QCOMPARE(quadPoints[2], 250.0);

    // A single marked line can be deleted
    const std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(100, 206)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Delete This Marked Area"));

    QVERIFY(fixture.annotations.removeAnnotationPart(highlight, 0));
    quadPoints = fixture.numbers(highlight, "QuadPoints");
    QCOMPARE(quadPoints.size(), size_t(8));
    QCOMPARE(quadPoints[1], 192.0);
    QVERIFY(!fixture.annotations.removeAnnotationPart(highlight, 0));
    QVERIFY(!fixture.annotations.removeAnnotationPart(PDFObjectReference(), 0));
}

void AnnotationSelectionTest::pointSnapsToOtherAnnotation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(200, 200, 40, 40), 1.0, Qt::green, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    const QPointF corner = fixture.rectangle(square).topLeft();

    // The vertex is dropped near the corner of the square, so it snaps to the corner
    fixture.annotations.setSelectedAnnotations({ polygon });
    QVERIFY(fixture.press(fixture.device(QPointF(100, 150))));
    QVERIFY(fixture.move(fixture.device(corner + QPointF(1.0, -1.0))));
    QVERIFY(fixture.release(fixture.device(corner + QPointF(1.0, -1.0))));

    std::vector<PDFReal> vertices = fixture.numbers(polygon, "Vertices");
    QVERIFY(std::abs(vertices[4] - corner.x()) < 0.01);
    QVERIFY(std::abs(vertices[5] - corner.y()) < 0.01);

    // Ctrl disables the snapping
    fixture.annotations.setSelectedAnnotations({ polygon });
    QVERIFY(fixture.press(fixture.device(corner)));
    QVERIFY(fixture.move(fixture.device(corner + QPointF(1.5, -1.5)), Qt::ControlModifier));
    QVERIFY(fixture.release(fixture.device(corner + QPointF(1.5, -1.5)), Qt::ControlModifier));

    vertices = fixture.numbers(polygon, "Vertices");
    QVERIFY(std::abs(vertices[4] - corner.x()) > 0.5);
    QVERIFY(std::abs(vertices[5] - corner.y()) > 0.5);
    QCOMPARE(fixture.modificationCount, 2);
}

void AnnotationSelectionTest::alignAndDistribute()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference first = builder.createAnnotationSquare(page, QRectF(50, 50, 40, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference second = builder.createAnnotationSquare(page, QRectF(120, 80, 40, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference third = builder.createAnnotationSquare(page, QRectF(220, 140, 40, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ first, second, third });

    const std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(60, 60)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Align"));
    QVERIFY(actions.count("Common Properties"));

    // The gaps between the annotations are the same, the first and the last one stay
    fixture.annotations.distributeSelectedAnnotations(Qt::Horizontal);
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(first), QRectF(50, 50, 40, 40), 0.01));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(second), QRectF(135, 80, 40, 40), 0.01));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(third), QRectF(220, 140, 40, 40), 0.01));

    fixture.annotations.distributeSelectedAnnotations(Qt::Vertical);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(second), QRectF(135, 95, 40, 40), 0.01));

    // Alignment is given on the screen - the top of the screen is the top of the page
    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::Left);
    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::Top);
    for (const PDFObjectReference& annotation : { first, second, third })
    {
        QVERIFY(std::abs(fixture.rectangle(annotation).left() - 50.0) < 0.01);
        QVERIFY(std::abs(fixture.rectangle(annotation).bottom() - 180.0) < 0.01);
    }

    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::Right);
    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::Bottom);
    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::HorizontalCenter);
    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::VerticalCenter);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(first), fixture.rectangle(third), 0.01));

    // A single annotation has nothing to be aligned to
    const int modificationCount = fixture.modificationCount;
    fixture.annotations.setSelectedAnnotations({ first });
    fixture.annotations.alignSelectedAnnotations(PDFWidgetAnnotationManager::Alignment::Left);
    QCOMPARE(fixture.modificationCount, modificationCount);
}

void AnnotationSelectionTest::keyboardEditsPoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ polygon });
    QCOMPARE(fixture.annotations.getActivePoint(), -1);

    // Alt + arrows select the point
    QVERIFY(fixture.key(Qt::Key_Left, Qt::AltModifier));
    QCOMPARE(fixture.annotations.getActivePoint(), 2);
    QVERIFY(fixture.key(Qt::Key_Right, Qt::AltModifier));
    QCOMPARE(fixture.annotations.getActivePoint(), 0);
    QVERIFY(fixture.key(Qt::Key_Right, Qt::AltModifier));
    QCOMPARE(fixture.annotations.getActivePoint(), 1);

    // Arrows move the point (not the whole annotation)
    QVERIFY(fixture.key(Qt::Key_Right));
    QVERIFY(fixture.key(Qt::Key_Up, Qt::ShiftModifier));
    std::vector<PDFReal> vertices = fixture.numbers(polygon, "Vertices");
    QVERIFY(std::abs(vertices[0] - 50.0) < 0.01 && std::abs(vertices[1] - 50.0) < 0.01);
    QVERIFY(std::abs(vertices[2] - 151.0) < 0.01 && std::abs(vertices[3] - 60.0) < 0.01);
    QCOMPARE(fixture.annotations.getActivePoint(), 1);

    // Insert adds a point behind the point, Delete removes the point
    QVERIFY(fixture.key(Qt::Key_Insert));
    QCOMPARE(fixture.numbers(polygon, "Vertices").size(), size_t(8));
    QCOMPARE(fixture.annotations.getActivePoint(), 2);
    QVERIFY(fixture.key(Qt::Key_Delete));
    QCOMPARE(fixture.numbers(polygon, "Vertices").size(), size_t(6));
    QCOMPARE(fixture.pageAnnotations().size(), size_t(1));

    // A triangle cannot lose a point, the annotation is not deleted instead of it
    QVERIFY(fixture.key(Qt::Key_Delete));
    QCOMPARE(fixture.numbers(polygon, "Vertices").size(), size_t(6));
    QCOMPARE(fixture.pageAnnotations().size(), size_t(1));

    // Escape ends the editing of the point, the next one clears the selection
    QVERIFY(fixture.key(Qt::Key_Escape));
    QCOMPARE(fixture.annotations.getActivePoint(), -1);
    QVERIFY(fixture.annotations.hasSelection());
    QVERIFY(fixture.key(Qt::Key_Escape));
    QVERIFY(!fixture.annotations.hasSelection());
}

void AnnotationSelectionTest::copyToPagesAndPasteInPlace()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    builder.appendPage(QRectF(0, 0, 300, 300));
    builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ note });

    // The annotation is copied with its popup window, its own page is skipped
    fixture.annotations.copySelectedAnnotationsToPages({ 0, 1, 2, 7 });
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(fixture.pageAnnotations(0).size(), size_t(2));
    QCOMPARE(fixture.pageAnnotations(1).size(), size_t(2));
    QCOMPARE(fixture.pageAnnotations(2).size(), size_t(2));
    QVERIFY(fixture.entry(fixture.pageAnnotations(1).front(), "Popup").isReference());

    fixture.annotations.copySelectedAnnotationsToPages({ 0 });
    QCOMPARE(fixture.modificationCount, 1);

    // Paste at the original position - the position of the cursor selects just the page
    fixture.annotations.copySelectedAnnotations();
    fixture.annotations.pasteAnnotations(fixture.device(QPointF(200, 200)), true);
    QCOMPARE(fixture.pageAnnotations(0).size(), size_t(4));
    const QRectF original = fixture.rectangle(note);
    const QRectF pasted = fixture.rectangle(fixture.annotations.getSelectedAnnotations().front());
    QVERIFY(SelectionFixture::fuzzyCompare(pasted, original, 0.01));
}

void AnnotationSelectionTest::commonProperties()
{
    TwoSquares squares;
    PDFDocumentBuilder builder(&squares.document);
    const PDFObjectReference locked = builder.createAnnotationSquare(squares.page, QRectF(200, 50, 40, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference note = builder.createAnnotationText(squares.page, QRectF(50, 200, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    SelectionFixture::setEntry(builder, locked, "F", PDFObject::createInteger(PDFAnnotation::Locked));
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ squares.first, squares.second, locked, note });

    fixture.annotations.setSelectedAnnotationsColor(Qt::red);
    fixture.annotations.setSelectedAnnotationsOpacity(0.5);
    fixture.annotations.setSelectedAnnotationsBorderWidth(3.0);
    fixture.annotations.setSelectedAnnotationsColor(QColor());
    QCOMPARE(fixture.modificationCount, 3);

    PDFDocumentDataLoaderDecorator loader(fixture.document.data());
    for (const PDFObjectReference& annotation : { squares.first, squares.second })
    {
        QVERIFY(fixture.numbers(annotation, "C") == (std::vector<PDFReal>{ 1.0, 0.0, 0.0 }));
        const PDFDictionary* dictionary = fixture.document->getDictionaryFromObject(fixture.document->getObjectByReference(annotation));
        QCOMPARE(loader.readNumberFromDictionary(dictionary, "CA", 1.0), 0.5);
        QCOMPARE(loader.readNumberFromDictionary(dictionary, "ca", 1.0), 0.5);
        QCOMPARE(loader.readNumberFromDictionary(fixture.document->getDictionaryFromObject(dictionary->get("BS")), "W", 0.0), 3.0);
    }

    // Locked annotation is skipped, a sticky note has no border
    QVERIFY(fixture.numbers(locked, "C") != (std::vector<PDFReal>{ 1.0, 0.0, 0.0 }));
    QVERIFY(fixture.numbers(note, "C") == (std::vector<PDFReal>{ 1.0, 0.0, 0.0 }));
    QVERIFY(fixture.entry(note, "BS").isNull());
}

void AnnotationSelectionTest::annotationRectangleTransformsGeometry()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    SelectionFixture fixture(builder.build());

    // The points of the line follow the rectangle
    const QRectF lineRectangle = fixture.rectangle(line);
    QVERIFY(fixture.annotations.setAnnotationRectangle(line, QRectF(lineRectangle.left() + 100.0, lineRectangle.top() + 50.0, lineRectangle.width() * 2.0, lineRectangle.height())));
    const std::vector<PDFReal> points = fixture.numbers(line, "L");
    QVERIFY(std::abs((points[2] - points[0]) - 200.0) < 0.01);
    QVERIFY(std::abs(points[1] - 200.0) < 0.01);

    // A sticky note cannot be resized, so it is moved to the center of the rectangle
    const QRectF noteRectangle = fixture.rectangle(note);
    QVERIFY(fixture.annotations.setAnnotationRectangle(note, QRectF(100, 100, 200, 100)));
    QVERIFY(SelectionFixture::fuzzyCompare(QRectF(QPointF(), fixture.rectangle(note).size()), QRectF(QPointF(), noteRectangle.size()), 0.01));
    QVERIFY(QLineF(fixture.rectangle(note).center(), QPointF(200, 150)).length() < 0.01);

    QVERIFY(!fixture.annotations.setAnnotationRectangle(note, QRectF()));
    QVERIFY(!fixture.annotations.setAnnotationRectangle(PDFObjectReference(), QRectF(100, 100, 200, 100)));
    QCOMPARE(fixture.modificationCount, 2);
}

void AnnotationSelectionTest::geometryDialog()
{
    using Manipulator = PDFAnnotationManipulator;

    Manipulator::EditablePoints points;
    points.points = { QPointF(10, 10), QPointF(110, 10) };
    points.minimalCount = 2;
    points.maximalCount = 2;

    // Annotation, which supports everything
    const Manipulator::Capabilities all = Manipulator::Move | Manipulator::Resize | Manipulator::RotateRightAngle | Manipulator::RotateArbitrary | Manipulator::Mirror | Manipulator::EditPoints;
    {
        PDFAnnotationGeometryDialog dialog(QRectF(5, 5, 110, 10), points, all, nullptr);
        QVERIFY(!dialog.isRectangleChanged());
        QVERIFY(!dialog.isPointsChanged());
        QVERIFY(dialog.getLineInfo().contains("100"));

        dialog.setRectangle(QRectF(20, 30, 220, 40));
        QVERIFY(dialog.isRectangleChanged());
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(20, 30, 220, 40), 0.001));

        dialog.setRotation(33.5);
        QCOMPARE(dialog.getRotation(), 33.5);
    }

    // Units - the values are converted, the geometry stays in the page units
    {
        PDFAnnotationGeometryDialog dialog(QRectF(72, 72, 144, 72), points, all, nullptr);
        dialog.setUnit(2);
        QVERIFY(!dialog.isRectangleChanged());
        dialog.setRectangle(QRectF(72, 72, 288, 72));
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(72, 72, 288, 72), 0.1));
    }

    // Points - the rectangle cannot be edited together with them
    {
        PDFAnnotationGeometryDialog dialog(QRectF(5, 5, 110, 10), points, all, nullptr);
        dialog.setPoint(1, QPointF(210, 60));
        dialog.setPoint(5, QPointF(0, 0));
        QVERIFY(dialog.isPointsChanged());
        QVERIFY(!dialog.isRectangleChanged());
        QVERIFY(dialog.getPoints() == (std::vector<QPointF>{ QPointF(10, 10), QPointF(210, 60) }));
    }

    // Annotation, which can be rotated only by the right angle
    {
        PDFAnnotationGeometryDialog dialog(QRectF(5, 5, 110, 10), Manipulator::EditablePoints(), Manipulator::Move | Manipulator::Resize | Manipulator::RotateRightAngle, nullptr);
        dialog.setRotation(100.0);
        QCOMPARE(dialog.getRotation(), 90.0);
        QVERIFY(dialog.getPoints().empty());
    }

    // Annotation, which can only be moved
    {
        PDFAnnotationGeometryDialog dialog(QRectF(5, 5, 20, 20), Manipulator::EditablePoints(), Manipulator::Capabilities(Manipulator::Move), nullptr);
        dialog.setRectangle(QRectF(50, 60, 200, 200));
        dialog.setRotation(45.0);
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(50, 60, 20, 20), 0.001));
        QCOMPARE(dialog.getRotation(), 0.0);
    }
}

void AnnotationSelectionTest::selectionIsDrawn()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(200, 200, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(160, 10, 130, 140), QRectF(200, 100, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(170, 20), QPointF(200, 110), AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None);
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, QRectF(50, 250, 100, 12), Qt::yellow);
    SelectionFixture fixture(builder.build());

    // Returns the number of pixels drawn by the markers of the selection
    auto countDrawnPixels = [&fixture]()
    {
        QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            fixture.annotations.drawPostRendering(&painter, image.rect());
        }

        int count = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                count += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
            }
        }
        return count;
    };

    QCOMPARE(countDrawnPixels(), 0);

    // Selected annotations with the frame and the handles, points of the polygon
    fixture.annotations.setSelectedAnnotations({ polygon });
    const int polygonPixels = countDrawnPixels();
    QVERIFY(polygonPixels > 0);

    // Dragged point - preview of the shape and the numeric feedback
    QVERIFY(fixture.press(fixture.device(QPointF(100, 150))));
    QVERIFY(fixture.move(fixture.device(QPointF(120, 180))));
    QVERIFY(countDrawnPixels() != polygonPixels);
    QVERIFY(fixture.key(Qt::Key_Escape));
    QCOMPARE(fixture.modificationCount, 0);

    // Dragged handle - the annotation is drawn, as it will look like
    const QRectF frame = fixture.pageToDevice().mapRect(fixture.rectangle(polygon)).normalized();
    // (there is no vertex of the polygon at the top left corner of the frame)
    QVERIFY(fixture.press(frame.topLeft().toPoint()));
    QVERIFY(fixture.move(frame.topLeft().toPoint() - QPoint(40, 30)));
    QVERIFY(countDrawnPixels() > polygonPixels);
    QVERIFY(fixture.key(Qt::Key_Escape));

    // Point edited from the keyboard
    QVERIFY(fixture.key(Qt::Key_Right, Qt::AltModifier));
    QVERIFY(countDrawnPixels() > polygonPixels);

    // Group of annotations, a sticky note without handles, text box of a callout, marked line
    fixture.annotations.setSelectedAnnotations({ polygon, note, callout, highlight });
    QVERIFY(countDrawnPixels() > 0);
    fixture.annotations.setSelectedAnnotations({ note });
    QVERIFY(countDrawnPixels() > 0);
    fixture.annotations.setSelectedAnnotations({ highlight });
    QVERIFY(countDrawnPixels() > 0);

    fixture.annotations.setSelectedAnnotations({ callout });
    const QRectF textFrame = fixture.pageToDevice().mapRect(QRectF(200, 100, 80, 30)).normalized();
    QVERIFY(fixture.press(textFrame.topRight().toPoint()));
    QVERIFY(fixture.move(textFrame.topRight().toPoint() + QPoint(10, -10)));
    QVERIFY(countDrawnPixels() > 0);
    QVERIFY(fixture.key(Qt::Key_Escape));

    // Rubber band and the annotation under the cursor
    fixture.annotations.clearSelection();
    QVERIFY(fixture.press(fixture.device(QPointF(20, 280)), Qt::ControlModifier));
    QVERIFY(fixture.move(fixture.device(QPointF(160, 20)), Qt::ControlModifier));
    QVERIFY(countDrawnPixels() > 0);
    QVERIFY(fixture.release(fixture.device(QPointF(160, 20)), Qt::ControlModifier));
    QVERIFY(fixture.annotations.isAnnotationSelected(polygon));

    fixture.annotations.clearSelection();
    fixture.move(fixture.device(QPointF(210, 210)), Qt::NoModifier, Qt::NoButton);
    QVERIFY(countDrawnPixels() > 0);
    QCOMPARE(fixture.modificationCount, 0);
}

QTEST_MAIN(AnnotationSelectionTest)

#include "tst_annotationselectiontest.moc"
