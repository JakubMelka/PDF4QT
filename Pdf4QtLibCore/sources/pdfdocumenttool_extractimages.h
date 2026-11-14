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

#ifndef PDFDOCUMENTTOOL_EXTRACTIMAGES_H
#define PDFDOCUMENTTOOL_EXTRACTIMAGES_H

#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool.h"

#include <QDate>
#include <QImage>
#include <QMutex>

#include <map>
#include <optional>
#include <vector>

namespace pdf
{

/// The tool Extract Images (convert.extract-images, Figma 341:7516 ... 360:12757; plan of step 4 of
/// phase 2, 4.5, K4.5, K4.12, owner answers Q4.5, Q4.8, R4-8, R4-11, R4-19; package P1). The logic
/// of PdfTool fetch-images moves here (PdfTool calls it). The images are the items of
/// PDFDocumentImageList with the options of an export: the images of the content of the pages and
/// forms of all layers, inline images too, never a glyph of Type 3, an appearance of an annotation
/// or a mask of another image; the same image is written once - by the reference, then by a
/// fingerprint of the raw stream (filter, parameters, dictionary without /Length, data), nothing
/// decoded (B60). An image is decoded and converted by the color management as on the page, with
/// its mask (SMask, /Mask; an ImageMask is a stencil and is not exported), in the pixels of the
/// source; a format without alpha gets a white background. Decoded one by one in the threads,
/// written in their order, never all held at once.
///
/// Parameters: pages, format, compression, quality, optimizedWrite, progressiveScanWrite (as PDF to
/// Images), exclude (the ids of the images, which are left out, PDFDocumentImageList::Key::toString,
/// separated by commas; never stored), folder, fileName (default "{source_base}_img_#" - # is the
/// number N of the image in the list, a left out number is skipped, K4.12), existingFiles;
/// onlyOutputs (panel-only, Retry N Files, R4-20).
///
/// Result: Files written by the tool: writtenFiles, notWrittenFiles, failedFiles (an image, which
/// cannot be read, is too large for the format or is not written); facts of 341:8462 "files"
/// ("409 PNG · numbered like the images"), "leftOut" ("3 images, by your choice", identical
/// images), "size" (Total size), "time"; with failed files "written", "failed"; Stopped
/// "written", "notExported"; NothingToDo - the selected pages have no image; Failed - every image
/// is left out (exclude); permission bit 5 (catalog). Data "imageCount", "exportedCount",
/// "excludedCount", "duplicateCount", "writtenCount", "notWrittenCount", "failedCount",
/// "sizeTotal", "fileNames", "images" (per exported image: "id", "number" - N of "Image N",
/// "fileName", "pages" - from 1, "width", "height", "streamBytes", "duplicates" - the ids of the
/// identical images exported by it).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolExtractImages : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolExtractImages)

public:
    /// What is exported of the list of the images (the GUI shows it before the run: "409 to
    /// export", "Report_img_3.png" or "left out")
    class PDF4QTLIBCORESHARED_EXPORT ExportPlan
    {
    public:
        /// One exported image
        class Image
        {
        public:
            size_t itemIndex = 0;               ///< Index of the item of the list (the number N of "Image N" is itemIndex + 1)
            QString fileName;                   ///< Name of its file ("#" is its number, K4.12)
            std::vector<size_t> duplicates;     ///< Items of the list, which are the same image (exported once by this one)
        };

        std::vector<Image> images;              ///< The exported images in the order of the list
        std::vector<size_t> excluded;           ///< Items left out by the user (exclude) or by the pages
        std::vector<size_t> stencils;           ///< Image masks (stencils, 8.9.6.2): no picture of their own, never exported
        std::vector<size_t> duplicates;         ///< Items exported by another identical item

        /// The file name of an item, empty = it is not exported (left out, a stencil, a duplicate)
        QString getFileName(size_t itemIndex) const;
    };

    /// Ids of the images separated by commas ("148 0, inline:12 0:3"); a key given twice once
    /// \param errorMessage Translated reason (empty = valid)
    static std::vector<PDFDocumentImageList::Key> parseKeys(const QString& text, QString* errorMessage);

    /// The text of ids of the keys (parseKeys reads it back)
    static QString writeKeys(const std::vector<PDFDocumentImageList::Key>& keys);

    /// The list of the images of an export (the options of an export: the images of all layers, an
    /// image only in hidden layers is flagged, R4-11) - hidden by \p layerStates (the tab,
    /// PDFDocumentToolContext::rendering), without them by OCUsage::Export (MCP, PdfTool)
    static PDFDocumentImageList createImageList(const PDFDocument* document, const std::optional<std::map<PDFObjectReference, OCState>>& layerStates,
                                                const PDFOperationControl* control, PDFProgressSink* progress);

    /// What createExportPlan reads of the images of one list (one version of the document): the
    /// hash of the raw data of each image and the copy of the stream of an inline image. The GUI
    /// makes the plan again at each change of its parameters (RS4APP: the SHA-256 of every stream
    /// each time); the data of the images do not change with the parameters. One cache belongs to
    /// one list - a new list (a new version) gets a new cache. Thread-safe: the jobs of the GUI
    /// may overlap (a canceled job runs to its next check).
    class PDF4QTLIBCORESHARED_EXPORT ExportPlanCache
    {
    public:
        /// What the plan reads of one image
        class Entry
        {
        public:
            QByteArray hash;                        ///< SHA-256 of the raw data of the stream, empty = no stream
            std::optional<PDFStream> inlineStream;  ///< An inline image: the copy of its stream (the walk of its first page), none = not found
        };

        /// The entry of a key: false = not in the cache
        bool find(const PDFDocumentImageList::Key& key, Entry* entry) const;

        /// The cache has the entry of the key
        bool contains(const PDFDocumentImageList::Key& key) const;

        /// Puts the entry of a key into the cache (a computed entry is the same for each job)
        void insert(const PDFDocumentImageList::Key& key, Entry entry);

        /// Number of the entries
        size_t getCount() const;

    private:
        mutable QMutex m_mutex;
        std::map<PDFDocumentImageList::Key, Entry> m_entries;
    };

    /// What the parameters export of the list: the selected pages, exclude, the identical images
    /// (the reference, then the raw stream - its data and its dictionary without /Length, B60) and
    /// the names of the files; std::nullopt and the reason (an invalid template, equal names, an
    /// invalid page selection, an id of exclude, which is not an image)
    /// \param control Cancellation, checked between two images (optional): a canceled plan is
    ///        std::nullopt without a reason
    /// \param cache The hashes of the images of \p list (optional; null = computed for this call)
    static std::optional<ExportPlan> createExportPlan(const PDFDocument* document, const PDFDocumentImageList& list, const QString& sourceFileName,
                                                      const QVariantMap& parameters, const QDate& date, const PDFOperationControl* control,
                                                      ExportPlanCache* cache, QString* errorMessage);

    /// A change of the parameter changes the plan (the pages, exclude, the format - the extension
    /// of the names, the file name); a change of the others (the encoding, the folder, Existing
    /// files) keeps it, the GUI does not make it again
    static bool isExportPlanParameter(const QString& name);

    /// Decodes one item of the list as it is exported: converted by the color management \p cms
    /// (null = the default), with its soft mask or mask as alpha, in the pixels of the source.
    /// A null image and the reason, when it cannot be read or is too large (Copy Image of the GUI).
    static QImage readImage(const PDFDocument* document, const PDFDocumentImageList::Item& item, PDFCMSPointer cms, const PDFOperationControl* control, QString* errorMessage);

    static constexpr const char* TOOL_ID = "convert.extract-images";

    /// Names of the parameters (folder, fileName and existingFiles are of PDFDocumentToolFileOutput)
    static constexpr const char* PARAMETER_PAGES = "pages";
    static constexpr const char* PARAMETER_FORMAT = "format";
    static constexpr const char* PARAMETER_COMPRESSION = "compression";
    static constexpr const char* PARAMETER_QUALITY = "quality";
    static constexpr const char* PARAMETER_OPTIMIZED_WRITE = "optimizedWrite";
    static constexpr const char* PARAMETER_PROGRESSIVE_SCAN_WRITE = "progressiveScanWrite";
    static constexpr const char* PARAMETER_EXCLUDE = "exclude";

    /// Default format and name (341:7516: PNG, "Report_img_#")
    static constexpr const char* DEFAULT_FORMAT = "png";
    static constexpr const char* DEFAULT_FILE_NAME = "{source_base}_img_#";

    /// Longest text of exclude
    static constexpr int MAXIMUM_EXCLUDE_LENGTH = 4 * 1024 * 1024;

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFDocumentToolParameterError> validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
    virtual QVariantMap mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_EXTRACTIMAGES_H
