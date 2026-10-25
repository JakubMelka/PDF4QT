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

#include "pdfappsettingssection_speech.h"
#include "pdfappcontext.h"
#include "pdfapppagelayout.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdffluentwidgets.h"
#include "pdfthemeiconengine.h"

#include <QPushButton>
#include <QSignalBlocker>
#include <QTextToSpeech>

namespace pdf
{

/// Helpers of the section Speech
class PDFAppSpeechSettingsSectionHelper
{
public:
    PDFAppSpeechSettingsSectionHelper() = delete;

    /// Role of the item "(not available)" of a combo box
    static constexpr int NOT_AVAILABLE_ROLE = Qt::UserRole + 1;

    /// Largest number of the engines listed
    static constexpr int MAXIMUM_ENGINES = 64;

    /// A stored text shown to the user (a value of another computer, untrusted):
    /// printable characters of one line, cut
    static QString toDisplayText(const QString& text)
    {
        QString result;
        for (const QChar character : text.left(PDFAppSpeechSettingsSection::MAXIMUM_VALUE_TEXT))
        {
            result += character.isPrint() ? character : QChar(QLatin1Char(' '));
        }
        return result.simplified();
    }

    /// Fills a combo box (without signals; nothing is changed, when the items are the same)
    static void setItems(PDFComboBox* comboBox, const QStringList& ids, const QStringList& titles, const QStringList& toolTips, int notAvailableIndex, int currentIndex)
    {
        if (!comboBox)
        {
            return;
        }

        bool isSame = comboBox->count() == ids.size();
        for (int i = 0; isSame && i < ids.size(); ++i)
        {
            isSame = comboBox->itemData(i).toString() == ids[i] && comboBox->itemText(i) == titles[i] && comboBox->itemData(i, NOT_AVAILABLE_ROLE).toBool() == (i == notAvailableIndex);
        }

        const QSignalBlocker blocker(comboBox);
        if (!isSame)
        {
            comboBox->clear();
            for (int i = 0; i < ids.size(); ++i)
            {
                comboBox->addItem(titles[i], ids[i]);
                comboBox->setItemData(i, toolTips.value(i), Qt::ToolTipRole);
                comboBox->setItemData(i, i == notAvailableIndex, NOT_AVAILABLE_ROLE);
            }
        }
        comboBox->setCurrentIndex(currentIndex);
    }

    static bool isNotAvailable(const PDFComboBox* comboBox, int index)
    {
        return comboBox && comboBox->itemData(index, NOT_AVAILABLE_ROLE).toBool();
    }
};

PDFAppSpeechSettingsSection::EngineListProvider PDFAppSpeechSettingsSection::s_engineListProvider = nullptr;

PDFAppSpeechSettingsSection::PDFAppSpeechSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) :
    BaseClass(context, sectionId, parent)
{

}

PDFAppSpeechSettingsSection::~PDFAppSpeechSettingsSection()
{
    // A choice of the user is never lost; nobody is notified (the page goes)
    if (!m_pendingValues.empty() && getContext() && getContext()->getDatabase())
    {
        getContext()->getSettings().setValues(m_pendingValues);
    }

    if (m_engine)
    {
        m_engine->disconnect(this);
        m_engine->stop();
    }
}

void PDFAppSpeechSettingsSection::setEngineListProvider(EngineListProvider provider)
{
    s_engineListProvider = provider;
}

QStringList PDFAppSpeechSettingsSection::getAvailableEngines()
{
    const QStringList engines = s_engineListProvider ? s_engineListProvider() : QTextToSpeech::availableEngines();
    QStringList result;
    for (const QString& engine : engines)
    {
        if (!engine.isEmpty() && engine.size() <= MAXIMUM_VALUE_TEXT && !result.contains(engine))
        {
            result << engine;
        }
        if (result.size() >= PDFAppSpeechSettingsSectionHelper::MAXIMUM_ENGINES)
        {
            break;
        }
    }
    return result;
}

QString PDFAppSpeechSettingsSection::getEngineTitle(const QString& engineId)
{
    // Readable names of the plugins of QTextToSpeech (944:9031)
    if (engineId.isEmpty())
    {
        return tr("Automatic");
    }
    if (engineId == QLatin1String("winrt"))
    {
        return tr("Windows (WinRT)");
    }
    if (engineId == QLatin1String("sapi"))
    {
        return tr("Windows (SAPI 5)");
    }
    if (engineId == QLatin1String("speechd"))
    {
        return tr("Speech Dispatcher");
    }
    if (engineId == QLatin1String("flite"))
    {
        return tr("Festival Lite");
    }
    if (engineId == QLatin1String("darwin"))
    {
        return tr("macOS");
    }
    if (engineId == QLatin1String("android"))
    {
        return tr("Android");
    }
    return PDFAppSpeechSettingsSectionHelper::toDisplayText(engineId);
}

QString PDFAppSpeechSettingsSection::getSampleText()
{
    return tr("This is the voice, which PDF4QT uses to read your documents aloud.");
}

PDFAppSettingRow* PDFAppSpeechSettingsSection::createRow(const PDFSettingDefinition& definition)
{
    // The synthesizer, its locale and its voice are combo boxes of the engine (the
    // settings are texts, the options come from the engine)
    if (definition.getKey() == PDFSettingKeys::speechEngine())
    {
        m_engineRow = createComboRow(definition, m_engineComboBox);
        connect(m_engineComboBox.data(), &QComboBox::activated, this, &PDFAppSpeechSettingsSection::onEngineActivated);
        return m_engineRow;
    }
    if (definition.getKey() == PDFSettingKeys::speechLocale())
    {
        PDFAppSettingRow* row = createComboRow(definition, m_localeComboBox);
        connect(m_localeComboBox.data(), &QComboBox::activated, this, &PDFAppSpeechSettingsSection::onLocaleActivated);
        return row;
    }
    if (definition.getKey() == PDFSettingKeys::speechVoice())
    {
        PDFAppSettingRow* row = createComboRow(definition, m_voiceComboBox);
        connect(m_voiceComboBox.data(), &QComboBox::activated, this, &PDFAppSpeechSettingsSection::onVoiceActivated);
        return row;
    }
    return BaseClass::createRow(definition);
}

PDFAppSettingRow* PDFAppSpeechSettingsSection::createComboRow(const PDFSettingDefinition& definition, QPointer<PDFComboBox>& comboBox)
{
    const QString anchor = definition.getAnchor().isEmpty() ? definition.getKey() : definition.getAnchor();
    PDFAppSettingRow* row = new PDFAppSettingRow(definition.getTitle(), definition.getDescription(), anchor);

    QStringList keywords = definition.getKeywords();
    keywords << QString::fromUtf8(definition.getTitleSource()) << QString::fromUtf8(definition.getDescriptionSource());
    row->setSearchKeywords(keywords);

    // Combo box 260 x 34 (147:4101)
    comboBox = new PDFComboBox(row);
    comboBox->setObjectName(QStringLiteral("setting-%1").arg(anchor));
    comboBox->setFixedWidth(PDFAppSettingControl::CONTROL_WIDTH);
    comboBox->setAccessibleName(definition.getTitle());
    comboBox->setAccessibleDescription(definition.getDescription());
    row->setControlWidget(comboBox);
    return row;
}

void PDFAppSpeechSettingsSection::finishContent()
{
    // row-Preview 147:4166: Play sample (Secondary, play-circle, 136 x 36)
    PDFAppSettingGroup* group = findGroup(QStringLiteral("Sound"));
    if (!group)
    {
        group = addGroup(QStringLiteral("Sound"), tr("Sound"), QString());
    }

    m_previewRow = new PDFAppSettingRow(tr("Preview"), tr("Hear the current voice and settings"), getPreviewAnchor());
    m_previewRow->setSearchKeywords({ QStringLiteral("preview"), QStringLiteral("sample"), QStringLiteral("play"), QStringLiteral("test"), QStringLiteral("speech") });
    m_playButton = PDFAppPageWidgets::createButton(tr("Play sample"), QStringLiteral("play-circle"), false, m_previewRow);
    m_playButton->setObjectName(QStringLiteral("playSampleButton"));

    // Play sample and Stop keep the place of the button (stable buttons): the
    // width of the wider of the two, measured before the button is shown (the
    // page is built again in a new language)
    m_playButton->setText(tr("Stop"));
    m_playButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("stop")));
    const int stopWidth = m_playButton->sizeHint().width();
    m_playButton->setText(tr("Play sample"));
    m_playButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("play-circle")));
    m_playButton->setMinimumWidth(qMax(stopWidth, m_playButton->sizeHint().width()));
    connect(m_playButton.data(), &QPushButton::clicked, this, &PDFAppSpeechSettingsSection::onPlayClicked);
    m_previewRow->setControlWidget(m_playButton);
    group->addRow(m_previewRow);

    updateEngineItems();
    updateVoiceItems();
    updatePlayButton();
}

QString PDFAppSpeechSettingsSection::getStoredString(const QString& key) const
{
    PDFAppContext* context = getContext();
    return (context && context->getDatabase()) ? context->getSettings().getString(key) : QString();
}

void PDFAppSpeechSettingsSection::createEngine()
{
    if (m_engine)
    {
        m_engine->disconnect(this);
        m_engine->stop();
        m_engine->deleteLater();
        m_engine = nullptr;
    }

    m_isSampleRequested = false;
    m_engineName = getStoredString(PDFSettingKeys::speechEngine());
    m_engine = PDFAppSpeechEngine::create(m_engineName, this);
    if (m_engine)
    {
        connect(m_engine.data(), &PDFAppSpeechEngine::stateChanged, this, &PDFAppSpeechSettingsSection::onEngineStateChanged);
        connect(m_engine.data(), &PDFAppSpeechEngine::voicesChanged, this, &PDFAppSpeechSettingsSection::onEngineVoicesChanged);
        applySettingsToEngine();
    }

    updateVoiceItems();
    updatePlayButton();
}

void PDFAppSpeechSettingsSection::applySettingsToEngine()
{
    PDFAppContext* context = getContext();
    if (!m_engine || !context || !context->getDatabase())
    {
        return;
    }

    // The same as the speech of a document (PDFAppDocumentSpeechController::applySettings)
    const PDFSettingsRepository settings = context->getSettings();
    const QString localeName = settings.getString(PDFSettingKeys::speechLocale());
    if (!localeName.isEmpty())
    {
        m_engine->setLocale(QLocale(localeName));
    }
    m_engine->setVoice(settings.getString(PDFSettingKeys::speechVoice()));
    m_engine->setRate(settings.getReal(PDFSettingKeys::speechRate()));
    m_engine->setPitch(settings.getReal(PDFSettingKeys::speechPitch()));
    m_engine->setVolume(settings.getReal(PDFSettingKeys::speechVolume()));
}

void PDFAppSpeechSettingsSection::updateEngineItems()
{
    // Automatic (the engine Qt chooses) and the engines of the system; a stored
    // engine of another computer is not available
    const QString stored = getStoredString(PDFSettingKeys::speechEngine());
    QStringList ids = { QString() };
    ids << getAvailableEngines();

    QStringList titles;
    QStringList toolTips;
    for (const QString& id : std::as_const(ids))
    {
        titles << getEngineTitle(id);
        toolTips << (id.isEmpty() ? tr("The default voice synthesizer of the system") : id);
    }

    int notAvailableIndex = -1;
    int currentIndex = int(ids.indexOf(stored));
    if (currentIndex < 0)
    {
        const QString text = PDFAppSpeechSettingsSectionHelper::toDisplayText(stored);
        ids << stored.left(MAXIMUM_VALUE_TEXT);
        titles << tr("%1 (not available)").arg(text);
        toolTips << text;
        notAvailableIndex = int(ids.size()) - 1;
        currentIndex = notAvailableIndex;
    }

    PDFAppSpeechSettingsSectionHelper::setItems(m_engineComboBox, ids, titles, toolTips, notAvailableIndex, currentIndex);
}

void PDFAppSpeechSettingsSection::updateVoiceItems()
{
    const PDFSettingDefinition* engineDefinition = getContext() ? getContext()->getSettingRegistry()->findDefinition(PDFSettingKeys::speechEngine()) : nullptr;
    const PDFAppSpeechEngine::State state = m_engine ? m_engine->getState() : PDFAppSpeechEngine::State::Initializing;
    const bool isReady = m_engine && state != PDFAppSpeechEngine::State::Initializing && state != PDFAppSpeechEngine::State::Error;
    const QString storedLocale = getStoredString(PDFSettingKeys::speechLocale());
    const QString storedVoice = getStoredString(PDFSettingKeys::speechVoice());

    // An engine in the error state: the row of the synthesizer says so
    if (m_engineRow)
    {
        const bool isError = m_engine && state == PDFAppSpeechEngine::State::Error;
        m_engineRow->setDescription(isError ? tr("The voice synthesizer is not available") : (engineDefinition ? engineDefinition->getDescription() : QString()));
        m_engineRow->setToolTip(isError ? PDFAppSpeechSettingsSectionHelper::toDisplayText(m_engine->getErrorString()) : QString());
    }

    if (!isReady)
    {
        // The engine is starting or not available: the stored values are shown
        const QString localeText = storedLocale.isEmpty() ? tr("Automatic") : PDFAppDocumentSpeechController::getLocaleText(QLocale(storedLocale));
        const QString voiceText = storedVoice.isEmpty() ? tr("Automatic") : PDFAppSpeechSettingsSectionHelper::toDisplayText(storedVoice);
        PDFAppSpeechSettingsSectionHelper::setItems(m_localeComboBox, { storedLocale }, { localeText }, { QString() }, -1, 0);
        PDFAppSpeechSettingsSectionHelper::setItems(m_voiceComboBox, { storedVoice }, { voiceText }, { QString() }, -1, 0);
        if (m_localeComboBox)
        {
            m_localeComboBox->setEnabled(false);
        }
        if (m_voiceComboBox)
        {
            m_voiceComboBox->setEnabled(false);
        }
        return;
    }

    // Locales of the engine: native language (native territory) – English name
    {
        QStringList ids;
        QStringList titles;
        QStringList toolTips;
        for (const QLocale& locale : m_engine->getLocales())
        {
            if (ids.contains(locale.name()))
            {
                continue;
            }
            ids << locale.name();
            titles << PDFAppDocumentSpeechController::getLocaleText(locale);
            toolTips << QStringLiteral("%1 (%2)").arg(QLocale::languageToString(locale.language()), locale.name());
        }

        const QString current = storedLocale.isEmpty() ? m_engine->getLocale().name() : storedLocale;
        int currentIndex = int(ids.indexOf(current));
        int notAvailableIndex = -1;
        if (currentIndex < 0 && !storedLocale.isEmpty())
        {
            const QString text = PDFAppSpeechSettingsSectionHelper::toDisplayText(storedLocale);
            ids << storedLocale;
            titles << tr("%1 (not available)").arg(text);
            toolTips << text;
            notAvailableIndex = int(ids.size()) - 1;
            currentIndex = notAvailableIndex;
        }
        PDFAppSpeechSettingsSectionHelper::setItems(m_localeComboBox, ids, titles, toolTips, notAvailableIndex, currentIndex);
        if (m_localeComboBox)
        {
            m_localeComboBox->setEnabled(!ids.isEmpty());
        }
    }

    // Voices of the locale of the engine: name (gender, age)
    {
        QStringList ids;
        QStringList titles;
        QStringList toolTips;
        for (const PDFAppSpeechVoice& voice : m_engine->getVoices())
        {
            if (voice.name.isEmpty() || ids.contains(voice.name))
            {
                continue;
            }
            ids << voice.name;
            titles << PDFAppSpeechSettingsSectionHelper::toDisplayText(voice.displayText.isEmpty() ? voice.name : voice.displayText);
            toolTips << PDFAppSpeechSettingsSectionHelper::toDisplayText(voice.name);
        }

        const QString current = storedVoice.isEmpty() ? m_engine->getVoice() : storedVoice;
        int currentIndex = int(ids.indexOf(current));
        int notAvailableIndex = -1;
        if (currentIndex < 0 && !storedVoice.isEmpty())
        {
            const QString text = PDFAppSpeechSettingsSectionHelper::toDisplayText(storedVoice);
            ids << storedVoice;
            titles << tr("%1 (not available)").arg(text);
            toolTips << text;
            notAvailableIndex = int(ids.size()) - 1;
            currentIndex = notAvailableIndex;
        }
        PDFAppSpeechSettingsSectionHelper::setItems(m_voiceComboBox, ids, titles, toolTips, notAvailableIndex, currentIndex);
        if (m_voiceComboBox)
        {
            m_voiceComboBox->setEnabled(!ids.isEmpty());
        }
    }
}

bool PDFAppSpeechSettingsSection::isPlaying() const
{
    if (!m_engine || !m_isSampleRequested)
    {
        return false;
    }

    const PDFAppSpeechEngine::State state = m_engine->getState();
    return state == PDFAppSpeechEngine::State::Speaking || state == PDFAppSpeechEngine::State::Paused;
}

void PDFAppSpeechSettingsSection::updatePlayButton()
{
    if (!m_playButton)
    {
        return;
    }

    // Play sample, Stop while the sample is said; disabled without an engine
    const bool isPlayingSample = isPlaying();
    m_playButton->setText(isPlayingSample ? tr("Stop") : tr("Play sample"));
    m_playButton->setIcon(PDFThemeIcons::getIcon(isPlayingSample ? QStringLiteral("stop") : QStringLiteral("play-circle")));

    const PDFAppSpeechEngine::State state = m_engine ? m_engine->getState() : PDFAppSpeechEngine::State::Ready;
    m_playButton->setEnabled(state != PDFAppSpeechEngine::State::Error && state != PDFAppSpeechEngine::State::Initializing);
}

void PDFAppSpeechSettingsSection::playSample()
{
    if (!m_engine)
    {
        createEngine();
    }

    if (!m_engine)
    {
        return;
    }

    const PDFAppSpeechEngine::State state = m_engine->getState();
    if (state == PDFAppSpeechEngine::State::Error || state == PDFAppSpeechEngine::State::Initializing)
    {
        return;
    }

    if (state == PDFAppSpeechEngine::State::Speaking || state == PDFAppSpeechEngine::State::Paused)
    {
        m_engine->stop();
    }

    // The stored voice, rate, pitch and volume (also of a change of another instance)
    applySettingsToEngine();
    m_isSampleRequested = true;
    QPointer<PDFAppSpeechSettingsSection> guard(this);
    m_engine->say(getSampleText());
    if (guard)
    {
        updatePlayButton();
    }
}

void PDFAppSpeechSettingsSection::stopSample()
{
    if (!m_isSampleRequested)
    {
        return;
    }

    m_isSampleRequested = false;
    QPointer<PDFAppSpeechSettingsSection> guard(this);
    if (m_engine)
    {
        m_engine->stop();
    }
    if (guard)
    {
        updatePlayButton();
    }
}

void PDFAppSpeechSettingsSection::onPlayClicked()
{
    if (isPlaying())
    {
        stopSample();
    }
    else
    {
        playSample();
    }
}

void PDFAppSpeechSettingsSection::onEngineStateChanged()
{
    // The sample has ended (or the engine has failed)
    if (m_engine && m_isSampleRequested && !isPlaying())
    {
        m_isSampleRequested = false;
    }

    updateVoiceItems();
    updatePlayButton();
}

void PDFAppSpeechSettingsSection::onEngineVoicesChanged()
{
    updateVoiceItems();
}

void PDFAppSpeechSettingsSection::onEngineActivated(int index)
{
    if (!m_engineComboBox || index < 0 || PDFAppSpeechSettingsSectionHelper::isNotAvailable(m_engineComboBox, index))
    {
        return;
    }

    const QString engine = m_engineComboBox->itemData(index).toString();
    if (engine != getStoredString(PDFSettingKeys::speechEngine()))
    {
        queueWrite({ { PDFSettingKeys::speechEngine(), engine } });
    }
}

void PDFAppSpeechSettingsSection::onLocaleActivated(int index)
{
    if (!m_localeComboBox || index < 0 || PDFAppSpeechSettingsSectionHelper::isNotAvailable(m_localeComboBox, index))
    {
        return;
    }

    // The voice of another locale is not valid (as the Speech panel does)
    const QString locale = m_localeComboBox->itemData(index).toString();
    if (locale != getStoredString(PDFSettingKeys::speechLocale()))
    {
        queueWrite({ { PDFSettingKeys::speechLocale(), locale }, { PDFSettingKeys::speechVoice(), QString() } });
    }
}

void PDFAppSpeechSettingsSection::onVoiceActivated(int index)
{
    if (!m_voiceComboBox || index < 0 || PDFAppSpeechSettingsSectionHelper::isNotAvailable(m_voiceComboBox, index))
    {
        return;
    }

    const QString voice = m_voiceComboBox->itemData(index).toString();
    if (voice != getStoredString(PDFSettingKeys::speechVoice()))
    {
        queueWrite({ { PDFSettingKeys::speechVoice(), voice } });
    }
}

void PDFAppSpeechSettingsSection::queueWrite(std::vector<std::pair<QString, QVariant>> values)
{
    // A later choice of the same key replaces the earlier one
    for (std::pair<QString, QVariant>& value : values)
    {
        bool isReplaced = false;
        for (std::pair<QString, QVariant>& pending : m_pendingValues)
        {
            if (pending.first == value.first)
            {
                pending.second = value.second;
                isReplaced = true;
            }
        }
        if (!isReplaced)
        {
            m_pendingValues.push_back(std::move(value));
        }
    }

    // Written from the event loop: the slots of the notification may delete the
    // combo box, which is emitting its signals (Qt does not survive it)
    if (!m_isWriteQueued)
    {
        m_isWriteQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppSpeechSettingsSection::writePending, Qt::QueuedConnection);
    }
}

void PDFAppSpeechSettingsSection::writePending()
{
    m_isWriteQueued = false;
    const std::vector<std::pair<QString, QVariant>> values = std::move(m_pendingValues);
    m_pendingValues.clear();

    PDFAppContext* context = getContext();
    if (values.empty() || !context || !context->getDatabase())
    {
        return;
    }

    // The notification of the change may delete the page with this section
    QPointer<PDFAppSpeechSettingsSection> guard(this);
    const PDFDatabaseError error = context->getSettings().setValues(values);
    if (!guard)
    {
        return;
    }

    if (error.isError())
    {
        Q_EMIT statusMessageRequested(tr("The setting could not be saved: %1").arg(error.toString()));
        if (!guard)
        {
            return;
        }
    }

    // The combo boxes show the stored values (also after a refused write)
    refresh(PDFSettingsRepository::getTopic(), QString());
}

void PDFAppSpeechSettingsSection::commitPendingValues()
{
    QPointer<PDFAppSpeechSettingsSection> guard(this);
    BaseClass::commitPendingValues();
    if (guard)
    {
        writePending();
    }
}

void PDFAppSpeechSettingsSection::refresh(const QString& topic, const QString& key)
{
    QPointer<PDFAppSpeechSettingsSection> guard(this);
    BaseClass::refresh(topic, key);
    if (!guard || (!topic.isEmpty() && topic != PDFSettingsRepository::getTopic()))
    {
        return;
    }
    if (!key.isEmpty() && !key.startsWith(QLatin1String("speech.")))
    {
        return;
    }

    // A new synthesizer (also of another instance) - a new engine; otherwise the
    // engine takes the voice and the sound
    if (m_engine && getStoredString(PDFSettingKeys::speechEngine()) != m_engineName)
    {
        createEngine();
    }
    else
    {
        applySettingsToEngine();
    }

    updateEngineItems();
    updateVoiceItems();
    updatePlayButton();
}

std::vector<PDFAppSettingChange> PDFAppSpeechSettingsSection::getChangedValues() const
{
    // The values of the texts of the engine are shown by their names
    std::vector<PDFAppSettingChange> changes = BaseClass::getChangedValues();
    for (PDFAppSettingChange& change : changes)
    {
        PDFAppContext* context = getContext();
        const QString stored = (context && context->getDatabase()) ? context->getSettings().getString(change.key) : QString();
        if (change.key == PDFSettingKeys::speechEngine())
        {
            change.currentValue = getEngineTitle(stored);
            change.defaultValue = getEngineTitle(QString());
        }
        else if (change.key == PDFSettingKeys::speechLocale())
        {
            change.currentValue = stored.isEmpty() ? tr("Automatic") : PDFAppDocumentSpeechController::getLocaleText(QLocale(stored));
            change.defaultValue = tr("Automatic");
        }
        else if (change.key == PDFSettingKeys::speechVoice())
        {
            change.currentValue = stored.isEmpty() ? tr("Automatic") : PDFAppSpeechSettingsSectionHelper::toDisplayText(stored);
            change.defaultValue = tr("Automatic");
        }
    }
    return changes;
}

void PDFAppSpeechSettingsSection::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The engine is created, when the section is shown first (its locales and voices)
    if (!m_engine)
    {
        createEngine();
    }
}

void PDFAppSpeechSettingsSection::hideEvent(QHideEvent* event)
{
    // From the event loop: the hide event comes from inside of
    // QWidgetPrivate::hideChildren, which does not survive a slot of the stopped
    // engine (stateChanged), which deletes the widgets of the page
    BaseClass::hideEvent(event);
    QMetaObject::invokeMethod(this, &PDFAppSpeechSettingsSection::stopSampleWhenHidden, Qt::QueuedConnection);
}

void PDFAppSpeechSettingsSection::finishActivity()
{
    // The page goes: the sample is stopped now, not by the destructor (the engine
    // announces its state)
    stopSample();
}

void PDFAppSpeechSettingsSection::stopSampleWhenHidden()
{
    // A section shown again meanwhile keeps saying the sample
    if (!isVisible())
    {
        stopSample();
    }
}

PDFAppSettingsSection* PDFAppSpeechSettingsSectionFactory::createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const
{
    return new PDFAppSpeechSettingsSection(context, sectionId, parent);
}

}   // namespace pdf
