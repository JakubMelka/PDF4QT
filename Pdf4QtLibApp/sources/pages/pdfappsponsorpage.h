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

#ifndef PDFAPPSPONSORPAGE_H
#define PDFAPPSPONSORPAGE_H

#include "pdfappglobal.h"
#include "pdfappinfowidgets.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"

#include <QPointer>
#include <QPushButton>

namespace pdf
{

/// Page Sponsor (pdf4qt-sponsor 122:2652): a static page - the hero with the
/// facts and the buttons Sponsor on GitHub, Star on GitHub, Rate on Microsoft
/// Store and the one-time contribution, what the support makes possible (four
/// highlight cards), the ways to help (link rows), the note of the author, what
/// the time goes into now and the promise, that PDF4QT stays free. Every link is
/// an external https address.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSponsorPage : public PDFAppContentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppContentPage;

public:
    explicit PDFAppSponsorPage(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppSponsorPage() override;

    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual QString getTitle() const override;
    virtual QString getHeaderTitle() const override;
    virtual QString getHeaderSubtitle() const override;

    /// Parts of the page (tests)
    QWidget* getHero() const { return m_hero; }
    QPushButton* getSponsorButton() const { return m_sponsorButton; }
    QPushButton* getStarButton() const { return m_starButton; }
    QPushButton* getStoreButton() const { return m_storeButton; }
    PDFTextLink* getOneTimeLink() const { return m_oneTimeLink; }
    QList<PDFHighlightCard*> getHighlightCards() const;
    QList<PDFLinkRow*> getLinkRows() const;
    QList<PDFStatusRow*> getPriorityRows() const;
    QWidget* getPromise() const { return m_promise; }
    PDFAppColumns* getColumns() const { return m_columns; }

private:
    QWidget* createHero(QWidget* parent);
    QWidget* createPossibilities(QWidget* parent);
    QWidget* createWaysToHelp(QWidget* parent);
    QWidget* createAuthorNote(QWidget* parent);
    QWidget* createPriorities(QWidget* parent);
    QWidget* createPromise(QWidget* parent);

    QPointer<PDFAppColumns> m_columns;
    QPointer<QWidget> m_hero;
    QPointer<QPushButton> m_sponsorButton;
    QPointer<QPushButton> m_starButton;
    QPointer<QPushButton> m_storeButton;
    QPointer<PDFTextLink> m_oneTimeLink;
    QList<QPointer<PDFHighlightCard>> m_highlightCards;
    QList<QPointer<PDFLinkRow>> m_linkRows;
    QList<QPointer<PDFStatusRow>> m_priorityRows;
    QPointer<QWidget> m_promise;
};

/// Factory of the page Sponsor
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSponsorPageFactory : public PDFAppPageFactory
{
public:
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSPONSORPAGE_H
