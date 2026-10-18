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

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessible>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace pdf
{

PDFRowButton::PDFRowButton(QWidget* parent) :
    BaseClass(FocusRing::Inside, parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setTrailingMargins(PDFComponentPainter::getRowTrailingMargins());
    setTrailingSpacing(PDFComponentPainter::getRowTrailingSpacing());
}

PDFLinkRow::PDFLinkRow(QWidget* parent) :
    BaseClass(parent)
{

}

PDFLinkRow::PDFLinkRow(const QString& title, const QString& subtitle, const QString& iconName, QWidget* parent) :
    BaseClass(parent)
{
    setText(title);
    setSubtitle(subtitle);
    m_row.iconName = iconName;
}

void PDFLinkRow::setTitle(const QString& title)
{
    setText(title);
}

void PDFLinkRow::setSubtitle(const QString& subtitle)
{
    if (m_row.subtitle != subtitle)
    {
        replaceAccessibleDescription(m_row.subtitle, subtitle);
        m_row.subtitle = subtitle;
        updateGeometry();
        update();
    }
}

void PDFLinkRow::setIconName(const QString& iconName)
{
    if (m_row.iconName != iconName)
    {
        m_row.iconName = iconName;
        update();
    }
}

void PDFLinkRow::setIconColor(PDFThemeColors::Color color)
{
    if (static_cast<int>(color) < 0 || static_cast<int>(color) >= static_cast<int>(PDFThemeColors::ColorCount))
    {
        resetIconColor();
        return;
    }

    if (m_row.iconColor != color)
    {
        m_row.iconColor = color;
        update();
    }
}

void PDFLinkRow::resetIconColor()
{
    if (m_row.iconColor.has_value())
    {
        m_row.iconColor.reset();
        update();
    }
}

PDFComponentPainter::LinkRow PDFLinkRow::getContent() const
{
    PDFComponentPainter::LinkRow row = m_row;
    row.title = text();
    return row;
}

void PDFLinkRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintLinkRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFLinkRow::getContentSizeHint() const
{
    return PDFComponentPainter::getLinkRowSize(getContent());
}

QSize PDFLinkRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getLinkRowMinimumSize(getContent());
}

PDFRecentFileRow::PDFRecentFileRow(QWidget* parent) :
    BaseClass(parent),
    m_star(new PDFStarButton(PDFStarButton::StarSize::Small, this)),
    m_moreButton(new PDFMoreButton(this))
{
    addTrailingWidget(m_star.data());
    addTrailingWidget(m_moreButton.data());

    connect(m_star.data(), &PDFStarButton::pinnedChanged, this, &PDFRecentFileRow::pinnedChanged);
    connect(m_moreButton.data(), &PDFMoreButton::menuRequested, this, &PDFRecentFileRow::menuRequested);
}

PDFRecentFileRow::PDFRecentFileRow(const QString& fileName, const QString& path, const QString& time, QWidget* parent) :
    PDFRecentFileRow(parent)
{
    setFileName(fileName);
    setPath(path);
    setTime(time);
}

void PDFRecentFileRow::setFileName(const QString& fileName)
{
    setText(fileName);
}

void PDFRecentFileRow::setPath(const QString& path)
{
    if (m_row.path != path)
    {
        m_row.path = path;
        updateDescription();
        update();
    }
}

void PDFRecentFileRow::setTime(const QString& time)
{
    if (m_row.time != time)
    {
        m_row.time = time;
        updateDescription();
        updateGeometry();
        update();
    }
}

void PDFRecentFileRow::setStatus(Status status)
{
    status = PDFComponentPainter::normalized(status);
    if (m_row.status != status)
    {
        m_row.status = status;
        updateDescription();
        updateGeometry();
        update();
    }
}

bool PDFRecentFileRow::isStarVisible() const
{
    return isNestedShown(m_star.data());
}

void PDFRecentFileRow::setStarVisible(bool isVisible)
{
    // The star hides itself through the layout of the nested widgets of the row
    if (m_star && isStarVisible() != isVisible)
    {
        m_star->setVisible(isVisible);
        updateGeometry();
        update();
    }
}

bool PDFRecentFileRow::isPinned() const
{
    return m_star && m_star->isPinned();
}

void PDFRecentFileRow::setPinned(bool isPinned)
{
    if (m_star)
    {
        m_star->setPinned(isPinned);
    }
}

QMenu* PDFRecentFileRow::getMenu() const
{
    return m_moreButton ? m_moreButton->getMenu() : nullptr;
}

void PDFRecentFileRow::setMenu(QMenu* menu)
{
    if (m_moreButton)
    {
        m_moreButton->setMenu(menu);
    }
}

void PDFRecentFileRow::updateDescription()
{
    // A missing file says so before its folder, it has no time
    const QString pattern = getListPattern();
    QString description;
    if (m_row.status == Status::Missing)
    {
        description = joinTexts({ PDFComponentPainter::getMissingFileText(), m_row.path }, pattern);
    }
    else
    {
        description = joinTexts({ m_row.path, m_row.time }, pattern);
    }

    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

PDFComponentPainter::RecentFileRow PDFRecentFileRow::getContent() const
{
    PDFComponentPainter::RecentFileRow row = m_row;
    row.fileName = text();
    row.starVisible = isStarVisible();
    row.pinned = isPinned();
    row.paintNested = false;
    return row;
}

void PDFRecentFileRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintRecentFileRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFRecentFileRow::getContentSizeHint() const
{
    return PDFComponentPainter::getRecentFileRowSize(getContent());
}

QSize PDFRecentFileRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getRecentFileRowMinimumSize(getContent());
}

void PDFRecentFileRow::contextMenuEvent(QContextMenuEvent* event)
{
    showMoreButtonMenu(m_moreButton.data(), event);
}

void PDFRecentFileRow::requestMoreButtonMenu()
{
    Q_EMIT menuRequested();
}

void PDFRecentFileRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        // The description contains the translated text of a missing file
        updateDescription();
    }
}

PDFRecentToolRow::PDFRecentToolRow(QWidget* parent) :
    BaseClass(parent)
{

}

PDFRecentToolRow::PDFRecentToolRow(const QString& name, const QString& lastUsed, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    BaseClass(parent)
{
    setText(name);
    setLastUsed(lastUsed);
    m_row.iconName = iconName;
    m_row.hue = PDFComponentPainter::normalized(hue);
}

void PDFRecentToolRow::setName(const QString& name)
{
    setText(name);
}

void PDFRecentToolRow::setLastUsed(const QString& lastUsed)
{
    if (m_row.lastUsed != lastUsed)
    {
        replaceAccessibleDescription(m_row.lastUsed, lastUsed);
        m_row.lastUsed = lastUsed;
        update();
    }
}

void PDFRecentToolRow::setIconName(const QString& iconName)
{
    if (m_row.iconName != iconName)
    {
        m_row.iconName = iconName;
        update();
    }
}

void PDFRecentToolRow::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_row.hue != hue)
    {
        m_row.hue = hue;
        update();
    }
}

PDFComponentPainter::RecentToolRow PDFRecentToolRow::getContent() const
{
    PDFComponentPainter::RecentToolRow row = m_row;
    row.name = text();
    return row;
}

void PDFRecentToolRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintRecentToolRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFRecentToolRow::getContentSizeHint() const
{
    return PDFComponentPainter::getRecentToolRowSize(getContent());
}

QSize PDFRecentToolRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getRecentToolRowMinimumSize(getContent());
}

PDFWorkspaceRow::PDFWorkspaceRow(QWidget* parent) :
    BaseClass(parent),
    m_moreButton(new PDFMoreButton(this))
{
    setTrailingMargins(PDFComponentPainter::getWorkspaceRowTrailingMargins());
    addTrailingWidget(m_moreButton.data());
    connect(m_moreButton.data(), &PDFMoreButton::menuRequested, this, &PDFWorkspaceRow::menuRequested);
}

PDFWorkspaceRow::PDFWorkspaceRow(const QString& name, const QString& meta, QWidget* parent) :
    PDFWorkspaceRow(parent)
{
    setName(name);
    setMeta(meta);
}

void PDFWorkspaceRow::setName(const QString& name)
{
    setText(name);
}

void PDFWorkspaceRow::setMeta(const QString& meta)
{
    if (m_row.meta != meta)
    {
        replaceAccessibleDescription(m_row.meta, meta);
        m_row.meta = meta;
        update();
    }
}

void PDFWorkspaceRow::setIconName(const QString& iconName)
{
    if (m_row.iconName != iconName)
    {
        m_row.iconName = iconName;
        update();
    }
}

void PDFWorkspaceRow::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_row.hue != hue)
    {
        m_row.hue = hue;
        update();
    }
}

QMenu* PDFWorkspaceRow::getMenu() const
{
    return m_moreButton ? m_moreButton->getMenu() : nullptr;
}

void PDFWorkspaceRow::setMenu(QMenu* menu)
{
    if (m_moreButton)
    {
        m_moreButton->setMenu(menu);
    }
}

PDFComponentPainter::WorkspaceRow PDFWorkspaceRow::getContent() const
{
    PDFComponentPainter::WorkspaceRow row = m_row;
    row.name = text();
    row.paintNested = false;
    return row;
}

void PDFWorkspaceRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintWorkspaceRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFWorkspaceRow::getContentSizeHint() const
{
    return PDFComponentPainter::getWorkspaceRowSize(getContent());
}

QSize PDFWorkspaceRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getWorkspaceRowMinimumSize(getContent());
}

void PDFWorkspaceRow::contextMenuEvent(QContextMenuEvent* event)
{
    showMoreButtonMenu(m_moreButton.data(), event);
}

void PDFWorkspaceRow::requestMoreButtonMenu()
{
    Q_EMIT menuRequested();
}

PDFToolRow::PDFToolRow(QWidget* parent) :
    BaseClass(parent),
    m_tag(new PDFTag(this)),
    m_star(new PDFStarButton(PDFStarButton::StarSize::Medium, this))
{
    setTrailingMargins(PDFComponentPainter::getToolRowTrailingMargins());
    setTrailingSpacing(PDFComponentPainter::getToolRowTrailingSpacing());

    // The tag is shown only with a text; the star pins the tool
    m_tag->hide();
    m_star->setSubject(PDFStarButton::Subject::Tool);
    addTrailingWidget(m_tag.data());
    addTrailingWidget(m_star.data());

    connect(m_star.data(), &PDFStarButton::pinnedChanged, this, &PDFToolRow::pinnedChanged);
}

PDFToolRow::PDFToolRow(const QString& name, const QString& description, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFToolRow(parent)
{
    setName(name);
    setDescription(description);
    m_row.iconName = iconName;
    m_row.hue = PDFComponentPainter::normalized(hue);
}

void PDFToolRow::setName(const QString& name)
{
    setText(name);
}

void PDFToolRow::setDescription(const QString& description)
{
    if (m_row.description != description)
    {
        replaceAccessibleDescription(m_row.description, description);
        m_row.description = description;
        update();
    }
}

void PDFToolRow::setIconName(const QString& iconName)
{
    if (m_row.iconName != iconName)
    {
        m_row.iconName = iconName;
        update();
    }
}

void PDFToolRow::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_row.hue != hue)
    {
        m_row.hue = hue;
        update();
    }
}

void PDFToolRow::setTag(const QString& tag)
{
    if (m_row.tag != tag)
    {
        m_row.tag = tag;
        if (m_tag)
        {
            m_tag->setText(tag);
            m_tag->setVisible(!tag.isEmpty());
        }
        updateGeometry();
        update();
    }
}

bool PDFToolRow::isPinned() const
{
    return m_star && m_star->isPinned();
}

void PDFToolRow::setPinned(bool isPinned)
{
    if (m_star)
    {
        m_star->setPinned(isPinned);
    }
}

PDFComponentPainter::ToolRow PDFToolRow::getContent() const
{
    PDFComponentPainter::ToolRow row = m_row;
    row.name = text();
    row.pinned = isPinned();
    row.paintNested = false;

    // A tag hidden or deleted by the application leaves its place to the texts
    if (!isNestedShown(m_tag.data()))
    {
        row.tag.clear();
    }
    return row;
}

void PDFToolRow::layoutTrailingWidgets()
{
    BaseClass::layoutTrailingWidgets();

    // The tag, which would reach the badge, is dropped as by the painter
    // (it keeps its visibility, the row may become wider again)
    if (isNestedShown(m_tag.data()) && !PDFComponentPainter::isToolRowTagShown(QRectF(rect()), getContent()))
    {
        m_tag->setGeometry(QRect());
    }
}

void PDFToolRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintToolRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFToolRow::getContentSizeHint() const
{
    return PDFComponentPainter::getToolRowSize(getContent());
}

QSize PDFToolRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getToolRowMinimumSize(getContent());
}

PDFCollectionRow::PDFCollectionRow(QWidget* parent) :
    BaseClass(parent)
{

}

PDFCollectionRow::PDFCollectionRow(const QString& name, const QString& meta, QWidget* parent) :
    BaseClass(parent)
{
    setText(name);
    setMeta(meta);
}

void PDFCollectionRow::setName(const QString& name)
{
    setText(name);
}

void PDFCollectionRow::setMeta(const QString& meta)
{
    if (m_row.meta != meta)
    {
        replaceAccessibleDescription(m_row.meta, meta);
        m_row.meta = meta;
        update();
    }
}

void PDFCollectionRow::setIconName(const QString& iconName)
{
    if (m_row.iconName != iconName)
    {
        m_row.iconName = iconName;
        update();
    }
}

PDFComponentPainter::CollectionRow PDFCollectionRow::getContent() const
{
    PDFComponentPainter::CollectionRow row = m_row;
    row.name = text();
    return row;
}

void PDFCollectionRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintCollectionRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFCollectionRow::getContentSizeHint() const
{
    return PDFComponentPainter::getCollectionRowSize(getContent());
}

QSize PDFCollectionRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getCollectionRowMinimumSize(getContent());
}

PDFToolShortcut::PDFToolShortcut(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusRingRadius(PDFComponentPainter::getToolShortcutRadius());
}

PDFToolShortcut::PDFToolShortcut(const QString& name, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFToolShortcut(parent)
{
    setText(name);
    m_shortcut.iconName = iconName;
    m_shortcut.hue = PDFComponentPainter::normalized(hue);
}

void PDFToolShortcut::setName(const QString& name)
{
    setText(name);
}

void PDFToolShortcut::setIconName(const QString& iconName)
{
    if (m_shortcut.iconName != iconName)
    {
        m_shortcut.iconName = iconName;
        update();
    }
}

void PDFToolShortcut::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_shortcut.hue != hue)
    {
        m_shortcut.hue = hue;
        update();
    }
}

PDFComponentPainter::ToolShortcut PDFToolShortcut::getContent() const
{
    PDFComponentPainter::ToolShortcut shortcut = m_shortcut;
    shortcut.name = text();
    return shortcut;
}

void PDFToolShortcut::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    // The focus frame of the style draws the ring outside, the tile only drops its border
    PDFComponentPainter::State paintedState = state;
    if (isEnabled() && PDFWidgetStyle::isFocusRingVisible(this))
    {
        paintedState.flags |= QStyle::State_HasFocus;
    }

    PDFComponentPainter::paintToolShortcut(painter, QRectF(rect), getContent(), paintedState, false);
}

QSize PDFToolShortcut::getContentSizeHint() const
{
    return PDFComponentPainter::getToolShortcutSize(getContent());
}

QSize PDFToolShortcut::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getToolShortcutMinimumSize(getContent());
}

PDFIssueRow::PDFIssueRow(QWidget* parent) :
    BaseClass(parent)
{

}

PDFIssueRow::PDFIssueRow(const QString& number, const QString& title, const QString& description, const QString& date, QWidget* parent) :
    BaseClass(parent)
{
    m_row.number = number;
    m_row.description = description;
    m_row.date = date;
    setText(title);
    updateDescription();
}

void PDFIssueRow::setNumber(const QString& number)
{
    if (m_row.number != number)
    {
        m_row.number = number;
        update();

#if QT_CONFIG(accessibility)
        if (accessibleName().isEmpty())
        {
            QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
            QAccessible::updateAccessibility(&nameEvent);
        }
#endif
    }
}

void PDFIssueRow::setTitle(const QString& title)
{
    setText(title);
}

void PDFIssueRow::setDescription(const QString& description)
{
    if (m_row.description != description)
    {
        m_row.description = description;
        updateDescription();
        update();
    }
}

void PDFIssueRow::setDate(const QString& date)
{
    if (m_row.date != date)
    {
        m_row.date = date;
        updateDescription();
        updateGeometry();
        update();
    }
}

void PDFIssueRow::setStatusIconName(const QString& iconName)
{
    if (m_row.statusIconName != iconName)
    {
        m_row.statusIconName = iconName;
        update();
    }
}

void PDFIssueRow::updateDescription()
{
    // "Documents protected with AES-256 now open correctly, Sep 12, 2026"
    const QString description = joinTexts({ m_row.description, m_row.date }, getListPattern());
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

void PDFIssueRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateDescription();
    }
}

QString PDFIssueRow::getDefaultAccessibleName() const
{
    // "#612 Crash when opening some encrypted documents"
    return QStringLiteral("%1 %2").arg(m_row.number, text()).trimmed();
}

PDFComponentPainter::IssueRow PDFIssueRow::getContent() const
{
    PDFComponentPainter::IssueRow row = m_row;
    row.title = text();
    return row;
}

void PDFIssueRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintIssueRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFIssueRow::getContentSizeHint() const
{
    return PDFComponentPainter::getIssueRowSize(getContent());
}

QSize PDFIssueRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getIssueRowMinimumSize(getContent());
}

PDFVersionRow::PDFVersionRow(QWidget* parent) :
    BaseClass(parent)
{
    // Without interaction: no focus, no hover, the height follows the width
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, false);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    updateDescription();
}

PDFVersionRow::PDFVersionRow(const QString& version, const QString& date, const QString& summary, QWidget* parent) :
    PDFVersionRow(parent)
{
    setText(version);
    setDate(date);
    setSummary(summary);
}

void PDFVersionRow::setVersion(const QString& version)
{
    setText(version);
}

void PDFVersionRow::setDate(const QString& date)
{
    if (m_row.date != date)
    {
        m_row.date = date;
        updateDescription();
        update();
    }
}

void PDFVersionRow::setSummary(const QString& summary)
{
    if (m_row.summary != summary)
    {
        m_row.summary = summary;
        updateDescription();
        invalidateHeight();
    }
}

void PDFVersionRow::setCurrent(bool isCurrent)
{
    if (m_row.current != isCurrent)
    {
        m_row.current = isCurrent;
        updateDescription();
        update();
    }
}

void PDFVersionRow::setLineVisible(bool isVisible)
{
    if (m_row.lineVisible != isVisible)
    {
        m_row.lineVisible = isVisible;
        update();
    }
}

void PDFVersionRow::updateDescription()
{
    // "Current version. Sep 14, 2026. One unified app, faster loading."
    const QString currentText = m_row.current ? tr("Current version") : QString();
    const QString description = joinTexts({ currentText, m_row.date, m_row.summary }, tr("%1. %2"));
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

void PDFVersionRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateDescription();
    }
}

int PDFVersionRow::computeHeightForWidth(int width) const
{
    // Cached by PDFComponentButton (the summary invalidates it, the version is the text)
    return PDFComponentPainter::getVersionRowHeight(getContent(), width);
}

PDFComponentPainter::VersionRow PDFVersionRow::getContent() const
{
    PDFComponentPainter::VersionRow row = m_row;
    row.version = text();
    return row;
}

void PDFVersionRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintVersionRow(painter, QRectF(rect), getContent(), state);
}

QSize PDFVersionRow::getContentSizeHint() const
{
    return PDFComponentPainter::getVersionRowSize(getContent());
}

QSize PDFVersionRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getVersionRowMinimumSize(getContent());
}

/// Action button of a property row, which draws itself by the state of the row
/// through PDFComponentPainter::paintPropertyRowAction (the place, where the
/// look of the actions is defined): the copy button is drawn only under the
/// pointer of the row, in the state Copied or with the keyboard focus (its
/// place is reserved otherwise)
class PDFPropertyRowButton : public PDFIconButton
{
public:
    using Action = PDFComponentPainter::PropertyAction;

    explicit PDFPropertyRowButton(Action action, QWidget* parent) :
        PDFIconButton(getIconName(action), Size::Small, parent),
        m_action(action)
    {

    }

    static QString getIconName(Action action)
    {
        switch (action)
        {
            case Action::Edit:
                return QStringLiteral("edit");
            case Action::Copy:
                return QStringLiteral("copy");
            case Action::Confirm:
                return QStringLiteral("check");
            case Action::Cancel:
                return QStringLiteral("x");
        }

        return QString();
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        using PropertyState = PDFComponentPainter::PropertyState;

        const PDFPropertyRow* row = qobject_cast<const PDFPropertyRow*>(parentWidget());
        const PropertyState state = row ? row->getPropertyState() : PropertyState::Hover;

        if (m_action == Action::Copy && !PDFComponentPainter::isPropertyRowCopyShown(state, hasFocus()))
        {
            return;
        }

        QPainter painter(this);
        PDFComponentPainter::paintPropertyRowAction(&painter, QRectF(rect()), m_action, state, getPaintedState());
    }

private:
    Action m_action;
};

PDFPropertyRow::PDFPropertyRow(QWidget* parent) :
    BaseClass(parent),
    m_editor(new PDFLineEdit(this)),
    m_editButton(new PDFPropertyRowButton(PDFComponentPainter::PropertyAction::Edit, this)),
    m_copyButton(new PDFPropertyRowButton(PDFComponentPainter::PropertyAction::Copy, this)),
    m_confirmButton(new PDFPropertyRowButton(PDFComponentPainter::PropertyAction::Confirm, this)),
    m_cancelButton(new PDFPropertyRowButton(PDFComponentPainter::PropertyAction::Cancel, this))
{
    // The row is a text, its actions are buttons of their own
    setFocusPolicy(Qt::NoFocus);
    setTrailingMargins(PDFComponentPainter::getPropertyRowTrailingMargins());
    setTrailingSpacing(0);

    m_confirmButton->setIconColor(PDFThemeColors::IconAccent);
    m_editButton->hide();
    m_confirmButton->hide();
    m_cancelButton->hide();

    addTrailingWidget(m_editButton.data());
    addTrailingWidget(m_copyButton.data());
    addTrailingWidget(m_confirmButton.data());
    addTrailingWidget(m_cancelButton.data());

    // The field paints nothing, the row paints its frame below it; the text
    // starts 10 px from the edge of the frame (the ring is counted in the layout)
    m_editor->setFrame(false);
    m_editor->setFont(PDFComponentPainter::getPropertyRowValueFont());
    const int inset = PDFComponentPainter::getPropertyRowEditorTextInset();
    m_editor->setProperty(PDFWidgetStyleProperties::FIELD_TEXT_INSETS, QVariant::fromValue(QMargins(inset, 0, inset, 0)));
    m_editor->hide();

    // Enter, Esc and the focus leaving the row are handled for the field and the
    // actions of the editing
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_editor.data(), m_confirmButton.data(), m_cancelButton.data() })
    {
        widget->installEventFilter(this);
    }

    m_copiedTimer.setSingleShot(true);
    connect(&m_copiedTimer, &QTimer::timeout, this, &PDFPropertyRow::onCopiedTimeout);
    connect(m_copyButton.data(), &QAbstractButton::clicked, this, &PDFPropertyRow::copy);
    connect(m_editButton.data(), &QAbstractButton::clicked, this, &PDFPropertyRow::startEditing);
    connect(m_confirmButton.data(), &QAbstractButton::clicked, this, &PDFPropertyRow::confirmEditing);
    connect(m_cancelButton.data(), &QAbstractButton::clicked, this, &PDFPropertyRow::cancelEditing);

    updateTexts();
    updateActions();
}

PDFPropertyRow::PDFPropertyRow(const QString& key, const QString& value, QWidget* parent) :
    PDFPropertyRow(parent)
{
    setKey(key);
    setValue(value);
}

PDFPropertyRow::PDFPropertyRow(const QString& key, const QString& value, Editability editability, QWidget* parent) :
    PDFPropertyRow(key, value, parent)
{
    setEditable(editability == Editability::Editable);
}

PDFPropertyRow::~PDFPropertyRow()
{
    // The timer and the field are children; nothing may call the row back
    m_copiedTimer.stop();
    for (QWidget* widget : std::initializer_list<QWidget*>{ m_editor.data(), m_confirmButton.data(), m_cancelButton.data() })
    {
        if (widget)
        {
            widget->removeEventFilter(this);
        }
    }
}

void PDFPropertyRow::setKey(const QString& key)
{
    setText(key);

    // The field is named by the key
    if (m_editor)
    {
        m_editor->setAccessibleName(text());
    }
}

void PDFPropertyRow::setValue(const QString& value)
{
    if (m_value != value)
    {
        replaceAccessibleDescription(m_value, value);
        m_value = value;
        update();
    }
}

void PDFPropertyRow::setEditable(bool isEditable)
{
    if (m_isEditable == isEditable)
    {
        return;
    }

    m_isEditable = isEditable;

    if (!isEditable && m_isEditing)
    {
        QPointer<PDFPropertyRow> guard(this);
        cancelEditing();
        if (!guard)
        {
            return;
        }
    }

    QPointer<PDFPropertyRow> guard(this);
    updateActions();
    if (!guard)
    {
        return;
    }
    placeEditor();
    update();
}

bool PDFPropertyRow::startEditing()
{
    if (m_isEditing)
    {
        return true;
    }

    if (!m_isEditable || !isEnabled() || !m_editor)
    {
        return false;
    }

    // The confirmation of a copy is not shown over the field
    m_copiedTimer.stop();
    m_isCopied = false;
    m_isEditing = true;

    QPointer<PDFPropertyRow> guard(this);
    const QString value = m_value;
    m_editor->setText(value);
    if (!guard || !m_isEditing || !m_editor)
    {
        return false;
    }
    m_editor->setAccessibleName(text());
    placeEditor();
    if (!guard || !m_isEditing)
    {
        return false;
    }

    // The field is shown before the buttons are hidden: the focus of the edit
    // button goes to the field, not out of the row
    updateActions();
    if (!guard || !m_isEditing)
    {
        return false;
    }
    if (m_editor)
    {
        m_editor->selectAll();
        if (!guard || !m_isEditing || !m_editor)
        {
            return false;
        }
        m_editor->setFocus(Qt::OtherFocusReason);
    }
    if (!guard || !m_isEditing)
    {
        return false;
    }
    update();

    Q_EMIT editingChanged(true);
    return guard && m_isEditing;
}

void PDFPropertyRow::finishEditing()
{
    QPointer<PDFPropertyRow> guard(this);
    m_isEditing = false;
    updateActions();
    if (guard)
    {
        update();
    }
}

void PDFPropertyRow::confirmEditing()
{
    if (!m_isEditing)
    {
        return;
    }

    // The state is changed first, a slot of the signals (or of the change of
    // the focus) may change or delete the row
    QPointer<PDFPropertyRow> guard(this);
    const QString value = m_editor ? m_editor->text() : m_value;
    const bool isChanged = value != m_value;
    if (isChanged)
    {
        setValue(value);
    }

    finishEditing();
    if (!guard)
    {
        return;
    }

    Q_EMIT editingChanged(false);
    if (guard && isChanged)
    {
        Q_EMIT edited(value);
    }
}

void PDFPropertyRow::cancelEditing()
{
    if (!m_isEditing)
    {
        return;
    }

    QPointer<PDFPropertyRow> guard(this);
    finishEditing();
    if (guard)
    {
        Q_EMIT editingChanged(false);
    }
}

void PDFPropertyRow::copy()
{
    if (!isEnabled() || m_isEditing)
    {
        return;
    }

    // The clipboard notifies its observers synchronously, they may delete the row
    QPointer<PDFPropertyRow> guard(this);
    const QString value = m_value;
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(value);
    }

    if (!guard)
    {
        return;
    }

    // The check is shown for a moment in the place of the copy button; the
    // timer does not run for a hidden row
    if (isVisible())
    {
        m_isCopied = true;
        m_copiedTimer.start(m_copiedDuration);
        updateActions();
        update();
    }

    Q_EMIT copied(value);
}

void PDFPropertyRow::setCopiedDuration(int milliseconds)
{
    m_copiedDuration = qMax(0, milliseconds);
}

void PDFPropertyRow::onCopiedTimeout()
{
    if (m_isCopied)
    {
        m_isCopied = false;
        updateActions();
        update();
    }
}

bool PDFPropertyRow::isHoveredRow() const
{
    return isEnabled() && underMouse();
}

bool PDFPropertyRow::hasFocusInside() const
{
    const QWidget* focusWidget = QApplication::focusWidget();
    return focusWidget && (focusWidget == this || isAncestorOf(focusWidget));
}

PDFComponentPainter::PropertyState PDFPropertyRow::getPropertyState() const
{
    if (m_isEditing)
    {
        return PDFComponentPainter::PropertyState::Editing;
    }

    if (m_isCopied)
    {
        return PDFComponentPainter::PropertyState::Copied;
    }

    return isHoveredRow() ? PDFComponentPainter::PropertyState::Hover : PDFComponentPainter::PropertyState::Default;
}

void PDFPropertyRow::updateActions()
{
    QPointer<PDFPropertyRow> guard(this);
    const bool isEditing = m_isEditing;
    const bool showEdit = m_isEditable && !isEditing;
    const bool showCopy = !isEditing;

    struct Visibility
    {
        QPointer<QWidget> widget;
        bool isVisible;
    };

    const Visibility visibilities[] =
    {
        { m_editor.data(), isEditing },
        { m_editButton.data(), showEdit },
        { m_copyButton.data(), showCopy },
        { m_confirmButton.data(), isEditing },
        { m_cancelButton.data(), isEditing }
    };

    // The widgets of the state are shown first. The keyboard focus of a widget,
    // which is hidden, goes to the widget of the state (the field, the edit or
    // the copy button) before the hiding, so it does not leave the row
    // (QWidget::hide gives the focus to the next widget of the window).
    QPointer<QWidget> focusWidget = QApplication::focusWidget();
    bool isFocusHidden = false;
    for (const Visibility& visibility : visibilities)
    {
        if (!visibility.widget)
        {
            continue;
        }

        if (visibility.isVisible)
        {
            visibility.widget->show();
            if (!guard)
            {
                return;
            }
        }
        else if (focusWidget && (focusWidget == visibility.widget || visibility.widget->isAncestorOf(focusWidget)))
        {
            isFocusHidden = true;
        }
    }

    if (isFocusHidden && isVisible())
    {
        QWidget* target = isEditing ? static_cast<QWidget*>(m_editor.data()) : (showEdit ? static_cast<QWidget*>(m_editButton.data()) : static_cast<QWidget*>(m_copyButton.data()));
        if (target && target->isVisible() && target->isEnabled())
        {
            target->setFocus(Qt::OtherFocusReason);
            if (!guard)
            {
                return;
            }
        }
    }

    for (const Visibility& visibility : visibilities)
    {
        if (visibility.widget && !visibility.isVisible)
        {
            visibility.widget->hide();
            if (!guard)
            {
                return;
            }
        }
    }

    if (m_copyButton)
    {
        const QString copyText = m_isCopied ? tr("Copied") : tr("Copy");
        if (m_copyButton->toolTip() != copyText)
        {
            m_copyButton->setToolTip(copyText);
            m_copyButton->setAccessibleName(copyText);
        }
    }

    // The buttons draw themselves by the state of the row
    for (QWidget* button : std::initializer_list<QWidget*>{ m_editButton.data(), m_copyButton.data(), m_confirmButton.data(), m_cancelButton.data() })
    {
        if (button)
        {
            button->update();
        }
    }
}

void PDFPropertyRow::updateTexts()
{
    struct ButtonText
    {
        PDFIconButton* button;
        QString text;
    };

    for (const ButtonText& buttonText : { ButtonText{ m_editButton.data(), tr("Edit") }, ButtonText{ m_confirmButton.data(), tr("Confirm") }, ButtonText{ m_cancelButton.data(), tr("Cancel") } })
    {
        if (buttonText.button)
        {
            buttonText.button->setToolTip(buttonText.text);
            buttonText.button->setAccessibleName(buttonText.text);
        }
    }

    if (m_editor)
    {
        m_editor->setAccessibleName(text());
    }

    // The copy button changes its text with the state
    if (m_copyButton)
    {
        m_copyButton->setToolTip(QString());
    }
    updateActions();
}

void PDFPropertyRow::placeEditor()
{
    if (m_editor)
    {
        const PDFComponentPainter::State state(QStyle::State_Enabled, layoutDirection());
        m_editor->setGeometry(PDFComponentPainter::getPropertyRowEditorRect(QRectF(rect()), getContent(), state).toAlignedRect());
    }
}

PDFComponentPainter::PropertyRow PDFPropertyRow::getContent() const
{
    PDFComponentPainter::PropertyRow row;
    row.key = text();
    row.value = m_value;
    row.editable = m_isEditable;
    row.state = getPropertyState();
    row.paintNested = false;
    return row;
}

void PDFPropertyRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    // The row is not a button for the pointer, its hover is the state of the content
    PDFComponentPainter::State paintedState = state;
    paintedState.flags &= ~(QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus);
    PDFComponentPainter::paintPropertyRow(painter, QRectF(rect), getContent(), paintedState);
}

QSize PDFPropertyRow::getContentSizeHint() const
{
    return PDFComponentPainter::getPropertyRowSize(getContent());
}

QSize PDFPropertyRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getPropertyRowMinimumSize(getContent());
}

bool PDFPropertyRow::event(QEvent* event)
{
    const bool result = BaseClass::event(event);

    switch (event->type())
    {
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
            updateActions();
            break;

        case QEvent::ChildRemoved:
            // The field deleted by the application ends the editing
            if (m_isEditing && !m_editor)
            {
                cancelEditing();
            }
            break;

        default:
            break;
    }

    return result;
}

bool PDFPropertyRow::eventFilter(QObject* watched, QEvent* event)
{
    const bool isEditingWidget = watched && (watched == m_editor.data() || watched == m_confirmButton.data() || watched == m_cancelButton.data());
    if (!isEditingWidget || !m_isEditing)
    {
        return BaseClass::eventFilter(watched, event);
    }

    // A widget being deleted (the destructor of QWidget takes its focus away,
    // while the pointers to it are still set) is only QWidget now: nothing of it
    // is called, the editing ends by the event ChildRemoved after its deletion
    if ((watched == m_editor.data() && !qobject_cast<QLineEdit*>(watched)) ||
        (watched != m_editor.data() && !qobject_cast<PDFIconButton*>(watched)))
    {
        return BaseClass::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::KeyPress:
        case QEvent::ShortcutOverride:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            const int key = keyEvent->key();
            const bool isEnter = key == Qt::Key_Return || key == Qt::Key_Enter;
            const bool isEscape = key == Qt::Key_Escape;

            if ((isEnter || isEscape) && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
            {
                // The keys belong to the editing, not to the default button or to
                // the rejection of a dialog: Enter confirms (on the cancel button
                // it cancels), Esc cancels
                event->accept();
                if (event->type() == QEvent::KeyPress && !keyEvent->isAutoRepeat())
                {
                    if (isEscape || watched == m_cancelButton.data())
                    {
                        cancelEditing();
                    }
                    else
                    {
                        confirmEditing();
                    }
                }
                return true;
            }
            break;
        }

        case QEvent::FocusOut:
        {
            // The focus leaving the row confirms the editing at once (as the
            // editors of the item views commit their data), before a click of the
            // OK button of a dialog closes it. The new focus widget is already
            // known. The context menu of the field and the deactivation of the
            // window keep the editing.
            const Qt::FocusReason reason = static_cast<const QFocusEvent*>(event)->reason();
            if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason && !hasFocusInside())
            {
                QPointer<QObject> watchedGuard(watched);
                confirmEditing();
                if (!watchedGuard)
                {
                    // A slot has deleted the row with the field, the event ends here
                    return true;
                }
            }
            break;
        }

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFPropertyRow::keyPressEvent(QKeyEvent* event)
{
    // The keys come from the nested widgets (the row has no focus). The arrows
    // and the page keys, which the field does not use, keep the focus in it.
    switch (event->key())
    {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            if (m_isEditing)
            {
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    BaseClass::keyPressEvent(event);
}

void PDFPropertyRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    placeEditor();
}

void PDFPropertyRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LayoutDirectionChange:
            placeEditor();
            break;

        case QEvent::LanguageChange:
            updateTexts();
            break;

        case QEvent::EnabledChange:
            // A disabled row cancels the editing and the confirmation of a copy
            if (!isEnabled())
            {
                m_copiedTimer.stop();
                m_isCopied = false;
                QPointer<PDFPropertyRow> guard(this);
                cancelEditing();
                if (!guard)
                {
                    return;
                }
            }
            updateActions();
            break;

        default:
            break;
    }
}

void PDFPropertyRow::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    // The timer does not run for a hidden row
    m_copiedTimer.stop();
    if (m_isCopied)
    {
        m_isCopied = false;
        updateActions();
    }

    // A row hidden by the application (or with its window) cancels the editing;
    // a minimized window keeps it
    if (!event->spontaneous())
    {
        cancelEditing();
    }
}

/// Geometry of the navigation items
class PDFNavItemHelper
{
public:
    PDFNavItemHelper() = delete;

    static constexpr int HOME_WIDTH = 190;
    static constexpr int HOME_HEIGHT = 38;
    static constexpr int SETTINGS_WIDTH = 300;
    static constexpr int SETTINGS_HEIGHT = 52;
    static constexpr int PADDING = 12;
    static constexpr int ICON = 20;
    static constexpr int HOME_GAP = 10;
    static constexpr int SETTINGS_GAP = 12;
    static constexpr qreal RADIUS = 10.0;

    static constexpr int NAME_LINE = 16;        ///< Line of Semi Bold 13
    static constexpr int SUBTITLE_LINE = 13;    ///< Line of Regular 11
    static constexpr int TEXT_GAP = 1;

    /// Line between two groups of a navigation: the line of 1 px lies 10 px
    /// from the items around it, 6 px inside of the column of the items
    static constexpr int SEPARATOR_HEIGHT = 17;
    static constexpr int SEPARATOR_LINE = 8;
    static constexpr int SEPARATOR_INSET = 6;

    /// Returns the rectangle of the item in the direction of the state (the
    /// logical rectangle aligned to whole pixels)
    static QRectF place(const PDFComponentPainter::State& state, const QRectF& shape, const QRectF& logical)
    {
        return state.visualRect(shape, QRectF(logical.toAlignedRect()));
    }
};

/// Line between two groups of a navigation
class PDFNavSeparator : public QWidget
{
public:
    explicit PDFNavSeparator(QWidget* parent) :
        QWidget(parent)
    {
        setFixedHeight(PDFNavItemHelper::SEPARATOR_HEIGHT);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const QRectF line(PDFNavItemHelper::SEPARATOR_INSET, PDFNavItemHelper::SEPARATOR_LINE, width() - 2 * PDFNavItemHelper::SEPARATOR_INSET, 1.0);
        PDFStylePainter::fillRect(&painter, line, PDFThemeColors::BorderSeparator);
    }
};

PDFNavItem::PDFNavItem(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFNavItem::PDFNavItem(const QString& text, const QString& iconName, Kind kind, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName),
    m_kind(kind == Kind::Settings ? Kind::Settings : Kind::Home)
{
    setText(text);
    init();
}

void PDFNavItem::init()
{
    // The current page: a click on it does nothing (decision 10 of components.md)
    setSelectionBehavior(SelectionBehavior::ExclusiveStatic);
}

void PDFNavItem::setKind(Kind kind)
{
    if (kind != Kind::Home && kind != Kind::Settings)
    {
        kind = Kind::Home;
    }

    if (m_kind != kind)
    {
        m_kind = kind;
        updateGeometry();
        update();
    }
}

void PDFNavItem::setIconName(const QString& iconName)
{
    if (m_iconName != iconName)
    {
        m_iconName = iconName;
        update();
    }
}

void PDFNavItem::setSubtitle(const QString& subtitle)
{
    if (m_subtitle != subtitle)
    {
        // The description of the item is replaced only, when it was the subtitle
        replaceAccessibleDescription(m_subtitle, subtitle);
        m_subtitle = subtitle;
        update();
    }
}

QSize PDFNavItem::getContentSizeHint() const
{
    if (m_kind == Kind::Settings)
    {
        return QSize(PDFNavItemHelper::SETTINGS_WIDTH, PDFNavItemHelper::SETTINGS_HEIGHT);
    }

    return QSize(PDFNavItemHelper::HOME_WIDTH, PDFNavItemHelper::HOME_HEIGHT);
}

QSize PDFNavItem::getContentMinimumSizeHint() const
{
    // The texts are elided, the item keeps its height
    const int height = m_kind == Kind::Settings ? PDFNavItemHelper::SETTINGS_HEIGHT : PDFNavItemHelper::HOME_HEIGHT;
    return QSize(2 * PDFNavItemHelper::PADDING + PDFNavItemHelper::ICON, height);
}

void PDFNavItem::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    const QRectF shape(rect);
    const PDFStylePainter::Radii radii(PDFNavItemHelper::RADIUS);
    const bool enabled = state.isEnabled();
    const bool selected = state.isChecked();

    // The selected item does not react to the pointer (the state has no hover and press for it)
    if (selected)
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgAccentSubtle);
    }
    else if (state.isPressed())
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgPressed);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgHover);
    }

    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, shape, radii);
    }

    PDFThemeColors::Color iconColor = PDFThemeColors::IconDefault;
    PDFThemeColors::Color textColor = m_kind == Kind::Settings ? PDFThemeColors::TextPrimary : PDFThemeColors::TextSecondary;
    PDFThemeColors::Color subtitleColor = PDFThemeColors::TextMuted;

    if (!enabled)
    {
        iconColor = PDFThemeColors::IconDisabled;
        textColor = PDFThemeColors::TextDisabled;
        subtitleColor = PDFThemeColors::TextDisabled;
    }
    else if (selected)
    {
        iconColor = PDFThemeColors::IconAccent;
        textColor = PDFThemeColors::TextAccent;
    }

    const qreal left = shape.left();
    const qreal top = shape.top();
    const qreal height = shape.height();
    const qreal gap = m_kind == Kind::Settings ? PDFNavItemHelper::SETTINGS_GAP : PDFNavItemHelper::HOME_GAP;
    const QRectF iconRect(left + PDFNavItemHelper::PADDING, top + (height - PDFNavItemHelper::ICON) / 2.0, PDFNavItemHelper::ICON, PDFNavItemHelper::ICON);
    PDFStylePainter::icon(painter, PDFNavItemHelper::place(state, shape, iconRect), m_iconName, iconColor);

    const qreal textLeft = left + PDFNavItemHelper::PADDING + PDFNavItemHelper::ICON + gap;
    const qreal textWidth = qMax(0.0, shape.right() - textLeft - PDFNavItemHelper::PADDING);
    const int alignment = Qt::AlignVCenter | state.leadingAlignment() | Qt::TextSingleLine;

    if (m_kind == Kind::Settings)
    {
        // Name and subtitle form a block of 30 px in the middle of the item
        const qreal blockHeight = PDFNavItemHelper::NAME_LINE + PDFNavItemHelper::TEXT_GAP + PDFNavItemHelper::SUBTITLE_LINE;
        const qreal blockTop = top + qRound((height - blockHeight) / 2.0);
        const QFont nameFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold);
        const QFont subtitleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);

        const QRectF nameRect(textLeft, blockTop, textWidth, PDFNavItemHelper::NAME_LINE);
        const QRectF subtitleRect(textLeft, blockTop + PDFNavItemHelper::NAME_LINE + PDFNavItemHelper::TEXT_GAP, textWidth, PDFNavItemHelper::SUBTITLE_LINE);
        PDFStylePainter::text(painter, PDFNavItemHelper::place(state, shape, nameRect), alignment, PDFComponentPainter::elidedText(text(), nameFont, textWidth), nameFont, PDFStylePainter::color(textColor));
        PDFStylePainter::text(painter, PDFNavItemHelper::place(state, shape, subtitleRect), alignment, PDFComponentPainter::elidedText(m_subtitle, subtitleFont, textWidth), subtitleFont, PDFStylePainter::color(subtitleColor));
        return;
    }

    const QFont font = PDFThemeFonts::getFont(selected ? PDFThemeFonts::Role::BodySemiBold : PDFThemeFonts::Role::Body);
    const QRectF textRect(textLeft, top, textWidth, height);
    PDFStylePainter::text(painter, PDFNavItemHelper::place(state, shape, textRect), alignment, PDFComponentPainter::elidedText(text(), font, textWidth), font, PDFStylePainter::color(textColor));
}

PDFNavList::PDFNavList(QWidget* parent) :
    PDFNavList(PDFNavItem::Kind::Home, parent)
{

}

PDFNavList::PDFNavList(PDFNavItem::Kind kind, QWidget* parent) :
    BaseClass(parent),
    m_layout(new QVBoxLayout(this)),
    m_kind(kind)
{
    // Items of a group are 2 px apart
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    m_layout->addStretch(1);
    m_group.setExclusive(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    connect(&m_group, &QButtonGroup::idToggled, this, &PDFNavList::onGroupIdToggled);
}

void PDFNavList::onGroupIdToggled(int id, bool isChecked)
{
    if (isChecked)
    {
        Q_EMIT currentIndexChanged(static_cast<int>(m_group.buttons().indexOf(m_group.button(id))));
    }
}

int PDFNavList::addItem(const QString& text, const QString& iconName, const QString& subtitle)
{
    PDFNavItem* item = new PDFNavItem(text, iconName, m_kind, this);
    item->setSubtitle(subtitle);
    item->installEventFilter(this);

    const int index = getCount();
    m_group.addButton(item);

    // The stretch stays the last item of the layout (a layout deleted by the
    // application lays nothing out)
    if (m_layout)
    {
        m_layout->insertWidget(m_layout->count() - 1, item);
    }

    if (index == 0)
    {
        item->setChecked(true);
    }

    return index;
}

void PDFNavList::addGroup()
{
    // A separator is only a part of the layout (a layout deleted by the application lays nothing out)
    if (m_layout)
    {
        m_layout->insertWidget(m_layout->count() - 1, new PDFNavSeparator(this));
    }
}

int PDFNavList::getCount() const
{
    return static_cast<int>(m_group.buttons().size());
}

PDFNavItem* PDFNavList::getItem(int index) const
{
    return qobject_cast<PDFNavItem*>(m_group.buttons().value(index));
}

int PDFNavList::getCurrentIndex() const
{
    return static_cast<int>(m_group.buttons().indexOf(m_group.checkedButton()));
}

void PDFNavList::setCurrentIndex(int index)
{
    if (PDFNavItem* item = getItem(index))
    {
        item->setChecked(true);
    }
}

bool PDFNavList::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const int key = keyEvent->key();
        PDFNavItem* item = qobject_cast<PDFNavItem*>(watched);

        if (item && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Home || key == Qt::Key_End))
        {
            // The arrows move the selection to the next enabled item, also into another group
            const int count = getCount();
            const int current = static_cast<int>(m_group.buttons().indexOf(item));
            int step = key == Qt::Key_Up || key == Qt::Key_End ? -1 : 1;
            int index = current;

            if (key == Qt::Key_Home)
            {
                index = -1;
            }
            else if (key == Qt::Key_End)
            {
                index = count;
            }

            for (index += step; index >= 0 && index < count; index += step)
            {
                PDFNavItem* target = getItem(index);
                if (target && target->isEnabled() && target->isVisible())
                {
                    target->setFocus(Qt::TabFocusReason);
                    target->setChecked(true);
                    break;
                }
            }

            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf
