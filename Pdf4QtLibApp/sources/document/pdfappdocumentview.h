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

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentSidebar;
class PDFAppDocumentTab;
class PDFAppDocumentToolbar;
class PDFAppInfoBar;
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
};

enum class PDFAppInfoBarTone
{
    Information,
    Warning,
    Error,
    Progress
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

    /// Page navigation (page index from 0; an index out of range is ignored)
    void goToPage(PDFInteger pageIndex);

    /// First page shown in the canvas (-1 without a document)
    PDFInteger getCurrentPageIndex() const;

    /// Number of the pages of the shown document (0 without a document)
    PDFInteger getPageCount() const;

    /// Label of the page (page labels of the document, the number of the page from
    /// 1 without them); an empty text for an index out of range
    QString getPageLabel(PDFInteger pageIndex) const;

    /// The document has page labels, which differ from the numbers of the pages
    bool hasPageLabels() const;

    /// Page index of a text typed by the user: a page label of the document, or a
    /// number of the page from 1. std::nullopt for an unknown label or a number out of range.
    std::optional<PDFInteger> findPageIndex(const QString& text) const;

    /// Pages selected in the thumbnails (package C sets them by setSelectedPages),
    /// otherwise the current page
    std::vector<PDFInteger> getSelectedPages() const;

    /// Pages selected in the thumbnails (package C); an empty list = no selection
    /// (getSelectedPages returns the current page). Pages out of range are dropped.
    void setSelectedPages(std::vector<PDFInteger> pages);

    /// Shows the panel of the sidebar (?panel=). \returns false for an unknown panel.
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

    /// The sidebar is shown (F4)
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
    /// The tool, which is active already, gives the place back to Select text (the
    /// default tool); ToolEnd = no tool.
    void setCanvasTool(int tool);

    /// The predefined tool (PDFToolManager::PredefinedTools) is the active tool
    bool isCanvasToolActive(int tool) const;

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

Q_SIGNALS:
    /// The current page has changed (page index from 0)
    void currentPageChanged(pdf::PDFInteger pageIndex);

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

    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    void onDocumentStateChanged();
    void onDocumentDestroyed();
    void onSignatureResultsChanged();
    void onThemeChanged();
    void onDrawSpaceChanged();
    void onTabCommandStatesChanged();
    void onCanvasActionTriggered();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onInfoBarAction(const QString& infoBarId, const QString& actionId);
    void onInfoBarClosed(const QString& infoBarId);
    void onToolMessage(const QString& text, int timeout);
    void onActiveToolChanged();
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
    void onSplitterMoved();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocumentToolbar> m_toolbar;
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
    std::map<QString, QPointer<QAction>> m_canvasActions;
    std::vector<PDFAppInfoBarRequest> m_infoBars;
    std::vector<QPointer<PDFAppInfoBar>> m_infoBarWidgets;
    std::vector<PDFInteger> m_selectedPages;
    PDFInteger m_lastPageIndex = -1;
    PDFAppViewLayout m_layout = PDFAppViewLayout::Continuous;
    bool m_isFirstPageRight = false;
    bool m_isFullScreen = false;
    bool m_isDefaultViewApplied = false;
    bool m_isSelectAllPending = false;
    bool m_isApplyingSettings = false;
    bool m_isSidebarVisible = true;
    bool m_isShowingAnnotations = true;
    QString m_colorMode;
    bool m_isPrefetchEnabled = true;
    int m_sidebarWidth = 300;
    AppliedSettings m_appliedSettings;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTVIEW_H
