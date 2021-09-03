//    Copyright (C) 2021 Jakub Melka
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

#ifndef PDFDOCDIFF_MAINWINDOW_H
#define PDFDOCDIFF_MAINWINDOW_H

#include "pdfdocument.h"
#include "pdfdiff.h"

#include <QMainWindow>
#include <QSignalMapper>

namespace Ui
{
class MainWindow;
}

namespace pdfdocdiff
{

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent);
    virtual ~MainWindow() override;

    enum class Operation
    {
        GetSource,
        About
    };

private slots:
    void updateActions();

private:
    void onMappedActionTriggered(int actionId);

    void loadSettings();
    void saveSettings();

    bool canPerformOperation(Operation operation) const;
    void performOperation(Operation operation);

    struct Settings
    {
        QString directory;
    };

    Ui::MainWindow* ui;

    Settings m_settings;
    QSignalMapper m_mapper;
    pdf::PDFDiff m_diff;

    pdf::PDFDocument m_leftDocument;
    pdf::PDFDocument m_rightDocument;
};

}   // namespace pdfdocdiff

#endif // PDFDOCDIFF_MAINWINDOW_H
