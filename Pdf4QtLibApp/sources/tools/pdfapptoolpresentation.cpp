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

#include "pdfapptoolpresentation.h"
#include "pdfapptoolcollections.h"
#include "pdfapphomewidgets.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"

#include <QCoreApplication>
#include <QObject>
#include <QWidget>

namespace pdf
{

PDFAccentHue PDFAppToolPresentation::getHue(PDFDocumentToolHue hue)
{
    switch (hue)
    {
        case PDFDocumentToolHue::Amber:
            return PDFAccentHue::Amber;
        case PDFDocumentToolHue::Blue:
            return PDFAccentHue::Blue;
        case PDFDocumentToolHue::Green:
            return PDFAccentHue::Green;
        case PDFDocumentToolHue::Red:
            return PDFAccentHue::Red;
        case PDFDocumentToolHue::Violet:
            return PDFAccentHue::Violet;
        case PDFDocumentToolHue::Teal:
            return PDFAccentHue::Teal;
    }
    return PDFAccentHue::Neutral;
}

PDFAccentHue PDFAppToolPresentation::getCategoryHue(PDFDocumentToolCategory category)
{
    const PDFDocumentToolCategoryInfo* info = PDFDocumentToolCatalog::getCategoryInfo(category);
    return info ? getHue(info->getHue()) : PDFAccentHue::Neutral;
}

QString PDFAppToolPresentation::getObjectName(const QString& toolId)
{
    return QStringLiteral("tool-") + toolId;
}

QString PDFAppToolPresentation::getAdvancedTagText()
{
    //: Tag of the tools for experts in the catalog of the tools (Set Page Boxes, Print production, Developer)
    return QCoreApplication::translate("pdf::PDFAppToolPresentation", "Advanced");
}

QString PDFAppToolPresentation::getTileDescription(const PDFDocumentToolCatalogEntry& tool)
{
    if (tool.getId() == QLatin1String("protect.sanitize"))
    {
        //: Short description of the tool Remove Hidden Data in its tile
        return QCoreApplication::translate("pdf::PDFAppToolPresentation", "Strip metadata");
    }
    return tool.getDescription();
}

PDFAppRoute PDFAppToolPresentation::getToolRoute(const QString& toolId)
{
    return PDFAppRoutes::tool(toolId);
}

void PDFAppToolPresentation::setToolData(QObject* widget, const QString& toolId, const PDFAppRoute& route)
{
    if (!widget)
    {
        return;
    }
    widget->setProperty(TOOL_PROPERTY, toolId);
    widget->setProperty(ROUTE_PROPERTY, route.toString());
}

QString PDFAppToolPresentation::getToolId(const QObject* widget)
{
    return widget ? widget->property(TOOL_PROPERTY).toString() : QString();
}

PDFAppRoute PDFAppToolPresentation::getRoute(const QObject* widget)
{
    const QString route = widget ? widget->property(ROUTE_PROPERTY).toString() : QString();
    return route.isEmpty() ? PDFAppRoute() : PDFAppRoute::parse(route);
}

PDFToolRow* PDFAppToolPresentation::createToolRow(const PDFDocumentToolCatalogEntry& tool, bool isPinned, QWidget* parent)
{
    PDFToolRow* row = new PDFToolRow(tool.getTitle(), tool.getDescription(), tool.getIconName(), getHue(tool.getHue()), parent);
    row->setObjectName(getObjectName(tool.getId()));
    row->setTag(tool.isAdvanced() ? getAdvancedTagText() : QString());
    row->setPinned(isPinned);
    setToolData(row, tool.getId(), getToolRoute(tool.getId()));
    return row;
}

PDFQuickActionTile* PDFAppToolPresentation::createQuickActionTile(const PDFDocumentToolCatalogEntry& tool, bool isPinned, QWidget* parent)
{
    PDFQuickActionTile* tile = new PDFQuickActionTile(tool.getTitle(), getTileDescription(tool), tool.getIconName(), getHue(tool.getHue()), parent);
    tile->setObjectName(getObjectName(tool.getId()));
    tile->setStarVisible(true);
    tile->setPinned(isPinned);
    setToolData(tile, tool.getId(), getToolRoute(tool.getId()));
    return tile;
}

PDFToolShortcut* PDFAppToolPresentation::createToolShortcut(const PDFDocumentToolCatalogEntry& tool, QWidget* parent)
{
    PDFToolShortcut* shortcut = new PDFToolShortcut(tool.getTitle(), tool.getIconName(), getHue(tool.getHue()), parent);
    shortcut->setObjectName(getObjectName(tool.getId()));
    shortcut->setToolTip(tool.getDescription());
    setToolData(shortcut, tool.getId(), getToolRoute(tool.getId()));
    return shortcut;
}

PDFRecentToolRow* PDFAppToolPresentation::createRecentToolRow(const PDFDocumentToolCatalogEntry& tool, qint64 lastUsed, qint64 now, QWidget* parent)
{
    PDFRecentToolRow* row = new PDFRecentToolRow(tool.getTitle(), QString(), tool.getIconName(), getHue(tool.getHue()), parent);
    PDFAppHomeFormat::setTimeText(row, PDFAppHomeFormat::TimeText::Time, lastUsed, now);
    row->setObjectName(getObjectName(tool.getId()));
    setToolData(row, tool.getId(), getToolRoute(tool.getId()));
    return row;
}

PDFCollectionRow* PDFAppToolPresentation::createCollectionRow(const PDFAppToolCollection& collection, QWidget* parent)
{
    PDFCollectionRow* row = new PDFCollectionRow(collection.name, getCollectionMetaText(collection.toolIds), parent);
    row->setObjectName(QStringLiteral("collection-") + collection.routeId);
    row->setIconName(collection.iconName);
    row->setProperty(ROUTE_PROPERTY, PDFAppRoutes::tools(QString(), QString(), collection.routeId).toString());
    return row;
}

QString PDFAppToolPresentation::getMatchReasonText(const QString& keyword)
{
    //: Why a tool was found by the search: the keyword, which matched (Q3 of step 6)
    return QCoreApplication::translate("pdf::PDFAppToolPresentation", "matches “%1”").arg(keyword);
}

QString PDFAppToolPresentation::getToolCountText(int count)
{
    if (count == 1)
    {
        return QCoreApplication::translate("pdf::PDFAppToolPresentation", "%Ln tool", nullptr, count);
    }
    return QCoreApplication::translate("pdf::PDFAppToolPresentation", "%Ln tools", nullptr, count);
}

QString PDFAppToolPresentation::getCollectionMetaText(const QStringList& toolIds)
{
    QStringList titles;
    for (const QString& toolId : toolIds)
    {
        if (const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(toolId))
        {
            titles << tool->getTitle();
        }
    }

    const QString count = getToolCountText(int(titles.size()));
    if (titles.isEmpty())
    {
        return count;
    }

    //: Meta text of a collection of tools: the number of the tools · the names of the tools
    return QCoreApplication::translate("pdf::PDFAppToolPresentation", "%1 · %2").arg(count, titles.join(QCoreApplication::translate("pdf::PDFAppToolPresentation", ", ")));
}

}   // namespace pdf
