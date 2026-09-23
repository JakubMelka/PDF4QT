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

#ifndef PDFOCRPROJECT_H
#define PDFOCRPROJECT_H

#include "pdfglobal.h"
#include "pdfocrmodel.h"
#include "pdfocrconfiguration.h"

#include <QJsonObject>

#include <map>

namespace pdf
{

/// Identity of the input document (EXPORT-04)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRDocumentIdentity
{
    /// File name of the document (may be empty for unsaved documents)
    QString fileName;

    /// Hash of the source data of the document (may be empty)
    QByteArray sourceHash;

    /// Fingerprint of the document (computed from the page fingerprints)
    QByteArray fingerprint;

    /// Page count
    PDFInteger pageCount = 0;

    /// Document is encrypted (EXPORT-05)
    bool isEncrypted = false;

    QJsonObject toJson() const;
    static PDFOCRDocumentIdentity fromJson(const QJsonObject& object);

    bool operator==(const PDFOCRDocumentIdentity&) const = default;
};

/// Serializable OCR project (EXPORT-03, EXPORT-04, EXPORT-05)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRProject
{
    static constexpr int FORMAT_VERSION = 1;
    static constexpr const char* FORMAT_IDENTIFIER = "pdf4qt-ocr-project";
    static constexpr const char* FILE_EXTENSION = "pdf4qt-ocr";

    PDFOCRDocumentIdentity document;
    PDFOCRConfiguration configuration;
    std::map<PDFInteger, PDFOCRPageOverride> pageOverrides;
    std::map<PDFInteger, PDFOCRPageResult> pages;

    /// Pages selected for the recognition
    std::vector<PDFInteger> selectedPages;

    QDateTime created;
    QDateTime modified;
    QString application;

    /// Project contains the original recognitions (historical texts) of the words
    bool containsOriginalTexts = true;

    /// Project contains page previews (not in P0)
    bool containsPreviews = false;
};

/// Flags for the serialization of the page result
enum class PDFOCRSerializationFlag
{
    None = 0,
    ReviewData = 1,     ///< Original texts, confidences and review states
    Analysis = 2,       ///< Page analysis
    Geometry = 4,       ///< Page geometry, provenance and orientation
    Regions = 8,        ///< Regions
    All = ReviewData | Analysis | Geometry | Regions
};
Q_DECLARE_FLAGS(PDFOCRSerializationFlags, PDFOCRSerializationFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(PDFOCRSerializationFlags)

/// Serializer of the project and of the page results. The format is JSON,
/// no executable code is ever deserialized.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRProjectSerializer
{
public:
    static QJsonObject quadToJson(const PDFOCRQuad& quad);
    static PDFOCRQuad quadFromJson(const QJsonValue& value);

    static QJsonObject confidenceToJson(const PDFOCRConfidence& confidence);
    static PDFOCRConfidence confidenceFromJson(const QJsonValue& value);

    static QJsonObject wordToJson(const PDFOCRWord& word, PDFOCRSerializationFlags flags);
    static PDFOCRWord wordFromJson(const QJsonObject& object);

    static QJsonObject lineToJson(const PDFOCRLine& line, PDFOCRSerializationFlags flags);
    static PDFOCRLine lineFromJson(const QJsonObject& object);

    static QJsonObject blockToJson(const PDFOCRBlock& block, PDFOCRSerializationFlags flags);
    static PDFOCRBlock blockFromJson(const QJsonObject& object);

    static QJsonObject regionToJson(const PDFOCRRegion& region);
    static PDFOCRRegion regionFromJson(const QJsonObject& object);

    static QJsonObject geometryToJson(const PDFOCRPageGeometry& geometry);
    static PDFOCRPageGeometry geometryFromJson(const QJsonObject& object);

    static QJsonObject provenanceToJson(const PDFOCRProvenance& provenance);
    static PDFOCRProvenance provenanceFromJson(const QJsonObject& object);

    static QJsonObject analysisToJson(const PDFOCRPageAnalysis& analysis);
    static PDFOCRPageAnalysis analysisFromJson(const QJsonObject& object);

    static QJsonObject errorToJson(const PDFOCRError& error);
    static PDFOCRError errorFromJson(const QJsonObject& object);

    static QJsonObject orientationToJson(const PDFOCROrientation& orientation);
    static PDFOCROrientation orientationFromJson(const QJsonObject& object);

    static QJsonObject pageResultToJson(const PDFOCRPageResult& result, PDFOCRSerializationFlags flags);
    static PDFOCRPageResult pageResultFromJson(const QJsonObject& object);

    /// Loads the page result and enforces the limits of the data model while
    /// parsing (DATA-03, OPS-05): at most PDFOCRValidator::MaximumWordsPerPage words,
    /// PDFOCRValidator::MaximumTextLength characters of a word text and
    /// PDFOCRValidator::MaximumRegionsPerPage regions. Returns false, if a limit
    /// is exceeded; the error message names the limit.
    static bool pageResultFromJson(const QJsonObject& object, PDFOCRPageResult& result, QString* errorMessage);

    static QJsonObject projectToJson(const PDFOCRProject& project);

    /// Loads project from JSON. Returns false, if the format is invalid or
    /// a limit of the data model is exceeded (at most PDFOCRValidator::MaximumPages
    /// pages, see pageResultFromJson for the limits of a page).
    static bool projectFromJson(const QJsonObject& object, PDFOCRProject& project, QString* errorMessage);

    /// Saves project to file. Returns false on error.
    static bool save(const PDFOCRProject& project, const QString& fileName, QString* errorMessage);

    /// Loads project from file. Returns false on error. A file larger than
    /// PDFOCRValidator::MaximumProjectFileSize is refused before it is read (OPS-05).
    static bool load(const QString& fileName, PDFOCRProject& project, QString* errorMessage);

    static QByteArray toBytes(const PDFOCRProject& project);
    static bool fromBytes(const QByteArray& data, PDFOCRProject& project, QString* errorMessage);

    /// Result of the comparison of the project with the document (EXPORT-04)
    struct MatchResult
    {
        bool documentMatches = false;
        std::vector<PDFInteger> matchingPages;
        std::vector<PDFInteger> changedPages;
        std::vector<PDFInteger> missingPages;
    };

    /// Compares the project with the document identity and page fingerprints
    static MatchResult match(const PDFOCRProject& project, const PDFOCRDocumentIdentity& identity, const std::function<QByteArray(PDFInteger)>& pageFingerprintGetter);
};

/// Text export (EXPORT-01, EXPORT-02)
class PDF4QTLIBCORESHARED_EXPORT PDFOCRTextExporter
{
public:
    enum class PageSeparator
    {
        None,
        FormFeed,
        Label
    };

    struct Options
    {
        /// Keep the line breaks of the recognized lines
        bool preserveLines = true;

        /// Separator of the pages
        PageSeparator pageSeparator = PageSeparator::Label;

        /// Normalize the text to NFC (offered at export, EDIT-08)
        bool normalizeNFC = false;

        /// Join words hyphenated at the line end (EDIT-10, optional transformation)
        bool joinHyphenatedWords = false;

        /// Export only reviewed (confirmed/modified) words
        bool onlyReviewed = false;
    };

    struct Report
    {
        std::vector<PDFInteger> exportedPages;
        std::vector<PDFInteger> skippedPages;

        /// Human readable descriptions of pages (physical number and label)
        QStringList pageDescriptions;

        /// Human readable descriptions of the skipped pages with the reason
        /// ("page <physical number> (<label>): <reason>"), one item per item
        /// of skippedPages (EXPORT-02)
        QStringList skippedDescriptions;

        /// Human readable descriptions of the exported pages without text
        /// (state NoText), with the reason (EXPORT-02)
        QStringList noTextDescriptions;

        /// Region order per page (human readable)
        QStringList regionOrders;

        /// Number of exported words
        int wordCount = 0;
    };

    /// Returns text of the page (current corrected text, reading order)
    static QString getPageText(const PDFOCRPageResult& result, const Options& options);

    /// Returns human readable description of the state of the page with its
    /// reason (skip reason, error message), for the export report (EXPORT-02)
    static QString getPageStateDescription(const PDFOCRPageResult& result);

    /// Exports text of the pages
    static QString exportText(const std::vector<const PDFOCRPageResult*>& pages, const Options& options, Report* report);

    /// Writes UTF-8 text file
    static bool writeTextFile(const QString& fileName, const QString& text, QString* errorMessage);

    /// Joins words hyphenated at the line end
    static QString joinHyphenatedLines(const QString& text);
};

}   // namespace pdf

#endif // PDFOCRPROJECT_H
