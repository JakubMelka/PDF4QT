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

#include "pdfappopenpage.h"
#include "pdfappfiledrop.h"
#include "pdfapproute.h"
#include "pdfrepositoryhelper.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdfthemefonts.h"

#include <QBoxLayout>
#include <QMenu>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page Open
class PDFAppOpenPageHelper
{
public:
    PDFAppOpenPageHelper() = delete;

    static constexpr const char* PATH_PROPERTY = "pdf4qtPath";
    static constexpr const char* ID_PROPERTY = "pdf4qtId";

    /// Folder cards: three in a row at 782 px (253), fewer in a narrow page
    static constexpr int FOLDER_MINIMUM_WIDTH = 200;

    template<typename T>
    static QList<T*> toList(const QList<QPointer<T>>& pointers)
    {
        QList<T*> result;
        for (const QPointer<T>& pointer : pointers)
        {
            if (pointer)
            {
                result << pointer.data();
            }
        }
        return result;
    }

    template<typename T>
    static void deleteAll(QList<QPointer<T>>& pointers)
    {
        for (const QPointer<T>& pointer : std::as_const(pointers))
        {
            if (pointer)
            {
                pointer->hide();
                pointer->deleteLater();
            }
        }
        pointers.clear();
    }

    static PDFLabel* createEmptyLabel(const QString& text, QWidget* parent)
    {
        PDFLabel* label = new PDFLabel(text, PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, parent);
        label->setContentsMargins(12, 10, 12, 10);
        label->setWordWrap(true);
        return label;
    }
};

PDFAppOpenPage::PDFAppOpenPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("openPage"));

    // open-grid: main column (drop zone, folders, pinned and recent files) and the
    // side column 360 (workspaces, pinned locations), gap 24
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 0);
    m_columns = new PDFAppColumns(this);
    m_columns->setObjectName(QStringLiteral("openGrid"));
    m_columns->addColumn(0, 1, 400);
    m_columns->addColumn(360, 0, 0);
    layout->addWidget(m_columns);
    layout->addStretch(1);

    QBoxLayout* main = m_columns->getColumnLayout(0);
    QBoxLayout* side = m_columns->getColumnLayout(1);

    m_dropZone = new PDFAppDropZone(PDFAppDropZone::Variant::Large, m_columns->getColumn(0));
    m_dropZone->setShortcutText(getBrowseShortcutText());
    connect(m_dropZone, &PDFAppDropZone::browseRequested, this, &PDFAppOpenPage::onBrowseRequested);
    main->addWidget(m_dropZone);
    main->addWidget(createFolders(m_columns->getColumn(0)));

    PDFLabel* noEmpty = nullptr;
    m_pinnedViewAll = createLink(tr("View all"), PDFAppRoutes::recent(QStringLiteral("pinned")), m_columns->getColumn(0));
    m_pinnedViewAll->setObjectName(QStringLiteral("pinnedViewAll"));
    m_pinnedSection = createFileList(tr("Pinned Files"), m_pinnedViewAll, m_columns->getColumn(0), &m_pinnedCard, &m_pinnedLayout, &noEmpty, QString());
    m_pinnedSection->setObjectName(QStringLiteral("pinnedFiles"));
    main->addWidget(m_pinnedSection);

    PDFLabel* recentEmpty = nullptr;
    PDFTextLink* recentViewAll = createLink(tr("View all"), PDFAppRoutes::recent(), m_columns->getColumn(0));
    recentViewAll->setObjectName(QStringLiteral("recentViewAll"));
    m_recentSection = createFileList(tr("Recent Files"), recentViewAll, m_columns->getColumn(0), &m_recentCard, &m_recentLayout, &recentEmpty,
                                     tr("The documents you open appear here."));
    m_recentSection->setObjectName(QStringLiteral("recentFiles"));
    m_recentEmpty = recentEmpty;
    main->addWidget(m_recentSection);
    main->addStretch(1);

    side->addWidget(createWorkspaces(m_columns->getColumn(1)));
    side->addWidget(createLocations(m_columns->getColumn(1)));
    side->addStretch(1);
}

PDFAppOpenPage::~PDFAppOpenPage() = default;

bool PDFAppOpenPage::setRoute(const PDFAppRoute& route)
{
    if (!route.getQueryItems().isEmpty() || !route.getFragment().isEmpty())
    {
        return false;
    }
    return BaseClass::setRoute(route);
}

QString PDFAppOpenPage::getTitle() const
{
    return tr("Open");
}

QString PDFAppOpenPage::getHeaderTitle() const
{
    return tr("Open");
}

QBoxLayout* PDFAppOpenPage::getBannerLayout() const
{
    return m_columns ? m_columns->getMainLayout() : nullptr;
}

QString PDFAppOpenPage::getHeaderSubtitle() const
{
    return tr("Open a PDF document from your computer, a recent folder or a saved workspace.");
}

QWidget* PDFAppOpenPage::createFolders(QWidget* parent)
{
    // recent-folders: the header with the hint, the cards 3 in a row, gaps 12
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("recentFolders"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    PDFLabel* hint = PDFAppPageWidgets::createHint(tr("Standard folders first, then the folders you opened from recently"), section);
    hint->setObjectName(QStringLiteral("foldersHint"));
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Recent Folders"), hint, section));

    PDFAppGrid* grid = new PDFAppGrid(PDFAppOpenPageHelper::FOLDER_MINIMUM_WIDTH, 3, 12, 12, section);
    grid->setObjectName(QStringLiteral("folders"));
    m_foldersGrid = grid;
    layout->addWidget(grid);
    return section;
}

QWidget* PDFAppOpenPage::createFileList(const QString& title, PDFTextLink* link, QWidget* parent, QPointer<QWidget>* card, QPointer<QVBoxLayout>* layout,
                                        PDFLabel** emptyLabel, const QString& emptyText)
{
    // pinned-files / recent-files: the header with View all, the list card (padding 8, rows without gaps)
    QWidget* section = new QWidget(parent);
    QVBoxLayout* sectionLayout = PDFAppPageWidgets::createColumn(section, 12);
    sectionLayout->addWidget(PDFAppPageWidgets::createSectionHeader(title, link, section));

    QWidget* listCard = PDFAppPageWidgets::createCard(8, 0, section);
    listCard->setObjectName(QStringLiteral("fileList"));
    *card = listCard;
    *layout = qobject_cast<QVBoxLayout*>(listCard->layout());
    if (!emptyText.isEmpty())
    {
        *emptyLabel = PDFAppOpenPageHelper::createEmptyLabel(emptyText, listCard);
        (*emptyLabel)->setObjectName(QStringLiteral("emptyLabel"));
        (*layout)->addWidget(*emptyLabel);
    }
    sectionLayout->addWidget(listCard);
    return section;
}

QWidget* PDFAppOpenPage::createWorkspaces(QWidget* parent)
{
    // workspaces: the header with Manage, the list card (padding 8, gap 2)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("workspaces"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    PDFTextLink* manage = createLink(tr("Manage"), PDFAppRoutes::recent(QStringLiteral("workspaces")), section);
    manage->setObjectName(QStringLiteral("manageWorkspaces"));
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Workspaces"), manage, section));

    m_workspacesCard = PDFAppPageWidgets::createCard(8, 2, section);
    m_workspacesCard->setObjectName(QStringLiteral("workspacesList"));
    m_workspacesLayout = qobject_cast<QVBoxLayout*>(m_workspacesCard->layout());

    m_sessionRow = new PDFAppSessionRow(true, m_workspacesCard);
    m_sessionRow->hide();
    connect(m_sessionRow, &PDFAppSessionRow::restoreRequested, this, &PDFAppOpenPage::onSessionRestoreRequested);
    m_workspacesLayout->addWidget(m_sessionRow);

    m_saveWorkspaceButton = PDFAppPageWidgets::createButton(tr("Save open documents as workspace…"), QStringLiteral("save"), false, m_workspacesCard);
    m_saveWorkspaceButton->setObjectName(QStringLiteral("saveWorkspaceButton"));
    connect(m_saveWorkspaceButton, &QPushButton::clicked, this, &PDFAppOpenPage::onSaveWorkspaceClicked);
    m_workspacesLayout->addWidget(m_saveWorkspaceButton);
    layout->addWidget(m_workspacesCard);
    return section;
}

QWidget* PDFAppOpenPage::createLocations(QWidget* parent)
{
    // pinned-locations: the header with Add…, the list card (padding 8, gap 8)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("pinnedLocations"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    m_addLocationLink = new PDFTextLink(tr("Add…"), section);
    m_addLocationLink->setObjectName(QStringLiteral("addLocation"));
    connect(m_addLocationLink, &PDFTextLink::clicked, this, &PDFAppOpenPage::onAddLocationClicked);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Pinned Locations"), m_addLocationLink, section));

    m_locationsCard = PDFAppPageWidgets::createCard(8, 8, section);
    m_locationsCard->setObjectName(QStringLiteral("locationsList"));
    m_locationsLayout = qobject_cast<QVBoxLayout*>(m_locationsCard->layout());
    m_locationsEmpty = PDFAppOpenPageHelper::createEmptyLabel(tr("Pin a folder you use often with Add…"), m_locationsCard);
    m_locationsEmpty->setObjectName(QStringLiteral("locationsEmpty"));
    m_locationsLayout->addWidget(m_locationsEmpty);
    layout->addWidget(m_locationsCard);
    return section;
}

QList<PDFFolderCard*> PDFAppOpenPage::getFolderCards() const
{
    return PDFAppOpenPageHelper::toList(m_folderCards);
}

QList<PDFRecentFileRow*> PDFAppOpenPage::getPinnedFileRows() const
{
    return PDFAppOpenPageHelper::toList(m_pinnedRows);
}

QList<PDFRecentFileRow*> PDFAppOpenPage::getRecentFileRows() const
{
    return PDFAppOpenPageHelper::toList(m_recentRows);
}

QList<PDFWorkspaceRow*> PDFAppOpenPage::getWorkspaceRows() const
{
    return PDFAppOpenPageHelper::toList(m_workspaceRows);
}

QList<PDFFolderCard*> PDFAppOpenPage::getLocationCards() const
{
    return PDFAppOpenPageHelper::toList(m_locationCards);
}

void PDFAppOpenPage::refresh()
{
    // The hint of the drop zone shows the shortcut of Open… of the user
    if (m_dropZone)
    {
        m_dropZone->setShortcutText(getBrowseShortcutText());
    }
    refreshFolders();
    refreshFiles();
    refreshWorkspaces();
    refreshLocations();
}

void PDFAppOpenPage::refreshFolders()
{
    PDFAppContext* context = getContext();
    std::vector<PDFRecentFolder> folders;
    std::vector<PDFPinnedLocation> locations;
    if (context && context->getDatabase())
    {
        // The standard folders first, then the folders used recently
        folders = context->getRecent().getFolders();
        locations = context->getRecent().getPinnedLocations();
        if (folders.size() > size_t(MAXIMUM_FOLDERS))
        {
            folders.resize(size_t(MAXIMUM_FOLDERS));
        }
    }

    const qint64 now = getNow();
    QStringList signature;
    for (const PDFRecentFolder& folder : folders)
    {
        bool isPinned = false;
        for (const PDFPinnedLocation& location : locations)
        {
            isPinned = isPinned || PDFRepositoryHelper::isSamePath(location.path, folder.path);
        }
        // The times, not their texts: the minute ticker updates the texts (O25)
        signature << QStringLiteral("%1|%2|%3|%4|%5").arg(folder.path, folder.name).arg(int(folder.kind)).arg(folder.lastUsed).arg(isPinned);
    }

    const QString text = signature.join(QChar(0x1F));
    if (text == m_foldersSignature && m_folderCards.size() == qsizetype(folders.size()))
    {
        return;
    }
    m_foldersSignature = text;

    PDFAppGrid* grid = qobject_cast<PDFAppGrid*>(m_foldersGrid.data());
    if (!grid)
    {
        return;
    }

    PDFAppOpenPageHelper::deleteAll(m_folderCards);
    for (const PDFRecentFolder& folder : folders)
    {
        bool isPinned = false;
        for (const PDFPinnedLocation& location : locations)
        {
            isPinned = isPinned || PDFRepositoryHelper::isSamePath(location.path, folder.path);
        }

        PDFFolderCard* card = new PDFFolderCard(PDFAppHomeFormat::toDisplayText(folder.name), PDFAppHomeFormat::toDisplayPath(folder.path), QString(),
                                                PDFAppHomeFormat::getFolderHue(folder.kind), grid);
        PDFAppHomeFormat::setTimeText(card, PDFAppHomeFormat::TimeText::LastUsed, folder.lastUsed, now);
        card->setObjectName(QStringLiteral("folderCard"));
        card->setIconName(PDFAppHomeFormat::getFolderIconName(folder.kind));
        card->setProperty(PDFAppOpenPageHelper::PATH_PROPERTY, folder.path);

        PDFAppHomeItem item;
        item.kind = PDFAppHomeItemKind::Folder;
        item.path = folder.path;
        item.name = folder.name;
        item.isPinned = isPinned;
        item.isRemovable = folder.lastUsed > 0;
        card->setMenu(createItemMenu(item, card));
        connect(card, &PDFFolderCard::clicked, this, &PDFAppOpenPage::onFolderClicked);
        grid->addWidget(card);
        m_folderCards << card;
    }
    grid->updateCells();
}

PDFRecentFileRow* PDFAppOpenPage::createFileRow(const PDFRecentFile& file, QWidget* parent)
{
    // file row of Open: the name, the folder, the time, the star (24) and the menu
    PDFRecentFileRow* row = new PDFRecentFileRow(PDFAppHomeFormat::toDisplayText(file.name), PDFAppHomeFormat::toDisplayPath(file.folder), QString(), parent);
    PDFAppHomeFormat::setTimeText(row, PDFAppHomeFormat::TimeText::Time, file.lastOpened, getNow());
    row->setObjectName(QStringLiteral("fileRow"));
    row->setPinned(file.pinned);
    row->setProperty(PDFAppOpenPageHelper::PATH_PROPERTY, file.path);

    PDFAppHomeItem item;
    item.kind = PDFAppHomeItemKind::File;
    item.path = file.path;
    item.name = file.name;
    item.isPinned = file.pinned;
    row->setMenu(createItemMenu(item, row));
    connect(row, &PDFRecentFileRow::clicked, this, &PDFAppOpenPage::onFileClicked);
    connect(row, &PDFRecentFileRow::pinnedChanged, this, &PDFAppOpenPage::onFilePinnedChanged);
    requestPathInfo(file.path);
    updateFileStatus(row);
    return row;
}

void PDFAppOpenPage::refreshFiles()
{
    PDFAppContext* context = getContext();
    std::vector<PDFRecentFile> pinned;
    std::vector<PDFRecentFile> recent;
    int pinnedCount = 0;
    if (context && context->getDatabase())
    {
        PDFRecentRepository repository = context->getRecent();
        pinned = repository.getPinnedFiles();
        pinnedCount = int(pinned.size());
        if (pinned.size() > size_t(MAXIMUM_PINNED_FILES))
        {
            pinned.resize(size_t(MAXIMUM_PINNED_FILES));
        }

        PDFRecentQuery query;
        query.limit = MAXIMUM_RECENT_FILES;
        recent = repository.getFiles(query);
    }

    QStringList signature;
    for (const std::vector<PDFRecentFile>* list : { &pinned, &recent })
    {
        for (const PDFRecentFile& file : *list)
        {
            signature << QStringLiteral("%1|%2|%3|%4").arg(file.path, file.name).arg(file.lastOpened).arg(file.pinned);
        }
        signature << QStringLiteral("--");
    }
    signature << QString::number(pinnedCount);

    const QString text = signature.join(QChar(0x1F));
    if (text != m_filesSignature || m_pinnedRows.size() != qsizetype(pinned.size()) || m_recentRows.size() != qsizetype(recent.size()))
    {
        m_filesSignature = text;
        PDFAppOpenPageHelper::deleteAll(m_pinnedRows);
        PDFAppOpenPageHelper::deleteAll(m_recentRows);

        int index = 0;
        for (const PDFRecentFile& file : pinned)
        {
            PDFRecentFileRow* row = createFileRow(file, m_pinnedCard);
            m_pinnedLayout->insertWidget(index++, row);
            m_pinnedRows << row;
        }

        index = 0;
        for (const PDFRecentFile& file : recent)
        {
            PDFRecentFileRow* row = createFileRow(file, m_recentCard);
            m_recentLayout->insertWidget(index++, row);
            m_recentRows << row;
        }
    }

    // The pinned files are shown only, when there are some ("View all (12)")
    if (m_pinnedSection)
    {
        m_pinnedSection->setVisible(pinnedCount > 0);
    }
    if (m_pinnedViewAll)
    {
        m_pinnedViewAll->setText(tr("View all (%1)").arg(pinnedCount));
    }
    if (m_recentEmpty)
    {
        m_recentEmpty->setVisible(recent.empty());
    }
}

void PDFAppOpenPage::refreshWorkspaces()
{
    PDFAppContext* context = getContext();
    std::vector<PDFWorkspace> workspaces;
    std::optional<PDFWorkspace> session;
    if (context && context->getDatabase())
    {
        workspaces = context->getWorkspaces().getWorkspaces();
        session = context->getWorkspaces().getLastSession();
    }

    const qint64 now = getNow();
    if (m_sessionRow)
    {
        m_sessionRow->setVisible(session.has_value());
        if (session)
        {
            PDFAppHomeFormat::setTimeText(m_sessionRow, PDFAppHomeFormat::TimeText::SessionMeta, session->saved, now, session->documentCount);
        }
    }

    // The documents open in the window (its document tabs)
    const bool hasOpenDocuments = !PDFAppDocumentActions::getOpenDocuments(this).empty();
    if (m_saveWorkspaceButton)
    {
        m_saveWorkspaceButton->setEnabled(hasOpenDocuments);
        m_saveWorkspaceButton->setToolTip(hasOpenDocuments ? QString() : tr("Open some documents first, then save them as a workspace."));
    }

    QStringList signature;
    for (const PDFWorkspace& workspace : workspaces)
    {
        signature << QStringLiteral("%1|%2|%3|%4").arg(workspace.id).arg(workspace.name).arg(workspace.documentCount).arg(workspace.opened);
    }
    const QString text = signature.join(QChar(0x1F));
    if (text == m_workspacesSignature && m_workspaceRows.size() == qsizetype(workspaces.size()))
    {
        return;
    }
    m_workspacesSignature = text;

    PDFAppOpenPageHelper::deleteAll(m_workspaceRows);
    int index = m_workspacesLayout->indexOf(m_sessionRow) + 1;
    for (const PDFWorkspace& workspace : workspaces)
    {
        PDFWorkspaceRow* row = new PDFWorkspaceRow(PDFAppHomeFormat::toDisplayText(workspace.name), QString(), m_workspacesCard);
        PDFAppHomeFormat::setTimeText(row, PDFAppHomeFormat::TimeText::WorkspaceMeta, workspace.opened, now, workspace.documentCount);
        row->setObjectName(QStringLiteral("workspaceRow"));
        row->setProperty(PDFAppOpenPageHelper::ID_PROPERTY, workspace.id);

        PDFAppHomeItem item;
        item.kind = PDFAppHomeItemKind::Workspace;
        item.id = workspace.id;
        item.name = workspace.name;
        item.documentCount = workspace.documentCount;
        row->setMenu(createItemMenu(item, row));
        connect(row, &PDFWorkspaceRow::clicked, this, &PDFAppOpenPage::onWorkspaceClicked);
        m_workspacesLayout->insertWidget(index++, row);
        m_workspaceRows << row;
    }
}

void PDFAppOpenPage::refreshLocations()
{
    PDFAppContext* context = getContext();
    std::vector<PDFPinnedLocation> locations;
    if (context && context->getDatabase())
    {
        locations = context->getRecent().getPinnedLocations();
    }

    QStringList signature;
    for (const PDFPinnedLocation& location : locations)
    {
        signature << QStringLiteral("%1|%2|%3|%4").arg(location.id).arg(location.name, location.path).arg(int(location.kind));
    }
    const QString text = signature.join(QChar(0x1F));
    if (text != m_locationsSignature || m_locationCards.size() != qsizetype(locations.size()))
    {
        m_locationsSignature = text;
        PDFAppOpenPageHelper::deleteAll(m_locationCards);

        int index = 0;
        for (const PDFPinnedLocation& location : locations)
        {
            // location card: Folder card Neutral with the icon of the kind and the kind as the meta
            PDFFolderCard* card = new PDFFolderCard(PDFAppHomeFormat::toDisplayText(location.name), PDFAppHomeFormat::toDisplayPath(location.path),
                                                    PDFRecentRepository::getLocationKindText(location.kind), PDFAccentHue::Neutral, m_locationsCard);
            card->setObjectName(QStringLiteral("locationCard"));
            card->setIconName(PDFAppHomeFormat::getLocationIconName(location.kind));
            card->setProperty(PDFAppOpenPageHelper::PATH_PROPERTY, location.path);
            card->setProperty(PDFAppOpenPageHelper::ID_PROPERTY, location.id);

            PDFAppHomeItem item;
            item.kind = PDFAppHomeItemKind::Location;
            item.path = location.path;
            item.id = location.id;
            item.name = location.name;
            item.isPinned = true;
            card->setMenu(createItemMenu(item, card));
            connect(card, &PDFFolderCard::clicked, this, &PDFAppOpenPage::onFolderClicked);
            m_locationsLayout->insertWidget(index++, card);
            m_locationCards << card;
        }
    }

    if (m_locationsEmpty)
    {
        m_locationsEmpty->setVisible(locations.empty());
    }
}

void PDFAppOpenPage::onPathInfoChanged(const QString& path)
{
    for (const QList<QPointer<PDFRecentFileRow>>* rows : { &m_pinnedRows, &m_recentRows })
    {
        for (const QPointer<PDFRecentFileRow>& row : *rows)
        {
            if (row && row->property(PDFAppOpenPageHelper::PATH_PROPERTY).toString() == path)
            {
                updateFileStatus(row);
            }
        }
    }
}

void PDFAppOpenPage::updateFileStatus(PDFRecentFileRow* row)
{
    const std::optional<PDFAppFolderInfo> info = getPathInfo(row->property(PDFAppOpenPageHelper::PATH_PROPERTY).toString());
    const bool isMissing = info && info->pdfCount != -2 && !info->isFile;
    row->setStatus(isMissing ? PDFRecentFileRow::Status::Missing : PDFRecentFileRow::Status::Available);
}

void PDFAppOpenPage::onFileClicked()
{
    // The rows do not identify themselves in the signal: the path is their property
    const QObject* row = sender();
    if (row)
    {
        openFile(row->property(PDFAppOpenPageHelper::PATH_PROPERTY).toString());
    }
}

void PDFAppOpenPage::onFilePinnedChanged(bool isPinned)
{
    const QObject* row = sender();
    if (row)
    {
        setFilePinned(row->property(PDFAppOpenPageHelper::PATH_PROPERTY).toString(), isPinned);
    }
}

void PDFAppOpenPage::onFolderClicked()
{
    // A click on a folder or a location browses the files in it
    const QObject* card = sender();
    if (!card)
    {
        return;
    }

    const QString path = card->property(PDFAppOpenPageHelper::PATH_PROPERTY).toString();
    if (!path.isEmpty())
    {
        browseFiles(path);
    }
}

void PDFAppOpenPage::onWorkspaceClicked()
{
    const QObject* row = sender();
    if (row)
    {
        openWorkspace(row->property(PDFAppOpenPageHelper::ID_PROPERTY).toLongLong());
    }
}

void PDFAppOpenPage::onSessionRestoreRequested()
{
    restoreLastSession();
}

void PDFAppOpenPage::onSaveWorkspaceClicked()
{
    showSaveWorkspaceDialog();
}

void PDFAppOpenPage::onAddLocationClicked()
{
    addPinnedLocation();
}

void PDFAppOpenPage::onBrowseRequested()
{
    browseFiles();
}

PDFAppPage* PDFAppOpenPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppOpenPage(context, parent);
}

}   // namespace pdf
