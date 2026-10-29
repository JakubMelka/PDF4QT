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

#include "pdfappdocumentspeech.h"
#include "pdfapplocale.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsearch.h"
#include "pdfappdocumentsearchindex.h"
#include "pdfappdocumentview.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfdocumenttextindex.h"
#include "pdfdrawspacecontroller.h"

#include "pdfcomponentbadges.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTextToSpeech>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVoice>

#include <cmath>

namespace pdf
{

/// Helpers of the speech
class PDFAppDocumentSpeechHelper
{
public:
    PDFAppDocumentSpeechHelper() = delete;

    /// Maximal length of one said part of a block (a block of a damaged page may be
    /// huge, a longer block is said in more parts)
    static constexpr qsizetype MAXIMUM_BLOCK_LENGTH = 4000;

    /// Splits the simplified text of a block into parts of at most \p maximumLength
    /// characters: after the end of a sentence, otherwise at a space, otherwise
    /// (one long word) at the limit; nothing of the text is left out
    static QStringList splitText(const QString& text, qsizetype maximumLength)
    {
        QStringList parts;
        qsizetype start = 0;
        while (start < text.size())
        {
            qsizetype end = text.size();
            if (end - start > maximumLength)
            {
                end = start + maximumLength;
                qsizetype sentenceEnd = -1;
                qsizetype space = -1;
                for (qsizetype i = end; i > start; --i)
                {
                    if (text.at(i) == QLatin1Char(' '))
                    {
                        if (space < 0)
                        {
                            space = i;
                        }
                        const QChar previous = text.at(i - 1);
                        if (previous == QLatin1Char('.') || previous == QLatin1Char('!') || previous == QLatin1Char('?'))
                        {
                            sentenceEnd = i;
                            break;
                        }
                    }
                }

                // A sentence end in the second half of the part, otherwise a space
                if (sentenceEnd > start + maximumLength / 2)
                {
                    end = sentenceEnd;
                }
                else if (space > start)
                {
                    end = space;
                }
            }

            const QString part = text.mid(start, end - start).trimmed();
            if (!part.isEmpty())
            {
                parts << part;
            }
            start = end;
        }
        return parts;
    }

    /// Steps of the sliders (Rate and Pitch -1 to 1, Volume 0 to 1)
    static constexpr int SLIDER_STEPS = 100;

    static QString formatValue(double value)
    {
        return PDFAppLocale::getUiLocale().toString(value, 'f', 2);
    }

    /// Caption of a field (Medium 11, text/muted)
    static PDFLabel* createCaption(QWidget* parent)
    {
        return new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, parent);
    }

    /// Toolbar button of the transport (speech-pause 195:2940: 36 x 36, icon 20)
    static QToolButton* createToolButton(const QString& objectName, const QString& iconName, QWidget* parent)
    {
        QToolButton* button = new QToolButton(parent);
        button->setObjectName(objectName);
        button->setAutoRaise(true);
        button->setIcon(PDFThemeIcons::getIcon(iconName));
        button->setIconSize(QSize(20, 20));
        button->setFixedSize(36, 36);
        return button;
    }

    /// One slider row (slider-row 195:2913: label 48, slider, value 30, gap 10)
    static QSlider* createSlider(const QString& objectName, int minimum, int maximum, QWidget* parent)
    {
        QSlider* slider = new QSlider(Qt::Horizontal, parent);
        slider->setObjectName(objectName);
        slider->setRange(minimum, maximum);
        slider->setFixedHeight(20);
        return slider;
    }
};

PDFAppSpeechEngine::Factory PDFAppSpeechEngine::s_factory = nullptr;

PDFAppSpeechEngine::PDFAppSpeechEngine(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppSpeechEngine::~PDFAppSpeechEngine() = default;

PDFAppSpeechEngine* PDFAppSpeechEngine::create(const QString& engineName, QObject* parent)
{
    if (s_factory)
    {
        return s_factory(engineName, parent);
    }
    return new PDFAppQtSpeechEngine(engineName, parent);
}

void PDFAppSpeechEngine::setFactory(Factory factory)
{
    s_factory = factory;
}

PDFAppQtSpeechEngine::PDFAppQtSpeechEngine(const QString& engineName, QObject* parent) :
    BaseClass(parent)
{
    // An unknown engine (a value of another computer) is the default engine
    QString engine = engineName;
    if (!engine.isEmpty() && !QTextToSpeech::availableEngines().contains(engine))
    {
        engine.clear();
    }

    m_textToSpeech = new QTextToSpeech(engine, this);
    connect(m_textToSpeech, &QTextToSpeech::stateChanged, this, &PDFAppQtSpeechEngine::onStateChanged);
    connect(m_textToSpeech, &QTextToSpeech::localeChanged, this, &PDFAppQtSpeechEngine::onLocaleChanged);
    connect(m_textToSpeech, &QTextToSpeech::sayingWord, this, &PDFAppQtSpeechEngine::onSayingWord);
    m_lastState = getState();
}

PDFAppQtSpeechEngine::~PDFAppQtSpeechEngine()
{
    if (m_textToSpeech)
    {
        m_textToSpeech->disconnect(this);
        m_textToSpeech->stop(QTextToSpeech::BoundaryHint::Immediate);
    }
}

PDFAppSpeechEngine::State PDFAppQtSpeechEngine::getState() const
{
    if (!m_textToSpeech)
    {
        return State::Error;
    }

    switch (m_textToSpeech->state())
    {
        case QTextToSpeech::Ready:
            return State::Ready;
        case QTextToSpeech::Speaking:
        case QTextToSpeech::Synthesizing:
            return State::Speaking;
        case QTextToSpeech::Paused:
            return State::Paused;
        case QTextToSpeech::Error:
            return State::Error;
    }
    return State::Error;
}

QString PDFAppQtSpeechEngine::getErrorString() const
{
    if (!m_textToSpeech)
    {
        return tr("The speech engine is not available.");
    }

    const QString error = m_textToSpeech->errorString();
    if (m_textToSpeech->state() == QTextToSpeech::Error && error.isEmpty())
    {
        return tr("The speech engine is not available.");
    }
    return error;
}

QList<QLocale> PDFAppQtSpeechEngine::getLocales() const
{
    return m_textToSpeech ? m_textToSpeech->availableLocales() : QList<QLocale>();
}

QLocale PDFAppQtSpeechEngine::getLocale() const
{
    // The language of the voice is not the language of the user interface (O17)
    return m_textToSpeech ? m_textToSpeech->locale() : PDFAppLocale::getRegionalLocale();
}

void PDFAppQtSpeechEngine::setLocale(const QLocale& locale)
{
    if (m_textToSpeech && m_textToSpeech->availableLocales().contains(locale) && m_textToSpeech->locale() != locale)
    {
        m_textToSpeech->setLocale(locale);
    }
}

QList<PDFAppSpeechVoice> PDFAppQtSpeechEngine::getVoices() const
{
    QList<PDFAppSpeechVoice> voices;
    if (!m_textToSpeech)
    {
        return voices;
    }

    for (const QVoice& voice : m_textToSpeech->availableVoices())
    {
        PDFAppSpeechVoice item;
        item.name = voice.name();
        item.displayText = tr("%1 (%2, %3)").arg(voice.name(), QVoice::genderName(voice.gender()), QVoice::ageName(voice.age()));
        voices.append(item);
    }
    return voices;
}

QString PDFAppQtSpeechEngine::getVoice() const
{
    return m_textToSpeech ? m_textToSpeech->voice().name() : QString();
}

void PDFAppQtSpeechEngine::setVoice(const QString& name)
{
    if (!m_textToSpeech || name.isEmpty() || m_textToSpeech->voice().name() == name)
    {
        return;
    }

    for (const QVoice& voice : m_textToSpeech->availableVoices())
    {
        if (voice.name() == name)
        {
            m_textToSpeech->setVoice(voice);
            break;
        }
    }
}

void PDFAppQtSpeechEngine::setRate(double rate)
{
    if (m_textToSpeech)
    {
        m_textToSpeech->setRate(qBound(-1.0, rate, 1.0));
    }
}

void PDFAppQtSpeechEngine::setPitch(double pitch)
{
    if (m_textToSpeech)
    {
        m_textToSpeech->setPitch(qBound(-1.0, pitch, 1.0));
    }
}

void PDFAppQtSpeechEngine::setVolume(double volume)
{
    if (m_textToSpeech)
    {
        m_textToSpeech->setVolume(qBound(0.0, volume, 1.0));
    }
}

void PDFAppQtSpeechEngine::say(const QString& text)
{
    if (m_textToSpeech)
    {
        m_textToSpeech->say(text);
    }
}

void PDFAppQtSpeechEngine::pause()
{
    if (m_textToSpeech)
    {
        m_textToSpeech->pause();
    }
}

void PDFAppQtSpeechEngine::resume()
{
    if (m_textToSpeech)
    {
        m_textToSpeech->resume();
    }
}

void PDFAppQtSpeechEngine::stop()
{
    if (m_textToSpeech)
    {
        m_textToSpeech->stop(QTextToSpeech::BoundaryHint::Immediate);
    }
}

void PDFAppQtSpeechEngine::onStateChanged()
{
    // The locales and voices of an engine are known, when it is ready first
    const State state = getState();
    const bool isStarted = m_lastState == State::Initializing && state == State::Ready;
    m_lastState = state;

    QPointer<PDFAppQtSpeechEngine> guard(this);
    if (isStarted)
    {
        Q_EMIT voicesChanged();
    }
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppQtSpeechEngine::onLocaleChanged()
{
    Q_EMIT voicesChanged();
}

void PDFAppQtSpeechEngine::onSayingWord(const QString& word, qsizetype id, qsizetype start, qsizetype length)
{
    Q_UNUSED(word);
    Q_UNUSED(id);
    Q_EMIT wordStarted(start, length);
}

PDFAppDocumentSpeechController::PDFAppDocumentSpeechController(PDFAppDocument* document, PDFAppDocumentView* view, PDFAppDocumentTextIndexer* indexer, QObject* parent) :
    BaseClass(parent),
    m_document(document),
    m_view(view),
    m_indexer(indexer)
{
    if (indexer)
    {
        connect(indexer, &PDFAppDocumentTextIndexer::indexReady, this, &PDFAppDocumentSpeechController::onIndexReady);
        connect(indexer, &PDFAppDocumentTextIndexer::buildingChanged, this, &PDFAppDocumentSpeechController::onIndexBuildingChanged);
    }
    if (document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppDocumentSpeechController::onDocumentChanged);
    }
    if (PDFAppContext* context = getContext())
    {
        if (context->getNotifier())
        {
            connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppDocumentSpeechController::onSettingChanged);
        }
    }

    createEngine();
}

PDFAppDocumentSpeechController::~PDFAppDocumentSpeechController()
{
    if (m_engine)
    {
        m_engine->disconnect(this);
        m_engine->stop();
    }
}

PDFAppContext* PDFAppDocumentSpeechController::getContext() const
{
    return m_document ? m_document->getContext() : nullptr;
}

void PDFAppDocumentSpeechController::createEngine()
{
    PDFAppContext* context = getContext();
    m_engineName = (context && context->getDatabase()) ? context->getSettings().getString(PDFSettingKeys::speechEngine()) : QString();

    if (m_engine)
    {
        m_engine->disconnect(this);
        m_engine->stop();
        m_engine->deleteLater();
    }

    m_engine = PDFAppSpeechEngine::create(m_engineName, this);
    if (m_engine)
    {
        connect(m_engine, &PDFAppSpeechEngine::stateChanged, this, &PDFAppDocumentSpeechController::onEngineStateChanged);
        connect(m_engine, &PDFAppSpeechEngine::voicesChanged, this, &PDFAppDocumentSpeechController::onEngineVoicesChanged);
        connect(m_engine, &PDFAppSpeechEngine::wordStarted, this, &PDFAppDocumentSpeechController::onWordStarted);
        connect(m_engine, &QObject::destroyed, this, &PDFAppDocumentSpeechController::onEngineDestroyed);
    }
    applySettings();
}

bool PDFAppDocumentSpeechController::isAvailable() const
{
    return m_engine && m_engine->getState() != PDFAppSpeechEngine::State::Error;
}

QString PDFAppDocumentSpeechController::getCurrentPageLabel() const
{
    if (!isReading() || m_currentPage < 0)
    {
        return QString();
    }

    const QString label = m_view ? m_view->getPageLabel(m_currentPage) : QString();
    return label.isEmpty() ? QString::number(m_currentPage + 1) : label;
}

QString PDFAppDocumentSpeechController::getErrorString() const
{
    if (!m_engine)
    {
        return tr("The speech engine is not available.");
    }
    return m_engine->getState() == PDFAppSpeechEngine::State::Error ? m_engine->getErrorString() : QString();
}

void PDFAppDocumentSpeechController::applySettings()
{
    PDFAppContext* context = getContext();
    if (!m_engine || !context || !context->getDatabase())
    {
        return;
    }

    const PDFSettingsRepository settings = context->getSettings();
    if (settings.getString(PDFSettingKeys::speechEngine()) != m_engineName)
    {
        stop();
        createEngine();

        QPointer<PDFAppDocumentSpeechController> guard(this);
        Q_EMIT voicesChanged();
        if (guard)
        {
            Q_EMIT stateChanged();
        }
        return;
    }

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

void PDFAppDocumentSpeechController::setLocale(const QLocale& locale)
{
    if (PDFAppContext* context = getContext(); context && context->getDatabase())
    {
        context->getSettings().setValue(PDFSettingKeys::speechLocale(), locale.name());
        // The voice of another locale is not valid
        context->getSettings().resetValue(PDFSettingKeys::speechVoice());
    }
    applySettings();
}

void PDFAppDocumentSpeechController::setVoice(const QString& name)
{
    if (PDFAppContext* context = getContext(); context && context->getDatabase())
    {
        context->getSettings().setValue(PDFSettingKeys::speechVoice(), name);
    }
    applySettings();
}

void PDFAppDocumentSpeechController::setRate(double rate)
{
    if (PDFAppContext* context = getContext(); context && context->getDatabase())
    {
        context->getSettings().setValue(PDFSettingKeys::speechRate(), qBound(-1.0, rate, 1.0));
    }
    applySettings();
}

void PDFAppDocumentSpeechController::setPitch(double pitch)
{
    if (PDFAppContext* context = getContext(); context && context->getDatabase())
    {
        context->getSettings().setValue(PDFSettingKeys::speechPitch(), qBound(-1.0, pitch, 1.0));
    }
    applySettings();
}

void PDFAppDocumentSpeechController::setVolume(double volume)
{
    if (PDFAppContext* context = getContext(); context && context->getDatabase())
    {
        context->getSettings().setValue(PDFSettingKeys::speechVolume(), qBound(0.0, volume, 1.0));
    }
    applySettings();
}

QString PDFAppDocumentSpeechController::getLocaleText(const QLocale& locale)
{
    const QString english = QStringLiteral("%1 (%2)").arg(QLocale::languageToString(locale.language()), QLocale::territoryToString(locale.territory()));
    if (locale.language() == QLocale::English)
    {
        // "English (United States)" of the design, not "American English (United States)"
        return english;
    }

    QString native = locale.nativeLanguageName();
    if (native.isEmpty())
    {
        native = QLocale::languageToString(locale.language());
    }
    if (!native.isEmpty())
    {
        native[0] = native[0].toUpper();
    }

    const QString territory = locale.nativeTerritoryName();
    QString text = territory.isEmpty() ? native : QStringLiteral("%1 (%2)").arg(native, territory);
    if (english != text)
    {
        text = tr("%1 – %2").arg(text, english);
    }
    return text;
}

void PDFAppDocumentSpeechController::setFollowingReading(bool follow)
{
    if (m_isFollowingReading != follow)
    {
        m_isFollowingReading = follow;
        if (follow && m_view && m_currentPage >= 0 && isReading())
        {
            m_view->goToPage(m_currentPage);
        }
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSpeechController::setState(State state)
{
    if (m_state != state)
    {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSpeechController::play()
{
    if (!isAvailable() || !m_document || !m_document->isReady())
    {
        return;
    }

    // The engine and the indexer announce their states at once (engine
    // stateChanged, indexer buildingChanged): a slot may delete the tab
    QPointer<PDFAppDocumentSpeechController> guard(this);
    switch (m_state)
    {
        case State::Paused:
            m_engine->resume();
            if (guard)
            {
                setState(State::Reading);
            }
            break;

        case State::Stopped:
        {
            const bool isIndexReady = m_indexer && m_indexer->requestIndex();
            if (!guard)
            {
                return;
            }

            if (isIndexReady)
            {
                startReading();
            }
            else if (m_indexer && m_indexer->isBuilding())
            {
                setState(State::Preparing);
            }
            break;
        }

        default:
            break;
    }
}

void PDFAppDocumentSpeechController::pause()
{
    if (m_state == State::Reading && m_engine)
    {
        QPointer<PDFAppDocumentSpeechController> guard(this);
        m_engine->pause();
        if (guard)
        {
            setState(State::Paused);
        }
    }
}

void PDFAppDocumentSpeechController::stop()
{
    m_isSpeakingBlock = false;
    QPointer<PDFAppDocumentSpeechController> guard(this);
    if (m_engine)
    {
        m_engine->stop();
        if (!guard)
        {
            return;
        }
    }
    m_blocks.clear();
    m_blockIndex = -1;
    m_currentText.clear();
    m_spokenLength = 0;

    setState(State::Stopped);
    if (guard)
    {
        Q_EMIT textChanged();
    }
}

void PDFAppDocumentSpeechController::startReading()
{
    const PDFInteger startPage = m_view ? qMax(PDFInteger(0), m_view->getDisplayedPageIndex()) : 0;
    m_blocks.clear();
    m_blockIndex = -1;
    m_currentPage = startPage - 1;

    QPointer<PDFAppDocumentSpeechController> guard(this);
    setState(State::Reading);
    if (guard && m_state == State::Reading)
    {
        speakNext();
    }
}

bool PDFAppDocumentSpeechController::loadPage(PDFInteger pageIndex)
{
    std::shared_ptr<const PDFDocumentTextIndex> index = m_indexer ? m_indexer->getIndex() : nullptr;
    if (!index || pageIndex < 0 || pageIndex >= index->getPageCount())
    {
        return false;
    }

    m_currentPage = pageIndex;
    m_blocks.clear();
    m_blockIndex = -1;

    const PDFTextFlows flows = PDFTextFlow::createTextFlows(index->getTextLayout(pageIndex),
                                                            PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen,
                                                            pageIndex);
    for (const PDFTextFlow& flow : flows)
    {
        // A long block is said in parts (none of its text is left out)
        m_blocks << PDFAppDocumentSpeechHelper::splitText(flow.getText().simplified(), PDFAppDocumentSpeechHelper::MAXIMUM_BLOCK_LENGTH);
    }
    return true;
}

void PDFAppDocumentSpeechController::speakNext()
{
    if (m_state != State::Reading || !m_engine)
    {
        return;
    }

    // The next block, otherwise the next page with a text
    ++m_blockIndex;
    while (m_blockIndex >= m_blocks.size())
    {
        if (!loadPage(m_currentPage + 1))
        {
            // The end of the document
            stop();
            return;
        }

        if (!m_blocks.isEmpty())
        {
            m_blockIndex = 0;
            if (m_isFollowingReading && m_view)
            {
                m_view->goToPage(m_currentPage);
            }
        }
    }

    m_currentText = m_blocks[m_blockIndex];
    m_spokenLength = 0;
    m_isSpeakingBlock = true;

    // The engine may announce its state at once (a slot may delete the tab)
    QPointer<PDFAppDocumentSpeechController> guard(this);
    m_engine->say(m_currentText);
    if (guard)
    {
        Q_EMIT textChanged();
    }
}

void PDFAppDocumentSpeechController::onEngineStateChanged()
{
    if (!m_engine)
    {
        return;
    }

    const PDFAppSpeechEngine::State engineState = m_engine->getState();
    if (engineState == PDFAppSpeechEngine::State::Error)
    {
        if (isReading())
        {
            stop();
        }
        else
        {
            Q_EMIT stateChanged();
        }
        return;
    }

    // The block has been said: the next one
    if (engineState == PDFAppSpeechEngine::State::Ready && m_state == State::Reading && m_isSpeakingBlock)
    {
        m_isSpeakingBlock = false;
        m_spokenLength = m_currentText.size();
        speakNext();
        return;
    }

    Q_EMIT stateChanged();
}

void PDFAppDocumentSpeechController::onEngineVoicesChanged()
{
    applySettings();
    Q_EMIT voicesChanged();
}

void PDFAppDocumentSpeechController::onEngineDestroyed()
{
    // The engine is gone (its pointer is cleared): the reading ends, the panel
    // shows that the speech is not available
    m_isSpeakingBlock = false;
    m_blocks.clear();
    m_blockIndex = -1;
    m_currentText.clear();
    m_spokenLength = 0;
    m_state = State::Stopped;

    QPointer<PDFAppDocumentSpeechController> guard(this);
    Q_EMIT textChanged();
    if (guard)
    {
        Q_EMIT voicesChanged();
    }
    if (guard)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppDocumentSpeechController::onWordStarted(qsizetype start, qsizetype length)
{
    if (start >= 0 && length >= 0 && start <= m_currentText.size())
    {
        m_spokenLength = qMin(m_currentText.size(), start + length);
        Q_EMIT textChanged();
    }
}

void PDFAppDocumentSpeechController::onIndexReady()
{
    if (m_state == State::Preparing)
    {
        startReading();
    }
}

void PDFAppDocumentSpeechController::onIndexBuildingChanged()
{
    // The build has been stopped
    if (m_state == State::Preparing && m_indexer && !m_indexer->isBuilding() && !m_indexer->getIndex())
    {
        setState(State::Stopped);
    }
}

void PDFAppDocumentSpeechController::onDocumentChanged(const PDFModifiedDocument& modified)
{
    // The text being read belongs to the old page contents
    if ((modified.hasReset() || modified.hasPageContentsChanged()) && isReading() && m_state != State::Preparing)
    {
        stop();
    }
}

void PDFAppDocumentSpeechController::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key.startsWith(QStringLiteral("speech.")))
    {
        applySettings();
    }
}

PDFAppDocumentSpeechPanel::PDFAppDocumentSpeechPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("speechPanel"));

    using Helper = PDFAppDocumentSpeechHelper;

    // The content scrolls in a small window (720 x 480)
    QVBoxLayout* panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("speechScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(scrollArea, PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    panelLayout->addWidget(scrollArea);
    QWidget* content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("speechContent"));
    scrollArea->setWidget(content);

    // sidebar-content 195:2899: paddings 16 / 2 / 16 / 16, gap 14
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 2, 16, 16);
    layout->setSpacing(14);

    m_errorLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    m_errorLabel->setObjectName(QStringLiteral("speechError"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_errorLabel);

    // field 195:2900: caption, gap 6, combo box 34
    QVBoxLayout* localeField = new QVBoxLayout();
    localeField->setSpacing(6);
    m_localeLabel = Helper::createCaption(this);
    m_localeComboBox = new PDFComboBox(this);
    m_localeComboBox->setObjectName(QStringLiteral("speechLocale"));
    m_localeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_localeComboBox->setMinimumContentsLength(8);
    m_localeLabel->setBuddy(m_localeComboBox);
    localeField->addWidget(m_localeLabel);
    localeField->addWidget(m_localeComboBox);
    layout->addLayout(localeField);

    QVBoxLayout* voiceField = new QVBoxLayout();
    voiceField->setSpacing(6);
    m_voiceLabel = Helper::createCaption(this);
    m_voiceComboBox = new PDFComboBox(this);
    m_voiceComboBox->setObjectName(QStringLiteral("speechVoice"));
    m_voiceComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_voiceComboBox->setMinimumContentsLength(8);
    m_voiceLabel->setBuddy(m_voiceComboBox);
    voiceField->addWidget(m_voiceLabel);
    voiceField->addWidget(m_voiceComboBox);
    layout->addLayout(voiceField);

    connect(m_localeComboBox, &QComboBox::activated, this, &PDFAppDocumentSpeechPanel::onLocaleActivated);
    connect(m_voiceComboBox, &QComboBox::activated, this, &PDFAppDocumentSpeechPanel::onVoiceActivated);

    // sliders 195:2912: rows with the gap 12
    QVBoxLayout* sliders = new QVBoxLayout();
    sliders->setSpacing(12);
    const int steps = Helper::SLIDER_STEPS;
    struct SliderRow
    {
        QPointer<PDFLabel>* label;
        QPointer<QSlider>* slider;
        QPointer<PDFLabel>* value;
        const char* name;
        int minimum;
    };
    for (const SliderRow& row : { SliderRow{ &m_rateLabel, &m_rateSlider, &m_rateValue, "speechRate", -steps },
                                  SliderRow{ &m_pitchLabel, &m_pitchSlider, &m_pitchValue, "speechPitch", -steps },
                                  SliderRow{ &m_volumeLabel, &m_volumeSlider, &m_volumeValue, "speechVolume", 0 } })
    {
        QHBoxLayout* rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(10);
        *row.label = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
        (*row.label)->setFixedWidth(48);
        *row.slider = Helper::createSlider(QString::fromLatin1(row.name), row.minimum, steps, this);
        (*row.label)->setBuddy(*row.slider);
        *row.value = new PDFLabel(QString(), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextPrimary, this);
        (*row.value)->setFixedWidth(30);
        (*row.value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(*row.label);
        rowLayout->addWidget(*row.slider, 1);
        rowLayout->addWidget(*row.value);
        sliders->addLayout(rowLayout);
        connect(*row.slider, &QSlider::sliderReleased, this, &PDFAppDocumentSpeechPanel::onSliderReleased);
        connect(*row.slider, &QSlider::valueChanged, this, &PDFAppDocumentSpeechPanel::onSliderMoved);
    }
    layout->addLayout(sliders);

    // transport 195:2934: Play (primary), Pause, Stop, spacer, Follow the reading; gap 4
    QHBoxLayout* transport = new QHBoxLayout();
    transport->setSpacing(4);
    m_playButton = new QPushButton(PDFThemeIcons::getIcon(QStringLiteral("play")), QString(), this);
    m_playButton->setObjectName(QStringLiteral("speechPlay"));
    PDFWidgetStyleProperties::set(m_playButton, PDFWidgetStyleProperties::PRIMARY);
    m_pauseButton = Helper::createToolButton(QStringLiteral("speechPause"), QStringLiteral("pause"), this);
    m_stopButton = Helper::createToolButton(QStringLiteral("speechStop"), QStringLiteral("stop"), this);
    // speech-sync of 195:2499: icon/locate, as thumbnail-sync of the thumbnails
    m_followButton = Helper::createToolButton(QStringLiteral("speechFollow"), QStringLiteral("locate"), this);
    m_followButton->setCheckable(true);
    transport->addWidget(m_playButton);
    transport->addWidget(m_pauseButton);
    transport->addWidget(m_stopButton);
    transport->addStretch(1);
    transport->addWidget(m_followButton);
    layout->addLayout(transport);
    connect(m_playButton, &QPushButton::clicked, this, &PDFAppDocumentSpeechPanel::onPlayClicked);
    connect(m_pauseButton, &QToolButton::clicked, this, &PDFAppDocumentSpeechPanel::onPauseClicked);
    connect(m_stopButton, &QToolButton::clicked, this, &PDFAppDocumentSpeechPanel::onStopClicked);
    connect(m_followButton, &QToolButton::toggled, this, &PDFAppDocumentSpeechPanel::onFollowToggled);

    // now-reading 195:2956: card bg/surface, padding 12, gap 6
    m_nowReadingCard = new PDFTintedCard(std::nullopt, this);
    m_nowReadingCard->setObjectName(QStringLiteral("speechNowReading"));
    QVBoxLayout* cardLayout = new QVBoxLayout(m_nowReadingCard);
    cardLayout->setContentsMargins(12, 12, 12, 12);
    cardLayout->setSpacing(6);
    m_nowReadingLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, m_nowReadingCard);
    m_textLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextMuted, m_nowReadingCard);
    m_textLabel->setObjectName(QStringLiteral("speechText"));
    m_textLabel->setWordWrap(true);
    m_textLabel->setTextFormat(Qt::RichText);
    m_textLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    cardLayout->addWidget(m_nowReadingLabel);
    cardLayout->addWidget(m_textLabel);
    layout->addWidget(m_nowReadingCard);
    layout->addStretch(1);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppDocumentSpeechPanel::onThemeChanged);

    retranslate();
    updateControls();
}

PDFAppDocumentSpeechPanel::~PDFAppDocumentSpeechPanel() = default;

QString PDFAppDocumentSpeechPanel::getId() const
{
    return PDFAppDocumentPanelIds::speech();
}

QString PDFAppDocumentSpeechPanel::getTitle() const
{
    return tr("Speech");
}

QString PDFAppDocumentSpeechPanel::getSubtitle() const
{
    return tr("Read the document aloud");
}

QString PDFAppDocumentSpeechPanel::getIconName() const
{
    return QStringLiteral("volume-2");
}

void PDFAppDocumentSpeechPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    BaseClass::setDocument(document, view);

    if (m_controller)
    {
        m_controller->disconnect(this);
    }

    // The speech of the tab is created, when the panel is shown first
    PDFAppDocumentSearchCommandHandler* handler = PDFAppDocumentSearchCommandHandler::findHandler(view ? static_cast<QObject*>(view) : static_cast<QObject*>(document));
    m_controller = (handler && document) ? handler->getSpeechController(true) : nullptr;
    if (m_controller)
    {
        connect(m_controller, &PDFAppDocumentSpeechController::stateChanged, this, &PDFAppDocumentSpeechPanel::updateControls);
        connect(m_controller, &PDFAppDocumentSpeechController::textChanged, this, &PDFAppDocumentSpeechPanel::updateText);
        connect(m_controller, &PDFAppDocumentSpeechController::voicesChanged, this, &PDFAppDocumentSpeechPanel::updateVoices);
    }

    PDFAppContext* context = document ? document->getContext() : nullptr;
    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppDocumentSpeechPanel::onSettingChanged, Qt::UniqueConnection);
    }

    updateVoices();
    updateControls();
}

PDFComboBox* PDFAppDocumentSpeechPanel::getLocaleComboBox() const { return m_localeComboBox.data(); }
PDFComboBox* PDFAppDocumentSpeechPanel::getVoiceComboBox() const { return m_voiceComboBox.data(); }
QSlider* PDFAppDocumentSpeechPanel::getRateSlider() const { return m_rateSlider.data(); }
QSlider* PDFAppDocumentSpeechPanel::getPitchSlider() const { return m_pitchSlider.data(); }
QSlider* PDFAppDocumentSpeechPanel::getVolumeSlider() const { return m_volumeSlider.data(); }
QPushButton* PDFAppDocumentSpeechPanel::getPlayButton() const { return m_playButton.data(); }
QToolButton* PDFAppDocumentSpeechPanel::getPauseButton() const { return m_pauseButton.data(); }
QToolButton* PDFAppDocumentSpeechPanel::getStopButton() const { return m_stopButton.data(); }
QToolButton* PDFAppDocumentSpeechPanel::getFollowButton() const { return m_followButton.data(); }
PDFLabel* PDFAppDocumentSpeechPanel::getNowReadingLabel() const { return m_nowReadingLabel.data(); }
PDFLabel* PDFAppDocumentSpeechPanel::getTextLabel() const { return m_textLabel.data(); }
PDFLabel* PDFAppDocumentSpeechPanel::getErrorLabel() const { return m_errorLabel.data(); }

void PDFAppDocumentSpeechPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
        updateControls();
    }
}

void PDFAppDocumentSpeechPanel::retranslate()
{
    m_localeLabel->setText(tr("Language"));
    m_voiceLabel->setText(tr("Voice"));
    m_rateLabel->setText(tr("Rate"));
    m_pitchLabel->setText(tr("Pitch"));
    m_volumeLabel->setText(tr("Volume"));
    m_rateSlider->setAccessibleName(tr("Rate"));
    m_pitchSlider->setAccessibleName(tr("Pitch"));
    m_volumeSlider->setAccessibleName(tr("Volume"));
    m_pauseButton->setToolTip(tr("Pause"));
    m_pauseButton->setAccessibleName(tr("Pause"));
    m_stopButton->setToolTip(tr("Stop"));
    m_stopButton->setAccessibleName(tr("Stop"));
    m_followButton->setToolTip(tr("Follow the reading: show the page being read"));
    m_followButton->setAccessibleName(tr("Follow the reading"));
}

void PDFAppDocumentSpeechPanel::updateControls()
{
    const bool isAvailable = m_controller && m_controller->isAvailable();
    const PDFAppDocumentSpeechController::State state = m_controller ? m_controller->getState() : PDFAppDocumentSpeechController::State::Stopped;

    QString error;
    if (!m_controller)
    {
        error = tr("Open a document to read it aloud.");
    }
    else if (!isAvailable)
    {
        error = tr("Speech is not available: %1").arg(m_controller->getErrorString());
    }
    m_errorLabel->setText(error);
    m_errorLabel->setVisible(!error.isEmpty());

    m_localeComboBox->setEnabled(isAvailable);
    m_voiceComboBox->setEnabled(isAvailable);
    for (QSlider* slider : { m_rateSlider.data(), m_pitchSlider.data(), m_volumeSlider.data() })
    {
        slider->setEnabled(isAvailable);
    }

    m_playButton->setText(state == PDFAppDocumentSpeechController::State::Paused ? tr("Resume") : tr("Play"));
    m_playButton->setEnabled(isAvailable && (state == PDFAppDocumentSpeechController::State::Stopped || state == PDFAppDocumentSpeechController::State::Paused));
    m_pauseButton->setEnabled(isAvailable && state == PDFAppDocumentSpeechController::State::Reading);
    m_stopButton->setEnabled(isAvailable && state != PDFAppDocumentSpeechController::State::Stopped);

    m_isUpdating = true;
    m_followButton->setChecked(m_controller ? m_controller->isFollowingReading() : true);
    m_isUpdating = false;

    updateValues();
    updateText();
}

void PDFAppDocumentSpeechPanel::updateVoices()
{
    PDFAppSpeechEngine* engine = m_controller ? m_controller->getEngine() : nullptr;

    m_isUpdating = true;
    m_localeComboBox->clear();
    m_voiceComboBox->clear();
    if (engine)
    {
        const QLocale current = engine->getLocale();
        for (const QLocale& locale : engine->getLocales())
        {
            m_localeComboBox->addItem(PDFAppDocumentSpeechController::getLocaleText(locale), locale.name());
            m_localeComboBox->setItemData(m_localeComboBox->count() - 1, QStringLiteral("%1 (%2)").arg(QLocale::languageToString(locale.language()), locale.name()), Qt::ToolTipRole);
        }
        m_localeComboBox->setCurrentIndex(m_localeComboBox->findData(current.name()));

        const QString voice = engine->getVoice();
        for (const PDFAppSpeechVoice& item : engine->getVoices())
        {
            m_voiceComboBox->addItem(item.displayText, item.name);
        }
        m_voiceComboBox->setCurrentIndex(m_voiceComboBox->findData(voice));
    }
    m_isUpdating = false;
}

void PDFAppDocumentSpeechPanel::updateValues()
{
    PDFAppDocument* document = getDocument();
    PDFAppContext* context = document ? document->getContext() : nullptr;
    double rate = 0.0;
    double pitch = 0.0;
    double volume = 1.0;
    if (context && context->getDatabase())
    {
        const PDFSettingsRepository settings = context->getSettings();
        rate = settings.getReal(PDFSettingKeys::speechRate());
        pitch = settings.getReal(PDFSettingKeys::speechPitch());
        volume = settings.getReal(PDFSettingKeys::speechVolume());
    }

    const int steps = PDFAppDocumentSpeechHelper::SLIDER_STEPS;
    m_isUpdating = true;
    if (!m_rateSlider->isSliderDown())
    {
        m_rateSlider->setValue(int(std::lround(rate * steps)));
    }
    if (!m_pitchSlider->isSliderDown())
    {
        m_pitchSlider->setValue(int(std::lround(pitch * steps)));
    }
    if (!m_volumeSlider->isSliderDown())
    {
        m_volumeSlider->setValue(int(std::lround(volume * steps)));
    }
    m_isUpdating = false;

    m_rateValue->setText(PDFAppDocumentSpeechHelper::formatValue(double(m_rateSlider->value()) / steps));
    m_pitchValue->setText(PDFAppDocumentSpeechHelper::formatValue(double(m_pitchSlider->value()) / steps));
    m_volumeValue->setText(PDFAppDocumentSpeechHelper::formatValue(double(m_volumeSlider->value()) / steps));
}

void PDFAppDocumentSpeechPanel::updateText()
{
    const bool isReading = m_controller && m_controller->isReading() && !m_controller->getCurrentText().isEmpty();
    m_nowReadingCard->setVisible(isReading);
    if (!isReading)
    {
        m_nowReadingLabel->clear();
        m_textLabel->clear();
        return;
    }

    // The page by its label as the rest of the application (1232:164124)
    m_nowReadingLabel->setText(tr("Now reading · page %1").arg(m_controller->getCurrentPageLabel()));

    // The spoken part text/primary, the rest text/muted (the text is data of the
    // document: escaped); the lines are 18 px apart (now-reading text of 195:2499)
    const QString& text = m_controller->getCurrentText();
    const qsizetype spoken = qBound(qsizetype(0), m_controller->getSpokenLength(), text.size());
    const PDFThemeManager* manager = PDFThemeManager::getInstance();
    const QString primary = manager->getColor(PDFThemeColors::TextPrimary).name();
    const QString muted = manager->getColor(PDFThemeColors::TextMuted).name();
    const QString html = QStringLiteral("<p style=\"margin:0; line-height:18px\"><span style=\"color:%1\">%2</span><span style=\"color:%3\">%4</span></p>")
                             .arg(primary, text.left(spoken).toHtmlEscaped(), muted, text.mid(spoken).toHtmlEscaped());
    m_textLabel->setText(html);
    m_textLabel->setAccessibleName(text);

    // The text is data of the document: it keeps its own direction (a Latin text
    // in a right-to-left interface stays left-to-right)
    const Qt::LayoutDirection direction = text.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight;
    m_textLabel->setLayoutDirection(direction);
    m_textLabel->setAlignment((direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop | Qt::AlignAbsolute);
}

void PDFAppDocumentSpeechPanel::onLocaleActivated(int index)
{
    if (!m_isUpdating && m_controller && index >= 0)
    {
        m_controller->setLocale(QLocale(m_localeComboBox->itemData(index).toString()));
        updateVoices();
    }
}

void PDFAppDocumentSpeechPanel::onVoiceActivated(int index)
{
    if (!m_isUpdating && m_controller && index >= 0)
    {
        m_controller->setVoice(m_voiceComboBox->itemData(index).toString());
    }
}

void PDFAppDocumentSpeechPanel::onSliderMoved(int value)
{
    Q_UNUSED(value);
    if (m_isUpdating || !m_controller)
    {
        return;
    }

    // The value is shown at once; it is stored at the release of the slider
    // (a click or a key stores it at once)
    QSlider* slider = qobject_cast<QSlider*>(sender());
    if (!slider)
    {
        return;
    }

    const double steps = PDFAppDocumentSpeechHelper::SLIDER_STEPS;
    const double sliderValue = slider->value() / steps;
    if (slider == m_rateSlider)
    {
        m_rateValue->setText(PDFAppDocumentSpeechHelper::formatValue(sliderValue));
    }
    else if (slider == m_pitchSlider)
    {
        m_pitchValue->setText(PDFAppDocumentSpeechHelper::formatValue(sliderValue));
    }
    else
    {
        m_volumeValue->setText(PDFAppDocumentSpeechHelper::formatValue(sliderValue));
    }

    if (!slider->isSliderDown())
    {
        onSliderReleased();
    }
}

void PDFAppDocumentSpeechPanel::onSliderReleased()
{
    if (m_isUpdating || !m_controller)
    {
        return;
    }

    QPointer<PDFAppDocumentSpeechController> controller = m_controller;
    const double steps = PDFAppDocumentSpeechHelper::SLIDER_STEPS;
    const double rate = m_rateSlider->value() / steps;
    const double pitch = m_pitchSlider->value() / steps;
    const double volume = m_volumeSlider->value() / steps;
    controller->setRate(rate);
    if (controller)
    {
        controller->setPitch(pitch);
    }
    if (controller)
    {
        controller->setVolume(volume);
    }
}

void PDFAppDocumentSpeechPanel::onPlayClicked()
{
    if (m_controller)
    {
        m_controller->play();
    }
}

void PDFAppDocumentSpeechPanel::onPauseClicked()
{
    if (m_controller)
    {
        m_controller->pause();
    }
}

void PDFAppDocumentSpeechPanel::onStopClicked()
{
    if (m_controller)
    {
        m_controller->stop();
    }
}

void PDFAppDocumentSpeechPanel::onFollowToggled(bool checked)
{
    if (!m_isUpdating && m_controller)
    {
        m_controller->setFollowingReading(checked);
    }
}

void PDFAppDocumentSpeechPanel::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key.startsWith(QStringLiteral("speech.")))
    {
        updateValues();
    }
}

void PDFAppDocumentSpeechPanel::onThemeChanged()
{
    updateText();
}

}   // namespace pdf
