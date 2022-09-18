//    Copyright (C) 2021-2022 Jakub Melka
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

#include "pdfoptimizedocumentdialog.h"
#include "ui_pdfoptimizedocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfdbgheap.h"

#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>

namespace pdfviewer
{

PDFOptimizeDocumentDialog::PDFOptimizeDocumentDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFOptimizeDocumentDialog),
    m_document(document),
    m_optimizer(pdf::PDFOptimizer::All, nullptr),
    m_optimizeButton(nullptr),
    m_optimizationInProgress(false),
    m_wasOptimized(false)
{
    ui->setupUi(this);

    auto addCheckBox = [this](QString text, pdf::PDFOptimizer::OptimizationFlag flag)
    {
        QCheckBox* checkBox = new QCheckBox(text, this);
        checkBox->setChecked(m_optimizer.getFlags().testFlag(flag));
        connect(checkBox, &QCheckBox::clicked, this, [this, flag](bool checked) { m_optimizer.setFlags(m_optimizer.getFlags().setFlag(flag, checked)); });
        ui->groupBoxLayout->addWidget(checkBox);
    };

    addCheckBox(tr("Embed (dereference) simple objects, such as int, bool, real"), pdf::PDFOptimizer::DereferenceSimpleObjects);
    addCheckBox(tr("Remove null objects from dictionary entries"), pdf::PDFOptimizer::RemoveNullObjects);
    addCheckBox(tr("Remove unused objects (objects unreachable from document root object)"), pdf::PDFOptimizer::RemoveUnusedObjects);
    addCheckBox(tr("Merge identical objects"), pdf::PDFOptimizer::MergeIdenticalObjects);
    addCheckBox(tr("Shrink object storage (squeeze free entries)"), pdf::PDFOptimizer::ShrinkObjectStorage);
    addCheckBox(tr("Recompress flate streams by maximal compression"), pdf::PDFOptimizer::RecompressFlateStreams);

    m_optimizeButton = ui->buttonBox->addButton(tr("Optimize"), QDialogButtonBox::ActionRole);

    connect(m_optimizeButton, &QPushButton::clicked, this, &PDFOptimizeDocumentDialog::onOptimizeButtonClicked);
    connect(&m_optimizer, &pdf::PDFOptimizer::optimizationStarted, this, &PDFOptimizeDocumentDialog::onOptimizationStarted);
    connect(&m_optimizer, &pdf::PDFOptimizer::optimizationProgress, this, &PDFOptimizeDocumentDialog::onOptimizationProgress);
    connect(&m_optimizer, &pdf::PDFOptimizer::optimizationFinished, this, &PDFOptimizeDocumentDialog::onOptimizationFinished);
    connect(this, &PDFOptimizeDocumentDialog::displayOptimizationInfo, this, &PDFOptimizeDocumentDialog::onDisplayOptimizationInfo);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(640, 380));
    updateUi();
    pdf::PDFWidgetUtils::style(this);
}

PDFOptimizeDocumentDialog::~PDFOptimizeDocumentDialog()
{
    Q_ASSERT(!m_optimizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFOptimizeDocumentDialog::optimize()
{
    QElapsedTimer timer;
    timer.start();

    m_optimizer.setDocument(m_document);
    m_optimizer.optimize();
    m_optimizedDocument = m_optimizer.takeOptimizedDocument();

    qreal msecsElapsed = timer.nsecsElapsed() / 1000000.0;
    timer.invalidate();

    m_optimizationInfo.msecsElapsed = msecsElapsed;
    m_optimizationInfo.bytesBeforeOptimization = pdf::PDFDocumentWriter::getDocumentFileSize(m_document);
    m_optimizationInfo.bytesAfterOptimization = pdf::PDFDocumentWriter::getDocumentFileSize(&m_optimizedDocument);
    Q_EMIT displayOptimizationInfo();
}

void PDFOptimizeDocumentDialog::onOptimizeButtonClicked()
{
    Q_ASSERT(!m_optimizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    m_optimizationInProgress = true;
    m_future = QtConcurrent::run([this]() { optimize(); });
    updateUi();
}

void PDFOptimizeDocumentDialog::onOptimizationStarted()
{
    Q_ASSERT(m_optimizationInProgress);
    ui->logTextEdit->setPlainText(tr("Optimization started!"));
}

void PDFOptimizeDocumentDialog::onOptimizationProgress(QString progressText)
{
    Q_ASSERT(m_optimizationInProgress);
    ui->logTextEdit->setPlainText(QString("%1\n%2").arg(ui->logTextEdit->toPlainText(), progressText));
}

void PDFOptimizeDocumentDialog::onOptimizationFinished()
{
    ui->logTextEdit->setPlainText(QString("%1\n%2").arg(ui->logTextEdit->toPlainText(), tr("Optimization finished!")));
    m_future.waitForFinished();
    m_optimizationInProgress = false;
    m_wasOptimized = true;
    updateUi();
}

void PDFOptimizeDocumentDialog::onDisplayOptimizationInfo()
{
    QStringList texts;
    texts << tr("Optimized in %1 msecs").arg(m_optimizationInfo.msecsElapsed);
    if (m_optimizationInfo.bytesBeforeOptimization != -1 &&
        m_optimizationInfo.bytesAfterOptimization != -1)
    {
        texts << tr("Bytes before optimization: %1").arg(m_optimizationInfo.bytesBeforeOptimization);
        texts << tr("Bytes after optimization:  %1").arg(m_optimizationInfo.bytesAfterOptimization);
        texts << tr("Bytes saved by optimization: %1").arg(m_optimizationInfo.bytesBeforeOptimization - m_optimizationInfo.bytesAfterOptimization);

        qreal ratio = 100.0;
        if (m_optimizationInfo.bytesBeforeOptimization > 0)
        {
            ratio = 100.0 * qreal(m_optimizationInfo.bytesAfterOptimization) / qreal(m_optimizationInfo.bytesBeforeOptimization);
        }

        texts << tr("Compression ratio: %1 %").arg(ratio);
    }
    ui->logTextEdit->setPlainText(QString("%1\n%2").arg(ui->logTextEdit->toPlainText(), texts.join("\n")));
}

void PDFOptimizeDocumentDialog::updateUi()
{
    for (QCheckBox* checkBox : findChildren<QCheckBox*>(QString(), Qt::FindChildrenRecursively))
    {
        checkBox->setEnabled(!m_optimizationInProgress);
    }

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_wasOptimized && !m_optimizationInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_optimizationInProgress);
    m_optimizeButton->setEnabled(!m_optimizationInProgress);
}

}   // namespace pdfviewer
