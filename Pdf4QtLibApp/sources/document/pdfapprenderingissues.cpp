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

#include "pdfapprenderingissues.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace pdf
{

/// Helpers of the rendering issues (file-local)
class PDFAppRenderingIssuesHelper
{
public:
    PDFAppRenderingIssuesHelper() = delete;

    /// Gap between the popover and the item of the status bar (198:5950 ends 6 px above the status bar)
    static constexpr int POPOVER_GAP = 6;

    /// Name of the property of a row holding its page
    static constexpr const char* PAGE_PROPERTY = "pdf4qtIssuePage";

    static bool isBefore(const PDFAppRenderingIssue& left, const PDFAppRenderingIssue& right)
    {
        return left.pageIndex < right.pageIndex;
    }

    /// Prepares an item of the status bar (Status bar item, Tone=Warning, alert-triangle)
    static void initItem(QToolButton* item)
    {
        item->setProperty(PDFWidgetStyleProperties::STATUS_ITEM, true);
        item->setProperty(PDFWidgetStyleProperties::TONE, QStringLiteral("warning"));
        item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        item->setIcon(PDFThemeIcons::getIcon(QStringLiteral("alert-triangle")));
        item->setIconSize(QSize(14, 14));
        item->setCursor(Qt::PointingHandCursor);
        item->setFocusPolicy(Qt::TabFocus);
        item->hide();
    }
};

PDFAppRenderingIssuesPanel::PDFAppRenderingIssuesPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("renderingIssuesPanel"));
    setFixedWidth(WIDTH);

    // Panel 198:5950: padding 16, sections 10 apart
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(10);

    // Header: the title, the number of the issues and the close button
    QHBoxLayout* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);
    m_title = new PDFLabel(tr("Rendering issues"), PDFThemeFonts::Role::TileTitle, PDFThemeColors::TextPrimary, this);
    m_title->setFont(PDFThemeFonts::getFont(QFont::DemiBold, 14));
    m_count = new PDFTag(QStringLiteral("0"), this);
    m_count->setObjectName(QStringLiteral("issueCount"));
    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setAccessibleName(tr("Close"));
    connect(m_closeButton.data(), &QAbstractButton::clicked, this, &PDFAppRenderingIssuesPanel::closeRequested);
    header->addWidget(m_title);
    header->addWidget(m_count);
    header->addStretch(1);
    header->addWidget(m_closeButton);
    layout->addLayout(header);

    // The issues (2 px apart), scrolled when there are many of them
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("issueList"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list = new QWidget(m_scrollArea);
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);
    m_more = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, m_list);
    m_more->setObjectName(QStringLiteral("moreIssues"));
    m_more->setWordWrap(true);
    m_more->setContentsMargins(10, 4, 10, 4);
    m_more->hide();
    m_listLayout->addWidget(m_more);
    m_listLayout->addStretch(1);
    m_scrollArea->setWidget(m_list);
    layout->addWidget(m_scrollArea);

    QFrame* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("divider"));
    divider->setFrameShape(QFrame::HLine);
    layout->addWidget(divider);

    PDFLabel* hint = new PDFLabel(tr("Click an issue to show the page."), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    hint->setObjectName(QStringLiteral("hint"));
    layout->addWidget(hint);
}

PDFAppRenderingIssuesPanel::~PDFAppRenderingIssuesPanel() = default;

PDFIconButton* PDFAppRenderingIssuesPanel::getCloseButton() const
{
    return m_closeButton.data();
}

void PDFAppRenderingIssuesPanel::setIssues(const std::vector<PDFAppRenderingIssue>& issues)
{
    // The rows are replaced (the old ones are deleted later: a row may be the sender of a signal)
    for (const QPointer<QWidget>& row : m_rows)
    {
        if (row)
        {
            row->hide();
            row->deleteLater();
        }
    }
    m_rows.clear();

    const QLocale locale;
    const int count = int(qMin<size_t>(issues.size(), size_t(MAXIMUM_ROWS)));
    for (int i = 0; i < count; ++i)
    {
        const PDFAppRenderingIssue& issue = issues[i];
        PDFRenderingIssueRow* row = new PDFRenderingIssueRow(tr("Page %1").arg(locale.toString(issue.pageIndex + 1)), issue.message, issue.severity, m_list);
        row->setObjectName(QStringLiteral("issueRow"));
        row->setProperty(PDFAppRenderingIssuesHelper::PAGE_PROPERTY, QVariant::fromValue<qlonglong>(issue.pageIndex));
        row->installEventFilter(this);
        connect(row, &QAbstractButton::clicked, this, &PDFAppRenderingIssuesPanel::onRowClicked);
        m_listLayout->insertWidget(i, row);
        m_rows.emplace_back(row);
    }

    if (m_count)
    {
        m_count->setText(locale.toString(qulonglong(issues.size())));
    }
    if (m_more)
    {
        const int hidden = int(issues.size()) - count;
        m_more->setText(hidden == 1 ? tr("%n more issue is not listed.", nullptr, hidden) : tr("%n more issues are not listed.", nullptr, hidden));
        m_more->setVisible(hidden > 0);
    }
    updateListHeight();
}

void PDFAppRenderingIssuesPanel::setOuterInsets(int horizontal, int vertical)
{
    m_horizontalInset = qBound(0, horizontal, PADDING);
    const int verticalInset = qBound(0, vertical, PADDING);
    setFixedWidth(WIDTH - 2 * m_horizontalInset);
    if (QLayout* panelLayout = layout())
    {
        panelLayout->setContentsMargins(PADDING - m_horizontalInset, PADDING - verticalInset, PADDING - m_horizontalInset, PADDING - verticalInset);
    }
    updateListHeight();
}

void PDFAppRenderingIssuesPanel::setMaximumListHeight(int height)
{
    m_maximumListHeight = qMax(46, height);
    updateListHeight();
}

std::vector<QWidget*> PDFAppRenderingIssuesPanel::getRows() const
{
    std::vector<QWidget*> rows;
    for (const QPointer<QWidget>& row : m_rows)
    {
        if (row)
        {
            rows.push_back(row.data());
        }
    }
    return rows;
}

void PDFAppRenderingIssuesPanel::focusFirstRow()
{
    const std::vector<QWidget*> rows = getRows();
    if (!rows.empty())
    {
        rows.front()->setFocus(Qt::TabFocusReason);
    }
    else if (m_closeButton)
    {
        m_closeButton->setFocus(Qt::TabFocusReason);
    }
}

void PDFAppRenderingIssuesPanel::updateListHeight()
{
    if (!m_scrollArea || !m_list)
    {
        return;
    }

    // The list is as high as its rows, at most the maximal height (it scrolls then)
    const int width = WIDTH - 2 * PADDING;
    int height = 0;
    for (const QWidget* row : getRows())
    {
        height += row->heightForWidth(width) + (height > 0 ? 2 : 0);
    }
    if (m_more && !m_more->isHidden())
    {
        height += m_more->heightForWidth(width) + 2;
    }
    m_scrollArea->setFixedHeight(qBound(0, height, m_maximumListHeight));
}

void PDFAppRenderingIssuesPanel::onRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }

    bool ok = false;
    const qlonglong pageIndex = row->property(PDFAppRenderingIssuesHelper::PAGE_PROPERTY).toLongLong(&ok);
    if (ok)
    {
        Q_EMIT issueActivated(PDFInteger(pageIndex));
    }
}

bool PDFAppRenderingIssuesPanel::eventFilter(QObject* watched, QEvent* event)
{
    // The arrows move the focus between the rows (the menu would take them)
    if (event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const bool isDown = keyEvent->key() == Qt::Key_Down;
        const bool isUp = keyEvent->key() == Qt::Key_Up;
        if (isDown || isUp)
        {
            const std::vector<QWidget*> rows = getRows();
            const auto it = std::find(rows.cbegin(), rows.cend(), watched);
            if (it != rows.cend())
            {
                const qsizetype index = std::distance(rows.cbegin(), it) + (isDown ? 1 : -1);
                if (index >= 0 && index < qsizetype(rows.size()))
                {
                    rows[index]->setFocus(Qt::TabFocusReason);
                    if (m_scrollArea)
                    {
                        m_scrollArea->ensureWidgetVisible(rows[index], 0, 0);
                    }
                }
                return true;
            }
        }
    }

    return BaseClass::eventFilter(watched, event);
}

PDFAppRenderingIssues::PDFAppRenderingIssues(PDFAppDocumentView* view, PDFAppDocument* document, QObject* parent) :
    BaseClass(parent),
    m_view(view),
    m_document(document)
{
    // The errors come page by page, while the pages are compiled: one update for many of them
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(0);
    connect(&m_updateTimer, &QTimer::timeout, this, &PDFAppRenderingIssues::onUpdateTimeout);

    if (m_view)
    {
        m_statusItem = new QToolButton();
        m_statusItem->setObjectName(QStringLiteral("statusIssues"));
        PDFAppRenderingIssuesHelper::initItem(m_statusItem);
        connect(m_statusItem.data(), &QAbstractButton::clicked, this, &PDFAppRenderingIssues::onStatusItemClicked);
        m_view->addStatusItem(m_statusItem, PDFAppDocumentStatusBar::Area::Trailing);

        if (PDFWidget* widget = m_view->getPdfWidget())
        {
            connect(widget, &PDFWidget::pageRenderingErrorsChanged, this, &PDFAppRenderingIssues::onRenderingErrorsChanged);
        }
    }

    if (m_document)
    {
        connect(m_document.data(), &PDFAppDocument::documentChanged, this, &PDFAppRenderingIssues::onDocumentChanged);
    }

    updateItems();
}

PDFAppRenderingIssues::~PDFAppRenderingIssues()
{
    // The popover is a window of its own: it goes with the controller
    if (m_popover)
    {
        m_popover->disconnect(this);
        m_popover->hide();
        delete m_popover.data();
    }
}

QToolButton* PDFAppRenderingIssues::getStatusItem() const
{
    return m_statusItem.data();
}

QMenu* PDFAppRenderingIssues::getPopover() const
{
    return m_popover.data();
}

PDFAppRenderingIssuesPanel* PDFAppRenderingIssues::getPanel() const
{
    return m_panel.data();
}

std::vector<PDFAppRenderingIssue> PDFAppRenderingIssues::getIssues() const
{
    std::vector<PDFAppRenderingIssue> issues;
    const PDFWidget* widget = m_view ? m_view->getPdfWidget() : nullptr;
    if (!widget)
    {
        return issues;
    }

    for (const auto& pageErrors : *widget->getPageRenderingErrors())
    {
        for (const PDFRenderError& error : pageErrors.second)
        {
            PDFAppRenderingIssue issue;
            issue.pageIndex = pageErrors.first;
            issue.message = error.message.left(1024);
            issue.severity = getSeverity(int(error.type));
            issues.push_back(std::move(issue));
        }
    }

    std::stable_sort(issues.begin(), issues.end(), &PDFAppRenderingIssuesHelper::isBefore);
    return issues;
}

int PDFAppRenderingIssues::getIssueCount() const
{
    const PDFWidget* widget = m_view ? m_view->getPdfWidget() : nullptr;
    return widget ? widget->getPageRenderingErrorCount() : 0;
}

QToolButton* PDFAppRenderingIssues::createStatusItem(QWidget* parent)
{
    QToolButton* item = new QToolButton(parent);
    item->setObjectName(QStringLiteral("statusIssues"));
    PDFAppRenderingIssuesHelper::initItem(item);
    connect(item, &QAbstractButton::clicked, this, &PDFAppRenderingIssues::onAdditionalItemClicked);
    m_additionalItems.emplace_back(item);
    updateItems();
    return item;
}

bool PDFAppRenderingIssues::showPopover()
{
    if (isPopoverVisible())
    {
        return true;
    }

    const std::vector<PDFAppRenderingIssue> issues = getIssues();
    if (issues.empty() || !m_statusItem || !m_statusItem->isVisible())
    {
        return false;
    }

    // A previous popover, which is being closed, is gone already
    if (m_popover)
    {
        m_popover->disconnect(this);
        m_popover->deleteLater();
    }

    // Popover of the style (a menu with the panel as its only item): the
    // shadow, the closing by Esc and by a click outside
    QMenu* popover = new QMenu(m_statusItem->window());
    popover->setObjectName(QStringLiteral("renderingIssuesPopover"));
    popover->setProperty(PDFWidgetStyleProperties::POPOVER, true);
    popover->setAccessibleName(tr("Rendering issues"));

    PDFAppRenderingIssuesPanel* panel = new PDFAppRenderingIssuesPanel(popover);
    QStyle* style = popover->style();
    const int frame = style->pixelMetric(QStyle::PM_MenuPanelWidth, nullptr, popover);
    panel->setOuterInsets(style->pixelMetric(QStyle::PM_MenuHMargin, nullptr, popover) + frame, style->pixelMetric(QStyle::PM_MenuVMargin, nullptr, popover) + frame);
    const int windowHeight = m_statusItem->window()->height();
    panel->setMaximumListHeight(qBound(46, windowHeight - 260, 480));
    panel->setIssues(issues);
    connect(panel, &PDFAppRenderingIssuesPanel::issueActivated, this, &PDFAppRenderingIssues::onIssueActivated);
    connect(panel, &PDFAppRenderingIssuesPanel::closeRequested, this, &PDFAppRenderingIssues::hidePopover);

    QWidgetAction* action = new QWidgetAction(popover);
    action->setDefaultWidget(panel);
    popover->addAction(action);
    connect(popover, &QMenu::aboutToHide, this, &PDFAppRenderingIssues::onPopoverAboutToHide);

    m_popover = popover;
    m_panel = panel;

    // Above the item, aligned with its trailing edge (198:5950), 6 px apart
    const QRect itemRect(m_statusItem->mapToGlobal(QPoint(0, 0)), m_statusItem->size());
    const QRect anchor = itemRect.adjusted(0, -PDFAppRenderingIssuesHelper::POPOVER_GAP, 0, 0);
    popover->setProperty(PDFWidgetStyleProperties::POPUP_ANCHOR, anchor);
    const QSize size = popover->sizeHint();
    const bool isRightToLeft = m_statusItem->layoutDirection() == Qt::RightToLeft;
    const QPoint position(isRightToLeft ? anchor.left() : anchor.right() + 1 - size.width(), anchor.top() - size.height());
    popover->popup(position);

    QPointer<PDFAppRenderingIssuesPanel> guard(panel);
    if (guard)
    {
        guard->focusFirstRow();
    }
    return isPopoverVisible();
}

void PDFAppRenderingIssues::hidePopover()
{
    if (m_popover)
    {
        m_popover->hide();
    }
}

bool PDFAppRenderingIssues::isPopoverVisible() const
{
    return m_popover && m_popover->isVisible();
}

QString PDFAppRenderingIssues::getStatusText(int count)
{
    const int validCount = qMax(0, count);
    return validCount == 1 ? tr("%n rendering issue", nullptr, validCount) : tr("%n rendering issues", nullptr, validCount);
}

PDFComponentPainter::IssueSeverity PDFAppRenderingIssues::getSeverity(int renderErrorType)
{
    switch (RenderErrorType(renderErrorType))
    {
        case RenderErrorType::Error:
            return PDFComponentPainter::IssueSeverity::Error;
        case RenderErrorType::Warning:
        case RenderErrorType::NotImplemented:
        case RenderErrorType::NotSupported:
            return PDFComponentPainter::IssueSeverity::Warning;
        case RenderErrorType::Information:
            return PDFComponentPainter::IssueSeverity::Information;
    }
    return PDFComponentPainter::IssueSeverity::Warning;
}

void PDFAppRenderingIssues::onRenderingErrorsChanged()
{
    m_updateTimer.start();
}

void PDFAppRenderingIssues::onDocumentChanged()
{
    // PDFWidget clears its errors with a new document (without a signal)
    m_updateTimer.start();
}

void PDFAppRenderingIssues::onUpdateTimeout()
{
    updateItems();

    // An open popover shows the new issues (it closes, when there are none)
    if (m_panel && isPopoverVisible())
    {
        const std::vector<PDFAppRenderingIssue> issues = getIssues();
        if (issues.empty())
        {
            hidePopover();
        }
        else
        {
            m_panel->setIssues(issues);
            if (m_popover)
            {
                m_popover->adjustSize();
            }
        }
    }
}

void PDFAppRenderingIssues::onStatusItemClicked()
{
    if (isPopoverVisible())
    {
        hidePopover();
        return;
    }
    showPopover();
}

void PDFAppRenderingIssues::onAdditionalItemClicked()
{
    Q_EMIT statusItemClicked();
}

void PDFAppRenderingIssues::onIssueActivated(PDFInteger pageIndex)
{
    // The popover closes first (the row, which has sent the signal, is deleted later)
    QPointer<PDFAppRenderingIssues> guard(this);
    hidePopover();
    if (guard && m_view)
    {
        m_view->goToPage(pageIndex);
    }
}

void PDFAppRenderingIssues::onPopoverAboutToHide()
{
    // The popover is deleted after it has been hidden (it may be hidden from a slot of its own row)
    if (m_popover)
    {
        m_popover->disconnect(this);
        m_popover->deleteLater();
    }
    m_popover = nullptr;
    m_panel = nullptr;

    if (m_statusItem && m_statusItem->isVisible())
    {
        m_statusItem->setFocus(Qt::PopupFocusReason);
    }
}

void PDFAppRenderingIssues::updateItems()
{
    const int count = getIssueCount();
    const bool countChanged = count != m_count;
    m_count = count;

    const QString text = getStatusText(count);
    std::vector<QToolButton*> items;
    if (m_statusItem)
    {
        items.push_back(m_statusItem.data());
    }
    for (const QPointer<QToolButton>& item : m_additionalItems)
    {
        if (item)
        {
            items.push_back(item.data());
        }
    }
    m_additionalItems.erase(std::remove(m_additionalItems.begin(), m_additionalItems.end(), QPointer<QToolButton>()), m_additionalItems.end());

    for (QToolButton* item : items)
    {
        item->setText(text);
        item->setToolTip(tr("Show the rendering issues"));
        item->setAccessibleName(text);
        item->setVisible(count > 0);
    }

    if (countChanged)
    {
        Q_EMIT issuesChanged();
    }
}

}   // namespace pdf
