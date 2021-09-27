//    Copyright (C) 2020-2021 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#ifndef GENERATORMAINWINDOW_H
#define GENERATORMAINWINDOW_H

#include <QMainWindow>

class QTreeWidgetItem;

namespace codegen
{
class CodeGenerator;
class GeneratedFunction;
class GeneratedBase;
}

namespace Ui
{
class GeneratorMainWindow;
}

class BoolGuard
{
public:
    explicit inline BoolGuard(bool& value) :
        m_oldValue(value),
        m_value(&value)
    {
        *m_value = true;
    }

    inline ~BoolGuard()
    {
        *m_value = m_oldValue;
    }

private:
    bool m_oldValue;
    bool* m_value;
};

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
    void on_newFunctionButton_clicked();
    void on_cloneFunctionButton_clicked();
    void on_removeFunctionButton_clicked();
    void on_itemDeleteButton_clicked();
    void on_itemUpButton_clicked();
    void on_itemDownButton_clicked();
    void on_itemNewChildButton_clicked();
    void on_itemNewSiblingButton_clicked();
    void on_actionSet_code_header_h_triggered();
    void on_actionSet_code_source_cpp_triggered();
    void on_actionGenerate_code_triggered();

private:
    void loadSettings();
    void saveSettings();

    void loadGeneratedSettings();
    void saveGeneratedSettings();

    void updateFunctionListUI();
    void updateGeneratedSettingsTree();
    void setCurrentFunction(codegen::GeneratedFunction* currentFunction);
    void setCurrentSettings(codegen::GeneratedBase* currentSettings);
    void onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onParameterTreeCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);

    Ui::GeneratorMainWindow* ui;
    codegen::CodeGenerator* m_generator;
    codegen::GeneratedFunction* m_currentFunction;
    codegen::GeneratedBase* m_currentSettings;
    QString m_defaultFileName;
    QString m_headerFileName;
    QString m_sourceFileName;
    std::map<codegen::GeneratedFunction*, QTreeWidgetItem*> m_mapFunctionToWidgetItem;
    bool m_isLoadingData;
};

#endif // GENERATORMAINWINDOW_H
