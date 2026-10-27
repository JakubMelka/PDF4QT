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

#ifndef PDFCOMPONENTDELEGATES_H
#define PDFCOMPONENTDELEGATES_H

#include "pdffluentglobal.h"
#include "pdfaccenthue.h"
#include "pdfcomponentpainter.h"
#include "pdfwatchedpointer.h"

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QWidget>

#include <optional>
#include <vector>

class QAbstractItemView;
class QHeaderView;
class QMenu;

namespace pdf
{

/// Roles of the models, which the delegates of the components read. A role,
/// which the model does not have (an invalid QVariant), or of another type,
/// gives the default of the delegate; an invalid index draws the default
/// content. The roles start at \p FirstRole (Qt::UserRole + 0x0C00), so they
/// do not collide with the roles of the application starting at Qt::UserRole.
///
/// | Role | Type | Default | Delegates |
/// | --- | --- | --- | --- |
/// | Qt::DisplayRole | QString (a number or a date is formatted by the locale) | empty | page label (Thumbnail), title (Bookmark), author (Note), name (Attachment, Font, Recent item), page (Search result), action (Shortcut) |
/// | \p SubtitleRole | QString | empty | subtitle (Bookmark), meta (Attachment), family (Font), details (Recent item) |
/// | \p TextRole | QString | empty | text (Note), snippet (Search result) |
/// | \p DateRole | QString, QDate, QDateTime | empty | date (Note), time of opening (Recent item) |
/// | \p IconNameRole | QString | the icon of the design | badge (Note: highlighter, Attachment: file-text), icon (Shortcut: folder-open) |
/// | \p HueRole | int (PDFAccentHue) | the hue of the design | badge (Note: Amber, Attachment: Neutral) |
/// | \p TagRole | QString | empty (no tag) | tag (Bookmark: "Auto"), type (Font: "TrueType") |
/// | \p PixmapRole | QPixmap, QImage or QIcon, else Qt::DecorationRole | none (placeholder) | page (Thumbnail), first page (Recent item); read only for painting, never for the size |
/// | \p MatchRangesRole | QPoint (start, length), a list of them (QVariantList, QList<QPoint>) | none | found terms (Search result) |
/// | \p EmbeddedRole | bool | true | status (Font) |
/// | \p StatusTextRole | QString | "Embedded subset" / "Not embedded" | status (Font) |
/// | \p ItemTypeRole | int (PDFComponentPainter::RecentItemType) | File | type (Recent item) |
/// | \p ProgressRole | double 0 to 1 | none (no bar) | reading progress (Recent item, File) |
/// | \p ProgressTextRole | QString | empty | "Page 12 of 24" (Recent item) |
/// | \p PinnedRole | bool | false | star (Recent item; written by the star) |
/// | \p ShortcutRole | QKeySequence or QString | none ("Add shortcut") | shortcut (Shortcut; written by the recording) |
/// | \p StarVisibleRole | bool | true | star (Recent item; false: no star and no pin action - an item, which cannot be pinned) |
/// | \p MissingRole | bool | false | the badge "No longer exists" instead of the progress, the name text/muted (Recent item) |
/// | \p LockedRole | bool | false | the lock instead of the thumbnail of a document protected by a password (Recent item, File) |
/// | \p IndentRole | int (pixels, 0 - 96) | 0 | indentation of the row at its leading side (Note: a reply 24, note-row-reply 1025:21921) |
/// | \p StateIconRole | QString | empty (no icon) | icon of a state at the trailing side of the text, icon/muted 14 (Note: eye-off of a hidden annotation, 1026:21995) |
///
/// Qt::ToolTipRole is the tooltip of the item outside of its actions. A valid
/// QSize in Qt::SizeHintRole is the size of the item (as QStyledItemDelegate).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFComponentRoles
{
public:
    PDFComponentRoles() = delete;

    enum Role : int
    {
        FirstRole = Qt::UserRole + 0x0C00,
        SubtitleRole = FirstRole,
        TextRole,
        DateRole,
        IconNameRole,
        HueRole,
        TagRole,
        PixmapRole,
        MatchRangesRole,
        EmbeddedRole,
        StatusTextRole,
        ItemTypeRole,
        ProgressRole,
        ProgressTextRole,
        PinnedRole,
        ShortcutRole,
        StarVisibleRole,
        MissingRole,
        LockedRole,
        IndentRole,
        StateIconRole,
        LastRole = StateIconRole
    };
};

/// Common base of the delegates of the components (components.md 4.6), which
/// draw the items of the views by PDFComponentPainter:
/// - the state of an item comes from its QStyleOptionViewItem (enabled, hover,
///   selection, layout direction), a selected item reacts to the pointer
///   (decision 10); the selection of an item is drawn also, when the view does
///   not have the focus (it is the current page, bookmark, annotation...),
/// - the press is tracked by the delegate itself (Qt does not set State_Sunken
///   on items): a press of the left button on an item draws it pressed while
///   the pointer stays over it with the button held; a release, a move without
///   the button (a release lost to a modal dialog or to a drag), a leave, a
///   hide, the loss of the focus, a blocking modal window, a drag or a change
///   of the model ends it,
/// - actions inside of an item (save, copy, star, menu, key cap) react to the
///   hover and the press of the pointer, show their tooltips, a click on them
///   neither selects the item nor activates it, and they are reachable also by
///   the context menu of the item (the mouse, the context menu key, Shift+F10)
///   and by their keyboard shortcut on the current item, because assistive
///   technologies do not see them,
/// - the focus ring of 2 px inside of the current item is shown after the keyboard,
/// - a disabled item (of a disabled view, or without Qt::ItemIsEnabled) is drawn
///   by the rule O-08, its actions do nothing,
/// - the items are not edited by the generic editors of Qt (\p createEditor
///   returns nullptr); a delegate, which edits, has its own editor.
///
/// The delegates draw the rows of flat lists: the size of an item has no gap,
/// the view gives the spacing of the screens of the design (each delegate says
/// it). In a QTreeView the style leaves the area of the branches of the items of
/// a component delegate unfilled, so a nested item has one shape of the selection.
///
/// The delegate watches the views, which it paints (event filters on the view and
/// its viewport, installed at the first paint or size hint, or at once, when the
/// parent of the delegate is the view). One delegate can serve more views. A view
/// stops to be watched, when it no longer uses the delegate (setItemDelegate of
/// another delegate) or when it is destroyed; a viewport replaced by setViewport
/// is watched instead of the old one. The delegate never calls a function of a
/// view, which is being destroyed, and never touches a destroyed viewport (every
/// watched object is held in a QPointer). It never replaces anything of the
/// style, the style never replaces it. Every model, view and index may change or
/// vanish at any time; an index of another model than the model of the view is
/// treated as an item, which the view does not show.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFRowDelegate : public QStyledItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QStyledItemDelegate;

public:
    /// Action inside of an item
    struct Action
    {
        int id = -1;                ///< Identifier of the action (of the delegate)
        QRectF rect;                ///< Rectangle in the coordinates of the rectangle of the option (the viewport); empty: only in the context menu
        QString text;               ///< Text in the context menu (and the tooltip without \p toolTip)
        QString toolTip;            ///< Tooltip of the action
        QKeySequence shortcut;      ///< Shortcut on the current item (optional)
        bool enabled = true;
    };

    explicit PDFRowDelegate(QObject* parent = nullptr);
    virtual ~PDFRowDelegate() override;

    /// Returns the actions of the item (an invalid index has none)
    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    /// Returns the enabled action at the position (in the coordinates of the rectangle of the option), -1 for none
    int getActionAt(const QStyleOptionViewItem& option, const QModelIndex& index, const QPointF& position) const;

    /// Returns the state of the item for PDFComponentPainter: enabled, hover and
    /// selection from the option, the press tracked by the delegate (or
    /// State_Sunken of the option), the focus ring after the keyboard
    PDFComponentPainter::State getItemState(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    /// Returns the hover and the press (State_MouseOver, State_Sunken) of the action of the item
    QStyle::State getActionFlags(const QStyleOptionViewItem& option, const QModelIndex& index, int action) const;

    /// Returns the index of the pressed item (invalid, when no item is pressed)
    QModelIndex getPressedIndex() const;

    /// Returns the pressed action (-1, when no action is pressed)
    int getPressedAction() const;

    /// Returns the hovered action (-1, when no action is under the pointer)
    int getHoveredAction() const;

    /// The context menu of an item with actions is shown by the delegate, when the
    /// view has Qt::DefaultContextMenu. The filter of the delegate gets the event
    /// before the view: by default (automatic mode) the menu is shown only for the
    /// views of Qt and of this library, so a view class of an application, which
    /// has its own contextMenuEvent, keeps its menu. \p setContextMenuEnabled(true)
    /// shows the menu of the delegate for every view, false for none. An application
    /// with its own menu calls \p addActionsToMenu.
    bool isContextMenuEnabled() const;
    void setContextMenuEnabled(bool enabled);

    /// Adds the actions of the item to the menu (the actions call \p triggerAction;
    /// an action of a menu, whose view has been destroyed meanwhile, does nothing)
    /// \returns Number of the added actions
    int addActionsToMenu(QMenu* menu, QAbstractItemView* view, const QModelIndex& index);

    /// Creates the context menu of the item (nullptr, when the item has no action);
    /// the menu is deleted, when it is closed
    QMenu* createContextMenu(QAbstractItemView* view, const QModelIndex& index, QWidget* parent);

    /// Triggers the action of the item, as a click on it does: checks that the
    /// action is available (\p isActionAvailable: a valid index of the model of the
    /// view, an enabled item, view and action), remembers the view
    /// (\p getLastActionView) and calls \p performAction. Otherwise it does nothing.
    void triggerAction(QAbstractItemView* view, const QModelIndex& index, int action);

    /// Returns the view of the last triggered action (nullptr without a view, or
    /// when the view has been destroyed); a slot of \p actionTriggered places a
    /// menu of the action by \p getActionRect of this view
    QAbstractItemView* getLastActionView() const;

    /// Returns the rectangle of the action of the item in the viewport of the view (empty, if it has none)
    QRect getActionRect(QAbstractItemView* view, const QModelIndex& index, int action) const;

    /// Starts to watch the view (the delegate does it by itself at the first paint)
    void watchView(QAbstractItemView* view);

    /// Stops to watch the view (its event filters are removed, the hover attribute,
    /// which the delegate has set, too); the delegate does it by itself, when the
    /// view no longer uses it or is destroyed
    void unwatchView(QAbstractItemView* view);

    /// Returns true, if the delegate watches the view
    bool isWatching(const QAbstractItemView* view) const;

    /// Returns the number of the watched views (for the tests of the lifetime)
    int getWatchedViewCount() const;

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    virtual QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Readers of the roles: the default for an invalid index, a missing role or another type
    static QString getString(const QModelIndex& index, int role, const QString& defaultValue = QString());
    static bool getBool(const QModelIndex& index, int role, bool defaultValue);
    static qreal getReal(const QModelIndex& index, int role, qreal defaultValue);
    static int getInt(const QModelIndex& index, int role, int defaultValue);
    static PDFAccentHue getHue(const QModelIndex& index, int role, PDFAccentHue defaultValue);

    /// Returns the picture of the item (PixmapRole, else Qt::DecorationRole: a
    /// QPixmap, a QImage - converted once and kept in QPixmapCache by its cache
    /// key, so the scaled copy of PDFComponentPainter::drawScaledPixmap is found
    /// again at each repaint - or the largest pixmap of a QIcon up to 512 px)
    static QPixmap getPixmap(const QModelIndex& index);

    /// Returns the text of a value of a role: a string as it is, a date, a time,
    /// a date and time and a number formatted by the locale (as
    /// QStyledItemDelegate::displayText), an empty string for other types
    static QString getDisplayString(const QVariant& value, const QLocale& locale);

Q_SIGNALS:
    /// The action of the item has been triggered (a click, the context menu or the
    /// shortcut); \p getLastActionView is the view of the action
    void actionTriggered(const QModelIndex& index, int action);

protected:
    /// Draws the item by PDFComponentPainter
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const = 0;

    /// Returns the size of the item of the width (\p getItemWidth; -1, when the
    /// width is not known - the default width of the component is used). It never
    /// reads the picture of the item (a model may render it in Qt::DecorationRole).
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const = 0;

    /// Performs the action, which \p triggerAction has checked. The default emits \p actionTriggered.
    virtual void performAction(QAbstractItemView* view, const QModelIndex& index, int action);

    /// Returns true, if the item shows the selection (the rows without the axis
    /// Selected in the design - Font row, Recent item row, Shortcut row - do not)
    virtual bool hasSelection() const { return true; }

    /// Returns true, if the items have actions inside (the hit test is done only then)
    virtual bool hasActions() const { return false; }

    /// Returns true, if the height of an item follows its width (the size is asked
    /// again, when the width of the viewport or of a column of a tree changes)
    virtual bool hasHeightForWidth() const { return false; }

    /// Returns true, if the key on the current item of the view is used by the
    /// delegate (\p handleKey). The shortcuts of the application do not get such
    /// a key (ShortcutOverride). The default: the enabled action with the shortcut
    /// of the key (the keypad modifier is ignored).
    virtual bool isKeyHandled(QAbstractItemView* view, const QModelIndex& index, const QKeyEvent* event) const;

    /// Handles a key on the current item of the view, before the view gets it.
    /// The default triggers the action with the shortcut of the key.
    /// \returns true, if the key has been used
    virtual bool handleKey(QAbstractItemView* view, const QModelIndex& index, QKeyEvent* event);

    /// Returns the option of the item of the view (its rectangle and state); an
    /// item, which the view does not show (also an index of another model), has
    /// the rectangle of the size of the component at the origin
    QStyleOptionViewItem getViewOption(QAbstractItemView* view, const QModelIndex& index) const;

    /// Returns true, if the action of the item can be triggered: the index is
    /// valid (of the model of the view, if a view is given), the item and the
    /// view are enabled and the item has the enabled action
    bool isActionAvailable(QAbstractItemView* view, const QModelIndex& index, int action) const;

    /// Returns the width of the item: the rectangle of the option, or the width of
    /// the column (without the indentation of the level below the root index of a
    /// tree) or of the viewport of the view (a tree view asks for the height
    /// without the width); -1, when it is not known
    static int getItemWidth(const QStyleOptionViewItem& option, const QModelIndex& index);

    /// Repaints the item (the whole row of a table)
    static void updateIndex(QAbstractItemView* view, const QModelIndex& index);

    /// Returns the size remembered under the key (std::nullopt, when it is not
    /// remembered; the subclass computes it and remembers it by \p setCachedSize).
    /// The sizes are forgotten, when the theme, the fonts, the style or the
    /// language change. The key contains everything, on which the size depends
    /// (the texts and the width).
    [[nodiscard]] std::optional<QSize> getCachedSize(const QString& key) const;

    /// Remembers the size under the key (see \p getCachedSize)
    /// \returns The size
    QSize setCachedSize(const QString& key, const QSize& size) const;

    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void disconnectNotify(const QMetaMethod& signal) override;

private:
    /// A watched view and its watched viewport
    struct WatchedView
    {
        PDFWatchedPointer<QAbstractItemView> view;
        PDFWatchedPointer<QWidget> viewport;
        QPointer<QHeaderView> header;
        QMetaObject::Connection headerConnection;
        bool isHoverSet = false;    ///< The delegate has set Qt::WA_Hover of the viewport
        bool isFilterReinstallPending = false;  ///< The filter is installed again on the viewport set by setViewport (\p installPendingViewportFilters)
        int viewportWidth = -1;
    };

    /// Tracking of the pointer over one view
    void setHover(QAbstractItemView* view, const QModelIndex& index, int action, const QPoint& position);
    void resetHover();
    void clearPress();
    void clearPressOf(const QAbstractItemView* view);
    void repaintRect(const QRect& rect);
    static QRect getRowRect(QAbstractItemView* view, const QModelIndex& index);
    bool handleViewportEvent(QAbstractItemView* view, QEvent* event);
    bool handleViewEvent(QAbstractItemView* view, QEvent* event);
    void handlePointerMove(QAbstractItemView* view, const QPoint& position, bool isButtonHeld);
    bool showContextMenu(QAbstractItemView* view, const QModelIndex& index, const QPoint& globalPosition);
    bool isOwnIndex(const QAbstractItemView* view, const QModelIndex& index) const;
    bool isUsedByView(const QAbstractItemView* view) const;
    void onThemeChanged();
    void onWatchedDestroyed(QObject* object);
    void onHeaderSectionResized();
    void installPendingViewportFilters();
    void checkWatchedViews();
    void invalidateSizes();

    /// Watching from paint and sizeHint (const functions of Qt)
    void ensureWatched(const QWidget* widget) const;
    WatchedView* findWatched(const QObject* view);
    void installViewport(WatchedView& watched, QWidget* viewport);
    void uninstall(WatchedView& watched);

    enum class ContextMenuMode
    {
        Automatic,
        Always,
        Never
    };

    std::vector<WatchedView> m_views;
    PDFWatchedPointer<QAbstractItemView> m_trackedView;
    PDFWatchedPointer<QWidget> m_trackedViewport;
    QPointer<QAbstractItemView> m_lastActionView;
    QPersistentModelIndex m_hoverIndex;
    int m_hoverAction = -1;
    QRect m_hoverRect;
    QPoint m_hoverPosition;
    bool m_hasHoverPosition = false;
    QPersistentModelIndex m_pressedIndex;
    QRect m_pressedRect;
    int m_pressedAction = -1;
    bool m_pressedInside = false;
    bool m_isMoveWithButton = false;
    ContextMenuMode m_contextMenuMode = ContextMenuMode::Automatic;
    mutable QHash<QString, QSize> m_sizeCache;
};

/// Page of the thumbnails of the document (component Thumbnail item 191:777).
/// Roles: Qt::DisplayRole (page label), PixmapRole / Qt::DecorationRole (the
/// rendered page, read only for painting). The slot of the page is 108 x 148 by
/// default. The view of the screens (pdf4qt-document 193:756) is a QListView in
/// QListView::IconMode with the grid of the item and of the gaps 8 (columns) and
/// 6 (rows): setGridSize(item + QSize(8, 6)), setSpacing(0), Static movement.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThumbnailDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    explicit PDFThumbnailDelegate(QObject* parent = nullptr);

    /// Slot of the page, into which the picture is fitted (an invalid size is the
    /// default 108 x 148, each side is clamped to 4096)
    QSize getPageSize() const { return m_pageSize; }
    void setPageSize(const QSize& size);

    /// Returns the content of the item (\p withPixmap false: without the picture,
    /// which the size does not need)
    PDFComponentPainter::ThumbnailItem getContent(const QModelIndex& index, bool withPixmap = true) const;

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;

private:
    QSize m_pageSize = QSize(108, 148);
};

/// Bookmark of the document sidebar (component Bookmark row 191:911), the
/// filled bookmark icon. Roles: Qt::DisplayRole (title), SubtitleRole, TagRole
/// ("Auto"). The list of the screens (195:3445) has the spacing 2 between the
/// rows (QListView::setSpacing(2)).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFBookmarkDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    explicit PDFBookmarkDelegate(QObject* parent = nullptr);

    /// Returns the content of the item
    PDFComponentPainter::BookmarkRow getContent(const QModelIndex& index) const;

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
};

/// Annotation of the Notes tab (component Note row 191:998), the author in
/// text/accent when selected. Roles: Qt::DisplayRole (author), DateRole,
/// TextRole, IconNameRole and HueRole (the badge of the type), IndentRole (a
/// reply is indented by 24 - the whole row with its selection, 1025:21920) and
/// StateIconRole (eye-off of a hidden annotation, 1026:21995). The height
/// follows the width. The list of the screens (195:3910) has the spacing 2.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFNoteDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    explicit PDFNoteDelegate(QObject* parent = nullptr);

    /// Returns the content of the item
    PDFComponentPainter::NoteRow getContent(const QModelIndex& index) const;

    /// Largest indentation of a row (IndentRole is bounded to 0 - 96)
    static constexpr int MAXIMUM_INDENT = 96;

    /// Indentation of the item (IndentRole, bounded)
    static int getIndent(const QModelIndex& index);

    /// Rectangle of the row of the item in the rectangle of the item: without the
    /// indentation at the leading side of \p direction
    static QRect getIndentedRect(const QRect& itemRect, int indent, Qt::LayoutDirection direction);

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
    virtual bool hasHeightForWidth() const override { return true; }
};

/// File embedded in the document (component Attachment row 191:1045) with the
/// save action (Icon button 24 download). Roles: Qt::DisplayRole (name),
/// SubtitleRole (meta), IconNameRole and HueRole (the badge). The list of the
/// screens (195:2070) has the spacing 2.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAttachmentDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    enum ActionId
    {
        SaveAction = 0  ///< Save the file ("Save As…")
    };

    explicit PDFAttachmentDelegate(QObject* parent = nullptr);

    /// Returns the content of the item (without the states of the save button)
    PDFComponentPainter::AttachmentRow getContent(const QModelIndex& index) const;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
    virtual bool hasActions() const override { return true; }
};

/// Occurrence of the search (component Search result row 191:1073), the found
/// terms Semi Bold text/primary. Roles: Qt::DisplayRole (page, "Page 308"),
/// TextRole (snippet), MatchRangesRole (the found terms in the snippet). The
/// height follows the width. The list of the screens (198:4520) has the spacing 2.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFSearchResultDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    explicit PDFSearchResultDelegate(QObject* parent = nullptr);

    /// Returns the content of the item
    PDFComponentPainter::SearchResultRow getContent(const QModelIndex& index) const;

    /// Returns the ranges of the value of MatchRangesRole (invalid ones are left out)
    static QList<PDFComponentPainter::TextRange> getMatchRanges(const QVariant& value);

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
    virtual bool hasHeightForWidth() const override { return true; }
};

/// Font of the document (component Font row 206:944) with the copy action on
/// the hovered row (it copies the name of the font into the clipboard, also by
/// Ctrl+C and the context menu). Roles: Qt::DisplayRole (name), SubtitleRole
/// (family), TagRole (type), EmbeddedRole, StatusTextRole. The rows of the
/// screen (236:9258) follow each other without a gap.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFFontDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    enum ActionId
    {
        CopyAction = 0  ///< Copy the name of the font
    };

    explicit PDFFontDelegate(QObject* parent = nullptr);

    /// Returns the content of the item
    PDFComponentPainter::FontRow getContent(const QModelIndex& index) const;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    virtual void performAction(QAbstractItemView* view, const QModelIndex& index, int action) override;
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
    virtual bool hasSelection() const override { return false; }
    virtual bool hasActions() const override { return true; }
};

/// Item of the Recent list (component Recent item row 79:1026) with the star
/// (it writes PinnedRole) and the menu button (the application shows its menu
/// on the signal actionTriggered with MenuAction, \p getActionRect of
/// \p getLastActionView places it). Roles: Qt::DisplayRole (name), SubtitleRole
/// (details), ProgressTextRole, ProgressRole, DateRole (opened), ItemTypeRole,
/// PixmapRole, PinnedRole, StarVisibleRole (an item without the star has no pin
/// action), MissingRole, LockedRole. The rows of the screen (80:910) follow each
/// other without a gap.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFRecentItemDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    enum ActionId
    {
        PinAction = 0,  ///< Pin or unpin the item (the star)
        MenuAction = 1  ///< Show the menu of the item
    };

    explicit PDFRecentItemDelegate(QObject* parent = nullptr);

    /// Returns the content of the item (without the states of the nested
    /// buttons; \p withPixmap false: without the picture, which the size does not need)
    PDFComponentPainter::RecentItemRow getContent(const QModelIndex& index, bool withPixmap = true) const;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    virtual void performAction(QAbstractItemView* view, const QModelIndex& index, int action) override;
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
    virtual bool hasSelection() const override { return false; }
    virtual bool hasActions() const override { return true; }
};

/// Editor of the shortcut of PDFShortcutDelegate: it records one key with its
/// modifiers. The application shortcuts do not steal the keys while it has the
/// focus. Escape and the loss of the focus cancel the recording; Tab and
/// Backtab (with Shift only) cancel it and the delegate moves the focus, as the
/// key would without the recording (the view gets the key after the editor is
/// closed). The combination is normalized as QKeySequenceEdit does: Backtab is
/// Shift+Tab, a symbol typed with Shift ("!" of Shift+1) is the symbol alone, a
/// symbol typed with AltGr (a character, which is not a letter or a digit) is
/// the symbol alone.
///
/// Ctrl+Alt on Windows: Windows cannot tell AltGr from Ctrl+Alt and give the
/// key of the AltGr level ("[" of Ctrl+Alt+F on the Czech layout). The
/// combination is always all the modifiers with the base key of the physical
/// key - its character without the modifiers in the current layout, uppercased
/// (Ctrl+Alt+F, Ctrl+Alt+V for "@" of AltGr+V, Ctrl+Alt+Ě for the key "ě/2" of
/// the Czech layout, Ctrl+Alt+É on the French one, Ctrl+Alt+Ф on the Russian
/// one, Ctrl+Alt+´ for a dead key). This is the candidate, which the shortcut
/// map of Qt always matches for the key (QWindowsKeyMapper::
/// possibleKeyCombinations); the character of the AltGr level ("@") is another
/// candidate of the same key, so both shortcuts fire on the same key press. A
/// key without a character (F-keys, arrows, Enter, the keypad without NumLock)
/// keeps its Qt key. Other platforms are not changed. The pure mapping is
/// getWindowsKeyCombination, the lookup of the layout getBaseCharacter.
///
/// It paints nothing, the delegate draws the row in the state Recording.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFShortcutRecorder : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFShortcutRecorder(QWidget* parent = nullptr);

    /// Returns the recorded sequence (empty before a key is recorded)
    const QKeySequence& getKeySequence() const { return m_sequence; }

    /// Returns true, if a key has been recorded
    bool hasKeySequence() const { return !m_sequence.isEmpty(); }

    /// Returns the move of the focus requested by the key, which has canceled the
    /// recording: 1 Tab (next), -1 Backtab (previous), 0 none
    int getFocusMove() const { return m_focusMove; }

    /// Returns true, if the key (without the modifiers) can be a shortcut (not a
    /// lone modifier, not an unknown key)
    static bool isRecordableKey(int key);

    /// Returns the normalized combination of the key event (see the class)
    static QKeyCombination getKeyCombination(const QKeyEvent* event);

    /// Returns the combinations, which the key event matches as a shortcut (of
    /// the actions of PDFRowDelegate, the start keys of PDFShortcutDelegate):
    /// the recorded combination (getKeyCombination), the combination of the
    /// event and the combination of the other platforms (the AltGr symbol alone),
    /// without the keypad modifier and without duplicates; empty for nullptr
    static QList<QKeyCombination> getMatchingKeyCombinations(const QKeyEvent* event);

    /// Returns the normalized combination of a key on the platforms other than
    /// Windows (and of a key without the base key on Windows): \p key and \p text
    /// of the event, the keypad modifier is dropped
    static QKeyCombination getKeyCombination(int key, Qt::KeyboardModifiers modifiers, const QString& text);

    /// Returns the normalized combination of a key on Windows (see the class):
    /// with Ctrl+Alt and a valid \p baseCharacter (the character of the key
    /// without the modifiers, as getBaseCharacter gives it; 0 - none) all the
    /// modifiers with the uppercased base character, otherwise the combination
    /// of the other platforms. Pure function (it does not read the layout).
    static QKeyCombination getWindowsKeyCombination(int key, Qt::KeyboardModifiers modifiers, const QString& text, char16_t baseCharacter);

    /// Returns the character of the virtual key without the modifiers in the
    /// current keyboard layout of Windows (ToUnicode without the keys held, the
    /// spacing character of a dead key, as Qt computes qtKey[0] of the key);
    /// 0, when the key has no character, for an invalid virtual key and on
    /// other platforms
    static char16_t getBaseCharacter(quint32 virtualKey, quint32 scanCode);

Q_SIGNALS:
    /// A key has been recorded
    void recorded(const QKeySequence& sequence);

    /// The recording has been canceled (Escape, Tab, the loss of the focus)
    void canceled();

protected:
    virtual bool event(QEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    void cancel();

    QKeySequence m_sequence;
    int m_focusMove = 0;
    bool m_isFinished = false;
};

/// Action of the table of the shortcuts (component Shortcut row 145:2904). A
/// click on the key cap (or on the placeholder "Add shortcut"), Enter, Space and
/// F2 start the recording of a new shortcut: the editor of the delegate
/// (PDFShortcutRecorder), the row shows the cap with the focus ring and "Press
/// keys…". The recorded sequence is written into ShortcutRole (the item must be
/// editable, Qt::ItemIsEditable); a sequence, which another row of the same
/// parent already has, is reported by \p shortcutConflict and it is not written,
/// unless \p setConflictsAccepted. The context menu offers "Change shortcut" and
/// "Remove shortcut" (it writes an empty sequence). Roles: Qt::DisplayRole
/// (action), ShortcutRole, IconNameRole. The rows of the screen (147:4184)
/// follow each other without a gap.
///
/// The delegate can be shared by more views: each recorder remembers its view
/// and its item, the recorded sequence is committed and the editor closed only
/// in the view, which owns it (the signals commitData and closeEditor of the
/// delegate, which every view of the delegate gets, are not used).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFShortcutDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    enum ActionId
    {
        RecordAction = 0,   ///< Record a new shortcut (the key cap)
        ClearAction = 1     ///< Remove the shortcut (context menu only)
    };

    explicit PDFShortcutDelegate(QObject* parent = nullptr);

    /// Returns the content of the item (recording: in any view)
    PDFComponentPainter::ShortcutRow getContent(const QModelIndex& index) const;

    /// Returns the shortcut of the item (ShortcutRole as a QKeySequence or a portable string)
    static QKeySequence getShortcut(const QModelIndex& index);

    /// Returns true, if the item records a shortcut now (in any view)
    bool isRecording(const QModelIndex& index) const;

    /// Returns true, if the item records a shortcut in the view
    bool isRecording(const QAbstractItemView* view, const QModelIndex& index) const;

    /// Returns the index of the recorded item (invalid, when nothing is recorded;
    /// the last started recording, when more views record)
    QModelIndex getRecordingIndex() const;

    /// Returns the number of the running recordings (of all views)
    int getRecordingCount() const;

    /// A conflicting sequence is written too (it is reported always)
    bool isConflictsAccepted() const { return m_conflictsAccepted; }
    void setConflictsAccepted(bool accepted) { m_conflictsAccepted = accepted; }

    /// Returns the rows of the same parent (and column), which already have the sequence
    static QModelIndexList findConflicts(const QModelIndex& index, const QKeySequence& sequence);

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    virtual QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual void destroyEditor(QWidget* editor, const QModelIndex& index) const override;
    virtual void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    virtual void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    virtual void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

Q_SIGNALS:
    /// The recorded sequence is the shortcut of other rows (\p conflicts)
    void shortcutConflict(const QModelIndex& index, const QKeySequence& sequence, const QModelIndexList& conflicts);

    /// The shortcut of the item has been changed by the recording or removed
    /// (emitted only, when the model has really changed the value)
    void shortcutChanged(const QModelIndex& index, const QKeySequence& sequence);

protected:
    virtual void performAction(QAbstractItemView* view, const QModelIndex& index, int action) override;
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
    virtual bool hasSelection() const override { return false; }
    virtual bool hasActions() const override { return true; }
    virtual bool isKeyHandled(QAbstractItemView* view, const QModelIndex& index, const QKeyEvent* event) const override;
    virtual bool handleKey(QAbstractItemView* view, const QModelIndex& index, QKeyEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// A running recording: the editor, its view and its item
    struct Recording
    {
        QPointer<PDFShortcutRecorder> recorder;
        QPointer<QAbstractItemView> view;
        QPersistentModelIndex index;
    };

    void onRecorderRecorded(const QKeySequence& sequence);
    void onRecorderCanceled();
    void onRecorded(PDFShortcutRecorder* recorder, const QKeySequence& sequence);
    void onCanceled(PDFShortcutRecorder* recorder);
    bool startRecording(QAbstractItemView* view, const QModelIndex& index);
    const Recording* findRecording(const PDFShortcutRecorder* recorder) const;
    void purgeRecordings() const;
    PDFComponentPainter::ShortcutRow getRowContent(const QAbstractItemView* view, const QModelIndex& index) const;
    static bool isStartKey(const QKeyEvent* event);
    static void closeEditorOfView(QAbstractItemView* view, PDFShortcutRecorder* recorder, bool isCommitted);

    mutable std::vector<Recording> m_recordings;
    bool m_conflictsAccepted = false;
};

}   // namespace pdf

#endif // PDFCOMPONENTDELEGATES_H
