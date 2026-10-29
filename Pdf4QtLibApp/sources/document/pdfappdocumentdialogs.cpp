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

#include "pdfappdocumentdialogs.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfapphomewidgets.h"
#include "pdfapppagelayout.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbadges.h"
#include "pdffluentwidgets.h"
#include "pdflockeddocumentpicture.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtMath>
#include <QWidgetAction>

#include <initializer_list>

namespace pdf
{

/// Helpers of the dialogs and screens of a document tab
class PDFAppDocumentDialogsHelper
{
public:
    PDFAppDocumentDialogsHelper() = delete;

    /// Width of a message box with a list (the note 958:28033)
    static constexpr int LIST_WIDTH = 520;

    /// Width of a message box without a list
    static constexpr int WIDTH = 440;

    /// Radius and shadow of the cards of the state screen (password-card 228:8800)
    static constexpr qreal CARD_RADIUS = 16.0;

    /// Plain icon of the design (an icon badge without the fill)
    static PDFIconBadge* createIcon(const QString& iconName, int size, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFIconBadge* icon = new PDFIconBadge(iconName, size, size, parent);
        icon->setRadius(0.0);
        icon->setColors(std::nullopt, color);
        return icon;
    }

    /// Layout without margins
    template<typename Layout>
    static Layout* createLayout(QWidget* widget, const QMargins& margins, int spacing)
    {
        Layout* layout = new Layout(widget);
        layout->setContentsMargins(margins);
        layout->setSpacing(spacing);
        return layout;
    }

    /// Text of a count with a singular form ("1 file", "2 files")
    static QString getCountText(int count, const QString& singular, const QString& plural)
    {
        return count == 1 ? singular : plural.arg(count);
    }

    /// Shown path of a folder (native separators, bounded)
    static QString getFolderText(const QString& filePath)
    {
        return QDir::toNativeSeparators(QFileInfo(filePath).absolutePath()).left(1024);
    }
};

// ------------------------------------------------------------------ state screen

PDFAppDocumentStateScreen::PDFAppDocumentStateScreen(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("documentStateScreen"));

    QVBoxLayout* layout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(this, QMargins(), 0);

    // locked-body: the card centered in bg/subtle; a window lower than the card scrolls
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("stateScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scrollArea, 1);

    m_body = new QWidget(scrollArea);
    m_body->setObjectName(QStringLiteral("lockedBody"));
    m_body->installEventFilter(this);
    QVBoxLayout* bodyLayout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(m_body, QMargins(16, 24, 16, 24), 0);
    bodyLayout->addStretch(1);

    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addStretch(1);
    createPasswordCard(m_body);
    createLoadingPanel(m_body);
    rowLayout->addWidget(m_passwordCard);
    rowLayout->addWidget(m_loadingPanel);
    rowLayout->addStretch(1);
    bodyLayout->addLayout(rowLayout);
    bodyLayout->addStretch(1);
    scrollArea->setWidget(m_body);

    m_statusBar = new PDFAppDocumentStatusBar(this);
    m_statusBar->setObjectName(QStringLiteral("stateStatusBar"));
    layout->addWidget(m_statusBar);

    m_loadingTimer = new QTimer(this);
    m_loadingTimer->setSingleShot(true);
    m_loadingTimer->setInterval(LOADING_DELAY);
    connect(m_loadingTimer.data(), &QTimer::timeout, this, &PDFAppDocumentStateScreen::onLoadingDelayElapsed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppDocumentStateScreen::updateTexts);

    updateState();
}

PDFAppDocumentStateScreen::~PDFAppDocumentStateScreen()
{
    if (m_document)
    {
        m_document->disconnect(this);
    }
}

void PDFAppDocumentStateScreen::createPasswordCard(QWidget* parent)
{
    // password-card: 440 wide, paddings 36 / 40 / 32 (inside the border of 1 px), gap 24
    m_passwordCard = new QFrame(parent);
    m_passwordCard->setObjectName(QStringLiteral("passwordCard"));
    m_passwordCard->setFrameShape(QFrame::StyledPanel);
    PDFWidgetStyleProperties::set(m_passwordCard, PDFWidgetStyleProperties::OVERLAY);
    m_passwordCard->setFixedWidth(CARD_WIDTH);
    QVBoxLayout* cardLayout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(m_passwordCard, QMargins(40, 36, 40, 32), 24);

    m_picture = new PDFLockedDocumentPicture(PDFLockedDocumentPicture::State::Locked, m_passwordCard);
    m_picture->setObjectName(QStringLiteral("lockedDocumentPicture"));
    cardLayout->addWidget(m_picture, 0, Qt::AlignHCenter);

    // texts: the title Semi Bold 20, the subtitle Regular 13 text/secondary with
    // the name of the file Semi Bold text/primary, gap 8
    QWidget* texts = new QWidget(m_passwordCard);
    QVBoxLayout* textsLayout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(texts, QMargins(), 8);
    m_titleLabel = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 20, PDFThemeColors::TextPrimary, texts);
    m_titleLabel->setObjectName(QStringLiteral("stateTitle"));
    m_titleLabel->setAlignment(Qt::AlignHCenter);
    m_titleLabel->setFixedHeight(24);
    textsLayout->addWidget(m_titleLabel);

    // The subtitle is one text (the order and the direction of the translation),
    // the name of the file is escaped (never markup of the document) and stressed;
    // a long name wraps
    m_subtitle = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::Body, PDFThemeColors::TextSecondary, texts);
    m_subtitle->setObjectName(QStringLiteral("stateSubtitle"));
    m_subtitle->setTextFormat(Qt::RichText);
    m_subtitle->setTextInteractionFlags(Qt::NoTextInteraction);
    m_subtitle->setAlignment(Qt::AlignHCenter);
    m_subtitle->setWordWrap(true);
    QWidget* subtitle = m_subtitle;
    textsLayout->addWidget(subtitle);
    cardLayout->addWidget(texts);

    // form: the password field 40 and the error, gap 8
    QWidget* form = new QWidget(m_passwordCard);
    QVBoxLayout* formLayout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(form, QMargins(), 8);
    m_passwordField = new PDFLineEdit(form);
    m_passwordField->setObjectName(QStringLiteral("passwordField"));
    m_passwordField->setPasswordField();
    m_passwordField->setMaxLength(1024);
    connect(m_passwordField.data(), &QLineEdit::textEdited, this, &PDFAppDocumentStateScreen::onPasswordEdited);
    connect(m_passwordField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentStateScreen::onOpenClicked);
    formLayout->addWidget(m_passwordField);

    // error: alert-circle 14 icon/danger, Medium 12 text/danger, gap 6
    m_errorRow = new QWidget(form);
    m_errorRow->setObjectName(QStringLiteral("passwordError"));
    QHBoxLayout* errorLayout = PDFAppDocumentDialogsHelper::createLayout<QHBoxLayout>(m_errorRow, QMargins(), 6);
    errorLayout->addWidget(PDFAppDocumentDialogsHelper::createIcon(QStringLiteral("alert-circle"), 14, PDFThemeColors::IconDanger, m_errorRow), 0, Qt::AlignVCenter);
    m_errorText = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 12, PDFThemeColors::TextDanger, m_errorRow);
    m_errorText->setWordWrap(true);
    errorLayout->addWidget(m_errorText, 1);
    m_errorRow->hide();
    formLayout->addWidget(m_errorRow);
    cardLayout->addWidget(form);

    // actions: Close tab (Secondary) and Open document (Primary) of the same width, gap 12
    QWidget* actions = new QWidget(m_passwordCard);
    QHBoxLayout* actionsLayout = PDFAppDocumentDialogsHelper::createLayout<QHBoxLayout>(actions, QMargins(), 12);
    m_closeButton = PDFAppPageWidgets::createButton(QString(), QString(), false, actions);
    m_closeButton->setObjectName(QStringLiteral("closeTabButton"));
    m_openButton = PDFAppPageWidgets::createButton(QString(), QString(), true, actions);
    m_openButton->setObjectName(QStringLiteral("openDocumentButton"));
    for (QPushButton* button : { m_closeButton.data(), m_openButton.data() })
    {
        button->setIcon(QIcon());
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        actionsLayout->addWidget(button, 1);
    }
    connect(m_closeButton.data(), &QPushButton::clicked, this, &PDFAppDocumentStateScreen::onCloseClicked);
    connect(m_openButton.data(), &QPushButton::clicked, this, &PDFAppDocumentStateScreen::onOpenClicked);
    cardLayout->addWidget(actions);
}

void PDFAppDocumentStateScreen::createLoadingPanel(QWidget* parent)
{
    // loading-card 1232:164057 (the reading of a large file): the card of the
    // password screen - 440 wide, paddings 36 / 40 / 32 (inside the border of 1 px),
    // gap 24 - with the title Semi Bold 20 and the progress bar of the full width
    m_loadingPanel = new QFrame(parent);
    m_loadingPanel->setObjectName(QStringLiteral("loadingPanel"));
    static_cast<QFrame*>(m_loadingPanel.data())->setFrameShape(QFrame::StyledPanel);
    PDFWidgetStyleProperties::set(m_loadingPanel, PDFWidgetStyleProperties::OVERLAY);
    m_loadingPanel->setFixedWidth(CARD_WIDTH);
    QVBoxLayout* panelLayout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(m_loadingPanel, QMargins(40, 36, 40, 32), 24);

    // A long name of the file wraps (the card has a fixed width)
    m_loadingTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 20, PDFThemeColors::TextPrimary, m_loadingPanel);
    m_loadingTitle->setObjectName(QStringLiteral("loadingTitle"));
    m_loadingTitle->setAlignment(Qt::AlignHCenter);
    m_loadingTitle->setWordWrap(true);
    panelLayout->addWidget(m_loadingTitle);

    QProgressBar* progressBar = new QProgressBar(m_loadingPanel);
    progressBar->setObjectName(QStringLiteral("loadingProgress"));
    progressBar->setTextVisible(false);
    progressBar->setRange(0, 0);
    // loading-progress 1232:164120 is the bar of 4 px itself (the style keeps an
    // explicit height of the application)
    progressBar->setFixedHeight(4);
    m_loadingBar = progressBar;
    panelLayout->addWidget(progressBar);
    m_loadingPanel->hide();
}

void PDFAppDocumentStateScreen::setDocument(PDFAppDocument* document)
{
    if (m_document == document)
    {
        return;
    }

    if (m_document)
    {
        m_document->disconnect(this);
    }
    m_document = document;
    if (m_document)
    {
        connect(m_document.data(), &PDFAppDocument::stateChanged, this, &PDFAppDocumentStateScreen::updateState);
        connect(m_document.data(), &PDFAppDocument::fileChanged, this, &PDFAppDocumentStateScreen::updateTexts);
        connect(m_document.data(), &PDFAppDocument::loadingProgressChanged, this, &PDFAppDocumentStateScreen::onLoadingProgressChanged);
    }
    updateState();
}

void PDFAppDocumentStateScreen::updateState()
{
    const PDFAppDocumentState state = m_document ? m_document->getState() : PDFAppDocumentState::Ready;
    const bool isPassword = state == PDFAppDocumentState::PasswordRequired;
    const bool isLoading = state == PDFAppDocumentState::Loading;
    const bool isWrong = isPassword && m_document->isPasswordWrong();

    // The progress of the reading appears only after a delay (a fast opening shows nothing)
    if (isLoading)
    {
        if (!m_isLoadingShown && m_loadingTimer && !m_loadingTimer->isActive())
        {
            m_loadingTimer->start();
        }
    }
    else
    {
        m_isLoadingShown = false;
        if (m_loadingTimer)
        {
            m_loadingTimer->stop();
        }
    }

    if (m_passwordCard)
    {
        m_passwordCard->setVisible(isPassword);
    }
    if (m_loadingPanel)
    {
        m_loadingPanel->setVisible(isLoading && m_isLoadingShown);
    }

    if (m_picture)
    {
        m_picture->setState(isWrong ? PDFLockedDocumentPicture::State::Wrong : PDFLockedDocumentPicture::State::Locked);
    }
    if (m_passwordField)
    {
        // A wrong password is cleared (228:8965 shows the empty field with the error)
        if (isWrong)
        {
            m_passwordField->clear();
        }
        m_passwordField->setInvalid(isWrong, isWrong ? tr("Incorrect password. Check it and try again.") : QString());
        m_passwordField->setEnabled(isPassword);
    }
    if (m_errorRow)
    {
        m_errorRow->setVisible(isWrong);
    }

    updateTexts();
    updateOpenButton();

    if (isPassword && m_passwordField && isVisible())
    {
        m_passwordField->setFocus(Qt::OtherFocusReason);
    }
    update();
}

void PDFAppDocumentStateScreen::updateTexts()
{
    const QString title = m_document ? m_document->getTitle() : QString();
    const PDFAppDocumentState state = m_document ? m_document->getState() : PDFAppDocumentState::Ready;
    const bool isWrong = state == PDFAppDocumentState::PasswordRequired && m_document && m_document->isPasswordWrong();

    if (m_titleLabel)
    {
        m_titleLabel->setText(tr("This document is protected"));
    }

    // The name of the file is a part of the translated sentence (its stress has the
    // token text/primary of the current theme, set again at a change of the theme)
    if (m_subtitle)
    {
        const QString name = QStringLiteral("<span style=\"font-weight:600; color:%1\">%2</span>")
                                 .arg(PDFThemeManager::getInstance()->getColor(PDFThemeColors::TextPrimary).name(), title.toHtmlEscaped());
        m_subtitle->setText(tr("Enter the password to open %1.").toHtmlEscaped().arg(name));
        m_subtitle->setToolTip(m_document ? QDir::toNativeSeparators(m_document->getFilePath()) : QString());
    }
    if (m_errorText)
    {
        m_errorText->setText(tr("Incorrect password. Check it and try again."));
    }
    if (m_passwordField)
    {
        m_passwordField->setPlaceholderText(tr("Password"));
        m_passwordField->setAccessibleName(tr("Password of %1").arg(title));
    }
    if (m_closeButton)
    {
        m_closeButton->setText(tr("Close tab"));
    }
    if (m_openButton)
    {
        m_openButton->setText(tr("Open document"));
    }
    if (m_loadingTitle)
    {
        m_loadingTitle->setText(tr("Opening %1…").arg(title));
    }

    if (m_statusBar)
    {
        switch (state)
        {
            case PDFAppDocumentState::Loading:
                m_statusBar->showMessage(tr("Opening…"));
                break;
            case PDFAppDocumentState::PasswordRequired:
                m_statusBar->showMessage(isWrong ? tr("Incorrect password") : tr("Password required"));
                break;
            case PDFAppDocumentState::Failed:
                m_statusBar->showMessage(m_document ? m_document->getErrorMessage() : QString());
                break;
            case PDFAppDocumentState::Ready:
                m_statusBar->showMessage(QString());
                break;
        }
    }
}

void PDFAppDocumentStateScreen::updateOpenButton()
{
    if (m_openButton)
    {
        const bool isPassword = m_document && m_document->getState() == PDFAppDocumentState::PasswordRequired;
        m_openButton->setEnabled(isPassword && m_passwordField && !m_passwordField->text().isEmpty());
    }
}

void PDFAppDocumentStateScreen::onPasswordEdited()
{
    // A new password hides the error of the last one
    if (m_passwordField && m_passwordField->isInvalid())
    {
        m_passwordField->setInvalid(false);
    }
    if (m_errorRow)
    {
        m_errorRow->hide();
    }
    updateOpenButton();
}

void PDFAppDocumentStateScreen::onOpenClicked()
{
    if (!m_passwordField || !m_openButton || !m_openButton->isEnabled())
    {
        return;
    }

    // The password leaves the field (it is not kept by the widget)
    const QString password = m_passwordField->text();
    m_passwordField->clear();
    updateOpenButton();
    Q_EMIT passwordEntered(password);
}

void PDFAppDocumentStateScreen::onCloseClicked()
{
    Q_EMIT closeRequested();
}

void PDFAppDocumentStateScreen::onLoadingDelayElapsed()
{
    m_isLoadingShown = m_document && m_document->getState() == PDFAppDocumentState::Loading;
    if (m_loadingPanel)
    {
        m_loadingPanel->setVisible(m_isLoadingShown);
    }
    onLoadingProgressChanged();
    update();
}

void PDFAppDocumentStateScreen::onLoadingProgressChanged()
{
    QProgressBar* progressBar = qobject_cast<QProgressBar*>(m_loadingBar.data());
    if (!progressBar || !m_document)
    {
        return;
    }

    const int progress = m_document->getLoadingProgress();
    if (progress < 0)
    {
        progressBar->setRange(0, 0);
    }
    else
    {
        progressBar->setRange(0, 100);
        progressBar->setValue(progress);
    }
}

void PDFAppDocumentStateScreen::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, rect(), PDFThemeColors::BgSubtle);
}

void PDFAppDocumentStateScreen::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppDocumentStateScreen::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The field takes the focus, when the screen is shown (the tab became current)
    if (m_passwordField && m_passwordCard && m_passwordCard->isVisibleTo(this) && m_passwordField->isEnabled())
    {
        m_passwordField->setFocus(Qt::OtherFocusReason);
    }
}

bool PDFAppDocumentStateScreen::eventFilter(QObject* watched, QEvent* event)
{
    // The body paints bg/subtle and the shadow 0 / 8 / 24 of the visible card
    // below it (the card is a child, it paints itself over the shadow)
    if (watched == m_body && event->type() == QEvent::Paint)
    {
        QPainter painter(m_body);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::fillRect(&painter, m_body->rect(), PDFThemeColors::BgSubtle);

        for (QWidget* card : std::initializer_list<QWidget*>{ m_passwordCard.data(), m_loadingPanel.data() })
        {
            if (card && card->isVisible())
            {
                PDFStylePainter::shadow(&painter, QRectF(card->geometry()), PDFStylePainter::Radii(PDFAppDocumentDialogsHelper::CARD_RADIUS), QPointF(0.0, 8.0), 24.0,
                                        PDFThemeManager::getInstance()->getColor(PDFThemeColors::EffectShadow));
            }
        }
    }
    return BaseClass::eventFilter(watched, event);
}

// ---------------------------------------------------------------- choice dialog

PDFAppDocumentChoiceDialog::PDFAppDocumentChoiceDialog(PDFThemeIcons::Badge badge, const QString& title, const QString& text, int width, QWidget* parent) :
    BaseClass(badge, true, title, text, width, parent)
{

}

PDFAppDocumentChoiceDialog::~PDFAppDocumentChoiceDialog() = default;

QPushButton* PDFAppDocumentChoiceDialog::addChoice(const QString& text, PDFAppDocumentChoice choice, bool isPrimary, bool isLeading, const QString& objectName)
{
    QPushButton* button = addActionButton(text, isPrimary, isLeading);
    button->setObjectName(objectName);
    connect(button, &QPushButton::clicked, this, &PDFAppDocumentChoiceDialog::onChoiceClicked);

    ChoiceButton choiceButton;
    choiceButton.button = button;
    choiceButton.choice = choice;
    m_buttons.push_back(choiceButton);
    return button;
}

QPushButton* PDFAppDocumentChoiceDialog::getChoiceButton(PDFAppDocumentChoice choice) const
{
    for (const ChoiceButton& button : m_buttons)
    {
        if (button.choice == choice && button.button)
        {
            return button.button.data();
        }
    }
    return nullptr;
}

void PDFAppDocumentChoiceDialog::onChoiceClicked()
{
    // The buttons do not identify themselves in the signal
    const QObject* clicked = sender();
    for (const ChoiceButton& button : m_buttons)
    {
        if (button.button && button.button.data() == clicked)
        {
            m_choice = button.choice;
            accept();
            return;
        }
    }
}

PDFAppDocumentChoice PDFAppDocumentChoiceDialog::run(PDFAppDocumentChoiceDialog* dialog)
{
    if (!dialog)
    {
        return PDFAppDocumentChoice::Cancel;
    }

    // The dialog runs its own event loop: the parent (a tab, the window) may be
    // deleted meanwhile, and the dialog with it
    QPointer<PDFAppDocumentChoiceDialog> guard(dialog);
    dialog->exec();
    if (!guard)
    {
        return PDFAppDocumentChoice::Cancel;
    }

    const PDFAppDocumentChoice choice = guard->getChoice();
    guard->deleteLater();
    return choice;
}

// -------------------------------------------------------------------- item list

PDFAppDocumentItemList::PDFAppDocumentItemList(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("itemList"));
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::NOTE);
    m_layout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(this, QMargins(12, 4, 12, 4), 0);
}

PDFAppDocumentItemList::~PDFAppDocumentItemList() = default;

void PDFAppDocumentItemList::addDivider()
{
    QFrame* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("divider"));
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    m_layout->addWidget(divider);
}

void PDFAppDocumentItemList::addItem(const QString& iconName, const QString& name, const QString& detail, const QString& status, bool isError)
{
    if (!m_layout)
    {
        return;
    }

    if (m_count > 0)
    {
        addDivider();
    }
    ++m_count;

    // row: icon 16, the texts (gap 2), the status; gap 10, paddings 8; the name
    // Medium 12 in a line of 16 px, the folder Regular 11 in a line of 15 px,
    // so the row is 49 px high (document-row 955:150708)
    QWidget* row = new QWidget(this);
    row->setObjectName(QStringLiteral("itemRow"));
    QHBoxLayout* rowLayout = PDFAppDocumentDialogsHelper::createLayout<QHBoxLayout>(row, QMargins(0, 8, 0, 8), 10);
    rowLayout->addWidget(PDFAppDocumentDialogsHelper::createIcon(iconName, 16, PDFThemeColors::IconMuted, row), 0, Qt::AlignVCenter);

    // The texts take the rest of the row and are elided (they never widen the row)
    QWidget* texts = new QWidget(row);
    texts->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QVBoxLayout* textsLayout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(texts, QMargins(), 2);
    PDFAppElidedLabel* nameLabel = new PDFAppElidedLabel(PDFThemeFonts::getFont(QFont::Medium, 12), PDFThemeColors::TextPrimary, PDFComponentPainter::TextKind::FileName, texts);
    nameLabel->setObjectName(QStringLiteral("itemName"));
    nameLabel->setText(name.left(1024));
    nameLabel->setFixedHeight(16);
    textsLayout->addWidget(nameLabel);
    if (!detail.isEmpty())
    {
        PDFAppElidedLabel* detailLabel = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular), PDFThemeColors::TextMuted, PDFComponentPainter::TextKind::Data, texts);
        detailLabel->setObjectName(QStringLiteral("itemDetail"));
        detailLabel->setText(detail.left(1024));
        detailLabel->setFixedHeight(15);
        textsLayout->addWidget(detailLabel);
    }
    rowLayout->addWidget(texts, 1);

    if (!status.isEmpty())
    {
        // One line at the trailing side (a long reason is elided, its whole text is the tooltip)
        PDFAppElidedLabel* statusLabel = new PDFAppElidedLabel(PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), isError ? PDFThemeColors::TextDanger : PDFThemeColors::TextSecondary,
                                                               PDFComponentPainter::TextKind::Interface, row);
        statusLabel->setObjectName(QStringLiteral("itemStatus"));
        statusLabel->setText(status.left(1024));
        statusLabel->setFixedWidth(qBound(0, qCeil(PDFStylePainter::textWidth(statusLabel->text(), statusLabel->font())), STATUS_WIDTH));
        statusLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        statusLabel->setFixedHeight(16);
        rowLayout->addWidget(statusLabel, 0);
    }

    m_layout->addWidget(row);
}

void PDFAppDocumentItemList::addMoreRow(int count)
{
    if (!m_layout || count <= 0)
    {
        return;
    }

    addDivider();
    PDFLabel* more = PDFAppPageWidgets::createLabel(tr("… and %1 more").arg(count), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, this);
    more->setObjectName(QStringLiteral("moreRow"));
    more->setContentsMargins(0, 8, 0, 8);
    m_layout->addWidget(more);
}

// --------------------------------------------------------------- close-unsaved

PDFAppCloseUnsavedDialog::PDFAppCloseUnsavedDialog(const PDFAppDocument* document, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, tr("Save the changes to %1?").arg(document ? document->getTitle() : QString()), getText(document),
              PDFAppDocumentDialogsHelper::WIDTH, parent)
{
    setObjectName(QStringLiteral("closeUnsavedDialog"));

    // Don't Save at the leading edge, Cancel next to the primary Save
    addChoice(tr("Don’t Save"), PDFAppDocumentChoice::Discard, false, true, QStringLiteral("dontSaveButton"));
    addChoice(tr("Cancel"), PDFAppDocumentChoice::Cancel, false, false, QStringLiteral("cancelButton"));
    addChoice(tr("Save"), PDFAppDocumentChoice::Save, true, false, QStringLiteral("saveButton"));
}

QString PDFAppCloseUnsavedDialog::getText(const PDFAppDocument* document)
{
    const int count = document ? document->getUnsavedChangeCount() : -1;
    if (document && document->isCopy())
    {
        return tr("The copy has not been saved to a file yet. If you close the tab without saving, it is lost.");
    }
    if (count > 0)
    {
        return PDFAppDocumentDialogsHelper::getCountText(count, tr("The document has 1 unsaved change. If you close the tab without saving, the change is lost."),
                                                         tr("The document has %1 unsaved changes. If you close the tab without saving, the changes are lost."));
    }
    return tr("The document has unsaved changes. If you close the tab without saving, the changes are lost.");
}

PDFAppCloseUnsavedDialog::~PDFAppCloseUnsavedDialog() = default;

QString PDFAppCloseUnsavedDialog::getChangesText(const PDFAppDocument* document)
{
    if (!document)
    {
        return QString();
    }
    if (document->isCopy())
    {
        return tr("Not saved to a file yet");
    }

    const int count = document->getUnsavedChangeCount();
    if (count > 0)
    {
        return PDFAppDocumentDialogsHelper::getCountText(count, tr("1 unsaved change"), tr("%1 unsaved changes"));
    }
    return tr("Unsaved changes");
}

// --------------------------------------------------------------- close-running

PDFAppCloseRunningDialog::PDFAppCloseRunningDialog(const PDFAppDocument* document, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, getTitle(document), getText(document), PDFAppDocumentDialogsHelper::WIDTH, parent)
{
    setObjectName(QStringLiteral("closeRunningDialog"));

    addChoice(tr("Stop and Close"), PDFAppDocumentChoice::Stop, false, true, QStringLiteral("stopAndCloseButton"));
    addChoice(tr("Keep Running"), PDFAppDocumentChoice::Keep, true, false, QStringLiteral("keepRunningButton"));
}

const PDFAppDocumentOperation* PDFAppCloseRunningDialog::getOperation(const PDFAppDocument* document, int* count)
{
    // The first operation, which the user is asked about, names the dialog
    const PDFAppDocumentOperation* operation = nullptr;
    int operationCount = 0;
    if (document)
    {
        for (const PDFAppDocumentOperation* item : document->getOperations())
        {
            if (item->isAskedBeforeClose())
            {
                operation = operation ? operation : item;
                ++operationCount;
            }
        }
    }
    if (count)
    {
        *count = operationCount;
    }
    return operation;
}

QString PDFAppCloseRunningDialog::getTitle(const PDFAppDocument* document)
{
    const PDFAppDocumentOperation* operation = getOperation(document, nullptr);
    return operation ? tr("Stop %1 and close the tab?").arg(operation->getText().left(256)) : tr("Stop the operation and close the tab?");
}

QString PDFAppCloseRunningDialog::getText(const PDFAppDocument* document)
{
    int count = 0;
    const PDFAppDocumentOperation* operation = getOperation(document, &count);
    const QString operationText = operation ? operation->getText().left(256) : tr("An operation");
    const QString title = document ? document->getTitle() : QString();

    QString text = operation && operation->getProgress() >= 0 ? tr("%1 is still running on %2 (%3 % done).").arg(operationText, title).arg(operation->getProgress())
                                                              : tr("%1 is still running on %2.").arg(operationText, title);
    if (count > 1)
    {
        text += QLatin1Char(' ') + PDFAppDocumentDialogsHelper::getCountText(count - 1, tr("1 more operation is running."), tr("%1 more operations are running."));
    }
    text += QLatin1Char(' ') + tr("Closing the tab stops it. The document and the file stay unchanged.");
    return text;
}

PDFAppCloseRunningDialog::~PDFAppCloseRunningDialog() = default;

// ---------------------------------------------------------------- quit-unsaved

PDFAppQuitUnsavedDialog::PDFAppQuitUnsavedDialog(const std::vector<const PDFAppDocument*>& documents, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, tr("Save the changes before quitting?"),
              PDFAppDocumentDialogsHelper::getCountText(int(documents.size()), tr("1 document has unsaved changes. If you quit without saving, the changes are lost."),
                                                        tr("%1 documents have unsaved changes. If you quit without saving, the changes are lost.")),
              PDFAppDocumentDialogsHelper::LIST_WIDTH, parent)
{
    setObjectName(QStringLiteral("quitUnsavedDialog"));

    m_list = new PDFAppDocumentItemList(this);
    int index = 0;
    for (const PDFAppDocument* document : documents)
    {
        if (!document)
        {
            continue;
        }
        if (index++ >= PDFAppDocumentItemList::MAXIMUM_ROWS)
        {
            m_list->addMoreRow(int(documents.size()) - PDFAppDocumentItemList::MAXIMUM_ROWS);
            break;
        }

        const QString folder = document->getFilePath().isEmpty() ? tr("Not saved to a file yet") : PDFAppDocumentDialogsHelper::getFolderText(document->getFilePath());
        m_list->addItem(QStringLiteral("file"), document->getTitle(), folder, PDFAppCloseUnsavedDialog::getChangesText(document), false);
    }
    addContent(m_list);

    addChoice(tr("Quit Without Saving"), PDFAppDocumentChoice::Discard, false, true, QStringLiteral("quitWithoutSavingButton"));
    addChoice(tr("Cancel"), PDFAppDocumentChoice::Cancel, false, false, QStringLiteral("cancelButton"));
    addChoice(tr("Save All"), PDFAppDocumentChoice::Save, true, false, QStringLiteral("saveAllButton"));
}

PDFAppQuitUnsavedDialog::~PDFAppQuitUnsavedDialog() = default;

// ----------------------------------------------------------------- open-failed

PDFAppOpenFailedDialog::PDFAppOpenFailedDialog(const std::vector<PDFAppOpenFailure>& failures, const QString& openedTitle, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Critical, true, getTitle(failures),
              openedTitle.isEmpty() ? tr("These items were skipped:") : tr("%1 is open; these items were skipped:").arg(openedTitle.left(256)),
              PDFAppDocumentDialogsHelper::LIST_WIDTH, parent),
    m_failures(failures)
{
    setObjectName(QStringLiteral("openFailedDialog"));

    m_list = new PDFAppDocumentItemList(this);
    int index = 0;
    for (const PDFAppOpenFailure& failure : m_failures)
    {
        if (index++ >= PDFAppDocumentItemList::MAXIMUM_ROWS)
        {
            m_list->addMoreRow(int(m_failures.size()) - PDFAppDocumentItemList::MAXIMUM_ROWS);
            break;
        }

        if (failure.isOption)
        {
            m_list->addItem(QStringLiteral("terminal"), failure.name, tr("Option of the command line"), failure.reason, true);
        }
        else
        {
            const QFileInfo fileInfo(failure.name);
            m_list->addItem(QStringLiteral("file"), fileInfo.fileName().isEmpty() ? failure.name : fileInfo.fileName(),
                            PDFAppDocumentDialogsHelper::getFolderText(failure.name), failure.reason, true);
        }
    }
    addContent(m_list);

    // Copy Details at the leading edge, Close (Primary) at the trailing edge
    m_copyButton = addActionButton(tr("Copy Details"), false, true);
    m_copyButton->setObjectName(QStringLiteral("copyDetailsButton"));
    m_closeButton = addActionButton(tr("Close"), true, false);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    connect(m_copyButton.data(), &QPushButton::clicked, this, &PDFAppOpenFailedDialog::onCopyClicked);
    connect(m_closeButton.data(), &QPushButton::clicked, this, &PDFAppOpenFailedDialog::onCloseClicked);
}

PDFAppOpenFailedDialog::~PDFAppOpenFailedDialog() = default;

QString PDFAppOpenFailedDialog::getTitle(const std::vector<PDFAppOpenFailure>& failures)
{
    int files = 0;
    int options = 0;
    for (const PDFAppOpenFailure& failure : failures)
    {
        (failure.isOption ? options : files) += 1;
    }

    const QString filesText = PDFAppDocumentDialogsHelper::getCountText(files, tr("1 file"), tr("%1 files"));
    const QString optionsText = PDFAppDocumentDialogsHelper::getCountText(options, tr("1 option"), tr("%1 options"));
    if (files > 0 && options > 0)
    {
        return tr("%1 and %2 could not be used").arg(filesText, optionsText);
    }
    if (options > 0)
    {
        return PDFAppDocumentDialogsHelper::getCountText(options, tr("1 option of the command line could not be used"), tr("%1 options of the command line could not be used"));
    }
    return PDFAppDocumentDialogsHelper::getCountText(files, tr("1 file could not be opened"), tr("%1 files could not be opened"));
}

QString PDFAppOpenFailedDialog::getDetails(const std::vector<PDFAppOpenFailure>& failures)
{
    QStringList lines;
    for (const PDFAppOpenFailure& failure : failures)
    {
        const QString name = failure.isOption ? failure.name : QDir::toNativeSeparators(failure.name);
        lines << (failure.reason.isEmpty() ? name : tr("%1 – %2").arg(name, failure.reason));
    }
    return lines.join(QLatin1Char('\n'));
}

void PDFAppOpenFailedDialog::onCopyClicked()
{
    if (QClipboard* clipboard = QApplication::clipboard())
    {
        clipboard->setText(getDetails(m_failures));
    }
}

void PDFAppOpenFailedDialog::onCloseClicked()
{
    accept();
}

// ---------------------------------------------------------------- lock popover

PDFAppLockPopover::PDFAppLockPopover(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("lockPopover"));
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::POPOVER);

    // The panel of the menu has the border 1 and the margins 6 of the style: the
    // content keeps the paddings 16 / 14 of popover-locked 266:12435
    QWidget* content = new QWidget(this);
    content->setObjectName(QStringLiteral("lockPopoverContent"));
    content->setFixedWidth(WIDTH - 14);
    QVBoxLayout* layout = PDFAppDocumentDialogsHelper::createLayout<QVBoxLayout>(content, QMargins(10, 8, 10, 8), 12);

    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Document is locked"), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, content);
    title->setObjectName(QStringLiteral("lockTitle"));
    layout->addWidget(title);

    PDFAppParagraph* text = PDFAppPageWidgets::createText(tr("Editing is turned off so the file is not changed by accident. Unlock it to edit the file, or edit a copy and keep the original locked."),
                                                        PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 17, PDFThemeColors::TextSecondary, content);
    text->setObjectName(QStringLiteral("lockText"));
    m_text = text;

    // The menu sizes the content by its size hint: the wrapped text has its height for the width
    const int textWidth = WIDTH - 14 - 20;
    text->setFixedSize(textWidth, text->heightForWidth(textWidth));
    layout->addWidget(text);

    QWidget* actions = new QWidget(content);
    QHBoxLayout* actionsLayout = PDFAppDocumentDialogsHelper::createLayout<QHBoxLayout>(actions, QMargins(), 8);
    actionsLayout->addStretch(1);
    m_unlockButton = PDFAppPageWidgets::createButton(tr("Unlock"), QString(), false, actions);
    m_unlockButton->setIcon(QIcon());
    m_unlockButton->setObjectName(QStringLiteral("unlockButton"));
    m_editCopyButton = PDFAppPageWidgets::createButton(tr("Edit a Copy"), QStringLiteral("copy"), true, actions);
    m_editCopyButton->setObjectName(QStringLiteral("editCopyButton"));
    actionsLayout->addWidget(m_unlockButton);
    actionsLayout->addWidget(m_editCopyButton);
    layout->addWidget(actions);
    connect(m_unlockButton.data(), &QPushButton::clicked, this, &PDFAppLockPopover::onUnlockClicked);
    connect(m_editCopyButton.data(), &QPushButton::clicked, this, &PDFAppLockPopover::onEditCopyClicked);

    QWidgetAction* action = new QWidgetAction(this);
    action->setDefaultWidget(content);
    addAction(action);

    // One showing: a hidden popover (Esc, a click outside, a button) is deleted
    connect(this, &QMenu::aboutToHide, this, &QObject::deleteLater);
}

PDFAppLockPopover::~PDFAppLockPopover() = default;

void PDFAppLockPopover::setPagedXFA(bool isPagedXFA)
{
    // A copy of a display version of an XFA form is a display version too, so
    // Edit a Copy is not offered (as the command Edit a Copy of the File panel)
    if (m_editCopyButton)
    {
        m_editCopyButton->setVisible(!isPagedXFA);
    }

    if (m_text && isPagedXFA)
    {
        m_text->setText(tr("Editing is turned off so the file is not changed by accident. The pages of this XFA form are shown as laid out, so the document cannot be edited, also when it is unlocked."));
        const int textWidth = m_text->width();
        m_text->setFixedSize(textWidth, m_text->heightForWidth(textWidth));
    }
}

void PDFAppLockPopover::showBelow(const QRect& anchor)
{
    // The panel starts 24 px before the middle of the lock (the caret of the frame
    // points to the lock) and 6 px below the tab
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QPoint position(isRightToLeft ? anchor.center().x() + 24 : anchor.center().x() - 24, anchor.bottom() + 1 + 6);
    setProperty(PDFWidgetStyleProperties::POPUP_ANCHOR, QRect(QPoint(position.x() - (isRightToLeft ? 1 : 0), anchor.top()), QSize(1, position.y() - anchor.top())));
    popup(position);

    if (m_editCopyButton && !m_editCopyButton->isHidden())
    {
        m_editCopyButton->setFocus(Qt::PopupFocusReason);
    }
    else if (m_unlockButton)
    {
        m_unlockButton->setFocus(Qt::PopupFocusReason);
    }
}

void PDFAppLockPopover::onUnlockClicked()
{
    // The popover closes first (its slots may delete it)
    QPointer<PDFAppLockPopover> guard(this);
    close();
    if (guard)
    {
        Q_EMIT unlockRequested();
    }
}

void PDFAppLockPopover::onEditCopyClicked()
{
    QPointer<PDFAppLockPopover> guard(this);
    close();
    if (guard)
    {
        Q_EMIT editCopyRequested();
    }
}

}   // namespace pdf
