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

#include "pdfocrdocumentdialog.h"
#include "ui_pdfocrdocumentdialog.h"
#include "pdfocrpageview.h"
#include "pdfocrlanguagesdialog.h"
#include "pdfocrexport.h"
#include "pdfocrapplyprocessor.h"
#include "pdfocrcompressionpreviewdialog.h"

#include "pdfcms.h"
#include "pdffont.h"
#include "pdfpage.h"
#include "pdfcatalog.h"
#include "pdfwidgetutils.h"
#include "pdfocrpagepreparer.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfoptionalcontent.h"
#include "pdfdrawspacecontroller.h"

#include <QPushButton>
#include <QMenu>
#include <QLocale>
#include <QSpinBox>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QClipboard>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace pdfviewer
{

static constexpr int ROLE_PAGE_INDEX = Qt::UserRole;
static constexpr int ROLE_ITEM_TYPE = Qt::UserRole + 1;
static constexpr int ROLE_ITEM_ID = Qt::UserRole + 2;
static constexpr int ROLE_LANGUAGE = Qt::UserRole + 3;

static constexpr int ITEM_BLOCK = 0;
static constexpr int ITEM_LINE = 1;
static constexpr int ITEM_WORD = 2;

static constexpr int TEMPORARY_REGION_ID = 1000000;
static constexpr int PREVIEW_DELAY_MSECS = 150;
static constexpr double PREVIEW_DPI = 200.0;
static constexpr qint64 PREVIEW_MAXIMUM_PIXELS = qint64(16) * 1000 * 1000;
static constexpr pdf::PDFInteger MAXIMUM_DOCUMENT_FINGERPRINT_PAGES = 200;

/// Format of the export: plain text, or a structured format (value of PDFOCRStructuredExporter::Format)
static constexpr int EXPORT_FORMAT_TEXT = -1;

/// Value of the resolution of the export, which means the resolution of the recognition
static constexpr int EXPORT_DPI_OF_RECOGNITION = 71;

enum ReviewFilter
{
    FilterAll,
    FilterRequiresReview,
    FilterBelowThreshold,
    FilterUnknownConfidence,
    FilterModified,
    FilterConfirmed,
    FilterDiscarded,
    FilterOutsideDictionary
};

enum HelperSelection
{
    HelperWithoutText,
    HelperOwnLayer,
    HelperErrors,
    HelperWaitingForReview
};

static QString getSettingsGroup()
{
    return QStringLiteral("OCRDialog");
}

static QString getLanguageSuggestion()
{
    // First selection is suggested by the language of the user interface (LANG-03)
    switch (QLocale().language())
    {
        case QLocale::Czech:
            return QStringLiteral("ces");
        case QLocale::Slovak:
            return QStringLiteral("slk");
        case QLocale::German:
            return QStringLiteral("deu");
        case QLocale::Spanish:
            return QStringLiteral("spa");
        case QLocale::Russian:
            return QStringLiteral("rus");
        case QLocale::Chinese:
            return QStringLiteral("chi_sim");
        case QLocale::French:
            return QStringLiteral("fra");
        case QLocale::Korean:
            return QStringLiteral("kor");
        case QLocale::Turkish:
            return QStringLiteral("tur");
        default:
            break;
    }
    return QStringLiteral("eng");
}

PDFOCRDocumentDialog::PDFOCRDocumentDialog(const Context& context, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFOCRDocumentDialog),
    m_context(context),
    m_session(new pdf::PDFOCRSession(this)),
    m_jobController(new pdf::PDFOCRJobController(this)),
    m_modelManager(new pdf::PDFOCRModelManager(this)),
    m_optionalContentActivity(nullptr)
{
    ui->setupUi(this);

    // No button is the default one: Enter pressed in an edit box (page range, text of a
    // word, search) must not click an unrelated button of the dialog.
    for (QPushButton* button : findChildren<QPushButton*>())
    {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    setSizeGripEnabled(true);

    qRegisterMetaType<pdf::PDFOCRPageAnalysis>("pdf::PDFOCRPageAnalysis");

    m_pageCount = pdf::PDFInteger(m_context.document->getCatalog()->getPageCount());
    m_meshQualitySettings = m_context.proxy->getMeshQualitySettings();
    m_isTagged = pdf::PDFOCRPagePreparer::isTaggedDocument(m_context.document);
    m_hasConformanceDeclaration = pdf::PDFOCRPagePreparer::hasConformanceDeclaration(m_context.document, &m_conformanceDeclarations);

    // The visible configuration of the optional content is frozen at the start (IMAGE-03)
    m_optionalContentActivity = new pdf::PDFOptionalContentActivity(m_context.document, pdf::OCUsage::View, this);
    if (const pdf::PDFOptionalContentActivity* activity = m_context.proxy->getOptionalContentActivity())
    {
        for (const pdf::PDFObjectReference& ocg : m_context.document->getCatalog()->getOptionalContentProperties()->getAllOptionalContentGroups())
        {
            m_optionalContentActivity->setState(ocg, activity->getState(ocg), false);
        }
    }

    m_context.proxy->getFontCache()->setCacheShrinkEnabled(this, false);

    m_modelManager->loadBundledCatalog();
    m_modelManager->refresh();

    m_documentFingerprintReady = m_pageCount > MAXIMUM_DOCUMENT_FINGERPRINT_PAGES;
    m_session->setDocument(m_context.document, createIdentity());
    m_jobController->setEnvironment(m_context.document, m_context.proxy->getFontCache(), m_context.cms, m_optionalContentActivity, &m_meshQualitySettings, m_context.proxy->getRendererEngine());

    initializeUi();
    initializePages();
    loadSettings();

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(1280, 820));
    updateUi();
    pdf::PDFWidgetUtils::style(this);

    // Settings can contain the remembered geometry (UI-05)
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
        settings.beginGroup(getSettingsGroup());
        const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
        if (!geometry.isEmpty())
        {
            restoreGeometry(geometry);
        }
        if (!ui->mainSplitter->restoreState(settings.value(QStringLiteral("mainSplitter")).toByteArray()))
        {
            ui->mainSplitter->setSizes({ width() * 20 / 100, width() * 48 / 100, width() * 32 / 100 });
        }
        // The right column follows the workflow: settings first, review after the recognition
        showReviewPanel(false);
        settings.endGroup();
    }

    startPageDataTask();

    if (ui->pagesListWidget->count() > 0)
    {
        const pdf::PDFInteger current = m_context.visiblePages.empty() ? 0 : m_context.visiblePages.front();
        ui->pagesListWidget->setCurrentRow(int(qBound<pdf::PDFInteger>(0, current, m_pageCount - 1)));
    }
}

PDFOCRDocumentDialog::~PDFOCRDocumentDialog()
{
    // Cancel everything first, then wait; late callbacks must not reach the destroyed dialog (UI-07)
    m_jobController->stop();
    cancelTask(m_pageDataTask);
    cancelTask(m_previewTask);
    cancelTask(m_applyTask);
    cancelTask(m_prepareTask);
    cancelTask(m_compressionEstimateTask);

    disconnect(m_jobController, nullptr, this, nullptr);
    m_jobController->waitForFinished();

    for (QFuture<void>& future : m_futures)
    {
        future.waitForFinished();
    }
    m_futures.clear();

    m_context.proxy->getFontCache()->setCacheShrinkEnabled(this, true);
    delete ui;
}

pdf::PDFOCRDocumentIdentity PDFOCRDocumentDialog::createIdentity() const
{
    pdf::PDFOCRDocumentIdentity identity;
    identity.fileName = QFileInfo(m_context.fileName).fileName();
    identity.sourceHash = m_context.document->getSourceDataHash();
    identity.pageCount = m_pageCount;
    identity.isEncrypted = m_context.isEncrypted;

    // The fingerprint of the document is computed by the background page data task (R12);
    // the fingerprint of a large document is expensive, its pages are verified individually
    return identity;
}

int PDFOCRDocumentDialog::getCertificationPermissions(const pdf::PDFDocument* document)
{
    return pdf::PDFOCRApplyProcessor::getCertificationPermissions(document);
}

pdf::PDFOCRApplyProcessor::Context PDFOCRDocumentDialog::getApplyContext() const
{
    pdf::PDFOCRApplyProcessor::Context context;
    context.document = m_context.document;
    context.fileName = m_context.fileName;
    context.canModify = m_context.canModify;
    context.canCopyContent = m_context.canCopyContent;
    context.hasSignatures = m_context.hasSignatures;
    context.isEncrypted = m_context.isEncrypted;
    context.certificationPermissions = m_context.certificationPermissions;
    context.isTagged = m_isTagged;
    context.conformanceDeclarations = m_hasConformanceDeclaration ? m_conformanceDeclarations : QStringList();
    return context;
}

// -------------------------------------------------------------------------
// Initialization
// -------------------------------------------------------------------------

void PDFOCRDocumentDialog::initializeUi()
{
    // Page views
    QVBoxLayout* viewLayout = new QVBoxLayout(ui->viewContainer);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    m_viewSplitter = new QSplitter(Qt::Horizontal, ui->viewContainer);
    m_originalView = new PDFOCRPageView(m_viewSplitter);
    m_workingView = new PDFOCRPageView(m_viewSplitter);
    m_originalView->setAccessibleName(tr("Original page"));
    m_workingView->setAccessibleName(tr("Working image"));
    m_viewSplitter->addWidget(m_originalView);
    m_viewSplitter->addWidget(m_workingView);
    viewLayout->addWidget(m_viewSplitter);

    for (PDFOCRPageView* view : { m_originalView, m_workingView })
    {
        connect(view, &PDFOCRPageView::wordClicked, this, [this](int wordId) { selectWord(wordId, false); });
        connect(view, &PDFOCRPageView::regionClicked, this, [this](int regionId)
        {
            m_originalView->setSelectedRegion(regionId);
            m_workingView->setSelectedRegion(regionId);
            updateUi();
        });
        connect(view, &PDFOCRPageView::rectangleDrawn, this, &PDFOCRDocumentDialog::onRectangleDrawn);
        connect(view, &PDFOCRPageView::regionContextMenuRequested, this, &PDFOCRDocumentDialog::showRegionContextMenu);
        connect(view, &PDFOCRPageView::regionGeometryChanged, this, [this](int regionId, QRectF rectangle)
        {
            const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
            const pdf::PDFOCRRegion* region = page ? page->findRegion(regionId) : nullptr;
            if (region && !isPageInRunningJob(m_currentPage))
            {
                pdf::PDFOCRRegion changed = *region;
                changed.rect = rectangle;
                m_session->updateRegion(m_currentPage, changed);
            }
            else
            {
                // The view shows the geometry of the session again
                onSessionPageChanged(m_currentPage);
            }
        });
        connect(view, &PDFOCRPageView::wordQuadChanged, this, [this](int wordId, pdf::PDFOCRQuad quad)
        {
            if (isPageEditable(m_currentPage))
            {
                m_session->setWordQuad(m_currentPage, wordId, quad);
            }
        });
        connect(view, &PDFOCRPageView::modeFinished, this, [this]()
        {
            ui->addRecognizeRegionButton->setChecked(false);
            ui->addExcludeRegionButton->setChecked(false);
            ui->addLineButton->setChecked(false);
            ui->editGeometryButton->setChecked(false);
            m_originalView->setMode(PDFOCRPageView::Mode::Select);
            m_workingView->setMode(PDFOCRPageView::Mode::Select);
        });
        connect(view, &PDFOCRPageView::zoomChanged, this, [this, view](double zoom)
        {
            ui->zoomLabel->setText(tr("%1 %").arg(qRound(zoom * 100.0)));
            PDFOCRPageView* other = view == m_originalView ? m_workingView : m_originalView;
            if (other->isVisible() && !qFuzzyCompare(other->getZoom(), zoom) && other->hasImage() && view->hasImage())
            {
                other->setZoom(zoom);
            }
        });
    }

    // Combo boxes
    ui->viewModeComboBox->addItem(tr("Original page"), int(ViewMode::Original));
    ui->viewModeComboBox->addItem(tr("Working image"), int(ViewMode::Working));
    ui->viewModeComboBox->addItem(tr("Side by side"), int(ViewMode::SideBySide));

    ui->helperSelectionComboBox->addItem(tr("Pages without text (heuristic)"), HelperWithoutText);
    ui->helperSelectionComboBox->addItem(tr("Pages with own OCR layer"), HelperOwnLayer);
    ui->helperSelectionComboBox->addItem(tr("Pages with errors"), HelperErrors);
    ui->helperSelectionComboBox->addItem(tr("Pages waiting for review"), HelperWaitingForReview);

    ui->parityComboBox->addItem(tr("All pages of the range"), int(pdf::PDFOCRPageSelection::Parity::All));
    ui->parityComboBox->addItem(tr("Odd pages only"), int(pdf::PDFOCRPageSelection::Parity::Odd));
    ui->parityComboBox->addItem(tr("Even pages only"), int(pdf::PDFOCRPageSelection::Parity::Even));

    for (const auto& factory : pdf::PDFOCREngineRegistry::getInstance()->getFactories())
    {
        QString reason;
        if (factory->isAvailable(&reason))
        {
            ui->engineComboBox->addItem(tr("%1 %2").arg(factory->getName(), factory->getVersion()), factory->getIdentifier());
        }
    }

    ui->profileComboBox->addItem(tr("Fast (built-in models)"), int(pdf::PDFOCRModelProfile::Fast));
    ui->profileComboBox->addItem(tr("Standard (larger models, download needed)"), int(pdf::PDFOCRModelProfile::Standard));
    ui->profileComboBox->addItem(tr("Quality (largest models, download needed)"), int(pdf::PDFOCRModelProfile::Best));

    ui->existingTextPolicyComboBox->addItem(tr("Only pages without text"), int(pdf::PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
    ui->existingTextPolicyComboBox->addItem(tr("Add text in the drawn regions"), int(pdf::PDFOCRExistingTextPolicy::AddInRegions));
    ui->existingTextPolicyComboBox->addItem(tr("Replace OCR created by PDF4QT"), int(pdf::PDFOCRExistingTextPolicy::ReplaceOwnLayer));
    ui->existingTextPolicyComboBox->addItem(tr("Recognize for review/export only"), int(pdf::PDFOCRExistingTextPolicy::ReviewOnly));

    for (int resolution : pdf::PDFOCRConfiguration::getStandardResolutions())
    {
        ui->dpiComboBox->addItem(tr("%1 DPI").arg(resolution), resolution);
    }
    ui->dpiComboBox->addItem(tr("Custom"), 0);

    for (QComboBox* comboBox : { ui->rotationComboBox, ui->overrideRotationComboBox })
    {
        if (comboBox == ui->overrideRotationComboBox)
        {
            comboBox->addItem(tr("Common settings"), -1);
        }
        comboBox->addItem(tr("No rotation"), 0);
        comboBox->addItem(tr("90 degrees clockwise"), 90);
        comboBox->addItem(tr("180 degrees"), 180);
        comboBox->addItem(tr("270 degrees clockwise"), 270);
    }

    ui->binarizationComboBox->addItem(tr("Automatic (engine)"), int(pdf::PDFOCRBinarization::Automatic));
    ui->binarizationComboBox->addItem(tr("Otsu (PDF4QT)"), int(pdf::PDFOCRBinarization::Otsu));
    ui->binarizationComboBox->addItem(tr("Adaptive Otsu (engine)"), int(pdf::PDFOCRBinarization::AdaptiveOtsu));
    ui->binarizationComboBox->addItem(tr("Sauvola (engine)"), int(pdf::PDFOCRBinarization::Sauvola));

    ui->outputModeComboBox->addItem(tr("Add/update invisible text in the current document"), int(OutputMode::ModifyCurrent));
    ui->outputModeComboBox->addItem(tr("Create a copy of the document with OCR"), int(OutputMode::CreateCopy));
    ui->outputModeComboBox->addItem(tr("Only export the recognized text"), int(OutputMode::ExportOnly));

    ui->pageSeparatorComboBox->addItem(tr("Readable page label"), int(pdf::PDFOCRTextExporter::PageSeparator::Label));
    ui->pageSeparatorComboBox->addItem(tr("Form feed"), int(pdf::PDFOCRTextExporter::PageSeparator::FormFeed));
    ui->pageSeparatorComboBox->addItem(tr("None"), int(pdf::PDFOCRTextExporter::PageSeparator::None));

    for (pdf::PDFOCRCompressionMode mode : { pdf::PDFOCRCompressionMode::Off, pdf::PDFOCRCompressionMode::Lossless, pdf::PDFOCRCompressionMode::BitonalTextScans, pdf::PDFOCRCompressionMode::Custom })
    {
        ui->compressionModeComboBox->addItem(pdf::PDFOCRCompressionSettings::getModeName(mode), int(mode));
    }
    for (pdf::PDFOCRBitonalEncoding encoding : { pdf::PDFOCRBitonalEncoding::Smallest, pdf::PDFOCRBitonalEncoding::JBIG2, pdf::PDFOCRBitonalEncoding::CCITTGroup4, pdf::PDFOCRBitonalEncoding::Flate })
    {
        ui->bitonalAlgorithmComboBox->addItem(pdf::PDFOCRCompressionSettings::getBitonalEncodingName(encoding), int(encoding));
    }
    for (pdf::PDFOCRThresholdMethod method : { pdf::PDFOCRThresholdMethod::Automatic, pdf::PDFOCRThresholdMethod::Adaptive, pdf::PDFOCRThresholdMethod::Manual })
    {
        ui->bitonalThresholdComboBox->addItem(pdf::PDFOCRCompressionSettings::getThresholdMethodName(method), int(method));
    }

    ui->exportFormatComboBox->addItem(tr("Plain text (TXT)"), EXPORT_FORMAT_TEXT);
    for (pdf::PDFOCRStructuredExporter::Format format : { pdf::PDFOCRStructuredExporter::Format::Hocr, pdf::PDFOCRStructuredExporter::Format::Alto, pdf::PDFOCRStructuredExporter::Format::Tsv })
    {
        ui->exportFormatComboBox->addItem(pdf::PDFOCRStructuredExporter::getFormatName(format), int(format));
    }

    ui->engineModeComboBox->addItem(tr("1 - LSTM neural network"), 1);
    ui->engineModeComboBox->addItem(tr("3 - Default of the available models"), 3);

    ui->reviewFilterComboBox->addItem(tr("All words"), FilterAll);
    ui->reviewFilterComboBox->addItem(tr("Words requiring review"), FilterRequiresReview);
    ui->reviewFilterComboBox->addItem(tr("Words below the confidence threshold"), FilterBelowThreshold);
    ui->reviewFilterComboBox->addItem(tr("Words not found in the dictionary"), FilterOutsideDictionary);
    ui->reviewFilterComboBox->addItem(tr("Words with unknown confidence"), FilterUnknownConfidence);
    ui->reviewFilterComboBox->addItem(tr("Corrected words"), FilterModified);
    ui->reviewFilterComboBox->addItem(tr("Confirmed words"), FilterConfirmed);
    ui->reviewFilterComboBox->addItem(tr("Not text"), FilterDiscarded);

    ui->findScopeComboBox->addItem(tr("Current page"), 0);
    ui->findScopeComboBox->addItem(tr("Checked pages"), 1);
    ui->findScopeComboBox->addItem(tr("All results"), 2);

    updateLayoutCombos();

    ui->preserveLinesCheckBox->setChecked(true);
    ui->showOverlayCheckBox->setChecked(true);
    ui->showRegionsCheckBox->setChecked(true);
    ui->resultsTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->resultsTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->rangeErrorLabel->setVisible(false);
    ui->engineWarningLabel->setVisible(false);
    ui->customDpiSpinBox->setKeyboardTracking(false);

    // Page selection radio buttons (PAGE-01)
    ui->editorSelectionRadioButton->setEnabled(!m_context.selectedPages.empty());
    ui->visiblePagesRadioButton->setEnabled(!m_context.visiblePages.empty());
    ui->currentPageRadioButton->setEnabled(!m_context.visiblePages.empty());
    if (!m_context.selectedPages.empty())
    {
        ui->editorSelectionRadioButton->setChecked(true);
    }
    else
    {
        ui->allPagesRadioButton->setChecked(true);
    }

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(PREVIEW_DELAY_MSECS);
    connect(&m_previewTimer, &QTimer::timeout, this, &PDFOCRDocumentDialog::startPreviewTask);

    // Worker signals
    connect(this, &PDFOCRDocumentDialog::pageDataReady, this, &PDFOCRDocumentDialog::onPageDataReady, Qt::QueuedConnection);
    connect(this, &PDFOCRDocumentDialog::previewReady, this, &PDFOCRDocumentDialog::onPreviewReady, Qt::QueuedConnection);
    connect(this, &PDFOCRDocumentDialog::applyFinished, this, &PDFOCRDocumentDialog::onApplyFinished, Qt::QueuedConnection);
    connect(this, &PDFOCRDocumentDialog::compressionEstimateReady, this, &PDFOCRDocumentDialog::onCompressionEstimateReady, Qt::QueuedConnection);

    // Compression of the scanned images (phase 3 of OCR_PLAN.md)
    m_compressionEstimateTimer.setSingleShot(true);
    m_compressionEstimateTimer.setInterval(500);
    connect(&m_compressionEstimateTimer, &QTimer::timeout, this, &PDFOCRDocumentDialog::startCompressionEstimate);
    for (QComboBox* comboBox : { ui->compressionModeComboBox, ui->bitonalAlgorithmComboBox, ui->bitonalThresholdComboBox })
    {
        connect(comboBox, &QComboBox::currentIndexChanged, this, &PDFOCRDocumentDialog::onCompressionSettingsChanged);
    }
    for (QSpinBox* spinBox : { ui->bitonalThresholdSpinBox, ui->downsampleDpiSpinBox, ui->jpegQualitySpinBox })
    {
        connect(spinBox, &QSpinBox::valueChanged, this, &PDFOCRDocumentDialog::onCompressionSettingsChanged);
    }
    for (QCheckBox* checkBox : { ui->downsampleCheckBox, ui->compressSharedImagesCheckBox })
    {
        connect(checkBox, &QCheckBox::toggled, this, &PDFOCRDocumentDialog::onCompressionSettingsChanged);
    }
    connect(ui->compressionModeComboBox, &QComboBox::activated, this, [this]()
    {
        // The lossy modes are chosen by the user only after a look at their result
        if (getCompressionSettings().isLossy() && !isCompressionPreviewConfirmed())
        {
            QTimer::singleShot(0, this, &PDFOCRDocumentDialog::onCompressionPreviewClicked);
        }
    });
    connect(ui->compressionPreviewButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onCompressionPreviewClicked);
    connect(this, &PDFOCRDocumentDialog::ownLayerLoaded, this, &PDFOCRDocumentDialog::onOwnLayerLoaded, Qt::QueuedConnection);
    connect(this, &PDFOCRDocumentDialog::documentFingerprintReady, this, &PDFOCRDocumentDialog::onDocumentFingerprintReady, Qt::QueuedConnection);
    connect(this, &PDFOCRDocumentDialog::recognitionPrepared, this, &PDFOCRDocumentDialog::onRecognitionPrepared, Qt::QueuedConnection);

    connect(m_jobController, &pdf::PDFOCRJobController::pageStateChanged, this, &PDFOCRDocumentDialog::onJobPageStateChanged, Qt::QueuedConnection);
    connect(m_jobController, &pdf::PDFOCRJobController::pageProgress, this, &PDFOCRDocumentDialog::onJobPageProgress, Qt::QueuedConnection);
    connect(m_jobController, &pdf::PDFOCRJobController::pageFinished, this, &PDFOCRDocumentDialog::onJobPageFinished, Qt::QueuedConnection);
    connect(m_jobController, &pdf::PDFOCRJobController::jobProgress, this, &PDFOCRDocumentDialog::onJobProgress, Qt::QueuedConnection);
    connect(m_jobController, &pdf::PDFOCRJobController::jobFinished, this, &PDFOCRDocumentDialog::onJobFinished, Qt::QueuedConnection);

    connect(m_session, &pdf::PDFOCRSession::pageChanged, this, &PDFOCRDocumentDialog::onSessionPageChanged);
    connect(m_session, &pdf::PDFOCRSession::pagesChanged, this, [this]()
    {
        for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
        {
            updatePageItem(page);
        }
        onCurrentPageChanged();
    });
    connect(m_session, &pdf::PDFOCRSession::undoRedoChanged, this, &PDFOCRDocumentDialog::updateUi);
    connect(m_modelManager, &pdf::PDFOCRModelManager::modelsChanged, this, [this]() { updateLanguageList(getSelectedLanguages()); });

    // Pages
    connect(ui->pagesListWidget, &QListWidget::currentRowChanged, this, &PDFOCRDocumentDialog::onCurrentPageChanged);
    connect(ui->pagesListWidget, &QListWidget::itemChanged, this, [this]() { if (!m_updatingUi) { updateSelectionInfo(); updateUi(); scheduleCompressionEstimate(); } });
    connect(ui->selectAllPagesButton, &QPushButton::clicked, this, [this]()
    {
        std::vector<pdf::PDFInteger> pages(size_t(m_pageCount), 0);
        std::iota(pages.begin(), pages.end(), 0);
        setCheckedPages(pages);
    });
    connect(ui->selectNoPagesButton, &QPushButton::clicked, this, [this]() { setCheckedPages({ }); });
    connect(ui->invertPagesButton, &QPushButton::clicked, this, [this]()
    {
        const std::vector<pdf::PDFInteger> checked = getCheckedPages();
        std::vector<pdf::PDFInteger> pages;
        for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
        {
            if (!std::binary_search(checked.begin(), checked.end(), page))
            {
                pages.push_back(page);
            }
        }
        setCheckedPages(pages);
    });
    connect(ui->applyHelperSelectionButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onApplyHelperSelection);
    connect(ui->applyPageSelectionButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onApplyPageSelection);
    connect(ui->customRangeEdit, &QLineEdit::textEdited, this, [this]() { ui->customRangeRadioButton->setChecked(true); });
    connect(ui->customRangeEdit, &QLineEdit::returnPressed, this, &PDFOCRDocumentDialog::onApplyPageSelection);

    // View
    connect(ui->viewModeComboBox, &QComboBox::currentIndexChanged, this, [this]() { setViewMode(ViewMode(ui->viewModeComboBox->currentData().toInt())); });
    connect(ui->showOverlayCheckBox, &QCheckBox::toggled, this, [this](bool checked) { m_originalView->setOverlayVisible(checked); m_workingView->setOverlayVisible(checked); });
    connect(ui->showRegionsCheckBox, &QCheckBox::toggled, this, [this](bool checked) { m_originalView->setRegionsVisible(checked); m_workingView->setRegionsVisible(checked); });
    connect(ui->zoomInButton, &QToolButton::clicked, this, [this]() { m_originalView->isVisible() ? m_originalView->zoomIn() : m_workingView->zoomIn(); });
    connect(ui->zoomOutButton, &QToolButton::clicked, this, [this]() { m_originalView->isVisible() ? m_originalView->zoomOut() : m_workingView->zoomOut(); });
    connect(ui->zoomFitButton, &QToolButton::clicked, this, [this]() { m_originalView->zoomFit(); m_workingView->zoomFit(); });

    auto connectModeButton = [this](QAbstractButton* button, PDFOCRPageView::Mode mode)
    {
        connect(button, &QAbstractButton::clicked, this, [this, button, mode](bool checked)
        {
            for (QAbstractButton* other : { static_cast<QAbstractButton*>(ui->addRecognizeRegionButton), static_cast<QAbstractButton*>(ui->addExcludeRegionButton),
                                            static_cast<QAbstractButton*>(ui->addLineButton), static_cast<QAbstractButton*>(ui->editGeometryButton) })
            {
                if (other != button)
                {
                    other->setChecked(false);
                }
            }
            const PDFOCRPageView::Mode newMode = checked ? mode : PDFOCRPageView::Mode::Select;
            m_originalView->setMode(newMode);
            m_workingView->setMode(newMode);
        });
    };
    connectModeButton(ui->addRecognizeRegionButton, PDFOCRPageView::Mode::DrawRecognizeRegion);
    connectModeButton(ui->addExcludeRegionButton, PDFOCRPageView::Mode::DrawExcludeRegion);
    connectModeButton(ui->addLineButton, PDFOCRPageView::Mode::DrawLine);
    connectModeButton(ui->editGeometryButton, PDFOCRPageView::Mode::EditWordGeometry);
    connect(ui->regionPropertiesButton, &QToolButton::clicked, this, &PDFOCRDocumentDialog::onRegionProperties);
    connect(ui->removeRegionButton, &QToolButton::clicked, this, &PDFOCRDocumentDialog::onRemoveRegion);
    connect(ui->regionsFromBlocksButton, &QToolButton::clicked, this, [this]()
    {
        // Detected blocks are offered as editable regions (REGION-02)
        const int count = m_session->createRegionsFromBlocks(m_currentPage);
        ui->showRegionsCheckBox->setChecked(true);
        ui->progressLabel->setText(count > 0 ? tr("%n region(s) created from the detected blocks. They can be moved, resized, renamed and removed.", nullptr, count)
                                             : tr("No block without a region was found on the page."));
        schedulePreview();
    });

    // Configuration
    for (QComboBox* comboBox : { ui->engineComboBox, ui->profileComboBox, ui->layoutComboBox, ui->existingTextPolicyComboBox, ui->dpiComboBox, ui->rotationComboBox,
                                 ui->binarizationComboBox, ui->outputModeComboBox, ui->engineModeComboBox })
    {
        connect(comboBox, &QComboBox::currentIndexChanged, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    }
    for (QCheckBox* checkBox : { ui->autoOrientationCheckBox, ui->deskewCheckBox, ui->grayscaleCheckBox, ui->denoiseCheckBox, ui->invertCheckBox, ui->detectBlankPagesCheckBox,
                                 ui->onlyReviewedCheckBox, ui->keepReviewDataCheckBox, ui->reviewDictionaryCheckBox })
    {
        connect(checkBox, &QCheckBox::toggled, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    }
    for (QSpinBox* spinBox : { ui->customDpiSpinBox, ui->reviewThresholdSpinBox, ui->workerCountSpinBox, ui->memoryBudgetSpinBox, ui->pageTimeoutSpinBox })
    {
        connect(spinBox, &QSpinBox::valueChanged, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    }
    connect(ui->whitelistEdit, &QLineEdit::editingFinished, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    connect(ui->blacklistEdit, &QLineEdit::editingFinished, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    // User words are accepted words of the dictionary review, the marks follow them immediately
    connect(ui->userWordsEdit, &QPlainTextEdit::textChanged, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    connect(ui->userPatternsEdit, &QPlainTextEdit::textChanged, this, &PDFOCRDocumentDialog::onConfigurationChanged);
    connect(ui->languagesListWidget, &QListWidget::itemChanged, this, [this]() { if (!m_updatingUi) { onConfigurationChanged(); } });
    connect(ui->advancedLayoutsCheckBox, &QCheckBox::toggled, this, &PDFOCRDocumentDialog::updateLayoutCombos);
    connect(ui->languageUpButton, &QPushButton::clicked, this, [this]()
    {
        const int row = ui->languagesListWidget->currentRow();
        if (row > 0)
        {
            QListWidgetItem* item = ui->languagesListWidget->takeItem(row);
            ui->languagesListWidget->insertItem(row - 1, item);
            ui->languagesListWidget->setCurrentRow(row - 1);
            onConfigurationChanged();
        }
    });
    connect(ui->languageDownButton, &QPushButton::clicked, this, [this]()
    {
        const int row = ui->languagesListWidget->currentRow();
        if (row >= 0 && row + 1 < ui->languagesListWidget->count())
        {
            QListWidgetItem* item = ui->languagesListWidget->takeItem(row);
            ui->languagesListWidget->insertItem(row + 1, item);
            ui->languagesListWidget->setCurrentRow(row + 1);
            onConfigurationChanged();
        }
    });
    connect(ui->manageLanguagesButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onManageLanguages);
    connect(ui->saveProfileButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onSaveProfile);
    connect(ui->loadProfileButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onLoadProfile);
    connect(ui->deleteProfileButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onDeleteProfile);

    // Page override
    connect(ui->pageOverrideGroupBox, &QGroupBox::toggled, this, &PDFOCRDocumentDialog::onPageOverrideChanged);
    connect(ui->overrideLanguagesEdit, &QLineEdit::editingFinished, this, &PDFOCRDocumentDialog::onPageOverrideChanged);
    connect(ui->overrideLayoutComboBox, &QComboBox::currentIndexChanged, this, &PDFOCRDocumentDialog::onPageOverrideChanged);
    connect(ui->overrideRotationComboBox, &QComboBox::currentIndexChanged, this, &PDFOCRDocumentDialog::onPageOverrideChanged);

    // Perspective correction of a photographed page (phase 6 of OCR_PLAN.md)
    connect(ui->perspectiveCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_updatingUi || m_currentPage < 0)
        {
            return;
        }

        std::optional<pdf::PDFOCRQuad> perspective;
        if (checked)
        {
            perspective = m_originalView->getPerspective() ? *m_originalView->getPerspective() : getPageCorners(m_currentPage);
        }
        setPagePerspective(m_currentPage, perspective);
        if (checked)
        {
            startPerspectiveEditing();
        }
    });
    connect(ui->perspectiveEditButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::startPerspectiveEditing);
    connect(ui->perspectiveResetButton, &QPushButton::clicked, this, [this]()
    {
        if (m_currentPage >= 0)
        {
            setPagePerspective(m_currentPage, getPageCorners(m_currentPage));
        }
    });
    connect(ui->perspectiveCopyButton, &QPushButton::clicked, this, [this]()
    {
        const std::optional<pdf::PDFOCRPageOverride> current = m_currentPage >= 0 ? m_session->getPageOverride(m_currentPage) : std::nullopt;
        if (!current || !current->perspective)
        {
            return;
        }

        // Only the pages of the same size can share the corners (page space coordinates)
        const pdf::PDFPage* currentPage = m_context.document->getCatalog()->getPage(m_currentPage);
        int copied = 0;
        int skipped = 0;
        for (pdf::PDFInteger page : getCheckedPages())
        {
            if (page == m_currentPage)
            {
                continue;
            }

            const pdf::PDFPage* otherPage = m_context.document->getCatalog()->getPage(page);
            if (otherPage->getCropBox() != currentPage->getCropBox() || otherPage->getPageRotation() != currentPage->getPageRotation())
            {
                ++skipped;
                continue;
            }
            setPagePerspective(page, current->perspective);
            ++copied;
        }

        QString message = tr("The corners were used for %n page(s).", nullptr, copied);
        if (skipped > 0)
        {
            message += QChar(' ') + tr("%n page(s) of a different size were skipped.", nullptr, skipped);
        }
        ui->perspectiveInfoLabel->setText(message);
    });
    connect(ui->perspectivePreviewCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        // The corrected working image without the overlay of the words
        m_workingView->setOverlayVisible(!checked && ui->showOverlayCheckBox->isChecked());
        setViewMode(checked ? ViewMode::Working : ViewMode::SideBySide);
    });
    connect(m_originalView, &PDFOCRPageView::perspectiveEdited, this, [this](pdf::PDFOCRQuad perspective)
    {
        if (m_currentPage < 0)
        {
            return;
        }

        const QString error = pdf::PDFOCRPagePreparer::validatePerspective(perspective, m_context.document->getCatalog()->getPage(m_currentPage)->getCropBox());
        if (!error.isEmpty())
        {
            QMessageBox::warning(this, windowTitle(), tr("The corners are not used: %1").arg(error));
            updatePageOverrideUi();
            return;
        }
        setPagePerspective(m_currentPage, perspective);
    });
    connect(ui->clearPageOverrideButton, &QPushButton::clicked, this, [this]()
    {
        m_session->clearPageOverride(m_currentPage);
        updatePageOverrideUi();
        schedulePreview();
    });
    connect(ui->overrideBlankCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!m_updatingUi && m_currentPage >= 0)
        {
            m_session->setBlankDetectionOverridden(m_currentPage, checked);
        }
    });

    // Review
    connect(ui->reviewFilterComboBox, &QComboBox::currentIndexChanged, this, &PDFOCRDocumentDialog::updateResultsTree);
    connect(ui->resultsTreeWidget, &QTreeWidget::itemSelectionChanged, this, &PDFOCRDocumentDialog::onTreeSelectionChanged);
    connect(ui->resultsTreeWidget, &QTreeWidget::customContextMenuRequested, this, &PDFOCRDocumentDialog::showTreeContextMenu);
    connect(ui->wordTextEdit, &QLineEdit::editingFinished, this, &PDFOCRDocumentDialog::onWordTextEdited);
    connect(ui->lineTextEdit, &QLineEdit::editingFinished, this, &PDFOCRDocumentDialog::onLineTextEdited);
    connect(ui->confirmNextButton, &QPushButton::clicked, this, [this]() { onReviewNavigation(true, true); });
    connect(ui->skipButton, &QPushButton::clicked, this, [this]() { onReviewNavigation(true, false); });
    connect(ui->previousReviewButton, &QPushButton::clicked, this, [this]() { onReviewNavigation(false, false); });
    connect(ui->restoreOriginalButton, &QPushButton::clicked, this, [this]() { if (isPageEditable(m_currentPage)) { m_session->restoreOriginalText(m_currentPage, m_selectedWordId); } });
    connect(ui->notTextButton, &QPushButton::clicked, this, [this]() { if (isPageEditable(m_currentPage)) { m_session->setWordReviewState(m_currentPage, m_selectedWordId, pdf::PDFOCRReviewState::Discarded); } });
    connect(ui->rerecognizeButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onRerecognizeClicked);
    connect(ui->mergeButton, &QPushButton::clicked, this, [this]()
    {
        const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
        const pdf::PDFOCRLine* line = page ? page->findLineOfWord(m_selectedWordId) : nullptr;
        if (!line || !isPageEditable(m_currentPage))
        {
            return;
        }
        for (size_t i = 0; i + 1 < line->words.size(); ++i)
        {
            if (line->words[i].id == m_selectedWordId)
            {
                int mergedId = 0;
                if (m_session->mergeWords(m_currentPage, m_selectedWordId, line->words[i + 1].id, &mergedId))
                {
                    selectWord(mergedId, false);
                }
                break;
            }
        }
    });
    connect(ui->splitButton, &QPushButton::clicked, this, [this]()
    {
        if (!isPageEditable(m_currentPage))
        {
            return;
        }
        int newWordId = 0;
        if (!m_session->splitWord(m_currentPage, m_selectedWordId, ui->wordTextEdit->cursorPosition(), &newWordId))
        {
            QMessageBox::information(this, windowTitle(), tr("Place the cursor inside the text of the word, where it should be split."));
        }
    });
    connect(ui->insertWordButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onInsertWord);
    connect(ui->deleteWordButton, &QPushButton::clicked, this, [this]() { if (isPageEditable(m_currentPage)) { m_session->removeWord(m_currentPage, m_selectedWordId); } });
    connect(ui->moveUpButton, &QPushButton::clicked, this, [this]() { onMoveItem(true); });
    connect(ui->moveDownButton, &QPushButton::clicked, this, [this]() { onMoveItem(false); });
    connect(ui->confirmAllButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onConfirmAll);
    connect(ui->findNextButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onFindNext);
    connect(ui->findEdit, &QLineEdit::returnPressed, this, &PDFOCRDocumentDialog::onFindNext);
    connect(ui->replaceAllButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onReplaceAll);

    // Bottom buttons
    connect(ui->undoButton, &QPushButton::clicked, m_session, &pdf::PDFOCRSession::undo);
    connect(ui->redoButton, &QPushButton::clicked, m_session, &pdf::PDFOCRSession::redo);
    connect(ui->recognizeButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onRecognizeClicked);
    connect(ui->stopButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onStopClicked);
    connect(ui->applyButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onApplyClicked);
    connect(ui->removeLayerButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onRemoveLayerClicked);
    connect(ui->exportButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onExportClicked);
    connect(ui->exportFormatComboBox, &QComboBox::currentIndexChanged, this, [this]()
    {
        ui->exportOptionsStack->setCurrentIndex(ui->exportFormatComboBox->currentData().toInt() == EXPORT_FORMAT_TEXT ? 0 : 1);
    });
    connect(ui->openProjectButton, &QPushButton::clicked, this, &PDFOCRDocumentDialog::onOpenProject);
    connect(ui->saveProjectButton, &QPushButton::clicked, this, [this]() { saveProject(); });
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);

    // Shortcuts follow the state of the buttons (no undo while the layer is being written)
    QShortcut* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, [this]() { if (ui->undoButton->isEnabled()) { m_session->undo(); } });
    QShortcut* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, [this]() { if (ui->redoButton->isEnabled()) { m_session->redo(); } });
    QShortcut* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]()
    {
        ui->reviewTabWidget->setCurrentWidget(ui->findReplaceTab);
        ui->findEdit->setFocus();
        ui->findEdit->selectAll();
    });

    setViewMode(ViewMode::Original);
}

void PDFOCRDocumentDialog::initializePages()
{
    m_updatingUi = true;

    // Default selection: explicit selection of the editor, otherwise all pages (PAGE-01)
    std::set<pdf::PDFInteger> selected(m_context.selectedPages.begin(), m_context.selectedPages.end());

    QPixmap placeholder(ui->pagesListWidget->iconSize());
    placeholder.fill(palette().color(QPalette::Base));

    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        QListWidgetItem* item = new QListWidgetItem(ui->pagesListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(ROLE_PAGE_INDEX, qint64(page));
        item->setCheckState(selected.empty() || selected.count(page) ? Qt::Checked : Qt::Unchecked);
        item->setIcon(QIcon(placeholder));
    }

    m_updatingUi = false;

    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        updatePageItem(page);
    }
    updateSelectionInfo();
}

void PDFOCRDocumentDialog::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());

    pdf::PDFOCRConfiguration configuration;
    const QByteArray configurationData = settings.value(QStringLiteral("configuration")).toByteArray();
    if (!configurationData.isEmpty())
    {
        configuration = pdf::PDFOCRConfiguration::fromJson(QJsonDocument::fromJson(configurationData).object());
    }

    if (ui->engineComboBox->findData(configuration.engineId) == -1 && ui->engineComboBox->count() > 0)
    {
        configuration.engineId = ui->engineComboBox->itemData(0).toString();
    }

    if (configuration.languages.isEmpty())
    {
        const QString suggestion = getLanguageSuggestion();
        configuration.languages << (m_modelManager->isLanguageUsable(configuration.engineId, suggestion, configuration.profile) ? suggestion : QStringLiteral("eng"));
    }

    ui->preserveLinesCheckBox->setChecked(settings.value(QStringLiteral("exportPreserveLines"), true).toBool());
    ui->joinHyphenatedCheckBox->setChecked(settings.value(QStringLiteral("exportJoinHyphenated"), false).toBool());
    ui->normalizeNfcCheckBox->setChecked(settings.value(QStringLiteral("exportNormalizeNfc"), false).toBool());
    ui->pageSeparatorComboBox->setCurrentIndex(qMax(0, ui->pageSeparatorComboBox->findData(settings.value(QStringLiteral("exportPageSeparator"), int(pdf::PDFOCRTextExporter::PageSeparator::Label)))));
    ui->exportFormatComboBox->setCurrentIndex(qMax(0, ui->exportFormatComboBox->findData(settings.value(QStringLiteral("exportFormat"), EXPORT_FORMAT_TEXT))));
    ui->exportOptionsStack->setCurrentIndex(ui->exportFormatComboBox->currentData().toInt() == EXPORT_FORMAT_TEXT ? 0 : 1);
    ui->exportDpiSpinBox->setValue(settings.value(QStringLiteral("exportDpi"), EXPORT_DPI_OF_RECOGNITION).toInt());
    ui->exportConfidenceCheckBox->setChecked(settings.value(QStringLiteral("exportConfidence"), true).toBool());
    ui->exportPerPageCheckBox->setChecked(settings.value(QStringLiteral("exportPerPage"), false).toBool());
    settings.endGroup();

    m_session->setConfiguration(configuration);
    m_session->setDirty(false);
    setConfigurationToUi(configuration);
    updateProfiles();
}

void PDFOCRDocumentDialog::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainSplitter"), ui->mainSplitter->saveState());

    // Only the general settings are stored, never the text of the document (REC-02)
    settings.setValue(QStringLiteral("configuration"), QJsonDocument(getConfigurationFromUi().toJson()).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("exportPreserveLines"), ui->preserveLinesCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportJoinHyphenated"), ui->joinHyphenatedCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportNormalizeNfc"), ui->normalizeNfcCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportPageSeparator"), ui->pageSeparatorComboBox->currentData());
    settings.setValue(QStringLiteral("exportFormat"), ui->exportFormatComboBox->currentData());
    settings.setValue(QStringLiteral("exportDpi"), ui->exportDpiSpinBox->value());
    settings.setValue(QStringLiteral("exportConfidence"), ui->exportConfidenceCheckBox->isChecked());
    settings.setValue(QStringLiteral("exportPerPage"), ui->exportPerPageCheckBox->isChecked());
    settings.endGroup();
}

// -------------------------------------------------------------------------
// Background tasks
// -------------------------------------------------------------------------

void PDFOCRDocumentDialog::startTask(AsyncTask& task, std::function<void(int, const pdf::PDFOperationControl*)> worker)
{
    cancelTask(task);

    std::erase_if(m_futures, [](const QFuture<void>& future) { return future.isFinished(); });

    task.token = std::make_shared<pdf::PDFOCRCancelToken>();
    const int generation = task.generation;
    std::shared_ptr<pdf::PDFOCRCancelToken> token = task.token;

    m_futures.push_back(QtConcurrent::run([worker = std::move(worker), generation, token]()
    {
        try
        {
            worker(generation, token.get());
        }
        catch (...)
        {
            // No exception is allowed to escape into the future, which is waited in the destructor
        }
    }));
}

void PDFOCRDocumentDialog::cancelTask(AsyncTask& task)
{
    ++task.generation;
    if (task.token)
    {
        task.token->cancel();
        task.token.reset();
    }
}

void PDFOCRDocumentDialog::startPageDataTask()
{
    const pdf::PDFDocument* document = m_context.document;
    const pdf::PDFFontCache* fontCache = m_context.proxy->getFontCache();
    const pdf::PDFCMS* cms = m_context.cms;
    const pdf::PDFOptionalContentActivity* activity = m_optionalContentActivity;
    const pdf::PDFMeshQualitySettings* meshQualitySettings = &m_meshQualitySettings;
    const pdf::RendererEngine rendererEngine = m_context.proxy->getRendererEngine();
    const pdf::PDFInteger pageCount = m_pageCount;
    const int thumbnailSize = qRound(128 * devicePixelRatioF());

    // Pages near the current page are processed first
    std::vector<pdf::PDFInteger> order(size_t(pageCount), 0);
    std::iota(order.begin(), order.end(), 0);
    const pdf::PDFInteger first = m_context.visiblePages.empty() ? 0 : m_context.visiblePages.front();
    std::stable_sort(order.begin(), order.end(), [first](pdf::PDFInteger left, pdf::PDFInteger right) { return qAbs(left - first) < qAbs(right - first); });

    const bool computeDocumentFingerprint = !m_documentFingerprintReady;

    startTask(m_pageDataTask, [this, document, fontCache, cms, activity, meshQualitySettings, rendererEngine, order, thumbnailSize, computeDocumentFingerprint](int generation, const pdf::PDFOperationControl* operationControl)
    {
        pdf::PDFOCRPagePreparer preparer(document, fontCache, cms, activity, *meshQualitySettings, rendererEngine);

        for (pdf::PDFInteger pageIndex : order)
        {
            if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
            {
                return;
            }

            pdf::PDFOCRPageAnalysis analysis = preparer.analyze(pageIndex, operationControl);
            QByteArray fingerprint;
            try
            {
                // Fingerprints are needed by the recognition, by the project and by the application (R12)
                fingerprint = pdf::PDFOCRPagePreparer::computePageFingerprint(document, pageIndex);
            }
            catch (const pdf::PDFException&)
            {
                // The fingerprint is computed again, when it is needed
            }

            QImage thumbnail;
            const pdf::PDFPage* page = document->getCatalog()->getPage(size_t(pageIndex));
            const QSizeF pageSize = page->getRotatedCropBox().size();
            const double largest = qMax(pageSize.width(), pageSize.height());
            if (largest > 0.0)
            {
                const double dpi = qBound(4.0, thumbnailSize * 72.0 / largest, 72.0);
                pdf::PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(pageIndex, dpi, { }, PREVIEW_MAXIMUM_PIXELS, operationControl);
                thumbnail = raster.image;
            }

            if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
            {
                return;
            }

            Q_EMIT pageDataReady(generation, pageIndex, thumbnail, analysis, fingerprint);

            // Own OCR layer written earlier is read back, so the text can be corrected
            // further without a new recognition (PDF-10)
            if (analysis.hasOwnOCRLayer)
            {
                try
                {
                    pdf::PDFOCRTextLayerWriter::LayerInfo layerInfo;
                    std::optional<pdf::PDFOCRPageResult> layer = pdf::PDFOCRTextLayerWriter::readLayer(document, pageIndex, &layerInfo);
                    if (layer && layerInfo.fingerprintMatches && pdf::PDFOCRValidator::validate(*layer).isEmpty())
                    {
                        layer->analysis = analysis;
                        Q_EMIT ownLayerLoaded(generation, std::move(*layer));
                    }
                }
                catch (const pdf::PDFException&)
                {
                    // Damaged layer data: the page is simply offered for a new recognition
                }
            }
        }

        // The fingerprint of the document is not computed in the constructor (R12)
        if (computeDocumentFingerprint && !pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            try
            {
                QByteArray fingerprint = pdf::PDFOCRPagePreparer::computeDocumentFingerprint(document);
                Q_EMIT documentFingerprintReady(generation, std::move(fingerprint));
            }
            catch (const pdf::PDFException&)
            {
                // The fingerprint is computed again, when it is needed
            }
        }
    });
}

void PDFOCRDocumentDialog::onDocumentFingerprintReady(int generation, QByteArray fingerprint)
{
    if (generation != m_pageDataTask.generation || m_documentFingerprintReady)
    {
        return;
    }

    pdf::PDFOCRDocumentIdentity identity = m_session->getDocumentIdentity();
    identity.fingerprint = std::move(fingerprint);
    m_session->setDocument(m_context.document, std::move(identity));
    m_documentFingerprintReady = true;
}

void PDFOCRDocumentDialog::runBlockingTask(const QString& text, const std::function<void(const pdf::PDFOperationControl*)>& worker)
{
    std::shared_ptr<pdf::PDFOCRCancelToken> token = std::make_shared<pdf::PDFOCRCancelToken>();
    QFuture<void> future = QtConcurrent::run([&worker, token]()
    {
        try
        {
            worker(token.get());
        }
        catch (...)
        {
            // No exception is allowed to escape into the future
        }
    });

    if (!future.isFinished())
    {
        // The GUI is repainted and the background results are delivered, but the user
        // cannot start another action, until the worker finishes
        const bool wasBlocking = m_blockingTaskInProgress;
        m_blockingTaskInProgress = true;
        const QString previousText = ui->progressLabel->text();
        ui->progressLabel->setText(text);
        QApplication::setOverrideCursor(Qt::BusyCursor);

        QEventLoop loop;
        QFutureWatcher<void> watcher;
        connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        if (!future.isFinished())
        {
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        }

        QApplication::restoreOverrideCursor();
        ui->progressLabel->setText(previousText);
        m_blockingTaskInProgress = wasBlocking;
    }

    future.waitForFinished();
}

void PDFOCRDocumentDialog::ensureFingerprints(const std::vector<pdf::PDFInteger>& pages, bool documentFingerprint)
{
    std::vector<pdf::PDFInteger> missingPages;
    for (pdf::PDFInteger page : pages)
    {
        if (page >= 0 && page < m_pageCount && !m_fingerprints.count(page))
        {
            missingPages.push_back(page);
        }
    }

    const bool computeDocumentFingerprint = documentFingerprint && !m_documentFingerprintReady;
    if (missingPages.empty() && !computeDocumentFingerprint)
    {
        return;
    }

    const pdf::PDFDocument* document = m_context.document;
    std::map<pdf::PDFInteger, QByteArray> fingerprints;
    QByteArray fingerprint;
    bool fingerprintComputed = false;
    runBlockingTask(tr("Computing the fingerprints of the pages..."), [&](const pdf::PDFOperationControl*)
    {
        for (pdf::PDFInteger page : missingPages)
        {
            fingerprints[page] = pdf::PDFOCRPagePreparer::computePageFingerprint(document, page);
        }
        if (computeDocumentFingerprint)
        {
            fingerprint = pdf::PDFOCRPagePreparer::computeDocumentFingerprint(document);
            fingerprintComputed = true;
        }
    });

    for (const auto& item : fingerprints)
    {
        m_fingerprints.emplace(item.first, item.second);
    }

    if (fingerprintComputed && !m_documentFingerprintReady)
    {
        pdf::PDFOCRDocumentIdentity identity = m_session->getDocumentIdentity();
        identity.fingerprint = std::move(fingerprint);
        m_session->setDocument(m_context.document, std::move(identity));
        m_documentFingerprintReady = true;
    }
}

void PDFOCRDocumentDialog::onOwnLayerLoaded(int generation, pdf::PDFOCRPageResult result)
{
    if (generation != m_pageDataTask.generation)
    {
        return;
    }

    const pdf::PDFInteger pageIndex = result.pageIndex;
    const pdf::PDFOCRPageResult* existing = m_session->getPage(pageIndex);
    if ((existing && (existing->hasResult() || !existing->blocks.empty())) || isPageInRunningJob(pageIndex))
    {
        // Result of this session (recognition, project) has the priority
        return;
    }

    // Loading of the layer is not a change, which should be saved into a project
    const bool wasDirty = m_session->isDirty();
    m_fingerprints[pageIndex] = result.pageFingerprint;
    m_session->setPageResult(std::move(result));
    m_session->setDirty(wasDirty);

    showReviewPanel(true);
    updatePageItem(pageIndex);
    updateUi();
    if (pageIndex == m_currentPage)
    {
        onCurrentPageChanged();
    }
}

void PDFOCRDocumentDialog::onPageDataReady(int generation, qint64 pageIndex, QImage thumbnail, pdf::PDFOCRPageAnalysis analysis, QByteArray fingerprint)
{
    if (generation != m_pageDataTask.generation)
    {
        return;
    }

    m_analysis[pageIndex] = std::move(analysis);
    if (!fingerprint.isEmpty())
    {
        m_fingerprints.emplace(pageIndex, std::move(fingerprint));
    }

    if (QListWidgetItem* item = ui->pagesListWidget->item(int(pageIndex)))
    {
        if (!thumbnail.isNull())
        {
            m_updatingUi = true;
            QPixmap pixmap = QPixmap::fromImage(thumbnail.scaled(ui->pagesListWidget->iconSize() * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            pixmap.setDevicePixelRatio(devicePixelRatioF());
            item->setIcon(QIcon(pixmap));
            m_updatingUi = false;
        }
    }

    updatePageItem(pageIndex);

    if (pageIndex == m_currentPage)
    {
        onCurrentPageChanged();
    }
}

void PDFOCRDocumentDialog::schedulePreview()
{
    cancelTask(m_previewTask);
    m_previewTimer.start();
}

void PDFOCRDocumentDialog::startPreviewTask()
{
    if (m_currentPage < 0)
    {
        return;
    }

    const pdf::PDFDocument* document = m_context.document;
    const pdf::PDFFontCache* fontCache = m_context.proxy->getFontCache();
    const pdf::PDFCMS* cms = m_context.cms;
    const pdf::PDFOptionalContentActivity* activity = m_optionalContentActivity;
    const pdf::PDFMeshQualitySettings* meshQualitySettings = &m_meshQualitySettings;
    const pdf::RendererEngine rendererEngine = m_context.proxy->getRendererEngine();
    const pdf::PDFInteger pageIndex = m_currentPage;
    const ViewMode viewMode = ViewMode(ui->viewModeComboBox->currentData().toInt());
    pdf::PDFOCRConfiguration configuration = m_session->getEffectiveConfiguration(pageIndex);

    // The working image of a recognized page reproduces the orientation and the resolution
    // of its last recognition, so the review shows the image, on which the engine decided;
    // a page without a result shows the preview of the current settings (R10, UI-04)
    std::vector<pdf::PDFOCRRegion> regions;
    std::optional<pdf::PDFOCROrientation> orientation;
    bool isLastRecognition = false;
    if (const pdf::PDFOCRPageResult* page = m_session->getPage(pageIndex))
    {
        regions = page->regions;
        if (page->hasResult() && page->geometry.dpi > 0.0)
        {
            isLastRecognition = true;
            orientation = page->orientation;
            if (orientation)
            {
                configuration.preprocessing.autoOrientation = true;
            }
            if (page->geometry.requestedDpi > 0.0)
            {
                configuration.dpi = page->geometry.requestedDpi;
            }
        }
    }

    std::vector<QRectF> maskedRectangles;
    if (const pdf::PDFOCRPageAnalysis* analysis = getAnalysis(pageIndex))
    {
        maskedRectangles = analysis->annotationRectangles;
        maskedRectangles.insert(maskedRectangles.end(), analysis->redactionRectangles.begin(), analysis->redactionRectangles.end());
    }

    startTask(m_previewTask, [=, this](int generation, const pdf::PDFOperationControl* operationControl)
    {
        pdf::PDFOCRPagePreparer preparer(document, fontCache, cms, activity, *meshQualitySettings, rendererEngine);

        QImage original;
        QTransform pageToOriginal;
        QImage working;
        QTransform pageToWorking;
        QString message;

        if (viewMode != ViewMode::Working)
        {
            pdf::PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(pageIndex, PREVIEW_DPI, { }, PREVIEW_MAXIMUM_PIXELS, operationControl);
            if (raster.error && raster.error.code != pdf::PDFOCRErrorCode::Cancelled)
            {
                message = raster.error.message;
            }
            original = raster.image;
            pageToOriginal = raster.geometry.pageToRaster;
        }

        if (viewMode != ViewMode::Original && !pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            pdf::PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(pageIndex, configuration.dpi, maskedRectangles, pdf::PDFOCRPagePreparer::DefaultMaximumPixels, operationControl);
            if (raster.error)
            {
                if (raster.error.code != pdf::PDFOCRErrorCode::Cancelled)
                {
                    message = raster.error.message;
                }
            }
            else
            {
                pdf::PDFOCRPagePreparer::PreprocessResult preprocessed = pdf::PDFOCRPagePreparer::preprocess(raster.image, raster.geometry, configuration.preprocessing, orientation, operationControl);
                if (!preprocessed.error)
                {
                    pageToWorking = preprocessed.geometry.getPageToEngine();
                    working = pdf::PDFOCRPagePreparer::maskRegions(preprocessed.image, pageToWorking, regions);
                }
            }
        }

        if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        Q_EMIT previewReady(generation, pageIndex, original, pageToOriginal, working, pageToWorking, message, isLastRecognition);
    });
}

void PDFOCRDocumentDialog::onPreviewReady(int generation, qint64 pageIndex, QImage original, QTransform pageToOriginal, QImage working, QTransform pageToWorking, QString message, bool isLastRecognition)
{
    if (generation != m_previewTask.generation || pageIndex != m_currentPage)
    {
        // Late result of an obsolete preview is ignored (JOB-02)
        return;
    }

    if (!original.isNull())
    {
        m_originalView->setImage(original, pageToOriginal, tr("Original"));
    }
    else if (m_originalView->isVisible())
    {
        m_originalView->clearImage(message.isEmpty() ? tr("Page cannot be displayed.") : message);
    }

    if (!working.isNull())
    {
        QString caption;
        if (isLastRecognition)
        {
            caption = tr("Working image of the last recognition, %1 x %2 pixels").arg(working.width()).arg(working.height());
        }
        else
        {
            caption = tr("Preview of the current settings, %1 x %2 pixels").arg(working.width()).arg(working.height());
            if (m_session->getEffectiveConfiguration(pageIndex).preprocessing.autoOrientation)
            {
                caption += tr(" (orientation is detected during the recognition)");
            }
        }
        m_workingView->setImage(working, pageToWorking, caption);
    }
    else if (m_workingView->isVisible())
    {
        m_workingView->clearImage(message.isEmpty() ? tr("Working image cannot be created.") : message);
    }
}

// -------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------

pdf::PDFOCRConfiguration PDFOCRDocumentDialog::getConfigurationFromUi() const
{
    pdf::PDFOCRConfiguration configuration = m_session->getConfiguration();
    configuration.engineId = ui->engineComboBox->currentData().toString();
    if (configuration.engineId.isEmpty())
    {
        configuration.engineId = QStringLiteral("tesseract");
    }
    configuration.languages = getSelectedLanguages();
    configuration.profile = pdf::PDFOCRModelProfile(ui->profileComboBox->currentData().toInt());
    configuration.layout = pdf::PDFOCRLayout(ui->layoutComboBox->currentData().toInt());
    configuration.existingTextPolicy = pdf::PDFOCRExistingTextPolicy(ui->existingTextPolicyComboBox->currentData().toInt());
    configuration.engineMode = ui->engineModeComboBox->currentData().toInt();

    const int resolution = ui->dpiComboBox->currentData().toInt();
    configuration.dpi = resolution > 0 ? resolution : ui->customDpiSpinBox->value();

    configuration.preprocessing.rotation = ui->rotationComboBox->currentData().toInt();
    configuration.preprocessing.autoOrientation = ui->autoOrientationCheckBox->isChecked();
    configuration.preprocessing.deskew = ui->deskewCheckBox->isChecked();
    configuration.preprocessing.grayscale = ui->grayscaleCheckBox->isChecked();
    configuration.preprocessing.denoise = ui->denoiseCheckBox->isChecked();
    configuration.preprocessing.invert = ui->invertCheckBox->isChecked();
    configuration.preprocessing.binarization = pdf::PDFOCRBinarization(ui->binarizationComboBox->currentData().toInt());
    configuration.detectBlankPages = ui->detectBlankPagesCheckBox->isChecked();
    configuration.keepReviewDataInDocument = ui->keepReviewDataCheckBox->isChecked();
    configuration.reviewThreshold = ui->reviewThresholdSpinBox->value();
    configuration.reviewOutsideDictionary = ui->reviewDictionaryCheckBox->isChecked();
    configuration.compression.mode = pdf::PDFOCRCompressionMode(ui->compressionModeComboBox->currentData().toInt());
    configuration.compression.bitonalEncoding = pdf::PDFOCRBitonalEncoding(ui->bitonalAlgorithmComboBox->currentData().toInt());
    configuration.compression.thresholdMethod = pdf::PDFOCRThresholdMethod(ui->bitonalThresholdComboBox->currentData().toInt());
    configuration.compression.manualThreshold = ui->bitonalThresholdSpinBox->value();
    configuration.compression.downsample = ui->downsampleCheckBox->isChecked();
    configuration.compression.downsampleDpi = ui->downsampleDpiSpinBox->value();
    configuration.compression.jpegQuality = ui->jpegQualitySpinBox->value();
    configuration.compression.compressSharedImages = ui->compressSharedImagesCheckBox->isChecked();
    configuration.compression.excludedImages.clear();
    configuration.workerCount = ui->workerCountSpinBox->value();
    configuration.memoryBudget = qint64(ui->memoryBudgetSpinBox->value()) << 20;
    configuration.pageTimeoutSeconds = ui->pageTimeoutSpinBox->value();
    configuration.characterWhitelist = ui->whitelistEdit->text();
    configuration.characterBlacklist = ui->blacklistEdit->text();
    configuration.userWords = ui->userWordsEdit->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts);
    configuration.userPatterns = ui->userPatternsEdit->toPlainText().split(QChar('\n'), Qt::SkipEmptyParts);
    return configuration;
}

void PDFOCRDocumentDialog::setConfigurationToUi(const pdf::PDFOCRConfiguration& configuration)
{
    m_updatingUi = true;

    auto select = [](QComboBox* comboBox, const QVariant& value)
    {
        const int index = comboBox->findData(value);
        if (index >= 0)
        {
            comboBox->setCurrentIndex(index);
        }
    };

    select(ui->engineComboBox, configuration.engineId);
    select(ui->profileComboBox, int(configuration.profile));
    select(ui->existingTextPolicyComboBox, int(configuration.existingTextPolicy));
    select(ui->engineModeComboBox, configuration.engineMode);

    if (!pdf::PDFOCRConfiguration::isBasicLayout(configuration.layout))
    {
        ui->advancedLayoutsCheckBox->setChecked(true);
    }
    updateLayoutCombos();
    select(ui->layoutComboBox, int(configuration.layout));

    const int dpiIndex = ui->dpiComboBox->findData(qRound(configuration.dpi));
    ui->dpiComboBox->setCurrentIndex(dpiIndex >= 0 ? dpiIndex : ui->dpiComboBox->findData(0));
    ui->customDpiSpinBox->setValue(qRound(configuration.dpi));

    select(ui->rotationComboBox, configuration.preprocessing.rotation);
    select(ui->binarizationComboBox, int(configuration.preprocessing.binarization));
    ui->autoOrientationCheckBox->setChecked(configuration.preprocessing.autoOrientation);
    ui->deskewCheckBox->setChecked(configuration.preprocessing.deskew);
    ui->grayscaleCheckBox->setChecked(configuration.preprocessing.grayscale);
    ui->denoiseCheckBox->setChecked(configuration.preprocessing.denoise);
    ui->invertCheckBox->setChecked(configuration.preprocessing.invert);
    ui->detectBlankPagesCheckBox->setChecked(configuration.detectBlankPages);
    ui->keepReviewDataCheckBox->setChecked(configuration.keepReviewDataInDocument);
    ui->reviewThresholdSpinBox->setValue(qRound(configuration.reviewThreshold));
    ui->reviewDictionaryCheckBox->setChecked(configuration.reviewOutsideDictionary);
    select(ui->compressionModeComboBox, int(configuration.compression.mode));
    select(ui->bitonalAlgorithmComboBox, int(configuration.compression.bitonalEncoding));
    select(ui->bitonalThresholdComboBox, int(configuration.compression.thresholdMethod));
    ui->bitonalThresholdSpinBox->setValue(configuration.compression.manualThreshold);
    ui->downsampleCheckBox->setChecked(configuration.compression.downsample);
    ui->downsampleDpiSpinBox->setValue(configuration.compression.downsampleDpi);
    ui->jpegQualitySpinBox->setValue(configuration.compression.jpegQuality);
    ui->compressSharedImagesCheckBox->setChecked(configuration.compression.compressSharedImages);
    ui->workerCountSpinBox->setValue(configuration.workerCount);
    ui->memoryBudgetSpinBox->setValue(int(configuration.memoryBudget >> 20));
    ui->pageTimeoutSpinBox->setValue(configuration.pageTimeoutSeconds);
    ui->whitelistEdit->setText(configuration.characterWhitelist);
    ui->blacklistEdit->setText(configuration.characterBlacklist);
    ui->userWordsEdit->setPlainText(configuration.userWords.join(QChar('\n')));
    ui->userPatternsEdit->setPlainText(configuration.userPatterns.join(QChar('\n')));

    m_updatingUi = false;

    updateEngineCapabilities();
    updateLanguageList(configuration.languages);
    updateMemoryEstimate();
    updatePolicySummary();
    updateCompressionUi();
    updateUi();
}

void PDFOCRDocumentDialog::onConfigurationChanged()
{
    if (m_updatingUi)
    {
        return;
    }

    const pdf::PDFOCRConfiguration oldConfiguration = m_session->getConfiguration();
    const pdf::PDFOCRConfiguration configuration = getConfigurationFromUi();
    const bool dirty = m_session->isDirty();
    m_session->setConfiguration(configuration);
    m_session->setDirty(dirty);

    if (oldConfiguration.profile != configuration.profile || oldConfiguration.engineId != configuration.engineId)
    {
        // Languages of the old profile, which are missing in the new profile, are offered
        // for download; a different model is never used silently (LANG-03)
        std::shared_ptr<pdf::PDFOCREngineFactory> factory = pdf::PDFOCREngineRegistry::getInstance()->getFactory(configuration.engineId);
        const bool usesManagedModels = !factory || factory->usesManagedModels();
        const QStringList missing = usesManagedModels ? m_modelManager->getMissingModels(configuration.engineId, oldConfiguration.languages, configuration.profile) : QStringList();
        updateLanguageList(oldConfiguration.languages);
        if (!missing.isEmpty())
        {
            if (QMessageBox::question(this, windowTitle(), tr("The following language models are not installed for the profile '%1':\n\n%2\n\nDo you want to open the language manager to download them?")
                                      .arg(pdf::PDFOCRConfiguration::getProfileName(configuration.profile), missing.join(QChar('\n')))) == QMessageBox::Yes)
            {
                onManageLanguages();
            }
        }
    }

    ui->customDpiSpinBox->setEnabled(ui->dpiComboBox->currentData().toInt() == 0);
    m_originalView->setReviewCriteria(m_session->getReviewCriteria());
    m_workingView->setReviewCriteria(m_session->getReviewCriteria());

    updateEngineCapabilities();
    updateMemoryEstimate();
    updatePolicySummary();

    if (!qFuzzyCompare(oldConfiguration.reviewThreshold, configuration.reviewThreshold) ||
        oldConfiguration.reviewOutsideDictionary != configuration.reviewOutsideDictionary ||
        oldConfiguration.userWords != configuration.userWords)
    {
        // Change of the review criteria (threshold, dictionary, accepted user words)
        // does not require a new recognition (JOB-10)
        updateResultsTree();
        for (pdf::PDFInteger page : m_session->getPagesWithResults())
        {
            updatePageItem(page);
        }
    }

    if (oldConfiguration.dpi != configuration.dpi || !(oldConfiguration.preprocessing == configuration.preprocessing))
    {
        schedulePreview();
    }

    updateUi();
}

void PDFOCRDocumentDialog::updateLayoutCombos()
{
    const bool advanced = ui->advancedLayoutsCheckBox->isChecked();
    const QVariant currentLayout = ui->layoutComboBox->currentData();
    const QVariant currentOverride = ui->overrideLayoutComboBox->currentData();

    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;
    ui->layoutComboBox->clear();
    ui->overrideLayoutComboBox->clear();
    ui->overrideLayoutComboBox->addItem(tr("Common settings"), -1);

    for (pdf::PDFOCRLayout layout : pdf::PDFOCRConfiguration::getLayouts())
    {
        if (!advanced && !pdf::PDFOCRConfiguration::isBasicLayout(layout))
        {
            continue;
        }

        QString name = pdf::PDFOCRConfiguration::getLayoutName(layout);
        if (advanced)
        {
            name = tr("%1 - %2").arg(int(layout)).arg(name);
        }

        for (QComboBox* comboBox : { ui->layoutComboBox, ui->overrideLayoutComboBox })
        {
            comboBox->addItem(name, int(layout));
            comboBox->setItemData(comboBox->count() - 1, pdf::PDFOCRConfiguration::getLayoutDescription(layout), Qt::ToolTipRole);
        }
    }

    const int layoutIndex = ui->layoutComboBox->findData(currentLayout.isValid() ? currentLayout : QVariant(int(pdf::PDFOCRLayout::Automatic)));
    ui->layoutComboBox->setCurrentIndex(qMax(0, layoutIndex));
    ui->overrideLayoutComboBox->setCurrentIndex(qMax(0, ui->overrideLayoutComboBox->findData(currentOverride)));
    m_updatingUi = wasUpdating;
}

void PDFOCRDocumentDialog::updateLanguageList(const QStringList& selectedLanguages)
{
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;

    const QString engineId = ui->engineComboBox->currentData().toString().isEmpty() ? QStringLiteral("tesseract") : ui->engineComboBox->currentData().toString();
    const pdf::PDFOCRModelProfile profile = pdf::PDFOCRModelProfile(ui->profileComboBox->currentData().toInt());
    const std::vector<pdf::PDFOCRModelInfo> models = m_modelManager->getUsableLanguageModels(engineId, profile, false);

    ui->languagesListWidget->clear();

    // An engine without language models managed by the application has a single fixed entry;
    // the controls, which are not supported by the engine, are disabled with an explanation (REC-01)
    std::shared_ptr<pdf::PDFOCREngineFactory> factory = pdf::PDFOCREngineRegistry::getInstance()->getFactory(engineId);
    const bool usesManagedModels = !factory || factory->usesManagedModels();
    ui->languagesListWidget->setEnabled(usesManagedModels);
    ui->profileComboBox->setEnabled(usesManagedModels);
    ui->manageLanguagesButton->setEnabled(true);
    if (!usesManagedModels)
    {
        QListWidgetItem* item = new QListWidgetItem(tr("Languages are determined by the engine"), ui->languagesListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(ROLE_LANGUAGE, QStringLiteral("default"));
        ui->languageInfoLabel->setText(tr("The selected engine does not use the language models managed by the application."));
        ui->languageInfoLabel->setVisible(true);
        m_updatingUi = wasUpdating;
        return;
    }

    auto addItem = [this](const pdf::PDFOCRModelInfo& model, bool checked)
    {
        QListWidgetItem* item = new QListWidgetItem(pdf::PDFOCRModelManager::getModelDisplayText(model), ui->languagesListWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        item->setData(ROLE_LANGUAGE, model.language);
        item->setToolTip(QDir::toNativeSeparators(model.path));
    };

    // Selected languages first, in their order
    QStringList missing;
    for (const QString& language : selectedLanguages)
    {
        auto it = std::find_if(models.begin(), models.end(), [&language](const pdf::PDFOCRModelInfo& model) { return model.language == language; });
        if (it != models.end())
        {
            addItem(*it, true);
        }
        else
        {
            missing << language;
        }
    }

    for (const pdf::PDFOCRModelInfo& model : models)
    {
        if (!selectedLanguages.contains(model.language))
        {
            addItem(model, false);
        }
    }

    QString info;
    if (!missing.isEmpty())
    {
        info = tr("Not installed for this profile: %1. The languages are not replaced by another model; download them in the language manager.").arg(missing.join(QStringLiteral(", ")));
    }
    else if (models.empty())
    {
        info = tr("No language model is installed for this profile.");
    }
    ui->languageInfoLabel->setText(info);
    ui->languageInfoLabel->setVisible(!info.isEmpty());

    m_updatingUi = wasUpdating;
}

QStringList PDFOCRDocumentDialog::getSelectedLanguages() const
{
    QStringList languages;
    for (int i = 0; i < ui->languagesListWidget->count(); ++i)
    {
        const QListWidgetItem* item = ui->languagesListWidget->item(i);
        if (item->checkState() == Qt::Checked)
        {
            languages << item->data(ROLE_LANGUAGE).toString();
        }
    }
    return languages;
}

void PDFOCRDocumentDialog::updateMemoryEstimate()
{
    // Estimate of the pixels and the memory before the run (IMAGE-01)
    if (m_currentPage < 0)
    {
        ui->memoryEstimateLabel->clear();
        return;
    }

    const pdf::PDFPage* page = m_context.document->getCatalog()->getPage(size_t(m_currentPage));
    const double requestedDpi = m_session->getEffectiveConfiguration(m_currentPage).dpi;
    const double usedDpi = pdf::PDFOCRPagePreparer::getLimitedDpi(page, requestedDpi, pdf::PDFOCRPagePreparer::DefaultMaximumPixels, getMaximumImageDimension(m_engineCapabilities));
    const QSize size = pdf::PDFOCRPagePreparer::getRasterSize(page, usedDpi);
    const double megabytes = double(pdf::PDFOCRPagePreparer::estimateRasterBytes(page, usedDpi)) / (1024.0 * 1024.0);

    QString text = tr("Current page: %1 x %2 pixels (%3 MP), about %4 MB of memory.").arg(size.width()).arg(size.height()).arg(double(size.width()) * size.height() / 1.0e6, 0, 'f', 1).arg(megabytes, 0, 'f', 0);
    if (usedDpi + 0.5 < requestedDpi)
    {
        text += QChar(' ') + tr("The page is too large for %1 DPI; %2 DPI would be used. Select a lower resolution or recognize smaller regions.").arg(qRound(requestedDpi)).arg(qRound(usedDpi));
    }
    ui->memoryEstimateLabel->setText(text);
}

void PDFOCRDocumentDialog::updateEngineCapabilities()
{
    m_engineCapabilities = pdf::PDFOCREngineCapabilities();
    if (std::shared_ptr<pdf::PDFOCREngineFactory> factory = pdf::PDFOCREngineRegistry::getInstance()->getFactory(ui->engineComboBox->currentData().toString()))
    {
        m_engineCapabilities = factory->getCapabilities();
    }
}

int PDFOCRDocumentDialog::getMaximumImageDimension(const pdf::PDFOCREngineCapabilities& capabilities)
{
    return capabilities.maximumImageSize.isEmpty() ? 0 : qMin(capabilities.maximumImageSize.width(), capabilities.maximumImageSize.height());
}

bool PDFOCRDocumentDialog::isExportOnlyEngine(const QString& engineId)
{
    std::shared_ptr<pdf::PDFOCREngineFactory> factory = engineId.isEmpty() ? nullptr : pdf::PDFOCREngineRegistry::getInstance()->getFactory(engineId);
    return factory && factory->getCapabilities().isExportOnly;
}

void PDFOCRDocumentDialog::updatePolicySummary()
{
    // Numbers of pages to process, to skip and to decide manually (INPUT-04)
    const pdf::PDFOCRExistingTextPolicy policy = pdf::PDFOCRExistingTextPolicy(ui->existingTextPolicyComboBox->currentData().toInt());
    int recognize = 0;
    int skip = 0;
    int decide = 0;
    int unknown = 0;

    for (pdf::PDFInteger page : getCheckedPages())
    {
        auto it = m_analysis.find(page);
        if (it == m_analysis.end())
        {
            ++unknown;
            continue;
        }

        bool hasInclusiveRegions = false;
        if (const pdf::PDFOCRPageResult* result = m_session->getPage(page))
        {
            hasInclusiveRegions = std::any_of(result->regions.begin(), result->regions.end(), [](const pdf::PDFOCRRegion& region) { return region.type == pdf::PDFOCRRegionType::Recognize; });
        }

        switch (pdf::PDFOCRPagePreparer::evaluateExistingTextPolicy(it->second, policy, hasInclusiveRegions, nullptr))
        {
            case pdf::PDFOCRPagePreparer::PolicyDecision::Recognize:
                ++recognize;
                break;
            case pdf::PDFOCRPagePreparer::PolicyDecision::Skip:
                ++skip;
                break;
            case pdf::PDFOCRPagePreparer::PolicyDecision::NeedsDecision:
                ++decide;
                break;
        }
    }

    QString text = tr("Checked pages: %1 to recognize, %2 to skip, %3 to decide manually.").arg(recognize).arg(skip).arg(decide);
    if (unknown > 0)
    {
        text += QChar(' ') + tr("%n page(s) are not analyzed yet.", nullptr, unknown);
    }
    if (policy == pdf::PDFOCRExistingTextPolicy::ReviewOnly)
    {
        text += QChar(' ') + tr("Results of this mode are never written into the PDF.");
    }
    ui->policySummaryLabel->setText(text);
}

void PDFOCRDocumentDialog::updateProfiles()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    const QJsonArray profiles = QJsonDocument::fromJson(settings.value(QStringLiteral("profiles")).toByteArray()).array();
    settings.endGroup();

    ui->profilesComboBox->clear();
    for (const QJsonValue& value : profiles)
    {
        const pdf::PDFOCRProfile profile = pdf::PDFOCRProfile::fromJson(value.toObject());
        ui->profilesComboBox->addItem(profile.name, QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }

    ui->loadProfileButton->setEnabled(ui->profilesComboBox->count() > 0);
    ui->deleteProfileButton->setEnabled(ui->profilesComboBox->count() > 0);
}

void PDFOCRDocumentDialog::onSaveProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Profile"), tr("Name of the profile:"), QLineEdit::Normal, ui->profilesComboBox->currentText(), &ok).trimmed();
    if (!ok || name.isEmpty())
    {
        return;
    }

    pdf::PDFOCRProfile profile;
    profile.name = name;
    profile.configuration = getConfigurationFromUi();

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    QJsonArray profiles = QJsonDocument::fromJson(settings.value(QStringLiteral("profiles")).toByteArray()).array();
    for (int i = profiles.size() - 1; i >= 0; --i)
    {
        if (profiles[i].toObject().value(QStringLiteral("name")).toString() == name)
        {
            profiles.removeAt(i);
        }
    }
    profiles.append(profile.toJson());
    settings.setValue(QStringLiteral("profiles"), QJsonDocument(profiles).toJson(QJsonDocument::Compact));
    settings.endGroup();

    updateProfiles();
    ui->profilesComboBox->setCurrentIndex(ui->profilesComboBox->findText(name));
}

void PDFOCRDocumentDialog::onLoadProfile()
{
    const QByteArray profileData = ui->profilesComboBox->currentData().toByteArray();
    if (profileData.isEmpty())
    {
        return;
    }

    const pdf::PDFOCRProfile profile = pdf::PDFOCRProfile::fromJson(QJsonDocument::fromJson(profileData).object());

    // A profile of another engine is never loaded with silently ignored options (REC-02)
    if (ui->engineComboBox->findData(profile.configuration.engineId) == -1)
    {
        QMessageBox::warning(this, windowTitle(), tr("The profile '%1' belongs to the engine '%2', which is not available.").arg(profile.name, profile.configuration.engineId));
        return;
    }

    m_session->setConfiguration(profile.configuration);
    setConfigurationToUi(profile.configuration);

    const QStringList missing = m_modelManager->getMissingModels(profile.configuration.engineId, profile.configuration.languages, profile.configuration.profile);
    if (!missing.isEmpty())
    {
        QMessageBox::information(this, windowTitle(), tr("The profile requires language models, which are not installed:\n\n%1").arg(missing.join(QChar('\n'))));
    }
    schedulePreview();
}

void PDFOCRDocumentDialog::onDeleteProfile()
{
    const QString name = ui->profilesComboBox->currentText();
    if (name.isEmpty() || QMessageBox::question(this, windowTitle(), tr("Do you want to delete the profile '%1'?").arg(name)) != QMessageBox::Yes)
    {
        return;
    }

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup(getSettingsGroup());
    QJsonArray profiles = QJsonDocument::fromJson(settings.value(QStringLiteral("profiles")).toByteArray()).array();
    for (int i = profiles.size() - 1; i >= 0; --i)
    {
        if (profiles[i].toObject().value(QStringLiteral("name")).toString() == name)
        {
            profiles.removeAt(i);
        }
    }
    settings.setValue(QStringLiteral("profiles"), QJsonDocument(profiles).toJson(QJsonDocument::Compact));
    settings.endGroup();
    updateProfiles();
}

void PDFOCRDocumentDialog::onManageLanguages()
{
    PDFOCRLanguagesDialog dialog(m_modelManager, this);
    dialog.exec();
    updateLanguageList(getSelectedLanguages());
    updateUi();
}

void PDFOCRDocumentDialog::onPageOverrideChanged()
{
    if (m_updatingUi || m_currentPage < 0)
    {
        return;
    }

    const std::optional<pdf::PDFOCRPageOverride> previousOverride = m_session->getPageOverride(m_currentPage);
    const std::optional<pdf::PDFOCRQuad> perspective = previousOverride ? previousOverride->perspective : std::nullopt;

    if (!ui->pageOverrideGroupBox->isChecked())
    {
        pdf::PDFOCRPageOverride pageOverride;
        pageOverride.perspective = perspective;
        m_session->setPageOverride(m_currentPage, pageOverride);
    }
    else
    {
        pdf::PDFOCRPageOverride pageOverride;
        pageOverride.perspective = perspective;
        const QStringList languages = ui->overrideLanguagesEdit->text().split(QChar('+'), Qt::SkipEmptyParts);
        if (!languages.isEmpty())
        {
            pageOverride.languages = languages;
        }
        if (ui->overrideLayoutComboBox->currentData().toInt() >= 0)
        {
            pageOverride.layout = pdf::PDFOCRLayout(ui->overrideLayoutComboBox->currentData().toInt());
        }
        if (ui->overrideRotationComboBox->currentData().toInt() >= 0)
        {
            pageOverride.rotation = ui->overrideRotationComboBox->currentData().toInt();
        }
        m_session->setPageOverride(m_currentPage, pageOverride);
    }

    updatePageItem(m_currentPage);
    schedulePreview();
}

pdf::PDFOCRQuad PDFOCRDocumentDialog::getPageCorners(pdf::PDFInteger pageIndex) const
{
    // Corners of the visible page (bottom-left, bottom-right, top-right, top-left as seen)
    const pdf::PDFPage* page = m_context.document->getCatalog()->getPage(pageIndex);
    const QSizeF size = page->getRotatedCropBox().size();
    const QTransform rasterToPage = pdf::PDFOCRPagePreparer::getPageToRasterMatrix(page, QSize(qMax(1, qRound(size.width())), qMax(1, qRound(size.height())))).inverted();
    pdf::PDFOCRQuad quad;
    quad.points[0] = rasterToPage.map(QPointF(0, qRound(size.height())));
    quad.points[1] = rasterToPage.map(QPointF(qRound(size.width()), qRound(size.height())));
    quad.points[2] = rasterToPage.map(QPointF(qRound(size.width()), 0));
    quad.points[3] = rasterToPage.map(QPointF(0, 0));
    return quad;
}

void PDFOCRDocumentDialog::setPagePerspective(pdf::PDFInteger pageIndex, const std::optional<pdf::PDFOCRQuad>& perspective)
{
    std::optional<pdf::PDFOCRPageOverride> pageOverride = m_session->getPageOverride(pageIndex);
    pdf::PDFOCRPageOverride newOverride = pageOverride.value_or(pdf::PDFOCRPageOverride());
    newOverride.perspective = perspective;
    if (newOverride.isEmpty())
    {
        m_session->clearPageOverride(pageIndex);
    }
    else
    {
        m_session->setPageOverride(pageIndex, newOverride);
    }

    updatePageItem(pageIndex);
    if (pageIndex == m_currentPage)
    {
        updatePageOverrideUi();
        schedulePreview();
    }
}

void PDFOCRDocumentDialog::startPerspectiveEditing()
{
    if (m_currentPage < 0 || !m_originalView->hasImage())
    {
        return;
    }

    // The corners are placed in the original view (the photo as it is)
    if (ui->perspectivePreviewCheckBox->isChecked())
    {
        ui->perspectivePreviewCheckBox->setChecked(false);
    }
    setViewMode(ViewMode::SideBySide);
    m_originalView->setMode(PDFOCRPageView::Mode::EditPerspective);
    m_originalView->setFocus();
}

void PDFOCRDocumentDialog::updatePageOverrideUi()
{
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;

    const std::optional<pdf::PDFOCRPageOverride> pageOverride = m_currentPage >= 0 ? m_session->getPageOverride(m_currentPage) : std::nullopt;
    ui->pageOverrideGroupBox->setChecked(pageOverride.has_value() && (pageOverride->languages || pageOverride->layout || pageOverride->rotation || pageOverride->dpi || pageOverride->autoOrientation || pageOverride->deskew));

    // Perspective correction of the page
    const bool hasPerspective = pageOverride && pageOverride->perspective;
    ui->perspectiveCheckBox->setChecked(hasPerspective);
    ui->perspectiveEditButton->setEnabled(m_currentPage >= 0);
    ui->perspectiveResetButton->setEnabled(hasPerspective);
    ui->perspectiveCopyButton->setEnabled(hasPerspective);
    m_originalView->setPerspective(hasPerspective ? pageOverride->perspective : std::nullopt);
    if (hasPerspective)
    {
        const QString error = pdf::PDFOCRPagePreparer::validatePerspective(*pageOverride->perspective, m_context.document->getCatalog()->getPage(m_currentPage)->getCropBox());
        ui->perspectiveInfoLabel->setText(error.isEmpty() ? tr("The working image is corrected to the rectangle of the document; deskew is not applied.") : error);
    }
    else
    {
        ui->perspectiveInfoLabel->clear();
    }
    ui->overrideLanguagesEdit->setText(pageOverride && pageOverride->languages ? pageOverride->languages->join(QChar('+')) : QString());
    ui->overrideLayoutComboBox->setCurrentIndex(qMax(0, ui->overrideLayoutComboBox->findData(pageOverride && pageOverride->layout ? int(*pageOverride->layout) : -1)));
    ui->overrideRotationComboBox->setCurrentIndex(qMax(0, ui->overrideRotationComboBox->findData(pageOverride && pageOverride->rotation ? *pageOverride->rotation : -1)));

    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    ui->overrideBlankCheckBox->setChecked(page && page->blankDetectionOverridden);

    m_updatingUi = wasUpdating;
}

// -------------------------------------------------------------------------
// Pages
// -------------------------------------------------------------------------

std::vector<pdf::PDFInteger> PDFOCRDocumentDialog::getCheckedPages() const
{
    std::vector<pdf::PDFInteger> pages;
    for (int i = 0; i < ui->pagesListWidget->count(); ++i)
    {
        if (ui->pagesListWidget->item(i)->checkState() == Qt::Checked)
        {
            pages.push_back(i);
        }
    }
    return pages;
}

void PDFOCRDocumentDialog::setCheckedPages(const std::vector<pdf::PDFInteger>& pages)
{
    const std::set<pdf::PDFInteger> pageSet(pages.begin(), pages.end());
    m_updatingUi = true;
    for (int i = 0; i < ui->pagesListWidget->count(); ++i)
    {
        ui->pagesListWidget->item(i)->setCheckState(pageSet.count(i) ? Qt::Checked : Qt::Unchecked);
    }
    m_updatingUi = false;
    updateSelectionInfo();
    updateUi();
}

void PDFOCRDocumentDialog::onApplyPageSelection()
{
    std::vector<pdf::PDFInteger> pages;
    QString errorMessage;
    const pdf::PDFOCRPageSelection::Parity parity = pdf::PDFOCRPageSelection::Parity(ui->parityComboBox->currentData().toInt());

    if (ui->customRangeRadioButton->isChecked())
    {
        pages = pdf::PDFOCRPageSelection::parseRange(m_pageCount, ui->customRangeEdit->text(), parity, &errorMessage);
    }
    else
    {
        if (ui->currentPageRadioButton->isChecked() && !m_context.visiblePages.empty())
        {
            pages = { m_context.visiblePages.front() };
        }
        else if (ui->visiblePagesRadioButton->isChecked())
        {
            pages = m_context.visiblePages;
        }
        else if (ui->editorSelectionRadioButton->isChecked())
        {
            pages = m_context.selectedPages;
        }
        else
        {
            pages.resize(size_t(m_pageCount));
            std::iota(pages.begin(), pages.end(), 0);
        }

        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
        pages = pdf::PDFOCRPageSelection::filterParity(std::move(pages), parity);
        if (pages.empty())
        {
            errorMessage = tr("Selected page range is empty.");
        }
    }

    ui->rangeErrorLabel->setText(errorMessage);
    ui->rangeErrorLabel->setVisible(!errorMessage.isEmpty());

    if (errorMessage.isEmpty())
    {
        setCheckedPages(pages);
    }
}

void PDFOCRDocumentDialog::onApplyHelperSelection()
{
    std::vector<pdf::PDFInteger> pages;
    const int helper = ui->helperSelectionComboBox->currentData().toInt();
    int notAnalyzed = 0;

    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        const pdf::PDFOCRPageResult* result = m_session->getPage(page);
        switch (helper)
        {
            case HelperWithoutText:
            case HelperOwnLayer:
            {
                auto it = m_analysis.find(page);
                if (it == m_analysis.end())
                {
                    ++notAnalyzed;
                    break;
                }
                const pdf::PDFOCRPageAnalysis& analysis = it->second;
                const bool withoutText = !analysis.hasOwnOCRLayer && (analysis.contentClass == pdf::PDFOCRPageContentClass::Image || analysis.contentClass == pdf::PDFOCRPageContentClass::Empty);
                if ((helper == HelperWithoutText && withoutText) || (helper == HelperOwnLayer && analysis.hasOwnOCRLayer))
                {
                    pages.push_back(page);
                }
                break;
            }

            case HelperErrors:
                if (result && (result->state == pdf::PDFOCRPageState::Error || result->state == pdf::PDFOCRPageState::Cancelled))
                {
                    pages.push_back(page);
                }
                break;

            case HelperWaitingForReview:
                if (result && result->hasResult() && !m_session->getReviewWords(page).empty())
                {
                    pages.push_back(page);
                }
                break;
        }
    }

    setCheckedPages(pages);

    QString info = tr("%n page(s) selected.", nullptr, int(pages.size()));
    if (helper == HelperWithoutText || helper == HelperOwnLayer)
    {
        info += QChar(' ') + tr("The selection is based on a heuristic analysis of the pages; check it and change it manually, if needed.");
        if (notAnalyzed > 0)
        {
            info += QChar(' ') + tr("%n page(s) are not analyzed yet.", nullptr, notAnalyzed);
        }
    }
    ui->selectionInfoLabel->setText(info);
}

QString PDFOCRDocumentDialog::getPageStateName(pdf::PDFOCRPageState state)
{
    switch (state)
    {
        case pdf::PDFOCRPageState::Pending:
            return tr("Waiting");
        case pdf::PDFOCRPageState::Preparing:
            return tr("Preparing");
        case pdf::PDFOCRPageState::Recognizing:
            return tr("Recognizing");
        case pdf::PDFOCRPageState::Done:
            return tr("Done");
        case pdf::PDFOCRPageState::NoText:
            return tr("No text found");
        case pdf::PDFOCRPageState::Skipped:
            return tr("Skipped");
        case pdf::PDFOCRPageState::Error:
            return tr("Error");
        case pdf::PDFOCRPageState::Cancelled:
            return tr("Cancelled");
        case pdf::PDFOCRPageState::Stale:
            return tr("Outdated");
    }
    return QString();
}

static QString getContentClassName(pdf::PDFOCRPageContentClass contentClass)
{
    switch (contentClass)
    {
        case pdf::PDFOCRPageContentClass::Unknown:
            return PDFOCRDocumentDialog::tr("not analyzed");
        case pdf::PDFOCRPageContentClass::Image:
            return PDFOCRDocumentDialog::tr("image page without text");
        case pdf::PDFOCRPageContentClass::VisibleText:
            return PDFOCRDocumentDialog::tr("page with visible text");
        case pdf::PDFOCRPageContentClass::InvisibleText:
            return PDFOCRDocumentDialog::tr("page with invisible text");
        case pdf::PDFOCRPageContentClass::Mixed:
            return PDFOCRDocumentDialog::tr("mixed page (text and images)");
        case pdf::PDFOCRPageContentClass::Empty:
            return PDFOCRDocumentDialog::tr("empty page");
        case pdf::PDFOCRPageContentClass::Ambiguous:
            return PDFOCRDocumentDialog::tr("ambiguous content");
    }
    return QString();
}

void PDFOCRDocumentDialog::updatePageItem(pdf::PDFInteger pageIndex)
{
    QListWidgetItem* item = ui->pagesListWidget->item(int(pageIndex));
    if (!item)
    {
        return;
    }

    const pdf::PDFOCRPageResult* result = m_session->getPage(pageIndex);

    QString label = pdf::PDFOCRPagePreparer::getPageLabel(m_context.document, pageIndex);
    QString text = tr("Page %1").arg(pageIndex + 1);
    if (!label.isEmpty() && label != QString::number(pageIndex + 1))
    {
        text += tr(" (label %1)").arg(label);
    }

    QStringList lines;
    QStringList toolTip;

    auto analysisIt = m_analysis.find(pageIndex);
    if (analysisIt != m_analysis.end())
    {
        const pdf::PDFOCRPageAnalysis& analysis = analysisIt->second;
        QString analysisText = getContentClassName(analysis.contentClass);
        if (analysis.hasOwnOCRLayer)
        {
            analysisText += tr(", own OCR layer");
        }
        toolTip << tr("Content: %1").arg(analysisText);
        toolTip << analysis.ambiguityReasons << analysis.notes;
        lines << analysisText;
    }

    if (result && (result->state != pdf::PDFOCRPageState::Pending || result->hasResult()))
    {
        QString stateText = getPageStateName(result->state);
        if (result->hasResult())
        {
            const int reviewCount = int(m_session->getReviewWords(pageIndex).size());
            stateText += tr(": %n word(s)", nullptr, result->getWordCount());
            if (reviewCount > 0)
            {
                stateText += tr(", %n to review", nullptr, reviewCount);
            }
            if (result->isModified)
            {
                stateText += tr(", corrected");
            }
        }
        lines << stateText;

        if (result->error)
        {
            toolTip << tr("Error [%1] in step '%2': %3").arg(pdf::PDFOCRError::getCodeIdentifier(result->error.code), result->error.step, result->error.message);
            if (!result->error.detail.isEmpty())
            {
                toolTip << result->error.detail;
            }
        }
        if (!result->skipReason.isEmpty())
        {
            toolTip << result->skipReason;
        }

        // Report of the page: time, models, actual resolution and pipeline (JOB-04)
        if (result->hasResult() && result->geometry.dpi > 0.0)
        {
            toolTip << tr("Recognized in %1 s at %2 DPI (requested %3 DPI), image %4 x %5 pixels.")
                       .arg(double(result->elapsedMilliseconds) / 1000.0, 0, 'f', 1).arg(qRound(result->geometry.dpi)).arg(qRound(result->geometry.requestedDpi))
                       .arg(result->geometry.engineImageSize.width()).arg(result->geometry.engineImageSize.height());
            toolTip << tr("Engine: %1 %2; models: %3").arg(result->provenance.engineId, result->provenance.engineVersion, result->provenance.modelIds.join(QStringLiteral(", ")));
            toolTip << tr("Pipeline: %1").arg(result->geometry.pipeline.join(QStringLiteral(" > ")));
            if (result->orientation)
            {
                toolTip << tr("Detected orientation: rotation %1 degrees, confidence indicator %2/100.").arg(result->orientation->rotation).arg(qRound(result->orientation->confidence.value_or(0.0)));
            }
        }
    }

    if (const std::optional<pdf::PDFOCRPageOverride> pageOverride = m_session->getPageOverride(pageIndex))
    {
        if (pageOverride->perspective)
        {
            lines << tr("Perspective corrected ⬚");
        }
        if (pageOverride->languages || pageOverride->layout || pageOverride->rotation || pageOverride->dpi || pageOverride->autoOrientation || pageOverride->deskew)
        {
            lines << tr("Different settings *");
        }
    }
    if (result && result->reviewOnly)
    {
        lines << tr("Review/export only");
    }
    if (m_maskedTextPages.count(pageIndex))
    {
        toolTip << tr("Existing text masked: the digital text of the page was not recognized again and is kept.");
    }
    if (result && !result->regions.empty())
    {
        lines << tr("%n region(s)", nullptr, int(result->regions.size()));
    }

    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;
    item->setText(text + QChar('\n') + lines.join(QChar('\n')));
    item->setToolTip(toolTip.join(QChar('\n')));
    m_updatingUi = wasUpdating;
}

void PDFOCRDocumentDialog::updateSelectionInfo()
{
    // Count, range and estimate of the work (PAGE-04)
    const std::vector<pdf::PDFInteger> pages = getCheckedPages();
    if (pages.empty())
    {
        ui->selectionInfoLabel->setText(tr("No page is checked."));
    }
    else
    {
        qint64 pixels = 0;
        const double dpi = m_session->getConfiguration().dpi;
        for (pdf::PDFInteger page : pages)
        {
            const QSize size = pdf::PDFOCRPagePreparer::getRasterSize(m_context.document->getCatalog()->getPage(size_t(page)), dpi);
            pixels += qint64(size.width()) * size.height();
        }
        ui->selectionInfoLabel->setText(tr("%n page(s) checked: %1. Work estimate: %2 megapixels at %3 DPI.", nullptr, int(pages.size()))
                                        .arg(pdf::PDFOCRPageSelection::describe(pages)).arg(double(pixels) / 1.0e6, 0, 'f', 0).arg(qRound(dpi)));
    }

    updatePolicySummary();
}

const pdf::PDFOCRPageAnalysis* PDFOCRDocumentDialog::getAnalysis(pdf::PDFInteger pageIndex)
{
    auto it = m_analysis.find(pageIndex);
    if (it != m_analysis.end())
    {
        return &it->second;
    }
    return nullptr;
}

void PDFOCRDocumentDialog::onCurrentPageChanged()
{
    const pdf::PDFInteger page = ui->pagesListWidget->currentRow();
    const bool pageChanged = page != m_currentPage;
    m_currentPage = page;

    if (pageChanged)
    {
        m_selectedWordId = 0;
        m_selectedLineId = 0;
        m_selectedBlockId = 0;
        m_originalView->clearImage(tr("Loading..."));
        m_workingView->clearImage(tr("Loading..."));
        schedulePreview();
    }

    updatePageOverrideUi();
    updateMemoryEstimate();
    updateViews();
    updateResultsTree();
    updateUi();

    // Page information
    QStringList info;
    if (const pdf::PDFOCRPageAnalysis* analysis = getAnalysis(m_currentPage))
    {
        info << tr("Content: %1.").arg(getContentClassName(analysis->contentClass));
        info << analysis->ambiguityReasons << analysis->notes;
    }
    if (const pdf::PDFOCRPageResult* result = m_session->getPage(m_currentPage))
    {
        if (result->error)
        {
            info << tr("Error in step '%1': %2").arg(result->error.step, result->error.message);
        }
        if (!result->skipReason.isEmpty())
        {
            info << result->skipReason;
        }
        if (result->state == pdf::PDFOCRPageState::NoText && m_analysis.count(m_currentPage) && m_analysis[m_currentPage].hasImages)
        {
            info << tr("No text was found, although the page is not empty. Try a different resolution, language or layout, or disable the blank page detection.");
        }
        if (result->hasResult() && result->geometry.dpi > 0.0)
        {
            info << tr("Recognized at %1 DPI in %2 s.").arg(qRound(result->geometry.dpi)).arg(double(result->elapsedMilliseconds) / 1000.0, 0, 'f', 1);
        }

        // A skewed page is straightened only for the recognition; the visible page is
        // straightened by the preparation of the scanned pages (phase 4 of OCR_PLAN.md)
        std::optional<double> skew;
        static const QRegularExpression deskewExpression(QStringLiteral("^deskew\\((?:skipped,)?angle=(-?[0-9.]+)"));
        for (const QString& step : result->geometry.pipeline)
        {
            const QRegularExpressionMatch match = deskewExpression.match(step);
            if (match.hasMatch())
            {
                skew = match.captured(1).toDouble();
            }
        }
        if (!skew && result->orientation && !qFuzzyIsNull(result->orientation->deskewAngle))
        {
            skew = result->orientation->deskewAngle;
        }
        if (skew && std::abs(*skew) >= 0.5)
        {
            info << tr("The page is skewed by %1°. Straightening in OCR does not change the visible page; use Tools > Prepare Scanned Pages before the recognition to straighten it permanently.").arg(*skew, 0, 'f', 1);
        }
    }
    ui->pageInfoLabel->setText(info.join(QChar(' ')));
}

// -------------------------------------------------------------------------
// Recognition
// -------------------------------------------------------------------------

void PDFOCRDocumentDialog::onRecognizeClicked()
{
    startRecognition(getCheckedPages(), RunMode::Pages);
}

void PDFOCRDocumentDialog::onStopClicked()
{
    if (m_pendingRecognition)
    {
        // The job was not started yet, only its preparation is cancelled
        cancelRecognitionPreparation();
        return;
    }

    if (m_jobController->isRunning())
    {
        // The state is displayed immediately, the workers finish cooperatively (JOB-06)
        ui->progressLabel->setText(tr("Stopping..."));
        m_jobController->stop();
        updateUi();
    }
}

bool PDFOCRDocumentDialog::startRecognition(const std::vector<pdf::PDFInteger>& pages, RunMode runMode)
{
    if (m_jobController->isRunning() || m_pendingRecognition || pages.empty())
    {
        return false;
    }

    pdf::PDFOCRConfiguration configuration = getConfigurationFromUi();
    m_session->setConfiguration(configuration);

    // Validation of the configuration (REC-03)
    QStringList errors = configuration.validate();
    std::shared_ptr<pdf::PDFOCREngineFactory> factory = pdf::PDFOCREngineRegistry::getInstance()->getFactory(configuration.engineId);
    QString reason;
    pdf::PDFOCREngineCapabilities capabilities;
    if (!factory || !factory->isAvailable(&reason))
    {
        errors << tr("OCR engine is not available. %1").arg(reason);
    }
    else
    {
        // Engine parameters are checked against the typed schema of the engine (REC-03, ARCH-02)
        capabilities = factory->getCapabilities();
        QStringList parameterErrors;
        pdf::PDFOCRConfiguration::validateEngineParameters(configuration.engineParameters, capabilities.parameters, &parameterErrors);
        errors << parameterErrors;
    }

    if (!errors.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), tr("Recognition cannot be started:\n\n%1").arg(errors.join(QChar('\n'))));
        return false;
    }

    // All languages of the run (common settings and page exceptions) are resolved into a single model set
    QStringList languages = configuration.languages;
    for (pdf::PDFInteger page : pages)
    {
        for (const QString& language : m_session->getEffectiveConfiguration(page).languages)
        {
            if (!languages.contains(language))
            {
                languages << language;
            }
        }
        if (const pdf::PDFOCRPageResult* result = m_session->getPage(page))
        {
            for (const pdf::PDFOCRRegion& region : result->regions)
            {
                for (const QString& language : region.configuration.languages)
                {
                    if (!languages.contains(language))
                    {
                        languages << language;
                    }
                }
            }
        }
    }

    // Policy of the existing text, evaluated for every page (INPUT-04)
    std::vector<pdf::PDFInteger> pagesToRecognize;
    std::vector<std::pair<pdf::PDFInteger, QString>> pagesToDecide;
    std::vector<std::pair<pdf::PDFInteger, QString>> pagesToSkip;
    QStringList regionConflicts;
    std::set<pdf::PDFInteger> reviewOnlyPages;
    std::set<pdf::PDFInteger> maskedPages;

    if (runMode == RunMode::Pages)
    {
        // Pages not analyzed by the background task yet are analyzed outside of the GUI thread (R12)
        std::vector<pdf::PDFInteger> pagesToAnalyze;
        std::copy_if(pages.begin(), pages.end(), std::back_inserter(pagesToAnalyze), [this](pdf::PDFInteger page) { return !m_analysis.count(page); });
        if (!pagesToAnalyze.empty())
        {
            std::map<pdf::PDFInteger, pdf::PDFOCRPageAnalysis> analyses;
            const pdf::PDFDocument* document = m_context.document;
            const pdf::PDFFontCache* fontCache = m_context.proxy->getFontCache();
            const pdf::PDFCMS* cms = m_context.cms;
            const pdf::PDFOptionalContentActivity* activity = m_optionalContentActivity;
            const pdf::PDFMeshQualitySettings& meshQualitySettings = m_meshQualitySettings;
            const pdf::RendererEngine rendererEngine = m_context.proxy->getRendererEngine();
            runBlockingTask(tr("Analyzing the pages..."), [&](const pdf::PDFOperationControl* operationControl)
            {
                pdf::PDFOCRPagePreparer preparer(document, fontCache, cms, activity, meshQualitySettings, rendererEngine);
                for (pdf::PDFInteger page : pagesToAnalyze)
                {
                    analyses[page] = preparer.analyze(page, operationControl);
                }
            });

            for (auto& item : analyses)
            {
                if (!m_analysis.count(item.first))
                {
                    m_analysis[item.first] = std::move(item.second);
                }
            }
        }

        for (pdf::PDFInteger page : pages)
        {
            const pdf::PDFOCRPageResult* result = m_session->getPage(page);
            bool hasInclusiveRegions = false;
            if (result)
            {
                // Overlapping inclusive regions with a different configuration must be resolved by the user (REGION-02)
                for (size_t i = 0; i < result->regions.size(); ++i)
                {
                    const pdf::PDFOCRRegion& first = result->regions[i];
                    hasInclusiveRegions = hasInclusiveRegions || first.type == pdf::PDFOCRRegionType::Recognize;
                    for (size_t j = i + 1; j < result->regions.size(); ++j)
                    {
                        const pdf::PDFOCRRegion& second = result->regions[j];
                        if (first.type == pdf::PDFOCRRegionType::Recognize && second.type == pdf::PDFOCRRegionType::Recognize &&
                            first.rect.intersects(second.rect) && !(first.configuration == second.configuration))
                        {
                            regionConflicts << tr("Page %1: overlapping regions with different settings.").arg(page + 1);
                        }
                    }
                }
            }

            // Inclusive regions over the existing text are collisions, which the user must
            // resolve before the run (chapter 6.2, R05)
            QString skipReason;
            switch (pdf::PDFOCRPagePreparer::evaluateExistingTextPolicy(m_analysis[page], configuration.existingTextPolicy, hasInclusiveRegions, &skipReason, result ? &result->regions : nullptr))
            {
                case pdf::PDFOCRPagePreparer::PolicyDecision::Recognize:
                    pagesToRecognize.push_back(page);
                    break;
                case pdf::PDFOCRPagePreparer::PolicyDecision::Skip:
                    pagesToSkip.emplace_back(page, skipReason);
                    break;
                case pdf::PDFOCRPagePreparer::PolicyDecision::NeedsDecision:
                    pagesToDecide.emplace_back(page, skipReason);
                    break;
            }
        }

        if (!regionConflicts.isEmpty())
        {
            QMessageBox::warning(this, windowTitle(), tr("Recognition cannot be started. Resolve the overlapping regions first:\n\n%1").arg(regionConflicts.join(QChar('\n'))));
            return false;
        }

        if (!pagesToSkip.empty() || !pagesToDecide.empty())
        {
            // A scan with a small existing text (page number, stamp) can be recognized with
            // the existing text masked, so the text layer is never written over it (R04).
            // Pages with a region over the existing text are not offered (R05).
            std::vector<pdf::PDFInteger> decidePages;
            std::vector<pdf::PDFInteger> maskablePages;
            QStringList collisionDetails;
            for (const auto& item : pagesToDecide)
            {
                const pdf::PDFInteger page = item.first;
                const pdf::PDFOCRPageAnalysis& analysis = m_analysis[page];
                const pdf::PDFOCRPageResult* result = m_session->getPage(page);
                decidePages.push_back(page);

                if (result && !pdf::PDFOCRPagePreparer::getRegionsCollidingWithText(analysis, result->regions).empty())
                {
                    collisionDetails << tr("Page %1: %2").arg(page + 1).arg(item.second);
                }
                else if (analysis.contentClass == pdf::PDFOCRPageContentClass::Mixed && !analysis.hasVisibleText && !analysis.textRectangles.empty())
                {
                    maskablePages.push_back(page);
                }
            }

            QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("Pages to recognize: %1\nPages to skip because of the existing text: %2\nPages requiring your decision: %3")
                                   .arg(pagesToRecognize.size()).arg(pagesToSkip.size()).arg(pagesToDecide.size()), QMessageBox::NoButton, this);
            QStringList details;
            for (const auto& item : pagesToSkip)
            {
                details << tr("Page %1: %2").arg(item.first + 1).arg(item.second);
            }
            for (const auto& item : pagesToDecide)
            {
                details << tr("Page %1 requires a decision: %2").arg(item.first + 1).arg(item.second);
            }
            if (!collisionDetails.isEmpty())
            {
                details << tr("Pages with a region over the existing text are recognized only for the review; otherwise move the regions:") << collisionDetails;
            }
            if (!maskablePages.empty())
            {
                details << tr("Pages with a small existing text, which can be masked: %1").arg(pdf::PDFOCRPageSelection::describe(maskablePages));
            }
            messageBox.setDetailedText(details.join(QChar('\n')));

            QString informativeText;
            if (pagesToDecide.empty())
            {
                informativeText = tr("Skipped pages are not recognized. Use the mode 'Recognize for review/export only' to recognize them without writing into the PDF.");
            }
            else
            {
                informativeText = tr("Pages requiring a decision contain both text and images, their content is ambiguous, or a region covers the existing text. They can be recognized for review and export only; their results will not be written into the PDF. To add text to such pages, draw the regions outside of the existing text and use the mode 'Add text in the drawn regions'.");
                if (!maskablePages.empty())
                {
                    informativeText += QStringLiteral("\n\n") + tr("Scanned pages with a small existing text (for example a page number) can be recognized with the existing text masked; the text layer is written, the existing text is kept and is not recognized again.");
                }
            }
            messageBox.setInformativeText(informativeText);

            QPushButton* continueButton = messageBox.addButton(tr("Continue"), QMessageBox::AcceptRole);
            QPushButton* reviewButton = pagesToDecide.empty() ? nullptr : messageBox.addButton(tr("Recognize Them for Review Only"), QMessageBox::ActionRole);
            QPushButton* maskButton = maskablePages.empty() ? nullptr : messageBox.addButton(tr("Recognize with Existing Text Masked"), QMessageBox::ActionRole);
            messageBox.addButton(QMessageBox::Cancel);
            messageBox.exec();

            if (messageBox.clickedButton() == reviewButton && reviewButton)
            {
                for (pdf::PDFInteger page : decidePages)
                {
                    pagesToRecognize.push_back(page);
                    reviewOnlyPages.insert(page);
                }
                decidePages.clear();
            }
            else if (messageBox.clickedButton() == maskButton && maskButton)
            {
                for (pdf::PDFInteger page : maskablePages)
                {
                    pagesToRecognize.push_back(page);
                    maskedPages.insert(page);
                    std::erase(decidePages, page);
                }
            }
            else if (messageBox.clickedButton() != continueButton)
            {
                return false;
            }

            for (const auto& item : pagesToDecide)
            {
                if (std::count(decidePages.begin(), decidePages.end(), item.first))
                {
                    pagesToSkip.emplace_back(item.first, tr("Page requires a manual decision (existing text). %1").arg(item.second));
                }
            }
            std::sort(pagesToRecognize.begin(), pagesToRecognize.end());
        }

        if (configuration.existingTextPolicy == pdf::PDFOCRExistingTextPolicy::ReviewOnly)
        {
            reviewOnlyPages.insert(pagesToRecognize.begin(), pagesToRecognize.end());
        }
        else
        {
            for (pdf::PDFInteger page : pagesToRecognize)
            {
                const pdf::PDFOCRPageResult* existing = m_session->getPage(page);
                if (!existing || !existing->reviewOnly || reviewOnlyPages.count(page) || maskedPages.count(page))
                {
                    continue;
                }

                // A page recognized under a writing policy is no more review only,
                // otherwise the flag of the previous result is inherited
                if (pdf::PDFOCRPagePreparer::evaluateExistingTextPolicy(m_analysis[page], configuration.existingTextPolicy, true, nullptr) != pdf::PDFOCRPagePreparer::PolicyDecision::Recognize ||
                    m_analysis[page].hasUsableVisibleText())
                {
                    reviewOnlyPages.insert(page);
                }
            }
        }

        // Manual corrections are never overwritten silently (EDIT-07)
        std::vector<pdf::PDFInteger> correctedPages;
        for (pdf::PDFInteger page : pagesToRecognize)
        {
            if (m_session->hasManualCorrections(page))
            {
                correctedPages.push_back(page);
            }
        }

        m_candidatePages.clear();
        if (!correctedPages.empty())
        {
            QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("Pages %1 contain manual corrections.").arg(pdf::PDFOCRPageSelection::describe(correctedPages)), QMessageBox::NoButton, this);
            messageBox.setInformativeText(tr("The repeated recognition does not overwrite the corrections automatically. You can keep the current results of these pages, or compare the new recognition with the current text after it finishes and decide for every page."));
            QPushButton* keepButton = messageBox.addButton(tr("Keep Corrections (Skip Pages)"), QMessageBox::AcceptRole);
            QPushButton* compareButton = messageBox.addButton(tr("Recognize and Compare"), QMessageBox::ActionRole);
            messageBox.addButton(QMessageBox::Cancel);
            messageBox.exec();

            if (messageBox.clickedButton() == keepButton)
            {
                for (pdf::PDFInteger page : correctedPages)
                {
                    std::erase(pagesToRecognize, page);
                }
            }
            else if (messageBox.clickedButton() == compareButton)
            {
                m_candidatePages.insert(correctedPages.begin(), correctedPages.end());
            }
            else
            {
                return false;
            }
        }
    }
    else
    {
        pagesToRecognize = pages;
    }

    if (pagesToRecognize.empty())
    {
        // Nothing to recognize, the skipped pages are marked immediately
        applySkippedPages(pagesToSkip);
        updateUi();
        return false;
    }

    // The resolution of a page too large for the memory limit or for the image size limit
    // of the engine is reduced only with an explicit consent of the user (IMAGE-01, ARCH-02)
    {
        const int maximumDimension = getMaximumImageDimension(capabilities);
        QStringList limitedPages;
        for (pdf::PDFInteger page : pagesToRecognize)
        {
            const double requestedDpi = m_session->getEffectiveConfiguration(page).dpi;
            const double limitedDpi = pdf::PDFOCRPagePreparer::getLimitedDpi(m_context.document->getCatalog()->getPage(size_t(page)), requestedDpi, pdf::PDFOCRPagePreparer::DefaultMaximumPixels, maximumDimension);
            if (limitedDpi + 0.5 < requestedDpi)
            {
                limitedPages << tr("Page %1: %2 DPI instead of %3 DPI").arg(page + 1).arg(qRound(limitedDpi)).arg(qRound(requestedDpi));
            }
        }

        if (!limitedPages.isEmpty())
        {
            QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("%n page(s) are too large for the requested resolution.", nullptr, int(limitedPages.size())), QMessageBox::NoButton, this);
            messageBox.setInformativeText(tr("The raster would exceed the memory limit or the maximal image size of the engine, so the resolution of these pages would be reduced. Continue with the reduced resolution, or cancel and select a lower resolution or smaller regions."));
            messageBox.setDetailedText(limitedPages.join(QChar('\n')));
            QPushButton* continueButton = messageBox.addButton(tr("Continue with Reduced Resolution"), QMessageBox::AcceptRole);
            messageBox.addButton(QMessageBox::Cancel);
            messageBox.exec();

            if (messageBox.clickedButton() != continueButton)
            {
                updateUi();
                return false;
            }
        }
    }

    // All decisions of the user are made. The language models are resolved and the missing
    // fingerprints of the pages are computed outside of the GUI thread (R12); the job is
    // built by the queued continuation onRecognitionPrepared.
    PendingRecognition pending;
    pending.runMode = runMode;
    pending.configuration = configuration;
    pending.pagesToRecognize = std::move(pagesToRecognize);
    pending.pagesToSkip = std::move(pagesToSkip);
    pending.reviewOnlyPages = std::move(reviewOnlyPages);
    pending.maskedPages = std::move(maskedPages);

    std::vector<pdf::PDFInteger> missingFingerprints;
    std::copy_if(pending.pagesToRecognize.begin(), pending.pagesToRecognize.end(), std::back_inserter(missingFingerprints), [this](pdf::PDFInteger page) { return !m_fingerprints.count(page); });

    const bool usesManagedModels = factory->usesManagedModels();
    const pdf::PDFDocument* document = m_context.document;
    pdf::PDFOCRModelManager* modelManager = m_modelManager;
    const QString engineId = configuration.engineId;
    const pdf::PDFOCRModelProfile profile = configuration.profile;

    m_pendingRecognition = std::move(pending);
    ui->progressBar->setRange(0, 0);
    ui->progressLabel->setText(tr("Preparing the recognition (language models, fingerprints of the pages)..."));

    startTask(m_prepareTask, [this, document, modelManager, engineId, languages, profile, usesManagedModels, missingFingerprints](int generation, const pdf::PDFOperationControl* operationControl)
    {
        PreparedRecognition prepared;

        try
        {
            if (usesManagedModels)
            {
                // The model manager is safe to be used from a worker thread
                prepared.models = modelManager->resolveModelSet(engineId, languages, profile, &prepared.error);
            }
            else
            {
                prepared.models.dataPath = QStringLiteral("-");
                prepared.models.languages = languages;
                prepared.models.profile = profile;
            }

            for (pdf::PDFInteger page : missingFingerprints)
            {
                if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
                {
                    return;
                }
                prepared.fingerprints[page] = pdf::PDFOCRPagePreparer::computePageFingerprint(document, page);
            }
        }
        catch (const pdf::PDFException& exception)
        {
            prepared.error = pdf::PDFOCRError::create(pdf::PDFOCRErrorCode::Unknown, exception.getMessage());
        }
        catch (...)
        {
            // Without the final signal the dialog would stay in the preparation forever
            prepared.error = pdf::PDFOCRError::create(pdf::PDFOCRErrorCode::Unknown, tr("Unexpected error."));
        }

        if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        {
            QMutexLocker lock(&m_prepareMutex);
            m_preparedRecognition = std::move(prepared);
        }
        Q_EMIT recognitionPrepared(generation);
    });

    updateUi();
    return true;
}

void PDFOCRDocumentDialog::applySkippedPages(const std::vector<std::pair<pdf::PDFInteger, QString>>& pages)
{
    for (const auto& item : pages)
    {
        if (m_session->getPage(item.first) && m_session->getPage(item.first)->hasResult())
        {
            // Existing results are not destroyed by skipping
            continue;
        }
        pdf::PDFOCRPageResult skippedResult;
        skippedResult.pageIndex = item.first;
        skippedResult.state = pdf::PDFOCRPageState::Skipped;
        skippedResult.skipReason = item.second;
        skippedResult.error = pdf::PDFOCRError::create(pdf::PDFOCRErrorCode::None, item.second, tr("Existing text policy"));
        skippedResult.analysis = m_analysis[item.first];
        m_session->setPageResult(skippedResult);
    }
}

void PDFOCRDocumentDialog::cancelRecognitionPreparation()
{
    if (!m_pendingRecognition)
    {
        return;
    }

    cancelTask(m_prepareTask);
    m_pendingRecognition.reset();
    m_candidatePages.clear();
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    ui->progressLabel->setText(tr("The recognition was not started."));
    updateUi();
}

void PDFOCRDocumentDialog::onRecognitionPrepared(int generation)
{
    if (generation != m_prepareTask.generation || !m_pendingRecognition)
    {
        // Late result of a cancelled preparation
        return;
    }

    PendingRecognition pending = std::move(*m_pendingRecognition);
    m_pendingRecognition.reset();

    PreparedRecognition prepared;
    {
        QMutexLocker lock(&m_prepareMutex);
        prepared = std::move(m_preparedRecognition);
        m_preparedRecognition = PreparedRecognition();
    }

    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(0);
    ui->progressLabel->clear();

    for (const auto& item : prepared.fingerprints)
    {
        m_fingerprints.emplace(item.first, item.second);
    }

    if (prepared.error)
    {
        m_candidatePages.clear();
        updateUi();

        if (prepared.error.code == pdf::PDFOCRErrorCode::MissingModel)
        {
            if (QMessageBox::question(this, windowTitle(), tr("%1\n\nDo you want to open the language manager?").arg(prepared.error.message)) == QMessageBox::Yes)
            {
                onManageLanguages();
            }
        }
        else
        {
            QMessageBox::critical(this, windowTitle(), prepared.error.message);
        }
        return;
    }

    applySkippedPages(pending.pagesToSkip);

    const RunMode runMode = pending.runMode;

    // Job description
    pdf::PDFOCRJobDescription description;
    description.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    description.documentFingerprint = m_session->getDocumentIdentity().fingerprint;
    description.configuration = pending.configuration;
    description.models = prepared.models;

    for (pdf::PDFInteger page : pending.pagesToRecognize)
    {
        pdf::PDFOCRPageTask task;
        task.pageIndex = page;
        task.configuration = m_session->getEffectiveConfiguration(page);
        task.pageLabel = pdf::PDFOCRPagePreparer::getPageLabel(m_context.document, page);
        task.pageFingerprint = m_fingerprints[page];

        if (m_analysis.count(page))
        {
            task.analysis = m_analysis[page];
            task.maskedRectangles = task.analysis.annotationRectangles;
            task.maskedRectangles.insert(task.maskedRectangles.end(), task.analysis.redactionRectangles.begin(), task.analysis.redactionRectangles.end());

            if (pending.maskedPages.count(page))
            {
                // The existing text is not recognized again (R04)
                task.maskedRectangles.insert(task.maskedRectangles.end(), task.analysis.textRectangles.begin(), task.analysis.textRectangles.end());
            }
        }

        const pdf::PDFOCRPageResult* result = m_session->getPage(page);
        if (result)
        {
            task.regions = result->regions;
            task.generation = result->generation + 1;
            task.skipBlankDetection = result->blankDetectionOverridden;
        }
        else
        {
            task.generation = 1;
        }

        if (runMode != RunMode::Pages)
        {
            // Repeated recognition of a line, a word or a region (REGION-03): temporary inclusive region
            pdf::PDFOCRRegion region;
            if (runMode == RunMode::Region)
            {
                // The region keeps its identifier and its configuration exception, so the
                // blocks of the candidate belong to the region (CandidateMode::ReplaceRegion)
                const pdf::PDFOCRRegion* sourceRegion = result ? result->findRegion(m_rerecognizeRegionId) : nullptr;
                if (!sourceRegion || sourceRegion->type != pdf::PDFOCRRegionType::Recognize || sourceRegion->rect.isEmpty())
                {
                    updateUi();
                    return;
                }
                region = *sourceRegion;
            }
            else
            {
                const pdf::PDFOCRLine* line = result ? result->findLine(m_rerecognizeLineId) : nullptr;
                const pdf::PDFOCRWord* word = result ? result->findWord(m_rerecognizeWordId) : nullptr;
                QRectF rect = runMode == RunMode::Word && word ? word->quad.boundingRect() : (line ? line->quad.boundingRect() : QRectF());
                if (rect.isEmpty())
                {
                    updateUi();
                    return;
                }

                const double margin = rect.height() * 0.25;
                rect.adjust(-margin, -margin, margin, margin);

                region.id = TEMPORARY_REGION_ID;
                region.type = pdf::PDFOCRRegionType::Recognize;
                region.rect = rect;
                region.order = 1;
                task.configuration.layout = runMode == RunMode::Word ? pdf::PDFOCRLayout::SingleWord : pdf::PDFOCRLayout::SingleLine;
            }

            std::erase_if(task.regions, [](const pdf::PDFOCRRegion& item) { return item.type == pdf::PDFOCRRegionType::Recognize; });
            task.regions.push_back(region);

            // Orientation cannot be detected from a single line, the orientation detected for the page is used
            if (task.configuration.preprocessing.autoOrientation && result && result->orientation &&
                (!result->orientation->confidence || *result->orientation->confidence >= pdf::PDFOCRPagePreparer::MinimumOrientationConfidence))
            {
                task.configuration.preprocessing.rotation = result->orientation->rotation;
            }
            task.configuration.preprocessing.autoOrientation = false;
            task.skipBlankDetection = true;
            description.configuration.workerCount = 1;
        }

        description.pages.push_back(std::move(task));
    }

    m_runMode = runMode;
    m_jobReviewOnlyPages = std::move(pending.reviewOnlyPages);
    m_candidates.clear();
    m_jobFinishedPages = 0;
    m_jobTotalPages = int(description.pages.size());
    m_jobTimer.start();

    m_previousPageStates.clear();
    m_keptResultsCount = 0;
    if (runMode == RunMode::Pages)
    {
        for (const pdf::PDFOCRPageTask& task : description.pages)
        {
            if (pending.maskedPages.count(task.pageIndex))
            {
                m_maskedTextPages.insert(task.pageIndex);
            }
            else
            {
                m_maskedTextPages.erase(task.pageIndex);
            }

            if (!m_candidatePages.count(task.pageIndex))
            {
                const pdf::PDFOCRPageResult* existing = m_session->getPage(task.pageIndex);
                m_previousPageStates[task.pageIndex] = existing ? existing->state : pdf::PDFOCRPageState::Pending;
                m_session->setPageState(task.pageIndex, pdf::PDFOCRPageState::Pending);
            }
        }
    }

    if (!m_jobController->start(std::move(description), &m_jobGeneration))
    {
        // Pages keep their results
        for (const auto& item : m_previousPageStates)
        {
            m_session->setPageState(item.first, item.second);
        }
        m_previousPageStates.clear();
        updateUi();
        QMessageBox::critical(this, windowTitle(), tr("Recognition cannot be started."));
        return;
    }

    ui->progressBar->setRange(0, m_jobTotalPages);
    ui->progressBar->setValue(0);
    ui->progressLabel->setText(tr("Recognition started (%n page(s)).", nullptr, m_jobTotalPages));
    updateUi();
}

void PDFOCRDocumentDialog::onJobPageStateChanged(int generation, qint64 pageIndex, int state, QString phase)
{
    if (generation != m_jobGeneration)
    {
        return;
    }

    if (m_runMode == RunMode::Pages && !m_candidatePages.count(pageIndex))
    {
        m_session->setPageState(pageIndex, pdf::PDFOCRPageState(state));
    }

    // Phases without a real percentage have an indeterminate progress (JOB-04)
    ui->progressLabel->setText(tr("Page %1: %2 (%3 of %4 finished)").arg(pageIndex + 1).arg(phase).arg(m_jobFinishedPages).arg(m_jobTotalPages));
}

void PDFOCRDocumentDialog::onJobPageProgress(int generation, qint64 pageIndex, int percent)
{
    if (generation != m_jobGeneration || m_jobController->isStopping())
    {
        return;
    }

    QString text = percent >= 0 ? tr("Page %1: recognition %2 % (%3 of %4 finished)").arg(pageIndex + 1).arg(percent).arg(m_jobFinishedPages).arg(m_jobTotalPages)
                                : tr("Page %1: recognition (%2 of %3 finished)").arg(pageIndex + 1).arg(m_jobFinishedPages).arg(m_jobTotalPages);

    // The remaining time is estimated only after enough samples and is marked as an estimate (JOB-04)
    if (m_jobFinishedPages >= 3 && m_jobTotalPages > m_jobFinishedPages)
    {
        const double secondsPerPage = double(m_jobTimer.elapsed()) / 1000.0 / m_jobFinishedPages;
        const int remaining = qRound(secondsPerPage * (m_jobTotalPages - m_jobFinishedPages));
        text += tr(", estimated remaining time %1 s").arg(remaining);
    }

    ui->progressLabel->setText(text);
}

void PDFOCRDocumentDialog::onJobPageFinished(int generation, pdf::PDFOCRPageResult result)
{
    if (generation != m_jobGeneration)
    {
        // Late result of an old run must not join the current session (JOB-02)
        return;
    }

    const pdf::PDFInteger pageIndex = result.pageIndex;

    if (m_runMode == RunMode::Pages)
    {
        // Permission to write the result into the PDF is a serialized property of
        // the result, independent of the settings of the next run (INPUT-04, R03)
        result.reviewOnly = m_jobReviewOnlyPages.count(pageIndex) > 0;
    }

    if (m_runMode != RunMode::Pages || m_candidatePages.count(pageIndex))
    {
        // The candidate is attached after the run finishes; a failure keeps the previous result (EDIT-07)
        m_candidates[pageIndex] = std::move(result);
        return;
    }

    // A stopped or failed repeated recognition keeps the previous result of the page (JOB-05)
    if (result.state == pdf::PDFOCRPageState::Cancelled || result.state == pdf::PDFOCRPageState::Error)
    {
        auto it = m_previousPageStates.find(pageIndex);
        const bool hadResult = it != m_previousPageStates.end() &&
                               (it->second == pdf::PDFOCRPageState::Done || it->second == pdf::PDFOCRPageState::NoText || it->second == pdf::PDFOCRPageState::Stale);
        if (hadResult)
        {
            m_session->setPageState(pageIndex, it->second);
            ++m_keptResultsCount;
            updatePageItem(pageIndex);
            return;
        }
    }

    m_session->setPageResult(std::move(result));
    updatePageItem(pageIndex);
}

void PDFOCRDocumentDialog::onJobProgress(int generation, int finished, int total)
{
    if (generation != m_jobGeneration)
    {
        return;
    }

    m_jobFinishedPages = finished;
    m_jobTotalPages = total;
    ui->progressBar->setRange(0, total);
    ui->progressBar->setValue(finished);
}

void PDFOCRDocumentDialog::onJobFinished(int generation, pdf::PDFOCRJobSummary summary)
{
    if (generation != m_jobGeneration)
    {
        return;
    }

    ui->progressBar->setRange(0, qMax(1, summary.totalPages));
    ui->progressBar->setValue(summary.totalPages);

    // A partial result is never reported as a total success (JOB-07)
    QString text = tr("Finished in %1 s: %2 recognized, %3 without text, %4 errors, %5 cancelled.")
                   .arg(double(summary.elapsedMilliseconds) / 1000.0, 0, 'f', 1).arg(summary.donePages).arg(summary.noTextPages).arg(summary.errorPages).arg(summary.cancelledPages);
    if (summary.cancelled)
    {
        text = tr("Stopped. ") + text;
    }
    if (m_keptResultsCount > 0)
    {
        text += QChar(' ') + tr("Previous results of %n page(s) were kept.", nullptr, m_keptResultsCount);
    }
    ui->progressLabel->setText(text);
    m_previousPageStates.clear();

    const RunMode runMode = m_runMode;

    if (m_closeRequested)
    {
        // The dialog is being closed, the user is not asked about the candidates anymore
        m_candidates.clear();
        m_candidatePages.clear();
        m_closeRequested = false;
        updateUi();
        done(QDialog::Rejected);
        return;
    }

    if (runMode == RunMode::Pages && (summary.donePages > 0 || summary.noTextPages > 0))
    {
        showReviewPanel(true);
    }
    processCandidates();
    updateUi();
    onCurrentPageChanged();

    if (runMode != RunMode::Pages)
    {
        return;
    }

    if (summary.criticalError)
    {
        QMessageBox::critical(this, windowTitle(), tr("Recognition failed in step '%1':\n\n%2").arg(summary.criticalError.step, summary.criticalError.message));
    }
    else if (summary.errorPages > 0)
    {
        if (QMessageBox::question(this, windowTitle(), tr("%1\n\nThe results are partial. Do you want to check only the pages with errors, so they can be recognized again with the same or changed settings?").arg(text)) == QMessageBox::Yes)
        {
            ui->helperSelectionComboBox->setCurrentIndex(ui->helperSelectionComboBox->findData(HelperErrors));
            onApplyHelperSelection();
        }
    }
}

void PDFOCRDocumentDialog::processCandidates()
{
    std::map<pdf::PDFInteger, pdf::PDFOCRPageResult> candidates = std::move(m_candidates);
    m_candidates.clear();

    for (auto& item : candidates)
    {
        const pdf::PDFInteger pageIndex = item.first;
        const pdf::PDFOCRPageResult& candidate = item.second;

        if (!candidate.hasResult())
        {
            if (candidate.state == pdf::PDFOCRPageState::Error)
            {
                QMessageBox::warning(this, windowTitle(), tr("Page %1: repeated recognition failed, the previous result is kept.\n\n%2").arg(pageIndex + 1).arg(candidate.error.message));
            }
            continue;
        }

        if (m_runMode == RunMode::Pages)
        {
            // Comparison of the current and the new text with a preview before the replacement (EDIT-07)
            const pdf::PDFOCRPageResult* current = m_session->getPage(pageIndex);
            QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("Page %1 was recognized again. The current result contains manual corrections.").arg(pageIndex + 1), QMessageBox::NoButton, this);
            messageBox.setInformativeText(tr("Do you want to replace the current result by the new recognition? The replacement can be undone."));
            messageBox.setDetailedText(tr("CURRENT TEXT:\n%1\n\nNEW RECOGNITION:\n%2").arg(current ? current->getText() : QString(), candidate.getText()));
            QPushButton* replaceButton = messageBox.addButton(tr("Replace"), QMessageBox::AcceptRole);
            messageBox.addButton(tr("Keep Current"), QMessageBox::RejectRole);
            messageBox.exec();

            if (messageBox.clickedButton() == replaceButton)
            {
                if (m_session->applyCandidate(pageIndex, candidate, pdf::PDFOCRSession::CandidateMode::Replace, -1))
                {
                    m_session->setPageReviewOnly(pageIndex, candidate.reviewOnly);
                }
            }
            continue;
        }

        if (m_runMode == RunMode::Region)
        {
            // Only the blocks of the region are replaced, the rest of the page is kept (REGION-03)
            const int regionId = m_rerecognizeRegionId;
            QStringList newTexts;
            for (const pdf::PDFOCRBlock& block : candidate.blocks)
            {
                if (block.regionId == regionId && !block.getText().trimmed().isEmpty())
                {
                    newTexts << block.getText();
                }
            }

            if (newTexts.isEmpty())
            {
                QMessageBox::information(this, windowTitle(), tr("No text was found in the region. The previous result is kept."));
                continue;
            }

            QStringList oldTexts;
            if (const pdf::PDFOCRPageResult* current = m_session->getPage(pageIndex))
            {
                for (const pdf::PDFOCRBlock& block : current->blocks)
                {
                    if (block.regionId == regionId)
                    {
                        oldTexts << block.getText();
                    }
                }
            }

            if (QMessageBox::question(this, windowTitle(), tr("Current text of the region:\n%1\n\nNew recognition:\n%2\n\nDo you want to replace the text of the region?")
                                      .arg(oldTexts.join(QChar('\n')), newTexts.join(QChar('\n')))) == QMessageBox::Yes)
            {
                m_session->applyCandidate(pageIndex, candidate, pdf::PDFOCRSession::CandidateMode::ReplaceRegion, regionId);
            }
            continue;
        }

        // Line or word
        std::vector<pdf::PDFOCRWord> words;
        for (const pdf::PDFOCRWord* word : candidate.getWords())
        {
            words.push_back(*word);
        }

        if (words.empty())
        {
            QMessageBox::information(this, windowTitle(), tr("No text was found in the selected area. The previous result is kept."));
            continue;
        }

        QStringList newTexts;
        for (const pdf::PDFOCRWord& word : words)
        {
            newTexts << word.text;
        }

        const pdf::PDFOCRPageResult* current = m_session->getPage(pageIndex);
        const pdf::PDFOCRLine* line = current ? current->findLine(m_rerecognizeLineId) : nullptr;
        const pdf::PDFOCRWord* word = current ? current->findWord(m_rerecognizeWordId) : nullptr;
        const QString oldText = m_runMode == RunMode::Word ? (word ? word->text : QString()) : (line ? line->getText() : QString());

        if (QMessageBox::question(this, windowTitle(), tr("Current text:\n%1\n\nNew recognition:\n%2\n\nDo you want to replace the current text?").arg(oldText, newTexts.join(QChar(' ')))) == QMessageBox::Yes)
        {
            if (m_runMode == RunMode::Word)
            {
                m_session->replaceWord(pageIndex, m_rerecognizeWordId, words);
            }
            else
            {
                m_session->replaceLineWords(pageIndex, m_rerecognizeLineId, words);
            }
        }
    }

    m_candidatePages.clear();
    m_runMode = RunMode::Pages;
}

void PDFOCRDocumentDialog::onRerecognizeClicked()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page || !isPageEditable(m_currentPage) || m_jobController->isRunning())
    {
        return;
    }

    const pdf::PDFOCRLine* line = m_selectedWordId ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId);
    if (!line)
    {
        QMessageBox::information(this, windowTitle(), tr("Select a word or a line first."));
        return;
    }

    QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("What should be recognized again?"), QMessageBox::NoButton, this);
    QPushButton* lineButton = messageBox.addButton(tr("Whole Line"), QMessageBox::AcceptRole);
    QPushButton* wordButton = m_selectedWordId ? messageBox.addButton(tr("Selected Word"), QMessageBox::ActionRole) : nullptr;
    messageBox.addButton(QMessageBox::Cancel);
    messageBox.exec();

    if (messageBox.clickedButton() != lineButton && (!wordButton || messageBox.clickedButton() != wordButton))
    {
        return;
    }

    m_rerecognizePage = m_currentPage;
    m_rerecognizeLineId = line->id;
    m_rerecognizeWordId = m_selectedWordId;
    startRecognition({ m_currentPage }, messageBox.clickedButton() == lineButton ? RunMode::Line : RunMode::Word);
}

// -------------------------------------------------------------------------
// Review
// -------------------------------------------------------------------------

bool PDFOCRDocumentDialog::isPageInRunningJob(pdf::PDFInteger pageIndex) const
{
    if (!m_jobController->isRunning() || m_runMode != RunMode::Pages)
    {
        return false;
    }

    if (m_jobController->isPagePending(pageIndex))
    {
        return true;
    }

    const pdf::PDFOCRPageResult* page = m_session->getPage(pageIndex);
    return page && (page->state == pdf::PDFOCRPageState::Preparing || page->state == pdf::PDFOCRPageState::Recognizing);
}

bool PDFOCRDocumentDialog::isPageEditable(pdf::PDFInteger pageIndex) const
{
    // The page being processed or scheduled for an overwrite cannot be edited (UI-06)
    if (pageIndex < 0 || (m_jobController->isRunning() && m_runMode == RunMode::Pages && m_jobController->isPagePending(pageIndex)))
    {
        return false;
    }

    const pdf::PDFOCRPageResult* page = m_session->getPage(pageIndex);
    return page && page->state != pdf::PDFOCRPageState::Preparing && page->state != pdf::PDFOCRPageState::Recognizing;
}

void PDFOCRDocumentDialog::onSessionPageChanged(qint64 pageIndex)
{
    updatePageItem(pageIndex);
    if (pageIndex == m_currentPage)
    {
        updateViews();
        updateResultsTree();
        updateUi();
    }
}

void PDFOCRDocumentDialog::updateViews()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    for (PDFOCRPageView* view : { m_originalView, m_workingView })
    {
        view->setPageResult(page);
        view->setReviewCriteria(m_session->getReviewCriteria());
        view->setSelectedWord(m_selectedWordId, false);
        view->setSelectedLine(m_selectedLineId);
    }
}

void PDFOCRDocumentDialog::setViewMode(ViewMode mode)
{
    m_originalView->setVisible(mode != ViewMode::Working);
    m_workingView->setVisible(mode != ViewMode::Original);
    schedulePreview();
}

void PDFOCRDocumentDialog::updateResultsTree()
{
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;

    ui->resultsTreeWidget->clear();
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const int filter = ui->reviewFilterComboBox->currentData().toInt();
    const pdf::PDFOCRReviewCriteria criteria = m_session->getReviewCriteria();
    const double threshold = criteria.threshold;
    QTreeWidgetItem* selectedItem = nullptr;

    if (page)
    {
        int blockNumber = 0;
        for (const pdf::PDFOCRBlock& block : page->blocks)
        {
            ++blockNumber;
            QString blockText = tr("Block %1").arg(blockNumber);
            if (const pdf::PDFOCRRegion* region = page->findRegion(block.regionId))
            {
                blockText += tr(" (region %1)").arg(region->name.isEmpty() ? QString::number(region->order) : region->name);
            }

            QTreeWidgetItem* blockItem = new QTreeWidgetItem(QStringList{ blockText });
            blockItem->setData(0, ROLE_ITEM_TYPE, ITEM_BLOCK);
            blockItem->setData(0, ROLE_ITEM_ID, block.id);

            for (const pdf::PDFOCRLine& line : block.lines)
            {
                QTreeWidgetItem* lineItem = new QTreeWidgetItem(QStringList{ line.getText() });
                lineItem->setData(0, ROLE_ITEM_TYPE, ITEM_LINE);
                lineItem->setData(0, ROLE_ITEM_ID, line.id);

                for (const pdf::PDFOCRWord& word : line.words)
                {
                    bool visible = true;
                    switch (filter)
                    {
                        case FilterRequiresReview:
                            visible = pdf::PDFOCRReview::requiresReview(word, criteria);
                            break;
                        case FilterBelowThreshold:
                            visible = pdf::PDFOCRReview::isBelowThreshold(word.confidence, threshold);
                            break;
                        case FilterUnknownConfidence:
                            visible = !word.confidence.isAvailable();
                            break;
                        case FilterModified:
                            visible = word.reviewState == pdf::PDFOCRReviewState::Modified;
                            break;
                        case FilterConfirmed:
                            visible = word.reviewState == pdf::PDFOCRReviewState::Confirmed;
                            break;
                        case FilterDiscarded:
                            visible = word.reviewState == pdf::PDFOCRReviewState::Discarded;
                            break;
                        case FilterOutsideDictionary:
                            visible = pdf::PDFOCRReview::isOutsideDictionary(word, criteria);
                            break;
                        default:
                            break;
                    }

                    if (!visible)
                    {
                        continue;
                    }

                    QString confidence = tr("n/a");
                    if (word.confidence.isAvailable())
                    {
                        confidence = QString::number(qRound(word.confidence.normalized.value()));
                        if (word.confidence.level == pdf::PDFOCRConfidenceLevel::Line)
                        {
                            confidence += tr(" (line)");
                        }
                    }

                    QString state;
                    switch (word.reviewState)
                    {
                        case pdf::PDFOCRReviewState::Unreviewed:
                            state = pdf::PDFOCRReview::requiresReview(word, criteria) ? tr("To review") : tr("Unreviewed");
                            break;
                        case pdf::PDFOCRReviewState::Confirmed:
                            state = tr("Confirmed");
                            break;
                        case pdf::PDFOCRReviewState::Modified:
                            state = tr("Corrected");
                            break;
                        case pdf::PDFOCRReviewState::Discarded:
                            state = tr("Not text");
                            break;
                    }
                    if (word.overlapsExcludedRegion)
                    {
                        state += tr(", in excluded region");
                    }
                    if (word.hasExtremeScaling)
                    {
                        state += tr(", extreme scaling");
                    }

                    // Dictionary information of the original recognition (historical, like the score)
                    QString dictionary = tr("n/a");
                    if (word.inDictionary.has_value())
                    {
                        dictionary = *word.inDictionary ? tr("yes") : tr("no");
                    }
                    if (pdf::PDFOCRReview::isOutsideDictionary(word, criteria))
                    {
                        state += tr(", not in dictionary");
                    }

                    QTreeWidgetItem* wordItem = new QTreeWidgetItem(QStringList{ word.text, confidence, state, dictionary });
                    wordItem->setData(0, ROLE_ITEM_TYPE, ITEM_WORD);
                    wordItem->setData(0, ROLE_ITEM_ID, word.id);
                    if (word.reviewState == pdf::PDFOCRReviewState::Discarded)
                    {
                        QFont font = wordItem->font(0);
                        font.setStrikeOut(true);
                        wordItem->setFont(0, font);
                    }
                    else if (pdf::PDFOCRReview::requiresReview(word, criteria))
                    {
                        QFont font = wordItem->font(0);
                        font.setBold(true);
                        wordItem->setFont(0, font);
                    }
                    lineItem->addChild(wordItem);

                    if (word.id == m_selectedWordId)
                    {
                        selectedItem = wordItem;
                    }
                }

                if (lineItem->childCount() > 0 || filter == FilterAll)
                {
                    blockItem->addChild(lineItem);
                    if (!selectedItem && line.id == m_selectedLineId && m_selectedWordId == 0)
                    {
                        selectedItem = lineItem;
                    }
                }
                else
                {
                    delete lineItem;
                }
            }

            if (blockItem->childCount() > 0 || filter == FilterAll)
            {
                ui->resultsTreeWidget->addTopLevelItem(blockItem);
                if (!selectedItem && block.id == m_selectedBlockId && m_selectedWordId == 0 && m_selectedLineId == 0)
                {
                    selectedItem = blockItem;
                }
            }
            else
            {
                delete blockItem;
            }
        }
    }

    ui->resultsTreeWidget->expandAll();
    if (selectedItem)
    {
        ui->resultsTreeWidget->setCurrentItem(selectedItem);
        ui->resultsTreeWidget->scrollToItem(selectedItem);
    }

    m_updatingUi = wasUpdating;
    updateInspector();
    updateStatistics();
}

void PDFOCRDocumentDialog::updateStatistics()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page || !page->hasResult())
    {
        ui->statisticsLabel->clear();
        return;
    }

    // Aggregates state their unit and the way of computation (CONF-04)
    const pdf::PDFOCRConfidenceStatistics statistics = m_session->getStatistics(m_currentPage);
    QStringList parts;
    parts << tr("%n word(s)", nullptr, statistics.wordCount);

    if (statistics.scoredWordCount > 0)
    {
        parts << tr("%1 below the threshold %2 (%3 % of %4 words with a score)").arg(statistics.belowThresholdCount).arg(qRound(m_session->getReviewThreshold()))
                 .arg(100.0 * statistics.belowThresholdCount / statistics.scoredWordCount, 0, 'f', 1).arg(statistics.scoredWordCount);
        const QString unit = statistics.level == pdf::PDFOCRConfidenceLevel::Line ? tr("line scores") : tr("word scores");
        parts << tr("arithmetic mean of the original %1: %2/100").arg(unit).arg(statistics.meanScore.value_or(0.0), 0, 'f', 1);
    }
    parts << tr("unknown confidence: %1").arg(statistics.unknownWordCount);
    parts << tr("manually added: %1").arg(statistics.manualWordCount);
    if (statistics.dictionaryCheckedCount > 0)
    {
        parts << tr("not in dictionary: %1").arg(statistics.outsideDictionaryCount);
    }
    parts << tr("to review: %1").arg(statistics.reviewRequiredCount);
    parts << tr("%n line(s) in %1 block(s)", nullptr, int(std::accumulate(page->blocks.begin(), page->blocks.end(), size_t(0), [](size_t count, const pdf::PDFOCRBlock& block) { return count + block.lines.size(); }))).arg(page->blocks.size());

    QString text = parts.join(QStringLiteral("; ")) + QChar('.');

    // Inclusive regions, in which no text was recognized, are listed, so a missing text
    // is not hidden by the high scores of the recognized words (EDIT-11)
    QStringList emptyRegions;
    int inclusiveRegionCount = 0;
    for (const pdf::PDFOCRRegion& region : page->regions)
    {
        if (region.type != pdf::PDFOCRRegionType::Recognize)
        {
            continue;
        }

        ++inclusiveRegionCount;
        const bool hasText = std::any_of(page->blocks.begin(), page->blocks.end(), [&region](const pdf::PDFOCRBlock& block)
        {
            return block.regionId == region.id && std::any_of(block.lines.begin(), block.lines.end(), [](const pdf::PDFOCRLine& line)
            {
                return std::any_of(line.words.begin(), line.words.end(), [](const pdf::PDFOCRWord& word) { return word.reviewState != pdf::PDFOCRReviewState::Discarded && !word.text.trimmed().isEmpty(); });
            });
        });

        if (!hasText)
        {
            emptyRegions << (region.name.isEmpty() ? tr("region %1").arg(region.order) : region.name);
        }
    }

    if (inclusiveRegionCount > 0)
    {
        text += QChar('\n');
        text += emptyRegions.isEmpty() ? tr("Regions without recognized text: 0 of %1.").arg(inclusiveRegionCount)
                                       : tr("Regions without recognized text: %1 (%2).").arg(emptyRegions.size()).arg(emptyRegions.join(QStringLiteral(", ")));
    }

    ui->statisticsLabel->setText(text);
    ui->statisticsLabel->setToolTip(tr("High scores do not prove that the transcript of the page is complete; check the areas without any detected text in the page view."));
}

void PDFOCRDocumentDialog::updateInspector()
{
    const bool wasUpdating = m_updatingUi;
    m_updatingUi = true;

    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRWord* word = page ? page->findWord(m_selectedWordId) : nullptr;
    const pdf::PDFOCRLine* line = page ? (word ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId)) : nullptr;

    ui->wordTextEdit->setText(word ? word->text : QString());
    ui->lineTextEdit->setText(line ? line->getText() : QString());

    if (word)
    {
        ui->originalTextLabel->setText(word->originalText.isEmpty() ? tr("(inserted manually)") : word->originalText);

        // The score is a historical property of the original recognition (CONF-05)
        QString confidence;
        if (!word->confidence.isAvailable())
        {
            confidence = tr("Confidence is not available");
        }
        else
        {
            const int score = qRound(word->confidence.normalized.value());
            const bool lineLevel = word->confidence.level == pdf::PDFOCRConfidenceLevel::Line;
            if (word->reviewState == pdf::PDFOCRReviewState::Modified || word->isTextModified())
            {
                confidence = tr("Manually corrected; original confidence %1/100").arg(score);
            }
            else
            {
                confidence = lineLevel ? tr("%1/100 (score of the whole line, not of the word)").arg(score) : tr("%1/100 (engine score, not a probability of correctness)").arg(score);
            }
        }
        if (word->inDictionary.has_value() && !word->isTextModified())
        {
            confidence += QStringLiteral("; ") + (*word->inDictionary ? tr("found in the dictionary of the language model") : tr("not found in the dictionary of the language model"));
        }
        ui->confidenceLabel->setText(confidence);

        QString state;
        switch (word->reviewState)
        {
            case pdf::PDFOCRReviewState::Unreviewed:
                state = tr("Not reviewed");
                break;
            case pdf::PDFOCRReviewState::Confirmed:
                state = tr("Confirmed by the user");
                break;
            case pdf::PDFOCRReviewState::Modified:
                state = tr("Corrected by the user");
                break;
            case pdf::PDFOCRReviewState::Discarded:
                state = tr("Not text (will not be written)");
                break;
        }
        if (word->reviewTime)
        {
            state += tr(", %1").arg(QLocale().toString(*word->reviewTime, QLocale::ShortFormat));
        }
        ui->reviewStateLabel->setText(state);

        QString model = word->language.isEmpty() ? tr("unknown language") : word->language;
        if (!page->provenance.engineId.isEmpty())
        {
            model += tr("; %1 %2").arg(page->provenance.engineId, page->provenance.engineVersion);
        }
        ui->modelLabel->setText(model);

        QString geometry;
        switch (word->geometryOrigin)
        {
            case pdf::PDFOCRGeometryOrigin::Engine:
                geometry = tr("geometry from the engine");
                break;
            case pdf::PDFOCRGeometryOrigin::Estimated:
                geometry = tr("estimated geometry (check the box)");
                break;
            case pdf::PDFOCRGeometryOrigin::Manual:
                geometry = tr("geometry edited manually");
                break;
            case pdf::PDFOCRGeometryOrigin::Imported:
                geometry = tr("geometry read from the document");
                break;
            case pdf::PDFOCRGeometryOrigin::Digital:
                geometry = tr("geometry of digital text");
                break;
        }

        QString region = tr("whole page");
        for (const pdf::PDFOCRBlock& block : page->blocks)
        {
            for (const pdf::PDFOCRLine& blockLine : block.lines)
            {
                if (line && blockLine.id == line->id)
                {
                    if (const pdf::PDFOCRRegion* blockRegion = page->findRegion(block.regionId))
                    {
                        region = blockRegion->name.isEmpty() ? tr("region %1").arg(blockRegion->order) : blockRegion->name;
                    }
                }
            }
        }
        ui->regionLabel->setText(tr("%1; %2").arg(region, geometry));
    }
    else
    {
        ui->originalTextLabel->clear();
        ui->confidenceLabel->clear();
        ui->reviewStateLabel->clear();
        ui->modelLabel->clear();
        ui->regionLabel->clear();
    }

    m_updatingUi = wasUpdating;
}

void PDFOCRDocumentDialog::selectWord(int wordId, bool fromTree)
{
    m_selectedWordId = wordId;
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRLine* line = page ? page->findLineOfWord(wordId) : nullptr;
    m_selectedLineId = line ? line->id : 0;

    // Selection of the text and of its image area is synchronized in both directions (UI-04)
    m_originalView->setSelectedWord(wordId, fromTree);
    m_workingView->setSelectedWord(wordId, fromTree);
    m_originalView->setSelectedLine(m_selectedLineId);
    m_workingView->setSelectedLine(m_selectedLineId);

    if (!fromTree)
    {
        const bool wasUpdating = m_updatingUi;
        m_updatingUi = true;
        QTreeWidgetItemIterator it(ui->resultsTreeWidget);
        while (*it)
        {
            if ((*it)->data(0, ROLE_ITEM_TYPE).toInt() == ITEM_WORD && (*it)->data(0, ROLE_ITEM_ID).toInt() == wordId)
            {
                ui->resultsTreeWidget->setCurrentItem(*it);
                ui->resultsTreeWidget->scrollToItem(*it);
                break;
            }
            ++it;
        }
        m_updatingUi = wasUpdating;
    }

    updateInspector();
    updateUi();
}

void PDFOCRDocumentDialog::onTreeSelectionChanged()
{
    if (m_updatingUi)
    {
        return;
    }

    QTreeWidgetItem* item = ui->resultsTreeWidget->currentItem();
    if (!item)
    {
        return;
    }

    const int id = item->data(0, ROLE_ITEM_ID).toInt();
    switch (item->data(0, ROLE_ITEM_TYPE).toInt())
    {
        case ITEM_WORD:
            selectWord(id, true);
            return;

        case ITEM_LINE:
            m_selectedWordId = 0;
            m_selectedLineId = id;
            m_selectedBlockId = 0;
            break;

        case ITEM_BLOCK:
            m_selectedWordId = 0;
            m_selectedLineId = 0;
            m_selectedBlockId = id;
            break;
    }

    m_originalView->setSelectedWord(0, false);
    m_workingView->setSelectedWord(0, false);
    m_originalView->setSelectedLine(m_selectedLineId);
    m_workingView->setSelectedLine(m_selectedLineId);
    updateInspector();
    updateUi();
}

void PDFOCRDocumentDialog::onWordTextEdited()
{
    if (m_updatingUi || !m_selectedWordId || !isPageEditable(m_currentPage))
    {
        return;
    }

    const QString text = ui->wordTextEdit->text().trimmed();
    if (text.isEmpty())
    {
        updateInspector();
        return;
    }

    if (text.contains(QChar(' ')))
    {
        // More words: the whole line is edited, so the geometry of the new words is assigned (EDIT-03)
        const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
        const pdf::PDFOCRLine* line = page ? page->findLineOfWord(m_selectedWordId) : nullptr;
        if (line)
        {
            QStringList words;
            for (const pdf::PDFOCRWord& word : line->words)
            {
                if (word.reviewState != pdf::PDFOCRReviewState::Discarded)
                {
                    words << (word.id == m_selectedWordId ? text : word.text);
                }
            }
            m_session->setLineText(m_currentPage, line->id, words.join(QChar(' ')));
        }
        return;
    }

    m_session->setWordText(m_currentPage, m_selectedWordId, text);
}

void PDFOCRDocumentDialog::onLineTextEdited()
{
    if (m_updatingUi || !isPageEditable(m_currentPage))
    {
        return;
    }

    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRLine* line = page ? (m_selectedWordId ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId)) : nullptr;
    if (!line || ui->lineTextEdit->text().simplified() == line->getText())
    {
        return;
    }

    if (ui->lineTextEdit->text().trimmed().isEmpty())
    {
        if (QMessageBox::question(this, windowTitle(), tr("Do you want to delete the whole line?")) == QMessageBox::Yes)
        {
            m_session->removeLine(m_currentPage, line->id);
        }
        else
        {
            updateInspector();
        }
        return;
    }

    m_session->setLineText(m_currentPage, line->id, ui->lineTextEdit->text());
}

void PDFOCRDocumentDialog::onReviewNavigation(bool forward, bool confirm)
{
    if (confirm && m_selectedWordId && isPageEditable(m_currentPage))
    {
        // Pending text edit is committed before the confirmation
        onWordTextEdited();
        const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
        const pdf::PDFOCRWord* word = page ? page->findWord(m_selectedWordId) : nullptr;
        if (word && word->reviewState != pdf::PDFOCRReviewState::Discarded)
        {
            m_session->setWordReviewState(m_currentPage, m_selectedWordId, pdf::PDFOCRReviewState::Confirmed);
        }
    }

    pdf::PDFOCRSession::WordReference start;
    start.pageIndex = m_currentPage;
    start.wordId = m_selectedWordId;

    const std::optional<pdf::PDFOCRSession::WordReference> next = m_session->findReviewItem(m_session->getPagesWithResults(), start, forward);
    if (!next)
    {
        QMessageBox::information(this, windowTitle(), tr("No other word requires a review. Note that a complete review also includes the areas, where no text was detected."));
        return;
    }

    if (next->pageIndex != m_currentPage)
    {
        ui->pagesListWidget->setCurrentRow(int(next->pageIndex));
    }

    if (ui->reviewFilterComboBox->currentData().toInt() != FilterAll && ui->reviewFilterComboBox->currentData().toInt() != FilterRequiresReview)
    {
        ui->reviewFilterComboBox->setCurrentIndex(ui->reviewFilterComboBox->findData(FilterAll));
    }

    selectWord(next->wordId, false);
    m_originalView->setSelectedWord(next->wordId, true);
    m_workingView->setSelectedWord(next->wordId, true);
    ui->wordTextEdit->setFocus();
    ui->wordTextEdit->selectAll();
}

std::vector<pdf::PDFInteger> PDFOCRDocumentDialog::getFindScopePages() const
{
    switch (ui->findScopeComboBox->currentData().toInt())
    {
        case 0:
            return m_currentPage >= 0 ? std::vector<pdf::PDFInteger>{ m_currentPage } : std::vector<pdf::PDFInteger>();
        case 1:
            return getCheckedPages();
        default:
            break;
    }
    return m_session->getPagesWithResults();
}

void PDFOCRDocumentDialog::onFindNext()
{
    pdf::PDFOCRSession::FindOptions options;
    options.caseSensitive = ui->caseSensitiveCheckBox->isChecked();
    options.wholeWords = ui->wholeWordsCheckBox->isChecked();

    const std::vector<pdf::PDFOCRSession::FindHit> hits = m_session->find(getFindScopePages(), ui->findEdit->text(), options);
    if (hits.empty())
    {
        QMessageBox::information(this, windowTitle(), tr("Text '%1' was not found in the OCR results.").arg(ui->findEdit->text()));
        return;
    }

    // Next hit after the current selection. A word can contain more hits, they are
    // one item for the navigation (the whole word is selected), so the search
    // continues with the first hit of the following word.
    size_t index = 0;
    for (size_t i = 0; i < hits.size(); ++i)
    {
        if (hits[i].pageIndex == m_currentPage && hits[i].wordId == m_selectedWordId)
        {
            size_t next = i;
            while (next < hits.size() && hits[next].pageIndex == m_currentPage && hits[next].wordId == m_selectedWordId)
            {
                ++next;
            }
            index = next % hits.size();
            break;
        }
    }

    const pdf::PDFOCRSession::FindHit& hit = hits[index];
    if (hit.pageIndex != m_currentPage)
    {
        ui->pagesListWidget->setCurrentRow(int(hit.pageIndex));
    }
    if (ui->reviewFilterComboBox->currentData().toInt() != FilterAll)
    {
        ui->reviewFilterComboBox->setCurrentIndex(ui->reviewFilterComboBox->findData(FilterAll));
    }
    selectWord(hit.wordId, false);
    m_originalView->setSelectedWord(hit.wordId, true);
    m_workingView->setSelectedWord(hit.wordId, true);
    if (hit.singleWord)
    {
        ui->wordTextEdit->setSelection(hit.position, hit.length);
    }
    else
    {
        // A phrase over several words: the position is an offset in the text of the line (EDIT-05)
        ui->wordTextEdit->selectAll();
        ui->lineTextEdit->setSelection(hit.position, hit.length);
    }
}

void PDFOCRDocumentDialog::onReplaceAll()
{
    pdf::PDFOCRSession::FindOptions options;
    options.caseSensitive = ui->caseSensitiveCheckBox->isChecked();
    options.wholeWords = ui->wholeWordsCheckBox->isChecked();

    std::vector<pdf::PDFInteger> pages;
    for (pdf::PDFInteger page : getFindScopePages())
    {
        if (isPageEditable(page))
        {
            pages.push_back(page);
        }
    }

    const QString findText = ui->findEdit->text();
    const QString replaceText = ui->replaceEdit->text();
    const std::vector<pdf::PDFOCRSession::FindHit> hits = m_session->find(pages, findText, options);
    if (hits.empty())
    {
        QMessageBox::information(this, windowTitle(), tr("Text '%1' was not found in the OCR results.").arg(findText));
        return;
    }

    // Number of the hits and a preview are shown before the replacement (EDIT-05)
    std::set<pdf::PDFInteger> hitPages;
    for (const pdf::PDFOCRSession::FindHit& hit : hits)
    {
        hitPages.insert(hit.pageIndex);
    }

    QStringList preview;
    for (size_t i = 0; i < hits.size() && i < 15; ++i)
    {
        const pdf::PDFOCRPageResult* page = m_session->getPage(hits[i].pageIndex);
        if (!hits[i].singleWord)
        {
            // The hit spans more words, the position is an offset in the text of the line
            if (const pdf::PDFOCRLine* line = page ? page->findLine(hits[i].lineId) : nullptr)
            {
                const QString text = line->getText();
                QString replaced = text;
                replaced.replace(hits[i].position, hits[i].length, replaceText);
                preview << tr("Page %1: %2 -> %3").arg(hits[i].pageIndex + 1).arg(text, replaced);
            }
            continue;
        }

        const pdf::PDFOCRWord* word = page ? page->findWord(hits[i].wordId) : nullptr;
        if (word)
        {
            QString replaced = word->text;
            replaced.replace(hits[i].position, hits[i].length, replaceText);
            preview << tr("Page %1: %2 -> %3").arg(hits[i].pageIndex + 1).arg(word->text, replaced);
        }
    }
    if (hits.size() > 15)
    {
        preview << tr("... and %1 more").arg(hits.size() - 15);
    }

    QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("%n occurrence(s) of '%1' will be replaced by '%2' on %3 page(s).", nullptr, int(hits.size())).arg(findText, replaceText, QString::number(hitPages.size())),
                           QMessageBox::Yes | QMessageBox::Cancel, this);
    messageBox.setInformativeText(tr("The replacement changes only the invisible text, not the scanned image. The whole replacement can be undone in a single step."));
    messageBox.setDetailedText(preview.join(QChar('\n')));
    if (messageBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    const int count = m_session->replaceAll(pages, findText, replaceText, options);
    ui->progressLabel->setText(tr("%n occurrence(s) replaced.", nullptr, count));
}

void PDFOCRDocumentDialog::onRectangleDrawn(int mode, QRectF pageRectangle, pdf::PDFOCRQuad pageQuad)
{
    if (m_currentPage < 0)
    {
        return;
    }

    if (isPageInRunningJob(m_currentPage))
    {
        // The result of the running recognition would overwrite the change without any notice (UI-06)
        ui->progressLabel->setText(tr("Page %1 is being recognized, it can be changed after the recognition finishes.").arg(m_currentPage + 1));
        return;
    }

    switch (PDFOCRPageView::Mode(mode))
    {
        case PDFOCRPageView::Mode::DrawRecognizeRegion:
        case PDFOCRPageView::Mode::DrawExcludeRegion:
        {
            pdf::PDFOCRRegion region;
            region.type = PDFOCRPageView::Mode(mode) == PDFOCRPageView::Mode::DrawExcludeRegion ? pdf::PDFOCRRegionType::Exclude : pdf::PDFOCRRegionType::Recognize;
            region.rect = pageRectangle;
            const int regionId = m_session->addRegion(m_currentPage, region);
            m_originalView->setSelectedRegion(regionId);
            m_workingView->setSelectedRegion(regionId);
            ui->showRegionsCheckBox->setChecked(true);
            schedulePreview();
            break;
        }

        case PDFOCRPageView::Mode::DrawLine:
        {
            // A missing text can be added even on a page, where the engine found nothing (REGION-03)
            if (!m_session->getPage(m_currentPage) || !isPageEditable(m_currentPage))
            {
                m_session->getOrCreatePage(m_currentPage);
            }

            bool ok = false;
            const QString text = QInputDialog::getText(this, tr("Add Text Line"), tr("Text of the line:"), QLineEdit::Normal, QString(), &ok);
            if (ok && !text.trimmed().isEmpty())
            {
                ensureFingerprints({ m_currentPage }, false);
                pdf::PDFOCRPageResult& page = m_session->getOrCreatePage(m_currentPage);
                if (!page.hasResult())
                {
                    page.pageFingerprint = m_fingerprints[m_currentPage];
                    page.state = pdf::PDFOCRPageState::NoText;
                    page.provenance.engineId = QStringLiteral("manual");
                }

                int lineId = 0;
                if (m_session->insertLine(m_currentPage, m_selectedBlockId ? m_selectedBlockId : -1, text, pageQuad, &lineId))
                {
                    m_selectedWordId = 0;
                    m_selectedLineId = lineId;
                }
            }
            break;
        }

        default:
            break;
    }

    updateUi();
}

void PDFOCRDocumentDialog::onRegionProperties()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRRegion* region = page ? page->findRegion(m_originalView->getSelectedRegion()) : nullptr;
    if (!region)
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Region"));
    QFormLayout* layout = new QFormLayout(&dialog);

    QLineEdit* nameEdit = new QLineEdit(region->name, &dialog);
    QComboBox* typeComboBox = new QComboBox(&dialog);
    typeComboBox->addItem(tr("Recognize text"), int(pdf::PDFOCRRegionType::Recognize));
    typeComboBox->addItem(tr("Leave out"), int(pdf::PDFOCRRegionType::Exclude));
    typeComboBox->setCurrentIndex(typeComboBox->findData(int(region->type)));
    QSpinBox* orderSpinBox = new QSpinBox(&dialog);
    orderSpinBox->setRange(1, 999);
    orderSpinBox->setValue(qMax(1, region->order));
    QLineEdit* languagesEdit = new QLineEdit(region->configuration.languages.join(QChar('+')), &dialog);
    languagesEdit->setPlaceholderText(tr("Common settings"));
    QComboBox* layoutComboBox = new QComboBox(&dialog);
    layoutComboBox->addItem(tr("Common settings"), -1);
    for (pdf::PDFOCRLayout regionLayout : pdf::PDFOCRConfiguration::getLayouts())
    {
        layoutComboBox->addItem(pdf::PDFOCRConfiguration::getLayoutName(regionLayout), int(regionLayout));
    }
    layoutComboBox->setCurrentIndex(qMax(0, layoutComboBox->findData(region->configuration.segmentation)));

    layout->addRow(tr("&Name:"), nameEdit);
    layout->addRow(tr("&Type:"), typeComboBox);
    layout->addRow(tr("Reading &order:"), orderSpinBox);
    layout->addRow(tr("&Languages (for example ces+eng):"), languagesEdit);
    layout->addRow(tr("&Page layout:"), layoutComboBox);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    pdf::PDFOCRRegion changed = *region;
    changed.name = nameEdit->text().trimmed();
    changed.type = pdf::PDFOCRRegionType(typeComboBox->currentData().toInt());
    changed.order = orderSpinBox->value();
    changed.configuration.languages = languagesEdit->text().split(QChar('+'), Qt::SkipEmptyParts);
    changed.configuration.segmentation = layoutComboBox->currentData().toInt();
    m_session->updateRegion(m_currentPage, changed);
    schedulePreview();
}

void PDFOCRDocumentDialog::onRemoveRegion()
{
    const int regionId = m_originalView->getSelectedRegion();
    if (regionId && m_session->removeRegion(m_currentPage, regionId))
    {
        m_originalView->setSelectedRegion(0);
        m_workingView->setSelectedRegion(0);
        schedulePreview();
    }
}

void PDFOCRDocumentDialog::onMoveItem(bool up)
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page || !isPageEditable(m_currentPage))
    {
        return;
    }

    const pdf::PDFOCRLine* line = m_selectedWordId ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId);
    if (line)
    {
        for (const pdf::PDFOCRBlock& block : page->blocks)
        {
            for (size_t i = 0; i < block.lines.size(); ++i)
            {
                if (block.lines[i].id == line->id)
                {
                    m_session->moveLine(m_currentPage, line->id, int(i) + (up ? -1 : 1));
                    return;
                }
            }
        }
        return;
    }

    for (size_t i = 0; i < page->blocks.size(); ++i)
    {
        if (page->blocks[i].id == m_selectedBlockId)
        {
            // The order of the blocks is the reading order of the export and of the written text (EDIT-09)
            m_session->moveBlock(m_currentPage, m_selectedBlockId, int(i) + (up ? -1 : 1));
            return;
        }
    }
}

void PDFOCRDocumentDialog::onInsertWord()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRLine* line = page ? page->findLineOfWord(m_selectedWordId) : nullptr;
    if (!line || !isPageEditable(m_currentPage))
    {
        QMessageBox::information(this, windowTitle(), tr("Select the word, after which the new word should be inserted. To add a text to an area without any result, use 'Add Text Line'."));
        return;
    }

    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Insert Word"), tr("Text of the missing word:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || text.isEmpty())
    {
        return;
    }

    int newWordId = 0;
    if (m_session->insertWord(m_currentPage, line->id, m_selectedWordId, text, pdf::PDFOCRQuad(), &newWordId))
    {
        selectWord(newWordId, false);
        ui->progressLabel->setText(tr("The geometry of the inserted word is estimated. Use 'Edit Box' to place it exactly."));
    }
}

void PDFOCRDocumentDialog::onConfirmAll()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page || !isPageEditable(m_currentPage))
    {
        return;
    }

    int count = 0;
    for (const pdf::PDFOCRWord* word : page->getWords())
    {
        if (word->reviewState == pdf::PDFOCRReviewState::Unreviewed || word->reviewState == pdf::PDFOCRReviewState::Modified)
        {
            ++count;
        }
    }

    // The action states the number of items and is not a mere page navigation (EDIT-06)
    if (count == 0 || QMessageBox::question(this, windowTitle(), tr("Do you want to confirm %n word(s) of the page %1 as reviewed? The confidence scores are not changed.", nullptr, count).arg(m_currentPage + 1)) != QMessageBox::Yes)
    {
        return;
    }

    m_session->confirmAllWords(m_currentPage, &count);
}

void PDFOCRDocumentDialog::showTreeContextMenu(const QPoint& point)
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page)
    {
        return;
    }

    const bool editable = isPageEditable(m_currentPage) && !isBusy();
    const pdf::PDFOCRLine* line = m_selectedWordId ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId);
    const int regionId = getRegionIdOfSelection();

    // Structure of the lines (EDIT-02): the next line of the same block, the word after
    // which the line can be split, and the other blocks, into which the line can be moved
    const pdf::PDFOCRLine* nextLine = nullptr;
    const pdf::PDFOCRBlock* lineBlock = nullptr;
    if (line)
    {
        for (const pdf::PDFOCRBlock& block : page->blocks)
        {
            for (size_t i = 0; i < block.lines.size(); ++i)
            {
                if (block.lines[i].id == line->id)
                {
                    lineBlock = &block;
                    nextLine = i + 1 < block.lines.size() ? &block.lines[i + 1] : nullptr;
                }
            }
        }
    }
    const bool canSplitLine = line && m_selectedWordId && !line->words.empty() && line->words.back().id != m_selectedWordId;

    QMenu menu(this);
    QAction* copyPageAction = menu.addAction(tr("Copy Text of the Page"));
    QAction* copyItemAction = menu.addAction(tr("Copy Text of the Selected Item"));
    menu.addSeparator();
    QAction* recognizeAction = menu.addAction(tr("Recognize Again..."));
    QAction* recognizeRegionAction = menu.addAction(tr("Re-recognize Region"));
    QAction* confirmAction = menu.addAction(tr("Confirm"));
    QAction* notTextAction = menu.addAction(tr("Not Text"));
    QAction* restoreAction = menu.addAction(tr("Restore Original Recognition"));
    QAction* addUserWordAction = menu.addAction(tr("Add to User Words"));
    menu.addSeparator();
    QAction* mergeLinesAction = menu.addAction(tr("Merge with Next Line"));
    QAction* splitLineAction = menu.addAction(tr("Split Line After Word"));
    QAction* moveLineAction = menu.addAction(tr("Move Line to Block..."));

    copyPageAction->setEnabled(m_context.canCopyContent);
    copyItemAction->setEnabled(m_context.canCopyContent && ui->resultsTreeWidget->currentItem());
    recognizeAction->setEnabled(!m_jobController->isRunning() && (m_selectedWordId || m_selectedLineId));
    recognizeRegionAction->setEnabled(regionId != -1 && canRerecognizeRegion(regionId));
    confirmAction->setEnabled(m_selectedWordId != 0);
    notTextAction->setEnabled(m_selectedWordId != 0);
    restoreAction->setEnabled(m_selectedWordId != 0);
    const pdf::PDFOCRWord* selectedWord = m_selectedWordId ? page->findWord(m_selectedWordId) : nullptr;
    addUserWordAction->setEnabled(selectedWord && !pdf::PDFOCRReviewCriteria::normalizeWord(selectedWord->text).isEmpty());
    addUserWordAction->setToolTip(tr("The word is accepted by the dictionary review and it is passed to the engine as a user word in the next recognition"));
    mergeLinesAction->setEnabled(editable && nextLine && nextLine->direction == line->direction);
    splitLineAction->setEnabled(editable && canSplitLine);
    moveLineAction->setEnabled(editable && line && lineBlock && page->blocks.size() > 1);

    QAction* action = menu.exec(ui->resultsTreeWidget->viewport()->mapToGlobal(point));

    // The menu has its own event loop, the page could be changed meanwhile
    page = m_session->getPage(m_currentPage);
    line = page ? (m_selectedWordId ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId)) : nullptr;
    if (!page)
    {
        return;
    }

    if (action == copyPageAction)
    {
        pdf::PDFOCRTextExporter::Options options;
        QApplication::clipboard()->setText(pdf::PDFOCRTextExporter::getPageText(*page, options));
    }
    else if (action == copyItemAction)
    {
        QApplication::clipboard()->setText(ui->resultsTreeWidget->currentItem()->text(0));
    }
    else if (action == recognizeAction)
    {
        onRerecognizeClicked();
    }
    else if (action == recognizeRegionAction)
    {
        rerecognizeRegion(regionId);
    }
    else if (action == mergeLinesAction && line && isPageEditable(m_currentPage))
    {
        const pdf::PDFOCRBlock* block = nullptr;
        for (const pdf::PDFOCRBlock& currentBlock : page->blocks)
        {
            if (std::any_of(currentBlock.lines.begin(), currentBlock.lines.end(), [line](const pdf::PDFOCRLine& item) { return item.id == line->id; }))
            {
                block = &currentBlock;
            }
        }

        auto it = block ? std::find_if(block->lines.begin(), block->lines.end(), [line](const pdf::PDFOCRLine& item) { return item.id == line->id; }) : std::vector<pdf::PDFOCRLine>::const_iterator();
        if (block && it != block->lines.end() && std::next(it) != block->lines.end())
        {
            int newLineId = 0;
            if (m_session->mergeLines(m_currentPage, line->id, std::next(it)->id, &newLineId))
            {
                m_selectedWordId = 0;
                m_selectedBlockId = 0;
                m_selectedLineId = newLineId;
                updateResultsTree();
                updateViews();
            }
            else
            {
                QMessageBox::information(this, windowTitle(), tr("The lines cannot be merged. Only the lines of the same block with the same text direction can be merged."));
            }
        }
    }
    else if (action == splitLineAction && line && m_selectedWordId && isPageEditable(m_currentPage))
    {
        int newLineId = 0;
        if (!m_session->splitLine(m_currentPage, line->id, m_selectedWordId, &newLineId))
        {
            QMessageBox::information(this, windowTitle(), tr("The line cannot be split after the selected word."));
        }
    }
    else if (action == moveLineAction && line && isPageEditable(m_currentPage))
    {
        // Blocks are offered by their order and their first words
        QStringList items;
        std::vector<int> blockIds;
        int blockNumber = 0;
        for (const pdf::PDFOCRBlock& block : page->blocks)
        {
            ++blockNumber;
            if (std::any_of(block.lines.begin(), block.lines.end(), [line](const pdf::PDFOCRLine& item) { return item.id == line->id; }))
            {
                continue;
            }

            QStringList firstWords;
            for (const pdf::PDFOCRLine& blockLine : block.lines)
            {
                for (const pdf::PDFOCRWord& word : blockLine.words)
                {
                    if (firstWords.size() < 5 && word.reviewState != pdf::PDFOCRReviewState::Discarded)
                    {
                        firstWords << word.text;
                    }
                }
            }
            items << tr("Block %1: %2").arg(blockNumber).arg(firstWords.isEmpty() ? tr("(empty)") : firstWords.join(QChar(' ')));
            blockIds.push_back(block.id);
        }

        bool ok = false;
        const int lineId = line->id;
        const QString item = QInputDialog::getItem(this, tr("Move Line to Block"), tr("Target block of the line '%1':").arg(line->getText()), items, 0, false, &ok);
        const qsizetype index = items.indexOf(item);
        const pdf::PDFOCRPageResult* currentPage = m_session->getPage(m_currentPage);
        if (ok && index >= 0 && currentPage && isPageEditable(m_currentPage))
        {
            // The line is appended at the end of the target block
            const pdf::PDFOCRBlock* targetBlock = currentPage->findBlock(blockIds[size_t(index)]);
            if (targetBlock && m_session->moveLineToBlock(m_currentPage, lineId, targetBlock->id, int(targetBlock->lines.size())))
            {
                m_selectedWordId = 0;
                m_selectedBlockId = 0;
                m_selectedLineId = lineId;
                updateResultsTree();
                updateViews();
            }
        }
    }
    else if (action == confirmAction && isPageEditable(m_currentPage))
    {
        m_session->setWordReviewState(m_currentPage, m_selectedWordId, pdf::PDFOCRReviewState::Confirmed);
    }
    else if (action == notTextAction && isPageEditable(m_currentPage))
    {
        m_session->setWordReviewState(m_currentPage, m_selectedWordId, pdf::PDFOCRReviewState::Discarded);
    }
    else if (action == restoreAction && isPageEditable(m_currentPage))
    {
        m_session->restoreOriginalText(m_currentPage, m_selectedWordId);
    }
    else if (action == addUserWordAction)
    {
        addSelectedWordToUserWords();
    }
}

pdf::PDFOCRCompressionSettings PDFOCRDocumentDialog::getCompressionSettings() const
{
    pdf::PDFOCRCompressionSettings settings = getConfigurationFromUi().compression;
    settings.excludedImages = m_compressionExcludedImages;
    return settings;
}

bool PDFOCRDocumentDialog::isCompressionPreviewConfirmed() const
{
    return m_confirmedCompression.has_value() && *m_confirmedCompression == getCompressionSettings();
}

std::vector<pdf::PDFInteger> PDFOCRDocumentDialog::getPagesToWrite() const
{
    // The same selection as the writing of the layer: checked pages with a result,
    // or all pages with a result, if no checked page has one
    std::vector<pdf::PDFInteger> pages = getCheckedPages();
    std::erase_if(pages, [this](pdf::PDFInteger page)
    {
        const pdf::PDFOCRPageResult* result = m_session->getPage(page);
        return !result || !result->hasResult() || result->reviewOnly;
    });
    if (pages.empty())
    {
        pages = m_session->getPagesWithResults();
        std::erase_if(pages, [this](pdf::PDFInteger page) { return m_session->getPage(page)->reviewOnly; });
    }
    return pages;
}

void PDFOCRDocumentDialog::updateCompressionUi()
{
    const pdf::PDFOCRCompressionMode mode = pdf::PDFOCRCompressionMode(ui->compressionModeComboBox->currentData().toInt());
    const bool enabled = mode != pdf::PDFOCRCompressionMode::Off;
    const bool converts = mode == pdf::PDFOCRCompressionMode::BitonalTextScans;
    const bool custom = mode == pdf::PDFOCRCompressionMode::Custom;

    ui->compressionDescriptionLabel->setText(pdf::PDFOCRCompressionSettings::getModeDescription(mode));
    ui->bitonalAlgorithmLabel->setEnabled(enabled);
    ui->bitonalAlgorithmComboBox->setEnabled(enabled);
    for (QWidget* widget : std::initializer_list<QWidget*>{ ui->bitonalThresholdLabel, ui->bitonalThresholdComboBox, ui->bitonalThresholdSpinBox })
    {
        widget->setVisible(converts);
    }
    ui->bitonalThresholdSpinBox->setEnabled(pdf::PDFOCRThresholdMethod(ui->bitonalThresholdComboBox->currentData().toInt()) == pdf::PDFOCRThresholdMethod::Manual);
    for (QWidget* widget : std::initializer_list<QWidget*>{ ui->downsampleCheckBox, ui->downsampleDpiSpinBox, ui->jpegQualityLabel, ui->jpegQualitySpinBox })
    {
        widget->setVisible(custom);
    }
    ui->downsampleDpiSpinBox->setEnabled(ui->downsampleCheckBox->isChecked());
    ui->compressSharedImagesCheckBox->setEnabled(enabled);

    if (!enabled)
    {
        ui->compressionEstimateLabel->clear();
    }
    else if (getCompressionSettings().isLossy())
    {
        ui->compressionEstimateLabel->setText(isCompressionPreviewConfirmed() ? tr("The preview was confirmed.") : tr("The lossy compression must be confirmed in the preview."));
    }
}

void PDFOCRDocumentDialog::onCompressionSettingsChanged()
{
    if (m_updatingUi)
    {
        return;
    }

    onConfigurationChanged();
    updateCompressionUi();
    scheduleCompressionEstimate();
    updateUi();
}

void PDFOCRDocumentDialog::onCompressionPreviewClicked()
{
    const std::vector<pdf::PDFInteger> pages = getPagesToWrite();
    if (pages.empty())
    {
        QMessageBox::information(this, tr("Preview of the Compression"), tr("There is no recognized page, whose images could be compressed."));
        return;
    }

    PDFOCRCompressionPreviewDialog dialog(m_context.document, pages, m_currentPage, getCompressionSettings(), this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    // The threshold and the excluded images of the preview are taken over and confirmed
    const pdf::PDFOCRCompressionSettings& settings = dialog.getSettings();
    m_compressionExcludedImages = settings.excludedImages;
    m_updatingUi = true;
    ui->bitonalThresholdComboBox->setCurrentIndex(qMax(0, ui->bitonalThresholdComboBox->findData(int(settings.thresholdMethod))));
    ui->bitonalThresholdSpinBox->setValue(settings.manualThreshold);
    m_updatingUi = false;
    onConfigurationChanged();

    m_confirmedCompression = getCompressionSettings();
    updateCompressionUi();
    scheduleCompressionEstimate();
    updateUi();
}

void PDFOCRDocumentDialog::scheduleCompressionEstimate()
{
    if (!getCompressionSettings().isEnabled())
    {
        cancelTask(m_compressionEstimateTask);
        ui->compressionEstimateLabel->clear();
        return;
    }
    m_compressionEstimateTimer.start();
}

void PDFOCRDocumentDialog::startCompressionEstimate()
{
    const pdf::PDFOCRCompressionSettings settings = getCompressionSettings();
    const std::vector<pdf::PDFInteger> pages = getPagesToWrite();
    if (!settings.isEnabled() || pages.empty())
    {
        cancelTask(m_compressionEstimateTask);
        ui->compressionEstimateLabel->clear();
        return;
    }

    const pdf::PDFDocument* document = m_context.document;
    const bool lossyNotConfirmed = settings.isLossy() && !isCompressionPreviewConfirmed();
    ui->compressionEstimateLabel->setText(tr("Estimating the size..."));

    startTask(m_compressionEstimateTask, [this, document, pages, settings, lossyNotConfirmed](int generation, const pdf::PDFOperationControl* operationControl)
    {
        // Size of all images of the pages (without decoding) and a sample of up to three pages,
        // whose images are really compressed; the result is an extrapolation of the sample
        const std::map<pdf::PDFObjectReference, std::vector<pdf::PDFInteger>> usage = pdf::PDFOCRImageCompressor::getImageUsage(document);
        const std::set<pdf::PDFInteger> pageSet(pages.begin(), pages.end());
        qint64 totalBytes = 0;
        for (const auto& [reference, imagePages] : usage)
        {
            if (std::any_of(imagePages.begin(), imagePages.end(), [&pageSet](pdf::PDFInteger page) { return pageSet.count(page) > 0; }))
            {
                const pdf::PDFObject& object = document->getObjectByReference(reference);
                if (object.isStream() && object.getStream()->getContent())
                {
                    totalBytes += object.getStream()->getContent()->size();
                }
            }
        }

        std::vector<pdf::PDFInteger> sample;
        const size_t sampleCount = qMin<size_t>(3, pages.size());
        for (size_t i = 0; i < sampleCount; ++i)
        {
            sample.push_back(pages[i * pages.size() / sampleCount]);
        }

        qint64 sampleOriginal = 0;
        qint64 sampleNew = 0;
        std::set<pdf::PDFObjectReference> counted;
        for (pdf::PDFInteger page : sample)
        {
            if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
            {
                return;
            }

            for (const pdf::PDFOCRImageCompressor::Preview& preview : pdf::PDFOCRImageCompressor::createPreview(document, page, pages, settings, operationControl))
            {
                if (counted.insert(preview.result.reference).second)
                {
                    sampleOriginal += preview.result.originalBytes;
                    sampleNew += preview.result.action == pdf::PDFOCRCompressionImageResult::Action::Compressed ? preview.result.newBytes : preview.result.originalBytes;
                }
            }
        }

        if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        QString text;
        if (totalBytes == 0 || sampleOriginal == 0)
        {
            text = tr("The pages to write have no image, which could be compressed.");
        }
        else
        {
            const qint64 estimate = qint64(double(totalBytes) * double(sampleNew) / double(sampleOriginal));
            text = tr("Images of %n page(s) to write: %1 -> about %2 (estimated from %3 page(s)).", nullptr, int(pages.size()))
                       .arg(pdf::PDFOCRCompressionReport::formatBytes(totalBytes), pdf::PDFOCRCompressionReport::formatBytes(estimate)).arg(sample.size());
        }
        if (lossyNotConfirmed)
        {
            text += QChar(' ') + tr("The lossy compression must be confirmed in the preview.");
        }

        Q_EMIT compressionEstimateReady(generation, text);
    });
}

void PDFOCRDocumentDialog::onCompressionEstimateReady(int generation, QString text)
{
    if (generation == m_compressionEstimateTask.generation)
    {
        ui->compressionEstimateLabel->setText(text);
    }
}

void PDFOCRDocumentDialog::addSelectedWordToUserWords()
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRWord* word = page ? page->findWord(m_selectedWordId) : nullptr;
    if (!word)
    {
        return;
    }

    // The user word is the text without the surrounding punctuation, the case is kept
    QString text = word->text;
    while (!text.isEmpty() && !text.front().isLetterOrNumber())
    {
        text.remove(0, 1);
    }
    while (!text.isEmpty() && !text.back().isLetterOrNumber())
    {
        text.chop(1);
    }
    if (text.isEmpty())
    {
        return;
    }

    const int wordId = word->id;
    const pdf::PDFOCRReviewCriteria criteria = m_session->getReviewCriteria();
    if (!criteria.isAcceptedWord(text))
    {
        QString words = ui->userWordsEdit->toPlainText();
        if (!words.isEmpty() && !words.endsWith(QChar('\n')))
        {
            words += QChar('\n');
        }
        words += text;
        ui->userWordsEdit->setPlainText(words);
    }

    if (isPageEditable(m_currentPage))
    {
        const pdf::PDFOCRPageResult* currentPage = m_session->getPage(m_currentPage);
        const pdf::PDFOCRWord* currentWord = currentPage ? currentPage->findWord(wordId) : nullptr;
        if (currentWord && currentWord->reviewState == pdf::PDFOCRReviewState::Unreviewed)
        {
            m_session->setWordReviewState(m_currentPage, wordId, pdf::PDFOCRReviewState::Confirmed);
        }
    }
}

void PDFOCRDocumentDialog::showRegionContextMenu(int regionId, QPoint globalPosition)
{
    m_originalView->setSelectedRegion(regionId);
    m_workingView->setSelectedRegion(regionId);
    updateUi();

    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page || !page->findRegion(regionId))
    {
        return;
    }

    QMenu menu(this);
    QAction* propertiesAction = menu.addAction(tr("Region Properties..."));
    QAction* removeAction = menu.addAction(tr("Remove Region"));
    menu.addSeparator();
    QAction* recognizeRegionAction = menu.addAction(tr("Re-recognize Region"));

    const bool running = m_jobController->isRunning();
    propertiesAction->setEnabled(!running);
    removeAction->setEnabled(!running);
    recognizeRegionAction->setEnabled(canRerecognizeRegion(regionId));

    QAction* action = menu.exec(globalPosition);
    if (action == propertiesAction)
    {
        onRegionProperties();
    }
    else if (action == removeAction)
    {
        onRemoveRegion();
    }
    else if (action == recognizeRegionAction)
    {
        rerecognizeRegion(regionId);
    }
}

int PDFOCRDocumentDialog::getRegionIdOfSelection() const
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    if (!page)
    {
        return -1;
    }

    const pdf::PDFOCRLine* line = m_selectedWordId ? page->findLineOfWord(m_selectedWordId) : page->findLine(m_selectedLineId);
    for (const pdf::PDFOCRBlock& block : page->blocks)
    {
        const bool isSelected = line ? std::any_of(block.lines.begin(), block.lines.end(), [line](const pdf::PDFOCRLine& item) { return item.id == line->id; })
                                     : (m_selectedBlockId != 0 && block.id == m_selectedBlockId);
        if (isSelected)
        {
            return block.regionId;
        }
    }
    return -1;
}

bool PDFOCRDocumentDialog::canRerecognizeRegion(int regionId) const
{
    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRRegion* region = page ? page->findRegion(regionId) : nullptr;
    return region && region->type == pdf::PDFOCRRegionType::Recognize && page->hasResult() && isPageEditable(m_currentPage) &&
           !isBusy() && ui->engineComboBox->count() > 0;
}

void PDFOCRDocumentDialog::rerecognizeRegion(int regionId)
{
    if (!canRerecognizeRegion(regionId))
    {
        return;
    }

    // Temporary inclusive region equal to the region with its configuration exception (REGION-03)
    m_rerecognizePage = m_currentPage;
    m_rerecognizeRegionId = regionId;
    m_rerecognizeLineId = 0;
    m_rerecognizeWordId = 0;
    startRecognition({ m_currentPage }, RunMode::Region);
}

// -------------------------------------------------------------------------
// Output
// -------------------------------------------------------------------------

void PDFOCRDocumentDialog::onApplyClicked()
{
    if (isBusy())
    {
        return;
    }

    const OutputMode outputMode = OutputMode(ui->outputModeComboBox->currentData().toInt());
    if (outputMode == OutputMode::ExportOnly)
    {
        QMessageBox::information(this, windowTitle(), tr("The output mode is 'Only export the recognized text'. Use the button 'Export...', or change the output mode on the tab 'Output'."));
        return;
    }

    if (m_engineCapabilities.isExportOnly)
    {
        QMessageBox::information(this, windowTitle(), tr("The selected engine provides the text without exact geometry; its results can only be exported."));
        return;
    }

    const pdf::PDFOCRApplyProcessor::OutputMode processorMode = outputMode == OutputMode::CreateCopy ? pdf::PDFOCRApplyProcessor::OutputMode::CreateCopy
                                                                                                      : pdf::PDFOCRApplyProcessor::OutputMode::ModifyDocument;
    const pdf::PDFOCRApplyProcessor::Context applyContext = getApplyContext();

    // Permissions of the document and the certification (PDF-12), conformance declaration (PDF-15)
    const QString permissionError = pdf::PDFOCRApplyProcessor::checkPermissions(applyContext, processorMode);
    if (!permissionError.isEmpty())
    {
        QMessageBox::warning(this, windowTitle(), permissionError);
        return;
    }

    // The lossy compression is applied only after the preview was confirmed
    const pdf::PDFOCRCompressionSettings compression = getCompressionSettings();
    if (compression.isLossy() && !isCompressionPreviewConfirmed())
    {
        QMessageBox::information(this, windowTitle(), tr("The compression of the scanned images is lossy. Check its result in the preview and confirm it before the text layer is written."));
        return;
    }

    // Pages with a valid result (PAGE-05, PDF-02)
    std::vector<pdf::PDFInteger> candidates = getCheckedPages();
    std::erase_if(candidates, [this](pdf::PDFInteger page) { const pdf::PDFOCRPageResult* result = m_session->getPage(page); return !result || !result->hasResult(); });
    bool usedAllResults = false;
    if (candidates.empty())
    {
        candidates = m_session->getPagesWithResults();
        usedAllResults = true;
    }

    // Only the cached analysis and fingerprints are used here; the layer information and
    // the missing fingerprints are read by the apply worker, outside of the GUI thread (R12)
    pdf::PDFOCRApplyProcessor::Request request;
    request.outputMode = processorMode;
    request.usedAllResults = usedAllResults;
    request.reviewCriteria = m_session->getReviewCriteria();
    request.writerOptions.keepReviewData = ui->keepReviewDataCheckBox->isChecked();
    request.writerOptions.onlyReviewed = ui->onlyReviewedCheckBox->isChecked();
    request.compression = compression;
    request.memoryBudget = m_session->getConfiguration().memoryBudget;
    for (pdf::PDFInteger pageIndex : candidates)
    {
        request.results.push_back(*m_session->getPage(pageIndex));

        auto analysisIt = m_analysis.find(pageIndex);
        if (analysisIt != m_analysis.end())
        {
            request.analysis[pageIndex] = analysisIt->second;
        }

        auto fingerprintIt = m_fingerprints.find(pageIndex);
        if (fingerprintIt != m_fingerprints.end())
        {
            request.fingerprints[pageIndex] = fingerprintIt->second;
        }
    }

    pdf::PDFOCRApplyProcessor::Plan plan = pdf::PDFOCRApplyProcessor::createPlan(applyContext, request);
    if (!plan.hasRequests())
    {
        QMessageBox::information(this, windowTitle(), tr("There is no result, which can be written into the PDF.\n\n%1").arg(plan.excluded.join(QChar('\n'))));
        return;
    }

    if (outputMode == OutputMode::CreateCopy)
    {
        QFileInfo fileInfo(m_context.fileName);
        const QString suggestion = fileInfo.absolutePath() + QStringLiteral("/") + fileInfo.completeBaseName() + QStringLiteral("_ocr.pdf");
        const QString copyFileName = QFileDialog::getSaveFileName(this, tr("Create a Copy of the Document with OCR"), suggestion, tr("Portable Document (*.pdf)"));
        if (copyFileName.isEmpty())
        {
            return;
        }

        const QString copyError = pdf::PDFOCRApplyProcessor::setCopyFileName(plan, applyContext, copyFileName);
        if (!copyError.isEmpty())
        {
            QMessageBox::warning(this, windowTitle(), copyError);
            return;
        }
    }

    // Summary before the application (PDF-02)
    QMessageBox messageBox(QMessageBox::Question, tr("Apply to PDF"), pdf::PDFOCRApplyProcessor::getSummary(plan).join(QChar('\n')), QMessageBox::NoButton, this);
    messageBox.setInformativeText(pdf::PDFOCRApplyProcessor::getWarnings(plan, applyContext).join(QStringLiteral("\n\n")));
    if (!plan.excluded.isEmpty())
    {
        messageBox.setDetailedText(tr("Pages, which are not written:\n%1").arg(plan.excluded.join(QChar('\n'))));
    }
    QPushButton* applyButton = messageBox.addButton(tr("Apply"), QMessageBox::AcceptRole);
    messageBox.addButton(QMessageBox::Cancel);
    messageBox.exec();
    if (messageBox.clickedButton() != applyButton)
    {
        return;
    }

    m_applyInProgress = true;
    ui->progressBar->setRange(0, 0);
    ui->progressLabel->setText(compression.isEnabled() ? tr("Compressing the images and writing the text layer...") : tr("Writing the text layer..."));
    updateUi();

    // The change is prepared above an immutable snapshot and attached only after a successful validation (PDF-03)
    startTask(m_applyTask, [this, applyContext, plan](int generation, const pdf::PDFOperationControl* operationControl)
    {
        pdf::PDFOCRApplyProcessor::Result processorResult = pdf::PDFOCRApplyProcessor::execute(applyContext, plan, operationControl);
        if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        ApplyResult result;
        result.document = std::move(processorResult.document);
        result.report = std::move(processorResult.report);
        result.errorMessage = std::move(processorResult.errorMessage);
        result.copyFileName = std::move(processorResult.copyFileName);
        result.conformanceRemoved = processorResult.conformanceRemoved;
        result.compressionReport = std::move(processorResult.compressionReport);
        result.fingerprints = std::move(processorResult.fingerprints);

        {
            QMutexLocker lock(&m_applyMutex);
            m_applyResult = std::move(result);
        }
        Q_EMIT applyFinished(generation);
    });
}

void PDFOCRDocumentDialog::onRemoveLayerClicked()
{
    if (isBusy())
    {
        return;
    }

    if (!m_context.canModify)
    {
        QMessageBox::warning(this, windowTitle(), tr("The permissions of the document do not allow its modification."));
        return;
    }

    if (m_context.certificationPermissions > 0)
    {
        // The removal changes the content of the certified document (PDF-12)
        QMessageBox::warning(this, windowTitle(), tr("The document is certified; removing the text layer would invalidate the certification."));
        return;
    }

    // The cached analysis is used (R12); a page, which is not analyzed yet, is checked by the worker
    std::vector<pdf::PDFInteger> pages;
    for (pdf::PDFInteger page : getCheckedPages())
    {
        auto it = m_analysis.find(page);
        if (it == m_analysis.end() || it->second.hasOwnOCRLayer)
        {
            pages.push_back(page);
        }
    }

    if (pages.empty())
    {
        QMessageBox::information(this, windowTitle(), tr("No checked page contains an OCR layer created by PDF4QT."));
        return;
    }

    QString question = tr("Do you want to remove the OCR text layer created by PDF4QT from the pages %1, including its private data? The original content and the text of other tools are not removed.").arg(pdf::PDFOCRPageSelection::describe(pages));
    if (m_context.hasSignatures)
    {
        question += QStringLiteral("\n\n") + tr("The document is signed; the change of the content may invalidate the state of the signatures.");
    }
    question += QStringLiteral("\n\n") + tr("Note: an incremental save can keep the previous revision of the document in the file. To remove sensitive content safely, use the redaction and a full save.");

    if (QMessageBox::question(this, tr("Remove OCR Layer"), question) != QMessageBox::Yes)
    {
        return;
    }

    m_applyInProgress = true;
    ui->progressBar->setRange(0, 0);
    ui->progressLabel->setText(tr("Removing the text layer..."));
    updateUi();

    const pdf::PDFOCRApplyProcessor::Context applyContext = getApplyContext();
    startTask(m_applyTask, [this, applyContext, pages](int generation, const pdf::PDFOperationControl* operationControl)
    {
        pdf::PDFOCRApplyProcessor::Result processorResult = pdf::PDFOCRApplyProcessor::removeLayers(applyContext, pages, operationControl);
        if (pdf::PDFOperationControl::isOperationCancelled(operationControl))
        {
            return;
        }

        ApplyResult result;
        result.isRemoval = true;
        result.document = std::move(processorResult.document);
        result.report = std::move(processorResult.report);
        result.errorMessage = std::move(processorResult.errorMessage);

        {
            QMutexLocker lock(&m_applyMutex);
            m_applyResult = std::move(result);
        }
        Q_EMIT applyFinished(generation);
    });
}

void PDFOCRDocumentDialog::onApplyFinished(int generation)
{
    if (generation != m_applyTask.generation)
    {
        return;
    }

    ApplyResult result;
    {
        QMutexLocker lock(&m_applyMutex);
        result = std::move(m_applyResult);
        m_applyResult = ApplyResult();
    }

    m_applyInProgress = false;
    ui->progressBar->setRange(0, 1);
    ui->progressBar->setValue(1);

    if (!result.errorMessage.isEmpty())
    {
        // The document is not modified partially (PDF-03)
        ui->progressLabel->setText(tr("The text layer was not written."));
        updateUi();
        QMessageBox::critical(this, windowTitle(), tr("The text layer was not written, the document was not changed.\n\n%1\n\nYou can uncheck the failing pages and repeat the action.").arg(result.errorMessage));
        return;
    }

    QStringList messages = result.report.messages;
    if (!result.compressionReport.images.empty())
    {
        messages.prepend(result.compressionReport.getSummary());
    }
    if (!result.report.unchangedPages.empty())
    {
        messages << tr("Pages with an identical layer (not changed): %1").arg(pdf::PDFOCRPageSelection::describe(result.report.unchangedPages));
    }
    if (!result.report.skippedPages.empty())
    {
        messages << tr("Pages without text to write: %1").arg(pdf::PDFOCRPageSelection::describe(result.report.skippedPages));
    }
    if (!result.report.removedPages.empty())
    {
        messages << tr("Pages without text, whose obsolete OCR layer was removed: %1").arg(pdf::PDFOCRPageSelection::describe(result.report.removedPages));
    }

    if (!result.copyFileName.isEmpty())
    {
        ui->progressLabel->setText(tr("The copy with OCR was created."));
        updateUi();
        QMessageBox messageBox(QMessageBox::Information, windowTitle(), tr("The copy of the document with the text layer on %n page(s) was saved to:\n%1", nullptr, int(result.report.writtenPages.size())).arg(QDir::toNativeSeparators(result.copyFileName)), QMessageBox::Ok, this);
        messageBox.setDetailedText(messages.join(QChar('\n')));
        messageBox.exec();
        return;
    }

    if (!result.document && result.isRemoval)
    {
        ui->progressLabel->setText(tr("Nothing was removed."));
        updateUi();
        QMessageBox::information(this, windowTitle(), tr("No checked page contains an OCR layer created by PDF4QT."));
        return;
    }

    if (!result.document)
    {
        // Idempotent operation: no new history step is created (PDF-11)
        ui->progressLabel->setText(tr("The document already contains this text layer, nothing was changed."));
        updateUi();
        QMessageBox::information(this, windowTitle(), tr("The document already contains the identical text layer. Nothing was changed.\n\n%1").arg(messages.join(QChar('\n'))));
        return;
    }

    m_modifiedDocument = result.document;
    ui->progressLabel->setText(result.isRemoval ? tr("The text layer was removed.") : tr("The text layer was written."));

    // The compression changes the fingerprints of the pages, the results (and a project
    // saved now) are bound to the new revision of the pages
    m_session->rebindPageFingerprints(result.fingerprints);
    for (const auto& [pageIndex, fingerprint] : result.fingerprints)
    {
        m_fingerprints[pageIndex] = fingerprint;
    }

    QString text = result.isRemoval ? tr("The OCR layer was removed from %n page(s).", nullptr, int(result.report.writtenPages.size()))
                                    : tr("The invisible text layer with %1 words was written on %n page(s).", nullptr, int(result.report.writtenPages.size())).arg(result.report.writtenWords);
    if (result.compressionReport.isChanged())
    {
        text += QStringLiteral("\n\n") + result.compressionReport.getSummary();
    }
    text += QStringLiteral("\n\n") + tr("The dialog will be closed and the document of the editor will be updated in a single undo step. The file on the disk is changed by the standard Save command.");
    if (m_session->isDirty() && !result.isRemoval)
    {
        text += QStringLiteral("\n\n") + tr("The confidence scores and the history of the corrections are kept only in the OCR project. Do you want to save the project now?");
        QMessageBox messageBox(QMessageBox::Information, windowTitle(), text, QMessageBox::Yes | QMessageBox::No, this);
        messageBox.setDetailedText(messages.join(QChar('\n')));
        if (messageBox.exec() == QMessageBox::Yes)
        {
            saveProject();
        }
    }
    else
    {
        QMessageBox messageBox(QMessageBox::Information, windowTitle(), text, QMessageBox::Ok, this);
        messageBox.setDetailedText(messages.join(QChar('\n')));
        messageBox.exec();
    }

    m_session->setDirty(false);
    done(QDialog::Accepted);
}

bool PDFOCRDocumentDialog::confirmReadableTextOutput(const QString& title)
{
    if (!m_context.canCopyContent)
    {
        QMessageBox::warning(this, title, tr("The permissions of the document do not allow to copy its content, so the recognized text cannot be exported."));
        return false;
    }

    if (m_context.isEncrypted)
    {
        // No unencrypted file is created automatically for an encrypted input (EXPORT-05)
        return QMessageBox::warning(this, title, tr("The document is encrypted. The created file will contain the recognized text in a readable, unencrypted form. Passwords and keys are not stored. Do you want to continue?"),
                                    QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes;
    }

    return true;
}

void PDFOCRDocumentDialog::onExportClicked()
{
    const int exportFormat = ui->exportFormatComboBox->currentData().toInt();
    const bool isStructured = exportFormat != EXPORT_FORMAT_TEXT;
    const QString title = isStructured ? tr("Export %1").arg(ui->exportFormatComboBox->currentText()) : tr("Export Text");

    if (!confirmReadableTextOutput(title))
    {
        return;
    }

    const QStringList scopes = { tr("Current page"), tr("Checked pages"), tr("All results") };
    bool ok = false;
    const QString scope = QInputDialog::getItem(this, title, tr("Pages to export (the current corrected text in the reading order):"), scopes, 1, false, &ok);
    if (!ok)
    {
        return;
    }

    std::vector<pdf::PDFInteger> pages;
    if (scope == scopes[0])
    {
        pages = { m_currentPage };
    }
    else if (scope == scopes[1])
    {
        pages = getCheckedPages();
    }
    else
    {
        // All records, so the skipped pages and the pages with errors are reported too (EXPORT-02)
        pages = m_session->getPages();
    }

    // Pages without a record are reported as missing (EXPORT-02)
    std::vector<pdf::PDFOCRPageResult> placeholders;
    placeholders.reserve(pages.size());
    std::vector<const pdf::PDFOCRPageResult*> results;
    for (pdf::PDFInteger page : pages)
    {
        if (const pdf::PDFOCRPageResult* result = m_session->getPage(page))
        {
            results.push_back(result);
        }
        else
        {
            pdf::PDFOCRPageResult placeholder;
            placeholder.pageIndex = page;
            placeholders.push_back(placeholder);
            results.push_back(&placeholders.back());
        }
    }

    if (isStructured)
    {
        exportStructured(pdf::PDFOCRStructuredExporter::Format(exportFormat), results, title);
        return;
    }

    pdf::PDFOCRTextExporter::Options options;
    options.preserveLines = ui->preserveLinesCheckBox->isChecked();
    options.joinHyphenatedWords = ui->joinHyphenatedCheckBox->isChecked();
    options.normalizeNFC = ui->normalizeNfcCheckBox->isChecked();
    options.pageSeparator = pdf::PDFOCRTextExporter::PageSeparator(ui->pageSeparatorComboBox->currentData().toInt());
    options.onlyReviewed = false;

    pdf::PDFOCRTextExporter::Report report;
    const QString text = pdf::PDFOCRTextExporter::exportText(results, options, &report);

    if (report.exportedPages.empty())
    {
        QMessageBox::information(this, tr("Export Text"), tr("The selected pages do not contain any result to export."));
        return;
    }

    QFileInfo fileInfo(m_context.fileName);
    const QString suggestion = fileInfo.absolutePath() + QStringLiteral("/") + fileInfo.completeBaseName() + QStringLiteral("_ocr.txt");
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Export Text"), suggestion, tr("Text file, UTF-8 (*.txt)"));
    if (fileName.isEmpty())
    {
        return;
    }

    QString errorMessage;
    if (!pdf::PDFOCRTextExporter::writeTextFile(fileName, text, &errorMessage))
    {
        QMessageBox::critical(this, tr("Export Text"), errorMessage);
        return;
    }

    QStringList details;
    details << tr("Exported pages: %1").arg(report.pageDescriptions.join(QStringLiteral(", ")));
    if (!report.skippedPages.empty())
    {
        // Every missing page is listed with its state and reason (EXPORT-02)
        details << tr("Pages without a result (missing in the export): %1").arg(pdf::PDFOCRPageSelection::describe(report.skippedPages));
        details << report.skippedDescriptions;
    }
    if (!report.noTextDescriptions.isEmpty())
    {
        details << tr("Exported pages without text:");
        details << report.noTextDescriptions;
    }
    details << report.regionOrders;

    QMessageBox messageBox(QMessageBox::Information, tr("Export Text"), tr("The text of %n page(s) (%1 words) was exported.", nullptr, int(report.exportedPages.size())).arg(report.wordCount), QMessageBox::Ok, this);
    if (!report.skippedPages.empty())
    {
        messageBox.setInformativeText(tr("%n page(s) have no result and are missing in the export.", nullptr, int(report.skippedPages.size())));
    }
    messageBox.setDetailedText(details.join(QChar('\n')));
    messageBox.exec();
}

void PDFOCRDocumentDialog::exportStructured(pdf::PDFOCRStructuredExporter::Format format, const std::vector<const pdf::PDFOCRPageResult*>& results, const QString& title)
{
    pdf::PDFOCRStructuredExporter::Options options;
    options.format = format;
    options.dpi = ui->exportDpiSpinBox->value() <= EXPORT_DPI_OF_RECOGNITION ? 0.0 : ui->exportDpiSpinBox->value();
    options.includeConfidence = ui->exportConfidenceCheckBox->isChecked();
    options.normalizeNFC = ui->normalizeNfcCheckBox->isChecked();
    options.title = QFileInfo(m_context.fileName).fileName();

    // The pages with a result are exported, the others are reported (EXPORT-02)
    pdf::PDFOCRTextExporter::Report report;
    const QByteArray allPages = pdf::PDFOCRStructuredExporter::exportPages(m_context.document, results, options, &report);
    if (report.exportedPages.empty())
    {
        QMessageBox::information(this, title, tr("The selected pages do not contain any result to export."));
        return;
    }

    const bool perPage = ui->exportPerPageCheckBox->isChecked();
    const QString suffix = pdf::PDFOCRStructuredExporter::getFileSuffix(format);
    QFileInfo fileInfo(m_context.fileName);
    const QString suggestion = fileInfo.absolutePath() + QStringLiteral("/") + fileInfo.completeBaseName() + QStringLiteral("_ocr.") + suffix;
    QString fileName = QFileDialog::getSaveFileName(this, title, suggestion, pdf::PDFOCRStructuredExporter::getFileFilter(format));
    if (fileName.isEmpty())
    {
        return;
    }
    if (QFileInfo(fileName).suffix().isEmpty())
    {
        fileName += QChar('.') + suffix;
    }

    QStringList writtenFiles;
    QString errorMessage;
    if (perPage)
    {
        const pdf::PDFInteger pageCount = pdf::PDFInteger(m_context.document->getCatalog()->getPageCount());
        for (const pdf::PDFOCRPageResult* result : results)
        {
            if (std::find(report.exportedPages.begin(), report.exportedPages.end(), result->pageIndex) == report.exportedPages.end())
            {
                continue;
            }

            const QString pageFileName = pdf::PDFOCRStructuredExporter::getPageFileName(fileName, result->pageIndex, pageCount);
            const QByteArray pageData = pdf::PDFOCRStructuredExporter::exportPages(m_context.document, { result }, options, nullptr);
            if (!pdf::PDFOCRStructuredExporter::writeFile(pageFileName, pageData, &errorMessage))
            {
                break;
            }
            writtenFiles << QDir::toNativeSeparators(pageFileName);
        }
    }
    else if (pdf::PDFOCRStructuredExporter::writeFile(fileName, allPages, &errorMessage))
    {
        writtenFiles << QDir::toNativeSeparators(fileName);
    }

    if (!errorMessage.isEmpty())
    {
        QMessageBox::critical(this, title, errorMessage);
        return;
    }

    QStringList details;
    details << tr("Exported pages: %1").arg(report.pageDescriptions.join(QStringLiteral(", ")));
    details << tr("Files: %1").arg(writtenFiles.join(QStringLiteral(", ")));
    details << (options.dpi > 0.0 ? tr("Coordinates: pixels of the visible page at %1 DPI.").arg(qRound(options.dpi))
                                  : tr("Coordinates: pixels of the visible page at the resolution of the recognition of each page."));
    if (!report.skippedPages.empty())
    {
        details << tr("Pages without a result (missing in the export): %1").arg(pdf::PDFOCRPageSelection::describe(report.skippedPages));
        details << report.skippedDescriptions;
    }
    if (!report.noTextDescriptions.isEmpty())
    {
        details << tr("Exported pages without text:");
        details << report.noTextDescriptions;
    }

    QMessageBox messageBox(QMessageBox::Information, title, tr("%n page(s) (%1 words) were exported into %2 file(s).", nullptr, int(report.exportedPages.size())).arg(report.wordCount).arg(writtenFiles.size()), QMessageBox::Ok, this);
    if (!report.skippedPages.empty())
    {
        messageBox.setInformativeText(tr("%n page(s) have no result and are missing in the export.", nullptr, int(report.skippedPages.size())));
    }
    messageBox.setDetailedText(details.join(QChar('\n')));
    messageBox.exec();
}

bool PDFOCRDocumentDialog::saveProject()
{
    if (!confirmReadableTextOutput(tr("Save OCR Project")))
    {
        return false;
    }

    QString fileName = m_projectFileName;
    if (fileName.isEmpty())
    {
        QFileInfo fileInfo(m_context.fileName);
        fileName = fileInfo.absolutePath() + QStringLiteral("/") + fileInfo.completeBaseName() + QStringLiteral(".") + QLatin1String(pdf::PDFOCRProject::FILE_EXTENSION);
    }

    fileName = QFileDialog::getSaveFileName(this, tr("Save OCR Project"), fileName, tr("PDF4QT OCR project (*.%1)").arg(QLatin1String(pdf::PDFOCRProject::FILE_EXTENSION)));
    if (fileName.isEmpty())
    {
        return false;
    }

    // The fingerprints, which are not computed yet, are computed outside of the GUI thread (R12)
    ensureFingerprints(m_session->getPages(), true);
    pdf::PDFOCRProject project = m_session->createProject(getCheckedPages());

    // Results are bound to the fingerprints of the pages, not only to the file name (EXPORT-04)
    for (auto& item : project.pages)
    {
        if (item.second.pageFingerprint.isEmpty())
        {
            auto it = m_fingerprints.find(item.first);
            if (it != m_fingerprints.end())
            {
                item.second.pageFingerprint = it->second;
            }
        }
    }

    QString errorMessage;
    if (!pdf::PDFOCRProjectSerializer::save(project, fileName, &errorMessage))
    {
        QMessageBox::critical(this, tr("Save OCR Project"), errorMessage);
        return false;
    }

    m_projectFileName = fileName;
    m_session->setDirty(false);
    ui->progressLabel->setText(tr("The project was saved. It contains the recognized and corrected texts including the original recognitions, not the PDF document or page images."));
    return true;
}

void PDFOCRDocumentDialog::onSaveProject()
{
    saveProject();
}

void PDFOCRDocumentDialog::onOpenProject()
{
    if (isBusy())
    {
        return;
    }

    if (m_session->isDirty() && QMessageBox::question(this, tr("Open OCR Project"), tr("The current results and corrections are not saved and will be replaced by the project. Do you want to continue?")) != QMessageBox::Yes)
    {
        return;
    }

    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open OCR Project"), QFileInfo(m_context.fileName).absolutePath(), tr("PDF4QT OCR project (*.%1)").arg(QLatin1String(pdf::PDFOCRProject::FILE_EXTENSION)));
    if (fileName.isEmpty())
    {
        return;
    }

    pdf::PDFOCRProject project;
    QString errorMessage;
    if (!pdf::PDFOCRProjectSerializer::load(fileName, project, &errorMessage))
    {
        QMessageBox::critical(this, tr("Open OCR Project"), errorMessage);
        return;
    }

    // The fingerprints, which are not computed yet, are computed outside of the GUI thread (R12)
    std::vector<pdf::PDFInteger> projectPages;
    for (const auto& item : project.pages)
    {
        projectPages.push_back(item.first);
    }
    ensureFingerprints(projectPages, true);

    const pdf::PDFOCRProjectSerializer::MatchResult match = pdf::PDFOCRProjectSerializer::match(project, m_session->getDocumentIdentity(), [this](pdf::PDFInteger page)
    {
        auto it = m_fingerprints.find(page);
        return it != m_fingerprints.end() ? it->second : QByteArray();
    });

    std::vector<pdf::PDFInteger> pagesToLoad = match.matchingPages;
    std::vector<pdf::PDFInteger> reviewOnly;

    if (!match.changedPages.empty() || !match.missingPages.empty())
    {
        // Results of a changed document are never applied automatically (EXPORT-04)
        QMessageBox messageBox(QMessageBox::Warning, tr("Open OCR Project"), tr("The project does not fully match the opened document."), QMessageBox::NoButton, this);
        messageBox.setInformativeText(tr("Matching pages: %1\nPages with a different content: %2\nPages missing in the document: %3\n\nPages with a different content can be loaded for review and export only; they cannot be written into this PDF.")
                                      .arg(match.matchingPages.empty() ? tr("none") : pdf::PDFOCRPageSelection::describe(match.matchingPages),
                                           match.changedPages.empty() ? tr("none") : pdf::PDFOCRPageSelection::describe(match.changedPages),
                                           match.missingPages.empty() ? tr("none") : pdf::PDFOCRPageSelection::describe(match.missingPages)));
        QPushButton* matchingButton = messageBox.addButton(tr("Load Matching Pages Only"), QMessageBox::AcceptRole);
        QPushButton* allButton = match.changedPages.empty() ? nullptr : messageBox.addButton(tr("Load All for Review/Export"), QMessageBox::ActionRole);
        messageBox.addButton(QMessageBox::Cancel);
        messageBox.exec();

        if (allButton && messageBox.clickedButton() == allButton)
        {
            reviewOnly = match.changedPages;
            pagesToLoad.insert(pagesToLoad.end(), reviewOnly.begin(), reviewOnly.end());
        }
        else if (messageBox.clickedButton() != matchingButton)
        {
            return;
        }
    }

    // A profile of another engine must not be loaded with silently ignored options (REC-02)
    if (ui->engineComboBox->findData(project.configuration.engineId) == -1)
    {
        const QString engineId = ui->engineComboBox->count() > 0 ? ui->engineComboBox->itemData(0).toString() : QStringLiteral("tesseract");
        QMessageBox::information(this, tr("Open OCR Project"), tr("The project was created with the engine '%1', which is not available. The results can be reviewed and exported; a new recognition uses the settings of the available engine.").arg(project.configuration.engineId));
        pdf::PDFOCRConfiguration configuration = m_session->getConfiguration();
        configuration.engineId = engineId;
        project.configuration = configuration;
    }

    m_session->loadProject(project, pagesToLoad);

    // Results of the changed pages must never be written into this document; the flag of
    // the other pages is loaded from the project and is not recomputed (R03, EXPORT-04)
    const bool wasDirty = m_session->isDirty();
    for (pdf::PDFInteger page : reviewOnly)
    {
        m_session->setPageReviewOnly(page, true);
    }
    m_session->setDirty(wasDirty);

    m_projectFileName = fileName;
    showReviewPanel(true);
    setConfigurationToUi(m_session->getConfiguration());
    if (!project.selectedPages.empty())
    {
        std::vector<pdf::PDFInteger> selected = project.selectedPages;
        std::erase_if(selected, [this](pdf::PDFInteger page) { return page < 0 || page >= m_pageCount; });
        setCheckedPages(selected);
    }

    for (pdf::PDFInteger page = 0; page < m_pageCount; ++page)
    {
        updatePageItem(page);
    }
    onCurrentPageChanged();
    ui->progressLabel->setText(tr("The project was loaded: %n page(s).", nullptr, int(pagesToLoad.size())));
}

// -------------------------------------------------------------------------
// Common
// -------------------------------------------------------------------------

void PDFOCRDocumentDialog::showReviewPanel(bool show)
{
    const int total = qMax(100, ui->rightSplitter->height());
    if (show)
    {
        ui->rightSplitter->setSizes({ total * 22 / 100, total * 78 / 100 });
    }
    else
    {
        ui->rightSplitter->setSizes({ total, 0 });
    }
}

bool PDFOCRDocumentDialog::isBusy() const
{
    return m_jobController->isRunning() || m_applyInProgress || m_pendingRecognition.has_value() || m_blockingTaskInProgress;
}

void PDFOCRDocumentDialog::updateWorkflowLabel()
{
    // Four distinct phases of the workflow (UI-03); the active phase is marked by the text style and by an arrow
    int phase = 0;
    if (m_jobController->isRunning())
    {
        phase = 1;
    }
    else if (m_applyInProgress)
    {
        phase = 3;
    }
    else if (!m_session->getPagesWithResults().empty())
    {
        phase = 2;
    }

    const QStringList phases = { tr("1. Set up"), tr("2. Recognize"), tr("3. Review and correct"), tr("4. Apply to PDF / Export") };
    QStringList parts;
    for (int i = 0; i < phases.size(); ++i)
    {
        parts << (i == phase ? QStringLiteral("<b>&#9654; %1</b>").arg(phases[i]) : phases[i]);
    }
    ui->workflowLabel->setText(parts.join(QStringLiteral(" &nbsp;&rarr;&nbsp; ")));
}

void PDFOCRDocumentDialog::updateUi()
{
    const bool running = m_jobController->isRunning();
    const bool busy = isBusy();
    const bool hasEngine = ui->engineComboBox->count() > 0;
    const bool hasResults = !m_session->getPagesWithResults().empty();
    const bool hasChecked = !getCheckedPages().empty();
    const bool editable = isPageEditable(m_currentPage);

    const pdf::PDFOCRPageResult* page = m_session->getPage(m_currentPage);
    const pdf::PDFOCRWord* word = page ? page->findWord(m_selectedWordId) : nullptr;
    const bool hasLine = page && (word || page->findLine(m_selectedLineId));

    if (!hasEngine)
    {
        // A missing engine blocks only a new recognition (UI-02)
        ui->engineWarningLabel->setText(tr("No OCR engine is available in this build, so a new recognition cannot be started. Saved OCR projects can be opened, reviewed, exported and applied, and the language models can be managed."));
        ui->engineWarningLabel->setVisible(true);
    }

    // The labels distinguish the settings of the running job and of the next run (UI-06)
    const bool preparing = m_pendingRecognition.has_value();
    ui->recognizeButton->setText((running || preparing) ? tr("Recognizing...") : tr("Recogni&ze Checked"));
    ui->recognizeButton->setEnabled(!busy && hasEngine && hasChecked);
    ui->recognizeButton->setToolTip(running ? tr("A recognition is running. Changes of the settings apply to the next run only.") : tr("Recognize the checked pages. The document is not modified."));
    ui->stopButton->setEnabled((running && !m_jobController->isStopping()) || preparing);
    ui->manageLanguagesButton->setEnabled(!preparing);
    ui->settingsTabWidget->setToolTip(running ? tr("The running recognition uses the settings from its start. Changes apply to the next run.") : QString());

    // An engine without exact geometry is offered for the export only (ARCH-02, ENGINE-01)
    const bool exportOnly = m_engineCapabilities.isExportOnly;
    const QString exportOnlyToolTip = tr("The selected engine provides the text without exact geometry; its results can only be exported.");
    const bool compressionNeedsPreview = getCompressionSettings().isLossy() && !isCompressionPreviewConfirmed();
    ui->applyButton->setEnabled(!busy && hasResults && !exportOnly && !compressionNeedsPreview);
    ui->applyButton->setToolTip(exportOnly ? exportOnlyToolTip
                                           : (compressionNeedsPreview ? tr("The lossy compression of the images must be checked and confirmed in the preview first (tab Output, button Preview).")
                                                                      : tr("Write the invisible text layer into the document")));
    ui->compressionPreviewButton->setEnabled(hasResults && !m_applyInProgress);
    ui->removeLayerButton->setEnabled(!busy && hasChecked && m_context.canModify && m_context.certificationPermissions == 0 && !exportOnly);
    ui->removeLayerButton->setToolTip(exportOnly ? exportOnlyToolTip : tr("Removes the OCR text layer created by PDF4QT including its private data. Other content is never removed."));
    ui->exportButton->setEnabled(hasResults && !m_applyInProgress);
    ui->saveProjectButton->setEnabled(!m_applyInProgress && (hasResults || m_session->isDirty()));
    ui->openProjectButton->setEnabled(!busy);
    ui->closeButton->setEnabled(!m_applyInProgress);

    ui->undoButton->setEnabled(m_session->canUndo() && !m_applyInProgress);
    ui->redoButton->setEnabled(m_session->canRedo() && !m_applyInProgress);
    ui->undoButton->setToolTip(m_session->canUndo() ? tr("Undo: %1").arg(m_session->getUndoText()) : tr("Undo the last correction"));
    ui->redoButton->setToolTip(m_session->canRedo() ? tr("Redo: %1").arg(m_session->getRedoText()) : tr("Redo the correction"));

    ui->inspectorGroupBox->setEnabled(page != nullptr);
    ui->wordTextEdit->setEnabled(word && editable);
    ui->lineTextEdit->setEnabled(hasLine && editable);
    ui->confirmNextButton->setEnabled(hasResults);
    ui->skipButton->setEnabled(hasResults);
    ui->previousReviewButton->setEnabled(hasResults);
    ui->restoreOriginalButton->setEnabled(word && editable);
    ui->notTextButton->setEnabled(word && editable);
    ui->rerecognizeButton->setEnabled(hasLine && editable && !busy && hasEngine);
    ui->mergeButton->setEnabled(word && editable);
    ui->splitButton->setEnabled(word && editable);
    ui->insertWordButton->setEnabled(word && editable);
    ui->deleteWordButton->setEnabled(word && editable);
    ui->moveUpButton->setEnabled(editable && (hasLine || m_selectedBlockId));
    ui->moveDownButton->setEnabled(editable && (hasLine || m_selectedBlockId));
    ui->editGeometryButton->setEnabled(word && editable);
    ui->confirmAllButton->setEnabled(page && page->hasResult() && editable);
    ui->replaceAllButton->setEnabled(hasResults && !ui->findEdit->text().isEmpty());
    ui->findNextButton->setEnabled(hasResults);

    const bool hasRegion = page && page->findRegion(m_originalView->getSelectedRegion());
    ui->regionPropertiesButton->setEnabled(hasRegion && !running);
    ui->removeRegionButton->setEnabled(hasRegion && !running);
    ui->addRecognizeRegionButton->setEnabled(m_currentPage >= 0);
    ui->addExcludeRegionButton->setEnabled(m_currentPage >= 0);
    ui->addLineButton->setEnabled(m_currentPage >= 0 && (!page || editable || !page->hasResult()));
    ui->regionsFromBlocksButton->setEnabled(page && page->hasResult() && editable && !running);
    ui->customDpiSpinBox->setEnabled(ui->dpiComboBox->currentData().toInt() == 0);

    // Output information
    QStringList outputInfo;
    if (!m_context.canModify)
    {
        outputInfo << tr("The permissions of the document do not allow its modification.");
    }
    if (m_hasConformanceDeclaration)
    {
        outputInfo << tr("The document declares %1: only an export or an ordinary PDF copy without the declaration is possible.").arg(m_conformanceDeclarations.join(QStringLiteral(", ")));
    }
    if (m_context.certificationPermissions == 1)
    {
        outputInfo << tr("The document is certified without permitted changes: the recognized text can only be exported.");
    }
    else if (m_context.certificationPermissions > 0)
    {
        outputInfo << tr("The document is certified: it cannot be modified, only a copy with OCR can be created, whose certification is not valid.");
    }
    else if (m_context.hasSignatures)
    {
        outputInfo << tr("The document is signed; writing into it may invalidate the state of the signatures.");
    }
    if (m_isTagged)
    {
        outputInfo << tr("The document is tagged; the text layer is written as an artifact and the structure tree is preserved.");
    }
    ui->outputInfoLabel->setText(outputInfo.join(QChar(' ')));
    ui->outputInfoLabel->setVisible(!outputInfo.isEmpty());

    updateWorkflowLabel();
}

void PDFOCRDocumentDialog::done(int result)
{
    if (m_applyInProgress)
    {
        // The commit cannot be interrupted in the middle
        return;
    }

    cancelRecognitionPreparation();

    if (m_jobController->isRunning())
    {
        // The running job is cancelled first; the GUI stays responsive and the dialog is closed after the workers finish (UI-07, JOB-06)
        m_closeRequested = true;
        ui->progressLabel->setText(tr("Stopping the recognition before closing..."));
        m_jobController->stop();
        updateUi();
        return;
    }

    if (result != QDialog::Accepted && m_session->isDirty() && !m_session->getPagesWithResults().empty())
    {
        QMessageBox messageBox(QMessageBox::Question, windowTitle(), tr("The OCR results and corrections are not saved."), QMessageBox::NoButton, this);
        messageBox.setInformativeText(tr("Do you want to save them into an OCR project, so the work can continue later?"));
        QPushButton* saveButton = messageBox.addButton(tr("Save Project..."), QMessageBox::AcceptRole);
        QPushButton* discardButton = messageBox.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        messageBox.addButton(tr("Continue Working"), QMessageBox::RejectRole);
        messageBox.exec();

        if (messageBox.clickedButton() == saveButton)
        {
            if (!saveProject())
            {
                return;
            }
        }
        else if (messageBox.clickedButton() != discardButton)
        {
            return;
        }
    }

    cancelTask(m_pageDataTask);
    cancelTask(m_previewTask);
    cancelTask(m_compressionEstimateTask);
    saveSettings();
    QDialog::done(result);
}

}   // namespace pdfviewer
