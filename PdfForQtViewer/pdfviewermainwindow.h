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

#include <QTreeView>
#include <QMainWindow>
#include <QSharedPointer>

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

private slots:
    void on_actionPageLayoutSinglePage_triggered();
    void on_actionPageLayoutContinuous_triggered();
    void on_actionPageLayoutTwoPages_triggered();
    void on_actionPageLayoutTwoColumns_triggered();
    void on_actionFirstPageOnRightSide_triggered();

    void on_actionRendering_Errors_triggered();

    void on_actionGenerateCMAPrepository_triggered();

private:
    void onActionOpenTriggered();
    void onActionCloseTriggered();
    void onActionQuitTriggered();
    void onPageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount);

    void readSettings();
    void writeSettings();

    void updateTitle();
    void updatePageLayoutActions();

    void openDocument(const QString& fileName);
    void setDocument(const pdf::PDFDocument* document);
    void closeDocument();

    void setPageLayout(pdf::PageLayout pageLayout);

    Ui::PDFViewerMainWindow* ui;
    pdf::PDFWidget* m_pdfWidget;
    QSharedPointer<pdf::PDFDocument> m_pdfDocument;
    QString m_directory;
    QString m_currentFile;
    QDockWidget* m_optionalContentDockWidget;
    QTreeView* m_optionalContentTreeView;
    pdf::PDFOptionalContentTreeItemModel* m_optionalContentTreeModel;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H
