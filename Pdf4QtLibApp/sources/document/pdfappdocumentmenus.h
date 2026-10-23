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

#ifndef PDFAPPDOCUMENTMENUS_H
#define PDFAPPDOCUMENTMENUS_H

#include "pdfappglobal.h"

#include <QAbstractButton>
#include <QMenu>
#include <QPointer>
#include <QWidget>

#include <map>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppDocumentView;
class PDFLabel;
class PDFLineEdit;
class PDFPreviewCard;
class PDFTextLink;

/// Row of a panel (component Menu item 190:704): the icon of 16 px, the label,
/// the optional shortcut and the check mark of a checked item, 32 px high. The
/// row is drawn by the style exactly as an item of a menu (CE_MenuItem): the fill
/// of the hover, the focus ring of the keyboard inside, the disabled colors.
/// Space, Enter and Return click it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPanelMenuItem : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    /// Height of the row
    static constexpr int HEIGHT = 32;

    explicit PDFAppPanelMenuItem(const QString& iconName, QWidget* parent = nullptr);

    const QString& getIconName() const { return m_iconName; }

    /// Text of the shortcut at the trailing side (empty = none)
    const QString& getShortcutText() const { return m_shortcutText; }
    void setShortcutText(const QString& text);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    QString m_iconName;
    QString m_shortcutText;
};

/// Panel of a toolbar menu button (File 196:2583, View 196:2749, Go to 196:3437):
/// a popover window of the style below the toolbar (4 px gap), aligned with the
/// leading edge of its button, padding 16, radius 12, sections 14 px apart. It
/// holds action tiles (Action tile 190:740), rows (PDFAppPanelMenuItem) and other
/// controls; a tile and a row close the panel and run their command from the event
/// loop (the command may delete the tab), cards and check boxes change the view
/// and keep the panel open. Esc closes it, the arrows move the focus. The panel
/// reads the states of its commands, when it opens and when they change.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentPanelPopup : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Padding inside of the border, radius of the panel, gap of the sections
    static constexpr int PADDING = 16;
    static constexpr int RADIUS = 12;
    static constexpr int SECTION_GAP = 14;

    /// Height of an action tile and the gap of the tiles
    static constexpr int TILE_HEIGHT = 56;
    static constexpr int TILE_GAP = 6;

    /// \param panelWidth Width of the panel with its border (the design)
    explicit PDFAppDocumentPanelPopup(PDFAppDocumentView* view, int panelWidth, QWidget* parent);
    virtual ~PDFAppDocumentPanelPopup() override;

    PDFAppDocumentView* getView() const;

    /// Opens the panel: its top edge lies at \p globalTop, its leading edge at the
    /// leading edge of the anchor (both kept inside of the screen)
    void popup(QWidget* anchor, int globalTop);

    /// The anchor of the open panel (nullptr, if the panel is closed)
    QWidget* getAnchor() const;

    /// Rectangle of the panel without the margins of its shadow
    QRect getPanelRect() const;

    /// Button of the command in the panel (a tile or a row), nullptr if there is none
    QAbstractButton* getCommandButton(const QString& commandId) const;

    /// Reads the states of the commands again (enabled, checked). A closed panel
    /// does not follow the view (every scroll step changes it): it reads them,
    /// when it opens.
    virtual void updateState();

Q_SIGNALS:
    /// The panel has been closed (sent from the event loop, the slot may delete it)
    void closed();

protected:
    /// Layout of the content (inside of the padding)
    QVBoxLayout* getContentLayout() const { return m_contentLayout; }

    /// Caption of a section (Medium 11 text/muted)
    PDFLabel* createCaption(const char* sourceText);

    /// Action tile of a command (Action tile 190:740: icon 18, label Medium 11, 56 px high)
    QToolButton* createTile(const QString& commandId, const QString& iconName, const char* sourceText);

    /// Row of a command (Menu item); \p showShortcut shows the shortcut of the command
    PDFAppPanelMenuItem* createMenuItem(const QString& commandId, const QString& iconName, const char* sourceText, bool showShortcut = false);

    /// Row with its own reaction (not a command)
    PDFAppPanelMenuItem* createActionItem(const QString& iconName, const char* sourceText);

    /// Row of tiles, which share the width; \p columns > number of the tiles leaves empty places
    QWidget* createTileRow(const std::vector<QToolButton*>& tiles, int columns = 0);

    /// Horizontal divider (1 px border/divider)
    QWidget* createDivider();

    /// Text of the widget translated again at a change of the language
    void addText(QWidget* widget, const char* sourceText);

    /// Name of the panel for assistive technologies (translated again at a change of the language)
    void setTitleText(const char* sourceText);

    /// Closes the panel and runs the command from the event loop
    void runCommand(const QString& commandId);

    /// Closes the panel and opens the section of Settings from the event loop
    void openSettings(const QString& section);

    /// Called at a change of the language after the texts of addText
    virtual void retranslate();

    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onViewStateChanged();
    void onCommandButtonClicked();
    void onHiddenQueued(quint64 openCount);
    void onAnchorDestroyed();
    void watchAnchor(bool watch);
    void updateTexts();

    QPointer<PDFAppDocumentView> m_view;
    QPointer<QVBoxLayout> m_contentLayout;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_anchorWindow;
    QMetaObject::Connection m_anchorDestroyed;
    std::map<QString, QPointer<QAbstractButton>> m_commandButtons;
    std::vector<std::pair<QPointer<QWidget>, const char*>> m_texts;
    const char* m_titleText = nullptr;
    int m_panelWidth = 0;
    quint64 m_openCount = 0;
    bool m_isOpen = false;
};

/// File panel (196:2583): tiles Save, Save as, Print, E-mail (hidden without
/// PDFAppSendMail::isAvailable, D2: macOS); rows Properties…,
/// Automatic refresh, Lock document, Settings…; Close document with its shortcut
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentFilePanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    explicit PDFAppDocumentFilePanel(PDFAppDocumentView* view, QWidget* parent);

    virtual void updateState() override;

private:
    void onSettingsClicked();
    void onCloseClicked();

    QPointer<PDFAppPanelMenuItem> m_closeItem;
};

/// View panel (196:2749): cards of the page layouts and of the page colors, tiles
/// of the zoom and the rotation, the magnifier and the full screen, the check
/// boxes Show annotations and First page on the right side, the link Rendering
/// settings. The cards and the check boxes keep the panel open.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentViewPanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    explicit PDFAppDocumentViewPanel(PDFAppDocumentView* view, QWidget* parent);

    virtual void updateState() override;

    /// Card of a page layout or of a color mode command (nullptr for another command)
    PDFPreviewCard* getCard(const QString& commandId) const;

    QCheckBox* getShowAnnotationsCheckBox() const;
    QCheckBox* getFirstPageRightCheckBox() const;
    PDFTextLink* getRenderingSettingsLink() const;

private:
    PDFPreviewCard* createCard(const QString& commandId, int preview, QButtonGroup* group);
    void onCardClicked();
    void onCheckBoxClicked();
    void onRenderingSettingsClicked();

    std::map<QString, QPointer<PDFPreviewCard>> m_cards;
    QPointer<QCheckBox> m_showAnnotations;
    QPointer<QCheckBox> m_firstPageRight;
    QPointer<PDFTextLink> m_renderingSettings;
};

/// Go to panel (196:3437): tiles of the pages, the field Go to page with the
/// count of the pages and the button Go, tiles of the lines and of the
/// bookmarks (package C), rows Export / Import bookmarks… and the setting
/// Generate automatically (bookmarks.autoGenerate)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentGoToPanel : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    explicit PDFAppDocumentGoToPanel(PDFAppDocumentView* view, QWidget* parent);

    virtual void updateState() override;

    PDFLineEdit* getPageField() const;
    QPushButton* getGoButton() const;
    PDFAppPanelMenuItem* getAutoGenerateItem() const;

    /// Focuses the field of the page with its text selected
    void focusPageField();

protected:
    virtual void retranslate() override;
    virtual void showEvent(QShowEvent* event) override;

private:
    void onGoClicked();
    void onPageTextEdited();
    void onAutoGenerateClicked();

    QPointer<PDFLineEdit> m_pageField;
    QPointer<PDFLabel> m_pageCount;
    QPointer<QPushButton> m_goButton;
    QPointer<PDFAppPanelMenuItem> m_autoGenerate;
};

/// Menu of the Select button (198:5517): Select text, Select table, Screenshot,
/// Extract image as one exclusive choice (the tool of the canvas). The panel is
/// 232 px wide and it opens 4 px below the toolbar as the panels of the toolbar
/// (y 94 of 198:5380), not at the bottom of the button.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSelectMenu : public QMenu
{
    Q_OBJECT

private:
    using BaseClass = QMenu;

public:
    /// Width of the panel of the menu (menu-select 198:5517)
    static constexpr int PANEL_WIDTH = 232;

    explicit PDFAppDocumentSelectMenu(PDFAppDocumentView* view, QWidget* parent);

    /// Action of the command (nullptr for another command)
    QAction* getCommandAction(const QString& commandId) const;

    /// Reads the states of the tools again
    void updateState();

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private:
    void retranslate();
    void placeBelowToolbar();
    void onAboutToShow();
    void onActionTriggered(QAction* action);

    QPointer<PDFAppDocumentView> m_view;
    std::map<QString, QPointer<QAction>> m_actions;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTMENUS_H
