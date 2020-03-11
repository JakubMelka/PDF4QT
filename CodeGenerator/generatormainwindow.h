//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef GENERATORMAINWINDOW_H
#define GENERATORMAINWINDOW_H

#include <QMainWindow>

namespace codegen
{
class CodeGenerator;
}

namespace Ui
{
class GeneratorMainWindow;
}

class GeneratorMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    GeneratorMainWindow(QWidget* parent = nullptr);
    virtual ~GeneratorMainWindow() override;

    void load(const QString& fileName);
    void save(const QString& fileName);

private slots:
    void on_actionLoad_triggered();
    void on_actionSaveAs_triggered();

    void on_actionSave_triggered();

private:
    void loadSettings();
    void saveSettings();

    Ui::GeneratorMainWindow* ui;
    codegen::CodeGenerator* m_generator;
    QString m_defaultFileName;
};

#endif // GENERATORMAINWINDOW_H
