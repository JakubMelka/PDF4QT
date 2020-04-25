//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.


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
