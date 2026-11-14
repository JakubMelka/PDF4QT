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

#ifndef PDFDOCUMENTTOOL_GRAYSCALE_H
#define PDFDOCUMENTTOOL_GRAYSCALE_H

#include "pdfdocumenttool_optimizeimages.h"

namespace pdf
{

/// The tool Convert Images to Grayscale (optimize.grayscale, Figma 308:6911, 320:7812, 308:7341;
/// plan of step 4 of phase 2, 4.3, owner answers Q4.4, Q4.15, R4-5, R4-20; package P1): a preset
/// of Optimize Images in the core - colorMode grayscale, Settings::onlyColorImages (the color by
/// the declaration, PDFDocumentImageList::isColorImage), one gray profile. Keep original if larger
/// chooses only the smallest gray encoding; a color image is always converted, unless it is
/// excluded (Q4.15 a). Its own catalog id, MCP action and help topic; no "All settings" (Q4.4).
///
/// Parameters: goal, keepOriginalIfLarger, preserveTransparency, the profile without a prefix
/// (algorithm, targetDpi - default 0, the resolution unchanged, 308:6911 -, resample, jpegQuality -
/// default 85 -, jpeg2000Rate, pngPredictor) and exclude: the ids of the excluded images
/// (PDFDocumentImageList::Key::toString) separated by commas (R4-20), never stored.
///
/// Result: ChangeDocument (Keep; LostCopyOnly, Q4.14); the result of PDFDocumentToolOptimizeImages
/// with the facts of 308:7341: "imagesSize" (the color images, "18.9 MB → 11.6 MB (39 % smaller)"),
/// "converted" ("285 · excluded 1 · skipped 126"), "problems" only when an image cannot be read;
/// the status of a gray or bitonal image in "images" is "skipped". NothingToDo: no color image
/// is converted (none, or all excluded).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolGrayscale : public PDFDocumentToolOptimizeImages
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolGrayscale)

public:
    static constexpr const char* TOOL_ID = "optimize.grayscale";

    /// Names of the parameters (besides the options and the profile of the base class)
    static constexpr const char* PARAMETER_EXCLUDE = "exclude";

    /// The settings of the optimizer of the normalized parameters: colorMode Grayscale,
    /// onlyColorImages, the one profile (every profile of the settings, the image resolves to the gray one)
    static PDFImageOptimizer::Settings createGrayscaleSettings(const QVariantMap& parameters);

    /// The excluded images of the text of "exclude" (ids separated by commas) as exceptions "off"
    /// \param text The text
    /// \param errorMessage Translated reason of an error (empty = valid)
    static std::vector<ImageException> parseExclude(const QString& text, QString* errorMessage);

    /// The text of "exclude" of the keys (parseExclude reads it back)
    static QString writeExclude(const std::vector<PDFDocumentImageList::Key>& keys);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_GRAYSCALE_H
