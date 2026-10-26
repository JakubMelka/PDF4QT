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

#ifndef PDFAPPTOOLMENU_H
#define PDFAPPTOOLMENU_H

#include "pdfappglobal.h"
#include "pdfapptoolcollections.h"

#include <QMenu>
#include <QPointer>

#include <vector>

class QAction;

namespace pdf
{

/// Context menu of a tool of the Tools page (a tool row, a pinned tile, a recent
/// tool; tools.md 9.7 - the menu key or Shift+F10 too; a missing frame of Figma,
/// built from the menus of 1019:18817): Open, Pin / Unpin and Add to collection ▸
/// with the collections (built-in first, then the custom ones; the collections,
/// which hold the tool, checked - a click adds or removes the tool) and New
/// collection… The menu only says what was chosen (its signals); the page writes
/// it. The texts of the collections are data: an ampersand is no mnemonic.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMenu : public QMenu
{
    Q_OBJECT

private:
    using BaseClass = QMenu;

public:
    /// Most collections listed in the submenu (the others are reached by Manage)
    static constexpr int MAXIMUM_COLLECTIONS = 50;

    explicit PDFAppToolMenu(const QString& toolId, bool isPinned, const std::vector<PDFAppToolCollection>& collections, QWidget* parent);
    virtual ~PDFAppToolMenu() override;

    const QString& getToolId() const { return m_toolId; }

    QAction* getOpenAction() const { return m_openAction; }
    QAction* getPinAction() const { return m_pinAction; }
    QMenu* getCollectionsMenu() const { return m_collectionsMenu; }
    QAction* getNewCollectionAction() const { return m_newCollectionAction; }

    /// Action of the collection in the submenu (nullptr = none)
    QAction* findCollectionAction(const QString& routeId) const;

Q_SIGNALS:
    void openRequested(const QString& toolId);
    void pinRequested(const QString& toolId, bool isPinned);

    /// The tool is added to (\p isIncluded) or removed from the collection
    void collectionToggled(const QString& toolId, const QString& routeId, bool isIncluded);

    void newCollectionRequested(const QString& toolId);

private:
    void onOpenTriggered();
    void onPinTriggered();
    void onCollectionTriggered(bool isChecked);
    void onNewCollectionTriggered();

    QString m_toolId;
    bool m_isPinned = false;
    QPointer<QAction> m_openAction;
    QPointer<QAction> m_pinAction;
    QPointer<QMenu> m_collectionsMenu;
    QPointer<QAction> m_newCollectionAction;
};

}   // namespace pdf

#endif // PDFAPPTOOLMENU_H
