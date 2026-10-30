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

#ifndef PDFAPPFLOATINGBAR_H
#define PDFAPPFLOATINGBAR_H

#include "pdfappglobal.h"

#include <QMargins>
#include <QPointer>
#include <QRect>
#include <QWidget>

class QHBoxLayout;

namespace pdf
{

class PDFColorSwatchRow;

/// Divider of a floating bar: a line 1 x 18 of border/separator, vertically centered
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFloatingBarDivider : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int LENGTH = 18;

    explicit PDFAppFloatingBarDivider(QWidget* parent);
    virtual ~PDFAppFloatingBarDivider() override;

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Shadow of a floating bar: a sibling below the bar, transparent for the mouse,
/// of the same geometry (as PDFAppInfoBarShadow)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFloatingBarShadow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppFloatingBarShadow(int radius, QWidget* parent);
    virtual ~PDFAppFloatingBarShadow() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    int m_radius;
};

/// Floating bar over the canvas of a document view (step 4 plan 3.1, Figma frames
/// below): the base of the tool bar of package A
/// (198:4978, 877:132786, 207:6306, 207:6770), of the annotation bar of package B
/// (1015:17319, 1018:18584) and of the markup bar (877:133774).
///
/// Shell: a card bg/surface with the border border/default (1 px inside), the shadow
/// 0 / 4 / 16 effect/shadow; Normal = height 46, padding 14 / 6 / 8 / 6 inside the
/// border, gap 10, radius 14; Compact (markup bar) = height 38, padding 4, gap 2,
/// radius 10. The children are laid out in a row (QHBoxLayout - mirrored for
/// right-to-left), the subclass adds them by addWidget / addDivider.
///
/// Placement: Top = 12 px below the toolbar (the top of the parent, which is the
/// canvas area), centered horizontally; Anchored = centered on a rectangle of the
/// parent, its bottom 8 px above the rectangle (below it, when there is no room
/// above), clamped to the parent with the margin 8. The bar follows the resizes of
/// its parent. A bar wider than its parent folds the swatches of its swatch row
/// (setFoldableSwatchRow) into the More button of the row.
///
/// The widget holds the margins of the shadow around the card; it is masked to its
/// card, so the pointer in the margins reaches the canvas below. The shadow is a
/// sibling below the bar (PDFAppFloatingBarShadow).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFloatingBar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Size
    {
        Normal,     ///< Tool bar, annotation bar (46)
        Compact     ///< Markup bar over the text selection (38)
    };

    enum class Placement
    {
        Top,        ///< 12 px below the top of the parent, centered
        Anchored    ///< Above (or below) a rectangle of the parent
    };

    static constexpr int NORMAL_HEIGHT = 46;
    static constexpr int COMPACT_HEIGHT = 38;
    static constexpr int NORMAL_RADIUS = 14;
    static constexpr int COMPACT_RADIUS = 10;
    static constexpr int NORMAL_GAP = 10;
    static constexpr int COMPACT_GAP = 2;

    /// Gap between the top of the canvas (the bottom of the toolbar) and the bar
    static constexpr int TOP_GAP = 12;

    /// Gap between an anchored bar and its rectangle
    static constexpr int ANCHOR_GAP = 8;

    /// Smallest distance of the card from the edges of the parent
    static constexpr int PARENT_MARGIN = 8;

    /// Margins of the shadow 0 / 4 / 16 around the card
    static QMargins getShadowMargins();

    /// Padding of the content inside of the card (the border included) for
    /// left-to-right; the left and right sides are swapped for right-to-left
    static QMargins getContentPadding(Size size);

    explicit PDFAppFloatingBar(Size size, QWidget* parent);
    virtual ~PDFAppFloatingBar() override;

    Size getSize() const { return m_size; }
    int getCardHeight() const;
    int getRadius() const;

    /// Height of the card of a variant, whose parts are lower or higher than the
    /// controls of the size (the card hugs its parts in Figma: a hint only 34, a
    /// button 36 - 50); 0 = the height of the size (46 / 38)
    void setCardHeight(int height);

    /// Layout of the children of the card
    QHBoxLayout* getContentLayout() const;

    /// Adds a child to the end of the row (the bar becomes its parent)
    void addWidget(QWidget* widget);

    /// Adds a divider to the end of the row
    PDFAppFloatingBarDivider* addDivider();

    /// Adds a fixed space to the end of the row
    void addSpacing(int spacing);

    /// Swatch row, whose swatches are folded into its More button, when the bar
    /// is wider than its parent (nullptr = none)
    void setFoldableSwatchRow(PDFColorSwatchRow* row);
    PDFColorSwatchRow* getFoldableSwatchRow() const;

    /// Places the bar 12 px below the top of the parent, centered
    void placeAtTop();

    /// Places the bar at the rectangle \p anchor (coordinates of the parent)
    void placeAnchored(const QRect& anchor);

    Placement getPlacement() const { return m_placement; }
    const QRect& getAnchor() const { return m_anchor; }

    /// Places the bar again (its size, the folding, the placement)
    void updatePlacement();

    /// Rectangle of the card in the coordinates of the bar
    QRect getCardRect() const;

    /// Rectangle of the card in the coordinates of the parent
    QRect getCardGeometry() const;

    /// Shadow below the bar (nullptr without a parent)
    PDFAppFloatingBarShadow* getShadow() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateShadow();
    void updateFolding();

    /// Margins of the layout: the shadow and the padding of the card, whose
    /// leading and trailing sides are mirrored for right-to-left (QBoxLayout
    /// mirrors the items, not its margins)
    void applyContentMargins();
    QSize getCardSizeHint() const;

    Size m_size;
    int m_cardHeight = 0;
    Placement m_placement = Placement::Top;
    QRect m_anchor;
    QPointer<QHBoxLayout> m_layout;
    QPointer<PDFColorSwatchRow> m_foldableRow;
    QPointer<PDFAppFloatingBarShadow> m_shadow;
    QPointer<QWidget> m_watchedParent;
    bool m_isUpdatingPlacement = false;
};

}   // namespace pdf

#endif // PDFAPPFLOATINGBAR_H
