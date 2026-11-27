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

#include "pdffluentwidgets.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdfdeferreddeletion.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessibleWidget>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPicture>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionSpinBox>
#include <QTableView>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QToolButton>
#include <QValidator>
#include <QtMath>

#include <cmath>
#include <limits>

namespace pdf
{

class PDFFluentWidgetsHelper
{
public:
    PDFFluentWidgetsHelper() = delete;

    static constexpr const char* TEXT_COLOR = "_pdf4qt_textColor";
    static constexpr const char* DISABLED_TEXT_COLOR = "_pdf4qt_disabledTextColor";
    static constexpr const char* TEXT_PALETTE_BINDER = "_pdf4qt_textPaletteBinder";

    /// Returns true, if the text palette is bound to the widget: its binder is
    /// found among the children (not remembered by a flag), the application may
    /// delete it as any child object, then the next binding creates a new one
    static bool isTextPaletteBound(const QWidget* widget)
    {
        return widget && widget->findChild<QObject*>(QLatin1String(TEXT_PALETTE_BINDER), Qt::FindDirectChildrenOnly);
    }

    // Geometry of the password field
    static constexpr int PASSWORD_HEIGHT = 40;
    static constexpr int PASSWORD_LOCK = 16;
    static constexpr int PASSWORD_LOCK_LEFT = 13;       ///< Border 1 and padding 12
    static constexpr int PASSWORD_BUTTON = 28;
    static constexpr int PASSWORD_BUTTON_RIGHT = 7;     ///< Border 1 and padding 6
    static constexpr int PASSWORD_GAP = 8;

    /// Returns the physical distances of the text of a password field from its edges
    static QMargins getPasswordInsets(Qt::LayoutDirection direction)
    {
        const int start = PASSWORD_LOCK_LEFT + PASSWORD_LOCK + PASSWORD_GAP;
        const int end = PASSWORD_BUTTON_RIGHT + PASSWORD_BUTTON + PASSWORD_GAP;
        return direction == Qt::RightToLeft ? QMargins(end, 0, start, 0) : QMargins(start, 0, end, 0);
    }

    /// The placeholder of the editor is a value ("Mixed" of the mixed state): it has
    /// the color of the text, not text/muted (edit-content-v2-multi-selected 1647:49458)
    static constexpr const char* PLACEHOLDER_AS_TEXT = "_pdf4qt_placeholderAsText";

    /// Returns the text of the mixed state ("Mixed", translated)
    static QString getMixedText()
    {
        return QCoreApplication::translate("pdf::PDFSpinBox", "Mixed");
    }

    /// Shows the placeholder of the editor as a value (\p isValue) or as a placeholder
    static void setPlaceholderAsValue(QWidget* editor, bool isValue)
    {
        editor->setProperty(PLACEHOLDER_AS_TEXT, isValue ? QVariant(true) : QVariant());
        if (editor->property(TEXT_COLOR).isValid())
        {
            applyTextPalette(editor);
        }
    }

    /// Empties the editor of a spin box in the mixed state (signals blocked: no text of the user).
    /// The validator of the editor (QSpinBoxValidator) adds the prefix and the suffix of Qt to
    /// every text, also to an empty one: it is detached for the write (setValidator does not
    /// validate the text again); the next text typed by the user gets the affixes from it, as
    /// in a spin box cleared by Qt.
    static void clearEditor(QLineEdit* editor)
    {
        if (!editor)
        {
            return;
        }

        const QSignalBlocker blocker(editor);
        const QPointer<const QValidator> validator = editor->validator();
        editor->setValidator(nullptr);
        editor->setText(QString());
        editor->setValidator(validator.data());
    }

    /// A text edited by the user (QLineEdit::textEdited) makes the editor modified. The editor of
    /// a spin box is not modified after the validator of Qt has added the prefix or the suffix to
    /// the typed text (QWidgetLineControl::internalSetText resets the state) - the entry of the
    /// user (the mixed state left by typing, valueEntered when the field is left) needs it.
    static void markEditorModified(QLineEdit* editor)
    {
        if (editor && !editor->isModified())
        {
            editor->setModified(true);
        }
    }

    /// Tells accessibility the new value of a spin box ("Mixed" or the value shown again)
    static void notifyValueChanged(QAbstractSpinBox* spinBox, const QVariant& value)
    {
#if QT_CONFIG(accessibility)
        QAccessibleValueChangeEvent event(spinBox, value);
        QAccessible::updateAccessibility(&event);
#else
        Q_UNUSED(spinBox);
        Q_UNUSED(value);
#endif
    }

    /// Enters the mixed state of a spin box: the editor is cleared (QAbstractSpinBox::clear:
    /// no value signal, the held value stays, the text is not interpreted) and shows "Mixed"
    /// as its placeholder. QAbstractSpinBox::clear leaves the prefix and the suffix of Qt in the
    /// editor, which would hide the placeholder: the editor is emptied (clearEditor). The signals
    /// of the editor are blocked - the text written here is no text of the user.
    static void enterMixed(QAbstractSpinBox* spinBox, QLineEdit* editor, PDFMixedSpinBoxState& state)
    {
        state = PDFMixedSpinBoxState();
        state.isMixed = true;

        if (editor)
        {
            state.placeholderBeforeMixed = editor->placeholderText();
            editor->setPlaceholderText(getMixedText());
            setPlaceholderAsValue(editor, true);
        }

        {
            const QSignalBlocker blocker(editor);
            spinBox->clear();
        }
        clearEditor(editor);
        spinBox->update();
        notifyValueChanged(spinBox, getMixedText());
    }

    /// The editor of a mixed spin box is empty (no text, no affix of Qt)
    static bool isMixedEditorEmpty(const QLineEdit* editor, const PDFMixedSpinBoxState& state)
    {
        return state.isMixed && editor && editor->text().isEmpty();
    }

    /// QAbstractSpinBox selects the text between its prefix and suffix (focus by Tab, Enter): an
    /// empty editor of the mixed state has no suffix, the selection would start before the text
    /// (a warning of QLineEdit::setSelection). Returns true, if the selection would do so.
    static bool isSelectionOutsideOfEditor(const QLineEdit* editor, const PDFMixedSpinBoxState& state, const QString& suffix)
    {
        return isMixedEditorEmpty(editor, state) && !suffix.isEmpty();
    }

    /// The prefix and the suffix of Qt are shown in the empty editor of the mixed state, while
    /// QAbstractSpinBox handles Enter (it selects the text between them); signals blocked
    /// \returns true, if they are shown (hideQtAffixes is to be called then)
    static bool showQtAffixes(QLineEdit* editor, const PDFMixedSpinBoxState& state, const QString& suffix, const QString& affixes)
    {
        if (!isSelectionOutsideOfEditor(editor, state, suffix))
        {
            return false;
        }

        const QSignalBlocker blocker(editor);
        editor->setText(affixes);
        return true;
    }

    /// The editor of the mixed state is empty again after showQtAffixes (unless the state was
    /// left meanwhile - a slot of a signal of Qt has set a value)
    static void hideQtAffixes(QLineEdit* editor, const PDFMixedSpinBoxState& state, const QString& affixes)
    {
        if (editor && state.isMixed && editor->text() == affixes)
        {
            clearEditor(editor);
        }
    }

    /// An entry of the user is committed (Enter, leaving the field after typing, a step):
    /// valueEntered follows at the end of the outermost event of the spin box. A call outside
    /// of an event of the spin box (the program: setValue, stepBy) is no entry of the user.
    static void requestEntry(PDFSpinBoxEntryState& entry)
    {
        if (entry.eventDepth > 0)
        {
            entry.isCommitPending = true;
        }
    }

    /// Takes the pending entry of the spin box at the end of its outermost event
    /// \returns true, if valueEntered is to be emitted now (the text is committed then)
    static bool takePendingEntry(QAbstractSpinBox* spinBox, QLineEdit* editor, PDFSpinBoxEntryState& entry, bool isMixed)
    {
        if (entry.eventDepth > 0 || !entry.isCommitPending)
        {
            return false;
        }
        entry.isCommitPending = false;

        // A retired spin box sends nothing (PDFDeferredDeletion::retire moves the focus out of
        // it); a field, which is mixed (Enter without a text), has no entry
        if (isMixed || PDFDeferredDeletion::isRetired(spinBox))
        {
            return false;
        }

        // The text is committed: leaving the field without typing again commits nothing
        if (editor)
        {
            editor->setModified(false);
        }
        return true;
    }

    /// The editor of a spin box no longer shows "Mixed"
    static void restorePlaceholder(QLineEdit* editor, PDFMixedSpinBoxState& state)
    {
        if (editor)
        {
            editor->setPlaceholderText(state.placeholderBeforeMixed);
            setPlaceholderAsValue(editor, false);
        }
        state.placeholderBeforeMixed.clear();
    }

    /// A text of the editor leaves the mixed state; a text of the user (the editor
    /// is modified - a text set by the spin box itself is not) begins an entry. The
    /// prefix and the suffix of Qt alone (\p affixes: QAbstractSpinBox::clear called
    /// by the application) are no value: the editor is emptied, the state stays.
    static void onMixedEditorTextChanged(QLineEdit* editor, PDFMixedSpinBoxState& state, const QString& text, const QString& affixes)
    {
        if (!state.isMixed || text.isEmpty())
        {
            return;
        }

        if (editor && text == affixes)
        {
            clearEditor(editor);
            return;
        }

        state.isMixed = false;
        state.isLeftByText = editor && editor->isModified();
        restorePlaceholder(editor, state);
    }

    /// A text of the editor ends the state also, when QAbstractSpinBox has written it with
    /// the signals of the editor blocked (setValue by the program, QAbstractSpinBoxPrivate::updateEdit);
    /// a text of the user (the editor is modified) begins an entry
    static void syncMixed(QLineEdit* editor, PDFMixedSpinBoxState& state, const QString& affixes)
    {
        if (editor)
        {
            onMixedEditorTextChanged(editor, state, editor->text(), affixes);
        }
    }

    /// Returns true, if the text of the editor is a value (as is, or after its fixup)
    static bool isSpinBoxTextAcceptable(const QAbstractSpinBox* spinBox, const QLineEdit* editor)
    {
        QString text = editor ? editor->displayText() : QString();
        int position = 0;
        if (spinBox->validate(text, position) == QValidator::Acceptable)
        {
            return true;
        }
        spinBox->fixup(text);
        return spinBox->validate(text, position) == QValidator::Acceptable;
    }

    /// Before the spin box finishes an entry (Enter, the loss of the focus): an entry of the
    /// mixed state with an empty or unacceptable text returns to the state
    /// \returns true, if the spin box is to enter the mixed state again
    static bool isMixedEntryAbandoned(const QAbstractSpinBox* spinBox, const QLineEdit* editor, const PDFMixedSpinBoxState& state)
    {
        return !state.isMixed && state.isLeftByText && !isSpinBoxTextAcceptable(spinBox, editor);
    }

    /// Before the spin box finishes an entry (Enter, the loss of the focus): an abandoned entry
    /// returns to the state, a finished one ends the tracking of the entry
    static void finishMixedEntry(QAbstractSpinBox* spinBox, QLineEdit* editor, PDFMixedSpinBoxState& state, const QString& affixes)
    {
        syncMixed(editor, state, affixes);
        if (isMixedEntryAbandoned(spinBox, editor, state))
        {
            enterMixed(spinBox, editor, state);
        }
        state.isLeftByText = false;
    }

    static void applyTextPalette(QWidget* widget)
    {
        const PDFThemeColors::Color text = static_cast<PDFThemeColors::Color>(widget->property(TEXT_COLOR).toInt());
        const PDFThemeColors::Color disabledText = static_cast<PDFThemeColors::Color>(widget->property(DISABLED_TEXT_COLOR).toInt());
        QPalette palette = PDFThemeWidgetHelper::createTextPalette(text, disabledText);
        if (widget->property(PLACEHOLDER_AS_TEXT).toBool())
        {
            // The placeholder is a value: the tokens of the text (as createTextPalette has
            // them), no color read back from a palette
            const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();
            for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
            {
                palette.setColor(group, QPalette::PlaceholderText, theme.getColor(group == QPalette::Disabled ? disabledText : text));
            }
        }
        widget->setPalette(palette);
    }

    /// Sets the width of the prefix or of the suffix, which is drawn beside the value of a spin box
    static void setAffix(QAbstractSpinBox* spinBox, const char* property, const QString& text)
    {
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
        spinBox->setProperty(property, text.isEmpty() ? QVariant() : QVariant(qCeil(PDFStylePainter::textWidth(text, font))));

        // The spin box forgets its cached size hints and places its editor again
        QEvent styleChange(QEvent::StyleChange);
        QApplication::sendEvent(spinBox, &styleChange);
        spinBox->updateGeometry();
        spinBox->update();
    }

    /// The prefix and the suffix are only drawn, so they are told to assistive
    /// technologies as the description of the spin box (unless the application
    /// has set a description of its own)
    static void setAffixDescription(QAbstractSpinBox* spinBox, const QString& oldPrefix, const QString& oldSuffix, const QString& prefix, const QString& suffix)
    {
        const QString oldDescription = QStringLiteral("%1 %2").arg(oldPrefix, oldSuffix).trimmed();
        const QString description = QStringLiteral("%1 %2").arg(prefix, suffix).trimmed();

        if (spinBox->accessibleDescription().isEmpty() || spinBox->accessibleDescription() == oldDescription)
        {
            spinBox->setAccessibleDescription(description);
        }
    }

    /// Draws the muted prefix and suffix of a spin box
    static void paintAffixes(QAbstractSpinBox* spinBox, const QString& prefix, const QString& suffix)
    {
        if (prefix.isEmpty() && suffix.isEmpty())
        {
            return;
        }

        QStyleOptionSpinBox option;
        option.initFrom(spinBox);
        option.buttonSymbols = spinBox->buttonSymbols();
        option.frame = spinBox->hasFrame();

        const QRect editRect = spinBox->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, spinBox);
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
        const QColor color = PDFStylePainter::color(PDFThemeColors::TextMuted);
        const bool isRightToLeft = spinBox->layoutDirection() == Qt::RightToLeft;
        const QRectF outer(spinBox->rect());

        QPainter painter(spinBox);

        if (!prefix.isEmpty())
        {
            const qreal width = qCeil(PDFStylePainter::textWidth(prefix, font));
            QRectF rect(editRect.left() + 2.0 - 8.0 - width, 0.0, width, outer.height());
            if (isRightToLeft)
            {
                rect.moveLeft(editRect.right() + 1.0 - 2.0 + 8.0);
            }
            PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, prefix, font, color);
        }

        if (!suffix.isEmpty())
        {
            const qreal width = qCeil(PDFStylePainter::textWidth(suffix, font));
            QRectF rect(editRect.right() + 1.0 + 8.0, 0.0, width, outer.height());
            if (isRightToLeft)
            {
                rect.moveLeft(editRect.left() - 8.0 - width);
            }
            PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, suffix, font, color);
        }
    }

    /// Context menu of a spin box (QR3.5): the menu of QAbstractSpinBox::contextMenuEvent - the
    /// standard menu of the line edit, its Select All replaced by Select All of the spin box, Step
    /// up and Step down - shown by popup() instead of QMenu::exec (no nested event loop). The
    /// texts and the translation context are those of Qt. The menu is a child of the line edit
    /// (deleted with the spin box) and is deleted, when it is closed. Select All and the steps
    /// are queued calls of the slots of the spin box: a slot of the value may delete the spin box
    /// and its menu while the menu still emits; a deleted spin box drops them. The actions of
    /// the line edit act as in the menu of QLineEdit, which Qt shows by popup() too.
    /// \param spinBox Spin box
    /// \param lineEdit Line edit of the spin box (QAbstractSpinBox::lineEdit is protected)
    /// \param stepEnabled Enabled steps of the spin box (QAbstractSpinBox::stepEnabled is protected)
    /// \param event Event of the context menu
    /// \param isEditorSelected Select All selects the editor (the empty editor of the mixed state,
    ///        isSelectionOutsideOfEditor), not the text between the affixes of Qt
    static void showSpinBoxContextMenu(QAbstractSpinBox* spinBox, QLineEdit* lineEdit, QAbstractSpinBox::StepEnabled stepEnabled, QContextMenuEvent* event, bool isEditorSelected)
    {
        QMenu* menu = lineEdit ? lineEdit->createStandardContextMenu() : nullptr;
        if (!menu)
        {
            return;
        }
        menu->setAttribute(Qt::WA_DeleteOnClose);

        // Select All of the spin box replaces Select All of the line edit (the action of the line
        // edit stays a child of the menu: QLineEdit remembers it until the menu is deleted)
        QAction* lineEditSelectAll = nullptr;
        for (QAction* action : menu->actions())
        {
            if (action->objectName() == QLatin1String("select-all"))
            {
                lineEditSelectAll = action;
                break;
            }
        }
        QAction* selectAllAction = new QAction(QAbstractSpinBox::tr("&Select All"), menu);
        selectAllAction->setShortcut(QKeySequence::SelectAll);
        menu->insertAction(lineEditSelectAll, selectAllAction);
        if (lineEditSelectAll)
        {
            menu->removeAction(lineEditSelectAll);
        }
        menu->addSeparator();
        QAction* stepUpAction = menu->addAction(QAbstractSpinBox::tr("&Step up"));
        stepUpAction->setEnabled(stepEnabled.testFlag(QAbstractSpinBox::StepUpEnabled));
        QAction* stepDownAction = menu->addAction(QAbstractSpinBox::tr("Step &down"));
        stepDownAction->setEnabled(stepEnabled.testFlag(QAbstractSpinBox::StepDownEnabled));
        menu->addSeparator();

        if (isEditorSelected)
        {
            QObject::connect(selectAllAction, &QAction::triggered, lineEdit, &QLineEdit::selectAll, Qt::QueuedConnection);
        }
        else
        {
            QObject::connect(selectAllAction, &QAction::triggered, spinBox, &QAbstractSpinBox::selectAll, Qt::QueuedConnection);
        }
        QObject::connect(stepUpAction, &QAction::triggered, spinBox, &QAbstractSpinBox::stepUp, Qt::QueuedConnection);
        QObject::connect(stepDownAction, &QAction::triggered, spinBox, &QAbstractSpinBox::stepDown, Qt::QueuedConnection);

        // The position of QAbstractSpinBox: the pointer, the middle of the spin box for the keyboard
        const QPoint position = (event->reason() == QContextMenuEvent::Mouse) ? event->globalPos()
                                                                              : spinBox->mapToGlobal(QPoint(event->pos().x(), 0)) + QPoint(spinBox->width() / 2, spinBox->height() / 2);
        event->accept();
        menu->popup(position);
    }

    /// Position of the instant menu of a tool button in a row (positionMenu of QToolButton, Qt
    /// 6.11): below the button, aligned with its leading edge, above it, when there is no room
    /// below, the point kept inside of the available area of the screen of the button
    /// \param button Tool button
    /// \param menuSize Size hint of the menu
    static QPoint getToolButtonMenuPosition(const QWidget* button, const QSize& menuSize)
    {
        const QRect rect = button->rect();
        const QScreen* buttonScreen = button->screen();
        const QRect screen = buttonScreen ? buttonScreen->availableGeometry() : QRect();
        const bool isBelow = screen.isNull() || button->mapToGlobal(QPoint(0, rect.bottom())).y() + menuSize.height() <= screen.bottom();

        QPoint position;
        if (button->isRightToLeft())
        {
            position = isBelow ? button->mapToGlobal(rect.bottomRight()) : button->mapToGlobal(rect.topRight() - QPoint(0, menuSize.height()));
            position.rx() -= menuSize.width();
        }
        else
        {
            position = isBelow ? button->mapToGlobal(rect.bottomLeft()) : button->mapToGlobal(rect.topLeft() - QPoint(0, menuSize.height()));
        }

        if (!screen.isNull())
        {
            position.rx() = qMax(screen.left(), qMin(position.x(), screen.right() - menuSize.width()));
            position.ry() = qMax(screen.top(), qMin(position.y() + 1, screen.bottom()));
        }
        return position;
    }

    /// The release of the mouse, which stops the steps of a held arrow of a spin box (the reset of
    /// QAbstractSpinBox::contextMenuEvent: the release goes to the menu); it changes nothing, while
    /// no arrow is held
    static QMouseEvent createSpinBoxRelease()
    {
        return QMouseEvent(QEvent::MouseButtonRelease, QPointF(), QPointF(), Qt::LeftButton, Qt::NoButton, QGuiApplication::keyboardModifiers());
    }
};

/// Sets the text palette of the theme to its parent widget, whenever the theme
/// changes. It is a child of the widget, so it lives exactly as long as the widget.
class PDFTextPaletteBinder : public QObject
{
public:
    explicit PDFTextPaletteBinder(QWidget* widget) :
        QObject(widget)
    {
        setObjectName(QLatin1String(PDFFluentWidgetsHelper::TEXT_PALETTE_BINDER));
        PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFTextPaletteBinder::apply);
    }

private:
    void apply()
    {
        if (QWidget* widget = qobject_cast<QWidget*>(parent()))
        {
            PDFFluentWidgetsHelper::applyTextPalette(widget);
        }
    }
};

QPalette PDFThemeWidgetHelper::createTextPalette(PDFThemeColors::Color text, PDFThemeColors::Color disabledText)
{
    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();
    QPalette palette = PDFThemeColors::createLeakPalette();

    for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
    {
        const bool isDisabled = group == QPalette::Disabled;
        const QColor textColor = theme.getColor(isDisabled ? disabledText : text);

        palette.setColor(group, QPalette::WindowText, textColor);
        palette.setColor(group, QPalette::Text, textColor);
        palette.setColor(group, QPalette::ButtonText, textColor);
        palette.setColor(group, QPalette::PlaceholderText, theme.getColor(PDFThemeColors::TextMuted));
        palette.setColor(group, QPalette::Highlight, theme.getColor(isDisabled ? PDFThemeColors::BorderStrong : PDFThemeColors::BgAccent));
        palette.setColor(group, QPalette::HighlightedText, theme.getColor(PDFThemeColors::IconOnAccent));
        palette.setColor(group, QPalette::Link, theme.getColor(isDisabled ? disabledText : PDFThemeColors::TextAccent));
        palette.setColor(group, QPalette::LinkVisited, theme.getColor(isDisabled ? disabledText : PDFThemeColors::TextAccentPressed));
    }

    return palette;
}

void PDFThemeWidgetHelper::bindTextPalette(QWidget* widget, PDFThemeColors::Color text, PDFThemeColors::Color disabledText)
{
    if (!widget)
    {
        return;
    }

    widget->setProperty(PDFFluentWidgetsHelper::TEXT_COLOR, static_cast<int>(text));
    widget->setProperty(PDFFluentWidgetsHelper::DISABLED_TEXT_COLOR, static_cast<int>(disabledText));
    PDFFluentWidgetsHelper::applyTextPalette(widget);

    if (!PDFFluentWidgetsHelper::isTextPaletteBound(widget))
    {
        new PDFTextPaletteBinder(widget);
    }
}

PDFLabel::PDFLabel(QWidget* parent) :
    QLabel(parent)
{
    init();
}

PDFLabel::PDFLabel(const QString& text, QWidget* parent) :
    QLabel(text, parent)
{
    init();
}

PDFLabel::PDFLabel(const QString& text, PDFThemeFonts::Role fontRole, PDFThemeColors::Color color, QWidget* parent) :
    QLabel(text, parent),
    m_fontRole(fontRole),
    m_color(color)
{
    init();
}

void PDFLabel::setFontRole(PDFThemeFonts::Role fontRole)
{
    m_fontRole = fontRole;
    setFont(PDFThemeFonts::getFont(fontRole));
}

void PDFLabel::setTextColor(PDFThemeColors::Color color)
{
    m_color = color;
    PDFThemeWidgetHelper::bindTextPalette(this, m_color);
    update();
}

void PDFLabel::setLineHeight(int lineHeight)
{
    lineHeight = qBound(0, lineHeight, MAX_LINE_HEIGHT);
    if (m_lineHeight != lineHeight)
    {
        m_lineHeight = lineHeight;
        m_selectionAnchor = -1;
        m_selectionCursor = -1;
        updateGeometry();
        update();
    }
}

bool PDFLabel::isLineLayout() const
{
    if (m_lineHeight <= 0 || !pixmap().isNull() || movie() || !picture().isNull())
    {
        return false;
    }

    return textFormat() == Qt::PlainText || (textFormat() == Qt::AutoText && !Qt::mightBeRichText(text()));
}

QString PDFLabel::selectedText() const
{
    if (!isLineLayout())
    {
        return QLabel::selectedText();
    }

    const QString labelText = text();
    if (m_selectionAnchor < 0 || m_selectionCursor < 0 || m_selectionText != labelText)
    {
        return QString();
    }

    const int start = qBound(0, qMin(m_selectionAnchor, m_selectionCursor), int(labelText.size()));
    const int end = qBound(0, qMax(m_selectionAnchor, m_selectionCursor), int(labelText.size()));
    return labelText.mid(start, end - start);
}

bool PDFLabel::hasSelectedText() const
{
    return !selectedText().isEmpty();
}

QSize PDFLabel::getLineLayoutExtra() const
{
    const QMargins margins = contentsMargins();
    return QSize(margins.left() + margins.right() + 2 * margin(), margins.top() + margins.bottom() + 2 * margin());
}

int PDFLabel::heightForWidth(int width) const
{
    if (!isLineLayout())
    {
        return QLabel::heightForWidth(width);
    }

    const QSize extra = getLineLayoutExtra();
    return layoutLines(qMax(1, width - extra.width()), nullptr, QPointF(), QPointF(), nullptr) + extra.height();
}

QSize PDFLabel::sizeHint() const
{
    if (!isLineLayout())
    {
        return QLabel::sizeHint();
    }

    const QSize extra = getLineLayoutExtra();
    if (wordWrap())
    {
        // QLabel chooses the width of a wrapped text, the height follows from it
        const int width = QLabel::sizeHint().width();
        return QSize(width, heightForWidth(width));
    }

    // The widest line of the text (lines are not wrapped)
    qreal width = 0.0;
    const QFontMetricsF metrics(font());
    const QStringList lines = text().split(QLatin1Char('\n'));
    for (const QString& line : lines)
    {
        width = qMax(width, metrics.horizontalAdvance(line));
    }

    return QSize(qCeil(width) + extra.width(), int(lines.size()) * m_lineHeight + extra.height());
}

QSize PDFLabel::minimumSizeHint() const
{
    if (!isLineLayout())
    {
        return QLabel::minimumSizeHint();
    }

    // As QLabel: the width of the longest word (of the whole text without the
    // wrapping), the height of the text without the wrapping (or less)
    const QSize extra = getLineLayoutExtra();
    const int unwrappedHeight = int(text().count(QLatin1Char('\n')) + 1) * m_lineHeight + extra.height();
    const QSize hint = sizeHint();
    const int width = wordWrap() ? QLabel::minimumSizeHint().width() : hint.width();
    return QSize(width, qMin(unwrappedHeight, hint.height()));
}

int PDFLabel::layoutLines(qreal width, QPainter* painter, const QPointF& origin, const QPointF& point, int* position) const
{
    const QString labelText = text();
    const Qt::Alignment visualAlignment = QStyle::visualAlignment(layoutDirection(), alignment());

    QTextOption textOption;
    textOption.setWrapMode(wordWrap() ? QTextOption::WordWrap : QTextOption::ManualWrap);
    textOption.setAlignment(visualAlignment & Qt::AlignHorizontal_Mask);
    textOption.setTextDirection(layoutDirection());

    // The selection is drawn by the colors of the text palette of the library
    const bool hasSelection = painter && m_selectionAnchor >= 0 && m_selectionCursor >= 0 && m_selectionAnchor != m_selectionCursor && m_selectionText == labelText;
    const int selectionStart = qMin(m_selectionAnchor, m_selectionCursor);
    const int selectionEnd = qMax(m_selectionAnchor, m_selectionCursor);
    QTextCharFormat selectionFormat;
    selectionFormat.setBackground(PDFStylePainter::color(PDFThemeColors::BgAccent));
    selectionFormat.setForeground(PDFStylePainter::color(PDFThemeColors::IconOnAccent));

    int y = 0;
    int paragraphStart = 0;
    bool isPositionFound = false;
    const QStringList paragraphs = labelText.split(QLatin1Char('\n'));
    for (const QString& paragraph : paragraphs)
    {
        QTextLayout layout(paragraph, font());
        layout.setTextOption(textOption);
        layout.beginLayout();

        int lineIndex = 0;
        while (true)
        {
            QTextLine line = layout.createLine();
            if (!line.isValid())
            {
                break;
            }

            // The text is centered in the line of the design
            line.setLineWidth(width);
            const qreal textHeight = line.ascent() + line.descent();
            line.setPosition(QPointF(0.0, lineIndex * m_lineHeight + (m_lineHeight - textHeight) / 2.0));
            ++lineIndex;
        }
        layout.endLayout();

        const int lineCount = qMax(1, layout.lineCount());
        if (position && !isPositionFound)
        {
            const qreal relativeY = point.y() - origin.y() - y;
            const bool isLast = paragraphStart + paragraph.size() >= labelText.size();
            if (relativeY < lineCount * m_lineHeight || isLast)
            {
                const int lineIndexOfPoint = qBound(0, int(relativeY) / qMax(1, m_lineHeight), layout.lineCount() - 1);
                const QTextLine line = layout.lineCount() > 0 ? layout.lineAt(lineIndexOfPoint) : QTextLine();
                const int cursor = line.isValid() ? line.xToCursor(point.x() - origin.x()) : 0;
                *position = relativeY < 0 ? paragraphStart : paragraphStart + cursor;
                isPositionFound = true;
            }
        }

        if (painter)
        {
            QList<QTextLayout::FormatRange> selections;
            if (hasSelection)
            {
                const int start = qMax(selectionStart, paragraphStart) - paragraphStart;
                const int end = qMin(selectionEnd, paragraphStart + int(paragraph.size())) - paragraphStart;
                if (end > start)
                {
                    QTextLayout::FormatRange range;
                    range.start = start;
                    range.length = end - start;
                    range.format = selectionFormat;
                    selections << range;
                }
            }

            layout.draw(painter, origin + QPointF(0.0, y), selections);
        }

        y += lineCount * m_lineHeight;
        paragraphStart += int(paragraph.size()) + 1;
    }

    return y;
}

QRectF PDFLabel::getLineLayoutRect(QPointF* origin) const
{
    const QRectF rect = QRectF(contentsRect()).adjusted(margin(), margin(), -margin(), -margin());
    const int height = layoutLines(qMax(1.0, rect.width()), nullptr, QPointF(), QPointF(), nullptr);

    // The vertical alignment of the label places the lines
    qreal top = rect.top();
    if (alignment() & Qt::AlignBottom)
    {
        top = rect.bottom() - height;
    }
    else if (alignment() & Qt::AlignVCenter)
    {
        top = rect.top() + (rect.height() - height) / 2.0;
    }

    if (origin)
    {
        *origin = QPointF(rect.left(), qRound(top));
    }
    return rect;
}

int PDFLabel::getTextPosition(const QPointF& point) const
{
    QPointF origin;
    const QRectF rect = getLineLayoutRect(&origin);
    int position = 0;
    layoutLines(qMax(1.0, rect.width()), nullptr, origin, point, &position);
    return qBound(0, position, int(text().size()));
}

void PDFLabel::mousePressEvent(QMouseEvent* event)
{
    if (!isLineLayout())
    {
        QLabel::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton && textInteractionFlags().testFlag(Qt::TextSelectableByMouse))
    {
        m_selectionText = text();
        m_selectionAnchor = getTextPosition(event->position());
        m_selectionCursor = m_selectionAnchor;
        m_isSelecting = true;
        update();
        event->accept();
        return;
    }

    event->ignore();
}

void PDFLabel::mouseMoveEvent(QMouseEvent* event)
{
    if (!isLineLayout())
    {
        QLabel::mouseMoveEvent(event);
        return;
    }

    if (m_isSelecting && event->buttons().testFlag(Qt::LeftButton) && m_selectionText == text())
    {
        m_selectionCursor = getTextPosition(event->position());
        update();
        event->accept();
        return;
    }

    m_isSelecting = false;
    event->ignore();
}

void PDFLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (!isLineLayout())
    {
        QLabel::mouseReleaseEvent(event);
        return;
    }

    if (m_isSelecting && event->button() == Qt::LeftButton)
    {
        m_isSelecting = false;
        event->accept();
        return;
    }

    event->ignore();
}

void PDFLabel::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!isLineLayout())
    {
        QLabel::mouseDoubleClickEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton && textInteractionFlags().testFlag(Qt::TextSelectableByMouse))
    {
        // A double click selects the word below the pointer
        const QString labelText = text();
        const int position = getTextPosition(event->position());
        int start = position;
        int end = position;
        while (start > 0 && labelText.at(start - 1).isLetterOrNumber())
        {
            --start;
        }
        while (end < labelText.size() && labelText.at(end).isLetterOrNumber())
        {
            ++end;
        }

        m_selectionText = labelText;
        m_selectionAnchor = start;
        m_selectionCursor = end;
        m_isSelecting = false;
        update();
        event->accept();
        return;
    }

    event->ignore();
}

void PDFLabel::keyPressEvent(QKeyEvent* event)
{
    if (isLineLayout() && event->matches(QKeySequence::Copy) && hasSelectedText())
    {
        QGuiApplication::clipboard()->setText(selectedText());
        event->accept();
        return;
    }

    QLabel::keyPressEvent(event);
}

void PDFLabel::contextMenuEvent(QContextMenuEvent* event)
{
    if (!isLineLayout())
    {
        QLabel::contextMenuEvent(event);
        return;
    }

    // The text control of QLabel would offer its own selection, which is not the
    // selection of the label: the menu of a selectable text has Copy and Select All
    // as the menu of QLabel
    if (!textInteractionFlags().testFlag(Qt::TextSelectableByMouse) || text().isEmpty())
    {
        event->ignore();
        return;
    }

    // A popup, no nested event loop (A0): the menu is a child of the label (deleted
    // with it) and is deleted, when it is closed; its actions queue their work into
    // the label (a deleted label drops it). The selected text is taken now.
    m_contextMenuSelection = selectedText();
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* copyAction = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("copy")), tr("&Copy"));
    copyAction->setEnabled(!m_contextMenuSelection.isEmpty());
    QAction* selectAllAction = menu->addAction(tr("Select All"));
    connect(copyAction, &QAction::triggered, this, &PDFLabel::copyContextMenuSelection, Qt::QueuedConnection);
    connect(selectAllAction, &QAction::triggered, this, &PDFLabel::selectAllFromContextMenu, Qt::QueuedConnection);
    event->accept();
    menu->popup(event->globalPos());
}

void PDFLabel::copyContextMenuSelection()
{
    if (!m_contextMenuSelection.isEmpty())
    {
        QGuiApplication::clipboard()->setText(m_contextMenuSelection);
    }
}

void PDFLabel::selectAllFromContextMenu()
{
    const QString labelText = text();
    m_selectionText = labelText;
    m_selectionAnchor = 0;
    m_selectionCursor = int(labelText.size());
    m_isSelecting = false;
    update();
}

void PDFLabel::paintEvent(QPaintEvent* event)
{
    if (isLineLayout())
    {
        // A plain text in the lines of the line height of the design
        QPainter painter(this);
        drawFrame(&painter);
        painter.setFont(font());
        painter.setPen(PDFStylePainter::color(isEnabled() ? m_color : PDFThemeColors::TextDisabled));

        QPointF origin;
        const QRectF rect = getLineLayoutRect(&origin);
        layoutLines(qMax(1.0, rect.width()), &painter, origin, QPointF(), nullptr);
        return;
    }

    const bool hasGraphics = !pixmap().isNull() || movie() || !picture().isNull();
    const bool isRichText = textFormat() == Qt::RichText || textFormat() == Qt::MarkdownText || (textFormat() == Qt::AutoText && Qt::mightBeRichText(text()));
    const bool isInteractive = textInteractionFlags() & (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::TextEditable);

    if (hasGraphics || isRichText || isInteractive || text().isEmpty())
    {
        // The text control of QLabel draws by the palette of the label, which holds the colors of the theme
        QLabel::paintEvent(event);
        return;
    }

    // Plain text is drawn directly by the color of the theme
    QPainter painter(this);
    drawFrame(&painter);

    QRect rect = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
    const Qt::Alignment visualAlignment = QStyle::visualAlignment(layoutDirection(), alignment());

    if (indent() > 0)
    {
        if (visualAlignment.testFlag(Qt::AlignLeft))
        {
            rect.setLeft(rect.left() + indent());
        }
        if (visualAlignment.testFlag(Qt::AlignRight))
        {
            rect.setRight(rect.right() - indent());
        }
        if (visualAlignment.testFlag(Qt::AlignTop))
        {
            rect.setTop(rect.top() + indent());
        }
        if (visualAlignment.testFlag(Qt::AlignBottom))
        {
            rect.setBottom(rect.bottom() - indent());
        }
    }

    int flags = static_cast<int>(visualAlignment);
    if (wordWrap())
    {
        flags |= Qt::TextWordWrap;
    }
    if (buddy())
    {
        flags |= style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, this) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
    }

    painter.setFont(font());
    painter.setPen(PDFStylePainter::color(isEnabled() ? m_color : PDFThemeColors::TextDisabled));
    if (m_isDataText)
    {
        // The visual alignment is absolute (QStyle::visualAlignment): only the order of the
        // characters takes the direction of the text
        painter.setLayoutDirection(PDFComponentPainter::getDataTextDirection(text()));
    }
    painter.drawText(rect, flags, text());
}

void PDFLabel::setDataText(bool isDataText)
{
    if (m_isDataText != isDataText)
    {
        m_isDataText = isDataText;
        update();
    }
}

void PDFLabel::init()
{
    setFont(PDFThemeFonts::getFont(m_fontRole));
    PDFThemeWidgetHelper::bindTextPalette(this, m_color);
}

PDFLineEdit::PDFLineEdit(QWidget* parent) :
    QLineEdit(parent)
{
    init();
}

PDFLineEdit::PDFLineEdit(const QString& text, QWidget* parent) :
    QLineEdit(text, parent)
{
    init();
}

bool PDFLineEdit::isInvalid() const
{
    return PDFWidgetStyleProperties::isSet(this, PDFWidgetStyleProperties::INVALID);
}

void PDFLineEdit::setInvalid(bool invalid, const QString& message)
{
    if (isInvalid() != invalid)
    {
        setProperty(PDFWidgetStyleProperties::INVALID, invalid);
        update();
    }

    // The description is replaced only, when it is the last message of the field
    const QString newMessage = invalid ? message : QString();
    if (accessibleDescription().isEmpty() || accessibleDescription() == m_invalidMessage)
    {
        setAccessibleDescription(newMessage);
    }
    m_invalidMessage = newMessage;
}

bool PDFLineEdit::isPill() const
{
    return PDFWidgetStyleProperties::isSet(this, PDFWidgetStyleProperties::PILL);
}

void PDFLineEdit::setPill(bool pill)
{
    if (isPill() != pill)
    {
        PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::PILL, pill);
    }
}

QAction* PDFLineEdit::addIconAction(const QString& iconName, ActionPosition position, const QString& text)
{
    QAction* action = addAction(PDFThemeIcons::getIcon(iconName), position);
    action->setText(text);
    return action;
}

void PDFLineEdit::setSearchField()
{
    setPill(true);
    addIconAction(QStringLiteral("search"), QLineEdit::LeadingPosition, tr("Search"));
}

void PDFLineEdit::setPasswordField()
{
    setEchoMode(QLineEdit::Password);

    if (!m_isPasswordField)
    {
        m_isPasswordField = true;

        // Height 40; the lock 16 px at 13 px from the left edge and the gap 8 before the
        // text; the button of 28 px at 7 px from the right edge and the gap 8 after the text
        setProperty(PDFWidgetStyleProperties::FIELD_HEIGHT, PDFFluentWidgetsHelper::PASSWORD_HEIGHT);
        setProperty(PDFWidgetStyleProperties::FIELD_TEXT_INSETS, QVariant::fromValue(PDFFluentWidgetsHelper::getPasswordInsets(layoutDirection())));

        // S5.2: the selection of a password is never dragged out (mousePressEvent keeps it off)
        setDragEnabled(false);

        PDFIconButton* passwordButton = new PDFIconButton(QStringLiteral("eye"), PDFIconButton::Size::Medium, this);
        passwordButton->setCheckable(true);
        passwordButton->setCursor(Qt::ArrowCursor);
        m_passwordButton = passwordButton;

        connect(passwordButton, &QAbstractButton::toggled, this, &PDFLineEdit::onPasswordButtonToggled);
        connect(this, &QLineEdit::textChanged, this, &PDFLineEdit::updatePasswordButton);
        connect(this, &QLineEdit::textChanged, this, &PDFLineEdit::updatePasswordFont);

        updatePasswordButton();
        updateGeometry();
        placePasswordButton();
    }

    updatePasswordFont();
}

PDFIconButton* PDFLineEdit::getPasswordButton() const
{
    return qobject_cast<PDFIconButton*>(m_passwordButton.data());
}

void PDFLineEdit::onPasswordButtonToggled(bool isShown)
{
    setEchoMode(isShown ? QLineEdit::Normal : QLineEdit::Password);
    updatePasswordButton();
    updatePasswordFont();
}

void PDFLineEdit::updatePasswordButton()
{
    PDFIconButton* passwordButton = getPasswordButton();
    if (!passwordButton)
    {
        return;
    }

    const bool isShown = passwordButton->isChecked();
    const QString buttonText = isShown ? tr("Hide password") : tr("Show password");
    passwordButton->setIconName(isShown ? QStringLiteral("eye-off") : QStringLiteral("eye"));
    passwordButton->setAccessibleName(buttonText);
    passwordButton->setToolTip(buttonText);

    // The toggle is a plain icon button in every content of the field (icon/muted at rest,
    // icon/default on hover, icon/strong when pressed), its color is not overridden
}

void PDFLineEdit::placePasswordButton()
{
    if (!m_passwordButton)
    {
        return;
    }

    const QSize buttonSize = m_passwordButton->sizeHint();
    const QRect logical(width() - PDFFluentWidgetsHelper::PASSWORD_BUTTON_RIGHT - buttonSize.width(), (height() - buttonSize.height()) / 2, buttonSize.width(), buttonSize.height());
    m_passwordButton->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
}

void PDFLineEdit::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    placePasswordButton();
}

void PDFLineEdit::keyPressEvent(QKeyEvent* event)
{
    // S5.2: a password goes to no clipboard, also when the button shows it (QLineEdit copies
    // a shown value); Cut does not remove the selection, so nothing seems to have been taken
    if (m_isPasswordField && (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)))
    {
        event->accept();
        return;
    }

    // A key may emit returnPressed / textEdited, whose slot deletes the field
    QPointer<PDFLineEdit> guard(this);
    QLineEdit::keyPressEvent(event);
    if (guard)
    {
        releaseSelectionClipboard();
    }
}

void PDFLineEdit::mousePressEvent(QMouseEvent* event)
{
    // The drag of a selection stays off for a password, also when someone has enabled it
    if (m_isPasswordField && dragEnabled())
    {
        setDragEnabled(false);
    }
    QLineEdit::mousePressEvent(event);
}

void PDFLineEdit::mouseReleaseEvent(QMouseEvent* event)
{
    // The release may emit a signal of the selection, whose slot deletes the field
    QPointer<PDFLineEdit> guard(this);
    QLineEdit::mouseReleaseEvent(event);
    if (guard)
    {
        releaseSelectionClipboard();
    }
}

void PDFLineEdit::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_isPasswordField)
    {
        QLineEdit::contextMenuEvent(event);
        return;
    }

    // The standard menu of QLineEdit without Cut and Copy (disabled as for a hidden password; an
    // explicitly disabled action ignores also a trigger by the code); deleted when it is closed
    QMenu* menu = createStandardContextMenu();
    if (!menu)
    {
        return;
    }
    for (QAction* action : menu->actions())
    {
        if (action->objectName() == QLatin1String("edit-cut") || action->objectName() == QLatin1String("edit-copy"))
        {
            action->setEnabled(false);
        }
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void PDFLineEdit::releaseSelectionClipboard()
{
    if (!m_isPasswordField)
    {
        return;
    }

    // QLineEdit puts the selection of a shown value into the selection clipboard (X11, Wayland)
    // after a key and a release of the mouse; it is taken back before any other application can
    // read it (the request of another client waits for the event loop)
    QClipboard* clipboard = QGuiApplication::clipboard();
    const QString selection = selectedText();
    if (clipboard && clipboard->supportsSelection() && !selection.isEmpty() && clipboard->ownsSelection() &&
        clipboard->text(QClipboard::Selection) == selection)
    {
        clipboard->clear(QClipboard::Selection);
    }
}

void PDFLineEdit::setElidedWhenInactive(bool elided)
{
    if (m_isElidedWhenInactive != elided)
    {
        m_isElidedWhenInactive = elided;
        update();
    }
}

QRect PDFLineEdit::getTextLineRect(int lineHeight) const
{
    // As QLineEdit::paintEvent: the contents without the text margins, 2 px at the sides
    // (QLineEditPrivate::horizontalMargin), the line centered vertically
    QStyleOptionFrame panel;
    initStyleOption(&panel);
    const QRect contents = style()->subElementRect(QStyle::SE_LineEditContents, &panel, this).marginsRemoved(textMargins());
    return QRect(contents.x() + 2, contents.y() + (contents.height() - lineHeight + 1) / 2, contents.width() - 4, lineHeight);
}

bool PDFLineEdit::isTextElided() const
{
    // The side widgets of the actions move the text (private margins of QLineEdit)
    if (!m_isElidedWhenInactive || hasFocus() || echoMode() != QLineEdit::Normal || text().isEmpty() || !actions().isEmpty())
    {
        return false;
    }

    const QFontMetrics metrics = fontMetrics();
    return metrics.horizontalAdvance(text()) > getTextLineRect(metrics.height()).width();
}

QString PDFLineEdit::getDisplayedText() const
{
    if (!isTextElided())
    {
        return text();
    }

    const QFontMetrics metrics = fontMetrics();
    return metrics.elidedText(text(), Qt::ElideRight, getTextLineRect(metrics.height()).width());
}

void PDFLineEdit::paintElidedText()
{
    // The panel of the style as QLineEdit, the value elided at its end: its first line
    // from the leading edge (the place of an unscrolled text), the baseline as QLineEdit
    QPainter painter(this);
    QStyleOptionFrame panel;
    initStyleOption(&panel);
    style()->drawPrimitive(QStyle::PE_PanelLineEdit, &panel, &painter, this);

    const QFontMetrics metrics = fontMetrics();
    const QString displayedText = getDisplayedText();
    const int lineHeight = qMax(metrics.height(), metrics.boundingRect(text()).height());
    const QRect lineRect = getTextLineRect(lineHeight);

    // The text is laid out as by the line control of QLineEdit (one line, the direction of the
    // field, trailing spaces), so the elided value lies, where the same value would lie unscrolled
    QTextLayout layout(displayedText, font());
    QTextOption option = layout.textOption();
    option.setTextDirection(layoutDirection());
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    layout.setTextOption(option);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    layout.endLayout();

    // QLineEdit: the indent of the line by the visual alignment of a text, which fits
    const int widthUsed = qRound(line.naturalTextWidth()) + 1;
    int indent = 0;
    switch (QStyle::visualAlignment(layoutDirection(), alignment()) & Qt::AlignHorizontal_Mask & ~Qt::AlignAbsolute)
    {
        case Qt::AlignRight:
            indent = widthUsed - lineRect.width() + 1;
            break;

        case Qt::AlignHCenter:
            indent = (widthUsed - lineRect.width()) / 2;
            break;

        default:
            break;
    }

    // The color of the text by its tokens (the field binds them, its palette is never read)
    const QVariant textToken = property(isEnabled() ? PDFFluentWidgetsHelper::TEXT_COLOR : PDFFluentWidgetsHelper::DISABLED_TEXT_COLOR);
    const PDFThemeColors::Color color = textToken.isValid() ? static_cast<PDFThemeColors::Color>(textToken.toInt()) : PDFThemeColors::TextPrimary;
    painter.setClipRect(style()->subElementRect(QStyle::SE_LineEditContents, &panel, this).marginsRemoved(textMargins()));
    painter.setPen(PDFThemeManager::getInstance()->getColor(color));
    layout.draw(&painter, QPointF(lineRect.topLeft() - QPoint(indent, qRound(line.ascent()) - metrics.ascent())));
}

void PDFLineEdit::paintEvent(QPaintEvent* event)
{
    if (isTextElided())
    {
        paintElidedText();
    }
    else
    {
        QLineEdit::paintEvent(event);
    }

    if (!m_isPasswordField)
    {
        return;
    }

    // Lock of 16 px: muted, red in an invalid field, icon/disabled in a disabled field
    PDFThemeColors::Color color = PDFThemeColors::IconMuted;
    if (!isEnabled())
    {
        color = PDFThemeColors::IconDisabled;
    }
    else if (isInvalid())
    {
        color = PDFThemeColors::IconDanger;
    }

    const int extent = PDFFluentWidgetsHelper::PASSWORD_LOCK;
    const QRect logical(PDFFluentWidgetsHelper::PASSWORD_LOCK_LEFT, (height() - extent) / 2, extent, extent);
    QPainter painter(this);
    PDFStylePainter::icon(&painter, QRectF(QStyle::visualRect(layoutDirection(), rect(), logical)), QStringLiteral("lock"), color);
}

void PDFLineEdit::updatePasswordFont()
{
    // The bullets of a hidden password are 2 px apart, the placeholder has no spacing
    QFont passwordFont = font();
    const qreal spacing = (echoMode() == QLineEdit::Password && !text().isEmpty()) ? 2.0 : 0.0;

    if (passwordFont.letterSpacing() != spacing || passwordFont.letterSpacingType() != QFont::AbsoluteSpacing)
    {
        passwordFont.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
        setFont(passwordFont);
    }
}

void PDFLineEdit::changeEvent(QEvent* event)
{
    QLineEdit::changeEvent(event);

    if (event->type() == QEvent::ReadOnlyChange)
    {
        updateColors();
    }
    else if (event->type() == QEvent::LayoutDirectionChange && m_isPasswordField)
    {
        setProperty(PDFWidgetStyleProperties::FIELD_TEXT_INSETS, QVariant::fromValue(PDFFluentWidgetsHelper::getPasswordInsets(layoutDirection())));
        placePasswordButton();
    }
}

void PDFLineEdit::init()
{
    updateColors();
}

void PDFLineEdit::updateColors()
{
    // Text of a read-only field is secondary
    PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
}

PDFTextEdit::PDFTextEdit(QWidget* parent) :
    QTextEdit(parent)
{
    init();
}

PDFTextEdit::PDFTextEdit(const QString& text, QWidget* parent) :
    QTextEdit(text, parent)
{
    init();
}

void PDFTextEdit::changeEvent(QEvent* event)
{
    QTextEdit::changeEvent(event);

    if (event->type() == QEvent::ReadOnlyChange)
    {
        PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
    }
}

void PDFTextEdit::init()
{
    // Paddings of the document: 12 at the sides, 10 above and below (inside of the border)
    document()->setDocumentMargin(10.0);
    setViewportMargins(2, 0, 2, 0);
    PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
}

/// Layout of a plain text with the lines of the design
class PDFPlainTextLayout : public QPlainTextDocumentLayout
{
public:
    explicit PDFPlainTextLayout(QTextDocument* document) :
        QPlainTextDocumentLayout(document)
    {

    }

    static constexpr qreal LINE_HEIGHT = 18.0;

    /// The height of a line (the default LINE_HEIGHT, the design of the editor)
    qreal getLineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal lineHeight) { m_lineHeight = lineHeight; }

    virtual QRectF blockBoundingRect(const QTextBlock& block) const override
    {
        const QRectF rect = QPlainTextDocumentLayout::blockBoundingRect(block);
        QTextLayout* layout = block.isValid() ? block.layout() : nullptr;
        const qreal lineHeight = m_lineHeight;

        if (!layout || !block.isVisible() || layout->lineCount() == 0)
        {
            return rect;
        }

        const int lineCount = layout->lineCount();
        for (int i = 0; i < lineCount; ++i)
        {
            if (layout->lineAt(i).height() > lineHeight)
            {
                // A larger font of the application keeps its own lines
                return rect;
            }
        }

        // The base class stacks the lines by the height of the font. They are
        // moved into the lines of the design, each of them is centered in its
        // line. The base class lays a block out again, when its text is changed,
        // and all users of the geometry of a block ask this function first.
        for (int i = 0; i < lineCount; ++i)
        {
            QTextLine line = layout->lineAt(i);
            const qreal y = i * lineHeight + qFloor((lineHeight - line.height()) / 2.0);

            if (line.position().y() != y)
            {
                line.setPosition(QPointF(line.position().x(), y));
            }
        }

        const qreal margin = block.next().isValid() ? 0.0 : document()->documentMargin();
        return QRectF(rect.left(), rect.top(), rect.width(), lineCount * lineHeight + margin);
    }

private:
    qreal m_lineHeight = LINE_HEIGHT;
};

PDFPlainTextEdit::PDFPlainTextEdit(QWidget* parent) :
    QPlainTextEdit(parent)
{
    init();
}

PDFPlainTextEdit::PDFPlainTextEdit(const QString& text, QWidget* parent) :
    QPlainTextEdit(parent)
{
    init();
    setPlainText(text);
}

qreal PDFPlainTextEdit::getLineHeight() const
{
    const PDFPlainTextLayout* layout = dynamic_cast<const PDFPlainTextLayout*>(document()->documentLayout());
    return layout ? layout->getLineHeight() : PDFPlainTextLayout::LINE_HEIGHT;
}

void PDFPlainTextEdit::setLineHeight(qreal lineHeight)
{
    PDFPlainTextLayout* layout = dynamic_cast<PDFPlainTextLayout*>(document()->documentLayout());
    if (layout && layout->getLineHeight() != lineHeight)
    {
        // The blocks are laid out again in the new lines
        layout->setLineHeight(lineHeight);
        document()->markContentsDirty(0, document()->characterCount());
        viewport()->update();
    }
}

void PDFPlainTextEdit::changeEvent(QEvent* event)
{
    QPlainTextEdit::changeEvent(event);

    if (event->type() == QEvent::ReadOnlyChange)
    {
        PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
    }
}

void PDFPlainTextEdit::paintEvent(QPaintEvent* event)
{
    // The editor draws the selection in the height of the lines of the font.
    // The selection of the design fills the whole line, so it is completed here
    // below the text.
    const QTextCursor cursor = textCursor();
    const qreal lineHeight = getLineHeight();

    if (cursor.hasSelection())
    {
        QPainter painter(viewport());
        const QColor color = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Highlight);
        const int selectionStart = cursor.selectionStart();
        const int selectionEnd = cursor.selectionEnd();
        const QPointF offset = contentOffset();

        for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next())
        {
            const QRectF geometry = blockBoundingGeometry(block).translated(offset);
            if (geometry.top() > event->rect().bottom())
            {
                break;
            }

            const QTextLayout* layout = block.layout();
            if (!block.isVisible() || !layout || block.position() >= selectionEnd || block.position() + block.length() <= selectionStart)
            {
                continue;
            }

            for (int i = 0; i < layout->lineCount(); ++i)
            {
                const QTextLine line = layout->lineAt(i);
                const int from = qMax(selectionStart, block.position() + line.textStart());
                const int to = qMin(selectionEnd, block.position() + line.textStart() + line.textLength());

                if (from >= to || line.height() > lineHeight)
                {
                    continue;
                }

                const qreal x1 = line.cursorToX(from - block.position());
                const qreal x2 = line.cursorToX(to - block.position());
                const qreal top = geometry.top() + line.y() - qFloor((lineHeight - line.height()) / 2.0);
                painter.fillRect(QRectF(geometry.left() + qMin(x1, x2), top, qAbs(x2 - x1), lineHeight), color);
            }
        }
    }

    QPlainTextEdit::paintEvent(event);
}

void PDFPlainTextEdit::mousePressEvent(QMouseEvent* event)
{
    handleMouseEvent(event);
}

void PDFPlainTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    handleMouseEvent(event);
}

void PDFPlainTextEdit::mouseReleaseEvent(QMouseEvent* event)
{
    handleMouseEvent(event);
}

void PDFPlainTextEdit::mouseDoubleClickEvent(QMouseEvent* event)
{
    handleMouseEvent(event);
}

void PDFPlainTextEdit::handleMouseEvent(QMouseEvent* event)
{
    // The editor gets the event with the pointer moved into a line of the text, see getLinePosition
    QMouseEvent lineEvent(event->type(), getLinePosition(event->position()), event->scenePosition(), event->globalPosition(),
                          event->button(), event->buttons(), event->modifiers(), event->pointingDevice());

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
            QPlainTextEdit::mousePressEvent(&lineEvent);
            break;

        case QEvent::MouseMove:
            QPlainTextEdit::mouseMoveEvent(&lineEvent);
            break;

        case QEvent::MouseButtonRelease:
            QPlainTextEdit::mouseReleaseEvent(&lineEvent);
            break;

        case QEvent::MouseButtonDblClick:
            QPlainTextEdit::mouseDoubleClickEvent(&lineEvent);
            break;

        default:
            break;
    }

    event->setAccepted(lineEvent.isAccepted());
}

QPointF PDFPlainTextEdit::getLinePosition(const QPointF& position) const
{
    // The lines of the font are lower than the lines of the design, in which they
    // are centered. The editor takes a point between two lines as a point behind
    // the end of the upper line, so the caret would jump to the end of a line in
    // the gaps. The point is moved into the line, in whose line of the design it lies.
    const QPointF offset = contentOffset();

    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next())
    {
        const QRectF geometry = blockBoundingGeometry(block).translated(offset);
        if (geometry.top() > position.y())
        {
            break;
        }

        const QTextLayout* layout = block.layout();
        if (!block.isVisible() || !layout || layout->lineCount() == 0 || position.y() >= geometry.bottom())
        {
            continue;
        }

        QTextLine line = layout->lineAt(0);
        for (int i = 1; i < layout->lineCount() && geometry.top() + layout->lineAt(i).y() - 1.0 <= position.y(); ++i)
        {
            line = layout->lineAt(i);
        }

        // The editor tests the rectangle of the text of the line, which is lower
        // than the rounded height of the line
        const QRectF lineRect = line.naturalTextRect().translated(0.0, geometry.top());
        return QPointF(position.x(), qBound(lineRect.top(), position.y(), qMax(lineRect.top(), lineRect.bottom() - 0.5)));
    }

    return position;
}

void PDFPlainTextEdit::init()
{
    // The editor remembers the layout of its document, so the layout with the
    // lines of the design comes with a new document
    QTextDocument* textDocument = new QTextDocument(this);
    textDocument->setDocumentLayout(new PDFPlainTextLayout(textDocument));
    setDocument(textDocument);

    document()->setDocumentMargin(10.0);
    setViewportMargins(2, 0, 2, 0);
    PDFThemeWidgetHelper::bindTextPalette(this, isReadOnly() ? PDFThemeColors::TextSecondary : PDFThemeColors::TextPrimary);
}

#if QT_CONFIG(accessibility)
/// Spin box of the library (PDFSpinBox, PDFDoubleSpinBox) for accessibility: the interface of
/// QAccessibleSpinBox / QAccessibleDoubleSpinBox of Qt 6.11 (rangecontrols.cpp: the role, the
/// value, the text of the editor forwarded as QAccessibleLineEdit has it), except the mixed state:
/// the value is "Mixed" and there is no value interface - the held value of one of the selected
/// objects is not presented as their common value (UIA RangeValue, NVDA)
class PDFSpinBoxAccessible : public QAccessibleWidgetV2, public QAccessibleValueInterface, public QAccessibleTextInterface, public QAccessibleEditableTextInterface
{
public:
    explicit PDFSpinBoxAccessible(QAbstractSpinBox* spinBox) :
        QAccessibleWidgetV2(spinBox, QAccessible::SpinBox)
    {
        addControllingSignal(qobject_cast<QDoubleSpinBox*>(spinBox) ? QStringLiteral("valueChanged(double)") : QStringLiteral("valueChanged(int)"));
    }

    QAbstractSpinBox* spinBox() const { return qobject_cast<QAbstractSpinBox*>(object()); }

    /// The editor of the spin box (QAbstractSpinBox::lineEdit is protected)
    QLineEdit* editor() const
    {
        const QAbstractSpinBox* box = spinBox();
        return box ? box->findChild<QLineEdit*>(QStringLiteral("qt_spinbox_lineedit"), Qt::FindDirectChildrenOnly) : nullptr;
    }

    bool isMixed() const
    {
        if (const PDFSpinBox* box = qobject_cast<const PDFSpinBox*>(object()))
        {
            return box->isMixed();
        }
        if (const PDFDoubleSpinBox* box = qobject_cast<const PDFDoubleSpinBox*>(object()))
        {
            return box->isMixed();
        }
        return false;
    }

    virtual QAccessible::State state() const override
    {
        QAccessible::State state = QAccessibleWidgetV2::state();
        const QAbstractSpinBox* box = spinBox();
        if (box && box->isReadOnly())
        {
            state.readOnly = true;
        }
        else
        {
            state.editable = true;
        }
        return state;
    }

    virtual QString text(QAccessible::Text textType) const override
    {
        if (textType != QAccessible::Value)
        {
            return QAccessibleWidgetV2::text(textType);
        }

        if (isMixed())
        {
            return PDFFluentWidgetsHelper::getMixedText();
        }
        if (const PDFDoubleSpinBox* box = qobject_cast<const PDFDoubleSpinBox*>(object()))
        {
            return box->textFromValue(box->value());
        }
        const QAbstractSpinBox* box = spinBox();
        return box ? box->text() : QString();
    }

    virtual void* interface_cast(QAccessible::InterfaceType type) override
    {
        switch (type)
        {
            case QAccessible::ValueInterface:
                return isMixed() ? nullptr : static_cast<QAccessibleValueInterface*>(this);
            case QAccessible::TextInterface:
                return static_cast<QAccessibleTextInterface*>(this);
            case QAccessible::EditableTextInterface:
                return static_cast<QAccessibleEditableTextInterface*>(this);
            default:
                return QAccessibleWidgetV2::interface_cast(type);
        }
    }

    // QAccessibleValueInterface (QAccessibleAbstractSpinBox: the properties of the spin box)
    virtual QVariant currentValue() const override { return object()->property("value"); }
    virtual void setCurrentValue(const QVariant& value) override { object()->setProperty("value", value); }
    virtual QVariant maximumValue() const override { return object()->property("maximum"); }
    virtual QVariant minimumValue() const override { return object()->property("minimum"); }
    virtual QVariant minimumStepSize() const override { return object()->property("singleStep"); }

    // QAccessibleTextInterface (QAccessibleLineEdit over the editor)
    virtual void selection(int selectionIndex, int* startOffset, int* endOffset) const override
    {
        *startOffset = *endOffset = 0;
        const QLineEdit* lineEdit = editor();
        if (selectionIndex == 0 && lineEdit)
        {
            *startOffset = lineEdit->selectionStart();
            *endOffset = *startOffset + int(lineEdit->selectedText().size());
        }
    }

    virtual int selectionCount() const override
    {
        const QLineEdit* lineEdit = editor();
        return lineEdit && lineEdit->hasSelectedText() ? 1 : 0;
    }

    virtual void addSelection(int startOffset, int endOffset) override { setSelection(0, startOffset, endOffset); }

    virtual void removeSelection(int selectionIndex) override
    {
        QLineEdit* lineEdit = editor();
        if (selectionIndex == 0 && lineEdit)
        {
            lineEdit->deselect();
        }
    }

    virtual void setSelection(int selectionIndex, int startOffset, int endOffset) override
    {
        QLineEdit* lineEdit = editor();
        if (selectionIndex == 0 && lineEdit)
        {
            lineEdit->setSelection(startOffset, endOffset - startOffset);
        }
    }

    virtual int cursorPosition() const override
    {
        const QLineEdit* lineEdit = editor();
        return lineEdit ? lineEdit->cursorPosition() : 0;
    }

    virtual void setCursorPosition(int position) override
    {
        if (QLineEdit* lineEdit = editor())
        {
            lineEdit->setCursorPosition(position);
        }
    }

    virtual QString text(int startOffset, int endOffset) const override
    {
        const QLineEdit* lineEdit = editor();
        if (!lineEdit || startOffset > endOffset)
        {
            return QString();
        }
        return lineEdit->displayText().mid(startOffset, endOffset - startOffset);
    }

    virtual QString textBeforeOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset, int* endOffset) const override
    {
        return QAccessibleTextInterface::textBeforeOffset(offset == -2 ? cursorPosition() : offset, boundaryType, startOffset, endOffset);
    }

    virtual QString textAfterOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset, int* endOffset) const override
    {
        return QAccessibleTextInterface::textAfterOffset(offset == -2 ? cursorPosition() : offset, boundaryType, startOffset, endOffset);
    }

    virtual QString textAtOffset(int offset, QAccessible::TextBoundaryType boundaryType, int* startOffset, int* endOffset) const override
    {
        return QAccessibleTextInterface::textAtOffset(offset == -2 ? cursorPosition() : offset, boundaryType, startOffset, endOffset);
    }

    virtual int characterCount() const override
    {
        const QLineEdit* lineEdit = editor();
        return lineEdit ? int(lineEdit->displayText().size()) : 0;
    }

    virtual QRect characterRect(int offset) const override
    {
        // As QAccessibleLineEdit: the advance of the text before the character (the position of
        // the cursor of a line of plain text), the top margin of the text, in global coordinates
        const QLineEdit* lineEdit = editor();
        const QString character = text(offset, offset + 1);
        if (!lineEdit || character.isEmpty())
        {
            return QRect();
        }

        const QFontMetrics metrics(lineEdit->font());
        const int x = metrics.horizontalAdvance(lineEdit->displayText().left(offset));
        QRect rect(x, lineEdit->textMargins().top(), metrics.horizontalAdvance(character), metrics.height());
        rect.moveTo(lineEdit->mapToGlobal(rect.topLeft()));
        return rect;
    }

    virtual int offsetAtPoint(const QPoint& point) const override
    {
        QLineEdit* lineEdit = editor();
        return lineEdit ? lineEdit->cursorPositionAt(lineEdit->mapFromGlobal(point)) : -1;
    }

    virtual void scrollToSubstring(int startIndex, int endIndex) override
    {
        setCursorPosition(endIndex);
        setCursorPosition(startIndex);
    }

    virtual QString attributes(int offset, int* startOffset, int* endOffset) const override
    {
        // A line edit has no text attributes
        *startOffset = *endOffset = offset;
        return QString();
    }

    // QAccessibleEditableTextInterface (QAccessibleLineEdit)
    virtual void deleteText(int startOffset, int endOffset) override
    {
        if (QLineEdit* lineEdit = editor())
        {
            lineEdit->setText(lineEdit->text().remove(startOffset, endOffset - startOffset));
        }
    }

    virtual void insertText(int offset, const QString& text) override
    {
        if (QLineEdit* lineEdit = editor())
        {
            lineEdit->setText(lineEdit->text().insert(offset, text));
        }
    }

    virtual void replaceText(int startOffset, int endOffset, const QString& text) override
    {
        if (QLineEdit* lineEdit = editor())
        {
            lineEdit->setText(lineEdit->text().replace(startOffset, endOffset - startOffset, text));
        }
    }

    static QAccessibleInterface* create(const QString& className, QObject* object)
    {
        Q_UNUSED(className);

        // A spin box in its destructor is a QWidget already (no cast succeeds)
        if (qobject_cast<PDFSpinBox*>(object) || qobject_cast<PDFDoubleSpinBox*>(object))
        {
            return new PDFSpinBoxAccessible(static_cast<QAbstractSpinBox*>(object));
        }
        return nullptr;
    }

    /// Installs the factory once for the whole application
    static void ensureFactory()
    {
        static const bool isFactoryInstalled = installFactory();
        Q_UNUSED(isFactoryInstalled);
    }

private:
    static bool installFactory()
    {
        QAccessible::installFactory(&PDFSpinBoxAccessible::create);
        return true;
    }
};
#endif

PDFSpinBox::PDFSpinBox(QWidget* parent) :
    QSpinBox(parent)
{
#if QT_CONFIG(accessibility)
    PDFSpinBoxAccessible::ensureFactory();
#endif

    PDFThemeWidgetHelper::bindTextPalette(lineEdit());

    // The mixed state is left by a text typed into the editor; a text of the user is an entry
    connect(lineEdit(), &QLineEdit::textEdited, this, &PDFSpinBox::onEditorTextEdited);
    connect(lineEdit(), &QLineEdit::textChanged, this, &PDFSpinBox::onEditorTextChanged);
}

bool PDFSpinBox::isMixed() const
{
    PDFFluentWidgetsHelper::syncMixed(lineEdit(), m_mixed, prefix() + suffix());
    return m_mixed.isMixed;
}

void PDFSpinBox::setMixed(bool mixed)
{
    if (isMixed() == mixed)
    {
        return;
    }

    if (mixed)
    {
        PDFFluentWidgetsHelper::enterMixed(this, lineEdit(), m_mixed);
    }
    else
    {
        m_mixed.isMixed = false;
        m_mixed.isLeftByText = false;
        PDFFluentWidgetsHelper::restorePlaceholder(lineEdit(), m_mixed);

        // The editor shows the held value again: setValue of the same value emits nothing
        setValue(value());
        PDFFluentWidgetsHelper::notifyValueChanged(this, value());
    }
}

void PDFSpinBox::commitEntry()
{
    PDFFluentWidgetsHelper::finishMixedEntry(this, lineEdit(), m_mixed, prefix() + suffix());
    if (!lineEdit() || !lineEdit()->isModified() || isMixed())
    {
        return;
    }

    // Qt interprets the text (valueChanged of a changed value), then the entry
    const QPointer<PDFSpinBox> guard(this);
    interpretText();
    if (guard)
    {
        m_entry.isCommitPending = true;
        emitPendingEntry();
    }
}

void PDFSpinBox::stepBy(int steps)
{
    // A step in the mixed state steps from the held value (QAbstractSpinBox would not
    // step from the empty text, it would only show the held value)
    if (isMixed())
    {
        setMixed(false);
    }

    // A step of an event (the keys, the wheel, the buttons, the menu) is an entry
    PDFFluentWidgetsHelper::requestEntry(m_entry);
    QSpinBox::stepBy(steps);
}

bool PDFSpinBox::event(QEvent* event)
{
    // An entry is signalled, when Qt has handled the outermost event of the spin box,
    // which has made it: Qt uses the spin box after a step and after its own signals,
    // a slot of valueEntered may delete it
    const QPointer<PDFSpinBox> guard(this);
    ++m_entry.eventDepth;
    const bool result = QSpinBox::event(event);
    if (guard)
    {
        --m_entry.eventDepth;
        emitPendingEntry();
    }
    return result;
}

void PDFSpinBox::emitPendingEntry()
{
    if (PDFFluentWidgetsHelper::takePendingEntry(this, lineEdit(), m_entry, isMixed()))
    {
        Q_EMIT valueEntered(value());
    }
}

void PDFSpinBox::keyPressEvent(QKeyEvent* event)
{
    const QString affixes = prefix() + suffix();
    const bool isEnter = event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return;
    if (isEnter)
    {
        PDFFluentWidgetsHelper::finishMixedEntry(this, lineEdit(), m_mixed, affixes);
        PDFFluentWidgetsHelper::requestEntry(m_entry);
    }

    // The empty editor of the mixed state: nothing to select
    if (event == QKeySequence::SelectAll && isMixed() && PDFFluentWidgetsHelper::isMixedEditorEmpty(lineEdit(), m_mixed))
    {
        event->accept();
        return;
    }

    if (isEnter && PDFFluentWidgetsHelper::showQtAffixes(lineEdit(), m_mixed, suffix(), affixes))
    {
        const QPointer<PDFSpinBox> guard(this);
        QSpinBox::keyPressEvent(event);
        if (guard)
        {
            PDFFluentWidgetsHelper::hideQtAffixes(lineEdit(), m_mixed, affixes);
        }
        return;
    }

    QSpinBox::keyPressEvent(event);
}

void PDFSpinBox::focusInEvent(QFocusEvent* event)
{
    // QAbstractSpinBox selects the text between its affixes for the keyboard, which the
    // empty editor of the mixed state does not have
    if (isMixed() && PDFFluentWidgetsHelper::isSelectionOutsideOfEditor(lineEdit(), m_mixed, suffix()) &&
        (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason))
    {
        QFocusEvent focusIn(event->type(), Qt::OtherFocusReason);
        QSpinBox::focusInEvent(&focusIn);
        return;
    }
    QSpinBox::focusInEvent(event);
}

void PDFSpinBox::focusOutEvent(QFocusEvent* event)
{
    // A text typed by the user is committed (an abandoned entry is mixed again, its editor
    // is not modified)
    PDFFluentWidgetsHelper::finishMixedEntry(this, lineEdit(), m_mixed, prefix() + suffix());
    if (lineEdit() && lineEdit()->isModified())
    {
        PDFFluentWidgetsHelper::requestEntry(m_entry);
    }
    QSpinBox::focusOutEvent(event);
}

void PDFSpinBox::changeEvent(QEvent* event)
{
    QSpinBox::changeEvent(event);

    if (event->type() == QEvent::LanguageChange && isMixed() && lineEdit())
    {
        lineEdit()->setPlaceholderText(PDFFluentWidgetsHelper::getMixedText());
    }
}

void PDFSpinBox::onEditorTextChanged(const QString& text)
{
    PDFFluentWidgetsHelper::onMixedEditorTextChanged(lineEdit(), m_mixed, text, prefix() + suffix());
}

void PDFSpinBox::onEditorTextEdited()
{
    PDFFluentWidgetsHelper::markEditorModified(lineEdit());
}

void PDFSpinBox::setDisplayPrefix(const QString& prefix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, prefix, m_displaySuffix);
    m_displayPrefix = prefix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::PREFIX_WIDTH, prefix);
}

void PDFSpinBox::setDisplaySuffix(const QString& suffix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, m_displayPrefix, suffix);
    m_displaySuffix = suffix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::SUFFIX_WIDTH, suffix);
}

void PDFSpinBox::paintEvent(QPaintEvent* event)
{
    QSpinBox::paintEvent(event);
    PDFFluentWidgetsHelper::paintAffixes(this, m_displayPrefix, m_displaySuffix);
}

void PDFSpinBox::contextMenuEvent(QContextMenuEvent* event)
{
    QMouseEvent release = PDFFluentWidgetsHelper::createSpinBoxRelease();
    QSpinBox::mouseReleaseEvent(&release);
    const bool isEditorSelected = isMixed() && PDFFluentWidgetsHelper::isMixedEditorEmpty(lineEdit(), m_mixed);
    PDFFluentWidgetsHelper::showSpinBoxContextMenu(this, lineEdit(), stepEnabled(), event, isEditorSelected);
}

PDFDoubleSpinBox::PDFDoubleSpinBox(QWidget* parent) :
    QDoubleSpinBox(parent)
{
#if QT_CONFIG(accessibility)
    PDFSpinBoxAccessible::ensureFactory();
#endif

    PDFThemeWidgetHelper::bindTextPalette(lineEdit());

    // The mixed state is left by a text typed into the editor; a text of the user is an entry
    connect(lineEdit(), &QLineEdit::textEdited, this, &PDFDoubleSpinBox::onEditorTextEdited);
    connect(lineEdit(), &QLineEdit::textChanged, this, &PDFDoubleSpinBox::onEditorTextChanged);
}

bool PDFDoubleSpinBox::isMixed() const
{
    PDFFluentWidgetsHelper::syncMixed(lineEdit(), m_mixed, prefix() + suffix());
    return m_mixed.isMixed;
}

void PDFDoubleSpinBox::setMixed(bool mixed)
{
    if (isMixed() == mixed)
    {
        return;
    }

    if (mixed)
    {
        PDFFluentWidgetsHelper::enterMixed(this, lineEdit(), m_mixed);
    }
    else
    {
        m_mixed.isMixed = false;
        m_mixed.isLeftByText = false;
        PDFFluentWidgetsHelper::restorePlaceholder(lineEdit(), m_mixed);

        // The editor shows the held value again: setValue of the same value emits nothing
        setValue(value());
        PDFFluentWidgetsHelper::notifyValueChanged(this, value());
    }
}

void PDFDoubleSpinBox::commitEntry()
{
    PDFFluentWidgetsHelper::finishMixedEntry(this, lineEdit(), m_mixed, prefix() + suffix());
    if (!lineEdit() || !lineEdit()->isModified() || isMixed())
    {
        return;
    }

    // Qt interprets the text (valueChanged of a changed value), then the entry
    const QPointer<PDFDoubleSpinBox> guard(this);
    interpretText();
    if (guard)
    {
        m_entry.isCommitPending = true;
        emitPendingEntry();
    }
}

void PDFDoubleSpinBox::stepBy(int steps)
{
    // A step in the mixed state steps from the held value (QAbstractSpinBox would not
    // step from the empty text, it would only show the held value)
    if (isMixed())
    {
        setMixed(false);
    }

    // A step of an event (the keys, the wheel, the buttons, the menu) is an entry
    PDFFluentWidgetsHelper::requestEntry(m_entry);
    QDoubleSpinBox::stepBy(steps);
}

bool PDFDoubleSpinBox::event(QEvent* event)
{
    // See PDFSpinBox::event: valueEntered at the end of the outermost event
    const QPointer<PDFDoubleSpinBox> guard(this);
    ++m_entry.eventDepth;
    const bool result = QDoubleSpinBox::event(event);
    if (guard)
    {
        --m_entry.eventDepth;
        emitPendingEntry();
    }
    return result;
}

void PDFDoubleSpinBox::emitPendingEntry()
{
    if (PDFFluentWidgetsHelper::takePendingEntry(this, lineEdit(), m_entry, isMixed()))
    {
        Q_EMIT valueEntered(value());
    }
}

void PDFDoubleSpinBox::keyPressEvent(QKeyEvent* event)
{
    const QString affixes = prefix() + suffix();
    const bool isEnter = event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return;
    if (isEnter)
    {
        PDFFluentWidgetsHelper::finishMixedEntry(this, lineEdit(), m_mixed, affixes);
        PDFFluentWidgetsHelper::requestEntry(m_entry);
    }

    // The empty editor of the mixed state: nothing to select
    if (event == QKeySequence::SelectAll && isMixed() && PDFFluentWidgetsHelper::isMixedEditorEmpty(lineEdit(), m_mixed))
    {
        event->accept();
        return;
    }

    if (isEnter && PDFFluentWidgetsHelper::showQtAffixes(lineEdit(), m_mixed, suffix(), affixes))
    {
        const QPointer<PDFDoubleSpinBox> guard(this);
        QDoubleSpinBox::keyPressEvent(event);
        if (guard)
        {
            PDFFluentWidgetsHelper::hideQtAffixes(lineEdit(), m_mixed, affixes);
        }
        return;
    }

    QDoubleSpinBox::keyPressEvent(event);
}

void PDFDoubleSpinBox::focusInEvent(QFocusEvent* event)
{
    // See PDFSpinBox::focusInEvent
    if (isMixed() && PDFFluentWidgetsHelper::isSelectionOutsideOfEditor(lineEdit(), m_mixed, suffix()) &&
        (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason))
    {
        QFocusEvent focusIn(event->type(), Qt::OtherFocusReason);
        QDoubleSpinBox::focusInEvent(&focusIn);
        return;
    }
    QDoubleSpinBox::focusInEvent(event);
}

void PDFDoubleSpinBox::focusOutEvent(QFocusEvent* event)
{
    // See PDFSpinBox::focusOutEvent
    PDFFluentWidgetsHelper::finishMixedEntry(this, lineEdit(), m_mixed, prefix() + suffix());
    if (lineEdit() && lineEdit()->isModified())
    {
        PDFFluentWidgetsHelper::requestEntry(m_entry);
    }
    QDoubleSpinBox::focusOutEvent(event);
}

void PDFDoubleSpinBox::changeEvent(QEvent* event)
{
    QDoubleSpinBox::changeEvent(event);

    if (event->type() == QEvent::LanguageChange && isMixed() && lineEdit())
    {
        lineEdit()->setPlaceholderText(PDFFluentWidgetsHelper::getMixedText());
    }
}

void PDFDoubleSpinBox::onEditorTextChanged(const QString& text)
{
    PDFFluentWidgetsHelper::onMixedEditorTextChanged(lineEdit(), m_mixed, text, prefix() + suffix());
}

void PDFDoubleSpinBox::onEditorTextEdited()
{
    PDFFluentWidgetsHelper::markEditorModified(lineEdit());
}

void PDFDoubleSpinBox::setDisplayPrefix(const QString& prefix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, prefix, m_displaySuffix);
    m_displayPrefix = prefix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::PREFIX_WIDTH, prefix);
}

void PDFDoubleSpinBox::setDisplaySuffix(const QString& suffix)
{
    PDFFluentWidgetsHelper::setAffixDescription(this, m_displayPrefix, m_displaySuffix, m_displayPrefix, suffix);
    m_displaySuffix = suffix;
    PDFFluentWidgetsHelper::setAffix(this, PDFWidgetStyleProperties::SUFFIX_WIDTH, suffix);
}

void PDFDoubleSpinBox::paintEvent(QPaintEvent* event)
{
    QDoubleSpinBox::paintEvent(event);
    PDFFluentWidgetsHelper::paintAffixes(this, m_displayPrefix, m_displaySuffix);
}

void PDFDoubleSpinBox::contextMenuEvent(QContextMenuEvent* event)
{
    QMouseEvent release = PDFFluentWidgetsHelper::createSpinBoxRelease();
    QDoubleSpinBox::mouseReleaseEvent(&release);
    const bool isEditorSelected = isMixed() && PDFFluentWidgetsHelper::isMixedEditorEmpty(lineEdit(), m_mixed);
    PDFFluentWidgetsHelper::showSpinBoxContextMenu(this, lineEdit(), stepEnabled(), event, isEditorSelected);
}

void PDFDoubleSpinBox::setTrailingZerosHidden(bool isHidden)
{
    if (m_isTrailingZerosHidden != isHidden)
    {
        m_isTrailingZerosHidden = isHidden;

        // The text of the value is made again (setValue would not change an equal value)
        if (QLineEdit* edit = lineEdit())
        {
            const QSignalBlocker blocker(edit);
            edit->setText(textFromValue(value()));
        }
    }
}

QString PDFDoubleSpinBox::textFromValue(double value) const
{
    QString text = QDoubleSpinBox::textFromValue(value);
    if (!m_isTrailingZerosHidden || decimals() <= 0)
    {
        return text;
    }

    // The fraction of the locale ("13.0" -> "13", "2.50" -> "2.5", "1 234,0" -> "1 234")
    const QString decimalPoint = locale().decimalPoint();
    const qsizetype pointIndex = text.lastIndexOf(decimalPoint);
    if (pointIndex < 0)
    {
        return text;
    }
    qsizetype end = text.size();
    while (end > pointIndex + decimalPoint.size() && text.at(end - 1) == locale().zeroDigit().front())
    {
        --end;
    }
    if (end == pointIndex + decimalPoint.size())
    {
        end = pointIndex;
    }
    text.truncate(end);
    return text;
}

#if QT_CONFIG(accessibility)
/// Button of the month of PDFCalendarWidget: a tool button of QCalendarWidget without a menu
/// of its own (the calendar opens the menu of the months by popup()), announced as
/// QAccessibleToolButton announces a tool button with an instant menu - the role ButtonMenu,
/// it has a popup, the action Show Menu only (it opens the menu by the calendar), the menu
/// as its child 0, the name and the accelerator of QAccessibleButton
class PDFCalendarMonthButtonAccessible : public QAccessibleWidget
{
public:
    explicit PDFCalendarMonthButtonAccessible(QToolButton* button) :
        QAccessibleWidget(button, QAccessible::ButtonMenu)
    {
        // As QAccessibleButton for a button, which is not checkable
        addControllingSignal(QStringLiteral("clicked()"));
    }

    QToolButton* button() const { return qobject_cast<QToolButton*>(widget()); }

    /// The calendar of the button (nullptr, if the button is no button of the month of PDFCalendarWidget)
    static PDFCalendarWidget* getCalendar(const QToolButton* button)
    {
        if (!button || button->objectName() != QLatin1String("qt_calendar_monthbutton"))
        {
            return nullptr;
        }

        for (QWidget* parent = button->parentWidget(); parent; parent = parent->parentWidget())
        {
            if (PDFCalendarWidget* calendar = qobject_cast<PDFCalendarWidget*>(parent))
            {
                return calendar;
            }
        }
        return nullptr;
    }

    virtual QAccessible::State state() const override
    {
        // QAccessibleButton::state + QAccessibleToolButton::state with a menu
        QAccessible::State state = QAccessibleWidget::state();
        if (const QToolButton* toolButton = button())
        {
            state.checkable = toolButton->isCheckable();
            state.checked = toolButton->isChecked();
            state.pressed = toolButton->isDown();
            state.hotTracked = toolButton->autoRaise();
        }
        state.hasPopup = true;
        return state;
    }

    virtual QString text(QAccessible::Text textType) const override
    {
        // QAccessibleButton::text: the accessible name or the text without its mnemonic, the
        // mnemonic of the text as the accelerator
        QString text;
        const QToolButton* toolButton = button();
        if (toolButton && textType == QAccessible::Name)
        {
            text = toolButton->accessibleName();
            if (text.isEmpty())
            {
                text = toolButton->text();
                text.remove(QRegularExpression(QStringLiteral("&(?!&)")));
            }
        }
        else if (toolButton && textType == QAccessible::Accelerator)
        {
            text = QKeySequence::mnemonic(toolButton->text()).toString(QKeySequence::NativeText);
        }

        return text.isEmpty() ? QAccessibleWidget::text(textType) : text;
    }

    virtual int childCount() const override
    {
        // No split button: no children (QAccessibleToolButton::childCount)
        return 0;
    }

    virtual QAccessibleInterface* child(int index) const override
    {
        // The menu as the child 0 (QAccessibleToolButton::child)
        const PDFCalendarWidget* calendar = getCalendar(button());
        QMenu* menu = calendar ? calendar->getMonthMenu() : nullptr;
        return index == 0 && menu ? QAccessible::queryAccessibleInterface(menu) : nullptr;
    }

    virtual QStringList actionNames() const override
    {
        // An instant menu: Show Menu only, while the button is enabled
        QStringList names;
        if (widget()->isEnabled())
        {
            names << showMenuAction();
        }
        return names;
    }

    virtual void doAction(const QString& actionName) override
    {
        // Show Menu opens the menu of the months by popup() of the calendar; the last call
        PDFCalendarWidget* calendar = getCalendar(button());
        if (calendar && actionName == showMenuAction())
        {
            calendar->showMonthMenu();
        }
    }

    virtual QStringList keyBindingsForAction(const QString& actionName) const override
    {
        Q_UNUSED(actionName);
        return QStringList();
    }

    static QAccessibleInterface* create(const QString& className, QObject* object)
    {
        Q_UNUSED(className);

        QToolButton* toolButton = qobject_cast<QToolButton*>(object);
        return getCalendar(toolButton) ? new PDFCalendarMonthButtonAccessible(toolButton) : nullptr;
    }

    static bool installFactory()
    {
        QAccessible::installFactory(&PDFCalendarMonthButtonAccessible::create);
        return true;
    }
};
#endif

PDFCalendarWidget::PDFCalendarWidget(QWidget* parent) :
    QCalendarWidget(parent)
{
    setGridVisible(false);
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);

    if (layout())
    {
        // Navigation of 32 px, the grid of days 6 px below it
        layout()->setSpacing(6);
    }

    if (QTableView* view = findChild<QTableView*>(QStringLiteral("qt_calendar_calendarview")))
    {
        // Columns of 30 px, the row of the names of days 22 px, rows of days 30 px
        // (shapes of the days of 28 px, which are 2 px apart)
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        view->horizontalHeader()->setMinimumSectionSize(0);
        view->horizontalHeader()->setDefaultSectionSize(30);
        view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        view->verticalHeader()->setMinimumSectionSize(0);
        view->verticalHeader()->setDefaultSectionSize(30);

        // Sections have got their sizes, while they were stretched
        for (int column = 0; column < view->horizontalHeader()->count(); ++column)
        {
            view->horizontalHeader()->resizeSection(column, 30);
        }
        for (int row = 0; row < view->verticalHeader()->count(); ++row)
        {
            view->verticalHeader()->resizeSection(row, row == 0 ? 22 : 30);
        }
        view->setFrameShape(QFrame::NoFrame);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_viewport = view->viewport();
        m_viewport->setMouseTracking(true);
        m_viewport->installEventFilter(this);
    }

    if (QWidget* navigationBar = findChild<QWidget*>(QStringLiteral("qt_calendar_navigationbar")))
    {
        // The navigation is 32 px high, its buttons of 28 px are centered in it
        navigationBar->setFixedHeight(32);
    }

    for (const char* name : { "qt_calendar_prevmonth", "qt_calendar_nextmonth" })
    {
        if (QToolButton* button = findChild<QToolButton*>(QString::fromLatin1(name)))
        {
            button->setFixedSize(28, 28);
            button->setIconSize(QSize(16, 16));
            button->setProperty(PDFWidgetStyleProperties::RADIUS, 8);
        }
    }

    for (const char* name : { "qt_calendar_monthbutton", "qt_calendar_yearbutton" })
    {
        if (QToolButton* button = findChild<QToolButton*>(QString::fromLatin1(name)))
        {
            // The calendar gives a bold font to these buttons, the design has Medium 13.
            // Both of them open a popup, both show the chevron by the property (the year has
            // no menu, the month gives its menu to the calendar below).
            button->setFixedHeight(28);
            button->setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::BodyMedium));
            button->setProperty(PDFWidgetStyleProperties::RADIUS, 8);
            button->setProperty(PDFWidgetStyleProperties::MENU_INDICATOR, true);
        }
    }

    // A0: the menu of the months is the instant menu of the button of the month, which
    // QToolButton::showMenu shows by QMenu::exec (a nested event loop). The button keeps no
    // menu; its press opens the same menu by popup(), QCalendarWidget still reacts by the
    // signal triggered of the menu. The menu stays a child of the button.
    m_monthButton = findChild<QToolButton*>(QStringLiteral("qt_calendar_monthbutton"));
    if (m_monthButton && m_monthButton->menu())
    {
#if QT_CONFIG(accessibility)
        // Announced as Qt announces a tool button with an instant menu
        static const bool isFactoryInstalled = PDFCalendarMonthButtonAccessible::installFactory();
        Q_UNUSED(isFactoryInstalled);
#endif
        m_monthMenu = m_monthButton->menu();
        m_monthButton->setMenu(nullptr);
#if QT_CONFIG(accessibility)
        // An interface of Qt created during the construction of QCalendarWidget (an active
        // assistive technology) would announce a push button: it is replaced by the own one
        if (QAccessible::isActive())
        {
            QAccessibleInterface* buttonInterface = QAccessible::queryAccessibleInterface(m_monthButton.data());
            if (buttonInterface && !dynamic_cast<PDFCalendarMonthButtonAccessible*>(buttonInterface))
            {
                QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(buttonInterface));
            }
        }
#endif
        connect(m_monthButton.data(), &QAbstractButton::pressed, this, &PDFCalendarWidget::onMonthButtonPressed);
        m_monthMenuHiddenConnection = connect(m_monthMenu.data(), &QMenu::aboutToHide, this, &PDFCalendarWidget::onMonthMenuHidden);

        // The end of the menu (retired or deleted; a retired menu sends no aboutToHide): its notifier is a
        // descendant of the calendar, so the connection is a plain one
        m_monthMenuDeletedConnection = connect(PDFDeferredDeletion::getNotifier(m_monthMenu.data()), &PDFLifetimeNotifier::aboutToBeDeleted, this, &PDFCalendarWidget::onMonthMenuAboutToBeDeleted);
    }

    // The editor of the year is a QSpinBox of Qt: its context menu (QAbstractSpinBox shows it by
    // QMenu::exec) is the menu of the spin boxes of the library, shown by popup()
    m_yearEdit = findChild<QSpinBox*>(QStringLiteral("qt_calendar_yearedit"));
    if (m_yearEdit)
    {
        PDFThemeWidgetHelper::bindTextPalette(m_yearEdit->findChild<QLineEdit*>());
        m_yearEdit->installEventFilter(this);
    }

    updateColors();
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFCalendarWidget::updateColors);
}

PDFCalendarWidget::~PDFCalendarWidget()
{
    // The menu of the months is deleted with the button by ~QWidget, when the calendar is
    // only a QWidget: its signals must not reach the calendar any more
    disconnect(m_monthMenuHiddenConnection);
    disconnect(m_monthMenuDeletedConnection);
}

QMenu* PDFCalendarWidget::getMonthMenu() const
{
    return m_monthMenu.data();
}

bool PDFCalendarWidget::showMonthMenu()
{
    QToolButton* button = m_monthButton.data();
    QMenu* menu = m_monthMenu.data();
    if (!button || !menu || !button->isEnabled())
    {
        return false;
    }

    // The press, which closes the menu over the button, does not open it again; the button
    // looks as a button of an open menu, until the menu is hidden (QToolButton::showMenu)
    menu->setNoReplayFor(button);
    button->setProperty(PDFWidgetStyleProperties::MENU_OPEN, true);
    button->setDown(true);

    QPointer<PDFCalendarWidget> guard(this);
    menu->popup(PDFFluentWidgetsHelper::getToolButtonMenuPosition(button, menu->sizeHint()));
    if (guard && (!m_monthMenu || !m_monthMenu->isVisible()))
    {
        // An empty menu is not shown and it is never hidden
        onMonthMenuHidden();
    }
    return true;
}

void PDFCalendarWidget::onMonthButtonPressed()
{
    showMonthMenu();
}

void PDFCalendarWidget::onMonthMenuHidden()
{
    // Only the state of the button (no code of the application runs here)
    if (m_monthButton)
    {
        m_monthButton->setProperty(PDFWidgetStyleProperties::MENU_OPEN, false);
        if (m_monthButton->isDown())
        {
            m_monthButton->setDown(false);
        }
        else
        {
            m_monthButton->update();
        }
    }
}

void PDFCalendarWidget::onMonthMenuAboutToBeDeleted()
{
    // A calendar retired with its menu needs nothing (the retire hides it)
    if (!PDFDeferredDeletion::isRetired(this))
    {
        onMonthMenuHidden();
    }
}

QSize PDFCalendarWidget::sizeHint() const
{
    // Seven columns of 30 px; navigation 32, gap 6, names of days 22, six rows of 30 px
    return QSize(7 * 30, 32 + 6 + 22 + 6 * 30);
}

QSize PDFCalendarWidget::minimumSizeHint() const
{
    return sizeHint();
}

void PDFCalendarWidget::paintCell(QPainter* painter, const QRect& rect, QDate date) const
{
    const bool isDateEnabled = isEnabled() && date >= minimumDate() && date <= maximumDate();
    const bool isSelected = date == selectedDate();
    const bool isToday = date == QDate::currentDate();
    const bool isOtherMonth = date.month() != monthShown() || date.year() != yearShown();
    const bool isHovered = isDateEnabled && m_viewport && m_viewport->underMouse() && rect.contains(m_viewport->mapFromGlobal(QCursor::pos()));

    // Shape of the day: 30 x 28 with the radius 6. The gap of 2 px between the
    // rows of the shapes lies above each shape (also below the names of the days).
    const qreal shapeWidth = qMin(30, rect.width());
    const qreal shapeHeight = qMin(28, rect.height());
    const QRectF shape(rect.left() + (rect.width() - shapeWidth) / 2.0, rect.bottom() + 1.0 - shapeHeight, shapeWidth, shapeHeight);
    const PDFStylePainter::Radii radii(6.0);

    PDFThemeColors::Color textColor = PDFThemeColors::TextPrimary;
    PDFThemeFonts::Role fontRole = PDFThemeFonts::Role::Small;

    if (isSelected && isDateEnabled)
    {
        PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgAccent);
        textColor = PDFThemeColors::IconOnAccent;
        fontRole = PDFThemeFonts::Role::SmallMedium;
    }
    else
    {
        if (isHovered)
        {
            PDFStylePainter::fill(painter, shape, radii, PDFThemeColors::BgHover);
        }
        if (isToday)
        {
            PDFStylePainter::strokeInside(painter, shape, radii, PDFThemeColors::BorderFocus, 1.0);
        }
        if (!isDateEnabled)
        {
            textColor = PDFThemeColors::TextDisabled;
        }
        else if (isOtherMonth)
        {
            textColor = PDFThemeColors::TextMuted;
        }
    }

    PDFStylePainter::text(painter, shape, Qt::AlignCenter | Qt::TextSingleLine, locale().toString(date.day()), PDFThemeFonts::getFont(fontRole), PDFStylePainter::color(textColor));
}

bool PDFCalendarWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_viewport && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave || event->type() == QEvent::Enter))
    {
        // The hovered day follows the pointer
        m_viewport->update();
    }

    if (m_yearEdit && watched == m_yearEdit && event->type() == QEvent::ContextMenu)
    {
        // The menu of QAbstractSpinBox by popup(); the enabled steps of QAbstractSpinBox::stepEnabled
        // (protected) from the value, the range and the wrapping of the editor of the year
        QSpinBox* yearEdit = m_yearEdit.data();
        QAbstractSpinBox::StepEnabled stepEnabled = QAbstractSpinBox::StepNone;
        if (!yearEdit->isReadOnly())
        {
            if (yearEdit->wrapping())
            {
                stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
            }
            else
            {
                stepEnabled.setFlag(QAbstractSpinBox::StepUpEnabled, yearEdit->value() < yearEdit->maximum());
                stepEnabled.setFlag(QAbstractSpinBox::StepDownEnabled, yearEdit->value() > yearEdit->minimum());
            }
        }
        PDFFluentWidgetsHelper::showSpinBoxContextMenu(yearEdit, yearEdit->findChild<QLineEdit*>(), stepEnabled, static_cast<QContextMenuEvent*>(event), false);
        return true;
    }

    return QCalendarWidget::eventFilter(watched, event);
}

void PDFCalendarWidget::updateColors()
{
    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();

    if (QTableView* view = findChild<QTableView*>(QStringLiteral("qt_calendar_calendarview")))
    {
        // The model of the calendar takes the colors of the cells from the palette of the view
        QPalette palette = PDFThemeWidgetHelper::createTextPalette(PDFThemeColors::TextPrimary);
        for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
        {
            palette.setBrush(group, QPalette::Base, Qt::transparent);
            palette.setBrush(group, QPalette::AlternateBase, Qt::transparent);
            palette.setBrush(group, QPalette::Window, Qt::transparent);
        }
        view->setPalette(palette);
    }

    // Names of the days: Medium 11, muted. The weekend has no color of its own.
    QTextCharFormat headerFormat;
    headerFormat.setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption));
    headerFormat.setForeground(theme.getColor(PDFThemeColors::TextMuted));
    headerFormat.setBackground(Qt::transparent);
    setHeaderTextFormat(headerFormat);
    setWeekdayTextFormat(Qt::Saturday, QTextCharFormat());
    setWeekdayTextFormat(Qt::Sunday, QTextCharFormat());
    update();
}

PDFDateTimeEdit::PDFDateTimeEdit(QWidget* parent) :
    QDateTimeEdit(parent)
{
    init();
}

PDFDateTimeEdit::PDFDateTimeEdit(QDate date, QWidget* parent) :
    QDateTimeEdit(date, parent)
{
    init();
}

PDFDateTimeEdit::PDFDateTimeEdit(QTime time, QWidget* parent) :
    QDateTimeEdit(time, parent)
{
    init();
}

PDFDateTimeEdit::PDFDateTimeEdit(const QDateTime& dateTime, QWidget* parent) :
    QDateTimeEdit(dateTime, parent)
{
    init();
}

void PDFDateTimeEdit::setCalendarPopupEnabled(bool enabled)
{
    setCalendarPopup(enabled);
    ensureCalendar();
}

void PDFDateTimeEdit::showEvent(QShowEvent* event)
{
    // The popup can be turned on also by the function of QDateTimeEdit
    ensureCalendar();
    QDateTimeEdit::showEvent(event);
}

void PDFDateTimeEdit::contextMenuEvent(QContextMenuEvent* event)
{
    QMouseEvent release = PDFFluentWidgetsHelper::createSpinBoxRelease();
    QDateTimeEdit::mouseReleaseEvent(&release);
    PDFFluentWidgetsHelper::showSpinBoxContextMenu(this, lineEdit(), stepEnabled(), event, false);
}

void PDFDateTimeEdit::ensureCalendar()
{
    if (!calendarPopup())
    {
        return;
    }

    // An editor without a date has no calendar
    QCalendarWidget* calendar = calendarWidget();
    if (calendar && !qobject_cast<PDFCalendarWidget*>(calendar))
    {
        setCalendarWidget(new PDFCalendarWidget(this));
    }
}

void PDFDateTimeEdit::init()
{
    PDFThemeWidgetHelper::bindTextPalette(lineEdit());
}

PDFComboBox::PDFComboBox(QWidget* parent) :
    QComboBox(parent)
{
    // The mixed state is left, when an item becomes current (connected first, so the
    // slots of the application see the box out of the state)
    connect(this, &QComboBox::currentIndexChanged, this, &PDFComboBox::onCurrentIndexChanged);
}

void PDFComboBox::setMixed(bool mixed)
{
    if (m_isMixed == mixed)
    {
        return;
    }

    // Entering and leaving the state by the program emits nothing
    const QSignalBlocker blocker(this);
    if (mixed)
    {
        m_indexBeforeMixed = currentIndex();
        m_placeholderBeforeMixed = placeholderText();
        m_isMixed = true;
        PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::MIXED, true);
        setPlaceholderText(PDFFluentWidgetsHelper::getMixedText());
        setCurrentIndex(-1);
        updateMixedDescription();
    }
    else
    {
        // The item first: QComboBox::setPlaceholderText of an empty placeholder makes
        // the first item current, when no item is current
        setCurrentIndex(m_indexBeforeMixed);
        leaveMixed();
    }
    update();
}

void PDFComboBox::changeEvent(QEvent* event)
{
    QComboBox::changeEvent(event);

    if (event->type() == QEvent::LanguageChange && m_isMixed)
    {
        setPlaceholderText(PDFFluentWidgetsHelper::getMixedText());
        updateMixedDescription();
    }
}

void PDFComboBox::updateMixedDescription()
{
    // As PDFColorSwatchRow: the description of the state, unless the application has its own
    const QString description = m_isMixed ? PDFFluentWidgetsHelper::getMixedText() : QString();
    if (accessibleDescription().isEmpty() || accessibleDescription() == m_mixedDescription)
    {
        setAccessibleDescription(description);
    }
    m_mixedDescription = description;
}

void PDFComboBox::onCurrentIndexChanged(int index)
{
    if (m_isMixed && index >= 0)
    {
        leaveMixed();
    }
}

void PDFComboBox::leaveMixed()
{
    m_isMixed = false;
    m_indexBeforeMixed = -1;
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::MIXED, false);
    setPlaceholderText(m_placeholderBeforeMixed);
    m_placeholderBeforeMixed.clear();
    updateMixedDescription();
}

void PDFComboBox::setCompact(bool compact)
{
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::COMPACT, compact);
}

void PDFComboBox::setSelectPrefix(const QString& prefix)
{
    setProperty(PDFWidgetStyleProperties::SELECT_PREFIX, prefix);
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::SELECT, true);
}

bool PDFComboBox::event(QEvent* event)
{
    if (event->type() == QEvent::ChildPolished || event->type() == QEvent::Polish)
    {
        // The editor of an editable combo box draws its text by its palette
        if (QLineEdit* editor = lineEdit())
        {
            if (!PDFFluentWidgetsHelper::isTextPaletteBound(editor))
            {
                PDFThemeWidgetHelper::bindTextPalette(editor);
            }
        }
    }

    return QComboBox::event(event);
}

PDFStatusBar::PDFStatusBar(QWidget* parent) :
    QStatusBar(parent)
{
    // Paddings of the bar: 14 before the message, 8 after the last item
    setContentsMargins(14, 0, 8, 0);
    setFont(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular));
}

QToolButton* PDFStatusBar::createItem(const QString& text, const QString& iconName, bool isWarning)
{
    QToolButton* item = new QToolButton(this);
    item->setProperty(PDFWidgetStyleProperties::STATUS_ITEM, true);
    item->setText(text);
    item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    if (!iconName.isEmpty())
    {
        item->setIcon(PDFThemeIcons::getIcon(iconName));
    }
    if (isWarning)
    {
        item->setProperty(PDFWidgetStyleProperties::TONE, QStringLiteral("warning"));
    }

    addPermanentWidget(item);
    return item;
}

void PDFStatusBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, this);

    const QString message = currentMessage();
    if (message.isEmpty())
    {
        return;
    }

    // The message takes the space before the first item of the bar
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int left = contentsRect().left();
    int right = contentsRect().right() + 1;

    const QList<QWidget*> items = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (const QWidget* item : items)
    {
        if (!item->isVisible() || item->inherits("QSizeGrip"))
        {
            continue;
        }

        if (isRightToLeft)
        {
            left = qMax(left, item->geometry().right() + 1 + 12);
        }
        else
        {
            right = qMin(right, item->geometry().left() - 12);
        }
    }

    const QRectF rect(left, 1.0, qMax(0, right - left), height() - 1.0);
    const QFont messageFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
    const QString text = QFontMetricsF(messageFont).elidedText(message, Qt::ElideRight, rect.width());
    PDFStylePainter::text(&painter, rect, Qt::AlignVCenter | Qt::AlignAbsolute | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextSingleLine, text, messageFont, PDFStylePainter::color(PDFThemeColors::TextMuted));
}

#if QT_CONFIG(accessibility)
/// Segment of a segmented control. It is a checkable tool button, which would
/// be announced as a check box, although the segments are an exclusive choice.
class PDFSegmentAccessible : public QAccessibleWidget
{
public:
    explicit PDFSegmentAccessible(QWidget* widget) :
        QAccessibleWidget(widget, QAccessible::RadioButton)
    {

    }

    virtual QAccessible::State state() const override
    {
        QAccessible::State state = QAccessibleWidget::state();

        if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget()))
        {
            state.checkable = true;
            state.checked = button->isChecked();
        }

        return state;
    }

    virtual QString text(QAccessible::Text textType) const override
    {
        QString text = QAccessibleWidget::text(textType);
        const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget());

        if (text.isEmpty() && textType == QAccessible::Name && button)
        {
            text = button->text();
        }

        return text;
    }

    virtual QStringList actionNames() const override
    {
        return QStringList() << pressAction() << setFocusAction();
    }

    virtual void doAction(const QString& actionName) override
    {
        QAbstractButton* button = qobject_cast<QAbstractButton*>(widget());

        if (button && actionName == pressAction())
        {
            button->click();
            return;
        }

        QAccessibleWidget::doAction(actionName);
    }

    static QAccessibleInterface* create(const QString& className, QObject* object)
    {
        Q_UNUSED(className);

        QToolButton* button = qobject_cast<QToolButton*>(object);
        if (button && PDFWidgetStyleProperties::isSet(button, PDFWidgetStyleProperties::SEGMENT) && qobject_cast<PDFSegmentedControl*>(button->parentWidget()))
        {
            return new PDFSegmentAccessible(button);
        }

        return nullptr;
    }

    static bool installFactory()
    {
        QAccessible::installFactory(&PDFSegmentAccessible::create);
        return true;
    }
};
#endif

PDFSegmentedControl::PDFSegmentedControl(QWidget* parent) :
    QWidget(parent),
    m_layout(new QHBoxLayout(this))
{
#if QT_CONFIG(accessibility)
    // The factory is installed once for the whole application
    static const bool isFactoryInstalled = PDFSegmentAccessible::installFactory();
    Q_UNUSED(isFactoryInstalled);
#endif

    // Track without padding, segments follow each other without a gap
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_group.setExclusive(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // A member, not a child: outside of the subtree of the control, a retired control gets nothing of it
    PDFDeferredDeletion::connect(&m_group, &QButtonGroup::idToggled, this, &PDFSegmentedControl::onSegmentToggled);
}

void PDFSegmentedControl::onSegmentToggled(int id, bool isChecked)
{
    if (isChecked)
    {
        Q_EMIT currentIndexChanged(static_cast<int>(m_group.buttons().indexOf(m_group.button(id))));
    }
}

int PDFSegmentedControl::addSegment(const QString& text, const QString& iconName)
{
    QToolButton* segment = new QToolButton(this);
    segment->setProperty(PDFWidgetStyleProperties::SEGMENT, true);
    segment->setCheckable(true);
    segment->setText(text);
    segment->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Segments share the width of the control equally (three segments of 61 px
    // in the theme switch of 182 px), their own widths are not used by the layout
    segment->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    if (!iconName.isEmpty())
    {
        segment->setIcon(PDFThemeIcons::getIcon(iconName));
    }

    const int index = getCount();
    m_group.addButton(segment);
    // A layout deleted by the application lays nothing out
    if (m_layout)
    {
        m_layout->addWidget(segment, 1);
    }

    updateGeometry();
    if (index == 0)
    {
        segment->setChecked(true);
    }

    return index;
}

QSize PDFSegmentedControl::sizeHint() const
{
    int width = 0;
    int height = 0;
    int count = 0;
    for (const QAbstractButton* button : m_group.buttons())
    {
        if (button->isHidden())
        {
            continue;
        }

        const QSize hint = button->sizeHint();
        width = qMax(width, hint.width());
        height = qMax(height, hint.height());
        ++count;
    }

    const QMargins margins = contentsMargins();
    return QSize(width * count + margins.left() + margins.right(), height + margins.top() + margins.bottom());
}

QSize PDFSegmentedControl::minimumSizeHint() const
{
    return sizeHint();
}

int PDFSegmentedControl::getCount() const
{
    return static_cast<int>(m_group.buttons().size());
}

int PDFSegmentedControl::getCurrentIndex() const
{
    return static_cast<int>(m_group.buttons().indexOf(m_group.checkedButton()));
}

void PDFSegmentedControl::setCurrentIndex(int index)
{
    if (QAbstractButton* button = m_group.buttons().value(index))
    {
        button->setChecked(true);
    }
}

void PDFSegmentedControl::setSegmentIconColor(int index, PDFThemeColors::Color color)
{
    if (QAbstractButton* button = m_group.buttons().value(index))
    {
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::ICON_COLOR, static_cast<int>(color));
    }
}

void PDFSegmentedControl::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(16.0), PDFThemeColors::BgTrack);

    // Shadow of the raised plate of the selected segment lies on the track
    QAbstractButton* button = m_group.checkedButton();
    if (button && button->isVisible() && button->isEnabled())
    {
        PDFStylePainter::shadow(&painter, QRectF(button->geometry()), PDFStylePainter::Radii(16.0), QPointF(0.0, 1.0), 3.0, PDFStylePainter::color(PDFThemeColors::EffectShadow));
    }
}

PDFListView::PDFListView(QWidget* parent) :
    QListView(parent)
{
    // Rounded rows do not touch the frame of the view
    setViewportMargins(6, 6, 6, 6);
}

PDFTreeView::PDFTreeView(QWidget* parent) :
    QTreeView(parent)
{
    setViewportMargins(6, 6, 6, 6);
}

void PDFTreeView::setRowStyle(RowStyle rowStyle)
{
    if (m_rowStyle == rowStyle)
    {
        return;
    }

    m_rowStyle = rowStyle;
    clearPress();

    QString name;
    switch (rowStyle)
    {
        case RowStyle::Default:
            break;
        case RowStyle::Outline:
            name = QStringLiteral("outline");
            break;
        case RowStyle::Contents:
            name = QStringLiteral("contents");
            break;
        case RowStyle::Certificate:
            name = QStringLiteral("certificate");
            break;
        case RowStyle::Library:
            name = QStringLiteral("library");
            break;
    }

    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::ROW_STYLE, name.isEmpty() ? QVariant() : QVariant(name));

    // The indentation is taken from the style only, when the view is created
    setIndentation(style()->pixelMetric(QStyle::PM_TreeViewIndentation, nullptr, this));
    scheduleDelayedItemsLayout();
}

QModelIndex PDFTreeView::getPressedRow() const
{
    return (m_isPressedInside && m_pressedRow.isValid()) ? QModelIndex(m_pressedRow) : QModelIndex();
}

bool PDFTreeView::isPressTracked() const
{
    return m_rowStyle == RowStyle::Outline || m_rowStyle == RowStyle::Contents || m_rowStyle == RowStyle::Certificate;
}

qreal PDFTreeView::getRowRadius(RowStyle rowStyle)
{
    return rowStyle == RowStyle::Outline ? 8.0 : 6.0;
}

bool PDFTreeView::isRowFocusRingVisible() const
{
    return isEnabled() && hasFocus() && PDFWidgetStyle::isFocusRingVisible(this);
}

void PDFTreeView::drawRowFocusRing(QPainter* painter, const QRect& rowRect) const
{
    if (!painter || rowRect.isEmpty())
    {
        return;
    }

    PDFStylePainter::strokeInside(painter, QRectF(rowRect), PDFStylePainter::Radii(getRowRadius(m_rowStyle)), PDFThemeColors::BorderFocus, 2.0);
}

void PDFTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const
{
    // The pressed row: every cell and the area of the branches get State_Sunken
    const QModelIndex pressedRow = getPressedRow();
    if (pressedRow.isValid() && index.isValid() && index.row() == pressedRow.row() && index.parent() == pressedRow.parent() && index.model() == pressedRow.model())
    {
        QStyleOptionViewItem pressedOptions = options;
        pressedOptions.state |= QStyle::State_Sunken;
        BaseClass::drawRow(painter, pressedOptions, index);
    }
    else
    {
        BaseClass::drawRow(painter, options, index);
    }

    // The focus ring of the presets Outline and Certificate encloses the whole
    // current row (the style draws no ring of the current cell for them)
    const QModelIndex current = currentIndex();
    if (isPressTracked() && current.isValid() && index.isValid() && current.row() == index.row() && current.parent() == index.parent() && isRowFocusRingVisible())
    {
        drawRowFocusRing(painter, options.rect);
    }
}

bool PDFTreeView::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::FocusOut:
        case QEvent::WindowBlocked:
            // A modal window (opened by a slot of a double click) or the loss of
            // the focus swallow the release (review C2 of step 5)
            clearPress();
            break;

        default:
            break;
    }

    return BaseClass::event(event);
}

void PDFTreeView::startDrag(Qt::DropActions supportedActions)
{
    // The drag of the platform does not give the release back
    clearPress();
    BaseClass::startDrag(supportedActions);
}

void PDFTreeView::startPress(const QPoint& position, Qt::MouseButton button)
{
    clearPress();

    if (!isPressTracked() || button != Qt::LeftButton)
    {
        return;
    }

    const QModelIndex index = indexAt(position);
    if (index.isValid() && index.flags().testFlag(Qt::ItemIsEnabled))
    {
        m_pressedRow = index.siblingAtColumn(0);
        m_isPressedInside = true;
        updateRow(m_pressedRow);
    }
}

void PDFTreeView::setPressedInside(bool isInside)
{
    if (m_isPressedInside != isInside)
    {
        m_isPressedInside = isInside;
        updateRow(m_pressedRow);
    }
}

void PDFTreeView::clearPress()
{
    const QPersistentModelIndex previous = m_pressedRow;
    m_pressedRow = QPersistentModelIndex();
    m_isPressedInside = false;
    m_isMoveWithButton = false;
    updateRow(previous);
}

void PDFTreeView::updateRow(const QModelIndex& index)
{
    if (!index.isValid() || index.model() != model() || !viewport())
    {
        return;
    }

    const QRect rect = visualRect(index);
    viewport()->update(QRect(0, rect.top(), viewport()->width(), rect.height()));
}

void PDFTreeView::mousePressEvent(QMouseEvent* event)
{
    startPress(event->position().toPoint(), event->button());
    BaseClass::mousePressEvent(event);
}

void PDFTreeView::mouseDoubleClickEvent(QMouseEvent* event)
{
    startPress(event->position().toPoint(), event->button());
    BaseClass::mouseDoubleClickEvent(event);
}

void PDFTreeView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressedRow.isValid())
    {
        if (!event->buttons().testFlag(Qt::LeftButton))
        {
            // The release has been lost (a modal dialog, a drag)
            clearPress();
        }
        else
        {
            // The row looks pressed, while the pointer stays over it (the hover
            // move, which follows, belongs to this move)
            m_isMoveWithButton = true;
            const QModelIndex index = indexAt(event->position().toPoint());
            setPressedInside(index.isValid() && index.row() == m_pressedRow.row() && index.parent() == m_pressedRow.parent());
        }
    }

    BaseClass::mouseMoveEvent(event);
}

void PDFTreeView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        clearPress();
    }

    BaseClass::mouseReleaseEvent(event);
}

bool PDFTreeView::viewportEvent(QEvent* event)
{
    const bool isLeftButtonHeld = QGuiApplication::mouseButtons().testFlag(Qt::LeftButton);
    switch (event->type())
    {
        case QEvent::Hide:
        case QEvent::DragEnter:
        case QEvent::DragLeave:
        case QEvent::Drop:
            clearPress();
            break;

        case QEvent::HoverEnter:
        case QEvent::HoverMove:
            // A move without the button ends a press, whose release has been lost
            if (m_pressedRow.isValid() && !m_isMoveWithButton && !isLeftButtonHeld)
            {
                clearPress();
            }
            m_isMoveWithButton = false;
            break;

        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::HoverLeave:
            if (m_pressedRow.isValid() && !isLeftButtonHeld)
            {
                clearPress();
            }
            else if (event->type() != QEvent::Enter)
            {
                setPressedInside(false);
            }
            break;

        default:
            break;
    }

    return BaseClass::viewportEvent(event);
}

void PDFTreeView::updateGeometries()
{
    QTreeView::updateGeometries();

    // The tree view sets the margins of its viewport by its header, whenever it
    // updates its geometries. They are returned here. The base class resizes the
    // viewport before it computes the scroll bars, which calls this function
    // again, so the scroll bars are computed for the viewport with the margins.
    const QMargins margins = isHeaderHidden() ? QMargins(6, 6, 6, 6) : viewportMargins();

    if (!m_isUpdatingMargins && viewportMargins() != margins)
    {
        m_isUpdatingMargins = true;
        setViewportMargins(margins);
        m_isUpdatingMargins = false;
    }
}

PDFDial::PDFDial(QWidget* parent) :
    QDial(parent)
{

}

void PDFDial::mousePressEvent(QMouseEvent* event)
{
    if (maximum() == minimum() || event->button() != Qt::LeftButton || (event->buttons() ^ event->button()))
    {
        event->ignore();
        return;
    }

    event->accept();
    const QPointer<PDFDial> guard(this);
    setSliderPosition(getValueFromPoint(event->position()));
    if (guard)
    {
        setSliderDown(true);
    }
}

void PDFDial::mouseMoveEvent(QMouseEvent* event)
{
    if (!event->buttons().testFlag(Qt::LeftButton))
    {
        event->ignore();
        return;
    }

    event->accept();
    setSliderPosition(getValueFromPoint(event->position()));
}

void PDFDial::mouseReleaseEvent(QMouseEvent* event)
{
    if ((event->buttons() & ~event->button()) || event->button() != Qt::LeftButton)
    {
        event->ignore();
        return;
    }

    event->accept();
    const QPointer<PDFDial> guard(this);
    setValue(getValueFromPoint(event->position()));
    if (guard)
    {
        setSliderDown(false);
    }
}

int PDFDial::getValueFromPoint(const QPointF& point) const
{
    // Angle of the point measured clockwise from the top
    const QPointF center = QRectF(rect()).center();
    qreal angle = qRadiansToDegrees(qAtan2(point.x() - center.x(), center.y() - point.y()));
    if (angle < 0.0)
    {
        angle += 360.0;
    }

    qreal fraction = 0.0;

    if (wrapping())
    {
        // The whole circle from the bottom
        fraction = std::fmod(angle + 180.0, 360.0) / 360.0;
    }
    else
    {
        // The arc of 270 degrees from the bottom left, the gap at the bottom
        // belongs to the nearer end of the arc
        const qreal relative = std::fmod(angle + 135.0, 360.0);
        fraction = relative <= 270.0 ? relative / 270.0 : (relative < 315.0 ? 1.0 : 0.0);
    }

    if (invertedAppearance())
    {
        fraction = 1.0 - fraction;
    }

    const qreal range = qreal(maximum()) - qreal(minimum());
    return qBound(minimum(), static_cast<int>(qRound64(minimum() + fraction * range)), maximum());
}

/// Editor of a boolean value of an item
class PDFBooleanComboBox : public PDFComboBox
{
    Q_OBJECT
    Q_PROPERTY(bool value READ getValue WRITE setValue USER true)

public:
    explicit PDFBooleanComboBox(QWidget* parent) :
        PDFComboBox(parent)
    {
        addItem(QComboBox::tr("False"));
        addItem(QComboBox::tr("True"));
    }

    bool getValue() const { return currentIndex() == 1; }
    void setValue(bool value) { setCurrentIndex(value ? 1 : 0); }
};

/// Editor of an unsigned integer of an item (the range of a spin box of
/// integers ends in the half of the range of the unsigned integers)
class PDFUIntSpinBox : public PDFDoubleSpinBox
{
    Q_OBJECT
    Q_PROPERTY(uint uintValue READ getUIntValue WRITE setUIntValue USER true)

public:
    explicit PDFUIntSpinBox(QWidget* parent) :
        PDFDoubleSpinBox(parent)
    {
        setDecimals(0);
        setRange(0.0, std::numeric_limits<uint>::max());
    }

    uint getUIntValue() const { return static_cast<uint>(qRound64(value())); }
    void setUIntValue(uint value) { setValue(value); }
};

QWidget* PDFItemEditorFactory::createEditor(int userType, QWidget* parent) const
{
    switch (userType)
    {
        case QMetaType::Bool:
        {
            PDFBooleanComboBox* editor = new PDFBooleanComboBox(parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::Int:
        {
            PDFSpinBox* editor = new PDFSpinBox(parent);
            editor->setFrame(false);
            editor->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            return editor;
        }

        case QMetaType::UInt:
        {
            PDFUIntSpinBox* editor = new PDFUIntSpinBox(parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::Double:
        case QMetaType::Float:
        {
            PDFDoubleSpinBox* editor = new PDFDoubleSpinBox(parent);
            editor->setFrame(false);
            editor->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
            return editor;
        }

        case QMetaType::QDate:
        {
            PDFDateTimeEdit* editor = new PDFDateTimeEdit(QDate::currentDate(), parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::QTime:
        {
            PDFDateTimeEdit* editor = new PDFDateTimeEdit(QTime::currentTime(), parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::QDateTime:
        {
            PDFDateTimeEdit* editor = new PDFDateTimeEdit(parent);
            editor->setFrame(false);
            return editor;
        }

        case QMetaType::QString:
        {
            PDFLineEdit* editor = new PDFLineEdit(parent);
            editor->setFrame(false);
            return editor;
        }

        default:
            break;
    }

    // Other editors are created by the default factory. The default factory can
    // be a factory of this class (installed by the application for all views),
    // then a value of another type is edited as a text, as the factory of Qt does.
    const QItemEditorFactory* factory = QItemEditorFactory::defaultFactory();
    if (factory == this || dynamic_cast<const PDFItemEditorFactory*>(factory))
    {
        PDFLineEdit* editor = new PDFLineEdit(parent);
        editor->setFrame(false);
        return editor;
    }

    return factory->createEditor(userType, parent);
}

QByteArray PDFItemEditorFactory::valuePropertyName(int userType) const
{
    switch (userType)
    {
        case QMetaType::Bool:
        case QMetaType::Int:
        case QMetaType::Double:
        case QMetaType::Float:
            return QByteArrayLiteral("value");

        case QMetaType::UInt:
            return QByteArrayLiteral("uintValue");

        case QMetaType::QDate:
            return QByteArrayLiteral("date");

        case QMetaType::QTime:
            return QByteArrayLiteral("time");

        case QMetaType::QDateTime:
            return QByteArrayLiteral("dateTime");

        case QMetaType::QString:
            return QByteArrayLiteral("text");

        default:
            break;
    }

    const QItemEditorFactory* factory = QItemEditorFactory::defaultFactory();
    if (factory == this || dynamic_cast<const PDFItemEditorFactory*>(factory))
    {
        return QByteArrayLiteral("text");
    }

    return factory->valuePropertyName(userType);
}

PDFItemDelegate::PDFItemDelegate(QObject* parent) :
    QStyledItemDelegate(parent)
{
    static PDFItemEditorFactory factory;
    setItemEditorFactory(&factory);
}

void PDFItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);

    // The editor fills the whole cell, its ring is drawn inside of it
    if (editor)
    {
        editor->setGeometry(option.rect);
    }
}

}   // namespace pdf

#include "pdffluentwidgets.moc"

#include "moc_pdffluentwidgets.cpp"
