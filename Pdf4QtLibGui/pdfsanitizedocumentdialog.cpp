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

#include "pdfsanitizedocumentdialog.h"
#include "ui_pdfsanitizedocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"

#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFSanitizeDocumentDialog::PDFSanitizeDocumentDialog(const pdf::PDFDocument* document, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFSanitizeDocumentDialog),
    m_document(document),
    m_sanitizer(pdf::PDFDocumentSanitizer::All, nullptr),
    m_sanitizeButton(nullptr),
    m_sanitizationInProgress(false),
    m_wasSanitized(false)
{
    ui->setupUi(this);

    auto addCheckBox = [this](QString text, pdf::PDFDocumentSanitizer::SanitizationFlag flag)
    {
        QCheckBox* checkBox = new QCheckBox(text, this);
        checkBox->setChecked(m_sanitizer.getFlags().testFlag(flag));
        connect(checkBox, &QCheckBox::clicked, this, [this, flag](bool checked) { m_sanitizer.setFlags(m_sanitizer.getFlags().setFlag(flag, checked)); });
        ui->groupBoxLayout->addWidget(checkBox);
    };

    addCheckBox(tr("Remove document info"), pdf::PDFDocumentSanitizer::DocumentInfo);
    addCheckBox(tr("Remove all metadata"), pdf::PDFDocumentSanitizer::Metadata);
    addCheckBox(tr("Remove outline"), pdf::PDFDocumentSanitizer::Outline);
    addCheckBox(tr("Remove file attachments"), pdf::PDFDocumentSanitizer::FileAttachments);
    addCheckBox(tr("Remove embedded search index"), pdf::PDFDocumentSanitizer::EmbeddedSearchIndex);
    addCheckBox(tr("Remove comments and other markup annotations"), pdf::PDFDocumentSanitizer::MarkupAnnotations);
    addCheckBox(tr("Remove page thumbnails"), pdf::PDFDocumentSanitizer::PageThumbnails);

    m_sanitizeButton = ui->buttonBox->addButton(tr("Sanitize"), QDialogButtonBox::ActionRole);

    connect(m_sanitizeButton, &QPushButton::clicked, this, &PDFSanitizeDocumentDialog::onSanitizeButtonClicked);
    connect(&m_sanitizer, &pdf::PDFDocumentSanitizer::sanitizationStarted, this, &PDFSanitizeDocumentDialog::onSanitizationStarted);
    connect(&m_sanitizer, &pdf::PDFDocumentSanitizer::sanitizationProgress, this, &PDFSanitizeDocumentDialog::onSanitizationProgress);
    connect(&m_sanitizer, &pdf::PDFDocumentSanitizer::sanitizationFinished, this, &PDFSanitizeDocumentDialog::onSanitizationFinished);
    connect(this, &PDFSanitizeDocumentDialog::displaySanitizationInfo, this, &PDFSanitizeDocumentDialog::onDisplaySanitizationInfo);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(640, 380));
    updateUi();
    pdf::PDFWidgetUtils::style(this);
}

PDFSanitizeDocumentDialog::~PDFSanitizeDocumentDialog()
{
    Q_ASSERT(!m_sanitizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFSanitizeDocumentDialog::sanitize()
{
    QElapsedTimer timer;
    timer.start();

    m_sanitizer.setDocument(m_document);
    m_sanitizer.sanitize();
    m_sanitizedDocument = m_sanitizer.takeSanitizedDocument();

    qreal msecsElapsed = timer.nsecsElapsed() / 1000000.0;
    timer.invalidate();

    m_sanitizationInfo.msecsElapsed = msecsElapsed;
    m_sanitizationInfo.bytesBeforeSanitization = pdf::PDFDocumentWriter::getDocumentFileSize(m_document);
    m_sanitizationInfo.bytesAfterSanitization = pdf::PDFDocumentWriter::getDocumentFileSize(&m_sanitizedDocument);
    Q_EMIT displaySanitizationInfo();
}

void PDFSanitizeDocumentDialog::onSanitizeButtonClicked()
{
    Q_ASSERT(!m_sanitizationInProgress);
    Q_ASSERT(!m_future.isRunning());

    m_sanitizationInProgress = true;
    m_future = QtConcurrent::run([this]() { sanitize(); });
    updateUi();
}

void PDFSanitizeDocumentDialog::onSanitizationStarted()
{
    Q_ASSERT(m_sanitizationInProgress);
    ui->logTextEdit->setPlainText(tr("Sanitization started!"));
}

void PDFSanitizeDocumentDialog::onSanitizationProgress(QString progressText)
{
    Q_ASSERT(m_sanitizationInProgress);
    ui->logTextEdit->setPlainText(QString("%1\n%2").arg(ui->logTextEdit->toPlainText(), progressText));
}

void PDFSanitizeDocumentDialog::onSanitizationFinished()
{
    ui->logTextEdit->setPlainText(QString("%1\n%2").arg(ui->logTextEdit->toPlainText(), tr("Sanitization finished!")));
    m_future.waitForFinished();
    m_sanitizationInProgress = false;
    m_wasSanitized = true;
    updateUi();
}

void PDFSanitizeDocumentDialog::onDisplaySanitizationInfo()
{
    QStringList texts;
    texts << tr("Sanitized in %1 msecs").arg(m_sanitizationInfo.msecsElapsed);
    if (m_sanitizationInfo.bytesBeforeSanitization != -1 &&
        m_sanitizationInfo.bytesAfterSanitization != -1)
    {
        texts << tr("Bytes before sanitization: %1").arg(m_sanitizationInfo.bytesBeforeSanitization);
        texts << tr("Bytes after sanitization:  %1").arg(m_sanitizationInfo.bytesAfterSanitization);
        texts << tr("Bytes saved by sanitization: %1").arg(m_sanitizationInfo.bytesBeforeSanitization - m_sanitizationInfo.bytesAfterSanitization);

        qreal ratio = 100.0;
        if (m_sanitizationInfo.bytesBeforeSanitization > 0)
        {
            ratio = 100.0 * qreal(m_sanitizationInfo.bytesAfterSanitization) / qreal(m_sanitizationInfo.bytesBeforeSanitization);
        }

        texts << tr("Compression ratio: %1 %").arg(ratio);
    }
    ui->logTextEdit->setPlainText(QString("%1\n%2").arg(ui->logTextEdit->toPlainText(), texts.join("\n")));
}

void PDFSanitizeDocumentDialog::updateUi()
{
    for (QCheckBox* checkBox : findChildren<QCheckBox*>(QString(), Qt::FindChildrenRecursively))
    {
        checkBox->setEnabled(!m_sanitizationInProgress);
    }

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_wasSanitized && !m_sanitizationInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_sanitizationInProgress);
    m_sanitizeButton->setEnabled(!m_sanitizationInProgress);
}

}   // namespace pdfviewer
