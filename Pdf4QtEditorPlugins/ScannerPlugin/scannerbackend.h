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

#ifndef SCANNERBACKEND_H
#define SCANNERBACKEND_H

#include <QImage>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace pdfplugin
{

struct ScannerDevice
{
    QString id;
    QString name;
    QString vendor;
    QString model;
};

struct ScanSettings
{
    enum class ColorMode
    {
        Color,
        Grayscale,
        Lineart
    };

    QString deviceId;
    QString source;
    int resolutionDpi = 300;
    ColorMode colorMode = ColorMode::Color;
    int pageCount = 1;
};

struct ScannedPage
{
    QImage image;
    int dpiX = 300;
    int dpiY = 300;
};

struct ScanResult
{
    std::vector<ScannedPage> pages;
    QString errorMessage;

    explicit operator bool() const { return errorMessage.isEmpty(); }
};

class ScannerBackend
{
public:
    virtual ~ScannerBackend();

    virtual QString backendName() const = 0;
    virtual std::vector<ScannerDevice> devices(QString* errorMessage) = 0;
    virtual QStringList sources(const QString& deviceId) = 0;
    virtual ScanResult scan(const ScanSettings& settings) = 0;
};

std::unique_ptr<ScannerBackend> createPlatformScannerBackend();
QString colorModeToScannerName(ScanSettings::ColorMode mode);

}   // namespace pdfplugin

#endif // SCANNERBACKEND_H
