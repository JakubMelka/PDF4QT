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

#include "pdfappannotationpropertiesdialog.h"
#include "pdfappcolorpicker.h"
#include "pdfappannotationcontroller.h"
#include "pdfappannotationinfo.h"
#include "pdfappannotationoperations.h"
#include "pdfappdocument.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"

#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetsnapshot.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace pdf
{

/// Spin box of a length of the properties dialog without trailing zeros: "2", "10.5"
/// (the Spin box of 1022:20807 and 1027:21823 shows "2 pt", "11 pt"; "10.0" does not fit
/// into the field of 88 px of the font size)
class PDFAppPropertiesLengthSpinBox : public PDFDoubleSpinBox
{
public:
    explicit PDFAppPropertiesLengthSpinBox(QWidget* parent) :
        PDFDoubleSpinBox(parent)
    {

    }

    virtual QString textFromValue(double value) const override
    {
        QString text = locale().toString(value, 'f', decimals());
        const QString decimalPoint = locale().decimalPoint();
        if (decimals() > 0 && text.contains(decimalPoint))
        {
            while (text.endsWith(QLatin1Char('0')))
            {
                text.chop(1);
            }
            if (text.endsWith(decimalPoint))
            {
                text.chop(decimalPoint.size());
            }
        }
        return text;
    }
};

/// Tables and conversions of the properties dialog
class PDFAppAnnotationPropertiesDialogHelper
{
public:
    PDFAppAnnotationPropertiesDialogHelper() = delete;

    /// Millimeters of a length in points
    static constexpr PDFReal MM_PER_POINT = 25.4 / 72.0;

    /// Size of an icon choice of a sticky note (1027:21900)
    static constexpr int ICON_BUTTON = 36;

    /// Distance of the card from the edges of the window; the smallest scrolled body
    static constexpr int WINDOW_MARGIN = 16;
    static constexpr int MINIMUM_BODY_HEIGHT = 120;

    /// Destination types in the order of the Type combo box (O20)
    static constexpr DestinationType DESTINATION_TYPES[] = { DestinationType::Fit, DestinationType::FitH, DestinationType::FitV, DestinationType::FitR,
                                                            DestinationType::XYZ, DestinationType::FitB, DestinationType::FitBH, DestinationType::FitBV };

    /// Line endings in the order of the combo boxes
    static constexpr AnnotationLineEnding LINE_ENDINGS[] = { AnnotationLineEnding::None, AnnotationLineEnding::Square, AnnotationLineEnding::Circle,
                                                            AnnotationLineEnding::Diamond, AnnotationLineEnding::OpenArrow, AnnotationLineEnding::ClosedArrow,
                                                            AnnotationLineEnding::Butt, AnnotationLineEnding::ROpenArrow, AnnotationLineEnding::RClosedArrow,
                                                            AnnotationLineEnding::Slash };

    /// Border styles in the order of the combo box
    static constexpr AnnotationBorderStyle BORDER_STYLES[] = { AnnotationBorderStyle::Solid, AnnotationBorderStyle::Dashed, AnnotationBorderStyle::Beveled,
                                                              AnnotationBorderStyle::Inset, AnnotationBorderStyle::Underline };

    /// Icon of the design of a sticky note icon (the icons of Insert › Text and notes)
    static QString getStickyIconName(const QByteArray& name)
    {
        static const std::pair<const char*, const char*> icons[] = {
            { "Comment", "message-square" }, { "Note", "note-note" }, { "Help", "note-help" }, { "Key", "note-key" },
            { "Insert", "note-insert" }, { "Paragraph", "note-paragraph" }, { "NewParagraph", "note-new-paragraph" }
        };
        for (const auto& icon : icons)
        {
            if (name == icon.first)
            {
                return QString::fromLatin1(icon.second);
            }
        }
        return QStringLiteral("message-square");
    }

    /// Name of a sticky note icon, of a stamp, of a file attachment icon
    static QString getIconText(const QByteArray& name)
    {
        static const std::pair<const char*, const char*> names[] = {
            { "Comment", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Comment") },
            { "Note", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Note") },
            { "Help", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Help") },
            { "Key", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Key") },
            { "Insert", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Insert") },
            { "Paragraph", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Paragraph") },
            { "NewParagraph", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "New paragraph") },
            { "PushPin", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Push pin") },
            { "Paperclip", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Paperclip") },
            { "Graph", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Graph") },
            { "Tag", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Tag") },
            { "Speaker", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Speaker") },
            { "Mic", QT_TRANSLATE_NOOP("pdf::PDFAppAnnotationPropertiesDialog", "Microphone") }
        };
        for (const auto& item : names)
        {
            if (name == item.first)
            {
                return PDFAppAnnotationPropertiesDialog::tr(item.second);
            }
        }

        // A stamp: its readable name
        PDFAppAnnotationInfo info;
        info.type = AnnotationType::Stamp;
        info.iconName = name;
        const QString displayName = PDFAppAnnotationInfo::getDisplayName(info);
        const qsizetype start = displayName.indexOf(QChar(u'“'));
        return start >= 0 ? displayName.mid(start + 1).chopped(1) : QString::fromLatin1(name);
    }

    /// Title of the dialog ("Properties of the rectangle")
    static QString getTitle(PDFAppAnnotationController* controller, PDFObjectReference annotation)
    {
        const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
        return PDFAppAnnotationInfo::getPropertiesTitle(info.type);
    }

    /// Line below the title: "Page 308 · link to page 309 · created by Jakub Melka on Oct 19, 2026 14:05 · modified Today 14:12"
    static QString getSubtitle(PDFAppAnnotationController* controller, PDFObjectReference annotation)
    {
        const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
        const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
        const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument.data(), annotation);
        const PDFAnnotationProperties properties = PDFAnnotationPropertyEditor::read(pdfDocument ? &pdfDocument->getStorage() : nullptr, annotation);

        QStringList parts;
        if (info.pageIndex >= 0)
        {
            parts << PDFAppAnnotationPropertiesDialog::tr("Page %1").arg(info.pageIndex + 1);
        }
        if (properties.type == AnnotationType::Link)
        {
            const PDFDestination& destination = properties.destination;
            if (properties.isDestinationEditable && destination.isValid() && !destination.isNamedDestination() && pdfDocument)
            {
                // A target, which is not a page of the document (a hand-written file), has no number
                const PDFInteger target = destination.getPageReference().isValid() ? PDFInteger(pdfDocument->getCatalog()->getPageIndexFromPageReference(destination.getPageReference()))
                                                                                   : destination.getPageIndex();
                if (target >= 0 && target < PDFInteger(pdfDocument->getCatalog()->getPageCount()))
                {
                    parts << PDFAppAnnotationPropertiesDialog::tr("link to page %1").arg(target + 1);
                }
            }
            else if (!properties.uri.isEmpty())
            {
                parts << PDFAppAnnotationPropertiesDialog::tr("web link");
            }
        }

        const QString author = PDFAppAnnotationInfo::getAuthorText(info.author);
        if (info.created.isValid())
        {
            parts << PDFAppAnnotationPropertiesDialog::tr("created by %1 on %2").arg(author, PDFAppAnnotationInfo::formatDateTime(info.created));
        }
        else if (!info.author.isEmpty())
        {
            parts << PDFAppAnnotationPropertiesDialog::tr("created by %1").arg(author);
        }
        if (info.modified.isValid())
        {
            parts << PDFAppAnnotationPropertiesDialog::tr("modified %1").arg(PDFAppAnnotationInfo::formatDateTime(info.modified));
        }
        return parts.join(QStringLiteral(" \u00B7 "));
    }

    /// A control shows its value only (read only) or it can be changed: a field
    /// keeps its text selectable, other controls are disabled
    static void setEditable(QWidget* widget, bool isEditable)
    {
        if (QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget))
        {
            lineEdit->setReadOnly(!isEditable);
        }
        else if (QPlainTextEdit* textEdit = qobject_cast<QPlainTextEdit*>(widget))
        {
            textEdit->setReadOnly(!isEditable);
        }
        else if (widget)
        {
            widget->setEnabled(isEditable);
        }
    }

    /// Value of a spin box: the value read from the document, while the spin box
    /// shows it (rounded to its decimals) - a value, which the spin box cannot show
    /// exactly (0.75 pt as 0.8 pt), is not a change of the user
    static PDFReal getValue(const PDFDoubleSpinBox* spinBox, PDFReal original)
    {
        const double factor = std::pow(10.0, qBound(0, spinBox->decimals(), 6));
        const double shown = qBound(spinBox->minimum(), std::round(original * factor) / factor, spinBox->maximum());
        return std::isfinite(original) && qFuzzyCompare(1.0 + spinBox->value(), 1.0 + shown) ? original : spinBox->value();
    }

    /// A length in millimeters
    static QString formatMillimeters(PDFReal points)
    {
        return std::isfinite(points) ? PDFAppLocale::getUiLocale().toString(points * MM_PER_POINT, 'f', 1) : QString();
    }

    /// The edited destination of the types, which need the coordinates
    static PDFDestination createDestination(DestinationType type, PDFObjectReference page, const PDFDestination& source)
    {
        switch (type)
        {
            case DestinationType::XYZ:
                return PDFDestination::createXYZ(page, source.getLeft(), source.getTop(), source.getZoom());
            case DestinationType::FitH:
                return PDFDestination::createFitH(page, source.getTop());
            case DestinationType::FitV:
                return PDFDestination::createFitV(page, source.getLeft());
            case DestinationType::FitR:
                return PDFDestination::createFitR(page, source.getLeft(), source.getTop(), source.getRight(), source.getBottom());
            case DestinationType::FitB:
                return PDFDestination::createFitB(page);
            case DestinationType::FitBH:
                return PDFDestination::createFitBH(page, source.getTop());
            case DestinationType::FitBV:
                return PDFDestination::createFitBV(page, source.getLeft());
            default:
                break;
        }
        return PDFDestination::createFit(page);
    }
};

PDFAppAnnotationPropertiesDialog::PDFAppAnnotationPropertiesDialog(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Neutral, PDFAppAnnotationPropertiesDialogHelper::getTitle(controller, annotation),
              PDFAppAnnotationPropertiesDialogHelper::getSubtitle(controller, annotation), QDialogButtonBox::NoButton, parent),
    m_controller(controller),
    m_annotation(annotation)
{
    setObjectName(QStringLiteral("annotationPropertiesDialog"));
    setCardWidth(CARD_WIDTH);

    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    m_original = PDFAnnotationPropertyEditor::read(pdfDocument ? &pdfDocument->getStorage() : nullptr, annotation);
    m_shown = m_original;
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(pdfDocument.data(), annotation);
    m_pageIndex = info.pageIndex;
    m_isReadOnly = !controller || !controller->isAnnotationEditingAllowed();
    m_isLocked = m_original.flags.testFlag(PDFAnnotation::Locked) || m_original.flags.testFlag(PDFAnnotation::ReadOnly);
    m_isContentsLocked = m_original.flags.testFlag(PDFAnnotation::LockedContents) || m_original.flags.testFlag(PDFAnnotation::ReadOnly);

    // A measurement line (the measure of the page, /IT LineDimension)
    if (pdfDocument && m_original.type == AnnotationType::Line)
    {
        const PDFDictionary* dictionary = pdfDocument->getDictionaryFromObject(pdfDocument->getObjectByReference(annotation));
        m_isMeasurement = dictionary && (dictionary->hasKey("Measure") || dictionary->hasKey("LL"));
    }

    // A named destination is shown resolved; it stays named, until the user changes it
    m_resolvedDestination = m_original.destination;
    if (m_resolvedDestination.isNamedDestination() && pdfDocument)
    {
        if (const PDFDestination* named = pdfDocument->getCatalog()->getNamedDestination(m_resolvedDestination.getName()))
        {
            m_resolvedDestination = *named;
        }
    }

    // head 1022:20736: badge 36 with the icon of the type, the title, "Page 308 · created by ..."
    QString iconName;
    PDFAccentHue hue = PDFAccentHue::Neutral;
    PDFAppAnnotationInfo::getBadge(m_original.type, false, &iconName, &hue);
    if (m_original.type == AnnotationType::Link && m_original.isDestinationEditable)
    {
        // head 1171:28262: a link to a page of the document (a web link keeps the icon link)
        iconName = QStringLiteral("link-page");
    }
    setBadgeIconName(iconName);
    setWindowTitle(PDFAppAnnotationInfo::getPropertiesTitle(m_original.type));

    createContent();
    createActions();
    fitScrollArea(this, m_scrollArea);
    updateEnabledState();
    updateDestinationTexts();
    updateApplyButton();

    connect(this, &QDialog::finished, this, &PDFAppAnnotationPropertiesDialog::onDialogFinished);
    if (controller)
    {
        // The tab (with its controller) deleted: the dialog closes
        connect(controller, &QObject::destroyed, this, &PDFAppAnnotationPropertiesDialog::onControllerDestroyed);
    }
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppAnnotationPropertiesDialog::onDocumentChanged);
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppAnnotationPropertiesDialog::onDocumentChanged);
    }
    if (controller)
    {
        connect(controller, &PDFAppAnnotationController::editingAllowedChanged, this, &PDFAppAnnotationPropertiesDialog::onEditingAllowedChanged);
    }
}

PDFAppAnnotationPropertiesDialog::~PDFAppAnnotationPropertiesDialog() = default;

PDFAppAnnotationPropertiesDialog* PDFAppAnnotationPropertiesDialog::open(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent)
{
    const PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document ? document->getDocument().data() : nullptr, annotation);
    if (!info.isValid())
    {
        return nullptr;
    }

    PDFAppAnnotationPropertiesDialog* dialog = new PDFAppAnnotationPropertiesDialog(controller, annotation, parent);
    PDFAppMainWindow::prepareDialog(dialog, parent);
    PDFAppAnnotationPropertiesDialog::fitScrollArea(dialog, dialog->m_scrollArea);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->BaseClass::open();
    return dialog;
}

PDFAppAnnotationController* PDFAppAnnotationPropertiesDialog::getController() const
{
    return m_controller.data();
}

QString PDFAppAnnotationPropertiesDialog::getDestinationTypeName(DestinationType type)
{
    switch (type)
    {
        case DestinationType::Fit:
            return tr("Whole page");
        case DestinationType::FitH:
            return tr("Page width");
        case DestinationType::FitV:
            return tr("Page height");
        case DestinationType::FitR:
            return tr("Area");
        case DestinationType::XYZ:
            return tr("Position");
        case DestinationType::FitB:
            return tr("Visible content");
        case DestinationType::FitBH:
            return tr("Visible content width");
        case DestinationType::FitBV:
            return tr("Visible content height");
        case DestinationType::Named:
            return tr("Named destination");
        default:
            break;
    }
    return tr("None");
}

QString PDFAppAnnotationPropertiesDialog::getDestinationTypeDescription(DestinationType type)
{
    switch (type)
    {
        case DestinationType::Fit:
            return tr("The whole page fits into the window.");
        case DestinationType::FitH:
            return tr("The width of the page fits into the window, the page opens at this height.");
        case DestinationType::FitV:
            return tr("The height of the page fits into the window, the page opens at this horizontal position.");
        case DestinationType::FitR:
            return tr("The area fits into the window.");
        case DestinationType::XYZ:
            return tr("The page opens with this place at the top-left corner of the window.");
        case DestinationType::FitB:
            return tr("The content of the page without its margins fits into the window.");
        case DestinationType::FitBH:
            return tr("The width of the content of the page fits into the window, the page opens at this height.");
        case DestinationType::FitBV:
            return tr("The height of the content of the page fits into the window, the page opens at this horizontal position.");
        default:
            break;
    }
    return QString();
}

QString PDFAppAnnotationPropertiesDialog::getLineEndingName(AnnotationLineEnding lineEnding)
{
    switch (lineEnding)
    {
        case AnnotationLineEnding::Square:
            return tr("Square");
        case AnnotationLineEnding::Circle:
            return tr("Circle");
        case AnnotationLineEnding::Diamond:
            return tr("Diamond");
        case AnnotationLineEnding::OpenArrow:
            return tr("Open arrow");
        case AnnotationLineEnding::ClosedArrow:
            return tr("Closed arrow");
        case AnnotationLineEnding::Butt:
            return tr("Butt");
        case AnnotationLineEnding::ROpenArrow:
            return tr("Reversed open arrow");
        case AnnotationLineEnding::RClosedArrow:
            return tr("Reversed closed arrow");
        case AnnotationLineEnding::Slash:
            return tr("Slash");
        default:
            break;
    }
    return tr("None");
}

QString PDFAppAnnotationPropertiesDialog::getBorderStyleName(AnnotationBorderStyle style)
{
    switch (style)
    {
        case AnnotationBorderStyle::Dashed:
            return tr("Dashed");
        case AnnotationBorderStyle::Beveled:
            return tr("Beveled");
        case AnnotationBorderStyle::Inset:
            return tr("Inset");
        case AnnotationBorderStyle::Underline:
            return tr("Underline");
        default:
            break;
    }
    return tr("Solid");
}

QString PDFAppAnnotationPropertiesDialog::getBlendModeName(const QByteArray& name)
{
    switch (PDFAnnotationPropertyEditor::getBlendMode(name))
    {
        case BlendMode::Normal:
            return tr("Normal");
        case BlendMode::Multiply:
            return tr("Multiply");
        case BlendMode::Screen:
            return tr("Screen");
        case BlendMode::Overlay:
            return tr("Overlay");
        case BlendMode::Darken:
            return tr("Darken");
        case BlendMode::Lighten:
            return tr("Lighten");
        case BlendMode::ColorDodge:
            return tr("Color dodge");
        case BlendMode::ColorBurn:
            return tr("Color burn");
        case BlendMode::HardLight:
            return tr("Hard light");
        case BlendMode::SoftLight:
            return tr("Soft light");
        case BlendMode::Difference:
            return tr("Difference");
        case BlendMode::Exclusion:
            return tr("Exclusion");
        case BlendMode::Hue:
            return tr("%1 (not displayed)").arg(tr("Hue"));
        case BlendMode::Saturation:
            return tr("%1 (not displayed)").arg(tr("Saturation"));
        case BlendMode::Color:
            return tr("%1 (not displayed)").arg(tr("Color"));
        case BlendMode::Luminosity:
            return tr("%1 (not displayed)").arg(tr("Luminosity"));
        case BlendMode::Compatible:
            return tr("%1 (not displayed)").arg(tr("Compatible"));
        default:
            break;
    }

    // An unknown name of the document (a PDF name: printable ASCII, bounded by the editor)
    return tr("%1 (not displayed)").arg(QString::fromLatin1(name));
}

void PDFAppAnnotationPropertiesDialog::createContent()
{
    // The sections scroll in a low window (the minimal window 720 x 480)
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("annotationPropertiesBody"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);

    QWidget* body = new QWidget(scrollArea);
    body->setAutoFillBackground(false);
    QVBoxLayout* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(SECTION_GAP);

    createCommentSection(layout);
    createDestinationSection(layout);
    createAppearanceSection(layout);
    createBehaviorSection(layout);

    scrollArea->setWidget(body);
    scrollArea->viewport()->setAutoFillBackground(false);
    body->setAutoFillBackground(false);
    addContent(scrollArea);
    m_scrollArea = scrollArea;
}

QVBoxLayout* PDFAppAnnotationPropertiesDialog::createSection(QVBoxLayout* parent, const QString& caption)
{
    // section 1022:20743: caption Semi Bold 13 text/primary, gap 10
    QVBoxLayout* section = new QVBoxLayout();
    section->setContentsMargins(0, 0, 0, 0);
    section->setSpacing(ITEM_GAP);
    PDFLabel* label = new PDFLabel(caption, PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    section->addWidget(label);
    parent->addLayout(section);
    return section;
}

QHBoxLayout* PDFAppAnnotationPropertiesDialog::createRow(QVBoxLayout* section, const QString& label, bool alignTop)
{
    // row 1022:20745: label Regular 12 text/secondary 104 wide, gap 12, the control
    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(ROW_GAP);
    PDFLabel* rowLabel = new PDFLabel(label, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    rowLabel->setFixedWidth(LABEL_WIDTH);
    rowLabel->setWordWrap(true);
    if (alignTop)
    {
        // A multi-line field: the label stands at the first line of the text
        // (1022:20755 - the field padding 9 + the stroke 1, the line 16 vs 15)
        rowLabel->setContentsMargins(0, MULTILINE_LABEL_OFFSET, 0, 0);
    }
    row->addWidget(rowLabel, 0, alignTop ? Qt::AlignTop : Qt::AlignVCenter);

    QHBoxLayout* control = new QHBoxLayout();
    control->setContentsMargins(0, 0, 0, 0);
    control->setSpacing(8);
    row->addLayout(control, 1);
    section->addLayout(row);
    return control;
}

PDFLabel* PDFAppAnnotationPropertiesDialog::createHint(const QString& text)
{
    // hint 1023:20681: Regular 11 text/muted
    PDFLabel* hint = new PDFLabel(text, PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, this);
    hint->setWordWrap(true);
    return hint;
}

PDFColorSwatchRow* PDFAppAnnotationPropertiesDialog::createSwatchRow(const QString& objectName, bool hasNoColor, bool hasWhite, const QColor& color)
{
    PDFColorSwatchRow* row = new PDFColorSwatchRow(this);
    row->setObjectName(objectName);
    PDFColorSwatchRow::Options options = PDFColorSwatchRow::Option::More;
    options.setFlag(PDFColorSwatchRow::Option::NoColor, hasNoColor);
    options.setFlag(PDFColorSwatchRow::Option::White, hasWhite);
    row->setOptions(options);
    row->setColor(color);
    PDFAppColorPicker::attach(row);
    return row;
}

PDFComboBox* PDFAppAnnotationPropertiesDialog::createLineEndingComboBox(const QString& objectName, AnnotationLineEnding lineEnding)
{
    PDFComboBox* comboBox = new PDFComboBox(this);
    comboBox->setObjectName(objectName);
    for (const AnnotationLineEnding item : PDFAppAnnotationPropertiesDialogHelper::LINE_ENDINGS)
    {
        comboBox->addItem(getLineEndingName(item), int(item));
    }
    comboBox->setCurrentIndex(qMax(0, comboBox->findData(int(lineEnding))));
    return comboBox;
}

void PDFAppAnnotationPropertiesDialog::createCommentSection(QVBoxLayout* layout)
{
    using F = PDFAnnotationProperties;

    const F::Fields fields = m_original.supportedFields;
    const bool isLink = m_original.type == AnnotationType::Link;
    QVBoxLayout* section = createSection(layout, isLink ? tr("Description") : tr("Comment"));

    if (fields.testFlag(F::Author))
    {
        m_authorField = new PDFLineEdit(m_shown.author, this);
        m_authorField->setObjectName(QStringLiteral("propertiesAuthor"));
        m_authorField->setMaxLength(int(F::MAXIMUM_TITLE_LENGTH));
        m_authorField->setAccessibleName(tr("Author"));
        createRow(section, tr("Author"))->addWidget(m_authorField, 1);
    }
    if (fields.testFlag(F::Subject))
    {
        m_subjectField = new PDFLineEdit(m_shown.subject, this);
        m_subjectField->setObjectName(QStringLiteral("propertiesSubject"));
        m_subjectField->setMaxLength(int(F::MAXIMUM_TITLE_LENGTH));
        m_subjectField->setAccessibleName(tr("Subject"));
        createRow(section, tr("Subject"))->addWidget(m_subjectField, 1);
    }

    // The text (Contents); of a link its description for screen readers (owner, 2026-10-24)
    m_textEdit = new PDFPlainTextEdit(m_shown.contents, this);
    m_textEdit->setObjectName(QStringLiteral("propertiesText"));
    m_textEdit->setFixedHeight(TEXT_HEIGHT);
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->setAccessibleName(tr("Text"));
    if (isLink)
    {
        m_textEdit->setPlaceholderText(tr("Describe the link for screen readers"));
    }
    createRow(section, tr("Text"), true)->addWidget(m_textEdit, 1);
    if (isLink)
    {
        QHBoxLayout* hintRow = createRow(section, QString());
        hintRow->addWidget(createHint(tr("Saved as the alternate description of the link.")), 1);
    }
    connect(m_textEdit.data(), &PDFPlainTextEdit::textChanged, this, &PDFAppAnnotationPropertiesDialog::updateApplyButton);
}

void PDFAppAnnotationPropertiesDialog::createDestinationSection(QVBoxLayout* layout)
{
    using H = PDFAppAnnotationPropertiesDialogHelper;

    if (m_original.type != AnnotationType::Link)
    {
        return;
    }

    QVBoxLayout* section = createSection(layout, tr("Destination"));
    if (!m_original.isDestinationEditable)
    {
        // A web link shows its address instead of the destination
        PDFLabel* address = new PDFLabel(m_original.uri.isEmpty() ? tr("The link runs an action, which is not a destination in this document.") : m_original.uri,
                                         PDFThemeFonts::Role::Small, PDFThemeColors::TextPrimary, this);
        address->setObjectName(QStringLiteral("propertiesAddress"));
        address->setTextFormat(Qt::PlainText);
        address->setWordWrap(true);
        address->setTextInteractionFlags(Qt::TextSelectableByMouse);
        createRow(section, m_original.uri.isEmpty() ? tr("Action") : tr("Address"))->addWidget(address, 1);
        return;
    }

    const PDFAppDocument* document = m_controller ? m_controller->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const PDFInteger pageCount = document ? document->getPageCount() : 0;
    PDFInteger targetPage = m_resolvedDestination.getPageIndex();
    if (pdfDocument && m_resolvedDestination.getPageReference().isValid())
    {
        targetPage = PDFInteger(pdfDocument->getCatalog()->getPageIndexFromPageReference(m_resolvedDestination.getPageReference()));
    }
    if (targetPage < 0 || targetPage >= pageCount)
    {
        targetPage = qMax<PDFInteger>(0, m_pageIndex);
    }

    // Page: a number or a page label (309, "iv")
    QHBoxLayout* pageRow = createRow(section, tr("Page"));
    const PDFAppDocumentView* view = m_controller ? m_controller->getView() : nullptr;
    m_pageField = new PDFLineEdit(view ? view->getPageLabel(targetPage) : QString::number(targetPage + 1), this);
    m_pageField->setObjectName(QStringLiteral("propertiesPage"));
    m_pageField->setFixedWidth(96);
    m_pageField->setMaxLength(64);
    m_pageField->setAccessibleName(tr("Page"));
    pageRow->addWidget(m_pageField);
    pageRow->addWidget(createHint(tr("of %1 · a page label such as “iv” works too").arg(PDFAppLocale::formatNumber(pageCount))), 1);
    connect(m_pageField.data(), &PDFLineEdit::textChanged, this, &PDFAppAnnotationPropertiesDialog::onDestinationChanged);

    // Type: the 8 types with readable names and the description of the selected one
    m_destinationTypeComboBox = new PDFComboBox(this);
    m_destinationTypeComboBox->setObjectName(QStringLiteral("propertiesDestinationType"));
    m_destinationTypeComboBox->setAccessibleName(tr("Type"));
    for (const DestinationType type : H::DESTINATION_TYPES)
    {
        m_destinationTypeComboBox->addItem(getDestinationTypeName(type), int(type));
    }
    m_destinationTypeComboBox->setCurrentIndex(qMax(0, m_destinationTypeComboBox->findData(int(m_resolvedDestination.getDestinationType()))));
    m_destinationTypeComboBox->setFixedWidth(220);
    QHBoxLayout* typeRow = createRow(section, tr("Type"));
    typeRow->addWidget(m_destinationTypeComboBox);
    typeRow->addStretch(1);
    m_destinationDescription = createHint(QString());
    createRow(section, QString())->addWidget(m_destinationDescription, 1);
    connect(m_destinationTypeComboBox.data(), &PDFComboBox::currentIndexChanged, this, &PDFAppAnnotationPropertiesDialog::onDestinationChanged);

    // Position (read only), Keep the reader's zoom (Position only)
    m_positionLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    m_positionLabel->setObjectName(QStringLiteral("propertiesPosition"));
    createRow(section, tr("Position"))->addWidget(m_positionLabel, 1);

    m_keepZoomCheckBox = new QCheckBox(tr("Keep the reader's zoom"), this);
    m_keepZoomCheckBox->setObjectName(QStringLiteral("propertiesKeepZoom"));
    m_keepZoomCheckBox->setChecked(!std::isfinite(m_resolvedDestination.getZoom()) || m_resolvedDestination.getZoom() == 0.0);
    createRow(section, tr("Zoom"))->addWidget(m_keepZoomCheckBox, 1);
    connect(m_keepZoomCheckBox.data(), &QCheckBox::toggled, this, &PDFAppAnnotationPropertiesDialog::onDestinationChanged);

    // Go to target (the dialog stays open), Set to current view
    QHBoxLayout* buttonsRow = createRow(section, QString());
    QPushButton* goToTarget = new QPushButton(tr("Go to target"), this);
    goToTarget->setObjectName(QStringLiteral("propertiesGoToTarget"));
    goToTarget->setAutoDefault(false);
    QPushButton* setToCurrentView = new QPushButton(tr("Set to current view"), this);
    setToCurrentView->setObjectName(QStringLiteral("propertiesSetToCurrentView"));
    setToCurrentView->setAutoDefault(false);
    m_goToTargetButton = goToTarget;
    m_setToCurrentViewButton = setToCurrentView;
    buttonsRow->addWidget(goToTarget);
    buttonsRow->addWidget(setToCurrentView);
    buttonsRow->addStretch(1);
    connect(goToTarget, &QPushButton::clicked, this, &PDFAppAnnotationPropertiesDialog::onGoToTargetClicked);
    connect(setToCurrentView, &QPushButton::clicked, this, &PDFAppAnnotationPropertiesDialog::onSetToCurrentViewClicked);
}

void PDFAppAnnotationPropertiesDialog::createAppearanceSection(QVBoxLayout* layout)
{
    using F = PDFAnnotationProperties;
    using H = PDFAppAnnotationPropertiesDialogHelper;

    const F::Fields fields = m_original.supportedFields;
    const AnnotationType type = m_original.type;
    const bool isFreeText = type == AnnotationType::FreeText;
    const bool isLineType = type == AnnotationType::Line || type == AnnotationType::Polyline || type == AnnotationType::Ink;
    const bool hasAppearance = fields & (F::Color | F::Opacity | F::Border | F::IconName);
    if (!hasAppearance)
    {
        return;
    }

    QVBoxLayout* section = createSection(layout, tr("Appearance"));

    // Sticky note 1027:21900: the icon replaces Fill and Border
    if (type == AnnotationType::Text)
    {
        m_iconGroup = new QButtonGroup(this);
        m_iconGroup->setExclusive(true);
        QHBoxLayout* iconRow = createRow(section, tr("Icon"));
        m_iconNames = PDFAnnotationPropertyEditor::getIconNames(type);
        for (size_t i = 0; i < m_iconNames.size(); ++i)
        {
            QToolButton* button = new QToolButton(this);
            button->setObjectName(QStringLiteral("propertiesIcon:") + QString::fromLatin1(m_iconNames[i]));
            button->setCheckable(true);
            button->setIcon(PDFThemeIcons::getIcon(H::getStickyIconName(m_iconNames[i])));
            button->setIconSize(QSize(20, 20));
            button->setFixedSize(H::ICON_BUTTON, H::ICON_BUTTON);
            button->setToolTip(H::getIconText(m_iconNames[i]));
            button->setAccessibleName(button->toolTip());
            button->setChecked(m_iconNames[i] == m_shown.iconName);
            m_iconGroup->addButton(button, int(i));
            iconRow->addWidget(button);
        }
        iconRow->addStretch(1);
        createRow(section, QString())->addWidget(createHint(tr("Comment · Note · Help · Key · Insert · Paragraph · New paragraph")), 1);
    }
    else if (fields.testFlag(F::IconName))
    {
        // A stamp, a file attachment, a sound: the name of the icon
        m_iconNames = PDFAnnotationPropertyEditor::getIconNames(type);
        m_iconComboBox = new PDFComboBox(this);
        m_iconComboBox->setObjectName(QStringLiteral("propertiesIconName"));
        m_iconComboBox->setAccessibleName(type == AnnotationType::Stamp ? tr("Stamp") : tr("Icon"));
        for (const QByteArray& name : m_iconNames)
        {
            m_iconComboBox->addItem(H::getIconText(name), QString::fromLatin1(name));
        }
        if (m_iconComboBox->findData(QString::fromLatin1(m_shown.iconName)) < 0 && !m_shown.iconName.isEmpty())
        {
            m_iconComboBox->addItem(QString::fromLatin1(m_shown.iconName), QString::fromLatin1(m_shown.iconName));
        }
        m_iconComboBox->setCurrentIndex(qMax(0, m_iconComboBox->findData(QString::fromLatin1(m_shown.iconName))));
        m_iconComboBox->setFixedWidth(220);
        QHBoxLayout* row = createRow(section, type == AnnotationType::Stamp ? tr("Stamp") : tr("Icon"));
        row->addWidget(m_iconComboBox);
        row->addStretch(1);
    }

    // Free text 1027:21975: the text attributes come first
    if (isFreeText)
    {
        QHBoxLayout* fontRow = createRow(section, tr("Font"));
        m_fontComboBox = new PDFComboBox(this);
        m_fontComboBox->setObjectName(QStringLiteral("propertiesFont"));
        m_fontComboBox->setAccessibleName(tr("Font"));
        for (const QString& family : { QStringLiteral("Helvetica"), QStringLiteral("Times Roman"), QStringLiteral("Courier") })
        {
            m_fontComboBox->addItem(family, family);
        }
        if (m_fontComboBox->findData(m_shown.freeTextStyle.fontFamily) < 0)
        {
            m_fontComboBox->addItem(m_shown.freeTextStyle.fontFamily, m_shown.freeTextStyle.fontFamily);
        }
        m_fontComboBox->setCurrentIndex(m_fontComboBox->findData(m_shown.freeTextStyle.fontFamily));
        fontRow->addWidget(m_fontComboBox, 1);

        m_fontSizeSpinBox = new PDFAppPropertiesLengthSpinBox(this);
        m_fontSizeSpinBox->setObjectName(QStringLiteral("propertiesFontSize"));
        m_fontSizeSpinBox->setAccessibleName(tr("Font size"));
        m_fontSizeSpinBox->setRange(F::MINIMUM_FONT_SIZE, F::MAXIMUM_FONT_SIZE);
        m_fontSizeSpinBox->setDecimals(1);
        m_fontSizeSpinBox->setDisplaySuffix(tr("pt"));
        m_fontSizeSpinBox->setValue(m_shown.freeTextStyle.fontSize);
        m_fontSizeSpinBox->setFixedWidth(88);
        fontRow->addWidget(m_fontSizeSpinBox);

        m_textColorRow = createSwatchRow(QStringLiteral("propertiesTextColor"), false, false, m_shown.freeTextStyle.textColor);
        m_textColorRow->setAccessibleName(tr("Text color"));
        QHBoxLayout* textColorRow = createRow(section, tr("Text color"));
        textColorRow->addWidget(m_textColorRow);
        textColorRow->addStretch(1);

        m_alignmentControl = new PDFSegmentedControl(this);
        m_alignmentControl->setObjectName(QStringLiteral("propertiesAlignment"));
        m_alignmentControl->setAccessibleName(tr("Alignment"));
        m_alignmentControl->addSegment(tr("Left"), QStringLiteral("align-left"));
        m_alignmentControl->addSegment(tr("Center"), QStringLiteral("text-align-center"));
        m_alignmentControl->addSegment(tr("Right"), QStringLiteral("text-align-right"));
        const TextAlignment alignment = m_shown.freeTextStyle.textAlignment;
        m_alignmentControl->setCurrentIndex(alignment.testFlag(Qt::AlignHCenter) ? 1 : (alignment.testFlag(Qt::AlignRight) ? 2 : 0));
        QHBoxLayout* alignmentRow = createRow(section, tr("Alignment"));
        alignmentRow->addWidget(m_alignmentControl);
        alignmentRow->addStretch(1);
    }

    // Color (of a text box the color of its border - none + 8 swatches + More)
    if (fields.testFlag(F::Color) && !isFreeText)
    {
        m_colorRow = createSwatchRow(QStringLiteral("propertiesColor"), false, false, m_shown.color);
        m_colorRow->setAccessibleName(tr("Color"));
        QHBoxLayout* row = createRow(section, tr("Color"));
        row->addWidget(m_colorRow);
        row->addStretch(1);
    }

    // Fill (with none and white) - the closed shapes, a text box, a redaction
    const bool hasFill = fields.testFlag(F::InteriorColor) && type != AnnotationType::Line && type != AnnotationType::Polyline;
    if (hasFill)
    {
        m_fillRow = createSwatchRow(QStringLiteral("propertiesFill"), true, true, m_shown.interiorColor);
        m_fillRow->setAccessibleName(tr("Fill"));
        QHBoxLayout* row = createRow(section, tr("Fill"));
        row->addWidget(m_fillRow);
        row->addStretch(1);
    }

    // Opacity: slider + spin box (%)
    if (fields.testFlag(F::Opacity))
    {
        QHBoxLayout* row = createRow(section, tr("Opacity"));
        m_opacitySlider = new QSlider(Qt::Horizontal, this);
        m_opacitySlider->setObjectName(QStringLiteral("propertiesOpacitySlider"));
        m_opacitySlider->setRange(0, 100);
        m_opacitySlider->setAccessibleName(tr("Opacity"));
        m_opacitySpinBox = new PDFSpinBox(this);
        m_opacitySpinBox->setObjectName(QStringLiteral("propertiesOpacity"));
        m_opacitySpinBox->setRange(0, 100);
        m_opacitySpinBox->setDisplaySuffix(QStringLiteral("%"));
        m_opacitySpinBox->setFixedWidth(96);
        m_opacitySpinBox->setAccessibleName(tr("Opacity"));
        const int opacity = qRound(m_shown.opacity * 100.0);
        m_opacitySlider->setValue(opacity);
        m_opacitySpinBox->setValue(opacity);
        row->addWidget(m_opacitySlider, 1);
        row->addWidget(m_opacitySpinBox);
        connect(m_opacitySlider.data(), &QSlider::valueChanged, this, &PDFAppAnnotationPropertiesDialog::onOpacitySliderChanged);
        connect(m_opacitySpinBox.data(), &PDFSpinBox::valueChanged, this, &PDFAppAnnotationPropertiesDialog::onOpacitySpinBoxChanged);
    }

    // Blend mode 1296:31359 (owner 2026-10-27): Normal and the modes the viewer draws; a
    // mode of the document, which it does not draw, stays as an item, until another is picked
    if (fields.testFlag(F::BlendMode))
    {
        m_blendModeComboBox = new PDFComboBox(this);
        m_blendModeComboBox->setObjectName(QStringLiteral("propertiesBlendMode"));
        m_blendModeComboBox->setAccessibleName(tr("Blend mode"));
        for (const BlendMode mode : PDFAnnotationPropertyEditor::getOfferedBlendModes())
        {
            const QByteArray name = PDFAnnotationPropertyEditor::getBlendModeName(mode);
            m_blendModeComboBox->addItem(getBlendModeName(name), QString::fromLatin1(name));
        }
        for (const QByteArray& name : { m_original.blendMode, m_shown.blendMode })
        {
            if (m_blendModeComboBox->findData(QString::fromLatin1(name)) < 0)
            {
                m_blendModeComboBox->addItem(getBlendModeName(name), QString::fromLatin1(name));
            }
        }
        m_blendModeComboBox->setCurrentIndex(qMax(0, m_blendModeComboBox->findData(QString::fromLatin1(m_shown.blendMode))));
        m_blendModeComboBox->setFixedWidth(220);
        QHBoxLayout* row = createRow(section, tr("Blend mode"));
        row->addWidget(m_blendModeComboBox);
        row->addStretch(1);
    }

    // Border / Line: the width and the style
    if (fields.testFlag(F::Border))
    {
        QHBoxLayout* row = createRow(section, isLineType ? tr("Line") : tr("Border"));
        m_borderWidthSpinBox = new PDFAppPropertiesLengthSpinBox(this);
        m_borderWidthSpinBox->setObjectName(QStringLiteral("propertiesBorderWidth"));
        m_borderWidthSpinBox->setAccessibleName(isLineType ? tr("Line width") : tr("Border width"));
        m_borderWidthSpinBox->setRange(0.0, F::MAXIMUM_BORDER_WIDTH);
        m_borderWidthSpinBox->setDecimals(1);
        m_borderWidthSpinBox->setSingleStep(0.5);
        m_borderWidthSpinBox->setDisplaySuffix(tr("pt"));
        m_borderWidthSpinBox->setValue(m_shown.borderWidth);
        m_borderWidthSpinBox->setFixedWidth(96);
        row->addWidget(m_borderWidthSpinBox);

        if (isFreeText)
        {
            row->addStretch(1);

            // Border color of a text box: none + 8 swatches + More on its own line (owner, 2026-10-24)
            m_colorRow = createSwatchRow(QStringLiteral("propertiesColor"), true, false, m_shown.color);
            m_colorRow->setAccessibleName(tr("Border color"));
            QHBoxLayout* colorRow = createRow(section, QString());
            colorRow->addWidget(m_colorRow);
            colorRow->addStretch(1);
        }
        else
        {
            m_borderStyleComboBox = new PDFComboBox(this);
            m_borderStyleComboBox->setObjectName(QStringLiteral("propertiesBorderStyle"));
            m_borderStyleComboBox->setAccessibleName(isLineType ? tr("Line style") : tr("Border style"));
            for (const AnnotationBorderStyle style : H::BORDER_STYLES)
            {
                m_borderStyleComboBox->addItem(getBorderStyleName(style), int(style));
            }
            m_borderStyleComboBox->setCurrentIndex(qMax(0, m_borderStyleComboBox->findData(int(m_shown.borderStyle))));
            m_borderStyleComboBox->setFixedWidth(160);
            row->addWidget(m_borderStyleComboBox);
            row->addStretch(1);
        }
    }

    // Line start / Line end
    if (fields.testFlag(F::LineEndings))
    {
        m_lineStartComboBox = createLineEndingComboBox(QStringLiteral("propertiesLineStart"), m_shown.startLineEnding);
        m_lineStartComboBox->setAccessibleName(tr("Line start"));
        createRow(section, tr("Line start"))->addWidget(m_lineStartComboBox, 1);
        m_lineEndComboBox = createLineEndingComboBox(QStringLiteral("propertiesLineEnd"), m_shown.endLineEnding);
        m_lineEndComboBox->setAccessibleName(tr("Line end"));
        createRow(section, tr("Line end"))->addWidget(m_lineEndComboBox, 1);
    }

    // Callout end of a text box with a callout line
    if (fields.testFlag(F::CalloutLineEnding))
    {
        m_calloutEndComboBox = createLineEndingComboBox(QStringLiteral("propertiesCalloutEnd"), m_shown.calloutLineEnding);
        m_calloutEndComboBox->setAccessibleName(tr("Callout end"));
        createRow(section, tr("Callout end"))->addWidget(m_calloutEndComboBox, 1);
    }

    // Measurement 1027:22124: the measured value, the leader lines, the caption
    if (m_isMeasurement)
    {
        PDFLabel* value = new PDFLabel(m_original.contents.left(256), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
        value->setObjectName(QStringLiteral("propertiesMeasuredValue"));
        value->setTextFormat(Qt::PlainText);
        QHBoxLayout* valueRow = createRow(section, tr("Measured value"));
        valueRow->addWidget(value);
        valueRow->addWidget(createHint(tr("· recomputed from the points")), 1);

        QHBoxLayout* leaderRow = createRow(section, tr("Leader line"));
        m_leaderLineSpinBox = new PDFAppPropertiesLengthSpinBox(this);
        m_leaderLineSpinBox->setObjectName(QStringLiteral("propertiesLeaderLine"));
        m_leaderLineSpinBox->setAccessibleName(tr("Leader line"));
        m_leaderLineSpinBox->setRange(-F::MAXIMUM_LEADER_LINE, F::MAXIMUM_LEADER_LINE);
        m_leaderLineSpinBox->setDecimals(1);
        m_leaderLineSpinBox->setDisplaySuffix(tr("pt"));
        m_leaderLineSpinBox->setValue(m_shown.leaderLineLength);
        m_leaderLineSpinBox->setFixedWidth(110);
        m_leaderExtensionSpinBox = new PDFAppPropertiesLengthSpinBox(this);
        m_leaderExtensionSpinBox->setObjectName(QStringLiteral("propertiesLeaderExtension"));
        m_leaderExtensionSpinBox->setAccessibleName(tr("Leader line extension"));
        m_leaderExtensionSpinBox->setRange(0.0, F::MAXIMUM_LEADER_LINE);
        m_leaderExtensionSpinBox->setDecimals(1);
        m_leaderExtensionSpinBox->setDisplaySuffix(tr("pt"));
        m_leaderExtensionSpinBox->setValue(m_shown.leaderLineExtension);
        m_leaderExtensionSpinBox->setFixedWidth(96);
        leaderRow->addWidget(m_leaderLineSpinBox);
        leaderRow->addWidget(new PDFLabel(tr("extension"), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this));
        leaderRow->addWidget(m_leaderExtensionSpinBox);
        leaderRow->addStretch(1);

        m_captionControl = new PDFSegmentedControl(this);
        m_captionControl->setObjectName(QStringLiteral("propertiesCaption"));
        m_captionControl->setAccessibleName(tr("Caption"));
        m_captionControl->addSegment(tr("Inside the line"));
        m_captionControl->addSegment(tr("Above the line"));
        m_captionControl->setCurrentIndex(m_shown.isCaptionAbove ? 1 : 0);
        QHBoxLayout* captionRow = createRow(section, tr("Caption"));
        captionRow->addWidget(m_captionControl);
        captionRow->addStretch(1);
        createRow(section, QString())->addWidget(createHint(tr("The unit and the scale come from the measure of the page and are read-only here.")), 1);
    }

    // Highlight mode of a link
    if (fields.testFlag(F::HighlightMode))
    {
        m_highlightComboBox = new PDFComboBox(this);
        m_highlightComboBox->setObjectName(QStringLiteral("propertiesHighlight"));
        m_highlightComboBox->setAccessibleName(tr("Highlight"));
        m_highlightComboBox->addItem(tr("None"), int(LinkHighlightMode::None));
        m_highlightComboBox->addItem(tr("Invert"), int(LinkHighlightMode::Invert));
        m_highlightComboBox->addItem(tr("Outline"), int(LinkHighlightMode::Outline));
        m_highlightComboBox->addItem(tr("Push"), int(LinkHighlightMode::Push));
        m_highlightComboBox->setCurrentIndex(qMax(0, m_highlightComboBox->findData(int(m_shown.highlightMode))));
        m_highlightComboBox->setFixedWidth(160);
        QHBoxLayout* row = createRow(section, tr("Highlight"));
        row->addWidget(m_highlightComboBox);
        row->addStretch(1);
    }

    // Sticky note: the comment opened with the document
    if (fields.testFlag(F::Open))
    {
        m_openCheckBox = new QCheckBox(tr("Comment open when the document opens"), this);
        m_openCheckBox->setObjectName(QStringLiteral("propertiesOpen"));
        m_openCheckBox->setChecked(m_shown.isOpen);
        createRow(section, QString())->addWidget(m_openCheckBox, 1);
    }
}

void PDFAppAnnotationPropertiesDialog::createBehaviorSection(QVBoxLayout* layout)
{
    using A = PDFAnnotation;

    if (!m_original.supportedFields.testFlag(PDFAnnotationProperties::Flags))
    {
        return;
    }

    // flags 1022:20823: 3 rows of 2 check boxes, hint 1023:20681
    QVBoxLayout* section = createSection(layout, tr("Behavior"));
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(ROW_GAP);
    grid->setVerticalSpacing(ITEM_GAP);
    const std::pair<A::Flag, QString> flags[] = {
        { A::Locked, tr("Locked") }, { A::Hidden, tr("Hidden on the page") },
        { A::LockedContents, tr("Contents locked") }, { A::Print, tr("Printed with the page") },
        { A::NoZoom, tr("Keeps its size when zoomed") }, { A::NoRotate, tr("Stays upright when the page is rotated") }
    };
    int index = 0;
    for (const auto& flag : flags)
    {
        QCheckBox* checkBox = new QCheckBox(flag.second, this);
        checkBox->setObjectName(QStringLiteral("propertiesFlag:%1").arg(int(flag.first)));
        checkBox->setChecked(m_shown.flags.testFlag(flag.first));
        connect(checkBox, &QCheckBox::toggled, this, &PDFAppAnnotationPropertiesDialog::updateApplyButton);
        m_flagCheckBoxes[flag.first] = checkBox;
        grid->addWidget(checkBox, index / 2, index % 2);
        ++index;
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    section->addLayout(grid);

    const QString hint = m_original.type == AnnotationType::Link ?
                tr("Locked: the link cannot be moved, resized, restyled, deleted or retargeted. Contents locked: only its description cannot be changed.") :
                tr("Locked: the annotation cannot be moved, resized, restyled or deleted. Contents locked: only its comment text cannot be changed.");
    PDFLabel* hintLabel = createHint(hint);
    hintLabel->setObjectName(QStringLiteral("propertiesBehaviorHint"));
    section->addWidget(hintLabel);
}

void PDFAppAnnotationPropertiesDialog::createActions()
{
    // actions 1022:20847: hint, Cancel (Secondary), Apply (Primary); read only: Close.
    // Apply exists always: a dialog of a locked document becomes editable, when the
    // document is unlocked while it is open
    PDFLabel* hint = createHint(QString());
    hint->setObjectName(QStringLiteral("propertiesActionsHint"));
    hint->setWordWrap(false);
    m_actionsHint = hint;

    m_cancelButton = addActionButton(QString(), ActionPosition::Trailing, false);
    m_cancelButton->setObjectName(QStringLiteral("propertiesCancel"));
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppAnnotationPropertiesDialog::onCancelClicked);
    m_applyButton = addActionButton(tr("Apply"), ActionPosition::Trailing, true);
    m_applyButton->setObjectName(QStringLiteral("propertiesApply"));
    connect(m_applyButton.data(), &QPushButton::clicked, this, &PDFAppAnnotationPropertiesDialog::onApplyClicked);
    updateActions();

    // The hint lies at the start of the row of the buttons (1022:20847)
    if (!insertIntoActionRow(this, hint))
    {
        addContent(hint);
    }
}

void PDFAppAnnotationPropertiesDialog::updateActions()
{
    // A read only dialog has one button Close (the primary one), an editable one
    // Cancel and Apply (the primary one)
    if (m_actionsHint)
    {
        m_actionsHint->setText(m_isReadOnly ? tr("The document cannot be changed, the properties are shown only.")
                                            : tr("Ctrl+Z undoes all changes of this dialog at once."));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(m_isReadOnly ? tr("Close") : tr("Cancel"));
        PDFWidgetStyleProperties::set(m_cancelButton, PDFWidgetStyleProperties::PRIMARY, m_isReadOnly);
        m_cancelButton->setDefault(m_isReadOnly);
    }
    if (m_applyButton)
    {
        m_applyButton->setText(tr("Apply"));
        m_applyButton->setVisible(!m_isReadOnly);
        m_applyButton->setDefault(!m_isReadOnly);
    }
}

void PDFAppAnnotationPropertiesDialog::fitScrollArea(PDFMessageBox* box, QScrollArea* scrollArea)
{
    QWidget* body = scrollArea ? scrollArea->widget() : nullptr;
    if (!box || !body)
    {
        return;
    }

    const int contentWidth = box->getContentWidth() > 0 ? box->getContentWidth() : body->sizeHint().width();
    const int bodyHeight = body->hasHeightForWidth() ? body->heightForWidth(contentWidth) : body->sizeHint().height();
    int scrollAreaHeight = bodyHeight;

    // The window (the scrim target) limits the card: the body scrolls
    QWidget* host = box->getScrimTarget() ? box->getScrimTarget() : box->parentWidget();
    if (host)
    {
        const QMargins shadow = box->getShadowMargins();
        const int otherHeight = box->sizeHint().height() - shadow.top() - shadow.bottom() - scrollArea->minimumHeight();
        const int availableHeight = host->height() - 2 * PDFAppAnnotationPropertiesDialogHelper::WINDOW_MARGIN - qMax(0, otherHeight);
        scrollAreaHeight = qMax(PDFAppAnnotationPropertiesDialogHelper::MINIMUM_BODY_HEIGHT, qMin(bodyHeight, availableHeight));
    }
    scrollArea->setFixedHeight(scrollAreaHeight);

    if (box->isVisible() && box->layout())
    {
        box->layout()->activate();
        box->adjustSize();
    }
}

bool PDFAppAnnotationPropertiesDialog::insertIntoActionRow(PDFMessageBox* box, QWidget* widget)
{
    const QList<QPushButton*> buttons = box ? box->getActionButtons() : QList<QPushButton*>();
    if (!widget || buttons.isEmpty())
    {
        return false;
    }

    // The row of the buttons: the box layout, which holds the first button
    std::vector<QLayout*> layouts = { box->layout() };
    while (!layouts.empty())
    {
        QLayout* layout = layouts.back();
        layouts.pop_back();
        if (!layout)
        {
            continue;
        }

        QBoxLayout* boxLayout = qobject_cast<QBoxLayout*>(layout);
        if (boxLayout && boxLayout->indexOf(buttons.front()) >= 0)
        {
            boxLayout->insertWidget(0, widget, 0, Qt::AlignVCenter);
            return true;
        }
        for (int i = 0; i < layout->count(); ++i)
        {
            layouts.push_back(layout->itemAt(i)->layout());
        }
    }
    return false;
}

PDFAnnotationProperties::Fields PDFAppAnnotationPropertiesDialog::getEditableFields() const
{
    using F = PDFAnnotationProperties;

    if (m_isReadOnly)
    {
        return F::None;
    }

    // ISO 32000-2 Table 167: Locked keeps every property except the text, the
    // lock flags stay changeable; LockedContents keeps the text
    F::Fields fields = m_original.supportedFields;
    if (m_isLocked)
    {
        fields &= F::Fields(F::Contents | F::Flags);
    }
    if (m_isContentsLocked)
    {
        fields &= ~F::Fields(F::Contents);
    }
    return fields;
}

void PDFAppAnnotationPropertiesDialog::updateEnabledState()
{
    using F = PDFAnnotationProperties;

    using H = PDFAppAnnotationPropertiesDialogHelper;

    const F::Fields fields = getEditableFields();

    H::setEditable(m_authorField, fields.testFlag(F::Author));
    H::setEditable(m_subjectField, fields.testFlag(F::Subject));
    H::setEditable(m_textEdit, fields.testFlag(F::Contents));
    const bool isDestinationEditable = fields.testFlag(F::Destination);
    H::setEditable(m_pageField, isDestinationEditable);
    H::setEditable(m_destinationTypeComboBox, isDestinationEditable);
    H::setEditable(m_setToCurrentViewButton, isDestinationEditable);
    H::setEditable(m_colorRow, fields.testFlag(F::Color));
    H::setEditable(m_fillRow, fields.testFlag(F::InteriorColor));
    H::setEditable(m_opacitySlider, fields.testFlag(F::Opacity));
    H::setEditable(m_opacitySpinBox, fields.testFlag(F::Opacity));
    H::setEditable(m_blendModeComboBox, fields.testFlag(F::BlendMode));
    H::setEditable(m_borderWidthSpinBox, fields.testFlag(F::Border));
    H::setEditable(m_borderStyleComboBox, fields.testFlag(F::Border));
    H::setEditable(m_lineStartComboBox, fields.testFlag(F::LineEndings));
    H::setEditable(m_lineEndComboBox, fields.testFlag(F::LineEndings));
    H::setEditable(m_iconComboBox, fields.testFlag(F::IconName));
    H::setEditable(m_openCheckBox, fields.testFlag(F::Open));
    H::setEditable(m_fontComboBox, fields.testFlag(F::DefaultAppearance));
    H::setEditable(m_fontSizeSpinBox, fields.testFlag(F::DefaultAppearance));
    H::setEditable(m_textColorRow, fields.testFlag(F::DefaultAppearance));
    H::setEditable(m_alignmentControl, fields.testFlag(F::Alignment));
    H::setEditable(m_calloutEndComboBox, fields.testFlag(F::CalloutLineEnding));
    H::setEditable(m_leaderLineSpinBox, fields.testFlag(F::LeaderLines));
    H::setEditable(m_leaderExtensionSpinBox, fields.testFlag(F::LeaderLines));
    H::setEditable(m_captionControl, fields.testFlag(F::Caption));
    H::setEditable(m_highlightComboBox, fields.testFlag(F::HighlightMode));
    if (m_iconGroup)
    {
        for (QAbstractButton* button : m_iconGroup->buttons())
        {
            H::setEditable(button, fields.testFlag(F::IconName));
        }
    }

    // A locked annotation changes only its lock flags
    for (const auto& item : m_flagCheckBoxes)
    {
        const bool isLockFlag = item.first == PDFAnnotation::Locked || item.first == PDFAnnotation::LockedContents;
        H::setEditable(item.second, fields.testFlag(F::Flags) && (!m_isLocked || isLockFlag));
    }

    updateDestinationTexts();
}

std::optional<PDFInteger> PDFAppAnnotationPropertiesDialog::getDestinationPageIndex() const
{
    const PDFAppDocumentView* view = m_controller ? m_controller->getView() : nullptr;
    if (!m_pageField || !view)
    {
        return std::nullopt;
    }
    return view->findPageIndex(m_pageField->text().trimmed());
}

PDFDestination PDFAppAnnotationPropertiesDialog::getEditedDestination() const
{
    // The destination is kept (also a named one), until the user changes it
    if (!m_isDestinationChanged || !m_destinationTypeComboBox)
    {
        return m_original.destination;
    }

    const PDFAppDocument* document = m_controller ? m_controller->getDocument() : nullptr;
    const PDFDocumentPointer pdfDocument = document ? document->getDocument() : PDFDocumentPointer();
    const std::optional<PDFInteger> pageIndex = getDestinationPageIndex();
    const PDFPage* page = pdfDocument && pageIndex ? pdfDocument->getCatalog()->getPage(size_t(*pageIndex)) : nullptr;
    if (!page)
    {
        return m_original.destination;
    }

    const DestinationType type = static_cast<DestinationType>(m_destinationTypeComboBox->currentData().toInt());
    PDFDestination source = m_viewDestination.isValid() ? m_viewDestination : m_resolvedDestination;
    if (source.getPageReference() != page->getPageReference())
    {
        // Another page: the coordinates of the top left corner of the page
        const QRectF mediaBox = page->getMediaBox();
        source = PDFDestination::createFitR(page->getPageReference(), mediaBox.left(), mediaBox.bottom(), mediaBox.right(), mediaBox.top());
    }

    PDFDestination destination = PDFAppAnnotationPropertiesDialogHelper::createDestination(type, page->getPageReference(), source);
    if (type == DestinationType::XYZ)
    {
        const bool keepZoom = m_keepZoomCheckBox && m_keepZoomCheckBox->isChecked();
        destination.setZoom(keepZoom ? std::numeric_limits<PDFReal>::quiet_NaN() : (std::isfinite(source.getZoom()) ? source.getZoom() : 1.0));
    }
    return destination;
}

void PDFAppAnnotationPropertiesDialog::updateDestinationTexts()
{
    using H = PDFAppAnnotationPropertiesDialogHelper;

    if (!m_destinationTypeComboBox)
    {
        return;
    }

    const DestinationType type = static_cast<DestinationType>(m_destinationTypeComboBox->currentData().toInt());
    m_destinationDescription->setText(getDestinationTypeDescription(type));

    // Position (read only) of the coordinates of the type
    const PDFDestination destination = getEditedDestination().isNamedDestination() ? m_resolvedDestination : getEditedDestination();
    QStringList parts;
    switch (type)
    {
        case DestinationType::XYZ:
            parts << tr("Left %1 mm").arg(H::formatMillimeters(destination.getLeft())) << tr("Top %1 mm").arg(H::formatMillimeters(destination.getTop()));
            break;
        case DestinationType::FitH:
        case DestinationType::FitBH:
            parts << tr("Top %1 mm").arg(H::formatMillimeters(destination.getTop()));
            break;
        case DestinationType::FitV:
        case DestinationType::FitBV:
            parts << tr("Left %1 mm").arg(H::formatMillimeters(destination.getLeft()));
            break;
        case DestinationType::FitR:
            parts << tr("Left %1 mm").arg(H::formatMillimeters(destination.getLeft())) << tr("Top %1 mm").arg(H::formatMillimeters(destination.getTop()))
                  << tr("Right %1 mm").arg(H::formatMillimeters(destination.getRight())) << tr("Bottom %1 mm").arg(H::formatMillimeters(destination.getBottom()));
            break;
        default:
            parts << tr("The whole page");
            break;
    }
    m_positionLabel->setText(parts.join(QStringLiteral(" · ")));

    // Keep the reader's zoom: only Position
    const bool isEditable = getEditableFields().testFlag(PDFAnnotationProperties::Destination);
    m_keepZoomCheckBox->setEnabled(isEditable && type == DestinationType::XYZ);
    m_goToTargetButton->setEnabled(getDestinationPageIndex().has_value());

    const bool isPageValid = !isEditable || getDestinationPageIndex().has_value();
    m_pageField->setInvalid(!isPageValid, isPageValid ? QString() : tr("The document has no such page."));
}

PDFAnnotationProperties PDFAppAnnotationPropertiesDialog::getEditedProperties() const
{
    PDFAnnotationProperties properties = m_original;

    if (m_authorField)
    {
        properties.author = m_authorField->text();
    }
    if (m_subjectField)
    {
        properties.subject = m_subjectField->text();
    }
    if (m_textEdit)
    {
        properties.contents = m_textEdit->toPlainText();
    }
    if (m_colorRow)
    {
        properties.color = m_colorRow->getColor();
    }
    if (m_fillRow)
    {
        properties.interiorColor = m_fillRow->getColor();
    }
    if (m_opacitySpinBox)
    {
        properties.opacity = m_opacitySpinBox->value() == qRound(m_original.opacity * 100.0) ? m_original.opacity : m_opacitySpinBox->value() / 100.0;
    }
    if (m_blendModeComboBox)
    {
        properties.blendMode = m_blendModeComboBox->currentData().toString().toLatin1();
    }
    if (m_borderWidthSpinBox)
    {
        properties.borderWidth = PDFAppAnnotationPropertiesDialogHelper::getValue(m_borderWidthSpinBox, m_original.borderWidth);
    }
    if (m_borderStyleComboBox)
    {
        properties.borderStyle = static_cast<AnnotationBorderStyle>(m_borderStyleComboBox->currentData().toInt());
    }
    if (m_lineStartComboBox && m_lineEndComboBox)
    {
        properties.startLineEnding = static_cast<AnnotationLineEnding>(m_lineStartComboBox->currentData().toInt());
        properties.endLineEnding = static_cast<AnnotationLineEnding>(m_lineEndComboBox->currentData().toInt());
    }
    if (m_iconGroup && m_iconGroup->checkedId() >= 0 && size_t(m_iconGroup->checkedId()) < m_iconNames.size())
    {
        properties.iconName = m_iconNames[size_t(m_iconGroup->checkedId())];
    }
    if (m_iconComboBox)
    {
        properties.iconName = m_iconComboBox->currentData().toString().toLatin1();
    }
    if (m_openCheckBox)
    {
        properties.isOpen = m_openCheckBox->isChecked();
    }
    if (m_fontComboBox && m_fontSizeSpinBox && m_textColorRow)
    {
        properties.freeTextStyle.fontFamily = m_fontComboBox->currentData().toString();
        properties.freeTextStyle.fontSize = PDFAppAnnotationPropertiesDialogHelper::getValue(m_fontSizeSpinBox, m_original.freeTextStyle.fontSize);
        if (m_textColorRow->getColor().isValid())
        {
            properties.freeTextStyle.textColor = m_textColorRow->getColor();
        }
    }
    if (m_alignmentControl)
    {
        static constexpr Qt::AlignmentFlag alignments[] = { Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight };
        properties.freeTextStyle.textAlignment = TextAlignment(alignments[qBound(0, m_alignmentControl->getCurrentIndex(), 2)] | Qt::AlignTop);
    }
    if (m_calloutEndComboBox)
    {
        properties.calloutLineEnding = static_cast<AnnotationLineEnding>(m_calloutEndComboBox->currentData().toInt());
    }
    if (m_leaderLineSpinBox && m_leaderExtensionSpinBox)
    {
        properties.leaderLineLength = PDFAppAnnotationPropertiesDialogHelper::getValue(m_leaderLineSpinBox, m_original.leaderLineLength);
        properties.leaderLineExtension = PDFAppAnnotationPropertiesDialogHelper::getValue(m_leaderExtensionSpinBox, m_original.leaderLineExtension);
    }
    if (m_captionControl)
    {
        properties.isCaptionAbove = m_captionControl->getCurrentIndex() == 1;
    }
    if (m_highlightComboBox)
    {
        properties.highlightMode = static_cast<LinkHighlightMode>(m_highlightComboBox->currentData().toInt());
    }
    for (const auto& item : m_flagCheckBoxes)
    {
        if (item.second)
        {
            properties.flags.setFlag(item.first, item.second->isChecked());
        }
    }
    properties.destination = getEditedDestination();
    return properties;
}

PDFAnnotationProperties::Fields PDFAppAnnotationPropertiesDialog::getChangedFields() const
{
    // Values shown by the controls are compared with the values read (a value,
    // which a control cannot show exactly - 33.3 % - is not a change)
    const PDFAnnotationProperties edited = PDFAnnotationPropertyEditor::bound(getEditedProperties());
    PDFAnnotationProperties::Fields fields = PDFAnnotationPropertyEditor::getChangedFields(m_original, edited) & getEditableFields();
    if (m_opacitySpinBox && m_opacitySpinBox->value() == qRound(m_original.opacity * 100.0))
    {
        fields &= ~PDFAnnotationProperties::Fields(PDFAnnotationProperties::Opacity);
    }
    return fields;
}

void PDFAppAnnotationPropertiesDialog::updateApplyButton()
{
    if (m_applyButton)
    {
        const bool isPageValid = !m_pageField || !m_isDestinationChanged || getDestinationPageIndex().has_value();
        m_applyButton->setEnabled(isPageValid);
    }
}

bool PDFAppAnnotationPropertiesDialog::apply()
{
    PDFAppAnnotationController* controller = m_controller.data();
    PDFAppDocument* document = controller ? controller->getDocument() : nullptr;
    if (m_isReadOnly || !controller || !document)
    {
        return false;
    }

    PDFAnnotationProperties::Fields fields = getChangedFields();
    if (fields == PDFAnnotationProperties::None)
    {
        // Nothing changed: no undo step
        m_isApplied = false;
        accept();
        return true;
    }

    // A locked annotation changes only its lock flags (the other flags are kept)
    PDFAnnotationProperties properties = getEditedProperties();
    if (m_isLocked)
    {
        PDFAnnotation::Flags flags = m_original.flags;
        flags.setFlag(PDFAnnotation::Locked, properties.flags.testFlag(PDFAnnotation::Locked));
        flags.setFlag(PDFAnnotation::LockedContents, properties.flags.testFlag(PDFAnnotation::LockedContents));
        properties.flags = flags;
    }

    const PDFAppAnnotationResult result = PDFAppAnnotationOperations::setProperties(document->getDocument(), m_annotation, properties, fields);
    if (!result)
    {
        accept();
        return true;
    }

    QPointer<PDFAppAnnotationPropertiesDialog> guard(this);
    const bool isCommitted = controller->commit(*result, PDFAppAnnotationChangeKind::Annotation, tr("Edit annotation properties"));
    if (!guard)
    {
        return isCommitted;
    }
    if (isCommitted)
    {
        m_isApplied = true;
        accept();
    }
    return isCommitted;
}

void PDFAppAnnotationPropertiesDialog::onApplyClicked()
{
    apply();
}

void PDFAppAnnotationPropertiesDialog::onCancelClicked()
{
    reject();
}

void PDFAppAnnotationPropertiesDialog::onControllerDestroyed()
{
    // The sender is dying: only the own state is changed, the dialog goes from the event loop
    m_controller = nullptr;
    if (isVisible())
    {
        reject();
    }
    deleteLater();
}

void PDFAppAnnotationPropertiesDialog::onDialogFinished(int result)
{
    Q_UNUSED(result);

    // The dialog closed (Apply, Cancel, Esc, its annotation gone): the merge
    // session of the bar ends (Apply is one step)
    if (m_controller)
    {
        m_controller->endMergeSession();
    }
    Q_EMIT dialogFinished(m_isApplied);
}

void PDFAppAnnotationPropertiesDialog::onDocumentChanged()
{
    // The annotation is gone (deleted, undo of its creation, another document)
    const PDFAppDocument* document = m_controller ? m_controller->getDocument() : nullptr;
    const PDFAppAnnotationInfo info = PDFAppAnnotationInfo::read(document && document->isReady() ? document->getDocument().data() : nullptr, m_annotation);
    if (!info.isValid() && isVisible())
    {
        reject();
    }
}

void PDFAppAnnotationPropertiesDialog::onEditingAllowedChanged()
{
    // The lock of the document while the dialog is open: read only
    const bool isReadOnly = !m_controller || !m_controller->isAnnotationEditingAllowed();
    if (isReadOnly != m_isReadOnly)
    {
        m_isReadOnly = isReadOnly;
        updateActions();
        updateEnabledState();
        updateApplyButton();
    }
}

void PDFAppAnnotationPropertiesDialog::onOpacitySliderChanged(int value)
{
    if (m_opacitySpinBox && m_opacitySpinBox->value() != value)
    {
        m_opacitySpinBox->setValue(value);
    }
}

void PDFAppAnnotationPropertiesDialog::onOpacitySpinBoxChanged(int value)
{
    if (m_opacitySlider && m_opacitySlider->value() != value)
    {
        m_opacitySlider->setValue(value);
    }
}

void PDFAppAnnotationPropertiesDialog::onDestinationChanged()
{
    m_isDestinationChanged = true;
    updateDestinationTexts();
    updateApplyButton();
}

void PDFAppAnnotationPropertiesDialog::onGoToTargetClicked()
{
    // The view goes to the destination (its type, its place and its zoom - the
    // edited one, or the one of the link), the dialog stays open
    PDFAppDocumentView* view = m_controller ? m_controller->getView() : nullptr;
    const PDFAppDocument* document = m_controller ? m_controller->getDocument() : nullptr;
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    const std::optional<PDFInteger> pageIndex = getDestinationPageIndex();
    if (!view || !pageIndex)
    {
        return;
    }

    PDFDestination destination = getEditedDestination();
    if (destination.isNamedDestination())
    {
        destination = m_resolvedDestination;
    }

    // The destination must lead to the page of the field (else the page only)
    const PDFCatalog* catalog = document && document->getDocument() ? document->getDocument()->getCatalog() : nullptr;
    PDFInteger targetPage = destination.getPageIndex();
    if (catalog && destination.getPageReference().isValid())
    {
        targetPage = PDFInteger(catalog->getPageIndexFromPageReference(destination.getPageReference()));
    }
    if (proxy && destination.isValid() && !destination.isNamedDestination() && targetPage == *pageIndex)
    {
        proxy->goToDestination(destination);
    }
    else
    {
        view->goToPage(*pageIndex);
    }
}

void PDFAppAnnotationPropertiesDialog::onSetToCurrentViewClicked()
{
    // The page and the place shown at the top left corner of the canvas, the zoom
    PDFAppDocumentView* view = m_controller ? m_controller->getView() : nullptr;
    const PDFAppDocument* document = m_controller ? m_controller->getDocument() : nullptr;
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    if (!view || !document || !proxy || !document->getDocument() || !m_pageField)
    {
        return;
    }

    const PDFInteger pageIndex = view->getCurrentPageIndex();
    const PDFPage* page = document->getDocument()->getCatalog()->getPage(size_t(qMax<PDFInteger>(0, pageIndex)));
    const PDFWidgetSnapshot snapshot = proxy->getSnapshot();
    const PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
    if (!page || !item)
    {
        return;
    }

    bool isInvertible = false;
    const QTransform deviceToPage = item->pageToDeviceMatrix.inverted(&isInvertible);
    const QRectF pageRect = page->getMediaBox().normalized();
    QRectF visible = pageRect;
    const PDFWidget* pdfWidget = view->getPdfWidget();
    const QWidget* drawWidget = pdfWidget && pdfWidget->getDrawWidget() ? pdfWidget->getDrawWidget()->getWidget() : nullptr;
    if (isInvertible && drawWidget)
    {
        visible = deviceToPage.mapRect(QRectF(drawWidget->rect())).normalized().intersected(pageRect);
    }
    if (visible.isEmpty())
    {
        visible = pageRect;
    }

    // PDF coordinates grow upwards: the top of the visible part is its maximal y
    m_viewDestination = PDFDestination::createFitR(page->getPageReference(), visible.left(), visible.bottom(), visible.right(), visible.top());
    m_viewDestination.setZoom(view->getZoom());

    const QSignalBlocker blocker(m_pageField);
    m_pageField->setText(view->getPageLabel(pageIndex));
    onDestinationChanged();
}

void PDFAppAnnotationPropertiesDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppAnnotationPropertiesDialog::retranslate()
{
    using H = PDFAppAnnotationPropertiesDialogHelper;

    // The head and the buttons
    setTitle(H::getTitle(m_controller, m_annotation));
    setText(H::getSubtitle(m_controller, m_annotation));
    setWindowTitle(PDFAppAnnotationInfo::getPropertiesTitle(m_original.type));
    updateActions();

    // The sections are built again with the values of the controls (an edited value
    // stays, the dialog commits nothing); the old body goes from the event loop
    QScrollArea* oldScrollArea = m_scrollArea.data();
    if (!oldScrollArea)
    {
        return;
    }

    m_shown = getEditedProperties();
    const QString pageText = m_pageField ? m_pageField->text() : QString();
    const QVariant destinationType = m_destinationTypeComboBox ? m_destinationTypeComboBox->currentData() : QVariant();
    const bool isKeepZoomChecked = m_keepZoomCheckBox && m_keepZoomCheckBox->isChecked();
    const int scrollPosition = oldScrollArea->verticalScrollBar()->value();
    QWidget* focusWidget = QApplication::focusWidget();
    const QString focusName = focusWidget && oldScrollArea->isAncestorOf(focusWidget) ? focusWidget->objectName() : QString();

    delete m_iconGroup.data();
    m_flagCheckBoxes.clear();
    m_iconNames.clear();
    oldScrollArea->hide();
    oldScrollArea->setParent(nullptr);
    oldScrollArea->deleteLater();

    createContent();
    if (m_pageField)
    {
        const QSignalBlocker blocker(m_pageField);
        m_pageField->setText(pageText);
    }
    if (m_destinationTypeComboBox)
    {
        const QSignalBlocker blocker(m_destinationTypeComboBox);
        m_destinationTypeComboBox->setCurrentIndex(qMax(0, m_destinationTypeComboBox->findData(destinationType)));
    }
    if (m_keepZoomCheckBox)
    {
        const QSignalBlocker blocker(m_keepZoomCheckBox);
        m_keepZoomCheckBox->setChecked(isKeepZoomChecked);
    }

    updateEnabledState();
    updateApplyButton();
    fitScrollArea(this, m_scrollArea);
    if (m_scrollArea)
    {
        m_scrollArea->verticalScrollBar()->setValue(scrollPosition);
    }
    if (QWidget* widget = !focusName.isEmpty() && m_scrollArea ? m_scrollArea->findChild<QWidget*>(focusName) : nullptr)
    {
        widget->setFocus(Qt::OtherFocusReason);
    }
}

void PDFAppAnnotationPropertiesDialog::keyPressEvent(QKeyEvent* event)
{
    // Ctrl+Enter applies also from the text field
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers().testFlag(Qt::ControlModifier) && m_applyButton && m_applyButton->isEnabled())
    {
        apply();
        event->accept();
        return;
    }
    BaseClass::keyPressEvent(event);
}

}   // namespace pdf
