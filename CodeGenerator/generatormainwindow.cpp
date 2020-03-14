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

#include "generatormainwindow.h"
#include "ui_generatormainwindow.h"

#include "codegenerator.h"

#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>

GeneratorMainWindow::GeneratorMainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::GeneratorMainWindow),
    m_generator(new codegen::CodeGenerator(this)),
    m_currentFunction(nullptr),
    m_isLoadingData(false)
{
    ui->setupUi(this);

    m_generator->initialize();

    loadSettings();

    if (!m_defaultFileName.isEmpty())
    {
        load(m_defaultFileName);
    }
    connect(ui->generatedFunctionsTreeWidget, &QTreeWidget::currentItemChanged, this, &GeneratorMainWindow::onCurrentItemChanged);

    setWindowState(Qt::WindowMaximized);
    updateFunctionListUI();
}

GeneratorMainWindow::~GeneratorMainWindow()
{
    delete ui;
}

void GeneratorMainWindow::load(const QString& fileName)
{
    QFile file(fileName);
    if (file.open(QFile::ReadOnly | QFile::Truncate))
    {
        QTextStream stream(&file);
        stream.setCodec("UTF-8");

        QDomDocument document;
        document.setContent(stream.readAll());
        m_generator->load(document);
        file.close();
    }
}

void GeneratorMainWindow::saveSettings()
{
    QSettings settings("MelkaJ");
    settings.setValue("fileName", m_defaultFileName);
}

void GeneratorMainWindow::updateFunctionListUI()
{
    BoolGuard guard(m_isLoadingData);

    ui->generatedFunctionsTreeWidget->setUpdatesEnabled(false);
    ui->generatedFunctionsTreeWidget->clear();
    m_mapFunctionToWidgetItem.clear();

    // First, create roots
    std::map<codegen::GeneratedFunction::FunctionType, QTreeWidgetItem*> mapFunctionTypeToRoot;
    for (QObject* functionObject : m_generator->getFunctions())
    {
        codegen::GeneratedFunction* function = qobject_cast<codegen::GeneratedFunction*>(functionObject);
        Q_ASSERT(function);

        if (!mapFunctionTypeToRoot.count(function->getFunctionType()))
        {
            QTreeWidgetItem* subroot = new QTreeWidgetItem(ui->generatedFunctionsTreeWidget, QStringList(function->getFunctionTypeString()));
            subroot->setFlags(Qt::ItemIsEnabled);
            mapFunctionTypeToRoot[function->getFunctionType()] = subroot;
        }
    }
    ui->generatedFunctionsTreeWidget->sortByColumn(0, Qt::AscendingOrder);

    for (QObject* functionObject : m_generator->getFunctions())
    {
        codegen::GeneratedFunction* function = qobject_cast<codegen::GeneratedFunction*>(functionObject);
        Q_ASSERT(function);

        QTreeWidgetItem* functionItem = new QTreeWidgetItem(mapFunctionTypeToRoot[function->getFunctionType()], QStringList(function->getFunctionName()));
        functionItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        functionItem->setData(0, Qt::UserRole, QVariant::fromValue(function));
        m_mapFunctionToWidgetItem[function] = functionItem;
    }

    ui->generatedFunctionsTreeWidget->expandAll();
    ui->generatedFunctionsTreeWidget->setUpdatesEnabled(true);

    // Select current function
    auto it = m_mapFunctionToWidgetItem.find(m_currentFunction);
    if (it != m_mapFunctionToWidgetItem.cend())
    {
        ui->generatedFunctionsTreeWidget->setCurrentItem(it->second, 0, QItemSelectionModel::SelectCurrent);
    }

    m_isLoadingData = false;
}

void GeneratorMainWindow::setCurrentFunction(codegen::GeneratedFunction* currentFunction)
{
    BoolGuard guard(m_isLoadingData);

    if (m_currentFunction != currentFunction)
    {
        m_currentFunction = currentFunction;

        // Select current function
        auto it = m_mapFunctionToWidgetItem.find(m_currentFunction);
        if (it != m_mapFunctionToWidgetItem.cend())
        {
            ui->generatedFunctionsTreeWidget->setCurrentItem(it->second, 0, QItemSelectionModel::SelectCurrent);
        }
    }
}

void GeneratorMainWindow::onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);

    if (m_isLoadingData)
    {
        return;
    }

    codegen::GeneratedFunction* function = current->data(0, Qt::UserRole).value<codegen::GeneratedFunction*>();
    setCurrentFunction(function);
}

void GeneratorMainWindow::loadSettings()
{
    QSettings settings("MelkaJ");
    m_defaultFileName = settings.value("fileName").toString();
}

void GeneratorMainWindow::save(const QString& fileName)
{
    QFile file(fileName);
    if (file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QTextStream stream(&file);
        stream.setCodec("UTF-8");

        QDomDocument document;
        m_generator->store(document);
        document.save(stream, 2, QDomDocument::EncodingFromTextStream);
        file.close();

        // Update default file name
        m_defaultFileName = fileName;
        saveSettings();
    }
}

void GeneratorMainWindow::on_actionLoad_triggered()
{
    QFileInfo info(m_defaultFileName);
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select XML definition file"), info.dir().absolutePath(), "XML files (*.xml)");
    if (!fileName.isEmpty())
    {
        m_defaultFileName = fileName;
        saveSettings();

        load(m_defaultFileName);
    }
}

void GeneratorMainWindow::on_actionSaveAs_triggered()
{
    QFileInfo info(m_defaultFileName);
    QString fileName = QFileDialog::getSaveFileName(this, tr("Select XML definition file"), info.dir().absolutePath(), "XML files (*.xml)");
    if (!fileName.isEmpty())
    {
        save(fileName);
    }
}

void GeneratorMainWindow::on_actionSave_triggered()
{
    if (!m_defaultFileName.isEmpty())
    {
        save(m_defaultFileName);
    }
    else
    {
        on_actionSaveAs_triggered();
    }
}

void GeneratorMainWindow::on_newFunctionButton_clicked()
{
    QString functionName = QInputDialog::getText(this, tr("Create function"), tr("Enter function name"));
    if (!functionName.isEmpty())
    {
        codegen::GeneratedFunction* generatedFunction = m_generator->addFunction(functionName);
        updateFunctionListUI();
        setCurrentFunction(generatedFunction);
    }
}

void GeneratorMainWindow::on_cloneFunctionButton_clicked()
{
    if (m_currentFunction)
    {
        codegen::GeneratedFunction* generatedFunction = m_generator->addFunction(m_currentFunction->clone(nullptr));
        updateFunctionListUI();
        setCurrentFunction(generatedFunction);
    }
}

void GeneratorMainWindow::on_removeFunctionButton_clicked()
{
    if (m_currentFunction)
    {
        codegen::GeneratedFunction* functionToDelete = m_currentFunction;
        setCurrentFunction(nullptr);
        m_generator->removeFunction(functionToDelete);
        updateFunctionListUI();
    }
}
