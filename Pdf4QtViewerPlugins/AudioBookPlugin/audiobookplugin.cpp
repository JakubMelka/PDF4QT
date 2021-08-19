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

namespace pdfplugin
{

AudioBookPlugin::AudioBookPlugin() :
    pdf::PDFPlugin(nullptr),
    m_createTextStreamAction(nullptr)
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

    if (!m_textFlowEditor.isEmpty())
    {
        return;
    }

    pdf::PDFDocumentTextFlowFactory factory;
    factory.setCalculateBoundingBoxes(true);
    pdf::PDFDocumentTextFlow textFlow = factory.create(m_document, pdf::PDFDocumentTextFlowFactory::Algorithm::Auto);
    m_textFlowEditor.setTextFlow(std::move(textFlow));
}

void AudioBookPlugin::updateActions()
{
    m_createTextStreamAction->setEnabled(m_document);
}

}   // namespace pdfplugin
