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

#include "pdfapptoolgui_audiobook.h"
#include "pdfapptoolgui_bitonal.h"
#include "pdfapptoolgui_blank.h"
#include "pdfapptoolgui_certificates.h"
#include "pdfapptoolgui_compress.h"
#include "pdfapptoolgui_encryption.h"
#include "pdfapptoolgui_extractimages.h"
#include "pdfapptoolgui_extracttext.h"
#include "pdfapptoolgui_optimizeimages.h"
#include "pdfapptoolgui_pdftoimages.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolgui_inkcoverage.h"
#include "pdfapptoolgui_insert.h"
#include "pdfapptoolgui_manage.h"
#include "pdfapptoolgui_merge.h"
#include "pdfapptoolgui_objectinspector.h"
#include "pdfapptoolgui_outputpreview.h"
#include "pdfapptoolgui_pages.h"
#include "pdfapptoolgui_protect.h"
#include "pdfapptoolgui_redact.h"
#include "pdfapptoolgui_report.h"
#include "pdfapptoolgui_signature.h"
#include "pdfapptoolgui_signbyhand.h"
#include "pdfapptoolgui_softproof.h"
#include "pdfapptoolgui_split.h"
#include "pdfapptoolgui_statistics.h"
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

    // Step 3b of phase 2 (plan 9.1): Manage Pages, package M3 registers it in its own file
    PDFAppToolGUIManage::registerManageTools();

    // Step 4 of phase 2 (plan 9.1, 9.2): each package registers its tools in its own file
    PDFAppToolGUIPdfToImages::registerPdfToImagesTools();           // P1: convert.pdf-to-images
    PDFAppToolGUIOptimizeImages::registerOptimizeImagesTools();     // P2: optimize.images, optimize.grayscale
    PDFAppToolGUIExtractImages::registerExtractImagesTools();       // P2: convert.extract-images
    PDFAppToolGUIBitonal::registerBitonalTools();                   // P3: optimize.bitonal
    PDFAppToolGUIAudioBook::registerAudioBookTools();               // P4: convert.audiobook

    // Step 5 of phase 2 (plan 10.1, 10.2): package P2 registers the four tools of the encryption
    // (protect.password, protect.permissions, protect.certificate-encrypt, protect.unlock) with the
    // one GUI of their presets
    PDFAppToolGUIEncryption::registerEncryptionTools();

    // Step 6 of phase 2 (plan 10.1, 10.2 with R6-23): each package registers its tools in its own file,
    // a tool stays unimplemented until then
    PDFAppToolGUISignature::registerSignatureTools();           // P4: protect.sign, protect.timestamp
    PDFAppToolGUISignByHand::registerSignByHandTools();         // P4: protect.sign-by-hand
    PDFAppToolGUIRedact::registerRedactTools();                 // P5: protect.redact
    PDFAppToolGUICertificates::registerCertificatesTools();     // P5: protect.certificates

    // Step 7a + 7b of phase 2 (plan 9.1, 9.2 with R7-17): each GUI package registers its tools in its own
    // file, a tool stays unimplemented until then (the review.statistics test tool of the tests replaces the
    // built-in one by PDFDocumentToolRegistry::unregisterTool and restores it, R7-19)
    PDFAppToolGUIStatistics::registerStatisticsTools();             // P2: review.statistics
    PDFAppToolGUIObjectInspector::registerObjectInspectorTools();   // P2: review.object-inspector
    PDFAppToolGUIReport::registerReportTools();                     // P2: review.report
    PDFAppToolGUIInkCoverage::registerInkCoverageTools();           // P4: review.ink-coverage
    PDFAppToolGUIOutputPreview::registerOutputPreviewTools();       // P4: review.output-preview
    PDFAppToolGUISoftProof::registerSoftProofTools();               // P4: review.soft-proof
}

}   // namespace pdf
