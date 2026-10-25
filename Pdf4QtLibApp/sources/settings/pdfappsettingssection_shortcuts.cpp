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

#include "pdfappsettingssection_shortcuts.h"
#include "pdfappcontext.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingrow.h"
#include "pdfappshortcutsmodel.h"
#include "pdfshortcutrepository.h"

#include "pdfcomponentpanels.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace pdf
{

PDFAppShortcutDelegate::PDFAppShortcutDelegate(QObject* parent) :
    BaseClass(parent)
{
    // The model checks the conflicts (the scopes of the commands), the delegate
    // passes every recorded sequence to it
    setConflictsAccepted(true);
}

PDFAppShortcutDelegate::~PDFAppShortcutDelegate() = default;

QList<PDFRowDelegate::Action> PDFAppShortcutDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QList<Action> actions = BaseClass::getActions(option, index);
    if (index.isValid())
    {
        Action reset;
        reset.id = ResetAction;
        reset.text = tr("Reset to Default");
        reset.enabled = index.flags().testFlag(Qt::ItemIsEditable) && index.data(PDFAppShortcutsModel::IsChangedRole).toBool() && !isRecording(index);
        actions.append(reset);
    }
    return actions;
}

void PDFAppShortcutDelegate::performAction(QAbstractItemView* view, const QModelIndex& index, int action)
{
    if (action == ResetAction)
    {
        // Through the model of the view (the filter forwards it to the shortcuts)
        if (QAbstractItemModel* model = const_cast<QAbstractItemModel*>(index.model()))
        {
            model->setData(index, QVariant(), PDFAppShortcutsModel::ResetRole);
        }
        return;
    }

    BaseClass::performAction(view, index, action);
}

PDFAppShortcutsListView::PDFAppShortcutsListView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("shortcutsList"));
    setFrameShape(QFrame::NoFrame);

    // rows 147:4446: 4 px below the header; the rows end 18 px before the border of
    // the card, the scroll bar (12) lies in that padding
    setViewportMargins(0, 4, 5, 0);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setResizeMode(QListView::Adjust);
}

PDFAppShortcutsListView::~PDFAppShortcutsListView() = default;

PDFAppShortcutsTableHeader::PDFAppShortcutsTableHeader(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("shortcutsHeader"));

    // columns 147:4443: paddings 10 / 6 / 10 / 8, gap 12; the header ends where
    // the rows end (17 px of the padding of the card right of it)
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10 + 17, 8 + 1);
    layout->setSpacing(12);

    PDFLabel* action = PDFAppPageWidgets::createLabel(tr("Action"), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, this);
    action->setObjectName(QStringLiteral("actionColumn"));
    layout->addWidget(action, 1);

    PDFLabel* shortcut = PDFAppPageWidgets::createLabel(tr("Shortcut"), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, this);
    shortcut->setObjectName(QStringLiteral("shortcutColumn"));
    shortcut->setFixedWidth(120);
    shortcut->setAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
    layout->addWidget(shortcut);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppShortcutsTableHeader::update));
}

PDFAppShortcutsTableHeader::~PDFAppShortcutsTableHeader() = default;

void PDFAppShortcutsTableHeader::paintEvent(QPaintEvent* event)
{
    BaseClass::paintEvent(event);

    // border-bottom border/divider of the columns (to the end of the rows)
    QPainter painter(this);
    const int width = qMax(0, this->width() - 17);
    const int x = layoutDirection() == Qt::RightToLeft ? 17 : 0;
    painter.fillRect(QRect(x, height() - 1, width, 1), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BorderDivider));
}

PDFAppShortcutConflictDialog::PDFAppShortcutConflictDialog(const QString& commandTitle, const QKeySequence& shortcut, const QStringList& conflictTitles, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true,
              // The names of the commands in quotes, as the names in the other confirmations:
              // a title ending with an ellipsis (Open…) is not followed by a bare period
              tr("Assign %1 to “%2”?").arg(PDFAppShortcutsModel::getShortcutText(shortcut), commandTitle),
              tr("%1 is already the shortcut of “%2”. It is removed from there, so the keys do one thing.").arg(PDFAppShortcutsModel::getShortcutText(shortcut), conflictTitles.join(QStringLiteral("”, “"))),
              CARD_WIDTH, parent)
{
    setObjectName(QStringLiteral("shortcutConflictDialog"));

    // facts of 941:136881: bg/subtle, radius 10, paddings 4 / 12, Key value rows
    QFrame* factsFrame = new QFrame(this);
    factsFrame->setObjectName(QStringLiteral("facts"));
    PDFWidgetStyleProperties::set(factsFrame, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(factsFrame);
    factsLayout->setContentsMargins(12, 4, 12, 4);
    factsLayout->setSpacing(0);
    m_facts = new PDFKeyValueList(factsFrame);
    m_facts->setObjectName(QStringLiteral("factsList"));
    m_facts->addRow(tr("Shortcut"), PDFAppShortcutsModel::getShortcutText(shortcut));
    m_facts->addRow(tr("Used by"), conflictTitles.join(QStringLiteral(", ")));
    m_facts->addRow(tr("Assigned to"), commandTitle);
    factsLayout->addWidget(m_facts);
    addContent(factsFrame);

    // Assign Shortcut (Secondary) at the leading edge, Cancel (Primary): 941:136741
    m_assignButton = addActionButton(tr("Assign Shortcut"), false, true);
    m_assignButton->setObjectName(QStringLiteral("assignButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_assignButton.data(), &QPushButton::clicked, this, &PDFAppShortcutConflictDialog::accept);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppShortcutConflictDialog::reject);
}

PDFAppShortcutConflictDialog::~PDFAppShortcutConflictDialog() = default;

PDFAppShortcutsSettingsSection::PDFAppShortcutsSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{

}

PDFAppShortcutsSettingsSection::~PDFAppShortcutsSettingsSection()
{
    // The dialog belongs to the window: it goes with the section, its result is
    // not used any more. It is deleted later: the section may go inside of an
    // event delivered to the widgets of the window (LanguageChange rebuilds the
    // page; QWidget::event sends it to a copy of the list of the children of the
    // window), so a dialog - a child of the window - deleted now would be used
    // by that delivery after its deletion (its scrim is deleted later by
    // PDFMessageBox itself).
    if (m_conflictDialog)
    {
        disconnect(m_conflictDialog.data(), nullptr, this, nullptr);
        m_conflictDialog->deleteLater();
    }
}

void PDFAppShortcutsSettingsSection::finishContent()
{
    PDFAppSettingGroup* group = addGroup(QString::fromLatin1(GROUP_KEY), tr("Keyboard shortcuts"), PDFAppSettingsSectionRegistry::getGroupHint(QString::fromLatin1(GROUP_KEY)));
    if (!group)
    {
        return;
    }

    m_model = new PDFAppShortcutsModel(getContext(), this);
    m_filterModel = new PDFAppShortcutsFilterModel(this);
    m_filterModel->setSourceModel(m_model);

    // The model sends its signals from the event loop (never inside of the commit
    // of the editor of the delegate)
    connect(m_model.data(), &PDFAppShortcutsModel::conflictFound, this, &PDFAppShortcutsSettingsSection::onConflictFound);
    connect(m_model.data(), &PDFAppShortcutsModel::shortcutRefused, this, &PDFAppShortcutsSettingsSection::onShortcutRefused);
    connect(m_model.data(), &PDFAppShortcutsModel::writeFailed, this, &PDFAppShortcutsSettingsSection::onWriteFailed);

    // settings-search 147:4438: Search field (pill), the whole width
    m_searchField = new PDFLineEdit(group);
    m_searchField->setObjectName(QStringLiteral("shortcutsSearch"));
    m_searchField->setSearchField();
    m_searchField->setPlaceholderText(tr("Search actions or shortcuts…"));
    m_searchField->setAccessibleName(tr("Search actions or shortcuts"));
    m_searchField->setMaxLength(PDFAppShortcutsFilterModel::MAXIMUM_FILTER_LENGTH);
    m_searchField->setClearButtonEnabled(true);
    connect(m_searchField.data(), &QLineEdit::textChanged, this, &PDFAppShortcutsSettingsSection::onSearchTextChanged);
    group->insertWidget(m_searchField);

    // card 147:4442: paddings 8 / 18 / 8 / 8 inside of the border (the scroll bar
    // in the right padding), 540 high
    m_tableCard = new PDFAppCard(QMargins(PDFAppPageWidgets::BORDER_WIDTH + 8, PDFAppPageWidgets::BORDER_WIDTH + 8, PDFAppPageWidgets::BORDER_WIDTH + 1, PDFAppPageWidgets::BORDER_WIDTH + 8), 0, group);
    m_tableCard->setObjectName(QStringLiteral("shortcutsCard"));
    m_tableCard->setFixedHeight(TABLE_HEIGHT);

    PDFAppShortcutsTableHeader* header = new PDFAppShortcutsTableHeader(m_tableCard);
    m_tableCard->getLayout()->addWidget(header);

    m_view = new PDFAppShortcutsListView(m_tableCard);
    m_view->setAccessibleName(tr("Keyboard shortcuts"));
    m_delegate = new PDFAppShortcutDelegate(m_view);
    m_view->setItemDelegate(m_delegate);
    m_view->setModel(m_filterModel);
    m_tableCard->getLayout()->addWidget(m_view, 1);

    // No match of the search
    m_emptyText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextMuted, m_tableCard);
    m_emptyText->setObjectName(QStringLiteral("shortcutsEmpty"));
    m_emptyText->setContentsMargins(10, 12, 10, 12);
    m_emptyText->hide();
    m_tableCard->getLayout()->insertWidget(1, m_emptyText);

    group->insertWidget(m_tableCard);
}

void PDFAppShortcutsSettingsSection::onSearchTextChanged(const QString& text)
{
    if (m_filterModel)
    {
        m_filterModel->setFilterText(text);
    }
    updateEmptyState();
}

void PDFAppShortcutsSettingsSection::updateEmptyState()
{
    if (!m_filterModel || !m_emptyText || !m_view)
    {
        return;
    }

    const bool isEmpty = m_filterModel->rowCount() == 0;
    m_emptyText->setText(tr("No action matches “%1”.").arg(m_filterModel->getFilterText()));
    m_emptyText->setVisible(isEmpty);
    m_view->setVisible(!isEmpty);
}

QStringList PDFAppShortcutsSettingsSection::getChangedKeys() const
{
    return m_model ? m_model->getChangedCommands() : QStringList();
}

std::vector<PDFAppSettingChange> PDFAppShortcutsSettingsSection::getChangedValues() const
{
    std::vector<PDFAppSettingChange> changes;
    if (!m_model)
    {
        return changes;
    }

    for (const QString& commandId : m_model->getChangedCommands())
    {
        PDFAppSettingChange change;
        change.key = commandId;
        change.title = m_model->getCommandTitle(commandId);
        // All keys: a stored default shortcut differs from the default by the
        // alternate keys only ("F2" against "F2, Alt+Return")
        change.currentValue = PDFAppShortcutsModel::getShortcutsText(m_model->getKeys(commandId));
        change.defaultValue = PDFAppShortcutsModel::getShortcutsText(m_model->getDefaultKeys(commandId));
        changes.push_back(std::move(change));
    }
    return changes;
}

QString PDFAppShortcutsSettingsSection::getRestoreDefaultsText(int count) const
{
    if (count == 1)
    {
        return tr("This shortcut goes back to its default. Your settings and your history stay as they are.");
    }
    // %Ln: the plural forms of the language and the number of its locale (O17)
    return tr("These %Ln shortcuts go back to their defaults. Your settings and your history stay as they are.", nullptr, count);
}

QStringList PDFAppShortcutsSettingsSection::getRestoreTopics() const
{
    // The changed shortcuts (Restore defaults resets the shortcuts only)
    return { PDFShortcutRepository::getTopic() };
}

PDFDatabaseError PDFAppShortcutsSettingsSection::restoreDefaults()
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return PDFDatabaseError::fromMessage(tr("The settings are not available."));
    }

    const PDFDatabaseError error = context->getShortcuts().resetAll();
    if (!error.isError() && m_model)
    {
        m_model->refresh();
    }
    return error;
}

QWidget* PDFAppShortcutsSettingsSection::findAnchorWidget(const QString& anchor) const
{
    if (anchor.startsWith(QLatin1String(ANCHOR_PREFIX)) && m_model && m_filterModel && m_view)
    {
        const int row = m_model->findRow(anchor.mid(int(qstrlen(ANCHOR_PREFIX))));
        if (row >= 0)
        {
            // The row of the command is shown, current and selected
            if (m_searchField && !m_searchField->text().isEmpty())
            {
                m_searchField->clear();
            }
            const QModelIndex index = m_filterModel->mapFromSource(m_model->index(row));
            if (index.isValid())
            {
                m_view->setCurrentIndex(index);
                m_view->scrollTo(index, QAbstractItemView::PositionAtCenter);
            }
            return m_tableCard;
        }
    }

    return BaseClass::findAnchorWidget(anchor);
}

void PDFAppShortcutsSettingsSection::refresh(const QString& topic, const QString& key)
{
    BaseClass::refresh(topic, key);

    if ((topic.isEmpty() || topic == PDFShortcutRepository::getTopic()) && m_model)
    {
        m_model->refresh();
        updateEmptyState();
    }
}

void PDFAppShortcutsSettingsSection::openConflictDialog(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts)
{
    if (!m_model || conflicts.isEmpty())
    {
        return;
    }

    // One question at a time: a newer conflict replaces the open one
    if (m_conflictDialog)
    {
        disconnect(m_conflictDialog.data(), nullptr, this, nullptr);
        m_conflictDialog->reject();
    }

    QStringList titles;
    for (const QString& conflict : conflicts)
    {
        titles << m_model->getCommandTitle(conflict);
    }

    m_conflictCommandId = commandId;
    m_conflictShortcut = shortcut;
    PDFAppShortcutConflictDialog* dialog = new PDFAppShortcutConflictDialog(m_model->getCommandTitle(commandId), shortcut, titles, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppShortcutsSettingsSection::onConflictDialogFinished);
    m_conflictDialog = dialog;
    dialog->open();
}

void PDFAppShortcutsSettingsSection::onConflictFound(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts)
{
    openConflictDialog(commandId, shortcut, conflicts);
}

void PDFAppShortcutsSettingsSection::onShortcutRefused(const QString& commandId, const QKeySequence& shortcut, const QString& reason)
{
    Q_UNUSED(commandId);
    Q_UNUSED(shortcut);
    Q_EMIT statusMessageRequested(reason);
}

void PDFAppShortcutsSettingsSection::onWriteFailed(const QString& message)
{
    Q_EMIT statusMessageRequested(tr("The shortcut could not be saved: %1").arg(message));
}

void PDFAppShortcutsSettingsSection::onConflictDialogFinished(int result)
{
    const QString commandId = m_conflictCommandId;
    const QKeySequence shortcut = m_conflictShortcut;
    m_conflictCommandId.clear();
    m_conflictShortcut = QKeySequence();

    if (result != QDialog::Accepted || !m_model || commandId.isEmpty())
    {
        return;
    }

    // The shortcuts may have changed meanwhile (another instance): the conflicts
    // of now are removed. An error is reported by writeFailed of the model (once).
    const QStringList conflicts = m_model->getConflicts(commandId, shortcut);
    m_model->assign(commandId, shortcut, conflicts);
}

PDFAppSettingsSection* PDFAppShortcutsSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppShortcutsSettingsSection(context, sectionId, parent);
}

}   // namespace pdf
