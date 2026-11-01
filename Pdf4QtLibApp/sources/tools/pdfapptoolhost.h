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

#ifndef PDFAPPTOOLHOST_H
#define PDFAPPTOOLHOST_H

#include "pdfappdocumentview.h"
#include "pdfappviewstate.h"
#include "pdfdocumenttoolrunner.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>
#include <vector>

class QWidget;

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppDocumentView;
class PDFDrawWidgetProxy;
class PDFWidget;

/// The places of a window, in which the shell of a tool works (F17 of the plan of the
/// first batch, chapter 2.2): the toolbar, the panels, the canvas, the status message,
/// the commands of Undo and Redo. The shell knows only this interface, so a tool with
/// its own tab (Merge, a second implementation) needs no change of the shell. A host
/// never owns the shell; the shell holds the host as long as it lives. Every function
/// may be called while the place has gone already (the tab, the view): it then does
/// nothing and returns a null value. A signal may delete the shell, the host and the tab.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolHost : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppToolHost(QObject* parent);
    virtual ~PDFAppToolHost() override;

    /// Context of the application
    virtual PDFAppContext* getContext() const = 0;

    /// Document of the tool (the input document), nullptr = none
    virtual PDFAppDocument* getDocument() const = 0;

    /// Tab of the document (the result dialog, the panels), nullptr for a tool with its own tab
    virtual PDFAppDocumentTab* getDocumentTab() const = 0;

    /// View of the document (the panels of the toolbar, the reading of the pages), nullptr = none
    virtual PDFAppDocumentView* getView() const = 0;

    /// The canvas of the document (draw interfaces of an overlay), nullptr = none
    virtual PDFWidget* getPdfWidget() const = 0;

    /// The canvas area: the target of the scrim of the dialogs of the tool (312:7373),
    /// the parent of an overlay
    virtual QWidget* getCanvasArea() const = 0;

    /// The widget, on which the keys of the tool are actions (Esc, Ctrl+Enter)
    virtual QWidget* getKeyWidget() const = 0;

    /// Brings the tab of the tool to the front (a question about the tool, a route of the
    /// tool asked again from another tab). A slot may delete the shell.
    virtual void activate() = 0;

    /// Shows the parts of the tool. \returns false, when they are not shown: the mode
    /// is active already, no document is shown, or a slot has left the mode or deleted
    /// the view meanwhile. A slot may delete the shell.
    virtual bool enterMode(const PDFAppToolModeParts& parts) = 0;

    /// The parts go back to their former parents. A slot may delete the shell.
    virtual void leaveMode() = 0;

    /// The parts of the tool are shown
    virtual bool isModeActive() const = 0;

    /// Message of the tool in the status bar during the mode ("Compress · Report.pdf · 24.8 MB")
    virtual void setMessage(const QString& text) = 0;

    /// A message for a moment (a refusal), also after the mode has ended
    virtual void showTransientMessage(const QString& text) = 0;

    /// Shows the panel of the sidebar
    virtual void showPanel(const QString& panelId) = 0;

    /// Info bars over the canvas (a result, which has nothing to decide about)
    virtual void showInfoBar(const PDFAppInfoBarRequest& request) = 0;
    virtual void hideInfoBar(const QString& id) = 0;

    /// Stores the state of the view, which \p restoreViewState gives back (the layout,
    /// the zoom, the rotation, the sidebar, the page)
    virtual void saveViewState() = 0;
    virtual void restoreViewState() = 0;

    /// Commands of the tool in the toolbar: Undo, Redo, the sidebar (from the event loop)
    virtual bool isCommandEnabled(const QString& commandId) const = 0;
    virtual bool isCommandChecked(const QString& commandId) const = 0;
    virtual void requestCommand(const QString& commandId) = 0;

    /// Tooltip of a button of a command with the shortcut of the user
    virtual QString getCommandToolTip(const QString& commandId) const = 0;

    /// Inputs of a run besides the snapshot (selected pages, current page, color
    /// management, author), filled in the thread of the user interface
    virtual PDFAppToolRunEnvironment createEnvironment() const = 0;

    /// Current page and the pages selected in the thumbnails (indices from 0); an
    /// empty list = none selected
    virtual PDFInteger getCurrentPage() const = 0;
    virtual std::vector<PDFInteger> getSelectedPages() const = 0;

Q_SIGNALS:
    /// The parts of the tool have been taken away without \p leaveMode of the shell (the
    /// document is gone, the parts are gone): the tool ends
    void modeLeft();

    /// The lock of the document has changed
    void lockChanged(bool isLocked);

    /// The states of the commands may have changed
    void commandStatesChanged();

    /// The current page or the pages selected in the thumbnails have changed
    void pagesChanged();

    /// The document has left the state Ready (F2)
    void documentNotReady();
};

/// The document tab as the host of a tool: the tool mode of its persistent view
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentToolHost : public PDFAppToolHost
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolHost;

public:
    /// \param tab Tab of the document
    /// \param parent Parent object
    explicit PDFAppDocumentToolHost(PDFAppDocumentTab* tab, QObject* parent);
    virtual ~PDFAppDocumentToolHost() override;

    virtual PDFAppContext* getContext() const override;
    virtual PDFAppDocument* getDocument() const override;
    virtual PDFAppDocumentTab* getDocumentTab() const override;
    virtual PDFAppDocumentView* getView() const override;
    virtual PDFWidget* getPdfWidget() const override;
    virtual QWidget* getCanvasArea() const override;
    virtual QWidget* getKeyWidget() const override;
    virtual void activate() override;
    virtual bool enterMode(const PDFAppToolModeParts& parts) override;
    virtual void leaveMode() override;
    virtual bool isModeActive() const override;
    virtual void setMessage(const QString& text) override;
    virtual void showTransientMessage(const QString& text) override;
    virtual void showPanel(const QString& panelId) override;
    virtual void showInfoBar(const PDFAppInfoBarRequest& request) override;
    virtual void hideInfoBar(const QString& id) override;
    virtual void saveViewState() override;
    virtual void restoreViewState() override;
    virtual bool isCommandEnabled(const QString& commandId) const override;
    virtual bool isCommandChecked(const QString& commandId) const override;
    virtual void requestCommand(const QString& commandId) override;
    virtual QString getCommandToolTip(const QString& commandId) const override;
    virtual PDFAppToolRunEnvironment createEnvironment() const override;
    virtual PDFInteger getCurrentPage() const override;
    virtual std::vector<PDFInteger> getSelectedPages() const override;

    /// Longest time of a transient message (ms)
    static constexpr int MESSAGE_TIMEOUT = 8000;

private:
    void onToolModeChanged(bool isActive);
    void onDocumentStateChanged();

    QPointer<PDFAppDocumentTab> m_tab;
    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    std::optional<PDFAppViewState> m_savedViewState;
    PDFInteger m_savedPage = -1;
    bool m_isLeaving = false;   ///< The shell leaves the mode itself: no modeLeft
};

}   // namespace pdf

#endif // PDFAPPTOOLHOST_H
