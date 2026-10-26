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

#include "pdfapptoolmenu.h"
#include "pdfapppagelayout.h"

#include <QAction>

namespace pdf
{

PDFAppToolMenu::PDFAppToolMenu(const QString& toolId, bool isPinned, const std::vector<PDFAppToolCollection>& collections, QWidget* parent) :
    BaseClass(parent),
    m_toolId(toolId),
    m_isPinned(isPinned)
{
    setObjectName(QStringLiteral("toolMenu"));

    m_openAction = addAction(tr("Open"));
    m_openAction->setObjectName(QStringLiteral("openAction"));
    connect(m_openAction.data(), &QAction::triggered, this, &PDFAppToolMenu::onOpenTriggered);

    m_pinAction = addAction(isPinned ? tr("Unpin") : tr("Pin"));
    m_pinAction->setObjectName(QStringLiteral("pinAction"));
    connect(m_pinAction.data(), &QAction::triggered, this, &PDFAppToolMenu::onPinTriggered);

    addSeparator();

    m_collectionsMenu = addMenu(tr("Add to Collection"));
    m_collectionsMenu->setObjectName(QStringLiteral("collectionsMenu"));

    int count = 0;
    bool isCustomSeparated = false;
    for (const PDFAppToolCollection& collection : collections)
    {
        if (count >= MAXIMUM_COLLECTIONS)
        {
            break;
        }

        // The custom collections below the built-in ones
        if (!collection.isBuiltin && !isCustomSeparated)
        {
            if (count > 0)
            {
                m_collectionsMenu->addSeparator();
            }
            isCustomSeparated = true;
        }

        QAction* action = m_collectionsMenu->addAction(PDFAppPageWidgets::escapeMnemonic(collection.name));
        action->setObjectName(QStringLiteral("collection-") + collection.routeId);
        action->setData(collection.routeId);
        action->setCheckable(true);
        action->setChecked(collection.toolIds.contains(toolId));
        connect(action, &QAction::triggered, this, &PDFAppToolMenu::onCollectionTriggered);
        ++count;
    }

    if (count > 0)
    {
        m_collectionsMenu->addSeparator();
    }
    m_newCollectionAction = m_collectionsMenu->addAction(tr("New Collection…"));
    m_newCollectionAction->setObjectName(QStringLiteral("newCollectionAction"));
    connect(m_newCollectionAction.data(), &QAction::triggered, this, &PDFAppToolMenu::onNewCollectionTriggered);
}

PDFAppToolMenu::~PDFAppToolMenu() = default;

QAction* PDFAppToolMenu::findCollectionAction(const QString& routeId) const
{
    if (!m_collectionsMenu)
    {
        return nullptr;
    }

    for (QAction* action : m_collectionsMenu->actions())
    {
        if (action->isCheckable() && action->data().toString() == routeId)
        {
            return action;
        }
    }
    return nullptr;
}

void PDFAppToolMenu::onOpenTriggered()
{
    const QString toolId = m_toolId;
    Q_EMIT openRequested(toolId);
}

void PDFAppToolMenu::onPinTriggered()
{
    const QString toolId = m_toolId;
    Q_EMIT pinRequested(toolId, !m_isPinned);
}

void PDFAppToolMenu::onCollectionTriggered(bool isChecked)
{
    // The triggered signal does not identify the collection: its route id is the data of the action
    const QAction* action = qobject_cast<const QAction*>(sender());
    if (!action)
    {
        return;
    }

    const QString toolId = m_toolId;
    const QString routeId = action->data().toString();
    Q_EMIT collectionToggled(toolId, routeId, isChecked);
}

void PDFAppToolMenu::onNewCollectionTriggered()
{
    const QString toolId = m_toolId;
    Q_EMIT newCollectionRequested(toolId);
}

}   // namespace pdf
