//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "outputpreviewplugin.h"
#include "outputpreviewdialog.h"
#include "inkcoveragedialog.h"
#include "pdfdrawwidget.h"

#include <QAction>

namespace pdfplugin
{

OutputPreviewPlugin::OutputPreviewPlugin() :
    pdf::PDFPlugin(nullptr),
    m_outputPreviewAction(nullptr),
    m_inkCoverageAction(nullptr)
{

}

void OutputPreviewPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_outputPreviewAction = new QAction(QIcon(":/pdfplugins/outputpreview/preview.svg"), tr("Output Preview"), this);
    m_outputPreviewAction->setObjectName("actionOutputPreview_OutputPreview");
    m_inkCoverageAction = new QAction(QIcon(":/pdfplugins/outputpreview/ink-coverage.svg"), tr("Ink Coverage"), this);
    m_inkCoverageAction->setObjectName("actionOutputPreview_InkCoverage");

    connect(m_outputPreviewAction, &QAction::triggered, this, &OutputPreviewPlugin::onOutputPreviewTriggered);
    connect(m_inkCoverageAction, &QAction::triggered, this, &OutputPreviewPlugin::onInkCoverageTriggered);

    updateActions();
}

void OutputPreviewPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> OutputPreviewPlugin::getActions() const
{
    return { m_outputPreviewAction, m_inkCoverageAction };
}

void OutputPreviewPlugin::onOutputPreviewTriggered()
{
    OutputPreviewDialog dialog(m_document, m_widget, m_widget);
    dialog.exec();
}

void OutputPreviewPlugin::onInkCoverageTriggered()
{
    InkCoverageDialog dialog(m_document, m_widget, m_widget);
    dialog.exec();
}

void OutputPreviewPlugin::updateActions()
{
    m_outputPreviewAction->setEnabled(m_widget && m_document);
    m_inkCoverageAction->setEnabled(m_widget && m_document);
}

}
