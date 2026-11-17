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

#ifndef PDFOUTPUTPREVIEWANALYZER_H
#define PDFOUTPUTPREVIEWANALYZER_H

#include "pdfglobal.h"
#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfdocumenttool.h"
#include "pdfexception.h"
#include "pdftransparencyrenderer.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QPoint>
#include <QSize>

#include <memory>
#include <vector>

namespace pdf
{
class PDFCMSManager;
class PDFFontCache;
class PDFICCProfileCache;
class PDFOperationControl;
class PDFOptionalContentActivity;

/// The content kinds of the menu Content of Output Preview (474:9478)
enum class PDFOutputPreviewContent : uint32_t
{
    None        = 0x00,
    Images      = 0x01,
    Text        = 0x02,
    Vectors     = 0x04,
    Shadings    = 0x08,
    Patterns    = 0x10,
    All         = 0x1F
};

/// The settings of one picture of Output Preview (a value, copied into the job of the rendering)
class PDF4QTLIBCORESHARED_EXPORT PDFOutputPreviewSettings
{
public:
    /// Most pixels of the longer side of the picture: the one limit of the canvas, the tools and MCP (R7-22 (5))
    static constexpr int MAXIMUM_RESOLUTION = 4096;

    int resolution = 1920;                              ///< Pixels of the longer side of the page (72 to MAXIMUM_RESOLUTION)
    bool isSeparationSimulated = true;                  ///< Simulate Separations (476:10352)
    bool isPaperColorSimulated = false;                 ///< Paper Color (476:9987)
    QColor paperColor = Qt::white;                      ///< The color of the paper
    uint32_t content = uint32_t(PDFOutputPreviewContent::All);  ///< The drawn kinds of content (PDFOutputPreviewContent flags)
    std::vector<QByteArray> inactiveInks;               ///< Names of the inks, which are not drawn (the unchecked rows of Inks)
    double coverageLimit = 300.0;                       ///< Total area coverage limit in % (TAC of the checked inks, R7-22 (2), 474:9818)
    double richBlackLimit = 95.0;                       ///< Rich black: K at least this % together with C, M or Y above 0 (owner answer Q7.7)
    PDFCMSSettings cmsSettings;                         ///< Color management (withoutProofing: a proof never reaches it, Q7.10)
    PDFDocumentToolRenderContext rendering;             ///< The features and the layers (Q7.8: as printing, layers by /Print, annotations with the Print flag)

    /// The settings of a run of a tool: \p rendering and its color management - the color management
    /// of the settings of the application, else the default one with the output intents of the
    /// document (PDFCMSManager::getDefaultSettings, as PDFDocumentToolPageRenderSettings::create)
    /// \param document The document (its output intents)
    /// \param rendering The rendering of the context of the run
    static PDFOutputPreviewSettings create(const PDFDocument* document, const PDFDocumentToolRenderContext& rendering);
};

/// One ink of the picture (Inks of 474:8386)
class PDF4QTLIBCORESHARED_EXPORT PDFOutputPreviewInk
{
public:
    QByteArray name;            ///< Name of the ink ("Cyan", "PANTONE 151 C")
    QString displayName;        ///< Text shown (the name decoded)
    QColor color;               ///< Color of the swatch
    bool isSpot = false;        ///< A spot color (not a process ink)
    bool isActive = true;       ///< The ink is drawn (not in inactiveInks and not over the limit of the spot inks)
    double area = 0.0;          ///< Area of the ink weighted by its tint, in mm² (K7.15), of the whole page (the media box)
    double ratio = 0.0;         ///< The ink inside of the box of the ratios over the area of that box, in % (owner answer Q7.6, R7-8)
    int channel = -1;           ///< Channel of PDFOutputPreviewResult::bitmap; -1 = none (a spot ink over the limit of the spot inks, not measured)
};

/// The picture of one page and what was measured on it
class PDF4QTLIBCORESHARED_EXPORT PDFOutputPreviewResult
{
public:
    PDFFloatBitmapWithColorSpace bitmap;    ///< The separations of the page (process and spot inks, shape and opacity channels); the inks of the process color space (CMYK), every ink with its value also when it is not checked
    QImage image;                           ///< The picture of the separations on the paper (device RGB, the unchecked inks hidden); the mode Separations
    std::vector<PDFOutputPreviewInk> inks;  ///< Process inks first, then the spot inks
    bool isSpotLimitReached = false;        ///< More spot inks than PDFTransparencyRendererSettings allows (476:11703, 26 spot inks)
    PDFInteger pageIndex = -1;              ///< The page (index from 0)
    double pageArea = 0.0;                  ///< Area of the box of the ratios in mm² (TrimBox, else CropBox, clipped by MediaBox - owner answer Q7.6, R7-8)
    bool isTrimBox = false;                 ///< The box of the ratios is the TrimBox of the page (else its CropBox)
    double mediaArea = 0.0;                 ///< Area of the whole page (the media box with /UserUnit) in mm²: the areas of the inks are of it
    double maximumCoverage = 0.0;           ///< Highest total area coverage of a pixel in % (checked inks)
    double areaOverCoverageLimit = 0.0;     ///< Area over the coverage limit in mm²
    double richBlackArea = 0.0;             ///< Area of the rich black in mm²
    std::vector<PDFRenderError> renderingIssues;    ///< Errors of the rendering (476:11358: drawn with a warning)
    QString errorMessage;                   ///< The page cannot be rendered at all (translated); empty = rendered
    bool isCanceled = false;                ///< Canceled: nothing else is valid

    /// The page was rendered (no error, not canceled)
    bool isRendered() const { return errorMessage.isEmpty() && !isCanceled; }
};

/// The objects of the rendering of one document, which every page of a run shares (pointers, owned
/// by the caller - PDFOutputPreviewEnvironment, the old dialog of the Editor plugin, PdfTool - for
/// the whole time of the rendering). Threads: the pages are rendered in parallel; the caches, the
/// color management and the activity of the layers are only read, the ink mapper is not changed
/// after its spot colors are created.
class PDF4QTLIBCORESHARED_EXPORT PDFOutputPreviewContext
{
public:
    const PDFDocument* document = nullptr;
    const PDFFontCache* fontCache = nullptr;
    const PDFICCProfileCache* iccProfileCache = nullptr;
    const PDFCMSManager* cmsManager = nullptr;
    const PDFOptionalContentActivity* optionalContentActivity = nullptr;
    const PDFInkMapper* inkMapper = nullptr;
};

/// The objects of the rendering of one document as printing (owner answer Q7.8 a: the layers by
/// their /Print usage, ISO 32000-2, 8.11.4.4; the annotations with the flag Print): the font and
/// ICC caches, the color management of the settings (withoutProofing - a proof never reaches it,
/// Q7.10), the activity of the layers and the ink mapper with the spot colors of the pages
/// (PDFInkMapper::createSpotColors over PDFResourceWalk, B197). Ownership: the object owns them;
/// it is created and deleted in the thread of a run (the CMS manager is a QObject of that thread),
/// the threads of the pages only read them through getContext.
class PDF4QTLIBCORESHARED_EXPORT PDFOutputPreviewEnvironment
{
public:
    /// \param document The document (shared and immutable for the whole life of the object)
    /// \param settings The settings (the color management, the simulation of the separations)
    /// \param spotPages The pages, whose spot colors are the spot inks (indices from 0)
    /// \param control Cancellation of the walk of the resources (optional)
    explicit PDFOutputPreviewEnvironment(const PDFDocument* document, const PDFOutputPreviewSettings& settings, const std::vector<PDFInteger>& spotPages, const PDFOperationControl* control);
    ~PDFOutputPreviewEnvironment();

    PDFOutputPreviewEnvironment(const PDFOutputPreviewEnvironment&) = delete;
    PDFOutputPreviewEnvironment& operator=(const PDFOutputPreviewEnvironment&) = delete;

    /// The pointers for PDFOutputPreviewAnalyzer::renderPage
    PDFOutputPreviewContext getContext() const;

    /// The ink mapper (the inks of the document)
    const PDFInkMapper* getInkMapper() const { return m_inkMapper.get(); }

private:
    const PDFDocument* m_document;
    std::unique_ptr<PDFOptionalContentActivity> m_optionalContentActivity;
    std::unique_ptr<PDFCMSManager> m_cmsManager;
    std::unique_ptr<PDFFontCache> m_fontCache;
    std::unique_ptr<PDFICCProfileCache> m_iccProfileCache;
    std::unique_ptr<PDFInkMapper> m_inkMapper;
};

/// The analysis of Output Preview (plan of step 7 of phase 2, 4.6, 9.3): one page rendered by
/// PDFTransparencyRenderer as separations with the simulation of the paper and the chosen content and inks;
/// from the bitmap the separations, the map of the coverage, the shape and opacity channels, the warnings of
/// the coverage (TAC) and of the rich black, the areas under and over the limits and the value under a
/// point. The move of outputpreviewwidget.cpp:632-840 and outputpreviewdialog.cpp:340-388, with B168
/// (rich black only against C, M, Y; Q7.7), B169 (the box of Q7.6), B211 (overprint mode 1 only for
/// DeviceCMYK and not for images and shadings, ISO 32000-2 8.6.7, 11.7.4.4, R7-7).
///
/// Threads: render is reentrant over the shared immutable document, it runs in one job of the GUI
/// (QtConcurrent::run, a new picture cancels the old one - plan 6.3) or in the run of the tool; never on the
/// GUI thread. The picture images and the values under a point are computed from the result alone.
class PDF4QTLIBCORESHARED_EXPORT PDFOutputPreviewAnalyzer
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFOutputPreviewAnalyzer)

public:
    PDFOutputPreviewAnalyzer() = delete;

    /// What the picture of the canvas shows (the modes of 474:8728)
    enum class Mode
    {
        Separations,        ///< "separations"
        CoverageWarning,    ///< "coverage-warning"
        RichBlackWarning,   ///< "rich-black-warning"
        InkCoverage,        ///< "ink-coverage"
        ShapeChannel,       ///< "shape-channel"
        OpacityChannel      ///< "opacity-channel"
    };

    /// Smallest value of a process ink, which counts as present for the rich black (Q7.7: "C, M
    /// or Y above 0"): the float rounding of the color conversion is no ink (0.01 %)
    static constexpr double PRESENT_INK_TINT = 1.0e-4;

    /// Renders and measures the page (one PDFOutputPreviewEnvironment for this page)
    /// \param document The document
    /// \param pageIndex The page (index from 0)
    /// \param settings The settings
    /// \param control Cancellation (checked by the renderer, B199)
    static PDFOutputPreviewResult render(const PDFDocument* document, PDFInteger pageIndex, const PDFOutputPreviewSettings& settings, const PDFOperationControl* control);

    /// Renders and measures the page with the shared objects of a run (the one implementation of
    /// Output Preview, Ink Coverage and PDFInkCoverageCalculator): the page as printing - its
    /// contents and the printed annotations (PDFTransparencyRenderer::processPrintedAnnotations) -
    /// by PDFTransparencyRenderer in the process color space (CMYK) with the spot inks of the ink
    /// mapper; the areas of the inks weighted by the tint and by the opacity (an ink painted with
    /// opacity 0.5 over the paper leaves half of its tint) for the whole page, the ratios over the
    /// pixels of the box of the ratios (Q7.6: numerator and denominator of the same pixels, R7-8),
    /// the total area coverage of the checked inks (R7-22 (2)), the rich black (Q7.7: K at least
    /// richBlackLimit together with C, M or Y of a checked process ink above PRESENT_INK_TINT;
    /// only with four process inks). Reentrant over the shared context; called by more threads at
    /// once (PDFExecutionPolicy).
    /// \param context The objects of the run
    /// \param pageIndex The page (index from 0)
    /// \param settings The settings (cmsSettings and rendering are of the context, not used)
    /// \param control Cancellation (optional)
    static PDFOutputPreviewResult renderPage(const PDFOutputPreviewContext& context, PDFInteger pageIndex, const PDFOutputPreviewSettings& settings, const PDFOperationControl* control);

    /// The inks of a picture (PDFOutputPreviewResult::inks without the measured values): the process
    /// inks and the active spot inks of the ink mapper - the channels of the bitmap in this order -,
    /// then the spot inks over the limit without a channel (only when the separations are simulated);
    /// an ink of \p settings inactiveInks is not active
    static std::vector<PDFOutputPreviewInk> createInks(const PDFInkMapper* inkMapper, const PDFOutputPreviewSettings& settings);

    /// The size of the picture of the page: the rotated media box scaled to \p resolution pixels of
    /// its longer side (bounded to 72 to MAXIMUM_RESOLUTION); an empty size for a page without an area
    static QSize getImageSize(const PDFPage* page, int resolution);

    /// The bytes, which the rendering of the page takes at most at once (the bitmaps of the device
    /// group, of the page group, of the draw buffer, the masks and the result; plan 4.5, the budget
    /// of the images of a run). Nested groups take more (SECURITY_TODO.md 59).
    /// \param page The page
    /// \param resolution Pixels of the longer side
    /// \param spotInkCount Active spot inks
    static qint64 getMemoryEstimate(const PDFPage* page, int resolution, size_t spotInkCount);

    /// The image of the canvas of a mode (the warnings in \p warningColor)
    static QImage createImage(const PDFOutputPreviewResult& result, Mode mode, const PDFOutputPreviewSettings& settings, const QColor& warningColor);

    /// The tints of the inks at a pixel of the bitmap in % (the order of PDFOutputPreviewResult::inks; empty
    /// outside of the bitmap) - Under the cursor of 474:8386, from the stored bitmap without a new rendering
    static std::vector<double> getValuesAt(const PDFOutputPreviewResult& result, QPoint pixel);

    /// The identifier of a mode ("separations") and the mode of an identifier (Separations for an unknown one)
    static QString getModeId(Mode mode);
    static Mode getMode(const QString& id);
};

}   // namespace pdf

#endif // PDFOUTPUTPREVIEWANALYZER_H
