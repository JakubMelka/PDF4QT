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

signals:
    void toolActivityChanged(bool active);

protected:
    virtual void setActiveImpl(bool active);

    PDFDrawWidgetProxy* getProxy() const { return m_proxy; }

private:
    bool m_active;
    const PDFDocument* m_document;
    PDFDrawWidgetProxy* m_proxy;
    std::vector<PDFWidgetTool*> m_toolStack;
};

/// Simple tool for find text in PDF document. It is much simpler than advanced
/// search and can't search using regular expressions.
class PDFFORQTLIBSHARED_EXPORT PDFFindTextTool : public PDFWidgetTool
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

private:
    void onSearchText();
    void onActionPrevious();
    void onActionNext();

    void performSearch();
    void updateActions();
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

/// Manager used for managing tools, their activity, availability
/// and other settings. It also defines a predefined set of tools,
/// available for various purposes (text searching, magnifier tool etc.)
class PDFFORQTLIBSHARED_EXPORT PDFToolManager : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Construct new text search tool
    /// \param proxy Draw widget proxy
    /// \param prevAction Action for navigating to previous result
    /// \param nextAction Action for navigating to next result
    /// \param parent Parent object
    /// \param parentDialog Paret dialog for tool dialog
    explicit PDFToolManager(PDFDrawWidgetProxy* proxy, QAction* findPreviousAction, QAction* findNextAction, QObject* parent, QWidget* parentDialog);

    /// Sets document
    /// \param document Document
    void setDocument(const PDFDocument* document);

    enum PredefinedTools
    {
        FindTextTool,
        ToolEnd
    };

    /// Returns first active tool from tool set. If no tool is active,
    /// then nullptr is returned.
    PDFWidgetTool* getActiveTool() const;

    /// Returns find text tool
    PDFFindTextTool* getFindTextTool() const;

private:
    std::set<PDFWidgetTool*> m_tools;
    std::array<PDFWidgetTool*, ToolEnd> m_predefinedTools;
};

}   // namespace pdf

#endif // PDFWIDGETTOOL_H
