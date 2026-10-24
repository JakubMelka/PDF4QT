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

#ifndef PDFAPPANNOTATIONCREATEDIALOGS_H
#define PDFAPPANNOTATIONCREATEDIALOGS_H

#include "pdfappglobal.h"

#include "pdfaction.h"
#include "pdfannotation.h"

#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QAbstractButton>
#include <QPointer>
#include <QWidget>

#include <map>

class QCheckBox;
class QPushButton;
class QVBoxLayout;

namespace pdf
{

class PDFColorSwatchRow;
class PDFIconButton;
class PDFLabel;
class PDFLineEdit;
class PDFPlainTextEdit;
class PDFSegmentedControl;
class PDFToolBadge;

/// Popup of a new text box (Figma pdf4qt-document-textbox-popup 1168:25948 /
/// -filled 1168:26582, the card of the comment popup 1025:21533): anchored below
/// the drawn rectangle (above it, when there is no room), 504 wide. The head
/// (Tool icon, "New text box", the page and the author, close), the multi-line
/// text, Font + size, Text color, Alignment, Border width + color (the swatch
/// row with "No color"), the hint and Cancel / Add. Add is disabled while the
/// text is empty; Ctrl+Enter or Add accepts, Esc, Cancel, the close button or a
/// press outside cancel (nothing is created). A child widget of the canvas area
/// (not a window): it follows the view, it never takes the focus of another
/// window.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppTextBoxPopup : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Width of the card 1168:26450
    static constexpr int CARD_WIDTH = 504;

    /// Gap between the rectangle and the card
    static constexpr int ANCHOR_GAP = 12;

    /// Smallest distance of the card from the edges of the parent
    static constexpr int PARENT_MARGIN = 8;

    /// Radius of the card
    static constexpr int RADIUS = 12;

    /// Width of the label column of the rows
    static constexpr int LABEL_WIDTH = 68;

    explicit PDFAppTextBoxPopup(QWidget* parent);
    virtual ~PDFAppTextBoxPopup() override;

    /// Margins of the shadow 0 / 8 / 24 around the card
    static QMargins getShadowMargins();

    /// Opens the popup at the rectangle \p anchor (coordinates of the parent) with
    /// the subtitle ("Page 308 · Jakub Melka") and the remembered style
    void open(const QRect& anchor, const QString& subtitle, const PDFFreeTextStyle& style, const QColor& borderColor, PDFReal borderWidth);

    /// Places the card at the rectangle again (a scroll of the view)
    void placeAt(const QRect& anchor);

    /// Card in the coordinates of the popup and of the parent
    QRect getCardRect() const;
    QRect getCardGeometry() const;

    /// Values of the form
    QString getText() const;
    void setText(const QString& text);
    PDFFreeTextStyle getStyle() const;
    QColor getBorderColor() const;
    PDFReal getBorderWidth() const;

    /// Accepts the form (Add, Ctrl+Enter); refused (false) for an empty text
    bool accept();

    /// Cancels the form (Esc, Cancel, close, a press outside)
    void cancel();

    // Parts (tests)
    PDFPlainTextEdit* getTextEdit() const;
    PDFComboBox* getFontCombo() const;
    PDFSpinBox* getSizeSpin() const;
    PDFColorSwatchRow* getTextColorRow() const;
    PDFSegmentedControl* getAlignmentControl() const;
    PDFSpinBox* getBorderWidthSpin() const;
    PDFColorSwatchRow* getBorderColorRow() const;
    QPushButton* getAddButton() const;
    QPushButton* getCancelButton() const;
    PDFIconButton* getCloseButton() const;
    PDFLabel* getTitleLabel() const;
    PDFLabel* getSubtitleLabel() const;

Q_SIGNALS:
    /// The text box has been accepted (the values are read by the getters)
    void accepted();

    /// The popup has been canceled
    void canceled();

    /// A value of the form has changed (the live preview of the tool)
    void previewChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* createRow(const char* label, QWidget* control);
    void createAlignmentControl();
    void retranslate();
    void updateState();
    void onTextChanged();
    void onValueChanged();
    void watchApplication(bool watch);

    QPointer<QWidget> m_shadow;
    QPointer<PDFToolBadge> m_badge;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_subtitle;
    QPointer<PDFIconButton> m_closeButton;
    QPointer<PDFPlainTextEdit> m_textEdit;
    QPointer<PDFComboBox> m_fontCombo;
    QPointer<PDFSpinBox> m_sizeSpin;
    QPointer<PDFColorSwatchRow> m_textColorRow;
    QPointer<QWidget> m_alignmentContainer;
    QPointer<PDFSegmentedControl> m_alignmentControl;
    QPointer<PDFSpinBox> m_borderWidthSpin;
    QPointer<PDFColorSwatchRow> m_borderColorRow;
    QPointer<PDFLabel> m_hint;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_addButton;
    std::vector<std::pair<QPointer<PDFLabel>, const char*>> m_rowLabels;
    QRect m_anchor;
    bool m_isWatchingApplication = false;
    bool m_isClosing = false;
};

/// Dialog of a new web link (Insert - Web link, a dialog of the design built on
/// PDFMessageBox): the address field (http, https or mailto only, at most
/// PDFAppAnnotationOperations::MAXIMUM_URL_LENGTH characters, no control
/// characters - PDFAppAnnotationOperations::isWebLinkAllowed), the error row and
/// Cancel / Add Link (Add disabled while the address is not allowed). Accepted =
/// QDialog::Accepted with getUrl.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppWebLinkDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Width of the card
    static constexpr int CARD_WIDTH = 440;

    explicit PDFAppWebLinkDialog(QWidget* parent);
    virtual ~PDFAppWebLinkDialog() override;

    /// The address (trimmed)
    QString getUrl() const;

    PDFLineEdit* getField() const;
    QPushButton* getAddButton() const;
    QPushButton* getCancelButton() const;
    PDFLabel* getErrorLabel() const;

private:
    void onTextChanged();
    void onAddClicked();

    QPointer<PDFLineEdit> m_field;
    QPointer<PDFLabel> m_errorLabel;
    QPointer<QPushButton> m_addButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Row of the Target menu of Link to page (1170:28180): an icon tile 30
/// (bg/accent-subtle when chosen, bg/muted otherwise), the name Medium 13 and the
/// description Regular 11 text/muted, the check mark of the chosen target. The
/// row is drawn by the tokens of the design (the designer lists it as "not a
/// component yet", note 1172:28734); hover bg/hover, the focus ring of the
/// keyboard inside, Space / Enter choose it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageLinkTargetItem : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    static constexpr int HEIGHT = 46;

    explicit PDFAppPageLinkTargetItem(DestinationType type, QWidget* parent);
    virtual ~PDFAppPageLinkTargetItem() override;

    DestinationType getType() const { return m_type; }

    /// Description below the name (also the accessible description)
    const QString& getDescription() const { return m_description; }
    void setDescription(const QString& description);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    DestinationType m_type;
    QString m_description;
};

/// Target menu of Link to page (1170:27714 / 1170:28236): a popover 380 wide
/// below the Target select of the bar - the caption "Target view", the five
/// targets and, below the separator, the check box "Keep the reader's zoom"
/// with its hint (enabled only for Position). A click on a target chooses it and
/// closes the menu; the check box keeps it open. Esc and a click outside close it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageLinkTargetMenu : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PANEL_WIDTH = 380;
    static constexpr int RADIUS = 12;

    explicit PDFAppPageLinkTargetMenu(QWidget* parent);
    virtual ~PDFAppPageLinkTargetMenu() override;

    /// Shows the menu with the chosen target below the anchor (4 px gap)
    void popup(QWidget* anchor, DestinationType type, bool keepZoom, PDFReal zoom);

    DestinationType getType() const { return m_type; }
    bool isKeepZoom() const;

    PDFAppPageLinkTargetItem* getItem(DestinationType type) const;
    QCheckBox* getKeepZoomCheckBox() const;
    PDFLabel* getKeepZoomHint() const;

    /// Rectangle of the panel without the margins of the shadow
    QRect getPanelRect() const;

Q_SIGNALS:
    /// A target has been chosen (or the check box changed)
    void targetChosen(pdf::DestinationType type, bool keepZoom);

    /// The menu has been closed (from the event loop)
    void closed();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void updateState();
    void onItemClicked();
    void onKeepZoomToggled(bool checked);
    void onClosedQueued();

    std::map<DestinationType, QPointer<PDFAppPageLinkTargetItem>> m_items;
    QPointer<PDFLabel> m_caption;
    QPointer<QCheckBox> m_keepZoom;
    QPointer<PDFLabel> m_keepZoomHint;
    QPointer<QVBoxLayout> m_layout;
    DestinationType m_type = DestinationType::Fit;
    PDFReal m_zoom = 1.0;
};

/// Target select of the bar of Link to page (Select 78:857 "Target  Whole page"):
/// a select of the style with the prefix, whose popup is the Target menu
/// (PDFAppPageLinkTargetMenu) instead of the list of a combo box
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageLinkTargetSelect : public PDFComboBox
{
    Q_OBJECT

private:
    using BaseClass = PDFComboBox;

public:
    explicit PDFAppPageLinkTargetSelect(QWidget* parent);
    virtual ~PDFAppPageLinkTargetSelect() override;

    DestinationType getType() const;
    void setType(DestinationType type);

    bool isKeepZoom() const { return m_keepZoom; }
    void setKeepZoom(bool keepZoom) { m_keepZoom = keepZoom; }

    /// Zoom shown in the hint of Keep the reader's zoom
    void setZoom(PDFReal zoom) { m_zoom = zoom; }

    /// The menu (created at the first popup)
    PDFAppPageLinkTargetMenu* getMenu() const;

    virtual void showPopup() override;
    virtual void hidePopup() override;

Q_SIGNALS:
    /// The user has chosen a target or changed Keep the reader's zoom
    void targetChosen(pdf::DestinationType type, bool keepZoom);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void onMenuTargetChosen(pdf::DestinationType type, bool keepZoom);
    void onActivated(int index);

    QPointer<PDFAppPageLinkTargetMenu> m_menu;
    bool m_keepZoom = false;
    PDFReal m_zoom = 1.0;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONCREATEDIALOGS_H
