//    Copyright (C) 2019 Jakub Melka
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
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFVIEWERMAINWINDOW_H
#define PDFVIEWERMAINWINDOW_H

#include "pdfcatalog.h"
#include "pdfrenderer.h"
#include "pdfprogress.h"
#include "pdfviewersettings.h"

#include <QTreeView>
#include <QMainWindow>
#include <QSharedPointer>
#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>

class QLabel;
class QSpinBox;
class QSettings;
class QDoubleSpinBox;

namespace Ui
{
class PDFViewerMainWindow;
}

namespace pdf
{
class PDFWidget;
class PDFDocument;
class PDFOptionalContentTreeItemModel;
}

namespace pdfviewer
{

class PDFViewerMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PDFViewerMainWindow(QWidget *parent = nullptr);
    virtual ~PDFViewerMainWindow() override;

    virtual void closeEvent(QCloseEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

private slots:
    void on_actionPageLayoutSinglePage_triggered();
    void on_actionPageLayoutContinuous_triggered();
    void on_actionPageLayoutTwoPages_triggered();
    void on_actionPageLayoutTwoColumns_triggered();
    void on_actionFirstPageOnRightSide_triggered();

    void on_actionRendering_Errors_triggered();
    void on_actionOptions_triggered();

    void on_actionAbout_triggered();

private:
    void onActionOpenTriggered();
    void onActionCloseTriggered();
    void onActionQuitTriggered();
    void onPageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount);
    void onDrawSpaceChanged();
    void onPageLayoutChanged();
    void onPageNumberSpinboxEditingFinished();
    void onPageZoomSpinboxEditingFinished();

    void onProgressStarted();
    void onProgressStep(int percentage);
    void onProgressFinished();

    void readSettings();
    void writeSettings();

    void updateTitle();
    void updatePageLayoutActions();
    void updateRenderingOptionActions();
    void updateUI(bool fullUpdate);

    void onViewerSettingsChanged();
    void onRenderingOptionTriggered(bool checked);

    void openDocument(const QString& fileName);
    void setDocument(const pdf::PDFDocument* document);
    void closeDocument();

    void setPageLayout(pdf::PageLayout pageLayout);

    std::vector<QAction*> getRenderingOptionActions() const;

    int adjustDpiX(int value);

    Ui::PDFViewerMainWindow* ui;
    PDFViewerSettings* m_settings;
    pdf::PDFWidget* m_pdfWidget;
    QSharedPointer<pdf::PDFDocument> m_pdfDocument;
    QString m_currentFile;
    QDockWidget* m_optionalContentDockWidget;
    QTreeView* m_optionalContentTreeView;
    pdf::PDFOptionalContentTreeItemModel* m_optionalContentTreeModel;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    QSpinBox* m_pageNumberSpinBox;
    QLabel* m_pageNumberLabel;
    QDoubleSpinBox* m_pageZoomSpinBox;
    bool m_isLoadingUI;
    pdf::PDFProgress* m_progress;
    QWinTaskbarButton* m_taskbarButton;
    QWinTaskbarProgress* m_progressTaskbarIndicator;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H
