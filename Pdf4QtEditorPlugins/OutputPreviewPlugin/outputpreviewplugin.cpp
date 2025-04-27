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

    m_outputPreviewAction = new QAction(QIcon(":/pdfplugins/outputpreview/preview.svg"), tr("&Output Preview"), this);
    m_outputPreviewAction->setObjectName("actionOutputPreview_OutputPreview");
    m_inkCoverageAction = new QAction(QIcon(":/pdfplugins/outputpreview/ink-coverage.svg"), tr("&Ink Coverage"), this);
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

QString OutputPreviewPlugin::getPluginMenuName() const
{
    return tr("Output Previe&w");
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
