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
#include "pdfobjecteditormodel.h"
#include "pdffile.h"

#include <QtTest>
#include <QAction>
#include <QClipboard>
#include <QMimeData>
#include <QMenu>
#include <QTimer>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QLabel>

#include <map>
#include <set>
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
    void reviewPointDragNoRotate_data();
    void reviewPointDragNoRotate();
    void editsOfNoRotateAnnotation_data();
    void editsOfNoRotateAnnotation();
    void textBoxOfNoRotateCallout();
    void nudgeOnDifferentlyRotatedPages();
    void resizeSnapsToOtherAnnotation();
    void dragAndDropSnaps();
    void dragAndDropMovesTextBox();
    void interactionScope();
    void previewIsResultOfOperation();
    void thinShapeDoesNotBlockAnnotationsInside();
    void altClickCyclesOverlappingAnnotations();
    void textBoxOfCalloutIsResizedAlone();
    void calloutLineCanBeAddedAndRemoved();
    void markedLineEndsAndParts();
    void partsAreDrawnByMouse();
    void inkIsErasedAndItsPointsAreEdited();
    void handlesOfLongMarkupAreNearCursor();
    void replyIsWrittenInPopup();
    void attachedFileIsReplaced();
    void pointSnapsToOtherAnnotation();
    void alignAndDistribute();
    void keyboardEditsPoints();
    void copyToPagesAndPasteInPlace();
    void commonProperties();
    void annotationRectangleTransformsGeometry();
    void geometryDialog();
    void geometryDialogReferencePointAndSegments();
    void geometryDialogFromMenu();
    void propertiesDialogRectangle();
    void propertiesModelAttributes();
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

    /// Shows the menu, triggers its action with the text, and lets the function to operate the
    /// modal dialogs opened by the action (the function is called once for each dialog, a dialog
    /// without a function is rejected). Returns true, if the action has been triggered.
    static bool triggerMenuAction(const std::function<void()>& showMenu, const QString& text, const std::function<void(QDialog*)>& onDialog)
    {
        bool isTriggered = false;
        bool isMenuHandled = false;
        std::set<QDialog*> handledDialogs;

        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&]()
        {
            // The popup window of an annotation is a dialog, which is a popup widget
            QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
            {
                dialog = qobject_cast<QDialog*>(QApplication::activePopupWidget());
            }

            if (dialog)
            {
                if (handledDialogs.insert(dialog).second)
                {
                    if (onDialog)
                    {
                        onDialog(dialog);
                    }
                    else
                    {
                        dialog->reject();
                    }
                }
                return;
            }

            QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (menu && !isMenuHandled)
            {
                isMenuHandled = true;

                // The action can be in a submenu
                QList<QAction*> actions = menu->actions();
                for (qsizetype i = 0; i < actions.size(); ++i)
                {
                    if (actions[i]->menu())
                    {
                        actions.append(actions[i]->menu()->actions());
                    }
                }

                for (QAction* action : actions)
                {
                    if (action->text() == text && action->isEnabled())
                    {
                        // A timer is not activated again, until its slot returns, so the action (which
                        // opens a modal dialog operated by this timer) is triggered by another timer
                        isTriggered = true;
                        QTimer::singleShot(0, menu, [menu, action]()
                        {
                            action->trigger();
                            menu->close();
                        });
                        return;
                    }
                }

                menu->close();
            }
        });
        timer.start(1);
        showMenu();
        timer.stop();

        return isTriggered;
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

    // The points of the line follow the rectangle. The rectangle is the line with a margin (the
    // width of the line), which is not scaled - the rectangle is exactly, what has been asked for.
    const QRectF lineRectangle = fixture.rectangle(line);
    const QRectF newLineRectangle(lineRectangle.left() + 100.0, lineRectangle.top() + 50.0, lineRectangle.width() * 2.0, lineRectangle.height());
    const PDFReal margin = 50.0 - lineRectangle.left();
    QVERIFY(margin > 0.0);
    QVERIFY(fixture.annotations.setAnnotationRectangle(line, newLineRectangle));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(line), newLineRectangle, 0.001));
    const std::vector<PDFReal> points = fixture.numbers(line, "L");
    QVERIFY(std::abs(points[0] - (newLineRectangle.left() + margin)) < 0.01);
    QVERIFY(std::abs(points[2] - (newLineRectangle.right() - margin)) < 0.01);
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

void AnnotationSelectionTest::geometryDialogReferencePointAndSegments()
{
    using Manipulator = PDFAnnotationManipulator;
    using ReferencePoint = PDFAnnotationGeometryDialog::ReferencePoint;
    const Manipulator::Capabilities all = Manipulator::Move | Manipulator::Resize | Manipulator::RotateRightAngle | Manipulator::RotateArbitrary | Manipulator::Mirror | Manipulator::EditPoints;

    // The reference point stays, when the size is changed. The y axis points upwards.
    {
        PDFAnnotationGeometryDialog dialog(QRectF(0, 0, 100, 50), Manipulator::EditablePoints(), all, nullptr);
        QCOMPARE(dialog.getReferencePoint(QRectF(0, 0, 100, 50)), QPointF(50, 25));
        dialog.setSize(QSizeF(200, 50));
        QVERIFY(dialog.isRectangleChanged());
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(-50, 0, 200, 50), 0.001));

        dialog.setReferencePoint(ReferencePoint::BottomLeft);
        QCOMPARE(dialog.getReferencePoint(dialog.getRectangle()), QPointF(-50, 0));
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(-50, 0, 200, 50), 0.001));
        dialog.setSize(QSizeF(100, 100));
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(-50, 0, 100, 100), 0.001));

        dialog.setReferencePoint(ReferencePoint::TopRight);
        QCOMPARE(dialog.getReferencePoint(dialog.getRectangle()), QPointF(50, 100));
        dialog.setSize(QSizeF(50, 20));
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(0, 80, 50, 20), 0.001));

        // The whole rectangle is set regardless of the reference point
        dialog.setReferencePoint(ReferencePoint::Right);
        dialog.setRectangle(QRectF(10, 20, 30, 40));
        QVERIFY(SelectionFixture::fuzzyCompare(dialog.getRectangle(), QRectF(10, 20, 30, 40), 0.001));
    }

    // Length and angle of the line are typed directly - the start of the line stays
    {
        Manipulator::EditablePoints line;
        line.points = { QPointF(10, 10), QPointF(110, 10) };
        line.minimalCount = 2;
        line.maximalCount = 2;

        PDFAnnotationGeometryDialog dialog(QRectF(5, 5, 110, 10), line, all, nullptr);
        dialog.setSegmentLength(50.0);
        QVERIFY(dialog.isPointsChanged());
        QVERIFY(!dialog.isRectangleChanged());
        QVERIFY(QLineF(dialog.getPoints()[1], QPointF(60, 10)).length() < 0.001);

        dialog.setSegmentAngle(90.0);
        QVERIFY(QLineF(dialog.getPoints()[0], QPointF(10, 10)).length() < 0.001);
        QVERIFY(QLineF(dialog.getPoints()[1], QPointF(10, 60)).length() < 0.001);
        QVERIFY(dialog.getLineInfo().contains("90"));

        // The length is typed in the selected unit
        dialog.setUnit(2);
        dialog.setSegmentLength(144.0);
        QVERIFY(QLineF(dialog.getPoints()[1], QPointF(10, 154)).length() < 0.01);
    }

    // The closing segment of a polygon ends at its first point
    {
        Manipulator::EditablePoints polygon;
        polygon.points = { QPointF(0, 0), QPointF(100, 0), QPointF(100, 100) };
        polygon.isClosed = true;
        polygon.minimalCount = 3;

        PDFAnnotationGeometryDialog dialog(QRectF(0, 0, 100, 100), polygon, all, nullptr);
        dialog.setSegment(2);
        QVERIFY(dialog.getLineInfo().contains("141"));
        dialog.setSegmentAngle(180.0);
        dialog.setSegmentLength(100.0);
        QVERIFY(QLineF(dialog.getPoints()[0], QPointF(0, 100)).length() < 0.001);
        QVERIFY(QLineF(dialog.getPoints()[2], QPointF(100, 100)).length() < 0.001);

        dialog.setSegment(7);
        dialog.setSegment(1);
        QVERIFY(dialog.getLineInfo().contains("100"));
    }

    // Points of an annotation, which does not allow to edit them, are read only
    {
        Manipulator::EditablePoints line;
        line.points = { QPointF(10, 10), QPointF(110, 10) };
        PDFAnnotationGeometryDialog dialog(QRectF(5, 5, 110, 10), line, Manipulator::Move | Manipulator::Resize, nullptr);
        QVERIFY(!dialog.isPointsChanged());
        QVERIFY(dialog.getLineInfo().contains("100"));
    }
}

void AnnotationSelectionTest::geometryDialogFromMenu()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    // The length and the angle of the line are typed in the dialog
    fixture.annotations.setSelectedAnnotations({ line });
    auto showLineMenu = [&]() { fixture.press(fixture.device(QPointF(100, 150)), Qt::NoModifier, Qt::RightButton); };
    QVERIFY(SelectionFixture::triggerMenuAction(showLineMenu, "Geometry...", [](QDialog* dialog)
    {
        PDFAnnotationGeometryDialog* geometryDialog = qobject_cast<PDFAnnotationGeometryDialog*>(dialog);
        if (!geometryDialog)
        {
            dialog->reject();
            return;
        }
        geometryDialog->setSegmentLength(80.0);
        geometryDialog->setSegmentAngle(90.0);
        geometryDialog->accept();
    }));
    QCOMPARE(fixture.modificationCount, 1);
    const std::vector<PDFReal> points = fixture.numbers(line, "L");
    QVERIFY(std::abs(points[0] - 50.0) < 0.01 && std::abs(points[1] - 150.0) < 0.01);
    QVERIFY(std::abs(points[2] - 50.0) < 0.01 && std::abs(points[3] - 230.0) < 0.01);

    // The square is rotated around its corner
    fixture.annotations.setSelectedAnnotations({ square });
    auto showSquareMenu = [&]() { fixture.press(fixture.device(QPointF(80, 70)), Qt::NoModifier, Qt::RightButton); };
    QVERIFY(SelectionFixture::triggerMenuAction(showSquareMenu, "Geometry...", [](QDialog* dialog)
    {
        PDFAnnotationGeometryDialog* geometryDialog = qobject_cast<PDFAnnotationGeometryDialog*>(dialog);
        if (!geometryDialog)
        {
            dialog->reject();
            return;
        }
        geometryDialog->setReferencePoint(PDFAnnotationGeometryDialog::ReferencePoint::BottomLeft);
        geometryDialog->setRotation(-90.0);
        geometryDialog->accept();
    }));
    QCOMPARE(fixture.modificationCount, 2);

    // Counterclockwise rotation around the point (50, 50)
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(square), QRectF(10, 50, 40, 60), 0.01));

    // The dialog is cancelled
    QVERIFY(SelectionFixture::triggerMenuAction([&]() { fixture.press(fixture.device(QPointF(30, 80)), Qt::NoModifier, Qt::RightButton); }, "Geometry...", nullptr));
    QVERIFY(!SelectionFixture::triggerMenuAction([&]() { fixture.press(fixture.device(QPointF(30, 80)), Qt::NoModifier, Qt::RightButton); }, "No Such Action", nullptr));
    QCOMPARE(fixture.modificationCount, 2);
}

void AnnotationSelectionTest::propertiesDialogRectangle()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, { QPointF(50, 50), QPointF(110, 50), QPointF(110, 90), QPointF(50, 90) }, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ polygon });
    const QRectF oldRectangle = fixture.rectangle(polygon);

    // Jakub Melka: the real dialog of the properties is operated - the button of the rectangle
    // opens the dialog with the coordinates, both dialogs are confirmed. The geometry of the
    // annotation must follow the rectangle.
    QDialog* propertiesDialog = nullptr;
    bool isRectangleEdited = false;
    auto onDialog = [&](QDialog* dialog)
    {
        if (!propertiesDialog)
        {
            propertiesDialog = dialog;

            QPushButton* rectangleButton = nullptr;
            for (QPushButton* button : dialog->findChildren<QPushButton*>())
            {
                if (button->text().startsWith(QChar('[')))
                {
                    rectangleButton = button;
                }
            }

            if (!rectangleButton)
            {
                dialog->reject();
                return;
            }

            // The click opens a modal dialog, so it cannot be done in the slot of the timer, which operates it
            QTimer::singleShot(0, rectangleButton, &QPushButton::click);
            return;
        }

        const QList<QDoubleSpinBox*> spinBoxes = dialog->findChildren<QDoubleSpinBox*>();
        if (spinBoxes.size() == 4)
        {
            spinBoxes[0]->setValue(oldRectangle.left() + 100.0);
            spinBoxes[1]->setValue(oldRectangle.top() + 120.0);
            spinBoxes[2]->setValue(oldRectangle.width() * 2.0);
            spinBoxes[3]->setValue(oldRectangle.height());
            isRectangleEdited = true;
            dialog->accept();
        }
        else
        {
            dialog->reject();
        }

        QTimer::singleShot(0, propertiesDialog, isRectangleEdited ? &QDialog::accept : &QDialog::reject);
    };

    auto showMenu = [&]() { fixture.press(fixture.device(QPointF(80, 70)), Qt::NoModifier, Qt::RightButton); };
    QVERIFY(SelectionFixture::triggerMenuAction(showMenu, "Edit...", onDialog));
    QVERIFY(isRectangleEdited);
    QCOMPARE(fixture.modificationCount, 1);

    const QRectF expectedRectangle(oldRectangle.left() + 100.0, oldRectangle.top() + 120.0, oldRectangle.width() * 2.0, oldRectangle.height());
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(polygon), expectedRectangle, 0.01));

    // The vertices follow the rectangle (the margin of the rectangle - the width of the line - is not scaled)
    const std::vector<PDFReal> vertices = fixture.numbers(polygon, "Vertices");
    QCOMPARE(vertices.size(), size_t(8));
    const PDFReal margin = 50.0 - oldRectangle.left();
    QVERIFY(margin > 0.0);
    QVERIFY(std::abs(vertices[0] - (expectedRectangle.left() + margin)) < 0.01 && std::abs(vertices[1] - (expectedRectangle.top() + margin)) < 0.01);
    QVERIFY(std::abs(vertices[4] - (expectedRectangle.right() - margin)) < 0.01 && std::abs(vertices[5] - (expectedRectangle.bottom() - margin)) < 0.01);

    // The dialog is cancelled
    QVERIFY(SelectionFixture::triggerMenuAction([&]() { fixture.press(fixture.device(QPointF(vertices[0] + 20, vertices[1] + 20)), Qt::NoModifier, Qt::RightButton); }, "Edit...", nullptr));
    QCOMPARE(fixture.modificationCount, 1);
}

void AnnotationSelectionTest::propertiesModelAttributes()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(150, 200, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    const PDFDocument document = builder.build();

    // The functions of the model are public in its base class
    PDFObjectEditorAnnotationsModel model(nullptr);
    PDFObjectEditorAbstractModel& baseModel = model;

    auto findAttribute = [&model](const QString& subcategory, const QString& name)
    {
        for (size_t i = 0; i < model.getAttributeCount(); ++i)
        {
            if (model.getAttributeSubcategory(i) == subcategory && model.getAttributeName(i) == name)
            {
                return i;
            }
        }
        return model.getAttributeCount();
    };

    const size_t borderWidth = findAttribute("Border Style", "Width");
    const size_t leaderLength = findAttribute("Style", "Leader line length (negative for the other side)");
    const size_t captionAlong = findAttribute("Text", "Caption offset along the line");
    const size_t captionPerpendicular = findAttribute("Text", "Caption offset perpendicular to the line");
    QVERIFY(borderWidth < model.getAttributeCount());
    QVERIFY(leaderLength < model.getAttributeCount());
    QVERIFY(captionAlong < model.getAttributeCount());
    QVERIFY(captionPerpendicular < model.getAttributeCount());

    // The attribute belongs to the annotation, if the type of the annotation
    // has it, and if its selector (the border style is optional) is switched on
    auto setEditedObject = [&model](const PDFObject& object)
    {
        model.setEditedObject(object);
        for (const size_t selector : model.getSelectorAttributes())
        {
            model.setSelectorValue(selector, true);
        }
    };

    // Free text annotation has the border style, a sticky note has none
    setEditedObject(document.getObjectByReference(freeText));
    QVERIFY(model.queryAttribute(borderWidth, PDFObjectEditorAbstractModel::Question::HasAttribute));
    QVERIFY(!model.queryAttribute(leaderLength, PDFObjectEditorAbstractModel::Question::HasAttribute));
    setEditedObject(document.getObjectByReference(note));
    QVERIFY(!model.queryAttribute(borderWidth, PDFObjectEditorAbstractModel::Question::HasAttribute));

    // The length of the leader lines is oriented
    setEditedObject(document.getObjectByReference(line));
    QVERIFY(model.queryAttribute(leaderLength, PDFObjectEditorAbstractModel::Question::HasAttribute));
    QVERIFY(model.getMinimumValue(leaderLength).toDouble() < 0.0);

    PDFDocumentDataLoaderDecorator loader(&document);
    PDFObject object = baseModel.writeAttributeValueToObject(leaderLength, model.getEditedObject(), PDFObject::createReal(-12.0));
    QCOMPARE(loader.readNumberFromDictionary(document.getDictionaryFromObject(object), "LL", 0.0), -12.0);

    // The offset of the caption is an array of two numbers. If the second number is written
    // into a missing array, then the first one is the default value (it is not a null object).
    object = baseModel.writeAttributeValueToObject(captionPerpendicular, object, PDFObject::createReal(5.0));
    const PDFObject captionOffset = document.getDictionaryFromObject(object)->get("CO");
    QVERIFY(captionOffset.isArray());
    QCOMPARE(captionOffset.getArray()->getCount(), size_t(2));
    QVERIFY(captionOffset.getArray()->getItem(0).isReal() || captionOffset.getArray()->getItem(0).isInt());
    QVERIFY(loader.readNumberArray(captionOffset) == (std::vector<PDFReal>{ 0.0, 5.0 }));

    // The other items of the array are taken from the edited object
    model.setEditedObject(object);
    object = baseModel.writeAttributeValueToObject(captionAlong, object, PDFObject::createReal(-3.0));
    QVERIFY(loader.readNumberArray(document.getDictionaryFromObject(object)->get("CO")) == (std::vector<PDFReal>{ -3.0, 5.0 }));

    // The values are read back
    model.setEditedObject(object);
    QCOMPARE(loader.readNumber(baseModel.getValue(captionAlong, true), 0.0), -3.0);
    QCOMPARE(loader.readNumber(baseModel.getValue(captionPerpendicular, true), 0.0), 5.0);
}

void AnnotationSelectionTest::partsAreDrawnByMouse()
{
    using PartEdit = PDFWidgetAnnotationManager::PartEdit;

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference ink = builder.createAnnotationInk(page, Polygons{ QPolygonF({ QPointF(50, 100), QPointF(250, 100) }) }, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, QRectF(50, 250, 100, 12), Qt::yellow);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(200, 200, 50, 30), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    auto partCount = [&fixture](PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getParts(&fixture.document->getStorage(), annotation).shapes.size();
    };

    auto render = [&fixture]()
    {
        QImage image(fixture.widget.getDrawWidget()->getWidget()->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        fixture.annotations.drawPostRendering(&painter, image.rect());
        return image;
    };

    // The edit needs a single selected annotation, which has the parts
    QVERIFY(!fixture.annotations.beginPartEdit(PartEdit::AddStroke));
    fixture.annotations.setSelectedAnnotations({ square });
    QVERIFY(!fixture.annotations.beginPartEdit(PartEdit::AddStroke));
    fixture.annotations.setSelectedAnnotations({ ink });
    QVERIFY(!fixture.annotations.beginPartEdit(PartEdit::None));
    QVERIFY(!fixture.annotations.beginPartEdit(PartEdit::AddMarkedAreas));
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::None);

    // The menu offers it
    std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(150, 100)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Add Stroke"));
    QVERIFY(actions.count("Erase Stroke at This Place"));
    QVERIFY(!actions.count("Add Marked Text or Area"));

    // Escape cancels the edit, the selection stays
    QVERIFY(fixture.annotations.beginPartEdit(PartEdit::AddStroke));
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::AddStroke);
    QVERIFY(fixture.key(Qt::Key_Escape));
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::None);
    QVERIFY(fixture.annotations.isAnnotationSelected(ink));

    // A new stroke is drawn by the mouse (even over another annotation, which is not selected by it)
    QVERIFY(SelectionFixture::triggerMenuAction([&]() { fixture.press(fixture.device(QPointF(150, 100)), Qt::NoModifier, Qt::RightButton); }, "Add Stroke", nullptr));
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::AddStroke);
    QVERIFY(fixture.move(fixture.device(QPointF(190, 190)), Qt::NoModifier, Qt::NoButton));
    QVERIFY(fixture.press(fixture.device(QPointF(190, 190))));
    for (int i = 1; i <= 8; ++i)
    {
        QVERIFY(fixture.move(fixture.device(QPointF(190 + 10 * i, 190 + 5 * i))));
    }
    const QImage strokeImage = render();
    QVERIFY(fixture.release(fixture.device(QPointF(270, 230))));
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::None);
    QVERIFY(fixture.annotations.isAnnotationSelected(ink));
    QVERIFY(!fixture.annotations.isAnnotationSelected(square));
    QCOMPARE(partCount(ink), size_t(2));

    const QPolygonF newStroke = PDFAnnotationManipulator::getParts(&fixture.document->getStorage(), ink).shapes.back();
    QVERIFY(newStroke.size() >= 5);
    QVERIFY(QLineF(newStroke.front(), QPointF(190, 190)).length() < 2.0);
    QVERIFY(QLineF(newStroke.back(), QPointF(270, 230)).length() < 2.0);

    // The stroke is displayed, when it is drawn
    const QPoint onStroke = fixture.device(QPointF(230, 210));
    bool hasStrokePixel = false;
    for (int dx = -3; dx <= 3 && !hasStrokePixel; ++dx)
    {
        for (int dy = -3; dy <= 3 && !hasStrokePixel; ++dy)
        {
            const QColor color = strokeImage.pixelColor(onStroke + QPoint(dx, dy));
            hasStrokePixel = color.red() > color.green() + 40 && color.red() > color.blue() + 40;
        }
    }
    QVERIFY(hasStrokePixel);

    // A click draws nothing, the edit is finished
    QVERIFY(fixture.annotations.beginPartEdit(PartEdit::AddStroke));
    QVERIFY(fixture.press(fixture.device(QPointF(20, 20))));
    QVERIFY(fixture.release(fixture.device(QPointF(20, 20))));
    QCOMPARE(fixture.modificationCount, 1);
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::None);

    // The press out of the page cancels the edit
    QVERIFY(fixture.annotations.beginPartEdit(PartEdit::AddStroke));
    fixture.press(QPoint(-50, -50));
    fixture.release(QPoint(-50, -50));
    QCOMPARE(fixture.annotations.getPartEdit(), PartEdit::None);
    QCOMPARE(fixture.modificationCount, 1);

    // Another area is marked (there is no text on the page, so the area itself is marked)
    fixture.annotations.setSelectedAnnotations({ highlight });
    QVERIFY(!fixture.annotations.beginPartEdit(PartEdit::AddStroke));
    actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(100, 256)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Add Marked Text or Area"));
    QVERIFY(actions.count("Mark Another Text or Area Instead"));
    QVERIFY(!actions.count("Add Stroke"));

    QVERIFY(fixture.annotations.beginPartEdit(PartEdit::AddMarkedAreas));
    QVERIFY(fixture.press(fixture.device(QPointF(50, 230))));
    QVERIFY(fixture.move(fixture.device(QPointF(120, 218))));
    render();
    QVERIFY(fixture.release(fixture.device(QPointF(120, 218))));
    QCOMPARE(fixture.modificationCount, 2);
    QCOMPARE(partCount(highlight), size_t(2));
    QVERIFY(SelectionFixture::fuzzyCompare(PDFAnnotationManipulator::getParts(&fixture.document->getStorage(), highlight).shapes.back().boundingRect(), QRectF(50, 218, 70, 12), 2.0));

    // Another area is marked instead
    QVERIFY(fixture.annotations.beginPartEdit(PartEdit::ReplaceMarkedAreas));
    QVERIFY(fixture.press(fixture.device(QPointF(60, 180))));
    QVERIFY(fixture.move(fixture.device(QPointF(160, 168))));
    QVERIFY(fixture.release(fixture.device(QPointF(160, 168))));
    QCOMPARE(fixture.modificationCount, 3);
    QCOMPARE(partCount(highlight), size_t(1));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(highlight), QRectF(60, 168, 100, 12), 3.0));

    // Nothing is marked
    QVERIFY(fixture.annotations.beginPartEdit(PartEdit::ReplaceMarkedAreas));
    QVERIFY(fixture.press(fixture.device(QPointF(60, 100))));
    QVERIFY(fixture.release(fixture.device(QPointF(60, 100))));
    QCOMPARE(fixture.modificationCount, 3);
    QCOMPARE(partCount(highlight), size_t(1));

    QVERIFY(fixture.annotations.getMarkedShapes(0, QPointF(10, 10), QPointF(10.5, 40)).empty());
    QCOMPARE(fixture.annotations.getMarkedShapes(0, QPointF(10, 40), QPointF(30, 10)), (std::vector<QPolygonF>{ QPolygonF({ QPointF(10, 40), QPointF(30, 40), QPointF(30, 10), QPointF(10, 10) }) }));

    // Functions of the manager
    QVERIFY(!fixture.annotations.addAnnotationParts(highlight, { }));
    QVERIFY(!fixture.annotations.addAnnotationParts(highlight, { QPolygonF({ QPointF(0, 0), QPointF(1, 1) }) }));
    QVERIFY(!fixture.annotations.addAnnotationParts(PDFObjectReference(), { QPolygonF({ QPointF(0, 0), QPointF(1, 1) }) }));
    QVERIFY(!fixture.annotations.setAnnotationParts(highlight, { }));
    QVERIFY(!fixture.annotations.setAnnotationParts(PDFObjectReference(), { }));
    QCOMPARE(fixture.modificationCount, 3);
}

void AnnotationSelectionTest::inkIsErasedAndItsPointsAreEdited()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference ink = builder.createAnnotationInk(page, Polygons{ QPolygonF({ QPointF(50, 100), QPointF(250, 100) }), QPolygonF({ QPointF(50, 200), QPointF(150, 250), QPointF(250, 200) }) },
                                                               2.0, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ ink });

    auto getShapes = [&]() { return PDFAnnotationManipulator::getParts(&fixture.document->getStorage(), ink).shapes; };

    // The stroke is erased at the place of the menu, so it is split
    QVERIFY(SelectionFixture::triggerMenuAction([&]() { fixture.press(fixture.device(QPointF(150, 100)), Qt::NoModifier, Qt::RightButton); }, "Erase Stroke at This Place", nullptr));
    QCOMPARE(fixture.modificationCount, 1);
    Polygons shapes = getShapes();
    QCOMPARE(shapes.size(), size_t(3));
    QVERIFY(shapes[0].back().x() < 150.0 && shapes[0].back().x() > 120.0);
    QVERIFY(shapes[1].front().x() > 150.0 && shapes[1].front().x() < 180.0);

    QVERIFY(!fixture.annotations.eraseAnnotationInk(ink, QPointF(10, 10), 3.0));
    QVERIFY(!fixture.annotations.eraseAnnotationInk(PDFObjectReference(), QPointF(10, 10), 3.0));
    QVERIFY(fixture.annotations.eraseAnnotationInk(ink, QPointF(250, 100), 10.0));
    QCOMPARE(fixture.modificationCount, 2);

    // Points of the strokes are dragged
    shapes = getShapes();
    const QPointF vertex = shapes.back()[1];
    QVERIFY(QLineF(vertex, QPointF(150, 250)).length() < 0.01);
    QVERIFY(fixture.press(fixture.device(vertex)));
    QVERIFY(fixture.move(fixture.device(QPointF(150, 280)), Qt::ControlModifier));

    QImage image(fixture.widget.getDrawWidget()->getWidget()->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        fixture.annotations.drawPostRendering(&painter, image.rect());
    }

    QVERIFY(fixture.release(fixture.device(QPointF(150, 280)), Qt::ControlModifier));
    QCOMPARE(fixture.modificationCount, 3);

    const Polygons editedShapes = getShapes();
    QCOMPARE(editedShapes.size(), shapes.size());
    QVERIFY(QLineF(editedShapes.back()[1], QPointF(150, 280)).length() < 2.0);
    QCOMPARE(editedShapes.back()[0], shapes.back()[0]);
    QCOMPARE(editedShapes.front(), shapes.front());

    // Points of an ink cannot be inserted, nor removed
    std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(editedShapes.back()[1]), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(!actions.count("Delete Point") || !actions["Delete Point"]);
}

void AnnotationSelectionTest::handlesOfLongMarkupAreNearCursor()
{
    // Text markup with forty marked lines
    QPolygonF quadrilaterals;
    for (int i = 0; i < 40; ++i)
    {
        const qreal bottom = 20.0 + i * 6.0;
        quadrilaterals << QPointF(50, bottom + 5) << QPointF(250, bottom + 5) << QPointF(50, bottom) << QPointF(250, bottom);
    }

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, quadrilaterals, Qt::yellow);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ highlight });

    // Returns the count of the red pixels (handles of the points) around the place
    auto countHandlePixels = [&fixture](const QPointF& pagePoint)
    {
        QImage image(fixture.widget.getDrawWidget()->getWidget()->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            fixture.annotations.drawPostRendering(&painter, image.rect());
        }

        int count = 0;
        const QPoint center = fixture.device(pagePoint);
        for (int dx = -4; dx <= 4; ++dx)
        {
            for (int dy = -4; dy <= 4; ++dy)
            {
                const QColor color = image.pixelColor(center + QPoint(dx, dy));
                count += (color.red() > 180 && color.green() < 80 && color.blue() < 80) ? 1 : 0;
            }
        }
        return count;
    };

    // The handles near to the cursor are displayed, all the handles can be dragged
    fixture.move(fixture.device(QPointF(50, 22)), Qt::NoModifier, Qt::NoButton);
    QVERIFY(countHandlePixels(QPointF(50, 22.5)) > 0);
    QCOMPARE(countHandlePixels(QPointF(50, 20.0 + 39 * 6.0 + 2.5)), 0);

    fixture.move(fixture.device(QPointF(250, 20.0 + 39 * 6.0 + 2.5)), Qt::NoModifier, Qt::NoButton);
    QVERIFY(countHandlePixels(QPointF(250, 20.0 + 39 * 6.0 + 2.5)) > 0);
    QCOMPARE(countHandlePixels(QPointF(50, 22.5)), 0);

    QVERIFY(fixture.press(fixture.device(QPointF(250, 20.0 + 39 * 6.0 + 2.5))));
    QVERIFY(fixture.move(fixture.device(QPointF(200, 20.0 + 39 * 6.0 + 2.5)), Qt::ControlModifier));
    QVERIFY(fixture.release(fixture.device(QPointF(200, 20.0 + 39 * 6.0 + 2.5)), Qt::ControlModifier));
    QCOMPARE(fixture.modificationCount, 1);

    const std::vector<PDFReal> quadPoints = fixture.numbers(highlight, "QuadPoints");
    QCOMPARE(quadPoints.size(), size_t(320));
    QVERIFY(std::abs(quadPoints[39 * 8 + 2] - 200.0) < 2.0);
    QVERIFY(std::abs(quadPoints[38 * 8 + 2] - 250.0) < 0.01);
}

void AnnotationSelectionTest::replyIsWrittenInPopup()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ square });

    auto showMenu = [&]() { fixture.press(fixture.device(QPointF(80, 70)), Qt::NoModifier, Qt::RightButton); };

    // The reply is written in the popup window of the annotation
    bool isReplyWritten = false;
    QVERIFY(SelectionFixture::triggerMenuAction(showMenu, "Show Popup Window", [&isReplyWritten](QDialog* dialog)
    {
        QTextEdit* replyEdit = dialog->findChild<QTextEdit*>("replyEdit");
        QPushButton* replyButton = dialog->findChild<QPushButton*>("replyButton");
        if (!replyEdit || !replyButton || replyButton->isEnabled())
        {
            dialog->reject();
            return;
        }

        // Empty reply cannot be sent
        replyEdit->setPlainText("   ");
        if (replyButton->isEnabled())
        {
            dialog->reject();
            return;
        }

        replyEdit->setPlainText("I do not agree.");
        isReplyWritten = replyButton->isEnabled();
        replyButton->click();
    }));
    QVERIFY(isReplyWritten);
    QCOMPARE(fixture.modificationCount, 1);

    const std::vector<PDFObjectReference> replies = PDFAnnotationManipulator::getReplies(&fixture.document->getStorage(), page, square);
    QCOMPARE(replies.size(), size_t(1));
    QCOMPARE(fixture.contents(replies.front()), QString("I do not agree."));
    QVERIFY(fixture.annotations.isAnnotationSelected(square));

    // The popup window displays the reply
    bool isReplyDisplayed = false;
    QVERIFY(SelectionFixture::triggerMenuAction(showMenu, "Show Popup Window", [&isReplyDisplayed](QDialog* dialog)
    {
        for (const QLabel* label : dialog->findChildren<QLabel*>())
        {
            isReplyDisplayed = isReplyDisplayed || label->text() == "I do not agree.";
        }
        dialog->reject();
    }));
    QVERIFY(isReplyDisplayed);
    QCOMPARE(fixture.modificationCount, 1);

    QVERIFY(!fixture.annotations.addAnnotationReply(square, QString()));
    QVERIFY(!fixture.annotations.addAnnotationReply(PDFObjectReference(), "Text"));
    QVERIFY(fixture.annotations.addAnnotationReply(replies.front(), "Why?"));
    QCOMPARE(PDFAnnotationManipulator::getReplies(&fixture.document->getStorage(), page, square).size(), size_t(2));
}

void AnnotationSelectionTest::attachedFileIsReplaced()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference fileSpecification = builder.createFileSpecification("old.txt");
    const PDFObjectReference attachment = builder.createAnnotationFileAttachment(page, QPointF(100, 100), fileSpecification, FileAttachmentIcon::Paperclip, "Title", "Description");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(200, 200, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    fixture.annotations.setSelectedAnnotations({ attachment });
    const QRectF rectangle = fixture.rectangle(attachment);
    std::map<QString, bool> actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(rectangle.center()), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(actions.count("Replace Attached File..."));

    fixture.annotations.setSelectedAnnotations({ square });
    actions = SelectionFixture::menuActions([&]() { fixture.press(fixture.device(QPointF(230, 220)), Qt::NoModifier, Qt::RightButton); });
    QVERIFY(!actions.count("Replace Attached File..."));

    const QByteArray data = "Content of the new file";
    QVERIFY(fixture.annotations.setAnnotationFileAttachment(attachment, "new.txt", data));
    QVERIFY(!fixture.annotations.setAnnotationFileAttachment(square, "new.txt", data));
    QVERIFY(!fixture.annotations.setAnnotationFileAttachment(PDFObjectReference(), "new.txt", data));
    QCOMPARE(fixture.modificationCount, 1);

    const PDFAnnotationPtr parsed = PDFAnnotation::parse(&fixture.document->getStorage(), attachment);
    const PDFFileAttachmentAnnotation* fileAttachment = dynamic_cast<const PDFFileAttachmentAnnotation*>(parsed.data());
    QVERIFY(fileAttachment);
    QCOMPARE(fileAttachment->getFileSpecification().getPlatformFileName(), QString("new.txt"));
    const PDFEmbeddedFile* embeddedFile = fileAttachment->getFileSpecification().getPlatformFile();
    QVERIFY(embeddedFile && embeddedFile->isValid());
    QCOMPARE(fixture.document->getDecodedStream(embeddedFile->getStream()), data);
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(attachment), rectangle, 0.01));
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

void AnnotationSelectionTest::reviewPointDragNoRotate_data()
{
    QTest::addColumn<int>("rotation");
    QTest::newRow("unrotated-control") << 0;
    QTest::newRow("rotate-90") << 90;
    QTest::newRow("rotate-180") << 180;
    QTest::newRow("rotate-270") << 270;
}

void AnnotationSelectionTest::reviewPointDragNoRotate()
{
    QFETCH(int, rotation);
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const QPolygonF points = { QPointF(60, 100), QPointF(140, 100), QPointF(100, 160) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, points, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
    SelectionFixture::setEntry(builder, page, "Rotate", PDFObject::createInteger(rotation));
    SelectionFixture::setEntry(builder, polygon, "F", PDFObject::createInteger(PDFAnnotation::NoRotate));
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({polygon});

    auto displayedPoints = [&]()
    {
        const PDFAnnotationPtr annotation = PDFAnnotation::parse(&fixture.document->getStorage(), polygon);
        QRectF rectangle = annotation->getRectangle();
        const QTransform matrix = fixture.annotations.prepareTransformations(fixture.pageToDevice(), &fixture.widget,
            annotation->getEffectiveFlags(), fixture.document->getCatalog()->getPage(0), rectangle);
        QPolygonF result;
        for (const QPointF& point : PDFAnnotationManipulator::getEditablePoints(annotation.data()).points)
        {
            result << matrix.map(point);
        }
        return result;
    };

    const QPolygonF before = displayedPoints();
    // Moving the top vertex changes the annotation anchor. The other vertices must stay put.
    const QPoint target = before[2].toPoint() + QPoint(30, -20);
    QVERIFY(fixture.press(before[2].toPoint()));
    QVERIFY(fixture.move(target, Qt::ControlModifier));
    QVERIFY(fixture.release(target, Qt::ControlModifier));
    QCOMPARE(fixture.modificationCount, 1);
    const QPolygonF after = displayedPoints();
    qInfo() << "Displayed points before/after:" << before << after << "target:" << target;
    const bool followsCursor = QLineF(after[2], QPointF(target)).length() < 2.0;
    const bool otherPointsStayPut = QLineF(after[0], before[0]).length() < 2.0 && QLineF(after[1], before[1]).length() < 2.0;
    QVERIFY(followsCursor && otherPointsStayPut);
}

void AnnotationSelectionTest::editsOfNoRotateAnnotation_data()
{
    QTest::addColumn<int>("rotation");
    QTest::addColumn<int>("flags");
    QTest::newRow("no-rotate-90") << 90 << int(PDFAnnotation::NoRotate);
    QTest::newRow("no-rotate-180") << 180 << int(PDFAnnotation::NoRotate);
    QTest::newRow("no-zoom-90") << 90 << int(PDFAnnotation::NoZoom);
    QTest::newRow("no-rotate-no-zoom-270") << 270 << int(PDFAnnotation::NoRotate | PDFAnnotation::NoZoom);
}

void AnnotationSelectionTest::editsOfNoRotateAnnotation()
{
    QFETCH(int, rotation);
    QFETCH(int, flags);

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const QPolygonF points = { QPointF(60, 100), QPointF(140, 100), QPointF(140, 140), QPointF(100, 160) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, points, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
    SelectionFixture::setEntry(builder, page, "Rotate", PDFObject::createInteger(rotation));
    SelectionFixture::setEntry(builder, polygon, "F", PDFObject::createInteger(flags));
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ polygon });

    auto displayedPoints = [&]()
    {
        const PDFAnnotationPtr annotation = PDFAnnotation::parse(&fixture.document->getStorage(), polygon);
        QRectF rectangle = annotation->getRectangle();
        const QTransform matrix = fixture.annotations.prepareTransformations(fixture.pageToDevice(), &fixture.widget,
            annotation->getEffectiveFlags(), fixture.document->getCatalog()->getPage(0), rectangle);
        QPolygonF result;
        for (const QPointF& point : PDFAnnotationManipulator::getEditablePoints(annotation.data()).points)
        {
            result << matrix.map(point);
        }
        return result;
    };

    auto isNear = [](const QPointF& left, const QPointF& right) { return QLineF(left, right).length() < 0.5; };

    // The top vertex (it defines the anchor of the displayed annotation) is moved by the
    // keyboard. It moves in the direction of the arrow on the screen, other vertices stay.
    const QPolygonF before = displayedPoints();
    QVERIFY(fixture.key(Qt::Key_Left, Qt::AltModifier));
    QCOMPARE(fixture.annotations.getActivePoint(), 3);

    for (const auto& [key, direction] : { std::pair<Qt::Key, QPointF>{ Qt::Key_Up, QPointF(0, -1) }, std::pair<Qt::Key, QPointF>{ Qt::Key_Left, QPointF(-1, 0) },
                                          std::pair<Qt::Key, QPointF>{ Qt::Key_Down, QPointF(0, 1) }, std::pair<Qt::Key, QPointF>{ Qt::Key_Right, QPointF(1, 0) } })
    {
        const QPolygonF start = displayedPoints();
        QVERIFY(fixture.key(key, Qt::ShiftModifier));
        const QPolygonF moved = displayedPoints();
        const QPointF difference = moved[3] - start[3];
        const qreal distance = QLineF(moved[3], start[3]).length();
        QVERIFY(distance > 2.0);
        QVERIFY(isNear(difference / distance, direction));
        QVERIFY(isNear(moved[0], before[0]) && isNear(moved[1], before[1]) && isNear(moved[2], before[2]));
    }
    QVERIFY(isNear(displayedPoints()[3], before[3]));

    // The vertex is removed
    QVERIFY(fixture.key(Qt::Key_Delete));
    const QPolygonF after = displayedPoints();
    QCOMPARE(after.size(), 3);
    QVERIFY(isNear(after[0], before[0]) && isNear(after[1], before[1]) && isNear(after[2], before[2]));
}

void AnnotationSelectionTest::textBoxOfNoRotateCallout()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None);
    SelectionFixture::setEntry(builder, page, "Rotate", PDFObject::createInteger(90));
    SelectionFixture::setEntry(builder, callout, "F", PDFObject::createInteger(PDFAnnotation::NoRotate));
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ callout });

    auto matrix = [&]()
    {
        const PDFAnnotationPtr annotation = PDFAnnotation::parse(&fixture.document->getStorage(), callout);
        QRectF rectangle = annotation->getRectangle();
        return fixture.annotations.prepareTransformations(fixture.pageToDevice(), &fixture.widget, annotation->getEffectiveFlags(),
                                                          fixture.document->getCatalog()->getPage(0), rectangle);
    };
    auto displayedTextBox = [&]()
    {
        return matrix().mapRect(PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout)).normalized();
    };
    auto displayedTip = [&]()
    {
        const std::vector<PDFReal> calloutLine = fixture.numbers(callout, "CL");
        return matrix().map(QPointF(calloutLine[0], calloutLine[1]));
    };

    // The text box is resized - it is, where the user has put it, and the tip of the callout line stays
    const QRectF before = displayedTextBox();
    const QPointF tipBefore = displayedTip();
    QVERIFY(fixture.press(before.topLeft().toPoint()));
    QVERIFY(fixture.move(before.topLeft().toPoint() - QPoint(30, 20)));
    QVERIFY(fixture.release(before.topLeft().toPoint() - QPoint(30, 20)));
    QCOMPARE(fixture.modificationCount, 1);

    QRectF expected = before;
    expected.setTopLeft(before.topLeft() - QPointF(30, 20));
    QVERIFY(SelectionFixture::fuzzyCompare(displayedTextBox(), expected, 2.0));
    QVERIFY(QLineF(displayedTip(), tipBefore).length() < 2.0);
}

void AnnotationSelectionTest::resizeSnapsToOtherAnnotation()
{
    for (const bool isSnappingEnabled : { true, false })
    {
        TwoSquares squares;
        SelectionFixture fixture(squares.document);
        fixture.annotations.setSelectedAnnotations({ squares.first });

        // The corner of the frame is dragged near to the corner of the other square. Snapping is disabled by Ctrl.
        const Qt::KeyboardModifiers modifiers = isSnappingEnabled ? Qt::NoModifier : Qt::ControlModifier;
        QVERIFY(fixture.press(fixture.device(QPointF(110, 90))));
        QVERIFY(fixture.move(fixture.device(QPointF(148, 148)), modifiers));
        QVERIFY(fixture.release(fixture.device(QPointF(148, 148)), modifiers));
        QCOMPARE(fixture.modificationCount, 1);

        const QRectF rectangle = fixture.rectangle(squares.first);
        QVERIFY(SelectionFixture::fuzzyCompare(rectangle, QRectF(50, 50, 98, 98), 1.0) != isSnappingEnabled);
        QCOMPARE(SelectionFixture::fuzzyCompare(rectangle, QRectF(50, 50, 100, 100), 0.01), isSnappingEnabled);
    }
}

void AnnotationSelectionTest::dragAndDropSnaps()
{
    for (const bool isSnappingEnabled : { true, false })
    {
        TwoSquares squares;
        SelectionFixture fixture(squares.document);
        fixture.annotations.setSelectedAnnotations({ squares.first });
        QVERIFY(!fixture.annotations.createAnnotationDragData());

        // The drag and drop operation is prepared by the press on the selected annotation
        QVERIFY(fixture.press(fixture.device(QPointF(80, 70))));
        std::unique_ptr<QMimeData> data(fixture.annotations.createAnnotationDragData());
        QVERIFY(data);
        QVERIFY(fixture.annotations.canAcceptAnnotationDrag(data.get()));

        // The corner of the dragged square is near to the corner of the other square
        const QPoint dropPosition = fixture.device(QPointF(218, 172));
        fixture.annotations.updateAnnotationDropFeedback(data.get(), dropPosition, isSnappingEnabled);
        const std::optional<QPointF> snapPoint = fixture.annotations.getAnnotationDropSnapPoint();
        QCOMPARE(snapPoint.has_value(), isSnappingEnabled);
        if (snapPoint)
        {
            QVERIFY(QLineF(*snapPoint, fixture.pageToDevice().map(QPointF(190, 150))).length() < 0.5);
        }

        QImage image(fixture.widget.getDrawWidget()->getWidget()->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            fixture.annotations.drawPostRendering(&painter, image.rect());
        }

        QVERIFY(fixture.annotations.handleAnnotationDrop(data.get(), dropPosition, Qt::MoveAction, isSnappingEnabled));
        QVERIFY(!fixture.annotations.getAnnotationDropSnapPoint());
        QCOMPARE(fixture.modificationCount, 1);
        // The position of the cursor is rounded to pixels, if the annotation does not snap
        QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), isSnappingEnabled ? QRectF(190, 150, 60, 40) : QRectF(188, 152, 60, 40), 2.0));
        QCOMPARE(SelectionFixture::fuzzyCompare(fixture.rectangle(squares.first), QRectF(190, 150, 60, 40), 0.01), isSnappingEnabled);
        fixture.release(dropPosition);

        // The feedback is cleared, when the cursor leaves the widget
        fixture.annotations.updateAnnotationDropFeedback(data.get(), QPoint(-100, -100), true);
        fixture.annotations.clearAnnotationDropFeedback();
        QVERIFY(!fixture.annotations.getAnnotationDropSnapPoint());
    }
}

void AnnotationSelectionTest::dragAndDropMovesTextBox()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());
    fixture.annotations.setSelectedAnnotations({ callout });

    // The text box is dragged - the tip of the callout line stays
    QVERIFY(fixture.press(fixture.device(QPointF(150, 125))));
    std::unique_ptr<QMimeData> data(fixture.annotations.createAnnotationDragData());
    QVERIFY(data);
    QVERIFY(fixture.annotations.handleAnnotationDrop(data.get(), fixture.device(QPointF(180, 165)), Qt::MoveAction, false));
    fixture.release(fixture.device(QPointF(180, 165)));
    QCOMPARE(fixture.modificationCount, 1);

    QVERIFY(SelectionFixture::fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout), QRectF(140, 150, 80, 30), 2.0));
    std::vector<PDFReal> calloutLine = fixture.numbers(callout, "CL");
    QVERIFY(std::abs(calloutLine[0] - 20.0) < 0.01 && std::abs(calloutLine[1] - 20.0) < 0.01);

    // The whole annotation is dragged by its callout line
    const QRectF textRectangle = PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout);
    const QPointF onLine = (QPointF(calloutLine[0], calloutLine[1]) + QPointF(calloutLine[2], calloutLine[3])) * 0.5;
    QVERIFY(fixture.press(fixture.device(onLine)));
    data.reset(fixture.annotations.createAnnotationDragData());
    QVERIFY(data);
    QVERIFY(fixture.annotations.handleAnnotationDrop(data.get(), fixture.device(onLine + QPointF(30, 20)), Qt::MoveAction, false));
    fixture.release(fixture.device(onLine + QPointF(30, 20)));
    QCOMPARE(fixture.modificationCount, 2);

    QVERIFY(SelectionFixture::fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout), textRectangle.translated(30, 20), 2.0));
    calloutLine = fixture.numbers(callout, "CL");
    QVERIFY(std::abs(calloutLine[0] - 50.0) < 2.0 && std::abs(calloutLine[1] - 40.0) < 2.0);
}

void AnnotationSelectionTest::interactionScope()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference first = builder.createAnnotationSquare(page1, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference second = builder.createAnnotationSquare(page1, QRectF(150, 150, 40, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference third = builder.createAnnotationSquare(page2, QRectF(50, 50, 60, 40), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    SelectionFixture fixture(builder.build());

    // Handles and dragging work with the annotations of a single page. If the selection
    // spans several pages, then the user is told about it during the whole interaction.
    fixture.annotations.setSelectedAnnotations({ first, second });
    QVERIFY(fixture.annotations.getInteractionScopeText(0).isEmpty());

    fixture.annotations.setSelectedAnnotations({ first, second, third });
    QVERIFY(fixture.annotations.getInteractionScopeText(0).contains("2 of 3"));
    QVERIFY(fixture.annotations.getInteractionScopeText(1).contains("1 of 3"));

    // The handles change the annotations of the page (the text is a part of the feedback of
    // the interaction; fonts are not available on the offscreen platform, so it is just drawn)
    const QRectF frame = fixture.pageToDevice().mapRect(QRectF(50, 50, 140, 140)).normalized();
    QVERIFY(fixture.press(frame.bottomRight().toPoint()));
    QVERIFY(fixture.move(frame.bottomRight().toPoint() + QPoint(40, 40), Qt::ControlModifier));

    QImage image(fixture.widget.getDrawWidget()->getWidget()->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        fixture.annotations.drawPostRendering(&painter, image.rect());
    }

    QVERIFY(fixture.release(frame.bottomRight().toPoint() + QPoint(40, 40), Qt::ControlModifier));
    QCOMPARE(fixture.modificationCount, 1);
    QVERIFY(!SelectionFixture::fuzzyCompare(fixture.rectangle(first), QRectF(50, 50, 60, 40), 1.0));
    QVERIFY(SelectionFixture::fuzzyCompare(fixture.rectangle(third), QRectF(50, 50, 60, 40), 0.01));
}

void AnnotationSelectionTest::previewIsResultOfOperation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 300));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "100 pt", AnnotationLineEnding::None, AnnotationLineEnding::None, 0.0, 0.0, 0.0, true, true);
    SelectionFixture::setEntry(builder, line, "IT", PDFObject::createName("LineDimension"));
    const PDFObjectReference stamp = builder.createAnnotationStamp(page, QRectF(20, 20, 120, 40), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(160, 160, 130, 130), QRectF(210, 250, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(170, 170), QPointF(210, 260), AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None);
    SelectionFixture fixture(builder.build());

    // Point of a measurement is dragged - the preview displays the new measured value
    fixture.annotations.setSelectedAnnotations({ line });
    QVERIFY(!fixture.annotations.getInteractionPreview(line));
    QVERIFY(fixture.press(fixture.device(QPointF(150, 150))));
    QVERIFY(fixture.move(fixture.device(QPointF(250, 150)), Qt::ControlModifier));
    PDFAnnotationPtr preview = fixture.annotations.getInteractionPreview(line);
    QVERIFY(preview);
    QCOMPARE(preview->getContents(), QString("200 pt"));
    QCOMPARE(fixture.contents(line), QString("100 pt"));
    QCOMPARE(fixture.modificationCount, 0);

    QImage image(fixture.widget.getDrawWidget()->getWidget()->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        fixture.annotations.drawPostRendering(&painter, image.rect());
    }
    // The preview of the line is drawn - there is a red pixel behind the old end of the line
    const QPoint onNewPart = fixture.device(QPointF(220, 150));
    bool hasLinePixel = false;
    for (int dy = -2; dy <= 2; ++dy)
    {
        const QColor color = image.pixelColor(onNewPart + QPoint(0, dy));
        hasLinePixel = hasLinePixel || (color.red() > color.green() + 40 && color.red() > color.blue() + 40);
    }
    QVERIFY(hasLinePixel);

    QVERIFY(fixture.release(fixture.device(QPointF(250, 150)), Qt::ControlModifier));
    QCOMPARE(fixture.contents(line), QString("200 pt"));
    QVERIFY(!fixture.annotations.getInteractionPreview(line));

    // Text box is resized - the preview has the new text box
    fixture.annotations.setSelectedAnnotations({ callout });
    const QRectF textFrame = fixture.pageToDevice().mapRect(QRectF(210, 250, 80, 30)).normalized();
    QVERIFY(fixture.press(textFrame.topLeft().toPoint()));
    QVERIFY(fixture.move(textFrame.topLeft().toPoint() - QPoint(20, 20), Qt::ControlModifier));
    preview = fixture.annotations.getInteractionPreview(callout);
    QVERIFY(preview);
    const PDFFreeTextAnnotation* freeTextPreview = dynamic_cast<const PDFFreeTextAnnotation*>(preview.data());
    QVERIFY(freeTextPreview);
    QVERIFY(freeTextPreview->getTextRectangle().width() > 85.0);
    QVERIFY(freeTextPreview->getTextRectangle().height() > 35.0);
    QVERIFY(SelectionFixture::fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&fixture.document->getStorage(), callout), QRectF(210, 250, 80, 30), 0.01));

    // Escape cancels the interaction, nothing is changed
    QVERIFY(fixture.key(Qt::Key_Escape));
    QVERIFY(!fixture.annotations.getInteractionPreview(callout));
    QCOMPARE(fixture.modificationCount, 1);

    // A stamp is displayed by its appearance stream, which is transformed by the preview.
    // A square, which is resized together with it, is displayed as the result of the operation.
    const QRectF stampFrame = fixture.pageToDevice().mapRect(QRectF(20, 20, 120, 40)).normalized();
    fixture.annotations.setSelectedAnnotations({ stamp });
    QVERIFY(fixture.press(stampFrame.bottomRight().toPoint()));
    QVERIFY(fixture.move(stampFrame.bottomRight().toPoint() + QPoint(20, 20), Qt::ControlModifier));
    QVERIFY(!fixture.annotations.getInteractionPreview(stamp));
    QVERIFY(fixture.key(Qt::Key_Escape));
    QCOMPARE(fixture.modificationCount, 1);
}

QTEST_MAIN(AnnotationSelectionTest)

#include "tst_annotationselectiontest.moc"
