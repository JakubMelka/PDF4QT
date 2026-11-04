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

#ifndef PDFAPPTOOLTOOLBAR_H
#define PDFAPPTOOLTOOLBAR_H

#include "pdfappglobal.h"
#include "pdfapptoolpanelpopup.h"

#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"

#include <QPointer>
#include <QToolButton>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QPushButton;
class QToolButton;

namespace pdf
{

class PDFAppToolHost;
class PDFAppToolbarDivider;
class PDFDocumentToolCatalogEntry;

/// Toolbar of a tool (toolbar 291:1000, 52 px, bg/subtle, paddings 0 / 12, gap 4): at
/// the leading side (gap 2) the sidebar, a divider, the title (the Tool icon 28 and Semi
/// Bold 14), a divider, Undo, Redo, a divider and the actions of the tool; at the trailing
/// side (gap 8) Tutorial (disabled with its place kept, owner answer Q5), Cancel and
/// the Primary button - during a run Tutorial and Stop (295:1711), while the result
/// waits for the decision only Tutorial (K2). The commands go to the host from the event
/// loop. The panels of the actions behave as a menu bar: with a panel open the pointer
/// over another panel button or Left / Right opens that panel.
///
/// In a narrow window it narrows itself as PDFAppDocumentToolbar: Tutorial loses its text,
/// the name of the tool is hidden, the actions lose their texts and labels, Tutorial
/// is hidden, Undo / Redo are hidden (their keys stay), the Primary button loses its text
/// (Compress at 720 x 480: the segments have a fixed width) and at last Cancel is hidden
/// (Esc stays); it never makes the window wider and the actions never overlap.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolToolbar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Height of the toolbar
    static constexpr int HEIGHT = 52;
    static constexpr int PADDING_H = 12;
    static constexpr int GROUP_GAP = 4;
    static constexpr int LEFT_GAP = 2;
    static constexpr int RIGHT_GAP = 8;

    /// Gap between the toolbar and a panel opened below it (PDFAppDocumentToolbar::PANEL_GAP)
    static constexpr int PANEL_GAP = 4;

    /// Property of a label of the actions, which a narrow toolbar hides ("Preset")
    static constexpr const char* NARROW_HIDDEN_PROPERTY = "pdf4qtToolbarNarrowHidden";

    /// What the trailing side shows
    enum class Mode
    {
        Editing,    ///< Tutorial, Cancel, the Primary button
        Running,    ///< Tutorial, Stop
        Finished    ///< Tutorial
    };

    /// \param host Host of the tool (the commands)
    /// \param tool The tool (its title and icon)
    /// \param parent Parent widget
    explicit PDFAppToolToolbar(PDFAppToolHost* host, const PDFDocumentToolCatalogEntry& tool, QWidget* parent);
    virtual ~PDFAppToolToolbar() override;

    /// Replaces the actions of the tool (nullptr = none and no third divider); the old
    /// ones are deleted later (they may be the sender of the change)
    void setToolActions(QWidget* actions);
    QWidget* getToolActions() const { return m_actions.data(); }

    void setMode(Mode mode);
    Mode getMode() const { return m_mode; }

    /// Text and icon of the Primary button
    void setConfirmText(const QString& text, const QString& iconName);
    void setConfirmEnabled(bool isEnabled);

    /// Text of the Primary button; the button itself has none in a very narrow window
    const QString& getConfirmText() const { return m_confirmText; }

    /// Adds a panel of the actions: a click on \p button opens the panel below it
    /// (toolbar + PANEL_GAP), the button is checked while it is open
    void addPanel(QToolButton* button, PDFAppToolPanelPopup* panel);

    /// Opens the panel of the button. \returns the panel, nullptr for a hidden, disabled or unknown button.
    PDFAppToolPanelPopup* openPanel(QToolButton* button);

    /// The panel, which is open, nullptr = none
    PDFAppToolPanelPopup* getOpenPanel() const;

    /// Menu bar: with a panel open, the panel of \p button opens instead of it
    /// \returns true, when it is open. The toolbar may be deleted meanwhile.
    bool switchPanel(QToolButton* button);

    /// Buttons of the panels in their order
    std::vector<QToolButton*> getPanelButtons() const;

    /// Number of the steps, which make the toolbar narrower (0 = everything shown)
    int getCollapseLevel() const { return m_collapseLevel; }

    /// Parts of the toolbar
    QToolButton* getSidebarButton() const { return m_sidebarButton; }
    PDFToolBadge* getBadge() const { return m_badge; }
    PDFLabel* getTitleLabel() const { return m_titleLabel; }
    QWidget* getTitleGroup() const { return m_titleGroup; }
    QToolButton* getUndoButton() const { return m_undoButton; }
    QToolButton* getRedoButton() const { return m_redoButton; }
    QToolButton* getTutorialButton() const { return m_tutorialButton; }
    QPushButton* getCancelButton() const { return m_cancelButton; }
    QPushButton* getConfirmButton() const { return m_confirmButton; }
    QPushButton* getStopButton() const { return m_stopButton; }

    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    void cancelClicked();
    void confirmClicked();
    void stopClicked();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PanelItem
    {
        QPointer<QToolButton> button;
        QPointer<PDFAppToolPanelPopup> panel;
    };

    void retranslate();
    void updateCommands();
    void updateCollapse();
    void applyCollapse(int level);
    void invalidateGroupSizes();
    void onCommandClicked();
    void onPanelButtonClicked();
    void onPanelClosed();
    void onPanelNeighborRequested(bool isNext);

    QToolButton* findPanelButton(const PDFAppToolPanelPopup* panel) const;

    QPointer<PDFAppToolHost> m_host;
    QString m_toolId;
    QPointer<QHBoxLayout> m_layout;
    QPointer<QWidget> m_leftGroup;
    QPointer<QHBoxLayout> m_leftLayout;
    QPointer<QWidget> m_rightGroup;
    QPointer<QToolButton> m_sidebarButton;
    QPointer<QWidget> m_titleGroup;
    QPointer<PDFToolBadge> m_badge;
    QPointer<PDFLabel> m_titleLabel;
    QPointer<QToolButton> m_undoButton;
    QPointer<QToolButton> m_redoButton;
    QPointer<PDFAppToolbarDivider> m_undoDivider;
    QPointer<PDFAppToolbarDivider> m_actionsDivider;
    QPointer<QWidget> m_actions;
    QPointer<QToolButton> m_tutorialButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_confirmButton;
    QPointer<QPushButton> m_stopButton;
    std::vector<PanelItem> m_panels;
    QString m_confirmText;
    Mode m_mode = Mode::Editing;
    int m_collapseLevel = 0;
    bool m_isUpdatingCollapse = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLTOOLBAR_H
