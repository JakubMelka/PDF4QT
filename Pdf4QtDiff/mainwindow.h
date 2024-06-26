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

#ifndef PDFDIFF_MAINWINDOW_H
#define PDFDIFF_MAINWINDOW_H

#include "utils.h"

#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdfcms.h"
#include "pdfoptionalcontent.h"

#include <QMainWindow>
#include <QSignalMapper>
#ifdef WIN_TASKBAR_BUTTON
#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>
#endif

namespace Ui
{
class MainWindow;
}

namespace pdfdiff
{

class SettingsDockWidget;
class DifferencesDockWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    using BaseClass = QMainWindow;

public:
    explicit MainWindow(QWidget* parent);
    virtual ~MainWindow() override;

    enum class Operation
    {
        OpenLeft,
        OpenRight,
        Compare,
        Close,
        GetSource,
        BecomeSponsor,
        About,
        PreviousDifference,
        NextDifference,
        CreateCompareReport,
        FilterText,
        FilterVectorGraphics,
        FilterImages,
        FilterShading,
        FilterPageMovement,
        ViewDifferences,
        ViewLeft,
        ViewRight,
        ViewOverlay,
        ShowPageswithDifferences,
        SaveDifferencesToXML,
        DisplayDifferences,
        DisplayMarkers
    };

    virtual void showEvent(QShowEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;

    void setLeftDocument(pdf::PDFDocument&& newLeftDocument);
    void setRightDocument(pdf::PDFDocument&& newRightDocument);

    void updateViewDocument();

    bool canPerformOperation(Operation operation) const;
    void performOperation(Operation operation);

private slots:
    void updateActions();
    void onSelectionChanged(size_t currentIndex);

private:
    void onMappedActionTriggered(int actionId);
    void onComparationFinished();
    void onColorsChanged();

    void onProgressStarted(pdf::ProgressStartupInfo info);
    void onProgressStep(int percentage);
    void onProgressFinished();

    void loadSettings();
    void saveSettings();

    void setViewDocument(pdf::PDFDocument* document, bool updateCustomPageLayout);

    ComparedDocumentMapper::Mode getDocumentViewMode() const;

    /// Clears all data, and possibly documents also.
    /// View document is set to nullptr.
    /// \param clearLeftDocument Clear left document?
    /// \param clearRightDocument Clear left document?
    void clear(bool clearLeftDocument, bool clearRightDocument);

    void updateAll(bool resetFilters);
    void updateFilteredResult();
    void updateCustomPageLayout();
    void updateOverlayTransparency();

    std::optional<pdf::PDFDocument> openDocument();

    Ui::MainWindow* ui;

    pdf::PDFProgress* m_progress;
#ifdef WIN_TASKBAR_BUTTON
    QWinTaskbarButton* m_taskbarButton;
    QWinTaskbarProgress* m_progressTaskbarIndicator;
#endif
    pdf::PDFCMSManager* m_cmsManager;
    pdf::PDFWidget* m_pdfWidget;
    SettingsDockWidget* m_settingsDockWidget;
    DifferencesDockWidget* m_differencesDockWidget;
    pdf::PDFOptionalContentActivity* m_optionalContentActivity;

    Settings m_settings;
    QSignalMapper m_mapper;
    pdf::PDFDiff m_diff;
    bool m_isChangingProgressStep;
    bool m_dontDisplayErrorMessage;

    pdf::PDFDocument m_leftDocument;
    pdf::PDFDocument m_rightDocument;
    pdf::PDFDocument m_combinedDocument;

    pdf::PDFDiffResult m_diffResult;
    pdf::PDFDiffResult m_filteredDiffResult; ///< Difference result with filters applied
    pdf::PDFDiffResultNavigator m_diffNavigator; ///< Difference navigator
    ComparedDocumentMapper m_documentMapper;
    DifferencesDrawInterface m_drawInterface;
};

}   // namespace pdfdiff

#endif // PDFDIFF_MAINWINDOW_H
