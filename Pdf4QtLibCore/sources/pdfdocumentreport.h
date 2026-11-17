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

#ifndef PDFDOCUMENTREPORT_H
#define PDFDOCUMENTREPORT_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdffont.h"
#include "pdfjavascriptscanner.h"
#include "pdfsecurityhandler.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{
class PDFOperationControl;
class PDFProgressSink;

/// The sections of Document Report (484:15472, 9 sections; plan of step 7 of phase 2, 4.4)
enum class PDFDocumentReportSection
{
    BasicInformation,   ///< "basicInformation": Info, version, Tagged, Trapped, form, pages, size, custom keys, encryption, permissions, PDF/A and PDF/UA declarations
    FileHashes,         ///< "fileHashes": MD5, SHA1, SHA256, SHA384, SHA512 of the original file data (not of the disk; Disabled for unsaved changes, FD-2)
    Metadata,           ///< "metadata": the XMP packet as a text (at most MAXIMUM_METADATA_SIZE)
    JavaScript,         ///< "javascript": every script, as a text, never run (PDFJavaScriptScanner, B201 - B203)
    NamedDestinations,  ///< "namedDestinations": /Dests and the name tree /Names /Dests; an unresolved page is "unresolved" (B189)
    PageBoxes,          ///< "pageBoxes": the boxes of the pages in mm with the effective /UserUnit, the unit in the header
    StructureTree,      ///< "structureTree": PDFStructureTree
    Inks,               ///< "inks": PDFInkMapper (process and spot inks)
    Fonts               ///< "fonts": the fonts of every resource (PDFResourceWalk), substitutes, optional character maps (K7.24)
};

/// The basic information of a document (Report Basic information, the Properties page of the
/// application, MCP document.get_info and PdfTool info): one implementation for all of them (R7-5, K7.24)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReportBasicInformation
{
public:
    /// One entry of the Info dictionary
    class InfoEntry
    {
    public:
        QByteArray key;         ///< The key ("Title", a custom key)
        QString value;          ///< Its text, sanitized (control characters replaced, at most MAXIMUM_VALUE_LENGTH)
        QDateTime dateTime;     ///< The value of a date key (CreationDate, ModDate), invalid otherwise
        bool isCustom = false;  ///< Not a key of ISO 32000-2 Table 349
        bool isShortened = false;   ///< The sanitized value differs from the value of the document (line breaks, too long): an editor must not write it back
    };

    /// The entry of the key (nullptr = none)
    const InfoEntry* findInfo(const QByteArray& key) const;

    QString version;                        ///< "1.7", "2.0"
    std::vector<InfoEntry> info;            ///< Standard keys first in the order of Table 349, then at most MAXIMUM_CUSTOM_ENTRIES custom keys
    bool isTagged = false;                  ///< /MarkInfo /Marked true (ISO 32000-2, 14.7.1, Table 353) - the one definition of Tagged (R7-5)
    bool hasStructureTree = false;          ///< /StructTreeRoot exists
    QByteArray trapped;                     ///< /Trapped of Info: "True", "False", "Unknown" (14.11.6), empty = absent (B204: never written as Tagged)
    QString formType;                       ///< "AcroForm", "XFA", "None"
    PDFInteger pageCount = 0;
    QSizeF firstPageSizeMM;                 ///< Size of the first page in mm (with /UserUnit)
    QString paperFormat;                    ///< Name of the standard paper of the first page ("A4"), empty = none
    bool isEncrypted = false;
    QString encryptionMethod;               ///< Filter, revision, method and key length as a text ("Standard, revision 6, AES 256")
    int encryptionRevision = 0;             ///< /R of the handler (the key "encryption-revision" of PdfTool, B176)
    std::vector<std::pair<PDFSecurityHandler::Permission, bool>> permissions;   ///< The 8 permissions of Table 22 and whether each is granted
    QStringList conformance;                ///< Declared standards (PDF/A-2b, PDF/UA-1) from pdfaid / pdfuaid of the XMP
    bool isRepaired = false;                ///< The file was restored when it was opened (FD-7: "Repaired when opened"); the caller sets it

    EncryptionMode encryptionMode = EncryptionMode::None;   ///< The security handler
    int encryptionVersion = 0;              ///< /V of the encryption dictionary (Table 20)
    PDFSecurityHandler::AuthorizationResult authorization = PDFSecurityHandler::AuthorizationResult::NoAuthorizationRequired;    ///< How the document was opened
    bool isMetadataEncrypted = false;       ///< The XMP is encrypted (/EncryptMetadata)
};

/// A named destination (Named destinations, PdfTool info-dests; ISO 32000-2, 12.3.2)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReportDestination
{
public:
    QString name;               ///< The name (a text of the document, sanitized)
    PDFInteger pageIndex = -1;  ///< The page (index from 0), -1 = unresolved (B189: a page, which is not in the document)
    QString type;               ///< The keyword of Table 149 ("XYZ", "Fit", ...), "Named" for a name of another destination, "Invalid"
    std::optional<PDFReal> left;
    std::optional<PDFReal> top;
    std::optional<PDFReal> right;
    std::optional<PDFReal> bottom;
    std::optional<PDFReal> zoom;    ///< The zoom of XYZ, absent for 0 or null (unchanged)
};

/// The boxes of a group of pages, which have the same boxes (Page boxes, PdfTool info-page-boxes; 14.11.2)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReportPageBoxes
{
public:
    std::vector<PDFInteger> pageIndices;    ///< The pages (indices from 0)
    PDFReal userUnit = 1.0;                 ///< The effective /UserUnit (Table 31)
    QRectF mediaBox;                        ///< In mm with the effective /UserUnit (the boxes as written, the defaults of Table 31)
    QRectF cropBox;
    QRectF bleedBox;
    QRectF trimBox;
    QRectF artBox;
};

/// An ink of the document (Inks, PdfTool info-inks)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReportInk
{
public:
    QByteArray name;    ///< The name of the colorant
    QString textName;   ///< Its text
    bool isSpot = false;
    QColor color;       ///< The color in RGB
};

/// One font of the report (Fonts 497:22345, Properties, PdfTool info-fonts)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReportFont
{
public:
    QString name;                       ///< /BaseFont exactly, with the subset prefix (K7.21)
    QString family;                     ///< Family of the descriptor, empty = unknown
    QString type;                       ///< Type ("Type 0 (CID)", "TrueType (8 bit)", ...)
    QString encoding;
    bool isEmbedded = false;
    bool isSubset = false;
    bool hasToUnicode = false;
    QString substitute;                 ///< Font, which substitutes a font not embedded (empty = none)
    PDFObjectReference reference;       ///< The font object (invalid for a direct font)
    std::vector<PDFInteger> pageIndices;    ///< Pages, which use it (PDFResourceWalkResult::getPageIndices)
    FontType fontType = FontType::Invalid;  ///< The type of the font (the callers have their own texts)
    CharacterInfos characterInfos;      ///< The character map of an embedded font, when it was asked for
};

/// The data of Document Report: the sections, which were asked for, read from a document (an untrusted
/// input: every value may be missing or damaged, texts are bounded, no function throws). The tool, the
/// Properties page of the application, MCP and PdfTool info* take their data from here (R7-5, one
/// implementation); the texts of the user interface stay in their callers.
///
/// Package P1b implements it (the move of PdfTool info* and of the data part of PDFAppDocumentPropertiesData,
/// the one definition of Tagged); the skeleton has stubs. P1b may add members to these classes.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentReport
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentReport)

public:
    /// Longest text of a value
    static constexpr int MAXIMUM_VALUE_LENGTH = 4096;

    /// Largest XMP packet, which is reported (bytes; the limit of the Properties page)
    static constexpr qsizetype MAXIMUM_METADATA_SIZE = qsizetype(16) * 1024 * 1024;

    /// Most custom entries of Info
    static constexpr int MAXIMUM_CUSTOM_ENTRIES = 500;

    /// Most fonts reported
    static constexpr int MAXIMUM_FONTS = 10000;

    /// Most rows of a section of many items (named destinations, structure elements, groups of page boxes)
    static constexpr int MAXIMUM_ROWS = 100000;

    /// What is read
    class Options
    {
    public:
        std::vector<PDFDocumentReportSection> sections;    ///< The sections
        std::vector<PDFInteger> pageIndices;                ///< The pages of JavaScript, named destinations and page boxes (the other sections describe the whole document)
        bool isCharacterMapIncluded = false;                ///< The fonts list their character maps
        QByteArray originalData;                            ///< The data of the file of the hashes (empty = no hashes: a document with unsaved changes, FD-2)
        QString fileName;                                   ///< The name of the file (Basic information; empty = not shown)
        bool isRepaired = false;                            ///< The file was restored when it was opened (FD-7)
    };

    /// The basic information of the document (an empty value for no document)
    static PDFDocumentReportBasicInformation getBasicInformation(const PDFDocument* document);

    /// The parts of the basic information, which read more than the dictionaries (a caller in the thread of
    /// the user interface leaves them out: the Properties page)
    enum class BasicInformationPart
    {
        Form,           ///< formType: the interactive form is parsed
        Conformance     ///< conformance: the XMP packet is decoded and read
    };

    /// The basic information of the document with the parts \p parts only (the other values of those parts are empty)
    static PDFDocumentReportBasicInformation getBasicInformation(const PDFDocument* document, std::initializer_list<BasicInformationPart> parts);

    /// The XMP packet as a text; \p hasMetadata false = none; \p errorMessage when it cannot be shown
    /// (decoded by PDFLimitedStreamDecoder with the limit MAXIMUM_METADATA_SIZE: a bomb is never decoded whole)
    static QString getMetadata(const PDFDocument* document, bool* hasMetadata, QString* errorMessage);

    /// The fonts of the pages (every resource of PDFResourceWalk, each font once); empty when it was canceled
    /// \param document The document
    /// \param pageIndices The pages (indices from 0)
    /// \param isCharacterMapIncluded The embedded fonts get their character maps
    /// \param control Cancellation (optional)
    /// \param isLimitReached Not all fonts were read: MAXIMUM_FONTS or the limits of the walk (optional)
    static std::vector<PDFDocumentReportFont> getFonts(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, bool isCharacterMapIncluded, const PDFOperationControl* control,
                                                       bool* isLimitReached = nullptr);

    /// The named destinations (/Dests and /Names /Dests), whose page is one of \p pageIndices; an unresolved
    /// destination only when \p pageIndices are all pages of the document (B189). Sorted by the page, then by the name.
    static std::vector<PDFDocumentReportDestination> getNamedDestinations(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices);

    /// The boxes of the pages, grouped: pages with the same boxes and /UserUnit are one group
    static std::vector<PDFDocumentReportPageBoxes> getPageBoxes(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices);

    /// The process inks (CMYK) and the spot inks of the document (PDFInkMapper)
    /// \param control Cancellation (optional): a canceled scan gives the inks found so far
    static std::vector<PDFDocumentReportInk> getInks(const PDFDocument* document, const PDFOperationControl* control = nullptr);

    /// The hashes of the file: the names of the algorithms ("MD5", "SHA1", "SHA256", "SHA384", "SHA512") and
    /// the hashes in upper-case hexadecimal
    static std::vector<std::pair<QString, QString>> getFileHashes(const QByteArray& data);

    /// The declared conformance of the XMP packet: pdfaid (ISO 19005, "PDF/A-2b") and pdfuaid (ISO 14289,
    /// "PDF/UA-1"); a packet with a document type definition is not read
    static QStringList getConformance(const QString& metadata);

    /// The model of the report of the sections (PDFDocumentToolTableWriter writes it) and its data for MCP
    /// \param document The document
    /// \param options What is read
    /// \param formatter Numbers and dates of the texts (nullptr = the C locale)
    /// \param control Cancellation (optional; a canceled report is empty)
    /// \param progress A step per section (optional)
    /// \param data The JSON data of the sections for MCP (optional): { sectionId: data } - see createModel
    static PDFDocumentToolTableSection createReport(const PDFDocument* document, const Options& options, const PDFDocumentToolValueFormatter* formatter,
                                                    const PDFOperationControl* control, PDFProgressSink* progress, QJsonObject* data);

    /// The data of the sections (createReport) without the model: { sectionId: data }
    static QJsonObject createData(const PDFDocument* document, const Options& options, const PDFOperationControl* control, PDFProgressSink* progress);

    /// The model of the report from its data (createData): the preview of the GUI and an export of a kept result
    /// build it from the data again, never by a new run. The data are of this class; a value of a wrong type
    /// is an empty cell.
    /// \param data The data of the sections
    /// \param formatter Numbers and dates of the texts, which are composed of values (nullptr = the C locale)
    /// \param dateFormat The format of the dates of Basic information
    static PDFDocumentToolTableSection createModel(const QJsonObject& data, const PDFDocumentToolValueFormatter* formatter, PDFDocumentToolDateFormat dateFormat);

    /// Why the section of the document would be empty (484:20505, FD-8: the tile of the section is Disabled
    /// with the reason), an empty text when it has content. JavaScript, Inks and Fonts are not checked (their
    /// scan is a run): an empty text.
    /// \param document The document
    /// \param section The section
    /// \param hasOriginalData The data of the file are known (FileHashes; a document with unsaved changes has none, FD-2)
    static QString getEmptySectionReason(const PDFDocument* document, PDFDocumentReportSection section, bool hasOriginalData);

    /// A text of a document for one line: control characters and line separators replaced by spaces, at
    /// most MAXIMUM_VALUE_LENGTH characters followed by an ellipsis (the one rule of the Properties page)
    static QString sanitize(const QString& text);

    /// Name of the standard paper of a size in millimeters ("A4", either orientation), empty = none
    static QString getPaperFormatName(const QSizeF& sizeMM);

    /// Identifier of a permission of Table 22 in the data ("copyContent")
    static QString getPermissionId(PDFSecurityHandler::Permission permission);

    /// The identifier of a section ("basicInformation") and the section of an identifier
    static QString getSectionId(PDFDocumentReportSection section);
    static std::optional<PDFDocumentReportSection> findSection(const QString& id);

    /// Every section in the order of the panel (484:15472)
    static std::vector<PDFDocumentReportSection> getSections();
};

}   // namespace pdf

#endif // PDFDOCUMENTREPORT_H
