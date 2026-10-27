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

#ifndef PDFCOMPONENTASSISTANTS_H
#define PDFCOMPONENTASSISTANTS_H

#include "pdffluentglobal.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentrows.h"
#include "pdfwatchedpointer.h"

#include <QLineEdit>     // QPointer<QLineEdit> of PDFPropertyRow (pdfcomponentrows.h) needs the class
#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QToolButton;

namespace pdf
{

class PDFPlainTextEdit;

/// Avatar of an AI assistant (the page AI, the confirmations of the requests of
/// the assistants): the monogram in a circle of the hue, the icon mcp of an
/// unknown client, the CLI badge of a command line client. Not interactive (the
/// mouse passes through it); the application gives it the accessible name of
/// the assistant. The size includes the CLI badge reaching out of the circle.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAssistantAvatar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAssistantAvatar(QWidget* parent = nullptr);
    explicit PDFAssistantAvatar(const PDFComponentPainter::AssistantAvatar& avatar, QWidget* parent = nullptr);

    /// Sets the whole avatar (an invalid hue is Neutral, a negative extent 0)
    void setAvatar(const PDFComponentPainter::AssistantAvatar& avatar);
    const PDFComponentPainter::AssistantAvatar& getAvatar() const { return m_avatar; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    PDFComponentPainter::AssistantAvatar m_avatar;
};

/// Connected assistant of the page AI (client row 653:5591, remote 653:6767):
/// the avatar, the name (data, elided) with an optional tag ("Remote"), the
/// meta text (data, elided) and the disconnect button (a nested Icon button 28,
/// icon unplug) at the trailing side. The row itself is not interactive: it
/// takes no focus, has no hover (Figma has none), the nested button is the stop
/// of Tab. Its accessible name is the name, the description the meta text and
/// the tag. The disconnect button may be deleted by the application: the row
/// draws and behaves without it (its place is freed, as by \p setDisconnectVisible).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAssistantRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAssistantRow(QWidget* parent = nullptr);
    explicit PDFAssistantRow(const PDFComponentPainter::AssistantRow& row, QWidget* parent = nullptr);
    virtual ~PDFAssistantRow() override;

    /// Sets the whole row (its \p paintNested is ignored, the row has the real button)
    void setRow(const PDFComponentPainter::AssistantRow& row);
    const PDFComponentPainter::AssistantRow& getRow() const { return m_row; }

    /// The nested disconnect button (nullptr, when the application has deleted it)
    PDFIconButton* getDisconnectButton() const { return m_disconnectButton.data(); }

    /// Tooltip and the accessible name of the disconnect button (tr("Disconnect") by default)
    void setDisconnectToolTip(const QString& toolTip);
    QString getDisconnectToolTip() const;

    /// The disconnect button is shown (its place is freed, when it is hidden)
    void setDisconnectVisible(bool isVisible);
    bool isDisconnectVisible() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The disconnect button has been clicked (a slot may delete the row)
    void disconnectClicked();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onDisconnectButtonClicked();

    /// Returns the disconnect button, when it is a child of the row (not deleted, not moved away)
    PDFIconButton* getOwnButton() const;
    void layoutButton();
    void updateAccessibility();
    void updateButtonTexts();

    /// Returns the content drawn by the row: the place of the button follows the button
    PDFComponentPainter::AssistantRow getPaintedRow() const;
    PDFComponentPainter::State getState() const;

    PDFComponentPainter::AssistantRow m_row;
    PDFWatchedPointer<PDFIconButton> m_disconnectButton;  ///< Recognized by the event filter also in its destructor
    QString m_disconnectToolTip;
    bool m_isDisconnectToolTipSet = false;
    QString m_name;
    QString m_description;
};

/// Item of the recent activity of the page AI (activity rows 955:147547 …): the
/// status circle of the hue, the title with the time, the meta text and the
/// status, which wraps, with an optional inline link text ("Show"). Only a
/// clickable row (\p setClickable, off by default; Figma: the waiting row, an
/// <a>) is a link: hover, press, the focus ring inside, Enter and Space, the
/// signal clicked. A row, which is not clickable, takes no focus, passes the
/// mouse to the widget below it and is announced as a text. The height follows
/// the width (the status wraps).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFActivityRow : public PDFRowButton
{
    Q_OBJECT
    Q_PROPERTY(bool clickable READ isClickable WRITE setClickable)

private:
    using BaseClass = PDFRowButton;

public:
    explicit PDFActivityRow(QWidget* parent = nullptr);
    explicit PDFActivityRow(const PDFComponentPainter::ActivityRow& row, QWidget* parent = nullptr);

    /// Sets the whole row (the title is the text of the button)
    void setRow(const PDFComponentPainter::ActivityRow& row);
    PDFComponentPainter::ActivityRow getRow() const;

    /// A clickable row is a link, which takes the focus; a row, which stops
    /// being clickable, gives its focus away and is a text
    bool isClickable() const { return m_isClickable; }
    void setClickable(bool isClickable);

    virtual bool isInteractive() const override { return m_isClickable; }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual int computeHeightForWidth(int width) const override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateDescription();

    PDFComponentPainter::ActivityRow m_row;
    QString m_description;
    bool m_isClickable = false;
};

/// Assistant of "Connect an assistant" of the page AI (assistant tiles 653:5691
/// …): the logo, the name and the vendor. It is a static choice of an exclusive
/// group given by the application (SelectionBehavior::ExclusiveStatic: the
/// selected tile does not react to the pointer; one stop of Tab, the arrows move
/// the selection as by the other choice cards). The focus ring lies outside
/// (radius 12). 66 high in each state, the width follows the layout.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAssistantTile : public PDFCardButton
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY toggled)

private:
    using BaseClass = PDFCardButton;

public:
    explicit PDFAssistantTile(QWidget* parent = nullptr);
    explicit PDFAssistantTile(const PDFComponentPainter::AssistantTile& tile, QWidget* parent = nullptr);

    /// Sets the whole tile (the name is the text of the button)
    void setTile(const PDFComponentPainter::AssistantTile& tile);
    PDFComponentPainter::AssistantTile getTile() const;

    bool isSelected() const { return isChecked(); }
    void setSelected(bool isSelected);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual QSize getContentMinimumSizeHint() const override;
    virtual void updateDescription() override;

private:
    PDFComponentPainter::AssistantTile m_tile;
    QString m_description;
};

/// Code box of the setup card of the page AI (653:5754 a command, 653:5781 a
/// prompt in Inter, 955:149431 a configuration of more lines): the text, which is
/// data - plain text, never rich, selectable by the mouse and by the keyboard,
/// wrapped at any character inside of the box (no horizontal scroll) - and the
/// copy Toolbar button (a QToolButton with autoRaise, icon copy 20, 36 x 36 - the
/// toolbar button of the library). The copy puts \p getCopyText on the clipboard
/// (the page shows a shortened token and copies the whole command) and emits
/// \p copied. The height follows the width. The accessible name of the box is
/// given to its text. The text and the button may be deleted by the
/// application: the box draws its text itself then, the place of the button stays.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFCodeBox : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFCodeBox(QWidget* parent = nullptr);
    explicit PDFCodeBox(const QString& text, QWidget* parent = nullptr);
    virtual ~PDFCodeBox() override;

    /// Shown text (plain text)
    void setText(const QString& text);
    QString getText() const { return m_box.text; }

    /// Text put on the clipboard by the copy (empty: the shown text)
    void setCopyText(const QString& text);
    QString getCopyText() const;

    /// JetBrains Mono (true, the default) or Inter (the prompt of step 3)
    void setMonospace(bool isMonospace);
    bool isMonospace() const { return m_box.monospace; }

    /// The copy button (nullptr, when the application has deleted it)
    QAbstractButton* getCopyButton() const;

    /// Tooltip and the accessible name of the copy button (tr("Copy") by default)
    void setCopyToolTip(const QString& toolTip);
    QString getCopyToolTip() const;

    /// Copies the copy text to the clipboard and emits \p copied (the copy button)
    void copy();

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

Q_SIGNALS:
    /// The copy text has been put on the clipboard (a slot may delete the box)
    void copied();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onCopyButtonClicked();

    /// Keeps the scroll bars of the text without a range: the box shows all its
    /// lines, a line wrapped to the width never scrolls (QPlainTextEdit counts the
    /// width of its cursor into the width of the document)
    void onEditorScrollRangeChanged();
    void layoutChildren();
    void updateEditorText();
    void updateEditorFont();
    void updateButtonTexts();
    void forwardAccessibleName();

    /// Returns the text editor / the copy button, when it is a child of the box (not deleted, not moved away)
    PDFPlainTextEdit* getOwnEditor() const;
    QToolButton* getOwnButton() const;

    PDFComponentPainter::CodeBox getPaintedBox() const;
    PDFComponentPainter::State getState() const;

    PDFComponentPainter::CodeBox m_box;
    QString m_copyText;
    QString m_copyToolTip;
    bool m_isCopyToolTipSet = false;
    QString m_forwardedName;
    PDFWatchedPointer<PDFPlainTextEdit> m_editor;  ///< Recognized by the event filter also in its destructor
    QPointer<QToolButton> m_copyButton;

    /// Height of the last width (a layout asks for the same width repeatedly)
    mutable int m_cachedWidth = -1;
    mutable int m_cachedHeight = -1;
};

}   // namespace pdf

#endif // PDFCOMPONENTASSISTANTS_H
