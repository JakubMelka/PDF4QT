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

#include "scannerbackend.h"

#ifdef Q_OS_WIN
#include "wiascannerbackend.h"
#endif

#ifdef PDF4QT_HAS_SANE
#include "sanescannerbackend.h"
#endif

namespace pdfplugin
{

ScannerBackend::~ScannerBackend() = default;

std::unique_ptr<ScannerBackend> createPlatformScannerBackend()
{
#ifdef Q_OS_WIN
    return std::make_unique<WiaScannerBackend>();
#elif defined(PDF4QT_HAS_SANE)
    return std::make_unique<SaneScannerBackend>();
#else
    return nullptr;
#endif
}

QString colorModeToScannerName(ScanSettings::ColorMode mode)
{
    switch (mode)
    {
        case ScanSettings::ColorMode::Color:
            return QStringLiteral("Color");
        case ScanSettings::ColorMode::Grayscale:
            return QStringLiteral("Gray");
        case ScanSettings::ColorMode::Lineart:
            return QStringLiteral("Lineart");
    }

    return QStringLiteral("Color");
}

}   // namespace pdfplugin
