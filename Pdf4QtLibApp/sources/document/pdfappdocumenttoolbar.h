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

#ifndef PDFAPPDOCUMENTTOOLBAR_H
#define PDFAPPDOCUMENTTOOLBAR_H

#include "pdfappglobal.h"

#include "pdfthemecolors.h"

#include <QPointer>
#include <QWidget>

#include <map>
#include <vector>

class QHBoxLayout;
class QToolButton;

namespace pdf
{

class PDFAppDocumentPanelPopup;
class PDFAppDocumentSelectMenu;
class PDFAppDocumentView;
class PDFLineEdit;
class PDFToolBarField;

/// Divider of the toolbar, of the info bars and of the panels: a line of 1 px
/// with the paddings at its sides (vertical divider 193:792 border/separator,
/// horizontal divider 196:2605 border/divider over the whole width)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolbarDivider : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// \param orientation Direction of the line
    /// \param length Length of the line (0 = the line fills the widget)
    /// \param padding Padding at each side of the line
    /// \param color Color of the line
    explicit PDFAppToolbarDivider(Qt::Orientation orientation, int length, int padding, PDFThemeColors::Color color, QWidget* parent = nullptr);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    Qt::Orientation m_orientation;
    int m_length;
    int m_padding;
    PDFThemeColors::Color m_color;
};

/// Toolbar of the document view (Figma toolbar 193:785, 52 px, bg/subtle with the
/// border at the bottom): the sidebar, save, undo and redo buttons and the menu
/// buttons File / View / Annotate / Insert / Go to / Tools at the leading side,
/// the page navigation and the zoom in the center, the Select menu button and the
/// search field at the trailing side. The buttons run the commands of the tab
/// (PDFAppDocumentView::requestCommand), the menu buttons open the panels below
/// them. In a narrow window the menu buttons lose their texts, the search field
/// gets narrower and the zoom, Annotate / Insert and the page navigation are hidden
/// in this order (the commands stay in the panels and on the keyboard).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentToolbar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Height of the toolbar
    static constexpr int HEIGHT = 52;

    /// Width of the search field (search 193:878) and its narrow widths
    static constexpr int SEARCH_FIELD_WIDTH = 240;
    static constexpr int SEARCH_FIELD_NARROW_WIDTH = 160;
    static constexpr int SEARCH_FIELD_MINIMUM_WIDTH = 120;

    /// Gap between the toolbar and a panel opened below it
    static constexpr int PANEL_GAP = 4;

    /// Panels opened by the menu buttons
    enum class Panel
    {
        File,
        View,
        GoTo,
        Annotate,   ///< Step 4, package A (877:10157)
        Insert,     ///< Step 4, package A (196:3160)
        Tools       ///< Step 6, package C (196:3650)
    };

    explicit PDFAppDocumentToolbar(PDFAppDocumentView* view, QWidget* parent = nullptr);
    virtual ~PDFAppDocumentToolbar() override;

    PDFAppDocumentView* getView() const;

    /// Search field of the document (search 193:878); package D connects it
    PDFLineEdit* getSearchField() const;

    /// Fields of the page and of the zoom
    PDFToolBarField* getPageField() const;
    PDFToolBarField* getZoomField() const;

    /// Button of a command (nullptr for a command without a button)
    QToolButton* getCommandButton(const QString& commandId) const;

    /// Menu buttons (File, View, Annotate, Insert, Go to, Tools) by the name of
    /// their object (menuFile, menuView, ...), the Select menu button
    QToolButton* getMenuButton(Panel panel) const;
    QToolButton* getAnnotateButton() const;
    QToolButton* getInsertButton() const;
    QToolButton* getToolsButton() const;
    QToolButton* getSelectButton() const;
    PDFAppDocumentSelectMenu* getSelectMenu() const;

    /// Opens the panel below its menu button. \returns the panel (nullptr, when the
    /// button is not visible or the toolbar is hidden).
    PDFAppDocumentPanelPopup* openPanel(Panel panel);

    /// Panel of the menu button (created at the first opening), nullptr before
    PDFAppDocumentPanelPopup* findPanel(Panel panel) const;

    /// Panel, which is open (nullptr, if none)
    PDFAppDocumentPanelPopup* getOpenPanel() const;

    /// Buttons of the panels in the order of the toolbar: File, View, Annotate,
    /// Insert, Go to, Tools and the Select button (its menu is a panel too); the buttons,
    /// which do not exist, are left out
    std::vector<QToolButton*> getPanelButtons() const;

    /// Button of the open panel or of the open Select menu (nullptr, if none is open)
    QToolButton* getOpenPanelButton() const;

    /// Menu bar: with a panel (or the Select menu) open, the panel of \p button
    /// opens instead of it at once (no click; the pointer moved over the button or
    /// Left / Right). Nothing happens without an open panel, for the button of the
    /// open panel, a hidden or disabled button, a button of no panel, or while a
    /// modal window blocks the toolbar. The keyboard focus goes into the new panel
    /// as at a click; \p byKeyboard makes the first item of the Select menu current.
    /// \returns true, when the panel of the button is open. The toolbar can be
    /// deleted meanwhile (an application filter of the shown panel).
    bool switchPanel(QToolButton* button, bool byKeyboard);

    /// Reads the states of the commands, the page and the zoom again
    void updateState();

    /// Number of the steps, which make the toolbar narrower (0 = everything shown)
    int getCollapseLevel() const { return m_collapseLevel; }

    virtual QSize minimumSizeHint() const override;

    /// Tooltips of the icon buttons (PDFAppDocumentView::getCommandToolTip); the view
    /// calls it, when the shortcuts of the user change
    void updateCommandToolTips();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QToolButton* createButton(const QString& objectName, const QString& iconName);
    QToolButton* createCommandButton(const QString& commandId, const QString& iconName);
    QToolButton* createMenuButton(const QString& objectName, const QString& iconName);
    void retranslate();
    void updateCollapse();
    void applyCollapse(int level);
    void invalidateGroupSizes();
    void updatePageField();
    void updateZoomField();
    void updateSelectButton();
    void onCommandButtonClicked();
    void onMenuButtonClicked();
    void onPanelClosed();
    void onPanelDestroyed(QObject* object);
    void onPanelNeighborRequested(bool isNext);
    void onSelectMenuAboutToHide();
    void switchToNeighbor(bool isNext);
    void openSelectMenu(bool byKeyboard);
    QToolButton* findPanelButtonAt(const QPoint& globalPosition) const;
    bool isPanelWindow(const QObject* object) const;
    void onPageFieldEdited();
    void onZoomFieldEdited();
    void onViewChanged();

    QPointer<PDFAppDocumentView> m_view;
    QPointer<QHBoxLayout> m_layout;
    QPointer<QWidget> m_leftGroup;
    QPointer<QWidget> m_pageGroup;
    QPointer<QWidget> m_zoomGroup;
    QPointer<QWidget> m_rightGroup;
    std::map<QString, QPointer<QToolButton>> m_commandButtons;
    std::vector<QPointer<QToolButton>> m_menuButtons;     ///< File, View, Annotate, Insert, Go to, Tools
    QPointer<QToolButton> m_fileButton;
    QPointer<QToolButton> m_viewButton;
    QPointer<QToolButton> m_annotateButton;
    QPointer<QToolButton> m_insertButton;
    QPointer<QToolButton> m_goToButton;
    QPointer<QToolButton> m_toolsButton;
    QPointer<QToolButton> m_selectButton;
    QPointer<PDFAppDocumentSelectMenu> m_selectMenu;
    QPointer<PDFToolBarField> m_pageField;
    QPointer<PDFToolBarField> m_zoomField;
    QPointer<PDFLineEdit> m_searchField;
    std::map<Panel, QPointer<PDFAppDocumentPanelPopup>> m_panels;
    int m_collapseLevel = 0;
    bool m_isUpdatingCollapse = false;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTTOOLBAR_H
