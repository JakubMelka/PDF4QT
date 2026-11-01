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

#include "pdfapptoolpanelpopup.h"

#include "pdfapppagelayout.h"
#include "pdfappdocumenttoolbar.h"

#include "pdfcomponentbuttons.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

PDFAppToolPanelPopup::PDFAppToolPanelPopup(PDFAppDocumentView* view, int panelWidth, QWidget* parent) :
    BaseClass(view, panelWidth, parent)
{
    getContentLayout()->setSpacing(ITEM_GAP);
}

PDFAppToolPanelPopup::~PDFAppToolPanelPopup() = default;

QMargins PDFAppToolPanelPopup::getContentPadding() const
{
    return QMargins(PADDING, PADDING, PADDING, PADDING);
}

PDFLabel* PDFAppToolPanelPopup::addCaption(const QString& text)
{
    // caption-row 313:7345: the paddings 0 / 10 / 6, Medium 11 text/tertiary; the first
    // one lies 13 px below the top of the panel
    QWidget* row = new QWidget(this);
    row->setObjectName(QStringLiteral("toolPanelCaptionRow"));
    QVBoxLayout* layout = new QVBoxLayout(row);
    layout->setContentsMargins(10, getContentLayout()->count() == 0 ? CAPTION_TOP : 0, 10, 6);
    layout->setSpacing(0);

    PDFLabel* label = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::Medium, 11), 13, PDFThemeColors::TextTertiary, row);
    label->setObjectName(QStringLiteral("toolPanelCaption"));
    layout->addWidget(label);
    getContentLayout()->addWidget(row);
    return label;
}

PDFLabel* PDFAppToolPanelPopup::addGroupCaption(const QString& text)
{
    // caption 339:9500: the paddings 6 / 10 / 2, Semi Bold 11 text/tertiary
    QWidget* row = new QWidget(this);
    row->setObjectName(QStringLiteral("toolPanelGroupCaptionRow"));
    QVBoxLayout* layout = new QVBoxLayout(row);
    layout->setContentsMargins(10, 6, 10, 2);
    layout->setSpacing(0);

    PDFLabel* label = PDFAppPageWidgets::createText(text, PDFThemeFonts::getFont(QFont::DemiBold, 11), 13, PDFThemeColors::TextTertiary, row);
    label->setObjectName(QStringLiteral("toolPanelGroupCaption"));
    layout->addWidget(label);
    getContentLayout()->addWidget(row);
    return label;
}

QToolButton* PDFAppToolPanelPopup::getButton() const
{
    return qobject_cast<QToolButton*>(parentWidget());
}

QWidget* PDFAppToolPanelPopup::addGroupDivider()
{
    // divider 339:9572: 9 high, the line 4 px from the edges of the row
    QWidget* row = new QWidget(this);
    row->setObjectName(QStringLiteral("toolPanelGroupDivider"));
    QVBoxLayout* layout = new QVBoxLayout(row);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);
    layout->addWidget(new PDFAppToolbarDivider(Qt::Horizontal, 0, 0, PDFThemeColors::BorderDivider, row));
    getContentLayout()->addWidget(row);
    return row;
}

PDFTileRow* PDFAppToolPanelPopup::addRow(const QString& title, const QString& description, const QString& iconName, PDFTileRow::Kind kind)
{
    PDFTileRow* row = new PDFTileRow(title, description, iconName, kind, this);
    getContentLayout()->addWidget(row);
    m_rows.emplace_back(row);
    return row;
}

QWidget* PDFAppToolPanelPopup::addDividerRow()
{
    // divider-row 313:7346: 13 high with the line in its middle
    QWidget* row = new PDFAppToolbarDivider(Qt::Horizontal, 0, 6, PDFThemeColors::BorderDivider, this);
    row->setObjectName(QStringLiteral("toolPanelDivider"));
    row->setFixedHeight(13);
    getContentLayout()->addWidget(row);
    return row;
}

PDFTextLink* PDFAppToolPanelPopup::addLink(const QString& text)
{
    // links-row 313:7347: 18 high, the link 10 px from the edge, 13 px from the bottom
    QWidget* row = new QWidget(this);
    row->setObjectName(QStringLiteral("toolPanelLinkRow"));
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 2, 10, LINKS_BOTTOM);
    layout->setSpacing(12);

    PDFTextLink* link = new PDFTextLink(text, row);
    layout->addWidget(link);
    layout->addStretch(1);
    getContentLayout()->addWidget(row);
    return link;
}

}   // namespace pdf

#include "moc_pdfapptoolpanelpopup.cpp"
