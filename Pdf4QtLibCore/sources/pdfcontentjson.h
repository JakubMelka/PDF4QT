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

#ifndef PDFCONTENTJSON_H
#define PDFCONTENTJSON_H

#include "pdfcontentcommands.h"
#include "pdfcontentnode.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <map>
#include <optional>
#include <vector>

namespace pdf
{
class PDFContentEditSession;
class PDFDocument;

// Edit Content, stage E0, package P05. Plan: zadani_jadro.md 4.6 (script, mode list);
// zadani_overeni.md 7.1 (rules of the operations: identifiers, revisions, coordinates, colors).
// The JSON of MCP stays English (keys and enumerated values); only error texts are translated.

/// A script of operations: the parameter "operations" of the tool. Threads and ownership: a
/// value owned by the caller (implicitly shared JSON objects); it points to nothing.
struct PDFContentScript
{
    /// Page number from 1 -> the revision (fingerprint) of the page, which the caller read by
    /// the mode list. An operation over an existing node of a page without its revision is
    /// refused.
    std::map<PDFInteger, QByteArray> revisions;

    /// The operations in their order ("type" and the fields of the command)
    std::vector<QJsonObject> items;
};

/// Reads and writes the JSON of the tool: identifiers, values, the script and the listing.
///
/// Identifiers (zadani_overeni.md 7.1): a node of a page is "p<page>:<node>" with the page
/// number from 1 ("p3:57"), a node inside of a form used by a node of the page
/// "p3:57/12" (the node 57 is the FormUse, 12 the node of the container of the form).
///
/// Threads and ownership: static and reentrant.
class PDF4QTLIBCORESHARED_EXPORT PDFContentJson
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFContentJson)

public:
    PDFContentJson() = delete;

    /// Limits of a script (zadani_overeni.md 6.2)
    static constexpr int MAXIMUM_OPERATION_COUNT = 500;
    static constexpr int MAXIMUM_CHANGED_PAGE_COUNT = 20;
    static constexpr int MAXIMUM_TEXT_LENGTH = 65536;

    /// Limits of a listing
    static constexpr int DEFAULT_LIST_OBJECT_COUNT = 500;
    static constexpr int MAXIMUM_LIST_OBJECT_COUNT = 2000;
    static constexpr int DEFAULT_LIST_DEPTH = 4;
    static constexpr int MAXIMUM_LIST_DEPTH = 16;
    static constexpr int MAXIMUM_LIST_PAGE_COUNT = 20;

    /// Keys of a script
    static constexpr const char* KEY_REVISIONS = "revisions";
    static constexpr const char* KEY_ITEMS = "items";
    static constexpr const char* KEY_TYPE = "type";
    static constexpr const char* KEY_ID = "id";
    static constexpr const char* KEY_AS = "as";     ///< A temporary name of a node created by an operation, for the later operations of the same script: the replay names the first added node of the executed command (PDFContentEditSession::setScriptName), readNodeId resolves the name

    /// Reads a script: strict (an unknown key of the top level, a wrong type, too many items are
    /// errors with their path "operations.items[3]: ...").
    /// \returns no value with at least one error
    static std::optional<PDFContentScript> readScript(const QString& text, std::vector<QString>* errors);

    /// Writes a script (the commands of an undo stack as operations: "replaying the script gives
    /// the live model")
    static QString writeScript(const PDFContentScript& script);

    /// The identifier of a node of a page, or of a node inside of the forms used by a node of
    /// the page: "p3:57" is the path [57], "p3:57/12" the path [57, 12]. Each item but the last
    /// is a FormUse node, the next item is a node of the container of that form; forms in forms
    /// continue the path. A cell of a tiling pattern has no identifier in stage E0 (stage E4).
    /// \param pageIndex Index of the page from 0 (written as the page number from 1)
    /// \param path The nodes from the page to the node; not empty
    static QString writeNodeId(PDFInteger pageIndex, const std::vector<PDFContentNodeId>& path);

    /// Reads an identifier of a node against the containers of a session, or a temporary name
    /// of the script (KEY_AS) through PDFContentEditSession::getScriptName.
    /// \returns a reference, which is not valid, with \p errorText for a wrong text, a page out
    ///          of range, a container, which is not loaded, or a node, which does not exist
    static PDFContentNodeReference readNodeId(const QString& text, const PDFContentEditSession* session, QString* errorText);

    /// A matrix as six numbers [a, b, c, d, e, f]; no value for anything else or a value, which
    /// is not finite
    static QJsonArray writeMatrix(const QTransform& matrix);
    static std::optional<QTransform> readMatrix(const QJsonValue& value);

    /// A rectangle as { "left", "bottom", "right", "top" } in points of the unrotated default
    /// user space
    static QJsonObject writeRect(const QRectF& rect);
    static std::optional<QRectF> readRect(const QJsonValue& value);

    /// A color as { "space": ..., "values": [...] }; the short form "#rrggbb" is DeviceRGB
    static QJsonValue writeColor(const PDFContentColorValue& color);
    static std::optional<PDFContentColorValue> readColor(const QJsonValue& value, QString* errorText);

    /// The name of a node kind ("text", "path", "image", "form", "group", ...)
    static QString writeNodeKind(PDFContentNodeKind kind);

    /// Options of a listing (a plain value)
    struct ListOptions
    {
        int depth = DEFAULT_LIST_DEPTH;                     ///< 1 ... MAXIMUM_LIST_DEPTH
        PDFInteger offset = 0;                              ///< Objects of the page to skip (paging)
        int maximumObjects = DEFAULT_LIST_OBJECT_COUNT;     ///< 1 ... MAXIMUM_LIST_OBJECT_COUNT
    };

    /// The listing of one page for the mode list: { page, revision, width, height, rotation,
    /// userUnit, objects: [ { id, kind, rect, text?, appearance?, layer?, tag?, readOnly?,
    /// children? } ], nextOffset? }. Text of the document carries "origin": "document".
    /// \param session The session: it gives the document, the container of the page and the
    ///        loaded containers of the forms (a form, which is not loaded, is listed without
    ///        children)
    /// \param pageIndex Index of the page from 0; its container must be loaded
    /// \param options Paging and depth
    static QJsonObject writePage(const PDFContentEditSession* session, PDFInteger pageIndex, const ListOptions& options);
};

}   // namespace pdf

#endif // PDFCONTENTJSON_H
