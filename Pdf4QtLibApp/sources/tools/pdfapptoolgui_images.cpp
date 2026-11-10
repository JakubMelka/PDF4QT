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

#include "pdfapptoolgui_images.h"

#include "pdfappdocumenttoolbar.h"
#include "pdfappfiledrop.h"
#include "pdfapphomewidgets.h"
#include "pdfappimagepagepreview.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolgui_blank.h"
#include "pdfapptoolgui_interleave.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfpagesizepresets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfapphomedialogs.h"
#include "pdfexecutionpolicy.h"
#include "pdfthreadannotations.h"

#include <QAction>
#include <QBuffer>
#include <QButtonGroup>
#include <QClipboard>
#include <QCollator>
#include <QDir>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QThreadPool>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace pdf
{

/// A decoding of the thumbnail of a row of an image in the pool (checklist point 1): plain values
/// written in the GUI thread before the start (a copy of the bytes, implicitly shared) and read
/// there after the end (the hand-offs are annotated); only the cancellation is shared. The job
/// never holds the GUI.
class PDFAppImageThumbnailJob
{
public:
    /// Longest side of a decoded thumbnail (the slot 56 x 43 at 2x)
    static constexpr int DECODED_SIDE = 128;

    std::shared_ptr<PDFAppImageThumbnailLink> link;
    QString key;
    QByteArray data;
    QByteArray format;
    QImage picture;     ///< The result

    bool isCanceled() const { return m_isCanceled.load(std::memory_order_relaxed); }
    void cancel() { m_isCanceled.store(true, std::memory_order_relaxed); }

    /// Runs in a worker: the first frame, the orientation of EXIF applied, at most DECODED_SIDE
    void run()
    {
        if (isCanceled())
        {
            return;
        }
        QBuffer buffer;
        buffer.setData(data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, format);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && qMax(size.width(), size.height()) > DECODED_SIDE)
        {
            reader.setScaledSize(size.scaled(DECODED_SIDE, DECODED_SIDE, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
        }
        if (!isCanceled())
        {
            picture = reader.read();
        }
    }

private:
    std::atomic_bool m_isCanceled = false;
};

/// The way of a decoded thumbnail back to the cache: cut by the destructor of the cache (a late
/// thumbnail is dropped, nobody waits)
class PDFAppImageThumbnailLink
{
public:
    explicit PDFAppImageThumbnailLink(PDFAppImageThumbnailCache* cache) :
        m_cache(cache)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_cache = nullptr;
    }

    void finished(const std::shared_ptr<PDFAppImageThumbnailJob>& job)
    {
        QMutexLocker locker(&m_mutex);
        if (m_cache)
        {
            PDFThreadAnnotations::happensBefore(job.get());
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_cache, &PDFAppImageThumbnailCache::onThumbnailDecoded, Qt::QueuedConnection, job);
        }
    }

private:
    QMutex m_mutex;
    PDFAppImageThumbnailCache* m_cache;
};

/// The task of the pool: it holds the job, never the GUI
class PDFAppImageThumbnailTask
{
public:
    explicit PDFAppImageThumbnailTask(std::shared_ptr<PDFAppImageThumbnailJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()()
    {
        m_job->run();
        const std::shared_ptr<PDFAppImageThumbnailLink> link = m_job->link;
        link->finished(m_job);
    }

private:
    std::shared_ptr<PDFAppImageThumbnailJob> m_job;
};

/// Rename of an image (the link Rename of Selected image 336:1976; the layout of Rename workspace
/// 955:150171, no frame of its own): the name of the file is kept for the next image only by the
/// tool, the file is not renamed
class PDFAppRenameImageDialog : public PDFAppNameDialog
{
public:
    explicit PDFAppRenameImageDialog(PDFAppImagesToPdfToolGUI* gui, const QString& inputId, const QString& name, QWidget* parent) :
        PDFAppNameDialog(PDFAppImagesToPdfToolGUI::tr("Rename image"),
                         PDFAppImagesToPdfToolGUI::tr("The name is used in the tool and for the name of the PDF. The image file keeps its name."),
                         name, PDFAppImagesToPdfToolGUI::tr("Rename"), parent),
        m_gui(gui),
        m_inputId(inputId)
    {
        setObjectName(QStringLiteral("imagesRenameDialog"));
    }

protected:
    virtual bool submitName(const QString& name, QString* error) override
    {
        if (!m_gui)
        {
            *error = PDFAppImagesToPdfToolGUI::tr("The tool has been closed.");
            return false;
        }
        return m_gui->renameImage(m_inputId, name, error);
    }

private:
    QPointer<PDFAppImagesToPdfToolGUI> m_gui;
    QString m_inputId;
};

/// Helpers of the GUI of Images to PDF
class PDFAppImagesToPdfToolGUIHelper
{
public:
    PDFAppImagesToPdfToolGUIHelper() = delete;

    /// Widths of the panels of the toolbar (336:2639)
    static constexpr int ADD_PANEL_WIDTH = 290;
    static constexpr int SIZE_PANEL_WIDTH = 270;
    static constexpr int SORT_PANEL_WIDTH = 250;
    static constexpr int OPTIMIZE_PANEL_WIDTH = 260;

    /// Key of a row of the panel of Sort
    static constexpr const char* ROW_KEY = "pdf4qtImagesSortKey";

    /// The keys of Sort
    enum SortKey
    {
        SortByName,
        SortByFolder,
        SortByType,
        SortReverse
    };

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// An image for Sort
    struct SortEntry
    {
        QString id;
        QString name;
        QString folder;
        QString type;
    };

    /// The order of Sort by its key (336:2639): the name, the folder then the name, the type
    struct SortKeyLess
    {
        int key = SortByName;
        QCollator collator;

        bool operator()(const SortEntry& left, const SortEntry& right) const
        {
            switch (key)
            {
                case SortByFolder:
                {
                    const int folder = collator.compare(left.folder, right.folder);
                    return folder != 0 ? folder < 0 : collator.compare(left.name, right.name) < 0;
                }
                case SortByType:
                    return collator.compare(left.type, right.type) < 0;
                default:
                    return collator.compare(left.name, right.name) < 0;
            }
        }
    };

    static QStringList splitIds(const QString& text)
    {
        QStringList ids;
        for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            ids << part.trimmed();
        }
        return ids;
    }

    /// Image files… (336:2639: Ctrl+Alt+I) and Paste from clipboard (Ctrl+V)
    static QKeySequence getAddFilesKeys() { return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I); }
    static QKeySequence getPasteKeys() { return QKeySequence(QKeySequence::Paste); }

    /// The box of the crop overlay at whole pixels of the image
    static QRect toPixels(const QRectF& rect);

    /// "90°" of a turn
    static int getAngle(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
            default:
                return 0;
        }
    }

    /// Text of the format of QImageReader ("JPEG", "PNG", "TIFF")
    static QString getFormatText(const QByteArray& format)
    {
        const QString text = QString::fromLatin1(format).toUpper();
        if (text == QLatin1String("JPG"))
        {
            return QStringLiteral("JPEG");
        }
        if (text == QLatin1String("TIF"))
        {
            return QStringLiteral("TIFF");
        }
        return text;
    }
};

// ------------------------------------------------------------------ PDFAppToolGUIImages

void PDFAppToolGUIImages::registerImageTools()
{
    // Images to PDF and Interleave Pages: the tool and its GUI, each once
    PDFAppImagesToPdfToolGUI::registerTools();

    const QString interleaveId = QString::fromLatin1(PDFDocumentToolInterleave::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(interleaveId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolInterleave>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(interleaveId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(interleaveId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppInterleaveToolGUI>>());
    }
}

QString PDFAppToolGUIImages::getPresetName(const QSizeF& sizeInPoints)
{
    const QString id = PDFPageSizePresets::findPreset(sizeInPoints, 1.0);
    if (id == QLatin1String(PDFPageSizePresets::LETTER))
    {
        return tr("Letter");
    }
    if (id == QLatin1String(PDFPageSizePresets::LEGAL))
    {
        return tr("Legal");
    }
    if (id == QLatin1String(PDFPageSizePresets::SIZE_16X9))
    {
        return QStringLiteral("16:9");
    }
    return id.toUpper();
}

QString PDFAppToolGUIImages::getPageDescription(const QSizeF& sizeInPoints)
{
    if (sizeInPoints.isEmpty())
    {
        return QString();
    }

    // 336:2639 "A4 landscape", "A4 portrait"; another size by its millimeters
    const QString name = getPresetName(sizeInPoints);
    if (name.isEmpty())
    {
        return tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(sizeInPoints.width())),
                                    PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(sizeInPoints.height())));
    }
    return sizeInPoints.width() > sizeInPoints.height() ? tr("%1 landscape").arg(name) : tr("%1 portrait").arg(name);
}

// ------------------------------------------------------------- PDFAppToolSelectedItemCard

PDFAppToolSelectedItemCard* PDFAppToolPanelBuilder::addSelectedItemCard(const QString& title)
{
    // Plan of step 3, R1 d: the element of package D (its class lives in this file)
    PDFAppToolSelectedItemCard* card = new PDFAppToolSelectedItemCard(title, getWidget());
    addWidget(card);
    return card;
}

PDFAppToolSelectedItemCard::PDFAppToolSelectedItemCard(const QString& title, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolSelectedItem"));

    // section-Selected image 336:1976: the header (title, Tag, link) and the card 8 below it
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    QWidget* header = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(title, QFont::DemiBold, 13, PDFThemeColors::TextPrimary, header);
    headerLayout->addWidget(m_title);
    m_tag = new PDFTag(header);
    m_tag->setObjectName(QStringLiteral("toolSelectedItemTag"));
    m_tag->setFocusPolicy(Qt::NoFocus);
    m_tag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    headerLayout->addWidget(m_tag);
    headerLayout->addStretch(1);
    m_link = new PDFTextLink(header);
    m_link->setObjectName(QStringLiteral("toolSelectedItemLink"));
    connect(m_link.data(), &PDFTextLink::clicked, this, &PDFAppToolSelectedItemCard::onLinkClicked);
    headerLayout->addWidget(m_link);
    layout->addWidget(header);

    PDFAppToolCard* card = new PDFAppToolCard(this);
    m_values = new PDFKeyValueList(card);
    m_values->setObjectName(QStringLiteral("toolSelectedItemValues"));
    // 336:1976: the rows of 28 without a gap, as the card Summary below it
    m_values->setRowGap(0);
    card->getBodyLayout()->addWidget(m_values);
    layout->addWidget(card);

    setTagText(QString());
    setLinkText(QString());
}

PDFAppToolSelectedItemCard::~PDFAppToolSelectedItemCard() = default;

void PDFAppToolSelectedItemCard::setTagText(const QString& text)
{
    m_tag->setText(text);
    m_tag->setVisible(!text.isEmpty());
}

void PDFAppToolSelectedItemCard::setLinkText(const QString& text)
{
    m_link->setText(text);
    m_link->setVisible(!text.isEmpty());
}

void PDFAppToolSelectedItemCard::onLinkClicked()
{
    Q_EMIT linkActivated();
}

// ------------------------------------------------------------- PDFAppImageCropPanel

PDFAppImageCropPanel::PDFAppImageCropPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("imagesCropPanel"));

    // sidebar-right 336:3159: the sections 14 apart
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP + 6);

    // The header: "Crop" Semi Bold 13 and the Tag of the name (data)
    QWidget* section = new QWidget(this);
    QVBoxLayout* sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    QWidget* header = new QWidget(section);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, header);
    headerLayout->addWidget(m_title);
    m_nameTag = new PDFTag(header);
    m_nameTag->setObjectName(QStringLiteral("imagesCropName"));
    m_nameTag->setFocusPolicy(Qt::NoFocus);
    m_nameTag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    headerLayout->addWidget(m_nameTag);
    headerLayout->addStretch(1);
    sectionLayout->addWidget(header);

    // The card: Equal margins (Margin), Crop box (Left, Top, Width, Height), Apply to
    // (336:3448: padding 12 inside of the border, gap 8 - the card pads 12 / 6 itself)
    PDFAppToolCard* card = new PDFAppToolCard(section);
    QGridLayout* grid = new QGridLayout();
    grid->setContentsMargins(0, PDFAppToolCard::PADDING_H - PDFAppToolCard::PADDING_V, 0, PDFAppToolCard::PADDING_H - PDFAppToolCard::PADDING_V);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 1);
    card->getBodyLayout()->addLayout(grid);
    QButtonGroup* modes = new QButtonGroup(card);
    m_equalRadio = new QRadioButton(card);
    m_equalRadio->setObjectName(QStringLiteral("imagesCropEqual"));
    modes->addButton(m_equalRadio);
    grid->addWidget(m_equalRadio, 0, 0, 1, 2);
    m_marginField = createField(grid, 1, QString());
    m_boxRadio = new QRadioButton(card);
    m_boxRadio->setObjectName(QStringLiteral("imagesCropBox"));
    modes->addButton(m_boxRadio);
    grid->addWidget(m_boxRadio, 2, 0, 1, 2);
    m_leftField = createField(grid, 3, QString());
    m_topField = createField(grid, 4, QString());
    m_widthField = createField(grid, 5, QString());
    m_heightField = createField(grid, 6, QString());
    m_applyToField = new PDFComboBox(card);
    m_applyToField->setObjectName(QStringLiteral("imagesCropApplyTo"));
    m_applyToField->setFixedWidth(PDFAppToolMarginFields::FIELD_WIDTH);
    m_applyToField->addItem(QString());
    m_applyToField->addItem(QString());
    PDFLabel* applyToLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 13, PDFThemeColors::TextSecondary, card);
    applyToLabel->setBuddy(m_applyToField);
    grid->addWidget(applyToLabel, 7, 0);
    grid->addWidget(m_applyToField, 7, 1);
    m_fieldLabels[m_applyToField.data()] = applyToLabel;
    sectionLayout->addWidget(card);
    layout->addWidget(section);

    // Result: Original size, Size after crop
    QWidget* resultSection = new QWidget(this);
    QVBoxLayout* resultLayout = new QVBoxLayout(resultSection);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);
    m_resultTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, resultSection);
    resultLayout->addWidget(m_resultTitle);
    PDFAppToolCard* resultCard = new PDFAppToolCard(resultSection);
    m_result = new PDFKeyValueList(resultCard);
    m_result->setObjectName(QStringLiteral("imagesCropResult"));
    m_result->setRowGap(8);
    resultCard->getBodyLayout()->addWidget(m_result);
    resultLayout->addWidget(resultCard);
    layout->addWidget(resultSection);

    // Reset Crop (secondary) at the start, Apply Crop (primary, check) at the end
    QWidget* buttons = new QWidget(this);
    QHBoxLayout* buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->setSpacing(8);
    m_resetButton = PDFAppPageWidgets::createButton(QString(), QString(), false, buttons);
    m_resetButton->setObjectName(QStringLiteral("imagesCropReset"));
    m_applyButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("check"), true, buttons);
    m_applyButton->setObjectName(QStringLiteral("imagesCropApply"));
    buttonsLayout->addWidget(m_resetButton);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(m_applyButton);
    layout->addWidget(buttons);

    m_callout = new PDFAppToolCallout(PDFAccentHue::Blue, QStringLiteral("info"), QString(), QString(), this);
    m_callout->setObjectName(QStringLiteral("imagesCropCallout"));
    layout->addWidget(m_callout);
    layout->addStretch(1);

    connect(m_equalRadio.data(), &QRadioButton::toggled, this, &PDFAppImageCropPanel::onModeToggled);
    for (PDFSpinBox* field : { m_marginField.data(), m_leftField.data(), m_topField.data(), m_widthField.data(), m_heightField.data() })
    {
        connect(field, &QSpinBox::valueChanged, this, &PDFAppImageCropPanel::onFieldEdited);
    }
    connect(m_applyToField.data(), &QComboBox::activated, this, &PDFAppImageCropPanel::onApplyToActivated);
    connect(m_resetButton.data(), &QPushButton::clicked, this, &PDFAppImageCropPanel::onResetClicked);
    connect(m_applyButton.data(), &QPushButton::clicked, this, &PDFAppImageCropPanel::onApplyClicked);

    updateTexts();
    updateFields();
}

PDFAppImageCropPanel::~PDFAppImageCropPanel() = default;

PDFSpinBox* PDFAppImageCropPanel::createField(QGridLayout* grid, int row, const QString& label)
{
    // A field of 140 with " px" (336:3159 "120 px")
    QWidget* parent = grid->parentWidget();
    PDFSpinBox* field = new PDFSpinBox(parent);
    field->setFixedWidth(PDFAppToolMarginFields::FIELD_WIDTH);
    field->setKeyboardTracking(false);
    PDFLabel* labelWidget = PDFAppPageWidgets::createLabel(label, QFont::Normal, 13, PDFThemeColors::TextSecondary, parent);
    labelWidget->setBuddy(field);
    grid->addWidget(labelWidget, row, 0);
    grid->addWidget(field, row, 1);
    m_fieldLabels[field] = labelWidget;
    return field;
}

int PDFAppImageCropPanel::getEqualMargin(const QRect& rect, const QSize& imageSize)
{
    if (rect.isEmpty() || imageSize.isEmpty())
    {
        return 0;
    }
    const int right = imageSize.width() - rect.right() - 1;
    const int bottom = imageSize.height() - rect.bottom() - 1;
    return qMax(0, qMin(qMin(rect.left(), rect.top()), qMin(right, bottom)));
}

QRect PDFAppImageCropPanel::getEqualMarginRect(int margin, const QSize& imageSize)
{
    // At least one pixel stays (the rule of the core, PDFDocumentToolImagesToPdf::getClip)
    const int largest = qMax(0, (qMin(imageSize.width(), imageSize.height()) - 1) / 2);
    const int bounded = qBound(0, margin, largest);
    return QRect(QPoint(0, 0), imageSize).adjusted(bounded, bounded, -bounded, -bounded);
}

void PDFAppImageCropPanel::setValues(const Values& values)
{
    m_values = values;
    updateFields();
}

void PDFAppImageCropPanel::updateFields()
{
    // The values are said without a signal of the fields
    m_isUpdating = true;
    const QSize size = m_values.imageSize;
    const QRect rect = m_values.rect.isEmpty() ? QRect(QPoint(0, 0), size) : m_values.rect;
    const int largestMargin = qMax(0, (qMin(size.width(), size.height()) - 1) / 2);
    m_marginField->setRange(0, largestMargin);
    m_marginField->setValue(getEqualMargin(rect, size));
    m_leftField->setRange(0, qMax(0, size.width() - 1));
    m_leftField->setValue(rect.left());
    m_topField->setRange(0, qMax(0, size.height() - 1));
    m_topField->setValue(rect.top());
    m_widthField->setRange(1, qMax(1, size.width()));
    m_widthField->setValue(rect.width());
    m_heightField->setRange(1, qMax(1, size.height()));
    m_heightField->setValue(rect.height());
    m_equalRadio->setChecked(m_values.isEqualMargins);
    m_boxRadio->setChecked(!m_values.isEqualMargins);
    m_applyToField->setCurrentIndex(m_values.isAllImages ? 1 : 0);
    m_isUpdating = false;

    // The fields of the mode, which is not chosen, are disabled (336:3159)
    for (QWidget* field : { static_cast<QWidget*>(m_marginField.data()) })
    {
        field->setEnabled(m_values.isEqualMargins);
        m_fieldLabels[field]->setEnabled(m_values.isEqualMargins);
    }
    for (QWidget* field : { static_cast<QWidget*>(m_leftField.data()), static_cast<QWidget*>(m_topField.data()),
                            static_cast<QWidget*>(m_widthField.data()), static_cast<QWidget*>(m_heightField.data()) })
    {
        field->setEnabled(!m_values.isEqualMargins);
        m_fieldLabels[field]->setEnabled(!m_values.isEqualMargins);
    }

    m_nameTag->setText(m_values.name);
    m_nameTag->setVisible(!m_values.name.isEmpty());
    m_result->setValue(0, tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(size.width()), PDFAppLocale::formatNumber(size.height())));
    m_result->setValue(1, tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(rect.width()), PDFAppLocale::formatNumber(rect.height())));
    m_applyToField->setItemData(1, tr("The same margins for all %Ln images", nullptr, m_values.imageCount), Qt::ToolTipRole);
}

void PDFAppImageCropPanel::updateTexts()
{
    m_title->setText(tr("Crop"));
    m_equalRadio->setText(tr("Equal margins"));
    m_boxRadio->setText(tr("Crop box"));
    m_fieldLabels[m_marginField.data()]->setText(tr("Margin"));
    m_fieldLabels[m_leftField.data()]->setText(tr("Left"));
    m_fieldLabels[m_topField.data()]->setText(tr("Top"));
    m_fieldLabels[m_widthField.data()]->setText(tr("Width"));
    m_fieldLabels[m_heightField.data()]->setText(tr("Height"));
    m_fieldLabels[m_applyToField.data()]->setText(tr("Apply to"));
    for (PDFSpinBox* field : { m_marginField.data(), m_leftField.data(), m_topField.data(), m_widthField.data(), m_heightField.data() })
    {
        field->setSuffix(tr(" px"));
        field->setAccessibleName(m_fieldLabels[field]->text());
    }
    m_applyToField->setItemText(0, tr("Selected image"));
    m_applyToField->setItemText(1, tr("All images"));
    m_applyToField->setAccessibleName(tr("Apply to"));
    m_resultTitle->setText(tr("Result"));
    m_result->clear();
    m_result->addRow(tr("Original size"), QString());
    m_result->addRow(tr("Size after crop"), QString());
    m_resetButton->setText(tr("Reset Crop"));
    m_applyButton->setText(tr("Apply Crop"));
    m_callout->setTexts(tr("The image file is not changed"), tr("The crop is applied when the PDF is created. A new crop replaces the previous one."));
}

void PDFAppImageCropPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
        updateFields();
    }
}

void PDFAppImageCropPanel::onFieldEdited()
{
    if (m_isUpdating)
    {
        return;
    }

    // The box of the fields of the mode, bounded by the image
    const QSize size = m_values.imageSize;
    if (m_values.isEqualMargins)
    {
        m_values.rect = getEqualMarginRect(m_marginField->value(), size);
    }
    else
    {
        const int left = qBound(0, m_leftField->value(), qMax(0, size.width() - 1));
        const int top = qBound(0, m_topField->value(), qMax(0, size.height() - 1));
        const int width = qBound(1, m_widthField->value(), qMax(1, size.width() - left));
        const int height = qBound(1, m_heightField->value(), qMax(1, size.height() - top));
        m_values.rect = QRect(left, top, width, height);
    }
    updateFields();
    Q_EMIT valuesEdited();
}

void PDFAppImageCropPanel::onModeToggled(bool isChecked)
{
    Q_UNUSED(isChecked);
    if (m_isUpdating)
    {
        return;
    }

    // Equal margins makes the box of the smallest margin of the box (no change for Crop box)
    m_values.isEqualMargins = m_equalRadio->isChecked();
    if (m_values.isEqualMargins)
    {
        m_values.rect = getEqualMarginRect(getEqualMargin(m_values.rect, m_values.imageSize), m_values.imageSize);
    }
    updateFields();
    Q_EMIT valuesEdited();
}

void PDFAppImageCropPanel::onApplyToActivated(int index)
{
    m_values.isAllImages = index == 1;
    updateFields();
    Q_EMIT valuesEdited();
}

void PDFAppImageCropPanel::onResetClicked()
{
    Q_EMIT resetClicked();
}

void PDFAppImageCropPanel::onApplyClicked()
{
    Q_EMIT applyClicked();
}

// ------------------------------------------------------------- PDFAppImagesToPdfCanvas

PDFAppImagesToPdfCanvas::PDFAppImagesToPdfCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("imagesCanvas"));

    // document-canvas 336:2009: padding 20, gap 12
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(GAP);

    // preview-bar 336:2217: the title Semi Bold 13, the segments Fit / 100 % (60 each)
    QWidget* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("imagesPreviewBar"));
    bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, bar);
    m_title->setObjectName(QStringLiteral("imagesPreviewTitle"));
    m_title->setMinimumWidth(1);
    barLayout->addWidget(m_title, 1);
    m_zoom = new PDFSegmentedControl(bar);
    m_zoom->setObjectName(QStringLiteral("imagesPreviewZoom"));
    m_zoom->addSegment(QString());
    m_zoom->addSegment(QString());
    m_zoom->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    connect(m_zoom.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppImagesToPdfCanvas::onZoomIndexChanged);
    barLayout->addWidget(m_zoom);
    layout->addWidget(bar);

    // stage 336:2234: the page and its caption 12 below it, centered; 100 % scrolls
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("imagesStageArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setProperty("overlayScrollBars", true);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_stage = new QWidget(m_scrollArea);
    m_stage->setObjectName(QStringLiteral("imagesStage"));
    m_stage->setAutoFillBackground(false);
    QVBoxLayout* stageLayout = new QVBoxLayout(m_stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(GAP);
    m_preview = new PDFAppImagePagePreview(m_stage);
    m_preview->setContentsMargins(STAGE_MARGIN, STAGE_MARGIN, STAGE_MARGIN, STAGE_MARGIN);
    stageLayout->addWidget(m_preview, 1);

    // 336:3159: the image alone with the box of the crop (hidden until the crop mode)
    m_cropStage = new PDFAppToolPageStage(m_stage);
    m_cropStage->setObjectName(QStringLiteral("imagesCropStage"));
    m_cropStage->hide();
    stageLayout->addWidget(m_cropStage, 1);
    // M11: the caption lies directly below the page (placeCaption), not at the bottom of the stage
    m_caption = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextSecondary, m_stage);
    m_caption->setObjectName(QStringLiteral("imagesStageCaption"));
    m_caption->setAlignment(Qt::AlignCenter);
    m_stage->installEventFilter(this);
    m_preview->installEventFilter(this);
    m_cropStage->installEventFilter(this);
    connect(m_cropStage.data(), &PDFAppToolPageStage::pageGeometryChanged, this, &PDFAppImagesToPdfCanvas::placeCaption);
    m_scrollArea->setWidget(m_stage);
    layout->addWidget(m_scrollArea, 1);

    // drop-zone 336:2360: "Drop images here", Add Images…, the formats
    m_emptyState = new QWidget(this);
    m_emptyState->setObjectName(QStringLiteral("imagesEmptyState"));
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->addStretch(1);
    PDFAppCard* card = PDFAppPageWidgets::createCard(28, 12, m_emptyState);
    card->setObjectName(QStringLiteral("imagesDropCard"));
    card->setMaximumWidth(560);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Drop images here"), QFont::DemiBold, 16, PDFThemeColors::TextPrimary, card);
    title->setAlignment(Qt::AlignCenter);
    card->getLayout()->addWidget(title);
    PDFAppParagraph* text = PDFAppPageWidgets::createText(tr("or paste one from the clipboard with Ctrl+V"), PDFThemeFonts::getFont(QFont::Normal, 12), 17, PDFThemeColors::TextSecondary, card);
    text->setAlignment(Qt::AlignCenter);
    card->getLayout()->addWidget(text);
    m_addImagesButton = PDFAppPageWidgets::createButton(tr("Add Images…"), QStringLiteral("plus"), true, card);
    m_addImagesButton->setObjectName(QStringLiteral("imagesAddImages"));
    card->getLayout()->addWidget(m_addImagesButton, 0, Qt::AlignHCenter);
    PDFAppParagraph* formats = PDFAppPageWidgets::createText(tr("PNG, JPEG, TIFF, BMP, WebP, GIF and every other raster format the application can read.\nFiles that cannot be read are skipped and listed by name."),
                                                             PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, card);
    formats->setAlignment(Qt::AlignCenter);
    card->getLayout()->addWidget(formats);
    emptyLayout->addWidget(card, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(2);
    layout->addWidget(m_emptyState, 1);
    m_emptyState->hide();

    updateTexts();
    setFit(true);
}

PDFAppImagesToPdfCanvas::~PDFAppImagesToPdfCanvas() = default;

void PDFAppImagesToPdfCanvas::setTitle(const QString& title)
{
    m_title->setText(title);
}

void PDFAppImagesToPdfCanvas::setCaption(const QString& caption)
{
    m_caption->setText(caption);
    m_preview->setAccessibleName(caption);
    placeCaption();
}

void PDFAppImagesToPdfCanvas::setPage(const QImage& image, QSizeF imageSize, const PDFImagePageLayout& layout)
{
    m_preview->setImage(image, imageSize);
    m_preview->setPageLayout(layout);
    placeCaption();
}

bool PDFAppImagesToPdfCanvas::eventFilter(QObject* watched, QEvent* event)
{
    // The type first: a resize or a show comes only to a living stage
    if ((event->type() == QEvent::Resize || event->type() == QEvent::Show) &&
        (watched == m_stage.data() || watched == m_preview.data() || watched == m_cropStage.data()))
    {
        placeCaption();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppImagesToPdfCanvas::placeCaption()
{
    if (!m_stage || !m_preview || !m_cropStage || !m_caption || !m_zoom)
    {
        return;
    }

    // The room of the caption below the page area (336:2234: the caption 12 below the page)
    const int captionHeight = m_caption->text().isEmpty() ? 0 : m_caption->sizeHint().height();
    const int bottom = captionHeight > 0 ? captionHeight + GAP : 0;
    if (QLayout* layout = m_stage->layout(); layout && layout->contentsMargins().bottom() != bottom)
    {
        layout->setContentsMargins(0, 0, 0, bottom);
    }

    // Fit: the page takes FIT_SHARE of the stage, centered with its caption; at least the room of
    // the shadow (the crop stage has its own share)
    QMargins margins(STAGE_MARGIN, STAGE_MARGIN, STAGE_MARGIN, STAGE_MARGIN);
    if (m_zoom->getCurrentIndex() == 0)
    {
        const int horizontal = qMax(STAGE_MARGIN, qRound(m_preview->width() * (1.0 - FIT_SHARE) * 0.5));
        const int vertical = qMax(STAGE_MARGIN, qRound(m_preview->height() * (1.0 - FIT_SHARE) * 0.5));
        margins = QMargins(horizontal, vertical, horizontal, vertical);
    }
    if (m_preview->contentsMargins() != margins)
    {
        m_preview->setContentsMargins(margins);
    }

    const bool isCrop = isCropMode();
    QWidget* shown = isCrop ? static_cast<QWidget*>(m_cropStage.data()) : static_cast<QWidget*>(m_preview.data());
    const QRectF pageRect = isCrop ? m_cropStage->getPageRect() : m_preview->getPageRect();
    int top = m_stage->height() - captionHeight;
    if (!pageRect.isEmpty())
    {
        top = shown->mapTo(m_stage.data(), QPoint(0, qCeil(pageRect.bottom()))).y() + GAP;
    }
    m_caption->setGeometry(0, top, m_stage->width(), captionHeight);
}

void PDFAppImagesToPdfCanvas::setFit(bool isFit)
{
    // The segments write the mode (onZoomIndexChanged), no signal of the canvas
    const QSignalBlocker blocker(m_zoom);
    m_zoom->setCurrentIndex(isFit ? 0 : 1);
    updateStage();
}

bool PDFAppImagesToPdfCanvas::isFit() const
{
    return m_zoom->getCurrentIndex() == 0;
}

void PDFAppImagesToPdfCanvas::setEmpty(bool isEmpty)
{
    m_emptyState->setVisible(isEmpty);
    m_scrollArea->setVisible(!isEmpty);
    m_zoom->setVisible(!isEmpty);
}

bool PDFAppImagesToPdfCanvas::isEmpty() const
{
    return !m_emptyState->isHidden();
}

void PDFAppImagesToPdfCanvas::setCropMode(bool isCropMode)
{
    m_preview->setVisible(!isCropMode);
    m_cropStage->setVisible(isCropMode);
    updateStage();
    placeCaption();
}

bool PDFAppImagesToPdfCanvas::isCropMode() const
{
    return !m_cropStage->isHidden();
}

void PDFAppImagesToPdfCanvas::onZoomIndexChanged(int index)
{
    Q_UNUSED(index);
    updateStage();
    Q_EMIT zoomChanged();
}

void PDFAppImagesToPdfCanvas::updateStage()
{
    // Fit fills the area, 100 % has the size of the page and scrolls
    const bool isFit = m_zoom->getCurrentIndex() == 0;
    m_preview->setFit(isFit);
    m_cropStage->setZoom(isFit ? PDFAppToolPageStage::Zoom::Fit : PDFAppToolPageStage::Zoom::Actual);
    m_scrollArea->setHorizontalScrollBarPolicy(isFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(isFit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    placeCaption();
}

void PDFAppImagesToPdfCanvas::updateTexts()
{
    // The segments of the canvas follow the language (as PDFAppPageSizePreview, 871:135793)
    const QStringList segments = { tr("Fit"), tr("100 %") };
    const QList<QToolButton*> buttons = m_zoom->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (qsizetype i = 0; i < buttons.size() && i < segments.size(); ++i)
    {
        buttons[i]->setText(segments[i]);
    }
    m_zoom->setAccessibleName(tr("Size of the page"));
}

void PDFAppImagesToPdfCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 336:2009: bg/track
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
}

void PDFAppImagesToPdfCanvas::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

// ------------------------------------------------------------ PDFAppImageThumbnailCache

PDFAppImageThumbnailCache::PDFAppImageThumbnailCache(QObject* parent) :
    BaseClass(parent),
    m_link(std::make_shared<PDFAppImageThumbnailLink>(this))
{

}

PDFAppImageThumbnailCache::~PDFAppImageThumbnailCache()
{
    // The decodings go on with their own values, their late thumbnails are dropped; nobody waits
    m_link->detach();
    for (const auto& item : m_pending)
    {
        item.second->cancel();
    }
}

QString PDFAppImageThumbnailCache::getKey(const PDFAppToolInput& input)
{
    return input.id + QLatin1Char('/') + QString::number(input.contentGeneration);
}

QImage PDFAppImageThumbnailCache::getThumbnail(const PDFAppToolInput& input)
{
    const QString key = getKey(input);
    auto it = m_thumbnails.find(key);
    if (it != m_thumbnails.cend())
    {
        // A null picture: the image could not be decoded (the empty slot)
        return it->second;
    }
    if (m_pending.count(key) || input.imageData.isEmpty())
    {
        return QImage();
    }

    // Checklist point 1: the job gets copies of plain values (the bytes are implicitly shared and
    // never written), never the input or the list
    std::shared_ptr<PDFAppImageThumbnailJob> job = std::make_shared<PDFAppImageThumbnailJob>();
    job->link = m_link;
    job->key = key;
    job->data = input.imageData;
    job->format = input.imageFormat;
    m_pending[key] = job;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppImageThumbnailTask(std::move(job)));
    return QImage();
}

void PDFAppImageThumbnailCache::retain(const std::set<QString>& keys)
{
    for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();)
    {
        it = keys.count(it->first) ? std::next(it) : m_thumbnails.erase(it);
    }
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        if (keys.count(it->first))
        {
            ++it;
            continue;
        }
        it->second->cancel();
        it = m_pending.erase(it);
    }
}

void PDFAppImageThumbnailCache::onThumbnailDecoded(const std::shared_ptr<PDFAppImageThumbnailJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    // A thumbnail of an image, which is not in the list any more (retain), is dropped
    auto it = m_pending.find(job->key);
    if (it == m_pending.end() || it->second != job)
    {
        return;
    }
    m_pending.erase(it);
    if (job->isCanceled())
    {
        return;
    }
    m_thumbnails[job->key] = std::move(job->picture);
    Q_EMIT thumbnailDecoded();
}

// ------------------------------------------------------------ PDFAppImagesToPdfToolGUI

PDFAppImagesToPdfToolGUI::PDFAppImagesToPdfToolGUI(QObject* parent) :
    BaseClass(parent),
    m_thumbnailCache(new PDFAppImageThumbnailCache(this))
{
    connect(m_thumbnailCache.data(), &PDFAppImageThumbnailCache::thumbnailDecoded, this, &PDFAppImagesToPdfToolGUI::onThumbnailDecoded);
}

PDFAppImagesToPdfToolGUI::~PDFAppImagesToPdfToolGUI()
{
    // The cache (a child) cuts its link: the decodings go on with their own values, their late
    // thumbnails are dropped; nobody waits
    delete m_thumbnailCache.data();

    // The dialog of Rename holds this GUI by a QPointer; it goes with the tool
    if (m_renameDialog)
    {
        m_renameDialog->close();
    }

    // The key widget gets back what it had: its drops and no actions of the tool
    if (m_keyWidget)
    {
        m_keyWidget->removeEventFilter(this);
        m_keyWidget->setAcceptDrops(m_wasDropAccepted);
    }
    delete m_addFilesAction.data();
    delete m_pasteAction.data();
}

void PDFAppImagesToPdfToolGUI::registerTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolImagesToPdf::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolImagesToPdf>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppImagesToPdfToolGUI>>());
    }
}

PDFAppToolInputRules PDFAppImagesToPdfToolGUI::getInputRules() const
{
    // K41: 1 to 1000 images, no PDF
    PDFAppToolInputRules rules;
    rules.minimum = 1;
    rules.maximum = PDFDocumentToolImagesToPdf::MAXIMUM_INPUT_COUNT;
    rules.acceptsPdf = false;
    rules.acceptsImages = true;
    return rules;
}

bool PDFAppImagesToPdfToolGUI::isResultInThisTab() const
{
    // 336:3572: Open in this tab (Recommended), R16
    return true;
}

// ------------------------------------------------------------- the list and the values

void PDFAppImagesToPdfToolGUI::connectList()
{
    PDFAppToolInputList* list = getInputList();
    if (list == m_list)
    {
        return;
    }

    if (m_list)
    {
        disconnect(m_list.data(), nullptr, this, nullptr);
    }
    m_list = list;
    if (m_list)
    {
        const PDFAppToolInputRules rules = getInputRules();
        m_list->setAcceptedInputs(rules.maximum, rules.acceptsPdf, rules.acceptsImages, tr("Images to PDF takes images"));
        connect(m_list.data(), &PDFAppToolInputList::inputsChanged, this, &PDFAppImagesToPdfToolGUI::onListChanged);
        connect(m_list.data(), &PDFAppToolInputList::inputChanged, this, &PDFAppImagesToPdfToolGUI::onListInputChanged);
        connect(m_list.data(), &QObject::destroyed, this, &PDFAppImagesToPdfToolGUI::onListDestroyed);
    }
    initializeValues();
}

void PDFAppImagesToPdfToolGUI::initializeValues()
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (m_isValuesInitialized || !model || !m_list)
    {
        return;
    }

    // K21: the images, which the tab came with, are no change of the user (no undo step)
    m_isValuesInitialized = true;
    if (getModelInputIds().isEmpty() && m_list->getCount() > 0)
    {
        model->setFollowingValue(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_INPUTS), m_list->getInputIds().join(QLatin1Char(',')));
    }
    readValues();
}

QStringList PDFAppImagesToPdfToolGUI::getModelInputIds() const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? PDFAppImagesToPdfToolGUIHelper::splitIds(model->getValue(PDFAppImagesToPdfToolGUIHelper::name(PDFDocumentToolImagesToPdf::PARAMETER_INPUTS)).toString()) : QStringList();
}

QString PDFAppImagesToPdfToolGUI::getParameter(const char* name) const
{
    const PDFAppToolParameterModel* model = getModel();
    return model ? model->getValue(QString::fromLatin1(name)).toString() : QString();
}

bool PDFAppImagesToPdfToolGUI::isEditable() const
{
    return !m_isRunning;
}

void PDFAppImagesToPdfToolGUI::readValues()
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    const PDFAppToolParameterModel* model = getModel();
    if (!model || !m_list)
    {
        return;
    }

    // The images of the list follow the values (Undo / Redo: a parked image comes back without a
    // second reading); the list announces it, the slot waits
    QStringList ids = getModelInputIds();
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    if (ids != m_list->getInputIds())
    {
        m_isChangingList = true;
        m_list->setInputIds(ids);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
        if (!m_list)
        {
            return;
        }
        ids = m_list->getInputIds();
    }

    // The parked images, which no step of the history names, are freed (R12 of step 2)
    QStringList historyIds;
    for (const QVariant& value : model->getHistoryValues(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_INPUTS)))
    {
        historyIds << Helper::splitIds(value.toString());
    }
    historyIds << ids;
    m_list->dropParkedInputs(historyIds);
    if (!guard || !m_list)
    {
        return;
    }

    // The adjustments by the ids (their letters are the order of the images)
    m_adjustments.clear();
    const QString text = model->getValue(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_ADJUSTMENTS)).toString();
    if (const std::optional<std::map<int, PDFDocumentToolImagesToPdf::Adjustment>> parsed = PDFDocumentToolImagesToPdf::parseAdjustments(text, int(ids.size()), nullptr))
    {
        for (const auto& [index, adjustment] : *parsed)
        {
            m_adjustments[ids.at(index)] = adjustment;
        }
    }

    if (!ids.contains(m_selectedId))
    {
        m_selectedId = ids.isEmpty() ? QString() : ids.front();
    }

    // The selection of more images keeps the images of the list; the crop ends without its image
    QStringList extra;
    for (const QString& id : std::as_const(m_extraSelectedIds))
    {
        if (id != m_selectedId && ids.contains(id) && !extra.contains(id))
        {
            extra << id;
        }
    }
    m_extraSelectedIds = extra;
    const PDFAppToolInput* selected = m_list->findInput(m_selectedId);
    if (m_isCropping && (!selected || !selected->isUsable()))
    {
        m_isCropping = false;
    }
    updateAll();
}

void PDFAppImagesToPdfToolGUI::applyChange(const QStringList& inputIds, const Adjustments& adjustments)
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    PDFAppToolParameterModel* model = getModel();
    if (!model)
    {
        return;
    }

    // One undo step of both values; the model announces it (readValues)
    std::map<int, PDFDocumentToolImagesToPdf::Adjustment> byIndex;
    for (int i = 0; i < inputIds.size(); ++i)
    {
        auto it = adjustments.find(inputIds.at(i));
        if (it != adjustments.cend() && !it->second.isEmpty())
        {
            byIndex[i] = it->second;
        }
    }
    QVariantMap values;
    values.insert(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_INPUTS), inputIds.join(QLatin1Char(',')));
    values.insert(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_ADJUSTMENTS), PDFDocumentToolImagesToPdf::createAdjustments(byIndex));

    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    const QString error = model->setValues(values);
    if (guard && !error.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(error);
    }
}

QStringList PDFAppImagesToPdfToolGUI::getUsableInputIds() const
{
    QStringList ids;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        if (input->isUsable())
        {
            ids << input->id;
        }
    }
    return ids;
}

void PDFAppImagesToPdfToolGUI::onListChanged()
{
    if (!m_isChangingList)
    {
        readValues();
    }
}

void PDFAppImagesToPdfToolGUI::onListInputChanged(const QString& inputId)
{
    // A reading finished: the rows, the page and the summary (no undo step)
    Q_UNUSED(inputId);
    if (!m_isChangingList)
    {
        updateAll();
    }
}

void PDFAppImagesToPdfToolGUI::onListDestroyed()
{
    m_list = nullptr;
}

// ------------------------------------------------------------- the actions of the user

void PDFAppImagesToPdfToolGUI::addResults(const std::vector<PDFAppToolInputList::AddResult>& results, const QStringList& refusedTexts)
{
    if (!m_list)
    {
        return;
    }

    QStringList refused = refusedTexts;
    QString lastAdded;
    for (const PDFAppToolInputList::AddResult& result : results)
    {
        if (result.id.isEmpty())
        {
            refused << PDFAppToolInputList::getRefusalText(result.refusal, result.name, m_list->getMaximumCount());
        }
        else
        {
            lastAdded = result.id;
        }
    }

    // The new images are selected (the first of them shows its page); the refusals are said in
    // the status bar (the last call)
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    if (!lastAdded.isEmpty())
    {
        if (m_selectedId.isEmpty() || !m_list->findInput(m_selectedId))
        {
            m_selectedId = lastAdded;
        }
        applyChange(m_list->getInputIds(), m_adjustments);
    }
    if (guard && !refused.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(refused.join(QStringLiteral(" · ")));
    }
}

void PDFAppImagesToPdfToolGUI::addFiles(const QStringList& filePaths)
{
    if (!m_list || !isEditable() || filePaths.isEmpty())
    {
        return;
    }

    // The list announces each addition, the values follow at once as one undo step
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    QPointer<PDFAppToolInputList> list(m_list);
    m_isChangingList = true;
    const std::vector<PDFAppToolInputList::AddResult> results = list->addFiles(filePaths);
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults(results, QStringList());
}

void PDFAppImagesToPdfToolGUI::addFilesFromDialog()
{
    if (!isEditable())
    {
        return;
    }

    // The native dialog runs its own event loop: the GUI may go meanwhile
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    QWidget* parent = m_canvas ? m_canvas->window() : nullptr;
    const QStringList files = PDFAppFileDialogs::getOpenInputFileNames(parent, QString());
    if (guard && !files.isEmpty())
    {
        addFiles(files);
    }
}

void PDFAppImagesToPdfToolGUI::pasteImage()
{
    if (!m_list || !isEditable())
    {
        return;
    }

    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QImage image = clipboard ? clipboard->image() : QImage();
    if (image.isNull())
    {
        if (getHost())
        {
            getHost()->showTransientMessage(tr("The clipboard has no image"));
        }
        return;
    }

    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    m_isChangingList = true;
    const PDFAppToolInputList::AddResult result = m_list->addImage(image, QString());
    if (!guard)
    {
        return;
    }
    m_isChangingList = false;
    addResults({ result }, QStringList());
}

void PDFAppImagesToPdfToolGUI::turnSelected(bool isClockwise)
{
    if (!m_list || !isEditable() || m_selectedId.isEmpty() || !m_list->findInput(m_selectedId))
    {
        return;
    }

    // A quarter turn more of each selected image (5.8: the image turns, the page follows by Auto)
    Adjustments adjustments = m_adjustments;
    const PageRotation quarter = isClockwise ? PageRotation::Rotate90 : PageRotation::Rotate270;
    for (const QString& id : getSelectedImages())
    {
        PDFDocumentToolImagesToPdf::Adjustment& adjustment = adjustments[id];
        adjustment.turn = getPageRotationCombined(adjustment.turn, quarter);
    }
    applyChange(m_list->getInputIds(), adjustments);
}

QStringList PDFAppImagesToPdfToolGUI::getSelectedImages() const
{
    // In the order of the pages
    QStringList ids;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const QString& id = m_list->getInput(i)->id;
        if (id == m_selectedId || m_extraSelectedIds.contains(id))
        {
            ids << id;
        }
    }
    return ids;
}

void PDFAppImagesToPdfToolGUI::clickImage(const QString& inputId, Qt::KeyboardModifiers modifiers)
{
    if (!m_list || !m_list->findInput(inputId))
    {
        return;
    }

    // The crop works with one image: a click chooses the image of the crop
    if (m_isCropping || !(modifiers & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        m_extraSelectedIds.clear();
        if (m_isCropping && m_selectedId != inputId)
        {
            // The box of the crop of the other image; an image, which cannot be read, ends it
            m_selectedId = inputId;
            const PDFAppToolInput* clicked = m_list->findInput(inputId);
            setCropping(clicked->isUsable());
            return;
        }
        m_selectedId = inputId;
        updateAll();
        return;
    }

    QStringList selected = getSelectedImages();
    if (modifiers & Qt::ShiftModifier)
    {
        // The images from the image of the preview to the clicked one
        const int from = m_list->indexOf(m_selectedId);
        const int to = m_list->indexOf(inputId);
        selected.clear();
        for (int i = qMin(from, to); from >= 0 && i <= qMax(from, to); ++i)
        {
            selected << m_list->getInput(i)->id;
        }
        m_selectedId = inputId;
    }
    else if (selected.contains(inputId) && selected.size() > 1)
    {
        // Ctrl on a selected image takes it out; the preview shows the last of the others
        selected.removeAll(inputId);
        if (m_selectedId == inputId)
        {
            m_selectedId = selected.back();
        }
    }
    else
    {
        selected << inputId;
        m_selectedId = inputId;
    }
    selected.removeAll(m_selectedId);
    m_extraSelectedIds = selected;
    updateAll();
}

bool PDFAppImagesToPdfToolGUI::renameImage(const QString& inputId, const QString& name, QString* errorText)
{
    QString dummy;
    QString& error = errorText ? *errorText : dummy;
    const PDFAppToolInput* input = m_list ? m_list->findInput(inputId) : nullptr;
    if (!input || m_list->indexOf(inputId) < 0 || !isEditable())
    {
        error = tr("The image is not in the list any more.");
        return false;
    }
    const QString validName = PDFAppToolInputList::getValidName(name);
    if (validName.isEmpty())
    {
        error = tr("Enter a name of at most %1 characters without a slash or a backslash.").arg(PDFAppToolInputList::MAXIMUM_NAME_LENGTH);
        return false;
    }
    if (validName == input->name)
    {
        return true;
    }

    // A copy under the name takes the place of the image (one undo step of the ids); its
    // adjustment and its selection go with it
    const PDFAppToolInputList::AddResult copy = m_list->createRenamedCopy(inputId, validName);
    if (copy.id.isEmpty())
    {
        error = tr("Only an image, which has been read, can be renamed.");
        return false;
    }
    QStringList ids = m_list->getInputIds();
    ids.replace(ids.indexOf(inputId), copy.id);
    Adjustments adjustments = m_adjustments;
    auto it = m_adjustments.find(inputId);
    if (it != m_adjustments.cend())
    {
        adjustments[copy.id] = it->second;
    }
    if (m_selectedId == inputId)
    {
        m_selectedId = copy.id;
    }
    for (QString& id : m_extraSelectedIds)
    {
        if (id == inputId)
        {
            id = copy.id;
        }
    }
    applyChange(ids, adjustments);
    return true;
}

void PDFAppImagesToPdfToolGUI::openRenameDialog()
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (!input || !input->isUsable() || !isEditable())
    {
        return;
    }

    // One dialog at a time; it is deleted on its close (a QPointer here, it holds the GUI by one)
    if (m_renameDialog)
    {
        m_renameDialog->close();
    }
    PDFAppRenameImageDialog* dialog = new PDFAppRenameImageDialog(this, input->id, input->name, m_canvas ? m_canvas->window() : nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_renameDialog = dialog;
    dialog->open();
}

PDFAppNameDialog* PDFAppImagesToPdfToolGUI::getRenameDialog() const
{
    return m_renameDialog.data();
}

void PDFAppImagesToPdfToolGUI::onSelectedLinkActivated()
{
    openRenameDialog();
}

// ------------------------------------------------------------- the crop of an image

QSize PDFAppImagesToPdfToolGUI::getTurnedSize(const QSize& size, PageRotation turn)
{
    return turn == PageRotation::Rotate90 || turn == PageRotation::Rotate270 ? size.transposed() : size;
}

QMargins PDFAppImagesToPdfToolGUI::getTurnedMargins(const QMargins& margins, PageRotation turn)
{
    // A turn clockwise: the bottom of the image is at the left then (90°), at the top (180°)
    switch (turn)
    {
        case PageRotation::Rotate90:
            return QMargins(margins.bottom(), margins.left(), margins.top(), margins.right());
        case PageRotation::Rotate180:
            return QMargins(margins.right(), margins.bottom(), margins.left(), margins.top());
        case PageRotation::Rotate270:
            return QMargins(margins.top(), margins.right(), margins.bottom(), margins.left());
        default:
            return margins;
    }
}

QMargins PDFAppImagesToPdfToolGUI::getUnturnedMargins(const QMargins& turnedMargins, PageRotation turn)
{
    // The opposite turn
    switch (turn)
    {
        case PageRotation::Rotate90:
            return getTurnedMargins(turnedMargins, PageRotation::Rotate270);
        case PageRotation::Rotate270:
            return getTurnedMargins(turnedMargins, PageRotation::Rotate90);
        default:
            return getTurnedMargins(turnedMargins, turn);
    }
}

void PDFAppImagesToPdfToolGUI::setCropping(bool isCropping)
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (isCropping && (!input || !input->isUsable() || !isEditable()))
    {
        return;
    }

    m_isCropping = isCropping;
    if (isCropping)
    {
        // The box of the crop of the image (in the turned image; at least one pixel stays)
        m_extraSelectedIds.clear();
        const PDFDocumentToolImagesToPdf::Adjustment adjustment = getAdjustment(input->id);
        const QSize size = getTurnedSize(input->imageSize, adjustment.turn);
        const QMargins margins = getTurnedMargins(adjustment.crop, adjustment.turn);
        const int left = qBound(0, margins.left(), qMax(0, size.width() - 1));
        const int top = qBound(0, margins.top(), qMax(0, size.height() - 1));
        const int width = qMax(1, size.width() - left - qMax(0, margins.right()));
        const int height = qMax(1, size.height() - top - qMax(0, margins.bottom()));
        m_cropRect = QRect(left, top, width, height);
        m_isCropEqual = margins.left() == margins.top() && margins.left() == margins.right() && margins.left() == margins.bottom();
        m_isCropAll = false;
    }
    updateAll();
}

void PDFAppImagesToPdfToolGUI::setCropRect(const QRect& rect, bool isEqualMargins)
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (!m_isCropping || !input)
    {
        return;
    }

    const QRect bounds(QPoint(0, 0), getTurnedSize(input->imageSize, getAdjustment(input->id).turn));
    QRect bounded = rect.intersected(bounds);
    if (bounded.isEmpty())
    {
        bounded = QRect(qBound(0, rect.left(), bounds.width() - 1), qBound(0, rect.top(), bounds.height() - 1), 1, 1);
    }
    m_cropRect = bounded;
    m_isCropEqual = isEqualMargins;

    // The box, the panel and the status bar (no undo step)
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    updateCropCanvas();
    if (guard)
    {
        updateCropPanel();
    }
    if (guard)
    {
        updateStatusText();
    }
}

void PDFAppImagesToPdfToolGUI::setCropAppliedToAll(bool isAllImages)
{
    m_isCropAll = isAllImages;
    updateCropPanel();
}

void PDFAppImagesToPdfToolGUI::resetCrop()
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (m_isCropping && input)
    {
        setCropRect(QRect(QPoint(0, 0), getTurnedSize(input->imageSize, getAdjustment(input->id).turn)), true);
    }
}

void PDFAppImagesToPdfToolGUI::applyCrop()
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (!m_isCropping || !input || !isEditable())
    {
        return;
    }

    // The margins of the box in the turned image; each image gets them by its own turn
    const QSize size = getTurnedSize(input->imageSize, getAdjustment(input->id).turn);
    const QMargins turnedMargins(m_cropRect.left(), m_cropRect.top(), size.width() - m_cropRect.right() - 1, size.height() - m_cropRect.bottom() - 1);
    const QStringList targets = m_isCropAll ? getUsableInputIds() : QStringList({ input->id });
    Adjustments adjustments = m_adjustments;
    for (const QString& id : targets)
    {
        PDFDocumentToolImagesToPdf::Adjustment& adjustment = adjustments[id];
        adjustment.crop = getUnturnedMargins(turnedMargins, adjustment.turn);
        if (adjustment.isEmpty())
        {
            adjustments.erase(id);
        }
    }

    // One undo step; the mode ends (the model announces the values, readValues shows them)
    m_isCropping = false;
    const bool isChanged = adjustments != m_adjustments;
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    applyChange(m_list->getInputIds(), adjustments);
    if (guard && !isChanged)
    {
        updateAll();
    }
}

void PDFAppImagesToPdfToolGUI::onCropClicked()
{
    setCropping(!m_isCropping);
}

QRect PDFAppImagesToPdfToolGUIHelper::toPixels(const QRectF& rect)
{
    // The edges of the box at whole pixels, at least one pixel
    const int left = int(std::lround(rect.left()));
    const int top = int(std::lround(rect.top()));
    const int right = qMax(left + 1, int(std::lround(rect.right())));
    const int bottom = qMax(top + 1, int(std::lround(rect.bottom())));
    return QRect(left, top, right - left, bottom - top);
}

void PDFAppImagesToPdfToolGUI::onCropRectChanged(const QRectF& rect)
{
    // A drag of the box makes it a crop box (as Crop Pages)
    setCropRect(PDFAppImagesToPdfToolGUIHelper::toPixels(rect), false);
}

void PDFAppImagesToPdfToolGUI::onCropRectChosen(const QRectF& rect)
{
    setCropRect(PDFAppImagesToPdfToolGUIHelper::toPixels(rect), false);
}

void PDFAppImagesToPdfToolGUI::onCropPanelEdited()
{
    if (!m_cropPanel)
    {
        return;
    }
    const PDFAppImageCropPanel::Values values = m_cropPanel->getValues();
    m_isCropAll = values.isAllImages;
    setCropRect(values.rect, values.isEqualMargins);
}

void PDFAppImagesToPdfToolGUI::onCropReset()
{
    resetCrop();
}

void PDFAppImagesToPdfToolGUI::onCropApply()
{
    applyCrop();
}

void PDFAppImagesToPdfToolGUI::rotateLeft()
{
    turnSelected(false);
}

void PDFAppImagesToPdfToolGUI::rotateRight()
{
    turnSelected(true);
}

void PDFAppImagesToPdfToolGUI::duplicateSelected()
{
    if (!m_list || !isEditable() || m_selectedId.isEmpty())
    {
        return;
    }

    // One more page of each selected image right after it, with its adjustment; the copies are
    // the selection then
    const QStringList selected = getSelectedImages();
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    Adjustments adjustments = m_adjustments;
    QStringList copies;
    QString refusal;
    for (const QString& id : selected)
    {
        m_isChangingList = true;
        const PDFAppToolInputList::AddResult result = m_list->duplicateInput(id);
        if (!guard)
        {
            return;
        }
        m_isChangingList = false;
        if (!m_list)
        {
            return;
        }
        if (result.id.isEmpty())
        {
            refusal = PDFAppToolInputList::getRefusalText(result.refusal, result.name, m_list->getMaximumCount());
            break;
        }
        auto it = m_adjustments.find(id);
        if (it != m_adjustments.cend())
        {
            adjustments[result.id] = it->second;
        }
        copies << result.id;
    }

    if (!copies.isEmpty())
    {
        m_selectedId = copies.back();
        copies.removeLast();
        m_extraSelectedIds = copies;
        applyChange(m_list->getInputIds(), adjustments);
    }
    if (guard && !refusal.isEmpty() && getHost())
    {
        getHost()->showTransientMessage(refusal);
    }
}

void PDFAppImagesToPdfToolGUI::removeSelected()
{
    if (!m_list || !isEditable() || m_selectedId.isEmpty())
    {
        return;
    }

    // The image after the last removed one is selected; the removed ones are parked (Undo takes
    // them back)
    QStringList ids = m_list->getInputIds();
    const QStringList selected = getSelectedImages();
    if (selected.isEmpty())
    {
        return;
    }
    const int index = int(ids.indexOf(selected.back())) - int(selected.size()) + 1;
    for (const QString& id : selected)
    {
        ids.removeAll(id);
    }
    m_selectedId = ids.isEmpty() ? QString() : ids.at(qBound(0, index, int(ids.size()) - 1));
    m_extraSelectedIds.clear();
    applyChange(ids, m_adjustments);
}

void PDFAppImagesToPdfToolGUI::moveImage(const QString& inputId, int targetIndex)
{
    if (!m_list || !isEditable())
    {
        return;
    }

    QStringList ids = m_list->getInputIds();
    const int index = int(ids.indexOf(inputId));
    if (index < 0)
    {
        return;
    }

    // The index of the item, in front of which it goes, in the order before the move
    const int target = qBound(0, targetIndex, int(ids.size()));
    ids.removeAt(index);
    ids.insert(target > index ? target - 1 : target, inputId);
    if (ids != m_list->getInputIds())
    {
        applyChange(ids, m_adjustments);
    }
}

void PDFAppImagesToPdfToolGUI::sortBy(int key)
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    if (!m_list || !isEditable())
    {
        return;
    }

    std::vector<Helper::SortEntry> entries;
    for (int i = 0; i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        entries.push_back({ input->id, input->name, QFileInfo(input->filePath).absolutePath(), QString::fromLatin1(input->imageFormat).toLower() });
    }

    // 336:2639: natural order of the names (QCollator numeric), stable (a comparator object, no lambda)
    Helper::SortKeyLess less;
    less.key = key;
    less.collator.setNumericMode(true);
    less.collator.setCaseSensitivity(Qt::CaseInsensitive);
    if (key == Helper::SortReverse)
    {
        std::reverse(entries.begin(), entries.end());
    }
    else
    {
        std::stable_sort(entries.begin(), entries.end(), less);
    }

    QStringList ids;
    for (const Helper::SortEntry& entry : entries)
    {
        ids << entry.id;
    }
    if (ids != m_list->getInputIds())
    {
        applyChange(ids, m_adjustments);
    }
}

void PDFAppImagesToPdfToolGUI::sortByName()
{
    sortBy(PDFAppImagesToPdfToolGUIHelper::SortByName);
}

void PDFAppImagesToPdfToolGUI::sortByFolder()
{
    sortBy(PDFAppImagesToPdfToolGUIHelper::SortByFolder);
}

void PDFAppImagesToPdfToolGUI::sortByType()
{
    sortBy(PDFAppImagesToPdfToolGUIHelper::SortByType);
}

void PDFAppImagesToPdfToolGUI::reverseOrder()
{
    sortBy(PDFAppImagesToPdfToolGUIHelper::SortReverse);
}

void PDFAppImagesToPdfToolGUI::selectImage(const QString& inputId)
{
    clickImage(inputId, Qt::NoModifier);
}

PDFDocumentToolImagesToPdf::Adjustment PDFAppImagesToPdfToolGUI::getAdjustment(const QString& inputId) const
{
    auto it = m_adjustments.find(inputId);
    return it != m_adjustments.cend() ? it->second : PDFDocumentToolImagesToPdf::Adjustment();
}

bool PDFAppImagesToPdfToolGUI::dropFiles(const QMimeData* mimeData)
{
    if (!PDFAppFileDrop::hasLocalFiles(mimeData) || !isEditable())
    {
        return false;
    }

    // The images in the dropped order; the refused items are said
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(mimeData, PDFAppFileDrop::MAXIMUM_FILES, true);
    QStringList refused;
    for (const PDFAppFileDrop::RefusedItem& item : result.refused)
    {
        refused << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
    }

    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    std::vector<PDFAppToolInputList::AddResult> results;
    if (m_list && !result.files.isEmpty())
    {
        m_isChangingList = true;
        results = m_list->addFiles(result.files);
        if (!guard)
        {
            return true;
        }
        m_isChangingList = false;
    }
    addResults(results, refused);
    return true;
}

bool PDFAppImagesToPdfToolGUI::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_keyWidget)
    {
        switch (event->type())
        {
            case QEvent::DragEnter:
            case QEvent::DragMove:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }

            case QEvent::Drop:
            {
                QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
                if (isEditable() && PDFAppFileDrop::hasLocalFiles(dropEvent->mimeData()))
                {
                    // The files are added from the event handler, the last thing
                    dropEvent->acceptProposedAction();
                    dropFiles(dropEvent->mimeData());
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppImagesToPdfToolGUI::onImageRowClicked(const QString& inputId)
{
    // Ctrl or Shift of the click selects more images (336:1976)
    clickImage(inputId, m_imagesPanel ? m_imagesPanel->getLastClickModifiers() : Qt::NoModifier);
}

void PDFAppImagesToPdfToolGUI::onImageLinkActivated(const QString& inputId)
{
    // The link of an image, which cannot be read: Remove file (the image is skipped, 336:2360)
    if (!m_list || !isEditable())
    {
        return;
    }
    QStringList ids = m_list->getInputIds();
    ids.removeAll(inputId);
    applyChange(ids, m_adjustments);
}

void PDFAppImagesToPdfToolGUI::onAddFilesRowClicked()
{
    if (PDFAppToolPanelPopup* panel = getPanel(QStringLiteral("add")))
    {
        panel->close();
    }
    addFilesFromDialog();
}

void PDFAppImagesToPdfToolGUI::onPasteRowClicked()
{
    if (PDFAppToolPanelPopup* panel = getPanel(QStringLiteral("add")))
    {
        panel->close();
    }
    pasteImage();
}

void PDFAppImagesToPdfToolGUI::onSortRowClicked()
{
    const QObject* row = sender();
    if (!row)
    {
        return;
    }
    const int key = row->property(PDFAppImagesToPdfToolGUIHelper::ROW_KEY).toInt();
    if (PDFAppToolPanelPopup* panel = getPanel(QStringLiteral("sort")))
    {
        panel->close();
    }
    sortBy(key);
}

void PDFAppImagesToPdfToolGUI::onZoomChanged()
{
    m_isFit = m_canvas ? m_canvas->isFit() : true;
    updateStatusText();
}

// ------------------------------------------------------------- the layout of a page

QSizeF PDFAppImagesToPdfToolGUI::getImageSizeInPoints(const PDFAppToolInput& input)
{
    // The size of the shown image by its resolution (R15: an invalid one is 300 dpi)
    return PDFImagePageFactory::getPageSize(input.imageSize, input.imageDpiX, input.imageDpiY);
}

std::optional<PDFImagePageLayout> PDFAppImagesToPdfToolGUI::getImageLayout(const QString& inputId) const
{
    const PDFAppToolParameterModel* model = getModel();
    const PDFAppToolInput* input = m_list ? m_list->findInput(inputId) : nullptr;
    if (!model || !input)
    {
        return std::nullopt;
    }

    // The one rule of the core (createLayout) with the turn and the crop of the image
    std::optional<PDFImagePageLayout> layout = PDFDocumentToolImagesToPdf::createLayout(model->getValues(), nullptr, nullptr);
    if (layout)
    {
        const PDFDocumentToolImagesToPdf::Adjustment adjustment = getAdjustment(inputId);
        layout->turn = adjustment.turn;
        layout->clip = PDFDocumentToolImagesToPdf::getClip(adjustment.crop, input->imageSize);
    }
    return layout;
}

QSizeF PDFAppImagesToPdfToolGUI::getImagePageSize(const QString& inputId) const
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(inputId) : nullptr;
    const std::optional<PDFImagePageLayout> layout = getImageLayout(inputId);
    if (!input || !layout || !input->isUsable())
    {
        return QSizeF();
    }
    return layout->getPageSize(getImageSizeInPoints(*input));
}

QImage PDFAppImagesToPdfToolGUI::getThumbnail(const PDFAppToolInput& input)
{
    // The decoded picture (null while it is decoded or when it cannot be decoded: the empty slot)
    const QImage picture = m_thumbnailCache ? m_thumbnailCache->getThumbnail(input) : QImage();
    if (picture.isNull())
    {
        return QImage();
    }
    const QString key = PDFAppImageThumbnailCache::getKey(input);

    // The crop and the turn of the image (the same rule as the page: the crop before the turn)
    const PDFDocumentToolImagesToPdf::Adjustment adjustment = getAdjustment(input.id);
    if (adjustment.isEmpty())
    {
        return picture;
    }
    const QString adjustedKey = QStringLiteral("%1/%2/%3,%4,%5,%6").arg(key).arg(int(adjustment.turn)).arg(adjustment.crop.left()).arg(adjustment.crop.top()).arg(adjustment.crop.right()).arg(adjustment.crop.bottom());
    auto adjustedIt = m_adjustedThumbnails.find(adjustedKey);
    if (adjustedIt != m_adjustedThumbnails.cend())
    {
        return adjustedIt->second;
    }

    const QRectF clip = PDFDocumentToolImagesToPdf::getClip(adjustment.crop, input.imageSize);
    QImage adjusted = picture;
    if (!clip.isEmpty())
    {
        const QRect pixels = QRectF(clip.left() * picture.width(), clip.top() * picture.height(), clip.width() * picture.width(), clip.height() * picture.height()).toAlignedRect().intersected(picture.rect());
        adjusted = pixels.isEmpty() ? picture : picture.copy(pixels);
    }
    const int angle = PDFAppImagesToPdfToolGUIHelper::getAngle(adjustment.turn);
    if (angle != 0)
    {
        adjusted = adjusted.transformed(QTransform().rotate(angle));
    }
    m_adjustedThumbnails[adjustedKey] = adjusted;
    return adjusted;
}

void PDFAppImagesToPdfToolGUI::onThumbnailDecoded()
{
    updateImages();
}

QImage PDFAppImagesToPdfToolGUI::getRowThumbnail(const QString& inputId) const
{
    const PDFAppToolInputRow* row = m_imagesPanel ? m_imagesPanel->findRow(inputId) : nullptr;
    return row ? row->getContent().thumbnail : QImage();
}

QImage PDFAppImagesToPdfToolGUI::getTurnedPreviewImage(const PDFAppToolInput& input, PageRotation turn)
{
    const QImage picture = getPreviewImage(input);
    const int angle = PDFAppImagesToPdfToolGUIHelper::getAngle(turn);
    return angle != 0 && !picture.isNull() ? picture.transformed(QTransform().rotate(angle)) : picture;
}

QImage PDFAppImagesToPdfToolGUI::getPreviewImage(const PDFAppToolInput& input)
{
    // The decoded picture of the shown image, scaled to the stage; one image is kept. Its colors
    // go through the color management of the application as a viewer shows them (K43).
    const QString key = input.id + QLatin1Char('/') + QString::number(input.contentGeneration);
    if (key == m_previewKey)
    {
        return m_previewImage;
    }
    const PDFAppToolHost* host = getHost();
    const PDFCMSPointer cms = host ? host->createEnvironment().cms : PDFCMSPointer();

    QImage image;
    if (!input.imageData.isEmpty())
    {
        QByteArray bytes = input.imageData;
        QBuffer buffer(&bytes);
        buffer.open(QBuffer::ReadOnly);
        QImageReader reader(&buffer, input.imageFormat);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid() && qMax(size.width(), size.height()) > MAXIMUM_PREVIEW_SIDE)
        {
            reader.setScaledSize(size.scaled(MAXIMUM_PREVIEW_SIDE, MAXIMUM_PREVIEW_SIDE, Qt::KeepAspectRatio));
        }
        image = PDFAppImagePagePreview::getColorManagedPicture(reader.read(), cms.data());
    }

    m_previewKey = key;
    m_previewImage = image;
    return image;
}

// ------------------------------------------------------------- the parts

void PDFAppImagesToPdfToolGUI::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;
    connectList();
    m_actions.clear();
    m_panels.clear();

    // tb-Add Images 336:1976 with its panel 336:2639: Image files… (Ctrl+Alt+I), Paste from clipboard (Ctrl+V)
    PDFAppToolPanelPopup* addPanel = builder.addMenu(QStringLiteral("addImages"), QStringLiteral("plus"), tr("Add Images"), Helper::ADD_PANEL_WIDTH);
    m_panels[QStringLiteral("add")] = addPanel;
    m_actions[QStringLiteral("add")] = addPanel->getButton();
    PDFTileRow* filesRow = addPanel->addRow(tr("Image files…"), tr("Pick one or more files"), QStringLiteral("folder-open"), PDFTileRow::Kind::MenuItem);
    filesRow->setObjectName(QStringLiteral("imagesAddFilesRow"));
    filesRow->setShortcutText(Helper::getAddFilesKeys().toString(QKeySequence::NativeText));
    connect(filesRow, &PDFTileRow::clicked, this, &PDFAppImagesToPdfToolGUI::onAddFilesRowClicked);
    PDFTileRow* pasteRow = addPanel->addRow(tr("Paste from clipboard"), tr("Adds the copied picture as “Pasted image”"), QStringLiteral("clipboard"), PDFTileRow::Kind::MenuItem);
    pasteRow->setObjectName(QStringLiteral("imagesPasteRow"));
    pasteRow->setShortcutText(Helper::getPasteKeys().toString(QKeySequence::NativeText));
    connect(pasteRow, &PDFTileRow::clicked, this, &PDFAppImagesToPdfToolGUI::onPasteRowClicked);

    builder.addActionDivider();

    // tb-rotate-left, tb-rotate-right, tb-crop, tb-duplicate, tb-remove 336:1976
    struct IconAction
    {
        const char* name;
        QString iconName;
        QString text;
        void (PDFAppImagesToPdfToolGUI::*slot)();
    };
    const IconAction iconActions[] = {
        { "rotateLeft", QStringLiteral("rotate-ccw"), tr("Rotate Left"), &PDFAppImagesToPdfToolGUI::rotateLeft },
        { "rotateRight", QStringLiteral("rotate-cw"), tr("Rotate Right"), &PDFAppImagesToPdfToolGUI::rotateRight },
        { "crop", QStringLiteral("crop"), tr("Crop"), &PDFAppImagesToPdfToolGUI::onCropClicked },
        { "duplicate", QStringLiteral("duplicate-page"), tr("Duplicate"), &PDFAppImagesToPdfToolGUI::duplicateSelected },
        { "remove", QStringLiteral("trash"), tr("Remove"), &PDFAppImagesToPdfToolGUI::removeSelected },
    };
    for (const IconAction& action : iconActions)
    {
        QToolButton* button = PDFAppDocumentToolbar::createToolbarButton(QStringLiteral("imagesAction-") + QString::fromLatin1(action.name), action.iconName, builder.getWidget());
        button->setToolTip(action.text);
        button->setAccessibleName(action.text);
        if (action.slot)
        {
            connect(button, &QToolButton::clicked, this, action.slot);
        }
        builder.addWidget(button);
        m_actions[QString::fromLatin1(action.name)] = button;
    }

    builder.addActionDivider();

    // tb-Size, tb-Sort, tb-Optimize 336:1976 with their panels 336:2639
    builder.addEnumMenu(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_PAGE_SIZE), QStringLiteral("file"), tr("Size: %1"), Helper::SIZE_PANEL_WIDTH);
    PDFAppToolPanelPopup* sortPanel = builder.addMenu(QStringLiteral("sort"), QStringLiteral("arrow-up-down"), tr("Sort"), Helper::SORT_PANEL_WIDTH);
    m_panels[QStringLiteral("sort")] = sortPanel;
    m_actions[QStringLiteral("sort")] = sortPanel->getButton();
    struct SortRow
    {
        int key;
        QString title;
        QString description;
        QString iconName;
    };
    const SortRow sortRows[] = {
        { Helper::SortByName, tr("File name"), tr("A to Z, numbers in natural order"), QStringLiteral("sort-az") },
        { Helper::SortByFolder, tr("Folder, then file name"), tr("Keeps files of one folder together"), QStringLiteral("folder-open") },
        { Helper::SortByType, tr("File type"), tr("JPEG, PNG, TIFF…"), QStringLiteral("images") },
        { Helper::SortReverse, tr("Reverse order"), tr("Turns the current order around"), QStringLiteral("reverse") },
    };
    for (const SortRow& sortRow : sortRows)
    {
        if (sortRow.key == Helper::SortReverse)
        {
            sortPanel->addGroupDivider();
        }
        PDFTileRow* row = sortPanel->addRow(sortRow.title, sortRow.description, sortRow.iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("imagesSort%1").arg(sortRow.key));
        row->setProperty(Helper::ROW_KEY, sortRow.key);
        connect(row, &PDFTileRow::clicked, this, &PDFAppImagesToPdfToolGUI::onSortRowClicked);
    }
    builder.addEnumMenu(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_OPTIMIZE_IMAGES), QStringLiteral("minimize-2"), tr("Optimize: %1"), Helper::OPTIMIZE_PANEL_WIDTH);

    // The keys of the frames (Ctrl+Alt+I, Ctrl+V) and the drop of files on the key widget of the host
    if (getHost() && getHost()->getKeyWidget() && !m_addFilesAction)
    {
        m_keyWidget = getHost()->getKeyWidget();
        m_addFilesAction = new QAction(m_keyWidget);
        m_addFilesAction->setObjectName(QStringLiteral("imagesAddFilesAction"));
        m_addFilesAction->setShortcut(Helper::getAddFilesKeys());
        m_addFilesAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_addFilesAction.data(), &QAction::triggered, this, &PDFAppImagesToPdfToolGUI::addFilesFromDialog);
        m_keyWidget->addAction(m_addFilesAction);
        m_pasteAction = new QAction(m_keyWidget);
        m_pasteAction->setObjectName(QStringLiteral("imagesPasteAction"));
        m_pasteAction->setShortcut(Helper::getPasteKeys());
        m_pasteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(m_pasteAction.data(), &QAction::triggered, this, &PDFAppImagesToPdfToolGUI::pasteImage);
        m_keyWidget->addAction(m_pasteAction);
        m_wasDropAccepted = m_keyWidget->acceptDrops();
        m_keyWidget->setAcceptDrops(true);
        m_keyWidget->installEventFilter(this);
    }
    updateToolbar();
}

void PDFAppImagesToPdfToolGUI::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    connectList();

    // sidebar-left 336:1976: "Images" / "6 images · 14.2 MB", the rows, the hint
    m_imagesPanel = new PDFAppToolInputFilesPanel(builder.getWidget());
    m_imagesPanel->setObjectName(QStringLiteral("imagesPanel"));
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::rowClicked, this, &PDFAppImagesToPdfToolGUI::onImageRowClicked);
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::linkActivated, this, &PDFAppImagesToPdfToolGUI::onImageLinkActivated);
    connect(m_imagesPanel.data(), &PDFAppToolInputFilesPanel::moveRequested, this, &PDFAppImagesToPdfToolGUI::moveImage);
    builder.addWidget(m_imagesPanel);
    builder.setFillsPanel(m_imagesPanel);
    updateImages();
}

void PDFAppImagesToPdfToolGUI::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;
    connectList();

    // The panel of the editing (336:1976) and the panel of the crop (336:3159): one is shown
    m_layoutSection = new QWidget(builder.getWidget());
    m_layoutSection->setObjectName(QStringLiteral("imagesLayoutSection"));
    PDFAppToolPanelBuilder layoutBuilder(this, m_layoutSection, Qt::Vertical, getRightPanelSpacing());

    // section-Page layout 336:1976 / 356:9556: the card of the fields (label 111, field 140) -
    // Width and Height of Custom, Orientation, Margins, Image, Anchor (K14: the combo box with
    // the grid in its popover) - and the note in the card
    layoutBuilder.addSection(tr("Page layout"));
    m_layoutFields = layoutBuilder.addMarginFields({ Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_WIDTH), Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_HEIGHT),
                                                     Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_ORIENTATION), Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_MARGINS),
                                                     Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_IMAGE_SCALING) },
                                                   tr("The page size is chosen in the toolbar. Auto orientation turns each page to match its image."));
    if (m_layoutFields)
    {
        m_layoutFields->setObjectName(QStringLiteral("imagesLayoutFields"));
        m_anchorComboBox = new PDFAppToolAnchorComboBox(this, Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_ANCHOR), m_layoutFields);
        m_layoutFields->addWidgetRow(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_ANCHOR), tr("Anchor"), m_anchorComboBox);
    }
    layoutBuilder.endSection();

    // section-Selected image 336:1976: the link Rename
    m_selectedCard = layoutBuilder.addSelectedItemCard(tr("Selected image"));
    m_selectedCard->getValues()->addRow(tr("Name"), QString(), PDFComponentPainter::TextKind::Data);
    m_selectedCard->getValues()->addRow(tr("Folder"), QString(), PDFComponentPainter::TextKind::Data);
    m_selectedCard->getValues()->addRow(tr("Pixels"), QString());
    m_selectedCard->getValues()->addRow(tr("Rotation"), QString());
    m_selectedCard->getValues()->addRow(tr("Crop"), QString());
    m_selectedCard->getValues()->addRow(tr("Page"), QString());
    connect(m_selectedCard.data(), &PDFAppToolSelectedItemCard::linkActivated, this, &PDFAppImagesToPdfToolGUI::onSelectedLinkActivated);

    // section-Summary 336:1976: Pages, Page size, Image optimization, Estimated size
    layoutBuilder.addSection(tr("Summary"));
    m_summary = layoutBuilder.addKeyValues();
    m_summary->setObjectName(QStringLiteral("imagesSummary"));
    m_summary->addRow(tr("Pages"), QString());
    m_summary->addRow(tr("Page size"), QString());
    m_summary->addRow(tr("Image optimization"), QString());
    m_summary->addRow(tr("Estimated size"), QString());
    layoutBuilder.endSection();
    builder.addWidget(m_layoutSection);

    // sidebar-right 336:3159: the crop of the image of the preview
    m_cropSection = new QWidget(builder.getWidget());
    m_cropSection->setObjectName(QStringLiteral("imagesCropSection"));
    QVBoxLayout* cropLayout = new QVBoxLayout(m_cropSection);
    cropLayout->setContentsMargins(0, 0, 0, 0);
    m_cropPanel = new PDFAppImageCropPanel(m_cropSection);
    cropLayout->addWidget(m_cropPanel);
    connect(m_cropPanel.data(), &PDFAppImageCropPanel::valuesEdited, this, &PDFAppImagesToPdfToolGUI::onCropPanelEdited);
    connect(m_cropPanel.data(), &PDFAppImageCropPanel::resetClicked, this, &PDFAppImagesToPdfToolGUI::onCropReset);
    connect(m_cropPanel.data(), &PDFAppImageCropPanel::applyClicked, this, &PDFAppImagesToPdfToolGUI::onCropApply);
    builder.addWidget(m_cropSection);

    updateRightPanel();
}

QWidget* PDFAppImagesToPdfToolGUI::createCanvasPage(QWidget* parent)
{
    connectList();
    m_canvas = new PDFAppImagesToPdfCanvas(parent);
    m_canvas->setFit(m_isFit);
    connect(m_canvas.data(), &PDFAppImagesToPdfCanvas::zoomChanged, this, &PDFAppImagesToPdfToolGUI::onZoomChanged);
    connect(m_canvas->getAddImagesButton(), &QPushButton::clicked, this, &PDFAppImagesToPdfToolGUI::addFilesFromDialog);
    updateCanvas();
    return m_canvas.data();
}

QToolButton* PDFAppImagesToPdfToolGUI::getAction(const QString& name) const
{
    auto it = m_actions.find(name);
    return it != m_actions.cend() ? it->second.data() : nullptr;
}

PDFAppToolPanelPopup* PDFAppImagesToPdfToolGUI::getPanel(const QString& name) const
{
    auto it = m_panels.find(name);
    return it != m_panels.cend() ? it->second.data() : nullptr;
}

// ------------------------------------------------------------- updates

void PDFAppImagesToPdfToolGUI::updateAll()
{
    // The parts first, then the button and the status bar (signals, the last call); a change of
    // the visibility, the rows of the panel or the page of the preview may run an observer,
    // which deletes this GUI: a guard after each part
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    updateImages();
    if (!guard)
    {
        return;
    }
    updateCanvas();
    if (!guard)
    {
        return;
    }
    updateRightPanel();
    if (!guard)
    {
        return;
    }
    updateToolbar();
    if (!guard)
    {
        return;
    }
    updateConfirmState();
    if (guard)
    {
        updateStatusText();
    }
    if (guard)
    {
        updateOwnTabState();
    }
}

PDFAppToolInputRow::Content PDFAppImagesToPdfToolGUI::createRowContent(const PDFAppToolInput& input, int index, int firstPage)
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    PDFAppToolInputRow::Content content;
    content.inputId = input.id;
    content.letter = PDFAppToolInputList::getLetter(index);
    content.hue = PDFAppToolInputList::getHue(index, &input);
    content.name = input.name;
    if (input.isUsable() && input.isImage())
    {
        // thumb 336:2128: the picture of the image with its crop and turn (decoded in the pool)
        content.hasThumbnail = true;
        content.thumbnail = getThumbnail(input);
    }
    if (!input.isUsable())
    {
        // 336:2360: a file, which cannot be read, is skipped and listed by name
        content.firstLine = input.reason.isEmpty() ? PDFAppToolInputList::getStateText(input.state) : input.reason;
        content.isFirstLineProblem = input.state != PDFAppToolInputState::Loading;
        content.toolTip = content.firstLine;
        content.linkText = input.state != PDFAppToolInputState::Loading && isEditable() ? tr("Remove file") : QString();
        return content;
    }

    // 336:1976: "4032 × 3024 px · JPEG · 3.1 MB", "Page 1 · A4 landscape", Tags "rotated 90°", "cropped"
    QStringList first = { tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(input.imageSize.width()), PDFAppLocale::formatNumber(input.imageSize.height())) };
    const QString format = Helper::getFormatText(input.imageFormat);
    if (!format.isEmpty())
    {
        first << format;
    }
    if (input.fileSize > 0)
    {
        first << PDFDocumentToolFormat::formatSize(input.fileSize);
    }
    content.firstLine = first.join(QStringLiteral(" · "));

    const QString page = PDFAppToolGUIImages::getPageDescription(getImagePageSize(input.id));
    content.secondLine = input.pageCount > 1 ? tr("Pages %1–%2 · %3").arg(PDFAppLocale::formatNumber(firstPage), PDFAppLocale::formatNumber(firstPage + input.pageCount - 1), page)
                                             : tr("Page %1 · %2").arg(PDFAppLocale::formatNumber(firstPage), page);

    const PDFDocumentToolImagesToPdf::Adjustment adjustment = getAdjustment(input.id);
    if (adjustment.turn != PageRotation::None)
    {
        content.tag = tr("rotated %1°").arg(Helper::getAngle(adjustment.turn));
    }
    else if (!adjustment.crop.isNull())
    {
        content.tag = tr("cropped");
    }
    return content;
}

void PDFAppImagesToPdfToolGUI::updateImages()
{
    if (!m_imagesPanel)
    {
        return;
    }

    std::vector<PDFAppToolInputRow::Content> rows;
    int page = 1;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        rows.push_back(createRowContent(*input, i, page));
        page += input->isUsable() ? int(qMin<PDFInteger>(input->pageCount, PDFDocumentToolImagesToPdf::MAXIMUM_INPUT_COUNT * 10000)) : 0;
    }

    const int count = m_list ? m_list->getCount() : 0;
    const qint64 size = m_list ? m_list->getTotalSize() : 0;
    m_imagesPanel->setHeader(tr("Images"), count > 0 ? tr("%Ln images", nullptr, count) + QStringLiteral(" · ") + PDFDocumentToolFormat::formatSize(size) : tr("No images yet"));
    m_imagesPanel->setHint(count > 0 ? tr("Drag to change the page order. Ctrl or Shift selects more images. Drop files anywhere in the window or paste with Ctrl+V.")
                                     : tr("Images appear here in the order of the pages. One image makes one page."));
    m_imagesPanel->setCurrentInput(m_selectedId);
    m_imagesPanel->setSelectedInputs(m_extraSelectedIds);
    m_imagesPanel->setRows(rows);
    m_imagesPanel->setEditable(isEditable());

    // The thumbnails of the images, which are not in the list any more, are forgotten
    std::set<QString> keys;
    for (int i = 0; m_list && i < m_list->getCount(); ++i)
    {
        keys.insert(PDFAppImageThumbnailCache::getKey(*m_list->getInput(i)));
    }
    if (m_thumbnailCache)
    {
        m_thumbnailCache->retain(keys);
    }
    for (auto it = m_adjustedThumbnails.begin(); it != m_adjustedThumbnails.end();)
    {
        it = keys.count(it->first.section(QLatin1Char('/'), 0, 1)) ? std::next(it) : m_adjustedThumbnails.erase(it);
    }
}

void PDFAppImagesToPdfToolGUI::updateCanvas()
{
    if (!m_canvas)
    {
        return;
    }

    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    const bool hasImages = m_list && m_list->getCount() > 0;
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    m_canvas->setEmpty(!hasImages);
    if (!guard || !m_canvas)
    {
        return;
    }
    m_canvas->setCropMode(m_isCropping);
    if (!guard || !m_canvas)
    {
        return;
    }
    if (m_isCropping)
    {
        updateCropCanvas();
        return;
    }
    input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (!input)
    {
        m_canvas->setTitle(QString());
        m_canvas->setCaption(QString());
        m_canvas->setPage(QImage(), QSizeF(), PDFImagePageLayout());
        return;
    }

    // "Page 1 of 6 · IMG_2041.jpg": the first page of the image among the pages of the result
    int page = 1;
    int pageCount = 0;
    for (int i = 0; i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* other = m_list->getInput(i);
        if (other->id == input->id)
        {
            page = pageCount + 1;
        }
        pageCount += other->isUsable() ? int(other->pageCount) : 0;
    }
    m_canvas->setTitle(tr("Page %1 of %2 · %3").arg(PDFAppLocale::formatNumber(page), PDFAppLocale::formatNumber(pageCount), input->name));

    const std::optional<PDFImagePageLayout> layout = getImageLayout(input->id);
    if (!layout || !input->isUsable())
    {
        m_canvas->setPage(QImage(), QSizeF(), PDFImagePageLayout());
        m_canvas->setCaption(input->isUsable() ? QString() : input->reason);
        return;
    }

    const QSizeF imageSize = getImageSizeInPoints(*input);
    m_canvas->setPage(getPreviewImage(*input), imageSize, *layout);

    // 336:2234 "A4 landscape · 297 × 210 mm · image fits inside 10 mm margins, centered"
    const QSizeF pageSize = layout->getPageSize(imageSize);
    const QString dimensions = tr("%1 × %2 mm").arg(PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(pageSize.width())),
                                                    PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(pageSize.height())));
    const QString margins = PDFAppBlankToolGUI::formatMillimeters(PDFPageSizePresets::getMillimeters(layout->margins.left()));
    QString scaling;
    switch (layout->scaling)
    {
        case PDFImagePageLayout::Scaling::Fit:
            scaling = tr("image fits inside %1 mm margins").arg(margins);
            break;
        case PDFImagePageLayout::Scaling::Fill:
            scaling = tr("image fills the page inside %1 mm margins").arg(margins);
            break;
        case PDFImagePageLayout::Scaling::Stretch:
            scaling = tr("image stretched inside %1 mm margins").arg(margins);
            break;
        case PDFImagePageLayout::Scaling::OriginalSize:
            scaling = tr("image in its original size inside %1 mm margins").arg(margins);
            break;
    }
    const QString anchor = PDFDocumentToolImagesToPdf::getAnchorId(layout->anchor) == QLatin1String(PDFDocumentToolImagesToPdf::ANCHOR_MIDDLE_CENTER) ? tr("centered") : tr("anchored");
    m_canvas->setCaption(tr("%1 · %2 · %3, %4").arg(PDFAppToolGUIImages::getPageDescription(pageSize), dimensions, scaling, anchor));
}

void PDFAppImagesToPdfToolGUI::updateCropCanvas()
{
    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    PDFAppToolPageStage* stage = m_canvas ? m_canvas->getCropStage() : nullptr;
    if (!m_isCropping || !input || !stage)
    {
        return;
    }

    // 336:3159 "Crop · IMG_2041.jpg": the image as shown and turned, the box over it in its pixels
    const PageRotation turn = getAdjustment(input->id).turn;
    const QSize size = getTurnedSize(input->imageSize, turn);
    m_canvas->setTitle(tr("Crop · %1").arg(input->name));
    m_canvas->setCaption(tr("Drag the handles or type the values on the right · darkened parts are removed"));
    stage->setImage(getTurnedPreviewImage(*input, turn), size);
    if (!m_cropOverlay)
    {
        // Ownership: a child of the stage of the canvas, held by a QPointer
        m_cropOverlay = new PDFAppCropOverlay(stage);
        m_cropOverlay->setObjectName(QStringLiteral("imagesCropOverlay"));
        m_cropOverlay->setMinimumSize(1.0);
        connect(m_cropOverlay.data(), &PDFAppCropOverlay::rectChanged, this, &PDFAppImagesToPdfToolGUI::onCropRectChanged);
        connect(m_cropOverlay.data(), &PDFAppCropOverlay::rectChosen, this, &PDFAppImagesToPdfToolGUI::onCropRectChosen);
    }
    m_cropOverlay->setBounds(QRectF(QPointF(0.0, 0.0), QSizeF(size)));
    m_cropOverlay->setRect(QRectF(m_cropRect));
}

void PDFAppImagesToPdfToolGUI::updateRightPanel()
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    // 356:9556: Width and Height only with the size Custom
    // The visibility changes go with guards (an observer of the layout may delete this GUI)
    const bool isCustom = getParameter(PDFDocumentToolImagesToPdf::PARAMETER_PAGE_SIZE) == QLatin1String(PDFDocumentToolImagesToPdf::SIZE_CUSTOM);
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    if (m_layoutSection && m_cropSection)
    {
        // 336:3159: the panel of the crop instead of the panel of the editing
        m_layoutSection->setVisible(!m_isCropping);
        m_cropSection->setVisible(m_isCropping);
        if (!guard)
        {
            return;
        }
    }
    if (m_layoutFields)
    {
        m_layoutFields->setRowVisible(PDFAppImagesToPdfToolGUIHelper::name(PDFDocumentToolImagesToPdf::PARAMETER_WIDTH), isCustom);
        m_layoutFields->setRowVisible(PDFAppImagesToPdfToolGUIHelper::name(PDFDocumentToolImagesToPdf::PARAMETER_HEIGHT), isCustom);
    }
    if (!guard)
    {
        return;
    }
    updateCropPanel();

    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    if (m_selectedCard)
    {
        m_selectedCard->setVisible(input != nullptr);
        if (!guard)
        {
            return;
        }
        if (input)
        {
            const int index = m_list->indexOf(input->id);
            m_selectedCard->setTagText(tr("%1 of %2").arg(PDFAppLocale::formatNumber(index + 1), PDFAppLocale::formatNumber(m_list->getCount())));
            m_selectedCard->setLinkText(isEditable() && input->isUsable() ? tr("Rename") : QString());
            const PDFDocumentToolImagesToPdf::Adjustment adjustment = getAdjustment(input->id);
            PDFKeyValueList* values = m_selectedCard->getValues();
            values->setValue(0, input->name);
            values->setValue(1, input->filePath.isEmpty() ? tr("Clipboard") : QDir::toNativeSeparators(QFileInfo(input->filePath).absolutePath()));
            values->setValue(2, tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(input->imageSize.width()), PDFAppLocale::formatNumber(input->imageSize.height())));
            values->setValue(3, adjustment.turn == PageRotation::None ? tr("None") : tr("%1° clockwise").arg(Helper::getAngle(adjustment.turn)));
            const QMargins& crop = adjustment.crop;
            values->setValue(4, crop.isNull() ? tr("None") : tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(qMax(1, input->imageSize.width() - crop.left() - crop.right())),
                                                                                    PDFAppLocale::formatNumber(qMax(1, input->imageSize.height() - crop.top() - crop.bottom()))));
            values->setValue(5, PDFAppToolGUIImages::getPageDescription(getImagePageSize(input->id)));
        }
    }

    if (m_summary)
    {
        int pages = 0;
        for (int i = 0; m_list && i < m_list->getCount(); ++i)
        {
            const PDFAppToolInput* other = m_list->getInput(i);
            pages += other->isUsable() ? int(other->pageCount) : 0;
        }
        const QString sizeId = getParameter(PDFDocumentToolImagesToPdf::PARAMETER_PAGE_SIZE);
        const QString orientation = getParameter(PDFDocumentToolImagesToPdf::PARAMETER_ORIENTATION);
        QString sizeText = sizeId == QLatin1String(PDFDocumentToolImagesToPdf::SIZE_FIT_TO_IMAGE) ? tr("Fit to image")
                         : sizeId == QLatin1String(PDFDocumentToolImagesToPdf::SIZE_CUSTOM) ? tr("Custom")
                         : PDFAppToolGUIImages::getPresetName(PDFPageSizePresets::getSize(sizeId).value_or(QSizeF()));
        if (sizeId != QLatin1String(PDFDocumentToolImagesToPdf::SIZE_FIT_TO_IMAGE))
        {
            // 336:1976 "A4, auto"
            const QString orientationText = orientation == QLatin1String(PDFDocumentToolImagesToPdf::ORIENTATION_PORTRAIT) ? tr("portrait")
                                          : orientation == QLatin1String(PDFDocumentToolImagesToPdf::ORIENTATION_LANDSCAPE) ? tr("landscape") : tr("auto");
            sizeText = tr("%1, %2").arg(sizeText, orientationText);
        }
        const QString optimize = getParameter(PDFDocumentToolImagesToPdf::PARAMETER_OPTIMIZE_IMAGES);
        const QString optimizeText = optimize == QLatin1String(PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY) ? tr("Prefer quality")
                                   : optimize == QLatin1String(PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE) ? tr("Minimum size") : tr("Off");
        m_summary->setValue(0, PDFAppLocale::formatNumber(pages));
        m_summary->setValue(1, sizeText);
        m_summary->setValue(2, optimizeText);

        // The images are written with their bytes (Off): the size of the files is the estimate
        const qint64 size = m_list ? m_list->getTotalSize() : 0;
        m_summary->setValue(3, size > 0 ? PDFDocumentToolFormat::formatSize(size) : tr("–"));
    }
}

void PDFAppImagesToPdfToolGUI::updateToolbar()
{
    const PDFAppToolInput* selected = m_list ? m_list->findInput(m_selectedId) : nullptr;
    const bool hasSelection = selected != nullptr;
    const bool isEditableNow = isEditable();
    for (const char* name : { "rotateLeft", "rotateRight", "duplicate", "remove" })
    {
        if (QToolButton* button = getAction(QString::fromLatin1(name)))
        {
            // 336:3159: the crop mode leaves only the button Crop active
            button->setEnabled(isEditableNow && hasSelection && !m_isCropping);
        }
    }

    // tb-crop 336:3159: Active in the crop mode; an image, which cannot be read, has no crop
    if (QToolButton* crop = getAction(QStringLiteral("crop")))
    {
        const QSignalBlocker blocker(crop);
        crop->setCheckable(true);
        crop->setChecked(m_isCropping);
        crop->setEnabled(isEditableNow && (m_isCropping || (selected && selected->isUsable())));
    }
}

void PDFAppImagesToPdfToolGUI::updateCropPanel()
{
    if (!m_cropPanel || !m_isCropping)
    {
        return;
    }

    const PDFAppToolInput* input = m_list ? m_list->findInput(m_selectedId) : nullptr;
    PDFAppImageCropPanel::Values values;
    if (input)
    {
        values.name = input->name;
        values.imageSize = getTurnedSize(input->imageSize, getAdjustment(input->id).turn);
    }
    values.rect = m_cropRect;
    values.isEqualMargins = m_isCropEqual;
    values.isAllImages = m_isCropAll;
    values.imageCount = int(getUsableInputIds().size());
    m_cropPanel->setValues(values);
}

// ------------------------------------------------------------- texts

QString PDFAppImagesToPdfToolGUI::getConfirmText() const
{
    // 336:1976 "Create PDF"
    return tr("Create PDF");
}

QString PDFAppImagesToPdfToolGUI::getConfirmIconName() const
{
    return QStringLiteral("file-plus");
}

bool PDFAppImagesToPdfToolGUI::isConfirmEnabled() const
{
    // Every image can be read (a file, which cannot, is removed by the user), at least one; the
    // crop is applied or left first (336:3159: Create PDF Disabled)
    return BaseClass::isConfirmEnabled() && m_list && m_list->isRunnable(1) && !m_isCropping;
}

QString PDFAppImagesToPdfToolGUI::getStatusText() const
{
    const QString title = getTool() ? getTool()->getTitle() : tr("Images to PDF");
    const int count = m_list ? m_list->getCount() : 0;
    if (count == 0)
    {
        // 336:2360
        return tr("%1 · add at least one image to create a PDF").arg(title);
    }
    if (const PDFAppToolInput* cropped = m_isCropping ? m_list->findInput(m_selectedId) : nullptr)
    {
        // 336:3159 "Images to PDF · cropping IMG_2041.jpg · 3792 × 2784 px will be kept"
        return tr("%1 · cropping %2 · %3 × %4 px will be kept").arg(title, cropped->name, PDFAppLocale::formatNumber(m_cropRect.width()), PDFAppLocale::formatNumber(m_cropRect.height()));
    }

    // 336:1976 "Images to PDF · 6 images · 14.2 MB · the PDF will have 6 pages, about 14.3 MB"
    int pages = 0;
    for (int i = 0; i < count; ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        pages += input->isUsable() ? int(input->pageCount) : 0;
    }
    const QString size = PDFDocumentToolFormat::formatSize(m_list->getTotalSize());
    // Both numbers are counts, each one has its own %Ln
    const QString result = tr("the PDF will have %Ln pages, about %1", nullptr, pages).arg(size);
    return tr("%1 · %Ln images · %2 · %3", nullptr, count).arg(title, size, result);
}

QString PDFAppImagesToPdfToolGUI::getRunningStatusText(const QString& stepText) const
{
    // 356:9165 "Images to PDF · creating page 4 of 6 · Stop ends the run"
    const QString title = getTool() ? getTool()->getTitle() : tr("Images to PDF");
    return stepText.isEmpty() ? tr("%1 · Stop ends the run").arg(title) : tr("%1 · %2 · Stop ends the run").arg(title, stepText);
}

QString PDFAppImagesToPdfToolGUI::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(percentage);
    return stepText.isEmpty() ? tr("Creating the PDF") : stepText;
}

void PDFAppImagesToPdfToolGUI::fillResultInput(PDFAppToolResultInput* input) const
{
    // 336:3572 "Images to PDF finished"
    input->title = tr("Images to PDF finished");
}

QString PDFAppImagesToPdfToolGUI::getOwnTabTitle() const
{
    // 336:1976: the tab "New document"
    return tr("New document");
}

QString PDFAppImagesToPdfToolGUI::getOwnTabIconName() const
{
    return QStringLiteral("file");
}

QString PDFAppImagesToPdfToolGUI::getOwnTabLossText() const
{
    const int count = m_list ? m_list->getCount() : (getInputList() ? getInputList()->getCount() : 0);
    return count > 0 ? tr("The list of %Ln images is lost.", nullptr, count) : QString();
}

QString PDFAppImagesToPdfToolGUI::getOwnTabPageText() const
{
    // 336:1976 "Page 1 / 6"
    if (!m_list || m_list->getCount() == 0)
    {
        return QString();
    }
    int page = 1;
    int pages = 0;
    for (int i = 0; i < m_list->getCount(); ++i)
    {
        const PDFAppToolInput* input = m_list->getInput(i);
        if (input->id == m_selectedId)
        {
            page = pages + 1;
        }
        pages += input->isUsable() ? int(input->pageCount) : 0;
    }
    return tr("Page %1 / %2").arg(PDFAppLocale::formatNumber(page), PDFAppLocale::formatNumber(pages));
}

QString PDFAppImagesToPdfToolGUI::getOwnTabZoomText() const
{
    // 336:1976 "151 %": the scale of the preview to the actual size
    const PDFAppImagePagePreview* preview = m_canvas ? m_canvas->getPreview() : nullptr;
    const double scale = preview ? preview->getScale() : 0.0;
    if (scale <= 0.0)
    {
        return QString();
    }
    return tr("%1 %").arg(PDFAppLocale::formatNumber(qRound64(scale / (PDFAppImagePagePreview::ACTUAL_DPI / 72.0) * 100.0)));
}

QString PDFAppImagesToPdfToolGUI::getStartPageText(PDFAppToolStartPageText text) const
{
    // K37: the start page of 1392:4528 (Open Images to PDF)
    switch (text)
    {
        case PDFAppToolStartPageText::Title:
            return tr("Start");
        case PDFAppToolStartPageText::Hint:
            return tr("Images to PDF needs no document · it creates a new one from images");
        case PDFAppToolStartPageText::Card:
            return tr("The tool opens in its own tab. Add the images there, check the pages and press Create PDF.");
        case PDFAppToolStartPageText::Note:
            return tr("The image files are never changed. One image makes one page; a multi-page TIFF makes a page of each of its pages.");
    }
    return QString();
}

// ------------------------------------------------------------- flow

QVariantMap PDFAppImagesToPdfToolGUI::getRunParameters() const
{
    using Helper = PDFAppImagesToPdfToolGUIHelper;

    // The run gets the usable images only (R16 of step 2): their ids and their adjustments by
    // their letters among them
    QVariantMap parameters = BaseClass::getRunParameters();
    const QStringList ids = getUsableInputIds();
    std::map<int, PDFDocumentToolImagesToPdf::Adjustment> byIndex;
    for (int i = 0; i < ids.size(); ++i)
    {
        const PDFDocumentToolImagesToPdf::Adjustment adjustment = getAdjustment(ids.at(i));
        if (!adjustment.isEmpty())
        {
            byIndex[i] = adjustment;
        }
    }
    parameters.insert(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_INPUTS), ids.join(QLatin1Char(',')));
    parameters.insert(Helper::name(PDFDocumentToolImagesToPdf::PARAMETER_ADJUSTMENTS), PDFDocumentToolImagesToPdf::createAdjustments(byIndex));
    return parameters;
}

void PDFAppImagesToPdfToolGUI::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    // R16 of step 2: the usable images are the images of the context (implicitly shared bytes)
    const PDFAppToolInputList* list = getInputList();
    for (int i = 0; list && i < list->getCount(); ++i)
    {
        const PDFAppToolInput* input = list->getInput(i);
        if (input->isUsable() && input->isImage())
        {
            environment->images.push_back(input->createToolInputImage());
        }
    }
}

void PDFAppImagesToPdfToolGUI::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    if (name == QLatin1String(PDFDocumentToolImagesToPdf::PARAMETER_INPUTS) || name == QLatin1String(PDFDocumentToolImagesToPdf::PARAMETER_ADJUSTMENTS))
    {
        readValues();
        return;
    }
    updateAll();
}

void PDFAppImagesToPdfToolGUI::onRunStateChanged(bool isRunning)
{
    m_isRunning = isRunning;
    if (isRunning)
    {
        m_isCropping = false;
    }
    BaseClass::onRunStateChanged(isRunning);
    QPointer<PDFAppImagesToPdfToolGUI> guard(this);
    updateImages();
    if (guard)
    {
        updateToolbar();
    }
    if (guard && m_renameDialog && isRunning)
    {
        // Nothing of the list changes during the run
        m_renameDialog->close();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_images.cpp"
