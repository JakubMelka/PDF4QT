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

#ifndef PDFPAGEMASTER_ASSEMBLEOUTPUTSETTINGSDIALOG_H
#define PDFPAGEMASTER_ASSEMBLEOUTPUTSETTINGSDIALOG_H

#include "pdfdocumentmanipulator.h"
#include "pdfimageoptimizer.h"

#include <QDialog>

#include <functional>
#include <vector>

namespace Ui
{
class AssembleOutputSettingsDialog;
}

class QTableWidget;

namespace pdfpagemaster
{

class AssembleOutputSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    struct OutputPreviewItem
    {
        QString fileName;
        QString pageCount;
        QString firstSource;
        QString mode;
        QString status;
        bool isBlocking = false;
    };

    explicit AssembleOutputSettingsDialog(QString directory, QWidget* parent);
    virtual ~AssembleOutputSettingsDialog() override;

    QString getDirectory() const;
    QString getFileName() const;
    bool isOverwriteFiles() const;
    pdf::PDFDocumentManipulator::OutlineMode getOutlineMode() const;
    bool isImageOptimizationEnabled() const;
    pdf::PDFImageOptimizer::Settings getImageOptimizationSettings() const;
    void setOutputPreview(const std::vector<OutputPreviewItem>& items);
    void setOutputPreviewFactory(std::function<std::vector<OutputPreviewItem>()> factory);

public slots:
    virtual void accept() override;

private slots:
    void on_selectDirectoryButton_clicked();
    void on_imageOptimizationSettingsButton_clicked();
    void refreshOutputPreview();

private:
    Ui::AssembleOutputSettingsDialog* ui;
    QTableWidget* m_previewTable;
    std::function<std::vector<OutputPreviewItem>()> m_outputPreviewFactory;
    bool m_hasBlockingPreviewItems = false;
    pdf::PDFImageOptimizer::Settings m_imageOptimizationSettings;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_ASSEMBLEOUTPUTSETTINGSDIALOG_H
