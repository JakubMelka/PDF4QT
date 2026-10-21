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
}

}   // namespace pdf
