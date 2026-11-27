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

#ifndef PDFAPPEDITCONTENTKEYBOARD_H
#define PDFAPPEDITCONTENTKEYBOARD_H

#include "pdfappglobal.h"
#include "pdfappeditcontentcards.h"
#include "pdfdeferreddeletion.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <utility>
#include <vector>

class QAction;
class QWidget;

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentRegistry;
class PDFKeyValueList;

// Edit Content, stage E1, package P14 (GUI of the tool, shell, keys). Plan: zadani_gui.md 4.20
// (keys: way 3, the list of the keys), 4.23 (F6 between the regions), KE.22, KE.23; the frame
// edit-content-v2-keyboard.

/// The shortcuts of the tool with a modifier (zadani_gui.md 4.20, way 3) and the walk between
/// the regions of the window.
///
/// - install creates one QAction with Qt::WidgetWithChildrenShortcut on the key widget of the
///   host for every key of the scope Tool of the action entries of the registry
///   (PDFAppEditContentKeyTable::getRows); an action only calls
///   PDFAppEditContentContext::triggerAction (the work runs from the event loop). The actions are
///   children of this object, removed from the key widget by uninstall and by the hook retire().
/// - F6 / Shift+F6 move the focus to the next / previous region: toolbar -> left panel ->
///   canvas -> right panel (zadani_gui.md 4.23).
/// - The unmodified keys of the canvas (way 1) are not here: the view gives them to the tool
///   controller. The commands of the application (way 2) are PDFDocumentToolGUI::
///   handleDocumentCommand of the GUI of the tool.
/// - This file also registers the action entries of P14 (Preview, Keyboard Shortcuts, Clear
///   Page) and the entry of the card Keyboard - registerParts.
///
/// Threads and ownership: a child of the GUI of the tool (it lives as long as the tool), GUI
/// thread. The key widget is foreign (the view of the document): held by a QPointer; its
/// actions are removed when the tool ends.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentKeyboard : public QObject, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppEditContentKeyboard(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentKeyboard() override;

    /// Creates the actions on the key widget (the actions created before are removed first)
    void install(QWidget* keyWidget);

    /// Removes the actions from the key widget
    void uninstall();

    /// The actions installed (tests)
    std::vector<QAction*> getActions() const;

    /// The entries of this file for the registry (see the class)
    static void registerParts(PDFAppEditContentRegistry* registry);

    /// The rows of the card Keyboard for the registry (edit-content-v2-keyboard; P14's own files and tests): the
    /// keys of the canvas of the tool Select (when it is registered), the shortcuts of the action entries, the
    /// letters of the tools, Preview and F6 - (key text, description), translated
    static std::vector<std::pair<QString, QString>> getKeyRows(const PDFAppEditContentRegistry& registry);

    /// The identifiers of the action entries of this file, which answer the commands of the application
    /// (zadani_gui.md 4.20, way 2): "command." + the identifier of the command (tests)
    static QString getCommandActionId(const QString& commandId);

    /// The regions of the window in the order of F6 (zadani_gui.md 4.23): the toolbar, the left panel, the canvas,
    /// the right panel; a region, which does not exist, is left out (tests)
    std::vector<QWidget*> getRegions() const;

protected:
    virtual void retire() override;

private:
    void onActionTriggered();
    void onNextRegion();
    void onPreviousRegion();

    /// Moves the focus to the next (\p step 1) or previous (-1) region
    void moveToRegion(int step);

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<QWidget> m_keyWidget;
    std::vector<QPointer<QAction>> m_actions;
};

/// The card Keyboard of the right panel (edit-content-v2-keyboard; zadani.md KE.22: Ctrl+/ or View > Keyboard
/// Shortcuts): the list of the keys of the tool (PDFAppEditContentKeyboard::getKeyRows) and the link Close. Shown
/// while the card is requested (PDFAppEditContentContext::isCardRequested); the cards of the selection stay.
///
/// Threads and ownership: a part - a card of the stack (PDFAppEditContentCard). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentKeyboardCard : public PDFAppEditContentCard
{
    Q_OBJECT

private:
    using BaseClass = PDFAppEditContentCard;

public:
    explicit PDFAppEditContentKeyboardCard(PDFAppEditContentContext* context, QWidget* parent);
    virtual ~PDFAppEditContentKeyboardCard() override;

    virtual void updateCard() override;

    /// The list of the keys (tests)
    PDFKeyValueList* getList() const { return m_list.data(); }

private:
    void onCloseClicked();

    QPointer<PDFKeyValueList> m_list;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTKEYBOARD_H
