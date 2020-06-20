//    Copyright (C) 2019-2020 Jakub Melka
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
#include "pdfdocument.h"
#include "pdfviewersettings.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentpropertiesdialog.h"
#include "pdfwidgettool.h"
#include "pdfrecentfilemanager.h"
#include "pdftexttospeech.h"
#include "pdfannotation.h"
#include "pdfform.h"
#include "pdfundoredomanager.h"

#include <QFuture>
#include <QTreeView>
#include <QMainWindow>
#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>
#include <QFutureWatcher>
#include <QProgressDialog>

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
class PDFAction;
class PDFWidget;
class PDFDocument;
class PDFOptionalContentTreeItemModel;
}

namespace pdfviewer
{
class PDFSidebarWidget;
class PDFAdvancedFindWidget;

class PDFViewerMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PDFViewerMainWindow(QWidget *parent = nullptr);
    virtual ~PDFViewerMainWindow() override;

    virtual void closeEvent(QCloseEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

signals:
    void queryPasswordRequest(QString* password, bool* ok);

private slots:
    void onQueryPasswordRequest(QString* password, bool* ok);

    void on_actionPageLayoutSinglePage_triggered();
    void on_actionPageLayoutContinuous_triggered();
    void on_actionPageLayoutTwoPages_triggered();
    void on_actionPageLayoutTwoColumns_triggered();
    void on_actionFirstPageOnRightSide_triggered();

    void on_actionRendering_Errors_triggered();
    void on_actionOptions_triggered();
    void on_actionAbout_triggered();
    void on_actionFitPage_triggered();
    void on_actionFitWidth_triggered();
    void on_actionFitHeight_triggered();
    void on_actionProperties_triggered();
    void on_actionSend_by_E_Mail_triggered();
    void on_actionRotateRight_triggered();
    void on_actionRotateLeft_triggered();
    void on_actionPrint_triggered();
    void on_actionRender_to_Images_triggered();

    void on_actionOptimize_triggered();

    void on_actionSave_As_triggered();

    void on_actionSave_triggered();

private:
    void onActionOpenTriggered();
    void onActionCloseTriggered();
    void onActionQuitTriggered();
    void onPageRenderingErrorsChanged(pdf::PDFInteger pageIndex, int errorsCount);
    void onDrawSpaceChanged();
    void onPageLayoutChanged();
    void onPageNumberSpinboxEditingFinished();
    void onPageZoomSpinboxEditingFinished();
    void onActionTriggered(const pdf::PDFAction* action);

    void onProgressStarted(pdf::ProgressStartupInfo info);
    void onProgressStep(int percentage);
    void onProgressFinished();

    void onDocumentReadingFinished();
    void onDocumentModified(pdf::PDFModifiedDocument document);
    void onDocumentUndoRedo(pdf::PDFModifiedDocument document);

    void readSettings();
    void readActionSettings();
    void writeSettings();

    void updateTitle();
    void updatePageLayoutActions();
    void updateRenderingOptionActions();
    void updateUI(bool fullUpdate);
    void updateActionsAvailability();
    void updateMagnifierToolSettings();
    void updateUndoRedoSettings();
    void updateUndoRedoActions();

    void onViewerSettingsChanged();
    void onRenderingOptionTriggered(bool checked);

    void openDocument(const QString& fileName);
    void setDocument(pdf::PDFModifiedDocument document);
    void closeDocument();
    void saveDocument(const QString& fileName);

    void setPageLayout(pdf::PageLayout pageLayout);
    void updateFileInfo(const QString& fileName);

    std::vector<QAction*> getRenderingOptionActions() const;
    QList<QAction*> getActions() const;

    int adjustDpiX(int value);

    struct AsyncReadingResult
    {
        pdf::PDFDocumentPointer document;
        QString errorMessage;
        pdf::PDFDocumentReader::Result result = pdf::PDFDocumentReader::Result::Cancelled;
        std::vector<pdf::PDFSignatureVerificationResult> signatures;
    };

    Ui::PDFViewerMainWindow* ui;
    pdf::PDFCMSManager* m_CMSManager;
    PDFRecentFileManager* m_recentFileManager;
    PDFViewerSettings* m_settings;
    pdf::PDFWidget* m_pdfWidget;
    pdf::PDFDocumentPointer m_pdfDocument;
    PDFSidebarWidget* m_sidebarWidget;
    QDockWidget* m_sidebarDockWidget;
    PDFAdvancedFindWidget* m_advancedFindWidget;
    QDockWidget* m_advancedFindDockWidget;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;
    QSpinBox* m_pageNumberSpinBox;
    QLabel* m_pageNumberLabel;
    QDoubleSpinBox* m_pageZoomSpinBox;
    bool m_isLoadingUI;
    pdf::PDFProgress* m_progress;
    QWinTaskbarButton* m_taskbarButton;
    QWinTaskbarProgress* m_progressTaskbarIndicator;
    PDFFileInfo m_fileInfo;
    std::vector<pdf::PDFSignatureVerificationResult> m_signatures;

    QFuture<AsyncReadingResult> m_future;
    QFutureWatcher<AsyncReadingResult>* m_futureWatcher;

    QProgressDialog* m_progressDialog;
    bool m_isBusy;
    bool m_isChangingProgressStep;

    pdf::PDFToolManager* m_toolManager;
    pdf::PDFWidgetAnnotationManager* m_annotationManager;
    pdf::PDFFormManager* m_formManager;
    PDFTextToSpeech* m_textToSpeech;
    PDFUndoRedoManager* m_undoRedoManager;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H
