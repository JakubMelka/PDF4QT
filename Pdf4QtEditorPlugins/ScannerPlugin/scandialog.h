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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef SCANDIALOG_H
#define SCANDIALOG_H

#include "scannerbackend.h"

#include <QDialog>

#include <memory>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace pdfplugin
{

class ScanDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScanDialog(QWidget* parent);
    virtual ~ScanDialog() override;

    std::vector<ScannedPage> takePages();

private:
    void reloadDevices();
    void updateSources();
    void scan();
    ScanSettings getSettings() const;

    std::unique_ptr<ScannerBackend> m_backend;
    std::vector<ScannerDevice> m_devices;
    std::vector<ScannedPage> m_pages;

    QComboBox* m_deviceComboBox = nullptr;
    QComboBox* m_sourceComboBox = nullptr;
    QComboBox* m_colorModeComboBox = nullptr;
    QSpinBox* m_resolutionSpinBox = nullptr;
    QSpinBox* m_pageCountSpinBox = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_reloadButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
};

}   // namespace pdfplugin

#endif // SCANDIALOG_H
