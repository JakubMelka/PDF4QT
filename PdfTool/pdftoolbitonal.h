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

#ifndef PDFTOOLBITONAL_H
#define PDFTOOLBITONAL_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

/// Command-line application that creates a bitonal (monochromatic) version of a
/// document. Either the images of the document are converted one by one, or whole
/// pages are rasterized and converted - the latter is the right choice for scanned
/// documents, which store a single page as several images (a background image and
/// a masked text layer), because neither of these images is a picture of the page.
class PDFToolBitonal : public PDFToolAbstractApplication
{
public:
    /// Returns command metadata such as name/description/command string.
    virtual QString getStandardString(StandardString standardString) const override;
    /// Executes the conversion workflow using parsed options.
    virtual int execute(const PDFToolOptions& options) override;
    /// Returns the supported option flags for this tool.
    virtual Options getOptionsFlags() const override;
};

}   // namespace pdftool

#endif // PDFTOOLBITONAL_H
