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

#include "pdfappannotationbar.h"
#include "pdfappcolorpicker.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationmenus.h"
#include "pdfappcommand.h"
#include "pdfappcommentpopup.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"

#include "pdfannotationmanipulator.h"
#include "pdfannotationproperties.h"
#include "pdfwidgetannotation.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>

#include <cmath>

namespace pdf
{

/// Values of the annotation bar
class PDFAppAnnotationBarHelper
{
public:
    PDFAppAnnotationBarHelper() = delete;

    /// The type has a fill, which the bar changes (Stroke / Fill target)
    static bool hasFill(AnnotationType type)
    {
        return type == AnnotationType::Square || type == AnnotationType::Circle || type == AnnotationType::Polygon;
    }

    /// The type has a line width
    static bool hasWidth(AnnotationType type)
    {
        switch (type)
        {
            case AnnotationType::Square:
            case AnnotationType::Circle:
            case AnnotationType::Polygon:
            case AnnotationType::Polyline:
            case AnnotationType::Line:
            case AnnotationType::Ink:
            case AnnotationType::FreeText:
                return true;
            default:
                break;
        }
        return false;
    }

    /// Text of a width ("2 pt", "0.5 pt") in the language of the user interface
    static QString formatWidth(PDFReal width)
    {
        return PDFAppAnnotationBar::tr("%1 pt").arg(PDFAppLocale::getUiLocale().toString(width, 'g', 3));
    }

    /// Commands of the multiple selection (Align ×6, Distribute ×2) with their icons
    static std::vector<std::pair<QString, QString>> getArrangeCommands()
    {
        using C = PDFAppCommands;
        return { { C::annotationAlignLeft(), QStringLiteral("obj-align-left") }, { C::annotationAlignCenter(), QStringLiteral("obj-align-hcenter") },
                 { C::annotationAlignRight(), QStringLiteral("obj-align-right") }, { C::annotationAlignTop(), QStringLiteral("obj-align-top") },
                 { C::annotationAlignMiddle(), QStringLiteral("obj-align-vcenter") }, { C::annotationAlignBottom(), QStringLiteral("obj-align-bottom") },
                 { C::annotationDistributeHorizontally(), QStringLiteral("distribute-h") }, { C::annotationDistributeVertically(), QStringLiteral("distribute-v") } };
    }
};

std::vector<PDFReal> PDFAppAnnotationBar::getWidths()
{
    return { 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0 };
}

PDFAppAnnotationBar::PDFAppAnnotationBar(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(Size::Normal, parent),
    m_view(view)
{
    setObjectName(QStringLiteral("annotationBar"));
    createWidgets();
    retranslate();
    hide();

    if (PDFAppAnnotationController* controller = getController())
    {
        connect(controller, &PDFAppAnnotationController::selectionChanged, this, &PDFAppAnnotationBar::onSelectionChanged);
        connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppAnnotationBar::onStateChanged);
        connect(controller, &PDFAppAnnotationController::activeToolChanged, this, &PDFAppAnnotationBar::onActiveToolChanged);
        connect(controller, &PDFAppAnnotationController::changeCommitted, this, &PDFAppAnnotationBar::onDocumentChanged);
    }
    if (view)
    {
        connect(view, &PDFAppDocumentView::commandStatesChanged, this, &PDFAppAnnotationBar::onStateChanged);
    }
}

PDFAppAnnotationBar::~PDFAppAnnotationBar() = default;

PDFAppDocumentView* PDFAppAnnotationBar::getView() const
{
    return m_view.data();
}

PDFAppAnnotationController* PDFAppAnnotationBar::getController() const
{
    return m_view ? m_view->getAnnotationController() : nullptr;
}

PDFIconButton* PDFAppAnnotationBar::createCommandButton(const QString& commandId, const QString& iconName)
{
    PDFIconButton* button = new PDFIconButton(iconName, PDFIconButton::Size::Medium, this);
    button->setObjectName(QStringLiteral("annotationBar:") + commandId);
    button->setProperty("commandId", commandId);
    connect(button, &PDFIconButton::clicked, this, &PDFAppAnnotationBar::onCommandButtonClicked);
    m_commandButtons.emplace_back(commandId, button);
    return button;
}

void PDFAppAnnotationBar::createWidgets()
{
    // tool 1015:17769: icon 16 (icon/accent, as the tool bar of a creation tool) + the type
    // (Semi Bold 13) + the author (Regular 12 muted), gap 8
    QWidget* toolBlock = new QWidget(this);
    QHBoxLayout* toolLayout = new QHBoxLayout(toolBlock);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(8);
    m_typeIcon = new PDFAppCommentPopupIcon(QStringLiteral("square"), 16, PDFThemeColors::IconAccent, toolBlock);
    m_nameLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, toolBlock);
    m_nameLabel->setObjectName(QStringLiteral("annotationBarName"));
    m_nameLabel->setTextFormat(Qt::PlainText);
    m_authorLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, toolBlock);
    m_authorLabel->setObjectName(QStringLiteral("annotationBarAuthor"));
    m_authorLabel->setTextFormat(Qt::PlainText);
    m_authorLabel->setMaximumWidth(200);
    toolLayout->addWidget(m_typeIcon);
    toolLayout->addWidget(m_nameLabel);
    toolLayout->addWidget(m_authorLabel);
    addWidget(toolBlock);

    // color-target 1015:17773 (Stroke / Fill, 166 x 32), swatches 1015:17776
    m_targetDivider = addDivider();
    m_targetControl = new PDFSegmentedControl(this);
    m_targetControl->setObjectName(QStringLiteral("annotationBarTarget"));
    m_targetControl->addSegment(tr("Stroke"), QStringLiteral("stroke"));
    m_targetControl->addSegment(tr("Fill"), QStringLiteral("droplet"));
    m_targetControl->setFixedWidth(TARGET_WIDTH);
    connect(m_targetControl.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppAnnotationBar::onTargetChanged);
    addWidget(m_targetControl);

    m_swatchRow = new PDFColorSwatchRow(this);
    m_swatchRow->setObjectName(QStringLiteral("annotationBarSwatches"));
    connect(m_swatchRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppAnnotationBar::onColorSelected);
    PDFAppColorPicker::attach(m_swatchRow.data());
    addWidget(m_swatchRow);
    setFoldableSwatchRow(m_swatchRow);

    // line-width 1015:17787: Select "Width 2 pt" (Mixed for a selection of more widths)
    m_widthDivider = addDivider();
    m_widthComboBox = new PDFComboBox(this);
    m_widthComboBox->setObjectName(QStringLiteral("annotationBarWidth"));
    for (const PDFReal width : getWidths())
    {
        m_widthComboBox->addItem(QString(), width);
    }
    m_widthComboBox->setFixedWidth(WIDTH_SELECT);
    connect(m_widthComboBox.data(), &PDFComboBox::activated, this, &PDFAppAnnotationBar::onWidthActivated);
    addWidget(m_widthComboBox);

    // Multi 1018:18676: Align ×6, Distribute ×2
    m_alignDivider = addDivider();
    for (const auto& item : PDFAppAnnotationBarHelper::getArrangeCommands())
    {
        addWidget(createCommandButton(item.first, item.second));
    }

    // btn-comment 1015:17814, btn-properties 1015:17818, btn-more 1015:17822
    m_actionsDivider = addDivider();
    addWidget(createCommandButton(PDFAppCommands::annotationComment(), QStringLiteral("message-square")));
    addWidget(createCommandButton(PDFAppCommands::annotationProperties(), QStringLiteral("info")));
    m_moreButton = new PDFIconButton(QStringLiteral("more-horizontal"), PDFIconButton::Size::Medium, this);
    m_moreButton->setObjectName(QStringLiteral("annotationBarMore"));
    connect(m_moreButton.data(), &PDFIconButton::clicked, this, &PDFAppAnnotationBar::onMoreClicked);
    addWidget(m_moreButton);
}

void PDFAppAnnotationBar::retranslate()
{
    setAccessibleName(tr("Annotation"));
    m_targetControl->setAccessibleName(tr("Color target"));
    m_targetControl->setToolTip(tr("Stroke or fill"));
    m_swatchRow->setAccessibleName(tr("Color"));
    m_widthComboBox->setSelectPrefix(tr("Width"));
    m_widthComboBox->setAccessibleName(tr("Line width"));
    m_widthComboBox->setPlaceholderText(tr("Mixed"));
    const std::vector<PDFReal> widths = getWidths();
    for (int i = 0; i < m_widthComboBox->count() && size_t(i) < widths.size(); ++i)
    {
        m_widthComboBox->setItemText(i, PDFAppAnnotationBarHelper::formatWidth(widths[size_t(i)]));
    }

    // The buttons have the names and the shortcuts of their commands (user shortcuts)
    const PDFAppContext* context = m_view ? m_view->getContext() : nullptr;
    const PDFAppMainWindow* window = m_view ? qobject_cast<const PDFAppMainWindow*>(m_view->window()) : nullptr;
    for (const auto& item : m_commandButtons)
    {
        PDFIconButton* button = item.second.data();
        const PDFAppCommand* command = context ? context->getCommandRegistry()->findCommand(item.first) : nullptr;
        if (!button || !command)
        {
            continue;
        }

        const QString name = command->getTitle();
        const QAction* canvasAction = m_view ? m_view->getCanvasAction(item.first) : nullptr;
        const QAction* action = canvasAction ? canvasAction : (window ? window->getCommandAction(item.first) : nullptr);
        const QKeySequence shortcut = action && !action->shortcut().isEmpty() ? action->shortcut() : command->defaultShortcut;
        button->setAccessibleName(name);
        button->setToolTip(shortcut.isEmpty() ? name : tr("%1 (%2)").arg(name, shortcut.toString(QKeySequence::NativeText)));
    }
    m_moreButton->setAccessibleName(tr("More"));
    m_moreButton->setToolTip(tr("More"));
}

PDFAppCommentPopupIcon* PDFAppAnnotationBar::getTypeIcon() const { return m_typeIcon.data(); }
PDFLabel* PDFAppAnnotationBar::getNameLabel() const { return m_nameLabel.data(); }
PDFLabel* PDFAppAnnotationBar::getAuthorLabel() const { return m_authorLabel.data(); }
PDFSegmentedControl* PDFAppAnnotationBar::getTargetControl() const { return m_targetControl.data(); }
PDFColorSwatchRow* PDFAppAnnotationBar::getSwatchRow() const { return m_swatchRow.data(); }
PDFComboBox* PDFAppAnnotationBar::getWidthComboBox() const { return m_widthComboBox.data(); }
PDFIconButton* PDFAppAnnotationBar::getMoreButton() const { return m_moreButton.data(); }

PDFIconButton* PDFAppAnnotationBar::getCommandButton(const QString& commandId) const
{
    for (const auto& item : m_commandButtons)
    {
        if (item.first == commandId)
        {
            return item.second.data();
        }
    }
    return nullptr;
}

PDFIconButton* PDFAppAnnotationBar::getCommentButton() const
{
    return getCommandButton(PDFAppCommands::annotationComment());
}

PDFIconButton* PDFAppAnnotationBar::getPropertiesButton() const
{
    return getCommandButton(PDFAppCommands::annotationProperties());
}

bool PDFAppAnnotationBar::isFillTarget() const
{
    return m_isFillTarget;
}

void PDFAppAnnotationBar::setFillTarget(bool isFill)
{
    if (m_isFillTarget != isFill)
    {
        m_isFillTarget = isFill;
        if (PDFAppAnnotationController* controller = getController())
        {
            controller->endMergeSession();
        }
        updateControls();
    }
}

PDFAppAnnotationBar::StateKey PDFAppAnnotationBar::getStateKey() const
{
    StateKey key;
    const PDFAppAnnotationController* controller = getController();
    if (!controller)
    {
        return key;
    }

    key.isShown = controller->hasAnnotationSelection() && !controller->isCreationTool(controller->getActiveTool());
    if (key.isShown)
    {
        const PDFAppDocument* document = controller->getDocument();
        key.selection = controller->getSelectedAnnotations();
        key.document = document;
        key.version = document ? document->getVersion() : 0;
        key.isEditingAllowed = controller->isAnnotationEditingAllowed();
    }
    return key;
}

void PDFAppAnnotationBar::updateState()
{
    m_stateKey = getStateKey();
    const bool isShown = m_stateKey.isShown;
    if (!isShown)
    {
        if (!isHidden())
        {
            hide();
        }
        updateStatusHint(false);
        return;
    }

    updateControls();
    if (isHidden())
    {
        placeAtTop();
        show();
        raise();
    }
    updatePlacement();
    updateStatusHint(true);
}

QString PDFAppAnnotationBar::createStatusHint() const
{
    const PDFAppAnnotationController* controller = getController();
    const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const std::vector<PDFObjectReference> selection = controller ? controller->getSelectedAnnotations() : std::vector<PDFObjectReference>();
    if (!pdfDocument || selection.empty())
    {
        return QString();
    }

    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument.data(), selection.front());
    if (selection.size() > 1)
    {
        // 1018:18584
        bool isOnePage = true;
        for (const PDFObjectReference& reference : selection)
        {
            isOnePage = isOnePage && PDFAppAnnotationInfo::read(pdfDocument.data(), reference).pageIndex == info.pageIndex;
        }
        const QString selected = isOnePage && m_view ? tr("%Ln annotations selected on page %1", nullptr, int(selection.size())).arg(m_view->getPageLabel(info.pageIndex))
                                                     : tr("%Ln annotations selected", nullptr, int(selection.size()));
        if (!controller->isAnnotationEditingAllowed())
        {
            return tr("%1 · Shift+click adds or removes one").arg(selected);
        }
        return tr("%1 · drag moves them together, the handles transform the group · Align moves them to an edge or the center of the selection frame · Shift+click adds or removes one").arg(selected);
    }

    // 1015:17319: what a drag does with the annotation (the kinds 1026:21809)
    const QString selected = tr("%1 selected").arg(PDFAppAnnotationInfo::getDisplayName(info));
    if (!controller->isAnnotationEditingAllowed())
    {
        return tr("%1 · Esc deselects").arg(selected);
    }
    if (info.isLocked())
    {
        // 1026:21979
        return tr("This annotation is locked. Unlock it in Properties.");
    }

    QStringList actions = { tr("drag to move") };
    const PDFAnnotationPtr annotation = PDFAnnotation::parse(&pdfDocument->getStorage(), selection.front());
    const PDFAnnotation::Flags flags = annotation ? annotation->getEffectiveFlags() : PDFAnnotation::Flags();
    if (annotation && !flags.testFlag(PDFAnnotation::NoZoom) && !flags.testFlag(PDFAnnotation::NoRotate))
    {
        const PDFAnnotationManipulator::Capabilities capabilities = PDFAnnotationManipulator::getCapabilities(annotation.data());
        if (capabilities.testFlag(PDFAnnotationManipulator::EditPoints))
        {
            actions << tr("drag a point to move it");
        }
        if (capabilities.testFlag(PDFAnnotationManipulator::Resize))
        {
            actions << tr("drag a handle to resize (Shift keeps the aspect ratio)");
        }
        if (capabilities.testFlag(PDFAnnotationManipulator::RotateArbitrary))
        {
            actions << tr("drag the round handle to rotate");
        }
        else if (capabilities.testFlag(PDFAnnotationManipulator::RotateRightAngle))
        {
            actions << tr("drag the round handle to rotate in 90° steps");
        }
    }
    return tr("%1 · %2 · arrows nudge · Esc deselects").arg(selected, actions.join(QStringLiteral(", ")));
}

void PDFAppAnnotationBar::updateStatusHint(bool isShown)
{
    PDFAppDocumentStatusBar* statusBar = m_view ? m_view->getStatusBar() : nullptr;
    if (!statusBar)
    {
        return;
    }

    const QString hint = isShown ? createStatusHint() : QString();
    if (hint == m_statusHint)
    {
        // The same selection: a message shown meanwhile (a refused change) stays
        return;
    }

    // The hint of the previous selection goes, a message of another part stays
    const bool isOwnMessage = !m_statusHint.isEmpty() && statusBar->getMessage() == m_statusHint;
    m_statusHint = hint;
    if (!hint.isEmpty())
    {
        statusBar->showMessage(hint);
    }
    else if (isOwnMessage)
    {
        statusBar->showMessage(PDFAppDocumentView::tr("Ready"));
    }
}

void PDFAppAnnotationBar::updateControls()
{
    using F = PDFAnnotationProperties;

    const PDFAppAnnotationController* controller = getController();
    const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const std::vector<PDFObjectReference> selection = controller ? controller->getSelectedAnnotations() : std::vector<PDFObjectReference>();
    if (!pdfDocument || selection.empty())
    {
        return;
    }

    m_isUpdating = true;
    const bool isEditingAllowed = controller->isAnnotationEditingAllowed();
    const bool isSingle = selection.size() == 1;

    // The common values of the selection
    std::vector<PDFAppAnnotationInfo> infos;
    std::vector<F> properties;
    bool isAnyModifiable = false;
    bool hasFill = isSingle;
    bool hasWidth = false;
    for (const PDFObjectReference& reference : selection)
    {
        infos.push_back(PDFAppAnnotationInfo::read(pdfDocument.data(), reference));
        properties.push_back(PDFAnnotationPropertyEditor::read(&pdfDocument->getStorage(), reference));
        isAnyModifiable = isAnyModifiable || (infos.back().isValid() && !infos.back().isLocked());
        hasFill = hasFill && PDFAppAnnotationBarHelper::hasFill(infos.back().type);
        hasWidth = hasWidth || PDFAppAnnotationBarHelper::hasWidth(infos.back().type);
    }
    const bool canChange = isEditingAllowed && isAnyModifiable;
    if (!hasFill)
    {
        m_isFillTarget = false;
    }

    // tool block: the type and the author; multi "3 annotations" + "Page 308"
    if (isSingle)
    {
        QString iconName;
        PDFAccentHue hue = PDFAccentHue::Neutral;
        PDFAppAnnotationInfo::getBadge(infos.front().type, infos.front().isReply, &iconName, &hue);
        m_typeIcon->setIconName(iconName);
        m_typeIcon->show();
        m_nameLabel->setText(PDFAppAnnotationInfo::getDisplayName(infos.front()));
        m_authorLabel->setText(infos.front().isMarkup ? PDFAppAnnotationInfo::getAuthorText(infos.front().author) : QString());
    }
    else
    {
        m_typeIcon->hide();
        m_nameLabel->setText(tr("%n annotations", nullptr, int(selection.size())));
        bool isOnePage = true;
        for (const PDFAppAnnotationInfo& info : infos)
        {
            isOnePage = isOnePage && info.pageIndex == infos.front().pageIndex;
        }
        const PDFAppDocumentView* view = m_view.data();
        m_authorLabel->setText(isOnePage && view ? tr("Page %1").arg(view->getPageLabel(infos.front().pageIndex)) : tr("More pages"));
    }
    m_isAuthorShown = !m_authorLabel->text().isEmpty();
    m_authorLabel->setToolTip(m_authorLabel->text());

    // Color target (single annotation with a fill), swatches of the target
    m_isTargetShown = isEditingAllowed && hasFill;
    m_targetControl->setCurrentIndex(m_isFillTarget ? 1 : 0);
    m_targetControl->setEnabled(canChange);
    m_targetDivider->setVisible(isEditingAllowed);
    m_swatchRow->setVisible(isEditingAllowed);
    m_swatchRow->setEnabled(canChange);
    const PDFColorSwatchRow::Options options = m_isFillTarget ? PDFColorSwatchRow::Options(PDFColorSwatchRow::Option::NoColor | PDFColorSwatchRow::Option::White | PDFColorSwatchRow::Option::More)
                                                              : PDFColorSwatchRow::Options(PDFColorSwatchRow::Option::More);
    if (m_swatchRow->getOptions() != options)
    {
        m_swatchRow->setOptions(options);
    }

    QColor commonColor = m_isFillTarget ? properties.front().interiorColor : properties.front().color;
    bool isColorCommon = true;
    for (const F& property : properties)
    {
        const QColor color = m_isFillTarget ? property.interiorColor : property.color;
        isColorCommon = isColorCommon && color == commonColor;
    }
    if (isColorCommon && (commonColor.isValid() || m_isFillTarget))
    {
        m_swatchRow->setColor(commonColor);
    }
    else
    {
        // Mixed colors: no swatch is selected (the stroke row has no None swatch, an
        // invalid color selects nothing; the fill row would select None by it, so it
        // gets a custom color, which no swatch has)
        m_swatchRow->setColor(m_isFillTarget ? QColor(1, 2, 3) : QColor());
    }

    // Width: common value or Mixed
    PDFReal commonWidth = -1.0;
    bool isWidthCommon = true;
    for (size_t i = 0; i < properties.size(); ++i)
    {
        if (!PDFAppAnnotationBarHelper::hasWidth(infos[i].type))
        {
            continue;
        }
        if (commonWidth < 0.0)
        {
            commonWidth = properties[i].borderWidth;
        }
        isWidthCommon = isWidthCommon && qFuzzyCompare(1.0 + commonWidth, 1.0 + properties[i].borderWidth);
    }
    int widthIndex = -1;
    const std::vector<PDFReal> widths = getWidths();

    // A width of the document added for an earlier selection goes away (the list
    // has at most one width, which is not a standard one - of the current selection)
    while (m_widthComboBox->count() > int(widths.size()))
    {
        m_widthComboBox->removeItem(m_widthComboBox->count() - 1);
    }
    for (size_t i = 0; isWidthCommon && i < widths.size(); ++i)
    {
        if (qFuzzyCompare(1.0 + widths[i], 1.0 + commonWidth))
        {
            widthIndex = int(i);
        }
    }
    if (isWidthCommon && widthIndex < 0 && commonWidth >= 0.0)
    {
        // A width of the document, which the list does not have
        m_widthComboBox->addItem(PDFAppAnnotationBarHelper::formatWidth(commonWidth), commonWidth);
        widthIndex = m_widthComboBox->count() - 1;
    }
    m_widthComboBox->setCurrentIndex(widthIndex);
    m_widthComboBox->setFixedWidth(widthIndex < 0 ? WIDTH_SELECT_MIXED : WIDTH_SELECT);
    m_widthComboBox->setVisible(isEditingAllowed && hasWidth);
    m_widthComboBox->setEnabled(canChange);
    m_widthDivider->setVisible(isEditingAllowed && hasWidth);

    // Align and distribute: the multiple selection; Properties: the single one;
    // a locked document shows only Comment (1030:161012)
    m_alignDivider->setVisible(isEditingAllowed && !isSingle);
    for (const auto& item : m_commandButtons)
    {
        PDFIconButton* button = item.second.data();
        if (!button)
        {
            continue;
        }

        bool isVisible = true;
        if (item.first == PDFAppCommands::annotationComment())
        {
            isVisible = isSingle;
        }
        else if (item.first == PDFAppCommands::annotationProperties())
        {
            isVisible = isSingle && isEditingAllowed;
        }
        else
        {
            isVisible = !isSingle && isEditingAllowed;
        }
        button->setVisible(isVisible);
        button->setEnabled(m_view && m_view->isCommandEnabled(item.first));
    }
    m_moreButton->setVisible(isEditingAllowed);
    m_actionsDivider->setVisible(isEditingAllowed || isSingle);

    m_isUpdating = false;
    updateCompaction();
    updatePlacement();
}

void PDFAppAnnotationBar::applyCompaction(int level)
{
    m_authorLabel->setVisible(m_isAuthorShown && level < 1);
    m_nameLabel->setVisible(level < 2);
    m_targetControl->setVisible(m_isTargetShown && level < 3);
}

void PDFAppAnnotationBar::updateCompaction()
{
    // A narrow canvas (the minimal window): the swatches fold into More (the base
    // class), then the author, the name of the type and the Stroke / Fill target go
    QWidget* parent = parentWidget();
    PDFColorSwatchRow* row = m_swatchRow.data();
    if (!parent || !row || !getContentLayout())
    {
        return;
    }

    const int available = parent->width() - 2 * PARENT_MARGIN;
    const QMargins shadow = getShadowMargins();
    const int oldLimit = row->getVisibleSwatchLimit();
    const QSignalBlocker blocker(row);
    row->setVisibleSwatchLimit(1);

    int level = 0;
    for (; level < 3; ++level)
    {
        applyCompaction(level);
        getContentLayout()->invalidate();
        if (sizeHint().width() - shadow.left() - shadow.right() <= available)
        {
            break;
        }
    }
    m_compaction = level;
    applyCompaction(level);
    row->setVisibleSwatchLimit(oldLimit);
}

bool PDFAppAnnotationBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && !isHidden())
    {
        updateCompaction();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppAnnotationBar::beginSession(const QString& property, const QString& description)
{
    // One merge session per property and selection; a new press of another
    // control (or a closed session) starts a new step. Only the change of the
    // engine called next joins the session (up to endEngineChange).
    PDFAppAnnotationController* controller = getController();
    if (!controller)
    {
        return;
    }

    if (m_sessionKey.isEmpty() || controller->getMergeSessionKey() != m_sessionKey || !m_sessionKey.startsWith(property + QLatin1Char(':')))
    {
        m_sessionKey = PDFAppAnnotationController::createMergeKey(property, controller->getSelectedAnnotations(), ++m_sessionCounter);
    }
    controller->beginEngineChange(description, m_sessionKey);
}

void PDFAppAnnotationBar::applyColor(const QColor& color)
{
    PDFAppAnnotationController* controller = getController();
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    if (!manager || !controller->isAnnotationEditingAllowed() || !controller->hasAnnotationSelection())
    {
        return;
    }

    QPointer<PDFAppAnnotationBar> guard(this);
    QPointer<PDFAppAnnotationController> controllerGuard(controller);
    if (m_isFillTarget)
    {
        beginSession(QStringLiteral("fill"), tr("Change fill"));
        manager->setSelectedAnnotationsFillColor(color);
    }
    else if (color.isValid())
    {
        beginSession(QStringLiteral("color"), tr("Change color"));
        manager->setSelectedAnnotationsColor(color);
    }

    // The call of the engine has returned (a commit of another source - a reply,
    // a command - never joins the session)
    if (controllerGuard)
    {
        controllerGuard->endEngineChange();
    }
    if (guard)
    {
        updateControls();
    }
}

void PDFAppAnnotationBar::applyWidth(PDFReal width)
{
    PDFAppAnnotationController* controller = getController();
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    if (!manager || !controller->isAnnotationEditingAllowed() || !controller->hasAnnotationSelection() || !std::isfinite(width))
    {
        return;
    }

    QPointer<PDFAppAnnotationBar> guard(this);
    QPointer<PDFAppAnnotationController> controllerGuard(controller);
    beginSession(QStringLiteral("width"), tr("Change line width"));
    manager->setSelectedAnnotationsBorderWidth(qBound(0.0, width, PDFAnnotationProperties::MAXIMUM_BORDER_WIDTH));

    // The call of the engine has returned (a commit of another source - a reply,
    // a command - never joins the session)
    if (controllerGuard)
    {
        controllerGuard->endEngineChange();
    }
    if (guard)
    {
        updateControls();
    }
}

void PDFAppAnnotationBar::onColorSelected(const QColor& color)
{
    if (!m_isUpdating)
    {
        applyColor(color);
    }
}

void PDFAppAnnotationBar::onWidthActivated(int index)
{
    if (!m_isUpdating && index >= 0)
    {
        applyWidth(m_widthComboBox->itemData(index).toDouble());
    }
}

void PDFAppAnnotationBar::onTargetChanged(int index)
{
    if (!m_isUpdating)
    {
        setFillTarget(index == 1);
    }
}

void PDFAppAnnotationBar::onCommandButtonClicked()
{
    // The buttons do not identify themselves in the signal
    QAbstractButton* button = qobject_cast<QAbstractButton*>(sender());
    PDFAppAnnotationController* controller = getController();
    if (!button || !m_view)
    {
        return;
    }

    // A press of another control ends the live session; the command runs from
    // the event loop (it may open a dialog or delete the tab)
    if (controller)
    {
        controller->endMergeSession();
    }
    m_view->requestCommand(button->property("commandId").toString());
}

void PDFAppAnnotationBar::onMoreClicked()
{
    showMoreMenu();
}

void PDFAppAnnotationBar::showMoreMenu()
{
    PDFAppAnnotationController* controller = getController();
    PDFWidgetAnnotationManager* manager = controller ? controller->getAnnotationManager() : nullptr;
    if (!controller || !manager || !controller->hasAnnotationSelection())
    {
        return;
    }

    // The menu of the selection (More = the context menu, 1030:161012) below the button
    controller->endMergeSession();
    PDFAnnotationMenuRequest request;
    request.kind = PDFAnnotationMenuRequest::Kind::Selection;
    request.selection = controller->getSelectedAnnotations();
    request.annotation = request.selection.size() == 1 ? request.selection.front() : PDFObjectReference();
    request.isModificationAllowed = controller->isAnnotationEditingAllowed();
    request.canPaste = controller->canPaste();
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    request.globalPosition = m_moreButton->mapToGlobal(QPoint(isRightToLeft ? m_moreButton->width() : 0, m_moreButton->height() + 4));
    controller->requestMenu(request);
}

void PDFAppAnnotationBar::onSelectionChanged()
{
    m_sessionKey.clear();
    updateState();
}

void PDFAppAnnotationBar::onStateChanged()
{
    // The command states change with every change of the window: the selection is
    // read again only, when the selection, the document version, the editing or
    // the visibility of the bar has changed (review RC-B)
    if (!isHidden() || (getController() && getController()->hasAnnotationSelection()))
    {
        if (getStateKey() == m_stateKey && isHidden() == !m_stateKey.isShown)
        {
            return;
        }
        updateState();
    }
}

void PDFAppAnnotationBar::onActiveToolChanged(PDFWidgetTool* tool)
{
    Q_UNUSED(tool);
    updateState();
}

void PDFAppAnnotationBar::onDocumentChanged()
{
    if (!isHidden())
    {
        updateControls();
    }
}

bool PDFAppAnnotationBar::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Hide:
            // The live session ends with the bar
            if (PDFAppAnnotationController* controller = getController())
            {
                controller->endMergeSession();
            }
            break;

        case QEvent::LanguageChange:
            retranslate();
            if (!isHidden())
            {
                updateControls();
            }
            break;

        default:
            break;
    }
    return BaseClass::event(event);
}

}   // namespace pdf
