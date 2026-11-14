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

#ifndef PDFAPPTOOLIMAGELIST_H
#define PDFAPPTOOLIMAGELIST_H

#include "pdfappglobal.h"

#include "pdfappdocumentpanels_common.h"
#include "pdfapppagelayout.h"
#include "pdfcms.h"
#include "pdffluentwidgets.h"
#include "pdfcomponentdelegates.h"
#include "pdfdocument.h"
#include "pdfdocumentimagelist.h"

#include <QAbstractListModel>
#include <QImage>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QWidget>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QButtonGroup;
class QMouseEvent;
class QPushButton;

namespace pdf
{

class PDFAppPanelListView;
class PDFAppParagraph;
class PDFAppToolImageDecodeJob;
class PDFAppToolImageDecodeLink;
class PDFComboBox;
class PDFLabel;
class PDFOperationControl;

/// The decoding of images of a document in the pool (the thumbnails of the list, the BEFORE /
/// AFTER of Optimize Images, Copy Image of Extract Images): an item of the list is decoded by the
/// one decoding of the core, as Extract Images writes it (PDFDocumentToolExtractImages::readImage:
/// the color management, the mask as alpha, inline images too), never by a second decoder.
///
/// A job (PDFExecutionPolicy::startTask) holds only plain values: the shared document (immutable),
/// the color management, the keys and pages - never the decoder. Its result comes back queued
/// through a link, which the destructor cuts (a late result is dropped, nobody waits); cancel
/// drops the results of the started jobs. imagesDecoded is emitted from the event loop, the last
/// statement; a slot may delete the decoder.
///
/// Ownership: a child of its user (the list, the tool GUI).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageDecoder : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// One image to decode
    class Request
    {
    public:
        PDFDocumentImageList::Item item;    ///< The image (its key and its pages)
    };

    explicit PDFAppToolImageDecoder(QObject* parent);
    virtual ~PDFAppToolImageDecoder() override;

    /// Starts a decoding; \returns its ticket (never 0)
    /// \param document The document (null = every image null)
    /// \param cms The color management (null = the default settings)
    /// \param requests The images
    /// \param maximumSide Longest side of a result (0 = the pixels of the image, at most MAXIMUM_PIXELS)
    quint64 decode(PDFDocumentPointer document, PDFCMSPointer cms, std::vector<Request> requests, int maximumSide);

    /// The results of the started decodings are dropped
    void cancelAll();

    /// A decoding runs
    bool isBusy() const { return !m_jobs.empty(); }

    /// Most pixels of an image decoded without a maximum side (a larger one is scaled down)
    static constexpr qint64 MAXIMUM_PIXELS = 64 * 1024 * 1024;

    /// Decodes an image (any thread, reentrant; see the class)
    /// \param document The document
    /// \param cms The color management (null = the default settings)
    /// \param item The image of the list of the document
    /// \param maximumSide Longest side of the result (0 = the pixels of the image, at most MAXIMUM_PIXELS)
    /// \param control Cancellation (optional)
    static QImage decodeImage(const PDFDocument* document, const PDFCMSPointer& cms, const PDFDocumentImageList::Item& item, int maximumSide, const PDFOperationControl* control);

Q_SIGNALS:
    /// The images of a decoding, in the order of its requests (from the event loop)
    void imagesDecoded(quint64 ticket, const QList<QImage>& images);

private:
    friend class PDFAppToolImageDecodeLink;

    void onJobFinished(const std::shared_ptr<pdf::PDFAppToolImageDecodeJob>& job);

    std::shared_ptr<PDFAppToolImageDecodeLink> m_link;
    std::map<quint64, std::shared_ptr<PDFAppToolImageDecodeJob>> m_jobs;
    quint64 m_nextTicket = 1;
};

/// The rows of the list of the images (Figma 297:3135, 301:3594, 308:6911, 341:7516; plan of step
/// 4 of phase 2, 4.1, 9.3; package P2): one row per item of a PDFDocumentImageList, in its order
/// ("Image N" is the row + 1). The state of a row (its tag, the size text, the mark of the export)
/// comes from the tool GUI; the thumbnails come from a worker, keyed by the key and the generation
/// of the document - a late thumbnail of another generation is dropped. A value of the GUI thread;
/// the list is held as a shared immutable value.
///
/// The texts of a row are made by the model from the item (RowStyle): Optimize Images and Grayscale
/// "2480 × 3508 px · 300 dpi" and "DeviceRGB · DCTDecode" (297:3651), Extract Images "Pages 308,
/// 954 · 1654 × 620 px" (341:7704).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageListModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    /// The tag of a row (297:3135 override, will keep original, excluded; 308:6911 already gray,
    /// bitonal; 341:7516 left out; the states without a frame of 2.7: cannot be read, too large,
    /// inline · kept, hidden layer)
    enum class Tag
    {
        None,
        Override,
        KeepOriginal,
        Excluded,
        AlreadyGray,
        Bitonal,
        InlineKept,
        AppearanceKept,     ///< "in an annotation · kept": an annotation appearance draws the image (/AP, R4-4)
        HiddenLayer,
        CannotBeRead,
        TooLarge,
        LeftOut
    };

    /// What the tool says about a row
    class RowState
    {
    public:
        Tag tag = Tag::None;
        QString sizeText;       ///< "1.2 MB → 310 KB", "Report_img_3.png", empty = the size of the streams
        bool isMarked = true;   ///< The image is exported / converted (the mark of Extract Images, 341:7516)
        bool isDimmed = false;  ///< The row is drawn as skipped (308:6911 "already gray": the texts text/disabled)
        QString badgeText;      ///< Translated text of the Tag of a tool, which has its own states (Black & White "converted", "filled white"); not empty = shown instead of the text of \p tag

        bool operator==(const RowState& other) const { return tag == other.tag && sizeText == other.sizeText && isMarked == other.isMarked && isDimmed == other.isDimmed && badgeText == other.badgeText; }
        bool operator!=(const RowState& other) const { return !(*this == other); }
    };

    /// The look and the texts of the rows
    enum class RowStyle
    {
        Optimize,   ///< 297:3651: the thumbnail 64 x 50, four lines (the size line), no mark
        Export      ///< 341:7672: the thumbnail 56 x 43, three lines (the file name), the mark at the trailing edge
    };

    /// Roles of a row
    enum Role
    {
        KeyRole = Qt::UserRole + 1, ///< QString: PDFDocumentImageList::Key::toString
        NumberRole,                 ///< int: N of "Image N"
        TagRole,                    ///< int: Tag
        FirstLineRole,              ///< QString: "2480 × 3508 px · 300 dpi"
        SecondLineRole,             ///< QString: "DeviceRGB · DCTDecode"
        SizeTextRole,               ///< QString: RowState::sizeText
        MarkedRole,                 ///< bool: RowState::isMarked
        ThumbnailRole,              ///< QImage: the thumbnail (null until it is ready)
        TagTextRole,                ///< QString: the translated text of the tag (empty = none)
        DimmedRole,                 ///< bool: RowState::isDimmed
        BytesRole,                  ///< qint64: the bytes of the streams and the masks of the image
        ImageColorRole,             ///< int: PDFDocumentImageList::ImageColor
        IndexRole                   ///< int: the index of the item in the list (the row of this model)
    };

    explicit PDFAppToolImageListModel(QObject* parent);
    virtual ~PDFAppToolImageListModel() override;

    /// The list of a version of the document (a reset); \p generation keys the thumbnails
    void setImageList(std::shared_ptr<const PDFDocumentImageList> list, quint64 generation);
    const std::shared_ptr<const PDFDocumentImageList>& getImageList() const { return m_list; }
    quint64 getGeneration() const { return m_generation; }

    RowStyle getRowStyle() const { return m_rowStyle; }
    void setRowStyle(RowStyle rowStyle);

    /// The state of the row of the key (one dataChanged)
    void setRowState(const PDFDocumentImageList::Key& key, const RowState& state);

    /// The states of all rows in the order of the list (one dataChanged; missing rows get the default)
    void setRowStates(std::vector<RowState> states);

    /// The state of the row (the default for an invalid row)
    RowState getRowState(int row) const;

    /// A thumbnail of a worker; dropped, when \p generation is not the generation of the list
    void setThumbnail(const PDFDocumentImageList::Key& key, quint64 generation, const QImage& thumbnail);

    /// The thumbnail of the row is known (it may be null - the image gives no pixels)
    bool hasThumbnail(int row) const;

    /// Row of the key, -1 = none
    int findRow(const PDFDocumentImageList::Key& key) const;

    /// The item of the row, nullptr = none
    const PDFDocumentImageList::Item* getItem(int row) const;

    /// Translated text of a tag ("override", "inline · kept"); empty for None
    static QString getTagText(Tag tag);

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

private:
    QString getFirstLine(const PDFDocumentImageList::Item& item) const;
    QString getSecondLine(const PDFDocumentImageList::Item& item) const;

    std::shared_ptr<const PDFDocumentImageList> m_list;
    quint64 m_generation = 0;
    RowStyle m_rowStyle = RowStyle::Optimize;
    std::vector<RowState> m_states;     ///< By the row
    std::vector<QImage> m_thumbnails;   ///< By the row
    std::vector<bool> m_hasThumbnail;   ///< By the row
};

/// The rows shown by a chip and their order (Sort): a proxy of PDFAppToolImageListModel. The chips
/// "all" (every row), "overrides" (Tag::Override), "excluded" (Tag::Excluded, Tag::LeftOut),
/// "color" (a color image by its declaration, R4-5), "export" (the marked rows). Sort Document
/// keeps the order of the list, Largest / Smallest order by the bytes (ties in the order of the
/// list).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageListProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

private:
    using BaseClass = QSortFilterProxyModel;

public:
    /// The order of the rows
    enum class Sort
    {
        Document,   ///< The order of the first drawing (Image N)
        Largest,    ///< The largest streams first (297:3135 "Sort: Largest")
        Smallest    ///< The smallest streams first
    };

    explicit PDFAppToolImageListProxyModel(QObject* parent);
    virtual ~PDFAppToolImageListProxyModel() override;

    void setChipId(const QString& chipId);
    const QString& getChipId() const { return m_chipId; }

    void setSort(Sort sort);
    Sort getSort() const { return m_sort; }

    /// Only the images drawn on these pages (indices from 0) are shown (Extract Images, the menu
    /// Pages 341:7936); std::nullopt = every image
    void setPageFilter(std::optional<std::set<PDFInteger>> pages);
    const std::optional<std::set<PDFInteger>>& getPageFilter() const { return m_pages; }

    /// The row of the source is shown by the chip (also without a proxy)
    static bool isAcceptedByChip(const QString& chipId, const QModelIndex& sourceIndex);

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    virtual bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString m_chipId;
    Sort m_sort = Sort::Document;
    std::optional<std::set<PDFInteger>> m_pages;
};

/// The rows of the list of the images (image-Image 1 297:3651, 341:7672): drawn by
/// PDFComponentPainter as the other rows of the components - the selection bg/accent-subtle, the
/// hover, the focus ring after the keyboard; the thumbnail on the paper (sample/white) in its
/// frame, the name Semi Bold 12 and the Tag of the row, the lines Regular 11, the size Medium 11
/// (text/accent in a selected row), the mark (icon/check) of Export. A dimmed row has its texts
/// text/disabled (308:6911).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageListDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    static constexpr int PADDING = 8;
    static constexpr int GAP = 10;
    static constexpr int LINE_GAP = 1;
    static constexpr int NAME_LINE = 15;
    static constexpr int TAG_LINE = 21;
    static constexpr int TEXT_LINE = 13;
    static constexpr int TAG_GAP = 6;
    static constexpr int MARK_SIZE = 16;
    static constexpr int RADIUS = 8;

    explicit PDFAppToolImageListDelegate(QObject* parent);
    virtual ~PDFAppToolImageListDelegate() override;

    /// The size of the thumbnail of the style (64 x 50 of 297:3652, 56 x 43 of 341:7662)
    static QSize getThumbnailSize(PDFAppToolImageListModel::RowStyle style);

    /// The rectangle of the mark of an Export row (in the coordinates of the option rectangle)
    static QRectF getMarkRect(const QRectF& rowRect, Qt::LayoutDirection direction);

    /// The style of the rows of the model of the index (Optimize without a model)
    static PDFAppToolImageListModel::RowStyle getRowStyle(const QModelIndex& index);

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
};

/// The list of the images of the left panel (297:3135, 308:6911, 341:7516; package P2): the head
/// "412 images · 21.3 MB", the chips (All · Overrides · Excluded, Color · Excluded, All · To
/// export), Sort, the rows of PDFAppToolImageListModel (a model and a delegate; thumbnails only of
/// the visible rows), the selection by Ctrl / Shift / Ctrl+A, Space toggles the mark. Used by
/// Optimize Images, Grayscale and Extract Images (P2) and by the Source Images of Black & White
/// (P3, the grid of the images). Every signal is emitted last; a slot may delete the list.
///
/// Thumbnails: setThumbnailSource gives the document and the color management; the list decodes
/// the images of the visible rows by its PDFAppToolImageDecoder (one decoding of the rows at a
/// time, each image at most THUMBNAIL_SIDE pixels); a late result of another generation is
/// dropped, the destructor deletes the decoder first (nobody waits).
///
/// Ownership: a child of the panel of the tool GUI, which keeps it in a QPointer; the model, the
/// proxy and the delegate are its children.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// A chip of the head ("All 412")
    class Chip
    {
    public:
        QString id;     ///< "all", "overrides", "excluded", "color", "export"
        QString text;   ///< Translated text with its count
        int count = -1; ///< The count drawn after the text (Filter chip "Show count"); -1 = none
    };

    static constexpr int SORT_WIDTH = 126;
    static constexpr int CONTROL_HEIGHT = 32;
    static constexpr int CHIP_GAP = 6;
    static constexpr int ROW_GAP = 2;
    static constexpr int THUMBNAIL_SIDE = 128;  ///< Longest side of a decoded thumbnail (64 x 50 at 2x)

    explicit PDFAppToolImageList(QWidget* parent);
    virtual ~PDFAppToolImageList() override;

    PDFAppToolImageListModel* getModel() const { return m_model; }
    PDFAppToolImageListProxyModel* getProxyModel() const { return m_proxy; }
    PDFAppPanelListView* getView() const { return m_view; }
    PDFComboBox* getSortComboBox() const { return m_sort; }
    PDFLabel* getTitleLabel() const { return m_title; }
    PDFLabel* getHeadLabel() const { return m_head; }
    PDFAppParagraph* getTipLabel() const { return m_tip; }
    PDFLabel* getEmptyLabel() const { return m_empty; }

    /// The button of the chip, nullptr = none
    QPushButton* getChipButton(const QString& chipId) const;

    /// The look of the rows (the style of the model)
    void setRowStyle(PDFAppToolImageListModel::RowStyle style);

    /// The head ("412 images · 21.3 MB") and its chips
    void setHeadText(const QString& text);
    void setChips(const std::vector<Chip>& chips, const QString& currentChipId);
    QString getCurrentChipId() const { return m_currentChipId; }

    /// Sort (297:3624 "Sort: Largest"); hidden = the order of the list (Extract Images)
    void setSortVisible(bool isVisible);
    void setSort(PDFAppToolImageListProxyModel::Sort sort);

    /// The tip below the rows (297:3764)
    void setTipText(const QString& text);

    /// The text shown instead of the rows when no row is shown (the list is empty or the chip shows
    /// none; 341:8976 "No images on page 308"); empty = the default text
    void setEmptyText(const QString& text);

    /// The keys of the selected rows in the order of the list
    std::vector<PDFDocumentImageList::Key> getSelectedKeys() const;

    /// Selects the rows of the keys (no signal); the first one becomes the current row
    void setSelectedKeys(const std::vector<PDFDocumentImageList::Key>& keys);

    /// The current row (the BEFORE / AFTER of the canvas), invalid key = none
    PDFDocumentImageList::Key getCurrentKey() const;

    /// The document of the thumbnails (null = no thumbnails)
    void setThumbnailSource(PDFDocumentPointer document, PDFCMSPointer cms);

    /// Asks for the thumbnails of the visible rows (after a change of the list or of the scroll)
    void requestVisibleThumbnails();

    /// A decoding of the thumbnails runs (tests)
    bool isThumbnailDecodingBusy() const;

Q_SIGNALS:
    /// The selected rows have changed (from the event loop, once for the changes of one event: the
    /// selection model of Qt finishes its change first)
    void selectionChanged();

    /// The user has toggled the mark of the rows (Space, the mark of a row; from the event loop)
    void markToggled(const std::vector<pdf::PDFDocumentImageList::Key>& keys, bool isMarked);

    /// The user has chosen another chip (from the event loop)
    void chipChanged(const QString& chipId);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void updateEmptyState();
    void onChipToggled(int id, bool isChecked);
    void onSortIndexChanged(int index);
    void onSelectionChanged();
    void emitSelectionChanged();
    void emitMarkToggled(std::vector<PDFDocumentImageList::Key> keys, bool isMarked);
    void emitChipChanged();
    void onScrolled();
    void onModelReset();
    void onThumbnailsDecoded(quint64 ticket, const QList<QImage>& images);
    void toggleMarks(const std::vector<PDFDocumentImageList::Key>& keys);

    /// The item of an Export row, whose mark (with 4 px around it) is under the left button of
    /// \p mouseEvent (341:7670); nullptr = none
    const PDFDocumentImageList::Item* getMarkItemAt(const QMouseEvent* mouseEvent) const;

    QPointer<PDFAppToolImageListModel> m_model;
    QPointer<PDFAppToolImageListProxyModel> m_proxy;
    QPointer<PDFAppToolImageListDelegate> m_delegate;
    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_head;
    QPointer<PDFComboBox> m_sort;
    QPointer<QWidget> m_chipsWidget;
    QPointer<QButtonGroup> m_chipGroup;
    std::vector<QPointer<QPushButton>> m_chipButtons;
    std::vector<QString> m_chipIds;
    QPointer<PDFAppPanelListView> m_view;
    QPointer<PDFLabel> m_empty;
    QPointer<PDFAppParagraph> m_tip;
    QString m_currentChipId;
    QString m_emptyText;
    bool m_isUpdatingChips = false;
    bool m_isSelectionChangePending = false;
    bool m_isChipChangePending = false;

    PDFDocumentPointer m_thumbnailDocument;
    PDFCMSPointer m_thumbnailCms;
    QPointer<PDFAppToolImageDecoder> m_decoder;
    quint64 m_pendingTicket = 0;                            ///< The decoding of the thumbnails, 0 = none
    quint64 m_pendingGeneration = 0;                        ///< The generation of the list of the decoding
    std::vector<PDFDocumentImageList::Key> m_pendingKeys;   ///< The keys of the decoding in its order
    std::set<int> m_requestedRows;  ///< Rows of the source, whose thumbnails are being decoded or are known
};

}   // namespace pdf

#endif // PDFAPPTOOLIMAGELIST_H
