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

#ifndef PDFAPPTOOLPANELPARTS_H
#define PDFAPPTOOLPANELPARTS_H

#include "pdfappglobal.h"
#include "pdfappworkspace.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdffluentwidgets.h"

#include <QPointer>
#include <QProgressBar>
#include <QScrollArea>
#include <QWidget>

class QFontMetricsF;
class QVBoxLayout;

namespace pdf
{

/// Frame of the panels of a tool (sidebar-right 291:1177 and the panels of Rotate, Remove,
/// Links, Sanitize, Extract in the place of the sidebar): bg/sidebar with the line
/// border/default towards the canvas, the paddings of the frames (right 14 / 16 / 16 /
/// 16 + the border, left 14 / 10 / 12 / 10). A long content scrolls by an overlay bar.
/// The content comes from the GUI of the tool; the shell replaces it after a change
/// of the language, the frame stays in the view.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolSidePanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Side
    {
        Left,
        Right
    };

    /// Padding of the right panel (section-Result 293:1354 at x 17 of 320, y 14, width 287)
    static constexpr int RIGHT_PADDING_LEADING = 17;    ///< Padding 16 + the border
    static constexpr int RIGHT_PADDING_TOP = 14;
    static constexpr int RIGHT_PADDING_TRAILING = 16;
    static constexpr int RIGHT_PADDING_BOTTOM = 16;

    /// Padding of the left panel (389:20177: x 10, width 279 of 300, gap 10)
    static constexpr int LEFT_PADDING_LEADING = 10;
    static constexpr int LEFT_PADDING_TOP = 14;
    static constexpr int LEFT_PADDING_TRAILING = 11;    ///< Padding 10 + the border
    static constexpr int LEFT_PADDING_BOTTOM = 12;

    /// \param side The side of the canvas
    /// \param spacing Gap of the items of the body
    /// \param parent Parent widget
    explicit PDFAppToolSidePanel(Side side, int spacing, QWidget* parent);
    virtual ~PDFAppToolSidePanel() override;

    Side getSide() const { return m_side; }

    /// Parent of the content and of the card of the progress
    QWidget* getContentParent() const { return m_body; }

    /// Layout of the body: the content, the card of the progress above it
    QVBoxLayout* getBodyLayout() const { return m_bodyLayout; }

    /// Scroll area of the panel
    QScrollArea* getScrollArea() const { return m_scrollArea; }

    /// Replaces the content (the old one is deleted later: it may be a sender);
    /// nullptr = an empty panel
    void setContent(QWidget* content);
    QWidget* getContent() const { return m_content.data(); }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    Side m_side;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_body;
    QPointer<QVBoxLayout> m_bodyLayout;
    QPointer<QWidget> m_content;
};

/// Card of the progress of a tool (result-card 295:1792: 287 x 100, bg/surface,
/// radius 10, padding 12, gap 8): the title Semi Bold 15, the bar of 4 px, the step
/// Regular 12, the time left Regular 11. A missing step or time takes no place
/// (438:14093, 74 high).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolProgressCard : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    static constexpr int RADIUS = 10;
    static constexpr int PADDING = 13;  ///< Padding 12 + the border
    static constexpr int GAP = 8;

    explicit PDFAppToolProgressCard(QWidget* parent);
    virtual ~PDFAppToolProgressCard() override;

    /// \param percentage Progress 0 - 100
    /// \param title "Compressing… 37 %"
    /// \param stepText "Recompressing images · 152 of 412", may be empty
    /// \param timeText "About 40 seconds left", may be empty
    void setProgress(int percentage, const QString& title, const QString& stepText, const QString& timeText);

    PDFLabel* getTitleLabel() const { return m_title; }
    QProgressBar* getBar() const { return m_bar; }
    PDFLabel* getStepLabel() const { return m_step; }
    PDFLabel* getTimeLabel() const { return m_time; }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<PDFLabel> m_title;
    QPointer<QProgressBar> m_bar;
    QPointer<PDFLabel> m_step;
    QPointer<PDFLabel> m_time;
};

/// Callout of a panel of a tool (callout-blue 295:2156: the fill and the border of the
/// hue, radius 10, paddings 10 / 12 / 12 / 12, gap 8): the icon 16 of the hue, the title
/// Semi Bold 12 in lines of 16, the text Regular 12 text/secondary in lines of 17
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCallout : public PDFTintedCard
{
    Q_OBJECT

private:
    using BaseClass = PDFTintedCard;

public:
    static constexpr int RADIUS = 10;
    static constexpr int PADDING_H = 13;        ///< Padding 12 + the border
    static constexpr int PADDING_TOP = 11;      ///< Padding 10 + the border
    static constexpr int PADDING_BOTTOM = 13;   ///< Padding 12 + the border
    static constexpr int GAP = 8;
    static constexpr int TEXT_LINE = 17;
    static constexpr int TITLE_LINE = 16;       ///< Lines of the title (295:2625, also two of them)

    /// \param hue Hue of the callout
    /// \param iconName Icon of the head
    /// \param title Title
    /// \param text Text, may be empty
    /// \param parent Parent widget
    explicit PDFAppToolCallout(PDFAccentHue hue, const QString& iconName, const QString& title, const QString& text, QWidget* parent);
    virtual ~PDFAppToolCallout() override;

    void setTexts(const QString& title, const QString& text);

    /// The callout of a narrow panel (Remove Hidden Data 438:12703, Remove External Links
    /// 438:15026): radius 8, the head and the text 4 apart, the text Regular 11 in lines of 13
    void setCompactStyle();

    static constexpr int COMPACT_RADIUS = 8;
    static constexpr int COMPACT_GAP = 4;
    static constexpr int COMPACT_TEXT_LINE = 13;

    PDFLabel* getTitleLabel() const { return m_title; }
    PDFAppParagraph* getTextLabel() const { return m_text; }

    /// The layout of the callout, into which a tool adds its button (Try Smaller file 295:2182)
    QVBoxLayout* getBodyLayout() const { return m_layout; }

private:
    QPointer<QVBoxLayout> m_layout;
    QPointer<PDFLabel> m_title;
    QPointer<PDFAppParagraph> m_text;
};

/// Card of the sections of a panel (breakdown 293:1369: bg/surface, border/default,
/// radius 10, paddings 6 / 12 inside of the border)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCard : public PDFTintedCard
{
    Q_OBJECT

private:
    using BaseClass = PDFTintedCard;

public:
    static constexpr int RADIUS = 10;
    static constexpr int PADDING_H = 13;    ///< Padding 12 + the border
    static constexpr int PADDING_V = 7;     ///< Padding 6 + the border

    explicit PDFAppToolCard(QWidget* parent);
    virtual ~PDFAppToolCard() override;

    QVBoxLayout* getBodyLayout() const { return m_layout; }

private:
    QPointer<QVBoxLayout> m_layout;
};

/// Card of a measured size of a panel (result-card 293:1358: bg/surface, border/default,
/// radius 10, padding 12, gap 8): the size before Medium 15 text/tertiary, the arrow Medium
/// 15 text/muted, the size after Semi Bold 20 text/primary (gap 8), the bar of 4 px with
/// the share of the size after, the line of the saving Medium 12 - text/accent, without
/// the bar and in text/secondary, when nothing is saved (295:2263)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMeasuredCard : public PDFAppToolCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolCard;

public:
    static constexpr int PADDING = 13;  ///< Padding 12 + the border
    static constexpr int GAP = 8;

    explicit PDFAppToolMeasuredCard(QWidget* parent);
    virtual ~PDFAppToolMeasuredCard() override;

    /// \param before Text of the size before ("24.8 MB")
    /// \param after Text of the size after ("6.1 MB")
    /// \param afterPercentage Share of the size after in the bar, 0 - 100; -1 = nothing is saved
    /// \param savingText "75 % smaller · 18.7 MB saved"
    void setSizes(const QString& before, const QString& after, int afterPercentage, const QString& savingText);

    PDFLabel* getBeforeLabel() const { return m_before; }
    PDFLabel* getAfterLabel() const { return m_after; }
    QProgressBar* getBar() const { return m_bar; }
    PDFLabel* getSavingLabel() const { return m_saving; }

private:
    QPointer<PDFLabel> m_before;
    QPointer<PDFLabel> m_after;
    QPointer<QProgressBar> m_bar;
    QPointer<PDFLabel> m_saving;
};

/// Header of a whole panel (438:12686, 438:12810: "Hidden data" / "Found in Report.pdf"): the
/// title Semi Bold 14, 1 px below it the subtitle Regular 11 text/tertiary, 31 high
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPanelHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int TITLE_LINE = 17;
    static constexpr int SUBTITLE_LINE = 13;
    static constexpr int GAP = 1;

    explicit PDFAppToolPanelHeader(const QString& title, const QString& subtitle, QWidget* parent);
    virtual ~PDFAppToolPanelHeader() override;

    void setTexts(const QString& title, const QString& subtitle);

    PDFLabel* getTitleLabel() const { return m_title; }
    PDFLabel* getSubtitleLabel() const { return m_subtitle; }

private:
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_subtitle;
};

/// Header of a section of a panel (header 293:1355): the title Semi Bold 13 and the
/// hint Regular 11 text/muted on one baseline, 8 px apart, 16 high
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolSectionHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int HEIGHT = 16;
    static constexpr int GAP = 8;

    /// Height of a header of a list with its count (389:20403)
    static constexpr int COUNT_STYLE_HEIGHT = 22;

    explicit PDFAppToolSectionHeader(const QString& title, const QString& hint, QWidget* parent);
    virtual ~PDFAppToolSectionHeader() override;

    /// The hint is the count of a list at the trailing edge (389:20403)
    void setCountStyle();

    PDFLabel* getTitleLabel() const { return m_title; }
    PDFLabel* getHintLabel() const { return m_hint; }
    void setHint(const QString& hint);

    /// Puts a widget at the trailing edge of the header, the title takes the rest (the link
    /// Copy report 303:5371: the title and the link 8 apart, centered); the header owns it
    void addTrailingWidget(QWidget* widget);

private:
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_hint;
};

/// A chip over the canvas of a tool (the chips BEFORE / AFTER of Compress 293:1350, the chip
/// Lossless of Optimize Structure 302:4542): a pill icon/strong with Semi Bold 11 bg/surface and
/// an optional icon 14 at its leading side. The drawing is one function for the overlay of
/// Compress, which draws its chips into the canvas, and for this widget, which lies over the
/// canvas centered at the top (CHIP_TOP of the frames). The widget takes no mouse and no focus:
/// the wheel and the clicks reach the canvas below it. A child of the canvas area (the shell
/// deletes it, the canvas area when it dies first); it emits no signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCanvasChip : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int HEIGHT = 24;           ///< chip-lossless 302:4542
    static constexpr int TOP = 36;              ///< Below the top of the canvas (126 of 90)
    static constexpr int FONT_SIZE = 11;
    static constexpr int ICON_SIZE = 14;
    static constexpr int ICON_GAP = 6;
    static constexpr int PADDING_LEADING = 10;  ///< With an icon; without it the padding is the same at both sides
    static constexpr int PADDING_TRAILING = 12;
    static constexpr int MARGIN = 8;            ///< Distance from the edges of a narrow canvas, where the text is elided

    /// \param canvasArea The canvas area, the parent
    explicit PDFAppToolCanvasChip(QWidget* canvasArea);
    virtual ~PDFAppToolCanvasChip() override;

    /// The text and the icon of the chip; an empty text hides the chip
    void setChip(const QString& text, const QString& iconName);
    const QString& getText() const { return m_text; }
    const QString& getIconName() const { return m_iconName; }

    /// Width of a chip of the text: \p padding at each side, or the paddings of a chip with an icon
    static int getChipWidth(const QString& text, const QString& iconName, int padding);

    /// Draws a chip into its rectangle, in the direction (the icon at the leading side); the text
    /// is elided to the room of the chip
    static void paintChip(QPainter* painter, const QRect& rect, const QString& text, const QString& iconName, int padding, Qt::LayoutDirection direction);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    static QString elideText(const QFontMetricsF& metrics, const QString& text, int room);
    void updatePlacement();
    void onThemeChanged();

    QPointer<QWidget> m_canvasArea;
    QString m_text;
    QString m_iconName;
};

}   // namespace pdf

#endif // PDFAPPTOOLPANELPARTS_H
