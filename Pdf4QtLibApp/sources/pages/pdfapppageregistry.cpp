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

#include "pdfapppageregistry.h"
#include "pdfapproutetable.h"
#include "pdfappaboutpage.h"
#include "pdfappsponsorpage.h"
#include "pdfappsupportpage.h"
#include "pdfapphelppage.h"
#include "pdfapphomepage.h"
#include "pdfappopenpage.h"
#include "pdfapprecentpage.h"
#include "pdfappsearchpage.h"
#include "pdfappwhatsnewpage.h"
#include "pdfappdocumentpage.h"
#include "pdfappdocumentproperties.h"
#include "pdfappsettingspage.h"
#include "pdfapptoolspage.h"
#include "pdfapptoolcollectionspage.h"
#include "pdfapptoolpageregistry.h"
#include "pdfappaipage.h"

namespace pdf
{

void PDFAppPageRegistry::registerFactories(PDFAppRouteTable& table)
{
    // Each page adds one block here: table.setFactory(pattern, std::make_shared<Factory>())
    Q_UNUSED(table);

    // About, Support and Sponsor
    table.setFactory(QStringLiteral("/about"), std::make_shared<PDFAppAboutPageFactory>());
    table.setFactory(QStringLiteral("/support"), std::make_shared<PDFAppSupportPageFactory>());
    table.setFactory(QStringLiteral("/sponsor"), std::make_shared<PDFAppSponsorPageFactory>());

    // What's New, the release notes and the user manual
    table.setFactory(QStringLiteral("/whats-new"), std::make_shared<PDFAppWhatsNewPageFactory>());
    table.setFactory(QStringLiteral("/whats-new/{version}"), std::make_shared<PDFAppWhatsNewPageFactory>());
    table.setFactory(QStringLiteral("/whats-new/{version}/notes"), std::make_shared<PDFAppReleaseNotesPageFactory>());
    table.setFactory(QStringLiteral("/help/{topic}"), std::make_shared<PDFAppHelpPageFactory>());

    // The page of all results of the global search
    table.setFactory(QStringLiteral("/search"), std::make_shared<PDFAppSearchPageFactory>());

    // Home, Open (also the tab New tab) and Recent
    table.setFactory(QStringLiteral("/home"), std::make_shared<PDFAppHomePageFactory>());
    table.setFactory(QStringLiteral("/open"), std::make_shared<PDFAppOpenPageFactory>());
    table.setFactory(QStringLiteral("/recent"), std::make_shared<PDFAppRecentPageFactory>());

    // The document tab (step 3): the route adapter of the view and the properties
    // (package F; Page Not Found until the page exists)
    table.setFactory(QStringLiteral("/document/{documentId}"), std::make_shared<PDFAppDocumentPageFactory>());
    table.setFactory(QStringLiteral("/document/{documentId}/properties/{section}"), std::make_shared<PDFAppDocumentPropertiesPageFactory>());

    // Settings (step 5): the sections and the filter /settings?q=
    table.setFactory(QStringLiteral("/settings"), std::make_shared<PDFAppSettingsPageFactory>());
    table.setFactory(QStringLiteral("/settings/{section}"), std::make_shared<PDFAppSettingsPageFactory>());

    // The Tools catalog (step 6): the catalog with its filters, the collections and
    // the pages of the tools - a tool has a page only, when PDFAppToolPageRegistry
    // has one (phase 2), otherwise Page Not Found
    table.setFactory(QStringLiteral("/tools"), std::make_shared<PDFAppToolsPageFactory>());
    table.setFactory(QStringLiteral("/tools/collections"), std::make_shared<PDFAppToolCollectionsPageFactory>());
    table.setFactory(QStringLiteral("/tools/collections/new"), std::make_shared<PDFAppToolCollectionsPageFactory>());
    table.setFactory(QStringLiteral("/tools/collections/{collectionId}"), std::make_shared<PDFAppToolCollectionsPageFactory>());
    table.setFactory(QStringLiteral("/tools/{toolId}"), std::make_shared<PDFAppToolRoutePageFactory>(PDFAppToolPageKind::Start));
    table.setFactory(QStringLiteral("/document/{documentId}/tool/{toolId}"), std::make_shared<PDFAppToolRoutePageFactory>(PDFAppToolPageKind::Document));

    // The AI page (step 7): the MCP server, its connection, permissions, assistants and activity
    table.setFactory(QStringLiteral("/ai"), std::make_shared<PDFAppAiPageFactory>());
}

}   // namespace pdf
