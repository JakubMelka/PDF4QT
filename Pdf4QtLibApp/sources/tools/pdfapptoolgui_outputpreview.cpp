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

#include "pdfapptoolgui_outputpreview.h"

#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappoutputpreviewcanvas.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolgui_softproof.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfappsettingcontrol.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include "pdfdrawspacecontroller.h"

#include "pdfdocumenttool_outputpreview.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfoperationcontrol.h"
#include "pdfutils.h"

#include <QButtonGroup>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <cmath>

namespace pdf
{

/// The cancellation of the job of a picture: a new request cancels the old job
class PDFAppOutputPreviewControl : public PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return m_isCanceled.load(); }
    void cancel() { m_isCanceled.store(true); }

private:
    std::atomic_bool m_isCanceled = false;
};

using PDFAppOutputPreviewResultPointer = std::shared_ptr<const PDFOutputPreviewResult>;

/// The job of a picture (QtConcurrent::run, one job of the GUI, owner 2026-11-09): plain values only - the
/// document by its shared pointer (immutable), the settings and the shared cancellation; never the GUI
class PDFAppOutputPreviewRenderTask
{
public:
    PDFDocumentPointer document;
    PDFInteger pageIndex = -1;
    PDFOutputPreviewSettings settings;
    std::shared_ptr<PDFAppOutputPreviewControl> control;

    PDFAppOutputPreviewResultPointer operator()() const
    {
        return std::make_shared<const PDFOutputPreviewResult>(PDFOutputPreviewAnalyzer::render(document.data(), pageIndex, settings, control.get()));
    }
};

/// Helpers of the GUI of Output Preview (no free functions, CLAUDE.md)
class PDFAppToolGUIOutputPreviewHelper
{
public:
    PDFAppToolGUIOutputPreviewHelper() = delete;

    using Tool = PDFDocumentToolOutputPreview;
    using Mode = PDFOutputPreviewAnalyzer::Mode;

    /// Width of the panels of the mode and of the content (panel-mode 474:9070)
    static constexpr int PANEL_WIDTH = 330;

    /// Property of a row of the mode: its identifier
    static constexpr const char* MODE_PROPERTY = "pdf4qtOutputPreviewMode";

    /// Properties of an item of the inks: the name of the ink (Latin-1), the index in the result
    static constexpr int INK_NAME_ROLE = Qt::UserRole + 1;
    static constexpr int INK_INDEX_ROLE = Qt::UserRole + 2;

    /// The columns of the inks (ink-table 474:8521)
    static constexpr int INK_COLUMN = 0;
    static constexpr int CURSOR_COLUMN = 1;
    static constexpr int AREA_COLUMN = 2;

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    static QString formatNumber(double value, int decimals)
    {
        return PDFAppLocale::getUiLocale().toString(value, 'f', decimals);
    }

    static QString formatPercent(double value, int decimals)
    {
        return PDFAppToolGUIOutputPreview::tr("%1 %", "A percentage").arg(formatNumber(value, decimals));
    }

    static QString formatArea(double value)
    {
        return PDFAppToolGUIOutputPreview::tr("%1 mm²", "An area").arg(formatNumber(value, 2));
    }

    /// The check box of a group by its inks: all checked, none, or Mixed (Qt::PartiallyChecked)
    static void updateGroupState(QStandardItem* group)
    {
        if (!group)
        {
            return;
        }
        int checked = 0;
        int count = 0;
        for (int row = 0; row < group->rowCount(); ++row)
        {
            const QStandardItem* child = group->child(row, INK_COLUMN);
            if (child && child->isEnabled())
            {
                checked += child->checkState() == Qt::Checked ? 1 : 0;
                ++count;
            }
        }
        group->setCheckState(checked == 0 ? Qt::Unchecked : (checked == count ? Qt::Checked : Qt::PartiallyChecked));
    }

    static bool isChannelMode(Mode mode)
    {
        return mode == Mode::ShapeChannel || mode == Mode::OpacityChannel;
    }

    /// The field of a limit in percent (474:9818 "300 %", 474:10178 "95 %"): whole percent, the unit drawn muted
    static void preparePercentField(PDFAppSettingControl* control)
    {
        if (PDFDoubleSpinBox* spinBox = control ? control->getDoubleSpinBox() : nullptr)
        {
            spinBox->setDecimals(0);
            spinBox->setSingleStep(1.0);
            spinBox->setDisplaySuffix(PDFAppToolGUIOutputPreview::tr("%", "Unit of a limit in percent"));
        }
    }

    /// A swatch of a color for the item of an ink (the color of the ink, not of the theme)
    static QIcon createSwatchIcon(const QColor& color)
    {
        QPixmap pixmap(14, 14);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        PDFStylePainter::fill(&painter, QRectF(0.5, 0.5, 13, 13), PDFStylePainter::Radii(3), color);
        PDFStylePainter::strokeInside(&painter, QRectF(0, 0, 14, 14), PDFStylePainter::Radii(3), PDFThemeColors::BorderDefault);
        return QIcon(pixmap);
    }
};

// ------------------------------------------------------------------ PDFAppOutputPreviewSample

PDFAppOutputPreviewSample::PDFAppOutputPreviewSample(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outputPreviewSample"));

    // sample 474:8644: the rectangle 44 × 30, 10 px, the texts
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(54, 0, 0, 0);
    layout->setSpacing(0);
    QVBoxLayout* texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(1);
    texts->addWidget(PDFAppPageWidgets::createLabel(tr("Sample color"), QFont::Medium, 12, PDFThemeColors::TextPrimary, this));
    m_coverage = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    m_coverage->setObjectName(QStringLiteral("outputPreviewSampleCoverage"));
    texts->addWidget(m_coverage);
    layout->addLayout(texts, 1);
    setMinimumHeight(30);
}

PDFAppOutputPreviewSample::~PDFAppOutputPreviewSample() = default;

void PDFAppOutputPreviewSample::setSample(const QColor& color, const QString& coverageText)
{
    m_color = color;
    m_coverage->setText(coverageText);
    update();
}

void PDFAppOutputPreviewSample::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QRectF swatch(isRightToLeft ? width() - 44 : 0, (height() - 30) / 2.0, 44, 30);
    const PDFStylePainter::Radii radii(6);
    PDFStylePainter::fill(&painter, swatch, radii, m_color.isValid() ? m_color : PDFStylePainter::color(PDFThemeColors::BgSubtle));
    PDFStylePainter::strokeInside(&painter, swatch, radii, PDFThemeColors::BorderDefault);
}

// ------------------------------------------------------------------ PDFAppOutputPreviewLegend

PDFAppOutputPreviewLegend::PDFAppOutputPreviewLegend(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outputPreviewLegend"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

PDFAppOutputPreviewLegend::~PDFAppOutputPreviewLegend() = default;

void PDFAppOutputPreviewLegend::setValues(double maximum, double cursorValue)
{
    m_maximum = maximum;
    m_cursorValue = cursorValue;
    update();
}

double PDFAppOutputPreviewLegend::getScaleMaximum() const
{
    // The scale of PDFOutputPreviewAnalyzer::createImage: from no ink to the highest coverage, at least 100 %
    return qMax(100.0, m_maximum);
}

QString PDFAppOutputPreviewLegend::getLabelText(int index) const
{
    const double value = getScaleMaximum() * double(4 - index) / 4.0;
    return PDFAppToolGUIOutputPreviewHelper::formatPercent(value, 2);
}

QString PDFAppOutputPreviewLegend::getMarkerText() const
{
    return m_cursorValue < 0.0 ? QString() : tr("%1 at the cursor").arg(PDFAppToolGUIOutputPreviewHelper::formatPercent(m_cursorValue, 2));
}

QSize PDFAppOutputPreviewLegend::sizeHint() const
{
    return QSize(261, 200);
}

void PDFAppOutputPreviewLegend::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Distribution 474:10536: the bar 22 wide, the values 10 px after it, the marker of the cursor
    QPainter painter(this);
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal barTop = 6.0;
    const qreal barHeight = height() - 12.0;
    const qreal barX = isRightToLeft ? width() - 22.0 : 0.0;
    const PDFColorScale scale(0.0, getScaleMaximum());
    const int steps = qMax(1, int(barHeight));
    for (int i = 0; i < steps; ++i)
    {
        const double value = getScaleMaximum() * (1.0 - double(i) / double(steps));
        PDFStylePainter::fillRect(&painter, QRectF(barX, barTop + i, 22.0, 1.0), scale.map(value));
    }

    const QFont font = PDFThemeFonts::getFont(QFont::Normal, 11);
    const int alignment = int(isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute | Qt::AlignVCenter;
    for (int i = 0; i <= 4; ++i)
    {
        const qreal y = barTop + barHeight * i / 4.0;
        const QRectF text = isRightToLeft ? QRectF(0.0, y - 8.0, width() - 32.0, 16.0) : QRectF(32.0, y - 8.0, width() - 32.0, 16.0);
        PDFStylePainter::text(&painter, text, alignment, getLabelText(i), font, PDFStylePainter::color(PDFThemeColors::TextSecondary));
    }

    if (m_cursorValue >= 0.0)
    {
        // The marker: a pill bg/subtle with the value at the cursor, at its height of the bar
        const double share = qBound(0.0, m_cursorValue / getScaleMaximum(), 1.0);
        const qreal y = barTop + barHeight * (1.0 - share);
        const QFont markerFont = PDFThemeFonts::getFont(QFont::Medium, 11);
        const QString text = getMarkerText();
        const qreal textWidth = PDFStylePainter::textWidth(text, markerFont);
        const qreal pillWidth = textWidth + 30.0;
        const QRectF pill(isRightToLeft ? 8.0 : width() - pillWidth - 8.0, y - 10.0, pillWidth, 20.0);
        PDFStylePainter::fill(&painter, pill, PDFStylePainter::Radii(10), PDFThemeColors::BgSubtle);
        PDFStylePainter::icon(&painter, QRectF(isRightToLeft ? pill.right() - 18.0 : pill.left() + 6.0, pill.center().y() - 6.0, 12.0, 12.0),
                              isRightToLeft ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"), PDFThemeColors::AccentRedIcon);
        PDFStylePainter::text(&painter, QRectF(isRightToLeft ? pill.left() + 8.0 : pill.left() + 22.0, pill.top(), textWidth, pill.height()),
                              Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute, text, markerFont, PDFStylePainter::color(PDFThemeColors::TextPrimary));
    }
}

// ------------------------------------------------------------------ PDFAppOutputPreviewLimitRows

PDFAppOutputPreviewLimitRows::PDFAppOutputPreviewLimitRows(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outputPreviewLimitRows"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppOutputPreviewLimitRows::update));
}

PDFAppOutputPreviewLimitRows::~PDFAppOutputPreviewLimitRows() = default;

QString PDFAppOutputPreviewLimitRows::getWithinTitle()
{
    return PDFAppToolGUIOutputPreview::tr("Within the limit");
}

QString PDFAppOutputPreviewLimitRows::getOverTitle()
{
    return PDFAppToolGUIOutputPreview::tr("Over the limit");
}

void PDFAppOutputPreviewLimitRows::setValues(const QString& withinValue, const QString& overValue, const QColor& alarmColor)
{
    m_withinValue = withinValue;
    m_overValue = overValue;
    m_alarmColor = alarmColor;
    setAccessibleDescription(isEmpty() ? QString() : QStringLiteral("%1: %2; %3: %4").arg(getWithinTitle(), m_withinValue, getOverTitle(), m_overValue));
    updateGeometry();
    update();
}

void PDFAppOutputPreviewLimitRows::clear()
{
    setValues(QString(), QString(), QColor());
}

QRect PDFAppOutputPreviewLimitRows::getDotRect(int row) const
{
    // The dot 10 at the leading edge of the row, centered in its 15 (474:10099)
    const int top = PADDING_Y + row * (ROW_HEIGHT + ROW_GAP) + (ROW_HEIGHT - DOT_SIZE) / 2;
    return QStyle::visualRect(layoutDirection(), rect(), QRect(PADDING_X, top, DOT_SIZE, DOT_SIZE));
}

QSize PDFAppOutputPreviewLimitRows::sizeHint() const
{
    return isEmpty() ? QSize(0, 0) : QSize(2 * PADDING_X + 200, 2 * PADDING_Y + 2 * ROW_HEIGHT + ROW_GAP);
}

QSize PDFAppOutputPreviewLimitRows::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppOutputPreviewLimitRows::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (isEmpty())
    {
        return;
    }

    // result 474:10097: the band bg/subtle, radius 6; a row: the dot, 8, the title (Regular 12 text/secondary), the
    // value (Medium 12 text/primary) at the trailing edge
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(6), PDFThemeColors::BgSubtle);

    const Qt::LayoutDirection direction = layoutDirection();
    const QFont titleFont = PDFThemeFonts::getFont(QFont::Normal, 12);
    const QFont valueFont = PDFThemeFonts::getFont(QFont::Medium, 12);
    const int leading = QStyle::visualAlignment(direction, Qt::AlignLeft | Qt::AlignVCenter);
    const int trailing = QStyle::visualAlignment(direction, Qt::AlignRight | Qt::AlignVCenter);
    const QString titles[2] = { getWithinTitle(), getOverTitle() };
    const QString values[2] = { m_withinValue, m_overValue };
    const QColor dots[2] = { PDFStylePainter::color(PDFThemeColors::AccentGreenIcon), m_alarmColor.isValid() ? m_alarmColor : PDFStylePainter::color(PDFThemeColors::IconDanger) };
    for (int row = 0; row < 2; ++row)
    {
        const int top = PADDING_Y + row * (ROW_HEIGHT + ROW_GAP);
        const QRect rowRect(PADDING_X, top, width() - 2 * PADDING_X, ROW_HEIGHT);
        const QRect titleRect(PADDING_X + DOT_SIZE + DOT_GAP, top, rowRect.width() - DOT_SIZE - DOT_GAP, ROW_HEIGHT);
        PDFStylePainter::fill(&painter, QRectF(getDotRect(row)), PDFStylePainter::Radii(DOT_SIZE / 2.0), dots[row]);
        PDFStylePainter::text(&painter, QStyle::visualRect(direction, rect(), titleRect), leading | Qt::TextSingleLine, titles[row], titleFont, PDFStylePainter::color(PDFThemeColors::TextSecondary));
        PDFStylePainter::text(&painter, QStyle::visualRect(direction, rect(), rowRect), trailing | Qt::TextSingleLine, values[row], valueFont, PDFStylePainter::color(PDFThemeColors::TextPrimary));
    }
}

// ------------------------------------------------------------------ PDFAppInkTreeView

PDFAppInkTreeView::PDFAppInkTreeView(QWidget* parent) :
    BaseClass(parent)
{

}

PDFAppInkTreeView::~PDFAppInkTreeView() = default;

bool PDFAppInkTreeView::isGroupRow(const QModelIndex& index) const
{
    const QAbstractItemModel* itemModel = model();
    return itemModel && index.isValid() && !index.parent().isValid() && itemModel->hasChildren(index.siblingAtColumn(0));
}

void PDFAppInkTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const
{
    // 474:9958: the band bg/subtle under the whole row of a group (all its cells), the row itself over it
    if (isGroupRow(index))
    {
        PDFStylePainter::fillRect(painter, QRectF(options.rect), PDFThemeColors::BgSubtle);
    }
    BaseClass::drawRow(painter, options, index);
}

// ------------------------------------------------------------------ PDFAppPaperColorField

PDFAppPaperColorField::PDFAppPaperColorField(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("outputPreviewPaper"));

    // 476:9987: "Paper" with the name, the swatches White, Cream, Gray and "+"
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 6);
    QHBoxLayout* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->addWidget(PDFAppPageWidgets::createLabel(tr("Paper"), QFont::Normal, 12, PDFThemeColors::TextSecondary, this), 1);
    m_name = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    m_name->setObjectName(QStringLiteral("outputPreviewPaperName"));
    head->addWidget(m_name);
    layout->addLayout(head);

    QHBoxLayout* swatches = new QHBoxLayout();
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->setSpacing(PDFColorSwatchRow::INLINE_SWATCH_GAP);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    for (const auto& [color, name] : getPresets())
    {
        PDFColorSwatch* swatch = new PDFColorSwatch(color, name, this);
        swatch->setCheckable(true);
        swatch->setToolTip(name);
        m_group->addButton(swatch);
        connect(swatch, &QAbstractButton::clicked, this, &PDFAppPaperColorField::onSwatchClicked);
        swatches->addWidget(swatch);
    }
    PDFIconButton* custom = new PDFIconButton(QStringLiteral("plus"), PDFIconButton::Size::Medium, this);
    custom->setObjectName(QStringLiteral("outputPreviewPaperCustom"));
    custom->setToolTip(tr("Custom color…"));
    custom->setAccessibleName(tr("Custom color…"));
    connect(custom, &QAbstractButton::clicked, this, &PDFAppPaperColorField::onCustomClicked);
    swatches->addWidget(custom);
    swatches->addStretch(1);
    layout->addLayout(swatches);
}

PDFAppPaperColorField::~PDFAppPaperColorField()
{
    // An open dialog of a custom color goes with the field (it is a window of its own)
    if (m_dialog)
    {
        m_dialog->disconnect(this);
        m_dialog->deleteLater();
    }
}

std::vector<std::pair<QColor, QString>> PDFAppPaperColorField::getPresets()
{
    // The color of the paper is written into the settings of the preview: constant values, not the theme
    return { { QColor(0xFF, 0xFF, 0xFF), tr("White") }, { QColor(0xF5, 0xEC, 0xD9), tr("Cream") }, { QColor(0xE6, 0xE6, 0xE6), tr("Gray") } };
}

QString PDFAppPaperColorField::getColorName(const QColor& color)
{
    for (const auto& [preset, name] : getPresets())
    {
        if (preset.rgb() == color.rgb())
        {
            return name;
        }
    }
    return color.name(QColor::HexRgb).toUpper();
}

void PDFAppPaperColorField::setColor(const QColor& color)
{
    m_color = color;
    m_name->setText(getColorName(color));
    for (QAbstractButton* button : m_group->buttons())
    {
        PDFColorSwatch* swatch = qobject_cast<PDFColorSwatch*>(button);
        if (swatch)
        {
            const QSignalBlocker blocker(swatch);
            swatch->setSelected(swatch->getColor().rgb() == color.rgb());
        }
    }
}

void PDFAppPaperColorField::onSwatchClicked()
{
    PDFColorSwatch* swatch = qobject_cast<PDFColorSwatch*>(sender());
    if (!swatch)
    {
        return;
    }
    setColor(swatch->getColor());
    Q_EMIT colorSelected(m_color);
}

void PDFAppPaperColorField::onCustomClicked()
{
    if (m_dialog)
    {
        m_dialog->raise();
        return;
    }

    // A popup is new + QPointer, never on the stack, and never exec (CLAUDE.md)
    PDFColorPickerDialog* dialog = new PDFColorPickerDialog(m_color, window());
    // A dialog of the window owned by its creator (step A): retire closes it unanswered
    PDFDeferredDeletion::setOwner(dialog, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &PDFColorPickerDialog::colorSelected, this, &PDFAppPaperColorField::onDialogColorSelected);
    m_dialog = dialog;
    dialog->open();
}

void PDFAppPaperColorField::onDialogColorSelected(const QColor& color)
{
    if (!color.isValid())
    {
        return;
    }
    setColor(color);
    Q_EMIT colorSelected(m_color);
}

// ------------------------------------------------------------------ PDFAppToolGUIOutputPreview

PDFAppToolGUIOutputPreview::PDFAppToolGUIOutputPreview(QObject* parent) :
    BaseClass(parent)
{
    // 474:9818, 474:10178: the alarm color is the swatch Red of the row (its canonical value, the built-in light
    // token annotation/red), so the row shows "Red" with the swatch chosen - not a custom #FF0000
    m_warningColor = PDFThemeColors().getColor(PDFThemeColors::AnnotationRed);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(RENDER_DELAY);
    connect(m_renderTimer.data(), &QTimer::timeout, this, &PDFAppToolGUIOutputPreview::onRenderTimeout);

    m_inkModel = new QStandardItemModel(this);
    connect(m_inkModel.data(), &QStandardItemModel::itemChanged, this, &PDFAppToolGUIOutputPreview::onInkItemChanged);
}

PDFAppToolGUIOutputPreview::~PDFAppToolGUIOutputPreview()
{
    // The job ends by itself; its result is dropped with the watcher (a child)
    cancelRender();
}

void PDFAppToolGUIOutputPreview::registerOutputPreviewTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolOutputPreview::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolOutputPreview>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIOutputPreview>>());
    }
}

bool PDFAppToolGUIOutputPreview::isConfirmShown() const
{
    // 10.3 point 34, 474:8386: only Close
    return false;
}

PDFAppDocumentView* PDFAppToolGUIOutputPreview::getView() const
{
    const PDFAppToolHost* host = getHost();
    return host ? host->getView() : nullptr;
}

PDFOutputPreviewAnalyzer::Mode PDFAppToolGUIOutputPreview::getMode() const
{
    using H = PDFAppToolGUIOutputPreviewHelper;
    const PDFAppToolParameterModel* model = getModel();
    return PDFOutputPreviewAnalyzer::getMode(model ? model->getValue(H::name(H::Tool::PARAMETER_MODE)).toString() : QString());
}

PDFInteger PDFAppToolGUIOutputPreview::getPageIndex() const
{
    const PDFAppDocumentView* view = getView();
    const PDFAppDocument* document = getDocument();
    const PDFInteger pageCount = document && document->getDocument() ? PDFInteger(document->getDocument()->getCatalog()->getPageCount()) : 0;
    if (!view || pageCount == 0)
    {
        return -1;
    }
    return qBound<PDFInteger>(0, view->getDisplayedPageIndex(), pageCount - 1);
}

QString PDFAppToolGUIOutputPreview::getModeTitle(Mode mode) const
{
    switch (mode)
    {
        case Mode::Separations:
            return tr("Separations");
        case Mode::CoverageWarning:
            return tr("Coverage Warning");
        case Mode::RichBlackWarning:
            return tr("Rich Black Warning");
        case Mode::InkCoverage:
            return tr("Ink Coverage");
        case Mode::ShapeChannel:
            return tr("Shape Channel");
        case Mode::OpacityChannel:
            return tr("Opacity Channel");
    }
    return QString();
}

QString PDFAppToolGUIOutputPreview::getModeIcon(Mode mode) const
{
    // panel-mode 474:9070
    switch (mode)
    {
        case Mode::Separations:
            return QStringLiteral("separations");
        case Mode::CoverageWarning:
            return QStringLiteral("ink-warning");
        case Mode::RichBlackWarning:
            return QStringLiteral("rich-black");
        case Mode::InkCoverage:
            return QStringLiteral("ink-coverage");
        case Mode::ShapeChannel:
            return QStringLiteral("shape-channel");
        case Mode::OpacityChannel:
            return QStringLiteral("opacity-channel");
    }
    return QStringLiteral("separations");
}

void PDFAppToolGUIOutputPreview::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    // tool-actions 474:8406: the mode, Content | Simulate Separations, Paper Color
    m_modePanel = builder.addMenu(QStringLiteral("mode"), getModeIcon(getMode()), getModeTitle(getMode()), H::PANEL_WIDTH);
    m_modePanel->getButton()->setObjectName(QStringLiteral("outputPreviewMode"));
    const std::vector<std::tuple<Mode, QString, QString>> modes =
    {
        { Mode::Separations, QString(), tr("The page as the checked inks print it") },
        { Mode::CoverageWarning, tr("Color warnings"), tr("Marks places above the ink coverage limit") },
        { Mode::RichBlackWarning, QString(), tr("Marks black mixed with other inks") },
        { Mode::InkCoverage, tr("Maps"), tr("Heat map of the total ink coverage") },
        { Mode::ShapeChannel, QString(), tr("Where the page is covered by objects") },
        { Mode::OpacityChannel, QString(), tr("How opaque the content is") }
    };
    for (const auto& [mode, caption, description] : modes)
    {
        if (!caption.isEmpty())
        {
            m_modePanel->addGroupCaption(caption);
        }
        PDFTileRow* row = m_modePanel->addRow(getModeTitle(mode), description, getModeIcon(mode), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("outputPreviewMode-") + PDFOutputPreviewAnalyzer::getModeId(mode));
        row->setProperty(H::MODE_PROPERTY, PDFOutputPreviewAnalyzer::getModeId(mode));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIOutputPreview::onModeRowClicked);
    }

    // Content 497:19566: DRAW and the five kinds
    std::vector<PDFAppToolFlagItem> contents =
    {
        { H::name(H::Tool::PARAMETER_IMAGES), tr("Images"), tr("Photos and other bitmap images"), QStringLiteral("image"), QString(), tr("Draw") },
        { H::name(H::Tool::PARAMETER_TEXT), tr("Text"), tr("All text of the page"), QStringLiteral("type"), QString(), QString() },
        { H::name(H::Tool::PARAMETER_VECTORS), tr("Vector Graphics"), tr("Lines, shapes and filled paths"), QStringLiteral("vector-shape"), QString(), QString() },
        { H::name(H::Tool::PARAMETER_SHADINGS), tr("Shading Patterns"), tr("Smooth color gradients"), QStringLiteral("gradient"), QString(), QString() },
        { H::name(H::Tool::PARAMETER_PATTERNS), tr("Tiling Patterns"), tr("Fills made of a repeated tile"), QStringLiteral("pattern"), QString(), QString() }
    };
    PDFAppToolFlagsMenu* contentMenu = builder.addFlagsMenu(std::move(contents), QStringLiteral("filter"), QString(), H::PANEL_WIDTH, QString(), PDFTileRow::Kind::MenuItem);
    if (QToolButton* button = contentMenu ? contentMenu->getButton() : nullptr)
    {
        button->setObjectName(QStringLiteral("outputPreviewContent"));
        button->setText(tr("Content"));
        button->setToolTip(tr("Content"));
    }
    builder.addActionDivider();

    // The switches (owner 17. 11.: labeled, Open = on, as the toggles of Redact)
    m_simulateButton = builder.addAction(tr("Simulate Separations"), QStringLiteral("image-color-mode"), this, &PDFAppToolGUIOutputPreview::onSimulateClicked);
    m_simulateButton->setObjectName(QStringLiteral("outputPreviewSimulate"));
    m_simulateButton->setCheckable(true);
    m_paperButton = builder.addAction(tr("Paper Color"), QStringLiteral("paper-white"), this, &PDFAppToolGUIOutputPreview::onPaperClicked);
    m_paperButton->setObjectName(QStringLiteral("outputPreviewPaperSwitch"));
    m_paperButton->setCheckable(true);

    updateParts();
}

void PDFAppToolGUIOutputPreview::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    // sidebar-left 474:8413: Inks, the table of the inks, the hint, the callout of the spot inks
    m_inksHeader = builder.addPanelHeader(tr("Inks"), QString());
    PDFAppToolCard* card = new PDFAppToolCard(builder.getWidget());
    card->setObjectName(QStringLiteral("outputPreviewInkCard"));
    card->getBodyLayout()->setContentsMargins(1, 1, 1, 1);
    m_inkView = new PDFAppInkTreeView(card);
    m_inkView->setObjectName(QStringLiteral("outputPreviewInks"));
    // ink-table 474:8521 is a table of a card: texts 12, paddings 10, the head without separators (Library row)
    m_inkView->setRowStyle(PDFTreeView::RowStyle::Library);
    m_inkView->setModel(m_inkModel);
    m_inkView->setRootIsDecorated(false);
    m_inkView->setItemsExpandable(false);
    m_inkView->setIndentation(0);
    m_inkView->setSelectionMode(QAbstractItemView::NoSelection);
    m_inkView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inkView->setAccessibleName(tr("Inks"));
    card->getBodyLayout()->addWidget(m_inkView);
    builder.addWidget(card);

    m_inksHint = builder.addHint(QString());
    m_spotCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString());
    m_spotCallout->setObjectName(QStringLiteral("outputPreviewSpotLimit"));
    m_spotCallout->setCompactStyle();
    updateInks();
    updateParts();
}

QWidget* PDFAppToolGUIOutputPreview::createSection(const QString& name, const QString& title, QWidget* parent, PDFAppToolCard** card)
{
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("outputPreviewSection-") + name);
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, PDFAppToolPanelBuilder::SECTION_GAP);
    PDFAppToolSectionHeader* header = new PDFAppToolSectionHeader(title, QString(), section);
    layout->addWidget(header);
    PDFAppToolCard* sectionCard = new PDFAppToolCard(section);
    sectionCard->setFieldSpacing();
    layout->addWidget(sectionCard);
    m_sections[name] = section;
    *card = sectionCard;
    return section;
}

QWidget* PDFAppToolGUIOutputPreview::getSection(const QString& name) const
{
    auto it = m_sections.find(name);
    return it != m_sections.cend() ? it->second.data() : nullptr;
}

void PDFAppToolGUIOutputPreview::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    QWidget* parent = builder.getWidget();
    m_sections.clear();

    // 476:11033: the card of the rendering (the bar runs, the time is not known)
    m_progressCard = new PDFAppToolProgressCard(parent);
    m_progressCard->setObjectName(QStringLiteral("outputPreviewProgress"));
    builder.addWidget(m_progressCard);

    PDFAppToolCard* card = nullptr;

    // section-Under the cursor 474:8640
    builder.addWidget(createSection(QStringLiteral("cursor"), tr("Under the cursor"), parent, &card));
    m_sample = new PDFAppOutputPreviewSample(card);
    card->getBodyLayout()->addWidget(m_sample);

    // section-Preview 474:8649: Separations, Overprint (K7.12), Paper color, Content
    builder.addWidget(createSection(QStringLiteral("preview"), tr("Preview"), parent, &card));
    card->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_V, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_V);
    m_previewRows = new PDFKeyValueList(card);
    m_previewRows->setObjectName(QStringLiteral("outputPreviewPreviewRows"));
    card->getBodyLayout()->addWidget(m_previewRows);

    // Paper color 476:9987
    builder.addWidget(createSection(QStringLiteral("paper"), tr("Paper color"), parent, &card));
    m_paperField = new PDFAppPaperColorField(card);
    connect(m_paperField.data(), &PDFAppPaperColorField::colorSelected, this, &PDFAppToolGUIOutputPreview::onPaperColorSelected);
    card->getBodyLayout()->addWidget(m_paperField);

    // Coverage warning 474:9818: the limit, the alarm color, the areas, the text
    builder.addWidget(createSection(QStringLiteral("coverage"), tr("Coverage warning"), parent, &card));
    if (PDFAppSettingControl* control = builder.createValueControl(H::name(H::Tool::PARAMETER_COVERAGE_LIMIT), card))
    {
        H::preparePercentField(control);
        card->getBodyLayout()->addWidget(PDFAppToolCard::createFieldRow(tr("Coverage limit"), control->getWidget(), card));
    }
    m_coverageAlarm = new PDFAppProofColorField(tr("Alarm color"), card);
    m_coverageAlarm->setObjectName(QStringLiteral("outputPreviewCoverageAlarm"));
    connect(m_coverageAlarm.data(), &PDFAppProofColorField::colorSelected, this, &PDFAppToolGUIOutputPreview::onWarningColorSelected);
    card->getBodyLayout()->addWidget(m_coverageAlarm);
    m_coverageRows = new PDFAppOutputPreviewLimitRows(card);
    m_coverageRows->setObjectName(QStringLiteral("outputPreviewCoverageRows"));
    card->getBodyLayout()->addWidget(m_coverageRows);
    card->getBodyLayout()->addWidget(PDFAppPageWidgets::createText(tr("Places where the sum of all inks is above the limit are painted in the alarm color."),
                                                                   PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, card));

    // Rich black warning 474:10178 (Q7.7: K at least the limit with C, M or Y)
    builder.addWidget(createSection(QStringLiteral("richBlack"), tr("Rich black warning"), parent, &card));
    if (PDFAppSettingControl* control = builder.createValueControl(H::name(H::Tool::PARAMETER_RICH_BLACK_LIMIT), card))
    {
        H::preparePercentField(control);
        card->getBodyLayout()->addWidget(PDFAppToolCard::createFieldRow(tr("Rich black limit"), control->getWidget(), card));
    }
    m_richBlackAlarm = new PDFAppProofColorField(tr("Alarm color"), card);
    m_richBlackAlarm->setObjectName(QStringLiteral("outputPreviewRichBlackAlarm"));
    connect(m_richBlackAlarm.data(), &PDFAppProofColorField::colorSelected, this, &PDFAppToolGUIOutputPreview::onWarningColorSelected);
    card->getBodyLayout()->addWidget(m_richBlackAlarm);
    m_richBlackRows = new PDFAppOutputPreviewLimitRows(card);
    m_richBlackRows->setObjectName(QStringLiteral("outputPreviewRichBlackRows"));
    card->getBodyLayout()->addWidget(m_richBlackRows);
    card->getBodyLayout()->addWidget(PDFAppPageWidgets::createText(tr("Places where black reaches the limit and cyan, magenta or yellow is also present are painted in the alarm color."),
                                                                   PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, card));

    // Distribution 474:10536
    builder.addWidget(createSection(QStringLiteral("distribution"), tr("Distribution"), parent, &card));
    m_legend = new PDFAppOutputPreviewLegend(card);
    card->getBodyLayout()->addWidget(m_legend);
    card->getBodyLayout()->addWidget(PDFAppPageWidgets::createText(tr("The colors run from no ink to the highest coverage found on the page."),
                                                                   PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, card));

    // Shape channel 474:10896, Opacity channel 476:9651
    builder.addWidget(createSection(QStringLiteral("channel"), QString(), parent, &card));
    if (QWidget* section = getSection(QStringLiteral("channel")))
    {
        m_channelTitle = section->findChild<PDFAppToolSectionHeader*>()->getTitleLabel();
    }
    m_channelText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, card);
    m_channelText->setObjectName(QStringLiteral("outputPreviewChannelText"));
    card->getBodyLayout()->addWidget(m_channelText);

    updateParts();
}

QWidget* PDFAppToolGUIOutputPreview::createCanvasPage(QWidget* parent)
{
    // updateParts announces the status text (a foreign slot may delete this GUI): the page is held here
    QPointer<PDFAppOutputPreviewCanvas> canvas = new PDFAppOutputPreviewCanvas(parent);
    m_canvas = canvas;
    connect(m_canvas.data(), &PDFAppOutputPreviewCanvas::pixelHovered, this, &PDFAppToolGUIOutputPreview::onPixelHovered);
    connect(m_canvas.data(), &PDFAppOutputPreviewCanvas::pageStepRequested, this, &PDFAppToolGUIOutputPreview::onPageStepRequested);
    connect(m_canvas.data(), &PDFAppOutputPreviewCanvas::imageRectChanged, this, &PDFAppToolGUIOutputPreview::onImageRectChanged);
    PDFAppDocumentView* view = getView();
    if (view && m_connectedView != view)
    {
        PDFDeferredDeletion::connect(view, &PDFAppDocumentView::displayedPageChanged, this, &PDFAppToolGUIOutputPreview::onDisplayedPageChanged);
        m_connectedView = view;
    }

    // The first picture (the result of before stays, when the parts are created again)
    if (!m_result && !isRendering())
    {
        scheduleRender();
    }
    updateImage();
    updateParts();
    return canvas.data();
}

// ------------------------------------------------------------------ the job

void PDFAppToolGUIOutputPreview::scheduleRender()
{
    m_renderTimer->start();
}

bool PDFAppToolGUIOutputPreview::isRenderingPending() const
{
    return isRendering() || (m_renderTimer && m_renderTimer->isActive());
}

void PDFAppToolGUIOutputPreview::onRenderTimeout()
{
    startRender();
}

void PDFAppToolGUIOutputPreview::cancelRender()
{
    if (m_control)
    {
        m_control->cancel();
        m_control.reset();
    }
    if (m_watcher)
    {
        // The late result is dropped: the watcher goes, the job keeps only its copies
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
}

void PDFAppToolGUIOutputPreview::startRender()
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    // A new request cancels the old one (plan 6.3)
    cancelRender();

    const PDFAppDocument* document = getDocument();
    const PDFAppToolParameterModel* model = getModel();
    PDFAppToolHost* host = getHost();
    const PDFInteger pageIndex = getPageIndex();
    if (!document || !document->getDocument() || !model || !host || pageIndex < 0)
    {
        updateParts();
        return;
    }

    // The settings of the parameters, the color management and the rendering of the application (a copy;
    // the proof of the tab never reaches it, Q7.10), the layers and annotations as printing (Q7.8 a)
    PDFAppOutputPreviewRenderTask task;
    task.document = document->getDocument();
    task.pageIndex = pageIndex;
    task.settings = H::Tool::createSettings(model->getValues());
    const PDFOutputPreviewSettings base = PDFOutputPreviewSettings::create(task.document.data(), host->createEnvironment().rendering);
    task.settings.cmsSettings = base.cmsSettings.withoutProofing();
    task.settings.rendering = base.rendering;
    task.control = std::make_shared<PDFAppOutputPreviewControl>();

    m_control = task.control;
    m_renderingPage = pageIndex;
    m_settings = task.settings;
    QFutureWatcher<PDFAppOutputPreviewResultPointer>* watcher = new QFutureWatcher<PDFAppOutputPreviewResultPointer>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcherBase::finished, this, &PDFAppToolGUIOutputPreview::onRenderFinished);
    watcher->setFuture(QtConcurrent::run(task));
    updateParts();
}

void PDFAppToolGUIOutputPreview::onRenderFinished()
{
    QFutureWatcher<PDFAppOutputPreviewResultPointer>* watcher = static_cast<QFutureWatcher<PDFAppOutputPreviewResultPointer>*>(m_watcher.data());
    if (!watcher || sender() != watcher)
    {
        return;
    }

    PDFAppOutputPreviewResultPointer result = watcher->result();
    m_watcher = nullptr;
    m_control.reset();
    watcher->deleteLater();
    if (!result || result->isCanceled)
    {
        updateParts();
        return;
    }

    m_result = std::move(result);
    m_cursorValues.clear();
    updateInks();
    updateImage();
    updateCursor();
    updateParts();
}

void PDFAppToolGUIOutputPreview::updateImage()
{
    m_image = m_result && m_result->isRendered() ? PDFOutputPreviewAnalyzer::createImage(*m_result, getMode(), m_settings, m_warningColor) : QImage();
    if (m_canvas)
    {
        m_canvas->setImage(m_image);
    }
}

// ------------------------------------------------------------------ the inks

QStringList PDFAppToolGUIOutputPreview::getInactiveInks() const
{
    using H = PDFAppToolGUIOutputPreviewHelper;
    const PDFAppToolParameterModel* model = getModel();
    const QString value = model ? model->getValue(H::name(H::Tool::PARAMETER_INACTIVE_INKS)).toString() : QString();
    QStringList inks;
    for (const QString& ink : value.split(QLatin1Char(',')))
    {
        if (!ink.trimmed().isEmpty())
        {
            inks << ink.trimmed();
        }
    }
    return inks;
}

void PDFAppToolGUIOutputPreview::updateInks()
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    if (!m_inkModel)
    {
        return;
    }

    // The model is built again for every result: the groups Process Inks and Spot Inks with the three states
    // of their check boxes (K7.18, Checkbox Checked=Mixed); the values of the columns follow updateCursor
    const QSignalBlocker blocker(m_inkModel.data());
    m_isUpdatingInks = true;
    m_inkModel->clear();
    m_inkModel->setHorizontalHeaderLabels({ tr("Ink"), tr("At cursor"), tr("Page mm²") });
    m_inkModel->horizontalHeaderItem(H::CURSOR_COLUMN)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_inkModel->horizontalHeaderItem(H::AREA_COLUMN)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    const QStringList inactive = getInactiveInks();
    const bool isSimulated = m_settings.isSeparationSimulated;
    QStandardItem* groups[2] = { nullptr, nullptr };
    if (m_result)
    {
        for (size_t i = 0; i < m_result->inks.size(); ++i)
        {
            const PDFOutputPreviewInk& ink = m_result->inks[i];
            const int groupIndex = ink.isSpot ? 1 : 0;
            if (ink.isSpot && !isSimulated)
            {
                // 476:10352: without the simulation the spot inks are process colors
                continue;
            }
            if (!groups[groupIndex])
            {
                // The state of the group follows its inks (onInkItemChanged), a click on it sets all of them
                QStandardItem* group = new QStandardItem(ink.isSpot ? tr("Spot Inks") : tr("Process Inks"));
                group->setCheckable(true);
                group->setEditable(false);
                QFont font = PDFThemeFonts::getFont(QFont::DemiBold, 12);
                group->setFont(font);
                QStandardItem* cursorCell = new QStandardItem();
                cursorCell->setEditable(false);
                QStandardItem* areaCell = new QStandardItem();
                areaCell->setEditable(false);
                m_inkModel->appendRow({ group, cursorCell, areaCell });
                groups[groupIndex] = group;
            }

            const QString name = QString::fromLatin1(ink.name);
            QStandardItem* item = new QStandardItem(H::createSwatchIcon(ink.color), ink.displayName.isEmpty() ? name : ink.displayName);
            item->setEditable(false);
            item->setCheckable(true);
            item->setData(name, H::INK_NAME_ROLE);
            item->setData(qint64(i), H::INK_INDEX_ROLE);
            item->setCheckState(inactive.contains(name) || !ink.isActive ? Qt::Unchecked : Qt::Checked);
            // RS7APP L8: a screen reader says what the swatch and the group show (the kind of the ink)
            QString description = ink.isSpot ? tr("Spot ink") : tr("Process ink");
            if (ink.channel < 0)
            {
                // 476:11703: over the limit of the spot inks, converted to process colors
                item->setEnabled(false);
                item->setToolTip(tr("Converted to process colors: only 26 spot inks can be shown as plates."));
                description = tr("%1, converted to process colors", "Accessible description of a spot ink over the limit of the plates").arg(description);
            }
            item->setData(description, Qt::AccessibleDescriptionRole);
            QStandardItem* cursorCell = new QStandardItem();
            cursorCell->setEditable(false);
            cursorCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            QStandardItem* areaCell = new QStandardItem(H::formatNumber(ink.area, 2));
            areaCell->setEditable(false);
            areaCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            groups[groupIndex]->appendRow({ item, cursorCell, areaCell });
        }
    }

    // The groups follow their inks
    for (QStandardItem* group : groups)
    {
        H::updateGroupState(group);
    }
    m_isUpdatingInks = false;

    if (m_inkView)
    {
        const bool isChannel = H::isChannelMode(getMode());
        m_inkView->setColumnHidden(H::CURSOR_COLUMN, isChannel);
        m_inkView->setColumnHidden(H::AREA_COLUMN, isChannel);
        m_inkView->reset();
        m_inkView->expandAll();

        // ink-table 474:8521: the cells At cursor 50 and Page mm² 64 wide (+ the gap 8), never narrower than their
        // head or values (no elided "At c…"); the name takes the rest. The sections exist only since the model has
        // its columns (clear() above), so their modes and widths are set here, after the reset of the view.
        QHeaderView* header = m_inkView->header();
        header->setStretchLastSection(false);
        header->setSectionResizeMode(QHeaderView::Fixed);
        if (header->count() > H::AREA_COLUMN)
        {
            header->setSectionResizeMode(H::INK_COLUMN, QHeaderView::Stretch);
            const QAbstractItemView* view = m_inkView.data();   // sizeHintForColumn is public in the base class only
            const int cursorWidth = qMax(qMax(58, header->sectionSizeHint(H::CURSOR_COLUMN)), view->sizeHintForColumn(H::CURSOR_COLUMN));
            const int areaWidth = qMax(qMax(72, header->sectionSizeHint(H::AREA_COLUMN)), view->sizeHintForColumn(H::AREA_COLUMN));
            header->resizeSection(H::CURSOR_COLUMN, cursorWidth);
            header->resizeSection(H::AREA_COLUMN, areaWidth);
        }
    }
}

void PDFAppToolGUIOutputPreview::onInkItemChanged(QStandardItem* item)
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    if (m_isUpdatingInks || !item || !item->isCheckable())
    {
        return;
    }

    // A check of the user (owner 2026-11-20, isInteractive)
    markUsed();

    // K7.18: a group sets all its inks, an ink sets the state of its group (Checked, Unchecked, Mixed)
    {
        const QSignalBlocker blocker(m_inkModel.data());
        m_isUpdatingInks = true;
        if (!item->parent())
        {
            const Qt::CheckState state = item->checkState() == Qt::Unchecked ? Qt::Unchecked : Qt::Checked;
            for (int row = 0; row < item->rowCount(); ++row)
            {
                QStandardItem* child = item->child(row, H::INK_COLUMN);
                if (child && child->isEnabled())
                {
                    child->setCheckState(state);
                }
            }
            H::updateGroupState(item);
        }
        else
        {
            H::updateGroupState(item->parent());
        }
        m_isUpdatingInks = false;
    }
    if (m_inkView)
    {
        m_inkView->viewport()->update();
    }

    // The unchecked inks into the parameter (one undo step; the plates are rendered again)
    QStringList inactive;
    for (int groupRow = 0; groupRow < m_inkModel->rowCount(); ++groupRow)
    {
        const QStandardItem* group = m_inkModel->item(groupRow, H::INK_COLUMN);
        for (int row = 0; group && row < group->rowCount(); ++row)
        {
            const QStandardItem* ink = group->child(row, H::INK_COLUMN);
            if (ink && ink->isEnabled() && ink->checkState() == Qt::Unchecked)
            {
                inactive << ink->data(H::INK_NAME_ROLE).toString();
            }
        }
    }

    const QString value = inactive.join(QLatin1Char(','));
    PDFAppToolParameterModel* model = getModel();
    if (model && model->getValue(H::name(H::Tool::PARAMETER_INACTIVE_INKS)).toString() != value)
    {
        model->setValue(H::name(H::Tool::PARAMETER_INACTIVE_INKS), value);
    }
}

QString PDFAppToolGUIOutputPreview::getSinglePlateName() const
{
    // 474:9162: exactly one checked ink - "Cyan plate"
    if (!m_result || !m_settings.isSeparationSimulated)
    {
        return QString();
    }

    const QStringList inactive = getInactiveInks();
    QString single;
    int checked = 0;
    for (const PDFOutputPreviewInk& ink : m_result->inks)
    {
        if (ink.channel >= 0 && !inactive.contains(QString::fromLatin1(ink.name)))
        {
            single = ink.displayName.isEmpty() ? QString::fromLatin1(ink.name) : ink.displayName;
            ++checked;
        }
    }
    return checked == 1 ? single : QString();
}

// ------------------------------------------------------------------ the cursor

void PDFAppToolGUIOutputPreview::setHoveredPixel(const QPoint& pixel)
{
    m_hoveredPixel = pixel;
    updateCursor();
    updateParts();
}

void PDFAppToolGUIOutputPreview::onPixelHovered(const QPoint& pixel)
{
    setHoveredPixel(pixel);
}

void PDFAppToolGUIOutputPreview::updateCursor()
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    // The tints under the pointer from the stored bitmap (no new rendering)
    m_cursorValues = m_result && m_hoveredPixel.x() >= 0 ? PDFOutputPreviewAnalyzer::getValuesAt(*m_result, m_hoveredPixel) : std::vector<double>();
    if (!m_inkModel)
    {
        return;
    }

    const QSignalBlocker blocker(m_inkModel.data());
    for (int groupRow = 0; groupRow < m_inkModel->rowCount(); ++groupRow)
    {
        const QStandardItem* group = m_inkModel->item(groupRow, H::INK_COLUMN);
        for (int row = 0; group && row < group->rowCount(); ++row)
        {
            const QStandardItem* ink = group->child(row, H::INK_COLUMN);
            QStandardItem* cell = group->child(row, H::CURSOR_COLUMN);
            if (!ink || !cell)
            {
                continue;
            }
            const size_t index = size_t(ink->data(H::INK_INDEX_ROLE).toLongLong());
            cell->setText(index < m_cursorValues.size() ? H::formatPercent(std::round(m_cursorValues[index]), 0) : QString());
        }
    }
    if (m_inkView)
    {
        m_inkView->viewport()->update();
    }
}

// ------------------------------------------------------------------ the changes

void PDFAppToolGUIOutputPreview::onModeRowClicked()
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    QObject* row = sender();
    PDFAppToolParameterModel* model = getModel();
    if (!row || !model)
    {
        return;
    }
    const QString id = row->property(H::MODE_PROPERTY).toString();
    if (m_modePanel)
    {
        m_modePanel->close();
    }
    model->setValue(H::name(H::Tool::PARAMETER_MODE), id);
}

void PDFAppToolGUIOutputPreview::onSimulateClicked()
{
    using H = PDFAppToolGUIOutputPreviewHelper;
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(H::name(H::Tool::PARAMETER_SIMULATE_SEPARATIONS), m_simulateButton && m_simulateButton->isChecked());
    }
}

void PDFAppToolGUIOutputPreview::onPaperClicked()
{
    using H = PDFAppToolGUIOutputPreviewHelper;
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(H::name(H::Tool::PARAMETER_SIMULATE_PAPER_COLOR), m_paperButton && m_paperButton->isChecked());
    }
}

void PDFAppToolGUIOutputPreview::onPaperColorSelected(const QColor& color)
{
    using H = PDFAppToolGUIOutputPreviewHelper;
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(H::name(H::Tool::PARAMETER_PAPER_COLOR), color);
    }
}

void PDFAppToolGUIOutputPreview::onWarningColorSelected(const QColor& color)
{
    // No parameter: an action of the user (owner 2026-11-20, isInteractive)
    markUsed();

    // The color of the warnings is drawn into the picture of the mode only (no new rendering)
    m_warningColor = color;
    updateImage();
    updateParts();
}

void PDFAppToolGUIOutputPreview::onParametersChanged(const QString& name)
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    BaseClass::onParametersChanged(name);
    if (name == H::name(H::Tool::PARAMETER_MODE))
    {
        // A mode is a picture of the stored result
        updateInks();
        updateCursor();
        updateImage();
        updateParts();
        return;
    }

    scheduleRender();
    updateParts();
}

void PDFAppToolGUIOutputPreview::onDocumentChanged()
{
    BaseClass::onDocumentChanged();
    scheduleRender();
}

void PDFAppToolGUIOutputPreview::onDisplayedPageChanged(PDFInteger pageIndex)
{
    Q_UNUSED(pageIndex);
    if (!m_result || m_result->pageIndex != getPageIndex())
    {
        scheduleRender();
    }
}

void PDFAppToolGUIOutputPreview::onPageStepRequested(int step)
{
    // The navigation is the last call (the view emits its page)
    PDFAppDocumentView* view = getView();
    const PDFInteger pageIndex = getPageIndex();
    if (view && pageIndex >= 0)
    {
        view->goToPage(pageIndex + step);
    }
}

// ------------------------------------------------------------------ texts

QString PDFAppToolGUIOutputPreview::getOwnTabPageText() const
{
    // 474:8423 "Page 3 of 16"
    const PDFAppDocument* document = getDocument();
    const PDFInteger pageIndex = getPageIndex();
    if (!document || !document->getDocument() || pageIndex < 0)
    {
        return QString();
    }
    return tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(pageIndex + 1), PDFAppLocale::formatNumber(qint64(document->getDocument()->getCatalog()->getPageCount())));
}

QString PDFAppToolGUIOutputPreview::getOwnTabZoomText() const
{
    // 474:8386 "151 %" (K7.27): the scale of the shown picture, in the units of the zoom of the view
    // (100 % = the page in its size on the screen, PDFDrawWidgetProxy::getPixelPerMM); the view behind
    // the canvas has its own zoom, which this tool does not show. No picture = no item.
    const PDFAppDocument* document = getDocument();
    const PDFAppOutputPreviewPicture* picture = m_canvas ? m_canvas->getPicture() : nullptr;
    const PDFAppDocumentView* view = getView();
    const PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    if (!document || !document->getDocument() || !picture || !proxy || !m_result || !m_result->isRendered())
    {
        return QString();
    }

    // The picture may come from a version of the document with more pages (getPage throws out of range)
    const PDFCatalog* catalog = document->getDocument()->getCatalog();
    if (m_result->pageIndex < 0 || size_t(m_result->pageIndex) >= catalog->getPageCount())
    {
        return QString();
    }
    const PDFPage* page = catalog->getPage(size_t(m_result->pageIndex));
    const QRect imageRect = picture->getImageRect();
    const PDFReal pageWidth = page ? page->getRotatedMediaBoxMM().width() * proxy->getPixelPerMM() : 0.0;
    if (imageRect.isEmpty() || !(pageWidth > 0.0))
    {
        return QString();
    }
    return tr("%1 %").arg(PDFAppLocale::formatNumber(qRound(imageRect.width() / pageWidth * 100.0)));
}

void PDFAppToolGUIOutputPreview::onImageRectChanged()
{
    // The zoom item follows the size of the picture (the last call: a slot may delete this GUI)
    updateStatusText();
}

QString PDFAppToolGUIOutputPreview::getCancelText() const
{
    // 474:8386 … 476:11703: only Close (plan 2.3) - the preview changes nothing in the document
    return tr("Close");
}

QString PDFAppToolGUIOutputPreview::getStatusText() const
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    const QString page = PDFAppLocale::formatNumber(getPageIndex() + 1);
    if (isRendering())
    {
        return tr("Rendering page %1…").arg(PDFAppLocale::formatNumber(m_renderingPage + 1));
    }
    if (!m_result)
    {
        return QString();
    }
    if (!m_result->isRendered())
    {
        return tr("Page %1 cannot be rendered.").arg(page);
    }
    if (!m_result->renderingIssues.empty())
    {
        return tr("Page %1 was rendered with %Ln errors.", "", int(m_result->renderingIssues.size())).arg(page);
    }

    const Mode mode = getMode();
    if (m_result->isSpotLimitReached && mode == Mode::Separations)
    {
        int spotCount = 0;
        int shownCount = 0;
        for (const PDFOutputPreviewInk& ink : m_result->inks)
        {
            spotCount += ink.isSpot ? 1 : 0;
            shownCount += ink.isSpot && ink.channel >= 0 ? 1 : 0;
        }
        return tr("The document uses %1 spot inks, %2 of them can be shown as plates.").arg(PDFAppLocale::formatNumber(spotCount), PDFAppLocale::formatNumber(shownCount));
    }

    switch (mode)
    {
        case Mode::Separations:
        {
            if (!m_settings.isSeparationSimulated)
            {
                return tr("Separations are not simulated – spot inks are shown as process colors.");
            }
            const QString plate = getSinglePlateName();
            if (!plate.isEmpty())
            {
                return tr("Only the %1 plate is shown.").arg(plate);
            }
            if (m_settings.isPaperColorSimulated)
            {
                return tr("The preview simulates the color of the paper.");
            }
            return tr("Separations of page %1. Move the pointer over the page to read the ink values.").arg(page);
        }
        case Mode::CoverageWarning:
            return tr("%1 of page %2 is above the coverage limit of %3.").arg(H::formatArea(m_result->areaOverCoverageLimit), page, H::formatPercent(m_settings.coverageLimit, 0));
        case Mode::RichBlackWarning:
            return tr("%1 of page %2 is rich black: black at %3 or more with cyan, magenta or yellow.").arg(H::formatArea(m_result->richBlackArea), page, H::formatPercent(m_settings.richBlackLimit, 0));
        case Mode::InkCoverage:
            return tr("Total ink coverage of page %1, highest value %2.").arg(page, H::formatPercent(std::round(m_result->maximumCoverage), 0));
        case Mode::ShapeChannel:
            return tr("Shape channel of page %1.").arg(page);
        case Mode::OpacityChannel:
            return tr("Opacity channel of page %1.").arg(page);
    }
    return QString();
}

void PDFAppToolGUIOutputPreview::updateParts()
{
    using H = PDFAppToolGUIOutputPreviewHelper;

    const Mode mode = getMode();
    const PDFAppToolParameterModel* model = getModel();
    const bool isSimulated = model && model->getValue(H::name(H::Tool::PARAMETER_SIMULATE_SEPARATIONS)).toBool();
    const bool isPaper = model && model->getValue(H::name(H::Tool::PARAMETER_SIMULATE_PAPER_COLOR)).toBool();
    QColor paperColor = model ? model->getValue(H::name(H::Tool::PARAMETER_PAPER_COLOR)).value<QColor>() : QColor();
    paperColor = paperColor.isValid() ? paperColor : QColor(Qt::white);
    const bool isRendering = this->isRendering();
    const PDFInteger pageIndex = getPageIndex();
    const QString page = PDFAppLocale::formatNumber(pageIndex + 1);

    // The toolbar
    if (m_modePanel)
    {
        QToolButton* button = m_modePanel->getButton();
        button->setText(getModeTitle(mode));
        button->setToolTip(getModeTitle(mode));
        button->setIcon(PDFThemeIcons::getIcon(getModeIcon(mode)));
        const QString modeId = PDFOutputPreviewAnalyzer::getModeId(mode);
        for (const QPointer<PDFTileRow>& row : m_modePanel->getRows())
        {
            if (row)
            {
                const QSignalBlocker blocker(row.data());
                row->setChecked(row->property(H::MODE_PROPERTY).toString() == modeId);
            }
        }
    }
    if (m_simulateButton)
    {
        m_simulateButton->setChecked(isSimulated);
    }
    if (m_paperButton)
    {
        m_paperButton->setChecked(isPaper);
    }

    // The left panel: "4 process · 2 spot", "4 process · 31 spot (26 shown)"
    int processCount = 0;
    int spotCount = 0;
    int shownSpotCount = 0;
    if (m_result)
    {
        for (const PDFOutputPreviewInk& ink : m_result->inks)
        {
            processCount += ink.isSpot ? 0 : 1;
            spotCount += ink.isSpot ? 1 : 0;
            shownSpotCount += ink.isSpot && ink.channel >= 0 ? 1 : 0;
        }
    }
    if (m_inksHeader)
    {
        QString subtitle = tr("%1 process").arg(PDFAppLocale::formatNumber(processCount));
        if (isSimulated && spotCount > 0)
        {
            subtitle = (shownSpotCount < spotCount) ? tr("%1 process · %2 spot (%3 shown)").arg(PDFAppLocale::formatNumber(processCount), PDFAppLocale::formatNumber(spotCount), PDFAppLocale::formatNumber(shownSpotCount))
                                                    : tr("%1 process · %2 spot").arg(PDFAppLocale::formatNumber(processCount), PDFAppLocale::formatNumber(spotCount));
        }
        m_inksHeader->setTexts(tr("Inks"), m_result ? subtitle : QString());
    }
    if (m_inksHint)
    {
        m_inksHint->setText(isSimulated ? tr("Uncheck an ink to leave its plate out of the preview. At cursor shows the value under the mouse pointer, Page mm² the ink area weighted by tint.")
                                        : tr("Spot inks are not simulated. Turn Simulate Separations on to see them as separate plates."));
    }
    if (m_spotCallout)
    {
        const bool isOverLimit = isSimulated && m_result && m_result->isSpotLimitReached;
        m_spotCallout->setTexts(tr("Only %1 spot inks can be shown").arg(PDFAppLocale::formatNumber(shownSpotCount)),
                                tr("The document uses %1 spot inks. The remaining %2 are converted to process colors.").arg(PDFAppLocale::formatNumber(spotCount), PDFAppLocale::formatNumber(spotCount - shownSpotCount)));
        m_spotCallout->setVisible(isOverLimit);
    }

    // The canvas: the title, the rendering, the errors
    if (m_canvas)
    {
        QString title = pageIndex >= 0 ? tr("Page %1 · %2").arg(page, getModeTitle(mode)) : QString();
        const QString plate = mode == Mode::Separations ? getSinglePlateName() : QString();
        if (!plate.isEmpty())
        {
            title = tr("%1 · %2 plate", "474:9162: Page 3 · Separations · Cyan plate").arg(title, plate);
        }
        m_canvas->setTitle(title);
        m_canvas->setRenderingText(isRendering ? tr("Rendering page %1…").arg(PDFAppLocale::formatNumber(m_renderingPage + 1)) : QString());

        QString errorTitle;
        QString errorText;
        if (m_result && !m_result->isRendered())
        {
            errorTitle = tr("Page %1 cannot be rendered").arg(PDFAppLocale::formatNumber(m_result->pageIndex + 1));
            errorText = m_result->errorMessage;
        }
        else if (m_result && !m_result->renderingIssues.empty())
        {
            // 476:11358: the messages and that the preview may differ from the printed page
            QStringList messages;
            for (const PDFRenderError& error : m_result->renderingIssues)
            {
                if (messages.size() < 4 && !messages.contains(error.message))
                {
                    messages << error.message;
                }
            }
            errorTitle = tr("Page %1 was rendered with %Ln errors", "", int(m_result->renderingIssues.size())).arg(PDFAppLocale::formatNumber(m_result->pageIndex + 1));
            errorText = tr("%1 The preview may differ from the printed page.").arg(messages.join(QLatin1Char(' ')));
        }
        m_canvas->setError(errorTitle, errorText);
    }

    // The right panel: the card of the rendering alone while it runs (476:11033), else the sections of the mode
    if (m_progressCard)
    {
        m_progressCard->setVisible(isRendering);
        if (isRendering)
        {
            m_progressCard->setProgress(0, tr("Rendering page %1").arg(PDFAppLocale::formatNumber(m_renderingPage + 1)), tr("Separations are computed in the background"), QString());
            if (QProgressBar* bar = m_progressCard->getBar())
            {
                bar->setRange(0, 0);
            }
        }
    }

    const bool hasResult = !isRendering && m_result && m_result->isRendered();
    const bool isChannel = H::isChannelMode(mode);
    const std::map<QString, bool> visible =
    {
        { QStringLiteral("cursor"), hasResult && !isChannel },
        { QStringLiteral("preview"), hasResult && mode == Mode::Separations && !isPaper },
        { QStringLiteral("paper"), hasResult && mode == Mode::Separations && isPaper },
        { QStringLiteral("coverage"), hasResult && mode == Mode::CoverageWarning },
        { QStringLiteral("richBlack"), hasResult && mode == Mode::RichBlackWarning },
        { QStringLiteral("distribution"), hasResult && mode == Mode::InkCoverage },
        { QStringLiteral("channel"), hasResult && isChannel }
    };
    for (const auto& [name, isVisible] : visible)
    {
        if (QWidget* section = getSection(name))
        {
            section->setVisible(isVisible);
        }
    }

    if (m_sample)
    {
        // The color of the page under the pointer (the separations on the paper) and the sum of the checked inks
        QColor color;
        double coverage = 0.0;
        if (m_result && m_hoveredPixel.x() >= 0 && m_result->image.rect().contains(m_hoveredPixel))
        {
            color = m_result->image.pixelColor(m_hoveredPixel);
            const QStringList inactive = getInactiveInks();
            for (size_t i = 0; i < m_result->inks.size() && i < m_cursorValues.size(); ++i)
            {
                if (!inactive.contains(QString::fromLatin1(m_result->inks[i].name)))
                {
                    coverage += m_cursorValues[i];
                }
            }
        }
        m_sample->setSample(color, color.isValid() ? tr("Total coverage %1").arg(H::formatPercent(std::round(coverage), 0)) : tr("Move the pointer over the page"));
    }

    if (m_previewRows)
    {
        // K7.12: overprint is always simulated (as printing)
        int contentCount = 0;
        for (const char* parameter : { H::Tool::PARAMETER_IMAGES, H::Tool::PARAMETER_TEXT, H::Tool::PARAMETER_VECTORS, H::Tool::PARAMETER_SHADINGS, H::Tool::PARAMETER_PATTERNS })
        {
            contentCount += model && model->getValue(H::name(parameter)).toBool() ? 1 : 0;
        }
        m_previewRows->clear();
        m_previewRows->addRow(tr("Separations"), isSimulated ? tr("Simulated") : tr("Not simulated"));
        m_previewRows->addRow(tr("Overprint"), tr("Simulated"));
        m_previewRows->addRow(tr("Paper color"), isPaper ? PDFAppPaperColorField::getColorName(paperColor) : tr("White"));
        m_previewRows->addRow(tr("Content"), contentCount == 5 ? tr("Everything") : tr("%1 of %2 kinds").arg(PDFAppLocale::formatNumber(contentCount), PDFAppLocale::formatNumber(5)));
    }
    if (m_paperField)
    {
        m_paperField->setColor(paperColor);
    }
    if (m_coverageAlarm)
    {
        m_coverageAlarm->setColor(m_warningColor);
    }
    if (m_richBlackAlarm)
    {
        m_richBlackAlarm->setColor(m_warningColor);
    }

    const double mediaArea = m_result ? m_result->mediaArea : 0.0;
    if (m_coverageRows)
    {
        m_coverageRows->clear();
        if (m_result)
        {
            m_coverageRows->setValues(H::formatArea(qMax(0.0, mediaArea - m_result->areaOverCoverageLimit)), H::formatArea(m_result->areaOverCoverageLimit), m_warningColor);
        }
    }
    if (m_richBlackRows)
    {
        m_richBlackRows->clear();
        if (m_result)
        {
            m_richBlackRows->setValues(H::formatArea(qMax(0.0, mediaArea - m_result->richBlackArea)), H::formatArea(m_result->richBlackArea), m_warningColor);
        }
    }
    if (m_legend)
    {
        double cursorCoverage = -1.0;
        if (!m_cursorValues.empty() && m_result)
        {
            cursorCoverage = 0.0;
            const QStringList inactive = getInactiveInks();
            for (size_t i = 0; i < m_result->inks.size() && i < m_cursorValues.size(); ++i)
            {
                cursorCoverage += inactive.contains(QString::fromLatin1(m_result->inks[i].name)) ? 0.0 : m_cursorValues[i];
            }
        }
        m_legend->setValues(m_result ? m_result->maximumCoverage : 0.0, cursorCoverage);
    }
    if (m_channelTitle && m_channelText)
    {
        m_channelTitle->setText(mode == Mode::OpacityChannel ? tr("Opacity channel") : tr("Shape channel"));
        m_channelText->setText(mode == Mode::OpacityChannel ? tr("White is fully opaque content, darker shades are more transparent, black is the empty paper.")
                                                            : tr("White marks the places covered by any object of the page, black the untouched paper."));
    }

    updateStatusText();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_outputpreview.cpp"
