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

#ifndef PDFAPPSETTINGSSECTION_SPEECH_H
#define PDFAPPSETTINGSSECTION_SPEECH_H

#include "pdfappglobal.h"
#include "pdfappsettingssection.h"
#include "pdfappdocumentspeech.h"

#include "pdffluentwidgets.h"

#include <QPointer>
#include <QStringList>
#include <QVariant>

#include <utility>
#include <vector>

class QPushButton;

namespace pdf
{

class PDFAppSettingRow;

/// Section Speech (147:3842): the voice synthesizer, its locale and voice (combo
/// boxes of the engine, 944:9031), the generic sliders Rate / Pitch / Volume and
/// the row Preview with Play sample. The keys are the same as of the Speech panel
/// of a document (195:2499), so a change applies at once to every open document.
///
/// The section owns one PDFAppSpeechEngine of the selected synthesizer (created,
/// when the section is shown first, again after a change of speech.engine): its
/// locales and voices fill the combo boxes once it is ready; an engine in the
/// error state disables them and says so in the description of the synthesizer.
/// A stored value the engine does not offer is shown as "… (not available)" (the
/// engine uses its default). The combo boxes write from the event loop (a slot
/// may delete the page); the sample is stopped, when the page is hidden.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSpeechSettingsSection : public PDFAppGenericSettingsSection
{
    Q_OBJECT

private:
    using BaseClass = PDFAppGenericSettingsSection;

public:
    /// List of the available engines (tests); nullptr = QTextToSpeech::availableEngines()
    using EngineListProvider = QStringList (*)();

    /// Longest text of a stored value shown as "(not available)"
    static constexpr int MAXIMUM_VALUE_TEXT = 64;

    explicit PDFAppSpeechSettingsSection(PDFAppContext* context, const QString& sectionId, QWidget* parent = nullptr);
    virtual ~PDFAppSpeechSettingsSection() override;

    /// Anchor of the row Preview
    static QString getPreviewAnchor() { return QStringLiteral("preview"); }

    static void setEngineListProvider(EngineListProvider provider);

    /// Ids of the engines of the system (bounded, no empty ids)
    static QStringList getAvailableEngines();

    /// Readable name of an engine id (944:9031: "Windows (WinRT)"; "" = Automatic;
    /// an unknown id is shown as it is)
    static QString getEngineTitle(const QString& engineId);

    /// Text spoken by Play sample
    static QString getSampleText();

    PDFComboBox* getEngineComboBox() const { return m_engineComboBox; }
    PDFComboBox* getLocaleComboBox() const { return m_localeComboBox; }
    PDFComboBox* getVoiceComboBox() const { return m_voiceComboBox; }
    QPushButton* getPlayButton() const { return m_playButton; }
    PDFAppSettingRow* getEngineRow() const { return m_engineRow; }
    PDFAppSettingRow* getPreviewRow() const { return m_previewRow; }

    /// Engine of the section (nullptr before the section is shown)
    PDFAppSpeechEngine* getEngine() const { return m_engine; }

    /// The sample is being said
    bool isPlaying() const;

    /// Says the sample with the stored voice, rate, pitch and volume / stops it
    void playSample();
    void stopSample();

    /// Stops the sample of a hidden section (from the event loop after the hide)
    void stopSampleWhenHidden();

    virtual std::vector<PDFAppSettingChange> getChangedValues() const override;
    virtual void refresh(const QString& topic, const QString& key) override;
    virtual void commitPendingValues() override;
    virtual void finishActivity() override;

protected:
    virtual PDFAppSettingRow* createRow(const PDFSettingDefinition& definition) override;
    virtual void finishContent() override;
    virtual void showEvent(QShowEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;

private:
    PDFAppSettingRow* createComboRow(const PDFSettingDefinition& definition, QPointer<PDFComboBox>& comboBox);
    void createEngine();
    void applySettingsToEngine();
    void updateEngineItems();
    void updateVoiceItems();
    void updatePlayButton();
    void queueWrite(std::vector<std::pair<QString, QVariant>> values);
    void writePending();
    QString getStoredString(const QString& key) const;
    void onEngineActivated(int index);
    void onLocaleActivated(int index);
    void onVoiceActivated(int index);
    void onPlayClicked();
    void onEngineStateChanged();
    void onEngineVoicesChanged();

    static EngineListProvider s_engineListProvider;

    QPointer<PDFAppSpeechEngine> m_engine;
    QString m_engineName;
    QPointer<PDFAppSettingRow> m_engineRow;
    QPointer<PDFAppSettingRow> m_previewRow;
    QPointer<PDFComboBox> m_engineComboBox;
    QPointer<PDFComboBox> m_localeComboBox;
    QPointer<PDFComboBox> m_voiceComboBox;
    QPointer<QPushButton> m_playButton;
    std::vector<std::pair<QString, QVariant>> m_pendingValues;
    bool m_isWriteQueued = false;
    bool m_isSampleRequested = false;
};

/// Factory of the section
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSpeechSettingsSectionFactory : public PDFAppSettingsSectionFactory
{
public:
    virtual PDFAppSettingsSection* createSection(PDFAppContext* context, const QString& sectionId, QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPSETTINGSSECTION_SPEECH_H
