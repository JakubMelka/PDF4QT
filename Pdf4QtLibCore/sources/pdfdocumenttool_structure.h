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

#ifndef PDFDOCUMENTTOOL_STRUCTURE_H
#define PDFDOCUMENTTOOL_STRUCTURE_H

#include "pdfdocumenttool.h"
#include "pdfoptimizer.h"

#include <vector>

namespace pdf
{

/// The tool Optimize Structure (optimize.structure, Figma 302:4072, 303:4405, 303:4560,
/// 303:4897; plan of the first batch, chapter 2.7): the lossless steps of PDFOptimizer over the
/// objects of the file, each a Bool parameter (at least one is on). A local optimizer per run,
/// the whole file is written (PDFDocumentToolSignatureImpact::LostCopyOnly): the result keeps
/// the version of the input and loses the dead containers of the file and the declaration of
/// PDF/A or PDF/UA. No preview: the size is known after the run.
///
/// Result:
///   Success - the changed document; facts "size", "steps", "time" of the result dialog
///   303:4897 (and "conformance"); the rows of the panel by getResultRows and getStepRows;
///   data: "sizeBefore", "sizeAfter", "savedBytes", "savedPercent", "objectsBefore",
///   "objectsAfter", "stepCount", "stepsRun", "steps" (per parameter: "run", "count"),
///   "recompressedBytesSaved", "durationMilliseconds", "signatureCount", "signatureImpact";
///   NothingToDo - the file is not smaller: the same facts and data, no document;
///   Stopped - canceled, no document.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolStructure : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolStructure)

public:
    static constexpr const char* TOOL_ID = "optimize.structure";

    /// Names of the parameters, one per step of PDFOptimizer in its order
    static constexpr const char* PARAMETER_EMBED_SIMPLE_OBJECTS = "embedSimpleObjects";
    static constexpr const char* PARAMETER_REMOVE_NULL_OBJECTS = "removeNullObjects";
    static constexpr const char* PARAMETER_REMOVE_UNUSED_OBJECTS = "removeUnusedObjects";
    static constexpr const char* PARAMETER_MERGE_IDENTICAL_OBJECTS = "mergeIdenticalObjects";
    static constexpr const char* PARAMETER_SHRINK_OBJECT_STORAGE = "shrinkObjectStorage";
    static constexpr const char* PARAMETER_RECOMPRESS_FLATE_STREAMS = "recompressFlateStreams";

    /// A step of the tool: its parameter, the short texts of the panel (steps 319:7288) and its icon
    struct Step
    {
        const char* parameter;
        const char* title;          ///< "Embed simple objects" (QT_TRANSLATE_NOOP of the tool)
        const char* description;    ///< "Writes small values in place"
        const char* iconName;
        PDFOptimizer::OptimizationFlag flag;
    };

    /// The six steps in the order of the frames and of the optimizer
    static const std::vector<Step>& getSteps();

    /// Translated title and description of a step
    static QString getStepTitle(const Step& step);
    static QString getStepDescription(const Step& step);

    /// Steps of the normalized parameters
    static PDFOptimizer::OptimizationFlags getFlags(const QVariantMap& parameters);

    /// Numbers of the objects of a document (Document 302:4549): the objects of its storage
    /// and the streams among them
    struct Counts
    {
        qint64 objects = 0;
        qint64 streams = 0;
    };
    static Counts getCounts(const PDFDocument* document);

    /// A row of the steps of a finished run (Steps performed 303:5370)
    struct StepRow
    {
        QString parameter;
        QString title;          ///< "Embed simple objects"
        QString text;           ///< "1,204 objects embedded", "Skipped – not selected"
        QString iconName;
        bool isRun = false;
    };

    /// Rows of the steps of a result (from its data, in the current language)
    static std::vector<StepRow> getStepRows(const PDFDocumentToolResult& result);

    /// Rows of the card Result of a finished run (303:5001): Size before, Size after, Saved,
    /// Objects, Time (from its data, in the current language)
    static std::vector<PDFDocumentToolFact> getResultRows(const PDFDocumentToolResult& result);

    /// The report of the run for the clipboard (Copy report 303:5373): the title of the tool,
    /// the rows of Result, the declaration of a standard, the rows of the steps; plain text
    static QString createReport(const PDFDocumentToolResult& result);

    /// Time of a run: "4.2 s" (303:5001) below a minute, then PDFDocumentToolFormat::formatDuration
    static QString formatDuration(qint64 milliseconds);

    /// Saved share with one decimal place: "12.6 %" (303:5001), "0.0 %" when not smaller
    static QString formatSavedPercent(qint64 before, qint64 after);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_STRUCTURE_H
