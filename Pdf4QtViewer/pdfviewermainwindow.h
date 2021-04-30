//    Copyright (C) 2019-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFVIEWERMAINWINDOW_H
#define PDFVIEWERMAINWINDOW_H

#include "pdfviewerglobal.h"
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
#include "pdfplugin.h"
#include "pdfprogramcontroller.h"

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

class Pdf4QtVIEWERLIBSHARED_EXPORT PDFViewerMainWindow : public QMainWindow, public IMainWindow
{
    Q_OBJECT

public:
    explicit PDFViewerMainWindow(QWidget *parent = nullptr);
    virtual ~PDFViewerMainWindow() override;

    virtual void closeEvent(QCloseEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;

    PDFProgramController* getProgramController() const { return m_programController; }

    virtual void updateUI(bool fullUpdate) override;
    virtual QMenu* addToolMenu(QString name) override;
    virtual void setStatusBarMessage(QString message, int time) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual void adjustToolbar(QToolBar* toolbar) override;
    virtual pdf::PDFTextSelection getSelectedText() const override;

protected:
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    void onActionQuitTriggered();

    void onPageNumberSpinboxEditingFinished();
    void onPageZoomSpinboxEditingFinished();

    void onProgressStarted(pdf::ProgressStartupInfo info);
    void onProgressStep(int percentage);
    void onProgressFinished();

    QIcon createStickyNoteIcon(QString key) const;

    Ui::PDFViewerMainWindow* ui;
    PDFActionManager* m_actionManager;
    PDFProgramController* m_programController;

    PDFSidebarWidget* m_sidebarWidget;
    QDockWidget* m_sidebarDockWidget;
    PDFAdvancedFindWidget* m_advancedFindWidget;
    QDockWidget* m_advancedFindDockWidget;
    QSpinBox* m_pageNumberSpinBox;
    QLabel* m_pageNumberLabel;
    QDoubleSpinBox* m_pageZoomSpinBox;
    bool m_isLoadingUI;
    pdf::PDFProgress* m_progress;
    QWinTaskbarButton* m_taskbarButton;
    QWinTaskbarProgress* m_progressTaskbarIndicator;

    QProgressDialog* m_progressDialog;
    bool m_isChangingProgressStep;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H
