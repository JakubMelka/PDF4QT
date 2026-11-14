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

#include "pdfapptoolgui_extractimages.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpermissions.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolhost.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfimagecompressor.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontentprocessor.h"
#include "pdfrenderer.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>

namespace pdf
{

/// The cancellation of a computation of Extract Images (shared by the GUI and its job)
class PDFAppToolExtractCollectionControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_isCanceled.load(std::memory_order_relaxed); }
    void cancel() { m_isCanceled.store(true, std::memory_order_relaxed); }

private:
    std::atomic_bool m_isCanceled = false;
};

/// The job of the list and the plan: plain values and the shared immutable document, never the
/// GUI (a functor, no lambda)
class PDFAppToolExtractCollectionJob
{
public:
    PDFDocumentPointer document;
    std::shared_ptr<const PDFDocumentImageList> list;   ///< The list of the version, null = collect it
    std::map<PDFObjectReference, OCState> layerStates;
    QVariantMap parameters;
    QString sourceFileName;
    QDate date;
    quint64 version = 0;
    quint64 serial = 0;
    std::shared_ptr<PDFAppToolExtractCollectionControl> control;
    std::shared_ptr<PDFDocumentToolExtractImages::ExportPlanCache> planCache;  ///< The hashes of the images of list, null = a new list gets a new cache

    PDFAppToolExtractCollection operator()() const
    {
        PDFAppToolExtractCollection result;
        result.version = version;
        result.serial = serial;
        result.list = list;
        result.planCache = list ? planCache : std::shared_ptr<PDFDocumentToolExtractImages::ExportPlanCache>();
        if (!result.list)
        {
            // R4-11: the images of all layers, an image only in layers hidden in the tab is flagged
            const PDFDocumentImageList::Options options = PDFDocumentImageList::Options::createForExport(layerStates);
            result.list = std::make_shared<const PDFDocumentImageList>(PDFDocumentImageList::create(document.data(), options, control.get(), nullptr));
        }
        if (!result.planCache)
        {
            result.planCache = std::make_shared<PDFDocumentToolExtractImages::ExportPlanCache>();
        }
        if (!PDFOperationControl::isOperationCancelled(control.get()))
        {
            result.plan = PDFDocumentToolExtractImages::createExportPlan(document.data(), *result.list, sourceFileName, parameters, date, control.get(), result.planCache.get(), &result.planError);
        }
        return result;
    }
};

/// The drawings of the images of one page, nothing decoded: the content processor of the core
/// reports every image before it would decode it (the matrix of the drawing is its place)
class PDFAppToolImagePlacementProcessor : public PDFPageContentProcessor
{
public:
    explicit PDFAppToolImagePlacementProcessor(const PDFPage* page, const PDFDocument* document, const PDFImageCompressor::Environment& environment,
                                               const PDFMeshQualitySettings& meshQualitySettings, std::vector<PDFAppToolImagePlacement>* placements) :
        PDFPageContentProcessor(page, document, environment.getFontCache(), nullptr, environment.getCMS(), nullptr, QTransform(), meshQualitySettings),
        m_placements(placements)
    {

    }

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override
    {
        switch (kind)
        {
            case ContentKind::Images:
            case ContentKind::Tiling:
            case ContentKind::Forms:
                return false;

            default:
                return true;
        }
    }

    virtual bool isTextIgnored() const override
    {
        return true;
    }

    virtual bool isOriginalImageRequired(const PDFStream* stream, PDFObjectReference reference) override
    {
        Q_UNUSED(stream);

        PDFAppToolImagePlacement placement;
        if (reference.isValid())
        {
            placement.key = PDFDocumentImageList::Key::createReference(reference);
        }
        else
        {
            const InlineImagePosition& position = getInlineImagePosition();
            if (position.index < 0 || !position.contentStream.isValid())
            {
                return false;
            }
            placement.key = PDFDocumentImageList::Key::createInline(position.contentStream, position.index);
        }

        // An image is the unit square of its space (8.9.4)
        const QTransform matrix = getCurrentWorldMatrix();
        placement.quad = matrix.map(QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0)));
        m_placements->push_back(std::move(placement));
        return false;
    }

private:
    std::vector<PDFAppToolImagePlacement>* m_placements;
};

/// Helpers of the GUI of Extract Images
class PDFAppToolGUIExtractImagesHelper
{
public:
    PDFAppToolGUIExtractImagesHelper() = delete;

    using Tool = PDFDocumentToolExtractImages;

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// The layer states of the tab (the options of the list of an export, R4-11)
    static std::map<PDFObjectReference, OCState> getLayerStates(const PDFAppDocument* document)
    {
        const PDFOptionalContentActivity* activity = document ? document->getOptionalContentActivity() : nullptr;
        return activity ? activity->getStates() : std::map<PDFObjectReference, OCState>();
    }

    /// Distance of a click from a drawing, which still hits it (a thin image)
    static constexpr qreal CLICK_TOLERANCE = 2.0;
};

// ---------------------------------------------------------- PDFAppToolImagePlacement

std::vector<PDFAppToolImagePlacement> PDFAppToolImagePlacement::collect(const PDFDocument* document, PDFInteger pageIndex, const PDFCMSPointer& cms)
{
    std::vector<PDFAppToolImagePlacement> placements;
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
    if (!page)
    {
        return placements;
    }

    const PDFImageCompressor::Environment environment(document, cms);
    PDFMeshQualitySettings meshQualitySettings;
    PDFAppToolImagePlacementProcessor processor(page, document, environment, meshQualitySettings, &placements);
    processor.processContents();
    return placements;
}

const PDFAppToolImagePlacement* PDFAppToolImagePlacement::findTopmost(const std::vector<PDFAppToolImagePlacement>& placements, const QPointF& point)
{
    // B64: the last drawing covers the earlier ones
    for (auto it = placements.crbegin(); it != placements.crend(); ++it)
    {
        if (it->quad.containsPoint(point, Qt::OddEvenFill))
        {
            return &*it;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------- PDFAppToolImagePlacementOverlay

PDFAppToolImagePlacementOverlay::PDFAppToolImagePlacementOverlay(PDFAppToolPageStage* stage) :
    BaseClass(stage),
    m_stage(stage)
{
    setObjectName(QStringLiteral("toolImagePlacementOverlay"));
    setAttribute(Qt::WA_NoSystemBackground, true);
    setCursor(Qt::PointingHandCursor);
    if (stage)
    {
        stage->addOverlay(this);
        connect(stage, &PDFAppToolPageStage::pageGeometryChanged, this, qOverload<>(&QWidget::update));
    }
}

PDFAppToolImagePlacementOverlay::~PDFAppToolImagePlacementOverlay() = default;

void PDFAppToolImagePlacementOverlay::setPlacements(std::vector<PDFAppToolImagePlacement> placements)
{
    m_placements = std::move(placements);
    update();
}

void PDFAppToolImagePlacementOverlay::setSelectedKey(const PDFDocumentImageList::Key& key)
{
    if (m_selectedKey != key)
    {
        m_selectedKey = key;
        update();
    }
}

QTransform PDFAppToolImagePlacementOverlay::getPageToOverlayTransform() const
{
    const PDFPage* page = m_stage ? m_stage->getPage() : nullptr;
    if (!page || m_stage->isImageShown())
    {
        return QTransform();
    }

    // As the picture of the stage: the shown box rotated by /Rotate in the rectangle of the page
    const QRectF rotatedBox = PDFPage::getRotatedBox(m_stage->getShownBoxRect(), page->getPageRotation());
    return PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedBox, m_stage->getPageRect(), page->getPageRotation());
}

void PDFAppToolImagePlacementOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_selectedKey.isValid() || !m_stage || m_stage->getPageRect().isEmpty())
    {
        return;
    }

    // picked-image 341:7516: the drawings of the selected image outlined in the accent color
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QTransform transform = getPageToOverlayTransform();
    QColor fill = PDFStylePainter::color(PDFThemeColors::BgAccent);
    fill.setAlphaF(0.12f);
    QPen pen(PDFStylePainter::color(PDFThemeColors::BorderFocus), 2.0);
    pen.setJoinStyle(Qt::MiterJoin);
    for (const PDFAppToolImagePlacement& placement : m_placements)
    {
        if (placement.key == m_selectedKey)
        {
            const QPolygonF polygon = transform.map(placement.quad);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPolygon(polygon);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(polygon);
        }
    }
}

void PDFAppToolImagePlacementOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        BaseClass::mouseReleaseEvent(event);
        return;
    }

    bool isInvertible = false;
    const QTransform transform = getPageToOverlayTransform().inverted(&isInvertible);
    const PDFAppToolImagePlacement* placement = isInvertible ? PDFAppToolImagePlacement::findTopmost(m_placements, transform.map(event->position())) : nullptr;
    event->accept();
    if (placement)
    {
        Q_EMIT imageClicked(placement->key.toString());
    }
}

// ---------------------------------------------------------- PDFAppToolExtractEmptyState

PDFAppToolExtractEmptyState::PDFAppToolExtractEmptyState(QWidget* parent) :
    BaseClass(parent)
{

}

PDFAppToolExtractEmptyState::~PDFAppToolExtractEmptyState() = default;

void PDFAppToolExtractEmptyState::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 341:8976: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

// ---------------------------------------------------------- PDFAppToolGUIExtractImages

PDFAppToolGUIExtractImages::PDFAppToolGUIExtractImages(QObject* parent) :
    BaseClass(parent)
{
    m_decoder = new PDFAppToolImageDecoder(this);
    connect(m_decoder.data(), &PDFAppToolImageDecoder::imagesDecoded, this, &PDFAppToolGUIExtractImages::onImagesDecoded);
}

PDFAppToolGUIExtractImages::~PDFAppToolGUIExtractImages()
{
    // The job goes on with its own values and its cancellation; the decoder cuts its link
    if (m_control)
    {
        m_control->cancel();
    }
    delete m_decoder.data();
}

void PDFAppToolGUIExtractImages::registerExtractImagesTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolExtractImages::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolExtractImages>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIExtractImages>>());
    }
}

QToolButton* PDFAppToolGUIExtractImages::getPagesButton() const
{
    return m_pagesPanel ? m_pagesPanel->getButton() : nullptr;
}

QToolButton* PDFAppToolGUIExtractImages::getFormatButton() const
{
    return m_formatMenu ? m_formatMenu->getButton() : nullptr;
}

PDFTileRow* PDFAppToolGUIExtractImages::getPagesRow(PagesMode mode) const
{
    auto it = m_pagesRows.find(int(mode));
    return it != m_pagesRows.end() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppToolGUIExtractImages::getFormatRow(const QString& format) const
{
    return m_formatMenu ? m_formatMenu->getRow(format.toLatin1()) : nullptr;
}

std::vector<PDFDocumentImageList::Key> PDFAppToolGUIExtractImages::getExcludedKeys() const
{
    const PDFAppToolParameterModel* model = getModel();
    QString error;
    return model ? PDFDocumentToolExtractImages::parseKeys(model->getValue(PDFAppToolGUIExtractImagesHelper::name(PDFDocumentToolExtractImages::PARAMETER_EXCLUDE)).toString(), &error)
                 : std::vector<PDFDocumentImageList::Key>();
}

std::vector<PDFInteger> PDFAppToolGUIExtractImages::getChosenPages() const
{
    const PDFAppToolParameterModel* model = getModel();
    const PDFAppDocument* document = getDocument();
    if (!model || !document)
    {
        return {};
    }

    QString error;
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(model->getValue(PDFAppToolGUIExtractImagesHelper::name(PDFDocumentToolExtractImages::PARAMETER_PAGES)), &error);
    const PDFDocumentPointer pdf = document->getSnapshot().document;
    return selection && pdf ? selection->getPageIndices(pdf.data(), &error) : std::vector<PDFInteger>();
}

void PDFAppToolGUIExtractImages::setPagesMode(PagesMode mode, const QString& rangeText)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    PDFDocumentToolPageSelection selection;
    switch (mode)
    {
        case PagesMode::All:
            break;
        case PagesMode::Current:
        {
            const PDFAppToolHost* host = getHost();
            const PDFInteger page = host ? host->getCurrentPage() : 0;
            selection.pages = QString::number(qMax(PDFInteger(0), page) + 1);
            break;
        }
        case PagesMode::Range:
            selection.pages = rangeText;
            break;
    }

    m_pagesMode = mode;
    QPointer<PDFAppToolGUIExtractImages> guard(this);
    const QString error = model->setValue(PDFAppToolGUIExtractImagesHelper::name(PDFDocumentToolExtractImages::PARAMETER_PAGES), selection.toVariant());
    if (guard && error.isEmpty())
    {
        updateToolbar();
    }
}

void PDFAppToolGUIExtractImages::setExcluded(const std::vector<PDFDocumentImageList::Key>& keys, bool isExcluded)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || keys.empty())
    {
        return;
    }

    // A set: Exclude all of 10 000 images is not quadratic
    const std::vector<PDFDocumentImageList::Key> oldExcluded = getExcludedKeys();
    std::set<PDFDocumentImageList::Key> excludedSet(oldExcluded.cbegin(), oldExcluded.cend());
    for (const PDFDocumentImageList::Key& key : keys)
    {
        if (isExcluded)
        {
            excludedSet.insert(key);
        }
        else
        {
            excludedSet.erase(key);
        }
    }
    const std::vector<PDFDocumentImageList::Key> excluded(excludedSet.cbegin(), excludedSet.cend());
    model->setValue(PDFAppToolGUIExtractImagesHelper::name(PDFDocumentToolExtractImages::PARAMETER_EXCLUDE), PDFDocumentToolExtractImages::writeKeys(excluded));
}

void PDFAppToolGUIExtractImages::includeAll()
{
    // Include all: every image of the chosen pages is exported again (a left out image of another
    // page, which is not listed now, stays left out - as Exclude all)
    if (!m_list)
    {
        return;
    }
    const std::vector<PDFInteger> pages = getChosenPages();
    const std::set<PDFInteger> pageSet(pages.cbegin(), pages.cend());
    std::vector<PDFDocumentImageList::Key> keys;
    for (const PDFDocumentImageList::Item& item : m_list->getItems())
    {
        for (PDFInteger pageIndex : item.pageIndices)
        {
            if (pageSet.count(pageIndex))
            {
                keys.push_back(item.key);
                break;
            }
        }
    }
    setExcluded(keys, false);
}

void PDFAppToolGUIExtractImages::excludeAll()
{
    // Exclude all: every image of the chosen pages is left out
    if (!m_list)
    {
        return;
    }
    const std::vector<PDFInteger> pages = getChosenPages();
    const std::set<PDFInteger> pageSet(pages.cbegin(), pages.cend());
    std::vector<PDFDocumentImageList::Key> keys;
    for (const PDFDocumentImageList::Item& item : m_list->getItems())
    {
        for (PDFInteger pageIndex : item.pageIndices)
        {
            if (pageSet.count(pageIndex))
            {
                keys.push_back(item.key);
                break;
            }
        }
    }
    setExcluded(keys, true);
}

void PDFAppToolGUIExtractImages::copyImage()
{
    // Copy Image (R4-12): the selected image decoded as Extract Images writes it
    const PDFDocumentImageList::Item* item = getCurrentItem();
    PDFAppDocument* document = getDocument();
    if (!item || !document || !m_list)
    {
        return;
    }

    // Bit 5 of Table 22 (ISO 32000-2, 7.6.4.2; the shared check, owner 2026-11-08): the image on
    // the clipboard is a copy of the content, as Copy of a text selection. A restricted document
    // opens the owner password dialog (no nested event loop) and the user copies again after the
    // unlock.
    const PDFAppToolHost* host = getHost();
    QWidget* dialogParent = host && host->getKeyWidget() ? host->getKeyWidget()->window() : nullptr;
    if (!PDFAppDocumentPermissions::request(document, PDFAppPermissionRequest::create(PDFAppPermissionAction::CopyContent), dialogParent))
    {
        return;
    }

    // The item may belong to another list after the foreign code of the check; the list must be
    // the list of the version, which is decoded (RS4APP: after a new version of the document the
    // old list names the objects of the old one - the copy waits for the new list)
    item = getCurrentItem();
    document = getDocument();
    PDFAppToolHost* currentHost = getHost();
    if (!item || !m_list || !document || !m_decoder)
    {
        return;
    }
    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    if (m_listVersion != snapshot.version)
    {
        if (currentHost)
        {
            currentHost->showTransientMessage(tr("The images of the new version of the document are being listed. Copy the image again in a moment."));
        }
        return;
    }

    PDFAppToolImageDecoder::Request request;
    request.item = *item;
    m_decoder->cancelAll();
    m_copyNumber = m_list->indexOf(item->key) + 1;
    m_copyTicket = m_decoder->decode(snapshot.document, currentHost ? currentHost->createEnvironment().cms : PDFCMSPointer(), { request }, 0);
}

void PDFAppToolGUIExtractImages::onImagesDecoded(quint64 ticket, const QList<QImage>& images)
{
    if (ticket != m_copyTicket || images.isEmpty())
    {
        return;
    }
    m_copyTicket = 0;

    PDFAppToolHost* host = getHost();
    if (images.front().isNull())
    {
        if (host)
        {
            host->showTransientMessage(tr("Image %1 cannot be read.").arg(PDFAppLocale::formatNumber(m_copyNumber)));
        }
        return;
    }

    // 2.7: "Image 3 was copied to the clipboard"; the clipboard runs foreign code (its listeners)
    const QString message = tr("Image %1 was copied to the clipboard").arg(PDFAppLocale::formatNumber(m_copyNumber));
    QPointer<PDFAppToolGUIExtractImages> guard(this);
    QApplication::clipboard()->setImage(images.front());
    host = guard ? getHost() : nullptr;
    if (host)
    {
        host->showTransientMessage(message);
    }
}

const PDFDocumentImageList::Item* PDFAppToolGUIExtractImages::getCurrentItem() const
{
    return m_imageList && m_list ? m_list->findItem(m_imageList->getCurrentKey()) : nullptr;
}

QString PDFAppToolGUIExtractImages::getFileName(const PDFDocumentImageList::Item& item) const
{
    if (!m_plan || !m_list)
    {
        return QString();
    }
    const int index = m_list->indexOf(item.key);
    return index >= 0 ? m_plan->getFileName(size_t(index)) : QString();
}

// ------------------------------------------------------------- computation

void PDFAppToolGUIExtractImages::startComputation()
{
    const PDFAppDocument* document = getDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !model || !getShell())
    {
        return;
    }

    if (m_control)
    {
        m_control->cancel();
    }
    if (m_watcher)
    {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
    }

    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    PDFAppToolExtractCollectionJob job;
    job.document = snapshot.document;
    job.list = m_list && m_listVersion == snapshot.version ? m_list : std::shared_ptr<const PDFDocumentImageList>();
    job.planCache = job.list ? m_planCache : std::shared_ptr<PDFDocumentToolExtractImages::ExportPlanCache>();
    job.layerStates = PDFAppToolGUIExtractImagesHelper::getLayerStates(document);
    job.parameters = getRunParameters();
    job.sourceFileName = snapshot.filePath;
    job.date = QDate::currentDate();
    job.version = snapshot.version;
    job.serial = ++m_serial;
    job.control = std::make_shared<PDFAppToolExtractCollectionControl>();
    m_control = job.control;
    m_isComputing = true;

    // Owner 2026-11-09: one asynchronous job may be QtConcurrent::run; the watcher is a child of
    // the GUI, its signal comes in the GUI thread
    m_watcher = new QFutureWatcher<PDFAppToolExtractCollection>(this);
    connect(m_watcher.data(), &QFutureWatcher<PDFAppToolExtractCollection>::finished, this, &PDFAppToolGUIExtractImages::onComputationFinished);
    m_watcher->setFuture(QtConcurrent::run(std::move(job)));
}

void PDFAppToolGUIExtractImages::onComputationFinished()
{
    QFutureWatcher<PDFAppToolExtractCollection>* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFAppToolExtractCollection result = watcher->result();
    m_watcher.clear();
    watcher->deleteLater();
    if (result.serial != m_serial)
    {
        return;
    }

    m_isComputing = false;
    const bool isNewList = result.list != m_list;
    m_list = result.list;
    m_listVersion = result.version;
    m_planCache = result.planCache;
    m_plan = result.plan;
    m_planError = result.planError;

    if (m_imageList && isNewList)
    {
        const std::vector<PDFDocumentImageList::Key> selected = m_imageList->getSelectedKeys();
        m_imageList->getModel()->setImageList(m_list, m_listVersion);
        const PDFAppDocument* document = getDocument();
        const PDFAppToolHost* host = getHost();
        m_imageList->setThumbnailSource(document ? document->getSnapshot().document : PDFDocumentPointer(), host ? host->createEnvironment().cms : PDFCMSPointer());
        m_isUpdatingSelection = true;
        m_imageList->setSelectedKeys(selected);
        m_isUpdatingSelection = false;
    }

    // The state of the Primary button and the status message: their slots may delete the GUI
    QPointer<PDFAppToolGUIExtractImages> guard(this);
    updateParts();
    if (guard)
    {
        updateConfirmState();
    }
    if (guard)
    {
        updateStatusText();
    }
}

// ------------------------------------------------------------- parts

QWidget* PDFAppToolGUIExtractImages::createCanvasPage(QWidget* parent)
{
    // document-canvas 341:7516: the bar and the page with the selected image; 341:8976 the empty state
    QWidget* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("toolExtractCanvas"));
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_canvas = new PDFAppToolPageCanvas(1, page);
    m_canvas->setObjectName(QStringLiteral("toolExtractPageCanvas"));
    layout->addWidget(m_canvas, 1);
    if (PDFAppToolPageStage* stage = m_canvas->getStage(0))
    {
        const PDFAppToolHost* host = getHost();
        const PDFAppDocumentView* view = host ? host->getView() : nullptr;
        stage->setProxy(view ? view->getProxy() : nullptr);
        m_overlay = new PDFAppToolImagePlacementOverlay(stage);
        connect(m_overlay.data(), &PDFAppToolImagePlacementOverlay::imageClicked, this, &PDFAppToolGUIExtractImages::onPageImageClicked);
    }
    m_shownPage = -1;

    // empty-state 341:8976: the icon tile, the title Semi Bold 15, the text, Search All Pages
    m_emptyState = new PDFAppToolExtractEmptyState(page);
    m_emptyState->setObjectName(QStringLiteral("toolExtractEmpty"));
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(20, 20, 20, 20);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(1);
    m_emptyTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 15, PDFThemeColors::TextPrimary, m_emptyState);
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyTitle);
    m_emptyText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, m_emptyState);
    m_emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyText);
    QPushButton* searchAll = PDFAppPageWidgets::createButton(tr("Search All Pages"), QStringLiteral("pages"), true, m_emptyState);
    searchAll->setObjectName(QStringLiteral("toolExtractSearchAll"));
    connect(searchAll, &QPushButton::clicked, this, &PDFAppToolGUIExtractImages::onSearchAllPages);
    emptyLayout->addWidget(searchAll, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    m_emptyState->hide();
    layout->addWidget(m_emptyState, 1);

    updateCanvas();
    return page;
}

bool PDFAppToolGUIExtractImages::isConfirmEnabled() const
{
    // Something to export: the plan of the current parameters with an image
    return BaseClass::isConfirmEnabled() && !m_isComputing && m_plan.has_value() && !m_plan->images.empty();
}

QString PDFAppToolGUIExtractImages::getConfirmText() const
{
    return tr("Export Images");
}

QString PDFAppToolGUIExtractImages::getStatusDetailText() const
{
    if (!m_list)
    {
        return QString();
    }

    // 341:7516 "412 images found · 409 will be exported · about 386 MB" (R4-19: the bytes of the
    // streams of the exported images, "about" - the written size depends on the format)
    const int count = int(m_list->getItems().size());
    if (!m_plan)
    {
        return tr("%Ln images found", nullptr, count);
    }

    qint64 bytes = 0;
    for (const PDFDocumentToolExtractImages::ExportPlan::Image& image : m_plan->images)
    {
        if (image.itemIndex < m_list->getItems().size())
        {
            const PDFDocumentImageList::Item& item = m_list->getItems()[image.itemIndex];
            bytes += item.streamBytes + item.maskBytes;
        }
    }
    return tr("%1 · %2 · %3").arg(tr("%Ln images found", nullptr, count), tr("%Ln will be exported", nullptr, int(m_plan->images.size())), tr("about %1").arg(PDFDocumentToolFormat::formatSize(bytes)));
}

void PDFAppToolGUIExtractImages::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIExtractImagesHelper;
    using Tool = Helper::Tool;

    // panel-pages 341:7936: All pages, Current page, Page range (the field and Apply in the panel)
    m_pagesPanel = builder.addMenu(QStringLiteral("pages"), QStringLiteral("pages"), QString(), PAGES_MENU_WIDTH);
    m_pagesRows.clear();
    const std::pair<PagesMode, const char*> pagesRows[] = { { PagesMode::All, "pages" }, { PagesMode::Current, "page-current" }, { PagesMode::Range, "hash" } };
    for (const auto& pagesRow : pagesRows)
    {
        PDFTileRow* row = m_pagesPanel->addRow(QString(), QString(), QString::fromLatin1(pagesRow.second), PDFTileRow::Kind::MenuItem);
        row->setProperty("pagesMode", int(pagesRow.first));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIExtractImages::onPagesRowClicked);
        m_pagesRows[int(pagesRow.first)] = row;
    }

    // popover-range 341:7936: the field, the count of the pages and the images, Cancel and Apply
    QBoxLayout* panelLayout = qobject_cast<QBoxLayout*>(m_pagesRows.begin()->second->parentWidget()->layout());
    m_rangeEditor = new QWidget(m_pagesRows.begin()->second->parentWidget());
    m_rangeEditor->setObjectName(QStringLiteral("toolExtractRangeEditor"));
    QVBoxLayout* rangeLayout = new QVBoxLayout(m_rangeEditor);
    rangeLayout->setContentsMargins(9, 6, 9, 6);
    rangeLayout->setSpacing(8);
    m_rangeField = new PDFLineEdit(m_rangeEditor);
    m_rangeField->setObjectName(QStringLiteral("toolExtractRangeField"));
    connect(m_rangeField.data(), &QLineEdit::returnPressed, this, &PDFAppToolGUIExtractImages::onApplyRange);
    rangeLayout->addWidget(m_rangeField);
    m_rangeMessage = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, m_rangeEditor);
    m_rangeMessage->setWordWrap(true);
    rangeLayout->addWidget(m_rangeMessage);
    QWidget* actions = new QWidget(m_rangeEditor);
    QHBoxLayout* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    actionsLayout->addStretch(1);
    QPushButton* cancel = PDFAppPageWidgets::createButton(tr("Cancel"), QString(), false, actions);
    cancel->setObjectName(QStringLiteral("toolExtractRangeCancel"));
    connect(cancel, &QPushButton::clicked, this, &PDFAppToolGUIExtractImages::onCancelRange);
    actionsLayout->addWidget(cancel);
    QPushButton* apply = PDFAppPageWidgets::createButton(tr("Apply"), QStringLiteral("check"), true, actions);
    apply->setObjectName(QStringLiteral("toolExtractRangeApply"));
    connect(apply, &QPushButton::clicked, this, &PDFAppToolGUIExtractImages::onApplyRange);
    actionsLayout->addWidget(apply);
    rangeLayout->addWidget(actions);
    m_rangeEditor->hide();
    if (panelLayout)
    {
        panelLayout->addWidget(m_rangeEditor);
    }

    // panel-format 341:7936: PNG, JPEG, TIFF, WebP, BMP, More formats (the shared menu of the
    // image exports: ALL FORMATS of 356:11387 after More formats)
    delete m_formatMenu.data();
    m_formatMenu = new PDFAppToolImageFormatMenu(builder.addMenu(QStringLiteral("format"), QStringLiteral("image"), QString(), FORMAT_MENU_WIDTH), this);
    connect(m_formatMenu.data(), &PDFAppToolImageFormatMenu::formatChosen, this, &PDFAppToolGUIExtractImages::onFormatChosen);

    // panel-options 341:7936: Optimized write, Progressive scan write (JPEG only)
    const PDFAppToolParameterModel* model = getModel();
    std::vector<PDFAppToolFlagItem> items;
    for (const char* parameter : { Tool::PARAMETER_OPTIMIZED_WRITE, Tool::PARAMETER_PROGRESSIVE_SCAN_WRITE })
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(parameter)) : nullptr;
        PDFAppToolFlagItem item;
        item.parameter = Helper::name(parameter);
        item.title = definition ? definition->getTitle() : QString();
        item.description = parameter == Tool::PARAMETER_OPTIMIZED_WRITE ? tr("Smaller files, slower export") : tr("The image sharpens gradually while it loads");
        item.iconName = parameter == Tool::PARAMETER_OPTIMIZED_WRITE ? QStringLiteral("zap") : QStringLiteral("layers");
        items.push_back(std::move(item));
    }
    if (PDFAppToolFlagsMenu* options = builder.addFlagsMenu(std::move(items), QStringLiteral("settings"), QString(), OPTIONS_MENU_WIDTH, tr("Not available for PNG · JPEG only"), PDFTileRow::Kind::MenuItem))
    {
        if (QToolButton* button = options->getButton())
        {
            button->setText(tr("Options"));
        }
    }

    builder.addActionDivider();
    m_includeAllButton = builder.addAction(tr("Include all"), QStringLiteral("check-circle"), this, &PDFAppToolGUIExtractImages::includeAll);
    m_excludeAllButton = builder.addAction(tr("Exclude all"), QStringLiteral("x-circle"), this, &PDFAppToolGUIExtractImages::excludeAll);
    builder.addActionDivider();
    m_copyButton = builder.addAction(tr("Copy Image"), QStringLiteral("clipboard"), this, &PDFAppToolGUIExtractImages::copyImage);
    updateToolbar();
}

void PDFAppToolGUIExtractImages::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 341:7543: x 12, width 275
    if (QWidget* widget = builder.getWidget())
    {
        if (QLayout* layout = widget->layout())
        {
            layout->setContentsMargins(2, 0, 2, 0);
        }
    }

    m_imageList = new PDFAppToolImageList(builder.getWidget());
    m_imageList->setObjectName(QStringLiteral("toolImageList"));
    m_imageList->setRowStyle(PDFAppToolImageListModel::RowStyle::Export);
    m_imageList->setSortVisible(false);
    m_imageList->setTipText(tr("Click an image in the list or on the page. Space leaves the selected images out or puts them back. Identical pictures are exported once."));
    connect(m_imageList.data(), &PDFAppToolImageList::selectionChanged, this, &PDFAppToolGUIExtractImages::onImageSelectionChanged);
    connect(m_imageList.data(), &PDFAppToolImageList::markToggled, this, &PDFAppToolGUIExtractImages::onMarkToggled);
    builder.addWidget(m_imageList);
    builder.setFillsPanel(m_imageList);

    if (m_list)
    {
        const PDFAppDocument* document = getDocument();
        const PDFAppToolHost* host = getHost();
        m_imageList->getModel()->setImageList(m_list, m_listVersion);
        m_imageList->setThumbnailSource(document ? document->getSnapshot().document : PDFDocumentPointer(), host ? host->createEnvironment().cms : PDFCMSPointer());
    }
    else
    {
        m_imageList->setEmptyText(tr("Looking for images…"));
    }
    updateRows();
    updateHead();
    if (!m_watcher)
    {
        startComputation();
    }
}

void PDFAppToolGUIExtractImages::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIExtractImagesHelper;
    using Tool = Helper::Tool;

    // section-Output files 341:7516
    if (!m_outputFiles)
    {
        m_outputFiles = new PDFAppToolOutputFilesSection(this);
    }
    m_outputFiles->describe(builder);

    // section-Image 341:7516: the card of the fields Compression and Quality (the rows of PDF to
    // Images 339:3546) and the note; -1 of a field is the default of the format, said by a word
    builder.addSection(tr("Image"));
    PDFAppToolCard* imageCard = new PDFAppToolCard(builder.getWidget());
    imageCard->setObjectName(QStringLiteral("toolExtractImagesImageCard"));
    imageCard->setFieldSpacing();
    builder.addWidget(imageCard);
    // The controls go straight into their field rows (a second layout on the card would be
    // refused by Qt and would leak with its items)
    const std::pair<const char*, QString> fields[] = { { Tool::PARAMETER_COMPRESSION, tr("Compression") }, { Tool::PARAMETER_QUALITY, tr("Quality") } };
    for (const auto& [parameter, label] : fields)
    {
        if (PDFAppSettingControl* control = builder.createValueControl(Helper::name(parameter), imageCard))
        {
            if (PDFSpinBox* spinBox = control->getSpinBox())
            {
                spinBox->setSpecialValueText(tr("Default"));
            }
            imageCard->getBodyLayout()->addWidget(PDFAppToolCard::createFieldRow(label, control->getWidget(), imageCard));
            (qstrcmp(parameter, Tool::PARAMETER_COMPRESSION) == 0 ? m_compressionControl : m_qualityControl) = control;
        }
    }
    PDFAppParagraph* note = PDFAppPageWidgets::createText(tr("Images keep their pixel size. Colors are converted with the current color management settings."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, imageCard);
    note->setObjectName(QStringLiteral("toolExtractImagesImageNote"));
    note->setMinimumWidth(1);
    imageCard->getBodyLayout()->addWidget(note);
    builder.endSection();

    // section-Selected image 341:7516: Used on pages, Pixels, Size, Saved as
    m_selectedCard = new PDFAppToolSelectedItemCard(tr("Selected image"), builder.getWidget());
    m_selectedCard->setObjectName(QStringLiteral("toolSelectedImage"));
    builder.addWidget(m_selectedCard);

    updateOutputFiles();
    updateSelectedCard();
}

// ------------------------------------------------------------- updates

void PDFAppToolGUIExtractImages::updateParts()
{
    updateRows();
    updateHead();
    updateToolbar();
    updateOutputFiles();
    updateSelectedCard();
    updateCanvas();
}

void PDFAppToolGUIExtractImages::updateRows()
{
    using Model = PDFAppToolImageListModel;

    if (!m_imageList || !m_list)
    {
        return;
    }

    // The rows of the chosen pages only (341:8976 "Images of the chosen pages are listed here")
    const std::vector<PDFInteger> pages = getChosenPages();
    m_imageList->getProxyModel()->setPageFilter(std::set<PDFInteger>(pages.cbegin(), pages.cend()));

    const std::vector<PDFDocumentImageList::Key> excludedKeys = getExcludedKeys();
    const std::set<PDFDocumentImageList::Key> excluded(excludedKeys.cbegin(), excludedKeys.cend());
    std::map<size_t, size_t> duplicateOf;
    std::set<size_t> stencils;
    if (m_plan)
    {
        for (const PDFDocumentToolExtractImages::ExportPlan::Image& image : m_plan->images)
        {
            for (size_t duplicate : image.duplicates)
            {
                duplicateOf[duplicate] = image.itemIndex;
            }
        }
        stencils.insert(m_plan->stencils.cbegin(), m_plan->stencils.cend());
    }

    std::vector<Model::RowState> states;
    const std::vector<PDFDocumentImageList::Item>& items = m_list->getItems();
    states.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        const PDFDocumentImageList::Item& item = items[i];
        Model::RowState state;
        if (excluded.count(item.key))
        {
            // 341:7516 "left out"
            state.tag = Model::Tag::LeftOut;
            state.isMarked = false;
            state.sizeText = tr("left out");
        }
        else if (stencils.count(i))
        {
            // An image mask is a stencil, no picture of its own (8.9.6.2)
            state.isMarked = false;
            state.isDimmed = true;
            state.sizeText = tr("an image mask is not exported");
        }
        else if (duplicateOf.count(i))
        {
            state.sizeText = tr("identical to Image %1").arg(PDFAppLocale::formatNumber(qint64(duplicateOf[i] + 1)));
        }
        else
        {
            state.sizeText = m_plan ? m_plan->getFileName(i) : QString();
        }
        if (item.isInHiddenLayer && state.tag == Model::Tag::None)
        {
            // R4-11: the image is only in layers hidden in the tab
            state.tag = Model::Tag::HiddenLayer;
        }
        states.push_back(std::move(state));
    }
    m_imageList->getModel()->setRowStates(std::move(states));
}

void PDFAppToolGUIExtractImages::updateHead()
{
    if (!m_imageList)
    {
        return;
    }
    if (!m_list)
    {
        m_imageList->setHeadText(QString());
        m_imageList->setChips({}, QString());
        return;
    }

    // 341:7646 "412 images · 409 to export", chips All · To export; 341:8976 "No images on page 308"
    int count = 0;
    int exportCount = 0;
    const std::vector<PDFInteger> pages = getChosenPages();
    const std::set<PDFInteger> pageSet(pages.cbegin(), pages.cend());
    for (int row = 0; row < m_imageList->getModel()->rowCount(); ++row)
    {
        const PDFDocumentImageList::Item* item = m_imageList->getModel()->getItem(row);
        bool isOnPages = false;
        for (PDFInteger pageIndex : item->pageIndices)
        {
            isOnPages = isOnPages || pageSet.count(pageIndex);
        }
        if (isOnPages)
        {
            ++count;
            exportCount += m_imageList->getModel()->getRowState(row).isMarked ? 1 : 0;
        }
    }

    if (count == 0)
    {
        const QString pagesText = PDFAppLocale::formatPageNumbers(pages, 3);
        m_imageList->setHeadText(pages.size() == 1 ? tr("No images on page %1").arg(pagesText) : tr("No images on the chosen pages"));
        m_imageList->setChips({}, QString());
        m_imageList->setEmptyText(tr("Images of the chosen pages are listed here."));
        return;
    }

    m_imageList->setHeadText(tr("%1 · %2").arg(tr("%Ln images", nullptr, count), tr("%Ln to export", nullptr, exportCount)));
    const QString current = m_imageList->getCurrentChipId().isEmpty() ? QStringLiteral("all") : m_imageList->getCurrentChipId();
    m_imageList->setChips({ { QStringLiteral("all"), tr("All"), count }, { QStringLiteral("export"), tr("To export"), exportCount } }, current);
    m_imageList->setEmptyText(tr("No image is shown by this filter."));
}

void PDFAppToolGUIExtractImages::updateToolbar()
{
    using Helper = PDFAppToolGUIExtractImagesHelper;
    using Tool = Helper::Tool;

    const PDFAppToolParameterModel* model = getModel();
    const bool hasImages = m_list && !m_list->getItems().empty() && m_imageList && m_imageList->getProxyModel()->rowCount() > 0;

    // Pages: All (1,592 pages), Current (Page 308), Page range
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getSnapshot().document : PDFDocumentPointer();
    const int pageCount = pdf ? int(pdf->getCatalog()->getPageCount()) : 0;
    const PDFAppToolHost* host = getHost();
    const PDFInteger currentPage = host ? host->getCurrentPage() : 0;
    const std::pair<PagesMode, std::pair<QString, QString>> pagesTexts[] =
    {
        { PagesMode::All, { tr("All pages"), tr("%Ln pages", nullptr, pageCount) } },
        { PagesMode::Current, { tr("Current page"), tr("Page %1").arg(PDFAppLocale::formatNumber(currentPage + 1)) } },
        { PagesMode::Range, { tr("Page range"), tr("For example 1-12,17,24,27-") } }
    };
    for (const auto& pagesText : pagesTexts)
    {
        if (PDFTileRow* row = getPagesRow(pagesText.first))
        {
            const QSignalBlocker blocker(row);
            row->setText(pagesText.second.first);
            row->setDescription(pagesText.second.second);
            row->setChecked(m_pagesMode == pagesText.first);
        }
    }
    if (QToolButton* button = getPagesButton())
    {
        // 341:7516 "Pages: All", 341:8976 "Pages: Current"
        const QString mode = m_pagesMode == PagesMode::All ? tr("All") : (m_pagesMode == PagesMode::Current ? tr("Current") : tr("Range"));
        button->setText(tr("Pages: %1").arg(mode));
    }

    // Format: "Format: PNG"
    const QString format = model ? model->getValue(Helper::name(Tool::PARAMETER_FORMAT)).toString().toLower() : QString();
    if (m_formatMenu)
    {
        m_formatMenu->setFormat(format.toLatin1());
    }
    if (QToolButton* button = getFormatButton())
    {
        button->setEnabled(hasImages);
    }

    // The fields of the card Image, which the format cannot use, are disabled (RS4FIGMA; as PDF to
    // Images 339:3546 and its note "Settings the chosen format does not support are disabled")
    PDFImageWriterSettings writerSettings;
    writerSettings.selectFormat(PDFDocumentToolImageFormat::fromParameters(model ? model->getValues() : QVariantMap()).format);
    if (m_compressionControl)
    {
        m_compressionControl->getWidget()->setEnabled(writerSettings.isOptionSupported(QImageIOHandler::CompressionRatio));
    }
    if (m_qualityControl)
    {
        m_qualityControl->getWidget()->setEnabled(writerSettings.isOptionSupported(QImageIOHandler::Quality));
    }

    // 341:8976: nothing to choose without an image of the chosen pages
    for (const QPointer<QToolButton>& button : { m_includeAllButton, m_excludeAllButton, m_copyButton })
    {
        if (button)
        {
            button->setEnabled(hasImages);
        }
    }
    if (m_copyButton)
    {
        m_copyButton->setEnabled(hasImages && getCurrentItem() != nullptr);
    }
}

void PDFAppToolGUIExtractImages::updateOutputFiles()
{
    if (!m_outputFiles)
    {
        return;
    }

    // name-preview 341:7516: "Report_img_1.png … Report_img_412.png", "409 files · # is replaced
    // by the image number, left-out numbers are skipped"
    QStringList names;
    if (m_plan)
    {
        for (const PDFDocumentToolExtractImages::ExportPlan::Image& image : m_plan->images)
        {
            names << image.fileName;
        }
    }
    const QString hint = m_plan ? tr("%Ln files · # is replaced by the image number, left-out numbers are skipped", nullptr, int(names.size())) : QString();
    m_outputFiles->setPreview(m_planError.isEmpty() ? names : QStringList(), hint, m_planError);
}

void PDFAppToolGUIExtractImages::updateSelectedCard()
{
    if (!m_selectedCard)
    {
        return;
    }

    const std::vector<PDFDocumentImageList::Key> selected = m_imageList ? m_imageList->getSelectedKeys() : std::vector<PDFDocumentImageList::Key>();
    const int total = m_list ? int(m_list->getItems().size()) : 0;
    m_selectedCard->setTagText(selected.empty() ? QString() : tr("%1 of %2").arg(PDFAppLocale::formatNumber(m_list ? m_list->indexOf(selected.front()) + 1 : 0), PDFAppLocale::formatNumber(total)));

    PDFKeyValueList* values = m_selectedCard->getValues();
    values->clear();
    const PDFDocumentImageList::Item* item = getCurrentItem();
    if (item)
    {
        // 341:7516: Used on pages "308, 954", Pixels, Size, Saved as
        values->addRow(tr("Used on pages"), PDFAppLocale::formatPageNumbers(item->pageIndices));
        values->addRow(tr("Pixels"), tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(item->pixelSize.width()), PDFAppLocale::formatNumber(item->pixelSize.height())));
        values->addRow(tr("Size"), PDFDocumentToolFormat::formatSize(item->streamBytes + item->maskBytes));
        const QString fileName = getFileName(*item);
        values->addRow(tr("Saved as"), fileName.isEmpty() ? tr("not exported") : fileName);
    }
    values->setVisible(values->getRowCount() > 0);
}

void PDFAppToolGUIExtractImages::updateCanvas()
{
    if (!m_canvas)
    {
        return;
    }

    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getSnapshot().document : PDFDocumentPointer();
    const std::vector<PDFInteger> pages = getChosenPages();
    const bool hasImages = m_imageList && m_imageList->getProxyModel()->rowCount() > 0;

    // 341:8976: no image on the chosen pages
    const bool isEmpty = m_list && !hasImages && m_imageList && m_imageList->getCurrentChipId() != QLatin1String("export");
    if (m_emptyState)
    {
        m_emptyState->setVisible(isEmpty);
        m_canvas->setVisible(!isEmpty);
        const QString pagesText = PDFAppLocale::formatPageNumbers(pages, 3);
        m_emptyTitle->setText(tr("No images on the chosen pages"));
        m_emptyText->setText(pages.size() == 1 ? tr("Page %1 contains only text and vector drawings.\nChoose other pages in the Pages menu.").arg(pagesText)
                                               : tr("The chosen pages contain only text and vector drawings.\nChoose other pages in the Pages menu."));
    }

    // The page of the selected image (the first of its pages among the chosen ones)
    const PDFDocumentImageList::Item* item = getCurrentItem();
    PDFInteger pageIndex = pages.empty() ? 0 : pages.front();
    if (item && !item->pageIndices.empty())
    {
        pageIndex = item->pageIndices.front();
        const std::set<PDFInteger> pageSet(pages.cbegin(), pages.cend());
        for (PDFInteger candidate : item->pageIndices)
        {
            if (pageSet.count(candidate))
            {
                pageIndex = candidate;
                break;
            }
        }
    }

    // preview-bar 341:7516 "Image 3 · page 308"
    const QString page = PDFAppLocale::formatPageNumbers({ pageIndex });
    m_canvas->setTitle(item ? tr("Image %1 · page %2").arg(PDFAppLocale::formatNumber(m_list->indexOf(item->key) + 1), page) : tr("Page %1").arg(page));

    PDFAppToolPageStage* stage = m_canvas->getStage(0);
    if (stage && pdf && (stage->getDocument() != pdf || m_shownPage != pageIndex))
    {
        m_shownPage = pageIndex;
        stage->setPage(pdf, QStringLiteral("extract-images"), pageIndex);
        if (m_overlay)
        {
            const PDFAppToolHost* host = getHost();
            m_overlay->setPlacements(PDFAppToolImagePlacement::collect(pdf.data(), pageIndex, host ? host->createEnvironment().cms : PDFCMSPointer()));
        }
    }
    if (m_overlay)
    {
        m_overlay->setSelectedKey(item ? item->key : PDFDocumentImageList::Key());
    }
}

// ------------------------------------------------------------- reactions

void PDFAppToolGUIExtractImages::onImageSelectionChanged()
{
    if (m_isUpdatingSelection)
    {
        return;
    }
    updateToolbar();
    updateSelectedCard();
    updateCanvas();
}

void PDFAppToolGUIExtractImages::onMarkToggled(const std::vector<PDFDocumentImageList::Key>& keys, bool isMarked)
{
    setExcluded(keys, !isMarked);
}

void PDFAppToolGUIExtractImages::onPageImageClicked(const QString& key)
{
    // B64: the topmost image under the pointer becomes the selected row
    const std::optional<PDFDocumentImageList::Key> imageKey = PDFDocumentImageList::Key::fromString(key);
    if (!imageKey || !m_imageList)
    {
        return;
    }
    m_imageList->setSelectedKeys({ *imageKey });
    onImageSelectionChanged();
}

void PDFAppToolGUIExtractImages::onPagesRowClicked()
{
    const QObject* row = sender();
    if (!row)
    {
        return;
    }

    const PagesMode mode = PagesMode(row->property("pagesMode").toInt());
    if (mode == PagesMode::Range)
    {
        // The range is written by Apply (popover-range 341:7936)
        if (m_rangeEditor && m_rangeField)
        {
            m_rangeEditor->show();
            m_rangeField->setFocus(Qt::OtherFocusReason);
        }
        updateToolbar();
        return;
    }

    QPointer<PDFAppToolGUIExtractImages> guard(this);
    if (m_pagesPanel)
    {
        m_pagesPanel->close();
    }
    if (guard)
    {
        setPagesMode(mode);
    }
}

void PDFAppToolGUIExtractImages::onApplyRange()
{
    if (!m_rangeField || !getModel())
    {
        return;
    }

    // The range is checked by the parameter (K4.5: the syntax of PDFPageRangeList)
    QString error;
    PDFDocumentToolPageSelection selection;
    selection.pages = m_rangeField->text().trimmed();
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pdf = document ? document->getSnapshot().document : PDFDocumentPointer();
    const std::vector<PDFInteger> pages = pdf ? selection.getPageIndices(pdf.data(), &error) : std::vector<PDFInteger>();
    if (pages.empty())
    {
        m_rangeMessage->setText(error.isEmpty() ? tr("No page is chosen.") : error);
        m_rangeMessage->setTextColor(PDFThemeColors::TextDanger);
        return;
    }

    QPointer<PDFAppToolGUIExtractImages> guard(this);
    m_rangeEditor->hide();
    if (m_pagesPanel)
    {
        m_pagesPanel->close();
    }
    if (guard)
    {
        setPagesMode(PagesMode::Range, selection.pages);
    }
}

void PDFAppToolGUIExtractImages::onCancelRange()
{
    if (m_rangeEditor)
    {
        m_rangeEditor->hide();
    }
    updateToolbar();
}

void PDFAppToolGUIExtractImages::onFormatChosen(const QByteArray& format)
{
    // The menu is closed already; the model runs the slots of the GUI
    QPointer<PDFAppToolGUIExtractImages> guard(this);
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(PDFAppToolGUIExtractImagesHelper::name(PDFDocumentToolExtractImages::PARAMETER_FORMAT), QString::fromLatin1(format));
    }
    if (guard)
    {
        updateToolbar();
    }
}

void PDFAppToolGUIExtractImages::onSearchAllPages()
{
    setPagesMode(PagesMode::All);
}

void PDFAppToolGUIExtractImages::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);

    // The plan follows the parameters (the names, the left out images, the pages)
    if (name == QLatin1String(PDFDocumentToolExtractImages::PARAMETER_PAGES))
    {
        // Undo / Redo of a change of the pages: the mode follows the value - no pages are All
        // (also after a range, RS4APP), the number of the current page stays Current, any other
        // value is a range
        const PDFAppToolParameterModel* model = getModel();
        QString error;
        const std::optional<PDFDocumentToolPageSelection> selection = model ? PDFDocumentToolPageSelection::fromVariant(model->getValue(name), &error) : std::nullopt;
        if (selection && selection->pages.isEmpty())
        {
            m_pagesMode = PagesMode::All;
        }
        else if (selection)
        {
            const PDFAppToolHost* host = getHost();
            const QString currentPage = QString::number(qMax(PDFInteger(0), host ? host->getCurrentPage() : 0) + 1);
            if (m_pagesMode != PagesMode::Current || selection->pages != currentPage)
            {
                m_pagesMode = PagesMode::Range;
            }
        }
    }
    updateRows();
    updateHead();
    updateToolbar();
    updateCanvas();
    if (PDFDocumentToolExtractImages::isExportPlanParameter(name))
    {
        // RS4APP: the encoding, the folder and Existing files keep the plan
        startComputation();
    }
}

void PDFAppToolGUIExtractImages::onDocumentChanged()
{
    startComputation();
}

void PDFAppToolGUIExtractImages::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    if (isRunning)
    {
        for (const QPointer<PDFAppToolPanelPopup>& panel : { m_pagesPanel, QPointer<PDFAppToolPanelPopup>(m_formatMenu ? m_formatMenu->getPanel() : nullptr) })
        {
            if (panel && panel->isVisible())
            {
                panel->hide();
            }
        }
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_extractimages.cpp"
