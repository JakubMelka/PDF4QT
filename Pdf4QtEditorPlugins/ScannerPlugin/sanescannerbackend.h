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

#ifndef SANESCANNERBACKEND_H
#define SANESCANNERBACKEND_H

#include "scannerbackend.h"

#include <sane/sane.h>

namespace pdfplugin
{

class SaneScannerBackend : public ScannerBackend
{
public:
    SaneScannerBackend();
    virtual ~SaneScannerBackend() override;

    virtual QString backendName() const override;
    virtual std::vector<ScannerDevice> devices(QString* errorMessage) override;
    virtual QStringList sources(const QString& deviceId) override;
    virtual ScanResult scan(const ScanSettings& settings) override;

private:
    bool setOptionInt(SANE_Handle handle, const char* name, int value);
    bool setOptionString(SANE_Handle handle, const char* name, const QString& value);
    int findOption(SANE_Handle handle, const char* name) const;
    QImage readImage(SANE_Handle handle, int dpi, QString* errorMessage);

    bool m_initialized = false;
};

}   // namespace pdfplugin

#endif // SANESCANNERBACKEND_H
