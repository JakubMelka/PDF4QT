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

#ifndef PDFDOCUMENTTOOL_STATISTICS_H
#define PDFDOCUMENTTOOL_STATISTICS_H

#include "pdfdocumenttool.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdfobjectutils.h"

#include <QJsonObject>

namespace pdf
{

/// The tool Document Statistics (review.statistics, Figma 480:14219 …; plan of step 7 of phase 2, 4.2): what
/// takes up the space of the file - the objects classified by their function (PDFObjectClassifier over
/// PDFObjectWalk: one visited set, the depth, a budget of the nodes; the resources of the pages with the
/// inherited ones, the arrays of /Contents, the forms, the appearances, the patterns, the Type 3 fonts; B190,
/// B205, B206) and by their object type. An analysis (Result::Analysis): the run is the preview of the tool
/// (hasPreview, isPreviewFinal) - computed at the entry and after every change of the document (with a short
/// delay, a running computation canceled, R7-22 (7)); the export writes the data of the last result.
///
/// Parameters: "view" (function, type; only the export and the CSV, K7.20: CSV is the current view, Text, HTML
/// and XML both tables), "format" (none, text, html, xml, csv; default none - the data only).
///
/// Data (QJsonObject of the result): "classes" (rows: "id", "count", "bytes", "share" - the share of the
/// object bytes), "types" (rows: "type", "count", "share" - the share of the values, K7.13), "fileSize"
/// (bytes of the file, -1 for unsaved changes - FD-2), "version", "objectCount", "isFromUnsavedState",
/// "isComplete" (false: the budget of the walk was reached - the fact "Not all objects were classified"),
/// "isRepaired" (the reader restored the document of a damaged file - the row "Repaired when opened", plan 4.2).
/// The size of an object is its serialized size ("object bytes", B186).
///
/// No permission (Q7.2: counts, no content). Package P1a.
///
/// The rows of "classes" are the classes with an object, by their bytes (the largest first; the classes of
/// PDFObjectClassifier, "other" last of the equal ones); "id" is getClassId, "share" the percent of the
/// object bytes. The rows of "types" are the PDF types with a value, by their count; "type" is getTypeId,
/// "share" the percent of all values (K7.13 "Share of values"). Besides the keys above: "totalBytes" (the
/// object bytes of all objects), "valueCount" (all values), "pageCount". A share of nothing is 0 (B186).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolStatistics : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolStatistics)

public:
    static constexpr const char* TOOL_ID = "review.statistics";

    /// Names of the parameters
    static constexpr const char* PARAMETER_VIEW = "view";
    static constexpr const char* PARAMETER_FORMAT = PDFDocumentToolTableWriter::PARAMETER_FORMAT;

    /// Identifiers of the views (480:14833)
    static constexpr const char* VIEW_FUNCTION = "function";
    static constexpr const char* VIEW_TYPE = "type";

    /// Keys of the data of the result
    static constexpr const char* DATA_CLASSES = "classes";
    static constexpr const char* DATA_TYPES = "types";
    static constexpr const char* DATA_FILE_SIZE = "fileSize";
    static constexpr const char* DATA_VERSION = "version";
    static constexpr const char* DATA_OBJECT_COUNT = "objectCount";
    static constexpr const char* DATA_UNSAVED = "isFromUnsavedState";
    static constexpr const char* DATA_COMPLETE = "isComplete";
    static constexpr const char* DATA_TOTAL_BYTES = "totalBytes";
    static constexpr const char* DATA_VALUE_COUNT = "valueCount";
    static constexpr const char* DATA_PAGE_COUNT = "pageCount";
    static constexpr const char* DATA_REPAIRED = "isRepaired";

    /// Keys of a row of "classes" and "types"
    static constexpr const char* ROW_ID = "id";
    static constexpr const char* ROW_TYPE = "type";
    static constexpr const char* ROW_COUNT = "count";
    static constexpr const char* ROW_BYTES = "bytes";
    static constexpr const char* ROW_SHARE = "share";

    /// The data of the statistics of a document (the run; a test and PdfTool call it directly)
    /// \param document The document
    /// \param fileSize Bytes of its file, -1 = unknown (unsaved changes, FD-2)
    /// \param isRepaired The reader restored the document of a damaged file, when it was opened (the input
    ///        isRestored of the tool, PdfTool by its reader; plan 4.2 "Repaired when opened", FD-7)
    /// \param control Cancellation (optional)
    /// \param isCanceled Set to true, when it was canceled (the data are then empty)
    static QJsonObject createData(const PDFDocument* document, qint64 fileSize, bool isRepaired, const PDFOperationControl* control, bool* isCanceled);

    /// The identifier of a class ("content-stream", "file-structure", "other" for None)
    static QString getClassId(PDFObjectClassifier::Type type);

    /// The translated title of a class by its identifier ("Content Stream"; the identifier, when unknown)
    static QString getClassTitle(const QString& id);

    /// The identifier of a PDF type ("dictionary")
    static QString getTypeId(PDFObject::Type type);

    /// The translated title of a PDF type by its identifier ("Dictionary"; the identifier, when unknown)
    static QString getTypeTitle(const QString& id);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual bool hasPreview() const override { return true; }
    virtual bool isPreviewFinal() const override { return true; }
    virtual PDFDocumentToolResult computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The file of an export of the data of a result (plan 4.1: the export never runs the tool again)
    /// \param data The data of the result
    /// \param parameters The view and the format (normalized)
    /// \param options The formatter of the numbers (PDFAppToolValueFormatter in the GUI, the C locale otherwise)
    /// \param baseName Suggested name without the extension
    /// \param errorMessage Translated reason of a failure (optional)
    static PDFDocumentToolOutputFile writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_STATISTICS_H
