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

#ifndef PDFDOCUMENTTOOL_SPLIT_H
#define PDFDOCUMENTTOOL_SPLIT_H

#include "pdfdocumenttool.h"
#include "pdffilenametemplate.h"
#include "pdfimageoptimizer.h"

#include <optional>

namespace pdf
{

/// What both tools of more files share: the parameters of the panel Output files (Folder, File
/// name, Existing files; plan of step 3 of phase 2, 4.2, R14) and the writing of the parts of a
/// document through the file sink of the context (PDFDocumentSplitter, PDFDocumentToolFileSink).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolFileOutput
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolFileOutput)

public:
    PDFDocumentToolFileOutput() = delete;

    /// Names of the parameters
    static constexpr const char* PARAMETER_FOLDER = "folder";
    static constexpr const char* PARAMETER_FILE_NAME = "fileName";
    static constexpr const char* PARAMETER_EXISTING_FILES = "existingFiles";

    /// Identifiers of Existing files (383:4447)
    static constexpr const char* EXISTING_ASK = "ask";
    static constexpr const char* EXISTING_OVERWRITE = "overwrite";
    static constexpr const char* EXISTING_SKIP = "skip";
    static constexpr const char* EXISTING_RENAME = "rename";

    /// One file of the run: its pages and the values of its name
    struct File
    {
        std::vector<PDFInteger> pages;          ///< Pages of the file (indices from 0, in the order of the document)
        QString title;                          ///< Title of the file ("Introduction", the bookmark), may be empty
        qint64 estimatedSize = -1;              ///< Estimated size in bytes (By file size), -1 = not estimated
        QString fileName;                       ///< Name of the file (createFileNames)
    };

    /// The definitions of Folder, File name (its default \p defaultFileName) and Existing files
    static void addDefinitions(PDFDocumentToolParameterDefinitions* definitions, PDFDocumentToolTranslator translator, const QString& defaultFileName);

    /// The policy of the parameter existingFiles (Ask for an unknown value)
    static PDFDocumentToolFileSink::ExistingFiles getPolicy(const QVariantMap& parameters);

    /// Identifier of a policy (EXISTING_ASK, ...)
    static QString getPolicyId(PDFDocumentToolFileSink::ExistingFiles policy);

    /// The names of the files by the template of the parameters (the source name of the
    /// first input, the outputs numbered from 1, the first page of each); std::nullopt and
    /// the reason (383:9088 "Every file would be named Handbook.pdf") for an invalid template,
    /// equal names or more than PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT files
    /// \param files The files, their names are filled
    /// \param sourceFileName Name of the source file ("Handbook.pdf")
    /// \param parameters The normalized parameters
    /// \param date Date of {date}, invalid = today
    static bool createFileNames(std::vector<File>* files, const QString& sourceFileName, const QVariantMap& parameters, const QDate& date, QString* errorMessage);

    /// The names of the files by a template (as above; PdfTool separate gives its own template)
    static bool createFileNames(std::vector<File>* files, const PDFFileNameTemplate& nameTemplate, const QString& sourceFileName, const QDate& date, QString* errorMessage);

    /// Writes the files: each one a part of the first document of the context
    /// (PDFDocumentSplitter), each given to the file sink of the context in their order. A sink,
    /// which is stopped, or a cancellation ends the run as Stopped with the files written
    /// before. Facts "files" ("4 · Handbook_1.pdf … Handbook_4.pdf"), "size", "notWritten",
    /// "attachments" (Q6), "tags" (Q5 a), "signatures", "links"; data "fileCount",
    /// "writtenCount", "notWrittenCount", "sizeTotal", "fileNames".
    /// \param context The context (its fileSink is required)
    /// \param files The files with their names
    /// \param imageSettings Optimization of the images of each file, std::nullopt = off
    /// \param control Cancellation (optional)
    /// \param progress Progress (optional): a step per file
    static PDFDocumentToolResult writeFiles(const PDFDocumentToolContext& context, const std::vector<File>& files,
                                            const std::optional<PDFImageOptimizer::Settings>& imageSettings,
                                            const PDFOperationControl* control, PDFProgressSink* progress);

    /// Errors of the permissions of the first input of a tool, whose files are new unencrypted
    /// documents (owner answer Q2 of step 2, Q4 of step 3: Assemble, and the owner password,
    /// when anything else is restricted)
    static std::vector<PDFDocumentToolParameterError> checkPermissions(const PDFDocumentToolContext& context);

    /// Text of a list of names: "Handbook_1.pdf … Handbook_4.pdf", one name alone, two "a, b"
    static QString formatFileNames(const QStringList& fileNames);
};

/// The tool Split (pages.split, Figma 383:3315 ... 403:29978; plan of step 3 of phase 2, 5.6,
/// R2, R3, Q4, Q6): the pages of the selection are separated into files by a rule, each file a
/// new document (PDFDocumentSplitter), written by the file sink of the context.
///
/// Rules: every-page (a file per page); every-n-pages ("pagesPerFile", 1 to 10 000); at-pages
/// ("cuts": the page numbers, where a new file starts, PDFPageRangeList; sorted, duplicates and
/// the page 1 left out); at-bookmarks ("bookmarks": "top-level", or the numbers of the
/// bookmarks from 1 in the order of the outline, PDFOutlineBreakPoints - the pages before the
/// first bookmark join the first file); by-size ("sizeMB": the files are about this size, by an
/// estimate of the objects of each page - an object shared by more pages of a file counts once,
/// 383:7775 "The size is approximate"); odd-even (two files: the odd and the even pages).
///
/// Result: Files (R3) - the written files; facts of PDFDocumentToolFileOutput::writeFiles;
/// data "rule", "fileCount", "pageCount" and the data of writeFiles. Stopped - with the files
/// written before. Failed - an invalid rule, template, equal names (383:9088), more than
/// 10 000 files, no permission (Q4), no file sink.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSplit : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolSplit)

public:
    static constexpr const char* TOOL_ID = "pages.split";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_RULE = "rule";
    static constexpr const char* PARAMETER_PAGES_PER_FILE = "pagesPerFile";
    static constexpr const char* PARAMETER_CUTS = "cuts";
    static constexpr const char* PARAMETER_BOOKMARKS = "bookmarks";
    static constexpr const char* PARAMETER_SIZE_MB = "sizeMB";
    static constexpr const char* PARAMETER_OPTIMIZE_IMAGES = "optimizeImages";

    /// Identifiers of the rules (383:4447)
    static constexpr const char* RULE_EVERY_PAGE = "every-page";
    static constexpr const char* RULE_EVERY_N_PAGES = "every-n-pages";
    static constexpr const char* RULE_AT_PAGES = "at-pages";
    static constexpr const char* RULE_AT_BOOKMARKS = "at-bookmarks";
    static constexpr const char* RULE_BY_SIZE = "by-size";
    static constexpr const char* RULE_ODD_EVEN = "odd-even";

    /// The value of bookmarks: every bookmark of the top level
    static constexpr const char* BOOKMARKS_TOP_LEVEL = "top-level";

    /// Most pages per file
    static constexpr qint64 MAXIMUM_PAGES_PER_FILE = 10000;

    /// The files of the parameters: their pages and titles (names not filled); std::nullopt and
    /// the translated reason for invalid parameters (the GUI shows the files before the run)
    /// \param document The document
    /// \param parameters The normalized parameters
    static std::optional<std::vector<PDFDocumentToolFileOutput::File>> createFiles(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    /// Estimated sizes of the pages for By file size: the bytes of the objects, which the page
    /// reaches (its content, resources, annotations - never another page or the page tree); an
    /// object reached by more pages of one file counts at the first of them. The greedy grouping
    /// of the pages into files of at most \p limit bytes (a file has at least one page).
    /// \param document The document
    /// \param pages The pages (indices from 0, sorted)
    /// \param limit Size of a file in bytes
    static std::vector<PDFDocumentToolFileOutput::File> groupBySize(const PDFDocument* document, const std::vector<PDFInteger>& pages, qint64 limit);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

/// The tool Extract Pages (pages.extract, Figma 385:9405 ... 403:31676; plan of step 3 of
/// phase 2, 5.6, K49): the selected pages, in the order of the document, as a new document
/// ("one-file": PDFDocumentMerger of one source with the other pages left out - R3 and Q18 of
/// step 2) or as a file per page ("one-file-per-page": PDFDocumentSplitter, written by the file
/// sink). The source is never changed; a locked document may be extracted (4.7).
///
/// Result: one-file - a New document with its written bytes, facts "pages" ("7 · 1–3, 8,
/// 10–12"), "source" ("Handbook.pdf · not changed"), "tags", "signatures", "conformance";
/// one-file-per-page - Files (R3) as Split. Failed - no page, an invalid selection, no
/// permission (Q4).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolExtract : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolExtract)

public:
    static constexpr const char* TOOL_ID = "pages.extract";

    /// Names of the parameters
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_OUTPUT = "output";

    /// Identifiers of Output (385:10213)
    static constexpr const char* OUTPUT_ONE_FILE = "one-file";
    static constexpr const char* OUTPUT_FILE_PER_PAGE = "one-file-per-page";

    /// Indices of the selected pages (from 0, the order of the document); empty and the
    /// reason for an invalid or empty selection
    static std::vector<PDFInteger> getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage);

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolResultKind getResultKind(const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_SPLIT_H
