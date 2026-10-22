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

#include "pdfappprintdialog.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfsettingdefinitions.h"

#include "pdfaccenthue.h"
#include "pdfcatalog.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// Helpers of the print dialog
class PDFAppPrintDialogHelper
{
public:
    PDFAppPrintDialogHelper() = delete;

    /// Gaps of the design (1029:24633)
    static constexpr int SECTION_GAP = 16;      ///< head, body, note and actions
    static constexpr int COLUMN_GAP = 24;       ///< preview and settings
    static constexpr int ROW_GAP = 12;          ///< rows of the settings
    static constexpr int CONTROL_GAP = 8;       ///< controls of one row
    static constexpr int PREVIEW_GAP = 10;      ///< stage, navigation and the paper text
    static constexpr int FIELD_WIDTH = 88;      ///< copies, the custom scale
    static constexpr int WINDOW_MARGIN = 16;    ///< smallest distance of the card from the edge of the window

    /// Segments of the controls (the order of the design)
    enum OrientationSegment { OrientationAuto, OrientationPortrait, OrientationLandscape };
    enum ScalingSegment { ScalingFitToPage, ScalingActualSize, ScalingCustom };
    enum ColorSegment { ColorColor, ColorGrayscale };

    /// Text of a paper size: "A4 · 210 × 297 mm", "Letter · 8.5 × 11 in"
    static QString getPaperSizeText(const QPageSize& pageSize)
    {
        const QPageSize::Unit unit = QLocale().measurementSystem() == QLocale::MetricSystem ? QPageSize::Millimeter : QPageSize::Inch;
        const QSizeF size = pageSize.size(unit);
        const QString unitText = unit == QPageSize::Millimeter ? QStringLiteral("mm") : QStringLiteral("in");
        const int precision = unit == QPageSize::Millimeter ? 0 : 2;
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        const QString width = locale.toString(size.width(), 'g', precision == 0 ? 6 : 4);
        const QString height = locale.toString(size.height(), 'g', precision == 0 ? 6 : 4);
        return PDFAppPrintDialog::tr("%1 · %2 × %3 %4").arg(pageSize.name(), width, height, unitText);
    }

    /// "1 page", "1,592 pages" (English has no translation file: the singular is a
    /// text of its own, the plural forms of other languages come from the plural)
    static QString getPagesText(int count)
    {
        return count == 1 ? PDFAppPrintDialog::tr("1 page") : PDFAppPrintDialog::tr("%Ln pages", nullptr, count);
    }

    /// Page (from 1) at the index of the result intervals, 0 if there is none
    static int getPageAt(const QList<PDFPageRange::Interval>& intervals, qint64 index)
    {
        for (const PDFPageRange::Interval& interval : intervals)
        {
            const qint64 count = qint64(interval.last) - qint64(interval.first) + 1;
            if (count <= 0)
            {
                continue;
            }
            if (index < count)
            {
                return int(interval.first + index);
            }
            index -= count;
        }
        return 0;
    }

    /// Creates a label of the design
    static PDFLabel* createLabel(const QString& text, PDFThemeFonts::Role role, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFLabel* label = new PDFLabel(text, role, color, parent);
        label->setTextFormat(Qt::PlainText);
        return label;
    }
};

/// Amber note of the dialog without a printer (1029:25542): bg and border of the
/// amber hue, radius 10, paddings 12 / 10, the icon alert-triangle 16, the texts
/// and the button Refresh
class PDFAppPrintNoPrinterNote : public QWidget
{
public:
    explicit PDFAppPrintNoPrinterNote(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF noteRect(rect());
        PDFStylePainter::fill(&painter, noteRect, PDFStylePainter::Radii(RADIUS), PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::Background));
        PDFStylePainter::strokeInside(&painter, noteRect, PDFStylePainter::Radii(RADIUS), PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::Border));
    }

private:
    static constexpr qreal RADIUS = 10.0;
};

PDFAppPrintPreview::PDFAppPrintPreview(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("printPreview"));
    setFixedSize(STAGE_WIDTH, STAGE_HEIGHT);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

PDFAppPrintPreview::~PDFAppPrintPreview() = default;

void PDFAppPrintPreview::setPaperSize(const QSizeF& paperSize)
{
    if (paperSize.isValid() && !paperSize.isEmpty() && m_paperSize != paperSize)
    {
        m_paperSize = paperSize;
        update();
    }
}

QRectF PDFAppPrintPreview::getPaperRect() const
{
    const QRectF area = QRectF(rect()).adjusted(STAGE_PADDING, STAGE_PADDING, -STAGE_PADDING, -STAGE_PADDING);
    QRectF paperRect(QPointF(), m_paperSize.scaled(area.size(), Qt::KeepAspectRatio));
    paperRect.moveCenter(area.center());
    return paperRect;
}

void PDFAppPrintPreview::setImage(const QImage& image)
{
    m_image = image;
    update();
}

QSize PDFAppPrintPreview::sizeHint() const
{
    return QSize(STAGE_WIDTH, STAGE_HEIGHT);
}

QSize PDFAppPrintPreview::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppPrintPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Stage bg/track, radius 12
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(STAGE_RADIUS), PDFThemeColors::BgTrack);

    // The paper is a page of the document: its colors do not follow the theme (Sample page)
    const QRectF paperRect = getPaperRect();
    PDFStylePainter::shadow(&painter, paperRect, PDFStylePainter::Radii(0.0), QPointF(0.0, 0.72), 3.6, PDFStylePainter::color(PDFThemeColors::EffectShadow));
    PDFStylePainter::fillRect(&painter, paperRect, PDFThemeColors::ReadingPaperPage);
    if (!m_image.isNull())
    {
        painter.drawImage(paperRect, m_image);
    }
}

PDFAppPrintDialog::PDFAppPrintDialog(PDFAppDocument* document, PDFInteger currentPage, const std::vector<PDFInteger>& initialPages,
                                     const PDFCMSSettings& cmsSettings, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral,
              tr("Print %1").arg(document ? document->getTitle() : QString()),
              tr("%1 · the printers of this computer").arg(PDFAppPrintDialogHelper::getPagesText(document ? int(qMin<PDFInteger>(document->getPageCount(), INT_MAX)) : 0)),
              QDialogButtonBox::NoButton, parent),
    m_document(document),
    m_pdfDocument(document ? document->getDocument() : PDFDocumentPointer()),
    m_cmsSettings(cmsSettings),
    m_currentPage(currentPage)
{
    setObjectName(QStringLiteral("printDialog"));
    setBadgeIconName(QStringLiteral("printer"));
    setCardWidth(CARD_WIDTH);
    if (layout())
    {
        layout()->setSpacing(PDFAppPrintDialogHelper::SECTION_GAP);
    }

    m_isLowResolution = !PDFAppPrintController::isHighResolutionAllowed(m_pdfDocument.data());
    PDFAppContext* context = document ? document->getContext() : nullptr;
    m_isRealText = context && context->getDatabase() && context->getSettings().getBool(PDFSettingKeys::renderingRealText());

    // Body: the preview and the settings; a low window scrolls them
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("printBody"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    m_scrollArea = scrollArea;

    QWidget* body = new QWidget(scrollArea);
    body->setObjectName(QStringLiteral("printBodyContent"));
    QHBoxLayout* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(PDFAppPrintDialogHelper::COLUMN_GAP);
    bodyLayout->addWidget(createPreviewColumn(), 0, Qt::AlignTop);
    bodyLayout->addWidget(createSettingsColumn(), 1, Qt::AlignTop);
    scrollArea->setWidget(body);
    if (scrollArea->viewport())
    {
        scrollArea->viewport()->setAutoFillBackground(false);
    }
    body->setAutoFillBackground(false);
    addContent(scrollArea);

    createNoPrinterNote();
    createActions();

    // The pages: the selected thumbnails preselect the mode Selected (O9)
    if (PDFPageSelection* selection = m_pageSelection.data())
    {
        const int pageCount = document ? int(qMin<PDFInteger>(document->getPageCount(), PDFPageRange::MAX_PAGE_COUNT)) : 0;
        selection->setPageCount(pageCount);
        selection->setCurrentPage(int(currentPage) + 1);

        QList<int> selectedPages;
        for (PDFInteger page : initialPages)
        {
            if (page >= 0 && page < pageCount)
            {
                selectedPages << int(page + 1);
            }
        }
        selection->setSelectedPages(selectedPages);

        const PDFCatalog* catalog = m_pdfDocument ? m_pdfDocument->getCatalog() : nullptr;
        if (catalog)
        {
            QStringList labels;
            QList<QSizeF> sizes;
            bool hasLabels = false;
            labels.reserve(pageCount);
            sizes.reserve(pageCount);
            for (int i = 0; i < pageCount; ++i)
            {
                const QString label = catalog->getPageLabel(i);
                hasLabels = hasLabels || (!label.isEmpty() && label != QString::number(i + 1));
                labels << label;
                sizes << PDFAppPrintRenderer::getPageSize(catalog->getPage(i));
            }
            if (hasLabels)
            {
                selection->setPageLabels(labels);
            }
            selection->setPageSizes(sizes);
        }

        const bool isThumbnailSelection = selectedPages.size() > 1 || (selectedPages.size() == 1 && selectedPages.front() != int(currentPage) + 1);
        selection->setMode(isThumbnailSelection ? PDFPageSelection::Mode::Selected : PDFPageSelection::Mode::All);
        connect(selection, &PDFPageSelection::resultChanged, this, &PDFAppPrintDialog::onSettingsChanged);
    }

    refreshPrinters();
    updateControls();
    adaptToHost();
    schedulePreview();
}

PDFAppPrintDialog::~PDFAppPrintDialog()
{
    // A running preview keeps its own snapshot; its picture is not delivered
    if (m_previewWatcher)
    {
        m_previewWatcher->disconnect(this);
    }
    if (m_host)
    {
        m_host->removeEventFilter(this);
    }
}

PDFComboBox* PDFAppPrintDialog::getPrinterCombo() const
{
    return m_printerCombo.data();
}

QPushButton* PDFAppPrintDialog::getPropertiesButton() const
{
    return m_propertiesButton.data();
}

PDFPageSelection* PDFAppPrintDialog::getPageSelection() const
{
    return m_pageSelection.data();
}

PDFSpinBox* PDFAppPrintDialog::getCopiesSpinBox() const
{
    return m_copiesSpinBox.data();
}

QCheckBox* PDFAppPrintDialog::getCollateCheckBox() const
{
    return m_collateCheckBox.data();
}

PDFSegmentedControl* PDFAppPrintDialog::getOrientationControl() const
{
    return m_orientationControl.data();
}

PDFComboBox* PDFAppPrintDialog::getPaperCombo() const
{
    return m_paperCombo.data();
}

PDFSegmentedControl* PDFAppPrintDialog::getScalingControl() const
{
    return m_scalingControl.data();
}

PDFSpinBox* PDFAppPrintDialog::getScaleSpinBox() const
{
    return m_scaleSpinBox.data();
}

PDFSegmentedControl* PDFAppPrintDialog::getColorControl() const
{
    return m_colorControl.data();
}

QWidget* PDFAppPrintDialog::getNoPrinterNote() const
{
    return m_noPrinterNote.data();
}

QPushButton* PDFAppPrintDialog::getRefreshButton() const
{
    return m_refreshButton.data();
}

QPushButton* PDFAppPrintDialog::getPrintButton() const
{
    return m_printButton.data();
}

QPushButton* PDFAppPrintDialog::getCancelButton() const
{
    return m_cancelButton.data();
}

PDFAppPrintPreview* PDFAppPrintDialog::getPreview() const
{
    return m_preview.data();
}

PDFIconButton* PDFAppPrintDialog::getPreviousButton() const
{
    return m_previousButton.data();
}

PDFIconButton* PDFAppPrintDialog::getNextButton() const
{
    return m_nextButton.data();
}

QWidget* PDFAppPrintDialog::createPreviewColumn()
{
    using Helper = PDFAppPrintDialogHelper;

    QWidget* column = new QWidget(this);
    column->setObjectName(QStringLiteral("previewColumn"));
    column->setFixedWidth(PDFAppPrintPreview::STAGE_WIDTH);
    QVBoxLayout* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Helper::PREVIEW_GAP);

    m_preview = new PDFAppPrintPreview(column);
    layout->addWidget(m_preview, 0, Qt::AlignHCenter);

    QHBoxLayout* navigation = new QHBoxLayout();
    navigation->setContentsMargins(0, 0, 0, 0);
    navigation->setSpacing(Helper::CONTROL_GAP);
    navigation->addStretch(1);
    m_previousButton = new PDFIconButton(QStringLiteral("chevron-left"), PDFIconButton::Size::Medium, column);
    m_previousButton->setObjectName(QStringLiteral("previewPreviousButton"));
    m_previousButton->setAccessibleName(tr("Previous page of the preview"));
    m_previousButton->setToolTip(tr("Previous page"));
    navigation->addWidget(m_previousButton);
    m_previewLabel = Helper::createLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, column);
    m_previewLabel->setObjectName(QStringLiteral("previewLabel"));
    navigation->addWidget(m_previewLabel);
    m_nextButton = new PDFIconButton(QStringLiteral("chevron-right"), PDFIconButton::Size::Medium, column);
    m_nextButton->setObjectName(QStringLiteral("previewNextButton"));
    m_nextButton->setAccessibleName(tr("Next page of the preview"));
    m_nextButton->setToolTip(tr("Next page"));
    navigation->addWidget(m_nextButton);
    navigation->addStretch(1);
    layout->addLayout(navigation);

    m_paperLabel = Helper::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, column);
    m_paperLabel->setObjectName(QStringLiteral("paperLabel"));
    m_paperLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(m_paperLabel);

    connect(m_previousButton, &QAbstractButton::clicked, this, &PDFAppPrintDialog::onPreviousClicked);
    connect(m_nextButton, &QAbstractButton::clicked, this, &PDFAppPrintDialog::onNextClicked);

    m_previewColumn = column;
    return column;
}

QWidget* PDFAppPrintDialog::createRow(const QString& label, QWidget* control, bool isTopAligned)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppPrintDialogHelper::ROW_GAP);

    PDFLabel* labelWidget = PDFAppPrintDialogHelper::createLabel(label, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, row);
    labelWidget->setFixedWidth(LABEL_WIDTH);
    labelWidget->setWordWrap(true);
    labelWidget->setBuddy(control);
    layout->addWidget(labelWidget, 0, isTopAligned ? Qt::AlignTop : Qt::AlignVCenter);
    layout->addWidget(control, 1);
    return row;
}

QWidget* PDFAppPrintDialog::createSettingsColumn()
{
    using Helper = PDFAppPrintDialogHelper;

    QWidget* column = new QWidget(this);
    column->setObjectName(QStringLiteral("settingsColumn"));
    QVBoxLayout* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Helper::ROW_GAP);

    // Printer: the combo box, Printer properties… of the driver (Windows; not in
    // Figma yet) and the state of the printer
    {
        QWidget* control = new QWidget(column);
        QVBoxLayout* controlLayout = new QVBoxLayout(control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->setSpacing(6);

        QHBoxLayout* printerLayout = new QHBoxLayout();
        printerLayout->setContentsMargins(0, 0, 0, 0);
        printerLayout->setSpacing(Helper::CONTROL_GAP);
        m_printerCombo = new PDFComboBox(control);
        m_printerCombo->setObjectName(QStringLiteral("printerCombo"));
        m_printerCombo->setAccessibleName(tr("Printer"));
        printerLayout->addWidget(m_printerCombo, 1);
        m_propertiesButton = new QPushButton(tr("Printer properties…"), control);
        m_propertiesButton->setObjectName(QStringLiteral("printerPropertiesButton"));
        m_propertiesButton->setToolTip(tr("Settings of the driver of the printer"));
        printerLayout->addWidget(m_propertiesButton);
        controlLayout->addLayout(printerLayout);

        QWidget* status = new QWidget(control);
        status->setObjectName(QStringLiteral("printerStatus"));
        QHBoxLayout* statusLayout = new QHBoxLayout(status);
        statusLayout->setContentsMargins(0, 0, 0, 0);
        statusLayout->setSpacing(6);
        m_printerStatusIcon = new PDFIconBadge(QStringLiteral("check-circle"), 14, 14, status);
        m_printerStatusIcon->setColors(std::nullopt, PDFAccentHueColors::getColor(PDFAccentHue::Green, PDFAccentHueColors::Role::Icon));
        statusLayout->addWidget(m_printerStatusIcon);
        m_printerStatusLabel = Helper::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextSecondary, status);
        m_printerStatusLabel->setObjectName(QStringLiteral("printerStatusLabel"));
        statusLayout->addWidget(m_printerStatusLabel, 1);
        controlLayout->addWidget(status);
        m_printerStatus = status;

        layout->addWidget(createRow(tr("Printer"), control, true));

        connect(m_printerCombo, &QComboBox::currentIndexChanged, this, &PDFAppPrintDialog::onPrinterChanged);
        connect(m_propertiesButton, &QPushButton::clicked, this, &PDFAppPrintDialog::onPropertiesClicked);
    }

    // Pages: the common page selection; its header has only the count (the row has the label)
    {
        m_pageSelection = new PDFPageSelection(column);
        m_pageSelection->setObjectName(QStringLiteral("pageSelection"));
        m_pageSelection->setLabel(QString());
        layout->addWidget(createRow(tr("Pages"), m_pageSelection, true));
    }

    // Copies and Collate
    {
        QWidget* control = new QWidget(column);
        QHBoxLayout* controlLayout = new QHBoxLayout(control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->setSpacing(Helper::CONTROL_GAP);
        m_copiesSpinBox = new PDFSpinBox(control);
        m_copiesSpinBox->setObjectName(QStringLiteral("copiesSpinBox"));
        m_copiesSpinBox->setAccessibleName(tr("Copies"));
        m_copiesSpinBox->setRange(1, PDFAppPrintSettings::MAXIMUM_COPIES);
        m_copiesSpinBox->setFixedWidth(Helper::FIELD_WIDTH);
        controlLayout->addWidget(m_copiesSpinBox);
        m_collateCheckBox = new QCheckBox(tr("Collate"), control);
        m_collateCheckBox->setObjectName(QStringLiteral("collateCheckBox"));
        m_collateCheckBox->setChecked(true);
        controlLayout->addWidget(m_collateCheckBox);
        controlLayout->addStretch(1);
        layout->addWidget(createRow(tr("Copies"), control, false));

        connect(m_copiesSpinBox, &QSpinBox::valueChanged, this, &PDFAppPrintDialog::onSettingsChanged);
        connect(m_collateCheckBox, &QCheckBox::toggled, this, &PDFAppPrintDialog::onSettingsChanged);
    }

    // Orientation
    {
        QWidget* control = new QWidget(column);
        QHBoxLayout* controlLayout = new QHBoxLayout(control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        m_orientationControl = new PDFSegmentedControl(control);
        m_orientationControl->setObjectName(QStringLiteral("orientationControl"));
        m_orientationControl->setAccessibleName(tr("Orientation"));
        m_orientationControl->addSegment(tr("Auto"), QStringLiteral("orientation-auto"));
        m_orientationControl->addSegment(tr("Portrait"), QStringLiteral("file-tall"));
        m_orientationControl->addSegment(tr("Landscape"), QStringLiteral("file-wide"));
        controlLayout->addWidget(m_orientationControl);
        controlLayout->addStretch(1);
        layout->addWidget(createRow(tr("Orientation"), control, false));

        connect(m_orientationControl, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppPrintDialog::onSettingsChanged);
    }

    // Paper size
    {
        m_paperCombo = new PDFComboBox(column);
        m_paperCombo->setObjectName(QStringLiteral("paperCombo"));
        m_paperCombo->setAccessibleName(tr("Paper size"));
        layout->addWidget(createRow(tr("Paper size"), m_paperCombo, false));

        connect(m_paperCombo, &QComboBox::currentIndexChanged, this, &PDFAppPrintDialog::onSettingsChanged);
    }

    // Scaling and the custom scale
    {
        QWidget* control = new QWidget(column);
        QHBoxLayout* controlLayout = new QHBoxLayout(control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->setSpacing(Helper::CONTROL_GAP);
        m_scalingControl = new PDFSegmentedControl(control);
        m_scalingControl->setObjectName(QStringLiteral("scalingControl"));
        m_scalingControl->setAccessibleName(tr("Scaling"));
        m_scalingControl->addSegment(tr("Fit to page"));
        m_scalingControl->addSegment(tr("Actual size"));
        m_scalingControl->addSegment(tr("Custom"));
        controlLayout->addWidget(m_scalingControl);
        m_scaleSpinBox = new PDFSpinBox(control);
        m_scaleSpinBox->setObjectName(QStringLiteral("scaleSpinBox"));
        m_scaleSpinBox->setAccessibleName(tr("Custom scale"));
        m_scaleSpinBox->setRange(PDFAppPrintSettings::MINIMUM_SCALE, PDFAppPrintSettings::MAXIMUM_SCALE);
        m_scaleSpinBox->setValue(100);
        m_scaleSpinBox->setDisplaySuffix(QStringLiteral("%"));
        m_scaleSpinBox->setFixedWidth(Helper::FIELD_WIDTH);
        controlLayout->addWidget(m_scaleSpinBox);
        controlLayout->addStretch(1);
        layout->addWidget(createRow(tr("Scaling"), control, false));

        connect(m_scalingControl, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppPrintDialog::onScalingChanged);
        connect(m_scaleSpinBox, &QSpinBox::valueChanged, this, &PDFAppPrintDialog::onSettingsChanged);
    }

    // Color
    {
        QWidget* control = new QWidget(column);
        QHBoxLayout* controlLayout = new QHBoxLayout(control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        m_colorControl = new PDFSegmentedControl(control);
        m_colorControl->setObjectName(QStringLiteral("colorControl"));
        m_colorControl->setAccessibleName(tr("Color"));
        m_colorControl->addSegment(tr("Color"));
        m_colorControl->addSegment(tr("Grayscale"));
        controlLayout->addWidget(m_colorControl);
        controlLayout->addStretch(1);
        layout->addWidget(createRow(tr("Color"), control, false));

        connect(m_colorControl, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppPrintDialog::onSettingsChanged);
    }

    return column;
}

void PDFAppPrintDialog::createNoPrinterNote()
{
    using Helper = PDFAppPrintDialogHelper;

    PDFAppPrintNoPrinterNote* note = new PDFAppPrintNoPrinterNote(this);
    note->setObjectName(QStringLiteral("noPrinterNote"));
    QHBoxLayout* layout = new QHBoxLayout(note);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("alert-triangle"), 16, 16, note);
    icon->setColors(std::nullopt, PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::Icon));
    layout->addWidget(icon, 0, Qt::AlignVCenter);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    textLayout->addWidget(Helper::createLabel(tr("No printer is installed"), PDFThemeFonts::Role::SmallSemiBold, PDFThemeColors::TextPrimary, note));
    PDFLabel* text = Helper::createLabel(tr("Add a printer in the settings of your system, then refresh the list. The settings above are kept."),
                                         PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, note);
    text->setWordWrap(true);
    textLayout->addWidget(text);
    layout->addLayout(textLayout, 1);

    m_refreshButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("refresh-cw")), tr("Refresh"), note);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    layout->addWidget(m_refreshButton, 0, Qt::AlignVCenter);
    connect(m_refreshButton, &QPushButton::clicked, this, &PDFAppPrintDialog::onRefreshClicked);

    m_noPrinterNote = note;
    addContent(note);
}

void PDFAppPrintDialog::createActions()
{
    // The summary at the leading edge, Cancel and Print at the trailing edge
    QWidget* actions = new QWidget(this);
    actions->setObjectName(QStringLiteral("printActions"));
    QHBoxLayout* layout = new QHBoxLayout(actions);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppPrintDialogHelper::CONTROL_GAP);

    m_summaryLabel = PDFAppPrintDialogHelper::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, actions);
    m_summaryLabel->setObjectName(QStringLiteral("summaryLabel"));
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel, 1);

    m_cancelButton = new QPushButton(tr("Cancel"), actions);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_cancelButton->setAutoDefault(false);
    layout->addWidget(m_cancelButton);

    m_printButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("printer")), tr("Print"), actions);
    m_printButton->setObjectName(QStringLiteral("printButton"));
    PDFWidgetStyleProperties::set(m_printButton, PDFWidgetStyleProperties::PRIMARY);
    m_printButton->setDefault(true);
    layout->addWidget(m_printButton);

    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_printButton, &QPushButton::clicked, this, &PDFAppPrintDialog::onPrintClicked);
    addContent(actions);
}

void PDFAppPrintDialog::refreshPrinters()
{
    const QString previousName = getPrinterIndex() >= 0 ? m_printers[size_t(getPrinterIndex())].name : QString();
    m_printers = PDFAppPrinters::getPrinters();

    if (PDFComboBox* combo = m_printerCombo.data())
    {
        QSignalBlocker blocker(combo);
        combo->clear();
        int index = 0;
        for (size_t i = 0; i < m_printers.size(); ++i)
        {
            combo->addItem(m_printers[i].name);
            if (m_printers[i].name == previousName)
            {
                index = int(i);
            }
        }
        if (m_printers.empty())
        {
            combo->addItem(tr("No printer found"));
        }
        combo->setCurrentIndex(index);
    }

    createPrinterForSelection();
    updateControls();
}

int PDFAppPrintDialog::getPrinterIndex() const
{
    const int index = m_printerCombo ? m_printerCombo->currentIndex() : -1;
    return index >= 0 && index < int(m_printers.size()) ? index : -1;
}

void PDFAppPrintDialog::setPrinterIndex(int index)
{
    if (m_printerCombo && index >= 0 && index < int(m_printers.size()))
    {
        m_printerCombo->setCurrentIndex(index);
    }
}

void PDFAppPrintDialog::createPrinterForSelection()
{
    const int index = getPrinterIndex();
    if (index < 0)
    {
        m_printer.reset();
        fillPaperSizes();
        return;
    }

    const QPrinter::PrinterMode mode = m_isLowResolution ? QPrinter::ScreenResolution : QPrinter::HighResolution;
    m_printer = PDFAppPrinters::createPrinter(m_printers[size_t(index)], mode);
    fillPaperSizes();
}

void PDFAppPrintDialog::fillPaperSizes()
{
    PDFComboBox* combo = m_paperCombo.data();
    if (!combo)
    {
        return;
    }

    const QPageSize previous = combo->currentIndex() >= 0 ? combo->currentData().value<QPageSize>() : QPageSize();
    const int index = getPrinterIndex();
    QList<QPageSize> pageSizes = index >= 0 ? m_printers[size_t(index)].pageSizes : QList<QPageSize>();
    if (pageSizes.isEmpty())
    {
        pageSizes = PDFAppPrinters::getStandardPageSizes();
    }

    QPageSize selected = previous;
    if (!selected.isValid())
    {
        selected = index >= 0 && m_printers[size_t(index)].defaultPageSize.isValid() ? m_printers[size_t(index)].defaultPageSize : PDFAppPrinters::getLocaleDefaultPageSize();
    }

    QSignalBlocker blocker(combo);
    combo->clear();
    int selectedIndex = 0;
    for (const QPageSize& pageSize : pageSizes)
    {
        if (!pageSize.isValid())
        {
            continue;
        }
        if (pageSize.isEquivalentTo(selected))
        {
            selectedIndex = combo->count();
        }
        combo->addItem(PDFAppPrintDialogHelper::getPaperSizeText(pageSize), QVariant::fromValue(pageSize));
    }
    combo->setCurrentIndex(selectedIndex);
}

void PDFAppPrintDialog::setRange(const QString& range)
{
    if (m_pageSelection)
    {
        m_pageSelection->setMode(PDFPageSelection::Mode::Range);
        m_pageSelection->setRange(range);
    }
}

void PDFAppPrintDialog::setCopies(int copies)
{
    if (m_copiesSpinBox)
    {
        m_copiesSpinBox->setValue(qBound(1, copies, PDFAppPrintSettings::MAXIMUM_COPIES));
    }
}

std::vector<PDFInteger> PDFAppPrintDialog::getResultPages() const
{
    std::vector<PDFInteger> pages;
    if (!m_pageSelection || !m_pageSelection->isValid())
    {
        return pages;
    }

    const QList<PDFPageRange::Interval>& intervals = m_pageSelection->getResultIntervals();
    pages.reserve(size_t(qMax<qint64>(0, m_pageSelection->getResultCount())));
    for (const PDFPageRange::Interval& interval : intervals)
    {
        for (int page = interval.first; page <= interval.last && page >= 1; ++page)
        {
            pages.push_back(PDFInteger(page - 1));
        }
    }
    return pages;
}

PDFAppPrintSettings PDFAppPrintDialog::getSettings() const
{
    using Helper = PDFAppPrintDialogHelper;

    PDFAppPrintSettings settings;
    settings.pages = getResultPages();
    settings.copies = m_copiesSpinBox ? m_copiesSpinBox->value() : 1;
    settings.collate = !m_collateCheckBox || m_collateCheckBox->isChecked();

    switch (m_orientationControl ? m_orientationControl->getCurrentIndex() : int(Helper::OrientationAuto))
    {
        case Helper::OrientationPortrait:
            settings.orientation = PDFAppPrintOrientation::Portrait;
            break;
        case Helper::OrientationLandscape:
            settings.orientation = PDFAppPrintOrientation::Landscape;
            break;
        default:
            settings.orientation = PDFAppPrintOrientation::Auto;
            break;
    }

    switch (m_scalingControl ? m_scalingControl->getCurrentIndex() : int(Helper::ScalingFitToPage))
    {
        case Helper::ScalingActualSize:
            settings.scaling = PDFAppPrintScaling::ActualSize;
            break;
        case Helper::ScalingCustom:
            settings.scaling = PDFAppPrintScaling::Custom;
            break;
        default:
            settings.scaling = PDFAppPrintScaling::FitToPage;
            break;
    }

    settings.customScale = m_scaleSpinBox ? m_scaleSpinBox->value() : 100;
    settings.isGrayscale = m_colorControl && m_colorControl->getCurrentIndex() == Helper::ColorGrayscale;
    if (m_paperCombo && m_paperCombo->currentIndex() >= 0)
    {
        settings.pageSize = m_paperCombo->currentData().value<QPageSize>();
    }
    if (!settings.pageSize.isValid())
    {
        settings.pageSize = PDFAppPrinters::getLocaleDefaultPageSize();
    }
    return settings;
}

void PDFAppPrintDialog::updateControls()
{
    const int index = getPrinterIndex();
    const bool hasPrinter = index >= 0 && m_printer;

    // The printer-bound controls are disabled without a printer (1029:24881)
    for (QWidget* widget : { static_cast<QWidget*>(m_printerCombo.data()), static_cast<QWidget*>(m_copiesSpinBox.data()),
                             static_cast<QWidget*>(m_paperCombo.data()) })
    {
        if (widget)
        {
            widget->setEnabled(hasPrinter);
        }
    }
    if (m_collateCheckBox)
    {
        m_collateCheckBox->setEnabled(hasPrinter && m_copiesSpinBox && m_copiesSpinBox->value() > 1);
    }
    if (m_propertiesButton)
    {
        m_propertiesButton->setVisible(hasPrinter && PDFAppPrinters::hasPrinterProperties(m_printers[size_t(index)]));
    }
    if (m_noPrinterNote)
    {
        m_noPrinterNote->setVisible(m_printers.empty());
    }
    if (m_scaleSpinBox && m_scalingControl)
    {
        m_scaleSpinBox->setEnabled(m_scalingControl->getCurrentIndex() == PDFAppPrintDialogHelper::ScalingCustom);
    }

    if (m_printerStatus && m_printerStatusLabel && m_printerStatusIcon)
    {
        m_printerStatus->setVisible(hasPrinter);
        if (hasPrinter)
        {
            const PDFAppPrinterInfo& info = m_printers[size_t(index)];
            m_printerStatusIcon->setIconName(info.isReady ? QStringLiteral("check-circle") : QStringLiteral("alert-triangle"));
            m_printerStatusIcon->setColors(std::nullopt, PDFAccentHueColors::getColor(info.isReady ? PDFAccentHue::Green : PDFAccentHue::Amber, PDFAccentHueColors::Role::Icon));
            m_printerStatusLabel->setText(getPrinterStatusText());
        }
    }

    if (m_printButton)
    {
        const bool hasPages = m_pageSelection && m_pageSelection->isValid() && m_pageSelection->getResultCount() > 0;
        m_printButton->setEnabled(hasPrinter && hasPages);
    }

    updateSummary();
    updatePreviewNavigation();
}

QString PDFAppPrintDialog::getPrinterStatusText() const
{
    const int index = getPrinterIndex();
    if (index < 0)
    {
        return QString();
    }

    const PDFAppPrinterInfo& info = m_printers[size_t(index)];
    QStringList parts;
    parts << (info.stateText.isEmpty() ? tr("Ready") : info.stateText);
    if (!info.location.isEmpty())
    {
        parts << info.location;
    }
    else if (!info.description.isEmpty() && info.description != info.name)
    {
        parts << info.description;
    }
    return parts.join(QStringLiteral(" · "));
}

QString PDFAppPrintDialog::getSummaryText() const
{
    const qint64 pageCount = m_pageSelection && m_pageSelection->isValid() ? m_pageSelection->getResultCount() : 0;
    const int copies = m_copiesSpinBox ? m_copiesSpinBox->value() : 1;
    const qint64 total = pageCount * copies;
    const int totalInt = int(qMin<qint64>(total, INT_MAX));

    // One page on a sheet (no two-sided printing in phase 1)
    const QString pages = PDFAppPrintDialogHelper::getPagesText(totalInt);
    const QString sheets = totalInt == 1 ? tr("1 sheet") : tr("%Ln sheets", nullptr, totalInt);
    QString text = copies > 1 ? tr("%1 on %2 – %3").arg(pages, sheets, tr("%Ln copies", nullptr, copies)) : tr("%1 on %2").arg(pages, sheets);
    if (m_isLowResolution)
    {
        text += tr(" · low resolution (the document allows no more)");
    }
    return text;
}

QString PDFAppPrintDialog::getPaperText() const
{
    const PDFAppPrintSettings settings = getSettings();
    QString scaling;
    switch (settings.scaling)
    {
        case PDFAppPrintScaling::FitToPage:
            scaling = tr("fit to page");
            break;
        case PDFAppPrintScaling::ActualSize:
            scaling = tr("actual size");
            break;
        case PDFAppPrintScaling::Custom:
            scaling = tr("%1 %").arg(settings.customScale);
            break;
    }
    return tr("%1 · %2").arg(PDFAppPrintDialogHelper::getPaperSizeText(settings.pageSize), scaling);
}

QString PDFAppPrintDialog::getPreviewText() const
{
    const qint64 count = m_pageSelection && m_pageSelection->isValid() ? m_pageSelection->getResultCount() : 0;
    if (count <= 0 || !m_pageSelection)
    {
        return tr("No page");
    }

    const int page = PDFAppPrintDialogHelper::getPageAt(m_pageSelection->getResultIntervals(), qBound(qint64(0), qint64(m_previewIndex), count - 1));
    const PDFCatalog* catalog = m_pdfDocument ? m_pdfDocument->getCatalog() : nullptr;
    const QString label = catalog ? catalog->getPageLabel(page - 1) : QString::number(page);
    return tr("Page %1 · %2 of %3").arg(label.isEmpty() ? QString::number(page) : label).arg(m_previewIndex + 1).arg(count);
}

void PDFAppPrintDialog::updateSummary()
{
    if (m_summaryLabel)
    {
        m_summaryLabel->setText(getSummaryText());
    }
    if (m_paperLabel)
    {
        m_paperLabel->setText(getPaperText());
    }
}

void PDFAppPrintDialog::updatePreviewNavigation()
{
    const qint64 count = m_pageSelection && m_pageSelection->isValid() ? m_pageSelection->getResultCount() : 0;
    m_previewIndex = int(qBound(qint64(0), qint64(m_previewIndex), qMax(qint64(0), count - 1)));
    if (m_previousButton)
    {
        m_previousButton->setEnabled(m_previewIndex > 0);
    }
    if (m_nextButton)
    {
        m_nextButton->setEnabled(m_previewIndex + 1 < count);
    }
    if (m_previewLabel)
    {
        m_previewLabel->setText(getPreviewText());
    }
}

void PDFAppPrintDialog::setPreviewIndex(int index)
{
    if (m_previewIndex != index)
    {
        m_previewIndex = index;
        updatePreviewNavigation();
        schedulePreview();
    }
}

void PDFAppPrintDialog::onPreviousClicked()
{
    setPreviewIndex(m_previewIndex - 1);
}

void PDFAppPrintDialog::onNextClicked()
{
    setPreviewIndex(m_previewIndex + 1);
}

void PDFAppPrintDialog::onPrinterChanged(int index)
{
    Q_UNUSED(index);
    createPrinterForSelection();
    updateControls();
    schedulePreview();
}

void PDFAppPrintDialog::onPropertiesClicked()
{
    const int index = getPrinterIndex();
    if (index < 0 || !m_printer)
    {
        return;
    }

    // The settings of the dialog go to the driver first
    const PDFAppPrintSettings settings = getSettings();
    m_printer->setPageSize(settings.pageSize);
    m_printer->setPageOrientation(settings.orientation == PDFAppPrintOrientation::Landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    m_printer->setColorMode(settings.isGrayscale ? QPrinter::GrayScale : QPrinter::Color);

    // The native dialog runs its own event loop: the dialog may be deleted meanwhile
    QPointer<PDFAppPrintDialog> guard(this);
    std::shared_ptr<QPrinter> printer = m_printer;
    const PDFAppPrinterInfo info = m_printers[size_t(index)];
    const bool isAccepted = PDFAppPrinters::showPrinterProperties(this, printer.get(), info);
    if (!guard || !isAccepted || printer != m_printer)
    {
        return;
    }

    // The settings of the driver come back to the dialog
    const QPageLayout layout = printer->pageLayout();
    if (m_paperCombo)
    {
        for (int i = 0; i < m_paperCombo->count(); ++i)
        {
            if (m_paperCombo->itemData(i).value<QPageSize>().isEquivalentTo(layout.pageSize()))
            {
                m_paperCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_orientationControl && settings.orientation != PDFAppPrintOrientation::Auto)
    {
        m_orientationControl->setCurrentIndex(layout.orientation() == QPageLayout::Landscape ? PDFAppPrintDialogHelper::OrientationLandscape
                                                                                              : PDFAppPrintDialogHelper::OrientationPortrait);
    }
    if (m_colorControl)
    {
        m_colorControl->setCurrentIndex(printer->colorMode() == QPrinter::GrayScale ? PDFAppPrintDialogHelper::ColorGrayscale : PDFAppPrintDialogHelper::ColorColor);
    }
    if (m_copiesSpinBox && printer->copyCount() > 1)
    {
        // The job prints the copies itself
        m_copiesSpinBox->setValue(printer->copyCount());
        printer->setCopyCount(1);
    }
    updateControls();
    schedulePreview();
}

void PDFAppPrintDialog::onRefreshClicked()
{
    refreshPrinters();
    schedulePreview();
}

void PDFAppPrintDialog::onPrintClicked()
{
    if (!m_printer || getSettings().pages.empty())
    {
        return;
    }
    accept();
}

void PDFAppPrintDialog::onSettingsChanged()
{
    updateControls();
    schedulePreview();
}

void PDFAppPrintDialog::onScalingChanged(int index)
{
    Q_UNUSED(index);
    onSettingsChanged();
}

PDFAppPrintDialog::PreviewGeometry PDFAppPrintDialog::getPreviewGeometry(const PDFAppPrintSettings& settings) const
{
    PreviewGeometry geometry;
    const qint64 count = m_pageSelection && m_pageSelection->isValid() ? m_pageSelection->getResultCount() : 0;
    const int page = count > 0 ? PDFAppPrintDialogHelper::getPageAt(m_pageSelection->getResultIntervals(), qBound(qint64(0), qint64(m_previewIndex), count - 1)) : 0;
    const PDFCatalog* catalog = m_pdfDocument ? m_pdfDocument->getCatalog() : nullptr;
    const PDFPage* pdfPage = catalog && page >= 1 && page <= PDFInteger(catalog->getPageCount()) ? catalog->getPage(page - 1) : nullptr;
    geometry.page = pdfPage ? page : 0;

    // The paper of the page (points, oriented) and the printable area of the printer
    const QPageLayout::Orientation orientation = PDFAppPrintRenderer::getPaperOrientation(pdfPage, settings.orientation);
    QSizeF paperSize = settings.pageSize.size(QPageSize::Point);
    if (orientation == QPageLayout::Landscape)
    {
        paperSize.transpose();
    }
    geometry.paperRect = QRectF(QPointF(0, 0), paperSize);
    geometry.printableRect = geometry.paperRect;
    if (m_printer)
    {
        QPageLayout layout = m_printer->pageLayout();
        layout.setPageSize(settings.pageSize);
        layout.setOrientation(orientation);
        geometry.printableRect = layout.paintRect(QPageLayout::Point);
    }
    return geometry;
}

void PDFAppPrintDialog::schedulePreview()
{
    // The paper follows the settings at once, the picture comes from the worker
    if (m_preview)
    {
        m_preview->setPaperSize(getPreviewGeometry(getSettings()).paperRect.size());
    }

    // One preview at a time: the newest settings are rendered after the running one
    if (m_previewWatcher)
    {
        m_isPreviewPending = true;
        return;
    }
    startPreview();
}

void PDFAppPrintDialog::startPreview()
{
    m_isPreviewPending = false;
    PDFAppPrintPreview* preview = m_preview.data();
    if (!preview || m_pdfDocument.isNull() || !m_pageSelection)
    {
        return;
    }

    const PDFAppPrintSettings settings = getSettings();
    const PreviewGeometry geometry = getPreviewGeometry(settings);
    preview->setPaperSize(geometry.paperRect.size());
    if (geometry.page <= 0)
    {
        preview->setImage(QImage());
        return;
    }

    const qreal devicePixelRatio = qMax<qreal>(1.0, preview->devicePixelRatioF());
    const QSize imageSize = (preview->getPaperRect().size() * devicePixelRatio).toSize();
    if (imageSize.isEmpty())
    {
        return;
    }

    QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(this);
    m_previewWatcher = watcher;
    connect(watcher, &QFutureWatcher<QImage>::finished, this, &PDFAppPrintDialog::onPreviewFinished);
    watcher->setFuture(QtConcurrent::run(&PDFAppPrintRenderer::renderPreview, m_pdfDocument, m_cmsSettings, PDFInteger(geometry.page - 1), settings,
                                         geometry.paperRect, geometry.printableRect, imageSize, m_isRealText));
}

void PDFAppPrintDialog::onPreviewFinished()
{
    QFutureWatcher<QImage>* watcher = m_previewWatcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    QImage image = watcher->future().result();
    m_previewWatcher = nullptr;
    watcher->deleteLater();

    if (m_isPreviewPending)
    {
        // The settings have changed meanwhile: the picture is old
        startPreview();
        return;
    }

    if (m_preview)
    {
        image.setDevicePixelRatio(qMax<qreal>(1.0, m_preview->devicePixelRatioF()));
        m_preview->setImage(image);
    }
    Q_EMIT previewRendered();
}

void PDFAppPrintDialog::setVisible(bool visible)
{
    if (visible)
    {
        // The card follows the window it covers (a narrow or a low window); the
        // size is set before the message box places itself
        QWidget* host = getScrimTarget() ? getScrimTarget() : parentWidget();
        if (host != m_host.data())
        {
            if (m_host)
            {
                m_host->removeEventFilter(this);
            }
            m_host = host;
            if (host)
            {
                host->installEventFilter(this);
            }
        }
        adaptToHost();
    }
    BaseClass::setVisible(visible);
}

bool PDFAppPrintDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_host.data() && event->type() == QEvent::Resize)
    {
        adaptToHost();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppPrintDialog::adaptToHost()
{
    using Helper = PDFAppPrintDialogHelper;

    QWidget* host = m_host.data();
    if (!m_scrollArea || !m_scrollArea->widget())
    {
        return;
    }

    if (!host)
    {
        // Not shown yet: the body has its whole height
        QWidget* body = m_scrollArea->widget();
        const int width = getContentWidth() > 0 ? getContentWidth() : body->sizeHint().width();
        m_scrollArea->setFixedHeight(body->hasHeightForWidth() ? body->heightForWidth(width) : body->sizeHint().height());
        return;
    }

    // A narrow window: the preview is hidden and the card fits the window
    const int availableWidth = host->width() - 2 * Helper::WINDOW_MARGIN;
    const bool isNarrow = availableWidth < CARD_WIDTH;
    if (m_previewColumn)
    {
        m_previewColumn->setVisible(!isNarrow);
    }
    setCardWidth(isNarrow ? qMax(360, availableWidth) : CARD_WIDTH);

    // A low window: the body scrolls
    QWidget* body = m_scrollArea->widget();
    const int contentWidth = getContentWidth() > 0 ? getContentWidth() : body->sizeHint().width();
    const int bodyHeight = body->hasHeightForWidth() ? body->heightForWidth(contentWidth) : body->sizeHint().height();
    const int otherHeight = sizeHint().height() - m_scrollArea->minimumHeight();
    const int availableHeight = host->height() - 2 * Helper::WINDOW_MARGIN - qMax(0, otherHeight);
    const int scrollAreaHeight = qMax(120, qMin(bodyHeight, availableHeight));
    m_scrollArea->setFixedHeight(scrollAreaHeight);

    // The scroll bar lies over the content: a scrolled body keeps its width free
    if (QLayout* bodyLayout = body->layout())
    {
        const int scrollBarWidth = bodyHeight > scrollAreaHeight ? style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_scrollArea) : 0;
        bodyLayout->setContentsMargins(isRightToLeft() ? scrollBarWidth : 0, 0, isRightToLeft() ? 0 : scrollBarWidth, 0);
    }

    if (isVisible() && isWindow())
    {
        // A shown card takes its new size and stays centered over the window
        if (layout())
        {
            layout()->activate();
        }
        adjustSize();
        const QMargins shadow = getShadowMargins();
        const QRect card = rect().marginsRemoved(shadow);
        QRect cardRect(QPoint(0, 0), card.size());
        cardRect.moveCenter(QRect(host->mapToGlobal(QPoint(0, 0)), host->size()).center());
        move(cardRect.topLeft() - card.topLeft());
    }
}

}   // namespace pdf
