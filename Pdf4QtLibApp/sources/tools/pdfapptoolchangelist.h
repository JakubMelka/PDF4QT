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

#ifndef PDFAPPTOOLCHANGELIST_H
#define PDFAPPTOOLCHANGELIST_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <vector>

class QVBoxLayout;

namespace pdf
{

class PDFLabel;
class PDFTileRow;

/// "What changes" of Remove Password (Figma 433:5022, 433:5811; plan of step 5 of phase 2, 4.2;
/// package P2): the rows Encryption / Open password / Permissions with their state before › after
/// in a card with the caption "What changes" (433:5145: padding 8 + the border, the caption 21,
/// the rows of 44 with their tile 2 apart; before Regular 12 text/secondary, the chevron 14,
/// after Medium 12 text/primary, 6 apart). A child of the canvas, held by the GUI in QPointer;
/// its parts are its children, it emits nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolChangeList : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// One row: what changes, its state now and after the run ("AES 256-bit" › "None")
    struct Change
    {
        QString title;
        QString before;
        QString after;
        QString description;    ///< The line below the title ("Contents are encrypted in the file")
        QString iconName;       ///< The icon of the tile (433:5155 icon/key)
    };

    explicit PDFAppToolChangeList(QWidget* parent);
    virtual ~PDFAppToolChangeList() override;

    /// Shows the rows
    void setChanges(std::vector<Change> changes);
    const std::vector<Change>& getChanges() const { return m_changes; }

    /// The caption of the card ("What changes")
    void setCaption(const QString& caption);

    /// The tile row of a change and its texts before / after (nullptr = none)
    PDFTileRow* getRow(int index) const;
    PDFLabel* getBeforeLabel(int index) const;
    PDFLabel* getAfterLabel(int index) const;

private:
    struct RowParts
    {
        QPointer<QWidget> widget;
        QPointer<PDFTileRow> row;
        QPointer<PDFLabel> before;
        QPointer<PDFLabel> after;
    };

    std::vector<Change> m_changes;
    std::vector<RowParts> m_rows;
    QPointer<PDFTileRow> m_caption;
    QVBoxLayout* m_bodyLayout = nullptr;
};

}   // namespace pdf

#endif // PDFAPPTOOLCHANGELIST_H
