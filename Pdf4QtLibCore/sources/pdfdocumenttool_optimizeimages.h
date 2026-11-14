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

#ifndef PDFDOCUMENTTOOL_OPTIMIZEIMAGES_H
#define PDFDOCUMENTTOOL_OPTIMIZEIMAGES_H

#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool.h"
#include "pdfimageoptimizer.h"

namespace pdf
{

/// The tool Optimize Images (optimize.images, Figma 297:3135, 299:3362, 301:3594, 301:4019; plan of
/// step 4 of phase 2, 4.2, 7, R4-4, R4-7, R4-22; package P1). It recompresses the images of the
/// document by PDFImageOptimizer: the images of all layers (R4-4), never an inline image (R4-8),
/// never an appearance of an annotation; a shared mask is written as a new object (B62). The
/// preview is the result (isPreviewFinal, the measuring of Compress, Q4.9): the measuring runs the
/// optimizer, its per-image results fill the rows of the list and the Summary.
///
/// Parameters (the ids are the fixed interface of step 4, 9.3; the parameter system has no object
/// type, so the profiles of chapter 7 are flattened into "<profile><Field>"):
///  - mode (auto | custom), colorMode (auto | preserve | color | grayscale | bitonal), goal
///    (prefer-quality | minimum-size), keepOriginalIfLarger, preserveTransparency;
///  - the profiles color*, gray*, bitonal*: Algorithm, TargetDpi (0 = unchanged), Resample,
///    JpegQuality (not bitonal; exactJpegQuality - the number of the panel is the number in the
///    file), Jpeg2000Rate (not bitonal), PngPredictor; bitonalThreshold (-1 = automatic);
///  - images: the exceptions of the images (IMAGES syntax below), never stored.
///
/// Syntax of "images": exceptions separated by ';', each "<key>:off" (the image keeps its stream,
/// "excluded") or "<key>:" followed by settings "name=value" separated by ',' (its own profile,
/// "override"): mode (auto | custom), colorMode, goal (prefer-quality | minimum-size),
/// keepOriginalIfLarger and preserveTransparency (true | false), algorithm, targetDpi, resample,
/// jpegQuality, jpeg2000Rate, pngPredictor, threshold (an integer or "auto"); the key is
/// PDFDocumentImageList::Key::toString of an image XObject ("148 0"). Example:
/// "148 0:off; 150 0:mode=custom,colorMode=bitonal,goal=minimum-size,algorithm=jbig2,targetDpi=300".
///
/// The fields of an override apply to every profile of the image (an image resolves to one of
/// them); mode, colorMode, goal and the two options replace the global ones for the image (owner
/// 14. 11., 301:3594 "Mode: Custom"); a setting, which an override does not name, is the global
/// one. An inline image is always kept (R4-8): only "<key>:off" is accepted for it.
///
/// Result: ChangeDocument (Keep; LostCopyOnly - a signed document only as a copy written whole,
/// Q4.14); the declaration of PDF/A and PDF/UA removed and said (as Compress).
///   Success - the changed document (at least one image was written anew), facts of 301:4019:
///   "imagesSize" (Images size "21.3 MB → 3.4 MB (84 % smaller)" - all images of the pages),
///   "recompressed" ("398 · kept original 12 · excluded 2"), "problems" (images, which cannot be
///   read or are too large), "spotColors" when a recompressed image had a Separation or DeviceN
///   space (plan 5: "N images with spot colors were converted"), "conformance" when a declaration
///   was removed; data "imagesBefore", "imagesAfter", "imagesRecompressed", "imagesKept",
///   "imagesExcluded", "imagesSkipped", "imagesProblems", "imagesSpotColors",
///   "durationMilliseconds", the sizes of the file (finishWholeFileResult) and
///   "images": an object per image of the list in its order - "id" (PDFDocumentImageList::Key),
///   "status" (IMAGE_STATUS_*), "originalBytes", "newBytes" (the rows "1.2 MB → 310 KB", Q4.9);
///   NothingToDo - no image gets a new encoding: the same facts and data, no document;
///   Stopped - canceled, no document.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolOptimizeImages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolOptimizeImages)

public:
    /// Status of an image in the data "images" of the result
    static constexpr const char* IMAGE_STATUS_RECOMPRESSED = "recompressed";    ///< A new encoding is written
    static constexpr const char* IMAGE_STATUS_KEPT = "kept";                    ///< The new encoding was not smaller (Keep original if larger), or the colors cannot be written exactly
    static constexpr const char* IMAGE_STATUS_EXCLUDED = "excluded";            ///< An exception "off"
    static constexpr const char* IMAGE_STATUS_SKIPPED = "skipped";              ///< Grayscale: the image is gray or bitonal by its declaration (R4-5)
    static constexpr const char* IMAGE_STATUS_INLINE = "inline";                ///< An inline image, never changed (R4-8, "inline · kept")
    static constexpr const char* IMAGE_STATUS_TOO_LARGE = "too-large";          ///< Not decoded, its pixels would be too large (plan 6)
    static constexpr const char* IMAGE_STATUS_CANNOT_BE_READ = "cannot-be-read"; ///< The image gives no pixels

    /// Names of the settings of an exception of "images" (mode, goal and the two options have the
    /// names and the values of the global parameters)
    static constexpr const char* EXCEPTION_MODE = "mode";
    static constexpr const char* EXCEPTION_COLOR_MODE = "colorMode";
    static constexpr const char* EXCEPTION_GOAL = "goal";
    static constexpr const char* EXCEPTION_KEEP_ORIGINAL = "keepOriginalIfLarger";
    static constexpr const char* EXCEPTION_PRESERVE_TRANSPARENCY = "preserveTransparency";
    static constexpr const char* EXCEPTION_ALGORITHM = "algorithm";
    static constexpr const char* EXCEPTION_TARGET_DPI = "targetDpi";
    static constexpr const char* EXCEPTION_RESAMPLE = "resample";
    static constexpr const char* EXCEPTION_JPEG_QUALITY = "jpegQuality";
    static constexpr const char* EXCEPTION_JPEG2000_RATE = "jpeg2000Rate";
    static constexpr const char* EXCEPTION_PNG_PREDICTOR = "pngPredictor";
    static constexpr const char* EXCEPTION_THRESHOLD = "threshold";

    /// Which tool runs the optimization
    enum class Purpose
    {
        OptimizeImages,     ///< The size (a run, where no image gets a new encoding, is NothingToDo)
        Grayscale           ///< The conversion of the color images (Q4.15 a; nothing to convert is NothingToDo)
    };

    /// One exception of "images" (excluded, or its own settings - the override of 301:3594). A value.
    class PDF4QTLIBCORESHARED_EXPORT ImageException
    {
    public:
        PDFDocumentImageList::Key key;
        bool enabled = true;        ///< false = "off": the image keeps its stream
        QVariantMap settings;       ///< Its own settings by EXCEPTION_* (strings of the ids, integers, a real, a bool; threshold -1 = auto); empty = none

        bool operator==(const ImageException& other) const { return key == other.key && enabled == other.enabled && settings == other.settings; }
    };

    /// Reads the text of "images" (the syntax above).
    /// \param text The text
    /// \param errorMessage Translated reason of the first error (empty = valid)
    /// \returns The exceptions in their order; a key given twice keeps its last exception
    static std::vector<ImageException> parseImageExceptions(const QString& text, QString* errorMessage);

    /// The text of "images" of the exceptions (parseImageExceptions reads it back the same)
    static QString writeImageExceptions(const std::vector<ImageException>& exceptions);

    /// The settings of the optimizer of the normalized parameters (exactJpegQuality, the profiles,
    /// the mode and the color; the exceptions are not a part of them)
    static PDFImageOptimizer::Settings createSettings(const QVariantMap& parameters);

    /// The settings of one image with its exception (the global ones without settings): its mode,
    /// color, goal, options and profile fields replace the global ones
    static PDFImageOptimizer::Settings createImageSettings(const PDFImageOptimizer::Settings& settings, const ImageException& exception);

    /// The overrides of the optimizer of the exceptions of image XObjects (an inline image is
    /// always kept, it needs none)
    static PDFImageOptimizer::ImageOverrides createOverrides(const PDFImageOptimizer::Settings& settings, const std::vector<ImageException>& exceptions);

    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    static constexpr const char* TOOL_ID = "optimize.images";

    /// Names of the parameters
    static constexpr const char* PARAMETER_MODE = "mode";
    static constexpr const char* PARAMETER_COLOR_MODE = "colorMode";
    static constexpr const char* PARAMETER_GOAL = "goal";
    static constexpr const char* PARAMETER_KEEP_ORIGINAL = "keepOriginalIfLarger";
    static constexpr const char* PARAMETER_PRESERVE_TRANSPARENCY = "preserveTransparency";
    static constexpr const char* PARAMETER_IMAGES = "images";
    static constexpr const char* PARAMETER_BITONAL_THRESHOLD = "bitonalThreshold";

    /// Prefixes of the profiles (chapter 7: colorProfile, grayProfile, bitonalProfile)
    static constexpr const char* PROFILE_COLOR = "color";
    static constexpr const char* PROFILE_GRAY = "gray";
    static constexpr const char* PROFILE_BITONAL = "bitonal";

    /// Fields of a profile: the name of the parameter is the prefix + the field ("colorTargetDpi")
    static constexpr const char* FIELD_ALGORITHM = "Algorithm";
    static constexpr const char* FIELD_TARGET_DPI = "TargetDpi";
    static constexpr const char* FIELD_RESAMPLE = "Resample";
    static constexpr const char* FIELD_JPEG_QUALITY = "JpegQuality";
    static constexpr const char* FIELD_JPEG2000_RATE = "Jpeg2000Rate";
    static constexpr const char* FIELD_PNG_PREDICTOR = "PngPredictor";

    /// Identifiers of Mode (299:3362)
    static constexpr const char* MODE_AUTO = "auto";
    static constexpr const char* MODE_CUSTOM = "custom";

    /// Identifiers of Color (299:3362)
    static constexpr const char* COLOR_AUTO = "auto";
    static constexpr const char* COLOR_PRESERVE = "preserve";
    static constexpr const char* COLOR_COLOR = "color";
    static constexpr const char* COLOR_GRAYSCALE = "grayscale";
    static constexpr const char* COLOR_BITONAL = "bitonal";

    /// Identifiers of Goal (299:3362; the same as Compress)
    static constexpr const char* GOAL_PREFER_QUALITY = "prefer-quality";
    static constexpr const char* GOAL_MINIMUM_SIZE = "minimum-size";

    /// Identifiers of Algorithm (297:3135 Settings editor)
    static constexpr const char* ALGORITHM_AUTO = "auto";
    static constexpr const char* ALGORITHM_FLATE = "flate";
    static constexpr const char* ALGORITHM_JPEG = "jpeg";
    static constexpr const char* ALGORITHM_JPEG2000 = "jpeg2000";
    static constexpr const char* ALGORITHM_RUN_LENGTH = "runlength";
    static constexpr const char* ALGORITHM_CCITT_G4 = "ccitt-g4";
    static constexpr const char* ALGORITHM_JBIG2 = "jbig2";

    /// Identifiers of Resample
    static constexpr const char* RESAMPLE_NEAREST = "nearest";
    static constexpr const char* RESAMPLE_BILINEAR = "bilinear";
    static constexpr const char* RESAMPLE_BICUBIC = "bicubic";
    static constexpr const char* RESAMPLE_LANCZOS = "lanczos";

    /// The value "off" of an exception and "auto" of a threshold of "images"
    static constexpr const char* EXCEPTION_OFF = "off";
    static constexpr const char* THRESHOLD_AUTO = "auto";

    /// Largest Target DPI of a profile
    static constexpr int MAXIMUM_TARGET_DPI = 2400;

    /// Longest text of "images" (an exception of each of 100 000 images)
    static constexpr int MAXIMUM_IMAGES_LENGTH = 4 * 1024 * 1024;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual bool hasPreview() const override { return true; }
    virtual bool isPreviewFinal() const override { return true; }
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The name of a parameter of a profile ("color" + "TargetDpi" = "colorTargetDpi")
    static QString getProfileParameterName(const char* profile, const char* field);

protected:
    /// The definitions of one profile (the fields of the profile; the bitonal one without JPEG,
    /// JPEG 2000 and with the threshold)
    /// \param definitions Definitions, to which they are appended
    /// \param profile PROFILE_COLOR, PROFILE_GRAY or PROFILE_BITONAL; nullptr = the one profile of Grayscale (no prefix)
    /// \param defaultDpi Target DPI of the default
    void addProfileDefinitions(PDFDocumentToolParameterDefinitions* definitions, const char* profile, int defaultDpi) const;

    /// The definitions of Goal, Keep original if larger and Preserve transparency (299:3362, 320:7812;
    /// Grayscale says its own meaning of Keep original if larger, Q4.15 a, owner 14. 11.)
    void addOptionDefinitions(PDFDocumentToolParameterDefinitions* definitions, Purpose purpose) const;

    /// The checks of the exceptions shared by both tools: the syntax, and every key is an image
    /// XObject of the document (an inline key: its page or form exists)
    /// \param document The document
    /// \param exceptions The exceptions
    /// \param errorMessage Translated reason
    static bool checkImageExceptions(const PDFDocument* document, const std::vector<ImageException>& exceptions, QString* errorMessage);

    /// The run of both tools (see the classes): the list of the images, the optimizer, the facts
    /// and the document of the whole file
    PDFDocumentToolResult runOptimization(const PDFDocumentToolContext& context, const PDFImageOptimizer::Settings& settings, const std::vector<ImageException>& exceptions,
                                          Purpose purpose, const PDFOperationControl* control, PDFProgressSink* progress) const;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_OPTIMIZEIMAGES_H
