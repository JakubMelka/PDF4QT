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

#include "pdfappdocumentsearch.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentsearchcontroller.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentsearchpanel.h"
#include "pdfappdocumentsignatures.h"
#include "pdfappdocumentspeech.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"

#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>

namespace pdf
{

/// Factory of the panel of the search results (no tab of the sidebar)
class PDFAppDocumentSearchPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override { return PDFAppDocumentPanelIds::search(); }
    virtual int getOrder() const override { return 90; }
    virtual bool hasSidebarTab() const override { return false; }
    virtual QString getTitle() const override { return PDFAppDocumentSearchPanel::tr("Search results"); }
    virtual QString getIconName() const override { return QStringLiteral("search"); }
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override { return new PDFAppDocumentSearchPanel(parent); }
};

/// Factory of the panel Speech
class PDFAppDocumentSpeechPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override { return PDFAppDocumentPanelIds::speech(); }
    virtual int getOrder() const override { return 50; }
    virtual QString getTitle() const override { return PDFAppDocumentSpeechPanel::tr("Speech"); }
    virtual QString getIconName() const override { return QStringLiteral("volume-2"); }
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override { return new PDFAppDocumentSpeechPanel(parent); }
};

/// Factory of the panel Signatures: its tab is shown for a document with
/// signature fields (Figma 190:619: tabs without content are hidden)
class PDFAppDocumentSignaturesPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override { return PDFAppDocumentPanelIds::signatures(); }
    virtual int getOrder() const override { return 60; }
    virtual QString getTitle() const override { return PDFAppDocumentSignaturesPanel::tr("Signatures"); }
    virtual QString getIconName() const override { return QStringLiteral("pen-tool"); }
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override { return new PDFAppDocumentSignaturesPanel(parent); }

    virtual bool isAvailable(const PDFAppDocument* document) const override
    {
        if (!document)
        {
            return false;
        }
        if (!document->getSignatureResults().empty())
        {
            return true;
        }

        PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(document);
        PDFAppDocumentSignatureVerifier* verifier = handler ? handler->getSignatureVerifier() : nullptr;
        return verifier && verifier->hasSignatures();
    }
};

void PDFAppDocumentPanelRegistry::registerSearchPanels(PDFAppDocumentPanelRegistry* registry)
{
    if (!registry)
    {
        return;
    }

    registry->registerFactory(std::make_shared<PDFAppDocumentSearchPanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppDocumentSpeechPanelFactory>());
    registry->registerFactory(std::make_shared<PDFAppDocumentSignaturesPanelFactory>());
}

PDFAppDocumentSearchCommandHandler::PDFAppDocumentSearchCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::Search, tab)
{
    PDFAppDocument* document = getDocument();
    PDFAppDocumentView* view = getView();

    m_indexer = new PDFAppDocumentTextIndexer(document, this);
    m_searchController = new PDFAppDocumentSearchController(document, view, m_indexer, this);
    m_signatureVerifier = new PDFAppDocumentSignatureVerifier(document, this);
    connect(m_searchController, &PDFAppDocumentSearchController::stateChanged, this, &PDFAppDocumentSearchCommandHandler::onSearchStateChanged);

    if (view)
    {
        connect(view, &PDFAppDocumentView::panelChanged, this, &PDFAppDocumentSearchCommandHandler::onPanelChanged);
        view->addStatusItem(new PDFAppSignatureStatusItem(m_signatureVerifier, view), PDFAppDocumentStatusBar::Area::Trailing);
    }

    connectSearchField();
}

PDFAppDocumentSearchCommandHandler::~PDFAppDocumentSearchCommandHandler()
{
    if (m_searchField)
    {
        m_searchField->removeEventFilter(this);
        m_searchField->disconnect(this);
    }

    // The services go before the handler: their workers are canceled, nobody waits.
    // Their signals are not wanted any more (the handler is not whole).
    for (QObject* service : std::initializer_list<QObject*>{ m_speechController.data(), m_searchController.data(), m_signatureVerifier.data(), m_indexer.data() })
    {
        if (service)
        {
            service->disconnect(this);
        }
    }
    delete m_speechController.data();
    delete m_searchController.data();
    delete m_signatureVerifier.data();
    delete m_indexer.data();
}

void PDFAppDocumentSearchCommandHandler::connectSearchField()
{
    // The field of the toolbar (package B, Figma search 193:878); the search
    // gives it its behavior
    PDFAppDocumentView* view = getView();
    QLineEdit* field = (view && view->getToolbar()) ? view->getToolbar()->getSearchField() : nullptr;
    if (!field || field == m_searchField)
    {
        return;
    }

    m_searchField = field;
    connect(field, &QLineEdit::returnPressed, this, &PDFAppDocumentSearchCommandHandler::onFieldReturnPressed);
    field->installEventFilter(this);
}

PDFAppDocumentTextIndexer* PDFAppDocumentSearchCommandHandler::getIndexer() const
{
    return m_indexer.data();
}

PDFAppDocumentSearchController* PDFAppDocumentSearchCommandHandler::getSearchController() const
{
    return m_searchController.data();
}

PDFAppDocumentSignatureVerifier* PDFAppDocumentSearchCommandHandler::getSignatureVerifier() const
{
    return m_signatureVerifier.data();
}

PDFAppDocumentSpeechController* PDFAppDocumentSearchCommandHandler::getSpeechController(bool create)
{
    if (!m_speechController && create)
    {
        m_speechController = new PDFAppDocumentSpeechController(getDocument(), getView(), m_indexer, this);
        connect(m_speechController, &PDFAppDocumentSpeechController::stateChanged, this, &PDFAppDocumentSearchCommandHandler::onSpeechStateChanged);
    }
    return m_speechController.data();
}

QLineEdit* PDFAppDocumentSearchCommandHandler::getSearchField() const
{
    return m_searchField.data();
}

void PDFAppDocumentSearchCommandHandler::execute(const QString& commandId)
{
    if (!isEnabled(commandId) || !m_searchController)
    {
        return;
    }

    connectSearchField();

    if (commandId == PDFAppCommands::find())
    {
        if (m_searchField && m_searchField->isVisible())
        {
            m_searchField->setFocus(Qt::ShortcutFocusReason);
            m_searchField->selectAll();
        }
        else if (PDFAppDocumentView* view = getView())
        {
            view->showPanel(PDFAppDocumentPanelIds::search());
        }
    }
    else if (commandId == PDFAppCommands::findNext() || commandId == PDFAppCommands::findPrevious())
    {
        // Without results, F3 searches the text of the field
        if (m_searchController->getResultCount() == 0)
        {
            const QString text = m_searchField ? m_searchField->text() : m_searchController->getQuery();
            if (!text.isEmpty() && (text != m_searchController->getQuery() || m_searchController->getState() == PDFAppDocumentSearchController::State::Idle))
            {
                startSearch(text);
            }
            return;
        }

        if (commandId == PDFAppCommands::findNext())
        {
            m_searchController->goToNext();
        }
        else
        {
            m_searchController->goToPrevious();
        }
    }
    else if (commandId == PDFAppCommands::readAloud())
    {
        PDFAppDocumentSpeechController* speech = getSpeechController(true);
        if (speech->isReading())
        {
            speech->stop();
        }
        else
        {
            speech->play();
        }
    }
}

bool PDFAppDocumentSearchCommandHandler::isEnabled(const QString& commandId) const
{
    // The services are gone, while the handler is being destroyed (the window
    // asks the states, when the operations of the tab end)
    if (!BaseClass::isEnabled(commandId) || !m_searchController || !m_indexer)
    {
        return false;
    }

    if (commandId == PDFAppCommands::findNext() || commandId == PDFAppCommands::findPrevious())
    {
        const bool hasText = !m_searchController->getQuery().isEmpty() || (m_searchField && !m_searchField->text().isEmpty());
        return m_searchController->getResultCount() > 0 || hasText;
    }

    return handles(commandId);
}

bool PDFAppDocumentSearchCommandHandler::isChecked(const QString& commandId) const
{
    if (commandId == PDFAppCommands::readAloud())
    {
        return m_speechController && m_speechController->isReading();
    }
    return false;
}

void PDFAppDocumentSearchCommandHandler::startSearch(const QString& text)
{
    connectSearchField();

    // The text of the route or of the field is untrusted: it is bounded by the
    // search itself (PDFDocumentTextSearch refuses a too long text)
    if (m_searchField && m_searchField->text() != text)
    {
        m_searchField->setText(text);
    }

    if (text.isEmpty())
    {
        closeSearch();
        return;
    }

    QPointer<PDFAppDocumentSearchCommandHandler> guard(this);
    if (PDFAppDocumentView* view = getView(); view && m_searchController)
    {
        view->showPanel(PDFAppDocumentPanelIds::search());
    }
    if (!guard || !m_searchController)
    {
        return;
    }

    m_searchController->search(text);
}

void PDFAppDocumentSearchCommandHandler::closeSearch()
{
    QPointer<PDFAppDocumentSearchCommandHandler> guard(this);
    if (m_searchController)
    {
        m_searchController->clear();
    }
    if (!guard)
    {
        return;
    }

    if (m_searchField)
    {
        m_searchField->clear();
    }

    PDFAppDocumentView* view = getView();
    if (view && view->getCurrentPanelId() == PDFAppDocumentPanelIds::search())
    {
        view->showPanel(m_previousPanelId.isEmpty() ? PDFAppDocumentPanelIds::thumbnails() : m_previousPanelId);
        if (view->getPdfWidget())
        {
            view->getPdfWidget()->setFocus(Qt::OtherFocusReason);
        }
    }
}

QString PDFAppDocumentSearchCommandHandler::getSearchText() const
{
    return m_searchText;
}

void PDFAppDocumentSearchCommandHandler::applyRouteQuery(PDFAppDocumentTab* tab, const QString& query)
{
    PDFAppDocumentSearchCommandHandler* handler = findHandler(tab);
    if (handler && !query.isEmpty() && query != handler->getSearchText())
    {
        handler->startSearch(query);
    }
}

QString PDFAppDocumentSearchCommandHandler::getRouteQuery(const PDFAppDocumentTab* tab)
{
    const PDFAppDocumentSearchCommandHandler* handler = findHandler(tab);
    return handler ? handler->getSearchText() : QString();
}

PDFAppDocumentSearchCommandHandler* PDFAppDocumentSearchCommandHandler::findHandler(const QObject* object)
{
    for (const QObject* item = object; item; item = item->parent())
    {
        if (const PDFAppDocumentTab* tab = qobject_cast<const PDFAppDocumentTab*>(item))
        {
            return qobject_cast<PDFAppDocumentSearchCommandHandler*>(tab->findHandler(PDFAppCommands::find()));
        }
    }
    return nullptr;
}

bool PDFAppDocumentSearchCommandHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_searchField && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier)
        {
            closeSearch();
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppDocumentSearchCommandHandler::onFieldReturnPressed()
{
    if (!m_searchField)
    {
        return;
    }

    // Enter with the text of the shown results goes to the next result
    const QString text = m_searchField->text();
    if (!m_searchController)
    {
        return;
    }
    if (!text.isEmpty() && text == m_searchController->getQuery())
    {
        if (m_searchController->getResultCount() > 0)
        {
            m_searchController->goToNext();
            return;
        }

        // The text is being searched: Enter does not start it again
        const PDFAppDocumentSearchController::State state = m_searchController->getState();
        if (state == PDFAppDocumentSearchController::State::Indexing || state == PDFAppDocumentSearchController::State::Searching)
        {
            return;
        }
    }
    startSearch(text);
}

void PDFAppDocumentSearchCommandHandler::onPanelChanged(const QString& panelId)
{
    if (panelId != PDFAppDocumentPanelIds::search() && !panelId.isEmpty())
    {
        m_previousPanelId = panelId;
    }
}

void PDFAppDocumentSearchCommandHandler::onSearchStateChanged()
{
    const QString text = m_searchController ? m_searchController->getQuery() : QString();
    QPointer<PDFAppDocumentSearchCommandHandler> guard(this);
    if (text != m_searchText)
    {
        m_searchText = text;
        Q_EMIT searchTextChanged();
    }
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSearchCommandHandler::onSpeechStateChanged()
{
    Q_EMIT stateChanged();
}

}   // namespace pdf
