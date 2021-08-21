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
#include "pdfdrawwidget.h"
#include "pdfwidgettool.h"
#include "pdfutils.h"

#include <QAction>
#include <QMainWindow>
#include <QMessageBox>
#include <QRegularExpression>

namespace pdfplugin
{

AudioBookPlugin::AudioBookPlugin() :
    pdf::PDFPlugin(nullptr),
    m_actionCreateTextStream(nullptr),
    m_actionSynchronizeFromTableToGraphics(nullptr),
    m_actionSynchronizeFromGraphicsToTable(nullptr),
    m_actionActivateSelection(nullptr),
    m_actionDeactivateSelection(nullptr),
    m_actionSelectByRectangle(nullptr),
    m_actionSelectByContainedText(nullptr),
    m_actionSelectByRegularExpression(nullptr),
    m_actionSelectByPageList(nullptr),
    m_actionRestoreOriginalText(nullptr),
    m_actionMoveSelectionUp(nullptr),
    m_actionMoveSelectionDown(nullptr),
    m_actionCreateAudioBook(nullptr),
    m_audioTextStreamDockWidget(nullptr),
    m_audioTextStreamEditorModel(nullptr)
{

}

void AudioBookPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_actionCreateTextStream = new QAction(QIcon(":/pdfplugins/audiobook/create-text-stream.svg"), tr("Create Text Stream for Audio Book"), this);
    m_actionCreateTextStream->setObjectName("actionAudioBook_CreateTextStream");
    connect(m_actionCreateTextStream, &QAction::triggered, this, &AudioBookPlugin::onCreateTextStreamTriggered);

    m_actionSynchronizeFromTableToGraphics = new QAction(QIcon(":/pdfplugins/audiobook/synchronize-from-table-to-graphics.svg"), tr("Synchronize Selection from Table to Graphics"), this);
    m_actionSynchronizeFromTableToGraphics->setObjectName("actionAudioBook_SynchronizeFromTableToGraphics");
    m_actionSynchronizeFromTableToGraphics->setCheckable(true);

    m_actionSynchronizeFromGraphicsToTable = new QAction(QIcon(":/pdfplugins/audiobook/synchronize-from-graphics-to-table.svg"), tr("Synchronize Selection from Graphics to Table"), this);
    m_actionSynchronizeFromGraphicsToTable->setObjectName("actionAudioBook_SynchronizeFromGraphicsToTable");
    m_actionSynchronizeFromGraphicsToTable->setCheckable(true);

    m_actionActivateSelection = new QAction(QIcon(":/pdfplugins/audiobook/activate-selection.svg"), tr("Activate Selection"), this);
    m_actionActivateSelection->setObjectName("actionAudioBook_ActivateSelection");
    connect(m_actionActivateSelection, &QAction::triggered, this, &AudioBookPlugin::onActivateSelection);

    m_actionDeactivateSelection = new QAction(QIcon(":/pdfplugins/audiobook/deactivate-selection.svg"), tr("Deactivate Selection"), this);
    m_actionDeactivateSelection->setObjectName("actionAudioBook_DeactivateSelection");
    connect(m_actionDeactivateSelection, &QAction::triggered, this, &AudioBookPlugin::onDeactivateSelection);

    m_actionSelectByRectangle = new QAction(QIcon(":/pdfplugins/audiobook/select-by-rectangle.svg"), tr("Select by Rectangle"), this);
    m_actionSelectByRectangle->setObjectName("actionAudioBook_SelectByRectangle");
    connect(m_actionSelectByRectangle, &QAction::triggered, this, &AudioBookPlugin::onSelectByRectangle);

    m_actionSelectByContainedText = new QAction(QIcon(":/pdfplugins/audiobook/select-by-contained-text.svg"), tr("Select by Contained Text"), this);
    m_actionSelectByContainedText->setObjectName("actionAudioBook_SelectByContainedText");
    connect(m_actionSelectByContainedText, &QAction::triggered, this, &AudioBookPlugin::onSelectByContainedText);

    m_actionSelectByRegularExpression = new QAction(QIcon(":/pdfplugins/audiobook/select-by-regular-expression.svg"), tr("Select by Regular Expression"), this);
    m_actionSelectByRegularExpression->setObjectName("actionAudioBook_SelectByRegularExpression");
    connect(m_actionSelectByRegularExpression, &QAction::triggered, this, &AudioBookPlugin::onSelectByRegularExpression);

    m_actionSelectByPageList = new QAction(QIcon(":/pdfplugins/audiobook/select-by-page-list.svg"), tr("Select by Page List"), this);
    m_actionSelectByPageList->setObjectName("actionAudioBook_SelectByPageList");
    connect(m_actionSelectByPageList, &QAction::triggered, this, &AudioBookPlugin::onSelectByPageList);

    m_actionRestoreOriginalText = new QAction(QIcon(":/pdfplugins/audiobook/restore-original-text.svg"), tr("Restore Original Text"), this);
    m_actionRestoreOriginalText->setObjectName("actionAudioBook_RestoreOriginalText");

    m_actionMoveSelectionUp = new QAction(QIcon(":/pdfplugins/audiobook/move-selection-up.svg"), tr("Move Selection Up"), this);
    m_actionMoveSelectionUp->setObjectName("actionAudioBook_MoveSelectionUp");

    m_actionMoveSelectionDown = new QAction(QIcon(":/pdfplugins/audiobook/move-selection-down.svg"), tr("Move Selection Down"), this);
    m_actionMoveSelectionDown->setObjectName("actionAudioBook_MoveSelectionDown");

    m_actionCreateAudioBook = new QAction(QIcon(":/pdfplugins/audiobook/create-audio-book.svg"), tr("Create Audio Book"), this);
    m_actionCreateAudioBook->setObjectName("actionAudioBook_CreateAudioBook");

    updateActions();
}

void AudioBookPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        if (m_audioTextStreamEditorModel)
        {
            m_audioTextStreamEditorModel->beginFlowChange();
        }
        m_textFlowEditor.clear();
        if (m_audioTextStreamEditorModel)
        {
            m_audioTextStreamEditorModel->endFlowChange();
        }

        updateActions();
    }
}

std::vector<QAction*> AudioBookPlugin::getActions() const
{
    return { m_actionCreateTextStream,
             m_actionSynchronizeFromTableToGraphics,
             m_actionSynchronizeFromGraphicsToTable,
             m_actionCreateAudioBook };
}

void AudioBookPlugin::onCreateTextStreamTriggered()
{
    Q_ASSERT(m_document);

    if (!m_audioTextStreamDockWidget)
    {
        AudioTextStreamActions actions;
        actions.actionCreateTextStream = m_actionCreateTextStream;
        actions.actionSynchronizeFromTableToGraphics = m_actionSynchronizeFromTableToGraphics;
        actions.actionSynchronizeFromGraphicsToTable = m_actionSynchronizeFromGraphicsToTable;
        actions.actionActivateSelection = m_actionActivateSelection;
        actions.actionDeactivateSelection = m_actionDeactivateSelection;
        actions.actionSelectByRectangle = m_actionSelectByRectangle;
        actions.actionSelectByContainedText = m_actionSelectByContainedText;
        actions.actionSelectByRegularExpression = m_actionSelectByRegularExpression;
        actions.actionSelectByPageList = m_actionSelectByPageList;
        actions.actionRestoreOriginalText = m_actionRestoreOriginalText;
        actions.actionMoveSelectionUp = m_actionMoveSelectionUp;
        actions.actionMoveSelectionDown = m_actionMoveSelectionDown;
        actions.actionCreateAudioBook = m_actionCreateAudioBook;

        m_audioTextStreamDockWidget = new AudioTextStreamEditorDockWidget(actions, m_dataExchangeInterface->getMainWindow());
        m_audioTextStreamDockWidget->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
        m_dataExchangeInterface->getMainWindow()->addDockWidget(Qt::BottomDockWidgetArea, m_audioTextStreamDockWidget, Qt::Horizontal);
        m_audioTextStreamDockWidget->setFloating(false);

        Q_ASSERT(!m_audioTextStreamEditorModel);
        m_audioTextStreamEditorModel = new pdf::PDFDocumentTextFlowEditorModel(m_audioTextStreamDockWidget);
        m_audioTextStreamEditorModel->setEditor(&m_textFlowEditor);
        m_audioTextStreamDockWidget->setModel(m_audioTextStreamEditorModel);
        connect(m_audioTextStreamEditorModel, &pdf::PDFDocumentTextFlowEditorModel::modelReset, this, &AudioBookPlugin::updateActions);
        connect(m_audioTextStreamEditorModel, &pdf::PDFDocumentTextFlowEditorModel::dataChanged, this, &AudioBookPlugin::updateActions);
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

void AudioBookPlugin::onActivateSelection()
{
    m_audioTextStreamEditorModel->setSelectionActivated(true);
}

void AudioBookPlugin::onDeactivateSelection()
{
    m_audioTextStreamEditorModel->setSelectionActivated(false);
}

void AudioBookPlugin::onSelectByRectangle()
{
    m_widget->getToolManager()->pickRectangle(std::bind(&AudioBookPlugin::onRectanglePicked, this, std::placeholders::_1, std::placeholders::_2));
}

void AudioBookPlugin::onSelectByContainedText()
{
    QString text = m_audioTextStreamDockWidget->getSelectionText();
    m_audioTextStreamDockWidget->clearSelectionText();

    if (!text.isEmpty())
    {
        m_audioTextStreamEditorModel->selectByContainedText(text);
    }
    else
    {
        QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by text, because text is empty."));
    }
}

void AudioBookPlugin::onSelectByRegularExpression()
{
    QString pattern = m_audioTextStreamDockWidget->getSelectionText();
    m_audioTextStreamDockWidget->clearSelectionText();

    if (!pattern.isEmpty())
    {
        QRegularExpression expression(pattern);

        if (expression.isValid())
        {
            m_audioTextStreamEditorModel->selectByRegularExpression(expression);
        }
        else
        {
            QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Regular expression is not valid. %1").arg(expression.errorString()));
        }
    }
    else
    {
        QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by regular expression, because regular expression definition is empty."));
    }
}

void AudioBookPlugin::onSelectByPageList()
{
    QString pageIndicesText = m_audioTextStreamDockWidget->getSelectionText();
    m_audioTextStreamDockWidget->clearSelectionText();

    if (!pageIndicesText.isEmpty())
    {
        QString errorMessage;
        auto pageIndices = pdf::PDFClosedIntervalSet::parse(1, m_document->getCatalog()->getPageCount(), pageIndicesText, &errorMessage);

        if (errorMessage.isEmpty())
        {
            m_audioTextStreamEditorModel->selectByPageIndices(pageIndices);
        }
        else
        {
            QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by page indices, because page indices are invalid. %1").arg(errorMessage));
        }
    }
    else
    {
        QMessageBox::critical(m_audioTextStreamDockWidget, tr("Error"), tr("Cannot select items by page indices, because page indices are empty."));
    }
}

void AudioBookPlugin::onRectanglePicked(pdf::PDFInteger pageIndex, QRectF rectangle)
{
    Q_UNUSED(pageIndex);
    m_audioTextStreamEditorModel->selectByRectangle(rectangle);
}

void AudioBookPlugin::updateActions()
{
    m_actionCreateTextStream->setEnabled(m_document);
    m_actionSynchronizeFromTableToGraphics->setEnabled(true);
    m_actionSynchronizeFromGraphicsToTable->setEnabled(true);
    m_actionActivateSelection->setEnabled(!m_textFlowEditor.isSelectionEmpty());
    m_actionDeactivateSelection->setEnabled(!m_textFlowEditor.isSelectionEmpty());
    m_actionSelectByRectangle->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionSelectByContainedText->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionSelectByRegularExpression->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionSelectByPageList->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionRestoreOriginalText->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionMoveSelectionUp->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionMoveSelectionDown->setEnabled(!m_textFlowEditor.isEmpty());
    m_actionCreateAudioBook->setEnabled(!m_textFlowEditor.isEmpty());
}

}   // namespace pdfplugin
