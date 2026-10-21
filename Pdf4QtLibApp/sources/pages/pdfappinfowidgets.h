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

#ifndef PDFAPPINFOWIDGETS_H
#define PDFAPPINFOWIDGETS_H

#include "pdfappglobal.h"
#include "pdfapppage.h"
#include "pdfapppagelayout.h"
#include "pdfappworkspace.h"

#include "pdfthemecolors.h"
#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"

#include <QAbstractButton>
#include <QLayout>
#include <QList>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QWidget>

#include <optional>

class QBoxLayout;
class QScrollArea;

namespace pdf
{

/// Addresses of the project used by the pages About, Support and Sponsor. Every
/// address is an external https address (the navigator opens only those). The
/// addresses, which the repository does not know (the YouTube channel, the
/// listing in Microsoft Store), are searches of the site until the owner gives
/// the real address (report of step 2).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppProjectLinks
{
public:
    PDFAppProjectLinks() = delete;

    static QUrl getWebsite();
    static QUrl getRepository();
    static QUrl getDiscussions();
    static QUrl getNewIssue();
    static QUrl getPullRequests();
    static QUrl getAuthorProfile();
    static QUrl getSponsors();
    static QUrl getOneTimeSponsorship();
    static QUrl getMicrosoftStore();
    static QUrl getYouTubeChannel();

    /// Address of the license of the application (as the footer)
    static QUrl getLicense();
};

}   // namespace pdf

#endif // PDFAPPINFOWIDGETS_H
