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
                         QString iconName, QKeySequence shortcut, QStringList keywords,
                         PDFAppCommandScope scope, bool isCheckable)
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
    add(registry, zoomOut(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Zoom out"), viewGroup, QStringLiteral("minus"),
        QKeySequence(Qt::CTRL | Qt::Key_Minus), { QStringLiteral("smaller") }, document);
    add(registry, actualSize(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Actual size"), viewGroup, QStringLiteral("scale"),
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
    add(registry, magnifier(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Magnifier"), viewGroup, QStringLiteral("loupe"),
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
    add(registry, nextBookmark(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Next bookmark"), bookmarksGroup, QStringLiteral("chevron-down"),
        QKeySequence(Qt::CTRL | Qt::Key_Period), { QStringLiteral("my bookmarks") }, document);
    add(registry, previousBookmark(), QT_TRANSLATE_NOOP("pdf::PDFAppCommands", "Previous bookmark"), bookmarksGroup, QStringLiteral("chevron-up"),
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

}   // namespace pdf
