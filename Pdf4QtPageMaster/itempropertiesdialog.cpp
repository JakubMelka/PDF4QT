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

#include "itempropertiesdialog.h"
#include "ui_itempropertiesdialog.h"

#include "pdfwidgetutils.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>

namespace pdfpagemaster
{

ItemPropertiesDialog::ItemPropertiesDialog(const PageItemModel* model, const QModelIndex& index, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::ItemPropertiesDialog),
    m_model(model),
    m_editableImageIndex(-1)
{
    ui->setupUi(this);
    loadItem(index);

    QSize size = pdf::PDFWidgetUtils::scaleDPI(this, QSize(520, 520));
    resize(size);
    pdf::PDFWidgetUtils::style(this);
}

ItemPropertiesDialog::~ItemPropertiesDialog()
{
    delete ui;
}

QString ItemPropertiesDialog::getImageDisplayName() const
{
    return ui->displayNameEdit->text();
}

QString ItemPropertiesDialog::getTypeText(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageType)
    {
        case PT_DocumentPage:
            return tr("PDF Page");
        case PT_Image:
            return tr("Image");
        case PT_Empty:
            return tr("Blank Page");
        default:
            break;
    }

    return QString();
}

QString ItemPropertiesDialog::getSourcePathText(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageType)
    {
        case PT_DocumentPage:
        {
            auto it = m_model->getDocuments().find(groupItem.documentIndex);
            return it != m_model->getDocuments().cend() ? it->second.fileName : QString();
        }

        case PT_Image:
        {
            auto it = m_model->getImages().find(groupItem.imageIndex);
            if (it != m_model->getImages().cend())
            {
                if (!it->second.sourcePath.isEmpty())
                {
                    return it->second.sourcePath;
                }
                return it->second.fileName;
            }
            break;
        }

        case PT_Empty:
            return QString();

        default:
            break;
    }

    return QString();
}

QString ItemPropertiesDialog::getImagePixelDimensionsText(const PageGroupItem::GroupItem& groupItem) const
{
    if (groupItem.pageType != PT_Image)
    {
        return QString();
    }

    auto it = m_model->getImages().find(groupItem.imageIndex);
    if (it == m_model->getImages().cend() || it->second.image.isNull())
    {
        return QString();
    }

    return tr("%1 x %2 px").arg(it->second.image.width()).arg(it->second.image.height());
}

QString ItemPropertiesDialog::getPageSizeText(const PageGroupItem::GroupItem& groupItem) const
{
    const QSizeF pageSize = PageItemModel::getCroppedPageDimensionsMM(groupItem);
    return QString("%1 x %2 mm")
            .arg(pageSize.width(), 0, 'f', 1)
            .arg(pageSize.height(), 0, 'f', 1);
}

QString ItemPropertiesDialog::getRotationText(pdf::PageRotation rotation) const
{
    switch (rotation)
    {
        case pdf::PageRotation::None:
            return tr("None");
        case pdf::PageRotation::Rotate90:
            return tr("90 deg");
        case pdf::PageRotation::Rotate180:
            return tr("180 deg");
        case pdf::PageRotation::Rotate270:
            return tr("270 deg");
        default:
            break;
    }

    return QString::number(int(rotation));
}

void ItemPropertiesDialog::setValueLabelText(QLabel* label, const QString& text) const
{
    label->setText(text);
    label->setToolTip(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
}

void ItemPropertiesDialog::loadItem(const QModelIndex& index)
{
    const PageGroupItem* item = m_model ? m_model->getItem(index) : nullptr;
    if (!item)
    {
        ui->buttonBox->setStandardButtons(QDialogButtonBox::Close);
        return;
    }

    const bool isSingleImage = item->groups.size() == 1 && item->groups.front().pageType == PT_Image;
    m_editableImageIndex = isSingleImage ? item->groups.front().imageIndex : -1;

    if (m_editableImageIndex != -1)
    {
        auto imageIt = m_model->getImages().find(m_editableImageIndex);
        ui->displayNameEdit->setText(imageIt != m_model->getImages().cend() ? imageIt->second.displayName : m_model->getItemDisplayText(item));
        ui->displayNameStack->setCurrentWidget(ui->displayNameEditPage);
        ui->buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    }
    else
    {
        setValueLabelText(ui->displayNameValueLabel, m_model->getItemDisplayText(item));
        ui->displayNameStack->setCurrentWidget(ui->displayNameLabelPage);
        ui->buttonBox->setStandardButtons(QDialogButtonBox::Close);
    }

    setValueLabelText(ui->typeValueLabel, m_model->getItemTypeText(item));

    QStringList sourcePaths;
    QStringList pixelDimensions;
    QStringList pageSizes;
    QStringList contentLines;
    int contentIndex = 1;

    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        const QString sourcePath = getSourcePathText(groupItem);
        if (!sourcePath.isEmpty() && !sourcePaths.contains(sourcePath))
        {
            sourcePaths << sourcePath;
        }

        const QString pixelDimension = getImagePixelDimensionsText(groupItem);
        if (!pixelDimension.isEmpty() && !pixelDimensions.contains(pixelDimension))
        {
            pixelDimensions << pixelDimension;
        }

        const QString pageSize = getPageSizeText(groupItem);
        if (!pageSize.isEmpty() && !pageSizes.contains(pageSize))
        {
            pageSizes << pageSize;
        }

        QStringList parts;
        parts << getTypeText(groupItem);
        if (!sourcePath.isEmpty())
        {
            parts << sourcePath;
        }
        if (groupItem.pageIndex > 0)
        {
            parts << tr("page %1").arg(groupItem.pageIndex);
        }
        if (!pixelDimension.isEmpty())
        {
            parts << pixelDimension;
        }
        parts << pageSize;
        parts << getRotationText(groupItem.pageAdditionalRotation);
        contentLines << QString("%1. %2").arg(contentIndex++).arg(parts.join(QStringLiteral(" | ")));
    }

    const QString originalPageText = m_model->getItemOriginalPageText(item);
    setValueLabelText(ui->sourcePathValueLabel, sourcePaths.isEmpty() ? tr("None") : sourcePaths.join(QLatin1Char('\n')));
    setValueLabelText(ui->originalPageValueLabel, originalPageText.isEmpty() ? tr("Not applicable") : originalPageText);
    setValueLabelText(ui->pixelDimensionsValueLabel, pixelDimensions.isEmpty() ? tr("Not applicable") : pixelDimensions.join(QLatin1String(", ")));
    setValueLabelText(ui->pageSizeValueLabel, pageSizes.join(QLatin1String(", ")));
    setValueLabelText(ui->rotationValueLabel, m_model->getItemRotationText(item));
    ui->groupContentsEdit->setPlainText(contentLines.join(QLatin1Char('\n')));
}

}   // namespace pdfpagemaster
