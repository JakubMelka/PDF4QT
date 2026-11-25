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

#ifndef PDFDOCUMENTTOOL_CONTENT_H
#define PDFDOCUMENTTOOL_CONTENT_H

#include "pdfdocumenttool.h"

namespace pdf
{

// Edit Content, stage E0, package P05. Plan: zadani_jadro.md 4.6 (table of parameters, execute);
// zadani.md KE.6, KE.8, KE.9, QE.3, QE.4, RE-15; zadani_overeni.md 7.1.

/// Edit Content (edit.content). The tool of the core: it writes the work of an editing session
/// (the parameter session of the panels) or replays a script of operations (the parameter
/// operations of MCP) and writes the result; in the mode list it only reads the structure of the
/// content of pages.
///
/// **The tool is not registered in stage E0** (no registry names it; the page of the tool leads to
/// Page Not Found). Stage E1 registers it behind the developer switch.
///
/// Parameters (the names are PARAMETER_*; zadani_jadro.md 4.6):
///   - mode (Enum apply, list): list only reads; getResultKind tells them apart;
///   - session (Value PDFContentEditSnapshotPointer; the panels give it, not stored, not in MCP);
///   - operations (String, structured data up to MAXIMUM_OPERATIONS_LENGTH; the script of
///     PDFContentJson::readScript); checkGivenParameters refuses apply with none or both of
///     session and operations;
///   - imagePaths (files of images for add_image / replace_image, named by their index; from
///     stage E3);
///   - pages, depth, offset, maximumObjects (mode list: the pages, the depth of the listing and
///     its paging);
///   - skipPages (PageSelection, panels only: pages left out after an error of writing).
///
/// Run (apply): the snapshot of session, or a session of its own over the input document, which
/// loads the touched containers in this thread and replays the script. Every container of the
/// snapshot and every page named in the revisions of the script is compared with the document of
/// the run by its fingerprint; a mismatch is Failed "The document changed in the meantime". An
/// error of an operation of the script is Failed with its path, nothing is written. A page, which
/// cannot be written, makes the run Failed with the structured errors of the pages in the data
/// (DATA_PAGE_ERRORS); the GUI offers to skip the page. The result is the changed document with
/// PDFModifiedDocument::Reset (KE.9).
///
/// Permission: Modify (bit 4 of ISO 32000-2, Table 22) for apply, none for list. apply is a change
/// of the pages (isPageChange): a certified document loses its certification with a warning; a
/// signed one is saved incrementally (QE.3). The declarations of PDF/A and PDF/UA are removed and
/// the result says so (QE.2).
///
/// Threads and ownership: one instance serves all runs (PDFDocumentTool): every function is const
/// and reentrant. A run works in the thread of its runner with its arguments only; the snapshot of
/// the parameter session is immutable and shared, a session of a script is a local object of the
/// run.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolContent : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolContent)

public:
    static constexpr const char* TOOL_ID = "edit.content";

    /// Names of the parameters
    static constexpr const char* PARAMETER_MODE = "mode";
    static constexpr const char* PARAMETER_SESSION = "session";
    static constexpr const char* PARAMETER_OPERATIONS = "operations";
    static constexpr const char* PARAMETER_IMAGE_PATHS = "imagePaths";
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_DEPTH = "depth";
    static constexpr const char* PARAMETER_OFFSET = "offset";
    static constexpr const char* PARAMETER_MAXIMUM_OBJECTS = "maximumObjects";
    static constexpr const char* PARAMETER_SKIP_PAGES = "skipPages";

    /// Values of the parameter mode
    static constexpr const char* MODE_APPLY = "apply";
    static constexpr const char* MODE_LIST = "list";

    /// Longest script: the longest structured String of MCP
    static constexpr int MAXIMUM_OPERATIONS_LENGTH = PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH;

    /// Names of the data of the result (MCP)
    static constexpr const char* DATA_PAGES_CHANGED = "pagesChanged";       ///< apply: numbers of the written pages from 1
    static constexpr const char* DATA_OBJECTS_ADDED = "objectsAdded";
    static constexpr const char* DATA_OBJECTS_EDITED = "objectsEdited";
    static constexpr const char* DATA_OBJECTS_REMOVED = "objectsRemoved";
    static constexpr const char* DATA_LAYERS = "layers";
    static constexpr const char* DATA_STRUCTURE = "structure";
    static constexpr const char* DATA_FONTS = "fonts";
    static constexpr const char* DATA_PAGE_ERRORS = "pageErrors";           ///< Failed run of apply: [ { page, errors: [ { id, text } ] } ]
    static constexpr const char* DATA_PAGES = "pages";                      ///< list: the listings of the pages (PDFContentJson::writePage)

    /// Names of the facts of the result dialog (the frame edit-content-v2-done)
    static constexpr const char* FACT_PAGES_CHANGED = "pagesChanged";
    static constexpr const char* FACT_OBJECTS = "objects";                  ///< Objects added / edited / removed
    static constexpr const char* FACT_LAYERS = "layers";
    static constexpr const char* FACT_STRUCTURE = "structure";
    static constexpr const char* FACT_FONTS = "fonts";
    static constexpr const char* FACT_CONFORMANCE = "conformance";          ///< The removed declarations of PDF/A and PDF/UA
    static constexpr const char* FACT_NOT_TAGGED = "notTagged";             ///< Objects without a tag in a tagged document

    PDFDocumentToolContent() = default;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual PDFDocumentToolResultKind getResultKind(const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolResultKind> getPossibleResultKinds() const override;
    virtual bool isPageChange(const QVariantMap& parameters) const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const override;
    virtual std::vector<PDFDocumentToolParameterError> checkGivenParameters(const QVariantMap& given) const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_CONTENT_H
