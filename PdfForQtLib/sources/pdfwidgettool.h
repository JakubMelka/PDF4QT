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

#include <QDialog>
#include <QCursor>

class QCheckBox;

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

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix) const override;

    /// Sets document, shuts down the tool, if it is active, and document
    /// is changing.
    /// \param document Document
    void setDocument(const PDFDocument* document);

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
    const std::optional<QCursor>& getCursor() const { return m_cursor; }

signals:
    void toolActivityChanged(bool active);

protected:
    virtual void setActiveImpl(bool active);
    virtual void updateActions();

    const PDFDocument* getDocument() const { return m_document; }
    PDFDrawWidgetProxy* getProxy() const { return m_proxy; }

    inline void setCursor(QCursor cursor) { m_cursor = qMove(cursor); }
    inline void unsetCursor() { m_cursor = std::nullopt; }

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
                          const QMatrix& pagePointToDevicePointMatrix) const override;

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
                          const QMatrix& pagePointToDevicePointMatrix) const override;

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

/// Manager used for managing tools, their activity, availability
/// and other settings. It also defines a predefined set of tools,
/// available for various purposes (text searching, magnifier tool etc.)
class PDFFORQTLIBSHARED_EXPORT PDFToolManager : public QObject
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
    };

    /// Construct new text search tool
    /// \param proxy Draw widget proxy
    /// \param actions Actions
    /// \param parent Parent object
    /// \param parentDialog Paret dialog for tool dialog
    explicit PDFToolManager(PDFDrawWidgetProxy* proxy, Actions actions, QObject* parent, QWidget* parentDialog);

    /// Sets document
    /// \param document Document
    void setDocument(const PDFDocument* document);

    enum PredefinedTools
    {
        FindTextTool,
        SelectTextTool,
        MagnifierTool,
        ToolEnd
    };

    /// Sets active tool
    void setActiveTool(PDFWidgetTool* tool);

    /// Returns first active tool from tool set. If no tool is active,
    /// then nullptr is returned.
    PDFWidgetTool* getActiveTool() const;

    /// Returns find text tool
    PDFFindTextTool* getFindTextTool() const;

    /// Returns magnifier tool
    PDFMagnifierTool* getMagnifierTool() const;

    /// Handles key press event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    void keyPressEvent(QWidget* widget, QKeyEvent* event);

    /// Handles mouse press event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    void mousePressEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse release event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    void mouseReleaseEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse move event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    void mouseMoveEvent(QWidget* widget, QMouseEvent* event);

    /// Handles mouse wheel event from widget, over which tool operates
    /// \param widget Widget, over which tool operates
    /// \param event Event
    void wheelEvent(QWidget* widget, QWheelEvent* event);

    /// Returns actual cursor defined by the tool. Cursor can be undefined,
    /// in this case, optional will be set to nullopt.
    const std::optional<QCursor>& getCursor() const;

private:
    void onToolActionTriggered(bool checked);

    std::set<PDFWidgetTool*> m_tools;
    std::array<PDFWidgetTool*, ToolEnd> m_predefinedTools;
    std::map<QAction*, PDFWidgetTool*> m_actionsToTools;
};

}   // namespace pdf

#endif // PDFWIDGETTOOL_H
