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

#include "pdfapptoolgui_geometry.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolgui_blank.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfanchorgrid.h"
#include "pdfcatalog.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumenttool_geometry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfpagesizepresets.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"

#include <QCheckBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMetaObject>
#include <QPainter>
#include <QVBoxLayout>

#include <cmath>
#include <set>

namespace pdf
{

/// Helpers of the tools of the page geometry
class PDFAppToolGUIGeometryHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUIGeometryHelper)

public:
    PDFAppToolGUIGeometryHelper() = delete;

    /// The sizes in the order of their counts (the most common first)
    static bool isMoreCommon(const std::pair<int, QSizeF>& first, const std::pair<int, QSizeF>& second)
    {
        return first.first > second.first;
    }

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The slot of the thumbnails of the left panel (389:22382: as the panel Thumbnails)
    static constexpr QSize THUMBNAIL_SLOT = QSize(108, 148);

    /// Width of the menus of the toolbar (391:23821)
    static constexpr int MENU_WIDTH = 280;
    static constexpr int ANCHOR_PANEL_WIDTH = 160;

    /// The rows of the cards Result, Summary and Boxes of page are 36 apart (389:22382,
    /// 391:23146, 391:26688): 28 + the gap 8
    static constexpr int KEY_VALUE_ROW_GAP = 8;

    /// "210 × 297 mm": whole millimeters as the frames (389:22382)
    static QString formatSize(const QSizeF& sizeMM)
    {
        return PDFAppToolGUIGeometryHelper::tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(std::round(sizeMM.width())),
                                                                  PDFAppBlankToolGUI::formatMillimeters(std::round(sizeMM.height())));
    }

    /// "A4" of a size in millimeters as shown, empty = no named size
    static QString getPaperName(const QSizeF& sizeMM)
    {
        return PDFDocumentToolResizePages::getPaperName(QSizeF(PDFPageSizePresets::getPoints(sizeMM.width()), PDFPageSizePresets::getPoints(sizeMM.height())));
    }

    /// The title of an option of an Enum parameter (the menus say it)
    static QString getOptionTitle(const PDFAppToolParameterModel* model, const QString& parameter, const QString& optionId)
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), parameter) : nullptr;
        if (definition)
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                if (option.id == optionId)
                {
                    return option.getTitle();
                }
            }
        }
        return QString();
    }

    /// The rectangle of a box of the default user space in the shown coordinates of another box
    static QRectF mapToShown(const PDFPage* page, const QRectF& shownBox, const QRectF& rect)
    {
        return PDFPageGeometry::getShownToUserTransform(shownBox, page->getPageRotation()).inverted().mapRect(rect);
    }
};

/// The color swatch of a box in the tile of a row of the menus Based on and Write to (391:27131:
/// the tile filled by the color of the box, the colors of the legend and of the outlines). It is
/// a child of the row over its tile (PDFComponentPainter::getTileRowTileRect), the mouse passes
/// through it; it follows the size and the direction of the row.
///
/// Ownership: a child of its row (it dies with it); it watches the row by an event filter.
class PDFAppToolBoxSwatch : public QWidget
{
public:
    /// Radius of the tile of a row (PDFComponentPainter, the tile of 28)
    static constexpr qreal RADIUS = 8.0;

    explicit PDFAppToolBoxSwatch(PDFTileRow* row, PDFThemeColors::Color color) :
        QWidget(row),
        m_row(row),
        m_color(color)
    {
        setObjectName(QStringLiteral("toolBoxSwatch"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
        row->installEventFilter(this);
        connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
        place();
        show();
    }

    PDFThemeColors::Color getColor() const { return m_color; }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if ((event->type() == QEvent::Resize || event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::Show) && watched == m_row)
        {
            place();
        }
        return QWidget::eventFilter(watched, event);
    }

    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(RADIUS), m_color);
    }

private:
    void place()
    {
        if (m_row)
        {
            const QRectF tile = PDFComponentPainter::getTileRowTileRect(QRectF(m_row->rect()), m_row->getContent(), m_row->getComponentState());
            setGeometry(tile.toAlignedRect());
            setVisible(!tile.isEmpty());
        }
    }

    QPointer<PDFTileRow> m_row;
    PDFThemeColors::Color m_color;
};

/// The values of the tools for a new version of the document: the pages follow their page
/// objects (PDFDocumentTool::mapParameters); the current selection is followed by its binder
class PDFAppToolGeometryPageFollower : public PDFAppToolValueMapper
{
public:
    PDFAppToolGeometryPageFollower(const PDFDocumentTool* tool, const PDFDocument* from, const PDFDocument* to) :
        m_tool(tool),
        m_from(from),
        m_to(to)
    {

    }

    virtual QVariantMap mapValues(const QVariantMap& values, bool isCurrent) const override
    {
        QVariantMap mapped = m_tool->mapParameters(values, m_from, m_to);
        if (isCurrent)
        {
            mapped.insert(QStringLiteral("pages"), values.value(QStringLiteral("pages")));
        }
        return mapped;
    }

private:
    const PDFDocumentTool* m_tool;
    const PDFDocument* m_from;
    const PDFDocument* m_to;
};

/// Registers the tool and its GUI under the id, when they are not registered yet
template<typename Tool, typename GUI>
class PDFAppToolGUIGeometryRegistration
{
public:
    static void registerTool()
    {
        const QString toolId = QString::fromLatin1(Tool::TOOL_ID);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<Tool>());
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<GUI>>());
        }
    }
};

void PDFAppToolGUIGeometry::registerGeometryTools()
{
    PDFAppToolGUIGeometryRegistration<PDFDocumentToolCropPages, PDFAppCropToolGUI>::registerTool();
    PDFAppToolGUIGeometryRegistration<PDFDocumentToolResizePages, PDFAppResizeToolGUI>::registerTool();
    PDFAppToolGUIGeometryRegistration<PDFDocumentToolSetPageBoxes, PDFAppBoxesToolGUI>::registerTool();
}

// ------------------------------------------------------- the elements of the builder

PDFAnchorGrid* PDFAppToolPanelBuilder::addAnchorMenu(const QString& parameter, const QString& titleFormat, int panelWidth)
{
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    if (!model)
    {
        return nullptr;
    }

    // tb-anchor 391:23146 and its panel 391:23821 (the grid of 40 x 40)
    const PDFAppToolHost* host = m_gui->getHost();
    QToolButton* button = createMenuButton(QStringLiteral("anchor-point"), QString());
    PDFAppToolAnchorPanel* panel = new PDFAppToolAnchorPanel(host ? host->getView() : nullptr, model, parameter, PDFAnchorGrid::CELL_SIZE, panelWidth, button);
    panel->setObjectName(QStringLiteral("toolPanel-") + parameter);
    panel->setTitleButton(button, titleFormat);
    if (PDFAppToolToolbar* toolbar = m_gui->getToolbar())
    {
        toolbar->addPanel(button, panel);
    }
    return panel->getGrid();
}

PDFAppToolAnchorField* PDFAppToolPanelBuilder::addAnchorField(const QString& parameter, const QString& label)
{
    if (!m_gui || !m_gui->getModel())
    {
        return nullptr;
    }

    PDFAppToolAnchorField* field = new PDFAppToolAnchorField(m_gui, parameter, label, m_widget);
    addWidget(field);
    return field;
}

PDFAppToolMarginFields* PDFAppToolPanelBuilder::addMarginFields(const QStringList& parameters, const QString& label)
{
    if (!m_gui || !m_gui->getModel())
    {
        return nullptr;
    }

    PDFAppToolMarginFields* fields = new PDFAppToolMarginFields(m_gui, parameters, label, m_widget);
    addWidget(fields);
    return fields;
}

// ------------------------------------------------------------ PDFAppToolMarginFields

PDFAppToolMarginFields::PDFAppToolMarginFields(PDFDocumentToolGUI* gui, const QStringList& parameters, const QString& note, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolMarginFields"));
    PDFAppToolParameterModel* model = gui ? gui->getModel() : nullptr;

    // card 389:22382: rows of 42, the label Regular 13 text/secondary, the field 140 at the end;
    // the label and the field 12 from the border as in the other cards (the card has the
    // padding 12 + border at the sides, 6 + border above and below: 5 more there)
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 5, 0, 5);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    getBodyLayout()->addLayout(grid);
    m_grid = grid;

    int& row = m_rowCount;
    for (const QString& parameter : parameters)
    {
        const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), parameter) : nullptr;
        const std::optional<PDFSettingDefinition> setting = model ? model->toSettingDefinition(parameter) : std::nullopt;
        if (!definition || !setting)
        {
            continue;
        }

        PDFAppSettingControl* control = new PDFAppSettingControl(gui->getContext(), *setting, this, model->createValueStore());
        connect(model, &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
        Row item;
        item.control = control;
        if (definition->getType() == PDFDocumentToolParameterType::Bool)
        {
            // A check box with the title of the parameter (391:23146)
            if (QCheckBox* checkBox = control->getCheckBox())
            {
                checkBox->setText(definition->getTitle());
                checkBox->setObjectName(QStringLiteral("toolField-") + parameter);
                grid->addWidget(checkBox, row, 0, 1, 2);
                item.editor = checkBox;
            }
        }
        else
        {
            PDFLabel* label = PDFAppPageWidgets::createLabel(definition->getTitle(), QFont::Normal, 13, PDFThemeColors::TextSecondary, this);
            label->setObjectName(QStringLiteral("toolFieldLabel-") + parameter);
            grid->addWidget(label, row, 0);
            item.label = label;
            if (PDFDoubleSpinBox* spinBox = control->getDoubleSpinBox())
            {
                spinBox->setObjectName(QStringLiteral("toolField-") + parameter);
                spinBox->setSuffix(tr(" mm"));
                spinBox->setDecimals(1);
                spinBox->setFixedWidth(FIELD_WIDTH);
                spinBox->setAccessibleName(definition->getTitle());
                label->setBuddy(spinBox);
                grid->addWidget(spinBox, row, 1);
                item.editor = spinBox;
            }
            else if (PDFComboBox* comboBox = control->getComboBox())
            {
                // An Enum parameter: a combo box of 140 (Images to PDF 336:1976 "Orientation")
                comboBox->setObjectName(QStringLiteral("toolField-") + parameter);
                comboBox->setFixedWidth(FIELD_WIDTH);
                comboBox->setAccessibleName(definition->getTitle());
                label->setBuddy(comboBox);
                grid->addWidget(comboBox, row, 1);
                item.editor = comboBox;
            }
        }
        m_rows[parameter] = item;
        ++row;
    }

    if (!note.isEmpty())
    {
        m_note = PDFAppPageWidgets::createLabel(note, QFont::Normal, 12, PDFThemeColors::TextTertiary, this);
        m_note->setObjectName(QStringLiteral("toolFieldsNote"));
        m_note->setWordWrap(true);
        grid->addWidget(m_note, row, 0, 1, 2);
    }
}

PDFAppToolMarginFields::~PDFAppToolMarginFields() = default;

void PDFAppToolMarginFields::addWidgetRow(const QString& key, const QString& label, QWidget* editor)
{
    if (!m_grid || !editor)
    {
        return;
    }

    // The row goes above the note (the note stays the last row of the card)
    if (m_note)
    {
        m_grid->removeWidget(m_note);
    }
    PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Normal, 13, PDFThemeColors::TextSecondary, this);
    labelWidget->setObjectName(QStringLiteral("toolFieldLabel-") + key);
    labelWidget->setBuddy(editor);
    editor->setParent(this);
    editor->setFixedWidth(FIELD_WIDTH);
    editor->setAccessibleName(label);
    m_grid->addWidget(labelWidget, m_rowCount, 0);
    m_grid->addWidget(editor, m_rowCount, 1);
    ++m_rowCount;
    if (m_note)
    {
        m_grid->addWidget(m_note, m_rowCount, 0, 1, 2);
    }

    Row item;
    item.label = labelWidget;
    item.editor = editor;
    m_rows[key] = item;
}

QWidget* PDFAppToolMarginFields::getEditor(const QString& key) const
{
    auto it = m_rows.find(key);
    return it != m_rows.cend() ? it->second.editor.data() : nullptr;
}

void PDFAppToolMarginFields::setRowVisible(const QString& key, bool isVisible)
{
    auto it = m_rows.find(key);
    if (it == m_rows.cend())
    {
        return;
    }
    for (QWidget* widget : { it->second.label.data(), it->second.editor.data() })
    {
        if (widget)
        {
            widget->setVisible(isVisible);
        }
    }
}

PDFDoubleSpinBox* PDFAppToolMarginFields::getSpinBox(const QString& parameter) const
{
    auto it = m_rows.find(parameter);
    return it != m_rows.cend() ? qobject_cast<PDFDoubleSpinBox*>(it->second.editor.data()) : nullptr;
}

QAbstractButton* PDFAppToolMarginFields::getCheckBox(const QString& parameter) const
{
    auto it = m_rows.find(parameter);
    return it != m_rows.cend() ? qobject_cast<QAbstractButton*>(it->second.editor.data()) : nullptr;
}

void PDFAppToolMarginFields::setRowEnabled(const QString& parameter, bool isEnabled)
{
    auto it = m_rows.find(parameter);
    if (it == m_rows.cend())
    {
        return;
    }
    for (QWidget* widget : { it->second.label.data(), it->second.editor.data() })
    {
        if (widget)
        {
            widget->setEnabled(isEnabled);
        }
    }
}

// ------------------------------------------------------------- PDFAppToolAnchorPanel

PDFAppToolAnchorPanel::PDFAppToolAnchorPanel(PDFAppDocumentView* view, PDFAppToolParameterModel* model, const QString& parameter, int cellSize, int panelWidth, QWidget* parent) :
    BaseClass(view, panelWidth, parent),
    m_model(model),
    m_parameter(parameter)
{
    // 391:23821: the caption Anchor, the grid, the name and the description
    addCaption(tr("Anchor"));
    m_grid = new PDFAnchorGrid(this);
    m_grid->setObjectName(QStringLiteral("toolAnchorGrid-") + parameter);
    m_grid->setCellSize(cellSize);
    m_grid->setAccessibleName(tr("Anchor"));
    getContentLayout()->addWidget(m_grid, 0, Qt::AlignLeft);
    m_name = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 13, PDFThemeColors::TextPrimary, this);
    m_name->setObjectName(QStringLiteral("toolAnchorName"));
    getContentLayout()->addWidget(m_name);
    const PDFDocumentToolParameterDefinition* definition = model ? PDFDocumentToolParameters::find(model->getDefinitions(), parameter) : nullptr;
    PDFLabel* description = PDFAppPageWidgets::createLabel(definition ? definition->getDescription() : QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, this);
    description->setWordWrap(true);
    getContentLayout()->addWidget(description);

    connect(m_grid.data(), &PDFAnchorGrid::anchorChosen, this, &PDFAppToolAnchorPanel::onAnchorChosen);
    if (model)
    {
        connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolAnchorPanel::onParameterChanged);
    }
    refresh();
}

PDFAppToolAnchorPanel::~PDFAppToolAnchorPanel() = default;

void PDFAppToolAnchorPanel::setTitleButton(QToolButton* button, const QString& titleFormat)
{
    m_titleButton = button;
    m_titleFormat = titleFormat;
    refresh();
}

QString PDFAppToolAnchorPanel::getAnchorTitle() const
{
    return m_model ? PDFAppToolGUIGeometryHelper::getOptionTitle(m_model, m_parameter, m_model->getValue(m_parameter).toString()) : QString();
}

void PDFAppToolAnchorPanel::refresh()
{
    const QString title = getAnchorTitle();
    if (m_grid && m_model)
    {
        m_grid->setAnchor(PDFDocumentToolGeometry::getAnchor(m_model->getValue(m_parameter).toString()));
    }
    if (m_name)
    {
        m_name->setText(title);
    }
    if (m_titleButton)
    {
        const QString text = m_titleFormat.isEmpty() ? title : m_titleFormat.arg(title);
        m_titleButton->setText(text);
        m_titleButton->setToolTip(text);
    }
}

void PDFAppToolAnchorPanel::onAnchorChosen(Qt::Alignment anchor)
{
    // The panel closes first: a change of a parameter may start anything
    QPointer<PDFAppToolAnchorPanel> guard(this);
    close();
    if (guard && m_model)
    {
        m_model->setValue(m_parameter, PDFDocumentToolGeometry::getAnchorName(anchor));
    }
    if (guard)
    {
        // The model decides (also for a refusal)
        refresh();
    }
}

void PDFAppToolAnchorPanel::onParameterChanged(const QString& name)
{
    if (name == m_parameter || name.isEmpty())
    {
        refresh();
    }
}

// ------------------------------------------------------------- PDFAppToolAnchorField

PDFAppToolAnchorComboBox::PDFAppToolAnchorComboBox(PDFDocumentToolGUI* gui, const QString& parameter, QWidget* parent) :
    BaseClass(parent),
    m_model(gui ? gui->getModel() : nullptr),
    m_parameter(parameter)
{
    setObjectName(QStringLiteral("toolAnchorCombo-") + parameter);

    // The nine anchors by the options of the parameter ("Middle center"), their ids as the data
    const PDFDocumentToolParameterDefinition* definition = m_model ? PDFDocumentToolParameters::find(m_model->getDefinitions(), parameter) : nullptr;
    if (definition)
    {
        for (const PDFDocumentToolParameterOption& option : definition->getOptions())
        {
            addItem(option.getTitle(), option.id);
        }
    }

    // The popover of the grid of 36 x 36 (356:9556, K14) is the list of the combo box
    const PDFAppToolHost* host = gui ? gui->getHost() : nullptr;
    m_panel = new PDFAppToolAnchorPanel(host ? host->getView() : nullptr, m_model, parameter,
                                        PDFAnchorGrid::COMPACT_CELL_SIZE, PDFAppToolGUIGeometryHelper::ANCHOR_PANEL_WIDTH, this);
    m_panel->setObjectName(QStringLiteral("toolAnchorPanel-") + parameter);

    connect(this, &QComboBox::activated, this, &PDFAppToolAnchorComboBox::onActivated);
    if (m_model)
    {
        connect(m_model.data(), &PDFAppToolParameterModel::changed, this, &PDFAppToolAnchorComboBox::onParameterChanged);
    }
    refresh();
}

PDFAppToolAnchorComboBox::~PDFAppToolAnchorComboBox() = default;

void PDFAppToolAnchorComboBox::showPopup()
{
    if (m_panel)
    {
        m_panel->popup(this, mapToGlobal(QPoint(0, height())).y() + 4);
    }
}

void PDFAppToolAnchorComboBox::hidePopup()
{
    if (m_panel)
    {
        m_panel->close();
    }
    BaseClass::hidePopup();
}

void PDFAppToolAnchorComboBox::refresh()
{
    // The model decides (Undo, Redo, a refusal); no activation
    const QSignalBlocker blocker(this);
    const int index = m_model ? findData(m_model->getValue(m_parameter).toString()) : -1;
    setCurrentIndex(index);
}

void PDFAppToolAnchorComboBox::onActivated(int index)
{
    // An arrow on the closed combo box: one undo step; the model announces it (refresh)
    const QString anchorId = itemData(index).toString();
    if (m_model && !anchorId.isEmpty() && m_model->getValue(m_parameter).toString() != anchorId)
    {
        QPointer<PDFAppToolAnchorComboBox> guard(this);
        m_model->setValue(m_parameter, anchorId);
        if (!guard)
        {
            return;
        }
    }
    refresh();
}

void PDFAppToolAnchorComboBox::onParameterChanged(const QString& name)
{
    if (name == m_parameter || name.isEmpty())
    {
        refresh();
    }
}

PDFAppToolAnchorField::PDFAppToolAnchorField(PDFDocumentToolGUI* gui, const QString& parameter, const QString& label, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolAnchorField-") + parameter);

    // field-Anchor 336:2317: the label, the combo box of 140 at the end
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppToolPanelBuilder::LABEL_GAP);
    PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Normal, 13, PDFThemeColors::TextSecondary, this);
    layout->addWidget(labelWidget, 1);
    m_comboBox = new PDFAppToolAnchorComboBox(gui, parameter, this);
    m_comboBox->setFixedWidth(PDFAppToolMarginFields::FIELD_WIDTH);
    m_comboBox->setAccessibleName(label);
    labelWidget->setBuddy(m_comboBox);
    layout->addWidget(m_comboBox);
}

PDFAppToolAnchorField::~PDFAppToolAnchorField() = default;

void PDFAppToolAnchorField::openPanel()
{
    if (m_comboBox)
    {
        m_comboBox->showPopup();
    }
}

// --------------------------------------------------------- PDFAppPageGeometryToolGUI

PDFAppPageGeometryToolGUI::PDFAppPageGeometryToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppPageGeometryToolGUI::~PDFAppPageGeometryToolGUI() = default;

PDFDocumentPointer PDFAppPageGeometryToolGUI::getShownDocument() const
{
    const PDFAppDocument* document = getDocument();
    return document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
}

const PDFPage* PDFAppPageGeometryToolGUI::getPage(PDFInteger page) const
{
    const PDFDocumentPointer document = getShownDocument();
    if (!document || page < 0 || size_t(page) >= document->getCatalog()->getPageCount())
    {
        return nullptr;
    }
    return document->getCatalog()->getPage(size_t(page));
}

PDFInteger PDFAppPageGeometryToolGUI::getPageCount() const
{
    const PDFDocumentPointer document = getShownDocument();
    return document ? PDFInteger(document->getCatalog()->getPageCount()) : 0;
}

std::vector<PDFInteger> PDFAppPageGeometryToolGUI::getTargetPages() const
{
    // The same pages as the core decides
    const PDFDocumentPointer document = getShownDocument();
    QString error;
    return document ? PDFDocumentToolGeometry::getPages(document.data(), getValues(), &error) : std::vector<PDFInteger>();
}

QVariantMap PDFAppPageGeometryToolGUI::getValues() const
{
    QVariantMap values = BaseClass::getRunParameters();
    completeValues(&values);
    return values;
}

void PDFAppPageGeometryToolGUI::completeValues(QVariantMap* values) const
{
    Q_UNUSED(values);
}

QVariantMap PDFAppPageGeometryToolGUI::getRunParameters() const
{
    return getValues();
}

QString PDFAppPageGeometryToolGUI::getPreviewPageTitle() const
{
    const PDFDocumentPointer document = getShownDocument();
    return PDFAppPanelWidgets::getPageTitle(document ? document->getCatalog() : nullptr, m_previewPage);
}

void PDFAppPageGeometryToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;

    // page-selection 389:22382 (All: "48 pages selected · 1 – 48")
    m_pageSelection = builder.addPageSelection(Helper::name(PDFDocumentToolCropPages::PARAMETER_PAGES), getSelectionLabel());
    m_binder = m_pageSelection ? m_pageSelection->findChild<PDFAppToolPageSelectionBinder*>() : nullptr;

    // The thumbnails of the document (the Thumbnail item of the panel Thumbnails): the page of the
    // preview is the selected one (K48)
    PDFAppThumbnailListView* view = new PDFAppThumbnailListView(builder.getWidget());
    view->setObjectName(QStringLiteral("toolGeometryThumbnails"));
    PDFAppPanelWidgets::prepareView(view);
    view->setSidePadding(0);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setAccessibleName(tr("Page of the preview"));
    m_thumbnailModel = new PDFAppThumbnailsModel(view);
    m_thumbnailDelegate = new PDFThumbnailDelegate(view);
    m_thumbnailDelegate->setPageSize(Helper::THUMBNAIL_SLOT);
    view->setModel(m_thumbnailModel);
    view->setItemDelegate(m_thumbnailDelegate);
    PDFComponentPainter::ThumbnailItem item;
    item.pageSize = Helper::THUMBNAIL_SLOT;
    view->setItemSize(PDFComponentPainter::getThumbnailItemSize(item));
    m_thumbnailModel->setPictureSize(Helper::THUMBNAIL_SLOT, view->devicePixelRatioF());
    m_thumbnailScheduler = new PDFAppThumbnailScheduler(m_thumbnailModel, view, view);
    connect(view, &QAbstractItemView::clicked, this, &PDFAppPageGeometryToolGUI::onThumbnailClicked);

    // M12 filters the viewport. The filter sees the viewport also while the view is being
    // destroyed (no longer a QAbstractScrollArea): it compares only the address of the viewport,
    // and the filter goes with the view (the slot of destroyed, a former view here)
    forgetThumbnailView();
    m_thumbnailView = view;
    m_thumbnailViewport = view->viewport();
    m_thumbnailViewport->installEventFilter(this);
    connect(view, &QObject::destroyed, this, &PDFAppPageGeometryToolGUI::onThumbnailViewDestroyed);
    builder.addWidget(view);
    builder.setFillsPanel(view);

    updateThumbnails();
}

void PDFAppPageGeometryToolGUI::updateThumbnails()
{
    if (!m_thumbnailView || !m_thumbnailModel)
    {
        return;
    }

    PDFAppDocument* document = getDocument();
    const PDFAppToolHost* host = getHost();
    m_thumbnailModel->setDocument(getShownDocument(), document ? document->getId() : QString());
    if (m_thumbnailScheduler)
    {
        m_thumbnailScheduler->setProxy(host && host->getView() ? host->getView()->getProxy() : nullptr);
        m_thumbnailScheduler->setAppDocument(document);
    }

    const QModelIndex index = m_thumbnailModel->index(int(m_previewPage), 0);
    if (index.isValid() && m_thumbnailView->selectionModel())
    {
        const QSignalBlocker blocker(m_thumbnailView->selectionModel());
        m_thumbnailView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        m_thumbnailView->viewport()->update();
    }
    scrollToPreviewPage();
}

void PDFAppPageGeometryToolGUI::scrollToPreviewPage()
{
    if (!m_thumbnailView || !m_thumbnailModel)
    {
        return;
    }
    const QModelIndex index = m_thumbnailModel->index(int(m_previewPage), 0);
    if (index.isValid())
    {
        // QListView lays out its pending items first: the columns of the width of now
        m_thumbnailView->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
}

bool PDFAppPageGeometryToolGUI::eventFilter(QObject* watched, QEvent* event)
{
    // M12: the view had another width, when the thumbnail was scrolled into it (one column at
    // the entry, two after the panel has its width): the scroll is done again after the view
    // has laid out its items in the new width (from the event loop)
    // The type first, then the address of the viewport only: no call through the view, which
    // may be in its destructor (UBSan vptr)
    if (event->type() == QEvent::Resize && m_thumbnailViewport.isObject(watched))
    {
        QMetaObject::invokeMethod(this, &PDFAppPageGeometryToolGUI::scrollToPreviewPage, Qt::QueuedConnection);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppPageGeometryToolGUI::onThumbnailViewDestroyed(QObject* object)
{
    // ~QWidget of the view emits destroyed before it deletes its children: the viewport is
    // still a whole QWidget. Only the current view is forgotten (a former one is forgotten,
    // when the new one comes)
    QWidget* viewport = m_thumbnailViewport.data();
    if (viewport && viewport->parent() == object)
    {
        forgetThumbnailView();
    }
}

void PDFAppPageGeometryToolGUI::forgetThumbnailView()
{
    if (QWidget* viewport = m_thumbnailViewport.data())
    {
        viewport->removeEventFilter(this);
    }
    m_thumbnailViewport = nullptr;
    m_thumbnailView = nullptr;
}

void PDFAppPageGeometryToolGUI::onThumbnailClicked(const QModelIndex& index)
{
    // From the event loop: Qt uses the view after its signal, a change may rebuild anything
    if (index.isValid())
    {
        QMetaObject::invokeMethod(this, &PDFAppPageGeometryToolGUI::setPreviewPage, Qt::QueuedConnection, PDFInteger(index.row()));
    }
}

void PDFAppPageGeometryToolGUI::setPreviewPage(PDFInteger page)
{
    const PDFInteger pageCount = getPageCount();
    if (pageCount <= 0)
    {
        return;
    }
    m_previewPage = qBound(PDFInteger(0), page, pageCount - 1);
    updateThumbnails();

    // The view goes to the page of the preview: its page item in the status bar says it (K48,
    // 389:22382 "Page 8 / 48"); the view runs its slots, the last call
    QPointer<PDFAppPageGeometryToolGUI> guard(this);
    updateAll();
    if (guard)
    {
        showPreviewPageInView();
    }
}

void PDFAppPageGeometryToolGUI::showPreviewPageInView()
{
    // The view is hidden by the canvas during the mode: its current page is the page item of the
    // status bar; leaving the mode restores the page of the entry (restoreViewState)
    const PDFAppToolHost* host = getHost();
    PDFAppDocumentView* view = host ? host->getView() : nullptr;
    if (view && m_previewPage >= 0 && view->getCurrentPageIndex() != m_previewPage)
    {
        view->goToPage(m_previewPage);
    }
}

QWidget* PDFAppPageGeometryToolGUI::createCanvasPage(QWidget* parent)
{
    const PDFAppToolHost* host = getHost();
    if (m_previewPage < 0)
    {
        // K48: the current page of the view at the entry (389:22382 "Page 8")
        m_previewPage = host ? host->getCurrentPage() : 0;
    }
    const PDFInteger pageCount = getPageCount();
    m_previewPage = pageCount > 0 ? qBound(PDFInteger(0), m_previewPage, pageCount - 1) : -1;

    m_canvas = new PDFAppToolPageCanvas(getStageCount(), parent);
    for (int i = 0; i < m_canvas->getStageCount(); ++i)
    {
        m_canvas->getStage(i)->setProxy(host && host->getView() ? host->getView()->getProxy() : nullptr);
    }

    if (PDFDocumentToolShell* shell = getShell(); shell && !m_isShellConnected)
    {
        m_isShellConnected = true;
        connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppPageGeometryToolGUI::onShellStateChangedSlot);
    }

    onCanvasCreated();
    updateThumbnails();
    updateAll();
    return m_canvas.data();
}

void PDFAppPageGeometryToolGUI::onShellStateChangedSlot(PDFDocumentToolShell::State state)
{
    onShellStateChanged(state);
}

void PDFAppPageGeometryToolGUI::onShellStateChanged(PDFDocumentToolShell::State state)
{
    // 389:24633: nothing of the tool below the result dialog
    m_isDeciding = state == PDFDocumentToolShell::State::Deciding;
    updateTool();
}

void PDFAppPageGeometryToolGUI::updateAll()
{
    // The validation of the core with the values of now (the error callouts, the Primary button)
    m_valuesError.clear();
    const PDFDocumentPointer document = getShownDocument();
    const PDFDocumentToolShell* shell = getShell();
    if (document && shell && shell->getTool())
    {
        PDFDocumentToolInputDocument input;
        input.document = document;
        const PDFDocumentToolContext context = PDFDocumentToolContext::create(std::move(input));
        for (const PDFDocumentToolParameterError& error : shell->getTool()->validate(context, getValues()))
        {
            m_valuesError = error.message;
            break;
        }
    }

    updateTool();

    // The slots of the state of the Primary button may delete this GUI: the status is said only by
    // a living GUI, the last thing
    QPointer<PDFAppPageGeometryToolGUI> guard(this);
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
}

bool PDFAppPageGeometryToolGUI::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && m_valuesError.isEmpty() && !getTargetPages().empty();
}

void PDFAppPageGeometryToolGUI::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    updateAll();
}

void PDFAppPageGeometryToolGUI::followPages()
{
    const PDFDocumentPointer document = getShownDocument();
    const PDFDocumentPointer previousDocument = m_pageDocument;
    if (!document || document == previousDocument)
    {
        return;
    }

    // The state first: a slot of the model, which runs this again, finds the new version
    m_pageDocument = document;
    const PDFDocumentToolShell* shell = getShell();
    if (!previousDocument || !shell || !shell->getTool() || !getModel())
    {
        return;
    }

    const std::shared_ptr<const PDFDocumentTool> tool = shell->getTool();
    const std::vector<PDFInteger> pageMap = PDFDocumentToolPageSelection::mapPageIndices(previousDocument.data(), document.data());
    if (m_previewPage >= 0 && size_t(m_previewPage) < pageMap.size() && pageMap[size_t(m_previewPage)] >= 0)
    {
        m_previewPage = pageMap[size_t(m_previewPage)];
    }

    QPointer<PDFAppPageGeometryToolGUI> guard(this);
    if (m_binder)
    {
        m_binder->followPages(previousDocument.data(), pageMap);
        if (!guard)
        {
            return;
        }
    }
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->mapValues(PDFAppToolGeometryPageFollower(tool.get(), previousDocument.data(), document.data()));
    }
}

void PDFAppPageGeometryToolGUI::onDocumentChanged()
{
    if (!m_pageDocument)
    {
        m_pageDocument = getShownDocument();
    }

    // An assistant may change the pages below an open tool: the values follow their pages
    QPointer<PDFAppPageGeometryToolGUI> guard(this);
    followPages();
    if (!guard)
    {
        return;
    }

    const PDFInteger pageCount = getPageCount();
    m_previewPage = pageCount > 0 ? qBound(PDFInteger(0), m_previewPage, pageCount - 1) : -1;
    updateThumbnails();
    updateAll();
}

// ------------------------------------------------------------------ PDFAppCropToolGUI

PDFAppCropToolGUI::PDFAppCropToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppCropToolGUI::~PDFAppCropToolGUI() = default;

QString PDFAppCropToolGUI::getSelectionLabel() const
{
    return tr("Pages to crop");
}

QString PDFAppCropToolGUI::getMode() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolCropPages::PARAMETER_MODE)).toString() : QString();
}

QString PDFAppCropToolGUI::getMenuMode() const
{
    const QString mode = getMode();
    if (mode != QLatin1String(PDFDocumentToolCropPages::MODE_REMOVE))
    {
        return mode;
    }
    return m_lastMode.isEmpty() ? QString::fromLatin1(PDFDocumentToolCropPages::MODE_EQUAL_MARGINS) : m_lastMode;
}

QToolButton* PDFAppCropToolGUI::getModeButton() const
{
    return m_modePanel ? m_modePanel->getButton() : nullptr;
}

PDFTileRow* PDFAppCropToolGUI::getModeRow(const QString& mode) const
{
    auto it = m_modeRows.find(mode);
    return it != m_modeRows.cend() ? it->second.data() : nullptr;
}

void PDFAppCropToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;

    // tb-crop-mode 389:22382 with its menu 389:22931 (Equal margins, Custom box); Remove Crop is
    // its own toggle (K21)
    m_modePanel = builder.addMenu(QStringLiteral("cropMode"), QStringLiteral("margins"), QString(), Helper::MENU_WIDTH);
    m_modeRows.clear();
    for (const char* mode : { PDFDocumentToolCropPages::MODE_EQUAL_MARGINS, PDFDocumentToolCropPages::MODE_CUSTOM_BOX })
    {
        const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(getModel()->getDefinitions(), Helper::name(PDFDocumentToolCropPages::PARAMETER_MODE));
        for (const PDFDocumentToolParameterOption& option : definition->getOptions())
        {
            if (option.id == QLatin1String(mode))
            {
                PDFTileRow* row = m_modePanel->addRow(option.getTitle(), option.getDescription(), option.iconName, PDFTileRow::Kind::MenuItem);
                row->setObjectName(QStringLiteral("toolOption-") + option.id);
                row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
                row->setProperty("cropMode", option.id);
                connect(row, &PDFTileRow::clicked, this, &PDFAppCropToolGUI::onModeRowClicked);
                m_modeRows[option.id] = row;
            }
        }
    }

    builder.addActionDivider();
    m_removeButton = builder.addAction(tr("Remove Crop"), QStringLiteral("crop-off"), this, &PDFAppCropToolGUI::toggleRemoveCrop);
    m_removeButton->setCheckable(true);
}

void PDFAppCropToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;

    // 398:28158: the red callout at the top
    m_errorCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString());
    m_errorCallout->setObjectName(QStringLiteral("toolCropErrorCallout"));

    // section-Margins 389:22382 / section-Crop box 389:23500
    m_marginsHeader = builder.addSection(tr("Margins"));
    m_marginFields = builder.addMarginFields({ Helper::name(PDFDocumentToolCropPages::PARAMETER_MARGIN) }, tr("The same margin is cut from all four sides."));
    builder.endSection();
    m_boxHeader = builder.addSection(tr("Crop box"));
    m_boxFields = builder.addMarginFields({ Helper::name(PDFDocumentToolCropPages::PARAMETER_LEFT), Helper::name(PDFDocumentToolCropPages::PARAMETER_TOP),
                                            Helper::name(PDFDocumentToolCropPages::PARAMETER_WIDTH), Helper::name(PDFDocumentToolCropPages::PARAMETER_HEIGHT) }, QString());
    builder.endSection();

    // 389:24078: the pages have more sizes
    m_sizesCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString());
    m_sizesCallout->setObjectName(QStringLiteral("toolCropSizesCallout"));

    builder.addSection(tr("Result"));
    m_result = builder.addKeyValues();
    m_result->setRowGap(PDFAppToolGUIGeometryHelper::KEY_VALUE_ROW_GAP);
    builder.endSection();

    m_hiddenCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), tr("Cropping hides, it does not delete"),
                                         tr("Content outside the crop box stays in the file and can be brought back. To remove content for good, use Redact Content."));
    m_hiddenCallout->setObjectName(QStringLiteral("toolCropHiddenCallout"));
    m_removeCallout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("Hidden content becomes visible again"),
                                         tr("Remove Crop sets the crop box of the selected pages back to the whole page. Nothing is deleted or added."));
    m_removeCallout->setObjectName(QStringLiteral("toolCropRemoveCallout"));
    updateAll();
}

void PDFAppCropToolGUI::onCanvasCreated()
{
    PDFAppToolPageCanvas* canvas = getCanvas();
    PDFAppToolPageStage* stage = canvas ? canvas->getStage(0) : nullptr;
    if (!stage)
    {
        return;
    }
    m_overlay = new PDFAppCropOverlay(stage);
    connect(m_overlay.data(), &PDFAppCropOverlay::rectChanged, this, &PDFAppCropToolGUI::onOverlayRectChanged);
    connect(m_overlay.data(), &PDFAppCropOverlay::rectChosen, this, &PDFAppCropToolGUI::onOverlayRectChosen);
}

QRectF PDFAppCropToolGUI::getPreviewCropRect() const
{
    const PDFPage* page = getPage(getPreviewPage());
    if (!page)
    {
        return QRectF();
    }

    // The crop box of the values on the page of the preview (the reference of the custom box)
    PDFPageGeometry::CropSettings settings = PDFDocumentToolCropPages::getSettings(getValues());
    if (settings.mode == PDFPageGeometry::CropMode::Remove)
    {
        return QRectF();
    }
    const std::optional<QRectF> cropBox = PDFPageGeometry::computeCropBox(page, page, settings);
    return cropBox ? PDFAppToolGUIGeometryHelper::mapToShown(page, page->getEffectiveCropBox(), *cropBox) : QRectF();
}

QString PDFAppCropToolGUI::getSizesText(int* sizeCount) const
{
    // 389:24078 "46 pages are A4, 2 pages are A3" (sizes as shown, whole millimeters)
    std::map<std::pair<qint64, qint64>, int> sizes;
    for (const PDFInteger page : getTargetPages())
    {
        if (const PDFPage* pdfPage = getPage(page))
        {
            const QSizeF size = PDFPageGeometry::getShownSizeMM(pdfPage);
            ++sizes[{ std::llround(size.width()), std::llround(size.height()) }];
        }
    }

    *sizeCount = int(sizes.size());
    if (sizes.size() < 2)
    {
        return QString();
    }

    std::vector<std::pair<int, QSizeF>> ordered;
    for (const auto& [size, count] : sizes)
    {
        ordered.emplace_back(count, QSizeF(size.first, size.second));
    }
    std::stable_sort(ordered.begin(), ordered.end(), &PDFAppToolGUIGeometryHelper::isMoreCommon);

    QStringList parts;
    for (const auto& [count, size] : ordered)
    {
        const QString name = PDFAppToolGUIGeometryHelper::getPaperName(size);
        parts << tr("%Ln pages are %1", nullptr, count).arg(name.isEmpty() ? PDFAppToolGUIGeometryHelper::formatSize(size) : name);
        if (parts.size() == 3)
        {
            break;
        }
    }
    return parts.join(QStringLiteral(", "));
}

void PDFAppCropToolGUI::updateTool()
{
    using Helper = PDFAppToolGUIGeometryHelper;

    const QString mode = getMode();
    const QString menuMode = getMenuMode();
    const bool isRemove = mode == QLatin1String(PDFDocumentToolCropPages::MODE_REMOVE);
    const bool isCustom = menuMode == QLatin1String(PDFDocumentToolCropPages::MODE_CUSTOM_BOX);
    const QString error = getValuesError();
    const PDFPage* page = getPage(getPreviewPage());
    const PDFAppDocument* appDocument = getDocument();
    const std::vector<PDFInteger> targets = getTargetPages();

    // The toolbar: the menu shows the mode also while Remove Crop is on (405:32319)
    if (QToolButton* button = getModeButton())
    {
        button->setText(tr("Crop: %1").arg(Helper::getOptionTitle(getModel(), Helper::name(PDFDocumentToolCropPages::PARAMETER_MODE), menuMode)));
        button->setEnabled(!isRemove && !isDeciding());
    }
    for (const auto& [rowMode, row] : m_modeRows)
    {
        if (row)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(rowMode == menuMode);
        }
    }
    if (m_removeButton)
    {
        const QSignalBlocker blocker(m_removeButton);
        m_removeButton->setChecked(isRemove);
        m_removeButton->setEnabled(!isDeciding());
    }

    // The right panel (K54: the margins of the mode stay shown, disabled, while the crop is removed)
    for (QWidget* widget : { static_cast<QWidget*>(m_marginsHeader.data()), static_cast<QWidget*>(m_marginFields.data()) })
    {
        if (widget)
        {
            widget->setVisible(!isCustom);
            widget->setEnabled(!isRemove);
        }
    }
    for (QWidget* widget : { static_cast<QWidget*>(m_boxHeader.data()), static_cast<QWidget*>(m_boxFields.data()) })
    {
        if (widget)
        {
            widget->setVisible(isCustom);
            widget->setEnabled(!isRemove);
        }
    }
    if (m_errorCallout)
    {
        // 398:28158 "The crop box is wider than the page" with the largest value
        const PDFPageGeometry::CropSettings settings = PDFDocumentToolCropPages::getSettings(getValues());
        QString title = tr("The crop leaves nothing of the pages");
        if (isCustom && page)
        {
            const QSizeF size = PDFPageGeometry::getShownSizeMM(page);
            if (settings.boxMM.left() + settings.boxMM.width() > size.width())
            {
                title = tr("The crop box is wider than the page");
            }
            else if (settings.boxMM.top() + settings.boxMM.height() > size.height())
            {
                title = tr("The crop box is higher than the page");
            }
        }
        m_errorCallout->setTexts(title, error);
        m_errorCallout->setVisible(!error.isEmpty() && !targets.empty());
    }

    int sizeCount = 0;
    const QString sizesText = getSizesText(&sizeCount);
    if (m_sizesCallout)
    {
        // 389:24078
        const QString text = isCustom ? tr("%1. The crop box keeps its margins on every page; the values below are shown for %2.").arg(sizesText, getPreviewPageTitle().toLower())
                                      : tr("%1. The margin is cut from every page; the values below are shown for %2.").arg(sizesText, getPreviewPageTitle().toLower());
        m_sizesCallout->setTexts(tr("The pages have %Ln sizes", nullptr, sizeCount), text);
        m_sizesCallout->setVisible(sizeCount > 1 && !isRemove);
    }
    if (m_hiddenCallout)
    {
        m_hiddenCallout->setVisible(!isRemove);
    }
    if (m_removeCallout)
    {
        m_removeCallout->setVisible(isRemove);
    }

    // Result: Page 8 now, After crop, Pages (389:22382)
    const QSizeF now = page ? PDFPageGeometry::getShownSizeMM(page) : QSizeF();
    QString after = QStringLiteral("–");
    if (page && error.isEmpty())
    {
        if (isRemove)
        {
            const QSizeF media = PDFPage::getRotatedBox(page->getMediaBox().normalized(), page->getPageRotation()).size() / page->getMillimetersToUnits();
            after = Helper::formatSize(media);
        }
        else
        {
            const QRectF crop = getPreviewCropRect();
            if (crop.isValid())
            {
                after = Helper::formatSize(crop.size() / page->getMillimetersToUnits());
            }
        }
    }
    if (m_result)
    {
        m_result->clear();
        m_result->addRow(tr("%1 now").arg(getPreviewPageTitle()), page ? Helper::formatSize(now) : QStringLiteral("–"));
        m_result->addRow(tr("After crop"), after);
        const QString pages = PDFAppLocale::formatNumber(qint64(targets.size()));
        m_result->addRow(tr("Pages"), sizeCount > 1 ? tr("%1 · %Ln sizes", nullptr, sizeCount).arg(pages) : pages);
    }

    // The canvas: the page of the preview with the box
    PDFAppToolPageCanvas* canvas = getCanvas();
    PDFAppToolPageStage* stage = canvas ? canvas->getStage(0) : nullptr;
    if (canvas && stage)
    {
        canvas->setTitle(tr("%1 · crop preview").arg(getPreviewPageTitle()));
        stage->setShownBox(isRemove ? PDFPageGeometry::Box::MediaBox : PDFPageGeometry::Box::CropBox);
        stage->setPage(getShownDocument(), appDocument ? appDocument->getId() : QString(), getPreviewPage());

        QString caption;
        bool isCaptionError = false;
        if (m_overlay && page)
        {
            const QSizeF pageSize = stage->getPageSize();
            m_overlay->setBounds(QRectF(QPointF(0.0, 0.0), pageSize));
            m_overlay->setInactive(isRemove || isDeciding());
            const qreal factor = page->getMillimetersToUnits();
            if (isRemove)
            {
                // 405:32319: the crop, which is removed, dashed over the whole page
                m_overlay->setRect(Helper::mapToShown(page, page->getMediaBox().normalized(), page->getEffectiveCropBox()));
                caption = tr("The whole page is shown again · the dashed line is the crop that will be removed");
            }
            else if (!error.isEmpty() && isCustom)
            {
                // K20: the box of the values limited to the page (398:28158)
                const PDFPageGeometry::CropSettings settings = PDFDocumentToolCropPages::getSettings(getValues());
                const QRectF box(settings.boxMM.left() * factor, settings.boxMM.top() * factor, settings.boxMM.width() * factor, settings.boxMM.height() * factor);
                m_overlay->setRect(box.intersected(QRectF(QPointF(0.0, 0.0), pageSize)));
                caption = tr("The box in the preview is limited to the page");
            }
            else
            {
                const QRectF crop = getPreviewCropRect();
                m_overlay->setRect(crop.isValid() ? crop : QRectF(QPointF(0.0, 0.0), pageSize));
                if (!error.isEmpty())
                {
                    caption = error;
                    isCaptionError = true;
                }
                else if (isCustom)
                {
                    caption = tr("Result %1 · drag the box or its handles").arg(after);
                }
                else
                {
                    const QVariantMap values = getValues();
                    caption = tr("Result %1 · %2 mm removed on every side").arg(after, PDFAppBlankToolGUI::formatMillimeters(values.value(Helper::name(PDFDocumentToolCropPages::PARAMETER_MARGIN)).toDouble()));
                }
            }
        }
        canvas->setCaption(0, caption, isCaptionError);
    }
}

void PDFAppCropToolGUI::onModeRowClicked()
{
    QObject* row = sender();
    if (row)
    {
        chooseMode(row->property("cropMode").toString());
    }
}

void PDFAppCropToolGUI::chooseMode(const QString& mode)
{
    // The panel closes first: a change of a parameter may start anything
    QPointer<PDFAppCropToolGUI> guard(this);
    if (m_modePanel)
    {
        m_modePanel->close();
    }
    if (guard && getModel())
    {
        m_lastMode = mode;
        getModel()->setValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolCropPages::PARAMETER_MODE), mode);
    }
    if (guard)
    {
        updateTool();
    }
}

void PDFAppCropToolGUI::toggleRemoveCrop()
{
    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    // K21: a toggle; off returns the mode of the menu
    const QString mode = getMode();
    QString newMode = QString::fromLatin1(PDFDocumentToolCropPages::MODE_REMOVE);
    if (mode == QLatin1String(PDFDocumentToolCropPages::MODE_REMOVE))
    {
        newMode = getMenuMode();
    }
    else
    {
        m_lastMode = mode;
    }

    // The model announces the change (the last call)
    model->setValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolCropPages::PARAMETER_MODE), newMode);
}

void PDFAppCropToolGUI::onOverlayRectChanged(const QRectF& rect)
{
    // During a drag only the caption says the size (the values change at its end)
    const PDFPage* page = getPage(getPreviewPage());
    PDFAppToolPageCanvas* canvas = getCanvas();
    if (page && canvas)
    {
        canvas->setCaption(0, tr("Result %1 · drag the box or its handles").arg(PDFAppToolGUIGeometryHelper::formatSize(rect.size() / page->getMillimetersToUnits())));
    }
}

void PDFAppCropToolGUI::onOverlayRectChosen(const QRectF& rect)
{
    using Helper = PDFAppToolGUIGeometryHelper;

    const PDFPage* page = getPage(getPreviewPage());
    PDFAppToolParameterModel* model = getModel();
    if (!page || !model || isDeciding())
    {
        return;
    }

    // The box of the user is a custom box in millimeters (one undo step with the mode)
    const qreal factor = page->getMillimetersToUnits();
    QVariantMap values;
    values.insert(Helper::name(PDFDocumentToolCropPages::PARAMETER_MODE), QString::fromLatin1(PDFDocumentToolCropPages::MODE_CUSTOM_BOX));
    values.insert(Helper::name(PDFDocumentToolCropPages::PARAMETER_LEFT), std::round(rect.left() / factor * 100.0) / 100.0);
    values.insert(Helper::name(PDFDocumentToolCropPages::PARAMETER_TOP), std::round(rect.top() / factor * 100.0) / 100.0);
    values.insert(Helper::name(PDFDocumentToolCropPages::PARAMETER_WIDTH), std::round(rect.width() / factor * 100.0) / 100.0);
    values.insert(Helper::name(PDFDocumentToolCropPages::PARAMETER_HEIGHT), std::round(rect.height() / factor * 100.0) / 100.0);
    m_lastMode = QString::fromLatin1(PDFDocumentToolCropPages::MODE_CUSTOM_BOX);

    // The model announces the change (the last call)
    model->setValues(values);
}

void PDFAppCropToolGUI::completeValues(QVariantMap* values) const
{
    // The custom box is a box of the page of the preview (the reference page)
    values->insert(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolCropPages::PARAMETER_REFERENCE_PAGE), qint64(getPreviewPage() + 1));
}

QString PDFAppCropToolGUI::getConfirmText() const
{
    // 389:22382 "Crop 48 Pages", 405:32319 "Remove Crop from 48 Pages"
    const int count = int(getTargetPages().size());
    if (getMode() == QLatin1String(PDFDocumentToolCropPages::MODE_REMOVE))
    {
        return count > 0 ? tr("Remove Crop from %Ln Pages", nullptr, count) : tr("Remove Crop");
    }
    return count > 0 ? tr("Crop %Ln Pages", nullptr, count) : tr("Crop Pages");
}

QString PDFAppCropToolGUI::getStatusText() const
{
    using Helper = PDFAppToolGUIGeometryHelper;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const PDFPage* page = getPage(getPreviewPage());
    const int count = int(getTargetPages().size());
    if (!page || count == 0)
    {
        return BaseClass::getStatusText();
    }

    const QString now = Helper::formatSize(PDFPageGeometry::getShownSizeMM(page));
    if (!getValuesError().isEmpty())
    {
        // 398:28158
        return tr("%1 · the crop box reaches outside the page").arg(title);
    }

    const QVariantMap values = getValues();
    const QString mode = getMode();
    if (mode == QLatin1String(PDFDocumentToolCropPages::MODE_REMOVE))
    {
        // 405:32319 "Crop Pages · the crop of 48 pages will be removed · 190 × 277 mm → 210 × 297 mm"
        const QSizeF media = PDFPage::getRotatedBox(page->getMediaBox().normalized(), page->getPageRotation()).size() / page->getMillimetersToUnits();
        return tr("%1 · the crop of %Ln pages will be removed · %2 → %3", nullptr, count).arg(title, now, Helper::formatSize(media));
    }

    const QRectF crop = getPreviewCropRect();
    const QString after = Helper::formatSize(crop.size() / page->getMillimetersToUnits());
    if (mode == QLatin1String(PDFDocumentToolCropPages::MODE_CUSTOM_BOX))
    {
        // 389:23500 "Crop Pages · crop box 170 × 240 mm at 20, 25 mm · drag the handles or type the values"
        return tr("%1 · crop box %2 at %3, %4 mm · drag the handles or type the values")
                .arg(title, after, PDFAppBlankToolGUI::formatMillimeters(values.value(Helper::name(PDFDocumentToolCropPages::PARAMETER_LEFT)).toDouble()),
                     PDFAppBlankToolGUI::formatMillimeters(values.value(Helper::name(PDFDocumentToolCropPages::PARAMETER_TOP)).toDouble()));
    }

    // 389:22382 "Crop Pages · 10 mm off every side · 210 × 297 mm → 190 × 277 mm · 48 pages"
    return tr("%1 · %2 mm off every side · %3 → %4 · %Ln pages", nullptr, count)
            .arg(title, PDFAppBlankToolGUI::formatMillimeters(values.value(Helper::name(PDFDocumentToolCropPages::PARAMETER_MARGIN)).toDouble()), now, after);
}

void PDFAppCropToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 389:24633 "Pages cropped"
    input->title = getMode() == QLatin1String(PDFDocumentToolCropPages::MODE_REMOVE) ? tr("Crop removed") : tr("Pages cropped");
}

// ---------------------------------------------------------------- PDFAppResizeToolGUI

PDFAppResizeToolGUI::PDFAppResizeToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppResizeToolGUI::~PDFAppResizeToolGUI() = default;

QString PDFAppResizeToolGUI::getSelectionLabel() const
{
    return tr("Pages to resize");
}

QString PDFAppResizeToolGUI::getPageSizeId() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolResizePages::PARAMETER_PAGE_SIZE)).toString() : QString();
}

bool PDFAppResizeToolGUI::isScaleToFit() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model && model->getValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolResizePages::PARAMETER_CONTENT)).toString() != QLatin1String(PDFDocumentToolResizePages::CONTENT_KEEP_SIZE);
}

QString PDFAppResizeToolGUI::getSizeName(const QSizeF& sizeMM)
{
    // "Letter · 216 × 279 mm" (391:23146)
    const QString name = PDFAppToolGUIGeometryHelper::getPaperName(sizeMM);
    const QString size = PDFAppToolGUIGeometryHelper::formatSize(sizeMM);
    return name.isEmpty() ? size : tr("%1 · %2").arg(name, size);
}

PDFPageGeometry::ResizePlacement PDFAppResizeToolGUI::getPreviewPlacement() const
{
    const PDFDocumentPointer document = getShownDocument();
    return PDFPageGeometry::computeResize(getPage(getPreviewPage()), PDFDocumentToolResizePages::getSettings(getValues()), document ? &document->getStorage() : nullptr);
}

void PDFAppResizeToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;

    // tool-actions 391:23146 with the menus 391:23821
    builder.addEnumMenu(Helper::name(PDFDocumentToolResizePages::PARAMETER_PAGE_SIZE), QStringLiteral("file"), tr("Size: %1"), Helper::MENU_WIDTH);
    PDFAppToolEnumMenu* orientation = builder.addEnumMenu(Helper::name(PDFDocumentToolResizePages::PARAMETER_ORIENTATION), QStringLiteral("orientation-auto"), tr("Orientation: %1"), Helper::MENU_WIDTH);
    m_orientationButton = orientation ? orientation->getButton() : nullptr;
    builder.addEnumMenu(Helper::name(PDFDocumentToolResizePages::PARAMETER_CONTENT), QStringLiteral("scale"), tr("Content: %1"), Helper::MENU_WIDTH);
    m_anchorGrid = builder.addAnchorMenu(Helper::name(PDFDocumentToolResizePages::PARAMETER_ANCHOR), tr("Anchor: %1"), Helper::ANCHOR_PANEL_WIDTH);
}

void PDFAppResizeToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;
    using Tool = PDFDocumentToolResizePages;

    // 391:25272: the red callout at the top
    m_errorCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), tr("Margins exceed the page size"), QString());
    m_errorCallout->setObjectName(QStringLiteral("toolResizeErrorCallout"));

    // 391:24594: Page size of Custom
    m_sizeHeader = builder.addSection(tr("Page size"));
    m_sizeFields = builder.addMarginFields({ Helper::name(Tool::PARAMETER_WIDTH), Helper::name(Tool::PARAMETER_HEIGHT) }, QString());
    builder.endSection();

    builder.addSection(tr("Margins"));
    m_marginFields = builder.addMarginFields({ Helper::name(Tool::PARAMETER_MARGIN_LEFT), Helper::name(Tool::PARAMETER_MARGIN_TOP),
                                               Helper::name(Tool::PARAMETER_MARGIN_RIGHT), Helper::name(Tool::PARAMETER_MARGIN_BOTTOM) }, QString());
    builder.endSection();

    builder.addSection(tr("Content"));
    m_contentFields = builder.addMarginFields({ Helper::name(Tool::PARAMETER_OFFSET_X), Helper::name(Tool::PARAMETER_OFFSET_Y),
                                                Helper::name(Tool::PARAMETER_PRESERVE_ASPECT_RATIO), Helper::name(Tool::PARAMETER_SCALE_ANNOTATIONS) }, QString());
    builder.endSection();
    // 391:24594: the note of Keep size
    PDFAppParagraph* note = builder.addNote(tr("With Content: Keep size nothing on the pages is rewritten – only the page boxes change, so the step can be reversed by another resize. The scaling options are not used."));
    note->setObjectName(QStringLiteral("toolResizeKeepSizeNote"));
    m_keepSizeNote = note;

    // K55: Summary stays also in Keep size
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->setRowGap(PDFAppToolGUIGeometryHelper::KEY_VALUE_ROW_GAP);
    m_summary->addRow(tr("Pages"), QString());
    m_summary->addRow(tr("Page size"), QString());
    m_summary->addRow(tr("Content"), QString());
    m_summary->addRow(tr("Orientation"), QString());
    builder.endSection();
    updateAll();
}

void PDFAppResizeToolGUI::onCanvasCreated()
{
    PDFAppToolPageCanvas* canvas = getCanvas();
    PDFAppToolPageStage* after = canvas ? canvas->getStage(1) : nullptr;
    if (after)
    {
        m_marginsOverlay = new PDFAppPageBoxesOverlay(after, PDFAppPageBoxesOverlay::Coordinates::Sheet);
    }
}

void PDFAppResizeToolGUI::updateTool()
{
    using Helper = PDFAppToolGUIGeometryHelper;
    using Tool = PDFDocumentToolResizePages;

    const QVariantMap values = getValues();
    const QString sizeId = getPageSizeId();
    const bool isCustom = sizeId == QLatin1String(Tool::SIZE_CUSTOM);
    const bool isKeep = sizeId == QLatin1String(Tool::SIZE_KEEP);
    const bool isScale = isScaleToFit();
    const PDFPage* page = getPage(getPreviewPage());
    const PDFPageGeometry::ResizePlacement placement = getPreviewPlacement();
    const QString error = getValuesError();
    const PDFAppDocument* appDocument = getDocument();

    // The toolbar: Orientation is for a named size (391:24594); nothing below the result dialog
    if (m_orientationButton)
    {
        m_orientationButton->setEnabled(!isCustom && !isKeep && !isDeciding());
    }

    // The right panel
    for (QWidget* widget : { static_cast<QWidget*>(m_sizeHeader.data()), static_cast<QWidget*>(m_sizeFields.data()) })
    {
        if (widget)
        {
            widget->setVisible(isCustom);
        }
    }
    if (m_contentFields)
    {
        m_contentFields->setRowEnabled(Helper::name(Tool::PARAMETER_PRESERVE_ASPECT_RATIO), isScale);
        m_contentFields->setRowEnabled(Helper::name(Tool::PARAMETER_SCALE_ANNOTATIONS), isScale);
    }
    if (m_keepSizeNote)
    {
        m_keepSizeNote->setVisible(!isScale);
    }
    if (m_errorCallout)
    {
        m_errorCallout->setTexts(tr("Margins exceed the page size"), error);
        m_errorCallout->setVisible(!error.isEmpty() && !getTargetPages().empty());
    }

    // The names of the sizes before and after (391:23146 "Letter → A4")
    const qreal factor = page ? page->getMillimetersToUnits() : 1.0;
    const QSizeF nowMM = page ? PDFPageGeometry::getShownSizeMM(page) : QSizeF();
    const QSizeF afterMM = placement.sheetSize / factor;
    QString afterName = Helper::getOptionTitle(getModel(), Helper::name(Tool::PARAMETER_PAGE_SIZE), sizeId);
    if (isKeep)
    {
        afterName = Helper::getPaperName(afterMM);
    }
    const QString nowName = Helper::getPaperName(nowMM);
    const int percent = int(std::lround(qMin(placement.scaleX, placement.scaleY) * 100.0));
    const QString content = !placement.isValid ? QStringLiteral("–") : (isScale ? tr("Scaled to %1 %").arg(percent) : tr("Not scaled"));

    if (m_summary && m_summary->getRowCount() == 4)
    {
        m_summary->setValue(0, PDFAppLocale::formatNumber(qint64(getTargetPages().size())));
        m_summary->setValue(1, tr("%1 → %2").arg(nowName.isEmpty() ? Helper::formatSize(nowMM) : nowName, afterName.isEmpty() ? Helper::formatSize(afterMM) : afterName));
        m_summary->setValue(2, content);
        m_summary->setValue(3, isCustom || isKeep ? QStringLiteral("–") : Helper::getOptionTitle(getModel(), Helper::name(Tool::PARAMETER_ORIENTATION), values.value(Helper::name(Tool::PARAMETER_ORIENTATION)).toString()));
    }

    // The canvas: Now and After (391:23146)
    PDFAppToolPageCanvas* canvas = getCanvas();
    if (!canvas || canvas->getStageCount() < 2)
    {
        return;
    }
    const QString documentId = appDocument ? appDocument->getId() : QString();
    canvas->setTitle(tr("%1 · before and after").arg(getPreviewPageTitle()));
    canvas->setStageTitle(0, tr("Now"));
    canvas->setStageTitle(1, tr("After"));
    canvas->getStage(0)->setPage(getShownDocument(), documentId, getPreviewPage());
    canvas->getStage(1)->setPage(getShownDocument(), documentId, getPreviewPage());
    canvas->setCaption(0, page ? getSizeName(nowMM) : QString());

    const QString afterTitle = afterName.isEmpty() ? Helper::formatSize(afterMM) : tr("%1 · %2").arg(afterName, Helper::formatSize(afterMM));
    if (placement.isValid)
    {
        canvas->getStage(1)->setSheet(placement.sheetSize, placement.contentRect);
        canvas->setCaption(1, isScale ? tr("%1 · content scaled to %2 %").arg(afterTitle).arg(percent) : tr("%1 · content not scaled").arg(afterTitle));
    }
    else if (!placement.sheetSize.isEmpty())
    {
        // 391:25272: the page on the sheet, the margins red
        const QSizeF pageSize = canvas->getStage(1)->getPageSize();
        const qreal scale = pageSize.isEmpty() ? 1.0 : qMin(placement.sheetSize.width() / pageSize.width(), placement.sheetSize.height() / pageSize.height());
        const QSizeF fittedSize = pageSize * scale;
        canvas->getStage(1)->setSheet(placement.sheetSize, QRectF(QPointF((placement.sheetSize.width() - fittedSize.width()) * 0.5, (placement.sheetSize.height() - fittedSize.height()) * 0.5), fittedSize));
        canvas->setCaption(1, tr("%1 · the margins leave no room for the content").arg(afterName.isEmpty() ? Helper::formatSize(afterMM) : afterName), true);
    }

    // 391:23146: Now and After at one scale, so that the sizes can be compared (Letter 216 mm
    // is drawn wider than A4 210 mm, 299 × 387 and 291 × 411 in the frame)
    const QSizeF nowSize = canvas->getStage(0)->getPageSize();
    const QSizeF afterSheet = canvas->getStage(1)->getSheetSize();
    const QSizeF afterSize = afterSheet.isEmpty() ? canvas->getStage(1)->getPageSize() : afterSheet;
    const QSizeF extent(qMax(nowSize.width(), afterSize.width()), qMax(nowSize.height(), afterSize.height()));
    canvas->getStage(0)->setFitExtent(extent);
    canvas->getStage(1)->setFitExtent(extent);

    if (m_marginsOverlay)
    {
        PDFAppPageBoxesOverlay::Outline outline;
        outline.rect = placement.innerRect.normalized();
        outline.color = placement.isValid ? PDFThemeColors::BgAccent : PDFThemeColors::AccentRedIcon;
        outline.isDashed = true;
        m_marginsOverlay->setOutlines({ outline });
    }
}

std::vector<PDFSecurityHandler::Permission> PDFAppResizeToolGUI::getRunPermissions() const
{
    // R4, Table 22: a run, which moves or scales an annotation, needs bit 6
    const PDFDocumentPointer document = getShownDocument();
    QString error;
    const std::optional<std::vector<PDFPageGeometryChange>> changes = document ? PDFDocumentToolResizePages::createChanges(document.data(), getValues(), &error) : std::nullopt;
    if (changes && PDFPageGeometry::isAnnotationChanged(document.data(), *changes))
    {
        return { PDFSecurityHandler::Permission::ModifyInteractiveItems };
    }
    return { };
}

QString PDFAppResizeToolGUI::getConfirmText() const
{
    // 391:23146 "Resize 48 Pages"
    const int count = int(getTargetPages().size());
    return count > 0 ? tr("Resize %Ln Pages", nullptr, count) : tr("Resize Pages");
}

QString PDFAppResizeToolGUI::getStatusText() const
{
    using Helper = PDFAppToolGUIGeometryHelper;
    using Tool = PDFDocumentToolResizePages;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const PDFPage* page = getPage(getPreviewPage());
    const int count = int(getTargetPages().size());
    if (!page || count == 0)
    {
        return BaseClass::getStatusText();
    }
    if (!getValuesError().isEmpty())
    {
        // 391:25272
        return tr("%1 · the margins are wider than the page").arg(title);
    }

    const PDFPageGeometry::ResizePlacement placement = getPreviewPlacement();
    const QSizeF nowMM = PDFPageGeometry::getShownSizeMM(page);
    const QSizeF afterMM = placement.sheetSize / page->getMillimetersToUnits();
    const QString nowName = Helper::getPaperName(nowMM);
    QString afterName = Helper::getPaperName(afterMM);
    if (getPageSizeId() == QLatin1String(Tool::SIZE_CUSTOM) || afterName.isEmpty())
    {
        afterName = Helper::formatSize(afterMM);
    }
    const QString sizes = tr("%1 → %2").arg(nowName.isEmpty() ? Helper::formatSize(nowMM) : nowName, afterName);
    if (!isScaleToFit())
    {
        // 391:24594 "Resize Pages · Letter → 200 × 280 mm · content keeps its size · 48 pages"
        return tr("%1 · %2 · content keeps its size · %Ln pages", nullptr, count).arg(title, sizes);
    }

    // 391:23146 "Resize Pages · Letter → A4 · content scaled to fit inside 15 mm margins · 48 pages"
    const QVariantMap values = getValues();
    const double left = values.value(Helper::name(Tool::PARAMETER_MARGIN_LEFT)).toDouble();
    const bool isEqual = qFuzzyCompare(left + 1.0, values.value(Helper::name(Tool::PARAMETER_MARGIN_TOP)).toDouble() + 1.0) &&
                         qFuzzyCompare(left + 1.0, values.value(Helper::name(Tool::PARAMETER_MARGIN_RIGHT)).toDouble() + 1.0) &&
                         qFuzzyCompare(left + 1.0, values.value(Helper::name(Tool::PARAMETER_MARGIN_BOTTOM)).toDouble() + 1.0);
    if (isEqual)
    {
        return tr("%1 · %2 · content scaled to fit inside %3 mm margins · %Ln pages", nullptr, count).arg(title, sizes, PDFAppBlankToolGUI::formatMillimeters(left));
    }
    return tr("%1 · %2 · content scaled to fit inside the margins · %Ln pages", nullptr, count).arg(title, sizes);
}

void PDFAppResizeToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 391:25948 "Pages resized"
    input->title = tr("Pages resized");
}

// ----------------------------------------------------------------- PDFAppBoxesToolGUI

PDFAppBoxesToolGUI::PDFAppBoxesToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppBoxesToolGUI::~PDFAppBoxesToolGUI() = default;

QString PDFAppBoxesToolGUI::getSelectionLabel() const
{
    // 391:26688 "Pages"
    return tr("Pages");
}

PDFThemeColors::Color PDFAppBoxesToolGUI::getBoxColor(PDFPageGeometry::Box box)
{
    // 391:26688: media slate, crop blue, bleed red, trim green, art violet
    switch (box)
    {
        case PDFPageGeometry::Box::MediaBox:
            return PDFThemeColors::AccentNeutralIcon;
        case PDFPageGeometry::Box::CropBox:
            return PDFThemeColors::AccentBlueIcon;
        case PDFPageGeometry::Box::BleedBox:
            return PDFThemeColors::AccentRedIcon;
        case PDFPageGeometry::Box::TrimBox:
            return PDFThemeColors::AccentGreenIcon;
        case PDFPageGeometry::Box::ArtBox:
            return PDFThemeColors::AccentVioletIcon;
    }
    return PDFThemeColors::BgAccent;
}

std::vector<PDFPageGeometry::Box> PDFAppBoxesToolGUI::getWriteTo() const
{
    const PDFAppToolParameterModel* model = getModel();
    QString error;
    const std::optional<std::vector<PDFPageGeometry::Box>> boxes = model ? PDFDocumentToolSetPageBoxes::parseWriteTo(model->getValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolSetPageBoxes::PARAMETER_WRITE_TO)).toString(), &error) : std::nullopt;
    return boxes.value_or(std::vector<PDFPageGeometry::Box>());
}

void PDFAppBoxesToolGUI::toggleWriteTo(PDFPageGeometry::Box box)
{
    PDFAppToolParameterModel* model = getModel();
    if (!model || isDeciding())
    {
        return;
    }

    std::vector<PDFPageGeometry::Box> boxes = getWriteTo();
    const auto it = std::find(boxes.begin(), boxes.end(), box);
    if (it != boxes.end())
    {
        boxes.erase(it);
    }
    else
    {
        boxes.push_back(box);
    }

    // One undo step; the model announces it (the last call)
    model->setValue(PDFAppToolGUIGeometryHelper::name(PDFDocumentToolSetPageBoxes::PARAMETER_WRITE_TO), PDFDocumentToolSetPageBoxes::createWriteTo(boxes));
}

void PDFAppBoxesToolGUI::setBoxesShown(bool isShown)
{
    m_isBoxesShown = isShown;
    updateTool();
}

QToolButton* PDFAppBoxesToolGUI::getWriteToButton() const
{
    return m_writeToPanel ? m_writeToPanel->getButton() : nullptr;
}

PDFTileRow* PDFAppBoxesToolGUI::getWriteToRow(PDFPageGeometry::Box box) const
{
    auto it = m_writeToRows.find(box);
    return it != m_writeToRows.cend() ? it->second.data() : nullptr;
}

void PDFAppBoxesToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;

    // tool-actions 391:26688 with the menus 391:27131: the rows of both menus have the swatches of
    // the colors of their boxes
    if (PDFAppToolEnumMenu* basedOn = builder.addEnumMenu(Helper::name(PDFDocumentToolSetPageBoxes::PARAMETER_BASED_ON), QStringLiteral("box-edit"), tr("Based on: %1"), Helper::MENU_WIDTH))
    {
        for (const PDFPageGeometry::Box box : PDFPageGeometry::getAllBoxes())
        {
            if (PDFTileRow* row = basedOn->getRow(PDFPageGeometry::getBoxName(box)))
            {
                new PDFAppToolBoxSwatch(row, getBoxColor(box));
            }
        }
    }

    // K19: Write to is a multi-select list, the rows stay open
    m_writeToPanel = builder.addMenu(QStringLiteral("writeTo"), QStringLiteral("boxes-write"), QString(), Helper::MENU_WIDTH);
    m_writeToPanel->addCaption(tr("Write the new box to"));
    m_writeToRows.clear();
    static constexpr const char* descriptions[] =
    {
        QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The whole sheet"),
        QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "What viewers show and print"),
        QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "Trim plus the bleed area"),
        QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The finished page after cutting"),
        QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The meaningful content"),
    };
    for (const PDFPageGeometry::Box box : PDFPageGeometry::getAllBoxes())
    {
        PDFTileRow* row = m_writeToPanel->addRow(PDFDocumentToolSetPageBoxes::getBoxTitle(box), tr(descriptions[int(box)]), QString(), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("toolWriteTo-") + PDFPageGeometry::getBoxName(box));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
        row->setProperty("pageBox", int(box));
        new PDFAppToolBoxSwatch(row, getBoxColor(box));
        connect(row, &PDFTileRow::clicked, this, &PDFAppBoxesToolGUI::onWriteToRowClicked);
        m_writeToRows[box] = row;
    }

    // The toggle of the boxes in the preview (36, Active by default; K52)
    m_showBoxesButton = builder.addAction(QString(), QStringLiteral("boxes-show"), this, &PDFAppBoxesToolGUI::onShowBoxesClicked);
    m_showBoxesButton->setCheckable(true);
    m_showBoxesButton->setToolTip(tr("Show page boxes"));
    m_showBoxesButton->setAccessibleName(tr("Show page boxes"));
}

void PDFAppBoxesToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIGeometryHelper;
    using Tool = PDFDocumentToolSetPageBoxes;

    m_errorCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), tr("No box is selected"), tr("Choose at least one box in Write to – otherwise nothing would change."));
    m_errorCallout->setObjectName(QStringLiteral("toolBoxesErrorCallout"));
    m_warningCallout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString());
    m_warningCallout->setObjectName(QStringLiteral("toolBoxesWarningCallout"));

    m_marginsHeader = builder.addSection(QString());
    m_marginFields = builder.addMarginFields({ Helper::name(Tool::PARAMETER_MARGIN_LEFT), Helper::name(Tool::PARAMETER_MARGIN_TOP),
                                               Helper::name(Tool::PARAMETER_MARGIN_RIGHT), Helper::name(Tool::PARAMETER_MARGIN_BOTTOM) }, QStringLiteral(" "));
    builder.endSection();

    m_boxesHeader = builder.addSection(QString());
    m_boxes = builder.addKeyValues();
    m_boxes->setRowGap(PDFAppToolGUIGeometryHelper::KEY_VALUE_ROW_GAP);
    builder.endSection();

    PDFAppToolCallout* callout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The content is not touched"),
                                                    tr("Only the page boxes are written. To change the paper size or scale the content, use Resize Pages."));
    callout->setObjectName(QStringLiteral("toolBoxesContentCallout"));
    updateAll();
}

void PDFAppBoxesToolGUI::onCanvasCreated()
{
    PDFAppToolPageCanvas* canvas = getCanvas();
    PDFAppToolPageStage* stage = canvas ? canvas->getStage(0) : nullptr;
    if (!stage)
    {
        return;
    }

    // The whole sheet with the five boxes and their legend below (391:26688)
    stage->setShownBox(PDFPageGeometry::Box::MediaBox);
    m_overlay = new PDFAppPageBoxesOverlay(stage, PDFAppPageBoxesOverlay::Coordinates::Page);
    m_legend = new PDFAppPageBoxesLegend(canvas);
    canvas->setBottomWidget(m_legend);
}

void PDFAppBoxesToolGUI::updateTool()
{
    using Helper = PDFAppToolGUIGeometryHelper;
    using Tool = PDFDocumentToolSetPageBoxes;

    const QVariantMap values = getValues();
    const std::vector<PDFPageGeometry::Box> writeTo = getWriteTo();
    const std::optional<PDFPageGeometry::Box> basedOn = PDFPageGeometry::getBoxByName(values.value(Helper::name(Tool::PARAMETER_BASED_ON)).toString());
    const QString basedOnTitle = Tool::getBoxTitle(basedOn.value_or(PDFPageGeometry::Box::TrimBox)).toLower();
    const PDFDocumentPointer document = getShownDocument();
    const PDFPage* page = getPage(getPreviewPage());
    bool isOutsideMedia = false;
    const QRectF newBox = page ? Tool::computeNewBox(page, values, &isOutsideMedia) : QRectF();
    const PDFAppDocument* appDocument = getDocument();
    const bool hasTargets = !getTargetPages().empty();

    // The toolbar: "Write to: Bleed, Trim", "Write to: nothing" (391:28085)
    QStringList written;
    for (const PDFPageGeometry::Box box : writeTo)
    {
        written << Tool::getBoxShortTitle(box);
    }
    if (QToolButton* button = getWriteToButton())
    {
        button->setText(written.isEmpty() ? tr("Write to: nothing") : tr("Write to: %1").arg(written.join(QStringLiteral(", "))));
        button->setEnabled(!isDeciding());
    }
    for (const auto& [box, row] : m_writeToRows)
    {
        if (row)
        {
            const QSignalBlocker blocker(row);
            row->setChecked(std::find(writeTo.cbegin(), writeTo.cend(), box) != writeTo.cend());
        }
    }
    if (m_showBoxesButton)
    {
        const QSignalBlocker blocker(m_showBoxesButton);
        m_showBoxesButton->setChecked(m_isBoxesShown);
        m_showBoxesButton->setEnabled(!isDeciding());
    }

    // The right panel
    if (m_errorCallout)
    {
        m_errorCallout->setVisible(writeTo.empty() && hasTargets);
    }
    if (m_warningCallout)
    {
        // 391:27638 "The bleed box is larger than the media box"
        const PDFReal largest = page ? Tool::getLargestMarginInsideMedia(page, values) : -1.0;
        const QString boxTitle = writeTo.empty() ? QString() : Tool::getBoxTitle(writeTo.back() == PDFPageGeometry::Box::MediaBox && writeTo.size() > 1 ? writeTo.front() : writeTo.back()).toLower();
        const QString text = largest >= 0.0 ? tr("No box may reach outside the media box – viewers and printers clip it. Add Media box to Write to, or reduce the margins to %1 mm or less.").arg(PDFAppBlankToolGUI::formatMillimeters(largest))
                                            : tr("No box may reach outside the media box – viewers and printers clip it. Add Media box to Write to.");
        m_warningCallout->setTexts(tr("The %1 is larger than the media box").arg(boxTitle), text);
        m_warningCallout->setVisible(isOutsideMedia && getValuesError().isEmpty() && hasTargets);
    }
    if (m_marginsHeader)
    {
        // "New box = trim box +" (391:26688)
        m_marginsHeader->getTitleLabel()->setText(tr("New box = %1 +").arg(basedOnTitle));
    }
    if (m_marginFields && m_marginFields->getNoteLabel())
    {
        m_marginFields->getNoteLabel()->setText(tr("Positive values make the box larger than the %1, negative values smaller.").arg(basedOnTitle));
    }
    if (m_boxesHeader)
    {
        m_boxesHeader->getTitleLabel()->setText(tr("Boxes of %1").arg(getPreviewPageTitle().toLower()));
    }
    if (m_boxes && page && document)
    {
        // Media, Crop, Bleed, Trim, Art: the size, "not set", "→ 216 × 303 mm" of a box written
        m_boxes->clear();
        for (const PDFPageGeometry::Box box : PDFPageGeometry::getAllBoxes())
        {
            const bool isWritten = std::find(writeTo.cbegin(), writeTo.cend(), box) != writeTo.cend();
            QString value;
            if (isWritten && PDFPageGeometry::isValidBox(newBox))
            {
                value = tr("→ %1").arg(Helper::formatSize(PDFPage::getRotatedBox(newBox, page->getPageRotation()).size() / page->getMillimetersToUnits()));
            }
            else if (box == PDFPageGeometry::Box::MediaBox || box == PDFPageGeometry::Box::CropBox || PDFPageGeometry::isBoxDefined(page, &document->getStorage(), box))
            {
                value = Helper::formatSize(PDFPage::getRotatedBox(PDFPageGeometry::getEffectiveBox(page, box), page->getPageRotation()).size() / page->getMillimetersToUnits());
            }
            else
            {
                value = tr("not set");
            }
            m_boxes->addRow(Tool::getBoxShortTitle(box), value);
        }
    }

    // The canvas: the sheet with the boxes (391:26688)
    PDFAppToolPageCanvas* canvas = getCanvas();
    PDFAppToolPageStage* stage = canvas ? canvas->getStage(0) : nullptr;
    if (!canvas || !stage)
    {
        return;
    }
    canvas->setTitle(tr("%1 · page boxes").arg(getPreviewPageTitle()));
    stage->setPage(document, appDocument ? appDocument->getId() : QString(), getPreviewPage());

    std::vector<PDFAppPageBoxesOverlay::Outline> outlines;
    std::vector<PDFAppPageBoxesLegend::Item> legend;
    for (const PDFPageGeometry::Box box : PDFPageGeometry::getAllBoxes())
    {
        const bool isWritten = std::find(writeTo.cbegin(), writeTo.cend(), box) != writeTo.cend();
        legend.push_back({ Tool::getBoxTitle(box), getBoxColor(box), isWritten });
        if (!page || !document || !m_isBoxesShown)
        {
            continue;
        }

        const bool isDefined = box == PDFPageGeometry::Box::MediaBox || box == PDFPageGeometry::Box::CropBox || PDFPageGeometry::isBoxDefined(page, &document->getStorage(), box);
        QRectF rect;
        if (isWritten && PDFPageGeometry::isValidBox(newBox))
        {
            rect = newBox;
        }
        else if (isDefined)
        {
            rect = box == PDFPageGeometry::Box::MediaBox ? page->getMediaBox().normalized() : PDFPageGeometry::getEffectiveBox(page, box);
        }
        if (rect.isEmpty())
        {
            continue;
        }

        PDFAppPageBoxesOverlay::Outline outline;
        outline.rect = Helper::mapToShown(page, page->getMediaBox().normalized(), rect);
        outline.color = getBoxColor(box);
        outline.isDashed = box == PDFPageGeometry::Box::CropBox || box == PDFPageGeometry::Box::ArtBox;
        outline.isThick = isWritten;
        outlines.push_back(outline);
    }
    if (m_overlay)
    {
        m_overlay->setOutlines(std::move(outlines));
    }
    if (m_legend)
    {
        // The legend lies at the sheet (M11): the stage places it by its new height
        m_legend->setItems(std::move(legend));
        stage->updateTextGeometry();
    }

    QString caption;
    bool isError = false;
    if (writeTo.empty())
    {
        caption = tr("No box will be written – choose one in Write to");
    }
    else if (isOutsideMedia)
    {
        caption = tr("The new %1 reaches outside the media box").arg(Tool::getBoxTitle(writeTo.back()).toLower());
        isError = true;
    }
    else if (writeTo.size() == 1)
    {
        static constexpr const char* colors[] =
        {
            QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The thick gray line is the new media box"),
            QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The thick blue line is the new crop box"),
            QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The thick red line is the new bleed box"),
            QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The thick green line is the new trim box"),
            QT_TRANSLATE_NOOP("pdf::PDFAppBoxesToolGUI", "The thick violet line is the new art box"),
        };
        caption = tr(colors[int(writeTo.front())]);
    }
    else
    {
        caption = tr("The thick lines are the new boxes");
    }
    canvas->setCaption(0, caption, isError);
}

void PDFAppBoxesToolGUI::onWriteToRowClicked()
{
    // The panel stays open (K19)
    QObject* row = sender();
    if (row)
    {
        toggleWriteTo(PDFPageGeometry::Box(row->property("pageBox").toInt()));
    }
}

void PDFAppBoxesToolGUI::onShowBoxesClicked()
{
    setBoxesShown(!m_isBoxesShown);
}

QString PDFAppBoxesToolGUI::getConfirmText() const
{
    // 391:26688 "Set Boxes on 2 Pages"
    const int count = int(getTargetPages().size());
    return count > 0 ? tr("Set Boxes on %Ln Pages", nullptr, count) : tr("Set Boxes");
}

QString PDFAppBoxesToolGUI::getStatusText() const
{
    using Helper = PDFAppToolGUIGeometryHelper;
    using Tool = PDFDocumentToolSetPageBoxes;

    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const int count = int(getTargetPages().size());
    const std::vector<PDFPageGeometry::Box> writeTo = getWriteTo();
    if (count == 0)
    {
        return BaseClass::getStatusText();
    }
    if (writeTo.empty())
    {
        // 391:28085
        return tr("%1 · choose at least one box to write").arg(title);
    }

    // 391:26688 "Set Page Boxes · bleed box = trim box + 3 mm · 2 pages · the content is not changed"
    const QVariantMap values = getValues();
    const std::optional<PDFPageGeometry::Box> basedOn = PDFPageGeometry::getBoxByName(values.value(Helper::name(Tool::PARAMETER_BASED_ON)).toString());
    QStringList boxes;
    for (const PDFPageGeometry::Box box : writeTo)
    {
        boxes << Tool::getBoxTitle(box).toLower();
    }
    const QMarginsF margins = Tool::getMargins(values);
    const bool isEqual = qFuzzyCompare(margins.left() + 1.0, margins.top() + 1.0) && qFuzzyCompare(margins.left() + 1.0, margins.right() + 1.0) && qFuzzyCompare(margins.left() + 1.0, margins.bottom() + 1.0);
    const QString basedOnTitle = Tool::getBoxTitle(basedOn.value_or(PDFPageGeometry::Box::TrimBox)).toLower();
    const QString formula = isEqual ? tr("%1 = %2 + %3 mm").arg(boxes.join(QStringLiteral(", ")), basedOnTitle, PDFAppBlankToolGUI::formatMillimeters(margins.left()))
                                    : tr("%1 = %2 with the margins").arg(boxes.join(QStringLiteral(", ")), basedOnTitle);
    return tr("%1 · %2 · %Ln pages · the content is not changed", nullptr, count).arg(title, formula);
}

void PDFAppBoxesToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 391:28535 "Page boxes set"
    input->title = tr("Page boxes set");
}

}   // namespace pdf

#include "moc_pdfapptoolgui_geometry.cpp"
