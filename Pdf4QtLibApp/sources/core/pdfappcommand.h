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

#ifndef PDFAPPCOMMAND_H
#define PDFAPPCOMMAND_H

#include "pdfappglobal.h"

#include <QKeySequence>
#include <QString>
#include <QStringList>

#include <deque>
#include <map>
#include <vector>

namespace pdf
{

/// Command of the application: the data, which the global search (step 2), the
/// Shortcuts section of Settings (step 5) and the shell need. There are no widgets
/// here - the shell binds each command to a QAction by its id.
///
/// Texts are source texts in English translated in the context TRANSLATION_CONTEXT
/// (mark them by QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "...")).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommand
{
public:
    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFAppCommands";

    /// Stable id "group.name" (for example window.newTab), stored in the table shortcut
    QString id;

    /// Title (source text)
    const char* title = "";

    /// Name of the icon of PDFThemeIcons (empty = no icon)
    QString iconName;

    /// Default shortcut (empty = none)
    QKeySequence defaultShortcut;

    /// Keywords for the search (English, lowercase)
    QStringList keywords;

    /// Group of the Shortcuts section and of the search (source text, e.g. "Window")
    const char* group = "";

    QString getTitle() const;
    QString getGroupTitle() const;

    /// Id has the form group.name
    static bool isValidId(const QString& id);
};

/// Two or more commands with the same shortcut (or one shortcut is the beginning
/// of the other one, Ctrl+K and Ctrl+K, Ctrl+D)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppShortcutConflict
{
public:
    QKeySequence shortcut;
    QStringList commandIds;
};

/// Registry of the commands of the application. Commands are registered at the start
/// (PDFAppCommands::registerBuiltinCommands); pointers stay valid for the lifetime of
/// the registry. Shortcuts of the user come from PDFShortcutRepository::getShortcuts
/// (command id -> portable text of the keys, empty = removed by the user).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommandRegistry
{
public:
    using UserShortcuts = std::map<QString, QString>;

    PDFAppCommandRegistry() = default;

    PDFAppCommandRegistry(const PDFAppCommandRegistry&) = delete;
    PDFAppCommandRegistry& operator=(const PDFAppCommandRegistry&) = delete;

    /// Registers the command. \returns false for an invalid id or an id, which is
    /// already registered (the first command is kept)
    bool registerCommand(PDFAppCommand command);

    /// Command of the id, or nullptr
    const PDFAppCommand* findCommand(const QString& id) const;

    /// All commands in the order of the registration
    std::vector<const PDFAppCommand*> getCommands() const;

    /// Effective shortcut of the command: the valid shortcut of the user, otherwise
    /// the default one. A stored text, which is not a valid key sequence, is ignored.
    QKeySequence getShortcut(const QString& id, const UserShortcuts& userShortcuts) const;

    /// Conflicts of the effective shortcuts of all commands
    std::vector<PDFAppShortcutConflict> findConflicts(const UserShortcuts& userShortcuts) const;

    /// Commands, whose effective shortcut conflicts with \p shortcut, if it is
    /// assigned to the command \p id (the command itself is not reported)
    QStringList getConflictingCommands(const QString& id, const QKeySequence& shortcut, const UserShortcuts& userShortcuts) const;

    /// Text of the shortcut for the table shortcut
    static QString toStoredText(const QKeySequence& shortcut);

    /// Shortcut from the stored text. \returns false in \p ok for a text, which is not
    /// a valid key sequence (an empty text is valid - no shortcut)
    static QKeySequence fromStoredText(const QString& text, bool* ok);

    /// The two shortcuts conflict (equal, or one is the beginning of the other)
    static bool isConflict(const QKeySequence& first, const QKeySequence& second);

private:
    std::deque<PDFAppCommand> m_commands;
    std::map<QString, size_t> m_index;
};

/// Ids and registration of the built-in commands
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCommands
{
public:
    PDFAppCommands() = delete;

    // Window (step 1)
    static QString newTab() { return QStringLiteral("window.newTab"); }
    static QString closeTab() { return QStringLiteral("window.closeTab"); }
    static QString nextTab() { return QStringLiteral("window.nextTab"); }
    static QString previousTab() { return QStringLiteral("window.previousTab"); }
    static QString quit() { return QStringLiteral("window.quit"); }

    // File (step 2)
    static QString open() { return QStringLiteral("file.open"); }

    // Navigation (step 1)
    static QString back() { return QStringLiteral("navigation.back"); }
    static QString forward() { return QStringLiteral("navigation.forward"); }
    static QString search() { return QStringLiteral("navigation.search"); }
    static QString help() { return QStringLiteral("navigation.help"); }

    /// Registers all built-in commands
    static void registerBuiltinCommands(PDFAppCommandRegistry* registry);

private:
    static void registerWindowCommands(PDFAppCommandRegistry* registry);
    static void registerFileCommands(PDFAppCommandRegistry* registry);
    static void registerNavigationCommands(PDFAppCommandRegistry* registry);
    static void add(PDFAppCommandRegistry* registry, QString id, const char* title, const char* group,
                    QString iconName, QKeySequence shortcut, QStringList keywords);
};

}   // namespace pdf

#endif // PDFAPPCOMMAND_H
