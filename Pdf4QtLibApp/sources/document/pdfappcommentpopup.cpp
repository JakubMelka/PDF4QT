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

#include "pdfappcommentpopup.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationoperations.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapppage.h"

#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfwidgetsnapshot.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace pdf
{

/// Layout constants and small helpers of the comment popup
class PDFAppCommentPopupHelper
{
public:
    PDFAppCommentPopupHelper() = delete;

    /// head 1025:21946: gap 10; who: gap 1
    static constexpr int HEAD_GAP = 10;

    /// reply 1025:21964: icon 14, gap 8; text gap 2; head gap 6
    static constexpr int REPLY_ICON = 14;
    static constexpr int REPLY_GAP = 8;
    static constexpr int REPLY_TEXT_GAP = 2;
    static constexpr int REPLY_HEAD_GAP = 6;

    /// Line heights of the contents (18) and of the text of a reply (16)
    static constexpr int CONTENTS_LINE_HEIGHT = 18;
    static constexpr int REPLY_LINE_HEIGHT = 16;

    /// Smallest height of the scrolled thread, when the canvas is low
    static constexpr int MINIMUM_THREAD_HEIGHT = 40;

    /// Ctrl+Enter / Ctrl+Return
    static bool isSendKey(const QKeyEvent* event)
    {
        return (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers().testFlag(Qt::ControlModifier);
    }

    /// Label of a text of the document (a plain text, never a rich text)
    static PDFLabel* createDataLabel(PDFThemeFonts::Role role, PDFThemeColors::Color color, int lineHeight, QWidget* parent)
    {
        PDFLabel* label = new PDFLabel(QString(), role, color, parent);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setLineHeight(lineHeight);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        return label;
    }
};

PDFAppCommentPopupShadow::PDFAppCommentPopupShadow(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("commentPopupShadow"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppCommentPopupShadow::update));
}

PDFAppCommentPopupShadow::~PDFAppCommentPopupShadow() = default;

void PDFAppCommentPopupShadow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Popover shadow 0 / 8 / 24 of the card (the popup above draws the card)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card(rect().marginsRemoved(PDFAppCommentPopup::getShadowMargins()));
    PDFStylePainter::shadow(&painter, card, PDFStylePainter::Radii(PDFAppCommentPopup::RADIUS), QPointF(0.0, 8.0), 24.0,
                            PDFThemeManager::getInstance()->getColor(PDFThemeColors::EffectShadow));
}

PDFAppCommentPopupIcon::PDFAppCommentPopupIcon(const QString& iconName, int extent, PDFThemeColors::Color color, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName),
    m_extent(qBound(1, extent, 64)),
    m_color(color)
{
    setFixedSize(m_extent, m_extent);
    setFocusPolicy(Qt::NoFocus);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppCommentPopupIcon::update));
}

PDFAppCommentPopupIcon::~PDFAppCommentPopupIcon() = default;

QSize PDFAppCommentPopupIcon::sizeHint() const
{
    return QSize(m_extent, m_extent);
}

void PDFAppCommentPopupIcon::setIconName(const QString& iconName)
{
    if (m_iconName != iconName)
    {
        m_iconName = iconName;
        update();
    }
}

void PDFAppCommentPopupIcon::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::icon(&painter, QRectF(rect()), m_iconName, PDFThemeManager::getInstance()->getColor(isEnabled() ? m_color : PDFThemeColors::IconDisabled));
}

QMargins PDFAppCommentPopup::getShadowMargins()
{
    // Blur 24, offset 8 down
    return QMargins(24, 16, 24, 32);
}

PDFAppCommentPopup::PDFAppCommentPopup(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("commentPopup"));
    setAttribute(Qt::WA_NoMousePropagation, true);
    setFocusPolicy(Qt::ClickFocus);
    hide();

    createWidgets();

    // The shadow is a sibling below the popup; the popup takes the pointer only over its card
    if (parent)
    {
        m_shadow = new PDFAppCommentPopupShadow(parent);
        m_shadow->hide();
        m_watchedParent = parent;
        parent->installEventFilter(this);
    }

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppCommentPopup::onThemeChanged);
    if (view)
    {
        if (PDFDrawWidgetProxy* proxy = view->getProxy())
        {
            connect(proxy, &PDFDrawWidgetProxy::drawSpaceChanged, this, &PDFAppCommentPopup::onViewChanged);
            connect(proxy, &PDFDrawWidgetProxy::pageLayoutChanged, this, &PDFAppCommentPopup::onViewChanged);
        }
        if (PDFAppAnnotationController* controller = view->getAnnotationController())
        {
            connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppCommentPopup::onEditingAllowedChanged);
        }
    }

    updateTexts();
}

PDFAppCommentPopup::~PDFAppCommentPopup()
{
    watchCanvas(false);
    if (m_watchedParent)
    {
        m_watchedParent->removeEventFilter(this);
    }

    // The shadow goes with the popup (the parent may be deleting its children)
    delete m_shadow.data();
    delete m_moreMenu.data();
}

void PDFAppCommentPopup::createWidgets()
{
    using H = PDFAppCommentPopupHelper;

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(getShadowMargins() + QMargins(PADDING, PADDING, PADDING, PADDING));
    m_layout->setSpacing(GAP);

    // head 1025:21946: Tool icon 28, who (author Semi Bold 13 / date Regular 11), more 24, close 20
    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(H::HEAD_GAP);
    m_badge = new PDFToolBadge(QStringLiteral("message-square"), PDFAccentHue::Blue, PDFToolBadge::BadgeSize::Small, this);
    headLayout->addWidget(m_badge);

    QVBoxLayout* whoLayout = new QVBoxLayout();
    whoLayout->setContentsMargins(0, 0, 0, 0);
    whoLayout->setSpacing(1);
    m_titleLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_infoLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_infoLabel->setTextFormat(Qt::PlainText);
    m_infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    whoLayout->addWidget(m_titleLabel);
    whoLayout->addWidget(m_infoLabel);
    headLayout->addLayout(whoLayout, 1);

    m_moreButton = new PDFIconButton(QStringLiteral("more-horizontal"), PDFIconButton::Size::Small, this);
    m_moreButton->setObjectName(QStringLiteral("commentPopupMore"));
    connect(m_moreButton.data(), &PDFIconButton::clicked, this, &PDFAppCommentPopup::onMoreClicked);
    headLayout->addWidget(m_moreButton);

    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
    m_closeButton->setObjectName(QStringLiteral("commentPopupClose"));
    connect(m_closeButton.data(), &PDFIconButton::clicked, this, &PDFAppCommentPopup::onCloseClicked);
    headLayout->addWidget(m_closeButton);
    m_layout->addLayout(headLayout);

    // Text field of the compose mode and of the editing of the text
    m_textEdit = new PDFPlainTextEdit(this);
    m_textEdit->setObjectName(QStringLiteral("commentPopupText"));
    m_textEdit->setFixedHeight(EDITOR_HEIGHT);
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->installEventFilter(this);
    m_textEdit->hide();
    connect(m_textEdit.data(), &PDFPlainTextEdit::textChanged, this, &PDFAppCommentPopup::onEditorTextChanged);
    m_layout->addWidget(m_textEdit);

    // Thread: contents, divider, "N replies", the replies (scrolled, when long)
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("commentPopupThread"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFocusPolicy(Qt::NoFocus);
    PDFWidgetStyleProperties::set(m_scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    m_scrollArea->viewport()->setAutoFillBackground(false);

    m_threadWidget = new QWidget();
    m_threadWidget->setAutoFillBackground(false);
    QVBoxLayout* threadLayout = new QVBoxLayout(m_threadWidget);
    threadLayout->setContentsMargins(0, 0, 0, 0);
    threadLayout->setSpacing(GAP);

    m_contentsLabel = H::createDataLabel(PDFThemeFonts::Role::Body, PDFThemeColors::TextPrimary, H::CONTENTS_LINE_HEIGHT, m_threadWidget);
    m_contentsLabel->setObjectName(QStringLiteral("commentPopupContents"));
    threadLayout->addWidget(m_contentsLabel);

    m_divider = new QWidget(m_threadWidget);
    m_divider->setFixedHeight(1);
    m_divider->installEventFilter(this);
    threadLayout->addWidget(m_divider);

    m_repliesCaption = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextTertiary, m_threadWidget);
    m_repliesCaption->setTextFormat(Qt::PlainText);
    threadLayout->addWidget(m_repliesCaption);

    m_repliesWidget = new QWidget(m_threadWidget);
    m_repliesLayout = new QVBoxLayout(m_repliesWidget);
    m_repliesLayout->setContentsMargins(0, 0, 0, 0);
    m_repliesLayout->setSpacing(GAP);
    threadLayout->addWidget(m_repliesWidget);
    threadLayout->addStretch(1);

    m_scrollArea->setWidget(m_threadWidget);
    m_threadWidget->setAutoFillBackground(false);
    m_layout->addWidget(m_scrollArea);

    // reply-field 1025:21980 (Text field 34)
    m_replyField = new PDFLineEdit(this);
    m_replyField->setObjectName(QStringLiteral("commentPopupReply"));
    m_replyField->setMaxLength(int(MAXIMUM_TEXT_LENGTH));
    m_replyField->installEventFilter(this);
    connect(m_replyField.data(), &PDFLineEdit::textChanged, this, &PDFAppCommentPopup::onReplyTextChanged);
    connect(m_replyField.data(), &PDFLineEdit::returnPressed, this, &PDFAppCommentPopup::onPrimaryClicked);
    m_layout->addWidget(m_replyField);

    // actions 1025:21982: hint Regular 11 muted, Reply (Primary, disabled while empty)
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    m_hintLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_hintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    actionsLayout->addWidget(m_hintLabel, 1);

    m_cancelButton = new QPushButton(this);
    m_cancelButton->setObjectName(QStringLiteral("commentPopupCancel"));
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppCommentPopup::onCancelClicked);
    actionsLayout->addWidget(m_cancelButton);

    m_primaryButton = new QPushButton(this);
    m_primaryButton->setObjectName(QStringLiteral("commentPopupPrimary"));
    m_primaryButton->setAutoDefault(false);
    PDFWidgetStyleProperties::set(m_primaryButton, PDFWidgetStyleProperties::PRIMARY);
    connect(m_primaryButton.data(), &QPushButton::clicked, this, &PDFAppCommentPopup::onPrimaryClicked);
    actionsLayout->addWidget(m_primaryButton);
    m_layout->addLayout(actionsLayout);

    setFixedWidth(POPUP_WIDTH + getShadowMargins().left() + getShadowMargins().right());
}

void PDFAppCommentPopup::updateTexts()
{
    setAccessibleName(tr("Comment"));
    m_moreButton->setToolTip(tr("More"));
    m_moreButton->setAccessibleName(tr("More"));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setAccessibleName(tr("Close"));
    m_replyField->setPlaceholderText(tr("Write a reply…"));
    m_replyField->setAccessibleName(tr("Reply"));
    m_textEdit->setAccessibleName(m_mode == Mode::Compose ? tr("Comment") : tr("Text"));
    m_textEdit->setPlaceholderText(tr("Write a comment…"));
    m_cancelButton->setText(tr("Cancel"));

    if (m_mode == Mode::Compose)
    {
        m_primaryButton->setText(tr("Post"));
        m_hintLabel->setText(tr("Ctrl+Enter posts · Esc cancels"));
    }
    else if (m_isEditingContents)
    {
        m_primaryButton->setText(tr("Save"));
        m_hintLabel->setText(tr("Ctrl+Enter saves · Esc cancels"));
    }
    else
    {
        m_primaryButton->setText(tr("Reply"));
        m_hintLabel->setText(isReplyFieldShown() ? tr("Ctrl+Enter sends · Esc closes") : tr("Esc closes"));
    }
}

PDFAppDocumentView* PDFAppCommentPopup::getView() const
{
    return m_view.data();
}

PDFAppAnnotationController* PDFAppCommentPopup::getController() const
{
    return m_view ? m_view->getAnnotationController() : nullptr;
}

PDFAppDocument* PDFAppCommentPopup::getDocument() const
{
    return m_view ? m_view->getDocument() : nullptr;
}

PDFToolBadge* PDFAppCommentPopup::getBadge() const { return m_badge.data(); }
PDFLabel* PDFAppCommentPopup::getTitleLabel() const { return m_titleLabel.data(); }
PDFLabel* PDFAppCommentPopup::getInfoLabel() const { return m_infoLabel.data(); }
PDFLabel* PDFAppCommentPopup::getContentsLabel() const { return m_contentsLabel.data(); }
PDFPlainTextEdit* PDFAppCommentPopup::getTextEdit() const { return m_textEdit.data(); }
PDFLineEdit* PDFAppCommentPopup::getReplyField() const { return m_replyField.data(); }
QPushButton* PDFAppCommentPopup::getPrimaryButton() const { return m_primaryButton.data(); }
QPushButton* PDFAppCommentPopup::getCancelButton() const { return m_cancelButton.data(); }
PDFIconButton* PDFAppCommentPopup::getMoreButton() const { return m_moreButton.data(); }
PDFIconButton* PDFAppCommentPopup::getCloseButton() const { return m_closeButton.data(); }
QMenu* PDFAppCommentPopup::getMoreMenu() const { return m_moreMenu.data(); }

void PDFAppCommentPopup::showThread(PDFObjectReference annotation, bool focusReply)
{
    const PDFAppDocument* document = getDocument();
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
    QPointer<PDFAppCommentPopup> guard(this);

    // A new comment being written is canceled first (no undo step)
    if (m_mode == Mode::Compose)
    {
        cancel();
        if (!guard)
        {
            return;
        }
    }

    if (!info.isValid())
    {
        if (m_mode != Mode::None)
        {
            cancel();
        }
        return;
    }

    if (m_annotation != annotation && m_replyField)
    {
        m_replyField->clear();
    }

    connectDocument();
    if (m_annotation != annotation)
    {
        m_isAnchorTracked = false;
    }
    m_mode = Mode::Thread;
    m_annotation = annotation;
    m_isEditingContents = false;
    m_isClosePending = false;
    m_composedText.clear();

    updateContents();
    updateReplies();
    updateButtons();
    show();
    raise();
    watchCanvas(true);
    updatePlacement();
    focusInput(focusReply);
}

void PDFAppCommentPopup::showCompose(const QRect& anchor, const QString& title, const QString& iconName)
{
    QPointer<PDFAppCommentPopup> guard(this);
    if (m_mode != Mode::None)
    {
        cancel();
        if (!guard)
        {
            return;
        }
    }

    m_mode = Mode::Compose;
    m_annotation = PDFObjectReference();
    m_anchor = anchor.normalized();
    m_title = title.left(256);
    m_iconName = iconName.left(64);
    m_composedText.clear();
    m_isEditingContents = false;
    m_isClosePending = false;

    const QSignalBlocker blocker(m_textEdit);
    m_textEdit->clear();

    updateContents();
    updateReplies();
    updateButtons();
    show();
    raise();
    watchCanvas(true);
    updatePlacement();
    focusInput(false);
}

void PDFAppCommentPopup::setComposedText(const QString& text)
{
    m_composedText = text.left(MAXIMUM_TEXT_LENGTH);
    if (m_mode == Mode::Compose && m_textEdit && m_textEdit->toPlainText() != m_composedText)
    {
        const QSignalBlocker blocker(m_textEdit);
        m_textEdit->setPlainText(m_composedText);
        updateButtons();
    }
}

void PDFAppCommentPopup::accept()
{
    if (m_mode != Mode::Compose)
    {
        return;
    }

    const QString text = m_composedText;
    hidePopup();
    Q_EMIT composeAccepted(text);
}

void PDFAppCommentPopup::cancel()
{
    if (m_mode == Mode::None)
    {
        return;
    }

    const bool isCompose = m_mode == Mode::Compose;
    hidePopup();
    if (isCompose)
    {
        Q_EMIT composeCanceled();
    }
}

void PDFAppCommentPopup::hidePopup()
{
    m_mode = Mode::None;
    m_isAnchorTracked = false;
    m_isEditingContents = false;
    m_annotation = PDFObjectReference();
    watchCanvas(false);
    if (m_moreMenu)
    {
        m_moreMenu->hide();
    }

    // The focus goes back to the canvas (the keyboard of the page continues)
    const bool hadFocus = isAncestorOf(QApplication::focusWidget()) || hasFocus();
    hide();
    if (hadFocus && m_view && m_view->getPdfWidget())
    {
        m_view->getPdfWidget()->setFocus(Qt::OtherFocusReason);
    }

    if (!m_isClosePending)
    {
        m_isClosePending = true;
        QMetaObject::invokeMethod(this, &PDFAppCommentPopup::onClosedQueued, Qt::QueuedConnection);
    }
}

void PDFAppCommentPopup::onClosedQueued()
{
    m_isClosePending = false;
    if (m_mode == Mode::None)
    {
        Q_EMIT closed();
    }
}

QString PDFAppCommentPopup::getReplyText() const
{
    return m_replyField ? m_replyField->text() : QString();
}

void PDFAppCommentPopup::setReplyText(const QString& text)
{
    if (m_replyField)
    {
        m_replyField->setText(text.left(MAXIMUM_TEXT_LENGTH));
    }
}

bool PDFAppCommentPopup::isReplyFieldShown() const
{
    const PDFAppAnnotationController* controller = getController();
    const PDFAppDocument* document = getDocument();
    if (m_mode != Mode::Thread || m_isEditingContents || !controller || !document || !controller->isAnnotationEditingAllowed())
    {
        return false;
    }

    // A read-only annotation takes no reply
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document->getDocument().data(), m_annotation);
    return info.isValid() && !info.flags.testFlag(PDFAnnotation::ReadOnly);
}

bool PDFAppCommentPopup::sendReply()
{
    PDFAppAnnotationController* controller = getController();
    PDFAppDocument* document = getDocument();
    const QString text = getReplyText();
    if (!isReplyFieldShown() || !controller || !document || text.trimmed().isEmpty())
    {
        return false;
    }

    PDFObjectReference reply;
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::addReply(document->getDocument(), m_annotation, text, PDFAuthorSettings::getAuthorName(), &reply);
    if (!result)
    {
        return false;
    }

    QPointer<PDFAppCommentPopup> guard(this);
    const bool isCommitted = controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Reply to annotation"));
    if (!guard || !isCommitted)
    {
        return false;
    }

    m_replyField->clear();
    Q_EMIT replyAdded(reply);
    return true;
}

bool PDFAppCommentPopup::canEditContents() const
{
    const PDFAppAnnotationController* controller = getController();
    const PDFAppDocument* document = getDocument();
    return m_mode == Mode::Thread && controller && document && controller->isAnnotationEditingAllowed() &&
           PDFAppAnnotationOperations::canModifyContents(document->getDocument(), m_annotation);
}

bool PDFAppCommentPopup::beginEditContents()
{
    if (!canEditContents())
    {
        return false;
    }

    const PDFAppDocument* document = getDocument();
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document->getDocument().data(), m_annotation);
    m_isEditingContents = true;
    {
        const QSignalBlocker blocker(m_textEdit);
        m_textEdit->setPlainText(info.contents);
    }
    updateButtons();
    updatePlacement();
    focusInput(false);
    return true;
}

void PDFAppCommentPopup::setEditedContents(const QString& text)
{
    if (m_isEditingContents && m_textEdit)
    {
        m_textEdit->setPlainText(text.left(MAXIMUM_TEXT_LENGTH));
    }
}

bool PDFAppCommentPopup::commitContents()
{
    PDFAppAnnotationController* controller = getController();
    PDFAppDocument* document = getDocument();
    if (!m_isEditingContents || !controller || !document || !canEditContents())
    {
        return false;
    }

    const QString text = m_textEdit->toPlainText().left(MAXIMUM_TEXT_LENGTH);
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::setContents(document->getDocument(), m_annotation, text);

    // The editor closes first: the commit changes the document, the popup shows its new text
    m_isEditingContents = false;
    updateButtons();

    bool isCommitted = true;
    if (result)
    {
        QPointer<PDFAppCommentPopup> guard(this);
        isCommitted = controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Edit comment"));
        if (!guard)
        {
            return isCommitted;
        }
    }

    updateContents();
    updatePlacement();
    focusInput(false);
    return isCommitted;
}

void PDFAppCommentPopup::cancelEditContents()
{
    if (!m_isEditingContents)
    {
        return;
    }

    m_isEditingContents = false;
    updateButtons();
    updatePlacement();
    focusInput(false);
}

void PDFAppCommentPopup::updateContents()
{
    const PDFAppDocument* document = getDocument();
    const PDFDocument* pdfDocument = document ? document->getDocument().data() : nullptr;

    QString iconName = m_iconName.isEmpty() ? QStringLiteral("message-square") : m_iconName;
    PDFAccentHue hue = PDFAccentHue::Blue;
    if (m_mode == Mode::Thread)
    {
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument, m_annotation);
        PDFAppAnnotationInfo::getBadge(info.type, false, &iconName, &hue);

        // who 1025:21951: the author; "Today 14:05 · Rectangle"
        m_titleLabel->setText(PDFAppAnnotationInfo::getAuthorText(info.author));
        QStringList parts;
        const QString date = PDFAppAnnotationInfo::formatDateTime(info.getDate());
        if (!date.isEmpty())
        {
            parts << date;
        }
        parts << PDFAppAnnotationInfo::getDisplayName(info);
        m_infoLabel->setText(parts.join(QStringLiteral(" · ")));
        m_contentsLabel->setText(info.contents);
        m_contentsLabel->setVisible(!info.contents.isEmpty());
    }
    else
    {
        m_titleLabel->setText(m_title.isEmpty() ? tr("New comment") : m_title);
        m_infoLabel->setText(PDFAppAnnotationInfo::getAuthorText(PDFAuthorSettings::getAuthorName()));
        m_contentsLabel->clear();
    }

    m_badge->setIconName(iconName);
    m_badge->setHue(hue);
    m_titleLabel->setToolTip(m_titleLabel->text());
    m_infoLabel->setToolTip(m_infoLabel->text());
}

void PDFAppCommentPopup::updateReplies()
{
    using H = PDFAppCommentPopupHelper;

    // The rows are created again (a reply has no state of its own)
    const QList<QWidget*> rows = m_repliesWidget->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* row : rows)
    {
        delete row;
    }

    const PDFAppDocument* document = getDocument();
    const std::vector<PDFAppAnnotationInfo> replies = m_mode == Mode::Thread && document ?
                PDFAppAnnotationInfo::readReplies(document->getDocument().data(), m_annotation) : std::vector<PDFAppAnnotationInfo>();
    m_replyCount = int(replies.size());

    for (const PDFAppAnnotationInfo& reply : replies)
    {
        // reply 1025:21964: corner-down-right 14, gap 8; author Semi Bold 12, date Regular 11; text Regular 12 / 16
        QWidget* row = new QWidget(m_repliesWidget);
        row->setObjectName(QStringLiteral("commentPopupReplyRow"));
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(H::REPLY_GAP);
        rowLayout->addWidget(new PDFAppCommentPopupIcon(PDFAppPage::getDirectionalIconName(QStringLiteral("corner-down-right"), layoutDirection()), H::REPLY_ICON,
                                                        PDFThemeColors::IconMuted, row),
                             0, Qt::AlignTop);

        QVBoxLayout* textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(H::REPLY_TEXT_GAP);
        QHBoxLayout* headLayout = new QHBoxLayout();
        headLayout->setContentsMargins(0, 0, 0, 0);
        headLayout->setSpacing(H::REPLY_HEAD_GAP);
        PDFLabel* author = new PDFLabel(PDFAppAnnotationInfo::getAuthorText(reply.author), PDFThemeFonts::Role::SmallSemiBold, PDFThemeColors::TextPrimary, row);
        author->setTextFormat(Qt::PlainText);
        PDFLabel* date = new PDFLabel(PDFAppAnnotationInfo::formatDateTime(reply.getDate()), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, row);
        date->setTextFormat(Qt::PlainText);
        headLayout->addWidget(author);
        headLayout->addWidget(date);
        headLayout->addStretch(1);
        textLayout->addLayout(headLayout);

        PDFLabel* text = H::createDataLabel(PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, H::REPLY_LINE_HEIGHT, row);
        text->setText(reply.contents);
        textLayout->addWidget(text);
        rowLayout->addLayout(textLayout, 1);
        m_repliesLayout->addWidget(row);
    }

    m_repliesCaption->setText(tr("%n replies", nullptr, m_replyCount));
}

void PDFAppCommentPopup::updateButtons()
{
    const bool isCompose = m_mode == Mode::Compose;
    const bool isThread = m_mode == Mode::Thread;
    const bool isEditing = isThread && m_isEditingContents;
    const bool isReplyShown = isReplyFieldShown();

    m_moreButton->setVisible(isThread && !isEditing);
    m_textEdit->setVisible(isCompose || isEditing);
    m_scrollArea->setVisible(isThread);
    m_contentsLabel->setVisible(isThread && !isEditing && !m_contentsLabel->text().isEmpty());
    m_divider->setVisible(isThread && m_replyCount > 0 && m_contentsLabel->isVisibleTo(m_threadWidget));
    m_repliesCaption->setVisible(isThread && m_replyCount > 0);
    m_repliesWidget->setVisible(isThread && m_replyCount > 0);
    m_replyField->setVisible(isReplyShown);
    m_cancelButton->setVisible(isCompose || isEditing);

    bool isPrimaryShown = isCompose || isEditing || isReplyShown;
    bool isPrimaryEnabled = false;
    if (isCompose)
    {
        isPrimaryEnabled = !m_textEdit->toPlainText().trimmed().isEmpty();
    }
    else if (isEditing)
    {
        isPrimaryEnabled = true;
    }
    else if (isReplyShown)
    {
        isPrimaryEnabled = !m_replyField->text().trimmed().isEmpty();
    }
    m_primaryButton->setVisible(isPrimaryShown);
    m_primaryButton->setEnabled(isPrimaryEnabled);

    updateTexts();
}

void PDFAppCommentPopup::focusInput(bool focusReply)
{
    if (!isVisible())
    {
        return;
    }

    if (m_textEdit->isVisibleTo(this))
    {
        m_textEdit->setFocus(Qt::OtherFocusReason);
    }
    else if (focusReply && m_replyField->isVisibleTo(this))
    {
        m_replyField->setFocus(Qt::OtherFocusReason);
    }
    else
    {
        setFocus(Qt::OtherFocusReason);
    }
}

QRect PDFAppCommentPopup::computeAnchor() const
{
    if (m_mode == Mode::Compose)
    {
        return m_anchor;
    }

    const PDFAppDocument* document = getDocument();
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    PDFWidget* pdfWidget = m_view ? m_view->getPdfWidget() : nullptr;
    QWidget* drawWidget = pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr;
    QWidget* parent = parentWidget();
    if (m_mode != Mode::Thread || !document || !proxy || !drawWidget || !parent)
    {
        return QRect();
    }

    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document->getDocument().data(), m_annotation);
    const PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = info.isValid() ? snapshot.getPageSnapshot(info.pageIndex) : nullptr;
    if (!item)
    {
        return QRect();
    }

    // The rectangle of the annotation on the screen (a NoZoom icon keeps its size,
    // its top left corner is the anchor point of the page)
    const QRect deviceRect = item->pageToDeviceMatrix.mapRect(info.rectangle).toAlignedRect();
    if (!deviceRect.intersects(drawWidget->rect()))
    {
        return QRect();
    }

    const QPoint topLeft = drawWidget->mapTo(parent, deviceRect.topLeft());
    return QRect(topLeft, deviceRect.size());
}

void PDFAppCommentPopup::updatePlacement()
{
    using H = PDFAppCommentPopupHelper;

    QWidget* parent = parentWidget();
    if (m_mode == Mode::None || !parent || !m_layout)
    {
        return;
    }

    m_anchor = computeAnchor();
    if (!m_anchor.isValid() && m_mode == Mode::Thread)
    {
        if (m_isAnchorTracked)
        {
            // The annotation is scrolled out of the view: the popup waits for it
            hide();
            updateShadow();
            return;
        }

        // A thread of an annotation, which is not shown (Notes, the menu of a
        // hidden annotation): the popup stays at the top trailing corner of the canvas
        const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
        m_anchor = QRect(isRightToLeft ? PARENT_MARGIN : parent->width() - PARENT_MARGIN, PARENT_MARGIN, 1, 1);
    }
    else if (m_mode == Mode::Thread)
    {
        m_isAnchorTracked = true;
    }

    // Height of the card: the parts, the thread scrolled, when the canvas is low
    const int innerWidth = POPUP_WIDTH - 2 * PADDING;
    int threadHeight = 0;
    if (m_scrollArea->isVisibleTo(this))
    {
        m_threadWidget->layout()->invalidate();
        threadHeight = m_threadWidget->layout()->hasHeightForWidth() ? m_threadWidget->layout()->totalHeightForWidth(innerWidth)
                                                                      : m_threadWidget->layout()->totalSizeHint().height();
    }

    // The other parts: the height of the layout with an empty thread (the gaps,
    // the paddings and the wrapped texts as the layout counts them)
    const QMargins shadow = getShadowMargins();
    m_scrollArea->setFixedHeight(0);
    m_layout->invalidate();
    const int layoutWidth = POPUP_WIDTH + shadow.left() + shadow.right();
    const int layoutHeight = m_layout->hasHeightForWidth() ? m_layout->totalHeightForWidth(layoutWidth) : m_layout->totalSizeHint().height();
    const int otherHeight = qMax(2 * PADDING, layoutHeight - shadow.top() - shadow.bottom());

    const int available = qMax(0, parent->height() - 2 * PARENT_MARGIN);
    if (otherHeight + threadHeight > available && threadHeight > 0)
    {
        threadHeight = qMax(H::MINIMUM_THREAD_HEIGHT, available - otherHeight);
    }
    m_scrollArea->setFixedHeight(threadHeight);
    const QSize cardSize(POPUP_WIDTH, otherHeight + threadHeight);

    // Next to the anchor: at the trailing side, else at the leading side, clamped
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const int rightX = m_anchor.right() + 1 + ANCHOR_GAP;
    const int leftX = m_anchor.left() - ANCHOR_GAP - cardSize.width();
    const bool rightFits = rightX + cardSize.width() <= parent->width() - PARENT_MARGIN;
    const bool leftFits = leftX >= PARENT_MARGIN;
    int x = isRightToLeft ? (leftFits || !rightFits ? leftX : rightX) : (rightFits || !leftFits ? rightX : leftX);
    int y = m_anchor.top();
    x = qBound(PARENT_MARGIN, x, qMax(PARENT_MARGIN, parent->width() - PARENT_MARGIN - cardSize.width()));
    y = qBound(PARENT_MARGIN, y, qMax(PARENT_MARGIN, parent->height() - PARENT_MARGIN - cardSize.height()));

    setGeometry(QRect(QPoint(x, y) - QPoint(shadow.left(), shadow.top()), cardSize.grownBy(shadow)));
    if (isHidden())
    {
        show();
        raise();
    }
    updateShadow();
}

QSize PDFAppCommentPopup::sizeHint() const
{
    const QMargins shadow = getShadowMargins();
    return QSize(POPUP_WIDTH, POPUP_HEIGHT).grownBy(shadow);
}

QRect PDFAppCommentPopup::getCardRect() const
{
    return rect().marginsRemoved(getShadowMargins());
}

QRect PDFAppCommentPopup::getCardGeometry() const
{
    return getCardRect().translated(pos());
}

void PDFAppCommentPopup::updateShadow()
{
    if (!m_shadow || m_shadow->parentWidget() != parentWidget())
    {
        return;
    }

    m_shadow->setGeometry(geometry());
    m_shadow->setVisible(!isHidden());
    m_shadow->stackUnder(this);
}

void PDFAppCommentPopup::watchCanvas(bool watch)
{
    if (m_watchedCanvas)
    {
        m_watchedCanvas->removeEventFilter(this);
        m_watchedCanvas = nullptr;
    }

    PDFWidget* pdfWidget = m_view ? m_view->getPdfWidget() : nullptr;
    QWidget* drawWidget = pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr;
    if (watch && drawWidget)
    {
        m_watchedCanvas = drawWidget;
        drawWidget->installEventFilter(this);
    }
}

void PDFAppCommentPopup::showMoreMenu()
{
    if (m_mode != Mode::Thread)
    {
        return;
    }

    // The menu of the popup (a popup of its own, never exec on the stack)
    if (!m_moreMenu)
    {
        m_moreMenu = new QMenu(this);
        m_moreMenu->setObjectName(QStringLiteral("commentPopupMenu"));
    }
    m_moreMenu->clear();

    const PDFAppDocument* document = getDocument();
    const PDFAppAnnotationController* controller = getController();
    const bool isEditingAllowed = controller && controller->isAnnotationEditingAllowed();
    const bool canDelete = isEditingAllowed && document && PDFAppAnnotationOperations::canModifyAnnotation(document->getDocument(), m_annotation);

    QAction* editAction = m_moreMenu->addAction(PDFThemeIcons::getIcon(QStringLiteral("edit")), tr("Edit Text"));
    editAction->setObjectName(QStringLiteral("commentPopupEditText"));
    editAction->setEnabled(canEditContents());
    connect(editAction, &QAction::triggered, this, &PDFAppCommentPopup::onEditTextTriggered);

    QAction* propertiesAction = m_moreMenu->addAction(PDFThemeIcons::getIcon(QStringLiteral("info")), tr("Properties…"));
    propertiesAction->setObjectName(QStringLiteral("commentPopupProperties"));
    connect(propertiesAction, &QAction::triggered, this, &PDFAppCommentPopup::onPropertiesTriggered);

    m_moreMenu->addSeparator();
    QAction* deleteAction = m_moreMenu->addAction(PDFThemeIcons::getIcon(QStringLiteral("trash")), tr("Delete"));
    deleteAction->setObjectName(QStringLiteral("commentPopupDelete"));
    deleteAction->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
    deleteAction->setEnabled(canDelete);
    connect(deleteAction, &QAction::triggered, this, &PDFAppCommentPopup::onDeleteTriggered);

    m_moreMenu->setLayoutDirection(layoutDirection());
    const QPoint position = m_moreButton->mapToGlobal(QPoint(isRightToLeft() ? m_moreButton->width() : 0, m_moreButton->height()));
    m_moreMenu->popup(position);
}

void PDFAppCommentPopup::onMoreClicked()
{
    showMoreMenu();
}

void PDFAppCommentPopup::onEditTextTriggered()
{
    beginEditContents();
}

void PDFAppCommentPopup::onPropertiesTriggered()
{
    // The dialog comes from the event loop (the controller queues it)
    if (PDFAppAnnotationController* controller = getController(); controller && m_mode == Mode::Thread)
    {
        controller->requestProperties(m_annotation);
    }
}

void PDFAppCommentPopup::onDeleteTriggered()
{
    PDFAppAnnotationController* controller = getController();
    PDFAppDocument* document = getDocument();
    if (!controller || !document || m_mode != Mode::Thread || !controller->isAnnotationEditingAllowed())
    {
        return;
    }

    // The popup closes, when its annotation is gone (onDocumentChanged)
    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), { m_annotation });
    if (result)
    {
        controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Delete annotation"));
    }
}

void PDFAppCommentPopup::onCloseClicked()
{
    cancel();
}

void PDFAppCommentPopup::onCancelClicked()
{
    if (m_isEditingContents)
    {
        cancelEditContents();
        return;
    }
    cancel();
}

void PDFAppCommentPopup::onPrimaryClicked()
{
    if (!m_primaryButton->isVisible() || !m_primaryButton->isEnabled())
    {
        return;
    }

    switch (m_mode)
    {
        case Mode::Compose:
            accept();
            break;
        case Mode::Thread:
            if (m_isEditingContents)
            {
                commitContents();
            }
            else
            {
                sendReply();
            }
            break;
        case Mode::None:
            break;
    }
}

void PDFAppCommentPopup::onReplyTextChanged()
{
    updateButtons();
}

void PDFAppCommentPopup::onEditorTextChanged()
{
    if (m_mode == Mode::Compose)
    {
        m_composedText = m_textEdit->toPlainText().left(MAXIMUM_TEXT_LENGTH);
    }
    updateButtons();
}

void PDFAppCommentPopup::onDocumentChanged()
{
    if (m_mode != Mode::Thread)
    {
        return;
    }

    // The annotation is gone (deleted, undo of its creation, another document)
    const PDFAppDocument* document = getDocument();
    if (document != m_document)
    {
        cancel();
        return;
    }

    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, m_annotation);
    if (!info.isValid())
    {
        cancel();
        return;
    }

    if (m_isEditingContents && !canEditContents())
    {
        m_isEditingContents = false;
    }
    updateContents();
    updateReplies();
    updateButtons();
    updatePlacement();
}

void PDFAppCommentPopup::connectDocument()
{
    PDFAppDocument* document = getDocument();
    if (m_document == document)
    {
        return;
    }

    if (m_document)
    {
        m_document->disconnect(this);
    }
    m_document = document;
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppCommentPopup::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppCommentPopup::onDocumentChanged);
    }
}

void PDFAppCommentPopup::onViewChanged()
{
    if (m_mode != Mode::None)
    {
        updatePlacement();
    }
}

void PDFAppCommentPopup::onEditingAllowedChanged()
{
    const PDFAppAnnotationController* controller = getController();
    const bool isEditingAllowed = controller && controller->isAnnotationEditingAllowed();

    // A locked document (1030:161012): no new comment, no editing, no reply field
    if (m_mode == Mode::Compose && !isEditingAllowed)
    {
        cancel();
        return;
    }
    if (m_isEditingContents && !canEditContents())
    {
        m_isEditingContents = false;
    }
    if (m_mode == Mode::Thread)
    {
        updateButtons();
        updatePlacement();
    }
}

void PDFAppCommentPopup::onThemeChanged()
{
    update();
    if (m_divider)
    {
        m_divider->update();
    }
}

bool PDFAppCommentPopup::handleKey(QKeyEvent* event)
{
    if (PDFAppCommentPopupHelper::isSendKey(event))
    {
        onPrimaryClicked();
        return true;
    }
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
    {
        if (m_isEditingContents)
        {
            cancelEditContents();
        }
        else
        {
            cancel();
        }
        return true;
    }
    return false;
}

void PDFAppCommentPopup::keyPressEvent(QKeyEvent* event)
{
    if (handleKey(event))
    {
        event->accept();
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppCommentPopup::mousePressEvent(QMouseEvent* event)
{
    // The margins of the shadow are not a part of the popup for the pointer
    if (!getCardRect().contains(event->position().toPoint()))
    {
        event->ignore();
        return;
    }
    BaseClass::mousePressEvent(event);
}

bool PDFAppCommentPopup::event(QEvent* event)
{
    const bool result = BaseClass::event(event);

    switch (event->type())
    {
        case QEvent::Resize:
            setMask(QRegion(getCardRect()));
            updateShadow();
            break;

        case QEvent::Move:
        case QEvent::Hide:
        case QEvent::Show:
        case QEvent::ZOrderChange:
            updateShadow();
            break;

        case QEvent::LayoutDirectionChange:
            updatePlacement();
            break;

        case QEvent::LanguageChange:
            updateTexts();
            if (m_mode != Mode::None)
            {
                updateContents();
                updateReplies();
                updatePlacement();
            }
            break;

        default:
            break;
    }

    return result;
}

bool PDFAppCommentPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_watchedParent && event->type() == QEvent::Resize && m_mode != Mode::None)
    {
        updatePlacement();
    }
    else if (watched == m_divider && event->type() == QEvent::Paint)
    {
        // divider 1025:21962: 1 px border/divider
        QPainter painter(m_divider);
        PDFStylePainter::fillRect(&painter, QRectF(m_divider->rect()), PDFThemeColors::BorderDivider);
        return true;
    }
    else if (watched == m_watchedCanvas && event->type() == QEvent::MouseButtonPress && m_mode != Mode::None)
    {
        // A click on the page outside of the popup closes it (the compose mode is
        // canceled); the click itself goes on to the page. Closed from the event
        // loop: the press must not delete anything the canvas still uses.
        QMetaObject::invokeMethod(this, &PDFAppCommentPopup::cancel, Qt::QueuedConnection);
    }
    else if ((watched == m_textEdit || watched == m_replyField) && event->type() == QEvent::KeyPress)
    {
        if (handleKey(static_cast<QKeyEvent*>(event)))
        {
            return true;
        }
    }
    else if ((watched == m_textEdit || watched == m_replyField) && event->type() == QEvent::ShortcutOverride)
    {
        // Esc and Ctrl+Enter belong to the popup, not to the shortcuts of the window
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (PDFAppCommentPopupHelper::isSendKey(keyEvent) || keyEvent->key() == Qt::Key_Escape)
        {
            event->accept();
            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppCommentPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Card bg/surface with the border border/default, radius 12; the shadow is
    // drawn by PDFAppCommentPopupShadow below the popup
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card(getCardRect());
    const PDFStylePainter::Radii radii(RADIUS);
    PDFStylePainter::fill(&painter, card, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, card, radii, PDFThemeColors::BorderDefault, 1.0);
}

}   // namespace pdf
