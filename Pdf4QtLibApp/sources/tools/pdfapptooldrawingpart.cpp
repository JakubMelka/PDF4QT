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

#include "pdfapptooldrawingpart.h"
#include "pdfappcolorpicker.h"
#include "pdfappcontext.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfappworkspace.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdeferreddeletion.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfpagecontenteditortools.h"
#include "pdfpagecontentelements.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"
#include "pdfwidgettool.h"

#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Helpers of the drawing part (no free functions in Pdf4QtLibApp)
class PDFAppToolDrawingPartHelper
{
public:
    PDFAppToolDrawingPartHelper() = delete;

    static constexpr const char* KIND_PROPERTY = "pdf4qtDrawingKind";
    static constexpr const char* OPERATION_PROPERTY = "pdf4qtDrawingOperation";
    static constexpr const char* ELEMENT_PROPERTY = "pdf4qtDrawingElement";

    /// The properties of a style changed over the selected elements (changeSelectedStyle)
    enum StyleProperty
    {
        PenColor,
        PenWidth,
        FillColor,
        FontFamily,
        FontSize,
        Alignment,
        Angle
    };

    /// card-elements 435:8765: padding 6 + the border, the rows 2 apart
    static constexpr int LIST_PADDING = 7;
    static constexpr int LIST_SPACING = 2;

    /// The card of the style (434:5700): padding 12 + the border, the fields 8 apart; the swatches 21
    /// below the head of the field (1567:19208)
    static constexpr int STYLE_PADDING = 13;
    static constexpr int STYLE_SPACING = 8;
    static constexpr int SWATCH_GAP = 6;
    static constexpr int SPIN_WIDTH = 90;
    static constexpr int COMBO_WIDTH = 150;
    static constexpr int ALIGNMENT_WIDTH = 195;

    /// A field row of the card (the label left, the control right) with the width of the control of
    /// the design: Combo box 150, Spin box 90, the alignment 195 (434:4614). The common row gives every
    /// control its field width (140), which cut "Center" of the alignment (review RS6FIGMA).
    static QWidget* createFieldRow(const QString& label, QWidget* control, int width, QWidget* parent)
    {
        QWidget* row = PDFAppToolCard::createFieldRow(label, control, parent);
        control->setFixedWidth(width);
        return row;
    }

    /// Largest width of a pen and size of a font of the panel
    static constexpr double MAXIMUM_PEN_WIDTH = 72.0;
    static constexpr double MINIMUM_FONT_SIZE = 4.0;
    static constexpr double MAXIMUM_FONT_SIZE = 144.0;

    static QPen createPen(const PDFPageDrawing::Style& style)
    {
        QPen pen(style.penColor);
        pen.setWidthF(style.penWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        if (style.penWidth <= 0.0)
        {
            pen.setStyle(Qt::NoPen);
        }
        return pen;
    }

    static QBrush createBrush(const PDFPageDrawing::Style& style)
    {
        return style.fillColor.isValid() ? QBrush(style.fillColor) : QBrush(Qt::NoBrush);
    }

    static QFont createFont(const PDFPageDrawing::Style& style)
    {
        QFont font;
        if (!style.fontFamily.isEmpty())
        {
            font.setFamily(style.fontFamily);
        }
        font.setPointSizeF(qBound(MINIMUM_FONT_SIZE, style.fontSize, MAXIMUM_FONT_SIZE));
        return font;
    }

    static Qt::Alignment createAlignment(PDFPageDrawing::Alignment alignment)
    {
        switch (alignment)
        {
            case PDFPageDrawing::Alignment::Left:
                return Qt::AlignLeft | Qt::AlignVCenter;
            case PDFPageDrawing::Alignment::Center:
                return Qt::AlignHCenter | Qt::AlignVCenter;
            case PDFPageDrawing::Alignment::Right:
                return Qt::AlignRight | Qt::AlignVCenter;
        }
        return Qt::AlignLeft | Qt::AlignVCenter;
    }

    static PDFPageDrawing::Alignment getAlignment(Qt::Alignment alignment)
    {
        if (alignment.testFlag(Qt::AlignHCenter))
        {
            return PDFPageDrawing::Alignment::Center;
        }
        if (alignment.testFlag(Qt::AlignRight))
        {
            return PDFPageDrawing::Alignment::Right;
        }
        return PDFPageDrawing::Alignment::Left;
    }

    /// The style of a styled element of the scene
    static PDFPageDrawing::Style getStyle(const PDFPageContentElement* element)
    {
        PDFPageDrawing::Style style;
        if (const PDFPageContentStyledElement* styled = dynamic_cast<const PDFPageContentStyledElement*>(element))
        {
            const QPen& pen = styled->getPen();
            style.penColor = pen.color();
            style.penWidth = pen.style() == Qt::NoPen ? 0.0 : pen.widthF();
            const QBrush& brush = styled->getBrush();
            style.fillColor = brush.style() == Qt::NoBrush ? QColor() : brush.color();
        }
        if (const PDFPageContentElementTextBox* text = element ? element->asElementTextBox() : nullptr)
        {
            style.fontFamily = text->getFont().family();
            style.fontSize = text->getFont().pointSizeF() > 0.0 ? text->getFont().pointSizeF() : style.fontSize;
            style.alignment = getAlignment(text->getAlignment());
            style.angle = text->getAngle();
        }
        return style;
    }

    /// The points of a curve of the scene (its move and line elements)
    static std::vector<QPointF> getCurvePoints(const QPainterPath& path)
    {
        std::vector<QPointF> points;
        points.reserve(size_t(path.elementCount()));
        for (int i = 0; i < path.elementCount(); ++i)
        {
            const QPainterPath::Element element = path.elementAt(i);
            points.emplace_back(element.x, element.y);
        }
        return points;
    }

    static QPainterPath createCurve(const std::vector<QPointF>& points)
    {
        QPainterPath path;
        for (size_t i = 0; i < points.size(); ++i)
        {
            if (i == 0)
            {
                path.moveTo(points[i]);
            }
            else
            {
                path.lineTo(points[i]);
            }
        }
        return path;
    }

    static bool isFinite(const QRectF& rect)
    {
        return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) && std::isfinite(rect.height());
    }

    /// The kinds of the menu Draw of Sign (434:5346) with the dividers after Svg image and Dot
    static std::vector<PDFAppToolDrawingPart::ElementKind> getDrawKinds()
    {
        using Kind = PDFAppToolDrawingPart::ElementKind;
        return { Kind::Text, Kind::FreehandCurve, Kind::SvgImage, Kind::AcceptMark, Kind::RejectMark, Kind::Dot,
                 Kind::Rectangle, Kind::RoundedRectangle, Kind::Line, Kind::HorizontalLine, Kind::VerticalLine };
    }

    /// The kinds of the buttons of Sign by Hand (435:8712 ... 435:8741)
    static std::vector<PDFAppToolDrawingPart::ElementKind> getButtonKinds()
    {
        using Kind = PDFAppToolDrawingPart::ElementKind;
        return { Kind::Text, Kind::FreehandCurve, Kind::SvgImage, Kind::AcceptMark, Kind::RejectMark, Kind::Dot };
    }

    /// The kinds of the menu Shapes of Sign by Hand (435:9472)
    static std::vector<PDFAppToolDrawingPart::ElementKind> getShapeKinds()
    {
        using Kind = PDFAppToolDrawingPart::ElementKind;
        return { Kind::Rectangle, Kind::RoundedRectangle, Kind::Line, Kind::HorizontalLine, Kind::VerticalLine };
    }
};

// ------------------------------------------------------------ PDFAppToolDrawingElementList

PDFAppToolDrawingElementList::PDFAppToolDrawingElementList(PDFAppToolDrawingPart* part, QWidget* parent) :
    BaseClass(parent),
    m_part(part)
{
    using Helper = PDFAppToolDrawingPartHelper;

    setObjectName(QStringLiteral("drawingElementList"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, 0);
    m_card = new PDFAppToolCard(this);
    m_card->setObjectName(QStringLiteral("drawingElementCard"));
    m_card->getBodyLayout()->setContentsMargins(Helper::LIST_PADDING, Helper::LIST_PADDING, Helper::LIST_PADDING, Helper::LIST_PADDING);
    m_card->getBodyLayout()->setSpacing(Helper::LIST_SPACING);
    layout->addWidget(m_card);
    rebuild();
}

PDFAppToolDrawingElementList::~PDFAppToolDrawingElementList() = default;

PDFInteger PDFAppToolDrawingElementList::getElementId(const PDFTileRow* row)
{
    return row ? row->property(PDFAppToolDrawingPartHelper::ELEMENT_PROPERTY).toLongLong() : 0;
}

void PDFAppToolDrawingElementList::rebuild()
{
    // The old rows are retired from the event loop (step A: a rebuilt row): one of them may be the sender of the
    // click, which leads here
    for (const QPointer<PDFTileRow>& row : m_rows)
    {
        if (row)
        {
            PDFDeferredDeletion::retireLater(row.data());
        }
    }
    for (const QPointer<PDFTileRow>& caption : m_captions)
    {
        if (caption)
        {
            PDFDeferredDeletion::retireLater(caption.data());
        }
    }
    m_rows.clear();
    m_captions.clear();

    // A retired card (step A: its tab, page or shell closed, a change of the drawing reaching the list from the rest
    // of a call or emission) gets no new rows
    if (!m_part || !m_card || PDFDeferredDeletion::isRetired(m_card.data()))
    {
        return;
    }

    const bool hasCaptions = m_part->getLayout() == PDFAppToolDrawingPart::Layout::SignByHand;
    std::vector<PDFInteger> ids = m_part->getElementIds();

    // 435:8765: the pages in their order, the elements of a page in the order of painting
    std::vector<std::pair<PDFInteger, PDFInteger>> pagesAndIds;
    pagesAndIds.reserve(ids.size());
    for (const PDFInteger id : ids)
    {
        pagesAndIds.emplace_back(m_part->getElementPage(id), id);
    }
    std::stable_sort(pagesAndIds.begin(), pagesAndIds.end());

    PDFInteger lastPage = -1;
    for (const auto& [pageIndex, id] : pagesAndIds)
    {
        if (hasCaptions && pageIndex != lastPage)
        {
            PDFTileRow* caption = new PDFTileRow(tr("Page %1").arg(pageIndex + 1), QString(), QString(), PDFTileRow::Kind::Caption, m_card);
            caption->setObjectName(QStringLiteral("drawingPageCaption-%1").arg(pageIndex + 1));
            caption->setClickable(false);
            m_card->getBodyLayout()->addWidget(caption);
            m_captions.emplace_back(caption);
            lastPage = pageIndex;
        }

        const std::optional<PDFAppToolDrawingPart::ElementKind> kind = m_part->getElementKind(id);
        const QString iconName = kind ? PDFAppToolDrawingPart::getKindIconName(*kind) : QString();
        PDFTileRow* row = new PDFTileRow(m_part->getElementTitle(id), m_part->getElementDescription(id), iconName, PDFTileRow::Kind::Result, m_card);
        row->setObjectName(QStringLiteral("drawingElement-%1").arg(id));
        row->setProperty(PDFAppToolDrawingPartHelper::ELEMENT_PROPERTY, qint64(id));
        row->setDescriptionKind(PDFTileRow::TextKind::Data);
        row->setClickable(true);
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        row->installEventFilter(this);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolDrawingElementList::onRowClicked);
        m_card->getBodyLayout()->addWidget(row);
        m_rows.emplace_back(row);
    }

    m_card->setVisible(!m_rows.empty());
    updateSelection();
}

void PDFAppToolDrawingElementList::updateSelection()
{
    const std::set<PDFInteger> selected = m_part ? m_part->getSelectedElements() : std::set<PDFInteger>();
    for (const QPointer<PDFTileRow>& row : m_rows)
    {
        if (!row)
        {
            continue;
        }

        // 435:8768: the selected row bg/accent-subtle with its tile Accent, the others Muted
        const bool isSelected = selected.count(getElementId(row.data())) > 0;
        row->setChecked(isSelected);
        row->setTone(isSelected ? PDFTileRow::Tone::Accent : PDFTileRow::Tone::Muted);
    }
}

bool PDFAppToolDrawingElementList::eventFilter(QObject* watched, QEvent* event)
{
    // R6-24: a double click on a text label edits its text (the old dock, Edit Item)
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        PDFTileRow* row = qobject_cast<PDFTileRow*>(watched);
        const PDFInteger id = getElementId(row);
        if (m_part && id > 0 && m_part->getElementKind(id) == PDFAppToolDrawingPart::ElementKind::Text)
        {
            // The last call: the dialog runs foreign code, this list may be made again meanwhile
            m_part->editText(id);
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolDrawingElementList::onRowClicked()
{
    const PDFInteger id = getElementId(qobject_cast<PDFTileRow*>(sender()));
    if (m_part && id > 0)
    {
        // The selection of the part makes the rows again (the last call)
        m_part->selectElements({ id });
    }
}

// ------------------------------------------------------------ PDFAppToolDrawingStylePanel

PDFAppToolDrawingStylePanel::PDFAppToolDrawingStylePanel(PDFAppToolDrawingPart* part, QWidget* parent) :
    BaseClass(parent),
    m_part(part)
{
    using Helper = PDFAppToolDrawingPartHelper;

    setObjectName(QStringLiteral("drawingStylePanel"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, PDFAppToolPanelBuilder::SECTION_GAP);
    m_header = new PDFAppToolSectionHeader(QString(), QString(), this);
    m_header->setObjectName(QStringLiteral("drawingStyleHeader"));
    layout->addWidget(m_header);

    // The text: 434:5700 (Font, Size, Text color, Alignment, Text angle, the note of Sign)
    PDFAppToolCard* textCard = new PDFAppToolCard(this);
    textCard->setObjectName(QStringLiteral("drawingTextCard"));
    textCard->getBodyLayout()->setContentsMargins(Helper::STYLE_PADDING, Helper::STYLE_PADDING, Helper::STYLE_PADDING, Helper::STYLE_PADDING);
    textCard->getBodyLayout()->setSpacing(Helper::STYLE_SPACING);

    m_fontCombo = new PDFComboBox(textCard);
    m_fontCombo->setObjectName(QStringLiteral("drawingFont"));
    m_fontCombo->setFixedWidth(Helper::COMBO_WIDTH);
    m_fontCombo->addItems(QFontDatabase::families());
    connect(m_fontCombo.data(), &QComboBox::currentIndexChanged, this, &PDFAppToolDrawingStylePanel::onFontChanged);
    textCard->getBodyLayout()->addWidget(Helper::createFieldRow(tr("Font"), m_fontCombo, Helper::COMBO_WIDTH, textCard));

    m_fontSizeSpin = new PDFDoubleSpinBox(textCard);
    m_fontSizeSpin->setObjectName(QStringLiteral("drawingFontSize"));
    m_fontSizeSpin->setFixedWidth(Helper::SPIN_WIDTH);
    m_fontSizeSpin->setRange(Helper::MINIMUM_FONT_SIZE, Helper::MAXIMUM_FONT_SIZE);
    m_fontSizeSpin->setDecimals(1);
    m_fontSizeSpin->setTrailingZerosHidden(true);   // 434:5884 "13", not "13.0"
    m_fontSizeSpin->setDisplaySuffix(tr("pt", "Unit of a size of a font"));
    connect(m_fontSizeSpin.data(), &QDoubleSpinBox::editingFinished, this, &PDFAppToolDrawingStylePanel::onFontSizeChanged);
    textCard->getBodyLayout()->addWidget(Helper::createFieldRow(tr("Size"), m_fontSizeSpin, Helper::SPIN_WIDTH, textCard));

    // field-Text color 1567:19204: the label and the name of the color, the swatches 21 below
    QWidget* textColorField = new QWidget(textCard);
    QVBoxLayout* textColorLayout = PDFAppPageWidgets::createColumn(textColorField, Helper::SWATCH_GAP);
    QHBoxLayout* textColorHead = new QHBoxLayout();
    textColorHead->setContentsMargins(0, 0, 0, 0);
    textColorHead->addWidget(PDFAppPageWidgets::createLabel(tr("Text color"), QFont::Normal, 12, PDFThemeColors::TextSecondary, textColorField), 1);
    m_textColorName = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, textColorField);
    m_textColorName->setObjectName(QStringLiteral("drawingTextColorName"));
    textColorHead->addWidget(m_textColorName);
    textColorLayout->addLayout(textColorHead);
    m_textColorRow = new PDFColorSwatchRow(textColorField);
    m_textColorRow->setObjectName(QStringLiteral("drawingTextColor"));
    // 1567:19183: black, white, red, orange, yellow, green, blue and "+" (a custom color) - the row fits
    // the card of 261 (the annotation palette with More would be 302 wide and widen the panel)
    m_textColorRow->setOptions(PDFColorSwatchRow::Option::Inline | PDFColorSwatchRow::Option::More);
    connect(m_textColorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppToolDrawingStylePanel::onTextColorSelected);
    PDFAppColorPicker::attach(m_textColorRow.data());
    textColorLayout->addWidget(m_textColorRow);
    textCard->getBodyLayout()->addWidget(textColorField);

    m_alignmentControl = new PDFSegmentedControl(textCard);
    m_alignmentControl->setObjectName(QStringLiteral("drawingAlignment"));
    m_alignmentControl->addSegment(tr("Left"));
    m_alignmentControl->addSegment(tr("Center"));
    m_alignmentControl->addSegment(tr("Right"));
    m_alignmentControl->setFixedWidth(Helper::ALIGNMENT_WIDTH);
    connect(m_alignmentControl.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolDrawingStylePanel::onAlignmentChanged);
    textCard->getBodyLayout()->addWidget(Helper::createFieldRow(tr("Alignment"), m_alignmentControl, Helper::ALIGNMENT_WIDTH, textCard));

    m_angleSpin = new PDFDoubleSpinBox(textCard);
    m_angleSpin->setObjectName(QStringLiteral("drawingAngle"));
    m_angleSpin->setFixedWidth(Helper::SPIN_WIDTH);
    m_angleSpin->setRange(-360.0, 360.0);
    m_angleSpin->setDecimals(0);
    m_angleSpin->setDisplaySuffix(tr("°", "Unit of an angle"));
    connect(m_angleSpin.data(), &QDoubleSpinBox::editingFinished, this, &PDFAppToolDrawingStylePanel::onAngleChanged);
    textCard->getBodyLayout()->addWidget(Helper::createFieldRow(tr("Text angle"), m_angleSpin, Helper::SPIN_WIDTH, textCard));

    if (m_part && m_part->getLayout() == PDFAppToolDrawingPart::Layout::SignatureAppearance)
    {
        // 434:5755: the other elements are styled here too, when one is selected
        PDFAppParagraph* note = PDFAppPageWidgets::createText(tr("Shapes and curves show pen color, pen width and fill here instead."),
                                                              PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, textCard);
        note->setObjectName(QStringLiteral("drawingStyleNote"));
        textCard->getBodyLayout()->addWidget(note);
    }
    layout->addWidget(textCard);
    m_textCard = textCard;

    // Pen and fill: 435:8877 (Pen color, Pen width, Fill)
    PDFAppToolCard* penCard = new PDFAppToolCard(this);
    penCard->setObjectName(QStringLiteral("drawingPenCard"));
    penCard->getBodyLayout()->setContentsMargins(Helper::STYLE_PADDING, Helper::STYLE_PADDING, Helper::STYLE_PADDING, Helper::STYLE_PADDING);
    penCard->getBodyLayout()->setSpacing(Helper::STYLE_SPACING);

    QWidget* penColorField = new QWidget(penCard);
    QVBoxLayout* penColorLayout = PDFAppPageWidgets::createColumn(penColorField, Helper::SWATCH_GAP);
    QHBoxLayout* penColorHead = new QHBoxLayout();
    penColorHead->setContentsMargins(0, 0, 0, 0);
    penColorHead->addWidget(PDFAppPageWidgets::createLabel(tr("Pen color"), QFont::Normal, 12, PDFThemeColors::TextSecondary, penColorField), 1);
    m_penColorName = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, penColorField);
    m_penColorName->setObjectName(QStringLiteral("drawingPenColorName"));
    penColorHead->addWidget(m_penColorName);
    penColorLayout->addLayout(penColorHead);
    m_penColorRow = new PDFColorSwatchRow(penColorField);
    m_penColorRow->setObjectName(QStringLiteral("drawingPenColor"));
    m_penColorRow->setOptions(PDFColorSwatchRow::Option::Inline | PDFColorSwatchRow::Option::More);
    connect(m_penColorRow.data(), &PDFColorSwatchRow::colorSelected, this, &PDFAppToolDrawingStylePanel::onPenColorSelected);
    PDFAppColorPicker::attach(m_penColorRow.data());
    penColorLayout->addWidget(m_penColorRow);
    penCard->getBodyLayout()->addWidget(penColorField);

    m_penWidthSpin = new PDFDoubleSpinBox(penCard);
    m_penWidthSpin->setObjectName(QStringLiteral("drawingPenWidth"));
    m_penWidthSpin->setFixedWidth(Helper::SPIN_WIDTH);
    m_penWidthSpin->setRange(0.1, Helper::MAXIMUM_PEN_WIDTH);
    m_penWidthSpin->setDecimals(1);
    m_penWidthSpin->setTrailingZerosHidden(true);   // "2", "2.5"
    m_penWidthSpin->setSingleStep(0.5);
    m_penWidthSpin->setDisplaySuffix(tr("pt", "Unit of a width of a pen"));
    connect(m_penWidthSpin.data(), &QDoubleSpinBox::editingFinished, this, &PDFAppToolDrawingStylePanel::onPenWidthChanged);
    penCard->getBodyLayout()->addWidget(Helper::createFieldRow(tr("Pen width"), m_penWidthSpin, Helper::SPIN_WIDTH, penCard));

    // 435:8900 Fill: None, the color of the pen, white
    m_fillCombo = new PDFComboBox(penCard);
    m_fillCombo->setObjectName(QStringLiteral("drawingFill"));
    m_fillCombo->setFixedWidth(Helper::COMBO_WIDTH);
    m_fillCombo->addItem(tr("None"));
    m_fillCombo->addItem(tr("Pen color"));
    m_fillCombo->addItem(tr("White"));
    connect(m_fillCombo.data(), &QComboBox::activated, this, &PDFAppToolDrawingStylePanel::onFillChanged);
    penCard->getBodyLayout()->addWidget(Helper::createFieldRow(tr("Fill"), m_fillCombo, Helper::COMBO_WIDTH, penCard));
    layout->addWidget(penCard);
    m_penCard = penCard;

    refresh();
}

PDFAppToolDrawingStylePanel::~PDFAppToolDrawingStylePanel() = default;

QString PDFAppToolDrawingStylePanel::getColorName(const QColor& color)
{
    if (!color.isValid())
    {
        return tr("None");
    }
    for (const PDFColorSwatchRow::Entry& entry : PDFColorSwatchRow::getAnnotationColors(PDFColorSwatchRow::Option::White))
    {
        if (entry.canonicalColor.isValid() && entry.canonicalColor.rgb() == color.rgb())
        {
            return entry.name;
        }
    }
    return color.name(QColor::HexRgb).toUpper();
}

bool PDFAppToolDrawingStylePanel::isTextStyleShown() const
{
    return m_textCard && !m_textCard->isHidden();
}

void PDFAppToolDrawingStylePanel::refresh()
{
    if (!m_part)
    {
        return;
    }

    m_isRefreshing = true;
    const bool isText = m_part->isTextStyleShown();
    const PDFPageDrawing::Style style = m_part->getShownStyle();
    const bool isSign = m_part->getLayout() == PDFAppToolDrawingPart::Layout::SignatureAppearance;

    // 434:5699 "Style of the text label"; 435:8876 "Pen and fill", 435:10179 "Text"
    if (m_header)
    {
        if (isSign)
        {
            m_header->getTitleLabel()->setText(isText ? tr("Style of the text label") : tr("Style of the shapes"));
        }
        else
        {
            m_header->getTitleLabel()->setText(isText ? tr("Text") : tr("Pen and fill"));
        }
    }
    if (m_textCard)
    {
        m_textCard->setVisible(isText);
    }
    if (m_penCard)
    {
        m_penCard->setVisible(!isText);
    }

    if (m_fontCombo)
    {
        const QString family = style.fontFamily.isEmpty() ? QFont().family() : style.fontFamily;
        const int index = m_fontCombo->findText(family);
        m_fontCombo->setCurrentIndex(index);
    }
    if (m_fontSizeSpin)
    {
        m_fontSizeSpin->setValue(style.fontSize);
    }
    if (m_textColorRow)
    {
        m_textColorRow->setColor(style.penColor);
    }
    if (m_textColorName)
    {
        m_textColorName->setText(getColorName(style.penColor));
    }
    if (m_alignmentControl)
    {
        m_alignmentControl->setCurrentIndex(int(style.alignment));
    }
    if (m_angleSpin)
    {
        m_angleSpin->setValue(style.angle);
    }
    if (m_penColorRow)
    {
        m_penColorRow->setColor(style.penColor);
    }
    if (m_penColorName)
    {
        m_penColorName->setText(getColorName(style.penColor));
    }
    if (m_penWidthSpin)
    {
        m_penWidthSpin->setValue(style.penWidth);
    }
    if (m_fillCombo)
    {
        int fillIndex = 0;
        if (style.fillColor.isValid())
        {
            fillIndex = style.fillColor.rgb() == QColor(Qt::white).rgb() && style.penColor.rgb() != QColor(Qt::white).rgb() ? 2 : 1;
        }
        m_fillCombo->setCurrentIndex(fillIndex);
    }
    m_isRefreshing = false;
}

void PDFAppToolDrawingStylePanel::onFontChanged()
{
    if (!m_isRefreshing && m_part && m_fontCombo && m_fontCombo->currentIndex() >= 0)
    {
        m_part->setFontFamily(m_fontCombo->currentText());
    }
}

void PDFAppToolDrawingStylePanel::onFontSizeChanged()
{
    if (!m_isRefreshing && m_part && m_fontSizeSpin)
    {
        m_part->setFontSize(m_fontSizeSpin->value());
    }
}

void PDFAppToolDrawingStylePanel::onTextColorSelected(const QColor& color)
{
    if (!m_isRefreshing && m_part && color.isValid())
    {
        m_part->setTextColor(color);
    }
}

void PDFAppToolDrawingStylePanel::onAlignmentChanged(int index)
{
    if (!m_isRefreshing && m_part && index >= 0 && index <= int(PDFPageDrawing::Alignment::Right))
    {
        m_part->setAlignment(PDFPageDrawing::Alignment(index));
    }
}

void PDFAppToolDrawingStylePanel::onAngleChanged()
{
    if (!m_isRefreshing && m_part && m_angleSpin)
    {
        m_part->setAngle(m_angleSpin->value());
    }
}

void PDFAppToolDrawingStylePanel::onPenColorSelected(const QColor& color)
{
    if (!m_isRefreshing && m_part && color.isValid())
    {
        m_part->setPenColor(color);
    }
}

void PDFAppToolDrawingStylePanel::onPenWidthChanged()
{
    if (!m_isRefreshing && m_part && m_penWidthSpin)
    {
        m_part->setPenWidth(m_penWidthSpin->value());
    }
}

void PDFAppToolDrawingStylePanel::onFillChanged()
{
    if (m_isRefreshing || !m_part || !m_fillCombo)
    {
        return;
    }
    QColor fill;
    switch (m_fillCombo->currentIndex())
    {
        case 1:
            fill = m_part->getShownStyle().penColor;
            break;
        case 2:
            fill = QColor(Qt::white);
            break;
        default:
            break;
    }
    m_part->setFillColor(fill);
}

// ------------------------------------------------------------ PDFAppToolDrawingHint

PDFAppToolDrawingHint::PDFAppToolDrawingHint(QWidget* canvasArea) :
    BaseClass(canvasArea)
{
    setObjectName(QStringLiteral("drawingCanvasHint"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_label = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, this);
    m_label->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_label);
    if (canvasArea)
    {
        PDFDeferredDeletion::installEventFilter(canvasArea, this);
    }
    updatePlacement();
}

PDFAppToolDrawingHint::~PDFAppToolDrawingHint() = default;

void PDFAppToolDrawingHint::setText(const QString& text)
{
    if (m_label)
    {
        m_label->setText(text);
    }
    setVisible(!text.isEmpty());
    updatePlacement();
}

QString PDFAppToolDrawingHint::getText() const
{
    return m_label ? m_label->text() : QString();
}

bool PDFAppToolDrawingHint::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent() && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    {
        updatePlacement();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolDrawingHint::updatePlacement()
{
    QWidget* canvas = parentWidget();
    if (!canvas)
    {
        return;
    }
    setGeometry(0, TOP, canvas->width(), HEIGHT);
    raise();
}

// ------------------------------------------------------------ PDFAppToolDrawingPart

PDFAppToolDrawingPart::PDFAppToolDrawingPart(PDFAppContext* context, Layout layout, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context),
    m_layout(layout)
{
    m_scene = new PDFPageContentScene(this);
    m_scene->setObjectName(QStringLiteral("drawingScene"));
    connect(m_scene, &PDFPageContentScene::sceneChanged, this, &PDFAppToolDrawingPart::onSceneChanged);
    connect(m_scene, &PDFPageContentScene::selectionChanged, this, &PDFAppToolDrawingPart::onSceneSelectionChanged);
    connect(m_scene, &PDFPageContentScene::editElementRequest, this, &PDFAppToolDrawingPart::onSceneEditRequest);
}

PDFAppToolDrawingPart::~PDFAppToolDrawingPart()
{
    // The scene leaves the view before it dies (the proxy and the widget would keep its address);
    // nothing is reported into the model any more
    if (m_model)
    {
        disconnect(m_model.data(), nullptr, this, nullptr);
    }

    // The form of a text goes with the tool (it reports nothing more, it deletes itself)
    if (m_editDialog)
    {
        disconnect(m_editDialog.data(), nullptr, this, nullptr);
        m_editDialog->close();
    }
    detachFromView();

    // The scene may be inside of its own call (a slot of its signal deletes the tool): it goes later
    if (m_scene)
    {
        disconnect(m_scene, nullptr, this, nullptr);
        m_scene->setParent(nullptr);
        m_scene->deleteLater();
        m_scene = nullptr;
    }
}

void PDFAppToolDrawingPart::retire()
{
    // The scene leaves the view at once (the proxy and the widget forget it, the active creation tool ends);
    // detachFromView reports nothing (m_isDetaching). The creation tools stay children of the part (a hook never
    // moves an object out of the retired subtree): retire shuts them down with the part, they are deleted with it
    // (deleteTools). The form of a text and the error message are owned by the part (setOwner) and close with it.
    detachFromView();
}

void PDFAppToolDrawingPart::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppToolDrawingPartHelper;

    m_drawRows.clear();
    m_arrangeRows.clear();
    m_elementButtons.clear();

    if (m_layout == Layout::SignatureAppearance)
    {
        // 434:5346 Draw the appearance: 11 elements, dividers after SVG image… and Dot
        m_drawPanel = builder.addMenu(QStringLiteral("draw"), QStringLiteral("pen-tool"), tr("Draw"), DRAW_PANEL_WIDTH);
        m_drawPanel->addGroupCaption(tr("Draw the appearance"));
        for (const ElementKind kind : Helper::getDrawKinds())
        {
            if (kind == ElementKind::AcceptMark || kind == ElementKind::Rectangle)
            {
                m_drawPanel->addGroupDivider();
            }
            const QString title = kind == ElementKind::SvgImage ? tr("SVG image…") : getKindTitle(kind);
            PDFTileRow* row = m_drawPanel->addRow(title, getKindDescription(kind), getKindIconName(kind), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("drawElement-%1").arg(int(kind)));
            row->setProperty(Helper::KIND_PROPERTY, int(kind));
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolDrawingPart::onDrawRowClicked);
            m_drawRows[kind] = row;
        }
    }
    else
    {
        // 435:8712 ... 435:8741: icon buttons, the divider after SVG image…
        for (const ElementKind kind : Helper::getButtonKinds())
        {
            if (kind == ElementKind::AcceptMark)
            {
                builder.addActionDivider();
            }
            const QString title = kind == ElementKind::SvgImage ? tr("SVG image…") : getKindTitle(kind);
            QToolButton* button = builder.addAction(title, getKindIconName(kind), this, &PDFAppToolDrawingPart::onElementButtonClicked);
            button->setObjectName(QStringLiteral("drawButton-%1").arg(int(kind)));

            // An icon button (Toolbar button 190:538, the icon 20 at every width of the toolbar): the
            // title is its tooltip and its accessible name (435:9169)
            button->setText(QString());
            PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::ICON_ONLY);
            button->setToolTip(QStringLiteral("%1 – %2").arg(title, getKindDescription(kind)));
            button->setAccessibleName(title);
            button->setCheckable(true);
            button->setProperty(Helper::KIND_PROPERTY, int(kind));
            m_elementButtons[kind] = button;
        }
        builder.addActionDivider();

        // 435:9472 Shapes
        m_shapesPanel = builder.addMenu(QStringLiteral("shapes"), QStringLiteral("square"), tr("Shapes"), SHAPES_PANEL_WIDTH);
        m_shapesPanel->addGroupCaption(tr("Shapes"));
        for (const ElementKind kind : Helper::getShapeKinds())
        {
            PDFTileRow* row = m_shapesPanel->addRow(getKindTitle(kind), getKindDescription(kind), getKindIconName(kind), PDFTileRow::Kind::MenuItem);
            row->setObjectName(QStringLiteral("drawElement-%1").arg(int(kind)));
            row->setProperty(Helper::KIND_PROPERTY, int(kind));
            connect(row, &PDFTileRow::clicked, this, &PDFAppToolDrawingPart::onDrawRowClicked);
            m_drawRows[kind] = row;
        }
    }

    // 434:5772 Arrange: one list of Align, Size, Center on page, Lay out (434:5882)
    m_arrangePanel = builder.addMenu(QStringLiteral("arrange"), QStringLiteral("obj-align-left"), tr("Arrange"), ARRANGE_PANEL_WIDTH);
    struct ArrangeItem
    {
        ArrangeOperation operation;
        const char* title;
        const char* description;
        const char* iconName;
    };
    const ArrangeItem items[] = {
        { ArrangeOperation::AlignLeft, QT_TR_NOOP("Align left"), QT_TR_NOOP("To the leftmost edge"), "obj-align-left" },
        { ArrangeOperation::AlignCenterHorizontally, QT_TR_NOOP("Center horizontally"), QT_TR_NOOP("On a common vertical axis"), "obj-align-hcenter" },
        { ArrangeOperation::AlignRight, QT_TR_NOOP("Align right"), QT_TR_NOOP("To the rightmost edge"), "obj-align-right" },
        { ArrangeOperation::AlignTop, QT_TR_NOOP("Align top"), QT_TR_NOOP("To the topmost edge"), "obj-align-top" },
        { ArrangeOperation::AlignCenterVertically, QT_TR_NOOP("Center vertically"), QT_TR_NOOP("On a common horizontal axis"), "obj-align-vcenter" },
        { ArrangeOperation::AlignBottom, QT_TR_NOOP("Align bottom"), QT_TR_NOOP("To the lowest edge"), "obj-align-bottom" },
        { ArrangeOperation::SameWidth, QT_TR_NOOP("Same width"), QT_TR_NOOP("Make the widths equal"), "same-width" },
        { ArrangeOperation::SameHeight, QT_TR_NOOP("Same height"), QT_TR_NOOP("Make the heights equal"), "same-height" },
        { ArrangeOperation::SameSize, QT_TR_NOOP("Same size"), QT_TR_NOOP("Make both equal"), "same-size" },
        { ArrangeOperation::CenterHorizontally, QT_TR_NOOP("Horizontally"), QT_TR_NOOP("Between the left and right edge"), "center-h" },
        { ArrangeOperation::CenterVertically, QT_TR_NOOP("Vertically"), QT_TR_NOOP("Between the top and bottom edge"), "center-v" },
        { ArrangeOperation::CenterBoth, QT_TR_NOOP("Both"), QT_TR_NOOP("In the middle of the page"), "center-both" },
        { ArrangeOperation::InAColumn, QT_TR_NOOP("In a column"), QT_TR_NOOP("One under another"), "layout-columns" },
        { ArrangeOperation::InARow, QT_TR_NOOP("In a row"), QT_TR_NOOP("Side by side"), "layout-rows" },
        { ArrangeOperation::AsAForm, QT_TR_NOOP("As a form"), QT_TR_NOOP("Labels left, values right"), "layout-form" },
        { ArrangeOperation::InAGrid, QT_TR_NOOP("In a grid"), QT_TR_NOOP("In rows and columns"), "layout-grid" },
    };
    for (const ArrangeItem& item : items)
    {
        if (item.operation == ArrangeOperation::AlignLeft)
        {
            m_arrangePanel->addGroupCaption(tr("Align"));
        }
        else if (item.operation == ArrangeOperation::SameWidth)
        {
            m_arrangePanel->addGroupDivider();
            m_arrangePanel->addGroupCaption(tr("Size"));
        }
        else if (item.operation == ArrangeOperation::CenterHorizontally)
        {
            m_arrangePanel->addGroupDivider();
            m_arrangePanel->addGroupCaption(tr("Center on page"));
        }
        else if (item.operation == ArrangeOperation::InAColumn)
        {
            m_arrangePanel->addGroupDivider();
            m_arrangePanel->addGroupCaption(tr("Lay out"));
        }
        PDFTileRow* row = m_arrangePanel->addRow(tr(item.title), tr(item.description), QString::fromLatin1(item.iconName), PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("arrange-%1").arg(int(item.operation)));
        row->setProperty(Helper::OPERATION_PROPERTY, int(item.operation));
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolDrawingPart::onArrangeRowClicked);
        m_arrangeRows[item.operation] = row;
    }

    // 434:5539 Clear All
    m_clearButton = builder.addAction(tr("Clear All"), QStringLiteral("eraser"), this, &PDFAppToolDrawingPart::onClearClicked);
    m_clearButton->setObjectName(QStringLiteral("drawingClearAll"));

    updateActions();
}

QWidget* PDFAppToolDrawingPart::createElementList(QWidget* parent)
{
    PDFAppToolDrawingElementList* list = new PDFAppToolDrawingElementList(this, parent);
    m_elementList = list;
    return list;
}

QWidget* PDFAppToolDrawingPart::createStylePanel(QWidget* parent)
{
    PDFAppToolDrawingStylePanel* panel = new PDFAppToolDrawingStylePanel(this, parent);
    m_stylePanel = panel;
    return panel;
}

QWidget* PDFAppToolDrawingPart::createCanvasHint(QWidget* canvasArea)
{
    PDFAppToolDrawingHint* hint = new PDFAppToolDrawingHint(canvasArea);
    hint->setText(m_canvasHintText);
    m_canvasHint = hint;
    return hint;
}

void PDFAppToolDrawingPart::setCanvasHintText(const QString& text)
{
    m_canvasHintText = text;

    // The hint lies on the canvas of the tool: once the shell has closed (step A: the canvas retired, its GUI
    // still updating from the rest of a call or emission) it is shown no more
    if (m_canvasHint && !PDFDeferredDeletion::isRetired(m_canvasHint.data()))
    {
        m_canvasHint->setText(text);
    }
}

void PDFAppToolDrawingPart::attachToView(PDFDrawWidgetProxy* proxy)
{
    if (proxy == m_proxy.data() && proxy)
    {
        return;
    }

    // Also after the view has died (onProxyDestroyed): the scene is deactivated and the tools of
    // the dead view go here
    detachFromView();
    if (!proxy || !proxy->getWidget())
    {
        return;
    }

    m_proxy = proxy;
    m_widget = proxy->getWidget();
    m_toolManager = m_widget->getToolManager();
    connect(proxy, &QObject::destroyed, this, &PDFAppToolDrawingPart::onProxyDestroyed);

    // The scene draws over the pages and takes the input of the widget (the old Signature plugin);
    // its activation is no change of the drawing (K6.10: no step of Undo)
    m_scene->setWidget(m_widget);
    if (m_isInputEnabled)
    {
        m_widget->addInputInterface(m_scene);
    }
    proxy->registerDrawInterface(m_scene);
    const bool wasApplying = m_isApplying;
    m_isApplying = true;
    m_scene->setActive(true);
    m_isApplying = wasApplying;
    createTools();
    updateActions();
}

void PDFAppToolDrawingPart::detachFromView()
{
    const bool wasDetaching = m_isDetaching;
    m_isDetaching = true;

    deleteTools();
    if (m_proxy)
    {
        disconnect(m_proxy.data(), nullptr, this, nullptr);
        m_proxy->unregisterDrawInterface(m_scene);
    }
    if (m_widget)
    {
        m_widget->removeInputInterface(m_scene);
        if (IDrawWidget* drawWidget = m_widget->getDrawWidget())
        {
            drawWidget->getWidget()->update();
        }
    }
    if (m_scene)
    {
        m_scene->setActive(false);
        m_scene->setWidget(nullptr);
    }
    m_proxy = nullptr;
    m_widget = nullptr;
    m_toolManager = nullptr;
    m_isDetaching = wasDetaching;
}

bool PDFAppToolDrawingPart::isAttached() const
{
    return m_proxy && m_widget;
}

void PDFAppToolDrawingPart::onProxyDestroyed()
{
    // The view dies first (ASan A): the proxy is a child of the widget, which is inside of its
    // destructor, so nothing may run here - no signal of the scene, no call of the widget or of the
    // tools. Only the pointers are forgotten (also the one the scene keeps). The scene stays active
    // and the tools (they forget their proxy themselves) stay children of this part until the part
    // leaves the view state by detachFromView - the next attachToView, the close of the tool, the
    // destructor - while every object it touches is whole (the widget is gone by then, so the scene
    // reports its deactivation to nobody of the view).
    m_proxy = nullptr;
    m_widget = nullptr;
    m_toolManager = nullptr;
    if (m_scene)
    {
        m_scene->setWidget(nullptr);
    }
}

void PDFAppToolDrawingPart::createTools()
{
    deleteTools();
    if (!m_proxy || !m_toolManager)
    {
        return;
    }

    PDFDrawWidgetProxy* proxy = m_proxy.data();
    m_tools[ElementKind::Text] = new PDFCreatePCElementTextTool(proxy, m_scene, nullptr, this);
    m_tools[ElementKind::FreehandCurve] = new PDFCreatePCElementFreehandCurveTool(proxy, m_scene, nullptr, this);
    m_tools[ElementKind::AcceptMark] = new PDFCreatePCElementImageTool(proxy, m_scene, nullptr, getMarkSvg(true), false, this);
    m_tools[ElementKind::RejectMark] = new PDFCreatePCElementImageTool(proxy, m_scene, nullptr, getMarkSvg(false), false, this);
    m_tools[ElementKind::Dot] = new PDFCreatePCElementDotTool(proxy, m_scene, nullptr, this);
    m_tools[ElementKind::Rectangle] = new PDFCreatePCElementRectangleTool(proxy, m_scene, nullptr, false, this);
    m_tools[ElementKind::RoundedRectangle] = new PDFCreatePCElementRectangleTool(proxy, m_scene, nullptr, true, this);
    m_tools[ElementKind::Line] = new PDFCreatePCElementLineTool(proxy, m_scene, nullptr, false, false, this);
    m_tools[ElementKind::HorizontalLine] = new PDFCreatePCElementLineTool(proxy, m_scene, nullptr, true, false, this);
    m_tools[ElementKind::VerticalLine] = new PDFCreatePCElementLineTool(proxy, m_scene, nullptr, false, true, this);
    if (!m_svgData.isEmpty())
    {
        m_tools[ElementKind::SvgImage] = new PDFCreatePCElementImageTool(proxy, m_scene, nullptr, m_svgData, false, this);
    }

    for (auto& item : m_tools)
    {
        m_toolManager->addTool(item.second.data());
        connect(item.second.data(), &PDFWidgetTool::toolActivityChanged, this, &PDFAppToolDrawingPart::onToolActivityChanged);
    }
    updateToolStyles();
}

void PDFAppToolDrawingPart::deleteTools()
{
    // An active tool of the part is ended first (the manager would keep its input); the manager
    // forgets a destroyed tool by itself
    if (m_toolManager)
    {
        PDFWidgetTool* activeTool = m_toolManager->getActiveTool();
        for (const auto& item : m_tools)
        {
            if (item.second && item.second.data() == activeTool)
            {
                m_toolManager->setActiveTool(nullptr);
                break;
            }
        }
    }
    // A tool may be inside of its own call (this part is deleted from a signal emitted by it, PDFCreatePCElementDotTool::
    // onPointPicked goes on with its members): it is deleted later, the manager forgets it at its destroyed. In the
    // hook of retire (step A) the tool stays a child of the part (a hook never moves an object out of the retired
    // subtree): its signals into the manager are cut now (the manager keeps its entry until its destroyed), retire
    // shuts it down with the part and it is deleted with the part.
    const bool isRetiring = isRetireHookCalled();
    for (auto& item : m_tools)
    {
        if (item.second)
        {
            disconnect(item.second.data(), nullptr, this, nullptr);
            if (isRetiring)
            {
                if (m_toolManager)
                {
                    disconnect(item.second.data(), &PDFWidgetTool::toolActivityChanged, m_toolManager.data(), nullptr);
                    disconnect(item.second.data(), &PDFWidgetTool::messageDisplayRequest, m_toolManager.data(), nullptr);
                }
                if (m_proxy)
                {
                    // The pick tool of a creation tool follows the draw space of the proxy (Pdf4QtLibWidgets connects
                    // it directly, outside of the helper): the retired tools take no more of its signals
                    QList<QObject*> objects = item.second->findChildren<QObject*>();
                    objects.push_back(item.second.data());
                    for (QObject* object : objects)
                    {
                        disconnect(m_proxy.data(), &PDFDrawWidgetProxy::drawSpaceChanged, object, nullptr);
                        disconnect(m_proxy.data(), &PDFDrawWidgetProxy::pageImageChanged, object, nullptr);
                    }
                }
                continue;
            }
            item.second->setParent(nullptr);
            item.second->deleteLater();
        }
    }
    m_tools.clear();
}

void PDFAppToolDrawingPart::bindParameter(PDFAppToolParameterModel* model, const QString& parameter)
{
    if (m_model)
    {
        disconnect(m_model.data(), nullptr, this, nullptr);
    }
    m_model = model;
    m_parameter = parameter;
    if (!model)
    {
        return;
    }

    PDFDeferredDeletion::connect(model, &PDFAppToolParameterModel::changed, this, &PDFAppToolDrawingPart::onModelChanged);
    PDFDeferredDeletion::connect(model, &PDFAppToolUndoTarget::undoStateChanged, this, &PDFAppToolDrawingPart::onModelUndoStateChanged);

    // The scene shows the value of the model (an Undo step of the open tool, a new GUI after a
    // change of the language)
    const QVariant value = model->getValue(parameter);
    if (value.canConvert<PDFPageDrawing>())
    {
        const PDFPageDrawing drawing = value.value<PDFPageDrawing>();
        m_lastWrittenDrawing = drawing;
        applyDrawing(drawing);
    }
}

PDFPageDrawing PDFAppToolDrawingPart::toDrawing() const
{
    std::vector<PDFPageDrawing::Element> elements;
    if (m_scene)
    {
        for (const PDFInteger id : getElementIds())
        {
            if (std::optional<PDFPageDrawing::Element> element = createDrawingElement(m_scene->getElementById(id)))
            {
                elements.push_back(std::move(*element));
            }
        }
    }
    return elements.empty() ? PDFPageDrawing() : PDFPageDrawing(std::move(elements));
}

void PDFAppToolDrawingPart::setDrawing(const PDFPageDrawing& drawing)
{
    // One step: the scene changes and reports it (onSceneChanged writes the model)
    if (!m_scene)
    {
        return;
    }
    PDFPageContentScene::SceneState state;
    state.elements = createElements(drawing);
    m_scene->restoreState(std::move(state));
}

void PDFAppToolDrawingPart::applyDrawing(const PDFPageDrawing& drawing)
{
    if (!m_scene)
    {
        return;
    }
    const bool wasApplying = m_isApplying;
    m_isApplying = true;
    PDFPageContentScene::SceneState state;
    state.elements = createElements(drawing);
    m_scene->restoreState(std::move(state));
    m_isApplying = wasApplying;
    updateParts();
}

std::vector<std::unique_ptr<PDFPageContentElement>> PDFAppToolDrawingPart::createElements(const PDFPageDrawing& drawing)
{
    using Helper = PDFAppToolDrawingPartHelper;
    using Kind = PDFPageDrawing::Kind;

    std::vector<std::unique_ptr<PDFPageContentElement>> elements;
    PDFInteger id = 1;
    for (const PDFPageDrawing::Element& source : drawing.getElements())
    {
        const QPen pen = Helper::createPen(source.style);
        const QBrush brush = Helper::createBrush(source.style);
        std::unique_ptr<PDFPageContentElement> element;
        switch (source.kind)
        {
            case Kind::Text:
            {
                auto text = std::make_unique<PDFPageContentElementTextBox>();
                text->setRectangle(source.rectangle);
                text->setText(source.text);
                text->setFont(Helper::createFont(source.style));
                text->setAlignment(Helper::createAlignment(source.style.alignment));
                text->setAngle(source.style.angle);
                text->setPen(pen);
                text->setBrush(brush);
                element = std::move(text);
                break;
            }

            case Kind::FreehandCurve:
            {
                auto curve = std::make_unique<PDFPageContentElementFreehandCurve>();
                curve->setCurve(Helper::createCurve(source.points));
                curve->setPen(pen);
                curve->setBrush(brush);
                element = std::move(curve);
                break;
            }

            case Kind::Line:
            {
                if (source.points.size() != 2)
                {
                    continue;
                }
                auto line = std::make_unique<PDFPageContentElementLine>();
                const QLineF lineGeometry(source.points[0], source.points[1]);
                line->setLine(lineGeometry);
                if (qFuzzyCompare(lineGeometry.y1(), lineGeometry.y2()))
                {
                    line->setGeometry(PDFPageContentElementLine::LineGeometry::Horizontal);
                }
                else if (qFuzzyCompare(lineGeometry.x1(), lineGeometry.x2()))
                {
                    line->setGeometry(PDFPageContentElementLine::LineGeometry::Vertical);
                }
                line->setPen(pen);
                line->setBrush(brush);
                element = std::move(line);
                break;
            }

            case Kind::Rectangle:
            case Kind::RoundedRectangle:
            {
                auto rectangle = std::make_unique<PDFPageContentElementRectangle>();
                rectangle->setRectangle(source.rectangle);
                rectangle->setRounded(source.kind == Kind::RoundedRectangle);
                rectangle->setPen(pen);
                rectangle->setBrush(brush);
                element = std::move(rectangle);
                break;
            }

            case Kind::Dot:
            {
                // The dot of the scene is a point of the width of its pen (its diameter)
                auto dot = std::make_unique<PDFPageContentElementDot>();
                dot->setPoint(source.rectangle.center());
                QPen dotPen = pen;
                dotPen.setStyle(Qt::SolidLine);
                dotPen.setWidthF(qMax(source.rectangle.width(), source.rectangle.height()));
                dot->setPen(dotPen);
                dot->setBrush(brush);
                element = std::move(dot);
                break;
            }

            case Kind::AcceptMark:
            case Kind::RejectMark:
            case Kind::SvgImage:
            {
                auto image = std::make_unique<PDFPageContentImageElement>();
                image->setRectangle(source.rectangle);
                image->setContent(source.kind == Kind::SvgImage ? source.svgData : getMarkSvg(source.kind == Kind::AcceptMark));
                element = std::move(image);
                break;
            }
        }

        if (element)
        {
            element->setPageIndex(source.pageIndex);
            element->setElementId(id++);
            elements.push_back(std::move(element));
        }
    }
    return elements;
}

std::optional<PDFAppToolDrawingPart::ElementKind> PDFAppToolDrawingPart::getKindOfElement(const PDFPageContentElement* element)
{
    if (!element)
    {
        return std::nullopt;
    }
    if (element->asElementTextBox())
    {
        return ElementKind::Text;
    }
    if (element->asElementFreehandCurve())
    {
        return ElementKind::FreehandCurve;
    }
    if (element->asElementDot())
    {
        return ElementKind::Dot;
    }
    if (const PDFPageContentElementRectangle* rectangle = element->asElementRectangle())
    {
        return rectangle->isRounded() ? ElementKind::RoundedRectangle : ElementKind::Rectangle;
    }
    if (const PDFPageContentElementLine* line = element->asElementLine())
    {
        switch (line->getGeometry())
        {
            case PDFPageContentElementLine::LineGeometry::Horizontal:
                return ElementKind::HorizontalLine;
            case PDFPageContentElementLine::LineGeometry::Vertical:
                return ElementKind::VerticalLine;
            case PDFPageContentElementLine::LineGeometry::General:
                break;
        }
        return ElementKind::Line;
    }
    if (const PDFPageContentImageElement* image = element->asElementImage())
    {
        if (image->getContent() == getMarkSvg(true))
        {
            return ElementKind::AcceptMark;
        }
        if (image->getContent() == getMarkSvg(false))
        {
            return ElementKind::RejectMark;
        }
        return ElementKind::SvgImage;
    }
    return std::nullopt;
}

std::optional<PDFPageDrawing::Element> PDFAppToolDrawingPart::createDrawingElement(const PDFPageContentElement* element)
{
    using Helper = PDFAppToolDrawingPartHelper;
    using Kind = PDFPageDrawing::Kind;

    const std::optional<ElementKind> kind = getKindOfElement(element);
    if (!kind)
    {
        return std::nullopt;
    }

    PDFPageDrawing::Element result;
    result.pageIndex = element->getPageIndex();
    result.style = Helper::getStyle(element);

    switch (*kind)
    {
        case ElementKind::Text:
        {
            const PDFPageContentElementTextBox* text = element->asElementTextBox();
            result.kind = Kind::Text;
            result.rectangle = text->getRectangle().normalized();
            result.text = text->getText();
            break;
        }

        case ElementKind::FreehandCurve:
            result.kind = Kind::FreehandCurve;
            result.points = Helper::getCurvePoints(element->asElementFreehandCurve()->getCurve());
            if (result.points.size() < 2)
            {
                return std::nullopt;
            }
            break;

        case ElementKind::Line:
        case ElementKind::HorizontalLine:
        case ElementKind::VerticalLine:
        {
            const QLineF line = element->asElementLine()->getLine();
            result.kind = Kind::Line;
            result.points = { line.p1(), line.p2() };
            break;
        }

        case ElementKind::Rectangle:
        case ElementKind::RoundedRectangle:
        {
            const QRectF rectangle = element->asElementRectangle()->getRectangle().normalized();
            result.kind = *kind == ElementKind::RoundedRectangle ? Kind::RoundedRectangle : Kind::Rectangle;
            result.rectangle = rectangle;

            // The radius of the scene (PDFPageContentElementRectangle::drawPage): a quarter of the shorter side
            result.cornerRadius = *kind == ElementKind::RoundedRectangle ? qMin(rectangle.width(), rectangle.height()) * 0.25 : 0.0;
            break;
        }

        case ElementKind::Dot:
        {
            // A filled circle of the color of the pen, its diameter the width of the pen
            const PDFPageContentElementDot* dot = element->asElementDot();
            const double diameter = qMax(dot->getPen().widthF(), 0.1);
            result.kind = Kind::Dot;
            result.rectangle = QRectF(dot->getPoint().x() - diameter * 0.5, dot->getPoint().y() - diameter * 0.5, diameter, diameter);
            result.style.fillColor = result.style.penColor;
            break;
        }

        case ElementKind::AcceptMark:
        case ElementKind::RejectMark:
        case ElementKind::SvgImage:
        {
            const PDFPageContentImageElement* image = element->asElementImage();
            result.kind = *kind == ElementKind::AcceptMark ? Kind::AcceptMark : (*kind == ElementKind::RejectMark ? Kind::RejectMark : Kind::SvgImage);
            result.rectangle = image->getRectangle().normalized();
            if (result.kind == Kind::SvgImage)
            {
                result.svgData = image->getContent();
            }
            break;
        }
    }

    if (!Helper::isFinite(result.rectangle))
    {
        return std::nullopt;
    }
    return result;
}

bool PDFAppToolDrawingPart::isEmpty() const
{
    return !m_scene || m_scene->isEmpty();
}

int PDFAppToolDrawingPart::getElementCount() const
{
    return m_scene ? int(m_scene->getElementIds().size()) : 0;
}

std::vector<PDFInteger> PDFAppToolDrawingPart::getPageIndices() const
{
    std::vector<PDFInteger> result;
    if (m_scene)
    {
        const std::set<PDFInteger> pages = m_scene->getPageIndices();
        result.assign(pages.cbegin(), pages.cend());
    }
    return result;
}

std::vector<PDFInteger> PDFAppToolDrawingPart::getElementIds() const
{
    // The order of painting: the scene keeps its elements in the order of their creation, the ids
    // grow with it (restoreState keeps the ids of the step)
    std::vector<PDFInteger> result;
    if (m_scene)
    {
        const std::set<PDFInteger> ids = m_scene->getElementIds();
        result.assign(ids.cbegin(), ids.cend());
    }
    return result;
}

void PDFAppToolDrawingPart::clearAll()
{
    if (m_scene && !m_scene->isEmpty())
    {
        const std::vector<PDFInteger> ids = getElementIds();
        m_scene->removeElementsById(ids);
    }
}

std::optional<PDFAppToolDrawingPart::ElementKind> PDFAppToolDrawingPart::getElementKind(PDFInteger id) const
{
    return m_scene ? getKindOfElement(m_scene->getElementById(id)) : std::nullopt;
}

PDFInteger PDFAppToolDrawingPart::getElementPage(PDFInteger id) const
{
    const PDFPageContentElement* element = m_scene ? m_scene->getElementById(id) : nullptr;
    return element ? element->getPageIndex() : -1;
}

QString PDFAppToolDrawingPart::getElementTitle(PDFInteger id) const
{
    const std::optional<ElementKind> kind = getElementKind(id);
    return kind ? getKindTitle(*kind) : QString();
}

QString PDFAppToolDrawingPart::getElementDescription(PDFInteger id) const
{
    // 434:5696 “Jane Novak”: the text of a label in quotes
    const PDFPageContentElement* element = m_scene ? m_scene->getElementById(id) : nullptr;
    const PDFPageContentElementTextBox* text = element ? element->asElementTextBox() : nullptr;
    return text ? tr("“%1”").arg(text->getText()) : QString();
}

QString PDFAppToolDrawingPart::getKindTitle(ElementKind kind)
{
    switch (kind)
    {
        case ElementKind::Text:
            return tr("Text label");
        case ElementKind::FreehandCurve:
            return tr("Freehand curve");
        case ElementKind::SvgImage:
            return tr("SVG image");
        case ElementKind::AcceptMark:
            return tr("Accept mark");
        case ElementKind::RejectMark:
            return tr("Reject mark");
        case ElementKind::Dot:
            return tr("Dot");
        case ElementKind::Rectangle:
            return tr("Rectangle");
        case ElementKind::RoundedRectangle:
            return tr("Rounded rectangle");
        case ElementKind::Line:
            return tr("Line");
        case ElementKind::HorizontalLine:
            return tr("Horizontal line");
        case ElementKind::VerticalLine:
            return tr("Vertical line");
    }
    return QString();
}

QString PDFAppToolDrawingPart::getKindDescription(ElementKind kind)
{
    // 434:5346 (448:17825 ... 448:17835)
    switch (kind)
    {
        case ElementKind::Text:
            return tr("Typed name, date or note");
        case ElementKind::FreehandCurve:
            return tr("Draw with the mouse or a pen");
        case ElementKind::SvgImage:
            return tr("Insert a picture of your signature");
        case ElementKind::AcceptMark:
            return tr("A check mark");
        case ElementKind::RejectMark:
            return tr("A cross");
        case ElementKind::Dot:
            return tr("A filled dot");
        case ElementKind::Rectangle:
            return tr("Sharp corners");
        case ElementKind::RoundedRectangle:
            return tr("Round corners");
        case ElementKind::Line:
            return tr("In any direction");
        case ElementKind::HorizontalLine:
            return tr("Exactly level");
        case ElementKind::VerticalLine:
            return tr("Exactly upright");
    }
    return QString();
}

QString PDFAppToolDrawingPart::getKindIconName(ElementKind kind)
{
    switch (kind)
    {
        case ElementKind::Text:
            return QStringLiteral("type");
        case ElementKind::FreehandCurve:
            return QStringLiteral("scribble");
        case ElementKind::SvgImage:
            return QStringLiteral("image");
        case ElementKind::AcceptMark:
            return QStringLiteral("check");
        case ElementKind::RejectMark:
            return QStringLiteral("x");
        case ElementKind::Dot:
            return QStringLiteral("dot");
        case ElementKind::Rectangle:
            return QStringLiteral("square");
        case ElementKind::RoundedRectangle:
            return QStringLiteral("square-round");
        case ElementKind::Line:
            return QStringLiteral("line");
        case ElementKind::HorizontalLine:
            return QStringLiteral("line-h");
        case ElementKind::VerticalLine:
            return QStringLiteral("line-v");
    }
    return QString();
}

QByteArray PDFAppToolDrawingPart::getMarkSvg(bool isAccept)
{
    // The marks of the scene (the core writes its own, PDFPageDrawing::Kind::AcceptMark / RejectMark)
    if (isAccept)
    {
        return QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\">"
                                 "<polyline points=\"4,12.5 9.5,18 20,6\" fill=\"none\" stroke=\"#000000\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>");
    }
    return QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\">"
                             "<path d=\"M5,5 L19,19 M19,5 L5,19\" fill=\"none\" stroke=\"#000000\" stroke-width=\"2.5\" stroke-linecap=\"round\"/></svg>");
}

void PDFAppToolDrawingPart::startElement(ElementKind kind)
{
    if (!m_isActionsEnabled || !m_isInputEnabled)
    {
        return;
    }
    if (kind == ElementKind::SvgImage)
    {
        // The file dialog of the image; its answer comes later (onSvgFileChosen)
        chooseSvgImage();
        return;
    }

    auto it = m_tools.find(kind);
    PDFCreatePCElementTool* tool = it != m_tools.end() ? it->second.data() : nullptr;
    if (!tool || !m_toolManager)
    {
        return;
    }
    updateToolStyles();
    m_toolManager->setActiveTool(tool);
}

std::optional<PDFAppToolDrawingPart::ElementKind> PDFAppToolDrawingPart::getActiveElementKind() const
{
    for (const auto& item : m_tools)
    {
        if (item.second && item.second->isActive())
        {
            return item.first;
        }
    }
    return std::nullopt;
}

bool PDFAppToolDrawingPart::endActiveElement()
{
    if (!m_toolManager || !getActiveElementKind())
    {
        return false;
    }

    // The last call: the slots of the tool manager run
    m_toolManager->setActiveTool(nullptr);
    return true;
}

bool PDFAppToolDrawingPart::clearSelection()
{
    if (getSelectedElements().empty())
    {
        return false;
    }

    // The last call: the scene emits its selection (the part queues its update)
    selectElements(std::set<PDFInteger>());
    return true;
}

QString PDFAppToolDrawingPart::getEscapeHint() const
{
    if (const std::optional<ElementKind> kind = getActiveElementKind())
    {
        return tr("Esc ends %1").arg(getKindTitle(*kind));
    }
    return getSelectedElements().empty() ? QString() : tr("Esc clears the selection");
}

void PDFAppToolDrawingPart::chooseSvgImage()
{
    // Plan 7.2: the file only from the file dialog of the user. A0: the dialog answers onSvgFileChosen (the request is
    // a child of the part: a part gone meanwhile gets nothing); one file dialog at a time
    if (m_fileRequest && m_fileRequest->isPending())
    {
        return;
    }
    QWidget* parent = m_widget ? m_widget->window() : nullptr;
    m_fileRequest = PDFAppPanelFileDialogs::requestOpenFileName(parent, tr("Insert SVG Image"), QString(), tr("SVG images (*.svg)"), this,
                                                                &PDFAppToolDrawingPart::onSvgFileChosen);
}

void PDFAppToolDrawingPart::onSvgFileChosen(const QString& filePath)
{
    // The state again: the drawing still takes the input (a run started meanwhile takes no image). A file, which
    // cannot be used, says why (review RS6APP: the reason was dropped) - the message is the last call; a canceled
    // dialog gives the button its state back.
    QString errorMessage;
    const bool isStarted = !filePath.isEmpty() && m_isActionsEnabled && m_isInputEnabled && useSvgFile(filePath, &errorMessage);
    if (isStarted || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }
    updateActions();
    if (!errorMessage.isEmpty())
    {
        showSvgError(errorMessage);
    }
}

bool PDFAppToolDrawingPart::useSvgFile(const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    const QFileInfo info(filePath);
    if (info.size() > MAXIMUM_SVG_SIZE)
    {
        if (errorMessage)
        {
            *errorMessage = tr("The SVG image is larger than 4 MB.");
        }
        return false;
    }
    if (!file.open(QFile::ReadOnly))
    {
        if (errorMessage)
        {
            *errorMessage = tr("The SVG image cannot be read.");
        }
        return false;
    }
    const QByteArray data = file.read(MAXIMUM_SVG_SIZE + 1);
    file.close();
    return startSvgImage(data, errorMessage);
}

bool PDFAppToolDrawingPart::startSvgImage(const QByteArray& data, QString* errorMessage)
{
    if (data.isEmpty() || data.size() > MAXIMUM_SVG_SIZE)
    {
        if (errorMessage)
        {
            *errorMessage = data.isEmpty() ? tr("The SVG image is empty.") : tr("The SVG image is larger than 4 MB.");
        }
        return false;
    }

    // The rules of the core (plan 7.2: no external reference, a valid image): a drawing of the one
    // image on the first page is checked against the document of the view
    PDFPageDrawing::Element element;
    element.kind = PDFPageDrawing::Kind::SvgImage;
    element.pageIndex = 0;
    element.rectangle = QRectF(0, 0, 100, 50);
    element.svgData = data;
    const PDFPageDrawing check({ element });
    QString checkError;
    const PDFDocument* document = m_proxy ? m_proxy->getDocument() : nullptr;
    if (!check.isValid(document, &checkError))
    {
        if (errorMessage)
        {
            *errorMessage = checkError;
        }
        return false;
    }

    m_svgData = data;
    if (m_proxy && m_toolManager)
    {
        auto it = m_tools.find(ElementKind::SvgImage);
        if (it != m_tools.end() && it->second)
        {
            if (m_toolManager->getActiveTool() == it->second.data())
            {
                m_toolManager->setActiveTool(nullptr);
            }
            disconnect(it->second.data(), nullptr, this, nullptr);
            it->second->setParent(nullptr);
            it->second->deleteLater();
        }
        PDFCreatePCElementImageTool* tool = new PDFCreatePCElementImageTool(m_proxy.data(), m_scene, nullptr, m_svgData, false, this);
        m_tools[ElementKind::SvgImage] = tool;
        m_toolManager->addTool(tool);
        connect(tool, &PDFWidgetTool::toolActivityChanged, this, &PDFAppToolDrawingPart::onToolActivityChanged);
        m_toolManager->setActiveTool(tool);
    }
    return true;
}

void PDFAppToolDrawingPart::arrange(ArrangeOperation operation)
{
    // The order of ArrangeOperation is PDFPageContentElementManipulator::Operation
    if (m_scene && !m_scene->getSelectedElementIds().empty())
    {
        m_scene->performOperation(int(operation));
    }
}

void PDFAppToolDrawingPart::selectElements(const std::set<PDFInteger>& ids)
{
    if (m_scene)
    {
        m_scene->setSelectedElementIds(ids);
    }
}

std::set<PDFInteger> PDFAppToolDrawingPart::getSelectedElements() const
{
    return m_scene ? m_scene->getSelectedElementIds() : std::set<PDFInteger>();
}

void PDFAppToolDrawingPart::editText(PDFInteger id)
{
    const PDFPageContentElement* element = m_scene ? m_scene->getElementById(id) : nullptr;
    const PDFPageContentElementTextBox* text = element ? element->asElementTextBox() : nullptr;
    if (!text || m_editDialog)
    {
        return;
    }

    // R6-24 (the old Edit Item): a form of one field; new + QPointer, it deletes itself
    QWidget* parent = m_widget ? m_widget->window() : nullptr;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Question, tr("Edit the text label"), QString(), QDialogButtonBox::NoButton, parent);
    dialog->setObjectName(QStringLiteral("drawingEditTextDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setBadgeVisible(false);
    PDFLineEdit* field = new PDFLineEdit(text->getText(), dialog);
    field->setObjectName(QStringLiteral("drawingEditTextField"));
    field->setMaxLength(PDFPageDrawing::MAXIMUM_TEXT_LENGTH);
    dialog->addContent(field);
    QPushButton* cancelButton = dialog->addActionButton(tr("Cancel"), PDFMessageBox::ActionPosition::Trailing, false);
    QPushButton* applyButton = dialog->addActionButton(tr("Apply"), PDFMessageBox::ActionPosition::Trailing, true);
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, dialog, &QDialog::accept);
    dialog->setProperty(PDFAppToolDrawingPartHelper::ELEMENT_PROPERTY, qint64(id));
    connect(dialog, &QDialog::accepted, this, &PDFAppToolDrawingPart::onEditTextAccepted);
    m_editDialog = dialog;
    PDFDeferredDeletion::setOwner(dialog, this);
    dialog->open();
}

void PDFAppToolDrawingPart::onEditTextAccepted()
{
    PDFMessageBox* dialog = qobject_cast<PDFMessageBox*>(sender());
    const PDFLineEdit* field = dialog ? dialog->findChild<PDFLineEdit*>(QStringLiteral("drawingEditTextField")) : nullptr;
    if (!field)
    {
        return;
    }
    const PDFInteger id = dialog->property(PDFAppToolDrawingPartHelper::ELEMENT_PROPERTY).toLongLong();
    setElementText(id, field->text());
}

bool PDFAppToolDrawingPart::setElementText(PDFInteger id, const QString& text)
{
    const PDFPageContentElement* element = m_scene ? m_scene->getElementById(id) : nullptr;
    const PDFPageContentElementTextBox* textBox = element ? element->asElementTextBox() : nullptr;
    if (!textBox)
    {
        return false;
    }
    PDFPageContentElementTextBox* changed = textBox->clone();
    changed->setText(text.left(PDFPageDrawing::MAXIMUM_TEXT_LENGTH));
    m_scene->replaceElement(changed);
    return true;
}

PDFPageDrawing::Style PDFAppToolDrawingPart::getShownStyle() const
{
    const std::set<PDFInteger> selected = getSelectedElements();
    if (!selected.empty() && m_scene)
    {
        if (const PDFPageContentElement* element = m_scene->getElementById(*selected.begin()))
        {
            return PDFAppToolDrawingPartHelper::getStyle(element);
        }
    }
    return m_nextStyle;
}

bool PDFAppToolDrawingPart::isTextStyleShown() const
{
    const std::set<PDFInteger> selected = getSelectedElements();
    if (!selected.empty())
    {
        return getElementKind(*selected.begin()) == ElementKind::Text;
    }
    const std::optional<ElementKind> active = getActiveElementKind();
    if (active)
    {
        return *active == ElementKind::Text;
    }

    // Sign (434:5697): the style of the text label, the name of the signer; Sign by Hand (435:8874):
    // the pen of the next stroke
    return m_layout == Layout::SignatureAppearance;
}

void PDFAppToolDrawingPart::setPenColor(const QColor& color)
{
    m_nextStyle.penColor = color;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::PenColor, color);
}

void PDFAppToolDrawingPart::setPenWidth(double width)
{
    m_nextStyle.penWidth = width;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::PenWidth, width);
}

void PDFAppToolDrawingPart::setFillColor(const QColor& color)
{
    m_nextStyle.fillColor = color;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::FillColor, color);
}

void PDFAppToolDrawingPart::setFontFamily(const QString& family)
{
    m_nextStyle.fontFamily = family;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::FontFamily, family);
}

void PDFAppToolDrawingPart::setFontSize(double size)
{
    m_nextStyle.fontSize = size;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::FontSize, size);
}

void PDFAppToolDrawingPart::setTextColor(const QColor& color)
{
    // The text of a label is drawn by the pen (PDFPageContentElementTextBox::drawPage)
    setPenColor(color);
}

void PDFAppToolDrawingPart::setAlignment(PDFPageDrawing::Alignment alignment)
{
    m_nextStyle.alignment = alignment;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::Alignment, int(alignment));
}

void PDFAppToolDrawingPart::setAngle(double angle)
{
    m_nextStyle.angle = angle;
    updateToolStyles();
    changeSelectedStyle(PDFAppToolDrawingPartHelper::Angle, angle);
}

void PDFAppToolDrawingPart::changeSelectedStyle(int property, const QVariant& value)
{
    using Helper = PDFAppToolDrawingPartHelper;

    const std::set<PDFInteger> selected = getSelectedElements();
    if (selected.empty() || !m_scene)
    {
        // Only the next element: the panel shows it
        updateParts();
        return;
    }

    std::vector<PDFPageContentElement*> replacements;
    for (const PDFInteger id : selected)
    {
        const PDFPageContentElement* element = m_scene->getElementById(id);
        if (!element)
        {
            continue;
        }
        PDFPageContentElement* changed = element->clone();
        if (PDFPageContentStyledElement* styled = dynamic_cast<PDFPageContentStyledElement*>(changed))
        {
            QPen pen = styled->getPen();
            QBrush brush = styled->getBrush();
            switch (property)
            {
                case Helper::PenColor:
                    pen.setColor(value.value<QColor>());
                    break;
                case Helper::PenWidth:
                    pen.setWidthF(value.toDouble());
                    pen.setStyle(Qt::SolidLine);
                    break;
                case Helper::FillColor:
                    brush = value.value<QColor>().isValid() ? QBrush(value.value<QColor>()) : QBrush(Qt::NoBrush);
                    break;
                default:
                    break;
            }
            styled->setPen(pen);
            styled->setBrush(brush);
        }
        if (PDFPageContentElementTextBox* text = dynamic_cast<PDFPageContentElementTextBox*>(changed))
        {
            QFont font = text->getFont();
            switch (property)
            {
                case Helper::FontFamily:
                    font.setFamily(value.toString());
                    break;
                case Helper::FontSize:
                    font.setPointSizeF(qBound(Helper::MINIMUM_FONT_SIZE, value.toDouble(), Helper::MAXIMUM_FONT_SIZE));
                    break;
                case Helper::Alignment:
                    text->setAlignment(Helper::createAlignment(PDFPageDrawing::Alignment(value.toInt())));
                    break;
                case Helper::Angle:
                    text->setAngle(value.toDouble());
                    break;
                default:
                    break;
            }
            text->setFont(font);
        }
        replacements.push_back(changed);
    }

    // One step: the scene reports one change (onSceneChanged writes the model)
    m_scene->replaceElements(std::move(replacements));
    m_scene->setSelectedElementIds(selected);
}

void PDFAppToolDrawingPart::updateToolStyles()
{
    using Helper = PDFAppToolDrawingPartHelper;

    const QPen pen = Helper::createPen(m_nextStyle);
    const QBrush brush = Helper::createBrush(m_nextStyle);
    for (const auto& item : m_tools)
    {
        PDFCreatePCElementTool* tool = item.second.data();
        if (!tool)
        {
            continue;
        }
        tool->setPen(pen);
        tool->setBrush(item.first == ElementKind::Dot ? QBrush(m_nextStyle.penColor) : brush);
        tool->setFont(Helper::createFont(m_nextStyle));
        tool->setAlignment(Helper::createAlignment(m_nextStyle.alignment));
        tool->setTextAngle(m_nextStyle.angle);
    }
}

void PDFAppToolDrawingPart::setActionsEnabled(bool isEnabled)
{
    m_isActionsEnabled = isEnabled;
    if (!isEnabled && m_toolManager && getActiveElementKind())
    {
        m_toolManager->setActiveTool(nullptr);
    }
    updateActions();
}

void PDFAppToolDrawingPart::setInputEnabled(bool isEnabled)
{
    if (m_isInputEnabled == isEnabled)
    {
        return;
    }

    // The scene stays registered for drawing (the proxy); only its input goes and comes back
    m_isInputEnabled = isEnabled;
    if (m_widget && m_scene)
    {
        if (isEnabled)
        {
            m_widget->addInputInterface(m_scene);
        }
        else
        {
            m_widget->removeInputInterface(m_scene);
        }
    }

    // The tool manager tells the view (its slots run; a part closed by them is retired, deleted later): only the own
    // actions follow
    if (!isEnabled && m_toolManager && getActiveElementKind())
    {
        m_toolManager->setActiveTool(nullptr);
    }
    updateActions();
}

void PDFAppToolDrawingPart::showSvgError(const QString& errorMessage)
{
    QWidget* parent = m_widget ? m_widget->window() : nullptr;
    PDFMessageBox* dialog = new PDFMessageBox(PDFThemeIcons::Badge::Critical, tr("The SVG image cannot be used"), errorMessage, QDialogButtonBox::Ok, parent);
    dialog->setObjectName(QStringLiteral("drawingSvgErrorDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    PDFDeferredDeletion::setOwner(dialog, this);
    dialog->open();
}

void PDFAppToolDrawingPart::updateActions()
{
    const bool hasElements = !isEmpty();
    const bool hasSelection = !getSelectedElements().empty();
    const std::optional<ElementKind> active = getActiveElementKind();

    const bool isEnabled = m_isActionsEnabled && m_isInputEnabled;
    if (m_drawPanel && m_drawPanel->getButton())
    {
        m_drawPanel->getButton()->setEnabled(isEnabled);
    }
    if (m_shapesPanel && m_shapesPanel->getButton())
    {
        m_shapesPanel->getButton()->setEnabled(isEnabled);
    }
    if (m_arrangePanel && m_arrangePanel->getButton())
    {
        // 434:6889: Arrange and Clear All are Disabled without an element
        m_arrangePanel->getButton()->setEnabled(isEnabled && hasElements);
    }
    for (const auto& item : m_arrangeRows)
    {
        if (item.second)
        {
            item.second->setEnabled(hasSelection);
        }
    }
    if (m_clearButton)
    {
        m_clearButton->setEnabled(isEnabled && hasElements);
    }
    for (const auto& item : m_elementButtons)
    {
        if (item.second)
        {
            item.second->setEnabled(isEnabled);
            item.second->setChecked(active && *active == item.first);
        }
    }
}

void PDFAppToolDrawingPart::updateParts()
{
    updateActions();
    if (m_elementList)
    {
        m_elementList->rebuild();
    }
    if (m_stylePanel)
    {
        m_stylePanel->refresh();
    }
}

QToolButton* PDFAppToolDrawingPart::getElementButton(ElementKind kind) const
{
    auto it = m_elementButtons.find(kind);
    return it != m_elementButtons.end() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppToolDrawingPart::getDrawRow(ElementKind kind) const
{
    auto it = m_drawRows.find(kind);
    return it != m_drawRows.end() ? it->second.data() : nullptr;
}

PDFTileRow* PDFAppToolDrawingPart::getArrangeRow(ArrangeOperation operation) const
{
    auto it = m_arrangeRows.find(operation);
    return it != m_arrangeRows.end() ? it->second.data() : nullptr;
}

bool PDFAppToolDrawingPart::canUndo() const
{
    return m_model && m_model->canUndo();
}

bool PDFAppToolDrawingPart::canRedo() const
{
    return m_model && m_model->canRedo();
}

void PDFAppToolDrawingPart::undo()
{
    if (m_model)
    {
        m_model->undo();
    }
}

void PDFAppToolDrawingPart::redo()
{
    if (m_model)
    {
        m_model->redo();
    }
}

void PDFAppToolDrawingPart::onSceneChanged(bool graphicsOnly)
{
    if (m_isDetaching)
    {
        return;
    }

    if (m_widget)
    {
        if (IDrawWidget* drawWidget = m_widget->getDrawWidget())
        {
            drawWidget->getWidget()->update();
        }
    }
    if (graphicsOnly || m_isApplying)
    {
        return;
    }

    // The scene reports a change from inside of a creation tool or its manipulator (Pdf4QtLibWidgets
    // goes on with its members after the emission): the model, the panels and the GUI follow it from
    // the event loop, never inside of the call of the engine (lifetime, P4)
    if (!m_isSceneChangePending)
    {
        m_isSceneChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppToolDrawingPart::onSceneChangeQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolDrawingPart::onSceneChangeQueued()
{
    m_isSceneChangePending = false;
    if (m_isDetaching || !m_scene)
    {
        return;
    }

    // One step of the model per change of the user (the model emits changed, the GUI follows it; a part retired by
    // it tells nothing more)
    const PDFPageDrawing drawing = toDrawing();
    if (drawing.isEmpty() && m_lastWrittenDrawing.isEmpty())
    {
        // Nothing has changed for the drawing (an empty scene stays empty)
        updateParts();
        return;
    }
    if (m_model && !m_parameter.isEmpty())
    {
        // K6.10 (review RS6APP): a change without a change of the elements (a style the element has
        // already, an Arrange, which moves nothing) is no step of Undo
        if (drawing.getElements() == m_lastWrittenDrawing.getElements())
        {
            updateParts();
            return;
        }
        m_lastWrittenDrawing = drawing;
        m_model->setValue(m_parameter, QVariant::fromValue(drawing));
        if (PDFDeferredDeletion::isRetired(this))
        {
            return;
        }
    }
    updateParts();
    Q_EMIT drawingChanged();
}

void PDFAppToolDrawingPart::onSceneSelectionChanged()
{
    // From inside of the input of the scene or of a tool: the panels follow from the event loop
    queueSelectionUpdate();
}

void PDFAppToolDrawingPart::queueSelectionUpdate()
{
    if (m_isDetaching || m_isSelectionUpdatePending)
    {
        return;
    }
    m_isSelectionUpdatePending = true;
    QMetaObject::invokeMethod(this, &PDFAppToolDrawingPart::onSelectionUpdateQueued, Qt::QueuedConnection);
}

void PDFAppToolDrawingPart::onSelectionUpdateQueued()
{
    m_isSelectionUpdatePending = false;
    if (m_isDetaching)
    {
        return;
    }
    updateActions();
    if (m_elementList)
    {
        m_elementList->updateSelection();
    }
    if (m_stylePanel)
    {
        m_stylePanel->refresh();
    }
    Q_EMIT selectionChanged();
}

void PDFAppToolDrawingPart::onSceneEditRequest(const std::set<PDFInteger>& elements)
{
    // A double click on a text label on the page (R6-24)
    for (const PDFInteger id : elements)
    {
        if (getElementKind(id) == ElementKind::Text)
        {
            editText(id);
            return;
        }
    }
}

void PDFAppToolDrawingPart::onToolActivityChanged()
{
    // Emitted by PDFWidgetTool::setActive inside of the call of the tool (onPointPicked)
    queueSelectionUpdate();
}

void PDFAppToolDrawingPart::onModelChanged(const QString& name)
{
    if (name != m_parameter || !m_model)
    {
        return;
    }

    // Undo and Redo of the open tool: the scene takes the drawing of the step (not its own value)
    const PDFPageDrawing drawing = m_model->getValue(m_parameter).value<PDFPageDrawing>();
    if (drawing == m_lastWrittenDrawing)
    {
        return;
    }
    m_lastWrittenDrawing = drawing;
    applyDrawing(drawing);
    Q_EMIT drawingChanged();
}

void PDFAppToolDrawingPart::onModelUndoStateChanged()
{
    Q_EMIT undoStateChanged();
}

void PDFAppToolDrawingPart::onDrawRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }
    const ElementKind kind = ElementKind(row->property(PDFAppToolDrawingPartHelper::KIND_PROPERTY).toInt());

    // The panel closes before the tool or the file dialog takes the input
    for (PDFAppToolPanelPopup* panel : { m_drawPanel.data(), m_shapesPanel.data() })
    {
        if (panel && panel->isVisible())
        {
            panel->close();
        }
    }
    startElement(kind);
}

void PDFAppToolDrawingPart::onArrangeRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }
    const ArrangeOperation operation = ArrangeOperation(row->property(PDFAppToolDrawingPartHelper::OPERATION_PROPERTY).toInt());
    if (m_arrangePanel && m_arrangePanel->isVisible())
    {
        m_arrangePanel->close();
    }
    arrange(operation);
}

void PDFAppToolDrawingPart::onElementButtonClicked()
{
    QObject* button = sender();
    if (!button)
    {
        return;
    }
    const ElementKind kind = ElementKind(button->property(PDFAppToolDrawingPartHelper::KIND_PROPERTY).toInt());
    if (getActiveElementKind() == kind && m_toolManager)
    {
        // A second click ends the tool
        m_toolManager->setActiveTool(nullptr);
        return;
    }
    startElement(kind);
}

void PDFAppToolDrawingPart::onClearClicked()
{
    // The GUI asks first (the last call)
    Q_EMIT clearRequested();
}

}   // namespace pdf

#include "moc_pdfapptooldrawingpart.cpp"
