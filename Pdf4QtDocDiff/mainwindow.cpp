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

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "aboutdialog.h"

#include "pdfwidgetutils.h"

#include <QToolBar>
#include <QDesktopWidget>
#include <QDesktopServices>

namespace pdfdocdiff
{

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(800, 600)));

    ui->actionGet_Source->setData(int(Operation::GetSource));
    ui->actionAbout->setData(int(Operation::About));

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(24, 24));
    auto toolbars = findChildren<QToolBar*>();
    for (QToolBar* toolbar : toolbars)
    {
        toolbar->setIconSize(iconSize);
        ui->menuToolbars->addAction(toolbar->toggleViewAction());
    }

    connect(&m_mapper, QOverload<int>::of(&QSignalMapper::mapped), this, &MainWindow::onMappedActionTriggered);

    QList<QAction*> actions = findChildren<QAction*>();
    for (QAction* action : actions)
    {
        QVariant actionData = action->data();
        if (actionData.isValid())
        {
            connect(action, &QAction::triggered, &m_mapper, QOverload<>::of(&QSignalMapper::map));
            m_mapper.setMapping(action, actionData.toInt());
        }
    }

    m_diff.setLeftDocument(&m_leftDocument);
    m_diff.setRightDocument(&m_rightDocument);

    loadSettings();
    updateActions();
}

MainWindow::~MainWindow()
{
    saveSettings();
    delete ui;
}

void MainWindow::onMappedActionTriggered(int actionId)
{
    performOperation(static_cast<Operation>(actionId));
}

void MainWindow::updateActions()
{
    QList<QAction*> actions = findChildren<QAction*>();
    for (QAction* action : actions)
    {
        QVariant actionData = action->data();
        if (actionData.isValid())
        {
            action->setEnabled(canPerformOperation(static_cast<Operation>(actionData.toInt())));
        }
    }
}

void MainWindow::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty())
    {
        QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
        QRect windowRect(0, 0, availableGeometry.width() / 2, availableGeometry.height() / 2);
        windowRect = windowRect.translated(availableGeometry.center() - windowRect.center());
        setGeometry(windowRect);
    }
    else
    {
        restoreGeometry(geometry);
    }

    QByteArray state = settings.value("windowState", QByteArray()).toByteArray();
    if (!state.isEmpty())
    {
        restoreState(state);
    }
    settings.endGroup();

    settings.beginGroup("Settings");
    m_settings.directory = settings.value("directory").toString();
    settings.endGroup();
}

void MainWindow::saveSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();

    settings.beginGroup("Settings");
    settings.setValue("directory", m_settings.directory);
    settings.endGroup();
}

bool MainWindow::canPerformOperation(Operation operation) const
{
    switch (operation)
    {
        case Operation::GetSource:
        case Operation::About:
            return true;

        default:
            Q_ASSERT(false);
            break;
    }

    return false;
}

void MainWindow::performOperation(Operation operation)
{
    switch (operation)
    {
        case Operation::GetSource:
            QDesktopServices::openUrl(QUrl("https://github.com/JakubMelka/PDF4QT"));
            break;

        case Operation::About:
        {
            PDFAboutDialog aboutDialog(this);
            aboutDialog.exec();
            break;
        }
        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    updateActions();
}

}   // namespace pdfdocdiff
