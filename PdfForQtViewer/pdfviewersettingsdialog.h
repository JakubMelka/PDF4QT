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
    explicit PDFViewerSettingsDialog(QWidget* parent);
    virtual ~PDFViewerSettingsDialog() override;

    enum Page : int
    {
        EngineSettings,
        RenderingSettings,
        ShadingSettings
    };

private slots:
    void on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous);

private:
    Ui::PDFViewerSettingsDialog* ui;
};

}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGSDIALOG_H
