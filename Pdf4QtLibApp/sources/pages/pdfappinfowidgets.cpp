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

#include "pdfappinfowidgets.h"
#include "pdfappworkspace.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdfpreviewpainter.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QBoxLayout>
#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QtMath>

namespace pdf
{

/// Helpers of the widgets of the info pages
class PDFAppInfoWidgetsHelper
{
public:
    PDFAppInfoWidgetsHelper() = delete;

    static QUrl url(const char* text) { return QUrl(QString::fromLatin1(text), QUrl::StrictMode); }
};

QUrl PDFAppProjectLinks::getWebsite()
{
    return PDFAppInfoWidgetsHelper::url("https://jakubmelka.github.io/");
}

QUrl PDFAppProjectLinks::getRepository()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/JakubMelka/PDF4QT");
}

QUrl PDFAppProjectLinks::getDiscussions()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/JakubMelka/PDF4QT/discussions");
}

QUrl PDFAppProjectLinks::getNewIssue()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/JakubMelka/PDF4QT/issues/new");
}

QUrl PDFAppProjectLinks::getPullRequests()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/JakubMelka/PDF4QT/pulls");
}

QUrl PDFAppProjectLinks::getAuthorProfile()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/JakubMelka");
}

QUrl PDFAppProjectLinks::getSponsors()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/sponsors/JakubMelka");
}

QUrl PDFAppProjectLinks::getOneTimeSponsorship()
{
    return PDFAppInfoWidgetsHelper::url("https://github.com/sponsors/JakubMelka?frequency=one-time");
}

QUrl PDFAppProjectLinks::getMicrosoftStore()
{
    // Product page without the language of the store (the store chooses it by the browser)
    return PDFAppInfoWidgetsHelper::url("https://apps.microsoft.com/detail/9nr60gq66fcd");
}

QUrl PDFAppProjectLinks::getYouTubeChannel()
{
    return PDFAppInfoWidgetsHelper::url("https://www.youtube.com/@czechwisecoder");
}

QUrl PDFAppProjectLinks::getLicense()
{
    return PDFAppFooter::getLicenseUrl();
}

}   // namespace pdf
