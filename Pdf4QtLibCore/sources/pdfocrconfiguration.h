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

#ifndef PDFOCRCONFIGURATION_H
#define PDFOCRCONFIGURATION_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace pdf
{
struct PDFOCREngineParameterDescriptor;

/// Model profile (REC-01, LANG-03). For Tesseract, the profiles are the model
/// repositories tessdata_fast, tessdata and tessdata_best.
enum class PDFOCRModelProfile
{
    Fast,
    Standard,
    Best
};

/// Layout type of the page (basic offer of the page segmentation, REC-01).
/// Values are the page segmentation modes of Tesseract (chapter 3.2), other
/// engines map them to their own capabilities.
enum class PDFOCRLayout
{
    OrientationOnly = 0,
    AutomaticWithOrientation = 1,
    SegmentationOnly = 2,
    Automatic = 3,
    SingleColumn = 4,
    VerticalBlock = 5,
    SingleBlock = 6,
    SingleLine = 7,
    SingleWord = 8,
    CircleWord = 9,
    SingleCharacter = 10,
    SparseText = 11,
    SparseTextWithOrientation = 12,
    RawLine = 13
};

/// Binarization mode (IMAGE-04)
enum class PDFOCRBinarization
{
    Automatic,      ///< Engine internal binarization (default)
    Otsu,           ///< Global Otsu thresholding performed by PDF4QT
    AdaptiveOtsu,   ///< Engine adaptive Otsu (if supported)
    Sauvola         ///< Engine Sauvola (if supported)
};

/// Policy of the existing text (chapter 6.2)
enum class PDFOCRExistingTextPolicy
{
    OnlyPagesWithoutText,   ///< Skip pages with existing text, mark mixed pages for decision
    AddInRegions,           ///< Recognize only user defined regions, keep existing text
    ReplaceOwnLayer,        ///< Replace own OCR layer created by PDF4QT
    ReviewOnly              ///< Recognize for review/export only, never write into PDF
};

/// Preprocessing of the working raster (IMAGE-04)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRPreprocessing
{
    /// Manual rotation in degrees (0, 90, 180, 270)
    int rotation = 0;

    /// Automatic orientation detection (with manual override by rotation)
    bool autoOrientation = false;

    /// Small deskew
    bool deskew = false;

    /// Convert to grayscale
    bool grayscale = true;

    PDFOCRBinarization binarization = PDFOCRBinarization::Automatic;

    /// Mild noise removal (median filter)
    bool denoise = false;

    /// Invert light text on dark background
    bool invert = false;

    QJsonObject toJson() const;
    static PDFOCRPreprocessing fromJson(const QJsonObject& object);

    bool operator==(const PDFOCRPreprocessing&) const = default;
};

/// Complete recognition configuration (REC-01..03). Values are typed,
/// validated by PDFOCRConfiguration::validate and by the engine.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRConfiguration
{
    /// Stable engine identifier
    QString engineId = QStringLiteral("tesseract");

    /// Ordered list of languages (engine specific codes, for Tesseract e.g. "ces", "eng")
    QStringList languages;

    PDFOCRModelProfile profile = PDFOCRModelProfile::Fast;

    /// Explicitly selected model set identifier (empty = resolve by profile)
    QString modelSetId;

    PDFOCRLayout layout = PDFOCRLayout::Automatic;

    /// Engine mode (for Tesseract OEM: 0 legacy, 1 LSTM, 2 combined, 3 default)
    int engineMode = 1;

    /// Raster resolution in DPI
    double dpi = 300.0;

    PDFOCRPreprocessing preprocessing;

    /// User words (dictionary hints, never automatic replacement)
    QStringList userWords;

    /// User patterns
    QStringList userPatterns;

    /// Character whitelist (empty = no restriction)
    QString characterWhitelist;

    /// Character blacklist
    QString characterBlacklist;

    /// Review threshold, words with normalized score below this value require review (CONF-03)
    double reviewThreshold = 80.0;

    /// Blank page detection
    bool detectBlankPages = true;

    PDFOCRExistingTextPolicy existingTextPolicy = PDFOCRExistingTextPolicy::OnlyPagesWithoutText;

    /// Store detailed review data (original text, scores) into the document (PDF-10)
    bool keepReviewDataInDocument = false;

    /// Number of OCR workers (JOB-09)
    int workerCount = 2;

    /// Memory budget for rasters in bytes (JOB-10, QA-05)
    qint64 memoryBudget = qint64(1) << 30;

    /// Page timeout in seconds (0 = no timeout)
    int pageTimeoutSeconds = 0;

    /// Engine specific parameters (validated by the engine)
    QVariantMap engineParameters;

    /// Minimal and maximal allowed resolution (IMAGE-01)
    static constexpr double MinimumDpi = 150.0;
    static constexpr double MaximumDpi = 1200.0;

    /// Standard offered resolutions (IMAGE-01)
    static const std::vector<int>& getStandardResolutions();

    /// Validates the configuration (engine independent part). Returns list of errors.
    QStringList validate() const;

    /// Returns true, if the language identifier is acceptable: "ces", "script/Latin",
    /// optionally with the import suffix "ces@<import>". Path separators other than
    /// the "script/" prefix, backslashes and ".." are refused (R11).
    static bool isValidLanguageIdentifier(const QString& language);

    /// Validates the engine parameters against the typed schema declared by the
    /// engine (REC-03). Every parameter must be declared, convertible to the declared
    /// type and inside the declared range. Returns the names of the accepted parameters,
    /// the errors (translated, one per rejected parameter) are filled, if requested.
    static QStringList validateEngineParameters(const QVariantMap& parameters,
                                                const std::vector<PDFOCREngineParameterDescriptor>& descriptors,
                                                QStringList* errors);

    /// Returns true, if the layout is one of the basic layouts (offered without expert knowledge)
    static bool isBasicLayout(PDFOCRLayout layout);

    /// Returns human readable, translated name of the layout
    static QString getLayoutName(PDFOCRLayout layout);

    /// Returns translated description of the layout
    static QString getLayoutDescription(PDFOCRLayout layout);

    /// Returns all layouts
    static const std::vector<PDFOCRLayout>& getLayouts();

    /// Returns all profiles, from the fastest one to the most accurate one
    static const std::vector<PDFOCRModelProfile>& getProfiles();

    /// Returns translated name of the profile
    static QString getProfileName(PDFOCRModelProfile profile);

    /// Returns identifier of the profile ("fast", "standard", "best")
    static QString getProfileIdentifier(PDFOCRModelProfile profile);
    static PDFOCRModelProfile parseProfileIdentifier(const QString& identifier);

    /// Returns the languages joined with '+' (e.g. "ces+eng")
    QString getLanguageString() const { return languages.join(QChar('+')); }

    QJsonObject toJson() const;
    static PDFOCRConfiguration fromJson(const QJsonObject& object);

    /// Returns a variant map with the effective parameters (for the provenance record)
    QVariantMap toParameterMap() const;

    bool operator==(const PDFOCRConfiguration&) const = default;
};

/// Per page override of the configuration (PAGE-06). Empty optionals inherit.
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRPageOverride
{
    std::optional<QStringList> languages;
    std::optional<PDFOCRLayout> layout;
    std::optional<int> rotation;
    std::optional<double> dpi;
    std::optional<bool> autoOrientation;
    std::optional<bool> deskew;

    bool isEmpty() const;

    /// Applies the override to the configuration
    PDFOCRConfiguration apply(PDFOCRConfiguration configuration) const;

    QJsonObject toJson() const;
    static PDFOCRPageOverride fromJson(const QJsonObject& object);

    bool operator==(const PDFOCRPageOverride&) const = default;
};

/// Named profile (REC-02)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRProfile
{
    QString name;
    PDFOCRConfiguration configuration;

    QJsonObject toJson() const;
    static PDFOCRProfile fromJson(const QJsonObject& object);

    bool operator==(const PDFOCRProfile&) const = default;
};

/// Resolves effective configuration: profile -> document job -> page -> region (PAGE-06)
class PDF4QTLIBCORESHARED_EXPORT PDFOCRConfigurationResolver
{
public:
    static PDFOCRConfiguration resolve(const PDFOCRConfiguration& jobConfiguration,
                                       const PDFOCRPageOverride* pageOverride,
                                       const PDFOCRRegionOverride* regionOverride);
};

/// Page selection helper (PAGE-02, PAGE-03). Page ranges are entered as
/// 1-based physical page numbers, indices are 0-based.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRPageSelection
{
public:
    enum class Parity
    {
        All,
        Odd,
        Even
    };

    /// Parses the range text (for example "1, 3-5, 9"), applies the parity filter and
    /// returns sorted unique 0-based page indices. On error, empty vector is returned
    /// and the error message is filled.
    static std::vector<PDFInteger> parseRange(PDFInteger pageCount, const QString& text, Parity parity, QString* errorMessage);

    /// Filters the 0-based indices by the parity of the physical page numbers
    static std::vector<PDFInteger> filterParity(std::vector<PDFInteger> indices, Parity parity);

    /// Returns human readable description of the selection (for example "1-3, 7")
    static QString describe(const std::vector<PDFInteger>& indices);
};

}   // namespace pdf

#endif // PDFOCRCONFIGURATION_H
