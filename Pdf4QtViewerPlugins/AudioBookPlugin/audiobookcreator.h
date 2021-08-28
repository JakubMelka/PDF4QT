//    Copyright (C) 2021 Jakub Melka
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
