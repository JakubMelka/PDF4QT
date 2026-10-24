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

#include "pdfmessagebox.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfwidgetstyleimpl.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

namespace pdf
{

/// Badge of a message box: circle of 36 px with an icon
class PDFMessageBoxBadge : public QWidget
{
public:
    explicit PDFMessageBoxBadge(PDFThemeIcons::Badge badge, QWidget* parent) :
        QWidget(parent),
        m_badge(badge)
    {
        setFixedSize(36, 36);
    }

    const QString& getIconName() const { return m_iconName; }

    void setIconName(const QString& iconName)
    {
        if (m_iconName != iconName)
        {
            m_iconName = iconName;
            update();
        }
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        PDFThemeIcons::paintBadge(&painter, QRectF(rect()), m_badge, m_iconName);
    }

private:
    PDFThemeIcons::Badge m_badge;
    QString m_iconName;
};

/// Scrim (bg/scrim) covering the parent widget of a message box, while the
/// message box is shown
class PDFMessageBoxScrim : public QWidget
{
public:
    explicit PDFMessageBoxScrim(QWidget* host) :
        QWidget(host),
        m_host(host)
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setGeometry(host->rect());
        host->installEventFilter(this);
    }

    virtual ~PDFMessageBoxScrim() override
    {
        // The filter is removed from the host (also from a host, which is
        // deleting its children: its QObject part is still whole)
        if (m_host)
        {
            m_host->removeEventFilter(this);
        }
    }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_host && m_host == parentWidget() && event->type() == QEvent::Resize)
        {
            setGeometry(m_host->rect());
        }

        return QWidget::eventFilter(watched, event);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgScrim);
    }

private:
    QPointer<QWidget> m_host;
};

PDFMessageBox::PDFMessageBox(PDFThemeIcons::Badge badge, const QString& title, const QString& text, QDialogButtonBox::StandardButtons buttons, QWidget* parent) :
    QDialog(parent),
    m_mainLayout(new QVBoxLayout(this)),
    m_titleLabel(new PDFLabel(title, PDFThemeFonts::Role::DialogTitle, PDFThemeColors::TextPrimary, this)),
    m_textLabel(new PDFLabel(text, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this)),
    m_detailsLabel(nullptr),
    m_detailsEdit(nullptr),
    m_detailsFrame(new QFrame(this)),
    m_checkBox(new QCheckBox(this)),
    m_buttonBox(new QDialogButtonBox(buttons, this))
{
    setWindowTitle(QApplication::applicationDisplayName().isEmpty() ? title : QApplication::applicationDisplayName());
    setAccessibleName(title);
    setAccessibleDescription(text);
    setModal(true);

    // The card has no title bar of the system, its shadow is drawn into the
    // transparent margins of the window
    setWindowFlag(Qt::FramelessWindowHint, true);
    setWindowFlag(Qt::NoDropShadowWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, PDFWidgetStyleImpl::isTranslucencyAvailable());

    // Paddings 22 / 20 inside of the border of 1 px, sections 14 px apart
    QVBoxLayout* mainLayout = m_mainLayout;
    mainLayout->setSpacing(14);
    updateMargins();

    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(12);
    PDFMessageBoxBadge* badgeWidget = new PDFMessageBoxBadge(badge, this);
    badgeWidget->setAccessibleName(getBadgeName(badge));
    headLayout->addWidget(badgeWidget, 0, Qt::AlignVCenter);
    m_badgeWidget = badgeWidget;

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    // The title and the text are plain texts as the details: a name of a file or of
    // a folder ("<b>Reports</b>") given by an application is never a rich text
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_textLabel->setTextFormat(Qt::PlainText);

    // The text has the lines of the design (Regular 12 in lines of 16 px)
    m_textLabel->setLineHeight(TEXT_LINE_HEIGHT);
    m_textLabel->setWordWrap(true);
    m_textLabel->setMinimumWidth(336);
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_textLabel->setVisible(!text.isEmpty());

    // A label with a selectable text takes the focus by a click and it becomes
    // the first widget of the dialog, the first focus belongs to the buttons
    m_textLabel->setFocusPolicy(Qt::NoFocus);
    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_textLabel);
    headLayout->addLayout(textLayout, 1);
    mainLayout->addLayout(headLayout);

    // Details in a note box
    m_detailsFrame->setProperty(PDFWidgetStyleProperties::NOTE, true);
    QVBoxLayout* detailsLayout = new QVBoxLayout(m_detailsFrame);
    detailsLayout->setContentsMargins(12, 8, 12, 8);
    m_detailsLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, m_detailsFrame);
    m_detailsLabel->setWordWrap(true);

    // The details are a plain text as the long details of the editor (QMessageBox
    // as well): paths and names of files are never interpreted as rich text;
    // they have the lines of the design (Regular 11 in lines of 15 px)
    m_detailsLabel->setTextFormat(Qt::PlainText);
    m_detailsLabel->setLineHeight(DETAILS_LINE_HEIGHT);
    m_detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailsLabel->setFocusPolicy(Qt::NoFocus);
    detailsLayout->addWidget(m_detailsLabel);

    // Long details are scrolled
    m_detailsEdit = new PDFPlainTextEdit(m_detailsFrame);
    m_detailsEdit->setReadOnly(true);
    m_detailsEdit->setFrameShape(QFrame::NoFrame);
    m_detailsEdit->setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular));
    m_detailsEdit->document()->setDocumentMargin(0.0);
    m_detailsEdit->setFixedHeight(DETAILS_HEIGHT);
    m_detailsEdit->setVisible(false);
    PDFThemeWidgetHelper::bindTextPalette(m_detailsEdit, PDFThemeColors::TextMuted);
    detailsLayout->addWidget(m_detailsEdit);
    m_detailsFrame->setVisible(false);
    mainLayout->addWidget(m_detailsFrame);

    QHBoxLayout* actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    m_checkBox->setVisible(false);
    actionsLayout->addWidget(m_checkBox);
    actionsLayout->addStretch(1);
    actionsLayout->addWidget(m_buttonBox);
    mainLayout->addLayout(actionsLayout);
    m_actionsLayout = actionsLayout;

    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    connect(m_buttonBox, &QDialogButtonBox::clicked, this, &PDFMessageBox::onButtonClicked);
}

PDFMessageBox::~PDFMessageBox()
{
    hideScrim();
}

QMargins PDFMessageBox::getShadowMargins() const
{
    if (!isWindow() || !testAttribute(Qt::WA_TranslucentBackground))
    {
        return QMargins();
    }

    return PDFWidgetStyleImpl::getShadowMargins(PDFWidgetStyleImpl::PopupKind::Popover);
}

void PDFMessageBox::updateMargins()
{
    if (!m_mainLayout)
    {
        // The application has deleted the layout
        return;
    }

    m_mainLayout->setContentsMargins(QMargins(PADDING_H + BORDER, PADDING_V + BORDER, PADDING_H + BORDER, PADDING_V + BORDER) + getShadowMargins());
}

void PDFMessageBox::updateSizeConstraint()
{
    if (!m_mainLayout)
    {
        return;
    }

    // A card of a fixed width has the height of its content at that width (the
    // texts wrap), a card without it the size of its content
    m_mainLayout->setSizeConstraint(m_cardWidth > 0 ? QLayout::SetDefaultConstraint : QLayout::SetFixedSize);
    if (m_cardWidth > 0)
    {
        const QMargins shadow = getShadowMargins();
        setFixedWidth(m_cardWidth + shadow.left() + shadow.right());
    }
}

void PDFMessageBox::setCardWidth(int width)
{
    width = qMax(0, width);
    if (m_cardWidth == width)
    {
        return;
    }

    m_cardWidth = width;
    if (m_cardWidth == 0)
    {
        // The card follows its content again
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
    }
    updateSizeConstraint();
    updateGeometry();
}

int PDFMessageBox::getContentWidth() const
{
    return m_cardWidth > 0 ? qMax(0, m_cardWidth - 2 * (PADDING_H + BORDER)) : 0;
}

QSize PDFMessageBox::sizeHint() const
{
    if (m_cardWidth <= 0)
    {
        return QDialog::sizeHint();
    }

    // QWidget::adjustSize of a window asks the height for the width of the size hint
    const QMargins shadow = getShadowMargins();
    const int width = m_cardWidth + shadow.left() + shadow.right();
    int height = QDialog::sizeHint().height();
    if (m_mainLayout && m_mainLayout->hasHeightForWidth())
    {
        height = m_mainLayout->totalHeightForWidth(width);
    }
    return QSize(width, height);
}

bool PDFMessageBox::isBadgeVisible() const
{
    return m_badgeWidget && !m_badgeWidget->isHidden();
}

void PDFMessageBox::setBadgeVisible(bool isVisible)
{
    if (m_badgeWidget)
    {
        m_badgeWidget->setVisible(isVisible);
    }
}

QString PDFMessageBox::getBadgeIconName() const
{
    const PDFMessageBoxBadge* badge = dynamic_cast<const PDFMessageBoxBadge*>(m_badgeWidget.data());
    return badge ? badge->getIconName() : QString();
}

void PDFMessageBox::setTitle(const QString& title)
{
    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }
    setAccessibleName(title);
}

void PDFMessageBox::setText(const QString& text)
{
    if (m_textLabel)
    {
        m_textLabel->setText(text);
        m_textLabel->setVisible(!text.isEmpty());
    }
    setAccessibleDescription(text);
}

void PDFMessageBox::setBadgeIconName(const QString& iconName)
{
    if (PDFMessageBoxBadge* badge = dynamic_cast<PDFMessageBoxBadge*>(m_badgeWidget.data()))
    {
        badge->setIconName(iconName);
    }
}

void PDFMessageBox::addContent(QWidget* widget)
{
    if (!widget || !m_mainLayout)
    {
        return;
    }

    // Before the row of the buttons (the last item of the layout, unless the
    // application has removed it)
    widget->setParent(this);
    const int actionsIndex = m_actionsLayout ? m_mainLayout->indexOf(m_actionsLayout.data()) : -1;
    m_mainLayout->insertWidget(actionsIndex >= 0 ? actionsIndex : m_mainLayout->count(), widget);
}

QPushButton* PDFMessageBox::addActionButton(const QString& text, ActionPosition position, bool isPrimary)
{
    if (!m_actionsLayout)
    {
        return nullptr;
    }

    QPushButton* button = new QPushButton(text, this);
    button->setAutoDefault(false);
    if (isPrimary)
    {
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::PRIMARY);
        button->setDefault(true);
    }

    // The own row replaces the buttons of the platform
    if (m_buttonBox && m_buttonBox->buttons().isEmpty())
    {
        m_buttonBox->hide();
    }

    if (position == ActionPosition::Leading)
    {
        // Leading buttons come before the stretch (after the check box and the
        // leading buttons added before)
        int stretchIndex = m_actionsLayout->count();
        for (int i = 0; i < m_actionsLayout->count(); ++i)
        {
            if (m_actionsLayout->itemAt(i)->spacerItem())
            {
                stretchIndex = i;
                break;
            }
        }
        m_actionsLayout->insertWidget(stretchIndex, button);
    }
    else
    {
        m_actionsLayout->addWidget(button);
    }

    m_actionButtons.append(button);
    return button;
}

QList<QPushButton*> PDFMessageBox::getActionButtons() const
{
    // In the order of the row (leading buttons first)
    QList<QPushButton*> buttons;
    if (m_actionsLayout)
    {
        for (int i = 0; i < m_actionsLayout->count(); ++i)
        {
            QPushButton* button = qobject_cast<QPushButton*>(m_actionsLayout->itemAt(i)->widget());
            if (button && m_actionButtons.contains(button))
            {
                buttons.append(button);
            }
        }
    }
    return buttons;
}

void PDFMessageBox::setVisible(bool visible)
{
    if (visible && isWindow() && !isVisible())
    {
        // The window is placed before it is shown, so it does not jump
        updateMargins();
        updateSizeConstraint();
        ensurePolished();
        adjustSize();
        moveOverParent();
        showScrim();
    }

    QDialog::setVisible(visible);

    if (!visible)
    {
        hideScrim();
    }
}

QWidget* PDFMessageBox::getScrimTarget() const
{
    return m_scrimTarget ? m_scrimTarget.data() : parentWidget();
}

void PDFMessageBox::setScrimTarget(QWidget* target)
{
    if (m_scrimTarget.data() == target)
    {
        return;
    }

    m_scrimTarget = target;
    if (isWindow() && isVisible())
    {
        // The scrim moves to the new target, the card follows it
        hideScrim();
        moveOverParent();
        showScrim();
    }
}

void PDFMessageBox::moveOverParent()
{
    QWidget* host = getScrimTarget();
    if (!host)
    {
        return;
    }

    // The card (not the window with the shadow) is centered over the target
    const QRect hostRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    const QRect card = rect().marginsRemoved(getShadowMargins());
    QRect cardRect(QPoint(0, 0), card.size());
    cardRect.moveCenter(hostRect.center());

    if (const QScreen* screen = host->screen())
    {
        // The card stays on the screen, the shadow may leave it
        const QRect available = screen->availableGeometry();
        cardRect.moveLeft(qBound(available.left(), cardRect.left(), qMax(available.left(), available.right() - cardRect.width() + 1)));
        cardRect.moveTop(qBound(available.top(), cardRect.top(), qMax(available.top(), available.bottom() - cardRect.height() + 1)));
    }

    move(cardRect.topLeft() - card.topLeft());
}

void PDFMessageBox::showScrim()
{
    QWidget* host = getScrimTarget();
    if (!host || m_scrim || !host->isVisible())
    {
        return;
    }

    m_scrim = new PDFMessageBoxScrim(host);
    m_scrim->raise();
    m_scrim->show();
}

void PDFMessageBox::hideScrim()
{
    if (m_scrim)
    {
        // The scrim is deleted at once, the parent can be deleted before the event loop runs
        delete m_scrim.data();
    }
}

void PDFMessageBox::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);

    if (!event->spontaneous())
    {
        hideScrim();
    }
}

void PDFMessageBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QMargins margins = getShadowMargins();
    const QRectF card = QRectF(rect()).marginsRemoved(QMarginsF(margins));

    if (margins.isNull() && isWindow())
    {
        // Opaque window: the card fills the window, corners can not be rounded
        PDFStylePainter::fillRect(&painter, card, PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(&painter, card, PDFStylePainter::Radii(0.0), PDFThemeColors::BorderDefault, 1.0);
        return;
    }

    if (!margins.isNull())
    {
        PDFStylePainter::shadow(&painter, card, PDFStylePainter::Radii(CARD_RADIUS), QPointF(0.0, 8.0), 24.0, PDFStylePainter::color(PDFThemeColors::EffectShadow));
    }

    PDFStylePainter::fill(&painter, card, PDFStylePainter::Radii(CARD_RADIUS), PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, card, PDFStylePainter::Radii(CARD_RADIUS), PDFThemeColors::BorderDefault, 1.0);
}

void PDFMessageBox::setDetailedText(const QString& text)
{
    // Long details (a trace of an error) are scrolled, so the message box fits the screen
    const bool isLong = text.size() > DETAILS_LONG_SIZE || text.count(QLatin1Char('\n')) > DETAILS_LONG_LINES;

    m_detailedText = text;
    if (m_detailsLabel)
    {
        m_detailsLabel->setText(isLong ? QString() : text);
        m_detailsLabel->setVisible(!isLong);
    }
    if (m_detailsEdit)
    {
        m_detailsEdit->setPlainText(isLong ? text : QString());
        m_detailsEdit->setVisible(isLong);
    }
    if (m_detailsFrame)
    {
        m_detailsFrame->setVisible(!text.isEmpty());
    }
}

void PDFMessageBox::setCheckBoxText(const QString& text)
{
    if (m_checkBox)
    {
        m_checkBox->setText(text);
        m_checkBox->setVisible(!text.isEmpty());
    }
}

bool PDFMessageBox::isCheckBoxChecked() const
{
    return m_checkBox && m_checkBox->isVisibleTo(this) && m_checkBox->isChecked();
}

void PDFMessageBox::setDefaultButton(QDialogButtonBox::StandardButton button)
{
    if (QPushButton* pushButton = m_buttonBox ? m_buttonBox->button(button) : nullptr)
    {
        pushButton->setDefault(true);
        pushButton->setFocus();
    }
}

void PDFMessageBox::setEscapeButton(QDialogButtonBox::StandardButton button)
{
    m_escapeButton = m_buttonBox ? m_buttonBox->button(button) : nullptr;
}

QAbstractButton* PDFMessageBox::getClickedButton() const
{
    return m_clickedButton.data();
}

void PDFMessageBox::reject()
{
    if (!m_clickedButton)
    {
        // The Escape key or the close button of the window
        QAbstractButton* button = getEscapeButton();
        if (!button)
        {
            // Without the buttons (the application has deleted the button box or
            // all of its buttons) there is no choice, the message box must still
            // be closable
            if (!m_buttonBox || m_buttonBox->buttons().isEmpty())
            {
                QDialog::reject();
            }
            return;
        }

        m_clickedButton = button;
        m_clickedStandardButton = m_buttonBox ? m_buttonBox->standardButton(button) : QDialogButtonBox::NoButton;
    }

    QDialog::reject();
}

void PDFMessageBox::showEvent(QShowEvent* event)
{
    if (!event->spontaneous())
    {
        // A message box shown inside of another widget has no shadow
        updateMargins();

        // Not for a window restored by the system: the focus stays, where the user has left it
        m_clickedButton = nullptr;
        m_clickedStandardButton = QDialogButtonBox::NoButton;
        ensureDefaultButton();
    }

    QDialog::showEvent(event);

#if QT_CONFIG(accessibility)
    QAccessibleEvent alertEvent(this, QAccessible::Alert);
    QAccessible::updateAccessibility(&alertEvent);
#endif
}

void PDFMessageBox::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy))
    {
        // The selected text is copied, or the whole message as in QMessageBox
        QString text = m_textLabel ? m_textLabel->selectedText() : QString();
        if (text.isEmpty() && m_detailsLabel)
        {
            text = m_detailsLabel->selectedText();
        }
        if (text.isEmpty())
        {
            QStringList parts;
            for (const QString& part : { m_titleLabel ? m_titleLabel->text() : QString(), m_textLabel ? m_textLabel->text() : QString(), m_detailedText })
            {
                if (!part.isEmpty())
                {
                    parts << part;
                }
            }
            text = parts.join(QStringLiteral("\n\n"));
        }

        QGuiApplication::clipboard()->setText(text);
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

void PDFMessageBox::ensureDefaultButton()
{
    if (!m_buttonBox)
    {
        return;
    }

    const QList<QAbstractButton*> buttons = m_buttonBox->buttons();
    QPushButton* defaultButton = nullptr;

    for (QAbstractButton* button : buttons)
    {
        QPushButton* pushButton = qobject_cast<QPushButton*>(button);
        if (pushButton && pushButton->isDefault())
        {
            defaultButton = pushButton;
            break;
        }
    }

    if (!defaultButton)
    {
        // The first focused button would become the default one, which is the
        // first button of the layout - it can be the destructive one
        for (QDialogButtonBox::ButtonRole role : { QDialogButtonBox::AcceptRole, QDialogButtonBox::YesRole })
        {
            for (QAbstractButton* button : buttons)
            {
                QPushButton* pushButton = qobject_cast<QPushButton*>(button);
                if (!defaultButton && pushButton && m_buttonBox->buttonRole(button) == role)
                {
                    defaultButton = pushButton;
                }
            }
        }

        for (QAbstractButton* button : buttons)
        {
            QPushButton* pushButton = qobject_cast<QPushButton*>(button);
            if (!defaultButton && pushButton)
            {
                defaultButton = pushButton;
            }
        }

        if (defaultButton)
        {
            defaultButton->setDefault(true);
        }
    }

    if (defaultButton)
    {
        defaultButton->setFocus();
    }
}

QAbstractButton* PDFMessageBox::getEscapeButton() const
{
    if (m_escapeButton)
    {
        return m_escapeButton;
    }

    if (!m_buttonBox)
    {
        return nullptr;
    }

    const QList<QAbstractButton*> buttons = m_buttonBox->buttons();
    if (buttons.size() == 1)
    {
        return buttons.front();
    }

    // The Cancel button is the escape button also among other rejecting buttons
    if (QAbstractButton* cancelButton = m_buttonBox->button(QDialogButtonBox::Cancel))
    {
        return cancelButton;
    }

    for (QDialogButtonBox::ButtonRole role : { QDialogButtonBox::RejectRole, QDialogButtonBox::NoRole })
    {
        QAbstractButton* found = nullptr;
        int count = 0;

        for (QAbstractButton* button : buttons)
        {
            if (m_buttonBox->buttonRole(button) == role)
            {
                found = button;
                ++count;
            }
        }

        if (count == 1)
        {
            return found;
        }
    }

    return nullptr;
}

QString PDFMessageBox::getBadgeName(PDFThemeIcons::Badge badge)
{
    switch (badge)
    {
        case PDFThemeIcons::Badge::Information:
            return tr("Information");
        case PDFThemeIcons::Badge::Question:
            return tr("Question");
        case PDFThemeIcons::Badge::Warning:
            return tr("Warning");
        case PDFThemeIcons::Badge::Critical:
            return tr("Error");
        case PDFThemeIcons::Badge::Success:
            return tr("Success");
        case PDFThemeIcons::Badge::Neutral:
            return tr("Information");
    }

    return QString();
}

QAbstractButton* PDFMessageBox::addButton(const QString& text, QDialogButtonBox::ButtonRole role)
{
    return m_buttonBox ? m_buttonBox->addButton(text, role) : nullptr;
}

QDialogButtonBox::StandardButton PDFMessageBox::information(QWidget* parent, const QString& title, const QString& text, QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton)
{
    return showMessage(PDFThemeIcons::Badge::Information, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton PDFMessageBox::question(QWidget* parent, const QString& title, const QString& text, QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton)
{
    return showMessage(PDFThemeIcons::Badge::Question, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton PDFMessageBox::warning(QWidget* parent, const QString& title, const QString& text, QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton)
{
    return showMessage(PDFThemeIcons::Badge::Warning, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton PDFMessageBox::critical(QWidget* parent, const QString& title, const QString& text, QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton)
{
    return showMessage(PDFThemeIcons::Badge::Critical, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton PDFMessageBox::showMessage(PDFThemeIcons::Badge badge, QWidget* parent, const QString& title, const QString& text, QDialogButtonBox::StandardButtons buttons, QDialogButtonBox::StandardButton defaultButton)
{
    // The message box is a child of the parent: when the parent is deleted while
    // the message box is shown, the message box is deleted with it (a message box
    // on the stack would be deleted twice)
    QPointer<PDFMessageBox> messageBox = new PDFMessageBox(badge, title, text, buttons, parent);
    if (defaultButton != QDialogButtonBox::NoButton)
    {
        messageBox->setDefaultButton(defaultButton);
    }
    messageBox->exec();

    if (!messageBox)
    {
        return QDialogButtonBox::NoButton;
    }

    const QDialogButtonBox::StandardButton result = messageBox->getClickedStandardButton();
    delete messageBox.data();
    return result;
}

void PDFMessageBox::onButtonClicked(QAbstractButton* button)
{
    if (!m_buttonBox)
    {
        return;
    }

    m_clickedButton = button;
    m_clickedStandardButton = m_buttonBox->standardButton(button);

    switch (m_buttonBox->buttonRole(button))
    {
        case QDialogButtonBox::RejectRole:
        case QDialogButtonBox::NoRole:
        case QDialogButtonBox::DestructiveRole:
            reject();
            break;

        default:
            // Each button closes the message box, as in QMessageBox
            accept();
            break;
    }
}

}   // namespace pdf
