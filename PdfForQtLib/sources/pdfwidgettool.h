//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFWIDGETTOOL_H
#define PDFWIDGETTOOL_H

#include "pdfdrawspacecontroller.h"
#include "pdftextlayout.h"
#include "pdfsnapper.h"

#include <QDialog>
#include <QCursor>

class QCheckBox;
class QLineEdit;

namespace pdf
{

/// Base class for various widget tools (for example, searching, text selection,
/// screenshots, zoom tool etc.). Each tool can have subtools (for example,
/// screenshot tool is picking screenshot rectangle).
class PDFFORQTLIBSHARED_EXPORT PDFWidgetTool : public QObject, public IDocumentDrawInterface
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

    void addTool(PDFWidgetTool* tool);
    void removeTool();

private:
    bool m_active;
    const PDFDocument* m_document;
    QAction* m_action;
    PDFDrawWidgetProxy* m_proxy;
    std::vector<PDFWidgetTool*> m_toolStack;
    std::optional<QCursor> m_cursor;
};

/// Simple tool for find text in PDF document. It is much simpler than advanced
/// search and can't search using regular expressions.
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

protected:
    virtual void setActiveImpl(bool active) override;
    virtual void updateActions() override;

private:
    void onSearchText();
    void onActionPrevious();
    void onActionNext();

    void performSearch();
    void updateResultsUI();
    void updateTitle();
    void clearResults();

    QAction* m_prevAction;
    QAction* m_nextAction;
    QWidget* m_parentDialog;

    QDialog* m_dialog;
    QCheckBox* m_caseSensitiveCheckBox;
    QCheckBox* m_wholeWordsCheckBox;
    QLineEdit* m_findTextEdit;
    QPushButton* m_previousButton;
    QPushButton* m_nextButton;

    pdf::PDFTextSelection getTextSelection() const { return m_textSelection.get(this, &PDFFindTextTool::getTextSelectionImpl); }
    pdf::PDFTextSelection getTextSelectionImpl() const;

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
class PDFSelectTextTool : public PDFWidgetTool
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
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

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
};

/// Tool to magnify specific area in the drawing widget
class PDFFORQTLIBSHARED_EXPORT PDFMagnifierTool : public PDFWidgetTool
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

protected:
    virtual void setActiveImpl(bool active) override;

private:
    QPoint m_mousePos;
    int m_magnifierSize;
    PDFReal m_magnifierZoom;
};

/// Tools for picking various items on page - points, rectangles, images etc.
class PDFFORQTLIBSHARED_EXPORT PDFPickTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:

    enum class Mode
    {
        Points,     ///< Pick points
        Rectangles, ///< Pick rectangles
        Images      ///< Pick images
    };

    /// Constructs new picking tool
    /// \param proxy Draw widget proxy
    /// \param mode Picking mode
    /// \param parent Parent object
    explicit PDFPickTool(PDFDrawWidgetProxy* proxy, Mode mode, QObject* parent);

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    QPointF getSnappedPoint() const;
    PDFInteger getPageIndex() const { return m_pageIndex; }
    const std::vector<QPointF>& getPickedPoints() const { return m_pickedPoints; }

    /// Sets custom snap points for given page. If points on page aren't currently picked,
    /// function does nothing. Snap data are not updated.
    /// \param pageIndex pageIndex
    /// \param snapPoints Custom snap points
    void setCustomSnapPoints(PDFInteger pageIndex, const std::vector<QPointF>& snapPoints);

    void resetTool();

signals:
    void pointPicked(PDFInteger pageIndex, QPointF pagePoint);
    void rectanglePicked(PDFInteger pageIndex, QRectF pageRectangle);
    void imagePicked(const QImage& image);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void buildSnapData();

    Mode m_mode;
    PDFSnapper m_snapper;
    QPoint m_mousePosition;
    PDFInteger m_pageIndex;
    std::vector<QPointF> m_pickedPoints;
};

/// Tool that makes screenshot of page area and copies it to the clipboard,
/// using current client area to determine image size.
class PDFFORQTLIBSHARED_EXPORT PDFScreenshotTool : public PDFWidgetTool
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
class PDFFORQTLIBSHARED_EXPORT PDFExtractImageTool : public PDFWidgetTool
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
class PDFFORQTLIBSHARED_EXPORT PDFToolManager : public QObject, public IDrawWidgetInputInterface
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
    explicit PDFToolManager(PDFDrawWidgetProxy* proxy, Actions actions, QObject* parent, QWidget* parentDialog);

    /// Sets document
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document);

    enum PredefinedTools
    {
        FindTextTool,
        SelectTextTool,
        MagnifierTool,
        ScreenshotTool,
        ExtractImageTool,
        ToolEnd
    };

    /// Sets active tool
    void setActiveTool(PDFWidgetTool* tool);

    /// Adds a new tool to tool manager
    void addTool(PDFWidgetTool* tool);

    /// Returns first active tool from tool set. If no tool is active,
    /// then nullptr is returned.
    PDFWidgetTool* getActiveTool() const;

    /// Returns find text tool
    PDFFindTextTool* getFindTextTool() const;

    /// Returns magnifier tool
    PDFMagnifierTool* getMagnifierTool() const;

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

signals:
    /// This is signal is set, when we want to display information message
    /// to the user, with timeout in miliseconds, after which message
    /// dissappears.
    /// \param text Text, which should be displayed to the user
    /// \param timeout Timeout in miliseconds
    void messageDisplayRequest(const QString& text, int timeout);

    /// This signal is emitted, when tool changes the document by some way
    /// \param documet Modified document
    void documentModified(PDFModifiedDocument document);

private:
    void onToolActionTriggered(bool checked);

    std::set<PDFWidgetTool*> m_tools;
    std::array<PDFWidgetTool*, ToolEnd> m_predefinedTools;
    std::map<QAction*, PDFWidgetTool*> m_actionsToTools;
};

}   // namespace pdf

#endif // PDFWIDGETTOOL_H
