//    Copyright (C) 2019 Jakub Melka
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
        ShadingSettings,
        SecuritySettings
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
