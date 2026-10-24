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

#ifndef PDFAPPSEARCHWIDGETS_H
#define PDFAPPSEARCHWIDGETS_H

#include "pdfappglobal.h"
#include "pdfappsearchengine.h"

#include "pdfapppage.h"

#include "pdfcomponentpainter.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QDateTime>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <vector>

class QKeyEvent;

namespace pdf
{

/// Drawing of the parts of the global search (search-results-popover 285:2382,
/// results of pdf4qt-search-all 941:7550): the single place of their geometry.
/// The popup, the rows of the page and the tests use it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchPainter
{
public:
    PDFAppSearchPainter() = delete;

    /// Row of a result (result-*): 44 high, paddings 10 / 7, the badge 28, gap 12, radius 8
    static constexpr int ROW_HEIGHT = 44;
    static constexpr int ROW_PADDING_HORIZONTAL = 10;
    static constexpr int ROW_PADDING_VERTICAL = 7;
    static constexpr int ROW_GAP = 12;
    static constexpr int ROW_RADIUS = 8;
    static constexpr int BADGE_SIZE = 28;
    static constexpr int TITLE_HEIGHT = 16;
    static constexpr int SUBTITLE_HEIGHT = 13;
    static constexpr int EXTERNAL_ICON_SIZE = 14;

    /// Horizontal padding of the rows inside of the panel (row-wrap)
    static constexpr int ROW_WRAP_PADDING = 6;

    /// Header of a group (group-*): paddings 16 / 10 / 4, the texts 13 high, gap 6
    static constexpr int GROUP_HEIGHT = 27;
    static constexpr int GROUP_PADDING_HORIZONTAL = 16;
    static constexpr int GROUP_PADDING_TOP = 10;
    static constexpr int GROUP_GAP = 6;

    /// Show all (n) of a group of the page (show-all-*): 31 high, the text 56 from the edge, pt 6
    static constexpr int SHOW_ALL_HEIGHT = 31;
    static constexpr int SHOW_ALL_INDENT = 56;

    /// Footer of the popup (footer 285:2519): 34 high, bg/subtle, paddings 16 / 8,
    /// gaps 12 (between the four hints and the link) and 4 (inside of a hint)
    static constexpr int FOOTER_HEIGHT = 34;
    static constexpr int FOOTER_PADDING = 16;
    static constexpr int FOOTER_GAP = 12;
    static constexpr int KEY_GAP = 4;

    /// Panel: radius 12, border 1, the padding 6 above the first group
    static constexpr int PANEL_RADIUS = 12;
    static constexpr int PANEL_PADDING_TOP = 6;

    /// Key cap (kbd): 18 high, paddings 6 / 2, radius 4
    static constexpr int KEY_CAP_HEIGHT = 18;

    static QFont getTitleFont();
    static QFont getSubtitleFont();
    static QFont getMetaFont();
    static QFont getOpenFont();
    static QFont getGroupTitleFont();
    static QFont getGroupCountFont();
    static QFont getKeyCapFont();
    static QFont getFooterFont();
    static QFont getFooterLinkFont(bool isUnderlined);
    static QFont getShowAllFont(bool isUnderlined);

    /// Text "Open" beside the key cap Enter of the current result
    static QString getOpenText();
    static QString getEnterKeyText();

    /// Key caps of Ctrl+Enter, which opens the page of all results ("Ctrl", "Enter";
    /// the sign of the Command key and "Enter" on macOS)
    static QStringList getShowAllKeys();

    /// Draws a result into the rectangle of the row (without the row-wrap padding).
    /// The current result has bg/hover, "Open" and the key cap Enter instead of its
    /// meta text; the hover bg/hover, the press bg/pressed, the focus ring inside.
    static void paintResultRow(QPainter* painter, const QRectF& rect, const PDFAppSearchResult& result, const PDFComponentPainter::State& state, bool isCurrent);

    /// Rectangle of the badge of a row (in the left-to-right layout of the row)
    static QRectF getBadgeRect(const QRectF& rect);

    /// Draws the header of a group: the title Semi Bold 11 in capitals text/tertiary
    /// and the count Regular 11 text/muted ("6", "4 of 6")
    static void paintGroupHeader(QPainter* painter, const QRectF& rect, const QString& title, const QString& count, const PDFComponentPainter::State& state);

    /// Size and drawing of a key cap
    static QSizeF getKeyCapSize(const QString& text);
    static void paintKeyCap(QPainter* painter, const QRectF& rect, const QString& text);

    /// Draws the row Show all (n) of a group of the page
    static void paintShowAll(QPainter* painter, const QRectF& rect, const QString& text, const PDFComponentPainter::State& state);

    /// Text of the count of a group: "6", or "4 of 6" when a part is shown
    static QString getCountText(int shown, int total);
};

/// Row of a result of the page of all results (result-* of 941:7081). The whole
/// row activates the result; the title is the accessible name, the subtitle and
/// the meta text the description. The current row (the focused one, or the first
/// one of the page) shows "Open" and the key cap Enter.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchResultRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFAppSearchResultRow(const PDFAppSearchResult& result, QWidget* parent = nullptr);

    const PDFAppSearchResult& getResult() const { return m_result; }
    void setResult(const PDFAppSearchResult& result);

    bool isCurrent() const { return m_isCurrent; }
    void setCurrent(bool isCurrent);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual QString getDefaultAccessibleDescription() const override;

private:
    PDFAppSearchResult m_result;
    bool m_isCurrent = false;
};

/// Show all (n) › of a group of the page (show-all-Tools 955:23449): text/accent
/// Medium 12 with the chevron, underlined under the pointer; it filters the group
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchShowAllButton : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFAppSearchShowAllButton(PDFAppSearchGroup group, int count, QWidget* parent = nullptr);

    PDFAppSearchGroup getGroup() const { return m_group; }
    int getCount() const { return m_count; }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;

private:
    PDFAppSearchGroup m_group;
    int m_count = 0;
};

/// Header of a group of the page (group-* of 941:7550); announced as a text
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchGroupHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppSearchGroupHeader(const QString& title, const QString& count, QWidget* parent = nullptr);

    const QString& getTitle() const { return m_title; }
    const QString& getCount() const { return m_count; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_title;
    QString m_count;
};

/// Shadow of the popup of the global search (0 / 8 / 24 effect/shadow of the
/// popovers). A sibling below the popup, transparent for the pointer, so a click
/// into the shadow reaches the widget under it (and closes the popup).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchPopupShadow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppSearchPopupShadow(QWidget* parent);

    /// Margins of the shadow around the panel (as the popovers of the style)
    static QMargins getMargins();

    /// Places the shadow around the panel (in the coordinates of the parent)
    void setPanelGeometry(const QRect& panel);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QRect m_panel;
};

/// Popup of the global search below the field of the header (search-results-
/// popover 285:2382): the best results of the groups (at most MAXIMUM_ROWS, each
/// non-empty group gets a row, the rest the best scores), the headers of the
/// groups with their counts and the footer with the keyboard hints and "Show all
/// n results". It is a panel inside of the window (a child of the window raised
/// above the content), so the field keeps the keyboard focus and the input
/// method: the field moves the current row by the arrows (PDFAppWorkspaceHeader).
/// The pointer hovers and activates the rows. The popup never takes the focus.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchPopup : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Most rows of results in the popup
    static constexpr int MAXIMUM_ROWS = 10;

    /// Width of the panel (it is narrower in a narrow window)
    static constexpr int PANEL_WIDTH = 560;

    /// Gap between the field and the panel, the distance of the panel from the edges of the window
    static constexpr int ANCHOR_GAP = 6;
    static constexpr int WINDOW_MARGIN = 8;

    /// Item of the popup
    struct Item
    {
        enum class Kind
        {
            GroupHeader,
            Result,
            Message
        };

        Kind kind = Kind::Result;
        PDFAppSearchGroup group = PDFAppSearchGroup::Tools;
        int resultIndex = -1;   ///< Index of the result in its group
        QRect rect;             ///< Rectangle of the item in the popup (the whole width)
        QString count;          ///< Count text of a group header
    };

    /// \param window The window, over whose content the popup lies (its parent)
    explicit PDFAppSearchPopup(QWidget* window);
    virtual ~PDFAppSearchPopup() override;

    /// Shows the results (the current result stays, when it is still there, the first one otherwise)
    void setResults(const PDFAppSearchResults& results);
    const PDFAppSearchResults& getResults() const { return m_results; }

    /// Places the panel below the anchor (a rectangle in the coordinates of the
    /// window): aligned with its trailing edge, at most PANEL_WIDTH wide and as
    /// high as the window allows (fewer rows in a low window)
    void placeBelow(const QRect& anchor);

    /// Shows the popup with its shadow above the content of the window
    void open();

    /// Hides the popup and its shadow
    void close();

    bool isOpen() const { return isVisible(); }

    /// Laid out items (headers, rows, the message of no results)
    const std::vector<Item>& getItems() const { return m_items; }

    /// Rows of results in their order (indices into getItems)
    int getRowCount() const;

    /// Current row (-1 = none), its result (nullptr = none)
    int getCurrentRow() const { return m_currentRow; }
    void setCurrentRow(int row);
    const PDFAppSearchResult* getCurrentResult() const;

    /// Moves the current row by the step (clamped to the rows)
    void moveCurrentRow(int step);

    /// Result of the row (nullptr for an invalid row)
    const PDFAppSearchResult* getRowResult(int row) const;

    /// Rectangle of the row in the popup, the rectangle of the link Show all n results
    QRect getRowRect(int row) const;
    QRect getShowAllRect() const;

    /// Text of the link of the footer ("Show all 19 results", empty without a result)
    QString getShowAllText() const;

    /// Keyboard hints of the footer, which fit beside the link (navigate, open, show all, close)
    int getVisibleHintCount() const { return m_visibleHints; }

    /// Rectangle of a visible hint of the footer (its key caps and its text, in
    /// the popup), its key caps and text joined by spaces ("Ctrl Enter show all");
    /// empty for a hint, which is not shown
    QRect getHintRect(int index) const;
    QString getHintText(int index) const;

    /// Activates the current row (resultActivated), nothing without a current row
    void activateCurrent();

    /// The shadow (a sibling of the popup)
    PDFAppSearchPopupShadow* getShadow() const { return m_shadow; }

    virtual QSize sizeHint() const override;

Q_SIGNALS:
    /// A result has been activated (Enter, a click)
    void resultActivated(const pdf::PDFAppSearchResult& result);

    /// Show all n results has been activated
    void showAllRequested();

    /// The current row has changed (the field announces it)
    void currentRowChanged(int row);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;

private:
    /// Lays the items out for the height available below the anchor
    void layoutItems();

    /// Number of the shown results of each group for the number of rows
    std::vector<int> selectRows(int rowBudget) const;

    /// Height of the panel showing the counts
    int getPanelHeight(const std::vector<int>& counts) const;

    /// Row under the position (-1 for none)
    int getRowAt(const QPoint& position) const;

    void updateGeometryFromAnchor();
    void updateHints();

    PDFAppSearchResults m_results;
    std::vector<Item> m_items;
    std::vector<size_t> m_rowItems;
    QPointer<PDFAppSearchPopupShadow> m_shadow;
    QRect m_anchor;
    int m_availableHeight = 0;
    int m_currentRow = -1;
    int m_pressedRow = -1;
    bool m_isShowAllPressed = false;
    bool m_isShowAllHovered = false;
    int m_visibleHints = 4;
};

/// Controller of the global search of a field (the field Search tools,
/// documents, or help… of PDFAppWorkspaceHeader, Ctrl+K): a child of the field.
/// Typing searches after SEARCH_DELAY ms and opens the popup below the field;
/// the field keeps the focus. Keys of the field: Down / Up move the current row
/// (Down opens the popup again), Enter opens the current result (without a
/// popup the field asks for the page of all results), Ctrl+Enter asks for the
/// page of all results of the text (as the link Show all n results; nothing for
/// an empty text), Esc closes the popup (a
/// second Esc clears the field), Tab closes the popup and moves the focus on. The
/// popup closes, when the field loses the focus or is hidden, by a click outside
/// of the popup and of the field, when the window is deactivated or hidden. The
/// data (Recent, shortcuts) are read, when the popup opens, and again after a
/// change notification of the database while it is open: the results never
/// become stale. The controller asks the tab of the field for the routes.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchController : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Delay of the search after a change of the text
    static constexpr int SEARCH_DELAY = 60;

    /// \param field The search field (the parent of the controller)
    explicit PDFAppSearchController(PDFLineEdit* field);
    virtual ~PDFAppSearchController() override;

    /// Controller of the field (nullptr, if it has none)
    static PDFAppSearchController* find(const QWidget* field);

    PDFLineEdit* getField() const { return m_field; }

    /// The popup (nullptr before the first search)
    PDFAppSearchPopup* getPopup() const { return m_popup; }
    bool isPopupOpen() const;

    /// Searches the text of the field now (also the pending search after a key)
    void updateSearch();

    /// Closes the popup
    void closePopup();

    /// Time of "Opened yesterday" (tests; an invalid time = the clock)
    void setCurrentTime(const QDateTime& now) { m_now = now; }

    /// The search is waiting for its delay
    bool isSearchPending() const;

    /// Number of the searches done by the controller (tests: an event of the field
    /// is handled once, the data of Recent are read once per opening of the popup)
    int getSearchCount() const { return m_searchCount; }
    int getDataLoadCount() const { return m_dataLoadCount; }

Q_SIGNALS:
    /// A result or Show all asks for a route (connected to the tab of the field)
    void navigationRequested(const pdf::PDFAppRoute& route, pdf::PDFAppNavigationMode mode);

    /// A video tutorial asks for an external address (connected to the tab of the field)
    void externalUrlRequested(const QUrl& url);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void timerEvent(QTimerEvent* event) override;

private:
    /// Filter of the application installed while the popup is open: it reacts
    /// only to the presses outside of the popup and of the field (the controller
    /// itself filters the field and its window, so their events are handled once)
    class PressFilter;
    friend class PressFilter;

    void onApplicationPress(QObject* watched);
    void onTextEdited(const QString& text);
    void onReturnPressed();
    void onResultActivated(const pdf::PDFAppSearchResult& result);
    void onShowAllRequested();
    void onCurrentRowChanged(int row);
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onPopupDestroyed();

    bool handleFieldKey(QKeyEvent* event);

    /// Ctrl+Enter / Ctrl+keypad Enter (Cmd+Enter on macOS): the page of all results
    static bool isShowAllKey(const QKeyEvent* event);

    void openPopup(const PDFAppSearchResults& results);
    void placePopup();
    void watch(bool isOpen);
    void connectToTab();
    void connectToNotifier();
    PDFAppContext* findContext() const;

    QPointer<PDFLineEdit> m_field;
    QPointer<PDFAppSearchPopup> m_popup;
    QPointer<QWidget> m_watchedWindow;
    QPointer<QObject> m_connectedTab;
    QPointer<QObject> m_connectedNotifier;
    PDFAppSearchEngine m_engine;
    std::unique_ptr<PDFAppSearchData> m_data;
    QDateTime m_now;
    int m_timerId = 0;
    int m_searchCount = 0;
    int m_dataLoadCount = 0;
    bool m_isApplicationFiltered = false;
    QPointer<QObject> m_pressFilter;
};

/// Activation of the results of the search, which is not a navigation
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSearchActions
{
public:
    PDFAppSearchActions() = delete;

    /// Runs the command by the action of the main window of the widget.
    /// \returns false, if the window has no enabled action of the command
    static bool runCommand(QWidget* widget, const QString& commandId);

    /// Opens the document by the main window of the widget (its hook of opening
    /// the documents, step 3). \returns false without a main window
    static bool openDocument(QWidget* widget, const QString& filePath);
};

}   // namespace pdf

#endif // PDFAPPSEARCHWIDGETS_H
