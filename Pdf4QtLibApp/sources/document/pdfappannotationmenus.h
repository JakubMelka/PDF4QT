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

#ifndef PDFAPPANNOTATIONMENUS_H
#define PDFAPPANNOTATIONMENUS_H

#include "pdfappglobal.h"

#include "pdfwidgetannotation.h"

#include <QObject>
#include <QPointer>
#include <QVariant>

class QAction;
class QMenu;

namespace pdf
{

class PDFAppDocumentView;

/// Context menus of the annotations on the page (Figma 1019:18817 single,
/// 1019:19473 multiple with Align, 1019:20171 the chooser of overlapping
/// annotations, 1026:21995 hidden annotations and replies; note 1030:161012):
/// built from a PDFAnnotationMenuRequest of the engine (E4). The items run the
/// commands of package B from the event loop (a command may open a dialog or delete
/// the tab); the items of a point, a part or a callout line call the engine.
///
/// Locked document: Open Comment, Copy, Properties… (read only), Select All.
/// Locked annotation (ISO 32000-2 Table 167): no Cut, Delete, Hide, geometry,
/// rotation or alignment. The menu is a popup of its own (never exec on the
/// stack), it deletes itself when it closes; one menu at a time.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationMenus : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Widths of the panels of the menus of the design (single 300, multiple 260,
    /// chooser 330, the submenus Align 1019:20080 and of a row of the chooser
    /// 1019:20702 250); the window of a menu is wider by the margins of its shadow
    static constexpr int SINGLE_WIDTH = 300;
    static constexpr int MULTI_WIDTH = 260;
    static constexpr int CHOOSER_WIDTH = 330;
    static constexpr int SUBMENU_WIDTH = 250;

    /// Sets the minimal width of the panel of the menu (the window of the menu
    /// holds the shadow too, PDFWidgetStyle::getPopoverMargins)
    static void setPanelWidth(QMenu* menu, int width);

    explicit PDFAppAnnotationMenus(PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppAnnotationMenus() override;

    /// Shows the menu of the request (nothing for the kind None); a menu, which
    /// is open, is closed first. \returns the menu (nullptr = none)
    QMenu* showMenu(const PDFAnnotationMenuRequest& request);

    /// Builds the menu of the request without showing it (tests, screenshots)
    QMenu* createMenu(const PDFAnnotationMenuRequest& request, QWidget* parent);

    /// Menu, which is shown (nullptr = none)
    QMenu* getMenu() const { return m_menu.data(); }

    /// Finds the action of the command (or of an operation of the engine,
    /// "point.remove", ...) in the menu and in its submenus
    static QAction* findAction(const QMenu* menu, const QString& id);

    /// Reference of the annotation of an action (an item of a row of the chooser)
    /// stored in a property: the object and the generation numbers are kept whole
    /// (a hand-written document may have numbers beyond 32 bits)
    static QVariant toVariant(PDFObjectReference reference);
    static PDFObjectReference fromVariant(const QVariant& value);

private:
    void onActionTriggered();

    /// A row of the chooser of overlapping annotations is hovered: its annotation is
    /// outlined on the page (1019:20672); the outline goes, when the chooser closes
    void onChooserHovered(QAction* action);
    void onChooserHidden();
    void setHighlightedAnnotation(PDFObjectReference annotation);

    void addCommand(QMenu* menu, const QString& commandId, const QString& text, const QString& iconName, PDFObjectReference annotation, bool isEnabled = true);
    QAction* addOperation(QMenu* menu, const QString& id, const QString& text, const QString& iconName, QAction* before = nullptr);
    void addSection(QMenu* menu, const QString& text);
    void fillSelectionMenu(QMenu* menu, const PDFAnnotationMenuRequest& request, PDFObjectReference annotation, bool isChooser);
    void fillUnselectableMenu(QMenu* menu, const PDFAnnotationMenuRequest& request);
    void fillChooserMenu(QMenu* menu, const PDFAnnotationMenuRequest& request);
    void fillEngineItems(QMenu* menu, const PDFAnnotationMenuRequest& request);
    QMenu* createRotateMenu(QMenu* parent, PDFObjectReference annotation, bool isEnabled);
    QMenu* createAlignMenu(QMenu* parent, bool isEnabled);
    void runOperation(const QString& id);
    QKeySequence getShortcut(const QString& commandId) const;

    QPointer<PDFAppDocumentView> m_view;
    QPointer<QMenu> m_menu;
    QPointer<PDFWidgetAnnotationManager> m_highlightManager;   ///< Manager, which outlines the annotation of the hovered row of the chooser
    PDFAnnotationMenuRequest m_request;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONMENUS_H
