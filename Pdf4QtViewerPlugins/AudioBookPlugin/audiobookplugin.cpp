//    Copyright (C) 2021 Jakub Melka
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

#include "audiobookplugin.h"

#include <QAction>
#include <QMainWindow>

namespace pdfplugin
{

AudioBookPlugin::AudioBookPlugin() :
    pdf::PDFPlugin(nullptr),
    m_createTextStreamAction(nullptr),
    m_audioTextStreamDockWidget(nullptr),
    m_audioTextStreamEditorModel(nullptr)
{

}

void AudioBookPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_createTextStreamAction = new QAction(QIcon(":/pdfplugins/audiobook/create-text-stream.svg"), tr("Create Text Stream for Audio Book"), this);
    m_createTextStreamAction->setObjectName("actionAudioBook_CreateTextStream");

    connect(m_createTextStreamAction, &QAction::triggered, this, &AudioBookPlugin::onCreateTextStreamTriggered);

    updateActions();
}

void AudioBookPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        m_textFlowEditor.clear();
        updateActions();
    }
}

std::vector<QAction*> AudioBookPlugin::getActions() const
{
    return { m_createTextStreamAction };
}

void AudioBookPlugin::onCreateTextStreamTriggered()
{
    Q_ASSERT(m_document);

    if (!m_audioTextStreamDockWidget)
    {
        m_audioTextStreamDockWidget = new AudioTextStreamEditorDockWidget(m_dataExchangeInterface->getMainWindow());
        m_audioTextStreamDockWidget->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
        m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::BottomDockWidgetArea, m_audioTextStreamDockWidget, Qt::Horizontal);
        m_audioTextStreamDockWidget->setFloating(false);

        Q_ASSERT(!m_audioTextStreamEditorModel);
        m_audioTextStreamEditorModel = new pdf::PDFDocumentTextFlowEditorModel(m_audioTextStreamDockWidget);
        m_audioTextStreamEditorModel->setEditor(&m_textFlowEditor);
        m_audioTextStreamDockWidget->setModel(m_audioTextStreamEditorModel);
    }

    m_audioTextStreamDockWidget->show();

    if (!m_textFlowEditor.isEmpty())
    {
        return;
    }

    pdf::PDFDocumentTextFlowFactory factory;
    factory.setCalculateBoundingBoxes(true);
    pdf::PDFDocumentTextFlow textFlow = factory.create(m_document, pdf::PDFDocumentTextFlowFactory::Algorithm::Auto);

    m_audioTextStreamEditorModel->beginFlowChange();
    m_textFlowEditor.setTextFlow(std::move(textFlow));
    m_audioTextStreamEditorModel->endFlowChange();
}

void AudioBookPlugin::updateActions()
{
    m_createTextStreamAction->setEnabled(m_document);
}

}   // namespace pdfplugin
