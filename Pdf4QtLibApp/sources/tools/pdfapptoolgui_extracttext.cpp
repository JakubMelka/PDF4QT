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

#include "pdfapptoolgui_extracttext.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumenttool_extracttext.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawspacecontroller.h"
#include "pdffluentwidgets.h"
#include "pdfpainterutils.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLocale>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace pdf
{

/// Helpers of the GUI of Extract Text
class PDFAppToolGUIExtractTextHelper
{
public:
    PDFAppToolGUIExtractTextHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Paddings of a pane (pane-PAGE 339:8645: 10 / 12 / 12 / 12 inside of the border, gap 10)
    static constexpr int PANE_PADDING_TOP = 11;
    static constexpr int PANE_PADDING = 13;
    static constexpr int PANE_GAP = 10;
    static constexpr int AREA_PADDING = 14;
    static constexpr int AREA_RADIUS = 6;
    static constexpr int PANE_RADIUS = 10;

    /// Width of a segment of Fit / 100 % (871:135917)
    static constexpr int ZOOM_SEGMENT_WIDTH = 60;

    /// A scroll area of a pane: no frame, the scroll bars over the content
    static QScrollArea* createScrollArea(QWidget* parent)
    {
        QScrollArea* area = new QScrollArea(parent);
        area->setFrameShape(QFrame::NoFrame);
        area->setProperty("overlayScrollBars", true);
        area->viewport()->setAutoFillBackground(false);
        return area;
    }

    /// The short name of an included item in the row Included (339:8742 "Alt. descriptions")
    static QString getShortIncludeTitle(const char* parameter)
    {
        using Tool = PDFDocumentToolExtractText;
        const QString name = QString::fromLatin1(parameter);
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_PAGE_NUMBERS))
        {
            return PDFAppToolGUIExtractText::tr("Page numbers");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_TITLES))
        {
            return PDFAppToolGUIExtractText::tr("Structure titles");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_LANGUAGES))
        {
            return PDFAppToolGUIExtractText::tr("Languages");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_ALTERNATIVE_DESCRIPTIONS))
        {
            return PDFAppToolGUIExtractText::tr("Alt. descriptions");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_EXPANDED_FORMS))
        {
            return PDFAppToolGUIExtractText::tr("Expanded forms");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_ACTUAL_TEXT))
        {
            return PDFAppToolGUIExtractText::tr("Actual text");
        }
        return PDFAppToolGUIExtractText::tr("Phonemes");
    }

    /// "12 pages have no text" (339:8763)
    static QString getNoTextTitle(qint64 count)
    {
        const int value = int(qMin<qint64>(count, std::numeric_limits<int>::max()));
        return value == 1 ? PDFAppToolGUIExtractText::tr("%Ln page has no text", nullptr, value) : PDFAppToolGUIExtractText::tr("%Ln pages have no text", nullptr, value);
    }

    /// "about 412,880 words"
    static int getWordCount(const QJsonObject& data)
    {
        return int(qMin<qint64>(data.value(QStringLiteral("words")).toInteger(), std::numeric_limits<int>::max()));
    }

    /// Icons of the included items (339:9492)
    static QString getIncludeIcon(const char* parameter)
    {
        using Tool = PDFDocumentToolExtractText;
        const QString name = QString::fromLatin1(parameter);
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_PAGE_NUMBERS))
        {
            return QStringLiteral("list-ordered");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_TITLES))
        {
            return QStringLiteral("bookmark");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_LANGUAGES))
        {
            return QStringLiteral("globe");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_ALTERNATIVE_DESCRIPTIONS))
        {
            return QStringLiteral("alt-text");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_EXPANDED_FORMS))
        {
            return QStringLiteral("expand");
        }
        if (name == QLatin1String(Tool::PARAMETER_INCLUDE_ACTUAL_TEXT))
        {
            return QStringLiteral("text-cursor");
        }
        return QStringLiteral("volume-2");
    }

    /// The title of an option of a parameter in the language of the user interface
    static QString getOptionTitle(const PDFAppToolParameterModel* model, const char* parameter)
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), name(parameter)) : nullptr;
        const QString value = model ? model->getValue(name(parameter)).toString() : QString();
        if (definition)
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                if (option.id == value)
                {
                    return option.getTitle();
                }
            }
        }
        return value;
    }
};

// --------------------------------------------------------- PDFAppToolPagePicture

PDFAppToolPagePicture::PDFAppToolPagePicture(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPagePicture"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

PDFAppToolPagePicture::~PDFAppToolPagePicture() = default;

void PDFAppToolPagePicture::setProxy(PDFDrawWidgetProxy* proxy)
{
    if (m_proxy == proxy)
    {
        return;
    }
    if (m_proxy)
    {
        m_proxy->disconnect(this);
    }
    m_proxy = proxy;
    if (m_proxy)
    {
        connect(m_proxy.data(), &PDFDrawWidgetProxy::pageImageChanged, this, &PDFAppToolPagePicture::onPageImageChanged);
        connect(m_proxy.data(), &QObject::destroyed, this, &PDFAppToolPagePicture::onProxyDestroyed);
    }
    invalidate();
}

void PDFAppToolPagePicture::setPageIndex(PDFInteger pageIndex)
{
    if (m_pageIndex != pageIndex)
    {
        m_pageIndex = pageIndex;
        invalidate();
        updateGeometry();
    }
}

void PDFAppToolPagePicture::setFit(bool isFit)
{
    if (m_isFit != isFit)
    {
        m_isFit = isFit;
        invalidate();
        updateGeometry();
    }
}

QSize PDFAppToolPagePicture::getActualSize() const
{
    const PDFDocument* document = m_proxy ? m_proxy->getDocument() : nullptr;
    const PDFPage* page = document ? document->getCatalog()->getPage(m_pageIndex) : nullptr;
    if (!page)
    {
        return QSize();
    }

    // 100 %: a point of the page is 1/72 inch on the screen
    const QSizeF size = page->getRotatedMediaBox().size() * (logicalDpiX() / 72.0);
    return size.toSize();
}

QSize PDFAppToolPagePicture::sizeHint() const
{
    if (m_isFit)
    {
        return BaseClass::sizeHint();
    }

    const QSize actualSize = getActualSize();
    return actualSize.isValid() ? actualSize + QSize(2 * PDFAppToolGUIExtractTextHelper::AREA_PADDING, 2 * PDFAppToolGUIExtractTextHelper::AREA_PADDING) : BaseClass::sizeHint();
}

void PDFAppToolPagePicture::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    using Helper = PDFAppToolGUIExtractTextHelper;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();

    // area 339:8647: bg/muted, radius 6, padding 14
    painter.setPen(Qt::NoPen);
    painter.setBrush(manager->getColor(PDFThemeColors::BgMuted));
    painter.drawRoundedRect(QRectF(rect()), Helper::AREA_RADIUS, Helper::AREA_RADIUS);

    const QSize actualSize = getActualSize();
    if (!actualSize.isValid() || actualSize.isEmpty())
    {
        m_paperRect = QRect();
        return;
    }

    // The paper at the top, centered (Sample page 339:8649); Fit keeps the aspect ratio
    const QRect available = rect().adjusted(Helper::AREA_PADDING, Helper::AREA_PADDING, -Helper::AREA_PADDING, -Helper::AREA_PADDING);
    QSize paperSize = actualSize;
    if (m_isFit)
    {
        paperSize.scale(available.size(), Qt::KeepAspectRatio);
    }
    if (paperSize.isEmpty())
    {
        m_paperRect = QRect();
        return;
    }
    m_paperRect = QRect(QPoint(available.left() + (available.width() - paperSize.width()) / 2, available.top()), paperSize);

    // The picture of the compiled page in pixels of the device
    const qreal ratio = devicePixelRatioF();
    const QSize pixelSize = (QSizeF(paperSize) * ratio).toSize().boundedTo(QSize(MAXIMUM_PICTURE_SIZE, MAXIMUM_PICTURE_SIZE));
    if (m_image.isNull() || m_imagePixelSize != pixelSize)
    {
        m_imagePixelSize = pixelSize;
        m_image = m_proxy ? m_proxy->drawThumbnailImage(m_pageIndex, qMax(pixelSize.width(), pixelSize.height())) : QImage();
    }

    // The shadow 0 / 1 / 5 of effect/shadow (339:8649) and the paper
    const QColor shadow = manager->getColor(PDFThemeColors::EffectShadow);
    painter.setBrush(shadow);
    painter.drawRect(QRectF(m_paperRect).adjusted(-1.0, 0.0, 1.0, 2.0));
    painter.setBrush(Qt::white);
    painter.drawRect(m_paperRect);
    if (!m_image.isNull())
    {
        painter.drawImage(m_paperRect, m_image);
    }
}

void PDFAppToolPagePicture::onPageImageChanged(bool all, const std::vector<pdf::PDFInteger>& pages)
{
    if (all || std::find(pages.cbegin(), pages.cend(), m_pageIndex) != pages.cend())
    {
        invalidate();
    }
}

void PDFAppToolPagePicture::onProxyDestroyed()
{
    // The proxy is going away: only the pointer is forgotten
    m_image = QImage();
    update();
}

void PDFAppToolPagePicture::invalidate()
{
    m_image = QImage();
    m_imagePixelSize = QSize();
    update();
}

// --------------------------------------------------------- PDFAppToolTextPreviewPage

PDFAppToolTextPreviewPage::PDFAppToolTextPreviewPage(QWidget* parent) :
    BaseClass(parent)
{
    using Helper = PDFAppToolGUIExtractTextHelper;

    setObjectName(QStringLiteral("toolTextPreviewPage"));

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    m_content = new QWidget(this);
    outerLayout->addWidget(m_content);

    // document-canvas 339:8145: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 339:8629: the title Semi Bold 13, the segments Fit / 100 %
    QWidget* bar = new QWidget(m_content);
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("toolPreviewTitle"));
    barLayout->addWidget(m_title, 1);
    m_zoom = new PDFSegmentedControl(bar);
    m_zoom->setObjectName(QStringLiteral("toolPreviewZoom"));
    m_zoom->addSegment(tr("Fit"));
    m_zoom->addSegment(tr("100 %"));
    m_zoom->setFixedWidth(2 * Helper::ZOOM_SEGMENT_WIDTH);
    m_zoom->setAccessibleName(tr("Size of the page"));
    connect(m_zoom.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolTextPreviewPage::onZoomChanged);
    barLayout->addWidget(m_zoom);
    layout->addWidget(bar);

    // panes 339:8644: two cards of the same width, gap 12
    QWidget* panes = new QWidget(m_content);
    QHBoxLayout* panesLayout = new QHBoxLayout(panes);
    panesLayout->setContentsMargins(0, 0, 0, 0);
    panesLayout->setSpacing(GAP);
    layout->addWidget(panes, 1);

    for (int i = 0; i < 2; ++i)
    {
        PDFTintedCard* pane = new PDFTintedCard(panes);
        pane->setRadius(Helper::PANE_RADIUS);
        pane->setObjectName(i == 0 ? QStringLiteral("toolPanePage") : QStringLiteral("toolPaneText"));
        panesLayout->addWidget(pane, 1);
        QVBoxLayout* paneLayout = new QVBoxLayout(pane);
        paneLayout->setContentsMargins(Helper::PANE_PADDING, Helper::PANE_PADDING_TOP, Helper::PANE_PADDING, Helper::PANE_PADDING);
        paneLayout->setSpacing(Helper::PANE_GAP);

        // PAGE / EXTRACTED TEXT: Semi Bold 11 text/tertiary, centered
        PDFLabel* caption = PDFAppPageWidgets::createLabel(i == 0 ? tr("PAGE") : tr("EXTRACTED TEXT"), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, pane);
        caption->setObjectName(QStringLiteral("toolPaneCaption"));
        caption->setAlignment(Qt::AlignCenter);
        paneLayout->addWidget(caption);

        QScrollArea* area = Helper::createScrollArea(pane);
        paneLayout->addWidget(area, 1);

        PDFLabel* footer = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, pane);
        footer->setAlignment(Qt::AlignCenter);
        paneLayout->addWidget(footer);

        if (i == 0)
        {
            m_pictureArea = area;
            m_picture = new PDFAppToolPagePicture(area);
            area->setWidget(m_picture);
            area->setWidgetResizable(true);
            m_pageFooter = footer;
            m_pageFooter->setObjectName(QStringLiteral("toolPageFooter"));
        }
        else
        {
            // area 339:8711: bg/surface, padding 14, Regular 12 text/primary in lines of 18
            m_textArea = area;
            QWidget* textHolder = new QWidget(area);
            QVBoxLayout* textLayout = new QVBoxLayout(textHolder);
            textLayout->setContentsMargins(Helper::AREA_PADDING, Helper::AREA_PADDING, Helper::AREA_PADDING, Helper::AREA_PADDING);
            m_text = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 18, PDFThemeColors::TextPrimary, textHolder);
            m_text->setObjectName(QStringLiteral("toolPreviewText"));
            m_text->setTextFormat(Qt::PlainText);
            m_text->setMinimumWidth(1);
            textLayout->addWidget(m_text);
            textLayout->addStretch(1);
            area->setWidget(textHolder);
            area->setWidgetResizable(true);
            area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_textFooter = footer;
            m_textFooter->setObjectName(QStringLiteral("toolTextFooter"));
        }
    }

    // estimate 339:8714: Semi Bold 13 text/primary and Regular 12 text/secondary, gap 8
    QWidget* estimate = new QWidget(m_content);
    QHBoxLayout* estimateLayout = new QHBoxLayout(estimate);
    estimateLayout->setContentsMargins(0, 0, 0, 0);
    estimateLayout->setSpacing(8);
    m_estimate = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, estimate);
    m_estimate->setObjectName(QStringLiteral("toolEstimate"));
    m_estimateDetail = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, estimate);
    m_estimateDetail->setObjectName(QStringLiteral("toolEstimateDetail"));
    estimateLayout->addWidget(m_estimate);
    estimateLayout->addWidget(m_estimateDetail, 1);
    layout->addWidget(estimate);
}

PDFAppToolTextPreviewPage::~PDFAppToolTextPreviewPage() = default;

void PDFAppToolTextPreviewPage::setContentShown(bool isShown)
{
    if (m_content)
    {
        m_content->setVisible(isShown);
    }
}

void PDFAppToolTextPreviewPage::setTitle(const QString& text)
{
    if (m_title)
    {
        m_title->setText(text);
    }
}

void PDFAppToolTextPreviewPage::setPageFooter(const QString& text)
{
    if (m_pageFooter)
    {
        m_pageFooter->setText(text);
    }
}

void PDFAppToolTextPreviewPage::setText(const QString& text)
{
    if (m_text)
    {
        m_text->setText(text);
        m_text->updateGeometry();
    }
}

void PDFAppToolTextPreviewPage::setTextFooter(const QString& text)
{
    if (m_textFooter)
    {
        m_textFooter->setText(text);
    }
}

void PDFAppToolTextPreviewPage::setEstimate(const QString& text, const QString& detail)
{
    if (m_estimate)
    {
        m_estimate->setText(text);
    }
    if (m_estimateDetail)
    {
        m_estimateDetail->setText(detail);
    }
}

void PDFAppToolTextPreviewPage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppToolTextPreviewPage::onZoomChanged(int index)
{
    if (!m_picture || !m_pictureArea)
    {
        return;
    }

    // Fit fills the area, 100 % has its size and scrolls
    const bool isFit = index == 0;
    m_picture->setFit(isFit);
    m_pictureArea->setWidgetResizable(isFit);
    if (!isFit)
    {
        m_picture->resize(m_picture->sizeHint().expandedTo(m_pictureArea->viewport()->size()));
    }
}

// --------------------------------------------------------- PDFAppToolGUIExtractText

PDFAppToolGUIExtractText::PDFAppToolGUIExtractText(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIExtractText::~PDFAppToolGUIExtractText() = default;

void PDFAppToolGUIExtractText::registerTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolExtractText::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolExtractText>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIExtractText>>());
    }
}

void PDFAppToolGUIExtractText::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIExtractTextHelper;
    using Tool = PDFDocumentToolExtractText;

    // tool-actions 339:8132 with the panels of 339:8765 (270, 280, 260 wide)
    m_orderMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_ORDER), QStringLiteral("align-left"), tr("Order: %1"), 270);

    std::vector<PDFAppToolFlagItem> items;
    const PDFAppToolParameterModel* model = getModel();
    for (const char* parameter : Tool::getIncludeParameters())
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(parameter)) : nullptr;
        PDFAppToolFlagItem item;
        item.parameter = Helper::name(parameter);
        item.title = definition ? definition->getTitle() : QString();
        item.description = definition ? definition->getDescription() : QString();
        item.iconName = Helper::getIncludeIcon(parameter);
        if (item.parameter == QLatin1String(Tool::PARAMETER_INCLUDE_TITLES))
        {
            item.groupCaption = tr("FROM THE TAGS OF THE DOCUMENT");
        }
        items.push_back(std::move(item));
    }
    m_includeMenu = builder.addFlagsMenu(std::move(items), QStringLiteral("plus-circle"), QString(), 280, QString(), PDFTileRow::Kind::MenuItem);

    m_formatMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_FORMAT), QStringLiteral("file-text"), tr("Format: %1"), 260, tr("FORMAT"));
    if (m_formatMenu)
    {
        m_formatMenu->addGroup(Helper::name(Tool::PARAMETER_ENCODING), tr("ENCODING"));
    }
    updateButtons();
    updateActionsEnabled();
}

void PDFAppToolGUIExtractText::updateActionsEnabled()
{
    // 339:9601 and 339:10328: nothing to choose below the result dialog and for a document,
    // which refuses the tool (during a run the shell disables the actions as a whole)
    const PDFDocumentToolShell* shell = getShell();
    const bool isEnabled = shell && !shell->isPermissionRefused() && shell->getState() != PDFDocumentToolShell::State::Deciding;
    for (const QPointer<QToolButton>& button : { QPointer<QToolButton>(m_orderMenu ? m_orderMenu->getButton() : nullptr),
                                                 QPointer<QToolButton>(m_includeMenu ? m_includeMenu->getButton() : nullptr),
                                                 QPointer<QToolButton>(m_formatMenu ? m_formatMenu->getButton() : nullptr) })
    {
        if (button)
        {
            button->setEnabled(isEnabled);
        }
    }
}

void PDFAppToolGUIExtractText::onShellStateChanged(pdf::PDFDocumentToolShell::State state)
{
    Q_UNUSED(state);
    updateActionsEnabled();
}

void PDFAppToolGUIExtractText::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Tool = PDFDocumentToolExtractText;

    // sidebar-left 339:8139: x 12, width 275 (the frame of the panels has 10 / 11)
    if (QWidget* widget = builder.getWidget())
    {
        if (QLayout* layout = widget->layout())
        {
            layout->setContentsMargins(2, 0, 2, 0);
        }
    }

    builder.addPageSelection(PDFAppToolGUIExtractTextHelper::name(Tool::PARAMETER_PAGES), tr("Pages"));

    // thumbnails 339:8282: the panel of the sidebar, following the canvas
    m_thumbnails = new PDFAppThumbnailsPanel(builder.getWidget());
    m_thumbnails->setObjectName(QStringLiteral("toolThumbnails"));
    const PDFAppToolHost* host = getHost();
    m_thumbnails->setDocument(getDocument(), host ? host->getView() : nullptr);
    m_thumbnails->getListView()->setSidePadding(0);
    builder.addWidget(m_thumbnails);
    builder.setFillsPanel(m_thumbnails);
}

void PDFAppToolGUIExtractText::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-right 339:8146: Document, Settings, Result and the callout (gap 14)
    builder.addSection(tr("Document"));
    m_documentRows = builder.addKeyValues();
    m_documentRows->setObjectName(QStringLiteral("toolDocumentRows"));
    builder.endSection();

    builder.addSection(tr("Settings"));
    m_settingsRows = builder.addKeyValues();
    m_settingsRows->setObjectName(QStringLiteral("toolSettingsRows"));
    builder.endSection();

    builder.addSection(tr("Result"));
    m_resultRows = builder.addKeyValues();
    m_resultRows->setObjectName(QStringLiteral("toolResultRows"));
    builder.endSection();

    // callout-amber 339:8759
    m_noTextCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), tr("They are most likely scanned images. Nothing can be extracted from them, so they are left out of the result."));
    m_noTextCallout->setObjectName(QStringLiteral("toolNoTextCallout"));
    m_noTextCallout->getTitleLabel()->setWordWrap(true);

    updateRows();
}

QWidget* PDFAppToolGUIExtractText::createCanvasPage(QWidget* parent)
{
    m_previewPage = new PDFAppToolTextPreviewPage(parent);
    PDFAppToolHost* host = getHost();
    PDFAppDocumentView* view = host ? host->getView() : nullptr;
    m_previewPage->getPicture()->setProxy(view ? view->getProxy() : nullptr);
    if (view && m_connectedView != view)
    {
        connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolGUIExtractText::onDisplayedPageChanged);
        m_connectedView = view;
    }
    if (PDFDocumentToolShell* shell = getShell())
    {
        connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppToolGUIExtractText::onShellStateChanged, Qt::UniqueConnection);
    }

    updatePreviewPage();
    requestPagePreview();
    return m_previewPage.data();
}

PDFInteger PDFAppToolGUIExtractText::getPreviewPageIndex() const
{
    const PDFAppToolHost* host = getHost();
    const PDFAppDocumentView* view = host ? host->getView() : nullptr;
    const PDFAppDocument* document = getDocument();
    if (!view || !document || !document->getDocument() || document->getDocument()->getCatalog()->getPageCount() == 0)
    {
        return -1;
    }
    return qMax<PDFInteger>(0, view->getDisplayedPageIndex());
}

void PDFAppToolGUIExtractText::ensurePageRunner()
{
    if (m_pageRunner || !getDocument())
    {
        return;
    }

    // One tool for every GUI: it has no state, the runner holds it
    static const std::shared_ptr<const PDFDocumentTool> s_pageTool = std::make_shared<PDFDocumentToolExtractTextPage>();
    m_pageRunner = new PDFDocumentToolRunner(getDocument(), s_pageTool, this);
    connect(m_pageRunner.data(), &PDFDocumentToolRunner::previewFinished, this, &PDFAppToolGUIExtractText::onPagePreviewFinished);
    connect(m_pageRunner.data(), &PDFDocumentToolRunner::previewDropped, this, &PDFAppToolGUIExtractText::onPagePreviewDropped);
}

void PDFAppToolGUIExtractText::requestPagePreview()
{
    const PDFDocumentToolShell* shell = getShell();
    const PDFInteger pageIndex = getPreviewPageIndex();
    if (!shell || shell->isPermissionRefused() || pageIndex < 0 || !getModel())
    {
        return;
    }

    ensurePageRunner();
    if (!m_pageRunner)
    {
        return;
    }

    // A cached preview comes at once, by previewFinished (it only sets the texts)
    QVariantMap parameters = getRunParameters();
    parameters.insert(QString::fromLatin1(PDFDocumentToolExtractTextPage::PARAMETER_PREVIEW_PAGE), QVariant(qlonglong(pageIndex)));
    m_pageRunner->requestPreview(parameters);
}

void PDFAppToolGUIExtractText::onDisplayedPageChanged(pdf::PDFInteger pageIndex)
{
    Q_UNUSED(pageIndex);
    updatePreviewPage();
    requestPagePreview();
}

void PDFAppToolGUIExtractText::onPagePreviewFinished(const pdf::PDFDocumentToolResult& result)
{
    if (result.isSuccess())
    {
        m_pagePreview = result;
    }
    else
    {
        m_pagePreview.reset();
    }
    updatePreviewPage();
}

void PDFAppToolGUIExtractText::onPagePreviewDropped()
{
    // A new version of the document: the shown page is read again
    m_pagePreview.reset();
    updatePreviewPage();
    requestPagePreview();
}

void PDFAppToolGUIExtractText::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);
    updateButtons();
    updateRows();
    updatePreviewPage();
    requestPagePreview();
}

void PDFAppToolGUIExtractText::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    if (preview && preview->isSuccess())
    {
        m_preview = *preview;
    }
    else
    {
        m_preview.reset();
    }

    // The slots of the status may delete this GUI: the status is the last thing
    updateRows();
    updatePreviewPage();
    updateStatusText();
}

void PDFAppToolGUIExtractText::onDocumentChanged()
{
    updateRows();
    updatePreviewPage();
}

void PDFAppToolGUIExtractText::updateButtons()
{
    // 339:8132: "Include: 1 extra", "Format: Text" (the menu row says "Plain text")
    if (m_includeMenu && m_includeMenu->getButton())
    {
        const int count = m_includeMenu->getCheckedCount();
        m_includeMenu->getButton()->setText(count == 1 ? tr("Include: %Ln extra", nullptr, count) : tr("Include: %Ln extras", nullptr, count));
    }

    const PDFAppToolParameterModel* model = getModel();
    if (m_formatMenu && m_formatMenu->getButton() && model)
    {
        const PDFOutputFormatter::Style style = PDFDocumentToolExtractText::getStyle(model->getValues());
        QString format = tr("Text");
        if (style == PDFOutputFormatter::Style::Html)
        {
            format = tr("HTML");
        }
        else if (style == PDFOutputFormatter::Style::Xml)
        {
            format = tr("XML");
        }
        m_formatMenu->getButton()->setText(tr("Format: %1").arg(format));
    }
}

QString PDFAppToolGUIExtractText::getFormatText() const
{
    using Tool = PDFDocumentToolExtractText;

    // "Plain text · UTF-8" (339:8743, 339:8712)
    const PDFAppToolParameterModel* model = getModel();
    return tr("%1 · %2").arg(PDFAppToolGUIExtractTextHelper::getOptionTitle(model, Tool::PARAMETER_FORMAT), PDFAppToolGUIExtractTextHelper::getOptionTitle(model, Tool::PARAMETER_ENCODING));
}

QString PDFAppToolGUIExtractText::getEstimateText(qint64 value) const
{
    return tr("about %1").arg(QLocale().toString(value));
}

void PDFAppToolGUIExtractText::updateRows()
{
    using Helper = PDFAppToolGUIExtractTextHelper;
    using Tool = PDFDocumentToolExtractText;

    const QString none = QStringLiteral("–");
    const QLocale locale;
    const PDFAppToolParameterModel* model = getModel();
    const PDFAppDocument* appDocument = getDocument();
    const PDFDocument* document = appDocument && appDocument->getDocument() ? appDocument->getDocument().get() : nullptr;
    const QJsonObject data = m_preview ? m_preview->data : QJsonObject();
    const bool hasPreview = m_preview.has_value();

    if (m_documentRows)
    {
        // section-Document 339:8717 (339:10328: copying "Not allowed", the other rows as they are)
        const bool isCopyAllowed = document && document->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::CopyContent);
        m_documentRows->clear();
        m_documentRows->addRow(tr("Tagged"), hasPreview ? (data.value(QStringLiteral("tagged")).toBool() ? tr("Yes") : tr("No")) : none);
        m_documentRows->addRow(tr("Pages with text"), hasPreview ? tr("%1 of %2").arg(locale.toString(data.value(QStringLiteral("pagesWithText")).toInteger()), locale.toString(data.value(QStringLiteral("pageCount")).toInteger())) : none);
        m_documentRows->addRow(tr("Copying content"), isCopyAllowed ? tr("Allowed") : tr("Not allowed"));
    }

    if (m_settingsRows && model)
    {
        const QVariantMap values = model->getValues();

        // Pages: "All · 1,592" or the range of the selection
        QString pagesText = none;
        QString errorMessage;
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(values.value(Helper::name(Tool::PARAMETER_PAGES)), &errorMessage);
        if (selection && document)
        {
            const std::vector<PDFInteger> pages = selection->getPageIndices(document, &errorMessage);
            const bool isAll = selection->pages.trimmed().isEmpty() && selection->filters == PDFDocumentToolPageSelection::Filters();
            pagesText = tr("%1 · %2").arg(isAll ? tr("All") : selection->pages.trimmed().isEmpty() ? tr("Filtered") : selection->pages.trimmed(), locale.toString(qint64(pages.size())));
        }

        // Reading order: "Auto → structure" by the order the preview has used
        QString orderText = Helper::getOptionTitle(model, Tool::PARAMETER_ORDER);
        const QString usedOrder = data.value(QStringLiteral("order")).toString();
        if (hasPreview && values.value(Helper::name(Tool::PARAMETER_ORDER)).toString() == QLatin1String(Tool::ORDER_AUTO) && !usedOrder.isEmpty())
        {
            QString usedText = tr("layout");
            if (usedOrder == QLatin1String(Tool::ORDER_STRUCTURE))
            {
                usedText = tr("structure");
            }
            else if (usedOrder == QLatin1String(Tool::ORDER_CONTENT_STREAM))
            {
                usedText = tr("content stream");
            }
            orderText = tr("%1 → %2").arg(orderText, usedText);
        }

        // Included: the one item by its short name, more items counted
        QStringList included;
        for (const char* parameter : Tool::getIncludeParameters())
        {
            if (values.value(Helper::name(parameter)).toBool())
            {
                included << Helper::getShortIncludeTitle(parameter);
            }
        }
        const QString includedText = included.isEmpty() ? tr("Nothing") : (included.size() == 1 ? included.front() : tr("%Ln items", nullptr, int(included.size())));

        m_settingsRows->clear();
        m_settingsRows->addRow(tr("Pages"), pagesText);
        m_settingsRows->addRow(tr("Reading order"), orderText);
        m_settingsRows->addRow(tr("Included"), includedText);
        m_settingsRows->addRow(tr("Format"), getFormatText());
    }

    if (m_resultRows)
    {
        m_resultRows->clear();
        m_resultRows->addRow(tr("Words"), hasPreview ? getEstimateText(data.value(QStringLiteral("words")).toInteger()) : none);
        m_resultRows->addRow(tr("Characters"), hasPreview ? getEstimateText(data.value(QStringLiteral("characters")).toInteger()) : none);
        m_resultRows->addRow(tr("Size"), hasPreview ? tr("about %1").arg(PDFDocumentToolFormat::formatSize(data.value(QStringLiteral("size")).toInteger())) : none);
    }

    if (m_noTextCallout)
    {
        const qint64 pagesWithoutText = data.value(QStringLiteral("pagesWithoutText")).toInteger();
        m_noTextCallout->setVisible(hasPreview && pagesWithoutText > 0);
        m_noTextCallout->setTexts(Helper::getNoTextTitle(pagesWithoutText),
                                  tr("They are most likely scanned images. Nothing can be extracted from them, so they are left out of the result."));
    }
}

void PDFAppToolGUIExtractText::updatePreviewPage()
{
    if (!m_previewPage)
    {
        return;
    }

    // 339:10328: a document, which refuses the tool, has an empty canvas
    const PDFDocumentToolShell* shell = getShell();
    const bool isRefused = shell && shell->isPermissionRefused();
    m_previewPage->setContentShown(!isRefused);

    const PDFInteger pageIndex = getPreviewPageIndex();
    const QLocale locale;
    const QString pageNumber = locale.toString(pageIndex + 1);
    m_previewPage->getPicture()->setPageIndex(pageIndex);
    m_previewPage->setTitle(pageIndex >= 0 ? tr("Page %1 · preview of the extracted text").arg(pageNumber) : QString());
    m_previewPage->setTextFooter(getFormatText());

    const bool isPageShown = m_pagePreview && m_pagePreview->data.value(QStringLiteral("pageIndex")).toInteger() == pageIndex;
    if (isPageShown)
    {
        const QJsonObject& data = m_pagePreview->data;
        m_previewPage->setPageFooter(data.value(QStringLiteral("tagged")).toBool() ? tr("Page %1 · tagged document").arg(pageNumber) : tr("Page %1 · untagged document").arg(pageNumber));

        QString text = data.value(QStringLiteral("text")).toString();
        if (!data.value(QStringLiteral("hasText")).toBool())
        {
            text = tr("This page has no text.");
        }
        else if (data.value(QStringLiteral("isTruncated")).toBool())
        {
            text += QStringLiteral("\n…");
        }
        m_previewPage->setText(text);
    }
    else
    {
        m_previewPage->setPageFooter(pageIndex >= 0 ? tr("Page %1").arg(pageNumber) : QString());
        m_previewPage->setText(QString());
    }

    // estimate 339:8714: "Whole document: about 412,880 words", "2.9 MB of text · 12 pages have no text"
    if (m_preview)
    {
        const QJsonObject& data = m_preview->data;
        const PDFAppDocument* appDocument = getDocument();
        const qint64 documentPageCount = appDocument && appDocument->getDocument() ? qint64(appDocument->getDocument()->getCatalog()->getPageCount()) : 0;
        const int words = PDFAppToolGUIExtractTextHelper::getWordCount(data);
        QString title;
        if (data.value(QStringLiteral("pageCount")).toInteger() == documentPageCount)
        {
            title = words == 1 ? tr("Whole document: about %Ln word", nullptr, words) : tr("Whole document: about %Ln words", nullptr, words);
        }
        else
        {
            title = words == 1 ? tr("Selected pages: about %Ln word", nullptr, words) : tr("Selected pages: about %Ln words", nullptr, words);
        }
        QString detail = tr("%1 of text").arg(PDFDocumentToolFormat::formatSize(data.value(QStringLiteral("size")).toInteger()));
        const qint64 pagesWithoutText = data.value(QStringLiteral("pagesWithoutText")).toInteger();
        if (pagesWithoutText > 0)
        {
            detail += QStringLiteral(" · ") + PDFAppToolGUIExtractTextHelper::getNoTextTitle(pagesWithoutText);
        }
        m_previewPage->setEstimate(title, detail);
    }
    else
    {
        m_previewPage->setEstimate(QString(), QString());
    }
}

QString PDFAppToolGUIExtractText::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const PDFAppDocument* document = getDocument();
    const QString title = tool ? tool->getTitle() : QString();
    if (!document || !document->getDocument())
    {
        return title;
    }

    // 339:10328
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused())
    {
        return tr("%1 · the document does not allow copying its content").arg(title);
    }

    // 339:8147: "Extract Text · Project_Report.pdf · 1,592 pages · about 412,880 words"
    const int pageCount = int(qMin<size_t>(document->getDocument()->getCatalog()->getPageCount(), size_t(std::numeric_limits<int>::max())));
    QString text = (pageCount == 1 ? tr("%1 · %2 · %Ln page", nullptr, pageCount) : tr("%1 · %2 · %Ln pages", nullptr, pageCount)).arg(title, document->getTitle());
    if (m_preview)
    {
        const int words = PDFAppToolGUIExtractTextHelper::getWordCount(m_preview->data);
        text += QStringLiteral(" · ") + (words == 1 ? tr("about %Ln word", nullptr, words) : tr("about %Ln words", nullptr, words));
    }
    return text;
}

QString PDFAppToolGUIExtractText::getRunningStatusText(const QString& stepText) const
{
    // 356:13173: "Extract Text · reading page 640 of 1,592 · Stop ends the run"
    const PDFDocumentToolCatalogEntry* tool = getTool();
    QString step = getRunningCardTitle(0, stepText);
    if (!step.isEmpty())
    {
        step[0] = step[0].toLower();
    }
    return tr("%1 · %2 · Stop ends the run").arg(tool ? tool->getTitle() : QString(), step);
}

QString PDFAppToolGUIExtractText::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(percentage);

    // 356:13566: the step is the title, "Reading page 640 of 1,592"
    if (stepText.isEmpty())
    {
        return tr("Reading pages…");
    }
    QString title = stepText;
    title.replace(QStringLiteral(" · "), QStringLiteral(" "));
    return title;
}

QString PDFAppToolGUIExtractText::getRunningCardStepText(const QString& stepText) const
{
    Q_UNUSED(stepText);
    return QString();
}

QString PDFAppToolGUIExtractText::getRunningTimeText(int seconds) const
{
    // 356:13570: "About 20 s left · Stop ends the run, nothing is kept"
    if (seconds < 0)
    {
        return tr("Stop ends the run, nothing is kept");
    }
    if (seconds < 60)
    {
        return tr("About %1 s left · Stop ends the run, nothing is kept").arg(qMax(1, seconds));
    }
    return tr("About %1 min left · Stop ends the run, nothing is kept").arg((seconds + 59) / 60);
}

QString PDFAppToolGUIExtractText::getPermissionTitle() const
{
    return tr("This document does not allow copying");
}

QString PDFAppToolGUIExtractText::getPermissionSubtitle() const
{
    return tr("Its author restricted the extraction of text and images.");
}

void PDFAppToolGUIExtractText::fillResultInput(PDFAppToolResultInput* input) const
{
    // 339:9601
    const QString extension = getModel() ? PDFDocumentToolExtractText::getExtension(getModel()->getValues()) : QStringLiteral("txt");
    input->subtitle = tr("Nothing has been written yet – choose what to do with the text.");
    input->statusText = tr("%1 finished · the text is waiting for your decision").arg(getTool() ? getTool()->getTitle() : QString());
    input->fileDescription = tr("The text is written to a new .%1 file. The document is not changed.").arg(extension);
    input->fileConfirmText = tr("Save Text");
}

}   // namespace pdf

#include "moc_pdfapptoolgui_extracttext.cpp"
