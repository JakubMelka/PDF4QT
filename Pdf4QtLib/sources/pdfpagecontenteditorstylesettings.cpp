//    Copyright (C) 2022 Jakub Melka
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

#include "pdfpagecontenteditorstylesettings.h"
#include "ui_pdfpagecontenteditorstylesettings.h"

#include "pdfwidgetutils.h"
#include "pdfpagecontentelements.h"

#include <QFontDialog>
#include <QColorDialog>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QTextEdit>

namespace pdf
{

PDFPageContentEditorStyleSettings::PDFPageContentEditorStyleSettings(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFPageContentEditorStyleSettings)
{
    ui->setupUi(this);

    for (QString colorName : QColor::colorNames())
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
    ui->brushStyleCombo->addItem(tr("Horizontal"), int(Qt::HorPattern));
    ui->brushStyleCombo->addItem(tr("Vertical"), int(Qt::VerPattern));
    ui->brushStyleCombo->addItem(tr("B-Diagonal"), int(Qt::BDiagPattern));
    ui->brushStyleCombo->addItem(tr("F-Diagonal"), int(Qt::FDiagPattern));
    ui->brushStyleCombo->addItem(tr("Cross"), int(Qt::CrossPattern));

    connect(ui->fontComboBox, &QFontComboBox::currentFontChanged, this, &PDFPageContentEditorStyleSettings::onFontChanged);
    connect(ui->selectPenColorButton, &QToolButton::clicked, this, &PDFPageContentEditorStyleSettings::onSelectPenColorButtonClicked);
    connect(ui->selectBrushColorButton, &QToolButton::clicked, this, &PDFPageContentEditorStyleSettings::onSelectBrushColorButtonClicked);
    connect(ui->selectFontButton, &QToolButton::clicked, this, &PDFPageContentEditorStyleSettings::onSelectFontButtonClicked);
    connect(ui->penWidthEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PDFPageContentEditorStyleSettings::onPenWidthChanged);
    connect(ui->penStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorStyleSettings::onPenStyleChanged);
    connect(ui->brushStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorStyleSettings::onBrushStyleChanged);
    connect(ui->textAngleEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PDFPageContentEditorStyleSettings::onTextAngleChanged);
    connect(ui->penColorCombo->lineEdit(), &QLineEdit::editingFinished, this, &PDFPageContentEditorStyleSettings::onPenColorComboTextChanged);
    connect(ui->penColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorStyleSettings::onPenColorComboIndexChanged);
    connect(ui->brushColorCombo->lineEdit(), &QLineEdit::editingFinished, this, &PDFPageContentEditorStyleSettings::onBrushColorComboTextChanged);
    connect(ui->brushColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFPageContentEditorStyleSettings::onBrushColorComboIndexChanged);

    m_alignmentMapper.setMapping(ui->al11Button, int(Qt::AlignLeft | Qt::AlignTop));
    m_alignmentMapper.setMapping(ui->al12Button, int(Qt::AlignHCenter | Qt::AlignTop));
    m_alignmentMapper.setMapping(ui->al13Button, int(Qt::AlignRight | Qt::AlignTop));
    m_alignmentMapper.setMapping(ui->al21Button, int(Qt::AlignLeft | Qt::AlignVCenter));
    m_alignmentMapper.setMapping(ui->al22Button, int(Qt::AlignCenter));
    m_alignmentMapper.setMapping(ui->al23Button, int(Qt::AlignRight | Qt::AlignVCenter));
    m_alignmentMapper.setMapping(ui->al31Button, int(Qt::AlignLeft | Qt::AlignBottom));
    m_alignmentMapper.setMapping(ui->al32Button, int(Qt::AlignHCenter | Qt::AlignBottom));
    m_alignmentMapper.setMapping(ui->al33Button, int(Qt::AlignRight | Qt::AlignBottom));

    for (QRadioButton* radioButton : findChildren<QRadioButton*>())
    {
        connect(radioButton, &QRadioButton::clicked, &m_alignmentMapper, QOverload<>::of(&QSignalMapper::map));
    }
    connect(&m_alignmentMapper, &QSignalMapper::mappedInt, this, &PDFPageContentEditorStyleSettings::onAlignmentRadioButtonClicked);

    loadFromElement(nullptr, true);
}

PDFPageContentEditorStyleSettings::~PDFPageContentEditorStyleSettings()
{
    delete ui;
}

void PDFPageContentEditorStyleSettings::loadFromElement(const PDFPageContentElement* element, bool forceUpdate)
{
    const PDFPageContentStyledElement* styledElement = dynamic_cast<const PDFPageContentStyledElement*>(element);
    const PDFPageContentElementTextBox* textElement = dynamic_cast<const PDFPageContentElementTextBox*>(element);

    StyleFeatures features = None;

    if (styledElement)
    {
        features.setFlag(Pen);
        features.setFlag(PenColor);
        features.setFlag(Brush);
    }

    if (textElement)
    {
        features.setFlag(PenColor);
        features.setFlag(Text);
    }

    const bool hasPen = features.testFlag(Pen);
    const bool hasPenColor = features.testFlag(PenColor);
    const bool hasBrush = features.testFlag(Brush);
    const bool hasText = features.testFlag(Text);

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

    ui->fontComboBox->setEnabled(hasText);
    ui->fontLabel->setEnabled(hasText);
    ui->selectFontButton->setEnabled(hasText);

    for (QRadioButton* radioButton : findChildren<QRadioButton*>())
    {
        radioButton->setEnabled(hasText);
    }
    ui->textAlignmentLabel->setEnabled(hasText);

    ui->textAngleLabel->setEnabled(hasText);
    ui->textAngleEdit->setEnabled(hasText);

    QPen pen(Qt::SolidLine);
    QBrush brush(Qt::transparent);
    QFont font = QGuiApplication::font();
    Qt::Alignment alignment = Qt::AlignCenter;
    PDFReal textAngle = 0.0;

    if (styledElement)
    {
        pen = styledElement->getPen();
        brush = styledElement->getBrush();
    }

    if (textElement)
    {
        font = textElement->getFont();
        alignment = textElement->getAlignment();
        textAngle = textElement->getAngle();
    }

    setPen(pen, forceUpdate);
    setBrush(brush, forceUpdate);
    setFont(font, forceUpdate);
    setFontAlignment(alignment, forceUpdate);
    setTextAngle(textAngle, forceUpdate);
}

void PDFPageContentEditorStyleSettings::setPen(const QPen& pen, bool forceUpdate)
{
    if (m_pen != pen || forceUpdate)
    {
        const bool oldBlockSignals = blockSignals(true);

        m_pen = pen;
        ui->penWidthEdit->setValue(pen.widthF());
        ui->penStyleCombo->setCurrentIndex(ui->penStyleCombo->findData(int(pen.style())));
        setColorToComboBox(ui->penColorCombo, pen.color());

        blockSignals(oldBlockSignals);
        emit penChanged(m_pen);
    }
}

void PDFPageContentEditorStyleSettings::setBrush(const QBrush& brush, bool forceUpdate)
{
    if (m_brush != brush || forceUpdate)
    {
        const bool oldBlockSignals = blockSignals(true);

        m_brush = brush;
        ui->brushStyleCombo->setCurrentIndex(ui->brushStyleCombo->findData(int(brush.style())));
        setColorToComboBox(ui->brushColorCombo, brush.color());

        blockSignals(oldBlockSignals);
        emit brushChanged(m_brush);
    }
}

void PDFPageContentEditorStyleSettings::setFont(const QFont& font, bool forceUpdate)
{
    if (m_font != font || forceUpdate)
    {
        const bool oldBlockSignals = blockSignals(true);

        m_font = font;
        ui->fontComboBox->setCurrentFont(m_font);

        blockSignals(oldBlockSignals);
        emit fontChanged(m_font);
    }
}

void PDFPageContentEditorStyleSettings::setFontAlignment(Qt::Alignment alignment, bool forceUpdate)
{
    if (m_alignment != alignment || forceUpdate)
    {
        const bool oldBlockSignals = blockSignals(true);

        for (QRadioButton* radioButton : findChildren<QRadioButton*>())
        {
            radioButton->setChecked(false);
        }

        m_alignment = alignment;
        QRadioButton* radioButton = qobject_cast<QRadioButton*>(m_alignmentMapper.mapping(int(alignment)));
        radioButton->setChecked(true);

        blockSignals(oldBlockSignals);
        emit alignmentChanged(m_alignment);
    }
}

void PDFPageContentEditorStyleSettings::setTextAngle(PDFReal angle, bool forceUpdate)
{
    if (ui->textAngleEdit->value() != angle || forceUpdate)
    {
        const bool oldBlockSignals = blockSignals(true);
        ui->textAngleEdit->setValue(angle);
        blockSignals(oldBlockSignals);
        emit textAngleChanged(ui->textAngleEdit->value());
    }
}

bool PDFPageContentEditorStyleSettings::showEditElementStyleDialog(QWidget* parent,
                                                                   PDFPageContentElement* element)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Edit Item"));
    dialog.setLayout(new QVBoxLayout());

    QTextEdit* textEdit = nullptr;
    PDFPageContentStyledElement* styledElement = dynamic_cast<PDFPageContentStyledElement*>(element);
    PDFPageContentElementTextBox* textElement = dynamic_cast<PDFPageContentElementTextBox*>(element);
    if (textElement)
    {
        QGroupBox* contentGroupBox = new QGroupBox(&dialog);
        textEdit = new QTextEdit(textElement->getText(), contentGroupBox);
        textEdit->setFont(textElement->getFont());
        textEdit->setAlignment(textElement->getAlignment());
        textEdit->setTextColor(textElement->getPen().color());
        contentGroupBox->setTitle(tr("Content"));
        contentGroupBox->setLayout(new QVBoxLayout());
        contentGroupBox->layout()->addWidget(textEdit);
        dialog.layout()->addWidget(contentGroupBox);
    }

    PDFPageContentEditorStyleSettings* appearanceWidget = new PDFPageContentEditorStyleSettings(&dialog);
    appearanceWidget->loadFromElement(element, true);
    if (textEdit)
    {
        connect(appearanceWidget, &PDFPageContentEditorStyleSettings::alignmentChanged, textEdit, &QTextEdit::setAlignment);
        connect(appearanceWidget, &PDFPageContentEditorStyleSettings::fontChanged, textEdit, &QTextEdit::setFont);
        connect(appearanceWidget, &PDFPageContentEditorStyleSettings::penChanged, textEdit, [textEdit](const QPen& pen) { textEdit->setTextColor(pen.color()); });
    }

    QGroupBox* appearanceGroupBox = new QGroupBox(&dialog);
    appearanceGroupBox->setTitle(tr("Appearance"));
    appearanceGroupBox->setLayout(new QVBoxLayout());
    appearanceGroupBox->layout()->addWidget(appearanceWidget);
    dialog.layout()->addWidget(appearanceGroupBox);

    QDialogButtonBox* dialogButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(dialogButtonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(dialogButtonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.layout()->addWidget(dialogButtonBox);

    if (dialog.exec() == QDialog::Accepted)
    {
        if (styledElement)
        {
            styledElement->setPen(appearanceWidget->getPen());
            styledElement->setBrush(appearanceWidget->getBrush());
        }

        if (textElement)
        {
            textElement->setText(textEdit->toPlainText());
            textElement->setFont(appearanceWidget->getFont());
            textElement->setAlignment(appearanceWidget->getAlignment());
            textElement->setAngle(appearanceWidget->getTextAngle());
        }

        return true;
    }

    return false;
}

QIcon PDFPageContentEditorStyleSettings::getIconForColor(QColor color) const
{
    QIcon icon;

    QSize iconSize = PDFWidgetUtils::scaleDPI(this, QSize(16, 16));

    QPixmap pixmap(iconSize.width(), iconSize.height());
    pixmap.fill(color);
    icon.addPixmap(pixmap);

    return icon;
}

void PDFPageContentEditorStyleSettings::setColorToComboBox(QComboBox* comboBox, QColor color)
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

void PDFPageContentEditorStyleSettings::onSelectFontButtonClicked()
{
    bool ok = false;
    QFont font = QFontDialog::getFont(&ok, m_font, this, tr("Select Font"));

    if (ok && m_font != font)
    {
        m_font = font;
        ui->fontComboBox->setCurrentFont(m_font);
        emit fontChanged(m_font);
    }
}

void PDFPageContentEditorStyleSettings::setPenColor(QColor color)
{
    if (color.isValid() && m_pen.color() != color)
    {
        m_pen.setColor(color);
        setColorToComboBox(ui->penColorCombo, color);
        emit penChanged(m_pen);
    }
}

void PDFPageContentEditorStyleSettings::onSelectPenColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_pen.color(), this, tr("Select Color for Pen"), QColorDialog::ShowAlphaChannel);
    setPenColor(color);
}

void PDFPageContentEditorStyleSettings::setBrushColor(QColor color)
{
    if (color.isValid() && m_brush.color() != color)
    {
        m_brush.setColor(color);
        setColorToComboBox(ui->brushColorCombo, color);
        emit brushChanged(m_brush);
    }
}

Qt::Alignment PDFPageContentEditorStyleSettings::getAlignment() const
{
    return m_alignment;
}

PDFReal PDFPageContentEditorStyleSettings::getTextAngle() const
{
    return ui->textAngleEdit->value();
}

const QFont& PDFPageContentEditorStyleSettings::getFont() const
{
    return m_font;
}

const QBrush& PDFPageContentEditorStyleSettings::getBrush() const
{
    return m_brush;
}

const QPen& PDFPageContentEditorStyleSettings::getPen() const
{
    return m_pen;
}

void PDFPageContentEditorStyleSettings::onSelectBrushColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_pen.color(), this, tr("Select Color for Brush"), QColorDialog::ShowAlphaChannel);
    setBrushColor(color);
}

void PDFPageContentEditorStyleSettings::onPenWidthChanged(double value)
{
    if (m_pen.widthF() != value)
    {
        m_pen.setWidthF(value);
        emit penChanged(m_pen);
    }
}

void PDFPageContentEditorStyleSettings::onTextAngleChanged(double value)
{
    emit textAngleChanged(value);
}

void PDFPageContentEditorStyleSettings::onAlignmentRadioButtonClicked(int alignment)
{
    Qt::Alignment alignmentValue = static_cast<Qt::Alignment>(alignment);
    if (m_alignment != alignmentValue)
    {
        m_alignment = alignmentValue;
        emit alignmentChanged(m_alignment);
    }
}

void PDFPageContentEditorStyleSettings::onPenStyleChanged()
{
    Qt::PenStyle penStyle = static_cast<Qt::PenStyle>(ui->penStyleCombo->currentData().toInt());
    if (m_pen.style() != penStyle)
    {
        m_pen.setStyle(penStyle);
        emit penChanged(m_pen);
    }
}

void PDFPageContentEditorStyleSettings::onBrushStyleChanged()
{
    Qt::BrushStyle brushStyle = static_cast<Qt::BrushStyle>(ui->brushStyleCombo->currentData().toInt());
    if (m_brush.style() != brushStyle)
    {
        m_brush.setStyle(brushStyle);
        emit brushChanged(m_brush);
    }
}

void PDFPageContentEditorStyleSettings::onPenColorComboTextChanged()
{
    QColor color(ui->penColorCombo->currentText());
    if (color.isValid())
    {
        setColorToComboBox(ui->penColorCombo, color);

        if (m_pen.color() != color)
        {
            m_pen.setColor(color);
            emit penChanged(m_pen);
        }
    }
    else if (ui->penColorCombo->currentIndex() != -1)
    {
        ui->penColorCombo->setEditText(ui->penColorCombo->itemText(ui->penColorCombo->currentIndex()));
    }
}

void PDFPageContentEditorStyleSettings::onPenColorComboIndexChanged()
{
    const int index = ui->penColorCombo->currentIndex();
    QColor color = ui->penColorCombo->itemData(index, Qt::UserRole).value<QColor>();
    if (color.isValid() && m_pen.color() != color)
    {
        m_pen.setColor(color);
        emit penChanged(m_pen);
    }
}

void PDFPageContentEditorStyleSettings::onBrushColorComboTextChanged()
{
    QColor color(ui->brushColorCombo->currentText());
    if (color.isValid())
    {
        setColorToComboBox(ui->brushColorCombo, color);

        if (m_brush.color() != color)
        {
            m_brush.setColor(color);
            emit brushChanged(m_brush);
        }
    }
    else if (ui->brushColorCombo->currentIndex() != -1)
    {
        ui->brushColorCombo->setEditText(ui->brushColorCombo->itemText(ui->brushColorCombo->currentIndex()));
    }
}

void PDFPageContentEditorStyleSettings::onBrushColorComboIndexChanged()
{
    const int index = ui->brushColorCombo->currentIndex();
    QColor color = ui->brushColorCombo->itemData(index, Qt::UserRole).value<QColor>();
    if (color.isValid() && m_brush.color() != color)
    {
        m_brush.setColor(color);
        emit brushChanged(m_brush);
    }
}

void PDFPageContentEditorStyleSettings::onFontChanged(const QFont& font)
{
    if (m_font != font)
    {
        m_font = font;
        emit fontChanged(m_font);
    }
}

}   // namespace pdf
