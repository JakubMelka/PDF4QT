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

namespace pdf
{

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

/// Rules of the attachments (owner decisions D8, O18): the file name of a document
/// is untrusted; every attachment can be saved; only a PDF attachment is opened -
/// in a new tab of PDF4QT directly from the memory; no other file is ever opened
/// (no temporary file, no program of the system). A saved attachment takes over
/// the Mark of the Web of the source PDF (Windows).
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

    /// Attachments of the current version of \p document: collected once per
    /// version and shared by the panel and the tab of the sidebar
    /// (PDFAppDocument::getDerivedData); empty for a document, which is not shown
    static std::shared_ptr<const std::vector<PDFAppAttachment>> getAttachments(const PDFAppDocument* document);

    /// Number of the calls of collect since the start (tests)
    static int getCollectCount();

    /// File name usable on every system: the last part of a path, without control
    /// characters, reserved characters (\ / : * ? " < > |), leading dots, trailing
    /// dots and spaces and reserved names of Windows (CON, NUL, COM1...), at most
    /// MAXIMUM_FILE_NAME_LENGTH characters keeping the extension; "attachment" when
    /// nothing remains
    static QString sanitizeFileName(const QString& name);

    /// The file is a program or a script by its extension (exe, bat, cmd, com,
    /// msi, ps1, vbs, js, jar, sh, app, ...): used by the Launch actions
    /// (PDFAppDocumentActionHandler), which never start them
    static bool isExecutable(const QString& fileName);

    /// The file is a PDF by its extension (opened in a new tab of PDF4QT)
    static bool isPdf(const QString& fileName);

    /// Largest Mark of the Web, which is copied
    static constexpr qint64 MAXIMUM_ZONE_IDENTIFIER_SIZE = 64 * 1024;

    /// Copies the Mark of the Web (the alternate data stream Zone.Identifier of
    /// NTFS) of \p sourceFilePath to \p targetFilePath. Best effort: \returns
    /// false, when the source has none or it cannot be copied (never an error of
    /// the save); always false outside of Windows.
    static bool copyMarkOfTheWeb(const QString& sourceFilePath, const QString& targetFilePath);

    /// Returns the Mark of the Web of the file (the content of its stream
    /// Zone.Identifier, at most MAXIMUM_ZONE_IDENTIFIER_SIZE bytes); empty, when
    /// the file has none, and always outside of Windows
    static QByteArray readMarkOfTheWeb(const QString& filePath);

    /// Writes the Mark of the Web \p zoneIdentifier (readMarkOfTheWeb) to the file.
    /// Best effort: \returns false for an empty or too large mark, a write error and
    /// always outside of Windows.
    static bool writeMarkOfTheWeb(const QString& filePath, const QByteArray& zoneIdentifier);

    /// Kind and icon of the file by its extension
    static void getKind(const QString& fileName, QString* kind, QString* iconName);

    /// "84 kB", "1.2 MB" (decimal units of the design)
    static QString formatSize(PDFInteger size);

    /// Result of the writing or of the reading of an attachment
    class WriteResult
    {
    public:
        QString filePath;   ///< Written file (save)
        QString name;       ///< Name of the attachment (open)
        QString error;      ///< Translated error (empty = success)
        QByteArray data;    ///< Decoded PDF (open)
        bool isOpenRequested = false;
    };

    /// Decodes the embedded file (any thread); an empty result with \p error on a
    /// failure (no data, a damaged stream, larger than MAXIMUM_FILE_SIZE)
    static QByteArray decode(const PDFDocumentPointer& document, const PDFFileSpecification& specification, QString* error);

    /// Decodes the stream and writes it to the file (worker thread; the document
    /// and the stream are shared copies). The file is readable and writable only
    /// by the owner, never executable; it takes over the Mark of the Web of
    /// \p sourceFilePath (copyMarkOfTheWeb).
    static WriteResult write(PDFDocumentPointer document, PDFFileSpecification specification, QString filePath, QString sourceFilePath);

    /// Decodes a PDF attachment for its opening in a new tab (worker thread): the
    /// bytes are kept in the memory, nothing is written to the disk
    static WriteResult read(PDFDocumentPointer document, PDFFileSpecification specification, QString name);
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

/// Delegate of the attachments: Attachment row with the save action and, for a
/// PDF attachment, the action Open in the context menu
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAttachmentDelegate : public PDFAttachmentDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFAttachmentDelegate;

public:
    enum ExtraActionId
    {
        OpenAction = 1  ///< Open the PDF in a new tab (context menu only)
    };

    explicit PDFAppAttachmentDelegate(QObject* parent);
    virtual ~PDFAppAttachmentDelegate() override;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// Attachments panel (pdf4qt-document-attachments 195:2070): "Attachments / 3
/// files" and the Attachment rows (name, kind · size, the save button - the only
/// action of the row in the design). A click (Enter) on a PDF attachment opens it
/// in a new tab of PDF4QT directly from the memory (O18: no temporary file, the
/// tab is a copy without a file, not recorded in Recent); a click on another
/// attachment saves it (the action of its row) - no other file is ever opened.
/// The decoding and the writing run in a worker thread; a result arriving after
/// the panel is gone is dropped.
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

    /// Opens the PDF attachment of the row in a new tab (decoded in a worker, read
    /// from the memory). \returns false for another attachment (nothing happens)
    bool openAttachment(int row);

    /// Action of a click / Enter on the row: a PDF is opened, another file saved
    void activateAttachment(int row);

    /// Number of the running writes and readings (tests)
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
    void rebuild();
    void startWatcher(const QFuture<PDFAppAttachments::WriteResult>& future);
    QString getSourceFilePath() const;
    void showError(const QString& title, const QString& text);

    QPointer<PDFAppPanelListView> m_view;
    QPointer<PDFAppAttachmentsModel> m_model;
    QPointer<PDFAppAttachmentDelegate> m_delegate;
    QPointer<PDFAppDocument> m_connectedDocument;
    PDFDocumentPointer m_document;
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
