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

#ifndef PDFTOOLOPTIMIZE_H
#define PDFTOOLOPTIMIZE_H

#include "pdftoolabstractapplication.h"

namespace pdftool
{

/// Command-line application that optimizes a document using selected algorithms.
/// Applies PDFOptimizer and/or PDFImageOptimizer based on supplied options.
class PDFToolOptimize : public PDFToolAbstractApplication
{
public:
    /// Returns command metadata such as name/description/command string.
    virtual QString getStandardString(StandardString standardString) const override;
    /// Executes the optimization workflow using parsed options.
    virtual int execute(const PDFToolOptions& options) override;
    /// Returns the supported option flags for this tool.
    virtual Options getOptionsFlags() const override;
};

}   // namespace pdftool

#endif // PDFTOOLOPTIMIZE_H
