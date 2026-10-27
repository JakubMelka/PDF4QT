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

#include "pdfappannotationtoolbar.h"
#include "pdfappcolorpicker.h"
#include "pdfappannotationcreatedialogs.h"
#include "pdfappannotationcreatetools.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include "pdfdrawspacecontroller.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>

#include <cmath>
#include <limits>

namespace pdf
{

/// Geometry of the parts of the tool bar (step4_floating_bar_geometry.md 2)
class PDFAppAnnotationToolBarHelper
{
public:
    PDFAppAnnotationToolBarHelper() = delete;

    /// Tool block: icon 16, gap 8
    static constexpr int ICON = 16;
    static constexpr int TOOL_GAP = 8;

    /// Stroke / Fill target 166 x 32, the width select 111 x 32
    static constexpr int COLOR_TARGET_WIDTH = 166;
    static constexpr int CONTROL_HEIGHT = 32;
    static constexpr int WIDTH_SELECT_WIDTH = 111;
    static constexpr int TARGET_SELECT_WIDTH = 160;

    /// Step badge: padding 8 / 2, radius 10
    static constexpr int BADGE_PADDING_X = 8;
    static constexpr int BADGE_PADDING_Y = 2;
    static constexpr int BADGE_RADIUS = 10;

    /// Text of a width ("2 pt")
    static QString getWidthText(PDFReal width)
    {
        return PDFAppAnnotationTools::tr("%1 pt").arg(PDFAppLocale::getUiLocale().toString(width, 'g', 3));
    }
};

// -------------------------------------------------------- PDFAppFloatingBarIcon

PDFAppFloatingBarIcon::PDFAppFloatingBarIcon(const QString& iconName, int extent, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName),
    m_extent(qBound(1, extent, 64))
{
    setFixedSize(m_extent, m_extent);
    setFocusPolicy(Qt::NoFocus);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFloatingBarIcon::update));
}

PDFAppFloatingBarIcon::~PDFAppFloatingBarIcon() = default;

void PDFAppFloatingBarIcon::setIconName(const QString& iconName)
{
    if (m_iconName != iconName)
    {
        m_iconName = iconName;
        update();
    }
}

QSize PDFAppFloatingBarIcon::sizeHint() const
{
    return QSize(m_extent, m_extent);
}

void PDFAppFloatingBarIcon::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The icon of the active tool is icon/accent (207:6272, 1169:26822)
    QPainter painter(this);
    PDFStylePainter::icon(&painter, QRectF(rect()), m_iconName, PDFThemeManager::getInstance()->getColor(PDFThemeColors::IconAccent));
}

// -------------------------------------------------------------- PDFAppStepBadge

PDFAppStepBadge::PDFAppStepBadge(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("stepBadge"));
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppStepBadge::update));
}

PDFAppStepBadge::~PDFAppStepBadge() = default;

void PDFAppStepBadge::setText(const QString& text)
{
    if (m_text != text)
    {
        m_text = text.left(32);
        setAccessibleName(m_text);
        updateGeometry();
        update();
    }
}

QSize PDFAppStepBadge::sizeHint() const
{
    using H = PDFAppAnnotationToolBarHelper;
    const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold);
    const QFontMetrics metrics(font);
    return QSize(metrics.horizontalAdvance(m_text) + 2 * H::BADGE_PADDING_X, metrics.height() + 2 * H::BADGE_PADDING_Y);
}

QSize PDFAppStepBadge::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppStepBadge::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    using H = PDFAppAnnotationToolBarHelper;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds(rect());
    PDFStylePainter::fill(&painter, bounds, PDFStylePainter::Radii(H::BADGE_RADIUS), PDFThemeColors::BgAccentSubtle);
    PDFStylePainter::text(&painter, bounds, Qt::AlignCenter, m_text, PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold),
                          PDFThemeManager::getInstance()->getColor(PDFThemeColors::TextAccent));
}

void PDFAppStepBadge::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::FontChange)
    {
        updateGeometry();
    }
}

// ------------------------------------------------------ PDFAppAnnotationToolBar

PDFAppAnnotationToolBar::PDFAppAnnotationToolBar(PDFAppAnnotationCreator* creator, QWidget* parent) :
    BaseClass(Size::Normal, parent),
    m_creator(creator)
{
    setObjectName(QStringLiteral("annotationToolBar"));
    hide();
}

PDFAppAnnotationToolBar::~PDFAppAnnotationToolBar() = default;

void PDFAppAnnotationToolBar::clearParts()
{
    QHBoxLayout* layout = getContentLayout();
    if (layout)
    {
        while (QLayoutItem* item = layout->takeAt(0))
        {
            if (QWidget* widget = item->widget())
            {
                // A part may be the sender of the signal, which reconfigures the bar
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }
    }
    setFoldableSwatchRow(nullptr);
}

void PDFAppAnnotationToolBar::addPart(QWidget* widget)
{
    addWidget(widget);
}

void PDFAppAnnotationToolBar::configure(PDFAppAnnotationToolKind kind)
{
    // The parts are measured in the full form first; a parent, which is too narrow
    // for them, gets the compact form
    m_kind = kind;
    m_isCompact = false;
    buildParts();
    updateCompactness();
}

void PDFAppAnnotationToolBar::updateCompactness()
{
    const QWidget* parent = parentWidget();
    if (m_kind == PDFAppAnnotationToolKind::None || !parent)
    {
        return;
    }

    const bool isCompact = m_normalWidth > parent->width() - 2 * PARENT_MARGIN;
    if (isCompact != m_isCompact)
    {
        m_isCompact = isCompact;
        buildParts();
    }
}

void PDFAppAnnotationToolBar::onCompactnessQueued()
{
    m_isCompactnessPending = false;
    updateCompactness();
}

bool PDFAppAnnotationToolBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && !m_isCompactnessPending)
    {
        // The parts are created again from the event loop (not inside of the resize)
        m_isCompactnessPending = true;
        QMetaObject::invokeMethod(this, &PDFAppAnnotationToolBar::onCompactnessQueued, Qt::QueuedConnection);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppAnnotationToolBar::buildParts()
{
    using H = PDFAppAnnotationToolBarHelper;
    using Kind = PDFAppAnnotationToolKind;

    const PDFAppAnnotationToolKind kind = m_kind;
    clearParts();
    if (kind == Kind::None)
    {
        return;
    }

    // tool 207:6271: icon 16 + name Semi Bold 13 text/primary, gap 8
    QWidget* toolBlock = new QWidget(this);
    toolBlock->setObjectName(QStringLiteral("toolBlock"));
    QHBoxLayout* toolLayout = new QHBoxLayout(toolBlock);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(H::TOOL_GAP);
    m_icon = new PDFAppFloatingBarIcon(QString(), H::ICON, toolBlock);
    m_name = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, toolBlock);
    m_name->setObjectName(QStringLiteral("toolName"));
    m_name->setTextFormat(Qt::PlainText);
    m_name->setVisible(!m_isCompact);
    toolLayout->addWidget(m_icon);
    toolLayout->addWidget(m_name);
    addPart(toolBlock);
    addDivider();

    m_hint = nullptr;
    m_stepBadge = nullptr;
    m_instruction = nullptr;
    m_colorTarget = nullptr;
    m_colorRow = nullptr;
    m_widthSelect = nullptr;
    m_targetSelect = nullptr;
    m_backButton = nullptr;

    if (kind == Kind::PageLink)
    {
        // step 1169:27252: the badge "1 / 2" and the instruction Medium 13
        QWidget* stepBlock = new QWidget(this);
        stepBlock->setObjectName(QStringLiteral("stepBlock"));
        QHBoxLayout* stepLayout = new QHBoxLayout(stepBlock);
        stepLayout->setContentsMargins(0, 0, 0, 0);
        stepLayout->setSpacing(H::TOOL_GAP);
        m_stepBadge = new PDFAppStepBadge(stepBlock);
        m_instruction = new PDFLabel(QString(), PDFThemeFonts::Role::BodyMedium, PDFThemeColors::TextPrimary, stepBlock);
        m_instruction->setObjectName(QStringLiteral("stepInstruction"));
        stepLayout->addWidget(m_stepBadge);
        stepLayout->addWidget(m_instruction);
        addPart(stepBlock);
        addDivider();

        // target 1169:26839: Select "Target  Whole page" with the Target menu
        m_targetSelect = new PDFAppPageLinkTargetSelect(this);
        m_targetSelect->setFixedHeight(H::CONTROL_HEIGHT);
        m_targetSelect->setMinimumWidth(H::TARGET_SELECT_WIDTH);
        connect(m_targetSelect.data(), &PDFAppPageLinkTargetSelect::targetChosen, this, &PDFAppAnnotationToolBar::onTargetChosen);
        addPart(m_targetSelect);

        // btn-back 1170:27603 in the step 2 (Secondary button with chevron-left)
        m_backButton = new QPushButton(this);
        m_backButton->setObjectName(QStringLiteral("toolBack"));
        m_backButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("chevron-left")));
        m_backButton->setFixedHeight(BACK_BUTTON_HEIGHT);
        connect(m_backButton.data(), &QPushButton::clicked, this, &PDFAppAnnotationToolBar::onBackClicked);
        addPart(m_backButton);
    }
    else if (!PDFAppAnnotationTools::getBarHint(kind).isEmpty())
    {
        // hint 1168:26448: Regular 12 text/muted
        m_hint = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, this);
        m_hint->setObjectName(QStringLiteral("toolHint"));
        addPart(m_hint);
    }
    else
    {
        if (PDFAppAnnotationTools::hasFill(kind))
        {
            // color-target 207:6724: segmented Stroke / Fill 166 x 32
            m_colorTarget = new PDFSegmentedControl(this);
            m_colorTarget->setObjectName(QStringLiteral("colorTarget"));
            m_colorTarget->addSegment(m_isCompact ? QString() : PDFAppAnnotationTools::tr("Stroke"), QStringLiteral("stroke"));
            m_colorTarget->addSegment(m_isCompact ? QString() : PDFAppAnnotationTools::tr("Fill"), QStringLiteral("droplet"));
            if (m_isCompact)
            {
                m_colorTarget->setFixedHeight(H::CONTROL_HEIGHT);
            }
            else
            {
                m_colorTarget->setFixedSize(H::COLOR_TARGET_WIDTH, H::CONTROL_HEIGHT);
            }
            m_colorTarget->setAccessibleName(PDFAppAnnotationTools::tr("Color of"));
            connect(m_colorTarget.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppAnnotationToolBar::onColorTargetChanged);
            addPart(m_colorTarget);
        }

        // swatches 207:6277 (8 colors + More; Fill: No color + 8 + white + More)
        m_colorRow = new PDFColorSwatchRow(this);
        m_colorRow->setObjectName(QStringLiteral("toolColors"));
        connect(m_colorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppAnnotationToolBar::onColorSelected);
        PDFAppColorPicker::attach(m_colorRow.data());
        addPart(m_colorRow);
        setFoldableSwatchRow(m_colorRow);

        if (PDFAppAnnotationTools::hasWidth(kind))
        {
            // line-width 207:6757: Select "Width  2 pt", 111 x 32
            addDivider();
            m_widthSelect = new PDFComboBox(this);
            m_widthSelect->setObjectName(QStringLiteral("toolWidth"));
            for (PDFReal width : PDFAppAnnotationTools::getPenWidths())
            {
                m_widthSelect->addItem(H::getWidthText(width), QVariant::fromValue(width));
            }
            m_widthSelect->setFixedHeight(H::CONTROL_HEIGHT);
            if (!m_isCompact)
            {
                m_widthSelect->setMinimumWidth(H::WIDTH_SELECT_WIDTH);
            }
            connect(m_widthSelect.data(), &QComboBox::activated, this, &PDFAppAnnotationToolBar::onWidthActivated);
            addPart(m_widthSelect);
        }
    }

    // tool-close 207:6299: Tab close button 20
    addDivider();
    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
    m_closeButton->setObjectName(QStringLiteral("toolClose"));
    connect(m_closeButton.data(), &QAbstractButton::clicked, this, &PDFAppAnnotationToolBar::onCloseClicked);
    addPart(m_closeButton);

    updateTexts();
    updateState();

    // Width of the full form with all swatches (the base of the compact form)
    if (!m_isCompact && getContentLayout())
    {
        if (m_colorRow)
        {
            m_colorRow->setVisibleSwatchLimit(-1);
        }
        getContentLayout()->invalidate();
        const QMargins shadow = getShadowMargins();
        m_normalWidth = getContentLayout()->sizeHint().width() - shadow.left() - shadow.right();
        updatePlacement();
    }
}

void PDFAppAnnotationToolBar::updateTexts()
{
    using Kind = PDFAppAnnotationToolKind;
    PDFAppAnnotationCreator* creator = m_creator.data();
    if (m_kind == Kind::None || !creator)
    {
        return;
    }

    // The name of a variant tool names the variant (Sticky note "Key", Stamp "Approved")
    QString name = PDFAppAnnotationTools::getName(m_kind);
    QString iconName = PDFAppAnnotationTools::getIconName(m_kind);
    if (m_kind == Kind::StickyNote)
    {
        name = PDFAppAnnotationTools::getStickyNoteName(creator->getStickyNoteIcon());
        iconName = PDFAppAnnotationTools::getStickyNoteIconName(creator->getStickyNoteIcon());
    }
    else if (m_kind == Kind::Stamp)
    {
        //: Name of the stamp tool with the chosen stamp: Stamp "Approved"
        name = PDFAppAnnotationTools::tr("Stamp “%1”").arg(PDFAppAnnotationTools::getStampName(creator->getStamp()));
    }

    if (m_icon)
    {
        m_icon->setIconName(iconName);
        m_icon->setToolTip(m_isCompact ? name : QString());
    }
    if (m_name)
    {
        m_name->setText(name);
    }
    setAccessibleName(name);
    if (m_hint)
    {
        m_hint->setText(PDFAppAnnotationTools::getBarHint(m_kind));
    }
    if (m_backButton)
    {
        m_backButton->setText(PDFAppAnnotationTools::tr("Back"));
        m_backButton->setToolTip(PDFAppAnnotationTools::tr("Draw the link area again"));
    }
    if (m_closeButton)
    {
        //: Tooltip of the close button of the bar of a tool: End Highlight (Esc)
        m_closeButton->setToolTip(PDFAppAnnotationTools::tr("End %1 (Esc)").arg(name));
        m_closeButton->setAccessibleName(PDFAppAnnotationTools::tr("End %1").arg(name));
    }
    if (m_colorRow)
    {
        m_colorRow->setAccessibleName(PDFAppAnnotationTools::tr("Color"));
    }
    if (m_widthSelect)
    {
        using H = PDFAppAnnotationToolBarHelper;
        m_widthSelect->setSelectPrefix(m_isCompact ? QString() : PDFAppAnnotationTools::tr("Width"));
        m_widthSelect->setAccessibleName(PDFAppAnnotationTools::tr("Line width"));
        for (int i = 0; i < m_widthSelect->count(); ++i)
        {
            m_widthSelect->setItemText(i, H::getWidthText(m_widthSelect->itemData(i).toDouble()));
        }
    }
}

void PDFAppAnnotationToolBar::updateState()
{
    using Kind = PDFAppAnnotationToolKind;
    using Target = PDFAppAnnotationCreator::ColorTarget;

    PDFAppAnnotationCreator* creator = m_creator.data();
    if (m_kind == Kind::None || !creator || m_isUpdating)
    {
        return;
    }

    m_isUpdating = true;
    updateTexts();

    const Target target = PDFAppAnnotationTools::hasFill(m_kind) ? creator->getColorTarget() : Target::Stroke;
    if (m_colorTarget)
    {
        const QSignalBlocker blocker(m_colorTarget);
        m_colorTarget->setCurrentIndex(target == Target::Fill ? 1 : 0);
    }
    if (m_colorRow)
    {
        const PDFColorSwatchRow::Options options = target == Target::Fill ? (PDFColorSwatchRow::Option::NoColor | PDFColorSwatchRow::Option::White | PDFColorSwatchRow::Option::More)
                                                                          : PDFColorSwatchRow::Options(PDFColorSwatchRow::Option::More);
        if (m_colorRow->getOptions() != options)
        {
            // The full width follows the count of the swatches (Fill adds two)
            const int oldWidth = PDFColorSwatchRow::getRowSize(m_colorRow->getSwatchCount(), true).width();
            m_colorRow->setOptions(options);
            m_normalWidth += PDFColorSwatchRow::getRowSize(m_colorRow->getSwatchCount(), true).width() - oldWidth;
            if (!m_isCompactnessPending)
            {
                m_isCompactnessPending = true;
                QMetaObject::invokeMethod(this, &PDFAppAnnotationToolBar::onCompactnessQueued, Qt::QueuedConnection);
            }
        }
        m_colorRow->setColor(creator->getColor(m_kind, target));
    }
    if (m_widthSelect)
    {
        // The nearest width of the list (a stored width out of the list stays as it is)
        const PDFReal width = creator->getPenWidth(m_kind);
        int bestIndex = 0;
        PDFReal bestDistance = std::numeric_limits<PDFReal>::infinity();
        for (int i = 0; i < m_widthSelect->count(); ++i)
        {
            const PDFReal distance = std::abs(m_widthSelect->itemData(i).toDouble() - width);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        const QSignalBlocker blocker(m_widthSelect);
        m_widthSelect->setCurrentIndex(bestIndex);
    }
    if (m_kind == Kind::PageLink)
    {
        const PDFAppPageLinkTool* tool = creator->getPageLinkTool();
        const bool isTarget = tool && tool->getStep() == PDFAppPageLinkTool::Step::Target;
        const DestinationType type = creator->getPageLinkTarget();
        if (m_stepBadge)
        {
            //: Step of Link to page: 1 / 2, 2 / 2
            m_stepBadge->setText(PDFAppAnnotationTools::tr("%1 / %2").arg(isTarget ? 2 : 1).arg(2));
        }
        if (m_instruction)
        {
            m_instruction->setText(isTarget ? PDFAppAnnotationTools::getPageLinkInstruction(type) : PDFAppAnnotationTools::tr("Draw the link area"));
        }
        if (m_targetSelect)
        {
            m_targetSelect->setType(type);
            m_targetSelect->setKeepZoom(creator->isPageLinkKeepZoom());
        }
        updateZoom();
        if (m_backButton)
        {
            m_backButton->setVisible(isTarget);
        }
    }

    // The card hugs its parts as in Figma: 34 with a hint only (1168:26016), 50
    // with Back of the step 2 of Link to page (1170:27196), else 46
    int cardHeight = 0;
    if (m_hint)
    {
        cardHeight = HINT_HEIGHT;
    }
    else if (m_backButton && !m_backButton->isHidden())
    {
        cardHeight = TARGET_STEP_HEIGHT;
    }
    setCardHeight(cardHeight);

    m_isUpdating = false;
    updatePlacement();
}

void PDFAppAnnotationToolBar::updateZoom()
{
    // Read at each change of the view: the select shows the zoom, which a Position
    // link would store (PDFAppPageLinkTool::createDestination reads the same zoom)
    const PDFAppAnnotationCreator* creator = m_creator.data();
    const PDFAppDocumentView* view = creator ? creator->getView() : nullptr;
    const PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    if (m_targetSelect)
    {
        m_targetSelect->setZoom(proxy ? proxy->getZoom() : 1.0);
    }
}

PDFAppFloatingBarIcon* PDFAppAnnotationToolBar::getIcon() const
{
    return m_icon.data();
}

PDFLabel* PDFAppAnnotationToolBar::getNameLabel() const
{
    return m_name.data();
}

PDFLabel* PDFAppAnnotationToolBar::getHintLabel() const
{
    return m_hint.data();
}

PDFAppStepBadge* PDFAppAnnotationToolBar::getStepBadge() const
{
    return m_stepBadge.data();
}

PDFLabel* PDFAppAnnotationToolBar::getInstructionLabel() const
{
    return m_instruction.data();
}

PDFSegmentedControl* PDFAppAnnotationToolBar::getColorTargetControl() const
{
    return m_colorTarget.data();
}

PDFColorSwatchRow* PDFAppAnnotationToolBar::getColorRow() const
{
    return m_colorRow.data();
}

PDFComboBox* PDFAppAnnotationToolBar::getWidthSelect() const
{
    return m_widthSelect.data();
}

PDFAppPageLinkTargetSelect* PDFAppAnnotationToolBar::getTargetSelect() const
{
    return m_targetSelect.data();
}

QPushButton* PDFAppAnnotationToolBar::getBackButton() const
{
    return m_backButton.data();
}

PDFIconButton* PDFAppAnnotationToolBar::getCloseButton() const
{
    return m_closeButton.data();
}

void PDFAppAnnotationToolBar::onColorSelected(const QColor& color)
{
    if (PDFAppAnnotationCreator* creator = m_creator.data())
    {
        const PDFAppAnnotationCreator::ColorTarget target = PDFAppAnnotationTools::hasFill(m_kind) ? creator->getColorTarget() : PDFAppAnnotationCreator::ColorTarget::Stroke;
        creator->setColor(m_kind, target, color);
    }
}

void PDFAppAnnotationToolBar::onColorTargetChanged(int index)
{
    if (PDFAppAnnotationCreator* creator = m_creator.data())
    {
        creator->setColorTarget(index == 1 ? PDFAppAnnotationCreator::ColorTarget::Fill : PDFAppAnnotationCreator::ColorTarget::Stroke);
    }
}

void PDFAppAnnotationToolBar::onWidthActivated(int index)
{
    PDFAppAnnotationCreator* creator = m_creator.data();
    if (creator && m_widthSelect && index >= 0 && index < m_widthSelect->count())
    {
        creator->setPenWidth(m_kind, m_widthSelect->itemData(index).toDouble());
    }
}

void PDFAppAnnotationToolBar::onTargetChosen(DestinationType type, bool keepZoom)
{
    if (PDFAppAnnotationCreator* creator = m_creator.data())
    {
        creator->setPageLinkTarget(type, keepZoom);
    }
}

void PDFAppAnnotationToolBar::onBackClicked()
{
    PDFAppAnnotationCreator* creator = m_creator.data();
    if (PDFAppPageLinkTool* tool = creator ? creator->getPageLinkTool() : nullptr)
    {
        tool->goBack();
    }
}

void PDFAppAnnotationToolBar::onCloseClicked()
{
    // The bar is hidden by the end of the tool (it is not deleted from its signal)
    if (PDFAppAnnotationCreator* creator = m_creator.data())
    {
        creator->endTool();
    }
}

void PDFAppAnnotationToolBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
    {
        event->accept();
        onCloseClicked();
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppAnnotationToolBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_kind != PDFAppAnnotationToolKind::None)
    {
        // The segments have no setter of a text: the parts are created again
        configure(m_kind);
    }
}

}   // namespace pdf
