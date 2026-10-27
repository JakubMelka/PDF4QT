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

#include "pdfappannotationcreatedialogs.h"
#include "pdfappcolorpicker.h"
#include "pdfappannotationcreator.h"
#include "pdfappannotationoperations.h"
#include "pdfappcommentpopup.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappfloatingbar.h"
#include "pdfapplocale.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyleOptionMenuItem>
#include <QVBoxLayout>

#include <cmath>

namespace pdf
{

/// Values and helpers of the dialogs of package A
class PDFAppAnnotationCreateDialogsHelper
{
public:
    PDFAppAnnotationCreateDialogsHelper() = delete;

    static constexpr const char* CONTEXT = "pdf::PDFAppTextBoxPopup";

    /// Padding 12 inside of the border 1, gap 10 (1168:26450)
    static constexpr int PADDING = 13;
    static constexpr int GAP = 10;

    /// Lines of the title and of the subtitle of the head (1168:26454 / 1168:26455)
    static constexpr int TITLE_LINE = 16;
    static constexpr int SUBTITLE_LINE = 13;

    /// Height of the text field and the gap of the label and the control of a row
    static constexpr int TEXT_HEIGHT = 80;
    static constexpr int ROW_GAP = 12;

    /// Widths of the controls of 1168:26498
    static constexpr int SPIN_WIDTH = 88;
    static constexpr int ALIGNMENT_WIDTH = 252;

    /// Font sizes and border widths of the popup (pt)
    static constexpr int MINIMUM_FONT_SIZE = 4;
    static constexpr int MAXIMUM_FONT_SIZE = 144;
    static constexpr int MAXIMUM_BORDER_WIDTH = 12;

    /// Families of the text box (the standard fonts every PDF reader has)
    static QStringList getFontFamilies()
    {
        return { QStringLiteral("Helvetica"), QStringLiteral("Times"), QStringLiteral("Courier") };
    }

    /// Ctrl+Enter / Ctrl+Return
    static bool isAcceptKey(const QKeyEvent* event)
    {
        return (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers().testFlag(Qt::ControlModifier);
    }

    /// Popup menu row: menu padding 8 + border 1 at the sides, 12 + 1 at the top and the bottom
    static constexpr int MENU_PADDING_X = 9;
    static constexpr int MENU_PADDING_Y = 13;
    static constexpr int MENU_GAP = 2;

    /// Gap between the target menu and the bar of its select (1170:27714)
    static constexpr int MENU_ANCHOR_GAP = 4;

    /// Row of the target menu (1170:28180): padding 10 / 7, tile 30, gap 12, check 16
    static constexpr int ITEM_PADDING_X = 10;
    static constexpr int ITEM_TILE = 30;
    static constexpr int ITEM_TILE_ICON = 16;
    static constexpr int ITEM_GAP = 12;
    static constexpr int ITEM_CHECK = 16;
    static constexpr int ITEM_RADIUS = 8;
    static constexpr int ITEM_TITLE_LINE = 16;
    static constexpr int ITEM_DESCRIPTION_LINE = 15;
};

// ----------------------------------------------------------- PDFAppTextBoxPopup

QMargins PDFAppTextBoxPopup::getShadowMargins()
{
    // The card of the comment popup (popover shadow 0 / 8 / 24)
    return PDFAppCommentPopup::getShadowMargins();
}

PDFAppTextBoxPopup::PDFAppTextBoxPopup(QWidget* parent) :
    BaseClass(parent)
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    setObjectName(QStringLiteral("textBoxPopup"));
    setAttribute(Qt::WA_NoMousePropagation, true);
    setFocusPolicy(Qt::StrongFocus);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(getShadowMargins() + QMargins(H::PADDING, H::PADDING, H::PADDING, H::PADDING));
    layout->setSpacing(H::GAP);

    // head 1168:26451: Tool icon 28 (blue, text-box 14), who, close 20
    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(10);
    m_badge = new PDFToolBadge(QStringLiteral("text-box"), PDFAccentHue::Blue, PDFComponentPainter::BadgeSize::Small, this);
    headLayout->addWidget(m_badge);
    QVBoxLayout* whoLayout = new QVBoxLayout();
    whoLayout->setContentsMargins(0, 0, 0, 0);
    whoLayout->setSpacing(1);
    // who 1168:26453 (30 high): title Semi Bold 13 in a line of 16, gap 1, the
    // subtitle Regular 11 in a line of 13 (the font metrics of Qt would give 14)
    m_title = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("textBoxTitle"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setLineHeight(H::TITLE_LINE);
    m_subtitle = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_subtitle->setObjectName(QStringLiteral("textBoxSubtitle"));
    m_subtitle->setTextFormat(Qt::PlainText);
    m_subtitle->setLineHeight(H::SUBTITLE_LINE);
    m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    whoLayout->addWidget(m_title);
    whoLayout->addWidget(m_subtitle);
    headLayout->addLayout(whoLayout, 1);
    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
    m_closeButton->setObjectName(QStringLiteral("textBoxClose"));
    headLayout->addWidget(m_closeButton, 0, Qt::AlignVCenter);
    layout->addLayout(headLayout);

    // text-field 1168:26496: 80 high
    m_textEdit = new PDFPlainTextEdit(this);
    m_textEdit->setObjectName(QStringLiteral("textBoxText"));
    m_textEdit->setFixedHeight(H::TEXT_HEIGHT);
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->installEventFilter(this);
    layout->addWidget(m_textEdit);

    // format 1168:26498: Font, Text color, Alignment, Border
    m_fontCombo = new PDFComboBox(this);
    m_fontCombo->setObjectName(QStringLiteral("textBoxFont"));
    m_fontCombo->addItems(H::getFontFamilies());
    m_fontCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sizeSpin = new PDFSpinBox(this);
    m_sizeSpin->setObjectName(QStringLiteral("textBoxSize"));
    m_sizeSpin->setRange(H::MINIMUM_FONT_SIZE, H::MAXIMUM_FONT_SIZE);
    m_sizeSpin->setFixedWidth(H::SPIN_WIDTH);
    QWidget* fontControl = new QWidget(this);
    fontControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout* fontLayout = new QHBoxLayout(fontControl);
    fontLayout->setContentsMargins(0, 0, 0, 0);
    fontLayout->setSpacing(8);
    fontLayout->addWidget(m_fontCombo, 1);
    fontLayout->addWidget(m_sizeSpin);
    layout->addWidget(createRow(QT_TRANSLATE_NOOP("pdf::PDFAppTextBoxPopup", "Font"), fontControl));

    m_textColorRow = new PDFColorSwatchRow(this);
    m_textColorRow->setObjectName(QStringLiteral("textBoxTextColor"));
    layout->addWidget(createRow(QT_TRANSLATE_NOOP("pdf::PDFAppTextBoxPopup", "Text color"), m_textColorRow));

    m_alignmentContainer = new QWidget(this);
    m_alignmentContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout* alignmentLayout = new QHBoxLayout(m_alignmentContainer);
    alignmentLayout->setContentsMargins(0, 0, 0, 0);
    alignmentLayout->setSpacing(0);
    alignmentLayout->addStretch(1);
    layout->addWidget(createRow(QT_TRANSLATE_NOOP("pdf::PDFAppTextBoxPopup", "Alignment"), m_alignmentContainer));

    m_borderWidthSpin = new PDFSpinBox(this);
    m_borderWidthSpin->setObjectName(QStringLiteral("textBoxBorderWidth"));
    m_borderWidthSpin->setRange(0, H::MAXIMUM_BORDER_WIDTH);
    m_borderWidthSpin->setFixedWidth(H::SPIN_WIDTH);
    m_borderColorRow = new PDFColorSwatchRow(this);
    m_borderColorRow->setObjectName(QStringLiteral("textBoxBorderColor"));
    m_borderColorRow->setOptions(PDFColorSwatchRow::Option::NoColor | PDFColorSwatchRow::Option::More);
    QWidget* borderControl = new QWidget(this);
    borderControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout* borderLayout = new QHBoxLayout(borderControl);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    borderLayout->setSpacing(8);
    borderLayout->addWidget(m_borderWidthSpin);
    borderLayout->addWidget(m_borderColorRow);
    borderLayout->addStretch(1);
    layout->addWidget(createRow(QT_TRANSLATE_NOOP("pdf::PDFAppTextBoxPopup", "Border"), borderControl));

    // actions 1168:26476: hint, Cancel (Secondary), Add (Primary)
    QHBoxLayout* actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    m_hint = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    m_hint->setObjectName(QStringLiteral("textBoxHint"));
    actionsLayout->addWidget(m_hint);
    actionsLayout->addStretch(1);
    m_cancelButton = new QPushButton(this);
    m_cancelButton->setObjectName(QStringLiteral("textBoxCancel"));
    m_addButton = new QPushButton(this);
    m_addButton->setObjectName(QStringLiteral("textBoxAdd"));
    PDFWidgetStyleProperties::set(m_addButton, PDFWidgetStyleProperties::PRIMARY);
    actionsLayout->addWidget(m_cancelButton);
    actionsLayout->addWidget(m_addButton);
    layout->addLayout(actionsLayout);

    setFixedWidth(CARD_WIDTH + getShadowMargins().left() + getShadowMargins().right());

    // The shadow is a sibling below the card (as the comment popup), deleted with the popup
    if (parent)
    {
        m_shadow = new PDFAppCommentPopupShadow(parent);
        m_shadow->setObjectName(QStringLiteral("textBoxPopupShadow"));
        m_shadow->hide();
    }

    connect(m_closeButton.data(), &QAbstractButton::clicked, this, &PDFAppTextBoxPopup::cancel);
    connect(m_cancelButton.data(), &QAbstractButton::clicked, this, &PDFAppTextBoxPopup::cancel);
    connect(m_addButton.data(), &QAbstractButton::clicked, this, &PDFAppTextBoxPopup::accept);
    connect(m_textEdit.data(), &QPlainTextEdit::textChanged, this, &PDFAppTextBoxPopup::onTextChanged);
    connect(m_fontCombo.data(), &QComboBox::currentIndexChanged, this, &PDFAppTextBoxPopup::onValueChanged);
    connect(m_sizeSpin.data(), &QSpinBox::valueChanged, this, &PDFAppTextBoxPopup::onValueChanged);
    connect(m_textColorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppTextBoxPopup::onValueChanged);
    connect(m_borderWidthSpin.data(), &QSpinBox::valueChanged, this, &PDFAppTextBoxPopup::onValueChanged);
    connect(m_borderColorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppTextBoxPopup::onValueChanged);
    PDFAppColorPicker::attach(m_textColorRow.data());
    PDFAppColorPicker::attach(m_borderColorRow.data());
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppTextBoxPopup::update));

    retranslate();
    updateState();
    hide();
}

PDFAppTextBoxPopup::~PDFAppTextBoxPopup()
{
    watchApplication(false);
    delete m_shadow.data();
}

QWidget* PDFAppTextBoxPopup::createRow(const char* label, QWidget* control)
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    // row 1168:26499: label 68 Regular 12 text/secondary, gap 12, the control
    QWidget* row = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(H::ROW_GAP);
    PDFLabel* labelWidget = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, row);
    labelWidget->setFixedWidth(LABEL_WIDTH);
    labelWidget->setBuddy(control);
    layout->addWidget(labelWidget);
    control->setParent(row);
    if (control->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag)
    {
        layout->addWidget(control, 1);
    }
    else
    {
        // A control of a fixed width stays at the leading edge of the row
        layout->addWidget(control);
        layout->addStretch(1);
    }
    m_rowLabels.emplace_back(labelWidget, label);
    return row;
}

void PDFAppTextBoxPopup::createAlignmentControl()
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    QHBoxLayout* layout = m_alignmentContainer ? qobject_cast<QHBoxLayout*>(m_alignmentContainer->layout()) : nullptr;
    if (!layout)
    {
        return;
    }

    int currentIndex = 0;
    bool hadFocus = false;
    if (PDFSegmentedControl* oldControl = m_alignmentControl.data())
    {
        currentIndex = oldControl->getCurrentIndex();
        hadFocus = oldControl->hasFocus();
        layout->removeWidget(oldControl);
        oldControl->hide();
        oldControl->deleteLater();
    }

    // segments 1168:26540: Left / Center / Right with the icons, 84 each
    PDFSegmentedControl* control = new PDFSegmentedControl(m_alignmentContainer);
    control->setObjectName(QStringLiteral("textBoxAlignment"));
    control->addSegment(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Left"), QStringLiteral("align-left"));
    control->addSegment(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Center"), QStringLiteral("text-align-center"));
    control->addSegment(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Right"), QStringLiteral("text-align-right"));
    control->setFixedWidth(H::ALIGNMENT_WIDTH);
    control->setCurrentIndex(currentIndex);
    control->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Alignment"));
    layout->insertWidget(0, control);
    connect(control, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppTextBoxPopup::onValueChanged);
    m_alignmentControl = control;
    if (hadFocus)
    {
        control->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppTextBoxPopup::retranslate()
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    m_title->setText(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "New text box"));
    m_textEdit->setPlaceholderText(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Type the text…"));
    m_textEdit->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Text of the text box"));
    m_hint->setText(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Ctrl+Enter adds · Esc cancels"));
    m_cancelButton->setText(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Cancel"));
    m_addButton->setText(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Add"));
    m_closeButton->setToolTip(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Cancel"));
    m_closeButton->setAccessibleName(m_closeButton->toolTip());
    m_sizeSpin->setDisplaySuffix(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "pt"));
    m_sizeSpin->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Font size"));
    m_borderWidthSpin->setDisplaySuffix(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "pt"));
    m_borderWidthSpin->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Border width"));
    m_fontCombo->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Font"));
    m_textColorRow->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Text color"));
    m_borderColorRow->setAccessibleName(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", "Border color"));
    setAccessibleName(m_title->text());

    // The segments have the texts of the design (1168:26540); the control has no
    // setter of a text, it is created again with the texts of the language
    createAlignmentControl();

    for (const auto& item : m_rowLabels)
    {
        if (item.first)
        {
            item.first->setText(QCoreApplication::translate("pdf::PDFAppTextBoxPopup", item.second));
        }
    }
}

void PDFAppTextBoxPopup::open(const QRect& anchor, const QString& subtitle, const PDFFreeTextStyle& style, const QColor& borderColor, PDFReal borderWidth)
{
    m_isClosing = false;
    m_subtitle->setText(subtitle.left(512));

    {
        const QSignalBlocker textBlocker(m_textEdit);
        const QSignalBlocker fontBlocker(m_fontCombo);
        const QSignalBlocker sizeBlocker(m_sizeSpin);
        const QSignalBlocker alignmentBlocker(m_alignmentControl);
        const QSignalBlocker borderBlocker(m_borderWidthSpin);

        m_textEdit->clear();
        int fontIndex = m_fontCombo->findText(style.fontFamily, Qt::MatchFixedString);
        m_fontCombo->setCurrentIndex(fontIndex >= 0 ? fontIndex : 0);
        m_sizeSpin->setValue(std::isfinite(style.fontSize) ? qRound(style.fontSize) : 10);
        m_textColorRow->setColor(style.textColor);
        const Qt::Alignment horizontal = style.textAlignment & Qt::AlignHorizontal_Mask;
        m_alignmentControl->setCurrentIndex(horizontal == Qt::AlignHCenter ? 1 : (horizontal == Qt::AlignRight ? 2 : 0));
        m_borderWidthSpin->setValue(std::isfinite(borderWidth) ? qRound(borderWidth) : 1);
        m_borderColorRow->setColor(borderColor);
    }

    updateState();
    show();
    raise();
    placeAt(anchor);
    watchApplication(true);
    m_textEdit->setFocus(Qt::PopupFocusReason);
    Q_EMIT previewChanged();
}

void PDFAppTextBoxPopup::placeAt(const QRect& anchor)
{
    QWidget* parent = parentWidget();
    if (!parent)
    {
        return;
    }

    m_anchor = anchor.normalized();
    layout()->activate();
    const QMargins shadow = getShadowMargins();
    const int cardHeight = qMax(0, sizeHint().height() - shadow.top() - shadow.bottom());

    // Below the rectangle at its leading edge; above it, when there is no room; clamped
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int x = isRightToLeft ? m_anchor.right() + 1 - CARD_WIDTH : m_anchor.left();
    int y = m_anchor.bottom() + 1 + ANCHOR_GAP;
    if (y + cardHeight > parent->height() - PARENT_MARGIN)
    {
        const int above = m_anchor.top() - ANCHOR_GAP - cardHeight;
        if (above >= PARENT_MARGIN)
        {
            y = above;
        }
    }
    x = qBound(PARENT_MARGIN, x, qMax(PARENT_MARGIN, parent->width() - PARENT_MARGIN - CARD_WIDTH));
    y = qBound(PARENT_MARGIN, y, qMax(PARENT_MARGIN, parent->height() - PARENT_MARGIN - cardHeight));

    setGeometry(QRect(QPoint(x, y) - QPoint(shadow.left(), shadow.top()), QSize(CARD_WIDTH, cardHeight).grownBy(shadow)));
    setMask(QRegion(getCardRect()));
    if (m_shadow && m_shadow->parentWidget() == parent)
    {
        m_shadow->setGeometry(geometry());
        m_shadow->setVisible(isVisible());
        m_shadow->stackUnder(this);
    }
}

QRect PDFAppTextBoxPopup::getCardRect() const
{
    return rect().marginsRemoved(getShadowMargins());
}

QRect PDFAppTextBoxPopup::getCardGeometry() const
{
    return getCardRect().translated(pos());
}

QString PDFAppTextBoxPopup::getText() const
{
    return m_textEdit ? m_textEdit->toPlainText().left(PDFAppAnnotationOperations::MAXIMUM_TEXT_LENGTH) : QString();
}

void PDFAppTextBoxPopup::setText(const QString& text)
{
    if (m_textEdit)
    {
        m_textEdit->setPlainText(text.left(PDFAppAnnotationOperations::MAXIMUM_TEXT_LENGTH));
    }
}

PDFFreeTextStyle PDFAppTextBoxPopup::getStyle() const
{
    PDFFreeTextStyle style;
    style.fontFamily = m_fontCombo ? m_fontCombo->currentText() : QStringLiteral("Helvetica");
    style.fontSize = m_sizeSpin ? m_sizeSpin->value() : 10;
    const QColor textColor = m_textColorRow ? m_textColorRow->getColor() : QColor();
    style.textColor = textColor.isValid() ? textColor : QColor(Qt::black);
    const int alignment = m_alignmentControl ? m_alignmentControl->getCurrentIndex() : 0;
    style.textAlignment = TextAlignment(alignment == 1 ? Qt::AlignHCenter : (alignment == 2 ? Qt::AlignRight : Qt::AlignLeft)) | Qt::AlignTop;
    return style;
}

QColor PDFAppTextBoxPopup::getBorderColor() const
{
    return m_borderColorRow ? m_borderColorRow->getColor() : QColor();
}

PDFReal PDFAppTextBoxPopup::getBorderWidth() const
{
    return m_borderWidthSpin ? m_borderWidthSpin->value() : 0;
}

PDFPlainTextEdit* PDFAppTextBoxPopup::getTextEdit() const
{
    return m_textEdit.data();
}

PDFComboBox* PDFAppTextBoxPopup::getFontCombo() const
{
    return m_fontCombo.data();
}

PDFSpinBox* PDFAppTextBoxPopup::getSizeSpin() const
{
    return m_sizeSpin.data();
}

PDFColorSwatchRow* PDFAppTextBoxPopup::getTextColorRow() const
{
    return m_textColorRow.data();
}

PDFSegmentedControl* PDFAppTextBoxPopup::getAlignmentControl() const
{
    return m_alignmentControl.data();
}

PDFSpinBox* PDFAppTextBoxPopup::getBorderWidthSpin() const
{
    return m_borderWidthSpin.data();
}

PDFColorSwatchRow* PDFAppTextBoxPopup::getBorderColorRow() const
{
    return m_borderColorRow.data();
}

QPushButton* PDFAppTextBoxPopup::getAddButton() const
{
    return m_addButton.data();
}

QPushButton* PDFAppTextBoxPopup::getCancelButton() const
{
    return m_cancelButton.data();
}

PDFIconButton* PDFAppTextBoxPopup::getCloseButton() const
{
    return m_closeButton.data();
}

PDFLabel* PDFAppTextBoxPopup::getTitleLabel() const
{
    return m_title.data();
}

PDFLabel* PDFAppTextBoxPopup::getSubtitleLabel() const
{
    return m_subtitle.data();
}

bool PDFAppTextBoxPopup::accept()
{
    // isHidden, not isVisible: an open popup of a hidden tab is still open
    if (isHidden() || getText().trimmed().isEmpty() || m_isClosing)
    {
        return false;
    }

    m_isClosing = true;
    watchApplication(false);
    hide();
    Q_EMIT accepted();
    return true;
}

void PDFAppTextBoxPopup::cancel()
{
    if (isHidden() || m_isClosing)
    {
        return;
    }

    m_isClosing = true;
    watchApplication(false);
    hide();
    Q_EMIT canceled();
}

void PDFAppTextBoxPopup::updateState()
{
    if (m_addButton)
    {
        m_addButton->setEnabled(!getText().trimmed().isEmpty());
    }
}

void PDFAppTextBoxPopup::onTextChanged()
{
    updateState();
    schedulePreviewChanged();
}

void PDFAppTextBoxPopup::onValueChanged()
{
    schedulePreviewChanged();
}

void PDFAppTextBoxPopup::schedulePreviewChanged()
{
    // Slots of the signals of the text document and of the fields: previewChanged
    // goes from the event loop, so its receivers may delete the tab (with this
    // popup) while Qt is not on the stack (wrap-up item 10)
    if (!m_isPreviewChangePending)
    {
        m_isPreviewChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppTextBoxPopup::onPreviewChangeQueued, Qt::QueuedConnection);
    }
}

void PDFAppTextBoxPopup::onPreviewChangeQueued()
{
    if (m_isPreviewChangePending)
    {
        m_isPreviewChangePending = false;
        Q_EMIT previewChanged();
    }
}

void PDFAppTextBoxPopup::watchApplication(bool watch)
{
    // A press on the page outside of the card cancels the popup (as the comment
    // popup); the canvas is the sibling of the popup in the canvas area
    QWidget* parent = parentWidget();
    if (!parent || watch == m_isWatchingApplication)
    {
        return;
    }

    m_isWatchingApplication = watch;
    for (QWidget* child : parent->findChildren<QWidget*>())
    {
        if (child == this || isAncestorOf(child) || child->isWindow())
        {
            continue;
        }
        if (watch)
        {
            child->installEventFilter(this);
        }
        else
        {
            child->removeEventFilter(this);
        }
    }
}

void PDFAppTextBoxPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Card bg/surface with the border border/default, radius 12 (1168:26450)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card(getCardRect());
    const PDFStylePainter::Radii radii(RADIUS);
    PDFStylePainter::fill(&painter, card, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, card, radii, PDFThemeColors::BorderDefault, 1.0);
}

void PDFAppTextBoxPopup::keyPressEvent(QKeyEvent* event)
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
    {
        event->accept();
        cancel();
        return;
    }
    if (H::isAcceptKey(event))
    {
        event->accept();
        accept();
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppTextBoxPopup::mousePressEvent(QMouseEvent* event)
{
    // The margins of the shadow are not a part of the popup for the pointer
    if (!getCardRect().contains(event->position().toPoint()))
    {
        event->ignore();
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppTextBoxPopup::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppTextBoxPopup::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    // Only the popup itself is closed by its hide; a hidden ancestor (another
    // document tab is shown) hides it with its shadow and shows both again, the
    // press outside must still cancel it then
    if (isHidden())
    {
        watchApplication(false);
        if (m_shadow)
        {
            m_shadow->hide();
        }
    }
}

bool PDFAppTextBoxPopup::eventFilter(QObject* watched, QEvent* event)
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    if (watched == m_textEdit && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride))
    {
        // Esc and Ctrl+Enter belong to the popup, not to the text or the window
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        const bool isEscape = keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier;
        if (isEscape || H::isAcceptKey(keyEvent))
        {
            event->accept();
            if (event->type() == QEvent::KeyPress)
            {
                if (isEscape)
                {
                    cancel();
                }
                else
                {
                    accept();
                }
            }
            return true;
        }
    }
    else if (event->type() == QEvent::MouseButtonPress && m_isWatchingApplication && watched != m_textEdit)
    {
        QWidget* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget != this && !isAncestorOf(widget))
        {
            // Closed from the event loop: the press must not delete anything the
            // canvas still uses
            QMetaObject::invokeMethod(this, &PDFAppTextBoxPopup::cancel, Qt::QueuedConnection);
        }
    }

    return BaseClass::eventFilter(watched, event);
}

// ---------------------------------------------------------- PDFAppWebLinkDialog

PDFAppWebLinkDialog::PDFAppWebLinkDialog(QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral, tr("Add a web link"),
              tr("The link opens the address in the web browser or in the e-mail program of the reader."),
              QDialogButtonBox::NoButton, parent)
{
    setObjectName(QStringLiteral("webLinkDialog"));
    setBadgeVisible(false);
    setCardWidth(CARD_WIDTH);

    m_field = new PDFLineEdit(this);
    m_field->setObjectName(QStringLiteral("webLinkField"));
    m_field->setPlaceholderText(tr("https://example.com"));
    m_field->setMaxLength(int(PDFAppAnnotationOperations::MAXIMUM_URL_LENGTH));
    m_field->setAccessibleName(tr("Address of the link"));
    addContent(m_field);

    m_errorLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextDanger, this);
    m_errorLabel->setObjectName(QStringLiteral("webLinkError"));
    m_errorLabel->setTextFormat(Qt::PlainText);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    addContent(m_errorLabel);

    m_cancelButton = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    m_cancelButton->setObjectName(QStringLiteral("webLinkCancel"));
    m_addButton = addActionButton(tr("Add Link"), ActionPosition::Trailing, true);
    m_addButton->setObjectName(QStringLiteral("webLinkAdd"));

    connect(m_cancelButton.data(), &QPushButton::clicked, this, &QDialog::reject);
    connect(m_addButton.data(), &QPushButton::clicked, this, &PDFAppWebLinkDialog::onAddClicked);
    connect(m_field.data(), &QLineEdit::returnPressed, this, &PDFAppWebLinkDialog::onAddClicked);
    connect(m_field.data(), &QLineEdit::textChanged, this, &PDFAppWebLinkDialog::onTextChanged);
    onTextChanged();
}

PDFAppWebLinkDialog::~PDFAppWebLinkDialog() = default;

QString PDFAppWebLinkDialog::getUrl() const
{
    return m_field ? m_field->text().trimmed() : QString();
}

PDFLineEdit* PDFAppWebLinkDialog::getField() const
{
    return m_field.data();
}

QPushButton* PDFAppWebLinkDialog::getAddButton() const
{
    return m_addButton.data();
}

QPushButton* PDFAppWebLinkDialog::getCancelButton() const
{
    return m_cancelButton.data();
}

PDFLabel* PDFAppWebLinkDialog::getErrorLabel() const
{
    return m_errorLabel.data();
}

void PDFAppWebLinkDialog::onTextChanged()
{
    const QString url = getUrl();
    const bool isAllowed = PDFAppAnnotationOperations::isWebLinkAllowed(url);
    m_addButton->setEnabled(isAllowed);

    // The error is shown only for a typed address, which cannot be added
    const QString error = (!url.isEmpty() && !isAllowed) ? tr("Enter a complete web address (http or https) or an e-mail address (mailto).") : QString();
    m_field->setInvalid(!error.isEmpty(), error);
    m_errorLabel->setText(error);
    m_errorLabel->setVisible(!error.isEmpty());
}

void PDFAppWebLinkDialog::onAddClicked()
{
    if (PDFAppAnnotationOperations::isWebLinkAllowed(getUrl()))
    {
        accept();
    }
}

// ----------------------------------------------------- PDFAppPageLinkTargetItem

PDFAppPageLinkTargetItem::PDFAppPageLinkTargetItem(DestinationType type, QWidget* parent) :
    BaseClass(parent),
    m_type(type)
{
    setObjectName(QStringLiteral("pageLinkTarget:") + PDFAppAnnotationTools::getPageLinkTargetIconName(type));
    setCheckable(true);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppPageLinkTargetItem::update));
}

PDFAppPageLinkTargetItem::~PDFAppPageLinkTargetItem() = default;

void PDFAppPageLinkTargetItem::setDescription(const QString& description)
{
    if (m_description != description)
    {
        m_description = description;
        setAccessibleDescription(description);
        updateGeometry();
        update();
    }
}

QSize PDFAppPageLinkTargetItem::sizeHint() const
{
    using H = PDFAppAnnotationCreateDialogsHelper;
    const QFontMetrics titleMetrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::BodyMedium));
    const QFontMetrics descriptionMetrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular));
    const int textWidth = qMax(titleMetrics.horizontalAdvance(text()), descriptionMetrics.horizontalAdvance(m_description));
    return QSize(2 * H::ITEM_PADDING_X + H::ITEM_TILE + H::ITEM_GAP + textWidth + H::ITEM_GAP + H::ITEM_CHECK, HEIGHT);
}

QSize PDFAppPageLinkTargetItem::minimumSizeHint() const
{
    using H = PDFAppAnnotationCreateDialogsHelper;
    return QSize(2 * H::ITEM_PADDING_X + H::ITEM_TILE + H::ITEM_GAP + H::ITEM_CHECK, HEIGHT);
}

void PDFAppPageLinkTargetItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    using H = PDFAppAnnotationCreateDialogsHelper;
    using Color = PDFThemeColors::Color;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds(rect());
    const PDFStylePainter::Radii radii(H::ITEM_RADIUS);
    const bool isEnabled = this->isEnabled();
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;

    // Hover / press of a menu row (item 1170:28180 r8)
    if (isEnabled && isDown())
    {
        PDFStylePainter::fill(&painter, bounds, radii, Color::BgPressed);
    }
    else if (isEnabled && underMouse())
    {
        PDFStylePainter::fill(&painter, bounds, radii, Color::BgHover);
    }

    // Icon tile 30 r8: bg/accent-subtle for the chosen target, bg/muted otherwise
    const qreal tileTop = (bounds.height() - H::ITEM_TILE) / 2.0;
    const qreal tileLeft = isRightToLeft ? bounds.width() - H::ITEM_PADDING_X - H::ITEM_TILE : H::ITEM_PADDING_X;
    const QRectF tile(tileLeft, tileTop, H::ITEM_TILE, H::ITEM_TILE);
    PDFStylePainter::fill(&painter, tile, radii, isChecked() ? Color::BgAccentSubtle : Color::BgMuted);
    const Color iconColor = !isEnabled ? Color::IconDisabled : (isChecked() ? Color::IconAccent : Color::IconDefault);
    PDFStylePainter::icon(&painter, PDFStylePainter::centered(tile, QSizeF(H::ITEM_TILE_ICON, H::ITEM_TILE_ICON)),
                          PDFAppAnnotationTools::getPageLinkTargetIconName(m_type), iconColor);

    // Check mark of the chosen target at the trailing side
    const qreal checkLeft = isRightToLeft ? H::ITEM_PADDING_X : bounds.width() - H::ITEM_PADDING_X - H::ITEM_CHECK;
    if (isChecked())
    {
        const QRectF check(checkLeft, (bounds.height() - H::ITEM_CHECK) / 2.0, H::ITEM_CHECK, H::ITEM_CHECK);
        PDFStylePainter::icon(&painter, check, QStringLiteral("check"), isEnabled ? Color::IconAccent : Color::IconDisabled);
    }

    // Name Medium 13 (line 16) and the description Regular 11 (line 15), 1 apart;
    // the place of the check mark is taken only by the chosen row (1170:28706)
    const qreal checkWidth = isChecked() ? H::ITEM_GAP + H::ITEM_CHECK : 0.0;
    const qreal textWidth = qMax(0.0, bounds.width() - 2 * H::ITEM_PADDING_X - H::ITEM_TILE - H::ITEM_GAP - checkWidth);
    const qreal textLeft = isRightToLeft ? H::ITEM_PADDING_X + checkWidth : H::ITEM_PADDING_X + H::ITEM_TILE + H::ITEM_GAP;
    const qreal blockHeight = H::ITEM_TITLE_LINE + 1 + H::ITEM_DESCRIPTION_LINE;
    const qreal blockTop = (bounds.height() - blockHeight) / 2.0;
    const int alignment = int(isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute | Qt::AlignVCenter;
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    const QFont titleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::BodyMedium);
    const QFont descriptionFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);
    const QString title = QFontMetricsF(titleFont).elidedText(text(), Qt::ElideRight, textWidth);
    const QString description = QFontMetricsF(descriptionFont).elidedText(m_description, Qt::ElideRight, textWidth);
    PDFStylePainter::text(&painter, QRectF(textLeft, blockTop, textWidth, H::ITEM_TITLE_LINE), alignment, title, titleFont,
                          manager->getColor(isEnabled ? Color::TextPrimary : Color::TextDisabled));
    PDFStylePainter::text(&painter, QRectF(textLeft, blockTop + H::ITEM_TITLE_LINE + 1, textWidth, H::ITEM_DESCRIPTION_LINE), alignment, description, descriptionFont,
                          manager->getColor(isEnabled ? Color::TextMuted : Color::TextDisabled));

    // The focus ring of the keyboard inside of the row
    if (hasFocus() && PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFStylePainter::focusRing(&painter, bounds, radii, false);
    }
}

void PDFAppPageLinkTargetItem::keyPressEvent(QKeyEvent* event)
{
    // Enter and Return choose the row as Space does
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier)
    {
        event->accept();
        click();
        return;
    }
    BaseClass::keyPressEvent(event);
}

bool PDFAppPageLinkTargetItem::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            update();
            break;

        default:
            break;
    }
    return BaseClass::event(event);
}

// ----------------------------------------------------- PDFAppPageLinkTargetMenu

PDFAppPageLinkTargetMenu::PDFAppPageLinkTargetMenu(QWidget* parent) :
    BaseClass(parent, Qt::Popup)
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    setObjectName(QStringLiteral("pageLinkTargetMenu"));
    setProperty(PDFWidgetStyleProperties::POPOVER, true);
    setProperty(PDFWidgetStyleProperties::POPUP_PLACED, true);
    setAttribute(Qt::WA_WindowPropagation, true);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(H::MENU_GAP);
    m_layout->setSizeConstraint(QLayout::SetNoConstraint);

    // caption-row 1170:28178: "Target view" Medium 11 text/tertiary, padding 10 / 0 / 6
    QWidget* captionRow = new QWidget(this);
    QHBoxLayout* captionLayout = new QHBoxLayout(captionRow);
    captionLayout->setContentsMargins(10, 0, 10, 6);
    m_caption = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextTertiary, captionRow);
    captionLayout->addWidget(m_caption);
    m_layout->addWidget(captionRow);

    for (DestinationType type : PDFAppAnnotationTools::getPageLinkTargets())
    {
        PDFAppPageLinkTargetItem* item = new PDFAppPageLinkTargetItem(type, this);
        connect(item, &QAbstractButton::clicked, this, &PDFAppPageLinkTargetMenu::onItemClicked);
        m_layout->addWidget(item);
        m_items[type] = item;
    }

    // Menu separator 706:150117: 1 px border/divider, 3 above and below, 4 at the sides
    QWidget* separator = new QWidget(this);
    QHBoxLayout* separatorLayout = new QHBoxLayout(separator);
    separatorLayout->setContentsMargins(4, 3, 4, 3);
    separatorLayout->addWidget(new PDFAppToolbarDivider(Qt::Horizontal, 0, 0, PDFThemeColors::BorderDivider, separator));
    m_layout->addWidget(separator);

    // option-keep-zoom 1179:28701: the check box and its hint (26 from the edge)
    QWidget* option = new QWidget(this);
    QVBoxLayout* optionLayout = new QVBoxLayout(option);
    optionLayout->setContentsMargins(10, 6, 10, 4);
    optionLayout->setSpacing(2);
    m_keepZoom = new QCheckBox(option);
    m_keepZoom->setObjectName(QStringLiteral("pageLinkKeepZoom"));
    optionLayout->addWidget(m_keepZoom);
    m_keepZoomHint = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, option);
    m_keepZoomHint->setObjectName(QStringLiteral("pageLinkKeepZoomHint"));
    m_keepZoomHint->setWordWrap(true);
    m_keepZoomHint->setLineHeight(15);
    m_keepZoomHint->setContentsMargins(26, 0, 0, 0);
    optionLayout->addWidget(m_keepZoomHint);
    m_layout->addWidget(option);
    connect(m_keepZoom.data(), &QCheckBox::toggled, this, &PDFAppPageLinkTargetMenu::onKeepZoomToggled);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppPageLinkTargetMenu::update));
    retranslate();
}

PDFAppPageLinkTargetMenu::~PDFAppPageLinkTargetMenu()
{
    // An open menu closes, while it is whole
    if (isVisible())
    {
        hide();
    }
}

void PDFAppPageLinkTargetMenu::retranslate()
{
    m_caption->setText(PDFAppAnnotationTools::tr("Target view"));
    for (const auto& item : m_items)
    {
        if (PDFAppPageLinkTargetItem* widget = item.second.data())
        {
            widget->setText(PDFAppAnnotationTools::getPageLinkTargetName(item.first));
            widget->setAccessibleName(widget->text());
            widget->setDescription(PDFAppAnnotationTools::getPageLinkTargetDescription(item.first));
        }
    }
    m_keepZoom->setText(PDFAppAnnotationTools::tr("Keep the reader's zoom"));
    setAccessibleName(m_caption->text());
    setWindowTitle(accessibleName());
    updateState();
}

void PDFAppPageLinkTargetMenu::updateState()
{
    for (const auto& item : m_items)
    {
        if (PDFAppPageLinkTargetItem* widget = item.second.data())
        {
            const QSignalBlocker blocker(widget);
            widget->setChecked(item.first == m_type);
        }
    }

    // Keep the reader's zoom only for Position (XYZ)
    const bool isPosition = m_type == DestinationType::XYZ;
    m_keepZoom->setEnabled(isPosition);
    const int percent = std::isfinite(m_zoom) ? qBound(1, qRound(m_zoom * 100.0), 100000) : 100;
    m_keepZoomHint->setText(isPosition ? PDFAppAnnotationTools::tr("On: the reader's zoom stays · Off: the current %1 % is stored").arg(PDFAppLocale::formatNumber(percent))
                                       : PDFAppAnnotationTools::tr("Only for Position"));
}

void PDFAppPageLinkTargetMenu::popup(QWidget* anchor, DestinationType type, bool keepZoom, PDFReal zoom)
{
    using H = PDFAppAnnotationCreateDialogsHelper;

    if (!anchor || !anchor->isVisible())
    {
        return;
    }

    m_type = PDFAppAnnotationTools::isPageLinkTarget(type) ? type : DestinationType::Fit;
    m_zoom = zoom;
    {
        const QSignalBlocker blocker(m_keepZoom);
        m_keepZoom->setChecked(keepZoom);
    }
    setLayoutDirection(anchor->layoutDirection());
    ensurePolished();
    updateState();

    const QMargins margins = PDFWidgetStyle::getPopoverMargins(this);
    m_layout->setContentsMargins(margins + QMargins(H::MENU_PADDING_X, H::MENU_PADDING_Y, H::MENU_PADDING_X, H::MENU_PADDING_Y));
    m_layout->activate();
    const int windowWidth = PANEL_WIDTH + margins.left() + margins.right();
    const int windowHeight = m_layout->hasHeightForWidth() ? m_layout->totalHeightForWidth(windowWidth) : m_layout->totalSizeHint().height();
    const QSize panelSize(PANEL_WIDTH, qMax(0, windowHeight - margins.top() - margins.bottom()));

    // 4 px below the floating bar of the select (menu-target 1170:28177 at y 62 below
    // the bar ending at 58, never over the bar), the leading edge at the leading edge
    // of the select (x 623 as the target select of the bar), mirrored for
    // right-to-left; kept inside of the screen
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    int anchorBottom = anchorRect.bottom();
    for (const QWidget* widget = anchor->parentWidget(); widget; widget = widget->parentWidget())
    {
        if (const PDFAppFloatingBar* bar = qobject_cast<const PDFAppFloatingBar*>(widget))
        {
            anchorBottom = qMax(anchorBottom, bar->mapToGlobal(bar->getCardRect().bottomLeft()).y());
            break;
        }
    }
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    QRect panel(QPoint(isRightToLeft ? anchorRect.right() + 1 - panelSize.width() : anchorRect.left(), anchorBottom + 1 + H::MENU_ANCHOR_GAP), panelSize);
    if (const QScreen* screen = anchor->screen())
    {
        const QRect available = screen->availableGeometry();
        panel.moveLeft(qBound(available.left(), panel.left(), qMax(available.left(), available.right() + 1 - panel.width())));
        panel.moveTop(qBound(available.top(), panel.top(), qMax(available.top(), available.bottom() + 1 - panel.height())));
    }

    setFixedSize(panel.size().grownBy(margins));
    move(panel.topLeft() - QPoint(margins.left(), margins.top()));

    QPointer<PDFAppPageLinkTargetMenu> guard(this);
    show();
    if (!guard)
    {
        return;
    }
    activateWindow();
    if (PDFAppPageLinkTargetItem* item = getItem(m_type))
    {
        item->setFocus(Qt::PopupFocusReason);
    }
}

bool PDFAppPageLinkTargetMenu::isKeepZoom() const
{
    return m_keepZoom && m_keepZoom->isChecked();
}

PDFAppPageLinkTargetItem* PDFAppPageLinkTargetMenu::getItem(DestinationType type) const
{
    auto it = m_items.find(type);
    return it != m_items.end() ? it->second.data() : nullptr;
}

QCheckBox* PDFAppPageLinkTargetMenu::getKeepZoomCheckBox() const
{
    return m_keepZoom.data();
}

PDFLabel* PDFAppPageLinkTargetMenu::getKeepZoomHint() const
{
    return m_keepZoomHint.data();
}

QRect PDFAppPageLinkTargetMenu::getPanelRect() const
{
    return rect().marginsRemoved(PDFWidgetStyle::getPopoverMargins(this));
}

void PDFAppPageLinkTargetMenu::onItemClicked()
{
    // The rows do not identify themselves in the signal
    const PDFAppPageLinkTargetItem* item = qobject_cast<const PDFAppPageLinkTargetItem*>(sender());
    if (!item)
    {
        return;
    }

    m_type = item->getType();
    updateState();
    const DestinationType type = m_type;
    const bool keepZoom = isKeepZoom();
    QPointer<PDFAppPageLinkTargetMenu> guard(this);
    hide();
    if (guard)
    {
        Q_EMIT targetChosen(type, keepZoom);
    }
}

void PDFAppPageLinkTargetMenu::onKeepZoomToggled(bool checked)
{
    Q_EMIT targetChosen(m_type, checked);
}

void PDFAppPageLinkTargetMenu::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFWidgetStyle::drawPopoverPanel(&painter, this, RADIUS);
}

void PDFAppPageLinkTargetMenu::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Escape:
            event->accept();
            hide();
            return;

        case Qt::Key_Down:
        case Qt::Key_Up:
            event->accept();
            focusNextPrevChild(event->key() == Qt::Key_Down);
            return;

        default:
            break;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppPageLinkTargetMenu::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);
    QMetaObject::invokeMethod(this, &PDFAppPageLinkTargetMenu::onClosedQueued, Qt::QueuedConnection);
}

void PDFAppPageLinkTargetMenu::onClosedQueued()
{
    if (!isVisible())
    {
        Q_EMIT closed();
    }
}

void PDFAppPageLinkTargetMenu::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

// --------------------------------------------------- PDFAppPageLinkTargetSelect

PDFAppPageLinkTargetSelect::PDFAppPageLinkTargetSelect(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("pageLinkTargetSelect"));
    for (DestinationType type : PDFAppAnnotationTools::getPageLinkTargets())
    {
        addItem(QString(), QVariant::fromValue(int(type)));
    }
    connect(this, &QComboBox::activated, this, &PDFAppPageLinkTargetSelect::onActivated);
    retranslate();
}

PDFAppPageLinkTargetSelect::~PDFAppPageLinkTargetSelect()
{
    delete m_menu.data();
}

void PDFAppPageLinkTargetSelect::retranslate()
{
    setSelectPrefix(PDFAppAnnotationTools::tr("Target"));
    setAccessibleName(PDFAppAnnotationTools::tr("Target of the link"));
    for (int i = 0; i < count(); ++i)
    {
        setItemText(i, PDFAppAnnotationTools::getPageLinkTargetName(DestinationType(itemData(i).toInt())));
    }
}

DestinationType PDFAppPageLinkTargetSelect::getType() const
{
    const int index = currentIndex();
    return index >= 0 ? DestinationType(itemData(index).toInt()) : DestinationType::Fit;
}

void PDFAppPageLinkTargetSelect::setType(DestinationType type)
{
    const int index = findData(QVariant::fromValue(int(type)));
    if (index >= 0)
    {
        const QSignalBlocker blocker(this);
        setCurrentIndex(index);
    }
}

PDFAppPageLinkTargetMenu* PDFAppPageLinkTargetSelect::getMenu() const
{
    return m_menu.data();
}

void PDFAppPageLinkTargetSelect::showPopup()
{
    // The Target menu of the design instead of the list of the combo box (a
    // separate window parented to the window of the select, deleted with the select)
    if (!m_menu)
    {
        m_menu = new PDFAppPageLinkTargetMenu(window());
        connect(m_menu.data(), &PDFAppPageLinkTargetMenu::targetChosen, this, &PDFAppPageLinkTargetSelect::onMenuTargetChosen);
    }
    m_menu->popup(this, getType(), m_keepZoom, m_zoom);
}

void PDFAppPageLinkTargetSelect::hidePopup()
{
    if (m_menu && m_menu->isVisible())
    {
        m_menu->hide();
    }
    BaseClass::hidePopup();
}

void PDFAppPageLinkTargetSelect::onMenuTargetChosen(DestinationType type, bool keepZoom)
{
    setType(type);
    m_keepZoom = keepZoom;
    Q_EMIT targetChosen(type, keepZoom);
}

void PDFAppPageLinkTargetSelect::onActivated(int index)
{
    // The keyboard of the closed select (arrows) changes the target directly
    if (index >= 0)
    {
        Q_EMIT targetChosen(DestinationType(itemData(index).toInt()), m_keepZoom);
    }
}

void PDFAppPageLinkTargetSelect::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

}   // namespace pdf
