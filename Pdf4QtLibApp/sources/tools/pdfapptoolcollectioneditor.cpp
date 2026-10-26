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

#include "pdfapptoolcollectioneditor.h"
#include "pdfappbuiltintoolcollections.h"
#include "pdfappcollectioniconchooser.h"
#include "pdfappcollectiontoollist.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpresentation.h"
#include "pdftoolcollectionrepository.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolsearch.h"
#include "pdffluentwidgets.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

/// Keys of the draft in the view state of the history entry
class PDFAppToolCollectionEditorHelper
{
public:
    PDFAppToolCollectionEditorHelper() = delete;

    static constexpr const char* NAME_KEY = "name";
    static constexpr const char* ICON_KEY = "icon";
    static constexpr const char* TOOLS_KEY = "tools";
    static constexpr const char* BUILTIN_ID_KEY = "builtinId";
    static constexpr const char* ROW_ID_KEY = "rowId";
    static constexpr const char* MODIFIED_KEY = "modified";
    static constexpr const char* NAME_FOLLOWS_BASE_KEY = "nameFollowsBase";

    static constexpr const char* CHANGED_INFO_BAR_ID = "collection-changed";
    static constexpr const char* DELETED_INFO_BAR_ID = "collection-deleted";
    static constexpr const char* RELOAD_ACTION = "reload";
    static constexpr const char* KEEP_ACTION = "keep";
    static constexpr const char* DISCARD_ACTION = "discard";

    /// Valid tools of the catalog without duplicates, at most the items, which the repository reads
    static QStringList sanitizeTools(const QStringList& toolIds)
    {
        QStringList tools;
        for (const QString& toolId : toolIds)
        {
            if (tools.size() >= PDFToolCollectionRepository::MAXIMUM_READ_ITEMS)
            {
                break;
            }
            if (PDFDocumentToolCatalog::isValidToolId(toolId) && !tools.contains(toolId))
            {
                tools << toolId;
            }
        }
        return tools;
    }

    /// Name of a draft: at most the length of the name field, without control characters
    static QString sanitizeName(const QString& name)
    {
        QString text = name.left(PDFToolCollectionRepository::MAXIMUM_EDITED_NAME_LENGTH * 2);
        for (QChar& character : text)
        {
            if (character.category() == QChar::Other_Control)
            {
                character = QLatin1Char(' ');
            }
        }
        return text.left(PDFToolCollectionRepository::MAXIMUM_EDITED_NAME_LENGTH);
    }

    /// Text of the user shown in a caption: sanitized and bounded (quoted by the caller)
    static QString getQuotedText(const QString& text)
    {
        static constexpr qsizetype MAXIMUM_QUOTED_LENGTH = 48;
        const QString sanitized = PDFSearchQuery::sanitize(text);
        return sanitized.size() > MAXIMUM_QUOTED_LENGTH ? sanitized.left(MAXIMUM_QUOTED_LENGTH - 1) + QChar(0x2026) : sanitized;
    }
};

QVariantMap PDFAppToolCollectionDraftState::toVariantMap() const
{
    QVariantMap map;
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::NAME_KEY), name);
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::ICON_KEY), iconName);
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::TOOLS_KEY), toolIds);
    return map;
}

std::optional<PDFAppToolCollectionDraftState> PDFAppToolCollectionDraftState::fromVariantMap(const QVariantMap& map)
{
    const QVariant name = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::NAME_KEY));
    const QVariant icon = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::ICON_KEY));
    const QVariant tools = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::TOOLS_KEY));
    if (name.typeId() != QMetaType::QString || icon.typeId() != QMetaType::QString || tools.typeId() != QMetaType::QStringList)
    {
        return std::nullopt;
    }

    PDFAppToolCollectionDraftState state;
    state.name = PDFAppToolCollectionEditorHelper::sanitizeName(name.toString());
    state.iconName = PDFAppBuiltinToolCollections::isValidIconName(icon.toString()) ? icon.toString() : PDFAppBuiltinToolCollections::getDefaultIconName();
    state.toolIds = PDFAppToolCollectionEditorHelper::sanitizeTools(tools.toStringList());
    return state;
}

void PDFAppToolCollectionDraftIdentity::insertInto(QVariantMap& map) const
{
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::BUILTIN_ID_KEY), builtinId);
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::ROW_ID_KEY), rowId);
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::MODIFIED_KEY), modified);
    map.insert(QString::fromLatin1(PDFAppToolCollectionEditorHelper::NAME_FOLLOWS_BASE_KEY), isNameFollowingBase);
}

std::optional<PDFAppToolCollectionDraftIdentity> PDFAppToolCollectionDraftIdentity::fromVariantMap(const QVariantMap& map)
{
    const QVariant builtinId = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::BUILTIN_ID_KEY));
    const QVariant rowId = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::ROW_ID_KEY));
    const QVariant modified = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::MODIFIED_KEY));
    const QVariant nameFollowsBase = map.value(QString::fromLatin1(PDFAppToolCollectionEditorHelper::NAME_FOLLOWS_BASE_KEY));
    if (builtinId.typeId() != QMetaType::QString || rowId.typeId() != QMetaType::LongLong || modified.typeId() != QMetaType::LongLong ||
        nameFollowsBase.typeId() != QMetaType::Bool)
    {
        return std::nullopt;
    }

    // A built-in id of the definitions only, no negative rows or times
    PDFAppToolCollectionDraftIdentity identity;
    identity.builtinId = builtinId.toString();
    identity.rowId = rowId.toLongLong();
    identity.modified = modified.toLongLong();
    identity.isNameFollowingBase = nameFollowsBase.toBool();
    if ((!identity.builtinId.isEmpty() && !PDFAppBuiltinToolCollections::findCollection(identity.builtinId)) || identity.rowId < 0 || identity.modified < 0)
    {
        return std::nullopt;
    }
    return identity;
}

PDFAppToolCollectionEditor::PDFAppToolCollectionEditor(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("collectionEditor"));

    // editor-column 284:2210: the header, the card and the note, gaps 12
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(12);
    m_layout->addWidget(createHeader());

    // The info bar of a change by another instance lies over its slot (its shadow
    // reaches out of it, as the export bar of Settings › Storage)
    m_infoBarSlot = new QWidget(this);
    m_infoBarSlot->setObjectName(QStringLiteral("infoBarSlot"));
    m_infoBarSlot->setFixedHeight(PDFAppInfoBar::CARD_HEIGHT);
    m_infoBarSlot->hide();
    m_infoBarSlot->installEventFilter(this);
    m_layout->addWidget(m_infoBarSlot);

    // editor 284:2218: padding 20 inside of the border, gaps 20
    m_card = PDFAppPageWidgets::createCard(20, 20, this);
    m_card->setObjectName(QStringLiteral("editorCard"));
    QVBoxLayout* cardLayout = m_card->getLayout();
    cardLayout->addWidget(createNameAndIcon(m_card));
    cardLayout->addWidget(createToolsSection(m_card));
    cardLayout->addWidget(createAddSection(m_card));

    m_saveErrorLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextDanger, m_card);
    m_saveErrorLabel->setObjectName(QStringLiteral("saveError"));
    m_saveErrorLabel->setTextFormat(Qt::PlainText);
    m_saveErrorLabel->setWordWrap(true);
    m_saveErrorLabel->hide();
    cardLayout->addWidget(m_saveErrorLabel);
    cardLayout->addWidget(createActions(m_card));
    m_layout->addWidget(m_card);

    // note 284:2463: Regular 11 text/muted
    m_noteLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextMuted, this);
    m_noteLabel->setObjectName(QStringLiteral("editorNote"));
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setLineHeight(CAPTION_LINE_HEIGHT);
    m_layout->addWidget(m_noteLabel);
    m_layout->addStretch(1);

    retranslate();
    setCollection(nullptr);
}

PDFAppToolCollectionEditor::~PDFAppToolCollectionEditor()
{
    // The children are deleted by QWidget: their signals do not reach a half
    // destroyed editor
    for (QObject* object : { static_cast<QObject*>(m_nameField.data()), static_cast<QObject*>(m_iconChooser.data()), static_cast<QObject*>(m_toolList.data()),
                             static_cast<QObject*>(m_searchField.data()), static_cast<QObject*>(m_infoBar.data()), static_cast<QObject*>(m_resetLink.data()),
                             static_cast<QObject*>(m_deleteButton.data()), static_cast<QObject*>(m_cancelButton.data()), static_cast<QObject*>(m_saveButton.data()) })
    {
        if (object)
        {
            object->disconnect(this);
        }
    }
    for (const Offer& offer : m_offers)
    {
        for (QObject* object : { static_cast<QObject*>(offer.toolRow.data()), static_cast<QObject*>(offer.addButton.data()) })
        {
            if (object)
            {
                object->disconnect(this);
            }
        }
    }
    if (m_infoBarSlot)
    {
        m_infoBarSlot->removeEventFilter(this);
    }
    if (m_searchField)
    {
        m_searchField->removeEventFilter(this);
    }
}

QWidget* PDFAppToolCollectionEditor::createHeader()
{
    // header 284:2211: the name Semi Bold 15, the tag Built-in, Reset to default at the trailing side, gap 10
    QWidget* header = new QWidget(this);
    header->setObjectName(QStringLiteral("editorHeader"));
    QHBoxLayout* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // One line elided at its end with the whole name as the tooltip (a name of 64 characters)
    m_titleLabel = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::DemiBold, 15), PDFThemeColors::TextPrimary, header);
    m_titleLabel->setObjectName(QStringLiteral("editorTitle"));
    m_titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);

    m_builtinTag = new PDFTag(header);
    m_builtinTag->setObjectName(QStringLiteral("builtinTag"));
    layout->addWidget(m_builtinTag, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    m_resetLink = new PDFTextLink(header);
    m_resetLink->setObjectName(QStringLiteral("resetLink"));
    connect(m_resetLink.data(), &QAbstractButton::clicked, this, &PDFAppToolCollectionEditor::onResetClicked);
    layout->addWidget(m_resetLink, 0, Qt::AlignVCenter);
    return header;
}

QWidget* PDFAppToolCollectionEditor::createNameAndIcon(QWidget* parent)
{
    // field-name 284:2220: the label Medium 12 text/secondary, the Text field 34,
    // the help Regular 11 text/muted, gaps 6
    QWidget* nameColumn = new QWidget(parent);
    nameColumn->setObjectName(QStringLiteral("fieldName"));
    QVBoxLayout* nameLayout = new QVBoxLayout(nameColumn);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(6);

    m_nameLabel = new PDFLabel(QString(), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextSecondary, nameColumn);
    m_nameLabel->setObjectName(QStringLiteral("nameLabel"));
    nameLayout->addWidget(m_nameLabel);

    m_nameField = new PDFLineEdit(nameColumn);
    m_nameField->setObjectName(QStringLiteral("nameField"));
    m_nameField->setMaxLength(PDFToolCollectionRepository::MAXIMUM_EDITED_NAME_LENGTH);
    m_nameLabel->setBuddy(m_nameField);
    connect(m_nameField.data(), &QLineEdit::textEdited, this, &PDFAppToolCollectionEditor::onNameEdited);
    nameLayout->addWidget(m_nameField);

    m_nameHelp = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, nameColumn);
    m_nameHelp->setObjectName(QStringLiteral("nameHelp"));
    m_nameHelp->setTextFormat(Qt::PlainText);
    m_nameHelp->setWordWrap(true);
    m_nameHelp->setLineHeight(CAPTION_LINE_HEIGHT);
    nameLayout->addWidget(m_nameHelp);
    nameLayout->addStretch(1);

    // field-icon 284:2225: the label and the ten icons
    QWidget* iconColumn = new QWidget(parent);
    iconColumn->setObjectName(QStringLiteral("fieldIcon"));
    QVBoxLayout* iconLayout = new QVBoxLayout(iconColumn);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    iconLayout->setSpacing(6);

    m_iconLabel = new PDFLabel(QString(), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextSecondary, iconColumn);
    m_iconLabel->setObjectName(QStringLiteral("iconLabel"));
    iconLayout->addWidget(m_iconLabel);

    m_iconChooser = new PDFAppCollectionIconChooser(iconColumn);
    connect(m_iconChooser.data(), &PDFAppCollectionIconChooser::iconChosen, this, &PDFAppToolCollectionEditor::onIconChosen);
    iconLayout->addWidget(m_iconChooser);
    iconLayout->addStretch(1);

    // name-and-icon 284:2219: gap 24; a narrow card has the icons below the name
    PDFAppResponsiveRow* row = new PDFAppResponsiveRow(nameColumn, iconColumn, NAME_AND_ICON_THRESHOLD, 24, parent);
    row->setObjectName(QStringLiteral("nameAndIcon"));
    return row;
}

QWidget* PDFAppToolCollectionEditor::createToolsSection(QWidget* parent)
{
    // tools-in-collection 284:2296: the header (Semi Bold 13 + the hint Regular 11
    // text/muted on the baseline, gap 10) and the list, gap 8
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("toolsInCollection"));
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QWidget* header = new QWidget(section);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);
    m_toolsTitle = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, header);
    m_toolsTitle->setObjectName(QStringLiteral("toolsTitle"));
    headerLayout->addWidget(m_toolsTitle, 0, Qt::AlignBaseline);
    m_toolsHint = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, header);
    m_toolsHint->setObjectName(QStringLiteral("toolsHint"));
    m_toolsHint->setWordWrap(true);
    headerLayout->addWidget(m_toolsHint, 1, Qt::AlignBaseline);
    layout->addWidget(header);

    m_toolList = new PDFAppCollectionToolList(section);
    connect(m_toolList.data(), &PDFAppCollectionToolList::toolsChanged, this, &PDFAppToolCollectionEditor::onToolsChanged);
    connect(m_toolList.data(), &PDFAppCollectionToolList::toolActivated, this, &PDFAppToolCollectionEditor::toolActivated);
    layout->addWidget(m_toolList);
    return section;
}

QWidget* PDFAppToolCollectionEditor::createAddSection(QWidget* parent)
{
    // add-tool 284:2381: the title, the search, the suggestions (gap 2), gaps 8
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("addTool"));
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_addTitle = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, section);
    m_addTitle->setObjectName(QStringLiteral("addTitle"));
    layout->addWidget(m_addTitle);

    m_searchField = new PDFLineEdit(section);
    m_searchField->setObjectName(QStringLiteral("addSearch"));
    m_searchField->setSearchField();
    m_searchField->setMaxLength(int(PDFSearchQuery::MAXIMUM_LENGTH));
    m_searchField->setClearButtonEnabled(true);
    m_searchField->installEventFilter(this);
    connect(m_searchField.data(), &QLineEdit::textChanged, this, &PDFAppToolCollectionEditor::onSearchTextChanged);
    connect(m_searchField.data(), &QLineEdit::returnPressed, this, &PDFAppToolCollectionEditor::onSearchReturnPressed);
    layout->addWidget(m_searchField);

    QWidget* offers = new QWidget(section);
    offers->setObjectName(QStringLiteral("offers"));
    QVBoxLayout* offersLayout = new QVBoxLayout(offers);
    offersLayout->setContentsMargins(0, 0, 0, 0);
    offersLayout->setSpacing(2);

    m_offerCaption = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, offers);
    m_offerCaption->setObjectName(QStringLiteral("offerCaption"));
    m_offerCaption->setTextFormat(Qt::PlainText);
    m_offerCaption->setWordWrap(true);
    m_offerCaption->setLineHeight(CAPTION_LINE_HEIGHT);
    offersLayout->addWidget(m_offerCaption);

    m_offerContainer = new QWidget(offers);
    m_offerContainer->setObjectName(QStringLiteral("offerList"));
    m_offerLayout = new QVBoxLayout(m_offerContainer);
    m_offerLayout->setContentsMargins(0, 0, 0, 0);
    m_offerLayout->setSpacing(2);
    offersLayout->addWidget(m_offerContainer);
    layout->addWidget(offers);
    return section;
}

QWidget* PDFAppToolCollectionEditor::createActions(QWidget* parent)
{
    // actions 284:2441: Delete collection, the space, Cancel and Save collection, gap 8;
    // a narrow card has Cancel and Save below Delete (no horizontal scroll bar)
    QWidget* leading = new QWidget(parent);
    leading->setObjectName(QStringLiteral("leadingActions"));
    QHBoxLayout* leadingLayout = new QHBoxLayout(leading);
    leadingLayout->setContentsMargins(0, 0, 0, 0);
    leadingLayout->setSpacing(8);

    m_deleteButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("trash"), false, leading);
    m_deleteButton->setObjectName(QStringLiteral("deleteButton"));
    m_deleteButton->setFixedHeight(BUTTON_HEIGHT);
    connect(m_deleteButton.data(), &QPushButton::clicked, this, &PDFAppToolCollectionEditor::onDeleteClicked);
    leadingLayout->addWidget(m_deleteButton);
    leadingLayout->addStretch(1);

    QWidget* trailing = new QWidget(parent);
    trailing->setObjectName(QStringLiteral("trailingActions"));
    QHBoxLayout* trailingLayout = new QHBoxLayout(trailing);
    trailingLayout->setContentsMargins(0, 0, 0, 0);
    trailingLayout->setSpacing(8);
    trailingLayout->addStretch(1);

    // Cancel has no icon (284:2451)
    m_cancelButton = new QPushButton(trailing);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_cancelButton->setAutoDefault(false);
    m_cancelButton->setFixedHeight(BUTTON_HEIGHT);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppToolCollectionEditor::onCancelClicked);
    trailingLayout->addWidget(m_cancelButton);

    m_saveButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("check"), true, trailing);
    m_saveButton->setObjectName(QStringLiteral("saveButton"));
    m_saveButton->setFixedHeight(BUTTON_HEIGHT);
    connect(m_saveButton.data(), &QPushButton::clicked, this, &PDFAppToolCollectionEditor::onSaveClicked);
    trailingLayout->addWidget(m_saveButton);

    PDFAppResponsiveRow* actions = new PDFAppResponsiveRow(leading, trailing, ACTIONS_THRESHOLD, 8, parent);
    actions->setObjectName(QStringLiteral("editorActions"));
    return actions;
}

void PDFAppToolCollectionEditor::setCollection(const PDFAppToolCollection* collection, const QStringList& newTools)
{
    m_pendingStored.reset();
    if (collection)
    {
        m_isNew = false;
        m_routeId = collection->routeId;
        m_builtinId = collection->builtinId;
        m_rowId = collection->rowId;
        m_baseRowId = collection->rowId;
        m_baseModified = collection->modified;
        m_base = getStoredState(*collection);
    }
    else
    {
        m_isNew = true;
        m_routeId.clear();
        m_builtinId.clear();
        m_rowId = 0;
        m_baseRowId = 0;
        m_baseModified = 0;
        m_base = PDFAppToolCollectionDraftState();
        m_base.iconName = PDFAppBuiltinToolCollections::getDefaultIconName();
        m_base.toolIds = PDFAppToolCollectionEditorHelper::sanitizeTools(newTools);
    }

    m_draft = m_base;
    m_isNameTouched = false;
    m_nameError.clear();
    m_saveError.clear();
    setInfoBarKind(InfoBarKind::None);
    applyDraftToWidgets();
    updateState();
    updateOffers();
}

void PDFAppToolCollectionEditor::setDraft(const PDFAppToolCollectionDraftState& draft)
{
    PDFAppToolCollectionDraftState state;
    state.name = PDFAppToolCollectionEditorHelper::sanitizeName(draft.name);
    state.iconName = PDFAppBuiltinToolCollections::isValidIconName(draft.iconName) ? draft.iconName : PDFAppBuiltinToolCollections::getDefaultIconName();
    state.toolIds = PDFAppToolCollectionEditorHelper::sanitizeTools(draft.toolIds);

    m_draft = state;
    m_isNameTouched = m_draft.name != m_base.name;
    applyDraftToWidgets();
    updateState();
    updateOffers();
}

PDFAppToolCollectionDraftIdentity PDFAppToolCollectionEditor::getDraftIdentity() const
{
    PDFAppToolCollectionDraftIdentity identity;
    identity.builtinId = m_builtinId;
    identity.rowId = m_baseRowId;
    identity.modified = m_baseModified;
    identity.isNameFollowingBase = m_draft.name == m_base.name;
    return identity;
}

bool PDFAppToolCollectionEditor::restoreDraft(const PDFAppToolCollectionDraftState& draft, const std::optional<PDFAppToolCollectionDraftIdentity>& identity,
                                              const PDFAppToolCollection* stored)
{
    // A new collection has nothing stored to compare with
    if (m_isNew)
    {
        setDraft(draft);
        return true;
    }

    // A draft of another collection than the shown one: the collection of the draft
    // has gone (a custom collection is compared by its row, a built-in one by its id)
    if (identity && (identity->builtinId != m_builtinId || (m_builtinId.isEmpty() && identity->rowId != m_baseRowId)))
    {
        return false;
    }

    // A name, which the user has not changed, follows the stored name (the name of
    // a built-in collection in the language of the user interface, review RC6-2)
    PDFAppToolCollectionDraftState state = draft;
    if (identity && identity->isNameFollowingBase)
    {
        state.name = m_base.name;
    }
    setDraft(state);

    // The collection has changed since the draft was kept (another instance, or its id
    // was reused by a new collection), or the draft has no identity: the user decides
    const bool isChanged = !identity || identity->rowId != m_baseRowId || identity->modified != m_baseModified;
    if (isChanged && isModified() && stored && stored->routeId == m_routeId)
    {
        m_pendingStored = *stored;
        setInfoBarKind(InfoBarKind::Changed);
        updateState();
    }
    return true;
}

void PDFAppToolCollectionEditor::refreshNameCheck()
{
    m_isDuplicateCheckValid = false;
    updateState();
}

bool PDFAppToolCollectionEditor::isModified() const
{
    return m_draft != m_base;
}

bool PDFAppToolCollectionEditor::setStoredCollection(const PDFAppToolCollection* collection)
{
    // A new collection is not stored yet (a collection deleted elsewhere keeps its draft)
    if (m_isNew)
    {
        return true;
    }

    if (!collection)
    {
        if (!isModified())
        {
            return false;
        }

        // The draft becomes a new collection, Save creates it again
        m_isNew = true;
        m_rowId = 0;
        m_pendingStored.reset();
        setInfoBarKind(InfoBarKind::Deleted);
        updateState();
        return true;
    }

    m_rowId = collection->rowId;
    const PDFAppToolCollectionDraftState stored = getStoredState(*collection);
    if (stored == m_base)
    {
        // The stored values are the base of the draft again (the other window has
        // changed the collection back): a reported change is no longer pending,
        // Reload would load stale values (review RC6-2). The change reported for a
        // draft restored from the history (restoreDraft, its base are the shown
        // values) is still the same collection: it stays pending.
        const bool isSamePending = m_pendingStored && m_pendingStored->rowId == collection->rowId && m_pendingStored->modified == collection->modified &&
                                   getStoredState(*m_pendingStored) == stored;
        if (m_pendingStored && !isSamePending)
        {
            m_pendingStored.reset();
            setInfoBarKind(InfoBarKind::None);
        }
        if (!m_pendingStored)
        {
            m_baseRowId = collection->rowId;
            m_baseModified = collection->modified;
        }
        updateState();
        return true;
    }

    if (!isModified())
    {
        // Nothing of the user is lost: the editor follows the other window
        const PDFAppToolCollection copy = *collection;
        setCollection(&copy);
        return true;
    }

    if (stored == m_draft)
    {
        // The other window saved the same values (or this editor itself)
        m_base = stored;
        m_baseRowId = collection->rowId;
        m_baseModified = collection->modified;
        m_pendingStored.reset();
        setInfoBarKind(InfoBarKind::None);
        updateState();
        return true;
    }

    m_pendingStored = *collection;
    setInfoBarKind(InfoBarKind::Changed);
    updateState();
    return true;
}

void PDFAppToolCollectionEditor::rebase(const PDFAppToolCollection& collection)
{
    if (m_isNew || collection.routeId != m_routeId)
    {
        return;
    }

    if (!isModified())
    {
        const PDFAppToolCollection copy = collection;
        setCollection(&copy);
        return;
    }

    // A name, which the user has not changed, follows the base (the translated name
    // of a built-in collection): Save would store the name of the old language as
    // a renamed collection otherwise (review RC6-2)
    const PDFAppToolCollectionDraftState base = getStoredState(collection);
    const bool isNameFollowed = m_draft.name == m_base.name && m_draft.name != base.name;
    m_rowId = collection.rowId;
    if (!m_pendingStored)
    {
        m_baseRowId = collection.rowId;
        m_baseModified = collection.modified;
    }
    m_base = base;
    if (isNameFollowed)
    {
        m_draft.name = base.name;
        applyDraftToWidgets();
    }
    updateState();
    if (isNameFollowed)
    {
        notifyDraftChanged();
    }
}

void PDFAppToolCollectionEditor::setSuggestionData(const std::vector<PDFDocumentToolPair>& pairs, bool isSuggestNextEnabled)
{
    if (m_pairs == pairs && m_isSuggestNextEnabled == isSuggestNextEnabled)
    {
        return;
    }

    m_pairs = pairs;
    m_isSuggestNextEnabled = isSuggestNextEnabled;
    updateOffers();
}

void PDFAppToolCollectionEditor::setCustomCollectionCount(int count)
{
    if (m_customCollectionCount != count)
    {
        m_customCollectionCount = count;
        updateState();
    }
}

void PDFAppToolCollectionEditor::setSaveError(const QString& error)
{
    m_saveError = error;
    updateState();
}

QString PDFAppToolCollectionEditor::getSaveError() const
{
    return m_saveErrorLabel && !m_saveErrorLabel->isHidden() ? m_saveErrorLabel->text() : QString();
}

void PDFAppToolCollectionEditor::setNameError(const QString& error)
{
    m_nameError = error;
    updateState();
}

QString PDFAppToolCollectionEditor::getNameMessage() const
{
    return m_nameHelp ? m_nameHelp->text() : QString();
}

bool PDFAppToolCollectionEditor::isNameInvalid() const
{
    return m_nameField && m_nameField->isInvalid();
}

QString PDFAppToolCollectionEditor::getSearchText() const
{
    return m_searchField ? m_searchField->text() : QString();
}

void PDFAppToolCollectionEditor::setSearchText(const QString& text)
{
    if (m_searchField)
    {
        m_searchField->setText(text.left(m_searchField->maxLength()));
    }
}

bool PDFAppToolCollectionEditor::canSave(QString* reason) const
{
    QString text;
    if (PDFToolCollectionRepository::normalizeName(m_draft.name).isEmpty())
    {
        text = tr("Enter a name of the collection.");
    }
    else if (!m_nameError.isEmpty())
    {
        text = m_nameError;
    }
    else if (const QString duplicate = getDuplicateNameError(); !duplicate.isEmpty())
    {
        text = duplicate;
    }
    else if (m_draft.toolIds.isEmpty())
    {
        text = tr("Add at least one tool.");
    }
    else if (m_isNew && m_customCollectionCount >= PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS)
    {
        text = tr("You have %1 collections, the most PDF4QT keeps. Delete a collection to create another one.").arg(PDFAppLocale::formatNumber(PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS));
    }

    if (reason)
    {
        *reason = text;
    }
    return text.isEmpty();
}

void PDFAppToolCollectionEditor::resetToDefault()
{
    const PDFAppBuiltinToolCollection* builtin = PDFAppBuiltinToolCollections::findCollection(m_builtinId);
    if (!builtin)
    {
        return;
    }

    m_draft.name = builtin->getName();
    m_draft.iconName = builtin->getIconName();
    m_draft.toolIds = PDFAppToolCollectionEditorHelper::sanitizeTools(builtin->getToolIds());
    m_nameError.clear();
    m_saveError.clear();
    applyDraftToWidgets();
    updateState();
    updateOffers();
    notifyDraftChanged();
}

void PDFAppToolCollectionEditor::revert()
{
    m_draft = m_base;
    m_isNameTouched = false;
    m_nameError.clear();
    m_saveError.clear();
    applyDraftToWidgets();
    updateState();
    updateOffers();
    notifyDraftChanged();
}

bool PDFAppToolCollectionEditor::addTool(const QString& toolId)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId) || m_draft.toolIds.contains(toolId) ||
        m_draft.toolIds.size() >= PDFToolCollectionRepository::MAXIMUM_READ_ITEMS)
    {
        return false;
    }

    m_draft.toolIds << toolId;
    m_saveError.clear();
    applyDraftToWidgets();
    updateState();
    updateOffers();

    QPointer<PDFAppToolCollectionEditor> guard(this);
    Q_EMIT draftChanged();
    return !guard.isNull();
}

QStringList PDFAppToolCollectionEditor::getOfferedTools() const
{
    QStringList tools;
    for (const Offer& offer : m_offers)
    {
        if (offer.widget)
        {
            tools << offer.toolId;
        }
    }
    return tools;
}

PDFToolRow* PDFAppToolCollectionEditor::findOfferedToolRow(const QString& toolId) const
{
    for (const Offer& offer : m_offers)
    {
        if (offer.toolId == toolId && offer.widget)
        {
            return offer.toolRow.data();
        }
    }
    return nullptr;
}

PDFIconButton* PDFAppToolCollectionEditor::findAddButton(const QString& toolId) const
{
    for (const Offer& offer : m_offers)
    {
        if (offer.toolId == toolId && offer.widget)
        {
            return offer.addButton.data();
        }
    }
    return nullptr;
}

bool PDFAppToolCollectionEditor::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (watched == m_infoBarSlot && (type == QEvent::Move || type == QEvent::Resize || type == QEvent::Show || type == QEvent::Hide))
    {
        updateInfoBarGeometry();
    }
    else if (watched == m_searchField && type == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Down)
    {
        // Down goes from the search to the first offered tool
        for (const Offer& offer : m_offers)
        {
            if (offer.toolRow)
            {
                offer.toolRow->setFocus(Qt::TabFocusReason);
                return true;
            }
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolCollectionEditor::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppToolCollectionEditor::retranslate()
{
    // The translated names of the built-in collections and the text of the error
    m_isDuplicateCheckValid = false;

    if (m_builtinTag)
    {
        m_builtinTag->setText(tr("Built-in"));
    }
    if (m_resetLink)
    {
        m_resetLink->setText(tr("Reset to default"));
    }
    if (m_nameLabel)
    {
        m_nameLabel->setText(tr("Name"));
    }
    if (m_nameField)
    {
        m_nameField->setAccessibleName(tr("Name of the collection"));
    }
    if (m_iconLabel)
    {
        m_iconLabel->setText(tr("Icon"));
    }
    if (m_toolsTitle)
    {
        m_toolsTitle->setText(tr("Tools in this collection"));
    }
    if (m_addTitle)
    {
        m_addTitle->setText(tr("Add a tool"));
    }
    if (m_searchField)
    {
        m_searchField->setPlaceholderText(tr("Search the catalog by name, action or keyword…"));
        m_searchField->setAccessibleName(tr("Search the catalog"));
    }
    if (m_deleteButton)
    {
        m_deleteButton->setText(tr("Delete collection"));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
    if (m_saveButton)
    {
        m_saveButton->setText(tr("Save collection"));
    }
    if (m_noteLabel)
    {
        m_noteLabel->setText(tr("Built-in collections can be edited; Reset to default restores the original tools. Delete is available for custom collections only."));
    }

    // The titles of the tools and the texts of the info bar
    for (const Offer& offer : m_offers)
    {
        if (offer.widget)
        {
            offer.widget->hide();
            offer.widget->deleteLater();
        }
    }
    m_offers.clear();
    updateInfoBar();
    updateState();
    updateOffers();
}

void PDFAppToolCollectionEditor::applyDraftToWidgets()
{
    m_isUpdating = true;
    if (m_nameField && m_nameField->text() != m_draft.name)
    {
        m_nameField->setText(m_draft.name);
    }
    if (m_iconChooser)
    {
        m_iconChooser->setIconName(m_draft.iconName);
    }
    if (m_toolList)
    {
        m_toolList->setTools(m_draft.toolIds);
    }
    m_isUpdating = false;
}

void PDFAppToolCollectionEditor::updateState()
{
    // The header: the name of the draft (the stored name, while it is empty)
    if (m_titleLabel)
    {
        QString title = PDFToolCollectionRepository::sanitizeName(m_draft.name).simplified();
        if (title.isEmpty())
        {
            title = PDFToolCollectionRepository::sanitizeName(m_base.name).simplified();
        }
        if (title.isEmpty())
        {
            title = tr("New collection");
        }
        m_titleLabel->setHeaderText(title);
    }

    const PDFAppBuiltinToolCollection* builtin = PDFAppBuiltinToolCollections::findCollection(m_builtinId);
    const bool isBuiltin = builtin && !m_isNew;
    if (m_builtinTag)
    {
        m_builtinTag->setVisible(isBuiltin);
    }
    if (m_resetLink)
    {
        // 284:2216: the link of a built-in collection is always offered (an unchanged
        // collection is reset to the same values)
        m_resetLink->setVisible(isBuiltin);
    }

    // The name: the help, or the error of a duplicate / an empty name
    QString nameError = m_nameError;
    if (nameError.isEmpty())
    {
        nameError = getDuplicateNameError();
    }
    if (nameError.isEmpty() && m_isNameTouched && PDFToolCollectionRepository::normalizeName(m_draft.name).isEmpty())
    {
        nameError = tr("Enter a name of the collection.");
    }
    if (m_nameField)
    {
        m_nameField->setInvalid(!nameError.isEmpty(), nameError);
    }
    if (m_nameHelp)
    {
        m_nameHelp->setText(nameError.isEmpty() ? tr("Shown on the Tools page. Keep it short.") : nameError);
        m_nameHelp->setTextColor(nameError.isEmpty() ? PDFThemeColors::TextMuted : PDFThemeColors::TextDanger);
    }

    if (m_toolsHint)
    {
        //: Hint of the tools of a collection: "4 tools · drag to reorder · shown in this order"
        m_toolsHint->setText(tr("%1 · drag to reorder · shown in this order").arg(PDFAppToolPresentation::getToolCountText(int(m_draft.toolIds.size()))));
    }

    if (m_deleteButton)
    {
        m_deleteButton->setEnabled(!m_isNew && !isBuiltin && m_rowId > 0);
    }

    QString reason;
    const bool isSaveEnabled = canSave(&reason);
    if (m_saveButton)
    {
        m_saveButton->setEnabled(isSaveEnabled);
        m_saveButton->setToolTip(reason);
    }

    // The limit of the collections is an error of Save shown at once
    QString saveError = m_saveError;
    if (saveError.isEmpty() && m_isNew && m_customCollectionCount >= PDFToolCollectionRepository::MAXIMUM_CUSTOM_COLLECTIONS)
    {
        saveError = reason;
    }
    if (m_saveErrorLabel)
    {
        m_saveErrorLabel->setText(saveError);
        m_saveErrorLabel->setVisible(!saveError.isEmpty());
    }
}

void PDFAppToolCollectionEditor::updateOffers()
{
    if (!m_offerLayout || !m_offerContainer)
    {
        return;
    }

    // The search of the catalog without the tools of the collection, or the suggestions
    struct OfferData
    {
        QString toolId;
        QString description;
    };
    std::vector<OfferData> offers;
    QString caption;

    const PDFSearchQuery query(getSearchText());
    if (!query.isEmpty())
    {
        int matches = 0;
        for (const PDFDocumentToolSearchMatch& match : PDFDocumentToolSearch::search(query))
        {
            if (!match.tool)
            {
                continue;
            }
            ++matches;
            const QString toolId = match.tool->getId();
            if (m_draft.toolIds.contains(toolId) || int(offers.size()) >= MAXIMUM_RESULTS)
            {
                continue;
            }

            // Q3: a tool found by a keyword says why
            const QString description = match.field == PDFDocumentToolSearchMatch::Field::Keyword && !match.keyword.isEmpty() ?
                                            PDFAppToolPresentation::getMatchReasonText(match.keyword) : match.tool->getDescription();
            offers.push_back({ toolId, description });
        }

        const QString quoted = PDFAppToolCollectionEditorHelper::getQuotedText(getSearchText());
        if (!offers.empty())
        {
            caption = tr("Tools matching “%1”").arg(quoted);
        }
        else if (matches > 0)
        {
            caption = tr("Every tool matching “%1” is in the collection already.").arg(quoted);
        }
        else
        {
            caption = tr("No tools match “%1”.").arg(quoted);
        }
    }
    else
    {
        for (const QString& toolId : computeSuggestions())
        {
            if (const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId))
            {
                offers.push_back({ toolId, tool->getDescription() });
            }
        }
        caption = offers.empty() ? QString() : tr("Suggested for this collection");
    }

    if (m_offerCaption)
    {
        m_offerCaption->setText(caption);
        m_offerCaption->setVisible(!caption.isEmpty());
    }

    // The rows are built again only for other tools or texts (the focus stays)
    bool isSame = offers.size() == m_offers.size();
    for (size_t i = 0; isSame && i < offers.size(); ++i)
    {
        isSame = m_offers[i].toolId == offers[i].toolId && m_offers[i].toolRow && m_offers[i].toolRow->getDescription() == offers[i].description;
    }
    if (isSame)
    {
        return;
    }

    const bool hadFocus = m_offerContainer->isAncestorOf(QApplication::focusWidget());
    for (const Offer& offer : m_offers)
    {
        if (offer.widget)
        {
            // Later: a button of the row may be the sender of the current click
            offer.widget->hide();
            offer.widget->deleteLater();
        }
    }
    m_offers.clear();

    for (const OfferData& offerData : offers)
    {
        const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(offerData.toolId);
        if (!tool)
        {
            continue;
        }

        // suggest-<name> 284:2389: the Tool row without the star and the add button, gap 6, padding 6 at the trailing side
        Offer offer;
        offer.toolId = offerData.toolId;
        offer.widget = new QWidget(m_offerContainer);
        offer.widget->setObjectName(QStringLiteral("offer-") + offerData.toolId);
        QHBoxLayout* layout = new QHBoxLayout(offer.widget);
        layout->setContentsMargins(0, 0, PDFAppCollectionToolList::ROW_TRAILING_PADDING, 0);
        layout->setSpacing(PDFAppCollectionToolList::ROW_SPACING);

        offer.toolRow = PDFAppToolPresentation::createToolRow(*tool, false, offer.widget);
        offer.toolRow->setStarVisible(false);
        offer.toolRow->setDescription(offerData.description);
        offer.toolRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        offer.toolRow->setToolTip(tr("Add to the collection"));
        connect(offer.toolRow.data(), &QAbstractButton::clicked, this, &PDFAppToolCollectionEditor::onOfferClicked);
        layout->addWidget(offer.toolRow, 1);

        offer.addButton = new PDFIconButton(QStringLiteral("plus-circle"), PDFIconButton::Size::Medium, offer.widget);
        offer.addButton->setObjectName(QStringLiteral("addToolButton"));
        offer.addButton->setIconColor(PDFThemeColors::IconAccent);
        offer.addButton->setProperty(PDFAppToolPresentation::TOOL_PROPERTY, offerData.toolId);
        offer.addButton->setAccessibleName(tr("Add %1").arg(tool->getTitle()));
        offer.addButton->setToolTip(tr("Add to the collection"));
        connect(offer.addButton.data(), &QAbstractButton::clicked, this, &PDFAppToolCollectionEditor::onOfferClicked);
        layout->addWidget(offer.addButton, 0, Qt::AlignVCenter);

        m_offerLayout->addWidget(offer.widget);
        offer.widget->show();
        m_offers.push_back(offer);
    }

    if (hadFocus)
    {
        // The focus of a removed offer goes back to the search
        if (!m_offers.empty() && m_offers.front().toolRow)
        {
            m_offers.front().toolRow->setFocus(Qt::TabFocusReason);
        }
        else if (m_searchField)
        {
            m_searchField->setFocus(Qt::TabFocusReason);
        }
    }
}

void PDFAppToolCollectionEditor::updateInfoBar()
{
    if (m_infoBarKind == InfoBarKind::None)
    {
        if (m_infoBar)
        {
            // Later: this may run from a signal of the bar
            m_infoBar->hide();
            m_infoBar->deleteLater();
            m_infoBar = nullptr;
        }
        if (m_infoBarSlot)
        {
            m_infoBarSlot->hide();
        }
        return;
    }

    PDFAppInfoBarRequest request;
    request.tone = PDFAppInfoBarTone::Warning;
    if (m_infoBarKind == InfoBarKind::Changed)
    {
        request.id = QString::fromLatin1(PDFAppToolCollectionEditorHelper::CHANGED_INFO_BAR_ID);
        request.title = tr("This collection was changed in another window");
        request.text = tr("Your changes are not saved.");
        request.actions = { PDFAppInfoBarAction{ QString::fromLatin1(PDFAppToolCollectionEditorHelper::RELOAD_ACTION), tr("Reload"), QString() },
                            PDFAppInfoBarAction{ QString::fromLatin1(PDFAppToolCollectionEditorHelper::KEEP_ACTION), tr("Keep my changes"), QString() } };
        request.isClosable = false;
    }
    else
    {
        request.id = QString::fromLatin1(PDFAppToolCollectionEditorHelper::DELETED_INFO_BAR_ID);
        request.title = tr("This collection was deleted in another window");
        request.text = tr("Save creates it again.");
        request.actions = { PDFAppInfoBarAction{ QString::fromLatin1(PDFAppToolCollectionEditorHelper::DISCARD_ACTION), tr("Discard"), QString() } };
        request.isClosable = true;
    }

    if (m_infoBar)
    {
        m_infoBar->setRequest(request);
    }
    else
    {
        m_infoBar = new PDFAppInfoBar(request, this);
        m_infoBar->setObjectName(QStringLiteral("collectionInfoBar"));
        connect(m_infoBar.data(), &PDFAppInfoBar::actionTriggered, this, &PDFAppToolCollectionEditor::onInfoBarAction);
        connect(m_infoBar.data(), &PDFAppInfoBar::closeRequested, this, &PDFAppToolCollectionEditor::onInfoBarCloseRequested);
    }

    if (m_infoBarSlot)
    {
        m_infoBarSlot->show();
    }
    updateInfoBarGeometry();
}

void PDFAppToolCollectionEditor::updateInfoBarGeometry()
{
    if (!m_infoBar || !m_infoBarSlot)
    {
        return;
    }

    if (m_infoBarSlot->isHidden())
    {
        m_infoBar->hide();
        return;
    }

    // The card hugs its content at the leading edge of the slot; the widget of the
    // bar holds the margins of its shadow around the card
    const QMargins shadow = PDFAppInfoBar::getShadowMargins();
    const QRect slot(m_infoBarSlot->mapTo(this, QPoint(0, 0)), m_infoBarSlot->size());
    const int cardWidth = qMax(0, qMin(m_infoBar->sizeHint().width() - shadow.left() - shadow.right(), slot.width()));
    const int cardLeft = layoutDirection() == Qt::RightToLeft ? slot.right() + 1 - cardWidth : slot.left();
    m_infoBar->setGeometry(QRect(cardLeft - shadow.left(), slot.top() - shadow.top(), cardWidth + shadow.left() + shadow.right(), m_infoBar->sizeHint().height()));
    m_infoBar->show();
    m_infoBar->raise();
}

void PDFAppToolCollectionEditor::setInfoBarKind(InfoBarKind kind)
{
    if (m_infoBarKind != kind)
    {
        m_infoBarKind = kind;
        updateInfoBar();
    }
}

void PDFAppToolCollectionEditor::notifyDraftChanged()
{
    Q_EMIT draftChanged();
}

QString PDFAppToolCollectionEditor::getDuplicateNameError() const
{
    if (!m_context || !m_context->getDatabase() || PDFToolCollectionRepository::normalizeName(m_draft.name).isEmpty())
    {
        return QString();
    }

    // One query per change of the name (updateState and canSave ask both, review
    // RC6-2); a change of the stored names clears the result (refreshNameCheck)
    const QString excludedBuiltinId = m_isNew ? QString() : m_builtinId;
    const QString key = QStringLiteral("%1\n%2\n%3").arg(m_draft.name, QString::number(m_rowId), excludedBuiltinId);
    if (m_isDuplicateCheckValid && m_duplicateCheckKey == key)
    {
        return m_duplicateNameError;
    }

    // The name of another collection (custom, built-in in English or translated)
    QString error;
    PDFToolCollectionRepository repository = m_context->getToolCollections();
    if (const std::optional<PDFToolCollectionRow> other = repository.findByName(m_draft.name, m_rowId, excludedBuiltinId))
    {
        QString otherName = PDFToolCollectionRepository::sanitizeName(other->name);
        if (otherName.isEmpty())
        {
            if (const PDFAppBuiltinToolCollection* builtin = PDFAppBuiltinToolCollections::findCollection(other->builtinId))
            {
                otherName = builtin->getName();
            }
        }
        error = tr("A collection named “%1” already exists. Choose another name.").arg(otherName);
    }

    m_duplicateCheckKey = key;
    m_duplicateNameError = error;
    m_isDuplicateCheckValid = true;
    return error;
}

QStringList PDFAppToolCollectionEditor::computeSuggestions() const
{
    if (m_draft.toolIds.isEmpty())
    {
        return QStringList();
    }

    PDFDocumentToolSuggestionInput input;
    input.fromTools = m_draft.toolIds;
    input.pairs = m_pairs;
    input.excludedTools = m_draft.toolIds;
    input.isSuggestNextEnabled = m_isSuggestNextEnabled;
    input.isSameCategoryFilled = true;
    input.maximumCount = MAXIMUM_SUGGESTIONS;

    QStringList suggestions;
    for (const QString& toolId : PDFDocumentToolSuggestions::suggest(input))
    {
        if (suggestions.size() < MAXIMUM_SUGGESTIONS && PDFDocumentToolCatalog::isValidToolId(toolId) && !m_draft.toolIds.contains(toolId) && !suggestions.contains(toolId))
        {
            suggestions << toolId;
        }
    }
    return suggestions;
}

PDFAppToolCollectionDraftState PDFAppToolCollectionEditor::getStoredState(const PDFAppToolCollection& collection)
{
    PDFAppToolCollectionDraftState state;
    state.name = PDFAppToolCollectionEditorHelper::sanitizeName(collection.name);
    state.iconName = PDFAppBuiltinToolCollections::isValidIconName(collection.iconName) ? collection.iconName : PDFAppBuiltinToolCollections::getDefaultIconName();
    state.toolIds = PDFAppToolCollectionEditorHelper::sanitizeTools(collection.toolIds);
    return state;
}

void PDFAppToolCollectionEditor::onNameEdited(const QString& text)
{
    if (m_isUpdating)
    {
        return;
    }

    m_draft.name = text;
    m_isNameTouched = true;
    m_nameError.clear();
    m_saveError.clear();
    updateState();
    notifyDraftChanged();
}

void PDFAppToolCollectionEditor::onIconChosen(const QString& iconName)
{
    if (m_isUpdating)
    {
        return;
    }

    m_draft.iconName = iconName;
    updateState();
    notifyDraftChanged();
}

void PDFAppToolCollectionEditor::onToolsChanged(const QStringList& toolIds)
{
    if (m_isUpdating)
    {
        return;
    }

    m_draft.toolIds = PDFAppToolCollectionEditorHelper::sanitizeTools(toolIds);
    m_saveError.clear();
    updateState();
    updateOffers();
    notifyDraftChanged();
}

void PDFAppToolCollectionEditor::onSearchTextChanged(const QString& text)
{
    Q_UNUSED(text);
    updateOffers();
}

void PDFAppToolCollectionEditor::onSearchReturnPressed()
{
    // Enter adds the best result of a search
    if (!getSearchText().trimmed().isEmpty() && !m_offers.empty())
    {
        addTool(m_offers.front().toolId);
    }
}

void PDFAppToolCollectionEditor::onOfferClicked()
{
    QObject* object = sender();
    for (const Offer& offer : m_offers)
    {
        if (object && (object == offer.toolRow.data() || object == offer.addButton.data()))
        {
            const QString toolId = offer.toolId;
            addTool(toolId);
            return;
        }
    }
}

void PDFAppToolCollectionEditor::onResetClicked()
{
    resetToDefault();
}

void PDFAppToolCollectionEditor::onDeleteClicked()
{
    Q_EMIT deleteRequested();
}

void PDFAppToolCollectionEditor::onCancelClicked()
{
    if (m_isNew)
    {
        Q_EMIT leaveRequested();
    }
    else
    {
        revert();
    }
}

void PDFAppToolCollectionEditor::onSaveClicked()
{
    if (canSave())
    {
        Q_EMIT saveRequested();
    }
}

void PDFAppToolCollectionEditor::onInfoBarAction(const QString& infoBarId, const QString& actionId)
{
    if (infoBarId == QLatin1String(PDFAppToolCollectionEditorHelper::CHANGED_INFO_BAR_ID) && m_pendingStored)
    {
        const PDFAppToolCollection stored = *m_pendingStored;
        if (actionId == QLatin1String(PDFAppToolCollectionEditorHelper::RELOAD_ACTION))
        {
            setCollection(&stored);
            notifyDraftChanged();
        }
        else if (actionId == QLatin1String(PDFAppToolCollectionEditorHelper::KEEP_ACTION))
        {
            // The draft is kept against the new stored values
            m_base = getStoredState(stored);
            m_baseRowId = stored.rowId;
            m_baseModified = stored.modified;
            m_pendingStored.reset();
            setInfoBarKind(InfoBarKind::None);
            updateState();
        }
    }
    else if (infoBarId == QLatin1String(PDFAppToolCollectionEditorHelper::DELETED_INFO_BAR_ID) && actionId == QLatin1String(PDFAppToolCollectionEditorHelper::DISCARD_ACTION))
    {
        Q_EMIT leaveRequested();
    }
}

void PDFAppToolCollectionEditor::onInfoBarCloseRequested(const QString& infoBarId)
{
    Q_UNUSED(infoBarId);

    // The draft stays (Save creates the collection again)
    setInfoBarKind(InfoBarKind::None);
}

}   // namespace pdf
