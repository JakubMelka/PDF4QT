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

#include "launchdialog.h"
#include "launchapplicationwidget.h"
#include "ui_launchdialog.h"

#include <QLabel>
#include <QPushButton>
#include <QProcess>

LaunchDialog::LaunchDialog(QWidget* parent)
    : QDialog(parent, Qt::WindowStaysOnTopHint | Qt::Window | Qt::Dialog)
    , ui(new Ui::LaunchDialog)
{
    ui->setupUi(this);
    ui->retranslateUi(this);

    ui->appEditorWidget->getButton()->setIcon(QIcon(":/app-editor.svg"));
    ui->appViewerWidget->getButton()->setIcon(QIcon(":/app-viewer.svg"));
    ui->appDiffWidget->getButton()->setIcon(QIcon(":/app-diff.svg"));
    ui->appPageMasterWidget->getButton()->setIcon(QIcon(":/app-pagemaster.svg"));

    ui->appEditorWidget->getTitleLabel()->setText(tr("Editor"));
    ui->appViewerWidget->getTitleLabel()->setText(tr("Viewer"));
    ui->appDiffWidget->getTitleLabel()->setText(tr("Diff"));
    ui->appPageMasterWidget->getTitleLabel()->setText(tr("PageMaster"));

    ui->appEditorWidget->getDescriptionLabel()->setText(tr("Go beyond basic browsing. This tool packs a punch with a host of advanced features, including encryption, document reading, digital signature verification, annotation editing, and even support for searching text using regular expressions. Turn pages into images, and enhance your PDF interactions with multiple available plugins."));
    ui->appViewerWidget->getDescriptionLabel()->setText(tr("Simplify your viewing experience. This lightweight viewer offers essential viewing functions in a clean, user-friendly interface."));
    ui->appDiffWidget->getDescriptionLabel()->setText(tr("Spot differences effortlessly. This tool allows users to open two documents and receive a detailed list of differences. View these differences in a page-to-page window where they are clearly marked. Save these differences into an XML file for future reference."));
    ui->appPageMasterWidget->getDescriptionLabel()->setText(tr("Take control of your documents. Manage whole documents or individual pages with ease. Merge documents into a single file, or split them into multiple ones. You can also move, clone, or add pages with a few clicks, all within an intuitive user interface."));

    ui->appEditorWidget->getButton()->setDefault(true);
    ui->appEditorWidget->getButton()->setFocus();

    connect(ui->appEditorWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startEditor);
    connect(ui->appViewerWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startViewer);
    connect(ui->appDiffWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startDiff);
    connect(ui->appPageMasterWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startPageMaster);

    setFixedSize(minimumSizeHint());
}

LaunchDialog::~LaunchDialog()
{
    delete ui;
}

void LaunchDialog::startEditor()
{
    startProgram("Pdf4QtEditor");
}

void LaunchDialog::startViewer()
{
    startProgram("Pdf4QtViewer");
}

void LaunchDialog::startPageMaster()
{
    startProgram("Pdf4QtPageMaster");
}

void LaunchDialog::startDiff()
{
    startProgram("Pdf4QtDiff");
}

void LaunchDialog::startProgram(const QString& program)
{
    QProcess::startDetached(program);
    close();
}
