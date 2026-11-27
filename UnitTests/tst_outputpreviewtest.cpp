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

// Output Preview of the core (step 7 of phase 2, plan 4.6, 8; package P3a): the total area coverage of the
// checked inks (R7-22 (2)), the rich black against C, M, Y only (B168, owner answer Q7.7), the overprint
// mode 1 only for the current color of DeviceCMYK in a DeviceCMYK group, never for an image, never for the
// compositing of a group (B211, R7-7: ISO 32000-2, 8.6.7, 11.7.4.3, Table 146), the inks of the process space
// for a page group of RGB (B196), the box of the ratios (Q7.6, R7-8), /All and /None (8.6.6.4), the limit of
// the spot inks (476:11703), the printed annotations (Q7.8), the tool review.output-preview and the switch of the
// overprint simulation of the transparency renderer (the Preview of Edit Content). The expected
// values come from the specification: a page of 100 x 100 points is rendered at 100 pixels, one pixel is
// one point; a point is 25.4 / 72 mm.

#include "outputpreviewtestpdf.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumenttool_outputpreview.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"
#include "pdftransparencyrenderer.h"
#include "testlocaleguard.h"

#include <QElapsedTimer>
#include <QtTest>

#include <map>

using namespace pdf;

class OutputPreviewTest : public QObject
{
    Q_OBJECT

private slots:
    void totalAreaCoverage();
    void richBlack();
    void overprintModeOneDeviceCmyk();
    void overprintModeOneImage();
    void overprintModeOneStencilMask();
    void overprintModeOneRgbGroup();
    void groupCompositingIgnoresOverprint();
    void overprintSimulationSwitchedOff();
    void pageGroupRgbMeasuredInProcessInks();
    void ratioOfTrimBox();
    void separationAllAndNone();
    void spotInkLimit();
    void printedAnnotations();
    void printedAnnotationErrors();
    void printedAnnotationsWithoutAppearance();
    void distinctRenderingErrors();
    void createImageModes();
    void toolOutputPreview();
    void toolFactsInCLocale();

private:
    /// Area of a rectangle of points in mm²
    static double areaMM(double width, double height) { return width * height * (25.4 / 72.0) * (25.4 / 72.0); }

    /// The settings of a test: the page of 100 points at 100 pixels, the generic color management
    static PDFOutputPreviewSettings createSettings(int resolution = 100)
    {
        PDFOutputPreviewSettings settings;
        settings.resolution = resolution;
        return settings;
    }

    /// The tints of the inks at a pixel by the names of the inks
    static double tintAt(const PDFOutputPreviewResult& result, QPoint pixel, const QByteArray& inkName)
    {
        const std::vector<double> values = PDFOutputPreviewAnalyzer::getValuesAt(result, pixel);
        for (size_t i = 0; i < result.inks.size() && i < values.size(); ++i)
        {
            if (result.inks[i].name == inkName)
            {
                return values[i];
            }
        }
        return -1.0;
    }

    static const PDFOutputPreviewInk* findInk(const PDFOutputPreviewResult& result, const QByteArray& inkName)
    {
        for (const PDFOutputPreviewInk& ink : result.inks)
        {
            if (ink.name == inkName)
            {
                return &ink;
            }
        }
        return nullptr;
    }

    /// The resources with the graphics states of the overprint: GSopm (OP, OPM 1), GSop0 (OP, OPM 0), GSoff
    static QByteArray overprintStates()
    {
        return "/ExtGState << /GSopm << /Type /ExtGState /OP true /op true /OPM 1 >> /GSop0 << /Type /ExtGState /OP true /op true /OPM 0 >> /GSoff << /Type /ExtGState /OP false /op false /OPM 0 >> >>";
    }
};

void OutputPreviewTest::totalAreaCoverage()
{
    // C 100 + M 100 + Y 100 + K 100 = 400 % on the left half, over the limit 300 % (474:9818)
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 1 1 1 k 0 0 50 100 re f");
    const PDFDocument document = pdf.read();

    PDFOutputPreviewSettings settings = createSettings();
    settings.coverageLimit = 300.0;
    PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, settings, nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));

    QCOMPARE(qRound(result.maximumCoverage), 400);
    QVERIFY(qAbs(result.areaOverCoverageLimit - areaMM(50, 100)) < 0.01);
    QVERIFY(qAbs(result.mediaArea - areaMM(100, 100)) < 0.01);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Cyan")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Black")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Cyan")), 0);

    // An unchecked ink is not in the total area coverage (R7-22 (2): the checked inks; a varnish is
    // left out by its check box) - 300 % is not over the limit 300 %; its area stays
    settings.inactiveInks = { "Cyan" };
    result = PDFOutputPreviewAnalyzer::render(&document, 0, settings, nullptr);
    QVERIFY(result.isRendered());
    QCOMPARE(qRound(result.maximumCoverage), 300);
    QCOMPARE(result.areaOverCoverageLimit, 0.0);
    const PDFOutputPreviewInk* cyan = findInk(result, "Cyan");
    QVERIFY(cyan);
    QVERIFY(!cyan->isActive);
    QVERIFY(qAbs(cyan->area - areaMM(50, 100)) < 0.01);
}

void OutputPreviewTest::richBlack()
{
    // Owner answer Q7.7: K at least 95 % together with C, M or Y above 0; B168: a spot ink with K is no
    // rich black. Four strips 25 points wide: C40 K100 (rich), K100 (no), C40 K94 (no), K100 with
    // the spot ink overprinted (no)
    OutputPreviewTestPdf pdf;
    pdf.addPage("0.4 0 0 1 k 0 0 25 100 re f "
                "0 0 0 1 k 25 0 25 100 re f "
                "0.4 0 0 0.94 k 50 0 25 100 re f "
                "0 0 0 1 k 75 0 25 100 re f /GSop0 gs /CS1 cs 1 scn 75 0 25 100 re f",
                "<< /ColorSpace << /CS1 " + OutputPreviewTestPdf::separation("Spot1") + " >> " + overprintStates() + " >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));

    // The fourth strip has both the black and the spot ink (overprint, 8.6.7)
    QCOMPARE(qRound(tintAt(result, QPoint(87, 50), "Black")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(87, 50), "Spot1")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(87, 50), "Cyan")), 0);

    QVERIFY2(qAbs(result.richBlackArea - areaMM(25, 100)) < 0.01, qPrintable(QString::number(result.richBlackArea)));

    // The limit is a setting: K 94 % is rich black with the limit 90 %
    PDFOutputPreviewSettings settings = createSettings();
    settings.richBlackLimit = 90.0;
    const PDFOutputPreviewResult lowerLimit = PDFOutputPreviewAnalyzer::render(&document, 0, settings, nullptr);
    QVERIFY(qAbs(lowerLimit.richBlackArea - areaMM(50, 100)) < 0.01);
}

void OutputPreviewTest::overprintModeOneDeviceCmyk()
{
    // ISO 32000-2, 8.6.7: with OP true and OPM 1, the zero components of a DeviceCMYK color leave
    // the backdrop - cyan over magenta gives C 100, M 100
    OutputPreviewTestPdf pdf;
    pdf.addPage("0 1 0 0 k 0 0 100 100 re f /GSopm gs 1 0 0 0 k 0 0 50 100 re f", "<< " + overprintStates() + " >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Cyan")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Magenta")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Magenta")), 100);
}

void OutputPreviewTest::overprintModeOneImage()
{
    // 8.6.7: the nonzero overprint mode does not apply to images - a DeviceCMYK image of pure cyan
    // paints all four process components (Table 146: "not in a sampled image"), the magenta is erased
    OutputPreviewTestPdf pdf;
    const QByteArray imageData("\xFF\x00\x00\x00", 4);
    const int image = pdf.add(OutputPreviewTestPdf::stream(imageData, "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceCMYK /BitsPerComponent 8"));
    pdf.addPage("0 1 0 0 k 0 0 100 100 re f /GSopm gs q 50 0 0 100 50 0 cm /Im1 Do Q",
                "<< /XObject << /Im1 " + OutputPreviewTestPdf::ref(image) + " >> " + overprintStates() + " >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Cyan")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Magenta")), 0);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Magenta")), 100);
}

void OutputPreviewTest::overprintModeOneStencilMask()
{
    // 8.6.7 (PDF 1.7: 4.5.6, Table 7.14): the nonzero overprint mode applies to the current color of DeviceCMYK
    // "specified directly, not in a sampled image". A stencil mask (an image mask, 8.9.6.2) has no colors of
    // its own: it marks where the current color is painted - so it is painted as a fill (NOTES.txt 8.6.7). The
    // sample 0 of the mask marks (Decode [0 1]): cyan over magenta gives C 100, M 100.
    OutputPreviewTestPdf pdf;
    const int mask = pdf.add(OutputPreviewTestPdf::stream(QByteArray(1, '\0'), "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ImageMask true /BitsPerComponent 1"));
    pdf.addPage("0 1 0 0 k 0 0 100 100 re f /GSopm gs 1 0 0 0 k q 50 0 0 100 50 0 cm /Im1 Do Q",
                "<< /XObject << /Im1 " + OutputPreviewTestPdf::ref(mask) + " >> " + overprintStates() + " >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Cyan")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Magenta")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Cyan")), 0);
}

void OutputPreviewTest::overprintModeOneRgbGroup()
{
    // 11.7.4.3: the nonzero overprint mode needs the current and the group color space DeviceCMYK. In
    // an RGB group cyan is converted to RGB (0, 1, 1) and paints all components: the magenta under it
    // inside of the group is replaced - the result is cyan only
    OutputPreviewTestPdf pdf;
    const int form = pdf.add(OutputPreviewTestPdf::stream("0 1 0 0 k 0 0 100 100 re f /GSopm gs 1 0 0 0 k 0 0 50 100 re f",
                                                          "/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Group << /S /Transparency /CS /DeviceRGB /I true >> /Resources << " + overprintStates() + " >>"));
    pdf.addPage("/Fm1 Do", "<< /XObject << /Fm1 " + OutputPreviewTestPdf::ref(form) + " >> >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Cyan")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Magenta")), 0);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Magenta")), 100);
}

void OutputPreviewTest::groupCompositingIgnoresOverprint()
{
    // Table 146, its last row: a group is no elementary object - it paints all its components, the
    // overprint of the graphics state is not used for its compositing. The group paints cyan with the
    // overprint off and ends with the overprint and the overprint mode 1 on (the graphics state at the
    // end of the group); on the magenta backdrop the result is cyan only.
    OutputPreviewTestPdf pdf;
    const int form = pdf.add(OutputPreviewTestPdf::stream("/GSoff gs 1 0 0 0 k 0 0 50 100 re f /GSopm gs",
                                                          "/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Group << /S /Transparency /I true >> /Resources << " + overprintStates() + " >>"));
    pdf.addPage("0 1 0 0 k 0 0 100 100 re f /GSopm gs /Fm1 Do",
                "<< /XObject << /Fm1 " + OutputPreviewTestPdf::ref(form) + " >> " + overprintStates() + " >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Cyan")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Magenta")), 0);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Magenta")), 100);
}

void OutputPreviewTest::overprintSimulationSwitchedOff()
{
    // The Preview of Edit Content without Simulate overprint (PDFTransparencyRendererSettings::
    // DisableOverprintSimulation, off by default). ISO 32000-2, 8.6.7: with the overprint on, a
    // DeviceCMYK color in the overprint mode 1 leaves the backdrop in its zero components, and a
    // Separation color (any mode) changes only its own colorant; with the overprint off "painting a
    // color in any color space shall cause the corresponding areas of unspecified colorants to be
    // erased (painted with a tint value of 0.0)". A magenta page; cyan by OP true / OPM 1 on the
    // left half; the spot ink Spot1 by OP true / OPM 0 on the bottom right quarter. The process
    // inks are C, M, Y, K, the spot ink the fifth channel; y of the pixels grows down.
    OutputPreviewTestPdf pdf;
    pdf.addPage("0 1 0 0 k 0 0 100 100 re f /GSopm gs 1 0 0 0 k 0 0 50 100 re f /GSop0 gs /CS1 cs 1 scn 50 0 50 50 re f",
                "<< /ColorSpace << /CS1 " + OutputPreviewTestPdf::separation("Spot1") + " >> " + overprintStates() + " >>");
    const PDFDocument document = pdf.read();
    const PDFPage* page = document.getCatalog()->getPage(0);
    QVERIFY(page);

    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::Print, nullptr);
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(PDFCMSSettings());
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    PDFInkMapper inkMapper(&cmsManager, &document);
    inkMapper.createSpotColors(true);
    QCOMPARE(inkMapper.getActiveSpotColorCount(), size_t(1));
    const PDFCMSPointer cms = cmsManager.getCurrentCMS();

    // The tints of C, M, Y, K, Spot1 at a pixel of the process image (times the opacity)
    const auto render = [&](bool isOverprintSimulated)
    {
        PDFTransparencyRendererSettings settings;
        settings.flags.setFlag(PDFTransparencyRendererSettings::SaveOriginalProcessImage, true);
        settings.flags.setFlag(PDFTransparencyRendererSettings::DisableOverprintSimulation, !isOverprintSimulated);
        const QSize imageSize(100, 100);
        const QTransform pagePointToDevicePoint = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0.0, 0.0), QSizeF(imageSize)));
        PDFTransparencyRenderer renderer(page, &document, &fontCache, &iccProfileCache, cms.data(), &optionalContentActivity, &inkMapper, settings, pagePointToDevicePoint);
        renderer.beginPaint(imageSize);
        const QList<PDFRenderError> errors = renderer.processContents();
        renderer.endPaint();
        return std::make_pair(renderer.getOriginalProcessBitmap(), errors.size());
    };
    const auto tints = [](const PDFFloatBitmapWithColorSpace& bitmap, int x, int y)
    {
        const PDFPixelFormat format = bitmap.getPixelFormat();
        const PDFConstColorBuffer pixel = bitmap.getPixel(size_t(x), size_t(y));
        const PDFColorComponent alpha = format.hasOpacityChannel() ? pixel[format.getOpacityChannelIndex()] : 1.0f;
        QList<int> result;
        for (uint8_t i = 0; i < format.getColorChannelCount(); ++i)
        {
            result << qRound(pixel[i] * alpha * 100.0f);
        }
        return result;
    };

    // The default simulates the overprint (the old rendering does not change)
    QVERIFY(!PDFTransparencyRendererSettings().flags.testFlag(PDFTransparencyRendererSettings::DisableOverprintSimulation));

    //                                       C    M    Y  K  Spot1
    const auto simulated = render(true);
    QCOMPARE(simulated.second, qsizetype(0));
    QCOMPARE(simulated.first.getPixelFormat().getColorChannelCount(), uint8_t(5));
    QCOMPARE(tints(simulated.first, 25, 50), QList<int>({ 100, 100, 0, 0, 0 }));   // cyan over magenta: OPM 1 keeps M
    QCOMPARE(tints(simulated.first, 75, 75), QList<int>({ 0, 100, 0, 0, 100 }));   // Spot1 over magenta: OP keeps M
    QCOMPARE(tints(simulated.first, 75, 25), QList<int>({ 0, 100, 0, 0, 0 }));     // magenta alone

    const auto knockedOut = render(false);
    QCOMPARE(knockedOut.second, qsizetype(0));
    QCOMPARE(tints(knockedOut.first, 25, 50), QList<int>({ 100, 0, 0, 0, 0 }));    // M of the cyan is 0: erased
    QCOMPARE(tints(knockedOut.first, 75, 75), QList<int>({ 0, 0, 0, 0, 100 }));    // C, M, Y, K of Spot1 are 0: erased
    QCOMPARE(tints(knockedOut.first, 75, 25), QList<int>({ 0, 100, 0, 0, 0 }));    // magenta alone, as before
}

void OutputPreviewTest::pageGroupRgbMeasuredInProcessInks()
{
    // B196: a page group of DeviceRGB is converted to the process space of the output (11.4.7) -
    // the inks are Cyan, Magenta, Yellow, Black, never Red, Green, Blue. RGB red is M 100, Y 100 by
    // the generic color management (K = 1 - max(R, G, B), the rest under the black)
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 rg 0 0 100 100 re f", "<< >>", "/Group << /S /Transparency /CS /DeviceRGB >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(result.bitmap.getPixelFormat().getProcessColorChannelCount(), uint8_t(4));

    QStringList names;
    for (const PDFOutputPreviewInk& ink : result.inks)
    {
        names << QString::fromLatin1(ink.name);
    }
    QCOMPARE(names, QStringList({ "Cyan", "Magenta", "Yellow", "Black" }));

    QCOMPARE(qRound(tintAt(result, QPoint(50, 50), "Cyan")), 0);
    QCOMPARE(qRound(tintAt(result, QPoint(50, 50), "Magenta")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(50, 50), "Yellow")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(50, 50), "Black")), 0);
    QCOMPARE(qRound(findInk(result, "Magenta")->ratio), 100);
}

void OutputPreviewTest::ratioOfTrimBox()
{
    // Owner answer Q7.6 (b), R7-8: the ratio is the ink inside of the TrimBox (else the CropBox) over
    // the area of the same box; the ink of the bleed does not make it more than 100 %. The area of
    // an ink is of the whole page (the media box).
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 0 k 0 0 120 120 re f", "<< >>", "/TrimBox [10 10 110 110]", "[0 0 120 120]");
    pdf.addPage("1 0 0 0 k 0 0 120 120 re f", "<< >>", "/CropBox [10 10 110 110]", "[0 0 120 120]");
    pdf.addPage("1 0 0 0 k 10 10 50 100 re f", "<< >>", "/TrimBox [10 10 110 110] /CropBox [0 0 120 120]", "[0 0 120 120]");
    const PDFDocument document = pdf.read();
    const PDFOutputPreviewSettings settings = createSettings(120);

    const PDFOutputPreviewResult trim = PDFOutputPreviewAnalyzer::render(&document, 0, settings, nullptr);
    QVERIFY2(trim.isRendered(), qPrintable(trim.errorMessage));
    QVERIFY(trim.isTrimBox);
    QVERIFY(qAbs(trim.pageArea - areaMM(100, 100)) < 0.01);
    QVERIFY(qAbs(trim.mediaArea - areaMM(120, 120)) < 0.01);
    QVERIFY2(qAbs(findInk(trim, "Cyan")->ratio - 100.0) < 0.01, qPrintable(QString::number(findInk(trim, "Cyan")->ratio)));
    QVERIFY(qAbs(findInk(trim, "Cyan")->area - areaMM(120, 120)) < 0.01);

    const PDFOutputPreviewResult crop = PDFOutputPreviewAnalyzer::render(&document, 1, settings, nullptr);
    QVERIFY(!crop.isTrimBox);
    QVERIFY(qAbs(crop.pageArea - areaMM(100, 100)) < 0.01);
    QVERIFY(qAbs(findInk(crop, "Cyan")->ratio - 100.0) < 0.01);

    // Half of the trim box; the crop box is the whole page - the trim box is used
    const PDFOutputPreviewResult half = PDFOutputPreviewAnalyzer::render(&document, 2, settings, nullptr);
    QVERIFY(half.isTrimBox);
    QVERIFY2(qAbs(findInk(half, "Cyan")->ratio - 50.0) < 0.01, qPrintable(QString::number(findInk(half, "Cyan")->ratio)));
    QVERIFY(qAbs(findInk(half, "Cyan")->area - areaMM(50, 100)) < 0.01);
}

void OutputPreviewTest::separationAllAndNone()
{
    // ISO 32000-2, 8.6.6.4: /All marks every colorant (the process inks and the spot inks), /None has
    // no effect - nothing is painted, nothing is erased (the magenta under it stays)
    OutputPreviewTestPdf pdf;
    pdf.addPage("/CSAll cs 1 scn 0 0 50 100 re f 0 1 0 0 k 50 0 50 100 re f /CSNone cs 1 scn 50 0 50 100 re f",
                "<< /ColorSpace << /CS1 " + OutputPreviewTestPdf::separation("Spot1") + " /CSAll " + OutputPreviewTestPdf::separation("All", "1 1 1 1") +
                " /CSNone " + OutputPreviewTestPdf::separation("None", "0 0 1 0") + " >> >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));

    // The inks: the process inks and Spot1 - /All and /None are no inks
    QCOMPARE(result.inks.size(), size_t(5));
    QVERIFY(findInk(result, "Spot1"));
    QVERIFY(!findInk(result, "All"));
    QVERIFY(!findInk(result, "None"));

    for (const QByteArray& ink : { QByteArray("Cyan"), QByteArray("Magenta"), QByteArray("Yellow"), QByteArray("Black"), QByteArray("Spot1") })
    {
        QCOMPARE(qRound(tintAt(result, QPoint(25, 50), ink)), 100);
    }

    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Magenta")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Yellow")), 0);
}

void OutputPreviewTest::spotInkLimit()
{
    // 476:11703: 26 spot inks have their separations, the 27th has none (PDFInkMapper::MAX_SPOT_COLOR_COMPONENTS)
    QByteArray colorSpaces;
    for (int i = 1; i <= 27; ++i)
    {
        colorSpaces += "/CS" + QByteArray::number(i) + " " + OutputPreviewTestPdf::separation("Spot" + QByteArray::number(i)) + " ";
    }

    OutputPreviewTestPdf pdf;
    pdf.addPage("/CS27 cs 1 scn 0 0 100 100 re f", "<< /ColorSpace << " + colorSpaces + " >> >>");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(10), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QVERIFY(result.isSpotLimitReached);
    QCOMPARE(result.inks.size(), size_t(4 + 27));
    QCOMPARE(result.inks[4 + 25].name, QByteArray("Spot26"));
    QCOMPARE(result.inks[4 + 25].channel, 4 + 25);
    QCOMPARE(result.inks[4 + 26].name, QByteArray("Spot27"));
    QCOMPARE(result.inks[4 + 26].channel, -1);
    QVERIFY(!result.inks[4 + 26].isActive);

    // The ink without a separation is painted by its alternate color (8.6.6.4): magenta
    QCOMPARE(qRound(tintAt(result, QPoint(5, 5), "Magenta")), 100);
}

void OutputPreviewTest::printedAnnotations()
{
    // Owner answer Q7.8 (a): the page as printing - an annotation with the flag Print (12.5.3, Table 167)
    // is painted, one without it or a hidden one is not; the spot ink of a printed annotation is an
    // ink of the document (B197), the one of an annotation not printed is not
    OutputPreviewTestPdf pdf;
    const QByteArray appearanceResources = "/Resources << /ColorSpace << /CS9 " + OutputPreviewTestPdf::separation("AnnotSpot") + " /CS8 " + OutputPreviewTestPdf::separation("HiddenSpot") + " >> >>";
    const int printed = pdf.add(OutputPreviewTestPdf::stream("/CS9 cs 1 scn 0 0 10 10 re f", "/Type /XObject /Subtype /Form /BBox [0 0 10 10] " + appearanceResources));
    const int notPrinted = pdf.add(OutputPreviewTestPdf::stream("/CS8 cs 1 scn 0 0 10 10 re f", "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /ColorSpace << /CS8 " + OutputPreviewTestPdf::separation("NotPrintedSpot") + " >> >>"));
    const int hidden = pdf.add(OutputPreviewTestPdf::stream("1 0 0 0 k 0 0 10 10 re f", "/Type /XObject /Subtype /Form /BBox [0 0 10 10]"));
    const int annotation1 = pdf.add("<< /Type /Annot /Subtype /Square /Rect [0 0 50 100] /F 4 /AP << /N " + OutputPreviewTestPdf::ref(printed) + " >> >>");
    const int annotation2 = pdf.add("<< /Type /Annot /Subtype /Square /Rect [50 0 100 50] /F 0 /AP << /N " + OutputPreviewTestPdf::ref(notPrinted) + " >> >>");
    const int annotation3 = pdf.add("<< /Type /Annot /Subtype /Square /Rect [50 50 100 100] /F 6 /AP << /N " + OutputPreviewTestPdf::ref(hidden) + " >> >>");
    pdf.addPage("", "<< >>", "/Annots [" + OutputPreviewTestPdf::ref(annotation1) + " " + OutputPreviewTestPdf::ref(annotation2) + " " + OutputPreviewTestPdf::ref(annotation3) + "]");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QVERIFY(findInk(result, "AnnotSpot"));
    QVERIFY(findInk(result, "HiddenSpot"));
    QVERIFY(!findInk(result, "NotPrintedSpot"));

    // The appearance (10 x 10) is mapped onto the rectangle of the annotation (12.5.5); the pixel rows
    // go down from the top of the page: [50 0 100 50] is around (75, 75), [50 50 100 100] around (75, 25)
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "AnnotSpot")), 100);
    QVERIFY(qAbs(findInk(result, "AnnotSpot")->area - areaMM(50, 100)) < 0.01);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 75), "Magenta")), 0);  // not printed (its spot ink has the alternate magenta)
    QCOMPARE(qRound(tintAt(result, QPoint(75, 25), "Cyan")), 0);     // hidden
}

void OutputPreviewTest::printedAnnotationErrors()
{
    // ISO 32000-2, 7.8.2 (Table 33): an operator, which is not known, is an error of the content stream. The
    // appearance of a printed annotation is a content stream (12.5.5), so its error is a problem of the page
    // as an error of the contents of the page is (476:11358) - also an error the parser adds itself, not only
    // one reported by reportRenderError; the rest of the appearance is still painted
    OutputPreviewTestPdf pdf;
    const int appearance = pdf.add(OutputPreviewTestPdf::stream("0 1 0 0 k 0 0 10 10 re f xyzzy", "/Type /XObject /Subtype /Form /BBox [0 0 10 10]"));
    const int annotation = pdf.add("<< /Type /Annot /Subtype /Square /Rect [0 0 50 100] /F 4 /AP << /N " + OutputPreviewTestPdf::ref(appearance) + " >> >>");
    pdf.addPage("0 0 1 0 k 50 0 50 100 re f", "<< >>", "/Annots [" + OutputPreviewTestPdf::ref(annotation) + "]");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    const QString unknownOperator = PDFTranslationContext::tr("Unknown operator '%1'.").arg(QStringLiteral("xyzzy"));
    QVERIFY(std::any_of(result.renderingIssues.cbegin(), result.renderingIssues.cend(), [&unknownOperator](const PDFRenderError& issue) { return issue.message == unknownOperator; }));
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Magenta")), 100);
    QCOMPARE(qRound(tintAt(result, QPoint(75, 50), "Yellow")), 100);

    // The page without the annotation has no such problem: the error is the one of the appearance
    OutputPreviewTestPdf plain;
    plain.addPage("0 0 1 0 k 50 0 50 100 re f");
    const PDFDocument plainDocument = plain.read();
    const PDFOutputPreviewResult plainResult = PDFOutputPreviewAnalyzer::render(&plainDocument, 0, createSettings(), nullptr);
    QVERIFY(plainResult.isRendered());
    QVERIFY(plainResult.renderingIssues.empty());
}

void OutputPreviewTest::printedAnnotationsWithoutAppearance()
{
    // RS7COLOR: a printed annotation without an appearance stream is a problem of the page only when the
    // printing would draw it. A link (12.5.6.5: it only highlights itself, when it is activated) and a pop-up
    // (12.5.6.14: the text of its parent on the screen) are drawn by no printing without an appearance - an
    // office program writes /F 4 (Print) on every hyperlink, Acrobat /F 28 on every pop-up; such a page
    // has no problem. A printed square without an appearance is one (its border is not drawn).
    OutputPreviewTestPdf pdf;
    const int link = pdf.add("<< /Type /Annot /Subtype /Link /Rect [0 0 50 20] /F 4 /BS << /W 0 >> /A << /S /URI /URI (https://example.com/) >> >>");
    const int popup = pdf.add("<< /Type /Annot /Subtype /Popup /Rect [50 50 100 100] /F 28 /Open false >>");
    pdf.addPage("1 0 0 0 k 0 0 50 100 re f", "<< >>", "/Annots [" + OutputPreviewTestPdf::ref(link) + " " + OutputPreviewTestPdf::ref(popup) + "]");
    const int square = pdf.add("<< /Type /Annot /Subtype /Square /Rect [0 0 50 50] /F 4 /C [1 0 0] >>");
    pdf.addPage("1 0 0 0 k 0 0 50 100 re f", "<< >>", "/Annots [" + OutputPreviewTestPdf::ref(link) + " " + OutputPreviewTestPdf::ref(square) + "]");
    const PDFDocument document = pdf.read();

    const PDFOutputPreviewResult links = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    QVERIFY2(links.isRendered(), qPrintable(links.errorMessage));
    QVERIFY2(links.renderingIssues.empty(), qPrintable(links.renderingIssues.empty() ? QString() : links.renderingIssues.front().message));
    QCOMPARE(qRound(tintAt(links, QPoint(25, 50), "Cyan")), 100);

    const PDFOutputPreviewResult squares = PDFOutputPreviewAnalyzer::render(&document, 1, createSettings(), nullptr);
    QVERIFY2(squares.isRendered(), qPrintable(squares.errorMessage));
    QCOMPARE(squares.renderingIssues.size(), size_t(1));
    QCOMPARE(squares.renderingIssues.front().type, RenderErrorType::Warning);
}

void OutputPreviewTest::distinctRenderingErrors()
{
    // Hostile input (RS7COLOR): a content stream of 100 000 operators, which are not known (ISO 32000-2, 7.8.2),
    // each of another name (all names of the same length). Each is one problem of the page (476:11358, each text
    // once) and the page is rendered and measured in a time proportional to them - one search in the list of
    // the problems for each of them was quadratic (5 x 10^9 comparisons of the texts, minutes).
    constexpr int ERROR_COUNT = 100000;
    QByteArray content = "1 0 0 0 k 0 0 50 100 re f\n";
    content.reserve(content.size() + ERROR_COUNT * 9);
    for (int i = 0; i < ERROR_COUNT; ++i)
    {
        content += 'u' + QByteArray::number(i).rightJustified(7, '0') + ' ';
    }

    OutputPreviewTestPdf pdf;
    pdf.addPage(content);
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, createSettings(), nullptr);
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));
    QCOMPARE(result.renderingIssues.size(), size_t(ERROR_COUNT));
    QCOMPARE(result.renderingIssues.back().message, PDFTranslationContext::tr("Unknown operator '%1'.").arg(QStringLiteral("u0099999")));
    QCOMPARE(qRound(tintAt(result, QPoint(25, 50), "Cyan")), 100);

    const qint64 limit = 20000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
}

void OutputPreviewTest::createImageModes()
{
    // 474:9818: the pixels over the limit in the warning color; the separations on the paper; the
    // values under a point of the stored result (no new rendering)
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 1 1 1 k 0 0 50 100 re f 0.4 0 0 1 k 50 0 50 50 re f");
    const PDFDocument document = pdf.read();

    PDFOutputPreviewSettings settings = createSettings();
    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(&document, 0, settings, nullptr);
    QVERIFY2(result.isRendered(), qPrintable(result.errorMessage));

    const QColor warning(Qt::green);
    const QImage separations = PDFOutputPreviewAnalyzer::createImage(result, PDFOutputPreviewAnalyzer::Mode::Separations, settings, warning);
    QCOMPARE(separations.size(), QSize(100, 100));
    QCOMPARE(separations.pixelColor(75, 25), QColor(Qt::white));

    const QImage coverage = PDFOutputPreviewAnalyzer::createImage(result, PDFOutputPreviewAnalyzer::Mode::CoverageWarning, settings, warning);
    QCOMPARE(coverage.pixelColor(25, 50), warning);
    QCOMPARE(coverage.pixelColor(75, 75), separations.pixelColor(75, 75));

    // The limit is applied to the stored result: 450 % over everything is no warning
    settings.coverageLimit = 450.0;
    const QImage noWarning = PDFOutputPreviewAnalyzer::createImage(result, PDFOutputPreviewAnalyzer::Mode::CoverageWarning, settings, warning);
    QVERIFY(noWarning.pixelColor(25, 50) != warning);

    const QImage richBlack = PDFOutputPreviewAnalyzer::createImage(result, PDFOutputPreviewAnalyzer::Mode::RichBlackWarning, settings, warning);
    QCOMPARE(richBlack.pixelColor(75, 75), warning);    // C 40 K 100
    QCOMPARE(richBlack.pixelColor(25, 50), warning);    // C 100 M 100 Y 100 K 100 is rich black too
    QVERIFY(richBlack.pixelColor(75, 25) != warning);

    const QImage opacity = PDFOutputPreviewAnalyzer::createImage(result, PDFOutputPreviewAnalyzer::Mode::OpacityChannel, settings, warning);
    QCOMPARE(qGray(opacity.pixel(25, 50)), 255);
    QCOMPARE(qGray(opacity.pixel(75, 25)), 0);

    QVERIFY(PDFOutputPreviewAnalyzer::getValuesAt(result, QPoint(100, 0)).empty());
    QCOMPARE(PDFOutputPreviewAnalyzer::getValuesAt(result, QPoint(75, 75)).size(), result.inks.size());

    for (const PDFOutputPreviewAnalyzer::Mode mode : { PDFOutputPreviewAnalyzer::Mode::Separations, PDFOutputPreviewAnalyzer::Mode::CoverageWarning, PDFOutputPreviewAnalyzer::Mode::RichBlackWarning,
                                                       PDFOutputPreviewAnalyzer::Mode::InkCoverage, PDFOutputPreviewAnalyzer::Mode::ShapeChannel, PDFOutputPreviewAnalyzer::Mode::OpacityChannel })
    {
        QCOMPARE(PDFOutputPreviewAnalyzer::getMode(PDFOutputPreviewAnalyzer::getModeId(mode)), mode);
    }
}

void OutputPreviewTest::toolOutputPreview()
{
    // review.output-preview for MCP and PdfTool: the measurements of one page, no picture (plan 4.6, 7.1)
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 1 1 1 k 0 0 50 100 re f");
    pdf.addPage("1 0 0 0 k 0 0 100 100 re f");
    PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(pdf.read())), QByteArray(), QString(), false));
    context.rendering.cmsSettings = PDFCMSSettings();

    const PDFDocumentToolOutputPreview tool;
    QVariantMap parameters;
    parameters.insert(PDFDocumentToolOutputPreview::PARAMETER_PAGE, 1);
    parameters.insert(PDFDocumentToolOutputPreview::PARAMETER_RESOLUTION, 100);
    parameters.insert(PDFDocumentToolOutputPreview::PARAMETER_INACTIVE_INKS, QStringLiteral(" Cyan , ,Magenta"));

    const PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value("page").toInteger(), qint64(1));
    QCOMPARE(qRound(result.data.value(PDFDocumentToolOutputPreview::DATA_MAXIMUM_COVERAGE).toDouble()), 200);
    QCOMPARE(result.data.value(PDFDocumentToolOutputPreview::DATA_AREA_OVER_COVERAGE_LIMIT).toDouble(), 0.0);
    QVERIFY(qAbs(result.data.value(PDFDocumentToolOutputPreview::DATA_RICH_BLACK_AREA).toDouble() - areaMM(50, 100)) < 0.01);

    const QJsonArray inks = result.data.value(PDFDocumentToolOutputPreview::DATA_INKS).toArray();
    QCOMPARE(inks.size(), 4);
    QCOMPARE(inks.at(0).toObject().value("name").toString(), QStringLiteral("Cyan"));
    QCOMPARE(inks.at(0).toObject().value("isActive").toBool(), false);
    QCOMPARE(inks.at(1).toObject().value("isActive").toBool(), false);
    QCOMPARE(inks.at(2).toObject().value("isActive").toBool(), true);
    QVERIFY(qAbs(inks.at(3).toObject().value("area").toDouble() - areaMM(50, 100)) < 0.01);
    QVERIFY(qAbs(inks.at(3).toObject().value("ratio").toDouble() - 50.0) < 0.01);

    // A page, which does not exist, is an error of the parameter
    parameters.insert(PDFDocumentToolOutputPreview::PARAMETER_PAGE, 3);
    const PDFDocumentToolResult missing = tool.run(context, parameters, nullptr, nullptr);
    QCOMPARE(missing.status, PDFDocumentToolResultStatus::Failed);

    // createSettings reads the content kinds and the paper
    QVariantMap contentParameters;
    contentParameters.insert(PDFDocumentToolOutputPreview::PARAMETER_IMAGES, false);
    contentParameters.insert(PDFDocumentToolOutputPreview::PARAMETER_PAPER_COLOR, QColor(Qt::yellow));
    const PDFOutputPreviewSettings settings = PDFDocumentToolOutputPreview::createSettings(contentParameters);
    QCOMPARE(settings.content, uint32_t(PDFOutputPreviewContent::All) & ~uint32_t(PDFOutputPreviewContent::Images));
    QCOMPARE(settings.paperColor, QColor(Qt::yellow));
}

void OutputPreviewTest::toolFactsInCLocale()
{
    // The fix wave of step 7: the facts of the tool are machine-readable (MCP, PdfTool) - the numbers in the C
    // locale, whatever the default locale is. 400 % on the left half of 100 x 100 points: 50 x 100 points over
    // the limit 300 %, (50 x 25.4 / 72) x (100 x 25.4 / 72) = 622.25 mm², rich black there too; the facts are
    // the values of the data with two decimals and the decimal point.
    pdftest::TestLocaleGuard guard;
    QVERIFY(pdftest::TestLocaleGuard::isDefaultLocaleOtherThanC());

    OutputPreviewTestPdf pdf;
    pdf.addPage("1 1 1 1 k 0 0 50 100 re f");
    PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(pdf.read())), QByteArray(), QString(), false));
    context.rendering.cmsSettings = PDFCMSSettings();

    const PDFDocumentToolOutputPreview tool;
    QVariantMap parameters;
    parameters.insert(PDFDocumentToolOutputPreview::PARAMETER_RESOLUTION, 100);
    const PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    const double overLimit = result.data.value(PDFDocumentToolOutputPreview::DATA_AREA_OVER_COVERAGE_LIMIT).toDouble();
    const double richBlack = result.data.value(PDFDocumentToolOutputPreview::DATA_RICH_BLACK_AREA).toDouble();
    QVERIFY(qAbs(overLimit - areaMM(50, 100)) < 0.01);
    QVERIFY(qAbs(richBlack - areaMM(50, 100)) < 0.01);
    QCOMPARE(facts["inks"], QStringLiteral("4 ink(s)"));
    QCOMPARE(facts["maximumCoverage"], QStringLiteral("400 %"));
    QCOMPARE(facts["areaOverCoverageLimit"], QStringLiteral("%1 mm²").arg(QString::number(overLimit, 'f', 2)));
    QCOMPARE(facts["richBlackArea"], QStringLiteral("%1 mm²").arg(QString::number(richBlack, 'f', 2)));
    QVERIFY(facts["richBlackArea"].startsWith(QStringLiteral("622.")));
}

QTEST_MAIN(OutputPreviewTest)

#include "tst_outputpreviewtest.moc"
