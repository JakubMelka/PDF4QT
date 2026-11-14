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

#ifndef PDFTOOLAUDIOBOOK_H
#define PDFTOOLAUDIOBOOK_H

#include "pdftoolabstractapplication.h"
#include "pdfspeechsynthesizer.h"

namespace pdftool
{

/// The commands audio-book and audio-book-voices on every platform (step 4 of phase 2, plan 4.8,
/// owner answers Q4.1 - Q4.3, R4-15, B68 - B72, B77): the voices and the synthesis of Pdf4QtSpeech
/// (Qt TextToSpeech), the audio book of the core tool PDFDocumentToolAudioBook - no code of SAPI,
/// no second copy of the synthesis.
class PDFToolAudioBookBase : public PDFToolAbstractApplication
{
public:
    PDFToolAudioBookBase() = default;

protected:
    /// The voices of the engine, which match the options --voice-name, --voice-gender,
    /// --voice-age, --voice-lang-code (PDFSpeechVoiceSelector, the one selection)
    /// \param voices The voices of the engine
    /// \param errorMessage An unknown value of an option
    static std::vector<pdf::PDFSpeechVoice> filterVoices(const PDFToolOptions& options, const std::vector<pdf::PDFSpeechVoice>& voices, QString* errorMessage);

    /// A criterion of a voice is given
    static bool hasVoiceCriteria(const PDFToolOptions& options);
};

class PDFToolAudioBookVoices : public PDFToolAudioBookBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;
};

class PDFToolAudioBook : public PDFToolAudioBookBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;
};

}   // namespace pdftool

#endif // PDFTOOLAUDIOBOOK_H
