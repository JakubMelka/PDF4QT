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

#include "pdfapptoolpanelparts.h"

#include "pdfapppagelayout.h"

#include "pdfcomponentpainter.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"
#include "pdfwidgetstyle.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace pdf
{

PDFAppToolSidePanel::PDFAppToolSidePanel(Side side, int spacing, QWidget* parent) :
    BaseClass(parent),
    m_side(side)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("toolPanelScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    PDFWidgetStyleProperties::set(m_scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    layout->addWidget(m_scrollArea);

    m_body = new QWidget(m_scrollArea);
    m_body->setObjectName(QStringLiteral("toolPanelBody"));
    m_body->setAutoFillBackground(false);
    m_bodyLayout = new QVBoxLayout(m_body);
    if (side == Side::Right)
    {
        m_bodyLayout->setContentsMargins(RIGHT_PADDING_LEADING, RIGHT_PADDING_TOP, RIGHT_PADDING_TRAILING, RIGHT_PADDING_BOTTOM);
    }
    else
    {
        m_bodyLayout->setContentsMargins(LEFT_PADDING_LEADING, LEFT_PADDING_TOP, LEFT_PADDING_TRAILING, LEFT_PADDING_BOTTOM);
    }
    m_bodyLayout->setSpacing(spacing);
    m_bodyLayout->addStretch(1);
    m_scrollArea->setWidget(m_body);
}

PDFAppToolSidePanel::~PDFAppToolSidePanel() = default;

void PDFAppToolSidePanel::setContent(QWidget* content)
{
    if (m_content)
    {
        // The old content may be the sender of the change: it goes from the event loop
        m_bodyLayout->removeWidget(m_content);
        m_content->hide();
        m_content->deleteLater();
    }

    m_content = content;
    const bool isFilling = content && content->sizePolicy().verticalStretch() > 0;
    if (content)
    {
        // Above the stretch at the end; a card of the progress lies above the content
        m_bodyLayout->insertWidget(m_bodyLayout->count() - 1, content, isFilling ? 1 : 0);
    }

    // A content with a list of its own (the pending changes, the links) fills the panel
    m_bodyLayout->setStretch(m_bodyLayout->count() - 1, isFilling ? 0 : 1);
}

void PDFAppToolSidePanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/sidebar with the line border/default towards the canvas
    QPainter painter(this);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.fillRect(rect(), manager->getColor(PDFThemeColors::BgSidebar));
    const bool isLeadingLine = m_side == Side::Right;
    const bool isLeftLine = isLeadingLine != (layoutDirection() == Qt::RightToLeft);
    painter.fillRect(QRect(isLeftLine ? 0 : width() - 1, 0, 1, height()), manager->getColor(PDFThemeColors::BorderDefault));
}

PDFAppToolProgressCard::PDFAppToolProgressCard(QWidget* parent) :
    BaseClass(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    m_title = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::DemiBold, 15), 18, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("toolProgressTitle"));
    layout->addWidget(m_title);

    m_bar = new QProgressBar(this);
    m_bar->setObjectName(QStringLiteral("toolProgressBar"));
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(4);
    layout->addWidget(m_bar);

    m_step = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, this);
    m_step->setObjectName(QStringLiteral("toolProgressStep"));
    m_step->setMinimumWidth(1);
    layout->addWidget(m_step);

    m_time = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, this);
    m_time->setObjectName(QStringLiteral("toolProgressTime"));
    layout->addWidget(m_time);

    setProgress(0, QString(), QString(), QString());
}

PDFAppToolProgressCard::~PDFAppToolProgressCard() = default;

void PDFAppToolProgressCard::setProgress(int percentage, const QString& title, const QString& stepText, const QString& timeText)
{
    m_title->setText(title);
    m_bar->setValue(qBound(0, percentage, 100));
    m_step->setText(stepText);
    m_step->setVisible(!stepText.isEmpty());
    m_time->setText(timeText);
    m_time->setVisible(!timeText.isEmpty());
}

void PDFAppToolProgressCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    painter.setBrush(manager->getColor(PDFThemeColors::BgSurface));
    painter.setPen(QPen(manager->getColor(PDFThemeColors::BorderDefault), 1.0));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), RADIUS - 0.5, RADIUS - 0.5);
}

PDFAppToolCallout::PDFAppToolCallout(PDFAccentHue hue, const QString& iconName, const QString& title, const QString& text, QWidget* parent) :
    BaseClass(hue, parent)
{
    setRadius(RADIUS);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(PADDING_H, PADDING_TOP, PADDING_H, PADDING_BOTTOM);
    m_layout->setSpacing(GAP);

    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(GAP);

    PDFIconBadge* icon = new PDFIconBadge(iconName, 16, 16, this);
    icon->setObjectName(QStringLiteral("toolCalloutIcon"));
    icon->setColors(std::nullopt, PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));
    headLayout->addWidget(icon, 0, Qt::AlignVCenter);

    m_title = PDFAppPageWidgets::createLabel(title, PDFThemeFonts::Role::SmallSemiBold, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("toolCalloutTitle"));
    m_title->setMinimumWidth(1);
    m_title->setLineHeight(TITLE_LINE);
    headLayout->addWidget(m_title, 1);
    m_layout->addLayout(headLayout);

    m_text = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), TEXT_LINE, PDFThemeColors::TextSecondary, this);
    m_text->setObjectName(QStringLiteral("toolCalloutText"));
    m_text->setMinimumWidth(1);
    m_text->setVisible(!text.isEmpty());
    m_layout->addWidget(m_text);
}

PDFAppToolCallout::~PDFAppToolCallout() = default;

void PDFAppToolCallout::setCompactStyle()
{
    setRadius(COMPACT_RADIUS);
    m_layout->setSpacing(COMPACT_GAP);
    m_layout->setContentsMargins(PADDING_H, PADDING_TOP, PADDING_H, COMPACT_PADDING_BOTTOM);
    if (m_title)
    {
        m_title->setLineHeight(COMPACT_TITLE_LINE);
    }

    // The text has its own line height: a new one replaces it
    PDFAppParagraph* text = PDFAppPageWidgets::createText(m_text ? m_text->text() : QString(), PDFThemeFonts::getFont(QFont::Normal, 11), COMPACT_TEXT_LINE, PDFThemeColors::TextSecondary, this);
    text->setObjectName(QStringLiteral("toolCalloutText"));
    text->setMinimumWidth(1);
    text->setVisible(!text->text().isEmpty());
    if (m_text)
    {
        // The layout gives the item of the old text back: it is owned by the caller
        delete m_layout->replaceWidget(m_text, text);
        delete m_text.data();
    }
    else
    {
        m_layout->addWidget(text);
    }
    m_text = text;
}

PDFAppToolPanelHeader::PDFAppToolPanelHeader(const QString& title, const QString& subtitle, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPanelHeader"));
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(GAP);

    m_title = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 14, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("toolPanelTitle"));
    m_title->setLineHeight(TITLE_LINE);
    m_title->setMinimumWidth(1);
    layout->addWidget(m_title);

    m_subtitle = PDFAppPageWidgets::createLabel(subtitle, QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    m_subtitle->setObjectName(QStringLiteral("toolPanelSubtitle"));
    m_subtitle->setLineHeight(SUBTITLE_LINE);
    m_subtitle->setMinimumWidth(1);
    layout->addWidget(m_subtitle);
}

PDFAppToolPanelHeader::~PDFAppToolPanelHeader() = default;

void PDFAppToolPanelHeader::setTexts(const QString& title, const QString& subtitle)
{
    m_title->setText(title);
    m_subtitle->setText(subtitle);
}

void PDFAppToolCallout::setTexts(const QString& title, const QString& text)
{
    m_title->setText(title);
    m_text->setText(text);
    m_text->setVisible(!text.isEmpty());
}

PDFAppToolCard::PDFAppToolCard(QWidget* parent) :
    BaseClass(std::nullopt, parent)
{
    setRadius(RADIUS);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(PADDING_H, PADDING_V, PADDING_H, PADDING_V);
    m_layout->setSpacing(0);
}

PDFAppToolCard::~PDFAppToolCard() = default;

PDFAppToolMeasuredCard::PDFAppToolMeasuredCard(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolMeasuredCard"));

    QVBoxLayout* layout = getBodyLayout();
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // The sizes are data (as the values of the rows below them): "807.2 kB → 44.4 kB" in this
    // order at the leading edge also right-to-left
    QWidget* sizesRow = new QWidget(this);
    sizesRow->setObjectName(QStringLiteral("toolMeasuredSizes"));
    sizesRow->setLayoutDirection(Qt::LeftToRight);
    QHBoxLayout* sizes = new QHBoxLayout(sizesRow);
    sizes->setContentsMargins(0, 0, 0, 0);
    sizes->setSpacing(GAP);
    m_before = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 15, PDFThemeColors::TextTertiary, sizesRow);
    m_before->setObjectName(QStringLiteral("toolMeasuredBefore"));
    m_before->setFixedHeight(18);
    m_before->setDataText(true);
    PDFLabel* arrow = PDFAppPageWidgets::createLabel(QStringLiteral("→"), QFont::Medium, 15, PDFThemeColors::TextMuted, sizesRow);
    arrow->setFixedHeight(18);
    m_after = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 20, PDFThemeColors::TextPrimary, sizesRow);
    m_after->setObjectName(QStringLiteral("toolMeasuredAfter"));
    m_after->setFixedHeight(24);
    m_after->setDataText(true);

    // 293:1359: items-baseline, both sizes end on one line
    PDFAppPageWidgets::addBaselineLabels(sizes, { { m_before.data() }, { arrow }, { m_after.data() } });

    // The alignment of a layout item takes the direction of its widget: the stretch of a
    // mirrored row keeps the sizes at the leading edge of the card
    QHBoxLayout* sizesPlace = new QHBoxLayout();
    sizesPlace->setContentsMargins(0, 0, 0, 0);
    sizesPlace->setSpacing(0);
    sizesPlace->addWidget(sizesRow);
    sizesPlace->addStretch(1);
    layout->addLayout(sizesPlace);

    m_bar = new QProgressBar(this);
    m_bar->setObjectName(QStringLiteral("toolMeasuredBar"));
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(4);
    layout->addWidget(m_bar);

    m_saving = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 12, PDFThemeColors::TextAccent, this);
    m_saving->setObjectName(QStringLiteral("toolMeasuredSaving"));
    m_saving->setFixedHeight(15);
    layout->addWidget(m_saving);
}

PDFAppToolMeasuredCard::~PDFAppToolMeasuredCard() = default;

void PDFAppToolMeasuredCard::setSizes(const QString& before, const QString& after, int afterPercentage, const QString& savingText)
{
    m_before->setText(before);
    m_after->setText(after);
    m_bar->setVisible(afterPercentage >= 0);
    m_bar->setValue(qBound(0, afterPercentage, 100));
    m_saving->setText(savingText);
    m_saving->setTextColor(afterPercentage >= 0 ? PDFThemeColors::TextAccent : PDFThemeColors::TextSecondary);
}

PDFAppToolSectionHeader::PDFAppToolSectionHeader(const QString& title, const QString& hint, QWidget* parent) :
    BaseClass(parent)
{
    setFixedHeight(HEIGHT);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(GAP);

    m_title = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 13, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("toolSectionTitle"));

    m_hint = PDFAppPageWidgets::createLabel(hint, QFont::Normal, 11, PDFThemeColors::TextMuted, this);
    m_hint->setObjectName(QStringLiteral("toolSectionHint"));
    m_hint->setVisible(!hint.isEmpty());

    // 293:1355: items-baseline
    PDFAppPageWidgets::addBaselineLabels(layout, { { m_title.data() }, { m_hint.data() } });
    layout->addStretch(1);
}

PDFAppToolSectionHeader::~PDFAppToolSectionHeader() = default;

void PDFAppToolSectionHeader::setCountStyle()
{
    // header 389:20403: 22 high with 6 above, the count at the trailing edge, Regular 12 text/tertiary
    setFixedHeight(COUNT_STYLE_HEIGHT);
    QLayout* oldLayout = layout();
    oldLayout->removeWidget(m_title);
    oldLayout->removeWidget(m_hint);
    delete oldLayout;
    delete m_hint.data();

    QHBoxLayout* countLayout = new QHBoxLayout(this);
    countLayout->setContentsMargins(0, COUNT_STYLE_HEIGHT - HEIGHT, 0, 0);
    countLayout->setSpacing(GAP);
    m_hint = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextTertiary, this);
    m_hint->setObjectName(QStringLiteral("toolSectionHint"));
    m_hint->hide();
    PDFAppPageWidgets::addBaselineLabels(countLayout, { { m_title.data(), 1 }, { m_hint.data() } });
}

void PDFAppToolSectionHeader::setHint(const QString& hint)
{
    m_hint->setText(hint);
    m_hint->setVisible(!hint.isEmpty());
}

void PDFAppToolSectionHeader::addTrailingWidget(QWidget* widget)
{
    QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(layout());
    if (!widget || !headerLayout)
    {
        return;
    }

    // The stretch at the end goes, the widget takes its place (the title stays at the leading side)
    if (headerLayout->count() > 0 && headerLayout->itemAt(headerLayout->count() - 1)->spacerItem())
    {
        delete headerLayout->takeAt(headerLayout->count() - 1);
        headerLayout->addStretch(1);
    }
    widget->setParent(this);
    headerLayout->addWidget(widget, 0, Qt::AlignVCenter);
}

// --------------------------------------------------------- PDFAppToolCanvasChip

PDFAppToolCanvasChip::PDFAppToolCanvasChip(QWidget* canvasArea) :
    BaseClass(canvasArea),
    m_canvasArea(canvasArea)
{
    setObjectName(QStringLiteral("toolCanvasChip"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(HEIGHT);
    hide();

    if (canvasArea)
    {
        canvasArea->installEventFilter(this);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppToolCanvasChip::onThemeChanged);
}

PDFAppToolCanvasChip::~PDFAppToolCanvasChip() = default;

void PDFAppToolCanvasChip::setChip(const QString& text, const QString& iconName)
{
    m_text = text;
    m_iconName = iconName;
    setAccessibleName(text);
    updatePlacement();
    update();
}

int PDFAppToolCanvasChip::getChipWidth(const QString& text, const QString& iconName, int padding)
{
    const int textWidth = QFontMetrics(PDFThemeFonts::getFont(QFont::DemiBold, FONT_SIZE)).horizontalAdvance(text);
    return iconName.isEmpty() ? textWidth + 2 * padding : PADDING_LEADING + ICON_SIZE + ICON_GAP + textWidth + PADDING_TRAILING;
}

void PDFAppToolCanvasChip::paintChip(QPainter* painter, const QRect& rect, const QString& text, const QString& iconName, int padding, Qt::LayoutDirection direction)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    // chip 293:1350 / 302:4542: the pill icon/strong, the text and the icon on it bg/surface
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    PDFStylePainter::fill(painter, QRectF(rect), PDFStylePainter::Radii(qreal(rect.height()) / 2.0), PDFThemeColors::IconStrong);
    painter->setFont(PDFThemeFonts::getFont(QFont::DemiBold, FONT_SIZE));
    painter->setPen(PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgSurface));

    const QFontMetricsF metrics(painter->font());
    if (iconName.isEmpty())
    {
        painter->drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, elideText(metrics, text, qMax(0, rect.width() - 2 * padding)));
        return;
    }

    // The icon at the leading side, the text after it: positions mirrored for right-to-left
    const bool isRightToLeft = direction == Qt::RightToLeft;
    const int iconLeft = isRightToLeft ? rect.right() + 1 - PADDING_LEADING - ICON_SIZE : rect.left() + PADDING_LEADING;
    const QRectF iconRect(iconLeft, rect.top() + (rect.height() - ICON_SIZE) / 2.0, ICON_SIZE, ICON_SIZE);
    PDFStylePainter::icon(painter, iconRect, iconName, PDFThemeColors::BgSurface);

    const int textWidth = qMax(0, rect.width() - PADDING_LEADING - ICON_SIZE - ICON_GAP - PADDING_TRAILING);
    const int textLeft = isRightToLeft ? rect.left() + PADDING_TRAILING : rect.left() + PADDING_LEADING + ICON_SIZE + ICON_GAP;
    const QRect textRect(textLeft, rect.top(), textWidth + 1, rect.height());
    painter->drawText(textRect, (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter | Qt::TextSingleLine, elideText(metrics, text, textWidth));
}

QString PDFAppToolCanvasChip::elideText(const QFontMetricsF& metrics, const QString& text, int room)
{
    // The width of a chip has the advance of its text rounded to whole pixels: only a chip
    // narrower than that elides its text
    return metrics.horizontalAdvance(text) <= qreal(room) + 1.0 ? text : metrics.elidedText(text, Qt::ElideRight, qreal(room));
}

void PDFAppToolCanvasChip::updatePlacement()
{
    if (m_text.isEmpty() || !m_canvasArea)
    {
        hide();
        return;
    }

    // Centered at the top of the canvas (283 of 814 for 249), inside of a narrow canvas
    const int canvasWidth = m_canvasArea->width();
    const int width = qMax(0, qMin(getChipWidth(m_text, m_iconName, PADDING_LEADING), canvasWidth - 2 * MARGIN));
    setFixedWidth(width);
    move((canvasWidth - width) / 2, TOP);
    raise();
    show();
}

bool PDFAppToolCanvasChip::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_canvasArea && event->type() == QEvent::Resize)
    {
        updatePlacement();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolCanvasChip::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    paintChip(&painter, rect(), m_text, m_iconName, PADDING_LEADING, layoutDirection());
}

void PDFAppToolCanvasChip::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LayoutDirectionChange)
    {
        update();
    }
}

void PDFAppToolCanvasChip::onThemeChanged()
{
    update();
}

}   // namespace pdf

#include "moc_pdfapptoolpanelparts.cpp"
