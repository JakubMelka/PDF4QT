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

#include "pdfappmarkupbar.h"
#include "pdfappannotationcreator.h"
#include "pdfappcommand.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"

#include <QAction>
#include <QKeyEvent>

namespace pdf
{

/// Values of the markup bar
class PDFAppMarkupBarHelper
{
public:
    PDFAppMarkupBarHelper() = delete;

    /// gap 877:134248 / 877:134249 before and after the divider
    static constexpr int DIVIDER_SPACE = 2;

    static constexpr const char* BUTTON_PROPERTY = "pdf4qtMarkupButton";

    /// Command of the button (its shortcut is shown in the tooltip)
    static QString getCommandId(PDFAppMarkupBar::Button button)
    {
        switch (button)
        {
            case PDFAppMarkupBar::Button::Highlight:
                return PDFAppCommands::annotateHighlight();
            case PDFAppMarkupBar::Button::Underline:
                return PDFAppCommands::annotateUnderline();
            case PDFAppMarkupBar::Button::Strikeout:
                return PDFAppCommands::annotateStrikeout();
            case PDFAppMarkupBar::Button::Squiggly:
                return PDFAppCommands::annotateSquiggly();
            case PDFAppMarkupBar::Button::HighlightComment:
                return PDFAppCommands::annotateHighlightComment();
            case PDFAppMarkupBar::Button::Copy:
                return PDFAppCommands::copy();
        }
        return QString();
    }

    /// Token of an annotation color, whose canonical value is the color (nullopt
    /// for another color)
    static std::optional<PDFThemeColors::Color> findToken(const QColor& color)
    {
        for (const PDFColorSwatchRow::Entry& entry : PDFColorSwatchRow::getAnnotationColors(PDFColorSwatchRow::Option::White))
        {
            if (entry.canonicalColor.isValid() && color.isValid() && entry.canonicalColor.rgb() == color.rgb())
            {
                return entry.token;
            }
        }
        return std::nullopt;
    }
};

PDFAppMarkupBar::PDFAppMarkupBar(PDFAppAnnotationCreator* creator, QWidget* parent) :
    BaseClass(Size::Compact, parent),
    m_creator(creator)
{
    using H = PDFAppMarkupBarHelper;

    setObjectName(QStringLiteral("markupBar"));

    // markup-highlight 877:134227 … markup-highlight-comment 877:134243 (Icon button 28)
    addButton(Button::Highlight, QStringLiteral("highlighter"));
    addButton(Button::Underline, QStringLiteral("underline"));
    addButton(Button::Strikeout, QStringLiteral("strikethrough"));
    addButton(Button::Squiggly, QStringLiteral("squiggly"));
    addButton(Button::HighlightComment, QStringLiteral("highlight-comment"));

    // gap 2, divider 877:134247, gap 2, Copy 877:134250
    m_leadingSpace = new QWidget(this);
    m_leadingSpace->setFixedSize(H::DIVIDER_SPACE, 1);
    addWidget(m_leadingSpace);
    m_divider = addDivider();
    m_trailingSpace = new QWidget(this);
    m_trailingSpace->setFixedSize(H::DIVIDER_SPACE, 1);
    addWidget(m_trailingSpace);
    addButton(Button::Copy, QStringLiteral("copy"));

    updateTexts();
    hide();
}

PDFAppMarkupBar::~PDFAppMarkupBar() = default;

void PDFAppMarkupBar::addButton(Button button, const QString& iconName)
{
    PDFIconButton* widget = new PDFIconButton(iconName, PDFIconButton::Size::Medium, this);
    widget->setObjectName(QStringLiteral("markup:") + iconName);
    widget->setProperty(PDFAppMarkupBarHelper::BUTTON_PROPERTY, int(button));
    widget->setFocusPolicy(Qt::TabFocus);
    connect(widget, &QAbstractButton::clicked, this, &PDFAppMarkupBar::onButtonClicked);
    addWidget(widget);
    m_buttons[button] = widget;
}

PDFIconButton* PDFAppMarkupBar::getButton(Button button) const
{
    auto it = m_buttons.find(button);
    return it != m_buttons.end() ? it->second.data() : nullptr;
}

void PDFAppMarkupBar::showAt(const QRect& selection, bool isEditingAllowed)
{
    // Locked document or no permission: only Copy (tools.md 9.8)
    for (const auto& item : m_buttons)
    {
        if (item.second)
        {
            item.second->setVisible(item.first == Button::Copy || isEditingAllowed);
        }
    }
    for (QWidget* widget : { static_cast<QWidget*>(m_divider.data()), m_leadingSpace.data(), m_trailingSpace.data() })
    {
        if (widget)
        {
            widget->setVisible(isEditingAllowed);
        }
    }

    updateState();
    placeAnchored(selection);
    show();
    raise();
}

void PDFAppMarkupBar::updateState()
{
    updateTexts();

    // Highlight shows its last color (tools.md 9.8)
    if (PDFIconButton* highlight = getButton(Button::Highlight))
    {
        const std::optional<PDFThemeColors::Color> token = PDFAppMarkupBarHelper::findToken(PDFAppAnnotationToolSettings::getMarkupColor(AnnotationType::Highlight));
        if (token)
        {
            highlight->setIconColor(*token);
        }
        else
        {
            highlight->resetIconColor();
        }
    }
}

void PDFAppMarkupBar::updateTexts()
{
    using H = PDFAppMarkupBarHelper;

    PDFAppAnnotationCreator* creator = m_creator.data();
    const PDFAppDocumentView* view = creator ? creator->getView() : nullptr;
    const PDFAppMainWindow* window = view ? qobject_cast<const PDFAppMainWindow*>(view->window()) : nullptr;

    for (const auto& item : m_buttons)
    {
        PDFIconButton* button = item.second.data();
        if (!button)
        {
            continue;
        }

        QString name;
        switch (item.first)
        {
            case Button::Highlight:
                name = PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind::Highlight);
                break;
            case Button::Underline:
                name = PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind::Underline);
                break;
            case Button::Strikeout:
                name = PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind::Strikeout);
                break;
            case Button::Squiggly:
                name = PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind::Squiggly);
                break;
            case Button::HighlightComment:
                name = PDFAppAnnotationTools::getName(PDFAppAnnotationToolKind::HighlightComment);
                break;
            case Button::Copy:
                name = PDFAppAnnotationTools::tr("Copy");
                break;
        }

        // The effective shortcut of the user (the action of the window has it)
        const QAction* action = window ? window->getCommandAction(H::getCommandId(item.first)) : nullptr;
        const QString shortcut = action ? action->shortcut().toString(QKeySequence::NativeText) : QString();
        button->setAccessibleName(name);
        //: Tooltip of a button with its shortcut: Highlight (Ctrl+Shift+H)
        button->setToolTip(shortcut.isEmpty() ? name : PDFAppAnnotationTools::tr("%1 (%2)").arg(name, shortcut));
    }
    setAccessibleName(PDFAppAnnotationTools::tr("Mark the selected text"));
}

void PDFAppMarkupBar::onButtonClicked()
{
    // The buttons do not identify themselves in the signal
    const QAbstractButton* button = qobject_cast<const QAbstractButton*>(sender());
    PDFAppAnnotationCreator* creator = m_creator.data();
    if (!button || !creator)
    {
        return;
    }

    // The creator hides the bar, when the selection is cleared (the bar is not
    // deleted from the signal of its button)
    switch (Button(button->property(PDFAppMarkupBarHelper::BUTTON_PROPERTY).toInt()))
    {
        case Button::Highlight:
            creator->markTextSelection(AnnotationType::Highlight);
            break;
        case Button::Underline:
            creator->markTextSelection(AnnotationType::Underline);
            break;
        case Button::Strikeout:
            creator->markTextSelection(AnnotationType::StrikeOut);
            break;
        case Button::Squiggly:
            creator->markTextSelection(AnnotationType::Squiggly);
            break;
        case Button::HighlightComment:
            creator->commentTextSelection();
            break;
        case Button::Copy:
            creator->copyTextSelection();
            break;
    }
}

void PDFAppMarkupBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
    {
        event->accept();
        hide();
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppMarkupBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

}   // namespace pdf
