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

#include "pdfapptoolgui_bitonal.h"

#include "pdfappdocument.h"
#include "pdfapplocale.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolimagecomparison.h"
#include "pdfapptoolimagelist.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfcms.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfpage.h"
#include "pdfrenderer.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QPushButton>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pdf
{

/// Helpers of the GUI of Convert to Black & White
class PDFAppToolGUIBitonalHelper
{
public:
    PDFAppToolGUIBitonalHelper() = delete;

    /// Panels of 306:5353: Source 280, Method 320, Compression 280; Mode 306:5615 380
    static constexpr int SOURCE_PANEL_WIDTH = 280;
    static constexpr int METHOD_PANEL_WIDTH = 320;
    static constexpr int COMPRESSION_PANEL_WIDTH = 280;
    static constexpr int MODE_PANEL_WIDTH = 380;

    /// The slot of the tiles of the pages of the left panel (304:5102: 110 x 150 tiles)
    static constexpr int TILE_SLOT_WIDTH = 82;
    static constexpr int TILE_SLOT_HEIGHT = 112;

    /// Field of a value of a menu (306:5353: 110 x 34)
    static constexpr int FIELD_WIDTH = 110;
    static constexpr int FIELD_HEIGHT = 34;

    /// The step of the resolution (306:5353 "24–600 dpi, step 25")
    static constexpr int DPI_STEP = 25;

    /// Property of a row: the value it chooses
    static constexpr const char* ROW_VALUE_PROPERTY = "pdf4qtBitonalValue";

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The modes in the order of the menu (306:5615)
    static std::vector<PDFBitonalDocumentCreator::ItemMode> getModes()
    {
        using Mode = PDFBitonalDocumentCreator::ItemMode;
        return { Mode::Algorithm, Mode::AlgorithmInverted, Mode::Original, Mode::FillBlack, Mode::FillWhite };
    }
};

// ------------------------------------------------------------------ PDFAppToolBitonalMenuPanel

PDFAppToolBitonalMenuPanel::PDFAppToolBitonalMenuPanel(PDFAppDocumentView* view, int panelWidth, QWidget* parent) :
    BaseClass(view, panelWidth, parent)
{

}

PDFAppToolBitonalMenuPanel::~PDFAppToolBitonalMenuPanel() = default;

void PDFAppToolBitonalMenuPanel::addContentWidget(QWidget* widget)
{
    widget->setParent(this);
    getContentLayout()->addWidget(widget);
}

// ------------------------------------------------------------------ PDFAppToolBitonalJobs

PDFBitonalDocumentCreator::Preview PDFAppToolBitonalJobs::createPreview(PDFDocumentPointer document, PDFCMSSettings cmsSettings, PDFBitonalDocumentCreator::PreviewRequest request, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    if (!document || cancel->isOperationCancelled())
    {
        return PDFBitonalDocumentCreator::Preview();
    }

    // The environment lives in this worker (created and destroyed here); only an image item,
    // or a page with its rendered page given, needs no rendering
    std::unique_ptr<PDFBitonalRasterizationEnvironment> environment;
    if (request.conversionSource == PDFBitonalDocumentCreator::ConversionSource::Pages && request.originalImage.isNull())
    {
        environment = std::make_unique<PDFBitonalRasterizationEnvironment>(document.data(), cmsSettings, PDFRasterizerPool::getDefaultRasterizerCount());
    }

    const PDFBitonalDocumentCreator creator(document.data(), environment ? environment->getRasterizerPool() : nullptr, nullptr);
    return creator.createPreview(request, cancel.get());
}

std::vector<qint64> PDFAppToolBitonalJobs::estimateSizes(PDFDocumentPointer document, PDFCMSSettings cmsSettings, std::vector<PDFBitonalDocumentCreator::PreviewRequest> requests, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    std::vector<qint64> sizes(requests.size(), -2);
    if (!document)
    {
        return sizes;
    }

    std::unique_ptr<PDFBitonalRasterizationEnvironment> environment;
    for (size_t index = 0; index < requests.size(); ++index)
    {
        if (cancel->isOperationCancelled())
        {
            break;
        }

        if (!environment && requests[index].conversionSource == PDFBitonalDocumentCreator::ConversionSource::Pages)
        {
            environment = std::make_unique<PDFBitonalRasterizationEnvironment>(document.data(), cmsSettings, PDFRasterizerPool::getDefaultRasterizerCount());
        }

        const PDFBitonalDocumentCreator creator(document.data(), environment ? environment->getRasterizerPool() : nullptr, nullptr);
        const PDFBitonalDocumentCreator::Preview preview = creator.createPreview(requests[index], cancel.get());
        if (!cancel->isOperationCancelled())
        {
            sizes[index] = preview.encodedSize;
        }
    }
    return sizes;
}

std::shared_ptr<const PDFDocumentImageList> PDFAppToolBitonalJobs::collectImages(PDFDocumentPointer document, std::shared_ptr<PDFAppCancelFlag> cancel)
{
    if (!document)
    {
        return nullptr;
    }

    PDFDocumentImageList list = PDFDocumentImageList::create(document.data(), PDFDocumentImageList::Options::createForChange(), cancel.get(), nullptr);
    if (cancel->isOperationCancelled() || !list.isComplete())
    {
        return nullptr;
    }
    return std::make_shared<const PDFDocumentImageList>(std::move(list));
}

std::vector<PDFBitonalDocumentCreator::BlankPageResult> PDFAppToolBitonalJobs::detectBlankPages(PDFDocumentPointer document, PDFCMSSettings cmsSettings, std::vector<PDFInteger> pageIndices,
                                                                                               std::shared_ptr<PDFAppCancelFlag> cancel, std::shared_ptr<PDFProgressCounter> progress)
{
    if (!document)
    {
        return { };
    }

    const PDFBitonalRasterizationEnvironment environment(document.data(), cmsSettings, PDFRasterizerPool::getDefaultRasterizerCount());
    const PDFBitonalDocumentCreator creator(document.data(), environment.getRasterizerPool(), nullptr);
    return creator.detectBlankPages(pageIndices, cancel.get(), progress.get());
}

// ------------------------------------------------------------------ PDFAppToolGUIBitonal

PDFAppToolGUIBitonal::PDFAppToolGUIBitonal(QObject* parent) :
    BaseClass(parent)
{
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(PREVIEW_DELAY);
    m_detectionTimer.setInterval(DETECTION_POLL_INTERVAL);

    connect(&m_previewTimer, &QTimer::timeout, this, &PDFAppToolGUIBitonal::onPreviewTimeout);
    connect(&m_detectionTimer, &QTimer::timeout, this, &PDFAppToolGUIBitonal::onDetectionPoll);
    connect(&m_imagesWatcher, &QFutureWatcherBase::finished, this, &PDFAppToolGUIBitonal::onImagesCollected);
    connect(&m_previewWatcher, &QFutureWatcherBase::finished, this, &PDFAppToolGUIBitonal::onPreviewFinished);
    connect(&m_estimateWatcher, &QFutureWatcherBase::finished, this, &PDFAppToolGUIBitonal::onEstimateFinished);
    connect(&m_detectionWatcher, &QFutureWatcherBase::finished, this, &PDFAppToolGUIBitonal::onDetectionFinished);
}

PDFAppToolGUIBitonal::~PDFAppToolGUIBitonal()
{
    // The workers hold only values and their own flags: they are told to stop and are not
    // waited for (their results go nowhere, the watchers die with the GUI)
    cancelJobs();

    if (m_question)
    {
        // The question belongs to the window: it goes with the tool
        m_question->deleteLater();
    }
}

void PDFAppToolGUIBitonal::registerBitonalTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolBitonal::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolBitonal>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIBitonal>>());
    }
}

// ------------------------------------------------------------------ parts

QToolButton* PDFAppToolGUIBitonal::getSourceButton() const { return m_sourceButton.data(); }
QToolButton* PDFAppToolGUIBitonal::getMethodButton() const { return m_methodButton.data(); }
QToolButton* PDFAppToolGUIBitonal::getCompressionButton() const { return m_compressionButton.data(); }
QToolButton* PDFAppToolGUIBitonal::getModeButton() const { return m_modeButton.data(); }
QToolButton* PDFAppToolGUIBitonal::getBlankPagesButton() const { return m_blankPagesButton.data(); }
PDFAppToolBitonalMenuPanel* PDFAppToolGUIBitonal::getSourcePanel() const { return m_sourcePanel.data(); }
PDFAppToolBitonalMenuPanel* PDFAppToolGUIBitonal::getMethodPanel() const { return m_methodPanel.data(); }
PDFAppToolBitonalMenuPanel* PDFAppToolGUIBitonal::getCompressionPanel() const { return m_compressionPanel.data(); }
PDFAppToolBitonalMenuPanel* PDFAppToolGUIBitonal::getModePanel() const { return m_modePanel.data(); }
QSpinBox* PDFAppToolGUIBitonal::getResolutionSpinBox() const { return m_resolutionSpinBox.data(); }
QSpinBox* PDFAppToolGUIBitonal::getThresholdSpinBox() const { return m_thresholdSpinBox.data(); }
PDFAppToolPanelHeader* PDFAppToolGUIBitonal::getLeftHeader() const { return m_leftHeader.data(); }
PDFAppToolPageGrid* PDFAppToolGUIBitonal::getPageGrid() const { return m_pageGrid.data(); }
PDFAppToolImageList* PDFAppToolGUIBitonal::getImageList() const { return m_imageListWidget.data(); }
PDFAppToolImageComparison* PDFAppToolGUIBitonal::getComparison() const { return m_comparison.data(); }
PDFLabel* PDFAppToolGUIBitonal::getCanvasTitle() const { return m_canvasTitle.data(); }
PDFAppToolSectionHeader* PDFAppToolGUIBitonal::getSelectedHeader() const { return m_selectedHeader.data(); }
PDFKeyValueList* PDFAppToolGUIBitonal::getSelectedRows() const { return m_selectedRows.data(); }
PDFKeyValueList* PDFAppToolGUIBitonal::getSummaryRows() const { return m_summaryRows.data(); }
PDFAppToolCallout* PDFAppToolGUIBitonal::getCallout() const { return m_callout.data(); }
PDFMessageBox* PDFAppToolGUIBitonal::getQuestion() const { return m_question.data(); }

// ------------------------------------------------------------------ state

PDFDocumentPointer PDFAppToolGUIBitonal::getShownDocument() const
{
    const PDFAppDocument* document = getDocument();
    return document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
}

QVariant PDFAppToolGUIBitonal::getParameter(const char* name) const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(QString::fromLatin1(name)) : QVariant();
}

bool PDFAppToolGUIBitonal::isPageSource() const
{
    return getParameter(PDFDocumentToolBitonal::PARAMETER_SOURCE).toString() != QLatin1String(PDFDocumentToolBitonal::SOURCE_IMAGES);
}

int PDFAppToolGUIBitonal::getItemCount() const
{
    if (isPageSource())
    {
        const PDFDocumentPointer document = getShownDocument();
        return document ? int(document->getCatalog()->getPageCount()) : 0;
    }
    return int(m_convertibleImages.size());
}

int PDFAppToolGUIBitonal::getEffectiveDpi() const
{
    const int dpi = getParameter(PDFDocumentToolBitonal::PARAMETER_DPI).toInt();
    if (dpi > 0)
    {
        return dpi;
    }

    // 306:5353 "the default is estimated from the images of the document": the estimate of the
    // version shown, once per version (it reads the resources of the pages)
    const PDFDocumentPointer document = getShownDocument();
    if (!document)
    {
        return PDFBitonalDocumentCreator::DEFAULT_DPI_RESOLUTION;
    }

    if (m_estimatedDpiDocument != document)
    {
        m_estimatedDpiDocument = document;
        m_estimatedDpi = PDFBitonalDocumentCreator(document.data(), nullptr, nullptr).getEstimatedDpiResolution();
    }
    return m_estimatedDpi;
}

const std::map<int, PDFAppToolGUIBitonal::ItemMode>& PDFAppToolGUIBitonal::getModesOfItems() const
{
    // The text is parsed once per value (the modes are asked for every item of a document of
    // thousands of pages): the cache is checked without a copy (the text of the model is shared)
    const bool isPages = isPageSource();
    const QString itemsText = getParameter(PDFDocumentToolBitonal::PARAMETER_ITEMS).toString();
    if (m_isModesCacheValid && isPages == m_modesCacheIsPages && m_modesCacheListGeneration == m_imageListGeneration &&
        (itemsText.isSharedWith(m_modesCacheText) || itemsText == m_modesCacheText))
    {
        return m_modesCache;
    }

    m_isModesCacheValid = true;
    m_modesCacheIsPages = isPages;
    m_modesCacheText = itemsText;
    m_modesCacheListGeneration = m_imageListGeneration;
    m_modesCache.clear();

    std::map<int, ItemMode> modes;
    const std::optional<std::vector<PDFDocumentToolBitonal::ItemEntry>> entries = PDFDocumentToolBitonal::parseItems(itemsText, nullptr);
    if (!entries)
    {
        return m_modesCache;
    }

    std::map<PDFObjectReference, int> imageItems;
    if (!isPages && m_imageList)
    {
        for (size_t index = 0; index < m_convertibleImages.size(); ++index)
        {
            imageItems[m_imageList->getItems()[size_t(m_convertibleImages[index])].key.reference] = int(index);
        }
    }

    for (const PDFDocumentToolBitonal::ItemEntry& entry : *entries)
    {
        if (isPages && entry.isPage())
        {
            modes[int(entry.pageIndex)] = entry.mode;
        }
        else if (!isPages && !entry.isPage())
        {
            auto it = imageItems.find(entry.image.reference);
            if (it != imageItems.end())
            {
                modes[it->second] = entry.mode;
            }
        }
    }

    m_modesCache = std::move(modes);
    return m_modesCache;
}

PDFAppToolGUIBitonal::ItemMode PDFAppToolGUIBitonal::getItemMode(int itemIndex) const
{
    const std::map<int, ItemMode>& modes = getModesOfItems();
    auto it = modes.find(itemIndex);
    return it != modes.end() ? it->second : ItemMode::Algorithm;
}

std::optional<PDFDocumentImageList::Key> PDFAppToolGUIBitonal::getImageKey(int itemIndex) const
{
    const int listIndex = getImageListIndex(itemIndex);
    return listIndex >= 0 ? std::optional<PDFDocumentImageList::Key>(m_imageList->getItems()[size_t(listIndex)].key) : std::nullopt;
}

int PDFAppToolGUIBitonal::getImageListIndex(int itemIndex) const
{
    if (!m_imageList || itemIndex < 0 || size_t(itemIndex) >= m_convertibleImages.size())
    {
        return -1;
    }
    return m_convertibleImages[size_t(itemIndex)];
}

void PDFAppToolGUIBitonal::selectItems(std::vector<int> items, int current)
{
    const int itemCount = getItemCount();
    std::set<int> validItems;
    for (const int item : items)
    {
        if (item >= 0 && item < itemCount)
        {
            validItems.insert(item);
        }
    }

    m_selectedItems.assign(validItems.cbegin(), validItems.cend());
    m_currentItem = current >= 0 && current < itemCount ? current : (m_selectedItems.empty() ? -1 : m_selectedItems.back());
    if (m_anchorItem < 0 || m_selectedItems.size() <= 1)
    {
        m_anchorItem = m_currentItem;
    }

    updateTiles();
    updateLeftPanel();
    updateRightPanel();
    updateCanvas();
    schedulePreview();
}

void PDFAppToolGUIBitonal::setModeToItems(ItemMode mode)
{
    PDFAppToolParameterModel* model = getModel();
    const int itemCount = getItemCount();
    if (!model || itemCount == 0)
    {
        return;
    }

    std::map<int, ItemMode> modes = getModesOfItems();
    std::vector<int> items = m_selectedItems;
    if (items.empty())
    {
        // 304:5102 "With nothing selected, a mode applies to the whole document"
        items.resize(size_t(itemCount));
        for (int item = 0; item < itemCount; ++item)
        {
            items[size_t(item)] = item;
        }
    }

    for (const int item : items)
    {
        modes[item] = mode;
    }

    // The text says only the items, which are not converted by the method (the default)
    std::vector<PDFDocumentToolBitonal::ItemEntry> entries;
    for (const auto& [item, itemMode] : modes)
    {
        if (itemMode == ItemMode::Algorithm)
        {
            continue;
        }

        PDFDocumentToolBitonal::ItemEntry entry;
        entry.mode = itemMode;
        if (isPageSource())
        {
            entry.pageIndex = item;
        }
        else if (const std::optional<PDFDocumentImageList::Key> key = getImageKey(item))
        {
            entry.image = *key;
        }
        else
        {
            continue;
        }
        entries.push_back(entry);
    }

    // One undo step; the model announces the change (onParametersChanged updates the parts)
    model->setValues({ { PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_ITEMS), PDFDocumentToolBitonal::createItemsText(entries) } }, getModeTitle(mode));
}

bool PDFAppToolGUIBitonal::isCollectingImages() const
{
    return m_imagesCancel != nullptr && m_imagesWatcher.isRunning();
}

bool PDFAppToolGUIBitonal::isPreviewPending() const
{
    return m_previewTimer.isActive() || m_previewWatcher.isRunning();
}

bool PDFAppToolGUIBitonal::isEstimateComplete() const
{
    const std::map<int, ItemMode> modes = getModesOfItems();
    for (int item = 0, count = getItemCount(); item < count; ++item)
    {
        auto it = modes.find(item);
        const ItemMode mode = it != modes.end() ? it->second : ItemMode::Algorithm;
        if (mode == ItemMode::Original || (isPageSource() && (mode == ItemMode::FillBlack || mode == ItemMode::FillWhite)))
        {
            continue;
        }
        if (!m_estimates.count(getEstimateKey(item)))
        {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------ texts

QString PDFAppToolGUIBitonal::getModeTitle(ItemMode mode)
{
    switch (mode)
    {
        case ItemMode::Algorithm:
            return tr("Convert using the selected method");
        case ItemMode::AlgorithmInverted:
            return tr("Convert using the selected method, inverted");
        case ItemMode::Original:
            return tr("Leave unchanged");
        case ItemMode::FillBlack:
            return tr("Fill with black");
        case ItemMode::FillWhite:
            return tr("Fill with white");
    }
    return QString();
}

QString PDFAppToolGUIBitonal::getModeDescription(ItemMode mode)
{
    switch (mode)
    {
        case ItemMode::Algorithm:
            return tr("Convert this item to the bitonal format using the selected conversion method");
        case ItemMode::AlgorithmInverted:
            return tr("Convert, then swap the black and white pixels");
        case ItemMode::Original:
            return tr("Leave this item unchanged");
        case ItemMode::FillBlack:
            return tr("Replace this item with a black area");
        case ItemMode::FillWhite:
            return tr("Replace this item with a white area");
    }
    return QString();
}

QString PDFAppToolGUIBitonal::getModeLabel(ItemMode mode)
{
    // The labels of the tiles (304:5102 unchanged, white, inverted); converted items have none
    switch (mode)
    {
        case ItemMode::Algorithm:
            return QString();
        case ItemMode::AlgorithmInverted:
            return tr("inverted");
        case ItemMode::Original:
            return tr("unchanged");
        case ItemMode::FillBlack:
            return tr("black");
        case ItemMode::FillWhite:
            return tr("white");
    }
    return QString();
}

QString PDFAppToolGUIBitonal::getModeCaption(ItemMode mode)
{
    // The captions of the preview (306:5615)
    switch (mode)
    {
        case ItemMode::Algorithm:
            return tr("BITONAL");
        case ItemMode::AlgorithmInverted:
            return tr("BITONAL INVERTED");
        case ItemMode::Original:
            return tr("UNCHANGED");
        case ItemMode::FillBlack:
            return tr("FILLED BLACK");
        case ItemMode::FillWhite:
            return tr("FILLED WHITE");
    }
    return QString();
}

QString PDFAppToolGUIBitonal::getModeIconName(ItemMode mode)
{
    switch (mode)
    {
        case ItemMode::Algorithm:
            return QStringLiteral("circle");
        case ItemMode::AlgorithmInverted:
            return QStringLiteral("swap");
        case ItemMode::Original:
            return QStringLiteral("lock");
        case ItemMode::FillBlack:
            return QStringLiteral("square");
        case ItemMode::FillWhite:
            return QStringLiteral("square-round");
    }
    return QString();
}

QString PDFAppToolGUIBitonal::createNumbersText(std::vector<int> numbers)
{
    // 307:6250 "4, 9–12, 57, 88, 119": consecutive numbers collapse into a range, a long list
    // is cut (a panel and a message box do not grow over the screen) - the one text of the page
    // lists (owner D3) with the numbers of the locale of the application (RS4APP)
    constexpr int MAXIMUM_GROUPS = 40;
    std::vector<PDFInteger> indices;
    indices.reserve(numbers.size());
    for (const int number : numbers)
    {
        indices.push_back(PDFInteger(number) - 1);
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return PDFAppLocale::formatPageNumbers(indices, MAXIMUM_GROUPS);
}

QString PDFAppToolGUIBitonal::getFooterText() const
{
    // 304:5102 "Automatic threshold (Otsu): 141  the preview updates shortly after the settings change"
    return QStringLiteral("%1   %2").arg(getFooterValueText(), getFooterNoteText());
}

QString PDFAppToolGUIBitonal::getFooterNoteText()
{
    // The muted part of the line below the cards (304:5102)
    return tr("the preview updates shortly after the settings change");
}

QString PDFAppToolGUIBitonal::getFooterValueText() const
{
    // The leading part of the line below the cards: the method and its threshold (304:5102,
    // 306:5615 "User-defined threshold: 128")
    const QString method = getParameter(PDFDocumentToolBitonal::PARAMETER_METHOD).toString();
    const int threshold = getParameter(PDFDocumentToolBitonal::PARAMETER_THRESHOLD).toInt();

    QString value;
    if (method == QLatin1String(PDFDocumentToolBitonal::METHOD_THRESHOLD))
    {
        value = tr("User-defined threshold: %1").arg(PDFAppLocale::formatNumber(threshold));
    }
    else if (method == QLatin1String(PDFDocumentToolBitonal::METHOD_ADAPTIVE))
    {
        value = tr("Adaptive thresholding");
    }
    else if (method == QLatin1String(PDFDocumentToolBitonal::METHOD_DITHER))
    {
        value = tr("Dithering (Floyd-Steinberg), threshold %1").arg(PDFAppLocale::formatNumber(threshold));
    }
    else
    {
        const bool hasThreshold = m_preview && m_preview->threshold >= 0 && m_shownPreviewKey == m_previewKey;
        value = hasThreshold ? tr("Automatic threshold (Otsu): %1").arg(PDFAppLocale::formatNumber(m_preview->threshold)) : tr("Automatic threshold (Otsu)");
    }
    return value;
}

void PDFAppToolGUIBitonal::onShowOnPage()
{
    // tb-show-on-page 304:5102: the page of the item (a page, or the first page of the image) in
    // the thumbnails of the document; the canvas stays the comparison (as Optimize Images)
    PDFInteger pageIndex = -1;
    if (isPageSource())
    {
        pageIndex = m_currentItem;
    }
    else if (const int listIndex = getImageListIndex(m_currentItem); listIndex >= 0 && m_imageList && !m_imageList->getItems()[size_t(listIndex)].pageIndices.empty())
    {
        pageIndex = m_imageList->getItems()[size_t(listIndex)].pageIndices.front();
    }
    PDFAppToolHost* host = getHost();
    if (pageIndex < 0 || !host)
    {
        return;
    }

    // The panel runs foreign code: the host is taken again after it
    QPointer<PDFAppToolGUIBitonal> guard(this);
    host->showPanel(QStringLiteral("thumbnails"));
    host = guard ? getHost() : nullptr;
    if (PDFAppDocumentView* view = host ? host->getView() : nullptr)
    {
        view->goToPage(pageIndex);
    }
}

QString PDFAppToolGUIBitonal::getConfirmText() const
{
    // Owner 13. 11.: the title of the toolbar says the target, the button "Convert"
    return tr("Convert");
}

bool PDFAppToolGUIBitonal::isConfirmEnabled() const
{
    // The detection is about to change the modes: a conversion now would not be the one asked for
    if (m_isDetecting || !BaseClass::isConfirmEnabled())
    {
        return false;
    }
    // A document without pages has nothing to convert (RS4APP; the core refuses it as NothingToDo)
    return isPageSource() ? getItemCount() > 0 : (m_imageList && !m_convertibleImages.empty());
}

QString PDFAppToolGUIBitonal::getStatusText() const
{
    if (m_isDetecting)
    {
        // 307:5873 (K4.13 "click Stop to cancel")
        return tr("Detecting blank pages · %1 of %Ln pages examined at %2 dpi · click Stop to cancel", nullptr, m_pagesToExamineCount)
                   .arg(PDFAppLocale::formatNumber(m_examinedPageCount), PDFAppLocale::formatNumber(PDFBitonalDocumentCreator::BLANK_PAGE_DPI_RESOLUTION));
    }
    return BaseClass::getStatusText();
}

QString PDFAppToolGUIBitonal::getStatusDetailText() const
{
    // 304:5102 "120 pages · right-click the items to change their mode", 306:5615 "34 images · …"
    const int count = getItemCount();
    QString items;
    if (isPageSource())
    {
        items = count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count);
    }
    else
    {
        items = count == 1 ? tr("%Ln image", nullptr, count) : tr("%Ln images", nullptr, count);
    }
    return tr("%1 · right-click the items to change their mode").arg(items);
}

// ------------------------------------------------------------------ parts

QToolButton* PDFAppToolGUIBitonal::addMenuButton(PDFAppToolPanelBuilder& builder, const QString& objectName, const QString& iconName, QPointer<PDFAppToolBitonalMenuPanel>* panel, int panelWidth)
{
    // As PDFAppToolPanelBuilder::addMenu, with a panel, which takes a field below its rows; the
    // panel belongs to its button (the toolbar of the tool)
    QToolButton* button = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("toolMenu-") + objectName, iconName, builder.getWidget());
    builder.addWidget(button);

    const PDFAppToolHost* host = getHost();
    PDFAppToolBitonalMenuPanel* menuPanel = new PDFAppToolBitonalMenuPanel(host ? host->getView() : nullptr, panelWidth, button);
    menuPanel->setObjectName(QStringLiteral("toolPanel-") + objectName);
    if (PDFAppToolToolbar* toolbar = getToolbar())
    {
        toolbar->addPanel(button, menuPanel);
    }
    *panel = menuPanel;
    return button;
}

void PDFAppToolGUIBitonal::addEnumRows(PDFAppToolBitonalMenuPanel* panel, const char* parameter, const QStringList& iconNames, std::map<QString, QPointer<PDFTileRow>>* rows)
{
    rows->clear();
    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), QString::fromLatin1(parameter)) : nullptr;
    if (!definition)
    {
        return;
    }

    int index = 0;
    for (const PDFDocumentToolParameterOption& option : definition->getOptions())
    {
        PDFTileRow* row = panel->addRow(option.getTitle(), option.getDescription(), iconNames.value(index++), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("toolOption-") + option.id);
        row->setProperty(PDFAppToolGUIBitonalHelper::ROW_VALUE_PROPERTY, option.id);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        (*rows)[option.id] = row;
    }
}

QSpinBox* PDFAppToolGUIBitonal::createSpinRow(PDFAppToolBitonalMenuPanel* panel, const QString& label, const QString& suffix, int minimum, int maximum, int step, const QString& help)
{
    using Helper = PDFAppToolGUIBitonalHelper;

    panel->addDividerRow();

    // row 306:5353: the label Regular 13 and the field at the trailing edge
    QWidget* row = new QWidget(panel);
    row->setFixedHeight(Helper::FIELD_HEIGHT);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(8, 0, 8, 0);
    PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Normal, 13, PDFThemeColors::TextPrimary, row);
    rowLayout->addWidget(labelWidget, 1, Qt::AlignVCenter);

    // The spin box of Fluent (its text follows the theme, a plain QSpinBox reads the palette); the
    // unit drawn muted beside the value (306:5353 "300 dpi")
    PDFSpinBox* spinBox = new PDFSpinBox(row);
    spinBox->setRange(minimum, maximum);
    spinBox->setSingleStep(step);
    spinBox->setDisplaySuffix(suffix);
    spinBox->setFixedSize(Helper::FIELD_WIDTH, Helper::FIELD_HEIGHT);
    spinBox->setAccessibleName(label);
    spinBox->setKeyboardTracking(false);
    rowLayout->addWidget(spinBox, 0, Qt::AlignVCenter);
    panel->addContentWidget(row);

    PDFAppParagraph* helpText = PDFAppPageWidgets::createText(help, PDFThemeFonts::getFont(QFont::Normal, 11), 15, PDFThemeColors::TextMuted, panel);
    helpText->setContentsMargins(8, 0, 8, 0);
    panel->addContentWidget(helpText);
    return spinBox;
}

void PDFAppToolGUIBitonal::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIBitonalHelper;

    // tool-actions 304:5102: Source, Method, Compression | Mode, Blank pages; the menus of
    // 306:5353 have the rows of the options and their fields, the button the short value
    // (the icon of the pages of 304:5102 / 306:5353 is "pages")
    m_sourceButton = addMenuButton(builder, QStringLiteral("source"), QStringLiteral("pages"), &m_sourcePanel, Helper::SOURCE_PANEL_WIDTH);
    m_sourcePanel->addCaption(tr("Convert"));
    addEnumRows(m_sourcePanel, PDFDocumentToolBitonal::PARAMETER_SOURCE, { QStringLiteral("image"), QStringLiteral("pages") }, &m_sourceRows);
    m_resolutionSpinBox = createSpinRow(m_sourcePanel, tr("Resolution"), tr("dpi"), PDFDocumentToolBitonal::MINIMUM_DPI, PDFDocumentToolBitonal::MAXIMUM_DPI, Helper::DPI_STEP,
                                        tr("24–600 dpi, step 25 · the default is estimated from the images of the document · used for whole pages only"));

    m_methodButton = addMenuButton(builder, QStringLiteral("method"), QStringLiteral("sliders"), &m_methodPanel, Helper::METHOD_PANEL_WIDTH);
    m_methodPanel->addCaption(tr("Conversion method"));
    addEnumRows(m_methodPanel, PDFDocumentToolBitonal::PARAMETER_METHOD, { QStringLiteral("zap"), QStringLiteral("sliders"), QStringLiteral("bar-chart-2"), QStringLiteral("grid") }, &m_methodRows);
    m_thresholdSpinBox = createSpinRow(m_methodPanel, tr("Threshold (0-255)"), QString(), 0, 255, 1, tr("Used by the user-defined threshold and by the dithering"));

    m_compressionButton = addMenuButton(builder, QStringLiteral("compression"), QStringLiteral("minimize-2"), &m_compressionPanel, Helper::COMPRESSION_PANEL_WIDTH);
    m_compressionPanel->addCaption(tr("Image compression"));
    addEnumRows(m_compressionPanel, PDFDocumentToolBitonal::PARAMETER_COMPRESSION,
                { QStringLiteral("compress-auto"), QStringLiteral("compress-flate"), QStringLiteral("compress-rle"), QStringLiteral("compress-fax"), QStringLiteral("compress-bitmap") }, &m_compressionRows);

    builder.addActionDivider();

    // Mode 306:5615: the caption says, to what it applies ("Selected items (3)" / "All items")
    m_modeButton = addMenuButton(builder, QStringLiteral("mode"), QStringLiteral("edit"), &m_modePanel, Helper::MODE_PANEL_WIDTH);
    m_modeButton->setText(tr("Mode"));
    m_modeButton->setToolTip(tr("Mode"));
    m_modeCaption = m_modePanel->addCaption(QString());
    m_modeRows.clear();
    for (const ItemMode mode : Helper::getModes())
    {
        PDFTileRow* row = m_modePanel->addRow(getModeTitle(mode), getModeDescription(mode), getModeIconName(mode), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("toolMode-") + PDFDocumentToolBitonal::getModeId(mode));
        row->setProperty(Helper::ROW_VALUE_PROPERTY, int(mode));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIBitonal::onModeRowClicked);
        m_modeRows[int(mode)] = row;
    }
    PDFAppParagraph* modeNote = PDFAppPageWidgets::createText(tr("The same menu opens on right-click in the list. With nothing selected the mode applies to all items."),
                                                              PDFThemeFonts::getFont(QFont::Normal, 11), 15, PDFThemeColors::TextMuted, m_modePanel);
    modeNote->setContentsMargins(8, 0, 8, 0);
    m_modePanel->addContentWidget(modeNote);

    m_blankPagesButton = builder.addAction(tr("Blank pages"), QStringLiteral("file"), this, &PDFAppToolGUIBitonal::onBlankPagesClicked);
    m_blankPagesButton->setObjectName(QStringLiteral("toolAction-blankPages"));

    for (const auto& [id, row] : m_sourceRows)
    {
        connect(row.data(), &PDFTileRow::clicked, this, &PDFAppToolGUIBitonal::onSourceRowClicked);
    }
    for (const auto& [id, row] : m_methodRows)
    {
        connect(row.data(), &PDFTileRow::clicked, this, &PDFAppToolGUIBitonal::onMethodRowClicked);
    }
    for (const auto& [id, row] : m_compressionRows)
    {
        connect(row.data(), &PDFTileRow::clicked, this, &PDFAppToolGUIBitonal::onCompressionRowClicked);
    }
    connect(m_resolutionSpinBox.data(), &QSpinBox::valueChanged, this, &PDFAppToolGUIBitonal::onResolutionChanged);
    connect(m_thresholdSpinBox.data(), &QSpinBox::valueChanged, this, &PDFAppToolGUIBitonal::onThresholdChanged);

    updateToolbar();
}

void PDFAppToolGUIBitonal::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIBitonalHelper;

    // 304:5102: "Pages" and "120 pages · 3 selected", the tiles with the labels of the modes, the
    // hint below; 306:5615 the images of the document
    m_leftHeader = builder.addPanelHeader(QString(), QString());
    m_pageGrid = nullptr;
    m_imageListWidget = nullptr;

    // Both lists are built (the shell builds the parts once); the source shows one of them
    const PDFAppToolHost* host = getHost();
    PDFAppToolPageGrid* grid = new PDFAppToolPageGrid(QSize(Helper::TILE_SLOT_WIDTH, Helper::TILE_SLOT_HEIGHT), builder.getWidget());
    grid->setObjectName(QStringLiteral("bitonalPageGrid"));
    grid->setTileSize(PDFAppToolPageGrid::TileSize::Small);
    grid->setHintPlacement(false, 12);
    grid->getHeaderLabel()->setVisible(false);

    // 304:5102: the tiles lie on the side panel, not on the gray canvas of a tool (RS4FIGMA)
    grid->setCanvasPainted(false);
    const PDFAppDocument* document = getDocument();
    grid->setDocument(getShownDocument(), document ? document->getId() : QString(), host ? host->getView() : nullptr);
    grid->getView()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(grid, &PDFAppToolPageGrid::pageClicked, this, &PDFAppToolGUIBitonal::onGridPageClicked);
    connect(grid->getView(), &QWidget::customContextMenuRequested, this, &PDFAppToolGUIBitonal::onItemContextMenuRequested);
    builder.addWidget(grid);
    builder.setFillsPanel(grid);
    m_pageGrid = grid;

    PDFAppToolImageList* list = new PDFAppToolImageList(builder.getWidget());
    list->setObjectName(QStringLiteral("bitonalImageList"));
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &PDFAppToolImageList::selectionChanged, this, &PDFAppToolGUIBitonal::onImageListSelectionChanged);
    connect(list, &PDFAppToolImageList::markToggled, this, &PDFAppToolGUIBitonal::onImageMarksToggled);
    connect(list, &QWidget::customContextMenuRequested, this, &PDFAppToolGUIBitonal::onItemContextMenuRequested);
    builder.addWidget(list);
    builder.setFillsPanel(list);
    m_imageListWidget = list;

    m_leftHint = builder.addHint(tr("Ctrl or Shift selects more items. With nothing selected, a mode applies to the whole document. Right-click for the mode menu."));

    updateLeftPanel();
    updateTiles();
}

void PDFAppToolGUIBitonal::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // 304:5102: Selected items (Pages / Mode / Rasterized at), Summary (the five modes and the
    // estimated size), the callout of the source
    m_selectedHeader = builder.addSection(tr("Selected items"));
    m_selectedRows = builder.addKeyValues();
    m_selectedRows->setObjectName(QStringLiteral("bitonalSelectedRows"));

    builder.addSection(tr("Summary"));
    m_summaryRows = builder.addKeyValues();
    m_summaryRows->setObjectName(QStringLiteral("bitonalSummaryRows"));
    builder.endSection();

    m_callout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString());
    m_callout->setObjectName(QStringLiteral("bitonalCallout"));

    updateRightPanel();
    updateSummary();
}

QWidget* PDFAppToolGUIBitonal::createCanvasPage(QWidget* parent)
{
    // The canvas 304:5102 is the comparison itself (as 297:3135): the title of the item ("Page 3")
    // in its bar with Fit, 100 %, Sync zoom and Show on page, the cards ORIGINAL / BITONAL (never
    // mirrored), the line below the cards - the value and the muted note
    m_comparison = new PDFAppToolImageComparison(parent);
    m_comparison->setObjectName(QStringLiteral("bitonalComparison"));
    m_comparison->setSyncZoomVisible(true);
    m_comparison->setShowOnPageVisible(true);
    connect(m_comparison.data(), &PDFAppToolImageComparison::zoomChanged, this, &PDFAppToolGUIBitonal::onComparisonZoomChanged);
    connect(m_comparison.data(), &PDFAppToolImageComparison::showOnPageRequested, this, &PDFAppToolGUIBitonal::onShowOnPage);
    m_canvasTitle = m_comparison->getTitleLabel();
    m_canvas = m_comparison.data();
    QWidget* canvas = m_comparison.data();

    // The first state: nothing is emitted by the updates
    updateCanvas();
    if (!m_imageList && !m_imagesWatcher.isRunning())
    {
        startImageCollection();
    }
    schedulePreview();
    return canvas;
}

// ------------------------------------------------------------------ updates

void PDFAppToolGUIBitonal::updateAll()
{
    updateToolbar();
    updateLeftPanel();
    updateTiles();
    updateCanvas();
    updateRightPanel();
    updateSummary();
}

void PDFAppToolGUIBitonal::updateToolbar()
{
    // The short values of the buttons (304:5102 "Source: Pages", "Method: Auto", 306:5615
    // "Method: Threshold", "Compression: Auto")
    const QString source = getParameter(PDFDocumentToolBitonal::PARAMETER_SOURCE).toString();
    const QString method = getParameter(PDFDocumentToolBitonal::PARAMETER_METHOD).toString();
    const QString compression = getParameter(PDFDocumentToolBitonal::PARAMETER_COMPRESSION).toString();

    if (m_sourceButton)
    {
        m_sourceButton->setText(isPageSource() ? tr("Source: Pages") : tr("Source: Images"));
        m_sourceButton->setToolTip(m_sourceButton->text());
    }

    if (m_methodButton)
    {
        QString value = tr("Auto");
        if (method == QLatin1String(PDFDocumentToolBitonal::METHOD_THRESHOLD))
        {
            value = tr("Threshold");
        }
        else if (method == QLatin1String(PDFDocumentToolBitonal::METHOD_ADAPTIVE))
        {
            value = tr("Adaptive");
        }
        else if (method == QLatin1String(PDFDocumentToolBitonal::METHOD_DITHER))
        {
            value = tr("Dither");
        }
        m_methodButton->setText(tr("Method: %1").arg(value));
        m_methodButton->setToolTip(m_methodButton->text());
    }

    if (m_compressionButton)
    {
        const PDFBitonalDocumentCreator::Compression value = PDFDocumentToolBitonal::getCompression(compression);
        const QString name = value == PDFBitonalDocumentCreator::Compression::Auto ? tr("Auto") : PDFBitonalDocumentCreator::getCompressionName(value);
        m_compressionButton->setText(tr("Compression: %1").arg(name));
        m_compressionButton->setToolTip(m_compressionButton->text());
    }

    const std::pair<std::map<QString, QPointer<PDFTileRow>>*, QString> groups[] = { { &m_sourceRows, source }, { &m_methodRows, method }, { &m_compressionRows, compression } };
    for (const auto& [rows, value] : groups)
    {
        for (const auto& [id, row] : *rows)
        {
            if (row)
            {
                const QSignalBlocker blocker(row.data());
                row->setChecked(id == value);
            }
        }
    }

    m_isUpdatingControls = true;
    if (m_resolutionSpinBox)
    {
        // A value of the user, or the estimate of the document (0); the field is for pages only
        m_resolutionSpinBox->setValue(getEffectiveDpi());
        m_resolutionSpinBox->setEnabled(isPageSource());
    }
    if (m_thresholdSpinBox)
    {
        m_thresholdSpinBox->setValue(getParameter(PDFDocumentToolBitonal::PARAMETER_THRESHOLD).toInt());
        m_thresholdSpinBox->setEnabled(method == QLatin1String(PDFDocumentToolBitonal::METHOD_THRESHOLD) || method == QLatin1String(PDFDocumentToolBitonal::METHOD_DITHER));
    }
    m_isUpdatingControls = false;

    // Mode: the caption and the check of the mode, which all items it applies to have
    if (m_modeCaption)
    {
        m_modeCaption->setText(m_selectedItems.empty() ? tr("All items") : tr("Selected items (%1)").arg(PDFAppLocale::formatNumber(qint64(m_selectedItems.size()))));
    }

    std::optional<ItemMode> commonMode;
    bool isCommon = true;
    const std::map<int, ItemMode> modes = getModesOfItems();
    const int itemCount = getItemCount();
    for (int item = 0; item < itemCount && isCommon; ++item)
    {
        if (!m_selectedItems.empty() && !std::binary_search(m_selectedItems.cbegin(), m_selectedItems.cend(), item))
        {
            continue;
        }
        auto it = modes.find(item);
        const ItemMode mode = it != modes.end() ? it->second : ItemMode::Algorithm;
        isCommon = !commonMode || *commonMode == mode;
        commonMode = mode;
    }
    for (const auto& [mode, row] : m_modeRows)
    {
        if (row)
        {
            const QSignalBlocker blocker(row.data());
            row->setChecked(isCommon && commonMode && int(*commonMode) == mode);
        }
    }

    if (m_blankPagesButton)
    {
        // 307:5873: the button of the detection stops it ("Stop (37/120)"); only for the pages
        m_blankPagesButton->setText(m_isDetecting ? tr("Stop (%1/%2)").arg(PDFAppLocale::formatNumber(m_examinedPageCount), PDFAppLocale::formatNumber(m_pagesToExamineCount)) : tr("Blank pages"));
        m_blankPagesButton->setToolTip(m_isDetecting ? tr("Stop the detection of blank pages") : tr("Find pages, which are scans of a blank sheet"));
        m_blankPagesButton->setEnabled(m_isContentEnabled && isPageSource() && getItemCount() > 0);
    }

    // While the detection runs, the settings stay as they are (307:5873)
    for (QToolButton* button : { m_sourceButton.data(), m_methodButton.data(), m_compressionButton.data(), m_modeButton.data() })
    {
        if (button)
        {
            button->setEnabled(m_isContentEnabled && !m_isDetecting);
        }
    }
}

void PDFAppToolGUIBitonal::updateLeftPanel()
{
    const int count = getItemCount();
    if (m_leftHeader)
    {
        QString subtitle;
        if (isPageSource())
        {
            subtitle = count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count);
        }
        else if (!m_imageList)
        {
            subtitle = tr("Looking for images…");
        }
        else if (count == 0)
        {
            subtitle = tr("No image, which can be converted");
        }
        else
        {
            subtitle = count == 1 ? tr("%Ln image", nullptr, count) : tr("%Ln images", nullptr, count);
        }

        if (!m_selectedItems.empty())
        {
            subtitle = tr("%1 · %2 selected").arg(subtitle, PDFAppLocale::formatNumber(qint64(m_selectedItems.size())));
        }
        m_leftHeader->setTexts(isPageSource() ? tr("Pages") : tr("Images"), subtitle);

        // 306:5615: the list of the images has its own head "Images", the subtitle and Sort - one
        // head, not two
        m_leftHeader->setVisible(isPageSource());
        if (m_imageListWidget)
        {
            m_imageListWidget->setHeadText(subtitle);
        }
    }

    // The list of the source; the hint below it (the grid has its own place of a hint)
    if (m_pageGrid)
    {
        m_pageGrid->setVisible(isPageSource());
        m_pageGrid->setHintText(tr("Ctrl or Shift selects more items. With nothing selected, a mode applies to the whole document. Right-click for the mode menu."));
    }
    if (m_imageListWidget)
    {
        m_imageListWidget->setVisible(!isPageSource());
    }
    if (m_leftHint)
    {
        m_leftHint->setVisible(!isPageSource());
    }
}

void PDFAppToolGUIBitonal::updateTiles()
{
    const std::map<int, ItemMode> modes = getModesOfItems();

    if (m_pageGrid)
    {
        // The modes as the tags of the tiles (304:5102 "unchanged", "white", "inverted"), the selected tiles
        std::vector<PDFAppToolPageGridModel::Tile> tiles;
        tiles.resize(size_t(getItemCount()));
        for (size_t page = 0; page < tiles.size(); ++page)
        {
            auto it = modes.find(int(page));
            const ItemMode mode = it != modes.end() ? it->second : ItemMode::Algorithm;
            tiles[page].tag = getModeLabel(mode);
            tiles[page].state = std::binary_search(m_selectedItems.cbegin(), m_selectedItems.cend(), int(page)) ? PDFComponentPainter::PageTileState::Selected
                                                                                                                   : PDFComponentPainter::PageTileState::Normal;
        }
        m_pageGrid->setTiles(std::move(tiles));
    }

    if (m_imageListWidget && m_imageList)
    {
        PDFAppToolImageListModel* model = m_imageListWidget->getModel();
        if (model && model->getImageList() != m_imageList)
        {
            model->setImageList(m_imageList, m_imageListGeneration);

            // The thumbnails of the rows (306:5615), decoded from the shown document as in
            // Optimize Images
            const PDFAppToolHost* host = getHost();
            m_imageListWidget->setThumbnailSource(m_imageListDocument, host ? host->createEnvironment().cms : PDFCMSPointer());
        }

        // The images, which are not converted (inline images, stencil masks, masks), say why;
        // a converted image says its mode
        // (the item of a convertible image by its index in the list: 10 000 rows are not quadratic;
        // the states go in one batch)
        std::map<int, int> itemOfListIndex;
        for (size_t index = 0; index < m_convertibleImages.size(); ++index)
        {
            itemOfListIndex.emplace(m_convertibleImages[index], int(index));
        }
        std::vector<PDFAppToolImageListModel::RowState> states;
        states.reserve(m_imageList->getItems().size());
        for (size_t listIndex = 0; listIndex < m_imageList->getItems().size() && model; ++listIndex)
        {
            const PDFDocumentImageList::Item& item = m_imageList->getItems()[listIndex];
            PDFAppToolImageListModel::RowState state;
            const auto itemIt = itemOfListIndex.find(int(listIndex));
            if (item.kind == PDFDocumentImageList::Kind::Inline)
            {
                state.tag = PDFAppToolImageListModel::Tag::InlineKept;
                state.isMarked = false;
            }
            else if (item.isInAppearance)
            {
                // R4-4: an annotation appearance draws the image too, it is never converted
                state.tag = PDFAppToolImageListModel::Tag::AppearanceKept;
                state.isMarked = false;
            }
            else if (itemIt == itemOfListIndex.end())
            {
                state.tag = PDFAppToolImageListModel::Tag::Bitonal;
                state.isMarked = false;
            }
            else
            {
                auto it = modes.find(itemIt->second);
                // The mode is the Tag of the row (a converted item has none); the second line keeps
                // the size of the image
                state.badgeText = getModeLabel(it != modes.end() ? it->second : ItemMode::Algorithm);
            }
            states.push_back(std::move(state));
        }
        if (model)
        {
            model->setRowStates(std::move(states));
        }

        // The selection goes to the list only when it differs: a click of the user is already
        // there (setSelectedKeys moves the current row to the first one and scrolls to it - the
        // keyboard position and the scroll of the user would jump)
        std::vector<PDFDocumentImageList::Key> keys;
        for (const int item : m_selectedItems)
        {
            if (const std::optional<PDFDocumentImageList::Key> key = getImageKey(item))
            {
                keys.push_back(*key);
            }
        }
        std::vector<PDFDocumentImageList::Key> shownKeys = m_imageListWidget->getSelectedKeys();
        std::sort(keys.begin(), keys.end());
        std::sort(shownKeys.begin(), shownKeys.end());
        if (keys != shownKeys)
        {
            m_imageListWidget->setSelectedKeys(keys);
        }
    }
}

void PDFAppToolGUIBitonal::updateCanvas()
{
    if (!m_comparison)
    {
        return;
    }

    const int item = m_currentItem;
    const ItemMode mode = item >= 0 ? getItemMode(item) : ItemMode::Algorithm;
    const PDFDocumentPointer document = getShownDocument();

    // The numbers of the locale of the application (PDFAppLocale, O17)
    using Locale = PDFAppLocale;

    QString title;
    QString beforeDetail;
    QString afterTitle = getModeCaption(mode);
    QString afterDetail;

    if (item >= 0 && document && isPageSource())
    {
        // "Page 3 · 210 × 297 mm · 3 images"
        title = tr("Page %1").arg(Locale::formatNumber(item + 1));
        if (const PDFPage* page = document->getCatalog()->getPage(size_t(item)))
        {
            const QSizeF size = page->getMediaBox().size() * PDF_POINT_TO_MM;
            beforeDetail = tr("%1 · %2 × %3 mm").arg(title, Locale::formatNumber(qRound(size.width())), Locale::formatNumber(qRound(size.height())));
            if (m_imageList && m_imageListDocument == document)
            {
                int imageCount = 0;
                for (const PDFDocumentImageList::Item& image : m_imageList->getItems())
                {
                    imageCount += std::binary_search(image.pageIndices.cbegin(), image.pageIndices.cend(), PDFInteger(item)) ? 1 : 0;
                }
                beforeDetail = tr("%1 · %2").arg(beforeDetail, imageCount == 1 ? tr("%Ln image", nullptr, imageCount) : tr("%Ln images", nullptr, imageCount));
            }

            if (PDFBitonalDocumentCreator::isConversionMode(mode))
            {
                // "BITONAL – PREVIEW AT 300 DPI": the resolution of the preview
                const int previewDpi = m_preview && m_shownPreviewKey == m_previewKey && m_preview->resultImage.width() > 0 && page->getMediaBox().width() > 0
                                       ? qRound(m_preview->resultImage.width() / (page->getMediaBox().width() * PDF_POINT_TO_INCH)) : getEffectiveDpi();
                afterTitle = tr("%1 – PREVIEW AT %2 DPI").arg(afterTitle, Locale::formatNumber(previewDpi));

                // "2480 × 3508 px · 1 bit · JBIG2 · about 41 KB" of the converted page
                const QSize imageSize = PDFBitonalDocumentCreator::getPageImageSize(page, getEffectiveDpi());
                afterDetail = imageSize.isValid() ? tr("%1 × %2 px · 1 bit").arg(Locale::formatNumber(imageSize.width()), Locale::formatNumber(imageSize.height()))
                                                  : PDFBitonalDocumentCreator::getPageImageSizeError(page, item + 1, getEffectiveDpi());
            }
        }
    }
    else if (item >= 0 && m_imageList)
    {
        // "Image 3 · 2480 × 3508 px · DeviceRGB"
        const int listIndex = getImageListIndex(item);
        title = tr("Image %1").arg(Locale::formatNumber(listIndex + 1));
        if (listIndex >= 0)
        {
            const PDFDocumentImageList::Item& image = m_imageList->getItems()[size_t(listIndex)];
            beforeDetail = tr("%1 · %2 × %3 px · %4").arg(title, Locale::formatNumber(image.pixelSize.width()), Locale::formatNumber(image.pixelSize.height()), image.colorSpaceName);
            if (PDFBitonalDocumentCreator::isConversionMode(mode))
            {
                afterDetail = tr("%1 × %2 px · 1 bit").arg(Locale::formatNumber(image.pixelSize.width()), Locale::formatNumber(image.pixelSize.height()));
            }
        }
    }

    if (!afterDetail.isEmpty() && PDFBitonalDocumentCreator::isConversionMode(mode))
    {
        // The coding and the size of the encoded item: the estimate of the Summary
        auto it = m_estimates.find(getEstimateKey(item));
        if (m_preview && m_preview->compression && m_shownPreviewKey == m_previewKey)
        {
            afterDetail = tr("%1 · %2").arg(afterDetail, PDFBitonalDocumentCreator::getCompressionName(*m_preview->compression));
        }
        if (it != m_estimates.end() && it->second >= 0)
        {
            afterDetail = tr("%1 · about %2").arg(afterDetail, PDFDocumentToolFormat::formatSize(it->second));
        }
    }

    m_comparison->setTitle(item >= 0 ? title : tr("Select an item to see its preview"));

    if (item < 0)
    {
        m_comparison->setImages(QImage(), QImage());
    }
    else if (m_preview && m_shownPreviewKey == m_previewKey)
    {
        m_comparison->setImages(m_preview->originalImage, m_preview->resultImage);
    }
    m_comparison->setCaptions(tr("ORIGINAL"), beforeDetail, afterTitle, afterDetail);
    m_comparison->setFooterTexts(getFooterValueText(), getFooterNoteText());
}

void PDFAppToolGUIBitonal::updateRightPanel()
{
    const std::map<int, ItemMode> modes = getModesOfItems();
    const int itemCount = getItemCount();

    if (m_selectedHeader)
    {
        m_selectedHeader->setHint(tr("%1 of %2").arg(PDFAppLocale::formatNumber(qint64(m_selectedItems.size())), PDFAppLocale::formatNumber(itemCount)));
    }

    if (m_selectedRows)
    {
        // "Pages 3, 5, 6", "Mode 2 convert, 1 inverted", "Rasterized at 300 dpi" / "original"
        std::vector<int> numbers;
        std::map<ItemMode, int> counts;
        for (const int item : m_selectedItems)
        {
            numbers.push_back(isPageSource() ? item + 1 : getImageListIndex(item) + 1);
            auto it = modes.find(item);
            ++counts[it != modes.end() ? it->second : ItemMode::Algorithm];
        }

        QStringList modeParts;
        const std::pair<ItemMode, QString> modeTexts[] = { { ItemMode::Algorithm, tr("%1 convert") }, { ItemMode::AlgorithmInverted, tr("%1 inverted") }, { ItemMode::Original, tr("%1 unchanged") },
                                                           { ItemMode::FillWhite, tr("%1 white") }, { ItemMode::FillBlack, tr("%1 black") } };
        for (const auto& [mode, text] : modeTexts)
        {
            if (counts[mode] > 0)
            {
                modeParts << text.arg(PDFAppLocale::formatNumber(counts[mode]));
            }
        }

        const QString none = tr("None – a mode applies to all items");
        m_selectedRows->clear();
        m_selectedRows->addRow(isPageSource() ? tr("Pages") : tr("Images"), numbers.empty() ? none : createNumbersText(numbers));
        m_selectedRows->addRow(tr("Mode"), modeParts.isEmpty() ? QStringLiteral("–") : modeParts.join(QStringLiteral(", ")));
        m_selectedRows->addRow(tr("Rasterized at"), isPageSource() ? tr("%1 dpi").arg(PDFAppLocale::formatNumber(getEffectiveDpi())) : tr("original"));
    }

    if (m_callout)
    {
        // 304:5102 for the pages, 306:5615 for the images
        if (isPageSource())
        {
            m_callout->setTexts(tr("Converted pages become images"),
                                tr("Their text can no longer be selected or searched and their tags are removed; annotations and form fields stay. Run Recognize Text (OCR) afterwards to get the text back."));
        }
        else
        {
            m_callout->setTexts(tr("Converted images lose their colors"),
                                tr("Only the images are replaced by 1-bit versions. Text and vector graphics of the pages stay as they are and remain selectable."));
        }
    }

    updateSummary();
}

qint64 PDFAppToolGUIBitonal::getOriginalItemBytes(int itemIndex) const
{
    const int listIndex = getImageListIndex(itemIndex);
    if (isPageSource() || listIndex < 0)
    {
        return 0;
    }
    const PDFDocumentImageList::Item& item = m_imageList->getItems()[size_t(listIndex)];
    return item.streamBytes + item.maskBytes;
}

void PDFAppToolGUIBitonal::updateSummary()
{
    if (!m_summaryRows)
    {
        return;
    }

    const std::map<int, ItemMode> modes = getModesOfItems();
    const int itemCount = getItemCount();
    std::map<ItemMode, int> counts;
    std::set<int> replacedPages;
    qint64 encodedBytes = 0;
    qint64 removedBytes = 0;
    for (int item = 0; item < itemCount; ++item)
    {
        auto it = modes.find(item);
        const ItemMode mode = it != modes.end() ? it->second : ItemMode::Algorithm;
        ++counts[mode];
        if (mode == ItemMode::Original)
        {
            continue;
        }

        replacedPages.insert(item);
        removedBytes += getOriginalItemBytes(item);
        auto estimate = m_estimates.find(getEstimateKey(item));
        if (estimate != m_estimates.end() && estimate->second > 0)
        {
            encodedBytes += estimate->second;
        }
    }

    if (isPageSource() && m_imageList && m_imageListDocument == getShownDocument())
    {
        // A page image replaces the images of the page: an image goes, when no page keeps it
        for (const PDFDocumentImageList::Item& image : m_imageList->getItems())
        {
            bool isRemoved = !image.pageIndices.empty();
            for (const PDFInteger page : image.pageIndices)
            {
                isRemoved = isRemoved && replacedPages.count(int(page)) > 0;
            }
            removedBytes += isRemoved ? image.streamBytes + image.maskBytes : 0;
        }
    }

    const bool isPages = isPageSource();
    m_summaryRows->clear();
    const std::pair<ItemMode, QString> rows[] = { { ItemMode::Algorithm, tr("Convert") }, { ItemMode::AlgorithmInverted, tr("Convert, inverted") }, { ItemMode::Original, tr("Leave unchanged") },
                                                  { ItemMode::FillWhite, tr("Fill with white") }, { ItemMode::FillBlack, tr("Fill with black") } };
    for (const auto& [mode, title] : rows)
    {
        const int count = counts[mode];
        QString value;
        if (isPages)
        {
            value = count == 1 ? tr("%Ln page", nullptr, count) : tr("%Ln pages", nullptr, count);
        }
        else
        {
            value = count == 1 ? tr("%Ln image", nullptr, count) : tr("%Ln images", nullptr, count);
        }
        m_summaryRows->addRow(title, value);
    }

    // "24.8 MB → 1.9 MB": the file of now without the replaced originals, with the encoded items
    const PDFAppDocument* document = getDocument();
    const qint64 sizeBefore = document ? document->getSnapshot().originalData.size() : 0;
    QString estimate;
    if (itemCount == 0 || sizeBefore <= 0)
    {
        estimate = QStringLiteral("–");
    }
    else if (!isEstimateComplete())
    {
        estimate = tr("Estimating…");
    }
    else
    {
        const qint64 sizeAfter = qMax(qint64(0), sizeBefore - removedBytes) + encodedBytes;
        estimate = PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter);
    }
    m_summaryRows->addRow(tr("Estimated size"), estimate);
}

// ------------------------------------------------------------------ jobs

void PDFAppToolGUIBitonal::cancelJobs()
{
    cancelImageCollection();
    cancelPreview();
    cancelEstimate();
    cancelDetection();
}

void PDFAppToolGUIBitonal::startImageCollection()
{
    cancelImageCollection();
    const PDFDocumentPointer document = getShownDocument();
    if (!document)
    {
        return;
    }

    // The list of the images is needed by the source Images and by the Summary of the pages; it
    // reads the content of every page, so it never runs in the thread of the GUI
    m_imagesCancel = std::make_shared<PDFAppCancelFlag>();
    m_imageListDocument = document;
    m_imagesWatcher.setFuture(QtConcurrent::run(&PDFAppToolBitonalJobs::collectImages, document, m_imagesCancel));
}

void PDFAppToolGUIBitonal::cancelImageCollection()
{
    if (m_imagesCancel)
    {
        m_imagesCancel->cancel();
        m_imagesCancel.reset();
    }
}

std::optional<PDFBitonalDocumentCreator::PreviewRequest> PDFAppToolGUIBitonal::createPreviewRequest(int itemIndex, bool isEstimate) const
{
    const PDFDocumentPointer document = getShownDocument();
    if (!document || itemIndex < 0 || itemIndex >= getItemCount())
    {
        return std::nullopt;
    }

    PDFBitonalDocumentCreator::PreviewRequest request;
    request.conversionSource = isPageSource() ? PDFBitonalDocumentCreator::ConversionSource::Pages : PDFBitonalDocumentCreator::ConversionSource::Images;
    request.item.mode = getItemMode(itemIndex);
    request.conversionMethod = PDFDocumentToolBitonal::getConversionMethod(getParameter(PDFDocumentToolBitonal::PARAMETER_METHOD).toString());
    request.manualThreshold = getParameter(PDFDocumentToolBitonal::PARAMETER_THRESHOLD).toInt();
    request.compression = PDFDocumentToolBitonal::getCompression(getParameter(PDFDocumentToolBitonal::PARAMETER_COMPRESSION).toString());
    request.isEncoded = isEstimate;

    if (isPageSource())
    {
        request.item.pageIndex = itemIndex;
        request.dpiResolution = getEffectiveDpi();
        if (!isEstimate && m_comparison)
        {
            // Only the resolution, which a card can show, is rasterized for the preview
            const QSize paneSize = QSize(m_comparison->width() / 2, m_comparison->height()) * m_comparison->devicePixelRatioF();
            request.dpiResolution = PDFBitonalDocumentCreator::getPreviewDpiResolution(document->getCatalog()->getPage(size_t(itemIndex)), request.dpiResolution, paneSize);
        }
    }
    else if (const std::optional<PDFDocumentImageList::Key> key = getImageKey(itemIndex))
    {
        request.item.imageReference = key->reference;
    }
    else
    {
        return std::nullopt;
    }
    return request;
}

QString PDFAppToolGUIBitonal::getEstimateKey(int itemIndex) const
{
    // The cache of the encoded sizes (R4-22): the item, its mode and every setting, which changes
    // the encoded image
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7").arg(isPageSource() ? QStringLiteral("p") : QStringLiteral("i"))
                                                 .arg(itemIndex)
                                                 .arg(int(getItemMode(itemIndex)))
                                                 .arg(getParameter(PDFDocumentToolBitonal::PARAMETER_METHOD).toString())
                                                 .arg(getParameter(PDFDocumentToolBitonal::PARAMETER_THRESHOLD).toInt())
                                                 .arg(isPageSource() ? getEffectiveDpi() : 0)
                                                 .arg(getParameter(PDFDocumentToolBitonal::PARAMETER_COMPRESSION).toString());
}

void PDFAppToolGUIBitonal::schedulePreview()
{
    // The settings of the preview have changed: the old job is dropped, the new one starts after
    // the delay (a run of quick changes starts one job)
    cancelPreview();
    m_previewTimer.start();
}

void PDFAppToolGUIBitonal::cancelPreview()
{
    m_previewTimer.stop();
    if (m_previewCancel)
    {
        m_previewCancel->cancel();
        m_previewCancel.reset();
    }
}

void PDFAppToolGUIBitonal::onPreviewTimeout()
{
    startPreview();
}

void PDFAppToolGUIBitonal::startPreview()
{
    std::optional<PDFBitonalDocumentCreator::PreviewRequest> request = createPreviewRequest(m_currentItem, false);
    if (!request || !m_comparison)
    {
        m_previewKey.clear();
        updateCanvas();
        startEstimate();
        return;
    }

    // The rendered page / decoded image of an earlier preview of the same item and resolution
    // is reused: a change of the method or of the mode renders nothing. The key and the version of
    // the cached image are set with the image (onPreviewFinished): a preview canceled before its
    // end leaves the image of the item before
    const QString originalKey = QStringLiteral("%1|%2|%3").arg(isPageSource() ? QStringLiteral("p") : QStringLiteral("i")).arg(m_currentItem).arg(request->dpiResolution);
    const PDFDocumentPointer document = getShownDocument();
    if (originalKey == m_cachedOriginalKey && document == m_cachedOriginalDocument && !m_cachedOriginal.isNull())
    {
        request->originalImage = m_cachedOriginal;
    }

    m_previewKey = getEstimateKey(m_currentItem) + QStringLiteral("|") + originalKey;
    m_previewOriginalKey = originalKey;
    m_previewDocument = document;
    m_previewCancel = std::make_shared<PDFAppCancelFlag>();
    m_previewWatcher.setFuture(QtConcurrent::run(&PDFAppToolBitonalJobs::createPreview, document, getRunCmsSettings(), *request, m_previewCancel));
    updateCanvas();
}

void PDFAppToolGUIBitonal::onPreviewFinished()
{
    if (!m_previewCancel || m_previewCancel->isOperationCancelled() || !m_previewWatcher.future().isValid() || m_previewWatcher.future().resultCount() == 0)
    {
        return;
    }

    m_previewCancel.reset();
    m_preview = m_previewWatcher.result();
    m_shownPreviewKey = m_previewKey;
    m_cachedOriginal = m_preview->originalImage;
    m_cachedOriginalKey = m_previewOriginalKey;
    m_cachedOriginalDocument = m_previewDocument;
    m_previewDocument.reset();
    updateCanvas();
    startEstimate();
}

void PDFAppToolGUIBitonal::startEstimate()
{
    if (m_estimateWatcher.isRunning() && m_estimateCancel)
    {
        // The running batch goes on; its results stay valid by their keys
        return;
    }

    // The next batch of the items, whose encoded size with the settings of now is not known yet
    // (R4-22: a change of the mode of one item computes only it)
    std::vector<PDFBitonalDocumentCreator::PreviewRequest> requests;
    std::vector<QString> keys;
    for (int item = 0, count = getItemCount(); item < count && int(requests.size()) < ESTIMATE_BATCH_SIZE; ++item)
    {
        const ItemMode mode = getItemMode(item);
        if (mode == ItemMode::Original || (isPageSource() && mode != ItemMode::Algorithm && mode != ItemMode::AlgorithmInverted))
        {
            // Unchanged, or a filled page of one sample
            continue;
        }

        const QString key = getEstimateKey(item);
        if (m_estimates.count(key))
        {
            continue;
        }

        if (std::optional<PDFBitonalDocumentCreator::PreviewRequest> request = createPreviewRequest(item, true))
        {
            requests.push_back(std::move(*request));
            keys.push_back(key);
        }
    }

    if (requests.empty())
    {
        updateSummary();
        return;
    }

    m_estimateKeys = std::move(keys);
    m_estimateCancel = std::make_shared<PDFAppCancelFlag>();
    m_estimateWatcher.setFuture(QtConcurrent::run(&PDFAppToolBitonalJobs::estimateSizes, getShownDocument(), getRunCmsSettings(), std::move(requests), m_estimateCancel));
}

void PDFAppToolGUIBitonal::cancelEstimate()
{
    if (m_estimateCancel)
    {
        m_estimateCancel->cancel();
        m_estimateCancel.reset();
    }
}

void PDFAppToolGUIBitonal::onEstimateFinished()
{
    if (!m_estimateCancel || m_estimateCancel->isOperationCancelled() || m_estimateWatcher.future().resultCount() == 0)
    {
        return;
    }

    m_estimateCancel.reset();
    const std::vector<qint64> sizes = m_estimateWatcher.result();
    for (size_t index = 0; index < sizes.size() && index < m_estimateKeys.size(); ++index)
    {
        if (sizes[index] >= -1)
        {
            m_estimates[m_estimateKeys[index]] = sizes[index];
        }
    }
    m_estimateKeys.clear();

    updateSummary();
    updateCanvas();
    startEstimate();
}

void PDFAppToolGUIBitonal::onImagesCollected()
{
    if (!m_imagesCancel || m_imagesCancel->isOperationCancelled() || m_imagesWatcher.future().resultCount() == 0)
    {
        return;
    }

    m_imagesCancel.reset();
    m_imageList = m_imagesWatcher.result();
    ++m_imageListGeneration;
    m_convertibleImages.clear();
    if (m_imageList)
    {
        for (size_t index = 0; index < m_imageList->getItems().size(); ++index)
        {
            if (PDFBitonalDocumentCreator::isConvertibleImage(m_imageList->getItems()[index]))
            {
                m_convertibleImages.push_back(int(index));
            }
        }
    }

    // The items of the images are known now (the selection of the pages is kept)
    if (!isPageSource())
    {
        m_selectedItems.clear();
        m_currentItem = m_convertibleImages.empty() ? -1 : 0;
        m_anchorItem = m_currentItem;
        schedulePreview();
    }

    updateAll();

    // The Primary button and the status depend on the images (the last things: a slot may delete this)
    QPointer<PDFAppToolGUIBitonal> guard(this);
    updateStatusText();
    if (guard)
    {
        updateConfirmState();
    }
}

void PDFAppToolGUIBitonal::toggleBlankPageDetection()
{
    if (m_isDetecting)
    {
        // Stop: the user does not wait for a result, which is not interesting anymore
        cancelDetection();
        updateToolbar();
        QPointer<PDFAppToolGUIBitonal> guard(this);
        updateStatusText();
        if (guard)
        {
            updateConfirmState();
        }
        return;
    }

    const PDFDocumentPointer document = getShownDocument();
    if (!document || !isPageSource())
    {
        return;
    }

    // The pages, which would be converted (a page left unchanged or filled stays so)
    std::vector<PDFInteger> pages;
    for (int page = 0, count = getItemCount(); page < count; ++page)
    {
        if (PDFBitonalDocumentCreator::isConversionMode(getItemMode(page)))
        {
            pages.push_back(page);
        }
    }

    if (pages.empty())
    {
        return;
    }

    m_isDetecting = true;
    m_examinedPageCount = 0;
    m_pagesToExamineCount = int(pages.size());
    m_foundBlankPages.clear();
    m_detectionCancel = std::make_shared<PDFAppCancelFlag>();
    m_detectionProgress = std::make_shared<PDFProgressCounter>();
    m_detectionWatcher.setFuture(QtConcurrent::run(&PDFAppToolBitonalJobs::detectBlankPages, document, getRunCmsSettings(), std::move(pages), m_detectionCancel, m_detectionProgress));
    m_detectionTimer.start();

    updateToolbar();
    QPointer<PDFAppToolGUIBitonal> guard(this);
    updateStatusText();
    if (guard)
    {
        updateConfirmState();
    }
}

void PDFAppToolGUIBitonal::cancelDetection()
{
    m_detectionTimer.stop();
    m_isDetecting = false;
    if (m_detectionCancel)
    {
        m_detectionCancel->cancel();
        m_detectionCancel.reset();
    }
    m_detectionProgress.reset();
}

void PDFAppToolGUIBitonal::onDetectionPoll()
{
    if (!m_isDetecting || !m_detectionProgress)
    {
        return;
    }

    const PDFProgressCounter::Snapshot snapshot = m_detectionProgress->snapshot();
    const int examined = int(qMin(snapshot.done, size_t(m_pagesToExamineCount)));
    if (examined != m_examinedPageCount)
    {
        m_examinedPageCount = examined;
        updateToolbar();
        updateStatusText();
    }
}

void PDFAppToolGUIBitonal::onDetectionFinished()
{
    if (!m_isDetecting || !m_detectionCancel || m_detectionCancel->isOperationCancelled() || m_detectionWatcher.future().resultCount() == 0)
    {
        return;
    }

    const std::vector<PDFBitonalDocumentCreator::BlankPageResult> results = m_detectionWatcher.result();
    cancelDetection();

    bool isComplete = true;
    for (const PDFBitonalDocumentCreator::BlankPageResult& result : results)
    {
        isComplete = isComplete && result.isExamined;
        if (result.isBlank)
        {
            m_foundBlankPages.push_back(result.pageIndex);
        }
    }
    m_examinedPageCount = m_pagesToExamineCount;

    updateToolbar();

    // 307:6250 and 307:6388: the found pages are never filled silently - a page wrongly taken
    // for a blank one would lose its content
    const PDFAppToolHost* host = getHost();
    QWidget* parent = host && host->getKeyWidget() ? host->getKeyWidget()->window() : nullptr;
    PDFMessageBox* box = nullptr;
    if (!isComplete)
    {
        m_foundBlankPages.clear();
        box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, getTool() ? getTool()->getTitle() : QString(),
                                tr("Blank page detection could not examine all pages. No page modes have been changed."), QDialogButtonBox::Ok, parent);
    }
    else if (m_foundBlankPages.empty())
    {
        box = new PDFMessageBox(PDFThemeIcons::Badge::Information, getTool() ? getTool()->getTitle() : QString(),
                                tr("No blank page has been found in the document."), QDialogButtonBox::Ok, parent);
    }
    else
    {
        std::vector<int> numbers;
        for (const PDFInteger page : m_foundBlankPages)
        {
            numbers.push_back(int(page) + 1);
        }

        box = new PDFMessageBox(PDFThemeIcons::Badge::Question, getTool() ? getTool()->getTitle() : QString(),
                                tr("%Ln of %1 pages seem to be blank. Do you want to replace them by a white fill?", nullptr, int(m_foundBlankPages.size())).arg(PDFAppLocale::formatNumber(getItemCount())),
                                QDialogButtonBox::NoButton, parent);
        box->setDetailedText(tr("Blank pages: %1").arg(createNumbersText(numbers)));
        QPushButton* noButton = box->addActionButton(tr("No"), PDFMessageBox::ActionPosition::Trailing, false);
        QPushButton* replaceButton = box->addActionButton(tr("Replace with white fill"), PDFMessageBox::ActionPosition::Trailing, true);
        noButton->setObjectName(QStringLiteral("bitonalBlankNo"));
        replaceButton->setObjectName(QStringLiteral("bitonalBlankReplace"));
        connect(noButton, &QPushButton::clicked, box, &QDialog::reject);
        connect(replaceButton, &QPushButton::clicked, box, &QDialog::accept);
    }

    box->setObjectName(QStringLiteral("bitonalBlankPagesQuestion"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    if (host)
    {
        box->setScrimTarget(host->getCanvasArea());
    }
    connect(box, &QDialog::finished, this, &PDFAppToolGUIBitonal::onBlankPagesQuestionFinished);
    m_question = box;

    // The state is final before the foreign code: the status, the button, then the dialog
    QPointer<PDFAppToolGUIBitonal> guard(this);
    updateStatusText();
    if (!guard)
    {
        return;
    }
    updateConfirmState();
    if (guard && m_question)
    {
        m_question->open();
    }
}

void PDFAppToolGUIBitonal::onBlankPagesQuestionFinished(int result)
{
    m_question = nullptr;
    const std::vector<PDFInteger> pages = std::move(m_foundBlankPages);
    m_foundBlankPages.clear();
    if (result != QDialog::Accepted || pages.empty() || !isPageSource())
    {
        return;
    }

    // One undo step: the found pages get the white fill, the other modes stay; the selection of
    // the user is back before the model announces the change (its slots may delete this GUI, the
    // change is the last thing)
    const std::vector<int> selection = m_selectedItems;
    m_selectedItems.assign(pages.cbegin(), pages.cend());
    std::sort(m_selectedItems.begin(), m_selectedItems.end());
    std::map<int, ItemMode> modes = getModesOfItems();
    for (const int page : m_selectedItems)
    {
        modes[page] = ItemMode::FillWhite;
    }
    m_selectedItems = selection;

    std::vector<PDFDocumentToolBitonal::ItemEntry> entries;
    for (const auto& [page, mode] : modes)
    {
        if (mode != ItemMode::Algorithm)
        {
            PDFDocumentToolBitonal::ItemEntry entry;
            entry.pageIndex = page;
            entry.mode = mode;
            entries.push_back(entry);
        }
    }

    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValues({ { PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_ITEMS), PDFDocumentToolBitonal::createItemsText(entries) } }, getModeTitle(ItemMode::FillWhite));
    }
}

// ------------------------------------------------------------------ slots

void PDFAppToolGUIBitonal::onSourceRowClicked()
{
    const QObject* row = sender();
    if (!row || !getModel())
    {
        return;
    }

    // The value is read before the panel closes: its slots may delete the row, the panel or this
    // GUI with its model (RS4APP)
    const QVariant source = row->property(PDFAppToolGUIBitonalHelper::ROW_VALUE_PROPERTY);
    QPointer<PDFAppToolGUIBitonal> guard(this);
    if (m_sourcePanel)
    {
        m_sourcePanel->close();
    }
    PDFAppToolParameterModel* model = guard ? getModel() : nullptr;
    if (model)
    {
        // The modes of the other source say nothing about this one: the items start converted
        model->setValues({ { PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_SOURCE), source },
                           { PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_ITEMS), QString() } });
    }
}

void PDFAppToolGUIBitonal::onMethodRowClicked()
{
    QObject* row = sender();
    PDFAppToolParameterModel* model = getModel();
    if (row && model)
    {
        model->setValue(PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_METHOD), row->property(PDFAppToolGUIBitonalHelper::ROW_VALUE_PROPERTY));
    }
}

void PDFAppToolGUIBitonal::onCompressionRowClicked()
{
    const QObject* row = sender();
    if (!row || !getModel())
    {
        return;
    }

    // The value is read before the panel closes (its slots may delete the row or this GUI)
    const QVariant compression = row->property(PDFAppToolGUIBitonalHelper::ROW_VALUE_PROPERTY);
    QPointer<PDFAppToolGUIBitonal> guard(this);
    if (m_compressionPanel)
    {
        m_compressionPanel->close();
    }
    PDFAppToolParameterModel* model = guard ? getModel() : nullptr;
    if (model)
    {
        model->setValue(PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_COMPRESSION), compression);
    }
}

void PDFAppToolGUIBitonal::onModeRowClicked()
{
    const QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The value is read before the panel closes (its slots may delete the row or this GUI)
    const ItemMode mode = ItemMode(row->property(PDFAppToolGUIBitonalHelper::ROW_VALUE_PROPERTY).toInt());
    QPointer<PDFAppToolGUIBitonal> guard(this);
    if (m_modePanel)
    {
        m_modePanel->close();
    }
    if (guard)
    {
        setModeToItems(mode);
    }
}

void PDFAppToolGUIBitonal::onResolutionChanged(int value)
{
    PDFAppToolParameterModel* model = getModel();
    if (!m_isUpdatingControls && model)
    {
        model->setValue(PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_DPI), qint64(value));
    }
}

void PDFAppToolGUIBitonal::onThresholdChanged(int value)
{
    PDFAppToolParameterModel* model = getModel();
    if (!m_isUpdatingControls && model)
    {
        model->setValue(PDFAppToolGUIBitonalHelper::name(PDFDocumentToolBitonal::PARAMETER_THRESHOLD), qint64(value));
    }
}

void PDFAppToolGUIBitonal::onBlankPagesClicked()
{
    toggleBlankPageDetection();
}

void PDFAppToolGUIBitonal::onGridPageClicked(int pageIndex, Qt::KeyboardModifiers modifiers)
{
    // Ctrl toggles, Shift selects the range from the anchor, a plain click selects one item
    std::vector<int> items = m_selectedItems;
    if (modifiers.testFlag(Qt::ControlModifier))
    {
        auto it = std::find(items.begin(), items.end(), pageIndex);
        if (it != items.end())
        {
            items.erase(it);
        }
        else
        {
            items.push_back(pageIndex);
        }
        m_anchorItem = pageIndex;
    }
    else if (modifiers.testFlag(Qt::ShiftModifier) && m_anchorItem >= 0)
    {
        items.clear();
        for (int item = qMin(m_anchorItem, pageIndex); item <= qMax(m_anchorItem, pageIndex); ++item)
        {
            items.push_back(item);
        }
    }
    else
    {
        items = { pageIndex };
        m_anchorItem = pageIndex;
    }

    selectItems(std::move(items), pageIndex);
    updateToolbar();
}

void PDFAppToolGUIBitonal::onImageListSelectionChanged()
{
    if (!m_imageListWidget || !m_imageList)
    {
        return;
    }

    std::vector<int> items;
    for (const PDFDocumentImageList::Key& key : m_imageListWidget->getSelectedKeys())
    {
        // m_convertibleImages is ascending (the order of the list): Ctrl+A of 10 000 images is not quadratic
        const int listIndex = m_imageList->indexOf(key);
        auto it = std::lower_bound(m_convertibleImages.cbegin(), m_convertibleImages.cend(), listIndex);
        if (it != m_convertibleImages.cend() && *it == listIndex)
        {
            items.push_back(int(std::distance(m_convertibleImages.cbegin(), it)));
        }
    }

    const int current = items.empty() ? m_currentItem : items.back();
    selectItems(std::move(items), current);
    updateToolbar();
}

void PDFAppToolGUIBitonal::onImageMarksToggled()
{
    // The selection of the list is the selection of the items (onImageListSelectionChanged)
    if (m_selectedItems.empty() || !m_isContentEnabled || m_isDetecting || isPageSource())
    {
        return;
    }

    bool isAllOriginal = true;
    for (const int item : m_selectedItems)
    {
        isAllOriginal = isAllOriginal && getItemMode(item) == ItemMode::Original;
    }

    // One undo step; the model runs the slots (the last call)
    setModeToItems(isAllOriginal ? ItemMode::Algorithm : ItemMode::Original);
}

PDFCMSSettings PDFAppToolGUIBitonal::getRunCmsSettings() const
{
    return PDFAppDocumentView::createToolRenderContext(getContext()).cmsSettings.value_or(PDFCMSSettings());
}

void PDFAppToolGUIBitonal::onItemContextMenuRequested(const QPoint& position)
{
    QWidget* widget = qobject_cast<QWidget*>(sender());
    if (!widget || !m_isContentEnabled || m_isDetecting || getItemCount() == 0)
    {
        return;
    }

    // 304:5102 "Right-click for the mode menu": the modes of the menu Mode; the position of a
    // view is in its viewport
    QAbstractScrollArea* scrollArea = qobject_cast<QAbstractScrollArea*>(widget);
    const QPoint globalPosition = scrollArea ? scrollArea->viewport()->mapToGlobal(position) : widget->mapToGlobal(position);

    QMenu* menu = new QMenu(widget);
    menu->setObjectName(QStringLiteral("bitonalModeMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* section = menu->addSection(m_selectedItems.empty() ? tr("All items") : tr("Selected items (%1)").arg(PDFAppLocale::formatNumber(qint64(m_selectedItems.size()))));
    Q_UNUSED(section);
    for (const ItemMode mode : PDFAppToolGUIBitonalHelper::getModes())
    {
        QAction* action = menu->addAction(getModeTitle(mode));
        action->setData(int(mode));
    }
    connect(menu, &QMenu::triggered, this, &PDFAppToolGUIBitonal::onContextMenuTriggered);
    menu->popup(globalPosition);
}

void PDFAppToolGUIBitonal::onContextMenuTriggered(QAction* action)
{
    if (action && action->data().isValid())
    {
        setModeToItems(ItemMode(action->data().toInt()));
    }
}

void PDFAppToolGUIBitonal::onComparisonZoomChanged()
{
    // A larger card shows more: the preview of a page is rasterized for the size again
    if (isPageSource())
    {
        schedulePreview();
    }
}

// ------------------------------------------------------------------ PDFDocumentToolGUI

void PDFAppToolGUIBitonal::setContentEnabled(bool isEnabled)
{
    m_isContentEnabled = isEnabled;
    BaseClass::setContentEnabled(isEnabled);
    updateToolbar();
}

void PDFAppToolGUIBitonal::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);

    if (name == QLatin1String(PDFDocumentToolBitonal::PARAMETER_SOURCE))
    {
        // Another kind of items: the left panel is built again by the shell (retranslate), the
        // selection starts empty
        cancelDetection();
        m_selectedItems.clear();
        m_currentItem = getItemCount() > 0 ? 0 : -1;
        m_anchorItem = m_currentItem;
        m_preview.reset();
        m_cachedOriginalKey.clear();
    }

    updateAll();
    schedulePreview();
}

void PDFAppToolGUIBitonal::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    if (result.status != PDFDocumentToolResultStatus::Failed)
    {
        BaseClass::onRunNotAccepted(result);
        return;
    }

    // 307:6388: the message of the tool ("The bitonal document has not been created. No item of
    // the document could be converted.")
    const PDFAppToolHost* host = getHost();
    QWidget* parent = host && host->getKeyWidget() ? host->getKeyWidget()->window() : nullptr;
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, getTool() ? getTool()->getTitle() : QString(), result.message, QDialogButtonBox::Ok, parent);
    box->setObjectName(QStringLiteral("bitonalFailedMessage"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    if (host)
    {
        box->setScrimTarget(host->getCanvasArea());
    }
    if (m_question)
    {
        m_question->deleteLater();
    }
    m_question = box;
    box->open();
}

std::pair<QString, QString> PDFAppToolGUIBitonal::getPartialResultTexts(const PDFDocumentToolResult& result)
{
    // The core counts a failed item only as not converted (PDFDocumentToolBitonal: "converted"
    // with the inverted ones, the fills, "unchanged" and the list "notConverted")
    const qint64 notConverted = result.data.value(QStringLiteral("notConverted")).toArray().size();
    if (notConverted <= 0)
    {
        return { };
    }

    const qint64 total = notConverted + result.data.value(QStringLiteral("converted")).toInteger() + result.data.value(QStringLiteral("filledWhite")).toInteger() +
                         result.data.value(QStringLiteral("filledBlack")).toInteger() + result.data.value(QStringLiteral("unchanged")).toInteger();

    // 307:6388 "3 of 120 items could not be converted and they are left unchanged. Do you want to
    // use the partially converted document?" and "Not converted: pages 17, 43, 44 (rendering errors)"
    const QString text = tr("%1 of %Ln items could not be converted and they are left unchanged. Do you want to use the partially converted document?", nullptr, int(total))
                             .arg(PDFAppLocale::formatNumber(notConverted));
    QString detail;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String("notConverted"))
        {
            detail = tr("%1: %2").arg(fact.title, fact.value);
            break;
        }
    }
    return { text, detail };
}

bool PDFAppToolGUIBitonal::confirmResult(const PDFDocumentToolResult& result)
{
    const auto [text, detail] = getPartialResultTexts(result);
    if (text.isEmpty())
    {
        return true;
    }

    // 307:6388: the question of the tool (its title), asynchronous; the answer goes to the shell
    const PDFAppToolHost* host = getHost();
    QWidget* parent = host && host->getKeyWidget() ? host->getKeyWidget()->window() : nullptr;
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Question, getTool() ? getTool()->getTitle() : QString(), text, QDialogButtonBox::NoButton, parent);
    box->setObjectName(QStringLiteral("bitonalPartialResultQuestion"));
    box->setDetailedText(detail);
    QPushButton* discardButton = box->addActionButton(tr("No, discard"), PDFMessageBox::ActionPosition::Trailing, false);
    QPushButton* useButton = box->addActionButton(tr("Use partial document"), PDFMessageBox::ActionPosition::Trailing, true);
    discardButton->setObjectName(QStringLiteral("bitonalPartialDiscard"));
    useButton->setObjectName(QStringLiteral("bitonalPartialUse"));
    connect(discardButton, &QPushButton::clicked, box, &QDialog::reject);
    connect(useButton, &QPushButton::clicked, box, &QDialog::accept);
    box->setAttribute(Qt::WA_DeleteOnClose);
    if (host)
    {
        box->setScrimTarget(host->getCanvasArea());
    }
    connect(box, &QDialog::finished, this, &PDFAppToolGUIBitonal::onPartialResultQuestionFinished);
    if (m_question)
    {
        m_question->deleteLater();
    }
    m_question = box;

    // The state is final before the foreign code (the show of the question)
    box->open();
    return false;
}

void PDFAppToolGUIBitonal::onPartialResultQuestionFinished(int result)
{
    // The answer is the last call: the shell may close the tool and delete this GUI
    m_question = nullptr;
    if (PDFDocumentToolShell* shell = getShell())
    {
        shell->answerResult(result == QDialog::Accepted);
    }
}

void PDFAppToolGUIBitonal::onDocumentChanged()
{
    BaseClass::onDocumentChanged();

    // Another version of the document: the images, the previews and the estimates are of the old
    // one; the modes of "items" stay (an assistant changing the pages below the tool maps them)
    cancelJobs();
    if (m_question)
    {
        m_question->deleteLater();
        m_question = nullptr;
    }
    m_imageList.reset();
    ++m_imageListGeneration;
    m_isModesCacheValid = false;
    m_convertibleImages.clear();
    m_estimates.clear();
    m_preview.reset();
    m_cachedOriginalKey.clear();
    m_cachedOriginalDocument.reset();
    m_cachedOriginal = QImage();
    m_estimatedDpiDocument.reset();
    if (!isPageSource())
    {
        m_selectedItems.clear();
        m_currentItem = -1;
    }
    selectItems(m_selectedItems, m_currentItem);

    if (m_pageGrid)
    {
        const PDFAppDocument* document = getDocument();
        const PDFAppToolHost* host = getHost();
        m_pageGrid->setDocument(getShownDocument(), document ? document->getId() : QString(), host ? host->getView() : nullptr);
    }

    startImageCollection();
    updateAll();
}

void PDFAppToolGUIBitonal::retranslate()
{
    BaseClass::retranslate();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_bitonal.cpp"
