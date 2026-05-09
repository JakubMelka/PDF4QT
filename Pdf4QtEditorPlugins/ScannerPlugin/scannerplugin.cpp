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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "scannerplugin.h"

#include "scandialog.h"
#include "scannedpdfbuilder.h"

#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"

#include <QAction>
#include <QDialog>
#include <QIcon>

namespace pdfplugin
{

ScannerPlugin::ScannerPlugin() :
    pdf::PDFPlugin(nullptr)
{

}

void ScannerPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_scanAction = new QAction(QIcon(":/pdfplugins/scannerplugin/scan.svg"), tr("&Scan Pages..."), this);
    m_scanAction->setObjectName("scannerplugin_ScanPages");

    connect(m_scanAction, &QAction::triggered, this, &ScannerPlugin::onScanTriggered);
}

std::vector<QAction*> ScannerPlugin::getActions() const
{
    return { m_scanAction };
}

QString ScannerPlugin::getPluginMenuName() const
{
    return tr("&Scanner");
}

void ScannerPlugin::onScanTriggered()
{
    ScanDialog dialog(m_widget);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::vector<ScannedPage> pages = dialog.takePages();
    if (pages.empty())
    {
        return;
    }

    if (m_document)
    {
        pdf::PDFDocumentModifier modifier(m_document);
        ScannedPdfBuilder::appendPages(modifier.getBuilder(), pages);
        modifier.markReset();

        if (modifier.finalize())
        {
            pdf::PDFModifiedDocument::ModificationFlags flags = modifier.getFlags() | pdf::PDFModifiedDocument::PreserveView;
            Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, flags));
        }
    }
    else
    {
        pdf::PDFDocumentBuilder builder;
        ScannedPdfBuilder::appendPages(&builder, pages);

        pdf::PDFDocumentPointer document(new pdf::PDFDocument(builder.build()));
        Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(document, nullptr, pdf::PDFModifiedDocument::Reset));
    }
}

}   // namespace pdfplugin
