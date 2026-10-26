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

#ifndef PDFAPPTOOLCATEGORYCARD_H
#define PDFAPPTOOLCATEGORYCARD_H

#include "pdfappglobal.h"
#include "pdfapppagelayout.h"

#include "pdfaccenthue.h"

#include <QList>
#include <QPointer>
#include <QWidget>

#include <vector>

class QVBoxLayout;

namespace pdf
{

class PDFLabel;
class PDFToolCategoryHeader;
class PDFToolRow;

/// Card of a category of the Tools page (category card 273:1647): the Tool
/// category header (61 high, the top corners of the inside of the card) and the
/// list of the tool rows (padding 6, gap 2), divided by the captions of the
/// subgroups (Semi Bold 11, letter spacing 4 %, capitals, text/tertiary; the first
/// visible caption 19 high, the others 25). The same card shows the tools of a
/// collection (Q1 of step 6, no captions). Rows are hidden by the filter of the
/// page; a caption without a visible row is hidden and the card without a visible
/// row too (updateVisibility).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCategoryCard : public PDFAppCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCard;

public:
    /// Paddings of the list and the gap of its items (273:1649)
    static constexpr int LIST_PADDING = 6;
    static constexpr int LIST_SPACING = 2;

    /// Height of a caption: the first visible one (padding 4 / 2) and the others (10 / 2)
    static constexpr int FIRST_CAPTION_TOP = 4;
    static constexpr int CAPTION_TOP = 10;
    static constexpr int CAPTION_BOTTOM = 2;
    static constexpr int CAPTION_LEFT = 8;
    static constexpr int CAPTION_LINE_HEIGHT = 13;

    /// Radius of the inside of the card (the top corners of the header)
    static constexpr int INNER_RADIUS = 11;

    explicit PDFAppToolCategoryCard(const QString& title, const QString& description, const QString& count,
                                    const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);
    virtual ~PDFAppToolCategoryCard() override;

    PDFToolCategoryHeader* getHeader() const { return m_header; }

    /// Starts a new subgroup with the caption (the rows added later belong to it)
    PDFLabel* addCaption(const QString& text);

    /// Adds the row to the current subgroup (the card takes it over)
    void addRow(PDFToolRow* row);

    /// Rows in their order (also the hidden ones)
    QList<PDFToolRow*> getRows() const;

    /// Rows, which the filter shows
    QList<PDFToolRow*> getVisibleRows() const;

    /// Captions of the subgroups in their order
    QList<PDFLabel*> getCaptions() const;

    /// Shows the captions of the subgroups with a visible row (the first visible
    /// caption has the smaller padding) and the card, when it has a visible row.
    /// \returns the number of the visible rows
    int updateVisibility();

private:
    struct Item
    {
        QPointer<PDFLabel> caption;
        QList<QPointer<PDFToolRow>> rows;
    };

    QPointer<PDFToolCategoryHeader> m_header;
    QPointer<QWidget> m_list;
    QPointer<QVBoxLayout> m_listLayout;
    std::vector<Item> m_items;
};

/// Grid of the category cards of the Tools page (all-tools 272:12888): the
/// visible cards in their order, two in a row (gap 16, rows gap 12), each card
/// keeps its own height and is aligned to the top of its row (Edit 173 high next
/// to Protect & Sign 698, no masonry). One visible card takes the whole width
/// (530:2957: 802), a page narrower than two cards of MINIMUM_CARD_WIDTH has the
/// cards below each other. Right-to-left mirrors the columns.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCategoryGrid : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int HORIZONTAL_SPACING = 16;
    static constexpr int VERTICAL_SPACING = 12;

    /// Narrowest card beside another card
    static constexpr int MINIMUM_CARD_WIDTH = 320;

    explicit PDFAppToolCategoryGrid(QWidget* parent = nullptr);
    virtual ~PDFAppToolCategoryGrid() override;

    /// Adds the card (the grid takes it over)
    void addCard(QWidget* card);

    /// Cards in their order (deleted ones are forgotten)
    QList<QWidget*> getCards() const;

    /// Visible cards in their order
    QList<QWidget*> getVisibleCards() const;

    /// Number of the columns at the width for the visible cards
    int getColumnCount(int width) const;

    /// Places the cards again (after cards or rows have been shown or hidden)
    void updateCards();

    virtual bool hasHeightForWidth() const override;
    virtual int heightForWidth(int width) const override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual bool event(QEvent* event) override;

private:
    /// Places the cards at the width (\p isApplied false = only measures); \returns the height
    int placeCards(int width, bool isApplied) const;

    static int getCardHeight(QWidget* card, int width);

    QList<QPointer<QWidget>> m_cards;
    bool m_isPlacing = false;
};

/// Group of widgets, which is one stop of Tab (tools.md 9.7): the arrows move the
/// focus inside of the group by the positions of the widgets (Up / Down to the
/// nearest widget above / below in the same column, Left / Right to the nearest
/// widget of the neighbor column - the positions on the screen, so right-to-left
/// is mirrored), Home / End to the first / last widget. Only the current widget
/// of the group takes the focus by Tab (Qt::StrongFocus), the others only by the
/// mouse (Qt::ClickFocus); a hidden current widget gives its place to the first
/// visible one. The tool pages: the pinned tiles, the grid of all tools, the
/// collections and the recent tools.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolFocusGroup : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// \param root Widget, in whose coordinates the positions are compared (the page)
    explicit PDFAppToolFocusGroup(QWidget* root, QObject* parent = nullptr);
    virtual ~PDFAppToolFocusGroup() override;

    /// Adds the widget at the end of the group (the order of Home / End)
    void addWidget(QWidget* widget);

    /// Removes the widget from the group (it keeps its focus policy)
    void removeWidget(QWidget* widget);

    /// Widgets of the group (deleted ones are forgotten)
    QList<QWidget*> getWidgets() const;

    /// Visible widgets of the group in their order
    QList<QWidget*> getVisibleWidgets() const;

    /// Widget, which is the stop of Tab (nullptr = none visible)
    QWidget* getCurrentWidget() const;
    void setCurrentWidget(QWidget* widget);

    /// Sets the focus policies by the visibility (after the filter of the page)
    void updateStops();

    /// Moves the focus from the widget by the key (an arrow, Home, End).
    /// \returns true, if the key is a key of the group
    bool moveFocus(QWidget* from, int key);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool isShown(const QWidget* widget) const;
    QRect getRect(const QWidget* widget) const;
    QWidget* findNeighbor(QWidget* from, int key) const;

    QPointer<QWidget> m_root;
    QList<QPointer<QWidget>> m_widgets;
    QPointer<QWidget> m_current;
};

}   // namespace pdf

#endif // PDFAPPTOOLCATEGORYCARD_H
