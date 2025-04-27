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
    m_toolBar->addAction(actions.actionClear);

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

QTableView* AudioTextStreamEditorDockWidget::getTextStreamView() const
{
    return ui->textStreamTableView;
}

QString AudioTextStreamEditorDockWidget::getSelectionText() const
{
    return m_selectionTextEdit->text();
}

void AudioTextStreamEditorDockWidget::clearSelectionText()
{
    m_selectionTextEdit->clear();
}

void AudioTextStreamEditorDockWidget::goToIndex(size_t index)
{
    QModelIndex modelIndex = ui->textStreamTableView->model()->index(int(index), 0);
    ui->textStreamTableView->scrollTo(modelIndex);
}

} // namespace pdfplugin
