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

#ifndef PDFAPPDOCUMENTVIEW_H
#define PDFAPPDOCUMENTVIEW_H

#include "pdfappglobal.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappviewstate.h"

#include "pdfglobal.h"

#include <QPointer>
#include <QSplitter>
#include <QSplitterHandle>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <map>
#include <vector>

class QAction;
class QHBoxLayout;
class QStackedLayout;
class QStackedWidget;

namespace pdf
{

class PDFAppAnnotationController;
class PDFAppCommentPopup;
class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentSidebar;
class PDFAppDocumentTab;
class PDFAppDocumentToolbar;
class PDFAppFormController;
class PDFAppInfoBar;
class PDFAppOutlineEditor;
class PDFAppToolUndoTarget;
class PDFCMSManager;
class PDFDrawWidgetProxy;
class PDFModifiedDocument;
class PDFProgress;
class PDFToolManager;
class PDFWidget;
class PDFWidgetAnnotationManager;
class PDFWidgetFormManager;
class PDFWidgetTool;

class PDF4QTLIBAPPSHARED_EXPORT PDFAppInfoBarAction
{
public:
    bool operator==(const PDFAppInfoBarAction&) const = default;

    QString id;         ///< Identifier given back by PDFAppDocumentView::infoBarActionTriggered
    QString text;       ///< Translated text of the button
    QString iconName;   ///< Icon 16 of the button before its text (empty = no icon; btn-stop 1030:25657 = "stop")
};

/// Handle of the splitter between the sidebar and the canvas (Figma 266:7854):
/// 6 px of bg/sidebar with the grip 266:7855 - three dots 2 x 2 icon/muted, 3 px
/// apart, in the middle. The line of the style is drawn over it, when the pointer
/// rests over the handle or it is dragged.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSplitterHandle : public QSplitterHandle
{
    Q_OBJECT

private:
    using BaseClass = QSplitterHandle;

public:
    /// Size of a dot of the grip and the gap between the dots
    static constexpr int GRIP_DOT = 2;
    static constexpr int GRIP_GAP = 3;

    explicit PDFAppDocumentSplitterHandle(Qt::Orientation orientation, QSplitter* parent);
    virtual ~PDFAppDocumentSplitterHandle() override;

    /// Rectangles of the dots of the grip in the coordinates of the handle
    std::vector<QRect> getGripDots() const;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Splitter of the document body (266:7854), its handle is PDFAppDocumentSplitterHandle
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSplitter : public QSplitter
{
    Q_OBJECT

private:
    using BaseClass = QSplitter;

public:
    explicit PDFAppDocumentSplitter(Qt::Orientation orientation, QWidget* parent);
    virtual ~PDFAppDocumentSplitter() override;

protected:
    virtual QSplitterHandle* createHandle() override;
    virtual void paintEvent(QPaintEvent* event) override;
};

enum class PDFAppInfoBarTone
{
    Information,
    Warning,
    Error,
    Progress,
    Success     ///< A finished action (Settings › Storage, exported 1271:15212: accent/green, check-circle)
};

/// Request of an info bar over the canvas (notification bar of the design,
/// 1028:24277 and 1030:25657): the icon in a circle of the tone, the title, the
/// optional second line, the progress line (tone Progress), the buttons and the
/// close button. A request with the id of a shown bar replaces it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInfoBarRequest
{
public:
    bool operator==(const PDFAppInfoBarRequest&) const = default;

    QString id;                                 ///< Identifier ("link-blocked", "printing", ...)
    PDFAppInfoBarTone tone = PDFAppInfoBarTone::Information;
    QString title;                              ///< First line (translated)
    QString text;                               ///< Second line (translated, may be empty)
    std::vector<PDFAppInfoBarAction> actions;   ///< Buttons in the order of the design
    bool isClosable = true;                     ///< The bar has a close button
    QString iconName;                           ///< Icon of the design (empty = the icon of the tone)
    int progress = -1;                          ///< Progress 0 - 100 of the tone Progress (-1 = no progress line)
};

/// Parts of a tool shown by the document view in its tool mode (phase 2, batch plan 2.3).
/// The pointers do not own anything; a null part leaves the part of the view in its
/// place. While the mode is active the widgets are children of the view, leaveToolMode
/// gives them back to their former parents, hidden.
///
/// Who owns them, for the shell of a tool: a view, which dies during the mode, deletes
/// the widgets with it (never the target), so the owner holds them in QPointer and never
/// in a unique_ptr. An owner dying first calls leaveToolMode and then deletes its parts;
/// a part deleted during the mode leaves the part of the view in its place, the mode ends
/// by itself when every part and the target are gone.
class PDFAppToolModeParts
{
public:
    QWidget* toolbar = nullptr;                 ///< Replaces the toolbar of the document (toolbar 291:1000)
    QWidget* leftPanel = nullptr;               ///< Replaces the sidebar (the panels of Rotate, Remove, ...); null = the sidebar stays (Compress 291:1030)
    QWidget* canvasPage = nullptr;              ///< Replaces the PDFWidget in the canvas area (a grid, a form); null = the PDFWidget stays
    QWidget* rightPanel = nullptr;              ///< Right panel next to the splitter (sidebar-right 291:1177), no handle before it
    PDFAppToolUndoTarget* undoTarget = nullptr; ///< What Undo and Redo act on during the mode; null = they are disabled
};

/// View of a document (step 3 plan 1.1, package B): the toolbar (52 px), the
/// sidebar with the splitter, the canvas (PDFWidget with the tool, annotation and
/// form managers, one PDFCMSManager per tab), the status bar (28 px) and the info
/// bars over the canvas. The view follows its document (PDFAppDocument) and the
/// settings of the application (rendering, color modes, color management,
/// magnifier, wheel), which it reads again at each change of the settings.
///
/// The toolbar, the panels and the canvas run the commands of the document tab
/// (executeCommand / requestCommand); the keys of the Canvas commands (Ctrl+C,
/// Ctrl+A, arrows, PgUp / PgDn, Home / End) are actions of the canvas with the
/// context Qt::WidgetWithChildrenShortcut, so they keep their meaning in fields.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppDocumentView(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppDocumentView() override;

    /// Gap between the bottom of the toolbar and the first info bar, and between the bars
    static constexpr int INFO_BAR_TOP_GAP = 12;
    static constexpr int INFO_BAR_GAP = 8;

    /// Canvas around the pages (page 193:1257 on the document canvas 193:1256):
    /// the margin around the pages and the drop shadow 0 / 2 / 10 of effect/shadow
    static constexpr int PAGE_MARGIN = 24;
    static constexpr int PAGE_SHADOW_OFFSET_Y = 2;
    static constexpr int PAGE_SHADOW_BLUR = 10;

    PDFAppContext* getContext() const;

    /// Document shown by the view (nullptr = none); the view follows its changes
    void setDocument(PDFAppDocument* document);
    PDFAppDocument* getDocument() const;

    /// Tab of the view (nullptr, when the view is not inside of a document tab)
    PDFAppDocumentTab* getTab() const;

    /// Parts of the view
    PDFAppDocumentToolbar* getToolbar() const;
    PDFAppDocumentSidebar* getSidebar() const;
    PDFAppDocumentStatusBar* getStatusBar() const;
    QSplitter* getSplitter() const;

    /// The canvas: the widget, which holds the actions of the Canvas commands
    /// (Qt::WidgetWithChildrenShortcut) - the PDFWidget
    PDFWidget* getPdfWidget() const { return m_pdfWidget; }
    PDFDrawWidgetProxy* getProxy() const;
    PDFToolManager* getToolManager() const { return m_toolManager; }
    PDFWidgetAnnotationManager* getAnnotationManager() const { return m_annotationManager; }
    PDFWidgetFormManager* getFormManager() const { return m_formManager; }
    PDFCMSManager* getCMSManager() const { return m_cmsManager; }
    PDFProgress* getProgress() const { return m_progress; }

    /// Controller of the annotations (step 4): the commits of the engine, the
    /// delegated user interface, the lock (nullptr only while the view is destroyed)
    PDFAppAnnotationController* getAnnotationController() const { return m_annotationController; }

    /// Form filling of the view (step 4, package D)
    PDFAppFormController* getFormController() const { return m_formController; }

    /// Editing of the outline (step 4, package C; package A uses it for the
    /// Insert › Outline item tile)
    PDFAppOutlineEditor* getOutlineEditor() const { return m_outlineEditor; }

    /// Comment popup of the view (step 4, package B; package A uses its compose
    /// mode), created at the first call as a child of the canvas area
    PDFAppCommentPopup* getCommentPopup();

    /// Area of the canvas (parent of the floating bars and of the info bars)
    QWidget* getCanvasArea() const { return m_canvasArea.data(); }

    /// Page navigation (page index from 0; an index out of range is ignored)
    void goToPage(PDFInteger pageIndex);

    /// First page shown in the canvas (-1 without a document); with two pages side by
    /// side the left page of the first row shown
    PDFInteger getCurrentPageIndex() const;

    /// Pages seen in the canvas, sorted (all pages of the rows shown, also partly);
    /// empty without a document
    std::vector<PDFInteger> getShownPages() const;

    /// The page is seen in the canvas (one of getShownPages)
    bool isPageShown(PDFInteger pageIndex) const;

    /// The page of the document for the user (owner 2026-10-29): the page chosen in the
    /// thumbnails (setSelectedPages) while it is seen in the canvas - the right page of
    /// a row of two pages, which the user has clicked - otherwise the current page. The page field of the toolbar, the page item of the status bar, the Go to
    /// panel, the route ?page=, the reading state, Add bookmark, Print Current page, the
    /// state of MCP and the starts of Copy to Pages and of the speech show it; the
    /// navigation and the synchronization of the panels use getCurrentPageIndex.
    /// -1 without a document.
    PDFInteger getDisplayedPageIndex() const;

    /// Number of the pages of the shown document (0 without a document)
    PDFInteger getPageCount() const;

    /// Label of the page (page labels of the document, the number of the page from
    /// 1 without them); an empty text for an index out of range
    QString getPageLabel(PDFInteger pageIndex) const;

    /// The document has page labels, which differ from the numbers of the pages
    bool hasPageLabels() const;

    /// Radius of the circle of the magnifier (PDFMagnifierTool) of the setting
    /// magnifier.size, which is its diameter (1234:164735); at least 1
    static int getMagnifierRadius(int diameter);

    /// Page index of a text typed by the user: a page label of the document, or a
    /// number of the page from 1. std::nullopt for an unknown label or a number out of range.
    std::optional<PDFInteger> findPageIndex(const QString& text) const;

    /// Pages selected in the thumbnails (package C sets them by setSelectedPages),
    /// otherwise the current page
    std::vector<PDFInteger> getSelectedPages() const;

    /// Pages selected in the thumbnails (package C); an empty list = no selection
    /// (getSelectedPages returns the current page). Pages out of range are dropped.
    /// \p chosenPage is the one page the user has chosen in the thumbnails (a click, the
    /// keys, the right button; -1 for none): getDisplayedPageIndex while it is seen.
    void setSelectedPages(std::vector<PDFInteger> pages, PDFInteger chosenPage = -1);

    /// Shows the panel of the sidebar (?panel=). \returns false for an unknown panel and
    /// while the left panel of a tool lies in the place of the sidebar (the panel stays).
    bool showPanel(const QString& panelId);

    /// Id of the shown panel (empty = none)
    QString getCurrentPanelId() const;

    /// Adds an item of a package to the status bar (the status bar owns it)
    void addStatusItem(QWidget* item, PDFAppDocumentStatusBar::Area area);

    /// Shows / hides an info bar over the canvas
    void showInfoBar(const PDFAppInfoBarRequest& request);
    void hideInfoBar(const QString& id);

    /// Ids of the shown info bars (in the order they were shown)
    QStringList getInfoBarIds() const;

    /// Request of the shown info bar (nullptr, if there is none)
    const PDFAppInfoBarRequest* findInfoBar(const QString& id) const;

    /// Widget of the shown info bar (nullptr, if there is none)
    PDFAppInfoBar* findInfoBarWidget(const QString& id) const;

    /// Sets the colors of the canvas from the theme (also called at a theme change)
    void updateViewColors();

    // ------------------------------------------------------------- commands

    /// Runs the command of the document tab (or of the window: Close tab, New tab).
    /// The command may delete the tab and its view. \returns false, if the command
    /// is unknown or disabled.
    bool executeCommand(const QString& commandId);

    /// Runs the command from the event loop (the buttons of the toolbar and of the
    /// panels: the command may delete the tab together with the button)
    void requestCommand(const QString& commandId);

    /// State of the command (the tab asks the handler of its package)
    bool isCommandEnabled(const QString& commandId) const;
    bool isCommandChecked(const QString& commandId) const;

    /// Action of a Canvas command on the canvas (nullptr for another command)
    QAction* getCanvasAction(const QString& commandId) const;

    /// Tooltip of a button of a command: its title and the shortcut of the user
    /// ("Fit page (P)"); without a shortcut the title, or nothing when
    /// \p isEmptyWithoutShortcut (a tile shows its title itself). The shortcuts of
    /// the user are kept by the view (read again at a notification of the database,
    /// an unreadable database leaves the defaults) - shared by the toolbar and its panels.
    QString getCommandToolTip(const QString& commandId, bool isEmptyWithoutShortcut) const;

    // ------------------------------------------------------------- view

    /// Page layout of the view
    PDFAppViewLayout getViewLayout() const;
    void setViewLayout(PDFAppViewLayout layout);

    /// The first page lies on the right side (two pages, two columns)
    bool isFirstPageRight() const { return m_isFirstPageRight; }
    void setFirstPageRight(bool isFirstPageRight);

    /// Zoom factor (1.0 = 100 %) and the zoom of the user; the factor is bounded to 8 % - 6400 %
    double getZoom() const;
    void setZoom(double zoom);

    /// Rotation of the view in degrees (0, 90, 180, 270)
    int getRotation() const;

    /// The sidebar is shown (F4); the left panel of a tool lies in its place during the mode
    /// and F4 acts on it (the button of the toolbar of a tool, 389:20184), the sidebar of the
    /// user comes back as it was (getViewState says that state, never the state of the panel)
    bool isSidebarVisible() const;
    void setSidebarVisible(bool isVisible);

    /// Full screen (F11, Esc leaves it): the top bar of the window, the toolbar,
    /// the sidebar and the status bar are hidden
    bool isFullScreen() const { return m_isFullScreen; }
    void setFullScreen(bool isFullScreen);

    /// Tool of the canvas: the selection tools of the Select menu and the magnifier
    /// (nullptr = no tool, the canvas scrolls by dragging)
    PDFWidgetTool* getActiveTool() const;

    /// Activates the predefined tool (PDFToolManager::PredefinedTools) of the canvas.
    /// Select text and Hand (ToolEnd = no tool: a left drag pans the pages, links,
    /// fields and annotations still take their clicks) are the modes of the canvas, the
    /// other tools work above the chosen mode: the tool, which is active already, and
    /// Esc of the tool give the place back to the mode (Select menu 198:5517).
    void setCanvasTool(int tool);

    /// The predefined tool (PDFToolManager::PredefinedTools) is the active tool;
    /// ToolEnd = the Hand mode without another tool above it
    bool isCanvasToolActive(int tool) const;

    /// The Hand mode is the chosen mode of the canvas (the default mode of a new tab,
    /// as in the old applications - owner 2026-10-25; the mode is not stored)
    bool isHandMode() const { return m_isHandMode; }

    /// Activates the chosen mode of the canvas (Select text or Hand) - the end of
    /// a tool of the Select menu, of a creation tool, of a pick of a target
    void activateDefaultCanvasTool();

    /// Selection of the text (Select text): the selection exists, copy it to the
    /// clipboard, select the whole text (it waits for the text layout of the pages,
    /// made in a worker thread), remove the selection
    bool hasTextSelection() const;
    void copySelectedText();
    void selectAllText();
    void deselectText();

    /// Stores the setting of the view (Show annotations, color mode) and applies it
    void setViewSetting(const QString& key, const QVariant& value);

    /// Applied settings of the view: the color mode (value of viewing.colorMode)
    /// and the display of the annotations
    const QString& getColorMode() const { return m_colorMode; }
    bool isShowingAnnotations() const { return m_isShowingAnnotations; }

    /// Opens the Go to panel with the field of the page focused (Ctrl+G)
    void openGoToPage();

    /// Opens the section of Settings (empty = the first section) in the Home tab,
    /// from the event loop (Settings…, Rendering settings)
    void openSettings(const QString& section);

    /// State of the view (Recent - package A stores it) and its application
    PDFAppViewState getViewState() const;
    void applyViewState(const PDFAppViewState& state);

    /// Reads the settings of the application and applies them to the canvas
    /// (called at each change of the settings)
    void applySettings();

    /// Applies the settings once from the event loop (the notifications of the
    /// changes of the settings are coalesced into one application)
    void scheduleApplySettings();

    /// An application of the settings is queued (tests)
    bool isApplySettingsQueued() const { return m_isApplySettingsQueued; }

    // ------------------------------------------------------------------ tool mode

    /// Width of the right panel of a tool (sidebar-right 291:1177)
    static constexpr int TOOL_RIGHT_PANEL_WIDTH = 320;

    /// Enters the tool mode (owner decision of 2026-11-01, batch plan 2.3 and F4): the
    /// full screen ends, the pending edits are committed (a form field, an inline title of
    /// the outline), an open comment popup and the panels of the toolbar close, a creation
    /// tool ends and the changes of the user are suspended (PDFAppDocument::setUserChangesSuspended:
    /// the annotation controller, the form and the outline editor are read only). Undo and
    /// Redo go to \p parts.undoTarget. Reading - zoom, navigation, search, selection, Copy,
    /// the panels for reading - still works. The keyboard focus outside of the shown parts of
    /// the view goes to the canvas (the page of the tool or the PDFWidget), so the keys bound
    /// to the view reach the mode: the shell may focus its first field afterwards. A slot may
    /// delete the view.
    /// \returns false, when the mode is active already or no document is shown (the parts
    /// stay untouched), or when a slot has left the mode or has deleted the view meanwhile
    /// (the parts have gone with the view).
    bool enterToolMode(const PDFAppToolModeParts& parts);

    /// Leaves the tool mode: the parts go back to their parents and everything the mode has
    /// changed returns to its former state (a creation tool ended by the entry and a
    /// committed editor stay ended). Nothing for a view out of the mode. A slot may delete the view.
    void leaveToolMode();

    bool isToolModeActive() const { return m_isToolModeActive; }

    /// Target of Undo and Redo of the mode (nullptr out of the mode and for a deleted target)
    PDFAppToolUndoTarget* getToolUndoTarget() const;

    /// A part of the tool is shown in the place of the toolbar / of the sidebar. The commands
    /// with their user interface in that place are disabled (Go to, Find without the field).
    bool isToolToolbarShown() const { return !m_toolToolbar.widget.isNull(); }
    bool isToolLeftPanelShown() const { return !m_toolLeftPanel.widget.isNull(); }
    bool isToolCanvasPageShown() const { return !m_toolCanvasPage.widget.isNull(); }

    /// Message of the tool in the status bar during the mode ("Compress · Report.pdf · 24.8 MB",
    /// plain text, bounded); ignored out of the mode, the mode shows "Ready" again at its end
    void setToolMessage(const QString& text);
    QString getToolMessage() const { return m_toolMessage; }

Q_SIGNALS:
    /// The tool mode has been entered or left (also by itself: the document is gone, every
    /// part and the target of the mode are gone)
    void toolModeChanged(bool isActive);

    /// The lock of the document has changed during the mode (a tool, which cannot work on a
    /// locked document, closes; PDFAppDocument::commit refuses a locked document anyway)
    void toolModeLockChanged(bool isLocked);

    /// The result of canUndo or canRedo of the target may have changed (also the target is gone)
    void toolUndoStateChanged();

    /// The current page has changed (page index from 0)
    void currentPageChanged(pdf::PDFInteger pageIndex);

    /// The page for the user has changed (getDisplayedPageIndex): the current page, or
    /// the selected page of the thumbnails seen in the canvas
    void displayedPageChanged(pdf::PDFInteger pageIndex);

    /// The shown panel of the sidebar has changed
    void panelChanged(const QString& panelId);

    /// A button of an info bar has been clicked (the bar stays; its owner hides it).
    /// The close button of a bar hides it and is reported with an empty action id.
    void infoBarActionTriggered(const QString& infoBarId, const QString& actionId);

    /// The zoom, the layout, the rotation, the sidebar or the full screen has changed
    void viewChanged();

    /// The enabled or the checked state of the commands may have changed (the tab
    /// has reported it - the toolbar and the panels read the states again)
    void commandStatesChanged();

    /// A state shown by the commands of the view has changed (layout, sidebar, full
    /// screen, tool, selection, settings); the command handler of the view reports it
    void viewStateChanged();

    /// The selected pages have changed (setSelectedPages)
    void selectedPagesChanged();

protected:
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// Values of the settings applied to the canvas (a change is applied again only)
    struct AppliedSettings
    {
        bool operator==(const AppliedSettings&) const = default;

        int rendererEngine = -1;
        qint64 compiledPageCacheKB = -1;
        qint64 thumbnailCacheKB = -1;
        qint64 fontCacheLimit = -1;
        qint64 instancedFontCacheLimit = -1;
        double meshPreferredRatio = -1.0;
        double meshMinimalRatio = -1.0;
        double colorTolerance = -1.0;
        bool smoothWheelScrolling = false;
        qint64 wheelSpeedHorizontal = -1;
        qint64 wheelSpeedVertical = -1;
    };

    /// The page the user looks at and the top of its rectangle on the canvas: it is
    /// kept in its place over a new version of the document, whose layout changes
    /// (a rotated page above it, undo / redo of it)
    struct PageAnchor
    {
        PDFInteger pageIndex = -1;
        double top = 0.0;
        double height = 0.0;
    };

    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    PDFInteger getDisplayedPageIndex(const std::vector<PDFInteger>& shownPages) const;
    /// Announces a change of the page for the user (the page item of the status bar,
    /// displayedPageChanged); a slot may delete the view
    void updateDisplayedPage();
    PageAnchor capturePageAnchor(const PDFModifiedDocument& modified) const;
    void restorePageAnchor(const PageAnchor& anchor);
    void onDocumentStateChanged();
    void onDocumentAboutToBeDestroyed();
    void onDocumentDestroyed();
    void onSignatureResultsChanged();
    void onThemeChanged();
    void onDrawSpaceChanged();
    void onTabCommandStatesChanged();
    void onCanvasActionTriggered();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onApplySettingsQueued();
    void onInfoBarAction(const QString& infoBarId, const QString& actionId);
    void onInfoBarClosed(const QString& infoBarId);
    void onToolMessage(const QString& text, int timeout);
    void onActiveToolChanged(PDFWidgetTool* tool);
    void onDefaultCanvasToolQueued();
    /// Emits viewChanged and viewStateChanged (the second one only, when a slot of
    /// the first one has not deleted the view)
    void emitViewStateChanged();
    void onTextLayoutChanged();
    void onExecuteQueued(const QString& commandId);
    void onOpenSettingsQueued(const QString& section);
    void applyDocument();
    void applyDefaultView();
    void applyPageLayout();
    void createCanvasActions();
    void updateCanvasShortcuts();
    void updateStatusMessage();
    void layoutInfoBars();
    void connectTab();
    void leaveFullScreen();
    void updateSidebarVisibility();
    void applySplitterSizes();
    void onSplitterMoved();

    /// A part of the tool mode with the parent, which it goes back to
    struct ToolPart
    {
        QPointer<QWidget> widget;
        QPointer<QWidget> parent;
    };

    void installToolParts(const PDFAppToolModeParts& parts);
    void removeToolParts();
    void watchToolPart(ToolPart& part, QWidget* widget);
    void releaseToolPart(ToolPart& part);
    bool isToolPartAlive() const;
    void updatePageItem();
    void updateZoomItem();
    void onDocumentLockChanged();
    void onToolPartDestroyed();
    void onToolPartsChangedQueued();
    void onToolUndoStateChanged();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocumentToolbar> m_toolbar;
    QPointer<QStackedWidget> m_toolbarStack;    ///< The toolbar of the document or the toolbar of a tool
    QPointer<QHBoxLayout> m_bodyLayout;         ///< The splitter and the right panel of a tool
    QPointer<QStackedLayout> m_canvasLayout;    ///< Layout of the canvas area: the PDFWidget or the canvas page of a tool
    QPointer<QSplitter> m_splitter;
    QPointer<PDFAppDocumentSidebar> m_sidebar;
    QPointer<QWidget> m_canvasArea;
    QPointer<PDFAppDocumentStatusBar> m_statusBar;
    QPointer<QAction> m_leaveFullScreenAction;
    PDFCMSManager* m_cmsManager = nullptr;
    PDFProgress* m_progress = nullptr;
    PDFWidget* m_pdfWidget = nullptr;
    PDFWidgetAnnotationManager* m_annotationManager = nullptr;
    PDFWidgetFormManager* m_formManager = nullptr;
    PDFToolManager* m_toolManager = nullptr;
    PDFAppAnnotationController* m_annotationController = nullptr;
    PDFAppFormController* m_formController = nullptr;
    PDFAppOutlineEditor* m_outlineEditor = nullptr;
    QPointer<PDFAppCommentPopup> m_commentPopup;
    std::map<QString, QPointer<QAction>> m_canvasActions;
    std::map<QString, QString> m_userShortcuts;   ///< Shortcuts of the user (PDFAppCommandRegistry::UserShortcuts, updateCanvasShortcuts)
    std::vector<PDFAppInfoBarRequest> m_infoBars;
    std::vector<QPointer<PDFAppInfoBar>> m_infoBarWidgets;
    std::vector<PDFInteger> m_selectedPages;
    PDFInteger m_chosenPageIndex = -1;          ///< The page chosen by the user in the thumbnails (setSelectedPages)
    PDFInteger m_lastPageIndex = -1;
    PDFInteger m_lastDisplayedPageIndex = -1;   ///< The page announced by displayedPageChanged (-2 = announce again)
    PDFAppViewLayout m_layout = PDFAppViewLayout::Continuous;
    bool m_isFirstPageRight = false;
    bool m_isFullScreen = false;
    bool m_isDefaultViewApplied = false;
    bool m_isRestoringPageAnchor = false;     ///< The layout changes under the anchored page (no signals of the current page)
    bool m_isHandMode = true;
    bool m_isSelectAllPending = false;
    bool m_isApplyingSettings = false;
    bool m_isApplySettingsQueued = false;
    bool m_isSidebarVisible = true;
    bool m_isShowingAnnotations = true;
    QString m_colorMode;
    bool m_isPrefetchEnabled = true;
    int m_sidebarWidth = 300;
    AppliedSettings m_appliedSettings;

    // Tool mode
    ToolPart m_toolToolbar;
    ToolPart m_toolLeftPanel;
    ToolPart m_toolCanvasPage;
    ToolPart m_toolRightPanel;
    QPointer<PDFAppToolUndoTarget> m_toolUndoTarget;
    QString m_toolMessage;
    int m_toolLeftWidth = 300;                  ///< Width of the left panel of the tool (sidebar-left 291:1030)
    bool m_isToolModeActive = false;
    bool m_hasToolParts = false;                ///< The mode was entered with a part or a target (gone all = the mode ends)
    bool m_isToolPartsUpdateQueued = false;
    bool m_hadToolLeftPanel = false;            ///< The left panel of the tool has replaced the sidebar (not given back yet)
    bool m_wasSidebarVisible = true;            ///< m_isSidebarVisible before the left panel of the tool
    bool m_wasFullScreen = false;               ///< The full screen ended by the entry into the mode
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTVIEW_H
