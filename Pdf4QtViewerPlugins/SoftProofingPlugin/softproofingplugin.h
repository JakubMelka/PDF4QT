//    Copyright (C) 2020-2021 Jakub Melka
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

#ifndef SOFTPROOFINGPLUGIN_H
#define SOFTPROOFINGPLUGIN_H

#include "pdfplugin.h"

#include <QObject>

namespace pdfplugin
{

class SoftProofingPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.SoftProofingPlugin" FILE "SoftProofingPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    SoftProofingPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setCMSManager(pdf::PDFCMSManager* manager) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

private:
    void onSoftProofingTriggered();
    void onGamutCheckingTriggered();
    void onSettingsTriggered();

    void updateActions();

    QAction* m_enableSoftProofingAction;
    QAction* m_enableGamutCheckingAction;
    QAction* m_showSettingsAction;

    bool m_isLoadingGUI;
};

}   // namespace pdfplugin

#endif // SOFTPROOFINGPLUGIN_H
