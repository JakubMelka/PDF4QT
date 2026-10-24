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

#ifndef PDFAPPCOMMENTPOPUP_H
#define PDFAPPCOMMENTPOPUP_H

#include "pdfappglobal.h"

#include "pdfobject.h"
#include "pdfthemecolors.h"

#include <QMargins>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QMenu;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace pdf
{

class PDFAppAnnotationController;
class PDFAppDocument;
class PDFAppDocumentView;
class PDFIconButton;
class PDFLabel;
class PDFLineEdit;
class PDFModifiedDocument;
class PDFPlainTextEdit;
class PDFToolBadge;

/// Shadow of the comment popup: a sibling below the popup, transparent for the
/// mouse (the popover shadow 0 / 8 / 24 of effect/shadow)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommentPopupShadow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppCommentPopupShadow(QWidget* parent);
    virtual ~PDFAppCommentPopupShadow() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Icon of the design painted in a color of the theme (the arrow of a reply, the
/// icon of the type in the annotation bar)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommentPopupIcon : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppCommentPopupIcon(const QString& iconName, int extent, PDFThemeColors::Color color, QWidget* parent);
    virtual ~PDFAppCommentPopupIcon() override;

    virtual QSize sizeHint() const override;

    const QString& getIconName() const { return m_iconName; }
    void setIconName(const QString& iconName);

    PDFThemeColors::Color getColor() const { return m_color; }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_iconName;
    int m_extent;
    PDFThemeColors::Color m_color;
};

/// Comment popup of an annotation (Figma 1025:21945, 320 x 312, package B of step 4):
/// head (icon, author, date, more, close), contents, the replies, the reply field
/// and Reply. The popup is a child of the canvas area of the view, it follows the
/// scroll and the zoom of its annotation and closes, when the annotation
/// disappears. Locked document (1030:161012): no reply field. The comment is
/// edited by Edit text of the menu of the popup (LockedContents refuses it).
///
/// Compose mode (package A): a new comment of a sticky note or of Highlight and
/// comment - an empty text field with Post; Post = composeAccepted (A creates the
/// annotation as one undo step), Esc / Cancel / close / a click on the page =
/// composeCanceled (no undo step).
///
/// Every change (a reply, the text) is one undo step through the annotation
/// controller of the view. Ctrl+Enter sends, Esc closes.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommentPopup : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    enum class Mode
    {
        None,       ///< Not shown
        Thread,     ///< Comment thread of an existing annotation
        Compose     ///< A new comment (sticky note, Highlight and comment)
    };

    /// Size of the popup 1025:21945 (the height is the most, which the popup
    /// takes, unless its content needs less; a long thread scrolls)
    static constexpr int POPUP_WIDTH = 320;
    static constexpr int POPUP_HEIGHT = 312;

    /// Padding 12 inside of the border 1, gap 10, radius 12
    static constexpr int PADDING = 13;
    static constexpr int GAP = 10;
    static constexpr int RADIUS = 12;

    /// Gap between the annotation and the popup; smallest distance from the edges
    static constexpr int ANCHOR_GAP = 8;
    static constexpr int PARENT_MARGIN = 8;

    /// Height of the text field of the compose mode and of the text editing
    static constexpr int EDITOR_HEIGHT = 80;

    /// Maximal length of a composed text
    static constexpr qsizetype MAXIMUM_TEXT_LENGTH = 64 * 1024;

    /// Margins of the shadow 0 / 8 / 24 around the card
    static QMargins getShadowMargins();

    explicit PDFAppCommentPopup(PDFAppDocumentView* view, QWidget* parent);
    virtual ~PDFAppCommentPopup() override;

    PDFAppDocumentView* getView() const;
    Mode getMode() const { return m_mode; }

    /// Annotation of the thread (invalid in the compose mode)
    PDFObjectReference getAnnotation() const { return m_annotation; }

    /// Shows the thread of the annotation next to it; \p focusReply puts the focus
    /// into the reply field (annotation.reply). An annotation, which does not
    /// exist, closes the popup.
    void showThread(PDFObjectReference annotation, bool focusReply);

    /// Shows the compose mode next to \p anchor (coordinates of the canvas area)
    /// with the title and the icon of the new annotation
    void showCompose(const QRect& anchor, const QString& title, const QString& iconName);

    /// Text of the compose mode
    const QString& getComposedText() const { return m_composedText; }
    void setComposedText(const QString& text);

    /// Post of the compose mode (composeAccepted with the text, the popup closes)
    void accept();

    /// Esc / close: the compose mode is canceled (composeCanceled), the thread
    /// and the editing of its text close (nothing is committed)
    void cancel();

    /// Text of the reply field; Reply (Ctrl+Enter) adds it as a reply - one undo
    /// step. \returns false, when the reply cannot be added (empty, locked).
    QString getReplyText() const;
    void setReplyText(const QString& text);
    bool sendReply();

    /// Editing of the text of the annotation (Edit text of the menu): the editor
    /// replaces the text, Save (Ctrl+Enter) commits it as one undo step, Esc
    /// returns to the thread. Refused for LockedContents and a locked document.
    bool canEditContents() const;
    bool isEditingContents() const { return m_isEditingContents; }
    bool beginEditContents();
    void setEditedContents(const QString& text);
    bool commitContents();
    void cancelEditContents();

    /// Number of the replies shown
    int getReplyCount() const { return m_replyCount; }

    /// The reply field is shown (a thread of a document, which can be changed)
    bool isReplyFieldShown() const;

    /// Rectangle of the card in the coordinates of the parent (the canvas area)
    QRect getCardGeometry() const;

    /// Rectangle of the card in the coordinates of the popup
    QRect getCardRect() const;

    /// Anchor of the popup in the coordinates of the parent (the rectangle of the
    /// annotation on the screen, or the anchor of the compose mode); empty, when
    /// the annotation is not on a shown page
    QRect getAnchor() const { return m_anchor; }

    /// Parts of the popup (tests, accessibility)
    PDFToolBadge* getBadge() const;
    PDFLabel* getTitleLabel() const;
    PDFLabel* getInfoLabel() const;
    PDFLabel* getContentsLabel() const;
    PDFPlainTextEdit* getTextEdit() const;
    PDFLineEdit* getReplyField() const;
    QPushButton* getPrimaryButton() const;
    QPushButton* getCancelButton() const;
    PDFIconButton* getMoreButton() const;
    PDFIconButton* getCloseButton() const;
    QMenu* getMoreMenu() const;

    /// Opens the menu of the popup (more 1025:21954): Edit text, Properties…,
    /// Delete (only, what the annotation and the document allow)
    void showMoreMenu();

    /// Places the popup again (the annotation moved, the view scrolled or zoomed)
    void updatePlacement();

    virtual QSize sizeHint() const override;

Q_SIGNALS:
    /// The new comment has been posted
    void composeAccepted(const QString& text);

    /// The new comment has been canceled (no undo step)
    void composeCanceled();

    /// A reply has been added (one undo step)
    void replyAdded(pdf::PDFObjectReference reply);

    /// The popup has been closed (any mode); sent from the event loop
    void closed();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onClosedQueued();
    void onCloseClicked();
    void onMoreClicked();
    void onPrimaryClicked();
    void onCancelClicked();
    void onReplyTextChanged();
    void onEditorTextChanged();
    void onDocumentChanged();
    void onViewChanged();
    void onEditingAllowedChanged();
    void onEditTextTriggered();
    void onPropertiesTriggered();
    void onDeleteTriggered();
    void onThemeChanged();

    void createWidgets();
    void hidePopup();
    void finishHiding();
    void updateContents();
    void updateReplies();
    void updateButtons();
    void updateShadow();
    void updateTexts();
    void watchCanvas(bool watch);
    void connectDocument();
    void focusInput(bool focusReply);
    QRect computeAnchor() const;
    PDFAppAnnotationController* getController() const;
    PDFAppDocument* getDocument() const;
    bool handleKey(QKeyEvent* event);

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    Mode m_mode = Mode::None;
    PDFObjectReference m_annotation;
    QRect m_anchor;
    QString m_title;
    QString m_iconName;
    QString m_composedText;
    bool m_isEditingContents = false;
    bool m_isClosePending = false;
    bool m_isAnchorTracked = false;     ///< The annotation has been placed on the screen (it is followed, hidden when scrolled out)
    int m_replyCount = 0;

    QPointer<PDFAppCommentPopupShadow> m_shadow;
    QPointer<QWidget> m_watchedParent;
    QPointer<QWidget> m_watchedCanvas;
    QPointer<PDFToolBadge> m_badge;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<PDFLabel> m_infoLabel;
    QPointer<PDFIconButton> m_moreButton;
    QPointer<PDFIconButton> m_closeButton;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_threadWidget;
    QPointer<PDFLabel> m_contentsLabel;
    QPointer<QWidget> m_divider;
    QPointer<PDFLabel> m_repliesCaption;
    QPointer<QWidget> m_repliesWidget;
    QPointer<QVBoxLayout> m_repliesLayout;
    QPointer<PDFPlainTextEdit> m_textEdit;
    QPointer<PDFLineEdit> m_replyField;
    QPointer<PDFLabel> m_hintLabel;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_primaryButton;
    QPointer<QMenu> m_moreMenu;
    QPointer<QVBoxLayout> m_layout;
};

}   // namespace pdf

#endif // PDFAPPCOMMENTPOPUP_H
