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

#ifndef PDFTEXTTOSPEECH_H
#define PDFTEXTTOSPEECH_H

#include "pdftextlayout.h"

#include <QObject>

class QLabel;
class QSlider;
class QComboBox;
class QToolButton;
class QTextBrowser;
class QTextToSpeech;

namespace pdf
{
class PDFDocument;
class PDFDrawWidgetProxy;
class PDFModifiedDocument;
}

namespace pdfviewer
{
class PDFViewerSettings;

/// Text to speech engine used to reading the document
class PDFTextToSpeech : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFTextToSpeech(QObject* parent);

    enum State
    {
        Invalid,    ///< Text to speech engine is invalid (maybe bad engine)
        NoDocument, ///< No document to read
        Ready,      ///< Ready to read the document contents
        Playing,    ///< Document is being read
        Paused,     ///< User paused the reading
        Error       ///< Error occured in text to speech engine
    };

    /// Returns true, if text to speech engine is valid and can be used
    /// to synthetise text.
    bool isValid() const;

    /// Sets active document to text to speech engine
    void setDocument(const pdf::PDFModifiedDocument& document);

    /// Apply settings to the reader
    void setSettings(const PDFViewerSettings* viewerSettings);

    /// Set draw proxy
    void setProxy(pdf::PDFDrawWidgetProxy* proxy);

    /// Initialize the ui, which is used
    void initializeUI(QComboBox* speechLocaleComboBox,
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
                      QTextBrowser* speechActualTextBrowser);

private:
    /// Updates UI controls depending on the state
    void updateUI();

    /// Stop the engine, if it is reading
    void stop();

    void setLocale(const QString& locale);
    void setVoice(const QString& voice);
    void setRate(const double rate);
    void setPitch(const double pitch);
    void setVolume(const double volume);

    void onLocaleChanged();
    void onVoiceChanged();
    void onRateChanged(int rate);
    void onPitchChanged(int pitch);
    void onVolumeChanged(int volume);

    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();

    void updatePlay();
    void updateVoices();
    void updateToNextPage(pdf::PDFInteger pageIndex);

    QTextToSpeech* m_textToSpeech;
    const pdf::PDFDocument* m_document;
    pdf::PDFDrawWidgetProxy* m_proxy;
    State m_state;
    bool m_initialized;

    QComboBox* m_speechLocaleComboBox;
    QComboBox* m_speechVoiceComboBox;
    QSlider* m_speechRateEdit;
    QSlider* m_speechVolumeEdit;
    QSlider* m_speechPitchEdit;
    QToolButton* m_speechPlayButton;
    QToolButton* m_speechPauseButton;
    QToolButton* m_speechStopButton;
    QToolButton* m_speechSynchronizeButton;
    QLabel* m_speechRateValueLabel;
    QLabel* m_speechPitchValueLabel;
    QLabel* m_speechVolumeValueLabel;
    QTextBrowser* m_speechActualTextBrowser;

    pdf::PDFTextLayout m_currentTextLayout; ///< Text layout for actual page
    pdf::PDFTextFlows m_textFlows; ///< Text flows for actual page
    size_t m_currentTextFlowIndex = 0; ///< Index of current text flow
    pdf::PDFInteger m_currentPage = 0; ///< Current page
};

}   // namespace pdfviewer

#endif // PDFTEXTTOSPEECH_H
