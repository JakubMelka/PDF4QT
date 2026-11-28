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

#ifndef PDFAPPEDITCONTENTREGISTRY_H
#define PDFAPPEDITCONTENTREGISTRY_H

#include "pdfappglobal.h"

#include <QKeySequence>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QGraphicsItem;
class QMimeData;
class QObject;
class QWidget;

namespace pdf
{

class PDFAppEditContentCanvas;
class PDFAppEditContentCard;
class PDFAppEditContentContext;
class PDFAppEditContentItem;
class PDFAppEditContentScene;
class PDFAppEditContentTool;
class PDFContentContainer;
class PDFContentNode;

// Edit Content, stage E1, skeleton. Plan: zadani.md 11.2 (registries of the tool), zadani_gui.md
// 4.15 (factory of the scene items), 4.16 (tools), 4.18 (left tabs), 4.19 (cards), 4.20 (menus,
// keys), 4.25. Notes: GUI_overhaul/phase2_briefs/pe1_skeleton_notes.md section 4.
//
// What the parts of the tool are made of. A package adds its own file with its entry classes
// (one class per entry, no lambdas) and one static function registerParts(PDFAppEditContentRegistry*)
// of a class of that file, which adds its entries; PDFAppEditContentBuiltins::registerAll calls
// the functions of all files in a fixed order. pdfappeditcontentregistry.* is changed only by the
// coordinator.
//
// Threads and ownership (every type of this header): the registry owns its entries
// (std::unique_ptr<const ...>); an entry is immutable, has no state of a tool session and
// lives as long as the registry. The registry of the application
// (PDFAppEditContentRegistry::getInstance) is built once, at its first use in the GUI thread,
// and only read afterwards, in the GUI thread. What an entry creates (a tool, an item, a card, a
// panel) is owned by the caller named at the create function. An entry never keeps a pointer to
// a context or to anything it has created.

/// Identifiers of the tools of the canvas (zadani_gui.md 4.16). All stages are listed, so a
/// package names the tool of another package without its header; a tool exists only when the
/// file of its package has registered it (PDFAppEditContentRegistry::findTool returns nullptr
/// otherwise and the button, the key and the menu row of the tool do not exist: "hidden, not
/// disabled").
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentToolIds
{
public:
    PDFAppEditContentToolIds() = delete;

    static constexpr const char* SELECT = "select";                     ///< E1, P11 (V)
    static constexpr const char* TEXT = "text";                         ///< E2, P22 (T)
    static constexpr const char* TEXT_EDIT = "text.edit";               ///< E2, P22: typing in a text (a mode)
    static constexpr const char* IMAGE = "image";                       ///< E3, P33 (I)
    static constexpr const char* IMAGE_CROP = "image.crop";             ///< E3, P33 (a mode)
    static constexpr const char* SHAPE_RECTANGLE = "shape.rectangle";   ///< E3, P31 (R)
    static constexpr const char* SHAPE_ROUNDED_RECTANGLE = "shape.rounded-rectangle";
    static constexpr const char* SHAPE_ELLIPSE = "shape.ellipse";       ///< E3, P31 (E)
    static constexpr const char* SHAPE_LINE = "shape.line";             ///< E3, P31 (L)
    static constexpr const char* SHAPE_HORIZONTAL_LINE = "shape.horizontal-line";
    static constexpr const char* SHAPE_VERTICAL_LINE = "shape.vertical-line";
    static constexpr const char* SHAPE_DOT = "shape.dot";
    static constexpr const char* SHAPE_CHECK_MARK = "shape.check-mark";
    static constexpr const char* SHAPE_CROSS_MARK = "shape.cross-mark";
    static constexpr const char* PEN = "pen";                           ///< E3, P31 (N)
    static constexpr const char* FREEHAND = "freehand";                 ///< E3, P31 (F)
    static constexpr const char* CLIPPING_PATH = "clipping-path";       ///< E3, P31 (C)
    static constexpr const char* NODES = "nodes";                       ///< E3, P31: editing the nodes of a path or of a clipping path (a mode)
    static constexpr const char* EYEDROPPER = "eyedropper";             ///< E3, P34 (a mode; its argument is the row "fill" / "stroke")
    static constexpr const char* SHADING = "shading";                   ///< E4, P43 (a mode)
};

/// Identifiers of the cards of the right panel and their fixed order (zadani_gui.md 4.19: the
/// table from the top; the row "set of the cards of a path is always whole"). The order values
/// leave room between the cards; every stage is listed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCardIds
{
public:
    PDFAppEditContentCardIds() = delete;

    static constexpr const char* SELECTION = "selection";               ///< E1, P13: always the first
    static constexpr const char* LOCKED_OBJECT = "locked-object";       ///< E1, P13 (requested by a click on a locked object)
    static constexpr const char* GLYPH = "glyph";                       ///< E2, P22
    static constexpr const char* NODE = "node";                         ///< E3, P31
    static constexpr const char* CLIPPING = "clipping";                 ///< E3, P31
    static constexpr const char* IMAGE = "image";                       ///< E3, P33 (Image, Actions)
    static constexpr const char* CROP = "crop";                         ///< E3, P33
    static constexpr const char* SHARED_CONTENT = "shared-content";     ///< E4, P43
    static constexpr const char* SHADING = "shading";                   ///< E4, P43
    static constexpr const char* TILING_PATTERN = "tiling-pattern";     ///< E4, P43 (Tiling pattern, Actions)
    static constexpr const char* TEXT = "text";                         ///< E2, P22
    static constexpr const char* FILL_AND_STROKE = "fill-and-stroke";   ///< E1, P13 (Device spaces); E4, P41 replaces it (the same id, see addCard)
    static constexpr const char* TRANSPARENCY = "transparency";         ///< E1, P13
    static constexpr const char* PRINT_PRODUCTION = "print-production"; ///< E4, P41
    static constexpr const char* POSITION_AND_SIZE = "position-and-size";   ///< E1, P13
    static constexpr const char* LAYER_AND_TAG = "layer-and-tag";       ///< E4, P42 (the row Tag: E5, P52)
    static constexpr const char* ARRANGE = "arrange";                   ///< E1, P13: always the last of the selection
    static constexpr const char* LAYER = "layer";                       ///< E4, P42 (Layer, Content of the layer, Actions)
    static constexpr const char* TAG = "tag";                           ///< E5, P52 (Tag, Content of the tag, Actions)
    static constexpr const char* ARTIFACT = "artifact";                 ///< E5, P52
    static constexpr const char* GRID_AND_UNITS = "grid-and-units";     ///< E3, P32 (nothing selected and requested)
    static constexpr const char* ANNOTATIONS = "annotations";           ///< E1, P13 (nothing selected, the underlay is on)
    static constexpr const char* PREVIEW = "preview";                   ///< E1, P13 (Preview is on)
    static constexpr const char* KEYBOARD = "keyboard";                 ///< E1, P14 (requested by Ctrl+/ or View > Keyboard Shortcuts)

    /// The order of the card of the identifier (from the top; 100 apart), -1 for an unknown one
    static int getOrder(const QString& cardId);
};

/// Identifiers of the menus, which are built from the action entries
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentMenuIds
{
public:
    PDFAppEditContentMenuIds() = delete;

    static constexpr const char* CONTEXT = "context";       ///< The context menu of an object on the canvas and of a row of the tab Objects (edit-content-v2-objects-context-menu)
    static constexpr const char* VIEW = "view";             ///< View of the toolbar (edit-content-v2-menus-view)
    static constexpr const char* LAYER_ROW = "layer-row";   ///< The context menu of a row of the tab Layers (E4)
    static constexpr const char* TAG_ROW = "tag-row";       ///< The context menu of a row of the tab Structure (E5)

    /// The buttons of the toolbar of the tool after the tool buttons (edit-content-v2 1647:21313:
    /// "Multiple | Find, View, Preview | Clear Page"): the GUI of the tool (P14) builds a button for
    /// every action placed here, as it builds View - a checkable entry is a toggle button (its check
    /// is isChecked), another one a push button; both call PDFAppEditContentContext::triggerAction,
    /// lose their texts first in a narrow window (PDFAppToolToolbar::NARROW_FIRST_PROPERTY) and are
    /// hidden while their entry says isVisible false. A divider between two groups. So a later stage
    /// adds a button (Find of P23) by its entry, without a change of the GUI.
    static constexpr const char* TOOLBAR = "toolbar";

    /// Groups of the context menu in their order (a divider between two groups)
    static constexpr int GROUP_CLIPBOARD = 100;     ///< Cut ... Paste in Place
    static constexpr int GROUP_ORDER = 200;         ///< Bring to Front ... Send to Back
    static constexpr int GROUP_GROUPING = 300;      ///< Group, Ungroup, Make Clipping Group
    static constexpr int GROUP_CONVERT = 400;       ///< Convert to Paths, Merge Texts, Split Text
    static constexpr int GROUP_STATE = 500;         ///< Hide, Lock
    static constexpr int GROUP_ASSIGN = 600;        ///< Move to Layer, Tag as

    /// Groups of the menu View
    static constexpr int GROUP_SHOW = 100;          ///< Rulers ... Reading order
    static constexpr int GROUP_SNAP = 200;          ///< SNAP TO: Objects, Grid and guides
    static constexpr int GROUP_SETTINGS = 300;      ///< Grid and Units..., Keyboard Shortcuts

    /// Groups of the toolbar (TOOLBAR) and the orders of the frame. The toggle Multiple (order 100)
    /// and the menu button View (order 100) are buttons of the GUI itself, not entries.
    static constexpr int GROUP_TOOLBAR_MODES = 100;     ///< Multiple (100, the GUI), Find (200, P23 in E2)
    static constexpr int GROUP_TOOLBAR_VIEW = 200;      ///< View (100, the GUI), Preview (200, P14)
    static constexpr int GROUP_TOOLBAR_PAGE = 300;      ///< Clear Page (100, P14)
};

/// Identifiers of the actions (PDFAppEditContentActionEntry), which more packages name: a card
/// button or a key starts the action of another package by its identifier
/// (PDFAppEditContentContext::triggerAction). An action exists only when the file of its package
/// has registered it. The owner of each action: pe1_skeleton_notes.md section 4.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentActionIds
{
public:
    PDFAppEditContentActionIds() = delete;

    // Stage E1, P12 (pdfappeditcontentmenus.cpp): the actions of the selection. DELETE_SELECTION is
    // the one owner of "delete the selection" (which nodes: locked, hidden, children of a selected
    // group, a node outside of the edited root): the key Delete of the tool Select (P11), the key
    // Delete of the tree Objects (P12) and the routed application command edit.delete all call
    // PDFAppEditContentContext::triggerAction(DELETE_SELECTION). (Not "DELETE": winnt.h defines a
    // macro of that name.) SELECT_ALL answers edit.selectAll.
    static constexpr const char* DELETE_SELECTION = "delete";                       ///< Answers edit.delete (getApplicationCommandIds)
    static constexpr const char* SELECT_ALL = "select-all";                         ///< Answers edit.selectAll

    // Stage E1, P12 (pdfappeditcontentmenus.cpp): context menu, the card Arrange, keys
    static constexpr const char* ORDER_BRING_TO_FRONT = "order.bring-to-front";     ///< Ctrl+Shift+]
    static constexpr const char* ORDER_BRING_FORWARD = "order.bring-forward";       ///< Ctrl+]
    static constexpr const char* ORDER_SEND_BACKWARD = "order.send-backward";       ///< Ctrl+[
    static constexpr const char* ORDER_SEND_TO_BACK = "order.send-to-back";         ///< Ctrl+Shift+[
    static constexpr const char* GROUP = "group";                                   ///< Ctrl+Alt+G (proposal of pe1_skeleton_notes.md 8.2: Ctrl+G is Go to page of the application)
    static constexpr const char* UNGROUP = "ungroup";                               ///< Ctrl+Alt+Shift+G (the same proposal)
    static constexpr const char* HIDE = "hide";                                     ///< Ctrl+Alt+H; "Hide" / "Show"
    static constexpr const char* LOCK = "lock";                                     ///< Ctrl+Alt+L; "Lock" / "Unlock"
    static constexpr const char* DUPLICATE = "duplicate";                           ///< Ctrl+Shift+D
    static constexpr const char* VIEW_CLIPPING_PATHS = "view.clipping-paths";
    static constexpr const char* VIEW_INVISIBLE_TEXT = "view.invisible-text";
    static constexpr const char* VIEW_ANNOTATIONS = "view.annotations";

    // Stage E1, P14 (pdfappeditcontentkeyboard.cpp)
    static constexpr const char* PREVIEW = "preview";                               ///< The key P of the canvas
    static constexpr const char* VIEW_KEYBOARD = "view.keyboard";                   ///< Ctrl+/: the card Keyboard
    static constexpr const char* CLEAR_PAGE = "clear-page";

    // Stage E2: P23 (clipboard, Find), P22 (text). The clipboard actions answer the application
    // commands of the same names (getApplicationCommandIds); typing (P22) answers them first for its
    // characters through the hooks of its tool (PDFAppEditContentTool::getApplicationCommandState).
    static constexpr const char* PASTE_IN_PLACE = "paste-in-place";                 ///< Answers edit.pasteInPlace (Ctrl+Shift+V of the application); the row of the context menu
    static constexpr const char* CUT = "cut";                                       ///< Answers edit.cut
    static constexpr const char* COPY = "copy";                                     ///< Answers edit.copy
    static constexpr const char* PASTE = "paste";                                   ///< Answers edit.paste
    static constexpr const char* FIND = "find";                                     ///< Answers search.find (Ctrl+F of the application); the toggle of the toolbar (PDFAppEditContentMenuIds::TOOLBAR, GROUP_TOOLBAR_MODES, order 200), the panel by a canvas widget entry of the place FloatingPanel
    static constexpr const char* CONVERT_TO_PATHS = "convert-to-paths";
    static constexpr const char* MERGE_TEXTS = "merge-texts";
    static constexpr const char* SPLIT_TEXT = "split-text";

    // Stage E3: P31, P32
    static constexpr const char* MAKE_CLIPPING_GROUP = "make-clipping-group";
    static constexpr const char* VIEW_RULERS = "view.rulers";                       ///< Ctrl+R
    static constexpr const char* VIEW_GRID = "view.grid";                           ///< Ctrl+'
    static constexpr const char* VIEW_GUIDES = "view.guides";
    static constexpr const char* VIEW_SNAP_OBJECTS = "view.snap-objects";
    static constexpr const char* VIEW_SNAP_GRID = "view.snap-grid";
    static constexpr const char* VIEW_GRID_AND_UNITS = "view.grid-and-units";

    // Stage E4: P42; stage E5: P52
    static constexpr const char* MOVE_TO_LAYER = "move-to-layer";
    static constexpr const char* TAG_AS = "tag-as";
    static constexpr const char* VIEW_READING_ORDER = "view.reading-order";
};

/// Where a key of the tool acts (zadani_gui.md 4.20: the three ways of the keys)
enum class PDFAppEditContentKeyScope
{
    Canvas,     ///< An unmodified key handled by the key press of the view: only while the canvas has the focus
    Tool        ///< A shortcut with a modifier: a QAction with Qt::WidgetWithChildrenShortcut on the key widget of the host (the whole tool)
};

/// A tool of the canvas: what the toolbar, the keys and the controller need to know about it
/// without creating it, and its factory. One entry per tool.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentToolEntry
{
public:
    /// Where the tool is offered
    enum class Place
    {
        Toolbar,    ///< An icon check button of the exclusive group of the toolbar
        ShapeMenu,  ///< A row of the menu Shape; the button Shape shows the shape used last
        Mode        ///< No button: the tool is entered from another tool, a card or a double click (typing, nodes, crop, eyedropper, shading)
    };

    PDFAppEditContentToolEntry() = default;
    virtual ~PDFAppEditContentToolEntry();

    PDFAppEditContentToolEntry(const PDFAppEditContentToolEntry&) = delete;
    PDFAppEditContentToolEntry& operator=(const PDFAppEditContentToolEntry&) = delete;

    /// Identifier (PDFAppEditContentToolIds)
    virtual QString getId() const = 0;

    virtual Place getPlace() const = 0;

    /// Order inside of its place (the toolbar: Select 100, Text 200, Image 300, Shape 400, Pen
    /// 500, Freehand 600, Clipping Path 700; the menu Shape: the order of the frame
    /// edit-content-v2-menus-shape, 100 apart)
    virtual int getOrder() const = 0;

    /// Translated title ("Select") and description of the tooltip (the table of the text
    /// "row-note-Edit Content v2 - toolbar"); the tooltip shows title, key and description
    virtual QString getTitle() const = 0;
    virtual QString getDescription() const;

    /// Name of the icon of Pdf4QtLibFluent; empty for a mode
    virtual QString getIconName() const;

    /// The unmodified key of the canvas (V, T, I, R, E, L, N, F, C); empty = none (the default)
    virtual QKeySequence getKey() const;

    /// A double click on the node, or Enter with the node selected, starts this tool with the
    /// node as its target (text -> typing, path -> nodes, image -> crop; zadani_gui.md 4.16
    /// "contextual transition"). The tool Select asks the entries in the order of the registry and
    /// takes the first one. Default: false.
    virtual bool isEditorOf(const PDFContentNode& node, const PDFContentContainer& container) const;

    /// Creates the tool. \returns the tool; the caller (PDFAppEditContentToolController) owns
    /// it and deletes it with itself, never while a function of the tool runs.
    virtual std::unique_ptr<PDFAppEditContentTool> createTool(PDFAppEditContentContext* context) const = 0;
};

/// Layers of the scene from the bottom (zadani_gui.md 4.15); the z value of an item is its layer
/// times LAYER_STEP plus its order inside of the layer
enum class PDFAppEditContentSceneLayer
{
    Paper,          ///< The paper (crop box) on the gray area
    Underlay,       ///< The dimmed picture of the annotations and fields
    Content,        ///< The items of the nodes, in the order of the content stream
    Grid,
    Guides,
    Markers,        ///< Clipping paths, hidden objects, invisible text, reading order
    Selection,      ///< The one item of the frames and handles
    ToolPreview     ///< The one item, which paints the preview of the active tool; the text edit item of stage E2
};

/// What an item entry gets to create the item of a node
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentItemArguments
{
public:
    const PDFContentContainer* container = nullptr;     ///< The container of the node; valid only during the call
    const PDFContentNode* node = nullptr;               ///< The node; valid only during the call (the item copies what it draws or co-owns it by the shared pointers of the node data)
    PDFAppEditContentScene* scene = nullptr;            ///< The scene, which will own the item
    PDFAppEditContentItem* parent = nullptr;            ///< The item of the parent node (a group, a form use), nullptr for a child of the root
};

/// The factory of the scene item of a kind of node (zadani_gui.md 4.15, the table of the items).
/// The scene asks the entries by their priority and takes the first, which says isItemOf.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentItemEntry
{
public:
    PDFAppEditContentItemEntry() = default;
    virtual ~PDFAppEditContentItemEntry();

    PDFAppEditContentItemEntry(const PDFAppEditContentItemEntry&) = delete;
    PDFAppEditContentItemEntry& operator=(const PDFAppEditContentItemEntry&) = delete;

    /// Identifier for the tests and the diagnostics ("path", "text-line", "image", "opaque")
    virtual QString getId() const = 0;

    /// Entries of a higher priority are asked first; entries of the same priority in the order of
    /// their registration. Default: 0. A later stage replaces the item of a kind by an entry of a
    /// higher priority (the shading item of P43 over the fallback picture of P11).
    virtual int getPriority() const;

    /// This entry draws the node
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer& container) const = 0;

    /// Creates the item (new). The scene owns it: it adds it to itself and deletes it only from
    /// the event loop (PDFAppEditContentScene). The entry must not keep the pointer. GUI thread.
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const = 0;
};

/// A permanent item of the scene, which is no content: the grid, the guides, the snapping lines
/// (P32), the numbers of the reading order (P52). Created once with the scene and deleted with it;
/// it reads its state from the context when it paints and accepts no events.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentOverlayEntry
{
public:
    PDFAppEditContentOverlayEntry() = default;
    virtual ~PDFAppEditContentOverlayEntry();

    PDFAppEditContentOverlayEntry(const PDFAppEditContentOverlayEntry&) = delete;
    PDFAppEditContentOverlayEntry& operator=(const PDFAppEditContentOverlayEntry&) = delete;

    virtual QString getId() const = 0;
    virtual PDFAppEditContentSceneLayer getLayer() const = 0;

    /// Creates the item (new; not a QObject: no QGraphicsObject, zadani_gui.md 4.14). The scene
    /// owns it. The item may keep \p context only as a QPointer.
    virtual QGraphicsItem* createOverlay(PDFAppEditContentContext* context, PDFAppEditContentScene* scene) const = 0;
};

/// A widget of the canvas around the view (the rulers of P32, the bar of the group of P43).
/// The canvas creates the widgets of all entries when it is created.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCanvasWidgetEntry
{
public:
    /// Where the canvas puts the widget (the order from the top: ruler, bar, mode label)
    enum class Place
    {
        TopRuler,       ///< Above the view, as wide as the view
        LeftRuler,      ///< At the leading side of the view, as high as the view
        RulerCorner,    ///< The corner between the two rulers
        TopBar,         ///< Over the top edge of the view, below the ruler (the bar of the group); hidden when its sizeHint is empty
        FloatingPanel   ///< Over the view at its top trailing corner, 12 px from both edges, its sizeHint (the panel Find and Replace of P23, the frame -text-find-replace); the widget shows and hides itself (it starts hidden), the canvas only places it and keeps it inside of the view
    };

    PDFAppEditContentCanvasWidgetEntry() = default;
    virtual ~PDFAppEditContentCanvasWidgetEntry();

    PDFAppEditContentCanvasWidgetEntry(const PDFAppEditContentCanvasWidgetEntry&) = delete;
    PDFAppEditContentCanvasWidgetEntry& operator=(const PDFAppEditContentCanvasWidgetEntry&) = delete;

    virtual QString getId() const = 0;
    virtual Place getPlace() const = 0;

    /// Creates the widget (new, a child of \p canvas: a part of the tool, which dies with the
    /// canvas and keeps no state; it holds \p context by a QPointer and connects to it by
    /// PDFDeferredDeletion::connect). GUI thread.
    virtual QWidget* createWidget(PDFAppEditContentContext* context, PDFAppEditContentCanvas* canvas) const = 0;
};

/// What the canvas takes, when something is dropped onto it from outside (zadani_gui.md 4.16:
/// a file of an image or an image = AddImage, P33; a plain text = a new text, P23). The view
/// asks the entries in the order of their registration and takes the first, which can.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentDropEntry
{
public:
    PDFAppEditContentDropEntry() = default;
    virtual ~PDFAppEditContentDropEntry();

    PDFAppEditContentDropEntry(const PDFAppEditContentDropEntry&) = delete;
    PDFAppEditContentDropEntry& operator=(const PDFAppEditContentDropEntry&) = delete;

    virtual QString getId() const = 0;

    /// The data can be dropped. Runs at every move of the pointer: no access to the file system.
    virtual bool canDrop(const QMimeData* mimeData) const = 0;

    /// Takes the drop at the point of the default space of the page. Called by the view inside of
    /// its drop event: the entry reads what it needs from \p mimeData now (the data are gone after
    /// the call) and changes the model only by PDFAppEditContentContext::requestExecute (from the
    /// event loop). \returns the drop was taken.
    virtual bool drop(PDFAppEditContentContext* context, const QMimeData* mimeData, const QPointF& pagePoint) const = 0;
};

/// A tab of the left panel (zadani_gui.md 4.18): Objects and Pages (P12), Layers (P42),
/// Structure (P52).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentTabEntry
{
public:
    static constexpr const char* TAB_OBJECTS = "objects";
    static constexpr const char* TAB_PAGES = "pages";
    static constexpr const char* TAB_LAYERS = "layers";
    static constexpr const char* TAB_STRUCTURE = "structure";

    PDFAppEditContentTabEntry() = default;
    virtual ~PDFAppEditContentTabEntry();

    PDFAppEditContentTabEntry(const PDFAppEditContentTabEntry&) = delete;
    PDFAppEditContentTabEntry& operator=(const PDFAppEditContentTabEntry&) = delete;

    virtual QString getId() const = 0;

    /// Order of the tab buttons (Objects 100, Pages 200, Layers 300, Structure 400)
    virtual int getOrder() const = 0;

    /// Translated title of the tab button (its tooltip and accessible name) and its icon
    virtual QString getTitle() const = 0;
    virtual QString getIconName() const = 0;

    /// Creates the state of the tab, which lives as long as the tool (its item models): a child
    /// of \p parent (the context, which calls it once in its constructor). nullptr = the tab has
    /// no state (the default).
    virtual QObject* createState(PDFAppEditContentContext* context, QObject* parent) const;

    /// Creates the panel of the tab (new, a child of \p parent): a part, which the shell builds
    /// again after a change of the language - it keeps no state, reads everything from \p state
    /// (PDFAppEditContentContext::getTabState of this tab; nullptr without one) and from the
    /// context, both held by QPointer, connected by PDFDeferredDeletion::connect.
    virtual QWidget* createPanel(PDFAppEditContentContext* context, QObject* state, QWidget* parent) const = 0;
};

/// A card of the right panel (zadani_gui.md 4.19). The stack shows the cards of the entries,
/// which say isShown, in the order of getOrder.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCardEntry
{
public:
    PDFAppEditContentCardEntry() = default;
    virtual ~PDFAppEditContentCardEntry();

    PDFAppEditContentCardEntry(const PDFAppEditContentCardEntry&) = delete;
    PDFAppEditContentCardEntry& operator=(const PDFAppEditContentCardEntry&) = delete;

    /// Identifier (PDFAppEditContentCardIds)
    virtual QString getId() const = 0;

    /// Order from the top; the default is PDFAppEditContentCardIds::getOrder of the identifier
    virtual int getOrder() const;

    /// The card is shown for the state of now: the selection, the active tool, the preview, the
    /// requested cards (PDFAppEditContentContext). Cheap and without side effects: the stack asks
    /// at every change.
    virtual bool isShown(const PDFAppEditContentContext* context) const = 0;

    /// The card changes the model by commands (R7-8): the stack disables it while
    /// PDFAppEditContentContext::isEditingAllowed is false (Preview, a run, a read-only page). A
    /// card of the display, of the selection or of the state of the session outside of undo (the
    /// Selection, Preview, Annotations and fields, Locked object, Keyboard) says false and stays
    /// usable then. Default: true.
    virtual bool isChangingModel() const;

    /// Creates the card (new, a child of \p parent). The stack owns it and retires it
    /// (PDFDeferredDeletion::retireLater) when the entry says isShown no more.
    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const = 0;
};

/// A choice of an action with a submenu (Move to Layer > the layers; Tag as > the types)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentActionChoice
{
public:
    QString id;             ///< What triggerChoice gets
    QString title;          ///< Translated, or data (the name of a layer)
    QString iconName;
    bool isChecked = false;
    bool isEnabled = true;
};

/// An action of the tool: a row of a menu, a shortcut, or both (zadani_gui.md 4.20). The menus
/// (P12), the shortcuts of the key widget and the keys of the canvas (P14, P11) and the buttons of
/// the cards all start it by PDFAppEditContentContext::triggerAction, which calls trigger from
/// the event loop - never inside of QMenu::triggered, an item view signal or a key event.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentActionEntry
{
public:
    /// A key of the action
    class Key
    {
    public:
        QKeySequence sequence;
        PDFAppEditContentKeyScope scope = PDFAppEditContentKeyScope::Tool;
    };

    /// A place of the action in a menu
    class Placement
    {
    public:
        QString menuId;     ///< PDFAppEditContentMenuIds
        int group = 0;      ///< Groups are separated by dividers, ascending
        int order = 0;      ///< Inside of the group, ascending
    };

    PDFAppEditContentActionEntry() = default;
    virtual ~PDFAppEditContentActionEntry();

    PDFAppEditContentActionEntry(const PDFAppEditContentActionEntry&) = delete;
    PDFAppEditContentActionEntry& operator=(const PDFAppEditContentActionEntry&) = delete;

    /// Identifier ("order.bring-to-front", "view.rulers"); unique
    virtual QString getId() const = 0;

    /// Translated title for the state of now ("Hide" / "Show", "Lock" / "Unlock"); \p context may
    /// be nullptr (the list of the keys of a tool, which is not open: the general title)
    virtual QString getTitle(const PDFAppEditContentContext* context) const = 0;

    /// Icon of the menu row; empty = none (the default)
    virtual QString getIconName() const;

    /// Translated second line of the row of the action in a panel of the toolbar (the panel View,
    /// frame -menus-view: "Blue dashed outlines"); empty = none (the default). \p context may be
    /// nullptr, as for getTitle.
    virtual QString getDescription(const PDFAppEditContentContext* context) const;

    /// Keys of the action; none by default. A key must not be a key of the application
    /// (zadani_gui.md 4.20; the test of the key table refuses it).
    virtual std::vector<Key> getKeys() const;

    /// Places in the menus; none by default (a shortcut only). The menu TOOLBAR makes the action a
    /// button of the toolbar of the tool (PDFAppEditContentMenuIds::TOOLBAR).
    virtual std::vector<Placement> getPlacements() const;

    /// The commands of the application (PDFAppCommands: "edit.selectAll", "edit.copy",
    /// "search.find"), which this action answers while the canvas of the tool is shown
    /// (zadani_gui.md 4.20, way 2: the menu of the window, its shortcut, the status bar, MCP). The
    /// command is then the tool's: PDFAppEditContentContext::getApplicationCommandState gives the
    /// state of the command from isVisible / isEnabled of this entry, handleApplicationCommand
    /// runs trigger (queued, as triggerAction). A command answered by no entry (and not by the
    /// active tool) is not handled: the application acts as without the tool ("hidden, not
    /// disabled"). The rules (PDFAppEditContentKeyTable::checkApplicationCommands): a command is
    /// answered by one entry at most; never edit.undo, edit.redo, file.save, file.saveAs (the shell
    /// answers them); and the entry does not take a shortcut of a command it answers as its own key
    /// - a Window / Document shortcut (Ctrl+F) is delivered by the application, a Canvas one
    /// (Ctrl+C, Delete) never reaches the tool canvas and the view of the tool (P11) turns the key
    /// press into the same command (pe1_skeleton_notes.md 8.2). Default: none.
    virtual QStringList getApplicationCommandIds() const;

    /// The row is in the menu (false: an action, which does not apply to the selection at all).
    /// Default: true.
    virtual bool isVisible(const PDFAppEditContentContext* context) const;

    /// The action can run now. Default: PDFAppEditContentContext::isEditingAllowed.
    virtual bool isEnabled(const PDFAppEditContentContext* context) const;

    /// A check row (the rows of the menu View) and its state. Defaults: false.
    virtual bool isCheckable() const;
    virtual bool isChecked(const PDFAppEditContentContext* context) const;

    /// The action opens a submenu of these choices instead of running (empty = a plain action,
    /// the default)
    virtual std::vector<PDFAppEditContentActionChoice> getChoices(const PDFAppEditContentContext* context) const;

    /// Runs the action. Called from the event loop by PDFAppEditContentContext::triggerAction
    /// after isVisible and isEnabled were checked again. It changes the model only by commands
    /// (PDFAppEditContentContext::execute); it may open a dialog (open, never exec) or close the
    /// tool - the context may be retired when it returns.
    virtual void trigger(PDFAppEditContentContext* context) const = 0;

    /// Runs a choice of the submenu (the same rules); the default does nothing
    virtual void triggerChoice(PDFAppEditContentContext* context, const QString& choiceId) const;
};

/// The registry of the parts of Edit Content (see the head of this file)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentRegistry
{
public:
    /// An empty registry (a test fills its own one and gives it to its context)
    PDFAppEditContentRegistry();
    ~PDFAppEditContentRegistry();

    PDFAppEditContentRegistry(const PDFAppEditContentRegistry&) = delete;
    PDFAppEditContentRegistry& operator=(const PDFAppEditContentRegistry&) = delete;

    /// The registry of the application with the entries of every package
    /// (PDFAppEditContentBuiltins::registerAll), built at the first call. GUI thread.
    static const PDFAppEditContentRegistry& getInstance();

    // ---- registration: only inside of registerParts of a package file, or by a test on its own
    //      registry. An entry with an identifier, which is registered already, replaces the older
    //      one, when \p isReplacing is true (a later stage takes a card or an action over), and is
    //      refused with a warning otherwise. \returns the entry was added.

    bool addTool(std::unique_ptr<const PDFAppEditContentToolEntry> entry, bool isReplacing = false);
    bool addItem(std::unique_ptr<const PDFAppEditContentItemEntry> entry, bool isReplacing = false);
    bool addOverlay(std::unique_ptr<const PDFAppEditContentOverlayEntry> entry, bool isReplacing = false);
    bool addCanvasWidget(std::unique_ptr<const PDFAppEditContentCanvasWidgetEntry> entry, bool isReplacing = false);
    bool addDrop(std::unique_ptr<const PDFAppEditContentDropEntry> entry, bool isReplacing = false);
    bool addTab(std::unique_ptr<const PDFAppEditContentTabEntry> entry, bool isReplacing = false);
    bool addCard(std::unique_ptr<const PDFAppEditContentCardEntry> entry, bool isReplacing = false);
    bool addAction(std::unique_ptr<const PDFAppEditContentActionEntry> entry, bool isReplacing = false);

    // ---- reading (the pointers are valid as long as the registry) ----

    /// Tools sorted by their place and order / of one place
    std::vector<const PDFAppEditContentToolEntry*> getTools() const;
    std::vector<const PDFAppEditContentToolEntry*> getTools(PDFAppEditContentToolEntry::Place place) const;
    const PDFAppEditContentToolEntry* findTool(const QString& id) const;

    /// The tool of the canvas key, nullptr = none
    const PDFAppEditContentToolEntry* findToolByKey(const QKeySequence& key) const;

    /// The tool, which edits the node (PDFAppEditContentToolEntry::isEditorOf), nullptr = none
    const PDFAppEditContentToolEntry* findEditorTool(const PDFContentNode& node, const PDFContentContainer& container) const;

    /// The entry, which draws the node (by priority, then by registration), nullptr = none: the
    /// scene then draws nothing for the node and reports it once (a missing entry is a bug)
    const PDFAppEditContentItemEntry* findItem(const PDFContentNode& node, const PDFContentContainer& container) const;
    std::vector<const PDFAppEditContentItemEntry*> getItems() const;

    std::vector<const PDFAppEditContentOverlayEntry*> getOverlays() const;
    std::vector<const PDFAppEditContentCanvasWidgetEntry*> getCanvasWidgets() const;
    std::vector<const PDFAppEditContentDropEntry*> getDrops() const;

    /// Tabs sorted by their order
    std::vector<const PDFAppEditContentTabEntry*> getTabs() const;
    const PDFAppEditContentTabEntry* findTab(const QString& id) const;

    /// Cards sorted by their order
    std::vector<const PDFAppEditContentCardEntry*> getCards() const;
    const PDFAppEditContentCardEntry* findCard(const QString& id) const;

    /// Actions in the order of their registration / of a menu sorted by group and order
    std::vector<const PDFAppEditContentActionEntry*> getActions() const;
    std::vector<const PDFAppEditContentActionEntry*> getMenuActions(const QString& menuId) const;
    const PDFAppEditContentActionEntry* findAction(const QString& id) const;

    /// The action of the key in the scope, nullptr = none
    const PDFAppEditContentActionEntry* findActionByKey(const QKeySequence& key, PDFAppEditContentKeyScope scope) const;

    /// The action, which answers the command of the application
    /// (PDFAppEditContentActionEntry::getApplicationCommandIds; the first one registered, when a
    /// faulty registry has two), nullptr = none. Cheap (an index built at the registration): the
    /// shell asks for every command of the application at a change of its state.
    const PDFAppEditContentActionEntry* findActionByApplicationCommand(const QString& commandId) const;

    /// The placement of the action in the menu (group and order), no value = it is not there. A
    /// value, not a pointer (R8-12): getPlacements returns a temporary vector, a pointer into it
    /// dangled at three callers.
    static std::optional<PDFAppEditContentActionEntry::Placement> findPlacement(const std::vector<PDFAppEditContentActionEntry::Placement>& placements, const QString& menuId);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/// A row of the table of the keys of the tool
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentKeyRow
{
public:
    QKeySequence sequence;
    PDFAppEditContentKeyScope scope = PDFAppEditContentKeyScope::Tool;
    QString title;          ///< Translated title of the tool or of the action
    QString toolId;         ///< The tool of the key; empty for an action
    QString actionId;       ///< The action of the key; empty for a tool
};

/// The table of the keys of the tool (zadani_gui.md 4.20): the keys of the tool entries and of
/// the action entries of a registry. The card Keyboard (P14) shows it; the test of the skeleton
/// checks it against the commands of the application.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentKeyTable
{
public:
    PDFAppEditContentKeyTable() = delete;

    /// The rows of the registry: the tools in their order, then the actions in the order of
    /// their registration
    static std::vector<PDFAppEditContentKeyRow> getRows(const PDFAppEditContentRegistry& registry);

    /// Unmodified keys of the canvas, which the view and the active tool handle themselves and no
    /// entry may take (zadani_gui.md 4.20, way 1: P of Preview is an action; the arrows, Enter,
    /// Delete, Backspace, Tab belong to the tools)
    static std::vector<QKeySequence> getReservedCanvasKeys();

    /// Shortcuts, which the shell handles while the tool is open, and no entry may take: Esc,
    /// Ctrl+Return and Ctrl+Enter (PDFDocumentToolShell)
    static std::vector<QKeySequence> getShellKeys();

    /// The problems of the rows: an empty key, a key of more than one combination, a key used
    /// twice in one scope, a key of the scope Canvas with a modifier other than Shift or a
    /// reserved canvas key, a key of the scope Tool without Ctrl or Alt (except the function
    /// keys), a key of getShellKeys or of \p applicationShortcuts (the default shortcuts of the
    /// commands of the application, which act in the whole window: PDFAppCommandRegistry).
    /// Empty = the table is valid. The texts are for a test log (English, not translated).
    static QStringList check(const std::vector<PDFAppEditContentKeyRow>& rows, const std::vector<QKeySequence>& applicationShortcuts);

    /// The problems of the application commands answered by the action entries of the registry
    /// (PDFAppEditContentActionEntry::getApplicationCommandIds): a command, which is not in
    /// \p commandShortcuts (the shortcuts of every command of the application, of every scope:
    /// PDFAppCommandRegistry, the default and the alternate ones), a command answered by two entries,
    /// a command of the shell (edit.undo, edit.redo, file.save, file.saveAs), and a key of an entry
    /// (of either scope), which is a shortcut of a command the entry answers. Empty = valid. English
    /// texts for a test log.
    static QStringList checkApplicationCommands(const PDFAppEditContentRegistry& registry, const std::map<QString, std::vector<QKeySequence>>& commandShortcuts);

    /// The canvas keys of the registry (the keys of the tool entries and the keys of the scope
    /// Canvas of the action entries), which equal a shortcut of the scope Canvas of a command of
    /// the application, which an action entry of the registry answers (R6-6): on the canvas of
    /// the tool the key of the registry wins (the view asks the keys of the tool entries and the
    /// canvas keys of the registry before the Canvas-scope commands of the application), so the
    /// command is reachable there only by the menu, the status bar and MCP. Every such key is
    /// reported; a test accepts only the ones the plan decided (P = Preview over view.fitPage,
    /// zadani_gui.md 4.15 / 4.20). \p canvasCommandShortcuts are the shortcuts of the commands of
    /// the scope Canvas (default and alternate ones, PDFAppCommandRegistry); a command no entry
    /// answers is no conflict (its key never reaches the tool canvas). English texts for a test log.
    static QStringList checkCanvasKeys(const PDFAppEditContentRegistry& registry, const std::map<QString, std::vector<QKeySequence>>& canvasCommandShortcuts);

    /// The commands of the application, which the shell of the tool answers itself and no entry
    /// may answer: edit.undo, edit.redo (the own undo target of the tool), file.save, file.saveAs
    /// (the save question, PDFDocumentToolGUI::getSaveQuestion)
    static QStringList getShellCommandIds();
};

/// The entries of the application: one line per package file
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentBuiltins
{
public:
    PDFAppEditContentBuiltins() = delete;

    /// Adds the entries of every package file of the stages, which exist, in a fixed order.
    /// The only place, which names the registerParts functions (pdfappeditcontentregistry.cpp;
    /// the coordinator adds the line of a new file with its line of tools.cmake).
    static void registerAll(PDFAppEditContentRegistry* registry);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTREGISTRY_H
