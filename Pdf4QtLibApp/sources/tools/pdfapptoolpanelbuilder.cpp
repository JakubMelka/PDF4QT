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

#include "pdfapptoolpanelbuilder.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolgui.h"

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolparameter.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace pdf
{

/// Helpers of the builder
class PDFAppToolPanelBuilderHelper
{
public:
    PDFAppToolPanelBuilderHelper() = delete;

    /// Property of a row: its parameter or the id of its option
    static constexpr const char* ROW_KEY_PROPERTY = "pdf4qtToolRowKey";

    /// Property of a row of an Enum menu: its parameter
    static constexpr const char* ROW_PARAMETER_PROPERTY = "pdf4qtToolRowParameter";

    /// The filters of the core from the filters of the component
    static PDFDocumentToolPageSelection::Filters toToolFilters(PDFPageSelection::Filters filters)
    {
        PDFDocumentToolPageSelection::Filters result;
        result.setFlag(PDFDocumentToolPageSelection::Filter::Even, filters.testFlag(PDFPageSelection::Filter::Even));
        result.setFlag(PDFDocumentToolPageSelection::Filter::Odd, filters.testFlag(PDFPageSelection::Filter::Odd));
        result.setFlag(PDFDocumentToolPageSelection::Filter::Portrait, filters.testFlag(PDFPageSelection::Filter::Portrait));
        result.setFlag(PDFDocumentToolPageSelection::Filter::Landscape, filters.testFlag(PDFPageSelection::Filter::Landscape));
        return result;
    }

    static PDFPageSelection::Filters toSelectionFilters(PDFDocumentToolPageSelection::Filters filters)
    {
        PDFPageSelection::Filters result;
        result.setFlag(PDFPageSelection::Filter::Even, filters.testFlag(PDFDocumentToolPageSelection::Filter::Even));
        result.setFlag(PDFPageSelection::Filter::Odd, filters.testFlag(PDFDocumentToolPageSelection::Filter::Odd));
        result.setFlag(PDFPageSelection::Filter::Portrait, filters.testFlag(PDFDocumentToolPageSelection::Filter::Portrait));
        result.setFlag(PDFPageSelection::Filter::Landscape, filters.testFlag(PDFDocumentToolPageSelection::Filter::Landscape));
        return result;
    }
};

// --------------------------------------------------------------- PDFAppToolEnumMenu

PDFAppToolEnumMenu::PDFAppToolEnumMenu(PDFAppToolParameterModel* model, const QString& parameter, const QString& titleFormat, QToolButton* button, PDFAppToolPanelPopup* panel, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_titleFormat(titleFormat),
    m_button(button),
    m_panel(panel)
{
    addRows(parameter);

    if (model)
    {
        connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolEnumMenu::onParameterChanged);
    }
    refresh();
}

PDFAppToolEnumMenu::~PDFAppToolEnumMenu() = default;

void PDFAppToolEnumMenu::addGroup(const QString& parameter, const QString& caption)
{
    if (m_panel)
    {
        m_panel->addGroupDivider();
        if (!caption.isEmpty())
        {
            m_panel->addGroupCaption(caption);
        }
    }
    addRows(parameter);
    refresh();
}

void PDFAppToolEnumMenu::addRows(const QString& parameter)
{
    m_parameters << parameter;

    const PDFDocumentToolParameterDefinition* definition = m_model ? PDFDocumentToolParameters::find(m_model->getDefinitions(), parameter) : nullptr;
    if (!definition || !m_panel)
    {
        return;
    }

    // No button group: the arrows of a panel move the focus and switch the panels, the
    // model holds the choice (refresh); the role of a row is a radio button by its behavior
    for (const PDFDocumentToolParameterOption& option : definition->getOptions())
    {
        PDFTileRow* row = m_panel->addRow(option.getTitle(), option.getDescription(), option.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("toolOption-") + option.id);
        row->setProperty(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY, option.id);
        row->setProperty(PDFAppToolPanelBuilderHelper::ROW_PARAMETER_PROPERTY, parameter);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolEnumMenu::onRowClicked);
        m_rows[option.id] = row;
    }
}

PDFTileRow* PDFAppToolEnumMenu::getRow(const QString& optionId) const
{
    auto it = m_rows.find(optionId);
    return it != m_rows.end() ? it->second.data() : nullptr;
}

void PDFAppToolEnumMenu::refresh()
{
    if (!m_model)
    {
        return;
    }

    QString title;
    for (const QString& parameter : m_parameters)
    {
        const QString value = m_model->getValue(parameter).toString();
        if (const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(m_model->getDefinitions(), parameter))
        {
            for (const PDFDocumentToolParameterOption& option : definition->getOptions())
            {
                const bool isChosen = option.id == value;
                if (isChosen && parameter == m_parameters.front())
                {
                    title = option.getTitle();
                }
                if (PDFTileRow* row = getRow(option.id))
                {
                    const QSignalBlocker blocker(row);
                    row->setChecked(isChosen);
                }
            }
        }
    }

    if (m_button && !m_titleFormat.isEmpty())
    {
        m_button->setText(m_titleFormat.arg(title));
    }
}

void PDFAppToolEnumMenu::onRowClicked()
{
    QObject* row = sender();
    if (!row || !m_model)
    {
        return;
    }

    const QString parameter = row->property(PDFAppToolPanelBuilderHelper::ROW_PARAMETER_PROPERTY).toString();
    const QVariant value = row->property(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY);

    // The panel closes first: a change of a parameter may start anything
    QPointer<PDFAppToolEnumMenu> guard(this);
    if (m_panel && m_parameters.size() == 1)
    {
        m_panel->close();
    }
    if (guard && m_model)
    {
        m_model->setValue(parameter, value);
    }
    if (guard)
    {
        // The row has checked itself: the value of the model decides (also for a refusal)
        refresh();
    }
}

void PDFAppToolEnumMenu::onParameterChanged(const QString& name)
{
    if (m_parameters.contains(name))
    {
        refresh();
    }
}

// -------------------------------------------------------------- PDFAppToolFlagsMenu

PDFAppToolFlagsMenu::PDFAppToolFlagsMenu(PDFAppToolParameterModel* model, std::vector<PDFAppToolFlagItem> items, const QString& titleFormat, QToolButton* button, PDFAppToolPanelPopup* panel, PDFTileRow::Kind rowKind, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_items(std::move(items)),
    m_titleFormat(titleFormat),
    m_button(button),
    m_panel(panel)
{
    if (panel)
    {
        for (const PDFAppToolFlagItem& item : m_items)
        {
            if (!item.groupCaption.isEmpty())
            {
                panel->addGroupCaption(item.groupCaption);
            }

            PDFTileRow* row = panel->addRow(item.title, item.description, item.iconName, rowKind);
            row->setObjectName(QStringLiteral("toolFlag-") + item.parameter);
            row->setProperty(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY, item.parameter);
            row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Toggle);
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolFlagsMenu::onRowClicked);
            m_rows[item.parameter] = row;
        }
    }

    if (model)
    {
        connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolFlagsMenu::onParameterChanged);
    }
    refresh();
}

PDFAppToolFlagsMenu::~PDFAppToolFlagsMenu() = default;

int PDFAppToolFlagsMenu::getCheckedCount() const
{
    int count = 0;
    for (const PDFAppToolFlagItem& item : m_items)
    {
        if (m_model && m_model->getValue(item.parameter).toBool())
        {
            ++count;
        }
    }
    return count;
}

void PDFAppToolFlagsMenu::setAll(bool isChecked)
{
    if (!m_model)
    {
        return;
    }

    QVariantMap values;
    for (const PDFAppToolFlagItem& item : m_items)
    {
        values.insert(item.parameter, isChecked);
    }
    m_model->setValues(values);
}

void PDFAppToolFlagsMenu::restoreDefaults()
{
    if (!m_model)
    {
        return;
    }

    QVariantMap values;
    for (const PDFAppToolFlagItem& item : m_items)
    {
        if (const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(m_model->getDefinitions(), item.parameter))
        {
            values.insert(item.parameter, definition->getDefaultValue());
        }
    }
    m_model->setValues(values);
}

void PDFAppToolFlagsMenu::refresh()
{
    if (!m_model)
    {
        return;
    }

    for (const auto& item : m_rows)
    {
        if (item.second)
        {
            const QSignalBlocker blocker(item.second);
            item.second->setChecked(m_model->getValue(item.first).toBool());
        }
    }

    if (m_button && !m_titleFormat.isEmpty())
    {
        // "Steps: %1 of %2", "Include: %1 extra" (no second argument: Qt warns about it)
        const QString count = QString::number(getCheckedCount());
        m_button->setText(m_titleFormat.contains(QLatin1String("%2")) ? m_titleFormat.arg(count, QString::number(m_items.size())) : m_titleFormat.arg(count));
    }
}

void PDFAppToolFlagsMenu::onRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (row && m_model)
    {
        QPointer<PDFAppToolFlagsMenu> guard(this);
        m_model->setValue(row->property(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY).toString(), row->isChecked());
        if (guard)
        {
            // The row has toggled itself: the value of the model decides (also for a refusal)
            refresh();
        }
    }
}

void PDFAppToolFlagsMenu::onParameterChanged(const QString& name)
{
    if (m_rows.count(name))
    {
        refresh();
    }
}

// --------------------------------------------------------------- PDFAppToolStepList

PDFAppToolStepList::PDFAppToolStepList(PDFAppToolParameterModel* model, std::vector<PDFAppToolFlagItem> items, QWidget* parent) :
    BaseClass(parent),
    m_model(model),
    m_items(std::move(items))
{
    setObjectName(QStringLiteral("toolSteps"));

    // steps 319:7343: rows 273 wide at 7 from the border, 2 px apart
    getBodyLayout()->setContentsMargins(7, 7, 7, 7);
    getBodyLayout()->setSpacing(2);
    for (const PDFAppToolFlagItem& item : m_items)
    {
        PDFTileRow* row = new PDFTileRow(item.title, item.description, item.iconName, PDFTileRow::Kind::Step, this);
        row->setObjectName(QStringLiteral("toolStep-") + item.parameter);
        row->setClickable(false);
        getBodyLayout()->addWidget(row);
        m_rows[item.parameter] = row;
    }

    if (model)
    {
        connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolStepList::onParameterChanged);
    }
    refresh();
}

PDFAppToolStepList::~PDFAppToolStepList() = default;

PDFTileRow* PDFAppToolStepList::getRow(const QString& parameter) const
{
    auto it = m_rows.find(parameter);
    return it != m_rows.end() ? it->second.data() : nullptr;
}

void PDFAppToolStepList::setResults(std::vector<PDFAppToolStepResult> results)
{
    m_results = std::move(results);
    refresh();
}

void PDFAppToolStepList::refresh()
{
    if (!m_results.empty())
    {
        // 319:7398: what the run did
        for (const PDFAppToolFlagItem& item : m_items)
        {
            PDFTileRow* row = getRow(item.parameter);
            if (!row)
            {
                continue;
            }

            const PDFAppToolStepResult* result = nullptr;
            for (const PDFAppToolStepResult& candidate : m_results)
            {
                result = !result && candidate.parameter == item.parameter ? &candidate : result;
            }
            const bool isRun = result && result->isRun;
            row->setTone(isRun ? PDFTileRow::Tone::Accent : PDFTileRow::Tone::Muted);
            row->setEndIcon(isRun ? QStringLiteral("check-circle") : QStringLiteral("minus"), isRun ? PDFThemeColors::AccentGreenIcon : PDFThemeColors::IconMuted);
            row->setDescription(result ? result->text : item.offDescription);
            if (isRun)
            {
                row->setDescriptionColor(PDFThemeColors::TextSecondary);
            }
            else
            {
                row->resetDescriptionColor();
            }
        }
        return;
    }

    if (!m_model)
    {
        return;
    }

    for (const PDFAppToolFlagItem& item : m_items)
    {
        PDFTileRow* row = getRow(item.parameter);
        if (!row)
        {
            continue;
        }

        const bool isOn = m_model->getValue(item.parameter).toBool();
        row->setTone(isOn ? PDFTileRow::Tone::Accent : PDFTileRow::Tone::Muted);
        row->setEndIcon(isOn ? QStringLiteral("check") : QStringLiteral("minus"), isOn ? PDFThemeColors::IconAccent : PDFThemeColors::IconMuted);
        row->setDescription(!isOn && !item.offDescription.isEmpty() ? item.offDescription : item.description);
        row->resetDescriptionColor();
    }
}

void PDFAppToolStepList::onParameterChanged(const QString& name)
{
    if (m_rows.count(name))
    {
        refresh();
    }
}

// -------------------------------------------------------------- PDFAppToolChecklist

PDFAppToolChecklist::PDFAppToolChecklist(PDFAppToolParameterModel* model, std::vector<PDFAppToolFlagItem> items, QWidget* parent) :
    BaseClass(parent),
    m_model(model),
    m_items(std::move(items))
{
    setObjectName(QStringLiteral("toolChecklist"));

    // 438:12713: padding 8 + the border, the rows 2 apart, a caption of the card 21 high
    getBodyLayout()->setContentsMargins(9, 9, 9, 9);
    getBodyLayout()->setSpacing(2);
    for (const PDFAppToolFlagItem& item : m_items)
    {
        if (!item.groupCaption.isEmpty())
        {
            PDFTileRow* caption = new PDFTileRow(item.groupCaption, QString(), QString(), PDFTileRow::Kind::Caption, this);
            caption->setObjectName(QStringLiteral("toolCheckCaption"));
            caption->setClickable(false);
            getBodyLayout()->addWidget(caption);
        }

        PDFTileRow* row = new PDFTileRow(item.title, item.description, item.iconName, PDFTileRow::Kind::ListItem, this);
        row->setObjectName(QStringLiteral("toolCheck-") + item.parameter);
        row->setProperty(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY, item.parameter);
        row->setCheckBoxVisible(true);
        connect(row, &PDFTileRow::checkBoxToggled, this, &PDFAppToolChecklist::onCheckBoxToggled);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolChecklist::onRowClicked);
        getBodyLayout()->addWidget(row);
        m_rows[item.parameter] = row;
    }

    if (model)
    {
        connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolChecklist::onParameterChanged);
    }
    refresh();
}

PDFAppToolChecklist::~PDFAppToolChecklist() = default;

PDFTileRow* PDFAppToolChecklist::getRow(const QString& parameter) const
{
    auto it = m_rows.find(parameter);
    return it != m_rows.end() ? it->second.data() : nullptr;
}

void PDFAppToolChecklist::setRowAvailable(const QString& parameter, bool isAvailable)
{
    if (PDFTileRow* row = getRow(parameter))
    {
        row->setCheckBoxEnabled(isAvailable);
        refresh();
    }
}

void PDFAppToolChecklist::refresh()
{
    if (!m_model)
    {
        return;
    }

    for (const auto& item : m_rows)
    {
        if (item.second)
        {
            const QSignalBlocker blocker(item.second);
            // A kind, which is not there, is shown unticked (438:12588 "Not found"); its value stays
            item.second->setCheckBoxChecked(m_model->getValue(item.first).toBool() && item.second->isCheckBoxEnabled());
            item.second->update();
        }
    }
}

void PDFAppToolChecklist::onCheckBoxToggled(bool isChecked)
{
    QObject* row = sender();
    if (row && m_model)
    {
        m_model->setValue(row->property(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY).toString(), isChecked);
    }
}

void PDFAppToolChecklist::onRowClicked()
{
    PDFTileRow* row = qobject_cast<PDFTileRow*>(sender());
    if (row && row->isCheckBoxEnabled() && m_model)
    {
        m_model->setValue(row->property(PDFAppToolPanelBuilderHelper::ROW_KEY_PROPERTY).toString(), !row->isCheckBoxChecked());
    }
}

void PDFAppToolChecklist::onParameterChanged(const QString& name)
{
    if (m_rows.count(name))
    {
        refresh();
    }
}

// ---------------------------------------------------- PDFAppToolPageSelectionBinder

PDFAppToolPageSelectionBinder::PDFAppToolPageSelectionBinder(PDFDocumentToolGUI* gui, const QString& parameter, PDFPageSelection* selection, QObject* parent) :
    BaseClass(parent),
    m_gui(gui),
    m_parameter(parameter),
    m_selection(selection)
{
    updatePages();

    PDFAppToolParameterModel* model = gui ? gui->getModel() : nullptr;
    if (model)
    {
        connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolPageSelectionBinder::onParameterChanged);
        onParameterChanged(parameter);
    }

    // More pages selected in the thumbnails: the tool works on them (tools.md 9.6)
    if (selection && selection->getMode() == PDFPageSelection::Mode::All && selection->getRange().isEmpty() && selection->getSelectedPages().size() > 1)
    {
        selection->setMode(PDFPageSelection::Mode::Selected);
    }

    if (selection)
    {
        connect(selection, &PDFPageSelection::modeChanged, this, &PDFAppToolPageSelectionBinder::onUserChange);
        connect(selection, &PDFPageSelection::rangeChanged, this, &PDFAppToolPageSelectionBinder::onUserChange);
        connect(selection, &PDFPageSelection::filtersChanged, this, &PDFAppToolPageSelectionBinder::onUserChange);
    }
    if (gui && gui->getHost())
    {
        connect(gui->getHost(), &PDFAppToolHost::pagesChanged, this, &PDFAppToolPageSelectionBinder::updateViewPages);
    }
    if (PDFAppDocument* document = gui ? gui->getDocument() : nullptr)
    {
        // An assistant may change the pages below an open tool
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppToolPageSelectionBinder::onDocumentChanged);
    }
    writeValue(false);
}

PDFAppToolPageSelectionBinder::~PDFAppToolPageSelectionBinder()
{
    if (m_gui)
    {
        m_gui->setInputError(m_parameter, QString());
    }
}

QString PDFAppToolPageSelectionBinder::createRangeText(const QList<int>& pages)
{
    std::vector<PDFInteger> pageIndices;
    pageIndices.reserve(size_t(pages.size()));
    for (const int page : pages)
    {
        pageIndices.push_back(PDFInteger(page) - 1);
    }
    return PDFDocumentToolPageSelection::createRangeText(pageIndices);
}

void PDFAppToolPageSelectionBinder::updatePages()
{
    PDFAppDocument* document = m_gui ? m_gui->getDocument() : nullptr;
    if (!m_selection || !document)
    {
        return;
    }

    const PDFDocumentPointer pdfDocument = document->getDocument();
    const int pageCount = int(document->getPageCount());
    m_selection->setPageCount(pageCount);

    // The labels and the sizes as the pages are shown: for the status and the orientation filters
    if (pdfDocument)
    {
        const PDFCatalog* catalog = pdfDocument->getCatalog();
        QList<QSizeF> sizes;
        sizes.reserve(pageCount);
        for (int i = 0; i < pageCount; ++i)
        {
            const PDFPage* page = catalog->getPage(i);
            // The visible box, as PDFDocumentToolPageSelection decides the orientation of a page
            sizes.append(page ? page->getRotatedCropBox().size() : QSizeF());
        }
        m_selection->setPageSizes(sizes);
    }

    const PDFAppToolHost* host = m_gui->getHost();
    const PDFAppDocumentView* view = host ? host->getView() : nullptr;
    QStringList labels;
    if (view && view->hasPageLabels())
    {
        labels.reserve(pageCount);
        for (int i = 0; i < pageCount; ++i)
        {
            labels.append(view->getPageLabel(i));
        }
    }
    m_selection->setPageLabels(labels);
    updateViewPages();
}

void PDFAppToolPageSelectionBinder::onDocumentChanged()
{
    // The signals of the selection reach slots of the tool
    QPointer<PDFAppToolPageSelectionBinder> guard(this);
    m_isUpdating = true;
    updatePages();
    if (!guard)
    {
        return;
    }
    m_isUpdating = false;

    // The pages of a range or of the modes of the view may be others now
    writeValue(false);
}

void PDFAppToolPageSelectionBinder::updateViewPages()
{
    const PDFAppToolHost* host = m_gui ? m_gui->getHost() : nullptr;
    if (!m_selection || !host)
    {
        return;
    }

    m_isUpdating = true;
    const PDFInteger currentPage = host->getCurrentPage();
    m_selection->setCurrentPage(currentPage >= 0 ? int(currentPage) + 1 : 0);

    if (!m_isOwnSelection)
    {
        QList<int> pages;
        for (const PDFInteger page : host->getSelectedPages())
        {
            pages.append(int(page) + 1);
        }
        std::sort(pages.begin(), pages.end());
        m_selection->setSelectedPages(pages);
    }
    m_isUpdating = false;

    // Only Current and Selected read the pages of the view
    if (m_selection->getMode() == PDFPageSelection::Mode::Current || m_selection->getMode() == PDFPageSelection::Mode::Selected)
    {
        writeValue(false);
    }
}

void PDFAppToolPageSelectionBinder::onUserChange()
{
    writeValue(true);
}

void PDFAppToolPageSelectionBinder::writeValue(bool isUserChange)
{
    if (m_isUpdating || !m_selection || !m_gui)
    {
        return;
    }

    // The error of the text of the range only: the error of the application shown by the
    // selection is the refusal of the last value by the model, which the next value replaces
    PDFAppToolParameterModel* model = m_gui->getModel();
    const QString rangeError = m_selection->getMode() == PDFPageSelection::Mode::Range ? m_selection->getRangeError() : QString();
    if (!rangeError.isEmpty())
    {
        m_gui->setInputError(m_parameter, rangeError);
        return;
    }

    QString pages;
    switch (m_selection->getMode())
    {
        case PDFPageSelection::Mode::All:
            break;
        case PDFPageSelection::Mode::Current:
            pages = QString::number(m_selection->getCurrentPage());
            break;
        case PDFPageSelection::Mode::Selected:
            pages = createRangeText(m_selection->getSelectedPages());
            break;
        case PDFPageSelection::Mode::Range:
            pages = m_selection->getRange();
            break;
    }

    PDFDocumentToolPageSelection value;
    value.pages = pages;
    value.filters = PDFAppToolPanelBuilderHelper::toToolFilters(m_selection->getFilters());

    // The mode Selected without a selected page selects no page, not all of them
    value.isNone = m_selection->getMode() == PDFPageSelection::Mode::Selected && m_selection->getSelectedPages().isEmpty();

    // A selection of more pages than a range text can hold is refused by the model
    m_isUpdating = true;
    QPointer<PDFAppToolPageSelectionBinder> guard(this);
    const QString error = !model ? QString() : (isUserChange ? model->setValue(m_parameter, value.toVariant()) : model->setFollowingValue(m_parameter, value.toVariant()));
    if (!guard)
    {
        return;
    }
    m_isUpdating = false;

    if (m_gui)
    {
        m_gui->setInputError(m_parameter, error);
    }
    if (m_selection)
    {
        m_selection->setCustomError(error);
    }
}

void PDFAppToolPageSelectionBinder::onParameterChanged(const QString& name)
{
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    if (name != m_parameter || m_isUpdating || !model || !m_selection)
    {
        return;
    }

    // Undo, Redo, the last values: the shown text of the mode stays, when it says the same
    QString errorMessage;
    const std::optional<PDFDocumentToolPageSelection> value = PDFDocumentToolPageSelection::fromVariant(model->getValue(m_parameter), &errorMessage);
    if (!value)
    {
        return;
    }

    m_isUpdating = true;
    m_selection->setFilters(PDFAppToolPanelBuilderHelper::toSelectionFilters(value->filters));
    if (value->isNone)
    {
        m_isOwnSelection = true;
        m_selection->setSelectedPages(QList<int>());
        m_selection->setMode(PDFPageSelection::Mode::Selected);
    }
    else if (value->pages.isEmpty())
    {
        m_selection->setMode(PDFPageSelection::Mode::All);
    }
    else if (m_selection->getMode() != PDFPageSelection::Mode::Range || m_selection->getRange() != value->pages)
    {
        const bool isCurrent = m_selection->getMode() == PDFPageSelection::Mode::Current && value->pages == QString::number(m_selection->getCurrentPage());
        const bool isSelected = m_selection->getMode() == PDFPageSelection::Mode::Selected && value->pages == createRangeText(m_selection->getSelectedPages());
        if (!isCurrent && !isSelected)
        {
            m_selection->setRange(value->pages);
            m_selection->setMode(PDFPageSelection::Mode::Range);
        }
    }
    m_isUpdating = false;

    // The shown value is the value of the model again: a range with an error (the Primary
    // button is disabled by it) is gone after Undo
    m_gui->setInputError(m_parameter, m_selection->getError());
}

void PDFAppToolPageSelectionBinder::setPages(const QList<int>& pages)
{
    if (!m_selection)
    {
        return;
    }

    // The signals of the selection reach slots of the tool
    QPointer<PDFAppToolPageSelectionBinder> guard(this);
    m_isUpdating = true;
    m_isOwnSelection = true;
    m_selection->setFilters(PDFPageSelection::Filters());
    const QString rangeText = createRangeText(pages);
    if (guard && m_selection && m_selection->getMode() == PDFPageSelection::Mode::Range && !pages.isEmpty() && rangeText.size() <= PDFDocumentToolPageSelection::MAXIMUM_PAGES_LENGTH)
    {
        m_selection->setRange(rangeText);
    }
    else if (guard && m_selection)
    {
        m_selection->setSelectedPages(pages);
        m_selection->setMode(PDFPageSelection::Mode::Selected);
    }
    if (!guard)
    {
        return;
    }
    m_isUpdating = false;
    writeValue(true);
}

void PDFAppToolPageSelectionBinder::setEntryMode(PDFPageSelection::Mode mode)
{
    if (!m_selection)
    {
        return;
    }

    QPointer<PDFAppToolPageSelectionBinder> guard(this);
    m_isUpdating = true;
    m_selection->setMode(mode);
    if (!guard)
    {
        return;
    }
    m_isUpdating = false;
    writeValue(false);
}

void PDFAppToolPageSelectionBinder::followPages(const PDFDocument* from, const std::vector<PDFInteger>& pageMap)
{
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    if (!m_selection || !model || !from)
    {
        return;
    }

    const PDFPageSelection::Mode mode = m_selection->getMode();
    const bool isRange = mode == PDFPageSelection::Mode::Range;
    if (!isRange && !(mode == PDFPageSelection::Mode::Selected && m_isOwnSelection))
    {
        return;
    }

    QString error;
    const std::optional<PDFDocumentToolPageSelection> value = PDFDocumentToolPageSelection::fromVariant(model->getValue(m_parameter), &error);
    if (!value)
    {
        return;
    }

    const PDFDocumentToolPageSelection mapped = value->mapPages(from, pageMap);
    if (mapped == *value)
    {
        return;
    }

    QList<int> pages;
    if (!mapped.isNone)
    {
        // The pages of the shown version: the highest index of the map is one of them
        const PDFInteger highestPage = pageMap.empty() ? -1 : *std::max_element(pageMap.cbegin(), pageMap.cend());
        const std::vector<QSizeF> pageSizes(size_t(qMax<PDFInteger>(m_selection->getPageCount(), highestPage + 1)));
        for (const PDFInteger page : mapped.getPageIndices(pageSizes, &error))
        {
            pages.append(int(page) + 1);
        }
    }

    // The signals of the selection reach slots of the tool
    QPointer<PDFAppToolPageSelectionBinder> guard(this);
    m_isUpdating = true;
    m_selection->setFilters(PDFPageSelection::Filters());
    if (guard && m_selection && isRange && !pages.isEmpty())
    {
        m_selection->setRange(mapped.pages);
    }
    else if (guard && m_selection)
    {
        m_isOwnSelection = true;
        m_selection->setSelectedPages(pages);
        m_selection->setMode(PDFPageSelection::Mode::Selected);
    }
    if (!guard)
    {
        return;
    }
    m_isUpdating = false;
    writeValue(false);
}

// ------------------------------------------------------------ PDFAppToolPendingList

PDFAppToolPendingList::PDFAppToolPendingList(const QString& title, QAbstractItemModel* model, QWidget* parent) :
    BaseClass(parent),
    m_model(model)
{
    setObjectName(QStringLiteral("toolPendingList"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ROW_GAP);

    // 389:20204: the header of 22 and the rows of 46, each 10 apart
    m_header = new PDFAppToolSectionHeader(title, QString(), this);
    m_header->setCountStyle();
    layout->addWidget(m_header);

    PDFTileRowDelegate* delegate = new PDFTileRowDelegate(this);
    delegate->setKind(PDFTileRowDelegate::Kind::Pending);
    delegate->setClickable(false);
    connect(delegate, &PDFTileRowDelegate::actionTriggered, this, &PDFAppToolPendingList::onDelegateAction);

    PDFAppPanelListView* view = new PDFAppPanelListView(this);
    m_view = view;
    m_view->setObjectName(QStringLiteral("toolPendingView"));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setItemDelegate(delegate);
    m_view->setModel(model);
    m_view->setSelectionMode(QAbstractItemView::NoSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->viewport()->setAutoFillBackground(false);

    // The spacing of QListView is on every side of a row: half of the gap, the viewport
    // gives the outer half back, so the rows have the whole width of the list (279)
    m_view->setSpacing(ROW_GAP / 2);
    view->setContentPaddings(QMargins(-ROW_GAP / 2, -ROW_GAP / 2, -ROW_GAP / 2, -ROW_GAP / 2));
    layout->addWidget(m_view, 1);

    if (model)
    {
        connect(model, &QAbstractItemModel::rowsInserted, this, &PDFAppToolPendingList::updateCount);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &PDFAppToolPendingList::updateCount);
        connect(model, &QAbstractItemModel::modelReset, this, &PDFAppToolPendingList::updateCount);
    }
    updateCount();
}

PDFAppToolPendingList::~PDFAppToolPendingList() = default;

void PDFAppToolPendingList::updateCount()
{
    if (m_header)
    {
        m_header->setHint(m_model ? QString::number(m_model->rowCount()) : QString());
    }
}

void PDFAppToolPendingList::onDelegateAction(const QModelIndex& index, int action)
{
    if (action == PDFTileRowDelegate::LinkAction)
    {
        Q_EMIT linkActivated(index);
    }
}

// ----------------------------------------------------------- PDFAppToolPanelBuilder

PDFAppToolPanelBuilder::PDFAppToolPanelBuilder(PDFDocumentToolGUI* gui, QWidget* widget, Qt::Orientation orientation, int spacing) :
    m_gui(gui),
    m_widget(widget)
{
    QBoxLayout* layout = orientation == Qt::Horizontal ? static_cast<QBoxLayout*>(new QHBoxLayout(widget)) : static_cast<QBoxLayout*>(new QVBoxLayout(widget));
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
    m_layout = layout;
}

QBoxLayout* PDFAppToolPanelBuilder::getCurrentLayout() const
{
    return m_sectionLayout ? m_sectionLayout : m_layout;
}

void PDFAppToolPanelBuilder::addWidget(QWidget* widget)
{
    if (widget)
    {
        getCurrentLayout()->addWidget(widget);
        ++m_itemCount;
    }
}

PDFAppSettingControl* PDFAppToolPanelBuilder::addSegments(const QString& parameter, const QString& label, int segmentWidth)
{
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    if (!model)
    {
        return nullptr;
    }

    std::optional<PDFSettingDefinition> definition = model->toSettingDefinition(parameter);
    if (!definition)
    {
        return nullptr;
    }
    definition->setControl(PDFSettingDefinition::Control::Segmented);

    if (!label.isEmpty())
    {
        // label 293:1313: Medium 12 text/tertiary, 6 px before the segments
        PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Medium, 12, PDFThemeColors::TextTertiary, m_widget);
        labelWidget->setObjectName(QStringLiteral("toolLabel-") + parameter);
        labelWidget->setProperty(PDFAppToolToolbar::NARROW_HIDDEN_PROPERTY, true);
        labelWidget->setContentsMargins(0, 0, LABEL_GAP - m_layout->spacing(), 0);
        addWidget(labelWidget);
    }

    PDFAppSettingControl* control = new PDFAppSettingControl(m_gui->getContext(), *definition, m_widget, model->createValueStore());
    QObject::connect(model, &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
    if (PDFSegmentedControl* segmented = control->getSegmentedControl())
    {
        segmented->setObjectName(QStringLiteral("toolSegments-") + parameter);
        segmented->setFixedSize(segmented->getCount() * segmentWidth, SEGMENT_HEIGHT);
        addWidget(segmented);
    }
    return control;
}

PDFAppSettingControl* PDFAppToolPanelBuilder::addValueControl(const QString& parameter)
{
    PDFAppToolParameterModel* model = m_gui ? m_gui->getModel() : nullptr;
    const std::optional<PDFSettingDefinition> definition = model ? model->toSettingDefinition(parameter) : std::nullopt;
    if (!definition)
    {
        return nullptr;
    }

    PDFAppSettingControl* control = new PDFAppSettingControl(m_gui->getContext(), *definition, m_widget, model->createValueStore());
    QObject::connect(model, &PDFAppToolParameterModel::changed, control, &PDFAppSettingControl::refresh);
    addWidget(control->getWidget());
    return control;
}

QToolButton* PDFAppToolPanelBuilder::createActionButton(const QString& text, const QString& iconName)
{
    QToolButton* button = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("toolAction-") + iconName, iconName, m_widget);
    button->setCheckable(false);
    button->setText(text);
    button->setToolTip(text);
    addWidget(button);
    return button;
}

QToolButton* PDFAppToolPanelBuilder::createMenuButton(const QString& iconName, const QString& text)
{
    QToolButton* button = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("toolMenu-") + iconName, iconName, m_widget);
    button->setText(text);
    button->setToolTip(text);
    addWidget(button);
    return button;
}

PDFAppToolEnumMenu* PDFAppToolPanelBuilder::addEnumMenu(const QString& parameter, const QString& iconName, const QString& titleFormat, int panelWidth, const QString& caption)
{
    if (!m_gui || !m_gui->getModel())
    {
        return nullptr;
    }

    PDFAppToolPanelPopup* panel = addMenu(parameter, iconName, QString(), panelWidth);
    if (!caption.isEmpty())
    {
        panel->addGroupCaption(caption);
    }
    return new PDFAppToolEnumMenu(m_gui->getModel(), parameter, titleFormat, panel->getButton(), panel, m_widget);
}

PDFAppToolFlagsMenu* PDFAppToolPanelBuilder::addFlagsMenu(std::vector<PDFAppToolFlagItem> items, const QString& iconName, const QString& titleFormat, int panelWidth, const QString& caption, PDFTileRow::Kind rowKind)
{
    if (!m_gui || !m_gui->getModel())
    {
        return nullptr;
    }

    PDFAppToolPanelPopup* panel = addMenu(iconName, iconName, QString(), panelWidth);
    if (!caption.isEmpty())
    {
        panel->addCaption(caption);
    }
    return new PDFAppToolFlagsMenu(m_gui->getModel(), std::move(items), titleFormat, panel->getButton(), panel, rowKind, m_widget);
}

PDFAppToolPanelPopup* PDFAppToolPanelBuilder::addMenu(const QString& objectName, const QString& iconName, const QString& text, int panelWidth)
{
    const PDFAppToolHost* host = m_gui ? m_gui->getHost() : nullptr;
    QToolButton* button = createMenuButton(iconName, text);
    PDFAppToolPanelPopup* panel = new PDFAppToolPanelPopup(host ? host->getView() : nullptr, panelWidth, button);
    panel->setObjectName(QStringLiteral("toolPanel-") + objectName);
    if (PDFAppToolToolbar* toolbar = m_gui ? m_gui->getToolbar() : nullptr)
    {
        toolbar->addPanel(button, panel);
    }
    return panel;
}

void PDFAppToolPanelBuilder::addActionDivider()
{
    addWidget(new PDFAppToolbarDivider(Qt::Vertical, 20, 6, PDFThemeColors::BorderSeparator, m_widget));
}

PDFAppToolSectionHeader* PDFAppToolPanelBuilder::addSection(const QString& title, const QString& hint)
{
    endSection();

    QVBoxLayout* sectionLayout = new QVBoxLayout();
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(SECTION_GAP);
    m_layout->addLayout(sectionLayout);
    m_sectionLayout = sectionLayout;

    PDFAppToolSectionHeader* header = new PDFAppToolSectionHeader(title, hint, m_widget);
    addWidget(header);
    return header;
}

void PDFAppToolPanelBuilder::endSection()
{
    m_sectionLayout = nullptr;
}

PDFAppToolMeasuredCard* PDFAppToolPanelBuilder::addMeasuredCard()
{
    PDFAppToolMeasuredCard* card = new PDFAppToolMeasuredCard(m_widget);
    addWidget(card);
    return card;
}

PDFKeyValueList* PDFAppToolPanelBuilder::addKeyValues()
{
    PDFAppToolCard* card = new PDFAppToolCard(m_widget);
    PDFKeyValueList* list = new PDFKeyValueList(card);
    card->getBodyLayout()->addWidget(list);
    addWidget(card);
    return list;
}

PDFAppParagraph* PDFAppToolPanelBuilder::addTextCard(const QString& text)
{
    // result 303:4509: padding 12 + the border
    PDFAppToolCard* card = new PDFAppToolCard(m_widget);
    card->getBodyLayout()->setContentsMargins(13, 13, 13, 13);
    PDFAppParagraph* paragraph = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, card);
    paragraph->setMinimumWidth(1);
    card->getBodyLayout()->addWidget(paragraph);
    addWidget(card);
    return paragraph;
}

PDFAppToolPanelHeader* PDFAppToolPanelBuilder::addPanelHeader(const QString& title, const QString& subtitle)
{
    PDFAppToolPanelHeader* header = new PDFAppToolPanelHeader(title, subtitle, m_widget);
    addWidget(header);
    return header;
}

PDFAppToolCallout* PDFAppToolPanelBuilder::addCallout(PDFAccentHue hue, const QString& iconName, const QString& title, const QString& text)
{
    PDFAppToolCallout* callout = new PDFAppToolCallout(hue, iconName, title, text, m_widget);
    addWidget(callout);
    return callout;
}

PDFAppParagraph* PDFAppToolPanelBuilder::addNote(const QString& text)
{
    PDFAppParagraph* paragraph = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, m_widget);
    paragraph->setObjectName(QStringLiteral("toolNote"));
    paragraph->setMinimumWidth(1);
    addWidget(paragraph);
    return paragraph;
}

PDFAppParagraph* PDFAppToolPanelBuilder::addHint(const QString& text)
{
    PDFAppParagraph* paragraph = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, m_widget);
    paragraph->setObjectName(QStringLiteral("toolHint"));
    paragraph->setMinimumWidth(1);
    addWidget(paragraph);
    return paragraph;
}

PDFPageSelection* PDFAppToolPanelBuilder::addPageSelection(const QString& parameter, const QString& label)
{
    if (!m_gui)
    {
        return nullptr;
    }

    PDFPageSelection* selection = new PDFPageSelection(m_widget);
    selection->setObjectName(QStringLiteral("toolPages-") + parameter);
    selection->setLabel(label);
    addWidget(selection);
    new PDFAppToolPageSelectionBinder(m_gui, parameter, selection, selection);
    return selection;
}

void PDFAppToolPanelBuilder::setFillsPanel(QWidget* widget)
{
    if (!widget || !m_widget)
    {
        return;
    }

    getCurrentLayout()->setStretchFactor(widget, 1);
    QSizePolicy policy = m_widget->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Expanding);
    policy.setVerticalStretch(1);
    m_widget->setSizePolicy(policy);
}

PDFAppToolChecklist* PDFAppToolPanelBuilder::addChecklist(std::vector<PDFAppToolFlagItem> items)
{
    PDFAppToolChecklist* list = new PDFAppToolChecklist(m_gui ? m_gui->getModel() : nullptr, std::move(items), m_widget);
    addWidget(list);
    return list;
}

PDFAppToolStepList* PDFAppToolPanelBuilder::addSteps(std::vector<PDFAppToolFlagItem> items)
{
    PDFAppToolStepList* list = new PDFAppToolStepList(m_gui ? m_gui->getModel() : nullptr, std::move(items), m_widget);
    addWidget(list);
    return list;
}

PDFAppToolPendingList* PDFAppToolPanelBuilder::addPendingList(const QString& title, QAbstractItemModel* model)
{
    PDFAppToolPendingList* list = new PDFAppToolPendingList(title, model, m_widget);
    addWidget(list);
    setFillsPanel(list);
    return list;
}

}   // namespace pdf

#include "moc_pdfapptoolpanelbuilder.cpp"
