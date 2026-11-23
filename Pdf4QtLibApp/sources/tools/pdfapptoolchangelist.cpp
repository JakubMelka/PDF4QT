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

#include "pdfapptoolchangelist.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace pdf
{

PDFAppToolChangeList::PDFAppToolChangeList(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("changeList"));
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // card-changes 433:5145: padding 8 + the border, the caption 21, the rows 2 apart
    PDFAppToolCard* card = new PDFAppToolCard(this);
    card->setObjectName(QStringLiteral("changeCard"));
    card->getBodyLayout()->setContentsMargins(9, 9, 9, 9);
    card->getBodyLayout()->setSpacing(2);
    layout->addWidget(card);
    m_bodyLayout = card->getBodyLayout();

    m_caption = new PDFTileRow(QString(), QString(), QString(), PDFTileRow::Kind::Caption, card);
    m_caption->setObjectName(QStringLiteral("changeCaption"));
    m_caption->setClickable(false);
    m_bodyLayout->addWidget(m_caption);
}

PDFAppToolChangeList::~PDFAppToolChangeList() = default;

void PDFAppToolChangeList::setCaption(const QString& caption)
{
    if (m_caption)
    {
        m_caption->setTitle(caption);
    }
}

void PDFAppToolChangeList::setChanges(std::vector<Change> changes)
{
    m_changes = std::move(changes);

    // The same number of rows keeps the widgets (a change of the language, a new version)
    if (m_rows.size() != m_changes.size())
    {
        for (RowParts& parts : m_rows)
        {
            if (parts.widget)
            {
                // Retired (step A: a rebuilt row), deleted later
                PDFDeferredDeletion::retire(parts.widget.data());
            }
        }
        m_rows.clear();

        QWidget* card = m_caption ? m_caption->parentWidget() : nullptr;
        for (size_t i = 0; card && i < m_changes.size(); ++i)
        {
            // item 433:5153: the tile row and the change 10 from the trailing edge
            QWidget* rowWidget = new QWidget(card);
            rowWidget->setObjectName(QStringLiteral("changeRow-%1").arg(i));
            QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(0, 0, 10, 0);
            rowLayout->setSpacing(6);

            RowParts parts;
            parts.widget = rowWidget;
            parts.row = new PDFTileRow(QString(), QString(), QString(), PDFTileRow::Kind::ListItem, rowWidget);
            parts.row->setClickable(false);
            rowLayout->addWidget(parts.row, 1);

            parts.before = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, rowWidget);
            parts.before->setObjectName(QStringLiteral("changeBefore"));
            rowLayout->addWidget(parts.before, 0, Qt::AlignVCenter);

            PDFIconBadge* chevron = new PDFIconBadge(QStringLiteral("chevron-right"), 14, 14, rowWidget);
            chevron->setColors(std::nullopt, PDFThemeColors::TextMuted);
            chevron->setFixedSize(14, 14);
            rowLayout->addWidget(chevron, 0, Qt::AlignVCenter);

            parts.after = PDFAppPageWidgets::createLabel(QString(), QFont::Medium, 12, PDFThemeColors::TextPrimary, rowWidget);
            parts.after->setObjectName(QStringLiteral("changeAfter"));
            rowLayout->addWidget(parts.after, 0, Qt::AlignVCenter);

            m_bodyLayout->addWidget(rowWidget);
            m_rows.push_back(parts);
        }
    }

    for (size_t i = 0; i < m_rows.size() && i < m_changes.size(); ++i)
    {
        const Change& change = m_changes[i];
        RowParts& parts = m_rows[i];
        if (parts.row)
        {
            parts.row->setTitle(change.title);
            parts.row->setDescription(change.description);
            parts.row->setIconName(change.iconName);
        }
        if (parts.before)
        {
            parts.before->setText(change.before);
        }
        if (parts.after)
        {
            parts.after->setText(change.after);
        }
    }
}

PDFTileRow* PDFAppToolChangeList::getRow(int index) const
{
    return index >= 0 && size_t(index) < m_rows.size() ? m_rows[size_t(index)].row.data() : nullptr;
}

PDFLabel* PDFAppToolChangeList::getBeforeLabel(int index) const
{
    return index >= 0 && size_t(index) < m_rows.size() ? m_rows[size_t(index)].before.data() : nullptr;
}

PDFLabel* PDFAppToolChangeList::getAfterLabel(int index) const
{
    return index >= 0 && size_t(index) < m_rows.size() ? m_rows[size_t(index)].after.data() : nullptr;
}

}   // namespace pdf

#include "moc_pdfapptoolchangelist.cpp"
