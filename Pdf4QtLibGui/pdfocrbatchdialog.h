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

#ifndef PDFOCRBATCHDIALOG_H
#define PDFOCRBATCHDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfocrdocumentrunner.h"

#include <QDialog>

#include <memory>

class QLabel;
class QThread;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QProgressBar;
class QDialogButtonBox;

namespace pdf
{
class PDFOCRCancelToken;
class PDFOCRModelManager;
}

namespace pdfviewer
{

/// Recognition of the text of several PDF files (phase 7b of OCR_PLAN.md,
/// Tools > Batch Recognize Text). The files are processed one after another by
/// PDFOCRDocumentRunner::processFile on a worker thread, the pages of a file in
/// parallel. The settings are taken from the OCR dialog (its last settings or a named
/// profile), the languages and the policies can be changed. Every file is written
/// into a copy (next to the original or into a folder), the originals are never
/// changed. A lossy compression is not offered, it needs the preview of the OCR dialog.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFOCRBatchDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit PDFOCRBatchDialog(QWidget* parent);
    virtual ~PDFOCRBatchDialog() override;

    /// Adds the PDF files (directories are searched for the PDF files, not recursively);
    /// files already in the list are ignored
    void addFiles(const QStringList& files);

    /// Returns the files of the list
    QStringList getFiles() const;

    /// Returns true, while the batch is running
    bool isRunning() const;

    /// Returns the output files of the file (document first, then the exports)
    QStringList getOutputFiles(const QString& inputFile) const;

    /// Returns the result, which the user wants to open in the editor (the dialog
    /// is accepted then), or an empty string
    const QString& getDocumentToOpen() const { return m_documentToOpen; }

    virtual void reject() override;

protected:
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    /// Summary of a processed file, passed from the worker thread
    struct FileSummary
    {
        pdf::PDFOCRDocumentRunner::FileResult::Status status = pdf::PDFOCRDocumentRunner::FileResult::Status::Failed;
        QString message;
        QStringList details;
        QString outputFile;
        int pageCount = 0;
        int resultPages = 0;
        int failedPages = 0;
        int wordCount = 0;
        int reviewWords = 0;
        qint64 elapsedMilliseconds = 0;
        bool cancelled = false;
    };

    enum Column
    {
        ColumnFile,
        ColumnStatus,
        ColumnPages,
        ColumnRecognized,
        ColumnFailed,
        ColumnWords,
        ColumnReview,
        ColumnTime,
        ColumnMessage,
        ColumnCount
    };

    void createUi();
    void loadSettings();
    void saveSettings() const;
    void updateSettingsSources();
    void updateUi();

    void onAddFiles();
    void onAddFolder();
    void onRemoveFiles();
    void onClearFiles();
    void onSettingsSourceChanged();
    void onBrowseOutputDirectory();
    void onManageLanguages();
    void onStart();
    void onStop();
    void onOpenResult();

    void onFileStarted(int row);
    void onFileProgress(int row, int finished, int total);
    void onFileFinished(int row, FileSummary summary);
    void onBatchFinished();

    /// Returns the configuration of the batch (source changed by the dialog)
    pdf::PDFOCRConfiguration getConfiguration() const;

    /// Returns the task of the file of the row
    pdf::PDFOCRDocumentRunner::FileTask createTask(const QString& inputFile, const pdf::PDFOCRConfiguration& configuration) const;

    /// Validates the settings, returns the errors (translated)
    QStringList validate(const pdf::PDFOCRConfiguration& configuration) const;

    void setRowStatus(int row, const QString& status, const QString& message);

    pdf::PDFOCRModelManager* m_modelManager = nullptr;

    // Files
    QTableWidget* m_filesTable = nullptr;
    QPushButton* m_addFilesButton = nullptr;
    QPushButton* m_addFolderButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_clearButton = nullptr;

    // Recognition
    QComboBox* m_settingsSourceComboBox = nullptr;
    QLineEdit* m_languagesEdit = nullptr;
    QComboBox* m_modelProfileComboBox = nullptr;
    QPushButton* m_manageLanguagesButton = nullptr;
    QComboBox* m_existingTextComboBox = nullptr;
    QComboBox* m_decisionComboBox = nullptr;
    QCheckBox* m_allowPageErrorsCheckBox = nullptr;
    QCheckBox* m_allowReducedDpiCheckBox = nullptr;
    QLabel* m_settingsSummaryLabel = nullptr;

    // Output
    QRadioButton* m_nextToOriginalRadioButton = nullptr;
    QRadioButton* m_outputDirectoryRadioButton = nullptr;
    QLineEdit* m_outputDirectoryEdit = nullptr;
    QPushButton* m_browseOutputDirectoryButton = nullptr;
    QLineEdit* m_suffixEdit = nullptr;
    QCheckBox* m_writeDocumentCheckBox = nullptr;
    QComboBox* m_compressionComboBox = nullptr;
    QCheckBox* m_exportTextCheckBox = nullptr;
    QCheckBox* m_exportHocrCheckBox = nullptr;
    QCheckBox* m_exportAltoCheckBox = nullptr;
    QCheckBox* m_exportTsvCheckBox = nullptr;
    QCheckBox* m_skipExistingCheckBox = nullptr;

    // Progress
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_progressLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_openResultButton = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;

    QThread* m_thread = nullptr;
    std::shared_ptr<pdf::PDFOCRCancelToken> m_cancelToken;
    int m_finishedFiles = 0;
    int m_totalFiles = 0;
    int m_succeededFiles = 0;
    int m_failedFiles = 0;
    int m_skippedFiles = 0;
    bool m_closeRequested = false;
    QString m_documentToOpen;
};

}   // namespace pdfviewer

#endif // PDFOCRBATCHDIALOG_H
