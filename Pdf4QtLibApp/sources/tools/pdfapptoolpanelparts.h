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

    /// \param hue Hue of the callout
    /// \param iconName Icon of the head
    /// \param title Title
    /// \param text Text, may be empty
    /// \param parent Parent widget
    explicit PDFAppToolCallout(PDFAccentHue hue, const QString& iconName, const QString& title, const QString& text, QWidget* parent);
    virtual ~PDFAppToolCallout() override;

    void setTexts(const QString& title, const QString& text);

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

private:
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_hint;
};

}   // namespace pdf

#endif // PDFAPPTOOLPANELPARTS_H
