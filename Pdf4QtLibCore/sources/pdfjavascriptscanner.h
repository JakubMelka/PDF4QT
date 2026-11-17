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

#ifndef PDFJAVASCRIPTSCANNER_H
#define PDFJAVASCRIPTSCANNER_H

#include "pdfdocument.h"

#include <tuple>

namespace pdf
{
class PDFOperationControl;

struct PDFJavaScriptEntry
{
    enum class Type
    {
        Invalid,
        Document,       ///< /OpenAction and /AA of the catalog (ISO 32000-2, 12.3.3, 12.6.3 Table 200)
        Named,          ///< The name tree /Names /JavaScript (7.7.4, 12.6.4.17)
        Form,           ///< The actions of the fields of the interactive form (12.7.4, /AA Table 199)
        Page,           ///< /AA of a page (Table 198)
        Annotation,     ///< /A and /AA of an annotation of a page (Table 166, Table 197)
        Outline,        ///< /A of an item of the outline (12.3.3, Table 151)
        Xfa             ///< A <script> of the XFA form (/AcroForm /XFA, 12.7.9)
    };

    explicit PDFJavaScriptEntry() = default;
    explicit PDFJavaScriptEntry(Type type, PDFInteger pageIndex, QString javaScript) :
        type(type), pageIndex(pageIndex), javaScript(javaScript)
    {

    }

    bool operator <(const PDFJavaScriptEntry& other) const
    {
        return std::tie(type, pageIndex, name, trigger, javaScript) < std::tie(other.type, other.pageIndex, other.name, other.trigger, other.javaScript);
    }

    bool operator==(const PDFJavaScriptEntry&) const = default;

    Type type = Type::Invalid;
    PDFInteger pageIndex = -1;
    QString javaScript;
    QString name;                       ///< Name of the owner: the named script, the field (/T), the title of the outline item, the subtype of the annotation (bounded, sanitized)
    QString trigger;                    ///< What runs it: "OpenAction", "A", a key of /AA ("WC", "K", "O"), the activity of an XFA event; an action of a /Next chain has the trigger of its first action
    PDFObjectReference actionReference; ///< The action object (invalid for a direct action or an XFA script)
    bool isTruncated = false;           ///< The script is longer than PDFJavaScriptScanner::MAXIMUM_SCRIPT_LENGTH, only its beginning is in javaScript
};

/// Scans a document for its scripts: the JavaScript (and Rendition) actions and the scripts of an XFA form.
/// The scripts are only read, never run.
///
/// The walk is over the raw objects (plan of step 7 of phase 2, R7-4, B201 - B203): /OpenAction and /AA of
/// the catalog, the name tree /Names /JavaScript, the outline (/First, /Next of the items, their /A), the
/// fields of /AcroForm (/Fields, /Kids, /A, /AA), /AA of the pages and /A, /AA of their annotations - and of
/// every action its chain /Next (12.6.2, Table 196: a dictionary or an array of them). The parser of the actions
/// (PDFAction) does not read /Next (Q7.19, NOTES.txt), so the scanner never uses it. Every chain is walked by
/// one PDFObjectWalk of the whole scan (an action shared by two links, a cycle of /Next: entered once), the
/// containers (outline items, fields, annotations) by another one; both are iterative (a chain of 100 000
/// actions never overflows the stack), with a depth limit of the nesting and the budget of the nodes.
///
/// Reentrant: the document is read only.
class PDF4QTLIBCORESHARED_EXPORT PDFJavaScriptScanner
{
public:
    explicit PDFJavaScriptScanner(const PDFDocument* document);

    /// Most entries of a scan (a hostile file with millions of annotations with scripts)
    static constexpr int MAXIMUM_ENTRIES = 100000;

    /// Longest script, which is kept whole (characters)
    static constexpr int MAXIMUM_SCRIPT_LENGTH = 1024 * 1024;

    /// Largest decoded stream of a script or of the XFA form (bytes, PDFLimitedStreamDecoder)
    static constexpr qint64 MAXIMUM_STREAM_SIZE = qint64(64) * 1024 * 1024;

    /// Deepest chain of /Next of one action (the walk is iterative, the limit only bounds the work)
    static constexpr int MAXIMUM_ACTION_DEPTH = 1 << 20;

    enum Option
    {
        AllPages        = 0x0001,   ///< Scan all pages
        FindFirstOnly   = 0x0002,   ///< Return only first javascript found
        ScanDocument    = 0x0004,   ///< Scan document related actions (/OpenAction, /AA of the catalog)
        ScanNamed       = 0x0008,   ///< Scan named javascript in catalog
        ScanForm        = 0x0010,   ///< Scan javascript in form actions
        ScanPage        = 0x0020,   ///< Scan javascript of pages and of their annotations
        Optimize        = 0x0040,   ///< Remove redundant entries
        ScanOutline     = 0x0080,   ///< Scan the actions of the outline items (B203)
        ScanXfa         = 0x0100,   ///< Scan the scripts of the XFA form (SECURITY_TODO 61, R7-22 (4))

        ScanMask = ScanDocument | ScanNamed | ScanForm | ScanPage | ScanOutline | ScanXfa
    };
    Q_DECLARE_FLAGS(Options, Option)

    using Entries = std::vector<PDFJavaScriptEntry>;

    /// The result of a scan
    class Result
    {
    public:
        Entries entries;                ///< The scripts: by their type and page, each in the order of the document
        bool isLimitReached = false;    ///< Not everything was read (MAXIMUM_ENTRIES, the depth or the budget of a walk)
        bool isCanceled = false;        ///< The scan was canceled (the entries are incomplete)
        bool hasXfa = false;            ///< The document has an XFA form
    };

    /// Scans document for javascript actions using flags
    Entries scan(const std::vector<PDFInteger>& pages, Options options) const;

    /// Scans the document (see the class)
    /// \param pages The pages (indices from 0) of ScanPage without AllPages; an invalid index is skipped
    /// \param options What is scanned
    /// \param control Cancellation (optional)
    Result scanDocument(const std::vector<PDFInteger>& pages, Options options, const PDFOperationControl* control) const;

    /// Returns true, if document has any java script action. Calling
    /// this function can be slow.
    bool hasJavaScript() const;

private:
    const PDFDocument* m_document;
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFJavaScriptScanner::Options)

#endif // PDFJAVASCRIPTSCANNER_H
