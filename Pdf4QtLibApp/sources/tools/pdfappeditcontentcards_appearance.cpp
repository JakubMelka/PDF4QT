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
#include <map>

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
        const PDFReal r = color.redF();
        const PDFReal g = color.greenF();
        const PDFReal b = color.blueF();
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

    /// The references of the selected nodes
    static std::vector<PDFContentNodeReference> getReferences(const std::vector<PDFAppEditContentCardTools::SelectedNode>& nodes)
    {
        std::vector<PDFContentNodeReference> references;
        references.reserve(nodes.size());
        for (const PDFAppEditContentCardTools::SelectedNode& node : nodes)
        {
            references.push_back(node.reference);
        }
        return references;
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

    PDFAppEditContentMixedValue<PDFAppEditContentPaintColor> color;     ///< Of the leaves with the paint (updateCard)
    PDFAppEditContentMixedValue<QByteArray> colorSpace;                 ///< Their color spaces
    bool isPainted = false;     ///< Some leaf has the paint
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
        m_width = PDFAppEditContentAppearanceHelper::createRealField(m_widthRow, 0.0, 1000.0, 2, 0.25, PDFAppEditContentCardTools::getUnitSuffix(nullptr));
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

        bool isTruncated = false;
        m_nodes = PDFAppEditContentCardTools::getSelectedNodes(context, &isTruncated);
        bool isLeavesTruncated = false;
        const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, m_nodes, &PDFAppEditContentCardTools::isPaintedLeaf, &isLeavesTruncated);
        const PDFContentDisplayContext displayContext = PDFAppEditContentCardTools::getDisplayContext(context);

        // The values of the leaves (bounded by MAXIMUM_INSPECTED_NODES); what is beyond the bound makes them mixed
        m_fill.color = PDFAppEditContentMixedValue<PDFAppEditContentPaintColor>();
        m_stroke.color = PDFAppEditContentMixedValue<PDFAppEditContentPaintColor>();
        m_fill.colorSpace = PDFAppEditContentMixedValue<QByteArray>();
        m_stroke.colorSpace = PDFAppEditContentMixedValue<QByteArray>();
        m_fill.isPainted = false;
        m_stroke.isPainted = false;
        int fillCount = 0;
        int strokeCount = 0;
        PDFAppEditContentMixedValue<double> width;
        PDFAppEditContentMixedValue<int> dash;
        PDFAppEditContentMixedValue<int> cap;
        PDFAppEditContentMixedValue<int> join;
        PDFAppEditContentMixedValue<double> miterLimit;

        for (const PDFContentNodePointer& leaf : leaves)
        {
            const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, leaf->getId(), displayContext);
            if (!state)
            {
                continue;
            }

            if (PDFAppEditContentAppearanceHelper::hasFill(*leaf, *state))
            {
                ++fillCount;
                m_fill.isPainted = true;
                const PDFAppEditContentPaintColor color = PDFAppEditContentAppearanceHelper::getPaintColor(*leaf, *state, true);
                m_fill.color.add(color);
                m_fill.colorSpace.add(color.space);
            }
            if (PDFAppEditContentAppearanceHelper::hasStroke(*leaf, *state))
            {
                ++strokeCount;
                m_stroke.isPainted = true;
                const PDFAppEditContentPaintColor color = PDFAppEditContentAppearanceHelper::getPaintColor(*leaf, *state, false);
                m_stroke.color.add(color);
                m_stroke.colorSpace.add(color.space);
                width.add(state->lineWidth);
                dash.add(PDFAppEditContentAppearanceHelper::getDashIndex(state->dashPattern));
                cap.add(PDFAppEditContentAppearanceHelper::getCapIndex(state->lineCap));
                join.add(PDFAppEditContentAppearanceHelper::getJoinIndex(state->lineJoin));
                miterLimit.add(state->miterLimit);
            }
        }

        if (isTruncated || isLeavesTruncated)
        {
            // Not every object was read: no value is claimed to be the value of all of them
            m_fill.color.add(PDFAppEditContentPaintColor{ QByteArray("Other"), { }, QColor() });
            m_stroke.color.add(PDFAppEditContentPaintColor{ QByteArray("Other"), { }, QColor() });
            m_fill.colorSpace.add(QByteArray("Other"));
            m_stroke.colorSpace.add(QByteArray("Other"));
            width.add(-1.0);
            dash.add(-1);
            cap.add(-1);
            join.add(-1);
            miterLimit.add(-1.0);
        }

        const int leafCount = int(leaves.size());
        updatePaint(&m_fill, fillCount, leafCount);
        updatePaint(&m_stroke, strokeCount, leafCount);

        // The stroke rows exist only for a stroked object (-color-cmyk has no Width and no Expand link): the expanded
        // part is created when a stroke appears and retired when it goes (updateCard runs from the event loop)
        m_widthRow->setVisible(m_stroke.isPainted);
        PDFAppEditContentAppearanceHelper::setFieldValue(m_width, width);
        setComboValue(m_dash, dash);
        if (m_stroke.isPainted && !m_expanded)
        {
            createExpanded();
        }
        else if (!m_stroke.isPainted && m_expanded)
        {
            m_section->setExpandedWidget(nullptr);
            m_expanded = nullptr;
        }
        if (m_expanded)
        {
            setComboValue(m_cap, cap);
            setComboValue(m_join, join);
            PDFAppEditContentAppearanceHelper::setFieldValue(m_miterLimit, miterLimit);
        }

        // The note of the frame of the state
        const bool isMixed = m_fill.color.isMixed() || m_stroke.color.isMixed() || width.isMixed();
        if (m_nodes.size() > 1 && isMixed)
        {
            m_note->setText(tr("Mixed marks a value that differs between the selected objects."));
        }
        else if (m_fill.isPainted && m_stroke.isPainted)
        {
            m_note->setText(tr("Fill and stroke are two paints. Each has its own color space, swatches and values under its check box; the eyedropper of a row sets only that paint."));
        }
        else
        {
            m_note->setText(tr("The swatches are the colors used in the document. The values are in the color space of the object; nothing is converted until you choose another space."));
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

        // The core has no command, which switches the painting of an object on or off (set_appearance changes the
        // colors and the line only): the check box shows the state and cannot be changed yet
        paint->check->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        paint->check->setFocusPolicy(Qt::NoFocus);

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
        paint->check->setCheckState(paintedCount == 0 ? Qt::Unchecked : (paintedCount == leafCount ? Qt::Checked : Qt::PartiallyChecked));
        paint->space->setEnabled(paint->isPainted);

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

        // The swatches (shown for a painted object; none selected when the colors differ)
        updateSwatches(paint);
        paint->swatchRow->setVisible(paint->isPainted);

        // The values: one color of a device space
        const bool hasValues = paint->isPainted && !color.isMixed() && color.getValue() && color.getValue()->isDevice();
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
                    paint->labels[size_t(i)]->setText(labels[i]);
                    paint->fields[size_t(i)]->setAccessibleName(labels[i]);
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
        if (!container || (m_colorsVersion == container->getVersion() && m_colorsKey == container->getKey()))
        {
            return m_colors;
        }

        m_colors.clear();
        m_colorsVersion = container->getVersion();
        m_colorsKey = container->getKey();

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
        PDFAppEditContentContext* context = getContext();
        if (!context || m_nodes.empty())
        {
            return;
        }
        context->execute(PDFContentCommandFactory::createSetAppearance(PDFAppEditContentAppearanceHelper::getReferences(m_nodes), std::move(properties)));
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
        // Another device space: the display color of the objects in the new space (the frames: "nothing is converted
        // until you choose another space"); objects of different colors are changed in one undo step, each group
        // of the same color by one command
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
        std::map<QRgb, std::vector<PDFContentNodeReference>> groups;
        for (const PDFAppEditContentCardTools::SelectedNode& node : m_nodes)
        {
            bool isTruncated = false;
            const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, { node }, &PDFAppEditContentCardTools::isPaintedLeaf, &isTruncated);
            for (const PDFContentNodePointer& leaf : leaves)
            {
                const std::optional<PDFContentDisplayState> state = PDFContentDisplay::getDisplayState(*container, leaf->getId(), displayContext);
                if (state && (isFill ? PDFAppEditContentAppearanceHelper::hasFill(*leaf, *state) : PDFAppEditContentAppearanceHelper::hasStroke(*leaf, *state)))
                {
                    const QColor color = isFill ? state->fillColor.color : state->strokeColor.color;
                    groups[color.isValid() ? color.rgb() : qRgb(0, 0, 0)].push_back(node.reference);
                    break;
                }
            }
        }

        if (groups.empty())
        {
            return;
        }

        const PDFContentStateProperty property = getProperty(paint);
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
            properties[property] = PDFAppEditContentAppearanceHelper::createColorValue(space, PDFAppEditContentAppearanceHelper::convert(QColor::fromRgb(group.first), space));
            guard->execute(PDFContentCommandFactory::createSetAppearance(group.second, std::move(properties)));
        }
        if (isMacro && guard)
        {
            guard->endMacro();
        }
    }

    void onWidthEntered(double value)
    {
        setProperties({ { PDFContentStateProperty::LineWidth, PDFAppEditContentAppearanceHelper::createRealValue(value) } });
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
    QPointer<QWidget> m_expanded;               ///< Retired by the section, when the stroke goes
    QPointer<PDFComboBox> m_cap;
    QPointer<PDFComboBox> m_join;
    QPointer<PDFDoubleSpinBox> m_miterLimit;
    std::vector<PDFAppEditContentCardTools::SelectedNode> m_nodes;
    std::vector<PDFAppEditContentPaintColor> m_colors;
    quint64 m_colorsVersion = 0;
    PDFContentContainerKey m_colorsKey;
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

        bool isTruncated = false;
        m_nodes = PDFAppEditContentCardTools::getSelectedNodes(context, &isTruncated);
        bool isLeavesTruncated = false;
        const std::vector<PDFContentNodePointer> leaves = PDFAppEditContentCardTools::getLeaves(*container, m_nodes, &PDFAppEditContentCardTools::isDrawingLeaf, &isLeavesTruncated);
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
        if (context && !m_nodes.empty())
        {
            context->execute(PDFContentCommandFactory::createSetTransparency(PDFAppEditContentAppearanceHelper::getReferences(m_nodes), alphaStroking, alphaFilling, blendMode));
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
    std::vector<PDFAppEditContentCardTools::SelectedNode> m_nodes;
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
