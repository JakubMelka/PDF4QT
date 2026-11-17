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

#ifndef PDFDOCUMENTTOOL_REPORT_H
#define PDFDOCUMENTTOOL_REPORT_H

#include "pdfdocumentreport.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttooltablewriter.h"

namespace pdf
{

/// The tool Document Report (review.report, Figma 484:15472 …; plan of step 7 of phase 2, 4.4, R7-5): a report
/// of the chosen sections of PDFDocumentReport (Text, HTML, XML) or the structure of the document as XML
/// (PDFDocumentXmlExport). An analysis: the run writes the file of the export into the result (one output,
/// the result dialog saves it - 484:18811, FileOnly controls; R7-13), MCP returns the data and the text.
///
/// Parameters: "kind" (report, structure-xml), a Bool per section (the ids of
/// PDFDocumentReport::getSectionId: basicInformation, fileHashes, metadata, javascript, namedDestinations,
/// pageBoxes, structureTree, inks, fonts), "pages" (PageSelection: JavaScript, named destinations, page boxes),
/// "format" (none, text, html, xml; default html), "dateFormat" (short, long, iso, rfc2822; PDFAppLocale in the
/// GUI, the C locale in PdfTool and MCP - R7-14), "characterMaps" (Bool), the XML options "exportStreams",
/// "streamsAsText", "indent", "alwaysBinaryStrings" (484:18196).
///
/// Permissions (owner answer Q7.2): the report of the open document is reading it; the content exported from
/// it is an extraction - bit 5 (CopyContent, Table 22) with the owner password offered, when the run includes
/// the JavaScript or the XMP metadata (a report), or the data of the streams (Structure as XML with
/// exportStreams; a stream written as hexadecimal is the same content). MCP refuses such a run without bit 5
/// (permission_denied, plan 7.1).
///
/// Package P1b implements it; the skeleton has the parameters, the permissions and a stub run (Failed).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolReport : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolReport)

public:
    static constexpr const char* TOOL_ID = "review.report";

    /// Names of the parameters (the sections are named by PDFDocumentReport::getSectionId)
    static constexpr const char* PARAMETER_KIND = "kind";
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_FORMAT = PDFDocumentToolTableWriter::PARAMETER_FORMAT;
    static constexpr const char* PARAMETER_DATE_FORMAT = "dateFormat";
    static constexpr const char* PARAMETER_CHARACTER_MAPS = "characterMaps";
    static constexpr const char* PARAMETER_EXPORT_STREAMS = "exportStreams";
    static constexpr const char* PARAMETER_STREAMS_AS_TEXT = "streamsAsText";
    static constexpr const char* PARAMETER_INDENT = "indent";
    static constexpr const char* PARAMETER_ALWAYS_BINARY_STRINGS = "alwaysBinaryStrings";

    /// Identifiers of the kinds (484:15916)
    static constexpr const char* KIND_REPORT = "report";
    static constexpr const char* KIND_STRUCTURE_XML = "structure-xml";

    /// Keys of the data of the result: "kind" (report, structure-xml), "sections" (a report: the data of
    /// PDFDocumentReport::createData, { sectionId: data }), "text" (Structure as XML: the beginning of the
    /// written XML, at most PREVIEW_MAXIMUM_LENGTH characters cut at the end of a line - the preview of
    /// 484:18196), "size" (Structure as XML: the bytes of the file)
    static constexpr const char* DATA_KIND = "kind";
    static constexpr const char* DATA_SECTIONS = "sections";
    static constexpr const char* DATA_TEXT = "text";
    static constexpr const char* DATA_SIZE = "size";

    /// Longest preview of Structure as XML in the data (characters)
    static constexpr int PREVIEW_MAXIMUM_LENGTH = 64 * 1024;

    /// The sections, which are included by default (484:15472: 6 of 9)
    static bool isSectionIncludedByDefault(PDFDocumentReportSection section);

    /// The sections included by the normalized parameters
    static std::vector<PDFDocumentReportSection> getSections(const QVariantMap& parameters);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;

    /// A report: the data of the sections and - with a format - its file (one output, written in the C
    /// locale: the GUI writes its export with its own formatter by writeOutput from the data, never by a
    /// new run). Structure as XML: the XML streamed into a temporary file (never the whole file in the
    /// memory, plan 4.4), its output and the preview in the data.
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;

    /// The file of an export of the data of a result (the preview of the GUI exports what it shows): the
    /// report of the data in the format of \p parameters with the date format of \p parameters (when it
    /// has one, else of \p options); XML in the C locale and ISO 8601 always. Structure as XML has no
    /// such file (its XML is written by the run from the document): an error.
    static PDFDocumentToolOutputFile writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage);

    /// The model of the report of the data of a result (the preview of the GUI; the dates of Basic
    /// information by \p dateFormat, the composed numbers by \p formatter - nullptr = the C locale)
    static PDFDocumentToolTableSection createModel(const QJsonObject& data, const PDFDocumentToolValueFormatter* formatter, PDFDocumentToolDateFormat dateFormat);

    /// The suggested name of the file of the result without its extension ("Project_Report - report")
    static QString getBaseName(const QString& fileName, const QVariantMap& parameters);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_REPORT_H
