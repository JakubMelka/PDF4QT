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

#include "pdfapptoolgui_merge.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfapppagelayout.h"
#include "pdfapppassworddialog.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"

#include <QAction>
#include <QClipboard>
#include <QCollator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace pdf
{

/// Names, texts and widths of the parts of Merge
class PDFAppMergeToolGUIHelper
{
public:
    PDFAppMergeToolGUIHelper() = delete;

    /// Widths of the panels of the toolbar (378:1879)
    static constexpr int ADD_PANEL_WIDTH = 290;
    static constexpr int SORT_PANEL_WIDTH = 250;
    static constexpr int OUTLINE_PANEL_WIDTH = 270;
    static constexpr int OPTIMIZE_PANEL_WIDTH = 260;

    /// Gap of the rows of the cards Selected page and Result (378:1630, 378:1643: rows of 28, a pitch of 36)
    static constexpr int KEY_VALUE_ROW_GAP = 8;

    /// Keys of Files… (Ctrl+I) and of Paste image (the paste of the platform, Ctrl+V); the
    /// actions and the texts of the rows of the menu (378:2505, 378:2513) share them
    static QKeySequence getAddFilesKeys() { return QKeySequence(Qt::CTRL | Qt::Key_I); }
    static QKeySequence getPasteKeys() { return QKeySequence(QKeySequence::Paste); }

    /// Interval of the polling of the reading of the inputs (ms; the loader steps a counter)
    static constexpr int PROGRESS_INTERVAL = 100;

    /// Most names of files in a sentence of the callout of the files, which need attention
    static constexpr int MAXIMUM_NAMES = 3;

    /// Property of a row of a panel: its key (an option of a parameter, an id of a document)
    static constexpr const char* ROW_KEY = "pdf4qtMergeKey";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// Ids of the text of the parameter "inputs"
    static QStringList splitIds(const QString& text)
    {
        QStringList ids;
        for (const QString& id : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            const QString trimmed = id.trimmed();
            if (!trimmed.isEmpty() && !ids.contains(trimmed))
            {
                ids << trimmed;
            }
        }
        return ids;
    }

    /// Count of an int-sized text from a PDFInteger
    static int toInt(PDFInteger value)
    {
        return int(qBound(PDFInteger(0), value, PDFInteger(std::numeric_limits<int>::max())));
    }

    /// "1 file", "3 files" (English has no plural forms without its translation: both texts)
    static QString files(int count)
    {
        return count == 1 ? PDFAppMergeToolGUI::tr("%Ln file", nullptr, count) : PDFAppMergeToolGUI::tr("%Ln files", nullptr, count);
    }

    /// "1 page", "16 pages"
    static QString pages(int count)
    {
        return count == 1 ? PDFAppMergeToolGUI::tr("%Ln page", nullptr, count) : PDFAppMergeToolGUI::tr("%Ln pages", nullptr, count);
    }

    /// "Annex_A.pdf", "Annex_A.pdf and Invoice.pdf", "Annex_A.pdf, Invoice.pdf and 2 more files"
    static QString joinNames(const QStringList& names)
    {
        if (names.size() <= 1)
        {
            return names.value(0);
        }
        if (names.size() <= MAXIMUM_NAMES)
        {
            QStringList first = names;
            const QString last = first.takeLast();
            return PDFAppMergeToolGUI::tr("%1 and %2").arg(first.join(PDFAppMergeToolGUI::tr(", ")), last);
        }
        const QStringList first = names.mid(0, MAXIMUM_NAMES - 1);
        return PDFAppMergeToolGUI::tr("%1 and %2 more").arg(first.join(PDFAppMergeToolGUI::tr(", ")), files(int(names.size()) - (MAXIMUM_NAMES - 1)));
    }

    /// The first letter in lower case, without the final period (a reason in a line of a row)
    static QString toClause(QString text)
    {
        text = text.trimmed();
        if (text.endsWith(QLatin1Char('.')))
        {
            text.chop(1);
        }
        if (!text.isEmpty())
        {
            text[0] = text[0].toLower();
        }
        return text;
    }
};

/// Registers the tool and its GUI under the id, when they are not registered yet
class PDFAppMergeToolGUIRegistration
{
public:
    PDFAppMergeToolGUIRegistration() = delete;

    static void registerTool()
    {
        const QString toolId = QString::fromLatin1(PDFDocumentToolMerge::TOOL_ID);
        if (!PDFDocumentToolRegistry::isImplemented(toolId))
        {
            PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolMerge>());
        }
        if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
        {
            PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppMergeToolGUI>>());
        }
    }
};

// ---------------------------------------------------------- PDFAppToolMergeEmptyState

/// The card of the drop zone of 378:1827: bg/surface with the dashed border/strong
class PDFAppToolMergeEmptyCard : public QWidget
{
public:
    explicit PDFAppToolMergeEmptyCard(QWidget* parent) :
        QWidget(parent)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const PDFStylePainterGuard guard(&painter);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF shape = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        PDFStylePainter::fill(&painter, shape, PDFStylePainter::Radii(PDFAppToolMergeEmptyState::RADIUS), PDFThemeColors::BgSurface);
        QPen pen(PDFStylePainter::color(PDFThemeColors::BorderStrong), 1.0);
        pen.setDashPattern({ 4.0, 3.0 });
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(PDFStylePainter::roundedPath(shape, PDFStylePainter::Radii(PDFAppToolMergeEmptyState::RADIUS)));
    }
};

PDFAppToolMergeEmptyState::PDFAppToolMergeEmptyState(const std::vector<Document>& documents, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("mergeEmptyState"));

    // The card in the middle of the stage (378:1827 at 117, 218 of 774 x 802)
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);
    QHBoxLayout* center = new QHBoxLayout();
    center->addStretch(1);
    PDFAppToolMergeEmptyCard* card = new PDFAppToolMergeEmptyCard(this);
    card->setObjectName(QStringLiteral("mergeDropZone"));
    card->setFixedWidth(CARD_WIDTH);
    m_card = card;
    center->addWidget(card);
    center->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(1);

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(PADDING_SIDE, PADDING_TOP, PADDING_SIDE, PADDING_BOTTOM);
    layout->setSpacing(GAP);

    // icon-tile 378:1828: bg/accent-subtle, layers 16
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("layers"), ICON_TILE, 16, card);
    icon->setRadius(8);
    icon->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Add the files to merge"), QFont::DemiBold, 16, PDFThemeColors::TextPrimary, card);
    title->setObjectName(QStringLiteral("mergeEmptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    PDFLabel* text = PDFAppPageWidgets::createLabel(tr("PDF documents and images. Drop them anywhere in this window."), QFont::Normal, 12, PDFThemeColors::TextSecondary, card);
    text->setObjectName(QStringLiteral("mergeEmptyText"));
    text->setAlignment(Qt::AlignCenter);
    layout->addWidget(text);

    m_addFilesButton = PDFAppPageWidgets::createButton(tr("Add Files…"), QStringLiteral("plus"), true, card);
    m_addFilesButton->setObjectName(QStringLiteral("mergeEmptyAddFiles"));
    connect(m_addFilesButton.data(), &QPushButton::clicked, this, &PDFAppToolMergeEmptyState::addFilesRequested);
    layout->addWidget(m_addFilesButton, 0, Qt::AlignHCenter);

    if (!documents.empty())
    {
        // caption 378:1838 (8 above it) and a row of each open document (378:1851)
        PDFLabel* caption = PDFAppPageWidgets::createLabel(tr("Open documents"), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, card);
        caption->setObjectName(QStringLiteral("mergeEmptyCaption"));
        caption->setContentsMargins(0, 8, 0, 0);
        layout->addWidget(caption);

        for (const Document& document : documents)
        {
            QWidget* row = new QWidget(card);
            row->setObjectName(QStringLiteral("mergeEmptyDocument-") + document.documentId);
            row->setFixedHeight(ROW_HEIGHT);
            QHBoxLayout* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(8, 8, 8, 8);
            rowLayout->setSpacing(10);

            PDFIconBadge* tile = new PDFIconBadge(QStringLiteral("tab"), 30, 16, row);
            tile->setRadius(8);
            tile->setColors(PDFThemeColors::BgAccentSubtle, PDFThemeColors::IconAccent);
            rowLayout->addWidget(tile);

            QVBoxLayout* textLayout = new QVBoxLayout();
            textLayout->setContentsMargins(0, 0, 0, 0);
            textLayout->setSpacing(1);
            QHBoxLayout* top = new QHBoxLayout();
            top->setContentsMargins(0, 0, 0, 0);
            top->setSpacing(6);
            // text 378:1852: the name in a line of 15, 1 above the line of 13 (as the rows of Files)
            PDFLabel* name = PDFAppPageWidgets::createLabel(document.name, QFont::DemiBold, 12, PDFThemeColors::TextPrimary, row);
            name->setMinimumWidth(1);
            name->setLineHeight(PDFAppToolInputRow::NAME_HEIGHT);
            top->addWidget(name);
            if (document.isModified)
            {
                PDFTag* tag = new PDFTag(tr("unsaved changes"), row);
                tag->setFocusPolicy(Qt::NoFocus);
                tag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                top->addWidget(tag);
            }
            top->addStretch(1);
            textLayout->addLayout(top);
            PDFLabel* details = PDFAppPageWidgets::createLabel(document.details, QFont::Normal, 11, PDFThemeColors::TextTertiary, row);
            details->setLineHeight(PDFAppToolInputRow::LINE_HEIGHT);
            textLayout->addWidget(details);
            // The two lines stay together in the middle of the row (the labels do not grow)
            textLayout->insertStretch(0, 1);
            textLayout->addStretch(1);
            rowLayout->addLayout(textLayout, 1);

            QPushButton* add = PDFAppPageWidgets::createButton(tr("Add"), QStringLiteral("plus"), false, row);
            add->setObjectName(QStringLiteral("mergeEmptyAdd"));
            add->setProperty(PDFAppMergeToolGUIHelper::ROW_KEY, document.documentId);
            add->setAccessibleDescription(document.name);
            connect(add, &QPushButton::clicked, this, &PDFAppToolMergeEmptyState::onAddClicked);
            rowLayout->addWidget(add);
            m_addButtons.push_back(add);
            layout->addWidget(row);
        }
    }
}

PDFAppToolMergeEmptyState::~PDFAppToolMergeEmptyState() = default;

std::vector<QPushButton*> PDFAppToolMergeEmptyState::getAddButtons() const
{
    std::vector<QPushButton*> buttons;
    for (const QPointer<QPushButton>& button : m_addButtons)
    {
        if (button)
        {
            buttons.push_back(button.data());
        }
    }
    return buttons;
}

void PDFAppToolMergeEmptyState::onAddClicked()
{
    QObject* button = sender();
    if (button)
    {
        Q_EMIT addDocumentRequested(button->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString());
    }
}

// ------------------------------------------------------------- PDFAppMergeToolGUI

PDFAppMergeToolGUI::PDFAppMergeToolGUI(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppMergeToolGUI::~PDFAppMergeToolGUI()
{
    // The key widget gets back what it had: its drops and no actions of the tool
    if (m_keyWidget)
    {
        m_keyWidget->removeEventFilter(this);
        m_keyWidget->setAcceptDrops(m_wasDropAccepted);
    }
    delete m_addFilesAction.data();
    delete m_pasteAction.data();
    if (m_passwordDialog)
    {
        m_passwordDialog->disconnect(this);
        m_passwordDialog->close();
    }
}

void PDFAppMergeToolGUI::registerTools()
{
    PDFAppMergeToolGUIRegistration::registerTool();
}

// ------------------------------------------------------------- values and the list

void PDFAppMergeToolGUI::connectList()
{
    PDFAppToolInputList* list = getInputList();
    if (list == m_list)
    {
        return;
    }

    if (m_list)
    {
        disconnect(m_list.data(), nullptr, this, nullptr);
    }
    m_list = list;
    if (m_list)
    {
        connect(m_list.data(), &PDFAppToolInputList::inputsChanged, this, &PDFAppMergeToolGUI::onListInputsChanged);
        connect(m_list.data(), &PDFAppToolInputList::inputChanged, this, &PDFAppMergeToolGUI::onListInputChanged);
        connect(m_list.data(), &QObject::destroyed, this, &PDFAppMergeToolGUI::onListDestroyed);
    }

    if (!m_progressTimer)
    {
        m_progressTimer = new QTimer(this);
        m_progressTimer->setInterval(PDFAppMergeToolGUIHelper::PROGRESS_INTERVAL);
        connect(m_progressTimer.data(), &QTimer::timeout, this, &PDFAppMergeToolGUI::onProgressTimer);
    }

    if (!m_isShellConnected && getShell())
    {
        m_isShellConnected = true;
        connect(getShell(), &PDFDocumentToolShell::stateChanged, this, &PDFAppMergeToolGUI::onShellStateChanged);
    }

    if (!m_isRegistryConnected && getContext() && getContext()->getDocumentRegistry())
    {
        // The open documents of Add Files and of the empty state follow the registry
        m_isRegistryConnected = true;
        PDFAppDocumentRegistry* registry = getContext()->getDocumentRegistry();
        connect(registry, &PDFAppDocumentRegistry::documentAdded, this, &PDFAppMergeToolGUI::onRegistryChanged);
        connect(registry, &PDFAppDocumentRegistry::documentChanged, this, &PDFAppMergeToolGUI::onRegistryChanged);
        connect(registry, &PDFAppDocumentRegistry::documentRemoved, this, &PDFAppMergeToolGUI::onRegistryChanged);
    }

    initializeValues();
}

void PDFAppMergeToolGUI::initializeValues()
{
    using Helper = PDFAppMergeToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (m_isValuesInitialized || !model || !m_list)
    {
        return;
    }

    // K21: the inputs, which the tab came with, are no change of the user (no undo step)
    m_isValuesInitialized = true;
    if (getModelInputIds().isEmpty() && m_list->getCount() > 0)
    {
        const QStringList ids = m_list->getInputIds();
        model->setFollowingValue(Helper::name(PDFDocumentToolMerge::PARAMETER_INPUTS), ids.join(QLatin1Char(',')));
        model->setFollowingValue(Helper::name(PDFDocumentToolMerge::PARAMETER_PLAN), PDFAppToolMergePlan::createDefault(ids).toText(ids, true));
    }
    readValues();
}

QStringList PDFAppMergeToolGUI::getModelInputIds() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? PDFAppMergeToolGUIHelper::splitIds(model->getValue(PDFAppMergeToolGUIHelper::name(PDFDocumentToolMerge::PARAMETER_INPUTS)).toString()) : QStringList();
}

std::vector<PDFInteger> PDFAppMergeToolGUI::getKnownPageCounts(const QStringList& inputIds) const
{
    // The pages of a usable input are known; a pending input waits (-1)
    std::vector<PDFInteger> counts;
    counts.reserve(size_t(inputIds.size()));
    for (const QString& id : inputIds)
    {
        const PDFAppToolInput* input = m_list ? m_list->findInput(id) : nullptr;
        counts.push_back(input && input->isUsable() ? input->pageCount : -1);
    }
    return counts;
}

void PDFAppMergeToolGUI::readValues()
{
    using Helper = PDFAppMergeToolGUIHelper;

    const PDFAppToolParameterModel* model = getModel();
    if (!model || !m_list)
    {
        return;
    }

    // The selection before the change is kept by the identity of its pages (Undo / Redo, a
    // reading finished); a change of this GUI says its own selection
    const std::vector<PageKey> selectedKeys = getSelectedKeys();

    // The inputs of the list follow the values (Undo / Redo: a parked input comes back without
    // a second reading, a removed one is parked); the list announces it, the slot waits
    QStringList ids = getModelInputIds();
    QPointer<PDFAppMergeToolGUI> guard(this);
    if (ids != m_list->getInputIds())
    {
        m_isChangingList = true;
        m_list->setInputIds(ids);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
        if (!m_list)
        {
            return;
        }
        ids = m_list->getInputIds();
    }

    // The parked inputs, which no step of the history names, are freed (R12)
    QStringList historyIds;
    for (const QVariant& value : model->getHistoryValues(Helper::name(PDFDocumentToolMerge::PARAMETER_INPUTS)))
    {
        historyIds << Helper::splitIds(value.toString());
    }
    m_list->dropParkedInputs(historyIds);
    if (!guard || !m_list)
    {
        return;
    }

    // The pages: the text of the plan for these inputs, the pending inputs with known pages
    // become their pages; an invalid text (a value of another version) is the default plan
    const std::vector<PDFInteger> counts = getKnownPageCounts(ids);
    const QString text = model->getValue(Helper::name(PDFDocumentToolMerge::PARAMETER_PLAN)).toString();
    std::optional<PDFAppToolMergePlan> plan = PDFAppToolMergePlan::fromText(text, ids, counts, nullptr);
    m_plan = (plan ? *plan : PDFAppToolMergePlan::createDefault(ids)).resolved(ids, counts);

    if (m_canvas)
    {
        // The reset of the model and the new selection are said to the views and to their
        // observers, which may delete this GUI (the tab closes from them)
        m_canvas->getModel()->setPlan(m_plan);
        if (!guard || !m_canvas)
        {
            return;
        }
        if (m_hasPendingSelection)
        {
            std::vector<int> rows;
            for (const size_t item : m_pendingSelection)
            {
                const int row = m_canvas->getModel()->getRowOfItem(item);
                if (row >= 0)
                {
                    rows.push_back(row);
                }
            }
            selectRows(rows);
        }
        else
        {
            selectRows(getRowsOfKeys(selectedKeys));
        }
        if (!guard)
        {
            return;
        }
    }
    m_hasPendingSelection = false;
    m_pendingSelection.clear();
    updateAll();
}

void PDFAppMergeToolGUI::applyChange(const QStringList& inputIds, const PDFAppToolMergePlan& plan, const std::vector<size_t>& selectedItems)
{
    using Helper = PDFAppMergeToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    // One undo step of both values; the model announces it (readValues), the selection comes
    // with it
    QVariantMap values;
    values.insert(Helper::name(PDFDocumentToolMerge::PARAMETER_INPUTS), inputIds.join(QLatin1Char(',')));
    values.insert(Helper::name(PDFDocumentToolMerge::PARAMETER_PLAN), plan.toText(inputIds, true));
    m_pendingSelection = selectedItems;
    m_hasPendingSelection = true;

    QPointer<PDFAppMergeToolGUI> guard(this);
    const QString error = model->setValues(values);
    if (!guard)
    {
        return;
    }
    if (m_hasPendingSelection)
    {
        // Nothing changed (the same values): the selection is set now
        m_hasPendingSelection = false;
        if (m_canvas)
        {
            std::vector<int> rows;
            for (const size_t item : selectedItems)
            {
                const int row = m_canvas->getModel()->getRowOfItem(item);
                if (row >= 0)
                {
                    rows.push_back(row);
                }
            }
            selectRows(rows);
            if (!guard)
            {
                return;
            }
        }
    }
    if (!error.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

std::vector<size_t> PDFAppMergeToolGUI::getSelectedItems() const
{
    std::vector<size_t> items;
    if (m_canvas)
    {
        for (const int row : getSelectedRows())
        {
            const int item = m_canvas->getModel()->getItemIndex(row);
            if (item >= 0)
            {
                items.push_back(size_t(item));
            }
        }
    }
    return items;
}

std::vector<PDFAppMergeToolGUI::PageKey> PDFAppMergeToolGUI::getSelectedKeys() const
{
    std::vector<PageKey> keys;
    if (!m_canvas)
    {
        return keys;
    }

    // The occurrence of a page among the pages of the plan before it (a clone is the second)
    const std::vector<PDFAppToolMergePlan::Item>& items = m_canvas->getModel()->getPlan().getItems();
    const std::vector<size_t> selected = getSelectedItems();
    std::set<size_t> selectedSet(selected.cbegin(), selected.cend());
    std::map<std::pair<QString, PDFInteger>, int> occurrences;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isPending)
        {
            continue;
        }
        const int occurrence = occurrences[{ items[i].inputId, items[i].pageIndex }]++;
        if (selectedSet.count(i))
        {
            keys.push_back({ items[i].inputId, items[i].pageIndex, occurrence });
        }
    }
    return keys;
}

std::vector<int> PDFAppMergeToolGUI::getRowsOfKeys(const std::vector<PageKey>& keys) const
{
    std::vector<int> rows;
    if (!m_canvas || keys.empty())
    {
        return rows;
    }

    const PDFAppToolMergePageModel* model = m_canvas->getModel();
    const std::vector<PDFAppToolMergePlan::Item>& items = model->getPlan().getItems();
    std::map<std::pair<QString, PDFInteger>, int> occurrences;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].isPending)
        {
            continue;
        }
        const PageKey key{ items[i].inputId, items[i].pageIndex, occurrences[{ items[i].inputId, items[i].pageIndex }]++ };
        if (std::find(keys.cbegin(), keys.cend(), key) != keys.cend())
        {
            rows.push_back(model->getRowOfItem(i));
        }
    }
    return rows;
}

QStringList PDFAppMergeToolGUI::getUsableInputIds() const
{
    // The usable inputs with a page in the result (an input, whose pages were all removed,
    // stays in the list, it is no input of the run)
    std::set<QString> inputsWithPages;
    for (const PDFAppToolMergePlan::Item& item : m_plan.getItems())
    {
        if (!item.isPending)
        {
            inputsWithPages.insert(item.inputId);
        }
    }

    QStringList ids;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        if (input->isUsable() && inputsWithPages.count(input->id))
        {
            ids << input->id;
        }
    }
    return ids;
}

void PDFAppMergeToolGUI::onListInputsChanged()
{
    // The changes of this GUI come with their values; another change (none is known now) is
    // shown as it is
    if (!m_isChangingList)
    {
        readValues();
    }
}

void PDFAppMergeToolGUI::onListInputChanged(const QString& inputId)
{
    Q_UNUSED(inputId);

    // A reading has finished or a password was accepted: the pending input gets its pages (no
    // undo step - the plan says the same: "every page of the input"). The readings of many
    // files finish together (their results come in one pass of the event loop) and an update
    // of the parts grows with the files: one update after them (plan 6, 1 000 files)
    if (!m_isChangingList && !m_isReadQueued)
    {
        m_isReadQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppMergeToolGUI::onReadQueued, Qt::QueuedConnection);
    }
}

void PDFAppMergeToolGUI::onReadQueued()
{
    m_isReadQueued = false;
    readValues();
}

void PDFAppMergeToolGUI::onListDestroyed()
{
    m_list = nullptr;
}

// ------------------------------------------------------------- the actions of the user

void PDFAppMergeToolGUI::addResults(const std::vector<PDFAppToolInputList::AddResult>& results, const QStringList& refusedTexts)
{
    if (!m_list)
    {
        return;
    }

    PDFAppToolMergePlan plan = m_plan;
    QStringList refused = refusedTexts;
    bool isAdded = false;
    for (const PDFAppToolInputList::AddResult& result : results)
    {
        if (result.id.isEmpty())
        {
            refused << PDFAppToolInputList::getRefusalText(result.refusal, result.name);
        }
        else
        {
            plan.appendInput(result.id);
            isAdded = true;
        }
    }

    // K29: "Contract.pdf is already in the list" in the status bar (the last call)
    QPointer<PDFAppMergeToolGUI> guard(this);
    if (isAdded)
    {
        applyChange(m_list->getInputIds(), plan, getSelectedItems());
    }
    if (guard && !refused.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(refused.join(QStringLiteral(" · ")));
    }
}

void PDFAppMergeToolGUI::addFiles(const QStringList& filePaths)
{
    if (!m_list || !isEditable() || filePaths.isEmpty())
    {
        return;
    }

    // The list announces each addition, the values follow at once as one undo step
    QPointer<PDFAppMergeToolGUI> guard(this);
    QPointer<PDFAppToolInputList> list(m_list);
    m_isChangingList = true;
    const std::vector<PDFAppToolInputList::AddResult> results = list->addFiles(filePaths);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults(results, QStringList());
}

void PDFAppMergeToolGUI::addFilesFromDialog()
{
    if (!isEditable())
    {
        return;
    }

    // The native dialog runs its own event loop: the GUI may go meanwhile
    QPointer<PDFAppMergeToolGUI> guard(this);
    QWidget* parent = m_canvas ? m_canvas->window() : nullptr;
    const QStringList files = PDFAppFileDialogs::getOpenInputFileNames(parent, QString());
    if (guard && !files.isEmpty())
    {
        addFiles(files);
    }
}

void PDFAppMergeToolGUI::pasteImage()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QImage image = clipboard ? clipboard->image() : QImage();
    if (image.isNull())
    {
        if (getHost())
        {
            getHost()->showTransientMessage(tr("The clipboard has no image"));
        }
        return;
    }

    QPointer<PDFAppMergeToolGUI> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addImage(image, QString());
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ result }, QStringList());
}

void PDFAppMergeToolGUI::addOpenDocument(const QString& documentId)
{
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    PDFAppDocument* document = registry ? registry->findDocument(documentId) : nullptr;
    if (!m_list || !document || !isEditable())
    {
        return;
    }

    // R11: a snapshot of now, the tab is not followed
    QPointer<PDFAppMergeToolGUI> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addOpenDocument(document);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ result }, QStringList());
}

void PDFAppMergeToolGUI::removeInput(const QString& inputId)
{
    if (!m_list || !isEditable() || m_list->indexOf(inputId) < 0)
    {
        return;
    }

    QStringList ids = m_list->getInputIds();
    ids.removeAll(inputId);
    PDFAppToolMergePlan plan = m_plan;
    plan.removeInput(inputId);
    applyChange(ids, plan, std::vector<size_t>());
}

void PDFAppMergeToolGUI::moveInput(const QString& inputId, int targetIndex)
{
    if (!m_list || !isEditable())
    {
        return;
    }

    QStringList ids = m_list->getInputIds();
    const int index = int(ids.indexOf(inputId));
    if (index < 0 || targetIndex < 0 || targetIndex > ids.size() || targetIndex == index || targetIndex == index + 1)
    {
        return;
    }

    // The file goes in front of the target, its pages follow the order of the files
    ids.removeAt(index);
    ids.insert(targetIndex > index ? targetIndex - 1 : targetIndex, inputId);
    PDFAppToolMergePlan plan = m_plan;
    plan.orderByInputs(ids);
    applyChange(ids, plan, std::vector<size_t>());
}

void PDFAppMergeToolGUI::rotateSelection(bool isClockwise)
{
    const std::vector<size_t> items = getSelectedItems();
    if (!m_list || items.empty() || !isEditable())
    {
        return;
    }

    PDFAppToolMergePlan plan = m_plan;
    plan.rotateItems(items, isClockwise);
    applyChange(m_list->getInputIds(), plan, items);
}

void PDFAppMergeToolGUI::rotateLeft()
{
    rotateSelection(false);
}

void PDFAppMergeToolGUI::rotateRight()
{
    rotateSelection(true);
}

void PDFAppMergeToolGUI::cloneSelection()
{
    const std::vector<size_t> items = getSelectedItems();
    if (!m_list || items.empty() || !isEditable())
    {
        return;
    }

    // The copies follow their pages and are selected (Clone again copies the copies)
    PDFAppToolMergePlan plan = m_plan;
    const std::vector<size_t> clones = plan.cloneItems(items);
    applyChange(m_list->getInputIds(), plan, clones);
}

void PDFAppMergeToolGUI::removeSelection()
{
    const std::vector<int> rows = getSelectedRows();
    const std::vector<size_t> items = getSelectedItems();
    if (!m_list || items.empty() || !isEditable() || !m_canvas)
    {
        return;
    }

    // The page after the removed ones is selected (Delete again removes it)
    PDFAppToolMergePlan plan = m_plan;
    plan.removeItems(items);
    const int nextRow = rows.front();
    std::vector<size_t> selection;
    size_t pageCount = 0;
    for (size_t i = 0; i < plan.getItems().size(); ++i)
    {
        if (!plan.getItems()[i].isPending)
        {
            if (int(pageCount) == qMin(nextRow, int(plan.getPageCount()) - 1))
            {
                selection.push_back(i);
                break;
            }
            ++pageCount;
        }
    }
    applyChange(m_list->getInputIds(), plan, selection);
}

void PDFAppMergeToolGUI::moveSelection(int delta)
{
    const std::vector<int> rows = getSelectedRows();
    if (rows.empty() || !m_canvas)
    {
        return;
    }

    // One place earlier: in front of the page before the first selected one; one place later:
    // in front of the page after the next one of the last selected one
    const int rowCount = m_canvas->getModel()->rowCount();
    const int target = delta < 0 ? rows.front() - 1 : rows.back() + 2;
    if (target < 0 || target > rowCount)
    {
        return;
    }
    moveSelectionTo(rows, target);
}

void PDFAppMergeToolGUI::moveSelectionTo(const std::vector<int>& rows, int targetRow)
{
    if (!m_list || !m_canvas || rows.empty() || !isEditable())
    {
        return;
    }

    const PDFAppToolMergePageModel* model = m_canvas->getModel();
    std::vector<size_t> items;
    for (const int row : rows)
    {
        const int item = model->getItemIndex(row);
        if (item >= 0)
        {
            items.push_back(size_t(item));
        }
    }
    const int targetItem = targetRow < model->rowCount() ? model->getItemIndex(targetRow) : int(m_plan.getItems().size());
    if (items.empty() || targetItem < 0)
    {
        return;
    }

    PDFAppToolMergePlan plan = m_plan;
    const std::vector<size_t> moved = plan.moveItems(items, size_t(targetItem));
    if (plan == m_plan)
    {
        return;
    }
    applyChange(m_list->getInputIds(), plan, moved);
}

void PDFAppMergeToolGUI::sortByName()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    // "A to Z, numbers in natural order" (378:2539)
    QCollator collator(PDFAppLocale::getUiLocale());
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::vector<std::pair<QString, QString>> names;
    for (int i = 0; i < m_list->getCount(); ++i)
    {
        names.emplace_back(m_list->getInput(i)->name, m_list->getInput(i)->id);
    }
    std::vector<size_t> order(names.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    // A stable insertion sort by the collator (no comparator object: the conventions of the
    // library; the list has at most MAXIMUM_INPUTS files)
    for (size_t i = 1; i < order.size(); ++i)
    {
        const size_t current = order[i];
        size_t j = i;
        while (j > 0 && collator.compare(names[current].first, names[order[j - 1]].first) < 0)
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = current;
    }

    QStringList ids;
    for (const size_t index : order)
    {
        ids << names[index].second;
    }
    PDFAppToolMergePlan plan = m_plan;
    plan.orderByInputs(ids);
    applyChange(ids, plan, std::vector<size_t>());
}

void PDFAppMergeToolGUI::sortByType()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    // "PDF documents first, then images" (378:2546), each kind in its order
    QStringList pdfIds;
    QStringList imageIds;
    for (int i = 0; i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        (input->isImage() ? imageIds : pdfIds) << input->id;
    }
    const QStringList ids = pdfIds + imageIds;
    PDFAppToolMergePlan plan = m_plan;
    plan.orderByInputs(ids);
    applyChange(ids, plan, std::vector<size_t>());
}

void PDFAppMergeToolGUI::sortByPageNumber()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    PDFAppToolMergePlan plan = m_plan;
    plan.sortByPageNumber(m_list->getInputIds());
    applyChange(m_list->getInputIds(), plan, std::vector<size_t>());
}

void PDFAppMergeToolGUI::reverseOrder()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    // "Turns the current order around" (378:2562): the files and the runs of their pages
    QStringList ids = m_list->getInputIds();
    std::reverse(ids.begin(), ids.end());
    PDFAppToolMergePlan plan = m_plan;
    plan.reverseRuns();
    applyChange(ids, plan, std::vector<size_t>());
}

void PDFAppMergeToolGUI::openPasswordDialog(const QString& inputId)
{
    if (!m_list || !m_canvas)
    {
        return;
    }
    if (m_passwordDialog)
    {
        m_passwordDialog->raise();
        return;
    }

    // A popup (new + QPointer, deleted when it is closed); Remove File asks this GUI, which
    // removes the input by its Undo history
    m_passwordDialog = PDFAppPasswordDialog::openDialog(m_list, inputId, m_canvas->window());
    if (m_passwordDialog)
    {
        connect(m_passwordDialog.data(), &PDFAppPasswordDialog::removeRequested, this, &PDFAppMergeToolGUI::onPasswordRemoveRequested);
    }
}

void PDFAppMergeToolGUI::onPasswordRemoveRequested(const QString& inputId)
{
    removeInput(inputId);
}

bool PDFAppMergeToolGUI::dropFiles(const QMimeData* mimeData)
{
    if (!PDFAppFileDrop::hasLocalFiles(mimeData) || !isEditable())
    {
        return false;
    }

    // K8: PDF files and images, in the dropped order; the refused items are said
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(mimeData, PDFAppFileDrop::MAXIMUM_FILES, true);
    QStringList refused;
    for (const PDFAppFileDrop::RefusedItem& item : result.refused)
    {
        refused << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
    }

    QPointer<PDFAppMergeToolGUI> guard(this);
    std::vector<PDFAppToolInputList::AddResult> results;
    if (m_list && !result.files.isEmpty())
    {
        m_isChangingList = true;
        results = m_list->addFiles(result.files);
        if (!guard)
        {
            return true;
        }
        m_isChangingList = false;
    }
    addResults(results, refused);
    return true;
}

void PDFAppMergeToolGUI::setDetailsShown(bool isDetailsShown)
{
    m_isDetailsShown = isDetailsShown;
    if (m_canvas && m_canvas->isDetailsShown() != isDetailsShown)
    {
        m_canvas->setDetailsShown(isDetailsShown);
    }
    updateToolbar();

    // The status says the view (408:32003); its slots may delete this GUI: the last call
    updateStatusText();
}

void PDFAppMergeToolGUI::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    m_isTileSizeRead = true;
    m_tileSize = size;
    if (m_tileSizeMenu)
    {
        m_tileSizeMenu->setSize(size);
    }
    if (m_canvas)
    {
        QPointer<PDFAppMergeToolGUI> guard(this);
        m_canvas->setTileSize(size);
        if (!guard)
        {
            return;
        }
    }

    // The store announces the change of the setting to its readers (the last call)
    PDFAppToolTileSizeMenu::storeSize(getContext(), size);
}

std::vector<int> PDFAppMergeToolGUI::getSelectedRows() const
{
    return m_canvas ? m_canvas->getPageView()->getSelectedRows() : std::vector<int>();
}

void PDFAppMergeToolGUI::selectRows(const std::vector<int>& rows)
{
    if (m_canvas)
    {
        m_canvas->getPageView()->selectRows(rows);
    }
}

// ------------------------------------------------------------- the parts

QWidget* PDFAppMergeToolGUI::createToolActions(QWidget* parent)
{
    using Helper = PDFAppMergeToolGUIHelper;

    connectList();
    m_actions.clear();
    m_panels.clear();
    m_panelRows.clear();
    m_openDocumentRows.clear();
    m_openDocumentsCaption = nullptr;

    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("toolActions"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Horizontal, PDFAppToolPanelBuilder::ACTION_GAP);

    // tb-Add Files 378:1149 with panel-add 378:2497: Files… (Ctrl+I), Paste image (Ctrl+V), the
    // open documents of the window
    PDFAppToolPanelPopup* addPanel = builder.addMenu(QStringLiteral("addFiles"), QStringLiteral("plus"), tr("Add Files"), Helper::ADD_PANEL_WIDTH);
    m_panels[QStringLiteral("addFiles")] = addPanel;
    m_actions[QStringLiteral("addFiles")] = addPanel->getButton();
    // The shortcuts in the rows (378:2505, 378:2513) are the native texts of the keys of the platform
    const QString addFilesKeys = Helper::getAddFilesKeys().toString(QKeySequence::NativeText);
    const QString pasteKeys = Helper::getPasteKeys().toString(QKeySequence::NativeText);
    addPanel->getButton()->setToolTip(tr("Add files (%1)").arg(addFilesKeys));
    PDFTileRow* filesRow = addPanel->addRow(tr("Files…"), tr("PDF documents and images"), QStringLiteral("folder-open"), PDFTileRow::Kind::MenuItem);
    filesRow->setObjectName(QStringLiteral("mergeAddFilesRow"));
    filesRow->setShortcutText(addFilesKeys);
    connect(filesRow, &PDFTileRow::clicked, this, &PDFAppMergeToolGUI::onAddFilesRowClicked);
    m_panelRows[QStringLiteral("files")] = filesRow;
    PDFTileRow* pasteRow = addPanel->addRow(tr("Paste image"), tr("Adds the picture from the clipboard"), QStringLiteral("clipboard"), PDFTileRow::Kind::MenuItem);
    pasteRow->setObjectName(QStringLiteral("mergePasteRow"));
    pasteRow->setShortcutText(pasteKeys);
    connect(pasteRow, &PDFTileRow::clicked, this, &PDFAppMergeToolGUI::onPasteRowClicked);
    m_panelRows[QStringLiteral("paste")] = pasteRow;
    updateOpenDocumentsRows();

    builder.addActionDivider();

    // tb-rotate-left, tb-rotate-right, tb-clone, tb-remove 378:1156 … 378:1171: icon buttons
    struct IconAction
    {
        const char* name;
        QString iconName;
        QString text;
        void (PDFAppMergeToolGUI::*slot)();
    };
    const IconAction iconActions[] = {
        { "rotateLeft", QStringLiteral("rotate-ccw"), tr("Rotate Left"), &PDFAppMergeToolGUI::rotateLeft },
        { "rotateRight", QStringLiteral("rotate-cw"), tr("Rotate Right"), &PDFAppMergeToolGUI::rotateRight },
        { "clone", QStringLiteral("duplicate-page"), tr("Clone"), &PDFAppMergeToolGUI::cloneSelection },
        { "remove", QStringLiteral("trash"), tr("Remove (Delete)"), &PDFAppMergeToolGUI::removeSelection },
    };
    for (const IconAction& action : iconActions)
    {
        QToolButton* button = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("mergeAction-") + QString::fromLatin1(action.name), action.iconName, widget);
        button->setToolTip(action.text);
        button->setAccessibleName(action.text);
        connect(button, &QToolButton::clicked, this, action.slot);
        builder.addWidget(button);
        m_actions[QString::fromLatin1(action.name)] = button;
    }

    builder.addActionDivider();

    // tb-Sort 378:1178 with panel-sort 378:2532
    PDFAppToolPanelPopup* sortPanel = builder.addMenu(QStringLiteral("sort"), QStringLiteral("arrow-up-down"), tr("Sort"), Helper::SORT_PANEL_WIDTH);
    m_panels[QStringLiteral("sort")] = sortPanel;
    m_actions[QStringLiteral("sort")] = sortPanel->getButton();
    struct SortRow
    {
        const char* key;
        QString title;
        QString description;
        QString iconName;
    };
    const SortRow sortRows[] = {
        { "name", tr("File name"), tr("A to Z, numbers in natural order"), QStringLiteral("sort-az") },
        { "type", tr("File type"), tr("PDF documents first, then images"), QStringLiteral("images") },
        { "page", tr("Page number"), tr("Original page numbers of each file"), QStringLiteral("list-ordered") },
        { "reverse", tr("Reverse order"), tr("Turns the current order around"), QStringLiteral("reverse") },
    };
    for (const SortRow& sortRow : sortRows)
    {
        if (QLatin1String(sortRow.key) == QLatin1String("reverse"))
        {
            sortPanel->addGroupDivider();
        }
        PDFTileRow* row = sortPanel->addRow(sortRow.title, sortRow.description, sortRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("mergeSort-") + QString::fromLatin1(sortRow.key));
        row->setProperty(Helper::ROW_KEY, QString::fromLatin1(sortRow.key));
        connect(row, &PDFTileRow::clicked, this, &PDFAppMergeToolGUI::onSortRowClicked);
        m_panelRows[QStringLiteral("sort-") + QString::fromLatin1(sortRow.key)] = row;
    }

    // tb-outline 378:1183 with panel-outline 378:2563 and tb-optimize 378:1188 with
    // panel-optimize 378:2587: the texts and the icons of the frames (the parameters of the core)
    struct OptionRow
    {
        const char* parameter;
        const char* option;
        QString title;
        QString description;
        QString iconName;
    };
    const OptionRow outlineRows[] = {
        { PDFDocumentToolMerge::PARAMETER_OUTLINE, PDFDocumentToolMerge::OUTLINE_DOCUMENT_PARTS, tr("Document parts"), tr("One bookmark per source file"), QStringLiteral("tree") },
        { PDFDocumentToolMerge::PARAMETER_OUTLINE, PDFDocumentToolMerge::OUTLINE_JOIN, tr("Join outlines"), tr("Bookmarks of the files nested under each part"), QStringLiteral("list") },
        { PDFDocumentToolMerge::PARAMETER_OUTLINE, PDFDocumentToolMerge::OUTLINE_NONE, tr("No outline"), tr("The result has no bookmarks"), QStringLiteral("ban") },
    };
    const OptionRow optimizeRows[] = {
        { PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES, PDFDocumentToolMerge::OPTIMIZE_OFF, tr("Off"), tr("Images are stored exactly as they are"), QStringLiteral("ban") },
        { PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES, PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY, tr("Prefer quality"), tr("Downsamples large images, keeps detail"), QStringLiteral("award") },
        { PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES, PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE, tr("Minimum size"), tr("Smallest file, stronger compression"), QStringLiteral("minimize-2") },
    };
    PDFAppToolPanelPopup* outlinePanel = builder.addMenu(QStringLiteral("outline"), QStringLiteral("tree"), QString(), Helper::OUTLINE_PANEL_WIDTH);
    PDFAppToolPanelPopup* optimizePanel = builder.addMenu(QStringLiteral("optimize"), QStringLiteral("minimize-2"), QString(), Helper::OPTIMIZE_PANEL_WIDTH);
    m_panels[QStringLiteral("outline")] = outlinePanel;
    m_panels[QStringLiteral("optimize")] = optimizePanel;
    m_actions[QStringLiteral("outline")] = outlinePanel->getButton();
    m_actions[QStringLiteral("optimize")] = optimizePanel->getButton();
    for (const OptionRow& optionRow : outlineRows)
    {
        PDFTileRow* row = outlinePanel->addRow(optionRow.title, optionRow.description, optionRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("mergeOutline-") + QString::fromLatin1(optionRow.option));
        row->setProperty(Helper::ROW_KEY, QString::fromLatin1(optionRow.option));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppMergeToolGUI::onOutlineRowClicked);
        m_panelRows[QStringLiteral("outline-") + QString::fromLatin1(optionRow.option)] = row;
    }
    for (const OptionRow& optionRow : optimizeRows)
    {
        PDFTileRow* row = optimizePanel->addRow(optionRow.title, optionRow.description, optionRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("mergeOptimize-") + QString::fromLatin1(optionRow.option));
        row->setProperty(Helper::ROW_KEY, QString::fromLatin1(optionRow.option));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppMergeToolGUI::onOptimizeRowClicked);
        m_panelRows[QStringLiteral("optimize-") + QString::fromLatin1(optionRow.option)] = row;
    }

    // The keys of the frames (Ctrl+I, Ctrl+V) on the key widget of the host: they reach every
    // part of the tab
    if (getHost() && getHost()->getKeyWidget() && !m_addFilesAction)
    {
        m_keyWidget = getHost()->getKeyWidget();
        m_addFilesAction = new QAction(m_keyWidget);
        m_addFilesAction->setObjectName(QStringLiteral("mergeAddFilesAction"));
        m_addFilesAction->setShortcut(Helper::getAddFilesKeys());
        m_addFilesAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_addFilesAction.data(), &QAction::triggered, this, &PDFAppMergeToolGUI::addFilesFromDialog);
        m_keyWidget->addAction(m_addFilesAction);
        m_pasteAction = new QAction(m_keyWidget);
        m_pasteAction->setObjectName(QStringLiteral("mergePasteAction"));
        m_pasteAction->setShortcut(Helper::getPasteKeys());
        m_pasteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_pasteAction.data(), &QAction::triggered, this, &PDFAppMergeToolGUI::pasteImage);
        m_keyWidget->addAction(m_pasteAction);

        // A drop of files anywhere in the tab adds them (378:1657 "Drop them anywhere in this window")
        m_wasDropAccepted = m_keyWidget->acceptDrops();
        m_keyWidget->setAcceptDrops(true);
        m_keyWidget->installEventFilter(this);
    }

    updateToolbar();
    return widget;
}

QWidget* PDFAppMergeToolGUI::createLeftPanel(QWidget* parent)
{
    connectList();
    m_filesPanel = new PDFAppToolInputFilesPanel(parent);
    connect(m_filesPanel.data(), &PDFAppToolInputFilesPanel::rowClicked, this, &PDFAppMergeToolGUI::onFileRowClicked);
    connect(m_filesPanel.data(), &PDFAppToolInputFilesPanel::linkActivated, this, &PDFAppMergeToolGUI::onFileLinkActivated);
    connect(m_filesPanel.data(), &PDFAppToolInputFilesPanel::removeRequested, this, &PDFAppMergeToolGUI::removeInput);
    connect(m_filesPanel.data(), &PDFAppToolInputFilesPanel::moveRequested, this, &PDFAppMergeToolGUI::moveInput);
    updateFiles();
    return m_filesPanel.data();
}

QWidget* PDFAppMergeToolGUI::createRightPanel(QWidget* parent)
{
    connectList();

    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("toolRightContent"));
    PDFAppToolPanelBuilder builder(this, widget, Qt::Vertical, getRightPanelSpacing());

    // 378:2613 / 1390:34233: the files, which need attention
    m_attentionCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), QString(), QString());
    m_attentionCallout->setObjectName(QStringLiteral("mergeAttention"));
    // callout-red 378:3015: radius 8, gap 4, the text Regular 11 (the compact callout)
    m_attentionCallout->setCompactStyle();

    // section-Selected page 378:1625: the header with the Tag "Page 3 of 16", Source, Size, Rotation
    m_selectedSection = new QWidget(widget);
    m_selectedSection->setObjectName(QStringLiteral("mergeSelectedSection"));
    QVBoxLayout* selectedLayout = new QVBoxLayout(m_selectedSection);
    selectedLayout->setContentsMargins(0, 0, 0, 0);
    selectedLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    QWidget* selectedHeader = new QWidget(m_selectedSection);
    QHBoxLayout* selectedHeaderLayout = new QHBoxLayout(selectedHeader);
    selectedHeaderLayout->setContentsMargins(0, 0, 0, 0);
    selectedHeaderLayout->setSpacing(8);
    PDFLabel* selectedTitle = PDFAppPageWidgets::createLabel(tr("Selected page"), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, selectedHeader);
    selectedHeaderLayout->addWidget(selectedTitle);
    m_selectedTag = new PDFTag(selectedHeader);
    m_selectedTag->setObjectName(QStringLiteral("mergeSelectedTag"));
    m_selectedTag->setFocusPolicy(Qt::NoFocus);
    m_selectedTag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    selectedHeaderLayout->addWidget(m_selectedTag);
    selectedHeaderLayout->addStretch(1);
    selectedLayout->addWidget(selectedHeader);
    PDFAppToolCard* selectedCard = new PDFAppToolCard(m_selectedSection);
    m_selectedValues = new PDFKeyValueList(selectedCard);
    m_selectedValues->setObjectName(QStringLiteral("mergeSelectedValues"));
    // The rows of the cards of Merge 8 apart (378:1630, 378:1643: a pitch of 36)
    m_selectedValues->setRowGap(PDFAppMergeToolGUIHelper::KEY_VALUE_ROW_GAP);
    m_selectedValues->addRow(tr("Source"), QString(), PDFComponentPainter::TextKind::Data);
    m_selectedValues->addRow(tr("Size"), QString());
    m_selectedValues->addRow(tr("Rotation"), QString());
    selectedCard->getBodyLayout()->addWidget(m_selectedValues);
    selectedLayout->addWidget(selectedCard);
    builder.addWidget(m_selectedSection);

    // section-Result 378:1640: Pages, Outline, Images, Estimated size
    m_resultSection = new QWidget(widget);
    m_resultSection->setObjectName(QStringLiteral("mergeResultSection"));
    QVBoxLayout* resultLayout = new QVBoxLayout(m_resultSection);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    resultLayout->addWidget(new PDFAppToolSectionHeader(tr("Result"), QString(), m_resultSection));
    PDFAppToolCard* resultCard = new PDFAppToolCard(m_resultSection);
    m_resultValues = new PDFKeyValueList(resultCard);
    m_resultValues->setObjectName(QStringLiteral("mergeResultValues"));
    m_resultValues->setRowGap(PDFAppMergeToolGUIHelper::KEY_VALUE_ROW_GAP);
    m_resultValues->addRow(tr("Pages"), QString());
    m_resultValues->addRow(tr("Outline"), QString());
    m_resultValues->addRow(tr("Images"), QString());
    m_resultValues->addRow(tr("Estimated size"), QString());
    resultCard->getBodyLayout()->addWidget(m_resultValues);
    resultLayout->addWidget(resultCard);
    builder.addWidget(m_resultSection);
    // 378:1656: Regular 11 text/muted (the hint of the builder)
    m_outlineNote = builder.addHint(QString());
    m_outlineNote->setObjectName(QStringLiteral("mergeOutlineNote"));

    // section-How Merge works 378:1874 (no files)
    m_howSection = new QWidget(widget);
    m_howSection->setObjectName(QStringLiteral("mergeHowSection"));
    QVBoxLayout* howLayout = new QVBoxLayout(m_howSection);
    howLayout->setContentsMargins(0, 0, 0, 0);
    howLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    howLayout->addWidget(new PDFAppToolSectionHeader(tr("How Merge works"), QString(), m_howSection));
    PDFAppToolCard* howCard = new PDFAppToolCard(m_howSection);
    howCard->getBodyLayout()->setContentsMargins(PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H, PDFAppToolCard::PADDING_H);
    PDFAppParagraph* howText = PDFAppPageWidgets::createText(tr("1. Add PDF files, images or open documents.\n2. Put the files and pages in order.\n3. Create PDF – the result opens as a new document, the source files are never changed."),
                                                             PDFThemeFonts::getFont(QFont::Normal, 12), 15, PDFThemeColors::TextSecondary, howCard);
    howText->setObjectName(QStringLiteral("mergeHowText"));
    howCard->getBodyLayout()->addWidget(howText);
    howLayout->addWidget(howCard);
    builder.addWidget(m_howSection);

    updateRightPanel();
    return widget;
}

QWidget* PDFAppMergeToolGUI::createCanvasPage(QWidget* parent)
{
    connectList();
    if (!m_isTileSizeRead)
    {
        m_isTileSizeRead = true;
        m_tileSize = PDFAppToolTileSizeMenu::readSize(getContext());
    }

    // The canvas of the pages (378:1082); it lives with the parts of the shell
    m_canvas = new PDFAppToolMergeCanvas(parent);
    m_canvas->getModel()->setInputList(m_list);
    m_canvas->setTileSize(m_tileSize);
    m_canvas->setDetailsShown(m_isDetailsShown);
    connect(m_canvas.data(), &PDFAppToolMergeCanvas::viewSwitched, this, &PDFAppMergeToolGUI::onViewSwitched);
    connect(m_canvas->getPageView()->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppMergeToolGUI::onSelectionChanged);
    connect(m_canvas->getPageView()->selectionModel(), &QItemSelectionModel::currentChanged, this, &PDFAppMergeToolGUI::onSelectionChanged);
    for (QObject* view : { static_cast<QObject*>(m_canvas->getPageView()), static_cast<QObject*>(m_canvas->getDetailsView()) })
    {
        if (PDFAppToolMergePageView* pageView = qobject_cast<PDFAppToolMergePageView*>(view))
        {
            connect(pageView, &PDFAppToolMergePageView::moveRequested, this, &PDFAppMergeToolGUI::moveSelectionTo);
            connect(pageView, &PDFAppToolMergePageView::moveByRequested, this, &PDFAppMergeToolGUI::moveSelection);
            connect(pageView, &PDFAppToolMergePageView::removeRequested, this, &PDFAppMergeToolGUI::removeSelection);
        }
        if (PDFAppToolMergeDetailsView* detailsView = qobject_cast<PDFAppToolMergeDetailsView*>(view))
        {
            connect(detailsView, &PDFAppToolMergeDetailsView::moveRequested, this, &PDFAppMergeToolGUI::moveSelectionTo);
            connect(detailsView, &PDFAppToolMergeDetailsView::moveByRequested, this, &PDFAppMergeToolGUI::moveSelection);
            connect(detailsView, &PDFAppToolMergeDetailsView::removeRequested, this, &PDFAppMergeToolGUI::removeSelection);
        }
    }

    // Size: Small 1370:34102 in the bar of the preview: its panel opens below the button (the
    // panels of the toolbar open below the toolbar, this one would cover its own button)
    QToolButton* sizeButton = m_canvas->getTileSizeButton();
    PDFAppToolPanelPopup* sizePanel = new PDFAppToolPanelPopup(nullptr, PDFAppToolTileSizeMenu::PANEL_WIDTH, sizeButton);
    sizePanel->setObjectName(QStringLiteral("toolPanel-thumbnailSize"));
    connect(sizeButton, &QToolButton::clicked, this, &PDFAppMergeToolGUI::onTileSizeButtonClicked);
    connect(sizePanel, &PDFAppDocumentPanelPopup::closed, this, &PDFAppMergeToolGUI::onTileSizePanelClosed);
    m_tileSizeMenu = new PDFAppToolTileSizeMenu(sizePanel, tr("Size: %1"), sizePanel);
    m_tileSizeMenu->setSize(m_tileSize);
    connect(m_tileSizeMenu.data(), &PDFAppToolTileSizeMenu::sizeChosen, this, &PDFAppMergeToolGUI::onTileSizeChosen);

    // The reset of the model is said to its observers, which may delete this GUI
    QPointer<PDFAppMergeToolGUI> guard(this);
    QPointer<PDFAppToolMergeCanvas> canvas = m_canvas;
    m_canvas->getModel()->setPlan(m_plan);
    if (guard)
    {
        updateAll();
    }
    return canvas.data();
}

void PDFAppMergeToolGUI::setContentEnabled(bool isEnabled)
{
    // 378:3038: the actions are disabled while the PDF is created; the pages, the files and the
    // panels stay as they are (they only cannot be changed), the card of the progress lies in
    // the right panel
    if (PDFDocumentToolShell* shell = getShell())
    {
        if (QWidget* actions = shell->getToolActions())
        {
            actions->setEnabled(isEnabled);
        }
    }
    updateEditable();
}

// ------------------------------------------------------------- the slots of the parts

void PDFAppMergeToolGUI::onShellStateChanged(PDFDocumentToolShell::State state)
{
    const bool wasDeciding = m_isDeciding;
    m_isDeciding = state == PDFDocumentToolShell::State::Deciding;
    if (wasDeciding && !m_isDeciding && !m_leftOutInputs.isEmpty())
    {
        // Discard result: the list is whole again (378:4376 shows the inputs left out only
        // below its dialog)
        m_leftOutInputs.clear();
        if (m_canvas)
        {
            QPointer<PDFAppMergeToolGUI> guard(this);
            m_canvas->getModel()->setLeftOutInputs(m_leftOutInputs);
            if (!guard)
            {
                return;
            }
        }
    }
    updateAll();
}

void PDFAppMergeToolGUI::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    updateAll();
}

void PDFAppMergeToolGUI::onSelectionChanged()
{
    if (!m_canvas)
    {
        return;
    }

    // The tiles show the selection of the views (the state of the model)
    m_canvas->getModel()->setSelectedRows(getSelectedRows());
    const QModelIndex current = m_canvas->getPageView()->currentIndex();
    const PDFAppToolInput* input = current.isValid() ? m_canvas->getModel()->getInput(current.row()) : nullptr;
    if (input)
    {
        m_currentInput = input->id;
    }
    if (m_filesPanel)
    {
        m_filesPanel->setCurrentInput(m_currentInput);
    }
    updateSelectedPage();
    updateToolbar();
}

void PDFAppMergeToolGUI::onFileRowClicked(const QString& inputId)
{
    m_currentInput = inputId;
    if (m_filesPanel)
    {
        m_filesPanel->setCurrentInput(inputId);
    }
    if (!m_canvas)
    {
        return;
    }

    // The group of the file: its first page is selected, the card comes into the view
    const PDFAppToolMergePageModel* model = m_canvas->getModel();
    const int group = model->findGroupOfInput(inputId);
    if (group >= 0)
    {
        const PDFAppToolMergePageModel::Group& item = model->getGroups()[size_t(group)];
        if (item.rowCount > 0)
        {
            QPointer<PDFAppMergeToolGUI> guard(this);
            selectRows({ item.firstRow });
            if (!guard || !m_canvas)
            {
                return;
            }
        }
        m_canvas->getPageView()->scrollToGroup(group);
    }
}

void PDFAppMergeToolGUI::onFileLinkActivated(const QString& inputId)
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(inputId) : nullptr;
    if (!input)
    {
        return;
    }

    // Enter password… / Enter owner password… opens the dialog, Remove file removes the file
    if (input->state == PDFAppToolInputState::PasswordRequired || input->state == PDFAppToolInputState::Restricted)
    {
        openPasswordDialog(inputId);
    }
    else
    {
        removeInput(inputId);
    }
}

void PDFAppMergeToolGUI::onViewSwitched(bool isDetailsShown)
{
    setDetailsShown(isDetailsShown);
}

void PDFAppMergeToolGUI::onTileSizeChosen(PDFAppToolPageGrid::TileSize size)
{
    setTileSize(size);
}

void PDFAppMergeToolGUI::onTileSizeButtonClicked()
{
    QToolButton* button = m_canvas ? m_canvas->getTileSizeButton() : nullptr;
    PDFAppToolPanelPopup* panel = m_tileSizeMenu ? m_tileSizeMenu->getPanel() : nullptr;
    if (!button || !panel)
    {
        return;
    }

    // The button is checked while its panel is open (Toolbar menu button: Open)
    if (panel->isVisible())
    {
        panel->close();
        return;
    }
    button->setChecked(true);
    panel->popup(button, button->mapToGlobal(QPoint(0, button->height())).y() + PDFAppToolToolbar::PANEL_GAP);
}

void PDFAppMergeToolGUI::onTileSizePanelClosed()
{
    if (m_canvas && m_canvas->getTileSizeButton())
    {
        m_canvas->getTileSizeButton()->setChecked(false);
    }
}

void PDFAppMergeToolGUI::onProgressTimer()
{
    // The reading of the files: the bars of the rows and of the groups
    if (!m_list || m_list->getLoadingCount() == 0)
    {
        if (m_progressTimer)
        {
            m_progressTimer->stop();
        }
        return;
    }
    QPointer<PDFAppMergeToolGUI> guard(this);
    updateFiles();
    if (guard && m_canvas)
    {
        m_canvas->getModel()->updateGroups();
    }
}

void PDFAppMergeToolGUI::closePanel(const QString& name)
{
    const auto it = m_panels.find(name);
    if (it != m_panels.cend() && it->second)
    {
        it->second->close();
    }
}

void PDFAppMergeToolGUI::onAddFilesRowClicked()
{
    // The panel closes first (it may run code of the application), then the dialog opens
    QPointer<PDFAppMergeToolGUI> guard(this);
    closePanel(QStringLiteral("addFiles"));
    if (guard)
    {
        addFilesFromDialog();
    }
}

void PDFAppMergeToolGUI::onPasteRowClicked()
{
    QPointer<PDFAppMergeToolGUI> guard(this);
    closePanel(QStringLiteral("addFiles"));
    if (guard)
    {
        pasteImage();
    }
}

void PDFAppMergeToolGUI::onOpenDocumentRowClicked()
{
    QObject* row = sender();
    const QString documentId = row ? row->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString() : QString();
    QPointer<PDFAppMergeToolGUI> guard(this);
    closePanel(QStringLiteral("addFiles"));
    if (guard && !documentId.isEmpty())
    {
        addOpenDocument(documentId);
    }
}

void PDFAppMergeToolGUI::onSortRowClicked()
{
    QObject* row = sender();
    const QString key = row ? row->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString() : QString();
    QPointer<PDFAppMergeToolGUI> guard(this);
    closePanel(QStringLiteral("sort"));
    if (!guard)
    {
        return;
    }

    if (key == QLatin1String("name"))
    {
        sortByName();
    }
    else if (key == QLatin1String("type"))
    {
        sortByType();
    }
    else if (key == QLatin1String("page"))
    {
        sortByPageNumber();
    }
    else if (key == QLatin1String("reverse"))
    {
        reverseOrder();
    }
}

void PDFAppMergeToolGUI::onOutlineRowClicked()
{
    QObject* row = sender();
    const QString option = row ? row->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString() : QString();
    QPointer<PDFAppMergeToolGUI> guard(this);
    closePanel(QStringLiteral("outline"));
    if (guard && !option.isEmpty())
    {
        setParameter(PDFDocumentToolMerge::PARAMETER_OUTLINE, option);
    }
}

void PDFAppMergeToolGUI::onOptimizeRowClicked()
{
    QObject* row = sender();
    const QString option = row ? row->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString() : QString();
    QPointer<PDFAppMergeToolGUI> guard(this);
    closePanel(QStringLiteral("optimize"));
    if (guard && !option.isEmpty())
    {
        setParameter(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES, option);
    }
}

void PDFAppMergeToolGUI::onEmptyStateAddFiles()
{
    addFilesFromDialog();
}

void PDFAppMergeToolGUI::onEmptyStateAddDocument(const QString& documentId)
{
    addOpenDocument(documentId);
}

void PDFAppMergeToolGUI::onRegistryChanged()
{
    // A document opened, changed or closed: the rows of the open documents (the Tag open tab)
    QPointer<PDFAppMergeToolGUI> guard(this);
    updateOpenDocumentsRows();
    if (guard)
    {
        updateFiles();
    }
    if (guard)
    {
        updateEmptyState();
    }
}

bool PDFAppMergeToolGUI::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_keyWidget)
    {
        switch (event->type())
        {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }

            case QEvent::Drop:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    // The data of the drop are read now; the files are added from the event
                    // handler, the last thing (a slot may close the tab)
                    dropEvent->acceptProposedAction();
                    dropFiles(dropEvent->mimeData());
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------- update of the parts

bool PDFAppMergeToolGUI::isEditable() const
{
    return !m_isRunning && !m_isDeciding;
}

bool PDFAppMergeToolGUI::isTabOpen(const QString& documentId) const
{
    const QWidget* widget = m_canvas ? static_cast<QWidget*>(m_canvas.data()) : static_cast<QWidget*>(m_filesPanel.data());
    const PDFAppMainWindow* window = widget ? PDFAppPanelWidgets::findMainWindow(widget) : nullptr;
    return window && !documentId.isEmpty() && window->findDocumentTab(documentId);
}

QString PDFAppMergeToolGUI::getParameter(const char* name) const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(PDFAppMergeToolGUIHelper::name(name)).toString() : QString();
}

void PDFAppMergeToolGUI::setParameter(const char* name, const QString& value)
{
    PDFAppToolParameterModel* model = getModel();
    if (model && isEditable())
    {
        const QString error = model->setValue(PDFAppMergeToolGUIHelper::name(name), value);
        if (!error.isEmpty() && getHost())
        {
            getHost()->showTransientMessage(error);
        }
    }
}

void PDFAppMergeToolGUI::updateAll()
{
    // The parts announce their changes (the models to their views and observers), whose
    // slots may delete this GUI (the tab closes from them): each part only by a living GUI
    QPointer<PDFAppMergeToolGUI> guard(this);
    updateOpenDocumentsRows();
    if (guard)
    {
        updateFiles();
    }
    if (guard)
    {
        updateCanvas();
    }
    if (guard)
    {
        updateRightPanel();
    }
    if (guard)
    {
        updateToolbar();
    }
    if (guard)
    {
        updateEditable();
    }
    if (!guard)
    {
        return;
    }

    if (m_list && m_list->getLoadingCount() > 0 && m_progressTimer && !m_progressTimer->isActive())
    {
        m_progressTimer->start();
    }

    // The slots of the state of the Primary button and of the status may delete this GUI (the
    // tab closes from them): each is said only by a living GUI, the last things
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
    if (guard)
    {
        updateOwnTabState();
    }
}

PDFAppToolInputRow::Content PDFAppMergeToolGUI::createRowContent(const PDFAppToolInput& input, int index) const
{
    using Helper = PDFAppMergeToolGUIHelper;

    PDFAppToolInputRow::Content content;
    content.inputId = input.id;
    content.letter = PDFAppToolInputList::getLetter(index);
    content.hue = PDFAppToolInputList::getHue(index, &input);
    content.name = input.name;
    content.toolTip = input.reason;
    const QString size = input.fileSize > 0 ? PDFDocumentToolFormat::formatSize(input.fileSize) : QString();

    switch (input.state)
    {
        case PDFAppToolInputState::Loading:
        {
            // 1390:34124: "Reading the file – 50 %", "24.1 of 48.2 MB" and the bar
            const int percentage = m_list ? m_list->getLoadingProgress(input.id) : -1;
            content.firstLine = percentage >= 0 ? tr("Reading the file – %1 %").arg(percentage) : tr("Reading the file");
            if (input.fileSize > 0 && percentage >= 0)
            {
                // One unit at the end, the size as formatSize writes it (1 decimal, powers of 1024)
                const qint64 readBytes = qint64(double(input.fileSize) * qBound(0, percentage, 100) / 100.0);
                content.secondLine = PDFAppLocale::formatDataSizeOfTotal(readBytes, input.fileSize, 1, QLocale::DataSizeTraditionalFormat);
            }
            content.progress = percentage >= 0 ? percentage / 100.0 : 0.0;
            break;
        }

        case PDFAppToolInputState::Ready:
        {
            QStringList parts;
            const int pageCount = Helper::toInt(input.pageCount);
            if (input.isImage())
            {
                // "Image · 2480 × 3508 px · 1.2 MB" (378:1221)
                parts << (pageCount > 1 ? tr("Image · %1").arg(Helper::pages(pageCount)) : tr("Image"));
                if (input.imageSize.isValid())
                {
                    parts << tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(input.imageSize.width()), PDFAppLocale::formatNumber(input.imageSize.height()));
                }
            }
            else
            {
                // "6 pages · A4 · 2.1 MB" (378:1203): the size of the first page by its name
                parts << Helper::pages(pageCount);
                const QString pageSize = PDFAppToolMergePageModel::getSizeText(input.firstPageSize, true);
                const qsizetype separator = pageSize.indexOf(QStringLiteral(" · "));
                if (!pageSize.isEmpty())
                {
                    parts << (separator > 0 ? pageSize.left(separator) : pageSize);
                }
            }
            if (!size.isEmpty())
            {
                parts << size;
            }
            content.firstLine = parts.join(QStringLiteral(" · "));
            content.secondLine = m_leftOutInputs.contains(input.id) ? tr("Left out – could not be copied") : getRunInResultText(input.id);
            if (input.kind == PDFAppToolInputKind::OpenTab && isTabOpen(input.documentId))
            {
                content.tag = tr("open tab");
            }
            else if (input.isRepaired)
            {
                content.tag = tr("Repaired");
            }
            break;
        }

        case PDFAppToolInputState::PasswordRequired:
            content.firstLine = PDFAppToolInputList::getStateText(input.state);
            content.isFirstLineProblem = true;
            content.secondLine = input.isPasswordWrong ? tr("The password was wrong") : tr("The pages cannot be read yet");
            content.linkText = tr("Enter password…");
            break;

        case PDFAppToolInputState::Restricted:
        {
            content.firstLine = PDFAppToolInputList::getStateText(input.state);
            content.isFirstLineProblem = true;
            const QString restriction = PDFAppToolMergePageModel::getRestrictionText(input.document.data());
            content.secondLine = restriction.isEmpty() ? input.reason : restriction;
            content.linkText = tr("Enter owner password…");
            break;
        }

        case PDFAppToolInputState::Damaged:
            // 378:2809: the name of a file, which adds no page, is text/tertiary
            content.firstLine = PDFAppToolInputList::getStateText(input.state);
            content.isFirstLineProblem = true;
            content.isNameMuted = true;
            content.secondLine = tr("Remove it to continue");
            content.linkText = tr("Remove file");
            break;

        case PDFAppToolInputState::Unsupported:
            // 1390:34260 "Unsupported – not a PDF file or an image"; the whole reason in the tooltip
            if (input.reason == PDFAppToolInputLoader::tr("The file is neither a PDF document nor an image, which can be read."))
            {
                content.firstLine = tr("Unsupported – not a PDF file or an image");
            }
            else
            {
                content.firstLine = input.reason.isEmpty() ? tr("Unsupported") : tr("Unsupported – %1").arg(Helper::toClause(input.reason));
            }
            content.isFirstLineProblem = true;
            content.isNameMuted = true;
            content.secondLine = tr("Remove it to continue");
            content.linkText = tr("Remove file");
            break;

        case PDFAppToolInputState::NoPages:
            // 1390:34269: neutral (text/secondary), it does not block Create PDF; the name is
            // text/tertiary (1390:34268)
            content.firstLine = tr("No pages – nothing is added from this file");
            content.isFirstLineNotice = true;
            content.isNameMuted = true;
            content.secondLine = tr("It does not count as one of the two files");
            content.linkText = tr("Remove file");
            break;
    }
    return content;
}

QString PDFAppMergeToolGUI::getRunInResultText(const QString& inputId) const
{
    // "Pages 1–6 of the result", "Page 16 of the result"; the pages of more runs are counted
    if (!m_canvas)
    {
        return QString();
    }

    const PDFAppToolMergePageModel* model = m_canvas->getModel();
    int runs = 0;
    int pages = 0;
    int firstRow = 0;
    int rowCount = 0;
    for (const PDFAppToolMergePageModel::Group& group : model->getGroups())
    {
        if (group.inputId == inputId && group.rowCount > 0)
        {
            ++runs;
            pages += group.rowCount;
            firstRow = group.firstRow;
            rowCount = group.rowCount;
        }
    }

    if (runs == 0)
    {
        return tr("Not in the result");
    }
    if (runs > 1)
    {
        return tr("%1 in the result").arg(PDFAppMergeToolGUIHelper::pages(pages));
    }
    return rowCount == 1 ? tr("Page %1 of the result").arg(PDFAppLocale::formatNumber(firstRow + 1))
                         : tr("Pages %1–%2 of the result").arg(PDFAppLocale::formatNumber(firstRow + 1), PDFAppLocale::formatNumber(qint64(firstRow) + rowCount));
}

void PDFAppMergeToolGUI::updateFiles()
{
    if (!m_filesPanel)
    {
        return;
    }

    const int count = m_list ? m_list->getCount() : 0;
    QString subtitle;
    if (count == 0)
    {
        subtitle = tr("No files yet");
    }
    else if (!m_leftOutInputs.isEmpty())
    {
        subtitle = tr("%1 · %2").arg(PDFAppMergeToolGUIHelper::files(count), tr("%Ln left out", nullptr, int(m_leftOutInputs.size())));
    }
    else if (m_list->getAttentionCount() > 0)
    {
        subtitle = tr("%1 · %2").arg(PDFAppMergeToolGUIHelper::files(count), m_list->getAttentionCount() == 1 ? tr("1 needs attention") : tr("%Ln need attention", nullptr, m_list->getAttentionCount()));
    }
    else
    {
        // "3 files · 16 pages · 18.6 MB" (378:1196)
        subtitle = tr("%1 · %2 · %3").arg(PDFAppMergeToolGUIHelper::files(count), PDFAppMergeToolGUIHelper::pages(PDFAppMergeToolGUIHelper::toInt(m_plan.getPageCount())),
                                          PDFDocumentToolFormat::formatSize(m_list->getTotalSize()));
    }
    m_filesPanel->setHeader(tr("Files"), subtitle);
    m_filesPanel->setHint(count == 0 ? tr("The files you add are listed here in the order in which they are merged.")
                                     : tr("Drag a file to change the order of the files. Drag pages in the preview to move single pages. Drop more files anywhere in the window."));

    std::vector<PDFAppToolInputRow::Content> rows;
    rows.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
    {
        rows.push_back(createRowContent(*m_list->getInput(i), i));
    }
    QPointer<PDFAppMergeToolGUI> guard(this);
    m_filesPanel->setRows(rows);
    if (guard && m_filesPanel)
    {
        m_filesPanel->setCurrentInput(m_currentInput);
    }
}

void PDFAppMergeToolGUI::updateCanvas()
{
    if (!m_canvas)
    {
        return;
    }

    // The model says the new groups to its views and observers, which may delete this GUI
    QPointer<PDFAppMergeToolGUI> guard(this);
    PDFAppToolMergePageModel* model = m_canvas->getModel();
    model->setLeftOutInputs(m_leftOutInputs);
    if (!guard || !m_canvas)
    {
        return;
    }
    model->updateGroups();
    if (!guard || !m_canvas)
    {
        return;
    }

    // 378:1224 "Result · 16 pages"; with files, which cannot be used yet, the pages, which can
    // (378:2613 "7 pages can be read", 1390:34070 "9 pages can be used")
    const int pageCount = PDFAppMergeToolGUIHelper::toInt(m_plan.getPageCount());
    bool hasUnreadFile = false;
    bool hasProblem = false;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        hasUnreadFile = hasUnreadFile || input->state == PDFAppToolInputState::PasswordRequired || input->state == PDFAppToolInputState::Damaged;
        hasProblem = hasProblem || input->needsAttention() || input->state == PDFAppToolInputState::Loading;
    }
    if (!hasProblem)
    {
        m_canvas->setHeaderText(tr("Result · %1").arg(PDFAppMergeToolGUIHelper::pages(pageCount)));
    }
    else
    {
        const QString pages = PDFAppMergeToolGUIHelper::pages(pageCount);
        m_canvas->setHeaderText(hasUnreadFile ? tr("Result · %1 can be read").arg(pages) : tr("Result · %1 can be used").arg(pages));
    }

    updateEmptyState();
}

std::vector<PDFAppDocument*> PDFAppMergeToolGUI::getOpenAppDocuments() const
{
    // F13: the ready documents of the tabs of this window
    std::vector<PDFAppDocument*> documents;
    const PDFAppDocumentRegistry* registry = getContext() ? getContext()->getDocumentRegistry() : nullptr;
    const QWidget* widget = m_canvas ? static_cast<QWidget*>(m_canvas.data()) : static_cast<QWidget*>(m_filesPanel.data());
    const PDFAppMainWindow* window = widget ? PDFAppPanelWidgets::findMainWindow(widget) : nullptr;
    if (registry)
    {
        for (PDFAppDocument* document : registry->getDocuments())
        {
            if (document->isReady() && (!window || window->findDocumentTab(document->getId())))
            {
                documents.push_back(document);
            }
        }
    }
    return documents;
}

std::vector<PDFAppToolMergeEmptyState::Document> PDFAppMergeToolGUI::getOpenDocuments() const
{
    std::vector<PDFAppToolMergeEmptyState::Document> documents;
    for (const PDFAppDocument* document : getOpenAppDocuments())
    {
        PDFAppToolMergeEmptyState::Document item;
        item.documentId = document->getId();
        item.name = document->getTitle();
        item.isModified = document->isModified();
        const int pageCount = PDFAppMergeToolGUIHelper::toInt(document->getPageCount());
        item.details = item.isModified ? tr("%1 · the current unsaved state is used").arg(PDFAppMergeToolGUIHelper::pages(pageCount)) : PDFAppToolDocumentChooser::createNumbers(document);
        documents.push_back(item);
    }
    return documents;
}

void PDFAppMergeToolGUI::updateEmptyState()
{
    if (!m_canvas)
    {
        return;
    }

    // 378:1657: without files the drop zone with the open documents takes the stage
    const bool isEmpty = !m_list || m_list->getCount() == 0;
    PDFAppToolMergeEmptyState* emptyState = getEmptyState();
    if (!isEmpty)
    {
        if (emptyState)
        {
            m_canvas->setEmptyState(nullptr);
        }
        return;
    }

    const std::vector<PDFAppToolMergeEmptyState::Document> documents = getOpenDocuments();
    std::vector<QString> ids;
    for (const PDFAppToolMergeEmptyState::Document& document : documents)
    {
        ids.push_back(document.documentId + (document.isModified ? QStringLiteral("*") : QString()));
    }
    const QString signature = QStringList(ids.cbegin(), ids.cend()).join(QLatin1Char(','));
    if (emptyState && emptyState->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString() == signature)
    {
        return;
    }

    PDFAppToolMergeEmptyState* newState = new PDFAppToolMergeEmptyState(documents, m_canvas);
    newState->setProperty(PDFAppMergeToolGUIHelper::ROW_KEY, signature);
    connect(newState, &PDFAppToolMergeEmptyState::addFilesRequested, this, &PDFAppMergeToolGUI::onEmptyStateAddFiles);
    connect(newState, &PDFAppToolMergeEmptyState::addDocumentRequested, this, &PDFAppMergeToolGUI::onEmptyStateAddDocument);
    m_canvas->setEmptyState(newState);
}

PDFAppToolMergeEmptyState* PDFAppMergeToolGUI::getEmptyState() const
{
    return m_canvas ? qobject_cast<PDFAppToolMergeEmptyState*>(m_canvas->getEmptyState()) : nullptr;
}

void PDFAppMergeToolGUI::updateOpenDocumentsRows()
{
    using Helper = PDFAppMergeToolGUIHelper;

    const auto it = m_panels.find(QStringLiteral("addFiles"));
    PDFAppToolPanelPopup* panel = it != m_panels.cend() ? it->second.data() : nullptr;
    if (!panel)
    {
        return;
    }

    // "Open documents" (378:2516): a row of each ready document of the window; a document in
    // the list is disabled ("already in the list", K29), an unsaved one uses its state now
    const std::vector<PDFAppDocument*> documents = getOpenAppDocuments();
    if (!m_openDocumentsCaption && !documents.empty())
    {
        panel->addGroupDivider();
        m_openDocumentsCaption = panel->addGroupCaption(tr("Open documents"));
    }
    for (size_t i = 0; i < documents.size(); ++i)
    {
        PDFAppDocument* document = documents[i];
        if (i >= m_openDocumentRows.size() || !m_openDocumentRows[i])
        {
            PDFTileRow* row = panel->addRow(QString(), QString(), QStringLiteral("tab"), PDFTileRow::Kind::MenuItem);
            connect(row, &PDFTileRow::clicked, this, &PDFAppMergeToolGUI::onOpenDocumentRowClicked);
            if (i < m_openDocumentRows.size())
            {
                m_openDocumentRows[i] = row;
            }
            else
            {
                m_openDocumentRows.push_back(row);
            }
        }

        PDFTileRow* row = m_openDocumentRows[i].data();
        const bool isInList = m_list && m_list->containsDocument(document->getId());
        const QString pages = Helper::pages(Helper::toInt(document->getPageCount()));
        row->setObjectName(QStringLiteral("mergeOpenDocument-") + document->getId());
        row->setProperty(Helper::ROW_KEY, document->getId());
        row->setTitle(document->getTitle());
        row->setTitleKind(PDFComponentPainter::TextKind::FileName);
        row->setDescription(isInList ? tr("%1 · already in the list").arg(pages) : (document->isModified() ? tr("%1 · unsaved state is used").arg(pages) : pages));
        row->setEnabled(!isInList);
        row->show();
    }
    for (size_t i = documents.size(); i < m_openDocumentRows.size(); ++i)
    {
        if (m_openDocumentRows[i])
        {
            m_openDocumentRows[i]->hide();
        }
    }
    if (m_openDocumentsCaption)
    {
        m_openDocumentsCaption->setVisible(!documents.empty());
    }
}

QString PDFAppMergeToolGUI::getOutlineValue() const
{
    // "3 document parts" (378:1647): a part of each run of the pages of a file (K14)
    const QString outline = getParameter(PDFDocumentToolMerge::PARAMETER_OUTLINE);
    int parts = 0;
    if (m_canvas)
    {
        for (const PDFAppToolMergePageModel::Group& group : m_canvas->getModel()->getGroups())
        {
            parts += group.rowCount > 0 ? 1 : 0;
        }
    }
    if (outline == QLatin1String(PDFDocumentToolMerge::OUTLINE_NONE))
    {
        return tr("None");
    }
    if (outline == QLatin1String(PDFDocumentToolMerge::OUTLINE_JOIN))
    {
        return parts == 1 ? tr("1 document part with its bookmarks") : tr("%Ln document parts with their bookmarks", nullptr, parts);
    }
    return parts == 1 ? tr("1 document part") : tr("%Ln document parts", nullptr, parts);
}

QString PDFAppMergeToolGUI::getOptimizeValue() const
{
    const QString optimize = getParameter(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES);
    if (optimize == QLatin1String(PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY))
    {
        return tr("Optimized, quality kept");
    }
    if (optimize == QLatin1String(PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE))
    {
        return tr("Optimized for the smallest file");
    }
    return tr("Stored as they are");
}

QString PDFAppMergeToolGUI::getOutlineNoteText() const
{
    // 378:1656 by the mode of the outline
    const QString outline = getParameter(PDFDocumentToolMerge::PARAMETER_OUTLINE);
    if (outline == QLatin1String(PDFDocumentToolMerge::OUTLINE_NONE))
    {
        return tr("No outline: the result has no bookmarks. The source files are never changed.");
    }
    if (outline == QLatin1String(PDFDocumentToolMerge::OUTLINE_JOIN))
    {
        return tr("Join outlines: the bookmarks of each file are nested under its bookmark. The source files are never changed.");
    }
    return tr("Document parts: the result gets one bookmark per source file. The source files are never changed.");
}

QString PDFAppMergeToolGUI::getAttentionText() const
{
    using Helper = PDFAppMergeToolGUIHelper;

    // 378:2613 / 1390:34237: what makes Create PDF available, by the names of the files
    QStringList passwords;
    QStringList ownerPasswords;
    QStringList removals;
    QStringList readings;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        switch (input->state)
        {
            case PDFAppToolInputState::PasswordRequired:
                passwords << input->name;
                break;
            case PDFAppToolInputState::Restricted:
                ownerPasswords << input->name;
                break;
            case PDFAppToolInputState::Damaged:
            case PDFAppToolInputState::Unsupported:
                removals << input->name;
                break;
            case PDFAppToolInputState::Loading:
                readings << input->name;
                break;
            case PDFAppToolInputState::Ready:
            case PDFAppToolInputState::NoPages:
                break;
        }
    }

    QStringList actions;
    if (!passwords.isEmpty())
    {
        actions << tr("enter the password of %1").arg(Helper::joinNames(passwords));
    }
    if (!ownerPasswords.isEmpty())
    {
        actions << tr("enter the owner password of %1").arg(Helper::joinNames(ownerPasswords));
    }
    if (!removals.isEmpty())
    {
        actions << tr("remove %1").arg(Helper::joinNames(removals));
    }

    QString text = tr("Create PDF is available when every file in the list can be used.");
    if (!actions.isEmpty())
    {
        QString sentence = actions.join(tr(" and "));
        sentence[0] = sentence[0].toUpper();
        text += QLatin1Char(' ') + sentence;
        text += readings.isEmpty() ? QStringLiteral(".") : QString();
    }
    if (!readings.isEmpty())
    {
        text += actions.isEmpty() ? QStringLiteral(" ") : QStringLiteral("; ");
        text += readings.size() == 1 ? tr("%1 is still being read.").arg(readings.front()) : tr("%1 are still being read.").arg(Helper::joinNames(readings));
    }
    return text;
}

void PDFAppMergeToolGUI::updateRightPanel()
{
    const bool isEmpty = !m_list || m_list->getCount() == 0;
    const int attention = m_list ? m_list->getAttentionCount() : 0;
    if (m_attentionCallout)
    {
        m_attentionCallout->setVisible(attention > 0);
        if (attention > 0)
        {
            m_attentionCallout->setTexts(attention == 1 ? tr("1 file needs attention") : tr("%Ln files need attention", nullptr, attention), getAttentionText());
        }
    }
    if (m_howSection)
    {
        m_howSection->setVisible(isEmpty);
    }
    if (m_resultSection)
    {
        m_resultSection->setVisible(!isEmpty);
    }
    if (m_outlineNote)
    {
        m_outlineNote->setVisible(!isEmpty);
        m_outlineNote->setText(getOutlineNoteText());
    }

    if (m_resultValues && m_resultValues->getRowCount() == 4)
    {
        // "16 from 3 files", "3 document parts", "Stored as they are", "about 18.6 MB" (378:1643);
        // K23: the size of optimized images is not known before the run
        const int pageCount = PDFAppMergeToolGUIHelper::toInt(m_plan.getPageCount());
        const int fileCount = m_list ? m_list->getUsableCount() : 0;
        m_resultValues->setValue(0, tr("%1 from %2").arg(PDFAppLocale::formatNumber(pageCount), PDFAppMergeToolGUIHelper::files(fileCount)));
        m_resultValues->setValue(1, getOutlineValue());
        m_resultValues->setValue(2, getOptimizeValue());
        const bool isOptimized = getParameter(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES) != QLatin1String(PDFDocumentToolMerge::OPTIMIZE_OFF);
        m_resultValues->setValue(3, isOptimized ? tr("depends on the images") : tr("about %1").arg(PDFDocumentToolFormat::formatSize(m_list ? m_list->getTotalSize() : 0)));
    }
    updateSelectedPage();
}

void PDFAppMergeToolGUI::updateSelectedPage()
{
    if (!m_selectedSection || !m_selectedValues || m_selectedValues->getRowCount() != 3)
    {
        return;
    }

    const QModelIndex current = m_canvas ? m_canvas->getPageView()->currentIndex() : QModelIndex();
    const PDFAppToolMergePageModel* model = m_canvas ? m_canvas->getModel() : nullptr;
    const PDFAppToolMergePlan::Item* item = model && current.isValid() ? model->getItem(current.row()) : nullptr;
    const PDFAppToolInput* input = model && current.isValid() ? model->getInput(current.row()) : nullptr;
    m_selectedSection->setVisible(item && input);
    if (!item || !input)
    {
        return;
    }

    // "Page 3 of 16", "Contract.pdf, p. 3", "A4 · 210 × 297 mm", "None" (378:1625)
    m_selectedTag->setText(tr("Page %1 of %2").arg(PDFAppLocale::formatNumber(qint64(current.row()) + 1), PDFAppLocale::formatNumber(model->rowCount())));
    const QString source = input->isImage() && input->pageCount <= 1 ? input->name : tr("%1, p. %2").arg(input->name, PDFAppLocale::formatNumber(item->pageIndex + 1));
    m_selectedValues->setValue(0, source);
    m_selectedValues->setValue(1, PDFAppToolMergePageModel::getSizeText(model->getPageSize(current.row()), true));
    m_selectedValues->setValue(2, PDFAppToolMergePageModel::getRotationText(item->turn));
}

void PDFAppMergeToolGUI::updateToolbar()
{
    const bool isEditable = this->isEditable();
    const bool hasSelection = !getSelectedRows().empty();
    const bool hasInputs = m_list && m_list->getCount() > 0;
    for (const auto& [name, button] : m_actions)
    {
        if (!button)
        {
            continue;
        }

        // 378:1657: the actions of the pages and Sort need pages and files
        bool isEnabled = isEditable;
        if (name == QLatin1String("rotateLeft") || name == QLatin1String("rotateRight") || name == QLatin1String("clone") || name == QLatin1String("remove"))
        {
            isEnabled = isEnabled && hasSelection;
        }
        else if (name == QLatin1String("sort"))
        {
            isEnabled = isEnabled && hasInputs;
        }
        button->setEnabled(isEnabled);
    }

    // "Outline: Parts", "Optimize: Off" (378:1183, 378:1188) and the checks of the panels
    const QString outline = getParameter(PDFDocumentToolMerge::PARAMETER_OUTLINE);
    const QString optimize = getParameter(PDFDocumentToolMerge::PARAMETER_OPTIMIZE_IMAGES);
    const auto outlineButton = m_actions.find(QStringLiteral("outline"));
    if (outlineButton != m_actions.cend() && outlineButton->second)
    {
        QString value = tr("Parts");
        if (outline == QLatin1String(PDFDocumentToolMerge::OUTLINE_JOIN))
        {
            value = tr("Joined");
        }
        else if (outline == QLatin1String(PDFDocumentToolMerge::OUTLINE_NONE))
        {
            value = tr("None");
        }
        outlineButton->second->setText(tr("Outline: %1").arg(value));
        outlineButton->second->setToolTip(tr("Bookmarks of the result"));
        outlineButton->second->setAccessibleName(outlineButton->second->text());
    }
    const auto optimizeButton = m_actions.find(QStringLiteral("optimize"));
    if (optimizeButton != m_actions.cend() && optimizeButton->second)
    {
        QString value = tr("Off");
        if (optimize == QLatin1String(PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY))
        {
            value = tr("Quality");
        }
        else if (optimize == QLatin1String(PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE))
        {
            value = tr("Minimum");
        }
        optimizeButton->second->setText(tr("Optimize: %1").arg(value));
        optimizeButton->second->setToolTip(tr("Images of the result"));
        optimizeButton->second->setAccessibleName(optimizeButton->second->text());
    }
    for (const auto& [name, row] : m_panelRows)
    {
        if (!row)
        {
            continue;
        }
        if (name.startsWith(QStringLiteral("outline-")))
        {
            row->setChecked(name.mid(8) == outline);
        }
        else if (name.startsWith(QStringLiteral("optimize-")))
        {
            row->setChecked(name.mid(9) == optimize);
        }
    }

    if (m_canvas)
    {
        m_canvas->getTileSizeButton()->setEnabled(isEditable && !m_canvas->isDetailsShown());
    }
}

void PDFAppMergeToolGUI::updateEditable()
{
    const bool isEditable = this->isEditable();
    if (m_canvas)
    {
        m_canvas->setEditable(isEditable);
        if (PDFAppToolMergeEmptyState* emptyState = getEmptyState())
        {
            emptyState->setEnabled(isEditable);
        }
    }
    if (m_filesPanel)
    {
        m_filesPanel->setEditable(isEditable);
    }
}

QToolButton* PDFAppMergeToolGUI::getAction(const QString& name) const
{
    const auto it = m_actions.find(name);
    return it != m_actions.cend() ? it->second.data() : nullptr;
}

PDFAppToolPanelPopup* PDFAppMergeToolGUI::getPanel(const QString& name) const
{
    const auto it = m_panels.find(name);
    return it != m_panels.cend() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppMergeToolGUI::getPanelRow(const QString& name) const
{
    const auto it = m_panelRows.find(name);
    if (it != m_panelRows.cend())
    {
        return it->second.data();
    }
    for (const QPointer<PDFTileRow>& row : m_openDocumentRows)
    {
        if (row && row->property(PDFAppMergeToolGUIHelper::ROW_KEY).toString() == name)
        {
            return row.data();
        }
    }
    return nullptr;
}

// ------------------------------------------------------------- texts

QString PDFAppMergeToolGUI::getConfirmText() const
{
    // btn-confirm 378:1074 "Create PDF"
    return tr("Create PDF");
}

QString PDFAppMergeToolGUI::getConfirmIconName() const
{
    return QStringLiteral("file-plus");
}

bool PDFAppMergeToolGUI::isConfirmEnabled() const
{
    // 378:2613: every file can be read; K22: at least two usable files; a page in the result
    return BaseClass::isConfirmEnabled() && m_list && m_list->isRunnable(PDFDocumentToolMerge::MINIMUM_INPUT_COUNT) &&
           getUsableInputIds().size() >= PDFDocumentToolMerge::MINIMUM_INPUT_COUNT && isEditable();
}

QString PDFAppMergeToolGUI::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    const int count = m_list ? m_list->getCount() : 0;
    if (count == 0)
    {
        // 378:1692
        return tr("%1 · add two or more files to combine them into one PDF").arg(title);
    }

    // 378:2613 / 1390:34248: the files, which cannot be used yet, and the reading
    const int loading = m_list->getLoadingCount();
    const int attention = m_list->getAttentionCount();
    if (loading > 0 || attention > 0)
    {
        QStringList parts{ title };
        if (loading > 0)
        {
            parts << tr("reading %1").arg(PDFAppMergeToolGUIHelper::files(loading));
        }
        if (attention > 0)
        {
            bool hasPassword = false;
            bool hasOwnerPassword = false;
            for (int i = 0; i < count; ++i)
            {
                hasPassword = hasPassword || m_list->getInput(i)->state == PDFAppToolInputState::PasswordRequired;
                hasOwnerPassword = hasOwnerPassword || m_list->getInput(i)->state == PDFAppToolInputState::Restricted;
            }
            const QString action = hasPassword ? tr("enter the password or remove them") : (hasOwnerPassword ? tr("enter the owner password or remove them") : tr("remove them"));
            parts << tr("%1 of %2 cannot be used yet – %3").arg(PDFAppLocale::formatNumber(attention), PDFAppMergeToolGUIHelper::files(count), action);
        }
        return parts.join(QStringLiteral(" · "));
    }

    // K22 (no frame): one more file is needed; the pages of one file are no merge
    if (m_list->getUsableCount() < PDFDocumentToolMerge::MINIMUM_INPUT_COUNT)
    {
        return tr("%1 · add one more file").arg(title);
    }
    if (getUsableInputIds().size() < PDFDocumentToolMerge::MINIMUM_INPUT_COUNT)
    {
        return tr("%1 · the result needs pages of two or more files").arg(title);
    }

    const QString numbers = tr("%1 · %2").arg(PDFAppMergeToolGUIHelper::files(count), PDFAppMergeToolGUIHelper::pages(PDFAppMergeToolGUIHelper::toInt(m_plan.getPageCount())));
    if (m_isDetailsShown)
    {
        // 408:32003
        return tr("%1 · %2 · Details view").arg(title, numbers);
    }
    // 378:1085
    return tr("%1 · %2 · nothing is written until you choose Create PDF").arg(title, numbers);
}

QString PDFAppMergeToolGUI::getRunningStatusText(const QString& stepText) const
{
    Q_UNUSED(stepText);

    // 378:3038
    return tr("Creating the PDF · Stop ends the operation, no file has been written yet");
}

QString PDFAppMergeToolGUI::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(percentage);
    Q_UNUSED(stepText);
    return tr("Creating the PDF");
}

QString PDFAppMergeToolGUI::getRunningCardStepText(const QString& stepText) const
{
    return stepText;
}

QString PDFAppMergeToolGUI::getRunningTimeText(int seconds) const
{
    // 378:3038 has no line of the time left
    Q_UNUSED(seconds);
    return QString();
}

void PDFAppMergeToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 378:4376 (K36 corrected): "Merged without 1 file", the inputs left out are said in the
    // list below the dialog ("Left out – could not be copied")
    const int leftOut = int(qMin<size_t>(input->result.leftOutInputs.size(), size_t(std::numeric_limits<int>::max())));
    if (leftOut > 0)
    {
        input->title = leftOut == 1 ? tr("Merged without %Ln file", nullptr, leftOut) : tr("Merged without %Ln files", nullptr, leftOut);
        input->statusText = leftOut == 1 ? tr("Merge finished without %Ln file · the new document is waiting for your decision", nullptr, leftOut)
                                         : tr("Merge finished without %Ln files · the new document is waiting for your decision", nullptr, leftOut);

        // The list of the GUI shows them while the dialog is open (a const function of the
        // interface: the state is said by a queued update of this GUI)
        PDFAppMergeToolGUI* self = const_cast<PDFAppMergeToolGUI*>(this);
        self->m_leftOutInputs.clear();
        for (const PDFDocumentToolLeftOutInput& leftOutInput : input->result.leftOutInputs)
        {
            self->m_leftOutInputs << leftOutInput.inputId;
        }
        QTimer::singleShot(0, self, &PDFAppMergeToolGUI::updateAll);
    }
}

QString PDFAppMergeToolGUI::getOwnTabLossText() const
{
    // K16: the dot of the tab and the question at its close
    const int count = m_list ? m_list->getCount() : (getInputList() ? getInputList()->getCount() : 0);
    if (count == 1)
    {
        return tr("The list of 1 file is lost.");
    }
    return count > 0 ? tr("The list of %Ln files is lost.", nullptr, count) : QString();
}

QString PDFAppMergeToolGUI::getLockedGlanceText() const
{
    // K9: At a glance of 531:3208
    return tr("Can be merged");
}

QString PDFAppMergeToolGUI::getOwnTabPageText() const
{
    // 378:1086 "16 pages", 378:1694 "No pages"
    const int pageCount = PDFAppMergeToolGUIHelper::toInt(m_plan.getPageCount());
    return pageCount > 0 ? PDFAppMergeToolGUIHelper::pages(pageCount) : tr("No pages");
}

QString PDFAppMergeToolGUI::getOwnTabZoomText() const
{
    // K12: no zoom in the own tab of Merge
    return QString();
}

// ------------------------------------------------------------- flow

QVariantMap PDFAppMergeToolGUI::getRunParameters() const
{
    using Helper = PDFAppMergeToolGUIHelper;

    // The run gets the usable inputs only (R16): their ids and the plan of their pages with
    // their letters among them
    QVariantMap parameters = BaseClass::getRunParameters();
    const QStringList ids = getUsableInputIds();
    parameters.insert(Helper::name(PDFDocumentToolMerge::PARAMETER_INPUTS), ids.join(QLatin1Char(',')));
    parameters.insert(Helper::name(PDFDocumentToolMerge::PARAMETER_PLAN), m_plan.toText(ids, false));
    return parameters;
}

void PDFAppMergeToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    // R16: the inputs of the run (getRunParameters: the usable inputs with a page in the result)
    // are the documents and the images of the context - the core refuses an input of the context,
    // which its order of the inputs does not name. The documents are immutable shared pointers,
    // the bytes are implicitly shared: values.
    const PDFAppToolInputList* list = getInputList();
    const QStringList runInputIds = getUsableInputIds();
    for (int i = 0; list && i < list->getCount(); ++i)
    {
        const PDFAppToolInput* input = list->getInput(i);
        if (!input->isUsable() || !runInputIds.contains(input->id))
        {
            continue;
        }
        if (input->isImage())
        {
            environment->images.push_back(input->createToolInputImage());
        }
        else
        {
            environment->additionalDocuments.push_back(input->createToolInputDocument());
        }
    }
}

void PDFAppMergeToolGUI::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    if (name == QLatin1String(PDFDocumentToolMerge::PARAMETER_INPUTS) || name == QLatin1String(PDFDocumentToolMerge::PARAMETER_PLAN))
    {
        // Both values are set before the first announcement: one reading of both
        readValues();
        return;
    }
    updateAll();
}

}   // namespace pdf

#include "moc_pdfapptoolgui_merge.cpp"
