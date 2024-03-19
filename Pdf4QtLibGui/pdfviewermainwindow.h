//    Copyright (C) 2020-2024 Jakub Melka
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
#include "pdfwintaskbarprogress.h"

#include <QFuture>
#include <QTreeView>
#include <QMainWindow>
#include <QFutureWatcher>
#include <QProgressBar>
#include <QLabel>

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

class PDF4QTLIBGUILIBSHARED_EXPORT PDFViewerMainWindow : public QMainWindow, public IMainWindow
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
    QSpinBox* m_pageNumberSpinBox;
    QLabel* m_pageNumberLabel;
    QDoubleSpinBox* m_pageZoomSpinBox;
    bool m_isLoadingUI;
    pdf::PDFProgress* m_progress;
    PDFWinTaskBarProgress* m_progressTaskbarIndicator;

    QProgressBar* m_progressBarOnStatusBar;
    QLabel* m_progressBarLeftLabelOnStatusBar;
    bool m_isChangingProgressStep;
};

}   // namespace pdfviewer

#endif // PDFVIEWERMAINWINDOW_H
