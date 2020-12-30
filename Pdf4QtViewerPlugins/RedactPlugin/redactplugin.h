//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef REDACTPLUGIN_H
#define REDACTPLUGIN_H

#include "pdfplugin.h"

#include <QObject>

namespace pdfplugin
{

class RedactPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Pdf4Qt.RedactPlugin" FILE "RedactPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    RedactPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

private:
    void updateActions();

    void onRedactTextSelectionTriggered();
    void onRedactPageTriggered();
    void onCreateRedactedDocumentTriggered();

    QString getRedactedFileName() const;

    QAction* m_actionRedactRectangle;
    QAction* m_actionRedactText;
    QAction* m_actionRedactTextSelection;
    QAction* m_actionRedactPage;
    QAction* m_actionCreateRedactedDocument;
};

}   // namespace pdfplugin

#endif // REDACTPLUGIN_H
