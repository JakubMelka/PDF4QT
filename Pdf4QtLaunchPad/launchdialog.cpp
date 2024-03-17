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

    LaunchApplicationWidget* appEditorWidget = new LaunchApplicationWidget(this);
    appEditorWidget->setObjectName("appEditorWidget");
    ui->gridLayout->addWidget(appEditorWidget, 0, 0);

    LaunchApplicationWidget* appViewerWidget = new LaunchApplicationWidget(this);
    appViewerWidget->setObjectName("appViewerWidget");
    ui->gridLayout->addWidget(appViewerWidget, 0, 1);

    LaunchApplicationWidget* appDiffWidget = new LaunchApplicationWidget(this);
    appDiffWidget->setObjectName("appDiffWidget");
    ui->gridLayout->addWidget(appDiffWidget, 1, 0);

    LaunchApplicationWidget* appPageMasterWidget = new LaunchApplicationWidget(this);
    appPageMasterWidget->setObjectName("appPageMasterWidget");
    ui->gridLayout->addWidget(appPageMasterWidget, 1, 1);

    appEditorWidget->getButton()->setIcon(QIcon(":/app-editor.svg"));
    appViewerWidget->getButton()->setIcon(QIcon(":/app-viewer.svg"));
    appDiffWidget->getButton()->setIcon(QIcon(":/app-diff.svg"));
    appPageMasterWidget->getButton()->setIcon(QIcon(":/app-pagemaster.svg"));

    appEditorWidget->getTitleLabel()->setText(tr("Editor"));
    appViewerWidget->getTitleLabel()->setText(tr("Viewer"));
    appDiffWidget->getTitleLabel()->setText(tr("Diff"));
    appPageMasterWidget->getTitleLabel()->setText(tr("PageMaster"));

    appEditorWidget->getDescriptionLabel()->setText(tr("Go beyond basic browsing. This tool packs a punch with a host of advanced features, including encryption, document reading, digital signature verification, annotation editing, and even support for searching text using regular expressions. Turn pages into images, and enhance your PDF interactions with multiple available plugins."));
    appViewerWidget->getDescriptionLabel()->setText(tr("Simplify your viewing experience. This lightweight viewer offers essential viewing functions in a clean, user-friendly interface."));
    appDiffWidget->getDescriptionLabel()->setText(tr("Spot differences effortlessly. This tool allows users to open two documents and receive a detailed list of differences. View these differences in a page-to-page window where they are clearly marked. Save these differences into an XML file for future reference."));
    appPageMasterWidget->getDescriptionLabel()->setText(tr("Take control of your documents. Manage whole documents or individual pages with ease. Merge documents into a single file, or split them into multiple ones. You can also move, clone, or add pages with a few clicks, all within an intuitive user interface."));

    appEditorWidget->getButton()->setDefault(true);
    appEditorWidget->getButton()->setFocus();

    connect(appEditorWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startEditor);
    connect(appViewerWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startViewer);
    connect(appDiffWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startDiff);
    connect(appPageMasterWidget->getButton(), &QPushButton::clicked, this, &LaunchDialog::startPageMaster);

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
