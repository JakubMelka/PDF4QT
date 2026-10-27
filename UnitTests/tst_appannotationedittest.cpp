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

#include "appdocumentfixture.h"

#include "pdfappannotationcontroller.h"
#include "pdfappannotationbar.h"
#include "pdfappannotationeditcommands.h"
#include "pdfappannotationgeometrydialog.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationmenus.h"
#include "pdfappannotationoperations.h"
#include "pdfappannotationpropertiesdialog.h"
#include "pdfappcolorpicker.h"
#include "pdfappcopytopagesdialog.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapppage.h"
#include "pdfappundostack.h"
#include "pdfsettingdefinitions.h"

#include "pdfadvancedtools.h"
#include "pdfannotationmanipulator.h"
#include "pdfannotationproperties.h"
#include "pdfdocumentbuilder.h"
#include "pdfform.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfreferencepointpicker.h"
#include "pdfthemecolors.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAction>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QMimeData>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QTranslator>
#include <QToolButton>
#include <QtTest>

#include <cmath>
#include <memory>

/// Translator of the test: every text of the library is marked, so a retranslated
/// text is seen
class AnnotationEditTestTranslator : public QTranslator
{
public:
    explicit AnnotationEditTestTranslator(QObject* parent) :
        QTranslator(parent)
    {
    }

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (!sourceText || !context || !QByteArray(context).startsWith("pdf::"))
        {
            return QString();
        }
        return QStringLiteral("\u00A7") + QString::fromUtf8(sourceText);
    }
};

/// Helpers of the tests of package B (editing of the annotations on the page)
class AppAnnotationEditTestHelper
{
public:
    AppAnnotationEditTestHelper() = delete;

    /// Reference of the annotation of the page with the subject (Subj) of the
    /// samples of AppDocumentSamples::annotations
    static pdf::PDFObjectReference find(const pdf::PDFAppDocument* document, int pageIndex, const QString& subject)
    {
        const pdf::PDFDocumentPointer pdfDocument = document ? document->getDocument() : pdf::PDFDocumentPointer();
        const pdf::PDFPage* page = pdfDocument ? pdfDocument->getCatalog()->getPage(pageIndex) : nullptr;
        if (!page)
        {
            return pdf::PDFObjectReference();
        }

        pdf::PDFDocumentDataLoaderDecorator loader(pdfDocument.data());
        for (const pdf::PDFObjectReference& reference : page->getAnnotations())
        {
            const pdf::PDFObject& object = pdfDocument->getObjectByReference(reference);
            if (object.isDictionary() && loader.readTextStringFromDictionary(object.getDictionary(), "Subj", QString()) == subject)
            {
                return reference;
            }
        }
        return pdf::PDFObjectReference();
    }

    /// The annotation exists in the current document
    static bool exists(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference)
    {
        return document && document->getDocument() && document->getDocument()->getObjectByReference(reference).isDictionary();
    }

    /// Opens the sample with annotations and waits for the shown document
    static pdf::PDFAppDocumentTab* openAnnotations(pdftest::AppDocumentFixture& fixture)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
        return tab && tab->getDocument() && tab->getDocument()->isReady() ? tab : nullptr;
    }

    /// Edit command handler of the tab (the user interface of the requests)
    static pdf::PDFAppAnnotationEditCommandHandler* handler(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? tab->findChild<pdf::PDFAppAnnotationEditCommandHandler*>() : nullptr;
    }

    /// Picture of a widget (DPR \p ratio)
    static QImage render(QWidget* widget, qreal ratio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * ratio), qCeil(widget->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::transparent);
        widget->render(&image, QPoint(), QRegion(), widget->isWindow() ? QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground)
                                                                         : QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// Pixels of the leak color (a color of the system leaking into the user interface)
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb();
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if (qAlpha(line[x]) == 255 && (line[x] & 0x00FFFFFF) == (leak & 0x00FFFFFF))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// Writes the picture into the folder of PDF4QT_APP_SCREENSHOTS (nothing without it)
    static void save(const QString& name, const QImage& image)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        if (!folder.isEmpty())
        {
            QDir().mkpath(folder);
            image.save(QDir(folder).absoluteFilePath(name + QStringLiteral(".png")));
        }
    }

    /// Picture of the window with the popups (menus) drawn at their places
    static QImage renderWithPopups(QWidget* window, const QList<QWidget*>& popups, qreal ratio = 1.0)
    {
        QImage image = render(window, ratio);
        QPainter painter(&image);
        for (QWidget* popup : popups)
        {
            if (popup && popup->isVisible())
            {
                painter.drawImage(QPointF(popup->mapToGlobal(QPoint(0, 0)) - window->mapToGlobal(QPoint(0, 0))), render(popup, ratio));
            }
        }
        return image;
    }

    /// Menu request of the selection of the controller (as More of the bar asks it)
    static pdf::PDFAnnotationMenuRequest selectionRequest(pdf::PDFAppAnnotationController* controller)
    {
        pdf::PDFAnnotationMenuRequest request;
        request.kind = pdf::PDFAnnotationMenuRequest::Kind::Selection;
        request.selection = controller->getSelectedAnnotations();
        request.annotation = request.selection.size() == 1 ? request.selection.front() : pdf::PDFObjectReference();
        request.isModificationAllowed = controller->isAnnotationEditingAllowed();
        request.canPaste = controller->canPaste();
        request.globalPosition = QPoint(100, 60);
        return request;
    }

    /// Properties of the annotation of the current document
    static pdf::PDFAnnotationProperties properties(const pdf::PDFAppDocument* document, pdf::PDFObjectReference annotation)
    {
        return pdf::PDFAnnotationPropertyEditor::read(&document->getDocument()->getStorage(), annotation);
    }

    /// Closes the dialog of the handler and processes the events
    static void closeDialog(pdf::PDFAppDocumentTab* tab)
    {
        if (pdf::PDFAppAnnotationEditCommandHandler* editHandler = handler(tab))
        {
            if (QDialog* dialog = qobject_cast<QDialog*>(editHandler->getOpenDialog()))
            {
                dialog->reject();
            }
        }
        pdftest::AppDocumentFixture::settle();
    }
};

class AppAnnotationEditTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void controllerCommitsEngineChanges();
    void controllerRefusesWhenLocked();
    void queuedRequestsSurviveTabDeletion();
    void editCommandsWaveZero();
    void commentPopupCompose();
    void commentPopupThread();
    void commentPopupAnnotationRemoved();
    void selectionWithTextTool();
    void rubberBand();
    void altClickNext();
    void overlapChooser();
    void annotationBarSingle();
    void annotationBarMulti();
    void liveColorWidthOneStep();
    void mergeSessionClosed();
    void contextMenuSingle();
    void contextMenuMulti();
    void contextMenuHiddenAndReplies();
    void pointAndPartMenu();
    void propertiesDialogVariants();
    void propertiesApplyOneStep();
    void propertiesReadOnly();
    void propertiesLink();
    void propertiesBlendMode();
    void linksMoveAndResize();
    void geometryDialog();
    void copyToPagesOneStep();
    void hideOnPage();
    void clipboardCommands();
    void dragBetweenTabs();
    void keyboardNudge();
    void lockedDocument();
    void lockedAnnotation();
    void undoRedoSelection();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void accessibility();
    void languageChange();
    void annotationInfoTexts();
    void reviewMergeSessionOnlyBarChanges();
    void reviewPendingDescriptionNotLeaked();
    void reviewWidthListNoDuplicates();
    void reviewMenuReferenceRoundTrip();
    void reviewGeometryKeepsUnchangedValues();
    void reviewPropertiesUnchangedSpinValues();
    void reviewPropertiesLinkTarget();
    void reviewPropertiesLanguageChange();
    void reviewDialogsLanguageChange();
    void reviewDragOverTabBar();
    void reviewAuthorBidiAndMnemonics();
    void reviewCopyToPagesLimit();
    void reviewCommitBlockedByOperation();
    void reviewOperationsHostileArguments();
    void reviewBarAndPopupUpdates();
    void moreColorsDialog();
};

void AppAnnotationEditTest::initTestCase()
{
    // The style of the application (the sizes of the design, the leak palette)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppAnnotationEditTest::controllerCommitsEngineChanges()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    QVERIFY(controller);
    pdf::PDFWidgetAnnotationManager* manager = controller->getAnnotationManager();
    QVERIFY(manager);

    // The engine runs in the delegated mode of the new application
    QVERIFY(manager->isUserInterfaceDelegated());
    QVERIFY(controller->getToolManager()->isUserInterfaceDelegated());
    QVERIFY(controller->getToolManager()->isCreationToolsPersistent());
    QVERIFY(controller->getToolManager()->getSelectTextTool()->isAnnotationsSelectable());
    QVERIFY(controller->isAnnotationEditingAllowed());
    QVERIFY(controller->isFormFillingAllowed());
    QVERIFY(!manager->isReadOnly());
    QVERIFY(controller->getEditingRefusalReason().isEmpty());

    // Selection through the panel API (stale and foreign references are dropped)
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));
    QVERIFY(square.isValid());
    QVERIFY(circle.isValid());
    QSignalSpy selectionSpy(controller, &pdf::PDFAppAnnotationController::selectionChanged);
    controller->selectAnnotations({ square, pdf::PDFObjectReference(999999, 0) });
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ square }));
    QVERIFY(controller->hasAnnotationSelection());
    // (the signal of the controller comes from the event loop - not inside of the
    // emission of the engine, so its slots may delete the tab)
    QCOMPARE(selectionSpy.count(), 0);
    QTRY_COMPARE(selectionSpy.count(), 1);

    // An engine change is one undo step with the flag Annotation
    QSignalSpy documentSpy(document, &pdf::PDFAppDocument::documentChanged);
    QSignalSpy committedSpy(controller, &pdf::PDFAppAnnotationController::changeCommitted);
    const quint64 version = document->getVersion();
    manager->setSelectedAnnotationsColor(QColor(Qt::blue));
    QCOMPARE(document->getVersion(), version + 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), pdf::PDFAppAnnotationController::getDefaultDescription(pdf::PDFAppAnnotationChangeKind::Annotation));
    QCOMPARE(committedSpy.count(), 1);
    QVERIFY(!documentSpy.isEmpty());
    QVERIFY(documentSpy.last().at(0).value<pdf::PDFModifiedDocument>().hasFlag(pdf::PDFModifiedDocument::Annotation));
    QVERIFY(document->isModified());

    // A merge session (live color of the bar, O20): many changes = one step
    controller->beginMergeSession(pdf::PDFAppAnnotationController::createMergeKey(QStringLiteral("color"), controller->getSelectedAnnotations(), 1));
    QVERIFY(controller->isMergeSessionOpen());
    manager->setSelectedAnnotationsColor(QColor(Qt::green));
    manager->setSelectedAnnotationsColor(QColor(Qt::yellow));
    manager->setSelectedAnnotationsColor(QColor(Qt::magenta));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(document->getVersion(), version + 4);
    controller->endMergeSession();
    QVERIFY(!controller->isMergeSessionOpen());
    manager->setSelectedAnnotationsColor(QColor(Qt::red));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);

    // A change of the selection ends the session
    controller->beginMergeSession(pdf::PDFAppAnnotationController::createMergeKey(QStringLiteral("width"), controller->getSelectedAnnotations(), 2));
    manager->setSelectedAnnotationsBorderWidth(3.0);
    controller->selectAnnotations({ square, circle });
    QVERIFY(!controller->isMergeSessionOpen());
    manager->setSelectedAnnotationsBorderWidth(4.0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 5);

    // The merge keys differ by the property, the selection and the counter
    const QString key = pdf::PDFAppAnnotationController::createMergeKey(QStringLiteral("color"), { square }, 1);
    QVERIFY(key != pdf::PDFAppAnnotationController::createMergeKey(QStringLiteral("width"), { square }, 1));
    QVERIFY(key != pdf::PDFAppAnnotationController::createMergeKey(QStringLiteral("color"), { circle }, 1));
    QVERIFY(key != pdf::PDFAppAnnotationController::createMergeKey(QStringLiteral("color"), { square }, 2));
    QVERIFY(pdf::PDFAppAnnotationController::createMergeKey(QString(10000, QLatin1Char('x')), std::vector<pdf::PDFObjectReference>(10000, square), 3).size() < 200);

    // The pending description is used once
    controller->setPendingDescription(QStringLiteral("Move annotation"));
    manager->setSelectedAnnotationsOpacity(0.5);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Move annotation"));
    QVERIFY(controller->getPendingDescription().isEmpty());
    manager->setSelectedAnnotationsOpacity(0.75);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), pdf::PDFAppAnnotationController::getDefaultDescription(pdf::PDFAppAnnotationChangeKind::Annotation));

    // Undo / redo keep the selection of the annotations, which still exist
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(2));
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(2));
    QVERIFY(document->getUndoStack()->redo());
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(2));

    // Delete of the selection: one step, the annotations are gone, undo brings them back
    const int count = pdftest::AppDocumentFixture::undoCount(document);
    controller->deleteSelection();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), count + 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Delete annotations"));
    QVERIFY(!H::find(document, 0, QStringLiteral("square")).isValid());
    QVERIFY(document->getUndoStack()->undo());
    QVERIFY(H::find(document, 0, QStringLiteral("square")).isValid());

    // A change of the application through the controller (flag FormField of the kind Form)
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("Changed by the form"));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(controller->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), pdf::PDFAppAnnotationChangeKind::Form));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), pdf::PDFAppAnnotationController::getDefaultDescription(pdf::PDFAppAnnotationChangeKind::Form));
    QVERIFY(documentSpy.last().at(0).value<pdf::PDFModifiedDocument>().hasFlag(pdf::PDFModifiedDocument::FormField));

    // A change without a shared document is refused (nothing is committed)
    QVERIFY(!controller->commit(pdf::PDFModifiedDocument(), pdf::PDFAppAnnotationChangeKind::Annotation));

    // Clipboard: copy and paste in place are one step
    controller->selectAnnotations({ circle });
    controller->copySelection();
    QVERIFY(controller->canPaste());
    const int beforePaste = pdftest::AppDocumentFixture::undoCount(document);
    controller->paste(true);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), beforePaste + 1);
}

void AppAnnotationEditTest::controllerRefusesWhenLocked()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFWidgetAnnotationManager* manager = controller->getAnnotationManager();
    pdf::PDFToolManager* toolManager = controller->getToolManager();

    // A creation tool of package A (the application creates it without actions)
    QPointer<pdf::PDFCreateHighlightTextTool> tool = new pdf::PDFCreateHighlightTextTool(view->getProxy(), toolManager, nullptr, view);
    controller->addTool(tool);
    QVERIFY(controller->isCreationTool(tool));
    QVERIFY(!controller->isCreationTool(toolManager->getSelectTextTool()));
    QSignalSpy toolSpy(controller, &pdf::PDFAppAnnotationController::activeToolChanged);
    QVERIFY(controller->activateTool(tool));
    QCOMPARE(controller->getActiveTool(), tool.data());
    QVERIFY(!toolSpy.isEmpty());

    QAction* annotateAction = fixture.window->getCommandAction(pdf::PDFAppCommands::annotatePanel());
    QVERIFY(annotateAction);
    QVERIFY(annotateAction->isEnabled());

    // Lock: read-only engine, read-only form, the tool ends, the commands go off
    QSignalSpy editingSpy(controller, &pdf::PDFAppAnnotationController::editingAllowedChanged);
    document->setLocked(true);
    fixture.settle();
    QCOMPARE(editingSpy.count(), 1);
    QVERIFY(!controller->isAnnotationEditingAllowed());
    QVERIFY(!controller->isFormFillingAllowed());
    QVERIFY(manager->isReadOnly());
    QVERIFY(controller->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    QVERIFY(controller->getActiveTool() != tool.data());
    QCOMPARE(controller->getEditingRefusalReason(), QObject::tr("The document is locked. Unlock it to make changes."));
    QVERIFY(!annotateAction->isEnabled());
    QVERIFY(!view->getToolbar()->getAnnotateButton()->isEnabled());
    QCOMPARE(view->getToolbar()->getAnnotateButton()->toolTip(), controller->getEditingRefusalReason());
    QVERIFY(!controller->activateTool(tool));

    // The selection still works (1030:161012), a change is refused with a status message
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    QVERIFY(controller->hasAnnotationSelection());
    QSignalSpy refusedSpy(controller, &pdf::PDFAppAnnotationController::changeRefused);
    const quint64 version = document->getVersion();
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("Refused"));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(!controller->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), pdf::PDFAppAnnotationChangeKind::Annotation));
    QCOMPARE(refusedSpy.count(), 1);
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(view->getStatusBar()->getMessage(), QObject::tr("The document is locked. Unlock it to make changes."));
    manager->setSelectedAnnotationsColor(QColor(Qt::green));
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

    // Unlocked again: everything comes back
    document->setLocked(false);
    fixture.settle();
    QCOMPARE(editingSpy.count(), 2);
    QVERIFY(controller->isAnnotationEditingAllowed());
    QVERIFY(!manager->isReadOnly());
    QVERIFY(!controller->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    QVERIFY(annotateAction->isEnabled());
    QVERIFY(view->getToolbar()->getAnnotateButton()->isEnabled());

    // A setting of the forms keeps the read-only form of a locked document
    document->setLocked(true);
    fixture.settle();
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::formsHighlightFields(), false).isError());
    view->applySettings();
    QVERIFY(controller->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    document->setLocked(false);

    // A document without the permissions (O8: another text)
    pdf::PDFAppDocumentTab* restricted = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(restricted);
    QVERIFY(restricted->getDocument()->isReady());
    pdf::PDFAppAnnotationController* restrictedController = restricted->getView()->getAnnotationController();
    QVERIFY(!restrictedController->isAnnotationEditingAllowed());
    QVERIFY(!restrictedController->isFormFillingAllowed());
    QVERIFY(restrictedController->getAnnotationManager()->isReadOnly());
    QVERIFY(restrictedController->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    QCOMPARE(restrictedController->getEditingRefusalReason(), QObject::tr("The document does not allow changes."));
    QVERIFY(!fixture.window->getCommandAction(pdf::PDFAppCommands::insertPanel())->isEnabled());

    // The permissions are checked by the commit itself (security review RS4): an
    // operation computed outside of the user interface (a dialog opened before a
    // reload of a restricted file, a future caller) is refused, nothing is changed
    pdf::PDFAppDocument* restrictedDocument = restricted->getDocument();
    const pdf::PDFObjectReference restrictedSquare = H::find(restrictedDocument, 0, QStringLiteral("square"));
    QVERIFY(restrictedSquare.isValid());
    const pdf::PDFAppAnnotationResult hidden = pdf::PDFAppAnnotationOperations::setAnnotationsHidden(restrictedDocument->getDocument(), { restrictedSquare }, true);
    QVERIFY(hidden);
    const quint64 restrictedVersion = restrictedDocument->getVersion();
    QSignalSpy restrictedRefusedSpy(restrictedController, &pdf::PDFAppAnnotationController::changeRefused);
    for (const pdf::PDFAppAnnotationChangeKind kind : { pdf::PDFAppAnnotationChangeKind::Annotation, pdf::PDFAppAnnotationChangeKind::Tool, pdf::PDFAppAnnotationChangeKind::Form })
    {
        QVERIFY(!restrictedController->commit(*hidden, kind));
    }
    QCOMPARE(restrictedRefusedSpy.count(), 3);
    QCOMPARE(restrictedRefusedSpy.front().front().toString(), QObject::tr("The document does not allow changes."));
    QCOMPARE(restrictedDocument->getVersion(), restrictedVersion);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(restrictedDocument), 0);
}

void AppAnnotationEditTest::queuedRequestsSurviveTabDeletion()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    QPointer<pdf::PDFAppAnnotationController> controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(tab->getDocument(), 0, QStringLiteral("square"));

    // The requests come from the event loop, never inside of the call
    auto emitted = std::make_shared<int>(0);
    QObject receiver;
    auto count = [emitted]() { ++(*emitted); };
    connect(controller.data(), &pdf::PDFAppAnnotationController::propertiesRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::geometryRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::commentRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::replyRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::copyToPagesRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::menuRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::popupRequested, &receiver, count);
    connect(controller.data(), &pdf::PDFAppAnnotationController::creationRequested, &receiver, count);

    controller->requestProperties(square);
    controller->requestGeometry(square);
    controller->requestComment(square, true);
    controller->requestReply(square);
    controller->requestCopyToPages();
    controller->requestMenu(pdf::PDFAnnotationMenuRequest());
    QCOMPARE(*emitted, 0);
    fixture.settle();
    QCOMPARE(*emitted, 6);

    // The requests of the engine are queued as well (E3 / E4)
    pdf::PDFWidgetAnnotationManager* manager = controller->getAnnotationManager();
    Q_EMIT manager->propertiesRequested(square);
    Q_EMIT manager->popupRequested(square, pdf::PDFObjectReference(), QPoint(10, 10));
    pdf::PDFAnnotationCreationRequest creation;
    creation.kind = pdf::PDFAnnotationCreationRequest::Kind::StickyNote;
    Q_EMIT controller->getToolManager()->annotationCreationRequested(creation);
    QCOMPARE(*emitted, 6);
    fixture.settle();
    QCOMPARE(*emitted, 9);

    // A slot deleting the tab from the request (the controller dies inside of its signal)
    auto deleteTab = [tab]() { delete tab.data(); };
    connect(controller.data(), &pdf::PDFAppAnnotationController::propertiesRequested, &receiver, deleteTab);
    controller->requestProperties(square);
    fixture.settle();
    QVERIFY(!tab);
    QVERIFY(!controller);

    // Pending requests of a tab deleted before the event loop are dropped
    QPointer<pdf::PDFAppDocumentTab> second = H::openAnnotations(fixture);
    QVERIFY(second);
    QPointer<pdf::PDFAppAnnotationController> secondController = second->getView()->getAnnotationController();
    connect(secondController.data(), &pdf::PDFAppAnnotationController::propertiesRequested, &receiver, count);
    connect(secondController.data(), &pdf::PDFAppAnnotationController::menuRequested, &receiver, count);
    const int before = *emitted;
    secondController->requestProperties(square);
    Q_EMIT secondController->getAnnotationManager()->menuRequested(pdf::PDFAnnotationMenuRequest());
    delete second.data();
    fixture.settle();
    QVERIFY(!secondController);
    QCOMPARE(*emitted, before);

    // The window survives with its other tabs
    QVERIFY(fixture.window);
}

void AppAnnotationEditTest::editCommandsWaveZero()
{
    using H = AppAnnotationEditTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();

    // Without a selection the commands of a selection are disabled, Select all is not
    QVERIFY(!tab->isCommandEnabled(C::deleteSelection()));
    QVERIFY(!tab->isCommandEnabled(C::annotationProperties()));
    QVERIFY(tab->isCommandEnabled(C::annotationSelectAll()));
    QVERIFY(!tab->isCommandEnabled(C::annotationRotateClockwise()));

    // Hide on page (one step), then Delete by the command
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));
    controller->selectAnnotations({ circle });
    QVERIFY(tab->isCommandEnabled(C::annotationHide()));
    QVERIFY(tab->isCommandEnabled(C::annotationProperties()));
    QVERIFY(!tab->isCommandEnabled(C::annotationAlignLeft()));
    QVERIFY(tab->executeCommand(C::annotationHide()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Hide annotations"));
    QVERIFY(document->getUndoStack()->undo());

    // Copy is routed to the annotations, when some are selected
    controller->selectAnnotations({ circle });
    QVERIFY(tab->isCommandEnabled(C::copy()));
    QVERIFY(tab->executeCommand(C::copy()));
    QVERIFY(controller->canPaste());

    // Properties come from the event loop
    QSignalSpy propertiesSpy(controller, &pdf::PDFAppAnnotationController::propertiesRequested);
    QVERIFY(tab->executeCommand(C::annotationProperties()));
    QCOMPARE(propertiesSpy.count(), 0);
    fixture.settle();
    QCOMPARE(propertiesSpy.count(), 1);
    QCOMPARE(propertiesSpy.at(0).at(0).value<pdf::PDFObjectReference>(), circle);

    // Locked: only Properties / Comment / Select all stay
    document->setLocked(true);
    fixture.settle();
    QVERIFY(!tab->isCommandEnabled(C::deleteSelection()));
    QVERIFY(!tab->isCommandEnabled(C::annotationHide()));
    QVERIFY(!tab->isCommandEnabled(C::paste()));
    QVERIFY(tab->isCommandEnabled(C::annotationProperties()));
    QVERIFY(tab->isCommandEnabled(C::annotationComment()));
    QVERIFY(tab->isCommandEnabled(C::annotationSelectAll()));
    document->setLocked(false);
}

void AppAnnotationEditTest::commentPopupCompose()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    QVERIFY(popup);
    QCOMPARE(popup->parentWidget(), view->getCanvasArea());

    // Compose: the empty text field with Post disabled, next to the anchor
    QSignalSpy acceptedSpy(popup, &pdf::PDFAppCommentPopup::composeAccepted);
    QSignalSpy canceledSpy(popup, &pdf::PDFAppCommentPopup::composeCanceled);
    QSignalSpy closedSpy(popup, &pdf::PDFAppCommentPopup::closed);
    const QRect anchor(200, 150, 20, 20);
    popup->showCompose(anchor, QStringLiteral("Sticky note"), QStringLiteral("note-note"));
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Compose);
    QVERIFY(popup->isVisible());
    QCOMPARE(popup->getCardGeometry().width(), pdf::PDFAppCommentPopup::POPUP_WIDTH);
    QCOMPARE(popup->getCardGeometry().left(), anchor.right() + 1 + pdf::PDFAppCommentPopup::ANCHOR_GAP);
    QCOMPARE(popup->getCardGeometry().top(), anchor.top());
    QVERIFY(popup->getTextEdit()->isVisible());
    QVERIFY(!popup->getReplyField()->isVisible());
    QVERIFY(!popup->getMoreButton()->isVisible());
    QVERIFY(popup->getCancelButton()->isVisible());
    QVERIFY(!popup->getPrimaryButton()->isEnabled());
    QCOMPARE(popup->getPrimaryButton()->text(), QObject::tr("Post"));
    QCOMPARE(popup->getTitleLabel()->text(), QStringLiteral("Sticky note"));
    QCOMPARE(popup->getBadge()->getIconName(), QStringLiteral("note-note"));
    QCOMPARE(popup->window()->focusWidget(), popup->getTextEdit());

    // Typing enables Post, Ctrl+Enter posts the text (nothing is committed by the popup)
    QTest::keyClicks(popup->getTextEdit(), QStringLiteral("First line"));
    QVERIFY(popup->getPrimaryButton()->isEnabled());
    QCOMPARE(popup->getComposedText(), QStringLiteral("First line"));
    QTest::keyClick(popup->getTextEdit(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(acceptedSpy.at(0).at(0).toString(), QStringLiteral("First line"));
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);
    QVERIFY(!popup->isVisible());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QCOMPARE(closedSpy.count(), 0);
    fixture.settle();
    QCOMPARE(closedSpy.count(), 1);

    // Esc cancels (no undo step), the Cancel button too, a click on the page too
    popup->showCompose(anchor, QStringLiteral("Comment"), QStringLiteral("message-square"));
    QTest::keyClick(popup->getTextEdit(), Qt::Key_Escape);
    QCOMPARE(canceledSpy.count(), 1);
    popup->showCompose(anchor, QStringLiteral("Comment"), QStringLiteral("message-square"));
    popup->getCancelButton()->click();
    QCOMPARE(canceledSpy.count(), 2);
    popup->showCompose(anchor, QStringLiteral("Comment"), QStringLiteral("message-square"));
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(250, 380)));
    fixture.settle();
    QCOMPARE(canceledSpy.count(), 3);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);

    // A second compose cancels the first one; a thread cancels a compose
    popup->showCompose(anchor, QStringLiteral("A"), QStringLiteral("message-square"));
    popup->showCompose(anchor, QStringLiteral("B"), QStringLiteral("message-square"));
    QCOMPARE(canceledSpy.count(), 4);
    popup->showThread(H::find(document, 0, QStringLiteral("text")), false);
    QCOMPARE(canceledSpy.count(), 5);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    popup->cancel();

    // Hostile texts are bounded; an anchor at the right edge puts the popup at the left
    popup->showCompose(QRect(view->getCanvasArea()->width() - 30, 40, 20, 20), QString(10000, QLatin1Char('t')), QStringLiteral("x"));
    QVERIFY(popup->getCardGeometry().right() < view->getCanvasArea()->width() - 30);
    popup->setComposedText(QString(200000, QLatin1Char('a')));
    QCOMPARE(popup->getComposedText().size(), pdf::PDFAppCommentPopup::MAXIMUM_TEXT_LENGTH);
    popup->accept();
    QCOMPARE(acceptedSpy.count(), 2);
    QCOMPARE(acceptedSpy.at(1).at(0).toString().size(), pdf::PDFAppCommentPopup::MAXIMUM_TEXT_LENGTH);

    // The lock cancels a compose
    popup->showCompose(anchor, QStringLiteral("Comment"), QStringLiteral("message-square"));
    document->setLocked(true);
    fixture.settle();
    QCOMPARE(canceledSpy.count(), 6);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);
    document->setLocked(false);
}

void AppAnnotationEditTest::commentPopupThread()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference note = H::find(document, 0, QStringLiteral("text"));
    QVERIFY(note.isValid());

    // The command opens the thread from the event loop (the controller queues it)
    controller->requestComment(note, false);
    fixture.settle();
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    QCOMPARE(popup->getAnnotation(), note);
    QVERIFY(popup->isVisible());
    QCOMPARE(popup->getReplyCount(), 2);
    QCOMPARE(popup->getTitleLabel()->text(), QStringLiteral("Tester"));
    QVERIFY(popup->getInfoLabel()->text().contains(QObject::tr("Sticky note")));
    QVERIFY(popup->isReplyFieldShown());
    QVERIFY(popup->getReplyField()->isVisible());
    QVERIFY(!popup->getPrimaryButton()->isEnabled());
    QCOMPARE(popup->getPrimaryButton()->text(), QObject::tr("Reply"));

    // The popup lies next to the annotation on the screen
    const QRect anchor = popup->getAnchor();
    QVERIFY(anchor.isValid());
    QVERIFY(!popup->getCardGeometry().intersects(anchor));

    // A reply: one undo step, the thread has three replies
    QTest::keyClicks(popup->getReplyField(), QStringLiteral("My reply"));
    QVERIFY(popup->getPrimaryButton()->isEnabled());
    QSignalSpy replySpy(popup, &pdf::PDFAppCommentPopup::replyAdded);
    QTest::keyClick(popup->getReplyField(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(replySpy.count(), 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Reply to annotation"));
    QCOMPARE(popup->getReplyCount(), 3);
    QVERIFY(popup->getReplyText().isEmpty());
    QVERIFY(!popup->sendReply());

    // Undo removes the reply from the thread
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(popup->getReplyCount(), 2);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);

    // Edit text of the menu: one undo step
    popup->showMoreMenu();
    QVERIFY(popup->getMoreMenu());
    QVERIFY(popup->getMoreMenu()->isVisible());
    QAction* editAction = popup->getMoreMenu()->findChild<QAction*>(QStringLiteral("commentPopupEditText"));
    QVERIFY(editAction && editAction->isEnabled());
    popup->getMoreMenu()->hide();
    QVERIFY(popup->beginEditContents());
    QVERIFY(popup->isEditingContents());
    QVERIFY(popup->getTextEdit()->isVisible());
    QVERIFY(!popup->getReplyField()->isVisible());
    QCOMPARE(popup->getPrimaryButton()->text(), QObject::tr("Save"));
    popup->setEditedContents(QStringLiteral("Changed text"));
    QTest::keyClick(popup->getTextEdit(), Qt::Key_Escape);
    QVERIFY(!popup->isEditingContents());
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    QVERIFY(popup->beginEditContents());
    popup->setEditedContents(QStringLiteral("Changed text"));
    QVERIFY(popup->commitContents());
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Edit comment"));
    QCOMPARE(popup->getContentsLabel()->text(), QStringLiteral("Changed text"));

    // Locked document (1030:161012): no reply field, no editing, the thread stays
    document->setLocked(true);
    fixture.settle();
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    QVERIFY(!popup->isReplyFieldShown());
    QVERIFY(!popup->getReplyField()->isVisible());
    QVERIFY(!popup->getPrimaryButton()->isVisible());
    QVERIFY(!popup->canEditContents());
    QVERIFY(!popup->beginEditContents());
    popup->setReplyText(QStringLiteral("x"));
    QVERIFY(!popup->sendReply());
    document->setLocked(false);
    fixture.settle();
    QVERIFY(popup->isReplyFieldShown());

    // Esc closes the thread
    popup->showThread(note, true);
    QCOMPARE(popup->window()->focusWidget(), popup->getReplyField());
    QTest::keyClick(popup->getReplyField(), Qt::Key_Escape);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);

    // An invalid annotation is refused
    popup->showThread(pdf::PDFObjectReference(999999, 0), false);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);
}

void AppAnnotationEditTest::commentPopupAnnotationRemoved()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();

    // The annotation deleted: the popup closes; undo does not reopen it
    popup->showThread(square, false);
    QCOMPARE(popup->getReplyCount(), 0);
    QSignalSpy closedSpy(popup, &pdf::PDFAppCommentPopup::closed);
    controller->selectAnnotations({ square });
    controller->deleteSelection();
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);
    fixture.settle();
    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);

    // Delete of the menu of the popup: one step, the popup closes
    popup->showThread(square, false);
    const int count = pdftest::AppDocumentFixture::undoCount(document);
    popup->showMoreMenu();
    QAction* deleteAction = popup->getMoreMenu()->findChild<QAction*>(QStringLiteral("commentPopupDelete"));
    QVERIFY(deleteAction && deleteAction->isEnabled());
    deleteAction->trigger();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), count + 1);
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::None);
    QVERIFY(!H::find(document, 0, QStringLiteral("square")).isValid());

    // A locked annotation: no deletion, the text stays editable (ISO 32000-2 Table 167)
    const pdf::PDFObjectReference locked = H::find(document, 0, QStringLiteral("locked"));
    popup->showThread(locked, false);
    QVERIFY(popup->canEditContents());
    popup->showMoreMenu();
    QVERIFY(!popup->getMoreMenu()->findChild<QAction*>(QStringLiteral("commentPopupDelete"))->isEnabled());
    popup->getMoreMenu()->hide();

    // The tab deleted with the popup and its menu open
    popup->showMoreMenu();
    QPointer<pdf::PDFAppCommentPopup> guard(popup);
    delete tab.data();
    fixture.settle();
    QVERIFY(!guard);
}

void AppAnnotationEditTest::selectionWithTextTool()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(tab->getDocument(), 0, QStringLiteral("square"));

    // The text tool is active (the mode Select text - a new tab is in Hand); a click on
    // an annotation selects it (E7 pass-through)
    view->setCanvasTool(pdf::PDFToolManager::SelectTextTool);
    QCOMPARE(controller->getActiveTool(), controller->getToolManager()->getSelectTextTool());
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(40, 122)));
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ square }));

    // The annotation bar appears at the place of the tool bar (12 below the toolbar, centered)
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    QVERIFY(bar);
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->getCardGeometry().top(), pdf::PDFAppFloatingBar::TOP_GAP);
    QVERIFY(qAbs(bar->getCardGeometry().center().x() - view->getCanvasArea()->width() / 2) <= 1);
    QCOMPARE(bar->getNameLabel()->text(), QObject::tr("Rectangle"));

    // Shift+click adds, a click on an empty place clears the selection, the bar hides
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(110, 122), Qt::ShiftModifier));
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(2));
    QCOMPARE(bar->getNameLabel()->text(), QObject::tr("%n annotations", nullptr, 2));
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, QPointF(250, 380)));
    fixture.settle();
    QVERIFY(!controller->hasAnnotationSelection());
    QVERIFY(!bar->isVisible());

    // Esc clears the selection too
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(bar->isVisible());
    QTest::keyClick(pdftest::AppDocumentFixture::canvasWidget(view), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!controller->hasAnnotationSelection());
}

void AppAnnotationEditTest::rubberBand()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();

    // Ctrl + drag on an empty place: the annotations fully inside are selected
    // (the square and the circle of the first row, 1018:18079)
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, QPointF(6, 96), QPointF(146, 148), Qt::ControlModifier, 8));
    fixture.settle();
    const std::vector<pdf::PDFObjectReference> selection = controller->getSelectedAnnotations();
    QVERIFY(std::find(selection.cbegin(), selection.cend(), H::find(tab->getDocument(), 0, QStringLiteral("square"))) != selection.cend());
    QVERIFY(std::find(selection.cbegin(), selection.cend(), H::find(tab->getDocument(), 0, QStringLiteral("circle"))) != selection.cend());
    QVERIFY(std::find(selection.cbegin(), selection.cend(), H::find(tab->getDocument(), 0, QStringLiteral("line"))) == selection.cend());
    QVERIFY(H::handler(tab)->getAnnotationBar()->isVisible());
}

void AppAnnotationEditTest::altClickNext()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()), 1);
    QVERIFY(tab && tab->getDocument()->isReady());
    tab->getView()->goToPage(1);
    fixture.settle();
    fixture.settle();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference overlapA = H::find(tab->getDocument(), 1, QStringLiteral("overlapA"));
    const pdf::PDFObjectReference overlapB = H::find(tab->getDocument(), 1, QStringLiteral("overlapB"));

    // A click selects the topmost of the overlapping rectangles, Alt+click the next one
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 1, QPointF(120, 200)));
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(1));
    const pdf::PDFObjectReference first = controller->getSelectedAnnotations().front();
    QVERIFY(first == overlapA || first == overlapB);
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 1, QPointF(120, 200), Qt::AltModifier));
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(1));
    QVERIFY(controller->getSelectedAnnotations().front() != first);
}

void AppAnnotationEditTest::overlapChooser()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()), 1);
    QVERIFY(tab && tab->getDocument()->isReady());
    tab->getView()->goToPage(1);
    fixture.settle();
    fixture.settle();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFObjectReference overlapA = H::find(document, 1, QStringLiteral("overlapA"));
    const pdf::PDFObjectReference overlapB = H::find(document, 1, QStringLiteral("overlapB"));

    // The engine asks for the chooser (1019:20171) at a place of two annotations
    bool ok = false;
    const QPoint position = pdftest::AppDocumentFixture::pageToWidget(view, 1, QPointF(120, 200), &ok);
    QVERIFY(ok);
    pdf::PDFAnnotationMenuRequest request = controller->getAnnotationManager()->getMenuRequest(position);
    QCOMPARE(request.kind, pdf::PDFAnnotationMenuRequest::Kind::Overlap);
    QCOMPARE(request.annotationsUnderCursor.size(), size_t(2));
    request.globalPosition = QPoint(100, 60);
    controller->requestMenu(request);
    fixture.settle();

    pdf::PDFAppAnnotationMenus* menus = H::handler(tab)->getMenus();
    QMenu* menu = menus->getMenu();
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    QAction* row = nullptr;
    for (QAction* action : menu->actions())
    {
        if (action->objectName() == QStringLiteral("chooser:1"))
        {
            row = action;
        }
    }
    QVERIFY(row && row->menu());
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(row->menu(), pdf::PDFAppCommands::paste()));
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(row->menu(), pdf::PDFAppCommands::annotationSelectAll()));
    const QMargins menuMargins = pdf::PDFWidgetStyle::getPopoverMargins(menu);
    QCOMPARE(menu->minimumWidth(), pdf::PDFAppAnnotationMenus::CHOOSER_WIDTH + menuMargins.left() + menuMargins.right());
    QCOMPARE(row->menu()->minimumWidth(), pdf::PDFAppAnnotationMenus::SUBMENU_WIDTH + menuMargins.left() + menuMargins.right());
    const QList<QAction*> chooserActions = menu->actions();
    QVERIFY(chooserActions.size() >= 2 && chooserActions.back()->isSeparator() && !chooserActions.back()->text().isEmpty());
    QVERIFY(chooserActions[chooserActions.size() - 2]->isSeparator() && chooserActions[chooserActions.size() - 2]->text().isEmpty());
    H::save(QStringLiteral("annotation-overlap-menu"), H::renderWithPopups(fixture.window, { menu }));
    H::save(QStringLiteral("annotation-overlap-menu-only"), H::render(menu));

    // An item of a row selects its annotation and runs the command (Hide on Page: one step)
    QAction* hide = pdf::PDFAppAnnotationMenus::findAction(row->menu(), pdf::PDFAppCommands::annotationHide());
    QVERIFY(hide);
    hide->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    const pdf::PDFObjectReference hidden = request.annotationsUnderCursor[1];
    QVERIFY(pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), hidden).isHidden());
    QVERIFY(hidden == overlapA || hidden == overlapB);
    menu->close();
    fixture.settle();
}

void AppAnnotationEditTest::annotationBarSingle()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();

    // 1015:17768: type + author, Stroke / Fill, swatches, width, Comment, Properties, More
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->getNameLabel()->text(), QObject::tr("Rectangle"));
    QCOMPARE(bar->getAuthorLabel()->text(), QStringLiteral("Tester"));
    QVERIFY(bar->getTargetControl()->isVisible());
    QVERIFY(bar->getSwatchRow()->isVisible());
    QCOMPARE(bar->getSwatchRow()->getColor(), QColor(Qt::red));
    QVERIFY(bar->getWidthComboBox()->isVisible());
    QCOMPARE(bar->getWidthComboBox()->currentData().toDouble(), 2.0);
    QVERIFY(bar->getCommentButton()->isVisible());
    QVERIFY(bar->getPropertiesButton()->isVisible());
    QVERIFY(bar->getMoreButton()->isVisible());
    QVERIFY(!bar->getCommandButton(pdf::PDFAppCommands::annotationAlignLeft())->isVisible());
    QCOMPARE(bar->getCardHeight(), pdf::PDFAppFloatingBar::NORMAL_HEIGHT);

    // The icon of the type in icon/accent (1015:17770, as the bar of a creation tool)
    QCOMPARE(bar->getTypeIcon()->getColor(), pdf::PDFThemeColors::IconAccent);

    // The hint of the selection in the status bar (1015:17319), what a drag does (1026:21809)
    pdf::PDFAppDocumentStatusBar* statusBar = tab->getView()->getStatusBar();
    QCOMPARE(bar->getStatusHint(), QStringLiteral("Rectangle selected · drag to move, drag a handle to resize (Shift keeps the aspect ratio), "
                                                  "drag the round handle to rotate in 90° steps · arrows nudge · Esc deselects"));
    QCOMPARE(statusBar->getMessage(), bar->getStatusHint());

    // Fill target: none + 8 + white; the fill of the rectangle is none
    bar->setFillTarget(true);
    QVERIFY(bar->getSwatchRow()->getOptions().testFlag(pdf::PDFColorSwatchRow::Option::NoColor));
    QVERIFY(bar->getSwatchRow()->isNoColorSelected());
    bar->setFillTarget(false);

    // The buttons run their commands (from the event loop): Properties opens the dialog
    bar->getPropertiesButton()->click();
    fixture.settle();
    QVERIFY(qobject_cast<pdf::PDFAppAnnotationPropertiesDialog*>(H::handler(tab)->getOpenDialog()));
    H::closeDialog(tab);
    bar->getCommentButton()->click();
    fixture.settle();
    QCOMPARE(tab->getView()->getCommentPopup()->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    tab->getView()->getCommentPopup()->cancel();

    // More: the menu of the selection below the button
    bar->getMoreButton()->click();
    fixture.settle();
    QVERIFY(H::handler(tab)->getMenus()->getMenu());
    QVERIFY(H::handler(tab)->getMenus()->getMenu()->isVisible());
    H::handler(tab)->getMenus()->getMenu()->close();
    fixture.settle();

    // A type without a fill and without a width (a highlight): no target, no width
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("highlight")) });
    fixture.settle();
    QVERIFY(!bar->getTargetControl()->isVisible());
    QVERIFY(!bar->getWidthComboBox()->isVisible());
    QCOMPARE(bar->getNameLabel()->text(), QObject::tr("Highlight"));

    // A stamp has its name
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("stamp")) });
    fixture.settle();
    QVERIFY(bar->getNameLabel()->text().contains(QObject::tr("Approved")));
    QVERIFY(bar->getStatusHint().contains(QStringLiteral("drag the round handle to rotate ·")));
    QCOMPARE(statusBar->getMessage(), bar->getStatusHint());
    H::save(QStringLiteral("annotation-bar-single"), H::render(fixture.window));

    // A message of another part stays, until the selection changes; no selection = Ready
    statusBar->showMessage(QStringLiteral("Other message"));
    bar->updateState();
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Other message"));
    controller->clearAnnotationSelection();
    fixture.settle();
    QVERIFY(bar->getStatusHint().isEmpty());
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Other message"));
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();
    QCOMPARE(statusBar->getMessage(), bar->getStatusHint());
    controller->clearAnnotationSelection();
    fixture.settle();
    QCOMPARE(statusBar->getMessage(), QObject::tr("Ready"));
}

void AppAnnotationEditTest::annotationBarMulti()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));
    const pdf::PDFObjectReference polygon = H::find(document, 0, QStringLiteral("polygon"));
    controller->selectAnnotations({ square, circle, polygon });
    fixture.settle();

    // 1018:18676: "3 annotations" + "Page 1", no icon, no target, Align ×6, Distribute ×2
    QCOMPARE(bar->getNameLabel()->text(), QObject::tr("%n annotations", nullptr, 3));
    QCOMPARE(bar->getAuthorLabel()->text(), QObject::tr("Page %1").arg(tab->getView()->getPageLabel(0)));
    QVERIFY(!bar->getTypeIcon()->isVisible());
    QVERIFY(!bar->getTargetControl()->isVisible());
    QVERIFY(!bar->getCommentButton()->isVisible());
    QVERIFY(!bar->getPropertiesButton()->isVisible());
    for (const QString& commandId : { pdf::PDFAppCommands::annotationAlignLeft(), pdf::PDFAppCommands::annotationAlignCenter(), pdf::PDFAppCommands::annotationAlignRight(),
                                      pdf::PDFAppCommands::annotationAlignTop(), pdf::PDFAppCommands::annotationAlignMiddle(), pdf::PDFAppCommands::annotationAlignBottom(),
                                      pdf::PDFAppCommands::annotationDistributeHorizontally(), pdf::PDFAppCommands::annotationDistributeVertically() })
    {
        QVERIFY(bar->getCommandButton(commandId)->isVisible());
        QVERIFY(bar->getCommandButton(commandId)->isEnabled());
    }

    // Mixed colors: no swatch selected; mixed widths: "Mixed" (124 wide)
    QVERIFY(bar->getSwatchRow()->getColor() != QColor(Qt::red));
    controller->selectAnnotations({ circle });
    bar->applyWidth(5.0);
    controller->selectAnnotations({ square, circle });
    fixture.settle();
    QCOMPARE(bar->getWidthComboBox()->currentIndex(), -1);
    QCOMPARE(bar->getWidthComboBox()->width(), pdf::PDFAppAnnotationBar::WIDTH_SELECT_MIXED);

    // Align left by the button: one step
    const int count = pdftest::AppDocumentFixture::undoCount(document);
    bar->getCommandButton(pdf::PDFAppCommands::annotationAlignLeft())->click();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), count + 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Align annotations"));
    const QRectF squareRect = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;
    const QRectF circleRect = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), circle).rectangle;
    QVERIFY(qAbs(squareRect.left() - circleRect.left()) < 1.0);
    H::save(QStringLiteral("annotation-bar-multi"), H::render(fixture.window));
}

void AppAnnotationEditTest::liveColorWidthOneStep()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();

    // Trying three colors is one undo step (O20)
    bar->applyColor(QColor(Qt::green));
    bar->applyColor(QColor(Qt::blue));
    bar->applyColor(QColor(Qt::yellow));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::properties(document, square).color, QColor(Qt::yellow));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Change color"));

    // The width is another control: a new step, the widths are merged again
    bar->applyWidth(3.0);
    bar->applyWidth(4.0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(H::properties(document, square).borderWidth, 4.0);

    // The swatch of the bar by the mouse: the fill (Fill target) - a third step
    bar->setFillTarget(true);
    bar->getSwatchRow()->getSwatch(1)->click();
    bar->getSwatchRow()->getSwatch(2)->click();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);
    QVERIFY(H::properties(document, square).interiorColor.isValid());
    bar->getSwatchRow()->getSwatch(0)->click();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);
    QVERIFY(!H::properties(document, square).interiorColor.isValid());

    // One undo returns all colors of the step at once
    QVERIFY(document->getUndoStack()->undo());
    QVERIFY(document->getUndoStack()->undo());
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(H::properties(document, square).color, QColor(Qt::red));
    QCOMPARE(H::properties(document, square).borderWidth, 2.0);

    // The width select of the bar
    controller->selectAnnotations({ square });
    fixture.settle();
    pdf::PDFComboBox* width = bar->getWidthComboBox();
    const int index = width->findData(6.0);
    QVERIFY(index >= 0);
    QMetaObject::invokeMethod(width, "activated", Q_ARG(int, index));
    QCOMPARE(H::properties(document, square).borderWidth, 6.0);
}

void AppAnnotationEditTest::mergeSessionClosed()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));

    controller->selectAnnotations({ square });
    fixture.settle();
    bar->applyColor(QColor(Qt::green));
    QVERIFY(controller->isMergeSessionOpen());

    // A change of the selection ends the session
    controller->selectAnnotations({ circle });
    QVERIFY(!controller->isMergeSessionOpen());
    bar->applyColor(QColor(Qt::green));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);

    // A press of another control (Properties) ends it
    bar->getPropertiesButton()->click();
    QVERIFY(!controller->isMergeSessionOpen());
    fixture.settle();
    H::closeDialog(tab);
    bar->applyColor(QColor(Qt::blue));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);

    // The hide of the bar ends it
    QVERIFY(controller->isMergeSessionOpen());
    controller->clearAnnotationSelection();
    fixture.settle();
    QVERIFY(!bar->isVisible());
    QVERIFY(!controller->isMergeSessionOpen());

    // An undo between two colors: a new step
    controller->selectAnnotations({ square });
    fixture.settle();
    bar->applyColor(QColor(Qt::cyan));
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    bar->applyColor(QColor(Qt::magenta));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 4);
}

void AppAnnotationEditTest::contextMenuSingle()
{
    using H = AppAnnotationEditTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationMenus* menus = H::handler(tab)->getMenus();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    controller->copySelection();
    fixture.settle();

    // A right click on the selection: the engine request, the menu 1019:18817
    bool ok = false;
    const QPoint position = pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 0, QPointF(40, 122), &ok);
    QVERIFY(ok);
    QVERIFY(pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(40, 122), Qt::NoModifier, Qt::RightButton));
    fixture.settle();
    QMenu* menu = menus->getMenu();
    QVERIFY(menu);
    for (const QString& id : { C::annotationComment(), C::annotationReply(), C::annotationProperties(), C::annotationGeometry(), C::cut(), C::copy(), C::paste(),
                               C::pasteInPlace(), C::annotationCopyToPages(), C::annotationRotateClockwise(), C::annotationFlipVertical(), C::annotationHide(),
                               C::annotationSelectAll(), C::deleteSelection() })
    {
        QVERIFY2(pdf::PDFAppAnnotationMenus::findAction(menu, id), qPrintable(id));
    }
    QCOMPARE(pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationProperties())->shortcut(), QKeySequence(Qt::Key_F2));
    QCOMPARE(pdf::PDFAppAnnotationMenus::findAction(menu, C::deleteSelection())->shortcut(), QKeySequence(Qt::Key_Delete));
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, C::paste())->isEnabled());
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationAlignLeft()));

    // 1019:19318: Paste at Original Position and Rotate and Flip have no icon (Show icon off);
    // the panel is 300 wide, the window holds the shadow too
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, C::pasteInPlace())->icon().isNull());
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::paste())->icon().isNull());
    QAction* rotateAndFlip = nullptr;
    for (QAction* action : menu->actions())
    {
        rotateAndFlip = action->objectName() == QStringLiteral("rotateAndFlip") ? action : rotateAndFlip;
    }
    QVERIFY(rotateAndFlip && rotateAndFlip->icon().isNull());
    const QMargins menuMargins = pdf::PDFWidgetStyle::getPopoverMargins(menu);
    QCOMPARE(menu->minimumWidth(), pdf::PDFAppAnnotationMenus::SINGLE_WIDTH + menuMargins.left() + menuMargins.right());
    QCOMPARE(rotateAndFlip->menu()->minimumWidth(), pdf::PDFAppAnnotationMenus::SUBMENU_WIDTH + menuMargins.left() + menuMargins.right());
    // The rows of 1019:19318 fit into the panel of 300 (10 px between a label and its
    // shortcut, the widest row - not the widest label plus the widest shortcut of another row)
    QCOMPARE(menu->sizeHint().width(), pdf::PDFAppAnnotationMenus::SINGLE_WIDTH + menuMargins.left() + menuMargins.right());
    QCOMPARE(rotateAndFlip->menu()->sizeHint().width(), pdf::PDFAppAnnotationMenus::SUBMENU_WIDTH + menuMargins.left() + menuMargins.right());
    QMenu* shown = menus->showMenu(H::selectionRequest(controller));
    fixture.settle();
    H::save(QStringLiteral("annotation-menu-single"), H::renderWithPopups(fixture.window, { shown }));
    H::save(QStringLiteral("annotation-menu-single-only"), H::render(shown));
    menu = shown;

    // Rotate clockwise of the submenu: one step
    pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationRotateClockwise())->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Rotate annotations"));

    // Delete of the menu of the selection (More of the bar): one step
    menu = menus->showMenu(H::selectionRequest(controller));
    QVERIFY(menu);
    pdf::PDFAppAnnotationMenus::findAction(menu, C::deleteSelection())->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(!H::find(document, 0, QStringLiteral("square")).isValid());

    // A menu of the kind None is not shown
    QVERIFY(!menus->showMenu(pdf::PDFAnnotationMenuRequest()));
}

void AppAnnotationEditTest::contextMenuMulti()
{
    using H = AppAnnotationEditTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    controller->selectAnnotations({ H::find(document, 0, QStringLiteral("square")), H::find(document, 0, QStringLiteral("circle")),
                                    H::find(document, 0, QStringLiteral("polygon")) });
    fixture.settle();

    // 1019:19985: the section, no Comment / Properties, Align ▸ with Distribute
    QMenu* menu = H::handler(tab)->getMenus()->showMenu(H::selectionRequest(controller));
    QVERIFY(menu);
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationComment()));
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationProperties()));
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationAlignLeft()));
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationDistributeVertically())->isEnabled());
    QVERIFY(menu->actions().front()->isSeparator());
    QVERIFY(menu->actions().front()->text().contains(QStringLiteral("3")));

    // 1019:19985: no Select All on Displayed Pages, Align without an icon, the panel 260 wide
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationSelectAll()));
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, C::deleteSelection()));
    QAction* align = nullptr;
    for (QAction* action : menu->actions())
    {
        align = action->objectName() == QStringLiteral("align") ? action : align;
    }
    QVERIFY(align && align->icon().isNull() && align->menu());
    const QMargins menuMargins = pdf::PDFWidgetStyle::getPopoverMargins(menu);
    QCOMPARE(menu->minimumWidth(), pdf::PDFAppAnnotationMenus::MULTI_WIDTH + menuMargins.left() + menuMargins.right());
    QCOMPARE(align->menu()->minimumWidth(), pdf::PDFAppAnnotationMenus::SUBMENU_WIDTH + menuMargins.left() + menuMargins.right());
    QCOMPARE(menu->sizeHint().width(), pdf::PDFAppAnnotationMenus::MULTI_WIDTH + menuMargins.left() + menuMargins.right());
    QCOMPARE(align->menu()->sizeHint().width(), pdf::PDFAppAnnotationMenus::SUBMENU_WIDTH + menuMargins.left() + menuMargins.right());
    H::save(QStringLiteral("annotation-menu-multi"), H::renderWithPopups(fixture.window, { menu }));
    H::save(QStringLiteral("annotation-menu-multi-only"), H::render(menu));

    pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationDistributeHorizontally())->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Distribute annotations"));
}

void AppAnnotationEditTest::contextMenuHiddenAndReplies()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationMenus* menus = H::handler(tab)->getMenus();
    const pdf::PDFObjectReference hidden = H::find(document, 0, QStringLiteral("hidden"));
    const pdf::PDFObjectReference reply = H::find(document, 0, QStringLiteral("reply"));

    // A hidden annotation (1026:21995): Open Comment, Show on Page, Properties, Delete
    pdf::PDFAnnotationMenuRequest request;
    request.kind = pdf::PDFAnnotationMenuRequest::Kind::Unselectable;
    request.annotation = hidden;
    request.globalPosition = QPoint(200, 200);
    QMenu* menu = menus->showMenu(request);
    QVERIFY(menu);
    for (const QString& id : { QStringLiteral("annotation.openComment"), QStringLiteral("annotation.show"), QStringLiteral("annotation.openProperties"), QStringLiteral("annotation.remove") })
    {
        QVERIFY2(pdf::PDFAppAnnotationMenus::findAction(menu, id), qPrintable(id));
    }
    pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("annotation.show"))->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(!pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), hidden).isHidden());

    // A reply: no Show on Page; Properties opens the dialog, Open Comment the popup
    request.annotation = reply;
    menu = menus->showMenu(request);
    QVERIFY(menu);
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("annotation.show")));
    pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("annotation.openProperties"))->trigger();
    fixture.settle();
    QVERIFY(qobject_cast<pdf::PDFAppAnnotationPropertiesDialog*>(H::handler(tab)->getOpenDialog()));
    H::closeDialog(tab);
    menu = menus->showMenu(request);
    pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("annotation.openComment"))->trigger();
    fixture.settle();
    QCOMPARE(tab->getView()->getCommentPopup()->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    tab->getView()->getCommentPopup()->cancel();

    // Delete of a reply: one step
    menu = menus->showMenu(request);
    pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("annotation.remove"))->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(pdf::PDFAppAnnotationInfo::readReplies(document->getDocument().data(), H::find(document, 0, QStringLiteral("text"))).size(), size_t(1));

    // A stale annotation gives no menu
    request.annotation = pdf::PDFObjectReference(99999, 0);
    QVERIFY(!menus->showMenu(request));
    Q_UNUSED(controller);
}

void AppAnnotationEditTest::pointAndPartMenu()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationMenus* menus = H::handler(tab)->getMenus();
    const pdf::PDFObjectReference polygon = H::find(document, 0, QStringLiteral("polygon"));
    controller->selectAnnotations({ polygon });
    fixture.settle();

    // The items of the place of the menu come first (a point, a segment)
    pdf::PDFAnnotationMenuRequest request = H::selectionRequest(controller);
    request.widgetPosition = QPoint(10, 10);
    request.pointAnnotation = polygon;
    request.removablePoint = 1;
    request.insertSegment = 0;
    request.insertPosition = QPointF(20, 120);
    QMenu* menu = menus->showMenu(request);
    QVERIFY(menu);
    QCOMPARE(menu->actions().front()->property("annotationMenuId").toString(), QStringLiteral("point.remove"));
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("point.insert")));
    pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("point.insert"))->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Insert point"));

    // The ink: Add stroke starts the part edit; the free text with a callout
    request = H::selectionRequest(controller);
    request.widgetPosition = QPoint(10, 10);
    request.canAddStroke = true;
    request.canAddMarkedAreas = true;
    request.canEraseInk = true;
    request.canAddCalloutLine = true;
    request.canRemoveCalloutLine = true;
    request.partAnnotation = polygon;
    request.partIndex = 0;
    menu = menus->showMenu(request);
    for (const QString& id : { QStringLiteral("part.remove"), QStringLiteral("part.addStroke"), QStringLiteral("part.addMarkedAreas"),
                               QStringLiteral("part.eraseInk"), QStringLiteral("callout.add"), QStringLiteral("callout.remove") })
    {
        QVERIFY2(pdf::PDFAppAnnotationMenus::findAction(menu, id), qPrintable(id));
    }
    menu->close();

    // A locked document: no items of the place
    document->setLocked(true);
    fixture.settle();
    request.isModificationAllowed = false;
    menu = menus->showMenu(request);
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("part.remove")));
    document->setLocked(false);
}

void AppAnnotationEditTest::propertiesDialogVariants()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();

    // The parts of the types (1027:21823): object names of the controls, which must exist / not exist
    struct Variant
    {
        const char* subject;
        QStringList present;
        QStringList absent;
    };
    const Variant variants[] = {
        { "square", { "propertiesAuthor", "propertiesSubject", "propertiesText", "propertiesColor", "propertiesFill", "propertiesOpacity", "propertiesBlendMode", "propertiesBorderWidth", "propertiesBorderStyle" },
          { "propertiesLineStart", "propertiesIcon:Comment", "propertiesFont" } },
        { "line", { "propertiesColor", "propertiesBlendMode", "propertiesLineStart", "propertiesLineEnd", "propertiesBorderWidth" }, { "propertiesFill", "propertiesFont" } },
        { "polyline", { "propertiesLineStart", "propertiesLineEnd" }, { "propertiesFill" } },
        { "polygon", { "propertiesFill", "propertiesBorderWidth" }, { "propertiesLineStart" } },
        { "text", { "propertiesIcon:Comment", "propertiesIcon:NewParagraph", "propertiesOpen", "propertiesColor", "propertiesOpacity", "propertiesBlendMode" }, { "propertiesFill", "propertiesBorderWidth" } },
        { "freetext", { "propertiesFont", "propertiesFontSize", "propertiesTextColor", "propertiesAlignment", "propertiesBorderWidth", "propertiesColor", "propertiesFill" },
          { "propertiesBorderStyle" } },
        { "callout", { "propertiesCalloutEnd" }, {} },
        { "highlight", { "propertiesColor", "propertiesOpacity", "propertiesBlendMode" }, { "propertiesFill", "propertiesBorderWidth" } },
        { "measurement", { "propertiesMeasuredValue", "propertiesLeaderLine", "propertiesLeaderExtension", "propertiesCaption" }, {} },
        { "stamp", { "propertiesIconName" }, { "propertiesFill" } },
    };

    for (const Variant& variant : variants)
    {
        const pdf::PDFObjectReference annotation = H::find(document, 0, QString::fromLatin1(variant.subject));
        QVERIFY2(annotation.isValid(), variant.subject);
        pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, annotation, fixture.window);
        QVERIFY(dialog);
        fixture.settle();
        QVERIFY(dialog->isVisible());
        for (const QString& name : variant.present)
        {
            QVERIFY2(dialog->findChild<QWidget*>(name), qPrintable(QString::fromLatin1(variant.subject) + QStringLiteral(": ") + name));
        }
        for (const QString& name : variant.absent)
        {
            QVERIFY2(!dialog->findChild<QWidget*>(name), qPrintable(QString::fromLatin1(variant.subject) + QStringLiteral(": ") + name));
        }
        QCOMPARE(dialog->getChangedFields(), pdf::PDFAnnotationProperties::Fields(pdf::PDFAnnotationProperties::None));
        H::save(QStringLiteral("annotation-properties-%1").arg(QString::fromLatin1(variant.subject)), H::renderWithPopups(fixture.window, { dialog }));
        H::save(QStringLiteral("annotation-properties-only-%1").arg(QString::fromLatin1(variant.subject)), H::render(dialog));
        dialog->reject();
        fixture.settle();
    }

    // The dialog card is 600 wide; the multi-line label stands at the first line of the text
    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, H::find(document, 0, QStringLiteral("square")), fixture.window);
    fixture.settle();
    QCOMPARE(dialog->getCardWidth(), pdf::PDFAppAnnotationPropertiesDialog::CARD_WIDTH);
    QCOMPARE(dialog->windowTitle(), QObject::tr("Properties of the rectangle"));
    dialog->reject();
    fixture.settle();

    // An invalid annotation is refused
    QVERIFY(!pdf::PDFAppAnnotationPropertiesDialog::open(controller, pdf::PDFObjectReference(99999, 0), fixture.window));
}

void AppAnnotationEditTest::propertiesApplyOneStep()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));

    // F2 on the canvas opens the dialog of the selection (from the event loop)
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotationProperties()));
    fixture.settle();
    pdf::PDFAppAnnotationPropertiesDialog* dialog = qobject_cast<pdf::PDFAppAnnotationPropertiesDialog*>(H::handler(tab)->getOpenDialog());
    QVERIFY(dialog);
    QSignalSpy finishedSpy(dialog, &pdf::PDFAppAnnotationPropertiesDialog::dialogFinished);

    // A second request while the dialog is open is ignored
    controller->requestProperties(square);
    fixture.settle();
    QCOMPARE(H::handler(tab)->getOpenDialog(), dialog);

    // Several changes - one undo step, only the changed fields are written
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("New author"));
    dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->setPlainText(QStringLiteral("New text"));
    dialog->findChild<pdf::PDFColorSwatchRow*>(QStringLiteral("propertiesColor"))->setColor(pdf::PDFColorSwatchRow::getAnnotationColors(pdf::PDFColorSwatchRow::Option::More).at(5).canonicalColor);
    dialog->findChild<pdf::PDFSpinBox*>(QStringLiteral("propertiesOpacity"))->setValue(50);
    QCOMPARE(dialog->findChild<QSlider*>(QStringLiteral("propertiesOpacitySlider"))->value(), 50);
    dialog->findChild<QCheckBox*>(QStringLiteral("propertiesFlag:%1").arg(int(pdf::PDFAnnotation::NoZoom)))->setChecked(true);
    const pdf::PDFAnnotationProperties::Fields fields = dialog->getChangedFields();
    QVERIFY(fields.testFlag(pdf::PDFAnnotationProperties::Author));
    QVERIFY(fields.testFlag(pdf::PDFAnnotationProperties::Contents));
    QVERIFY(fields.testFlag(pdf::PDFAnnotationProperties::Color));
    QVERIFY(fields.testFlag(pdf::PDFAnnotationProperties::Opacity));
    QVERIFY(fields.testFlag(pdf::PDFAnnotationProperties::Flags));
    QVERIFY(!fields.testFlag(pdf::PDFAnnotationProperties::Subject));
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.at(0).at(0).toBool());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Edit annotation properties"));
    const pdf::PDFAnnotationProperties result = H::properties(document, square);
    QCOMPARE(result.author, QStringLiteral("New author"));
    QCOMPARE(result.contents, QStringLiteral("New text"));
    QCOMPARE(result.opacity, 0.5);
    QVERIFY(result.flags.testFlag(pdf::PDFAnnotation::NoZoom));
    QCOMPARE(result.subject, QStringLiteral("square"));

    // Nothing changed: no undo step
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    fixture.settle();
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // Cancel and Esc write nothing
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    fixture.settle();
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("Canceled"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(H::properties(document, square).author, QStringLiteral("New author"));

    // The text box: font, size, alignment, text color - one step
    const pdf::PDFObjectReference freeText = H::find(document, 0, QStringLiteral("freetext"));
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, freeText, fixture.window);
    fixture.settle();

    // The lengths without trailing zeros (1027:21975 "11 pt" in the field of 88): "10", "10.5"
    pdf::PDFDoubleSpinBox* fontSize = dialog->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("propertiesFontSize"));
    QCOMPARE(fontSize->text(), QStringLiteral("10"));
    fontSize->setValue(10.5);
    QCOMPARE(fontSize->text(), QStringLiteral("10.5"));
    QCOMPARE(dialog->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("propertiesBorderWidth"))->text(), QStringLiteral("1"));
    fontSize->setValue(18.0);
    dialog->findChild<pdf::PDFSegmentedControl*>(QStringLiteral("propertiesAlignment"))->setCurrentIndex(2);
    pdf::PDFComboBox* font = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesFont"));
    font->setCurrentIndex(font->findData(QStringLiteral("Courier")));
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(H::properties(document, freeText).freeTextStyle.fontSize, 18.0);
    QVERIFY(H::properties(document, freeText).freeTextStyle.textAlignment.testFlag(Qt::AlignRight));
    QCOMPARE(H::properties(document, freeText).freeTextStyle.fontFamily, QStringLiteral("Courier"));

    // The sticky note: the icon and Open
    const pdf::PDFObjectReference note = H::find(document, 0, QStringLiteral("text"));
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, note, fixture.window);
    fixture.settle();
    dialog->findChild<QToolButton*>(QStringLiteral("propertiesIcon:Key"))->click();
    dialog->findChild<QCheckBox*>(QStringLiteral("propertiesOpen"))->setChecked(true);
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(H::properties(document, note).iconName, QByteArray("Key"));
    QVERIFY(H::properties(document, note).isOpen);

    // The dialog closes, when its annotation disappears
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    fixture.settle();
    QPointer<pdf::PDFAppAnnotationPropertiesDialog> guard(dialog);
    controller->selectAnnotations({ square });
    controller->deleteSelection();
    fixture.settle();
    QVERIFY(!guard);
}

void AppAnnotationEditTest::propertiesBlendMode()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();

    // Two overlapping rectangles of the page 2: A filled yellow below B filled cyan
    const pdf::PDFObjectReference overlapA = H::find(document, 1, QStringLiteral("overlapA"));
    const pdf::PDFObjectReference overlapB = H::find(document, 1, QStringLiteral("overlapB"));
    QVERIFY(overlapA.isValid() && overlapB.isValid());
    const std::pair<pdf::PDFObjectReference, QColor> fills[] = { { overlapA, QColor(255, 255, 0) }, { overlapB, QColor(0, 255, 255) } };
    for (const auto& [annotation, fill] : fills)
    {
        pdf::PDFAnnotationProperties properties = H::properties(document, annotation);
        properties.interiorColor = fill;
        pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), annotation, properties, F::InteriorColor);
        QVERIFY(result);
        QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    }
    controller->selectAnnotations({});
    view->goToPage(1);
    fixture.settle();
    const int stepsBefore = pdftest::AppDocumentFixture::undoCount(document);

    // Pixel of the canvas at a point of the page 2 (the page compiled again after a change)
    auto canvasPixel = [&fixture, view](const QPointF& pagePoint)
    {
        fixture.settle();
        QTest::qWait(50);
        fixture.settle();
        bool ok = false;
        const QPoint position = pdftest::AppDocumentFixture::pageToWidget(view, 1, pagePoint, &ok);
        QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
        if (!ok || !canvas)
        {
            return QColor();
        }
        const QImage image = canvas->grab().toImage();
        const QPoint scaled(qRound(position.x() * image.devicePixelRatio()), qRound(position.y() * image.devicePixelRatio()));
        return image.rect().contains(scaled) ? image.pixelColor(scaled) : QColor();
    };
    const QPointF overlapPoint(120, 205);
    const QPointF onlyBPoint(170, 245);

    // Normal: B covers A, the overlap has the color of B
    QColor overlap = canvasPixel(overlapPoint);
    QColor onlyB = canvasPixel(onlyBPoint);
    QVERIFY2(overlap.isValid() && onlyB.isValid(), "page 2 is not shown");
    QVERIFY2(overlap.blue() > 200 && overlap.green() > 200 && overlap.red() < 60, qPrintable(overlap.name()));
    QVERIFY2(qAbs(overlap.blue() - onlyB.blue()) < 20, qPrintable(overlap.name() + QStringLiteral(" ") + onlyB.name()));

    // The row of the dialog (1296:31359): Normal and the modes the viewer draws, Normal shown
    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, overlapB, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    pdf::PDFComboBox* blendMode = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"));
    QVERIFY(blendMode);
    QVERIFY(blendMode->isEnabled());
    QCOMPARE(blendMode->count(), int(pdf::PDFAnnotationPropertyEditor::getOfferedBlendModes().size()));
    QCOMPARE(blendMode->currentData().toString(), QStringLiteral("Normal"));
    QCOMPARE(blendMode->currentText(), QObject::tr("Normal"));
    QCOMPARE(blendMode->accessibleName(), QObject::tr("Blend mode"));
    QCOMPARE(blendMode->itemText(blendMode->findData(QStringLiteral("ColorDodge"))), QObject::tr("Color dodge"));
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::None));

    // Multiply together with the opacity: one undo step, BM written, the page drawn by Multiply
    blendMode->setCurrentIndex(blendMode->findData(QStringLiteral("Multiply")));
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::BlendMode));
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("Blender"));
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::BlendMode | F::Author));
    H::save(QStringLiteral("annotation-properties-blend-mode"), H::renderWithPopups(fixture.window, { dialog }));
    blendMode->showPopup();
    fixture.settle();
    H::save(QStringLiteral("annotation-properties-blend-mode-list"), H::renderWithPopups(fixture.window, { dialog, blendMode->view()->window() }));
    blendMode->hidePopup();
    fixture.settle();
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), stepsBefore + 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Edit annotation properties"));
    QCOMPARE(H::properties(document, overlapB).blendMode, QByteArray("Multiply"));
    QCOMPARE(H::properties(document, overlapB).author, QStringLiteral("Blender"));
    controller->selectAnnotations({});
    overlap = canvasPixel(overlapPoint);
    onlyB = canvasPixel(onlyBPoint);
    QVERIFY2(overlap.blue() < 60 && overlap.green() > 200, qPrintable(overlap.name()));
    QVERIFY2(onlyB.blue() > 200 && onlyB.green() > 200, qPrintable(onlyB.name()));

    // Undo restores Normal and the drawing
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    QCOMPARE(H::properties(document, overlapB).blendMode, QByteArray("Normal"));
    overlap = canvasPixel(overlapPoint);
    QVERIFY2(overlap.blue() > 200 && overlap.red() < 60, qPrintable(overlap.name()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::redo()));
    fixture.settle();
    QCOMPARE(H::properties(document, overlapB).blendMode, QByteArray("Multiply"));

    // A mode the viewer does not draw is shown as it is and kept by other changes
    pdf::PDFAnnotationProperties properties = H::properties(document, overlapB);
    properties.blendMode = "Hue";
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), overlapB, properties, F::BlendMode);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, overlapB, fixture.window);
    fixture.settle();
    blendMode = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"));
    QCOMPARE(blendMode->count(), int(pdf::PDFAnnotationPropertyEditor::getOfferedBlendModes().size()) + 1);
    QCOMPARE(blendMode->currentData().toString(), QStringLiteral("Hue"));
    QCOMPARE(blendMode->currentText(), QObject::tr("%1 (not displayed)").arg(QObject::tr("Hue")));
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::None));
    H::save(QStringLiteral("annotation-properties-blend-mode-not-displayed"), H::render(dialog));
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("Hue keeper"));
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(H::properties(document, overlapB).blendMode, QByteArray("Hue"));
    QCOMPARE(H::properties(document, overlapB).author, QStringLiteral("Hue keeper"));

    // The item of the mode of the document stays after a change of the language (the
    // controls are built again) and another mode can be picked
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, overlapB, fixture.window);
    fixture.settle();
    blendMode = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"));
    blendMode->setCurrentIndex(blendMode->findData(QStringLiteral("Screen")));
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(dialog, &languageChange);
    fixture.settle();
    blendMode = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"));
    QVERIFY(blendMode);
    QCOMPARE(blendMode->currentData().toString(), QStringLiteral("Screen"));
    QVERIFY(blendMode->findData(QStringLiteral("Hue")) >= 0);
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::BlendMode));
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(H::properties(document, overlapB).blendMode, QByteArray("Screen"));

    // The names of the modes; an unknown name of the document is marked
    QCOMPARE(pdf::PDFAppAnnotationPropertiesDialog::getBlendModeName("Compatible"), QObject::tr("%1 (not displayed)").arg(QObject::tr("Compatible")));
    QCOMPARE(pdf::PDFAppAnnotationPropertiesDialog::getBlendModeName("Odd"), QObject::tr("%1 (not displayed)").arg(QStringLiteral("Odd")));
    for (const char* name : { "Screen", "Overlay", "Darken", "Lighten", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion", "Saturation", "Color", "Luminosity" })
    {
        QVERIFY(!pdf::PDFAppAnnotationPropertiesDialog::getBlendModeName(name).isEmpty());
    }

    // A Locked annotation and a locked document: the row is read only
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, H::find(document, 0, QStringLiteral("locked")), fixture.window);
    fixture.settle();
    QVERIFY(dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode")));
    QVERIFY(!dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"))->isEnabled());
    QVERIFY(!dialog->getEditableFields().testFlag(F::BlendMode));
    dialog->reject();
    fixture.settle();
    document->setLocked(true);
    fixture.settle();
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, overlapB, fixture.window);
    fixture.settle();
    QVERIFY(!dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"))->isEnabled());
    dialog->reject();
    fixture.settle();
    document->setLocked(false);
    fixture.settle();

    // Right-to-left and the dark theme: the row is laid out, no leak color
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, overlapB, fixture.window);
    fixture.settle();
    blendMode = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode"));
    QVERIFY(blendMode && blendMode->isVisible());
    const QImage darkImage = H::render(dialog);
    QCOMPARE(H::countLeakPixels(darkImage), 0);
    H::save(QStringLiteral("annotation-properties-blend-mode-dark-rtl"), darkImage);
    dialog->reject();
    fixture.settle();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    fixture.settle();
}

void AppAnnotationEditTest::propertiesReadOnly()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();

    // A Locked annotation (ISO 32000-2 Table 167): the text and the lock flags only
    const pdf::PDFObjectReference locked = H::find(document, 0, QStringLiteral("locked"));
    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, locked, fixture.window);
    fixture.settle();
    QCOMPARE(dialog->getEditableFields(), F::Fields(F::Contents | F::Flags));
    QVERIFY(dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->isReadOnly());
    QVERIFY(!dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->isReadOnly());
    QVERIFY(!dialog->findChild<pdf::PDFColorSwatchRow*>(QStringLiteral("propertiesColor"))->isEnabled());
    QVERIFY(dialog->findChild<QCheckBox*>(QStringLiteral("propertiesFlag:%1").arg(int(pdf::PDFAnnotation::Locked)))->isEnabled());
    QVERIFY(!dialog->findChild<QCheckBox*>(QStringLiteral("propertiesFlag:%1").arg(int(pdf::PDFAnnotation::Hidden)))->isEnabled());
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("propertiesBehaviorHint"))->text(),
             QObject::tr("Locked: the annotation cannot be moved, resized, restyled or deleted. Contents locked: only its comment text cannot be changed."));
    dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->setPlainText(QStringLiteral("Text of a locked annotation"));
    dialog->findChild<QCheckBox*>(QStringLiteral("propertiesFlag:%1").arg(int(pdf::PDFAnnotation::Locked)))->setChecked(false);
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(H::properties(document, locked).contents, QStringLiteral("Text of a locked annotation"));
    QVERIFY(!H::properties(document, locked).flags.testFlag(pdf::PDFAnnotation::Locked));

    // LockedContents: the text is read only, the rest can be changed
    pdf::PDFAnnotationProperties properties = H::properties(document, locked);
    properties.flags = pdf::PDFAnnotation::Flags(pdf::PDFAnnotation::LockedContents);
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), locked, properties, F::Flags);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, locked, fixture.window);
    fixture.settle();
    QVERIFY(dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->isReadOnly());
    QVERIFY(!dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->isReadOnly());
    dialog->reject();
    fixture.settle();

    // A locked document: everything read only, one button Close
    document->setLocked(true);
    fixture.settle();
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, H::find(document, 0, QStringLiteral("square")), fixture.window);
    fixture.settle();
    QVERIFY(dialog->isReadOnly());
    QCOMPARE(dialog->getEditableFields(), F::Fields(F::None));
    QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->isVisibleTo(dialog));
    QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("propertiesCancel"))->text(), QObject::tr("Close"));
    QVERIFY(dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->isReadOnly());
    QVERIFY(!dialog->apply());
    H::save(QStringLiteral("annotation-properties-readonly"), H::render(dialog));

    // The lock removed while the dialog is open: the controls can be changed and
    // applied (review RC-B: the dialog opened read only had no Apply button)
    document->setLocked(false);
    fixture.settle();
    QVERIFY(!dialog->isReadOnly());
    QVERIFY(!dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->isReadOnly());
    QPushButton* apply = dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"));
    QVERIFY(apply && apply->isVisibleTo(dialog) && apply->isEnabled());
    QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("propertiesCancel"))->text(), QObject::tr("Cancel"));
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("propertiesActionsHint"))->text(), QObject::tr("Ctrl+Z undoes all changes of this dialog at once."));
    const int stepsBefore = pdftest::AppDocumentFixture::undoCount(document);
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("Unlocked author"));
    apply->click();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), stepsBefore + 1);
    QCOMPARE(H::properties(document, H::find(document, 0, QStringLiteral("square"))).author, QStringLiteral("Unlocked author"));

    // The other way: a lock while the dialog is open hides Apply again
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, H::find(document, 0, QStringLiteral("square")), fixture.window);
    fixture.settle();
    QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->isVisibleTo(dialog));
    document->setLocked(true);
    fixture.settle();
    QVERIFY(dialog->isReadOnly());
    QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->isVisibleTo(dialog));
    QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("propertiesCancel"))->text(), QObject::tr("Close"));
    dialog->reject();
    fixture.settle();
    document->setLocked(false);
    fixture.settle();
}

void AppAnnotationEditTest::propertiesLink()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab && tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const std::vector<pdf::PDFObjectReference> links = document->getDocument()->getCatalog()->getPage(0)->getAnnotations();
    QVERIFY(links.size() >= 3);

    // GoTo page 3 (1171:28262): Description, Destination with the 8 types, Highlight
    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, links[0], fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    QCOMPARE(dialog->windowTitle(), QObject::tr("Properties of the link"));
    QVERIFY(dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText")));
    pdf::PDFLineEdit* page = dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesPage"));
    pdf::PDFComboBox* type = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesDestinationType"));
    QVERIFY(page && type);
    QCOMPARE(type->count(), 8);
    QCOMPARE(page->text(), tab->getView()->getPageLabel(2));
    QVERIFY(dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesHighlight")));
    QVERIFY(!dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor")));
    QVERIFY(!dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesBlendMode")));
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("propertiesBehaviorHint"))->text(),
             QObject::tr("Locked: the link cannot be moved, resized, restyled, deleted or retargeted. Contents locked: only its description cannot be changed."));

    // head 1171:28262: the icon link-page; row-description-hint without the name of the key
    QCOMPARE(dialog->getBadgeIconName(), QStringLiteral("link-page"));
    bool hasDescriptionHint = false;
    for (const pdf::PDFLabel* label : dialog->findChildren<pdf::PDFLabel*>())
    {
        hasDescriptionHint = hasDescriptionHint || label->text() == QStringLiteral("Saved as the alternate description of the link.");
    }
    QVERIFY(hasDescriptionHint);
    H::save(QStringLiteral("annotation-properties-link"), H::renderWithPopups(fixture.window, { dialog }));

    // Keep the reader's zoom only for Position; a description; Go to target keeps the dialog open
    type->setCurrentIndex(type->findData(int(pdf::DestinationType::XYZ)));
    QVERIFY(dialog->findChild<QCheckBox*>(QStringLiteral("propertiesKeepZoom"))->isEnabled());
    type->setCurrentIndex(type->findData(int(pdf::DestinationType::FitH)));
    QVERIFY(!dialog->findChild<QCheckBox*>(QStringLiteral("propertiesKeepZoom"))->isEnabled());
    dialog->findChild<QPushButton*>(QStringLiteral("propertiesGoToTarget"))->click();
    fixture.settle();
    QVERIFY(dialog->isVisible());
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));

    // A page that does not exist: Apply disabled
    page->setText(QStringLiteral("9999"));
    QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->isEnabled());
    page->setText(QStringLiteral("4"));
    QVERIFY(dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->isEnabled());
    dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->setPlainText(QStringLiteral("Go to the appendix"));
    QVERIFY(dialog->apply());
    fixture.settle();
    F properties = H::properties(document, links[0]);
    QCOMPARE(properties.destination.getDestinationType(), pdf::DestinationType::FitH);
    QCOMPARE(document->getDocument()->getCatalog()->getPageIndexFromPageReference(properties.destination.getPageReference()), size_t(3));
    QCOMPARE(properties.contents, QStringLiteral("Go to the appendix"));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // Set to current view: the page and the place of the view
    tab->getView()->goToPage(1);
    fixture.settle();
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, links[0], fixture.window);
    fixture.settle();
    type = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesDestinationType"));
    type->setCurrentIndex(type->findData(int(pdf::DestinationType::XYZ)));
    dialog->findChild<QPushButton*>(QStringLiteral("propertiesSetToCurrentView"))->click();
    QCOMPARE(dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesPage"))->text(), tab->getView()->getPageLabel(1));
    QVERIFY(dialog->apply());
    fixture.settle();
    properties = H::properties(document, links[0]);
    QCOMPARE(properties.destination.getDestinationType(), pdf::DestinationType::XYZ);
    QCOMPARE(document->getDocument()->getCatalog()->getPageIndexFromPageReference(properties.destination.getPageReference()), size_t(1));
    QVERIFY(std::isnan(properties.destination.getZoom()));

    // A web link shows its address, no destination
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, links[2], fixture.window);
    fixture.settle();
    QVERIFY(!dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesPage")));
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("propertiesAddress"))->text(), QStringLiteral("https://example.com/"));
    QCOMPARE(dialog->getBadgeIconName(), QStringLiteral("link"));
    dialog->reject();
    fixture.settle();

    // A Locked link: the destination cannot be changed, the description can (Table 167)
    pdf::PDFAnnotationProperties lockedProperties = H::properties(document, links[0]);
    lockedProperties.flags = pdf::PDFAnnotation::Flags(pdf::PDFAnnotation::Locked);
    QVERIFY(controller->commit(*pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), links[0], lockedProperties, F::Flags),
                               pdf::PDFAppAnnotationChangeKind::Annotation));
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, links[0], fixture.window);
    fixture.settle();
    QVERIFY(dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesPage"))->isReadOnly());
    QVERIFY(!dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesDestinationType"))->isEnabled());
    QVERIFY(!dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->isReadOnly());
    dialog->reject();
    fixture.settle();
}

void AppAnnotationEditTest::linksMoveAndResize()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    // Owner (O20): a link is moved and resized on the page like the other annotations, one
    // undo step each through the controller; a Locked link is not (ISO 32000-2, Table 167)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab && tab->getDocument()->isReady());
    fixture.settle();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFWidgetAnnotationManager* manager = controller->getAnnotationManager();
    const std::vector<pdf::PDFObjectReference> links = document->getDocument()->getCatalog()->getPage(0)->getAnnotations();
    QVERIFY(links.size() >= 3);
    const pdf::PDFObjectReference link = links[2];
    auto rectangle = [document](pdf::PDFObjectReference annotation)
    {
        return pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), annotation).rectangle.normalized();
    };
    const QRectF original = rectangle(link);
    QVERIFY(original.isValid());

    controller->selectAnnotations({ link });
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ link }));

    // Moved by the drag and drop of the canvas (a press on the selected link, which
    // becomes a drag, is not a click on the link, nothing is opened)
    bool ok = false;
    const QPoint from = pdftest::AppDocumentFixture::pageToWidget(view, 0, original.center(), &ok);
    QVERIFY(ok);
    const QPoint to = pdftest::AppDocumentFixture::pageToWidget(view, 0, original.center() + QPointF(40, -30), &ok);
    QVERIFY(ok);
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
    std::unique_ptr<QMimeData> data(manager->createAnnotationDragData());
    QVERIFY(data);
    QVERIFY(manager->handleAnnotationDrop(data.get(), to, Qt::MoveAction, false));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    const QRectF moved = rectangle(link);
    QVERIFY(qAbs(moved.width() - original.width()) < 0.01 && qAbs(moved.height() - original.height()) < 0.01);
    QVERIFY(QLineF(moved.center(), original.center() + QPointF(40, -30)).length() < 2.0);
    QVERIFY(fixture.closeMessageBoxes() == 0);

    // Resized by the bottom right handle of the selection frame (the bottom of the page coordinates)
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, QPointF(moved.right(), moved.top()), QPointF(moved.right() + 30, moved.top() - 10)));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    const QRectF resized = rectangle(link);
    QVERIFY2(resized.width() > moved.width() + 20.0 && resized.height() > moved.height() + 5.0,
             qPrintable(QString("%1 x %2").arg(resized.width()).arg(resized.height())));

    // Undo gives the size and then the place back
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QVERIFY(qAbs(rectangle(link).width() - moved.width()) < 0.01);
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QVERIFY(QLineF(rectangle(link).center(), original.center()).length() < 0.01);

    // Locked: selected, but neither moved nor resized
    F lockedProperties = H::properties(document, link);
    lockedProperties.flags = pdf::PDFAnnotation::Flags(pdf::PDFAnnotation::Locked);
    QVERIFY(controller->commit(*pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), link, lockedProperties, F::Flags),
                               pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();
    const int lockedCount = pdftest::AppDocumentFixture::undoCount(document);
    controller->selectAnnotations({ link });
    fixture.settle();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
    std::unique_ptr<QMimeData> lockedData(manager->createAnnotationDragData());
    QVERIFY(!lockedData || !manager->handleAnnotationDrop(lockedData.get(), to, Qt::MoveAction, false));
    // Released past the drag distance - no click, which would follow the link (a plain
    // click follows a link also while it is selected, owner 2026-10-25)
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
    fixture.settle();
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, QPointF(original.right(), original.top()), QPointF(original.right() + 30, original.top() - 10)));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), lockedCount);
    QCOMPARE(rectangle(link), original);
    fixture.closeMessageBoxes();
}

void AppAnnotationEditTest::geometryDialog()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const QRectF original = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;

    // The command opens the dialog of the selection (1024:21252)
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotationGeometry()));
    fixture.settle();
    pdf::PDFAppAnnotationGeometryDialog* dialog = qobject_cast<pdf::PDFAppAnnotationGeometryDialog*>(H::handler(tab)->getOpenDialog());
    QVERIFY(dialog);
    QCOMPARE(dialog->getCardWidth(), pdf::PDFAppAnnotationGeometryDialog::CARD_WIDTH);
    QCOMPARE(dialog->getUnit(), pdf::PDFAppAnnotationGeometryDialog::Unit::Millimeters);
    QVERIFY(dialog->getRotationControl());

    // hint 1024:21794 names the type as the title does
    QVERIFY(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("geometryReferenceHint"))->text().endsWith(QStringLiteral("and the rectangle turns around it.")));
    H::save(QStringLiteral("annotation-geometry"), H::renderWithPopups(fixture.window, { dialog }));

    // Units: the fields show the values in points
    dialog->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Points);
    QCOMPARE(dialog->getField(QStringLiteral("width"))->value(), original.width());
    QCOMPARE(dialog->getField(QStringLiteral("x"))->value(), original.left());
    QCOMPARE(dialog->getField(QStringLiteral("y"))->value(), original.bottom());

    // Keep aspect ratio: the height follows the width; the top left corner stays
    dialog->getKeepAspectRatioCheckBox()->setChecked(true);
    dialog->getField(QStringLiteral("width"))->setValue(original.width() * 2.0);
    QCOMPARE(dialog->getField(QStringLiteral("height"))->value(), qRound(original.height() * 20.0) / 10.0);
    QCOMPARE(dialog->getRectangle().left(), original.left());
    QCOMPARE(dialog->getRectangle().bottom(), original.bottom());
    dialog->getRotationControl()->setCurrentIndex(1);
    QCOMPARE(dialog->getRotation(), 90.0);
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Change geometry"));
    const QRectF changed = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;
    QVERIFY(qAbs(changed.width() - original.height() * 2.0) < 0.5);
    QVERIFY(qAbs(changed.height() - original.width() * 2.0) < 0.5);

    // The reference point: the center keeps its place
    dialog = pdf::PDFAppAnnotationGeometryDialog::open(controller, square, fixture.window);
    fixture.settle();
    dialog->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Points);
    dialog->getReferencePointPicker()->setAlignment(Qt::AlignCenter);
    QCOMPARE(dialog->getField(QStringLiteral("x"))->value(), qRound(changed.center().x() * 10.0) / 10.0);
    dialog->getField(QStringLiteral("height"))->setValue(10.0);
    QVERIFY(qAbs(dialog->getRectangle().center().y() - changed.center().y()) < 0.2);
    dialog->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Inches);
    QVERIFY(qAbs(dialog->getField(QStringLiteral("height"))->value() - 10.0 / 72.0) < 0.001);
    dialog->reject();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // An annotation with points (1027:22165): the table; the rectangle group disabled after a change
    const pdf::PDFObjectReference polygon = H::find(document, 0, QStringLiteral("polygon"));
    dialog = pdf::PDFAppAnnotationGeometryDialog::open(controller, polygon, fixture.window);
    fixture.settle();
    QVERIFY(dialog->getPoints().size() >= 3);
    QVERIFY(dialog->getAngleSpinBox());
    dialog->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Points);
    pdf::PDFDoubleSpinBox* pointX = dialog->getPointField(0, false);
    QVERIFY(pointX);
    pointX->setValue(pointX->value() + 5.0);
    QVERIFY(!dialog->getField(QStringLiteral("width"))->isEnabled());
    QVERIFY(!dialog->getField(QStringLiteral("width"))->toolTip().isEmpty());
    H::save(QStringLiteral("annotation-geometry-points"), H::render(dialog));
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);

    // A locked annotation and a locked document refuse the dialog
    QVERIFY(!pdf::PDFAppAnnotationGeometryDialog::open(controller, H::find(document, 0, QStringLiteral("locked")), fixture.window));
    document->setLocked(true);
    fixture.settle();
    QVERIFY(!pdf::PDFAppAnnotationGeometryDialog::open(controller, square, fixture.window));
    document->setLocked(false);
}

void AppAnnotationEditTest::copyToPagesOneStep()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference note = H::find(document, 0, QStringLiteral("text"));
    controller->selectAnnotations({ note });
    fixture.settle();

    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotationCopyToPages()));
    fixture.settle();
    pdf::PDFAppCopyToPagesDialog* dialog = qobject_cast<pdf::PDFAppCopyToPagesDialog*>(H::handler(tab)->getOpenDialog());
    QVERIFY(dialog);
    QCOMPARE(dialog->getCardWidth(), pdf::PDFAppCopyToPagesDialog::CARD_WIDTH);

    // badge 1024:21183: copy-to-pages on accent/blue/bg
    QCOMPARE(dialog->getBadgeIconName(), QStringLiteral("copy-to-pages"));
    QWidget* badge = nullptr;
    for (QWidget* child : dialog->findChildren<QWidget*>())
    {
        badge = (!badge && child->size() == QSize(36, 36) && child->accessibleName() == QObject::tr("Information")) ? child : badge;
    }
    QVERIFY(badge);
    const QImage badgeImage = badge->grab().toImage();
    QCOMPARE(badgeImage.pixelColor(18, 3).rgb(), pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::AccentBlueBg).rgb());

    // All pages: the source page is skipped - "Copy to 2 Pages"
    QCOMPARE(dialog->getTargetPages(), std::vector<pdf::PDFInteger>({ 1, 2 }));
    QCOMPARE(dialog->getCopyButton()->text(), QObject::tr("Copy to %n Pages", nullptr, 2));
    H::save(QStringLiteral("annotation-copy-to-pages"), H::renderWithPopups(fixture.window, { dialog }));

    // A range of the source page only: nothing to copy
    dialog->getPageSelection()->setMode(pdf::PDFPageSelection::Mode::Range);
    dialog->getPageSelection()->setRange(QStringLiteral("1"));
    QVERIFY(!dialog->getCopyButton()->isEnabled());
    dialog->getPageSelection()->setRange(QStringLiteral("3"));
    QCOMPARE(dialog->getTargetPages(), std::vector<pdf::PDFInteger>({ 2 }));
    QVERIFY(dialog->copy());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // The copy keeps its replies (the thread is copied)
    const std::vector<pdf::PDFObjectReference> page3 = document->getDocument()->getCatalog()->getPage(2)->getAnnotations();
    QVERIFY(!page3.empty());
    pdf::PDFObjectReference copiedNote;
    for (const pdf::PDFObjectReference& reference : page3)
    {
        const pdf::PDFAppAnnotationInfo info = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), reference);
        if (info.type == pdf::AnnotationType::Text && !info.isReply)
        {
            copiedNote = reference;
        }
    }
    QVERIFY(copiedNote.isValid());
    QCOMPARE(pdf::PDFAppAnnotationInfo::readReplies(document->getDocument().data(), copiedNote).size(), size_t(2));

    // The operation refuses hostile arguments
    QVERIFY(!pdf::PDFAppAnnotationOperations::copyToPages(document->getDocument(), { note }, {}));
    QVERIFY(!pdf::PDFAppAnnotationOperations::copyToPages(document->getDocument(), { note }, { -5, 1000 }));
    QVERIFY(!pdf::PDFAppAnnotationOperations::copyToPages(document->getDocument(), { pdf::PDFObjectReference(99999, 0) }, { 1 }));
    QVERIFY(!pdf::PDFAppAnnotationOperations::copyToPages(pdf::PDFDocumentPointer(), { note }, { 1 }));
}

void AppAnnotationEditTest::hideOnPage()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));
    controller->selectAnnotations({ circle });
    fixture.settle();

    // Hide on page: one step, the annotation leaves the selection, the bar hides
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotationHide()));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), circle).isHidden());
    QVERIFY(!controller->hasAnnotationSelection());
    QVERIFY(!H::handler(tab)->getAnnotationBar()->isVisible());

    // The operations: show again, a locked annotation stays (Table 167)
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setAnnotationsHidden(document->getDocument(), { circle, circle }, false);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    QVERIFY(!pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), circle).isHidden());
    QVERIFY(!pdf::PDFAppAnnotationOperations::setAnnotationsHidden(document->getDocument(), { circle }, false));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setAnnotationsHidden(document->getDocument(), { H::find(document, 0, QStringLiteral("locked")) }, true));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setAnnotationsHidden(document->getDocument(), { pdf::PDFObjectReference(99999, 0), pdf::PDFObjectReference() }, true));
}

void AppAnnotationEditTest::clipboardCommands()
{
    using H = AppAnnotationEditTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();

    // Copy, Paste at Original Position, Cut, Paste: one step each
    QVERIFY(tab->executeCommand(C::copy()));
    QVERIFY(tab->isCommandEnabled(C::paste()));
    QVERIFY(tab->executeCommand(C::pasteInPlace()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Paste annotations"));
    controller->selectAnnotations({ square });
    const size_t annotationCount = document->getDocument()->getCatalog()->getPage(0)->getAnnotations().size();
    QVERIFY(tab->executeCommand(C::cut()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(document->getDocument()->getCatalog()->getPage(0)->getAnnotations().size() < annotationCount);
    QVERIFY(tab->executeCommand(C::paste()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);

    // Between tabs: the clipboard of the annotations goes into another document
    pdf::PDFAppDocumentTab* second = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Target.pdf"), 2));
    QVERIFY(second && second->getDocument()->isReady());
    QVERIFY(second->isCommandEnabled(C::paste()));
    const size_t before = second->getDocument()->getDocument()->getCatalog()->getPage(0)->getAnnotations().size();
    QVERIFY(second->executeCommand(C::pasteInPlace()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(second->getDocument()), 1);
    QVERIFY(second->getDocument()->getDocument()->getCatalog()->getPage(0)->getAnnotations().size() > before);

    // Delete (Del) of the selection
    pdf::PDFAppAnnotationController* secondController = second->getView()->getAnnotationController();
    QVERIFY(secondController->hasAnnotationSelection());
    QVERIFY(second->executeCommand(C::deleteSelection()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(second->getDocument()), 2);
    QCOMPARE(second->getDocument()->getDocument()->getCatalog()->getPage(0)->getAnnotations().size(), before);

    // A locked document: Copy works, Cut / Paste / Delete do not
    controller->selectAnnotations({ H::find(document, 0, QStringLiteral("circle")) });
    document->setLocked(true);
    fixture.settle();
    QVERIFY(tab->isCommandEnabled(C::copy()));
    QVERIFY(!tab->isCommandEnabled(C::cut()));
    QVERIFY(!tab->isCommandEnabled(C::paste()));
    QVERIFY(!tab->isCommandEnabled(C::deleteSelection()));
    document->setLocked(false);
}

void AppAnnotationEditTest::dragBetweenTabs()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));

    // The data of a drag of the source tab (the mime type of the engine)
    QMimeData data;
    data.setData(QString::fromLatin1(pdf::PDFAnnotationManipulator::getMimeType()),
                 pdf::PDFAnnotationManipulator::serializeAnnotations(document->getDocument().data(), { square }));

    // The drop onto the canvas of another tab: one step of the target document
    pdf::PDFAppDocumentTab* second = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("DropTarget.pdf"), 2));
    QVERIFY(second && second->getDocument()->isReady());
    fixture.settle();
    pdf::PDFWidgetAnnotationManager* manager = second->getView()->getAnnotationController()->getAnnotationManager();
    QVERIFY(manager->canAcceptAnnotationDrag(&data));
    bool ok = false;
    const QPoint position = pdftest::AppDocumentFixture::pageToWidget(second->getView(), 0, QPointF(150, 200), &ok);
    QVERIFY(ok);
    QVERIFY(manager->handleAnnotationDrop(&data, position, Qt::CopyAction));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(second->getDocument()), 1);
    QCOMPARE(second->getDocument()->getDocument()->getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));

    // A locked target refuses the drop; garbage data are refused
    second->getDocument()->setLocked(true);
    fixture.settle();
    QVERIFY(!manager->handleAnnotationDrop(&data, position, Qt::CopyAction));
    second->getDocument()->setLocked(false);
    fixture.settle();
    QMimeData garbage;
    garbage.setData(QString::fromLatin1(pdf::PDFAnnotationManipulator::getMimeType()), QByteArray(4096, '\xFF'));
    QVERIFY(!manager->handleAnnotationDrop(&garbage, position, Qt::CopyAction));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(second->getDocument()), 1);
}

void AppAnnotationEditTest::keyboardNudge()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const QRectF before = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;
    controller->selectAnnotations({ square });
    fixture.settle();

    // An arrow moves the selection (one step), Del deletes it (the engine takes the keys)
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(tab->getView());
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Right);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle.left() > before.left());
    QTest::keyClick(canvas, Qt::Key_Delete);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(!H::find(document, 0, QStringLiteral("square")).isValid());
}

void AppAnnotationEditTest::lockedDocument()
{
    using H = AppAnnotationEditTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    document->setLocked(true);
    fixture.settle();

    // 1030:161012: the selection works, the bar shows only Comment
    controller->selectAnnotations({ H::find(document, 0, QStringLiteral("square")) });
    fixture.settle();
    QVERIFY(bar->isVisible());
    QVERIFY(bar->getCommentButton()->isVisible());
    QVERIFY(!bar->getPropertiesButton()->isVisible());
    QVERIFY(!bar->getSwatchRow()->isVisible());
    QVERIFY(!bar->getWidthComboBox()->isVisible());
    QVERIFY(!bar->getMoreButton()->isVisible());
    H::save(QStringLiteral("annotation-bar-locked"), H::render(fixture.window));

    // The menu: Open Comment, Copy, Properties… (read only), Select All
    QMenu* menu = H::handler(tab)->getMenus()->showMenu(H::selectionRequest(controller));
    QVERIFY(menu);
    QStringList ids;
    for (QAction* action : menu->actions())
    {
        if (!action->isSeparator())
        {
            ids << action->property("annotationMenuId").toString();
        }
    }
    QCOMPARE(ids, QStringList({ C::annotationComment(), C::annotationProperties(), C::copy(), C::annotationSelectAll() }));
    menu->close();
    fixture.settle();

    // The live change of the bar is refused, nothing is committed
    bar->applyColor(QColor(Qt::green));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    document->setLocked(false);
    fixture.settle();
    QVERIFY(bar->getSwatchRow()->isVisible());
}

void AppAnnotationEditTest::lockedAnnotation()
{
    using H = AppAnnotationEditTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference locked = H::find(document, 0, QStringLiteral("locked"));
    controller->selectAnnotations({ locked });
    fixture.settle();

    // Table 167: no color, width, geometry, deletion; Comment and Properties stay
    QVERIFY(bar->isVisible());
    QVERIFY(!bar->getSwatchRow()->isEnabled());
    QVERIFY(!bar->getWidthComboBox()->isEnabled());
    QVERIFY(bar->getPropertiesButton()->isEnabled());
    QVERIFY(!tab->isCommandEnabled(C::deleteSelection()));
    QVERIFY(!tab->isCommandEnabled(C::cut()));
    QVERIFY(!tab->isCommandEnabled(C::annotationGeometry()));
    QVERIFY(!tab->isCommandEnabled(C::annotationHide()));
    QVERIFY(!tab->isCommandEnabled(C::annotationRotateClockwise()));
    QVERIFY(tab->isCommandEnabled(C::annotationProperties()));
    QVERIFY(tab->isCommandEnabled(C::annotationReply()));
    QVERIFY(tab->isCommandEnabled(C::annotationCopyToPages()));

    // The status bar of 1026:21979
    QCOMPARE(bar->getStatusHint(), QStringLiteral("This annotation is locked. Unlock it in Properties."));
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), bar->getStatusHint());

    QMenu* menu = H::handler(tab)->getMenus()->showMenu(H::selectionRequest(controller));
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::deleteSelection()));
    QVERIFY(!pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationGeometry()));
    QVERIFY(pdf::PDFAppAnnotationMenus::findAction(menu, C::annotationProperties()));
    menu->close();

    // The operations refuse the geometry, the contents stay editable
    QVERIFY(!pdf::PDFAppAnnotationOperations::canModifyAnnotation(document->getDocument(), locked));
    QVERIFY(pdf::PDFAppAnnotationOperations::canModifyContents(document->getDocument(), locked));
    QVERIFY(!pdf::PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), { locked }));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setRectangle(document->getDocument(), locked, QRectF(0, 0, 10, 10)));
    QVERIFY(pdf::PDFAppAnnotationOperations::setContents(document->getDocument(), locked, QStringLiteral("x")));

    // A read-only annotation takes no reply and no text
    const pdf::PDFObjectReference readOnly = H::find(document, 0, QStringLiteral("readonly"));
    QVERIFY(!pdf::PDFAppAnnotationOperations::canModifyContents(document->getDocument(), readOnly));
    QVERIFY(!pdf::PDFAppAnnotationOperations::addReply(document->getDocument(), readOnly, QStringLiteral("x"), QStringLiteral("a"), nullptr));

    // setProperties (shared by the dialog and the future callers) keeps Table 167
    // itself (security review RS4): a Locked annotation changes only its text and
    // its lock flags, the other properties and flags stay; ReadOnly keeps the text
    using F = pdf::PDFAnnotationProperties;
    const pdf::PDFAnnotationProperties original = H::properties(document, locked);
    QVERIFY(original.flags.testFlag(pdf::PDFAnnotation::Locked));
    pdf::PDFAnnotationProperties changed = original;
    changed.color = QColor(Qt::green);
    changed.borderWidth = 7.0;
    changed.flags = pdf::PDFAnnotation::Flags(pdf::PDFAnnotation::Locked | pdf::PDFAnnotation::Hidden | pdf::PDFAnnotation::LockedContents);
    changed.contents = QStringLiteral("Changed text");
    QVERIFY(!pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), locked, changed, F::Fields(F::Color | F::Border)));
    const pdf::PDFAppAnnotationResult lockedResult = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), locked, changed, F::Fields(F::Color | F::Border | F::Flags | F::Contents));
    QVERIFY(lockedResult);
    const pdf::PDFDocumentPointer lockedDocument = *lockedResult;
    const pdf::PDFAnnotationProperties written = pdf::PDFAnnotationPropertyEditor::read(&lockedDocument->getStorage(), locked);
    QCOMPARE(written.color, original.color);
    QCOMPARE(written.borderWidth, original.borderWidth);
    QVERIFY(!written.flags.testFlag(pdf::PDFAnnotation::Hidden));
    QVERIFY(written.flags.testFlag(pdf::PDFAnnotation::Locked));
    QVERIFY(written.flags.testFlag(pdf::PDFAnnotation::LockedContents));
    QCOMPARE(written.contents, QStringLiteral("Changed text"));

    pdf::PDFAnnotationProperties readOnlyChanged = H::properties(document, readOnly);
    readOnlyChanged.contents = QStringLiteral("Changed text");
    QVERIFY(!pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), readOnly, readOnlyChanged, F::Fields(F::Contents)));
}

void AppAnnotationEditTest::undoRedoSelection()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();
    bar->applyColor(QColor(Qt::green));

    // Undo / redo keep the selection and the bar shows the color of the version
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ square }));
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->getSwatchRow()->getColor(), QColor(Qt::red));
    QVERIFY(document->getUndoStack()->redo());
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations(), std::vector<pdf::PDFObjectReference>({ square }));

    // The operations: the reply, the text and the deletion are undone as one step each
    pdf::PDFObjectReference reply;
    const pdf::PDFAppAnnotationResult replyResult = pdf::PDFAppAnnotationOperations::addReply(document->getDocument(), square, QStringLiteral("Reply"), QStringLiteral("Me"), &reply);
    QVERIFY(replyResult && reply.isValid());
    QVERIFY(controller->commit(*replyResult, pdf::PDFAppAnnotationChangeKind::Annotation));
    QCOMPARE(pdf::PDFAppAnnotationInfo::readReplies(document->getDocument().data(), square).size(), size_t(1));
    QVERIFY(document->getUndoStack()->undo());
    QCOMPARE(pdf::PDFAppAnnotationInfo::readReplies(document->getDocument().data(), square).size(), size_t(0));

    // Hostile arguments of the operations
    QVERIFY(!pdf::PDFAppAnnotationOperations::addReply(document->getDocument(), square, QStringLiteral("   "), QString(), nullptr));
    QVERIFY(!pdf::PDFAppAnnotationOperations::addReply(document->getDocument(), pdf::PDFObjectReference(99999, 0), QStringLiteral("x"), QString(), nullptr));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setContents(document->getDocument(), square, H::properties(document, square).contents));
    const pdf::PDFAppAnnotationResult contents = pdf::PDFAppAnnotationOperations::setContents(document->getDocument(), square, QString(100000, QLatin1Char('a')) + QChar(u'\u0001'));
    QVERIFY(contents);
    QVERIFY(controller->commit(*contents, pdf::PDFAppAnnotationChangeKind::Annotation));
    QCOMPARE(H::properties(document, square).contents.size(), pdf::PDFAppAnnotationOperations::MAXIMUM_TEXT_LENGTH);
    QVERIFY(!pdf::PDFAppAnnotationOperations::setRectangle(document->getDocument(), square, QRectF(0, 0, 1.0e9, 10)));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setRectangle(document->getDocument(), square, QRectF(qQNaN(), 0, 10, 10)));
    QVERIFY(!pdf::PDFAppAnnotationOperations::transformAnnotation(document->getDocument(), square, QTransform()));
    QVERIFY(!pdf::PDFAppAnnotationOperations::transformAnnotation(document->getDocument(), square, QTransform(0, 0, 0, 0, 0, 0)));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setEditablePoints(document->getDocument(), square, {}));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setEditablePoints(document->getDocument(), H::find(document, 0, QStringLiteral("polygon")), { QPointF(qInf(), 0) }));
    QVERIFY(!pdf::PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), {}));
    QVERIFY(!pdf::PDFAppAnnotationOperations::deleteAnnotations(pdf::PDFDocumentPointer(), { square }));
    QVERIFY(!pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), pdf::PDFObjectReference(99999, 0), pdf::PDFAnnotationProperties(),
                                                             pdf::PDFAnnotationProperties::All));

    // A deletion of an annotation and of its reply in one call (the reply goes with its owner)
    const pdf::PDFObjectReference note = H::find(document, 0, QStringLiteral("text"));
    const std::vector<pdf::PDFAppAnnotationInfo> replies = pdf::PDFAppAnnotationInfo::readReplies(document->getDocument().data(), note);
    QCOMPARE(replies.size(), size_t(2));
    const pdf::PDFAppAnnotationResult deleted = pdf::PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), { note, replies.front().reference });
    QVERIFY(deleted);
    QVERIFY(controller->commit(*deleted, pdf::PDFAppAnnotationChangeKind::Annotation));
    QVERIFY(!H::find(document, 0, QStringLiteral("text")).isValid());
}

void AppAnnotationEditTest::deletionMatrix()
{
    using H = AppAnnotationEditTestHelper;

    // Each dialog, the popup and a menu open; the tab, the window or the document
    // (a reload by another state) goes away - nothing crashes, nothing stays
    enum class Open { Properties, Geometry, CopyToPages, Popup, Menu, Bar };
    enum class Victim { Tab, Window, DocumentFailed };
    for (const Open open : { Open::Properties, Open::Geometry, Open::CopyToPages, Open::Popup, Open::Menu, Open::Bar })
    {
        for (const Victim victim : { Victim::Tab, Victim::Window, Victim::DocumentFailed })
        {
            pdftest::AppDocumentFixture fixture;
            QPointer<pdf::PDFAppDocumentTab> tab = H::openAnnotations(fixture);
            QVERIFY(tab);
            pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
            const pdf::PDFObjectReference square = H::find(tab->getDocument(), 0, QStringLiteral("square"));
            controller->selectAnnotations({ square });
            fixture.settle();

            QPointer<QWidget> widget;
            switch (open)
            {
                case Open::Properties:
                    widget = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
                    break;
                case Open::Geometry:
                    widget = pdf::PDFAppAnnotationGeometryDialog::open(controller, square, fixture.window);
                    break;
                case Open::CopyToPages:
                    widget = pdf::PDFAppCopyToPagesDialog::open(controller, fixture.window);
                    break;
                case Open::Popup:
                    tab->getView()->getCommentPopup()->showThread(square, true);
                    widget = tab->getView()->getCommentPopup();
                    break;
                case Open::Menu:
                    widget = H::handler(tab)->getMenus()->showMenu(H::selectionRequest(controller));
                    break;
                case Open::Bar:
                    widget = H::handler(tab)->getAnnotationBar();
                    break;
            }
            fixture.settle();
            QVERIFY(widget);
            QVERIFY2(widget->isVisible(), qPrintable(QString::number(int(open))));

            switch (victim)
            {
                case Victim::Tab:
                    delete tab.data();
                    break;
                case Victim::Window:
                    delete fixture.window.data();
                    break;
                case Victim::DocumentFailed:
                    tab->getDocument()->setFailed(QStringLiteral("x.pdf"), QStringLiteral("Damaged"));
                    break;
            }
            fixture.settle();

            const bool isDialog = open == Open::Properties || open == Open::Geometry || open == Open::CopyToPages;
            if (victim != Victim::DocumentFailed || isDialog)
            {
                QVERIFY2(!widget || !widget->isVisible(), "the dialog of a document, which is not shown, closes");
            }
        }
    }

    // The handler survives a deleted bar and deleted menus (the application deletes them)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    delete H::handler(tab)->getAnnotationBar();
    delete H::handler(tab)->getMenus();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    controller->requestMenu(H::selectionRequest(controller));
    fixture.settle();
    QVERIFY(fixture.window);
}

void AppAnnotationEditTest::deleteFromOwnSignals()
{
    using H = AppAnnotationEditTestHelper;

    // A slot of a signal of the dialogs and of the popup deletes the tab
    for (int scenario = 0; scenario < 5; ++scenario)
    {
        pdftest::AppDocumentFixture fixture;
        QPointer<pdf::PDFAppDocumentTab> tab = H::openAnnotations(fixture);
        QVERIFY(tab);
        pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
        const pdf::PDFObjectReference square = H::find(tab->getDocument(), 0, QStringLiteral("square"));
        controller->selectAnnotations({ square });
        fixture.settle();
        QObject receiver;
        auto deleteTab = [tab]() { delete tab.data(); };

        switch (scenario)
        {
            case 0:
            {
                pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
                connect(dialog, &pdf::PDFAppAnnotationPropertiesDialog::dialogFinished, &receiver, deleteTab);
                dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("x"));
                dialog->apply();
                break;
            }
            case 1:
            {
                pdf::PDFAppAnnotationGeometryDialog* dialog = pdf::PDFAppAnnotationGeometryDialog::open(controller, square, fixture.window);
                connect(dialog, &pdf::PDFAppAnnotationGeometryDialog::dialogFinished, &receiver, deleteTab);
                dialog->getField(QStringLiteral("width"))->setValue(5.0);
                dialog->apply();
                break;
            }
            case 2:
            {
                pdf::PDFAppCopyToPagesDialog* dialog = pdf::PDFAppCopyToPagesDialog::open(controller, fixture.window);
                connect(dialog, &pdf::PDFAppCopyToPagesDialog::dialogFinished, &receiver, deleteTab);
                dialog->copy();
                break;
            }
            case 3:
            {
                pdf::PDFAppCommentPopup* popup = tab->getView()->getCommentPopup();
                connect(popup, &pdf::PDFAppCommentPopup::replyAdded, &receiver, deleteTab);
                popup->showThread(square, true);
                popup->setReplyText(QStringLiteral("x"));
                popup->sendReply();
                break;
            }
            case 4:
            {
                pdf::PDFAppCommentPopup* popup = tab->getView()->getCommentPopup();
                connect(popup, &pdf::PDFAppCommentPopup::composeAccepted, &receiver, deleteTab);
                popup->showCompose(QRect(10, 10, 10, 10), QStringLiteral("x"), QStringLiteral("x"));
                popup->setComposedText(QStringLiteral("x"));
                popup->accept();
                break;
            }
        }
        fixture.settle();
        QVERIFY2(!tab, qPrintable(QString::number(scenario)));
        QVERIFY(fixture.window);
    }

    // The controller signal of the commit deletes the tab while the bar applies a color
    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = H::openAnnotations(fixture);
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();
    QPointer<pdf::PDFAppAnnotationBar> bar = H::handler(tab)->getAnnotationBar();
    QObject receiver;
    connect(controller, &pdf::PDFAppAnnotationController::changeCommitted, &receiver, [tab]() { tab->deleteLater(); });
    bar->applyColor(QColor(Qt::green));
    fixture.settle();
    QVERIFY(!tab);
    QVERIFY(!bar);
}

void AppAnnotationEditTest::themesAndScales()
{
    using H = AppAnnotationEditTestHelper;

    // Both themes at 1x, 1.5x and 2x: the bar, the popup, the dialogs have no leak color
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        pdftest::AppDocumentFixture fixture;
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
        QVERIFY(tab);
        pdf::PDFAppDocumentView* view = tab->getView();
        pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
        const pdf::PDFObjectReference note = H::find(tab->getDocument(), 0, QStringLiteral("text"));
        controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
        fixture.settle();
        pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
        pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
        popup->showThread(note, false);
        fixture.settle();

        for (const qreal ratio : { 1.0, 1.5, 2.0 })
        {
            const QString scale = QStringLiteral("%1x").arg(ratio);
            QCOMPARE(H::countLeakPixels(H::render(bar, ratio)), 0);
            for (QWidget* child : popup->findChildren<QWidget*>())
            {
                if (child->isVisible() && H::countLeakPixels(H::render(child, ratio)) > 0)
                {
                    qWarning() << "LEAK" << child->metaObject()->className() << child->objectName() << child->geometry() << H::countLeakPixels(H::render(child, ratio));
                }
            }
            QCOMPARE(H::countLeakPixels(H::render(popup, ratio)), 0);
            H::save(QStringLiteral("annotation-edit-%1-%2").arg(suffix, scale), H::render(fixture.window, ratio));
            H::save(QStringLiteral("annotation-popup-%1-%2").arg(suffix, scale), H::render(popup, ratio));
            H::save(QStringLiteral("annotation-bar-%1-%2").arg(suffix, scale), H::render(bar, ratio));
        }

        popup->cancel();
        popup->showCompose(QRect(300, 200, 20, 20), QStringLiteral("Sticky note"), QStringLiteral("note-note"));
        fixture.settle();
        QCOMPARE(H::countLeakPixels(H::render(popup)), 0);
        H::save(QStringLiteral("annotation-popup-compose-%1").arg(suffix), H::render(popup));
        popup->cancel();

        // The dialogs
        QList<QWidget*> dialogs;
        dialogs << pdf::PDFAppAnnotationPropertiesDialog::open(controller, note, fixture.window);
        fixture.settle();
        QCOMPARE(H::countLeakPixels(H::render(dialogs.back())), 0);
        H::save(QStringLiteral("annotation-properties-%1").arg(suffix), H::render(dialogs.back()));
        H::closeDialog(tab);
        dialogs << pdf::PDFAppAnnotationGeometryDialog::open(controller, H::find(tab->getDocument(), 0, QStringLiteral("square")), fixture.window);
        fixture.settle();
        QCOMPARE(H::countLeakPixels(H::render(dialogs.back())), 0);
        H::save(QStringLiteral("annotation-geometry-%1").arg(suffix), H::render(dialogs.back()));
        H::closeDialog(tab);
        dialogs << pdf::PDFAppCopyToPagesDialog::open(controller, fixture.window);
        fixture.settle();
        QCOMPARE(H::countLeakPixels(H::render(dialogs.back())), 0);
        H::save(QStringLiteral("annotation-copy-to-pages-%1").arg(suffix), H::render(dialogs.back()));
        H::closeDialog(tab);

        // The theme changes while the bar is shown: the bar repaints, no leak
        pdf::PDFThemeManager::getInstance()->setScheme(scheme == pdf::PDFThemeColors::Scheme::Light ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        fixture.settle();
        QCOMPARE(H::countLeakPixels(H::render(bar)), 0);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppAnnotationEditTest::rightToLeft()
{
    using H = AppAnnotationEditTestHelper;

    // RTL: the bar and the menus mirror (1030:161012), the handles and the page do not
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
        QVERIFY(tab);
        pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
        controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
        fixture.settle();
        pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
        QVERIFY(bar->isVisible());
        QVERIFY(bar->getMoreButton()->x() < bar->getNameLabel()->mapTo(bar, QPoint(0, 0)).x());
        H::save(QStringLiteral("annotation-bar-rtl"), H::render(fixture.window));

        QMenu* menu = H::handler(tab)->getMenus()->showMenu(H::selectionRequest(controller));
        QCOMPARE(menu->layoutDirection(), Qt::RightToLeft);
        H::save(QStringLiteral("annotation-menu-rtl"), H::renderWithPopups(fixture.window, { menu }));

        // The arrow of a reply points to the text (a directional icon mirrored as the chevrons)
        QCOMPARE(pdf::PDFAppPage::getDirectionalIconName(QStringLiteral("corner-down-right"), Qt::RightToLeft), QStringLiteral("corner-down-left"));
        const QIcon replyIcon = pdf::PDFAppAnnotationMenus::findAction(menu, pdf::PDFAppCommands::annotationReply())->icon();
        QCOMPARE(replyIcon.pixmap(16).toImage(), pdf::PDFThemeIcons::getIcon(QStringLiteral("corner-down-left")).pixmap(16).toImage());
        menu->close();
        tab->getView()->getCommentPopup()->showThread(H::find(tab->getDocument(), 0, QStringLiteral("text")), false);
        fixture.settle();
        int mirroredReplies = 0;
        for (const pdf::PDFAppCommentPopupIcon* icon : tab->getView()->getCommentPopup()->findChildren<pdf::PDFAppCommentPopupIcon*>())
        {
            QVERIFY(icon->getIconName() != QStringLiteral("corner-down-right"));
            mirroredReplies += icon->getIconName() == QStringLiteral("corner-down-left") ? 1 : 0;
        }
        QCOMPARE(mirroredReplies, 2);
        tab->getView()->getCommentPopup()->cancel();
        fixture.settle();

        // The popup prefers the left side of the anchor
        pdf::PDFAppCommentPopup* popup = tab->getView()->getCommentPopup();
        popup->showCompose(QRect(600, 200, 20, 20), QStringLiteral("Sticky note"), QStringLiteral("note-note"));
        QVERIFY(popup->getCardGeometry().right() < 600);
        H::save(QStringLiteral("annotation-popup-rtl"), H::render(fixture.window));
        popup->cancel();
        fixture.settle();
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppAnnotationEditTest::minimalWindow()
{
    using H = AppAnnotationEditTestHelper;

    // 720 x 480: the bar fits into the canvas (the swatches fold into More), the
    // dialogs fit into the window (the sections scroll)
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    QVERIFY(bar->isVisible());
    QVERIFY(bar->getCardGeometry().right() <= view->getCanvasArea()->width());
    QVERIFY(bar->getCardGeometry().left() >= 0);
    H::save(QStringLiteral("annotation-bar-minimal"), H::render(fixture.window));

    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    popup->showThread(H::find(tab->getDocument(), 0, QStringLiteral("text")), false);
    fixture.settle();
    if (popup->isVisible())
    {
        QVERIFY(popup->getCardGeometry().bottom() <= view->getCanvasArea()->height());
    }
    popup->cancel();

    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, H::find(tab->getDocument(), 0, QStringLiteral("freetext")), fixture.window);
    fixture.settle();
    QVERIFY(dialog->isVisible());
    H::save(QStringLiteral("annotation-properties-minimal"), H::renderWithPopups(fixture.window, { dialog }));
    QVERIFY(dialog->height() <= fixture.window->height() + 64);
    H::closeDialog(tab);
}

void AppAnnotationEditTest::accessibility()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();

    // The buttons of the bar have the names (and the tooltips) of their commands
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    QVERIFY(!bar->accessibleName().isEmpty());
    QVERIFY(!bar->getCommentButton()->accessibleName().isEmpty());
    QVERIFY(bar->getPropertiesButton()->toolTip().contains(QKeySequence(Qt::Key_F2).toString(QKeySequence::NativeText)));
    QVERIFY(!bar->getMoreButton()->accessibleName().isEmpty());
    QVERIFY(!bar->getWidthComboBox()->accessibleName().isEmpty());
    QVERIFY(!bar->getSwatchRow()->accessibleName().isEmpty());

    // The controls of the dialogs and of the popup
    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, H::find(tab->getDocument(), 0, QStringLiteral("square")), fixture.window);
    fixture.settle();
    for (const QString& name : { QStringLiteral("propertiesAuthor"), QStringLiteral("propertiesSubject"), QStringLiteral("propertiesText"), QStringLiteral("propertiesColor"),
                                 QStringLiteral("propertiesFill"), QStringLiteral("propertiesOpacity"), QStringLiteral("propertiesBorderWidth") })
    {
        QVERIFY2(!dialog->findChild<QWidget*>(name)->accessibleName().isEmpty(), qPrintable(name));
    }
    H::closeDialog(tab);

    pdf::PDFAppAnnotationGeometryDialog* geometry = pdf::PDFAppAnnotationGeometryDialog::open(controller, H::find(tab->getDocument(), 0, QStringLiteral("square")), fixture.window);
    fixture.settle();
    QVERIFY(!geometry->getReferencePointPicker()->accessibleName().isEmpty());
    QVERIFY(!geometry->getReferencePointPicker()->accessibleDescription().isEmpty());
    QVERIFY(!geometry->getField(QStringLiteral("width"))->accessibleName().isEmpty());
    H::closeDialog(tab);

    pdf::PDFAppCommentPopup* popup = tab->getView()->getCommentPopup();
    popup->showThread(H::find(tab->getDocument(), 0, QStringLiteral("text")), false);
    QVERIFY(!popup->accessibleName().isEmpty());
    QVERIFY(!popup->getReplyField()->accessibleName().isEmpty());
    QVERIFY(!popup->getCloseButton()->accessibleName().isEmpty());
    popup->cancel();
}

void AppAnnotationEditTest::languageChange()
{
    using H = AppAnnotationEditTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    controller->selectAnnotations({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();
    pdf::PDFAppCommentPopup* popup = tab->getView()->getCommentPopup();
    popup->showThread(H::find(tab->getDocument(), 0, QStringLiteral("text")), false);

    // A language change keeps the state of the bar and of the popup
    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(H::handler(tab)->getAnnotationBar(), &event);
    QCoreApplication::sendEvent(popup, &event);
    fixture.settle();
    QCOMPARE(H::handler(tab)->getAnnotationBar()->getNameLabel()->text(), QObject::tr("Rectangle"));
    QCOMPARE(popup->getMode(), pdf::PDFAppCommentPopup::Mode::Thread);
    QCOMPARE(popup->getReplyCount(), 2);
}

void AppAnnotationEditTest::annotationInfoTexts()
{
    using H = AppAnnotationEditTestHelper;

    // Every type has its name, the title of its properties and a badge
    for (int value = int(pdf::AnnotationType::Invalid); value <= int(pdf::AnnotationType::Unknown); ++value)
    {
        const pdf::AnnotationType type = static_cast<pdf::AnnotationType>(value);
        QVERIFY(!pdf::PDFAppAnnotationInfo::getTypeName(type).isEmpty());
        QVERIFY(!pdf::PDFAppAnnotationInfo::getPropertiesTitle(type).isEmpty());
        QString iconName;
        pdf::PDFAccentHue hue = pdf::PDFAccentHue::Neutral;
        pdf::PDFAppAnnotationInfo::getBadge(type, false, &iconName, &hue);
        QVERIFY(!iconName.isEmpty());
        QVERIFY(!pdf::PDFThemeIcons::getIcon(iconName).isNull());
    }
    QString replyIcon;
    pdf::PDFAccentHue replyHue = pdf::PDFAccentHue::Blue;
    pdf::PDFAppAnnotationInfo::getBadge(pdf::AnnotationType::Text, true, &replyIcon, &replyHue);
    QCOMPARE(replyIcon, QStringLiteral("corner-down-right"));

    // Dates: today, yesterday, an older date, an invalid one (O17: the language of the interface)
    const QDateTime now = QDateTime::currentDateTime();
    QVERIFY(pdf::PDFAppAnnotationInfo::formatDateTime(now).startsWith(QObject::tr("Today %1").arg(QString()).trimmed()));
    QVERIFY(pdf::PDFAppAnnotationInfo::formatDateTime(now.addDays(-1)).startsWith(QObject::tr("Yesterday %1").arg(QString()).trimmed()));
    QVERIFY(pdf::PDFAppAnnotationInfo::formatDateTime(QDateTime(QDate(2020, 3, 4), QTime(10, 20))).contains(QStringLiteral("2020")));
    QVERIFY(pdf::PDFAppAnnotationInfo::formatDateTime(QDateTime()).isEmpty());

    // Authors and stamps: an empty author is Anonymous, control characters are spaces
    QCOMPARE(pdf::PDFAppAnnotationInfo::getAuthorText(QString()), QObject::tr("Anonymous"));
    QCOMPARE(pdf::PDFAppAnnotationInfo::getAuthorText(QStringLiteral("A\nB\u0001C")), QStringLiteral("A B C"));
    QCOMPARE(pdf::PDFAppAnnotationInfo::getAuthorText(QString(5000, QLatin1Char('x'))).size(), pdf::PDFAppAnnotationInfo::MAXIMUM_NAME_LENGTH);
    pdf::PDFAppAnnotationInfo stamp;
    stamp.type = pdf::AnnotationType::Stamp;
    for (const QByteArray& name : pdf::PDFAnnotationPropertyEditor::getIconNames(pdf::AnnotationType::Stamp))
    {
        stamp.iconName = name;
        QVERIFY(pdf::PDFAppAnnotationInfo::getDisplayName(stamp).contains(QChar(u'“')));
    }
    stamp.iconName = "Custom\x01Name";
    QVERIFY(!pdf::PDFAppAnnotationInfo::getDisplayName(stamp).contains(QChar(u'\x01')));

    // The samples: the type, the page, the flags, the replies of a thread
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    const pdf::PDFAppAnnotationInfo note = pdf::PDFAppAnnotationInfo::read(document, H::find(tab->getDocument(), 0, QStringLiteral("text")));
    QCOMPARE(note.type, pdf::AnnotationType::Text);
    QCOMPARE(note.pageIndex, pdf::PDFInteger(0));
    QVERIFY(note.isMarkup);
    QVERIFY(!note.isReply);
    QCOMPARE(pdf::PDFAppAnnotationInfo::readReplies(document, note.reference).size(), size_t(2));
    QVERIFY(pdf::PDFAppAnnotationInfo::read(document, H::find(tab->getDocument(), 0, QStringLiteral("hidden"))).isHidden());
    QVERIFY(pdf::PDFAppAnnotationInfo::read(document, H::find(tab->getDocument(), 0, QStringLiteral("locked"))).isLocked());
    QVERIFY(pdf::PDFAppAnnotationInfo::read(document, H::find(tab->getDocument(), 0, QStringLiteral("readonly"))).isContentsLocked());
    QVERIFY(!pdf::PDFAppAnnotationInfo::read(nullptr, note.reference).isValid());
    QVERIFY(!pdf::PDFAppAnnotationInfo::read(document, pdf::PDFObjectReference(99999, 0)).isValid());
    QVERIFY(!pdf::PDFAppAnnotationInfo::read(document, document->getCatalog()->getPage(0)->getPageReference()).isValid());
    QVERIFY(pdf::PDFAppAnnotationInfo::readReplies(nullptr, note.reference).empty());
}

void AppAnnotationEditTest::reviewMergeSessionOnlyBarChanges()
{
    using H = AppAnnotationEditTestHelper;

    // Review RC-B: the merge session of the bar (live color, O20) took every
    // change of the annotations committed while it was open - a reply of the
    // comment popup, a command of the window - into the step "Change color"
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    QVERIFY(bar && bar->isVisible());

    // Two swatches of the bar: one step "Change color"
    bar->getSwatchRow()->getSwatch(1)->click();
    bar->getSwatchRow()->getSwatch(2)->click();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(controller->isMergeSessionOpen());
    const QColor barColor = H::properties(document, square).color;

    // A reply of the comment popup is its own step (the session ends)
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    popup->showThread(square, true);
    fixture.settle();
    popup->setReplyText(QStringLiteral("A reply during the session"));
    QVERIFY(popup->sendReply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Reply to annotation"));
    QVERIFY(!controller->isMergeSessionOpen());
    popup->cancel();
    fixture.settle();

    // Undo takes the reply only, the color stays
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(pdf::PDFAppAnnotationInfo::readReplies(document->getDocument().data(), square).size(), size_t(0));
    QCOMPARE(H::properties(document, square).color, barColor);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Change color"));

    // A command of the window during a session (Rotate) is a step of its own
    controller->selectAnnotations({ square });
    fixture.settle();
    bar->getSwatchRow()->getSwatch(3)->click();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QVERIFY(controller->isMergeSessionOpen());
    const QColor secondColor = H::properties(document, square).color;
    const QRectF rectangle = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;
    QVERIFY(view->executeCommand(pdf::PDFAppCommands::annotationRotateClockwise()));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 3);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Rotate annotations"));
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QCOMPARE(H::properties(document, square).color, secondColor);
    QCOMPARE(pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle, rectangle);

    // A change of another source (the Notes panel, an operation) during a session
    controller->selectAnnotations({ square });
    fixture.settle();
    bar->getSwatchRow()->getSwatch(4)->click();
    fixture.settle();
    const int steps = pdftest::AppDocumentFixture::undoCount(document);
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));
    controller->getAnnotationManager()->setAnnotationsHidden({ circle }, true);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), steps + 1);
    QVERIFY(!controller->isMergeSessionOpen());
}

void AppAnnotationEditTest::reviewPendingDescriptionNotLeaked()
{
    using H = AppAnnotationEditTestHelper;

    // Review RC-B: a description set before an engine call, which changed nothing,
    // named the next unrelated change (and replaced the description of a tool)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();

    // Remove Callout Line of an annotation without a callout: the engine changes nothing
    controller->setPendingDescription(QStringLiteral("Description of a tool"));
    pdf::PDFAnnotationMenuRequest request = H::selectionRequest(controller);
    request.widgetPosition = QPoint(10, 10);
    request.canRemoveCalloutLine = true;
    pdf::PDFAppAnnotationMenus* menus = H::handler(tab)->getMenus();
    QMenu* menu = menus->showMenu(request);
    QVERIFY(menu);
    QAction* removeCallout = pdf::PDFAppAnnotationMenus::findAction(menu, QStringLiteral("callout.remove"));
    QVERIFY(removeCallout);
    removeCallout->trigger();
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QCOMPARE(controller->getPendingDescription(), QStringLiteral("Description of a tool"));

    // The next change has its own (generic) description
    controller->setPendingDescription(QString());
    controller->getAnnotationManager()->setSelectedAnnotationsColor(QColor(Qt::blue));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), pdf::PDFAppAnnotationController::getDefaultDescription(pdf::PDFAppAnnotationChangeKind::Annotation));

    // A taken description is not given back
    controller->beginEngineChange(QStringLiteral("Named change"));
    controller->getAnnotationManager()->setSelectedAnnotationsColor(QColor(Qt::green));
    controller->endEngineChange();
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Named change"));
    QVERIFY(controller->getPendingDescription().isEmpty());

    // A request of a selection, which is empty (the selection has gone before the
    // queued request came): no menu, nothing read before the first item
    pdf::PDFAnnotationMenuRequest empty = H::selectionRequest(controller);
    empty.selection.clear();
    empty.annotation = pdf::PDFObjectReference();
    QVERIFY(!menus->createMenu(empty, fixture.window));
}

void AppAnnotationEditTest::reviewWidthListNoDuplicates()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    // Review RC-B: a width of the document, which is not in the list, was added
    // again at every update of the bar
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const pdf::PDFObjectReference circle = H::find(document, 0, QStringLiteral("circle"));
    F properties = H::properties(document, square);
    properties.borderWidth = 2.5;
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), square, properties, F::Border);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();

    controller->selectAnnotations({ square });
    fixture.settle();
    pdf::PDFComboBox* width = H::handler(tab)->getAnnotationBar()->getWidthComboBox();
    const int standardCount = int(pdf::PDFAppAnnotationBar::getWidths().size());
    QCOMPARE(width->count(), standardCount + 1);
    QCOMPARE(width->currentData().toDouble(), 2.5);
    for (int i = 0; i < 5; ++i)
    {
        Q_EMIT view->commandStatesChanged();
    }
    fixture.settle();
    QCOMPARE(width->count(), standardCount + 1);

    // Another selection with a standard width: the width of the document goes away
    F circleProperties = H::properties(document, circle);
    circleProperties.borderWidth = 2.0;
    result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), circle, circleProperties, F::Border);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    controller->selectAnnotations({ circle });
    fixture.settle();
    QCOMPARE(width->count(), standardCount);
    QCOMPARE(width->currentData().toDouble(), 2.0);
}

void AppAnnotationEditTest::reviewMenuReferenceRoundTrip()
{
    // Review RC-B: the reference of a row of the chooser was stored as QPoint (int):
    // numbers beyond 32 bits of a hand-written document selected another object
    const pdf::PDFObjectReference large(pdf::PDFInteger(1) << 40, (pdf::PDFInteger(1) << 33) + 5);
    QCOMPARE(pdf::PDFAppAnnotationMenus::fromVariant(pdf::PDFAppAnnotationMenus::toVariant(large)), large);
    const pdf::PDFObjectReference small(12, 0);
    QCOMPARE(pdf::PDFAppAnnotationMenus::fromVariant(pdf::PDFAppAnnotationMenus::toVariant(small)), small);
    QVERIFY(!pdf::PDFAppAnnotationMenus::fromVariant(QVariant()).isValid());
    QVERIFY(!pdf::PDFAppAnnotationMenus::fromVariant(QVariant(QStringLiteral("garbage"))).isValid());
    QVERIFY(!pdf::PDFAppAnnotationMenus::fromVariant(pdf::PDFAppAnnotationMenus::toVariant(pdf::PDFObjectReference())).isValid());
}

void AppAnnotationEditTest::reviewGeometryKeepsUnchangedValues()
{
    using H = AppAnnotationEditTestHelper;

    // Review RC-B: a change of one field read the other fields back from their
    // rounded values - the annotation was resized (100 pt = 35.3 mm = 100.06 pt)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setRectangle(document->getDocument(), square, QRectF(50.0, 60.0, 100.0, 70.0));
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();

    pdf::PDFAppAnnotationGeometryDialog* dialog = pdf::PDFAppAnnotationGeometryDialog::open(controller, square, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    dialog->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Millimeters);
    pdf::PDFDoubleSpinBox* x = dialog->getField(QStringLiteral("x"));
    x->setValue(x->value() + 10.0);
    QVERIFY(dialog->apply());
    fixture.settle();
    QRectF rectangle = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;
    QVERIFY(qAbs(rectangle.width() - 100.0) < 1e-6);
    QVERIFY(qAbs(rectangle.height() - 70.0) < 1e-6);
    QVERIFY(qAbs(rectangle.left() - (50.0 + 10.0 * 72.0 / 25.4)) < 0.2);

    // The points of a polygon: a change of one coordinate keeps the others exactly
    const pdf::PDFObjectReference polygon = H::find(document, 0, QStringLiteral("polygon"));
    const pdf::PDFAnnotationPtr parsed = pdf::PDFAnnotation::parse(&document->getDocument()->getStorage(), polygon);
    QVERIFY(parsed);
    const std::vector<QPointF> points = pdf::PDFAnnotationManipulator::getEditablePoints(parsed.data()).points;
    QVERIFY(points.size() >= 3);
    dialog = pdf::PDFAppAnnotationGeometryDialog::open(controller, polygon, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    dialog->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Inches);
    pdf::PDFDoubleSpinBox* pointX = dialog->getPointField(0, false);
    QVERIFY(pointX);
    pointX->setValue(pointX->value() + 0.5);
    QVERIFY(dialog->apply());
    fixture.settle();
    const pdf::PDFAnnotationPtr changed = pdf::PDFAnnotation::parse(&document->getDocument()->getStorage(), polygon);
    const std::vector<QPointF> changedPoints = pdf::PDFAnnotationManipulator::getEditablePoints(changed.data()).points;
    QCOMPARE(changedPoints.size(), points.size());
    QVERIFY(qAbs(changedPoints[0].x() - (points[0].x() + 36.0)) < 0.1);
    QVERIFY(qAbs(changedPoints[0].y() - points[0].y()) < 1e-6);
    for (size_t i = 1; i < points.size(); ++i)
    {
        QVERIFY(qAbs(changedPoints[i].x() - points[i].x()) < 1e-6);
        QVERIFY(qAbs(changedPoints[i].y() - points[i].y()) < 1e-6);
    }
}

void AppAnnotationEditTest::reviewPropertiesUnchangedSpinValues()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    // Review RC-B: a width, which the spin box shows rounded (0.75 pt as 0.8 pt),
    // was written by Apply of another change
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    F properties = H::properties(document, square);
    properties.borderWidth = 0.75;
    properties.opacity = 0.333;
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), square, properties, F::Fields(F::Border | F::Opacity));
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();
    QCOMPARE(H::properties(document, square).borderWidth, 0.75);

    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::None));
    dialog->findChild<pdf::PDFPlainTextEdit*>(QStringLiteral("propertiesText"))->setPlainText(QStringLiteral("Only the text"));
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::Contents));
    QVERIFY(dialog->apply());
    fixture.settle();
    const F changed = H::properties(document, square);
    QCOMPARE(changed.contents, QStringLiteral("Only the text"));
    QCOMPARE(changed.borderWidth, 0.75);
    QVERIFY(qAbs(changed.opacity - 0.333) < 0.002);

    // A value changed by the user is written
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    fixture.settle();
    dialog->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("propertiesBorderWidth"))->setValue(1.5);
    QCOMPARE(dialog->getChangedFields(), F::Fields(F::Border));
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(H::properties(document, square).borderWidth, 1.5);
}

void AppAnnotationEditTest::reviewPropertiesLinkTarget()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::links(fixture.folder()));
    QVERIFY(tab && tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference link = document->getDocument()->getCatalog()->getPage(0)->getAnnotations().front();
    const pdf::PDFObjectReference page3 = document->getDocument()->getCatalog()->getPage(2)->getPageReference();

    // Go to target goes to the destination (its place and its zoom), not only to its page
    F properties = H::properties(document, link);
    properties.destination = pdf::PDFDestination::createXYZ(page3, 20.0, 400.0, 2.0);
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), link, properties, F::Destination);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();
    view->setZoom(1.0);
    view->goToPage(0);
    fixture.settle();
    QVERIFY(qAbs(view->getZoom() - 2.0) > 0.1);

    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, link, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    dialog->findChild<QPushButton*>(QStringLiteral("propertiesGoToTarget"))->click();
    fixture.settle();
    QVERIFY(dialog->isVisible());
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(2));
    QVERIFY(qAbs(view->getZoom() - 2.0) < 0.01);

    // An edited destination (Whole page of page 2) is the target too
    pdf::PDFComboBox* type = dialog->findChild<pdf::PDFComboBox*>(QStringLiteral("propertiesDestinationType"));
    type->setCurrentIndex(type->findData(int(pdf::DestinationType::Fit)));
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesPage"))->setText(view->getPageLabel(1));
    dialog->findChild<QPushButton*>(QStringLiteral("propertiesGoToTarget"))->click();
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(qAbs(view->getZoom() - 2.0) > 0.01);
    dialog->reject();
    fixture.settle();

    // A link to an object, which is not a page: the subtitle has no page number
    // (review RC-B: "link to page 0" of INVALID_PAGE_INDEX + 1)
    pdf::PDFDocumentModifier modifier(document->getDocument().data());
    modifier.markAnnotationsChanged();
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("A");
    factory.beginDictionary();
    factory.beginDictionaryItem("S");
    factory << pdf::WrapName("GoTo");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("D");
    factory.beginArray();
    factory << link;
    factory << pdf::WrapName("Fit");
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    modifier.getBuilder()->mergeTo(link, factory.takeObject());
    QVERIFY(modifier.finalize());
    QVERIFY(controller->commit(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()), pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();
    dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, link, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    for (const pdf::PDFLabel* label : dialog->findChildren<pdf::PDFLabel*>())
    {
        QVERIFY2(!label->text().contains(QObject::tr("link to page %1").arg(0)) && !label->text().contains(QStringLiteral("18446744073709551")), qPrintable(label->text()));
    }
    dialog->reject();
    fixture.settle();
}

void AppAnnotationEditTest::reviewPropertiesLanguageChange()
{
    using H = AppAnnotationEditTestHelper;

    // Review RC-B (known item): the properties dialog did not retranslate while open
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    pdf::PDFAppAnnotationPropertiesDialog* dialog = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    QVERIFY(dialog);
    fixture.settle();

    // Edited values survive the language change
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->setText(QStringLiteral("Edited author"));
    dialog->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("propertiesBorderWidth"))->setValue(4.5);
    dialog->findChild<QCheckBox*>(QStringLiteral("propertiesFlag:%1").arg(int(pdf::PDFAnnotation::NoZoom)))->setChecked(true);
    dialog->activateWindow();
    dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesSubject"))->setFocus(Qt::OtherFocusReason);
    const bool isSubjectFocused = QApplication::focusWidget() == dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesSubject"));
    const pdf::PDFAnnotationProperties::Fields changedBefore = dialog->getChangedFields();

    AnnotationEditTestTranslator* translator = new AnnotationEditTestTranslator(qApp);
    QCoreApplication::installTranslator(translator);
    fixture.settle();
    QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->text(), QStringLiteral("§Apply"));
    QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("propertiesCancel"))->text(), QStringLiteral("§Cancel"));
    QVERIFY(dialog->windowTitle().startsWith(QStringLiteral("§")));
    bool isRowLabelTranslated = false;
    bool isTitleTranslated = false;
    for (const pdf::PDFLabel* label : dialog->findChildren<pdf::PDFLabel*>())
    {
        isRowLabelTranslated = isRowLabelTranslated || label->text() == QStringLiteral("§Author");
        isTitleTranslated = isTitleTranslated || label->text() == QStringLiteral("§Properties of the rectangle");
        QVERIFY2(label->text() != QStringLiteral("Author"), "an old label of the body is left");
    }
    QVERIFY(isRowLabelTranslated);
    QVERIFY(isTitleTranslated);
    QCOMPARE(dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesAuthor"))->text(), QStringLiteral("Edited author"));
    QCOMPARE(dialog->findChild<pdf::PDFDoubleSpinBox*>(QStringLiteral("propertiesBorderWidth"))->value(), 4.5);
    QVERIFY(dialog->findChild<QCheckBox*>(QStringLiteral("propertiesFlag:%1").arg(int(pdf::PDFAnnotation::NoZoom)))->isChecked());
    QCOMPARE(dialog->getChangedFields(), changedBefore);
    if (isSubjectFocused)
    {
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(dialog->findChild<pdf::PDFLineEdit*>(QStringLiteral("propertiesSubject"))));
    }

    QCoreApplication::removeTranslator(translator);
    delete translator;
    fixture.settle();
    QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("propertiesApply"))->text(), QObject::tr("Apply"));

    // Apply writes the edited values as one step
    QVERIFY(dialog->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    const pdf::PDFAnnotationProperties properties = H::properties(document, square);
    QCOMPARE(properties.author, QStringLiteral("Edited author"));
    QCOMPARE(properties.borderWidth, 4.5);
    QVERIFY(properties.flags.testFlag(pdf::PDFAnnotation::NoZoom));
}

void AppAnnotationEditTest::reviewDialogsLanguageChange()
{
    using H = AppAnnotationEditTestHelper;

    // Review RC-B: the geometry and the Copy to Pages dialogs did not retranslate
    // while open either
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const QRectF original = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;

    // Geometry: the unit, the reference point and an edited value survive
    pdf::PDFAppAnnotationGeometryDialog* geometry = pdf::PDFAppAnnotationGeometryDialog::open(controller, square, fixture.window);
    QVERIFY(geometry);
    fixture.settle();
    geometry->setUnit(pdf::PDFAppAnnotationGeometryDialog::Unit::Points);
    geometry->getReferencePointPicker()->setCurrentIndex(8);
    geometry->getField(QStringLiteral("width"))->setValue(original.width() + 20.0);

    AnnotationEditTestTranslator* translator = new AnnotationEditTestTranslator(qApp);
    QCoreApplication::installTranslator(translator);
    fixture.settle();
    QCOMPARE(geometry->findChild<QPushButton*>(QStringLiteral("geometryApply"))->text(), QStringLiteral("\u00A7Apply"));
    QCOMPARE(geometry->findChild<QPushButton*>(QStringLiteral("geometryCancel"))->text(), QStringLiteral("\u00A7Cancel"));
    bool isUnitsTranslated = false;
    for (const pdf::PDFLabel* label : geometry->findChildren<pdf::PDFLabel*>())
    {
        isUnitsTranslated = isUnitsTranslated || label->text() == QStringLiteral("\u00A7Units");
        QVERIFY2(label->text() != QStringLiteral("Units"), "an old label of the body is left");
    }
    QVERIFY(isUnitsTranslated);
    QCOMPARE(geometry->getUnitControl()->getCurrentIndex(), int(pdf::PDFAppAnnotationGeometryDialog::Unit::Points));
    QCOMPARE(geometry->getReferencePointPicker()->getCurrentIndex(), 8);
    QVERIFY(qAbs(geometry->getField(QStringLiteral("width"))->value() - (original.width() + 20.0)) < 0.1);
    QCoreApplication::removeTranslator(translator);
    fixture.settle();
    QVERIFY(geometry->apply());
    fixture.settle();
    const QRectF changed = pdf::PDFAppAnnotationInfo::read(document->getDocument().data(), square).rectangle;
    QVERIFY(qAbs(changed.width() - (original.width() + 20.0)) < 0.1);
    QVERIFY(qAbs(changed.right() - original.right()) < 0.1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // Copy to Pages
    controller->selectAnnotations({ square });
    fixture.settle();
    pdf::PDFAppCopyToPagesDialog* copy = pdf::PDFAppCopyToPagesDialog::open(controller, fixture.window);
    QVERIFY(copy);
    fixture.settle();
    QCoreApplication::installTranslator(translator);
    fixture.settle();
    QCOMPARE(copy->findChild<QPushButton*>(QStringLiteral("copyToPagesCancel"))->text(), QStringLiteral("\u00A7Cancel"));
    QVERIFY(copy->getCopyButton()->text().startsWith(QStringLiteral("\u00A7")));
    bool isTitleTranslated = false;
    for (const pdf::PDFLabel* label : copy->findChildren<pdf::PDFLabel*>())
    {
        isTitleTranslated = isTitleTranslated || label->text().startsWith(QStringLiteral("\u00A7Copy the"));
    }
    QVERIFY(isTitleTranslated);
    QCoreApplication::removeTranslator(translator);
    delete translator;
    fixture.settle();
    QCOMPARE(copy->findChild<QPushButton*>(QStringLiteral("copyToPagesCancel"))->text(), QObject::tr("Cancel"));
    copy->reject();
    fixture.settle();
}

void AppAnnotationEditTest::reviewDragOverTabBar()
{
    using H = AppAnnotationEditTestHelper;

    // Review RC-B (known item): annotations dragged over a tab of the tab bar
    // switch to the tab, so they can be dropped onto its canvas
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* first = H::openAnnotations(fixture);
    QVERIFY(first);
    pdf::PDFDocumentTabBar* tabBar = fixture.window->getTabBar();
    const int firstIndex = tabBar->currentIndex();
    pdf::PDFAppDocumentTab* second = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("DragTarget.pdf"), 2));
    QVERIFY(second && second->getDocument()->isReady());
    fixture.settle();
    const int secondIndex = tabBar->currentIndex();
    QVERIFY(firstIndex != secondIndex);
    QCOMPARE(fixture.currentDocumentTab(), second);

    QMimeData data;
    data.setData(QString::fromLatin1(pdf::PDFAnnotationManipulator::getMimeType()),
                 pdf::PDFAnnotationManipulator::serializeAnnotations(first->getDocument()->getDocument().data(), { H::find(first->getDocument(), 0, QStringLiteral("circle")) }));
    const QPoint position = tabBar->mapTo(fixture.window.data(), tabBar->tabRect(firstIndex).center());

    // Other data (text) never switch the tab
    QMimeData text;
    text.setText(QStringLiteral("text"));
    QDragEnterEvent textEnter(position, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window, &textEnter);
    QTest::qWait(qApp->style()->styleHint(QStyle::SH_TabBar_ChangeCurrentDelay) + 200);
    QCOMPARE(tabBar->currentIndex(), secondIndex);

    // A drag left before the delay does not switch
    QDragEnterEvent enter(position, Qt::CopyAction | Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window, &enter);
    QVERIFY(enter.isAccepted());
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(fixture.window, &leave);
    QTest::qWait(qApp->style()->styleHint(QStyle::SH_TabBar_ChangeCurrentDelay) + 200);
    QCOMPARE(tabBar->currentIndex(), secondIndex);

    // The pointer rests on the tab: it becomes current, the window takes no drop
    QDragEnterEvent enterAgain(position, Qt::CopyAction | Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window, &enterAgain);
    QDragMoveEvent move(position + QPoint(1, 0), Qt::CopyAction | Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window, &move);
    QVERIFY(!move.isAccepted());
    QTRY_COMPARE_WITH_TIMEOUT(tabBar->currentIndex(), firstIndex, 3000);
    QCOMPARE(fixture.currentDocumentTab(), first);

    // A drop onto the window (not onto a canvas) changes nothing
    QDropEvent drop(QPointF(position), Qt::CopyAction | Qt::MoveAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(fixture.window, &drop);
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(first->getDocument()), 0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(second->getDocument()), 0);
}

void AppAnnotationEditTest::reviewAuthorBidiAndMnemonics()
{
    using H = AppAnnotationEditTestHelper;
    using F = pdf::PDFAnnotationProperties;

    // Owner decision (security review of step 4): an author never carries
    // bidirectional controls, an ampersand of a data text is no mnemonic of a menu
    const QString hostile = QStringLiteral("Tom \u202E& Jerry\u2066\u2067\u2068\u2069\u202A\u202B\u202C\u202D\u200E\u200F\u061C");
    QCOMPARE(pdf::PDFAppAnnotationInfo::getAuthorText(hostile), QStringLiteral("Tom & Jerry"));
    QCOMPARE(pdf::PDFAppAnnotationInfo::sanitizeName(hostile), QStringLiteral("Tom & Jerry"));
    QCOMPARE(pdf::PDFAppContext::sanitizeAuthorName(hostile), QStringLiteral("Tom & Jerry"));
    QCOMPARE(pdf::PDFAppAnnotationInfo::sanitizeName(QStringLiteral("\u05D0\u200D\u05D1")), QStringLiteral("\u05D0\u200D\u05D1"));

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference overlapA = H::find(document, 1, QStringLiteral("overlapA"));
    const pdf::PDFObjectReference overlapB = H::find(document, 1, QStringLiteral("overlapB"));
    QVERIFY(overlapA.isValid() && overlapB.isValid());
    F properties = H::properties(document, overlapA);
    properties.author = hostile;
    pdf::PDFAppAnnotationResult result = pdf::PDFAppAnnotationOperations::setProperties(document->getDocument(), overlapA, properties, F::Author);
    QVERIFY(result);
    QVERIFY(controller->commit(*result, pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();

    // The chooser: the row shows the author with the ampersand, without the controls
    pdf::PDFAnnotationMenuRequest request;
    request.kind = pdf::PDFAnnotationMenuRequest::Kind::Overlap;
    request.annotationsUnderCursor = { overlapA, overlapB };
    request.annotation = overlapA;
    request.pageIndex = 1;
    request.isModificationAllowed = true;
    request.globalPosition = QPoint(100, 60);
    pdf::PDFAppAnnotationMenus* menus = H::handler(tab)->getMenus();
    std::unique_ptr<QMenu> menu(menus->createMenu(request, fixture.window));
    QVERIFY(menu);
    QAction* row = menu->findChild<QAction*>(QStringLiteral("chooser:0"));
    QVERIFY(row);
    QVERIFY2(row->text().endsWith(QStringLiteral("\tTom && Jerry")), qPrintable(row->text()));
    for (const QChar character : row->text())
    {
        QVERIFY(character.category() != QChar::Other_Format);
    }

    // The bar and the comment popup show the author without the controls
    controller->selectAnnotations({ overlapA });
    fixture.settle();
    QCOMPARE(H::handler(tab)->getAnnotationBar()->getAuthorLabel()->text(), QStringLiteral("Tom & Jerry"));
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    popup->showThread(overlapA, false);
    fixture.settle();
    QCOMPARE(popup->getTitleLabel()->text(), QStringLiteral("Tom & Jerry"));
    popup->cancel();
    fixture.settle();

}

void AppAnnotationEditTest::reviewCopyToPagesLimit()
{
    using H = AppAnnotationEditTestHelper;

    // Owner decision (security review of step 4): one Copy to Pages makes at most
    // MAXIMUM_COPIES annotations (selected annotations x target pages)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("CopyLimit.pdf"), 320));
    QVERIFY(tab && tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();

    // 320 annotations on page 1: 320 x 319 copies to all pages
    pdf::PDFDocumentModifier modifier(document->getDocument().data());
    modifier.markAnnotationsChanged();
    const pdf::PDFObjectReference page = document->getDocument()->getCatalog()->getPage(0)->getPageReference();
    std::vector<pdf::PDFObjectReference> annotations;
    for (int i = 0; i < 320; ++i)
    {
        annotations.push_back(modifier.getBuilder()->createAnnotationSquare(page, QRectF(10 + (i % 20) * 25, 10 + (i / 20) * 25, 20, 20), 1.0, QColor(),
                                                                             QColor(Qt::red), QStringLiteral("Tester"), QStringLiteral("limit"), QString()));
    }
    QVERIFY(modifier.finalize());
    QVERIFY(controller->commit(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()), pdf::PDFAppAnnotationChangeKind::Annotation));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    std::vector<pdf::PDFInteger> allPages;
    for (pdf::PDFInteger i = 0; i < 320; ++i)
    {
        allPages.push_back(i);
    }
    QCOMPARE(pdf::PDFAppAnnotationOperations::getCopyToPagesCount(document->getDocument(), annotations, allPages), size_t(320 * 319));
    QVERIFY(!pdf::PDFAppAnnotationOperations::copyToPages(document->getDocument(), annotations, allPages));
    const std::vector<pdf::PDFInteger> fewPages = { 1, 2, 3 };
    QCOMPARE(pdf::PDFAppAnnotationOperations::getCopyToPagesCount(document->getDocument(), annotations, fewPages), size_t(960));
    QVERIFY(pdf::PDFAppAnnotationOperations::copyToPages(document->getDocument(), annotations, fewPages));

    // The dialog: All pages is refused with the message, a range of a few pages is allowed
    controller->selectAnnotations(annotations);
    fixture.settle();
    pdf::PDFAppCopyToPagesDialog* dialog = pdf::PDFAppCopyToPagesDialog::open(controller, fixture.window);
    QVERIFY(dialog);
    fixture.settle();
    pdf::PDFPageSelection* selection = dialog->getPageSelection();
    selection->setMode(pdf::PDFPageSelection::Mode::All);
    QVERIFY(!dialog->getCopyButton()->isEnabled());
    QVERIFY(selection->getCustomError().contains(QLocale().toString(qulonglong(pdf::PDFAppAnnotationOperations::MAXIMUM_COPIES))));
    QVERIFY(!dialog->copy());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    selection->setMode(pdf::PDFPageSelection::Mode::Range);
    selection->setRange(QStringLiteral("2-4"));
    QVERIFY(selection->getCustomError().isEmpty());
    QVERIFY(dialog->getCopyButton()->isEnabled());
    QVERIFY(dialog->copy());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(document->getDocument()->getCatalog()->getPage(1)->getAnnotations().size(), size_t(320));
}

void AppAnnotationEditTest::reviewCommitBlockedByOperation()
{
    using H = AppAnnotationEditTestHelper;
    using Ops = pdf::PDFAppAnnotationOperations;

    // A change of the application made while a blocking operation runs (a save)
    // is refused with its own reason: nothing is committed, the status says why
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const int undoCount = pdftest::AppDocumentFixture::undoCount(document);
    const pdf::PDFAppAnnotationResult change = Ops::setAnnotationsHidden(document->getDocument(), { square }, true);
    QVERIFY(change.has_value());

    QPointer<pdf::PDFAppDocumentOperation> operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Save, QStringLiteral("Saving"), true);
    QVERIFY(operation);
    QVERIFY(document->isBlockedByOperation());
    QSignalSpy refusedSpy(controller, &pdf::PDFAppAnnotationController::changeRefused);
    QVERIFY(!controller->commit(*change, pdf::PDFAppAnnotationChangeKind::Annotation, QStringLiteral("Hide")));
    QCOMPARE(refusedSpy.count(), 1);
    QCOMPARE(refusedSpy.front().front().toString(), QStringLiteral("The document cannot be changed while an operation is running."));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount);

    // The bar of a selected annotation is refused the same way (live color)
    controller->selectAnnotations({ square });
    fixture.settle();
    H::handler(tab)->getAnnotationBar()->applyColor(QColor(Qt::green));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount);

    // After the operation the same change is committed (one step)
    operation->finish();
    fixture.settle();
    QVERIFY(!document->isBlockedByOperation());
    QVERIFY(controller->commit(*change, pdf::PDFAppAnnotationChangeKind::Annotation, QStringLiteral("Hide")));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount + 1);

    // A change without a document is no step
    QVERIFY(!controller->commit(pdf::PDFModifiedDocument(), pdf::PDFAppAnnotationChangeKind::Annotation, QString()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoCount + 1);
}

void AppAnnotationEditTest::reviewBarAndPopupUpdates()
{
    using H = AppAnnotationEditTestHelper;

    // Wrap-up RC-B: the bar read the selection at every change of the command
    // states, the reply field of the popup read the annotation at every key
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference square = H::find(tab->getDocument(), 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->getWidthComboBox()->currentData().toDouble(), 2.0);

    // The same selection, version and editing: the controls are not read again
    // (a probe value of the width select stays)
    {
        const QSignalBlocker blocker(bar->getWidthComboBox());
        bar->getWidthComboBox()->setCurrentIndex(0);
    }
    Q_EMIT view->commandStatesChanged();
    QCOMPARE(bar->getWidthComboBox()->currentIndex(), 0);

    // The lock (the editing changes) reads the bar again; so does a new selection
    tab->getDocument()->setLocked(true);
    fixture.settle();
    QVERIFY(!bar->getWidthComboBox()->isVisible());
    tab->getDocument()->setLocked(false);
    fixture.settle();
    QVERIFY(bar->getWidthComboBox()->isVisible());
    QCOMPARE(bar->getWidthComboBox()->currentData().toDouble(), 2.0);
    controller->selectAnnotations({});
    fixture.settle();
    QVERIFY(!bar->isVisible());
    Q_EMIT view->commandStatesChanged();
    QVERIFY(!bar->isVisible());
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(bar->isVisible());

    // Typing a reply enables the primary button only (a probe text stays)
    const pdf::PDFObjectReference note = H::find(tab->getDocument(), 0, QStringLiteral("text"));
    controller->requestComment(note, false);
    fixture.settle();
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    QVERIFY(popup->getReplyField()->isVisible());
    QVERIFY(!popup->getPrimaryButton()->isEnabled());
    popup->getPrimaryButton()->setText(QStringLiteral("probe"));
    QTest::keyClicks(popup->getReplyField(), QStringLiteral("Typed"));
    QVERIFY(popup->getPrimaryButton()->isEnabled());
    QCOMPARE(popup->getPrimaryButton()->text(), QStringLiteral("probe"));
    popup->getReplyField()->clear();
    QVERIFY(!popup->getPrimaryButton()->isEnabled());
    QTest::keyClicks(popup->getReplyField(), QStringLiteral("   "));
    QVERIFY(!popup->getPrimaryButton()->isEnabled());
}

void AppAnnotationEditTest::reviewOperationsHostileArguments()
{
    using H = AppAnnotationEditTestHelper;
    using Ops = pdf::PDFAppAnnotationOperations;

    // Every operation refuses a missing document, an unknown reference, a page
    // out of range and empty input (nullopt, never an exception or a crash)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    const pdf::PDFDocumentPointer document = tab->getDocument()->getDocument();
    const pdf::PDFDocumentPointer none;
    const pdf::PDFObjectReference square = H::find(tab->getDocument(), 0, QStringLiteral("square"));
    const pdf::PDFObjectReference unknown(999999, 0);
    pdf::PDFAppNewAnnotation data;
    data.author = QStringLiteral("Review");
    pdf::PDFObjectReference created;
    std::vector<pdf::PDFObjectReference> createdList;

    pdf::PDFAppTextMarkupPart part;
    part.pageIndex = 0;
    part.quadrilaterals = QPolygonF({ QPointF(10, 10), QPointF(50, 10), QPointF(50, 20), QPointF(10, 20) });
    pdf::PDFAppTextMarkupPart farPart = part;
    farPart.pageIndex = 1000;

    for (const pdf::PDFDocumentPointer& target : { none, document })
    {
        const bool isNone = target.isNull();
        QVERIFY(!Ops::createTextMarkups(target, pdf::AnnotationType::Highlight, isNone ? std::vector<pdf::PDFAppTextMarkupPart>{ part } : std::vector<pdf::PDFAppTextMarkupPart>{ farPart }, data, &createdList));
        QVERIFY(!Ops::createTextMarkups(target, pdf::AnnotationType::Square, { part }, data, &createdList));
        QVERIFY(!Ops::createStickyNote(target, isNone ? 0 : -1, QPointF(10, 10), pdf::TextAnnotationIcon::Note, data, &created));
        QVERIFY(!Ops::createFreeText(target, isNone ? 0 : 1000, QRectF(10, 10, 50, 20), QStringLiteral("x"), pdf::PDFFreeTextStyle(), data, &created));
        QVERIFY(!Ops::createWebLink(target, 0, QRectF(10, 10, 50, 20), isNone ? QStringLiteral("https://example.com") : QStringLiteral("javascript:alert(1)"), &created));
        QVERIFY(!Ops::createPageLink(target, isNone ? 0 : 1000, QRectF(10, 10, 50, 20), pdf::PDFDestination(), &created));
        QVERIFY(!Ops::deleteAnnotations(target, { isNone ? square : unknown }));
        QVERIFY(!Ops::setAnnotationsHidden(target, { isNone ? square : unknown }, true));
        QVERIFY(!Ops::setContents(target, isNone ? square : unknown, QStringLiteral("x")));
        QVERIFY(!Ops::addReply(target, isNone ? square : unknown, QStringLiteral("x"), QStringLiteral("y"), &created));
        QVERIFY(!Ops::copyToPages(target, { isNone ? square : unknown }, { 1 }));
        QCOMPARE(Ops::getCopyToPagesCount(target, { isNone ? square : unknown }, { 1 }), size_t(0));
        QVERIFY(!Ops::canModifyAnnotation(target, isNone ? square : unknown));
        QVERIFY(!Ops::canModifyContents(target, isNone ? square : unknown));
        QVERIFY(!Ops::setRectangle(target, isNone ? square : unknown, QRectF(10, 10, 20, 20)));
        QVERIFY(!Ops::transformAnnotation(target, isNone ? square : unknown, QTransform().rotate(90)));
        QVERIFY(!Ops::setEditablePoints(target, isNone ? square : unknown, { QPointF(1, 1) }));
        QVERIFY(!Ops::renameOutlineItem(target, { 0 }, QStringLiteral("x")));
        QVERIFY(!Ops::removeOutlineItem(target, { 0 }));
        QVERIFY(!Ops::setOutlineItemStyle(target, { 0 }, true, true));
        QVERIFY(!Ops::setOutlineItemDestination(target, { 0 }, pdf::PDFDestination()));
    }

    // Empty input of an existing document
    QVERIFY(!Ops::createTextMarkups(document, pdf::AnnotationType::Highlight, {}, data, &createdList));
    QVERIFY(!Ops::deleteAnnotations(document, {}));
    QVERIFY(!Ops::setAnnotationsHidden(document, {}, true));
    QVERIFY(!Ops::addReply(document, square, QStringLiteral("   "), QStringLiteral("y"), &created));
    QVERIFY(!Ops::copyToPages(document, { square }, {}));
    QVERIFY(!Ops::copyToPages(document, {}, { 1 }));
    QVERIFY(!Ops::setRectangle(document, square, QRectF(0, 0, Ops::MAXIMUM_COORDINATE * 10, 10)));
}

/// "More colors…" of a color row: the palette, its link and the dialog Color picker
class AppAnnotationEditMoreColorsHelper
{
public:
    AppAnnotationEditMoreColorsHelper() = delete;

    /// Opens the palette of the row, activates More colors… and returns the dialog
    static pdf::PDFColorPickerDialog* open(pdf::PDFColorSwatchRow* row)
    {
        if (!row || !row->showPalette())
        {
            return nullptr;
        }
        pdftest::AppDocumentFixture::settle();
        pdf::PDFColorPalette* palette = row->getPalette();
        if (!palette || !palette->getMoreColorsLink())
        {
            return nullptr;
        }
        palette->getMoreColorsLink()->click();
        pdftest::AppDocumentFixture::settle();
        pdf::PDFAppColorPicker* picker = pdf::PDFAppColorPicker::find(row);
        return picker ? picker->getDialog() : nullptr;
    }

    /// Types the hexadecimal color into the dialog and takes it by OK
    static void choose(pdf::PDFColorPickerDialog* dialog, const QString& hex)
    {
        QTest::keyClick(dialog->getHexField(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(dialog->getHexField(), hex);
        dialog->getOkButton()->click();
        pdftest::AppDocumentFixture::settle();
    }
};

void AppAnnotationEditTest::moreColorsDialog()
{
    using H = AppAnnotationEditTestHelper;
    using MoreColors = AppAnnotationEditMoreColorsHelper;
    using Dialog = pdf::PDFColorPickerDialog;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openAnnotations(fixture);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();
    pdf::PDFAppAnnotationBar* bar = H::handler(tab)->getAnnotationBar();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    fixture.settle();

    // The annotation bar: the color of the selection as one undo step, the
    // colors tried one after another merged as the swatches (O20)
    pdf::PDFColorSwatchRow* row = bar->getSwatchRow();
    QVERIFY(pdf::PDFAppColorPicker::find(row));
    QPointer<Dialog> dialog = MoreColors::open(row);
    QVERIFY(dialog);
    QCOMPARE(dialog->getCurrentColor().rgb(), H::properties(document, square).color.rgb());
    QCOMPARE(dialog->getScrimTarget(), fixture.window->getDialogHost());
    MoreColors::choose(dialog, QStringLiteral("#123456"));
    QVERIFY(!dialog);
    QCOMPARE(H::properties(document, square).color.rgb(), qRgb(0x12, 0x34, 0x56));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QObject::tr("Change color"));
    QCOMPARE(row->getColor().rgb(), qRgb(0x12, 0x34, 0x56));
    dialog = MoreColors::open(row);
    QVERIFY(dialog);
    MoreColors::choose(dialog, QStringLiteral("#224466"));
    QCOMPARE(H::properties(document, square).color.rgb(), qRgb(0x22, 0x44, 0x66));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // Cancel writes nothing
    dialog = MoreColors::open(row);
    QVERIFY(dialog);
    QTest::keyClick(dialog->getHexField(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(dialog->getHexField(), QStringLiteral("#FFFFFF"));
    dialog->getCancelButton()->click();
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(H::properties(document, square).color.rgb(), qRgb(0x22, 0x44, 0x66));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // One undo returns the color before the dialog
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QVERIFY(H::properties(document, square).color.rgb() != qRgb(0x22, 0x44, 0x66));

    // The properties dialog: More colors… of its Color row opens the picker over it;
    // the dialog takes the color at Apply as one step
    pdf::PDFAppAnnotationPropertiesDialog* properties = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    fixture.settle();
    QVERIFY(properties);
    pdf::PDFColorSwatchRow* colorRow = properties->findChild<pdf::PDFColorSwatchRow*>(QStringLiteral("propertiesColor"));
    QVERIFY(colorRow && pdf::PDFAppColorPicker::find(colorRow));
    dialog = MoreColors::open(colorRow);
    QVERIFY(dialog);
    QCOMPARE(dialog->parentWidget(), static_cast<QWidget*>(properties));
    QCOMPARE(dialog->getScrimTarget(), fixture.window->getDialogHost());
    MoreColors::choose(dialog, QStringLiteral("#0A84FF"));
    QVERIFY(!dialog);
    QVERIFY(properties->isVisible());
    QCOMPARE(colorRow->getColor().rgb(), qRgb(0x0A, 0x84, 0xFF));
    QVERIFY(properties->getChangedFields().testFlag(pdf::PDFAnnotationProperties::Color));
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
    QVERIFY(properties->apply());
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);
    QCOMPARE(H::properties(document, square).color.rgb(), qRgb(0x0A, 0x84, 0xFF));

    // The properties dialog closed while the picker is open: the picker goes without a color
    QPointer<pdf::PDFAppAnnotationPropertiesDialog> propertiesGuard = pdf::PDFAppAnnotationPropertiesDialog::open(controller, square, fixture.window);
    fixture.settle();
    QVERIFY(propertiesGuard);
    dialog = MoreColors::open(propertiesGuard->findChild<pdf::PDFColorSwatchRow*>(QStringLiteral("propertiesColor")));
    QVERIFY(dialog);
    delete propertiesGuard.data();
    fixture.settle();
    QTRY_VERIFY(!dialog);
    QCOMPARE(H::properties(document, square).color.rgb(), qRgb(0x0A, 0x84, 0xFF));

    // The tab deleted with the picker of the bar open
    controller->selectAnnotations({ square });
    fixture.settle();
    dialog = MoreColors::open(bar->getSwatchRow());
    QVERIFY(dialog);
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    delete tab;
    fixture.settle();
    QVERIFY(!tabGuard);
    QTRY_VERIFY(!dialog);
}

QTEST_MAIN(AppAnnotationEditTest)
#include "tst_appannotationedittest.moc"
