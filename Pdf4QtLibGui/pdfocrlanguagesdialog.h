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

#ifndef PDFOCRLANGUAGESDIALOG_H
#define PDFOCRLANGUAGESDIALOG_H

#include "pdfviewerglobal.h"
#include "pdfocrmodelmanager.h"

#include <QDialog>

namespace Ui
{
class PDFOCRLanguagesDialog;
}

class QTreeWidgetItem;

namespace pdfviewer
{

/// Dialog "Manage OCR Languages" (LANG-08): list of the language models
/// with their state, download from the catalog, import and removal.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFOCRLanguagesDialog : public QDialog
{
    Q_OBJECT

public:
    /// Creates the dialog. If manager is nullptr, the dialog creates its own manager.
    explicit PDFOCRLanguagesDialog(pdf::PDFOCRModelManager* manager, QWidget* parent);
    virtual ~PDFOCRLanguagesDialog() override;

    virtual void done(int result) override;

    /// Formats the size in bytes
    static QString formatSize(qint64 bytes);

private:
    void updateModels();
    void updateModelItem(const QString& modelId);
    void updateUi();
    void fillItem(QTreeWidgetItem* item, const pdf::PDFOCRModelInfo& model) const;
    bool isModelVisible(const pdf::PDFOCRModelInfo& model) const;
    std::vector<pdf::PDFOCRModelInfo> getSelectedModels() const;

    void onDownloadClicked(bool updateOnly);
    void onCancelClicked();
    void onImportClicked();
    void onRemoveClicked();
    void onHideClicked();
    void onOpenFolderClicked();
    void onDownloadFinished(const QString& modelId, bool success, const QString& message);

    Ui::PDFOCRLanguagesDialog* ui;
    pdf::PDFOCRModelManager* m_manager;
    QStringList m_messages;
};

}   // namespace pdfviewer

#endif // PDFOCRLANGUAGESDIALOG_H
