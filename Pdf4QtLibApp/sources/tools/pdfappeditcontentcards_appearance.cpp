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

#include "pdfappeditcontentcards_appearance.h"

#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfcolorspaces.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentsections.h"
#include "pdfcontentcommands.h"
#include "pdfcontentdisplay.h"
#include "pdfcontenteditsession.h"
#include "pdffluentwidgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

namespace pdf
{

// Edit Content, stage E1, package P13: the cards Fill and stroke (the device color spaces) and Transparency
// (zadani_gui.md 4.19; the frames -path-selected, -fill-and-stroke, -color-cmyk, -transparency, -multi-selected).

/// A color of a paint as the content sets it (zadani_jadro.md 4.8: never converted): the color space, its
/// components and the display color
class PDFAppEditContentPaintColor
{
public:
    QByteArray space;                   ///< "DeviceGray", "DeviceRGB", "DeviceCMYK", or the name of another space ("ICCBased", "Pattern")
    std::vector<PDFReal> components;    ///< The components of a device space (empty for another space)
    QColor color;                       ///< The display color

    bool isDevice() const { return space == "DeviceGray" || space == "DeviceRGB" || space == "DeviceCMYK"; }

    /// The same color of the content (the display color is not compared: it follows from the rest)
    bool operator==(const PDFAppEditContentPaintColor& other) const { return space == other.space && components == other.components; }
};

/// Helpers of the appearance cards
class PDFAppEditContentAppearanceHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentAppearanceHelper)

public:
    PDFAppEditContentAppearanceHelper() = delete;

    /// Most swatches of a row (-color-cmyk: seven colors and the eyedropper)
    static constexpr int SWATCH_COUNT = 7;

    /// The blend modes of the combo box in the order of the enum (ISO 32000-2, Table 134, Table 135)
    static constexpr std::array<const char*, 16> BLEND_MODE_NAMES = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "ColorDodge", "ColorBurn",
                                                                      "HardLight", "SoftLight", "Difference", "Exclusion", "Hue", "Saturation", "Color", "Luminosity" };

    static QString getBlendModeTitle(int index)
    {
        switch (index)
        {
            case 0: return tr("Normal");
            case 1: return tr("Multiply");
            case 2: return tr("Screen");
            case 3: return tr("Overlay");
            case 4: return tr("Darken");
            case 5: return tr("Lighten");
            case 6: return tr("Color Dodge");
            case 7: return tr("Color Burn");
            case 8: return tr("Hard Light");
            case 9: return tr("Soft Light");
            case 10: return tr("Difference");
            case 11: return tr("Exclusion");
            case 12: return tr("Hue");
            case 13: return tr("Saturation");
            case 14: return tr("Color");
            case 15: return tr("Luminosity");
            default: return QString();
        }
    }

    /// The index of the blend mode in BLEND_MODE_NAMES (Compatible is Normal), -1 for another one
    static int getBlendModeIndex(BlendMode mode)
    {
        if (mode == BlendMode::Compatible)
        {
            return 0;
        }
        const int index = int(mode);
        return index >= 0 && index < int(BLEND_MODE_NAMES.size()) ? index : -1;
    }

    /// The name of a color space of the processor
    static QByteArray getSpaceName(const PDFAbstractColorSpace* colorSpace)
    {
        if (!colorSpace)
        {
            return QByteArray();
        }

        switch (colorSpace->getColorSpace())
        {
            case PDFAbstractColorSpace::ColorSpace::DeviceGray: return "DeviceGray";
            case PDFAbstractColorSpace::ColorSpace::DeviceRGB: return "DeviceRGB";
            case PDFAbstractColorSpace::ColorSpace::DeviceCMYK: return "DeviceCMYK";
            case PDFAbstractColorSpace::ColorSpace::CalGray: return "CalGray";
            case PDFAbstractColorSpace::ColorSpace::CalRGB: return "CalRGB";
            case PDFAbstractColorSpace::ColorSpace::Lab: return "Lab";
            case PDFAbstractColorSpace::ColorSpace::ICCBased: return "ICCBased";
            case PDFAbstractColorSpace::ColorSpace::Indexed: return "Indexed";
            case PDFAbstractColorSpace::ColorSpace::Separation: return "Separation";
            case PDFAbstractColorSpace::ColorSpace::DeviceN: return "DeviceN";
            case PDFAbstractColorSpace::ColorSpace::Pattern: return "Pattern";
            case PDFAbstractColorSpace::ColorSpace::Invalid: return QByteArray();
        }
        return QByteArray();
    }

    /// The color of the paint of the leaf: the color of the effective override, else the color of the builder
    static PDFAppEditContentPaintColor getPaintColor(const PDFContentNode& node, const PDFContentDisplayState& state, bool isFill)
    {
        PDFAppEditContentPaintColor result;
        const PDFContentDisplayColor& display = isFill ? state.fillColor : state.strokeColor;
        result.color = display.color;

        if (display.kind == PDFContentDisplayColor::Kind::Pattern)
        {
            result.space = "Pattern";
            return result;
        }

        if (display.isOverridden)
        {
            result.space = !display.value.deviceSpace.isEmpty() ? display.value.deviceSpace : QByteArray("Other");
            if (result.isDevice())
            {
                result.components = display.value.components;
            }
            return result;
        }

        if (!node.getState())
        {
            return result;
        }

        const PDFPageContentProcessorState& processorState = node.getState()->getState();
        result.space = getSpaceName(isFill ? processorState.getFillColorSpace() : processorState.getStrokeColorSpace());
        if (result.isDevice())
        {
            const PDFColor& original = isFill ? processorState.getFillColorOriginal() : processorState.getStrokeColorOriginal();
            for (size_t i = 0; i < original.size(); ++i)
            {
                result.components.push_back(original[i]);
            }
        }
        return result;
    }

    /// The leaf paints a fill / a stroke (a path by its painting operator, a text by its rendering mode)
    static bool hasFill(const PDFContentNode& node, const PDFContentDisplayState& state)
    {
        if (node.getKind() == PDFContentNodeKind::Path)
        {
            return node.getData().paint.fill;
        }
        switch (state.textRenderingMode)
        {
            case TextRenderingMode::Fill:
            case TextRenderingMode::FillStroke:
            case TextRenderingMode::FillClip:
            case TextRenderingMode::FillStrokeClip:
                return true;
            default:
                return false;
        }
    }

    static bool hasStroke(const PDFContentNode& node, const PDFContentDisplayState& state)
    {
        if (node.getKind() == PDFContentNodeKind::Path)
        {
            return node.getData().paint.stroke;
        }
        switch (state.textRenderingMode)
        {
            case TextRenderingMode::Stroke:
            case TextRenderingMode::FillStroke:
            case TextRenderingMode::StrokeClip:
            case TextRenderingMode::FillStrokeClip:
                return true;
            default:
                return false;
        }
    }

    /// The components of the display color in a device space (ISO 32000-2, 10.4.2: gray = 0.3 R + 0.59 G + 0.11 B;
    /// CMYK by the default black generation min(C, M, Y) and the full undercolor removal)
    static std::vector<PDFReal> convert(const QColor& color, const QByteArray& space)
    {
        return convert(color.redF(), color.greenF(), color.blueF(), space);
    }

    /// The color in another device space (R7-2). A color of a device space is converted from its components by ISO
    /// 32000-2, 10.4.2: gray -> RGB r = g = b = gray (10.4.2.2), gray -> CMYK (0, 0, 0, 1 - gray) (10.4.2.3), RGB ->
    /// gray 0.3 R + 0.59 G + 0.11 B (10.4.2.2), RGB -> CMYK by the black generation k = min(C, M, Y) and the full
    /// undercolor removal (10.4.2.4), CMYK -> RGB r = 1 - min(1, C + K) ... (10.4.2.5), CMYK -> gray 1 - min(1, 0.3 C +
    /// 0.59 M + 0.11 Y + K) (10.4.2.3); a color of another space (ICC, Lab, a separation) from its display color.
    static std::vector<PDFReal> convert(const PDFAppEditContentPaintColor& color, const QByteArray& space)
    {
        const std::vector<PDFReal>& c = color.components;
        if (color.space == "DeviceGray" && c.size() == 1)
        {
            if (space == "DeviceRGB")
            {
                return { round(c[0]), round(c[0]), round(c[0]) };
            }
            if (space == "DeviceCMYK")
            {
                return { 0.0, 0.0, 0.0, round(1.0 - c[0]) };
            }
        }
        if (color.space == "DeviceRGB" && c.size() == 3)
        {
            return convert(c[0], c[1], c[2], space);
        }
        if (color.space == "DeviceCMYK" && c.size() == 4)
        {
            if (space == "DeviceGray")
            {
                return { round(1.0 - std::min(1.0, 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2] + c[3])) };
            }
            if (space == "DeviceRGB")
            {
                return { round(1.0 - std::min(1.0, c[0] + c[3])), round(1.0 - std::min(1.0, c[1] + c[3])), round(1.0 - std::min(1.0, c[2] + c[3])) };
            }
        }
        return convert(color.color, space);
    }

    /// An RGB color in a device space (see convert above)
    static std::vector<PDFReal> convert(PDFReal r, PDFReal g, PDFReal b, const QByteArray& space)
    {
        if (space == "DeviceGray")
        {
            return { round(0.3 * r + 0.59 * g + 0.11 * b) };
        }
        if (space == "DeviceCMYK")
        {
            const PDFReal c = 1.0 - r;
            const PDFReal m = 1.0 - g;
            const PDFReal y = 1.0 - b;
            const PDFReal k = std::min({ c, m, y });
            return { round(c - k), round(m - k), round(y - k), round(k) };
        }
        return { round(r), round(g), round(b) };
    }

    /// A component rounded to 4 decimals (the conversion of a display color is no exact value)
    static PDFReal round(PDFReal value)
    {
        return std::round(value * 10000.0) / 10000.0;
    }

    /// The scale of a line width under the matrix (R7-6: a width in the space of the node -> the width on the page): the
    /// square root of |det| - exact for a uniform scale (a rotation, a mirror, the same scale in x and y); \p isUniform
    /// false, when the matrix scales differently by the direction (its two singular values differ; ISO 32000-2, 8.4.3.2:
    /// the line width is a length of the user space)
    static double getLineScale(const QTransform& matrix, bool* isUniform)
    {
        const double a = matrix.m11();
        const double b = matrix.m12();
        const double c = matrix.m21();
        const double d = matrix.m22();
        const double determinant = a * d - b * c;
        const double sum = a * a + b * b + c * c + d * d;

        // The singular values s1, s2: s1^2 + s2^2 = sum, s1 x s2 = |det|; they are equal iff sum^2 = 4 det^2
        if (isUniform)
        {
            *isUniform = std::abs(sum * sum - 4.0 * determinant * determinant) <= 1e-9 * sum * sum;
        }
        return std::sqrt(std::abs(determinant));
    }

    /// The labels of the components of a device space
    static QStringList getComponentLabels(const QByteArray& space)
    {
        if (space == "DeviceGray")
        {
            return { tr("Gray") };
        }
        if (space == "DeviceCMYK")
        {
            return { tr("C"), tr("M"), tr("Y"), tr("K") };
        }
        return { tr("R"), tr("G"), tr("B") };
    }

    static PDFContentStateValue createColorValue(const QByteArray& space, const std::vector<PDFReal>& components)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Color;
        value.color.deviceSpace = space;
        value.color.components = components;
        return value;
    }

    static PDFContentStateValue createRealValue(PDFReal real)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Real;
        value.real = real;
        return value;
    }

    static PDFContentStateValue createIntegerValue(PDFInteger integer)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Integer;
        value.integer = integer;
        return value;
    }

    /// The selection has a painted leaf (the card Fill and stroke is shown)
    static bool hasPaintedLeaf(const PDFAppEditContentContext* context)
    {
        const PDFContentContainerPointer container = context->getContainer();
        if (!container)
        {
            return false;
        }
        bool isTruncated = false;
        return !PDFAppEditContentCardTools::getLeaves(*container, PDFAppEditContentCardTools::getSelectedNodes(context), &PDFAppEditContentCardTools::isPaintedLeaf, &isTruncated).empty();
    }

    /// The dash patterns of the combo box: Solid, Dashed, Dotted (in the units of the line)
    static PDFLineDashPattern getDashPattern(int index)
    {
        PDFLineDashPattern pattern;
        switch (index)
        {
            case 1:
                pattern.setDashArray({ 3.0, 3.0 });
                break;
            case 2:
                pattern.setDashArray({ 1.0, 2.0 });
                break;
            default:
                break;
        }
        return pattern;
    }

    /// The item of the dash pattern, -1 for another pattern
    static int getDashIndex(const PDFLineDashPattern& pattern)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (getDashPattern(i) == pattern)
            {
                return i;
            }
        }
        return -1;
    }

    static int getCapIndex(Qt::PenCapStyle cap)
    {
        switch (cap)
        {
            case Qt::RoundCap: return 1;
            case Qt::SquareCap: return 2;
            default: return 0;
        }
    }

    static int getJoinIndex(Qt::PenJoinStyle join)
    {
        switch (join)
        {
            case Qt::RoundJoin: return 1;
            case Qt::BevelJoin: return 2;
            default: return 0;
        }
    }

    /// A spin box of a value of a card
    static PDFDoubleSpinBox* createRealField(QWidget* parent, double minimum, double maximum, int decimals, double step, const QString& suffix)
    {
        PDFDoubleSpinBox* field = new PDFDoubleSpinBox(parent);
        field->setRange(minimum, maximum);
        field->setDecimals(decimals);
        field->setSingleStep(step);
        field->setKeyboardTracking(false);
        if (!suffix.isEmpty())
        {
            field->setDisplaySuffix(suffix);
        }
        return field;
    }

    /// Shows the value of the selected objects in the field: the value, or the value under "Mixed" (nothing while
    /// the field has the focus: it would overwrite the entry of the user)
    static void setFieldValue(PDFDoubleSpinBox* field, const PDFAppEditContentMixedValue<double>& value, double factor = 1.0)
    {
        if (field->hasFocus())
        {
            return;
        }
        field->setValue(value.getValue().value_or(0.0) * factor);
        field->setMixed(value.isMixed());
    }
};

/// The controls of one paint (the fill or the stroke) of the card Fill and stroke
class PDFAppEditContentPaintControls
{
public:
    QCheckBox* check = nullptr;
    PDFComboBox* space = nullptr;
    QWidget* swatchRow = nullptr;
    QButtonGroup* swatchGroup = nullptr;
    std::vector<PDFColorSwatch*> swatches;
    QWidget* values = nullptr;
    std::vector<PDFLabel*> labels;
    std::vector<PDFDoubleSpinBox*> fields;

    QString title;                      ///< "Fill", "Stroke" (translated; the accessible names of the values, R7-13)

    PDFAppEditContentMixedValue<PDFAppEditContentPaintColor> color;     ///< Of the leaves with the paint, else of all (the color a switch on uses, R-1)
    PDFAppEditContentMixedValue<QByteArray> colorSpace;                 ///< Their color spaces
    bool isPainted = false;                 ///< Some leaf has the paint
    Qt::CheckState shownState = Qt::Unchecked;  ///< The state of the check box the objects have (updateCard)
};

/// The card Fill and stroke in the device color spaces (stage E1; stage E4, P41, replaces the entry)
class PDFAppEditContentFillAndStrokeCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentFillAndStrokeCard)

public:
    explicit PDFAppEditContentFillAndStrokeCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::FILL_AND_STROKE), parent)
    {
        m_section = addSection(tr("Fill and stroke"));
        QVBoxLayout* layout = nullptr;
        QWidget* content = PDFAppEditContentCardTools::createColumn(m_section, &layout);

        createPaint(&m_fill, tr("Fill"), QStringLiteral("Fill"), content, layout);
        createPaint(&m_stroke, tr("Stroke"), QStringLiteral("Stroke"), content, layout);

        // Width and the dash pattern of a stroke (-path-selected)
        m_widthRow = new QWidget(content);
        QHBoxLayout* widthLayout = new QHBoxLayout(m_widthRow);
        widthLayout->setContentsMargins(0, 0, 0, 0);
        widthLayout->setSpacing(8);
        widthLayout->addWidget(new PDFLabel(tr("Width"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, m_widthRow), 1);
        m_width = PDFAppEditContentAppearanceHelper::createRealField(m_widthRow, 0.0, 1000.0, 2, 0.25, PDFAppEditContentCardTools::getUnitSuffix(context));
        m_width->setObjectName(QStringLiteral("editContentStrokeWidth"));
        m_width->setAccessibleName(tr("Width"));
        m_width->setTrailingZerosHidden(true);
        m_width->setFixedWidth(86);
        m_dash = new PDFComboBox(m_widthRow);
        m_dash->setObjectName(QStringLiteral("editContentDash"));
        m_dash->setAccessibleName(tr("Dash pattern"));
        m_dash->setCompact(true);
        m_dash->addItems({ tr("Solid"), tr("Dashed"), tr("Dotted") });
        m_dash->setFixedWidth(110);
        widthLayout->addWidget(m_width);
        widthLayout->addWidget(m_dash);
        layout->addWidget(m_widthRow);

        m_note = PDFAppEditContentCardTools::createNote(QString(), content);
        m_note->setObjectName(QStringLiteral("editContentFillAndStrokeNote"));
        layout->addWidget(m_note);
        m_section->setContentWidget(content);
        m_content = content;

        // The expanded part of the stroke (Cap, Join, Miter limit): disabled while no object strokes, not hidden
        createExpanded();

        connect(m_width, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentFillAndStrokeCard::onWidthEntered);
        connect(m_dash, &PDFComboBox::activated, this, &PDFAppEditContentFillAndStrokeCard::onDashActivated);
    }

    virtual void updateCard() override
    {
        const PDFAppEditContentContext* context = getContext();
        const PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        if (!container)
        {
            return;
        }

        // The values are read from the inspected nodes (the changeable selection, at most MAXIMUM_INSPECTED_NODES; only
        // locked objects selected: read only), never the list a command acts on (R7-1, R7-4)
        bool isTruncated = false;
        bool isChangeable = false;
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getInspectedNodes(context, &isTruncated, &isChangeable);
        bool isLeavesTruncated = false;
        const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, nodes, &PDFAppEditContentCardTools::isPaintedLeaf, &isLeavesTruncated);
        const PDFContentDisplayContext displayContext = PDFAppEditContentCardTools::getDisplayContext(context);
        const QTransform containerToPage = PDFAppEditContentCardTools::getContainerToDisplayed(context).value_or(QTransform());
        const double factor = PDFAppEditContentCardTools::getUnitFactor(context);

        // The values of the leaves (bounded by MAXIMUM_INSPECTED_NODES); what is beyond the bound makes them mixed. A
        // paint, which no leaf paints, shows the values of all leaves (the color a switch on uses, R-1), disabled.
        PDFAppEditContentPaintControls* paints[] = { &m_fill, &m_stroke };
        PDFAppEditContentMixedValue<PDFAppEditContentPaintColor> allColors[2];
        PDFAppEditContentMixedValue<QByteArray> allSpaces[2];
        int paintedCounts[2] = { 0, 0 };
        for (PDFAppEditContentPaintControls* paint : paints)
        {
            paint->color = PDFAppEditContentMixedValue<PDFAppEditContentPaintColor>();
            paint->colorSpace = PDFAppEditContentMixedValue<QByteArray>();
            paint->isPainted = false;
        }

        // The line: of the stroked leaves, else of all; the width in the space of the page (R7-6, QE.6)
        LineValues stroked;
        LineValues all;
        for (const PDFContentNodePointer& leaf : leaves)
        {
            const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, leaf->getId(), displayContext);
            if (!state)
            {
                continue;
            }

            const bool painted[] = { PDFAppEditContentAppearanceHelper::hasFill(*leaf, *state), PDFAppEditContentAppearanceHelper::hasStroke(*leaf, *state) };
            for (int i = 0; i < 2; ++i)
            {
                const PDFAppEditContentPaintColor color = PDFAppEditContentAppearanceHelper::getPaintColor(*leaf, *state, i == 0);
                allColors[i].add(color);
                allSpaces[i].add(color.space);
                if (painted[i])
                {
                    ++paintedCounts[i];
                    paints[i]->isPainted = true;
                    paints[i]->color.add(color);
                    paints[i]->colorSpace.add(color.space);
                }
            }

            all.add(*state, containerToPage);
            if (painted[1])
            {
                stroked.add(*state, containerToPage);
            }
        }

        if (isTruncated || isLeavesTruncated)
        {
            // Not every object was read: no value is claimed to be the value of all of them
            for (int i = 0; i < 2; ++i)
            {
                paints[i]->color.add(PDFAppEditContentPaintColor{ QByteArray("Other"), { }, QColor() });
                paints[i]->colorSpace.add(QByteArray("Other"));
                allColors[i].add(PDFAppEditContentPaintColor{ QByteArray("Other"), { }, QColor() });
                allSpaces[i].add(QByteArray("Other"));
            }
            stroked.addUnknown();
            all.addUnknown();
        }

        const int leafCount = int(leaves.size());
        for (int i = 0; i < 2; ++i)
        {
            if (!paints[i]->isPainted)
            {
                paints[i]->color = allColors[i];
                paints[i]->colorSpace = allSpaces[i];
            }
            updatePaint(paints[i], paintedCounts[i], leafCount);
        }

        // The rows of the stroke: shown, disabled while no object strokes (a switched-off paint, item 1 of FX3)
        const LineValues& line = m_stroke.isPainted ? stroked : all;
        m_widthRow->setEnabled(m_stroke.isPainted);
        m_width->setDisplaySuffix(PDFAppEditContentCardTools::getUnitSuffix(context));
        if (!m_width->hasFocus())
        {
            m_width->setValue(line.width.getValue().value_or(0.0) / factor);
            m_width->setMixed(line.width.isMixed() || line.isUneven);
        }
        setComboValue(m_dash, line.dash);
        if (m_expanded)
        {
            m_expanded->setEnabled(m_stroke.isPainted);
            setComboValue(m_cap, line.cap);
            setComboValue(m_join, line.join);
            PDFAppEditContentAppearanceHelper::setFieldValue(m_miterLimit, line.miterLimit);
        }

        // A selection of locked objects only is shown read only (R7-4)
        m_content->setEnabled(isChangeable);
        if (m_expanded)
        {
            m_expanded->setEnabled(isChangeable && m_stroke.isPainted);
        }

        // The note of the frame of the state
        const bool isMixed = m_fill.color.isMixed() || m_stroke.color.isMixed() || line.width.isMixed();
        if (!isChangeable)
        {
            m_note->setText(tr("The selected objects are locked. Unlock them to change their fill and stroke."));
        }
        else if (line.isUneven)
        {
            m_note->setText(tr("The matrix of an object scales its line differently in each direction, so its width on the page depends on the direction; a width typed here is set as the average of the two."));
        }
        else if (nodes.size() > 1 && isMixed)
        {
            m_note->setText(tr("Mixed marks a value that differs between the selected objects."));
        }
        else if (m_fill.isPainted && m_stroke.isPainted)
        {
            m_note->setText(tr("Fill and stroke are two paints. Each has its own color space, swatches and values under its check box; the eyedropper of a row sets only that paint."));
        }
        else
        {
            // R7-11: the swatches are the colors of the edited content (KE.5, QE.9), not of the whole document
            m_note->setText(tr("The swatches are the colors used on this page. The values are in the color space of the object; nothing is converted until you choose another space."));
        }
    }

    virtual void commitPendingEdit() override
    {
        // A value typed and not confirmed becomes its own command (valueEntered of the field)
        const std::vector<QPointer<PDFDoubleSpinBox>> fields = getFields();
        for (const QPointer<PDFDoubleSpinBox>& field : fields)
        {
            if (field && !PDFDeferredDeletion::isRetired(this))
            {
                field->commitEntry();
            }
        }
    }

    virtual void discardPendingEdit() override
    {
        // The typed text is forgotten: the fields show the values of the objects again (the program's setValue)
        for (PDFDoubleSpinBox* field : getFields())
        {
            if (field)
            {
                field->setValue(field->value());
            }
        }
    }

private:
    /// The values of the line of the leaves (the width in points of the page, R7-6)
    struct LineValues
    {
        PDFAppEditContentMixedValue<double> width;
        PDFAppEditContentMixedValue<int> dash;
        PDFAppEditContentMixedValue<int> cap;
        PDFAppEditContentMixedValue<int> join;
        PDFAppEditContentMixedValue<double> miterLimit;
        bool isUneven = false;      ///< The matrix of a leaf scales the line differently by the direction (QE.6: Mixed and a note)

        void add(const PDFContentDisplayState& state, const QTransform& containerToPage)
        {
            bool isUniform = true;
            const double scale = PDFAppEditContentAppearanceHelper::getLineScale(state.matrix * containerToPage, &isUniform);
            isUneven = isUneven || !isUniform;
            width.add(state.lineWidth * scale, &LineValues::isSameWidth);
            dash.add(PDFAppEditContentAppearanceHelper::getDashIndex(state.dashPattern));
            cap.add(PDFAppEditContentAppearanceHelper::getCapIndex(state.lineCap));
            join.add(PDFAppEditContentAppearanceHelper::getJoinIndex(state.lineJoin));
            miterLimit.add(state.miterLimit);
        }

        void addUnknown()
        {
            width.add(-1.0);
            dash.add(-1);
            cap.add(-1);
            join.add(-1);
            miterLimit.add(-1.0);
        }

        /// Two widths of the page are the same up to the rounding of the matrices
        static bool isSameWidth(const double& left, const double& right)
        {
            return std::abs(left - right) <= 1e-9 * std::max({ 1.0, std::abs(left), std::abs(right) });
        }
    };

    /// The expanded part of a stroke: Cap, Join, Miter limit (-path-selected)
    void createExpanded()
    {
        QVBoxLayout* expandedLayout = nullptr;
        m_expanded = PDFAppEditContentCardTools::createColumn(m_section, &expandedLayout);
        m_cap = new PDFComboBox(m_expanded);
        m_cap->setObjectName(QStringLiteral("editContentCap"));
        m_cap->setCompact(true);
        m_cap->addItems({ tr("Butt"), tr("Round"), tr("Square") });
        m_join = new PDFComboBox(m_expanded);
        m_join->setObjectName(QStringLiteral("editContentJoin"));
        m_join->setCompact(true);
        m_join->addItems({ tr("Miter"), tr("Round"), tr("Bevel") });
        m_miterLimit = PDFAppEditContentAppearanceHelper::createRealField(m_expanded, 1.0, 1000.0, 2, 1.0, QString());
        m_miterLimit->setObjectName(QStringLiteral("editContentMiterLimit"));
        m_miterLimit->setTrailingZerosHidden(true);
        expandedLayout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Cap"), m_cap, m_expanded, 140));
        expandedLayout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Join"), m_join, m_expanded, 140));
        expandedLayout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Miter limit"), m_miterLimit, m_expanded));
        m_section->setExpandedWidget(m_expanded);

        connect(m_cap, &PDFComboBox::activated, this, &PDFAppEditContentFillAndStrokeCard::onCapActivated);
        connect(m_join, &PDFComboBox::activated, this, &PDFAppEditContentFillAndStrokeCard::onJoinActivated);
        connect(m_miterLimit, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentFillAndStrokeCard::onMiterLimitEntered);
    }

    /// The numeric fields of the card
    std::vector<QPointer<PDFDoubleSpinBox>> getFields() const
    {
        std::vector<QPointer<PDFDoubleSpinBox>> fields;
        for (const PDFAppEditContentPaintControls* paint : { &m_fill, &m_stroke })
        {
            for (PDFDoubleSpinBox* field : paint->fields)
            {
                fields.emplace_back(field);
            }
        }
        fields.emplace_back(m_width);
        fields.emplace_back(m_miterLimit);
        return fields;
    }

    void createPaint(PDFAppEditContentPaintControls* paint, const QString& title, const QString& name, QWidget* parent, QVBoxLayout* layout)
    {
        // The row: the check box (the state of the paint) and the color space
        QWidget* row = new QWidget(parent);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        paint->check = new QCheckBox(title, row);
        paint->check->setObjectName(QStringLiteral("editContent%1Check").arg(name));
        paint->check->setTristate(true);
        paint->title = title;

        // The check box switches the painting of the objects on or off (set_appearance with fill / stroke): a click
        // sets the paint of all of them, the partly checked state is only shown (a mixed selection)
        connect(paint->check, &QCheckBox::clicked, this, &PDFAppEditContentFillAndStrokeCard::onCheckClicked);

        paint->space = new PDFComboBox(row);
        paint->space->setObjectName(QStringLiteral("editContent%1Space").arg(name));
        paint->space->setAccessibleName(tr("%1 color space").arg(title));
        paint->space->setFixedWidth(150);
        paint->space->addItem(QStringLiteral("DeviceGray"), QByteArray("DeviceGray"));
        paint->space->addItem(QStringLiteral("DeviceRGB"), QByteArray("DeviceRGB"));
        paint->space->addItem(QStringLiteral("DeviceCMYK"), QByteArray("DeviceCMYK"));
        rowLayout->addWidget(paint->check, 1);
        rowLayout->addWidget(paint->space);
        layout->addWidget(row);

        // The swatches: the colors used in the document (QE.9), seven at most
        paint->swatchRow = new QWidget(parent);
        paint->swatchRow->setObjectName(QStringLiteral("editContent%1Swatches").arg(name));
        QHBoxLayout* swatchLayout = new QHBoxLayout(paint->swatchRow);
        swatchLayout->setContentsMargins(0, 0, 0, 0);
        swatchLayout->setSpacing(4);
        paint->swatchGroup = new QButtonGroup(this);
        paint->swatchGroup->setExclusive(true);
        for (int i = 0; i < PDFAppEditContentAppearanceHelper::SWATCH_COUNT; ++i)
        {
            PDFColorSwatch* swatch = new PDFColorSwatch(paint->swatchRow);
            swatch->setObjectName(QStringLiteral("editContent%1Swatch%2").arg(name).arg(i));
            swatch->setCheckable(true);
            paint->swatchGroup->addButton(swatch, i);
            swatchLayout->addWidget(swatch);
            paint->swatches.push_back(swatch);
            connect(swatch, &PDFColorSwatch::clicked, this, &PDFAppEditContentFillAndStrokeCard::onSwatchClicked);
        }
        swatchLayout->addStretch(1);
        layout->addWidget(paint->swatchRow);

        // The values: up to four fields of the components, two per row (-color-cmyk)
        paint->values = new QWidget(parent);
        QGridLayout* grid = new QGridLayout(paint->values);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(6);
        for (int i = 0; i < 4; ++i)
        {
            PDFLabel* label = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextSecondary, paint->values);
            PDFDoubleSpinBox* field = PDFAppEditContentAppearanceHelper::createRealField(paint->values, 0.0, 1.0, 2, 0.01, QString());
            field->setObjectName(QStringLiteral("editContent%1Value%2").arg(name).arg(i));
            label->setBuddy(field);
            grid->addWidget(label, i / 2, (i % 2) * 2);
            grid->addWidget(field, i / 2, (i % 2) * 2 + 1);
            paint->labels.push_back(label);
            paint->fields.push_back(field);
            connect(field, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentFillAndStrokeCard::onComponentEntered);
        }
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        layout->addWidget(paint->values);

        connect(paint->space, &PDFComboBox::activated, this, &PDFAppEditContentFillAndStrokeCard::onSpaceActivated);
    }

    void updatePaint(PDFAppEditContentPaintControls* paint, int paintedCount, int leafCount)
    {
        // Tristate: some of the objects paint it (item 1 of FX3); the fields of a paint, which no object paints, are
        // disabled, not hidden
        paint->shownState = paintedCount == 0 ? Qt::Unchecked : (paintedCount == leafCount ? Qt::Checked : Qt::PartiallyChecked);
        paint->check->setCheckState(paint->shownState);
        paint->space->setEnabled(paint->isPainted);
        paint->swatchRow->setEnabled(paint->isPainted);
        paint->values->setEnabled(paint->isPainted);

        // The space: a device space is an item; another space is shown as an item of its own (it can only be
        // replaced by a device space in stage E1)
        const PDFAppEditContentMixedValue<PDFAppEditContentPaintColor>& color = paint->color;
        while (paint->space->count() > 3)
        {
            paint->space->removeItem(3);
        }

        const bool isSpaceMixed = paint->colorSpace.isMixed();
        const QByteArray space = paint->colorSpace.getValue().value_or(QByteArray());

        if (!space.isEmpty() && paint->space->findData(space) < 0)
        {
            paint->space->addItem(QString::fromLatin1(space), space);
        }
        paint->space->setCurrentIndex(space.isEmpty() ? 0 : paint->space->findData(space));
        paint->space->setMixed(isSpaceMixed);

        // The swatches (none selected when the colors differ)
        updateSwatches(paint);

        // The values: one color of a device space
        const bool hasValues = !color.isMixed() && color.getValue() && color.getValue()->isDevice();
        paint->values->setVisible(hasValues);
        if (hasValues)
        {
            const QStringList labels = PDFAppEditContentAppearanceHelper::getComponentLabels(color.getValue()->space);
            const std::vector<PDFReal>& components = color.getValue()->components;
            for (int i = 0; i < 4; ++i)
            {
                const bool isShown = i < labels.size();
                paint->labels[size_t(i)]->setVisible(isShown);
                paint->fields[size_t(i)]->setVisible(isShown);
                if (isShown)
                {
                    // R7-13: the accessible name says which paint ("Fill R", "Stroke C")
                    paint->labels[size_t(i)]->setText(labels[i]);
                    paint->fields[size_t(i)]->setAccessibleName(tr("%1 %2", "the paint and the component").arg(paint->title, labels[i]));
                    if (!paint->fields[size_t(i)]->hasFocus())
                    {
                        paint->fields[size_t(i)]->setValue(size_t(i) < components.size() ? components[size_t(i)] : 0.0);
                        paint->fields[size_t(i)]->setMixed(false);
                    }
                }
            }
        }
    }

    void updateSwatches(PDFAppEditContentPaintControls* paint)
    {
        const std::vector<PDFAppEditContentPaintColor>& colors = getDocumentColors();
        const PDFAppEditContentMixedValue<PDFAppEditContentPaintColor>& color = paint->color;
        paint->swatchGroup->setExclusive(false);
        for (int i = 0; i < PDFAppEditContentAppearanceHelper::SWATCH_COUNT; ++i)
        {
            PDFColorSwatch* swatch = paint->swatches[size_t(i)];
            const bool isShown = size_t(i) < colors.size();
            swatch->setVisible(isShown);
            if (!isShown)
            {
                swatch->setSelected(false);
                continue;
            }

            const PDFAppEditContentPaintColor& swatchColor = colors[size_t(i)];
            swatch->setColor(swatchColor.color);
            swatch->setColorName(QStringLiteral("%1 %2").arg(QString::fromLatin1(swatchColor.space), swatchColor.color.name()));
            swatch->setSelected(!color.isMixed() && color.getValue() && *color.getValue() == swatchColor);
        }
        paint->swatchGroup->setExclusive(true);
    }

    /// The colors of the device spaces of the edited container (QE.9: the colors of the content open in the editor),
    /// in the order of their first use, at most SWATCH_COUNT; computed again for a new version of the container
    const std::vector<PDFAppEditContentPaintColor>& getDocumentColors()
    {
        const PDFAppEditContentContext* context = getContext();
        const PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        // Keyed by the container object, not by its key and version: a session loaded again (reloadDocument) has the
        // same keys and versions for other content
        if (!container || m_colorsContainer.lock() == container)
        {
            return m_colors;
        }

        m_colors.clear();
        m_colorsContainer = container;

        // A walk over the whole container: one visit per node, MAXIMUM_INSPECTED_NODES visits at most
        PDFAppEditContentCardTools::SelectedNode root{ PDFContentNodeReference{ container->getKey(), container->getRoot()->getId() }, container->getRoot() };
        bool isTruncated = false;
        const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, { root }, &PDFAppEditContentCardTools::isPaintedLeaf, &isTruncated);
        const PDFContentDisplayContext displayContext = PDFAppEditContentCardTools::getDisplayContext(context);
        for (const PDFContentNodePointer& leaf : leaves)
        {
            if (m_colors.size() >= size_t(PDFAppEditContentAppearanceHelper::SWATCH_COUNT))
            {
                break;
            }

            const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, leaf->getId(), displayContext);
            if (!state)
            {
                continue;
            }

            for (const bool isFill : { true, false })
            {
                const bool isPainted = isFill ? PDFAppEditContentAppearanceHelper::hasFill(*leaf, *state) : PDFAppEditContentAppearanceHelper::hasStroke(*leaf, *state);
                if (!isPainted)
                {
                    continue;
                }

                const PDFAppEditContentPaintColor color = PDFAppEditContentAppearanceHelper::getPaintColor(*leaf, *state, isFill);
                if (color.isDevice() && color.color.isValid() && m_colors.size() < size_t(PDFAppEditContentAppearanceHelper::SWATCH_COUNT) &&
                    std::find(m_colors.cbegin(), m_colors.cend(), color) == m_colors.cend())
                {
                    m_colors.push_back(color);
                }
            }
        }
        return m_colors;
    }

    PDFAppEditContentPaintControls* getPaintOf(const QObject* object)
    {
        for (PDFAppEditContentPaintControls* paint : { &m_fill, &m_stroke })
        {
            if (object == paint->space)
            {
                return paint;
            }
            for (PDFColorSwatch* swatch : paint->swatches)
            {
                if (object == swatch)
                {
                    return paint;
                }
            }
            for (PDFDoubleSpinBox* field : paint->fields)
            {
                if (object == field)
                {
                    return paint;
                }
            }
        }
        return nullptr;
    }

    PDFContentStateProperty getProperty(const PDFAppEditContentPaintControls* paint) const
    {
        return paint == &m_fill ? PDFContentStateProperty::FillColor : PDFContentStateProperty::StrokeColor;
    }

    /// Executes the change of the properties over the selected nodes (one undo step; the steps of a dragged value
    /// merge in the core, PDFContentCommand::mergeWith)
    void setProperties(std::map<PDFContentStateProperty, PDFContentStateValue> properties)
    {
        // The whole changeable selection (R7-1, R7-4), read when the user acts
        PDFAppEditContentContext* context = getContext();
        const std::vector<PDFContentNodeReference> nodes = PDFAppEditContentCardTools::getReferences(PDFAppEditContentCardTools::getCommandNodes(context));
        if (!context || nodes.empty())
        {
            return;
        }
        context->execute(PDFContentCommandFactory::createSetAppearance(nodes, std::move(properties)));
    }

    /// One property, a value per group of the objects
    using Groups = std::vector<std::pair<PDFContentStateValue, std::vector<PDFContentNodeReference>>>;

    /// The commands of the groups of the objects (one value of \p property per group) as one undo step
    void executeGroups(PDFContentStateProperty property, const Groups& groups)
    {
        PDFAppEditContentContext* context = getContext();
        if (!context || groups.empty())
        {
            return;
        }

        QPointer<PDFAppEditContentContext> guard(context);
        const bool isMacro = groups.size() > 1;
        if (isMacro)
        {
            context->beginMacro(tr("Change Appearance"));
        }
        for (const auto& group : groups)
        {
            if (!guard || PDFDeferredDeletion::isRetired(guard))
            {
                break;
            }
            std::map<PDFContentStateProperty, PDFContentStateValue> properties;
            properties[property] = group.first;
            guard->execute(PDFContentCommandFactory::createSetAppearance(group.second, std::move(properties)));
        }
        if (isMacro && guard)
        {
            guard->endMacro();
        }
    }

    /// The color of the paint of an object of the core (a path, or a text object as a whole: no value when its shows
    /// differ in it, \p isDifferent), which paints it; no value for an object, which does not paint it
    std::optional<PDFAppEditContentPaintColor> getObjectColor(const PDFContentContainer& container, const PDFAppEditContentCardTools::SelectedNode& object, bool isFill,
                                                              const PDFContentDisplayContext& displayContext, bool* isDifferent) const
    {
        *isDifferent = false;
        bool isTruncated = false;
        const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(container, { object }, &PDFAppEditContentCardTools::isPaintedLeaf, &isTruncated,
                                                                                                std::numeric_limits<size_t>::max());
        PDFAppEditContentMixedValue<PDFAppEditContentPaintColor> color;
        for (const PDFContentNodePointer& leaf : leaves)
        {
            const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(container, leaf->getId(), displayContext);
            if (state && (isFill ? PDFAppEditContentAppearanceHelper::hasFill(*leaf, *state) : PDFAppEditContentAppearanceHelper::hasStroke(*leaf, *state)))
            {
                color.add(PDFAppEditContentAppearanceHelper::getPaintColor(*leaf, *state, isFill));
            }
        }
        if (color.isMixed())
        {
            *isDifferent = true;
            return std::nullopt;
        }
        return color.getValue();
    }

    void onCheckClicked()
    {
        // A click switches the paint on for all objects, unless all of them paint it already (then off); the check box
        // shows the state of the objects again when the command is refused (the notice of the context tells why: a
        // text of two modes, a clipping text)
        PDFAppEditContentPaintControls* paint = nullptr;
        for (PDFAppEditContentPaintControls* candidate : { &m_fill, &m_stroke })
        {
            paint = candidate->check == sender() ? candidate : paint;
        }
        PDFAppEditContentContext* context = getContext();
        const std::vector<PDFContentNodeReference> nodes = PDFAppEditContentCardTools::getReferences(PDFAppEditContentCardTools::getCommandNodes(context));
        if (!paint || !context)
        {
            return;
        }

        QPointer<QCheckBox> check = paint->check;
        const Qt::CheckState shownState = paint->shownState;
        const bool isOn = shownState != Qt::Checked;
        check->setCheckState(isOn ? Qt::Checked : Qt::Unchecked);

        PDFContentPaintSwitches switches;
        (paint == &m_fill ? switches.fill : switches.stroke) = isOn;
        const bool isApplied = !nodes.empty() && context->execute(PDFContentCommandFactory::createSetAppearance(nodes, { }, switches));
        if (!isApplied && check && !PDFDeferredDeletion::isRetired(this))
        {
            check->setCheckState(shownState);
        }
    }

    void onSwatchClicked()
    {
        PDFAppEditContentPaintControls* paint = getPaintOf(sender());
        const int index = paint ? paint->swatchGroup->id(qobject_cast<QAbstractButton*>(sender())) : -1;
        const std::vector<PDFAppEditContentPaintColor>& colors = m_colors;
        if (!paint || index < 0 || size_t(index) >= colors.size())
        {
            return;
        }

        const PDFAppEditContentPaintColor color = colors[size_t(index)];
        setProperties({ { getProperty(paint), PDFAppEditContentAppearanceHelper::createColorValue(color.space, color.components) } });
    }

    void onComponentEntered(double value)
    {
        PDFAppEditContentPaintControls* paint = getPaintOf(sender());
        if (!paint || paint->color.isMixed() || !paint->color.getValue() || !paint->color.getValue()->isDevice())
        {
            return;
        }

        const auto it = std::find(paint->fields.cbegin(), paint->fields.cend(), sender());
        const size_t index = size_t(it - paint->fields.cbegin());
        PDFAppEditContentPaintColor color = *paint->color.getValue();
        if (index >= color.components.size())
        {
            return;
        }

        color.components[index] = value;
        setProperties({ { getProperty(paint), PDFAppEditContentAppearanceHelper::createColorValue(color.space, color.components) } });
    }

    void onSpaceActivated(int index)
    {
        // Another device space (R7-2): each object of the core (a path, a text object as a whole) keeps its own color,
        // converted into the space (ISO 32000-2, 10.4.2, from its components); an object already in the space is left
        // as it is (choosing the space shown changes nothing); objects of the same converted color are one command, all
        // of them one undo step. A text of shows of different colors cannot be restyled part by part: it keeps its
        // colors (a notice says so); a pattern is not a color to convert (stage E4, SetPatternFill).
        PDFAppEditContentPaintControls* paint = getPaintOf(sender());
        PDFAppEditContentContext* context = getContext();
        const PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        if (!paint || !container || index < 0)
        {
            return;
        }

        const QByteArray space = paint->space->itemData(index).toByteArray();
        if (space != "DeviceGray" && space != "DeviceRGB" && space != "DeviceCMYK")
        {
            return;
        }

        const bool isFill = paint == &m_fill;
        const PDFContentDisplayContext displayContext = PDFAppEditContentCardTools::getDisplayContext(context);
        std::map<std::vector<PDFReal>, size_t> groupIndices;
        Groups groups;
        int keptTexts = 0;
        for (const PDFAppEditContentCardTools::SelectedNode& object : PDFAppEditContentCardTools::getAppearanceObjects(*container, PDFAppEditContentCardTools::getCommandNodes(context)))
        {
            bool isDifferent = false;
            const std::optional<PDFAppEditContentPaintColor> color = getObjectColor(*container, object, isFill, displayContext, &isDifferent);
            keptTexts += isDifferent ? 1 : 0;
            if (!color || color->space == space || color->space == "Pattern")
            {
                continue;
            }

            const std::vector<PDFReal> components = PDFAppEditContentAppearanceHelper::convert(*color, space);
            auto it = groupIndices.find(components);
            if (it == groupIndices.end())
            {
                it = groupIndices.emplace(components, groups.size()).first;
                groups.emplace_back(PDFAppEditContentAppearanceHelper::createColorValue(space, components), std::vector<PDFContentNodeReference>());
            }
            groups[it->second].second.push_back(object.reference);
        }

        QPointer<PDFAppEditContentContext> guard(context);
        executeGroups(getProperty(paint), groups);

        if (keptTexts > 0 && guard && !PDFDeferredDeletion::isRetired(guard))
        {
            PDFAppEditContentNoticeRequest notice;
            notice.id = QStringLiteral("convert-color");
            notice.kind = PDFAppEditContentNoticeRequest::Kind::Event;
            notice.severity = PDFAppEditContentNoticeRequest::Severity::Warning;
            notice.text = tr("%Ln text(s) of several colors kept their colors: a part of a text cannot be restyled yet.", nullptr, keptTexts);
            guard->showNotice(notice);
        }
    }

    void onWidthEntered(double value)
    {
        // The width is typed in the units of the page (R7-6, QE.6): each object gets the width in its own space, the
        // typed width divided by the scale of its matrix to the page. Objects of one scale are one command - the whole
        // changeable selection, when all of them have it; all of them one undo step.
        PDFAppEditContentContext* context = getContext();
        const PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        const std::optional<QTransform> containerToPage = PDFAppEditContentCardTools::getContainerToDisplayed(context);
        if (!container || !containerToPage)
        {
            return;
        }

        const double width = value * PDFAppEditContentCardTools::getUnitFactor(context);
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getCommandNodes(context);
        const PDFContentDisplayContext displayContext = PDFAppEditContentCardTools::getDisplayContext(context);
        std::map<double, size_t> groupIndices;
        Groups groups;
        for (const PDFAppEditContentCardTools::SelectedNode& object : PDFAppEditContentCardTools::getAppearanceObjects(*container, nodes))
        {
            const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, object.node->getId(), displayContext);
            const double scale = state ? PDFAppEditContentAppearanceHelper::getLineScale(state->matrix * *containerToPage, nullptr) : 0.0;
            if (!(scale > 1e-12) || !std::isfinite(scale))
            {
                continue;
            }

            const double nodeWidth = width / scale;
            auto it = groupIndices.find(nodeWidth);
            if (it == groupIndices.end())
            {
                it = groupIndices.emplace(nodeWidth, groups.size()).first;
                groups.emplace_back(PDFAppEditContentAppearanceHelper::createRealValue(nodeWidth), std::vector<PDFContentNodeReference>());
            }
            groups[it->second].second.push_back(object.reference);
        }

        if (groups.size() == 1)
        {
            // One scale: the command over the selected nodes (as every other field; the steps of the arrows merge)
            setProperties({ { PDFContentStateProperty::LineWidth, groups.front().first } });
            return;
        }
        executeGroups(PDFContentStateProperty::LineWidth, groups);
    }

    void onMiterLimitEntered(double value)
    {
        setProperties({ { PDFContentStateProperty::MiterLimit, PDFAppEditContentAppearanceHelper::createRealValue(value) } });
    }

    void onDashActivated(int index)
    {
        if (index < 0)
        {
            return;
        }
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Dash;
        value.dash = PDFAppEditContentAppearanceHelper::getDashPattern(index);
        setProperties({ { PDFContentStateProperty::DashPattern, value } });
    }

    void onCapActivated(int index)
    {
        if (index >= 0)
        {
            setProperties({ { PDFContentStateProperty::LineCap, PDFAppEditContentAppearanceHelper::createIntegerValue(index) } });
        }
    }

    void onJoinActivated(int index)
    {
        if (index >= 0)
        {
            setProperties({ { PDFContentStateProperty::LineJoin, PDFAppEditContentAppearanceHelper::createIntegerValue(index) } });
        }
    }

    static void setComboValue(PDFComboBox* combo, const PDFAppEditContentMixedValue<int>& value)
    {
        combo->setMixed(false);
        combo->setCurrentIndex(std::max(value.getValue().value_or(0), 0));
        combo->setMixed(value.isMixed() || value.getValue().value_or(0) < 0);
    }

    PDFExpanderCard* m_section = nullptr;
    PDFAppEditContentPaintControls m_fill;
    PDFAppEditContentPaintControls m_stroke;
    QWidget* m_widthRow = nullptr;
    PDFDoubleSpinBox* m_width = nullptr;
    PDFComboBox* m_dash = nullptr;
    PDFLabel* m_note = nullptr;
    QWidget* m_content = nullptr;
    QPointer<QWidget> m_expanded;               ///< The expanded part of the section (created once)
    QPointer<PDFComboBox> m_cap;
    QPointer<PDFComboBox> m_join;
    QPointer<PDFDoubleSpinBox> m_miterLimit;
    std::vector<PDFAppEditContentPaintColor> m_colors;
    std::weak_ptr<const PDFContentContainer> m_colorsContainer;     ///< The container version of m_colors (a reloaded session has other objects)
};

/// The card Transparency (-transparency): the opacity of the fill and of the stroke, the blend mode
class PDFAppEditContentTransparencyCard : public PDFAppEditContentCard
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentTransparencyCard)

public:
    explicit PDFAppEditContentTransparencyCard(PDFAppEditContentContext* context, QWidget* parent) :
        PDFAppEditContentCard(context, QLatin1String(PDFAppEditContentCardIds::TRANSPARENCY), parent)
    {
        m_section = addSection(tr("Transparency"));
        QVBoxLayout* layout = nullptr;
        QWidget* expanded = PDFAppEditContentCardTools::createColumn(m_section, &layout);
        m_fillOpacity = PDFAppEditContentAppearanceHelper::createRealField(expanded, 0.0, 100.0, 0, 1.0, tr("%"));
        m_fillOpacity->setObjectName(QStringLiteral("editContentFillOpacity"));
        m_strokeOpacity = PDFAppEditContentAppearanceHelper::createRealField(expanded, 0.0, 100.0, 0, 1.0, tr("%"));
        m_strokeOpacity->setObjectName(QStringLiteral("editContentStrokeOpacity"));
        m_blendMode = new PDFComboBox(expanded);
        m_blendMode->setObjectName(QStringLiteral("editContentBlendMode"));
        m_blendMode->setCompact(true);
        for (int i = 0; i < int(PDFAppEditContentAppearanceHelper::BLEND_MODE_NAMES.size()); ++i)
        {
            m_blendMode->addItem(PDFAppEditContentAppearanceHelper::getBlendModeTitle(i), QByteArray(PDFAppEditContentAppearanceHelper::BLEND_MODE_NAMES[size_t(i)]));
        }
        layout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Fill opacity"), m_fillOpacity, expanded));
        layout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Stroke opacity"), m_strokeOpacity, expanded));
        layout->addWidget(PDFAppEditContentCardTools::createFieldRow(tr("Blend mode"), m_blendMode, expanded, 150));
        layout->addWidget(PDFAppEditContentCardTools::createNote(tr("Opacity and the common blend modes are drawn while editing. Hue, Saturation, Color and Luminosity are exact only in Preview."), expanded));
        m_softMask = PDFAppEditContentCardTools::createNote(tr("A soft mask is in effect; it is drawn exactly only in Preview."), expanded);
        m_softMask->setObjectName(QStringLiteral("editContentSoftMask"));
        layout->addWidget(m_softMask);
        m_section->setExpandedWidget(expanded);
        m_content = expanded;

        connect(m_fillOpacity, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentTransparencyCard::onFillOpacityEntered);
        connect(m_strokeOpacity, &PDFDoubleSpinBox::valueEntered, this, &PDFAppEditContentTransparencyCard::onStrokeOpacityEntered);
        connect(m_blendMode, &PDFComboBox::activated, this, &PDFAppEditContentTransparencyCard::onBlendModeActivated);
    }

    virtual void updateCard() override
    {
        const PDFAppEditContentContext* context = getContext();
        const PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        if (!container)
        {
            return;
        }

        // The values of the inspected nodes (bounded); the command takes the whole changeable selection (R7-1, R7-4)
        bool isTruncated = false;
        bool isChangeable = false;
        const std::vector<PDFAppEditContentCardTools::SelectedNode> nodes = PDFAppEditContentCardTools::getInspectedNodes(context, &isTruncated, &isChangeable);
        m_content->setEnabled(isChangeable);
        bool isLeavesTruncated = false;
        const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, nodes, &PDFAppEditContentCardTools::isDrawingLeaf, &isLeavesTruncated);
        const PDFContentDisplayContext displayContext = PDFAppEditContentCardTools::getDisplayContext(context);

        PDFAppEditContentMixedValue<double> fillOpacity;
        PDFAppEditContentMixedValue<double> strokeOpacity;
        PDFAppEditContentMixedValue<int> blendMode;
        bool hasSoftMask = false;
        for (const PDFContentNodePointer& leaf : leaves)
        {
            const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, leaf->getId(), displayContext);
            if (state)
            {
                fillOpacity.add(state->alphaFilling);
                strokeOpacity.add(state->alphaStroking);
                blendMode.add(PDFAppEditContentAppearanceHelper::getBlendModeIndex(state->blendMode));
                hasSoftMask = hasSoftMask || state->hasSoftMask;
            }
        }
        if (isTruncated || isLeavesTruncated)
        {
            fillOpacity.add(-1.0);
            strokeOpacity.add(-1.0);
            blendMode.add(-1);
        }

        PDFAppEditContentAppearanceHelper::setFieldValue(m_fillOpacity, fillOpacity, 100.0);
        PDFAppEditContentAppearanceHelper::setFieldValue(m_strokeOpacity, strokeOpacity, 100.0);
        m_blendMode->setMixed(false);
        m_blendMode->setCurrentIndex(std::max(blendMode.getValue().value_or(0), 0));
        m_blendMode->setMixed(blendMode.isMixed() || blendMode.getValue().value_or(0) < 0);
        m_softMask->setVisible(hasSoftMask);

        // The summary of the collapsed card (edit-content-v2 1647:21850: "Opacity 100 % · blend mode Normal")
        const QString mixed = tr("Mixed");
        const QString opacity = fillOpacity.isMixed() ? mixed : tr("%1 %").arg(PDFAppEditContentCardTools::formatReal(fillOpacity.getValue().value_or(1.0) * 100.0, 0));
        const QString blend = blendMode.isMixed() || blendMode.getValue().value_or(0) < 0 ? mixed : PDFAppEditContentAppearanceHelper::getBlendModeTitle(blendMode.getValue().value_or(0));
        m_section->setSummaryText(tr("Opacity %1 · blend mode %2").arg(opacity, blend));
    }

    virtual void commitPendingEdit() override
    {
        QPointer<PDFDoubleSpinBox> strokeOpacity = m_strokeOpacity;
        m_fillOpacity->commitEntry();
        if (strokeOpacity && !PDFDeferredDeletion::isRetired(this))
        {
            strokeOpacity->commitEntry();
        }
    }

    virtual void discardPendingEdit() override
    {
        m_fillOpacity->setValue(m_fillOpacity->value());
        m_strokeOpacity->setValue(m_strokeOpacity->value());
    }

private:
    void setTransparency(std::optional<PDFReal> alphaStroking, std::optional<PDFReal> alphaFilling, std::optional<QByteArray> blendMode)
    {
        PDFAppEditContentContext* context = getContext();
        const std::vector<PDFContentNodeReference> nodes = PDFAppEditContentCardTools::getReferences(PDFAppEditContentCardTools::getCommandNodes(context));
        if (context && !nodes.empty())
        {
            context->execute(PDFContentCommandFactory::createSetTransparency(nodes, alphaStroking, alphaFilling, blendMode));
        }
    }

    void onFillOpacityEntered(double value)
    {
        setTransparency(std::nullopt, value / 100.0, std::nullopt);
    }

    void onStrokeOpacityEntered(double value)
    {
        setTransparency(value / 100.0, std::nullopt, std::nullopt);
    }

    void onBlendModeActivated(int index)
    {
        if (index >= 0)
        {
            setTransparency(std::nullopt, std::nullopt, m_blendMode->itemData(index).toByteArray());
        }
    }

    PDFExpanderCard* m_section = nullptr;
    PDFDoubleSpinBox* m_fillOpacity = nullptr;
    PDFDoubleSpinBox* m_strokeOpacity = nullptr;
    PDFComboBox* m_blendMode = nullptr;
    PDFLabel* m_softMask = nullptr;
    QWidget* m_content = nullptr;
};

/// The entry of the card Fill and stroke: a selected object paints a fill or a stroke
class PDFAppEditContentFillAndStrokeCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::FILL_AND_STROKE); }

    virtual bool isShown(const PDFAppEditContentContext* context) const override
    {
        return PDFAppEditContentCardTools::isSelectionStateShown(context) && !context->getSelection()->isEmpty() && PDFAppEditContentAppearanceHelper::hasPaintedLeaf(context);
    }

    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentFillAndStrokeCard(context, parent); }
};

/// The entry of the card Transparency: an object is selected
class PDFAppEditContentTransparencyCardEntry : public PDFAppEditContentCardEntry
{
public:
    virtual QString getId() const override { return QLatin1String(PDFAppEditContentCardIds::TRANSPARENCY); }

    virtual bool isShown(const PDFAppEditContentContext* context) const override
    {
        return PDFAppEditContentCardTools::isSelectionStateShown(context) && !context->getSelection()->isEmpty();
    }

    virtual PDFAppEditContentCard* createCard(PDFAppEditContentContext* context, QWidget* parent) const override { return new PDFAppEditContentTransparencyCard(context, parent); }
};

void PDFAppEditContentAppearanceCards::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addCard(std::make_unique<PDFAppEditContentFillAndStrokeCardEntry>());
    registry->addCard(std::make_unique<PDFAppEditContentTransparencyCardEntry>());
}

}   // namespace pdf
