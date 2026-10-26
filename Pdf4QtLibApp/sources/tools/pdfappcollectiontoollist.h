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

#ifndef PDFAPPCOLLECTIONTOOLLIST_H
#define PDFAPPCOLLECTIONTOOLLIST_H

#include "pdfappglobal.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"

#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QMimeData;
class QVBoxLayout;

namespace pdf
{

class PDFIconButton;
class PDFLabel;
class PDFToolRow;

/// Tools of a collection in the collections editor (Figma Manage collections
/// 283:1888, list 284:2300): a frame border/default (radius 10, padding 4, gap 2)
/// with one row per tool - the drag handle (more-vertical), the Tool row without
/// the star and the remove button (x). The order is the order of the collection.
///
/// The user changes the order by dragging a row by its handle (QDrag with the
/// private MIME type getMimeType: the id of this list and the tool id - data of
/// another list or of another application are refused; the drop indicator is a
/// line border/focus of 2 px between the rows), or by the keys of a focused row:
/// Alt+Up / Alt+Down / Alt+Home / Alt+End move it, Delete removes it. Every
/// change by the user is announced by toolsChanged (the editor keeps the draft);
/// setTools replaces the tools without the signal. Rows are reused by their tool
/// (the focus stays in a moved row). Widgets only in the GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCollectionToolList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 284:2300: padding 4 inside of the border, gap 2, radius 10;
    /// a row: gap 6, padding at the trailing side 6
    static constexpr int PADDING = 4;
    static constexpr int SPACING = 2;
    static constexpr qreal RADIUS = 10.0;
    static constexpr int ROW_SPACING = 6;
    static constexpr int ROW_TRAILING_PADDING = 6;

    /// Width of the drop indicator
    static constexpr int DROP_INDICATOR_WIDTH = 2;

    /// Longest dragged data, which are read (the instance id and a tool id)
    static constexpr int MAXIMUM_MIME_DATA_SIZE = 512;

    explicit PDFAppCollectionToolList(QWidget* parent = nullptr);
    virtual ~PDFAppCollectionToolList() override;

    /// MIME type of a dragged tool of a collection
    static QString getMimeType() { return QStringLiteral("application/x-pdf4qt-collection-tool"); }

    /// Tools of the list in their order
    const QStringList& getTools() const { return m_tools; }

    /// Replaces the tools (no signal); invalid and duplicate ids are left out
    void setTools(const QStringList& toolIds);

    /// Identifier of this list in the dragged data (unique per list)
    const QString& getInstanceId() const { return m_instanceId; }

    /// Dragged data of the tool of this list (the caller owns them)
    QMimeData* createMimeData(const QString& toolId) const;

    /// Tool of dragged data of this list, empty = not a tool of this list (data of
    /// another list, another application, malformed or too large data)
    QString getDraggedTool(const QMimeData* mimeData) const;

    /// Index, before which a tool dropped at the position (coordinates of the
    /// list) is placed (0 to the number of the tools)
    int getDropIndex(const QPoint& position) const;

    /// Index of the shown drop indicator (-1 = none; tests)
    int getDropIndicatorIndex() const { return m_dropIndex; }

    /// Moves the tool before the tool of \p index (0 to the number of the tools,
    /// the index of the list before the move) and emits toolsChanged. \returns
    /// false, when nothing has changed or the list has been deleted by a slot.
    bool moveTool(const QString& toolId, int index);

    /// Removes the tool and emits toolsChanged. \returns false, when the tool is
    /// not in the list or the list has been deleted by a slot.
    bool removeTool(const QString& toolId);

    /// Widgets of the row of the tool (nullptr = none)
    QWidget* findRow(const QString& toolId) const;
    PDFToolRow* findToolRow(const QString& toolId) const;
    PDFIconButton* findHandle(const QString& toolId) const;
    PDFIconButton* findRemoveButton(const QString& toolId) const;

    /// Text of the empty list
    PDFLabel* getEmptyLabel() const { return m_emptyLabel; }

    /// Drag in progress (QDrag::exec runs; tests)
    bool isDragging() const { return m_isDragging; }

Q_SIGNALS:
    /// The user has changed the tools (moved, removed)
    void toolsChanged(const QStringList& toolIds);

    /// The user has activated the tool row (the editor opens the tool)
    void toolActivated(const QString& toolId);

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dragLeaveEvent(QDragLeaveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    struct Row
    {
        QString toolId;
        QPointer<QWidget> widget;
        QPointer<PDFIconButton> handle;
        QPointer<PDFToolRow> toolRow;
        QPointer<PDFIconButton> removeButton;
    };

    Row createRow(const QString& toolId);
    void updateRowTexts(const Row& row) const;
    void placeRows();
    void retranslate();
    const Row* findRowData(const QString& toolId) const;
    static int findRowIndex(const std::vector<Row>& rows, const QString& toolId);
    QString getToolOfObject(const QObject* object) const;
    void setDropIndex(int index);
    void startDrag(const QString& toolId);
    bool handleKey(const QString& toolId, QKeyEvent* event);
    void focusRowAfterRemoval(int removedIndex);

    void onToolRowClicked();
    void onRemoveClicked();

    QString m_instanceId;
    QStringList m_tools;
    std::vector<Row> m_rows;
    QPointer<QVBoxLayout> m_layout;
    QPointer<PDFLabel> m_emptyLabel;
    QString m_pressedTool;
    QPoint m_pressPosition;
    int m_dropIndex = -1;
    bool m_isDragging = false;
};

}   // namespace pdf

#endif // PDFAPPCOLLECTIONTOOLLIST_H
