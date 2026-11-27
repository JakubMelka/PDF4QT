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

#ifndef PDFCOMPONENTSECTIONS_H
#define PDFCOMPONENTSECTIONS_H

#include "pdffluentglobal.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"

#include <QList>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QVBoxLayout;

namespace pdf
{

class PDFLabel;
class PDFTag;
class PDFTextLink;

/// Surface of the card of PDFExpanderCard: it draws the card (bg/surface,
/// border/default, radius 10) behind the content of the card. A part of the
/// expander card, never created alone by an application.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFExpanderCardSurface : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFExpanderCardSurface(QWidget* parent = nullptr);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
};

/// Section of a panel with a heading and a card (section-... of the right
/// panel of Edit Content, edit-content-v2 1647:21843 collapsed, -transparency
/// 1647:48204 expanded). The header: the heading Semi Bold 13 text/primary, an
/// optional badge (Tag, 1647:21725) and the link Expand / Collapse (Text link)
/// at the trailing side, gap 8. Below it (gap 8) the card: bg/surface,
/// border/default, radius 10, padding 12, its items 8 apart. The card holds
/// - the content widget: shown in both states,
/// - the expanded widget: shown only when expanded,
/// - the summary: a text Regular 11 text/tertiary, shown only when collapsed
///   ("Opacity 100 % · blend mode Normal").
/// The link is shown only when the card is expandable (it has an expanded
/// widget or a summary); a card without both is a plain section. The link is a
/// PDFTextLink (focus by the keyboard only, Space / Enter / Return activate
/// it, focus ring outside); its text is "Expand" / "Collapse" (translated).
/// The state is a property with the signal expandedChanged, emitted last.
/// GUI thread only. Widgets given to the card become its children; a replaced
/// widget is retired (PDFDeferredDeletion::retire).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFExpanderCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool expanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)

private:
    using BaseClass = QWidget;

public:
    explicit PDFExpanderCard(QWidget* parent = nullptr);
    explicit PDFExpanderCard(const QString& title, QWidget* parent = nullptr);
    virtual ~PDFExpanderCard() override;

    /// Heading of the section (a text of the user interface)
    QString getTitle() const;
    void setTitle(const QString& title);

    /// Text of the badge after the heading (data, "Text", "Path"); an empty
    /// text hides the badge
    QString getBadgeText() const;
    void setBadgeText(const QString& text);

    /// Summary shown in the card while it is collapsed; empty = none
    QString getSummaryText() const;
    void setSummaryText(const QString& text);

    /// Widget shown in the card in both states (null = none). The card takes
    /// it as its child; the previous widget is retired.
    QWidget* getContentWidget() const;
    void setContentWidget(QWidget* widget);

    /// Widget shown below the content widget only while the card is expanded
    /// (null = none). The card takes it as its child; the previous widget is retired.
    QWidget* getExpandedWidget() const;
    void setExpandedWidget(QWidget* widget);

    /// The card has something to expand or collapse (an expanded widget or a summary)
    bool isExpandable() const;

    /// Expanded / collapsed state (collapsed by default). The state is kept
    /// also while the card is not expandable. The signal expandedChanged is
    /// emitted last, only on a change.
    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);
    void toggleExpanded();

    /// Parts of the header and of the card (for tests and accessibility; null,
    /// when the application has deleted the part)
    PDFTextLink* getExpandLink() const;
    PDFTag* getBadge() const;
    PDFLabel* getTitleLabel() const;
    PDFLabel* getSummaryLabel() const;
    QWidget* getCard() const;

Q_SIGNALS:
    /// The state has changed (by the link, by setExpanded or by toggleExpanded)
    void expandedChanged(bool expanded);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void onExpandLinkClicked();

    /// Shows the parts of the state and the link of the expandable card
    void updateParts();

    /// Text of the link by the state (translated again after a change of the language)
    void updateLinkText();

    /// Puts the widget into the card at the position of the layout of the card
    /// (the previous widget at the place is retired)
    void replaceCardWidget(QPointer<QWidget>& slot, QWidget* widget, int position);

    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFTag> m_badge;
    QPointer<PDFTextLink> m_expandLink;
    QPointer<PDFExpanderCardSurface> m_card;
    QPointer<QVBoxLayout> m_cardLayout;
    QPointer<QWidget> m_contentWidget;
    QPointer<QWidget> m_expandedWidget;
    QPointer<PDFLabel> m_summaryLabel;
    bool m_expanded = false;
};

/// Segment of PDFPathBar: a link (Regular 12 / 11 text/accent, underlined under
/// the pointer, focus ring outside, radius 4) or the current segment (the last
/// one: Semi Bold 12 / Medium 11 text/primary, not interactive, no focus). The
/// text is data, taken literally. A part of the path bar.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPathSegment : public PDFComponentButton
{
    Q_OBJECT

private:
    using BaseClass = PDFComponentButton;

public:
    explicit PDFPathSegment(QWidget* parent = nullptr);

    /// Size of the text (PDFPathBar::Size)
    PDFComponentPainter::PathBarSize getSegmentSize() const { return m_size; }
    void setSegmentSize(PDFComponentPainter::PathBarSize size);

    /// The current segment (the last of the bar) is not a link: it takes no
    /// focus, the mouse and the keys do nothing on it
    bool isCurrent() const { return m_isCurrent; }
    void setCurrent(bool isCurrent);

    virtual bool isInteractive() const override { return !m_isCurrent; }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    PDFComponentPainter::PathBarSize m_size = PDFComponentPainter::PathBarSize::Default;
    bool m_isCurrent = false;
};

/// Breadcrumbs without routes (the bar of the group isolation-bar 1647:42965:
/// "Page 3 > Chart area > Revenue chart"; the path of types of the Selection
/// card 1647:21730 in the small size). The segments are data; the last one is
/// the current one (not a link), the others are links. Between them chevrons
/// (icon/chevron-right 10 px, icon/muted). Size Default: Regular 12, the current
/// segment Semi Bold 12, gap 8; Small: Regular 11, the current Medium 11, gap 4.
/// When the width does not fit, segments are hidden in the middle and replaced
/// by an ellipsis "…": the first and the last segment always stay, the hidden
/// run starts after the first segment and grows toward the end (the segments
/// near the current one stay longest); the first and the last segment are
/// elided as texts when not even "first > … > last" fits.
/// A hostile path (nested groups of a document): the bar keeps the list given,
/// but makes widgets only for the first segment and the last 255 - the ones
/// between are always in the ellipsis, so the cost is bounded for any length.
/// Every index of the bar (getShownSegments, getSegmentWidget, segmentActivated)
/// is an index of the list given to setSegments.
/// Keyboard: each shown link is a Tab stop (focus by the keyboard only, as a
/// text link); Left / Right move the focus between the shown links (mirrored in
/// right-to-left), Home / End to the first / last shown link; Space, Enter and
/// Return activate. When the focused link becomes the current segment (a slot of
/// segmentActivated trims the path), the focus goes to the nearest shown link
/// before it, without one to the next widget of the focus chain (as Tab). GUI
/// thread only. Segment widgets replaced by setSegments are retired (a slot of
/// segmentActivated may call setSegments).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFPathBar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using Size = PDFComponentPainter::PathBarSize;  ///< Default, Small

    explicit PDFPathBar(QWidget* parent = nullptr);
    explicit PDFPathBar(const QStringList& segments, QWidget* parent = nullptr);
    virtual ~PDFPathBar() override;

    /// Segments from the root to the current one (data, taken literally), as given
    QStringList getSegments() const { return m_segments; }
    void setSegments(const QStringList& segments);
    int getSegmentCount() const { return int(m_segments.size()); }

    Size getBarSize() const { return m_size; }
    void setBarSize(Size size);

    /// Indices of the segments shown at the current width, in their order
    QList<int> getShownSegments() const;

    /// A run of segments in the middle is replaced by the ellipsis
    bool isShortened() const;

    /// Widget of the segment (null for an index out of range and for a segment
    /// of a hostile path, which has no widget - see the class)
    PDFPathSegment* getSegmentWidget(int index) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Moves the focus from the segment by the key (Left, Right, Home, End);
    /// returns true, if the key was used. Called by the segments.
    bool moveFocus(const PDFPathSegment* from, int key);

Q_SIGNALS:
    /// A link (any segment but the last) was activated by the mouse or the
    /// keyboard; emitted last
    void segmentActivated(int index);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// Layout of the bar at a width, in the left-to-right coordinates of the bar
    struct Layout
    {
        QList<int> shown;                   ///< Indices of the widgets of the shown segments (m_segmentWidgets)
        std::vector<QRect> segmentRects;    ///< Rectangles of the shown segments (in the order of \p shown)
        std::vector<QRect> separatorRects;  ///< Rectangles of the separators
        QRect ellipsisRect;                 ///< Rectangle of the ellipsis (empty: not shortened)
        int width = 0;                      ///< Width of the whole content
    };

    void onSegmentClicked();
    Layout computeLayout(int width) const;
    void updateLayout();

    /// Index of the widget of the segment in m_segmentWidgets (-1: none)
    int getWidgetIndex(const QObject* segment) const;

    /// The index of the list given for the index of a widget, and back (-1: the
    /// segment has no widget)
    int toSegmentIndex(int widgetIndex) const;
    int toWidgetIndex(int segmentIndex) const;

    /// The focused segment has become the current one: the focus goes to the
    /// nearest shown link before it, or along the focus chain
    void moveFocusFromCurrentSegment();

    QStringList m_segments;
    std::vector<QPointer<PDFPathSegment>> m_segmentWidgets;    ///< The first segment and the last ones (at most MAX_SEGMENTS)
    int m_hiddenCount = 0;                                      ///< Segments after the first one without a widget
    Size m_size = Size::Default;
    Layout m_layout;
};

}   // namespace pdf

#endif // PDFCOMPONENTSECTIONS_H
