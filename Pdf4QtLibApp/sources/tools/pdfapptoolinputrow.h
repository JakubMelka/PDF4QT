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

#ifndef PDFAPPTOOLINPUTROW_H
#define PDFAPPTOOLINPUTROW_H

#include "pdfappglobal.h"

#include "pdfaccenthue.h"
#include "pdfcomponentrows.h"

#include <QImage>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QVBoxLayout;

namespace pdf
{

class PDFAppParagraph;
class PDFLabel;
class PDFTag;
class PDFTextLink;

/// A file of the list of a tool with more inputs (file-A 378:1199, 275 x 59; with a Tag 65,
/// file-B 378:1207; with the progress of the reading 64 and with a link 76, 1390:34070 - K39):
/// the tile 30 of the hue of the input with its letter (Semi Bold 11), the name Semi Bold 12,
/// an optional Tag ("open tab", "Repaired"), two lines Regular 11 (text/tertiary; the line of a
/// problem text/danger) and the progress bar of the reading or a link ("Enter password…",
/// "Remove file"). The current row has bg/accent-subtle (radius 8), the hover bg/hover.
///
/// The row is a button: a click makes it current, Delete asks for its removal, Alt + Up / Down
/// for a move; a press and a move over the distance of a drag drags it (the panel shows the
/// place, the release moves it). Its signals are emitted last (a slot may delete the row).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputRow : public PDFRowButton
{
    Q_OBJECT

private:
    using BaseClass = PDFRowButton;

public:
    static constexpr int RADIUS = 8;
    static constexpr int PADDING = 8;
    static constexpr int TILE = 30;
    static constexpr int GAP = 10;
    static constexpr int NAME_HEIGHT = 15;
    static constexpr int TAG_HEIGHT = 21;
    static constexpr int LINE_HEIGHT = 13;
    static constexpr int LINE_GAP = 1;
    static constexpr int PROGRESS_HEIGHT = 4;
    static constexpr int LINK_HEIGHT = 16;

    /// The thumbnail of an image instead of the tile (image-IMG_2041.jpg 336:2136: thumb 56 x 43)
    static constexpr int THUMBNAIL_WIDTH = 56;
    static constexpr int THUMBNAIL_HEIGHT = 43;
    static constexpr int THUMBNAIL_RADIUS = 4;

    /// What the row shows
    struct Content
    {
        QString inputId;
        QString letter;                         ///< "A"
        PDFAccentHue hue = PDFAccentHue::Neutral;
        QString name;                           ///< Name of the file (data)
        QString tag;                            ///< "open tab", "Repaired", empty = none
        QString firstLine;                      ///< "6 pages · A4 · 2.1 MB", "Protected by a password"
        bool isFirstLineProblem = false;        ///< The first line is text/danger (378:2613)
        bool isFirstLineNotice = false;         ///< The first line is text/secondary (No pages 1390:34269)
        bool isNameMuted = false;               ///< The name is text/tertiary: the file adds no page (378:2809, 1390:34259, 1390:34268)
        QString secondLine;                     ///< "Pages 1–6 of the result"
        QString linkText;                       ///< "Enter password…", "Remove file", empty = none
        double progress = -1.0;                 ///< The reading (0 - 1), -1 = no bar
        QString toolTip;                        ///< The whole reason of a state (a line is elided)
        bool hasThumbnail = false;              ///< The slot of a thumbnail 56 x 43 instead of the tile (Images to PDF 336:1976)
        QImage thumbnail;                       ///< The picture of the thumbnail (a value; null = the empty slot while it is decoded)

        bool operator==(const Content&) const = default;
    };

    explicit PDFAppToolInputRow(QWidget* parent);
    virtual ~PDFAppToolInputRow() override;

    const Content& getContent() const { return m_content; }
    void setContent(const Content& content);

    const QString& getInputId() const { return m_content.inputId; }

    /// The current row (the input of the current page of the canvas)
    bool isCurrent() const { return m_isCurrent; }
    void setCurrent(bool isCurrent);

    /// A row of a selection of more rows (Ctrl or Shift, 336:1976): drawn as the current one
    bool isSelected() const { return m_isSelected; }
    void setSelected(bool isSelected);

    /// The keys held by the last click of the row (Ctrl, Shift; none for a key of the keyboard)
    Qt::KeyboardModifiers getClickModifiers() const { return m_clickModifiers; }

    /// The row can be dragged (false while the tool runs)
    void setDraggable(bool isDraggable);

    PDFTag* getTag() const { return m_tag.data(); }
    PDFTextLink* getLink() const { return m_link.data(); }

    /// Height of the content
    static int getHeight(const Content& content);

Q_SIGNALS:
    /// The link has been clicked
    void linkActivated(const QString& inputId);

    /// Delete on the row
    void removeRequested(const QString& inputId);

    /// Alt + Up (-1) / Alt + Down (+1)
    void moveRequested(const QString& inputId, int delta);

    /// The row is dragged to the global position / dropped there
    void dragMoved(const QString& inputId, const QPoint& globalPosition);
    void dragFinished(const QString& inputId, const QPoint& globalPosition, bool isCanceled);

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const override;
    virtual QSize getContentSizeHint() const override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateChildren();
    void onLinkClicked();
    int getTextLeft() const;
    int getTextWidth() const;

    Content m_content;
    QPointer<PDFTag> m_tag;
    QPointer<PDFTextLink> m_link;
    QPoint m_pressPosition;
    Qt::KeyboardModifiers m_clickModifiers = Qt::NoModifier;
    bool m_isCurrent = false;
    bool m_isSelected = false;
    bool m_isDraggable = true;
    bool m_isPressed = false;
    bool m_isDragging = false;
};

/// The panel Files of a tool with more inputs (sidebar-left 378:1076): the header "Files" Semi
/// Bold 14 with its line Regular 11 text/tertiary ("3 files · 16 pages · 18.6 MB"), the rows of
/// the inputs 10 apart and the hint Regular 11 text/muted below them ("Drag a file to change
/// the order…"; 378:1657 without files). A row dragged over the panel shows the place of its
/// drop (a line bg/accent between the rows). The rows are made again only when the inputs
/// change their order or their set; a change of a row updates it in its place (a reading of a
/// file only moves its bar).
///
/// Ownership: the panel owns the rows; a row, which goes, is deleted later (it may be the
/// sender). The signals are emitted last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputFilesPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int ROW_GAP = 10;
    static constexpr int SIDE_MARGIN = 2;   ///< The rows of 378:1076 are 275 wide in the content of 279

    explicit PDFAppToolInputFilesPanel(QWidget* parent);
    virtual ~PDFAppToolInputFilesPanel() override;

    void setHeader(const QString& title, const QString& subtitle);
    void setHint(const QString& hint);

    /// The rows in their order
    void setRows(const std::vector<PDFAppToolInputRow::Content>& rows);

    /// The current row (empty = none)
    void setCurrentInput(const QString& inputId);

    /// The rows of a selection of more rows besides the current one (Images to PDF 336:1976:
    /// "Ctrl or Shift selects more images"); empty = only the current row
    void setSelectedInputs(const QStringList& inputIds);

    /// The keys held by the last click of a row (rowClicked: Ctrl adds, Shift extends)
    Qt::KeyboardModifiers getLastClickModifiers() const { return m_lastClickModifiers; }

    /// The rows can be dragged and moved (false while the tool runs)
    void setEditable(bool isEditable);

    PDFAppToolInputRow* findRow(const QString& inputId) const;
    std::vector<PDFAppToolInputRow*> getRows() const;
    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFLabel* getSubtitleLabel() const { return m_subtitle.data(); }
    PDFAppParagraph* getHintLabel() const { return m_hint.data(); }

    /// Index, in front of which a row dropped at the global position goes (the count = the end)
    int getDropIndex(const QPoint& globalPosition) const;

Q_SIGNALS:
    void rowClicked(const QString& inputId);
    void linkActivated(const QString& inputId);
    void removeRequested(const QString& inputId);

    /// The input moves to the index (in the order before the move: the index of the item, in
    /// front of which it goes; the count = to the end)
    void moveRequested(const QString& inputId, int targetIndex);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void onRowClicked();
    void onRowMoveRequested(const QString& inputId, int delta);
    void onRowDragMoved(const QString& inputId, const QPoint& globalPosition);
    void onRowDragFinished(const QString& inputId, const QPoint& globalPosition, bool isCanceled);
    PDFAppToolInputRow* createRow(const PDFAppToolInputRow::Content& content);

    QPointer<PDFLabel> m_title;
    QPointer<PDFLabel> m_subtitle;
    QPointer<PDFAppParagraph> m_hint;
    QPointer<QWidget> m_rowsWidget;
    QPointer<QVBoxLayout> m_rowsLayout;
    std::vector<QPointer<PDFAppToolInputRow>> m_rows;
    QString m_currentInput;
    QStringList m_selectedInputs;
    Qt::KeyboardModifiers m_lastClickModifiers = Qt::NoModifier;
    int m_dropIndex = -1;
    bool m_isEditable = true;
};

}   // namespace pdf

#endif // PDFAPPTOOLINPUTROW_H
