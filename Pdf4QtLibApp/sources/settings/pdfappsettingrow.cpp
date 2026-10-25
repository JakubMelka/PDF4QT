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

#include "pdfappsettingrow.h"
#include "pdfappcontext.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfappworkspace.h"
#include "pdfsettingsrepository.h"

#include "pdfdocumenttoolsearch.h"
#include "pdfpainterutils.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QBoxLayout>
#include <QPainter>
#include <QResizeEvent>

namespace pdf
{

/// Helpers of the rows of the settings
class PDFAppSettingRowHelper
{
public:
    PDFAppSettingRowHelper() = delete;

    // Scores of the fields of a row (the scale of the global search)
    static constexpr int TITLE_EXACT = 100;
    static constexpr int TITLE_PREFIX = 90;
    static constexpr int TITLE_TYPO = 60;
    static constexpr int KEYWORD_EXACT = 50;
    static constexpr int KEYWORD_PREFIX = 45;
    static constexpr int KEYWORD_TYPO = 30;
    static constexpr int DESCRIPTION_EXACT = 20;
    static constexpr int DESCRIPTION_PREFIX = 18;

    /// The translated text and the English source text (once, when equal)
    static void addTexts(QStringList& texts, const QString& translated, const QString& source)
    {
        if (!translated.isEmpty())
        {
            texts << translated;
        }
        if (!source.isEmpty() && source != translated)
        {
            texts << source;
        }
    }
};

PDFAppSettingRow::PDFAppSettingRow(PDFAppContext* context, const PDFSettingDefinition& definition, QWidget* parent) :
    BaseClass(parent),
    m_context(context),
    m_definition(definition),
    m_anchor(definition.getAnchor().isEmpty() ? definition.getKey() : definition.getAnchor())
{
    const QString title = definition.getTitle();
    init(title.isEmpty() ? definition.getKey() : title, definition.getDescription(), definition.getTag());

    // The binder shows the stored value already, the row takes only its dependency
    // (a value read twice per row doubled the reads of the filtered page)
    m_control = new PDFAppSettingControl(context, definition, m_controlWrap);
    setControlWidget(m_control->getWidget());
    if (m_definition->hasEnabledWhen())
    {
        m_controlWrap->setEnabled(isDependencyEnabled());
    }
}

PDFAppSettingRow::PDFAppSettingRow(const QString& title, const QString& description, const QString& anchor, QWidget* parent) :
    BaseClass(parent),
    m_anchor(anchor)
{
    init(title, description, QString());
}

PDFAppSettingRow::~PDFAppSettingRow() = default;

void PDFAppSettingRow::init(const QString& title, const QString& description, const QString& tag)
{
    setObjectName(QStringLiteral("row-%1").arg(m_anchor));
    setMinimumHeight(MINIMUM_HEIGHT);

    m_highlightTimer.setSingleShot(true);
    m_highlightTimer.setInterval(HIGHLIGHT_DURATION);
    connect(&m_highlightTimer, &QTimer::timeout, this, &PDFAppSettingRow::onHighlightTimeout);

    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    updateMargins();
    m_layout->setSpacing(SPACING);

    // text 146:3349: the label row (title and tag, gap 8) and the description, gap 2
    m_textColumn = new QWidget(this);
    QVBoxLayout* textLayout = new QVBoxLayout(m_textColumn);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QWidget* labelRow = new QWidget(m_textColumn);
    QHBoxLayout* labelLayout = new QHBoxLayout(labelRow);
    labelLayout->setContentsMargins(0, 0, 0, 0);
    labelLayout->setSpacing(8);
    // The title (label-row 146:3348) wraps only in a narrow row (its size hint is
    // the whole line)
    m_titleLabel = PDFAppPageWidgets::createText(title, PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold), 16, PDFThemeColors::TextPrimary, labelRow);
    m_titleLabel->setObjectName(QStringLiteral("title"));
    labelLayout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    m_tag = new PDFTag(tag, labelRow);
    m_tag->setObjectName(QStringLiteral("tag"));
    m_tag->setHidden(tag.isEmpty());
    labelLayout->addWidget(m_tag, 0, Qt::AlignVCenter);
    labelLayout->addStretch(1);
    textLayout->addWidget(labelRow);

    m_descriptionLabel = PDFAppPageWidgets::createText(description, PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), 13, PDFThemeColors::TextMuted, m_textColumn);
    m_descriptionLabel->setObjectName(QStringLiteral("description"));
    m_descriptionLabel->setHidden(description.isEmpty());
    textLayout->addWidget(m_descriptionLabel);

    // control-wrap 146:3355: 260 wide, the control at its leading edge
    m_controlWrap = new QWidget(this);
    m_controlWrap->setObjectName(QStringLiteral("controlWrap"));
    m_controlWrap->setFixedWidth(CONTROL_WRAP_WIDTH);
    m_controlLayout = new QHBoxLayout(m_controlWrap);
    m_controlLayout->setContentsMargins(0, 0, 0, 0);
    m_controlLayout->setSpacing(0);
    m_controlLayout->addStretch(1);

    m_layout->addWidget(m_textColumn, 1, Qt::AlignVCenter);
    m_layout->addWidget(m_controlWrap, 0, Qt::AlignVCenter);
}

QString PDFAppSettingRow::getKey() const
{
    return m_definition ? m_definition->getKey() : QString();
}

void PDFAppSettingRow::setControlWidget(QWidget* widget)
{
    if (m_controlWidget.data() == widget || !m_controlLayout)
    {
        return;
    }

    // A previous widget of a custom row goes (the widget of the binder stays its own)
    if (m_controlWidget && (!m_control || m_controlWidget.data() != m_control->getWidget()))
    {
        delete m_controlWidget.data();
    }

    m_controlWidget = widget;
    if (widget)
    {
        widget->setParent(m_controlWrap);
        m_controlLayout->insertWidget(0, widget, 0, Qt::AlignLeading | Qt::AlignVCenter);
        widget->show();
    }
}

void PDFAppSettingRow::setBlockWidget(QWidget* widget, const QMargins& margins)
{
    if (!widget || !m_layout || m_blockWidget.data() == widget)
    {
        return;
    }

    // A previous block widget goes; the texts and the control wrap are hidden (the
    // texts stay for the search)
    delete m_blockWidget.data();
    m_blockWidget = widget;
    if (m_textColumn)
    {
        m_textColumn->hide();
    }
    if (m_controlWrap)
    {
        m_controlWrap->hide();
    }
    widget->setParent(this);
    m_padding = margins;
    updateMargins();
    m_layout->addWidget(widget, 1);
    widget->show();
}

QString PDFAppSettingRow::getTitle() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

void PDFAppSettingRow::setTitle(const QString& title)
{
    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }
}

QString PDFAppSettingRow::getDescription() const
{
    return m_descriptionLabel ? m_descriptionLabel->text() : QString();
}

void PDFAppSettingRow::setDescription(const QString& description)
{
    if (m_descriptionLabel)
    {
        m_descriptionLabel->setText(description);
        m_descriptionLabel->setHidden(description.isEmpty());
    }
}

QString PDFAppSettingRow::getTagText() const
{
    return m_tag ? m_tag->text() : QString();
}

void PDFAppSettingRow::setTagText(const QString& text)
{
    if (m_tag)
    {
        m_tag->setText(text);
        m_tag->setHidden(text.isEmpty());
    }
}

void PDFAppSettingRow::setSearchKeywords(const QStringList& keywords)
{
    m_searchKeywords = keywords;
}

bool PDFAppSettingRow::matches(const PDFSearchQuery& query, const QStringList& groupTexts) const
{
    using Helper = PDFAppSettingRowHelper;

    if (query.isEmpty())
    {
        return true;
    }

    QStringList titles;
    QStringList keywords = m_searchKeywords;
    QStringList descriptions;
    keywords << groupTexts;

    if (m_definition)
    {
        Helper::addTexts(titles, m_definition->getTitle(), QString::fromUtf8(m_definition->getTitleSource()));
        Helper::addTexts(descriptions, m_definition->getDescription(), QString::fromUtf8(m_definition->getDescriptionSource()));
        keywords << m_definition->getKeywords();
        Helper::addTexts(keywords, m_definition->getTag(), QString::fromUtf8(m_definition->getTagSource()));
        if (m_definition->getType() == PDFSettingDefinition::Type::Option)
        {
            for (const PDFSettingOption& option : m_definition->getOptions())
            {
                Helper::addTexts(keywords, option.getTitle(), option.title);
            }
        }
    }
    else
    {
        Helper::addTexts(titles, getTitle(), QString());
        Helper::addTexts(descriptions, getDescription(), QString());
    }

    const std::vector<PDFSearchField> fields = {
        PDFSearchField(titles, Helper::TITLE_EXACT, Helper::TITLE_PREFIX, Helper::TITLE_TYPO, true),
        PDFSearchField(keywords, Helper::KEYWORD_EXACT, Helper::KEYWORD_PREFIX, Helper::KEYWORD_TYPO),
        PDFSearchField(descriptions, Helper::DESCRIPTION_EXACT, Helper::DESCRIPTION_PREFIX, 0)
    };
    return query.match(fields).has_value();
}

bool PDFAppSettingRow::isDependencyEnabled() const
{
    if (!m_definition || !m_definition->hasEnabledWhen())
    {
        return true;
    }
    if (!m_context || !m_context->getDatabase())
    {
        return false;
    }

    const QVariant value = m_context->getSettings().getValue(m_definition->getEnabledWhenKey());
    return value.isValid() && value == m_definition->getEnabledWhenValue();
}

void PDFAppSettingRow::updateFromSettings(const QString& key)
{
    if (!m_definition)
    {
        return;
    }

    if (m_control && (key.isEmpty() || key == m_definition->getKey()))
    {
        m_control->refresh();
    }

    if (m_controlWrap && m_definition->hasEnabledWhen() && (key.isEmpty() || key == m_definition->getEnabledWhenKey()))
    {
        // A control, which depends on another setting (Custom author name, the
        // color profiles, the developer overlays), is disabled without it
        m_controlWrap->setEnabled(isDependencyEnabled());
    }
}

void PDFAppSettingRow::highlight()
{
    setHighlighted(true);
    m_highlightTimer.start();
}

void PDFAppSettingRow::setHighlighted(bool highlighted)
{
    if (!highlighted)
    {
        m_highlightTimer.stop();
    }

    if (m_isHighlighted != highlighted)
    {
        m_isHighlighted = highlighted;
        update();
    }
}

void PDFAppSettingRow::setDividerVisible(bool visible)
{
    if (m_isDividerVisible != visible)
    {
        m_isDividerVisible = visible;
        updateMargins();
        update();
    }
}

void PDFAppSettingRow::commitPendingValue()
{
    if (m_control)
    {
        m_control->commitPendingValue();
    }
}

void PDFAppSettingRow::paintEvent(QPaintEvent* event)
{
    BaseClass::paintEvent(event);

    const PDFThemeManager* themeManager = PDFThemeManager::getInstance();
    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_isDividerVisible)
    {
        // border-top border/divider of 146:3360
        painter.fillRect(QRect(0, 0, width(), DIVIDER_WIDTH), themeManager->getColor(PDFThemeColors::BorderDivider));
    }

    if (m_isHighlighted)
    {
        // The row of an anchor: bg/accent-subtle with the ring border/focus inside
        const qreal inset = HIGHLIGHT_RING * 0.5 + 1.0;
        const QRectF rect = QRectF(this->rect()).adjusted(inset, inset, -inset, -inset);
        painter.setPen(QPen(themeManager->getColor(PDFThemeColors::BorderFocus), HIGHLIGHT_RING));
        painter.setBrush(themeManager->getColor(PDFThemeColors::BgAccentSubtle));
        painter.drawRoundedRect(rect, 8.0, 8.0);
    }
}

void PDFAppSettingRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateArrangement(event->size().width());
}

void PDFAppSettingRow::updateArrangement(int width)
{
    const bool isStacked = width < STACK_THRESHOLD;
    if (!m_layout || !m_controlWrap || isStacked == m_isStacked)
    {
        return;
    }

    // A narrow row has the control below its texts (the minimal window)
    m_isStacked = isStacked;
    m_layout->setDirection(isStacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_layout->setAlignment(m_controlWrap, isStacked ? Qt::Alignment(Qt::AlignLeading) : Qt::Alignment(Qt::AlignVCenter));
    m_layout->setAlignment(m_textColumn, isStacked ? Qt::Alignment() : Qt::Alignment(Qt::AlignVCenter));
    if (isStacked)
    {
        m_controlWrap->setMinimumWidth(0);
        m_controlWrap->setMaximumWidth(CONTROL_WRAP_WIDTH);
    }
    else
    {
        m_controlWrap->setFixedWidth(CONTROL_WRAP_WIDTH);
    }
}

void PDFAppSettingRow::updateMargins()
{
    // The border-top of 146:3559 is a part of the row: the padding 11 (or the
    // padding of a block) starts below it (a row of a spin box is 57 high, a row of
    // texts keeps 56)
    if (m_layout)
    {
        m_layout->setContentsMargins(m_padding + QMargins(0, m_isDividerVisible ? DIVIDER_WIDTH : 0, 0, 0));
    }
}

void PDFAppSettingRow::onHighlightTimeout()
{
    setHighlighted(false);
}

PDFAppSettingGroup::PDFAppSettingGroup(const QString& key, const QString& title, const QString& hint, QWidget* parent) :
    BaseClass(parent),
    m_key(key)
{
    setObjectName(QStringLiteral("group-%1").arg(key));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(SPACING);

    // header 146:3345: the title Bold 15 and the hint Regular 12 at the trailing side
    m_hint = PDFAppPageWidgets::createHint(hint, nullptr);
    m_hint->setObjectName(QStringLiteral("hint"));
    m_header = PDFAppPageWidgets::createSectionHeader(title, m_hint, this);
    m_hint->setHidden(hint.isEmpty());
    m_layout->addWidget(m_header);

    // card 146:3347: the rows without spacing, the border inside of the margins
    m_card = new PDFAppCard(QMargins(PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH), 0, this);
    m_card->setObjectName(QStringLiteral("card"));
    m_layout->addWidget(m_card);
    m_card->hide();
}

PDFAppSettingGroup::~PDFAppSettingGroup() = default;

QString PDFAppSettingGroup::getTitle() const
{
    return m_header && m_header->getTitleLabel() ? m_header->getTitleLabel()->text() : QString();
}

void PDFAppSettingGroup::setTexts(const QString& title, const QString& hint)
{
    if (m_header && m_header->getTitleLabel())
    {
        m_header->getTitleLabel()->setText(title);
    }
    if (m_hint)
    {
        m_hint->setText(hint);
        m_hint->setHidden(hint.isEmpty());
    }
}

void PDFAppSettingGroup::addRow(PDFAppSettingRow* row, int index)
{
    if (!row || !m_card || !m_card->getLayout())
    {
        return;
    }

    QVBoxLayout* layout = m_card->getLayout();
    const int position = index < 0 || index > layout->count() ? layout->count() : index;
    row->setParent(m_card);
    layout->insertWidget(position, row);
    row->show();
    updateRows();
}

void PDFAppSettingGroup::insertWidget(QWidget* widget, int index)
{
    if (!widget || !m_layout)
    {
        return;
    }

    const int position = index < 0 || index > m_layout->count() ? m_layout->count() : index;
    widget->setParent(this);
    m_layout->insertWidget(position, widget);
    widget->show();
    m_extraWidgets.append(widget);
}

QList<PDFAppSettingRow*> PDFAppSettingGroup::getRows() const
{
    // In the order of the card (deleted rows are forgotten)
    QList<PDFAppSettingRow*> rows;
    QVBoxLayout* layout = m_card ? m_card->getLayout() : nullptr;
    for (int i = 0; layout && i < layout->count(); ++i)
    {
        if (PDFAppSettingRow* row = qobject_cast<PDFAppSettingRow*>(layout->itemAt(i)->widget()))
        {
            rows.append(row);
        }
    }
    return rows;
}

QList<QWidget*> PDFAppSettingGroup::getExtraWidgets() const
{
    QList<QWidget*> widgets;
    for (const QPointer<QWidget>& widget : m_extraWidgets)
    {
        if (widget)
        {
            widgets.append(widget.data());
        }
    }
    return widgets;
}

int PDFAppSettingGroup::applyFilter(const PDFSearchQuery& query, bool showAll)
{
    const QStringList groupTexts = { getTitle(), m_key };
    int visibleRows = 0;
    const QList<PDFAppSettingRow*> rows = getRows();
    for (PDFAppSettingRow* row : rows)
    {
        const bool isVisible = showAll || row->matches(query, groupTexts);
        row->setHidden(!isVisible);
        visibleRows += isVisible ? 1 : 0;
    }

    const QList<QWidget*> extraWidgets = getExtraWidgets();
    for (QWidget* widget : extraWidgets)
    {
        widget->setHidden(!showAll);
    }

    updateRows();
    setHidden(showAll ? (rows.isEmpty() && extraWidgets.isEmpty()) : visibleRows == 0);
    return visibleRows;
}

void PDFAppSettingGroup::updateAncestorLayouts(QWidget* widget)
{
    // Invalidated from the widget up: the layout requests are posted in this order,
    // so each layout is activated after the layout of its child, which has given
    // its parent the new size hint - all levels in one pass
    for (QWidget* ancestor = widget ? widget->parentWidget() : nullptr; ancestor && !ancestor->isWindow(); ancestor = ancestor->parentWidget())
    {
        if (QLayout* layout = ancestor->layout())
        {
            layout->invalidate();
        }
        ancestor->updateGeometry();
    }
}

void PDFAppSettingGroup::updateRows()
{
    bool isFirst = true;
    for (PDFAppSettingRow* row : getRows())
    {
        if (row->isHidden())
        {
            continue;
        }
        row->setDividerVisible(!isFirst);
        isFirst = false;
    }

    if (m_card)
    {
        m_card->setHidden(isFirst);
    }
}

}   // namespace pdf
