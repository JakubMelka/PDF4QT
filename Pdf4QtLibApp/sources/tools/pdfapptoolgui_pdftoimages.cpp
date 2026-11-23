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

#include "pdfapptoolgui_pdftoimages.h"

#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapppagerendercontext.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolgui_split.h"
#include "pdfapptoolhost.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcatalog.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfpage.h"
#include "pdfstylepainterguard.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QPainter>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace pdf
{

/// Helpers of the GUI of PDF to Images
class PDFAppToolGUIPdfToImagesHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUIPdfToImages)

public:
    PDFAppToolGUIPdfToImagesHelper() = delete;

    using Tool = PDFDocumentToolPdfToImages;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The property of a row of the menu of the resolution: its resolution
    static constexpr const char* DPI_PROPERTY = "pdf4qtImageDpi";

    /// The name of a format ("WebP" of 339:4239, the others in upper case)
    static QString getFormatTitle(const QByteArray& format)
    {
        return PDFAppToolImageFormatMenu::getFormatTitle(format);
    }

    /// The description of a resolution of the menu (339:4239)
    static QString getResolutionDescription(int dpi)
    {
        switch (dpi)
        {
            case 72:
                return tr("Screen previews");
            case 150:
                return tr("Reading on screen");
            case 300:
                return tr("Print quality");
            case 600:
                return tr("Fine detail, large files");
            default:
                break;
        }
        return QString();
    }

    /// A row of a card of the right panel: the label and the control (339:3546 Subtype, Size by)
    static QWidget* createRow(const QString& label, QWidget* control, QWidget* parent)
    {
        // The field row shared with Extract Images (341:7516)
        return PDFAppToolCard::createFieldRow(label, control, parent);
    }

    /// A card of the right panel with its body (339:3546 Image, Resolution)
    static QVBoxLayout* addCard(PDFAppToolPanelBuilder& builder, const QString& objectName)
    {
        PDFAppToolCard* card = new PDFAppToolCard(builder.getWidget());
        card->setObjectName(objectName);
        card->setFieldSpacing();
        builder.addWidget(card);
        return card->getBodyLayout();
    }
};

PDFAppToolGUIPdfToImages::PDFAppToolGUIPdfToImages(QObject* parent) :
    BaseClass(parent)
{
    m_outputFiles = new PDFAppToolOutputFilesSection(this);
    m_estimateWatcher = new QFutureWatcher<qint64>(this);
    connect(m_estimateWatcher.data(), &QFutureWatcher<qint64>::finished, this, &PDFAppToolGUIPdfToImages::onEstimateFinished);
}

PDFAppToolGUIPdfToImages::~PDFAppToolGUIPdfToImages()
{
    // The worker of the estimate holds only values; its rendering stops (the watcher, a child,
    // does not wait for it)
    if (m_estimateCancel)
    {
        m_estimateCancel->cancel();
    }
}

void PDFAppToolGUIPdfToImages::registerPdfToImagesTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolPdfToImages::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolPdfToImages>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIPdfToImages>>());
    }
}

qint64 PDFAppToolGUIPdfToImages::estimatePageBytes(PDFDocumentPointer document, PDFInteger pageIndex, QVariantMap parameters, PDFDocumentToolRenderContext rendering,
                                                   std::shared_ptr<PDFAppCancelFlag> control)
{
    if (control && control->isOperationCanceled())
    {
        return -1;
    }

    const PDFPage* page = document && pageIndex >= 0 && size_t(pageIndex) < document->getCatalog()->getPageCount() ? document->getCatalog()->getPage(size_t(pageIndex)) : nullptr;
    const PDFDocumentToolPdfToImages::PageGeometry geometry = PDFDocumentToolPdfToImages::getPageGeometry(page, parameters);
    if (!geometry.isValid())
    {
        return -1;
    }

    // A sample of the rendered page (at most ESTIMATE_SAMPLE_PIXELS), the crop box of it encoded
    const qint64 renderPixels = qint64(geometry.renderSize.width()) * qint64(geometry.renderSize.height());
    const double scale = renderPixels > ESTIMATE_SAMPLE_PIXELS ? std::sqrt(double(ESTIMATE_SAMPLE_PIXELS) / double(renderPixels)) : 1.0;
    const QSize sampleSize(qMax(1, int(geometry.renderSize.width() * scale)), qMax(1, int(geometry.renderSize.height() * scale)));
    QImage image(sampleSize, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
    {
        return -1;
    }
    image.fill(Qt::white);

    {
        // The color management, the features and the layers of the run (R4-10 b)
        const PDFDocumentToolPageRenderSettings settings = PDFDocumentToolPageRenderSettings::create(rendering);
        PDFAppPageRenderContext renderContext(document.data(), settings.cmsSettings.value_or(PDFCMSSettings()), PDFDocumentToolPageRenderSettings::getExportFeatures(settings.features));
        if (settings.layerStates)
        {
            renderContext.setOptionalContentStates(*settings.layerStates);
        }
        QPainter painter(&image);
        const QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), QSizeF(sampleSize)), PageRotation::None);
        renderContext.draw(&painter, pageIndex, matrix, true, control.get());
    }
    if (control && control->isOperationCanceled())
    {
        return -1;
    }

    const QRect cropRect(QPoint(int(geometry.cropRect.left() * scale), int(geometry.cropRect.top() * scale)),
                         QSize(qMax(1, int(geometry.cropRect.width() * scale)), qMax(1, int(geometry.cropRect.height() * scale))));
    QString errorMessage;
    const QByteArray data = PDFDocumentToolImageFormat::fromParameters(parameters).encode(image.copy(cropRect.intersected(image.rect())), &errorMessage);
    if (data.isEmpty())
    {
        return -1;
    }

    // The bytes grow with the pixels of the real image
    const double ratio = double(geometry.imageSize.width()) * double(geometry.imageSize.height()) / qMax(1.0, double(cropRect.width()) * double(cropRect.height()));
    return qint64(double(data.size()) * ratio);
}

PDFDocumentToolImageFormat PDFAppToolGUIPdfToImages::getImageFormat() const
{
    return PDFDocumentToolImageFormat::fromParameters(getValues());
}

PDFDocumentToolPdfToImages::PageGeometry PDFAppToolGUIPdfToImages::getPreviewGeometry() const
{
    return PDFDocumentToolPdfToImages::getPageGeometry(getPage(getPreviewPage()), getValues());
}

qint64 PDFAppToolGUIPdfToImages::getEstimatedSize() const
{
    if (m_pageBytes < 0 || m_estimateKey != getEstimateKey() || !isSameRendering(m_estimateRendering, getRunRendering()))
    {
        return -1;
    }
    return m_pageBytes * qint64(getTargetPages().size());
}

bool PDFAppToolGUIPdfToImages::isSameRendering(const PDFDocumentToolRenderContext& first, const PDFDocumentToolRenderContext& second)
{
    return first.cmsSettings == second.cmsSettings && first.features == second.features && first.engine == second.engine && first.layerStates == second.layerStates;
}

PDFDocumentToolRenderContext PDFAppToolGUIPdfToImages::getRunRendering() const
{
    // The rendering of the run (the settings and the layers of the tab), values taken in the
    // thread of the GUI (R4-10 b)
    const PDFAppToolHost* host = getHost();
    return host ? host->createEnvironment().rendering : PDFDocumentToolRenderContext();
}

bool PDFAppToolGUIPdfToImages::isEstimating() const
{
    return !m_runningEstimateKey.isEmpty();
}

QString PDFAppToolGUIPdfToImages::getResolutionText() const
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;
    using Tool = Helper::Tool;

    // 339:3546 "300 dpi", 356:11387 "2000 px"
    const QVariantMap values = getValues();
    if (values.value(Helper::name(Tool::PARAMETER_SIZE_BY)).toString() == QLatin1String(Tool::SIZE_BY_PIXELS))
    {
        return tr("%1 px").arg(values.value(Helper::name(Tool::PARAMETER_PIXELS)).toInt());
    }
    return tr("%1 dpi").arg(values.value(Helper::name(Tool::PARAMETER_DPI)).toInt());
}

QToolButton* PDFAppToolGUIPdfToImages::getFormatButton() const
{
    return m_formatMenu ? m_formatMenu->getButton() : nullptr;
}

QToolButton* PDFAppToolGUIPdfToImages::getResolutionButton() const
{
    return m_resolutionPanel ? m_resolutionPanel->getButton() : nullptr;
}

QString PDFAppToolGUIPdfToImages::getSelectionLabel() const
{
    // 339:3546 "Pages 1,592"
    return tr("Pages");
}

void PDFAppToolGUIPdfToImages::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;

    // tool-actions 339:3546 with the menus of 339:4239: Format (the shared menu of the image
    // exports, 356:11387), the resolution, Options
    PDFDeferredDeletion::retire(m_formatMenu.data());
    m_formatMenu = new PDFAppToolImageFormatMenu(builder.addMenu(QStringLiteral("format"), QStringLiteral("image"), QString(), MENU_WIDTH), this);
    m_formatMenu->setFormat(getImageFormat().format);
    connect(m_formatMenu.data(), &PDFAppToolImageFormatMenu::formatChosen, this, &PDFAppToolGUIPdfToImages::onFormatChosen);

    // 339:4239: 72, 150, 300 (Print quality), 600 dpi and Custom
    m_resolutionPanel = builder.addMenu(QStringLiteral("resolution"), QStringLiteral("grid"), QString(), MENU_WIDTH);
    m_resolutionRows.clear();
    for (const int dpi : MENU_RESOLUTIONS)
    {
        PDFTileRow* row = m_resolutionPanel->addRow(tr("%1 dpi").arg(dpi), Helper::getResolutionDescription(dpi), QStringLiteral("grid"), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("toolResolution-") + QString::number(dpi));
        row->setProperty(Helper::DPI_PROPERTY, dpi);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIPdfToImages::onResolutionRowClicked);
        m_resolutionRows.push_back(row);
    }
    m_resolutionPanel->addDividerRow();
    PDFTileRow* custom = m_resolutionPanel->addRow(tr("Custom"), tr("Type 72 to 6000 dpi, or a size in pixels, in the Resolution panel"), QStringLiteral("settings"), PDFTileRow::Kind::MenuItem);
    custom->setObjectName(QStringLiteral("toolResolution-custom"));
    connect(custom, &PDFTileRow::clicked, this, &PDFAppToolGUIPdfToImages::onCustomResolutionClicked);

    // 339:4239 Options: the write options of JPEG; the caption says, when the format has none
    std::vector<PDFAppToolFlagItem> options;
    options.push_back({ Helper::name(PDFDocumentToolImageFormat::PARAMETER_OPTIMIZED_WRITE), tr("Optimized write"), tr("Smaller files, slower export"), QStringLiteral("zap"), QString(), QString() });
    options.push_back({ Helper::name(PDFDocumentToolImageFormat::PARAMETER_PROGRESSIVE_SCAN_WRITE), tr("Progressive scan write"), tr("The image sharpens gradually while it loads"), QStringLiteral("layers"), QString(), QString() });
    if (PDFAppToolFlagsMenu* menu = builder.addFlagsMenu(std::move(options), QStringLiteral("settings"), tr("Options"), MENU_WIDTH, tr("Not available for PNG · JPEG only"), PDFTileRow::Kind::MenuItem))
    {
        m_optionsPanel = menu->getPanel();
    }
}

void PDFAppToolGUIPdfToImages::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;
    using Tool = Helper::Tool;

    // section-Output files 339:3546: "Report_1.png … Report_1592.png", "@ is replaced by the page number"
    m_outputFiles->describe(builder);

    // section-Image 339:3546: Subtype, Compression, Quality and the note (K4.9: the help says,
    // which settings of the view the pages are rendered with)
    builder.addSection(tr("Image"));
    QVBoxLayout* imageCard = Helper::addCard(builder, QStringLiteral("toolPdfToImagesImageCard"));
    PDFComboBox* subtype = new PDFComboBox(nullptr);
    subtype->setObjectName(QStringLiteral("toolPdfToImagesSubtype"));
    connect(subtype, &QComboBox::activated, this, &PDFAppToolGUIPdfToImages::onSubtypeActivated);
    m_subtypeComboBox = subtype;
    imageCard->addWidget(Helper::createRow(tr("Subtype"), subtype, imageCard->parentWidget()));

    // The controls go straight into their field rows (a second layout on the card would be
    // refused by Qt and would leak with its items)
    m_compressionControl = builder.createValueControl(Helper::name(Tool::PARAMETER_COMPRESSION), imageCard->parentWidget());
    m_qualityControl = builder.createValueControl(Helper::name(Tool::PARAMETER_QUALITY), imageCard->parentWidget());
    for (PDFAppSettingControl* control : { m_compressionControl.data(), m_qualityControl.data() })
    {
        // -1 is the default of the format: said by a word, not by a number
        if (PDFSpinBox* spinBox = control ? control->getSpinBox() : nullptr)
        {
            spinBox->setSpecialValueText(tr("Default"));
        }
    }
    if (m_compressionControl)
    {
        imageCard->addWidget(Helper::createRow(tr("Compression"), m_compressionControl->getWidget(), imageCard->parentWidget()));
    }
    if (m_qualityControl)
    {
        imageCard->addWidget(Helper::createRow(tr("Quality"), m_qualityControl->getWidget(), imageCard->parentWidget()));
    }
    PDFAppParagraph* note = PDFAppPageWidgets::createText(tr("Settings the chosen format does not support are disabled. Pages are rendered with the current view settings, including color management."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, imageCard->parentWidget());
    note->setObjectName(QStringLiteral("toolPdfToImagesImageNote"));
    note->setMinimumWidth(1);
    imageCard->addWidget(note);
    builder.endSection();

    // section-Resolution 339:3546 / 356:11387: Size by, Resolution or Longer side, Image size
    builder.addSection(tr("Resolution"));
    QVBoxLayout* resolutionCard = Helper::addCard(builder, QStringLiteral("toolPdfToImagesResolutionCard"));
    if (PDFAppSettingControl* sizeBy = builder.createValueControl(Helper::name(Tool::PARAMETER_SIZE_BY), resolutionCard->parentWidget()))
    {
        resolutionCard->addWidget(Helper::createRow(tr("Size by"), sizeBy->getWidget(), resolutionCard->parentWidget()));
    }
    m_dpiControl = builder.createValueControl(Helper::name(Tool::PARAMETER_DPI), resolutionCard->parentWidget());
    if (m_dpiControl)
    {
        // 339:3546 "300 dpi", "2000 px": the units are the muted suffixes beside the values
        if (PDFSpinBox* spinBox = m_dpiControl->getSpinBox())
        {
            spinBox->setDisplaySuffix(tr("dpi"));
        }
        m_dpiRow = Helper::createRow(tr("Resolution"), m_dpiControl->getWidget(), resolutionCard->parentWidget());
        resolutionCard->addWidget(m_dpiRow);
    }
    m_pixelsControl = builder.createValueControl(Helper::name(Tool::PARAMETER_PIXELS), resolutionCard->parentWidget());
    if (m_pixelsControl)
    {
        if (PDFSpinBox* spinBox = m_pixelsControl->getSpinBox())
        {
            spinBox->setDisplaySuffix(tr("px"));
        }
        m_pixelsRow = Helper::createRow(tr("Longer side"), m_pixelsControl->getWidget(), resolutionCard->parentWidget());
        resolutionCard->addWidget(m_pixelsRow);
    }
    m_imageSize = new PDFKeyValueList(resolutionCard->parentWidget());
    m_imageSize->setObjectName(QStringLiteral("toolPdfToImagesImageSize"));
    m_imageSize->addRow(tr("Image size"), QString());
    resolutionCard->addWidget(m_imageSize);
    builder.endSection();

    updateAll();
}

void PDFAppToolGUIPdfToImages::onCanvasCreated()
{
    // The page of the preview as it is rendered: the crop box with its rotation (B57)
}

void PDFAppToolGUIPdfToImages::updateNames()
{
    m_fileNames.clear();
    m_namesError.clear();
    const std::vector<PDFInteger> pages = getTargetPages();
    if (pages.empty())
    {
        return;
    }
    const PDFAppDocument* document = getDocument();
    PDFDocumentToolPdfToImages::createFileNames(pages, document ? QFileInfo(document->getFilePath()).fileName() : QString(), getValues(), QDate::currentDate(), &m_fileNames, &m_namesError);
}

void PDFAppToolGUIPdfToImages::updateParts()
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;
    using Tool = Helper::Tool;

    const QVariantMap values = getValues();
    const PDFDocumentToolImageFormat format = getImageFormat();
    PDFImageWriterSettings writerSettings;
    writerSettings.selectFormat(format.format);
    const bool isPixels = values.value(Helper::name(Tool::PARAMETER_SIZE_BY)).toString() == QLatin1String(Tool::SIZE_BY_PIXELS);
    const bool isEditable = !isDeciding();

    // The toolbar: "Format: PNG" and its check mark, "300 dpi" / "2000 px" and the icon of its mode
    if (m_formatMenu)
    {
        m_formatMenu->setFormat(format.format);
    }
    if (QToolButton* button = getFormatButton())
    {
        button->setEnabled(isEditable);
    }
    if (QToolButton* button = getResolutionButton())
    {
        button->setText(getResolutionText());
        button->setIcon(PDFThemeIcons::getIcon(isPixels ? QStringLiteral("layout-grid") : QStringLiteral("grid")));
        button->setEnabled(isEditable);
    }
    const int dpi = values.value(Helper::name(Tool::PARAMETER_DPI)).toInt();
    for (const QPointer<PDFTileRow>& row : m_resolutionRows)
    {
        if (row)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(!isPixels && row->property(Helper::DPI_PROPERTY).toInt() == dpi);
        }
    }

    // Options: only the options of the format (339:4239 "Not available for PNG · JPEG only")
    const bool hasWriteOptions = writerSettings.isOptionSupported(QImageIOHandler::OptimizedWrite) || writerSettings.isOptionSupported(QImageIOHandler::ProgressiveScanWrite);
    if (m_optionsPanel)
    {
        for (const QPointer<PDFTileRow>& row : m_optionsPanel->getRows())
        {
            if (row)
            {
                row->setEnabled(hasWriteOptions && isEditable);
            }
        }
        if (PDFLabel* caption = m_optionsPanel->findChild<PDFLabel*>())
        {
            caption->setText(tr("Not available for %1 · JPEG only").arg(Helper::getFormatTitle(format.format)));
            caption->setVisible(!hasWriteOptions);
        }
    }

    // The right panel: what the format does not support is disabled (339:3546 Subtype "–" of PNG)
    if (m_subtypeComboBox)
    {
        const QSignalBlocker blocker(m_subtypeComboBox);
        m_subtypeComboBox->clear();
        const QList<QByteArray> subtypes = writerSettings.getSubtypes();
        if (subtypes.isEmpty())
        {
            m_subtypeComboBox->addItem(QStringLiteral("–"), QString());
        }
        for (const QByteArray& subtype : subtypes)
        {
            m_subtypeComboBox->addItem(QString::fromLatin1(subtype), QString::fromLatin1(subtype));
        }
        const int index = m_subtypeComboBox->findData(QString::fromLatin1(format.subtype));
        m_subtypeComboBox->setCurrentIndex(qMax(0, index));
        m_subtypeComboBox->setEnabled(!subtypes.isEmpty() && isEditable);
    }
    if (m_compressionControl && m_compressionControl->getWidget())
    {
        m_compressionControl->getWidget()->setEnabled(writerSettings.isOptionSupported(QImageIOHandler::CompressionRatio) && isEditable);
    }
    if (m_qualityControl && m_qualityControl->getWidget())
    {
        m_qualityControl->getWidget()->setEnabled(writerSettings.isOptionSupported(QImageIOHandler::Quality) && isEditable);
    }
    if (m_dpiRow)
    {
        m_dpiRow->setVisible(!isPixels);
    }
    if (m_pixelsRow)
    {
        m_pixelsRow->setVisible(isPixels);
    }

    // Image size "2480 × 3508 px" of the page of the preview (the frames write the pixels without
    // the separators of the thousands, as the dimensions of the images everywhere)
    const PDFDocumentToolPdfToImages::PageGeometry geometry = getPreviewGeometry();
    const QString sizeText = geometry.isValid() ? tr("%1 × %2 px").arg(geometry.imageSize.width()).arg(geometry.imageSize.height()) : QStringLiteral("–");
    if (m_imageSize && m_imageSize->getRowCount() == 1)
    {
        m_imageSize->setValue(0, sizeText);
    }

    // Output files: "Report_1.png … Report_1592.png", "1,592 files · @ is replaced by the page number"
    if (m_outputFiles)
    {
        m_outputFiles->setPreview(m_namesError.isEmpty() ? m_fileNames : QStringList(),
                                  m_fileNames.isEmpty() ? QString() : tr("%Ln files · @ is replaced by the page number", nullptr, int(m_fileNames.size())), m_namesError);
    }

    // The canvas: "Page 308 → Report_308.png · 2480 × 3508 px"; a page too large says why (R4-19)
    PDFAppToolPageCanvas* canvas = getCanvas();
    PDFAppToolPageStage* stage = canvas ? canvas->getStage(0) : nullptr;
    if (canvas && stage)
    {
        const PDFAppDocument* appDocument = getDocument();
        stage->setPage(getShownDocument(), appDocument ? appDocument->getId() : QString(), getPreviewPage());

        const std::vector<PDFInteger> pages = getTargetPages();
        const auto it = std::lower_bound(pages.cbegin(), pages.cend(), getPreviewPage());
        const bool isExported = it != pages.cend() && *it == getPreviewPage();
        const qsizetype position = isExported ? qsizetype(it - pages.cbegin()) : -1;
        const QString fileName = position >= 0 && position < m_fileNames.size() ? m_fileNames[position] : QString();
        canvas->setTitle(fileName.isEmpty() ? tr("%1 · not exported").arg(getPreviewPageTitle()) : tr("%1 → %2 · %3").arg(getPreviewPageTitle(), fileName, sizeText));

        const QString pageError = PDFDocumentToolPdfToImages::checkPageGeometry(getPreviewPage(), geometry, format);
        canvas->setCaption(0, pageError.isEmpty() ? QString() : pageError, !pageError.isEmpty());
    }
}

void PDFAppToolGUIPdfToImages::updateTool()
{
    updateNames();
    updateParts();
    startEstimate();
}

QString PDFAppToolGUIPdfToImages::getEstimateKey() const
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;
    using Tool = Helper::Tool;

    // The document by its identity and version (an address of a freed document may come again),
    // the values, which change the bytes of a page (not the pages, not the names)
    const QVariantMap values = getValues();
    const PDFAppDocument* document = getDocument();
    QStringList parts;
    parts << (document ? document->getId() : QString()) << QString::number(document ? document->getVersion() : 0) << QString::number(getPreviewPage());
    for (const char* name : { Tool::PARAMETER_FORMAT, Tool::PARAMETER_SUBTYPE, Tool::PARAMETER_COMPRESSION, Tool::PARAMETER_QUALITY, Tool::PARAMETER_SIZE_BY, Tool::PARAMETER_DPI, Tool::PARAMETER_PIXELS })
    {
        parts << values.value(Helper::name(name)).toString();
    }
    return parts.join(QLatin1Char('|'));
}

void PDFAppToolGUIPdfToImages::startEstimate()
{
    const QString key = getEstimateKey();
    const PDFDocumentToolRenderContext rendering = getRunRendering();
    if (!m_estimateWatcher || (key == m_estimateKey && isSameRendering(rendering, m_estimateRendering)))
    {
        return;
    }
    if (!m_runningEstimateKey.isEmpty())
    {
        // One job at a time: a job of other values is canceled (its rendering stops, RS4APP), the
        // finished job starts the next one
        if ((key != m_runningEstimateKey || !isSameRendering(rendering, m_runningEstimateRendering)) && m_estimateCancel)
        {
            m_estimateCancel->cancel();
        }
        return;
    }

    const PDFDocumentPointer document = getShownDocument();
    const PDFDocumentToolPdfToImages::PageGeometry geometry = getPreviewGeometry();
    if (!document || !geometry.isValid() || !PDFDocumentToolPdfToImages::checkPageGeometry(getPreviewPage(), geometry, getImageFormat()).isEmpty())
    {
        m_estimateKey = key;
        m_estimateRendering = rendering;
        m_pageBytes = -1;
        return;
    }

    m_runningEstimateKey = key;
    m_runningEstimateRendering = rendering;
    m_estimateCancel = std::make_shared<PDFAppCancelFlag>();
    m_estimateWatcher->setFuture(QtConcurrent::run(&PDFAppToolGUIPdfToImages::estimatePageBytes, document, getPreviewPage(), getValues(), rendering, m_estimateCancel));
}

void PDFAppToolGUIPdfToImages::onEstimateFinished()
{
    if (!m_estimateWatcher)
    {
        return;
    }

    // A canceled job says nothing about its values: the values of now are estimated again
    const bool isCanceled = !m_estimateCancel || m_estimateCancel->isOperationCanceled();
    if (!isCanceled)
    {
        m_estimateKey = m_runningEstimateKey;
        m_estimateRendering = m_runningEstimateRendering;
        m_pageBytes = m_estimateWatcher->future().result();
    }
    m_runningEstimateKey.clear();
    m_estimateCancel.reset();

    // The values may have changed meanwhile; the status bar says the estimate (a slot of the
    // shell runs: the last thing)
    startEstimate();
    updateStatusText();
}

void PDFAppToolGUIPdfToImages::selectFormat(const QByteArray& format)
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;

    // A subtype of the other format would not exist: it goes with the format (one undo step)
    if (PDFAppToolParameterModel* model = getModel())
    {
        QVariantMap values;
        values.insert(Helper::name(PDFDocumentToolImageFormat::PARAMETER_FORMAT), QString::fromLatin1(format));
        values.insert(Helper::name(PDFDocumentToolImageFormat::PARAMETER_SUBTYPE), QString());
        model->setValues(values);
    }
}

void PDFAppToolGUIPdfToImages::selectResolution(int dpi)
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;
    using Tool = Helper::Tool;

    if (PDFAppToolParameterModel* model = getModel())
    {
        QVariantMap values;
        values.insert(Helper::name(Tool::PARAMETER_SIZE_BY), QString::fromLatin1(Tool::SIZE_BY_DPI));
        values.insert(Helper::name(Tool::PARAMETER_DPI), dpi);
        model->setValues(values);
    }
}

void PDFAppToolGUIPdfToImages::onFormatChosen(const QByteArray& format)
{
    // The menu is closed already; the model runs the slots of the GUI last
    selectFormat(format);
}

void PDFAppToolGUIPdfToImages::onResolutionRowClicked()
{
    QObject* row = sender();
    const int dpi = row ? row->property(PDFAppToolGUIPdfToImagesHelper::DPI_PROPERTY).toInt() : 0;
    if (dpi <= 0)
    {
        return;
    }

    if (m_resolutionPanel)
    {
        m_resolutionPanel->close();
    }
    if (!PDFDeferredDeletion::isRetired(this))
    {
        selectResolution(dpi);
    }
}

void PDFAppToolGUIPdfToImages::onCustomResolutionClicked()
{
    // 339:4239 "Type 72 to 6000 dpi, or a size in pixels, in the Resolution panel"
    if (m_resolutionPanel)
    {
        m_resolutionPanel->close();
    }
    if (PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    PDFAppSettingControl* control = m_dpiRow && m_dpiRow->isVisible() ? m_dpiControl.data() : m_pixelsControl.data();
    if (control && control->getEditor())
    {
        control->getEditor()->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppToolGUIPdfToImages::onSubtypeActivated(int index)
{
    if (!m_subtypeComboBox || index < 0)
    {
        return;
    }
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(PDFAppToolGUIPdfToImagesHelper::name(PDFDocumentToolImageFormat::PARAMETER_SUBTYPE), m_subtypeComboBox->itemData(index).toString());
    }
}

QString PDFAppToolGUIPdfToImages::getConfirmText() const
{
    // 339:3546 "Export Images"
    return tr("Export Images");
}

QString PDFAppToolGUIPdfToImages::getStatusText() const
{
    using Helper = PDFAppToolGUIPdfToImagesHelper;
    using Tool = Helper::Tool;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return BaseClass::getStatusText();
    }

    // 339:3546 "PDF to Images · Project_Report.pdf · 1,592 pages · PNG at 300 dpi · about 2.1 GB",
    // 356:11387 "… · PNG, longer side 2000 px"
    const QVariantMap values = getValues();
    const PDFAppDocument* document = getDocument();
    const QString fileName = document ? QFileInfo(document->getFilePath()).fileName() : QString();
    const QString formatName = Helper::getFormatTitle(getImageFormat().format);
    const bool isPixels = values.value(Helper::name(Tool::PARAMETER_SIZE_BY)).toString() == QLatin1String(Tool::SIZE_BY_PIXELS);
    const QString resolution = isPixels ? tr("%1, longer side %2 px").arg(formatName).arg(values.value(Helper::name(Tool::PARAMETER_PIXELS)).toInt())
                                        : tr("%1 at %2 dpi").arg(formatName).arg(values.value(Helper::name(Tool::PARAMETER_DPI)).toInt());
    QString text = tr("%1 · %2 · %Ln pages · %3", nullptr, count).arg(title, fileName, resolution);
    const qint64 estimate = getEstimatedSize();
    if (estimate >= 0)
    {
        text = tr("%1 · about %2").arg(text, PDFAppLocale::formatDataSize(estimate, 1, QLocale::DataSizeTraditionalFormat));
    }
    return text;
}

void PDFAppToolGUIPdfToImages::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    // R14: a file of the run never replaces a document open in the application
    BaseClass::prepareEnvironment(environment);
    environment->protectedFilePaths = PDFAppToolGUISplit::getOpenDocumentPaths(this);
}

}   // namespace pdf

#include "moc_pdfapptoolgui_pdftoimages.cpp"
