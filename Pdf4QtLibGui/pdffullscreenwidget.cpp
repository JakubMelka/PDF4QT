// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdffullscreenwidget.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QCloseEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QVBoxLayout>

namespace pdfviewer
{

PDFFullscreenWidget::PDFFullscreenWidget(const pdf::PDFCMSManager* cmsManager, pdf::RendererEngine engine, QWidget* parent) :
    QDialog(parent),
    m_layout(new QVBoxLayout(this)),
    m_pdfWidget(new pdf::PDFWidget(cmsManager, engine, this))
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setStyleSheet(QStringLiteral("background-color: black;"));
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addWidget(m_pdfWidget);

    QShortcut* toggleShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(toggleShortcut, &QShortcut::activated, this, &PDFFullscreenWidget::exitRequested);

    QShortcut* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escapeShortcut, &QShortcut::activated, this, &PDFFullscreenWidget::exitRequested);

    auto addNavigationShortcut = [this](const QKeySequence& sequence, pdf::PDFDrawWidgetProxy::Operation operation)
    {
        QShortcut* shortcut = new QShortcut(sequence, this);
        connect(shortcut, &QShortcut::activated, this, [this, operation]()
        {
            m_pdfWidget->getDrawWidgetProxy()->performOperation(operation);
        });
    };

    addNavigationShortcut(QKeySequence(Qt::Key_PageDown), pdf::PDFDrawWidgetProxy::NavigateNextPage);
    addNavigationShortcut(QKeySequence(Qt::Key_Down), pdf::PDFDrawWidgetProxy::NavigateNextPage);
    addNavigationShortcut(QKeySequence(Qt::Key_Right), pdf::PDFDrawWidgetProxy::NavigateNextPage);
    addNavigationShortcut(QKeySequence(Qt::Key_Enter), pdf::PDFDrawWidgetProxy::NavigateNextPage);
    addNavigationShortcut(QKeySequence(Qt::Key_Return), pdf::PDFDrawWidgetProxy::NavigateNextPage);

    addNavigationShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Enter), pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
    addNavigationShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
    addNavigationShortcut(QKeySequence(Qt::Key_PageUp), pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
    addNavigationShortcut(QKeySequence(Qt::Key_Up), pdf::PDFDrawWidgetProxy::NavigatePreviousPage);
    addNavigationShortcut(QKeySequence(Qt::Key_Left), pdf::PDFDrawWidgetProxy::NavigatePreviousPage);

    addNavigationShortcut(QKeySequence(Qt::Key_Home), pdf::PDFDrawWidgetProxy::NavigateDocumentStart);
    addNavigationShortcut(QKeySequence(Qt::Key_End), pdf::PDFDrawWidgetProxy::NavigateDocumentEnd);
}

void PDFFullscreenWidget::setDocument(const pdf::PDFModifiedDocument& document, std::vector<pdf::PDFSignatureVerificationResult> signatureVerificationResult)
{
    m_pdfWidget->setDocument(document, std::move(signatureVerificationResult));
}

void PDFFullscreenWidget::closeEvent(QCloseEvent* event)
{
    Q_EMIT exitRequested();
    event->ignore();
}

}   // namespace pdfviewer
