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

#ifndef PDFAPPTOOLPRESENTATION_H
#define PDFAPPTOOLPRESENTATION_H

#include "pdfappglobal.h"
#include "pdfapproute.h"

#include "pdfaccenthue.h"
#include "pdfdocumenttoolcatalog.h"

#include <QString>
#include <QStringList>

class QObject;
class QWidget;

namespace pdf
{

class PDFToolRow;
class PDFQuickActionTile;
class PDFToolShortcut;
class PDFRecentToolRow;
class PDFCollectionRow;
class PDFAppToolCollection;

/// Presentation of the tools of the catalog shared by every place, which shows
/// them (plan of step 6, 4.1): the Tools page, the collections editor, the Tools
/// panel of the document, Home, the global search, What's New. The factories of
/// the components give every widget the object name "tool-<id>", its route as
/// data (getRoute), the tool id (getToolId) and the tag Advanced; the caller
/// connects their signals. Widgets only in the GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPresentation
{
public:
    PDFAppToolPresentation() = delete;

    /// Dynamic properties of a widget of a tool: its tool id and its route (text)
    static constexpr const char* TOOL_PROPERTY = "pdf4qtToolId";
    static constexpr const char* ROUTE_PROPERTY = "pdf4qtRoute";

    /// Hue of the components (PDFAccentHue) of a hue of the catalog; an invalid value is Neutral
    static PDFAccentHue getHue(PDFDocumentToolHue hue);

    /// Hue of the category (Tool category header)
    static PDFAccentHue getCategoryHue(PDFDocumentToolCategory category);

    /// Object name of the widget of a tool ("tool-pages.merge")
    static QString getObjectName(const QString& toolId);

    /// Text of the tag Advanced (Figma 272:12821)
    static QString getAdvancedTagText();

    /// Description of a tile (Quick action tile): the description of the catalog,
    /// shortened where the design shortens it (Remove Hidden Data: "Strip metadata")
    static QString getTileDescription(const PDFDocumentToolCatalogEntry& tool);

    /// Route of the activation of a tool of the catalog (/tools/{toolId})
    static PDFAppRoute getToolRoute(const QString& toolId);

    /// Sets the tool id and the route of the widget
    static void setToolData(QObject* widget, const QString& toolId, const PDFAppRoute& route);

    /// Tool id of the widget (empty = none)
    static QString getToolId(const QObject* widget);

    /// Route of the widget (an invalid route = none)
    static PDFAppRoute getRoute(const QObject* widget);

    /// Tool row of the catalog (272:12821): name, description, icon, hue, the tag
    /// Advanced, the star (\p isPinned), the route of the tool
    static PDFToolRow* createToolRow(const PDFDocumentToolCatalogEntry& tool, bool isPinned, QWidget* parent);

    /// Quick action tile with the star shown (Pinned tools of 272:12876)
    static PDFQuickActionTile* createQuickActionTile(const PDFDocumentToolCatalogEntry& tool, bool isPinned, QWidget* parent);

    /// Tool shortcut of the Tools panel of the document (196:3650)
    static PDFToolShortcut* createToolShortcut(const PDFDocumentToolCatalogEntry& tool, QWidget* parent);

    /// Recent tool row: the time of the last use formatted by PDFAppHomeFormat
    /// ("2 minutes ago", "Yesterday 16:20", "Sep 25, 2026") against \p now; the
    /// owner updates it by PDFAppHomeFormat::updateTimeTexts (O25, minute ticker)
    static PDFRecentToolRow* createRecentToolRow(const PDFDocumentToolCatalogEntry& tool, qint64 lastUsed, qint64 now, QWidget* parent);

    /// Collection row (272:12943): the icon, the name and the meta text, the route
    /// /tools?collection=<routeId> (owner decision Q1)
    static PDFCollectionRow* createCollectionRow(const PDFAppToolCollection& collection, QWidget* parent);

    /// Reason of a match by a keyword ("matches “combine”", Q3)
    static QString getMatchReasonText(const QString& keyword);

    /// Number of the tools ("1 tool", "3 tools"; the number in the language of the user interface)
    static QString getToolCountText(int count);

    /// Meta text of a collection: "3 tools · Compress, Remove Hidden Data, Protect
    /// with Password" (the titles in the order of the collection, unknown ids left
    /// out), "0 tools" for an empty one
    static QString getCollectionMetaText(const QStringList& toolIds);
};

}   // namespace pdf

#endif // PDFAPPTOOLPRESENTATION_H
