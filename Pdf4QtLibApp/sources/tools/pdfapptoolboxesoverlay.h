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

#ifndef PDFAPPTOOLBOXESOVERLAY_H
#define PDFAPPTOOLBOXESOVERLAY_H

#include "pdfappglobal.h"
#include "pdfthemecolors.h"

#include <QPointer>
#include <QRectF>
#include <QWidget>

#include <vector>

namespace pdf
{

class PDFAppToolPageStage;

/// Outlines of rectangles over the page or the sheet of a stage (plan of step 3 of phase 2, 4.3):
/// the five page boxes of Set Page Boxes (391:26688: media icon of the neutral accent solid, crop
/// blue dashed, bleed red, trim green, art violet dashed; the box, which is written, is thick) and
/// the margins of Resize Pages (391:23146 the area inside of the margins dashed, 391:25272 red,
/// when the margins leave no room). The rectangles are never mirrored for right-to-left; the
/// overlay takes no input (the mouse goes to the stage).
///
/// Ownership: a child of its stage (PDFAppToolPageStage::addOverlay), it holds the stage by a
/// QPointer; the GUI of the tool holds it by a QPointer. No signals. Thread: GUI only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageBoxesOverlay : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Width of a box and of the box, which is written (391:26688)
    static constexpr qreal BOX_WIDTH = 1.0;
    static constexpr qreal THICK_WIDTH = 2.5;

    /// One outline
    struct Outline
    {
        QRectF rect;                                            ///< The rectangle (page or sheet coordinates of the stage)
        PDFThemeColors::Color color = PDFThemeColors::BgAccent; ///< The color token
        bool isDashed = false;
        bool isThick = false;

        bool operator==(const Outline&) const = default;
    };

    /// The coordinates of the rectangles
    enum class Coordinates
    {
        Page,   ///< PDFAppToolPageStage::getPageToStageTransform
        Sheet   ///< PDFAppToolPageStage::getSheetToStageTransform
    };

    explicit PDFAppPageBoxesOverlay(PDFAppToolPageStage* stage, Coordinates coordinates);
    virtual ~PDFAppPageBoxesOverlay() override;

    void setOutlines(std::vector<Outline> outlines);
    const std::vector<Outline>& getOutlines() const { return m_outlines; }

    /// The rectangle of an outline in the coordinates of the overlay
    QRectF mapToOverlay(const QRectF& rect) const;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<PDFAppToolPageStage> m_stage;
    Coordinates m_coordinates;
    std::vector<Outline> m_outlines;
};

/// The legend of the page boxes below the preview of Set Page Boxes (391:26688): an outlined
/// square 10 x 10 of the color of a box and its name (Regular 11 text/secondary; the box, which
/// is written, Semi Bold text/primary), the items 12 apart and centered. No input, no signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageBoxesLegend : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int SWATCH_SIZE = 10;
    static constexpr int SWATCH_GAP = 5;
    static constexpr int ITEM_GAP = 12;
    static constexpr int HEIGHT = 18;

    /// An item of the legend
    struct Item
    {
        QString title;
        PDFThemeColors::Color color = PDFThemeColors::BgAccent;
        bool isBold = false;

        bool operator==(const Item&) const = default;
    };

    explicit PDFAppPageBoxesLegend(QWidget* parent);
    virtual ~PDFAppPageBoxesLegend() override;

    void setItems(std::vector<Item> items);
    const std::vector<Item>& getItems() const { return m_items; }

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QFont getFont(bool isBold) const;
    int getItemWidth(const Item& item) const;

    std::vector<Item> m_items;
};

}   // namespace pdf

#endif // PDFAPPTOOLBOXESOVERLAY_H
