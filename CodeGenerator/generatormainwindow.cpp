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
    m_currentSettings(nullptr),
    m_isLoadingData(false)
{
    ui->setupUi(this);

    ui->parameterTreeWidget->header()->setMinimumSectionSize(200);

    m_generator->initialize();

    loadSettings();

    if (!m_defaultFileName.isEmpty())
    {
        load(m_defaultFileName);
    }
    connect(ui->generatedFunctionsTreeWidget, &QTreeWidget::currentItemChanged, this, &GeneratorMainWindow::onCurrentItemChanged);
    connect(ui->parameterTreeWidget, &QTreeWidget::currentItemChanged, this, &GeneratorMainWindow::onParameterTreeCurrentItemChanged);
    connect(ui->parameterNameEdit, &QLineEdit::editingFinished, this, &GeneratorMainWindow::saveGeneratedSettings);
    connect(ui->parameterItemTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GeneratorMainWindow::saveGeneratedSettings);
    connect(ui->parameterDataTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GeneratorMainWindow::saveGeneratedSettings);
    connect(ui->parameterValueEdit, &QLineEdit::editingFinished, this, &GeneratorMainWindow::saveGeneratedSettings);
    connect(ui->parameterDescriptionEdit, &QTextBrowser::textChanged, this, &GeneratorMainWindow::saveGeneratedSettings);

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
    settings.setValue("headerFile", m_headerFileName);
    settings.setValue("sourceFile", m_sourceFileName);
}

void GeneratorMainWindow::loadGeneratedSettings()
{
    BoolGuard guard(m_isLoadingData);

    const bool hasName = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::Name);
    const bool hasItemType = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::ItemType);
    const bool hasDataType = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::DataType);
    const bool hasValue = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::Value);
    const bool hasDescription = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::Description);

    ui->parameterNameEdit->setEnabled(hasName);
    ui->parameterItemTypeCombo->setEnabled(hasItemType);
    ui->parameterDataTypeCombo->setEnabled(hasDataType);
    ui->parameterValueEdit->setEnabled(hasValue);
    ui->parameterDescriptionEdit->setEnabled(hasDescription);

    if (hasName)
    {
        ui->parameterNameEdit->setText(m_currentSettings->readField(codegen::GeneratedBase::FieldType::Name).toString());
    }
    else
    {
        ui->parameterNameEdit->clear();
    }

    if (hasItemType)
    {
        m_currentSettings->fillComboBox(ui->parameterItemTypeCombo, codegen::GeneratedBase::FieldType::ItemType);
    }
    else
    {
        ui->parameterItemTypeCombo->clear();
    }

    if (hasDataType)
    {
        m_currentSettings->fillComboBox(ui->parameterDataTypeCombo, codegen::GeneratedBase::FieldType::DataType);
    }
    else
    {
        ui->parameterDataTypeCombo->clear();
    }

    if (hasValue)
    {
        ui->parameterValueEdit->setText(m_currentSettings->readField(codegen::GeneratedBase::FieldType::Value).toString());
    }
    else
    {
        ui->parameterValueEdit->clear();
    }

    if (hasDescription)
    {
        ui->parameterDescriptionEdit->setText(m_currentSettings->readField(codegen::GeneratedBase::FieldType::Description).toString());
    }
    else
    {
        ui->parameterDescriptionEdit->clear();
    }

    ui->itemDeleteButton->setEnabled(m_currentSettings && m_currentSettings->canPerformOperation(codegen::GeneratedBase::Operation::Delete));
    ui->itemUpButton->setEnabled(m_currentSettings && m_currentSettings->canPerformOperation(codegen::GeneratedBase::Operation::MoveUp));
    ui->itemDownButton->setEnabled(m_currentSettings && m_currentSettings->canPerformOperation(codegen::GeneratedBase::Operation::MoveDown));
    ui->itemNewSiblingButton->setEnabled(m_currentSettings && m_currentSettings->canPerformOperation(codegen::GeneratedBase::Operation::NewSibling));
    ui->itemNewChildButton->setEnabled(m_currentSettings && m_currentSettings->canPerformOperation(codegen::GeneratedBase::Operation::NewChild));
}

void GeneratorMainWindow::saveGeneratedSettings()
{
    if (m_isLoadingData || !m_currentSettings)
    {
        return;
    }

    const bool hasName = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::Name);
    const bool hasItemType = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::ItemType);
    const bool hasDataType = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::DataType);
    const bool hasValue = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::Value);
    const bool hasDescription = m_currentSettings && m_currentSettings->hasField(codegen::GeneratedBase::FieldType::Description);

    m_currentSettings->writeField(codegen::GeneratedBase::FieldType::Name, hasName ? ui->parameterNameEdit->text() : QVariant());
    m_currentSettings->writeField(codegen::GeneratedBase::FieldType::ItemType, hasItemType ? ui->parameterItemTypeCombo->currentData() : QVariant());
    m_currentSettings->writeField(codegen::GeneratedBase::FieldType::DataType, hasDataType ? ui->parameterDataTypeCombo->currentData() : QVariant());
    m_currentSettings->writeField(codegen::GeneratedBase::FieldType::Value, hasValue ? ui->parameterValueEdit->text() : QVariant());
    m_currentSettings->writeField(codegen::GeneratedBase::FieldType::Description, hasDescription ? ui->parameterDescriptionEdit->toPlainText() : QVariant());

    if (sender() != ui->parameterDescriptionEdit)
    {
        loadGeneratedSettings();

        // Update the captions
        std::function<void(QTreeWidgetItem*)> updateFunction = [&](QTreeWidgetItem* item)
        {
            const codegen::GeneratedBase* generatedBase = item->data(0, Qt::UserRole).value<codegen::GeneratedBase*>();
            QStringList captions = generatedBase->getCaptions();

            for (int i = 0; i < captions.size(); ++i)
            {
                item->setText(i, captions[i]);
            }

            for (int i = 0; i < item->childCount(); ++i)
            {
                updateFunction(item->child(i));
            }
        };
        updateFunction(ui->parameterTreeWidget->topLevelItem(0));
    }
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
}

void GeneratorMainWindow::updateGeneratedSettingsTree()
{
    BoolGuard guard(m_isLoadingData);

    ui->parameterTreeWidget->setUpdatesEnabled(false);
    ui->parameterTreeWidget->clear();

    QTreeWidgetItem* selected = nullptr;
    std::function<void(codegen::GeneratedBase*, QTreeWidgetItem*)> createTree = [this, &selected, &createTree](codegen::GeneratedBase* generatedItem, QTreeWidgetItem* parent)
    {
        QTreeWidgetItem* item = nullptr;
        if (parent)
        {
            item = new QTreeWidgetItem(parent, generatedItem->getCaptions());
        }
        else
        {
            item = new QTreeWidgetItem(ui->parameterTreeWidget, generatedItem->getCaptions());
        }

        item->setData(0, Qt::UserRole, QVariant::fromValue(generatedItem));

        if (generatedItem == m_currentSettings)
        {
            selected = item;
        }

        for (QObject* object : generatedItem->getItems())
        {
            createTree(qobject_cast<codegen::GeneratedBase*>(object), item);
        }
    };

    if (m_currentFunction)
    {
        createTree(m_currentFunction, nullptr);
    }

    ui->parameterTreeWidget->expandAll();
    ui->parameterTreeWidget->resizeColumnToContents(0);
    ui->parameterTreeWidget->setUpdatesEnabled(true);

    if (selected)
    {
        ui->parameterTreeWidget->setCurrentItem(selected, 0, QItemSelectionModel::SelectCurrent);
    }
}

void GeneratorMainWindow::setCurrentFunction(codegen::GeneratedFunction* currentFunction)
{
    if (m_currentFunction != currentFunction)
    {
        BoolGuard guard(m_isLoadingData);
        m_currentFunction = currentFunction;

        // Select current function
        auto it = m_mapFunctionToWidgetItem.find(m_currentFunction);
        if (it != m_mapFunctionToWidgetItem.cend())
        {
            ui->generatedFunctionsTreeWidget->setCurrentItem(it->second, 0, QItemSelectionModel::SelectCurrent);
            ui->generatedFunctionsTreeWidget->setFocus();
            setCurrentSettings(m_currentFunction);
            updateGeneratedSettingsTree();
        }
    }
}

void GeneratorMainWindow::setCurrentSettings(codegen::GeneratedBase* currentSettings)
{
    if (m_currentSettings != currentSettings)
    {
        BoolGuard guard(m_isLoadingData);
        m_currentSettings = currentSettings;
        loadGeneratedSettings();
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

void GeneratorMainWindow::onParameterTreeCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);

    if (m_isLoadingData)
    {
        return;
    }

    codegen::GeneratedBase* baseObject = current->data(0, Qt::UserRole).value<codegen::GeneratedBase*>();
    setCurrentSettings(baseObject);
}

void GeneratorMainWindow::loadSettings()
{
    QSettings settings("MelkaJ");
    m_defaultFileName = settings.value("fileName").toString();
    m_headerFileName = settings.value("headerFile", QVariant()).toString();
    m_sourceFileName = settings.value("sourceFile", QVariant()).toString();
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

void GeneratorMainWindow::on_itemDeleteButton_clicked()
{
    if (m_currentSettings)
    {
        m_currentSettings->performOperation(codegen::GeneratedBase::Operation::Delete);
        setCurrentSettings(m_currentFunction);
        updateGeneratedSettingsTree();
    }
}

void GeneratorMainWindow::on_itemUpButton_clicked()
{
    if (m_currentSettings)
    {
        m_currentSettings->performOperation(codegen::GeneratedBase::Operation::MoveUp);
        updateGeneratedSettingsTree();
        loadGeneratedSettings();
    }
}

void GeneratorMainWindow::on_itemDownButton_clicked()
{
    if (m_currentSettings)
    {
        m_currentSettings->performOperation(codegen::GeneratedBase::Operation::MoveDown);
        updateGeneratedSettingsTree();
        loadGeneratedSettings();
    }
}

void GeneratorMainWindow::on_itemNewChildButton_clicked()
{
    if (m_currentSettings)
    {
        m_currentSettings->performOperation(codegen::GeneratedBase::Operation::NewChild);
        updateGeneratedSettingsTree();
        loadGeneratedSettings();
    }
}

void GeneratorMainWindow::on_itemNewSiblingButton_clicked()
{
    if (m_currentSettings)
    {
        m_currentSettings->performOperation(codegen::GeneratedBase::Operation::NewSibling);
        updateGeneratedSettingsTree();
        loadGeneratedSettings();
    }
}

void GeneratorMainWindow::on_actionSet_code_header_h_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select cpp header"), QString(), "cpp header (*.h)");
    if (!fileName.isEmpty())
    {
        m_headerFileName = fileName;
        saveSettings();
    }
}

void GeneratorMainWindow::on_actionSet_code_source_cpp_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select cpp source"), QString(), "cpp source (*.cpp)");
    if (!fileName.isEmpty())
    {
        m_sourceFileName = fileName;
        saveSettings();
    }
}

void GeneratorMainWindow::on_actionGenerate_code_triggered()
{
    if (m_generator)
    {
        m_generator->generateCode(m_headerFileName, m_sourceFileName);
    }
}
