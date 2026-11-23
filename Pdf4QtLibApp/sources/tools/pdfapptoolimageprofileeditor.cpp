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

#include "pdfapptoolimageprofileeditor.h"

#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolparametermodel.h"

#include "pdfcomponentbuttons.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttool_optimizeimages.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <map>

namespace pdf
{

/// Helpers of the profile editor
class PDFAppToolImageProfileEditorHelper
{
public:
    PDFAppToolImageProfileEditorHelper() = delete;

    using Tool = PDFDocumentToolOptimizeImages;

    /// The fields of the profiles and the settings of an exception (PDFDocumentToolOptimizeImages)
    static QString getSettingOfField(const QString& field)
    {
        struct Pair { const char* field; const char* setting; };
        static constexpr Pair pairs[] =
        {
            { Tool::FIELD_ALGORITHM, Tool::EXCEPTION_ALGORITHM },
            { Tool::FIELD_TARGET_DPI, Tool::EXCEPTION_TARGET_DPI },
            { Tool::FIELD_RESAMPLE, Tool::EXCEPTION_RESAMPLE },
            { Tool::FIELD_JPEG_QUALITY, Tool::EXCEPTION_JPEG_QUALITY },
            { Tool::FIELD_JPEG2000_RATE, Tool::EXCEPTION_JPEG2000_RATE },
            { Tool::FIELD_PNG_PREDICTOR, Tool::EXCEPTION_PNG_PREDICTOR },
        };
        for (const Pair& pair : pairs)
        {
            if (field.compare(QLatin1String(pair.field), Qt::CaseInsensitive) == 0)
            {
                return QString::fromLatin1(pair.setting);
            }
        }
        return QString();
    }

    /// The color mode of an override, which a profile chip stands for (color, grayscale, bitonal)
    static QString getColorModeOfProfile(const QString& profile)
    {
        if (profile == QLatin1String(Tool::PROFILE_GRAY))
        {
            return QString::fromLatin1(Tool::COLOR_GRAYSCALE);
        }
        if (profile == QLatin1String(Tool::PROFILE_BITONAL))
        {
            return QString::fromLatin1(Tool::COLOR_BITONAL);
        }
        return QString::fromLatin1(Tool::COLOR_COLOR);
    }

    /// A color mode, which has its own profile (not auto, not preserve)
    static bool isProfileColorMode(const QString& colorMode)
    {
        return colorMode == QLatin1String(Tool::COLOR_COLOR) || colorMode == QLatin1String(Tool::COLOR_GRAYSCALE) || colorMode == QLatin1String(Tool::COLOR_BITONAL);
    }

    /// A widget of the card, which a rebuild replaces: hidden and nameless at once, deleted from
    /// the event loop - the rebuild may run inside of a signal of a control in it (never delete the
    /// sender synchronously). PA6: not retire / retireLater - retire would cut the later receivers of
    /// the signal of the sender, retireLater would delay the deletion by one more call of the event
    /// loop (the test profileEditorRebuildIsDeferred expects it at the first DeferredDelete)
    static void retire(QWidget* widget)
    {
        widget->hide();
        widget->setObjectName(QString());
        for (QObject* child : widget->findChildren<QObject*>())
        {
            child->setObjectName(QString());
        }
        widget->deleteLater();
    }

    /// The default threshold shown by the disabled field of Auto (301:3594 "128")
    static constexpr int DEFAULT_THRESHOLD = 128;
};

// ---------------------------------------------------------- PDFAppToolImageOverrideStore

PDFAppToolImageOverrideStore::PDFAppToolImageOverrideStore(PDFAppToolParameterModel* model, std::vector<PDFDocumentImageList::Key> target) :
    m_model(model),
    m_target(std::move(target))
{

}

PDFAppToolImageOverrideStore::~PDFAppToolImageOverrideStore() = default;

bool PDFAppToolImageOverrideStore::isAvailable() const
{
    return !m_model.isNull() && !m_target.empty();
}

QString PDFAppToolImageOverrideStore::getExceptionSetting(const QString& parameter)
{
    using Tool = PDFDocumentToolOptimizeImages;

    // Mode, Color, Goal and Options of the toolbar (301:3594 "Toolbar menus change the override too")
    struct Pair { const char* parameter; const char* setting; };
    static constexpr Pair pairs[] =
    {
        { Tool::PARAMETER_MODE, Tool::EXCEPTION_MODE },
        { Tool::PARAMETER_COLOR_MODE, Tool::EXCEPTION_COLOR_MODE },
        { Tool::PARAMETER_GOAL, Tool::EXCEPTION_GOAL },
        { Tool::PARAMETER_KEEP_ORIGINAL, Tool::EXCEPTION_KEEP_ORIGINAL },
        { Tool::PARAMETER_PRESERVE_TRANSPARENCY, Tool::EXCEPTION_PRESERVE_TRANSPARENCY },
    };
    for (const Pair& pair : pairs)
    {
        if (parameter == QLatin1String(pair.parameter))
        {
            return QString::fromLatin1(pair.setting);
        }
    }
    if (parameter == QLatin1String(Tool::PARAMETER_BITONAL_THRESHOLD))
    {
        return QString::fromLatin1(Tool::EXCEPTION_THRESHOLD);
    }

    // "colorTargetDpi", "grayJpegQuality", "bitonalAlgorithm", "targetDpi" (Grayscale)
    for (const char* profile : { Tool::PROFILE_COLOR, Tool::PROFILE_GRAY, Tool::PROFILE_BITONAL })
    {
        const QString prefix = QString::fromLatin1(profile);
        if (parameter.startsWith(prefix) && parameter.size() > prefix.size() && parameter.at(prefix.size()).isUpper())
        {
            return PDFAppToolImageProfileEditorHelper::getSettingOfField(parameter.mid(prefix.size()));
        }
    }
    return PDFAppToolImageProfileEditorHelper::getSettingOfField(parameter);
}

QVariant PDFAppToolImageOverrideStore::readSetting(const PDFAppToolParameterModel* model, const PDFDocumentImageList::Key& key, const QString& setting)
{
    using Tool = PDFDocumentToolOptimizeImages;

    if (!model)
    {
        return QVariant();
    }

    QString error;
    const std::vector<Tool::ImageException> exceptions = Tool::parseImageExceptions(model->getValue(QString::fromLatin1(Tool::PARAMETER_IMAGES)).toString(), &error);
    for (const Tool::ImageException& exception : exceptions)
    {
        if (exception.key == key)
        {
            return exception.settings.value(setting);
        }
    }
    return QVariant();
}

QString PDFAppToolImageOverrideStore::writeSetting(PDFAppToolParameterModel* model, const std::vector<PDFDocumentImageList::Key>& target, const QString& setting, const QVariant& value)
{
    using Tool = PDFDocumentToolOptimizeImages;

    if (!model || target.empty() || setting.isEmpty())
    {
        return QString();
    }

    const QString parameter = QString::fromLatin1(Tool::PARAMETER_IMAGES);
    QString error;
    std::vector<Tool::ImageException> exceptions = Tool::parseImageExceptions(model->getValue(parameter).toString(), &error);
    if (!error.isEmpty())
    {
        return error;
    }

    // An index of the exceptions by their keys: a target of 10 000 images is not quadratic
    std::map<PDFDocumentImageList::Key, size_t> indices;
    for (size_t i = 0; i < exceptions.size(); ++i)
    {
        indices.emplace(exceptions[i].key, i);
    }
    for (const PDFDocumentImageList::Key& key : target)
    {
        auto it = indices.find(key);
        if (it == indices.end())
        {
            Tool::ImageException exception;
            exception.key = key;
            exceptions.push_back(std::move(exception));
            it = indices.emplace(key, exceptions.size() - 1).first;
        }
        exceptions[it->second].settings.insert(setting, value);
    }
    return model->setValue(parameter, Tool::writeImageExceptions(exceptions));
}

QVariant PDFAppToolImageOverrideStore::readValue(const QString& key) const
{
    if (!isAvailable())
    {
        return QVariant();
    }

    // The value of the first image of the target, the global one without an exception
    const QString parameter = key.section(QLatin1Char('.'), 1);
    const QVariant value = readSetting(m_model, m_target.front(), getExceptionSetting(parameter));
    return value.isValid() ? value : m_model->getValue(parameter);
}

QString PDFAppToolImageOverrideStore::writeValue(const QString& key, const QVariant& value)
{
    if (!isAvailable())
    {
        return QString();
    }
    return writeSetting(m_model, m_target, getExceptionSetting(key.section(QLatin1Char('.'), 1)), value);
}

// ---------------------------------------------------------- PDFAppToolImageProfileEditor

PDFAppToolImageProfileEditor::PDFAppToolImageProfileEditor(PDFAppToolParameterModel* model, Mode mode, QWidget* parent) :
    BaseClass(parent),
    m_model(model),
    m_mode(mode),
    m_profile(QString::fromLatin1(mode == Mode::SingleProfile ? PDFDocumentToolOptimizeImages::PROFILE_GRAY : PDFDocumentToolOptimizeImages::PROFILE_COLOR))
{
    setObjectName(QStringLiteral("toolImageProfileEditor"));

    // section-Settings editor 298:3332: gap 8
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFAppToolPanelBuilder::SECTION_GAP);

    // A typed threshold rests as the fields of the binder; the write comes from the event loop. The timer is a
    // child (in the subtree of the editor), so retire writes a resting value (setFlushOnRetire)
    m_thresholdTimer.setParent(this);
    m_thresholdTimer.setSingleShot(true);
    m_thresholdTimer.setInterval(PDFAppSettingControl::WRITE_DELAY);
    connect(&m_thresholdTimer, &QTimer::timeout, this, &PDFAppToolImageProfileEditor::onThresholdTimeout);
    PDFDeferredDeletion::setFlushOnRetire(&m_thresholdTimer);

    // header 298:3333: the title and the Tag of the scope (gap 8)
    QWidget* header = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, header);
    m_title->setObjectName(QStringLiteral("toolImageProfileTitle"));
    headerLayout->addWidget(m_title);
    m_scopeTag = new PDFTag(header);
    m_scopeTag->setObjectName(QStringLiteral("toolImageProfileScope"));
    m_scopeTag->setFocusPolicy(Qt::NoFocus);
    m_scopeTag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    headerLayout->addWidget(m_scopeTag);
    headerLayout->addStretch(1);
    layout->addWidget(header);

    // scope-note 298:3337: Regular 11 text/muted
    m_note = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 15, PDFThemeColors::TextMuted, this);
    m_note->setObjectName(QStringLiteral("toolImageProfileNote"));
    layout->addWidget(m_note);

    // profile-tabs 298:3338: Filter chips 32 high, gap 6
    m_chips = new QWidget(this);
    m_chips->setObjectName(QStringLiteral("toolImageProfileChips"));
    QHBoxLayout* chipsLayout = new QHBoxLayout(m_chips);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(6);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);
    const char* profiles[] = { PDFDocumentToolOptimizeImages::PROFILE_COLOR, PDFDocumentToolOptimizeImages::PROFILE_GRAY, PDFDocumentToolOptimizeImages::PROFILE_BITONAL };
    for (int i = 0; i < 3; ++i)
    {
        QPushButton* chip = new QPushButton(m_chips);
        chip->setObjectName(QStringLiteral("toolImageProfile-") + QString::fromLatin1(profiles[i]));
        chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
        chip->setCheckable(true);
        chip->setAutoDefault(false);
        chip->setFixedHeight(32);
        chip->setChecked(m_profile == QLatin1String(profiles[i]));
        m_chipGroup->addButton(chip, i);
        chipsLayout->addWidget(chip);
    }
    chipsLayout->addStretch(1);
    connect(m_chipGroup.data(), &QButtonGroup::idToggled, this, &PDFAppToolImageProfileEditor::onProfileChipToggled);
    m_chips->setVisible(mode == Mode::ThreeProfiles);
    layout->addWidget(m_chips);

    // profile 298:3357: the card of the fields (padding 12, gap 8)
    m_card = new PDFAppToolCard(this);
    m_card->setObjectName(QStringLiteral("toolImageProfileCard"));
    m_cardLayout = m_card->getBodyLayout();
    m_cardLayout->setContentsMargins(13, 13, 13, 13);
    m_cardLayout->setSpacing(8);
    layout->addWidget(m_card);

    if (m_model)
    {
        PDFDeferredDeletion::connect(m_model.data(), &PDFAppToolParameterModel::changed, this, &PDFAppToolImageProfileEditor::onModelChanged);
    }

    rebuild();
}

PDFAppToolImageProfileEditor::~PDFAppToolImageProfileEditor()
{
    // ~QWidget runs after the derived part is gone and may move the focus out of the threshold
    // field (a window closes, a parent is hidden); QAbstractSpinBox then emits editingFinished. No
    // signal of a child or of the model may reach a member slot of this any more (a UBSan finding)
    for (QObject* child : findChildren<QObject*>())
    {
        disconnect(child, nullptr, this, nullptr);
    }
    if (m_model)
    {
        disconnect(m_model.data(), nullptr, this, nullptr);
    }
    m_thresholdTimer.stop();
}

void PDFAppToolImageProfileEditor::setProfile(const QString& profile)
{
    if (m_mode == Mode::SingleProfile || m_profile == profile)
    {
        return;
    }

    m_profile = profile;
    rebuild();
}

void PDFAppToolImageProfileEditor::setOverrideTarget(const std::vector<PDFDocumentImageList::Key>& keys)
{
    if (m_overrideTarget == keys)
    {
        return;
    }

    m_overrideTarget = keys;
    rebuild();
}

QString PDFAppToolImageProfileEditor::getParameterName(const char* field) const
{
    if (m_mode == Mode::SingleProfile)
    {
        return PDFDocumentToolOptimizeImages::getProfileParameterName(nullptr, field);
    }
    const QByteArray profile = m_profile.toLatin1();
    return PDFDocumentToolOptimizeImages::getProfileParameterName(profile.constData(), field);
}

QPushButton* PDFAppToolImageProfileEditor::getProfileChip(const QString& profile) const
{
    const char* profiles[] = { PDFDocumentToolOptimizeImages::PROFILE_COLOR, PDFDocumentToolOptimizeImages::PROFILE_GRAY, PDFDocumentToolOptimizeImages::PROFILE_BITONAL };
    for (int i = 0; i < 3; ++i)
    {
        if (profile == QLatin1String(profiles[i]))
        {
            return qobject_cast<QPushButton*>(m_chipGroup->button(i));
        }
    }
    return nullptr;
}

PDFAppSettingControl* PDFAppToolImageProfileEditor::getControl(const QString& parameter) const
{
    const QString key = QStringLiteral("tool.") + parameter;
    for (const QPointer<PDFAppSettingControl>& control : m_controls)
    {
        if (control && control->getKey() == key)
        {
            return control.data();
        }
    }
    return nullptr;
}

bool PDFAppToolImageProfileEditor::isBitonalProfile() const
{
    return m_mode == Mode::ThreeProfiles && m_profile == QLatin1String(PDFDocumentToolOptimizeImages::PROFILE_BITONAL);
}

std::shared_ptr<PDFAppValueStore> PDFAppToolImageProfileEditor::createStore() const
{
    if (!m_overrideTarget.empty())
    {
        return std::make_shared<PDFAppToolImageOverrideStore>(m_model.data(), m_overrideTarget);
    }
    return m_model->createValueStore();
}

void PDFAppToolImageProfileEditor::rebuild()
{
    // The controls of the shown profile are made again (a profile, an override target). The old
    // ones go from the event loop: a rebuild runs also inside of a signal of one of them (the
    // threshold writes the model, the tool shows the profile of the override; PA6: deleteLater stays,
    // see PDFAppToolImageProfileEditorHelper::retire)
    for (const QPointer<PDFAppSettingControl>& control : m_controls)
    {
        if (control)
        {
            control->deleteLater();
        }
    }
    m_controls.clear();
    m_fieldLabels.clear();
    while (QLayoutItem* item = m_cardLayout->takeAt(0))
    {
        if (QWidget* widget = item->widget())
        {
            PDFAppToolImageProfileEditorHelper::retire(widget);
        }
        delete item;
    }
    m_thresholdAuto.clear();
    m_thresholdSpinBox.clear();
    m_help.clear();

    if (!m_model)
    {
        updateTexts();
        return;
    }

    using Tool = PDFDocumentToolOptimizeImages;
    const bool isBitonal = isBitonalProfile();
    addField(Tool::FIELD_ALGORITHM, tr("Algorithm"));
    addField(Tool::FIELD_TARGET_DPI, tr("Target DPI"));
    addField(Tool::FIELD_RESAMPLE, tr("Resample"));
    if (isBitonal)
    {
        addThreshold();
    }
    else
    {
        addField(Tool::FIELD_JPEG_QUALITY, tr("JPEG quality"));
        addField(Tool::FIELD_JPEG2000_RATE, tr("JPEG2000 rate"));
    }

    // check-png-predictor 298:3400
    const QString predictor = getParameterName(Tool::FIELD_PNG_PREDICTOR);
    if (std::optional<PDFSettingDefinition> definition = m_model->toSettingDefinition(predictor))
    {
        PDFAppSettingControl* control = new PDFAppSettingControl(nullptr, *definition, m_card, createStore());
        control->getWidget()->setObjectName(QStringLiteral("toolImageProfileField-") + predictor);
        m_controls.push_back(control);
        m_cardLayout->addWidget(control->getWidget());
    }

    // help 298:3405: Regular 11 text/muted
    m_help = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 15, PDFThemeColors::TextMuted, m_card);
    m_help->setObjectName(QStringLiteral("toolImageProfileHelp"));
    m_cardLayout->addWidget(m_help);

    updateTexts();
    refreshControls();
}

void PDFAppToolImageProfileEditor::addField(const char* field, const QString& title)
{
    const QString parameter = getParameterName(field);
    std::optional<PDFSettingDefinition> definition = m_model->toSettingDefinition(parameter);
    if (!definition)
    {
        return;
    }

    // field-Target DPI 298:3372: the label 111 Regular 12 text/secondary, the control 140 x 34, gap 10
    QWidget* row = new QWidget(m_card);
    row->setObjectName(QStringLiteral("toolImageProfileRow-") + parameter);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(FIELD_GAP);
    PDFLabel* label = PDFAppPageWidgets::createLabel(title, QFont::Normal, 12, PDFThemeColors::TextSecondary, row);
    label->setMinimumWidth(1);
    rowLayout->addWidget(label, 1);
    m_fieldLabels.push_back(label);

    PDFAppSettingControl* control = new PDFAppSettingControl(nullptr, *definition, row, createStore());
    m_controls.push_back(control);
    QWidget* editor = control->getEditor();
    editor->setObjectName(QStringLiteral("toolImageProfileField-") + parameter);
    editor->setAccessibleName(title);
    editor->setFixedSize(FIELD_WIDTH, FIELD_HEIGHT);
    if (PDFSpinBox* spinBox = control->getSpinBox())
    {
        if (QLatin1String(field) == QLatin1String(PDFDocumentToolOptimizeImages::FIELD_TARGET_DPI))
        {
            // 298:3372 "200 dpi": the unit is the muted suffix beside the value, not edited text
            spinBox->setDisplaySuffix(tr("dpi"));
        }
    }
    if (PDFDoubleSpinBox* spinBox = control->getDoubleSpinBox())
    {
        spinBox->setDecimals(2);
    }
    label->setBuddy(editor);
    control->getWidget()->setParent(row);
    rowLayout->addWidget(control->getWidget(), 0, Qt::AlignVCenter);
    m_cardLayout->addWidget(row);
}

void PDFAppToolImageProfileEditor::addThreshold()
{
    // field-Threshold 301:3594: the spin box (disabled with Auto) and the check box Auto
    const QString parameter = QString::fromLatin1(PDFDocumentToolOptimizeImages::PARAMETER_BITONAL_THRESHOLD);
    QWidget* row = new QWidget(m_card);
    row->setObjectName(QStringLiteral("toolImageProfileRow-") + parameter);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(FIELD_GAP);
    PDFLabel* label = PDFAppPageWidgets::createLabel(tr("Threshold"), QFont::Normal, 12, PDFThemeColors::TextSecondary, row);
    label->setMinimumWidth(1);
    rowLayout->addWidget(label, 1);
    m_fieldLabels.push_back(label);

    m_thresholdSpinBox = new PDFSpinBox(row);
    m_thresholdSpinBox->setObjectName(QStringLiteral("toolImageProfileField-") + parameter);
    m_thresholdSpinBox->setRange(0, 255);
    m_thresholdSpinBox->setFixedSize(FIELD_WIDTH, FIELD_HEIGHT);
    m_thresholdSpinBox->setAccessibleName(tr("Threshold"));
    label->setBuddy(m_thresholdSpinBox);
    connect(m_thresholdSpinBox.data(), &QSpinBox::valueChanged, this, &PDFAppToolImageProfileEditor::onThresholdValueChanged);
    connect(m_thresholdSpinBox.data(), &QSpinBox::editingFinished, this, &PDFAppToolImageProfileEditor::onThresholdEditingFinished);
    rowLayout->addWidget(m_thresholdSpinBox);

    m_thresholdAuto = new QCheckBox(row);
    m_thresholdAuto->setObjectName(QStringLiteral("toolImageProfileThresholdAuto"));
    connect(m_thresholdAuto.data(), &QCheckBox::toggled, this, &PDFAppToolImageProfileEditor::onThresholdAutoToggled);
    rowLayout->addWidget(m_thresholdAuto);
    m_cardLayout->addWidget(row);
}

void PDFAppToolImageProfileEditor::refreshControls()
{
    for (const QPointer<PDFAppSettingControl>& control : m_controls)
    {
        if (control)
        {
            control->refresh();
        }
    }

    if (m_thresholdSpinBox && m_thresholdAuto && m_model)
    {
        // The threshold of the override or the global one; -1 = automatic
        const QString parameter = QString::fromLatin1(PDFDocumentToolOptimizeImages::PARAMETER_BITONAL_THRESHOLD);
        const std::shared_ptr<PDFAppValueStore> store = createStore();
        const int threshold = store->readValue(QStringLiteral("tool.") + parameter).toInt();
        m_isUpdating = true;
        m_thresholdAuto->setChecked(threshold < 0);
        m_thresholdSpinBox->setValue(threshold < 0 ? PDFAppToolImageProfileEditorHelper::DEFAULT_THRESHOLD : threshold);
        m_thresholdSpinBox->setEnabled(threshold >= 0);
        m_isUpdating = false;
    }
}

void PDFAppToolImageProfileEditor::updateTexts()
{
    using Tool = PDFDocumentToolOptimizeImages;

    const int targetCount = int(m_overrideTarget.size());
    if (m_mode == Mode::SingleProfile)
    {
        // 308:6911: "Grayscale images" - "All converted images"
        m_title->setText(tr("Grayscale images"));
        m_scopeTag->setText(tr("All converted images"));
        m_note->setText(tr("How the converted images are stored. The defaults keep the resolution and the quality."));
    }
    else
    {
        m_title->setText(tr("Settings editor"));
        if (targetCount > 0)
        {
            // 301:3594: "Override · 3 images"
            m_scopeTag->setText(tr("Override · %Ln images", nullptr, targetCount));
            m_note->setText(tr("Edits apply to the overrides of %Ln selected images. Toolbar menus (Mode, Color, Goal, Options) change the override too.", nullptr, targetCount));
        }
        else
        {
            m_scopeTag->setText(tr("Global defaults"));
            m_note->setText(tr("Edits change the global settings used by images without an override."));
        }
    }

    const QStringList chipTexts = { tr("Color"), tr("Grayscale"), tr("Bitonal") };
    for (int i = 0; i < chipTexts.size(); ++i)
    {
        if (QAbstractButton* chip = m_chipGroup->button(i))
        {
            chip->setText(PDFAppPageWidgets::escapeMnemonic(chipTexts[i]));
            chip->setAccessibleName(chipTexts[i]);
        }
    }

    if (PDFAppSettingControl* predictor = getControl(getParameterName(Tool::FIELD_PNG_PREDICTOR)))
    {
        if (QCheckBox* checkBox = predictor->getCheckBox())
        {
            checkBox->setText(tr("PNG predictor"));
        }
    }
    if (m_thresholdAuto)
    {
        m_thresholdAuto->setText(tr("Auto"));
    }

    if (m_help)
    {
        if (m_mode == Mode::SingleProfile)
        {
            m_help->setText(tr("Algorithms: Auto, Flate, JPEG, JPEG2000, RunLength · Target DPI 0 keeps the original resolution · JPEG2000 rate 0 is lossless"));
        }
        else if (isBitonalProfile())
        {
            m_help->setText(tr("Algorithms for bitonal images: Auto (JBIG2), Flate, RunLength, CCITT Group 4, JBIG2 · Threshold 0–255, Auto picks it from the image"));
        }
        else
        {
            m_help->setText(tr("Target DPI 0 keeps the original resolution · JPEG2000 rate 0 is lossless"));
        }
    }
}

void PDFAppToolImageProfileEditor::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        rebuild();
    }
}

void PDFAppToolImageProfileEditor::onProfileChipToggled(int id, bool isChecked)
{
    const char* profiles[] = { PDFDocumentToolOptimizeImages::PROFILE_COLOR, PDFDocumentToolOptimizeImages::PROFILE_GRAY, PDFDocumentToolOptimizeImages::PROFILE_BITONAL };
    if (!isChecked || id < 0 || id >= 3 || m_profile == QLatin1String(profiles[id]))
    {
        return;
    }

    m_profile = QString::fromLatin1(profiles[id]);
    rebuild();

    // 301:3594: the profile of an override with its own color is that color (the tool shows the
    // profile of the color of the override, "Toolbar menus change the override too"): the chip
    // changes the color of the override, otherwise its fields would edit the other color and the
    // tool would switch the editor back. An override of Auto / Keep keeps its color. An editor retired meanwhile
    // announces nothing (step A).
    if (!m_overrideTarget.empty() && m_model)
    {
        using Helper = PDFAppToolImageProfileEditorHelper;
        const QString colorModeSetting = QString::fromLatin1(PDFDocumentToolOptimizeImages::EXCEPTION_COLOR_MODE);
        const QString colorMode = PDFAppToolImageOverrideStore::readSetting(m_model.data(), m_overrideTarget.front(), colorModeSetting).toString();
        if (Helper::isProfileColorMode(colorMode))
        {
            // The model runs the slots of the tool (the last call before the signal)
            PDFAppToolImageOverrideStore::writeSetting(m_model.data(), m_overrideTarget, colorModeSetting, Helper::getColorModeOfProfile(m_profile));
        }
    }
    if (!PDFDeferredDeletion::isRetired(this))
    {
        Q_EMIT profileChanged(m_profile);
    }
}

void PDFAppToolImageProfileEditor::onModelChanged(const QString& name)
{
    Q_UNUSED(name);

    // A change of the user, Undo and Redo: the controls show the values of the model
    refreshControls();
}

void PDFAppToolImageProfileEditor::onThresholdAutoToggled(bool isChecked)
{
    if (m_isUpdating || !m_model || !m_thresholdSpinBox)
    {
        return;
    }

    scheduleThresholdWrite(isChecked ? -1 : m_thresholdSpinBox->value(), false);
}

void PDFAppToolImageProfileEditor::onThresholdValueChanged(int value)
{
    if (m_isUpdating || !m_model || !m_thresholdAuto || m_thresholdAuto->isChecked())
    {
        return;
    }

    scheduleThresholdWrite(value, true);
}

void PDFAppToolImageProfileEditor::onThresholdEditingFinished()
{
    // Enter or the loss of the focus: the typed value is written now (from the event loop)
    if (m_pendingThreshold && m_thresholdTimer.isActive())
    {
        queueThresholdWrite();
    }
}

void PDFAppToolImageProfileEditor::onThresholdTimeout()
{
    // The rest has ended (or retire flushes the timer)
    queueThresholdWrite();
}

void PDFAppToolImageProfileEditor::queueThresholdWrite()
{
    m_thresholdTimer.stop();
    if (PDFDeferredDeletion::isRetired(this))
    {
        // retire drops the queued calls of the editor: the value is written now (the last call)
        writePendingThreshold();
        return;
    }
    m_isThresholdWriteQueued = true;
    QMetaObject::invokeMethod(this, &PDFAppToolImageProfileEditor::writePendingThreshold, Qt::QueuedConnection);
}

void PDFAppToolImageProfileEditor::scheduleThresholdWrite(int threshold, bool isTyped)
{
    // The store of the target of now (the selection may change before the write); more changes
    // of one event (or of one typing) make one write
    m_pendingThreshold = threshold;
    m_pendingThresholdStore = createStore();
    if (isTyped && !m_isThresholdWriteQueued)
    {
        m_thresholdTimer.start();
    }
    else if (!m_isThresholdWriteQueued)
    {
        queueThresholdWrite();
    }
}

void PDFAppToolImageProfileEditor::writePendingThreshold()
{
    m_isThresholdWriteQueued = false;
    m_thresholdTimer.stop();
    if (!m_pendingThreshold || !m_pendingThresholdStore)
    {
        return;
    }

    const int threshold = *m_pendingThreshold;
    const std::shared_ptr<PDFAppValueStore> store = std::move(m_pendingThresholdStore);
    m_pendingThreshold.reset();
    m_pendingThresholdStore.reset();

    // The model runs the slots of the tool (they may delete this editor): the last call
    const QString key = QStringLiteral("tool.") + QString::fromLatin1(PDFDocumentToolOptimizeImages::PARAMETER_BITONAL_THRESHOLD);
    store->writeValue(key, threshold);
}

}   // namespace pdf

#include "moc_pdfapptoolimageprofileeditor.cpp"
