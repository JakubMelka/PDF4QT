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
#include "pdfapplocale.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdeveloperoverlay.h"
#include "pdfappdocument.h"
#include "pdfappdocumentcommands.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappmainwindow.h"
#include "pdfapproute.h"
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
#include <QHideEvent>
#include <QLocale>
#include <QPainter>
#include <QSplitter>
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

PDFAppDocumentView::PDFAppDocumentView(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("documentView"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toolbar = new PDFAppDocumentToolbar(this, this);
    layout->addWidget(m_toolbar);

    // document-body 193:882: the sidebar, the splitter (6 px, 266:7854 - bg/sidebar
    // with the grip, PDFAppDocumentSplitterHandle) and the canvas
    m_splitter = new PDFAppDocumentSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("documentSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    layout->addWidget(m_splitter, 1);

    m_sidebar = new PDFAppDocumentSidebar(this, m_splitter);
    m_sidebar->setMinimumWidth(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH);
    m_splitter->addWidget(m_sidebar);
    connect(m_sidebar, &PDFAppDocumentSidebar::currentPanelChanged, this, &PDFAppDocumentView::panelChanged);
    connect(m_splitter, &QSplitter::splitterMoved, this, &PDFAppDocumentView::onSplitterMoved);

    // The canvas; the info bars lie over it (children of the area, placed by layoutInfoBars)
    m_canvasArea = new QWidget(m_splitter);
    m_canvasArea->setObjectName(QStringLiteral("canvasArea"));
    m_canvasArea->installEventFilter(this);
    QVBoxLayout* canvasLayout = new QVBoxLayout(m_canvasArea);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);

    m_cmsManager = new PDFCMSManager(this);
    m_progress = new PDFProgress(this);

    m_pdfWidget = new PDFWidget(m_cmsManager, RendererEngine::QPainter, m_canvasArea);
    m_pdfWidget->setObjectName(QStringLiteral("canvas"));
    canvasLayout->addWidget(m_pdfWidget, 1);
    m_splitter->addWidget(m_canvasArea);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
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
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppDocumentView::onThemeChanged);

    m_statusBar = new PDFAppDocumentStatusBar(this);
    connect(m_statusBar, &PDFAppDocumentStatusBar::pageItemClicked, this, &PDFAppDocumentView::openGoToPage);
    layout->addWidget(m_statusBar);

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
        connect(document, &PDFAppDocument::signatureResultsChanged, this, &PDFAppDocumentView::onSignatureResultsChanged);
        connect(document, &QObject::destroyed, this, &PDFAppDocumentView::onDocumentDestroyed);
    }

    if (m_sidebar)
    {
        m_sidebar->setDocument(document);
    }
    applyDocument();
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

    QPointer<PDFAppDocumentView> guard(this);
    m_cmsManager->setDocument(modified.getDocument());
    m_pdfWidget->setDocument(modified, std::move(signatures));
    m_annotationManager->setDocument(modified);
    m_formManager->setDocument(modified);
    m_toolManager->setDocument(modified);
    if (!guard)
    {
        return;
    }

    // The selected pages of a document with fewer pages
    PDFAppDocumentViewHelper::removePagesOutOfRange(m_selectedPages, getPageCount());

    if (modified.getDocument() && !m_isDefaultViewApplied)
    {
        applyDefaultView();
    }

    m_lastPageIndex = -2;
    onDrawSpaceChanged();
    updateStatusMessage();
    Q_EMIT viewChanged();
    Q_EMIT commandStatesChanged();
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

void PDFAppDocumentView::onDocumentDestroyed()
{
    // The document is not whole any more: only the view is cleared
    m_document = nullptr;
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

    // Select text is the tool of the canvas (Select menu 198:5517)
    if (m_toolManager)
    {
        m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
    }
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
    if (!m_pdfWidget || getPageCount() <= 0)
    {
        return -1;
    }

    const std::vector<PDFInteger> pages = m_pdfWidget->getDrawWidget()->getCurrentPages();
    return pages.empty() ? -1 : pages.front();
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

void PDFAppDocumentView::setSelectedPages(std::vector<PDFInteger> pages)
{
    PDFAppDocumentViewHelper::removePagesOutOfRange(pages, getPageCount());
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

    if (m_selectedPages != pages)
    {
        m_selectedPages = std::move(pages);
        Q_EMIT selectedPagesChanged();
    }
}

bool PDFAppDocumentView::showPanel(const QString& panelId)
{
    if (!m_sidebar || !m_sidebar->showPanel(panelId))
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
    if (request.id.isEmpty() || !m_canvasArea)
    {
        return;
    }

    for (size_t i = 0; i < m_infoBars.size(); ++i)
    {
        if (m_infoBars[i].id == request.id)
        {
            m_infoBars[i] = request;
            if (m_infoBarWidgets[i])
            {
                m_infoBarWidgets[i]->setRequest(request);
            }
            layoutInfoBars();
            return;
        }
    }

    PDFAppInfoBar* infoBar = new PDFAppInfoBar(request, m_canvasArea);
    connect(infoBar, &PDFAppInfoBar::actionTriggered, this, &PDFAppDocumentView::onInfoBarAction);
    connect(infoBar, &PDFAppInfoBar::closeRequested, this, &PDFAppDocumentView::onInfoBarClosed);
    m_infoBars.push_back(request);
    m_infoBarWidgets.emplace_back(infoBar);
    infoBar->show();
    layoutInfoBars();
}

void PDFAppDocumentView::hideInfoBar(const QString& id)
{
    for (size_t i = 0; i < m_infoBars.size(); ++i)
    {
        if (m_infoBars[i].id == id)
        {
            // The bar may be the sender of the signal, which hides it
            if (PDFAppInfoBar* infoBar = m_infoBarWidgets[i].data())
            {
                infoBar->disconnect(this);
                infoBar->hide();
                infoBar->deleteLater();
            }
            m_infoBars.erase(m_infoBars.begin() + i);
            m_infoBarWidgets.erase(m_infoBarWidgets.begin() + i);
            layoutInfoBars();
            return;
        }
    }
}

QStringList PDFAppDocumentView::getInfoBarIds() const
{
    QStringList ids;
    for (const PDFAppInfoBarRequest& infoBar : m_infoBars)
    {
        ids << infoBar.id;
    }
    return ids;
}

const PDFAppInfoBarRequest* PDFAppDocumentView::findInfoBar(const QString& id) const
{
    for (const PDFAppInfoBarRequest& infoBar : m_infoBars)
    {
        if (infoBar.id == id)
        {
            return &infoBar;
        }
    }
    return nullptr;
}

PDFAppInfoBar* PDFAppDocumentView::findInfoBarWidget(const QString& id) const
{
    for (size_t i = 0; i < m_infoBars.size(); ++i)
    {
        if (m_infoBars[i].id == id)
        {
            return m_infoBarWidgets[i].data();
        }
    }
    return nullptr;
}

void PDFAppDocumentView::onInfoBarAction(const QString& infoBarId, const QString& actionId)
{
    Q_EMIT infoBarActionTriggered(infoBarId, actionId);
}

void PDFAppDocumentView::onInfoBarClosed(const QString& infoBarId)
{
    QPointer<PDFAppDocumentView> guard(this);
    hideInfoBar(infoBarId);
    if (guard)
    {
        Q_EMIT infoBarActionTriggered(infoBarId, QString());
    }
}

void PDFAppDocumentView::layoutInfoBars()
{
    if (!m_canvasArea || !m_pdfWidget)
    {
        return;
    }

    // Top centered over the canvas, 12 px below the toolbar, 8 px apart (1028:24277)
    const QRect area = m_pdfWidget->geometry();
    const QMargins shadow = PDFAppInfoBar::getShadowMargins();
    const int maximumCardWidth = qMax(0, area.width() - 2 * INFO_BAR_TOP_GAP);
    int cardTop = area.top() + INFO_BAR_TOP_GAP;

    for (const QPointer<PDFAppInfoBar>& infoBar : m_infoBarWidgets)
    {
        if (!infoBar)
        {
            continue;
        }

        const int cardWidth = qMin(infoBar->sizeHint().width() - shadow.left() - shadow.right(), maximumCardWidth);
        const int cardLeft = area.left() + (area.width() - cardWidth) / 2;
        infoBar->setGeometry(QRect(cardLeft - shadow.left(), cardTop - shadow.top(), cardWidth + shadow.left() + shadow.right(), infoBar->height()));
        infoBar->raise();
        cardTop += PDFAppInfoBar::CARD_HEIGHT + INFO_BAR_GAP;
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

        if (m_statusBar)
        {
            const bool isPageItemShown = pageIndex >= 0 && hasPageLabels();
            m_statusBar->setPageText(isPageItemShown ? tr("Page %1 (%2) of %3").arg(PDFAppLocale::formatNumber(pageIndex + 1), getPageLabel(pageIndex), PDFAppLocale::formatNumber(getPageCount()))
                                                     : QString());
        }

        Q_EMIT currentPageChanged(pageIndex);
    }

    Q_EMIT viewChanged();
}

void PDFAppDocumentView::updateStatusMessage()
{
    if (m_statusBar && m_document && m_document->isReady())
    {
        m_statusBar->showMessage(tr("Ready"));
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

    // The keys of the Canvas commands of the view (D12): the canvas has them with
    // the context of its children, the fields of the window keep their keys
    for (const QString& commandId : PDFAppDocumentCommandTable::getCommands(PDFAppDocumentPackage::View))
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

    // The shortcuts of the user (an unreadable database leaves the defaults)
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

    for (const auto& item : m_canvasActions)
    {
        if (item.second)
        {
            item.second->setShortcut(registry->getShortcut(item.first, userShortcuts));
        }
    }
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
        applySettings();
    }
}

void PDFAppDocumentView::onEverythingChanged()
{
    updateCanvasShortcuts();
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
        Q_EMIT viewChanged();
        Q_EMIT viewStateChanged();
    }
}

void PDFAppDocumentView::setFirstPageRight(bool isFirstPageRight)
{
    if (m_isFirstPageRight != isFirstPageRight)
    {
        m_isFirstPageRight = isFirstPageRight;
        applyPageLayout();
        Q_EMIT viewChanged();
        Q_EMIT viewStateChanged();
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
        Q_EMIT viewChanged();
        Q_EMIT viewStateChanged();
    }
}

void PDFAppDocumentView::updateSidebarVisibility()
{
    if (!m_sidebar || !m_splitter)
    {
        return;
    }

    // The sidebar keeps its width (the splitter gives it back)
    const bool isShown = m_isSidebarVisible && !m_isFullScreen;
    if (m_sidebar->isHidden() == !isShown)
    {
        return;
    }

    m_sidebar->setVisible(isShown);
    if (isShown && m_splitter->width() > 0)
    {
        const int sidebarWidth = qBound(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, m_sidebarWidth, qMax(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, m_splitter->width() / 2));
        m_splitter->setSizes({ sidebarWidth, qMax(0, m_splitter->width() - sidebarWidth - m_splitter->handleWidth()) });
    }
}

void PDFAppDocumentView::onSplitterMoved()
{
    if (m_sidebar && m_sidebar->isVisible())
    {
        m_sidebarWidth = m_sidebar->width();
        Q_EMIT viewChanged();
    }
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
    Q_EMIT viewChanged();
    Q_EMIT viewStateChanged();
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
    if (predefinedTool != PDFToolManager::ToolEnd && predefinedTool != PDFToolManager::SelectTextTool && isCanvasToolActive(tool))
    {
        // The active tool is turned off: Select text is the tool of the canvas
        m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
        return;
    }

    if (predefinedTool == PDFToolManager::ToolEnd || m_toolManager->getPredefinedTool(predefinedTool))
    {
        m_isSelectAllPending = false;
        m_toolManager->setActiveTool(predefinedTool);
    }
}

bool PDFAppDocumentView::isCanvasToolActive(int tool) const
{
    if (!m_toolManager || tool < 0 || tool >= PDFToolManager::ToolEnd)
    {
        return false;
    }

    const PDFWidgetTool* predefinedTool = m_toolManager->getPredefinedTool(static_cast<PDFToolManager::PredefinedTools>(tool));
    return predefinedTool && predefinedTool->isActive();
}

bool PDFAppDocumentView::hasTextSelection() const
{
    const PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
    return selectTextTool && selectTextTool->isActive() && selectTextTool->hasSelection();
}

void PDFAppDocumentView::copySelectedText()
{
    if (PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr)
    {
        selectTextTool->copyText();
    }
}

void PDFAppDocumentView::selectAllText()
{
    PDFSelectTextTool* selectTextTool = m_toolManager ? m_toolManager->getSelectTextTool() : nullptr;
    PDFDrawWidgetProxy* proxy = getProxy();
    if (!selectTextTool || !proxy || getPageCount() <= 0)
    {
        return;
    }

    // The tool makes the text layout of the pages in a worker thread, when it is activated
    if (!selectTextTool->isActive())
    {
        m_toolManager->setActiveTool(PDFToolManager::SelectTextTool);
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

void PDFAppDocumentView::onActiveToolChanged()
{
    Q_EMIT viewChanged();
    Q_EMIT viewStateChanged();
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
    state.sidebarVisible = m_isSidebarVisible;
    state.sidebarWidth = qBound(PDFAppViewState::MINIMUM_SIDEBAR_WIDTH, m_sidebarWidth, PDFAppViewState::MAXIMUM_SIDEBAR_WIDTH);

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
        setSidebarVisible(*state.sidebarVisible);
    }
    if (state.panelId)
    {
        // An unknown panel is ignored
        if (m_sidebar)
        {
            m_sidebar->showPanel(*state.panelId);
        }
    }

    Q_EMIT viewChanged();
    Q_EMIT viewStateChanged();
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

    cmsSettings.foregroundColor = H::getColor(settings.getString(K::colorModesTextColor()), QColor(0xFF, 0xB0, 0x00));
    cmsSettings.backgroundColor = H::getColor(settings.getString(K::colorModesPageColor()), QColor(0x14, 0x14, 0x14));
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
        magnifier->setMagnifierSize(int(settings.getInteger(K::magnifierSize())));
        magnifier->setMagnifierZoom(settings.getReal(K::magnifierZoom()));
    }

    // Forms
    PDFFormManager::FormAppearanceFlags formFlags = PDFFormManager::None;
    formFlags.setFlag(PDFFormManager::HighlightFields, settings.getBool(K::formsHighlightFields()));
    formFlags.setFlag(PDFFormManager::HighlightRequiredFields, settings.getBool(K::formsHighlightRequiredFields()));
    formFlags.setFlag(PDFFormManager::EditReadOnlyFields, settings.getBool(K::formsAllowEditReadOnly()));
    if (m_formManager->getAppearanceFlags() != formFlags)
    {
        m_formManager->setAppearanceFlags(formFlags);
    }

    m_isApplyingSettings = false;
    Q_EMIT viewChanged();
    Q_EMIT viewStateChanged();
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
    if (watched == m_canvasArea && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest))
    {
        layoutInfoBars();
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
