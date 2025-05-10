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

#include "pdfviewersettingsdialog.h"
#include "ui_pdfviewersettingsdialog.h"

#include "pdfglobal.h"
#include "pdfutils.h"
#include "pdfwidgetutils.h"
#include "pdfrecentfilemanager.h"
#include "pdfcolorconvertor.h"

#include <QAction>
#include <QLineEdit>
#include <QMessageBox>
#include <QFileDialog>
#include <QListWidgetItem>
#include <QTextToSpeech>
#include <QDomDocument>
#include <QStyledItemDelegate>

#include "pdfdbgheap.h"

namespace pdfviewer
{

class SettingsDelegate : public QStyledItemDelegate
{
public:
    SettingsDelegate(QObject* parent) :
        QStyledItemDelegate(parent)
    {

    }

    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), pdf::PDFWidgetUtils::scaleDPI_y(qobject_cast<QWidget*>(parent()), 48)));
        return size;
    }
};

PDFViewerSettingsDialog::PDFViewerSettingsDialog(const PDFViewerSettings::Settings& settings,
                                                 const pdf::PDFCMSSettings& cmsSettings,
                                                 const OtherSettings& otherSettings,
                                                 const pdf::PDFCertificateStore& certificateStore,
                                                 const std::vector<QAction*>& actions,
                                                 pdf::PDFCMSManager* cmsManager,
                                                 const QStringList& enabledPlugins,
                                                 const pdf::PDFPluginInfos& plugins,
                                                 QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFViewerSettingsDialog),
    m_settings(settings),
    m_cmsSettings(cmsSettings),
    m_otherSettings(otherSettings),
    m_actions(),
    m_isLoadingData(false),
    m_certificateStore(certificateStore),
    m_enabledPlugins(enabledPlugins),
    m_plugins(plugins)
{
    ui->setupUi(this);

    m_textToSpeechEngines = QTextToSpeech::availableEngines();

    new QListWidgetItem(QIcon(":/resources/engine.svg"), tr("Engine"), ui->optionsPagesWidget, EngineSettings);
    new QListWidgetItem(QIcon(":/resources/rendering.svg"), tr("Rendering"), ui->optionsPagesWidget, RenderingSettings);
    new QListWidgetItem(QIcon(":/resources/shading.svg"), tr("Shading"), ui->optionsPagesWidget, ShadingSettings);
    new QListWidgetItem(QIcon(":/resources/cache.svg"), tr("Cache"), ui->optionsPagesWidget, CacheSettings);
    new QListWidgetItem(QIcon(":/resources/shortcuts.svg"), tr("Shortcuts"), ui->optionsPagesWidget, ShortcutSettings);
    new QListWidgetItem(QIcon(":/resources/cms.svg"), tr("Colors | CMS"), ui->optionsPagesWidget, ColorManagementSystemSettings);
    new QListWidgetItem(QIcon(":/resources/cms.svg"), tr("Colors | Postprocessing"), ui->optionsPagesWidget, ColorPostprocessingSettings);
    new QListWidgetItem(QIcon(":/resources/security.svg"), tr("Security"), ui->optionsPagesWidget, SecuritySettings);
    new QListWidgetItem(QIcon(":/resources/ui.svg"), tr("UI"), ui->optionsPagesWidget, UISettings);
    new QListWidgetItem(QIcon(":/resources/speech.svg"), tr("Speech"), ui->optionsPagesWidget, SpeechSettings);
    new QListWidgetItem(QIcon(":/resources/form-settings.svg"), tr("Forms"), ui->optionsPagesWidget, FormSettings);
    new QListWidgetItem(QIcon(":/resources/signature.svg"), tr("Signature"), ui->optionsPagesWidget, SignatureSettings);
    new QListWidgetItem(QIcon(":/resources/plugins.svg"), tr("Plugins"), ui->optionsPagesWidget, PluginsSettings);

    ui->optionsPagesWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->optionsPagesWidget->setItemDelegate(new SettingsDelegate(ui->optionsPagesWidget));
    ui->optionsPagesWidget->setIconSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(32, 32)));

    // Scale font
    QFont font = ui->optionsPagesWidget->font();
    font.setPointSize(font.pointSize() * 1.5);
    ui->optionsPagesWidget->setFont(font);

    ui->renderingEngineComboBox->addItem(tr("Software | QPainter"), static_cast<int>(pdf::RendererEngine::QPainter));
    ui->renderingEngineComboBox->addItem(tr("Software | Blend2D | Parallel"), static_cast<int>(pdf::RendererEngine::Blend2D_MultiThread));
    ui->renderingEngineComboBox->addItem(tr("Software | Blend2D | Sequential"), static_cast<int>(pdf::RendererEngine::Blend2D_SingleThread));

    ui->multithreadingComboBox->addItem(tr("Single thread"), static_cast<int>(pdf::PDFExecutionPolicy::Strategy::SingleThreaded));
    ui->multithreadingComboBox->addItem(tr("Multithreading (load balanced)"), static_cast<int>(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded));
    ui->multithreadingComboBox->addItem(tr("Multithreading (maximum threads)"), static_cast<int>(pdf::PDFExecutionPolicy::Strategy::AlwaysMultithreaded));

    ui->maximumRecentFileCountEdit->setMinimum(PDFRecentFileManager::getMinimumRecentFiles());
    ui->maximumRecentFileCountEdit->setMaximum(PDFRecentFileManager::getMaximumRecentFiles());

    // Load CMS data
    ui->cmsTypeComboBox->addItem(pdf::PDFCMSManager::getSystemName(pdf::PDFCMSSettings::System::Generic), int(pdf::PDFCMSSettings::System::Generic));
    ui->cmsTypeComboBox->addItem(pdf::PDFCMSManager::getSystemName(pdf::PDFCMSSettings::System::LittleCMS2), int(pdf::PDFCMSSettings::System::LittleCMS2));

    ui->cmsRenderingIntentComboBox->addItem(tr("Auto"), int(pdf::RenderingIntent::Auto));
    ui->cmsRenderingIntentComboBox->addItem(tr("Perceptual"), int(pdf::RenderingIntent::Perceptual));
    ui->cmsRenderingIntentComboBox->addItem(tr("Relative colorimetric"), int(pdf::RenderingIntent::RelativeColorimetric));
    ui->cmsRenderingIntentComboBox->addItem(tr("Absolute colorimetric"), int(pdf::RenderingIntent::AbsoluteColorimetric));
    ui->cmsRenderingIntentComboBox->addItem(tr("Saturation"), int(pdf::RenderingIntent::Saturation));

    ui->cmsAccuracyComboBox->addItem(tr("Low"), int(pdf::PDFCMSSettings::Accuracy::Low));
    ui->cmsAccuracyComboBox->addItem(tr("Medium"), int(pdf::PDFCMSSettings::Accuracy::Medium));
    ui->cmsAccuracyComboBox->addItem(tr("High"), int(pdf::PDFCMSSettings::Accuracy::High));

    ui->cmsColorAdaptationXYZComboBox->addItem(tr("None"), int (pdf::PDFCMSSettings::ColorAdaptationXYZ::None));
    ui->cmsColorAdaptationXYZComboBox->addItem(tr("XYZ scaling"), int (pdf::PDFCMSSettings::ColorAdaptationXYZ::XYZScaling));
    ui->cmsColorAdaptationXYZComboBox->addItem(tr("CAT97 matrix"), int (pdf::PDFCMSSettings::ColorAdaptationXYZ::CAT97));
    ui->cmsColorAdaptationXYZComboBox->addItem(tr("CAT02 matrix"), int (pdf::PDFCMSSettings::ColorAdaptationXYZ::CAT02));
    ui->cmsColorAdaptationXYZComboBox->addItem(tr("Bradford method"), int (pdf::PDFCMSSettings::ColorAdaptationXYZ::Bradford));

    // UI Color Schemes
    ui->colorSchemeCombo->addItem(tr("Automatic (or via command line)"), static_cast<int>(PDFViewerSettings::AutoScheme));
    ui->colorSchemeCombo->addItem(tr("Light scheme"), static_cast<int>(PDFViewerSettings::LightScheme));
    ui->colorSchemeCombo->addItem(tr("Dark scheme"), static_cast<int>(PDFViewerSettings::DarkScheme));

    // Langauges
    ui->languageCombo->addItem(tr("Automatic detection"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_AUTOMATIC_SELECTION));
    ui->languageCombo->addItem(tr("English"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_ENGLISH));
    ui->languageCombo->addItem(tr("German"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_GERMAN));
    ui->languageCombo->addItem(tr("Korean"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_KOREAN));
    ui->languageCombo->addItem(tr("Spanish"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_SPANISH));
    ui->languageCombo->addItem(tr("Czech"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_CZECH));
    ui->languageCombo->addItem(tr("Chinese"), static_cast<int>(pdf::PDFApplicationTranslator::E_LANGUAGE_CHINESE));

    auto fillColorProfileList = [](QComboBox* comboBox, const pdf::PDFColorProfileIdentifiers& identifiers)
    {
        for (const pdf::PDFColorProfileIdentifier& identifier : identifiers)
        {
            comboBox->addItem(identifier.name, identifier.id);
        }
    };
    fillColorProfileList(ui->cmsOutputColorProfileComboBox, cmsManager->getOutputProfiles());
    fillColorProfileList(ui->cmsDeviceGrayColorProfileComboBox, cmsManager->getGrayProfiles());
    fillColorProfileList(ui->cmsDeviceRGBColorProfileComboBox, cmsManager->getRGBProfiles());
    fillColorProfileList(ui->cmsDeviceCMYKColorProfileComboBox, cmsManager->getCMYKProfiles());

    for (QCheckBox* checkBox : findChildren<QCheckBox*>())
    {
        connect(checkBox, &QCheckBox::clicked, this, &PDFViewerSettingsDialog::saveData);
    }
    for (QComboBox* comboBox : findChildren<QComboBox*>())
    {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFViewerSettingsDialog::saveData);
    }
    for (QDoubleSpinBox* spinBox : findChildren<QDoubleSpinBox*>())
    {
        connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PDFViewerSettingsDialog::saveData);
    }
    for (QSpinBox* spinBox : findChildren<QSpinBox*>())
    {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &PDFViewerSettingsDialog::saveData);
    }
    for (QLineEdit* lineEdit : findChildren<QLineEdit*>())
    {
        connect(lineEdit, &QLineEdit::editingFinished, this, &PDFViewerSettingsDialog::saveData);
    }

    for (QAction* action : actions)
    {
        if (!action->objectName().isEmpty())
        {
            m_actions.append(action);
        }
    }

    // Text to speech
    for (const QString& engine : m_textToSpeechEngines)
    {
        ui->speechEnginesComboBox->addItem(engine, engine);
    }

    connect(ui->trustedCertificateStoreTableWidget, &QTableWidget::itemSelectionChanged, this, &PDFViewerSettingsDialog::updateTrustedCertificatesTableActions);
    connect(ui->pluginsTableWidget, &QTableWidget::itemSelectionChanged, this, &PDFViewerSettingsDialog::updatePluginInformation);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(640, 480)));
    pdf::PDFWidgetUtils::style(this);

    ui->optionsPagesWidget->setCurrentRow(0);
    loadData();
    loadActionShortcutsTable();
    loadPluginsTable();
    updateTrustedCertificatesTable();
    updateTrustedCertificatesTableActions();
    resize(sizeHint());
}

PDFViewerSettingsDialog::~PDFViewerSettingsDialog()
{
    delete ui;
}

void PDFViewerSettingsDialog::on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);

    switch (current->type())
    {
        case EngineSettings:
            ui->stackedWidget->setCurrentWidget(ui->enginePage);
            break;

        case RenderingSettings:
            ui->stackedWidget->setCurrentWidget(ui->renderingPage);
            break;

        case ShadingSettings:
            ui->stackedWidget->setCurrentWidget(ui->shadingPage);
            break;

        case CacheSettings:
            ui->stackedWidget->setCurrentWidget(ui->cachePage);
            break;

        case ShortcutSettings:
            ui->stackedWidget->setCurrentWidget(ui->shortcutsPage);
            break;

        case ColorManagementSystemSettings:
            ui->stackedWidget->setCurrentWidget(ui->cmsPage);
            break;

        case ColorPostprocessingSettings:
            ui->stackedWidget->setCurrentWidget(ui->cmsPostprocessingPage);
            break;

        case SecuritySettings:
            ui->stackedWidget->setCurrentWidget(ui->securityPage);
            break;

        case UISettings:
            ui->stackedWidget->setCurrentWidget(ui->uiPage);
            break;

        case SpeechSettings:
            ui->stackedWidget->setCurrentWidget(ui->speechPage);
            break;

        case FormSettings:
            ui->stackedWidget->setCurrentWidget(ui->formPage);
            break;

        case SignatureSettings:
            ui->stackedWidget->setCurrentWidget(ui->signaturePage);
            break;

        case PluginsSettings:
            ui->stackedWidget->setCurrentWidget(ui->pluginsPage);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

void PDFViewerSettingsDialog::loadData()
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingData, true);
    ui->renderingEngineComboBox->setCurrentIndex(ui->renderingEngineComboBox->findData(static_cast<int>(m_settings.m_rendererEngine)));

    // Engine
    ui->prefetchPagesCheckBox->setChecked(m_settings.m_prefetchPages);
    ui->multithreadingComboBox->setCurrentIndex(ui->multithreadingComboBox->findData(static_cast<int>(m_settings.m_multithreadingStrategy)));

    // Rendering
    ui->antialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::Antialiasing));
    ui->textAntialiasingCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::TextAntialiasing));
    ui->smoothPicturesCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::SmoothImages));
    ui->ignoreOptionalContentCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::IgnoreOptionalContent));
    ui->clipToCropBoxCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::ClipToCropBox));
    ui->displayTimeCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::DisplayTimes));
    ui->displayAnnotationsCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::DisplayAnnotations));

    // Shading
    ui->preferredMeshResolutionEdit->setValue(m_settings.m_preferredMeshResolutionRatio);
    ui->minimalMeshResolutionEdit->setValue(m_settings.m_minimalMeshResolutionRatio);
    ui->colorToleranceEdit->setValue(m_settings.m_colorTolerance);

    // Cache
    ui->compiledPageCacheSizeEdit->setValue(m_settings.m_compiledPageCacheLimit);
    ui->thumbnailCacheSizeEdit->setValue(m_settings.m_thumbnailsCacheLimit);
    ui->cachedFontLimitEdit->setValue(m_settings.m_fontCacheLimit);
    ui->cachedInstancedFontLimitEdit->setValue(m_settings.m_instancedFontCacheLimit);

    // Security
    ui->allowLaunchCheckBox->setChecked(m_settings.m_allowLaunchApplications);
    ui->allowRunURICheckBox->setChecked(m_settings.m_allowLaunchURI);

    // UI
    ui->maximumRecentFileCountEdit->setValue(m_otherSettings.maximumRecentFileCount);
    ui->magnifierSizeEdit->setValue(m_settings.m_magnifierSize);
    ui->magnifierZoomEdit->setValue(m_settings.m_magnifierZoom);
    ui->maximumUndoStepsEdit->setValue(m_settings.m_maximumUndoSteps);
    ui->maximumRedoStepsEdit->setValue(m_settings.m_maximumRedoSteps);
    ui->developerModeCheckBox->setChecked(m_settings.m_allowDeveloperMode);
    ui->logicalPixelZoomCheckBox->setChecked(m_settings.m_features.testFlag(pdf::PDFRenderer::LogicalSizeZooming));
    ui->colorSchemeCombo->setCurrentIndex(ui->colorSchemeCombo->findData(static_cast<int>(m_settings.m_colorScheme)));
    ui->languageCombo->setCurrentIndex(ui->languageCombo->findData(static_cast<int>(m_settings.m_language)));

    // CMS
    ui->cmsTypeComboBox->setCurrentIndex(ui->cmsTypeComboBox->findData(int(m_cmsSettings.system)));
    if (m_cmsSettings.system != pdf::PDFCMSSettings::System::Generic)
    {
        ui->cmsRenderingIntentComboBox->setEnabled(true);
        ui->cmsRenderingIntentComboBox->setCurrentIndex(ui->cmsRenderingIntentComboBox->findData(int(m_cmsSettings.intent)));
        ui->cmsAccuracyComboBox->setEnabled(true);
        ui->cmsAccuracyComboBox->setCurrentIndex(ui->cmsAccuracyComboBox->findData(int(m_cmsSettings.accuracy)));
        ui->cmsColorAdaptationXYZComboBox->setEnabled(true);
        ui->cmsColorAdaptationXYZComboBox->setCurrentIndex(ui->cmsColorAdaptationXYZComboBox->findData(int(m_cmsSettings.colorAdaptationXYZ)));
        ui->cmsIsBlackPointCompensationCheckBox->setEnabled(true);
        ui->cmsIsBlackPointCompensationCheckBox->setChecked(m_cmsSettings.isBlackPointCompensationActive);
        ui->cmsConsiderOutputIntentCheckBox->setEnabled(true);
        ui->cmsConsiderOutputIntentCheckBox->setChecked(m_cmsSettings.isConsiderOutputIntent);
        ui->cmsWhitePaperColorTransformedCheckBox->setEnabled(true);
        ui->cmsWhitePaperColorTransformedCheckBox->setChecked(m_cmsSettings.isWhitePaperColorTransformed);
        ui->cmsOutputColorProfileComboBox->setEnabled(true);
        ui->cmsOutputColorProfileComboBox->setCurrentIndex(ui->cmsOutputColorProfileComboBox->findData(m_cmsSettings.outputCS));
        ui->cmsDeviceGrayColorProfileComboBox->setEnabled(true);
        ui->cmsDeviceGrayColorProfileComboBox->setCurrentIndex(ui->cmsDeviceGrayColorProfileComboBox->findData(m_cmsSettings.deviceGray));
        ui->cmsDeviceRGBColorProfileComboBox->setEnabled(true);
        ui->cmsDeviceRGBColorProfileComboBox->setCurrentIndex(ui->cmsDeviceRGBColorProfileComboBox->findData(m_cmsSettings.deviceRGB));
        ui->cmsDeviceCMYKColorProfileComboBox->setEnabled(true);
        ui->cmsDeviceCMYKColorProfileComboBox->setCurrentIndex(ui->cmsDeviceCMYKColorProfileComboBox->findData(m_cmsSettings.deviceCMYK));
        ui->cmsProfileDirectoryButton->setEnabled(true);
        ui->cmsProfileDirectoryEdit->setEnabled(true);
        ui->cmsProfileDirectoryEdit->setText(m_cmsSettings.profileDirectory);
    }
    else
    {
        ui->cmsRenderingIntentComboBox->setEnabled(false);
        ui->cmsRenderingIntentComboBox->setCurrentIndex(-1);
        ui->cmsAccuracyComboBox->setEnabled(false);
        ui->cmsAccuracyComboBox->setCurrentIndex(-1);
        ui->cmsColorAdaptationXYZComboBox->setEnabled(false);
        ui->cmsColorAdaptationXYZComboBox->setCurrentIndex(-1);
        ui->cmsIsBlackPointCompensationCheckBox->setEnabled(false);
        ui->cmsIsBlackPointCompensationCheckBox->setChecked(false);
        ui->cmsConsiderOutputIntentCheckBox->setEnabled(false);
        ui->cmsConsiderOutputIntentCheckBox->setChecked(false);
        ui->cmsWhitePaperColorTransformedCheckBox->setEnabled(false);
        ui->cmsWhitePaperColorTransformedCheckBox->setChecked(false);
        ui->cmsOutputColorProfileComboBox->setEnabled(false);
        ui->cmsOutputColorProfileComboBox->setCurrentIndex(-1);
        ui->cmsDeviceGrayColorProfileComboBox->setEnabled(false);
        ui->cmsDeviceGrayColorProfileComboBox->setCurrentIndex(-1);
        ui->cmsDeviceRGBColorProfileComboBox->setEnabled(false);
        ui->cmsDeviceRGBColorProfileComboBox->setCurrentIndex(-1);
        ui->cmsDeviceCMYKColorProfileComboBox->setEnabled(false);
        ui->cmsDeviceCMYKColorProfileComboBox->setCurrentIndex(-1);
        ui->cmsProfileDirectoryButton->setEnabled(false);
        ui->cmsProfileDirectoryEdit->setEnabled(false);
        ui->cmsProfileDirectoryEdit->setText(QString());
    }

    // Color postprocessing
    ui->foregroundColorEdit->setText(m_cmsSettings.foregroundColor.name(QColor::HexRgb));
    ui->backgroundColorEdit->setText(m_cmsSettings.backgroundColor.name(QColor::HexRgb));
    ui->sigmoidFunctionSlopeEdit->setValue(m_cmsSettings.sigmoidSlopeFactor);
    ui->bitonalThresholdEdit->setValue(m_cmsSettings.bitonalThreshold);

    // Text-to-speech
    ui->speechEnginesComboBox->setCurrentIndex(ui->speechEnginesComboBox->findData(m_settings.m_speechEngine));
    setSpeechEngine(m_settings.m_speechEngine, m_settings.m_speechLocale);
    ui->speechLocaleComboBox->setCurrentIndex(ui->speechLocaleComboBox->findData(m_settings.m_speechLocale));
    ui->speechVoiceComboBox->setCurrentIndex(ui->speechVoiceComboBox->findData(m_settings.m_speechVoice));
    ui->speechRateEdit->setValue(m_settings.m_speechRate);
    ui->speechPitchEdit->setValue(m_settings.m_speechPitch);
    ui->speechVolumeEdit->setValue(m_settings.m_speechVolume);

    // Form Settings
    ui->formHighlightFieldsCheckBox->setChecked(m_settings.m_formAppearanceFlags.testFlag(pdf::PDFFormManager::HighlightFields));
    ui->formHighlightRequiredFieldsCheckBox->setChecked(m_settings.m_formAppearanceFlags.testFlag(pdf::PDFFormManager::HighlightRequiredFields));

    // Signature Settings
    ui->signatureVerificationEnableCheckBox->setChecked(m_settings.m_signatureVerificationEnabled);
    ui->signatureStrictModeEnabledCheckBox->setChecked(m_settings.m_signatureTreatWarningsAsErrors);
    ui->signatureIgnoreExpiredCheckBox->setChecked(m_settings.m_signatureIgnoreCertificateValidityTime);
    ui->signatureUseSystemCertificateStoreCheckBox->setChecked(m_settings.m_signatureUseSystemStore);
}

void PDFViewerSettingsDialog::saveData()
{
    if (m_isLoadingData)
    {
        return;
    }

    QObject* sender = this->sender();

    if (sender == ui->languageCombo)
    {
        m_settings.m_language = static_cast<pdf::PDFApplicationTranslator::ELanguage>(ui->languageCombo->currentData().toInt());
    }
    else if (sender == ui->colorSchemeCombo)
    {
        m_settings.m_colorScheme = static_cast<PDFViewerSettings::ColorScheme>(ui->colorSchemeCombo->currentData().toInt());
    }
    else if (sender == ui->renderingEngineComboBox)
    {
        m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(ui->renderingEngineComboBox->currentData().toInt());
    }
    else if (sender == ui->prefetchPagesCheckBox)
    {
        m_settings.m_prefetchPages = ui->prefetchPagesCheckBox->isChecked();
    }
    else if (sender == ui->antialiasingCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::Antialiasing, ui->antialiasingCheckBox->isChecked());
    }
    else if (sender == ui->textAntialiasingCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::TextAntialiasing, ui->textAntialiasingCheckBox->isChecked());
    }
    else if (sender == ui->smoothPicturesCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::SmoothImages, ui->smoothPicturesCheckBox->isChecked());
    }
    else if (sender == ui->ignoreOptionalContentCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::IgnoreOptionalContent, ui->ignoreOptionalContentCheckBox->isChecked());
    }
    else if (sender == ui->displayAnnotationsCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::DisplayAnnotations, ui->displayAnnotationsCheckBox->isChecked());
    }
    else if (sender == ui->clipToCropBoxCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::ClipToCropBox, ui->clipToCropBoxCheckBox->isChecked());
    }
    else if (sender == ui->displayTimeCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::DisplayTimes, ui->displayTimeCheckBox->isChecked());
    }
    else if (sender == ui->preferredMeshResolutionEdit)
    {
        m_settings.m_preferredMeshResolutionRatio = ui->preferredMeshResolutionEdit->value();
    }
    else if (sender == ui->minimalMeshResolutionEdit)
    {
        m_settings.m_minimalMeshResolutionRatio = ui->minimalMeshResolutionEdit->value();
    }
    else if (sender == ui->colorToleranceEdit)
    {
        m_settings.m_colorTolerance = ui->colorToleranceEdit->value();
    }
    else if (sender == ui->allowLaunchCheckBox)
    {
        m_settings.m_allowLaunchApplications = ui->allowLaunchCheckBox->isChecked();
    }
    else if (sender == ui->allowRunURICheckBox)
    {
        m_settings.m_allowLaunchURI = ui->allowRunURICheckBox->isChecked();
    }
    else if (sender == ui->developerModeCheckBox)
    {
        m_settings.m_allowDeveloperMode = ui->developerModeCheckBox->isChecked();
    }
    else if (sender == ui->compiledPageCacheSizeEdit)
    {
        m_settings.m_compiledPageCacheLimit = ui->compiledPageCacheSizeEdit->value();
    }
    else if (sender == ui->thumbnailCacheSizeEdit)
    {
        m_settings.m_thumbnailsCacheLimit = ui->thumbnailCacheSizeEdit->value();
    }
    else if (sender == ui->cachedFontLimitEdit)
    {
        m_settings.m_fontCacheLimit = ui->cachedFontLimitEdit->value();
    }
    else if (sender == ui->cachedInstancedFontLimitEdit)
    {
        m_settings.m_instancedFontCacheLimit = ui->cachedInstancedFontLimitEdit->value();
    }
    else if (sender == ui->cmsTypeComboBox)
    {
        m_cmsSettings.system = static_cast<pdf::PDFCMSSettings::System>(ui->cmsTypeComboBox->currentData().toInt());
    }
    else if (sender == ui->cmsRenderingIntentComboBox)
    {
        m_cmsSettings.intent = static_cast<pdf::RenderingIntent>(ui->cmsRenderingIntentComboBox->currentData().toInt());
    }
    else if (sender == ui->cmsAccuracyComboBox)
    {
        m_cmsSettings.accuracy = static_cast<pdf::PDFCMSSettings::Accuracy>(ui->cmsAccuracyComboBox->currentData().toInt());
    }
    else if (sender == ui->cmsColorAdaptationXYZComboBox)
    {
        m_cmsSettings.colorAdaptationXYZ = static_cast<pdf::PDFCMSSettings::ColorAdaptationXYZ>(ui->cmsColorAdaptationXYZComboBox->currentData().toInt());
    }
    else if (sender == ui->cmsIsBlackPointCompensationCheckBox)
    {
        m_cmsSettings.isBlackPointCompensationActive = ui->cmsIsBlackPointCompensationCheckBox->isChecked();
    }
    else if (sender == ui->cmsWhitePaperColorTransformedCheckBox)
    {
        m_cmsSettings.isWhitePaperColorTransformed = ui->cmsWhitePaperColorTransformedCheckBox->isChecked();
    }
    else if (sender == ui->cmsConsiderOutputIntentCheckBox)
    {
        m_cmsSettings.isConsiderOutputIntent = ui->cmsConsiderOutputIntentCheckBox->isChecked();
    }
    else if (sender == ui->cmsOutputColorProfileComboBox)
    {
        m_cmsSettings.outputCS = ui->cmsOutputColorProfileComboBox->currentData().toString();
    }
    else if (sender == ui->cmsDeviceGrayColorProfileComboBox)
    {
        m_cmsSettings.deviceGray = ui->cmsDeviceGrayColorProfileComboBox->currentData().toString();
    }
    else if (sender == ui->cmsDeviceRGBColorProfileComboBox)
    {
        m_cmsSettings.deviceRGB = ui->cmsDeviceRGBColorProfileComboBox->currentData().toString();
    }
    else if (sender == ui->cmsDeviceCMYKColorProfileComboBox)
    {
        m_cmsSettings.deviceCMYK = ui->cmsDeviceCMYKColorProfileComboBox->currentData().toString();
    }
    else if (sender == ui->cmsProfileDirectoryEdit)
    {
        m_cmsSettings.profileDirectory = ui->cmsProfileDirectoryEdit->text();
    }
    else if (sender == ui->multithreadingComboBox)
    {
        m_settings.m_multithreadingStrategy = static_cast<pdf::PDFExecutionPolicy::Strategy>(ui->multithreadingComboBox->currentData().toInt());
    }
    else if (sender == ui->maximumRecentFileCountEdit)
    {
        m_otherSettings.maximumRecentFileCount = ui->maximumRecentFileCountEdit->value();
    }
    else if (sender == ui->speechEnginesComboBox)
    {
        m_settings.m_speechEngine = ui->speechEnginesComboBox->currentData().toString();
    }
    else if (sender == ui->speechLocaleComboBox)
    {
        m_settings.m_speechLocale = ui->speechLocaleComboBox->currentData().toString();
    }
    else if (sender == ui->speechVoiceComboBox)
    {
        m_settings.m_speechVoice = ui->speechVoiceComboBox->currentData().toString();
    }
    else if (sender == ui->speechRateEdit)
    {
        m_settings.m_speechRate = ui->speechRateEdit->value();
    }
    else if (sender == ui->speechPitchEdit)
    {
        m_settings.m_speechPitch = ui->speechPitchEdit->value();
    }
    else if (sender == ui->speechVolumeEdit)
    {
        m_settings.m_speechVolume = ui->speechVolumeEdit->value();
    }
    else if (sender == ui->magnifierSizeEdit)
    {
        m_settings.m_magnifierSize = ui->magnifierSizeEdit->value();
    }
    else if (sender == ui->magnifierZoomEdit)
    {
        m_settings.m_magnifierZoom = ui->magnifierZoomEdit->value();
    }
    else if (sender == ui->foregroundColorEdit)
    {
        m_cmsSettings.foregroundColor.fromString(ui->foregroundColorEdit->text());
        if (!m_cmsSettings.foregroundColor.isValid())
        {
            pdf::PDFColorConvertor colorConvertor;
            m_cmsSettings.foregroundColor = colorConvertor.getForegroundColor();
        }
    }
    else if (sender == ui->backgroundColorEdit)
    {
        m_cmsSettings.backgroundColor.fromString(ui->backgroundColorEdit->text());
        if (!m_cmsSettings.backgroundColor.isValid())
        {
            pdf::PDFColorConvertor colorConvertor;
            m_cmsSettings.backgroundColor = colorConvertor.getBackgroundColor();
        }
    }
    else if (sender == ui->sigmoidFunctionSlopeEdit)
    {
        m_cmsSettings.sigmoidSlopeFactor = ui->sigmoidFunctionSlopeEdit->value();
    }
    else if (sender == ui->bitonalThresholdEdit)
    {
        m_cmsSettings.bitonalThreshold = ui->bitonalThresholdEdit->value();
    }
    else if (sender == ui->formHighlightFieldsCheckBox)
    {
        m_settings.m_formAppearanceFlags.setFlag(pdf::PDFFormManager::HighlightFields, ui->formHighlightFieldsCheckBox->isChecked());
    }
    else if (sender == ui->formHighlightRequiredFieldsCheckBox)
    {
        m_settings.m_formAppearanceFlags.setFlag(pdf::PDFFormManager::HighlightRequiredFields, ui->formHighlightRequiredFieldsCheckBox->isChecked());
    }
    else if (sender == ui->maximumUndoStepsEdit)
    {
        m_settings.m_maximumUndoSteps = ui->maximumUndoStepsEdit->value();
    }
    else if (sender == ui->maximumRedoStepsEdit)
    {
        m_settings.m_maximumRedoSteps = ui->maximumRedoStepsEdit->value();
    }
    else if (sender == ui->logicalPixelZoomCheckBox)
    {
        m_settings.m_features.setFlag(pdf::PDFRenderer::LogicalSizeZooming, ui->logicalPixelZoomCheckBox->isChecked());
    }
    else if (sender == ui->signatureVerificationEnableCheckBox)
    {
        m_settings.m_signatureVerificationEnabled = ui->signatureVerificationEnableCheckBox->isChecked();
    }
    else if (sender == ui->signatureStrictModeEnabledCheckBox)
    {
        m_settings.m_signatureTreatWarningsAsErrors = ui->signatureStrictModeEnabledCheckBox->isChecked();
    }
    else if (sender == ui->signatureIgnoreExpiredCheckBox)
    {
        m_settings.m_signatureIgnoreCertificateValidityTime = ui->signatureIgnoreExpiredCheckBox->isChecked();
    }
    else if (sender == ui->signatureUseSystemCertificateStoreCheckBox)
    {
        m_settings.m_signatureUseSystemStore = ui->signatureUseSystemCertificateStoreCheckBox->isChecked();
    }

    const bool loadData = !qobject_cast<const QDoubleSpinBox*>(sender) && !qobject_cast<const QSpinBox*>(sender);
    if (loadData)
    {
        this->loadData();
    }
}

void PDFViewerSettingsDialog::updateTrustedCertificatesTable()
{
    ui->trustedCertificateStoreTableWidget->setUpdatesEnabled(false);
    ui->trustedCertificateStoreTableWidget->clear();

    const pdf::PDFCertificateEntries& certificates = m_certificateStore.getCertificates();
    ui->trustedCertificateStoreTableWidget->setRowCount(int(certificates.size()));
    ui->trustedCertificateStoreTableWidget->setColumnCount(5);
    ui->trustedCertificateStoreTableWidget->verticalHeader()->setVisible(true);
    ui->trustedCertificateStoreTableWidget->setShowGrid(true);
    ui->trustedCertificateStoreTableWidget->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->trustedCertificateStoreTableWidget->setHorizontalHeaderLabels(QStringList() << tr("Type") << tr("Certificate") << tr("Organization") << tr("Valid from") << tr("Valid to"));

    for (int i = 0; i < certificates.size(); ++i)
    {
        QString type;
        switch (certificates[i].type)
        {
            case pdf::PDFCertificateEntry::EntryType::User:
                type = tr("User");
                break;

            case pdf::PDFCertificateEntry::EntryType::System:
                type = tr("System");
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        const pdf::PDFCertificateInfo& info = certificates[i].info;
        ui->trustedCertificateStoreTableWidget->setItem(i, 0, new QTableWidgetItem(type));
        ui->trustedCertificateStoreTableWidget->setItem(i, 1, new QTableWidgetItem(info.getName(pdf::PDFCertificateInfo::CommonName)));
        ui->trustedCertificateStoreTableWidget->setItem(i, 2, new QTableWidgetItem(info.getName(pdf::PDFCertificateInfo::OrganizationName)));

        QDateTime notValidBefore = info.getNotValidBefore().toLocalTime();
        QDateTime notValidAfter = info.getNotValidAfter().toLocalTime();

        if (notValidBefore.isValid())
        {
            ui->trustedCertificateStoreTableWidget->setItem(i, 3, new QTableWidgetItem(QLocale::system().toString(notValidBefore, QLocale::ShortFormat)));
        }

        if (notValidAfter.isValid())
        {
            ui->trustedCertificateStoreTableWidget->setItem(i, 4, new QTableWidgetItem(QLocale::system().toString(notValidAfter, QLocale::ShortFormat)));
        }
    }

    ui->trustedCertificateStoreTableWidget->resizeColumnsToContents();
    ui->trustedCertificateStoreTableWidget->setUpdatesEnabled(true);
}

void PDFViewerSettingsDialog::updateTrustedCertificatesTableActions()
{
    ui->removeCertificateButton->setEnabled(!ui->trustedCertificateStoreTableWidget->selectionModel()->selectedRows().isEmpty());
}

void PDFViewerSettingsDialog::loadActionShortcutsTable()
{
    ui->shortcutsTableWidget->setRowCount(m_actions.size());
    ui->shortcutsTableWidget->setColumnCount(2);
    ui->shortcutsTableWidget->setHorizontalHeaderLabels({ tr("Action"), tr("Shortcut")});
    ui->shortcutsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < m_actions.size(); ++i)
    {
        QAction* action = m_actions[i];

        // Action name and icon
        QTableWidgetItem* actionItem = new QTableWidgetItem(action->icon(), action->text());
        actionItem->setFlags(Qt::ItemIsEnabled);
        ui->shortcutsTableWidget->setItem(i, 0, actionItem);

        // Action shortcut
        QTableWidgetItem* shortcutItem = new QTableWidgetItem(action->shortcut().toString(QKeySequence::NativeText));
        ui->shortcutsTableWidget->setItem(i, 1, shortcutItem);
    }
}

bool PDFViewerSettingsDialog::saveActionShortcutsTable()
{
    // Jakub Melka: we need validation here
    for (int i = 0; i < m_actions.size(); ++i)
    {
        QString shortcut = ui->shortcutsTableWidget->item(i, 1)->data(Qt::DisplayRole).toString();
        if (!shortcut.isEmpty())
        {
            QKeySequence sequence = QKeySequence::fromString(shortcut, QKeySequence::NativeText);
            if (sequence.toString(QKeySequence::PortableText).isEmpty())
            {
                QMessageBox::critical(this, tr("Error"), tr("Shortcut '%1' is invalid for action %2.").arg(shortcut, m_actions[i]->text()));
                return false;
            }
        }
    }

    for (int i = 0; i < m_actions.size(); ++i)
    {
        QAction* action = m_actions[i];

        // Set shortcut to the action
        QString shortcut = ui->shortcutsTableWidget->item(i, 1)->data(Qt::DisplayRole).toString();
        QKeySequence sequence = QKeySequence::fromString(shortcut, QKeySequence::NativeText);
        action->setShortcut(sequence);
    }

    return true;
}

void PDFViewerSettingsDialog::loadPluginsTable()
{
    ui->pluginsTableWidget->setRowCount(int(m_plugins.size()));
    ui->pluginsTableWidget->setColumnCount(5);
    ui->pluginsTableWidget->setHorizontalHeaderLabels({ tr("Active"), tr("Name"), tr("Author"), tr("Version"), tr("License") });
    ui->pluginsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->pluginsTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    for (int i = 0; i < m_plugins.size(); ++i)
    {
        const pdf::PDFPluginInfo& plugin = m_plugins[i];

        QTableWidgetItem* activeItem = new QTableWidgetItem(QString());
        activeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        activeItem->setCheckState((m_enabledPlugins.contains(plugin.name)) ? Qt::Checked : Qt::Unchecked);
        ui->pluginsTableWidget->setItem(i, 0, activeItem);

        ui->pluginsTableWidget->setItem(i, 1, new QTableWidgetItem(plugin.name));
        ui->pluginsTableWidget->setItem(i, 2, new QTableWidgetItem(plugin.author));
        ui->pluginsTableWidget->setItem(i, 3, new QTableWidgetItem(plugin.version));
        ui->pluginsTableWidget->setItem(i, 4, new QTableWidgetItem(plugin.license));
    }
}

void PDFViewerSettingsDialog::savePluginsTable()
{
    QStringList enabledPlugins;

    for (int i = 0; i < m_plugins.size(); ++i)
    {
        bool enabled = ui->pluginsTableWidget->item(i, 0)->data(Qt::CheckStateRole).toInt() == Qt::Checked;
        if (enabled)
        {
            enabledPlugins << m_plugins[i].name;
        }
    }

    m_enabledPlugins = qMove(enabledPlugins);
}

void PDFViewerSettingsDialog::updatePluginInformation()
{
    QModelIndexList rows = ui->pluginsTableWidget->selectionModel()->selectedRows();
    if (rows.size() == 1)
    {
        ui->pluginDescriptionLabel->setText(m_plugins.at(rows.front().row()).description);
    }
    else
    {
        ui->pluginDescriptionLabel->setText(QString());
    }
}

void PDFViewerSettingsDialog::setSpeechEngine(const QString& engine, const QString& locale)
{
    if (m_currentSpeechEngine == engine && m_currentSpeechLocale == locale)
    {
        return;
    }

    QTextToSpeech textToSpeech(engine, nullptr);
    textToSpeech.setLocale(QLocale(locale));

    if (m_currentSpeechEngine != engine)
    {
        m_currentSpeechEngine = engine;

        QVector<QLocale> locales = textToSpeech.availableLocales();
        ui->speechLocaleComboBox->setUpdatesEnabled(false);
        ui->speechLocaleComboBox->clear();
        for (const QLocale& currentLocale : locales)
        {
            ui->speechLocaleComboBox->addItem(QString("%1 (%2)").arg(currentLocale.nativeLanguageName(), currentLocale.nativeTerritoryName()), currentLocale.name());
        }
        ui->speechLocaleComboBox->setUpdatesEnabled(true);
    }

    m_currentSpeechLocale = locale;

    QVector<QVoice> voices = textToSpeech.availableVoices();
    ui->speechVoiceComboBox->setUpdatesEnabled(false);
    ui->speechVoiceComboBox->clear();
    for (const QVoice& voice : voices)
    {
        ui->speechVoiceComboBox->addItem(QString("%1 (%2, %3)").arg(voice.name(), QVoice::genderName(voice.gender()), QVoice::ageName(voice.age())), voice.name());
    }
    ui->speechVoiceComboBox->setUpdatesEnabled(true);
}

bool PDFViewerSettingsDialog::canCloseDialog()
{
    return true;
}

void PDFViewerSettingsDialog::accept()
{
    if (!canCloseDialog())
    {
        return;
    }

    if (saveActionShortcutsTable())
    {
        savePluginsTable();
        QDialog::accept();
    }
}

void PDFViewerSettingsDialog::reject()
{
    if (canCloseDialog())
    {
        QDialog::reject();
    }
}

void PDFViewerSettingsDialog::on_cmsProfileDirectoryButton_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Select color profile directory"));
    if (!directory.isEmpty())
    {
        m_cmsSettings.profileDirectory = directory;
        loadData();
    }
}

void PDFViewerSettingsDialog::on_removeCertificateButton_clicked()
{
    std::set<int> rows;
    QModelIndexList selectedIndices = ui->trustedCertificateStoreTableWidget->selectionModel()->selectedRows();
    for (const QModelIndex& index : selectedIndices)
    {
        rows.insert(index.row());
    }

    pdf::PDFCertificateEntries newEntries;
    const pdf::PDFCertificateEntries& certificates = m_certificateStore.getCertificates();
    for (int i = 0; i < int(certificates.size()); ++i)
    {
        if (!rows.count(i))
        {
            newEntries.push_back(certificates[i]);
        }
    }
    m_certificateStore.setCertificates(qMove(newEntries));
    updateTrustedCertificatesTable();
}

}   // namespace pdfviewer
