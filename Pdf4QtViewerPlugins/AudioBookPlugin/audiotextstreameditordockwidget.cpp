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

#include "audiotextstreameditordockwidget.h"
#include "ui_audiotextstreameditordockwidget.h"

#include "pdfwidgetutils.h"

#include <QToolBar>
#include <QLineEdit>

namespace pdfplugin
{

AudioTextStreamEditorDockWidget::AudioTextStreamEditorDockWidget(AudioTextStreamActions actions,
                                                                 QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::AudioTextStreamEditorDockWidget),
    m_model(nullptr),
    m_toolBar(nullptr),
    m_selectionTextEdit(nullptr)
{
    ui->setupUi(this);
    ui->textStreamTableView->horizontalHeader()->setStretchLastSection(true);
    ui->textStreamTableView->horizontalHeader()->setMinimumSectionSize(pdf::PDFWidgetUtils::scaleDPI_x(this, 85));

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    m_toolBar = new QToolBar(tr("Audio Book Actions"), this);
    m_toolBar->setIconSize(iconSize);
    m_selectionTextEdit = new QLineEdit(m_toolBar);
    m_selectionTextEdit->setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 125));
    m_selectionTextEdit->setMaximumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 400));
    ui->verticalLayout->insertWidget(0, m_toolBar);

    m_toolBar->addActions({ actions.actionSynchronizeFromTableToGraphics,
                            actions.actionSynchronizeFromGraphicsToTable });
    m_toolBar->addSeparator();
    m_toolBar->addActions({ actions.actionActivateSelection,
                            actions.actionDeactivateSelection });
    m_toolBar->addSeparator();

    m_toolBar->addAction(actions.actionSelectByRectangle);
    m_toolBar->addWidget(m_selectionTextEdit);
    m_toolBar->addActions({ actions.actionSelectByContainedText,
                            actions.actionSelectByRegularExpression,
                            actions.actionSelectByPageList });

    m_toolBar->addSeparator();
    m_toolBar->addActions({ actions.actionRestoreOriginalText,
                            actions.actionMoveSelectionUp,
                            actions.actionMoveSelectionDown });
    m_toolBar->addSeparator();
    m_toolBar->addAction(actions.actionCreateAudioBook);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(300, 150)));
}

AudioTextStreamEditorDockWidget::~AudioTextStreamEditorDockWidget()
{
    delete ui;
}

pdf::PDFDocumentTextFlowEditorModel* AudioTextStreamEditorDockWidget::getModel() const
{
    return m_model;
}

void AudioTextStreamEditorDockWidget::setModel(pdf::PDFDocumentTextFlowEditorModel* model)
{
    m_model = model;
    ui->textStreamTableView->setModel(m_model);
}

QString AudioTextStreamEditorDockWidget::getSelectionText() const
{
    return m_selectionTextEdit->text();
}

void AudioTextStreamEditorDockWidget::clearSelectionText()
{
    m_selectionTextEdit->clear();
}

} // namespace pdfplugin
