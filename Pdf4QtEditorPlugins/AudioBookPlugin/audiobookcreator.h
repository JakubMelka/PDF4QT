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

#ifndef AUDIOBOOKCREATOR_H
#define AUDIOBOOKCREATOR_H

#include "pdfdocumenttextflow.h"

#include <QtGlobal>

namespace pdfplugin
{

class AudioBookCreator
{
    Q_DECLARE_TR_FUNCTIONS(pdfplugin::AudioBookCreator)

public:
    AudioBookCreator();
    ~AudioBookCreator();

    struct Settings
    {
        QString audioFileName;
        QString voiceName;
        QString voiceGender;
        QString voiceAge;
        QString voiceLangCode;
        double rate = 0.0;
        double volume = 1.0;
    };

    bool isInitialized() const { return m_initialized; }

    pdf::PDFOperationResult createAudioBook(const Settings& settings, pdf::PDFDocumentTextFlow& flow);

private:
    bool m_initialized;
};

}   // namespace pdfplugin

#endif // AUDIOBOOKCREATOR_H
