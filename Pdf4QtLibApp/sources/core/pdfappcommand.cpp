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

QList<QKeySequence> PDFAppCommandRegistry::getShortcuts(const QString& id, const UserShortcuts& userShortcuts) const
{
    QList<QKeySequence> shortcuts;
    const PDFAppCommand* command = findCommand(id);
    if (!command)
    {
        return shortcuts;
    }

    const QKeySequence shortcut = getShortcut(id, userShortcuts);
    if (!shortcut.isEmpty())
    {
        shortcuts << shortcut;
    }

    // The alternate keys belong to the default shortcut (a valid shortcut of the
    // user, also an empty one, replaces all keys of the command)
    bool isUserShortcut = false;
    auto it = userShortcuts.find(id);
    if (it != userShortcuts.end())
    {
        fromStoredText(it->second, &isUserShortcut);
    }
    if (!isUserShortcut)
    {
        for (const QKeySequence& alternate : command->alternateShortcuts)
        {
            if (!alternate.isEmpty() && !shortcuts.contains(alternate))
            {
                shortcuts << alternate;
            }
        }
    }
    return shortcuts;
}

bool PDFAppCommandRegistry::isScopeConflict(PDFAppCommandScope first, PDFAppCommandScope second)
{
    const bool isCanvasAndPanel = (first == PDFAppCommandScope::Canvas && second == PDFAppCommandScope::Panel) ||
                                  (first == PDFAppCommandScope::Panel && second == PDFAppCommandScope::Canvas);
    return !isCanvasAndPanel;
}

std::vector<PDFAppShortcutConflict> PDFAppCommandRegistry::findConflicts(const UserShortcuts& userShortcuts) const
{
    // Every key of every command (the shortcut and the alternate keys)
    struct Key
    {
        QString id;
        QKeySequence shortcut;
        PDFAppCommandScope scope = PDFAppCommandScope::Window;
    };

    std::vector<Key> shortcuts;
    for (const PDFAppCommand& command : m_commands)
    {
        for (const QKeySequence& shortcut : getShortcuts(command.id, userShortcuts))
        {
            shortcuts.push_back(Key{ command.id, shortcut, command.scope });
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
        conflict.shortcut = shortcuts[i].shortcut;
        conflict.commandIds << shortcuts[i].id;
        for (size_t j = i + 1; j < shortcuts.size(); ++j)
        {
            if (!isReported[j] && shortcuts[j].id != shortcuts[i].id &&
                isScopeConflict(shortcuts[i].scope, shortcuts[j].scope) &&
                isConflict(shortcuts[i].shortcut, shortcuts[j].shortcut))
            {
                if (!conflict.commandIds.contains(shortcuts[j].id))
                {
                    conflict.commandIds << shortcuts[j].id;
                }
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

    const PDFAppCommand* assignedCommand = findCommand(id);
    const PDFAppCommandScope scope = assignedCommand ? assignedCommand->scope : PDFAppCommandScope::Window;
    for (const PDFAppCommand& command : m_commands)
    {
        if (command.id == id || !isScopeConflict(scope, command.scope))
        {
            continue;
        }

        for (const QKeySequence& commandShortcut : getShortcuts(command.id, userShortcuts))
        {
            if (isConflict(shortcut, commandShortcut))
            {
                result << command.id;
                break;
            }
        }
    }
    return result;
}

void PDFAppCommands::add(PDFAppCommandRegistry* registry, QString id, const char* title, const char* group,
                         QString iconName, QKeySequence shortcut, QStringList keywords,
                         PDFAppCommandScope scope, bool isCheckable, QList<QKeySequence> alternateShortcuts)
{
    PDFAppCommand command;
    command.id = std::move(id);
    command.title = title;
    command.group = group;
    command.iconName = std::move(iconName);
    command.defaultShortcut = std::move(shortcut);
    command.keywords = std::move(keywords);
    command.scope = scope;
    command.isCheckable = isCheckable;
    command.alternateShortcuts = std::move(alternateShortcuts);

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
    registerFileCommands(registry);
    registerNavigationCommands(registry);
    registerDocumentCommands(registry);
    registerAnnotationCommands(registry);
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

void PDFAppCommands::registerFileCommands(PDFAppCommandRegistry* registry)
{
    const char* group = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "File");

    // Open… (Shortcuts of Settings, component Shortcut row 51:6118): the native
    // dialog of the system, the chosen files are opened in the window
    add(registry, open(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Open…"), group, QStringLiteral("folder-open"),
        QKeySequence(Qt::CTRL | Qt::Key_O), { QStringLiteral("browse"), QStringLiteral("file"), QStringLiteral("document"), QStringLiteral("pdf") });
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
    add(registry, help(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Help"), group, QStringLiteral("help-circle"),
        QKeySequence(Qt::Key_F1), { QStringLiteral("user manual"), QStringLiteral("documentation"), QStringLiteral("guide") });
}

void PDFAppCommands::registerDocumentCommands(PDFAppCommandRegistry* registry)
{
    // Commands of the document tab (step 3): the window dispatches them to the
    // current tab (PDFAppDocumentCommandTable says the package of each of them).
    // Canvas commands have their shortcuts on the canvas of the document view.
    const PDFAppCommandScope document = PDFAppCommandScope::Document;
    const PDFAppCommandScope canvas = PDFAppCommandScope::Canvas;

    const char* fileGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "File");
    add(registry, save(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Save"), fileGroup, QStringLiteral("save"),
        QKeySequence(Qt::CTRL | Qt::Key_S), { QStringLiteral("store"), QStringLiteral("write"), QStringLiteral("document") }, document);
    add(registry, saveAs(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Save as…"), fileGroup, QStringLiteral("save-as"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), { QStringLiteral("copy"), QStringLiteral("export"), QStringLiteral("file") }, document);
    add(registry, print(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Print…"), fileGroup, QStringLiteral("printer"),
        QKeySequence(Qt::CTRL | Qt::Key_P), { QStringLiteral("printer"), QStringLiteral("paper") }, document);
    add(registry, sendMail(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Send by e-mail…"), fileGroup, QStringLiteral("mail"),
        QKeySequence(), { QStringLiteral("email"), QStringLiteral("mail"), QStringLiteral("attach"), QStringLiteral("send") }, document);
    add(registry, autoRefresh(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Automatic refresh"), fileGroup, QStringLiteral("refresh-cw"),
        QKeySequence(), { QStringLiteral("reload"), QStringLiteral("watch"), QStringLiteral("changed on disk") }, document, true);
    add(registry, properties(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Document properties"), fileGroup, QStringLiteral("info"),
        QKeySequence(Qt::CTRL | Qt::Key_D), { QStringLiteral("metadata"), QStringLiteral("fonts"), QStringLiteral("security"), QStringLiteral("xmp") }, document);
    add(registry, lockDocument(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Lock document"), fileGroup, QStringLiteral("lock"),
        QKeySequence(), { QStringLiteral("read only"), QStringLiteral("protect"), QStringLiteral("unlock") }, document, true);
    add(registry, editCopy(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Edit a copy"), fileGroup, QStringLiteral("copy"),
        QKeySequence(), { QStringLiteral("duplicate"), QStringLiteral("locked") }, document);

    const char* editGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Edit");
    add(registry, undo(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Undo"), editGroup, QStringLiteral("undo"),
        QKeySequence(Qt::CTRL | Qt::Key_Z), { QStringLiteral("revert"), QStringLiteral("back") }, document);
    add(registry, redo(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Redo"), editGroup, QStringLiteral("redo"),
        QKeySequence(Qt::CTRL | Qt::Key_Y), { QStringLiteral("repeat") }, document);
    add(registry, copy(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Copy"), editGroup, QStringLiteral("copy"),
        QKeySequence(Qt::CTRL | Qt::Key_C), { QStringLiteral("clipboard"), QStringLiteral("text") }, canvas);
    add(registry, selectAll(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Select all"), editGroup, QStringLiteral("select-all"),
        QKeySequence(Qt::CTRL | Qt::Key_A), { QStringLiteral("text"), QStringLiteral("selection") }, canvas);
    add(registry, deselect(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Deselect"), editGroup, QStringLiteral("select-none"),
        QKeySequence(), { QStringLiteral("clear selection"), QStringLiteral("text") }, canvas);
    add(registry, selectText(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Select text"), editGroup, QStringLiteral("text-cursor"),
        QKeySequence(), { QStringLiteral("selection"), QStringLiteral("text tool") }, document, true);
    add(registry, selectTable(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Select table"), editGroup, QStringLiteral("table"),
        QKeySequence(), { QStringLiteral("table"), QStringLiteral("cells"), QStringLiteral("selection") }, document, true);
    add(registry, screenshot(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Screenshot"), editGroup, QStringLiteral("camera"),
        QKeySequence(), { QStringLiteral("capture"), QStringLiteral("image"), QStringLiteral("clipboard") }, document, true);
    add(registry, extractImage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Extract image"), editGroup, QStringLiteral("image"),
        QKeySequence(), { QStringLiteral("picture"), QStringLiteral("save image") }, document, true);

    const char* viewGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "View");
    add(registry, zoomIn(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Zoom in"), viewGroup, QStringLiteral("zoom-in"),
        QKeySequence(Qt::CTRL | Qt::Key_Plus), { QStringLiteral("magnify"), QStringLiteral("larger") }, document);
    add(registry, zoomOut(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Zoom out"), viewGroup, QStringLiteral("zoom-out"),
        QKeySequence(Qt::CTRL | Qt::Key_Minus), { QStringLiteral("smaller") }, document);
    add(registry, actualSize(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Actual size"), viewGroup, QStringLiteral("actual-size"),
        QKeySequence(Qt::CTRL | Qt::Key_0), { QStringLiteral("zoom"), QStringLiteral("100 %") }, document);
    add(registry, fitPage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Fit page"), viewGroup, QStringLiteral("maximize"),
        QKeySequence(), { QStringLiteral("zoom"), QStringLiteral("whole page") }, document);
    add(registry, fitWidth(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Fit width"), viewGroup, QStringLiteral("fit-width"),
        QKeySequence(), { QStringLiteral("zoom") }, document);
    add(registry, fitHeight(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Fit height"), viewGroup, QStringLiteral("fit-height"),
        QKeySequence(), { QStringLiteral("zoom") }, document);
    add(registry, rotateViewLeft(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate view left"), viewGroup, QStringLiteral("rotate-ccw"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus), { QStringLiteral("rotation"), QStringLiteral("counterclockwise") }, document);
    add(registry, rotateViewRight(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate view right"), viewGroup, QStringLiteral("rotate-cw"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus), { QStringLiteral("rotation"), QStringLiteral("clockwise") }, document);
    add(registry, layoutSinglePage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Single page"), viewGroup, QStringLiteral("file-tall"),
        QKeySequence(Qt::CTRL | Qt::Key_1), { QStringLiteral("page layout") }, document, true);
    add(registry, layoutContinuous(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Continuous"), viewGroup, QStringLiteral("layout-rows"),
        QKeySequence(Qt::CTRL | Qt::Key_2), { QStringLiteral("page layout"), QStringLiteral("scroll") }, document, true);
    add(registry, layoutTwoPages(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Two pages"), viewGroup, QStringLiteral("page-pair"),
        QKeySequence(Qt::CTRL | Qt::Key_3), { QStringLiteral("page layout"), QStringLiteral("spread") }, document, true);
    add(registry, layoutTwoColumns(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Two columns"), viewGroup, QStringLiteral("layout-columns"),
        QKeySequence(Qt::CTRL | Qt::Key_4), { QStringLiteral("page layout"), QStringLiteral("spread") }, document, true);
    add(registry, firstPageRight(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "First page on the right side"), viewGroup, QStringLiteral("split-spread"),
        QKeySequence(Qt::CTRL | Qt::Key_5), { QStringLiteral("page layout"), QStringLiteral("cover"), QStringLiteral("book") }, document, true);
    add(registry, showAnnotations(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Show annotations"), viewGroup, QStringLiteral("eye"),
        QKeySequence(), { QStringLiteral("comments"), QStringLiteral("markup"), QStringLiteral("hide") }, document, true);
    add(registry, colorModeNormal(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Normal colors"), viewGroup, QStringLiteral("droplet"),
        QKeySequence(), { QStringLiteral("color mode"), QStringLiteral("page colors") }, document, true);
    add(registry, colorModeInverted(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Inverted colors"), viewGroup, QStringLiteral("droplet"),
        QKeySequence(), { QStringLiteral("color mode"), QStringLiteral("dark"), QStringLiteral("night") }, document, true);
    add(registry, colorModeGrayscale(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Grayscale"), viewGroup, QStringLiteral("droplet"),
        QKeySequence(), { QStringLiteral("color mode"), QStringLiteral("gray") }, document, true);
    add(registry, colorModeHighContrast(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "High contrast"), viewGroup, QStringLiteral("droplet"),
        QKeySequence(), { QStringLiteral("color mode"), QStringLiteral("accessibility") }, document, true);
    add(registry, colorModeBitonal(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Bitonal"), viewGroup, QStringLiteral("droplet"),
        QKeySequence(), { QStringLiteral("color mode"), QStringLiteral("black and white") }, document, true);
    add(registry, colorModeCustom(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Custom colors"), viewGroup, QStringLiteral("droplet"),
        QKeySequence(), { QStringLiteral("color mode"), QStringLiteral("sepia"), QStringLiteral("amber") }, document, true);
    add(registry, toggleSidebar(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Show sidebar"), viewGroup, QStringLiteral("panel-left"),
        QKeySequence(Qt::Key_F4), { QStringLiteral("panel"), QStringLiteral("thumbnails"), QStringLiteral("outline") }, document, true);
    add(registry, fullScreen(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Full screen"), viewGroup, QStringLiteral("expand"),
        QKeySequence(Qt::Key_F11), { QStringLiteral("presentation"), QStringLiteral("fullscreen") }, document, true);
    add(registry, magnifier(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Magnifier"), viewGroup, QStringLiteral("magnifier"),
        QKeySequence(), { QStringLiteral("loupe"), QStringLiteral("zoom") }, document, true);
    add(registry, renderingIssues(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rendering issues"), viewGroup, QStringLiteral("alert-triangle"),
        QKeySequence(Qt::CTRL | Qt::Key_E), { QStringLiteral("errors"), QStringLiteral("warnings"), QStringLiteral("problems") }, document);

    const char* goToGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Go to");
    add(registry, firstPage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "First page"), goToGroup, QStringLiteral("chevron-first"),
        QKeySequence(Qt::Key_Home), { QStringLiteral("start"), QStringLiteral("beginning") }, canvas);
    add(registry, lastPage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Last page"), goToGroup, QStringLiteral("chevron-last"),
        QKeySequence(Qt::Key_End), { QStringLiteral("end") }, canvas);
    add(registry, nextPage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Next page"), goToGroup, QStringLiteral("chevron-right"),
        QKeySequence(Qt::Key_PageDown), { QStringLiteral("page down") }, canvas);
    add(registry, previousPage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Previous page"), goToGroup, QStringLiteral("chevron-left"),
        QKeySequence(Qt::Key_PageUp), { QStringLiteral("page up") }, canvas);
    add(registry, nextLine(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Next line"), goToGroup, QStringLiteral("chevron-down"),
        QKeySequence(Qt::Key_Down), { QStringLiteral("scroll down") }, canvas);
    add(registry, previousLine(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Previous line"), goToGroup, QStringLiteral("chevron-up"),
        QKeySequence(Qt::Key_Up), { QStringLiteral("scroll up") }, canvas);
    add(registry, goToPage(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Go to page…"), goToGroup, QStringLiteral("hash"),
        QKeySequence(Qt::CTRL | Qt::Key_G), { QStringLiteral("page number"), QStringLiteral("jump") }, document);

    const char* bookmarksGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Bookmarks");
    add(registry, addBookmark(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Add bookmark"), bookmarksGroup, QStringLiteral("bookmark-plus"),
        QKeySequence(Qt::CTRL | Qt::Key_M), { QStringLiteral("my bookmarks"), QStringLiteral("mark page") }, document);
    add(registry, nextBookmark(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Next bookmark"), bookmarksGroup, QStringLiteral("bookmark-next"),
        QKeySequence(Qt::CTRL | Qt::Key_Period), { QStringLiteral("my bookmarks") }, document);
    add(registry, previousBookmark(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Previous bookmark"), bookmarksGroup, QStringLiteral("bookmark-prev"),
        QKeySequence(Qt::CTRL | Qt::Key_Comma), { QStringLiteral("my bookmarks") }, document);
    add(registry, exportBookmarks(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Export bookmarks…"), bookmarksGroup, QStringLiteral("export"),
        QKeySequence(), { QStringLiteral("my bookmarks"), QStringLiteral("json") }, document);
    add(registry, importBookmarks(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Import bookmarks…"), bookmarksGroup, QStringLiteral("import"),
        QKeySequence(), { QStringLiteral("my bookmarks"), QStringLiteral("json") }, document);

    const char* pagesGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Pages");
    add(registry, rotatePagesLeft(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate pages left"), pagesGroup, QStringLiteral("rotate-ccw"),
        QKeySequence(), { QStringLiteral("rotation"), QStringLiteral("thumbnails") }, document);
    add(registry, rotatePagesRight(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate pages right"), pagesGroup, QStringLiteral("rotate-cw"),
        QKeySequence(), { QStringLiteral("rotation"), QStringLiteral("thumbnails") }, document);

    const char* searchGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Search");
    add(registry, find(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Find in document"), searchGroup, QStringLiteral("search"),
        QKeySequence(Qt::CTRL | Qt::Key_F), { QStringLiteral("text"), QStringLiteral("word") }, document);
    add(registry, findNext(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Find next"), searchGroup, QStringLiteral("chevron-down"),
        QKeySequence(Qt::Key_F3), { QStringLiteral("next result") }, document);
    add(registry, findPrevious(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Find previous"), searchGroup, QStringLiteral("chevron-up"),
        QKeySequence(Qt::SHIFT | Qt::Key_F3), { QStringLiteral("previous result") }, document);
    add(registry, readAloud(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Read aloud"), searchGroup, QStringLiteral("volume-2"),
        QKeySequence(), { QStringLiteral("speech"), QStringLiteral("text to speech"), QStringLiteral("voice") }, document, true);
}

void PDFAppCommands::registerAnnotationCommands(PDFAppCommandRegistry* registry)
{
    // Commands of the annotations, of the Insert panel and of the outline (step 4):
    // packages Annotate, AnnotationEdit and NotesOutline of PDFAppDocumentCommandTable.
    // The shortcuts of the text markups are those of tools.md 9.8 (Ctrl+Shift+U is
    // not used - IBus takes it on Linux); the keys of the selection of the
    // annotations work on the canvas only (Canvas), F2 of the outline only in the
    // Outline panel (Panel)
    const PDFAppCommandScope document = PDFAppCommandScope::Document;
    const PDFAppCommandScope canvas = PDFAppCommandScope::Canvas;
    const PDFAppCommandScope panel = PDFAppCommandScope::Panel;

    const char* annotateGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Annotate");
    add(registry, annotatePanel(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Annotate"), annotateGroup, QStringLiteral("highlighter"),
        QKeySequence(), { QStringLiteral("annotations"), QStringLiteral("markup"), QStringLiteral("comment"), QStringLiteral("panel") }, document);
    add(registry, annotateHighlight(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Highlight"), annotateGroup, QStringLiteral("highlighter"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), { QStringLiteral("highlight text"), QStringLiteral("markup"), QStringLiteral("marker") }, document, true);
    add(registry, annotateUnderline(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Underline"), annotateGroup, QStringLiteral("underline"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), { QStringLiteral("underline text"), QStringLiteral("markup") }, document, true);
    add(registry, annotateStrikeout(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Strikeout"), annotateGroup, QStringLiteral("strikethrough"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), { QStringLiteral("strike out"), QStringLiteral("strikethrough"), QStringLiteral("markup"), QStringLiteral("cross out") }, document, true);
    add(registry, annotateSquiggly(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Squiggly"), annotateGroup, QStringLiteral("squiggly"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q), { QStringLiteral("squiggly underline"), QStringLiteral("wavy"), QStringLiteral("markup") }, document, true);
    add(registry, annotateHighlightComment(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Highlight and comment"), annotateGroup, QStringLiteral("highlight-comment"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), { QStringLiteral("highlight"), QStringLiteral("comment"), QStringLiteral("note"), QStringLiteral("markup") }, document, true);
    // The icons of the tools are those of the tiles of the panels (Figma 196:3160:
    // line, scribble, eraser) - one icon per meaning in the menus, the search and
    // the Shortcuts section (PDFAppAnnotationTools::getIconName)
    add(registry, annotateDeleteTool(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Delete annotations"), annotateGroup, QStringLiteral("eraser"),
        QKeySequence(), { QStringLiteral("remove annotations"), QStringLiteral("eraser"), QStringLiteral("delete tool") }, document, true);
    add(registry, annotateMarkResults(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Mark search results as annotations"), annotateGroup, QStringLiteral("to-annotation"),
        QKeySequence(), { QStringLiteral("search"), QStringLiteral("highlight results"), QStringLiteral("mark all") }, document);

    const char* insertGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Insert");
    add(registry, insertPanel(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Insert"), insertGroup, QStringLiteral("plus-circle"),
        QKeySequence(), { QStringLiteral("add"), QStringLiteral("shapes"), QStringLiteral("stamp"), QStringLiteral("link"), QStringLiteral("note"), QStringLiteral("panel") }, document);
    add(registry, insertTextBox(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Text box"), insertGroup, QStringLiteral("text-box"),
        QKeySequence(), { QStringLiteral("free text"), QStringLiteral("typewriter"), QStringLiteral("add text") }, document, true);
    add(registry, insertStickyNote(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Sticky note"), insertGroup, QStringLiteral("note-note"),
        QKeySequence(), { QStringLiteral("comment"), QStringLiteral("note"), QStringLiteral("text annotation"), QStringLiteral("popup") }, document, true);
    add(registry, insertLine(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Line"), insertGroup, QStringLiteral("line"),
        QKeySequence(), { QStringLiteral("shape"), QStringLiteral("arrow"), QStringLiteral("draw") }, document, true);
    add(registry, insertPolyline(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Polyline"), insertGroup, QStringLiteral("polyline"),
        QKeySequence(), { QStringLiteral("shape"), QStringLiteral("path"), QStringLiteral("draw") }, document, true);
    add(registry, insertRectangle(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rectangle"), insertGroup, QStringLiteral("square"),
        QKeySequence(), { QStringLiteral("shape"), QStringLiteral("square"), QStringLiteral("box"), QStringLiteral("draw") }, document, true);
    add(registry, insertPolygon(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Polygon"), insertGroup, QStringLiteral("polygon"),
        QKeySequence(), { QStringLiteral("shape"), QStringLiteral("draw") }, document, true);
    add(registry, insertEllipse(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Ellipse"), insertGroup, QStringLiteral("circle"),
        QKeySequence(), { QStringLiteral("shape"), QStringLiteral("circle"), QStringLiteral("oval"), QStringLiteral("draw") }, document, true);
    add(registry, insertFreehand(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Freehand"), insertGroup, QStringLiteral("scribble"),
        QKeySequence(), { QStringLiteral("ink"), QStringLiteral("pen"), QStringLiteral("draw"), QStringLiteral("scribble") }, document, true);
    add(registry, insertWebLink(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Web link"), insertGroup, QStringLiteral("link"),
        QKeySequence(), { QStringLiteral("hyperlink"), QStringLiteral("url"), QStringLiteral("internet") }, document, true);
    add(registry, insertPageLink(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Link to page"), insertGroup, QStringLiteral("link-page"),
        QKeySequence(), { QStringLiteral("hyperlink"), QStringLiteral("go to page"), QStringLiteral("internal link") }, document, true);
    add(registry, insertStamp(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Stamp"), insertGroup, QStringLiteral("stamp"),
        QKeySequence(), { QStringLiteral("approved"), QStringLiteral("draft"), QStringLiteral("confidential") }, document, true);

    const char* editGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Edit");
    add(registry, cut(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Cut"), editGroup, QStringLiteral("clipboard-cut"),
        QKeySequence(Qt::CTRL | Qt::Key_X), { QStringLiteral("clipboard"), QStringLiteral("annotations") }, canvas);
    add(registry, paste(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Paste"), editGroup, QStringLiteral("clipboard"),
        QKeySequence(Qt::CTRL | Qt::Key_V), { QStringLiteral("clipboard"), QStringLiteral("annotations") }, canvas);
    add(registry, pasteInPlace(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Paste at original position"), editGroup, QStringLiteral("clipboard"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), { QStringLiteral("clipboard"), QStringLiteral("paste in place"), QStringLiteral("annotations") }, canvas);
    add(registry, deleteSelection(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Delete"), editGroup, QStringLiteral("trash"),
        QKeySequence(Qt::Key_Delete), { QStringLiteral("remove"), QStringLiteral("annotations") }, canvas);

    const char* annotationGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Annotation");
    // F2 as in the context menu of an annotation (Figma 1019:18817), Alt+Return
    // as a second key; F2 of the outline works only in the Outline panel
    add(registry, annotationProperties(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Annotation properties…"), annotationGroup, QStringLiteral("sliders"),
        QKeySequence(Qt::Key_F2), { QStringLiteral("color"), QStringLiteral("opacity"), QStringLiteral("border"), QStringLiteral("author") }, canvas, false,
        { QKeySequence(Qt::ALT | Qt::Key_Return) });
    add(registry, annotationComment(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Open comment"), annotationGroup, QStringLiteral("message-square"),
        QKeySequence(), { QStringLiteral("popup"), QStringLiteral("note"), QStringLiteral("text") }, document);
    add(registry, annotationReply(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Reply"), annotationGroup, QStringLiteral("corner-down-right"),
        QKeySequence(), { QStringLiteral("comment"), QStringLiteral("thread"), QStringLiteral("answer") }, document);
    add(registry, annotationGeometry(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Geometry…"), annotationGroup, QStringLiteral("transform"),
        QKeySequence(), { QStringLiteral("position"), QStringLiteral("size"), QStringLiteral("rotation"), QStringLiteral("coordinates") }, document);
    add(registry, annotationCopyToPages(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Copy to pages…"), annotationGroup, QStringLiteral("copy-to-pages"),
        QKeySequence(), { QStringLiteral("duplicate"), QStringLiteral("repeat"), QStringLiteral("all pages") }, document);
    add(registry, annotationHide(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Hide on page"), annotationGroup, QStringLiteral("eye-off"),
        QKeySequence(), { QStringLiteral("hidden"), QStringLiteral("invisible") }, document);
    add(registry, annotationSelectAll(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Select all annotations on displayed pages"), annotationGroup, QStringLiteral("select-all"),
        QKeySequence(), { QStringLiteral("selection"), QStringLiteral("annotations") }, document);
    add(registry, annotationRotateClockwise(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate clockwise"), annotationGroup, QStringLiteral("rotate-cw"),
        QKeySequence(), { QStringLiteral("rotation"), QStringLiteral("turn") }, document);
    add(registry, annotationRotateCounterclockwise(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate counterclockwise"), annotationGroup, QStringLiteral("rotate-ccw"),
        QKeySequence(), { QStringLiteral("rotation"), QStringLiteral("turn") }, document);
    add(registry, annotationRotate180(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rotate 180°"), annotationGroup, QStringLiteral("rotate-180"),
        QKeySequence(), { QStringLiteral("rotation"), QStringLiteral("upside down") }, document);
    add(registry, annotationFlipHorizontal(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Flip horizontally"), annotationGroup, QStringLiteral("swap"),
        QKeySequence(), { QStringLiteral("mirror") }, document);
    add(registry, annotationFlipVertical(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Flip vertically"), annotationGroup, QStringLiteral("arrow-up-down"),
        QKeySequence(), { QStringLiteral("mirror") }, document);
    add(registry, annotationAlignLeft(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Align left"), annotationGroup, QStringLiteral("obj-align-left"),
        QKeySequence(), { QStringLiteral("align"), QStringLiteral("arrange") }, document);
    add(registry, annotationAlignCenter(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Align center"), annotationGroup, QStringLiteral("obj-align-hcenter"),
        QKeySequence(), { QStringLiteral("align"), QStringLiteral("arrange") }, document);
    add(registry, annotationAlignRight(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Align right"), annotationGroup, QStringLiteral("obj-align-right"),
        QKeySequence(), { QStringLiteral("align"), QStringLiteral("arrange") }, document);
    add(registry, annotationAlignTop(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Align top"), annotationGroup, QStringLiteral("obj-align-top"),
        QKeySequence(), { QStringLiteral("align"), QStringLiteral("arrange") }, document);
    add(registry, annotationAlignMiddle(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Align middle"), annotationGroup, QStringLiteral("obj-align-vcenter"),
        QKeySequence(), { QStringLiteral("align"), QStringLiteral("arrange") }, document);
    add(registry, annotationAlignBottom(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Align bottom"), annotationGroup, QStringLiteral("obj-align-bottom"),
        QKeySequence(), { QStringLiteral("align"), QStringLiteral("arrange") }, document);
    add(registry, annotationDistributeHorizontally(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Distribute horizontally"), annotationGroup, QStringLiteral("distribute-h"),
        QKeySequence(), { QStringLiteral("spacing"), QStringLiteral("arrange") }, document);
    add(registry, annotationDistributeVertically(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Distribute vertically"), annotationGroup, QStringLiteral("distribute-v"),
        QKeySequence(), { QStringLiteral("spacing"), QStringLiteral("arrange") }, document);

    const char* outlineGroup = QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Outline");
    add(registry, outlineNewItem(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "New outline item"), outlineGroup, QStringLiteral("plus"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("table of contents"), QStringLiteral("add") }, document);
    add(registry, outlineRename(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Rename outline item"), outlineGroup, QStringLiteral("rename"),
        QKeySequence(Qt::Key_F2), { QStringLiteral("bookmarks"), QStringLiteral("title") }, panel);
    add(registry, outlineDelete(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Delete outline item"), outlineGroup, QStringLiteral("trash"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("remove") }, document);
    add(registry, outlineMoveUp(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Move outline item up"), outlineGroup, QStringLiteral("arrow-up"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("order") }, document);
    add(registry, outlineMoveDown(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Move outline item down"), outlineGroup, QStringLiteral("arrow-down"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("order") }, document);
    add(registry, outlineIndent(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Indent outline item"), outlineGroup, QStringLiteral("chevron-right"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("child"), QStringLiteral("level") }, document);
    add(registry, outlineOutdent(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Outdent outline item"), outlineGroup, QStringLiteral("chevron-left"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("parent"), QStringLiteral("level") }, document);
    add(registry, outlineSetTarget(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Set outline target to current view"), outlineGroup, QStringLiteral("destination"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("destination"), QStringLiteral("page") }, document);
    add(registry, outlineBold(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Bold outline item"), outlineGroup, QStringLiteral("type"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("style"), QStringLiteral("font") }, document, true);
    add(registry, outlineItalic(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Italic outline item"), outlineGroup, QStringLiteral("text-label"),
        QKeySequence(), { QStringLiteral("bookmarks"), QStringLiteral("style"), QStringLiteral("font") }, document, true);
}
}   // namespace pdf
