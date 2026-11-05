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

#include "pdfapptoolgui_pages.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentpropertiesdata.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolparametermodel.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcatalog.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumenttool_pages.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolregistry.h"

#include <QHBoxLayout>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of the tools of the pages
class PDFAppToolGUIPagesHelper
{
public:
    PDFAppToolGUIPagesHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Slots of the pages of the tiles (389:20177: 104 x 104, 388:20614: 82 x 112)
    static constexpr QSize ROTATE_SLOT = QSize(104, 104);
    static constexpr QSize REMOVE_SLOT = QSize(82, 112);

    /// Size of a page in millimeters, as it is shown
    static QSizeF getSizeMM(const PDFPage* page)
    {
        const QSizeF size = page->getRotatedCropBox().size();
        const qreal factor = 25.4 / 72.0 * page->getUserUnit();
        return QSizeF(size.width() * factor, size.height() * factor);
    }
};

/// Registers the tool and its GUI under the id, when they are not registered yet
template<typename Tool, typename GUI>
class PDFAppToolGUIPagesRegistration
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

void PDFAppToolGUIPages::registerTools()
{
    PDFAppToolGUIPagesRegistration<PDFDocumentToolRotatePages, PDFAppToolGUIRotatePages>::registerTool();
    PDFAppToolGUIPagesRegistration<PDFDocumentToolRemovePages, PDFAppToolGUIRemovePages>::registerTool();
}

// ----------------------------------------------------- PDFAppToolPendingPagesModel

PDFAppToolPendingPagesModel::PDFAppToolPendingPagesModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolPendingPagesModel::~PDFAppToolPendingPagesModel() = default;

void PDFAppToolPendingPagesModel::setRows(std::vector<Row> rows)
{
    if (rows == m_rows)
    {
        return;
    }

    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

PDFInteger PDFAppToolPendingPagesModel::getPage(int row) const
{
    return row >= 0 && size_t(row) < m_rows.size() ? m_rows[size_t(row)].page : -1;
}

int PDFAppToolPendingPagesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PDFAppToolPendingPagesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || size_t(index.row()) >= m_rows.size())
    {
        return QVariant();
    }

    const Row& row = m_rows[size_t(index.row())];
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return row.title;
        case PDFComponentRoles::SubtitleRole:
        case Qt::AccessibleDescriptionRole:
            return row.description;
        case PDFComponentRoles::IconNameRole:
            return row.iconName.isEmpty() ? QVariant() : QVariant(row.iconName);
        case PDFComponentRoles::TileTextRole:
            return row.tileText.isEmpty() ? QVariant() : QVariant(row.tileText);
        case PDFComponentRoles::TileToneRole:
            return int(row.tone);
        case PDFComponentRoles::LinkTextRole:
            return row.linkText;
        default:
            return QVariant();
    }
}

// ------------------------------------------------------- PDFAppPageMarkingToolGUI

PDFAppPageMarkingToolGUI::PDFAppPageMarkingToolGUI(QObject* parent) :
    BaseClass(parent)
{
    m_pendingModel = new PDFAppToolPendingPagesModel(this);
}

PDFAppPageMarkingToolGUI::~PDFAppPageMarkingToolGUI() = default;

PDFDocumentPointer PDFAppPageMarkingToolGUI::getShownDocument() const
{
    const PDFAppDocument* document = getDocument();
    return document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
}

const PDFPage* PDFAppPageMarkingToolGUI::getPage(PDFInteger page) const
{
    const PDFDocumentPointer document = getShownDocument();
    if (!document || page < 0 || size_t(page) >= document->getCatalog()->getPageCount())
    {
        return nullptr;
    }
    return document->getCatalog()->getPage(size_t(page));
}

PDFInteger PDFAppPageMarkingToolGUI::getPageCount() const
{
    const PDFDocumentPointer document = getShownDocument();
    return document ? PDFInteger(document->getCatalog()->getPageCount()) : 0;
}

QString PDFAppPageMarkingToolGUI::getOrientationText(const QSizeF& size)
{
    return size.width() > size.height() ? tr("landscape") : tr("portrait");
}

bool PDFAppPageMarkingToolGUI::isTarget(PDFInteger page) const
{
    return page >= 0 && size_t(page) < m_isTarget.size() && m_isTarget[size_t(page)];
}

QString PDFAppPageMarkingToolGUI::getLeftHintText() const
{
    return QString();
}

void PDFAppPageMarkingToolGUI::onDeleteRequested(PDFInteger page)
{
    Q_UNUSED(page);
}

void PDFAppPageMarkingToolGUI::updatePanels()
{

}

void PDFAppPageMarkingToolGUI::onShellStateChanged(PDFDocumentToolShell::State state)
{
    Q_UNUSED(state);
}

void PDFAppPageMarkingToolGUI::onShellStateChangedSlot(PDFDocumentToolShell::State state)
{
    onShellStateChanged(state);
}

QWidget* PDFAppPageMarkingToolGUI::createCanvasPage(QWidget* parent)
{
    // The grid in the place of the canvas (document-canvas 389:20210); it lives with the parts of the shell
    m_grid = new PDFAppToolPageGrid(getSlotSize(), parent);
    connect(m_grid.data(), &PDFAppToolPageGrid::pageClicked, this, &PDFAppPageMarkingToolGUI::onGridPageClicked);
    connect(m_grid.data(), &PDFAppToolPageGrid::deleteRequested, this, &PDFAppPageMarkingToolGUI::onGridDeleteRequested);

    PDFAppDocument* document = getDocument();
    const PDFAppToolHost* host = getHost();
    m_grid->setDocument(getShownDocument(), document ? document->getId() : QString(), host ? host->getView() : nullptr);

    if (PDFDocumentToolShell* shell = getShell(); shell && !m_isShellConnected)
    {
        m_isShellConnected = true;
        connect(shell, &PDFDocumentToolShell::stateChanged, this, &PDFAppPageMarkingToolGUI::onShellStateChangedSlot);
    }

    updateAll();
    return m_grid.data();
}

void PDFAppPageMarkingToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolGUIPagesHelper;

    // page-selection 389:20292 (279 wide)
    m_pageSelection = builder.addPageSelection(Helper::name(PDFDocumentToolRotatePages::PARAMETER_PAGES), getSelectionLabel());
    m_binder = m_pageSelection ? m_pageSelection->findChild<PDFAppToolPageSelectionBinder*>() : nullptr;

    // At the entry: the pages of the thumbnails, else the current page - never every page
    // of the default (a removal of every page is refused, 388:21466)
    if (!m_isEntryModeApplied && m_pageSelection && m_binder)
    {
        m_isEntryModeApplied = true;
        // The view gives the current page, when no page is selected in the thumbnails
        const PDFAppToolHost* host = getHost();
        const std::vector<PDFInteger> selectedPages = host ? host->getSelectedPages() : std::vector<PDFInteger>();
        const bool hasSelectedPages = selectedPages.size() > 1 || (selectedPages.size() == 1 && selectedPages.front() != host->getCurrentPage());
        if (m_pageSelection->getMode() == PDFPageSelection::Mode::All && m_pageSelection->getFilters() == PDFPageSelection::Filters())
        {
            QPointer<PDFAppPageMarkingToolGUI> guard(this);
            m_binder->setEntryMode(hasSelectedPages ? PDFPageSelection::Mode::Selected : PDFPageSelection::Mode::Current);
            if (!guard)
            {
                return;
            }
        }
    }

    // The hint 389:20402 (Rotate only)
    const QString hint = getLeftHintText();
    if (!hint.isEmpty())
    {
        m_leftHint = builder.addHint(hint);
    }

    // Pending rotation 389:20403, Marked for removal 388:20818
    m_pendingList = builder.addPendingList(getPendingTitle(), m_pendingModel);
    connect(m_pendingList.data(), &PDFAppToolPendingList::linkActivated, this, &PDFAppPageMarkingToolGUI::onPendingLink);
    updateAll();
}

void PDFAppPageMarkingToolGUI::updateTargets()
{
    using Helper = PDFAppToolGUIPagesHelper;

    m_targets.clear();
    const PDFDocumentPointer document = getShownDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (document && model)
    {
        // The same pages as the core: PDFDocumentToolPageSelection of the value of the model
        QString error;
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(model->getValue(Helper::name(PDFDocumentToolRotatePages::PARAMETER_PAGES)), &error);
        if (selection)
        {
            m_targets = selection->getPageIndices(document.data(), &error);
        }
    }

    m_isTarget.assign(size_t(getPageCount()), 0);
    for (const PDFInteger page : m_targets)
    {
        if (size_t(page) < m_isTarget.size())
        {
            m_isTarget[size_t(page)] = 1;
        }
    }
}

void PDFAppPageMarkingToolGUI::updateAll()
{
    updateTargets();

    if (m_grid)
    {
        const PDFInteger pageCount = getPageCount();
        std::vector<PDFAppToolPageGridModel::Tile> tiles;
        tiles.reserve(size_t(pageCount));
        for (PDFInteger page = 0; page < pageCount; ++page)
        {
            tiles.push_back(createTile(page));
        }
        m_grid->setTiles(std::move(tiles));
        m_grid->setHeaderText(getGridHeaderText());
        m_grid->setHintText(getGridHintText());
    }

    const std::vector<PDFInteger> pending = getPendingPages();
    if (m_pendingModel)
    {
        std::vector<PDFAppToolPendingPagesModel::Row> rows;
        rows.reserve(pending.size());
        for (const PDFInteger page : pending)
        {
            rows.push_back(createPendingRow(page));
        }
        m_pendingModel->setRows(std::move(rows));
    }
    if (m_pendingList)
    {
        // No frame of an empty list: 388:21466 has no list
        m_pendingList->setVisible(!pending.empty() && size_t(pending.size()) < size_t(getPageCount()));
    }
    if (m_leftHint)
    {
        m_leftHint->setText(getLeftHintText());
    }

    updatePanels();
    updateConfirmState();
    updateStatusText();
}

void PDFAppPageMarkingToolGUI::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    updateAll();
}

void PDFAppPageMarkingToolGUI::onDocumentChanged()
{
    // An assistant may change the pages below an open tool: the tiles of the new pages
    if (m_grid)
    {
        PDFAppDocument* document = getDocument();
        const PDFAppToolHost* host = getHost();
        m_grid->setDocument(getShownDocument(), document ? document->getId() : QString(), host ? host->getView() : nullptr);
    }
    updateAll();
}

void PDFAppPageMarkingToolGUI::clickPage(int page, Qt::KeyboardModifiers modifiers)
{
    const PDFInteger pageCount = getPageCount();
    if (page < 0 || page >= pageCount)
    {
        return;
    }

    std::vector<char> isTarget = m_isTarget;
    isTarget.resize(size_t(pageCount), 0);
    if (modifiers.testFlag(Qt::ShiftModifier) && m_anchorPage >= 0 && m_anchorPage < pageCount)
    {
        // Shift: the pages from the tile clicked before are added
        for (int i = qMin(m_anchorPage, page); i <= qMax(m_anchorPage, page); ++i)
        {
            isTarget[size_t(i)] = 1;
        }
    }
    else
    {
        isTarget[size_t(page)] = isTarget[size_t(page)] ? 0 : 1;
    }
    m_anchorPage = page;

    std::vector<PDFInteger> pages;
    for (size_t i = 0; i < isTarget.size(); ++i)
    {
        if (isTarget[i])
        {
            pages.push_back(PDFInteger(i));
        }
    }
    setTargetPages(pages);
}

void PDFAppPageMarkingToolGUI::setTargetPages(const std::vector<PDFInteger>& pages)
{
    if (!m_binder)
    {
        return;
    }

    QList<int> numbers;
    numbers.reserve(qsizetype(pages.size()));
    for (const PDFInteger page : pages)
    {
        numbers.append(int(page) + 1);
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());

    // The model announces the change, the parts follow it (the last call)
    m_binder->setPages(numbers);
}

void PDFAppPageMarkingToolGUI::onGridPageClicked(int page, Qt::KeyboardModifiers modifiers)
{
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        clickPage(page, modifiers);
    }
}

void PDFAppPageMarkingToolGUI::onGridDeleteRequested(int page)
{
    const PDFDocumentToolShell* shell = getShell();
    if (shell && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        onDeleteRequested(page);
    }
}

void PDFAppPageMarkingToolGUI::onPendingLink(const QModelIndex& index)
{
    const PDFDocumentToolShell* shell = getShell();
    const PDFInteger page = m_pendingModel ? m_pendingModel->getPage(index.row()) : -1;
    if (page >= 0 && shell && shell->getState() == PDFDocumentToolShell::State::Editing)
    {
        onPendingLinkActivated(page);
    }
}

// -------------------------------------------------------- PDFAppToolGUIRotatePages

PDFAppToolGUIRotatePages::PDFAppToolGUIRotatePages(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIRotatePages::~PDFAppToolGUIRotatePages() = default;

QSize PDFAppToolGUIRotatePages::getSlotSize() const
{
    return PDFAppToolGUIPagesHelper::ROTATE_SLOT;
}

QString PDFAppToolGUIRotatePages::getSelectionLabel() const
{
    return tr("Pages to rotate");
}

QString PDFAppToolGUIRotatePages::getLeftHintText() const
{
    // 389:20910 has the tip of a whole scan: All + Landscape
    const PDFPageSelection* selection = getPageSelection();
    if (selection && selection->getFilters().testFlag(PDFPageSelection::Filter::Landscape) && !selection->getFilters().testFlag(PDFPageSelection::Filter::Portrait))
    {
        return tr("Tip: All + Landscape picks every page that lies on its side – one click on Rotate Left then fixes a whole scan.");
    }
    return tr("The selection comes from Thumbnails. Click pages in the preview to change it; every click on a Rotate button turns the selected pages by another 90°.");
}

QString PDFAppToolGUIRotatePages::getPendingTitle() const
{
    return tr("Pending rotation");
}

QString PDFAppToolGUIRotatePages::getGridHeaderText() const
{
    // 389:20437 "3 of 48 pages will be turned"
    const int pageCount = int(getPageCount());
    return tr("%1 of %Ln pages will be turned", nullptr, pageCount).arg(PDFAppLocale::formatNumber(qint64(m_turns.size())));
}

QString PDFAppToolGUIRotatePages::getGridHintText() const
{
    return tr("The preview shows the pages as they will be saved.");
}

std::vector<PDFInteger> PDFAppToolGUIRotatePages::getPendingPages() const
{
    std::vector<PDFInteger> pages;
    pages.reserve(m_turns.size());
    for (const auto& [page, turn] : m_turns)
    {
        pages.push_back(page);
    }
    return pages;
}

QString PDFAppToolGUIRotatePages::getTurnIconName(PageRotation turn)
{
    switch (turn)
    {
        case PageRotation::Rotate270:
            return QStringLiteral("rotate-ccw");
        case PageRotation::Rotate180:
            return QStringLiteral("rotate-180");
        default:
            return QStringLiteral("rotate-cw");
    }
}

PDFAppToolPendingPagesModel::Row PDFAppToolGUIRotatePages::createPendingRow(PDFInteger page) const
{
    // rot-9 389:20411: "Page 9", "90° right · portrait → landscape", the turn in the tile, Undo
    const auto it = m_turns.find(page);
    const PageRotation turn = it != m_turns.cend() ? it->second : PageRotation::None;
    const PDFDocumentPointer document = getShownDocument();
    const PDFPage* pdfPage = getPage(page);

    PDFAppToolPendingPagesModel::Row row;
    row.page = page;
    row.title = PDFAppPanelWidgets::getPageTitle(document ? document->getCatalog() : nullptr, page);
    row.iconName = getTurnIconName(turn);
    row.tone = PDFComponentPainter::TileRowTone::Accent;
    row.linkText = tr("Undo");
    if (pdfPage)
    {
        const QSizeF size = pdfPage->getRotatedCropBox().size();
        const QSizeF turnedSize = int(turn) % 2 == 1 ? size.transposed() : size;
        const QString before = getOrientationText(size);
        const QString after = getOrientationText(turnedSize);
        row.description = before == after ? tr("%1 · %2").arg(PDFDocumentToolRotatePages::getTurnText(turn), before)
                                          : tr("%1 · %2 → %3").arg(PDFDocumentToolRotatePages::getTurnText(turn), before, after);
    }
    return row;
}

PDFAppToolPageGridModel::Tile PDFAppToolGUIRotatePages::createTile(PDFInteger page) const
{
    // page-9 389:20643: the page of the selection Selected; a turn has its caption and badge
    PDFAppToolPageGridModel::Tile tile;
    tile.state = isTarget(page) ? PDFComponentPainter::PageTileState::Selected : PDFComponentPainter::PageTileState::Normal;
    const auto it = m_turns.find(page);
    if (it != m_turns.cend())
    {
        tile.caption = PDFDocumentToolRotatePages::getTurnText(it->second);
        tile.badgeIconName = getTurnIconName(it->second);
        tile.extraRotation = it->second;
    }
    return tile;
}

void PDFAppToolGUIRotatePages::onPendingLinkActivated(PDFInteger page)
{
    // Undo of the row: the page keeps its rotation
    std::map<PDFInteger, PageRotation> turns = m_turns;
    turns.erase(page);
    writeTurns(turns);
}

void PDFAppToolGUIRotatePages::readTurns()
{
    m_turns.clear();
    const PDFAppToolParameterModel* model = getModel();
    const PDFDocumentPointer document = getShownDocument();
    const QString plan = model ? model->getValue(PDFAppToolGUIPagesHelper::name(PDFDocumentToolRotatePages::PARAMETER_PLAN)).toString() : QString();
    if (!document || plan.trimmed().isEmpty())
    {
        return;
    }

    // A plan, which does not fit the pages any more (an assistant has removed pages), has no turns
    QString error;
    const std::optional<std::map<PDFInteger, PDFDocumentToolRotatePages::Turn>> turns = PDFDocumentToolRotatePages::parsePlan(plan, document.data(), &error);
    if (!turns)
    {
        return;
    }
    for (const auto& [page, turn] : *turns)
    {
        if (!turn.isReset && turn.turn != PageRotation::None)
        {
            m_turns.emplace(page, turn.turn);
        }
    }
}

void PDFAppToolGUIRotatePages::onParametersChanged(const QString& name)
{
    // The turns of the plan of the model (also by Undo and Redo of the tool)
    readTurns();
    BaseClass::onParametersChanged(name);
}

void PDFAppToolGUIRotatePages::onDocumentChanged()
{
    // The plan is read by the pages of the new version
    readTurns();
    BaseClass::onDocumentChanged();
}

void PDFAppToolGUIRotatePages::writeTurns(const std::map<PDFInteger, PageRotation>& turns)
{
    // One undo step of the tool; the model announces it (the last call)
    if (PDFAppToolParameterModel* model = getModel())
    {
        model->setValue(PDFAppToolGUIPagesHelper::name(PDFDocumentToolRotatePages::PARAMETER_PLAN), PDFDocumentToolRotatePages::createPlan(turns));
    }
}

void PDFAppToolGUIRotatePages::turnTargets(PageRotation turn, bool isReset)
{
    std::map<PDFInteger, PageRotation> turns = m_turns;
    for (const PDFInteger page : getTargetPages())
    {
        const PDFPage* pdfPage = getPage(page);
        if (!pdfPage)
        {
            continue;
        }

        // Reset Rotation: the page gets the rotation 0 (owner answer Q9)
        const auto it = turns.find(page);
        const PageRotation current = it != turns.cend() ? it->second : PageRotation::None;
        const PageRotation newTurn = isReset ? getPageRotationInversed(pdfPage->getPageRotation()) : getPageRotationCombined(turn, current);
        if (newTurn == PageRotation::None)
        {
            turns.erase(page);
        }
        else
        {
            turns[page] = newTurn;
        }
    }
    writeTurns(turns);
}

void PDFAppToolGUIRotatePages::rotateLeft()
{
    turnTargets(PageRotation::Rotate270, false);
}

void PDFAppToolGUIRotatePages::rotateRight()
{
    turnTargets(PageRotation::Rotate90, false);
}

void PDFAppToolGUIRotatePages::rotateHalf()
{
    turnTargets(PageRotation::Rotate180, false);
}

void PDFAppToolGUIRotatePages::resetRotation()
{
    turnTargets(PageRotation::None, true);
}

QString PDFAppToolGUIRotatePages::getRotationText() const
{
    std::set<PageRotation> turns;
    for (const auto& [page, turn] : m_turns)
    {
        turns.insert(turn);
    }
    if (turns.empty())
    {
        return QString();
    }
    return turns.size() == 1 ? PDFDocumentToolRotatePages::getTurnText(*turns.begin()) : tr("Mixed");
}

void PDFAppToolGUIRotatePages::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // tool-actions 389:20197: Rotate Left, Rotate Right, Rotate 180°, divider, Reset Rotation
    m_rotateLeft = builder.addAction(tr("Rotate Left"), QStringLiteral("rotate-ccw"), this, &PDFAppToolGUIRotatePages::rotateLeft);
    m_rotateRight = builder.addAction(tr("Rotate Right"), QStringLiteral("rotate-cw"), this, &PDFAppToolGUIRotatePages::rotateRight);
    m_rotateHalf = builder.addAction(tr("Rotate 180°"), QStringLiteral("rotate-180"), this, &PDFAppToolGUIRotatePages::rotateHalf);
    builder.addActionDivider();
    m_reset = builder.addAction(tr("Reset Rotation"), QStringLiteral("rotation-reset"), this, &PDFAppToolGUIRotatePages::resetRotation);
    updatePanels();
}

void PDFAppToolGUIRotatePages::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // section-Summary 389:20891 and callout-blue 389:20904
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->addRow(tr("Pages"), QString());
    m_summary->addRow(tr("Rotation"), QString());
    m_summary->addRow(tr("Other pages"), QString());
    builder.endSection();
    m_callout = builder.addCallout(PDFAccentHue::Blue, QStringLiteral("info"), tr("The pages are turned in the file"),
                                   tr("Rotate Pages changes the document for everyone who opens it. To turn only what you see, use View ▸ Rotate – that is not saved."));
    updateAll();
}

void PDFAppToolGUIRotatePages::updatePanels()
{
    const bool hasTargets = !getTargetPages().empty();
    for (QToolButton* button : { m_rotateLeft.data(), m_rotateRight.data(), m_rotateHalf.data(), m_reset.data() })
    {
        if (button)
        {
            button->setEnabled(hasTargets && !m_isDeciding);
        }
    }

    if (m_summary && m_summary->getRowCount() == 3)
    {
        // 389:20895 "3 · 9, 10, 15", "90° right" / Mixed (K14), "45 unchanged"
        const std::vector<PDFInteger> pages = getPendingPages();
        const QString count = PDFAppLocale::formatNumber(qint64(pages.size()));
        m_summary->setValue(0, pages.empty() ? count : tr("%1 · %2").arg(count, PDFDocumentPageOperations::formatPageNumbers(pages)));
        const QString rotation = getRotationText();
        m_summary->setValue(1, rotation.isEmpty() ? QStringLiteral("–") : rotation);
        m_summary->setValue(2, tr("%Ln unchanged", nullptr, int(getPageCount() - PDFInteger(pages.size()))));
    }
}

void PDFAppToolGUIRotatePages::onShellStateChanged(PDFDocumentToolShell::State state)
{
    // 389:21590: nothing to turn below the result dialog
    m_isDeciding = state == PDFDocumentToolShell::State::Deciding;
    updatePanels();
}

QString PDFAppToolGUIRotatePages::getConfirmText() const
{
    // btn-confirm 389:20202 "Rotate 3 Pages"; K15: without a turn "Rotate Pages" disabled
    const int count = int(m_turns.size());
    return count > 0 ? tr("Rotate %Ln Pages", nullptr, count) : tr("Rotate Pages");
}

bool PDFAppToolGUIRotatePages::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && !m_turns.empty();
}

QString PDFAppToolGUIRotatePages::getStatusText() const
{
    if (m_turns.empty())
    {
        return BaseClass::getStatusText();
    }

    // 389:20213 "Rotate Pages · 3 pages turned 90° right · the rotation is saved in the document"
    const int count = int(m_turns.size());
    std::set<PageRotation> turns;
    for (const auto& [page, turn] : m_turns)
    {
        turns.insert(turn);
    }
    const QString rotation = getRotationText();
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    if (turns.size() > 1)
    {
        return tr("%1 · %Ln pages turned · the rotation is saved in the document", nullptr, count).arg(title);
    }
    return tr("%1 · %Ln pages turned %2 · the rotation is saved in the document", nullptr, count).arg(title, rotation);
}

void PDFAppToolGUIRotatePages::fillResultInput(PDFAppToolResultInput* input) const
{
    // 389:21590 "Pages rotated"
    input->title = tr("Pages rotated");
}

// -------------------------------------------------------- PDFAppToolGUIRemovePages

PDFAppToolGUIRemovePages::PDFAppToolGUIRemovePages(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolGUIRemovePages::~PDFAppToolGUIRemovePages() = default;

QSize PDFAppToolGUIRemovePages::getSlotSize() const
{
    return PDFAppToolGUIPagesHelper::REMOVE_SLOT;
}

QString PDFAppToolGUIRemovePages::getSelectionLabel() const
{
    return tr("Pages to remove");
}

QString PDFAppToolGUIRemovePages::getPendingTitle() const
{
    return tr("Marked for removal");
}

bool PDFAppToolGUIRemovePages::isEveryPageMarked() const
{
    const PDFInteger pageCount = getPageCount();
    return pageCount > 0 && PDFInteger(getTargetPages().size()) >= pageCount;
}

QString PDFAppToolGUIRemovePages::getGridHeaderText() const
{
    // 388:20858 "44 of 48 pages stay", 388:21621 "No page would stay"
    if (isEveryPageMarked())
    {
        return tr("No page would stay");
    }
    const int pageCount = int(getPageCount());
    return tr("%1 of %Ln pages stay", nullptr, pageCount).arg(PDFAppLocale::formatNumber(qint64(pageCount) - qint64(getTargetPages().size())));
}

QString PDFAppToolGUIRemovePages::getGridHintText() const
{
    return tr("Click a page to mark it or keep it; Shift and Ctrl select more pages. Marked pages stay visible until you confirm.");
}

std::vector<PDFInteger> PDFAppToolGUIRemovePages::getPendingPages() const
{
    return getTargetPages();
}

PDFAppToolPendingPagesModel::Row PDFAppToolGUIRemovePages::createPendingRow(PDFInteger page) const
{
    // removed-4 388:20825: the label in a red tile, "Page 4", "A4 portrait", Keep
    const PDFDocumentPointer document = getShownDocument();
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;

    PDFAppToolPendingPagesModel::Row row;
    row.page = page;
    row.title = PDFAppPanelWidgets::getPageTitle(catalog, page);
    row.tileText = PDFAppPanelWidgets::getPageLabel(catalog, page);
    row.tone = PDFComponentPainter::TileRowTone::Danger;
    row.linkText = tr("Keep");
    if (const PDFPage* pdfPage = getPage(page))
    {
        const QSizeF size = PDFAppToolGUIPagesHelper::getSizeMM(pdfPage);
        const QString paper = PDFAppDocumentPropertiesData::getPaperFormatName(size);
        const QString orientation = getOrientationText(size);
        row.description = !paper.isEmpty() ? tr("%1 %2").arg(paper, orientation)
                                           : tr("%1 × %2 mm %3").arg(PDFAppLocale::formatNumber(qint64(qRound(size.width()))), PDFAppLocale::formatNumber(qint64(qRound(size.height()))), orientation);
    }
    return row;
}

PDFAppToolPageGridModel::Tile PDFAppToolGUIRemovePages::createTile(PDFInteger page) const
{
    // page-4 388:20945: the page dimmed, the trash badge, "will be removed"
    PDFAppToolPageGridModel::Tile tile;
    if (isTarget(page))
    {
        tile.state = PDFComponentPainter::PageTileState::Marked;
        tile.caption = tr("will be removed");
        tile.badgeIconName = QStringLiteral("trash");
    }
    return tile;
}

void PDFAppToolGUIRemovePages::onPendingLinkActivated(PDFInteger page)
{
    // Keep of the row
    std::vector<PDFInteger> pages = getTargetPages();
    pages.erase(std::remove(pages.begin(), pages.end(), page), pages.end());
    setTargetPages(pages);
}

void PDFAppToolGUIRemovePages::onDeleteRequested(PDFInteger page)
{
    // Del marks the page (tools.md: "odebrání výběru (Del)")
    if (!isTarget(page))
    {
        std::vector<PDFInteger> pages = getTargetPages();
        pages.push_back(page);
        setTargetPages(pages);
    }
}

void PDFAppToolGUIRemovePages::keepAllPages()
{
    setTargetPages({});
}

void PDFAppToolGUIRemovePages::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    // tb-restore-all 388:20707
    m_keepAll = builder.addAction(tr("Keep All Pages"), QStringLiteral("trash-restore"), this, &PDFAppToolGUIRemovePages::keepAllPages);
    updatePanels();
}

void PDFAppToolGUIRemovePages::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // callout-red 388:22298 (only when every page is marked), section-Summary 388:21455, the note 388:21465
    m_allPagesCallout = builder.addCallout(PDFAccentHue::Red, QStringLiteral("alert-circle"), tr("All pages are selected"),
                                           tr("A document must keep at least one page. Unmark a page, or close the document instead."));
    m_allPagesCallout->setObjectName(QStringLiteral("toolAllPagesCallout"));
    builder.addSection(tr("Summary"));
    m_summary = builder.addKeyValues();
    m_summary->addRow(tr("Remove"), QString());
    m_summary->addRow(tr("Stay"), QString());
    builder.endSection();
    m_note = builder.addHint(tr("Removed pages come back with Undo until the document is closed. To keep them in another file first, use Extract Pages."));
    m_note->setObjectName(QStringLiteral("toolRemoveNote"));
    updateAll();
}

void PDFAppToolGUIRemovePages::updatePanels()
{
    const std::vector<PDFInteger>& pages = getTargetPages();
    const bool isAll = isEveryPageMarked();
    if (m_keepAll)
    {
        m_keepAll->setEnabled(!pages.empty() && !m_isDeciding);
    }
    if (m_allPagesCallout)
    {
        m_allPagesCallout->setVisible(isAll);
    }
    if (m_summary && m_summary->getRowCount() == 2)
    {
        // 388:21459 "4 pages · 4–6, 20", "44 pages"; every page: "48 pages", "0 pages"
        const int count = int(pages.size());
        const QString removed = tr("%Ln pages", nullptr, count);
        m_summary->setValue(0, pages.empty() || isAll ? removed : tr("%1 · %2").arg(removed, PDFDocumentPageOperations::formatPageNumbers(pages)));
        m_summary->setValue(1, tr("%Ln pages", nullptr, int(getPageCount()) - count));
    }
}

void PDFAppToolGUIRemovePages::onShellStateChanged(PDFDocumentToolShell::State state)
{
    // 388:22315: Keep All Pages disabled below the result dialog
    m_isDeciding = state == PDFDocumentToolShell::State::Deciding;
    updatePanels();
}

QString PDFAppToolGUIRemovePages::getConfirmText() const
{
    // 388:20639 "Remove 4 Pages", 388:21491 "Remove Pages" disabled
    const int count = int(getTargetPages().size());
    return count > 0 && !isEveryPageMarked() ? tr("Remove %Ln Pages", nullptr, count) : tr("Remove Pages");
}

bool PDFAppToolGUIRemovePages::isConfirmEnabled() const
{
    return BaseClass::isConfirmEnabled() && !getTargetPages().empty() && !isEveryPageMarked();
}

QString PDFAppToolGUIRemovePages::getStatusText() const
{
    const PDFDocumentToolCatalogEntry* tool = getTool();
    const QString title = tool ? tool->getTitle() : QString();
    if (isEveryPageMarked())
    {
        // 388:21502 "Remove Pages · a document must keep at least one page"
        return tr("%1 · a document must keep at least one page").arg(title);
    }

    const int count = int(getTargetPages().size());
    if (count == 0)
    {
        return BaseClass::getStatusText();
    }

    // 388:20650 "Remove Pages · 4 pages will be removed · 44 of 48 pages stay"
    const int pageCount = int(getPageCount());
    return tr("%1 · %Ln pages will be removed", nullptr, count).arg(title) + QStringLiteral(" · ") +
           tr("%1 of %Ln pages stay", nullptr, pageCount).arg(PDFAppLocale::formatNumber(qint64(pageCount - count)));
}

void PDFAppToolGUIRemovePages::fillResultInput(PDFAppToolResultInput* input) const
{
    // 388:22315 "Pages removed"
    input->title = tr("Pages removed");
}

}   // namespace pdf

#include "moc_pdfapptoolgui_pages.cpp"
