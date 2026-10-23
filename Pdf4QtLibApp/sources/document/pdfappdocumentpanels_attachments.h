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

#ifndef PDFAPPDOCUMENTPANELS_ATTACHMENTS_H
#define PDFAPPDOCUMENTPANELS_ATTACHMENTS_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentpanels_common.h"

#include "pdfcomponentdelegates.h"
#include "pdfdocument.h"
#include "pdffile.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>

#include <memory>
#include <optional>
#include <vector>

#include <QListView>

class QTemporaryDir;

namespace pdf
{

class PDFAppCardDialog;

/// File embedded in a document: an item of the /EmbeddedFiles name tree or of a
/// file attachment annotation
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachment
{
public:
    QString name;                       ///< File name (sanitized, PDFAppAttachments::sanitizeFileName)
    QString description;                ///< Description of the document (tooltip)
    QString kind;                       ///< "Spreadsheet", "Text file", "Image", ...
    QString iconName;                   ///< Icon of the badge (file-text, image)
    PDFInteger size = -1;               ///< Size in bytes (-1 = unknown)
    PDFInteger page = -1;               ///< Page of the annotation (-1 = the name tree)
    PDFFileSpecification specification; ///< Specification with the embedded file (a shared copy)
};

/// Rules of the attachments (owner decision D8): the file name of a document is
/// untrusted, programs and scripts are never opened, a file is opened only from
/// a private temporary folder
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachments
{
public:
    PDFAppAttachments() = delete;

    /// Longest file name written to the disk
    static constexpr int MAXIMUM_FILE_NAME_LENGTH = 128;

    /// Largest decoded file, which is saved or opened (1 GB)
    static constexpr qint64 MAXIMUM_FILE_SIZE = qint64(1024) * 1024 * 1024;

    /// Attachments of the document (the name tree first, then the annotations in
    /// the order of the pages; at most \p maximumCount)
    static std::vector<PDFAppAttachment> collect(const PDFDocument* document, size_t maximumCount = 10000);

    /// The document has an attachment
    static bool hasAttachments(const PDFDocument* document);

    /// File name usable on every system: the last part of a path, without control
    /// characters, reserved characters (\ / : * ? " < > |), leading dots, trailing
    /// dots and spaces and reserved names of Windows (CON, NUL, COM1...), at most
    /// MAXIMUM_FILE_NAME_LENGTH characters keeping the extension; "attachment" when
    /// nothing remains
    static QString sanitizeFileName(const QString& name);

    /// The file is a program or a script by its extension (exe, bat, cmd, com,
    /// msi, ps1, vbs, js, jar, sh, app, ...): it is never opened, only saved
    static bool isExecutable(const QString& fileName);

    /// The file is a PDF by its extension (opened in a new tab of PDF4QT)
    static bool isPdf(const QString& fileName);

    /// Kind and icon of the file by its extension
    static void getKind(const QString& fileName, QString* kind, QString* iconName);

    /// "84 kB", "1.2 MB" (decimal units of the design)
    static QString formatSize(PDFInteger size);

    /// Result of the writing of an attachment
    class WriteResult
    {
    public:
        QString filePath;   ///< Written file
        QString error;      ///< Translated error (empty = success)
        bool isOpenRequested = false;
        bool isPdf = false;
    };

    /// Decodes the stream and writes it to the file (worker thread; the document
    /// and the stream are shared copies). The file is readable and writable only
    /// by the owner, never executable.
    static WriteResult write(PDFDocumentPointer document, PDFFileSpecification specification, QString filePath, bool isOpenRequested);
};

/// Rows of the attachments panel (PDFComponentRoles of PDFAttachmentDelegate)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachmentsModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    explicit PDFAppAttachmentsModel(QObject* parent);
    virtual ~PDFAppAttachmentsModel() override;

    void setAttachments(std::vector<PDFAppAttachment> attachments);
    const PDFAppAttachment* getAttachment(int row) const;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<PDFAppAttachment> m_attachments;
};

/// Delegate of the attachments: Attachment row with the save action and the
/// action Open in the context menu
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachmentDelegate : public PDFAttachmentDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFAttachmentDelegate;

public:
    enum ExtraActionId
    {
        OpenAction = 1  ///< Open the file (context menu only)
    };

    explicit PDFAppAttachmentDelegate(QObject* parent);
    virtual ~PDFAppAttachmentDelegate() override;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// Attachments panel (pdf4qt-document-attachments 195:2070): "Attachments / 3
/// files" and the Attachment rows (name, kind · size, the save button). A click
/// (Enter) opens the file: a PDF in a new tab of PDF4QT, another file after the
/// confirmation 1028:23389 by the program of the system - both from a private
/// temporary folder of the panel; programs and scripts are refused (D8), they can
/// only be saved. Save As… writes the file anywhere. The decoding and the
/// writing run in a worker thread; a result arriving after the panel is gone is
/// dropped.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachmentsPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    explicit PDFAppAttachmentsPanel(QWidget* parent);
    virtual ~PDFAppAttachmentsPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Saves the attachment of the row (the save dialog)
    void saveAttachment(int row);

    /// Opens the attachment of the row (PDF: a new tab; other files: the confirmation)
    void openAttachment(int row);

    /// Folder of the opened attachments (empty until a file is opened)
    QString getTemporaryFolder() const;

    /// Number of the running writes (tests)
    int getRunningCount() const;

    QListView* getListView() const { return m_view; }
    PDFAppAttachmentsModel* getModel() const { return m_model; }

protected:
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void onActivated(const QModelIndex& index);
    void onDelegateAction(const QModelIndex& index, int action);
    void onWriteFinished();
    void onConfirmFinished(int result);
    void rebuild();
    void startWrite(const PDFAppAttachment& attachment, PDFDocumentPointer document, const QString& filePath, bool isOpenRequested);
    QString createTemporaryPath(const QString& fileName);
    void showError(const QString& title, const QString& text);

    QPointer<PDFAppPanelListView> m_view;
    QPointer<PDFAppAttachmentsModel> m_model;
    QPointer<PDFAppAttachmentDelegate> m_delegate;
    QPointer<PDFAppDocument> m_connectedDocument;
    QPointer<PDFAppCardDialog> m_confirmDialog;
    std::optional<PDFAppAttachment> m_confirmAttachment;   ///< Attachment of the open confirmation
    PDFDocumentPointer m_confirmDocument;                   ///< Its document (the version shown then)
    PDFDocumentPointer m_document;
    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
    std::vector<QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>> m_watchers;
};

/// Factory of the attachments panel
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachmentsPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual bool isAvailable(const PDFAppDocument* document) const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_ATTACHMENTS_H
