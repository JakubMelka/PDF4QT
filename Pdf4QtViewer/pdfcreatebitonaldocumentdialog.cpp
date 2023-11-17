//    Copyright (C) 2023 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfcreateBitonaldocumentdialog.h"
#include "ui_pdfcreateBitonaldocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfimage.h"
#include "pdfdbgheap.h"
#include "pdfexception.h"
#include "pdfwidgetutils.h"

#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrent>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QtSvg/QSvgRenderer>
#include <QMouseEvent>
#include <QToolTip>

namespace pdfviewer
{

class ImagePreviewDelegate : public QStyledItemDelegate
{
public:
    ImagePreviewDelegate(std::vector<PDFCreateBitonalDocumentDialog::ImageConversionInfo>* imageConversionInfos, QObject* parent) :
        QStyledItemDelegate(parent),
        m_imageConversionInfos(imageConversionInfos)
    {
        m_yesRenderer.load(QString(":/resources/result-ok.svg"));
        m_noRenderer.load(QString(":/resources/result-error.svg"));
    }

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);

        QRect markRect = getMarkRect(option);

        if (index.isValid())
        {
            const PDFCreateBitonalDocumentDialog::ImageConversionInfo& info = m_imageConversionInfos->at(index.row());
            if (info.conversionEnabled)
            {
                m_yesRenderer.render(painter, markRect);
            }
            else
            {
                m_noRenderer.render(painter, markRect);
            }
        }
    }

    virtual bool editorEvent(QEvent* event,
                             QAbstractItemModel* model,
                             const QStyleOptionViewItem& option,
                             const QModelIndex& index)
    {
        Q_UNUSED(model);
        Q_UNUSED(index);

        if (event->type() == QEvent::MouseButtonPress && index.isValid())
        {
            QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);
            if (mouseEvent && mouseEvent->button() == Qt::LeftButton)
            {
                // Do we click on yes/no mark?
                QRectF markRect = getMarkRect(option);
                if (markRect.contains(mouseEvent->position()))
                {
                    PDFCreateBitonalDocumentDialog::ImageConversionInfo& info = m_imageConversionInfos->at(index.row());
                    info.conversionEnabled = !info.conversionEnabled;
                    return true;
                }
            }
        }

        return false;
    }

    virtual bool helpEvent(QHelpEvent* event,
                           QAbstractItemView* view,
                           const QStyleOptionViewItem& option,
                           const QModelIndex& index) override
    {
        Q_UNUSED(index);

        if (!event || !view)
        {
            return false;
        }

        if (event->type() == QEvent::ToolTip)
        {
            // Are we hovering over yes/no mark?
            QRectF markRect = getMarkRect(option);
            if (markRect.contains(event->pos()))
            {
                event->accept();
                QToolTip::showText(event->globalPos(), tr("Toggle this icon to switch image conversion to bitonic format on or off."), view);
                return true;
            }
        }

        return false;
    }

private:
    static constexpr QSize s_iconSize = QSize(24, 24);

    QRect getMarkRect(const QStyleOptionViewItem& option) const
    {
        QSize markSize = pdf::PDFWidgetUtils::scaleDPI(option.widget, s_iconSize);
        QRect markRect(option.rect.left(), option.rect.top(), markSize.width(), markSize.height());
        return markRect;
    }

    std::vector<PDFCreateBitonalDocumentDialog::ImageConversionInfo>* m_imageConversionInfos;
    mutable QSvgRenderer m_yesRenderer;
    mutable QSvgRenderer m_noRenderer;
};

PDFCreateBitonalDocumentDialog::PDFCreateBitonalDocumentDialog(const pdf::PDFDocument* document,
                                                               const pdf::PDFCMS* cms,
                                                               QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateBitonalDocumentDialog),
    m_document(document),
    m_cms(cms),
    m_createBitonalDocumentButton(nullptr),
    m_conversionInProgress(false),
    m_processed(false)
{
    ui->setupUi(this);

    m_classifier.classify(document);
    m_imageReferences = m_classifier.getObjectsByType(pdf::PDFObjectClassifier::Image);

    m_createBitonalDocumentButton = ui->buttonBox->addButton(tr("Process"), QDialogButtonBox::ActionRole);
    connect(m_createBitonalDocumentButton, &QPushButton::clicked, this, &PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked);
    connect(ui->automaticThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);
    connect(ui->manualThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(640, 380));
    updateUi();
    pdf::PDFWidgetUtils::style(this);

    ui->imageListWidget->setItemDelegate( new ImagePreviewDelegate(&m_imagesToBeConverted, this));

    loadImages();
}

PDFCreateBitonalDocumentDialog::~PDFCreateBitonalDocumentDialog()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFCreateBitonalDocumentDialog::createBitonalDocument()
{

}

void PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    m_conversionInProgress = true;
    m_future = QtConcurrent::run([this]() { createBitonalDocument(); });
    updateUi();
}

void PDFCreateBitonalDocumentDialog::loadImages()
{
    QSize iconSize(QSize(256, 256));
    ui->imageListWidget->setIconSize(iconSize);
    QSize imageSize = iconSize * ui->imageListWidget->devicePixelRatioF();

    int i = 0;
    for (pdf::PDFObjectReference reference : m_imageReferences)
    {
        if (i++>10)
        {
            break;
        }

        std::optional<pdf::PDFImage> pdfImage = getImageFromReference(reference);
        if (!pdfImage)
        {
            continue;
        }

        pdf::PDFCMSGeneric genericCms;
        pdf::PDFRenderErrorReporterDummy errorReporter;
        QImage image = pdfImage->getImage(&genericCms, &errorReporter, nullptr);
        if (image.isNull())
        {
            continue;
        }

        QListWidgetItem* item = new QListWidgetItem(ui->imageListWidget);
        image = image.scaled(imageSize.width(), imageSize.height(), Qt::KeepAspectRatio, Qt::FastTransformation);
        item->setIcon(QIcon(QPixmap::fromImage(image)));
        Qt::ItemFlags flags = item->flags();
        flags.setFlag(Qt::ItemIsEditable, true);
        item->setFlags(flags);

        ImageConversionInfo imageConversionInfo;
        imageConversionInfo.imageReference = reference;
        imageConversionInfo.conversionEnabled = true;
        m_imagesToBeConverted.push_back(imageConversionInfo);
    }
}

void PDFCreateBitonalDocumentDialog::updateUi()
{
    ui->thresholdEditBox->setEnabled(ui->manualThresholdRadioButton->isChecked());

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(m_processed && !m_conversionInProgress);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(!m_conversionInProgress);
    m_createBitonalDocumentButton->setEnabled(!m_conversionInProgress);
}

std::optional<pdf::PDFImage> PDFCreateBitonalDocumentDialog::getImageFromReference(pdf::PDFObjectReference reference) const
{
    std::optional<pdf::PDFImage> pdfImage;
    pdf::PDFObject imageObject = m_document->getObjectByReference(reference);
    pdf::PDFRenderErrorReporterDummy errorReporter;

    if (!imageObject.isStream())
    {
        // Image is not stream
        return pdfImage;
    }

    const pdf::PDFStream* stream = imageObject.getStream();
    try
    {
        pdf::PDFColorSpacePointer colorSpace;
        const pdf::PDFDictionary* streamDictionary = stream->getDictionary();
        if (streamDictionary->hasKey("ColorSpace"))
        {
            const pdf::PDFObject& colorSpaceObject = m_document->getObject(streamDictionary->get("ColorSpace"));
            if (colorSpaceObject.isName() || colorSpaceObject.isArray())
            {
                pdf::PDFDictionary dummyDictionary;
                colorSpace = pdf::PDFAbstractColorSpace::createColorSpace(&dummyDictionary, m_document, colorSpaceObject);
            }
        }
        pdfImage.emplace(pdf::PDFImage::createImage(m_document,
                                                    stream,
                                                    colorSpace,
                                                    false,
                                                    pdf::RenderingIntent::Perceptual,
                                                    &errorReporter));
    }
    catch (pdf::PDFException)
    {
        // Do nothing
    }

    return pdfImage;
}

}   // namespace pdfviewer
