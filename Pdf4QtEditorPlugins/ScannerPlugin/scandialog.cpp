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

#include "scandialog.h"

#include "pdfwidgetutils.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pdfplugin
{

ScanDialog::ScanDialog(QWidget* parent) :
    QDialog(parent),
    m_backend(createPlatformScannerBackend())
{
    setWindowTitle(tr("Scan Pages"));

    m_deviceComboBox = new QComboBox(this);
    m_sourceComboBox = new QComboBox(this);
    m_colorModeComboBox = new QComboBox(this);
    m_resolutionSpinBox = new QSpinBox(this);
    m_pageCountSpinBox = new QSpinBox(this);
    m_statusLabel = new QLabel(this);
    m_reloadButton = new QPushButton(tr("Reload Devices"), this);
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_scanButton = m_buttonBox->addButton(tr("Scan"), QDialogButtonBox::ActionRole);

    m_colorModeComboBox->addItem(tr("Color"), int(ScanSettings::ColorMode::Color));
    m_colorModeComboBox->addItem(tr("Grayscale"), int(ScanSettings::ColorMode::Grayscale));
    m_colorModeComboBox->addItem(tr("Lineart"), int(ScanSettings::ColorMode::Lineart));

    m_resolutionSpinBox->setRange(75, 1200);
    m_resolutionSpinBox->setSingleStep(75);
    m_resolutionSpinBox->setValue(300);
    m_resolutionSpinBox->setSuffix(tr(" dpi"));

    m_pageCountSpinBox->setRange(1, 999);
    m_pageCountSpinBox->setValue(1);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->addRow(tr("Device:"), m_deviceComboBox);
    formLayout->addRow(tr("Source:"), m_sourceComboBox);
    formLayout->addRow(tr("Color mode:"), m_colorModeComboBox);
    formLayout->addRow(tr("Resolution:"), m_resolutionSpinBox);
    formLayout->addRow(tr("Pages:"), m_pageCountSpinBox);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_reloadButton);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_buttonBox);

    connect(m_reloadButton, &QPushButton::clicked, this, &ScanDialog::reloadDevices);
    connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &ScanDialog::updateSources);
    connect(m_scanButton, &QPushButton::clicked, this, &ScanDialog::scan);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ScanDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &ScanDialog::reject);

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(520, 260));
    pdf::PDFWidgetUtils::style(this);

    reloadDevices();
}

ScanDialog::~ScanDialog() = default;

std::vector<ScannedPage> ScanDialog::takePages()
{
    return std::move(m_pages);
}

void ScanDialog::reloadDevices()
{
    m_devices.clear();
    m_deviceComboBox->clear();
    m_sourceComboBox->clear();
    m_statusLabel->clear();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    if (!m_backend)
    {
        m_scanButton->setEnabled(false);
        m_statusLabel->setText(tr("No scanner backend is available for this platform."));
        return;
    }

    QString errorMessage;
    m_devices = m_backend->devices(&errorMessage);

    for (const ScannerDevice& device : m_devices)
    {
        QString label = device.name;
        if (!device.vendor.isEmpty() || !device.model.isEmpty())
        {
            label = tr("%1 (%2 %3)").arg(device.name, device.vendor, device.model).simplified();
        }
        m_deviceComboBox->addItem(label, device.id);
    }

    m_scanButton->setEnabled(!m_devices.empty());
    if (m_devices.empty())
    {
        m_statusLabel->setText(errorMessage.isEmpty() ? tr("No scanner devices found.") : errorMessage);
    }
    else
    {
        m_statusLabel->setText(tr("%1 scanner backend ready.").arg(m_backend->backendName()));
    }

    updateSources();
}

void ScanDialog::updateSources()
{
    m_sourceComboBox->clear();
    if (!m_backend || m_deviceComboBox->currentIndex() < 0)
    {
        return;
    }

    const QStringList sources = m_backend->sources(m_deviceComboBox->currentData().toString());
    if (sources.empty())
    {
        m_sourceComboBox->addItem(tr("Default"), QString());
        return;
    }

    for (const QString& source : sources)
    {
        m_sourceComboBox->addItem(source, source);
    }
}

void ScanDialog::scan()
{
    if (!m_backend)
    {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    setEnabled(false);
    ScanResult result = m_backend->scan(getSettings());
    setEnabled(true);
    QApplication::restoreOverrideCursor();

    if (!result)
    {
        QMessageBox::critical(this, tr("Scanner Error"), result.errorMessage);
        return;
    }

    m_pages = std::move(result.pages);
    m_statusLabel->setText(tr("Scanned pages: %1").arg(m_pages.size()));
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_pages.empty());
}

ScanSettings ScanDialog::getSettings() const
{
    ScanSettings settings;
    settings.deviceId = m_deviceComboBox->currentData().toString();
    settings.source = m_sourceComboBox->currentData().toString();
    settings.resolutionDpi = m_resolutionSpinBox->value();
    settings.pageCount = m_pageCountSpinBox->value();
    settings.colorMode = static_cast<ScanSettings::ColorMode>(m_colorModeComboBox->currentData().toInt());
    return settings;
}

}   // namespace pdfplugin
