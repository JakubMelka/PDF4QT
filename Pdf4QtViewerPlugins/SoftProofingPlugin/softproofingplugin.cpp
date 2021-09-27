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

#include "softproofingplugin.h"
#include "settingsdialog.h"

#include "pdfcms.h"
#include "pdfutils.h"
#include "pdfdrawwidget.h"

#include <QAction>

namespace pdfplugin
{

SoftProofingPlugin::SoftProofingPlugin() :
    pdf::PDFPlugin(nullptr),
    m_enableSoftProofingAction(nullptr),
    m_enableGamutCheckingAction(nullptr),
    m_showSettingsAction(nullptr),
    m_isLoadingGUI(false)
{

}

void SoftProofingPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    m_enableSoftProofingAction = new QAction(QIcon(":/pdfplugins/softproofing/soft-proofing.svg"), tr("Soft Proofing"), this);
    m_enableGamutCheckingAction = new QAction(QIcon(":/pdfplugins/softproofing/gamut-checking.svg"), tr("Gamut Checking"), this);
    m_showSettingsAction = new QAction(QIcon(":/pdfplugins/softproofing/settings.svg"), tr("Soft Proofing Settings"), this);

    m_enableSoftProofingAction->setCheckable(true);
    m_enableGamutCheckingAction->setCheckable(true);

    m_enableSoftProofingAction->setObjectName("actionSoftProofing_EnableSoftProofing");
    m_enableGamutCheckingAction->setObjectName("actionSoftProofing_EnableGamutChecking");
    m_showSettingsAction->setObjectName("actionSoftProofing_ShowSettings");

    connect(m_enableSoftProofingAction, &QAction::triggered, this, &SoftProofingPlugin::onSoftProofingTriggered);
    connect(m_enableGamutCheckingAction, &QAction::triggered, this, &SoftProofingPlugin::onGamutCheckingTriggered);
    connect(m_showSettingsAction, &QAction::triggered, this, &SoftProofingPlugin::onSettingsTriggered);

    updateActions();
}

void SoftProofingPlugin::setCMSManager(pdf::PDFCMSManager* manager)
{
    BaseClass::setCMSManager(manager);

    connect(manager, &pdf::PDFCMSManager::colorManagementSystemChanged, this, &SoftProofingPlugin::updateActions);
}

void SoftProofingPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        updateActions();
    }
}

std::vector<QAction*> SoftProofingPlugin::getActions() const
{
    return { m_enableSoftProofingAction, m_enableGamutCheckingAction, m_showSettingsAction };
}

void SoftProofingPlugin::onSoftProofingTriggered()
{
    if (m_isLoadingGUI)
    {
        // We are just updating gui, do nothing
        return;
    }

    pdf::PDFCMSSettings settings = m_cmsManager->getSettings();
    settings.isSoftProofing = m_enableSoftProofingAction->isChecked();
    m_cmsManager->setSettings(settings);
}

void SoftProofingPlugin::onGamutCheckingTriggered()
{
    if (m_isLoadingGUI)
    {
        // We are just updating gui, do nothing
        return;
    }

    pdf::PDFCMSSettings settings = m_cmsManager->getSettings();
    settings.isGamutChecking = m_enableGamutCheckingAction->isChecked();
    m_cmsManager->setSettings(settings);
}

void SoftProofingPlugin::onSettingsTriggered()
{
    SettingsDialog settingsDialog(m_widget, m_cmsManager->getSettings(), m_cmsManager);
    if (settingsDialog.exec() == SettingsDialog::Accepted)
    {
        m_cmsManager->setSettings(settingsDialog.getSettings());
    }
}

void SoftProofingPlugin::updateActions()
{
    pdf::PDFTemporaryValueChange guard(&m_isLoadingGUI, true);

    if (m_enableSoftProofingAction)
    {
        m_enableSoftProofingAction->setEnabled(m_widget);
        m_enableSoftProofingAction->setChecked(m_cmsManager && m_cmsManager->getSettings().isSoftProofing);
    }
    if (m_enableGamutCheckingAction)
    {
        m_enableGamutCheckingAction->setEnabled(m_widget);
        m_enableGamutCheckingAction->setChecked(m_cmsManager && m_cmsManager->getSettings().isGamutChecking);
    }
    if (m_showSettingsAction)
    {
        m_showSettingsAction->setEnabled(m_widget);
    }
}

}
