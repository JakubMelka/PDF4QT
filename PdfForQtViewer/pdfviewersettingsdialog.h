#ifndef PDFVIEWERSETTINGSDIALOG_H
#define PDFVIEWERSETTINGSDIALOG_H

#include "pdfviewersettings.h"

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
    explicit PDFViewerSettingsDialog(const PDFViewerSettings::Settings& settings, QWidget* parent);
    virtual ~PDFViewerSettingsDialog() override;

    enum Page : int
    {
        EngineSettings,
        RenderingSettings,
        ShadingSettings
    };

    const PDFViewerSettings::Settings& getSettings() const { return m_settings; }

private slots:
    void on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);

private:
    void loadData();
    void saveData();

    Ui::PDFViewerSettingsDialog* ui;
    PDFViewerSettings::Settings m_settings;
    bool m_isLoadingData;
};

}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGSDIALOG_H
