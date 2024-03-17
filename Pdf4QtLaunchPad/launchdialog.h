//    Copyright (C) 2024 Jakub Melka
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

#ifndef LAUNCHDIALOG_H
#define LAUNCHDIALOG_H

#include <QDialog>

namespace Ui
{
class LaunchDialog;
}

class LaunchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LaunchDialog(QWidget* parent);
    virtual ~LaunchDialog() override;

private:
    void startEditor();
    void startViewer();
    void startPageMaster();
    void startDiff();

    void startProgram(const QString& program);

    Ui::LaunchDialog* ui;
};

#endif // LAUNCHDIALOG_H
