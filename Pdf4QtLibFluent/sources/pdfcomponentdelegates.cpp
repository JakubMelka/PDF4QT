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

#include "pdfcomponentdelegates.h"
#include "pdfstylepainterguard.h"
#include "pdfthememanager.h"
#include "pdftooltip.h"
#include "pdfwidgetstyle.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDate>
#include <QDateTime>
#include <QHeaderView>
#include <QHelpEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmapCache>
#include <QScrollBar>
#include <QTextDocument>
#include <QTime>
#include <QToolTip>
#include <QTreeView>

#include <iterator>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace pdf
{

/// Helper functions of the delegates
class PDFComponentDelegatesHelper
{
public:
    PDFComponentDelegatesHelper() = delete;

    /// Largest number of the rows of a parent searched for conflicts of shortcuts
    static constexpr int MAX_CONFLICT_ROWS = 100000;

    /// Largest number of the rows and columns searched for the delegates of a view
    static constexpr int MAX_DELEGATE_SECTIONS = 1000;

    /// Largest number of the remembered sizes of the items
    static constexpr int MAX_CACHED_SIZES = 4096;

    /// Largest size of a pixmap taken from a QIcon
    static constexpr int MAX_ICON_PIXMAP = 512;

    /// Returns true, if both indices are the same item (the same row of the same parent of the same model)
    static bool isSameItem(const QModelIndex& first, const QModelIndex& second)
    {
        return first.isValid() && second.isValid() && first.model() == second.model() && first.row() == second.row() &&
               first.column() == second.column() && first.parent() == second.parent();
    }

    /// Returns true, if the item can be used: the view and the item are enabled
    static bool isItemEnabled(const QAbstractItemView* view, const QModelIndex& index)
    {
        if (!index.isValid() || !index.flags().testFlag(Qt::ItemIsEnabled))
        {
            return false;
        }

        return !view || view->isEnabled();
    }

    /// Returns true, if the index belongs to the model of the view (or no view is given)
    static bool isIndexOfView(const QAbstractItemView* view, const QModelIndex& index)
    {
        return index.isValid() && (!view || index.model() == view->model());
    }

    /// Returns the position of the event in its widget
    static QPoint getPosition(const QEvent* event)
    {
        switch (event->type())
        {
            case QEvent::MouseMove:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
                return static_cast<const QMouseEvent*>(event)->position().toPoint();

            case QEvent::HoverEnter:
            case QEvent::HoverMove:
            case QEvent::HoverLeave:
                return static_cast<const QHoverEvent*>(event)->position().toPoint();

            default:
                break;
        }

        return QPoint();
    }

    /// Returns true, if the key event is the shortcut: one of the combinations of
    /// PDFShortcutRecorder::getMatchingKeyCombinations (the recorded combination -
    /// Ctrl+Alt with the base key on Windows -, the combination of the event and
    /// the combination of the other platforms), always without the keypad
    /// modifier (the Enter of the keypad is Enter, its digits are digits)
    static bool isShortcutKey(const QKeySequence& shortcut, const QKeyEvent* event)
    {
        if (shortcut.isEmpty())
        {
            return false;
        }

        for (const QKeyCombination& combination : PDFShortcutRecorder::getMatchingKeyCombinations(event))
        {
            if (shortcut == QKeySequence(combination))
            {
                return true;
            }
        }

        return false;
    }

    /// Returns true, if the class of the view is a class of Qt or of this
    /// library (it has no context menu of its own)
    static bool isLibraryView(const QAbstractItemView* view)
    {
        const QLatin1StringView className(view->metaObject()->className());
        return className.startsWith(QLatin1Char('Q')) || className.startsWith(QLatin1StringView("pdf::PDF"));
    }

    /// Returns true, if the left button is held now (the state of the application)
    static bool isLeftButtonHeld()
    {
        return QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    }

    /// Appends the range of the value of MatchRangesRole (a QPoint: start,
    /// length; another type or an invalid range is left out)
    static void appendMatchRange(QList<PDFComponentPainter::TextRange>& ranges, const QVariant& item)
    {
        if (item.typeId() == QMetaType::QPoint)
        {
            const QPoint point = item.toPoint();
            if (point.x() >= 0 && point.y() > 0)
            {
                ranges << PDFComponentPainter::TextRange{ point.x(), point.y() };
            }
        }
    }
};

/// Action of the menu of an item (PDFRowDelegate::addActionsToMenu): it
/// remembers the delegate, the view and the item and triggers the action of
/// the delegate. It belongs to the menu, so it is deleted with it.
class PDFRowDelegateMenuAction : public QAction
{
public:
    explicit PDFRowDelegateMenuAction(const QString& text, PDFRowDelegate* delegate, QAbstractItemView* view, const QModelIndex& index, int id, QObject* parent) :
        QAction(text, parent),
        m_delegate(delegate),
        m_view(view),
        m_hasView(view != nullptr),
        m_index(index),
        m_id(id)
    {
        connect(this, &QAction::triggered, this, &PDFRowDelegateMenuAction::onTriggered);
    }

private:
    void onTriggered()
    {
        // The slots of the action may delete the menu with this action, so the
        // data are copied first and no member is used after the call
        const QPointer<PDFRowDelegate> delegate = m_delegate;
        const QPointer<QAbstractItemView> view = m_view;
        const QPersistentModelIndex index = m_index;
        const int id = m_id;

        // A menu of a view, which has been destroyed meanwhile, does nothing (review C14)
        if (delegate && index.isValid() && (!m_hasView || view))
        {
            delegate->triggerAction(view.data(), index, id);
        }
    }

    QPointer<PDFRowDelegate> m_delegate;
    QPointer<QAbstractItemView> m_view;
    bool m_hasView = false;
    QPersistentModelIndex m_index;
    int m_id = -1;
};

PDFRowDelegate::PDFRowDelegate(QObject* parent) :
    BaseClass(parent)
{
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFRowDelegate::onThemeChanged);

    if (QAbstractItemView* view = qobject_cast<QAbstractItemView*>(parent))
    {
        watchView(view);
    }
}

PDFRowDelegate::~PDFRowDelegate()
{
    // The views, which still exist, lose the filters (a destroyed view has been
    // removed by its signal destroyed, before its children were deleted)
    for (WatchedView& watched : m_views)
    {
        uninstall(watched);
    }
    m_views.clear();
}

QList<PDFRowDelegate::Action> PDFRowDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QList<Action>();
}

int PDFRowDelegate::getActionAt(const QStyleOptionViewItem& option, const QModelIndex& index, const QPointF& position) const
{
    if (!index.isValid())
    {
        return -1;
    }

    for (const Action& action : getActions(option, index))
    {
        if (action.enabled && !action.rect.isEmpty() && action.rect.contains(position))
        {
            return action.id;
        }
    }

    return -1;
}

PDFComponentPainter::State PDFRowDelegate::getItemState(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    using Helper = PDFComponentDelegatesHelper;

    QStyle::State flags = option.state & (QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_Selected);
    if (!hasSelection())
    {
        flags &= ~QStyle::State_Selected;
    }

    // The press of the item itself (not of an action inside of it)
    const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(option.widget);
    if (view && view == m_trackedView && m_pressedAction < 0 && m_pressedInside && Helper::isSameItem(index, m_pressedIndex))
    {
        flags |= QStyle::State_Sunken;
    }

    // The focus ring after the keyboard (a composed option without a view has it by its flag)
    if (option.state.testFlag(QStyle::State_HasFocus) && (!option.widget || PDFWidgetStyle::isFocusRingVisible(option.widget)))
    {
        flags |= QStyle::State_HasFocus;
    }

    return PDFComponentPainter::State(flags, option.direction == Qt::RightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
}

QStyle::State PDFRowDelegate::getActionFlags(const QStyleOptionViewItem& option, const QModelIndex& index, int action) const
{
    using Helper = PDFComponentDelegatesHelper;

    const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(option.widget);
    if (!view || view != m_trackedView || action < 0 || !option.state.testFlag(QStyle::State_Enabled))
    {
        return QStyle::State_None;
    }

    QStyle::State flags = QStyle::State_None;
    if (m_hoverAction == action && Helper::isSameItem(index, m_hoverIndex))
    {
        flags |= QStyle::State_MouseOver;
    }
    if (m_pressedAction == action && m_pressedInside && Helper::isSameItem(index, m_pressedIndex))
    {
        flags |= QStyle::State_Sunken;
    }
    return flags;
}

QModelIndex PDFRowDelegate::getPressedIndex() const
{
    return m_pressedIndex;
}

int PDFRowDelegate::getPressedAction() const
{
    return m_pressedIndex.isValid() ? m_pressedAction : -1;
}

int PDFRowDelegate::getHoveredAction() const
{
    return m_hoverIndex.isValid() ? m_hoverAction : -1;
}

bool PDFRowDelegate::isContextMenuEnabled() const
{
    return m_contextMenuMode != ContextMenuMode::Never;
}

void PDFRowDelegate::setContextMenuEnabled(bool enabled)
{
    m_contextMenuMode = enabled ? ContextMenuMode::Always : ContextMenuMode::Never;
}

int PDFRowDelegate::addActionsToMenu(QMenu* menu, QAbstractItemView* view, const QModelIndex& index)
{
    using Helper = PDFComponentDelegatesHelper;

    if (!menu || !Helper::isIndexOfView(view, index))
    {
        return 0;
    }

    const QStyleOptionViewItem option = getViewOption(view, index);
    const bool isEnabled = Helper::isItemEnabled(view, index);

    int count = 0;
    for (const Action& action : getActions(option, index))
    {
        // The shortcut is shown in the column of the shortcuts of the menu (after a tab)
        QString text = action.text;
        if (!action.shortcut.isEmpty())
        {
            text += QLatin1Char('\t') + action.shortcut.toString(QKeySequence::NativeText);
        }

        QAction* menuAction = new PDFRowDelegateMenuAction(text, this, view, index, action.id, menu);
        menu->addAction(menuAction);
        menuAction->setEnabled(isEnabled && action.enabled);
        ++count;
    }

    return count;
}

QMenu* PDFRowDelegate::createContextMenu(QAbstractItemView* view, const QModelIndex& index, QWidget* parent)
{
    if (!PDFComponentDelegatesHelper::isIndexOfView(view, index))
    {
        return nullptr;
    }

    QMenu* menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose, true);
    if (addActionsToMenu(menu, view, index) == 0)
    {
        delete menu;
        return nullptr;
    }
    return menu;
}

bool PDFRowDelegate::isActionAvailable(QAbstractItemView* view, const QModelIndex& index, int action) const
{
    using Helper = PDFComponentDelegatesHelper;

    // An index of another model than the model of the view is refused (review C6)
    if (!Helper::isIndexOfView(view, index) || !Helper::isItemEnabled(view, index))
    {
        return false;
    }

    for (const Action& item : getActions(getViewOption(view, index), index))
    {
        if (item.id == action)
        {
            return item.enabled;
        }
    }

    return false;
}

void PDFRowDelegate::triggerAction(QAbstractItemView* view, const QModelIndex& index, int action)
{
    if (isActionAvailable(view, index, action))
    {
        m_lastActionView = view;
        performAction(view, index, action);
    }
}

QAbstractItemView* PDFRowDelegate::getLastActionView() const
{
    return m_lastActionView.data();
}

void PDFRowDelegate::performAction(QAbstractItemView* view, const QModelIndex& index, int action)
{
    Q_UNUSED(view);
    Q_EMIT actionTriggered(index, action);
}

QRect PDFRowDelegate::getActionRect(QAbstractItemView* view, const QModelIndex& index, int action) const
{
    if (!view || !PDFComponentDelegatesHelper::isIndexOfView(view, index))
    {
        return QRect();
    }

    for (const Action& item : getActions(getViewOption(view, index), index))
    {
        if (item.id == action)
        {
            return item.rect.toAlignedRect();
        }
    }

    return QRect();
}

PDFRowDelegate::WatchedView* PDFRowDelegate::findWatched(const QObject* view)
{
    if (!view)
    {
        return nullptr;
    }

    for (WatchedView& watched : m_views)
    {
        if (watched.view.data() == view)
        {
            return &watched;
        }
    }

    return nullptr;
}

void PDFRowDelegate::installViewport(WatchedView& watched, QWidget* viewport)
{
    if (watched.viewport == viewport)
    {
        return;
    }

    // The old viewport (replaced by setViewport, still alive) loses the filter
    if (watched.viewport)
    {
        watched.viewport->removeEventFilter(this);
        disconnect(watched.viewport, &QObject::destroyed, this, &PDFRowDelegate::onWatchedDestroyed);
        if (watched.isHoverSet)
        {
            watched.viewport->setAttribute(Qt::WA_Hover, false);
        }
        if (m_trackedViewport == watched.viewport)
        {
            m_trackedViewport = viewport;
        }
    }

    watched.viewport = viewport;
    watched.isHoverSet = false;
    watched.isFilterReinstallPending = false;
    watched.viewportWidth = -1;

    if (viewport)
    {
        viewport->installEventFilter(this);
        connect(viewport, &QObject::destroyed, this, &PDFRowDelegate::onWatchedDestroyed, Qt::UniqueConnection);

        // The actions react to the pointer without a pressed button
        if (!viewport->testAttribute(Qt::WA_Hover) && !viewport->hasMouseTracking())
        {
            viewport->setAttribute(Qt::WA_Hover, true);
            watched.isHoverSet = true;
        }
        watched.viewportWidth = viewport->width();
    }
}

void PDFRowDelegate::uninstall(WatchedView& watched)
{
    // Only the objects, which still exist (QPointer), are touched
    if (watched.headerConnection)
    {
        disconnect(watched.headerConnection);
        watched.headerConnection = QMetaObject::Connection();
    }

    if (watched.viewport)
    {
        watched.viewport->removeEventFilter(this);
        disconnect(watched.viewport, &QObject::destroyed, this, &PDFRowDelegate::onWatchedDestroyed);
        if (watched.isHoverSet)
        {
            watched.viewport->setAttribute(Qt::WA_Hover, false);
        }
    }

    if (watched.view)
    {
        watched.view->removeEventFilter(this);
        disconnect(watched.view, &QObject::destroyed, this, &PDFRowDelegate::onWatchedDestroyed);
    }

    watched.view = nullptr;
    watched.viewport = nullptr;
    watched.header = nullptr;
    watched.isHoverSet = false;
}

void PDFRowDelegate::watchView(QAbstractItemView* view)
{
    if (!view)
    {
        return;
    }

    if (WatchedView* watched = findWatched(view))
    {
        // A viewport replaced by setViewport is watched instead of the old one
        if (watched->viewport != view->viewport())
        {
            installViewport(*watched, view->viewport());
        }
        return;
    }

    // Entries of the destroyed views are dropped
    for (auto it = m_views.begin(); it != m_views.end();)
    {
        it = it->view ? std::next(it) : m_views.erase(it);
    }

    WatchedView watched;
    watched.view = view;
    view->installEventFilter(this);

    // The signal destroyed of a widget comes before its children are deleted, so
    // the viewport (the first child of the view) still exists then (review C1)
    connect(view, &QObject::destroyed, this, &PDFRowDelegate::onWatchedDestroyed, Qt::UniqueConnection);

    // The heights of the items, which follow the width, after a resize of a column
    if (QTreeView* treeView = qobject_cast<QTreeView*>(view))
    {
        watched.header = treeView->header();
        if (watched.header)
        {
            watched.headerConnection = connect(watched.header.data(), &QHeaderView::sectionResized, this, &PDFRowDelegate::onHeaderSectionResized);
        }
    }

    installViewport(watched, view->viewport());
    m_views.push_back(watched);
}

void PDFRowDelegate::unwatchView(QAbstractItemView* view)
{
    for (auto it = m_views.begin(); it != m_views.end(); ++it)
    {
        if (it->view.data() == view && view)
        {
            if (m_trackedView == view)
            {
                clearPress();
                resetHover();
                m_trackedView = nullptr;
                m_trackedViewport = nullptr;
            }

            uninstall(*it);
            m_views.erase(it);
            return;
        }
    }
}

bool PDFRowDelegate::isWatching(const QAbstractItemView* view) const
{
    if (!view)
    {
        return false;
    }

    for (const WatchedView& watched : m_views)
    {
        if (watched.view.data() == view)
        {
            return true;
        }
    }

    return false;
}

int PDFRowDelegate::getWatchedViewCount() const
{
    int count = 0;
    for (const WatchedView& watched : m_views)
    {
        if (!watched.view.isNull())
        {
            ++count;
        }
    }
    return count;
}

void PDFRowDelegate::onHeaderSectionResized()
{
    // The heights of the items, which follow the width, after a resize of a column
    if (hasHeightForWidth())
    {
        invalidateSizes();
    }
}

void PDFRowDelegate::installPendingViewportFilters()
{
    // The filter of the delegate is installed again on the viewports set by
    // setViewport, after the scroll area has installed its own filter on them
    // (a filter installed later runs first); a view, which is no longer
    // watched, has no entry
    for (WatchedView& watched : m_views)
    {
        if (watched.isFilterReinstallPending)
        {
            watched.isFilterReinstallPending = false;
            if (watched.viewport)
            {
                watched.viewport->installEventFilter(this);
            }
        }
    }
}

void PDFRowDelegate::ensureWatched(const QWidget* widget) const
{
    // paint and sizeHint are const functions of Qt, the watching is a side
    // effect of the first of them (the view is alive, it paints or lays out)
    if (QAbstractItemView* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(widget)))
    {
        const_cast<PDFRowDelegate*>(this)->watchView(view);
    }
}

void PDFRowDelegate::onWatchedDestroyed(QObject* object)
{
    // No function of the destroyed object is called: a view is in the destructor
    // of QWidget (its viewport still exists), a viewport is being deleted
    for (auto it = m_views.begin(); it != m_views.end(); ++it)
    {
        if (it->view.data() == object)
        {
            if (m_trackedView.data() == object)
            {
                m_pressedIndex = QPersistentModelIndex();
                m_pressedAction = -1;
                m_pressedInside = false;
                m_pressedRect = QRect();
                m_hoverIndex = QPersistentModelIndex();
                m_hoverAction = -1;
                m_hoverRect = QRect();
                m_hasHoverPosition = false;
                m_trackedView = nullptr;
                m_trackedViewport = nullptr;
            }

            if (it->headerConnection)
            {
                disconnect(it->headerConnection);
            }

            // The viewport is alive (it is deleted after the signal), it loses the filter
            if (it->viewport)
            {
                it->viewport->removeEventFilter(this);
                disconnect(it->viewport, &QObject::destroyed, this, &PDFRowDelegate::onWatchedDestroyed);
            }

            m_views.erase(it);
            return;
        }

        if (it->viewport.data() == object)
        {
            // A viewport replaced by setViewport: the new one is installed by the
            // event ChildAdded of the view or at the next paint
            if (m_trackedViewport.data() == object)
            {
                m_trackedViewport = nullptr;
            }
            it->viewport = nullptr;
            it->isHoverSet = false;
            return;
        }
    }
}

bool PDFRowDelegate::isUsedByView(const QAbstractItemView* view) const
{
    if (!view)
    {
        return false;
    }

    if (view->itemDelegate() == this)
    {
        return true;
    }

    // The delegates of the columns and of the rows (Qt does not list them)
    const QAbstractItemModel* model = view->model();
    if (!model)
    {
        return false;
    }

    const QModelIndex root = view->rootIndex();
    const int columns = qMin(model->columnCount(root), PDFComponentDelegatesHelper::MAX_DELEGATE_SECTIONS);
    for (int column = 0; column < columns; ++column)
    {
        if (view->itemDelegateForColumn(column) == this)
        {
            return true;
        }
    }

    const int rows = qMin(model->rowCount(root), PDFComponentDelegatesHelper::MAX_DELEGATE_SECTIONS);
    for (int row = 0; row < rows; ++row)
    {
        if (view->itemDelegateForRow(row) == this)
        {
            return true;
        }
    }

    return false;
}

void PDFRowDelegate::disconnectNotify(const QMetaMethod& signal)
{
    BaseClass::disconnectNotify(signal);

    // A view, which stops to use the delegate (setItemDelegate of another
    // delegate), disconnects the signals of the delegate: the views are checked
    // later in the event loop (the view is in the middle of the change now)
    static const QMetaMethod sizeHintChangedSignal = QMetaMethod::fromSignal(&QAbstractItemDelegate::sizeHintChanged);
    if (signal == sizeHintChangedSignal || !signal.isValid())
    {
        QMetaObject::invokeMethod(this, &PDFRowDelegate::checkWatchedViews, Qt::QueuedConnection);
    }
}

void PDFRowDelegate::checkWatchedViews()
{
    std::vector<QAbstractItemView*> unused;
    for (const WatchedView& watched : m_views)
    {
        if (watched.view && !isUsedByView(watched.view.data()))
        {
            unused.push_back(watched.view.data());
        }
    }

    for (QAbstractItemView* view : unused)
    {
        unwatchView(view);
    }
}

void PDFRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!painter)
    {
        return;
    }

    ensureWatched(option.widget);

    const PDFStylePainterGuard guard(painter);
    paintItem(painter, option, index, getItemState(option, index));
}

QSize PDFRowDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    ensureWatched(option.widget);

    // The size of the model wins (as QStyledItemDelegate)
    if (index.isValid())
    {
        const QVariant sizeHintValue = index.data(Qt::SizeHintRole);
        if (sizeHintValue.typeId() == QMetaType::QSize)
        {
            const QSize size = sizeHintValue.toSize();
            if (size.isValid())
            {
                return size;
            }
        }
    }

    return getItemSize(option, index, getItemWidth(option, index));
}

QWidget* PDFRowDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // The rows are not edited by the generic editors of Qt, whose geometry is the
    // text of the generic item (over the icon and the badge, review C16)
    Q_UNUSED(parent);
    Q_UNUSED(option);
    Q_UNUSED(index);
    return nullptr;
}

std::optional<QSize> PDFRowDelegate::getCachedSize(const QString& key) const
{
    const auto it = m_sizeCache.constFind(key);
    if (it != m_sizeCache.cend())
    {
        return it.value();
    }

    return std::nullopt;
}

QSize PDFRowDelegate::setCachedSize(const QString& key, const QSize& size) const
{
    if (m_sizeCache.size() >= PDFComponentDelegatesHelper::MAX_CACHED_SIZES)
    {
        m_sizeCache.clear();
    }
    m_sizeCache.insert(key, size);
    return size;
}

void PDFRowDelegate::invalidateSizes()
{
    m_sizeCache.clear();
    Q_EMIT sizeHintChanged(QModelIndex());
}

bool PDFRowDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event && event->type() == QEvent::ToolTip)
    {
        if (view && PDFComponentDelegatesHelper::isIndexOfView(view, index))
        {
            // The tooltip of the action under the pointer (the tooltip of the design,
            // with the shortcut of the action)
            for (const Action& action : getActions(option, index))
            {
                if (!action.rect.isEmpty() && action.rect.contains(event->pos()))
                {
                    const QString text = action.toolTip.isEmpty() ? action.text : action.toolTip;
                    if (!text.isEmpty())
                    {
                        PDFToolTip::showText(event->globalPos(), text, action.shortcut.toString(QKeySequence::NativeText), QString(), view->viewport());
                        return true;
                    }
                }
            }

            // The tooltip of the item (a rich text is left to the tooltip of Qt);
            // lines after the first one are the description
            const QString text = index.data(Qt::ToolTipRole).toString();
            if (!text.isEmpty() && !Qt::mightBeRichText(text))
            {
                const qsizetype newLine = text.indexOf(QLatin1Char('\n'));
                PDFToolTip::showText(event->globalPos(), newLine >= 0 ? text.left(newLine).trimmed() : text,
                                     QString(), newLine >= 0 ? text.mid(newLine + 1).trimmed() : QString(), view->viewport());
                return true;
            }
        }

        // Also over an empty area of the view: the tooltip of an action does not
        // stay over the rows below it (review C11)
        PDFToolTip::hideText();
    }

    return BaseClass::helpEvent(event, view, option, index);
}

QString PDFRowDelegate::getDisplayString(const QVariant& value, const QLocale& locale)
{
    switch (value.typeId())
    {
        case QMetaType::QString:
            return value.toString();

        case QMetaType::QDate:
            return locale.toString(value.toDate(), QLocale::ShortFormat);

        case QMetaType::QTime:
            return locale.toString(value.toTime(), QLocale::ShortFormat);

        case QMetaType::QDateTime:
            return locale.toString(value.toDateTime(), QLocale::ShortFormat);

        case QMetaType::Double:
        case QMetaType::Float:
            return locale.toString(value.toDouble(), 'g', 6);

        case QMetaType::Int:
        case QMetaType::Short:
            return locale.toString(value.toInt());

        case QMetaType::Long:
        case QMetaType::LongLong:
            return locale.toString(value.toLongLong());

        case QMetaType::UInt:
        case QMetaType::UShort:
        case QMetaType::ULong:
        case QMetaType::ULongLong:
            return locale.toString(value.toULongLong());

        default:
            break;
    }

    return value.canConvert<QString>() ? value.toString() : QString();
}

QString PDFRowDelegate::getString(const QModelIndex& index, int role, const QString& defaultValue)
{
    if (!index.isValid())
    {
        return defaultValue;
    }

    const QVariant value = index.data(role);
    if (!value.isValid() || value.isNull())
    {
        return defaultValue;
    }

    // A date or a number of the model is formatted by the locale (review C25)
    const QString text = getDisplayString(value, QLocale());
    if (text.isNull() && value.typeId() != QMetaType::QString)
    {
        return defaultValue;
    }
    return text;
}

bool PDFRowDelegate::getBool(const QModelIndex& index, int role, bool defaultValue)
{
    if (!index.isValid())
    {
        return defaultValue;
    }

    const QVariant value = index.data(role);
    if (value.typeId() == QMetaType::Bool)
    {
        return value.toBool();
    }

    bool isNumber = false;
    const int number = value.toInt(&isNumber);
    return isNumber ? number != 0 : defaultValue;
}

qreal PDFRowDelegate::getReal(const QModelIndex& index, int role, qreal defaultValue)
{
    if (!index.isValid())
    {
        return defaultValue;
    }

    bool isNumber = false;
    const qreal number = index.data(role).toReal(&isNumber);
    return (isNumber && qIsFinite(number)) ? number : defaultValue;
}

int PDFRowDelegate::getInt(const QModelIndex& index, int role, int defaultValue)
{
    if (!index.isValid())
    {
        return defaultValue;
    }

    bool isNumber = false;
    const int number = index.data(role).toInt(&isNumber);
    return isNumber ? number : defaultValue;
}

PDFAccentHue PDFRowDelegate::getHue(const QModelIndex& index, int role, PDFAccentHue defaultValue)
{
    const int value = getInt(index, role, static_cast<int>(defaultValue));
    if (value < static_cast<int>(PDFAccentHue::Blue) || value > static_cast<int>(PDFAccentHue::Neutral))
    {
        return defaultValue;
    }

    return static_cast<PDFAccentHue>(value);
}

QPixmap PDFRowDelegate::getPixmap(const QModelIndex& index)
{
    if (!index.isValid())
    {
        return QPixmap();
    }

    for (const int role : { int(PDFComponentRoles::PixmapRole), int(Qt::DecorationRole) })
    {
        const QVariant value = index.data(role);
        switch (value.typeId())
        {
            case QMetaType::QPixmap:
                return value.value<QPixmap>();

            case QMetaType::QImage:
            {
                // The image is converted once: the same pixmap (with the same cache
                // key) is returned for each repaint, so its scaled copy is found in
                // QPixmapCache (review C4; delegates paint in the thread of the user
                // interface only)
                const QImage image = value.value<QImage>();
                if (image.isNull())
                {
                    return QPixmap();
                }

                const QString key = QStringLiteral("pdf4qt-delegate-image-%1").arg(image.cacheKey());
                QPixmap pixmap;
                if (!QPixmapCache::find(key, &pixmap))
                {
                    pixmap = QPixmap::fromImage(image);
                    QPixmapCache::insert(key, pixmap);
                }
                return pixmap;
            }

            case QMetaType::QIcon:
            {
                // An icon of a file model: its largest pixmap up to 512 px
                const QIcon icon = value.value<QIcon>();
                if (icon.isNull())
                {
                    return QPixmap();
                }

                const int extent = PDFComponentDelegatesHelper::MAX_ICON_PIXMAP;
                return icon.pixmap(icon.actualSize(QSize(extent, extent)));
            }

            default:
                break;
        }
    }

    return QPixmap();
}

bool PDFRowDelegate::isKeyHandled(QAbstractItemView* view, const QModelIndex& index, const QKeyEvent* event) const
{
    if (!event || !index.isValid() || !hasActions())
    {
        return false;
    }

    for (const Action& action : getActions(getViewOption(view, index), index))
    {
        if (action.enabled && PDFComponentDelegatesHelper::isShortcutKey(action.shortcut, event))
        {
            return true;
        }
    }

    return false;
}

bool PDFRowDelegate::handleKey(QAbstractItemView* view, const QModelIndex& index, QKeyEvent* event)
{
    if (!event || !index.isValid() || !hasActions())
    {
        return false;
    }

    for (const Action& action : getActions(getViewOption(view, index), index))
    {
        if (action.enabled && PDFComponentDelegatesHelper::isShortcutKey(action.shortcut, event))
        {
            triggerAction(view, index, action.id);
            return true;
        }
    }

    return false;
}

QStyleOptionViewItem PDFRowDelegate::getViewOption(QAbstractItemView* view, const QModelIndex& index) const
{
    using Helper = PDFComponentDelegatesHelper;

    // An index of another model is an item, which the view does not show (review C6)
    const bool isShown = view && index.isValid() && Helper::isIndexOfView(view, index);

    QStyleOptionViewItem option;
    if (view)
    {
        option.initFrom(view);
        option.widget = view;
        option.rect = isShown ? view->visualRect(index) : QRect();
        option.state &= ~(QStyle::State_MouseOver | QStyle::State_HasFocus);
        if (isShown && view->selectionModel() && view->selectionModel()->isSelected(index))
        {
            option.state |= QStyle::State_Selected;
        }
    }
    else
    {
        option.state = QStyle::State_Enabled;
        option.direction = QApplication::layoutDirection();
    }

    option.index = index;
    if (!Helper::isItemEnabled(view, index))
    {
        option.state &= ~QStyle::State_Enabled;
    }

    if (option.rect.isEmpty())
    {
        // An item, which the view does not show, has the size of the component
        QStyleOptionViewItem sizeOption = option;
        sizeOption.widget = nullptr;
        option.rect = QRect(QPoint(0, 0), getItemSize(sizeOption, isShown || !view ? index : QModelIndex(), -1));
    }

    return option;
}

int PDFRowDelegate::getItemWidth(const QStyleOptionViewItem& option, const QModelIndex& index)
{
    // A list view in the list mode asks also with the rectangle of the item,
    // which it has stretched to the width of its contents: the width never
    // exceeds the viewport without the spacing (a row wider than the viewport
    // would keep a horizontal scroll bar of a few pixels forever)
    const QListView* listView = qobject_cast<const QListView*>(option.widget);
    const bool isListMode = listView && listView->viewMode() == QListView::ListMode && listView->flow() == QListView::TopToBottom && !listView->isWrapping();
    if (option.rect.width() > 0)
    {
        if (isListMode && listView->viewport() && listView->viewport()->width() > 2 * listView->spacing())
        {
            return qMin(option.rect.width(), listView->viewport()->width() - 2 * listView->spacing());
        }
        return option.rect.width();
    }

    // A tree view asks for the height without the width: the width of the
    // column without the indentation of the levels below the root index
    if (const QTreeView* treeView = qobject_cast<const QTreeView*>(option.widget))
    {
        const QHeaderView* header = treeView->header();
        if (header && index.isValid() && index.model() == treeView->model())
        {
            int width = header->sectionSize(index.column());
            const int treeColumn = treeView->treePosition() < 0 ? header->logicalIndex(0) : treeView->treePosition();
            if (index.column() == treeColumn)
            {
                const QModelIndex root = treeView->rootIndex();
                int depth = treeView->rootIsDecorated() ? 1 : 0;
                for (QModelIndex parent = index.parent(); parent.isValid() && parent != root && depth < 1000; parent = parent.parent())
                {
                    ++depth;
                }
                width -= depth * treeView->indentation();
            }
            if (header->isHidden() && treeView->viewport())
            {
                width = qMin(width, treeView->viewport()->width());
            }
            return width > 0 ? width : -1;
        }
    }

    if (listView)
    {
        if (listView->viewMode() == QListView::IconMode)
        {
            // The items of a grid have their own width (the cell of the grid)
            const QSize grid = listView->gridSize();
            return grid.isValid() && grid.width() > 0 ? grid.width() : -1;
        }

        if (listView->viewport())
        {
            const int width = listView->viewport()->width() - 2 * listView->spacing();
            return width > 0 ? width : -1;
        }
    }

    return -1;
}

void PDFRowDelegate::updateIndex(QAbstractItemView* view, const QModelIndex& index)
{
    const QRect rect = getRowRect(view, index);
    if (!rect.isEmpty())
    {
        view->viewport()->update(rect);
    }
}

bool PDFRowDelegate::eventFilter(QObject* watched, QEvent* event)
{
    if (watched && event)
    {
        // The watched objects are compared by their pointers only: the view can be
        // in its destructor, the old viewport can be being deleted (review C1)
        for (const WatchedView& entry : m_views)
        {
            if (!entry.view)
            {
                continue;
            }

            if (watched == entry.view.data())
            {
                QPointer<QObject> guard(watched);
                const bool handled = handleViewEvent(entry.view.data(), event);
                return !guard || handled;
            }

            if (watched == entry.viewport.data())
            {
                QPointer<QObject> guard(watched);
                const bool handled = handleViewportEvent(entry.view.data(), event);
                return !guard || handled;
            }
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFRowDelegate::setHover(QAbstractItemView* view, const QModelIndex& index, int action, const QPoint& position)
{
    using Helper = PDFComponentDelegatesHelper;

    m_hoverPosition = position;
    m_hasHoverPosition = true;

    const bool isSameView = view == m_trackedView;
    const bool isSame = isSameView && ((!index.isValid() && !m_hoverIndex.isValid()) || Helper::isSameItem(index, m_hoverIndex)) && action == m_hoverAction;
    if (isSame)
    {
        return;
    }

    // Repaint the previous hovered item and the new one
    repaintRect(m_hoverRect);
    if (isSameView && m_hoverIndex.isValid())
    {
        // The item may have moved (rows inserted above, a sorted model)
        updateIndex(view, m_hoverIndex);
    }
    if (!m_pressedIndex.isValid() || view == m_trackedView)
    {
        m_trackedView = view;
        m_trackedViewport = view ? view->viewport() : nullptr;
    }
    m_hoverIndex = index;
    m_hoverAction = index.isValid() ? action : -1;
    m_hoverRect = getRowRect(view, index);
    repaintRect(m_hoverRect);
}

void PDFRowDelegate::resetHover()
{
    // No function of the view is called: the view can be just being destroyed
    repaintRect(m_hoverRect);
    m_hoverIndex = QPersistentModelIndex();
    m_hoverAction = -1;
    m_hoverRect = QRect();
    m_hasHoverPosition = false;
}

void PDFRowDelegate::clearPress()
{
    // No function of the view is called: the view can be just being destroyed
    repaintRect(m_pressedRect);
    m_pressedIndex = QPersistentModelIndex();
    m_pressedAction = -1;
    m_pressedInside = false;
    m_pressedRect = QRect();
    m_isMoveWithButton = false;
}

void PDFRowDelegate::clearPressOf(const QAbstractItemView* view)
{
    // A view, which is hidden or disabled, ends only its own press (review C12)
    if (view && view == m_trackedView && m_pressedIndex.isValid())
    {
        clearPress();
    }
}

QRect PDFRowDelegate::getRowRect(QAbstractItemView* view, const QModelIndex& index)
{
    if (!view || !index.isValid() || !view->viewport() || index.model() != view->model())
    {
        return QRect();
    }

    // The whole row (a table has more cells in it)
    const QRect rect = view->visualRect(index);
    return rect.isValid() ? QRect(0, rect.top(), view->viewport()->width(), rect.height()) : QRect();
}

void PDFRowDelegate::repaintRect(const QRect& rect)
{
    if (m_trackedViewport && !rect.isEmpty())
    {
        m_trackedViewport->update(rect);
    }
}

bool PDFRowDelegate::isOwnIndex(const QAbstractItemView* view, const QModelIndex& index) const
{
    return view && index.isValid() && index.model() == view->model() && view->itemDelegateForIndex(index) == this;
}

void PDFRowDelegate::handlePointerMove(QAbstractItemView* view, const QPoint& position, bool isButtonHeld)
{
    using Helper = PDFComponentDelegatesHelper;

    // A press, whose release has been lost (a modal dialog opened by a slot of
    // a double click, a drag of the platform), ends without the button (review C2)
    if (m_pressedIndex.isValid() && m_trackedView == view && !isButtonHeld)
    {
        clearPress();
    }

    QModelIndex index = view->indexAt(position);
    if (!isOwnIndex(view, index) || !Helper::isItemEnabled(view, index))
    {
        index = QModelIndex();
    }

    const int action = (index.isValid() && hasActions()) ? getActionAt(getViewOption(view, index), index, position) : -1;
    setHover(view, index, action, position);

    if (m_pressedIndex.isValid() && m_trackedView == view)
    {
        // The pressed item (or action) is drawn pressed, while the pointer is over it
        const bool isInside = Helper::isSameItem(index, m_pressedIndex) && (m_pressedAction < 0 || action == m_pressedAction);
        if (isInside != m_pressedInside)
        {
            m_pressedInside = isInside;
            updateIndex(view, m_pressedIndex);
        }
    }
}

bool PDFRowDelegate::handleViewportEvent(QAbstractItemView* view, QEvent* event)
{
    using Helper = PDFComponentDelegatesHelper;

    switch (event->type())
    {
        case QEvent::MouseMove:
        {
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            const bool isButtonHeld = mouseEvent->buttons().testFlag(Qt::LeftButton);

            // QApplication sends a hover move after each mouse move (with the same buttons)
            m_isMoveWithButton = isButtonHeld;
            handlePointerMove(view, mouseEvent->position().toPoint(), isButtonHeld);

            // A pressed action does not start a selection by a drag
            return m_pressedIndex.isValid() && m_trackedView == view && m_pressedAction >= 0;
        }

        case QEvent::HoverEnter:
        case QEvent::HoverMove:
        {
            // A hover move follows each mouse move; without a mouse move with the
            // button, the state of the buttons of the application decides
            const bool isButtonHeld = m_isMoveWithButton || Helper::isLeftButtonHeld();
            m_isMoveWithButton = false;
            handlePointerMove(view, Helper::getPosition(event), isButtonHeld);
            return false;
        }

        case QEvent::Enter:
        {
            if (m_pressedIndex.isValid() && m_trackedView == view && !Helper::isLeftButtonHeld())
            {
                clearPress();
            }
            return false;
        }

        case QEvent::HoverLeave:
        case QEvent::Leave:
        {
            // No function of the view is called (the destructor of QWidget sends a leave)
            if (m_trackedView == view)
            {
                resetHover();
                if (m_pressedIndex.isValid() && !Helper::isLeftButtonHeld())
                {
                    // The release has been lost (a drag of the platform)
                    clearPress();
                }
                else if (m_pressedIndex.isValid() && m_pressedInside)
                {
                    m_pressedInside = false;
                    repaintRect(m_pressedRect);
                }
            }
            return false;
        }

        case QEvent::DragEnter:
        case QEvent::DragLeave:
        case QEvent::Drop:
            // A drag ends the press (the platform may swallow the release)
            clearPressOf(view);
            return false;

        case QEvent::Paint:
        {
            // The item under the pointer, after a scroll or a change of the rows
            // (the pointer has not moved, the rows have, review C10)
            if (m_trackedView == view && m_hasHoverPosition && view->viewport() && view->viewport()->rect().contains(m_hoverPosition))
            {
                QModelIndex index = view->indexAt(m_hoverPosition);
                if (!isOwnIndex(view, index) || !Helper::isItemEnabled(view, index))
                {
                    index = QModelIndex();
                }
                const int action = (index.isValid() && hasActions()) ? getActionAt(getViewOption(view, index), index, m_hoverPosition) : -1;
                const bool isSame = ((!index.isValid() && !m_hoverIndex.isValid()) || Helper::isSameItem(index, m_hoverIndex)) && action == m_hoverAction;
                if (!isSame)
                {
                    setHover(view, index, action, m_hoverPosition);
                }
                else
                {
                    // The rectangle follows the item
                    m_hoverRect = getRowRect(view, m_hoverIndex);
                }
            }
            return false;
        }

        case QEvent::Resize:
        {
            // The heights of the items, which follow the width. The rows of a list
            // fill its width: a list lays its items out for the viewport without
            // the vertical scroll bar and does not lay them out again, when the
            // scroll bar appears (the view keeps its size), so the rows would be
            // wider than the viewport and a horizontal scroll bar would appear.
            WatchedView* watched = findWatched(view);
            const int width = static_cast<const QResizeEvent*>(event)->size().width();
            if (watched && watched->viewportWidth != width)
            {
                watched->viewportWidth = width;
                const QListView* listView = qobject_cast<const QListView*>(view);
                const bool isListMode = listView && listView->viewMode() == QListView::ListMode && listView->flow() == QListView::TopToBottom && !listView->isWrapping();
                if (hasHeightForWidth() || isListMode)
                {
                    invalidateSizes();
                }
            }
            return false;
        }

        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        {
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (mouseEvent->button() != Qt::LeftButton)
            {
                return false;
            }

            clearPress();
            const QPoint position = mouseEvent->position().toPoint();
            const QModelIndex index = view->indexAt(position);
            if (!isOwnIndex(view, index) || !Helper::isItemEnabled(view, index))
            {
                return false;
            }

            const int action = hasActions() ? getActionAt(getViewOption(view, index), index, position) : -1;
            m_trackedView = view;
            m_trackedViewport = view->viewport();
            m_pressedRect = getRowRect(view, index);
            m_pressedIndex = index;
            m_pressedAction = action;
            m_pressedInside = true;
            m_isMoveWithButton = false;
            updateIndex(view, index);

            // A press of an action neither selects the item nor activates it
            return action >= 0;
        }

        case QEvent::MouseButtonRelease:
        {
            const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
            if (mouseEvent->button() != Qt::LeftButton)
            {
                return false;
            }

            if (!m_pressedIndex.isValid() || m_trackedView != view)
            {
                clearPressOf(view);
                return false;
            }

            const QPersistentModelIndex pressedIndex = m_pressedIndex;
            const int pressedAction = m_pressedAction;
            clearPress();

            if (pressedAction < 0)
            {
                return false;
            }

            // The action is triggered, when it is released over itself
            const QPoint position = mouseEvent->position().toPoint();
            const QModelIndex index = view->indexAt(position);
            if (pressedIndex.isValid() && Helper::isSameItem(index, pressedIndex) && getActionAt(getViewOption(view, index), index, position) == pressedAction)
            {
                // The slot may delete the delegate, the view or the model: nothing follows
                triggerAction(view, pressedIndex, pressedAction);
            }
            return true;
        }

        case QEvent::ContextMenu:
        {
            QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
            if (contextEvent->reason() == QContextMenuEvent::Keyboard)
            {
                return false;
            }

            const QModelIndex index = view->indexAt(contextEvent->pos());
            if (isOwnIndex(view, index) && showContextMenu(view, index, contextEvent->globalPos()))
            {
                contextEvent->accept();
                return true;
            }
            return false;
        }

        case QEvent::Hide:
        {
            // No function of the view is called (it can be just being destroyed)
            if (m_trackedView == view)
            {
                clearPress();
                resetHover();
            }
            return false;
        }

        default:
            break;
    }

    return false;
}

bool PDFRowDelegate::handleViewEvent(QAbstractItemView* view, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::ShortcutOverride:
        case QEvent::KeyPress:
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            const QModelIndex index = view->currentIndex();
            if (!isOwnIndex(view, index) || !PDFComponentDelegatesHelper::isItemEnabled(view, index))
            {
                return false;
            }

            if (event->type() == QEvent::ShortcutOverride)
            {
                // A key of the current item wins over a shortcut of the application
                // (the same keys as the key press, review C8)
                if (isKeyHandled(view, index, keyEvent))
                {
                    keyEvent->accept();
                    return true;
                }
                return false;
            }

            return handleKey(view, index, keyEvent);
        }

        case QEvent::ContextMenu:
        {
            QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
            if (contextEvent->reason() != QContextMenuEvent::Keyboard)
            {
                return false;
            }

            // The menu of the current item lies below it, at its leading edge,
            // inside of the viewport (an item scrolled out of the view, review C30)
            const QModelIndex index = view->currentIndex();
            QWidget* viewport = view->viewport();
            if (!isOwnIndex(view, index) || !viewport)
            {
                return false;
            }

            const QRect rect = view->visualRect(index);
            QPoint position = view->isRightToLeft() ? rect.bottomRight() : rect.bottomLeft();
            const QRect bounds = viewport->rect();
            if (!bounds.isEmpty())
            {
                position.setX(qBound(bounds.left(), position.x(), bounds.right()));
                position.setY(qBound(bounds.top(), position.y(), bounds.bottom()));
            }
            if (showContextMenu(view, index, viewport->mapToGlobal(position)))
            {
                contextEvent->accept();
                return true;
            }
            return false;
        }

        case QEvent::ChildAdded:
        {
            // A viewport set by setViewport is watched instead of the old one.
            // The scroll area installs its own filter on the new viewport after
            // this event; the filter of the delegate must run before it (the
            // filter of the scroll area consumes the mouse events), so it is
            // installed again later, which moves it to the front.
            if (WatchedView* watched = findWatched(view))
            {
                if (watched->viewport != view->viewport())
                {
                    installViewport(*watched, view->viewport());
                    watched->isFilterReinstallPending = watched->viewport != nullptr;
                    QMetaObject::invokeMethod(this, &PDFRowDelegate::installPendingViewportFilters, Qt::QueuedConnection);
                }
            }
            return false;
        }

        case QEvent::FontChange:
        case QEvent::LanguageChange:
        case QEvent::StyleChange:
            // The sizes of the items depend on the fonts and the translated texts (review C9)
            invalidateSizes();
            return false;

        case QEvent::Hide:
        case QEvent::EnabledChange:
        case QEvent::FocusOut:
        case QEvent::WindowBlocked:
            // A modal window, the loss of the focus or the hide may swallow the release (review C2)
            clearPressOf(view);
            return false;

        default:
            break;
    }

    return false;
}

bool PDFRowDelegate::showContextMenu(QAbstractItemView* view, const QModelIndex& index, const QPoint& globalPosition)
{
    if (m_contextMenuMode == ContextMenuMode::Never || !view || view->contextMenuPolicy() != Qt::DefaultContextMenu || !index.isValid())
    {
        return false;
    }

    // A view class of an application may have its own context menu (review C17)
    if (m_contextMenuMode == ContextMenuMode::Automatic && !PDFComponentDelegatesHelper::isLibraryView(view))
    {
        return false;
    }

    QMenu* menu = createContextMenu(view, index, view);
    if (!menu)
    {
        return false;
    }

    menu->popup(globalPosition);
    return true;
}

void PDFRowDelegate::onThemeChanged()
{
    m_sizeCache.clear();
    for (const WatchedView& watched : m_views)
    {
        if (watched.viewport)
        {
            watched.viewport->update();
        }
    }
}

PDFThumbnailDelegate::PDFThumbnailDelegate(QObject* parent) :
    BaseClass(parent)
{

}

void PDFThumbnailDelegate::setPageSize(const QSize& size)
{
    PDFComponentPainter::ThumbnailItem item;
    item.pageSize = size;
    const QSize validSize = PDFComponentPainter::getThumbnailItemPageSize(item);
    if (m_pageSize == validSize)
    {
        return;
    }

    m_pageSize = validSize;
    Q_EMIT sizeHintChanged(QModelIndex());
}

PDFComponentPainter::ThumbnailItem PDFThumbnailDelegate::getContent(const QModelIndex& index, bool withPixmap) const
{
    PDFComponentPainter::ThumbnailItem item;
    item.label = getString(index, Qt::DisplayRole);
    if (withPixmap)
    {
        item.pixmap = getPixmap(index);
    }
    item.pageSize = m_pageSize;
    return item;
}

void PDFThumbnailDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintThumbnailItem(painter, QRectF(option.rect), getContent(index), state);
}

QSize PDFThumbnailDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    // The size does not depend on the picture: it is never read here (a model of
    // the thumbnails renders the page in Qt::DecorationRole, review C4)
    Q_UNUSED(option);
    Q_UNUSED(width);
    return PDFComponentPainter::getThumbnailItemSize(getContent(index, false));
}

PDFBookmarkDelegate::PDFBookmarkDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFComponentPainter::BookmarkRow PDFBookmarkDelegate::getContent(const QModelIndex& index) const
{
    PDFComponentPainter::BookmarkRow row;
    row.title = getString(index, Qt::DisplayRole);
    row.subtitle = getString(index, PDFComponentRoles::SubtitleRole);
    row.tag = getString(index, PDFComponentRoles::TagRole);
    return row;
}

void PDFBookmarkDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintBookmarkRow(painter, QRectF(option.rect), getContent(index), state);
}

QSize PDFBookmarkDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    const PDFComponentPainter::BookmarkRow row = getContent(index);
    const QSize size = PDFComponentPainter::getBookmarkRowSize(row);
    return QSize(width > 0 ? qMax(width, PDFComponentPainter::getBookmarkRowMinimumSize(row).width()) : size.width(), size.height());
}

PDFNoteDelegate::PDFNoteDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFComponentPainter::NoteRow PDFNoteDelegate::getContent(const QModelIndex& index) const
{
    PDFComponentPainter::NoteRow row;
    row.author = getString(index, Qt::DisplayRole);
    row.date = getString(index, PDFComponentRoles::DateRole);
    row.text = getString(index, PDFComponentRoles::TextRole);
    row.iconName = getString(index, PDFComponentRoles::IconNameRole, row.iconName);
    row.hue = getHue(index, PDFComponentRoles::HueRole, row.hue);
    row.stateIconName = getString(index, PDFComponentRoles::StateIconRole);
    return row;
}

int PDFNoteDelegate::getIndent(const QModelIndex& index)
{
    return qBound(0, getInt(index, PDFComponentRoles::IndentRole, 0), MAXIMUM_INDENT);
}

QRect PDFNoteDelegate::getIndentedRect(const QRect& itemRect, int indent, Qt::LayoutDirection direction)
{
    // The indentation lies at the leading side
    const int boundedIndent = qBound(0, indent, qMax(0, itemRect.width() - 1));
    return direction == Qt::RightToLeft ? itemRect.adjusted(0, 0, -boundedIndent, 0) : itemRect.adjusted(boundedIndent, 0, 0, 0);
}

void PDFNoteDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    // A reply is indented with its selection and its focus ring (1025:21920)
    const QRect rowRect = getIndentedRect(option.rect, getIndent(index), option.direction);
    PDFComponentPainter::paintNoteRow(painter, QRectF(rowRect), getContent(index), state);
}

QSize PDFNoteDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    const PDFComponentPainter::NoteRow row = getContent(index);
    const int indent = getIndent(index);

    // The wrapping of the text is remembered by the text, the width, the
    // indentation and the state icon (review C5)
    const QString key = QString::number(width) + QLatin1Char('\x1F') + QString::number(indent) + QLatin1Char('\x1F') + row.stateIconName + QLatin1Char('\x1F') + row.text;
    if (const std::optional<QSize> cachedSize = getCachedSize(key))
    {
        return *cachedSize;
    }

    if (width <= 0)
    {
        const QSize size = PDFComponentPainter::getNoteRowSize(row);
        return setCachedSize(key, QSize(size.width() + indent, size.height()));
    }

    const int rowWidth = qMax(width - indent, PDFComponentPainter::getNoteRowMinimumSize(row).width());
    return setCachedSize(key, QSize(rowWidth + indent, PDFComponentPainter::getNoteRowHeight(row, rowWidth)));
}

PDFAttachmentDelegate::PDFAttachmentDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFComponentPainter::AttachmentRow PDFAttachmentDelegate::getContent(const QModelIndex& index) const
{
    PDFComponentPainter::AttachmentRow row;
    row.name = getString(index, Qt::DisplayRole);
    row.meta = getString(index, PDFComponentRoles::SubtitleRole);
    row.iconName = getString(index, PDFComponentRoles::IconNameRole, row.iconName);
    row.hue = getHue(index, PDFComponentRoles::HueRole, row.hue);
    return row;
}

QList<PDFRowDelegate::Action> PDFAttachmentDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return QList<Action>();
    }

    Action save;
    save.id = SaveAction;
    save.rect = PDFComponentPainter::getAttachmentRowSaveRect(QRectF(option.rect), getItemState(option, index));
    save.text = tr("Save As…");
    save.toolTip = tr("Save to a file");
    return { save };
}

void PDFAttachmentDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::AttachmentRow row = getContent(index);
    row.saveFlags = getActionFlags(option, index, SaveAction);
    PDFComponentPainter::paintAttachmentRow(painter, QRectF(option.rect), row, state);
}

QSize PDFAttachmentDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    const PDFComponentPainter::AttachmentRow row = getContent(index);
    const QSize size = PDFComponentPainter::getAttachmentRowSize(row);
    return QSize(width > 0 ? qMax(width, PDFComponentPainter::getAttachmentRowMinimumSize(row).width()) : size.width(), size.height());
}

PDFSearchResultDelegate::PDFSearchResultDelegate(QObject* parent) :
    BaseClass(parent)
{

}

QList<PDFComponentPainter::TextRange> PDFSearchResultDelegate::getMatchRanges(const QVariant& value)
{
    QList<PDFComponentPainter::TextRange> ranges;

    if (value.typeId() == QMetaType::QPoint)
    {
        PDFComponentDelegatesHelper::appendMatchRange(ranges, value);
    }
    else if (value.isValid() && value.canConvert<QVariantList>())
    {
        const QVariantList list = value.toList();
        for (qsizetype i = 0; i < list.size() && i < 10000; ++i)
        {
            PDFComponentDelegatesHelper::appendMatchRange(ranges, list.at(i));
        }
    }

    return ranges;
}

PDFComponentPainter::SearchResultRow PDFSearchResultDelegate::getContent(const QModelIndex& index) const
{
    PDFComponentPainter::SearchResultRow row;
    row.page = getString(index, Qt::DisplayRole);
    row.snippet = getString(index, PDFComponentRoles::TextRole);
    if (index.isValid())
    {
        row.matches = getMatchRanges(index.data(PDFComponentRoles::MatchRangesRole));
    }
    return row;
}

void PDFSearchResultDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintSearchResultRow(painter, QRectF(option.rect), getContent(index), state);
}

QSize PDFSearchResultDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    const PDFComponentPainter::SearchResultRow row = getContent(index);

    // The layout of the snippet is remembered by its texts, terms and the width (review C5)
    QString key = QString::number(width) + QLatin1Char('\x1F') + row.page + QLatin1Char('\x1F');
    for (const PDFComponentPainter::TextRange& range : row.matches)
    {
        key += QString::number(range.start) + QLatin1Char(',') + QString::number(range.length) + QLatin1Char(';');
    }
    key += QLatin1Char('\x1F') + row.snippet;

    if (const std::optional<QSize> cachedSize = getCachedSize(key))
    {
        return *cachedSize;
    }

    if (width <= 0)
    {
        return setCachedSize(key, PDFComponentPainter::getSearchResultRowSize(row));
    }

    const int itemWidth = qMax(width, PDFComponentPainter::getSearchResultRowMinimumSize(row).width());
    return setCachedSize(key, QSize(itemWidth, PDFComponentPainter::getSearchResultRowHeight(row, itemWidth)));
}

PDFFontDelegate::PDFFontDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFComponentPainter::FontRow PDFFontDelegate::getContent(const QModelIndex& index) const
{
    PDFComponentPainter::FontRow row;
    row.name = getString(index, Qt::DisplayRole);
    row.family = getString(index, PDFComponentRoles::SubtitleRole);
    row.type = getString(index, PDFComponentRoles::TagRole);
    row.embedded = getBool(index, PDFComponentRoles::EmbeddedRole, true);
    row.statusText = getString(index, PDFComponentRoles::StatusTextRole);
    return row;
}

QList<PDFRowDelegate::Action> PDFFontDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return QList<Action>();
    }

    Action copy;
    copy.id = CopyAction;
    copy.rect = PDFComponentPainter::getFontRowCopyRect(QRectF(option.rect), getItemState(option, index));
    copy.text = tr("Copy name");
    copy.toolTip = tr("Copy the name of the font");
    copy.shortcut = QKeySequence(QKeySequence::Copy);
    copy.enabled = !getString(index, Qt::DisplayRole).isEmpty();
    return { copy };
}

void PDFFontDelegate::performAction(QAbstractItemView* view, const QModelIndex& index, int action)
{
    const QPointer<PDFFontDelegate> self(this);
    const QPointer<QAbstractItemView> viewPointer(view);
    const QPersistentModelIndex persistentIndex(index);
    if (action == CopyAction)
    {
        if (QClipboard* clipboard = QGuiApplication::clipboard())
        {
            clipboard->setText(getString(index, Qt::DisplayRole));
        }
    }

    // Clipboard observers may delete the delegate, the view or the model.
    if (self && persistentIndex.isValid())
    {
        BaseClass::performAction(viewPointer.data(), persistentIndex, action);
    }
}

void PDFFontDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintFontRow(painter, QRectF(option.rect), getContent(index), state);
}

QSize PDFFontDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    const PDFComponentPainter::FontRow row = getContent(index);
    const QSize size = PDFComponentPainter::getFontRowSize(row);
    return QSize(width > 0 ? qMax(width, PDFComponentPainter::getFontRowMinimumSize(row).width()) : size.width(), size.height());
}

PDFRecentItemDelegate::PDFRecentItemDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFComponentPainter::RecentItemRow PDFRecentItemDelegate::getContent(const QModelIndex& index, bool withPixmap) const
{
    PDFComponentPainter::RecentItemRow row;
    row.name = getString(index, Qt::DisplayRole);
    row.details = getString(index, PDFComponentRoles::SubtitleRole);
    row.progressText = getString(index, PDFComponentRoles::ProgressTextRole);
    row.progress = getReal(index, PDFComponentRoles::ProgressRole, -1.0);
    row.opened = getString(index, PDFComponentRoles::DateRole);
    row.type = PDFComponentPainter::normalized(static_cast<PDFComponentPainter::RecentItemType>(getInt(index, PDFComponentRoles::ItemTypeRole, 0)));
    row.pixmap = (withPixmap && row.type == PDFComponentPainter::RecentItemType::File) ? getPixmap(index) : QPixmap();
    row.pinned = getBool(index, PDFComponentRoles::PinnedRole, false);
    row.starVisible = getBool(index, PDFComponentRoles::StarVisibleRole, true);
    row.status = getBool(index, PDFComponentRoles::MissingRole, false) ? PDFComponentPainter::FileStatus::Missing : PDFComponentPainter::FileStatus::Available;
    row.locked = getBool(index, PDFComponentRoles::LockedRole, false);
    return row;
}

QList<PDFRowDelegate::Action> PDFRecentItemDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return QList<Action>();
    }

    const PDFComponentPainter::State state = getItemState(option, index);
    const bool isPinned = getBool(index, PDFComponentRoles::PinnedRole, false);

    Action pin;
    pin.id = PinAction;
    pin.rect = PDFComponentPainter::getRecentItemRowStarRect(QRectF(option.rect), state);
    pin.text = isPinned ? tr("Unpin") : tr("Pin");
    pin.toolTip = isPinned ? tr("Unpin") : tr("Pin file");

    Action menu;
    menu.id = MenuAction;
    menu.rect = PDFComponentPainter::getRecentItemRowMoreRect(QRectF(option.rect), state);
    menu.text = tr("More actions");

    // An item without the star (it cannot be pinned) has only the menu
    if (!getBool(index, PDFComponentRoles::StarVisibleRole, true))
    {
        return { menu };
    }

    return { pin, menu };
}

void PDFRecentItemDelegate::performAction(QAbstractItemView* view, const QModelIndex& index, int action)
{
    if (action == PinAction)
    {
        // The star writes the pinned state into the model (a model, which does not
        // accept it, keeps the item; the application gets the signal in both
        // cases). A slot of dataChanged may delete the delegate (review C15).
        QAbstractItemModel* model = const_cast<QAbstractItemModel*>(index.model());
        if (!model)
        {
            // An invalid index of a direct call of a subclass (triggerAction checks it)
            return;
        }

        const bool isPinned = getBool(index, PDFComponentRoles::PinnedRole, false);
        const QPersistentModelIndex persistentIndex(index);
        const QPointer<PDFRecentItemDelegate> self(this);
        model->setData(index, !isPinned, PDFComponentRoles::PinnedRole);
        if (!self || !persistentIndex.isValid())
        {
            return;
        }
        BaseClass::performAction(view, persistentIndex, action);
        return;
    }

    BaseClass::performAction(view, index, action);
}

void PDFRecentItemDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::RecentItemRow row = getContent(index);
    row.starFlags = getActionFlags(option, index, PinAction);
    row.moreFlags = getActionFlags(option, index, MenuAction);
    PDFComponentPainter::paintRecentItemRow(painter, QRectF(option.rect), row, state);
}

QSize PDFRecentItemDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    // The size does not depend on the picture of the first page (review C4)
    Q_UNUSED(option);
    const PDFComponentPainter::RecentItemRow row = getContent(index, false);
    const QSize size = PDFComponentPainter::getRecentItemRowSize(row);
    return QSize(width > 0 ? qMax(width, PDFComponentPainter::getRecentItemRowMinimumSize(row).width()) : size.width(), size.height());
}

PDFShortcutRecorder::PDFShortcutRecorder(QWidget* parent) :
    BaseClass(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setAccessibleName(PDFComponentPainter::getRecordShortcutText());
}

bool PDFShortcutRecorder::isRecordableKey(int key)
{
    switch (key)
    {
        case 0:
        case Qt::Key_unknown:
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Meta:
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
        case Qt::Key_CapsLock:
        case Qt::Key_NumLock:
        case Qt::Key_ScrollLock:
        case Qt::Key_Super_L:
        case Qt::Key_Super_R:
        case Qt::Key_Hyper_L:
        case Qt::Key_Hyper_R:
        case Qt::Key_Mode_switch:
            return false;

        default:
            break;
    }

    return true;
}

QKeyCombination PDFShortcutRecorder::getKeyCombination(const QKeyEvent* event)
{
    if (!event)
    {
        return QKeyCombination();
    }

#ifdef Q_OS_WIN
    // The base key is needed only for Ctrl+Alt (the lookup is not done for other keys)
    const bool isControlAlt = event->modifiers().testFlags(Qt::ControlModifier | Qt::AltModifier);
    const char16_t baseCharacter = isControlAlt ? getBaseCharacter(event->nativeVirtualKey(), event->nativeScanCode()) : char16_t(0);
    return getWindowsKeyCombination(event->key(), event->modifiers(), event->text(), baseCharacter);
#else
    return getKeyCombination(event->key(), event->modifiers(), event->text());
#endif
}

QList<QKeyCombination> PDFShortcutRecorder::getMatchingKeyCombinations(const QKeyEvent* event)
{
    QList<QKeyCombination> combinations;
    if (!event)
    {
        return combinations;
    }

    const QKeyCombination candidates[] = {
        getKeyCombination(event),
        QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, static_cast<Qt::Key>(event->key())),
        getKeyCombination(event->key(), event->modifiers(), event->text())
    };

    for (const QKeyCombination& candidate : candidates)
    {
        if (candidate.key() != Qt::Key(0) && !combinations.contains(candidate))
        {
            combinations.append(candidate);
        }
    }

    return combinations;
}

QKeyCombination PDFShortcutRecorder::getWindowsKeyCombination(int key, Qt::KeyboardModifiers modifiers, const QString& text, char16_t baseCharacter)
{
    // Windows give the key of the AltGr level for Ctrl+Alt ("[" of Ctrl+Alt+F on
    // the Czech layout, the dead caron of the key of U+011B). The shortcut is all
    // the modifiers with the base key: the character of the key without the
    // modifiers, uppercased - the candidate, which
    // QWindowsKeyMapper::possibleKeyCombinations always gives the shortcut map
    // of Qt (qtKey[0] of toKeyOrUnicode: also the spacing character of a dead
    // key; a control character is not a base key). A surrogate is not a key.
    if (modifiers.testFlags(Qt::ControlModifier | Qt::AltModifier))
    {
        const char16_t baseKey = QChar(baseCharacter).toUpper().unicode();
        if (baseKey >= 0x20 && baseKey != 0x7F && !QChar::isSurrogate(baseKey))
        {
            return QKeyCombination(modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier), static_cast<Qt::Key>(baseKey));
        }
    }

    // A key without a character (F-keys, arrows, Enter, Tab, the keypad without
    // NumLock): the rules of the other platforms keep its Qt key and modifiers
    return getKeyCombination(key, modifiers, text);
}

char16_t PDFShortcutRecorder::getBaseCharacter(quint32 virtualKey, quint32 scanCode)
{
#ifdef Q_OS_WIN
    if (virtualKey == 0 || virtualKey > 0xFE)
    {
        return 0;
    }

    // ToUnicode without any key held, as toKeyOrUnicode of QWindowsKeyMapper
    // fills qtKey[0]; flag 0x4: the state of the keyboard (a pending dead key)
    // is not changed. A dead key (-1) writes its spacing character.
    const BYTE keyState[256] = { };
    wchar_t buffer[5] = { };
    const int count = ::ToUnicode(UINT(virtualKey), UINT(scanCode), keyState, buffer, 5, 0x4);
    return count != 0 ? char16_t(buffer[0]) : char16_t(0);
#else
    Q_UNUSED(virtualKey);
    Q_UNUSED(scanCode);
    return 0;
#endif
}

QKeyCombination PDFShortcutRecorder::getKeyCombination(int key, Qt::KeyboardModifiers modifiers, const QString& text)
{
    modifiers &= (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const QChar character = text.isEmpty() ? QChar() : text.at(0);
    const bool isTypedSymbol = !text.isEmpty() && character.isPrint() && !character.isSpace();

    // Backtab is Shift+Tab (as QKeySequenceEdit)
    if (key == Qt::Key_Backtab)
    {
        key = Qt::Key_Tab;
        modifiers |= Qt::ShiftModifier;
    }

    // A character typed with AltGr (Ctrl+Alt on Windows, a key without the base
    // key there) on another key: the character alone
    if (modifiers.testFlags(Qt::ControlModifier | Qt::AltModifier) && isTypedSymbol && !character.isLetterOrNumber())
    {
        modifiers &= ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    }

    // A symbol typed with Shift ("!" of Shift+1): the symbol alone, the shortcut
    // "!" matches the same keys (the rule of QKeySequenceEdit of Qt 5)
    if (modifiers.testFlag(Qt::ShiftModifier) && isTypedSymbol && !character.isLetterOrNumber() && key != Qt::Key_Tab)
    {
        modifiers &= ~Qt::ShiftModifier;
    }

    return QKeyCombination(modifiers, static_cast<Qt::Key>(key));
}

bool PDFShortcutRecorder::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::ShortcutOverride:
            // The keys belong to the recording, not to the shortcuts of the application
            event->accept();
            return true;

        case QEvent::KeyPress:
            // Also Tab and Backtab come here (they do not move the focus by QWidget)
            keyPressEvent(static_cast<QKeyEvent*>(event));
            return true;

        default:
            break;
    }

    return BaseClass::event(event);
}

void PDFShortcutRecorder::keyPressEvent(QKeyEvent* event)
{
    event->accept();
    if (m_isFinished)
    {
        return;
    }

    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool isPlain = (modifiers & ~Qt::ShiftModifier) == Qt::NoModifier;

    if (key == Qt::Key_Escape && modifiers == Qt::NoModifier)
    {
        cancel();
        return;
    }

    if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && isPlain)
    {
        // The focus moves on, as the key would do without the recording
        m_focusMove = (key == Qt::Key_Backtab || modifiers.testFlag(Qt::ShiftModifier)) ? -1 : 1;
        cancel();
        return;
    }

    // The key of the combination: Windows give no Qt key for Ctrl+Alt with a key
    // without a character of the AltGr level (Ctrl+Alt+Shift+the key of U+011B
    // on the Czech layout), the base key is the key then
    const QKeyCombination combination = getKeyCombination(event);
    if (!isRecordableKey(combination.key()))
    {
        // A lone modifier: the recording waits for the key
        return;
    }

    m_sequence = QKeySequence(combination);
    m_isFinished = true;

    // A copy: a slot of the signal may delete the recorder, the next slots
    // must not read its member
    const QKeySequence sequence = m_sequence;
    Q_EMIT recorded(sequence);
}

void PDFShortcutRecorder::focusOutEvent(QFocusEvent* event)
{
    BaseClass::focusOutEvent(event);
    cancel();
}

void PDFShortcutRecorder::cancel()
{
    if (m_isFinished)
    {
        return;
    }

    m_isFinished = true;
    Q_EMIT canceled();
}

PDFShortcutDelegate::PDFShortcutDelegate(QObject* parent) :
    BaseClass(parent)
{

}

QKeySequence PDFShortcutDelegate::getShortcut(const QModelIndex& index)
{
    if (!index.isValid())
    {
        return QKeySequence();
    }

    const QVariant value = index.data(PDFComponentRoles::ShortcutRole);
    if (value.typeId() == QMetaType::QKeySequence)
    {
        return value.value<QKeySequence>();
    }
    if (value.typeId() == QMetaType::QString)
    {
        return QKeySequence::fromString(value.toString(), QKeySequence::PortableText);
    }

    return QKeySequence();
}

PDFComponentPainter::ShortcutRow PDFShortcutDelegate::getRowContent(const QAbstractItemView* view, const QModelIndex& index) const
{
    PDFComponentPainter::ShortcutRow row;
    row.action = getString(index, Qt::DisplayRole);
    row.iconName = getString(index, PDFComponentRoles::IconNameRole, row.iconName);
    row.recording = view ? isRecording(view, index) : isRecording(index);

    // A sequence is shown in the native text, a text of the model as it is
    if (index.isValid())
    {
        const QVariant value = index.data(PDFComponentRoles::ShortcutRole);
        if (value.typeId() == QMetaType::QKeySequence)
        {
            row.shortcut = value.value<QKeySequence>().toString(QKeySequence::NativeText);
        }
        else if (value.typeId() == QMetaType::QString)
        {
            row.shortcut = value.toString();
        }
    }
    return row;
}

PDFComponentPainter::ShortcutRow PDFShortcutDelegate::getContent(const QModelIndex& index) const
{
    return getRowContent(nullptr, index);
}

void PDFShortcutDelegate::purgeRecordings() const
{
    // A recorder deleted without destroyEditor (its view has been destroyed)
    for (auto it = m_recordings.begin(); it != m_recordings.end();)
    {
        it = it->recorder ? std::next(it) : m_recordings.erase(it);
    }
}

const PDFShortcutDelegate::Recording* PDFShortcutDelegate::findRecording(const PDFShortcutRecorder* recorder) const
{
    if (!recorder)
    {
        return nullptr;
    }

    for (const Recording& recording : m_recordings)
    {
        if (recording.recorder.data() == recorder)
        {
            return &recording;
        }
    }

    return nullptr;
}

bool PDFShortcutDelegate::isRecording(const QModelIndex& index) const
{
    for (const Recording& recording : m_recordings)
    {
        if (recording.recorder && PDFComponentDelegatesHelper::isSameItem(index, recording.index))
        {
            return true;
        }
    }

    return false;
}

bool PDFShortcutDelegate::isRecording(const QAbstractItemView* view, const QModelIndex& index) const
{
    if (!view)
    {
        return false;
    }

    for (const Recording& recording : m_recordings)
    {
        if (recording.recorder && recording.view.data() == view && PDFComponentDelegatesHelper::isSameItem(index, recording.index))
        {
            return true;
        }
    }

    return false;
}

QModelIndex PDFShortcutDelegate::getRecordingIndex() const
{
    for (auto it = m_recordings.crbegin(); it != m_recordings.crend(); ++it)
    {
        if (it->recorder && it->index.isValid())
        {
            return it->index;
        }
    }

    return QModelIndex();
}

int PDFShortcutDelegate::getRecordingCount() const
{
    int count = 0;
    for (const Recording& recording : m_recordings)
    {
        if (!recording.recorder.isNull())
        {
            ++count;
        }
    }
    return count;
}

QModelIndexList PDFShortcutDelegate::findConflicts(const QModelIndex& index, const QKeySequence& sequence)
{
    QModelIndexList conflicts;
    if (!index.isValid() || sequence.isEmpty())
    {
        return conflicts;
    }

    const QAbstractItemModel* model = index.model();
    const QModelIndex parent = index.parent();
    const int rowCount = qMin(model->rowCount(parent), PDFComponentDelegatesHelper::MAX_CONFLICT_ROWS);
    for (int row = 0; row < rowCount; ++row)
    {
        if (row == index.row())
        {
            continue;
        }

        const QModelIndex sibling = model->index(row, index.column(), parent);
        if (getShortcut(sibling) == sequence)
        {
            conflicts << sibling;
        }
    }

    return conflicts;
}

QList<PDFRowDelegate::Action> PDFShortcutDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return QList<Action>();
    }

    const PDFComponentPainter::ShortcutRow row = getRowContent(qobject_cast<const QAbstractItemView*>(option.widget), index);
    const bool hasShortcut = !row.shortcut.isEmpty();

    Action record;
    record.id = RecordAction;
    record.rect = PDFComponentPainter::getShortcutRowKeyRect(QRectF(option.rect), row, getItemState(option, index));
    record.text = hasShortcut ? tr("Change shortcut") : PDFComponentPainter::getAddShortcutText();
    record.toolTip = tr("Record a new shortcut");
    record.enabled = index.flags().testFlag(Qt::ItemIsEditable) && !row.recording;

    Action clear;
    clear.id = ClearAction;
    clear.text = tr("Remove shortcut");
    clear.enabled = hasShortcut && index.flags().testFlag(Qt::ItemIsEditable) && !row.recording;

    return { record, clear };
}

void PDFShortcutDelegate::performAction(QAbstractItemView* view, const QModelIndex& index, int action)
{
    const QPersistentModelIndex persistentIndex(index);
    const QPointer<PDFShortcutDelegate> self(this);
    switch (action)
    {
        case RecordAction:
            // The signal only, when the recording has really started (review C13)
            if (!startRecording(view, index) || !self || !persistentIndex.isValid())
            {
                return;
            }
            break;

        case ClearAction:
        {
            // A slot of dataChanged may delete the delegate (review C15)
            QAbstractItemModel* model = const_cast<QAbstractItemModel*>(index.model());
            if (!model)
            {
                // An invalid index of a direct call of a subclass (triggerAction checks it)
                return;
            }

            const QKeySequence before = getShortcut(index);
            const bool isWritten = model->setData(index, QVariant::fromValue(QKeySequence()), PDFComponentRoles::ShortcutRole);
            if (!self || !persistentIndex.isValid())
            {
                return;
            }
            if (isWritten && getShortcut(persistentIndex) != before)
            {
                Q_EMIT shortcutChanged(persistentIndex, QKeySequence());
                if (!self || !persistentIndex.isValid())
                {
                    return;
                }
            }
            break;
        }

        default:
            break;
    }

    BaseClass::performAction(view, persistentIndex, action);
}

bool PDFShortcutDelegate::startRecording(QAbstractItemView* view, const QModelIndex& index)
{
    if (!view || !index.isValid() || index.model() != view->model())
    {
        return false;
    }

    const QPointer<PDFShortcutDelegate> self(this);
    const QPointer<QAbstractItemView> viewPointer(view);
    const QPersistentModelIndex persistentIndex(index);
    if (view->currentIndex() != index && view->selectionModel())
    {
        view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        if (!self || !viewPointer || !persistentIndex.isValid() || persistentIndex.model() != viewPointer->model())
        {
            return false;
        }
    }

    // The view refuses the editor in some states (another editor is being
    // closed, the view is not editable): the recording has started only, when
    // the editor exists
    viewPointer->edit(persistentIndex);
    return self && viewPointer && persistentIndex.isValid() && isRecording(viewPointer.data(), persistentIndex);
}

QWidget* PDFShortcutDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    // createEditor is a const function of Qt, the delegate receives the signals of the editor
    PDFShortcutRecorder* recorder = new PDFShortcutRecorder(parent);
    PDFShortcutDelegate* self = const_cast<PDFShortcutDelegate*>(this);
    connect(recorder, &PDFShortcutRecorder::recorded, self, &PDFShortcutDelegate::onRecorderRecorded);
    connect(recorder, &PDFShortcutRecorder::canceled, self, &PDFShortcutDelegate::onRecorderCanceled);

    // Each recorder remembers its view and its item (one delegate for more views, review C3)
    purgeRecordings();
    Recording recording;
    recording.recorder = recorder;
    recording.index = index;
    recording.view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(option.widget));
    if (!recording.view && parent)
    {
        recording.view = qobject_cast<QAbstractItemView*>(parent->parentWidget());
    }
    m_recordings.push_back(recording);

    updateIndex(recording.view.data(), index);
    return recorder;
}

void PDFShortcutDelegate::destroyEditor(QWidget* editor, const QModelIndex& index) const
{
    if (const Recording* recording = findRecording(qobject_cast<PDFShortcutRecorder*>(editor)))
    {
        // The editor does not report anything more (it is hidden and deleted)
        editor->blockSignals(true);
        if (recording->view && recording->view->viewport())
        {
            recording->view->viewport()->update();
        }

        m_recordings.erase(m_recordings.begin() + (recording - m_recordings.data()));
    }
    purgeRecordings();

    BaseClass::destroyEditor(editor, index);
}

void PDFShortcutDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    // The recording starts empty, the old shortcut stays until a key is recorded
    Q_UNUSED(editor);
    Q_UNUSED(index);
}

void PDFShortcutDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    const PDFShortcutRecorder* recorder = qobject_cast<const PDFShortcutRecorder*>(editor);
    if (!recorder || !model || !index.isValid() || !recorder->hasKeySequence())
    {
        return;
    }

    model->setData(index, QVariant::fromValue(recorder->getKeySequence()), PDFComponentRoles::ShortcutRole);
}

void PDFShortcutDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);
    if (editor)
    {
        // The editor covers the row (it paints nothing, the row shows the recording)
        editor->setGeometry(option.rect);
    }
}

void PDFShortcutDelegate::closeEditorOfView(QAbstractItemView* view, PDFShortcutRecorder* recorder, bool isCommitted)
{
    // The slots of the view itself: only the view, which owns the editor, gets it
    // (the signals of the delegate would go to every view of the delegate)
    if (!view || !recorder)
    {
        return;
    }

    const QPointer<QAbstractItemView> viewPointer(view);
    const QPointer<PDFShortcutRecorder> recorderPointer(recorder);
    if (isCommitted)
    {
        QMetaObject::invokeMethod(view, "commitData", Qt::DirectConnection, Q_ARG(QWidget*, recorder));
    }
    if (viewPointer && recorderPointer)
    {
        const QAbstractItemDelegate::EndEditHint hint = isCommitted ? QAbstractItemDelegate::NoHint : QAbstractItemDelegate::RevertModelCache;
        QMetaObject::invokeMethod(viewPointer.data(), "closeEditor", Qt::DirectConnection, Q_ARG(QWidget*, recorderPointer.data()), Q_ARG(QAbstractItemDelegate::EndEditHint, hint));
    }
}

void PDFShortcutDelegate::onRecorderRecorded(const QKeySequence& sequence)
{
    // The signal does not carry the recorder and more recorders (of more views)
    // can be connected: the sender is the recorder (another sender, or a direct
    // call, is not a recording - findRecording finds nothing). The sequence is
    // copied: it may be a member of the recorder, which a slot of
    // shortcutConflict can delete before the next slot reads the sequence.
    const QKeySequence recordedSequence = sequence;
    onRecorded(qobject_cast<PDFShortcutRecorder*>(sender()), recordedSequence);
}

void PDFShortcutDelegate::onRecorderCanceled()
{
    onCanceled(qobject_cast<PDFShortcutRecorder*>(sender()));
}

void PDFShortcutDelegate::onRecorded(PDFShortcutRecorder* recorder, const QKeySequence& sequence)
{
    const Recording* recording = findRecording(recorder);
    if (!recording || !recording->view || !recording->index.isValid() || recording->view->indexWidget(recording->index) != recorder)
    {
        return;
    }

    const QPointer<PDFShortcutDelegate> self(this);
    const QPointer<PDFShortcutRecorder> recorderPointer(recorder);
    const QPointer<QAbstractItemView> view = recording->view;
    const QPersistentModelIndex index = recording->index;

    const QModelIndexList conflicts = findConflicts(index, sequence);
    if (!conflicts.isEmpty())
    {
        Q_EMIT shortcutConflict(index, sequence, conflicts);
        if (!self || !recorderPointer || !view)
        {
            return;
        }
    }

    if (conflicts.isEmpty() || m_conflictsAccepted)
    {
        const QKeySequence before = getShortcut(index);
        closeEditorOfView(view.data(), recorderPointer.data(), true);

        // The signal only, when the model has accepted the sequence (review C13)
        if (self && index.isValid() && getShortcut(index) != before)
        {
            Q_EMIT shortcutChanged(index, getShortcut(index));
        }
    }
    else
    {
        closeEditorOfView(view.data(), recorderPointer.data(), false);
    }
}

void PDFShortcutDelegate::onCanceled(PDFShortcutRecorder* recorder)
{
    // The view, which closes the editor itself, has removed it already
    const Recording* recording = findRecording(recorder);
    if (!recording || !recording->view || !recording->index.isValid() || recording->view->indexWidget(recording->index) != recorder)
    {
        return;
    }

    const QPointer<QAbstractItemView> view = recording->view;
    const int focusMove = recorder->getFocusMove();
    closeEditorOfView(view.data(), recorder, false);

    // Tab and Backtab move the focus on (the view has got the focus back from the
    // editor): the view gets the key, as it would without the recording (review C21)
    if (view && focusMove != 0 && view->hasFocus())
    {
        QKeyEvent key(QEvent::KeyPress, focusMove > 0 ? Qt::Key_Tab : Qt::Key_Backtab, focusMove > 0 ? Qt::NoModifier : Qt::ShiftModifier);
        QCoreApplication::sendEvent(view.data(), &key);
    }
}

bool PDFShortcutDelegate::isStartKey(const QKeyEvent* event)
{
    // The same combinations as the shortcuts of the actions (without the keypad
    // modifier); a start key is a key without the modifiers
    for (const QKeyCombination& combination : PDFShortcutRecorder::getMatchingKeyCombinations(event))
    {
        if (combination.keyboardModifiers() != Qt::NoModifier)
        {
            continue;
        }

        switch (combination.key())
        {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
            case Qt::Key_F2:
                return true;

            default:
                break;
        }
    }

    return false;
}

bool PDFShortcutDelegate::isKeyHandled(QAbstractItemView* view, const QModelIndex& index, const QKeyEvent* event) const
{
    // The keys, which start the recording, win over the shortcuts of the application (review C8)
    if (isStartKey(event) && isActionAvailable(view, index, RecordAction))
    {
        return true;
    }

    return BaseClass::isKeyHandled(view, index, event);
}

bool PDFShortcutDelegate::handleKey(QAbstractItemView* view, const QModelIndex& index, QKeyEvent* event)
{
    if (isStartKey(event) && isActionAvailable(view, index, RecordAction))
    {
        triggerAction(view, index, RecordAction);
        return true;
    }

    return BaseClass::handleKey(view, index, event);
}

bool PDFShortcutDelegate::eventFilter(QObject* watched, QEvent* event)
{
    // The recorder handles its keys and its focus itself (the editor filter of
    // Qt would commit on the loss of the focus and close on Enter)
    if (qobject_cast<PDFShortcutRecorder*>(watched))
    {
        return false;
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFShortcutDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    // The recording is shown in the view, which records
    PDFComponentPainter::paintShortcutRow(painter, QRectF(option.rect), getRowContent(qobject_cast<const QAbstractItemView*>(option.widget), index), state);
}

QSize PDFShortcutDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    const PDFComponentPainter::ShortcutRow row = getRowContent(qobject_cast<const QAbstractItemView*>(option.widget), index);
    const QSize size = PDFComponentPainter::getShortcutRowSize(row);
    return QSize(width > 0 ? qMax(width, PDFComponentPainter::getShortcutRowMinimumSize(row).width()) : size.width(), size.height());
}

}   // namespace pdf
