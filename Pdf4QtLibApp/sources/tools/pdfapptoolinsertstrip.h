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

#ifndef PDFAPPTOOLINSERTSTRIP_H
#define PDFAPPTOOLINSERTSTRIP_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanels_thumbnails.h"

#include "pdfdocument.h"

#include <QAbstractItemView>
#include <QPointer>
#include <QSize>
#include <QStringList>

#include <utility>
#include <vector>

namespace pdf
{

class PDFAppDocumentView;

/// The strip of the place of an insertion (plan of step 3 of phase 2, 4.3; Insert Pages 388:15487,
/// 388:17167, Add Image as Page 388:18837): the pages of the document of the tab around the place
/// - three in front of it and three after it (position 388:15601: 275 wide, the tiles 85 x 128
/// with the paper 73 x 100, 4 apart in both directions, wrapped by the width) - with the line of
/// the insertion (insert-marker 388:15670: 10 x 100 with the line 3 wide of bg/accent, 4 in front
/// of the next tile). Replace (388:17167) has no line: the replaced pages are dimmed with the
/// caption "replaced" and the badge replace.
///
/// The place is a gap: the gap \e g lies in front of the page of the index \e g (0 = in front of
/// the first page, the page count = after the last one).
///  - A click on a page moves the line in front of it (the leading half of the tile) or after it
///    (the trailing half); a click on the line does nothing.
///  - Left / Right move the line by one gap (mirrored for right-to-left: the flow of the tiles
///    starts at the right edge), Home / End to the start and the end.
///  - Files dragged over the strip show the line at the gap under the pointer; a drop of local
///    files there emits filesDropped with the gap (the GUI adds them as sources at that place).
///
/// The pictures are the thumbnails of the document (PDFAppThumbnailsModel, the scheduler of the
/// thumbnails with a value copy of the document; only the tiles of the strip ask for pictures,
/// the results come back by a queued call guarded by the scheduler).
///
/// Ownership: the strip owns its model and its scheduler (children); the GUI of the tool holds
/// the strip by a QPointer (a part of its left panel). The signals are emitted from the event
/// handlers of the strip as their last statement (a slot may delete the strip).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInsertPositionStrip : public QAbstractItemView
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemView;

public:
    /// Geometry of 388:15601
    static constexpr QSize SLOT_SIZE = QSize(73, 100);  ///< The paper of a page
    static constexpr int TILE_WIDTH = 85;               ///< The tile of a page (the slot + 12)
    static constexpr int TILE_HEIGHT = 128;             ///< The tile of a page (the slot + 28)
    static constexpr int CAPTION_HEIGHT = 16;           ///< A caption ("replaced") below the label
    static constexpr int GAP = 4;                       ///< Gap of the tiles in both directions
    static constexpr int MARKER_WIDTH = 10;             ///< insert-marker 388:15670
    static constexpr int MARKER_HEIGHT = 100;
    static constexpr int LINE_WIDTH = 3;
    static constexpr double LINE_OFFSET = 3.5;

    /// Pages shown in front of the place and after it (388:15601: pages 10-12 | 13-15)
    static constexpr int PAGES_BEFORE = 3;
    static constexpr int PAGES_AFTER = 3;

    /// An item of the flow of the strip: a page or the line
    struct Item
    {
        int pageIndex = -1;     ///< Page of the document, -1 = the line
        QRect rect;             ///< Rectangle in the coordinates of the viewport
    };

    explicit PDFAppInsertPositionStrip(QWidget* parent);
    virtual ~PDFAppInsertPositionStrip() override;

    /// The document and its view (the pictures follow the settings of its canvas and the
    /// states of its layers)
    void setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view);

    /// The place: the gap of the line (0 to the page count) and no replaced page, or the replaced
    /// pages (indices from 0, sorted) and no line (\p gap -1). No signal.
    void setPlace(int gap, const std::vector<PDFInteger>& replacedPages);
    int getGap() const { return m_gap; }
    const std::vector<PDFInteger>& getReplacedPages() const { return m_replacedPages; }

    /// Number of the pages of the document
    int getPageCount() const;

    /// The first and the last page of the window of the strip (-1, -1 for no page)
    std::pair<int, int> getWindow() const;

    /// The items of the flow in their order (tests: the geometry of 388:15601)
    const std::vector<Item>& getItems() const { return m_items; }

    /// Rectangle of the line in the coordinates of the viewport (empty = no line)
    QRect getLineRect() const;

    /// Rectangle of the tile of the page in the coordinates of the viewport (empty = not shown)
    QRect getTileRect(int pageIndex) const;

    /// The gap of a position of the viewport (the leading or the trailing half of a tile; the
    /// gap of the line, a place between the tiles: the nearest gap of the row), -1 = none
    int getGapAt(const QPoint& position) const;

    /// Files are dragged over the strip: the gap, where they would be dropped (-1 = none)
    int getDropGap() const { return m_dropGap; }

    /// Height of the content (the rows of the tiles)
    int getContentHeight() const { return m_contentHeight; }

    PDFAppThumbnailsModel* getModel() const { return m_model.data(); }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler.data(); }

    virtual QRect visualRect(const QModelIndex& index) const override;
    virtual void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;
    virtual QModelIndex indexAt(const QPoint& point) const override;
    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The user has moved the line to the gap (a click, a key)
    void gapChosen(int gap);

    /// Local files were dropped at the gap (\p refusedTexts: the dropped items, which are no
    /// PDF file or image, with their reasons - "notes.txt – …")
    void filesDropped(int gap, const QStringList& filePaths, const QStringList& refusedTexts);

protected:
    virtual QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers) override;
    virtual int horizontalOffset() const override;
    virtual int verticalOffset() const override;
    virtual bool isIndexHidden(const QModelIndex& index) const override;
    virtual void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
    virtual QRegion visualRegionForSelection(const QItemSelection& selection) const override;
    virtual void updateGeometries() override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dragLeaveEvent(QDragLeaveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    /// The flow of the items for the width of the viewport
    void layoutItems();

    /// The gap shown by the line: the gap of a drag, else the gap of the place
    int getShownGap() const;

    /// Mirrors a rectangle of the left-to-right flow for right-to-left
    QRect toVisual(const QRect& logical) const;

    void updatePictureSize();

    QPointer<PDFAppThumbnailsModel> m_model;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    PDFDocumentPointer m_document;
    std::vector<PDFInteger> m_replacedPages;
    std::vector<Item> m_items;
    int m_gap = 0;
    int m_dropGap = -1;
    int m_pressedGap = -1;
    int m_contentHeight = 0;
};

}   // namespace pdf

#endif // PDFAPPTOOLINSERTSTRIP_H
