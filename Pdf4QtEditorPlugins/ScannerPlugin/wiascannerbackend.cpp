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

#include "wiascannerbackend.h"

#include <QDir>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace pdfplugin
{

QString WiaScannerBackend::backendName() const
{
    return QStringLiteral("WIA");
}

std::vector<ScannerDevice> WiaScannerBackend::devices(QString* errorMessage)
{
    if (errorMessage)
    {
        errorMessage->clear();
    }

    ScannerDevice device;
    device.id = QStringLiteral("wia-dialog");
    device.name = QObject::tr("Windows Image Acquisition");
    return { device };
}

QStringList WiaScannerBackend::sources(const QString& deviceId)
{
    Q_UNUSED(deviceId);
    return { QObject::tr("System dialog") };
}

ScanResult WiaScannerBackend::scan(const ScanSettings& settings)
{
    ScanResult result;

    QTemporaryDir directory;
    if (!directory.isValid())
    {
        result.errorMessage = QObject::tr("Failed to create a temporary directory for scanned images.");
        return result;
    }

    const QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (powershell.isEmpty())
    {
        result.errorMessage = QObject::tr("PowerShell was not found. It is required to start the Windows WIA scanner dialog.");
        return result;
    }

    const QString outputPattern = QDir::toNativeSeparators(directory.path() + QStringLiteral("/scan_{0:D4}.png"));
    QString escapedOutputPattern = outputPattern;
    escapedOutputPattern.replace(QStringLiteral("'"), QStringLiteral("''"));
    int intent = 1;
    switch (settings.colorMode)
    {
        case ScanSettings::ColorMode::Color:
            intent = 1;
            break;
        case ScanSettings::ColorMode::Grayscale:
            intent = 2;
            break;
        case ScanSettings::ColorMode::Lineart:
            intent = 4;
            break;
    }

    QString script;
    script += QStringLiteral("$ErrorActionPreference = 'Stop'\n");
    script += QStringLiteral("$dialog = New-Object -ComObject WIA.CommonDialog\n");
    script += QStringLiteral("for ($i = 1; $i -le %1; $i++) {\n").arg(settings.pageCount);
    script += QStringLiteral("  $image = $dialog.ShowAcquireImage(1, %1, 0, '%2', $true, $true, $false)\n").arg(intent).arg(QStringLiteral("{B96B3CAF-0728-11D3-9D7B-0000F81EF32E}"));
    script += QStringLiteral("  if ($null -eq $image) { break }\n");
    script += QStringLiteral("  $path = [string]::Format('%1', $i)\n").arg(escapedOutputPattern);
    script += QStringLiteral("  if (Test-Path $path) { Remove-Item $path }\n");
    script += QStringLiteral("  $image.SaveFile($path)\n");
    script += QStringLiteral("}\n");

    QProcess process;
    process.start(powershell, { QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-Command"), script });
    if (!process.waitForStarted() || !process.waitForFinished(-1))
    {
        result.errorMessage = QObject::tr("Failed to run the Windows WIA scanner dialog.");
        return result;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        result.errorMessage = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (result.errorMessage.isEmpty())
        {
            result.errorMessage = QObject::tr("The Windows WIA scanner dialog failed.");
        }
        return result;
    }

    const QStringList files = QDir(directory.path()).entryList({ QStringLiteral("scan_*.png") }, QDir::Files, QDir::Name);
    for (const QString& file : files)
    {
        QImage image(directory.filePath(file));
        if (!image.isNull())
        {
            ScannedPage page;
            page.image = std::move(image);
            page.dpiX = settings.resolutionDpi;
            page.dpiY = settings.resolutionDpi;
            result.pages.push_back(std::move(page));
        }
    }

    if (result.pages.empty())
    {
        result.errorMessage = QObject::tr("No image was acquired from the Windows WIA scanner dialog.");
    }

    return result;
}

}   // namespace pdfplugin
