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

GeneratorMainWindow::GeneratorMainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::GeneratorMainWindow),
    m_generator(new codegen::CodeGenerator(this))
{
    ui->setupUi(this);

    m_generator->initialize();

    loadSettings();

    if (!m_defaultFileName.isEmpty())
    {
        load(m_defaultFileName);
    }

    // Temporary - to delete:
    m_generator->addFunction("createAnnotationWatermark");
    m_generator->addFunction("createAnnotationFreeText");
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
