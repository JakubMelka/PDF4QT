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

#include "pdfappdocumentview.h"
#include "pdfappannotationcontroller.h"
#include "pdfappcommentpopup.h"
#include "pdfappformcontroller.h"
#include "pdfappoutlineeditor.h"
#include "pdfapplocale.h"
#include "pdfappcommand.h"
#include "pdfappcolorprofiles.h"
#include "pdfappcontext.h"
#include "pdfappdeveloperoverlay.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentpanels_outline.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappmainwindow.h"
#include "pdfapproute.h"
#include "pdfapptoolmodelayout.h"
#include "pdfapptoolundotarget.h"
#include "pdfsettingdefinitions.h"

#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcompiler.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfexecutionpolicy.h"
#include "pdfprogress.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgettool.h"

#include "pdfthememanager.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLocale>
#include <QPainter>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Conversions of the settings and of the commands of the view
class PDFAppDocumentViewHelper
{
public:
    PDFAppDocumentViewHelper() = delete;

    /// Property of an action of the canvas: the id of its command
    static constexpr const char* COMMAND_PROPERTY = "pdf4qtCommandId";

    /// Features of the renderer owned by package F (developer overlays): the view
    /// keeps them, when it applies the settings
    static constexpr PDFRenderer::Features getDeveloperFeatures()
    {
        return PDFRenderer::Features(PDFRenderer::DisplayTimes | PDFRenderer::DebugTextBlocks | PDFRenderer::DebugTextLines);
    }

    /// Feature of the color mode of the setting viewing.colorMode
    static PDFRenderer::Features getColorModeFeature(const QString& mode)
    {
        if (mode == QLatin1String("inverted"))
        {
            return PDFRenderer::ColorAdjust_Invert;
        }
        if (mode == QLatin1String("grayscale"))
        {
            return PDFRenderer::ColorAdjust_Grayscale;
        }
        if (mode == QLatin1String("highContrast"))
        {
            return PDFRenderer::ColorAdjust_HighContrast;
        }
        if (mode == QLatin1String("bitonal"))
        {
            return PDFRenderer::ColorAdjust_Bitonal;
        }
        if (mode == QLatin1String("customColors"))
        {
            return PDFRenderer::ColorAdjust_CustomColors;
        }
        return PDFRenderer::None;
    }

    /// Color of a setting ("#RRGGBB"); an invalid text gives the default color
    static QColor getColor(const QString& text, const QColor& defaultColor)
    {
        const QColor color = QColor::fromString(text.trimmed());
        return color.isValid() ? color : defaultColor;
    }

    static PDFCMSSettings::System getSystem(const QString& text)
    {
        return text == QLatin1String("generic") ? PDFCMSSettings::System::Generic : PDFCMSSettings::System::LittleCMS2;
    }

    static RenderingIntent getIntent(const QString& text)
    {
        if (text == QLatin1String("perceptual")) { return RenderingIntent::Perceptual; }
        if (text == QLatin1String("relativeColorimetric")) { return RenderingIntent::RelativeColorimetric; }
        if (text == QLatin1String("absoluteColorimetric")) { return RenderingIntent::AbsoluteColorimetric; }
        if (text == QLatin1String("saturation")) { return RenderingIntent::Saturation; }
        return RenderingIntent::Auto;
    }

    static PDFCMSSettings::Accuracy getAccuracy(const QString& text)
    {
        if (text == QLatin1String("low")) { return PDFCMSSettings::Accuracy::Low; }
        if (text == QLatin1String("high")) { return PDFCMSSettings::Accuracy::High; }
        return PDFCMSSettings::Accuracy::Medium;
    }

    static PDFCMSSettings::ColorAdaptationXYZ getColorAdaptation(const QString& text)
    {
        if (text == QLatin1String("none")) { return PDFCMSSettings::ColorAdaptationXYZ::None; }
        if (text == QLatin1String("xyzScaling")) { return PDFCMSSettings::ColorAdaptationXYZ::XYZScaling; }
        if (text == QLatin1String("cat97")) { return PDFCMSSettings::ColorAdaptationXYZ::CAT97; }
        if (text == QLatin1String("cat02")) { return PDFCMSSettings::ColorAdaptationXYZ::CAT02; }
        return PDFCMSSettings::ColorAdaptationXYZ::Bradford;
    }

    static RendererEngine getRendererEngine(const QString& text)
    {
        if (text == QLatin1String("qpainter")) { return RendererEngine::QPainter; }
        if (text == QLatin1String("blend2dSingleThread")) { return RendererEngine::Blend2D_SingleThread; }
        return RendererEngine::Blend2D_MultiThread;
    }

    static PDFExecutionPolicy::Strategy getStrategy(const QString& text)
    {
        if (text == QLatin1String("singleThread")) { return PDFExecutionPolicy::Strategy::SingleThreaded; }
        if (text == QLatin1String("maximumThreads")) { return PDFExecutionPolicy::Strategy::AlwaysMultithreaded; }
        return PDFExecutionPolicy::Strategy::PageMultithreaded;
    }

    /// Page layout of the proxy for the layout of the view and the first page on the right
    static PageLayout getPageLayout(PDFAppViewLayout layout, bool isFirstPageRight)
    {
        switch (layout)
        {
            case PDFAppViewLayout::SinglePage:
                return PageLayout::SinglePage;
            case PDFAppViewLayout::Continuous:
                return PageLayout::OneColumn;
            case PDFAppViewLayout::TwoPages:
                return isFirstPageRight ? PageLayout::TwoPagesRight : PageLayout::TwoPagesLeft;
            case PDFAppViewLayout::TwoColumns:
                return isFirstPageRight ? PageLayout::TwoColumnRight : PageLayout::TwoColumnLeft;
        }
        return PageLayout::OneColumn;
    }

    /// Removes the pages out of the range of the document
    static void removePagesOutOfRange(std::vector<PDFInteger>& pages, PDFInteger pageCount)
    {
        std::vector<PDFInteger> validPages;
        validPages.reserve(pages.size());
        for (PDFInteger page : pages)
        {
            if (page >= 0 && page < pageCount)
            {
                validPages.push_back(page);
            }
        }
        pages = std::move(validPages);
    }

    /// Rotation of the view in degrees
    static int getRotationDegrees(PageRotation rotation)
    {
        switch (rotation)
        {
            case PageRotation::None:
                return 0;
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
        }
        return 0;
    }
};

PDFAppDocumentSplitterHandle::PDFAppDocumentSplitterHandle(Qt::Orientation orientation, QSplitter* parent) :
    BaseClass(orientation, parent)
{
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentSplitterHandle::update));
}

PDFAppDocumentSplitterHandle::~PDFAppDocumentSplitterHandle() = default;

std::vector<QRect> PDFAppDocumentSplitterHandle::getGripDots() const
{
    // grip 266:7855: three dots in the middle of the handle, along the handle
    std::vector<QRect> dots;
    const int length = 3 * GRIP_DOT + 2 * GRIP_GAP;
    const bool isVerticalHandle = orientation() == Qt::Horizontal;
    const int across = ((isVerticalHandle ? width() : height()) - GRIP_DOT) / 2;
    const int along = ((isVerticalHandle ? height() : width()) - length) / 2;
    for (int i = 0; i < 3; ++i)
    {
        const int position = along + i * (GRIP_DOT + GRIP_GAP);
        dots.push_back(isVerticalHandle ? QRect(across, position, GRIP_DOT, GRIP_DOT) : QRect(position, across, GRIP_DOT, GRIP_DOT));
    }
    return dots;
}

void PDFAppDocumentSplitterHandle::paintEvent(QPaintEvent* event)
{
    {
        const PDFThemeManager* manager = PDFThemeManager::getInstance();
        QPainter painter(this);
        painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSidebar));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(manager->getColor(PDFThemeColors::IconMuted));
        for (const QRect& dot : getGripDots())
        {
            painter.drawEllipse(QRectF(dot));
        }
    }

    // The line of the style, when the pointer rests over the handle or it is dragged
    BaseClass::paintEvent(event);
}

PDFAppDocumentSplitter::PDFAppDocumentSplitter(Qt::Orientation orientation, QWidget* parent) :
    BaseClass(orientation, parent)
{

}

PDFAppDocumentSplitter::~PDFAppDocumentSplitter() = default;

QSplitterHandle* PDFAppDocumentSplitter::createHandle()
{
    return new PDFAppDocumentSplitterHandle(orientation(), this);
}

void PDFAppDocumentSplitter::paintEvent(QPaintEvent* event)
{
    // Right-to-left QSplitterPrivate::setGeo places the widgets by moveRight(width - left), so
    // they lie 1 px to the right of their place and the splitter shows at its left edge: the
    // color of the handle, never the white of the window between a panel and the canvas
    {
        QPainter painter(this);
        painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgSidebar));
    }
    BaseClass::paintEvent(event);
}

PDFAppDocumentView::PDFAppDocumentView(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("documentView"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The places of the view (PDFAppToolModeLayout, shared with the own tab of a tool): the
    // toolbar of the document, in the tool mode the toolbar of the tool (both 52 px); the
    // body (document-body 193:882) with the sidebar, the splitter and the canvas, the right
    // panel of a tool next to the splitter (291:1029); the status bar below
    m_modeLayout = new PDFAppToolModeLayout(this, layout);
    connect(m_modeLayout.data(), &PDFAppToolModeLayout::partDestroyed, this, &PDFAppDocumentView::onToolPartDestroyed);
    connect(m_modeLayout.data(), &PDFAppToolModeLayout::infoBarActionTriggered, this, &PDFAppDocumentView::infoBarActionTriggered);
    m_toolbarStack = m_modeLayout->getToolbarStack();
    m_toolbar = new PDFAppDocumentToolbar(this, m_toolbarStack);
    m_modeLayout->setBaseToolbar(m_toolbar);

    m_splitter = m_modeLayout->getSplitter();
    m_splitter->installEventFilter(this);
    m_canvasArea = m_modeLayout->getCanvasArea();

    m_sidebar = new PDFAppDocumentSidebar(this, m_splitter);
    m_sidebar->setMinimumWidth(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH);
    m_modeLayout->setBaseLeftWidget(m_sidebar);
    connect(m_sidebar, &PDFAppDocumentSidebar::currentPanelChanged, this, &PDFAppDocumentView::panelChanged);
    connect(m_splitter, &QSplitter::splitterMoved, this, &PDFAppDocumentView::onSplitterMoved);

    // The installed profiles of the system are listed once per process (the cache
    // of the external profiles turned on by PDFAppContext)
    m_cmsManager = new PDFCMSManager(this);
    m_progress = new PDFProgress(this);

    m_pdfWidget = new PDFWidget(m_cmsManager, RendererEngine::QPainter, m_canvasArea);
    m_pdfWidget->setObjectName(QStringLiteral("canvas"));
    m_modeLayout->setBaseCanvas(m_pdfWidget);
    m_splitter->setSizes({ PDFAppDocumentSidebar::DEFAULT_WIDTH, 1134 });
    m_sidebarWidth = PDFAppDocumentSidebar::DEFAULT_WIDTH;

    PDFDrawWidgetProxy* proxy = m_pdfWidget->getDrawWidgetProxy();
    proxy->setProgress(m_progress);

    m_annotationManager = new PDFWidgetAnnotationManager(proxy, this);
    m_formManager = new PDFWidgetFormManager(proxy, this);
    m_toolManager = new PDFToolManager(proxy, this);

    m_pdfWidget->setAnnotationManager(m_annotationManager);
    m_pdfWidget->setFormManager(m_formManager);
    m_pdfWidget->setToolManager(m_toolManager);
    m_formManager->setAnnotationManager(m_annotationManager);
    m_annotationManager->setFormManager(m_formManager);

    connect(proxy, &PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFAppDocumentView::onDrawSpaceChanged);
    connect(proxy, &PDFDrawWidgetProxy::pageLayoutChanged, this, &PDFAppDocumentView::viewChanged);
    connect(proxy, &PDFDrawWidgetProxy::textLayoutChanged, this, &PDFAppDocumentView::onTextLayoutChanged);
    connect(m_toolManager, &PDFToolManager::messageDisplayRequest, this, &PDFAppDocumentView::onToolMessage);
    connect(m_toolManager, &PDFToolManager::activeToolChanged, this, &PDFAppDocumentView::onActiveToolChanged);
    if (PDFSelectTextTool* selectTextTool = m_toolManager->getSelectTextTool())
    {
        connect(selectTextTool, &PDFSelectTextTool::selectionChanged, this, &PDFAppDocumentView::viewStateChanged);
    }

    // Step 4: the controller of the annotations commits the changes of the engine
    // and gives it the delegated user interface; the form and outline controllers
    // of the packages D and C
    m_annotationController = new PDFAppAnnotationController(this);
    m_formController = new PDFAppFormController(this);
    m_outlineEditor = new PDFAppOutlineEditor(this, this);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppDocumentView::onThemeChanged);

    m_statusBar = m_modeLayout->createStatusBar(layout);
    connect(m_statusBar, &PDFAppDocumentStatusBar::pageItemClicked, this, &PDFAppDocumentView::openGoToPage);

    // Esc leaves the full screen (enabled only in the full screen)
    m_leaveFullScreenAction = new QAction(this);
    m_leaveFullScreenAction->setObjectName(QStringLiteral("leaveFullScreen"));
    m_leaveFullScreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
    m_leaveFullScreenAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_leaveFullScreenAction->setEnabled(false);
    connect(m_leaveFullScreenAction.data(), &QAction::triggered, this, &PDFAppDocumentView::leaveFullScreen);
    addAction(m_leaveFullScreenAction);

    createCanvasActions();

    if (m_context && m_context->getNotifier())
    {
        connect(m_context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppDocumentView::onDatabaseChanged);
        connect(m_context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppDocumentView::onEverythingChanged);
    }

    m_isSidebarVisible = m_context ? m_context->getSettings().getBool(PDFSettingKeys::viewingShowSidebar()) : true;
    updateSidebarVisibility();
    applySettings();
    updateViewColors();
}

PDFAppDocumentView::~PDFAppDocumentView()
{
    // The full screen of the window ends with its view
    if (m_isFullScreen)
    {
        if (PDFAppMainWindow* mainWindow = qobject_cast<PDFAppMainWindow*>(window()))
        {
            mainWindow->setFullScreenMode(false);
        }
    }

    // A dying view emits nothing more: the managers detached from the document
    // below change their state (the Select text tool clears its selection) and the
    // receivers would read a view being destroyed (review RT4; QObject::destroyed
    // is still emitted - it ignores the blocked signals)
    blockSignals(true);
    if (PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr)
    {
        selectTextTool->disconnect(this);
    }

    // The parts of the tool mode go while the view is whole (they are children of the
    // view now and they may ask it); they report nothing to the dying view
    const bool wasToolModeActive = m_isToolModeActive;
    m_isToolModeActive = false;
    if (m_toolUndoTarget)
    {
        m_toolUndoTarget->disconnect(this);
    }
    if (m_modeLayout)
    {
        m_modeLayout->disconnect(this);
        for (QWidget* part : { m_modeLayout->getToolbarPart(), m_modeLayout->getLeftPanelPart(), m_modeLayout->getCanvasPagePart(), m_modeLayout->getRightPanelPart() })
        {
            delete part;
        }
    }

    // The controllers of step 4 go first (they use the managers and the document)
    delete m_commentPopup.data();
    delete m_outlineEditor;
    m_outlineEditor = nullptr;
    delete m_formController;
    m_formController = nullptr;
    delete m_annotationController;
    m_annotationController = nullptr;

    // A document, which outlives the view, can be changed again
    if (wasToolModeActive && m_document)
    {
        m_document->setUserChangesSuspended(false);
    }

    // The managers are detached from the widget and from the document before the
    // widget and they are destroyed (the order of the test fixtures of Widgets)
    if (m_document)
    {
        m_document->disconnect(this);
    }
    if (m_tab)
    {
        m_tab->disconnect(this);
    }

    if (m_toolManager)
    {
        m_toolManager->disconnect(this);
        m_toolManager->setDocument(PDFModifiedDocument());
    }
    if (m_formManager)
    {
        m_formManager->setDocument(PDFModifiedDocument());
    }
    if (m_annotationManager)
    {
        m_annotationManager->setDocument(PDFModifiedDocument());
    }
    if (m_pdfWidget)
    {
        m_pdfWidget->getDrawWidgetProxy()->disconnect(this);
        m_pdfWidget->setDocument(PDFModifiedDocument(), {});
        m_pdfWidget->setFormManager(nullptr);
        m_pdfWidget->setAnnotationManager(nullptr);
        m_pdfWidget->setToolManager(nullptr);
    }

    // The toolbar with its panels goes before the canvas (the panels ask the view)
    delete m_toolbar.data();

    // The panels of the sidebar die with it: they report nothing to the sidebar,
    // which is being destroyed together with them
    if (m_sidebar)
    {
        for (PDFAppDocumentPanel* panel : m_sidebar->findChildren<PDFAppDocumentPanel*>())
        {
            panel->disconnect(m_sidebar);
        }
    }

    delete m_toolManager;
    m_toolManager = nullptr;
    delete m_formManager;
    m_formManager = nullptr;
    delete m_annotationManager;
    m_annotationManager = nullptr;
    delete m_pdfWidget;
    m_pdfWidget = nullptr;
}

PDFAppCommentPopup* PDFAppDocumentView::getCommentPopup()
{
    if (!m_commentPopup && m_canvasArea)
    {
        m_commentPopup = new PDFAppCommentPopup(this, m_canvasArea);
    }
    return m_commentPopup.data();
}

PDFDrawWidgetProxy* PDFAppDocumentView::getProxy() const
{
    return m_pdfWidget ? m_pdfWidget->getDrawWidgetProxy() : nullptr;
}

PDFAppDocumentTab* PDFAppDocumentView::getTab() const
{
    for (QObject* object = parent(); object; object = object->parent())
    {
        if (PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(object))
        {
            return tab;
        }
    }
    return nullptr;
}

void PDFAppDocumentView::connectTab()
{
    PDFAppDocumentTab* tab = getTab();
    if (m_tab == tab)
    {
        return;
    }

    if (m_tab)
    {
        m_tab->disconnect(this);
    }
    m_tab = tab;
    if (tab)
    {
        connect(tab, &PDFAppTab::commandStatesChanged, this, &PDFAppDocumentView::onTabCommandStatesChanged);
    }
}

void PDFAppDocumentView::setDocument(PDFAppDocument* document)
{
    connectTab();

    if (m_document == document)
    {
        return;
    }

    // The tool mode belongs to the shown document (a slot may delete the view)
    if (m_isToolModeActive)
    {
        QPointer<PDFAppDocumentView> guard(this);
        leaveToolMode();
        if (!guard)
        {
            return;
        }
    }

    if (m_document)
    {
        m_document->disconnect(this);
    }

    m_document = document;
    m_isDefaultViewApplied = false;
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppDocumentView::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentView::onDocumentStateChanged);
        connect(document, &PDFAppDocument::lockChanged, this, &PDFAppDocumentView::onDocumentLockChanged);
        connect(document, &PDFAppDocument::signatureResultsChanged, this, &PDFAppDocumentView::onSignatureResultsChanged);
        connect(document, &PDFAppDocument::aboutToBeDestroyed, this, &PDFAppDocumentView::onDocumentAboutToBeDestroyed);
        connect(document, &QObject::destroyed, this, &PDFAppDocumentView::onDocumentDestroyed);
    }

    if (m_sidebar)
    {
        m_sidebar->setDocument(document);
    }
    applyDocument();
    if (m_annotationController)
    {
        m_annotationController->setDocument(document);
    }
    if (m_formController)
    {
        m_formController->setDocument(document);
    }
    if (m_outlineEditor)
    {
        m_outlineEditor->setDocument(document);
    }
}

void PDFAppDocumentView::applyDocument()
{
    PDFModifiedDocument modified;
    if (m_document && m_document->getDocument())
    {
        modified = PDFModifiedDocument(m_document->getDocument(), m_document->getOptionalContentActivity());
    }
    onDocumentChanged(modified);
}

void PDFAppDocumentView::onDocumentStateChanged()
{
    // A document, which is not shown any more, has no tool (F2; a slot may delete the view)
    if (m_isToolModeActive && m_document && !m_document->isReady())
    {
        QPointer<PDFAppDocumentView> guard(this);
        leaveToolMode();
        if (!guard)
        {
            return;
        }
    }

    // A document read in a worker thread becomes Ready later (package A)
    if (m_document && m_document->isReady() && getProxy() && getProxy()->getDocument() != m_document->getDocument().data())
    {
        applyDocument();
        return;
    }

    updateStatusMessage();
    Q_EMIT commandStatesChanged();
}

void PDFAppDocumentView::onDocumentChanged(const PDFModifiedDocument& modified)
{
    if (!m_pdfWidget)
    {
        return;
    }

    std::vector<PDFSignatureVerificationResult> signatures;
    if (m_document && modified.getDocument())
    {
        signatures = m_document->getSignatureResults();
    }

    // A new version of the shown document with another layout (Rotate, undo / redo of
    // it) keeps the page the user looks at in its place; the layout changes in steps,
    // the current page of a step is not announced
    const PageAnchor anchor = capturePageAnchor(modified);
    m_isRestoringPageAnchor = anchor.pageIndex >= 0;

    // Each manager notifies the application (the draw space, the view): a slot may
    // delete the view with its tab or window (UnitTestsAppMcpLifetime)
    QPointer<PDFAppDocumentView> guard(this);
    m_cmsManager->setDocument(modified.getDocument());
    if (guard)
    {
        m_pdfWidget->setDocument(modified, std::move(signatures));
    }
    if (guard)
    {
        m_annotationManager->setDocument(modified);
    }
    if (guard)
    {
        m_formManager->setDocument(modified);
    }
    if (guard)
    {
        m_toolManager->setDocument(modified);
    }
    if (guard && m_isRestoringPageAnchor)
    {
        restorePageAnchor(anchor);
    }
    if (!guard)
    {
        return;
    }
    m_isRestoringPageAnchor = false;

    // The selected pages of a document with fewer pages
    PDFAppDocumentViewHelper::removePagesOutOfRange(m_selectedPages, getPageCount());
    if (m_chosenPageIndex >= getPageCount())
    {
        m_chosenPageIndex = -1;
    }

    if (modified.getDocument() && !m_isDefaultViewApplied)
    {
        applyDefaultView();
        if (!guard)
        {
            return;
        }
    }

    // Slots of the signals may delete the view (the tab)
    m_lastPageIndex = -2;
    m_lastDisplayedPageIndex = -2;
    onDrawSpaceChanged();
    if (!guard)
    {
        return;
    }
    updateStatusMessage();
    Q_EMIT viewChanged();
    if (!guard)
    {
        return;
    }
    Q_EMIT commandStatesChanged();
}

PDFAppDocumentView::PageAnchor PDFAppDocumentView::capturePageAnchor(const PDFModifiedDocument& modified) const
{
    PageAnchor anchor;

    // Only a new version of the shown document with the same pages, whose layout is
    // computed again (a Reset change); the first document gets its default view
    const PDFDrawWidgetProxy* proxy = getProxy();
    const PDFDocument* shownDocument = proxy ? proxy->getDocument() : nullptr;
    const PDFDocument* newDocument = modified.getDocument();
    if (!m_isDefaultViewApplied || !shownDocument || !newDocument || shownDocument == newDocument || !modified.hasReset() ||
        shownDocument->getCatalog()->getPageCount() != newDocument->getCatalog()->getPageCount())
    {
        return anchor;
    }

    const PDFInteger pageIndex = getCurrentPageIndex();
    const PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
    if (item && item->rect.height() > 0.0)
    {
        anchor.pageIndex = pageIndex;
        anchor.top = item->rect.top();
        anchor.height = item->rect.height();
    }
    return anchor;
}

void PDFAppDocumentView::restorePageAnchor(const PageAnchor& anchor)
{
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!proxy || anchor.pageIndex < 0 || anchor.pageIndex >= getPageCount())
    {
        return;
    }

    // A page, which has left the canvas, is placed at the top first (goToPage applies
    // the zoom mode for the page, so only then; a receiver of drawSpaceChanged may
    // delete the view), then moved to its former top
    if (!proxy->getSnapshot().hasPage(anchor.pageIndex))
    {
        QPointer<PDFAppDocumentView> guard(this);
        QPointer<PDFDrawWidgetProxy> proxyGuard(proxy);
        proxy->goToPage(anchor.pageIndex);
        if (!guard || !proxyGuard)
        {
            return;
        }
    }

    const PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(anchor.pageIndex);
    if (!item)
    {
        return;
    }

    // A page scrolled partly above the canvas shows the same part of it at the top
    // also, when its height has changed (the page itself was rotated)
    double top = anchor.top;
    if (top < 0.0)
    {
        top = anchor.top * item->rect.height() / anchor.height;
    }

    const int delta = int(std::lround(top - item->rect.top()));
    if (delta != 0)
    {
        proxy->scrollByPixels(QPoint(0, delta));
    }
}

void PDFAppDocumentView::onSignatureResultsChanged()
{
    // Package D verifies the signatures in a worker after the document is shown:
    // the canvas (signature fields of the form manager) gets the results when they
    // arrive. Only the results of the shown version are passed - the results of a
    // version not shown yet come with its documentChanged (onDocumentChanged).
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!m_pdfWidget || !proxy || !m_document || !m_document->getDocument() || proxy->getDocument() != m_document->getDocument().data())
    {
        return;
    }

    // The same document: the proxy keeps its compiled pages (and the widget its
    // rendering errors, PDFWidget::setDocument would clear them) and takes the results
    proxy->setDocument(PDFModifiedDocument(m_document->getDocument(), m_document->getOptionalContentActivity()), m_document->getSignatureResults());
    if (QWidget* drawWidget = m_pdfWidget->getDrawWidget() ? m_pdfWidget->getDrawWidget()->getWidget() : nullptr)
    {
        drawWidget->update();
    }
}

void PDFAppDocumentView::onDocumentAboutToBeDestroyed()
{
    // The document is still whole: the page compilers stop reading its PDF document
    if (m_document)
    {
        m_document->disconnect(this);
    }
    onDocumentDestroyed();
}

void PDFAppDocumentView::onDocumentDestroyed()
{
    // The document is not whole any more: only the view is cleared
    m_document = nullptr;

    // The tool mode ends with its document (a slot may delete the view)
    QPointer<PDFAppDocumentView> guard(this);
    if (m_isToolModeActive)
    {
        leaveToolMode();
        if (!guard)
        {
            return;
        }
    }
    onDocumentChanged(PDFModifiedDocument());
}

void PDFAppDocumentView::applyDefaultView()
{
    // The defaults of a new document (settings without a section); package A
    // applies the stored view state of the file afterwards
    m_isDefaultViewApplied = true;

    PDFSettingsRepository settings = m_context ? m_context->getSettings() : PDFSettingsRepository(nullptr, nullptr);
    const PDFSettingRegistry* registry = m_context ? m_context->getSettingRegistry() : nullptr;
    if (m_context && registry)
    {
        m_layout = PDFAppViewState::getLayoutFromName(settings.getString(PDFSettingKeys::viewingPageLayout())).value_or(PDFAppViewLayout::Continuous);
        m_isFirstPageRight = settings.getBool(PDFSettingKeys::viewingFirstPageRight());
    }
    applyPageLayout();

    PDFDrawWidgetProxy* proxy = getProxy();
    const QString zoom = m_context ? settings.getString(PDFSettingKeys::viewingZoom()) : QStringLiteral("fitWidth");
    if (proxy)
    {
        if (zoom == QLatin1String("fitPage"))
        {
            proxy->performOperation(PDFDrawWidgetProxy::ZoomFit);
        }
        else if (zoom == QLatin1String("actualSize"))
        {
            proxy->zoom(1.0);
        }
        else
        {
            proxy->performOperation(PDFDrawWidgetProxy::ZoomFitWidth);
        }
    }

    // The chosen mode of the canvas - Hand in a new tab (Select menu 198:5517)
    activateDefaultCanvasTool();
}

void PDFAppDocumentView::applyPageLayout()
{
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!proxy)
    {
        return;
    }

    const PageLayout pageLayout = PDFAppDocumentViewHelper::getPageLayout(m_layout, m_isFirstPageRight);
    if (proxy->getPageLayout() != pageLayout)
    {
        proxy->setPageLayout(pageLayout);
    }
}

void PDFAppDocumentView::goToPage(PDFInteger pageIndex)
{
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!proxy || pageIndex < 0 || pageIndex >= getPageCount())
    {
        return;
    }
    proxy->goToPage(pageIndex);
}

PDFInteger PDFAppDocumentView::getCurrentPageIndex() const
{
    const std::vector<PDFInteger> pages = getShownPages();
    return pages.empty() ? -1 : pages.front();
}

std::vector<PDFInteger> PDFAppDocumentView::getShownPages() const
{
    if (!m_pdfWidget || getPageCount() <= 0)
    {
        return std::vector<PDFInteger>();
    }
    return m_pdfWidget->getDrawWidget()->getCurrentPages();
}

bool PDFAppDocumentView::isPageShown(PDFInteger pageIndex) const
{
    const std::vector<PDFInteger> pages = getShownPages();
    return std::binary_search(pages.cbegin(), pages.cend(), pageIndex);
}

PDFInteger PDFAppDocumentView::getDisplayedPageIndex() const
{
    return getDisplayedPageIndex(getShownPages());
}

PDFInteger PDFAppDocumentView::getDisplayedPageIndex(const std::vector<PDFInteger>& shownPages) const
{
    if (shownPages.empty())
    {
        return -1;
    }

    // The page clicked in the thumbnails, while the user sees it (the thumbnails keep
    // it selected by the same rule): the right page of a row of two pages is not the
    // current page, the left one is
    if (m_chosenPageIndex >= 0 && std::binary_search(shownPages.cbegin(), shownPages.cend(), m_chosenPageIndex))
    {
        return m_chosenPageIndex;
    }
    return shownPages.front();
}

void PDFAppDocumentView::updateDisplayedPage()
{
    const PDFInteger pageIndex = getDisplayedPageIndex();
    if (pageIndex == m_lastDisplayedPageIndex)
    {
        return;
    }
    m_lastDisplayedPageIndex = pageIndex;
    updatePageItem();

    Q_EMIT displayedPageChanged(pageIndex);
}

void PDFAppDocumentView::updatePageItem()
{
    if (!m_statusBar)
    {
        return;
    }

    const PDFInteger pageIndex = getDisplayedPageIndex();
    if (pageIndex >= 0 && hasPageLabels())
    {
        m_statusBar->setPageText(tr("Page %1 (%2) of %3").arg(PDFAppLocale::formatNumber(pageIndex + 1), getPageLabel(pageIndex), PDFAppLocale::formatNumber(getPageCount())));
    }
    else if (pageIndex >= 0 && m_isToolModeActive)
    {
        // status-page 293:1407 "Page 308 / 1592": a toolbar of a tool has no page field;
        // the numbers without the group separator as the page field of the toolbar
        QLocale locale = PDFAppLocale::getUiLocale();
        locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
        m_statusBar->setPageText(tr("Page %1 / %2").arg(locale.toString(pageIndex + 1), locale.toString(getPageCount())));
    }
    else
    {
        m_statusBar->setPageText(QString());
    }
}

void PDFAppDocumentView::updateZoomItem()
{
    if (!m_statusBar)
    {
        return;
    }

    // status-zoom 293:1415 "151 %": only in the tool mode (a toolbar of a tool has no zoom field)
    m_statusBar->setZoomText(m_isToolModeActive && getPageCount() > 0 ? tr("%1 %").arg(PDFAppLocale::formatNumber(qRound(getZoom() * 100.0))) : QString());
}

PDFInteger PDFAppDocumentView::getPageCount() const
{
    // The shown document (the proxy), which may lag the model during its change
    const PDFDrawWidgetProxy* proxy = getProxy();
    const PDFDocument* document = proxy ? proxy->getDocument() : nullptr;
    return document ? PDFInteger(document->getCatalog()->getPageCount()) : 0;
}

QString PDFAppDocumentView::getPageLabel(PDFInteger pageIndex) const
{
    const PDFDrawWidgetProxy* proxy = getProxy();
    const PDFDocument* document = proxy ? proxy->getDocument() : nullptr;
    if (!document || pageIndex < 0 || pageIndex >= getPageCount())
    {
        return QString();
    }

    // The number without separators of the locale: the label is typed back into
    // the page fields ("1,500" would not be found by findPageIndex)
    const QString label = document->getCatalog()->getPageLabel(pageIndex);
    return label.isEmpty() ? QString::number(pageIndex + 1) : label;
}

int PDFAppDocumentView::getMagnifierRadius(int diameter)
{
    return qMax(diameter / 2, 1);
}

bool PDFAppDocumentView::hasPageLabels() const
{
    const PDFDrawWidgetProxy* proxy = getProxy();
    const PDFDocument* document = proxy ? proxy->getDocument() : nullptr;
    if (!document || document->getCatalog()->getPageLabels().empty())
    {
        return false;
    }

    // Labels "1", "2", ... are the numbers of the pages (the first ones decide)
    const PDFInteger checkedCount = qMin<PDFInteger>(getPageCount(), 64);
    for (PDFInteger i = 0; i < checkedCount; ++i)
    {
        if (document->getCatalog()->getPageLabel(i) != QString::number(i + 1))
        {
            return true;
        }
    }
    return false;
}

std::optional<PDFInteger> PDFAppDocumentView::findPageIndex(const QString& text) const
{
    const PDFDrawWidgetProxy* proxy = getProxy();
    const PDFDocument* document = proxy ? proxy->getDocument() : nullptr;
    const QString trimmed = text.trimmed();
    if (!document || trimmed.isEmpty() || trimmed.size() > 256)
    {
        return std::nullopt;
    }

    // A label of the document goes first (a label "5" may be the page 9)
    if (std::optional<PDFInteger> labelIndex = document->getCatalog()->findPageIndexByLabel(trimmed))
    {
        if (*labelIndex >= 0 && *labelIndex < getPageCount())
        {
            return labelIndex;
        }
    }

    // A number of a page: decimal digits of any script only (no signs, no
    // separators, no invisible characters), bounded by the count of the pages
    const PDFInteger pageCount = getPageCount();
    PDFInteger number = 0;
    for (QChar character : trimmed)
    {
        const int digit = character.digitValue();
        if (!character.isDigit() || digit < 0)
        {
            return std::nullopt;
        }

        number = number * 10 + digit;
        if (number > pageCount)
        {
            return std::nullopt;
        }
    }

    if (number >= 1)
    {
        return number - 1;
    }
    return std::nullopt;
}

std::vector<PDFInteger> PDFAppDocumentView::getSelectedPages() const
{
    if (!m_selectedPages.empty())
    {
        return m_selectedPages;
    }

    std::vector<PDFInteger> pages;
    const PDFInteger currentPage = getCurrentPageIndex();
    if (currentPage >= 0)
    {
        pages.push_back(currentPage);
    }
    return pages;
}

void PDFAppDocumentView::setSelectedPages(std::vector<PDFInteger> pages, PDFInteger chosenPage)
{
    PDFAppDocumentViewHelper::removePagesOutOfRange(pages, getPageCount());
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    m_chosenPageIndex = chosenPage >= 0 && chosenPage < getPageCount() ? chosenPage : -1;

    // A slot may delete the view (the tab)
    QPointer<PDFAppDocumentView> guard(this);
    if (m_selectedPages != pages)
    {
        m_selectedPages = std::move(pages);
        Q_EMIT selectedPagesChanged();
    }
    if (guard)
    {
        updateDisplayedPage();
    }
}

bool PDFAppDocumentView::showPanel(const QString& panelId)
{
    // The sidebar is hidden below the left panel of a tool and comes back as the user left it
    if (isToolLeftPanelShown())
    {
        return false;
    }

    // A slot of the change of the panel of the sidebar may delete the view (the tab)
    QPointer<PDFAppDocumentView> self(this);
    if (!m_sidebar || !m_sidebar->showPanel(panelId) || !self)
    {
        return false;
    }

    // A shown panel shows the sidebar
    if (!m_isSidebarVisible)
    {
        setSidebarVisible(true);
    }
    return true;
}

QString PDFAppDocumentView::getCurrentPanelId() const
{
    return m_sidebar ? m_sidebar->getCurrentPanelId() : QString();
}

void PDFAppDocumentView::addStatusItem(QWidget* item, PDFAppDocumentStatusBar::Area area)
{
    if (m_statusBar)
    {
        m_statusBar->addItem(item, area);
    }
}

// ----------------------------------------------------------------- info bars

void PDFAppDocumentView::showInfoBar(const PDFAppInfoBarRequest& request)
{
    if (m_modeLayout)
    {
        m_modeLayout->showInfoBar(request);
    }
}

void PDFAppDocumentView::hideInfoBar(const QString& id)
{
    if (m_modeLayout)
    {
        m_modeLayout->hideInfoBar(id);
    }
}

QStringList PDFAppDocumentView::getInfoBarIds() const
{
    return m_modeLayout ? m_modeLayout->getInfoBarIds() : QStringList();
}

const PDFAppInfoBarRequest* PDFAppDocumentView::findInfoBar(const QString& id) const
{
    return m_modeLayout ? m_modeLayout->findInfoBar(id) : nullptr;
}

PDFAppInfoBar* PDFAppDocumentView::findInfoBarWidget(const QString& id) const
{
    return m_modeLayout ? m_modeLayout->findInfoBarWidget(id) : nullptr;
}

void PDFAppDocumentView::layoutInfoBars()
{
    if (m_modeLayout)
    {
        m_modeLayout->layoutInfoBars();
    }
}

// ------------------------------------------------------------------- colors

void PDFAppDocumentView::updateViewColors()
{
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!proxy)
    {
        return;
    }

    // Colors of the theme, never of a palette (the palette of the application is
    // the violet leak color): the canvas bg/track (document-canvas 193:1256 and
    // 198:6240 - the white pages are seen on it), the selection of a text of a form
    // bg/accent-subtle with text/accent
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    PDFViewColors colors;
    colors.canvas = manager->getColor(PDFThemeColors::BgTrack);
    colors.selection = manager->getColor(PDFThemeColors::BgAccentSubtle);
    colors.selectionText = manager->getColor(PDFThemeColors::TextAccent);
    // magnifier 1234:164735: 1 px border/strong with the shadow 0 / 4 / 16 of effect/shadow
    colors.magnifierBorder = manager->getColor(PDFThemeColors::BorderStrong);
    colors.magnifierShadow = manager->getColor(PDFThemeColors::EffectShadow);
    PDFAppAnnotationController::applyThemeColors(colors);
    proxy->setViewColors(colors);

    // The pages lie on the canvas with a margin and a shadow (O18, 193:1257), the
    // fit zooms keep the margin free
    PDFPageDecoration decoration;
    decoration.margin = PAGE_MARGIN;
    decoration.shadowColor = manager->getColor(PDFThemeColors::EffectShadow);
    decoration.shadowOffset = QPointF(0.0, PAGE_SHADOW_OFFSET_Y);
    decoration.shadowBlur = PAGE_SHADOW_BLUR;
    proxy->setPageDecoration(decoration);
}

void PDFAppDocumentView::onThemeChanged()
{
    updateViewColors();
}

void PDFAppDocumentView::onDrawSpaceChanged()
{
    if (m_isRestoringPageAnchor)
    {
        // The pages are placed again (onDocumentChanged announces the result)
        return;
    }

    const PDFInteger pageIndex = getCurrentPageIndex();
    if (pageIndex != m_lastPageIndex)
    {
        m_lastPageIndex = pageIndex;

        // The next pages are prepared in advance (rendering.prefetchPages)
        PDFDrawWidgetProxy* proxy = getProxy();
        if (m_isPrefetchEnabled && proxy && m_pdfWidget && pageIndex >= 0)
        {
            const std::vector<PDFInteger> pages = m_pdfWidget->getDrawWidget()->getCurrentPages();
            if (!pages.empty())
            {
                proxy->prefetchPages(pages.back());
            }
        }

        // A slot may delete the view (the tab) from the signal
        QPointer<PDFAppDocumentView> guard(this);
        Q_EMIT currentPageChanged(pageIndex);
        if (!guard)
        {
            return;
        }
    }

    // The page for the user (the page item of the status bar): also the selected page
    // of the thumbnails, which has come to the canvas or left it (a slot may delete the view)
    QPointer<PDFAppDocumentView> guard(this);
    updateDisplayedPage();
    if (!guard)
    {
        return;
    }

    if (m_isToolModeActive)
    {
        updateZoomItem();
    }
    Q_EMIT viewChanged();
}

void PDFAppDocumentView::updateStatusMessage()
{
    if (m_statusBar && m_document && m_document->isReady())
    {
        m_statusBar->showMessage(m_isToolModeActive && !m_toolMessage.isEmpty() ? m_toolMessage : tr("Ready"));
    }
}

// ------------------------------------------------------------------ commands

bool PDFAppDocumentView::executeCommand(const QString& commandId)
{
    QPointer<PDFAppDocumentView> guard(this);
    QPointer<PDFAppMainWindow> mainWindow = qobject_cast<PDFAppMainWindow*>(window());

    PDFAppDocumentTab* tab = getTab();
    if (tab && tab->findHandler(commandId))
    {
        // The command may delete the tab and this view
        const bool isExecuted = tab->executeCommand(commandId);
        if (mainWindow)
        {
            mainWindow->updateCommandStates();
        }
        if (guard)
        {
            Q_EMIT commandStatesChanged();
        }
        return isExecuted;
    }

    // A command of the window (Close tab)
    QAction* action = mainWindow ? mainWindow->getCommandAction(commandId) : nullptr;
    if (action && action->isEnabled())
    {
        action->trigger();
        return true;
    }
    return false;
}

void PDFAppDocumentView::requestCommand(const QString& commandId)
{
    QMetaObject::invokeMethod(this, &PDFAppDocumentView::onExecuteQueued, Qt::QueuedConnection, commandId);
}

void PDFAppDocumentView::onExecuteQueued(const QString& commandId)
{
    executeCommand(commandId);
}

bool PDFAppDocumentView::isCommandEnabled(const QString& commandId) const
{
    PDFAppDocumentTab* tab = getTab();
    if (tab && tab->findHandler(commandId))
    {
        return tab->isCommandEnabled(commandId);
    }

    // Commands of the window are enabled always
    const PDFAppMainWindow* mainWindow = qobject_cast<const PDFAppMainWindow*>(window());
    const QAction* action = mainWindow ? mainWindow->getCommandAction(commandId) : nullptr;
    return action && action->isEnabled();
}

bool PDFAppDocumentView::isCommandChecked(const QString& commandId) const
{
    PDFAppDocumentTab* tab = getTab();
    return tab && tab->isCommandChecked(commandId);
}

void PDFAppDocumentView::onTabCommandStatesChanged()
{
    Q_EMIT commandStatesChanged();
}

QAction* PDFAppDocumentView::getCanvasAction(const QString& commandId) const
{
    auto it = m_canvasActions.find(commandId);
    return it != m_canvasActions.end() ? it->second.data() : nullptr;
}

void PDFAppDocumentView::createCanvasActions()
{
    const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
    if (!registry || !m_pdfWidget)
    {
        return;
    }

    // The keys of the Canvas commands (D12; the view and, since step 4, the editing
    // of the annotations): the canvas has them with the context of its children,
    // the fields of the window keep their keys
    for (const QString& commandId : PDFAppDocumentCommandTable::getAllCommands())
    {
        const PDFAppCommand* command = registry->findCommand(commandId);
        if (!command || command->scope != PDFAppCommandScope::Canvas)
        {
            continue;
        }

        QAction* action = new QAction(command->getTitle(), m_pdfWidget);
        action->setObjectName(QStringLiteral("canvas:") + commandId);
        action->setProperty(PDFAppDocumentViewHelper::COMMAND_PROPERTY, commandId);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, &PDFAppDocumentView::onCanvasActionTriggered);
        m_pdfWidget->addAction(action);
        m_canvasActions[commandId] = action;
    }

    updateCanvasShortcuts();
}

void PDFAppDocumentView::updateCanvasShortcuts()
{
    const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
    if (!registry)
    {
        return;
    }

    // The shortcuts of the user (an unreadable database leaves the defaults); the
    // view keeps them for the tooltips of the toolbar and of its panels
    PDFAppCommandRegistry::UserShortcuts userShortcuts;
    if (m_context->getDatabase())
    {
        PDFDatabaseError error;
        userShortcuts = m_context->getShortcuts().getShortcuts(&error);
        if (error.isError())
        {
            userShortcuts.clear();
        }
    }
    m_userShortcuts = std::move(userShortcuts);

    for (const auto& item : m_canvasActions)
    {
        if (item.second)
        {
            item.second->setShortcuts(registry->getShortcuts(item.first, m_userShortcuts));
        }
    }

    if (m_toolbar)
    {
        m_toolbar->updateCommandToolTips();
    }
}

QString PDFAppDocumentView::getCommandToolTip(const QString& commandId, bool isEmptyWithoutShortcut) const
{
    return formatCommandToolTip(m_context ? m_context->getCommandRegistry() : nullptr, commandId, m_userShortcuts, isEmptyWithoutShortcut);
}

QString PDFAppDocumentView::formatCommandToolTip(const PDFAppCommandRegistry* registry, const QString& commandId, const std::map<QString, QString>& userShortcuts, bool isEmptyWithoutShortcut)
{
    const PDFAppCommand* command = registry ? registry->findCommand(commandId) : nullptr;
    if (!command)
    {
        return QString();
    }

    const QString title = command->getTitle();
    const QKeySequence shortcut = registry->getShortcut(commandId, userShortcuts);
    if (shortcut.isEmpty())
    {
        return isEmptyWithoutShortcut ? QString() : title;
    }
    return tr("%1 (%2)").arg(title, shortcut.toString(QKeySequence::NativeText));
}

void PDFAppDocumentView::onCanvasActionTriggered()
{
    // The actions do not identify themselves in the signal
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
    {
        executeCommand(action->property(PDFAppDocumentViewHelper::COMMAND_PROPERTY).toString());
    }
}

void PDFAppDocumentView::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    if (topic == PDFShortcutRepository::getTopic())
    {
        updateCanvasShortcuts();
    }
    else if (topic == PDFSettingsRepository::getTopic())
    {
        scheduleApplySettings();
    }
}

void PDFAppDocumentView::onEverythingChanged()
{
    updateCanvasShortcuts();
    scheduleApplySettings();
}

void PDFAppDocumentView::scheduleApplySettings()
{
    // The notifications of one batch (a preset with its two colors, Restore
    // defaults - a notification per key) are applied once, so the pages are
    // compiled once (step 5)
    if (!m_isApplySettingsQueued)
    {
        m_isApplySettingsQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppDocumentView::onApplySettingsQueued, Qt::QueuedConnection);
    }
}

void PDFAppDocumentView::onApplySettingsQueued()
{
    m_isApplySettingsQueued = false;
    applySettings();
}

// ---------------------------------------------------------------------- view

PDFAppViewLayout PDFAppDocumentView::getViewLayout() const
{
    return m_layout;
}

void PDFAppDocumentView::setViewLayout(PDFAppViewLayout layout)
{
    if (m_layout != layout)
    {
        m_layout = layout;
        applyPageLayout();
        emitViewStateChanged();
    }
}

void PDFAppDocumentView::setFirstPageRight(bool isFirstPageRight)
{
    if (m_isFirstPageRight != isFirstPageRight)
    {
        m_isFirstPageRight = isFirstPageRight;
        applyPageLayout();
        emitViewStateChanged();
    }
}

double PDFAppDocumentView::getZoom() const
{
    const PDFDrawWidgetProxy* proxy = getProxy();
    return proxy ? proxy->getZoom() : 1.0;
}

void PDFAppDocumentView::setZoom(double zoom)
{
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!proxy || !std::isfinite(zoom))
    {
        return;
    }

    proxy->zoom(qBound(PDFDrawWidgetProxy::getMinZoom(), zoom, PDFDrawWidgetProxy::getMaxZoom()));
    Q_EMIT viewChanged();
}

int PDFAppDocumentView::getRotation() const
{
    const PDFDrawWidgetProxy* proxy = getProxy();
    return proxy ? PDFAppDocumentViewHelper::getRotationDegrees(proxy->getPageRotation()) : 0;
}

bool PDFAppDocumentView::isSidebarVisible() const
{
    return m_isSidebarVisible;
}

void PDFAppDocumentView::setSidebarVisible(bool isVisible)
{
    if (m_isSidebarVisible != isVisible)
    {
        m_isSidebarVisible = isVisible;
        updateSidebarVisibility();
        emitViewStateChanged();
    }
}

void PDFAppDocumentView::updateSidebarVisibility()
{
    if (!m_sidebar || !m_splitter)
    {
        return;
    }

    // The left panel of a tool lies in the place of the sidebar
    QWidget* leftWidget = m_sidebar.data();
    if (QWidget* leftPanel = m_modeLayout ? m_modeLayout->getLeftPanelPart() : nullptr)
    {
        leftWidget = leftPanel;
        if (!m_sidebar->isHidden())
        {
            m_sidebar->hide();
        }
    }

    // The sidebar keeps its width (the splitter gives it back)
    const bool isShown = m_isSidebarVisible && !m_isFullScreen;
    if (leftWidget->isHidden() == !isShown)
    {
        return;
    }

    leftWidget->setVisible(isShown);
    if (isShown)
    {
        applySplitterSizes();
    }
}

void PDFAppDocumentView::applySplitterSizes()
{
    if (m_modeLayout)
    {
        m_modeLayout->applySplitterSizes(m_sidebarWidth);
    }
}

void PDFAppDocumentView::onSplitterMoved()
{
    QWidget* leftPanel = m_modeLayout ? m_modeLayout->getLeftPanelPart() : nullptr;
    if (leftPanel && leftPanel->isVisible())
    {
        m_modeLayout->setLeftPanelWidth(leftPanel->width());
        Q_EMIT viewChanged();
    }
    else if (m_sidebar && m_sidebar->isVisible())
    {
        m_sidebarWidth = m_sidebar->width();
        Q_EMIT viewChanged();
    }
}

// ------------------------------------------------------------------ tool mode

bool PDFAppDocumentView::enterToolMode(const PDFAppToolModeParts& parts)
{
    if (m_isToolModeActive || !m_document || !m_document->isReady())
    {
        return false;
    }

    QPointer<PDFAppDocumentView> guard(this);

    // Everything below may run code of the application, which deletes the view or the parts
    const QPointer<QWidget> toolbarPart = parts.toolbar;
    const QPointer<QWidget> leftPanelPart = parts.leftPanel;
    const QPointer<QWidget> canvasPagePart = parts.canvasPage;
    const QPointer<QWidget> rightPanelPart = parts.rightPanel;
    const QPointer<PDFAppToolUndoTarget> undoTargetPart = parts.undoTarget;

    m_wasFullScreen = m_isFullScreen;
    if (m_isFullScreen)
    {
        setFullScreen(false);
        if (!guard)
        {
            return false;
        }
    }

    // The panels of the toolbar and the comment popup do not stay over the tool
    if (m_toolbar)
    {
        if (PDFAppDocumentPanelPopup* panel = m_toolbar->getOpenPanel())
        {
            panel->close();
        }
        if (guard && m_toolbar && m_toolbar->getSelectMenu() && m_toolbar->getSelectMenu()->isVisible())
        {
            m_toolbar->getSelectMenu()->hide();
        }
    }
    if (guard && m_commentPopup)
    {
        m_commentPopup->cancel();
    }

    // F4: the pending edits become undo steps of the document before the mode
    if (guard && m_document)
    {
        m_document->commitPendingEdits();
    }
    if (guard && m_formController)
    {
        m_formController->closeEditor();
    }
    if (guard && m_sidebar)
    {
        if (PDFAppOutlinePanel* outlinePanel = qobject_cast<PDFAppOutlinePanel*>(m_sidebar->findPanel(PDFAppDocumentPanelIds::outline())))
        {
            outlinePanel->finishInlineEditing();
        }
    }
    if (guard && m_outlineEditor && m_outlineEditor->isCreationPending())
    {
        m_outlineEditor->cancelCreation();
    }
    if (guard && m_annotationController)
    {
        m_annotationController->endMergeSession();
    }

    // A slot may have deleted the view or the document, or it may have entered the mode
    if (!guard || !m_document || !m_document->isReady() || m_isToolModeActive)
    {
        return false;
    }

    PDFAppToolModeParts livingParts;
    livingParts.toolbar = toolbarPart.data();
    livingParts.leftPanel = leftPanelPart.data();
    livingParts.canvasPage = canvasPagePart.data();
    livingParts.rightPanel = rightPanelPart.data();
    livingParts.undoTarget = undoTargetPart.data();

    m_isToolModeActive = true;
    m_toolMessage.clear();
    installToolParts(livingParts);

    // The keys of the mode are bound to the view: a focus outside of it or in a hidden part
    // would not reach them
    const QWidget* focusWidget = window()->focusWidget();
    if (isVisible() && (!focusWidget || !isAncestorOf(focusWidget) || !focusWidget->isVisible()))
    {
        QWidget* canvas = m_modeLayout && m_modeLayout->getCanvasPagePart() ? m_modeLayout->getCanvasPagePart() : static_cast<QWidget*>(m_pdfWidget);
        canvas->setFocus(Qt::OtherFocusReason);
    }

    // The annotation controller, the form and the outline editor follow the document:
    // read only, a creation tool ends (a slot may delete the view or leave the mode)
    m_document->setUserChangesSuspended(true);
    if (!guard || !m_isToolModeActive)
    {
        return false;
    }

    updatePageItem();
    updateZoomItem();
    updateStatusMessage();
    layoutInfoBars();

    Q_EMIT toolModeChanged(true);
    if (!guard)
    {
        return false;
    }
    Q_EMIT commandStatesChanged();
    return guard && m_isToolModeActive;
}

void PDFAppDocumentView::leaveToolMode()
{
    if (!m_isToolModeActive)
    {
        return;
    }

    QPointer<PDFAppDocumentView> guard(this);
    m_isToolModeActive = false;
    m_toolMessage.clear();

    if (m_toolUndoTarget)
    {
        m_toolUndoTarget->disconnect(this);
    }
    m_toolUndoTarget = nullptr;
    removeToolParts();

    // The document can be changed again (a slot may delete the view or enter the mode)
    if (m_document)
    {
        m_document->setUserChangesSuspended(false);
        if (!guard || m_isToolModeActive)
        {
            return;
        }
    }

    updatePageItem();
    updateZoomItem();
    updateStatusMessage();
    layoutInfoBars();

    Q_EMIT toolModeChanged(false);
    if (!guard)
    {
        return;
    }
    Q_EMIT commandStatesChanged();
    if (!guard)
    {
        return;
    }

    // The full screen, which the entry has ended
    if (m_wasFullScreen)
    {
        m_wasFullScreen = false;
        setFullScreen(true);
    }
}

PDFAppToolUndoTarget* PDFAppDocumentView::getToolUndoTarget() const
{
    return m_toolUndoTarget.data();
}

void PDFAppDocumentView::setToolMessage(const QString& text)
{
    if (m_isToolModeActive)
    {
        m_toolMessage = text.left(512);
        updateStatusMessage();
    }
}

void PDFAppDocumentView::installToolParts(const PDFAppToolModeParts& parts)
{
    m_hasToolParts = parts.toolbar || parts.leftPanel || parts.canvasPage || parts.rightPanel || parts.undoTarget;

    m_toolUndoTarget = parts.undoTarget;
    if (parts.undoTarget)
    {
        connect(parts.undoTarget, &PDFAppToolUndoTarget::undoStateChanged, this, &PDFAppDocumentView::onToolUndoStateChanged);
        connect(parts.undoTarget, &QObject::destroyed, this, &PDFAppDocumentView::onToolPartDestroyed);
    }

    if (m_modeLayout)
    {
        m_modeLayout->installParts(parts);
    }
    if (m_statusBar)
    {
        // The Go to panel opens below the toolbar of the document
        m_statusBar->setPageItemInteractive(!isToolToolbarShown());
    }

    if (isToolLeftPanelShown())
    {
        m_hadToolLeftPanel = true;
        m_wasSidebarVisible = m_isSidebarVisible;
        m_isSidebarVisible = true;
        updateSidebarVisibility();
    }

    if (layout())
    {
        layout()->activate();
    }
    applySplitterSizes();
}

void PDFAppDocumentView::removeToolParts()
{
    const bool isFocusInPart = m_modeLayout && m_modeLayout->removeParts();
    if (m_statusBar)
    {
        m_statusBar->setPageItemInteractive(true);
    }

    if (m_hadToolLeftPanel)
    {
        m_hadToolLeftPanel = false;
        m_isSidebarVisible = m_wasSidebarVisible;
    }
    updateSidebarVisibility();

    if (layout())
    {
        layout()->activate();
    }
    applySplitterSizes();

    if (isFocusInPart && m_pdfWidget)
    {
        m_pdfWidget->setFocus(Qt::OtherFocusReason);
    }
}

PDFAppToolModeLayout* PDFAppDocumentView::getModeLayout() const
{
    return m_modeLayout.data();
}

bool PDFAppDocumentView::isToolToolbarShown() const
{
    return m_modeLayout && m_modeLayout->getToolbarPart();
}

bool PDFAppDocumentView::isToolLeftPanelShown() const
{
    return m_modeLayout && m_modeLayout->getLeftPanelPart();
}

bool PDFAppDocumentView::isToolCanvasPageShown() const
{
    return m_modeLayout && m_modeLayout->getCanvasPagePart();
}

bool PDFAppDocumentView::isToolPartAlive() const
{
    return m_toolUndoTarget || (m_modeLayout && m_modeLayout->isPartAlive());
}

void PDFAppDocumentView::onDocumentLockChanged()
{
    if (m_isToolModeActive && m_document)
    {
        Q_EMIT toolModeLockChanged(m_document->isLocked());
    }
}

void PDFAppDocumentView::onToolPartDestroyed()
{
    // The part may be inside of its destructor (it is no longer of its class and the
    // QPointer of a widget is not cleared yet): only the fallback is queued
    if (!m_isToolPartsUpdateQueued)
    {
        m_isToolPartsUpdateQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppDocumentView::onToolPartsChangedQueued, Qt::QueuedConnection);
    }
}

void PDFAppDocumentView::onToolPartsChangedQueued()
{
    m_isToolPartsUpdateQueued = false;
    if (!m_isToolModeActive)
    {
        return;
    }

    // Nothing of the tool is left: the mode ends
    if (m_hasToolParts && !isToolPartAlive())
    {
        leaveToolMode();
        return;
    }

    // A deleted part leaves the part of the view in its place: QStackedWidget, QStackedLayout
    // and the layout of the body have dropped it, the sidebar comes back
    if (m_hadToolLeftPanel && !isToolLeftPanelShown())
    {
        m_hadToolLeftPanel = false;
        m_isSidebarVisible = m_wasSidebarVisible;
        updateSidebarVisibility();
        applySplitterSizes();
    }
    if (m_statusBar)
    {
        m_statusBar->setPageItemInteractive(!isToolToolbarShown());
    }
    if (!isToolCanvasPageShown() && m_pdfWidget)
    {
        // QStackedLayout has raised the widget it shows instead of the deleted page
        m_pdfWidget->lower();
    }
    layoutInfoBars();

    QPointer<PDFAppDocumentView> guard(this);
    Q_EMIT toolUndoStateChanged();
    if (guard)
    {
        Q_EMIT commandStatesChanged();
    }
}

void PDFAppDocumentView::onToolUndoStateChanged()
{
    Q_EMIT toolUndoStateChanged();
}

void PDFAppDocumentView::setFullScreen(bool isFullScreen)
{
    if (m_isFullScreen == isFullScreen)
    {
        return;
    }

    PDFAppMainWindow* mainWindow = qobject_cast<PDFAppMainWindow*>(window());
    if (isFullScreen && (!mainWindow || !isVisible()))
    {
        // Only a shown view of a window covers the screen
        return;
    }

    m_isFullScreen = isFullScreen;
    if (m_toolbarStack)
    {
        m_toolbarStack->setVisible(!isFullScreen);
    }
    if (m_toolbar)
    {
        m_toolbar->setVisible(!isFullScreen);
    }
    if (m_statusBar)
    {
        m_statusBar->setVisible(!isFullScreen);
    }
    updateSidebarVisibility();
    if (m_leaveFullScreenAction)
    {
        m_leaveFullScreenAction->setEnabled(isFullScreen);
    }

    QPointer<PDFAppDocumentView> guard(this);
    if (mainWindow)
    {
        mainWindow->setFullScreenMode(isFullScreen);
    }
    if (!guard)
    {
        return;
    }

    if (isFullScreen && m_pdfWidget)
    {
        m_pdfWidget->setFocus(Qt::OtherFocusReason);
    }

    layoutInfoBars();
    emitViewStateChanged();
}

void PDFAppDocumentView::leaveFullScreen()
{
    setFullScreen(false);
}

PDFWidgetTool* PDFAppDocumentView::getActiveTool() const
{
    return m_toolManager ? m_toolManager->getActiveTool() : nullptr;
}

void PDFAppDocumentView::setCanvasTool(int tool)
{
    if (!m_toolManager || tool < 0 || tool > PDFToolManager::ToolEnd)
    {
        return;
    }

    const PDFToolManager::PredefinedTools predefinedTool = static_cast<PDFToolManager::PredefinedTools>(tool);
    const bool isMode = predefinedTool == PDFToolManager::ToolEnd || predefinedTool == PDFToolManager::SelectTextTool;
    if (!isMode && isCanvasToolActive(tool))
    {
        // The active tool is turned off: the chosen mode (Select text or Hand) comes back
        activateDefaultCanvasTool();
        return;
    }

    if (predefinedTool == PDFToolManager::ToolEnd || m_toolManager->getPredefinedTool(predefinedTool))
    {
        if (isMode)
        {
            m_isHandMode = predefinedTool == PDFToolManager::ToolEnd;
        }

        m_isSelectAllPending = false;
        m_toolManager->setActiveTool(predefinedTool);
    }
}

bool PDFAppDocumentView::isCanvasToolActive(int tool) const
{
    if (!m_toolManager || tool < 0 || tool > PDFToolManager::ToolEnd)
    {
        return false;
    }

    if (tool == PDFToolManager::ToolEnd)
    {
        return m_isHandMode && !m_toolManager->getActiveTool();
    }

    const PDFWidgetTool* predefinedTool = m_toolManager->getPredefinedTool(static_cast<PDFToolManager::PredefinedTools>(tool));
    return predefinedTool && predefinedTool->isActive();
}

void PDFAppDocumentView::activateDefaultCanvasTool()
{
    if (m_toolManager)
    {
        m_toolManager->setActiveTool(m_isHandMode ? PDFToolManager::ToolEnd : PDFToolManager::SelectTextTool);
    }
}

bool PDFAppDocumentView::hasTextSelection() const
{
    const PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
    return selectTextTool && selectTextTool->isActive() && selectTextTool->hasSelection();
}

void PDFAppDocumentView::copySelectedText()
{
    // Bit 5 of Table 22 (the shared check): a restricted document asks for the owner
    // password, the user copies again after the unlock
    PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
    if (!selectTextTool || !selectTextTool->hasSelection() ||
        !PDFAppDocumentPermissions::request(getDocument(), PDFAppPermissionRequest::create(PDFAppPermissionAction::CopyContent), this))
    {
        return;
    }
    selectTextTool->copyText();
}

void PDFAppDocumentView::selectAllText()
{
    PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!selectTextTool || !proxy || getPageCount() <= 0)
    {
        return;
    }

    // The tool makes the text layout of the pages in a worker thread, when it is
    // activated; Select all of the text chooses the mode Select text (also from Hand)
    if (!selectTextTool->isActive())
    {
        // A slot of the change of the active tool may delete the view (the tab)
        QPointer<PDFAppDocumentView> self(this);
        QPointer<PDFSelectTextTool> selectTextToolGuard(selectTextTool);
        m_isHandMode = false;
        m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
        if (!self || !selectTextToolGuard || !m_toolManager || !selectTextTool->isActive())
        {
            return;
        }
        proxy = getProxy();
        if (!proxy)
        {
            return;
        }
    }

    if (proxy->getTextLayoutCompiler()->isTextLayoutReady())
    {
        m_isSelectAllPending = false;
        selectTextTool->selectAll();
    }
    else
    {
        m_isSelectAllPending = true;
    }
}

void PDFAppDocumentView::deselectText()
{
    m_isSelectAllPending = false;
    if (PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr)
    {
        selectTextTool->deselect();
    }
}

void PDFAppDocumentView::setViewSetting(const QString& key, const QVariant& value)
{
    if (!m_context)
    {
        return;
    }

    // A refused value (busy or read-only database) keeps the previous one
    const PDFDatabaseError error = m_context->getSettings().setValue(key, value);
    if (error.isError())
    {
        onToolMessage(tr("The setting cannot be stored: %1").arg(error.toString()), 5000);
    }
    applySettings();
}

void PDFAppDocumentView::onActiveToolChanged(PDFWidgetTool* tool)
{
    // Esc of a tool of the Select menu turns it off without another tool, a change of
    // the whole document turns Select text off: the chosen mode comes back (from the
    // event loop - the engine is in its key handler or in the change of the document)
    if (!tool && !m_isHandMode)
    {
        QMetaObject::invokeMethod(this, &PDFAppDocumentView::onDefaultCanvasToolQueued, Qt::QueuedConnection);
    }
    emitViewStateChanged();
}

void PDFAppDocumentView::onDefaultCanvasToolQueued()
{
    // A pick of a target (the tool manager activates its pick tool right after
    // turning the active tool off) or the Hand mode chosen meanwhile needs nothing
    if (m_toolManager && !m_toolManager->getActiveTool() && !m_isHandMode && getPageCount() > 0)
    {
        activateDefaultCanvasTool();
    }
}

void PDFAppDocumentView::emitViewStateChanged()
{
    // A slot of the first signal may delete the view (the tab)
    QPointer<PDFAppDocumentView> self(this);
    Q_EMIT viewChanged();
    if (self)
    {
        Q_EMIT viewStateChanged();
    }
}

void PDFAppDocumentView::onToolMessage(const QString& text, int timeout)
{
    // Messages of the tools (Screenshot copied, ...) in the status bar
    if (m_statusBar)
    {
        m_statusBar->showMessage(text.left(512), qBound(0, timeout, 60000));
    }
}

void PDFAppDocumentView::onTextLayoutChanged()
{
    // Select all waits for the text layout of the pages (made in a worker thread)
    if (m_isSelectAllPending)
    {
        PDFDrawWidgetProxy* proxy = getProxy();
        PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
        if (proxy && proxy->getTextLayoutCompiler()->isTextLayoutReady() && selectTextTool && selectTextTool->isActive())
        {
            m_isSelectAllPending = false;
            selectTextTool->selectAll();
        }
    }
    Q_EMIT viewStateChanged();
}

void PDFAppDocumentView::openGoToPage()
{
    if (!m_toolbar || m_isFullScreen)
    {
        return;
    }

    if (PDFAppDocumentGoToPanel* panel = qobject_cast<PDFAppDocumentGoToPanel*>(m_toolbar->openPanel(PDFAppDocumentToolbar::Panel::GoTo)))
    {
        panel->focusPageField();
    }
}

void PDFAppDocumentView::openSettings(const QString& section)
{
    QMetaObject::invokeMethod(this, &PDFAppDocumentView::onOpenSettingsQueued, Qt::QueuedConnection, section);
}

void PDFAppDocumentView::onOpenSettingsQueued(const QString& section)
{
    PDFAppMainWindow* mainWindow = qobject_cast<PDFAppMainWindow*>(window());
    if (mainWindow && mainWindow->getNavigator())
    {
        mainWindow->getNavigator()->navigate(PDFAppRoutes::settings(section));
    }
}

PDFAppViewState PDFAppDocumentView::getViewState() const
{
    PDFAppViewState state;
    state.layout = m_layout;
    state.firstPageRight = m_isFirstPageRight;
    state.rotation = getRotation();

    // The layout of an open tool is not the state of the view (Recent, Back): the left panel
    // of the tool lies in the place of the sidebar, which the user may have hidden
    state.sidebarVisible = m_hadToolLeftPanel ? m_wasSidebarVisible : m_isSidebarVisible;
    state.sidebarWidth =qBound(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, m_sidebarWidth, PDFAppViewState::MAXIMUM_SIDEBAR_WIDTH);

    const QString panelId = getCurrentPanelId();
    if (PDFAppViewState::isValidPanelId(panelId))
    {
        state.panelId = panelId;
    }

    const PDFDrawWidgetProxy* proxy = getProxy();
    const std::optional<PDFDrawWidgetProxy::ZoomHint> fit = proxy ? proxy->getZoomFitHint() : std::nullopt;
    if (!fit)
    {
        state.zoomMode = PDFAppViewZoomMode::Custom;
        state.zoom = qBound(PDFAppViewState::MINIMUM_ZOOM, getZoom(), PDFAppViewState::MAXIMUM_ZOOM);
    }
    else
    {
        switch (*fit)
        {
            case PDFDrawWidgetProxy::ZoomHint::Fit:
                state.zoomMode = PDFAppViewZoomMode::FitPage;
                break;
            case PDFDrawWidgetProxy::ZoomHint::FitWidth:
                state.zoomMode = PDFAppViewZoomMode::FitWidth;
                break;
            case PDFDrawWidgetProxy::ZoomHint::FitHeight:
                state.zoomMode = PDFAppViewZoomMode::FitHeight;
                break;
        }
    }
    return state;
}

void PDFAppDocumentView::applyViewState(const PDFAppViewState& state)
{
    // Every value is applied only when it is set (the text of the database is
    // untrusted, PDFAppViewState::fromText has validated it)
    if (state.layout)
    {
        m_layout = *state.layout;
    }
    if (state.firstPageRight)
    {
        m_isFirstPageRight = *state.firstPageRight;
    }
    applyPageLayout();

    PDFDrawWidgetProxy* proxy = getProxy();
    if (proxy && state.rotation && *state.rotation % 90 == 0 && *state.rotation >= 0 && *state.rotation < 360)
    {
        // The proxy rotates by steps of 90 degrees
        for (int steps = ((*state.rotation - getRotation()) / 90 + 4) % 4; steps > 0; --steps)
        {
            proxy->performOperation(PDFDrawWidgetProxy::RotateRight);
        }
    }

    if (proxy && state.zoomMode)
    {
        switch (*state.zoomMode)
        {
            case PDFAppViewZoomMode::FitPage:
                proxy->performOperation(PDFDrawWidgetProxy::ZoomFit);
                break;
            case PDFAppViewZoomMode::FitWidth:
                proxy->performOperation(PDFDrawWidgetProxy::ZoomFitWidth);
                break;
            case PDFAppViewZoomMode::FitHeight:
                proxy->performOperation(PDFDrawWidgetProxy::ZoomFitHeight);
                break;
            case PDFAppViewZoomMode::Custom:
                if (state.zoom)
                {
                    setZoom(*state.zoom);
                }
                break;
        }
    }

    if (state.sidebarWidth)
    {
        m_sidebarWidth = qBound(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, *state.sidebarWidth, PDFAppViewState::MAXIMUM_SIDEBAR_WIDTH);
        if (m_sidebar && m_sidebar->isVisible() && m_splitter && m_splitter->width() > 0)
        {
            const int sidebarWidth = qMin(m_sidebarWidth, qMax(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, m_splitter->width() / 2));
            m_splitter->setSizes({ sidebarWidth, qMax(0, m_splitter->width() - sidebarWidth - m_splitter->handleWidth()) });
        }
    }
    if (state.sidebarVisible)
    {
        // Below the left panel of a tool: the sidebar, which comes back at the end of the mode
        if (m_hadToolLeftPanel)
        {
            m_wasSidebarVisible = *state.sidebarVisible;
        }
        else
        {
            setSidebarVisible(*state.sidebarVisible);
        }
    }
    if (state.panelId)
    {
        // An unknown panel is ignored
        if (m_sidebar)
        {
            m_sidebar->showPanel(*state.panelId);
        }
    }

    emitViewStateChanged();
}

// ------------------------------------------------------------------ settings

void PDFAppDocumentView::applySettings()
{
    if (m_isApplyingSettings || !m_pdfWidget || !m_context)
    {
        return;
    }

    m_isApplyingSettings = true;
    using H = PDFAppDocumentViewHelper;
    using K = PDFSettingKeys;
    const PDFSettingsRepository settings = m_context->getSettings();
    PDFDrawWidgetProxy* proxy = getProxy();

    // Features of the renderer with the developer overlays of package F: all of
    // them are set at once, so a change of the settings compiles the pages once
    // (PDFAppDeveloperOverlay finds them set already)
    PDFRenderer::Features features = PDFAppDeveloperOverlay::readRequestedFeatures(settings) & H::getDeveloperFeatures();
    features.setFlag(PDFRenderer::Antialiasing, settings.getBool(K::renderingAntialiasing()));
    features.setFlag(PDFRenderer::TextAntialiasing, settings.getBool(K::renderingTextAntialiasing()));
    features.setFlag(PDFRenderer::SmoothImages, settings.getBool(K::renderingSmoothImages()));
    features.setFlag(PDFRenderer::DisplayAnnotations, settings.getBool(K::viewingShowAnnotations()));
    features.setFlag(PDFRenderer::ColorAdjust_Annotations, settings.getBool(K::renderingColorAdjustAnnotations()));
    features.setFlag(PDFRenderer::ClipToCropBox, settings.getBool(K::renderingClipToCropBox()));
    features.setFlag(PDFRenderer::IgnoreOptionalContent, settings.getBool(K::renderingIgnoreOptionalContent()));
    features.setFlag(PDFRenderer::LogicalSizeZooming, settings.getBool(K::renderingLogicalPixelZoom()));
    m_colorMode = settings.getString(K::viewingColorMode());
    m_isShowingAnnotations = settings.getBool(K::viewingShowAnnotations());
    features |= H::getColorModeFeature(m_colorMode);

    // Color management and the colors of the color modes
    PDFCMSSettings cmsSettings = m_cmsManager->getDefaultSettings();
    cmsSettings.system = H::getSystem(settings.getString(K::cmsSystem()));
    cmsSettings.intent = H::getIntent(settings.getString(K::cmsRenderingIntent()));
    cmsSettings.accuracy = H::getAccuracy(settings.getString(K::cmsAccuracy()));
    cmsSettings.colorAdaptationXYZ = H::getColorAdaptation(settings.getString(K::cmsColorAdaptation()));
    cmsSettings.isBlackPointCompensationActive = settings.getBool(K::cmsBlackPointCompensation());
    cmsSettings.isWhitePaperColorTransformed = settings.getBool(K::cmsWhitePaperTransformed());
    cmsSettings.isConsiderOutputIntent = settings.getBool(K::cmsConsiderOutputIntent());

    const std::pair<QString, QString*> profiles[] =
    {
        { K::cmsOutputProfile(), &cmsSettings.outputCS },
        { K::cmsDeviceGrayProfile(), &cmsSettings.deviceGray },
        { K::cmsDeviceRgbProfile(), &cmsSettings.deviceRGB },
        { K::cmsDeviceCmykProfile(), &cmsSettings.deviceCMYK },
        { K::cmsProfileDirectory(), &cmsSettings.profileDirectory }
    };
    for (const std::pair<QString, QString*>& profile : profiles)
    {
        // An empty value is the built-in default
        const QString value = settings.getString(profile.first);
        if (!value.isEmpty())
        {
            *profile.second = value;
        }
    }

    // The color management reads every profile of the folder in this thread without
    // limits (SECURITY_TODO.md): a network folder, a folder of thousands of files or
    // of huge files is not given to it (step 5, package B)
    if (!PDFAppColorProfiles::isDirectoryUsable(cmsSettings.profileDirectory))
    {
        cmsSettings.profileDirectory.clear();
    }

    cmsSettings.foregroundColor = H::getColor(settings.getString(K::colorModesTextColor()), QColor(0xFF, 0xB0, 0x00));
    cmsSettings.backgroundColor = H::getColor(settings.getString(K::colorModesPageColor()), QColor(0x0B, 0x0B, 0x10));
    cmsSettings.bitonalThreshold = int(qBound(qint64(0), settings.getInteger(K::colorModesBitonalThreshold()), qint64(255)));
    cmsSettings.sigmoidSlopeFactor = qBound(1.0, settings.getReal(K::colorModesHighContrastStrength()), 100.0);
    if (!(m_cmsManager->getSettings() == cmsSettings))
    {
        m_cmsManager->setSettings(cmsSettings);
    }

    if (features != proxy->getFeatures())
    {
        proxy->setFeatures(features);
    }
    m_annotationManager->setFeatures(features);

    // Engine, caches, shading and the wheel - only a changed value is applied again
    AppliedSettings applied;
    applied.rendererEngine = int(H::getRendererEngine(settings.getString(K::renderingEngine())));
    applied.compiledPageCacheKB = settings.getInteger(K::cacheCompiledPages());
    applied.thumbnailCacheKB = settings.getInteger(K::cacheThumbnails());
    applied.fontCacheLimit = settings.getInteger(K::cacheFontLimit());
    applied.instancedFontCacheLimit = settings.getInteger(K::cacheInstancedFontLimit());
    applied.meshPreferredRatio = settings.getReal(K::renderingMeshPreferredRatio());
    applied.meshMinimalRatio = settings.getReal(K::renderingMeshMinimalRatio());
    applied.colorTolerance = settings.getReal(K::renderingColorTolerance());
    applied.smoothWheelScrolling = settings.getBool(K::viewingSmoothWheelScrolling());
    applied.wheelSpeedHorizontal = settings.getInteger(K::viewingWheelSpeedHorizontal());
    applied.wheelSpeedVertical = settings.getInteger(K::viewingWheelSpeedVertical());

    if (applied.rendererEngine != m_appliedSettings.rendererEngine)
    {
        m_pdfWidget->updateRenderer(static_cast<RendererEngine>(applied.rendererEngine));
    }
    if (applied.compiledPageCacheKB != m_appliedSettings.compiledPageCacheKB || applied.thumbnailCacheKB != m_appliedSettings.thumbnailCacheKB ||
        applied.fontCacheLimit != m_appliedSettings.fontCacheLimit || applied.instancedFontCacheLimit != m_appliedSettings.instancedFontCacheLimit)
    {
        m_pdfWidget->updateCacheLimits(qsizetype(applied.compiledPageCacheKB) * 1024, int(qBound(qint64(0), applied.thumbnailCacheKB, qint64(1048576))),
                                       int(qBound(qint64(1), applied.fontCacheLimit, qint64(100000))), int(qBound(qint64(1), applied.instancedFontCacheLimit, qint64(100000))));
    }
    if (applied.meshPreferredRatio != m_appliedSettings.meshPreferredRatio)
    {
        proxy->setPreferredMeshResolutionRatio(applied.meshPreferredRatio);
    }
    if (applied.meshMinimalRatio != m_appliedSettings.meshMinimalRatio)
    {
        proxy->setMinimalMeshResolutionRatio(applied.meshMinimalRatio);
    }
    if (applied.colorTolerance != m_appliedSettings.colorTolerance)
    {
        proxy->setColorTolerance(applied.colorTolerance);
    }
    if (!(applied == m_appliedSettings))
    {
        m_annotationManager->setMeshQualitySettings(proxy->getMeshQualitySettings());
    }
    m_pdfWidget->setSmoothWheelScrolling(applied.smoothWheelScrolling);
    m_pdfWidget->setWheelScrollSpeed(int(applied.wheelSpeedHorizontal), int(applied.wheelSpeedVertical));
    m_appliedSettings = applied;

    // The strategy of the threads is the one of the process
    PDFExecutionPolicy::setStrategy(H::getStrategy(settings.getString(K::renderingMultithreading())));
    m_isPrefetchEnabled = settings.getBool(K::renderingPrefetchPages());

    // Magnifier (Settings › Viewing › Magnifier)
    if (PDFMagnifierTool* magnifier = m_toolManager->getMagnifierTool())
    {
        // The setting is the diameter of the circle (magnifier 1234:164735 - 100 px),
        // the tool takes its radius
        // TODO(remove-old-apps): the tool takes the diameter, when the old Editor (its
        // own setting is the radius) is gone
        magnifier->setMagnifierSize(getMagnifierRadius(int(settings.getInteger(K::magnifierSize()))));
        magnifier->setMagnifierZoom(settings.getReal(K::magnifierZoom()));
    }

    // Forms (Settings › Editing › Forms) - the form controller of package D of step 4
    // keeps the read-only form of a locked document
    if (m_formController)
    {
        m_formController->applySettings(settings);
    }

    m_isApplyingSettings = false;
    emitViewStateChanged();
}

// -------------------------------------------------------------------- events

void PDFAppDocumentView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutInfoBars();
}

void PDFAppDocumentView::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    // Another tab is shown in the window: the full screen ends with this view
    const QWidget* topLevel = window();
    if (m_isFullScreen && !event->spontaneous() && topLevel && topLevel != this && topLevel->isVisible())
    {
        setFullScreen(false);
    }
}

bool PDFAppDocumentView::eventFilter(QObject* watched, QEvent* event)
{
    // The canvas area (its info bars, bg/track) is watched by the layout of the places
    if (m_isToolModeActive && watched == m_splitter && event->type() == QEvent::Resize)
    {
        // The right panel of a tool leaves less to the splitter: the left panel gives
        // its width up in a narrow window, as the sidebar of a shown view
        applySplitterSizes();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppDocumentView::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        // The canvas actions have the titles of their commands
        const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
        for (const auto& item : m_canvasActions)
        {
            const PDFAppCommand* command = registry ? registry->findCommand(item.first) : nullptr;
            if (item.second && command)
            {
                item.second->setText(command->getTitle());
            }
        }

        m_lastPageIndex = -2;
        m_lastDisplayedPageIndex = -2;
        onDrawSpaceChanged();
        updateStatusMessage();
    }
}

PDFAppContext* PDFAppDocumentView::getContext() const
{
    return m_context.data();
}

PDFAppDocument* PDFAppDocumentView::getDocument() const
{
    return m_document.data();
}

PDFAppDocumentToolbar* PDFAppDocumentView::getToolbar() const
{
    return m_toolbar.data();
}

PDFAppDocumentSidebar* PDFAppDocumentView::getSidebar() const
{
    return m_sidebar.data();
}

PDFAppDocumentStatusBar* PDFAppDocumentView::getStatusBar() const
{
    return m_statusBar.data();
}

QSplitter* PDFAppDocumentView::getSplitter() const
{
    return m_splitter.data();
}

}   // namespace pdf

#include "moc_pdfappdocumentview.cpp"
