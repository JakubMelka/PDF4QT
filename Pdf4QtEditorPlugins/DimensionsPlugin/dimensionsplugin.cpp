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

#include "dimensionsplugin.h"
#include "scaledialog.h"
#include "scalepresetsdialog.h"
#include "settingsdialog.h"

#include "pdfannotation.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawwidget.h"
#include "pdfmeasure.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"

#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QTextStream>

namespace pdfplugin
{

namespace
{

/// Escapes the field so it can be written to a csv file
QString escapeCsvField(const QString& text)
{
    if (text.contains(QChar(';')) || text.contains(QChar('"')) || text.contains(QChar('\n')) || text.contains(QChar('\r')))
    {
        QString escapedText = text;
        escapedText.replace(QChar('"'), QString("\"\""));
        return QString("\"%1\"").arg(escapedText);
    }

    return text;
}

}   // namespace

DimensionsPlugin::DimensionsPlugin() :
    pdf::PDFPlugin(nullptr),
    m_dimensionTools(),
    m_showDimensionsAction(nullptr),
    m_clearDimensionsAction(nullptr),
    m_convertToAnnotationsAction(nullptr),
    m_exportAction(nullptr),
    m_scaleAction(nullptr),
    m_settingsAction(nullptr),
    m_scaleMenu(nullptr)
{

}

void DimensionsPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* horizontalDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/linear-horizontal.svg"), tr("&Horizontal Dimension"), this);
    QAction* verticalDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/linear-vertical.svg"), tr("&Vertical Dimension"), this);
    QAction* linearDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/linear.svg"), tr("&Linear Dimension"), this);
    QAction* perimeterDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/perimeter.svg"), tr("&Perimeter"), this);
    QAction* rectanglePerimeterDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/rectangle-perimeter.svg"), tr("&Rectangle Perimeter"), this);
    QAction* areaDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/area.svg"), tr("&Area"), this);
    QAction* rectangleAreaDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/rectangle-area.svg"), tr("R&ectangle Area"), this);
    QAction* angleAction = new QAction(QIcon(":/pdfplugins/dimensiontool/angle.svg"), tr("An&gle"), this);
    QAction* calibrateAction = new QAction(QIcon(":/pdfplugins/dimensiontool/calibrate.svg"), tr("Cali&brate Scale"), this);

    horizontalDimensionAction->setObjectName("dimensiontool_LinearHorizontalAction");
    verticalDimensionAction->setObjectName("dimensiontool_LinearVerticalAction");
    linearDimensionAction->setObjectName("dimensiontool_LinearAction");
    perimeterDimensionAction->setObjectName("dimensiontool_PerimeterAction");
    rectanglePerimeterDimensionAction->setObjectName("dimensiontool_RectanglePerimeterAction");
    areaDimensionAction->setObjectName("dimensiontool_AreaAction");
    rectangleAreaDimensionAction->setObjectName("dimensiontool_RectangleAreaAction");
    angleAction->setObjectName("dimensiontool_AngleAction");
    calibrateAction->setObjectName("dimensiontool_CalibrateAction");

    horizontalDimensionAction->setCheckable(true);
    verticalDimensionAction->setCheckable(true);
    linearDimensionAction->setCheckable(true);
    perimeterDimensionAction->setCheckable(true);
    rectanglePerimeterDimensionAction->setCheckable(true);
    areaDimensionAction->setCheckable(true);
    rectangleAreaDimensionAction->setCheckable(true);
    angleAction->setCheckable(true);
    calibrateAction->setCheckable(true);

    calibrateAction->setToolTip(tr("Pick two points of a known distance in the document to calculate the scale of the drawing"));

    m_dimensionTools[DimensionTool::LinearHorizontal] = new DimensionTool(DimensionTool::LinearHorizontal, widget->getDrawWidgetProxy(), horizontalDimensionAction, this);
    m_dimensionTools[DimensionTool::LinearVertical] = new DimensionTool(DimensionTool::LinearVertical, widget->getDrawWidgetProxy(), verticalDimensionAction, this);
    m_dimensionTools[DimensionTool::Linear] = new DimensionTool(DimensionTool::Linear, widget->getDrawWidgetProxy(), linearDimensionAction, this);
    m_dimensionTools[DimensionTool::Perimeter] = new DimensionTool(DimensionTool::Perimeter, widget->getDrawWidgetProxy(), perimeterDimensionAction, this);
    m_dimensionTools[DimensionTool::RectanglePerimeter] = new DimensionTool(DimensionTool::RectanglePerimeter, widget->getDrawWidgetProxy(), rectanglePerimeterDimensionAction, this);
    m_dimensionTools[DimensionTool::Area] = new DimensionTool(DimensionTool::Area, widget->getDrawWidgetProxy(), areaDimensionAction, this);
    m_dimensionTools[DimensionTool::RectangleArea] = new DimensionTool(DimensionTool::RectangleArea, widget->getDrawWidgetProxy(), rectangleAreaDimensionAction, this);
    m_dimensionTools[DimensionTool::Angle] = new DimensionTool(DimensionTool::Angle, widget->getDrawWidgetProxy(), angleAction, this);
    m_dimensionTools[DimensionTool::Calibrate] = new DimensionTool(DimensionTool::Calibrate, widget->getDrawWidgetProxy(), calibrateAction, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (DimensionTool* tool : m_dimensionTools)
    {
        toolManager->addTool(tool);
        connect(tool, &DimensionTool::dimensionCreated, this, &DimensionsPlugin::onDimensionCreated);
        connect(tool, &DimensionTool::calibrationLinePicked, this, &DimensionsPlugin::onCalibrationLinePicked);
    }

    m_showDimensionsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/show-dimensions.svg"), tr("&Show Dimensions"), this);
    m_clearDimensionsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/clear-dimensions.svg"), tr("&Clear Dimensions"), this);
    m_convertToAnnotationsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/convert-to-annotations.svg"), tr("Con&vert to Annotations"), this);
    m_exportAction = new QAction(QIcon(":/pdfplugins/dimensiontool/export-measurements.svg"), tr("&Export Measurements..."), this);
    m_scaleAction = new QAction(QIcon(":/pdfplugins/dimensiontool/scale.svg"), tr("Sca&le"), this);
    m_settingsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/settings.svg"), tr("Se&ttings"), this);

    m_showDimensionsAction->setObjectName("dimensiontool_ShowDimensionsAction");
    m_clearDimensionsAction->setObjectName("dimensiontool_ClearDimensionsAction");
    m_convertToAnnotationsAction->setObjectName("dimensiontool_ConvertToAnnotationsAction");
    m_exportAction->setObjectName("dimensiontool_ExportAction");
    m_scaleAction->setObjectName("dimensiontool_ScaleAction");
    m_settingsAction->setObjectName("dimensiontool_SettingsAction");

    m_showDimensionsAction->setCheckable(true);
    m_showDimensionsAction->setChecked(true);

    m_convertToAnnotationsAction->setToolTip(tr("Store the temporary measurements in the document as measurement annotations, "
                                                "so they are saved with it"));

    m_scaleMenu = new QMenu(tr("Scale"), widget);
    m_scaleMenu->setToolTipsVisible(true);
    m_scaleAction->setMenu(m_scaleMenu);
    connect(m_scaleMenu, &QMenu::aboutToShow, this, &DimensionsPlugin::onScaleMenuAboutToShow);

    // In the toolbar the action is displayed as a button, which would do nothing
    // when it is clicked, so the menu with the scales is opened instead
    connect(m_scaleAction, &QAction::triggered, this, [this]() { m_scaleMenu->popup(QCursor::pos()); });

    connect(m_showDimensionsAction, &QAction::triggered, this, &DimensionsPlugin::onShowDimensionsTriggered);
    connect(m_clearDimensionsAction, &QAction::triggered, this, &DimensionsPlugin::onClearDimensionsTriggered);
    connect(m_convertToAnnotationsAction, &QAction::triggered, this, &DimensionsPlugin::onConvertToAnnotationsTriggered);
    connect(m_exportAction, &QAction::triggered, this, &DimensionsPlugin::onExportTriggered);
    connect(m_settingsAction, &QAction::triggered, this, &DimensionsPlugin::onSettingsTriggered);

    m_settingsStorage.load();
    m_scale = getSettings().defaultScale;

    m_widget->getDrawWidgetProxy()->registerDrawInterface(this);

    updateScaleAction();
    updateActions();
}

void DimensionsPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        m_dimensions.clear();

        const QString fileName = m_dataExchangeInterface ? m_dataExchangeInterface->getOriginalFileName() : QString();
        const DocumentIdentity documentIdentity = DocumentIdentity::create(m_document, fileName);

        if (documentIdentity != m_documentIdentity || !documentIdentity.isValid())
        {
            // A different document was opened, so its own scale has to be selected.
            // The scale of the previous document must never be inherited. Two
            // documents, which cannot be identified at all, compare as equal,
            // so they are deliberately treated as two different documents.
            m_documentIdentity = documentIdentity;
            applyDocumentScale();
        }

        updateActions();
    }
}

std::vector<QAction*> DimensionsPlugin::getActions() const
{
    std::vector<QAction*> result;

    for (size_t i = 0; i < m_dimensionTools.size(); ++i)
    {
        if (i == size_t(DimensionTool::Calibrate))
        {
            // Calibration is not a measurement, it is displayed together
            // with the scale action
            continue;
        }

        if (DimensionTool* tool = m_dimensionTools[i])
        {
            result.push_back(tool->getAction());
        }
    }

    if (!result.empty())
    {
        result.push_back(nullptr);
        result.push_back(m_scaleAction);

        if (DimensionTool* calibrateTool = m_dimensionTools[DimensionTool::Calibrate])
        {
            result.push_back(calibrateTool->getAction());
        }

        result.push_back(nullptr);
        result.push_back(m_showDimensionsAction);
        result.push_back(m_clearDimensionsAction);
        result.push_back(m_convertToAnnotationsAction);
        result.push_back(m_exportAction);
        result.push_back(m_settingsAction);
    }

    return result;
}

QString DimensionsPlugin::getPluginMenuName() const
{
    return tr("&Dimensions");
}

void DimensionsPlugin::drawPage(QPainter* painter,
                                pdf::PDFInteger pageIndex,
                                const pdf::PDFPrecompiledPage* compiledPage,
                                pdf::PDFTextLayoutGetter& layoutGetter,
                                const QTransform& pagePointToDevicePointMatrix,
                                const pdf::PDFColorConvertor& convertor,
                                QList<pdf::PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(layoutGetter);
    Q_UNUSED(errors);

    if (!m_showDimensionsAction || !m_showDimensionsAction->isChecked() || m_dimensions.empty())
    {
        // Nothing to draw
        return;
    }

    const DimensionsPluginSettings& settings = getSettings();

    painter->setFont(settings.font);
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFontMetricsF fontMetrics(painter->font());

    for (const Dimension& dimension : m_dimensions)
    {
        if (pageIndex != dimension.getPageIndex())
        {
            continue;
        }

        switch (dimension.getType())
        {
            case Dimension::Linear:
            {
                QPointF p1 = pagePointToDevicePointMatrix.map(dimension.getPolygon().front());
                QPointF p2 = pagePointToDevicePointMatrix.map(dimension.getPolygon().back());

                // Swap so p1 is to the left of the page, before p2 (for correct determination of angle)
                if (p1.x() > p2.x())
                {
                    qSwap(p1, p2);
                }

                QLineF line(p1, p2);

                if (qFuzzyIsNull(line.length()))
                {
                    // If we have zero line, then do nothing
                    continue;
                }

                QLineF unitVectorLine = line.normalVector().unitVector();
                QPointF unitVector = unitVectorLine.p2() - unitVectorLine.p1();
                qreal extensionLineSize = pdf::PDFWidgetUtils::scaleDPI_y(painter->device(), 5);

                painter->setPen(convertor.convert(settings.textColor, false, true));
                painter->drawLine(line);

                QLineF extensionLineLeft(p1 - unitVector * extensionLineSize, p1 + unitVector * extensionLineSize);
                QLineF extensionLineRight(p2 - unitVector * extensionLineSize, p2 + unitVector * extensionLineSize);

                painter->drawLine(extensionLineLeft);
                painter->drawLine(extensionLineRight);

                QPointF textPoint = line.center();
                qreal angle = line.angle();

                QRectF textRect(-line.length() * 0.5, -fontMetrics.lineSpacing(), line.length(), fontMetrics.lineSpacing());
                QString text = getDimensionText(dimension);
                QRectF textBoundingRect;

                painter->save();
                painter->translate(textPoint);
                painter->rotate(-angle);
                painter->drawText(textRect, Qt::AlignCenter | Qt::TextDontClip | Qt::TextDontClip, text, &textBoundingRect);
                painter->fillRect(textBoundingRect, convertor.convert(settings.backgroundColor, true, false));
                painter->drawText(textRect, Qt::AlignCenter | Qt::TextDontClip, text);
                painter->restore();

                break;
            }

            case Dimension::Perimeter:
            case Dimension::Area:
            {
                const bool isArea = dimension.getType() == Dimension::Type::Area;
                const std::vector<QPointF>& polygon = dimension.getPolygon();

                qreal lineSize = pdf::PDFWidgetUtils::scaleDPI_x(painter->device(), 1);
                qreal pointSize = pdf::PDFWidgetUtils::scaleDPI_x(painter->device(), 5);

                painter->save();
                QPen pen(settings.textColor);
                pen.setWidthF(lineSize);
                pen.setCosmetic(true);

                painter->setPen(convertor.convert(pen));
                painter->setBrush(convertor.convert(QBrush(settings.backgroundColor, isArea ? Qt::SolidPattern : Qt::DiagCrossPattern)));

                painter->setTransform(QTransform(pagePointToDevicePointMatrix), true);
                painter->drawPolygon(polygon.data(), int(polygon.size()), Qt::OddEvenFill);
                painter->restore();

                QPen penPoint(convertor.convert(settings.textColor, false, true));
                penPoint.setCapStyle(Qt::RoundCap);
                penPoint.setWidthF(pointSize);
                painter->setPen(penPoint);

                QPointF centerPoint(0, 0);
                for (const QPointF& point : polygon)
                {
                    QPointF mappedPoint = pagePointToDevicePointMatrix.map(point);
                    centerPoint += mappedPoint;
                    painter->drawPoint(mappedPoint);
                }

                centerPoint *= 1.0 / qreal(polygon.size());

                QString text = getDimensionText(dimension);

                QRectF textBoundingRect;
                QRectF textRect(0, 0, fontMetrics.horizontalAdvance(text) * 2, fontMetrics.lineSpacing() * 2);
                textRect.moveCenter(centerPoint);
                painter->drawText(textRect, Qt::AlignCenter | Qt::TextDontClip | Qt::TextDontClip, text, &textBoundingRect);
                painter->fillRect(textBoundingRect, convertor.convert(settings.backgroundColor, true, false));
                painter->drawText(textRect, Qt::AlignCenter | Qt::TextDontClip, text);

                break;
            }

            case Dimension::Angular:
            {
                const std::vector<QPointF>& polygon = dimension.getPolygon();
                QLineF line1(pagePointToDevicePointMatrix.map(polygon[1]), pagePointToDevicePointMatrix.map(polygon.front()));
                QLineF line2(pagePointToDevicePointMatrix.map(polygon[1]), pagePointToDevicePointMatrix.map(polygon.back()));

                qreal lineSize = pdf::PDFWidgetUtils::scaleDPI_x(painter->device(), 1);
                qreal pointSize = pdf::PDFWidgetUtils::scaleDPI_x(painter->device(), 5);

                qreal maxLength = qMax(line1.length(), line2.length());
                line1.setLength(maxLength);
                line2.setLength(maxLength);

                QPen pen(convertor.convert(settings.textColor, false, true));
                pen.setWidthF(lineSize);
                painter->setPen(qMove(pen));

                painter->drawLine(line1);
                painter->drawLine(line2);

                qreal startAngle = line1.angle() * 16;
                qreal angleLength = dimension.getMeasuredValue() * 16;

                QRectF rect(-maxLength * 0.5, -maxLength * 0.5, maxLength, maxLength);
                rect.translate(line1.p1());
                painter->drawArc(rect, startAngle - angleLength, angleLength);

                QPen penPoint(convertor.convert(settings.textColor, false, true));
                penPoint.setCapStyle(Qt::RoundCap);
                penPoint.setWidthF(pointSize);
                painter->setPen(penPoint);

                painter->drawPoint(line1.p1());
                painter->drawPoint(line1.p2());
                painter->drawPoint(line2.p2());

                QTransform textMatrix;
                textMatrix.translate(line1.x1(), line1.y1());
                textMatrix.rotate(-line1.angle() + dimension.getMeasuredValue() * 0.5);

                QPointF textPoint = textMatrix.map(QPointF(maxLength * 0.25, 0.0));
                QString text = getDimensionText(dimension);

                QRectF textBoundingRect;
                QRectF textRect(0, 0, fontMetrics.horizontalAdvance(text) * 2, fontMetrics.lineSpacing() * 2);
                textRect.moveCenter(textPoint);
                painter->drawText(textRect, Qt::AlignCenter | Qt::TextDontClip | Qt::TextDontClip, text, &textBoundingRect);
                painter->fillRect(textBoundingRect, convertor.convert(settings.backgroundColor, true, false));
                painter->drawText(textRect, Qt::AlignCenter | Qt::TextDontClip, text);

                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }
}

QString DimensionsPlugin::getDimensionText(const Dimension& dimension) const
{
    QLocale locale;
    const DimensionsPluginSettings& settings = getSettings();
    const pdf::PDFReal scaleFactor = getEffectiveScale(dimension).getScaleFactor();

    switch (dimension.getType())
    {
        case Dimension::Linear:
            return QString("%1 %2").arg(locale.toString(dimension.getMeasuredValue() * scaleFactor * settings.lengthUnit.scale, 'f', 2), settings.lengthUnit.symbol);

        case Dimension::Perimeter:
            return tr("p = %1 %2").arg(locale.toString(dimension.getMeasuredValue() * scaleFactor * settings.lengthUnit.scale, 'f', 2), settings.lengthUnit.symbol);

        case Dimension::Area:
            return tr("A = %1 %2").arg(locale.toString(dimension.getMeasuredValue() * scaleFactor * scaleFactor * settings.areaUnit.scale, 'f', 2), settings.areaUnit.symbol);

        case Dimension::Angular:
            return QString("%1 %2").arg(locale.toString(dimension.getMeasuredValue() * settings.angleUnit.scale, 'f', 2), settings.angleUnit.symbol);

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString DimensionsPlugin::getDimensionTypeName(Dimension::Type type)
{
    switch (type)
    {
        case Dimension::Linear:
            return tr("Length");

        case Dimension::Perimeter:
            return tr("Perimeter");

        case Dimension::Area:
            return tr("Area");

        case Dimension::Angular:
            return tr("Angle");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

void DimensionsPlugin::onShowDimensionsTriggered()
{
    updateGraphics();
}

void DimensionsPlugin::onConvertToAnnotationsTriggered()
{
    if (m_dimensions.empty())
    {
        return;
    }

    // The measurements are removed before the conversion, so they are not drawn
    // twice - once by this plugin and once by the annotation manager
    const std::vector<Dimension> dimensions = qMove(m_dimensions);
    m_dimensions.clear();

    std::vector<Dimension> notCreated;
    const bool isSuccess = createDimensionAnnotations(dimensions, &notCreated);

    // A measurement, which could not be stored in the document, stays temporary,
    // so it is not silently lost
    m_dimensions = qMove(notCreated);
    updateActions();
    updateGraphics();

    if (!isSuccess)
    {
        QMessageBox::warning(m_widget, tr("Measurement"), tr("The measurements cannot be stored in the document as annotations."));
    }
    else if (!m_dimensions.empty())
    {
        QMessageBox::warning(m_widget, tr("Measurement"), tr("Some of the measurements could not be stored in the document as annotations. "
                                                             "They are displayed as temporary measurements."));
    }
}

void DimensionsPlugin::onClearDimensionsTriggered()
{
    m_dimensions.clear();
    updateActions();
    updateGraphics();
}

void DimensionsPlugin::onSettingsTriggered()
{
    DimensionsPluginSettings settings = getSettings();
    SettingsDialog dialog(m_widget, settings);

    if (dialog.exec() == QDialog::Accepted)
    {
        m_settingsStorage.setSettings(qMove(settings));

        // The dialog edits the default scale, the scale in effect is re-selected,
        // so a document with its own scale keeps it
        applyDocumentScale();

        m_settingsStorage.save();
        updateActions();
        updateGraphics();
    }
}

void DimensionsPlugin::onCustomScaleTriggered()
{
    ScaleDialog dialog(m_widget, m_scale, ScaleDialog::Mode::Edit);

    if (dialog.exec() == QDialog::Accepted)
    {
        DimensionScale scale = dialog.getScale();

        if (scale.isValid())
        {
            if (!scale.getName().isEmpty())
            {
                m_settingsStorage.addPreset(scale);
            }

            setScale(scale);
        }
    }
}

void DimensionsPlugin::onManagePresetsTriggered()
{
    ScalePresetsDialog dialog(m_widget, m_settingsStorage.getPresets());

    if (dialog.exec() == QDialog::Accepted)
    {
        m_settingsStorage.setPresets(dialog.getPresets());
        m_settingsStorage.save();
    }
}

void DimensionsPlugin::onCalibrationLinePicked(pdf::PDFReal measuredLength)
{
    if (DimensionTool* calibrateTool = m_dimensionTools[DimensionTool::Calibrate])
    {
        // Calibration is a single operation, the tool is not needed anymore
        calibrateTool->setActive(false);
    }

    if (measuredLength <= 0.0)
    {
        QMessageBox::warning(m_widget, tr("Calibrate Scale"), tr("The picked line has a zero length, the scale cannot be calculated."));
        return;
    }

    ScaleDialog dialog(m_widget, measuredLength, m_scale);

    if (dialog.exec() == QDialog::Accepted)
    {
        DimensionScale scale = dialog.getScale();

        if (scale.isValid())
        {
            if (!scale.getName().isEmpty())
            {
                m_settingsStorage.addPreset(scale);
            }

            setScale(scale);
        }
    }
}

void DimensionsPlugin::onScaleMenuAboutToShow()
{
    m_scaleMenu->clear();

    const DimensionScale& currentScale = m_scale;

    for (const DimensionScale& preset : m_settingsStorage.getPresets())
    {
        QAction* action = m_scaleMenu->addAction(preset.getDisplayName());
        action->setCheckable(true);
        action->setToolTip(preset.getDescription().isEmpty() ? preset.getRatioText()
                                                             : QString("%1 (%2)").arg(preset.getRatioText(), preset.getDescription()));

        // The name of the preset is not a part of the scale itself, so only
        // the ratio is compared here
        action->setChecked(qFuzzyCompare(preset.getScaleFactor(), currentScale.getScaleFactor()));

        connect(action, &QAction::triggered, this, [this, preset]() { setScale(preset); });
    }

    m_scaleMenu->addSeparator();

    if (DimensionTool* calibrateTool = m_dimensionTools[DimensionTool::Calibrate])
    {
        m_scaleMenu->addAction(calibrateTool->getAction());
    }

    QAction* customScaleAction = m_scaleMenu->addAction(tr("&Custom Scale..."));
    connect(customScaleAction, &QAction::triggered, this, &DimensionsPlugin::onCustomScaleTriggered);

    QAction* managePresetsAction = m_scaleMenu->addAction(tr("&Manage Presets..."));
    connect(managePresetsAction, &QAction::triggered, this, &DimensionsPlugin::onManagePresetsTriggered);
}

void DimensionsPlugin::onExportTriggered()
{
    std::vector<QStringList> rows;

    for (const Dimension& dimension : m_dimensions)
    {
        rows.push_back(QStringList() << QString::number(dimension.getPageIndex() + 1)
                                     << getDimensionTypeName(dimension.getType())
                                     << getDimensionText(dimension)
                                     << getEffectiveScale(dimension).getRatioText());
    }

    std::vector<QStringList> annotationRows = getAnnotationExportRows();
    rows.insert(rows.end(), annotationRows.begin(), annotationRows.end());

    if (rows.empty())
    {
        QMessageBox::information(m_widget, tr("Export Measurements"), tr("The document does not contain any measurement."));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(m_widget, tr("Export Measurements"), QString(), tr("Comma separated values (*.csv)"));

    if (fileName.isEmpty())
    {
        return;
    }

    QFile file(fileName);

    if (!file.open(QFile::WriteOnly | QFile::Truncate | QFile::Text))
    {
        QMessageBox::critical(m_widget, tr("Export Measurements"), tr("File '%1' cannot be opened for writing.").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream.setGenerateByteOrderMark(true);

    stream << escapeCsvField(tr("Page")) << ";"
           << escapeCsvField(tr("Type")) << ";"
           << escapeCsvField(tr("Measurement")) << ";"
           << escapeCsvField(tr("Scale")) << "\n";

    for (const QStringList& row : rows)
    {
        QStringList escapedRow;
        for (const QString& field : row)
        {
            escapedRow << escapeCsvField(field);
        }
        stream << escapedRow.join(QChar(';')) << "\n";
    }

    file.close();
}

std::vector<QStringList> DimensionsPlugin::getAnnotationExportRows() const
{
    std::vector<QStringList> rows;

    if (!m_document)
    {
        return rows;
    }

    const pdf::PDFObjectStorage& storage = m_document->getStorage();
    const pdf::PDFCatalog* catalog = m_document->getCatalog();

    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        const pdf::PDFPage* page = catalog->getPage(pageIndex);

        for (const pdf::PDFObjectReference& reference : page->getAnnotations())
        {
            pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&storage, reference);

            if (!annotation)
            {
                continue;
            }

            pdf::PDFObject measureObject;

            if (const pdf::PDFLineAnnotation* lineAnnotation = dynamic_cast<const pdf::PDFLineAnnotation*>(annotation.data()))
            {
                if (lineAnnotation->getIntent() != pdf::PDFLineAnnotation::Intent::Dimension)
                {
                    continue;
                }

                measureObject = lineAnnotation->getMeasureDictionary();
            }
            else if (const pdf::PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const pdf::PDFPolygonalGeometryAnnotation*>(annotation.data()))
            {
                if (polygonalAnnotation->getIntent() != pdf::PDFPolygonalGeometryAnnotation::Intent::Dimension)
                {
                    continue;
                }

                measureObject = polygonalAnnotation->getMeasure();
            }
            else
            {
                continue;
            }

            const pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&storage, measureObject);

            QString type;
            if (const pdf::PDFMarkupAnnotation* markupAnnotation = annotation->asMarkupAnnotation())
            {
                // The type of the measurement is stored in the subject
                // of the annotation, when it is created by this plugin
                type = markupAnnotation->getSubject();
            }

            rows.push_back(QStringList() << QString::number(qint64(pageIndex) + 1)
                                         << type
                                         << annotation->getContents()
                                         << measure.getScaleRatio());
        }
    }

    return rows;
}

void DimensionsPlugin::onDimensionCreated(Dimension dimension)
{
    if (!dimension.getPolygon().empty())
    {
        // If the measurement lies in a viewport, which defines its own measure,
        // then the scale of the viewport applies and the scale selected by the
        // user is ignored for this measurement. A measurement, which crosses
        // the border of a viewport, belongs to the viewport of its first point.
        dimension.setScale(getScaleFromViewport(dimension.getPageIndex(), dimension.getPolygon().front()));
    }

    if (getSettings().storageMode == DimensionsPluginSettings::StorageMode::Annotations)
    {
        if (createDimensionAnnotations({ dimension }))
        {
            // The measurement is a part of the document now, it is drawn
            // by the annotation manager and not by this plugin
            return;
        }

        QMessageBox::warning(m_widget, tr("Measurement"), tr("The measurement cannot be stored in the document as an annotation. "
                                                             "It is displayed as a temporary measurement instead."));
    }

    m_dimensions.emplace_back(qMove(dimension));
    updateActions();
    updateGraphics();
}

bool DimensionsPlugin::canCreateAnnotations() const
{
    return m_document && m_document->getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems);
}

bool DimensionsPlugin::createDimensionAnnotations(const std::vector<Dimension>& dimensions, std::vector<Dimension>* notCreated)
{
    if (notCreated)
    {
        notCreated->clear();
    }

    if (dimensions.empty())
    {
        return false;
    }

    if (!canCreateAnnotations())
    {
        if (notCreated)
        {
            *notCreated = dimensions;
        }

        return false;
    }

    // All the measurements are written in a single modification, so the whole
    // conversion is a single step in the undo history of the document
    pdf::PDFDocumentModifier modifier(m_document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();

    size_t createdAnnotations = 0;
    for (const Dimension& dimension : dimensions)
    {
        if (createDimensionAnnotation(builder, dimension))
        {
            ++createdAnnotations;
        }
        else if (notCreated)
        {
            notCreated->push_back(dimension);
        }
    }

    if (createdAnnotations > 0)
    {
        modifier.markAnnotationsChanged();
    }

    if (createdAnnotations == 0 || !modifier.finalize())
    {
        if (notCreated)
        {
            *notCreated = dimensions;
        }

        return false;
    }

    Q_EMIT m_widget->getToolManager()->documentModified(pdf::PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    return true;
}

bool DimensionsPlugin::createDimensionAnnotation(pdf::PDFDocumentBuilder* builder, const Dimension& dimension)
{
    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    const pdf::PDFInteger pageIndex = dimension.getPageIndex();

    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return false;
    }

    const pdf::PDFPage* page = catalog->getPage(size_t(pageIndex));

    const DimensionsPluginSettings& settings = getSettings();
    const QString author = pdf::PDFAuthorSettings::getAuthorName();
    const QString subject = getDimensionTypeName(dimension.getType());
    const QString contents = getDimensionText(dimension);
    const pdf::PDFObjectReference pageReference = page->getPageReference();
    const std::vector<QPointF>& polygon = dimension.getPolygon();

    constexpr pdf::PDFReal lineWidth = 1.0;

    // The interior color of an annotation is a plain color array without an alpha
    // channel. The transparency of the background color is therefore expressed
    // by the filling opacity of the annotation, see below.
    const bool isFilled = settings.backgroundColor.isValid() && settings.backgroundColor.alphaF() > 0.0;
    const QColor fillColor = isFilled ? settings.backgroundColor : QColor();

    QPolygonF annotationPolygon;
    for (const QPointF& point : polygon)
    {
        annotationPolygon << point;
    }

    if (annotationPolygon.size() > 2 && annotationPolygon.front() == annotationPolygon.back())
    {
        // Vertices of a polygon annotation are implicitly closed, so the repeated
        // first point is not stored
        annotationPolygon.removeLast();
    }

    pdf::PDFObjectReference annotation;
    const char* intent = nullptr;
    bool isAnnotationFilled = false;

    switch (dimension.getType())
    {
        case Dimension::Linear:
        {
            const QPointF startPoint = polygon.front();
            const QPointF endPoint = polygon.back();

            QRectF boundingRect(startPoint, endPoint);
            boundingRect = boundingRect.normalized().adjusted(-lineWidth, -lineWidth, lineWidth, lineWidth);

            // Butt line ending is a short line perpendicular to the measured line,
            // which is exactly the tick drawn at the end of a dimension line
            annotation = builder->createAnnotationLine(pageReference, boundingRect, startPoint, endPoint, lineWidth,
                                                       settings.textColor, settings.textColor, author, subject, contents,
                                                       pdf::AnnotationLineEnding::Butt, pdf::AnnotationLineEnding::Butt,
                                                       0.0, 0.0, 0.0, true, true);
            intent = "LineDimension";
            break;
        }

        case Dimension::Perimeter:
        case Dimension::Area:
        {
            isAnnotationFilled = isFilled;

            annotation = builder->createAnnotationPolygon(pageReference, annotationPolygon,
                                                          lineWidth, fillColor,
                                                          settings.textColor, author, subject, contents);
            intent = "PolygonDimension";
            break;
        }

        case Dimension::Angular:
        {
            annotation = builder->createAnnotationPolyline(pageReference, annotationPolygon,
                                                           lineWidth, QColor(), settings.textColor, author, subject, contents,
                                                           pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::None);
            intent = "PolyLineDimension";
            break;
        }

        default:
            Q_ASSERT(false);
            return false;
    }

    // Mark the annotation as a measurement and describe the used scale, so other
    // applications are able to interpret it
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("IT");
    factory << pdf::WrapName(intent);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Measure");
    factory << createMeasureDictionary(dimension);
    factory.endDictionaryItem();

    if (isAnnotationFilled)
    {
        // The filled interior must not hide the drawing under it, nor the displayed
        // value. The stroking opacity (CA) is left untouched, so the outline and
        // the text stay fully opaque. The appearance stream is generated below,
        // after this entry is written, so the transparency becomes a part of it
        // and the annotation looks the same in the viewers, which ignore the
        // non-standard filling opacity.
        factory.beginDictionaryItem("ca");
        factory << pdf::PDFReal(settings.backgroundColor.alphaF());
        factory.endDictionaryItem();
    }

    factory.endDictionary();

    builder->mergeTo(annotation, factory.takeObject());
    builder->updateAnnotationAppearanceStreams(annotation);

    return true;
}

pdf::PDFObject DimensionsPlugin::createMeasureDictionary(const Dimension& dimension) const
{
    const DimensionsPluginSettings& settings = getSettings();
    const DimensionScale& scale = getEffectiveScale(dimension);
    const pdf::PDFCatalog* catalog = m_document->getCatalog();
    const pdf::PDFInteger pageIndex = dimension.getPageIndex();
    const bool isPageValid = pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount();
    const pdf::PDFReal userUnit = isPageValid ? catalog->getPage(size_t(pageIndex))->getUserUnit() : 1.0;
    const pdf::PDFReal scaleFactor = scale.getScaleFactor();

    // Count of the length units, which correspond to a single unit
    // of the default user space
    const pdf::PDFReal unitsPerUserSpaceUnit = scaleFactor * settings.lengthUnit.scale * userUnit;

    // The area format converts from the square length units, so the conversion
    // factor is the count of the area units per one square length unit
    const pdf::PDFReal areaUnitsPerSquareLengthUnit = settings.areaUnit.scale / (settings.lengthUnit.scale * settings.lengthUnit.scale);

    auto createNumberFormat = [](pdf::PDFObjectFactory& factory, const QString& unitLabel, pdf::PDFReal conversionFactor)
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("NumberFormat");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("U");
        factory << unitLabel;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("C");
        factory << conversionFactor;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("F");
        factory << pdf::WrapName("D");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("D");
        factory << pdf::PDFInteger(100);
        factory.endDictionaryItem();
        factory.endDictionary();
    };

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Measure");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("RL");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("R");
    factory << scale.getRatioText();
    factory.endDictionaryItem();

    factory.beginDictionaryItem("X");
    factory.beginArray();
    createNumberFormat(factory, settings.lengthUnit.symbol, unitsPerUserSpaceUnit);
    factory.endArray();
    factory.endDictionaryItem();

    factory.beginDictionaryItem("D");
    factory.beginArray();
    createNumberFormat(factory, settings.lengthUnit.symbol, 1.0);
    factory.endArray();
    factory.endDictionaryItem();

    factory.beginDictionaryItem("A");
    factory.beginArray();
    createNumberFormat(factory, settings.areaUnit.symbol, areaUnitsPerSquareLengthUnit);
    factory.endArray();
    factory.endDictionaryItem();

    factory.beginDictionaryItem("T");
    factory.beginArray();
    createNumberFormat(factory, settings.angleUnit.symbol, settings.angleUnit.scale);
    factory.endArray();
    factory.endDictionaryItem();

    factory.endDictionary();

    return factory.takeObject();
}

void DimensionsPlugin::setScale(const DimensionScale& scale)
{
    if (!scale.isValid())
    {
        return;
    }

    m_scale = scale;

    if (getSettings().isScaleStoredPerDocument)
    {
        m_settingsStorage.setDocumentScale(m_documentIdentity, scale);
    }
    else
    {
        // Without the per document scales there is a single scale for everything,
        // so it is stored as the default one
        m_settingsStorage.getSettings().defaultScale = scale;
    }

    m_settingsStorage.save();

    updateScaleAction();
    updateGraphics();
}

void DimensionsPlugin::applyDocumentScale()
{
    if (getSettings().isScaleStoredPerDocument)
    {
        DimensionScale scale = m_settingsStorage.getDocumentScale(m_documentIdentity);

        if (scale.isValid())
        {
            m_scale = qMove(scale);
            updateScaleAction();
            return;
        }
    }

    // The document was not calibrated yet, so the scale defined by the document
    // itself is used, if there is any
    DimensionScale documentScale = getScaleFromDocument();

    // Finally the default scale is used. It is assigned unconditionally, so
    // the scale of the previously opened document is never inherited.
    m_scale = documentScale.isValid() ? qMove(documentScale) : getSettings().defaultScale;

    if (!m_scale.isValid())
    {
        m_scale = DimensionScale::createIdentity();
    }

    updateScaleAction();
}

DimensionScale DimensionsPlugin::getScaleFromDocument() const
{
    if (!m_document)
    {
        return DimensionScale();
    }

    const pdf::PDFObjectStorage& storage = m_document->getStorage();
    const pdf::PDFCatalog* catalog = m_document->getCatalog();

    DimensionScale result;

    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        const pdf::PDFPage* page = catalog->getPage(pageIndex);
        const std::vector<pdf::PDFViewport> viewports = pdf::PDFViewport::parseViewports(&storage, page->getViewports(&storage));

        for (const pdf::PDFViewport& viewport : viewports)
        {
            DimensionScale scale = DimensionScale::createFromMeasure(viewport.getMeasure(), page->getUserUnit());

            if (!scale.isValid())
            {
                continue;
            }

            if (!result.isValid())
            {
                result = qMove(scale);
                continue;
            }

            if (!qFuzzyCompare(result.getScaleFactor(), scale.getScaleFactor()))
            {
                // The document defines more than one scale, for example a plan
                // with details drawn in a different scale. A single scale cannot
                // represent it, each measurement is resolved by its own viewport
                // instead, see getScaleFromViewport.
                return DimensionScale();
            }
        }
    }

    return result;
}

DimensionScale DimensionsPlugin::getScaleFromViewport(pdf::PDFInteger pageIndex, const QPointF& point) const
{
    if (!m_document)
    {
        return DimensionScale();
    }

    const pdf::PDFCatalog* catalog = m_document->getCatalog();

    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return DimensionScale();
    }

    const pdf::PDFPage* page = catalog->getPage(size_t(pageIndex));

    const pdf::PDFObjectStorage& storage = m_document->getStorage();
    const std::vector<pdf::PDFViewport> viewports = pdf::PDFViewport::parseViewports(&storage, page->getViewports(&storage));

    // Overlapping viewports are resolved by the specification rule - the last
    // viewport of the array wins
    if (const pdf::PDFViewport* viewport = pdf::PDFViewport::findViewportForPoint(viewports, point))
    {
        return DimensionScale::createFromMeasure(viewport->getMeasure(), page->getUserUnit());
    }

    return DimensionScale();
}

const DimensionScale& DimensionsPlugin::getEffectiveScale(const Dimension& dimension) const
{
    // The scale prescribed by the document has a priority over the scale
    // selected by the user
    return dimension.getScale().isValid() ? dimension.getScale() : m_scale;
}

void DimensionsPlugin::updateActions()
{
    if (m_showDimensionsAction)
    {
        m_showDimensionsAction->setEnabled(!m_dimensions.empty());
    }
    if (m_clearDimensionsAction)
    {
        m_clearDimensionsAction->setEnabled(!m_dimensions.empty());
    }
    if (m_convertToAnnotationsAction)
    {
        m_convertToAnnotationsAction->setEnabled(!m_dimensions.empty() && canCreateAnnotations());
    }
    if (m_exportAction)
    {
        m_exportAction->setEnabled(m_document || !m_dimensions.empty());
    }
}

void DimensionsPlugin::updateScaleAction()
{
    if (!m_scaleAction)
    {
        return;
    }

    m_scaleAction->setText(tr("Sca&le: %1").arg(m_scale.getDisplayName()));

    QString toolTip = tr("Scale of the drawing: %1").arg(m_scale.getRatioText());

    if (getSettings().isScaleStoredPerDocument && m_document && !m_documentIdentity.isValid())
    {
        // The document has neither a file name nor a permanent identifier,
        // so there is nothing we could remember its scale by
        toolTip += QChar('\n');
        toolTip += tr("The scale of this document cannot be remembered, because the document cannot be identified.");
    }

    m_scaleAction->setToolTip(toolTip);
}

void DimensionsPlugin::updateGraphics()
{
    if (m_widget)
    {
        m_widget->getDrawWidget()->getWidget()->update();
    }
}

}   // namespace pdfplugin
