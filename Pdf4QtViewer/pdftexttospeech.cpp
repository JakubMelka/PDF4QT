//    Copyright (C) 2020-2022 Jakub Melka
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

#include "pdftexttospeech.h"
#include "pdfviewersettings.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfdrawwidget.h"
#include "pdfdbgheap.h"

#include <QLabel>
#include <QAction>
#include <QSlider>
#include <QComboBox>
#include <QToolButton>
#include <QTextBrowser>
#include <QTextToSpeech>

namespace pdfviewer
{

PDFTextToSpeech::PDFTextToSpeech(QObject* parent) :
    BaseClass(parent),
    m_textToSpeech(nullptr),
    m_document(nullptr),
    m_proxy(nullptr),
    m_state(Invalid),
    m_initialized(false),
    m_speechLocaleComboBox(nullptr),
    m_speechVoiceComboBox(nullptr),
    m_speechRateEdit(nullptr),
    m_speechVolumeEdit(nullptr),
    m_speechPitchEdit(nullptr),
    m_speechPlayButton(nullptr),
    m_speechPauseButton(nullptr),
    m_speechStopButton(nullptr),
    m_speechSynchronizeButton(nullptr),
    m_speechRateValueLabel(nullptr),
    m_speechPitchValueLabel(nullptr),
    m_speechVolumeValueLabel(nullptr),
    m_speechActualTextBrowser(nullptr)
{

}

bool PDFTextToSpeech::isValid() const
{
    return m_document != nullptr;
}

void PDFTextToSpeech::setDocument(const pdf::PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        stop();
        m_document = document;

        if (m_textToSpeech)
        {
            m_state = m_document ? Ready : NoDocument;
        }
        else
        {
            // Set state to invalid, speech engine is not set
            m_state = Invalid;
        }

        updateUI();
    }
}

void PDFTextToSpeech::updateVoices()
{
    QVector<QVoice> voices = m_textToSpeech->availableVoices();
    m_speechVoiceComboBox->setUpdatesEnabled(false);
    m_speechVoiceComboBox->clear();
    for (const QVoice& voice : voices)
    {
        m_speechVoiceComboBox->addItem(QString("%1 (%2, %3)").arg(voice.name(), QVoice::genderName(voice.gender()), QVoice::ageName(voice.age())), voice.name());
    }
    m_speechVoiceComboBox->setUpdatesEnabled(true);
}

void PDFTextToSpeech::setSettings(const PDFViewerSettings* viewerSettings)
{
    Q_ASSERT(viewerSettings);

    if (!m_initialized)
    {
        // This object is not initialized yet
        return;
    }

    // First, stop the engine
    stop();

    delete m_textToSpeech;
    m_textToSpeech = nullptr;

    const PDFViewerSettings::Settings& settings = viewerSettings->getSettings();
    if (!settings.m_speechEngine.isEmpty())
    {
        m_textToSpeech = new QTextToSpeech(settings.m_speechEngine, this);
        m_textToSpeech->setLocale(QLocale(settings.m_speechLocale));
        connect(m_textToSpeech, &QTextToSpeech::stateChanged, this, &PDFTextToSpeech::updatePlay);
        m_state = m_document ? Ready : NoDocument;

        QVector<QLocale> locales = m_textToSpeech->availableLocales();
        m_speechLocaleComboBox->setUpdatesEnabled(false);
        m_speechLocaleComboBox->clear();
        for (const QLocale& locale : locales)
        {
            m_speechLocaleComboBox->addItem(QString("%1 (%2)").arg(locale.nativeLanguageName(), locale.nativeTerritoryName()), locale.name());
        }
        m_speechLocaleComboBox->setUpdatesEnabled(true);

        updateVoices();
    }
    else
    {
        // Set state to invalid, speech engine is not set
        m_state = Invalid;

        m_speechLocaleComboBox->clear();
        m_speechVoiceComboBox->clear();
    }

    if (m_textToSpeech)
    {
        setLocale(settings.m_speechLocale);
        setVoice(settings.m_speechVoice);
        setRate(settings.m_speechRate);
        setPitch(settings.m_speechPitch);
        setVolume(settings.m_speechVolume);
    }

    updateUI();
}

void PDFTextToSpeech::setProxy(pdf::PDFDrawWidgetProxy* proxy)
{
    m_proxy = proxy;
    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    connect(compiler, &pdf::PDFAsynchronousTextLayoutCompiler::textLayoutChanged, this, &PDFTextToSpeech::updatePlay);
}

void PDFTextToSpeech::initializeUI(QComboBox* speechLocaleComboBox,
                                   QComboBox* speechVoiceComboBox,
                                   QSlider* speechRateEdit,
                                   QSlider* speechPitchEdit,
                                   QSlider* speechVolumeEdit,
                                   QToolButton* speechPlayButton,
                                   QToolButton* speechPauseButton,
                                   QToolButton* speechStopButton,
                                   QToolButton* speechSynchronizeButton,
                                   QLabel* speechRateValueLabel,
                                   QLabel* speechPitchValueLabel,
                                   QLabel* speechVolumeValueLabel,
                                   QTextBrowser* speechActualTextBrowser)
{
    Q_ASSERT(speechLocaleComboBox);
    Q_ASSERT(speechVoiceComboBox);
    Q_ASSERT(speechRateEdit);
    Q_ASSERT(speechVolumeEdit);
    Q_ASSERT(speechPitchEdit);
    Q_ASSERT(speechPlayButton);
    Q_ASSERT(speechPauseButton);
    Q_ASSERT(speechStopButton);
    Q_ASSERT(speechSynchronizeButton);

    m_speechLocaleComboBox = speechLocaleComboBox;
    m_speechVoiceComboBox = speechVoiceComboBox;
    m_speechRateEdit = speechRateEdit;
    m_speechVolumeEdit = speechVolumeEdit;
    m_speechPitchEdit = speechPitchEdit;
    m_speechPlayButton = speechPlayButton;
    m_speechPauseButton = speechPauseButton;
    m_speechStopButton = speechStopButton;
    m_speechSynchronizeButton = speechSynchronizeButton;
    m_speechRateValueLabel = speechRateValueLabel;
    m_speechPitchValueLabel = speechPitchValueLabel;
    m_speechVolumeValueLabel = speechVolumeValueLabel;
    m_speechActualTextBrowser = speechActualTextBrowser;

    connect(m_speechLocaleComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFTextToSpeech::onLocaleChanged);
    connect(m_speechVoiceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PDFTextToSpeech::onVoiceChanged);
    connect(m_speechRateEdit, &QSlider::valueChanged, this, &PDFTextToSpeech::onRateChanged);
    connect(m_speechPitchEdit, &QSlider::valueChanged, this, &PDFTextToSpeech::onPitchChanged);
    connect(m_speechVolumeEdit, &QSlider::valueChanged, this, &PDFTextToSpeech::onVolumeChanged);
    connect(m_speechPlayButton, &QToolButton::clicked, this, &PDFTextToSpeech::onPlayClicked);
    connect(m_speechPauseButton, &QToolButton::clicked, this, &PDFTextToSpeech::onPauseClicked);
    connect(m_speechStopButton, &QToolButton::clicked, this, &PDFTextToSpeech::onStopClicked);

    m_initialized = true;
}

void PDFTextToSpeech::updateUI()
{
    bool enableControls = false;
    bool enablePlay = false;
    bool enablePause = false;
    bool enableStop = false;

    switch (m_state)
    {
        case pdfviewer::PDFTextToSpeech::Invalid:
        {
            enableControls = false;
            enablePlay = false;
            enablePause = false;
            enableStop = false;
            break;
        }

        case pdfviewer::PDFTextToSpeech::NoDocument:
        {
            enableControls = true;
            enablePlay = false;
            enablePause = false;
            enableStop = false;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Ready:
        {
            enableControls = true;
            enablePlay = true;
            enablePause = false;
            enableStop = false;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Playing:
        {
            enableControls = true;
            enablePlay = false;
            enablePause = true;
            enableStop = true;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Paused:
        {
            enableControls = true;
            enablePlay = true;
            enablePause = false;
            enableStop = true;
            break;
        }

        case pdfviewer::PDFTextToSpeech::Error:
        {
            enableControls = false;
            enablePlay = false;
            enablePause = false;
            enableStop = false;
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    m_speechLocaleComboBox->setEnabled(enableControls && m_speechLocaleComboBox->count() > 0);
    m_speechVoiceComboBox->setEnabled(enableControls && m_speechVoiceComboBox->count() > 0);
    m_speechRateEdit->setEnabled(enableControls);
    m_speechVolumeEdit->setEnabled(enableControls);
    m_speechPitchEdit->setEnabled(enableControls);
    m_speechPlayButton->setEnabled(enablePlay);
    m_speechPauseButton->setEnabled(enablePause);
    m_speechStopButton->setEnabled(enableStop);
    m_speechSynchronizeButton->setEnabled(enableControls);
}

void PDFTextToSpeech::stop()
{
    switch (m_state)
    {
        case Playing:
        case Paused:
        {
            m_textToSpeech->stop();
            m_currentTextFlowIndex = 0;
            m_currentPage = 0;
            m_currentTextLayout = pdf::PDFTextLayout();
            m_textFlows = pdf::PDFTextFlows();
            m_state = Ready;
            break;
        }

        default:
            break;
    }

    updateUI();
}

void PDFTextToSpeech::setLocale(const QString& locale)
{
    m_speechLocaleComboBox->setCurrentIndex(m_speechLocaleComboBox->findData(locale));
}

void PDFTextToSpeech::setVoice(const QString& voice)
{
    m_speechVoiceComboBox->setCurrentIndex(m_speechVoiceComboBox->findData(voice));
}

void PDFTextToSpeech::setRate(const double rate)
{
    pdf::PDFLinearInterpolation<double> interpolation(-1.0, 1.0, m_speechRateEdit->minimum(), m_speechRateEdit->maximum());
    m_speechRateEdit->setValue(qRound(interpolation(rate)));
    onRateChanged(m_speechRateEdit->value());
}

void PDFTextToSpeech::setPitch(const double pitch)
{
    pdf::PDFLinearInterpolation<double> interpolation(-1.0, 1.0, m_speechPitchEdit->minimum(), m_speechPitchEdit->maximum());
    m_speechPitchEdit->setValue(qRound(interpolation(pitch)));
    onPitchChanged(m_speechPitchEdit->value());
}

void PDFTextToSpeech::setVolume(const double volume)
{
    pdf::PDFLinearInterpolation<double> interpolation(0.0, 1.0, m_speechVolumeEdit->minimum(), m_speechVolumeEdit->maximum());
    m_speechVolumeEdit->setValue(qRound(interpolation(volume)));
    onVolumeChanged(m_speechVolumeEdit->value());
}

void PDFTextToSpeech::onLocaleChanged()
{
    if (m_textToSpeech)
    {
        m_textToSpeech->setLocale(QLocale(m_speechLocaleComboBox->currentData().toString()));
        updateVoices();

        if (m_speechVoiceComboBox->currentIndex() == -1)
        {
            m_speechVoiceComboBox->setCurrentIndex(0);
        }
    }
}

void PDFTextToSpeech::onVoiceChanged()
{
    if (m_textToSpeech)
    {
        QString voice = m_speechVoiceComboBox->currentData().toString();
        for (const QVoice& voiceObject : m_textToSpeech->availableVoices())
        {
            if (voiceObject.name() == voice)
            {
                m_textToSpeech->setVoice(voiceObject);
            }
        }
    }
}

void PDFTextToSpeech::onRateChanged(int rate)
{
    if (m_textToSpeech)
    {
        pdf::PDFLinearInterpolation<double> interpolation(m_speechRateEdit->minimum(), m_speechRateEdit->maximum(), -1.0, 1.0);
        double value = interpolation(rate);
        m_textToSpeech->setRate(value);
        m_speechRateValueLabel->setText(QString::number(value, 'f', 2));
    }
}

void PDFTextToSpeech::onPitchChanged(int pitch)
{
    if (m_textToSpeech)
    {
        pdf::PDFLinearInterpolation<double> interpolation(m_speechPitchEdit->minimum(), m_speechPitchEdit->maximum(), -1.0, 1.0);
        double value = interpolation(pitch);
        m_textToSpeech->setPitch(pitch);
        m_speechPitchValueLabel->setText(QString::number(value, 'f', 2));
    }
}

void PDFTextToSpeech::onVolumeChanged(int volume)
{
    if (m_textToSpeech)
    {
        pdf::PDFLinearInterpolation<double> interpolation(m_speechVolumeEdit->minimum(), m_speechVolumeEdit->maximum(), 0.0, 1.0);
        double value = interpolation(volume);
        m_textToSpeech->setVolume(value);
        m_speechVolumeValueLabel->setText(QString::number(value, 'f', 2));
    }
}

void PDFTextToSpeech::onPlayClicked()
{
    switch (m_state)
    {
        case Paused:
        {
            m_textToSpeech->resume();
            m_state = Playing;
            if (m_textToSpeech->state() == QTextToSpeech::Ready)
            {
                updatePlay();
            }
            break;
        }

        case Ready:
        {
            m_state = Playing;
            m_currentTextFlowIndex = std::numeric_limits<size_t>::max();
            m_currentPage = -1;
            updatePlay();
            break;
        }

        default:
            break;
    }

    updateUI();
}

void PDFTextToSpeech::onPauseClicked()
{
    Q_ASSERT(m_state == Playing);

    if (m_state == Playing)
    {
        m_textToSpeech->pause();
        m_state = Paused;
        updateUI();
    }
}

void PDFTextToSpeech::onStopClicked()
{
    stop();
}

void PDFTextToSpeech::updatePlay()
{
    if (m_state != Playing)
    {
        return;
    }

    Q_ASSERT(m_proxy);
    Q_ASSERT(m_document);

    // Jakub Melka: Check, if we have text layout. If not, then create it and return immediately.
    // Otherwise, check, if we have something to say.
    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    if (!compiler->isTextLayoutReady())
    {
        compiler->makeTextLayout();
        return;
    }

    QTextToSpeech::State state = m_textToSpeech->state();
    if (state == QTextToSpeech::Ready)
    {
        if (m_currentPage == -1)
        {
            // Handle starting of document reading
            std::vector<pdf::PDFInteger> currentPages = m_proxy->getWidget()->getDrawWidget()->getCurrentPages();
            if (!currentPages.empty())
            {
                updateToNextPage(currentPages.front());
            }
        }
        else if (++m_currentTextFlowIndex >= m_textFlows.size())
        {
            // Handle transition to next page
            updateToNextPage(m_currentPage + 1);
        }

        if (m_currentTextFlowIndex < m_textFlows.size())
        {
            // Say next thing
            const pdf::PDFTextFlow& textFlow = m_textFlows[m_currentTextFlowIndex];
            QString text = textFlow.getText();
            m_textToSpeech->say(text);
            m_speechActualTextBrowser->setText(text);
        }
        else
        {
            // We are finished the reading
            m_state = Ready;
        }
    }
    else if (state == QTextToSpeech::Error)
    {
        m_state = Error;
    }

    updateUI();
}

void PDFTextToSpeech::updateToNextPage(pdf::PDFInteger pageIndex)
{
    Q_ASSERT(m_document);
    Q_ASSERT(m_state = Playing);

    m_currentPage = pageIndex;
    const pdf::PDFInteger pageCount = m_document->getCatalog()->getPageCount();

    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    Q_ASSERT(compiler->isTextLayoutReady());

    m_currentTextLayout = pdf::PDFTextLayout();
    m_textFlows.clear();
    m_speechActualTextBrowser->clear();

    // Find first nonempty page
    while (m_currentPage < pageCount)
    {
        m_currentTextLayout = compiler->getTextLayout(m_currentPage);
        m_textFlows = pdf::PDFTextFlow::createTextFlows(m_currentTextLayout, pdf::PDFTextFlow::SeparateBlocks | pdf::PDFTextFlow::RemoveSoftHyphen, m_currentPage);

        if (!m_textFlows.empty())
        {
            break;
        }

        ++m_currentPage;
    }

    if (m_currentPage < pageCount && m_speechSynchronizeButton->isChecked())
    {
        m_proxy->goToPage(m_currentPage);
    }

    m_currentTextFlowIndex = 0;
}

}   // namespace pdfviewer
