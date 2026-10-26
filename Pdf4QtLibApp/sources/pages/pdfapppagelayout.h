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

#ifndef PDFAPPPAGELAYOUT_H
#define PDFAPPPAGELAYOUT_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfappworkspace.h"

#include "pdffluentwidgets.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QFrame>
#include <QLayout>
#include <QList>
#include <QPointer>
#include <QUrl>
#include <QWidget>

#include <optional>
#include <vector>

class QAbstractButton;
class QBoxLayout;
class QHBoxLayout;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace pdf
{

/// Columns of a page (whatsnew-grid, help-grid, search-grid, about / sponsor
/// main-column + side-column, open-grid, recent-grid, dashboard-split-grid of Home):
/// columns of a fixed width beside stretched ones, the stretched columns share the
/// rest of the width by their stretch factors, the gap is 24. A page narrower than
/// the sum of the fixed widths, the minimal widths of the stretched columns and the
/// gaps has the columns below each other in the full width (the minimal window
/// 720 x 480); a hidden widget, which has no width yet, starts beside each other.
/// A column is a new widget with a vertical layout (gaps 24) or a widget of the
/// page, which the columns take over.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColumns : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Gap between the columns (and between the sections of a column)
    static constexpr int GAP = 24;

    /// Width of the side column of the design (340)
    static constexpr int SIDE_WIDTH = 340;

    /// Narrowest main column beside the side column of the info pages (About, Support, Sponsor)
    static constexpr int MINIMUM_MAIN_WIDTH = 400;

    /// Narrowest main column beside the side column of the pages of the content
    /// (What's New, the release notes, the manual, the search)
    static constexpr int CONTENT_MINIMUM_MAIN_WIDTH = 480;

    explicit PDFAppColumns(QWidget* parent = nullptr);
    virtual ~PDFAppColumns() override;

    /// Columns of a main column (stretched, at least \p minimumMainWidth wide beside
    /// the side column; object name mainColumn) and a side column of \p sideWidth
    /// (sideColumn), both new widgets with a vertical layout
    static PDFAppColumns* createMainAndSide(QWidget* parent, int minimumMainWidth = MINIMUM_MAIN_WIDTH, int sideWidth = SIDE_WIDTH);

    /// Columns of a page of the content placed into \p content (a widget without a
    /// layout): the widget \p main (stretched, at least \p minimumMainWidth wide beside
    /// the side column) and the widget \p side (SIDE_WIDTH); each column keeps its own
    /// height (whatsnew-grid, help-grid, search-grid)
    static PDFAppColumns* createInContent(QWidget* content, QWidget* main, QWidget* side, int minimumMainWidth = CONTENT_MINIMUM_MAIN_WIDTH);

    /// Adds a new column (a vertical layout with the gap of the sections 24).
    /// \param fixedWidth Width of the column beside the others (0 = stretched)
    /// \param stretch Stretch factor of a stretched column
    /// \param minimumWidth Narrowest stretched column, which is beside the others
    QWidget* addColumn(int fixedWidth, int stretch, int minimumWidth);

    /// Adds the widget as a column (the columns take it over). A column aligned to
    /// the top (\p alignment Qt::AlignTop) keeps its own height beside a higher
    /// column, otherwise the columns of a row have the same height.
    QWidget* addColumn(QWidget* widget, int fixedWidth, int stretch, int minimumWidth, Qt::Alignment alignment = Qt::Alignment());

    /// Column of the index (nullptr out of the range or for a deleted column) and its
    /// vertical layout (nullptr for a column of the page without a box layout)
    QWidget* getColumn(int index) const;
    QBoxLayout* getColumnLayout(int index) const;
    int getColumnCount() const { return int(m_columns.size()); }

    /// The main and the side column (createMainAndSide)
    QWidget* getMainColumn() const { return getColumn(0); }
    QWidget* getSideColumn() const { return getColumn(1); }
    QBoxLayout* getMainLayout() const { return getColumnLayout(0); }
    QBoxLayout* getSideLayout() const { return getColumnLayout(1); }

    /// The columns are below each other
    bool isStacked() const { return m_isStacked; }

    /// The columns are below each other at the width
    bool isStackedAt(int width) const;

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    struct Column
    {
        QPointer<QWidget> widget;
        int fixedWidth = 0;
        int stretch = 1;
        int minimumWidth = 0;
    };

    void updateArrangement(int width);

    std::vector<Column> m_columns;
    QPointer<QBoxLayout> m_layout;
    bool m_isStacked = false;
};

/// Grid of cells of the same width (quick action tiles, folder cards, pinned
/// cards, video cards, highlight cards, tool rows of What's New): as many columns
/// as the cells of the minimal width fit (at most \p maximumColumns), the columns
/// share the whole width (positions rounded to pixels, as the fractional widths of
/// the design), the cells of a row have the height of the highest one (stretched
/// as in the design). Hidden widgets take no cell, deleted or reparented widgets
/// are forgotten. Right-to-left mirrors the columns.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppGrid : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppGrid(int minimumCellWidth, int maximumColumns, int horizontalSpacing, int verticalSpacing, QWidget* parent = nullptr);
    virtual ~PDFAppGrid() override;

    /// Adds the widget as a cell (the grid takes it over)
    void addWidget(QWidget* widget);

    /// Visible cells in their order
    QList<QWidget*> getVisibleWidgets() const;

    /// Number of the cells, which the grid holds (visible or hidden)
    int getCellCount() const { return int(m_widgets.size()); }

    /// Number of the columns at the width
    int getColumnCount(int width) const;

    /// Places the cells again (after cells have been shown or hidden)
    void updateCells();

    virtual bool hasHeightForWidth() const override;
    virtual int heightForWidth(int width) const override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual bool event(QEvent* event) override;

private:
    /// Left and right edge of the column (the logical rectangle of the width)
    std::pair<int, int> getColumnEdges(int width, int columns, int column) const;
    int getCellWidth(int width, int columns) const;
    int getCellHeight(QWidget* widget, int cellWidth) const;
    void placeWidgets();
    void removeDeletedWidgets();

    QList<QPointer<QWidget>> m_widgets;
    int m_minimumCellWidth = 0;
    int m_maximumColumns = 1;
    int m_horizontalSpacing = 0;
    int m_verticalSpacing = 0;
    bool m_isPlacing = false;
};

/// Layout, which places its items in rows and wraps them (fact pills, buttons,
/// filter chips): the rows of a narrow page never need a horizontal scroll bar.
/// Items of a row are centered vertically; right-to-left mirrors the rows.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFlowLayout : public QLayout
{
    Q_OBJECT

private:
    using BaseClass = QLayout;

public:
    explicit PDFAppFlowLayout(QWidget* parent, int horizontalSpacing, int verticalSpacing);
    virtual ~PDFAppFlowLayout() override;

    virtual void addItem(QLayoutItem* item) override;
    virtual int count() const override;
    virtual QLayoutItem* itemAt(int index) const override;
    virtual QLayoutItem* takeAt(int index) override;
    virtual Qt::Orientations expandingDirections() const override;
    virtual bool hasHeightForWidth() const override;
    virtual int heightForWidth(int width) const override;
    virtual QSize minimumSize() const override;
    virtual QSize sizeHint() const override;
    virtual void setGeometry(const QRect& rect) override;

    /// Number of the rows of the items at the width (tests)
    int getRowCount(int width) const;

private:
    int doLayout(const QRect& rect, bool isApplied, int* rowCount) const;

    QList<QLayoutItem*> m_items;
    int m_horizontalSpacing = 0;
    int m_verticalSpacing = 0;
};

/// Row of a leading and a trailing part (the header of Video tutorials: the
/// texts and the search with the channel button, the search row of Recent): a row
/// narrower than the threshold has the trailing part below the leading part
class PDF4QTLIBAPPSHARED_EXPORT PDFAppResponsiveRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppResponsiveRow(QWidget* leading, QWidget* trailing, int threshold, int spacing, QWidget* parent = nullptr);
    virtual ~PDFAppResponsiveRow() override;

    /// The trailing part is below the leading part
    bool isStacked() const { return m_isStacked; }

protected:
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    void updateArrangement(int width);

    QPointer<QBoxLayout> m_layout;
    int m_threshold = 0;
    bool m_isStacked = false;
};

/// Header of a section of a page: the title Bold 15 text/primary (line 18, it
/// wraps in a narrow column) and an optional widget at the trailing side (a Text
/// link "View all", a hint)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSectionHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppSectionHeader(const QString& title, QWidget* parent = nullptr);
    virtual ~PDFAppSectionHeader() override;

    PDFAppParagraph* getTitleLabel() const { return m_title; }

    /// Places the widget at the trailing side (the header takes it over, the
    /// previous trailing widget is deleted)
    void setTrailingWidget(QWidget* widget);
    QWidget* getTrailingWidget() const { return m_trailing; }

private:
    QPointer<PDFAppParagraph> m_title;
    QPointer<QWidget> m_trailing;
    QPointer<QHBoxLayout> m_layout;
};

/// A card (bg/surface, border border/default, radius 12 - the style draws the
/// property card) with a vertical layout. Figma draws the border of a frame
/// outside of its padding box: the margins of the layout include it (createCard).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCard : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    explicit PDFAppCard(const QMargins& margins, int spacing, QWidget* parent = nullptr);
    virtual ~PDFAppCard() override;

    QVBoxLayout* getLayout() const { return m_layout; }

private:
    QPointer<QVBoxLayout> m_layout;
};

/// Builders of the parts of the pages, which are not components: plain texts,
/// section headers, cards and push buttons of the design, and the link property
/// of a widget (a route or an https address, which PDFAppContentPage follows)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageWidgets
{
public:
    PDFAppPageWidgets() = delete;

    /// Width of the border of the cards: the paddings of the design lie inside of
    /// it (Figma draws the border of a frame outside of its padding box)
    static constexpr int BORDER_WIDTH = 1;

    /// Label of a plain text (never a rich text) of the role and color
    static PDFLabel* createLabel(const QString& text, PDFThemeFonts::Role role, PDFThemeColors::Color color, QWidget* parent);

    /// Label of a plain text of the font of the weight and size
    static PDFLabel* createLabel(const QString& text, QFont::Weight weight, int pixelSize, PDFThemeColors::Color color, QWidget* parent);

    /// Plain text with the line height of the design ("leading normal" of Figma is
    /// the rounded line of Inter: 15 for 12 px, 16 for 13 px, 18 for 15 px; QLabel
    /// takes the line spacing of the font, which is 1 px more). The text wraps, its
    /// height follows the width.
    static PDFAppParagraph* createText(const QString& text, const QFont& font, int lineHeight, PDFThemeColors::Color color, QWidget* parent);

    /// Hint of a section header (Regular 12 text/muted, line 15)
    static PDFAppParagraph* createHint(const QString& text, QWidget* parent);

    /// Header of a section with an optional widget at its trailing side (hint, link)
    static PDFAppSectionHeader* createSectionHeader(const QString& title, QWidget* trailing, QWidget* parent);

    /// Card with the padding of the design (inside the border) and the spacing
    static PDFAppCard* createCard(int padding, int spacing, QWidget* parent);

    /// Push button of the design (primary or secondary) with an icon
    static QPushButton* createButton(const QString& text, const QString& iconName, bool isPrimary, QWidget* parent);

    /// Vertical layout of the widget without margins with the spacing
    static QVBoxLayout* createColumn(QWidget* widget, int spacing);

    /// Text of a button or of an action, whose '&' is not a mnemonic (a text of data)
    static QString escapeMnemonic(const QString& text);

    /// Name of the dynamic property of a widget holding its link (an https
    /// address or a route of the application)
    static constexpr const char* LINK_PROPERTY = "pdf4qtLink";

    /// Sets / returns the link of the widget (PDFAppContentPage::openLink follows it)
    static void setLink(QWidget* widget, const QString& link);
    static QString getLink(const QObject* object);
};

/// Base of the pages of the application (Home, Open, Recent, What's New, the
/// release notes, the manual, About, Support, Sponsor, the search):
///  - the position of the scroll area of the tab is the state of the view
///    (saveViewState / restoreViewState, key getScrollKey),
///  - anchors: a widget of the page (setAnchorWidget, a fragment of the route) or a
///    named anchor of the content (scrollToAnchor) is scrolled to the top after the
///    page has been shown and laid out (the tab resets the scroll of a new page),
///  - links: routes, https addresses and anchors "#..." of the content and of
///    buttons with the link property (withLink), anything else is refused.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppContentPage : public PDFAppPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPage;

public:
    explicit PDFAppContentPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppContentPage() override;

    /// Key of the scroll position in the view state
    static QString getScrollKey() { return QStringLiteral("scroll"); }

    virtual QVariantMap saveViewState() const override;
    virtual void restoreViewState(const QVariantMap& state) override;

    /// Scroll area of the tab, which holds the page (nullptr outside of a tab)
    QScrollArea* getScrollArea() const;

    /// Vertical position of the scroll area of the page (-1 without a scroll area)
    int getScrollPosition() const;

    /// Scrolls the scroll area of the page; the layout is finished first, so a
    /// position of a page, which has just been shown, is reached. \returns true,
    /// if the position could be reached.
    bool setScrollPosition(int position);

    /// Scrolls the scroll area, so the widget of the page is at its top (+ \p offset).
    /// \returns false, if there is no scroll area or no such widget
    bool scrollToWidget(QWidget* widget, int offset = 0);

    /// Opens a link: a route by the navigation, an https address by the system (the
    /// navigator checks it again), an anchor "#..." by scrolling (scrollToAnchor).
    /// Anything else is refused, an empty link does nothing. \returns false, if
    /// the page has been deleted by the navigation (do not touch members then).
    bool openLink(const QString& link);

protected:
    /// Scrolls to the named anchor of the content (default: nothing). \returns true,
    /// if the anchor exists.
    virtual bool scrollToAnchor(const QString& anchor);

    /// Requests a scroll after the layout of the page (the page has just been
    /// shown): a position (\p position >= 0) or a named anchor
    void requestDeferredScroll(int position, const QString& anchor);

    /// Widget of the anchor of the route (nullptr = none), scrolled to the top after
    /// the layout; the page sets it in setRoute
    void setAnchorWidget(QWidget* widget);

    /// The page asks to open the address (https only, checked by the navigator).
    /// \returns false, if the page has been deleted by it.
    bool openExternalUrl(const QUrl& url);

    /// Sets the link of the button and follows it, when the button is clicked
    QAbstractButton* withLink(QAbstractButton* button, const QString& link);

    virtual void showEvent(QShowEvent* event) override;

private:
    void queueDeferredScroll();
    void performDeferredScroll();

    /// A button with a link (withLink) has been clicked
    void onLinkClicked();

    std::optional<int> m_pendingScroll;
    QString m_pendingAnchor;
    QPointer<QWidget> m_anchorWidget;
    bool m_isScrollQueued = false;
};

}   // namespace pdf

#endif // PDFAPPPAGELAYOUT_H
