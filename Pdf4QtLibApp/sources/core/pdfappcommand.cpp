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

#include "pdfappcommand.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace pdf
{

QString PDFAppCommand::getTitle() const
{
    return QCoreApplication::translate(TRANSLATION_CONTEXT, title ? title : "");
}

QString PDFAppCommand::getGroupTitle() const
{
    return QCoreApplication::translate(TRANSLATION_CONTEXT, group ? group : "");
}

bool PDFAppCommand::isValidId(const QString& id)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z][A-Za-z0-9]*(\\.[a-z][A-Za-z0-9]*)+$"));
    return id.size() <= 128 && expression.match(id).hasMatch();
}

bool PDFAppCommandRegistry::registerCommand(PDFAppCommand command)
{
    if (!PDFAppCommand::isValidId(command.id) || m_index.count(command.id))
    {
        return false;
    }

    m_index[command.id] = m_commands.size();
    m_commands.push_back(std::move(command));
    return true;
}

const PDFAppCommand* PDFAppCommandRegistry::findCommand(const QString& id) const
{
    auto it = m_index.find(id);
    if (it == m_index.end() || it->second >= m_commands.size())
    {
        return nullptr;
    }
    return &m_commands[it->second];
}

std::vector<const PDFAppCommand*> PDFAppCommandRegistry::getCommands() const
{
    std::vector<const PDFAppCommand*> result;
    result.reserve(m_commands.size());
    for (const PDFAppCommand& command : m_commands)
    {
        result.push_back(&command);
    }
    return result;
}

QString PDFAppCommandRegistry::toStoredText(const QKeySequence& shortcut)
{
    return shortcut.toString(QKeySequence::PortableText);
}

QKeySequence PDFAppCommandRegistry::fromStoredText(const QString& text, bool* ok)
{
    if (text.trimmed().isEmpty())
    {
        if (ok)
        {
            *ok = true;
        }
        return QKeySequence();
    }

    const QKeySequence shortcut = QKeySequence::fromString(text, QKeySequence::PortableText);
    bool isValid = !shortcut.isEmpty();
    for (int i = 0; isValid && i < shortcut.count(); ++i)
    {
        const Qt::Key key = shortcut[i].key();
        isValid = key != Qt::Key_unknown && key != 0;
    }

    if (ok)
    {
        *ok = isValid;
    }
    return isValid ? shortcut : QKeySequence();
}

QKeySequence PDFAppCommandRegistry::getShortcut(const QString& id, const UserShortcuts& userShortcuts) const
{
    const PDFAppCommand* command = findCommand(id);
    if (!command)
    {
        return QKeySequence();
    }

    auto it = userShortcuts.find(id);
    if (it != userShortcuts.end())
    {
        bool ok = false;
        QKeySequence shortcut = fromStoredText(it->second, &ok);
        if (ok)
        {
            return shortcut;
        }
    }

    return command->defaultShortcut;
}

bool PDFAppCommandRegistry::isConflict(const QKeySequence& first, const QKeySequence& second)
{
    if (first.isEmpty() || second.isEmpty())
    {
        return false;
    }
    return first.matches(second) != QKeySequence::NoMatch || second.matches(first) != QKeySequence::NoMatch;
}

std::vector<PDFAppShortcutConflict> PDFAppCommandRegistry::findConflicts(const UserShortcuts& userShortcuts) const
{
    std::vector<std::pair<QString, QKeySequence>> shortcuts;
    for (const PDFAppCommand& command : m_commands)
    {
        QKeySequence shortcut = getShortcut(command.id, userShortcuts);
        if (!shortcut.isEmpty())
        {
            shortcuts.emplace_back(command.id, shortcut);
        }
    }

    std::vector<PDFAppShortcutConflict> conflicts;
    std::vector<bool> isReported(shortcuts.size(), false);
    for (size_t i = 0; i < shortcuts.size(); ++i)
    {
        if (isReported[i])
        {
            continue;
        }

        PDFAppShortcutConflict conflict;
        conflict.shortcut = shortcuts[i].second;
        conflict.commandIds << shortcuts[i].first;
        for (size_t j = i + 1; j < shortcuts.size(); ++j)
        {
            if (!isReported[j] && isConflict(shortcuts[i].second, shortcuts[j].second))
            {
                conflict.commandIds << shortcuts[j].first;
                isReported[j] = true;
            }
        }

        if (conflict.commandIds.size() > 1)
        {
            isReported[i] = true;
            conflicts.push_back(std::move(conflict));
        }
    }

    return conflicts;
}

QStringList PDFAppCommandRegistry::getConflictingCommands(const QString& id, const QKeySequence& shortcut, const UserShortcuts& userShortcuts) const
{
    QStringList result;
    if (shortcut.isEmpty())
    {
        return result;
    }

    for (const PDFAppCommand& command : m_commands)
    {
        if (command.id != id && isConflict(shortcut, getShortcut(command.id, userShortcuts)))
        {
            result << command.id;
        }
    }
    return result;
}

void PDFAppCommands::add(PDFAppCommandRegistry* registry, QString id, const char* title, const char* group,
                         QString iconName, QKeySequence shortcut, QStringList keywords)
{
    PDFAppCommand command;
    command.id = std::move(id);
    command.title = title;
    command.group = group;
    command.iconName = std::move(iconName);
    command.defaultShortcut = std::move(shortcut);
    command.keywords = std::move(keywords);

    const QString commandId = command.id;
    if (!registry->registerCommand(std::move(command)))
    {
        qWarning("Invalid or duplicate command %s", qPrintable(commandId));
    }
}

void PDFAppCommands::registerBuiltinCommands(PDFAppCommandRegistry* registry)
{
    if (!registry)
    {
        return;
    }

    registerWindowCommands(registry);
    registerNavigationCommands(registry);
}

void PDFAppCommands::registerWindowCommands(PDFAppCommandRegistry* registry)
{
    const char* group = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Window");

    add(registry, newTab(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "New tab"), group, QStringLiteral("plus"),
        QKeySequence(Qt::CTRL | Qt::Key_T), { QStringLiteral("open"), QStringLiteral("tab") });
    add(registry, closeTab(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Close tab"), group, QStringLiteral("x"),
        QKeySequence(Qt::CTRL | Qt::Key_W), { QStringLiteral("close"), QStringLiteral("tab") });
    add(registry, nextTab(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Next tab"), group, QStringLiteral("chevron-right"),
        QKeySequence(Qt::CTRL | Qt::Key_Tab), { QStringLiteral("switch"), QStringLiteral("tab") });
    add(registry, previousTab(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Previous tab"), group, QStringLiteral("chevron-left"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), { QStringLiteral("switch"), QStringLiteral("tab") });
    add(registry, quit(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Quit"), group, QStringLiteral("power"),
        QKeySequence(Qt::CTRL | Qt::Key_Q), { QStringLiteral("exit"), QStringLiteral("close") });
}

void PDFAppCommands::registerNavigationCommands(PDFAppCommandRegistry* registry)
{
    const char* group = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Navigation");

    add(registry, back(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Back"), group, QStringLiteral("chevron-left"),
        QKeySequence(Qt::ALT | Qt::Key_Left), { QStringLiteral("history"), QStringLiteral("previous page") });
    add(registry, forward(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Forward"), group, QStringLiteral("chevron-right"),
        QKeySequence(Qt::ALT | Qt::Key_Right), { QStringLiteral("history"), QStringLiteral("next page") });
    add(registry, search(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Search"), group, QStringLiteral("search"),
        QKeySequence(Qt::CTRL | Qt::Key_K), { QStringLiteral("find"), QStringLiteral("tools"), QStringLiteral("commands"), QStringLiteral("settings"), QStringLiteral("help") });
}

}   // namespace pdf
