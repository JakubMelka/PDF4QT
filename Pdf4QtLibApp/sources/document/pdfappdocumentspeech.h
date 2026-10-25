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

#ifndef PDFAPPDOCUMENTSPEECH_H
#define PDFAPPDOCUMENTSPEECH_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include "pdfglobal.h"

#include <QList>
#include <QLocale>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QPushButton;
class QSlider;
class QTextToSpeech;
class QToolButton;

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentTextIndexer;
class PDFAppDocumentView;
class PDFComboBox;
class PDFLabel;
class PDFModifiedDocument;
class PDFTintedCard;

/// Voice of a speech engine
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSpeechVoice
{
public:
    QString name;           ///< Name of the voice (the value of speech.voice)
    QString displayText;    ///< "Microsoft Aria (Female, Adult)"
};

/// Speech engine of the speech of a document. The application uses QTextToSpeech
/// (PDFAppQtSpeechEngine); the tests set their own engine by setFactory.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSpeechEngine : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    enum class State
    {
        Initializing,   ///< The engine is starting (the voices are not known yet)
        Ready,          ///< Ready, nothing is said
        Speaking,       ///< A text is said
        Paused,         ///< The text is paused
        Error           ///< The engine cannot be used (getErrorString)
    };

    /// Creates an engine: \p engineName is the plugin of QTextToSpeech ("" = default)
    using Factory = PDFAppSpeechEngine* (*)(const QString& engineName, QObject* parent);

    explicit PDFAppSpeechEngine(QObject* parent);
    virtual ~PDFAppSpeechEngine() override;

    virtual State getState() const = 0;
    virtual QString getErrorString() const = 0;

    virtual QList<QLocale> getLocales() const = 0;
    virtual QLocale getLocale() const = 0;
    virtual void setLocale(const QLocale& locale) = 0;

    /// Voices of the current locale
    virtual QList<PDFAppSpeechVoice> getVoices() const = 0;
    virtual QString getVoice() const = 0;
    virtual void setVoice(const QString& name) = 0;

    /// Rate and pitch -1 to 1, volume 0 to 1
    virtual void setRate(double rate) = 0;
    virtual void setPitch(double pitch) = 0;
    virtual void setVolume(double volume) = 0;

    virtual void say(const QString& text) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;

    /// Creates the engine of the application (or of the factory of the tests)
    static PDFAppSpeechEngine* create(const QString& engineName, QObject* parent);

    /// Factory of the engines (tests); nullptr = QTextToSpeech
    static void setFactory(Factory factory);

Q_SIGNALS:
    /// The state has changed
    void stateChanged();

    /// The locales or the voices have changed (the engine has started, the locale has changed)
    void voicesChanged();

    /// The word of the said text at \p start of \p length characters is said
    void wordStarted(qsizetype start, qsizetype length);

private:
    static Factory s_factory;
};

/// Speech engine of QTextToSpeech
class PDF4QTLIBAPPSHARED_EXPORT PDFAppQtSpeechEngine : public PDFAppSpeechEngine
{
    Q_OBJECT

private:
    using BaseClass = PDFAppSpeechEngine;

public:
    explicit PDFAppQtSpeechEngine(const QString& engineName, QObject* parent);
    virtual ~PDFAppQtSpeechEngine() override;

    virtual State getState() const override;
    virtual QString getErrorString() const override;
    virtual QList<QLocale> getLocales() const override;
    virtual QLocale getLocale() const override;
    virtual void setLocale(const QLocale& locale) override;
    virtual QList<PDFAppSpeechVoice> getVoices() const override;
    virtual QString getVoice() const override;
    virtual void setVoice(const QString& name) override;
    virtual void setRate(double rate) override;
    virtual void setPitch(double pitch) override;
    virtual void setVolume(double volume) override;
    virtual void say(const QString& text) override;
    virtual void pause() override;
    virtual void resume() override;
    virtual void stop() override;

private:
    void onStateChanged();
    void onLocaleChanged();
    void onSayingWord(const QString& word, qsizetype id, qsizetype start, qsizetype length);

    QPointer<QTextToSpeech> m_textToSpeech;
    State m_lastState = State::Initializing;
};

/// Speech of the document of a tab (Read aloud, Figma Speech panel 195:2499): the
/// text of the pages from the text index (built lazily, as for the search) is
/// said block after block from the current page; with "Follow the reading" the
/// view shows the page being read. The voice, the rate, the pitch and the volume
/// are the settings speech.* (the panel and Settings › Speech write the same keys,
/// a change from anywhere applies at once). The engine is created with the
/// controller (at the first use of the speech in the tab).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSpeechController : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    enum class State
    {
        Stopped,    ///< Nothing is read
        Preparing,  ///< The text index is being built
        Reading,    ///< A block is said
        Paused      ///< The reading is paused
    };

    explicit PDFAppDocumentSpeechController(PDFAppDocument* document, PDFAppDocumentView* view, PDFAppDocumentTextIndexer* indexer, QObject* parent);
    virtual ~PDFAppDocumentSpeechController() override;

    State getState() const { return m_state; }

    /// The reading runs (also paused or preparing)
    bool isReading() const { return m_state != State::Stopped; }

    /// The engine can be used
    bool isAvailable() const;

    /// Error of the engine (empty = none)
    QString getErrorString() const;

    PDFAppSpeechEngine* getEngine() const { return m_engine; }

    /// Starts the reading at the current page, or resumes a paused one
    void play();
    void pause();
    void stop();

    /// The view shows the page being read
    bool isFollowingReading() const { return m_isFollowingReading; }
    void setFollowingReading(bool follow);

    /// Page (from 0) and the block being read, the length of its part said so far
    PDFInteger getCurrentPage() const { return m_currentPage; }
    const QString& getCurrentText() const { return m_currentText; }

    /// Label of the page being read (the label of the document, else its number;
    /// empty when nothing is read)
    QString getCurrentPageLabel() const;
    qsizetype getSpokenLength() const { return m_spokenLength; }

    /// Writes a setting speech.* and applies it to the engine
    void setLocale(const QLocale& locale);
    void setVoice(const QString& name);
    void setRate(double rate);
    void setPitch(double pitch);
    void setVolume(double volume);

    /// Applies the settings speech.* to the engine (a new engine, when speech.engine has changed)
    void applySettings();

    /// Text of a locale ("English (United States)", "Deutsch (Deutschland) – German (Germany)")
    static QString getLocaleText(const QLocale& locale);

Q_SIGNALS:
    /// The state or the availability of the engine has changed
    void stateChanged();

    /// The engine, its locales or its voices have changed
    void voicesChanged();

    /// The block being read or its spoken part has changed
    void textChanged();

private:
    void createEngine();
    void setState(State state);
    void startReading();
    void speakNext();
    bool loadPage(PDFInteger pageIndex);
    void onEngineStateChanged();
    void onEngineVoicesChanged();
    void onEngineDestroyed();
    void onWordStarted(qsizetype start, qsizetype length);
    void onIndexReady();
    void onIndexBuildingChanged();
    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    void onSettingChanged(const QString& topic, const QString& key);
    PDFAppContext* getContext() const;

    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocumentTextIndexer> m_indexer;
    QPointer<PDFAppSpeechEngine> m_engine;
    QString m_engineName;
    State m_state = State::Stopped;
    bool m_isFollowingReading = true;
    bool m_isSpeakingBlock = false;
    PDFInteger m_currentPage = -1;
    QStringList m_blocks;
    qsizetype m_blockIndex = -1;
    QString m_currentText;
    qsizetype m_spokenLength = 0;
};

/// Panel Speech of the sidebar (Figma 195:2499): Language, Voice, the sliders
/// Rate, Pitch and Volume with their values, Play / Pause / Stop, Follow the
/// reading and the card "Now reading · page 308" with the block being read (its
/// spoken part text/primary, the rest text/muted).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentSpeechPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    explicit PDFAppDocumentSpeechPanel(QWidget* parent = nullptr);
    virtual ~PDFAppDocumentSpeechPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Parts of the panel (tests)
    PDFComboBox* getLocaleComboBox() const;
    PDFComboBox* getVoiceComboBox() const;
    QSlider* getRateSlider() const;
    QSlider* getPitchSlider() const;
    QSlider* getVolumeSlider() const;
    QPushButton* getPlayButton() const;
    QToolButton* getPauseButton() const;
    QToolButton* getStopButton() const;
    QToolButton* getFollowButton() const;
    PDFLabel* getNowReadingLabel() const;
    PDFLabel* getTextLabel() const;
    PDFLabel* getErrorLabel() const;

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void updateControls();
    void updateVoices();
    void updateValues();
    void updateText();
    void onLocaleActivated(int index);
    void onVoiceActivated(int index);
    void onSliderReleased();
    void onSliderMoved(int value);
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onFollowToggled(bool checked);
    void onSettingChanged(const QString& topic, const QString& key);
    void onThemeChanged();

    QPointer<PDFAppDocumentSpeechController> m_controller;
    QPointer<PDFLabel> m_errorLabel;
    QPointer<PDFLabel> m_localeLabel;
    QPointer<PDFComboBox> m_localeComboBox;
    QPointer<PDFLabel> m_voiceLabel;
    QPointer<PDFComboBox> m_voiceComboBox;
    QPointer<PDFLabel> m_rateLabel;
    QPointer<PDFLabel> m_pitchLabel;
    QPointer<PDFLabel> m_volumeLabel;
    QPointer<QSlider> m_rateSlider;
    QPointer<QSlider> m_pitchSlider;
    QPointer<QSlider> m_volumeSlider;
    QPointer<PDFLabel> m_rateValue;
    QPointer<PDFLabel> m_pitchValue;
    QPointer<PDFLabel> m_volumeValue;
    QPointer<QPushButton> m_playButton;
    QPointer<QToolButton> m_pauseButton;
    QPointer<QToolButton> m_stopButton;
    QPointer<QToolButton> m_followButton;
    QPointer<PDFTintedCard> m_nowReadingCard;
    QPointer<PDFLabel> m_nowReadingLabel;
    QPointer<PDFLabel> m_textLabel;
    bool m_isUpdating = false;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTSPEECH_H
