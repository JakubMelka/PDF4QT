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

#ifndef AUDIOBOOKPLUGIN_H
#define AUDIOBOOKPLUGIN_H

#include "pdfplugin.h"
#include "pdfdocumenttextflow.h"

#include <QObject>

namespace pdfplugin
{

class AudioBookPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.AudioBookPlugin" FILE "AudioBookPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    AudioBookPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

private:
    void onCreateTextStreamTriggered();

    void updateActions();

    QAction* m_createTextStreamAction;

    pdf::PDFDocumentTextFlowEditor m_textFlowEditor;
};

}   // namespace pdfplugin

#endif // AUDIOBOOKPLUGIN_H
