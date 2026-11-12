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

#ifndef PDFDOCUMENTASSEMBLYFILE_H
#define PDFDOCUMENTASSEMBLYFILE_H

#include "pdfglobal.h"
#include "pdfdocumentassembly.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace pdf
{

/// The file of an assembly ".pdf4qtassembly" (plan of step 3b of phase 2, 4.3, 6 with RB7, RB8;
/// package M1 - the core, package M4 - the application: opening, the dialog of the sources, the
/// recent assemblies). The skeleton declares it, package M1 defines it.
///
/// Contract:
///  - JSON in UTF-8: "format" "PDF4QT Assembly" (another one: "This is not an assembly file"),
///    "version" 1 (higher: "This assembly was saved by a newer version of PDF4QT"), "sources"
///    (id, kind, name, path, relativePath, size, modified ISO 8601, sha256, pageCount; an image
///    with format and its data in base64), "items" (id, name, group flag, pages as the text of the
///    runs of the plan with the items, the names and the crop - read by the own code of this
///    class with the limits, RB7), "checkpoints" (id, name, time, items), "output" (the values of
///    the output parameters of PDFDocumentToolManage). Nothing of passwords, of the bin or of the
///    undo history.
///  - read never trusts the bytes: the limits below are checked before and during the parsing
///    (the size before parsing, the depth of the JSON after it, the pages of the items and of the
///    checkpoints before a range is expanded), duplicate ids (of the sources, the items, the
///    checkpoints) are an error, a page out of the
///    range of its source, negative and non-numeric values are a problem (the page goes to the
///    bin as by a changed source) or an error with its text. The paths are only read: whether a
///    source exists, has changed or is on another computer (RB8: never touched automatically) is
///    checked by the application (M4), not here.
///  - Reentrant; the application reads and hashes in one job (QtConcurrent::run, owner 2026-11-09).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentAssemblyFile
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentAssemblyFile)

public:
    PDFDocumentAssemblyFile() = delete;

    /// Value of the key "format"
    static constexpr const char* FORMAT_NAME = "PDF4QT Assembly";

    /// Value of the key "version" written by this version
    static constexpr int VERSION = 1;

    /// Suffix of the file (without the dot)
    static constexpr const char* FILE_SUFFIX = "pdf4qtassembly";

    /// Pages of a file: the pages of the items and of all checkpoints together, 16 times the pages
    /// of an assembly (owner 2026-11-12, plan of step 3b chapter 13 point 3); each checkpoint has at
    /// most PDFDocumentAssembly::MAXIMUM_PAGE_COUNT pages, as the items. A range of ten characters
    /// is a million pages, so the pages are counted before any range is expanded: a file over the
    /// limit is refused without allocating its pages.
    static constexpr qint64 MAXIMUM_FILE_PAGE_COUNT = 16 * PDFDocumentAssembly::MAXIMUM_PAGE_COUNT;

    /// Pages of all checkpoints together, which Save checkpoint creates (canCreateCheckpoint): the
    /// file keeps the room of an assembly of PDFDocumentAssembly::MAXIMUM_PAGE_COUNT pages, so the
    /// items may grow to their limit after the checkpoints (a restore of a checkpoint, Add) and the
    /// file is still read - what the application saves, it always opens again
    static constexpr qint64 MAXIMUM_CHECKPOINT_PAGE_COUNT = MAXIMUM_FILE_PAGE_COUNT - PDFDocumentAssembly::MAXIMUM_PAGE_COUNT;

    /// Limits of a read (plan 6 with RB8: 256 MB, JSON depth 16, the limits of the assembly)
    class Limits
    {
    public:
        qint64 maximumFileSize = qint64(256) * 1024 * 1024;
        int maximumDepth = 16;
        int maximumSourceCount = PDFDocumentAssembly::MAXIMUM_SOURCE_COUNT;
        int maximumItemCount = PDFDocumentAssembly::MAXIMUM_ITEM_COUNT;
        qint64 maximumPageCount = PDFDocumentAssembly::MAXIMUM_PAGE_COUNT;     ///< Pages of the items (the assembly)
        qint64 maximumFilePageCount = MAXIMUM_FILE_PAGE_COUNT;                ///< Pages of the items and of all checkpoints together
        int maximumCheckpointCount = PDFDocumentAssembly::MAXIMUM_CHECKPOINT_COUNT;
        int maximumNameLength = PDFDocumentAssembly::MAXIMUM_NAME_LENGTH;
    };

    /// The content of the file
    class Content
    {
    public:
        PDFDocumentAssembly assembly;                               ///< Sources and items; after a read the bin holds only the pages over the page count of their source (a problem says it), the file has no bin
        std::vector<PDFDocumentAssembly::Checkpoint> checkpoints;   ///< The checkpoints (RB7: not in the assembly)
        QVariantMap outputParameters;                               ///< The values of the parameters output, outline, optimizeImages, folder, fileName, existingFiles
    };

    /// A problem of a read, which does not refuse the file (the dialog 387:16980 and the bin)
    class Problem
    {
    public:
        QString sourceId;       ///< The source of the problem, empty = the file
        QString text;           ///< Translated text
    };

    /// The result of a read
    class ReadResult
    {
    public:
        std::optional<Content> content;     ///< The content, std::nullopt = refused (errorMessage)
        std::vector<Problem> problems;      ///< Problems of an accepted file
        QString errorMessage;               ///< Translated reason of a refusal
    };

    /// Reads the bytes of a file (see the class)
    static ReadResult read(const QByteArray& data, const Limits& limits);

    /// The bytes of the file of \p content; \p folder is the folder of the file (the relative
    /// paths of the sources)
    static QByteArray write(const Content& content, const QString& folder);

    /// Save checkpoint (B22, B23): whether a checkpoint of the items of \p assembly may be added to
    /// \p checkpoints - at most PDFDocumentAssembly::MAXIMUM_CHECKPOINT_COUNT checkpoints and
    /// MAXIMUM_CHECKPOINT_PAGE_COUNT pages of them together, the new one included; else false and
    /// the translated reason. The GUI asks before PDFDocumentAssembly::createCheckpoint.
    static bool canCreateCheckpoint(const PDFDocumentAssembly& assembly, const std::vector<PDFDocumentAssembly::Checkpoint>& checkpoints, QString* errorMessage);

    /// Whether read with the default limits reads the file of \p content again by its pages (the
    /// items, each checkpoint, all of them together); else false and the reason of read. Save
    /// Assembly asks before writing: a file of other checkpoints (written by hand) is not saved
    /// over the limit.
    static bool checkPageCount(const Content& content, QString* errorMessage);
};

}   // namespace pdf

#endif // PDFDOCUMENTASSEMBLYFILE_H
