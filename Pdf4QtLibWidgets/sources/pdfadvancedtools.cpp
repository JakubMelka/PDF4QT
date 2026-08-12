// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfadvancedtools.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfutils.h"
#include "pdfcompiler.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetutils.h"
#include "pdfcatalog.h"
#include "insertpagenumbersdialog.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QColorDialog>
#include <QKeyEvent>
#include <QPushButton>
#include <QTextEdit>
#include <QVector2D>
#include <QVBoxLayout>
#include <QApplication>
#include <QSettings>

#include <limits>
#include <algorithm>
#include <cmath>

#include "pdfdbgheap.h"

namespace pdf
{

/// Opacity of the fill of the created shape annotations. Fully opaque fill would
/// hide the content of the page below the annotation.
static constexpr PDFReal SHAPE_FILL_OPACITY = 0.2;

/// Default style of the shape annotations (line, polyline, polygon, rectangle,
/// ellipse), which is used until the user selects his own style.
static PDFAnnotationStyle getDefaultShapeStyle()
{
    PDFAnnotationStyle style;
    style.strokeColor = Qt::red;
    style.fillColor = Qt::yellow;
    style.penWidth = 1.0;
    return style;
}

/// Converts the fill color of the style to the color used for the created
/// annotation. Invalid fill color means, that the shape is not filled at all.
static QColor getAnnotationFillColor(const PDFAnnotationStyle& style)
{
    QColor color = style.fillColor;

    if (!color.isValid())
    {
        return QColor(Qt::transparent);
    }

    color.setAlphaF(SHAPE_FILL_OPACITY);
    return color;
}

PDFCreateStickyNoteTool::PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr),
    m_icon(pdf::TextAnnotationIcon::Comment)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setSnapToAnnotations(true);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateStickyNoteTool::onPointPicked);
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateStickyNoteTool::onActionTriggered);

    updateActions();
}

void PDFCreateStickyNoteTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateStickyNoteTool::onActionTriggered(QAction* action)
{
    setActive(action && action->isChecked());

    if (action)
    {
        m_icon = static_cast<TextAnnotationIcon>(action->data().toInt());
    }
}

void PDFCreateStickyNoteTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    bool ok = false;
    QString text = QInputDialog::getText(getProxy()->getWidget(), tr("Sticky note"), tr("Enter text to be displayed in the sticky note"), QLineEdit::Normal, QString(), &ok);

    if (ok && !text.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        QString author = PDFAuthorSettings::getAuthorName();
        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationText(page, QRectF(pagePoint, QSizeF(0, 0)), m_icon, author, QString(), text, false);
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
    else
    {
        m_pickTool->resetTool();
    }
}

PDFCreateHyperlinkTool::PDFCreateHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateHyperlinkTool::onRectanglePicked);

    updateActions();
}

LinkHighlightMode PDFCreateHyperlinkTool::getHighlightMode() const
{
    return m_highlightMode;
}

void PDFCreateHyperlinkTool::setHighlightMode(const LinkHighlightMode& highlightMode)
{
    m_highlightMode = highlightMode;
}

void PDFCreateHyperlinkTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    bool ok = false;
    QString url = QInputDialog::getText(getProxy()->getWidget(), tr("Hyperlink"), tr("Enter url address of the hyperlink"), QLineEdit::Normal, QString(), &ok);

    if (ok && !url.isEmpty())
    {
        PDFDocumentModifier modifier(getDocument());

        PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
        modifier.getBuilder()->createAnnotationLink(page, pageRectangle, url, m_highlightMode);
        modifier.markAnnotationsChanged();

        if (modifier.finalize())
        {
            Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }

        setActive(false);
    }
}

PDFCreateInDocumentHyperlinkTool::PDFCreateInDocumentHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateInDocumentHyperlinkTool::onLinkRectanglePicked);
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateInDocumentHyperlinkTool::onActionTriggered);

    updateActions();
}

LinkHighlightMode PDFCreateInDocumentHyperlinkTool::getHighlightMode() const
{
    return m_highlightMode;
}

void PDFCreateInDocumentHyperlinkTool::setHighlightMode(const LinkHighlightMode& highlightMode)
{
    m_highlightMode = highlightMode;
}

void PDFCreateInDocumentHyperlinkTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateInDocumentHyperlinkTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        resetPendingLink();
        Q_EMIT messageDisplayRequest(tr("Select hyperlink rectangle."), 5000);
    }
    else
    {
        if (!m_isPickingTarget)
        {
            resetPendingLink();
        }
        m_pickTool->resetTool();
    }
}

void PDFCreateInDocumentHyperlinkTool::onActionTriggered(QAction* action)
{
    if (action)
    {
        m_destinationType = static_cast<DestinationType>(action->data().toInt());
        m_inheritZoom = action->property("inheritZoom").toBool();
    }

    setActive(action && action->isChecked());
}

void PDFCreateInDocumentHyperlinkTool::onLinkRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        m_pickTool->resetTool();
        return;
    }

    m_linkPageIndex = pageIndex;
    m_linkRectangle = pageRectangle;

    if (isRectangleDestination())
    {
        Q_EMIT messageDisplayRequest(tr("Select target rectangle."), 5000);
        m_isPickingTarget = true;
        m_toolManager->pickRectangle([this](PDFInteger targetPageIndex, QRectF targetRectangle) { onTargetRectanglePicked(targetPageIndex, targetRectangle); });
    }
    else
    {
        Q_EMIT messageDisplayRequest(tr("Select target page."), 5000);
        m_isPickingTarget = true;
        m_toolManager->pickPage([this](PDFInteger targetPageIndex) { onTargetPagePicked(targetPageIndex); });
    }
}

void PDFCreateInDocumentHyperlinkTool::onTargetPagePicked(PDFInteger pageIndex)
{
    createLinkAnnotation(createDestination(pageIndex, QRectF()));
}

void PDFCreateInDocumentHyperlinkTool::onTargetRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        resetPendingLink();
        updateActions();
        return;
    }

    createLinkAnnotation(createDestination(pageIndex, pageRectangle));
}

void PDFCreateInDocumentHyperlinkTool::createLinkAnnotation(const PDFDestination& destination)
{
    if (m_linkPageIndex < 0 || m_linkRectangle.isEmpty() || !destination.isValid())
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());

    PDFObjectReference page = getDocument()->getCatalog()->getPage(m_linkPageIndex)->getPageReference();
    PDFObjectReference action = modifier.getBuilder()->createActionGoTo(destination);
    modifier.getBuilder()->createAnnotationLink(page, m_linkRectangle, action, m_highlightMode);
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    resetPendingLink();
    setActive(false);
    updateActions();
}

PDFDestination PDFCreateInDocumentHyperlinkTool::createDestination(PDFInteger pageIndex, QRectF pageRectangle) const
{
    PDFDestination destination;
    destination.setDestinationType(m_destinationType);
    destination.setPageIndex(pageIndex);
    destination.setPageReference(getDocument()->getCatalog()->getPage(pageIndex)->getPageReference());
    destination.setZoom(m_inheritZoom ? std::numeric_limits<PDFReal>::quiet_NaN() : getProxy()->getZoom());

    if (!pageRectangle.isEmpty())
    {
        destination.setLeft(pageRectangle.left());
        destination.setRight(pageRectangle.right());
        destination.setTop(pageRectangle.bottom());
        destination.setBottom(pageRectangle.top());
    }

    return destination;
}

bool PDFCreateInDocumentHyperlinkTool::isRectangleDestination() const
{
    switch (m_destinationType)
    {
        case DestinationType::FitR:
        case DestinationType::XYZ:
            return true;

        case DestinationType::Fit:
        case DestinationType::FitH:
        case DestinationType::FitV:
        case DestinationType::FitB:
        case DestinationType::FitBH:
        case DestinationType::FitBV:
        case DestinationType::Invalid:
        case DestinationType::Named:
            break;
    }

    return false;
}

void PDFCreateInDocumentHyperlinkTool::resetPendingLink()
{
    m_isPickingTarget = false;
    m_linkPageIndex = -1;
    m_linkRectangle = QRectF();
}

PDFCreateFreeTextTool::PDFCreateFreeTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateFreeTextTool::onRectanglePicked);

    updateActions();
}

bool PDFCreateFreeTextTool::configureFreeText(QString& text)
{
    QDialog dialog(getProxy()->getWidget());
    dialog.setWindowTitle(tr("Free text annotation"));

    QVBoxLayout* rootLayout = new QVBoxLayout(&dialog);
    QFormLayout* formLayout = new QFormLayout();

    QTextEdit* textEdit = new QTextEdit(text, &dialog);
    textEdit->setMinimumHeight(140);

    QFontComboBox* fontCombo = new QFontComboBox(&dialog);
    if (!m_style.fontFamily.isEmpty())
    {
        fontCombo->setCurrentFont(QFont(m_style.fontFamily));
    }

    QDoubleSpinBox* fontSizeSpinBox = new QDoubleSpinBox(&dialog);
    fontSizeSpinBox->setRange(1.0, 512.0);
    fontSizeSpinBox->setDecimals(1);
    fontSizeSpinBox->setValue(m_style.fontSize);

    QPushButton* colorButton = new QPushButton(tr("Select"), &dialog);
    auto updateColorButton = [colorButton](const QColor& color)
    {
        const QColor effectiveColor = color.isValid() ? color : QColor(Qt::black);
        colorButton->setText(effectiveColor.name(QColor::HexRgb));
        colorButton->setStyleSheet(QString("background-color: %1;").arg(effectiveColor.name(QColor::HexRgb)));
    };
    updateColorButton(m_style.textColor);

    QComboBox* alignmentCombo = new QComboBox(&dialog);
    alignmentCombo->addItem(tr("Left"), static_cast<int>(Qt::AlignLeft));
    alignmentCombo->addItem(tr("Center"), static_cast<int>(Qt::AlignHCenter));
    alignmentCombo->addItem(tr("Right"), static_cast<int>(Qt::AlignRight));

    const Qt::Alignment horizontalAlignment = Qt::Alignment(m_style.textAlignment) & Qt::AlignHorizontal_Mask;
    int alignmentIndex = alignmentCombo->findData(static_cast<int>(horizontalAlignment));
    if (alignmentIndex < 0)
    {
        alignmentIndex = 0;
    }
    alignmentCombo->setCurrentIndex(alignmentIndex);

    QCheckBox* autoResizeCheckBox = new QCheckBox(tr("Automatically expand annotation to fit text"), &dialog);
    autoResizeCheckBox->setChecked(m_autoResizeToContents);

    formLayout->addRow(tr("Text:"), textEdit);
    formLayout->addRow(tr("Font:"), fontCombo);
    formLayout->addRow(tr("Size:"), fontSizeSpinBox);
    formLayout->addRow(tr("Color:"), colorButton);
    formLayout->addRow(tr("Alignment:"), alignmentCombo);
    formLayout->addRow(QString(), autoResizeCheckBox);
    rootLayout->addLayout(formLayout);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    rootLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QColor selectedColor = m_style.textColor;
    connect(colorButton, &QPushButton::clicked, &dialog, [&dialog, &selectedColor, updateColorButton]()
    {
        QColor color = QColorDialog::getColor(selectedColor, &dialog, QObject::tr("Text color"));
        if (color.isValid())
        {
            selectedColor = color;
            updateColorButton(selectedColor);
        }
    });

    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }

    text = textEdit->toPlainText();
    m_style.fontFamily = fontCombo->currentFont().family();
    m_style.fontSize = fontSizeSpinBox->value();
    m_style.textColor = selectedColor.isValid() ? selectedColor : QColor(Qt::black);
    m_style.textAlignment = TextAlignment(Qt::AlignTop | Qt::Alignment(alignmentCombo->currentData().toInt()));
    m_autoResizeToContents = autoResizeCheckBox->isChecked();

    return !text.trimmed().isEmpty();
}

void PDFCreateFreeTextTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    QString text;
    if (!configureFreeText(text))
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());

    QString author = PDFAuthorSettings::getAuthorName();
    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    modifier.getBuilder()->createAnnotationFreeText(page, pageRectangle, author, QString(), text, m_style, m_autoResizeToContents);
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateAnnotationTool::PDFCreateAnnotationTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent)
{

}

void PDFCreateAnnotationTool::updateActions()
{
    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
    }
}

PDFCreateLineTypeTool::PDFCreateLineTypeTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, PDFCreateLineTypeTool::Type type, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr),
    m_styleManager(nullptr),
    m_type(type)
{
    PDFPickTool::Mode mode = (type != Type::Rectangle) ? PDFPickTool::Mode::Points : PDFPickTool::Mode::Rectangles;
    m_pickTool = new PDFPickTool(proxy, mode, this);
    m_pickTool->setSnapToAnnotations(true);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateLineTypeTool::onPointPicked);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateLineTypeTool::onRectanglePicked);
    m_pickTool->setDrawSelectionRectangle(false);

    m_styleManager = new PDFAnnotationStyleManager(this,
                                                   PDFAnnotationStyleWidget::StrokeColor | PDFAnnotationStyleWidget::FillColor | PDFAnnotationStyleWidget::PenWidth,
                                                   getDefaultShapeStyle(),
                                                   PDFAnnotationStyleSettings::STYLE_SHAPE);
    connect(m_styleManager, &PDFAnnotationStyleManager::styleChanged, this, &PDFCreateLineTypeTool::onStyleChanged);

    updateActions();
}

void PDFCreateLineTypeTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        Q_EMIT messageDisplayRequest(tr("Use key 'C' to show/hide large cross. Use key 'O' to switch on/off orthogonal mode."), 25000);
        m_styleManager->showStyleWindow(getProxy()->getWidget());
    }
    else
    {
        m_styleManager->closeStyleWindow();
    }
}

void PDFCreateLineTypeTool::onStyleChanged(const PDFAnnotationStyle& style)
{
    Q_UNUSED(style);
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateLineTypeTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(pagePoint);

    if (isOrthogonalMode())
    {
        m_pickTool->makeLastPointOrthogonal();
    }

    if (m_type == Type::Line && m_pickTool->getPickedPoints().size() == 2)
    {
        finishDefinition();
    }
}

void PDFCreateLineTypeTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    m_rectPageIndex = pageIndex;
    m_rectOnPage = pageRectangle;

    finishDefinition();
}

void PDFCreateLineTypeTool::finishDefinition()
{
    const std::vector<QPointF>& pickedPoints = m_pickTool->getPickedPoints();

    const PDFAnnotationStyle& style = m_styleManager->getStyle();
    const PDFReal penWidth = style.penWidth;
    const QColor strokeColor = style.strokeColor;
    const QColor fillColor = getAnnotationFillColor(style);

    switch (m_type)
    {
        case Type::Line:
        {
            if (pickedPoints.size() >= 2)
            {
                PDFDocumentModifier modifier(getDocument());

                QString author = PDFAuthorSettings::getAuthorName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                modifier.getBuilder()->createAnnotationLine(page, QRectF(), pickedPoints.front(), pickedPoints.back(), penWidth, fillColor, strokeColor, author, QString(), QString(), AnnotationLineEnding::None, AnnotationLineEnding::None);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        case Type::PolyLine:
        {
            if (pickedPoints.size() >= 3)
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                for (const QPointF& point : pickedPoints)
                {
                    polygon << point;
                }

                QString author = PDFAuthorSettings::getAuthorName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                modifier.getBuilder()->createAnnotationPolyline(page, polygon, penWidth, fillColor, strokeColor, author, QString(), QString(), AnnotationLineEnding::None, AnnotationLineEnding::None);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        case Type::Polygon:
        {
            if (pickedPoints.size() >= 3)
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                for (const QPointF& point : pickedPoints)
                {
                    polygon << point;
                }
                if (!polygon.isClosed())
                {
                    polygon << pickedPoints.front();
                }

                QString author = PDFAuthorSettings::getAuthorName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                PDFObjectReference annotation = modifier.getBuilder()->createAnnotationPolygon(page, polygon, penWidth, fillColor, strokeColor, author, QString(), QString());
                modifier.getBuilder()->setAnnotationFillOpacity(annotation, fillColor.alphaF());
                modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        case Type::Rectangle:
        {
            if (!m_rectOnPage.isEmpty())
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                polygon << m_rectOnPage.topLeft();
                polygon << m_rectOnPage.topRight();
                polygon << m_rectOnPage.bottomRight();
                polygon << m_rectOnPage.bottomLeft();
                polygon << m_rectOnPage.topLeft();

                QString author = PDFAuthorSettings::getAuthorName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pickTool->getPageIndex())->getPageReference();
                PDFObjectReference annotation = modifier.getBuilder()->createAnnotationPolygon(page, polygon, penWidth, fillColor, strokeColor, author, QString(), QString());
                modifier.getBuilder()->setAnnotationFillOpacity(annotation, fillColor.alphaF());
                modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    m_pickTool->resetTool();
}

QColor PDFCreateLineTypeTool::getFillColor() const
{
    return m_styleManager->getStyle().fillColor;
}

void PDFCreateLineTypeTool::setFillColor(const QColor& fillColor)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.fillColor = fillColor;
    m_styleManager->setStyle(style);
}

bool PDFCreateLineTypeTool::canHaveOrthogonalMode() const
{
    switch (m_type)
    {
    case Type::Line:
    case Type::PolyLine:
    case Type::Polygon:
        return true;

    case Type::Rectangle:
        break;
    }

    return false;
}

bool PDFCreateLineTypeTool::isOrthogonalMode() const
{
    return canHaveOrthogonalMode() && m_orthogonalMode;
}

QColor PDFCreateLineTypeTool::getStrokeColor() const
{
    return m_styleManager->getStyle().strokeColor;
}

void PDFCreateLineTypeTool::setStrokeColor(const QColor& strokeColor)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.strokeColor = strokeColor;
    m_styleManager->setStyle(style);
}

PDFReal PDFCreateLineTypeTool::getPenWidth() const
{
    return m_styleManager->getStyle().penWidth;
}

void PDFCreateLineTypeTool::setPenWidth(PDFReal penWidth)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.penWidth = penWidth;
    m_styleManager->setStyle(style);
}

void PDFCreateLineTypeTool::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (event->key() == Qt::Key_O && canHaveOrthogonalMode())
    {
        m_orthogonalMode = !m_orthogonalMode;
        Q_EMIT getProxy()->repaintNeeded();

        if (m_orthogonalMode)
        {
            Q_EMIT messageDisplayRequest(tr("Orthogonal mode is enabled."), 5000);
        }
        else
        {
            Q_EMIT messageDisplayRequest(tr("Orthogonal mode is disabled."), 5000);
        }
    }

    switch (m_type)
    {
        case Type::PolyLine:
        case Type::Polygon:
        {
            if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
            {
                finishDefinition();
                event->accept();
            }
            else
            {
                event->ignore();
            }

            break;
        }

        default:
            event->ignore();
            break;
    }

    if (!event->isAccepted())
    {
        BaseClass::keyPressEvent(widget, event);
    }
}

void PDFCreateLineTypeTool::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    switch (m_type)
    {
        case Type::PolyLine:
        case Type::Polygon:
        {
            if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return)
            {
                event->accept();
            }
            else
            {
                event->ignore();
            }

            break;
        }

        default:
            event->ignore();
            break;
    }

    if (!event->isAccepted())
    {
        BaseClass::keyReleaseEvent(widget, event);
    }
}

void PDFCreateLineTypeTool::drawPage(QPainter* painter,
                                     PDFInteger pageIndex,
                                     const PDFPrecompiledPage* compiledPage,
                                     PDFTextLayoutGetter& layoutGetter,
                                     const QTransform& pagePointToDevicePointMatrix,
                                     const PDFColorConvertor& convertor,
                                     QList<PDFRenderError>& errors) const
{
    Q_UNUSED(pageIndex);
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pickTool->getPageIndex())
    {
        return;
    }

    const std::vector<QPointF>& points = m_pickTool->getPickedPoints();
    if (points.empty())
    {
        return;
    }

    QPointF snappedPoint = m_pickTool->getSnappedPoint();
    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(snappedPoint);

    if (isOrthogonalMode())
    {
        mousePoint = m_pickTool->getOrthogonalPoint(mousePoint);
    }

    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);

    const PDFAnnotationStyle& style = m_styleManager->getStyle();
    QPen pen = convertor.convert(QPen(style.strokeColor));
    QBrush brush = convertor.convert(QBrush(getAnnotationFillColor(style), Qt::SolidPattern));
    pen.setWidthF(style.penWidth);
    painter->setPen(qMove(pen));
    painter->setBrush(qMove(brush));
    painter->setRenderHint(QPainter::Antialiasing);

    switch (m_type)
    {
        case Type::Line:
        case Type::PolyLine:
        {
            for (size_t i = 1; i < points.size(); ++i)
            {
                painter->drawLine(points[i - 1], points[i]);
            }
            painter->drawLine(points.back(), mousePoint);
            break;
        }

        case Type::Polygon:
        {
            QPainterPath path;
            path.moveTo(points.front());
            for (size_t i = 1; i < points.size(); ++i)
            {
                path.lineTo(points[i]);
            }
            path.lineTo(mousePoint);
            path.closeSubpath();

            painter->drawPath(path);
            break;
        }

        case Type::Rectangle:
        {
            QPointF startPoint = points.front();
            qreal x1 = qMin(startPoint.x(), mousePoint.x());
            qreal y1 = qMin(startPoint.y(), mousePoint.y());
            qreal x2 = qMax(startPoint.x(), mousePoint.x());
            qreal y2 = qMax(startPoint.y(), mousePoint.y());

            QRectF rect(x1, y1, x2 - x1, y2 - y1);
            painter->drawRect(rect);
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

PDFCreateEllipseTool::PDFCreateEllipseTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr),
    m_styleManager(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    m_pickTool->setDrawSelectionRectangle(false);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateEllipseTool::onRectanglePicked);

    m_styleManager = new PDFAnnotationStyleManager(this,
                                                   PDFAnnotationStyleWidget::StrokeColor | PDFAnnotationStyleWidget::FillColor | PDFAnnotationStyleWidget::PenWidth,
                                                   getDefaultShapeStyle(),
                                                   PDFAnnotationStyleSettings::STYLE_SHAPE);
    connect(m_styleManager, &PDFAnnotationStyleManager::styleChanged, this, &PDFCreateEllipseTool::onStyleChanged);

    updateActions();
}

void PDFCreateEllipseTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        m_styleManager->showStyleWindow(getProxy()->getWidget());
    }
    else
    {
        m_styleManager->closeStyleWindow();
    }
}

void PDFCreateEllipseTool::onStyleChanged(const PDFAnnotationStyle& style)
{
    Q_UNUSED(style);
    Q_EMIT getProxy()->repaintNeeded();
}

PDFReal PDFCreateEllipseTool::getPenWidth() const
{
    return m_styleManager->getStyle().penWidth;
}

void PDFCreateEllipseTool::setPenWidth(PDFReal penWidth)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.penWidth = penWidth;
    m_styleManager->setStyle(style);
}

QColor PDFCreateEllipseTool::getStrokeColor() const
{
    return m_styleManager->getStyle().strokeColor;
}

void PDFCreateEllipseTool::setStrokeColor(const QColor& strokeColor)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.strokeColor = strokeColor;
    m_styleManager->setStyle(style);
}

QColor PDFCreateEllipseTool::getFillColor() const
{
    return m_styleManager->getStyle().fillColor;
}

void PDFCreateEllipseTool::setFillColor(const QColor& fillColor)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.fillColor = fillColor;
    m_styleManager->setStyle(style);
}

void PDFCreateEllipseTool::drawPage(QPainter* painter,
                                    PDFInteger pageIndex,
                                    const PDFPrecompiledPage* compiledPage,
                                    PDFTextLayoutGetter& layoutGetter,
                                    const QTransform& pagePointToDevicePointMatrix,
                                    const PDFColorConvertor& convertor,
                                    QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pickTool->getPageIndex())
    {
        return;
    }

    const std::vector<QPointF>& points = m_pickTool->getPickedPoints();
    if (points.empty())
    {
        return;
    }

    QPointF mousePoint = pagePointToDevicePointMatrix.inverted().map(m_pickTool->getSnappedPoint());

    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);

    const PDFAnnotationStyle& style = m_styleManager->getStyle();
    QPen pen = convertor.convert(QPen(style.strokeColor));
    QBrush brush = convertor.convert(QBrush(getAnnotationFillColor(style), Qt::SolidPattern));
    pen.setWidthF(style.penWidth);
    painter->setPen(qMove(pen));
    painter->setBrush(qMove(brush));
    painter->setRenderHint(QPainter::Antialiasing);

    QPointF point = points.front();
    qreal xMin = qMin(point.x(), mousePoint.x());
    qreal xMax = qMax(point.x(), mousePoint.x());
    qreal yMin = qMin(point.y(), mousePoint.y());
    qreal yMax = qMax(point.y(), mousePoint.y());
    qreal width = xMax - xMin;
    qreal height = yMax - yMin;

    if (!qFuzzyIsNull(width) && !qFuzzyIsNull(height))
    {
        QRectF rect(xMin, yMin, width, height);
        painter->drawEllipse(rect);
    }
}

void PDFCreateEllipseTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());

    QString author = PDFAuthorSettings::getAuthorName();
    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    const PDFAnnotationStyle& style = m_styleManager->getStyle();
    const QColor fillColor = getAnnotationFillColor(style);
    PDFObjectReference annotation = modifier.getBuilder()->createAnnotationCircle(page, pageRectangle, style.penWidth, fillColor, style.strokeColor, author, QString(), QString());
    modifier.getBuilder()->setAnnotationFillOpacity(annotation, fillColor.alphaF());
    modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateFreehandCurveTool::PDFCreateFreehandCurveTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_styleManager(nullptr),
    m_pageIndex(-1)
{
    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = Qt::red;
    defaultStyle.fillColor = QColor();
    defaultStyle.penWidth = 1.0;

    m_styleManager = new PDFAnnotationStyleManager(this,
                                                   PDFAnnotationStyleWidget::StrokeColor | PDFAnnotationStyleWidget::PenWidth,
                                                   defaultStyle,
                                                   PDFAnnotationStyleSettings::STYLE_FREEHAND);
    connect(m_styleManager, &PDFAnnotationStyleManager::styleChanged, this, [this]() { Q_EMIT getProxy()->repaintNeeded(); });
}

void PDFCreateFreehandCurveTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        m_styleManager->showStyleWindow(getProxy()->getWidget());
    }
    else
    {
        m_styleManager->closeStyleWindow();
    }
}

void PDFCreateFreehandCurveTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    BaseClass::drawPage(painter, pageIndex, compiledPage, layoutGetter, pagePointToDevicePointMatrix, convertor, errors);

    if (pageIndex != m_pageIndex || m_pickedPoints.empty())
    {
        return;
    }

    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);

    const PDFAnnotationStyle& style = m_styleManager->getStyle();
    QPen pen = convertor.convert(QPen(style.strokeColor));
    pen.setWidthF(style.penWidth);
    painter->setPen(qMove(pen));
    painter->setRenderHint(QPainter::Antialiasing);

    for (size_t i = 1; i < m_pickedPoints.size(); ++i)
    {
        painter->drawLine(m_pickedPoints[i - 1], m_pickedPoints[i]);
    }
}

void PDFCreateFreehandCurveTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        // Try to perform pick point
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1 &&    // We have picked some point on page
                (m_pageIndex == -1 || m_pageIndex == pageIndex)) // We are under current page
        {
            m_pageIndex = pageIndex;
            m_pickedPoints.push_back(pagePoint);
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        resetTool();
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateFreehandCurveTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->button() == Qt::LeftButton)
    {
        // Try to perform pick point
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1 &&    // We have picked some point on page
                (m_pageIndex == pageIndex)) // We are under current page
        {
            m_pageIndex = pageIndex;
            m_pickedPoints.push_back(pagePoint);

            if (m_pickedPoints.size() >= 3)
            {
                PDFDocumentModifier modifier(getDocument());

                QPolygonF polygon;
                for (const QPointF& point : m_pickedPoints)
                {
                    polygon << point;
                }

                QString author = PDFAuthorSettings::getAuthorName();
                PDFObjectReference page = getDocument()->getCatalog()->getPage(m_pageIndex)->getPageReference();
                const PDFAnnotationStyle& style = m_styleManager->getStyle();
                modifier.getBuilder()->createAnnotationPolyline(page, polygon, style.penWidth, Qt::black, style.strokeColor, author, QString(), QString(), AnnotationLineEnding::None, AnnotationLineEnding::None);
                modifier.markAnnotationsChanged();

                if (modifier.finalize())
                {
                    Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                }

                setActive(false);
            }
        }

        resetTool();
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateFreehandCurveTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);
    event->accept();

    if (event->buttons() & Qt::LeftButton && m_pageIndex != -1)
    {
        // Try to add point to the path
        QPointF pagePoint;
        PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex == m_pageIndex)
        {
            m_pickedPoints.push_back(pagePoint);
        }

        getProxy()->repaintNeeded();
    }
}

PDFReal PDFCreateFreehandCurveTool::getPenWidth() const
{
    return m_styleManager->getStyle().penWidth;
}

void PDFCreateFreehandCurveTool::setPenWidth(const PDFReal& penWidth)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.penWidth = penWidth;
    m_styleManager->setStyle(style);
}

QColor PDFCreateFreehandCurveTool::getStrokeColor() const
{
    return m_styleManager->getStyle().strokeColor;
}

void PDFCreateFreehandCurveTool::setStrokeColor(const QColor& strokeColor)
{
    PDFAnnotationStyle style = m_styleManager->getStyle();
    style.strokeColor = strokeColor;
    m_styleManager->setStyle(style);
}

void PDFCreateFreehandCurveTool::resetTool()
{
    m_pageIndex = -1;
    m_pickedPoints.clear();
}

PDFCreateStampTool::PDFCreateStampTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_pageIndex(-1),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Points, this);
    m_pickTool->setSnapToAnnotations(true);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::pointPicked, this, &PDFCreateStampTool::onPointPicked);
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateStampTool::onActionTriggered);

    m_stampAnnotation.setStrokingOpacity(0.5);
    m_stampAnnotation.setFillingOpacity(0.5);

    updateActions();
}

void PDFCreateStampTool::drawPage(QPainter* painter,
                                  PDFInteger pageIndex,
                                  const PDFPrecompiledPage* compiledPage,
                                  PDFTextLayoutGetter& layoutGetter,
                                  const QTransform& pagePointToDevicePointMatrix,
                                  const PDFColorConvertor& convertor,
                                  QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(pagePointToDevicePointMatrix);
    Q_UNUSED(errors);

    if (pageIndex != m_pageIndex)
    {
        return;
    }

    const PDFPage* page = getDocument()->getCatalog()->getPage(pageIndex);
    QRectF rectangle = m_stampAnnotation.getRectangle();
    QTransform matrix = getProxy()->getAnnotationManager()->prepareTransformations(pagePointToDevicePointMatrix, painter->device(), m_stampAnnotation.getFlags(), page, rectangle);
    painter->setWorldTransform(QTransform(matrix), true);

    AnnotationDrawParameters parameters;
    parameters.painter = painter;
    parameters.annotation = const_cast<PDFStampAnnotation*>(&m_stampAnnotation);
    parameters.key.first = PDFAppeareanceStreams::Appearance::Normal;
    parameters.colorConvertor = convertor;
    PDFRenderer::applyFeaturesToColorConvertor(getProxy()->getFeatures(), parameters.colorConvertor);

    m_stampAnnotation.draw(parameters);
}

void PDFCreateStampTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    BaseClass::mouseMoveEvent(widget, event);

    // Try to add point to the path
    QPointF pagePoint;
    m_pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    if (m_pageIndex != -1)
    {
        m_stampAnnotation.setRectangle(QRectF(pagePoint, QSizeF(0, 0)));
    }
}

void PDFCreateStampTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateStampTool::onActionTriggered(QAction* action)
{
    setActive(action && action->isChecked());

    if (action)
    {
        m_stampAnnotation.setStamp(static_cast<Stamp>(action->data().toInt()));
    }
}

void PDFCreateStampTool::onPointPicked(PDFInteger pageIndex, QPointF pagePoint)
{
    PDFDocumentModifier modifier(getDocument());

    QString author = PDFAuthorSettings::getAuthorName();
    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    modifier.getBuilder()->createAnnotationStamp(page, QRectF(pagePoint, QSizeF(0, 0)), m_stampAnnotation.getStamp(), author, QString(), QString());
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    setActive(false);
}

PDFCreateHighlightTextTool::PDFCreateHighlightTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent) :
    BaseClass(proxy, parent),
    m_toolManager(toolManager),
    m_actionGroup(actionGroup),
    m_styleManager(nullptr),
    m_type(AnnotationType::Highlight),
    m_isCursorOverText(false)
{
    connect(m_actionGroup, &QActionGroup::triggered, this, &PDFCreateHighlightTextTool::onActionTriggered);

    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = getDefaultColor();
    defaultStyle.fillColor = QColor();

    m_styleManager = new PDFAnnotationStyleManager(this, PDFAnnotationStyleWidget::StrokeColor, defaultStyle, getStyleId());
    connect(m_styleManager, &PDFAnnotationStyleManager::styleChanged, this, &PDFCreateHighlightTextTool::onStyleChanged);
    m_color = m_styleManager->getStyle().strokeColor;

    updateActions();
}

void PDFCreateHighlightTextTool::drawPage(QPainter* painter,
                                          PDFInteger pageIndex,
                                          const PDFPrecompiledPage* compiledPage,
                                          PDFTextLayoutGetter& layoutGetter,
                                          const QTransform& pagePointToDevicePointMatrix,
                                          const PDFColorConvertor& convertor,
                                          QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);
    Q_UNUSED(convertor);

    pdf::PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix, convertor);
}

void PDFCreateHighlightTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1)
        {
            m_selectionInfo.pageIndex = pageIndex;
            m_selectionInfo.selectionStartPoint = pagePoint;
            event->accept();
        }
        else
        {
            m_selectionInfo = SelectionInfo();
        }

        setSelection(pdf::PDFTextSelection());
        updateCursor();
    }
}

void PDFCreateHighlightTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        if (m_selectionInfo.pageIndex != -1)
        {
            QPointF pagePoint;
            const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

            if (m_selectionInfo.pageIndex == pageIndex)
            {
                // Jakub Melka: handle the selection
                PDFTextLayoutGetter textLayoutGetter = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
                PDFTextLayout textLayout = textLayoutGetter;
                setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, m_color));

                QPolygonF quadrilaterals;
                PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
                QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

                if (!path.isEmpty())
                {
                    PDFDocumentModifier modifier(getDocument());

                    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
                    PDFObjectReference annotationReference;
                    switch (m_type)
                    {
                        case AnnotationType::Highlight:
                            annotationReference = modifier.getBuilder()->createAnnotationHighlight(page, quadrilaterals, m_color);
                            modifier.getBuilder()->setAnnotationOpacity(annotationReference, 0.2);
                            modifier.getBuilder()->updateAnnotationAppearanceStreams(annotationReference);
                            break;

                        case AnnotationType::Underline:
                            annotationReference = modifier.getBuilder()->createAnnotationUnderline(page, quadrilaterals, m_color);
                            break;

                        case AnnotationType::Squiggly:
                            annotationReference = modifier.getBuilder()->createAnnotationSquiggly(page, quadrilaterals, m_color);
                            break;

                        case AnnotationType::StrikeOut:
                            annotationReference = modifier.getBuilder()->createAnnotationStrikeout(page, quadrilaterals, m_color);
                            break;

                        default:
                            Q_ASSERT(false);
                            break;
                    }

                    modifier.markAnnotationsChanged();

                    if (modifier.finalize())
                    {
                        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                    }
                }
            }

            setSelection(pdf::PDFTextSelection());

            m_selectionInfo = SelectionInfo();
            event->accept();
            updateCursor();
        }
    }
}

void PDFCreateHighlightTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
    m_isCursorOverText = textLayout.isHoveringOverTextBlock(pagePoint);

    if (m_selectionInfo.pageIndex != -1)
    {
        if (m_selectionInfo.pageIndex == pageIndex)
        {
            // Jakub Melka: handle the selection
            setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, m_color));
        }
        else
        {
            setSelection(pdf::PDFTextSelection());
        }

        event->accept();
    }

    updateCursor();
}

void PDFCreateHighlightTextTool::updateActions()
{
    BaseClass::updateActions();

    if (m_actionGroup)
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        m_actionGroup->setEnabled(isEnabled);

        if (!isActive() && m_actionGroup->checkedAction())
        {
            m_actionGroup->checkedAction()->setChecked(false);
        }
    }
}

void PDFCreateHighlightTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        // Just clear the text selection
        setSelection(PDFTextSelection());

        m_styleManager->closeStyleWindow();
    }
    else
    {
        // Jakub Melka: text layout of the document must be created, otherwise it would
        // be recreated page by page during the selection (on each mouse move).
        pdf::PDFAsynchronousTextLayoutCompiler* compiler = getProxy()->getTextLayoutCompiler();
        if (!compiler->isTextLayoutReady())
        {
            compiler->makeTextLayout();
        }

        m_styleManager->showStyleWindow(getProxy()->getWidget());
    }
}

void PDFCreateHighlightTextTool::onActionTriggered(QAction* action)
{
    if (action)
    {
        AnnotationType type = static_cast<AnnotationType>(action->data().toInt());

        if (m_type != type)
        {
            m_type = type;

            // Each kind of the text markup has its own persisted style, so the color
            // selected by the user for the highlight is not lost, when he switches
            // to the underline and back.
            PDFAnnotationStyle defaultStyle;
            defaultStyle.strokeColor = getDefaultColor();
            defaultStyle.fillColor = QColor();
            m_styleManager->setStyleId(getStyleId(), defaultStyle);
        }
    }

    setActive(action && action->isChecked());
}

void PDFCreateHighlightTextTool::onStyleChanged(const PDFAnnotationStyle& style)
{
    m_color = style.strokeColor;

    if (!m_textSelection.isEmpty())
    {
        // Recreate the selection, so it is drawn using the new color
        setSelection(PDFTextSelection());
    }

    Q_EMIT getProxy()->repaintNeeded();
}

QString PDFCreateHighlightTextTool::getStyleId() const
{
    switch (m_type)
    {
        case AnnotationType::Highlight:
            return PDFAnnotationStyleSettings::STYLE_HIGHLIGHT;

        case AnnotationType::Underline:
            return PDFAnnotationStyleSettings::STYLE_UNDERLINE;

        case AnnotationType::Squiggly:
            return PDFAnnotationStyleSettings::STYLE_SQUIGGLY;

        case AnnotationType::StrikeOut:
            return PDFAnnotationStyleSettings::STYLE_STRIKEOUT;

        default:
            Q_ASSERT(false);
            break;
    }

    return PDFAnnotationStyleSettings::STYLE_HIGHLIGHT;
}

QColor PDFCreateHighlightTextTool::getDefaultColor() const
{
    switch (m_type)
    {
        case AnnotationType::Highlight:
            return QColor(Qt::yellow);

        case AnnotationType::Underline:
            return QColor(Qt::black);

        case AnnotationType::Squiggly:
        case AnnotationType::StrikeOut:
            return QColor(Qt::red);

        default:
            Q_ASSERT(false);
            break;
    }

    return QColor(Qt::yellow);
}

void PDFCreateHighlightTextTool::updateCursor()
{
    if (isActive())
    {
        if (m_isCursorOverText)
        {
            setCursor(QCursor(Qt::IBeamCursor));
        }
        else
        {
            setCursor(QCursor(Qt::ArrowCursor));
        }
    }
}

void PDFCreateHighlightTextTool::setSelection(PDFTextSelection&& textSelection)
{
    if (m_textSelection != textSelection)
    {
        m_textSelection = qMove(textSelection);
        Q_EMIT getProxy()->repaintNeeded();
    }
}

PDFCreateRedactRectangleTool::PDFCreateRedactRectangleTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr),
    m_styleManager(nullptr),
    m_color(getRedactColor())
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    m_pickTool->setSelectionRectangleColor(m_color);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateRedactRectangleTool::onRectanglePicked);

    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = m_color;
    defaultStyle.fillColor = QColor();

    m_styleManager = new PDFAnnotationStyleManager(this, PDFAnnotationStyleWidget::StrokeColor, defaultStyle, PDFAnnotationStyleSettings::STYLE_REDACT);
    connect(m_styleManager, &PDFAnnotationStyleManager::styleChanged, this, &PDFCreateRedactRectangleTool::onStyleChanged);

    updateActions();
}

QColor PDFCreateRedactRectangleTool::getRedactColor()
{
    // Jakub Melka: the redaction color was originally stored in a separate group
    // of the settings. Value stored by the older versions of the application is
    // used as a default value, so the choice of the user is not lost.
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("RedactTool");
    QColor legacyColor = settings.value("redactColor", QColor(Qt::black)).value<QColor>();
    settings.endGroup();

    if (!legacyColor.isValid())
    {
        legacyColor = QColor(Qt::black);
    }

    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = legacyColor;
    defaultStyle.fillColor = QColor();

    const QColor color = PDFAnnotationStyleSettings::getStyle(PDFAnnotationStyleSettings::STYLE_REDACT, defaultStyle).strokeColor;
    return color.isValid() ? color : QColor(Qt::black);
}

void PDFCreateRedactRectangleTool::setRedactColor(const QColor& color)
{
    PDFAnnotationStyle style = PDFAnnotationStyleSettings::getStyle(PDFAnnotationStyleSettings::STYLE_REDACT, PDFAnnotationStyle());
    style.strokeColor = color;
    style.fillColor = QColor();
    PDFAnnotationStyleSettings::setStyle(PDFAnnotationStyleSettings::STYLE_REDACT, style);
}

void PDFCreateRedactRectangleTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        m_styleManager->closeStyleWindow();
    }
    else
    {
        m_color = m_styleManager->getStyle().strokeColor;
        m_pickTool->setSelectionRectangleColor(m_color);
        m_styleManager->showStyleWindow(getProxy()->getWidget());
    }
}

void PDFCreateRedactRectangleTool::onStyleChanged(const PDFAnnotationStyle& style)
{
    m_color = style.strokeColor;
    m_pickTool->setSelectionRectangleColor(m_color);
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateRedactRectangleTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty())
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());

    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
    PDFObjectReference annotation = modifier.getBuilder()->createAnnotationRedact(page, pageRectangle, m_color);
    modifier.getBuilder()->updateAnnotationAppearanceStreams(annotation);
    modifier.markAnnotationsChanged();

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

PDFCreateInsertPageNumbersTool::PDFCreateInsertPageNumbersTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_pickTool(nullptr)
{
    m_pickTool = new PDFPickTool(proxy, PDFPickTool::Mode::Rectangles, this);
    m_pickTool->setSnapToAnnotations(true);
    m_pickTool->setSelectionRectangleColor(Qt::black);
    addTool(m_pickTool);
    connect(m_pickTool, &PDFPickTool::rectanglePicked, this, &PDFCreateInsertPageNumbersTool::onRectanglePicked);

    updateActions();
}

void PDFCreateInsertPageNumbersTool::onRectanglePicked(PDFInteger pageIndex, QRectF pageRectangle)
{
    if (pageRectangle.isEmpty() || !getDocument())
    {
        return;
    }

    const PDFPage* referencePage = getDocument()->getCatalog()->getPage(pageIndex);
    const QRectF referenceMediaBox = referencePage->getMediaBox();

    // Anchor the picked rectangle to its nearest media box corner, so the same
    // relative position (and rectangle size) can be reproduced on pages whose
    // media box differs in size from the reference page.
    const bool anchorLeft = std::abs(pageRectangle.center().x() - referenceMediaBox.left()) <= std::abs(referenceMediaBox.right() - pageRectangle.center().x());
    const bool anchorTop = std::abs(pageRectangle.center().y() - referenceMediaBox.top()) <= std::abs(referenceMediaBox.bottom() - pageRectangle.center().y());

    const PDFReal offsetX = anchorLeft ? (pageRectangle.left() - referenceMediaBox.left()) : (referenceMediaBox.right() - pageRectangle.right());
    const PDFReal offsetY = anchorTop ? (pageRectangle.top() - referenceMediaBox.top()) : (referenceMediaBox.bottom() - pageRectangle.bottom());
    const PDFReal width = pageRectangle.width();
    const PDFReal height = pageRectangle.height();

    std::vector<PDFInteger> visiblePages;
    if (IDrawWidget* drawWidget = getProxy()->getWidget()->getDrawWidget())
    {
        visiblePages = drawWidget->getCurrentPages();
    }

    InsertPageNumbersDialog dialog(getDocument()->getCatalog()->getPageCount(), visiblePages, getProxy()->getWidget());
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::vector<PDFInteger> selectedPages = dialog.getSelectedPages();
    std::sort(selectedPages.begin(), selectedPages.end());

    const PDFPageLabel::NumberingStyle numberingStyle = dialog.getNumberingStyle();
    const QString formatPattern = dialog.getFormatPattern();
    const int startNumber = dialog.getStartNumber();
    const QFont font = dialog.getFont();
    const QColor color = dialog.getColor();
    const Qt::Alignment alignment = dialog.getAlignment();
    const QString totalPagesText = QString::number(getDocument()->getCatalog()->getPageCount());

    PDFDocumentModifier modifier(getDocument());

    for (size_t i = 0; i < selectedPages.size(); ++i)
    {
        const PDFInteger targetPageIndex = selectedPages[i] - 1;
        const PDFPage* targetPage = getDocument()->getCatalog()->getPage(targetPageIndex);
        const QRectF targetMediaBox = targetPage->getMediaBox();

        const PDFReal x = anchorLeft ? (targetMediaBox.left() + offsetX) : (targetMediaBox.right() - offsetX - width);
        const PDFReal y = anchorTop ? (targetMediaBox.top() + offsetY) : (targetMediaBox.bottom() - offsetY - height);

        QRectF targetRect;
        targetRect.setLeft(x);
        targetRect.setTop(y);
        targetRect.setWidth(width);
        targetRect.setHeight(height);

        const QString numberText = PDFPageLabel::formatPageNumber(numberingStyle, startNumber + PDFInteger(i));
        const QString labelText = QString(formatPattern).arg(numberText).arg(totalPagesText);

        PDFPageContentStreamBuilder contentStreamBuilder(modifier.getBuilder(), PDFContentStreamBuilder::CoordinateSystem::PDF, PDFPageContentStreamBuilder::Mode::PlaceAfter);
        QPainter* painter = contentStreamBuilder.begin(targetPage->getPageReference());
        if (painter)
        {
            painter->setFont(font);
            painter->setPen(QPen(color));

            // CoordinateSystem::PDF flips the painter's y-axis to match PDF space;
            // counteract it locally around the text, otherwise glyphs draw mirrored.
            painter->save();
            painter->translate(targetRect.center());
            painter->scale(1.0, -1.0);
            QRectF localRect(-targetRect.width() * 0.5, -targetRect.height() * 0.5, targetRect.width(), targetRect.height());
            painter->drawText(localRect, int(alignment), labelText);
            painter->restore();

            contentStreamBuilder.end(painter);
            modifier.markPageContentsChanged();
        }
    }

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }

    // Deactivate the tool after stamping, so an accidental extra rectangle pick
    // does not stamp the already numbered document a second time.
    setActive(false);
}

PDFDeleteAnnotationTool::PDFDeleteAnnotationTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_selectionPageIndex(-1),
    m_currentPageIndex(-1),
    m_isCursorOverAnnotation(false)
{
    updateActions();
}

void PDFDeleteAnnotationTool::drawPage(QPainter* painter,
                                       PDFInteger pageIndex,
                                       const PDFPrecompiledPage* compiledPage,
                                       PDFTextLayoutGetter& layoutGetter,
                                       const QTransform& pagePointToDevicePointMatrix,
                                       const PDFColorConvertor& convertor,
                                       QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    const std::vector<AnnotationInfo> markedAnnotations = getMarkedAnnotations();

    painter->save();
    painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix), true);
    painter->setRenderHint(QPainter::Antialiasing);

    // Mark the annotations, which will be deleted
    QColor markColor = convertor.convert(QColor(Qt::red), false, false);
    QPen markPen(markColor);
    markPen.setWidthF(0.0);
    markPen.setCosmetic(true);

    QColor fillColor = markColor;
    fillColor.setAlphaF(0.2f);

    painter->setPen(markPen);
    painter->setBrush(QBrush(fillColor));

    for (const AnnotationInfo& annotationInfo : markedAnnotations)
    {
        if (annotationInfo.pageIndex == pageIndex)
        {
            painter->drawRect(annotationInfo.rectangle);
        }
    }

    // Draw the selection rectangle
    const QRectF selectionRectangle = getSelectionRectangle();
    if (selectionRectangle.isValid() && m_selectionPageIndex == pageIndex)
    {
        QPen selectionPen(convertor.convert(QColor(Qt::blue), false, false));
        selectionPen.setCosmetic(true);
        selectionPen.setStyle(Qt::DashLine);
        painter->setPen(selectionPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(selectionRectangle);
    }

    painter->restore();
}

void PDFDeleteAnnotationTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

        if (pageIndex != -1)
        {
            m_selectionPageIndex = pageIndex;
            m_selectionStartPoint = pagePoint;
            m_currentPageIndex = pageIndex;
            m_currentPagePoint = pagePoint;
            event->accept();
        }
        else
        {
            resetTool();
        }

        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFDeleteAnnotationTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton && m_selectionPageIndex != -1)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

        if (pageIndex == m_selectionPageIndex)
        {
            m_currentPagePoint = pagePoint;
            deleteAnnotations(getMarkedAnnotations());
        }

        resetTool();
        event->accept();
        updateCursor();
        Q_EMIT getProxy()->repaintNeeded();
    }
}

void PDFDeleteAnnotationTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

    m_currentPageIndex = pageIndex;
    m_currentPagePoint = pagePoint;

    const std::vector<AnnotationInfo> markedAnnotations = getMarkedAnnotations();
    m_isCursorOverAnnotation = !markedAnnotations.empty();

    if (m_selectionPageIndex != -1)
    {
        event->accept();
    }

    updateCursor();
    Q_EMIT getProxy()->repaintNeeded();
}

void PDFDeleteAnnotationTool::updateActions()
{
    BaseClass::updateActions();

    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
    }
}

void PDFDeleteAnnotationTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (active)
    {
        Q_EMIT messageDisplayRequest(tr("Click on an annotation to delete it. Drag a rectangle to delete all annotations inside it."), 15000);
    }
    else
    {
        resetTool();
    }

    updateCursor();
}

std::vector<PDFDeleteAnnotationTool::AnnotationInfo> PDFDeleteAnnotationTool::getDeletableAnnotations(PDFInteger pageIndex) const
{
    std::vector<AnnotationInfo> result;

    const PDFDocument* document = getDocument();
    PDFWidget* widget = getProxy()->getWidget();

    if (pageIndex == -1 || !document || !widget)
    {
        return result;
    }

    PDFWidgetAnnotationManager* annotationManager = widget->getAnnotationManager();
    if (!annotationManager)
    {
        return result;
    }

    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return result;
    }

    const PDFAnnotationManager::PageAnnotations& pageAnnotations = annotationManager->getPageAnnotations(pageIndex);
    for (const PDFAnnotationManager::PageAnnotation& pageAnnotation : pageAnnotations.annotations)
    {
        const PDFAnnotation* annotation = pageAnnotation.annotation.get();

        // Popup annotations and replies are not deleted directly - they are
        // deleted together with the annotation, which owns them.
        if (!annotation || annotation->isReplyTo() || annotation->getType() == AnnotationType::Popup)
        {
            continue;
        }

        AnnotationInfo annotationInfo;
        annotationInfo.pageIndex = pageIndex;
        annotationInfo.pageReference = page->getPageReference();
        annotationInfo.annotationReference = annotation->getSelfReference();
        annotationInfo.rectangle = annotation->getRectangle().normalized();

        if (annotationInfo.annotationReference.isValid() && annotationInfo.rectangle.isValid())
        {
            result.push_back(annotationInfo);
        }
    }

    return result;
}

std::vector<PDFDeleteAnnotationTool::AnnotationInfo> PDFDeleteAnnotationTool::getMarkedAnnotations() const
{
    std::vector<AnnotationInfo> result;

    const PDFInteger pageIndex = (m_selectionPageIndex != -1) ? m_selectionPageIndex : m_currentPageIndex;
    if (pageIndex == -1)
    {
        return result;
    }

    const QRectF selectionRectangle = getSelectionRectangle();

    for (const AnnotationInfo& annotationInfo : getDeletableAnnotations(pageIndex))
    {
        const bool isMarked = selectionRectangle.isValid() ? selectionRectangle.intersects(annotationInfo.rectangle)
                                                           : annotationInfo.rectangle.contains(m_currentPagePoint);

        if (isMarked)
        {
            result.push_back(annotationInfo);
        }
    }

    return result;
}

void PDFDeleteAnnotationTool::deleteAnnotations(const std::vector<AnnotationInfo>& annotations)
{
    if (annotations.empty())
    {
        return;
    }

    PDFDocumentModifier modifier(getDocument());
    modifier.markAnnotationsChanged();

    for (const AnnotationInfo& annotationInfo : annotations)
    {
        modifier.getBuilder()->removeAnnotation(annotationInfo.pageReference, annotationInfo.annotationReference);
    }

    if (modifier.finalize())
    {
        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

QRectF PDFDeleteAnnotationTool::getSelectionRectangle() const
{
    if (m_selectionPageIndex == -1 || m_currentPageIndex != m_selectionPageIndex)
    {
        return QRectF();
    }

    const QRectF rectangle = QRectF(m_selectionStartPoint, m_currentPagePoint).normalized();

    // A very small rectangle means, that the user just clicked on an annotation.
    // In that case we do not use the rectangle selection at all, because a click
    // must delete the annotation under the cursor, even if the annotation is
    // larger than the (almost empty) rectangle.
    const qreal minimalSize = 2.0;
    if (rectangle.width() < minimalSize && rectangle.height() < minimalSize)
    {
        return QRectF();
    }

    return rectangle;
}

void PDFDeleteAnnotationTool::resetTool()
{
    m_selectionPageIndex = -1;
    m_selectionStartPoint = QPointF();
    m_currentPageIndex = -1;
    m_currentPagePoint = QPointF();
    m_isCursorOverAnnotation = false;
}

void PDFDeleteAnnotationTool::updateCursor()
{
    if (isActive())
    {
        setCursor(QCursor(m_isCursorOverAnnotation ? Qt::PointingHandCursor : Qt::ArrowCursor));
    }
}

PDFCreateRedactTextTool::PDFCreateRedactTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent) :
    BaseClass(proxy, action, parent),
    m_toolManager(toolManager),
    m_styleManager(nullptr),
    m_color(PDFCreateRedactRectangleTool::getRedactColor()),
    m_isCursorOverText(false)
{
    PDFAnnotationStyle defaultStyle;
    defaultStyle.strokeColor = m_color;
    defaultStyle.fillColor = QColor();

    // Jakub Melka: both redaction tools share the same style, so the user doesn't
    // have to set the redaction color twice.
    m_styleManager = new PDFAnnotationStyleManager(this, PDFAnnotationStyleWidget::StrokeColor, defaultStyle, PDFAnnotationStyleSettings::STYLE_REDACT);
    connect(m_styleManager, &PDFAnnotationStyleManager::styleChanged, this, &PDFCreateRedactTextTool::onStyleChanged);

    updateActions();
}

void PDFCreateRedactTextTool::drawPage(QPainter* painter,
                                       PDFInteger pageIndex,
                                       const PDFPrecompiledPage* compiledPage,
                                       PDFTextLayoutGetter& layoutGetter,
                                       const QTransform& pagePointToDevicePointMatrix,
                                       const PDFColorConvertor& convertor,
                                       QList<PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);
    Q_UNUSED(convertor);

    pdf::PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix, convertor);
}

void PDFCreateRedactTextTool::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        QPointF pagePoint;
        const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
        if (pageIndex != -1)
        {
            m_selectionInfo.pageIndex = pageIndex;
            m_selectionInfo.selectionStartPoint = pagePoint;
            event->accept();
        }
        else
        {
            m_selectionInfo = SelectionInfo();
        }

        setSelection(pdf::PDFTextSelection());
        updateCursor();
    }
}

void PDFCreateRedactTextTool::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    if (event->button() == Qt::LeftButton)
    {
        if (m_selectionInfo.pageIndex != -1)
        {
            QPointF pagePoint;
            const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);

            if (m_selectionInfo.pageIndex == pageIndex)
            {
                // Jakub Melka: handle the selection
                PDFTextLayoutGetter textLayoutGetter = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
                PDFTextLayout textLayout = textLayoutGetter;
                setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, m_color));

                QPolygonF quadrilaterals;
                PDFTextSelectionPainter textSelectionPainter(&m_textSelection);
                QPainterPath path = textSelectionPainter.prepareGeometry(pageIndex, textLayoutGetter, QTransform(), &quadrilaterals);

                if (!path.isEmpty())
                {
                    PDFDocumentModifier modifier(getDocument());

                    PDFObjectReference page = getDocument()->getCatalog()->getPage(pageIndex)->getPageReference();
                    modifier.getBuilder()->createAnnotationRedact(page, quadrilaterals, m_color);
                    modifier.markAnnotationsChanged();

                    if (modifier.finalize())
                    {
                        Q_EMIT m_toolManager->documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
                    }
                }
            }

            setSelection(pdf::PDFTextSelection());

            m_selectionInfo = SelectionInfo();
            event->accept();
            updateCursor();
        }
    }
}

void PDFCreateRedactTextTool::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    Q_UNUSED(widget);

    QPointF pagePoint;
    const PDFInteger pageIndex = getProxy()->getPageUnderPoint(event->pos(), &pagePoint);
    PDFTextLayout textLayout = getProxy()->getTextLayoutCompiler()->getTextLayoutLazy(pageIndex);
    m_isCursorOverText = textLayout.isHoveringOverTextBlock(pagePoint);

    if (m_selectionInfo.pageIndex != -1)
    {
        if (m_selectionInfo.pageIndex == pageIndex)
        {
            // Jakub Melka: handle the selection
            setSelection(textLayout.createTextSelection(pageIndex, m_selectionInfo.selectionStartPoint, pagePoint, m_color));
        }
        else
        {
            setSelection(pdf::PDFTextSelection());
        }

        event->accept();
    }

    updateCursor();
}

void PDFCreateRedactTextTool::updateActions()
{
    if (QAction* action = getAction())
    {
        const bool isEnabled = getDocument() && getDocument()->getStorage().getSecurityHandler()->isAllowed(PDFSecurityHandler::Permission::ModifyInteractiveItems);
        action->setChecked(isActive());
        action->setEnabled(isEnabled);
    }
}

void PDFCreateRedactTextTool::setActiveImpl(bool active)
{
    BaseClass::setActiveImpl(active);

    if (!active)
    {
        // Just clear the text selection
        setSelection(PDFTextSelection());

        m_styleManager->closeStyleWindow();
    }
    else
    {
        // Jakub Melka: text layout of the document must be created, otherwise it would
        // be recreated page by page during the selection (on each mouse move).
        pdf::PDFAsynchronousTextLayoutCompiler* compiler = getProxy()->getTextLayoutCompiler();
        if (!compiler->isTextLayoutReady())
        {
            compiler->makeTextLayout();
        }

        m_color = m_styleManager->getStyle().strokeColor;
        m_styleManager->showStyleWindow(getProxy()->getWidget());
    }
}

void PDFCreateRedactTextTool::onStyleChanged(const PDFAnnotationStyle& style)
{
    m_color = style.strokeColor;

    if (!m_textSelection.isEmpty())
    {
        setSelection(PDFTextSelection());
    }

    Q_EMIT getProxy()->repaintNeeded();
}

void PDFCreateRedactTextTool::updateCursor()
{
    if (isActive())
    {
        if (m_isCursorOverText)
        {
            setCursor(QCursor(Qt::IBeamCursor));
        }
        else
        {
            setCursor(QCursor(Qt::ArrowCursor));
        }
    }
}

void PDFCreateRedactTextTool::setSelection(PDFTextSelection&& textSelection)
{
    if (m_textSelection != textSelection)
    {
        m_textSelection = qMove(textSelection);
        getProxy()->repaintNeeded();
    }
}

} // namespace pdf
