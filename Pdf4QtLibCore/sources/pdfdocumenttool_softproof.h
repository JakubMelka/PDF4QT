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

#ifndef PDFDOCUMENTTOOL_SOFTPROOF_H
#define PDFDOCUMENTTOOL_SOFTPROOF_H

#include "pdfdocumenttool.h"
#include "pdfcms.h"

namespace pdf
{

/// The tool Soft Proofing for MCP and PdfTool (review.soft-proof; plan of step 7 of phase 2, 4.7, 7.1): the share
/// and the area of the colors of the pages, which the profile cannot print (out of gamut), each page rendered
/// with a private LittleCMS context with a marking alarm color (B200, R7-16). The GUI does not run it: the soft
/// proof of the GUI is a state of the view of the tab (PDFAppDocumentSoftProof) and never leaves it (Q7.10: the
/// printing, the exports and the previews never have it - PDFCMSSettings::withoutProofing).
///
/// Parameters: "profile" (the identifier of a CMYK profile of the list of PDFCMSManager: the profiles of the
/// system, of the folder of Settings and the output intents of the document; empty = the output intent of the
/// document), "intent" (auto, perceptual, relative-colorimetric, saturation, absolute-colorimetric; default
/// relative-colorimetric), "pages" (PageSelection, at most MAXIMUM_PAGES pages), "resolution" (72 to 4096,
/// default 1024), "alarmColor" (Color of the out-of-gamut marks of the GUI; the last value is remembered).
/// No built-in CMYK profiles (Q7.11). The Generic color management gives Failed with the text of Settings.
///
/// Data: "pages" (page from 1, "share" %, "area" mm²), "profile" (its description or file name), "intent".
///
/// No permission (Q7.2).
///
/// How a page is measured (package P3b): the page is rendered twice by PDFRasterizerPool with the color
/// management of the run (the settings of the context, PDFDocumentToolPageRenderSettings) and the gamut check
/// of the profile - once with the alarm color white, once black, each by its own color management with its
/// own context of Little CMS (B200). Everything, which is in the gamut, is the same in both images; a pixel
/// covered by a color out of the gamut by the fraction a differs by a x 255 in each channel. The share is the
/// sum of these fractions over the pixels of the crop box (ISO 32000-2, 14.11.2, the visible page) divided by
/// their count; the area is the same sum times the area of a pixel in mm² (with /UserUnit). The paper is not
/// a color of the document (never marked). The layers are those of the context (the tab, else OCUsage::Export),
/// the annotations with the flag Print are drawn (as PDF to Images). The rendering errors of a page are its
/// rendering issues; a page, which cannot be rendered, is left out of the data with its issue. The pages are
/// rendered in groups (both images of a page exist at once, at most MAXIMUM_GROUP_IMAGE_BYTES of the first
/// images of a group are kept), the cancellation is checked by the pool after each page.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSoftProof : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSoftProof)

public:
    static constexpr const char* TOOL_ID = "review.soft-proof";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PROFILE = "profile";
    static constexpr const char* PARAMETER_INTENT = "intent";
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_RESOLUTION = "resolution";
    static constexpr const char* PARAMETER_ALARM_COLOR = "alarmColor";

    /// Identifiers of the rendering intents (497:19614)
    static constexpr const char* INTENT_AUTO = "auto";
    static constexpr const char* INTENT_PERCEPTUAL = "perceptual";
    static constexpr const char* INTENT_RELATIVE_COLORIMETRIC = "relative-colorimetric";
    static constexpr const char* INTENT_SATURATION = "saturation";
    static constexpr const char* INTENT_ABSOLUTE_COLORIMETRIC = "absolute-colorimetric";

    /// Most pages of a run (plan 7.1)
    static constexpr int MAXIMUM_PAGES = 50;

    /// The default of "alarmColor": the swatch "Red" of the color row (Figma 479:11760, 479:15922; RS7FIGMA
    /// M11) - the canonical annotation red #FF3B30 (the light token annotation/red of Pdf4QtLibFluent, the
    /// alarm color of Output Preview); the core has no theme, so it is the value
    static constexpr QRgb DEFAULT_ALARM_COLOR = 0xFFFF3B30;

    /// Keys of the data of the result
    static constexpr const char* DATA_PAGES = "pages";
    static constexpr const char* DATA_PROFILE = "profile";
    static constexpr const char* DATA_INTENT = "intent";

    /// Keys of the data of the result (package P3b): an item of "pages" has "page" (from 1), "share" (% of the
    /// crop box) and "area" (mm²); "profileId" is the identifier of the profile (PDFSoftProofingProfile::id),
    /// "pagesOutOfGamut" the count of the measured pages with a color out of the gamut
    static constexpr const char* DATA_PAGE = "page";
    static constexpr const char* DATA_SHARE = "share";
    static constexpr const char* DATA_AREA = "area";
    static constexpr const char* DATA_PROFILE_ID = "profileId";
    static constexpr const char* DATA_PAGES_OUT_OF_GAMUT = "pagesOutOfGamut";

    /// Most bytes of the first images of a group of pages kept at once (see the class)
    static constexpr qint64 MAXIMUM_GROUP_IMAGE_BYTES = qint64(512) * 1024 * 1024;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;

    /// The default checks and the pages: at least one, at most MAXIMUM_PAGES
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;

    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// A page named by its number follows its page object (as PDF to Images)
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;

    /// The rendering intent of an identifier of the parameter "intent" (Auto for an unknown one)
    static RenderingIntent getRenderingIntent(const QString& intent);

    /// The profile of a run: the profile \p profile of \p profiles (PDFCMSManager::getSoftProofingProfiles), for
    /// an empty \p profile the first available output intent. nullptr and the translated reason, when there is
    /// no such profile or it is not available (its reason, FP-6).
    static const PDFSoftProofingProfile* findProfile(const PDFSoftProofingProfiles& profiles, const QString& profile, QString* errorMessage);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_SOFTPROOF_H
