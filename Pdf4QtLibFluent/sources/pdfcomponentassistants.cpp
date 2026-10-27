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

#include "pdfcomponentassistants.h"
#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QChildEvent>
#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegion>
#include <QScrollBar>
#include <QTextDocument>
#include <QToolButton>

namespace pdf
{

/// Common code of the widgets of the page AI
class PDFComponentAssistantsHelper
{
public:
    PDFComponentAssistantsHelper() = delete;

    /// Returns the state of a painted widget (enabled, direction)
    static PDFComponentPainter::State state(const QWidget* widget)
    {
        return PDFComponentPainter::State(widget->isEnabled() ? QStyle::State_Enabled : QStyle::State_None, widget->layoutDirection());
    }

    /// Returns true, if the change of the widget changes its picture or its geometry
    static bool isRepaintChange(const QEvent* event)
    {
        switch (event->type())
        {
            case QEvent::EnabledChange:
            case QEvent::LayoutDirectionChange:
            case QEvent::FontChange:
            case QEvent::StyleChange:
                return true;

            default:
                break;
        }
        return false;
    }

    /// Returns true, if the application has hidden the nested widget (the widget
    /// of a hidden parent is not hidden explicitly)
    static bool isHiddenByApplication(const QWidget* widget)
    {
        return widget->testAttribute(Qt::WA_WState_ExplicitShowHide) && widget->testAttribute(Qt::WA_WState_Hidden);
    }

    /// Returns the valid avatar (the hue Neutral for an invalid hue, the extents bounded)
    static PDFComponentPainter::AssistantAvatar normalized(PDFComponentPainter::AssistantAvatar avatar)
    {
        avatar.hue = PDFComponentPainter::normalized(avatar.hue);
        avatar.extent = qBound(0, avatar.extent, 1024);
        avatar.fontPixelSize = qBound(1, avatar.fontPixelSize, 1024);
        return avatar;
    }
};

/// Text of a code box: the plain text editor of the library without the margins
/// of its viewport (the box has its own paddings). Without a meta object of its
/// own: it is a PDFPlainTextEdit for the application and the tests.
class PDFCodeBoxTextEdit : public PDFPlainTextEdit
{
public:
    explicit PDFCodeBoxTextEdit(QWidget* parent) :
        PDFPlainTextEdit(parent)
    {
        setViewportMargins(0, 0, 0, 0);
    }
};

// ------------------------------------------------------------- Assistant avatar

PDFAssistantAvatar::PDFAssistantAvatar(QWidget* parent) :
    BaseClass(parent)
{
    // A picture: the mouse goes to the widget below it
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFAssistantAvatar::PDFAssistantAvatar(const PDFComponentPainter::AssistantAvatar& avatar, QWidget* parent) :
    PDFAssistantAvatar(parent)
{
    setAvatar(avatar);
}

void PDFAssistantAvatar::setAvatar(const PDFComponentPainter::AssistantAvatar& avatar)
{
    m_avatar = PDFComponentAssistantsHelper::normalized(avatar);
    updateGeometry();
    update();
}

QSize PDFAssistantAvatar::sizeHint() const
{
    return PDFComponentPainter::getAssistantAvatarSize(m_avatar);
}

QSize PDFAssistantAvatar::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAssistantAvatar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintAssistantAvatar(&painter, QRectF(rect()), m_avatar, PDFComponentAssistantsHelper::state(this));
}

void PDFAssistantAvatar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentAssistantsHelper::isRepaintChange(event))
    {
        update();
    }
}

// ------------------------------------------------------------------ Client row

PDFAssistantRow::PDFAssistantRow(QWidget* parent) :
    BaseClass(parent),
    m_disconnectButton(new PDFIconButton(QStringLiteral("unplug"), PDFIconButton::Size::Medium, this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_row.paintNested = false;

    m_disconnectButton->installEventFilter(this);
    connect(m_disconnectButton.data(), &QAbstractButton::clicked, this, &PDFAssistantRow::onDisconnectButtonClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));

    updateButtonTexts();
    updateAccessibility();
}

PDFAssistantRow::PDFAssistantRow(const PDFComponentPainter::AssistantRow& row, QWidget* parent) :
    PDFAssistantRow(parent)
{
    setRow(row);
}

PDFAssistantRow::~PDFAssistantRow()
{
    // The button is deleted with the children of QWidget, after this part of the row
    if (PDFIconButton* button = getOwnButton())
    {
        button->removeEventFilter(this);
    }
}

void PDFAssistantRow::setRow(const PDFComponentPainter::AssistantRow& row)
{
    m_row = row;
    m_row.avatar = PDFComponentAssistantsHelper::normalized(row.avatar);
    m_row.paintNested = false;
    m_row.disconnectVisible = true;

    updateAccessibility();
    updateButtonTexts();
    updateGeometry();
    layoutButton();
    update();
}

PDFIconButton* PDFAssistantRow::getOwnButton() const
{
    PDFIconButton* button = m_disconnectButton.data();
    return (button && button->parentWidget() == this) ? button : nullptr;
}

void PDFAssistantRow::setDisconnectToolTip(const QString& toolTip)
{
    m_disconnectToolTip = toolTip;
    m_isDisconnectToolTipSet = true;
    updateButtonTexts();
}

QString PDFAssistantRow::getDisconnectToolTip() const
{
    return m_isDisconnectToolTipSet ? m_disconnectToolTip : tr("Disconnect");
}

void PDFAssistantRow::setDisconnectVisible(bool isVisible)
{
    if (PDFIconButton* button = getOwnButton())
    {
        // Show / hide of the button relayouts the row (the event filter)
        button->setVisible(isVisible);
    }
}

bool PDFAssistantRow::isDisconnectVisible() const
{
    const PDFIconButton* button = getOwnButton();
    return button && !PDFComponentAssistantsHelper::isHiddenByApplication(button);
}

void PDFAssistantRow::updateButtonTexts()
{
    if (PDFIconButton* button = getOwnButton())
    {
        // The name says the action, the description the assistant, which it disconnects
        const QString toolTip = getDisconnectToolTip();
        button->setToolTip(toolTip);
        button->setAccessibleName(toolTip);
        button->setAccessibleDescription(m_row.name);
    }
}

void PDFAssistantRow::updateAccessibility()
{
    // "Claude Code" with the description "This computer · connected at 14:02, Remote";
    // the texts set by the application are kept
    QStringList parts;
    for (const QString& part : { m_row.meta, m_row.tag })
    {
        if (!part.isEmpty())
        {
            parts << part;
        }
    }
    const QString description = parts.join(tr(", "));

    if (accessibleName().isEmpty() || accessibleName() == m_name)
    {
        setAccessibleName(m_row.name);
    }
    if (accessibleDescription().isEmpty() || accessibleDescription() == m_description)
    {
        setAccessibleDescription(description);
    }

    m_name = m_row.name;
    m_description = description;
}

PDFComponentPainter::AssistantRow PDFAssistantRow::getPaintedRow() const
{
    PDFComponentPainter::AssistantRow row = m_row;
    row.paintNested = false;
    row.disconnectVisible = isDisconnectVisible();
    return row;
}

PDFComponentPainter::State PDFAssistantRow::getState() const
{
    return PDFComponentAssistantsHelper::state(this);
}

void PDFAssistantRow::layoutButton()
{
    PDFIconButton* button = getOwnButton();
    if (!button)
    {
        return;
    }

    const QRectF buttonRect = PDFComponentPainter::getAssistantRowDisconnectRect(QRectF(rect()), getPaintedRow(), getState());
    if (!buttonRect.isEmpty())
    {
        button->setGeometry(buttonRect.toAlignedRect());
    }
}

void PDFAssistantRow::onDisconnectButtonClicked()
{
    // Nothing of the row is used after the signal (a slot may delete it)
    Q_EMIT disconnectClicked();
}

QSize PDFAssistantRow::sizeHint() const
{
    return PDFComponentPainter::getAssistantRowSize(getPaintedRow());
}

QSize PDFAssistantRow::minimumSizeHint() const
{
    return PDFComponentPainter::getAssistantRowMinimumSize(getPaintedRow());
}

void PDFAssistantRow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintAssistantRow(&painter, QRectF(rect()), getPaintedRow(), getState());
}

void PDFAssistantRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentAssistantsHelper::isRepaintChange(event))
    {
        updateGeometry();
        layoutButton();
        update();
    }
    else if (event->type() == QEvent::LanguageChange)
    {
        updateButtonTexts();
        updateAccessibility();
    }
}

void PDFAssistantRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutButton();
}

bool PDFAssistantRow::event(QEvent* event)
{
    if (event->type() == QEvent::ChildRemoved)
    {
        // The button deleted or moved away by the application gives its place to the texts
        const bool result = BaseClass::event(event);
        updateGeometry();
        update();
        return result;
    }

    return BaseClass::event(event);
}

bool PDFAssistantRow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_disconnectButton.data() && (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent))
    {
        // The button shown or hidden (by the row or by the application) takes or frees its place
        updateGeometry();
        layoutButton();
        update();
    }

    return BaseClass::eventFilter(watched, event);
}

// ---------------------------------------------------------------- Activity row

PDFActivityRow::PDFActivityRow(QWidget* parent) :
    BaseClass(parent)
{
    // The height follows the width (the status wraps)
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);

    // Not clickable by default: a text without the focus, the mouse passes through it
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, false);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    updateDescription();
}

PDFActivityRow::PDFActivityRow(const PDFComponentPainter::ActivityRow& row, QWidget* parent) :
    PDFActivityRow(parent)
{
    setRow(row);
}

void PDFActivityRow::setRow(const PDFComponentPainter::ActivityRow& row)
{
    m_row = row;
    m_row.hue = PDFComponentPainter::normalized(row.hue);
    m_row.statusTone = PDFComponentPainter::normalized(row.statusTone);
    setText(row.title);
    updateDescription();
    invalidateHeight();
}

PDFComponentPainter::ActivityRow PDFActivityRow::getRow() const
{
    PDFComponentPainter::ActivityRow row = m_row;
    row.title = text();
    return row;
}

void PDFActivityRow::setClickable(bool isClickable)
{
    if (m_isClickable == isClickable)
    {
        return;
    }

    m_isClickable = isClickable;
    setFocusPolicy(isClickable ? Qt::StrongFocus : Qt::NoFocus);
    setAttribute(Qt::WA_Hover, isClickable);
    setAttribute(Qt::WA_TransparentForMouseEvents, !isClickable);

    if (isClickable)
    {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        unsetCursor();
        setDown(false);

        // A text takes no focus
        if (hasFocus())
        {
            clearFocus();
        }
    }

    update();
}

void PDFActivityRow::updateDescription()
{
    // "16:44, Claude Code · Q3_Forecast.pdf, Waiting for your confirmation · Show"
    const QString description = joinTexts({ m_row.time, m_row.meta, m_row.status + m_row.linkText }, getListPattern());
    replaceAccessibleDescription(m_description, description);
    m_description = description;
}

void PDFActivityRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateDescription();
    }
}

int PDFActivityRow::computeHeightForWidth(int width) const
{
    // Cached by PDFComponentButton (setRow invalidates it, the title is the text)
    return PDFComponentPainter::getActivityRowHeight(getRow(), width);
}

void PDFActivityRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintActivityRow(painter, QRectF(rect), getRow(), state);
}

QSize PDFActivityRow::getContentSizeHint() const
{
    return PDFComponentPainter::getActivityRowSize(getRow());
}

QSize PDFActivityRow::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getActivityRowMinimumSize(getRow());
}

// -------------------------------------------------------------- Assistant tile

PDFAssistantTile::PDFAssistantTile(QWidget* parent) :
    BaseClass(parent)
{
    // A static choice of an exclusive group of the application; the width follows the layout
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setSelectionBehavior(SelectionBehavior::ExclusiveStatic);
}

PDFAssistantTile::PDFAssistantTile(const PDFComponentPainter::AssistantTile& tile, QWidget* parent) :
    PDFAssistantTile(parent)
{
    setTile(tile);
}

void PDFAssistantTile::setTile(const PDFComponentPainter::AssistantTile& tile)
{
    m_tile = tile;
    m_tile.logo = PDFComponentAssistantsHelper::normalized(tile.logo);
    setText(tile.name);
    updateDescription();
    updateGeometry();
    update();
}

PDFComponentPainter::AssistantTile PDFAssistantTile::getTile() const
{
    PDFComponentPainter::AssistantTile tile = m_tile;
    tile.name = text();
    return tile;
}

void PDFAssistantTile::setSelected(bool isSelected)
{
    if (!isCheckable())
    {
        return;
    }

    if (isSelected)
    {
        setChecked(true);
    }
    else
    {
        uncheck();
    }
}

void PDFAssistantTile::updateDescription()
{
    replaceAccessibleDescription(m_description, m_tile.vendor);
    m_description = m_tile.vendor;
}

void PDFAssistantTile::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    // The focus ring of the style replaces the border of an unselected tile
    PDFComponentPainter::paintAssistantTile(painter, QRectF(rect), getTile(), getBorderState(state));
}

QSize PDFAssistantTile::getContentSizeHint() const
{
    return PDFComponentPainter::getAssistantTileSize(getTile());
}

QSize PDFAssistantTile::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getAssistantTileMinimumSize(getTile());
}

// -------------------------------------------------------------------- Code box

PDFCodeBox::PDFCodeBox(QWidget* parent) :
    BaseClass(parent),
    m_editor(new PDFCodeBoxTextEdit(this)),
    m_copyButton(new QToolButton(this))
{
    // The height follows the width (the text wraps)
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    m_box.paintNested = false;

    // The text: read only, selectable by the mouse and the keyboard, wrapped at
    // any character, no frame and no scroll bars (the box shows all its lines)
    m_editor->setObjectName(QStringLiteral("codeText"));
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setReadOnly(true);
    m_editor->setUndoRedoEnabled(false);
    m_editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_editor->setFocusPolicy(Qt::StrongFocus);
    m_editor->setTabChangesFocus(true);
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_editor->document()->setDocumentMargin(0.0);
    PDFThemeWidgetHelper::bindTextPalette(m_editor.data(), PDFThemeColors::TextPrimary);
    m_editor->installEventFilter(this);
    connect(m_editor->horizontalScrollBar(), &QAbstractSlider::rangeChanged, this, &PDFCodeBox::onEditorScrollRangeChanged);
    connect(m_editor->verticalScrollBar(), &QAbstractSlider::rangeChanged, this, &PDFCodeBox::onEditorScrollRangeChanged);
    updateEditorFont();

    // The copy: the Toolbar button 190:538 of the library (autoRaise, icon 20, 36 x 36)
    m_copyButton->setObjectName(QStringLiteral("copyButton"));
    m_copyButton->setAutoRaise(true);
    m_copyButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("copy")));
    m_copyButton->setIconSize(QSize(20, 20));
    m_copyButton->setFixedSize(36, 36);
    m_copyButton->setFocusPolicy(Qt::TabFocus);
    connect(m_copyButton.data(), &QAbstractButton::clicked, this, &PDFCodeBox::onCopyButtonClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));

    updateButtonTexts();
}

PDFCodeBox::PDFCodeBox(const QString& text, QWidget* parent) :
    PDFCodeBox(parent)
{
    setText(text);
}

PDFCodeBox::~PDFCodeBox()
{
    // The editor is deleted with the children of QWidget, after this part of the box
    if (PDFPlainTextEdit* editor = getOwnEditor())
    {
        editor->removeEventFilter(this);
    }
}

PDFPlainTextEdit* PDFCodeBox::getOwnEditor() const
{
    PDFPlainTextEdit* editor = m_editor.data();
    return (editor && editor->parentWidget() == this) ? editor : nullptr;
}

QToolButton* PDFCodeBox::getOwnButton() const
{
    QToolButton* button = m_copyButton.data();
    return (button && button->parentWidget() == this) ? button : nullptr;
}

void PDFCodeBox::setText(const QString& text)
{
    if (m_box.text == text)
    {
        return;
    }

    m_box.text = text;
    m_cachedWidth = -1;
    updateEditorText();
    forwardAccessibleName();
    updateGeometry();
    layoutChildren();
    update();
}

void PDFCodeBox::setCopyText(const QString& text)
{
    m_copyText = text;
}

QString PDFCodeBox::getCopyText() const
{
    return m_copyText.isEmpty() ? m_box.text : m_copyText;
}

void PDFCodeBox::setMonospace(bool isMonospace)
{
    if (m_box.monospace == isMonospace)
    {
        return;
    }

    m_box.monospace = isMonospace;
    m_cachedWidth = -1;
    updateEditorFont();
    updateGeometry();
    layoutChildren();
    update();
}

QAbstractButton* PDFCodeBox::getCopyButton() const
{
    return m_copyButton.data();
}

void PDFCodeBox::setCopyToolTip(const QString& toolTip)
{
    m_copyToolTip = toolTip;
    m_isCopyToolTipSet = true;
    updateButtonTexts();
}

QString PDFCodeBox::getCopyToolTip() const
{
    return m_isCopyToolTipSet ? m_copyToolTip : tr("Copy");
}

void PDFCodeBox::copy()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(getCopyText());
    }

    // Nothing of the box is used after the signal (a slot may delete it)
    Q_EMIT copied();
}

void PDFCodeBox::onCopyButtonClicked()
{
    copy();
}

void PDFCodeBox::onEditorScrollRangeChanged()
{
    if (PDFPlainTextEdit* editor = getOwnEditor())
    {
        for (QScrollBar* scrollBar : { editor->horizontalScrollBar(), editor->verticalScrollBar() })
        {
            if (scrollBar && (scrollBar->minimum() != 0 || scrollBar->maximum() != 0))
            {
                scrollBar->setRange(0, 0);
            }
        }
    }
}

void PDFCodeBox::updateEditorText()
{
    if (PDFPlainTextEdit* editor = getOwnEditor())
    {
        // The line breaks of all systems, as the painter of the box counts them
        QString text = m_box.text;
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        editor->setPlainText(text);
    }
}

void PDFCodeBox::updateEditorFont()
{
    if (PDFPlainTextEdit* editor = getOwnEditor())
    {
        editor->setFont(PDFComponentPainter::getCodeBoxFont(m_box.monospace));
    }
}

void PDFCodeBox::updateButtonTexts()
{
    if (QToolButton* button = getOwnButton())
    {
        const QString toolTip = getCopyToolTip();
        button->setToolTip(toolTip);
        button->setAccessibleName(toolTip);
    }
}

void PDFCodeBox::forwardAccessibleName()
{
    // The accessible name of the box names its text (the focus goes to the text);
    // a name set by the application to the text itself is kept
    PDFPlainTextEdit* editor = getOwnEditor();
    if (!editor)
    {
        return;
    }

    const QString name = accessibleName();
    if (editor->accessibleName().isEmpty() || editor->accessibleName() == m_forwardedName)
    {
        editor->setAccessibleName(name);
        m_forwardedName = name;
    }
}

PDFComponentPainter::CodeBox PDFCodeBox::getPaintedBox() const
{
    // A text deleted by the application is drawn by the box
    PDFComponentPainter::CodeBox box = m_box;
    box.paintNested = getOwnEditor() == nullptr;
    return box;
}

PDFComponentPainter::State PDFCodeBox::getState() const
{
    PDFComponentPainter::State state = PDFComponentAssistantsHelper::state(this);
    const PDFPlainTextEdit* editor = getOwnEditor();
    if (editor && editor->hasFocus())
    {
        // The text has the focus: the ring of a field inside of the box
        state.flags |= QStyle::State_HasFocus;
    }
    return state;
}

void PDFCodeBox::layoutChildren()
{
    const PDFComponentPainter::State state = PDFComponentAssistantsHelper::state(this);
    const QRectF boxRect(rect());

    if (PDFPlainTextEdit* editor = getOwnEditor())
    {
        // One pixel more than the lines: the editor never scrolls by a line
        const QRect textRect = PDFComponentPainter::getCodeBoxTextRect(boxRect, m_box, state).toAlignedRect();
        if (!textRect.isEmpty())
        {
            editor->setGeometry(textRect.adjusted(0, 0, 0, 1));
        }
    }

    if (QToolButton* button = getOwnButton())
    {
        const QRect buttonRect = PDFComponentPainter::getCodeBoxButtonRect(boxRect, state).toAlignedRect();
        if (!buttonRect.isEmpty())
        {
            button->move(buttonRect.topLeft());
        }
    }
}

QSize PDFCodeBox::sizeHint() const
{
    return PDFComponentPainter::getCodeBoxSize(m_box);
}

QSize PDFCodeBox::minimumSizeHint() const
{
    return PDFComponentPainter::getCodeBoxMinimumSize(m_box);
}

int PDFCodeBox::heightForWidth(int width) const
{
    if (width != m_cachedWidth)
    {
        m_cachedWidth = width;
        m_cachedHeight = PDFComponentPainter::getCodeBoxHeight(m_box, width);
    }
    return m_cachedHeight;
}

void PDFCodeBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFComponentPainter::CodeBox box = getPaintedBox();
    const PDFComponentPainter::State state = getState();
    if (box.paintNested)
    {
        if (const QToolButton* button = getOwnButton())
        {
            // The real button stays, only the text is drawn by the box
            painter.setClipRegion(QRegion(rect()).subtracted(QRegion(button->geometry())));
        }
    }
    PDFComponentPainter::paintCodeBox(&painter, QRectF(rect()), box, state);
}

void PDFCodeBox::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentAssistantsHelper::isRepaintChange(event))
    {
        m_cachedWidth = -1;
        updateGeometry();
        layoutChildren();
        update();
    }
    else if (event->type() == QEvent::LanguageChange)
    {
        updateButtonTexts();
    }
}

void PDFCodeBox::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutChildren();
}

void PDFCodeBox::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The name given to the box before it is shown names its text
    forwardAccessibleName();
}

bool PDFCodeBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor.data())
    {
        switch (event->type())
        {
            case QEvent::FocusIn:
                forwardAccessibleName();
                update();
                break;

            case QEvent::FocusOut:
            case QEvent::EnabledChange:
                update();
                break;

            default:
                break;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf
