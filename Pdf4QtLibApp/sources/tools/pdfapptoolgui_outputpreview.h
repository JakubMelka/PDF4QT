// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef PDFAPPTOOLGUI_OUTPUTPREVIEW_H
#define PDFAPPTOOLGUI_OUTPUTPREVIEW_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappoutputpreviewcanvas.h"
#include "pdfapptoolgui_softproof.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"

#include "pdfcolorpickerdialog.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfoutputpreviewanalyzer.h"

#include <QButtonGroup>
#include <QColor>
#include <QPointer>
#include <QStandardItemModel>
#include <QWidget>

#include <map>
#include <memory>

class QButtonGroup;
class QToolButton;
class QStandardItemModel;
class QStandardItem;
class QTimer;

namespace pdf
{

class PDFAppOutputPreviewCanvas;
class PDFAppOutputPreviewControl;
class PDFAppParagraph;
class PDFAppProofColorField;
class PDFAppToolCard;
class PDFAppToolCallout;
class PDFAppToolPanelHeader;
class PDFAppToolPanelPopup;
class PDFAppToolProgressCard;
class PDFAppDocumentView;
class PDFColorPickerDialog;
class PDFKeyValueList;
class PDFLabel;
class PDFTreeView;

/// The sample of Under the cursor (card 474:8643): the color of the page under the pointer (44 × 30, radius 6,
/// border/default), "Sample color" Medium 12 and "Total coverage 248 %" Regular 11 text/tertiary
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutputPreviewSample : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppOutputPreviewSample(QWidget* parent);
    virtual ~PDFAppOutputPreviewSample() override;

    /// An invalid color = nothing under the pointer
    void setSample(const QColor& color, const QString& coverageText);

    const QColor& getColor() const { return m_color; }
    PDFLabel* getCoverageLabel() const { return m_coverage.data(); }

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QColor m_color;
    QPointer<PDFLabel> m_coverage;
};

/// The legend of the map Ink Coverage (Distribution 474:10536): the bar of the colors of PDFColorScale from no
/// ink to the highest coverage of the page (at least 100 %), five values, the marker of the value at the cursor
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutputPreviewLegend : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppOutputPreviewLegend(QWidget* parent);
    virtual ~PDFAppOutputPreviewLegend() override;

    /// \param maximum Highest coverage of the page in %
    /// \param cursorValue Coverage at the cursor in %, negative = none
    void setValues(double maximum, double cursorValue);

    /// The top of the scale (the maximum of the scale of the core)
    double getScaleMaximum() const;

    /// The text of the label \p index (0 = the top) and of the marker (tests)
    QString getLabelText(int index) const;
    QString getMarkerText() const;

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    double m_maximum = 100.0;
    double m_cursorValue = -1.0;
};

/// The result of a limit of Output Preview (Coverage warning 474:10097, Rich black warning 474:10178): a band
/// bg/subtle (radius 6, padding 10 / 8) with the rows "Within the limit" (a dot of the green accent) and "Over the
/// limit" (a dot of the alarm color) and their areas, rows of 15 px 6 apart (RS7FIGMA). Painted by the tokens of
/// the theme; a screen reader reads the rows (the accessible description). Empty = no rows, no band.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppOutputPreviewLimitRows : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING_X = 10;
    static constexpr int PADDING_Y = 8;
    static constexpr int ROW_HEIGHT = 15;
    static constexpr int ROW_GAP = 6;
    static constexpr int DOT_SIZE = 10;
    static constexpr int DOT_GAP = 8;

    explicit PDFAppOutputPreviewLimitRows(QWidget* parent);
    virtual ~PDFAppOutputPreviewLimitRows() override;

    /// The areas of the two rows; empty texts = no result (the widget is empty)
    void setValues(const QString& withinValue, const QString& overValue, const QColor& alarmColor);
    void clear();

    bool isEmpty() const { return m_withinValue.isEmpty() && m_overValue.isEmpty(); }
    const QColor& getAlarmColor() const { return m_alarmColor; }

    /// The texts of the rows (tests)
    static QString getWithinTitle();
    static QString getOverTitle();
    const QString& getWithinValue() const { return m_withinValue; }
    const QString& getOverValue() const { return m_overValue; }

    /// The rectangle of the dot of the row \p row (0 = within, 1 = over) in the widget
    QRect getDotRect(int row) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QString m_withinValue;
    QString m_overValue;
    QColor m_alarmColor;
};

/// The table of the inks of Output Preview (ink-table 474:8521, Library rows): the rows of the groups Process
/// Inks / Spot Inks lie on a band bg/subtle (474:9958, RS7FIGMA), the inks on the card
class PDF4QTLIBAPPSHARED_EXPORT PDFAppInkTreeView : public PDFTreeView
{
    Q_OBJECT

private:
    using BaseClass = PDFTreeView;

public:
    explicit PDFAppInkTreeView(QWidget* parent);
    virtual ~PDFAppInkTreeView() override;

    /// The row of \p index is a row of a group (it has inks below it)
    bool isGroupRow(const QModelIndex& index) const;

protected:
    virtual void drawRow(QPainter* painter, const QStyleOptionViewItem& options, const QModelIndex& index) const override;
};

/// The paper of Paper Color (476:9987): "Paper" with the name of the color, the swatches White, Cream, Gray and
/// "+" for a custom color (the color picker, K7.19: the RGB fields only in that dialog). A choice of the user
/// emits colorSelected; setColor never emits.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPaperColorField : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppPaperColorField(QWidget* parent);
    virtual ~PDFAppPaperColorField() override;

    void setColor(const QColor& color);
    const QColor& getColor() const { return m_color; }

    /// The presets of the paper (canonical values, written into the parameter) and their names
    static std::vector<std::pair<QColor, QString>> getPresets();

    /// The name of a color of the paper ("Cream", or the hexadecimal value)
    static QString getColorName(const QColor& color);

    PDFLabel* getNameLabel() const { return m_name.data(); }
    QButtonGroup* getSwatchGroup() const { return m_group.data(); }
    PDFColorPickerDialog* getPickerDialog() const { return m_dialog.data(); }

Q_SIGNALS:
    void colorSelected(const QColor& color);

private:
    void onSwatchClicked();
    void onCustomClicked();
    void onDialogColorSelected(const QColor& color);

    QColor m_color;
    QPointer<PDFLabel> m_name;
    QPointer<QButtonGroup> m_group;
    QPointer<PDFColorPickerDialog> m_dialog;
};

/// The GUI of Output Preview (plan of step 7 of phase 2, 4.6; Figma 474:8386 … 476:11703): only Close (10.3 point
/// 34); the toolbar the mode (474:8728), Content (497:19166) and the switches Simulate Separations and Paper Color
/// (labeled Toolbar menu buttons, Open = on, as the toggles of Redact - owner 17. 11.); the left panel Inks (a tree
/// with the three states of the groups, K7.18); the canvas PDFAppOutputPreviewCanvas; the right panel Under the
/// cursor and the section of the mode (Preview with the row Overprint - K7.12, Paper color, Coverage warning, Rich
/// black warning, Distribution, the channels). One job renders a page (PDFOutputPreviewAnalyzer::render in
/// QtConcurrent::run, a new request cancels the old one, the result is taken only from the last request; plan 6.3)
/// - never on the GUI thread; the picture of a mode and the values under the cursor come from the stored result.
/// After a change of the document or of the shown page the page is rendered again.
///
/// Ownership: the shell owns this GUI and its parts; the model of the inks and the watcher of the job are its
/// children; the job holds only copies (the document by its shared pointer, the settings, the cancellation) and
/// the result is dropped, when the GUI is gone.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIOutputPreview : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUIOutputPreview(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIOutputPreview() override;

    /// Registers the core tool and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerOutputPreviewTools();

    /// Rest after a change before the page is rendered (ms; a burst of changes renders once)
    static constexpr int RENDER_DELAY = 60;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getStatusText() const override;
    virtual QString getCancelText() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;
    virtual bool hasOwnStatusItems() const override { return true; }    ///< K7.27: the zoom of the picture, not of the view
    virtual bool isConfirmShown() const override;

    /// The session is the use (owner 2026-11-20): a mode, an ink, a simulation, a color, or 10 s shown
    virtual bool isInteractive() const override { return true; }
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;

    using Mode = PDFOutputPreviewAnalyzer::Mode;

    /// The mode of the parameter
    Mode getMode() const;

    /// The shown page (from 0, -1 = none)
    PDFInteger getPageIndex() const;

    /// The result shown (nullptr before the first one)
    const PDFOutputPreviewResult* getResult() const { return m_result.get(); }

    /// A rendering is running
    bool isRendering() const { return m_watcher != nullptr; }

    /// A rendering is running or waits for its delay
    bool isRenderingPending() const;

    /// The color of the warnings (the alarm color of Coverage warning and Rich black warning)
    const QColor& getWarningColor() const { return m_warningColor; }

    /// The pixel under the pointer (invalid = none) as the canvas reports it
    void setHoveredPixel(const QPoint& pixel);

    /// Parts (tests; nullptr = none)
    PDFAppOutputPreviewCanvas* getCanvas() const { return m_canvas.data(); }
    QStandardItemModel* getInkModel() const { return m_inkModel.data(); }
    PDFTreeView* getInkView() const { return m_inkView.data(); }
    PDFAppToolPanelHeader* getInksHeader() const { return m_inksHeader.data(); }
    PDFAppToolCallout* getSpotLimitCallout() const { return m_spotCallout.data(); }
    QToolButton* getSimulateButton() const { return m_simulateButton.data(); }
    QToolButton* getPaperButton() const { return m_paperButton.data(); }
    PDFAppToolPanelPopup* getModePanel() const { return m_modePanel.data(); }
    PDFKeyValueList* getPreviewRows() const { return m_previewRows.data(); }
    PDFAppOutputPreviewSample* getSample() const { return m_sample.data(); }
    PDFAppOutputPreviewLegend* getLegend() const { return m_legend.data(); }
    PDFAppOutputPreviewLimitRows* getCoverageRows() const { return m_coverageRows.data(); }
    PDFAppOutputPreviewLimitRows* getRichBlackRows() const { return m_richBlackRows.data(); }
    PDFAppPaperColorField* getPaperField() const { return m_paperField.data(); }
    PDFAppToolProgressCard* getProgressCard() const { return m_progressCard.data(); }
    QWidget* getSection(const QString& name) const;

    /// The names of the inks, which are unchecked (the parameter inactiveInks)
    QStringList getInactiveInks() const;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void scheduleRender();
    void startRender();
    void cancelRender();
    void onRenderTimeout();
    void onRenderFinished();
    void onModeRowClicked();
    void onSimulateClicked();
    void onPaperClicked();
    void onPaperColorSelected(const QColor& color);
    void onWarningColorSelected(const QColor& color);
    void onInkItemChanged(QStandardItem* item);
    void onDisplayedPageChanged(pdf::PDFInteger pageIndex);
    void onPixelHovered(const QPoint& pixel);
    void onPageStepRequested(int step);
    void onImageRectChanged();
    void updateImage();
    void updateInks();
    void updateParts();
    void updateCursor();
    PDFAppDocumentView* getView() const;
    QWidget* createSection(const QString& name, const QString& title, QWidget* parent, PDFAppToolCard** card);
    QString getModeTitle(Mode mode) const;
    QString getModeIcon(Mode mode) const;
    QString getSinglePlateName() const;

    std::shared_ptr<const PDFOutputPreviewResult> m_result;
    PDFOutputPreviewSettings m_settings;            ///< The settings of the shown result
    QImage m_image;                                 ///< The picture of the mode
    QColor m_warningColor = Qt::red;
    QPoint m_hoveredPixel = QPoint(-1, -1);
    std::vector<double> m_cursorValues;
    bool m_isUpdatingInks = false;

    QPointer<QObject> m_watcher;                    ///< The watcher of the running job (nullptr = none)
    std::shared_ptr<PDFAppOutputPreviewControl> m_control;
    PDFInteger m_renderingPage = -1;
    QPointer<QTimer> m_renderTimer;

    QPointer<PDFAppOutputPreviewCanvas> m_canvas;
    QPointer<QStandardItemModel> m_inkModel;
    QPointer<PDFTreeView> m_inkView;
    QPointer<PDFAppToolPanelHeader> m_inksHeader;
    QPointer<PDFAppParagraph> m_inksHint;
    QPointer<PDFAppToolCallout> m_spotCallout;
    QPointer<QToolButton> m_simulateButton;
    QPointer<QToolButton> m_paperButton;
    QPointer<PDFAppToolPanelPopup> m_modePanel;
    QPointer<PDFAppToolProgressCard> m_progressCard;
    QPointer<PDFAppOutputPreviewSample> m_sample;
    QPointer<PDFKeyValueList> m_previewRows;
    QPointer<PDFAppPaperColorField> m_paperField;
    QPointer<PDFAppOutputPreviewLimitRows> m_coverageRows;
    QPointer<PDFAppOutputPreviewLimitRows> m_richBlackRows;
    QPointer<PDFAppProofColorField> m_coverageAlarm;
    QPointer<PDFAppProofColorField> m_richBlackAlarm;
    QPointer<PDFAppOutputPreviewLegend> m_legend;
    QPointer<PDFAppParagraph> m_channelText;
    QPointer<PDFLabel> m_channelTitle;
    std::map<QString, QPointer<QWidget>> m_sections;
    QPointer<PDFAppDocumentView> m_connectedView;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_OUTPUTPREVIEW_H
