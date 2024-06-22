//    Copyright (C) 2024 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfpagecontenteditorediteditemsettings.h"
#include "ui_pdfpagecontenteditorediteditemsettings.h"

#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditorprocessor.h"
#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"

#include <QDir>
#include <QFileDialog>
#include <QImageReader>
#include <QStandardPaths>
#include <QColorDialog>

namespace pdf
{

PDFPageContentEditorEditedItemSettings::PDFPageContentEditorEditedItemSettings(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFPageContentEditorEditedItemSettings)
{
    ui->setupUi(this);
    connect(ui->loadImageButton, &QPushButton::clicked, this, &PDFPageContentEditorEditedItemSettings::selectImage);

    for (const QString& colorName : QColor::colorNames())
    {
        QColor color(colorName);
        QIcon icon = getIconForColor(color);

        ui->penColorCombo->addItem(icon, colorName, color);
        ui->brushColorCombo->addItem(icon, colorName, color);
    }

    ui->penStyleCombo->addItem(tr("None"), int(Qt::NoPen));
    ui->penStyleCombo->addItem(tr("Solid"), int(Qt::SolidLine));
    ui->penStyleCombo->addItem(tr("Dashed"), int(Qt::DashLine));
    ui->penStyleCombo->addItem(tr("Dotted"), int(Qt::DotLine));
    ui->penStyleCombo->addItem(tr("Dash-dot"), int(Qt::DashDotLine));
    ui->penStyleCombo->addItem(tr("Dash-dot-dot"), int(Qt::DashDotDotLine));

    ui->brushStyleCombo->addItem(tr("None"), int(Qt::NoBrush));
    ui->brushStyleCombo->addItem(tr("Solid"), int(Qt::SolidPattern));

    connect(ui->selectPenColorButton, &QToolButton::clicked, this, &PDFPageContentEditorEditedItemSettings::onSelectPenColorButtonClicked);
    connect(ui->selectBrushColorButton, &QToolButton::clicked, this, &PDFPageContentEditorEditedItemSettings::onSelectBrushColorButtonClicked);
    connect(ui->penWidthEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PDFPageContentEditorEditedItemSettings::onPenWidthChanged);
    connect(ui->penStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorEditedItemSettings::onPenStyleChanged);
    connect(ui->brushStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorEditedItemSettings::onBrushStyleChanged);
    connect(ui->penColorCombo->lineEdit(), &QLineEdit::editingFinished, this, &PDFPageContentEditorEditedItemSettings::onPenColorComboTextChanged);
    connect(ui->penColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorEditedItemSettings::onPenColorComboIndexChanged);
    connect(ui->brushColorCombo->lineEdit(), &QLineEdit::editingFinished, this, &PDFPageContentEditorEditedItemSettings::onBrushColorComboTextChanged);
    connect(ui->brushColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorEditedItemSettings::onBrushColorComboIndexChanged);
}

QIcon PDFPageContentEditorEditedItemSettings::getIconForColor(QColor color) const
{
    QIcon icon;

    QSize iconSize = PDFWidgetUtils::scaleDPI(this, QSize(16, 16));

    QPixmap pixmap(iconSize.width(), iconSize.height());
    pixmap.fill(color);
    icon.addPixmap(pixmap);

    return icon;
}

PDFPageContentEditorEditedItemSettings::~PDFPageContentEditorEditedItemSettings()
{
    delete ui;
}

void PDFPageContentEditorEditedItemSettings::loadFromElement(PDFPageContentElementEdited* editedElement)
{
    const PDFEditedPageContentElement* contentElement = editedElement->getElement();

    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->imageTab));
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->textTab));
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->styleTab));
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->transformationTab));

    if (const PDFEditedPageContentElementImage* imageElement = contentElement->asImage())
    {
        ui->tabWidget->addTab(ui->imageTab, tr("Image"));
        m_image = imageElement->getImage();
        setImage(imageElement->getImage());
    }

    if (PDFEditedPageContentElementText* textElement = editedElement->getElement()->asText())
    {
        ui->tabWidget->addTab(ui->textTab, tr("Text"));
        QString text = textElement->getItemsAsText();
        ui->plainTextEdit->setPlainText(text);
    }

    if (editedElement->getElement()->asText() || editedElement->getElement()->asPath())
    {
        ui->tabWidget->addTab(ui->styleTab, tr("Style"));
    }

    QTransform matrix = editedElement->getElement()->getTransform();
    PDFTransformationDecomposition decomposedTransformation = PDFPainterHelper::decomposeTransform(matrix);

    ui->rotationAngleEdit->setValue(qRadiansToDegrees(decomposedTransformation.rotationAngle));
    ui->scaleInXEdit->setValue(decomposedTransformation.scaleX);
    ui->scaleInYEdit->setValue(decomposedTransformation.scaleY);
    ui->shearFactorEdit->setValue(decomposedTransformation.shearFactor);
    ui->translateInXEdit->setValue(decomposedTransformation.translateX);
    ui->translateInYEdit->setValue(decomposedTransformation.translateY);

    ui->tabWidget->addTab(ui->transformationTab, tr("Transformation"));

    // Style
    const PDFEditedPageContentElement* element = editedElement->getElement();

    StyleFeatures features = None;

    if (element->asPath())
    {
        features.setFlag(Pen);
        features.setFlag(PenColor);
        features.setFlag(Brush);
    }

    if (element->asText())
    {
        features.setFlag(PenColor);
    }

    const bool hasPen = features.testFlag(Pen);
    const bool hasPenColor = features.testFlag(PenColor);
    const bool hasBrush = features.testFlag(Brush);

    ui->penWidthEdit->setEnabled(hasPen);
    ui->penWidthLabel->setEnabled(hasPen);

    ui->penStyleCombo->setEnabled(hasPen);
    ui->penStyleLabel->setEnabled(hasPen);

    ui->penColorCombo->setEnabled(hasPenColor);
    ui->penColorLabel->setEnabled(hasPenColor);
    ui->selectPenColorButton->setEnabled(hasPenColor);

    ui->brushStyleLabel->setEnabled(hasBrush);
    ui->brushStyleCombo->setEnabled(hasBrush);

    ui->brushColorCombo->setEnabled(hasBrush);
    ui->brushColorLabel->setEnabled(hasBrush);
    ui->selectBrushColorButton->setEnabled(hasBrush);

    const PDFPageContentProcessorState& graphicState = element->getState();

    QPen pen = pdf::PDFPainterHelper::createPenFromState(&graphicState, graphicState.getAlphaStroking());
    QBrush brush = pdf::PDFPainterHelper::createBrushFromState(&graphicState, graphicState.getAlphaFilling());

    setPen(pen, true);
    setBrush(brush, true);
}

void PDFPageContentEditorEditedItemSettings::setPen(const QPen& pen, bool forceUpdate)
{
    if (m_pen != pen || forceUpdate)
    {
        m_pen = pen;
        ui->penWidthEdit->setValue(pen.widthF());
        ui->penStyleCombo->setCurrentIndex(ui->penStyleCombo->findData(int(pen.style())));
        setColorToComboBox(ui->penColorCombo, pen.color());
    }
}

void PDFPageContentEditorEditedItemSettings::setBrush(const QBrush& brush, bool forceUpdate)
{
    if (m_brush != brush || forceUpdate)
    {
        m_brush = brush;
        ui->brushStyleCombo->setCurrentIndex(ui->brushStyleCombo->findData(int(brush.style())));
        setColorToComboBox(ui->brushColorCombo, brush.color());
    }
}

void PDFPageContentEditorEditedItemSettings::setColorToComboBox(QComboBox* comboBox, QColor color)
{
    if (!color.isValid())
    {
        return;
    }

    QString name = color.name(QColor::HexArgb);

    int index = comboBox->findData(color, Qt::UserRole, Qt::MatchExactly);

    if (index == -1)
    {
        // Jakub Melka: try to find text (color name)
        index = comboBox->findText(name);
    }

    if (index != -1)
    {
        comboBox->setCurrentIndex(index);
    }
    else
    {
        comboBox->addItem(getIconForColor(color), name, color);
        comboBox->setCurrentIndex(comboBox->count() - 1);
    }
}

void PDFPageContentEditorEditedItemSettings::saveToElement(PDFPageContentElementEdited* editedElement)
{
    if (PDFEditedPageContentElementImage* imageElement = editedElement->getElement()->asImage())
    {
        imageElement->setImage(m_image);
        imageElement->setImageObject(PDFObject());
    }

    if (PDFEditedPageContentElementText* textElement = editedElement->getElement()->asText())
    {
        textElement->setItemsAsText(ui->plainTextEdit->toPlainText());
    }

    PDFTransformationDecomposition decomposedTransformation;
    decomposedTransformation.rotationAngle = ui->rotationAngleEdit->value();
    decomposedTransformation.shearFactor = ui->shearFactorEdit->value();
    decomposedTransformation.scaleX = ui->scaleInXEdit->value();
    decomposedTransformation.scaleY = ui->scaleInYEdit->value();
    decomposedTransformation.translateX = ui->translateInXEdit->value();
    decomposedTransformation.translateY = ui->translateInYEdit->value();

    QTransform transform = PDFPainterHelper::composeTransform(decomposedTransformation);
    editedElement->getElement()->setTransform(transform);

    if (editedElement->getElement()->asText() || editedElement->getElement()->asPath())
    {
        PDFPageContentProcessorState graphicState = editedElement->getElement()->getState();

        PDFPainterHelper::applyPenToGraphicState(&graphicState, m_pen);
        PDFPainterHelper::applyBrushToGraphicState(&graphicState, m_brush);

        editedElement->getElement()->setState(graphicState);
    }
}

static int PDF_gcd(int a, int b)
{
    if (b == 0)
    {
        return a;
    }

    return PDF_gcd(b, a % b);
}

void PDFPageContentEditorEditedItemSettings::setImage(QImage image)
{
    QSize imageSize = QSize(200, 200);

    int width = image.width();
    int height = image.height();

    int n = width;
    int d = height;

    int divisor = PDF_gcd(n, d);
    if (divisor > 1)
    {
        n /= divisor;
        d /= divisor;
    }

    ui->imageWidthEdit->setText(QString("%1 px").arg(width));
    ui->imageHeightEdit->setText(QString("%1 px").arg(height));
    ui->imageRatioEdit->setText(QString("%1 : %2").arg(n).arg(d));

    image.setDevicePixelRatio(this->devicePixelRatioF());
    image = image.scaled(imageSize * this->devicePixelRatioF(), Qt::KeepAspectRatio);

    ui->imageLabel->setPixmap(QPixmap::fromImage(image));
    ui->imageLabel->setFixedSize(PDFWidgetUtils::scaleDPI(this, imageSize));
}

void PDFPageContentEditorEditedItemSettings::selectImage()
{
    QString imageDirectory;

    QStringList pictureDirectiories = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
    if (!pictureDirectiories.isEmpty())
    {
        imageDirectory = pictureDirectiories.last();
    }
    else
    {
        imageDirectory = QDir::currentPath();
    }

    QList<QByteArray> mimeTypes = QImageReader::supportedMimeTypes();
    QStringList mimeTypeFilters;
    for (const QByteArray& mimeType : mimeTypes)
    {
        mimeTypeFilters.append(mimeType);
    }

    QFileDialog dialog(this, tr("Select Image"));
    dialog.setDirectory(imageDirectory);
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/svg+xml");
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);

    if (dialog.exec() == QFileDialog::Accepted)
    {
        QString fileName = dialog.selectedFiles().constFirst();
        QImageReader reader(fileName);
        QImage image = reader.read();

        if (!image.isNull())
        {
            setImage(image);
            m_image = std::move(image);
        }
    }
}

void PDFPageContentEditorEditedItemSettings::setPenColor(QColor color)
{
    if (color.isValid())
    {
        m_pen.setColor(color);
        setColorToComboBox(ui->penColorCombo, color);
    }
}

void PDFPageContentEditorEditedItemSettings::onSelectPenColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_pen.color(), this, tr("Select Color for Pen"), QColorDialog::ShowAlphaChannel);
    setPenColor(color);
}

void PDFPageContentEditorEditedItemSettings::setBrushColor(QColor color)
{
    if (color.isValid() && m_brush.color() != color)
    {
        m_brush.setColor(color);
        setColorToComboBox(ui->brushColorCombo, color);
    }
}

const QBrush& PDFPageContentEditorEditedItemSettings::getBrush() const
{
    return m_brush;
}

const QPen& PDFPageContentEditorEditedItemSettings::getPen() const
{
    return m_pen;
}

void PDFPageContentEditorEditedItemSettings::onSelectBrushColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_pen.color(), this, tr("Select Color for Brush"), QColorDialog::ShowAlphaChannel);
    setBrushColor(color);
}

void PDFPageContentEditorEditedItemSettings::onPenWidthChanged(double value)
{
    m_pen.setWidthF(value);
}

void PDFPageContentEditorEditedItemSettings::onPenStyleChanged()
{
    Qt::PenStyle penStyle = static_cast<Qt::PenStyle>(ui->penStyleCombo->currentData().toInt());
    m_pen.setStyle(penStyle);
}

void PDFPageContentEditorEditedItemSettings::onBrushStyleChanged()
{
    Qt::BrushStyle brushStyle = static_cast<Qt::BrushStyle>(ui->brushStyleCombo->currentData().toInt());
    m_brush.setStyle(brushStyle);
}

void PDFPageContentEditorEditedItemSettings::onPenColorComboTextChanged()
{
    QColor color(ui->penColorCombo->currentText());
    if (color.isValid())
    {
        setColorToComboBox(ui->penColorCombo, color);
        m_pen.setColor(color);
    }
    else if (ui->penColorCombo->currentIndex() != -1)
    {
        ui->penColorCombo->setEditText(ui->penColorCombo->itemText(ui->penColorCombo->currentIndex()));
    }
}

void PDFPageContentEditorEditedItemSettings::onPenColorComboIndexChanged()
{
    const int index = ui->penColorCombo->currentIndex();
    QColor color = ui->penColorCombo->itemData(index, Qt::UserRole).value<QColor>();
    if (color.isValid() && m_pen.color() != color)
    {
        m_pen.setColor(color);
    }
}

void PDFPageContentEditorEditedItemSettings::onBrushColorComboTextChanged()
{
    QColor color(ui->brushColorCombo->currentText());
    if (color.isValid())
    {
        setColorToComboBox(ui->brushColorCombo, color);
        m_brush.setColor(color);
    }
    else if (ui->brushColorCombo->currentIndex() != -1)
    {
        ui->brushColorCombo->setEditText(ui->brushColorCombo->itemText(ui->brushColorCombo->currentIndex()));
    }
}

void PDFPageContentEditorEditedItemSettings::onBrushColorComboIndexChanged()
{
    const int index = ui->brushColorCombo->currentIndex();
    QColor color = ui->brushColorCombo->itemData(index, Qt::UserRole).value<QColor>();
    if (color.isValid())
    {
        m_brush.setColor(color);
    }
}

}   // namespace pdf
