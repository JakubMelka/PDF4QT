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

#ifndef PDFTESSERACTOCRENGINE_H
#define PDFTESSERACTOCRENGINE_H

#include "pdfocrengine.h"

#include <pdf4qtocrtesseract_export.h>

namespace pdf
{

/// Factory of the Tesseract OCR engine (chapter 3 and 12.6 of the OCR
/// specification). The engine uses the C++ API of Tesseract 5 directly.
class PDF4QTOCRTESSERACTSHARED_EXPORT PDFTesseractOCREngineFactory : public PDFOCREngineFactory
{
public:
    static constexpr const char* IDENTIFIER = "tesseract";

    /// Registers the engine in the engine registry
    static void registerEngine();

    virtual QString getIdentifier() const override { return QLatin1String(IDENTIFIER); }
    virtual QString getName() const override;
    virtual QString getVersion() const override;
    virtual QString getLicense() const override;
    virtual bool isAvailable(QString* reason) const override;
    virtual PDFOCREngineCapabilities getCapabilities() const override;
    virtual std::unique_ptr<PDFOCREngine> createEngine() const override;
    virtual PDFOCRError validateModel(const QString& dataPath, const QString& language) const override;

    /// Returns version of the Leptonica library
    static QString getLeptonicaVersion();
};

}   // namespace pdf

#endif // PDFTESSERACTOCRENGINE_H
