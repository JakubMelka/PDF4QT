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

#include "pdfapprestoredfilebar.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"

#include <QEvent>

namespace pdf
{

PDFAppRestoredFileBar::PDFAppRestoredFileBar(PDFAppDocumentTab* tab, QObject* parent) :
    BaseClass(parent),
    m_document(tab ? tab->getDocument() : nullptr),
    m_view(tab ? tab->getView() : nullptr)
{
    if (m_document)
    {
        connect(m_document.data(), &PDFAppDocument::stateChanged, this, &PDFAppRestoredFileBar::updateBar);
        connect(m_document.data(), &PDFAppDocument::originalDataChanged, this, &PDFAppRestoredFileBar::updateBar);
        connect(m_document.data(), &PDFAppDocument::documentChanged, this, &PDFAppRestoredFileBar::onDocumentChanged);
    }

    if (m_view)
    {
        connect(m_view.data(), &PDFAppDocumentView::infoBarActionTriggered, this, &PDFAppRestoredFileBar::onInfoBarActionTriggered);

        // The texts of the bar in a new language
        m_view->installEventFilter(this);
    }

    updateBar();
}

PDFAppRestoredFileBar::~PDFAppRestoredFileBar()
{
    if (m_view)
    {
        m_view->removeEventFilter(this);
    }
}

bool PDFAppRestoredFileBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view.data() && event->type() == QEvent::LanguageChange)
    {
        updateBar();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppRestoredFileBar::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // A reload has no change of the state; a change of the user does not change the file
    if (modified.hasReset())
    {
        updateBar();
    }
}

void PDFAppRestoredFileBar::onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId)
{
    // The close button of the bar (an empty action): the bar has gone, the view has hidden it
    if (infoBarId == getBarId() && actionId.isEmpty() && m_document)
    {
        m_isClosed = true;
        m_closedData = m_document->getOriginalData();
    }
}

void PDFAppRestoredFileBar::updateBar()
{
    PDFAppDocumentView* view = m_view.data();
    const PDFAppDocument* document = m_document.data();
    if (!view)
    {
        return;
    }

    const bool isReady = document && document->isReady();
    const PDFAppDocumentRestoration restoration = isReady ? document->getRestoration() : PDFAppDocumentRestoration::None;

    // The file of the closed bar: other bytes are a new reason to tell it
    if (isReady && m_isClosed && !m_closedData.isSharedWith(document->getOriginalData()) && m_closedData != document->getOriginalData())
    {
        m_isClosed = false;
        m_closedData.clear();
    }

    if (restoration == PDFAppDocumentRestoration::None || m_isClosed)
    {
        // hideInfoBar deletes the bar later and it runs no code of the application
        view->hideInfoBar(getBarId());
        return;
    }

    PDFAppInfoBarRequest request;
    request.id = getBarId();
    request.tone = PDFAppInfoBarTone::Warning;
    if (restoration == PDFAppDocumentRestoration::OldestContent)
    {
        request.title = tr("This file is damaged and you may be seeing an old version");
        request.text = tr("Its latest changes cannot be read and are not shown. You can save it only as a copy.");
    }
    else
    {
        request.title = tr("This file is damaged");
        request.text = tr("PDF4QT repaired it to open it, so something may be missing or differ from the original.");
    }

    // showInfoBar creates or fills the widget of the bar, no signal is emitted
    const PDFAppInfoBarRequest* shown = view->findInfoBar(request.id);
    if (!shown || !(*shown == request))
    {
        view->showInfoBar(request);
    }
}

}   // namespace pdf

#include "moc_pdfapprestoredfilebar.cpp"
