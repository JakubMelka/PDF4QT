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

#include "pdfappannotatecommands.h"
#include "pdfappannotationbar.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationcreator.h"
#include "pdfappannotationeditcommands.h"
#include "pdfappautorefresh.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentpanels_bookmarks.h"
#include "pdfappdocumentpanels_notes.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappformcontroller.h"
#include "pdfappmarkresults.h"
#include "pdfappmarkupbar.h"
#include "pdfappoutlineeditor.h"
#include "pdfappsearchwidgets.h"
#include "pdfapptoolundotarget.h"
#include "pdfappundostack.h"
#include "pdfappviewstate.h"

#include "pdfannotation.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdfform.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"

#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSaveFile>
#include <QSignalSpy>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <functional>
#include <map>
#include <memory>

/// A target of Undo and Redo of the test: counts the calls, the states are set by the test
class FakeUndoTarget : public pdf::PDFAppToolUndoTarget
{
public:
    using PDFAppToolUndoTarget::PDFAppToolUndoTarget;

    virtual bool canUndo() const override { return isUndoPossible; }
    virtual bool canRedo() const override { return isRedoPossible; }

    virtual void undo() override
    {
        ++undoCalls;
        if (onUndo)
        {
            onUndo();
        }
    }

    virtual void redo() override { ++redoCalls; }

    void setStates(bool canUndoNow, bool canRedoNow)
    {
        isUndoPossible = canUndoNow;
        isRedoPossible = canRedoNow;
        Q_EMIT undoStateChanged();
    }

    bool isUndoPossible = true;
    bool isRedoPossible = true;
    int undoCalls = 0;
    int redoCalls = 0;
    std::function<void()> onUndo;
};

/// A part of a tool of the test: a plain area filled by a token of the theme (a widget
/// of the test draws no color of the leak palette)
class ToolTestPart : public QWidget
{
public:
    explicit ToolTestPart(const QString& name, pdf::PDFThemeColors::Color color, QWidget* parent) :
        QWidget(parent),
        m_color(color)
    {
        setObjectName(name);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), pdf::PDFThemeManager::getInstance()->getColor(m_color));
    }

private:
    pdf::PDFThemeColors::Color m_color;
};

/// All parts of a tool of the test, the widget "owner" is their parent before the mode
class ToolTestParts
{
public:
    enum Part
    {
        Toolbar = 1,
        LeftPanel = 2,
        CanvasPage = 4,
        RightPanel = 8,
        Target = 16,
        All = 31
    };

    ToolTestParts() :
        owner(std::make_unique<QWidget>())
    {
        using C = pdf::PDFThemeColors;
        toolbar = new ToolTestPart(QStringLiteral("toolToolbar"), C::BgSubtle, owner.get());
        leftPanel = new ToolTestPart(QStringLiteral("toolLeft"), C::BgSidebar, owner.get());
        canvasPage = new ToolTestPart(QStringLiteral("toolCanvas"), C::BgTrack, owner.get());
        rightPanel = new ToolTestPart(QStringLiteral("toolRight"), C::BgSidebar, owner.get());
        target = new FakeUndoTarget(owner.get());
    }

    pdf::PDFAppToolModeParts get(int parts = All) const
    {
        pdf::PDFAppToolModeParts result;
        result.toolbar = parts & Toolbar ? toolbar.data() : nullptr;
        result.leftPanel = parts & LeftPanel ? leftPanel.data() : nullptr;
        result.canvasPage = parts & CanvasPage ? canvasPage.data() : nullptr;
        result.rightPanel = parts & RightPanel ? rightPanel.data() : nullptr;
        result.undoTarget = parts & Target ? target.data() : nullptr;
        return result;
    }

    std::unique_ptr<QWidget> owner;
    QPointer<ToolTestPart> toolbar;
    QPointer<ToolTestPart> leftPanel;
    QPointer<ToolTestPart> canvasPage;
    QPointer<ToolTestPart> rightPanel;
    QPointer<FakeUndoTarget> target;
};

class AppToolViewTestHelper
{
public:
    AppToolViewTestHelper() = delete;

    /// Opens the file and waits until the document is shown
    static pdf::PDFAppDocumentTab* open(pdftest::AppDocumentFixture& fixture, const QString& path)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(path);
        if (!tab)
        {
            return nullptr;
        }
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        (void) QTest::qWaitFor([&guard]() { return !guard || guard->getDocument()->isReady(); }, 30000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        if (!guard || !guard->getDocument()->isReady())
        {
            return nullptr;
        }
        fixture.settle();
        return tab;
    }

    /// Rectangle of the widget in the coordinates of its window
    static QRect rectIn(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    /// Visible handles of the splitter
    static std::vector<QSplitterHandle*> visibleHandles(const QSplitter* splitter)
    {
        std::vector<QSplitterHandle*> handles;
        for (int i = 0; i < splitter->count(); ++i)
        {
            if (QSplitterHandle* handle = splitter->handle(i); handle && handle->isVisible())
            {
                handles.push_back(handle);
            }
        }
        return handles;
    }

    /// Reference of the annotation of the page with the subject of AppDocumentSamples::annotations
    static pdf::PDFObjectReference find(const pdf::PDFAppDocument* document, int pageIndex, const QString& subject)
    {
        const pdf::PDFDocumentPointer pdfDocument = document->getDocument();
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

    /// Rectangle of the annotation on its page
    static QRectF annotationRectangle(const pdf::PDFAppDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFDocumentPointer pdfDocument = document->getDocument();
        const pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&pdfDocument->getStorage(), reference);
        return annotation ? annotation->getRectangle() : QRectF();
    }

    /// Commands, which change the document or act on the annotations (the owner's rule of
    /// 2026-11-01); annotationSelectAll is reading
    static QStringList getBlockedCommands()
    {
        using C = pdf::PDFAppCommands;
        using T = pdf::PDFAppDocumentCommandTable;

        QStringList commands = T::getCommands(pdf::PDFAppDocumentPackage::Annotate);
        commands << T::getCommands(pdf::PDFAppDocumentPackage::AnnotationEdit);
        commands << T::getCommands(pdf::PDFAppDocumentPackage::NotesOutline);
        commands << C::rotatePagesLeft() << C::rotatePagesRight();
        commands.removeAll(C::annotationSelectAll());
        return commands;
    }

    static std::map<QString, bool> readCommandStates(const pdf::PDFAppDocumentView* view, const QStringList& commands)
    {
        std::map<QString, bool> states;
        for (const QString& command : commands)
        {
            states[command] = view->isCommandEnabled(command);
        }
        return states;
    }

    /// A change of the document as one undo step (Rotate of the thumbnails)
    static bool rotate(pdf::PDFAppDocumentView* view)
    {
        return view->executeCommand(pdf::PDFAppCommands::rotatePagesRight());
    }

    static pdf::PDFWidgetFormManager* formManager(pdf::PDFAppDocumentTab* tab)
    {
        return tab->getView()->getFormManager();
    }

    /// Value of the text field of the shown version
    static QString fieldValue(pdf::PDFAppDocumentTab* tab, const QString& name)
    {
        pdf::PDFWidgetFormManager* manager = formManager(tab);
        QString value = QStringLiteral("<none>");
        manager->getForm()->apply([&](const pdf::PDFFormField* field)
        {
            if (field->getName(pdf::PDFFormField::Partial) == name)
            {
                value = pdf::PDFDocumentDataLoaderDecorator(manager->getDocument()).readTextString(field->getValue(), QString());
            }
        });
        return value;
    }

    /// Center of the first widget of the form field on its page
    static QPointF fieldCenter(pdf::PDFAppDocumentTab* tab, const QString& name)
    {
        pdf::PDFWidgetFormManager* manager = formManager(tab);
        QPointF center;
        manager->getForm()->apply([&](const pdf::PDFFormField* field)
        {
            if (field->getName(pdf::PDFFormField::Partial) == name)
            {
                // A radio group has its widgets in its kids
                pdf::PDFFormWidgets widgets = field->getWidgets();
                for (const pdf::PDFFormFieldPointer& child : field->getChildFields())
                {
                    if (widgets.empty())
                    {
                        widgets = child->getWidgets();
                    }
                }
                if (!widgets.empty())
                {
                    center = manager->getWidgetRectangle(widgets.front()).center();
                }
            }
        });
        return center;
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        int count = 0;
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak && qAlpha(line[x]) > 0 ? 1 : 0;
            }
        }
        return count;
    }
};

/// The tool mode of the document view (phase 2, first batch, plan 2.3, F4 and the answer Undo)
class AppToolViewTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void frameGeometry_data();
    void frameGeometry();
    void partsGivenBack();
    void enteredAndLeftTwice();
    void undoRedoGoToTarget_data();
    void undoRedoGoToTarget();
    void undoStatesFollowTarget();
    void undoWithLockAndOperation();
    void documentCommandsBlocked_data();
    void documentCommandsBlocked();
    void creationToolEnded();
    void formEditorCommitted();
    void formReadOnlyDuringMode();
    void engineChangesNothing();
    void menusAndDialogsDoNotOpen();
    void outlineEditsFinished();
    void outlineCreationCanceled();
    void notesMenuOff();
    void readingStillWorks();
    void commandsOfReplacedParts();
    void lockDuringMode();
    void assistantCommitStaysOpen();
    void fullScreenGivenBack();
    void toolStatusItems();
    void toolMessage();
    void pageLabelsItem();
    void toolbarFactories();
    void partDeleted_data();
    void partDeleted();
    void tabOrWindowDeleted_data();
    void tabOrWindowDeleted();
    void documentDeleted();
    void documentReplaced();
    void deleteFromOwnSignals_data();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void languageChange();
    void refusedWithoutDocument();
    void userChangesRefusedByTheModel();
    void markResultsOfWorkerRefused();
    void viewStateNotOfTheMode();
    void panelRequestsBelowLeftPanel();
    void documentLeavesReady();
    void viewDeletedFlagCleared();
    void mergeSessionEndedByEntry();
    void reentrantLeaveAndEnter();
    void partsDeletedDuringEntry();
    void keyboardFocus();
    void barsOverCanvasPage();
    void automaticRefreshDuringMode();
    void otherPathsOfTheUser();
    void screenshots();
};

void AppToolViewTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppToolViewTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QThreadPool::globalInstance()->waitForDone(30000);
}

void AppToolViewTest::frameGeometry_data()
{
    QTest::addColumn<int>("partsMask");
    QTest::newRow("compress-sidebar-and-canvas") << int(ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target);
    QTest::newRow("left-panel-of-tool") << int(ToolTestParts::Toolbar | ToolTestParts::LeftPanel | ToolTestParts::RightPanel | ToolTestParts::Target);
    QTest::newRow("canvas-page-of-tool") << int(ToolTestParts::All);
}

void AppToolViewTest::frameGeometry()
{
    using H = AppToolViewTestHelper;
    QFETCH(int, partsMask);

    // document-canvas 291:1061 is bg/track up to its edge: no white of the window beside the
    // scroll bar of the PDFWidget (the slider 291:1063 lies directly on the canvas)
    const auto checkCanvasEdge = [](QWidget* window, QWidget* canvasArea)
    {
        const QImage image = window->grab().toImage();
        const QColor track = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgTrack);
        const QRect area = H::rectIn(canvasArea);
        for (int x = area.right() - 11; x <= area.right(); ++x)
        {
            const QColor color = image.pixelColor(x, area.bottom() - 40);
            if (color != track)
            {
                return false;
            }
        }
        return true;
    };

    // optimize-compress 291:994, 1440 x 960: top bar 38, toolbar 52, body 842, status bar 28
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    const int canvasWidthBefore = H::rectIn(view->getCanvasArea()).width();
    const QRect toolbarBefore = H::rectIn(view->getToolbar());

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(partsMask)));
    fixture.settle();

    // toolbar 291:1000
    QCOMPARE(H::rectIn(parts.toolbar), QRect(0, 38, 1440, 52));
    QVERIFY(!view->getToolbar()->isVisible());

    // sidebar-left 291:1030 300, splitter 291:1056 6 (one handle), canvas 291:1061 814
    const QWidget* left = partsMask & ToolTestParts::LeftPanel ? static_cast<QWidget*>(parts.leftPanel.data()) : static_cast<QWidget*>(view->getSidebar());
    QCOMPARE(H::rectIn(left), QRect(0, 90, 300, 842));
    QCOMPARE(view->getSidebar()->isVisible(), !(partsMask & ToolTestParts::LeftPanel));

    const std::vector<QSplitterHandle*> handles = H::visibleHandles(view->getSplitter());
    QCOMPARE(handles.size(), size_t(1));
    QCOMPARE(H::rectIn(handles.front()), QRect(300, 90, 6, 842));
    QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(306, 90, 814, 842));

    if (partsMask & ToolTestParts::CanvasPage)
    {
        QCOMPARE(H::rectIn(parts.canvasPage), QRect(306, 90, 814, 842));
        QVERIFY(!view->getPdfWidget()->isVisible());
    }
    else
    {
        QCOMPARE(H::rectIn(view->getPdfWidget()), QRect(306, 90, 814, 842));
        QVERIFY(checkCanvasEdge(fixture.window.data(), view->getCanvasArea()));
    }

    // sidebar-right 291:1177 320, the right panel is not a part of the splitter
    QCOMPARE(H::rectIn(parts.rightPanel), QRect(1120, 90, 320, 842));
    QCOMPARE(view->getSplitter()->indexOf(parts.rightPanel), -1);

    // status-bar 291:1064
    QCOMPARE(H::rectIn(view->getStatusBar()), QRect(0, 932, 1440, 28));

    // Everything returns
    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(H::rectIn(view->getToolbar()), toolbarBefore);
    QVERIFY(view->getToolbar()->isVisible());
    QVERIFY(view->getPdfWidget()->isVisible());
    QVERIFY(view->getSidebar()->isVisible());
    QCOMPARE(H::rectIn(view->getSidebar()).width(), 300);
    QCOMPARE(H::rectIn(view->getCanvasArea()).width(), canvasWidthBefore);
    QCOMPARE(view->getSplitter()->count(), 2);
}

void AppToolViewTest::partsGivenBack()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(view->isAncestorOf(parts.toolbar));
    QVERIFY(view->isAncestorOf(parts.leftPanel));
    QVERIFY(view->isAncestorOf(parts.canvasPage));
    QVERIFY(view->isAncestorOf(parts.rightPanel));

    view->leaveToolMode();
    fixture.settle();

    // The parts belong to their owner again, the shell decides about them
    for (const QPointer<ToolTestPart>& part : { parts.toolbar, parts.leftPanel, parts.canvasPage, parts.rightPanel })
    {
        QVERIFY(part);
        QCOMPARE(part->parentWidget(), parts.owner.get());
        QVERIFY(part->isHidden());
    }
    QCOMPARE(parts.rightPanel->minimumWidth(), 0);
    QCOMPARE(parts.rightPanel->maximumWidth(), QWIDGETSIZE_MAX);
    QCOMPARE(view->getToolUndoTarget(), nullptr);

    // The signals of the parts do not reach the view any more
    QSignalSpy undoSpy(view, &pdf::PDFAppDocumentView::toolUndoStateChanged);
    parts.target->setStates(false, false);
    QCOMPARE(undoSpy.count(), 0);

    // A deleted part after the mode does nothing to the view
    delete parts.leftPanel.data();
    fixture.settle();
    QVERIFY(view->getSidebar()->isVisible());
    QVERIFY(!view->isToolModeActive());
}

void AppToolViewTest::enteredAndLeftTwice()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QSignalSpy modeSpy(view, &pdf::PDFAppDocumentView::toolModeChanged);

    QVERIFY(!view->isToolModeActive());
    ToolTestParts first;
    ToolTestParts second;
    QVERIFY(view->enterToolMode(first.get()));
    QVERIFY(view->isToolModeActive());
    QVERIFY(!tab->getDocument()->isEditable());
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.last().at(0).toBool(), true);

    // The second entry is refused, the first parts stay
    QVERIFY(!view->enterToolMode(second.get()));
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(second.toolbar->parentWidget(), second.owner.get());
    QCOMPARE(view->getToolUndoTarget(), first.target.data());

    view->leaveToolMode();
    QVERIFY(!view->isToolModeActive());
    QVERIFY(tab->getDocument()->isEditable());
    QCOMPARE(modeSpy.count(), 2);
    QCOMPARE(modeSpy.last().at(0).toBool(), false);

    view->leaveToolMode();
    QCOMPARE(modeSpy.count(), 2);

    // The mode can be entered again, also with the first parts
    QVERIFY(view->enterToolMode(first.get()));
    fixture.settle();
    QCOMPARE(AppToolViewTestHelper::rectIn(first.rightPanel).width(), pdf::PDFAppDocumentView::TOOL_RIGHT_PANEL_WIDTH);
    view->leaveToolMode();
}

void AppToolViewTest::undoRedoGoToTarget_data()
{
    QTest::addColumn<int>("way");
    QTest::newRow("window-action") << 0;
    QTest::newRow("keys-on-canvas") << 1;
    QTest::newRow("button-of-document-toolbar") << 2;
    QTest::newRow("command-of-the-tool-toolbar") << 3;
}

void AppToolViewTest::undoRedoGoToTarget()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;
    QFETCH(int, way);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QAction* undoAction = fixture.window->getCommandAction(C::undo());
    QAction* redoAction = fixture.window->getCommandAction(C::redo());
    QVERIFY(undoAction && redoAction);

    // The owner's keys
    QVERIFY(undoAction->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Z)));
    QVERIFY(redoAction->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Y)));

    // A step and a step to redo of the document: nothing of them may be reachable
    QVERIFY(H::rotate(view));
    QVERIFY(H::rotate(view));
    QVERIFY(view->executeCommand(C::undo()));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(document->getUndoStack()->canRedo());
    const quint64 version = document->getVersion();
    const pdf::PDFDocumentPointer shownDocument = document->getDocument();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(way == 2 ? ToolTestParts::Target | ToolTestParts::RightPanel : ToolTestParts::All)));
    fixture.settle();

    switch (way)
    {
        case 0:
            undoAction->trigger();
            redoAction->trigger();
            break;

        case 1:
        {
            fixture.window->activateWindow();
            (void) QTest::qWaitForWindowActive(fixture.window.data());

            // The canvas of a mode with the PDFWidget: a part of the tool holds the focus otherwise
            parts.canvasPage->setFocusPolicy(Qt::StrongFocus);
            parts.canvasPage->setFocus(Qt::OtherFocusReason);
            QTest::keyClick(parts.canvasPage, Qt::Key_Z, Qt::ControlModifier);
            QTest::keyClick(parts.canvasPage, Qt::Key_Y, Qt::ControlModifier);
            break;
        }

        case 2:
        {
            QToolButton* undoButton = view->getToolbar()->getCommandButton(C::undo());
            QToolButton* redoButton = view->getToolbar()->getCommandButton(C::redo());
            QVERIFY(undoButton->isVisible() && undoButton->isEnabled());
            QVERIFY(redoButton->isVisible() && redoButton->isEnabled());
            QTest::mouseClick(undoButton, Qt::LeftButton);
            QTest::mouseClick(redoButton, Qt::LeftButton);
            break;
        }

        default:
            // The buttons of a toolbar of a tool run the commands of the view
            view->requestCommand(C::undo());
            view->requestCommand(C::redo());
            break;
    }
    fixture.settle();

    QCOMPARE(parts.target->undoCalls, 1);
    QCOMPARE(parts.target->redoCalls, 1);

    // The document and its undo stack are untouched
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(document->getDocument().data(), shownDocument.data());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(document->getUndoStack()->canRedo());

    // After leaving, Undo and Redo are of the document again
    view->leaveToolMode();
    fixture.settle();
    QVERIFY(undoAction->isEnabled());
    undoAction->trigger();
    fixture.settle();
    QCOMPARE(parts.target->undoCalls, 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QVERIFY(document->getVersion() != version);
}

void AppToolViewTest::undoStatesFollowTarget()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QAction* undoAction = fixture.window->getCommandAction(C::undo());
    QAction* redoAction = fixture.window->getCommandAction(C::redo());

    // The document can undo: the state of the document does not shine through
    QVERIFY(H::rotate(view));
    fixture.settle();
    QVERIFY(undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());

    ToolTestParts parts;
    parts.target->isUndoPossible = false;
    parts.target->isRedoPossible = true;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(redoAction->isEnabled());
    QVERIFY(!view->isCommandEnabled(C::undo()));
    QVERIFY(view->isCommandEnabled(C::redo()));
    QVERIFY(!view->getToolbar()->getCommandButton(C::undo())->isEnabled());
    QVERIFY(view->getToolbar()->getCommandButton(C::redo())->isEnabled());

    // A disabled command reaches nobody
    QVERIFY(!view->executeCommand(C::undo()));
    QCOMPARE(parts.target->undoCalls, 0);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 1);

    // The target reports its change
    QSignalSpy statesSpy(view, &pdf::PDFAppDocumentView::commandStatesChanged);
    parts.target->setStates(true, false);
    QVERIFY(statesSpy.count() > 0);
    QVERIFY(undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());
    QVERIFY(view->getToolbar()->getCommandButton(C::undo())->isEnabled());
    QVERIFY(!view->getToolbar()->getCommandButton(C::redo())->isEnabled());

    // After leaving the states are of the document
    parts.target->isUndoPossible = false;
    view->leaveToolMode();
    fixture.settle();
    QVERIFY(undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());
}

void AppToolViewTest::undoWithLockAndOperation()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(H::rotate(view));
    fixture.settle();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));

    // A running tool: 295:1711 has Undo disabled (K7)
    pdf::PDFAppDocumentOperation* operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Tool, QStringLiteral("Tool"), true);
    fixture.settle();
    QVERIFY(!view->isCommandEnabled(C::undo()));
    QVERIFY(!view->isCommandEnabled(C::redo()));
    delete operation;
    fixture.settle();
    QVERIFY(view->isCommandEnabled(C::undo()));

    // The parameters of a tool of a locked document change the document in no way:
    // Undo of the tool works, Undo of the document after the mode does not
    document->setLocked(true);
    fixture.settle();
    QVERIFY(view->isCommandEnabled(C::undo()));
    QVERIFY(view->executeCommand(C::undo()));
    QCOMPARE(parts.target->undoCalls, 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    view->leaveToolMode();
    fixture.settle();
    QVERIFY(document->isLocked());
    QVERIFY(!view->isCommandEnabled(C::undo()));
}

void AppToolViewTest::documentCommandsBlocked_data()
{
    QTest::addColumn<bool>("isLocked");
    QTest::newRow("unlocked") << false;
    QTest::newRow("locked") << true;
}

void AppToolViewTest::documentCommandsBlocked()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;
    QFETCH(bool, isLocked);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    controller->selectAnnotations({ H::find(document, 0, QStringLiteral("square")) });
    document->setLocked(isLocked);
    fixture.settle();
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(1));

    const QStringList commands = H::getBlockedCommands();
    const std::map<QString, bool> before = H::readCommandStates(view, commands);
    if (isLocked)
    {
        // The controller of the locked document allows only reading
        QVERIFY(!before.at(C::insertRectangle()));
        QVERIFY(!before.at(C::deleteSelection()));
    }
    else
    {
        // The control: these commands are on without a tool
        for (const QString& command : { C::annotatePanel(), C::insertPanel(), C::insertRectangle(), C::annotateHighlight(), C::deleteSelection(), C::annotationGeometry(), C::rotatePagesRight() })
        {
            QVERIFY2(before.at(command), qPrintable(command));
        }
    }
    QVERIFY(before.at(C::annotationProperties()));

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();

    // During the mode every one of them is off - the commands, the actions of the window,
    // the buttons of the toolbar
    for (const QString& command : commands)
    {
        QVERIFY2(!view->isCommandEnabled(command), qPrintable(command));
        if (const QAction* action = fixture.window->getCommandAction(command))
        {
            QVERIFY2(!action->isEnabled(), qPrintable(command));
        }
        if (const QAction* action = view->getCanvasAction(command))
        {
            QVERIFY2(!view->executeCommand(command), qPrintable(command));
            Q_UNUSED(action);
        }
    }
    QVERIFY(!view->getToolbar()->getAnnotateButton()->isEnabled());
    QVERIFY(!view->getToolbar()->getInsertButton()->isEnabled());

    // The engine and the controllers follow; the reason names the tool for an unlocked document
    QVERIFY(view->getAnnotationManager()->isReadOnly());
    QVERIFY(view->getFormManager()->isReadOnlyForm());
    QVERIFY(!controller->isAnnotationEditingAllowed());
    QVERIFY(!controller->isFormFillingAllowed());
    QVERIFY(!view->getOutlineEditor()->isEditingAllowed());
    QCOMPARE(controller->getEditingRefusalReason(), isLocked ? QStringLiteral("The document is locked. Unlock it to make changes.") : pdf::PDFAppDocument::getUserChangesSuspendedText());
    QCOMPARE(view->getOutlineEditor()->getEditingRefusalReason(), isLocked ? QStringLiteral("The document is locked. Unlock it to make changes.") : pdf::PDFAppDocument::getUserChangesSuspendedText());
    QCOMPARE(document->isLocked(), isLocked);

    // Selecting is reading
    QVERIFY(view->isCommandEnabled(C::annotationSelectAll()));

    view->leaveToolMode();
    fixture.settle();

    // Everything returns: the same state of each command, the same engine, the same lock
    QCOMPARE(H::readCommandStates(view, commands), before);
    QCOMPARE(view->getAnnotationManager()->isReadOnly(), isLocked);
    QCOMPARE(view->getFormManager()->isReadOnlyForm(), isLocked);
    QCOMPARE(controller->isAnnotationEditingAllowed(), !isLocked);
    QCOMPARE(document->isLocked(), isLocked);
    QCOMPARE(document->isEditable(), !isLocked);
    QCOMPARE(view->getToolbar()->getAnnotateButton()->isEnabled(), !isLocked);
    QCOMPARE(controller->getSelectedAnnotations().size(), size_t(1));
}

void AppToolViewTest::creationToolEnded()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFAppAnnotationCreator* creator = pdf::PDFAppAnnotateCommandHandler::findCreator(view);
    QVERIFY(creator);

    QVERIFY(view->executeCommand(C::insertRectangle()));
    fixture.settle();
    QVERIFY(controller->isCreationTool(view->getActiveTool()));
    QCOMPARE(creator->getActiveKind(), pdf::PDFAppAnnotationToolKind::Rectangle);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();

    // F4: the tool ends and its bar with it, nothing can start another one
    QVERIFY(!controller->isCreationTool(view->getActiveTool()));
    QCOMPARE(creator->getActiveKind(), pdf::PDFAppAnnotationToolKind::None);
    QVERIFY(!view->executeCommand(C::insertRectangle()));
    QVERIFY(!controller->isCreationTool(view->getActiveTool()));

    // A drag on the page creates nothing
    const int steps = pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, QPointF(50, 50), QPointF(120, 120)));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), steps);

    // The tool stays ended after leaving (a choice of the user made before the tool),
    // the command starts it again
    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(creator->getActiveKind(), pdf::PDFAppAnnotationToolKind::None);
    QVERIFY(view->executeCommand(C::insertRectangle()));
    fixture.settle();
    QCOMPARE(creator->getActiveKind(), pdf::PDFAppAnnotationToolKind::Rectangle);
}

void AppToolViewTest::formEditorCommitted()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);

    // An open editor with a typed value, which is not committed yet
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("name"))));
    fixture.settle();
    QVERIFY(H::formManager(tab)->getFocusedWidget().isValid());
    QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(canvas, QStringLiteral("Typed"));
    fixture.settle();
    QCOMPARE(H::fieldValue(tab, QStringLiteral("name")), QStringLiteral("John"));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();

    // F4: the value is one undo step of the document before the mode, no editor is left
    QCOMPARE(H::fieldValue(tab, QStringLiteral("name")), QStringLiteral("Typed"));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Fill form field"));
    QVERIFY(!H::formManager(tab)->getFocusedWidget().isValid());
    QVERIFY(!document->hasPendingEdits());

    view->leaveToolMode();
    fixture.settle();

    // The form can be filled again
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("name"))));
    fixture.settle();
    QVERIFY(H::formManager(tab)->getFocusedWidget().isValid());
}

void AppToolViewTest::formReadOnlyDuringMode()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    const quint64 version = document->getVersion();

    // A click on a text field opens no editor, keys write nothing, a check box stays
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("name"))));
    fixture.settle();
    QVERIFY(!H::formManager(tab)->getFocusedWidget().isValid());
    QTest::keyClicks(canvas, QStringLiteral("x"));
    QTest::keyClick(canvas, Qt::Key_Tab);
    QVERIFY(!H::formManager(tab)->getFocusedWidget().isValid());
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("agree"))));
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("reset"))));
    fixture.settle();
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);

    view->leaveToolMode();
    fixture.settle();

    // The control: the same click changes the check box out of the mode
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("agree"))));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
}

void AppToolViewTest::engineChangesNothing()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    QVERIFY(square.isValid());
    controller->selectAnnotations({ square });
    fixture.settle();
    const QRectF rectangle = H::annotationRectangle(document, square);
    QVERIFY(rectangle.isValid());

    pdf::PDFWidgetAnnotationManager* manager = view->getAnnotationManager();

    // The data of a drag of the annotations are made after a press on the selection
    bool isPressPointValid = false;
    const QPoint pressPoint = pdftest::AppDocumentFixture::pageToWidget(view, 0, rectangle.center(), &isPressPointValid);
    QVERIFY(isPressPointValid);
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, pressPoint);
    std::unique_ptr<QMimeData> dragData(manager->createAnnotationDragData());
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, pressPoint);
    QVERIFY(dragData);
    fixture.settle();
    controller->selectAnnotations({ square });
    fixture.settle();

    const QPointF handle(rectangle.right(), rectangle.top());
    const QPointF handleTarget = handle + QPointF(30, -10);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    const quint64 version = document->getVersion();

    // The handle of the selection frame, the drop of a drag and Delete change nothing
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, handle, handleTarget));
    fixture.settle();
    QCOMPARE(document->getVersion(), version);

    bool ok = false;
    const QPoint dropPoint = pdftest::AppDocumentFixture::pageToWidget(view, 0, rectangle.center() + QPointF(40, -30), &ok);
    QVERIFY(ok);
    QVERIFY(!manager->handleAnnotationDrop(dragData.get(), dropPoint, Qt::MoveAction, false));
    fixture.settle();
    QCOMPARE(document->getVersion(), version);

    QTest::keyClick(pdftest::AppDocumentFixture::canvasWidget(view), Qt::Key_Delete);
    fixture.settle();
    QCOMPARE(document->getVersion(), version);

    // An application change through the controller is refused with the reason in the status bar
    QSignalSpy refusedSpy(controller, &pdf::PDFAppAnnotationController::changeRefused);
    QVERIFY(!controller->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity()), pdf::PDFAppAnnotationChangeKind::Annotation));
    QCOMPARE(refusedSpy.count(), 1);
    QCOMPARE(refusedSpy.first().at(0).toString(), pdf::PDFAppDocument::getUserChangesSuspendedText());
    QCOMPARE(view->getStatusBar()->getMessage(), pdf::PDFAppDocument::getUserChangesSuspendedText());
    QCOMPARE(document->getVersion(), version);

    // The control: the same drag of the handle changes the annotation out of the mode
    view->leaveToolMode();
    fixture.settle();
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(pdftest::AppDocumentFixture::dragPage(view, 0, handle, handleTarget));
    fixture.settle();
    QVERIFY(document->getVersion() != version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(H::annotationRectangle(document, square) != rectangle);
}

void AppToolViewTest::menusAndDialogsDoNotOpen()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFAppAnnotationEditCommandHandler* handler = qobject_cast<pdf::PDFAppAnnotationEditCommandHandler*>(tab->findHandler(pdf::PDFAppCommands::annotationProperties()));
    QVERIFY(handler);
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    const QRectF rectangle = H::annotationRectangle(document, square);

    // The bar of the selection and a thread of comments are open before the mode
    controller->selectAnnotations({ square });
    fixture.settle();
    QVERIFY(handler->getAnnotationBar()->isVisible());
    pdf::PDFAppCommentPopup* popup = view->getCommentPopup();
    popup->showThread(square, false);
    fixture.settle();
    QVERIFY(popup->isVisible());

    QSignalSpy menuSpy(controller, &pdf::PDFAppAnnotationController::menuRequested);
    QSignalSpy propertiesSpy(controller, &pdf::PDFAppAnnotationController::propertiesRequested);
    QSignalSpy geometrySpy(controller, &pdf::PDFAppAnnotationController::geometryRequested);
    QSignalSpy copySpy(controller, &pdf::PDFAppAnnotationController::copyToPagesRequested);
    QSignalSpy commentSpy(controller, &pdf::PDFAppAnnotationController::commentRequested);
    QSignalSpy replySpy(controller, &pdf::PDFAppAnnotationController::replyRequested);
    QSignalSpy popupSpy(controller, &pdf::PDFAppAnnotationController::popupRequested);

    // A request queued before the entry does not come through either
    controller->requestProperties(square);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    QVERIFY(!handler->getAnnotationBar()->isVisible());
    QVERIFY(!popup->isVisible());
    QVERIFY(controller->isUserInterfaceSuspended());
    QCOMPARE(propertiesSpy.count(), 0);

    // The canvas: the right button, a double click; the requests of the panels
    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, rectangle.center(), Qt::NoModifier, Qt::RightButton));
    bool ok = false;
    const QPoint point = pdftest::AppDocumentFixture::pageToWidget(view, 0, rectangle.center(), &ok);
    QVERIFY(ok);
    QTest::mouseDClick(pdftest::AppDocumentFixture::canvasWidget(view), Qt::LeftButton, Qt::NoModifier, point);
    controller->requestMenu(pdf::PDFAnnotationMenuRequest());
    controller->requestProperties(square);
    controller->requestGeometry(square);
    controller->requestCopyToPages();
    controller->requestComment(square, false);
    controller->requestReply(square);
    fixture.settle();
    QCOMPARE(menuSpy.count(), 0);
    QCOMPARE(popupSpy.count(), 0);
    QCOMPARE(propertiesSpy.count(), 0);
    QCOMPARE(geometrySpy.count(), 0);
    QCOMPARE(copySpy.count(), 0);
    QCOMPARE(commentSpy.count(), 0);
    QCOMPARE(replySpy.count(), 0);

    view->leaveToolMode();
    fixture.settle();

    // The bar of the kept selection comes back, the requests work again
    QVERIFY(handler->getAnnotationBar()->isVisible());
    controller->requestGeometry(square);
    fixture.settle();
    QCOMPARE(geometrySpy.count(), 1);
    fixture.closeMessageBoxes();
}

void AppToolViewTest::outlineEditsFinished()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(view->showPanel(pdf::PDFAppDocumentPanelIds::outline()));
    fixture.settle();
    pdf::PDFAppOutlinePanel* panel = qobject_cast<pdf::PDFAppOutlinePanel*>(view->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::outline()));
    QVERIFY(panel);
    panel->getTreeView()->selectionModel()->setCurrentIndex(panel->getItemIndex({ 0 }), QItemSelectionModel::ClearAndSelect);
    fixture.settle();
    QVERIFY(view->isCommandEnabled(C::outlineRename()));
    QWidget* newItemButton = view->getSidebar()->findChild<QWidget*>(QStringLiteral("outlineNew"));
    QVERIFY(newItemButton);
    QVERIFY(newItemButton->isVisibleTo(view->getSidebar()));

    // An inline rename with a typed title: F4 - written as one undo step before the mode
    QVERIFY(view->executeCommand(C::outlineRename()));
    fixture.settle();
    QLineEdit* edit = nullptr;
    for (QLineEdit* candidate : panel->getTreeView()->viewport()->findChildren<QLineEdit*>())
    {
        edit = candidate->isVisible() ? candidate : edit;
    }
    QVERIFY(edit);
    edit->setText(QStringLiteral("Renamed"));

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QCOMPARE(panel->getItemIndex({ 0 }).data().toString(), QStringLiteral("Renamed"));

    // The panel is for reading now
    QVERIFY(!newItemButton->isVisibleTo(view->getSidebar()));
    QVERIFY(!view->isCommandEnabled(C::outlineRename()));
    QVERIFY(!view->executeCommand(C::outlineDelete()));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // The keys of the panel: F2, Insert and Delete of the tree do nothing
    panel->getTreeView()->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(panel->getTreeView(), Qt::Key_F2);
    QTest::keyClick(panel->getTreeView(), Qt::Key_Insert);
    QTest::keyClick(panel->getTreeView(), Qt::Key_Delete);
    fixture.settle();
    QVERIFY(!view->getOutlineEditor()->isCreationPending());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    for (QLineEdit* candidate : panel->getTreeView()->viewport()->findChildren<QLineEdit*>())
    {
        QVERIFY(!candidate->isVisible());
    }

    view->leaveToolMode();
    fixture.settle();
    QVERIFY(newItemButton->isVisibleTo(view->getSidebar()));
    QVERIFY(view->isCommandEnabled(C::outlineRename()));
}

void AppToolViewTest::outlineCreationCanceled()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(view->showPanel(pdf::PDFAppDocumentPanelIds::outline()));
    fixture.settle();

    // A new item, which waits for its title: it has no title to write, it ends with nothing
    QVERIFY(view->executeCommand(C::outlineNewItem()));
    fixture.settle();
    QVERIFY(view->getOutlineEditor()->isCreationPending());

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    QVERIFY(!view->getOutlineEditor()->isCreationPending());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    view->leaveToolMode();
}

void AppToolViewTest::notesMenuOff()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(view->showPanel(pdf::PDFAppDocumentPanelIds::notes()));
    fixture.settle();
    pdf::PDFAppNotesPanel* panel = qobject_cast<pdf::PDFAppNotesPanel*>(view->getSidebar()->findPanel(pdf::PDFAppDocumentPanelIds::notes()));
    QVERIFY(panel);
    panel->selectNotes({ H::find(tab->getDocument(), 0, QStringLiteral("square")) });
    fixture.settle();

    // The control: out of the mode the row has its menu
    QMenu* menu = panel->createContextMenu();
    QVERIFY(menu);
    delete menu;

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    QCOMPARE(panel->createContextMenu(), nullptr);

    view->leaveToolMode();
    fixture.settle();
    menu = panel->createContextMenu();
    QVERIFY(menu);
    delete menu;
}

void AppToolViewTest::readingStillWorks()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 5));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    const quint64 version = document->getVersion();

    // Zoom
    const double zoom = view->getZoom();
    QVERIFY(view->executeCommand(C::zoomIn()));
    fixture.settle();
    QVERIFY(view->getZoom() > zoom);

    // Navigation
    QVERIFY(view->executeCommand(C::layoutSinglePage()));
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(0));
    QVERIFY(view->executeCommand(C::nextPage()));
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(1));
    QVERIFY(view->executeCommand(C::lastPage()));
    fixture.settle();
    QCOMPARE(view->getCurrentPageIndex(), pdf::PDFInteger(4));
    QVERIFY(view->executeCommand(C::firstPage()));
    fixture.settle();

    // The sidebar of a tool, which keeps it (F4 still toggles it)
    QVERIFY(view->isSidebarVisible());
    QVERIFY(view->executeCommand(C::toggleSidebar()));
    fixture.settle();
    QVERIFY(!view->getSidebar()->isVisible());
    QCOMPARE(AppToolViewTestHelper::rectIn(view->getCanvasArea()).width(), 1120);
    QVERIFY(view->executeCommand(C::toggleSidebar()));
    fixture.settle();
    QVERIFY(view->getSidebar()->isVisible());
    QCOMPARE(AppToolViewTestHelper::rectIn(view->getCanvasArea()).width(), 814);

    // Search: the panel of the sidebar (the field of the toolbar is hidden), F3
    QVERIFY(view->isCommandEnabled(C::find()));
    QVERIFY(view->executeCommand(C::find()));
    fixture.settle();
    QCOMPARE(view->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::search());

    // Selection and Copy
    QVERIFY(view->executeCommand(C::selectAll()));
    QTRY_VERIFY(view->hasTextSelection());
    QApplication::clipboard()->clear();
    QVERIFY(view->isCommandEnabled(C::copy()));
    QVERIFY(view->executeCommand(C::copy()));
    QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("alpha")));

    // Annotations can be selected
    QVERIFY(view->isCommandEnabled(C::annotationSelectAll()));
    QVERIFY(view->isCommandEnabled(C::handTool()));
    QVERIFY(view->isCommandEnabled(C::properties()));

    // Nothing of this has changed the document
    QCOMPARE(document->getVersion(), version);
    view->leaveToolMode();
}

void AppToolViewTest::commandsOfReplacedParts()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    QVERIFY(view->isCommandEnabled(C::goToPage()));
    QVERIFY(view->isCommandEnabled(C::find()));
    QVERIFY(view->isCommandEnabled(C::readAloud()));
    QVERIFY(view->isCommandEnabled(C::fullScreen()));

    // A mode with the parts of the view keeps what lives in them
    ToolTestParts rightOnly;
    QVERIFY(view->enterToolMode(rightOnly.get(ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    QVERIFY(view->isCommandEnabled(C::goToPage()));
    QVERIFY(view->isCommandEnabled(C::find()));
    QVERIFY(view->isCommandEnabled(C::readAloud()));
    QVERIFY(!view->isCommandEnabled(C::fullScreen()));
    view->leaveToolMode();

    // The toolbar and the sidebar replaced: Go to and Find have no place
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(view->isToolToolbarShown());
    QVERIFY(view->isToolLeftPanelShown());
    QVERIFY(!view->isCommandEnabled(C::goToPage()));
    QVERIFY(!view->isCommandEnabled(C::find()));
    QVERIFY(!view->isCommandEnabled(C::readAloud()));
    QVERIFY(!view->isCommandEnabled(C::fullScreen()));

    view->leaveToolMode();
    fixture.settle();
    QVERIFY(!view->isToolToolbarShown());
    QVERIFY(!view->isToolLeftPanelShown());
    QVERIFY(view->isCommandEnabled(C::goToPage()));
    QVERIFY(view->isCommandEnabled(C::find()));
    QVERIFY(view->isCommandEnabled(C::readAloud()));
    QVERIFY(view->isCommandEnabled(C::fullScreen()));
}

void AppToolViewTest::lockDuringMode()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QSignalSpy lockSpy(view, &pdf::PDFAppDocumentView::toolModeLockChanged);

    // Out of the mode the view reports nothing
    document->setLocked(true);
    document->setLocked(false);
    QCOMPARE(lockSpy.count(), 0);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));

    // Plan 2.3: the lock is never blocked by the mode, the shell gets the signal
    QVERIFY(view->isCommandEnabled(C::lockDocument()));
    QVERIFY(view->executeCommand(C::lockDocument()));
    QVERIFY(document->isLocked());
    QCOMPARE(lockSpy.count(), 1);
    QCOMPARE(lockSpy.last().at(0).toBool(), true);
    QVERIFY(view->isToolModeActive());

    // commit of a locked document stays refused (the result of a tool)
    const pdf::PDFModifiedDocument modified(document->getDocument(), document->getOptionalContentActivity());
    QVERIFY(!document->commit(modified, QStringLiteral("x"), document->getVersion(), pdf::PDFAppChangeSource::User));

    // An unlock during the mode keeps the user changes suspended
    document->setLocked(false);
    QCOMPARE(lockSpy.count(), 2);
    QCOMPARE(lockSpy.last().at(0).toBool(), false);
    QVERIFY(!document->isEditable());
    QVERIFY(!view->getAnnotationController()->isAnnotationEditingAllowed());

    view->leaveToolMode();
    QVERIFY(document->isEditable());
    QVERIFY(view->getAnnotationController()->isAnnotationEditingAllowed());
}

void AppToolViewTest::assistantCommitStaysOpen()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    const quint64 version = document->getVersion();

    // The plan 2.4: a change of an assistant goes through commit during the mode
    const pdf::PDFModifiedDocument modified(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation);
    QVERIFY(document->commit(modified, QStringLiteral("Assistant"), version, pdf::PDFAppChangeSource::Mcp));
    QCOMPARE(document->getVersion(), version + 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    fixture.settle();

    // The mode stays read only, Undo stays of the tool
    QVERIFY(view->isToolModeActive());
    QVERIFY(view->getAnnotationManager()->isReadOnly());
    QVERIFY(view->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(parts.target->undoCalls, 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    view->leaveToolMode();
}

void AppToolViewTest::fullScreenGivenBack()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    view->setFullScreen(true);
    fixture.settle();
    QVERIFY(view->isFullScreen());
    QVERIFY(view->getToolbar()->isHidden());
    QVERIFY(!view->getStatusBar()->isVisible());

    // The parts of the tool need the place: the full screen ends
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(!view->isFullScreen());
    QVERIFY(parts.toolbar->isVisible());
    QVERIFY(view->getStatusBar()->isVisible());

    // A refused second entry does not forget it, nor does it touch the open mode
    ToolTestParts second;
    QVERIFY(!view->enterToolMode(second.get()));

    // ... and it returns with the end of the mode
    view->leaveToolMode();
    fixture.settle();
    QVERIFY(view->isFullScreen());
    QVERIFY(view->getToolbar()->isHidden());

    view->setFullScreen(false);
    fixture.settle();
    QVERIFY(view->getToolbar()->isVisible());
    QCOMPARE(view->getToolbar()->height(), pdf::PDFAppDocumentToolbar::HEIGHT);
}

void AppToolViewTest::toolStatusItems()
{
    using H = AppToolViewTestHelper;

    // status-bar 291:1064: "Page 308 / 1592" 123 wide, "151 %" 66 wide, 22 high, 12 apart
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 1592));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();

    // A document without page labels has no such items out of the mode
    QVERIFY(!statusBar->getPageItem()->isVisible());
    QVERIFY(!statusBar->getZoomItem()->isVisible());

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target)));
    view->goToPage(307);
    view->setZoom(1.51);
    fixture.settle();

    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 308 / 1592"));
    QCOMPARE(statusBar->getZoomText(), QStringLiteral("151 %"));
    QVERIFY(statusBar->getPageItem()->isVisible());
    QVERIFY(statusBar->getZoomItem()->isVisible());
    QCOMPARE(statusBar->getPageItem()->height(), 22);
    QCOMPARE(statusBar->getZoomItem()->height(), 22);

    // 8 + icon 14 + 6 + the text + 8: the advance of the text is rounded by Figma and by Qt
    QVERIFY2(qAbs(statusBar->getPageItem()->width() - 123) <= 1, qPrintable(QString::number(statusBar->getPageItem()->width())));
    QVERIFY2(qAbs(statusBar->getZoomItem()->width() - 66) <= 1, qPrintable(QString::number(statusBar->getZoomItem()->width())));
    QCOMPARE(statusBar->getZoomItem()->x() - statusBar->getPageItem()->geometry().right() - 1, 12);

    // The trailing items of the frame: status-page 1085, status-zoom 1220, status-issues 1298
    // 134 wide, 8 px from the edge of the window
    QWidget* issuesItem = new QWidget();
    issuesItem->setFixedSize(134, 22);
    view->addStatusItem(issuesItem, pdf::PDFAppDocumentStatusBar::Area::Trailing);
    fixture.settle();
    QCOMPARE(H::rectIn(issuesItem).x(), 1298);
    QVERIFY(qAbs(H::rectIn(issuesItem).y() - 935.5) <= 0.5);
    QVERIFY2(qAbs(H::rectIn(statusBar->getZoomItem()).x() - 1220) <= 1, qPrintable(QString::number(H::rectIn(statusBar->getZoomItem()).x())));
    QVERIFY2(qAbs(H::rectIn(statusBar->getPageItem()).x() - 1085) <= 2, qPrintable(QString::number(H::rectIn(statusBar->getPageItem()).x())));
    delete issuesItem;
    fixture.settle();

    // The zoom item follows the zoom, the page item the page
    view->setZoom(0.5);
    fixture.settle();
    QCOMPARE(statusBar->getZoomText(), QStringLiteral("50 %"));
    view->goToPage(0);
    fixture.settle();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 1592"));

    // Go to opens below the toolbar of the document: the item of the tool only shows its text
    QVERIFY(statusBar->getPageItem()->testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(statusBar->getPageItem()->focusPolicy(), Qt::NoFocus);
    QVERIFY(statusBar->getPageItem()->toolTip().isEmpty());

    view->leaveToolMode();
    fixture.settle();
    QVERIFY(!statusBar->getPageItem()->isVisible());
    QVERIFY(!statusBar->getZoomItem()->isVisible());
    QVERIFY(!statusBar->getPageItem()->testAttribute(Qt::WA_TransparentForMouseEvents));
}

void AppToolViewTest::toolMessage()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Ready"));

    // Out of the mode the text is ignored
    view->setToolMessage(QStringLiteral("Early"));
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Ready"));
    QVERIFY(view->getToolMessage().isEmpty());

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));

    // 291:1065; a name of a file is shown as it is
    const QString message = QStringLiteral("Compress · <b>Project</b>_Report.pdf · 24.8 MB");
    view->setToolMessage(message);
    QCOMPARE(statusBar->getMessage(), message);
    QCOMPARE(statusBar->findChild<QLabel*>(QStringLiteral("statusMessage"))->textFormat(), Qt::PlainText);

    // A new version of the document keeps the message
    const pdf::PDFModifiedDocument modified(tab->getDocument()->getDocument(), tab->getDocument()->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation);
    QVERIFY(tab->getDocument()->commit(modified, QStringLiteral("x"), tab->getDocument()->getVersion(), pdf::PDFAppChangeSource::Mcp));
    fixture.settle();
    QCOMPARE(statusBar->getMessage(), message);

    // A hostile length is bounded
    view->setToolMessage(QString(10000, QLatin1Char('x')));
    QCOMPARE(statusBar->getMessage().size(), 512);

    view->leaveToolMode();
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Ready"));
    QVERIFY(view->getToolMessage().isEmpty());
}

void AppToolViewTest::pageLabelsItem()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::pageLabels(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();

    // With page labels the item keeps its text and works as before
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 (i) of 10"));
    QVERIFY(!statusBar->getPageItem()->toolTip().isEmpty());

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 (i) of 10"));

    // The toolbar of the document stays: Go to still opens
    QVERIFY(!statusBar->getPageItem()->testAttribute(Qt::WA_TransparentForMouseEvents));
    QVERIFY(!statusBar->getPageItem()->toolTip().isEmpty());
    view->leaveToolMode();
}

void AppToolViewTest::toolbarFactories()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;
    using T = pdf::PDFAppDocumentToolbar;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2));
    QVERIFY(tab);
    const QToolButton* documentUndo = tab->getView()->getToolbar()->getCommandButton(C::undo());
    const QToolButton* documentMenu = tab->getView()->getToolbar()->getMenuButton(T::Panel::File);

    // tb-undo 291:1006: 36 x 36 as the button of the toolbar of the document
    QWidget parent;
    QToolButton* button = T::createToolbarButton(QStringLiteral("toolUndo"), QStringLiteral("undo"), &parent);
    QCOMPARE(button->parentWidget(), &parent);
    QCOMPARE(button->objectName(), QStringLiteral("toolUndo"));
    QCOMPARE(button->sizeHint(), QSize(36, 36));
    QCOMPARE(button->sizeHint(), documentUndo->sizeHint());
    QCOMPARE(button->iconSize(), documentUndo->iconSize());
    QCOMPARE(button->autoRaise(), documentUndo->autoRaise());
    QCOMPARE(button->focusPolicy(), documentUndo->focusPolicy());
    QVERIFY(!button->icon().isNull());

    // tb-tutorial 291:1160: the same as the menu button of the toolbar of the document
    QToolButton* menuButton = T::createToolbarMenuButton(QStringLiteral("toolTutorial"), QStringLiteral("file"), &parent);
    menuButton->setText(documentMenu->text());
    QCOMPARE(menuButton->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(menuButton->iconSize(), QSize(18, 18));
    QVERIFY(menuButton->isCheckable());
    QCOMPARE(menuButton->sizeHint().height(), 36);
    QCOMPARE(menuButton->sizeHint(), documentMenu->sizeHint());
}

void AppToolViewTest::partDeleted_data()
{
    QTest::addColumn<int>("part");
    QTest::newRow("toolbar") << int(ToolTestParts::Toolbar);
    QTest::newRow("left-panel") << int(ToolTestParts::LeftPanel);
    QTest::newRow("canvas-page") << int(ToolTestParts::CanvasPage);
    QTest::newRow("right-panel") << int(ToolTestParts::RightPanel);
    QTest::newRow("target") << int(ToolTestParts::Target);
}

void AppToolViewTest::partDeleted()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;
    QFETCH(int, part);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(H::rotate(view));
    fixture.settle();
    const int steps = pdftest::AppDocumentFixture::undoCount(document);

    // The sidebar hidden by the user comes back hidden with a deleted left panel
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(view->getSidebar()->isHidden());

    switch (part)
    {
        case ToolTestParts::Toolbar:
            delete parts.toolbar.data();
            break;
        case ToolTestParts::LeftPanel:
            delete parts.leftPanel.data();
            break;
        case ToolTestParts::CanvasPage:
            delete parts.canvasPage.data();
            break;
        case ToolTestParts::RightPanel:
            delete parts.rightPanel.data();
            break;
        default:
            delete parts.target.data();
            break;
    }
    fixture.settle();

    // The mode goes on with the part of the view in the place of the deleted part
    QVERIFY(view->isToolModeActive());
    QVERIFY(!tab->getDocument()->isEditable());
    switch (part)
    {
        case ToolTestParts::Toolbar:
            QVERIFY(view->getToolbar()->isVisible());
            QCOMPARE(H::rectIn(view->getToolbar()), QRect(0, 38, 1440, 52));
            QVERIFY(!view->isToolToolbarShown());
            QVERIFY(view->isCommandEnabled(C::goToPage()));
            QVERIFY(!view->getStatusBar()->getPageItem()->testAttribute(Qt::WA_TransparentForMouseEvents));
            break;

        case ToolTestParts::LeftPanel:
            QVERIFY(view->getSidebar()->isVisible());
            QCOMPARE(H::rectIn(view->getSidebar()), QRect(0, 90, 300, 842));
            QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(306, 90, 814, 842));
            QCOMPARE(view->getSplitter()->count(), 2);
            QVERIFY(!view->isToolLeftPanelShown());
            break;

        case ToolTestParts::CanvasPage:
            QVERIFY(view->getPdfWidget()->isVisible());
            QCOMPARE(H::rectIn(view->getPdfWidget()), QRect(306, 90, 814, 842));
            break;

        case ToolTestParts::RightPanel:
            QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(306, 90, 1134, 842));
            break;

        default:
            // No target: Undo and Redo do nothing - never the document
            QCOMPARE(view->getToolUndoTarget(), nullptr);
            QVERIFY(!view->isCommandEnabled(C::undo()));
            QVERIFY(!view->isCommandEnabled(C::redo()));
            QVERIFY(!fixture.window->getCommandAction(C::undo())->isEnabled());
            QVERIFY(!view->executeCommand(C::undo()));
            QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), steps);
            break;
    }

    // ... and it ends normally
    view->leaveToolMode();
    fixture.settle();
    QVERIFY(tab->getDocument()->isEditable());
    QVERIFY(view->getSidebar()->isVisible());
    QVERIFY(view->getToolbar()->isVisible());
    QVERIFY(view->getPdfWidget()->isVisible());
    QVERIFY(view->isCommandEnabled(C::undo()));
    QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(306, 90, 1134, 842));
}

void AppToolViewTest::tabOrWindowDeleted_data()
{
    QTest::addColumn<bool>("deleteWindow");
    QTest::newRow("tab") << false;
    QTest::newRow("window") << true;
}

void AppToolViewTest::tabOrWindowDeleted()
{
    using H = AppToolViewTestHelper;
    QFETCH(bool, deleteWindow);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentView> view = tab->getView();
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(view->isAncestorOf(parts.rightPanel));

    if (deleteWindow)
    {
        delete fixture.window.data();
    }
    else
    {
        delete tab;
    }
    fixture.settle();

    // The parts were children of the view; the target of the shell stays and reports to nobody
    QVERIFY(!view);
    QVERIFY(!document);
    QVERIFY(!parts.toolbar);
    QVERIFY(!parts.leftPanel);
    QVERIFY(!parts.canvasPage);
    QVERIFY(!parts.rightPanel);
    QVERIFY(parts.target);
    parts.target->setStates(false, false);
    QCOMPARE(parts.target->undoCalls, 0);
}

void AppToolViewTest::documentDeleted()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QSignalSpy modeSpy(view, &pdf::PDFAppDocumentView::toolModeChanged);

    delete tab->getDocument();
    fixture.settle();

    // The mode ends with its document, the parts are given back
    QVERIFY(!view->isToolModeActive());
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.first().at(0).toBool(), false);
    QCOMPARE(parts.rightPanel->parentWidget(), parts.owner.get());
    QCOMPARE(view->getToolUndoTarget(), nullptr);
}

void AppToolViewTest::documentReplaced()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(document->areUserChangesSuspended());

    // The mode belongs to the shown document: the document left behind can be changed again
    view->setDocument(nullptr);
    fixture.settle();
    QVERIFY(!view->isToolModeActive());
    QVERIFY(!document->areUserChangesSuspended());
    QVERIFY(document->isEditable());
    QCOMPARE(parts.toolbar->parentWidget(), parts.owner.get());

    view->setDocument(document);
    fixture.settle();
    QVERIFY(view->enterToolMode(parts.get()));
    view->leaveToolMode();
}

void AppToolViewTest::deleteFromOwnSignals_data()
{
    QTest::addColumn<QString>("site");
    QTest::addColumn<bool>("deleteWindow");

    for (const char* site : { "enter-mode", "enter-states", "enter-suspended", "leave-mode", "leave-states", "leave-suspended", "lock", "undo-state", "undo-call", "deleted-parts" })
    {
        QTest::newRow(QByteArray(site).append("-tab").constData()) << QString::fromLatin1(site) << false;
        QTest::newRow(QByteArray(site).append("-window").constData()) << QString::fromLatin1(site) << true;
    }
}

void AppToolViewTest::deleteFromOwnSignals()
{
    using H = AppToolViewTestHelper;
    QFETCH(QString, site);
    QFETCH(bool, deleteWindow);

    pdftest::AppDocumentFixture fixture;
    QPointer<pdf::PDFAppDocumentTab> tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentView> view = tab->getView();
    QPointer<pdf::PDFAppDocument> document = tab->getDocument();
    QPointer<pdf::PDFAppMainWindow> window = fixture.window.data();

    bool isDeleted = false;
    const std::function<void()> victim = [&]()
    {
        if (isDeleted)
        {
            return;
        }
        isDeleted = true;
        if (deleteWindow)
        {
            delete window.data();
        }
        else
        {
            delete tab.data();
        }
    };

    ToolTestParts parts;
    QObject context;
    const std::function<void()> connectSite = [&]()
    {
        if (site.endsWith(QLatin1String("-mode")))
        {
            QObject::connect(view.data(), &pdf::PDFAppDocumentView::toolModeChanged, &context, victim);
        }
        else if (site.endsWith(QLatin1String("-states")))
        {
            QObject::connect(view.data(), &pdf::PDFAppDocumentView::commandStatesChanged, &context, victim);
        }
        else if (site.endsWith(QLatin1String("-suspended")))
        {
            QObject::connect(document.data(), &pdf::PDFAppDocument::userChangesSuspendedChanged, &context, victim);
        }
        else if (site == QLatin1String("lock"))
        {
            QObject::connect(view.data(), &pdf::PDFAppDocumentView::toolModeLockChanged, &context, victim);
        }
        else if (site == QLatin1String("undo-state"))
        {
            QObject::connect(view.data(), &pdf::PDFAppDocumentView::toolUndoStateChanged, &context, victim);
        }
        else if (site == QLatin1String("deleted-parts"))
        {
            QObject::connect(view.data(), &pdf::PDFAppDocumentView::toolModeChanged, &context, victim);
        }
    };

    if (site.startsWith(QLatin1String("enter")))
    {
        connectSite();
        QVERIFY(!view->enterToolMode(parts.get()));
        QVERIFY(isDeleted);
    }
    else
    {
        QVERIFY(view->enterToolMode(parts.get()));
        fixture.settle();
        connectSite();

        if (site.startsWith(QLatin1String("leave")))
        {
            view->leaveToolMode();
        }
        else if (site == QLatin1String("lock"))
        {
            document->setLocked(true);
        }
        else if (site == QLatin1String("undo-state"))
        {
            parts.target->setStates(true, false);
        }
        else if (site == QLatin1String("undo-call"))
        {
            parts.target->onUndo = victim;
            view->executeCommand(pdf::PDFAppCommands::undo());
        }
        else
        {
            // The queued fallback after the deleted parts: the mode ends by itself
            delete parts.toolbar.data();
            delete parts.leftPanel.data();
            delete parts.canvasPage.data();
            delete parts.rightPanel.data();
            delete parts.target.data();
            fixture.settle();
        }
    }

    fixture.settle();
    QVERIFY(isDeleted);
    QVERIFY(!view);
    QVERIFY(!document);
    QCOMPARE(window.isNull(), deleteWindow);
}

void AppToolViewTest::themesAndScales()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target)));
    view->setToolMessage(QStringLiteral("Compress · a.pdf"));
    fixture.settle();
    QVERIFY(view->getStatusBar()->getZoomItem()->isVisible());

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();
        const QColor sidebar = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSidebar);
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QWidget* window = fixture.window.data();
            QImage image(QSize(qCeil(window->width() * ratio), qCeil(window->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(ratio);
            image.fill(Qt::transparent);
            window->render(&image);
            QCOMPARE(H::countLeakPixels(image), 0);

            // The right panel with the token of its part lies where the frame has it
            const QPoint rightCenter = parts.rightPanel->mapTo(window, parts.rightPanel->rect().center());
            QCOMPARE(image.pixelColor(qRound(rightCenter.x() * ratio), qRound(rightCenter.y() * ratio)), sidebar);
        }
    }
    view->leaveToolMode();
}

void AppToolViewTest::rightToLeft()
{
    using H = AppToolViewTestHelper;

    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::LeftPanel | ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();

    // The frame mirrored as the layout of the sidebar and the canvas of the view. QSplitter
    // places its widgets by moveRight(width - left) (QSplitterPrivate::setGeo, the right edge
    // of a QRect is inclusive), so the widgets inside of the splitter of Qt lie 1 px to the
    // right of their place, the handle does not - also out of the mode, without a tool. The
    // quirk of Qt is measured, not required: 0 is the mirrored frame.
    const int splitterShift = H::rectIn(view->getCanvasArea()).left() - 320;
    QVERIFY2(splitterShift == 0 || splitterShift == 1, qPrintable(QString::number(splitterShift)));
    QCOMPARE(H::rectIn(parts.rightPanel), QRect(0, 90, 320, 842));
    QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(320 + splitterShift, 90, 814, 842));
    QCOMPARE(H::rectIn(parts.leftPanel).left(), 1140 + splitterShift);
    QCOMPARE(parts.leftPanel->width(), 300);
    const std::vector<QSplitterHandle*> handles = H::visibleHandles(view->getSplitter());
    QCOMPARE(handles.size(), size_t(1));
    QCOMPARE(H::rectIn(handles.front()), QRect(1134, 90, 6, 842));
    QCOMPARE(H::rectIn(parts.toolbar), QRect(0, 38, 1440, 52));

    // The items of the status bar in the order of the reading: the zoom left of the page
    QVERIFY(view->getStatusBar()->getZoomItem()->x() < view->getStatusBar()->getPageItem()->x());
    // The whole window: no leak color; the column left by the quirk has the color of the handle
    // (bg/sidebar) next to the right panel, in both themes - no line of the white of the window
    QCOMPARE(H::countLeakPixels(fixture.window->grab().toImage()), 0);
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();
        const QImage image = fixture.window->grab().toImage();
        const QColor sidebar = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSidebar);
        QCOMPARE(H::countLeakPixels(image), 0);
        if (splitterShift == 1)
        {
            QCOMPARE(image.pixelColor(320, 400), sidebar);
        }
    }

    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(H::rectIn(view->getSidebar()).left(), 1140 + splitterShift);
    QCOMPARE(view->getSidebar()->width(), 300);
    QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(splitterShift, 90, 1134, 842));
}

void AppToolViewTest::minimalWindow()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    const QSize windowSize = fixture.window->size();
    QCOMPARE(windowSize, QSize(720, 480));
    const int sidebarBefore = view->getSidebar()->width();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::LeftPanel | ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();

    // The window keeps its size, the right panel its 320, the left panel gives its width up
    QCOMPARE(fixture.window->size(), windowSize);
    QCOMPARE(parts.rightPanel->width(), 320);
    QCOMPARE(H::rectIn(parts.rightPanel).right(), 719);
    QCOMPARE(parts.leftPanel->width(), 200);
    QCOMPARE(H::rectIn(parts.leftPanel).left(), 0);
    QCOMPARE(H::rectIn(view->getCanvasArea()), QRect(206, 90, 194, 480 - 90 - 28));
    QCOMPARE(H::rectIn(view->getStatusBar()), QRect(0, 452, 720, 28));
    QCOMPARE(H::rectIn(parts.toolbar).width(), 720);

    // A larger window gives the left panel its 300 back, a smaller one takes it again
    fixture.window->resize(1100, 700);
    fixture.settle();
    QCOMPARE(parts.leftPanel->width(), 300);
    QCOMPARE(parts.rightPanel->width(), 320);
    QCOMPARE(H::rectIn(view->getCanvasArea()).width(), 1100 - 300 - 6 - 320);
    fixture.window->resize(720, 480);
    fixture.settle();
    QCOMPARE(parts.leftPanel->width(), 200);

    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(fixture.window->size(), windowSize);
    QCOMPARE(view->getSidebar()->width(), sidebarBefore);
}

void AppToolViewTest::languageChange()
{
    using H = AppToolViewTestHelper;

    class Translator : public QTranslator
    {
    public:
        virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
        {
            Q_UNUSED(disambiguation);
            Q_UNUSED(n);
            return QByteArray(context) == "pdf::PDFAppDocumentView" ? QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText)) : QString();
        }

        virtual bool isEmpty() const override { return false; }
    };

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target)));
    view->setToolMessage(QStringLiteral("Tool text"));
    fixture.settle();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 3"));

    Translator translator;
    QCoreApplication::installTranslator(&translator);
    fixture.settle();

    // The items of the view in the new language; the message belongs to the tool (its
    // shell sets it again); the mode and its parts stay
    QTRY_COMPARE(statusBar->getPageText(), QStringLiteral("[Page 1 / 3]"));
    QVERIFY(statusBar->getZoomText().startsWith(QLatin1Char('[')));
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Tool text"));
    QVERIFY(view->isToolModeActive());
    QVERIFY(parts.toolbar->isVisible());

    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    QTRY_COMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 3"));
    view->leaveToolMode();
    QCOMPARE(statusBar->getMessage(), QStringLiteral("Ready"));
}

void AppToolViewTest::refusedWithoutDocument()
{
    pdftest::AppDocumentFixture fixture;

    // A view without a document has no mode
    pdf::PDFAppDocumentView view(fixture.context.get());
    ToolTestParts parts;
    QVERIFY(!view.enterToolMode(parts.get()));
    QVERIFY(!view.isToolModeActive());
    QCOMPARE(parts.toolbar->parentWidget(), parts.owner.get());
    QCOMPARE(view.getToolUndoTarget(), nullptr);
    view.setToolMessage(QStringLiteral("x"));
    QVERIFY(view.getToolMessage().isEmpty());
    view.leaveToolMode();

    // A document, which is not shown yet (Loading)
    pdf::PDFAppDocument document(fixture.context.get(), nullptr);
    view.setDocument(&document);
    QVERIFY(!view.enterToolMode(parts.get()));
    QVERIFY(document.isEditable() == false);
    QVERIFY(!document.areUserChangesSuspended());
    view.setDocument(nullptr);
}

void AppToolViewTest::userChangesRefusedByTheModel()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();

    // A step to undo and a step to redo of the document
    QVERIFY(H::rotate(view));
    QVERIFY(H::rotate(view));
    QVERIFY(view->executeCommand(pdf::PDFAppCommands::undo()));
    fixture.settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    const quint64 version = document->getVersion();
    const pdf::PDFModifiedDocument modified(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation);

    // The owner's rule holds in the model, for a path, which does not read isEditable: a
    // change of the user is refused, the undo stack does nothing
    QVERIFY(!document->commit(modified, QStringLiteral("user"), version, pdf::PDFAppChangeSource::User));
    QVERIFY(!document->getUndoStack()->undo());
    QVERIFY(!document->getUndoStack()->redo());
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    QVERIFY(document->getUndoStack()->canRedo());

    // Plan 2.4: the accepted result of the tool is one undo step of the document
    QVERIFY(document->commit(modified, QStringLiteral("tool"), version, pdf::PDFAppChangeSource::Tool));
    QCOMPARE(document->getVersion(), version + 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("tool"));

    view->leaveToolMode();
    fixture.settle();

    // Everything of the user works again
    QVERIFY(document->getUndoStack()->undo());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);
    const pdf::PDFModifiedDocument again(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Annotation);
    QVERIFY(document->commit(again, QStringLiteral("user"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
}

void AppToolViewTest::markResultsOfWorkerRefused()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 2000));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFAppAnnotateCommandHandler* annotateHandler = pdf::PDFAppAnnotateCommandHandler::findHandler(view);
    pdf::PDFAppMarkResults* markResults = annotateHandler->getMarkResults();
    pdf::PDFAppDocumentSearchController* controller = markResults->getSearchController();
    controller->search(QStringLiteral("alpha"));
    QVERIFY(QTest::qWaitFor([controller]() { return controller->getState() == pdf::PDFAppDocumentSearchController::State::Finished; }, 60000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
    fixture.settle();
    QCOMPARE(markResults->getMatchCount(pdf::PDFAppMarkResults::Scope::All), 2000);

    // A worker of Mark as annotations started before the tool: it reads no isEditable, it
    // would commit through the open tool as soon as its operation ends
    QSignalSpy finished(markResults, &pdf::PDFAppMarkResults::finished);
    QVERIFY(markResults->mark(pdf::AnnotationType::Highlight, QColor(), pdf::PDFAppMarkResults::Scope::All));
    QVERIFY(markResults->isRunning());
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    const quint64 version = document->getVersion();

    QVERIFY(finished.wait(120000 * pdftest::AppDocumentFixture::getTimeoutFactor()));
    QCOMPARE(finished.at(0).at(0).toInt(), 0);
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 0);
    QCOMPARE(view->getStatusBar()->getMessage(), pdf::PDFAppDocument::getUserChangesSuspendedText());
    view->leaveToolMode();
}

void AppToolViewTest::viewStateNotOfTheMode()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3);
    pdf::PDFAppDocumentTab* tab = H::open(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The user has hidden the sidebar, the tool has its panel in that place (389:20204)
    view->setSidebarVisible(false);
    fixture.settle();
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(parts.leftPanel->isVisible());
    QCOMPARE(parts.leftPanel->width(), 300);
    QVERIFY(!view->getSidebar()->isVisible());
    QCOMPARE(view->getViewState().sidebarVisible, std::optional<bool>(false));

    // The button panel-left of the toolbar of the tool (389:20184) and F4 hide its panel
    QVERIFY(view->isCommandEnabled(C::toggleSidebar()));
    QVERIFY(view->executeCommand(C::toggleSidebar()));
    fixture.settle();
    QVERIFY(!parts.leftPanel->isVisible());
    QCOMPARE(H::rectIn(view->getCanvasArea()).width(), 1120);
    QCOMPARE(view->getViewState().sidebarVisible, std::optional<bool>(false));
    QVERIFY(view->executeCommand(C::toggleSidebar()));
    fixture.settle();
    QVERIFY(parts.leftPanel->isVisible());
    QCOMPARE(parts.leftPanel->width(), 300);

    // A state applied during the mode is the state of the sidebar of the user
    pdf::PDFAppViewState state;
    state.sidebarVisible = true;
    view->applyViewState(state);
    fixture.settle();
    QVERIFY(parts.leftPanel->isVisible());
    QVERIFY(!view->getSidebar()->isVisible());
    QCOMPARE(view->getViewState().sidebarVisible, std::optional<bool>(true));
    state.sidebarVisible = false;
    view->applyViewState(state);
    fixture.settle();
    QVERIFY(parts.leftPanel->isVisible());
    QCOMPARE(view->getViewState().sidebarVisible, std::optional<bool>(false));

    // The end of the mode gives the sidebar of the user back, hidden
    view->leaveToolMode();
    fixture.settle();
    QVERIFY(!view->isSidebarVisible());
    QVERIFY(!view->getSidebar()->isVisible());
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(parts.leftPanel->isVisible());

    // The tab closed during the mode writes the layout of the user into Recent
    delete tab;
    fixture.settle();
    const std::optional<pdf::PDFRecentFile> recent = fixture.context->getRecent().getFile(QFileInfo(file).canonicalFilePath());
    QVERIFY(recent.has_value());
    QCOMPARE(pdf::PDFAppViewState::fromText(recent->viewState).sidebarVisible, std::optional<bool>(false));
}

void AppToolViewTest::panelRequestsBelowLeftPanel()
{
    using H = AppToolViewTestHelper;
    using Ids = pdf::PDFAppDocumentPanelIds;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    const QString panelBefore = view->getCurrentPanelId();
    QVERIFY(panelBefore != Ids::notes());

    // The left panel of a tool hides the sidebar: a request (the signature item of the status
    // bar) has nothing to show and leaves the panel of the user
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QVERIFY(!view->showPanel(Ids::notes()));
    QCOMPARE(view->getCurrentPanelId(), panelBefore);
    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(view->getCurrentPanelId(), panelBefore);
    QVERIFY(view->getSidebar()->isVisible());

    // A tool, which keeps the sidebar (291:1030), still shows its panels
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    QVERIFY(view->showPanel(Ids::notes()));
    QCOMPARE(view->getCurrentPanelId(), Ids::notes());
    view->leaveToolMode();
}

void AppToolViewTest::documentLeavesReady()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3);
    pdf::PDFAppDocumentTab* tab = H::open(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QSignalSpy modeSpy(view, &pdf::PDFAppDocumentView::toolModeChanged);

    // F2: nothing of a tool stays over a document, which is not shown; no suspended flag is left
    // (checked at once: the window closes the tab of a failed document from the event loop)
    document->setFailed(file, QStringLiteral("Damaged"));
    QVERIFY(!view->isToolModeActive());
    QVERIFY(!document->areUserChangesSuspended());
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.first().at(0).toBool(), false);
    QCOMPARE(parts.toolbar->parentWidget(), parts.owner.get());
    QCOMPARE(view->getToolUndoTarget(), nullptr);
    fixture.settle();
    fixture.closeMessageBoxes();
}

void AppToolViewTest::viewDeletedFlagCleared()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // A view dying during the mode leaves no suspended document behind, whatever outlives it
    auto view = std::make_unique<pdf::PDFAppDocumentView>(fixture.context.get());
    view->setDocument(document);
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target | ToolTestParts::RightPanel)));
    QVERIFY(document->areUserChangesSuspended());
    view.reset();
    QVERIFY(!document->areUserChangesSuspended());
    QVERIFY(document->isEditable());
    QVERIFY(!parts.rightPanel);
    QVERIFY(parts.target);
}

void AppToolViewTest::mergeSessionEndedByEntry()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    pdf::PDFWidgetAnnotationManager* manager = view->getAnnotationManager();
    const QString key = QStringLiteral("color:1");
    controller->selectAnnotations({ H::find(document, 0, QStringLiteral("square")) });
    fixture.settle();

    // A live change of the annotation bar (O20): the changes of one session are one step
    const std::function<void(const QColor&)> liveChange = [&](const QColor& color)
    {
        controller->beginEngineChange(QStringLiteral("Change color"), key);
        manager->setSelectedAnnotationsColor(color);
        controller->endEngineChange();
    };
    liveChange(QColor(Qt::blue));
    liveChange(QColor(Qt::green));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 1);

    // A tool between two changes: the step is closed by the entry (F4), the next one is new
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    view->leaveToolMode();
    fixture.settle();
    liveChange(QColor(Qt::red));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), 2);
}

void AppToolViewTest::reentrantLeaveAndEnter()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QSignalSpy modeSpy(view, &pdf::PDFAppDocumentView::toolModeChanged);

    // A slot leaves the mode from the middle of the entry: the last announcement is "left"
    ToolTestParts first;
    {
        QObject context;
        QObject::connect(document, &pdf::PDFAppDocument::userChangesSuspendedChanged, &context, [view]() { view->leaveToolMode(); });
        QVERIFY(!view->enterToolMode(first.get()));
    }
    QVERIFY(!view->isToolModeActive());
    QVERIFY(!document->areUserChangesSuspended());
    QVERIFY(document->isEditable());
    QCOMPARE(modeSpy.last().at(0).toBool(), false);
    QCOMPARE(first.toolbar->parentWidget(), first.owner.get());

    // A slot enters the next tool from the middle of the leave (Then continue with): the
    // next tool is open, the last announcement is "entered"
    modeSpy.clear();
    QVERIFY(view->enterToolMode(first.get()));
    ToolTestParts second;
    {
        QObject context;
        QObject::connect(document, &pdf::PDFAppDocument::userChangesSuspendedChanged, &context, [view, document, &second]()
        {
            if (!document->areUserChangesSuspended())
            {
                view->enterToolMode(second.get());
            }
        });
        view->leaveToolMode();
    }
    QVERIFY(view->isToolModeActive());
    QVERIFY(document->areUserChangesSuspended());
    QVERIFY(!document->isEditable());
    QCOMPARE(modeSpy.last().at(0).toBool(), true);
    QCOMPARE(view->getToolUndoTarget(), second.target.data());
    QVERIFY(view->isAncestorOf(second.toolbar));
    QCOMPARE(first.toolbar->parentWidget(), first.owner.get());
    view->leaveToolMode();
    QVERIFY(document->isEditable());
}

void AppToolViewTest::partsDeletedDuringEntry()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);

    QVERIFY(pdftest::AppDocumentFixture::clickPage(view, 0, H::fieldCenter(tab, QStringLiteral("name"))));
    fixture.settle();
    QTest::keyClicks(canvas, QStringLiteral("Typed"));
    fixture.settle();

    // The commit of the pending edit runs a slot, which deletes parts of the entered tool
    ToolTestParts parts;
    QObject context;
    QObject::connect(document, &pdf::PDFAppDocument::documentChanged, &context, [&parts]()
    {
        delete parts.toolbar.data();
        delete parts.rightPanel.data();
        delete parts.target.data();
    });

    QVERIFY(view->enterToolMode(parts.get()));
    QObject::disconnect(document, nullptr, &context, nullptr);
    QVERIFY(!parts.toolbar);
    fixture.settle();

    // The mode runs with the parts, which are left
    QVERIFY(view->isToolModeActive());
    QVERIFY(!view->isToolToolbarShown());
    QVERIFY(view->getToolbar()->isVisible());
    QCOMPARE(view->getToolUndoTarget(), nullptr);
    QCOMPARE(H::rectIn(parts.leftPanel), QRect(0, 90, 300, 842));
    QCOMPARE(H::rectIn(parts.canvasPage), QRect(306, 90, 1134, 842));
    view->leaveToolMode();
    QVERIFY(document->isEditable());
}

void AppToolViewTest::keyboardFocus()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QWidget* window = fixture.window.data();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);

    // A field of the window outside of the view (as the tab bar or a part of the top bar)
    QLineEdit outsideField(window);
    outsideField.show();
    QLineEdit* headerField = &outsideField;

    // The keys of the mode are bound to the view: the focus comes into the canvas of the tool
    headerField->setFocus(Qt::OtherFocusReason);
    QCOMPARE(window->focusWidget(), headerField);
    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QCOMPARE(window->focusWidget(), parts.canvasPage.data());

    // The part with the focus goes: the focus stays in the view, on the page
    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(window->focusWidget(), canvas);

    // Without a page of the tool: the canvas of the document
    headerField->setFocus(Qt::OtherFocusReason);
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    QCOMPARE(window->focusWidget(), canvas);
    view->leaveToolMode();

    // A focus in a visible part of the view stays (the list of the sidebar)
    QWidget* sidebarChild = nullptr;
    for (QWidget* child : view->getSidebar()->findChildren<QWidget*>())
    {
        if (child->isVisible() && (child->focusPolicy() & Qt::TabFocus))
        {
            sidebarChild = child;
        }
    }
    QVERIFY(sidebarChild);
    sidebarChild->setFocus(Qt::OtherFocusReason);
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    QCOMPARE(window->focusWidget(), sidebarChild);
    view->leaveToolMode();
}

void AppToolViewTest::barsOverCanvasPage()
{
    using H = AppToolViewTestHelper;

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QWidget* canvasArea = view->getCanvasArea();

    // A bar over the canvas (the bar of the change of an assistant lives 30 s) stays above a
    // page of a tool, which comes after it
    QWidget floatingBar(canvasArea);
    floatingBar.setGeometry(10, 10, 100, 30);
    floatingBar.show();
    floatingBar.raise();

    // The bar of a text selection shows Copy while the text is seen (9.8) ...
    pdf::PDFAppAnnotationCreator* creator = pdf::PDFAppAnnotateCommandHandler::findCreator(view);
    view->selectAllText();
    QTRY_VERIFY(creator->hasTextSelection());
    fixture.settle();
    QTRY_VERIFY(creator->getMarkupBar() && creator->getMarkupBar()->isVisible());

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    QVERIFY(creator->getMarkupBar()->isVisible());
    QVERIFY(creator->getMarkupBar()->getButton(pdf::PDFAppMarkupBar::Button::Copy)->isVisible());
    QVERIFY(!creator->getMarkupBar()->getButton(pdf::PDFAppMarkupBar::Button::Underline)->isVisible());
    view->leaveToolMode();
    fixture.settle();
    QVERIFY(creator->getMarkupBar()->isVisible());
    QVERIFY(creator->getMarkupBar()->getButton(pdf::PDFAppMarkupBar::Button::Underline)->isVisible());

    // ... and it goes with the text, when a page of the tool covers the canvas
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    QCOMPARE(canvasArea->childAt(20, 20), &floatingBar);
    QVERIFY(!creator->getMarkupBar()->isVisible());

    view->leaveToolMode();
    fixture.settle();
    QCOMPARE(canvasArea->childAt(20, 20), &floatingBar);

    // The canvas has its place again: the selection stays, its bar comes with the next one
    // (a change of the layout hides it, tools.md 9.8)
    QVERIFY(creator->hasTextSelection());

    // A deleted page gives the place back with the bars above the canvas too
    QVERIFY(view->enterToolMode(parts.get()));
    fixture.settle();
    delete parts.canvasPage.data();
    fixture.settle();
    QCOMPARE(canvasArea->childAt(20, 20), &floatingBar);
    view->leaveToolMode();
    fixture.settle();

    // A locked document: the entry changes no permission, the mode itself hides the bar
    tab->getDocument()->setLocked(true);
    view->deselectText();
    fixture.settle();
    view->selectAllText();
    QTRY_VERIFY(creator->hasTextSelection());
    fixture.settle();
    QTRY_VERIFY(creator->getMarkupBar()->isVisible());
    ToolTestParts lockedParts;
    QVERIFY(view->enterToolMode(lockedParts.get()));
    fixture.settle();
    QVERIFY(!creator->getMarkupBar()->isVisible());
    view->leaveToolMode();
    fixture.settle();
    tab->getDocument()->setLocked(false);
    view->deselectText();
}

void AppToolViewTest::automaticRefreshDuringMode()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refresh.pdf"), 2);
    const QString other = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 5);
    pdf::PDFAppDocumentTab* tab = H::open(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAutoRefresh* refresh = pdf::PDFAppDocumentFileCommandHandler::find(tab)->getAutoRefresh();
    QVERIFY(refresh->isEnabled());

    QVERIFY(H::rotate(view));
    document->setSaved();
    fixture.settle();

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target)));
    fixture.settle();
    QCOMPARE(view->getStatusBar()->getPageText(), QStringLiteral("Page 1 / 2"));

    // Another program replaces the file while the tool is open (no operation runs): the
    // document is reloaded (a Reset change), the mode goes on over the new pages
    QFile otherFile(other);
    QVERIFY(otherFile.open(QIODevice::ReadOnly));
    const QByteArray newData = otherFile.readAll();
    QSignalSpy reloadedSpy(refresh, &pdf::PDFAppAutoRefresh::reloaded);
    bool isReplaced = false;
    for (int attempt = 0; attempt < 20 && !isReplaced; ++attempt)
    {
        QSaveFile target(file);
        isReplaced = target.open(QIODevice::WriteOnly) && target.write(newData) == newData.size() && target.commit();
        if (!isReplaced)
        {
            QTest::qWait(100);
        }
    }
    QVERIFY(isReplaced);
    if (!reloadedSpy.wait(5000))
    {
        refresh->checkNow();
    }
    QTRY_VERIFY_WITH_TIMEOUT(reloadedSpy.count() >= 1, 20000);
    fixture.settle();
    QCOMPARE(document->getPageCount(), pdf::PDFInteger(5));

    QVERIFY(view->isToolModeActive());
    QVERIFY(!document->isEditable());
    QVERIFY(view->getAnnotationManager()->isReadOnly());
    QVERIFY(view->getFormManager()->isReadOnlyForm());
    QVERIFY(!view->isCommandEnabled(C::rotatePagesRight()));
    QCOMPARE(view->getStatusBar()->getPageText(), QStringLiteral("Page 1 / 5"));
    QVERIFY(view->executeCommand(C::undo()));
    QCOMPARE(parts.target->undoCalls, 1);

    view->leaveToolMode();
    fixture.settle();
    QVERIFY(document->isEditable());
    QVERIFY(!view->getAnnotationManager()->isReadOnly());
    QVERIFY(view->isCommandEnabled(C::rotatePagesRight()));
    QVERIFY(!view->isCommandEnabled(C::undo()));
}

void AppToolViewTest::otherPathsOfTheUser()
{
    using H = AppToolViewTestHelper;
    using C = pdf::PDFAppCommands;

    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::annotations(fixture.folder());
    pdf::PDFAppDocumentTab* tab = H::open(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFAppAnnotationController* controller = view->getAnnotationController();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);

    // An unsaved change, a selected annotation and the annotation on the clipboard
    QVERIFY(H::rotate(view));
    fixture.settle();
    const pdf::PDFObjectReference square = H::find(document, 0, QStringLiteral("square"));
    controller->selectAnnotations({ square });
    controller->copySelection();
    fixture.settle();
    QVERIFY(controller->canPaste());
    const int steps = pdftest::AppDocumentFixture::undoCount(document);

    ToolTestParts parts;
    QVERIFY(view->enterToolMode(parts.get(ToolTestParts::Target)));
    fixture.settle();
    const quint64 version = document->getVersion();
    const QRectF rectangle = H::annotationRectangle(document, square);

    // The keys of the engine: paste, cut, delete, a nudge by the arrows
    canvas->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(canvas, Qt::Key_V, Qt::ControlModifier);
    QTest::keyClick(canvas, Qt::Key_X, Qt::ControlModifier);
    QTest::keyClick(canvas, Qt::Key_Delete);
    QTest::keyClick(canvas, Qt::Key_Left);
    QTest::keyClick(canvas, Qt::Key_Up, Qt::ShiftModifier);
    fixture.settle();
    QCOMPARE(document->getVersion(), version);
    QCOMPARE(H::annotationRectangle(document, square), rectangle);

    // Commands reached by the search of the window (a disabled one does not run, Undo goes
    // to the tool); the same keys and commands reach nothing of the document
    QVERIFY(!pdf::PDFAppSearchActions::runCommand(fixture.window.data(), C::insertRectangle()));
    QVERIFY(!pdf::PDFAppSearchActions::runCommand(fixture.window.data(), C::deleteSelection()));
    QVERIFY(!pdf::PDFAppSearchActions::runCommand(fixture.window.data(), C::rotatePagesRight()));
    QVERIFY(pdf::PDFAppSearchActions::runCommand(fixture.window.data(), C::undo()));
    fixture.settle();
    QCOMPARE(parts.target->undoCalls, 1);
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), steps);

    // The suspended state is no lock: nothing says locked, nothing is stored as locked
    QVERIFY(!document->isLocked());
    QVERIFY(!tab->isTabLocked());
    QVERIFY(!view->isCommandChecked(C::lockDocument()));
    const std::optional<pdf::PDFRecentFile> recent = fixture.context->getRecent().getFile(QFileInfo(file).canonicalFilePath());
    QVERIFY(recent.has_value());
    QVERIFY(!recent->locked);

    // Writing the shown document and printing it are reading (plan: only the changes stop)
    QVERIFY(view->isCommandEnabled(C::save()));
    QVERIFY(view->isCommandEnabled(C::saveAs()));
    QVERIFY(view->isCommandEnabled(C::print()));

    // A personal bookmark does not change the document
    const size_t bookmarks = pdf::PDFAppBookmarks::getItems(fixture.context.get(), document).size();
    QVERIFY(view->executeCommand(C::addBookmark()));
    fixture.settle();
    QCOMPARE(pdf::PDFAppBookmarks::getItems(fixture.context.get(), document).size(), bookmarks + 1);
    QCOMPARE(document->getVersion(), version);

    // Edit a Copy opens an editable document in its own tab, the tool stays open
    const int tabCount = fixture.window->getTabs().size();
    QVERIFY(view->executeCommand(C::editCopy()));
    fixture.settle();
    QCOMPARE(fixture.window->getTabs().size(), tabCount + 1);
    pdf::PDFAppDocumentTab* copyTab = fixture.currentDocumentTab();
    QVERIFY(copyTab && copyTab != tab);
    QVERIFY(!copyTab->getDocument()->areUserChangesSuspended());
    QVERIFY(copyTab->getDocument()->isEditable());
    QVERIFY(document->areUserChangesSuspended());
    QVERIFY(view->isToolModeActive());
    fixture.closeMessageBoxes();
    view->leaveToolMode();
}

void AppToolViewTest::screenshots()
{
    using H = AppToolViewTestHelper;

    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);

    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::open(fixture, pdftest::AppDocumentSamples::large(fixture.folder(), 1592));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    struct Frame
    {
        const char* name;
        int parts;
    };
    for (const Frame& frame : { Frame{ "291-994-sidebar-and-canvas", ToolTestParts::Toolbar | ToolTestParts::RightPanel | ToolTestParts::Target },
                                Frame{ "339-8112-canvas-page", ToolTestParts::Toolbar | ToolTestParts::CanvasPage | ToolTestParts::RightPanel | ToolTestParts::Target },
                                Frame{ "389-20177-left-panel", ToolTestParts::All } })
    {
        for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            ToolTestParts parts;
            QVERIFY(view->enterToolMode(parts.get(frame.parts)));
            view->setToolMessage(QStringLiteral("Compress · Project_Report.pdf · 1,592 pages · 24.8 MB"));
            view->goToPage(307);
            view->setZoom(1.51);
            fixture.settle();
            const QString themeName = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
            QVERIFY(fixture.window->grab().save(QStringLiteral("%1/tool-view-%2-%3.png").arg(folder, QString::fromLatin1(frame.name), themeName)));
            view->leaveToolMode();
            fixture.settle();
        }
    }
}

QTEST_MAIN(AppToolViewTest)
#include "tst_apptoolviewtest.moc"
