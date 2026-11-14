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

#include "pdfapptoolgui_optimizeimages.h"

#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolimagecomparison.h"
#include "pdfapptoolimageprofileeditor.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfaccenthue.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_grayscale.h"
#include "pdfdocumenttool_optimizeimages.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfimageoptimizer.h"
#include "pdfoperationcontrol.h"

#include <QCheckBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>

namespace pdf
{

/// The cancellation of a collection of the list of the images (shared by the GUI and the job)
class PDFAppToolImageCollectionControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_isCanceled.load(std::memory_order_relaxed); }
    void cancel() { m_isCanceled.store(true, std::memory_order_relaxed); }

private:
    std::atomic_bool m_isCanceled = false;
};

/// The job of the collection: the shared immutable document, its version and the cancellation -
/// never the GUI (a functor, no lambda)
class PDFAppToolImageCollectionJob
{
public:
    PDFDocumentPointer document;
    quint64 version = 0;
    std::shared_ptr<PDFAppToolImageCollectionControl> control;

    PDFAppToolImageListCollection operator()() const
    {
        PDFAppToolImageListCollection collection;
        collection.version = version;
        collection.list = std::make_shared<const PDFDocumentImageList>(PDFDocumentImageList::create(document.data(), PDFDocumentImageList::Options::createForChange(), control.get(), nullptr));
        return collection;
    }
};

/// Helpers of the GUI of Optimize Images and Grayscale
class PDFAppToolGUIOptimizeImagesHelper
{
public:
    PDFAppToolGUIOptimizeImagesHelper() = delete;

    using Tool = PDFDocumentToolOptimizeImages;
    using Gray = PDFDocumentToolGrayscale;

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// A key of the sorted keys of getExcludedKeys / getOverrideKeys (a document of 10 000 images
    /// is not quadratic)
    static bool contains(const std::vector<PDFDocumentImageList::Key>& sortedKeys, const PDFDocumentImageList::Key& key)
    {
        return std::binary_search(sortedKeys.cbegin(), sortedKeys.cend(), key);
    }

    /// The exceptions without the marked ones, in their order
    static std::vector<Tool::ImageException> removeMarked(std::vector<Tool::ImageException> exceptions, const std::vector<bool>& isRemoved)
    {
        std::vector<Tool::ImageException> result;
        result.reserve(exceptions.size());
        for (size_t i = 0; i < exceptions.size(); ++i)
        {
            if (i >= isRemoved.size() || !isRemoved[i])
            {
                result.push_back(std::move(exceptions[i]));
            }
        }
        return result;
    }

    /// The icons of the color modes of the menu Color (299:3362): Auto and Preserve have icons;
    /// Color (RGB), Grayscale and Bitonal have swatches in Figma, not icons - their tiles stay
    /// empty (RS4FIGMA); nullptr = no icon
    static const char* getColorIcon(const QString& colorMode)
    {
        if (colorMode == QLatin1String(Tool::COLOR_AUTO))
        {
            return "zap";
        }
        if (colorMode == QLatin1String(Tool::COLOR_PRESERVE))
        {
            return "lock";
        }
        return nullptr;
    }

    /// The profile of the editor of a color mode of an override (301:3594: Bitonal shows Bitonal)
    static QString getProfileOfColorMode(const QString& colorMode)
    {
        if (colorMode == QLatin1String(Tool::COLOR_GRAYSCALE))
        {
            return name(Tool::PROFILE_GRAY);
        }
        if (colorMode == QLatin1String(Tool::COLOR_BITONAL))
        {
            return name(Tool::PROFILE_BITONAL);
        }
        if (colorMode == QLatin1String(Tool::COLOR_COLOR))
        {
            return name(Tool::PROFILE_COLOR);
        }
        return QString();
    }

    /// "DeviceRGB · DCTDecode" of an image dictionary of a document (the AFTER of the canvas): the
    /// color space with "1 bit" for a bitonal image, the filter as the panel names it (JPEG 85)
    static QString describeEncoding(const PDFDocument* document, const PDFDictionary* dictionary, int jpegQuality)
    {
        QString colorSpace;
        const PDFObject& colorSpaceObject = document->getObject(dictionary->get("ColorSpace"));
        if (colorSpaceObject.isName())
        {
            colorSpace = QString::fromLatin1(colorSpaceObject.getString());
        }
        else if (colorSpaceObject.isArray() && colorSpaceObject.getArray()->getCount() > 0)
        {
            const PDFObject& family = document->getObject(colorSpaceObject.getArray()->getItem(0));
            colorSpace = family.isName() ? QString::fromLatin1(family.getString()) : QString();
        }
        const PDFObject& bitsObject = document->getObject(dictionary->get("BitsPerComponent"));
        const PDFObject& maskObject = document->getObject(dictionary->get("ImageMask"));
        const bool isImageMask = maskObject.isBool() && maskObject.getBool();
        if (isImageMask || (bitsObject.isInt() && bitsObject.getInteger() == 1))
        {
            colorSpace = colorSpace.isEmpty() ? PDFAppToolGUIOptimizeImages::tr("1 bit") : PDFAppToolGUIOptimizeImages::tr("%1 1 bit").arg(colorSpace);
        }

        const QString filter = PDFImageOptimizer::readFilterName(document, dictionary);
        QString encoding = filter;
        if (filter == QLatin1String("DCTDecode"))
        {
            encoding = jpegQuality >= 0 ? PDFAppToolGUIOptimizeImages::tr("JPEG %1").arg(jpegQuality) : PDFAppToolGUIOptimizeImages::tr("JPEG");
        }
        else if (filter == QLatin1String("JPXDecode"))
        {
            encoding = PDFAppToolGUIOptimizeImages::tr("JPEG 2000");
        }
        else if (filter == QLatin1String("JBIG2Decode"))
        {
            encoding = PDFAppToolGUIOptimizeImages::tr("JBIG2");
        }
        else if (filter == QLatin1String("CCITTFaxDecode"))
        {
            encoding = PDFAppToolGUIOptimizeImages::tr("CCITT G4");
        }
        else if (filter.isEmpty())
        {
            encoding = PDFAppToolGUIOptimizeImages::tr("not compressed");
        }

        QStringList parts;
        if (!colorSpace.isEmpty())
        {
            parts << colorSpace;
        }
        parts << encoding;
        return parts.join(QStringLiteral(" · "));
    }

    /// The pixel size of an image dictionary (empty = not declared)
    static QSize getPixelSize(const PDFDocument* document, const PDFDictionary* dictionary)
    {
        const PDFObject& width = document->getObject(dictionary->get("Width"));
        const PDFObject& height = document->getObject(dictionary->get("Height"));
        if (!width.isInt() || !height.isInt() || width.getInteger() <= 0 || height.getInteger() <= 0 || width.getInteger() > INT_MAX || height.getInteger() > INT_MAX)
        {
            return QSize();
        }
        return QSize(int(width.getInteger()), int(height.getInteger()));
    }

    static QString formatPixels(const QSize& size)
    {
        return PDFAppToolGUIOptimizeImages::tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(size.width()), PDFAppLocale::formatNumber(size.height()));
    }

    /// The resolution of an item: the smaller of the axes, 0 = not known
    static int getDpi(const PDFDocumentImageList::Item& item)
    {
        const qreal x = item.minimalDpi.x();
        const qreal y = item.minimalDpi.y();
        if (x > 0.0 && y > 0.0)
        {
            return qRound(qMin(x, y));
        }
        return qRound(qMax(qMax(x, y), 0.0));
    }

    /// "84 %" of the saving (Compression ratio 298:3442; owner 14. 11.: percentages are whole
    /// numbers in the tools of step 4, the rounding of the facts of the core)
    static QString formatRatio(qint64 before, qint64 after)
    {
        if (before <= 0)
        {
            return QStringLiteral("–");
        }
        return PDFAppToolGUIOptimizeImages::tr("%1 %").arg(PDFAppLocale::formatNumber(PDFDocumentToolFormat::getSavedPercent(before, after)));
    }

    /// The canvas of the item, when nothing is shown
    static constexpr int LIST_MARGIN = 2;
};

// ---------------------------------------------------------- PDFAppToolGUIOptimizeImages

PDFAppToolGUIOptimizeImages::PDFAppToolGUIOptimizeImages(QObject* parent) :
    BaseClass(parent)
{
    m_decoder = new PDFAppToolImageDecoder(this);
    connect(m_decoder.data(), &PDFAppToolImageDecoder::imagesDecoded, this, &PDFAppToolGUIOptimizeImages::onImagesDecoded);
}

PDFAppToolGUIOptimizeImages::~PDFAppToolGUIOptimizeImages()
{
    // The collection goes on with its own values and its cancellation, its result is dropped by
    // the watcher, which dies with the GUI; the decoder cuts its link (nobody waits)
    if (m_collectionControl)
    {
        m_collectionControl->cancel();
    }
    delete m_decoder.data();
}

void PDFAppToolGUIOptimizeImages::registerOptimizeImagesTools()
{
    using Factory = PDFDocumentToolGUIFactoryOf<PDFAppToolGUIOptimizeImages>;

    const QString imagesId = QString::fromLatin1(PDFDocumentToolOptimizeImages::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(imagesId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolOptimizeImages>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(imagesId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(imagesId, std::make_shared<Factory>());
    }

    // Q4.4: the same class in the mode Grayscale (the id of the catalog entry decides)
    const QString grayscaleId = QString::fromLatin1(PDFDocumentToolGrayscale::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(grayscaleId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolGrayscale>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(grayscaleId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(grayscaleId, std::make_shared<Factory>());
    }
}

bool PDFAppToolGUIOptimizeImages::isGrayscale() const
{
    const PDFDocumentToolCatalogEntry* entry = getTool();
    return entry && entry->getId() == QLatin1String(PDFDocumentToolGrayscale::TOOL_ID);
}

QToolButton* PDFAppToolGUIOptimizeImages::getColorButton() const
{
    return m_colorPanel ? m_colorPanel->getButton() : nullptr;
}

PDFAppToolEnumMenu* PDFAppToolGUIOptimizeImages::getModeMenu() const
{
    return m_modeMenu.data();
}

PDFAppToolEnumMenu* PDFAppToolGUIOptimizeImages::getGoalMenu() const
{
    return m_goalMenu.data();
}

PDFAppToolFlagsMenu* PDFAppToolGUIOptimizeImages::getOptionsMenu() const
{
    return m_optionsMenu.data();
}

PDFTileRow* PDFAppToolGUIOptimizeImages::getColorRow(const QString& colorMode) const
{
    auto it = m_colorRows.find(colorMode);
    return it != m_colorRows.end() ? it->second.data() : nullptr;
}

// ------------------------------------------------------------- exceptions

std::vector<PDFDocumentImageList::Key> PDFAppToolGUIOptimizeImages::getExcludedKeys() const
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    std::vector<PDFDocumentImageList::Key> keys;
    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return keys;
    }

    QString error;
    const std::vector<Helper::Tool::ImageException> exceptions = isGrayscale() ? Helper::Gray::parseExclude(model->getValue(Helper::name(Helper::Gray::PARAMETER_EXCLUDE)).toString(), &error)
                                                                               : Helper::Tool::parseImageExceptions(model->getValue(Helper::name(Helper::Tool::PARAMETER_IMAGES)).toString(), &error);
    for (const Helper::Tool::ImageException& exception : exceptions)
    {
        if (!exception.enabled)
        {
            keys.push_back(exception.key);
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<PDFDocumentImageList::Key> PDFAppToolGUIOptimizeImages::getOverrideKeys() const
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    std::vector<PDFDocumentImageList::Key> keys;
    const PDFAppToolParameterModel* model = getModel();
    if (!model || isGrayscale())
    {
        return keys;
    }

    QString error;
    for (const Helper::Tool::ImageException& exception : Helper::Tool::parseImageExceptions(model->getValue(Helper::name(Helper::Tool::PARAMETER_IMAGES)).toString(), &error))
    {
        if (!exception.settings.isEmpty())
        {
            keys.push_back(exception.key);
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool PDFAppToolGUIOptimizeImages::isExcluded(const PDFDocumentImageList::Key& key) const
{
    return PDFAppToolGUIOptimizeImagesHelper::contains(getExcludedKeys(), key);
}

bool PDFAppToolGUIOptimizeImages::hasOverride(const PDFDocumentImageList::Key& key) const
{
    return PDFAppToolGUIOptimizeImagesHelper::contains(getOverrideKeys(), key);
}

bool PDFAppToolGUIOptimizeImages::isChangeable(const PDFDocumentImageList::Item& item) const
{
    // R4-8: an inline image is never changed; Grayscale converts only the color images (R4-5)
    return !item.isKeptByChangingTools() && (!isGrayscale() || item.isColorImage());
}

std::vector<PDFDocumentImageList::Key> PDFAppToolGUIOptimizeImages::getChangeableSelection() const
{
    std::vector<PDFDocumentImageList::Key> keys;
    if (!m_imageList || !m_list)
    {
        return keys;
    }
    for (const PDFDocumentImageList::Key& key : m_imageList->getSelectedKeys())
    {
        const PDFDocumentImageList::Item* item = m_list->findItem(key);
        if (item && isChangeable(*item))
        {
            keys.push_back(key);
        }
    }
    return keys;
}

void PDFAppToolGUIOptimizeImages::setExcluded(const std::vector<PDFDocumentImageList::Key>& keys, bool isExcludedValue)
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model || keys.empty())
    {
        return;
    }

    if (isGrayscale())
    {
        // "exclude": the ids separated by commas (a set: Exclude of 10 000 images is not quadratic)
        const std::vector<PDFDocumentImageList::Key> oldExcluded = getExcludedKeys();
        std::set<PDFDocumentImageList::Key> excludedSet(oldExcluded.cbegin(), oldExcluded.cend());
        for (const PDFDocumentImageList::Key& key : keys)
        {
            if (isExcludedValue)
            {
                excludedSet.insert(key);
            }
            else
            {
                excludedSet.erase(key);
            }
        }
        const std::vector<PDFDocumentImageList::Key> excluded(excludedSet.cbegin(), excludedSet.cend());
        model->setValue(Helper::name(Helper::Gray::PARAMETER_EXCLUDE), Helper::Gray::writeExclude(excluded));
        return;
    }

    // "images": an exclusion keeps the override of the image (it applies again, when it is back)
    QString error;
    const QString parameter = Helper::name(Helper::Tool::PARAMETER_IMAGES);
    std::vector<Helper::Tool::ImageException> exceptions = Helper::Tool::parseImageExceptions(model->getValue(parameter).toString(), &error);
    std::map<PDFDocumentImageList::Key, size_t> indices;
    for (size_t i = 0; i < exceptions.size(); ++i)
    {
        indices.emplace(exceptions[i].key, i);
    }
    std::vector<bool> isRemoved(exceptions.size(), false);
    for (const PDFDocumentImageList::Key& key : keys)
    {
        auto it = indices.find(key);
        if (it == indices.end())
        {
            if (!isExcludedValue)
            {
                continue;
            }
            Helper::Tool::ImageException exception;
            exception.key = key;
            exceptions.push_back(std::move(exception));
            isRemoved.push_back(false);
            it = indices.emplace(key, exceptions.size() - 1).first;
        }
        Helper::Tool::ImageException& exception = exceptions[it->second];
        exception.enabled = !isExcludedValue;
        isRemoved[it->second] = exception.enabled && exception.settings.isEmpty();
    }
    model->setValue(parameter, Helper::Tool::writeImageExceptions(Helper::removeMarked(std::move(exceptions), isRemoved)));
}

void PDFAppToolGUIOptimizeImages::setOverride(const std::vector<PDFDocumentImageList::Key>& keys, bool hasOverrideValue)
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model || keys.empty() || isGrayscale())
    {
        return;
    }

    QString error;
    const QString parameter = Helper::name(Helper::Tool::PARAMETER_IMAGES);
    std::vector<Helper::Tool::ImageException> exceptions = Helper::Tool::parseImageExceptions(model->getValue(parameter).toString(), &error);
    const QString globalColorMode = model->getValue(Helper::name(Helper::Tool::PARAMETER_COLOR_MODE)).toString();
    std::map<PDFDocumentImageList::Key, size_t> indices;
    for (size_t i = 0; i < exceptions.size(); ++i)
    {
        indices.emplace(exceptions[i].key, i);
    }
    std::vector<bool> isRemoved(exceptions.size(), false);
    for (const PDFDocumentImageList::Key& key : keys)
    {
        auto it = indices.find(key);
        if (hasOverrideValue)
        {
            if (it == indices.end())
            {
                Helper::Tool::ImageException exception;
                exception.key = key;
                exceptions.push_back(std::move(exception));
                isRemoved.push_back(false);
                it = indices.emplace(key, exceptions.size() - 1).first;
            }
            if (exceptions[it->second].settings.isEmpty())
            {
                // The override starts as "Mode: Custom" (301:3594, owner 14. 11.) with the color of
                // the global defaults (the editor and the menus change them then)
                exceptions[it->second].settings.insert(Helper::name(Helper::Tool::EXCEPTION_MODE), Helper::name(Helper::Tool::MODE_CUSTOM));
                exceptions[it->second].settings.insert(Helper::name(Helper::Tool::EXCEPTION_COLOR_MODE), globalColorMode);
            }
        }
        else if (it != indices.end())
        {
            exceptions[it->second].settings.clear();
            isRemoved[it->second] = exceptions[it->second].enabled;
        }
    }
    model->setValue(parameter, Helper::Tool::writeImageExceptions(Helper::removeMarked(std::move(exceptions), isRemoved)));
}

void PDFAppToolGUIOptimizeImages::toggleExclude()
{
    const std::vector<PDFDocumentImageList::Key> keys = getChangeableSelection();
    const std::vector<PDFDocumentImageList::Key> excluded = getExcludedKeys();
    bool isAllExcluded = !keys.empty();
    for (const PDFDocumentImageList::Key& key : keys)
    {
        isAllExcluded = isAllExcluded && PDFAppToolGUIOptimizeImagesHelper::contains(excluded, key);
    }
    setExcluded(keys, !isAllExcluded);
}

void PDFAppToolGUIOptimizeImages::onImageMarksToggled()
{
    // Space of the list (plan 4.1): Exclude of the selected images, or their return
    toggleExclude();
}

void PDFAppToolGUIOptimizeImages::toggleOverride()
{
    const std::vector<PDFDocumentImageList::Key> keys = getChangeableSelection();
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    bool isAllOverridden = !keys.empty();
    for (const PDFDocumentImageList::Key& key : keys)
    {
        isAllOverridden = isAllOverridden && PDFAppToolGUIOptimizeImagesHelper::contains(overrides, key);
    }

    QPointer<PDFAppToolGUIOptimizeImages> guard(this);
    setOverride(keys, !isAllOverridden);
    if (guard)
    {
        // The model refuses nothing here, but the button keeps the state of the model
        updateToolbar();
    }
}

QString PDFAppToolGUIOptimizeImages::getShownColorMode() const
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    const PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return QString();
    }

    // 301:3594: the menu shows the color of the override of the selection
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    for (const PDFDocumentImageList::Key& key : getChangeableSelection())
    {
        if (Helper::contains(overrides, key))
        {
            const QVariant value = PDFAppToolImageOverrideStore::readSetting(model, key, Helper::name(Helper::Tool::EXCEPTION_COLOR_MODE));
            if (value.isValid())
            {
                return value.toString();
            }
            break;
        }
    }
    return model->getValue(Helper::name(Helper::Tool::PARAMETER_COLOR_MODE)).toString();
}

const PDFAppToolGUIOptimizeImages::ImageResult* PDFAppToolGUIOptimizeImages::getImageResult(const PDFDocumentImageList::Key& key) const
{
    auto it = m_imageResults.find(key.toString());
    return it != m_imageResults.end() ? &it->second : nullptr;
}

const PDFDocumentImageList::Item* PDFAppToolGUIOptimizeImages::getCurrentItem() const
{
    if (!m_imageList || !m_list)
    {
        return nullptr;
    }
    return m_list->findItem(m_imageList->getCurrentKey());
}

// ------------------------------------------------------------- the list of the images

void PDFAppToolGUIOptimizeImages::startCollection()
{
    const PDFAppDocument* document = getDocument();
    if (!document || !getShell())
    {
        return;
    }

    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    if (m_list && m_listVersion == snapshot.version)
    {
        return;
    }

    if (m_collectionControl)
    {
        m_collectionControl->cancel();
    }
    if (m_collectionWatcher)
    {
        // The result of the old version is not wanted; the watcher dies, the job ends by itself
        m_collectionWatcher->disconnect(this);
        m_collectionWatcher->deleteLater();
    }

    PDFAppToolImageCollectionJob job;
    job.document = snapshot.document;
    job.version = snapshot.version;
    job.control = std::make_shared<PDFAppToolImageCollectionControl>();
    m_collectionControl = job.control;
    m_isCollecting = true;

    // Owner 2026-11-09: one asynchronous job may be QtConcurrent::run (its walk over the pages is
    // PDFExecutionPolicy); the watcher is a child of the GUI, its signal comes in the GUI thread
    m_collectionWatcher = new QFutureWatcher<PDFAppToolImageListCollection>(this);
    connect(m_collectionWatcher.data(), &QFutureWatcher<PDFAppToolImageListCollection>::finished, this, &PDFAppToolGUIOptimizeImages::onCollectionFinished);
    m_collectionWatcher->setFuture(QtConcurrent::run(std::move(job)));
}

void PDFAppToolGUIOptimizeImages::onCollectionFinished()
{
    QFutureWatcher<PDFAppToolImageListCollection>* watcher = m_collectionWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFAppToolImageListCollection collection = watcher->result();
    m_collectionWatcher.clear();
    watcher->deleteLater();

    const PDFAppDocument* document = getDocument();
    if (!document || collection.version != document->getVersion() || (m_collectionControl && m_collectionControl->isOperationCancelled()))
    {
        // A newer version is collected (onDocumentChanged starts it)
        m_isCollecting = false;
        startCollection();
        return;
    }

    m_isCollecting = false;
    m_list = collection.list;
    m_listVersion = collection.version;

    if (m_imageList)
    {
        const std::vector<PDFDocumentImageList::Key> selected = m_imageList->getSelectedKeys();
        m_imageList->getModel()->setImageList(m_list, m_listVersion);
        const PDFAppToolHost* host = getHost();
        m_imageList->setThumbnailSource(document->getSnapshot().document, host ? host->createEnvironment().cms : PDFCMSPointer());
        m_isUpdatingSelection = true;
        m_imageList->setSelectedKeys(selected);
        if (m_imageList->getSelectedKeys().empty() && m_imageList->getProxyModel()->rowCount() > 0)
        {
            const QModelIndex first = m_imageList->getProxyModel()->index(0, 0);
            const PDFDocumentImageList::Item* item = m_imageList->getModel()->getItem(m_imageList->getProxyModel()->mapToSource(first).row());
            if (item)
            {
                m_imageList->setSelectedKeys({ item->key });
            }
        }
        m_isUpdatingSelection = false;
    }

    // The status message counts the images; its slots may delete the GUI (emit last)
    updateParts();
    updateStatusText();
}

// ------------------------------------------------------------- parts

QWidget* PDFAppToolGUIOptimizeImages::createCanvasPage(QWidget* parent)
{
    m_comparison = new PDFAppToolImageComparison(parent);
    m_comparison->setObjectName(QStringLiteral("toolImageComparison"));
    m_comparison->setSyncZoomVisible(false);
    m_comparison->setShowOnPageVisible(true);
    connect(m_comparison.data(), &PDFAppToolImageComparison::showOnPageRequested, this, &PDFAppToolGUIOptimizeImages::onShowOnPage);
    m_shownKey = PDFDocumentImageList::Key();
    updateCanvas();
    return m_comparison.data();
}

int PDFAppToolGUIOptimizeImages::getRightPanelSpacing() const
{
    // sidebar-right 297:3211: gap 14 (the frames of Optimize have 16 between the sections of
    // Compress, the image tools 14)
    return 14;
}

PDFAppToolProgressPlace PDFAppToolGUIOptimizeImages::getProgressPlace() const
{
    // As Compress 295:1711: the card of the run in the section of the result (Summary)
    PDFAppToolProgressPlace place;
    place.cardLayout = m_summaryLayout;
    place.cardIndex = 1;
    return place;
}

QString PDFAppToolGUIOptimizeImages::getConfirmText() const
{
    // 308:6911 "Convert to Grayscale"; Optimize Images the title of the tool (297:3135)
    return isGrayscale() ? tr("Convert to Grayscale") : BaseClass::getConfirmText();
}

QString PDFAppToolGUIOptimizeImages::getConfirmIconName() const
{
    // btn-confirm 297:3135: the icon "image" (RS4FIGMA); Grayscale keeps the icon of its catalog entry
    return isGrayscale() ? BaseClass::getConfirmIconName() : QStringLiteral("image");
}

bool PDFAppToolGUIOptimizeImages::isConfirmEnabled() const
{
    // Nothing to change, a failed measuring has said why in its card (as Compress)
    return BaseClass::isConfirmEnabled() && m_measureState != MeasureState::NotReduced && m_measureState != MeasureState::Failed;
}

QString PDFAppToolGUIOptimizeImages::getStatusDetailText() const
{
    if (!m_list)
    {
        return QString();
    }

    const int count = int(m_list->getItems().size());
    if (isGrayscale())
    {
        // 308:6911 "286 color images of 412"
        int colorCount = 0;
        for (const PDFDocumentImageList::Item& item : m_list->getItems())
        {
            colorCount += item.isColorImage() ? 1 : 0;
        }
        return tr("%Ln color images of %1", nullptr, colorCount).arg(PDFAppLocale::formatNumber(count));
    }

    // 297:3135 "412 images · 21.3 MB"
    return tr("%1 · %2").arg(tr("%Ln images", nullptr, count), PDFDocumentToolFormat::formatSize(m_list->getTotalBytes()));
}

QString PDFAppToolGUIOptimizeImages::getRunningTitle(int percentage) const
{
    return isGrayscale() ? tr("Converting… %1 %").arg(percentage) : tr("Optimizing… %1 %").arg(percentage);
}

void PDFAppToolGUIOptimizeImages::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;
    using Tool = Helper::Tool;

    const PDFAppToolParameterModel* model = getModel();
    const bool isGray = isGrayscale();

    // tool-actions 297:3155 (Optimize Images) and 308:6911 (Grayscale: Goal, Options, Exclude)
    if (!isGray)
    {
        // Mode, Goal and Options write the override of the selection, when all of it has one
        // (owner 14. 11., 301:3594 "Mode: Custom"; updateToolbar gives them its store)
        m_modeMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_MODE), QStringLiteral("image-mode"), tr("Mode: %1"), MENU_WIDTH);

        // Color: the rows of the definition; a row writes the override of the selection, when it
        // has one (301:3594 "Toolbar menus change the override too"), the global value otherwise
        // tb-color-mode 299:3384: the icon image-color-mode for every value (1067:1564)
        m_colorPanel = builder.addMenu(QStringLiteral("color"), QStringLiteral("image-color-mode"), QString(), MENU_WIDTH);
        m_colorRows.clear();
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(Tool::PARAMETER_COLOR_MODE)) : nullptr;
        if (definition && m_colorPanel)
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                const QString iconName = option.iconName.isEmpty() ? QString::fromLatin1(Helper::getColorIcon(option.id)) : option.iconName;
                PDFTileRow* row = m_colorPanel->addRow(option.getTitle(), option.getDescription(), iconName, PDFTileRow::Kind::MenuItem);
                row->setObjectName(QStringLiteral("toolOption-") + option.id);
                row->setProperty("colorMode", option.id);
                row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
                connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIOptimizeImages::onColorRowClicked);
                m_colorRows[option.id] = row;
            }
        }
    }

    m_goalMenu = builder.addEnumMenu(Helper::name(Tool::PARAMETER_GOAL), QStringLiteral("sliders"), tr("Goal: %1"), MENU_WIDTH);
    if (m_goalMenu)
    {
        // 299:3362 / 308:6911 "Goal: Quality": the short titles of the button (as Optimize of Merge)
        m_goalMenu->setButtonTitle(Helper::name(Tool::GOAL_PREFER_QUALITY), tr("Quality"));
        m_goalMenu->setButtonTitle(Helper::name(Tool::GOAL_MINIMUM_SIZE), tr("Minimum"));
    }

    // Options 299:3362: Keep original if larger, Preserve transparency
    std::vector<PDFAppToolFlagItem> items;
    for (const char* parameter : { Tool::PARAMETER_KEEP_ORIGINAL, Tool::PARAMETER_PRESERVE_TRANSPARENCY })
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), Helper::name(parameter)) : nullptr;
        PDFAppToolFlagItem item;
        item.parameter = Helper::name(parameter);
        item.title = definition ? definition->getTitle() : QString();
        item.description = definition ? definition->getDescription() : QString();
        item.iconName = parameter == Tool::PARAMETER_KEEP_ORIGINAL ? QStringLiteral("shield-check") : QStringLiteral("layers");
        items.push_back(std::move(item));
    }
    m_optionsMenu = builder.addFlagsMenu(std::move(items), QStringLiteral("settings"), QString(), MENU_WIDTH, QString(), PDFTileRow::Kind::MenuItem);
    if (PDFAppToolFlagsMenu* options = m_optionsMenu.data())
    {
        if (QToolButton* button = options->getButton())
        {
            button->setText(tr("Options"));
        }
    }

    builder.addActionDivider();
    // tb-exclude 299:3389: icon/image-exclude (1052:1589, not the visibility)
    m_excludeButton = builder.addAction(tr("Exclude"), QStringLiteral("image-exclude"), this, &PDFAppToolGUIOptimizeImages::toggleExclude);
    if (!isGray)
    {
        // tb-override 301:3594: Open = the selection has an override
        m_overrideButton = builder.addAction(tr("Override"), QStringLiteral("edit"), this, &PDFAppToolGUIOptimizeImages::toggleOverride);
        if (m_overrideButton)
        {
            m_overrideButton->setCheckable(true);
        }
    }
    updateToolbar();
}

void PDFAppToolGUIOptimizeImages::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 297:3170: x 12, width 275 (the frame of the panels has 10 / 11)
    if (QWidget* widget = builder.getWidget())
    {
        if (QLayout* layout = widget->layout())
        {
            layout->setContentsMargins(PDFAppToolGUIOptimizeImagesHelper::LIST_MARGIN, 0, PDFAppToolGUIOptimizeImagesHelper::LIST_MARGIN, 0);
        }
    }

    m_imageList = new PDFAppToolImageList(builder.getWidget());
    m_imageList->setObjectName(QStringLiteral("toolImageList"));
    m_imageList->setRowStyle(PDFAppToolImageListModel::RowStyle::Optimize);
    m_imageList->setSortVisible(true);
    m_imageList->setTipText(isGrayscale() ? tr("Only color images are converted; images that are already grayscale or bitonal are skipped. Exclude keeps an image in color.")
                                          : tr("Ctrl or Shift selects more images, Ctrl+A selects all. Global defaults apply to every image without an override."));
    connect(m_imageList.data(), &PDFAppToolImageList::selectionChanged, this, &PDFAppToolGUIOptimizeImages::onImageSelectionChanged);
    connect(m_imageList.data(), &PDFAppToolImageList::markToggled, this, &PDFAppToolGUIOptimizeImages::onImageMarksToggled);
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
    startCollection();
}

void PDFAppToolGUIOptimizeImages::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    const bool isGray = isGrayscale();

    // Nothing is measured for a document, which refuses the tool (339:10328 is shown)
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->isPermissionRefused() && m_measureState == MeasureState::Measuring)
    {
        m_measureState = MeasureState::Failed;
        m_errorText = getPermissionTitle();
    }

    // section-Settings editor 298:3332 / Grayscale images 308:6911
    m_profileEditor = new PDFAppToolImageProfileEditor(getModel(), isGray ? PDFAppToolImageProfileEditor::Mode::SingleProfile : PDFAppToolImageProfileEditor::Mode::ThreeProfiles, builder.getWidget());
    builder.addWidget(m_profileEditor);

    // section-Selected image 298:3406: the check boxes and the rows of the image
    m_selectedCard = new PDFAppToolSelectedItemCard(tr("Selected image"), builder.getWidget());
    m_selectedCard->setObjectName(QStringLiteral("toolSelectedImage"));
    if (PDFAppToolCard* card = qobject_cast<PDFAppToolCard*>(m_selectedCard->getValues()->parentWidget()))
    {
        card->getBodyLayout()->setSpacing(6);
        m_enableCheckBox = new QCheckBox(card);
        m_enableCheckBox->setObjectName(QStringLiteral("toolImageEnable"));
        // clicked, not toggled: the commit may delete the tab, and QCheckBox::nextCheckState reads
        // the check box after toggled without a guard; clicked comes after it under the guard of
        // QAbstractButton (a lifetime finding of the row grayscale-tab)
        connect(m_enableCheckBox.data(), &QCheckBox::clicked, this, &PDFAppToolGUIOptimizeImages::onEnableToggled);
        card->getBodyLayout()->insertWidget(0, m_enableCheckBox);
        if (!isGray)
        {
            m_overrideCheckBox = new QCheckBox(card);
            m_overrideCheckBox->setObjectName(QStringLiteral("toolImageOverride"));
            connect(m_overrideCheckBox.data(), &QCheckBox::clicked, this, &PDFAppToolGUIOptimizeImages::onOverrideToggled);
            card->getBodyLayout()->insertWidget(1, m_overrideCheckBox);
        }
    }
    builder.addWidget(m_selectedCard);

    // section-Summary 298:3432, the card of the run below its header (as Compress 295:1711)
    builder.addSection(tr("Summary"));
    m_summaryLayout = builder.getSectionLayout();
    m_summaryValues = builder.addKeyValues();
    m_summaryValues->setObjectName(QStringLiteral("toolImageSummary"));
    m_summaryValues->setRowGap(0);
    m_measuringCard = new PDFAppToolProgressCard(builder.getWidget());
    m_measuringCard->setObjectName(QStringLiteral("toolMeasuringCard"));
    builder.addWidget(m_measuringCard);
    builder.endSection();

    // No image gets smaller (plan 2.7, as the callout of 295:2182)
    m_notReducedCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString());
    m_notReducedCallout->setObjectName(QStringLiteral("toolNotReducedCallout"));
    m_notReducedCallout->getTitleLabel()->setWordWrap(true);

    if (isGray)
    {
        // callout-blue 308:6911
        m_infoCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Only images are converted"),
                                           tr("Colored text and vector graphics stay in color. View › Page colors › Grayscale changes only the display, not the file."));
        m_infoCallout->setObjectName(QStringLiteral("toolGrayscaleCallout"));
    }

    Q_UNUSED(Helper::LIST_MARGIN);
    updateParts();
}

// ------------------------------------------------------------- updates

void PDFAppToolGUIOptimizeImages::updateParts()
{
    updateRows();
    updateHead();
    updateToolbar();
    updateProfileEditor();
    updateSelectedCard();
    updateSummary();
    updateCanvas();
}

void PDFAppToolGUIOptimizeImages::updateRows()
{
    using Model = PDFAppToolImageListModel;
    using Status = PDFDocumentToolOptimizeImages;

    if (!m_imageList || !m_list)
    {
        return;
    }

    const std::vector<PDFDocumentImageList::Key> excluded = getExcludedKeys();
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    const bool isGray = isGrayscale();
    const bool isMeasured = m_result.has_value() && (m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced);

    std::vector<Model::RowState> states;
    states.reserve(m_list->getItems().size());
    for (const PDFDocumentImageList::Item& item : m_list->getItems())
    {
        Model::RowState state;
        const qint64 bytes = item.streamBytes + item.maskBytes;
        const QString size = PDFDocumentToolFormat::formatSize(bytes);
        state.sizeText = size;

        if (isGray && !item.isColorImage())
        {
            // 308:6911: "already gray", "bitonal", the row as skipped
            state.tag = item.color == PDFDocumentImageList::ImageColor::Bitonal ? Model::Tag::Bitonal : Model::Tag::AlreadyGray;
            state.isDimmed = true;
            state.isMarked = false;
            state.sizeText = tr("%1 · skipped").arg(size);
            states.push_back(std::move(state));
            continue;
        }
        if (item.isKeptByChangingTools())
        {
            // R4-8: "inline · kept"; R4-4: an image of an annotation appearance is kept too, said
            // by its own tag
            state.tag = item.kind == PDFDocumentImageList::Kind::Inline ? Model::Tag::InlineKept : Model::Tag::AppearanceKept;
            state.isMarked = false;
            states.push_back(std::move(state));
            continue;
        }
        if (PDFAppToolGUIOptimizeImagesHelper::contains(excluded, item.key))
        {
            state.tag = Model::Tag::Excluded;
            state.isMarked = false;
            states.push_back(std::move(state));
            continue;
        }

        if (PDFAppToolGUIOptimizeImagesHelper::contains(overrides, item.key))
        {
            state.tag = Model::Tag::Override;
        }

        const ImageResult* result = isMeasured ? getImageResult(item.key) : nullptr;
        if (result)
        {
            if (result->status == QLatin1String(Status::IMAGE_STATUS_RECOMPRESSED))
            {
                state.sizeText = PDFDocumentToolFormat::formatSizeChange(result->originalBytes, result->newBytes);
            }
            else if (result->status == QLatin1String(Status::IMAGE_STATUS_KEPT) && state.tag == Model::Tag::None)
            {
                state.tag = Model::Tag::KeepOriginal;
            }
            else if (result->status == QLatin1String(Status::IMAGE_STATUS_TOO_LARGE))
            {
                state.tag = Model::Tag::TooLarge;
            }
            else if (result->status == QLatin1String(Status::IMAGE_STATUS_CANNOT_BE_READ))
            {
                state.tag = Model::Tag::CannotBeRead;
            }
        }
        states.push_back(std::move(state));
    }
    m_imageList->getModel()->setRowStates(std::move(states));
}

void PDFAppToolGUIOptimizeImages::updateHead()
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

    const int count = int(m_list->getItems().size());
    const int excludedCount = int(getExcludedKeys().size());
    std::vector<PDFAppToolImageList::Chip> chips;
    chips.push_back({ QStringLiteral("all"), tr("All"), count });
    if (isGrayscale())
    {
        // 308:6950 "412 images · 286 in color", chips All · Color · Excluded
        int colorCount = 0;
        for (const PDFDocumentImageList::Item& item : m_list->getItems())
        {
            colorCount += item.isColorImage() ? 1 : 0;
        }
        m_imageList->setHeadText(tr("%1 · %2").arg(tr("%Ln images", nullptr, count), tr("%Ln in color", nullptr, colorCount)));
        chips.push_back({ QStringLiteral("color"), tr("Color"), colorCount });
    }
    else
    {
        // 297:3623 "412 images · 21.3 MB", chips All · Overrides · Excluded
        m_imageList->setHeadText(tr("%1 · %2").arg(tr("%Ln images", nullptr, count), PDFDocumentToolFormat::formatSize(m_list->getTotalBytes())));
        chips.push_back({ QStringLiteral("overrides"), tr("Overrides"), int(getOverrideKeys().size()) });
    }
    chips.push_back({ QStringLiteral("excluded"), tr("Excluded"), excludedCount });

    const QString current = m_imageList->getCurrentChipId().isEmpty() ? QStringLiteral("all") : m_imageList->getCurrentChipId();
    m_imageList->setChips(chips, current);
    m_imageList->setEmptyText(count == 0 ? tr("The document has no images.") : tr("No image is shown by this filter."));
}

void PDFAppToolGUIOptimizeImages::updateToolbar()
{
    const std::vector<PDFDocumentImageList::Key> keys = getChangeableSelection();
    const bool hasSelection = !keys.empty();
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    bool isAllOverridden = hasSelection;
    for (const PDFDocumentImageList::Key& key : keys)
    {
        isAllOverridden = isAllOverridden && PDFAppToolGUIOptimizeImagesHelper::contains(overrides, key);
    }

    if (m_excludeButton)
    {
        m_excludeButton->setEnabled(hasSelection);
    }
    if (m_overrideButton)
    {
        const QSignalBlocker blocker(m_overrideButton.data());
        m_overrideButton->setEnabled(hasSelection);
        m_overrideButton->setChecked(isAllOverridden);
    }

    // Mode, Goal and Options: the override of the selection, when all of it has one (owner
    // 14. 11.; 301:3594 "Mode: Custom", "Toolbar menus change the override too"), the global
    // values otherwise; Grayscale has no overrides
    const bool isOverrideEdited = isAllOverridden && !isGrayscale();
    const std::shared_ptr<PDFAppValueStore> store = isOverrideEdited ? std::make_shared<PDFAppToolImageOverrideStore>(getModel(), keys) : nullptr;
    if (m_modeMenu)
    {
        m_modeMenu->setValueStore(store);
    }
    if (m_goalMenu)
    {
        m_goalMenu->setValueStore(store);
    }
    if (m_optionsMenu)
    {
        m_optionsMenu->setValueStore(store);
    }

    // Color: the title and the check of the shown color mode
    if (m_colorPanel)
    {
        const QString colorMode = getShownColorMode();
        QString title;
        for (const auto& item : m_colorRows)
        {
            if (PDFTileRow* row = item.second.data())
            {
                const QSignalBlocker blocker(row);
                row->setChecked(item.first == colorMode);
                if (item.first == colorMode)
                {
                    title = row->getTitle();
                }
            }
        }
        if (QToolButton* button = m_colorPanel->getButton())
        {
            button->setText(tr("Color: %1").arg(title));
        }
    }
}

void PDFAppToolGUIOptimizeImages::updateProfileEditor()
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    if (!m_profileEditor || isGrayscale())
    {
        return;
    }

    // 301:3594: the editor edits the override of the selection, when all of it has one
    const std::vector<PDFDocumentImageList::Key> keys = getChangeableSelection();
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    bool isAllOverridden = !keys.empty();
    for (const PDFDocumentImageList::Key& key : keys)
    {
        isAllOverridden = isAllOverridden && PDFAppToolGUIOptimizeImagesHelper::contains(overrides, key);
    }
    m_profileEditor->setOverrideTarget(isAllOverridden ? keys : std::vector<PDFDocumentImageList::Key>());
    if (isAllOverridden)
    {
        const QString profile = Helper::getProfileOfColorMode(getShownColorMode());
        if (!profile.isEmpty())
        {
            m_profileEditor->setProfile(profile);
        }
    }
}

void PDFAppToolGUIOptimizeImages::updateSelectedCard()
{
    if (!m_selectedCard)
    {
        return;
    }

    const bool isGray = isGrayscale();
    const std::vector<PDFDocumentImageList::Key> selected = m_imageList ? m_imageList->getSelectedKeys() : std::vector<PDFDocumentImageList::Key>();
    const std::vector<PDFDocumentImageList::Key> changeable = getChangeableSelection();
    const int total = m_list ? int(m_list->getItems().size()) : 0;
    const int count = int(selected.size());

    // 298:3409 "1 of 412", 301:3594 "Selected images" "3 of 412"
    m_selectedCard->getTitleLabel()->setText(count > 1 ? tr("Selected images") : tr("Selected image"));
    m_selectedCard->setTagText(count > 0 ? tr("%1 of %2").arg(PDFAppLocale::formatNumber(count), PDFAppLocale::formatNumber(total)) : QString());

    int enabledCount = 0;
    int overrideCount = 0;
    const std::vector<PDFDocumentImageList::Key> excluded = getExcludedKeys();
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    for (const PDFDocumentImageList::Key& key : changeable)
    {
        enabledCount += PDFAppToolGUIOptimizeImagesHelper::contains(excluded, key) ? 0 : 1;
        overrideCount += PDFAppToolGUIOptimizeImagesHelper::contains(overrides, key) ? 1 : 0;
    }

    if (m_enableCheckBox)
    {
        const QSignalBlocker blocker(m_enableCheckBox.data());
        m_enableCheckBox->setText(isGray ? tr("Convert this image") : tr("Enable compression"));
        m_enableCheckBox->setEnabled(!changeable.empty());
        m_enableCheckBox->setTristate(false);
        if (changeable.empty())
        {
            m_enableCheckBox->setChecked(false);
        }
        else if (enabledCount == int(changeable.size()))
        {
            m_enableCheckBox->setChecked(true);
        }
        else if (enabledCount == 0)
        {
            m_enableCheckBox->setChecked(false);
        }
        else
        {
            m_enableCheckBox->setTristate(true);
            m_enableCheckBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    if (m_overrideCheckBox)
    {
        const QSignalBlocker blocker(m_overrideCheckBox.data());
        m_overrideCheckBox->setText(count > 1 ? tr("Override settings") : tr("Override settings for this image"));
        m_overrideCheckBox->setEnabled(!changeable.empty());
        m_overrideCheckBox->setTristate(false);
        if (overrideCount > 0 && overrideCount < int(changeable.size()))
        {
            m_overrideCheckBox->setTristate(true);
            m_overrideCheckBox->setCheckState(Qt::PartiallyChecked);
        }
        else
        {
            m_overrideCheckBox->setChecked(!changeable.empty() && overrideCount == int(changeable.size()));
        }
    }

    PDFKeyValueList* values = m_selectedCard->getValues();
    values->clear();
    const PDFDocumentImageList::Item* item = count == 1 && m_list ? m_list->findItem(selected.front()) : nullptr;
    if (item)
    {
        const QString yes = tr("Yes");
        const QString no = tr("No");
        const bool hasTransparency = item->softMaskReference.isValid() || item->maskReference.isValid();
        if (!isGray)
        {
            // 298:3420 Reference "148 0", Bits per component
            values->addRow(tr("Reference"), item->key.toString());
            values->addRow(tr("Bits per component"), item->bitsPerComponent > 0 ? PDFAppLocale::formatNumber(item->bitsPerComponent) : tr("not declared"));
        }
        values->addRow(tr("Transparency"), hasTransparency ? yes : no);
        values->addRow(tr("Soft mask"), item->softMaskReference.isValid() ? yes : no);
    }
    else if (count > 1)
    {
        // 301:3594: Selected images, Enabled, Original size
        qint64 bytes = 0;
        for (const PDFDocumentImageList::Key& key : selected)
        {
            if (const PDFDocumentImageList::Item* selectedItem = m_list ? m_list->findItem(key) : nullptr)
            {
                bytes += selectedItem->streamBytes + selectedItem->maskBytes;
            }
        }
        values->addRow(tr("Selected images"), PDFAppLocale::formatNumber(count));
        values->addRow(isGray ? tr("Converted") : tr("Enabled"), PDFAppLocale::formatNumber(enabledCount));
        values->addRow(tr("Original size"), PDFDocumentToolFormat::formatSize(bytes));
    }
    m_selectedCard->getValues()->setVisible(values->getRowCount() > 0);
}

void PDFAppToolGUIOptimizeImages::updateSummary()
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    const bool isMeasured = m_result.has_value() && (m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced);
    if (m_summaryValues)
    {
        const QString dash = QStringLiteral("–");
        std::vector<std::pair<QString, QString>> rows;
        if (isGrayscale())
        {
            // 308:6911: Color images "286 · 18.9 MB", Estimated size, Skipped "126 gray or bitonal"
            int colorCount = 0;
            qint64 colorBytes = 0;
            int skippedCount = 0;
            if (m_list)
            {
                for (const PDFDocumentImageList::Item& item : m_list->getItems())
                {
                    if (item.isColorImage())
                    {
                        ++colorCount;
                        colorBytes += item.streamBytes + item.maskBytes;
                    }
                    else
                    {
                        ++skippedCount;
                    }
                }
            }
            rows.emplace_back(tr("Color images"), m_list ? tr("%1 · %2").arg(PDFAppLocale::formatNumber(colorCount), PDFDocumentToolFormat::formatSize(colorBytes)) : dash);
            rows.emplace_back(tr("Estimated size"), isMeasured ? PDFDocumentToolFormat::formatSize(m_result->data.value(QStringLiteral("imagesAfter")).toInteger()) : dash);
            rows.emplace_back(tr("Skipped"), m_list ? tr("%Ln gray or bitonal", nullptr, skippedCount) : dash);
        }
        else
        {
            // 298:3435: Original images size, Estimated size, Compression ratio
            const qint64 before = isMeasured ? m_result->data.value(QStringLiteral("imagesBefore")).toInteger() : (m_list ? m_list->getTotalBytes() : -1);
            const qint64 after = isMeasured ? m_result->data.value(QStringLiteral("imagesAfter")).toInteger() : -1;
            rows.emplace_back(tr("Original images size"), before >= 0 ? PDFDocumentToolFormat::formatSize(before) : dash);
            rows.emplace_back(tr("Estimated size"), after >= 0 ? PDFDocumentToolFormat::formatSize(after) : dash);
            rows.emplace_back(tr("Compression ratio"), after >= 0 ? Helper::formatRatio(before, after) : dash);
        }

        if (m_summaryValues->getRowCount() != int(rows.size()))
        {
            m_summaryValues->clear();
            for (const auto& row : rows)
            {
                m_summaryValues->addRow(row.first, row.second);
            }
        }
        else
        {
            for (int i = 0; i < int(rows.size()); ++i)
            {
                m_summaryValues->setValue(i, rows[size_t(i)].second);
            }
        }
    }

    if (m_measuringCard)
    {
        // As Compress 295:1711: the card of the measuring while it runs, the reason of a failure
        const bool isCardShown = !m_isRunning && !isMeasured;
        m_measuringCard->setVisible(isCardShown);
        switch (m_measureState)
        {
            case MeasureState::Measuring:
                m_measuringCard->setProgress(m_percentage, tr("Measuring… %1 %").arg(m_percentage), m_stepText, QString());
                m_measuringCard->getBar()->setVisible(true);
                break;
            case MeasureState::Stopped:
                m_measuringCard->setProgress(0, tr("Not measured"), isGrayscale() ? tr("Press Convert to Grayscale or change a setting to measure again.")
                                                                                  : tr("Press Optimize Images or change a setting to measure again."), QString());
                m_measuringCard->getBar()->setVisible(false);
                break;
            case MeasureState::Failed:
                m_measuringCard->setProgress(0, tr("Cannot be measured"), m_errorText, QString());
                m_measuringCard->getBar()->setVisible(false);
                break;
            default:
                break;
        }
    }

    if (m_notReducedCallout)
    {
        m_notReducedCallout->setVisible(!m_isRunning && m_measureState == MeasureState::NotReduced);
        m_notReducedCallout->setTexts(m_result ? m_result->message : QString(),
                                      isGrayscale() ? tr("Include an image or choose another document.") : tr("Choose a smaller resolution, another algorithm or Minimum size."));
    }
}

void PDFAppToolGUIOptimizeImages::updateCanvas()
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;
    using Status = PDFDocumentToolOptimizeImages;

    if (!m_comparison)
    {
        return;
    }

    const PDFDocumentImageList::Item* item = getCurrentItem();
    const int count = m_imageList ? int(m_imageList->getSelectedKeys().size()) : 0;
    if (!item)
    {
        m_comparison->setTitle(m_list && m_list->getItems().empty() ? tr("The document has no images") : QString());
        m_comparison->setCaptions(tr("BEFORE"), QString(), tr("AFTER"), QString());
        m_comparison->setPlaceholderTexts(m_list ? tr("Select an image in the list.") : tr("Looking for images…"), m_list ? tr("Select an image in the list.") : tr("Looking for images…"));
        m_comparison->setFooterTexts(QString(), QString());
        m_decoder->cancelAll();
        m_beforeTicket = 0;
        m_afterTicket = 0;
        m_shownKey = PDFDocumentImageList::Key();
        m_comparison->setImages(QImage(), QImage());
        return;
    }

    // preview-bar 298:3275 "Image 1 · page 308", 301:3594 "(first of 3 selected)"
    const int number = m_list->indexOf(item->key) + 1;
    const QString page = item->pageIndices.empty() ? QString() : PDFAppLocale::formatPageNumbers({ item->pageIndices.front() });
    QString title = page.isEmpty() ? tr("Image %1").arg(PDFAppLocale::formatNumber(number)) : tr("Image %1 · page %2").arg(PDFAppLocale::formatNumber(number), page);
    if (count > 1)
    {
        title = tr("%1 (first of %2 selected)").arg(title, PDFAppLocale::formatNumber(count));
    }
    m_comparison->setTitle(title);

    // info 298:3316 "2480 × 3508 px · DeviceRGB · DCTDecode · 1.2 MB"
    const qint64 bytes = item->streamBytes + item->maskBytes;
    QStringList beforeParts;
    beforeParts << Helper::formatPixels(item->pixelSize);
    beforeParts << (item->isImageMask ? tr("Image mask") : (item->colorSpaceName.isEmpty() ? tr("No color space") : item->colorSpaceName));
    beforeParts << (item->filterName.isEmpty() ? tr("not compressed") : item->filterName);
    beforeParts << PDFDocumentToolFormat::formatSize(bytes);
    const QString beforeInfo = beforeParts.join(QStringLiteral(" · "));

    const bool isMeasured = m_result.has_value() && (m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced);
    const ImageResult* result = isMeasured ? getImageResult(item->key) : nullptr;
    const bool isRecompressed = result && result->status == QLatin1String(Status::IMAGE_STATUS_RECOMPRESSED);

    QString afterInfo;
    QString footer;
    QString footerDetail;
    QString afterPlaceholder;
    if (isGrayscale() && !item->isColorImage())
    {
        afterInfo = tr("Skipped · the image is already gray or bitonal");
        footer = tr("Estimated size: %1").arg(PDFDocumentToolFormat::formatSize(bytes));
        footerDetail = tr("the image is not converted");
    }
    else if (item->isKeptByChangingTools() && item->kind == PDFDocumentImageList::Kind::Inline)
    {
        afterInfo = tr("Kept · an inline image is never changed");
        footer = tr("Estimated size: %1").arg(PDFDocumentToolFormat::formatSize(bytes));
        footerDetail = tr("inline images are a part of the content of the page");
    }
    else if (item->isKeptByChangingTools())
    {
        // R4-4: an annotation appearance draws the image too (/AP); a new encoding would change it
        afterInfo = tr("Kept · an annotation appearance uses this image");
        footer = tr("Estimated size: %1").arg(PDFDocumentToolFormat::formatSize(bytes));
        footerDetail = tr("images of annotation appearances are never changed");
    }
    else if (isExcluded(item->key))
    {
        afterInfo = tr("Excluded · the image keeps its stream");
        footer = tr("Estimated size: %1").arg(PDFDocumentToolFormat::formatSize(bytes));
        footerDetail = tr("the image is excluded");
    }
    else if (!isMeasured)
    {
        afterPlaceholder = m_measureState == MeasureState::Failed ? m_errorText : tr("Measuring…");
        footer = tr("Estimated size: measuring…");
    }
    else if (!isRecompressed)
    {
        afterInfo = result && (result->status == QLatin1String(Status::IMAGE_STATUS_CANNOT_BE_READ) || result->status == QLatin1String(Status::IMAGE_STATUS_TOO_LARGE))
                        ? tr("Kept · the image cannot be read") : tr("Kept · a new encoding would not be smaller");
        footer = tr("Estimated size: %1").arg(PDFDocumentToolFormat::formatSize(bytes));
        footerDetail = tr("the image keeps its stream");
    }
    else
    {
        // The encoding of the measured document (the real result, the same reference)
        const PDFDocumentToolOutputDocument* output = PDFDocumentToolRunner::findChangedDocument(*m_result);
        const PDFDocument* document = output ? output->document.data() : nullptr;
        const PDFObject& object = document ? document->getObjectByReference(item->key.reference) : PDFObject();
        const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : nullptr;
        QSize afterSize = item->pixelSize;
        if (dictionary)
        {
            // The quality of a JPEG is the number of the profile of the written image (the core
            // writes it exactly, exactJpegQuality): the gray profile of a gray image
            const QVariantMap parameters = getRunParameters();
            const PDFImageOptimizer::Settings settings = isGrayscale() ? Helper::Gray::createGrayscaleSettings(parameters) : Status::createSettings(parameters);
            QString error;
            Status::ImageException exception;
            exception.key = item->key;
            for (const Status::ImageException& candidate : Status::parseImageExceptions(parameters.value(Helper::name(Status::PARAMETER_IMAGES)).toString(), &error))
            {
                if (candidate.key == item->key)
                {
                    exception = candidate;
                }
            }
            const PDFImageOptimizer::Settings imageSettings = Status::createImageSettings(settings, exception);
            const PDFObject& colorSpace = document->getObject(dictionary->get("ColorSpace"));
            const bool isGray = colorSpace.isName() && colorSpace.getString() == "DeviceGray";
            const int jpegQuality = isGray ? imageSettings.grayProfile.jpegQuality : imageSettings.colorProfile.jpegQuality;
            const QSize size = Helper::getPixelSize(document, dictionary);
            if (!size.isEmpty())
            {
                afterSize = size;
            }
            afterInfo = tr("%1 · %2 · %3").arg(Helper::formatPixels(afterSize), Helper::describeEncoding(document, dictionary, jpegQuality), PDFDocumentToolFormat::formatSize(result->newBytes));
        }

        // estimate 298:3330 "Estimated size: 1.2 MB → 310 KB", "74 % smaller · downsampled from 300 to 200 dpi"
        footer = tr("Estimated size: %1").arg(PDFDocumentToolFormat::formatSizeChange(result->originalBytes, result->newBytes));
        QStringList detail;
        const int saved = PDFDocumentToolFormat::getSavedPercent(result->originalBytes, result->newBytes);
        if (result->newBytes < result->originalBytes)
        {
            detail << tr("%1 % smaller").arg(saved);
        }
        const int dpi = Helper::getDpi(*item);
        if (afterSize.width() < item->pixelSize.width() && item->pixelSize.width() > 0)
        {
            const int afterDpi = qRound(double(dpi) * double(afterSize.width()) / double(item->pixelSize.width()));
            detail << (dpi > 0 ? tr("downsampled from %1 to %2 dpi").arg(PDFAppLocale::formatNumber(dpi), PDFAppLocale::formatNumber(afterDpi)) : tr("downsampled"));
        }
        else
        {
            detail << tr("resolution kept");
        }
        footerDetail = detail.join(QStringLiteral(" · "));
    }

    // Owner 2026-11-14: "AFTER" only - the image is the real encoding of the measured document,
    // not a simulation (Figma 297:3135 "AFTER · SIMULATED" is a recorded deviation)
    m_comparison->setCaptions(tr("BEFORE"), beforeInfo, tr("AFTER"), afterInfo);
    m_comparison->setFooterTexts(footer, footerDetail);
    m_comparison->setPlaceholderTexts(QString(), afterPlaceholder);
    updateCanvasImages();
}

void PDFAppToolGUIOptimizeImages::updateCanvasImages()
{
    using Status = PDFDocumentToolOptimizeImages;

    const PDFDocumentImageList::Item* item = getCurrentItem();
    const PDFAppDocument* appDocument = getDocument();
    if (!m_comparison || !item || !appDocument)
    {
        return;
    }

    const bool isMeasured = m_result.has_value() && (m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced);
    const ImageResult* result = isMeasured ? getImageResult(item->key) : nullptr;
    const bool isRecompressed = result && result->status == QLatin1String(Status::IMAGE_STATUS_RECOMPRESSED);
    const PDFDocumentToolOutputDocument* output = isRecompressed ? PDFDocumentToolRunner::findChangedDocument(*m_result) : nullptr;
    const PDFDocumentPointer afterDocument = output ? output->document : PDFDocumentPointer();
    const bool isAfterOriginal = !isRecompressed && isMeasured;
    const bool isAfterKnown = isRecompressed || isAfterOriginal || item->isKeptByChangingTools() || isExcluded(item->key) || (isGrayscale() && !item->isColorImage());

    const PDFAppToolHost* host = getHost();
    const PDFCMSPointer cms = host ? host->createEnvironment().cms : PDFCMSPointer();
    const PDFDocumentPointer document = appDocument->getSnapshot().document;

    // BEFORE: a new image is decoded once; AFTER follows the measured document
    const bool isNewImage = m_shownKey != item->key;
    if (isNewImage)
    {
        m_decoder->cancelAll();
        m_beforeTicket = 0;
        m_afterTicket = 0;
        m_shownKey = item->key;
        m_shownAfterDocument.reset();
        m_comparison->setImages(QImage(), QImage());

        PDFAppToolImageDecoder::Request request;
        request.item = *item;
        m_beforeTicket = m_decoder->decode(document, cms, { request }, 0);
    }

    if (afterDocument != m_shownAfterDocument || isNewImage || !isAfterKnown)
    {
        m_shownAfterDocument = afterDocument;
        m_afterTicket = 0;
        if (afterDocument)
        {
            PDFAppToolImageDecoder::Request request;
            request.item = *item;
            m_afterTicket = m_decoder->decode(afterDocument, cms, { request }, 0);
            m_comparison->setImages(m_comparison->getBeforeImage(), QImage());
        }
        else
        {
            // The image keeps its stream: AFTER is BEFORE; not measured yet: nothing
            m_comparison->setImages(m_comparison->getBeforeImage(), isAfterKnown ? m_comparison->getBeforeImage() : QImage());
        }
    }
}

void PDFAppToolGUIOptimizeImages::onImagesDecoded(quint64 ticket, const QList<QImage>& images)
{
    if (!m_comparison || images.isEmpty())
    {
        return;
    }

    if (ticket == m_beforeTicket)
    {
        m_beforeTicket = 0;
        QImage after = m_comparison->getAfterImage();
        if (!m_shownAfterDocument && after.isNull() && m_afterTicket == 0)
        {
            // The image keeps its stream (or it is not measured: the placeholder stays)
            const PDFDocumentImageList::Item* item = getCurrentItem();
            const bool isMeasured = m_result.has_value() && (m_measureState == MeasureState::Measured || m_measureState == MeasureState::NotReduced);
            const bool isKept = item && (isMeasured || item->isKeptByChangingTools() || isExcluded(item->key) || (isGrayscale() && !item->isColorImage()));
            after = isKept ? images.front() : QImage();
        }
        m_comparison->setImages(images.front(), after);
        if (images.front().isNull())
        {
            m_comparison->setPlaceholderTexts(tr("The image cannot be read."), QString());
        }
    }
    else if (ticket == m_afterTicket)
    {
        m_afterTicket = 0;
        m_comparison->setImages(m_comparison->getBeforeImage(), images.front());
    }
}

void PDFAppToolGUIOptimizeImages::updateSelection()
{
    updateToolbar();
    updateProfileEditor();
    updateSelectedCard();
    updateCanvas();
}

// ------------------------------------------------------------- reactions

void PDFAppToolGUIOptimizeImages::onImageSelectionChanged()
{
    if (m_isUpdatingSelection)
    {
        return;
    }
    updateSelection();
}

void PDFAppToolGUIOptimizeImages::onColorRowClicked()
{
    using Helper = PDFAppToolGUIOptimizeImagesHelper;

    const QObject* row = sender();
    PDFAppToolParameterModel* model = getModel();
    if (!row || !model)
    {
        return;
    }

    const QString colorMode = row->property("colorMode").toString();
    const std::vector<PDFDocumentImageList::Key> keys = getChangeableSelection();
    const std::vector<PDFDocumentImageList::Key> overrides = getOverrideKeys();
    bool isAllOverridden = !keys.empty();
    for (const PDFDocumentImageList::Key& key : keys)
    {
        isAllOverridden = isAllOverridden && PDFAppToolGUIOptimizeImagesHelper::contains(overrides, key);
    }

    // The panel closes first: a change of a parameter may start anything
    QPointer<PDFAppToolGUIOptimizeImages> guard(this);
    if (m_colorPanel)
    {
        m_colorPanel->close();
    }
    if (!guard || !getModel())
    {
        return;
    }

    // 301:3594: "Toolbar menus change the override too"
    if (isAllOverridden)
    {
        PDFAppToolImageOverrideStore::writeSetting(getModel(), keys, Helper::name(Helper::Tool::EXCEPTION_COLOR_MODE), colorMode);
    }
    else
    {
        getModel()->setValue(Helper::name(Helper::Tool::PARAMETER_COLOR_MODE), colorMode);
    }
    if (guard)
    {
        // The row has checked itself: the value of the model decides
        updateToolbar();
    }
}

void PDFAppToolGUIOptimizeImages::onEnableToggled(bool isChecked)
{
    QPointer<PDFAppToolGUIOptimizeImages> guard(this);
    setExcluded(getChangeableSelection(), !isChecked);
    if (guard)
    {
        updateSelectedCard();
    }
}

void PDFAppToolGUIOptimizeImages::onOverrideToggled(bool isChecked)
{
    QPointer<PDFAppToolGUIOptimizeImages> guard(this);
    setOverride(getChangeableSelection(), isChecked);
    if (guard)
    {
        updateSelectedCard();
    }
}

void PDFAppToolGUIOptimizeImages::onShowOnPage()
{
    // tb-show-on-page 298:3299: the page of the image in the thumbnails of the document; the
    // canvas stays the comparison (the document is shown when the tool ends)
    const PDFDocumentImageList::Item* item = getCurrentItem();
    PDFAppToolHost* host = getHost();
    if (!item || item->pageIndices.empty() || !host)
    {
        return;
    }
    // The panel runs foreign code: the page is taken before, the host again after it
    const PDFInteger pageIndex = item->pageIndices.front();
    QPointer<PDFAppToolGUIOptimizeImages> guard(this);
    host->showPanel(QStringLiteral("thumbnails"));
    host = guard ? getHost() : nullptr;
    if (PDFAppDocumentView* view = host ? host->getView() : nullptr)
    {
        view->goToPage(pageIndex);
    }
}

// ------------------------------------------------------------- the flow of the shell

void PDFAppToolGUIOptimizeImages::setMeasureState(MeasureState state)
{
    m_measureState = state;
    updateParts();
    updateConfirmState();
}

void PDFAppToolGUIOptimizeImages::readMeasuredImages()
{
    m_imageResults.clear();
    if (!m_result)
    {
        return;
    }

    // The data "images" of the core: an object per image of the list in its order
    const QJsonArray images = m_result->data.value(QStringLiteral("images")).toArray();
    for (const QJsonValue& value : images)
    {
        const QJsonObject image = value.toObject();
        ImageResult result;
        result.status = image.value(QStringLiteral("status")).toString();
        result.originalBytes = image.value(QStringLiteral("originalBytes")).toInteger();
        result.newBytes = image.value(QStringLiteral("newBytes")).toInteger();
        m_imageResults[image.value(QStringLiteral("id")).toString()] = result;
    }
}

void PDFAppToolGUIOptimizeImages::onParametersChanged(const QString& name)
{
    Q_UNUSED(name);

    // The shell asks for a new preview after this hook: a cached one comes at once
    m_result.reset();
    m_imageResults.clear();
    m_percentage = 0;
    m_stepText.clear();
    setMeasureState(MeasureState::Measuring);
}

void PDFAppToolGUIOptimizeImages::onPreviewChanged(const PDFDocumentToolResult* preview)
{
    if (!preview)
    {
        // A new version of the document: the runner measures it again; the slots of the state
        // of the Primary button may delete this GUI
        m_result.reset();
        m_imageResults.clear();
        m_percentage = 0;
        m_stepText.clear();
        QPointer<PDFAppToolGUIOptimizeImages> guard(this);
        setMeasureState(MeasureState::Measuring);
        if (guard)
        {
            updateStatusText();
        }
        return;
    }

    switch (preview->status)
    {
        case PDFDocumentToolResultStatus::Success:
            m_result = *preview;
            readMeasuredImages();
            setMeasureState(MeasureState::Measured);
            break;
        case PDFDocumentToolResultStatus::NothingToDo:
            m_result = *preview;
            readMeasuredImages();
            setMeasureState(MeasureState::NotReduced);
            break;
        case PDFDocumentToolResultStatus::Failed:
            m_result.reset();
            m_imageResults.clear();
            m_errorText = preview->message;
            setMeasureState(MeasureState::Failed);
            break;
        default:
            break;
    }
}

void PDFAppToolGUIOptimizeImages::onPreviewProgress(int percentage, const QString& stepText)
{
    m_percentage = percentage;
    m_stepText = stepText;
    if (m_measureState == MeasureState::Measuring && m_measuringCard)
    {
        m_measuringCard->setProgress(percentage, tr("Measuring… %1 %").arg(percentage), stepText, QString());
    }
}

void PDFAppToolGUIOptimizeImages::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    if (isRunning && m_colorPanel && m_colorPanel->isVisible())
    {
        m_colorPanel->hide();
    }

    // The run has ended without a result here: Stop
    if (!isRunning && m_measureState == MeasureState::Measuring && !m_result)
    {
        m_measureState = MeasureState::Stopped;
    }
    updateSummary();
    updateConfirmState();
}

void PDFAppToolGUIOptimizeImages::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    // Nothing to change and an error are states of the panel, no info bar over the canvas
    onPreviewChanged(&result);
}

void PDFAppToolGUIOptimizeImages::onDocumentChanged()
{
    // A new version: its list is collected again (the rows keep the old one until then)
    startCollection();

    const PDFDocumentToolShell* shell = getShell();
    const bool isRefused = shell && shell->isPermissionRefused();
    if (!m_isRunning && !isRefused && (m_measureState == MeasureState::Stopped || m_measureState == MeasureState::Failed))
    {
        m_percentage = 0;
        m_stepText.clear();
        setMeasureState(MeasureState::Measuring);
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_optimizeimages.cpp"
