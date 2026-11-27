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

#ifndef PDFAPPEDITCONTENTMENUS_H
#define PDFAPPEDITCONTENTMENUS_H

#include "pdfappglobal.h"
#include "pdfdeferreddeletion.h"

#include <QKeySequence>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>

namespace pdf
{

class PDFAppEditContentActionEntry;
class PDFAppEditContentContext;
class PDFAppEditContentRegistry;
class PDFAppToolPanelPopup;

// Edit Content, stage E1, package P12. Plan: zadani_gui.md 4.18 ("the context menu of a row and
// of an object on the canvas is the same"), 4.20 (menus), 4.16 ("rows, whose function comes in
// a later stage, are hidden"); the frames -objects-context-menu, -menus-view.

/// Builds the menus of the tool from the action entries of the registry and shows them.
///
/// - A context menu opens on contextMenuRequested(menuId, position) of the context (openMenu of
///   that menu: CONTEXT of the canvas and of the tab Objects, LAYER_ROW of E4, TAG_ROW of E5): a
///   QMenu shown by popup() (never exec). Its rows are the actions placed in the
///   menu, which are visible now, in groups with dividers; a row shows the key of its action; an
///   action with choices is a submenu. A row only calls PDFAppEditContentContext::triggerAction
///   (the work runs from the event loop: Qt reads the menu after triggered).
/// - fillPanel fills a panel of the toolbar (View; a PDFAppToolPanelPopup created by the GUI of
///   the tool) with the rows of a menu of the registry: check rows stay open, the other rows
///   close the panel and trigger their action the same way.
/// - A menu, which is open, when the language changes or the tool closes, is closed: the menus
///   belong to this object (PDFDeferredDeletion::setOwner) and its hook retire() closes them.
/// - This file also registers the action entries of stage E1 with a place in the context menu
///   (order: Bring to Front ... Send to Back; group, ungroup; hide, lock) and in the menu View
///   (Clipping paths, Invisible text, Annotations and fields) - registerParts.
///
/// Threads and ownership: a child of the GUI of the tool (P14 creates it; it lives as long as
/// the tool), GUI thread. The menus it opens are parented to the window and owned logically
/// (setOwner) by this object.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentMenus : public QObject, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppEditContentMenus(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentMenus() override;

    /// Opens the menu of the registry at the global position (popup); a menu open already is
    /// closed first. \returns the menu, nullptr = it has no visible row (nothing is shown).
    QMenu* openMenu(const QString& menuId, const QPoint& globalPosition);

    /// The open menu, nullptr = none
    QMenu* getOpenMenu() const { return m_menu.data(); }

    /// Fills the panel of a toolbar button with the rows of the menu of the registry (the rows
    /// before are removed); called again when the panel opens (the checks and the titles follow
    /// the state)
    void fillPanel(PDFAppToolPanelPopup* panel, const QString& menuId);

    /// The entries of this file for the registry (see the class)
    static void registerParts(PDFAppEditContentRegistry* registry);

    // ---- P12's own functions (this file and the tests of P12)

    /// The property of a QAction of a menu and of a PDFTileRow of a panel: the id of its action
    /// entry (and of its choice, when it is a row of a submenu)
    static constexpr const char* ACTION_PROPERTY = "editContentAction";
    static constexpr const char* CHOICE_PROPERTY = "editContentChoice";

    /// The key shown at the row of the entry: its first key, else the default shortcut of the
    /// first command of the application it answers (the Delete row shows Del); empty = none
    static QKeySequence getShownKey(const PDFAppEditContentActionEntry* entry, const PDFAppEditContentContext* context);

    /// The description of a row of a panel (Clipping paths: "Blue dashed outlines"): the entries of
    /// this file have one, an entry of another package none (request of P12 to the coordinator:
    /// PDFAppEditContentActionEntry::getDescription)
    static QString getDescription(const PDFAppEditContentActionEntry* entry);

protected:
    virtual void retire() override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onContextMenuRequested(const QString& menuId, const QPoint& globalPosition);
    void onMenuTriggered(QAction* action);
    void closeMenu();

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<QMenu> m_menu;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTMENUS_H
