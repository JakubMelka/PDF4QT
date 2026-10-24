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

/// Where the shortcut of a command works (step 3 plan, decision D12)
enum class PDFAppCommandScope
{
    Window,     ///< The whole window (an action of the main window, Qt::WindowShortcut)
    Document,   ///< A command of the current document tab (an action of the main window,
                ///< Qt::WindowShortcut, dispatched to the current tab, disabled without a document)
    Canvas,     ///< Keys of the page canvas (Ctrl+C, Ctrl+A, arrows, PgUp / PgDn, Home / End):
                ///< an action of the canvas of the document view, Qt::WidgetWithChildrenShortcut,
                ///< so the keys keep their meaning in the fields of the window
    Panel       ///< Keys of a panel of the sidebar (F2 renames an outline item, step 4): the
                ///< window action has no shortcut, the panel creates its own action with
                ///< Qt::WidgetWithChildrenShortcut, so the key works only in the panel
};

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

    /// Further keys of the command, which work as long as the user has not
    /// changed its shortcut (annotation.properties: F2 and Alt+Return). Menus,
    /// tooltips and the search show only the shortcut.
    QList<QKeySequence> alternateShortcuts;

    /// Keywords for the search (English, lowercase)
    QStringList keywords;

    /// Group of the Shortcuts section and of the search (source text, e.g. "Window")
    const char* group = "";

    /// Where the shortcut works
    PDFAppCommandScope scope = PDFAppCommandScope::Window;

    /// The command is an on / off state (the action is checkable)
    bool isCheckable = false;

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

    /// All keys of the command for its QAction: the effective shortcut and, while
    /// the user has not changed it, the alternate shortcuts
    QList<QKeySequence> getShortcuts(const QString& id, const UserShortcuts& userShortcuts) const;

    /// Conflicts of the effective shortcuts (and of the alternate keys) of all
    /// commands. A key of the canvas and a key of a panel of the sidebar do not
    /// conflict (they work in different widgets, isScopeConflict).
    std::vector<PDFAppShortcutConflict> findConflicts(const UserShortcuts& userShortcuts) const;

    /// Commands, whose keys conflict with \p shortcut, if it is assigned to the
    /// command \p id (the command itself is not reported)
    QStringList getConflictingCommands(const QString& id, const QKeySequence& shortcut, const UserShortcuts& userShortcuts) const;

    /// Text of the shortcut for the table shortcut
    static QString toStoredText(const QKeySequence& shortcut);

    /// Shortcut from the stored text. \returns false in \p ok for a text, which is not
    /// a valid key sequence (an empty text is valid - no shortcut)
    static QKeySequence fromStoredText(const QString& text, bool* ok);

    /// The two shortcuts conflict (equal, or one is the beginning of the other)
    static bool isConflict(const QKeySequence& first, const QKeySequence& second);

    /// The keys of the two scopes can conflict: everything except a key of the
    /// canvas against a key of a panel (both are actions with
    /// Qt::WidgetWithChildrenShortcut of two different widgets)
    static bool isScopeConflict(PDFAppCommandScope first, PDFAppCommandScope second);

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

    // Document tab (step 3): file
    static QString save() { return QStringLiteral("file.save"); }
    static QString saveAs() { return QStringLiteral("file.saveAs"); }
    static QString print() { return QStringLiteral("file.print"); }
    static QString sendMail() { return QStringLiteral("file.sendMail"); }
    static QString autoRefresh() { return QStringLiteral("file.autoRefresh"); }
    static QString properties() { return QStringLiteral("file.properties"); }
    static QString lockDocument() { return QStringLiteral("file.lock"); }
    static QString editCopy() { return QStringLiteral("file.editCopy"); }

    // Document tab (step 3): edit and selection
    static QString undo() { return QStringLiteral("edit.undo"); }
    static QString redo() { return QStringLiteral("edit.redo"); }
    static QString copy() { return QStringLiteral("edit.copy"); }
    static QString selectAll() { return QStringLiteral("edit.selectAll"); }
    static QString deselect() { return QStringLiteral("edit.deselect"); }
    static QString selectText() { return QStringLiteral("select.text"); }
    static QString selectTable() { return QStringLiteral("select.table"); }
    static QString screenshot() { return QStringLiteral("select.screenshot"); }
    static QString extractImage() { return QStringLiteral("select.extractImage"); }

    // Document tab (step 3): view
    static QString zoomIn() { return QStringLiteral("view.zoomIn"); }
    static QString zoomOut() { return QStringLiteral("view.zoomOut"); }
    static QString actualSize() { return QStringLiteral("view.actualSize"); }
    static QString fitPage() { return QStringLiteral("view.fitPage"); }
    static QString fitWidth() { return QStringLiteral("view.fitWidth"); }
    static QString fitHeight() { return QStringLiteral("view.fitHeight"); }
    static QString rotateViewLeft() { return QStringLiteral("view.rotateLeft"); }
    static QString rotateViewRight() { return QStringLiteral("view.rotateRight"); }
    static QString layoutSinglePage() { return QStringLiteral("view.layoutSinglePage"); }
    static QString layoutContinuous() { return QStringLiteral("view.layoutContinuous"); }
    static QString layoutTwoPages() { return QStringLiteral("view.layoutTwoPages"); }
    static QString layoutTwoColumns() { return QStringLiteral("view.layoutTwoColumns"); }
    static QString firstPageRight() { return QStringLiteral("view.firstPageRight"); }
    static QString showAnnotations() { return QStringLiteral("view.showAnnotations"); }
    static QString colorModeNormal() { return QStringLiteral("view.colorModeNormal"); }
    static QString colorModeInverted() { return QStringLiteral("view.colorModeInverted"); }
    static QString colorModeGrayscale() { return QStringLiteral("view.colorModeGrayscale"); }
    static QString colorModeHighContrast() { return QStringLiteral("view.colorModeHighContrast"); }
    static QString colorModeBitonal() { return QStringLiteral("view.colorModeBitonal"); }
    static QString colorModeCustom() { return QStringLiteral("view.colorModeCustom"); }
    static QString toggleSidebar() { return QStringLiteral("view.sidebar"); }
    static QString fullScreen() { return QStringLiteral("view.fullScreen"); }
    static QString magnifier() { return QStringLiteral("view.magnifier"); }
    static QString renderingIssues() { return QStringLiteral("view.renderingIssues"); }

    // Document tab (step 3): go to
    static QString firstPage() { return QStringLiteral("goto.firstPage"); }
    static QString lastPage() { return QStringLiteral("goto.lastPage"); }
    static QString nextPage() { return QStringLiteral("goto.nextPage"); }
    static QString previousPage() { return QStringLiteral("goto.previousPage"); }
    static QString nextLine() { return QStringLiteral("goto.nextLine"); }
    static QString previousLine() { return QStringLiteral("goto.previousLine"); }
    static QString goToPage() { return QStringLiteral("goto.page"); }

    // Document tab (step 3): personal bookmarks and pages
    static QString addBookmark() { return QStringLiteral("bookmarks.add"); }
    static QString nextBookmark() { return QStringLiteral("bookmarks.next"); }
    static QString previousBookmark() { return QStringLiteral("bookmarks.previous"); }
    static QString exportBookmarks() { return QStringLiteral("bookmarks.export"); }
    static QString importBookmarks() { return QStringLiteral("bookmarks.import"); }
    static QString rotatePagesLeft() { return QStringLiteral("pages.rotateLeft"); }
    static QString rotatePagesRight() { return QStringLiteral("pages.rotateRight"); }

    // Document tab (step 3): search and speech
    static QString find() { return QStringLiteral("search.find"); }
    static QString findNext() { return QStringLiteral("search.findNext"); }
    static QString findPrevious() { return QStringLiteral("search.findPrevious"); }
    static QString readAloud() { return QStringLiteral("speech.readAloud"); }

    // Document tab (step 4, package A): Annotate and Insert
    static QString annotatePanel() { return QStringLiteral("annotate.panel"); }
    static QString insertPanel() { return QStringLiteral("insert.panel"); }
    static QString annotateHighlight() { return QStringLiteral("annotate.highlight"); }
    static QString annotateUnderline() { return QStringLiteral("annotate.underline"); }
    static QString annotateStrikeout() { return QStringLiteral("annotate.strikeout"); }
    static QString annotateSquiggly() { return QStringLiteral("annotate.squiggly"); }
    static QString annotateHighlightComment() { return QStringLiteral("annotate.highlightComment"); }
    static QString insertTextBox() { return QStringLiteral("insert.textBox"); }
    static QString insertStickyNote() { return QStringLiteral("insert.stickyNote"); }
    static QString insertLine() { return QStringLiteral("insert.line"); }
    static QString insertPolyline() { return QStringLiteral("insert.polyline"); }
    static QString insertRectangle() { return QStringLiteral("insert.rectangle"); }
    static QString insertPolygon() { return QStringLiteral("insert.polygon"); }
    static QString insertEllipse() { return QStringLiteral("insert.ellipse"); }
    static QString insertFreehand() { return QStringLiteral("insert.freehand"); }
    static QString insertWebLink() { return QStringLiteral("insert.webLink"); }
    static QString insertPageLink() { return QStringLiteral("insert.pageLink"); }
    static QString insertStamp() { return QStringLiteral("insert.stamp"); }
    static QString annotateDeleteTool() { return QStringLiteral("annotate.deleteTool"); }
    static QString annotateMarkResults() { return QStringLiteral("annotate.markResults"); }

    // Document tab (step 4, package B): editing of the annotations on the page
    static QString cut() { return QStringLiteral("edit.cut"); }
    static QString paste() { return QStringLiteral("edit.paste"); }
    static QString pasteInPlace() { return QStringLiteral("edit.pasteInPlace"); }
    static QString deleteSelection() { return QStringLiteral("edit.delete"); }
    static QString annotationProperties() { return QStringLiteral("annotation.properties"); }
    static QString annotationComment() { return QStringLiteral("annotation.comment"); }
    static QString annotationReply() { return QStringLiteral("annotation.reply"); }
    static QString annotationGeometry() { return QStringLiteral("annotation.geometry"); }
    static QString annotationCopyToPages() { return QStringLiteral("annotation.copyToPages"); }
    static QString annotationHide() { return QStringLiteral("annotation.hide"); }
    static QString annotationSelectAll() { return QStringLiteral("annotation.selectAll"); }
    static QString annotationRotateClockwise() { return QStringLiteral("annotation.rotateClockwise"); }
    static QString annotationRotateCounterclockwise() { return QStringLiteral("annotation.rotateCounterclockwise"); }
    static QString annotationRotate180() { return QStringLiteral("annotation.rotate180"); }
    static QString annotationFlipHorizontal() { return QStringLiteral("annotation.flipHorizontal"); }
    static QString annotationFlipVertical() { return QStringLiteral("annotation.flipVertical"); }
    static QString annotationAlignLeft() { return QStringLiteral("annotation.alignLeft"); }
    static QString annotationAlignCenter() { return QStringLiteral("annotation.alignCenter"); }
    static QString annotationAlignRight() { return QStringLiteral("annotation.alignRight"); }
    static QString annotationAlignTop() { return QStringLiteral("annotation.alignTop"); }
    static QString annotationAlignMiddle() { return QStringLiteral("annotation.alignMiddle"); }
    static QString annotationAlignBottom() { return QStringLiteral("annotation.alignBottom"); }
    static QString annotationDistributeHorizontally() { return QStringLiteral("annotation.distributeHorizontally"); }
    static QString annotationDistributeVertically() { return QStringLiteral("annotation.distributeVertically"); }

    // Document tab (step 4, package C): editing of the outline
    static QString outlineNewItem() { return QStringLiteral("outline.newItem"); }
    static QString outlineRename() { return QStringLiteral("outline.rename"); }
    static QString outlineDelete() { return QStringLiteral("outline.delete"); }
    static QString outlineMoveUp() { return QStringLiteral("outline.moveUp"); }
    static QString outlineMoveDown() { return QStringLiteral("outline.moveDown"); }
    static QString outlineIndent() { return QStringLiteral("outline.indent"); }
    static QString outlineOutdent() { return QStringLiteral("outline.outdent"); }
    static QString outlineSetTarget() { return QStringLiteral("outline.setTarget"); }
    static QString outlineBold() { return QStringLiteral("outline.bold"); }
    static QString outlineItalic() { return QStringLiteral("outline.italic"); }

    /// Registers all built-in commands
    static void registerBuiltinCommands(PDFAppCommandRegistry* registry);

private:
    static void registerWindowCommands(PDFAppCommandRegistry* registry);
    static void registerFileCommands(PDFAppCommandRegistry* registry);
    static void registerNavigationCommands(PDFAppCommandRegistry* registry);
    static void registerDocumentCommands(PDFAppCommandRegistry* registry);
    static void registerAnnotationCommands(PDFAppCommandRegistry* registry);
    static void add(PDFAppCommandRegistry* registry, QString id, const char* title, const char* group,
                    QString iconName, QKeySequence shortcut, QStringList keywords,
                    PDFAppCommandScope scope = PDFAppCommandScope::Window, bool isCheckable = false,
                    QList<QKeySequence> alternateShortcuts = QList<QKeySequence>());
};

}   // namespace pdf

#endif // PDFAPPCOMMAND_H
