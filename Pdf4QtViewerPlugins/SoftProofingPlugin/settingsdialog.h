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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "softproofingplugin.h"
#include "pdfcms.h"

#include <QDialog>

class QComboBox;

namespace Ui
{
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent, const pdf::PDFCMSSettings& settings, const pdf::PDFCMSManager* manager);
    virtual ~SettingsDialog() override;

    virtual void accept() override;

    const pdf::PDFCMSSettings& getSettings() const { return m_settings; }

private:
    Ui::SettingsDialog* ui;

    pdf::PDFCMSSettings m_settings;
};

#endif // SETTINGSDIALOG_H
