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

#ifndef PDFAPPANNOTATIONTOOLBAR_H
#define PDFAPPANNOTATIONTOOLBAR_H

#include "pdfappglobal.h"
#include "pdfappannotationcreator.h"
#include "pdfappfloatingbar.h"

#include <QPointer>
#include <QWidget>

#include <vector>

class QPushButton;

namespace pdf
{

class PDFAppPageLinkTargetSelect;
class PDFColorSwatchRow;
class PDFComboBox;
class PDFIconButton;
class PDFLabel;
class PDFSegmentedControl;

/// Icon of the design painted in a color of the theme at paint time (the icon
/// of the tool block of a floating bar, 16 px icon/accent)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFloatingBarIcon : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppFloatingBarIcon(const QString& iconName, int extent, QWidget* parent);
    virtual ~PDFAppFloatingBarIcon() override;

    const QString& getIconName() const { return m_iconName; }
    void setIconName(const QString& iconName);

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_iconName;
    int m_extent;
};

/// Step badge of the bar of Link to page ("1 / 2", step-number 1169:27253): a
/// pill bg/accent-subtle, radius 10, padding 8 / 2, Semi Bold 11 text/accent.
/// Not interactive, announced as a text.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStepBadge : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppStepBadge(QWidget* parent);
    virtual ~PDFAppStepBadge() override;

    const QString& getText() const { return m_text; }
    void setText(const QString& text);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    QString m_text;
};

/// Floating bar of the active creation tool (Figma 198:4978, 877:132786,
/// 207:6306, 207:6770, text box 1168:26016, Link to page 1169:26820 /
/// 1170:27196), 12 px below the toolbar, centered on the canvas. The parts
/// follow the tool:
/// - markups, Highlight and comment, sticky note: tool | swatches | close,
/// - line, polyline, freehand: tool | swatches | width | close,
/// - rectangle, polygon, ellipse: tool | Stroke / Fill, swatches | width | close
///   (Fill adds No color and white),
/// - text box, web link, stamp, Delete annotations: tool | hint | close,
/// - Link to page: tool | step badge + instruction | Target | (Back) | close.
/// A change of a color or of a width is stored for the next annotations at once
/// (PDFAppAnnotationCreator::setColor / setPenWidth); close and Esc end the tool.
/// A narrow canvas folds the swatches into More (PDFAppFloatingBar).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationToolBar : public PDFAppFloatingBar
{
    Q_OBJECT

private:
    using BaseClass = PDFAppFloatingBar;

public:
    explicit PDFAppAnnotationToolBar(PDFAppAnnotationCreator* creator, QWidget* parent);
    virtual ~PDFAppAnnotationToolBar() override;

    /// Tool shown by the bar (None = no parts)
    PDFAppAnnotationToolKind getKind() const { return m_kind; }

    /// Creates the parts of the tool (the old parts are deleted later)
    void configure(PDFAppAnnotationToolKind kind);

    /// Reads the values of the creator again (colors, width, target, step)
    void updateState();

    /// Gives the Target select of Link to page the zoom of the view (the hint of
    /// Keep the reader's zoom names the zoom, which a Position link stores)
    void updateZoom();

    /// Height of the card of a bar with a hint only (text box 1168:26016: the
    /// close button 20 + padding 6 + border 1 at the top and at the bottom)
    static constexpr int HINT_HEIGHT = 34;

    /// Height of the card of the step 2 of Link to page (1170:27196: Back 36)
    static constexpr int TARGET_STEP_HEIGHT = 50;

    /// Height of the Back button of Link to page (btn-back 1170:27603)
    static constexpr int BACK_BUTTON_HEIGHT = 36;

    /// The bar does not fit its parent with all its parts even with the swatches
    /// folded: the name of the tool is in the tooltip of its icon, the Stroke /
    /// Fill target shows only the icons and the width select only the value
    bool isCompact() const { return m_isCompact; }

    // Parts (tests)
    PDFAppFloatingBarIcon* getIcon() const;
    PDFLabel* getNameLabel() const;
    PDFLabel* getHintLabel() const;
    PDFAppStepBadge* getStepBadge() const;
    PDFLabel* getInstructionLabel() const;
    PDFSegmentedControl* getColorTargetControl() const;
    PDFColorSwatchRow* getColorRow() const;
    PDFComboBox* getWidthSelect() const;
    PDFAppPageLinkTargetSelect* getTargetSelect() const;
    QPushButton* getBackButton() const;
    PDFIconButton* getCloseButton() const;

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildParts();
    void updateCompactness();
    void onCompactnessQueued();
    void clearParts();
    void addPart(QWidget* widget);
    void updateTexts();
    void onColorSelected(const QColor& color);
    void onColorTargetChanged(int index);
    void onWidthActivated(int index);
    void onTargetChosen(pdf::DestinationType type, bool keepZoom);
    void onBackClicked();
    void onCloseClicked();

    QPointer<PDFAppAnnotationCreator> m_creator;
    PDFAppAnnotationToolKind m_kind = PDFAppAnnotationToolKind::None;
    std::vector<QPointer<QWidget>> m_parts;
    QPointer<PDFAppFloatingBarIcon> m_icon;
    QPointer<PDFLabel> m_name;
    QPointer<PDFLabel> m_hint;
    QPointer<PDFAppStepBadge> m_stepBadge;
    QPointer<PDFLabel> m_instruction;
    QPointer<PDFSegmentedControl> m_colorTarget;
    QPointer<PDFColorSwatchRow> m_colorRow;
    QPointer<PDFComboBox> m_widthSelect;
    QPointer<PDFAppPageLinkTargetSelect> m_targetSelect;
    QPointer<QPushButton> m_backButton;
    QPointer<QWidget> m_backDivider;
    QPointer<PDFIconButton> m_closeButton;
    int m_normalWidth = 0;
    bool m_isUpdating = false;
    bool m_isCompact = false;
    bool m_isCompactnessPending = false;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONTOOLBAR_H
