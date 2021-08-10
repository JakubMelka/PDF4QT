﻿//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "redactplugin.h"
#include "createredacteddocumentdialog.h"

#include "pdfdrawwidget.h"
#include "pdfadvancedtools.h"
#include "pdfdocumentbuilder.h"
#include "pdfredact.h"
#include "pdfdocumentwriter.h"
#include "pdfselectpagesdialog.h"
#include "pdfcompiler.h"

#include <QAction>
#include <QMessageBox>

namespace pdfplugin
{

RedactPlugin::RedactPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionRedactRectangle(nullptr),
    m_actionRedactText(nullptr),
    m_actionRedactTextSelection(nullptr),
    m_actionRedactPage(nullptr),
    m_actionCreateRedactedDocument(nullptr)
{

}

void RedactPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionRedactRectangle = new QAction(QIcon(":/pdfplugins/redactplugin/redact-rectangle.svg"), tr("Redact Rectangle"), this);
    m_actionRedactText = new QAction(QIcon(":/pdfplugins/redactplugin/redact-text.svg"), tr("Redact Text"), this);
    m_actionRedactTextSelection = new QAction(QIcon(":/pdfplugins/redactplugin/redact-text-selection.svg"), tr("Redact Text Selection"), this);
    m_actionRedactPage = new QAction(QIcon(":/pdfplugins/redactplugin/redact-page.svg"), tr("Redact Page(s)"), this);
    m_actionCreateRedactedDocument = new QAction(QIcon(":/pdfplugins/redactplugin/redact-create-document.svg"), tr("Create Redacted Document"), this);

    m_actionRedactRectangle->setObjectName("redactplugin_RedactRectangle");
    m_actionRedactText->setObjectName("redactplugin_RedactText");
    m_actionRedactTextSelection->setObjectName("redactplugin_RedactTextSelection");
    m_actionRedactPage->setObjectName("redactplugin_RedactPage");
    m_actionCreateRedactedDocument->setObjectName("redactplugin_CreateRedactedDocument");

    m_actionRedactRectangle->setCheckable(true);
    m_actionRedactText->setCheckable(true);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    pdf::PDFCreateRedactRectangleTool* redactRectangleTool = new pdf::PDFCreateRedactRectangleTool(widget->getDrawWidgetProxy(), toolManager, m_actionRedactRectangle, this);
    pdf::PDFCreateRedactTextTool* redactTextTool = new pdf::PDFCreateRedactTextTool(widget->getDrawWidgetProxy(), toolManager, m_actionRedactText, this);

    toolManager->addTool(redactRectangleTool);
    toolManager->addTool(redactTextTool);

    connect(m_actionRedactTextSelection, &QAction::triggered, this, &RedactPlugin::onRedactTextSelectionTriggered);
    connect(m_actionRedactPage, &QAction::triggered, this, &RedactPlugin::onRedactPageTriggered);
    connect(m_actionCreateRedactedDocument, &QAction::triggered, this, &RedactPlugin::onCreateRedactedDocumentTriggered);

    updateActions();
}

void RedactPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> RedactPlugin::getActions() const
{
    return { m_actionRedactRectangle, m_actionRedactText, m_actionRedactTextSelection, m_actionRedactPage, m_actionCreateRedactedDocument };
}

void RedactPlugin::updateActions()
{
    m_actionRedactTextSelection->setEnabled(m_document);
    m_actionRedactPage->setEnabled(m_document);
    m_actionCreateRedactedDocument->setEnabled(m_document);
}

void RedactPlugin::onRedactTextSelectionTriggered()
{
    pdf::PDFTextSelection selectedText = m_dataExchangeInterface->getSelectedText();

    if (selectedText.isEmpty())
    {
        QMessageBox::information(m_widget, tr("Information"), tr("Select text via 'Advanced Search' tool, and then redact it using this tool. Select rows in 'Result' table to select particular results."));
        return;
    }

    pdf::PDFDocumentModifier modifier(m_document);

    for (auto it = selectedText.begin(); it != selectedText.end(); it = selectedText.nextPageRange(it))
    {
        const pdf::PDFTextSelectionColoredItem& item = *it;
        const pdf::PDFInteger pageIndex = item.start.pageIndex;

        pdf::PDFTextLayoutGetter textLayoutGetter = m_widget->getDrawWidgetProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);

        QPolygonF quadrilaterals;
        pdf::PDFTextSelectionPainter textSelectionPainter(&selectedText);
        QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QMatrix(), &quadrilaterals);

        if (!path.isEmpty())
        {
            pdf::PDFObjectReference page = m_document->getCatalog()->getPage(pageIndex)->getPageReference();
            modifier.getBuilder()->createAnnotationRedact(page, quadrilaterals, Qt::black);
            modifier.markAnnotationsChanged();
        }
    }

    if (modifier.finalize())
    {
        emit m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

void RedactPlugin::onRedactPageTriggered()
{
    pdf::PDFSelectPagesDialog dialog(tr("Redact Pages"), tr("Page Range to be Redacted"), m_document->getCatalog()->getPageCount(), m_widget->getDrawWidget()->getCurrentPages(), m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        std::vector<pdf::PDFInteger> selectedPages = dialog.getSelectedPages();

        if (selectedPages.empty())
        {
            // Jakub Melka: no pages are selected, just return
            return;
        }

        pdf::PDFDocumentModifier modifier(m_document);

        for (pdf::PDFInteger pageIndex : selectedPages)
        {
            const pdf::PDFPage* page = m_document->getCatalog()->getPage(pageIndex - 1);
            pdf::PDFObjectReference pageReference = page->getPageReference();
            pdf::PDFObjectReference annotation = modifier.getBuilder()->createAnnotationRedact(pageReference, page->getMediaBox(), Qt::black);
            modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
        }

        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            emit m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

void RedactPlugin::onCreateRedactedDocumentTriggered()
{
    CreateRedactedDocumentDialog dialog(getRedactedFileName(), Qt::black, m_widget);
    if (dialog.exec() == QDialog::Accepted)
    {
        pdf::PDFCMSPointer cms = m_widget->getCMSManager()->getCurrentCMS();
        pdf::PDFRedact redactProcessor(m_document,
                                       m_widget->getDrawWidgetProxy()->getFontCache(),
                                       cms.data(),
                                       m_widget->getDrawWidgetProxy()->getOptionalContentActivity(),
                                       &m_widget->getDrawWidgetProxy()->getMeshQualitySettings(),
                                       dialog.getRedactColor());

        pdf::PDFRedact::Options options;
        options.setFlag(pdf::PDFRedact::CopyTitle, dialog.isCopyingTitle());
        options.setFlag(pdf::PDFRedact::CopyMetadata, dialog.isCopyingMetadata());
        options.setFlag(pdf::PDFRedact::CopyOutline, dialog.isCopyingOutline());

        pdf::PDFDocument redactedDocument = redactProcessor.perform(options);
        pdf::PDFDocumentWriter writer(m_widget->getDrawWidgetProxy()->getProgress());
        pdf::PDFOperationResult result = writer.write(dialog.getFileName(), &redactedDocument, false);
        if (!result)
        {
            QMessageBox::critical(m_widget, tr("Error"), result.getErrorMessage());
        }
    }
}

QString RedactPlugin::getRedactedFileName() const
{
    QFileInfo fileInfo(m_dataExchangeInterface->getOriginalFileName());

    return fileInfo.path() + "/" + fileInfo.baseName() + "_REDACTED.pdf";
}

}
