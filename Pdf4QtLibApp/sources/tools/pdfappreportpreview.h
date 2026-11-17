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

#ifndef PDFAPPREPORTPREVIEW_H
#define PDFAPPREPORTPREVIEW_H

#include "pdfappglobal.h"
#include "pdfdocumenttooltablewriter.h"

#include <QWidget>

#include <vector>

namespace pdf
{

/// The read-only preview of Document Report (plan of step 7 of phase 2, 4.4; package P2; Figma 484:15472,
/// 484:16862 …): the sections of the model of PDFDocumentToolTableWriter (headings, paragraphs, tables, blocks of
/// code) drawn by the tokens of Fluent - never QPalette, never the HTML of the report (a text of the document is
/// a text, never markup). The widget is the white card of the canvas; its height is the height of its content
/// (the GUI puts it into a scroll area). A large report is laid out once and drawn lazily - only the blocks in
/// the rectangle of the paint event; a table shows at most MAXIMUM_TABLE_ROWS rows and a block of code at most
/// MAXIMUM_CODE_LINES lines with a note "… follow in the saved report" (the saved report is whole). Numbers and
/// dates of the cells by PDFAppToolValueFormatter (PDFAppLocale, O17). GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppReportPreview : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Most rows of a table in the preview
    static constexpr int MAXIMUM_TABLE_ROWS = 500;

    /// Most lines of a block of code in the preview
    static constexpr int MAXIMUM_CODE_LINES = 2000;

    /// Deepest section, which is drawn (PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH)
    static constexpr int MAXIMUM_DEPTH = PDFDocumentToolTableWriter::MAXIMUM_SECTION_DEPTH;

    /// Geometry of 484:15472 (the card 774 wide, padding 24)
    static constexpr int PADDING = 24;
    static constexpr int ROW_HEIGHT = 26;

    explicit PDFAppReportPreview(QWidget* parent = nullptr);
    virtual ~PDFAppReportPreview() override;

    /// The shown report (a copy of the model) and the format of its dates
    void setReport(const PDFDocumentToolTableSection& report, PDFDocumentToolDateFormat dateFormat = PDFDocumentToolDateFormat::Short);
    const PDFDocumentToolTableSection& getReport() const { return m_report; }

    /// The preview is dimmed while the report is created (484:18372)
    void setDimmed(bool isDimmed);
    bool isDimmed() const { return m_isDimmed; }

    /// The top of the heading of the section of the name (the first one), -1 = none (the GUI scrolls to it)
    int getSectionTop(const QString& sectionName) const;

    /// The texts of the laid out blocks in their order (tests: what the preview shows)
    QStringList getBlockTexts() const;

    /// The text of a cell (a number or a date by the formatter of the application)
    QString getCellText(const QVariant& value, const PDFDocumentToolTableColumn& column) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
    virtual bool hasHeightForWidth() const override { return true; }
    virtual int heightForWidth(int width) const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    /// A laid out block of the preview
    struct Block
    {
        enum class Kind
        {
            Title,      ///< The title of the report (Semi Bold 16)
            Heading,    ///< The title of a section (Semi Bold 13)
            Paragraph,  ///< A paragraph (Regular 11, text/tertiary)
            Code,       ///< A block of code (bg/subtle, Regular 12)
            Table       ///< A table (a card with its head)
        };

        Kind kind = Kind::Paragraph;
        int top = 0;
        int height = 0;
        int indent = 0;
        QString text;                       ///< Title, Heading, Paragraph: the text (wrapped); Code: the lines
        QString sectionName;                ///< Heading: the name of its section
        const PDFDocumentToolTable* table = nullptr;    ///< Table: the table of m_report
        int shownRows = 0;                  ///< Table: the rows drawn
        std::vector<int> columnWidths;      ///< Table: the widths of its columns
    };

    void layoutBlocks(int width);
    void addSectionBlocks(const PDFDocumentToolTableSection& section, int depth, int width, int* y);
    void paintBlock(QPainter* painter, const Block& block) const;
    void paintTable(QPainter* painter, const Block& block) const;
    void onThemeChanged();

    PDFDocumentToolTableSection m_report;
    PDFDocumentToolDateFormat m_dateFormat = PDFDocumentToolDateFormat::Short;
    std::vector<Block> m_blocks;
    int m_layoutWidth = -1;
    int m_contentHeight = 0;
    bool m_isDimmed = false;
};

}   // namespace pdf

#endif // PDFAPPREPORTPREVIEW_H
