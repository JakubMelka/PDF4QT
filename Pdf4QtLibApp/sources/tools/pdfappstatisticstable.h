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

#ifndef PDFAPPSTATISTICSTABLE_H
#define PDFAPPSTATISTICSTABLE_H

#include "pdfappglobal.h"
#include "pdfthemecolors.h"

#include <QJsonObject>
#include <QWidget>

#include <vector>

namespace pdf
{

/// A message in the middle of a canvas of an analysis tool: an icon, a title and a text on bg/track (Statistics
/// 480:15218 "Analyzing the document…", Report 484:20937 "Check at least one section"). Painted by the tokens of
/// Fluent only; the three canvases of step 7a share it. GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCanvasMessage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolCanvasMessage(QWidget* parent = nullptr);
    virtual ~PDFAppToolCanvasMessage() override;

    /// The shown message (an empty title = nothing is drawn but the background)
    void setMessage(const QString& iconName, const QString& title, const QString& text);

    const QString& getIconName() const { return m_iconName; }
    const QString& getTitle() const { return m_title; }
    const QString& getText() const { return m_text; }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void onThemeChanged();

    QString m_iconName;
    QString m_title;
    QString m_text;
};

/// A row of the canvas of Document Statistics: a class of the objects (480:14219) or a type of the values
/// (480:14555)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStatisticsRow
{
public:
    QString id;             ///< The identifier of the data ("image", "dictionary")
    QString title;          ///< Translated title ("Image", "Dictionary")
    double share = 0.0;     ///< Share in percent (0 to 100): of the object bytes (classes), of the values (types, K7.13)
    qint64 count = 0;       ///< Number of the objects / values
    qint64 bytes = -1;      ///< Object bytes of a class (B186); -1 for a type of the values
};

/// The canvas of Document Statistics (plan of step 7 of phase 2, 4.2; Figma 480:14219, 480:14555): the head
/// "Statistics by object function" with "4,819,915 bytes in 10,275 objects", the stacked bar of the shares and
/// the table (Class / Type, Share (%), the bar of the share, Count, Size (bytes) - the last only by function).
/// The rows are sorted by their share; the colors of the rows are the categorical tokens chart/1…12 in the
/// order of the rows (K7.25); the numbers by PDFAppLocale (K7.26). A click or the arrows choose a row (emits
/// selectedRowChanged for the right panel Selected). Painted by the tokens of Fluent only, never QPalette.
/// GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppStatisticsTable : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 480:14219 (the canvas has a padding of 20 around this widget)
    static constexpr int HEAD_HEIGHT = 36;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 28;
    static constexpr int TABLE_HEAD_HEIGHT = 30;   ///< 29 + the border of the card
    static constexpr int ROW_HEIGHT = 34;
    static constexpr int CELL_PADDING = 12;
    static constexpr int NAME_WIDTH = 170;
    static constexpr int SHARE_WIDTH = 90;
    static constexpr int COUNT_WIDTH = 80;
    static constexpr int BYTES_WIDTH = 110;        ///< Not SIZE_WIDTH: a macro of <stdint.h> (C23) on glibc
    static constexpr int MINIMUM_SEGMENT = 2;      ///< The narrowest segment of a row with a share (the 2 px of 480:14385)
    static constexpr int MINIMUM_CELL_BAR = 3;     ///< The narrowest bar of a cell (the 3 px of 480:14433)

    explicit PDFAppStatisticsTable(QWidget* parent = nullptr);
    virtual ~PDFAppStatisticsTable() override;

    /// The data of the result of the core tool (PDFDocumentToolStatistics::DATA_*) and the view ("function", "type");
    /// the chosen row is kept by its id when the new data have it, else the first row is chosen
    void setData(const QJsonObject& statistics, const QString& view);

    /// The data are gone (a new version of the document is being analyzed)
    void clear();

    const QJsonObject& getData() const { return m_data; }
    const QString& getView() const { return m_view; }
    bool isTypeView() const;

    /// The rows in their order (by the share, the largest first)
    const std::vector<PDFAppStatisticsRow>& getRows() const { return m_rows; }

    /// The chosen row, -1 = none
    int getSelectedRow() const { return m_selectedRow; }
    void setSelectedRow(int row);
    const PDFAppStatisticsRow* getSelectedRowData() const;

    /// The texts of the head ("Statistics by object function", "4,819,915 bytes in 10,275 objects")
    QString getTitle() const;
    QString getSummary() const;

    /// What a screen reader says of the painted table (RS7APP L8; the accessible description): the head, the
    /// chosen row and every row with the values of its cells ("Image: share 61.42 %, count 12, size 2,961,000
    /// bytes"), in the language and the numbers of the user interface
    QString getAccessibleText() const;

    /// The rectangle of a row in the coordinates of the widget (tests, the stacked bar)
    QRect getRowRect(int row) const;
    QRect getStackedBarRect() const;
    QRect getSegmentRect(int row) const;

    /// The rows of the data of a view (sorted by their share, the largest first; a row without any object is
    /// left out); the share is taken from the data (never computed again here, K7.13)
    static std::vector<PDFAppStatisticsRow> createRows(const QJsonObject& data, const QString& view);

    /// Translated title of a class of the objects ("image" → "Image") and of a type of the values
    /// ("dictionary" → "Dictionary"); an unknown id is shown as it is
    static QString getClassTitle(const QString& id);
    static QString getTypeTitle(const QString& id);

    /// The subject of the status message of a class ("image" → "Images", 480:14255)
    static QString getClassPluralTitle(const QString& id);

    /// The categorical color of the row (chart/1…12, repeated after 12)
    static PDFThemeColors::Color getChartColor(int row);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The chosen row has changed (-1 = none); emitted last
    void selectedRowChanged(int row);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    void onThemeChanged();
    int getTableTop() const;
    int getRowAt(const QPoint& position) const;
    void paintTable(QPainter* painter) const;
    void paintStackedBar(QPainter* painter) const;

    /// The text of one row for getAccessibleText
    QString getAccessibleRowText(const PDFAppStatisticsRow& row) const;

    /// Sets getAccessibleText as the accessible description
    void updateAccessibleText();

    QJsonObject m_data;
    QString m_view;
    std::vector<PDFAppStatisticsRow> m_rows;
    int m_selectedRow = -1;
};

}   // namespace pdf

#endif // PDFAPPSTATISTICSTABLE_H
