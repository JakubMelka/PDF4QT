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

#ifndef PDFCONTENTCOMMANDS_H
#define PDFCONTENTCOMMANDS_H

#include "pdfcontentnode.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{
class PDFContentEditSession;

// Edit Content, stage E0, package P03 (session, undo, basic commands). Plan: zadani_jadro.md 4.5
// (table of commands), 4.4 (R-1, R-2, R-3, R-5, R-6), 4.12 (consistency of the structure). The
// commands of the later stages live in their own files pdfcontentcommands_<area>.*, which
// register their JSON types here (PDFContentCommandFactory::registerType).

// Threads and ownership of the plain types of this header (PDFContentNodeReference,
// PDFContentChange): values, which point to nothing (keys and identifiers only); copied freely.
// A change is told by a signal of the session and describes the model of that moment.

/// A node of a container
struct PDFContentNodeReference
{
    PDFContentContainerKey container;
    PDFContentNodeId node;

    bool isValid() const { return container.isValid() && node.isValid(); }
    auto operator<=>(const PDFContentNodeReference&) const = default;
};

/// What a command changed: told by PDFContentEditSession::modelChanged after the model is in
/// its new state
struct PDFContentChange
{
    std::vector<PDFContentContainerKey> containers;     ///< Containers with a new version
    std::vector<PDFContentNodeReference> addedNodes;
    std::vector<PDFContentNodeReference> removedNodes;
    std::vector<PDFContentNodeReference> changedNodes;  ///< Data, override or position in the tree
    bool isLayersChanged = false;                       ///< The layers model (from stage E4)
    bool isStructureChanged = false;                    ///< The structure model

    bool isEmpty() const { return containers.empty() && addedNodes.empty() && removedNodes.empty() && changedNodes.empty() && !isLayersChanged && !isStructureChanged; }
};

/// A change of the model. The GUI, undo / redo, the script of MCP and the tests all change the
/// model only by commands. A command holds the difference, not a snapshot of a page (the old and
/// the new versions of the containers share their unchanged nodes).
///
/// A command over a read-only container or over an Opaque node is refused by apply. The lock and
/// the "hidden in the editor" flag of a node are no commands (no work, no undo, no JSON).
///
/// Threads and ownership: lives in the thread of its session (the GUI thread, or the thread of a
/// tool run with its own session); owned by the undo stack after it was executed, by the caller
/// before.
class PDF4QTLIBCORESHARED_EXPORT PDFContentCommand
{
public:
    explicit PDFContentCommand() = default;
    virtual ~PDFContentCommand();

    PDFContentCommand(const PDFContentCommand&) = delete;
    PDFContentCommand& operator=(const PDFContentCommand&) = delete;

    /// Applies the command to the model of the session (PDFContentEditSession::setContainer,
    /// setStructure; identifiers of new nodes from allocateNodeId). Called by the session only.
    /// Applying it again after revert gives the same model, the same identifiers included.
    /// \param session The session
    /// \param errorText Translated reason of a refusal
    /// \returns false, when the command is refused; the model is unchanged then
    virtual bool apply(PDFContentEditSession* session, QString* errorText) = 0;

    /// Returns the model to the state before apply. Called by the session only.
    virtual void revert(PDFContentEditSession* session) = 0;

    /// Merges a following command into this one (typing by words, moves by the arrow keys
    /// within a second, a dragged value): this command then does both.
    /// \returns false, when the commands cannot be merged (the default)
    virtual bool mergeWith(const PDFContentCommand& next);

    /// The text of the command for Undo / Redo (translated, "Move 2 Objects")
    virtual QString getText() const = 0;

    /// The page undo switches to before it shows the change: -1 = the document (no page)
    virtual PDFInteger getPageIndex() const = 0;

    /// The JSON type of the command (the constants TYPE_* of PDFContentCommandFactory)
    virtual QByteArray getType() const = 0;

    /// The command as an operation of a script of MCP (the object has "type" and the fields
    /// of the command; PDFContentJson writes identifiers and values)
    virtual QJsonObject toJson() const = 0;

    /// What the last apply or revert changed
    const PDFContentChange& getChange() const { return m_change; }

protected:
    void setChange(PDFContentChange change) { m_change = std::move(change); }

private:
    PDFContentChange m_change;
};

using PDFContentCommandPointer = std::shared_ptr<PDFContentCommand>;

/// Creates the commands: the basic ones of stage E0 directly, every one from the JSON of a
/// script.
///
/// Threads and ownership: the functions creating commands are reentrant. The registry of the
/// types is filled once before any use (registerType from the static registration of each
/// command file) and only read afterwards, by any thread.
class PDF4QTLIBCORESHARED_EXPORT PDFContentCommandFactory
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFContentCommandFactory)

public:
    PDFContentCommandFactory() = delete;

    /// JSON types of the commands (zadani_jadro.md 4.5): stage E0
    static constexpr const char* TYPE_DELETE = "delete";
    static constexpr const char* TYPE_TRANSFORM = "transform";
    static constexpr const char* TYPE_REORDER = "reorder";
    static constexpr const char* TYPE_COPY = "copy";
    static constexpr const char* TYPE_SET_APPEARANCE = "set_appearance";
    static constexpr const char* TYPE_SET_TRANSPARENCY = "set_transparency";
    static constexpr const char* TYPE_GROUP = "group";
    static constexpr const char* TYPE_UNGROUP = "ungroup";
    static constexpr const char* TYPE_CLEAR_PAGE = "clear_page";

    /// JSON types of the later stages (their files register them; the names are fixed here)
    static constexpr const char* TYPE_MAKE_CLIPPING_GROUP = "make_clipping_group";
    static constexpr const char* TYPE_RELEASE_CLIPPING_GROUP = "release_clipping_group";
    static constexpr const char* TYPE_ADD_PATH = "add_path";
    static constexpr const char* TYPE_EDIT_PATH = "edit_path";
    static constexpr const char* TYPE_PATH_OPERATION = "path_operation";
    static constexpr const char* TYPE_ADD_IMAGE = "add_image";
    static constexpr const char* TYPE_REPLACE_IMAGE = "replace_image";
    static constexpr const char* TYPE_CROP_IMAGE = "crop_image";
    static constexpr const char* TYPE_SET_IMAGE_PROPERTIES = "set_image_properties";
    static constexpr const char* TYPE_ADD_TEXT = "add_text";
    static constexpr const char* TYPE_SET_TEXT = "set_text";
    static constexpr const char* TYPE_REPLACE_TEXT = "replace_text";
    static constexpr const char* TYPE_SET_TEXT_PROPERTIES = "set_text_properties";
    static constexpr const char* TYPE_MERGE_TEXTS = "merge_texts";
    static constexpr const char* TYPE_SPLIT_TEXT = "split_text";
    static constexpr const char* TYPE_CONVERT_TEXT_TO_PATHS = "convert_text_to_paths";
    static constexpr const char* TYPE_SET_GLYPH_UNICODE = "set_glyph_unicode";
    static constexpr const char* TYPE_CONVERT_COLOR = "convert_color";
    static constexpr const char* TYPE_SET_PRODUCTION = "set_production";
    static constexpr const char* TYPE_SET_PATTERN_FILL = "set_pattern_fill";
    static constexpr const char* TYPE_EDIT_SHADING = "edit_shading";
    static constexpr const char* TYPE_MAKE_FORM_UNIQUE = "make_form_unique";
    static constexpr const char* TYPE_ADD_LAYER = "add_layer";
    static constexpr const char* TYPE_RENAME_LAYER = "rename_layer";
    static constexpr const char* TYPE_DELETE_LAYER = "delete_layer";
    static constexpr const char* TYPE_SET_LAYER_PROPERTIES = "set_layer_properties";
    static constexpr const char* TYPE_REORDER_LAYER = "reorder_layer";
    static constexpr const char* TYPE_ASSIGN_LAYER = "assign_layer";
    static constexpr const char* TYPE_TAG = "tag";
    static constexpr const char* TYPE_UNTAG = "untag";
    static constexpr const char* TYPE_MARK_ARTIFACT = "mark_artifact";
    static constexpr const char* TYPE_ADD_TAG = "add_tag";
    static constexpr const char* TYPE_DELETE_TAG = "delete_tag";
    static constexpr const char* TYPE_MOVE_TAG = "move_tag";
    static constexpr const char* TYPE_SET_TAG_PROPERTIES = "set_tag_properties";

    /// How Reorder treats the layer and the tag of the moved nodes
    enum class ReorderMode
    {
        FollowParent,       ///< A drag of a row in the tree: the node belongs to its new parent (it leaves its sequence, layer wrap)
        KeepLayerAndTag     ///< Bring to Front / Send to Back / alignment: the layer and the tag stay (zadani_jadro.md 4.4 R-2, 4.12)
    };

    /// Removes nodes (R-3 residual state; consistency of the structure)
    static PDFContentCommandPointer createDelete(std::vector<PDFContentNodeReference> nodes);

    /// Transforms nodes by a matrix of the default space of their container (the override of
    /// R-1; R-5 patterns, R-6 soft mask; nodes with a clipping effect by a canonical rewrite)
    static PDFContentCommandPointer createTransform(std::vector<PDFContentNodeReference> nodes, const QTransform& matrix);

    /// Moves nodes of one container to another place of its tree (R-2)
    /// \param nodes Nodes of one container, kept in their mutual order
    /// \param parent The new parent
    /// \param index Index among the children of the parent, -1 = at the end
    /// \param mode Layer and tag
    static PDFContentCommandPointer createReorder(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, ReorderMode mode);

    /// Copies subtrees, also to another container (Duplicate, Paste; R-2, R-4, R-5; the copy has
    /// no structure tags and keeps its layer inside of the same document)
    /// \param nodes Nodes of one container
    /// \param parent The parent of the copies
    /// \param index Index among the children of the parent, -1 = at the end
    /// \param matrix A move of the copies in the default space of the target container
    static PDFContentCommandPointer createCopy(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, const QTransform& matrix);

    /// Sets properties of the appearance (fill, stroke, line width, dash, cap, join, miter limit;
    /// R-1: on leaves only, a container gives the change to its leaves)
    static PDFContentCommandPointer createSetAppearance(std::vector<PDFContentNodeReference> nodes, std::map<PDFContentStateProperty, PDFContentStateValue> properties);

    /// Sets the alphas and the blend mode (an ExtGState with exactly these keys, never /SMask)
    static PDFContentCommandPointer createSetTransparency(std::vector<PDFContentNodeReference> nodes, std::optional<PDFReal> alphaStroking, std::optional<PDFReal> alphaFilling, std::optional<QByteArray> blendMode);

    /// Wraps nodes of one parent into a new Group q ... Q (state runs between them stay in place)
    static PDFContentCommandPointer createGroup(std::vector<PDFContentNodeReference> nodes);

    /// Removes a Group and moves its children out by R-2 (refused for a group with a clip)
    static PDFContentCommandPointer createUngroup(PDFContentNodeReference group);

    /// Removes every node of a page
    static PDFContentCommandPointer createClearPage(PDFContentContainerKey page);

    /// Creates a command from an operation of a script. A replay reads and executes one
    /// operation after the other: fromJson, PDFContentEditSession::execute, then
    /// PDFContentEditSession::setScriptName for the "as" of the operation with the first added
    /// node of getChange(). So the identifiers and the temporary names of an operation are
    /// resolved against the model the earlier operations left (PDFContentJson::readNodeId).
    /// \param object The operation ("type" and its fields)
    /// \param session The session the identifiers of the operation belong to
    /// \param path The path of the operation in the script for the errors ("operations.items[3]")
    /// \param errors Translated errors with their path ("operations.items[3].nodeId: ...")
    /// \returns nullptr with at least one error, when the operation cannot be read
    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors);

    /// Creator of the commands of one JSON type
    using Creator = PDFContentCommandPointer (*)(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors);

    /// Registers the creator of a JSON type (static registration of a command file; before any
    /// thread uses the factory). The registry is a function-local static of the factory, so a
    /// static registration of a command file works in any order of the initialization.
    /// \returns false for a type, which is registered already
    static bool registerType(const char* type, Creator creator);

    /// The registered JSON types, sorted
    static std::vector<QByteArray> getTypes();
};

}   // namespace pdf

#endif // PDFCONTENTCOMMANDS_H
