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

#ifndef PDFAPPOUTLINEEDITOR_H
#define PDFAPPOUTLINEEDITOR_H

#include "pdfappglobal.h"
#include "pdfappannotationoperations.h"

#include <QObject>
#include <QPointer>

#include <optional>

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentView;
class PDFAppOutlinePanel;

/// Editing of the outline of a document view (package C of step 4): the "+"
/// button of the Outline panel (195:1485), the Insert › Outline item tile of
/// package A, inline rename, delete, move, indent, target and style of the items,
/// each one undo step with the flag Outline (PDFAppAnnotationOperations). Locked
/// document or a document, which does not allow changes: no editing (O20).
///
/// The editor works on the current item of the Outline panel (the panel tells it
/// by setCurrentItem) and on the paths the panel gives it; the commands of the
/// package NotesOutline call it. A new item (createItemAtCurrentView) is not
/// written until its title is confirmed in the inline editor of the panel: Enter
/// or a click elsewhere writes it (one undo step), Esc leaves no trace.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutlineEditor : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using OutlinePath = PDFAppAnnotationOperations::OutlinePath;

    /// Status message timeout (milliseconds)
    static constexpr int STATUS_MESSAGE_TIMEOUT = 5000;

    explicit PDFAppOutlineEditor(PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppOutlineEditor() override;

    PDFAppDocumentView* getView() const;
    PDFAppDocument* getDocument() const;

    /// The view has a new document model (PDFAppDocumentView::setDocument)
    void setDocument(PDFAppDocument* document);

    /// The outline can be edited: a shown, unlocked document, which allows the
    /// changes (Modify or Assemble), with an outline the operations can edit
    bool isEditingAllowed() const;

    /// Reason, why the outline cannot be edited (empty when it can)
    QString getEditingRefusalReason() const;

    /// Destination of the current view: XYZ of the top of the view on the current
    /// page (the top of the page, when the view shows the area above it), the
    /// left side and the zoom kept by the reader. nullopt without a page.
    std::optional<PDFDestination> getCurrentViewDestination() const;

    // ---------------------------------------------------------------- creation

    /// Creates a new outline item with the destination XYZ of the current view
    /// after the current item of the Outline panel (at the end without one), shows
    /// the Outline panel and starts the inline rename (Esc during the creation =
    /// no undo step). \returns false, when the outline cannot be edited.
    bool createItemAtCurrentView();

    /// A new item waits for its title in the panel
    bool isCreationPending() const { return m_isCreationPending; }
    const OutlinePath& getPendingParent() const { return m_pendingParent; }
    size_t getPendingIndex() const { return m_pendingIndex; }

    /// The document waits for the title of a new outline item (the Outline tab is
    /// shown also for a document without an outline)
    static bool isCreationPending(const PDFAppDocument* document);

    /// Writes the new item with the title (one undo step); an empty title is the
    /// default title. \returns false, when the item has not been written.
    bool finishCreation(const QString& title);

    /// Ends the creation without a change
    void cancelCreation();

    /// Default title of a new item ("New item")
    static QString getDefaultTitle();

    // ---------------------------------------------------------------- current item

    /// Current item of the Outline panel (empty = none)
    void setCurrentItem(const OutlinePath& path);
    const OutlinePath& getCurrentItem() const { return m_currentItem; }

    /// The current item exists in the current document
    bool hasCurrentItem() const;

    /// The operations of the commands are possible for the current item
    bool canMoveUp() const;
    bool canMoveDown() const;
    bool canIndent() const;
    bool canOutdent() const;
    bool isCurrentItemBold() const;
    bool isCurrentItemItalic() const;

    /// Operations on the current item (the commands outline.*); each one is one
    /// undo step. \returns false, when nothing has been changed.
    bool renameCurrentItem();
    bool deleteCurrentItem();
    bool moveCurrentItemUp();
    bool moveCurrentItemDown();
    bool indentCurrentItem();
    bool outdentCurrentItem();
    bool setCurrentItemTargetToCurrentView();
    bool setCurrentItemBold(bool bold);
    bool setCurrentItemItalic(bool italic);

    // ---------------------------------------------------------------- paths

    /// Operations on an item of the path (the panel: inline rename, drag and drop)
    bool renameItem(const OutlinePath& item, const QString& title);
    bool deleteItem(const OutlinePath& item);
    bool moveItem(const OutlinePath& item, const OutlinePath& newParent, size_t index);
    bool setItemTargetToCurrentView(const OutlinePath& item);
    bool setItemStyle(const OutlinePath& item, bool bold, bool italic);

Q_SIGNALS:
    /// The outline has been changed by the editor (one undo step)
    void outlineChanged();

    /// The state of the commands may have changed (current item, document, lock)
    void stateChanged();

private:
    const PDFOutlineItem* findItem(const OutlinePath& path) const;
    PDFAppOutlinePanel* findPanel() const;
    bool commit(const PDFAppAnnotationResult& result, const QString& description, const OutlinePath& selectedItem);
    void showStatusMessage(const QString& text) const;
    void setPendingProperty(bool pending);
    void onDocumentChanged();
    void onDocumentDestroyed();

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    OutlinePath m_currentItem;
    bool m_isCreationPending = false;
    OutlinePath m_pendingParent;
    size_t m_pendingIndex = 0;
    PDFDestination m_pendingDestination;
    QString m_previousPanelId;                  ///< Panel shown before the creation (shown again after a canceled creation of the first item)
    mutable PDFDocumentPointer m_editableDocument;  ///< Document of the cache of PDFAppAnnotationOperations::isOutlineEditable
    mutable bool m_isOutlineEditable = false;
};

}   // namespace pdf

#endif // PDFAPPOUTLINEEDITOR_H
