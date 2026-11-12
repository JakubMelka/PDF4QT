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

#include "pdfappmanagedialogs.h"

#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapplocale.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolgui_split.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcatalog.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfpagerangelist.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <set>

namespace pdf
{

/// Units of the dialogs of Manage Pages
class PDFAppManageDialogsHelper
{
public:
    PDFAppManageDialogsHelper() = delete;

    /// Points of a millimeter (72 points per inch)
    static constexpr double POINTS_PER_MILLIMETER = 72.0 / 25.4;

    /// Gap of the parts of a form (387:15926: the label 8 above its field, the groups 14 apart)
    static constexpr int GAP = 8;

    /// Paddings of the facts (387:16744, 387:16947: 12 at the sides, 4 above and below)
    static constexpr int FACTS_PADDING_H = 12;
    static constexpr int FACTS_PADDING_V = 4;

    /// The facts of a dialog (387:16744 Add pages of a PDF, 387:16947 Properties): the box of the
    /// facts of the design - bg/subtle, radius 10, no border (the box of the confirmations,
    /// PDFAppMcpBodyBox) - with a vertical layout of the paddings of the frames
    static QWidget* createFacts(QWidget* parent)
    {
        PDFAppMcpBodyBox* box = new PDFAppMcpBodyBox(parent);
        QVBoxLayout* layout = new QVBoxLayout(box);
        layout->setContentsMargins(FACTS_PADDING_H, FACTS_PADDING_V, FACTS_PADDING_H, FACTS_PADDING_V);
        layout->setSpacing(0);
        return box;
    }
};

// ------------------------------------------------------------- PDFAppManageDialogs

QString PDFAppManageDialogs::getEveryText(int pageCount, int pagesPerGroup)
{
    if (pageCount <= 0 || pagesPerGroup <= 0)
    {
        return QString();
    }

    // "17 selected pages → 3 groups (6, 6 and 5 pages)"
    const int groupCount = (pageCount + pagesPerGroup - 1) / pagesPerGroup;
    const int last = pageCount - (groupCount - 1) * pagesPerGroup;
    const QString pages = tr("%Ln selected pages", nullptr, pageCount);
    if (groupCount == 1)
    {
        return tr("%1 → 1 group").arg(pages);
    }
    QString sizes;
    if (groupCount <= 5)
    {
        QStringList parts;
        for (int i = 0; i < groupCount - 1; ++i)
        {
            parts << PDFAppLocale::formatNumber(pagesPerGroup);
        }
        sizes = tr("%1 and %2 pages").arg(parts.join(tr(", ")), PDFAppLocale::formatNumber(last));
    }
    else
    {
        sizes = last == pagesPerGroup ? tr("%Ln pages each", nullptr, pagesPerGroup) : tr("%1 pages each, the last %2").arg(PDFAppLocale::formatNumber(pagesPerGroup), PDFAppLocale::formatNumber(last));
    }
    return tr("%1 → %2 (%3)").arg(pages, tr("%Ln groups", nullptr, groupCount), sizes);
}

std::vector<int> PDFAppManageDialogs::parsePositions(const QString& text, int pageCount, QString* errorMessage)
{
    // "7, 13": positions in the selection from 1; the first group always starts at 1
    std::set<int> positions;
    errorMessage->clear();
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty())
    {
        *errorMessage = tr("Enter the positions, where a new group starts");
        return std::vector<int>();
    }
    if (parts.size() > qMax(1, pageCount))
    {
        *errorMessage = tr("There are more positions than pages");
        return std::vector<int>();
    }
    for (const QString& part : parts)
    {
        bool ok = false;
        const int position = part.trimmed().toInt(&ok);
        if (!ok)
        {
            *errorMessage = tr("\"%1\" is not a number").arg(part.trimmed().left(32));
            return std::vector<int>();
        }
        if (position < 1 || position > pageCount)
        {
            *errorMessage = tr("Position %1 does not exist – the selection has %Ln pages", nullptr, pageCount).arg(PDFAppLocale::formatNumber(position));
            return std::vector<int>();
        }
        if (position > 1)
        {
            positions.insert(position);
        }
    }
    return std::vector<int>(positions.cbegin(), positions.cend());
}

QString PDFAppManageDialogs::getPositionsText(int pageCount, const std::vector<int>& positions)
{
    if (pageCount <= 0)
    {
        return QString();
    }

    // "17 selected pages → 3 groups (1–6, 7–12, 13–17)"
    QStringList ranges;
    int first = 1;
    std::vector<int> starts = positions;
    starts.push_back(pageCount + 1);
    for (const int start : starts)
    {
        const int last = start - 1;
        if (last < first)
        {
            continue;
        }
        ranges << (first == last ? PDFAppLocale::formatNumber(first) : tr("%1–%2").arg(PDFAppLocale::formatNumber(first), PDFAppLocale::formatNumber(last)));
        first = start;
    }
    const int groupCount = int(ranges.size());
    return tr("%1 → %2 (%3)").arg(tr("%Ln selected pages", nullptr, pageCount), groupCount == 1 ? tr("1 group") : tr("%Ln groups", nullptr, groupCount), ranges.join(tr(", ")));
}

QString PDFAppManageDialogs::getSelectionRangeText(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Selection& selection)
{
    // The positions of the pages of the selected items (the rule of PDFDocumentAssembly::select,
    // SelectRule::PageRange: a position for every page of the assembly), ascending
    const std::set<QString> selected(selection.cbegin(), selection.cend());
    std::vector<qint64> positions;
    qint64 position = 0;
    for (const PDFDocumentAssembly::Item& item : assembly.getItems())
    {
        const bool isSelected = selected.count(item.id) > 0;
        for (size_t i = 0; i < item.pages.size(); ++i)
        {
            ++position;
            if (isSelected)
            {
                positions.push_back(position);
            }
        }
    }
    const qint64 lastPosition = position;

    // The syntax of PDFPageRangeList (ASCII, the field reads it back): "1-3, 8, 10-"
    QStringList parts;
    qint64 length = 0;
    size_t first = 0;
    while (first < positions.size())
    {
        size_t last = first;
        while (last + 1 < positions.size() && positions[last + 1] == positions[last] + 1)
        {
            ++last;
        }
        QString part = QString::number(positions[first]);
        if (last > first)
        {
            part += QLatin1Char('-');
            if (positions[last] != lastPosition)
            {
                part += QString::number(positions[last]);
            }
        }
        length += part.size() + 2;
        if (length > PDFPageRangeList::MAXIMUM_TEXT_LENGTH)
        {
            // A selection of many scattered pages: no text the field could read
            return QString();
        }
        parts << part;
        first = last + 1;
    }
    return parts.join(QStringLiteral(", "));
}

// ------------------------------------------------------------- PDFAppManageDialog

PDFAppManageDialog::PDFAppManageDialog(const QString& title, const QString& acceptText, int width, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral, title, QString(), QDialogButtonBox::NoButton, parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setBadgeVisible(false);
    setCardWidth(width);
    if (scrimTarget)
    {
        setScrimTarget(scrimTarget);
    }

    // The buttons of 387:15926: Cancel and the Primary button at the trailing edge
    m_cancelButton = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    m_cancelButton->setObjectName(QStringLiteral("manageDialogCancel"));
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppManageDialog::onCancelClicked);
    if (!acceptText.isEmpty())
    {
        m_acceptButton = addActionButton(acceptText, ActionPosition::Trailing, true);
        m_acceptButton->setObjectName(QStringLiteral("manageDialogAccept"));
        connect(m_acceptButton.data(), &QPushButton::clicked, this, &PDFAppManageDialog::onAcceptClicked);
    }
}

PDFAppManageDialog::~PDFAppManageDialog() = default;

void PDFAppManageDialog::setAcceptEnabled(bool isEnabled)
{
    if (m_acceptButton)
    {
        m_acceptButton->setEnabled(isEnabled);
    }
}

QWidget* PDFAppManageDialog::createCaption(const QString& text)
{
    // "Numbers mean", "Selection" (387:15926): Semi Bold 12 text/primary
    return PDFAppPageWidgets::createLabel(text, QFont::DemiBold, 12, PDFThemeColors::TextPrimary, this);
}

PDFLabel* PDFAppManageDialog::createStatus()
{
    PDFLabel* label = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextAccent, this);
    label->setObjectName(QStringLiteral("manageDialogStatus"));
    label->setWordWrap(true);
    label->setMinimumWidth(1);
    return label;
}

void PDFAppManageDialog::setStatus(PDFLabel* label, const QString& text, bool isError)
{
    if (label)
    {
        label->setText(text);
        label->setTextColor(isError ? PDFThemeColors::TextDanger : PDFThemeColors::TextAccent);
    }
}

void PDFAppManageDialog::triggerAccept()
{
    onAcceptClicked();
}

void PDFAppManageDialog::onAcceptClicked()
{
    if (m_acceptButton && !m_acceptButton->isEnabled())
    {
        return;
    }

    // The decision is said last by the subclass; its slot may delete this dialog
    QPointer<PDFAppManageDialog> guard(this);
    acceptDialog();
    if (guard)
    {
        accept();
    }
}

void PDFAppManageDialog::onCancelClicked()
{
    reject();
}

// ------------------------------------------------------------- PDFAppManageRangeDialog

PDFAppManageRangeDialog::PDFAppManageRangeDialog(PDFDocumentAssembly assembly, PDFDocumentAssembly::Selection selection, const QString& sourceId, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(tr("Select page range"), tr("Select"), PDFAppManageDialogs::FORM_WIDTH, scrimTarget, parent),
    m_assembly(std::move(assembly)),
    m_selection(std::move(selection)),
    m_sourceId(sourceId)
{
    setObjectName(QStringLiteral("manageRangeDialog"));

    QString sourceName;
    PDFInteger sourcePageCount = 0;
    for (const PDFDocumentAssembly::Source& source : m_assembly.getSources())
    {
        if (source.id == m_sourceId)
        {
            sourceName = source.name;
            sourcePageCount = source.pageCount;
        }
    }
    qint64 pageCount = 0;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        pageCount += qint64(item.pages.size());
    }

    // Range: the field of K23 with its examples (387:15926)
    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppManageDialogsHelper::GAP);
    layout->addWidget(PDFAppPageWidgets::createLabel(tr("Range"), QFont::Normal, 12, PDFThemeColors::TextSecondary, content));
    m_range = new PDFAppToolPageRangeField(content);
    m_range->setObjectName(QStringLiteral("manageRangeField"));
    m_range->setPageCount(qMax<PDFInteger>(pageCount, sourcePageCount));
    m_range->setExamples(tr("Examples: 1-5, 7, 10-, odd, even"));

    // Owner answer 7 of 12. 11.: the field starts with the positions of the selection ("1-3, 8,
    // 10-" of the frame), never in the error state; without a selection it is empty and Select
    // waits for a range. The frame has no line of the field below the examples: the count and an
    // error are the status of the dialog.
    m_range->setEmptyAllowed(true);
    m_range->setText(PDFAppManageDialogs::getSelectionRangeText(m_assembly, m_selection));
    m_range->getStatusLabel()->hide();
    layout->addWidget(m_range);

    // Numbers mean: Position in the assembly / Original pages of <source>
    layout->addWidget(createCaption(tr("Numbers mean")));
    m_positionRadio = new QRadioButton(tr("Position in the assembly"), content);
    m_positionRadio->setObjectName(QStringLiteral("manageRangePosition"));
    m_positionRadio->setChecked(true);
    layout->addWidget(m_positionRadio);
    m_originalRadio = new QRadioButton(sourceName.isEmpty() ? tr("Original pages of a file") : tr("Original pages of %1").arg(sourceName), content);
    m_originalRadio->setObjectName(QStringLiteral("manageRangeOriginal"));
    m_originalRadio->setEnabled(!sourceName.isEmpty());
    layout->addWidget(m_originalRadio);
    QButtonGroup* meaning = new QButtonGroup(content);
    meaning->addButton(m_positionRadio);
    meaning->addButton(m_originalRadio);

    // Selection: Replace / Add
    layout->addWidget(createCaption(tr("Selection")));
    m_replaceRadio = new QRadioButton(tr("Replace the selection"), content);
    m_replaceRadio->setObjectName(QStringLiteral("manageRangeReplace"));
    m_replaceRadio->setChecked(true);
    layout->addWidget(m_replaceRadio);
    m_addRadio = new QRadioButton(tr("Add to the selection"), content);
    m_addRadio->setObjectName(QStringLiteral("manageRangeAdd"));
    layout->addWidget(m_addRadio);
    QButtonGroup* mode = new QButtonGroup(content);
    mode->addButton(m_replaceRadio);
    mode->addButton(m_addRadio);

    m_status = createStatus();
    layout->addWidget(m_status);
    addContent(content);

    connect(m_range->getField(), &QLineEdit::textChanged, this, &PDFAppManageRangeDialog::updateStatus);
    connect(m_positionRadio.data(), &QRadioButton::toggled, this, &PDFAppManageRangeDialog::updateStatus);
    connect(m_addRadio.data(), &QRadioButton::toggled, this, &PDFAppManageRangeDialog::updateStatus);
    updateStatus();
}

PDFAppManageRangeDialog::~PDFAppManageRangeDialog() = default;

PDFDocumentAssembly::SelectSettings PDFAppManageRangeDialog::getSettings() const
{
    PDFDocumentAssembly::SelectSettings settings;
    settings.pageRange = m_range->getText();
    settings.sourceId = m_originalRadio->isChecked() ? m_sourceId : QString();
    settings.isAdded = m_addRadio->isChecked();
    return settings;
}

void PDFAppManageRangeDialog::updateStatus()
{
    // "5 items will be selected" by the rule of the core (387:15926)
    const PDFDocumentAssembly::SelectSettings settings = getSettings();
    if (settings.pageRange.trimmed().isEmpty())
    {
        setStatus(m_status, QString(), false);
        setAcceptEnabled(false);
        return;
    }
    if (!m_range->getError().isEmpty())
    {
        setStatus(m_status, m_range->getError(), true);
        setAcceptEnabled(false);
        return;
    }
    const PDFDocumentAssembly::Selection selected = PDFDocumentAssembly::select(m_assembly, m_selection, PDFDocumentAssembly::SelectRule::PageRange, settings);
    const int count = int(selected.size());
    setStatus(m_status, count == 1 ? tr("1 item will be selected") : tr("%Ln items will be selected", nullptr, count), false);
    setAcceptEnabled(true);
}

void PDFAppManageRangeDialog::acceptDialog()
{
    Q_EMIT selectRequested(getSettings());
}

// ------------------------------------------------------------- PDFAppManageBookmarksDialog

PDFAppManageBookmarksDialog::PDFAppManageBookmarksDialog(const QString& fileName, std::vector<PDFOutlineBreakPoints::Bookmark> bookmarks, std::vector<PDFInteger> pages, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(tr("Regroup by bookmarks"), tr("Regroup"), PDFAppManageDialogs::FORM_WIDTH, scrimTarget, parent),
    m_pages(std::move(pages))
{
    setObjectName(QStringLiteral("manageBookmarksDialog"));
    setText(tr("A new group starts at each checked bookmark of %1.").arg(fileName));

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppManageDialogsHelper::GAP);

    // The checklist of Split At bookmarks (383:6771): the model, the rows of the delegate
    m_model = new PDFAppSplitBookmarkModel(this);
    m_model->setBookmarks(std::move(bookmarks));
    m_model->setValue(QStringLiteral("top-level"));
    m_view = new PDFListView(content);
    m_view->setObjectName(QStringLiteral("manageBookmarksView"));
    m_view->setModel(m_model);
    m_view->setItemDelegate(new PDFAppSplitBookmarkDelegate(m_view));
    m_view->setSelectionMode(QAbstractItemView::NoSelection);
    m_view->setFixedHeight(LIST_HEIGHT);
    connect(m_view.data(), &QListView::clicked, this, &PDFAppManageBookmarksDialog::onViewClicked);
    layout->addWidget(m_view);

    // The links Select all, Level 1, Level 2, Invert
    QHBoxLayout* links = new QHBoxLayout();
    links->setContentsMargins(0, 0, 0, 0);
    links->setSpacing(16);
    struct Link
    {
        QString text;
        const char* name;
        void (PDFAppManageBookmarksDialog::*slot)();
    };
    const Link linkItems[] = {
        { tr("Select all"), "manageBookmarksAll", &PDFAppManageBookmarksDialog::onSelectAllClicked },
        { tr("Level 1"), "manageBookmarksLevel1", &PDFAppManageBookmarksDialog::onLevel1Clicked },
        { tr("Level 2"), "manageBookmarksLevel2", &PDFAppManageBookmarksDialog::onLevel2Clicked },
        { tr("Invert"), "manageBookmarksInvert", &PDFAppManageBookmarksDialog::onInvertClicked },
    };
    for (const Link& item : linkItems)
    {
        PDFTextLink* link = new PDFTextLink(item.text, content);
        link->setObjectName(QString::fromLatin1(item.name));
        connect(link, &PDFTextLink::clicked, this, item.slot);
        links->addWidget(link);
    }
    links->addStretch(1);
    layout->addLayout(links);
    addContent(content);
    setAcceptEnabled(m_model->rowCount() > 0);
}

PDFAppManageBookmarksDialog::~PDFAppManageBookmarksDialog() = default;

void PDFAppManageBookmarksDialog::setCheckedRows(const std::vector<int>& rows)
{
    m_model->setValue(m_model->createValue(rows));
    setAcceptEnabled(!m_model->getCheckedRows().empty());
}

void PDFAppManageBookmarksDialog::toggleRow(int row)
{
    std::vector<int> rows = m_model->getCheckedRows();
    const auto it = std::find(rows.begin(), rows.end(), row);
    if (it != rows.end())
    {
        rows.erase(it);
    }
    else if (row >= 0 && row < m_model->rowCount())
    {
        rows.push_back(row);
        std::sort(rows.begin(), rows.end());
    }
    setCheckedRows(rows);
}

void PDFAppManageBookmarksDialog::selectAll()
{
    std::vector<int> rows;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        rows.push_back(row);
    }
    setCheckedRows(rows);
}

void PDFAppManageBookmarksDialog::selectLevel(int level)
{
    // Level 1: the bookmarks of the top level; Level 2: also their children
    std::vector<int> rows;
    const std::vector<PDFOutlineBreakPoints::Bookmark>& bookmarks = m_model->getBookmarks();
    for (size_t row = 0; row < bookmarks.size(); ++row)
    {
        if (bookmarks[row].level < level)
        {
            rows.push_back(int(row));
        }
    }
    setCheckedRows(rows);
}

void PDFAppManageBookmarksDialog::invert()
{
    const std::vector<int> checked = m_model->getCheckedRows();
    const std::set<int> checkedSet(checked.cbegin(), checked.cend());
    std::vector<int> rows;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        if (!checkedSet.count(row))
        {
            rows.push_back(row);
        }
    }
    setCheckedRows(rows);
}

PDFDocumentAssembly::RegroupSettings PDFAppManageBookmarksDialog::getSettings() const
{
    // The groups of the checked bookmarks over the pages of the source in the selection (the rule
    // of Split At bookmarks, PDFOutlineBreakPoints)
    std::vector<PDFOutlineBreakPoints::Bookmark> checked;
    const std::vector<PDFOutlineBreakPoints::Bookmark>& bookmarks = m_model->getBookmarks();
    for (const int row : m_model->getCheckedRows())
    {
        if (row >= 0 && size_t(row) < bookmarks.size())
        {
            checked.push_back(bookmarks[size_t(row)]);
        }
    }
    PDFDocumentAssembly::RegroupSettings settings;
    for (const PDFOutlineBreakPoints::Group& group : PDFOutlineBreakPoints::createGroups(checked, m_pages, nullptr))
    {
        settings.breakPages.push_back(group.firstPage);
        settings.groupNames << group.title;
    }
    return settings;
}

void PDFAppManageBookmarksDialog::acceptDialog()
{
    Q_EMIT regroupRequested(getSettings());
}

void PDFAppManageBookmarksDialog::onViewClicked(const QModelIndex& index)
{
    toggleRow(index.row());
}

void PDFAppManageBookmarksDialog::onSelectAllClicked()
{
    selectAll();
}

void PDFAppManageBookmarksDialog::onLevel1Clicked()
{
    selectLevel(1);
}

void PDFAppManageBookmarksDialog::onLevel2Clicked()
{
    selectLevel(2);
}

void PDFAppManageBookmarksDialog::onInvertClicked()
{
    invert();
}

// ------------------------------------------------------------- PDFAppManageAddPagesDialog

PDFAppManageAddPagesDialog::PDFAppManageAddPagesDialog(const QString& sourceId, const QString& fileName, PDFInteger pageCount, bool isReplace, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(isReplace ? tr("Replace selection") : tr("Add pages of a PDF"), QStringLiteral(" "), PDFAppManageDialogs::ADD_PAGES_WIDTH, scrimTarget, parent),
    m_sourceId(sourceId),
    m_pageCount(pageCount),
    m_isReplace(isReplace)
{
    setObjectName(QStringLiteral("manageAddPagesDialog"));

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppManageDialogsHelper::GAP);

    // The file and its pages on bg/subtle (387:16744: radius 10, no border, paddings 12 / 4)
    m_facts = PDFAppManageDialogsHelper::createFacts(content);
    m_facts->setObjectName(QStringLiteral("manageAddPagesFacts"));
    PDFKeyValueList* values = new PDFKeyValueList(m_facts);
    values->addRow(tr("File"), fileName, PDFComponentPainter::TextKind::FileName);
    values->addRow(tr("Pages"), PDFAppLocale::formatNumber(pageCount));
    m_facts->layout()->addWidget(values);
    layout->addWidget(m_facts);

    m_allRadio = new QRadioButton(tr("All pages"), content);
    m_allRadio->setObjectName(QStringLiteral("manageAddAll"));
    layout->addWidget(m_allRadio);
    m_rangeRadio = new QRadioButton(tr("Page range"), content);
    m_rangeRadio->setObjectName(QStringLiteral("manageAddRange"));
    m_rangeRadio->setChecked(true);
    layout->addWidget(m_rangeRadio);
    QButtonGroup* group = new QButtonGroup(content);
    group->addButton(m_allRadio);
    group->addButton(m_rangeRadio);
    m_range = new PDFAppToolPageRangeField(content);
    m_range->setObjectName(QStringLiteral("manageAddRangeField"));
    m_range->setPageCount(pageCount);
    m_range->setExamples(tr("Examples: 1-5, 7, 10-, odd, even"));
    m_range->setText(pageCount > 0 ? QStringLiteral("1-") : QString());
    // The frame has no line of the field below the examples: the count and an error are the
    // status of the dialog below the previews
    m_range->getStatusLabel()->hide();
    layout->addWidget(m_range);

    // preview 387:16763: the tiles of the chosen pages (the grid of the tools and its delegate
    // with the box 41 x 56 on the card bg/surface); their pictures come with setPreviewSource
    m_previewView = new PDFAppToolPageGridView(content);
    m_previewView->setObjectName(QStringLiteral("manageAddPagesPreview"));
    m_previewView->setAccessibleName(tr("Pages to add"));
    m_previewView->setSpacing(PREVIEW_GAP);
    m_previewView->setFixedHeight(PREVIEW_HEIGHT);
    m_previewView->setFrameShape(QFrame::NoFrame);
    m_previewModel = new PDFAppInsertSourceModel(m_previewView);
    m_previewDelegate = new PDFAppToolPageTileDelegate(m_previewView);
    m_previewDelegate->setBackgroundColor(PDFThemeColors::BgSurface);
    m_previewDelegate->setSlotSize(PREVIEW_SLOT);
    m_previewView->setModel(m_previewModel);
    m_previewView->setItemDelegate(m_previewDelegate);
    m_previewModel->setPictureSize(PREVIEW_SLOT, devicePixelRatioF());
    m_previewScheduler = new PDFAppThumbnailScheduler(m_previewModel, m_previewView, m_previewView);
    layout->addWidget(m_previewView);

    m_status = createStatus();
    layout->addWidget(m_status);
    addContent(content);

    connect(m_range->getField(), &QLineEdit::textChanged, this, &PDFAppManageAddPagesDialog::updateState);
    connect(m_allRadio.data(), &QRadioButton::toggled, this, &PDFAppManageAddPagesDialog::updateState);
    updateState();
}

PDFAppManageAddPagesDialog::~PDFAppManageAddPagesDialog() = default;

void PDFAppManageAddPagesDialog::setPreviewSource(PDFAppToolInputList* list, const QString& inputId)
{
    m_previewList = list;
    m_previewInputId = inputId;
    m_previewModel->setInputList(list);
    updateState();
}

void PDFAppManageAddPagesDialog::updatePreviews(const std::vector<PDFInteger>& pages)
{
    // The tiles of the chosen pages in the order of the range (a page twice is two tiles, K23)
    const PDFAppToolInput* input = m_previewList && !m_previewInputId.isEmpty() ? m_previewList->findInput(m_previewInputId) : nullptr;
    const PDFCatalog* catalog = input && input->document ? input->document->getCatalog() : nullptr;
    std::vector<PDFAppInsertSourceModel::Row> rows;
    const size_t count = qMin(pages.size(), size_t(MAXIMUM_PREVIEW_PAGES));
    rows.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const PDFInteger page = pages[i];
        PDFAppInsertSourceModel::Row row;
        row.inputId = input ? m_previewInputId : QString();
        row.pageIndex = int(page);
        row.label = PDFAppLocale::formatNumber(page + 1);
        const PDFPage* pageObject = catalog && page >= 0 && size_t(page) < catalog->getPageCount() ? catalog->getPage(size_t(page)) : nullptr;
        row.pageSize = pageObject ? pageObject->getRotatedCropBox().size() : QSizeF();
        rows.push_back(std::move(row));
    }
    m_previewModel->setContent({ }, std::move(rows));
}

void PDFAppManageAddPagesDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::DevicePixelRatioChange && m_previewModel)
    {
        m_previewModel->setPictureSize(PREVIEW_SLOT, devicePixelRatioF());
    }
}

std::vector<PDFInteger> PDFAppManageAddPagesDialog::getPages() const
{
    std::vector<PDFInteger> pages;
    if (m_allRadio->isChecked())
    {
        for (PDFInteger page = 0; page < m_pageCount; ++page)
        {
            pages.push_back(page);
        }
        return pages;
    }
    return m_range->getError().isEmpty() ? m_range->getPages() : std::vector<PDFInteger>();
}

void PDFAppManageAddPagesDialog::updateState()
{
    m_range->setEnabled(m_rangeRadio->isChecked());
    const std::vector<PDFInteger> pages = getPages();
    const int count = int(qMin<size_t>(pages.size(), size_t(std::numeric_limits<int>::max())));
    updatePreviews(pages);
    if (m_rangeRadio->isChecked() && !m_range->getError().isEmpty())
    {
        setStatus(m_status, m_range->getError(), true);
    }
    else
    {
        // "33 of 36 pages · added after the selection" (387:15926)
        const QString where = m_isReplace ? tr("in place of the selected pages") : tr("added after the selection");
        setStatus(m_status, tr("%1 of %2 · %3").arg(PDFAppLocale::formatNumber(count), tr("%Ln pages", nullptr, int(qMin<PDFInteger>(m_pageCount, std::numeric_limits<int>::max()))), where), false);
    }
    if (QPushButton* button = getAcceptButton())
    {
        // "Add 33 Pages" (387:15926), "Replace with 33 Pages"
        const QString text = m_isReplace ? (count == 1 ? tr("Replace with 1 Page") : tr("Replace with %Ln Pages", nullptr, count))
                                         : (count == 1 ? tr("Add 1 Page") : tr("Add %Ln Pages", nullptr, count));
        button->setText(text);
        button->setIcon(PDFThemeIcons::getIcon(m_isReplace ? QStringLiteral("replace") : QStringLiteral("plus")));
    }
    setAcceptEnabled(count > 0);
}

void PDFAppManageAddPagesDialog::acceptDialog()
{
    Q_EMIT pagesChosen(m_sourceId, getPages(), m_isReplace);
}

// ------------------------------------------------------------- PDFAppManagePropertiesDialog

PDFAppManagePropertiesDialog::PDFAppManagePropertiesDialog(const std::vector<std::pair<QString, QString>>& rows, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(tr("Properties"), QString(), PDFAppManageDialogs::FORM_WIDTH, scrimTarget, parent)
{
    setObjectName(QStringLiteral("managePropertiesDialog"));

    // The rows in the muted card, Close (387:15926)
    if (QPushButton* close = getCancelButton())
    {
        close->setText(tr("Close"));
    }
    // facts 387:16947: bg/subtle, radius 10, no border
    QWidget* facts = PDFAppManageDialogsHelper::createFacts(this);
    facts->setObjectName(QStringLiteral("managePropertiesFacts"));
    m_values = new PDFKeyValueList(facts);
    m_values->setObjectName(QStringLiteral("managePropertiesValues"));
    for (const auto& [key, value] : rows)
    {
        m_values->addRow(key, value);
    }
    facts->layout()->addWidget(m_values);
    addContent(facts);
}

PDFAppManagePropertiesDialog::~PDFAppManagePropertiesDialog() = default;

void PDFAppManagePropertiesDialog::acceptDialog()
{
    // Nothing to decide
}

// ------------------------------------------------------------- PDFAppManageNumberDialog

PDFAppManageNumberDialog::PDFAppManageNumberDialog(Mode mode, int count, int value, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(mode == Mode::EveryPages ? tr("Regroup every N pages") : tr("Move to"),
              mode == Mode::EveryPages ? tr("Regroup") : tr("Move"),
              PDFAppManageDialogs::FORM_WIDTH, scrimTarget, parent),
    m_mode(mode),
    m_count(count)
{
    setObjectName(mode == Mode::EveryPages ? QStringLiteral("manageEveryDialog") : QStringLiteral("manageMoveDialog"));

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppManageDialogsHelper::GAP);

    // "Pages per group" and the field 140 wide at the trailing edge (387:15926)
    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(PDFAppPageWidgets::createLabel(mode == Mode::EveryPages ? tr("Pages per group") : tr("Before item"), QFont::Normal, 12, PDFThemeColors::TextSecondary, content), 1);
    m_spinBox = new PDFSpinBox(content);
    m_spinBox->setObjectName(QStringLiteral("manageNumberField"));
    m_spinBox->setFixedWidth(140);
    m_spinBox->setRange(1, qMax(1, mode == Mode::EveryPages ? count : count + 1));
    m_spinBox->setValue(qBound(1, value, m_spinBox->maximum()));
    row->addWidget(m_spinBox);
    layout->addLayout(row);
    m_status = createStatus();
    layout->addWidget(m_status);
    addContent(content);

    connect(m_spinBox.data(), &QSpinBox::valueChanged, this, &PDFAppManageNumberDialog::updateStatus);
    updateStatus();
}

PDFAppManageNumberDialog::~PDFAppManageNumberDialog() = default;

void PDFAppManageNumberDialog::updateStatus()
{
    if (m_mode == Mode::EveryPages)
    {
        setStatus(m_status, PDFAppManageDialogs::getEveryText(m_count, m_spinBox->value()), false);
    }
    else
    {
        const int value = m_spinBox->value();
        setStatus(m_status, value > m_count ? tr("The selection goes to the end") : tr("The selection goes before item %1").arg(PDFAppLocale::formatNumber(value)), false);
    }
    setAcceptEnabled(m_count > 0);
}

void PDFAppManageNumberDialog::acceptDialog()
{
    Q_EMIT numberChosen(m_spinBox->value());
}

// ------------------------------------------------------------- PDFAppManageTextDialog

PDFAppManageTextDialog::PDFAppManageTextDialog(Mode mode, const QString& title, const QString& acceptText, const QString& text, int pageCount, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(title, acceptText, PDFAppManageDialogs::FORM_WIDTH, scrimTarget, parent),
    m_mode(mode),
    m_pageCount(pageCount)
{
    setObjectName(mode == Mode::PageNumbers ? QStringLiteral("manageNumbersDialog") : QStringLiteral("manageNameDialog"));

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppManageDialogsHelper::GAP);
    layout->addWidget(PDFAppPageWidgets::createLabel(mode == Mode::PageNumbers ? tr("A new group starts at pages") : tr("Name"), QFont::Normal, 12, PDFThemeColors::TextSecondary, content));
    m_field = new PDFLineEdit(text, content);
    m_field->setObjectName(QStringLiteral("manageTextField"));
    m_field->setMaxLength(PDFDocumentAssembly::MAXIMUM_NAME_LENGTH);
    m_field->selectAll();
    layout->addWidget(m_field);
    if (mode == Mode::PageNumbers)
    {
        layout->addWidget(PDFAppPageWidgets::createLabel(tr("Positions in the selection, separated by commas."), QFont::Normal, 11, PDFThemeColors::TextTertiary, content));
    }
    m_status = createStatus();
    layout->addWidget(m_status);
    addContent(content);

    connect(m_field.data(), &QLineEdit::textChanged, this, &PDFAppManageTextDialog::updateStatus);
    connect(m_field.data(), &QLineEdit::returnPressed, this, &PDFAppManageTextDialog::triggerAccept);
    updateStatus();
}

PDFAppManageTextDialog::~PDFAppManageTextDialog() = default;

void PDFAppManageTextDialog::updateStatus()
{
    if (m_mode == Mode::Name)
    {
        m_status->setVisible(false);
        setAcceptEnabled(!m_field->text().trimmed().isEmpty());
        return;
    }

    // Owner answer 7 of 12. 11.: the empty field (the start) is no error - the line shows the
    // selection as one group, Regroup waits for the positions
    if (m_field->text().trimmed().isEmpty())
    {
        setStatus(m_status, PDFAppManageDialogs::getPositionsText(m_pageCount, std::vector<int>()), false);
        setAcceptEnabled(false);
        return;
    }
    QString error;
    const std::vector<int> positions = PDFAppManageDialogs::parsePositions(m_field->text(), m_pageCount, &error);
    setStatus(m_status, error.isEmpty() ? PDFAppManageDialogs::getPositionsText(m_pageCount, positions) : error, !error.isEmpty());
    setAcceptEnabled(error.isEmpty());
}

void PDFAppManageTextDialog::acceptDialog()
{
    Q_EMIT textChosen(m_mode == Mode::Name ? m_field->text().trimmed() : m_field->text());
}

// ------------------------------------------------------------- PDFAppManageCropDialog

PDFAppManageCropDialog::PDFAppManageCropDialog(const QMarginsF& marginsInPoints, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(tr("Crop pages"), tr("Crop"), PDFAppManageDialogs::DIALOG_WIDTH, scrimTarget, parent)
{
    using Helper = PDFAppManageDialogsHelper;

    setObjectName(QStringLiteral("manageCropDialog"));
    setText(tr("The margins are cut from the visible area of the selected pages."));

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Helper::GAP);

    // Left, top, right, bottom in millimeters (the margins of the page, never mirrored)
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(Helper::GAP);
    const QString labels[4] = { tr("Left"), tr("Top"), tr("Right"), tr("Bottom") };
    const double values[4] = { marginsInPoints.left(), marginsInPoints.top(), marginsInPoints.right(), marginsInPoints.bottom() };
    for (int i = 0; i < 4; ++i)
    {
        grid->addWidget(PDFAppPageWidgets::createLabel(labels[i], QFont::Normal, 12, PDFThemeColors::TextSecondary, content), i / 2, (i % 2) * 2);
        PDFDoubleSpinBox* field = new PDFDoubleSpinBox(content);
        field->setObjectName(QStringLiteral("manageCropField%1").arg(i));
        field->setRange(0.0, MAXIMUM_MARGIN);
        field->setDecimals(1);
        field->setSuffix(tr(" mm"));
        field->setValue(qBound(0.0, values[i] / Helper::POINTS_PER_MILLIMETER, MAXIMUM_MARGIN));
        grid->addWidget(field, i / 2, (i % 2) * 2 + 1);
        m_fields[i] = field;
    }
    layout->addLayout(grid);
    m_sameSource = new QCheckBox(tr("Apply to all pages of the same source"), content);
    m_sameSource->setObjectName(QStringLiteral("manageCropSameSource"));
    layout->addWidget(m_sameSource);
    addContent(content);
}

PDFAppManageCropDialog::~PDFAppManageCropDialog() = default;

PDFDoubleSpinBox* PDFAppManageCropDialog::getField(int index) const
{
    return index >= 0 && index < 4 ? m_fields[index].data() : nullptr;
}

QMarginsF PDFAppManageCropDialog::getMargins() const
{
    using Helper = PDFAppManageDialogsHelper;
    return QMarginsF(m_fields[0]->value() * Helper::POINTS_PER_MILLIMETER, m_fields[1]->value() * Helper::POINTS_PER_MILLIMETER,
                     m_fields[2]->value() * Helper::POINTS_PER_MILLIMETER, m_fields[3]->value() * Helper::POINTS_PER_MILLIMETER);
}

void PDFAppManageCropDialog::acceptDialog()
{
    Q_EMIT cropChosen(getMargins(), m_sameSource->isChecked());
}

// ------------------------------------------------------------- PDFAppManageSourcesDialog

PDFAppManageSourcesDialog::PDFAppManageSourcesDialog(PDFAppAssemblyLoadResult result, QWidget* scrimTarget, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, QString(), QString(), QDialogButtonBox::NoButton, parent),
    m_result(std::move(result))
{
    setObjectName(QStringLiteral("manageSourcesDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCardWidth(WIDTH);
    if (scrimTarget)
    {
        setScrimTarget(scrimTarget);
    }

    QWidget* content = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppManageDialogsHelper::GAP);
    m_rows = new QWidget(content);
    QVBoxLayout* rowsLayout = new QVBoxLayout(m_rows);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(PDFAppManageDialogsHelper::GAP);
    layout->addWidget(m_rows);
    PDFLabel* note = PDFAppPageWidgets::createLabel(tr("Pages of files that stay missing are left out and listed under Removed pages."), QFont::Normal, 11, PDFThemeColors::TextTertiary, content);
    note->setWordWrap(true);
    note->setMinimumWidth(1);
    layout->addWidget(note);
    addContent(content);

    // Cancel at the leading edge, Open Without Them and Open Assembly at the trailing edge
    m_cancelButton = addActionButton(tr("Cancel"), ActionPosition::Leading, false);
    m_cancelButton->setObjectName(QStringLiteral("manageSourcesCancel"));
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppManageSourcesDialog::onCancelClicked);
    m_withoutButton = addActionButton(tr("Open Without Them"), ActionPosition::Trailing, false);
    m_withoutButton->setObjectName(QStringLiteral("manageSourcesWithout"));
    connect(m_withoutButton.data(), &QPushButton::clicked, this, &PDFAppManageSourcesDialog::onOpenClicked);
    m_openButton = addActionButton(tr("Open Assembly"), ActionPosition::Trailing, true);
    m_openButton->setObjectName(QStringLiteral("manageSourcesOpen"));
    connect(m_openButton.data(), &QPushButton::clicked, this, &PDFAppManageSourcesDialog::onOpenClicked);
    updateRows();
}

PDFAppManageSourcesDialog::~PDFAppManageSourcesDialog() = default;

void PDFAppManageSourcesDialog::setResult(PDFAppAssemblyLoadResult result)
{
    // The accepted changed files stay accepted (the check of a located file is another source)
    for (const PDFAppAssemblySourceStatus& old : m_result.sources)
    {
        for (PDFAppAssemblySourceStatus& status : result.sources)
        {
            if (status.sourceId == old.sourceId && status.state == old.state && status.filePath == old.filePath)
            {
                status.isAccepted = status.isAccepted || old.isAccepted;
            }
        }
    }
    m_result = std::move(result);
    updateRows();
}

void PDFAppManageSourcesDialog::acceptChangedFile(const QString& sourceId)
{
    for (PDFAppAssemblySourceStatus& status : m_result.sources)
    {
        if (status.sourceId == sourceId && status.state == PDFAppAssemblySourceState::Changed)
        {
            status.isAccepted = true;
        }
    }
    updateRows();
}

QString PDFAppManageSourcesDialog::getStateText(const PDFAppAssemblySourceStatus& status, const PDFDocumentAssembly& assembly)
{
    PDFInteger oldCount = 0;
    for (const PDFDocumentAssembly::Source& source : assembly.getSources())
    {
        if (source.id == status.sourceId)
        {
            oldCount = source.pageCount;
        }
    }
    switch (status.state)
    {
        case PDFAppAssemblySourceState::Missing:
            // 387:16980 "Not found in C:\Tender"
            return tr("Not found in %1").arg(QDir::toNativeSeparators(QFileInfo(status.filePath).absolutePath()));
        case PDFAppAssemblySourceState::OnAnotherComputer:
            return tr("On another computer – %1").arg(QDir::toNativeSeparators(status.filePath));
        case PDFAppAssemblySourceState::Changed:
        {
            // "Changed on 2 Oct 2026 – now 5 pages instead of 6"
            const QString date = status.modified.isValid() ? PDFAppLocale::formatDate(status.modified.toLocalTime().date()) : QString();
            if (status.pageCount >= 0 && status.pageCount != oldCount)
            {
                return tr("Changed on %1 – now %2 instead of %3").arg(date, tr("%Ln pages", nullptr, int(qMin<PDFInteger>(status.pageCount, std::numeric_limits<int>::max()))), PDFAppLocale::formatNumber(oldCount));
            }
            return tr("Changed on %1").arg(date);
        }
        case PDFAppAssemblySourceState::Found:
            break;
    }
    return QString();
}

QString PDFAppManageSourcesDialog::getMeaningText(const PDFAppAssemblySourceStatus& status, const PDFDocumentAssembly& assembly)
{
    int index = -1;
    PDFInteger oldCount = 0;
    for (size_t i = 0; i < assembly.getSources().size(); ++i)
    {
        if (assembly.getSources()[i].id == status.sourceId)
        {
            index = int(i);
            oldCount = assembly.getSources()[i].pageCount;
        }
    }
    if (status.state == PDFAppAssemblySourceState::Changed && status.pageCount >= 0 && status.pageCount < oldCount)
    {
        // "Page 6 is no longer available"
        const PDFInteger first = status.pageCount + 1;
        return first == oldCount ? tr("Page %1 is no longer available").arg(PDFAppLocale::formatNumber(first))
                                 : tr("Pages %1–%2 are no longer available").arg(PDFAppLocale::formatNumber(first), PDFAppLocale::formatNumber(oldCount));
    }
    if (status.state == PDFAppAssemblySourceState::Changed)
    {
        return status.isAccepted ? tr("The changed file is used") : tr("Its pages stay at their places");
    }

    // "9 pages of the assembly use it"
    int uses = 0;
    for (const PDFDocumentAssembly::Item& item : assembly.getItems())
    {
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            uses += page.plan.input == index ? 1 : 0;
        }
    }
    return uses == 1 ? tr("1 page of the assembly uses it") : tr("%Ln pages of the assembly use it", nullptr, uses);
}

QPushButton* PDFAppManageSourcesDialog::getSourceButton(const QString& sourceId, const QString& action) const
{
    for (const QPointer<QPushButton>& button : m_sourceButtons)
    {
        if (button && button->property("sourceId").toString() == sourceId && button->property("action").toString() == action)
        {
            return button.data();
        }
    }
    return nullptr;
}

void PDFAppManageSourcesDialog::updateRows()
{
    // The rows are made again (an old button may be the sender of the click: later)
    for (const QPointer<QPushButton>& button : m_sourceButtons)
    {
        if (button)
        {
            button->disconnect(this);
        }
    }
    m_sourceButtons.clear();
    for (QWidget* child : m_rows->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        child->hide();
        child->deleteLater();
    }

    const PDFDocumentAssembly assembly = m_result.content ? m_result.content->assembly : PDFDocumentAssembly();
    int attention = 0;
    bool isEveryUsable = true;
    for (const PDFAppAssemblySourceStatus& status : m_result.sources)
    {
        if (status.state == PDFAppAssemblySourceState::Found)
        {
            continue;
        }
        isEveryUsable = isEveryUsable && status.isUsable();
        ++attention;

        int index = -1;
        QString name;
        for (size_t i = 0; i < assembly.getSources().size(); ++i)
        {
            if (assembly.getSources()[i].id == status.sourceId)
            {
                index = int(i);
                name = assembly.getSources()[i].name;
            }
        }

        // A row of 387:16980: the tile of the letter, the name, the state and its meaning, the button
        QWidget* row = new QWidget(m_rows);
        row->setObjectName(QStringLiteral("manageSourcesRow-") + status.sourceId);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 4, 0, 4);
        rowLayout->setSpacing(10);
        PDFLabel* letter = PDFAppPageWidgets::createLabel(PDFAppToolInputList::getLetter(qMax(0, index)), QFont::DemiBold, 11, PDFThemeColors::TextAccent, row);
        letter->setFixedSize(30, 30);
        letter->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(letter, 0, Qt::AlignTop);
        QVBoxLayout* texts = new QVBoxLayout();
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(1);
        texts->addWidget(PDFAppPageWidgets::createLabel(name, QFont::DemiBold, 12, PDFThemeColors::TextPrimary, row));
        PDFLabel* state = PDFAppPageWidgets::createLabel(getStateText(status, assembly), QFont::Normal, 11,
                                                         status.state == PDFAppAssemblySourceState::Changed ? PDFThemeColors::TextSecondary : PDFThemeColors::TextDanger, row);
        state->setObjectName(QStringLiteral("manageSourcesState"));
        state->setWordWrap(true);
        state->setMinimumWidth(1);
        texts->addWidget(state);
        PDFLabel* meaning = PDFAppPageWidgets::createLabel(getMeaningText(status, assembly), QFont::Normal, 11, PDFThemeColors::TextTertiary, row);
        meaning->setObjectName(QStringLiteral("manageSourcesMeaning"));
        texts->addWidget(meaning);
        rowLayout->addLayout(texts, 1);

        struct RowButton
        {
            const char* action;
            QString text;
            QString iconName;
        };
        std::vector<RowButton> buttons;
        if (status.state == PDFAppAssemblySourceState::Changed && !status.isAccepted)
        {
            buttons.push_back({ "useChanged", tr("Use Changed File"), QStringLiteral("replace") });
        }
        else if (status.state != PDFAppAssemblySourceState::Changed)
        {
            buttons.push_back({ "locate", tr("Locate…"), QStringLiteral("folder-open") });
            if (status.state == PDFAppAssemblySourceState::OnAnotherComputer)
            {
                // RB8: a file of another computer is opened only by this explicit choice
                buttons.push_back({ "openFromThere", tr("Open From There"), QStringLiteral("network") });
            }
        }
        for (const RowButton& item : buttons)
        {
            QPushButton* button = PDFAppPageWidgets::createButton(item.text, item.iconName, false, row);
            button->setObjectName(QStringLiteral("manageSources-") + QString::fromLatin1(item.action));
            button->setProperty("sourceId", status.sourceId);
            button->setProperty("action", QString::fromLatin1(item.action));
            connect(button, &QPushButton::clicked, this, &PDFAppManageSourcesDialog::onSourceButtonClicked);
            rowLayout->addWidget(button, 0, Qt::AlignVCenter);
            m_sourceButtons.push_back(button);
        }
        m_rows->layout()->addWidget(row);
    }

    // "2 source files need attention · The assembly Tender_2026 was saved on 28 Sep 2026."
    const QFileInfo info(m_result.filePath);
    setTitle(attention == 1 ? tr("1 source file needs attention") : tr("%Ln source files need attention", nullptr, attention));
    setText(tr("The assembly %1 was saved on %2.").arg(info.completeBaseName(), PDFAppLocale::formatDate(info.lastModified().date())));
    m_openButton->setEnabled(isEveryUsable && m_result.content.has_value());
    m_withoutButton->setEnabled(m_result.content.has_value());
}

void PDFAppManageSourcesDialog::onSourceButtonClicked()
{
    QObject* button = sender();
    if (!button)
    {
        return;
    }
    const QString sourceId = button->property("sourceId").toString();
    const QString action = button->property("action").toString();
    if (action == QLatin1String("useChanged"))
    {
        acceptChangedFile(sourceId);
    }
    else if (action == QLatin1String("locate"))
    {
        Q_EMIT locateRequested(sourceId);
    }
    else if (action == QLatin1String("openFromThere"))
    {
        Q_EMIT openFromThereRequested(sourceId);
    }
}

void PDFAppManageSourcesDialog::onOpenClicked()
{
    // Open Assembly and Open Without Them: the usable sources take their files, the others go to
    // the bin (PDFAppAssemblyLoader::createContent); the slot may delete this dialog
    QPointer<PDFAppManageSourcesDialog> guard(this);
    const PDFAppAssemblyLoadResult result = m_result;
    Q_EMIT openRequested(result);
    if (guard)
    {
        accept();
    }
}

void PDFAppManageSourcesDialog::onCancelClicked()
{
    reject();
}

}   // namespace pdf

#include "moc_pdfappmanagedialogs.cpp"
