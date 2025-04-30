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

    m_enableSoftProofingAction = new QAction(QIcon(":/pdfplugins/softproofing/soft-proofing.svg"), tr("&Soft Proofing"), this);
    m_enableGamutCheckingAction = new QAction(QIcon(":/pdfplugins/softproofing/gamut-checking.svg"), tr("&Gamut Checking"), this);
    m_showSettingsAction = new QAction(QIcon(":/pdfplugins/softproofing/settings.svg"), tr("Soft &Proofing Settings"), this);

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

QString SoftProofingPlugin::getPluginMenuName() const
{
    return tr("Soft &Proofing");
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
