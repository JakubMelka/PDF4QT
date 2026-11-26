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
    /// Page number from 1 -> the revision of the page (PDFContentJson::getPageRevision), which
    /// the caller read by the mode list. An operation over an existing node of a page without
    /// its revision is refused.
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

    /// Most characters (UTF-16 code units) of the texts of the document one listing returns: the
    /// values of "text" and "actualText" of its objects together. A densely printed page has
    /// about 50 000 characters and they are listed twice (by the text object and by its runs), so
    /// a call takes several such pages; and a document cannot make a call return more (2000
    /// objects with 2 * MAXIMUM_TEXT_LENGTH characters each would be 262 million). The listing
    /// ends before the object, whose texts do not fit any more, and "nextOffset" continues there;
    /// the first object of a call is always listed (its texts have at most 2 * MAXIMUM_TEXT_LENGTH
    /// characters).
    static constexpr qint64 MAXIMUM_LIST_TEXT_LENGTH = 1048576;

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
        qint64 maximumTextLength = MAXIMUM_LIST_TEXT_LENGTH;    ///< Characters of the texts of the document, which the listing may still return (0 ... MAXIMUM_LIST_TEXT_LENGTH; the first listed object is not limited by it)
    };

    /// The revision of a page: what the mode list returns as "revision" and a script gives back
    /// in "revisions". It covers everything an identifier of a node of the page can name and the
    /// patterns its objects are painted with: a SHA-256 hash of the fingerprint of the container
    /// of the page and, for every loaded container of a form the page reaches through its form
    /// uses (also through forms in forms) and for every tiling pattern named by the /Pattern
    /// resources of the page and of those forms, of its key and its fingerprint. So "p3:57/12"
    /// stops being valid, when the form of the node 57 was rewritten, though nothing of the page
    /// itself changed. The walk over the forms has one visited set and the nesting limit of the
    /// session; it reads only containers, which are loaded, so its value depends on what the
    /// session holds: a form another page has loaded beyond the limits of the loader of this page
    /// is hashed too, a form the session has refused because another page reached it too deep is
    /// missing. The listing and the replay of a script therefore compute it in a session, which
    /// holds the page alone, loaded by the same loader (the forms of a page beyond its limits are
    /// in neither and no identifier can name their nodes).
    /// \param session The session
    /// \param pageIndex Index of the page from 0; its container must be loaded
    /// \returns the revision (32 bytes); empty, when the container of the page is not loaded
    static QByteArray getPageRevision(const PDFContentEditSession* session, PDFInteger pageIndex);

    /// The listing of one page for the mode list: { page, revision, width, height, rotation,
    /// userUnit, objects: [ { id, kind, rect, text?, appearance?, layer?, tag?, readOnly?,
    /// children? } ], nextOffset? }. Text of the document carries "origin": "document".
    ///
    /// "revision" is getPageRevision in hexadecimal digits.
    ///
    /// "rect" is in points of the unrotated default user space of the page, also for an object
    /// inside of a form: such an object is listed below one use of its form, and its box in the
    /// space of the form is mapped by the matrix of that use (the box of the mapped corners: for
    /// a rotated or skewed use it is larger than the object). The object of a form use carries
    /// "matrix": the six numbers [a, b, c, d, e, f], which map the space of the form to the space
    /// of the page (/Matrix of the form, then the current transformation matrix of the use, then
    /// the same of the forms around it). The matrices of the operations stay in the space of the
    /// container of their node: for a node inside of a form the space of that form, so a move in
    /// the page space is converted by "matrix". When the matrix of a use is not known (its node
    /// has no state), the use has no "matrix" and the objects below it carry "space": "form" -
    /// their "rect" is in the space of the form then.
    ///
    /// The window of the listing ends after ListOptions::maximumObjects objects or before the
    /// object, whose texts do not fit into ListOptions::maximumTextLength any more;
    /// "nextOffset" is the offset of the next call then.
    /// \param session The session: it gives the document, the container of the page and the
    ///        loaded containers of the forms (a form, which is not loaded, is listed without
    ///        children)
    /// \param pageIndex Index of the page from 0; its container must be loaded
    /// \param options Paging and depth
    static QJsonObject writePage(const PDFContentEditSession* session, PDFInteger pageIndex, const ListOptions& options);
};

}   // namespace pdf

#endif // PDFCONTENTJSON_H
