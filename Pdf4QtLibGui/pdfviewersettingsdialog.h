//    Copyright (C) 2019-2025 Jakub Melka
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

#ifndef PDFVIEWERSETTINGSDIALOG_H
#define PDFVIEWERSETTINGSDIALOG_H

#include "pdfviewersettings.h"
#include "pdfplugin.h"

#include <QDialog>

class QListWidgetItem;

namespace Ui
{
class PDFViewerSettingsDialog;
}

namespace pdfviewer
{

class PDFViewerSettingsDialog : public QDialog
{
    Q_OBJECT

public:

    struct OtherSettings
    {
        int maximumRecentFileCount = 0;
    };

    /// Constructor
    /// \param settings Viewer settings
    /// \param cmsSettings Color management system settings
    /// \param otherSettings Other settings
    /// \param certificateStore Certificate store
    /// \param actions Actions
    /// \param cmsManager CMS manager
    /// \param parent Parent widget
    explicit PDFViewerSettingsDialog(const PDFViewerSettings::Settings& settings,
                                     const pdf::PDFCMSSettings& cmsSettings,
                                     const OtherSettings& otherSettings,
                                     const pdf::PDFCertificateStore& certificateStore,
                                     const std::vector<QAction*>& actions,
                                     pdf::PDFCMSManager* cmsManager,
                                     const QStringList& enabledPlugins,
                                     const pdf::PDFPluginInfos& plugins,
                                     QWidget* parent);
    virtual ~PDFViewerSettingsDialog() override;

    virtual void accept() override;
    virtual void reject() override;

    enum Page : int
    {
        EngineSettings,
        RenderingSettings,
        ShadingSettings,
        CacheSettings,
        ShortcutSettings,
        ColorManagementSystemSettings,
        ColorPostprocessingSettings,
        SecuritySettings,
        UISettings,
        SpeechSettings,
        FormSettings,
        SignatureSettings,
        PluginsSettings
    };

    const PDFViewerSettings::Settings& getSettings() const { return m_settings; }
    const pdf::PDFCMSSettings& getCMSSettings() const { return m_cmsSettings; }
    const OtherSettings& getOtherSettings() const { return m_otherSettings; }
    const pdf::PDFCertificateStore& getCertificateStore() const { return m_certificateStore; }
    const QStringList& getEnabledPlugins() const { return m_enabledPlugins; }

private slots:
    void on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void on_cmsProfileDirectoryButton_clicked();
    void on_removeCertificateButton_clicked();

private:
    void loadData();
    void saveData();

    void updateTrustedCertificatesTable();
    void updateTrustedCertificatesTableActions();

    void loadActionShortcutsTable();
    bool saveActionShortcutsTable();

    void loadPluginsTable();
    void savePluginsTable();
    void updatePluginInformation();

    void setSpeechEngine(const QString& engine, const QString& locale);

    /// Returns true, if dialog can be closed. If not, then message is displayed
    /// and false is returned.
    bool canCloseDialog();

    Ui::PDFViewerSettingsDialog* ui;
    PDFViewerSettings::Settings m_settings;
    pdf::PDFCMSSettings m_cmsSettings;
    OtherSettings m_otherSettings;
    QList<QAction*> m_actions;
    bool m_isLoadingData;
    QStringList m_textToSpeechEngines;
    QString m_currentSpeechEngine;
    QString m_currentSpeechLocale;
    pdf::PDFCertificateStore m_certificateStore;
    QStringList m_enabledPlugins;
    pdf::PDFPluginInfos m_plugins;
};

}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGSDIALOG_H
