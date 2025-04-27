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
    void on_actionSet_code_header_XFA_triggered();
    void on_actionSet_code_source_XFA_triggered();
    void on_actionGenerate_XFA_code_triggered();
    void on_actionSet_XFA_description_triggered();

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

    QString m_XFAdefinitionFileName;
    QString m_XFAheaderFileName;
    QString m_XFAsourceFileName;
};

#endif // GENERATORMAINWINDOW_H
