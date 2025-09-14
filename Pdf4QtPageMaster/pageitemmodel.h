// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFPAGEMASTER_PAGEITEMMODEL_H
#define PDFPAGEMASTER_PAGEITEMMODEL_H

#include "pdfdocument.h"
#include "pdfutils.h"
#include "pdfdocumentmanipulator.h"

#include <QImage>
#include <QItemSelection>
#include <QAbstractItemModel>

namespace pdfpagemaster
{

enum PageType
{
    PT_DocumentPage,
    PT_Image,
    PT_Empty,
    PT_Last
};

struct PageGroupItem
{
    QString groupNameWithTitle;
    QString groupNameWithoutTitle;
    QString pagesCaption;
    QStringList tags;

    struct GroupItem
    {
        auto operator<=>(const GroupItem&) const = default;

        int documentIndex = -1;
        pdf::PDFInteger pageIndex = -1;
        pdf::PDFInteger imageIndex = -1;
        QSizeF rotatedPageDimensionsMM; ///< Rotated page dimensions, but without additional rotation
        pdf::PageRotation pageAdditionalRotation = pdf::PageRotation::None; ///< Additional rotation applied to the page
        PageType pageType = PT_DocumentPage;
    };

    std::vector<GroupItem> groups;

    auto operator<=>(const PageGroupItem&) const = default;

    bool isGrouped() const { return groups.size() > 1; }

    std::set<int> getDocumentIndices() const;

    void rotateLeft();
    void rotateRight();
};

struct DocumentItem
{
    QString fileName;
    pdf::PDFDocument document;
};

struct ImageItem
{
    QImage image;
    QByteArray imageData;
};

class PageItemModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    explicit PageItemModel(QObject* parent);

    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& index) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QStringList mimeTypes() const override;
    virtual QMimeData* mimeData(const QModelIndexList& indexes) const override;
    virtual bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    virtual bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;
    virtual Qt::DropActions supportedDropActions() const override;
    virtual Qt::DropActions supportedDragActions() const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

    enum class AssembleMode
    {
        Unite,
        Separate,
        SeparateGrouped
    };

    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> getAssembledPages(AssembleMode mode) const;

    /// Clear all data and undo/redo
    void clear();

    /// Adds document to the model, inserts one single page group containing
    /// the whole document. Returns index of a newly added document. If document
    /// cannot be added (for example, it already exists), -1 is returned.
    /// \param fileName File name
    /// \param document Document
    /// \param index Index, where image is inserted
    /// \returns Identifier of the document (internal index)
    int insertDocument(QString fileName, pdf::PDFDocument document, const QModelIndex& index);

    /// Adds image to the model, inserts one single page containing
    /// the image. Returns index of a newly added image. If image
    /// cannot be read from the file, -1 is returned.
    /// \param fileName Image file
    /// \param index Index, where image is inserted
    /// \returns Identifier of the image (internal index)
    int insertImage(QString fileName, const QModelIndex& index);

    /// Adds image to the model, inserts one single page containing
    /// the image. Returns index of a newly added image.
    /// \param image Image
    /// \param index Index, where image is inserted
    /// \returns Identifier of the image (internal index)
    int insertImage(QImage image, const QModelIndex& index);

    /// Returns item at a given index. If item doesn't exist,
    /// then nullptr is returned.
    /// \param index Index
    const PageGroupItem* getItem(const QModelIndex& index) const;

    /// Returns item at a given index. If item doesn't exist,
    /// then nullptr is returned.
    /// \param index Index
    PageGroupItem* getItem(const QModelIndex& index);

    ///  Returns true, if grouped item exists in the indices
    bool isGrouped(const QModelIndexList& indices) const;

    /// Returns true, if trash bin is empty
    bool isTrashBinEmpty() const { return m_trashBin.empty(); }

    QItemSelection getSelectionEven() const;
    QItemSelection getSelectionOdd() const;
    QItemSelection getSelectionPortrait() const;
    QItemSelection getSelectionLandscape() const;

    void group(const QModelIndexList& list);
    void ungroup(const QModelIndexList& list);

    QModelIndexList restoreRemovedItems();
    QModelIndexList cloneSelection(const QModelIndexList& list);
    void removeSelection(const QModelIndexList& list);
    void insertEmptyPage(const QModelIndexList& list);

    void rotateLeft(const QModelIndexList& list);
    void rotateRight(const QModelIndexList& list);

    static QString getMimeDataType() { return QLatin1String("application/pagemodel.PDF4QtPageMaster"); }

    const std::map<int, DocumentItem>& getDocuments() const { return m_documents; }
    const std::map<int, ImageItem>& getImages() const { return m_images; }

    struct SelectionInfo
    {
        int documentCount = 0;
        int imageCount = 0;
        int blankPageCount = 0;
        int firstDocumentIndex = 0;

        bool isDocumentOnly() const { return documentCount > 0 && imageCount == 0 && blankPageCount == 0; }
        bool isSingleDocument() const { return isDocumentOnly() && documentCount == 1; }
        bool isTwoDocuments() const { return isDocumentOnly() && documentCount == 2; }
    };

    QString getItemDisplayText(const PageGroupItem* item) const;

    SelectionInfo getSelectionInfo(const QModelIndexList& list) const;

    void regroupReversed(const QModelIndexList& list);
    void regroupEvenOdd(const QModelIndexList& list);
    void regroupPaired(const QModelIndexList& list);
    void regroupOutline(const QModelIndexList& list, const std::vector<pdf::PDFInteger>& indices);
    void regroupAlternatingPages(const QModelIndexList& list, bool reversed);

    bool canUndo() const { return !m_undoSteps.empty(); }
    bool canRedo() const { return !m_redoSteps.empty(); }

    void undo();
    void redo();

    bool isUseTitleInDescription() const;
    void setShowTitleInDescription(bool newShowTitleInDescription);

private:
    static const int MAX_UNDO_REDO_STEPS = 10;

    void createDocumentGroup(int index, const QModelIndex& insertIndex);
    QString getGroupNameFromDocument(int index, bool useTitle) const;
    void updateItemCaptionAndTags(PageGroupItem& item) const;
    void insertEmptyPage(const QModelIndex& index);

    struct UndoRedoStep
    {
        auto operator<=>(const UndoRedoStep&) const = default;

        std::vector<PageGroupItem> pageGroupItems;
        std::vector<PageGroupItem> trashBin;
    };

    class Modifier
    {
    public:
        explicit Modifier(PageItemModel* model);
        ~Modifier();

    private:
        PageItemModel* m_model;
        UndoRedoStep m_stateBeforeModification;
    };

    std::vector<PageGroupItem::GroupItem> extractItems(std::vector<PageGroupItem>& items, const QModelIndexList& selection) const;

    QItemSelection getSelectionImpl(std::function<bool(const PageGroupItem::GroupItem&)> filter) const;

    UndoRedoStep getCurrentStep() const { return UndoRedoStep{ m_pageGroupItems, m_trashBin }; }
    void updateUndoRedoSteps();
    void clearUndoRedo();

    void performUndoRedo(std::vector<UndoRedoStep>& load, std::vector<UndoRedoStep>& save);

    std::vector<PageGroupItem> m_pageGroupItems;
    std::map<int, DocumentItem> m_documents;
    std::map<int, ImageItem> m_images;
    std::vector<PageGroupItem> m_trashBin;

    std::vector<UndoRedoStep> m_undoSteps; // Oldest step is first, newest step is last
    std::vector<UndoRedoStep> m_redoSteps;

    bool m_showTitleInDescription;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_PAGEITEMMODEL_H
