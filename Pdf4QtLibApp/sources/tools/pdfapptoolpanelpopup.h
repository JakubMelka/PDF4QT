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

#ifndef PDFAPPTOOLPANELPOPUP_H
#define PDFAPPTOOLPANELPOPUP_H

#include "pdfappdocumentmenus.h"

#include "pdfcomponentrows.h"

#include <QPointer>

namespace pdf
{

/// Panel of an action of the toolbar of a tool (panel-steps 303:4560, 438:15032,
/// 438:12831, 339:8765): a popover below the toolbar at its button, with the arrows and
/// the switching of the panels of PDFAppDocumentPanelPopup. Its rows are PDFTileRow,
/// its caption row is Medium 11 text/tertiary in a row of 19 (313:7345), the divider
/// row has 13, the rows of the links 18 with 10 px at the leading side (313:7347).
/// The panel has paddings of 8 + its border, 12 + the border above a caption and
/// below the links; the items of the content are 2 px apart.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPanelPopup : public PDFAppDocumentPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanelPopup;

public:
    /// Paddings inside of the panel with its border (panel-steps 303:4560)
    static constexpr int PADDING = 9;

    /// Space above a caption and below the links of the panel, over the padding (the caption
    /// at y 13, the bottom of the links 13 from the edge)
    static constexpr int CAPTION_TOP = 4;
    static constexpr int LINKS_BOTTOM = 4;
    static constexpr int ITEM_GAP = 2;

    /// \param view View of the document, may be null
    /// \param panelWidth Width of the panel with its border
    /// \param parent Parent widget
    explicit PDFAppToolPanelPopup(PDFAppDocumentView* view, int panelWidth, QWidget* parent);
    virtual ~PDFAppToolPanelPopup() override;

    /// Adds the caption of a panel of steps ("Optimization steps", caption-row 313:7345,
    /// Medium 11): the first one lies at the top
    PDFLabel* addCaption(const QString& text);

    /// Adds the caption of a group of rows ("FORMAT", "Presets tick the list…", caption
    /// 339:9500, Semi Bold 11 in a row of 21)
    PDFLabel* addGroupCaption(const QString& text);

    /// Adds a row: the title, the description, the icon of its tile
    PDFTileRow* addRow(const QString& title, const QString& description, const QString& iconName, PDFTileRow::Kind kind);

    /// Adds the divider row (divider-row 313:7346)
    QWidget* addDividerRow();

    /// Adds the divider of the groups of rows (divider 339:9572: 9 high, the line 4 from the edges)
    QWidget* addGroupDivider();

    /// The button of the toolbar, whose panel this is, nullptr = another parent
    QToolButton* getButton() const;

    /// Adds a link at the end of the panel ("Restore defaults"). The caller connects it.
    PDFTextLink* addLink(const QString& text);

    /// Rows of the panel in their order
    const std::vector<QPointer<PDFTileRow>>& getRows() const { return m_rows; }

protected:
    virtual QMargins getContentPadding() const override;

private:
    std::vector<QPointer<PDFTileRow>> m_rows;
};

}   // namespace pdf

#endif // PDFAPPTOOLPANELPOPUP_H
