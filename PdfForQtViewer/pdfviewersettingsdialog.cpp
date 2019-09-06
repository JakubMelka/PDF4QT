#include "pdfviewersettingsdialog.h"
#include "ui_pdfviewersettingsdialog.h"

#include <QListWidgetItem>

namespace pdfviewer
{

PDFViewerSettingsDialog::PDFViewerSettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PDFViewerSettingsDialog)
{
    ui->setupUi(this);

    new QListWidgetItem(QIcon(":/resources/engine.svg"), tr("Engine"), ui->optionsPagesWidget, EngineSettings);
    new QListWidgetItem(QIcon(":/resources/rendering.svg"), tr("Rendering"), ui->optionsPagesWidget, RenderingSettings);
    new QListWidgetItem(QIcon(":/resources/shading.svg"), tr("Shading"), ui->optionsPagesWidget, ShadingSettings);

    ui->renderingEngineComboBox->addItem(tr("Software"), static_cast<int>(pdf::RendererEngine::Software));
    ui->renderingEngineComboBox->addItem(tr("Hardware accelerated (OpenGL)"), static_cast<int>(pdf::RendererEngine::OpenGL));

    for (int i : { 1, 2, 4, 8, 16 })
    {
        ui->multisampleAntialiasingSamplesCountComboBox->addItem(QString::number(i));
    }
}

PDFViewerSettingsDialog::~PDFViewerSettingsDialog()
{
    delete ui;
}

void PDFViewerSettingsDialog::on_optionsPagesWidget_currentItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);

    switch (current->type())
    {
        case EngineSettings:
            ui->stackedWidget->setCurrentWidget(ui->enginePage);
            break;

        case RenderingSettings:
            ui->stackedWidget->setCurrentWidget(ui->renderingPage);
            break;

        case ShadingSettings:
            ui->stackedWidget->setCurrentWidget(ui->shadingPage);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
}

}   // namespace pdfviewer
