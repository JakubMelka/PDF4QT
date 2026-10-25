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

#ifndef PDFAPPSHORTCUTSMODEL_H
#define PDFAPPSHORTCUTSMODEL_H

#include "pdfappglobal.h"
#include "pdfappcommand.h"
#include "pdfdatabase.h"

#include "pdfcomponentdelegates.h"

#include <QAbstractListModel>
#include <QKeySequence>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <vector>

namespace pdf
{

class PDFAppContext;

/// Commands of the application with their shortcuts for the table of Settings ›
/// Shortcuts (147:4184, plan 2.6): one row per command of PDFAppCommandRegistry in
/// the order of the registration, the title, the icon and the effective shortcut
/// (the shortcut of the user, else the default one) in the roles of
/// PDFShortcutDelegate. A shortcut written into ShortcutRole (the recording of the
/// delegate) is checked first:
///  - a key, which cannot be a shortcut (Esc, Tab, Enter alone, a lone modifier,
///    Alt+F4), is refused - shortcutRefused,
///  - a key of another command (PDFAppCommandRegistry::getConflictingCommands: the
///    scopes and the prefixes of the sequences) is not written - conflictFound; the
///    section asks and assigns it by assign (one transaction),
///  - the default shortcut removes the own shortcut of the user (the command follows
///    its default again), an empty one removes the shortcut of the command.
/// The signals of setData (inside of the commit of the editor of the delegate) are
/// emitted from the event loop, so a slot may delete the view. The model reads the shortcuts of the user from
/// the database at refresh (the notifier, topic shortcut) and keeps no other state.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutsModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    enum Role : int
    {
        CommandIdRole = PDFComponentRoles::LastRole + 1,    ///< Id of the command (QString)
        DefaultShortcutRole,                                ///< Default shortcut (QKeySequence)
        IsChangedRole,                                      ///< The user has changed the shortcut (bool)
        ResetRole,                                          ///< setData: the default shortcut again
        SearchTextRole                                      ///< English title and id of the command (QStringList)
    };

    explicit PDFAppShortcutsModel(PDFAppContext* context, QObject* parent = nullptr);
    virtual ~PDFAppShortcutsModel() override;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    /// Reads the shortcuts of the user again (the rows are updated, not reset, so a
    /// running recording of the delegate goes on)
    void refresh();

    /// Id of the command of the row (empty for an invalid row) and the row of an id (-1)
    QString getCommandId(int row) const;
    int findRow(const QString& commandId) const;

    /// Shortcuts of the user (command id -> stored text, empty = removed)
    const PDFAppCommandRegistry::UserShortcuts& getUserShortcuts() const { return m_userShortcuts; }

    /// Effective and default shortcut of the command
    QKeySequence getShortcut(const QString& commandId) const;
    QKeySequence getDefaultShortcut(const QString& commandId) const;

    /// All keys of the command (the shortcut and, while the user has not changed it,
    /// the alternate keys - PDFAppCommandRegistry::getShortcuts), now and by default
    QList<QKeySequence> getKeys(const QString& commandId) const;
    QList<QKeySequence> getDefaultKeys(const QString& commandId) const;

    /// The keys of the command differ from its default keys (also a stored default
    /// shortcut, which has turned the alternate keys off)
    bool isChanged(const QString& commandId) const;

    /// Commands, whose keys differ from the default ones (Restore defaults)
    QStringList getChangedCommands() const;

    /// Translated title of the command (the id for an unknown command)
    QString getCommandTitle(const QString& commandId) const;

    /// Commands, which have \p shortcut, when it is assigned to \p commandId (the
    /// default shortcut brings the alternate keys of the command back: they are
    /// checked too)
    QStringList getConflicts(const QString& commandId, const QKeySequence& shortcut) const;

    /// Reason, why \p shortcut cannot be a shortcut (empty = it can)
    static QString getRefusalReason(const QKeySequence& shortcut);

    /// Text of a shortcut for the user (the notation of the platform; "None" for none)
    static QString getShortcutText(const QKeySequence& shortcut);

    /// Text of the keys of a command ("F2, Alt+Return"; "None" for none)
    static QString getShortcutsText(const QList<QKeySequence>& shortcuts);

    /// Assign of the conflict dialog: \p shortcut becomes the shortcut of
    /// \p commandId and the conflicting commands lose it, in one transaction. A
    /// command, whose own shortcut conflicts, has no shortcut then; a command, which
    /// conflicts only by an alternate key (F2 / Alt+Return), keeps its shortcut
    /// without the alternate keys. The default shortcut of \p commandId removes its
    /// own row (Reset to Default, the default recorded again: the alternate keys
    /// apply again). An error is also reported by writeFailed.
    PDFDatabaseError assign(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts);

    /// Writes the shortcut of the command without the checks of setData (an empty
    /// one removes it); the default shortcut is stored as no change
    PDFDatabaseError writeShortcut(const QString& commandId, const QKeySequence& shortcut);

    /// The default shortcut of the command again
    PDFDatabaseError resetShortcut(const QString& commandId);

Q_SIGNALS:
    /// The recorded shortcut belongs to other commands (it was not written)
    void conflictFound(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts);

    /// The recorded keys cannot be a shortcut (it was not written)
    void shortcutRefused(const QString& commandId, const QKeySequence& shortcut, const QString& reason);

    /// The database refused the write
    void writeFailed(const QString& message);

private:
    const PDFAppCommand* getCommand(int row) const;

    /// Keys of the command after \p shortcut is assigned to it (the default shortcut
    /// = the default keys with the alternate ones)
    QList<QKeySequence> getAssignedKeys(const QString& commandId, const QKeySequence& shortcut) const;

    /// Commands, whose keys conflict with any of \p keys of \p commandId
    QStringList getConflictingCommands(const QString& commandId, const QList<QKeySequence>& keys) const;

    PDFDatabaseError reportError(const PDFDatabaseError& error);
    void emitConflictFound(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts);
    void emitShortcutRefused(const QString& commandId, const QKeySequence& shortcut, const QString& reason);
    void emitWriteFailed(const QString& message);

    QPointer<PDFAppContext> m_context;
    std::vector<QString> m_commandIds;
    PDFAppCommandRegistry::UserShortcuts m_userShortcuts;
};

/// Filter of the table of the shortcuts: the search field of the group (Search
/// actions or shortcuts…) matches the title of the command (translated and
/// English) and the text of its shortcut (the notation of the platform and the
/// portable one), without case
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutsFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

private:
    using BaseClass = QSortFilterProxyModel;

public:
    /// Longest text of the filter
    static constexpr int MAXIMUM_FILTER_LENGTH = 256;

    explicit PDFAppShortcutsFilterModel(QObject* parent = nullptr);
    virtual ~PDFAppShortcutsFilterModel() override;

    const QString& getFilterText() const { return m_filterText; }
    void setFilterText(const QString& text);

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString m_filterText;
};

}   // namespace pdf

#endif // PDFAPPSHORTCUTSMODEL_H
