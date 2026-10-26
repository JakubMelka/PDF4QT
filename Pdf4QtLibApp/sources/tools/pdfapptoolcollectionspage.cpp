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

#include "pdfapptoolcollectionspage.h"
#include "pdfappbuiltintoolcollections.h"
#include "pdfapphomedialogs.h"
#include "pdfapphomewidgets.h"
#include "pdfapplocale.h"
#include "pdfapptoolcollectioneditor.h"
#include "pdfapptoolpresentation.h"
#include "pdfapptoolstate.h"
#include "pdfdatabasenotifier.h"
#include "pdftoolcollectionrepository.h"
#include "pdftoolrepository.h"

#include "pdfdocumenttoolcatalog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the collections page
class PDFAppToolCollectionsPageHelper
{
public:
    PDFAppToolCollectionsPageHelper() = delete;

    /// Longest search text of Add a tool kept in the history
    static constexpr int MAXIMUM_SEARCH_LENGTH = 256;

    /// Line heights of the design (Inter, line height normal): the group label
    /// Semi Bold 11 and the title "Your collections" Semi Bold 15 (283:2343)
    static constexpr int GROUP_LABEL_LINE_HEIGHT = 13;
    static constexpr int TITLE_LINE_HEIGHT = 18;

    /// Label of a group of the list (group-label 283:2347): Semi Bold 11, letter
    /// spacing 4 %, capitals, text/tertiary, padding 8 / 0 / 4 / 10
    static PDFLabel* createGroupLabel(QWidget* parent)
    {
        PDFLabel* label = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 11, PDFThemeColors::TextTertiary, parent);
        QFont font = label->font();
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.44);
        font.setCapitalization(QFont::AllUppercase);
        label->setFont(font);
        label->setContentsMargins(10, 8, 0, 4);
        // Line 13 of the design: the label is 25 high (QLabel would take the line
        // spacing of the font, 14)
        label->setLineHeight(GROUP_LABEL_LINE_HEIGHT);
        return label;
    }
};

PDFAppDeleteCollectionDialog::PDFAppDeleteCollectionDialog(const QString& name, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, QString(), QString(), CARD_WIDTH, parent),
    m_name(name)
{
    setObjectName(QStringLiteral("deleteCollectionDialog"));
    m_deleteButton = addActionButton(QString(), false, true);
    m_deleteButton->setObjectName(QStringLiteral("deleteButton"));
    m_cancelButton = addActionButton(QString(), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_deleteButton.data(), &QPushButton::clicked, this, &PDFAppDeleteCollectionDialog::accept);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppDeleteCollectionDialog::reject);
    updateTexts();
}

PDFAppDeleteCollectionDialog::~PDFAppDeleteCollectionDialog() = default;

void PDFAppDeleteCollectionDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppDeleteCollectionDialog::updateTexts()
{
    // The texts keep the translation context of the page, which asked before (the
    // context is a literal, so lupdate finds the texts)
    setTitle(QCoreApplication::translate("pdf::PDFAppToolCollectionsPage", "Delete the collection “%1”?").arg(m_name));
    setText(QCoreApplication::translate("pdf::PDFAppToolCollectionsPage", "The tools stay in the catalog, only this collection is deleted. This cannot be undone."));
    if (m_deleteButton)
    {
        m_deleteButton->setText(QCoreApplication::translate("pdf::PDFAppToolCollectionsPage", "Delete Collection"));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(QCoreApplication::translate("pdf::PDFAppToolCollectionsPage", "Cancel"));
    }
}

PDFAppToolCollectionListRow::PDFAppToolCollectionListRow(QWidget* parent) :
    BaseClass(parent)
{
    // One collection of the list is the shown one (it does not react to the pointer)
    setSelectionBehavior(SelectionBehavior::ExclusiveStatic);
}

PDFAppToolCollectionListRow::~PDFAppToolCollectionListRow() = default;

void PDFAppToolCollectionListRow::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    // The shown collection is the state Pressed of the design (283:2369)
    PDFComponentPainter::State paintedState = state;
    if (isChecked())
    {
        paintedState.flags |= QStyle::State_Sunken;
    }
    BaseClass::paintContent(painter, rect, paintedState);
}

PDFAppToolCollectionsPage::PDFAppToolCollectionsPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("toolCollectionsPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // collections-grid 283:1939: the list 340 and the editor, gap 24; a narrow page
    // has the list above the editor
    m_columns = new PDFAppColumns(this);
    m_columns->setObjectName(QStringLiteral("collectionsGrid"));
    layout->addWidget(m_columns);

    buildList();
    m_columns->addColumn(m_listColumn, LIST_WIDTH, 0, 0, Qt::AlignTop);

    m_editor = new PDFAppToolCollectionEditor(context);
    m_columns->addColumn(m_editor, 0, 1, MINIMUM_EDITOR_WIDTH, Qt::AlignTop);
    layout->addStretch(1);

    connect(m_editor.data(), &PDFAppToolCollectionEditor::draftChanged, this, &PDFAppToolCollectionsPage::onDraftChanged);
    connect(m_editor.data(), &PDFAppToolCollectionEditor::saveRequested, this, &PDFAppToolCollectionsPage::onSaveRequested);
    connect(m_editor.data(), &PDFAppToolCollectionEditor::deleteRequested, this, &PDFAppToolCollectionsPage::onDeleteRequested);
    connect(m_editor.data(), &PDFAppToolCollectionEditor::leaveRequested, this, &PDFAppToolCollectionsPage::onLeaveRequested);
    connect(m_editor.data(), &PDFAppToolCollectionEditor::toolActivated, this, &PDFAppToolCollectionsPage::onToolActivated);

    // Notifications are joined into one refresh per turn of the event loop
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &PDFAppToolCollectionsPage::refresh);

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppToolCollectionsPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppToolCollectionsPage::onEverythingChanged);
    }

    retranslate();
}

PDFAppToolCollectionsPage::~PDFAppToolCollectionsPage()
{
    // Nothing comes back to the page, which is being destroyed
    if (PDFAppContext* context = getContext(); context && context->getNotifier())
    {
        context->getNotifier()->disconnect(this);
    }

    if (PDFAppCardDialog* dialog = m_deleteDialog.data())
    {
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }

    if (m_editor)
    {
        m_editor->disconnect(this);
    }
    for (const auto& item : m_rows)
    {
        if (item.second)
        {
            item.second->disconnect(this);
        }
    }
    if (m_newButton)
    {
        m_newButton->disconnect(this);
    }
}

bool PDFAppToolCollectionsPage::setRoute(const PDFAppRoute& route)
{
    const QStringList& segments = route.getSegments();
    if (!route.isValid() || segments.size() < 2 || segments.size() > 3 || !route.getFragment().isEmpty())
    {
        return false;
    }

    PDFAppContext* context = getContext();
    m_collections = PDFAppToolCollections::read(context ? context->getDatabase() : nullptr);
    if (m_collections.empty())
    {
        return false;
    }

    QStringList newTools;
    if (segments.size() == 2)
    {
        // /tools/collections is redirected by the route table; a page without a
        // collection shows the first one
        m_isNewCollection = false;
        m_collectionId = m_collections.front().routeId;
    }
    else if (segments[2] == QLatin1String("new"))
    {
        const QString toolId = route.getQueryValue(QStringLiteral("tool"));
        if (!toolId.isEmpty())
        {
            if (!PDFDocumentToolCatalog::isValidToolId(toolId))
            {
                return false;
            }
            newTools << toolId;
        }
        m_isNewCollection = true;
        m_collectionId.clear();
    }
    else
    {
        m_isNewCollection = false;
        m_collectionId = segments[2];
    }

    const PDFAppToolCollection* collection = nullptr;
    if (!m_isNewCollection)
    {
        collection = PDFAppToolCollections::find(m_collections, m_collectionId);
        if (!collection)
        {
            return false;
        }
    }

    if (m_editor)
    {
        m_editor->setCollection(collection, newTools);
    }

    m_isLeaving = false;
    updateList();
    refresh();
    return BaseClass::setRoute(route);
}

QString PDFAppToolCollectionsPage::getTitle() const
{
    return tr("Manage collections");
}

QString PDFAppToolCollectionsPage::getHeaderTitle() const
{
    return tr("Manage collections");
}

QString PDFAppToolCollectionsPage::getHeaderSubtitle() const
{
    return tr("Group the tools you use for one task. Collections stay on the Tools page.");
}

QList<PDFAppBreadcrumbItem> PDFAppToolCollectionsPage::getHeaderBreadcrumbs() const
{
    PDFAppBreadcrumbItem tools;
    tools.text = tr("Tools");
    tools.route = PDFAppRoutes::tools();

    PDFAppBreadcrumbItem collections;
    collections.text = tr("Collections");
    return { tools, collections };
}

PDFAppHeaderIcon PDFAppToolCollectionsPage::getHeaderIcon() const
{
    // title-row 283:1932: the Tool icon 44 of the collections (layers) in Neutral
    PDFAppHeaderIcon icon;
    icon.iconName = QStringLiteral("layers");
    icon.hue = PDFAccentHue::Neutral;
    return icon;
}

bool PDFAppToolCollectionsPage::hasSearchHeader() const
{
    return true;
}

QVariantMap PDFAppToolCollectionsPage::saveViewState() const
{
    QVariantMap state = BaseClass::saveViewState();
    if (m_editor)
    {
        // Q4: the draft is kept in the history entry, Back returns to it; O25: with
        // the identity of the stored collection, from which it started
        if (m_editor->isModified() || m_editor->isNewCollection())
        {
            QVariantMap draft = m_editor->getDraft().toVariantMap();
            if (!m_isNewCollection)
            {
                m_editor->getDraftIdentity().insertInto(draft);
            }
            state.insert(getDraftKey(), draft);
        }

        const QString searchText = m_editor->getSearchText();
        if (!searchText.isEmpty())
        {
            state.insert(getSearchKey(), searchText.left(PDFAppToolCollectionsPageHelper::MAXIMUM_SEARCH_LENGTH));
        }
    }
    return state;
}

void PDFAppToolCollectionsPage::restoreViewState(const QVariantMap& state)
{
    if (m_editor)
    {
        // Untrusted values: of the right types, sanitized by the draft
        const QVariant draftValue = state.value(getDraftKey());
        if (draftValue.typeId() == QMetaType::QVariantMap)
        {
            const QVariantMap draftMap = draftValue.toMap();
            if (const std::optional<PDFAppToolCollectionDraftState> draft = PDFAppToolCollectionDraftState::fromVariantMap(draftMap))
            {
                // O25: a draft of a collection, which has gone, is dropped; a draft of
                // a changed collection asks Reload / Keep my changes
                const PDFAppToolCollection* stored = m_isNewCollection ? nullptr : PDFAppToolCollections::find(m_collections, m_collectionId);
                if (m_editor->restoreDraft(*draft, PDFAppToolCollectionDraftIdentity::fromVariantMap(draftMap), stored))
                {
                    updateSuggestionData(false);
                }
            }
        }

        const QVariant searchValue = state.value(getSearchKey());
        if (searchValue.typeId() == QMetaType::QString)
        {
            m_editor->setSearchText(searchValue.toString().left(PDFAppToolCollectionsPageHelper::MAXIMUM_SEARCH_LENGTH));
        }
    }

    BaseClass::restoreViewState(state);
}

QStringList PDFAppToolCollectionsPage::getToolIds() const
{
    return m_editor ? m_editor->getDraft().toolIds : QStringList();
}

PDFAppToolCollectionListRow* PDFAppToolCollectionsPage::findListRow(const QString& routeId) const
{
    auto it = m_rows.find(routeId);
    return it != m_rows.end() ? it->second.data() : nullptr;
}

bool PDFAppToolCollectionsPage::saveCollection()
{
    PDFAppContext* context = getContext();
    if (!m_editor || !context || !context->getDatabase())
    {
        return false;
    }

    QString reason;
    if (!m_editor->canSave(&reason))
    {
        return false;
    }

    const PDFAppToolCollectionDraftState& state = m_editor->getDraft();
    PDFToolCollectionDraft draft;
    draft.name = state.name;
    draft.iconName = state.iconName;
    draft.toolIds = state.toolIds;
    const bool isNew = m_editor->isNewCollection();
    if (!isNew)
    {
        if (m_editor->isBuiltin())
        {
            draft.builtinId = m_editor->getBuiltinId();
        }
        else
        {
            draft.id = m_editor->getRowId();
        }
    }

    // One transaction: the check of the name, the collection and all its tools
    qint64 id = 0;
    bool isDuplicate = false;
    bool isLimit = false;
    PDFToolCollectionRepository repository = context->getToolCollections();
    const PDFDatabaseError error = repository.saveCollection(draft, &id, &isDuplicate, &isLimit);
    if (isDuplicate)
    {
        m_editor->setNameError(tr("A collection of this name exists already. Choose another name."));
        return false;
    }
    if (isLimit)
    {
        m_editor->setCustomCollectionCount(PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS);
        return false;
    }
    if (error.isError())
    {
        // A custom collection deleted by another instance meanwhile: Save creates it again
        if (!isNew && !m_editor->isBuiltin() && !repository.getCollection(m_editor->getRowId()))
        {
            // A draft becomes a new collection, which Save creates again; an
            // unchanged collection has nothing to keep (the refresh leaves the page)
            const bool isDraftKept = m_editor->setStoredCollection(nullptr);
            m_editor->setSaveError(isDraftKept ? tr("This collection was deleted in another window. Save it again to create it.")
                                               : tr("This collection was deleted in another window."));
            return false;
        }

        m_editor->setSaveError(tr("The collection could not be saved: %1").arg(PDFAppHomeFormat::toDisplayText(error.getMessage(), 200)));
        return false;
    }

    if (isNew)
    {
        // The new collection has its own route now (the entry of /new is replaced)
        m_isLeaving = true;
        return requestNavigation(PDFAppRoutes::toolCollection(PDFAppToolCollections::getCustomRouteId(id)), PDFAppNavigationMode::Replace);
    }

    // The editor shows the stored values (the name normalized by the repository)
    PDFAppContext* currentContext = getContext();
    m_collections = PDFAppToolCollections::read(currentContext ? currentContext->getDatabase() : nullptr);
    if (const PDFAppToolCollection* collection = PDFAppToolCollections::find(m_collections, m_collectionId))
    {
        const QString searchText = m_editor->getSearchText();
        m_editor->setCollection(collection);
        m_editor->setSearchText(searchText);
    }
    updateList();
    return true;
}

bool PDFAppToolCollectionsPage::requestDelete()
{
    if (m_deleteDialog)
    {
        m_deleteDialog->raise();
        return true;
    }

    if (!m_editor || m_editor->isNewCollection() || m_editor->isBuiltin() || m_editor->getRowId() <= 0)
    {
        return false;
    }

    QString name = PDFToolCollectionRepository::sanitizeName(m_editor->getBase().name);
    m_pendingDeleteId = m_editor->getRowId();

    // The stored row, which is confirmed: only this row is deleted (another instance
    // may delete it and reuse its id for a new collection meanwhile); a missing row
    // matches nothing
    m_pendingDeleteName.clear();
    m_pendingDeleteModified = -1;
    if (PDFAppContext* context = getContext(); context && context->getDatabase())
    {
        if (const std::optional<PDFToolCollectionRow> row = context->getToolCollections().getCollection(m_pendingDeleteId))
        {
            m_pendingDeleteName = row->name;
            m_pendingDeleteModified = row->modified;

            // The question names the row, which is deleted (another instance may have
            // renamed it, while the info bar of the change is shown)
            if (const QString storedName = PDFToolCollectionRepository::sanitizeName(row->name); !storedName.isEmpty())
            {
                name = storedName;
            }
        }
    }

    // The confirmation of a destructive action: Delete (Secondary, leading) and the
    // focused Cancel (Primary) - the pattern of Remove certificates
    PDFAppDeleteCollectionDialog* dialog = new PDFAppDeleteCollectionDialog(name, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppToolCollectionsPage::onDeleteDialogFinished);

    m_deleteDialog = dialog;
    dialog->open();
    return true;
}

QString PDFAppToolCollectionsPage::getListMetaText(const PDFAppToolCollection& collection, qint64 now)
{
    // 283:2346: the number of the tools; a changed collection says when ("edited yesterday")
    const QString count = PDFAppToolPresentation::getToolCountText(int(collection.toolIds.size()));
    if (collection.modified <= 0)
    {
        return count;
    }

    const QDateTime time = QDateTime::fromMSecsSinceEpoch(collection.modified, QTimeZone::LocalTime);
    const QDate date = time.date();
    if (!date.isValid())
    {
        return count;
    }

    QString edited;
    switch (PDFAppHomeFormat::getDateGroup(collection.modified, now))
    {
        case PDFAppHomeFormat::DateGroup::Today:
            edited = tr("edited today");
            break;

        case PDFAppHomeFormat::DateGroup::Yesterday:
            edited = tr("edited yesterday");
            break;

        default:
        {
            const QDate today = QDateTime::fromMSecsSinceEpoch(now, QTimeZone::LocalTime).date();
            //: Time of the last change of a collection of tools ("edited Sep 12")
            edited = tr("edited %1").arg(date.year() == today.year() ? PDFAppLocale::formatShortDate(date) : PDFAppLocale::formatDate(date));
            break;
        }
    }

    //: Meta text of a collection of tools: "4 tools · edited yesterday"
    return tr("%1 · %2").arg(count, edited);
}

void PDFAppToolCollectionsPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();

        // The names of the built-in collections are translated
        m_isLanguageRefresh = true;
        requestRefresh();
        Q_EMIT titleChanged();
    }
}

void PDFAppToolCollectionsPage::buildList()
{
    // collections-column 283:2341: the header, the list and New collection, gaps 12
    m_listColumn = new QWidget();
    m_listColumn->setObjectName(QStringLiteral("collectionsColumn"));
    QVBoxLayout* layout = new QVBoxLayout(m_listColumn);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // header 283:2342: "Your collections" Semi Bold 15, the counts Regular 12 text/muted, gap 12
    QWidget* header = new QWidget(m_listColumn);
    header->setObjectName(QStringLiteral("collectionsHeader"));
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);
    m_listTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 15, PDFThemeColors::TextPrimary, header);
    m_listTitle->setObjectName(QStringLiteral("collectionsTitle"));
    m_listTitle->setLineHeight(PDFAppToolCollectionsPageHelper::TITLE_LINE_HEIGHT);
    headerLayout->addWidget(m_listTitle, 0, Qt::AlignVCenter);
    headerLayout->addStretch(1);
    m_countHint = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, header);
    m_countHint->setObjectName(QStringLiteral("collectionsCount"));
    headerLayout->addWidget(m_countHint, 0, Qt::AlignVCenter);
    layout->addWidget(header);

    // collections-list 283:2346: a card, padding 8, gap 2
    m_listCard = PDFAppPageWidgets::createCard(8, 2, m_listColumn);
    m_listCard->setObjectName(QStringLiteral("collectionsList"));
    m_builtinLabel = PDFAppToolCollectionsPageHelper::createGroupLabel(m_listCard);
    m_builtinLabel->setObjectName(QStringLiteral("builtinGroup"));
    m_customLabel = PDFAppToolCollectionsPageHelper::createGroupLabel(m_listCard);
    m_customLabel->setObjectName(QStringLiteral("customGroup"));
    layout->addWidget(m_listCard);

    // btn-new-collection 283:2449: Secondary, the full width
    m_newButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("plus"), false, m_listColumn);
    m_newButton->setObjectName(QStringLiteral("newCollectionButton"));
    m_newButton->setFixedHeight(PDFAppToolCollectionEditor::BUTTON_HEIGHT);
    connect(m_newButton.data(), &QPushButton::clicked, this, &PDFAppToolCollectionsPage::onNewClicked);
    layout->addWidget(m_newButton);
}

void PDFAppToolCollectionsPage::refresh()
{
    m_refreshTimer.stop();
    if (m_isLeaving)
    {
        return;
    }

    ++m_refreshCount;
    PDFAppContext* context = getContext();
    PDFDatabase* database = context ? context->getDatabase() : nullptr;
    const PDFAppToolState state = PDFAppToolState::read(database, 0);
    m_collections = state.collections;
    m_isSuggestNextEnabled = state.isSuggestNextEnabled;
    updateList();

    int customCount = 0;
    for (const PDFAppToolCollection& collection : m_collections)
    {
        if (!collection.isBuiltin)
        {
            ++customCount;
        }
    }

    if (m_editor)
    {
        m_editor->setCustomCollectionCount(customCount);

        // The names of the other collections may have changed (a duplicate name)
        m_editor->refreshNameCheck();

        if (!m_isNewCollection)
        {
            const PDFAppToolCollection* collection = PDFAppToolCollections::find(m_collections, m_collectionId);
            if (m_isLanguageRefresh && collection)
            {
                m_editor->rebase(*collection);
            }
            else if (!m_editor->setStoredCollection(collection))
            {
                // Deleted by another instance without a change of the draft: the list
                // shows the first collection (the history entry is replaced)
                m_isLeaving = true;
                m_isLanguageRefresh = false;
                requestNavigation(PDFAppRoutes::toolCollections(), PDFAppNavigationMode::Replace);
                return;
            }
        }
    }

    m_isLanguageRefresh = false;
    updateSuggestionData(m_isPairsRefreshNeeded);
    m_isPairsRefreshNeeded = false;
}

void PDFAppToolCollectionsPage::requestRefresh()
{
    if (!m_isLeaving)
    {
        m_refreshTimer.start();
    }
}

void PDFAppToolCollectionsPage::updateList()
{
    if (!m_listCard)
    {
        return;
    }

    QVBoxLayout* layout = m_listCard->getLayout();
    const qint64 now = PDFAppHomeFormat::getCurrentTime();

    // Rows are reused by their collection; the layout is filled again in the order
    // of the collections: the built-in group, then the custom group
    std::map<QString, QPointer<PDFAppToolCollectionListRow>> rows;
    std::vector<PDFAppToolCollectionListRow*> builtinRows;
    std::vector<PDFAppToolCollectionListRow*> customRows;
    for (const PDFAppToolCollection& collection : m_collections)
    {
        PDFAppToolCollectionListRow* row = nullptr;
        auto it = m_rows.find(collection.routeId);
        if (it != m_rows.end() && it->second)
        {
            row = it->second.data();
            m_rows.erase(it);
        }
        else
        {
            row = new PDFAppToolCollectionListRow(m_listCard);
            row->setObjectName(QStringLiteral("collection-") + collection.routeId);
            row->setRouteId(collection.routeId);
            connect(row, &QAbstractButton::clicked, this, &PDFAppToolCollectionsPage::onListRowClicked);
        }

        row->setName(collection.name);
        row->setMeta(getListMetaText(collection, now));
        row->setIconName(collection.iconName);
        row->setChecked(!m_isNewCollection && collection.routeId == m_collectionId);
        rows[collection.routeId] = row;
        (collection.isBuiltin ? builtinRows : customRows).push_back(row);
    }

    for (const auto& item : m_rows)
    {
        if (item.second)
        {
            // Later: the row may be the sender of the current signal
            item.second->disconnect(this);
            item.second->hide();
            item.second->deleteLater();
        }
    }
    m_rows = std::move(rows);

    if (layout)
    {
        layout->removeWidget(m_builtinLabel);
        layout->addWidget(m_builtinLabel);
        for (PDFAppToolCollectionListRow* row : builtinRows)
        {
            layout->removeWidget(row);
            layout->addWidget(row);
            row->show();
        }

        layout->removeWidget(m_customLabel);
        layout->addWidget(m_customLabel);
        m_customLabel->setVisible(!customRows.empty());
        for (PDFAppToolCollectionListRow* row : customRows)
        {
            layout->removeWidget(row);
            layout->addWidget(row);
            row->show();
        }
    }

    if (m_countHint)
    {
        //: Counts of the collections of tools: "7 built-in · 2 custom"
        m_countHint->setText(tr("%1 built-in · %2 custom").arg(PDFAppLocale::formatNumber(qint64(builtinRows.size())), PDFAppLocale::formatNumber(qint64(customRows.size()))));
    }
}

void PDFAppToolCollectionsPage::updateSuggestionData(bool isForced)
{
    if (!m_editor)
    {
        return;
    }

    // The counters of the pairs of the tools of the draft (read again for other tools)
    const QStringList tools = m_editor->getDraft().toolIds;
    if (isForced || tools != m_pairTools)
    {
        PDFAppContext* context = getContext();
        m_pairTools = tools;
        m_pairs = tools.isEmpty() ? std::vector<PDFDocumentToolPair>() : PDFAppToolState::readPairs(context ? context->getDatabase() : nullptr, tools);
    }
    m_editor->setSuggestionData(m_pairs, m_isSuggestNextEnabled);
}

void PDFAppToolCollectionsPage::retranslate()
{
    if (m_listTitle)
    {
        m_listTitle->setText(tr("Your collections"));
    }
    if (m_builtinLabel)
    {
        m_builtinLabel->setText(tr("Built-in"));
    }
    if (m_customLabel)
    {
        m_customLabel->setText(tr("Custom"));
    }
    if (m_newButton)
    {
        m_newButton->setText(tr("New collection"));
    }
    if (m_listCard)
    {
        m_listCard->setAccessibleName(tr("Your collections"));
    }
}

void PDFAppToolCollectionsPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    // The page shows no pins and no use of the tools (review RC6-2): only the
    // collections, the pairs of the suggestions and the setting tools.suggestNext
    if (topic == PDFToolRepository::getPinTopic() || topic == PDFToolRepository::getUsageTopic())
    {
        return;
    }

    if (PDFAppToolState::isRelevantTopic(topic, key))
    {
        if (topic == PDFToolRepository::getPairTopic())
        {
            m_isPairsRefreshNeeded = true;
        }
        requestRefresh();
    }
}

void PDFAppToolCollectionsPage::onEverythingChanged()
{
    m_isPairsRefreshNeeded = true;
    requestRefresh();
}

void PDFAppToolCollectionsPage::onListRowClicked()
{
    PDFAppToolCollectionListRow* row = qobject_cast<PDFAppToolCollectionListRow*>(sender());
    if (!row)
    {
        return;
    }

    const QString routeId = row->getRouteId();
    if (!m_isNewCollection && routeId == m_collectionId)
    {
        row->setChecked(true);
        return;
    }

    // A new entry of the history (the draft of this page stays in its entry)
    requestNavigation(PDFAppRoutes::toolCollection(routeId), PDFAppNavigationMode::Push);
}

void PDFAppToolCollectionsPage::onNewClicked()
{
    requestNavigation(PDFAppRoutes::toolCollectionNew(), PDFAppNavigationMode::Push);
}

void PDFAppToolCollectionsPage::onDraftChanged()
{
    updateSuggestionData(false);
}

void PDFAppToolCollectionsPage::onSaveRequested()
{
    saveCollection();
}

void PDFAppToolCollectionsPage::onDeleteRequested()
{
    requestDelete();
}

void PDFAppToolCollectionsPage::onLeaveRequested()
{
    // The editor of a new (or deleted) collection is left: the entry is replaced by the list
    if (!m_isLeaving)
    {
        m_isLeaving = true;
        requestNavigation(PDFAppRoutes::toolCollections(), PDFAppNavigationMode::Replace);
    }
}

void PDFAppToolCollectionsPage::onToolActivated(const QString& toolId)
{
    if (PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        requestNavigation(PDFAppRoutes::tool(toolId), PDFAppNavigationMode::Push);
    }
}

void PDFAppToolCollectionsPage::onDeleteDialogFinished(int result)
{
    m_deleteDialog = nullptr;
    const qint64 id = m_pendingDeleteId;
    const QString expectedName = m_pendingDeleteName;
    const qint64 expectedModified = m_pendingDeleteModified;
    m_pendingDeleteId = 0;
    m_pendingDeleteName.clear();
    m_pendingDeleteModified = -1;

    PDFAppContext* context = getContext();
    if (result != QDialog::Accepted || id <= 0 || !context || !context->getDatabase())
    {
        return;
    }

    const PDFDatabaseError error = context->getToolCollections().deleteCollection(id, expectedName, expectedModified);
    if (error.isError())
    {
        // Deleted meanwhile by another instance: the result is the same
        if (context->getToolCollections().getCollection(id))
        {
            if (m_editor)
            {
                m_editor->setSaveError(tr("The collection could not be deleted: %1").arg(PDFAppHomeFormat::toDisplayText(error.getMessage(), 200)));
            }
            return;
        }
    }

    m_isLeaving = true;
    requestNavigation(PDFAppRoutes::toolCollections(), PDFAppNavigationMode::Replace);
}

PDFAppPage* PDFAppToolCollectionsPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppToolCollectionsPage(context, parent);
}

}   // namespace pdf
