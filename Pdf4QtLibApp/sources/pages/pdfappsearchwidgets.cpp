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

#include "pdfappsearchwidgets.h"
#include "pdfapplocale.h"
#include "pdfappcontext.h"
#include "pdfappmainwindow.h"
#include "pdfapppagenotfound.h"
#include "pdfapptab.h"
#include "pdfdatabasenotifier.h"
#include "pdfrecentrepository.h"
#include "pdfshortcutrepository.h"

#include "pdfdocumenttoolsearch.h"

#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimerEvent>
#include <QPainter>
#include <QtMath>

#include <array>

namespace pdf
{

/// Helpers of the drawing of the search
class PDFAppSearchWidgetsHelper
{
public:
    PDFAppSearchWidgetsHelper() = delete;

    static constexpr const char* CONTEXT = "pdf::PDFAppSearchPainter";

    static QString translate(const char* text)
    {
        return QCoreApplication::translate(CONTEXT, text);
    }

    /// Keyboard hints of the footer of the popup: the key caps and the text
    struct Hint
    {
        QStringList keys;
        QString text;
    };

    static constexpr size_t HINT_COUNT = 4;

    /// Hints of the footer (285:2519): navigate, open, show all (Ctrl+Enter), close;
    /// a narrow popup drops them from the end
    static std::array<Hint, HINT_COUNT> getHints()
    {
        return { Hint{ { QString(QChar(0x2191)), QString(QChar(0x2193)) }, translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "navigate")) },
                 Hint{ { PDFAppSearchPainter::getEnterKeyText() }, translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "open")) },
                 Hint{ PDFAppSearchPainter::getShowAllKeys(), translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "show all")) },
                 Hint{ { translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Esc")) }, translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "close")) } };
    }

    /// Width of a hint (key caps, gaps 4, the text)
    static qreal getHintWidth(const Hint& hint)
    {
        qreal width = 0.0;
        for (const QString& key : hint.keys)
        {
            width += PDFAppSearchPainter::getKeyCapSize(key).width() + PDFAppSearchPainter::KEY_GAP;
        }
        return width + qCeil(PDFStylePainter::textWidth(hint.text, PDFAppSearchPainter::getFooterFont()));
    }

    static PDFComponentPainter::TextKind getTitleKind(const PDFAppSearchResult& result)
    {
        return result.isFileName ? PDFComponentPainter::TextKind::FileName : PDFComponentPainter::TextKind::Interface;
    }

    static PDFComponentPainter::TextKind getSubtitleKind(const PDFAppSearchResult& result)
    {
        return result.isDataSubtitle ? PDFComponentPainter::TextKind::Data : PDFComponentPainter::TextKind::Interface;
    }
};

QFont PDFAppSearchPainter::getTitleFont()
{
    return PDFThemeFonts::getFont(QFont::DemiBold, 13);
}

QFont PDFAppSearchPainter::getSubtitleFont()
{
    return PDFThemeFonts::getFont(QFont::Normal, 11);
}

QFont PDFAppSearchPainter::getMetaFont()
{
    return PDFThemeFonts::getFont(QFont::Normal, 11);
}

QFont PDFAppSearchPainter::getOpenFont()
{
    return PDFThemeFonts::getFont(QFont::Medium, 11);
}

QFont PDFAppSearchPainter::getGroupTitleFont()
{
    // TOOLS: Semi Bold 11, the letter spacing 0.44 (4 %), capitals
    QFont font = PDFThemeFonts::getFont(QFont::DemiBold, 11);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.44);
    font.setCapitalization(QFont::AllUppercase);
    return font;
}

QFont PDFAppSearchPainter::getGroupCountFont()
{
    return PDFThemeFonts::getFont(QFont::Normal, 11);
}

QFont PDFAppSearchPainter::getKeyCapFont()
{
    return PDFThemeFonts::getFont(QFont::Medium, 10);
}

QFont PDFAppSearchPainter::getFooterFont()
{
    return PDFThemeFonts::getFont(QFont::Normal, 11);
}

QFont PDFAppSearchPainter::getFooterLinkFont(bool isUnderlined)
{
    QFont font = PDFThemeFonts::getFont(QFont::Medium, 11);
    font.setUnderline(isUnderlined);
    return font;
}

QFont PDFAppSearchPainter::getShowAllFont(bool isUnderlined)
{
    QFont font = PDFThemeFonts::getFont(QFont::Medium, 12);
    font.setUnderline(isUnderlined);
    return font;
}

QString PDFAppSearchPainter::getOpenText()
{
    return PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Open"));
}

QString PDFAppSearchPainter::getEnterKeyText()
{
    return PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Enter"));
}

QStringList PDFAppSearchPainter::getShowAllKeys()
{
    // The modifier as the key caps of the shortcuts (Ctrl, Command on macOS) and Enter
    QStringList keys = PDFAppKeyCap::getKeyNames(QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)));
    if (!keys.isEmpty())
    {
        keys.removeLast();
    }
    keys << getEnterKeyText();
    return keys;
}

QString PDFAppSearchPainter::getCountText(int shown, int total)
{
    if (shown >= total || shown < 0)
    {
        return PDFAppLocale::formatNumber(qMax(0, total));
    }
    return PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "%1 of %2")).arg(PDFAppLocale::formatNumber(shown), PDFAppLocale::formatNumber(total));
}

QRectF PDFAppSearchPainter::getBadgeRect(const QRectF& rect)
{
    return QRectF(rect.left() + ROW_PADDING_HORIZONTAL, rect.top() + (rect.height() - BADGE_SIZE) / 2.0, BADGE_SIZE, BADGE_SIZE);
}

QSizeF PDFAppSearchPainter::getKeyCapSize(const QString& text)
{
    // Paddings 6 / 2 and the border 1, the line of the text 12
    return QSizeF(qCeil(PDFStylePainter::textWidth(text, getKeyCapFont())) + 2 * 6 + 2, KEY_CAP_HEIGHT);
}

void PDFAppSearchPainter::paintKeyCap(QPainter* painter, const QRectF& rect, const QString& text)
{
    if (!painter || rect.isEmpty())
    {
        return;
    }

    PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing);
    PDFStylePainter::fill(painter, rect, PDFStylePainter::Radii(4.0), PDFThemeColors::BgSubtle);
    PDFStylePainter::strokeInside(painter, rect, PDFStylePainter::Radii(4.0), PDFThemeColors::BorderDefault);
    PDFStylePainter::text(painter, rect, Qt::AlignCenter | Qt::TextSingleLine, text, getKeyCapFont(), PDFStylePainter::color(PDFThemeColors::TextSecondary));
}

void PDFAppSearchPainter::paintResultRow(QPainter* painter, const QRectF& rect, const PDFAppSearchResult& result, const PDFComponentPainter::State& state, bool isCurrent)
{
    if (!painter || !rect.isValid() || rect.isEmpty())
    {
        return;
    }

    PDFComponentPainter::DirectionGuard directionGuard(painter, state);
    PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing);

    const bool isEnabled = state.isEnabled();
    const PDFStylePainter::Radii radii(ROW_RADIUS);
    if (state.isPressed())
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgPressed);
    }
    else if (state.isHovered() || (isCurrent && isEnabled))
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgHover);
    }

    // Tool icon 28 (the hue of the category, Neutral for the other groups)
    PDFComponentPainter::ToolBadge badge;
    badge.iconName = result.iconName;
    badge.hue = result.hue;
    badge.size = PDFComponentPainter::BadgeSize::Small;
    PDFComponentPainter::paintToolBadge(painter, state.visualRect(rect, getBadgeRect(rect)), badge, state);

    // The trailing side: "Open" with the key cap Enter of the current result,
    // otherwise the meta text (and the icon external-link of an external result)
    const qreal centerY = rect.center().y();
    qreal trailing = rect.right() + 1.0 - ROW_PADDING_HORIZONTAL;
    const qreal leading = rect.left() + ROW_PADDING_HORIZONTAL + BADGE_SIZE + ROW_GAP;
    if (isCurrent && isEnabled)
    {
        const QString enter = getEnterKeyText();
        const QSizeF keySize = getKeyCapSize(enter);
        const QRectF keyRect(trailing - keySize.width(), centerY - keySize.height() / 2.0, keySize.width(), keySize.height());
        paintKeyCap(painter, state.visualRect(rect, keyRect), enter);
        trailing = keyRect.left() - ROW_GAP;

        const QString open = getOpenText();
        const qreal openWidth = qCeil(PDFStylePainter::textWidth(open, getOpenFont()));
        const QRectF openRect(trailing - openWidth, rect.top(), openWidth, rect.height());
        PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, openRect), open, getOpenFont(), PDFThemeColors::TextTertiary,
                                            PDFComponentPainter::TextKind::Interface, true);
        trailing = openRect.left() - ROW_GAP;
    }
    else
    {
        if (result.isExternal)
        {
            const QRectF iconRect(trailing - EXTERNAL_ICON_SIZE, centerY - EXTERNAL_ICON_SIZE / 2.0, EXTERNAL_ICON_SIZE, EXTERNAL_ICON_SIZE);
            PDFStylePainter::icon(painter, state.visualRect(rect, iconRect), QStringLiteral("external-link"), isEnabled ? PDFThemeColors::IconMuted : PDFThemeColors::IconDisabled);
            trailing = iconRect.left() - ROW_GAP;
        }

        if (!result.meta.isEmpty())
        {
            // The meta text takes at most a third of the row
            const qreal metaWidth = qMin<qreal>(qCeil(PDFStylePainter::textWidth(result.meta, getMetaFont())), qMax<qreal>(0.0, (trailing - leading) / 3.0));
            const QRectF metaRect(trailing - metaWidth, rect.top(), metaWidth, rect.height());
            PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, metaRect), result.meta, getMetaFont(),
                                                isEnabled ? PDFThemeColors::TextMuted : PDFThemeColors::TextDisabled, PDFComponentPainter::TextKind::Interface, true);
            trailing = metaRect.left() - ROW_GAP;
        }
    }

    // The title and the subtitle (gap 1), centered as a block of 30
    const qreal textWidth = qMax<qreal>(0.0, trailing - leading);
    const qreal textTop = rect.top() + (rect.height() - (TITLE_HEIGHT + 1 + SUBTITLE_HEIGHT)) / 2.0;
    const QRectF titleRect(leading, textTop, textWidth, TITLE_HEIGHT);
    const QRectF subtitleRect(leading, textTop + TITLE_HEIGHT + 1, textWidth, SUBTITLE_HEIGHT);
    PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, titleRect), result.title, getTitleFont(),
                                        isEnabled ? PDFThemeColors::TextPrimary : PDFThemeColors::TextDisabled, PDFAppSearchWidgetsHelper::getTitleKind(result));
    PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, subtitleRect), result.subtitle, getSubtitleFont(),
                                        isEnabled ? PDFThemeColors::TextMuted : PDFThemeColors::TextDisabled, PDFAppSearchWidgetsHelper::getSubtitleKind(result));

    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, rect, radii);
    }
}

void PDFAppSearchPainter::paintGroupHeader(QPainter* painter, const QRectF& rect, const QString& title, const QString& count, const PDFComponentPainter::State& state)
{
    if (!painter || !rect.isValid() || rect.isEmpty())
    {
        return;
    }

    PDFComponentPainter::DirectionGuard directionGuard(painter, state);
    PDFStylePainterGuard guard(painter);

    const QFont titleFont = getGroupTitleFont();
    const QFont countFont = getGroupCountFont();
    const qreal available = qMax<qreal>(0.0, rect.width() - 2 * GROUP_PADDING_HORIZONTAL);
    const qreal countWidth = qMin<qreal>(qCeil(PDFStylePainter::textWidth(count, countFont)), available / 2.0);
    const qreal titleWidth = qMin<qreal>(qCeil(PDFStylePainter::textWidth(title, titleFont)), qMax<qreal>(0.0, available - countWidth - GROUP_GAP));

    // The texts are 13 high, 10 below the top of the header
    const qreal top = rect.top() + GROUP_PADDING_TOP;
    const QRectF titleRect(rect.left() + GROUP_PADDING_HORIZONTAL, top, titleWidth, SUBTITLE_HEIGHT);
    const QRectF countRect(titleRect.right() + GROUP_GAP, top, countWidth, SUBTITLE_HEIGHT);
    const bool isEnabled = state.isEnabled();
    PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, titleRect), title, titleFont, isEnabled ? PDFThemeColors::TextTertiary : PDFThemeColors::TextDisabled);
    PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, countRect), count, countFont, isEnabled ? PDFThemeColors::TextMuted : PDFThemeColors::TextDisabled);
}

void PDFAppSearchPainter::paintShowAll(QPainter* painter, const QRectF& rect, const QString& text, const PDFComponentPainter::State& state)
{
    if (!painter || !rect.isValid() || rect.isEmpty())
    {
        return;
    }

    PDFComponentPainter::DirectionGuard directionGuard(painter, state);
    PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing);

    // Show all (6) › : Medium 12 text/accent 56 from the edge, 6 below the top, the
    // chevron 14 after a gap of 4; underlined under the pointer
    const bool isEnabled = state.isEnabled();
    const QFont font = getShowAllFont(state.isHovered() || state.isPressed());
    const qreal textWidth = qMin<qreal>(qCeil(PDFStylePainter::textWidth(text, font)), qMax<qreal>(0.0, rect.width() - SHOW_ALL_INDENT - 4 - EXTERNAL_ICON_SIZE));
    const QRectF textRect(rect.left() + SHOW_ALL_INDENT, rect.top() + 6, textWidth, 15);
    const PDFThemeColors::Color color = !isEnabled ? PDFThemeColors::TextDisabled : (state.isPressed() ? PDFThemeColors::TextAccentPressed : PDFThemeColors::TextAccent);
    PDFComponentPainter::drawElidedText(painter, state, state.visualRect(rect, textRect), text, font, color);

    const QRectF chevronRect(textRect.right() + 4, textRect.center().y() - EXTERNAL_ICON_SIZE / 2.0, EXTERNAL_ICON_SIZE, EXTERNAL_ICON_SIZE);
    const QString chevron = state.isRightToLeft() ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right");
    PDFStylePainter::icon(painter, state.visualRect(rect, chevronRect), chevron, isEnabled ? PDFThemeColors::IconAccent : PDFThemeColors::IconDisabled);

    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, rect.adjusted(ROW_WRAP_PADDING, 0, -ROW_WRAP_PADDING, 0), PDFStylePainter::Radii(ROW_RADIUS));
    }
}

PDFAppSearchResultRow::PDFAppSearchResultRow(const PDFAppSearchResult& result, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("searchResult"));
    setResult(result);
}

void PDFAppSearchResultRow::setResult(const PDFAppSearchResult& result)
{
    m_result = result;
    setText(result.title);
    setToolTip(QString());
    invalidateHeight();
}

void PDFAppSearchResultRow::setCurrent(bool isCurrent)
{
    if (m_isCurrent != isCurrent)
    {
        m_isCurrent = isCurrent;
        update();
    }
}

void PDFAppSearchResultRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFAppSearchPainter::paintResultRow(painter, QRectF(rect), m_result, state, m_isCurrent);
}

QSize PDFAppSearchResultRow::getContentSizeHint() const
{
    return QSize(480, PDFAppSearchPainter::ROW_HEIGHT);
}

QSize PDFAppSearchResultRow::getContentMinimumSizeHint() const
{
    // The texts are elided, the badge and the paddings stay
    return QSize(PDFAppSearchPainter::ROW_PADDING_HORIZONTAL * 2 + PDFAppSearchPainter::BADGE_SIZE + PDFAppSearchPainter::ROW_GAP + 48, PDFAppSearchPainter::ROW_HEIGHT);
}

QString PDFAppSearchResultRow::getDefaultAccessibleDescription() const
{
    return joinTexts({ m_result.subtitle, m_result.meta }, getListPattern());
}

PDFAppSearchShowAllButton::PDFAppSearchShowAllButton(PDFAppSearchGroup group, int count, QWidget* parent) :
    BaseClass(parent),
    m_group(group),
    m_count(count)
{
    setObjectName(QStringLiteral("showAll-%1").arg(PDFAppSearchGroups::getId(group)));
    setText(PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Show all (%1)")).arg(count));
    setAccessibleDescription(PDFAppSearchGroups::getTitle(group));
}

void PDFAppSearchShowAllButton::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFAppSearchPainter::paintShowAll(painter, QRectF(rect), text(), state);
}

QSize PDFAppSearchShowAllButton::getContentSizeHint() const
{
    const int textWidth = qCeil(PDFStylePainter::textWidth(text(), PDFAppSearchPainter::getShowAllFont(false)));
    return QSize(PDFAppSearchPainter::SHOW_ALL_INDENT + textWidth + 4 + PDFAppSearchPainter::EXTERNAL_ICON_SIZE + PDFAppSearchPainter::ROW_PADDING_HORIZONTAL,
                 PDFAppSearchPainter::SHOW_ALL_HEIGHT);
}

PDFAppSearchGroupHeader::PDFAppSearchGroupHeader(const QString& title, const QString& count, QWidget* parent) :
    BaseClass(parent),
    m_title(title),
    m_count(count)
{
    setObjectName(QStringLiteral("searchGroupHeader"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("%1 %2").arg(title, count));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppSearchGroupHeader::update));
}

QSize PDFAppSearchGroupHeader::sizeHint() const
{
    const int width = 2 * PDFAppSearchPainter::GROUP_PADDING_HORIZONTAL + PDFAppSearchPainter::GROUP_GAP +
                      qCeil(PDFStylePainter::textWidth(m_title, PDFAppSearchPainter::getGroupTitleFont())) +
                      qCeil(PDFStylePainter::textWidth(m_count, PDFAppSearchPainter::getGroupCountFont()));
    return QSize(width, PDFAppSearchPainter::GROUP_HEIGHT);
}

QSize PDFAppSearchGroupHeader::minimumSizeHint() const
{
    return QSize(2 * PDFAppSearchPainter::GROUP_PADDING_HORIZONTAL, PDFAppSearchPainter::GROUP_HEIGHT);
}

void PDFAppSearchGroupHeader::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    PDFAppSearchPainter::paintGroupHeader(&painter, QRectF(rect()), m_title, m_count, state);
}

PDFAppSearchPopupShadow::PDFAppSearchPopupShadow(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("searchPopupShadow"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppSearchPopupShadow::update));
}

QMargins PDFAppSearchPopupShadow::getMargins()
{
    // Shadow 0 / 8 / 24 (the margins of the popovers of the style)
    return QMargins(24, 16, 24, 32);
}

void PDFAppSearchPopupShadow::setPanelGeometry(const QRect& panel)
{
    QRect geometry = panel.marginsAdded(getMargins());
    if (QWidget* parent = parentWidget())
    {
        geometry = geometry.intersected(parent->rect());
    }

    m_panel = panel.translated(-geometry.topLeft());
    setGeometry(geometry);
    update();
}

void PDFAppSearchPopupShadow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_panel.isEmpty())
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    PDFStylePainter::shadow(&painter, QRectF(m_panel), PDFStylePainter::Radii(PDFAppSearchPainter::PANEL_RADIUS), QPointF(0.0, 8.0), 24.0,
                            PDFStylePainter::color(PDFThemeColors::EffectShadow));
}

PDFAppSearchPopup::PDFAppSearchPopup(QWidget* window) :
    BaseClass(window)
{
    setObjectName(QStringLiteral("searchPopup"));
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setAccessibleName(PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Search results")));

    m_shadow = new PDFAppSearchPopupShadow(window);
    m_shadow->hide();
    hide();

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppSearchPopup::update));
}

PDFAppSearchPopup::~PDFAppSearchPopup()
{
    // The shadow is a sibling (a child of the window)
    delete m_shadow.data();
}

void PDFAppSearchPopup::setResults(const PDFAppSearchResults& results)
{
    // The current result is kept by its identity, when it is still there
    const PDFAppSearchResult* current = getCurrentResult();
    const std::optional<std::pair<PDFAppSearchGroup, QString>> currentKey = current ? std::make_optional(std::make_pair(current->group, current->key)) : std::nullopt;

    m_results = results;
    m_currentRow = -1;
    m_pressedRow = -1;
    updateGeometryFromAnchor();

    int currentRow = getRowCount() > 0 ? 0 : -1;
    if (currentKey)
    {
        for (int row = 0; row < getRowCount(); ++row)
        {
            const PDFAppSearchResult* result = getRowResult(row);
            if (result && result->group == currentKey->first && result->key == currentKey->second)
            {
                currentRow = row;
                break;
            }
        }
    }
    setCurrentRow(currentRow);
    update();
}

std::vector<int> PDFAppSearchPopup::selectRows(int rowBudget) const
{
    std::vector<int> counts(m_results.groups.size(), 0);

    // Each group with a result has a row, the rest takes the best scores
    int budget = qMax(0, rowBudget);
    for (size_t i = 0; i < m_results.groups.size() && budget > 0; ++i)
    {
        if (!m_results.groups[i].results.empty())
        {
            counts[i] = 1;
            --budget;
        }
    }

    while (budget > 0)
    {
        int best = -1;
        int bestScore = 0;
        for (size_t i = 0; i < m_results.groups.size(); ++i)
        {
            const std::vector<PDFAppSearchResult>& results = m_results.groups[i].results;
            if (size_t(counts[i]) < results.size() && (best < 0 || results[size_t(counts[i])].score > bestScore))
            {
                best = int(i);
                bestScore = results[size_t(counts[i])].score;
            }
        }

        if (best < 0)
        {
            break;
        }
        ++counts[size_t(best)];
        --budget;
    }
    return counts;
}

int PDFAppSearchPopup::getPanelHeight(const std::vector<int>& counts) const
{
    int height = 2 + PDFAppSearchPainter::PANEL_PADDING_TOP + PDFAppSearchPainter::FOOTER_HEIGHT;
    int rows = 0;
    for (const int count : counts)
    {
        if (count > 0)
        {
            height += PDFAppSearchPainter::GROUP_HEIGHT + count * PDFAppSearchPainter::ROW_HEIGHT;
            rows += count;
        }
    }

    if (rows == 0)
    {
        // The message "No results for …"
        height += PDFAppSearchPainter::ROW_HEIGHT;
    }
    return height;
}

void PDFAppSearchPopup::layoutItems()
{
    m_items.clear();
    m_rowItems.clear();

    // Fewer rows in a low window (at least one, the panel is cut otherwise)
    int budget = MAXIMUM_ROWS;
    std::vector<int> counts = selectRows(budget);
    while (budget > 1 && getPanelHeight(counts) > m_availableHeight)
    {
        --budget;
        counts = selectRows(budget);
    }

    const int panelWidth = width();
    int y = 1 + PDFAppSearchPainter::PANEL_PADDING_TOP;
    for (size_t i = 0; i < m_results.groups.size() && i < counts.size(); ++i)
    {
        const PDFAppSearchGroupResults& group = m_results.groups[i];
        if (counts[i] <= 0)
        {
            continue;
        }

        Item header;
        header.kind = Item::Kind::GroupHeader;
        header.group = group.group;
        header.rect = QRect(0, y, panelWidth, PDFAppSearchPainter::GROUP_HEIGHT);
        header.count = PDFAppSearchPainter::getCountText(counts[i], group.totalCount);
        m_items.push_back(header);
        y += PDFAppSearchPainter::GROUP_HEIGHT;

        for (int j = 0; j < counts[i]; ++j)
        {
            Item row;
            row.kind = Item::Kind::Result;
            row.group = group.group;
            row.resultIndex = j;
            row.rect = QRect(0, y, panelWidth, PDFAppSearchPainter::ROW_HEIGHT);
            m_rowItems.push_back(m_items.size());
            m_items.push_back(row);
            y += PDFAppSearchPainter::ROW_HEIGHT;
        }
    }

    if (m_rowItems.empty())
    {
        Item message;
        message.kind = Item::Kind::Message;
        message.rect = QRect(0, y, panelWidth, PDFAppSearchPainter::ROW_HEIGHT);
        m_items.push_back(message);
    }

    updateHints();
}

void PDFAppSearchPopup::updateHints()
{
    // The hints, which fit beside the link (from the first one)
    const QString link = getShowAllText();
    const qreal linkWidth = link.isEmpty() ? 0.0 : PDFStylePainter::textWidth(link, PDFAppSearchPainter::getFooterLinkFont(false));
    const qreal available = width() - 2 - 2 * PDFAppSearchPainter::FOOTER_PADDING - (linkWidth > 0.0 ? linkWidth + PDFAppSearchPainter::FOOTER_GAP : 0.0);

    m_visibleHints = 0;
    qreal used = 0.0;
    for (const PDFAppSearchWidgetsHelper::Hint& hint : PDFAppSearchWidgetsHelper::getHints())
    {
        const qreal hintWidth = PDFAppSearchWidgetsHelper::getHintWidth(hint) + (m_visibleHints > 0 ? PDFAppSearchPainter::FOOTER_GAP : 0);
        if (used + hintWidth > available)
        {
            break;
        }
        used += hintWidth;
        ++m_visibleHints;
    }
}

void PDFAppSearchPopup::placeBelow(const QRect& anchor)
{
    m_anchor = anchor;
    updateGeometryFromAnchor();
}

void PDFAppSearchPopup::updateGeometryFromAnchor()
{
    QWidget* window = parentWidget();
    if (!window || !m_anchor.isValid())
    {
        layoutItems();
        return;
    }

    // Aligned with the trailing edge of the field (the right edge, the left one in
    // right-to-left), inside of the window
    const QRect windowRect = window->rect();
    const int panelWidth = qMax(1, qMin(PANEL_WIDTH, windowRect.width() - 2 * WINDOW_MARGIN));
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int x = isRightToLeft ? m_anchor.left() : m_anchor.right() + 1 - panelWidth;
    x = qBound(WINDOW_MARGIN, x, qMax(WINDOW_MARGIN, windowRect.width() - WINDOW_MARGIN - panelWidth));
    const int y = m_anchor.bottom() + 1 + ANCHOR_GAP;
    m_availableHeight = qMax(0, windowRect.height() - y - WINDOW_MARGIN);

    // The width first (the items take it), the height of the items then
    resize(panelWidth, qMax(1, height()));
    layoutItems();

    int panelHeight = 2 + PDFAppSearchPainter::PANEL_PADDING_TOP + PDFAppSearchPainter::FOOTER_HEIGHT;
    for (const Item& item : m_items)
    {
        panelHeight += item.rect.height();
    }

    setGeometry(x, y, panelWidth, panelHeight);
    if (m_shadow)
    {
        m_shadow->setPanelGeometry(geometry());
    }
}

void PDFAppSearchPopup::open()
{
    QWidget* window = parentWidget();
    if (!window)
    {
        return;
    }

    if (m_shadow)
    {
        m_shadow->setPanelGeometry(geometry());
        m_shadow->show();
        m_shadow->raise();
    }
    show();
    raise();
}

void PDFAppSearchPopup::close()
{
    hide();
}

void PDFAppSearchPopup::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    m_pressedRow = -1;
    m_isShowAllPressed = false;
    m_isShowAllHovered = false;
    if (m_shadow)
    {
        m_shadow->hide();
    }
}

int PDFAppSearchPopup::getRowCount() const
{
    return int(m_rowItems.size());
}

void PDFAppSearchPopup::setCurrentRow(int row)
{
    const int newRow = getRowCount() > 0 ? qBound(-1, row, getRowCount() - 1) : -1;
    if (newRow == m_currentRow)
    {
        return;
    }

    m_currentRow = newRow;
    update();
    Q_EMIT currentRowChanged(m_currentRow);
}

void PDFAppSearchPopup::moveCurrentRow(int step)
{
    if (getRowCount() <= 0)
    {
        return;
    }

    const int row = m_currentRow < 0 ? (step > 0 ? 0 : getRowCount() - 1) : m_currentRow + step;
    setCurrentRow(qBound(0, row, getRowCount() - 1));
}

const PDFAppSearchResult* PDFAppSearchPopup::getRowResult(int row) const
{
    if (row < 0 || row >= getRowCount())
    {
        return nullptr;
    }

    const Item& item = m_items[m_rowItems[size_t(row)]];
    const PDFAppSearchGroupResults* group = m_results.getGroup(item.group);
    if (!group || item.resultIndex < 0 || size_t(item.resultIndex) >= group->results.size())
    {
        return nullptr;
    }
    return &group->results[size_t(item.resultIndex)];
}

const PDFAppSearchResult* PDFAppSearchPopup::getCurrentResult() const
{
    return getRowResult(m_currentRow);
}

QRect PDFAppSearchPopup::getRowRect(int row) const
{
    if (row < 0 || row >= getRowCount())
    {
        return QRect();
    }

    // The row inside of the border and of the row-wrap padding
    const int inset = 1 + PDFAppSearchPainter::ROW_WRAP_PADDING;
    return m_items[m_rowItems[size_t(row)]].rect.adjusted(inset, 0, -inset, 0);
}

QString PDFAppSearchPopup::getShowAllText() const
{
    const int count = m_results.getTotalCount();
    if (count <= 0)
    {
        return QString();
    }
    if (count == 1)
    {
        return PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Show the result"));
    }
    return QCoreApplication::translate(PDFAppSearchWidgetsHelper::CONTEXT, QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "Show all %Ln results"), nullptr, count);
}

QRect PDFAppSearchPopup::getShowAllRect() const
{
    const QString text = getShowAllText();
    if (text.isEmpty())
    {
        return QRect();
    }

    const int textWidth = qCeil(PDFStylePainter::textWidth(text, PDFAppSearchPainter::getFooterLinkFont(false)));
    const QRect footer(1, height() - 1 - PDFAppSearchPainter::FOOTER_HEIGHT, width() - 2, PDFAppSearchPainter::FOOTER_HEIGHT);
    const QRect logical(footer.right() + 1 - PDFAppSearchPainter::FOOTER_PADDING - textWidth, footer.top(), textWidth, footer.height());
    return QStyle::visualRect(layoutDirection(), rect(), logical);
}

QString PDFAppSearchPopup::getHintText(int index) const
{
    if (index < 0 || index >= m_visibleHints || size_t(index) >= PDFAppSearchWidgetsHelper::HINT_COUNT)
    {
        return QString();
    }

    const PDFAppSearchWidgetsHelper::Hint hint = PDFAppSearchWidgetsHelper::getHints()[size_t(index)];
    return hint.keys.join(QLatin1Char(' ')) + QLatin1Char(' ') + hint.text;
}

QRect PDFAppSearchPopup::getHintRect(int index) const
{
    if (index < 0 || index >= m_visibleHints || size_t(index) >= PDFAppSearchWidgetsHelper::HINT_COUNT)
    {
        return QRect();
    }

    // The hints from the leading edge of the footer, the gaps 12 between them
    const std::array<PDFAppSearchWidgetsHelper::Hint, PDFAppSearchWidgetsHelper::HINT_COUNT> hints = PDFAppSearchWidgetsHelper::getHints();
    qreal x = 1 + PDFAppSearchPainter::FOOTER_PADDING;
    for (int i = 0; i < index; ++i)
    {
        x += PDFAppSearchWidgetsHelper::getHintWidth(hints[size_t(i)]) + PDFAppSearchPainter::FOOTER_GAP;
    }
    const QRectF logical(x, height() - 1 - PDFAppSearchPainter::FOOTER_HEIGHT, PDFAppSearchWidgetsHelper::getHintWidth(hints[size_t(index)]), PDFAppSearchPainter::FOOTER_HEIGHT);
    return QStyle::visualRect(layoutDirection(), rect(), logical.toAlignedRect());
}

void PDFAppSearchPopup::activateCurrent()
{
    const PDFAppSearchResult* result = getCurrentResult();
    if (!result)
    {
        return;
    }

    // A copy: a slot may change the results (or delete the popup)
    const PDFAppSearchResult activated = *result;
    Q_EMIT resultActivated(activated);
}

QSize PDFAppSearchPopup::sizeHint() const
{
    return size();
}

int PDFAppSearchPopup::getRowAt(const QPoint& position) const
{
    for (int row = 0; row < getRowCount(); ++row)
    {
        if (getRowRect(row).contains(position))
        {
            return row;
        }
    }
    return -1;
}

void PDFAppSearchPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF panel = QRectF(rect());
    const PDFStylePainter::Radii radii(PDFAppSearchPainter::PANEL_RADIUS);
    PDFStylePainter::fill(&painter, panel, radii, PDFThemeColors::BgSurface);

    const Qt::LayoutDirection direction = layoutDirection();
    const QStyle::State enabled = isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
    {
        PDFStylePainterGuard guard(&painter);
        painter.setClipPath(PDFStylePainter::roundedPath(panel, radii));

        int row = 0;
        for (const Item& item : m_items)
        {
            switch (item.kind)
            {
                case Item::Kind::GroupHeader:
                    PDFAppSearchPainter::paintGroupHeader(&painter, QRectF(item.rect), PDFAppSearchGroups::getTitle(item.group), item.count, PDFComponentPainter::State(enabled, direction));
                    break;

                case Item::Kind::Result:
                {
                    QStyle::State flags = enabled;
                    if (row == m_pressedRow && getRowRect(row).contains(mapFromGlobal(QCursor::pos())))
                    {
                        flags |= QStyle::State_Sunken;
                    }
                    if (const PDFAppSearchResult* result = getRowResult(row))
                    {
                        PDFAppSearchPainter::paintResultRow(&painter, QRectF(getRowRect(row)), *result, PDFComponentPainter::State(flags, direction), row == m_currentRow);
                    }
                    ++row;
                    break;
                }

                case Item::Kind::Message:
                {
                    // "No results for “epub”" in the place of a row
                    const PDFComponentPainter::State state(enabled, direction);
                    const QRectF textRect = QRectF(item.rect).adjusted(PDFAppSearchPainter::GROUP_PADDING_HORIZONTAL, 0, -PDFAppSearchPainter::GROUP_PADDING_HORIZONTAL, 0);
                    const QString text = PDFAppSearchWidgetsHelper::translate(QT_TRANSLATE_NOOP("pdf::PDFAppSearchPainter", "No results for “%1”")).arg(PDFAppSearchEngine::getVisibleText(m_results.text, 64));
                    PDFComponentPainter::drawElidedText(&painter, state, textRect, text, PDFThemeFonts::getFont(PDFThemeFonts::Role::Body), PDFThemeColors::TextMuted);
                    break;
                }
            }
        }

        // Footer: bg/subtle, the hints and the link
        const QRectF footer(0, height() - 1 - PDFAppSearchPainter::FOOTER_HEIGHT, width(), PDFAppSearchPainter::FOOTER_HEIGHT + 1);
        PDFStylePainter::fillRect(&painter, footer, PDFThemeColors::BgSubtle);

        const PDFComponentPainter::State state(enabled, direction);
        PDFComponentPainter::DirectionGuard directionGuard(&painter, state);
        const QRectF footerContent(1 + PDFAppSearchPainter::FOOTER_PADDING, footer.top(), width() - 2 - 2 * PDFAppSearchPainter::FOOTER_PADDING, PDFAppSearchPainter::FOOTER_HEIGHT);
        const std::array<PDFAppSearchWidgetsHelper::Hint, PDFAppSearchWidgetsHelper::HINT_COUNT> hints = PDFAppSearchWidgetsHelper::getHints();
        qreal x = footerContent.left();
        for (int i = 0; i < m_visibleHints && i < int(hints.size()); ++i)
        {
            const PDFAppSearchWidgetsHelper::Hint& hint = hints[size_t(i)];
            for (const QString& key : hint.keys)
            {
                const QSizeF keySize = PDFAppSearchPainter::getKeyCapSize(key);
                const QRectF keyRect(x, footerContent.center().y() - keySize.height() / 2.0, keySize.width(), keySize.height());
                PDFAppSearchPainter::paintKeyCap(&painter, state.visualRect(QRectF(rect()), keyRect), key);
                x = keyRect.right() + PDFAppSearchPainter::KEY_GAP;
            }

            const qreal textWidth = qCeil(PDFStylePainter::textWidth(hint.text, PDFAppSearchPainter::getFooterFont()));
            const QRectF textRect(x, footerContent.top(), textWidth, footerContent.height());
            PDFComponentPainter::drawElidedText(&painter, state, state.visualRect(QRectF(rect()), textRect), hint.text, PDFAppSearchPainter::getFooterFont(), PDFThemeColors::TextMuted);
            x = textRect.right() + PDFAppSearchPainter::FOOTER_GAP;
        }

        const QString link = getShowAllText();
        if (!link.isEmpty())
        {
            const QFont font = PDFAppSearchPainter::getFooterLinkFont(m_isShowAllHovered || m_isShowAllPressed);
            const PDFThemeColors::Color color = m_isShowAllPressed ? PDFThemeColors::TextAccentPressed : PDFThemeColors::TextAccent;
            PDFComponentPainter::drawElidedText(&painter, state, QRectF(getShowAllRect()), link, font, color);
        }
    }

    PDFStylePainter::strokeInside(&painter, panel, radii, PDFThemeColors::BorderDefault);
}

void PDFAppSearchPopup::mouseMoveEvent(QMouseEvent* event)
{
    // The pointer moves the current row (as the arrows do)
    const QPoint position = event->position().toPoint();
    const int row = getRowAt(position);
    if (row >= 0 && event->buttons() == Qt::NoButton)
    {
        setCurrentRow(row);
    }

    const bool isLinkHovered = getShowAllRect().contains(position);
    if (isLinkHovered != m_isShowAllHovered)
    {
        m_isShowAllHovered = isLinkHovered;
        update();
    }

    setCursor(row >= 0 || isLinkHovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (m_pressedRow >= 0)
    {
        update();
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFAppSearchPopup::mousePressEvent(QMouseEvent* event)
{
    // The popup takes every press (it never reaches the content below it)
    event->accept();
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    const QPoint position = event->position().toPoint();
    m_pressedRow = getRowAt(position);
    m_isShowAllPressed = m_pressedRow < 0 && getShowAllRect().contains(position);
    if (m_pressedRow >= 0)
    {
        setCurrentRow(m_pressedRow);
    }
    update();
}

void PDFAppSearchPopup::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    const QPoint position = event->position().toPoint();
    const int pressedRow = m_pressedRow;
    const bool isShowAllPressed = m_isShowAllPressed;
    m_pressedRow = -1;
    m_isShowAllPressed = false;
    update();

    if (pressedRow >= 0 && getRowAt(position) == pressedRow)
    {
        setCurrentRow(pressedRow);
        activateCurrent();
    }
    else if (isShowAllPressed && getShowAllRect().contains(position))
    {
        Q_EMIT showAllRequested();
    }
}

void PDFAppSearchPopup::leaveEvent(QEvent* event)
{
    BaseClass::leaveEvent(event);
    if (m_isShowAllHovered)
    {
        m_isShowAllHovered = false;
        update();
    }
}

void PDFAppSearchPopup::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LayoutDirectionChange:
        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateGeometryFromAnchor();
            update();
            break;

        case QEvent::LanguageChange:
            updateHints();
            update();
            break;

        default:
            break;
    }
}

class PDFAppSearchController::PressFilter : public QObject
{
public:
    explicit PressFilter(PDFAppSearchController* controller) :
        QObject(controller),
        m_controller(controller)
    {

    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        // The press goes on to its widget
        if (m_controller && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick))
        {
            m_controller->onApplicationPress(watched);
        }
        return false;
    }

private:
    QPointer<PDFAppSearchController> m_controller;
};

PDFAppSearchController::PDFAppSearchController(PDFLineEdit* field) :
    BaseClass(field),
    m_field(field)
{
    setObjectName(QStringLiteral("searchController"));
    if (!field)
    {
        return;
    }

    field->installEventFilter(this);
    connect(field, &QLineEdit::textEdited, this, &PDFAppSearchController::onTextEdited);
    connect(field, &QLineEdit::returnPressed, this, &PDFAppSearchController::onReturnPressed);
}

PDFAppSearchController::~PDFAppSearchController()
{
    watch(false);

    // The popup is a child of the window, not of the field
    if (m_popup)
    {
        m_popup->disconnect(this);
        delete m_popup.data();
    }
}

PDFAppSearchController* PDFAppSearchController::find(const QWidget* field)
{
    return field ? field->findChild<PDFAppSearchController*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

bool PDFAppSearchController::isPopupOpen() const
{
    return m_popup && m_popup->isOpen();
}

bool PDFAppSearchController::isSearchPending() const
{
    return m_timerId != 0;
}

PDFAppContext* PDFAppSearchController::findContext() const
{
    for (QWidget* widget = m_field ? m_field->parentWidget() : nullptr; widget; widget = widget->parentWidget())
    {
        if (PDFAppTab* tab = qobject_cast<PDFAppTab*>(widget))
        {
            return tab->getContext();
        }
    }
    return nullptr;
}

void PDFAppSearchController::connectToTab()
{
    PDFAppTab* tab = nullptr;
    for (QWidget* widget = m_field ? m_field->parentWidget() : nullptr; widget; widget = widget->parentWidget())
    {
        tab = qobject_cast<PDFAppTab*>(widget);
        if (tab)
        {
            break;
        }
    }

    if (tab == m_connectedTab.data())
    {
        return;
    }

    if (m_connectedTab)
    {
        disconnect(this, nullptr, m_connectedTab.data(), nullptr);
    }

    // The routes go through the tab to the navigator (as the search of the header)
    m_connectedTab = tab;
    if (tab)
    {
        connect(this, &PDFAppSearchController::navigationRequested, tab, &PDFAppTab::navigationRequested);
        connect(this, &PDFAppSearchController::externalUrlRequested, tab, &PDFAppTab::externalUrlRequested);
    }
}

void PDFAppSearchController::connectToNotifier()
{
    PDFAppContext* context = findContext();
    PDFDatabaseNotifier* notifier = context ? context->getNotifier() : nullptr;
    if (!notifier || notifier == m_connectedNotifier.data())
    {
        return;
    }

    if (m_connectedNotifier)
    {
        m_connectedNotifier->disconnect(this);
    }

    m_connectedNotifier = notifier;
    connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppSearchController::onDatabaseChanged);
    connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppSearchController::onEverythingChanged);
}

void PDFAppSearchController::onTextEdited(const QString& text)
{
    if (m_timerId != 0)
    {
        killTimer(m_timerId);
        m_timerId = 0;
    }

    if (PDFSearchQuery::sanitize(text).isEmpty())
    {
        closePopup();
        return;
    }

    // Typing is joined into one search
    m_timerId = startTimer(SEARCH_DELAY);
}

void PDFAppSearchController::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_timerId)
    {
        updateSearch();
        return;
    }

    BaseClass::timerEvent(event);
}

void PDFAppSearchController::updateSearch()
{
    if (m_timerId != 0)
    {
        killTimer(m_timerId);
        m_timerId = 0;
    }

    // The popup is shown only for the field, which the user types into
    if (!m_field || !m_field->isVisible() || !m_field->hasFocus() || PDFSearchQuery::sanitize(m_field->text()).isEmpty())
    {
        closePopup();
        return;
    }

    // The data are read, when the popup opens (and after a change notification);
    // the names and the folders of the files of Recent are normalized then, not
    // for each typed text
    if (!m_data)
    {
        m_data = std::make_unique<PDFAppSearchData>(PDFAppSearchData::load(findContext()));
        ++m_dataLoadCount;
    }
    m_data->now = m_now;
    ++m_searchCount;
    openPopup(m_engine.search(m_field->text(), *m_data));
}

void PDFAppSearchController::openPopup(const PDFAppSearchResults& results)
{
    QWidget* window = m_field ? m_field->window() : nullptr;
    if (!window)
    {
        return;
    }

    // A field moved into another window gets a new popup
    if (m_popup && m_popup->parentWidget() != window)
    {
        m_popup->disconnect(this);
        delete m_popup.data();
    }

    if (!m_popup)
    {
        m_popup = new PDFAppSearchPopup(window);
        connect(m_popup.data(), &PDFAppSearchPopup::resultActivated, this, &PDFAppSearchController::onResultActivated);
        connect(m_popup.data(), &PDFAppSearchPopup::showAllRequested, this, &PDFAppSearchController::onShowAllRequested);
        connect(m_popup.data(), &PDFAppSearchPopup::currentRowChanged, this, &PDFAppSearchController::onCurrentRowChanged);
        connect(m_popup.data(), &QObject::destroyed, this, &PDFAppSearchController::onPopupDestroyed);
    }

    connectToTab();
    connectToNotifier();

    m_popup->setLayoutDirection(m_field->layoutDirection());
    placePopup();
    m_popup->setResults(results);
    m_popup->open();
    watch(true);
    onCurrentRowChanged(m_popup->getCurrentRow());
}

void PDFAppSearchController::placePopup()
{
    QWidget* window = m_popup ? m_popup->parentWidget() : nullptr;
    if (!window || !m_field)
    {
        return;
    }

    m_popup->placeBelow(QRect(m_field->mapTo(window, QPoint(0, 0)), m_field->size()));
}

void PDFAppSearchController::closePopup()
{
    if (m_timerId != 0)
    {
        killTimer(m_timerId);
        m_timerId = 0;
    }

    watch(false);

    // The data are read again at the next opening
    m_data.reset();
    if (m_field)
    {
        m_field->setAccessibleDescription(QString());
    }

    if (m_popup && m_popup->isOpen())
    {
        m_popup->close();
    }
}

void PDFAppSearchController::watch(bool isOpen)
{
    QWidget* window = isOpen && m_field ? m_field->window() : nullptr;
    if (m_watchedWindow != window)
    {
        if (m_watchedWindow)
        {
            m_watchedWindow->removeEventFilter(this);
        }
        m_watchedWindow = window;
        if (window)
        {
            window->installEventFilter(this);
        }
    }

    // A press outside of the popup and of the field closes the popup: a filter of
    // its own, the filters of the field and of the window get the other events
    // (an application filter of the controller would get them twice)
    QCoreApplication* application = QCoreApplication::instance();
    if (isOpen != m_isApplicationFiltered && application)
    {
        if (!m_pressFilter)
        {
            m_pressFilter = new PressFilter(this);
        }

        // The filter is kept (a press may close the popup inside of the filter)
        if (isOpen)
        {
            application->installEventFilter(m_pressFilter);
        }
        else
        {
            application->removeEventFilter(m_pressFilter);
        }
        m_isApplicationFiltered = isOpen;
    }
}

void PDFAppSearchController::onApplicationPress(QObject* watched)
{
    QWidget* widget = qobject_cast<QWidget*>(watched);
    if (widget && widget != m_field && !(m_popup && (widget == m_popup || m_popup->isAncestorOf(widget))))
    {
        closePopup();
    }
}

void PDFAppSearchController::onReturnPressed()
{
    // The field asks for the page of all results (PDFAppWorkspaceHeader)
    closePopup();
}

void PDFAppSearchController::onResultActivated(const PDFAppSearchResult& result)
{
    closePopup();

    // The search is used: the field is cleared for the next one
    QPointer<PDFAppSearchController> guard(this);
    if (m_field)
    {
        // textChanged runs the code of the application
        m_field->clear();
    }
    if (!guard)
    {
        return;
    }

    connectToTab();
    switch (result.action)
    {
        case PDFAppSearchResult::Action::Navigate:
            if (result.route.isValid())
            {
                Q_EMIT navigationRequested(result.route, PDFAppNavigationMode::Push);
            }
            break;

        case PDFAppSearchResult::Action::OpenUrl:
            Q_EMIT externalUrlRequested(result.url);
            break;

        case PDFAppSearchResult::Action::RunCommand:
            if (!PDFAppSearchActions::runCommand(m_field, result.commandId))
            {
                qWarning("The command %s of a search result cannot be run", qPrintable(result.commandId));
            }
            break;

        case PDFAppSearchResult::Action::OpenDocument:
            PDFAppSearchActions::openDocument(m_field, result.filePath);
            break;
    }
}

void PDFAppSearchController::onShowAllRequested()
{
    const QString text = m_field ? m_field->text().simplified() : QString();
    closePopup();
    if (text.isEmpty())
    {
        return;
    }

    connectToTab();
    Q_EMIT navigationRequested(PDFAppRoutes::search(text.left(PDFSearchQuery::MAXIMUM_LENGTH)), PDFAppNavigationMode::Push);
}

void PDFAppSearchController::onCurrentRowChanged(int row)
{
    Q_UNUSED(row);

    // Assistive technologies hear the current result in the description of the field
    const PDFAppSearchResult* result = m_popup ? m_popup->getCurrentResult() : nullptr;
    if (m_field)
    {
        m_field->setAccessibleDescription(result ? QStringLiteral("%1, %2").arg(result->title, result->subtitle) : QString());
    }
}

void PDFAppSearchController::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    if (topic == PDFRecentRepository::getFileTopic() || topic == PDFShortcutRepository::getTopic())
    {
        onEverythingChanged();
    }
}

void PDFAppSearchController::onEverythingChanged()
{
    // Never stale results: the open popup reads the data again and searches
    m_data.reset();
    if (isPopupOpen())
    {
        updateSearch();
    }
}

void PDFAppSearchController::onPopupDestroyed()
{
    // The window has deleted the popup: nothing is watched any more
    watch(false);
}

bool PDFAppSearchController::isShowAllKey(const QKeyEvent* event)
{
    // Ctrl+Enter (Cmd+Enter on macOS, also the Enter of the keypad)
    return event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
           (event->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
}

bool PDFAppSearchController::handleFieldKey(QKeyEvent* event)
{
    if (isShowAllKey(event))
    {
        // The page of all results of the typed text (the same as the link Show all
        // n results; the route takes the text of the field, so a pending search is
        // not needed). An empty field does nothing, the key is consumed, so the
        // field never asks for the page of all results either.
        onShowAllRequested();
        return true;
    }

    if ((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)
    {
        return false;
    }

    switch (event->key())
    {
        case Qt::Key_Down:
            if (isPopupOpen() && !isSearchPending())
            {
                m_popup->moveCurrentRow(1);
            }
            else
            {
                updateSearch();
            }
            return true;

        case Qt::Key_Up:
            if (isPopupOpen())
            {
                m_popup->moveCurrentRow(-1);
                return true;
            }
            return false;

        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
            if (isPopupOpen())
            {
                m_popup->moveCurrentRow(event->key() == Qt::Key_PageDown ? PDFAppSearchPopup::MAXIMUM_ROWS : -PDFAppSearchPopup::MAXIMUM_ROWS);
                return true;
            }
            return false;

        case Qt::Key_Return:
        case Qt::Key_Enter:
        {
            // The results of the typed text (a pending search is done first)
            if (isSearchPending() || isPopupOpen())
            {
                updateSearch();
            }
            if (isPopupOpen() && m_popup->getCurrentResult())
            {
                m_popup->activateCurrent();
                return true;
            }
            return false;
        }

        case Qt::Key_Escape:
            if (isPopupOpen() || isSearchPending())
            {
                closePopup();
                return true;
            }
            if (m_field && !m_field->text().isEmpty())
            {
                m_field->clear();
                return true;
            }
            return false;

        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            closePopup();
            return false;

        default:
            return false;
    }
}

bool PDFAppSearchController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_field)
    {
        switch (event->type())
        {
            case QEvent::KeyPress:
                return handleFieldKey(static_cast<QKeyEvent*>(event));

            case QEvent::ShortcutOverride:
            {
                // Esc and the arrows of the field are not shortcuts of the window,
                // neither is Ctrl+Enter (show all results) of the field
                const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
                if (((keyEvent->modifiers() == Qt::NoModifier && (keyEvent->key() == Qt::Key_Escape || keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down)) || isShowAllKey(keyEvent)) &&
                    (isPopupOpen() || (m_field && !m_field->text().isEmpty())))
                {
                    event->accept();
                    return true;
                }
                break;
            }

            case QEvent::FocusOut:
            case QEvent::Hide:
                closePopup();
                break;

            case QEvent::Move:
            case QEvent::Resize:
                if (isPopupOpen())
                {
                    placePopup();
                }
                break;

            case QEvent::LanguageChange:
                if (isPopupOpen())
                {
                    updateSearch();
                }
                break;

            default:
                break;
        }
        return BaseClass::eventFilter(watched, event);
    }

    if (watched == m_watchedWindow)
    {
        switch (event->type())
        {
            case QEvent::WindowDeactivate:
            case QEvent::Hide:
            case QEvent::Close:
                closePopup();
                break;

            case QEvent::Resize:
            case QEvent::LayoutRequest:
                if (isPopupOpen())
                {
                    placePopup();
                }
                break;

            default:
                break;
        }
        return BaseClass::eventFilter(watched, event);
    }

    return BaseClass::eventFilter(watched, event);
}

bool PDFAppSearchActions::runCommand(QWidget* widget, const QString& commandId)
{
    PDFAppMainWindow* window = widget ? qobject_cast<PDFAppMainWindow*>(widget->window()) : nullptr;
    QAction* action = window ? window->getCommandAction(commandId) : nullptr;
    if (!action || !action->isEnabled())
    {
        return false;
    }

    // The command may close the tab or the window (deleted later)
    action->trigger();
    return true;
}

bool PDFAppSearchActions::openDocument(QWidget* widget, const QString& filePath)
{
    PDFAppMainWindow* window = widget ? qobject_cast<PDFAppMainWindow*>(widget->window()) : nullptr;
    if (!window || filePath.isEmpty())
    {
        return false;
    }

    // The opening of the documents of the window (Recent; the document tab of step 3)
    window->openDocuments({ filePath }, 0);
    return true;
}

}   // namespace pdf
