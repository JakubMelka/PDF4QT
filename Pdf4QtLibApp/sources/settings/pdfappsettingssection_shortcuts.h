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

#ifndef PDFAPPSETTINGSSECTION_SHORTCUTS_H
#define PDFAPPSETTINGSSECTION_SHORTCUTS_H

#include "pdfappglobal.h"
#include "pdfappsettingssection.h"
#include "pdfapphomedialogs.h"
#include "pdfapppagelayout.h"
#include "pdfappshortcutsmodel.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"

#include <QKeySequence>
#include <QPointer>
#include <QPushButton>
#include <QStringList>

namespace pdf
{

class PDFAppCard;
class PDFAppParagraph;
class PDFAppShortcutsFilterModel;
class PDFAppShortcutsModel;
class PDFKeyValueList;
class PDFLineEdit;

/// Delegate of the table of the shortcuts: PDFShortcutDelegate (the recording, Change
/// shortcut, Remove shortcut) with the item Reset to Default of the context menu
/// (enabled for a shortcut changed by the user). The conflicts are checked by the
/// model (scopes and prefixes of PDFAppCommandRegistry), so the delegate writes
/// every recorded sequence into the model.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutDelegate : public PDFShortcutDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFShortcutDelegate;

public:
    enum AppActionId
    {
        ResetAction = 2     ///< The default shortcut again (context menu only)
    };

    explicit PDFAppShortcutDelegate(QObject* parent = nullptr);
    virtual ~PDFAppShortcutDelegate() override;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    virtual void performAction(QAbstractItemView* view, const QModelIndex& index, int action) override;
};

/// List of the shortcuts in the card 147:4442: the rows start 4 px below the header
/// (rows 147:4446) and end 18 px before the edge of the card, the scroll bar lies
/// in that padding
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutsListView : public PDFListView
{
    Q_OBJECT

private:
    using BaseClass = PDFListView;

public:
    explicit PDFAppShortcutsListView(QWidget* parent = nullptr);
    virtual ~PDFAppShortcutsListView() override;
};

/// Header of the columns of the table (147:4443): Action and Shortcut (120 px, at
/// the trailing edge), Medium 11 text/muted, paddings 10 / 6 / 10 / 8, the divider
/// border/divider below it
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutsTableHeader : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppShortcutsTableHeader(QWidget* parent = nullptr);
    virtual ~PDFAppShortcutsTableHeader() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Conflict of a recorded shortcut (plan 2.6, no frame - the pattern of 941:136741):
/// "Assign Ctrl+X to Delete?", the shortcut, the commands using it and the command
/// getting it; Assign Shortcut (Secondary, leading) and Cancel (Primary)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutConflictDialog : public PDFAppCardDialog
{
    Q_OBJECT

private:
    using BaseClass = PDFAppCardDialog;

public:
    /// Width of the card (941:136881)
    static constexpr int CARD_WIDTH = 520;

    explicit PDFAppShortcutConflictDialog(const QString& commandTitle, const QKeySequence& shortcut, const QStringList& conflictTitles, QWidget* parent);
    virtual ~PDFAppShortcutConflictDialog() override;

    PDFKeyValueList* getFacts() const { return m_facts; }
    QPushButton* getAssignButton() const { return m_assignButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }

private:
    QPointer<PDFKeyValueList> m_facts;
    QPointer<QPushButton> m_assignButton;
    QPointer<QPushButton> m_cancelButton;
};

/// Section Shortcuts (147:4184): one group Keyboard shortcuts with the hint "Click a
/// shortcut to record a new one", the search field "Search actions or shortcuts…"
/// (a local filter of the table) and the table of all commands (147:4442, 540 high)
/// with PDFAppShortcutDelegate. A recorded shortcut is written at once and applies
/// in every window (the notifier of the database, topic shortcut); a conflict asks
/// (PDFAppShortcutConflictDialog), a key, which cannot be a shortcut, is refused
/// with a status message. Restore defaults removes all shortcuts of the user.
/// The anchor "shortcut-<command id>" selects the row of the command.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutsSettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    /// Key of the group (getGroupHint knows its hint)
    static constexpr const char* GROUP_KEY = "Keyboard shortcuts";

    /// Height of the card of the table (147:4442)
    static constexpr int TABLE_HEIGHT = 540;

    /// Prefix of the anchors of the rows
    static constexpr const char* ANCHOR_PREFIX = "shortcut-";

    explicit PDFAppShortcutsSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppShortcutsSettingsSection() override;

    virtual QStringList getChangedKeys() const override;
    virtual std::vector<PDFAppSettingChange> getChangedValues() const override;
    virtual QString getRestoreDefaultsText(int count) const override;
    virtual PDFDatabaseError restoreDefaults() override;
    virtual QStringList getRestoreTopics() const override;
    virtual QWidget* findAnchorWidget(const QString& anchor) const override;
    virtual void refresh(const QString& topic, const QString& key) override;

    PDFAppShortcutsModel* getModel() const { return m_model; }
    PDFAppShortcutsFilterModel* getFilterModel() const { return m_filterModel; }
    PDFAppShortcutsListView* getView() const { return m_view; }
    PDFAppShortcutDelegate* getDelegate() const { return m_delegate; }
    PDFLineEdit* getSearchField() const { return m_searchField; }
    PDFAppCard* getTableCard() const { return m_tableCard; }
    PDFAppParagraph* getEmptyText() const { return m_emptyText; }
    PDFAppShortcutConflictDialog* getConflictDialog() const { return m_conflictDialog; }

    /// Opens the conflict dialog of the shortcut (the model has found the conflicts)
    void openConflictDialog(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts);

protected:
    virtual void finishContent() override;

private:
    void onSearchTextChanged(const QString& text);
    void onConflictFound(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts);
    void onShortcutRefused(const QString& commandId, const QKeySequence& shortcut, const QString& reason);
    void onWriteFailed(const QString& message);
    void onConflictDialogFinished(int result);
    void updateEmptyState();

    QPointer<PDFAppShortcutsModel> m_model;
    QPointer<PDFAppShortcutsFilterModel> m_filterModel;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<PDFAppCard> m_tableCard;
    QPointer<PDFAppShortcutsListView> m_view;
    QPointer<PDFAppShortcutDelegate> m_delegate;
    QPointer<PDFAppParagraph> m_emptyText;
    QPointer<PDFAppShortcutConflictDialog> m_conflictDialog;

    /// The shortcut of the open conflict dialog
    QString m_conflictCommandId;
    QKeySequence m_conflictShortcut;
};

/// Factory of the section
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutsSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_SHORTCUTS_H
