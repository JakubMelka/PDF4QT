// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfappdocumentsave.h"
#include "pdfappautorefresh.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttab.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappprint.h"
#include "pdfappsendmail.h"
#include "pdfappthumbnailer.h"
#include "pdfrecentrepository.h"

#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfmessagebox.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// Helpers of the saving
class PDFAppDocumentSaveHelper
{
public:
    PDFAppDocumentSaveHelper() = delete;

    /// Counts the signed signature fields (a functor for PDFForm::apply)
    class SignatureCounter
    {
    public:
        explicit SignatureCounter(const PDFDocument* document, int* count) :
            m_document(document),
            m_count(count)
        {

        }

        void operator()(const PDFFormField* field) const
        {
            if (field && field->getFieldType() == PDFFormField::FieldType::Signature && !m_document->getObject(field->getValue()).isNull())
            {
                ++(*m_count);
            }
        }

    private:
        const PDFDocument* m_document;
        int* m_count;
    };

    /// Width of the message boxes of a document tab (958:28033)
    static constexpr int MESSAGE_BOX_WIDTH = 440;
};

PDFAppDocumentFileWriter::Result PDFAppDocumentFileWriter::write(Request request)
{
    Result result;
    result.version = request.version;
    result.isIncremental = request.isIncremental;
    result.requestedFilePath = request.filePath;

    if (request.document.isNull() || request.filePath.isEmpty() || (request.isOriginalData && request.originalData.isEmpty()))
    {
        result.errorMessage = tr("There is no document to save.");
        return result;
    }

    try
    {
        // The document is written into the memory first: its bytes become the
        // original data of the document (the next incremental save, the automatic
        // refresh compares the file with them)
        QByteArray data;
        if (request.isOriginalData)
        {
            // The shown document is a display version (the paged layout of an XFA
            // form): the file keeps the original PDF, byte for byte
            result.isIncremental = false;
            data = request.originalData;
        }
        else
        {
            QBuffer buffer(&data);
            if (!buffer.open(QIODevice::WriteOnly))
            {
                result.errorMessage = tr("The document cannot be written into the memory.");
                return result;
            }

            PDFDocumentWriter writer(nullptr);
            const PDFOperationResult writeResult = request.isIncremental ? writer.writeIncrementalUpdate(&buffer, request.originalData, request.document.data())
                                                                         : writer.write(&buffer, request.document.data());
            buffer.close();
            if (!writeResult)
            {
                result.errorMessage = writeResult.getErrorMessage();
                return result;
            }
        }

        if (data.isEmpty() || data.size() > MAXIMUM_FILE_SIZE)
        {
            result.errorMessage = tr("The document is too large to be saved.");
            return result;
        }

        // The Mark of the Web of a document from the internet (Windows) is read before
        // the file is replaced: the replacement by a temporary file has no stream
        // of the old file, the saved file (also of an attachment opened from the
        // memory, O18) keeps the mark
        const QByteArray markOfTheWeb = PDFAppAttachments::readMarkOfTheWeb(request.markSourceFilePath);

        // A temporary file renamed over the target: a failure leaves the old file. A
        // program, which reads the file at the moment (a thumbnail, an indexer of the
        // system), blocks the replacement on Windows for a short time: it is retried.
        if (!writeFile(request.filePath, data, &result.errorMessage))
        {
            return result;
        }

        if (!markOfTheWeb.isEmpty())
        {
            // Best effort: the document is saved also without its mark
            PDFAppAttachments::writeMarkOfTheWeb(request.filePath, markOfTheWeb);
        }

        result.filePath = QFileInfo(request.filePath).canonicalFilePath();
        if (result.filePath.isEmpty())
        {
            result.filePath = QFileInfo(request.filePath).absoluteFilePath();
        }
        result.data = std::move(data);
        result.isSaved = true;
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
    }
    catch (const std::bad_alloc&)
    {
        result.errorMessage = tr("There is not enough memory to save the document.");
    }

    return result;
}

bool PDFAppDocumentFileWriter::writeFile(const QString& filePath, const QByteArray& data, QString* errorMessage)
{
    for (int attempt = 0; attempt < WRITE_ATTEMPTS; ++attempt)
    {
        if (attempt > 0)
        {
            QThread::msleep(WRITE_RETRY_DELAY);
        }

        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly))
        {
            *errorMessage = file.errorString();
            if (!QFileInfo(filePath).absoluteDir().exists())
            {
                // A missing folder does not appear by waiting
                return false;
            }
            continue;
        }

        if (file.write(data) != data.size())
        {
            *errorMessage = file.errorString();
            file.cancelWriting();
            return false;
        }

        if (file.commit())
        {
            errorMessage->clear();
            return true;
        }
        *errorMessage = file.errorString();
    }
    return false;
}

int PDFAppDocumentFileWriter::getSignatureCount(const PDFDocument* document)
{
    if (!document)
    {
        return 0;
    }

    int count = 0;
    try
    {
        const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
        form.apply(PDFAppDocumentSaveHelper::SignatureCounter(document, &count));
    }
    catch (const PDFException&)
    {
        // A damaged form has no signatures, which could be kept
        return 0;
    }
    return count;
}

bool PDFAppDocumentFileWriter::isWritable(const QString& filePath)
{
    if (filePath.isEmpty())
    {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (fileInfo.exists())
    {
        return fileInfo.isFile() && fileInfo.isWritable();
    }

    const QFileInfo folderInfo(fileInfo.absolutePath());
    return folderInfo.isDir();
}

PDFAppDocumentSaver::PDFAppDocumentSaver(PDFAppDocumentTab* tab, QObject* parent) :
    BaseClass(parent),
    m_tab(tab),
    m_document(tab ? tab->getDocument() : nullptr)
{
    if (m_document)
    {
        // A document read again from the disk (a reload, another file) asks again
        // before its signatures are saved incrementally
        connect(m_document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentSaver::onDocumentStateChanged);
    }
}

PDFAppDocumentSaver::~PDFAppDocumentSaver()
{
    // The worker keeps its snapshot and ends by itself; nothing is delivered
    if (m_watcher)
    {
        m_watcher->disconnect(this);
    }

    if (m_operation)
    {
        m_operation->finish();
    }

    if (PDFMessageBox* dialog = m_dialog.data())
    {
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

PDFMessageBox* PDFAppDocumentSaver::getDialog() const
{
    return m_dialog.data();
}

PDFAppDocument* PDFAppDocumentSaver::getDocument() const
{
    return m_document.data();
}

QWidget* PDFAppDocumentSaver::getDialogParent() const
{
    return m_tab ? m_tab->window() : nullptr;
}

bool PDFAppDocumentSaver::save()
{
    PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return false;
    }

    if (!isRunning())
    {
        start(false);
    }
    return true;
}

bool PDFAppDocumentSaver::saveAs()
{
    PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return false;
    }

    if (!isRunning())
    {
        start(true);
    }
    return true;
}

bool PDFAppDocumentSaver::canSave() const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady() || isRunning() || document->isLocked())
    {
        return false;
    }

    if (document->getFilePath().isEmpty())
    {
        // A copy, which has no file yet, is saved by Save as
        return true;
    }

    // An open editor of a form field is committed by the save (step 4, package D)
    return (document->isModified() || document->hasPendingEdits()) && PDFAppDocumentFileWriter::isWritable(document->getFilePath());
}

bool PDFAppDocumentSaver::canSaveAs() const
{
    const PDFAppDocument* document = getDocument();
    return document && document->isReady() && !isRunning();
}

QString PDFAppDocumentSaver::getProposedFilePath() const
{
    const PDFAppDocument* document = getDocument();
    if (!document)
    {
        return QString();
    }

    if (!document->getFilePath().isEmpty())
    {
        return document->getFilePath();
    }

    // A copy without a file: its title in the documents of the user
    QString name = document->getTitle();
    if (!name.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
    {
        name += QStringLiteral(".pdf");
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return folder.isEmpty() ? name : QDir(folder).absoluteFilePath(name);
}

void PDFAppDocumentSaver::start(bool isSaveAs)
{
    PDFAppDocument* document = getDocument();
    if (!document)
    {
        return;
    }

    // An open editor of a form field is saved too (step 4, package D)
    QPointer<PDFAppDocumentSaver> guard(this);
    document->commitPendingEdits();
    if (!guard)
    {
        return;
    }
    document = getDocument();
    if (!document || !document->isReady())
    {
        finish(false);
        return;
    }

    const PDFDocumentPointer pdfDocument = document->getDocument();
    // A display version of an XFA form is written from its original bytes, which
    // keep its signatures (step 8): no incremental update, no question
    const int signatureCount = !document->getOriginalData().isEmpty() && !document->isPagedXFA() ? PDFAppDocumentFileWriter::getSignatureCount(pdfDocument.data()) : 0;
    const bool isIncremental = signatureCount > 0;

    if (isSaveAs || document->getFilePath().isEmpty())
    {
        // Save as: a signed document keeps its signatures in the new file as well
        chooseFileAndWrite(isIncremental);
        return;
    }

    if (isIncremental && !m_isSignedSaveConfirmed)
    {
        showSignedDialog(signatureCount);
        return;
    }

    writeFile(document->getFilePath(), isIncremental);
}

void PDFAppDocumentSaver::chooseFileAndWrite(bool isIncremental)
{
    m_step = Step::FileDialog;
    Q_EMIT stateChanged();

    // The native dialog runs its own event loop: the saver may be deleted meanwhile
    QPointer<PDFAppDocumentSaver> guard(this);
    const QString filePath = PDFAppFileDialogs::getSaveFileName(getDialogParent(), getProposedFilePath());
    if (!guard)
    {
        return;
    }

    if (filePath.isEmpty() || !getDocument() || !getDocument()->isReady())
    {
        finish(false);
        return;
    }

    writeFile(filePath, isIncremental);
}

void PDFAppDocumentSaver::writeFile(const QString& filePath, bool isIncremental)
{
    PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        finish(false);
        return;
    }

    const QString nativePath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    if (isOpenInAnotherDocument(filePath))
    {
        showErrorDialog(filePath, tr("The file is open in another tab. Your changes are still in the document – save them to another file, or close the other tab and save again."),
                        nativePath);
        return;
    }

    if (!PDFAppDocumentFileWriter::isWritable(filePath))
    {
        // The file is not touched: the user saves the changes elsewhere
        if (QFileInfo::exists(filePath))
        {
            showErrorDialog(filePath, tr("The file is read-only. Your changes are still in the document – save them to another file, or make the file writable and save again."),
                            tr("%1\nAccess is denied: the file is read-only.").arg(nativePath));
        }
        else
        {
            showErrorDialog(filePath, tr("The folder of the file does not exist. Your changes are still in the document – save them to another file."),
                            tr("%1\nThe folder was not found.").arg(nativePath));
        }
        return;
    }

    const PDFAppDocumentSnapshot snapshot = document->getSnapshot();
    PDFAppDocumentFileWriter::Request request;
    request.document = snapshot.document;
    request.originalData = snapshot.originalData;
    request.filePath = filePath;
    request.isIncremental = isIncremental && !snapshot.isPagedXFA;
    request.isOriginalData = snapshot.isPagedXFA;
    request.version = snapshot.version;
    request.markSourceFilePath = document->getOriginFilePath();

    // The document stays usable: the worker writes the snapshot
    m_step = Step::Writing;
    QFutureWatcher<PDFAppDocumentFileWriter::Result>* watcher = new QFutureWatcher<PDFAppDocumentFileWriter::Result>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<PDFAppDocumentFileWriter::Result>::finished, this, &PDFAppDocumentSaver::onWriterFinished);
    watcher->setFuture(QtConcurrent::run(&PDFAppDocumentFileWriter::write, std::move(request)));

    // The document announces the operation (operationsChanged): a slot may delete
    // the tab (the started worker writes the file anyway)
    const QString fileName = QFileInfo(filePath).fileName();
    QPointer<PDFAppDocumentSaver> guard(this);
    PDFAppDocumentOperation* operation = document->beginOperation(PDFAppDocumentOperationKind::Save, tr("Saving %1").arg(fileName), false);
    if (!guard)
    {
        return;
    }
    m_operation = operation;
    Q_EMIT stateChanged();
}

void PDFAppDocumentSaver::onWriterFinished()
{
    QFutureWatcher<PDFAppDocumentFileWriter::Result>* watcher = m_watcher.data();
    if (!watcher || sender() != watcher)
    {
        return;
    }

    const PDFAppDocumentFileWriter::Result result = watcher->future().result();
    m_watcher = nullptr;
    watcher->deleteLater();

    // The document announces the finished operation (a slot may delete the tab)
    if (PDFAppDocumentOperation* operation = m_operation.data())
    {
        QPointer<PDFAppDocumentSaver> operationGuard(this);
        m_operation = nullptr;
        operation->finish();
        if (!operationGuard)
        {
            return;
        }
    }

    PDFAppDocument* document = getDocument();
    if (!document)
    {
        finish(false);
        return;
    }

    if (!result.isSaved)
    {
        // The file of the request (Save As writes another file than the document has)
        const QString failedPath = !result.requestedFilePath.isEmpty() ? result.requestedFilePath : document->getFilePath();
        const QString nativePath = failedPath.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(failedPath).absoluteFilePath());
        showErrorDialog(failedPath, tr("The file could not be written. Your changes are still in the document – save them to another file, or try it again."),
                        result.errorMessage.isEmpty() ? nativePath : QStringLiteral("%1\n%2").arg(nativePath, result.errorMessage));
        return;
    }

    QPointer<PDFAppDocumentSaver> guard(this);
    if (document->isReady())
    {
        // The written version (a document changed meanwhile stays modified)
        document->setSavedAs(result.version, result.filePath, result.data);
    }
    if (!guard)
    {
        return;
    }

    Q_EMIT fileWritten(result.filePath);
    if (!guard)
    {
        return;
    }

    // Recent: the saved file and its new thumbnail
    PDFAppContext* context = m_tab ? m_tab->getContext() : nullptr;
    if (context && context->getDatabase())
    {
        QString storedPath;
        const PDFDatabaseError error = context->getRecent().recordOpened(result.filePath, &storedPath);
        if (error.isError())
        {
            qWarning("The saved file was not recorded in Recent: %s", qPrintable(error.toString()));
        }
        else if (PDFAppThumbnailer* thumbnailer = context->getThumbnailer())
        {
            thumbnailer->requestThumbnail(storedPath.isEmpty() ? result.filePath : storedPath);
        }
    }

    finish(true);
}

void PDFAppDocumentSaver::showSignedDialog(int signatureCount)
{
    PDFAppDocument* document = getDocument();
    if (!document)
    {
        finish(false);
        return;
    }

    closeDialog();

    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Information, tr("Save the signed document?"),
                                              (signatureCount == 1 ? tr("%1 has a signature. Your changes are saved as an incremental update: the signed version stays in the file and the signature can still be verified.")
                                                                   : tr("%1 has %Ln signatures. Your changes are saved as an incremental update: the signed versions stay in the file and the signatures can still be verified.", nullptr, signatureCount))
                                                  .arg(document->getTitle()),
                                              QDialogButtonBox::NoButton, getDialogParent());
    dialog->setObjectName(QStringLiteral("saveSignedDialog"));
    dialog->setCardWidth(PDFAppDocumentSaveHelper::MESSAGE_BOX_WIDTH);
    const int changeCount = document->getUnsavedChangeCount();
    dialog->setDetailedText(changeCount == 1 ? tr("Verification lists the changes made after signing (1 change).")
                                             : changeCount > 1 ? tr("Verification lists the changes made after signing (%Ln changes).", nullptr, changeCount)
                                                               : tr("Verification lists the changes made after signing."));
    PDFAppMainWindow::prepareDialog(dialog, m_tab);

    // destructive / other action leading, Cancel next to the primary button (958:28033)
    QPushButton* saveCopyButton = dialog->addActionButton(tr("Save as Copy…"), PDFMessageBox::ActionPosition::Leading, false);
    saveCopyButton->setObjectName(QStringLiteral("saveCopyButton"));
    QPushButton* cancelButton = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    QPushButton* saveButton = dialog->addActionButton(tr("Save Incrementally"), PDFMessageBox::ActionPosition::Trailing, true);
    saveButton->setObjectName(QStringLiteral("saveButton"));

    connect(saveCopyButton, &QPushButton::clicked, this, &PDFAppDocumentSaver::onSignedDialogSaveAsCopy);
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(dialog, &QDialog::finished, this, &PDFAppDocumentSaver::onSignedDialogFinished);

    m_dialog = dialog;
    m_step = Step::SignedDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    Q_EMIT stateChanged();
}

void PDFAppDocumentSaver::onSignedDialogFinished(int result)
{
    if (sender() != m_dialog.data() || m_step != Step::SignedDialog)
    {
        // Save as Copy… has already taken over the flow
        return;
    }

    m_dialog = nullptr;
    PDFAppDocument* document = getDocument();
    if (result != QDialog::Accepted || !document || !document->isReady())
    {
        finish(false);
        return;
    }

    m_isSignedSaveConfirmed = true;
    writeFile(document->getFilePath(), true);
}

void PDFAppDocumentSaver::onSignedDialogSaveAsCopy()
{
    if (m_step != Step::SignedDialog)
    {
        return;
    }

    // The dialog is closed without its result: the file dialog continues the flow
    if (PDFMessageBox* dialog = m_dialog.data())
    {
        m_dialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }

    chooseFileAndWrite(true);
}

void PDFAppDocumentSaver::showErrorDialog(const QString& filePath, const QString& text, const QString& details)
{
    closeDialog();

    const QString name = filePath.isEmpty() ? (getDocument() ? getDocument()->getTitle() : QString()) : QFileInfo(filePath).fileName();
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Critical, tr("%1 could not be saved").arg(name), text, QDialogButtonBox::NoButton, getDialogParent());
    dialog->setObjectName(QStringLiteral("saveErrorDialog"));
    dialog->setCardWidth(PDFAppDocumentSaveHelper::MESSAGE_BOX_WIDTH);
    dialog->setDetailedText(details);
    PDFAppMainWindow::prepareDialog(dialog, m_tab);

    QPushButton* cancelButton = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    QPushButton* saveAsButton = dialog->addActionButton(tr("Save As…"), PDFMessageBox::ActionPosition::Trailing, true);
    saveAsButton->setObjectName(QStringLiteral("saveAsButton"));

    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(saveAsButton, &QPushButton::clicked, this, &PDFAppDocumentSaver::onErrorDialogSaveAs);
    connect(dialog, &QDialog::rejected, this, &PDFAppDocumentSaver::onErrorDialogRejected);

    m_dialog = dialog;
    m_step = Step::ErrorDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    Q_EMIT stateChanged();
}

void PDFAppDocumentSaver::onErrorDialogSaveAs()
{
    if (m_step != Step::ErrorDialog)
    {
        return;
    }

    if (PDFMessageBox* dialog = m_dialog.data())
    {
        m_dialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }

    const PDFAppDocument* document = getDocument();
    const bool isIncremental = document && !document->isPagedXFA() && !document->getOriginalData().isEmpty() && PDFAppDocumentFileWriter::getSignatureCount(document->getDocument().data()) > 0;
    chooseFileAndWrite(isIncremental);
}

void PDFAppDocumentSaver::closeDialog()
{
    if (PDFMessageBox* dialog = m_dialog.data())
    {
        m_dialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

void PDFAppDocumentSaver::onErrorDialogRejected()
{
    // Cancel or Escape of save-error ends the flow
    if (sender() != m_dialog.data() || m_step != Step::ErrorDialog)
    {
        return;
    }

    m_dialog = nullptr;
    finish(false);
}

void PDFAppDocumentSaver::onDocumentStateChanged()
{
    // The document has been opened again (another file, a password entered)
    m_isSignedSaveConfirmed = false;
}

void PDFAppDocumentSaver::finish(bool isSaved)
{
    m_step = Step::Idle;

    QPointer<PDFAppDocumentSaver> guard(this);
    Q_EMIT stateChanged();
    if (guard)
    {
        Q_EMIT finished(isSaved);
    }
}

bool PDFAppDocumentSaver::isOpenInAnotherDocument(const QString& filePath) const
{
    PDFAppContext* context = m_tab ? m_tab->getContext() : nullptr;
    PDFAppDocumentRegistry* registry = context ? context->getDocumentRegistry() : nullptr;
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    if (!registry || canonicalPath.isEmpty())
    {
        return false;
    }

    const PDFAppDocument* other = registry->findByCanonicalPath(canonicalPath);
    return other && other != getDocument();
}

PDFAppDocumentFileCommandHandler::PDFAppDocumentFileCommandHandler(PDFAppDocumentTab* tab) :
    BaseClass(PDFAppDocumentPackage::File, tab)
{
    m_saver = new PDFAppDocumentSaver(tab, this);
    m_autoRefresh = new PDFAppAutoRefresh(tab, this);
    m_printController = new PDFAppPrintController(tab, this);
    m_sendMail = new PDFAppSendMail(tab, m_saver, this);

    connect(m_saver, &PDFAppDocumentSaver::stateChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);
    connect(m_saver, &PDFAppDocumentSaver::finished, this, &PDFAppDocumentFileCommandHandler::onSaverFinished);
    connect(m_saver, &PDFAppDocumentSaver::fileWritten, m_autoRefresh, &PDFAppAutoRefresh::ignoreOwnWrite);
    connect(m_autoRefresh, &PDFAppAutoRefresh::stateChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);
    connect(m_printController, &PDFAppPrintController::stateChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);
    connect(m_sendMail, &PDFAppSendMail::stateChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);

    if (PDFAppDocument* document = tab ? tab->getDocument() : nullptr)
    {
        // Save is enabled by the changes of the document
        connect(document, &PDFAppDocument::modifiedChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);
        connect(document, &PDFAppDocument::lockChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentFileCommandHandler::onPartStateChanged);
    }
}

PDFAppDocumentFileCommandHandler::~PDFAppDocumentFileCommandHandler()
{
    // The parts are children; they are deleted first, so they report nothing
    for (QObject* part : { static_cast<QObject*>(m_saver.data()), static_cast<QObject*>(m_autoRefresh.data()),
                           static_cast<QObject*>(m_printController.data()), static_cast<QObject*>(m_sendMail.data()) })
    {
        if (part)
        {
            part->disconnect(this);
            delete part;
        }
    }
}

PDFAppDocumentFileCommandHandler* PDFAppDocumentFileCommandHandler::find(const PDFAppDocumentTab* tab)
{
    return tab ? qobject_cast<PDFAppDocumentFileCommandHandler*>(tab->findHandler(PDFAppCommands::save())) : nullptr;
}

void PDFAppDocumentFileCommandHandler::execute(const QString& commandId)
{
    if (commandId == PDFAppCommands::save())
    {
        save();
    }
    else if (commandId == PDFAppCommands::saveAs())
    {
        saveAs();
    }
    else if (commandId == PDFAppCommands::print())
    {
        if (m_printController)
        {
            m_printController->showPrintDialog();
        }
    }
    else if (commandId == PDFAppCommands::sendMail())
    {
        if (m_sendMail)
        {
            m_sendMail->send();
        }
    }
    else if (commandId == PDFAppCommands::autoRefresh())
    {
        if (m_autoRefresh)
        {
            m_autoRefresh->setEnabled(!m_autoRefresh->isEnabled());
        }
    }
}

bool PDFAppDocumentFileCommandHandler::isEnabled(const QString& commandId) const
{
    const PDFAppDocument* document = getDocument();
    if (!document || !document->isReady())
    {
        return false;
    }

    if (commandId == PDFAppCommands::save())
    {
        return m_saver && m_saver->canSave();
    }
    if (commandId == PDFAppCommands::saveAs())
    {
        return m_saver && m_saver->canSaveAs();
    }
    if (commandId == PDFAppCommands::print())
    {
        return m_printController && m_printController->canPrint();
    }
    if (commandId == PDFAppCommands::sendMail())
    {
        return m_sendMail && m_sendMail->canSend();
    }
    if (commandId == PDFAppCommands::autoRefresh())
    {
        return !document->getFilePath().isEmpty();
    }
    return false;
}

bool PDFAppDocumentFileCommandHandler::isChecked(const QString& commandId) const
{
    if (commandId == PDFAppCommands::autoRefresh())
    {
        return m_autoRefresh && m_autoRefresh->isEnabled();
    }
    return false;
}

PDFAppDocumentSaver* PDFAppDocumentFileCommandHandler::getSaver() const
{
    return m_saver.data();
}

PDFAppAutoRefresh* PDFAppDocumentFileCommandHandler::getAutoRefresh() const
{
    return m_autoRefresh.data();
}

PDFAppPrintController* PDFAppDocumentFileCommandHandler::getPrintController() const
{
    return m_printController.data();
}

PDFAppSendMail* PDFAppDocumentFileCommandHandler::getSendMail() const
{
    return m_sendMail.data();
}

bool PDFAppDocumentFileCommandHandler::save()
{
    return m_saver && m_saver->save();
}

bool PDFAppDocumentFileCommandHandler::saveAs()
{
    return m_saver && m_saver->saveAs();
}

void PDFAppDocumentFileCommandHandler::onPartStateChanged()
{
    Q_EMIT stateChanged();
}

void PDFAppDocumentFileCommandHandler::onSaverFinished(bool isSaved)
{
    Q_EMIT saveFinished(isSaved);
}

}   // namespace pdf
