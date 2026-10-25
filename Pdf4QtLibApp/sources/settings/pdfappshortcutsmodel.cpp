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

#include "pdfappshortcutsmodel.h"
#include "pdfappcontext.h"
#include "pdfshortcutrepository.h"

#include <QCoreApplication>

#include <map>

namespace pdf
{

PDFAppShortcutsModel::PDFAppShortcutsModel(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{
    if (context)
    {
        for (const PDFAppCommand* command : context->getCommandRegistry()->getCommands())
        {
            m_commandIds.push_back(command->id);
        }
    }

    refresh();
}

PDFAppShortcutsModel::~PDFAppShortcutsModel() = default;

int PDFAppShortcutsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_commandIds.size());
}

const PDFAppCommand* PDFAppShortcutsModel::getCommand(int row) const
{
    if (!m_context || row < 0 || row >= int(m_commandIds.size()))
    {
        return nullptr;
    }
    return m_context->getCommandRegistry()->findCommand(m_commandIds[size_t(row)]);
}

QVariant PDFAppShortcutsModel::data(const QModelIndex& index, int role) const
{
    const PDFAppCommand* command = (index.isValid() && index.model() == this) ? getCommand(index.row()) : nullptr;
    if (!command)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return command->getTitle();

        case Qt::AccessibleTextRole:
        {
            const QKeySequence shortcut = getShortcut(command->id);
            return shortcut.isEmpty() ? tr("%1, no shortcut").arg(command->getTitle())
                                      : tr("%1, %2").arg(command->getTitle(), getShortcutText(shortcut));
        }

        case PDFComponentRoles::IconNameRole:
            return command->iconName;

        case PDFComponentRoles::ShortcutRole:
            return QVariant::fromValue(getShortcut(command->id));

        case CommandIdRole:
            return command->id;

        case DefaultShortcutRole:
            return QVariant::fromValue(command->defaultShortcut);

        case IsChangedRole:
            return isChanged(command->id);

        case SearchTextRole:
            return QStringList({ QString::fromUtf8(command->title), command->id });

        default:
            break;
    }

    return QVariant();
}

Qt::ItemFlags PDFAppShortcutsModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || index.model() != this || !getCommand(index.row()))
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool PDFAppShortcutsModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    const PDFAppCommand* command = (index.isValid() && index.model() == this) ? getCommand(index.row()) : nullptr;
    if (!command)
    {
        return false;
    }

    const QString commandId = command->id;
    if (role == ResetRole)
    {
        // The default keys may have been given to other commands meanwhile: the
        // dialog asks first (two commands with one key would both stop working)
        const QStringList conflicts = (!command->defaultShortcut.isEmpty() && m_userShortcuts.count(commandId))
                                          ? getConflictingCommands(commandId, getDefaultKeys(commandId)) : QStringList();
        if (!conflicts.isEmpty())
        {
            QMetaObject::invokeMethod(this, &PDFAppShortcutsModel::emitConflictFound, Qt::QueuedConnection, commandId, command->defaultShortcut, conflicts);
            return false;
        }
        return !resetShortcut(commandId).isError();
    }

    if (role != PDFComponentRoles::ShortcutRole && role != Qt::EditRole)
    {
        return false;
    }

    QKeySequence shortcut;
    if (value.metaType() == QMetaType::fromType<QKeySequence>())
    {
        shortcut = value.value<QKeySequence>();
    }
    else if (value.metaType() == QMetaType::fromType<QString>())
    {
        bool ok = false;
        shortcut = PDFAppCommandRegistry::fromStoredText(value.toString().left(PDFShortcutRepository::MAXIMUM_KEYS_LENGTH), &ok);
        if (!ok)
        {
            return false;
        }
    }
    else if (value.isValid())
    {
        return false;
    }

    if (!shortcut.isEmpty())
    {
        if (getAssignedKeys(commandId, shortcut) == getKeys(commandId))
        {
            // Nothing changes (the default recorded over a stored default shortcut,
            // which has turned the alternate keys off, brings them back)
            return true;
        }

        // The view is inside of the commit of the editor: the signals wait for the
        // event loop (a slot may delete the view, the page, the window)
        const QString reason = getRefusalReason(shortcut);
        if (!reason.isEmpty())
        {
            QMetaObject::invokeMethod(this, &PDFAppShortcutsModel::emitShortcutRefused, Qt::QueuedConnection, commandId, shortcut, reason);
            return false;
        }

        const QStringList conflicts = getConflicts(commandId, shortcut);
        if (!conflicts.isEmpty())
        {
            QMetaObject::invokeMethod(this, &PDFAppShortcutsModel::emitConflictFound, Qt::QueuedConnection, commandId, shortcut, conflicts);
            return false;
        }
    }

    return !writeShortcut(commandId, shortcut).isError();
}

void PDFAppShortcutsModel::refresh()
{
    PDFAppCommandRegistry::UserShortcuts shortcuts;
    if (m_context && m_context->getDatabase())
    {
        shortcuts = m_context->getShortcuts().getShortcuts();
    }

    if (shortcuts == m_userShortcuts)
    {
        return;
    }

    m_userShortcuts = std::move(shortcuts);
    if (!m_commandIds.empty())
    {
        Q_EMIT dataChanged(index(0), index(int(m_commandIds.size()) - 1));
    }
}

QString PDFAppShortcutsModel::getCommandId(int row) const
{
    return (row >= 0 && row < int(m_commandIds.size())) ? m_commandIds[size_t(row)] : QString();
}

int PDFAppShortcutsModel::findRow(const QString& commandId) const
{
    for (size_t i = 0; i < m_commandIds.size(); ++i)
    {
        if (m_commandIds[i] == commandId)
        {
            return int(i);
        }
    }
    return -1;
}

QKeySequence PDFAppShortcutsModel::getShortcut(const QString& commandId) const
{
    return m_context ? m_context->getCommandRegistry()->getShortcut(commandId, m_userShortcuts) : QKeySequence();
}

QKeySequence PDFAppShortcutsModel::getDefaultShortcut(const QString& commandId) const
{
    const PDFAppCommand* command = m_context ? m_context->getCommandRegistry()->findCommand(commandId) : nullptr;
    return command ? command->defaultShortcut : QKeySequence();
}

QList<QKeySequence> PDFAppShortcutsModel::getKeys(const QString& commandId) const
{
    return m_context ? m_context->getCommandRegistry()->getShortcuts(commandId, m_userShortcuts) : QList<QKeySequence>();
}

QList<QKeySequence> PDFAppShortcutsModel::getDefaultKeys(const QString& commandId) const
{
    return m_context ? m_context->getCommandRegistry()->getShortcuts(commandId, PDFAppCommandRegistry::UserShortcuts()) : QList<QKeySequence>();
}

bool PDFAppShortcutsModel::isChanged(const QString& commandId) const
{
    // A stored row, which is the default of a command without alternate keys (or a
    // stored text, which is not a key sequence - the default applies), is no change
    return m_userShortcuts.count(commandId) && getKeys(commandId) != getDefaultKeys(commandId);
}

QStringList PDFAppShortcutsModel::getChangedCommands() const
{
    QStringList commands;
    for (const QString& commandId : m_commandIds)
    {
        if (isChanged(commandId))
        {
            commands << commandId;
        }
    }
    return commands;
}

QList<QKeySequence> PDFAppShortcutsModel::getAssignedKeys(const QString& commandId, const QKeySequence& shortcut) const
{
    if (shortcut.isEmpty())
    {
        return QList<QKeySequence>();
    }

    // The default shortcut is stored as no change: the alternate keys come with it
    if (shortcut == getDefaultShortcut(commandId))
    {
        return getDefaultKeys(commandId);
    }
    return { shortcut };
}

QStringList PDFAppShortcutsModel::getConflictingCommands(const QString& commandId, const QList<QKeySequence>& keys) const
{
    QStringList conflicts;
    if (!m_context)
    {
        return conflicts;
    }

    for (const QKeySequence& key : keys)
    {
        for (const QString& conflict : m_context->getCommandRegistry()->getConflictingCommands(commandId, key, m_userShortcuts))
        {
            if (!conflicts.contains(conflict))
            {
                conflicts << conflict;
            }
        }
    }
    return conflicts;
}

QString PDFAppShortcutsModel::getCommandTitle(const QString& commandId) const
{
    const PDFAppCommand* command = m_context ? m_context->getCommandRegistry()->findCommand(commandId) : nullptr;
    return command ? command->getTitle() : commandId;
}

QStringList PDFAppShortcutsModel::getConflicts(const QString& commandId, const QKeySequence& shortcut) const
{
    return getConflictingCommands(commandId, getAssignedKeys(commandId, shortcut));
}

QString PDFAppShortcutsModel::getRefusalReason(const QKeySequence& shortcut)
{
    if (shortcut.isEmpty())
    {
        return QString();
    }

    const QString text = getShortcutText(shortcut);
    for (int i = 0; i < shortcut.count(); ++i)
    {
        const QKeyCombination combination = shortcut[i];
        const Qt::Key key = combination.key();
        const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers() & ~Qt::KeypadModifier;

        switch (key)
        {
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                return tr("%1 moves the focus or closes a dialog and cannot be a shortcut.").arg(text);

            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (modifiers == Qt::NoModifier)
                {
                    return tr("%1 confirms dialogs and cannot be a shortcut.").arg(text);
                }
                break;

            case Qt::Key_F4:
                if (modifiers == Qt::AltModifier)
                {
                    return tr("%1 closes the window and cannot be a shortcut.").arg(text);
                }
                break;

            case Qt::Key_Shift:
            case Qt::Key_Control:
            case Qt::Key_Meta:
            case Qt::Key_Alt:
            case Qt::Key_AltGr:
            case Qt::Key_unknown:
                return tr("A modifier key alone cannot be a shortcut.");

            default:
                break;
        }
    }

    return QString();
}

QString PDFAppShortcutsModel::getShortcutText(const QKeySequence& shortcut)
{
    return shortcut.isEmpty() ? tr("None") : shortcut.toString(QKeySequence::NativeText);
}

PDFDatabaseError PDFAppShortcutsModel::assign(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts)
{
    if (!m_context || !m_context->getDatabase())
    {
        return reportError(PDFDatabaseError::fromMessage(tr("The settings are not available.")));
    }

    const PDFAppCommandRegistry* registry = m_context->getCommandRegistry();
    if (!registry->findCommand(commandId) || shortcut.isEmpty() || !getRefusalReason(shortcut).isEmpty())
    {
        return reportError(PDFDatabaseError::fromMessage(tr("Invalid shortcut.")));
    }

    // The shortcut of the command; the default one is no own shortcut (its row is
    // removed, the alternate keys apply again and they are taken from the others)
    std::map<QString, QString> shortcuts;
    QStringList resetCommandIds;
    if (shortcut == getDefaultShortcut(commandId))
    {
        resetCommandIds << commandId;
    }
    else
    {
        shortcuts[commandId] = PDFAppCommandRegistry::toStoredText(shortcut);
    }
    const QList<QKeySequence> keys = getAssignedKeys(commandId, shortcut);

    for (const QString& conflict : conflicts)
    {
        if (conflict == commandId || !registry->findCommand(conflict))
        {
            continue;
        }

        // Its own shortcut is taken: none; only an alternate key: the own shortcut
        // is stored, which turns the alternate keys off
        const QKeySequence conflictShortcut = registry->getShortcut(conflict, m_userShortcuts);
        bool isOwnShortcutTaken = false;
        for (const QKeySequence& key : keys)
        {
            isOwnShortcutTaken = isOwnShortcutTaken || PDFAppCommandRegistry::isConflict(key, conflictShortcut);
        }
        shortcuts[conflict] = isOwnShortcutTaken ? QString() : PDFAppCommandRegistry::toStoredText(conflictShortcut);
    }

    const PDFDatabaseError error = m_context->getShortcuts().setShortcuts(shortcuts, resetCommandIds);
    if (error.isError())
    {
        return reportError(error);
    }

    refresh();
    return PDFDatabaseError();
}

PDFDatabaseError PDFAppShortcutsModel::reportError(const PDFDatabaseError& error)
{
    // From the event loop: the caller may be inside of the commit of the editor of
    // the delegate or of a slot of the conflict dialog
    QMetaObject::invokeMethod(this, &PDFAppShortcutsModel::emitWriteFailed, Qt::QueuedConnection, error.getMessage());
    return error;
}

QString PDFAppShortcutsModel::getShortcutsText(const QList<QKeySequence>& shortcuts)
{
    QStringList texts;
    for (const QKeySequence& shortcut : shortcuts)
    {
        if (!shortcut.isEmpty())
        {
            texts << getShortcutText(shortcut);
        }
    }
    return texts.isEmpty() ? getShortcutText(QKeySequence()) : texts.join(QStringLiteral(", "));
}

PDFDatabaseError PDFAppShortcutsModel::writeShortcut(const QString& commandId, const QKeySequence& shortcut)
{
    if (!m_context || !m_context->getDatabase())
    {
        return PDFDatabaseError::fromMessage(tr("The settings are not available."));
    }

    PDFShortcutRepository repository = m_context->getShortcuts();
    const bool isDefault = !shortcut.isEmpty() && shortcut == getDefaultShortcut(commandId);
    const PDFDatabaseError error = isDefault ? repository.resetShortcut(commandId)
                                             : repository.setShortcut(commandId, PDFAppCommandRegistry::toStoredText(shortcut));
    if (error.isError())
    {
        QMetaObject::invokeMethod(this, &PDFAppShortcutsModel::emitWriteFailed, Qt::QueuedConnection, error.getMessage());
        return error;
    }

    refresh();
    return PDFDatabaseError();
}

PDFDatabaseError PDFAppShortcutsModel::resetShortcut(const QString& commandId)
{
    if (!m_context || !m_context->getDatabase())
    {
        return PDFDatabaseError::fromMessage(tr("The settings are not available."));
    }

    const PDFDatabaseError error = m_context->getShortcuts().resetShortcut(commandId);
    if (error.isError())
    {
        QMetaObject::invokeMethod(this, &PDFAppShortcutsModel::emitWriteFailed, Qt::QueuedConnection, error.getMessage());
        return error;
    }

    refresh();
    return PDFDatabaseError();
}

void PDFAppShortcutsModel::emitConflictFound(const QString& commandId, const QKeySequence& shortcut, const QStringList& conflicts)
{
    Q_EMIT conflictFound(commandId, shortcut, conflicts);
}

void PDFAppShortcutsModel::emitShortcutRefused(const QString& commandId, const QKeySequence& shortcut, const QString& reason)
{
    Q_EMIT shortcutRefused(commandId, shortcut, reason);
}

void PDFAppShortcutsModel::emitWriteFailed(const QString& message)
{
    Q_EMIT writeFailed(message);
}

PDFAppShortcutsFilterModel::PDFAppShortcutsFilterModel(QObject* parent) :
    BaseClass(parent)
{
    setDynamicSortFilter(true);
}

PDFAppShortcutsFilterModel::~PDFAppShortcutsFilterModel() = default;

void PDFAppShortcutsFilterModel::setFilterText(const QString& text)
{
    const QString filterText = text.left(MAXIMUM_FILTER_LENGTH).simplified();
    if (filterText != m_filterText)
    {
        // endFilterChange exists since Qt 6.10 (the project builds with Qt 6.9 too)
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        m_filterText = filterText;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        m_filterText = filterText;
        invalidateRowsFilter();
#endif
    }
}

bool PDFAppShortcutsFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_filterText.isEmpty())
    {
        return true;
    }

    const QAbstractItemModel* model = sourceModel();
    const QModelIndex index = model ? model->index(sourceRow, 0, sourceParent) : QModelIndex();
    if (!index.isValid())
    {
        return false;
    }

    QStringList texts = { index.data(Qt::DisplayRole).toString() };
    const QKeySequence shortcut = index.data(PDFComponentRoles::ShortcutRole).value<QKeySequence>();
    if (!shortcut.isEmpty())
    {
        texts << shortcut.toString(QKeySequence::NativeText) << shortcut.toString(QKeySequence::PortableText);
    }

    // The English title and the id of a command (a translated user interface)
    texts << index.data(PDFAppShortcutsModel::SearchTextRole).toStringList();

    for (const QString& text : texts)
    {
        if (text.contains(m_filterText, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

}   // namespace pdf
