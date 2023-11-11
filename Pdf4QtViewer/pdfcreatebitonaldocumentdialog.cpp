//    Copyright (C) 2023 Jakub Melka
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

#include "pdfcreateBitonaldocumentdialog.h"
#include "ui_pdfcreateBitonaldocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfdbgheap.h"

#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>

namespace pdfviewer
{

PDFCreateBitonalDocumentDialog::PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateBitonalDocumentDialog),
    m_document(document),
    m_createBitonalDocumentButton(nullptr),
    m_conversionInProgress(false),
    m_processed(false)
{
    ui->setupUi(this);

    m_createBitonalDocumentButton = ui->buttonBox->addButton(tr("Process"), QDialogButtonBox::ActionRole);
    connect(m_createBitonalDocumentButton, &QPushButton::clicked, this, &PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked);
    connect(ui->automaticThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);
    connect(ui->manualThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(640, 380));
    updateUi();
    pdf::PDFWidgetUtils::style(this);
}

PDFCreateBitonalDocumentDialog::~PDFCreateBitonalDocumentDialog()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFCreateBitonalDocumentDialog::createBitonalDocument()
{

}

void PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    m_conversionInProgress = true;
    m_future = QtConcurrent::run([this]() { createBitonalDocument(); });
    updateUi();
}

void PDFCreateBitonalDocumentDialog::updateUi()
{
    ui->thresholdEditBox->setEnabled(ui->manualThresholdRadioButton->isChecked());

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_processed && !m_conversionInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_conversionInProgress);
    m_createBitonalDocumentButton->setEnabled(!m_conversionInProgress);
}

}   // namespace pdfviewer
