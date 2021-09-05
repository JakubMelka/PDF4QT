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
#include "pdfdocumentreader.h"

#include <QToolBar>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>

namespace pdfdocdiff
{

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_progress(new pdf::PDFProgress(this)),
    m_taskbarButton(new QWinTaskbarButton(this)),
    m_progressTaskbarIndicator(nullptr),
    m_diff(nullptr),
    m_isChangingProgressStep(false),
    m_dontDisplayErrorMessage(false)
{
    ui->setupUi(this);

    setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(this, QSize(800, 600)));

    // Initialize task bar progress
    m_progressTaskbarIndicator = m_taskbarButton->progress();

    ui->actionGet_Source->setData(int(Operation::GetSource));
    ui->actionAbout->setData(int(Operation::About));
    ui->actionOpen_Left->setData(int(Operation::OpenLeft));
    ui->actionOpen_Right->setData(int(Operation::OpenRight));
    ui->actionCompare->setData(int(Operation::Compare));
    ui->actionClose->setData(int(Operation::Close));

    QToolBar* mainToolbar = addToolBar(tr("Main"));
    mainToolbar->setObjectName("main_toolbar");
    mainToolbar->addActions({ ui->actionOpen_Left, ui->actionOpen_Right });
    mainToolbar->addSeparator();
    mainToolbar->addAction(ui->actionCompare);

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

    connect(m_progress, &pdf::PDFProgress::progressStarted, this, &MainWindow::onProgressStarted);
    connect(m_progress, &pdf::PDFProgress::progressStep, this, &MainWindow::onProgressStep);
    connect(m_progress, &pdf::PDFProgress::progressFinished, this, &MainWindow::onProgressFinished);

    m_diff.setProgress(m_progress);
    m_diff.setOption(pdf::PDFDiff::Asynchronous, true);
    connect(&m_diff, &pdf::PDFDiff::comparationFinished, this, &MainWindow::onComparationFinished);

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

void MainWindow::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    m_taskbarButton->setWindow(windowHandle());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    BaseClass::closeEvent(event);
    m_diff.stop();
}

void MainWindow::onMappedActionTriggered(int actionId)
{
    performOperation(static_cast<Operation>(actionId));
}

void MainWindow::onComparationFinished()
{
    auto result = m_diff.getResult().getResult();
    if (!result && !m_dontDisplayErrorMessage)
    {
        QMessageBox::critical(this, tr("Error"), result.getErrorMessage());
    }
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
        case Operation::OpenLeft:
        case Operation::OpenRight:
        case Operation::Compare:
        case Operation::Close:
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
        case Operation::OpenLeft:
        {
            pdf::PDFTemporaryValueChange guard(&m_dontDisplayErrorMessage, true);
            m_diff.stop();

            std::optional<pdf::PDFDocument> document = openDocument();
            if (document)
            {
                m_leftDocument = std::move(*document);

                const size_t pageCount = m_leftDocument.getCatalog()->getPageCount();
                if (pageCount > 1)
                {
                    ui->leftPageSelectionEdit->setText(QString("1-%2").arg(pageCount));
                }
                else if (pageCount == 1)
                {
                    ui->leftPageSelectionEdit->setText("1");
                }
                else
                {
                    ui->leftPageSelectionEdit->clear();
                }
            }
            break;
        }

        case Operation::OpenRight:
        {
            pdf::PDFTemporaryValueChange guard(&m_dontDisplayErrorMessage, true);
            m_diff.stop();

            std::optional<pdf::PDFDocument> document = openDocument();
            if (document)
            {
                m_rightDocument = std::move(*document);

                const size_t pageCount = m_rightDocument.getCatalog()->getPageCount();
                if (pageCount > 1)
                {
                    ui->rightPageSelectionEdit->setText(QString("1-%2").arg(pageCount));
                }
                else if (pageCount == 1)
                {
                    ui->rightPageSelectionEdit->setText("1");
                }
                else
                {
                    ui->rightPageSelectionEdit->clear();
                }
            }
            break;
        }

        case Operation::Compare:
        {
            pdf::PDFTemporaryValueChange guard(&m_dontDisplayErrorMessage, true);
            m_diff.stop();

            QString errorMessage;

            pdf::PDFClosedIntervalSet rightPageIndices;
            pdf::PDFClosedIntervalSet leftPageIndices = pdf::PDFClosedIntervalSet::parse(1, qMax<pdf::PDFInteger>(1, m_leftDocument.getCatalog()->getPageCount()), ui->leftPageSelectionEdit->text(), &errorMessage);

            if (errorMessage.isEmpty())
            {
                rightPageIndices = pdf::PDFClosedIntervalSet::parse(1, qMax<pdf::PDFInteger>(1, m_rightDocument.getCatalog()->getPageCount()), ui->rightPageSelectionEdit->text(), &errorMessage);
            }

            // Check if pages are succesfully parsed
            if (!errorMessage.isEmpty())
            {
                QMessageBox::critical(this, tr("Error"), errorMessage);
                break;
            }

            leftPageIndices.translate(-1);
            rightPageIndices.translate(-1);

            m_diff.setPagesForLeftDocument(std::move(leftPageIndices));
            m_diff.setPagesForRightDocument(std::move(rightPageIndices));

            m_diff.start();
            break;
        }

        case Operation::Close:
        {
            close();
            break;
        }

        case Operation::GetSource:
        {
            QDesktopServices::openUrl(QUrl("https://github.com/JakubMelka/PDF4QT"));
            break;
        }

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

std::optional<pdf::PDFDocument> MainWindow::openDocument()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select PDF document"), m_settings.directory, tr("PDF document (*.pdf)"));
    if (fileName.isEmpty())
    {
        return std::nullopt;
    }

    auto queryPassword = [this](bool* ok)
    {
        *ok = false;
        return QInputDialog::getText(this, tr("Encrypted document"), tr("Enter password to access document content"), QLineEdit::Password, QString(), ok);
    };

    // Mark current directory as this
    QFileInfo fileInfo(fileName);
    m_settings.directory = fileInfo.dir().absolutePath();

    // Try to open a new document
    pdf::PDFDocumentReader reader(nullptr, qMove(queryPassword), true, false);
    pdf::PDFDocument document = reader.readFromFile(fileName);

    QString errorMessage = reader.getErrorMessage();
    pdf::PDFDocumentReader::Result result = reader.getReadingResult();
    if (result == pdf::PDFDocumentReader::Result::OK)
    {
        return document;
    }
    else if (result == pdf::PDFDocumentReader::Result::Failed)
    {
        QMessageBox::critical(this, tr("Error"), errorMessage);
    }

    return pdf::PDFDocument();
}

void MainWindow::onProgressStarted(pdf::ProgressStartupInfo info)
{
    m_progressTaskbarIndicator->setRange(0, 100);
    m_progressTaskbarIndicator->reset();
    m_progressTaskbarIndicator->show();
}

void MainWindow::onProgressStep(int percentage)
{
    if (m_isChangingProgressStep)
    {
        return;
    }

    pdf::PDFTemporaryValueChange guard(&m_isChangingProgressStep, true);
    m_progressTaskbarIndicator->setValue(percentage);
}

void MainWindow::onProgressFinished()
{
    m_progressTaskbarIndicator->hide();
}

}   // namespace pdfdocdiff
