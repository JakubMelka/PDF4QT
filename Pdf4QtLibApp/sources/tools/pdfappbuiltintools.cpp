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

#include "pdfappbuiltintools.h"

#include "pdfapptoolgui_blank.h"
#include "pdfapptoolgui_compress.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolgui_merge.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolgui_split.h"
#include "pdfapptoolgui_structure.h"

namespace pdf
{

void PDFAppBuiltinTools::registerAll()
{
    // Each part of the first batch has its own line and file (plan, chapter 9), the
    // parallel parts do not touch this file.
    PDFAppToolGUICompress::registerTools();
    PDFAppToolGUIPages::registerTools();
    PDFAppToolGUIProtect::registerTools();
    PDFAppToolGUIStructure::registerTools();
    PDFAppToolGUIExtractText::registerTools();
    PDFAppMergeToolGUI::registerTools();
    PDFAppBlankToolGUI::registerTools();

    // Step 3a of phase 2 (plan 9.1, 9.2): each package registers its tools in its own file,
    // a tool stays unimplemented until its package registers it
    PDFAppToolGUISplit::registerSplitTools();
    PDFAppToolGUIGeometry::registerGeometryTools();
    PDFAppToolGUIInsert::registerInsertTools();
    PDFAppToolGUIImages::registerImageTools();
}

}   // namespace pdf
