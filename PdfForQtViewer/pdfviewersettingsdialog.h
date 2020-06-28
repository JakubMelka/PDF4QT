//    Copyright (C) 2019-2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFVIEWERSETTINGSDIALOG_H
#define PDFVIEWERSETTINGSDIALOG_H

#include "pdfviewersettings.h"

#include <QDialog>

class QListWidgetItem;
class QNetworkReply;
class QNetworkAccessManager;

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
                                     QList<QAction*> actions,
                                     pdf::PDFCMSManager* cmsManager,
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
        SecuritySettings,
        UISettings,
        SpeechSettings,
        FormSettings,
        SignatureSettings
    };

    const PDFViewerSettings::Settings& getSettings() const { return m_settings; }
    const pdf::PDFCMSSettings& getCMSSettings() const { return m_cmsSettings; }
    const OtherSettings& getOtherSettings() const { return m_otherSettings; }
    const pdf::PDFCertificateStore& getCertificateStore() const { return m_certificateStore; }

private slots:
    void on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);
    void on_cmsProfileDirectoryButton_clicked();

    void on_trustedCertificateStoreDownloadEUTLButton_clicked();

    void on_removeCertificateButton_clicked();

private:
    void loadData();
    void saveData();

    void updateTrustedCertificatesTable();
    void updateTrustedCertificatesTableActions();

    void loadActionShortcutsTable();
    bool saveActionShortcutsTable();

    void setSpeechEngine(const QString& engine);

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
    pdf::PDFCertificateStore m_certificateStore;
    QNetworkAccessManager* m_networkAccessManager;
    QNetworkReply* m_downloadCertificatesFromEUTLReply;
};

}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGSDIALOG_H
