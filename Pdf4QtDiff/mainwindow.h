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
