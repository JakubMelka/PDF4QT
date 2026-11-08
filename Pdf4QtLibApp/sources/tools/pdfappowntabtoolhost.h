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

#ifndef PDFAPPOWNTABTOOLHOST_H
#define PDFAPPOWNTABTOOLHOST_H

#include "pdfappglobal.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolmodelayout.h"

#include <QPointer>
#include <QString>
#include <QWidget>

namespace pdf
{

class PDFAppDocumentStatusBar;
class PDFAppToolInputList;
class PDFAppToolTab;
class PDFAppToolUndoTarget;

/// The body of the own tab of a tool (plan of step 2 of phase 2, 4.2): the layout of the
/// tool mode (PDFAppToolModeLayout, shared with the document view) with its own status bar
/// and no document. Outside of the mode the canvas area is empty (bg/track). The view
/// holds the parts of a tool like the document view in its tool mode: they are its
/// children while the mode is active; a view, which dies during the mode, deletes them
/// with it (the shell holds them in QPointer). Undo and Redo of the tab act on the target
/// of the mode (the parameter model, owner rule "Undo" of the first batch, 12).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTabView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolTabView(QWidget* parent = nullptr);
    virtual ~PDFAppToolTabView() override;

    PDFAppToolModeLayout* getLayout() const { return m_layout.data(); }
    QWidget* getCanvasArea() const;
    PDFAppDocumentStatusBar* getStatusBar() const;

    /// Shows the parts. \returns false, when the mode is active already (the parts stay
    /// untouched). The keyboard focus goes to the canvas page or the first part. No signal.
    bool enterToolMode(const PDFAppToolModeParts& parts);

    /// The parts go back to their parents, hidden. Emits toolModeChanged(false) last.
    void leaveToolMode();

    bool isToolModeActive() const { return m_isToolModeActive; }

    /// Target of Undo and Redo of the mode (nullptr out of the mode or a deleted target)
    PDFAppToolUndoTarget* getUndoTarget() const;

    /// Message of the tool in the status bar during the mode (plain text, bounded); out of
    /// the mode the status bar is empty
    void setToolMessage(const QString& text);
    QString getToolMessage() const { return m_toolMessage; }

    /// A message for a moment (\p timeout ms), also after the mode
    void showTransientMessage(const QString& text, int timeout);

    /// Texts of the page item and of the zoom item of the status bar during the mode (Merge
    /// "16 pages", Blank "Page 1 / 3" and "151 %"; empty = the item is hidden, K12); the items
    /// only show their texts
    void setStatusItems(const QString& pageText, const QString& zoomText);

Q_SIGNALS:
    /// The mode has been entered or left (also by itself: every part and the target are gone)
    void toolModeChanged(bool isActive);

    /// The result of canUndo / canRedo of the target may have changed (also the target is gone)
    void undoStateChanged();

    /// A button of an info bar has been clicked (see PDFAppToolModeLayout)
    void infoBarActionTriggered(const QString& infoBarId, const QString& actionId);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onPartDestroyed();
    void onPartsChangedQueued();
    void onUndoStateChanged();

    QPointer<PDFAppToolModeLayout> m_layout;
    QPointer<QWidget> m_emptyCanvas;    ///< The canvas outside of the mode (bg/track)
    QPointer<PDFAppToolUndoTarget> m_undoTarget;
    QString m_toolMessage;
    bool m_isToolModeActive = false;
    bool m_hasParts = false;
    bool m_isPartsUpdateQueued = false;
};

/// The own tab of a tool as the host of its shell (plan of step 2 of phase 2, 4.2): the
/// second implementation of PDFAppToolHost besides PDFAppDocumentToolHost. No document
/// (getDocument, getDocumentTab, getView, getPdfWidget are nullptr), the environment has no
/// selection and no current page, the color management is the shared copy of the settings
/// of the application in the GUI thread (F12), the inputs are the list of the tab. Undo and
/// Redo of the toolbar act on the parameter model of the tool (the target of the mode).
///
/// Ownership: the page of the tab owns the host and the shell; the view and the tab are
/// held by QPointer (either may die first: every function then does nothing).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOwnTabToolHost : public PDFAppToolHost
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolHost;

public:
    /// \param tab The own tab of the tool (its list of inputs, its activation)
    /// \param view The body of the tab
    /// \param parent Parent object (the page of the tab)
    explicit PDFAppOwnTabToolHost(PDFAppToolTab* tab, PDFAppToolTabView* view, QObject* parent);
    virtual ~PDFAppOwnTabToolHost() override;

    PDFAppToolTabView* getTabView() const { return m_view.data(); }

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
    virtual void setStatusItems(const QString& pageText, const QString& zoomText) override;
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
    virtual PDFAppTab* getTab() const override;
    virtual PDFAppToolInputList* getInputList() const override;
    virtual bool isOwnTab() const override;

    /// The tool runs or its result dialog is open: Undo and Redo of the tool are off (the
    /// page of the tab says it from the state of the shell)
    void setBusy(bool isBusy);
    bool isBusy() const { return m_isBusy; }

    /// Longest time of a transient message (ms)
    static constexpr int MESSAGE_TIMEOUT = 8000;

private:
    void onToolModeChanged(bool isActive);

    /// The left panel of the installed tool (nullptr = none)
    QWidget* getLeftPanel() const;

    QPointer<PDFAppToolTab> m_tab;
    QPointer<PDFAppToolTabView> m_view;
    bool m_isBusy = false;
    bool m_isLeaving = false;   ///< The shell leaves the mode itself: no modeLeft
};

}   // namespace pdf

#endif // PDFAPPOWNTABTOOLHOST_H
