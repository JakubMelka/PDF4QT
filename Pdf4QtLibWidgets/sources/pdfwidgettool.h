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

#ifndef PDFWIDGETTOOL_H
#define PDFWIDGETTOOL_H

#include "pdfwidgetsglobal.h"
#include "pdfdrawspacecontroller.h"
#include "pdftextlayout.h"
#include "pdfsnapper.h"
#include "pdfannotation.h"

#include <QDialog>
#include <QCursor>
#include <QPointer>

class QCheckBox;
class QLineEdit;

namespace pdf
{
class PDFAnnotationStyleManager;

/// Request of a tool to create an annotation, whose content the user gives
/// (PDFToolManager::setUserInterfaceDelegated): the tool has picked the place
/// on the page and the application asks the user for the rest (the text of a
/// sticky note, the text of a free text, the address of a hyperlink) and creates
/// the annotation itself.
struct PDF4QTLIBWIDGETSSHARED_EXPORT PDFAnnotationCreationRequest
{
    enum class Kind
    {
        StickyNote,     ///< Point, icon
        FreeText,       ///< Rectangle, free text style, automatic resize
        Hyperlink       ///< Rectangle, highlight mode
    };

    Kind kind = Kind::StickyNote;

    /// Page of the annotation (index, reference)
    PDFInteger pageIndex = -1;
    PDFObjectReference pageReference;

    /// Point of a sticky note (page coordinates)
    QPointF point;

    /// Rectangle of a free text or of a hyperlink (page coordinates, normalized)
    QRectF rectangle;

    /// Icon of a sticky note
    TextAnnotationIcon icon = TextAnnotationIcon::Comment;

    /// Style of a free text and its automatic resize to the text
    PDFFreeTextStyle freeTextStyle;
    bool autoResizeToContents = true;

    /// Highlight mode of a hyperlink
    LinkHighlightMode highlightMode = LinkHighlightMode::Outline;
};

/// Base class for various widget tools (for example, searching, text selection,
/// screenshots, zoom tool etc.). Each tool can have subtools (for example,
/// screenshot tool is picking screenshot rectangle).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidgetTool : public QObject, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFWidgetTool(PDFDrawWidgetProxy* proxy, QObject* parent);
    explicit PDFWidgetTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);
    virtual ~PDFWidgetTool();

    /// Sets document, shuts down the tool, if it is active, and document
    /// is changing.
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document);

    /// Sets tool as active or inactive. If tool is active, then it is processed
    /// in draw widget proxy events (such as drawing etc.).
    /// \param active Is tool active?
    void setActive(bool active);

    /// Returns true, if tool is active
    bool isActive() const { return m_active; }

    /// Returns action for activating/deactivating this tool
    QAction* getAction() const { return m_action; }

    /// Handles shortcut override event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);

    /// Handles key press event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event);

    /// Handles key release event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event);

    /// Handles mouse press event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse double click event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse release event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse move event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse wheel event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event);

    /// Returns actual cursor defined by the tool. Cursor can be undefined,
    /// in this case, optional will be set to nullopt.
    const std::optional<QCursor>& getCursor() const;

    /// Returns the manager of the style of the created annotations (stroke, fill,
    /// width), nullptr for a tool without a style. The application shows the style
    /// in its own tool bar and changes it by PDFAnnotationStyleManager::setStyle.
    virtual PDFAnnotationStyleManager* getStyleManager() const { return nullptr; }

signals:
    /// Informs about tool activity changed (if tool is enabled or disabled)
    /// \param active Is tool active?
    void toolActivityChanged(bool active);

    /// This is signal is set, when we want to display information message
    /// to the user, with timeout in miliseconds, after which message
    /// dissappears.
    /// \param text Text, which should be displayed to the user
    /// \param timeout Timeout in miliseconds
    void messageDisplayRequest(const QString& text, int timeout);

protected:
    virtual void setActiveImpl(bool active);
    virtual void updateActions();

    /// Returns currently active tool from toolstack,
    /// or nullptr, if no active tool from toolstack exists.
    PDFWidgetTool* getTopToolstackTool() const;

    const PDFDocument* getDocument() const { return m_document; }
    PDFDrawWidgetProxy* getProxy() const { return m_proxy; }

    inline void setCursor(QCursor cursor) { m_cursor = qMove(cursor); }
    inline void unsetCursor() { m_cursor = std::nullopt; }

    /// Draws a cross, which marks the given point of the widget. The cross is drawn in
    /// the difference composition mode, so it is visible on both light and dark page
    /// content, and on both light and dark theme of the user interface.
    /// \param painter Painter
    /// \param rect Rectangle of the widget
    /// \param point Point marked by the cross
    /// \param markSize Size of the arms of the cross; the cross is drawn over the whole
    ///        widget, when no size is given
    static void drawCross(QPainter* painter, QRect rect, QPoint point, std::optional<int> markSize);

    /// Draws the cross by a color of the application (its theme, PDFViewColors::pickCross),
    /// an invalid color draws the cross of the old applications (difference composition)
    static void drawCross(QPainter* painter, QRect rect, QPoint point, std::optional<int> markSize, const QColor& color);

    void addTool(PDFWidgetTool* tool);
    void removeTool();

private:
    bool m_active;
    const PDFDocument* m_document;
    QAction* m_action;
    PDFDrawWidgetProxy* m_proxy;
    QPointer<PDFDrawWidgetProxy> m_proxyGuard;  ///< Proxy of a tool destroyed while active (the proxy can be gone already)
    std::vector<PDFWidgetTool*> m_toolStack;
    std::optional<QCursor> m_cursor;
};

class PDFFindTextToolDialog : public QDialog
{
    Q_OBJECT
public:
    PDFFindTextToolDialog(PDFDrawWidgetProxy* proxy, QWidget* parent, Qt::WindowFlags f);

    virtual ~PDFFindTextToolDialog() override = default;
    virtual bool event(QEvent* event) override;

signals:
    void goToFirstResult();
    void goToLastResult();

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    PDFDrawWidgetProxy* m_proxy;
};

/// Simple tool for find text in PDF document. It is much simpler than advanced
/// search and can't search using regular expressions.
// TODO(remove-old-apps): the tool and its dialog (PDFFindTextToolDialog) go - the new application searches by its search panel (PDFDocumentTextSearch)
class PDFFindTextTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Construct new text search tool
    /// \param proxy Draw widget proxy
    /// \param prevAction Action for navigating to previous result
    /// \param nextAction Action for navigating to next result
    /// \param parent Parent object
    /// \param parentDialog Paret dialog for tool dialog
    explicit PDFFindTextTool(PDFDrawWidgetProxy* proxy, QAction* prevAction, QAction* nextAction, QObject* parent, QWidget* parentDialog);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

protected:
    virtual void setActiveImpl(bool active) override;
    virtual void updateActions() override;

private:
    void onSearchText();
    void onActionFirst();
    void onActionLast();
    void onActionPrevious();
    void onActionNext();
    void onDialogRejected();

    void setCurrentResultIndex(size_t index);
    void performSearch();
    void updateResultsUI();
    void updateTitle();
    void clearResults();
    void goToCurrentResult();

    QAction* m_prevAction;
    QAction* m_nextAction;
    QWidget* m_parentDialog;

    PDFFindTextToolDialog* m_dialog;
    QCheckBox* m_caseSensitiveCheckBox;
    QCheckBox* m_wholeWordsCheckBox;
    QLineEdit* m_findTextEdit;
    QPushButton* m_previousButton;
    QPushButton* m_nextButton;

    QString m_savedText;
    bool m_savedIsCaseSensitive = false;
    bool m_savedIsWholeWords = false;

    pdf::PDFTextSelection getTextSelection() const { return m_textSelection.get(this, &PDFFindTextTool::getTextSelectionImpl); }
    pdf::PDFTextSelection getTextSelectionImpl() const;
    pdf::PDFTextSelection getTextSelectionSelectedResultOnly() const;

    struct SearchParameters
    {
        QString phrase;
        bool isCaseSensitive = false;
        bool isWholeWordsOnly = false;
        bool isSearchFinished = false;
    };

    SearchParameters m_parameters;
    pdf::PDFFindResults m_findResults;
    size_t m_selectedResultIndex;
    mutable pdf::PDFCachedItem<pdf::PDFTextSelection> m_textSelection;
};

/// Tool for selection of text in document
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFSelectTextTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Construct new text selection tool
    /// \param proxy Draw widget proxy
    /// \param action Tool activation action
    /// \param copyTextAction Copy text action
    /// \param selectAllAction Select all text action
    /// \param deselectAction Deselect text action
    /// \param parent Parent object
    explicit PDFSelectTextTool(PDFDrawWidgetProxy* proxy, QAction* action, QAction* copyTextAction, QAction* selectAllAction, QAction* deselectAction, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    /// Returns text of the current text selection. If nothing is selected,
    /// or the document doesn't allow the content to be copied, then empty
    /// string is returned.
    /// \param pageIndices Pages, from which the selected text is taken. If the
    ///        list is empty, then text from all pages of the selection
    ///        is returned.
    QString getSelectedText(const std::vector<PDFInteger>& pageIndices = std::vector<PDFInteger>()) const;

    /// Copies the selected text to the clipboard (nothing, if the tool is not
    /// active, nothing is selected or the document does not allow copying)
    void copyText();

    /// Selects the whole text of the document (only an active tool)
    void selectAll();

    /// Clears the selection (only an active tool)
    void deselect();

    /// Some text is selected
    bool hasSelection() const { return !m_textSelection.isEmpty(); }

    /// Links stay clickable while the tool is active: a press of the left button
    /// without modifiers on a link (PDFWidgetAnnotationManager::getLinkActionAt) is
    /// not taken by the tool, the annotation manager gets it and follows the link
    /// at the release; a selection starts elsewhere. Off by default (the tools take
    /// every press before the annotations, as the old applications expect).
    // TODO(remove-old-apps): links are always clickable - the switch and the old behavior (the tool takes every press) go
    bool isLinksClickable() const { return m_isLinksClickable; }
    void setLinksClickable(bool linksClickable) { m_isLinksClickable = linksClickable; }

    /// Annotations stay selectable and editable while the tool is active: a press of
    /// the left button over a selectable annotation, a handle or a point of the
    /// selection, or with Ctrl / Shift on a page (rubber band) -
    /// PDFWidgetAnnotationManager::isInteractionAt - is not taken by the tool, the
    /// annotation manager gets it (the text selection is cleared); the cursor of the
    /// manager is shown there. Escape clears the text selection instead of turning the
    /// tool off (without a text selection it goes on to the annotation manager), and
    /// the annotation manager handles its keys while this tool is active
    /// (PDFWidgetAnnotationManager::isToolActive). A press on a field of a form
    /// (PDFWidgetFormManager::isEditorAt) goes to the form manager in this mode too.
    /// Off by default (the old applications select annotations only without an
    /// active tool).
    // TODO(remove-old-apps): annotations are always selectable with the text tool - the switch goes
    bool isAnnotationsSelectable() const { return m_isAnnotationsSelectable; }
    void setAnnotationsSelectable(bool annotationsSelectable);

    /// Returns the current text selection (empty, if nothing is selected). The
    /// application creates text markup annotations from it (one per page).
    const PDFTextSelection& getTextSelection() const { return m_textSelection; }

    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;

signals:
    /// The selection has changed (hasSelection, getSelectedText)
    void selectionChanged();

protected:
    virtual void setActiveImpl(bool active) override;
    virtual void updateActions() override;

private:
    void updateCursor();
    void onActionCopyText();
    void onActionSelectAll();
    void onActionDeselect();
    void setSelection(pdf::PDFTextSelection&& textSelection);

    struct SelectionInfo
    {
        PDFInteger pageIndex = -1;
        QPointF selectionStartPoint;
    };

    QAction* m_copyTextAction;
    QAction* m_selectAllAction;
    QAction* m_deselectAction;
    pdf::PDFTextSelection m_textSelection;
    SelectionInfo m_selectionInfo;
    bool m_isCursorOverText;
    bool m_isLinksClickable = false;
    bool m_isAnnotationsSelectable = false;
    bool m_isCursorOverAnnotation = false;

    /// Returns true, if a press at the position belongs to the annotation manager (setAnnotationsSelectable)
    bool isAnnotationInteractionAt(QPoint position, Qt::KeyboardModifiers modifiers) const;
};

/// Tool to magnify specific area in the drawing widget
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFMagnifierTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Constructs new magnifier tool
    /// \param proxy Draw widget proxy
    /// \param action Tool activation action
    /// \param parent Parent object
    explicit PDFMagnifierTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

    int getMagnifierSize() const;
    void setMagnifierSize(int magnifierSize);

    PDFReal getMagnifierZoom() const;
    void setMagnifierZoom(const PDFReal& magnifierZoom);

    /// Draws the shadow of a circle (a drop shadow of Figma without a spread)
    /// \param painter Painter
    /// \param center Center of the circle, the offset of the shadow applied [pixels of the widget]
    /// \param radius Radius of the circle
    /// \param color Color of the shadow, an invalid or a transparent color draws nothing
    /// \param blur Blur of the shadow (twice the standard deviation, as in Figma)
    static void drawCircleShadow(QPainter* painter, QPointF center, PDFReal radius, const QColor& color, PDFReal blur);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    /// Position of the mouse cursor. Empty value means, that the cursor is not
    /// over the draw widget, so the magnifier is not drawn at all. Do not use
    /// a null QPoint as the "no position" marker - QPoint(0, 0) is null, but it
    /// is a perfectly valid cursor position (the top left corner of the widget).
    std::optional<QPoint> m_mousePos;

    /// Magnifier draws the pages again, into the magnified area. That means, that
    /// the drawing of the pages is re-entered while it is running. This flag
    /// guards against the (unexpected) recursion - drawing the pages inside
    /// the magnifier must never trigger the magnifier again.
    mutable bool m_isDrawing;

    int m_magnifierSize;
    PDFReal m_magnifierZoom;
};

/// Tools for picking various items on page - points, rectangles, images etc.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPickTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:

    enum class Mode
    {
        Pages,      ///< Pick whole single page
        Points,     ///< Pick points
        Rectangles, ///< Pick rectangles
        Images      ///< Pick images
    };

    /// Constructs new picking tool
    /// \param proxy Draw widget proxy
    /// \param mode Picking mode
    /// \param parent Parent object
    explicit PDFPickTool(PDFDrawWidgetProxy* proxy, Mode mode, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;

    QPointF getSnappedPoint() const;
    PDFInteger getPageIndex() const { return m_pageIndex; }
    const std::vector<QPointF>& getPickedPoints() const { return m_pickedPoints; }

    /// Returns keyboard modifiers of the mouse event, which picked the last point.
    Qt::KeyboardModifiers getLastPickModifiers() const { return m_lastPickModifiers; }

    static QPointF getStaticOrthogonalPoint(const QPointF& referencePoint, const QPointF& originalPoint);
    QPointF getOrthogonalPoint(const QPointF& originalPoint) const;

    /// Sets custom snap points for given page. If points on page aren't currently picked,
    /// function does nothing. Snap data are not updated.
    /// \param pageIndex pageIndex
    /// \param snapPoints Custom snap points
    void setCustomSnapPoints(PDFInteger pageIndex, const std::vector<QPointF>& snapPoints);

    /// Enables snapping to annotation geometry.
    /// \param snapToAnnotations Snap to annotation geometry
    void setSnapToAnnotations(bool snapToAnnotations);

    void resetTool();

    /// Turns on/off drawing of selection rectangle, if rectangle picking
    /// mode is active.
    /// \param drawSelectionRectangle Draw selection rectangle?
    void setDrawSelectionRectangle(bool drawSelectionRectangle);

    QColor getSelectionRectangleColor() const;

    /// Sets color of the selection rectangle.
    /// \param selectionRectangleColor Color of the selection rectangle
    /// \param isAnnotationColor If true, then the color is a color of an annotation, which
    ///        will be created from the selection - it is then color adjusted only when the
    ///        color adjustment of annotations is turned on, so the preview and the created
    ///        annotation have the same color. If false, the color is a color of the page
    ///        content or just a selection marker, and it is adjusted always.
    void setSelectionRectangleColor(QColor selectionRectangleColor, bool isAnnotationColor = false);

    void makeLastPointOrthogonal();

signals:
    void pointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);
    void rectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void imagePicked(const QImage& image);
    void pagePicked(pdf::PDFInteger pageIndex);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void buildSnapData();

    /// Returns color adjusted color of the selection rectangle. See setSelectionRectangleColor.
    /// \param convertor Color convertor of the page content
    QColor getAdjustedSelectionRectangleColor(const PDFColorConvertor& convertor) const;

    Mode m_mode;
    PDFSnapper m_snapper;
    QPointF m_mousePosition;
    PDFInteger m_pageIndex;
    std::vector<QPointF> m_pickedPoints;
    Qt::KeyboardModifiers m_lastPickModifiers;
    bool m_drawSelectionRectangle;
    QColor m_selectionRectangleColor;
    bool m_isSelectionRectangleAnnotationColor;
    bool m_hideLargeCross;
    bool m_snapToAnnotations;
};

/// Tool for selection of table in document. Rows and columns
/// are automatically detected and are modifiable by the user.
class PDFSelectTableTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Construct new table selection tool
    /// \param proxy Draw widget proxy
    /// \param action Tool activation action
    /// \param parent Parent object
    explicit PDFSelectTableTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void setPickedRectangle(const QRectF& newPickedRectangle);
    void setPageIndex(PDFInteger newPageIndex);
    void setTextLayout(PDFTextLayout&& newTextLayout);
    void autodetectTableGeometry();

    bool isTablePicked() const;

    void onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool;
    PDFInteger m_pageIndex;
    QRectF m_pickedRectangle;
    PDFTextLayout m_textLayout;
    bool m_isTransposed;
    std::vector<PDFReal> m_horizontalBreaks;
    std::vector<PDFReal> m_verticalBreaks;
    PageRotation m_rotation;
};

/// Tool that makes screenshot of page area and copies it to the clipboard,
/// using current client area to determine image size.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFScreenshotTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Constructs new screenshot tool
    /// \param proxy Draw widget proxy
    /// \param action Tool activation action
    /// \param parent Parent object
    explicit PDFScreenshotTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

private:
    void onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle);

    PDFPickTool* m_pickTool;
};

/// Tool that extracts image from page and copies it to the clipboard,
/// using image original size (not zoomed size from widget area)
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFExtractImageTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    /// Constructs new extract image tool
    /// \param proxy Draw widget proxy
    /// \param action Tool activation action
    /// \param parent Parent object
    explicit PDFExtractImageTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

protected:
    virtual void updateActions() override;

private:
    void onImagePicked(const QImage& image);

    PDFPickTool* m_pickTool;
};

/// Manager used for managing tools, their activity, availability
/// and other settings. It also defines a predefined set of tools,
/// available for various purposes (text searching, magnifier tool etc.)
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFToolManager : public QObject, public IDrawWidgetInputInterface
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    struct Actions
    {
        QAction* findPrevAction = nullptr;  ///< Action for navigating to previous result
        QAction* findNextAction = nullptr;  ///< Action for navigating to next result
        QAction* selectTextToolAction = nullptr;
        QAction* selectTableToolAction = nullptr;
        QAction* selectAllAction = nullptr;
        QAction* deselectAction = nullptr;
        QAction* copyTextAction = nullptr;
        QAction* magnifierAction = nullptr;
        QAction* screenshotToolAction = nullptr;
        QAction* extractImageAction = nullptr;
    };

    /// Construct new text search tool
    /// \param proxy Draw widget proxy
    /// \param actions Actions
    /// \param parent Parent object
    /// \param parentDialog Paret dialog for tool dialog
    // TODO(remove-old-apps): the constructor with the actions, PDFToolManager::Actions and the find dialog (PDFFindTextTool) go - the new application drives the tools by its commands
    explicit PDFToolManager(PDFDrawWidgetProxy* proxy, Actions actions, QObject* parent, QWidget* parentDialog);

    /// Constructs the tool manager without actions and without the find dialog
    /// (the new application: the commands of the application drive the tools by
    /// setActiveTool and by the functions of the tools; getFindTextTool returns
    /// nullptr). The tools never touch an action, when they have none.
    /// \param proxy Draw widget proxy
    /// \param parent Parent object
    explicit PDFToolManager(PDFDrawWidgetProxy* proxy, QObject* parent);

    /// Sets document
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document);

    enum PredefinedTools
    {
        PickPageTool,
        PickRectangleTool,
        FindTextTool,
        SelectTextTool,
        SelectTableTool,
        MagnifierTool,
        ScreenshotTool,
        ExtractImageTool,
        ToolEnd
    };

    /// Sets active tool
    void setActiveTool(PDFWidgetTool* tool);

    /// Sets the predefined tool active (ToolEnd = no tool is active)
    void setActiveTool(PredefinedTools tool);

    /// Returns the predefined tool (nullptr for ToolEnd and for the find tool of
    /// the manager without actions)
    PDFWidgetTool* getPredefinedTool(PredefinedTools tool) const;

    /// Adds a new tool to tool manager
    void addTool(PDFWidgetTool* tool);

    /// Picks rectangle, if rectangle is successfully picked,
    /// then callback is called.
    /// \param callback Callback function
    void pickRectangle(std::function<void(PDFInteger, QRectF)> callback);

    /// Picks page, if page is successfully picked,
    /// then callback is called.
    /// \param callback Callback function
    void pickPage(std::function<void(PDFInteger)> callback);

    /// Returns first active tool from tool set. If no tool is active,
    /// then nullptr is returned.
    PDFWidgetTool* getActiveTool() const;

    /// Returns find text tool
    PDFFindTextTool* getFindTextTool() const;

    /// Returns magnifier tool
    PDFMagnifierTool* getMagnifierTool() const;

    /// Returns text selection tool
    PDFSelectTextTool* getSelectTextTool() const;

    /// Handles shortcut override event
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;

    /// Handles key press event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;

    /// Handles key release event from widget
    /// \param widget Widget
    /// \param event Event
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;

    /// Handles mouse press event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;

    /// Handles mouse double click event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;

    /// Handles mouse release event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;

    /// Handles mouse move event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    /// Handles mouse wheel event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;

    /// Returns actual cursor defined by the tool. Cursor can be undefined,
    /// in this case, optional will be set to nullopt.
    virtual const std::optional<QCursor>& getCursor() const override;

    virtual QString getTooltip() const override { return QString(); }
    virtual int getInputPriority() const override { return ToolPriority; }

    /// The tools ask the application for the content of a created annotation instead
    /// of showing their own dialogs: the sticky note, free text and hyperlink tools
    /// emit annotationCreationRequested with the picked place (the tool is deactivated
    /// then, as after a creation by its dialog, unless setCreationToolsPersistent) and the application creates the
    /// annotation. Off by default (the tools show QInputDialog / QDialog).
    // TODO(remove-old-apps): the delegation becomes the only behavior - the switch and the dialogs of the tools go
    void setUserInterfaceDelegated(bool delegated) { m_isUserInterfaceDelegated = delegated; }
    bool isUserInterfaceDelegated() const { return m_isUserInterfaceDelegated; }

    /// The creation tools (sticky note, free text, hyperlink, link to a page, line,
    /// polyline, polygon, rectangle, ellipse, freehand curve, stamp) stay active after
    /// they have created an annotation (or asked the application for it -
    /// setUserInterfaceDelegated): the user creates further annotations until he turns
    /// the tool off (Escape, another tool). The link to a page is active again after
    /// its target has been picked. Off by default - the tool is turned off after one
    /// annotation, as in the old applications.
    // TODO(remove-old-apps): the creation tools always stay active until Escape (tools.md 9.8) - the switch and the deactivation after one annotation go
    void setCreationToolsPersistent(bool persistent) { m_isCreationToolsPersistent = persistent; }
    bool isCreationToolsPersistent() const { return m_isCreationToolsPersistent; }

    /// The shape tools create the annotations, which the specification intends: the
    /// rectangle tool a Square annotation (instead of a closed Polygon) and the
    /// freehand curve tool an Ink annotation (instead of a PolyLine). Off by default -
    /// Polygon and PolyLine, as in the old applications.
    // TODO(remove-old-apps): Square and Ink become the only annotations of the tools - the switch and the Polygon / PolyLine path go
    void setSpecificationShapeTypes(bool specificationShapeTypes) { m_isSpecificationShapeTypes = specificationShapeTypes; }
    bool isSpecificationShapeTypes() const { return m_isSpecificationShapeTypes; }

signals:
    /// A tool asks the application to create an annotation (setUserInterfaceDelegated).
    /// It is emitted from the handling of a mouse event of the tool; the receiver must
    /// not open a modal dialog synchronously and must not delete the tool or the
    /// manager synchronously (queue it, deleteLater).
    /// \param request Request
    void annotationCreationRequested(const pdf::PDFAnnotationCreationRequest& request);

    /// This is signal is set, when we want to display information message
    /// to the user, with timeout in miliseconds, after which message
    /// dissappears.
    /// \param text Text, which should be displayed to the user
    /// \param timeout Timeout in miliseconds
    void messageDisplayRequest(const QString& text, int timeout);

    /// This signal is emitted, when tool changes the document by some way
    /// \param documet Modified document
    void documentModified(pdf::PDFModifiedDocument document);

    /// The active tool has changed (nullptr = no tool is active). It is emitted
    /// once, after the manager has finished the change (a switch from one tool
    /// to another one is one signal with the new tool), and only when the active
    /// tool differs from the last emitted one.
    void activeToolChanged(pdf::PDFWidgetTool* tool);

private:
    void createPredefinedTools(PDFDrawWidgetProxy* proxy, const Actions& actions, QWidget* parentDialog, bool createFindTool);
    void notifyActiveTool();
    void onToolActivityChanged(bool active);
    void onToolActionTriggered(bool checked);
    void onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle);
    void onPagePicked(PDFInteger pageIndex);

    /// Ends the picking of a rectangle / page (the pick tool is turned off)
    void finishPicking(PredefinedTools pickTool);

    /// A tool of the application has been destroyed (deleteLater from the slot of
    /// its request) - only its pointer is forgotten. Deactivate a tool, before it
    /// is destroyed.
    void onToolDestroyed(QObject* object);

    std::set<PDFWidgetTool*> m_tools;
    std::map<QObject*, PDFWidgetTool*> m_toolObjects;
    std::array<PDFWidgetTool*, ToolEnd> m_predefinedTools;
    std::map<QAction*, PDFWidgetTool*> m_actionsToTools;
    std::function<void(PDFInteger, QRectF)> m_pickRectangleCallback;
    std::function<void(PDFInteger)> m_pickPageCallback;

    /// Depth of the changes of the activity in progress (activeToolChanged is
    /// emitted by the outermost one) and the tool of the last emission
    int m_activityChangeNesting = 0;
    PDFWidgetTool* m_notifiedActiveTool = nullptr;
    bool m_isUserInterfaceDelegated = false;
    bool m_isCreationToolsPersistent = false;
    bool m_isSpecificationShapeTypes = false;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAnnotationCreationRequest)

#endif // PDFWIDGETTOOL_H
