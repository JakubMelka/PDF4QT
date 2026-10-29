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

#include "pdfappcopytopagesdialog.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationoperations.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"

#include "pdfcomponentpanels.h"

#include <QEvent>
#include <QPushButton>

#include <algorithm>

namespace pdf
{

/// Texts of the Copy to Pages dialog
class PDFAppCopyToPagesDialogHelper
{
public:
    PDFAppCopyToPagesDialogHelper() = delete;

    /// "Copy the rectangle to other pages", "Copy 3 annotations to other pages"
    static QString getTitle(PDFAppAnnotationController* controller, const std::vector<PDFObjectReference>& selection)
    {
        const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
        if (selection.size() == 1 && document)
        {
            const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document->getDocument().data(), selection.front());
            return PDFAppCopyToPagesDialog::tr("Copy the %1 to other pages").arg(PDFAppAnnotationInfo::getTypeName(info.type).toLower());
        }
        return PDFAppCopyToPagesDialog::tr("Copy %n annotations to other pages", nullptr, int(selection.size()));
    }

    /// Text below the title
    static QString getText()
    {
        return PDFAppCopyToPagesDialog::tr("Each copy gets the same position and size on its page. The comment and its replies are copied too.");
    }
};

PDFAppCopyToPagesDialog::PDFAppCopyToPagesDialog(PDFAppAnnotationController* controller, QWidget* parent) :
    // badge 1024:21183: copy-to-pages in accent/blue (the badge of Information)
    BaseClass(PDFThemeIcons::Badge::Information, PDFAppCopyToPagesDialogHelper::getTitle(controller, controller ? controller->getSelectedAnnotations() : std::vector<PDFObjectReference>()),
              PDFAppCopyToPagesDialogHelper::getText(), QDialogButtonBox::NoButton, parent),
    m_controller(controller)
{
    setObjectName(QStringLiteral("copyToPagesDialog"));
    setBadgeIconName(QStringLiteral("copy-to-pages"));
    setCardWidth(CARD_WIDTH);

    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const PDFAppDocumentView* view = controller ? controller->getView() : nullptr;
    m_annotations = controller ? controller->getSelectedAnnotations() : std::vector<PDFObjectReference>();
    for (const PDFObjectReference& annotation : m_annotations)
    {
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
        if (info.isValid() && std::find(m_sourcePages.cbegin(), m_sourcePages.cend(), info.pageIndex) == m_sourcePages.cend())
        {
            m_sourcePages.push_back(info.pageIndex);
        }
    }

    // Page selection 1024:21188 (394 x 200)
    m_pageSelection = new PDFPageSelection(this);
    m_pageSelection->setObjectName(QStringLiteral("copyToPagesSelection"));
    m_pageSelection->setLabel(tr("Pages"));
    const PDFInteger pageCount = document ? qMin<PDFInteger>(document->getPageCount(), PDFPageRange::MAX_PAGE_COUNT) : 0;
    m_pageSelection->setPageCount(int(pageCount));
    if (view)
    {
        m_pageSelection->setCurrentPage(int(view->getDisplayedPageIndex()) + 1);
        QList<int> selectedPages;
        for (const PDFInteger page : view->getSelectedPages())
        {
            if (page >= 0 && page < pageCount)
            {
                selectedPages << int(page + 1);
            }
        }
        m_pageSelection->setSelectedPages(selectedPages);
        if (view->hasPageLabels())
        {
            QStringList labels;
            labels.reserve(int(pageCount));
            for (PDFInteger i = 0; i < pageCount; ++i)
            {
                labels << view->getPageLabel(i);
            }
            m_pageSelection->setPageLabels(labels);
        }
    }
    m_pageSelection->setMode(PDFPageSelection::Mode::All);
    addContent(m_pageSelection);
    connect(m_pageSelection.data(), &PDFPageSelection::resultChanged, this, &PDFAppCopyToPagesDialog::onResultChanged);

    QPushButton* cancel = addActionButton(tr("Cancel"), ActionPosition::Trailing, false);
    cancel->setObjectName(QStringLiteral("copyToPagesCancel"));
    connect(cancel, &QPushButton::clicked, this, &PDFAppCopyToPagesDialog::reject);
    m_cancelButton = cancel;
    m_copyButton = addActionButton(QString(), ActionPosition::Trailing, true);
    m_copyButton->setObjectName(QStringLiteral("copyToPagesCopy"));
    connect(m_copyButton.data(), &QPushButton::clicked, this, &PDFAppCopyToPagesDialog::onCopyClicked);
    onResultChanged();

    connect(this, &QDialog::finished, this, &PDFAppCopyToPagesDialog::onDialogFinished);
    if (controller)
    {
        // The tab (with its controller) deleted: the dialog closes
        connect(controller, &QObject::destroyed, this, &PDFAppCopyToPagesDialog::onControllerDestroyed);
    }
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppCopyToPagesDialog::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppCopyToPagesDialog::onDocumentChanged);
    }
    if (controller)
    {
        connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppCopyToPagesDialog::onDocumentChanged);
    }
}

PDFAppCopyToPagesDialog::~PDFAppCopyToPagesDialog() = default;

PDFAppCopyToPagesDialog* PDFAppCopyToPagesDialog::open(PDFAppAnnotationController* controller, QWidget* parent)
{
    if (!controller || !controller->hasAnnotationSelection() || !controller->isAnnotationEditingAllowed())
    {
        return nullptr;
    }

    PDFAppCopyToPagesDialog* dialog = new PDFAppCopyToPagesDialog(controller, parent);
    PDFAppMainWindow::prepareDialog(dialog, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->BaseClass::open();
    return dialog;
}

PDFAppAnnotationController* PDFAppCopyToPagesDialog::getController() const
{
    return m_controller.data();
}

PDFPageSelection* PDFAppCopyToPagesDialog::getPageSelection() const
{
    return m_pageSelection.data();
}

QPushButton* PDFAppCopyToPagesDialog::getCopyButton() const
{
    return m_copyButton.data();
}

std::vector<PDFInteger> PDFAppCopyToPagesDialog::getTargetPages() const
{
    std::vector<PDFInteger> pages;
    if (!m_pageSelection || (m_pageSelection->getMode() == PDFPageSelection::Mode::Range && !m_pageSelection->getRangeError().isEmpty()))
    {
        return pages;
    }

    // The page of the annotations is skipped (one page of the selection; an
    // annotation of another page skips its own page in the operation)
    const bool isOnePage = m_sourcePages.size() == 1;
    for (const int page : m_pageSelection->getResultPages())
    {
        const PDFInteger pageIndex = PDFInteger(page) - 1;
        if (!isOnePage || pageIndex != m_sourcePages.front())
        {
            pages.push_back(pageIndex);
        }
    }
    return pages;
}

void PDFAppCopyToPagesDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppCopyToPagesDialog::retranslate()
{
    // The texts of the dialog (the page selection translates its own parts)
    setTitle(PDFAppCopyToPagesDialogHelper::getTitle(m_controller, m_annotations));
    setText(PDFAppCopyToPagesDialogHelper::getText());
    if (m_pageSelection)
    {
        m_pageSelection->setLabel(tr("Pages"));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
    onResultChanged();
}

void PDFAppCopyToPagesDialog::onResultChanged()
{
    const std::vector<PDFInteger> pages = getTargetPages();
    const size_t count = pages.size();

    // The operation makes at most MAXIMUM_COPIES annotations (annotations x pages)
    const PDFAppDocument* document = m_controller ? m_controller->getDocument() : nullptr;
    const size_t copies = document ? PDFAppAnnotationOperations::getCopyToPagesCount(document->getDocument(), m_annotations, pages) : 0;
    const bool isTooMany = copies > PDFAppAnnotationOperations::MAXIMUM_COPIES;
    if (m_pageSelection)
    {
        m_pageSelection->setCustomError(isTooMany ? tr("At most %L1 annotations can be copied at once (this would make %L2). Select fewer pages or annotations.")
                                                        .arg(qulonglong(PDFAppAnnotationOperations::MAXIMUM_COPIES)).arg(qulonglong(copies))
                                                  : QString());
    }
    if (m_copyButton)
    {
        m_copyButton->setText(tr("Copy to %n Pages", nullptr, int(count)));
        m_copyButton->setEnabled(count > 0 && !m_annotations.empty() && !isTooMany);
    }
}

bool PDFAppCopyToPagesDialog::copy()
{
    PDFAppAnnotationController* controller = m_controller.data();
    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const std::vector<PDFInteger> pages = getTargetPages();
    if (!controller || !document || pages.empty() || !controller->isAnnotationEditingAllowed())
    {
        return false;
    }

    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::copyToPages(document->getDocument(), m_annotations, pages);
    if (!result)
    {
        return false;
    }

    QPointer<PDFAppCopyToPagesDialog> guard(this);
    const bool isCommitted = controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Copy annotations to pages"));
    if (guard && isCommitted)
    {
        m_isApplied = true;
        accept();
    }
    return isCommitted;
}

void PDFAppCopyToPagesDialog::onCopyClicked()
{
    copy();
}

void PDFAppCopyToPagesDialog::onDocumentChanged()
{
    // The annotations are gone or the document cannot be changed: nothing to copy
    if (m_isApplied || !isVisible())
    {
        return;
    }

    const PDFAppAnnotationController* controller = m_controller.data();
    const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    bool isAnyValid = false;
    for (const PDFObjectReference& annotation : m_annotations)
    {
        isAnyValid = isAnyValid || PDFAppAnnotationInfo::read(document && document->isReady() ? document->getDocument().data() : nullptr, annotation).isValid();
    }
    if (!isAnyValid || !controller || !controller->isAnnotationEditingAllowed())
    {
        reject();
    }
}

void PDFAppCopyToPagesDialog::onControllerDestroyed()
{
    // The sender is dying: only the own state is changed, the dialog goes from the event loop
    m_controller = nullptr;
    if (isVisible())
    {
        reject();
    }
    deleteLater();
}

void PDFAppCopyToPagesDialog::onDialogFinished(int result)
{
    Q_UNUSED(result);
    Q_EMIT dialogFinished(m_isApplied);
}

}   // namespace pdf
