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

#include "pdfcomponentcards.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QAccessible>
#include <QChildEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QStyle>
#include <QtMath>

namespace pdf
{

/// Helper of the cards of the components
class PDFComponentCardsHelper
{
public:
    PDFComponentCardsHelper() = delete;

    /// Dynamic property of a tag of a video card, which the card has hidden (it does not fit)
    static constexpr const char* HIDDEN_BY_CARD_PROPERTY = "_pdf4qt_hiddenByCard";

    /// Returns true, if two pixmaps are the same picture
    static bool isSamePixmap(const QPixmap& first, const QPixmap& second)
    {
        return first.cacheKey() == second.cacheKey() || (first.isNull() && second.isNull());
    }

    /// Returns the token, if it is valid, otherwise the fallback (an invalid
    /// token would be drawn in the leak color)
    static PDFThemeColors::Color getValidColor(PDFThemeColors::Color color, PDFThemeColors::Color fallback)
    {
        return (static_cast<int>(color) >= 0 && static_cast<int>(color) < static_cast<int>(PDFThemeColors::ColorCount)) ? color : fallback;
    }
};

PDFCardButton::PDFCardButton(QWidget* parent) :
    BaseClass(FocusRing::Outside, parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusRingRadius(PDFComponentPainter::getCardRadius());
    setTrailingMargins(PDFComponentPainter::getCardTrailingMargins());
    setTrailingSpacing(PDFComponentPainter::getCardTrailingSpacing());
}

PDFComponentPainter::State PDFCardButton::getBorderState(const PDFComponentPainter::State& state) const
{
    // The focus frame of PDFWidgetStyle draws the ring outside, the card only
    // drops its border; under another style the card keeps its border
    PDFComponentPainter::State paintedState = state;
    if (isEnabled() && PDFWidgetStyle::isActive() && PDFWidgetStyle::isFocusRingVisible(this))
    {
        paintedState.flags |= QStyle::State_HasFocus;
    }
    return paintedState;
}

void PDFCardButton::changeEvent(QEvent* event)
{
    // PDFComponentButton forgets the cached height after a change of the font,
    // of the style and of the language
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        // The pattern of the composed description is translated
        updateDescription();
    }
}

// -------------------------------------------------------------- Quick action tile

PDFQuickActionTile::PDFQuickActionTile(QWidget* parent) :
    BaseClass(parent),
    m_star(new PDFStarButton(PDFStarButton::StarSize::Medium, this))
{
    setTrailingMargins(PDFComponentPainter::getQuickActionTileTrailingMargins());

    // The star is hidden by default (Show star false); it pins the tool, its
    // accessible description is the title of the tile
    m_star->hide();
    m_star->setSubject(PDFStarButton::Subject::Tool);
    addTrailingWidget(m_star.data());
    connect(m_star.data(), &PDFStarButton::pinnedChanged, this, &PDFQuickActionTile::pinnedChanged);
}

PDFQuickActionTile::PDFQuickActionTile(const QString& title, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFQuickActionTile(parent)
{
    setTitle(title);
    setDescription(description);
    m_tile.iconName = iconName;
    m_tile.hue = PDFComponentPainter::normalized(hue);
}

void PDFQuickActionTile::setTitle(const QString& title)
{
    setText(title);
}

void PDFQuickActionTile::setDescription(const QString& description)
{
    if (m_tile.description != description)
    {
        replaceAccessibleDescription(m_tile.description, description);
        m_tile.description = description;
        update();
    }
}

void PDFQuickActionTile::setIconName(const QString& iconName)
{
    if (m_tile.iconName != iconName)
    {
        m_tile.iconName = iconName;
        update();
    }
}

void PDFQuickActionTile::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_tile.hue != hue)
    {
        m_tile.hue = hue;
        update();
    }
}

bool PDFQuickActionTile::isStarVisible() const
{
    return isNestedShown(m_star.data());
}

void PDFQuickActionTile::setStarVisible(bool isVisible)
{
    // The star hides itself through the layout of the nested widgets of the card
    if (m_star && isStarVisible() != isVisible)
    {
        m_star->setVisible(isVisible);
        updateGeometry();
        update();
    }
}

bool PDFQuickActionTile::isPinned() const
{
    return m_star && m_star->isPinned();
}

void PDFQuickActionTile::setPinned(bool isPinned)
{
    if (m_star)
    {
        m_star->setPinned(isPinned);
    }
}

PDFComponentPainter::QuickActionTile PDFQuickActionTile::getContent() const
{
    PDFComponentPainter::QuickActionTile tile = m_tile;
    tile.title = text();
    tile.starVisible = isStarVisible();
    tile.pinned = isPinned();
    tile.paintNested = false;
    return tile;
}

void PDFQuickActionTile::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintQuickActionTile(painter, QRectF(rect), getContent(), getBorderState(state), false);
}

QSize PDFQuickActionTile::getContentSizeHint() const
{
    return PDFComponentPainter::getQuickActionTileSize(getContent());
}

QSize PDFQuickActionTile::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getQuickActionTileMinimumSize(getContent());
}

// -------------------------------------------------------------------- Folder card

PDFFolderCard::PDFFolderCard(QWidget* parent) :
    BaseClass(parent),
    m_moreButton(new PDFMoreButton(this))
{
    setTrailingMargins(PDFComponentPainter::getFolderCardTrailingMargins());
    addTrailingWidget(m_moreButton.data());
    connect(m_moreButton.data(), &PDFMoreButton::menuRequested, this, &PDFFolderCard::menuRequested);
}

PDFFolderCard::PDFFolderCard(const QString& name, const QString& path, const QString& meta, PDFAccentHue hue, QWidget* parent) :
    PDFFolderCard(parent)
{
    setName(name);
    m_card.path = path;
    m_card.meta = meta;
    m_card.hue = PDFComponentPainter::normalizedFolderHue(hue);
    updateDescription();
}

void PDFFolderCard::setName(const QString& name)
{
    setText(name);
}

void PDFFolderCard::setPath(const QString& path)
{
    if (m_card.path != path)
    {
        m_card.path = path;
        updateDescription();
        update();
    }
}

void PDFFolderCard::setMeta(const QString& meta)
{
    if (m_card.meta != meta)
    {
        m_card.meta = meta;
        updateDescription();
        update();
    }
}

void PDFFolderCard::setMetaVisible(bool isVisible)
{
    if (m_card.metaVisible != isVisible)
    {
        m_card.metaVisible = isVisible;
        updateDescription();
        update();
    }
}

void PDFFolderCard::setIconName(const QString& iconName)
{
    if (m_card.iconName != iconName)
    {
        m_card.iconName = iconName;
        update();
    }
}

void PDFFolderCard::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalizedFolderHue(hue);
    if (m_card.hue != hue)
    {
        m_card.hue = hue;
        update();
    }
}

QMenu* PDFFolderCard::getMenu() const
{
    return m_moreButton ? m_moreButton->getMenu() : nullptr;
}

void PDFFolderCard::setMenu(QMenu* menu)
{
    if (m_moreButton)
    {
        m_moreButton->setMenu(menu);
    }
}

void PDFFolderCard::updateDescription()
{
    const QString description = joinTexts({ m_card.path, m_card.metaVisible ? m_card.meta : QString() }, getListPattern());
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

PDFComponentPainter::FolderCard PDFFolderCard::getContent() const
{
    PDFComponentPainter::FolderCard card = m_card;
    card.name = text();
    card.paintNested = false;
    return card;
}

void PDFFolderCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintFolderCard(painter, QRectF(rect), getContent(), getBorderState(state), false);
}

QSize PDFFolderCard::getContentSizeHint() const
{
    return PDFComponentPainter::getFolderCardSize(getContent());
}

QSize PDFFolderCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getFolderCardMinimumSize(getContent());
}

void PDFFolderCard::contextMenuEvent(QContextMenuEvent* event)
{
    showMoreButtonMenu(m_moreButton.data(), event);
}

void PDFFolderCard::requestMoreButtonMenu()
{
    Q_EMIT menuRequested();
}

// -------------------------------------------------------------------- Pinned card

PDFPinnedCard::PDFPinnedCard(QWidget* parent) :
    BaseClass(parent),
    m_star(new PDFStarButton(PDFStarButton::StarSize::Small, this)),
    m_moreButton(new PDFMoreButton(this))
{
    // A column of the star and the menu button at the top of the trailing side
    setTrailingOrientation(Qt::Vertical);
    setTrailingMargins(PDFComponentPainter::getPinnedCardTrailingMargins());
    setTrailingSpacing(PDFComponentPainter::getPinnedCardTrailingSpacing());

    // The document of a pinned card is pinned (the star of the design is filled)
    m_star->setPinned(true);
    addTrailingWidget(m_star.data());
    addTrailingWidget(m_moreButton.data());

    connect(m_star.data(), &PDFStarButton::pinnedChanged, this, &PDFPinnedCard::pinnedChanged);
    connect(m_moreButton.data(), &PDFMoreButton::menuRequested, this, &PDFPinnedCard::menuRequested);
}

PDFPinnedCard::PDFPinnedCard(const QString& name, const QString& path, const QString& progressText, qreal progress, QWidget* parent) :
    PDFPinnedCard(parent)
{
    setName(name);
    m_card.path = path;
    m_card.progressText = progressText;
    m_card.progress = PDFComponentPainter::normalizedProgress(progress);
    updateDescription();
}

void PDFPinnedCard::setName(const QString& name)
{
    setText(name);
}

void PDFPinnedCard::setPath(const QString& path)
{
    if (m_card.path != path)
    {
        m_card.path = path;
        updateDescription();
        update();
    }
}

void PDFPinnedCard::setProgressText(const QString& progressText)
{
    if (m_card.progressText != progressText)
    {
        m_card.progressText = progressText;
        updateDescription();
        update();
    }
}

void PDFPinnedCard::setProgress(qreal progress)
{
    progress = PDFComponentPainter::normalizedProgress(progress);
    if (!qFuzzyCompare(m_card.progress + 1.0, progress + 1.0))
    {
        m_card.progress = progress;
        update();
    }
}

void PDFPinnedCard::setPixmap(const QPixmap& pixmap)
{
    if (!PDFComponentCardsHelper::isSamePixmap(m_card.pixmap, pixmap))
    {
        m_card.pixmap = pixmap;
        update();
    }
}

void PDFPinnedCard::setStatus(Status status)
{
    status = PDFComponentPainter::normalized(status);
    if (m_card.status != status)
    {
        m_card.status = status;
        updateDescription();
        update();
    }
}

void PDFPinnedCard::setLocked(bool isLocked)
{
    if (m_card.locked != isLocked)
    {
        m_card.locked = isLocked;
        updateDescription();
        update();
    }
}

bool PDFPinnedCard::isPinned() const
{
    return m_star && m_star->isPinned();
}

void PDFPinnedCard::setPinned(bool isPinned)
{
    if (m_star)
    {
        m_star->setPinned(isPinned);
    }
}

QMenu* PDFPinnedCard::getMenu() const
{
    return m_moreButton ? m_moreButton->getMenu() : nullptr;
}

void PDFPinnedCard::setMenu(QMenu* menu)
{
    if (m_moreButton)
    {
        m_moreButton->setMenu(menu);
    }
}

void PDFPinnedCard::updateDescription()
{
    // A missing file says so before its folder and has no progress, a locked one says it is protected
    const bool isMissing = m_card.status == Status::Missing;
    const QString lockedText = m_card.locked ? tr("Protected by a password") : QString();
    const QString description = isMissing ? joinTexts({ PDFComponentPainter::getMissingFileText(), m_card.path, lockedText }, getListPattern())
                                          : joinTexts({ m_card.path, m_card.progressText, lockedText }, getListPattern());
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

PDFComponentPainter::PinnedCard PDFPinnedCard::getContent() const
{
    PDFComponentPainter::PinnedCard card = m_card;
    card.name = text();
    card.pinned = isPinned();
    card.paintNested = false;
    return card;
}

void PDFPinnedCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintPinnedCard(painter, QRectF(rect), getContent(), getBorderState(state), false);
}

QSize PDFPinnedCard::getContentSizeHint() const
{
    return PDFComponentPainter::getPinnedCardSize(getContent());
}

QSize PDFPinnedCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getPinnedCardMinimumSize(getContent());
}

void PDFPinnedCard::contextMenuEvent(QContextMenuEvent* event)
{
    showMoreButtonMenu(m_moreButton.data(), event);
}

void PDFPinnedCard::requestMoreButtonMenu()
{
    Q_EMIT menuRequested();
}

// ----------------------------------------------------------- Tool category header

PDFToolCategoryHeader::PDFToolCategoryHeader(QWidget* parent) :
    BaseClass(parent)
{
    // A header, not a button: no focus, no hover
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, false);
    updateDescription();
}

PDFToolCategoryHeader::PDFToolCategoryHeader(const QString& title, const QString& description, const QString& count, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFToolCategoryHeader(parent)
{
    setTitle(title);
    m_header.description = description;
    m_header.count = count;
    m_header.iconName = iconName;
    m_header.hue = PDFComponentPainter::normalized(hue);
    updateDescription();
}

void PDFToolCategoryHeader::setTitle(const QString& title)
{
    setText(title);
}

void PDFToolCategoryHeader::setDescription(const QString& description)
{
    if (m_header.description != description)
    {
        m_header.description = description;
        updateDescription();
        update();
    }
}

void PDFToolCategoryHeader::setCount(const QString& count)
{
    if (m_header.count != count)
    {
        m_header.count = count;
        updateDescription();
        updateGeometry();
        update();
    }
}

void PDFToolCategoryHeader::setIconName(const QString& iconName)
{
    if (m_header.iconName != iconName)
    {
        m_header.iconName = iconName;
        update();
    }
}

void PDFToolCategoryHeader::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_header.hue != hue)
    {
        m_header.hue = hue;
        update();
    }
}

void PDFToolCategoryHeader::setTopRadius(qreal radius)
{
    radius = (qIsFinite(radius) && radius > 0.0) ? radius : 0.0;
    if (!qFuzzyCompare(m_header.topRadius + 1.0, radius + 1.0))
    {
        m_header.topRadius = radius;
        update();
    }
}

void PDFToolCategoryHeader::setTitleData(bool isTitleData)
{
    if (m_header.isTitleData != isTitleData)
    {
        m_header.isTitleData = isTitleData;
        update();
    }
}

void PDFToolCategoryHeader::updateDescription()
{
    // "Merge, split, reorder and rotate pages, 6 tools"
    const QString description = joinTexts({ m_header.description, m_header.count }, getListPattern());
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

PDFComponentPainter::ToolCategoryHeader PDFToolCategoryHeader::getContent() const
{
    PDFComponentPainter::ToolCategoryHeader header = m_header;
    header.title = text();
    return header;
}

void PDFToolCategoryHeader::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintToolCategoryHeader(painter, QRectF(rect), getContent(), state);
}

QSize PDFToolCategoryHeader::getContentSizeHint() const
{
    return PDFComponentPainter::getToolCategoryHeaderSize(getContent());
}

QSize PDFToolCategoryHeader::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getToolCategoryHeaderMinimumSize(getContent());
}

// -------------------------------------------------------------------- Tool choice

PDFToolChoice::PDFToolChoice(QWidget* parent) :
    BaseClass(parent)
{
    // 154 wide, the height hugs the name (46 or 48); a composite widget may
    // divide its width among the choices
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setFocusRingRadius(PDFComponentPainter::getToolChoiceRadius());
    setSelectionBehavior(SelectionBehavior::Deselectable);
}

PDFToolChoice::PDFToolChoice(const QString& name, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFToolChoice(parent)
{
    setName(name);
    m_choice.iconName = iconName;
    m_choice.hue = PDFComponentPainter::normalized(hue);
}

void PDFToolChoice::setName(const QString& name)
{
    setText(name);
}

void PDFToolChoice::setIconName(const QString& iconName)
{
    if (m_choice.iconName != iconName)
    {
        m_choice.iconName = iconName;
        update();
    }
}

void PDFToolChoice::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_choice.hue != hue)
    {
        m_choice.hue = hue;
        update();
    }
}

void PDFToolChoice::setSelected(bool isSelected)
{
    if (!isCheckable())
    {
        return;
    }

    if (isSelected)
    {
        setChecked(true);
    }
    else
    {
        uncheck();
    }
}

PDFComponentPainter::ToolChoice PDFToolChoice::getContent() const
{
    PDFComponentPainter::ToolChoice choice = m_choice;
    choice.name = text();
    return choice;
}

void PDFToolChoice::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintToolChoice(painter, QRectF(rect), getContent(), state);
}

QSize PDFToolChoice::getContentSizeHint() const
{
    return PDFComponentPainter::getToolChoiceSize(getContent());
}

QSize PDFToolChoice::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getToolChoiceMinimumSize(getContent());
}

int PDFToolChoice::computeHeightForWidth(int width) const
{
    return PDFComponentPainter::getToolChoiceHeight(getContent(), width);
}

// ------------------------------------------------------------------ Resource card

PDFResourceCard::PDFResourceCard(QWidget* parent) :
    BaseClass(parent)
{
    // The height follows the width; the cards of a row stretch to the tallest one
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

PDFResourceCard::PDFResourceCard(const QString& title, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFResourceCard(parent)
{
    setTitle(title);
    setDescription(description);
    m_card.iconName = iconName;
    m_card.hue = PDFComponentPainter::normalizedResourceHue(hue);
}

void PDFResourceCard::setTitle(const QString& title)
{
    if (title != text())
    {
        setText(title);
        invalidateHeight();
    }
}

void PDFResourceCard::setDescription(const QString& description)
{
    if (m_card.description != description)
    {
        replaceAccessibleDescription(m_card.description, description);
        m_card.description = description;
        invalidateHeight();
    }
}

void PDFResourceCard::setIconName(const QString& iconName)
{
    // The height follows the width: every change of the content invalidates it
    if (m_card.iconName != iconName)
    {
        m_card.iconName = iconName;
        invalidateHeight();
    }
}

void PDFResourceCard::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalizedResourceHue(hue);
    if (m_card.hue != hue)
    {
        m_card.hue = hue;
        invalidateHeight();
    }
}

int PDFResourceCard::computeHeightForWidth(int width) const
{
    return PDFComponentPainter::getResourceCardHeight(getContent(), width);
}

PDFComponentPainter::ResourceCard PDFResourceCard::getContent() const
{
    PDFComponentPainter::ResourceCard card = m_card;
    card.title = text();
    return card;
}

void PDFResourceCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintResourceCard(painter, QRectF(rect), getContent(), getBorderState(state), false);
}

QSize PDFResourceCard::getContentSizeHint() const
{
    return PDFComponentPainter::getResourceCardSize(getContent());
}

QSize PDFResourceCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getResourceCardMinimumSize(getContent());
}

// ----------------------------------------------------------------- Highlight card

PDFHighlightCard::PDFHighlightCard(QWidget* parent) :
    BaseClass(parent)
{
    // Not interactive: no focus, no hover, the height follows the width
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, false);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

PDFHighlightCard::PDFHighlightCard(const QString& title, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFHighlightCard(parent)
{
    setTitle(title);
    setDescription(description);
    m_card.iconName = iconName;
    m_card.hue = PDFComponentPainter::normalized(hue);
}

void PDFHighlightCard::setTitle(const QString& title)
{
    if (title != text())
    {
        setText(title);
        invalidateHeight();
    }
}

void PDFHighlightCard::setDescription(const QString& description)
{
    if (m_card.description != description)
    {
        replaceAccessibleDescription(m_card.description, description);
        m_card.description = description;
        invalidateHeight();
    }
}

void PDFHighlightCard::setIconName(const QString& iconName)
{
    // The height follows the width: every change of the content invalidates it
    if (m_card.iconName != iconName)
    {
        m_card.iconName = iconName;
        invalidateHeight();
    }
}

void PDFHighlightCard::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_card.hue != hue)
    {
        m_card.hue = hue;
        invalidateHeight();
    }
}

int PDFHighlightCard::computeHeightForWidth(int width) const
{
    return PDFComponentPainter::getHighlightCardHeight(getContent(), width);
}

PDFComponentPainter::HighlightCard PDFHighlightCard::getContent() const
{
    PDFComponentPainter::HighlightCard card = m_card;
    card.title = text();
    return card;
}

void PDFHighlightCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintHighlightCard(painter, QRectF(rect), getContent(), state);
}

QSize PDFHighlightCard::getContentSizeHint() const
{
    return PDFComponentPainter::getHighlightCardSize(getContent());
}

QSize PDFHighlightCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getHighlightCardMinimumSize(getContent());
}

// --------------------------------------------------------------------- Video card

PDFVideoCard::PDFVideoCard(QWidget* parent) :
    BaseClass(parent)
{
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

PDFVideoCard::PDFVideoCard(const QString& title, const QString& description, const QString& duration, const QStringList& tags, QWidget* parent) :
    PDFVideoCard(parent)
{
    setTitle(title);
    m_card.description = description;
    m_card.duration = duration;
    updateDescription();
    setTags(tags);
}

void PDFVideoCard::setTitle(const QString& title)
{
    if (title != text())
    {
        // The title of one line or none moves the tags
        setText(title);
        invalidateHeight();
        layoutTrailingWidgets();
    }
}

void PDFVideoCard::setDescription(const QString& description)
{
    if (m_card.description != description)
    {
        m_card.description = description;
        updateDescription();
        invalidateHeight();
        layoutTrailingWidgets();
    }
}

void PDFVideoCard::setDuration(const QString& duration)
{
    // The height follows the width: every change of the content invalidates it
    if (m_card.duration != duration)
    {
        m_card.duration = duration;
        updateDescription();
        invalidateHeight();
    }
}

void PDFVideoCard::setTags(const QStringList& tags)
{
    // Empty and blank tags are left out, the stored list has the indices of the tag widgets
    PDFComponentPainter::VideoCard probe;
    probe.tags = tags;
    const QStringList shownTags = PDFComponentPainter::getVideoCardTags(probe);

    // The same tags only recreate the tags deleted by the application
    bool areTagsComplete = m_tags.size() == m_card.tags.size();
    for (const QPointer<PDFTag>& tag : std::as_const(m_tags))
    {
        areTagsComplete = areTagsComplete && isOwnTag(tag.data());
    }

    if (m_card.tags != shownTags || !areTagsComplete)
    {
        m_card.tags = shownTags;
        rebuildTags();
        invalidateHeight();
        layoutTrailingWidgets();
    }
}

void PDFVideoCard::setPixmap(const QPixmap& pixmap)
{
    if (!PDFComponentCardsHelper::isSamePixmap(m_card.pixmap, pixmap))
    {
        m_card.pixmap = pixmap;
        invalidateHeight();
    }
}

bool PDFVideoCard::isOwnTag(const PDFTag* tag) const
{
    return tag && tag->parentWidget() == this;
}

bool PDFVideoCard::isHiddenByApplication(const PDFTag* tag)
{
    return tag && tag->isHidden() && !tag->property(PDFComponentCardsHelper::HIDDEN_BY_CARD_PROPERTY).toBool();
}

QList<PDFTag*> PDFVideoCard::getTagWidgets() const
{
    QList<PDFTag*> tags;
    for (const QPointer<PDFTag>& tag : m_tags)
    {
        if (isOwnTag(tag.data()))
        {
            tags << tag.data();
        }
    }
    return tags;
}

void PDFVideoCard::rebuildTags()
{
    // Showing and hiding the tags here is not a change by the application
    QScopedValueRollback<bool> placingGuard(m_isPlacingTags, true);

    // The previous tags are removed later: a slot of a tag may set the tags
    for (const QPointer<PDFTag>& tag : std::as_const(m_tags))
    {
        if (tag)
        {
            tag->removeEventFilter(this);
            tag->hide();
            tag->deleteLater();
        }
    }
    m_tags.clear();
    m_tagTexts.clear();

    // The tags follow the card in the order of the focus: a new child would be
    // the last widget of the window (QWidget::setTabOrder moves it after the card)
    QWidget* previous = this;
    for (const QString& text : std::as_const(m_card.tags))
    {
        PDFTag* tag = new PDFTag(text, this);
        tag->setClickable(true);

        // A click on a tag (also on a disabled one) is not a click on the card
        tag->setAttribute(Qt::WA_NoMousePropagation, true);
        tag->show();
        tag->installEventFilter(this);
        connect(tag, &QAbstractButton::clicked, this, &PDFVideoCard::onTagClicked);
        QWidget::setTabOrder(previous, tag);
        previous = tag;
        m_tags << tag;
        m_tagTexts << text;
    }
}

void PDFVideoCard::onTagClicked()
{
    // QAbstractButton::clicked does not say the button: the sender is the tag
    // (a tag removed by rebuildTags is no longer in the list and reports nothing)
    const PDFTag* tag = qobject_cast<const PDFTag*>(sender());
    if (!tag)
    {
        return;
    }

    for (qsizetype i = 0; i < m_tags.size() && i < m_tagTexts.size(); ++i)
    {
        if (m_tags.at(i).data() == tag)
        {
            // A copy: a slot may set the tags (the list is cleared then)
            const QString text = m_tagTexts.at(i);
            Q_EMIT tagClicked(text);
            return;
        }
    }
}

void PDFVideoCard::layoutTrailingWidgets()
{
    BaseClass::layoutTrailingWidgets();

    // The tags lie in the places of the painter; a tag, which does not fit, is
    // hidden by the card, a tag hidden by the application takes no place
    QScopedValueRollback<bool> placingGuard(m_isPlacingTags, true);
    m_tagLayoutTitle = text();
    const QList<QRectF> rects = PDFComponentPainter::getVideoCardTagRects(QRectF(rect()), getContent(), getComponentState());

    qsizetype index = 0;
    for (const QPointer<PDFTag>& tag : std::as_const(m_tags))
    {
        if (!isOwnTag(tag.data()) || isHiddenByApplication(tag.data()))
        {
            continue;
        }

        const QRectF tagRect = index < rects.size() ? rects.at(index) : QRectF();
        ++index;

        tag->setGeometry(tagRect.toAlignedRect());
        if (tagRect.isEmpty() && !tag->isHidden())
        {
            tag->setProperty(PDFComponentCardsHelper::HIDDEN_BY_CARD_PROPERTY, true);
            tag->hide();
        }
        else if (!tagRect.isEmpty() && tag->isHidden())
        {
            tag->setProperty(PDFComponentCardsHelper::HIDDEN_BY_CARD_PROPERTY, QVariant());
            tag->show();
        }
    }
}

bool PDFVideoCard::event(QEvent* event)
{
    const bool result = BaseClass::event(event);

    if (event->type() == QEvent::ChildRemoved)
    {
        // A tag deleted or moved away by the application leaves its place to the others
        const QObject* child = static_cast<QChildEvent*>(event)->child();
        bool isTag = false;
        for (const QPointer<PDFTag>& tag : std::as_const(m_tags))
        {
            if (!tag || tag.data() == child)
            {
                isTag = true;
                break;
            }
        }

        if (isTag)
        {
            invalidateHeight();
            layoutTrailingWidgets();
        }
    }

    return result;
}

bool PDFVideoCard::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_isPlacingTags && (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent))
    {
        PDFTag* tag = qobject_cast<PDFTag*>(watched);
        if (isOwnTag(tag) && m_tags.contains(tag))
        {
            // The application has shown or hidden the tag: a hidden tag takes no
            // place, a shown one gets its place (or the card hides it again)
            tag->setProperty(PDFComponentCardsHelper::HIDDEN_BY_CARD_PROPERTY, QVariant());
            invalidateHeight();
            QCoreApplication::postEvent(this, new QEvent(QEvent::LayoutRequest));
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFVideoCard::paintEvent(QPaintEvent* event)
{
    // A title set through QAbstractButton::setText moves the tags as well
    if (m_tagLayoutTitle != text())
    {
        layoutTrailingWidgets();
    }

    BaseClass::paintEvent(event);
}

void PDFVideoCard::updateDescription()
{
    // "2:45, A quick tour of the interface and the main features."
    const QString description = joinTexts({ m_card.duration, m_card.description }, getListPattern());
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

int PDFVideoCard::computeHeightForWidth(int width) const
{
    return PDFComponentPainter::getVideoCardHeight(getContent(), width);
}

PDFComponentPainter::VideoCard PDFVideoCard::getContent() const
{
    PDFComponentPainter::VideoCard card = m_card;
    card.title = text();
    card.paintNested = false;

    // The tags, which take their places
    card.tags.clear();
    for (const QPointer<PDFTag>& tag : m_tags)
    {
        if (isOwnTag(tag.data()) && !isHiddenByApplication(tag.data()))
        {
            card.tags << tag->text();
        }
    }
    return card;
}

void PDFVideoCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintVideoCard(painter, QRectF(rect), getContent(), getBorderState(state), false);
}

QSize PDFVideoCard::getContentSizeHint() const
{
    return PDFComponentPainter::getVideoCardSize(getContent());
}

QSize PDFVideoCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getVideoCardMinimumSize(getContent());
}

// ------------------------------------------------------------- Reading theme card

PDFReadingThemeCard::PDFReadingThemeCard(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setSelectionBehavior(SelectionBehavior::ExclusiveStatic);
}

PDFReadingThemeCard::PDFReadingThemeCard(const QString& name, PDFThemeColors::Color pageColor, PDFThemeColors::Color textColor, QWidget* parent) :
    PDFReadingThemeCard(parent)
{
    setName(name);
    setColors(pageColor, textColor);
}

void PDFReadingThemeCard::setName(const QString& name)
{
    setText(name);
}

void PDFReadingThemeCard::setColors(PDFThemeColors::Color pageColor, PDFThemeColors::Color textColor)
{
    // An invalid token would be drawn in the leak color
    pageColor = PDFComponentCardsHelper::getValidColor(pageColor, PDFThemeColors::ReadingPaperPage);
    textColor = PDFComponentCardsHelper::getValidColor(textColor, PDFThemeColors::ReadingPaperText);

    if (m_card.pageColor != pageColor || m_card.textColor != textColor)
    {
        m_card.pageColor = pageColor;
        m_card.textColor = textColor;
        update();
    }
}

void PDFReadingThemeCard::setCustomColors(const QColor& pageColor, const QColor& textColor)
{
    if (m_card.customPageColor != pageColor || m_card.customTextColor != textColor)
    {
        m_card.customPageColor = pageColor;
        m_card.customTextColor = textColor;
        update();
    }
}

void PDFReadingThemeCard::setSelected(bool isSelected)
{
    if (!isCheckable())
    {
        return;
    }

    if (isSelected)
    {
        setChecked(true);
    }
    else
    {
        uncheck();
    }
}

PDFComponentPainter::ReadingThemeCard PDFReadingThemeCard::getContent() const
{
    PDFComponentPainter::ReadingThemeCard card = m_card;
    card.name = text();
    return card;
}

void PDFReadingThemeCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintReadingThemeCard(painter, QRectF(rect), getContent(), state);
}

QSize PDFReadingThemeCard::getContentSizeHint() const
{
    return PDFComponentPainter::getReadingThemeCardSize(getContent());
}

QSize PDFReadingThemeCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getReadingThemeCardMinimumSize(getContent());
}

// ------------------------------------------------------------------- Preview card

PDFPreviewCard::PDFPreviewCard(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setSelectionBehavior(SelectionBehavior::ExclusiveStatic);
}

PDFPreviewCard::PDFPreviewCard(Preview preview, QWidget* parent) :
    PDFPreviewCard(parent)
{
    m_card.preview = PDFPreviewPainter::normalized(preview);
}

PDFPreviewCard::PDFPreviewCard(const QString& name, Preview preview, QWidget* parent) :
    PDFPreviewCard(preview, parent)
{
    setName(name);
}

void PDFPreviewCard::setName(const QString& name)
{
    setText(name);
}

void PDFPreviewCard::setPreview(Preview preview)
{
    preview = PDFPreviewPainter::normalized(preview);
    if (m_card.preview != preview)
    {
        const QString previousName = getDefaultAccessibleName();
        m_card.preview = preview;
        update();

#if QT_CONFIG(accessibility)
        if (accessibleName().isEmpty() && previousName != getDefaultAccessibleName())
        {
            QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
            QAccessible::updateAccessibility(&nameEvent);
        }
#endif
    }
}

void PDFPreviewCard::setSelected(bool isSelected)
{
    if (!isCheckable())
    {
        return;
    }

    if (isSelected)
    {
        setChecked(true);
    }
    else
    {
        uncheck();
    }
}

QString PDFPreviewCard::getShownName() const
{
    return text().isEmpty() ? PDFPreviewPainter::getPreviewName(m_card.preview) : text();
}

QString PDFPreviewCard::getDefaultAccessibleName() const
{
    return getShownName();
}

void PDFPreviewCard::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

#if QT_CONFIG(accessibility)
    if (event->type() == QEvent::LanguageChange && text().isEmpty() && accessibleName().isEmpty())
    {
        // The name of the picture is translated
        QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&nameEvent);
    }
#endif
}

PDFComponentPainter::PreviewCard PDFPreviewCard::getContent() const
{
    PDFComponentPainter::PreviewCard card = m_card;
    card.name = getShownName();
    return card;
}

void PDFPreviewCard::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintPreviewCard(painter, QRectF(rect), getContent(), state);
}

QSize PDFPreviewCard::getContentSizeHint() const
{
    return PDFComponentPainter::getPreviewCardSize(getContent());
}

QSize PDFPreviewCard::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getPreviewCardMinimumSize(getContent());
}

}   // namespace pdf
