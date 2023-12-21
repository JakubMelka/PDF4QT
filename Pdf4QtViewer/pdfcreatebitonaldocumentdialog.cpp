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

#include "pdfcreatebitonaldocumentdialog.h"
#include "ui_pdfcreatebitonaldocumentdialog.h"

#include "pdfwidgetutils.h"
#include "pdfdocumentwriter.h"
#include "pdfimage.h"
#include "pdfexception.h"
#include "pdfwidgetutils.h"
#include "pdfimageconversion.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

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

#include "pdfdbgheap.h"

namespace pdfviewer
{

PDFCreateBitonalDocumentPreviewWidget::PDFCreateBitonalDocumentPreviewWidget(QWidget* parent) :
    QWidget(parent)
{

}

PDFCreateBitonalDocumentPreviewWidget::~PDFCreateBitonalDocumentPreviewWidget()
{

}

void PDFCreateBitonalDocumentPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    // Caption rect
    QRect captionRect = rect();
    captionRect.setHeight(painter.fontMetrics().lineSpacing() * 2);

    painter.fillRect(captionRect, QColor::fromRgb(0, 0, 128, 255));

    if (!m_caption.isEmpty())
    {
        painter.setPen(Qt::white);
        painter.drawText(captionRect, m_caption, QTextOption(Qt::AlignCenter));
    }

    QRect imageRect = rect();
    imageRect.setTop(captionRect.bottom());
    imageRect = imageRect.adjusted(16, 16, -32, -32);

    if (imageRect.isValid() && !m_image.isNull())
    {
        QRect imageDrawRect = imageRect;
        imageDrawRect.setSize(m_image.size().scaled(imageRect.size(), Qt::KeepAspectRatio));
        imageDrawRect.moveCenter(imageRect.center());
        painter.drawImage(imageDrawRect, m_image);
    }
}

void PDFCreateBitonalDocumentPreviewWidget::setCaption(QString caption)
{
    if (m_caption != caption)
    {
        m_caption = caption;
        update();
    }
}

void PDFCreateBitonalDocumentPreviewWidget::setImage(QImage image)
{
    m_image = std::move(image);
    update();
}

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
                QToolTip::showText(event->globalPos(), tr("Toggle this icon to switch image conversion to bitonal format on or off."), view);
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
                                                               pdf::PDFProgress* progress,
                                                               QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFCreateBitonalDocumentDialog),
    m_document(document),
    m_cms(cms),
    m_createBitonalDocumentButton(nullptr),
    m_conversionInProgress(false),
    m_processed(false),
    m_leftPreviewWidget(new PDFCreateBitonalDocumentPreviewWidget(this)),
    m_rightPreviewWidget(new PDFCreateBitonalDocumentPreviewWidget(this)),
    m_progress(progress)
{
    ui->setupUi(this);

    m_leftPreviewWidget->setCaption(tr("ORIGINAL"));
    m_rightPreviewWidget->setCaption(tr("BITONAL"));

    ui->mainGridLayout->addWidget(m_leftPreviewWidget, 1, 1);
    ui->mainGridLayout->addWidget(m_rightPreviewWidget, 1, 2);

    m_classifier.classify(document);
    m_imageReferences = m_classifier.getObjectsByType(pdf::PDFObjectClassifier::Image);

    m_createBitonalDocumentButton = ui->buttonBox->addButton(tr("Perform"), QDialogButtonBox::ActionRole);
    connect(m_createBitonalDocumentButton, &QPushButton::clicked, this, &PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked);
    connect(ui->automaticThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);
    connect(ui->manualThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updateUi);
    connect(ui->automaticThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updatePreview);
    connect(ui->manualThresholdRadioButton, &QRadioButton::clicked, this, &PDFCreateBitonalDocumentDialog::updatePreview);
    connect(ui->imageListWidget, &QListWidget::currentItemChanged, this, &PDFCreateBitonalDocumentDialog::updatePreview);
    connect(ui->thresholdEditBox, &QSpinBox::editingFinished, this, &PDFCreateBitonalDocumentDialog::updatePreview);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1024, 768));
    updateUi();
    pdf::PDFWidgetUtils::style(this);

    ui->imageListWidget->setItemDelegate( new ImagePreviewDelegate(&m_imagesToBeConverted, this));

    setGeometry(parent->geometry());

    loadImages();
    updatePreview();
}

PDFCreateBitonalDocumentDialog::~PDFCreateBitonalDocumentDialog()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    delete ui;
}

void PDFCreateBitonalDocumentDialog::onPerformFinished()
{
    m_future.waitForFinished();
    m_conversionInProgress = false;
    m_processed = true;
    updateUi();
}

void PDFCreateBitonalDocumentDialog::createBitonalDocument()
{
    std::vector<ImageConversionInfo> imagesToBeConverted;
    std::copy_if(m_imagesToBeConverted.begin(), m_imagesToBeConverted.end(), std::back_inserter(imagesToBeConverted), [](const auto& item) { return item.conversionEnabled; });

    // Do we have something to be converted?
    if (imagesToBeConverted.empty())
    {
        return;
    }

    pdf::ProgressStartupInfo info;
    info.showDialog = true;
    info.text = tr("Converting images...");
    m_progress->start(imagesToBeConverted.size(),  std::move(info));

    pdf::PDFObjectStorage storage = m_document->getStorage();

    pdf::PDFCMSGeneric genericCms;
    pdf::PDFRenderErrorReporterDummy errorReporter;

    for (int i = 0; i < imagesToBeConverted.size(); ++i)
    {
        pdf::PDFObjectReference reference = imagesToBeConverted[i].imageReference;
        std::optional<pdf::PDFImage> pdfImage = getImageFromReference(reference);

        QImage image;
        try
        {
            image = pdfImage->getImage(&genericCms, &errorReporter, nullptr);
        }
        catch (pdf::PDFException)
        {
            // Do nothing
        }

        // Just for code safety - this should never occur in here.
        if (image.isNull())
        {
            continue;
        }

        pdf::PDFImageConversion imageConversion;
        imageConversion.setConversionMethod(m_conversionMethod);
        imageConversion.setThreshold(m_manualThreshold);
        imageConversion.setImage(image);

        if (imageConversion.convert())
        {
            QImage bitonalImage = imageConversion.getConvertedImage();
            Q_ASSERT(bitonalImage.format() == QImage::Format_Mono);

            pdf::PDFBitWriter bitWriter(1);
            bitWriter.reserve(bitonalImage.sizeInBytes());
            for (int row = 0; row < bitonalImage.height(); ++row)
            {
                for (int col = 0; col < bitonalImage.width(); ++col)
                {
                    QRgb pixelValue = bitonalImage.pixel(col, row);
                    QRgb withoutAlphaValue = pixelValue & 0xFFFFFF;
                    int value = withoutAlphaValue > 0 ? 1 : 0;
                    bitWriter.write(value);
                }

                bitWriter.finishLine();
            }

            QByteArray imageData = bitWriter.takeByteArray();
            QByteArray compressedData = pdf::PDFFlateDecodeFilter::compress(imageData);

            pdf::PDFArray array;
            array.appendItem(pdf::PDFObject::createName("FlateDecode"));

            pdf::PDFDictionary dictionary;
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(image.width()));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(image.height()));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceGray"));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(1));
            dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Predictor"), pdf::PDFObject::createInteger(1));
            dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(compressedData.size()));
            dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Filter"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(qMove(array))));

            pdf::PDFObject imageObject = pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(qMove(dictionary), qMove(compressedData)));
            storage.setObject(reference, std::move(imageObject));
        }

        m_progress->step();
    }

    m_bitonalDocument = pdf::PDFDocument(std::move(storage), m_document->getInfo()->version, QByteArray());

    m_progress->finish();
}

void PDFCreateBitonalDocumentDialog::onCreateBitonalDocumentButtonClicked()
{
    Q_ASSERT(!m_conversionInProgress);
    Q_ASSERT(!m_future.isRunning());

    m_conversionMethod = ui->automaticThresholdRadioButton->isChecked() ? pdf::PDFImageConversion::ConversionMethod::Automatic : pdf::PDFImageConversion::ConversionMethod::Manual;
    m_manualThreshold = ui->thresholdEditBox->value();

    m_conversionInProgress = true;
    m_future = QtConcurrent::run([this]() { createBitonalDocument(); });
    m_futureWatcher.emplace();
    connect(&m_futureWatcher.value(), &QFutureWatcher<void>::finished, this, &PDFCreateBitonalDocumentDialog::onPerformFinished);
    m_futureWatcher->setFuture(m_future);
    updateUi();
}

void PDFCreateBitonalDocumentDialog::loadImages()
{
    QSize iconSize(QSize(256, 256));
    ui->imageListWidget->setIconSize(iconSize);
    QSize imageSize = iconSize * ui->imageListWidget->devicePixelRatioF();

    for (pdf::PDFObjectReference reference : m_imageReferences)
    {
        std::optional<pdf::PDFImage> pdfImage = getImageFromReference(reference);
        if (!pdfImage)
        {
            continue;
        }

        pdf::PDFCMSGeneric genericCms;
        pdf::PDFRenderErrorReporterDummy errorReporter;
        QImage image;

        try
        {
            image = pdfImage->getImage(&genericCms, &errorReporter, nullptr);
        }
        catch (pdf::PDFException)
        {
            // Do nothing
        }

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

void PDFCreateBitonalDocumentDialog::updatePreview()
{
    QModelIndex index = ui->imageListWidget->currentIndex();

    m_previewImageLeft = QImage();
    m_previewImageRight = QImage();

    if (index.isValid())
    {
        const ImageConversionInfo& info = m_imagesToBeConverted.at(index.row());

        std::optional<pdf::PDFImage> pdfImage = getImageFromReference(info.imageReference);
        Q_ASSERT(pdfImage);

        pdf::PDFCMSGeneric cmsGeneric;
        pdf::PDFRenderErrorReporterDummy reporter;
        QImage image = pdfImage->getImage(&cmsGeneric, &reporter, nullptr);

        pdf::PDFImageConversion imageConversion;
        imageConversion.setConversionMethod(ui->automaticThresholdRadioButton->isChecked() ? pdf::PDFImageConversion::ConversionMethod::Automatic : pdf::PDFImageConversion::ConversionMethod::Manual);
        imageConversion.setThreshold(ui->thresholdEditBox->value());
        imageConversion.setImage(image);

        if (imageConversion.convert())
        {
            m_previewImageLeft = image;
            m_previewImageRight = imageConversion.getConvertedImage();
        }
    }

    m_leftPreviewWidget->setImage(m_previewImageLeft);
    m_rightPreviewWidget->setImage(m_previewImageRight);
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


