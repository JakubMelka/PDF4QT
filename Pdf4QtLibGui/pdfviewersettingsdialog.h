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
