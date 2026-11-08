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

#ifndef PDFAPPTOOLMODELAYOUT_H
#define PDFAPPTOOLMODELAYOUT_H

#include "pdfappglobal.h"
#include "pdfappdocumentview.h"

#include "pdfappdocumentstatusbar.h"

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QObject>
#include <QPointer>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

namespace pdf
{

class PDFAppInfoBar;

/// The layout of the tool mode (the parts: PDFAppToolModeParts), one implementation for the document view and for the own
/// tab of a tool (plan of step 2 of phase 2, 4.2, "nothing is copied"): from the top the
/// toolbar stack (52 px, toolbar 291:1000), the body - the splitter (the left widget, the
/// handle 6 px with the grip 266:7854, the canvas area) and the right panel next to it
/// (320 px, no handle before it, document-body 291:1029) - and the status bar (28 px,
/// 193:1319). The info bars lie over the canvas area (1028:24277, 12 px below the toolbar,
/// 8 px apart, centered over the canvas page or the base canvas).
///
/// Outside of the mode the places show the base widgets of the owner (the document view:
/// its toolbar, sidebar and PDFWidget; the own tab: none, an empty canvas of bg/track);
/// \p installParts puts the parts of a tool into the places, \p removeParts gives them
/// back. The layout owns its widgets (children of the host), never the parts. Every part
/// is watched: \p partDestroyed tells, that one has gone (from its destructor - queue
/// what follows). The functions run in the GUI thread; no function emits besides
/// \p partDestroyed and \p infoBarActionTriggered.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolModeLayout : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Width of the right panel of a tool (sidebar-right 291:1177)
    static constexpr int RIGHT_PANEL_WIDTH = PDFAppDocumentView::TOOL_RIGHT_PANEL_WIDTH;

    /// Width of the handle of the splitter (266:7854)
    static constexpr int HANDLE_WIDTH = 6;

    /// Width of the left panel of a tool at its entry (sidebar-left 291:1030)
    static constexpr int DEFAULT_LEFT_WIDTH = 300;

    /// Gap of the info bars below the toolbar and between them (1028:24277)
    static constexpr int INFO_BAR_TOP_GAP = PDFAppDocumentView::INFO_BAR_TOP_GAP;
    static constexpr int INFO_BAR_GAP = PDFAppDocumentView::INFO_BAR_GAP;

    /// Creates the places in \p host, from the top: the toolbar stack and the body (stretch 1),
    /// children of \p host added to \p hostLayout (a vertical layout of \p host without
    /// margins and spacing). The layout is a child of \p host.
    explicit PDFAppToolModeLayout(QWidget* host, QBoxLayout* hostLayout);
    virtual ~PDFAppToolModeLayout() override;

    /// Creates the status bar below the body (once; the owner creates it after its base
    /// widgets, so the order of the keyboard focus stays the order of the places)
    PDFAppDocumentStatusBar* createStatusBar(QBoxLayout* hostLayout);

    QWidget* getHost() const { return m_host.data(); }
    QStackedWidget* getToolbarStack() const { return m_toolbarStack.data(); }
    QHBoxLayout* getBodyLayout() const { return m_bodyLayout.data(); }
    QSplitter* getSplitter() const { return m_splitter.data(); }
    QWidget* getCanvasArea() const { return m_canvasArea.data(); }
    QStackedLayout* getCanvasLayout() const { return m_canvasLayout.data(); }
    PDFAppDocumentStatusBar* getStatusBar() const { return m_statusBar.data(); }

    /// Base widgets shown outside of the mode and in the place of a missing part. The
    /// toolbar is a child of the toolbar stack, the left widget of the splitter, the canvas
    /// of the canvas area (the owner creates them with these parents); null = none (the own
    /// tab of a tool always has its parts). Set once, before the first \p installParts.
    void setBaseToolbar(QWidget* toolbar);
    void setBaseLeftWidget(QWidget* leftWidget);
    void setBaseCanvas(QWidget* canvas);
    QWidget* getBaseToolbar() const { return m_baseToolbar.data(); }
    QWidget* getBaseLeftWidget() const { return m_baseLeftWidget.data(); }
    QWidget* getBaseCanvas() const { return m_baseCanvas.data(); }

    /// Puts the widget parts into their places (the undo target is the owner's). The left
    /// panel starts with DEFAULT_LEFT_WIDTH and replaces the base left widget, which the
    /// owner hides (\p getLeftWidget).
    void installParts(const PDFAppToolModeParts& parts);

    /// Gives the parts back to their former parents, hidden; the base widgets come back.
    /// \returns the keyboard focus was in a part (the owner gives it to its canvas)
    bool removeParts();

    /// The shown parts (nullptr = none or deleted)
    QWidget* getToolbarPart() const { return m_toolbar.widget.data(); }
    QWidget* getLeftPanelPart() const { return m_leftPanel.widget.data(); }
    QWidget* getCanvasPagePart() const { return m_canvasPage.widget.data(); }
    QWidget* getRightPanelPart() const { return m_rightPanel.widget.data(); }

    /// A part of the mode is still alive
    bool isPartAlive() const;

    /// The widget in the place of the left widget: the left panel of a tool, else the base
    QWidget* getLeftWidget() const;

    /// The widget shown in the canvas area: the canvas page of a tool, else the base canvas
    QWidget* getCanvasWidget() const;

    /// Width of the left panel of the tool (the user may drag the splitter)
    int getLeftPanelWidth() const { return m_leftPanelWidth; }
    void setLeftPanelWidth(int width) { m_leftPanelWidth = width; }

    /// Gives the left widget its width (\p baseLeftWidth for the base widget, the width of
    /// the left panel otherwise): at most a half of the splitter, so a canvas stays also in
    /// a narrow window; nothing for a hidden left widget
    void applySplitterSizes(int baseLeftWidth);

    /// Info bars over the canvas area (a result, which has nothing to decide about): a
    /// request of an id, which is shown already, replaces its content
    void showInfoBar(const PDFAppInfoBarRequest& request);
    void hideInfoBar(const QString& id);
    QStringList getInfoBarIds() const;
    const PDFAppInfoBarRequest* findInfoBar(const QString& id) const;
    PDFAppInfoBar* findInfoBarWidget(const QString& id) const;

    /// Places the info bars over the canvas widget (after a change of its geometry)
    void layoutInfoBars();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    /// A part has been destroyed (emitted from its destructor: the part is no longer of its
    /// class; a receiver queues what follows)
    void partDestroyed();

    /// A button of an info bar has been clicked (\p actionId empty = the bar was closed by
    /// its close button and it is hidden already); a slot may delete the host
    void infoBarActionTriggered(const QString& infoBarId, const QString& actionId);

private:
    /// A part with the parent, which it goes back to
    struct Part
    {
        QPointer<QWidget> widget;
        QPointer<QWidget> parent;
    };

    void watchPart(Part& part, QWidget* widget);
    void releasePart(Part& part);
    void onPartDestroyed();
    void onInfoBarAction(const QString& infoBarId, const QString& actionId);
    void onInfoBarClosed(const QString& infoBarId);

    QPointer<QWidget> m_host;
    QPointer<QStackedWidget> m_toolbarStack;
    QPointer<QHBoxLayout> m_bodyLayout;
    QPointer<QSplitter> m_splitter;
    QPointer<QWidget> m_canvasArea;
    QPointer<QStackedLayout> m_canvasLayout;
    QPointer<PDFAppDocumentStatusBar> m_statusBar;

    QPointer<QWidget> m_baseToolbar;
    QPointer<QWidget> m_baseLeftWidget;
    QPointer<QWidget> m_baseCanvas;

    Part m_toolbar;
    Part m_leftPanel;
    Part m_canvasPage;
    Part m_rightPanel;
    int m_leftPanelWidth = DEFAULT_LEFT_WIDTH;

    std::vector<PDFAppInfoBarRequest> m_infoBars;
    std::vector<QPointer<PDFAppInfoBar>> m_infoBarWidgets;
};

}   // namespace pdf

#endif // PDFAPPTOOLMODELAYOUT_H
