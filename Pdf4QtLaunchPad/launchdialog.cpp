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

#include "launchdialog.h"
#include "launchapplicationwidget.h"
#include "ui_launchdialog.h"

#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QMessageBox>

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
#ifndef Q_OS_WIN
    QString appDir = qgetenv("APPDIR");
#if defined(PDF4QT_FLATPAK_BUILD)
    QString flatpakAppDir = "/app";
#else
    QString flatpakAppDir;
#endif
    QString internalToolPath;

    if (!flatpakAppDir.isEmpty())
    {
        internalToolPath = QString("%1/bin/%2").arg(flatpakAppDir, program);
    }
    else if (!appDir.isEmpty())
    {
        internalToolPath = QString("%1/usr/bin/%2").arg(appDir, program);
    }
    else
    {
        internalToolPath = QString("./%1").arg(program);
    }

    qint64 pid = 0;
    if (!QProcess::startDetached(internalToolPath, {}, QString(), &pid))
    {
        QMessageBox::critical(this, tr("Error"), tr("Failed to start process '%1'").arg(internalToolPath));
    }
#else
    QProcess::startDetached(program);
#endif
    close();
}
