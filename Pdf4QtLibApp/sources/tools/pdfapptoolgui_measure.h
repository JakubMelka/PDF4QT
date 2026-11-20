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

#ifndef PDFAPPTOOLGUI_MEASURE_H
#define PDFAPPTOOLGUI_MEASURE_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappglobal.h"
#include "pdfappmeasuredialogs.h"
#include "pdfappmeasureoverlay.h"
#include "pdfapptoolpanelbuilder.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfdocumenttool_measure.h"
#include "pdfmeasure.h"
#include "pdfmeasurewriter.h"
#include "pdfobject.h"

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QToolButton>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QPushButton;
class QToolButton;

namespace pdf
{

class PDFAppAccessibleCheck;
class PDFAppCancelFlag;
class PDFAppMeasurePresetsDialog;
class PDFAppMeasureScaleDialog;
class PDFAppParagraph;
class PDFAppSettingControl;
class PDFAppToolCallout;
class PDFAppToolCard;
class PDFAppToolEnumMenu;
class PDFAppToolPanelHeader;
class PDFAppToolPanelPopup;
class PDFColorSwatchRow;
class PDFComboBox;
class PDFLabel;
class PDFListView;
class PDFMessageBox;
class PDFTextLink;
class PDFTileRow;

/// A temporary measurement of the GUI (lost with the tool, 471:5975)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasurement
{
public:
    quint64 id = 0;                                             ///< Unique in the GUI
    PDFInteger pageIndex = -1;                                  ///< The page of its first point
    PDFAppMeasureDrawKind drawKind = PDFAppMeasureDrawKind::Linear;
    PDFMeasureKind kind = PDFMeasureKind::Distance;
    std::vector<QPointF> points;                                ///< Normalized points in the space of the page
};

/// The measurement annotations of a document read by the job of PDFAppToolGUIMeasure (a plain value)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureAnnotationRead
{
public:
    std::vector<PDFMeasureAnnotationInfo> annotations;
    bool isTruncated = false;   ///< The document has more (PDFMeasureAnnotationInfo::readPages)
};

/// The list of the measurements (471:6681): as high as its rows, so the hint of the temporary measurements lies
/// right under them; a longer list scrolls in the room of the panel
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureListView : public PDFAppPanelListView
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPanelListView;

public:
    explicit PDFAppMeasureListView(QWidget* parent);
    virtual ~PDFAppMeasureListView() override;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
};

/// The rows of the list of the measurements (471:5975, 471:7947): the value ("11.20 m"), "Length · page 1 · 1:50",
/// the icon of the kind; the × of a temporary measurement, the tag Annotation of a measurement annotation (also
/// of another producer, FM-4); the selected row has the accent tile (FM-28)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureListModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    class Row
    {
    public:
        bool isAnnotation = false;
        quint64 id = 0;                     ///< A temporary measurement
        PDFObjectReference annotation;      ///< A measurement annotation
        PDFInteger pageIndex = -1;
        QString title;
        QString description;
        QString iconName;
    };

    explicit PDFAppMeasureListModel(QObject* parent);
    virtual ~PDFAppMeasureListModel() override;

    void setRows(std::vector<Row> rows);
    const std::vector<Row>& getRows() const { return m_rows; }
    const Row* getRow(int row) const;

    /// The row of a temporary measurement / of an annotation, -1 = none
    int findTemporary(quint64 id) const;
    int findAnnotation(PDFObjectReference annotation) const;

    /// The selected row has the accent tile (-1 = none)
    void setSelectedRow(int row);
    int getSelectedRow() const { return m_selectedRow; }

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    std::vector<Row> m_rows;
    int m_selectedRow = -1;
};

/// The rows of the measurements: PDFTileRowDelegate (ListItem) with Remove at the × of a temporary measurement
/// and Delete on the current row
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureListDelegate : public PDFTileRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFTileRowDelegate;

public:
    enum MeasureActionId
    {
        RemoveAction = LinkAction + 1   ///< The × of a temporary measurement
    };

    explicit PDFAppMeasureListDelegate(QObject* parent);
    virtual ~PDFAppMeasureListDelegate() override;

    virtual QList<Action> getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

/// The GUI of Measure (review.measure, Figma 471:5490 … 473:91785; plan of step 7 of phase 2, 2.2, 4.8 with
/// K7.5 - K7.8, R7-12, owner answers Q7.4, Q7.5, Q7.20, FM-9 cancelled): in the document tab - the toolbar (the
/// 8 kinds in three groups, Scale, Temporary / Annotations in the document, Show, the big cross, Clear, Export
/// CSV; Save as Annotations), the list of the measurements of every page (× of a temporary one, the label
/// Annotation of an existing one - also of another producer, FM-4; a selection highlights it on the canvas,
/// FM-28), the panels Scale, Units and Labels, the drawing by PDFAppMeasureOverlay with PDFSnapper, the dialogs of
/// PDFAppMeasureDialogs (new + QPointer). Temporary measurements may lie on different pages (FM-9 cancelled), each
/// one on one page; they always use the current scale (471:7608), a measurement inside a viewport of a page with
/// a measure the scale of the viewport (12.9.2). Annotations in the document writes each measurement at once (one
/// step of Undo); Save as Annotations writes the temporary ones in one step (Undo takes the annotations and their
/// rows back, FM-10); a locked document, a document without bit 6 (the owner password offered, FM-2) and a
/// certified document of P = 1 / 2 (Table 257) have only the temporary measurements and the CSV. The scales come
/// from PDFMeasureScaleRepository (never written into the document, Q7.4).
///
/// Esc (owner 2026-11-17, the order of Redact): the points of an unfinished drawing (the overlay is the drag
/// source of the shell) → the drawing mode (or Calibrate) → the selected row → the tool.
///
/// The measurement annotations of the document are read in one asynchronous job (review RS7CCORE L9; the pattern of
/// Compare): QtConcurrent::run on the immutable document of the tab (a copy of its shared pointer), a cancellation
/// flag by a shared pointer, the watcher a child of this GUI, a generation - a new version of the document cancels
/// the old read and only the result of the last request is taken; the list keeps the annotations read before until
/// then. The export uses the annotations of the list (never a second read in the GUI thread).
///
/// Ownership: the shell owns this GUI; the list model and the watcher of the read are its children; the parts of the
/// panels are children of the widgets of the shell (QPointer here); the overlay is the shell's (QPointer); the dialogs
/// are new + QPointer with WA_DeleteOnClose, closed by the destructor (the calibration has its own dialog, review
/// RS7CAPP L3: an open Custom / Default scale dialog never takes a picked calibration). Every commit, dialog and
/// emission is followed by a check of this GUI (a slot of the document may close the tab); the job holds only copies.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIMeasure : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// Where the scale of the document comes from (the subtitle of the card Scale, 473:9500, 473:89712)
    enum class ScaleSource
    {
        Default,        ///< The default scale (473:92070)
        Remembered,     ///< Remembered for the document (PDFMeasureScaleRepository)
        Document,       ///< The one scale of the viewports of the document (473:9500 "From the document")
        Session         ///< Chosen, not remembered (Remember is off, or the document cannot be identified)
    };

    /// The result of Save as Annotations, said by the callout and the status bar until the next change
    enum class Notice
    {
        None,
        Converted,          ///< 473:90043: the temporary measurements were saved as annotations
        PartiallyConverted  ///< 473:90376: some could not be stored
    };

    /// The callouts of the right panel (one at a time, the first one of this order which applies)
    enum class Callout
    {
        Partial,        ///< 473:90376 amber: some measurements could not be stored
        Converted,      ///< 473:90043 green: saved as annotations
        Refusal,        ///< 473:90704 amber: no new annotations (bit 6 with Enter Owner Password…, locked, certified)
        NotRemembered,  ///< 473:89712 amber: the document cannot be identified
        Annotations     ///< 471:7947 blue: the measurements go straight into the document
    };

    /// Widths of the panels of the toolbar: the kinds (471:6294: 320), the scales (471:6943: 330)
    static constexpr int KIND_PANEL_WIDTH = 320;
    static constexpr int SCALE_PANEL_WIDTH = 330;
    static constexpr int STORAGE_PANEL_WIDTH = 380;

    explicit PDFAppToolGUIMeasure(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIMeasure() override;

    /// Registers the tool review.measure and this GUI (PDFAppBuiltinTools::registerAll)
    static void registerMeasureTools();

    // ------------------------------------------------------------- state

    const std::vector<PDFAppMeasurement>& getMeasurements() const { return m_measurements; }
    const std::vector<PDFMeasureAnnotationInfo>& getAnnotations() const { return m_annotations; }

    /// The measurement annotations of the document are being read (the job of the last version runs)
    bool isReadingAnnotations() const { return !m_readWatcher.isNull(); }

    /// The drawing kind of the panel (the parameter "kind")
    PDFAppMeasureDrawKind getDrawKind() const;

    /// Chooses the drawing kind (one step of Undo of the parameters); the drawing mode is on
    void setDrawKind(PDFAppMeasureDrawKind kind);

    /// The drawing mode is on (Esc turns it off, a kind of the menu on)
    bool isDrawing() const { return m_isDrawing; }
    bool isCalibrating() const { return m_isCalibrating; }

    /// The scale of now and where it comes from
    const PDFMeasureScale& getScale() const { return m_scale; }
    ScaleSource getScaleSource() const { return m_scaleSource; }

    /// Chooses the scale (a preset, Custom, Calibrate): remembered for the document, when Remember is on and the
    /// document can be identified (473:89712)
    void setScale(const PDFMeasureScale& scale);

    /// Starts Calibrate Scale… (471:7363): two points of a known distance
    void startCalibration();

    /// The document can be identified (a file and /ID[0], 473:89712)
    bool isDocumentIdentifiable() const;

    /// The units of the parameters
    PDFMeasureDisplayUnits getUnits() const;

    /// Adds a measurement of a page (the overlay does it; also the tests): temporary, or an annotation at once in
    /// the storage Annotations (when it can be written). \returns the measurement was added
    bool addMeasurement(PDFInteger pageIndex, PDFMeasureKind kind, const std::vector<QPointF>& points, PDFAppMeasureDrawKind drawKind);

    /// Removes a temporary measurement (its ×, Delete)
    bool removeMeasurement(quint64 id);

    /// Clear of the toolbar: every temporary measurement goes (the annotations stay - they are the document's)
    void clearMeasurements();

    /// Save as Annotations (the Primary button): the temporary measurements become measurement annotations in one
    /// step of Undo; those, which cannot be written, stay temporary (473:90376). \returns some were written
    bool saveAsAnnotations();

    /// The measurement annotations can be written now: an editable document, bit 6 (or the owner password given),
    /// a certification of P = 3 or none (Table 257)
    bool canWriteAnnotations() const;

    /// The reason, why the annotations cannot be written (empty = they can)
    QString getWriteRefusalText() const;

    /// Selects a row (the temporary measurement or the annotation); -1 = none
    void selectRow(int row);
    int getSelectedRow() const;

    Notice getNotice() const { return m_notice; }

    /// Export CSV (473:91038): the dialog of the export of the data of the list (never a run of the tool)
    bool exportCsv();

    /// The data of the list (PDFDocumentToolMeasure::createData of every page with the temporary measurements)
    QJsonObject createData() const;

    /// The status text without its Esc hint
    QString getStatusTextWithoutEscape() const;

    // ------------------------------------------------------------- parts (nullptr = none)

    QToolButton* getKindButton() const;
    QToolButton* getScaleButton() const;
    QToolButton* getStorageButton() const;
    QToolButton* getShowButton() const { return m_showButton.data(); }
    QToolButton* getCrossButton() const { return m_crossButton.data(); }
    QToolButton* getClearButton() const { return m_clearButton.data(); }
    QToolButton* getExportButton() const { return m_exportButton.data(); }
    PDFTileRow* getKindRow(PDFAppMeasureDrawKind kind) const;
    PDFAppToolPanelPopup* getScalePanel() const { return m_scalePanel.data(); }
    PDFAppMeasureListModel* getListModel() const { return m_listModel.data(); }
    PDFListView* getListView() const { return m_listView.data(); }
    PDFAppToolPanelHeader* getLeftHeader() const { return m_leftHeader.data(); }
    QWidget* getEmptyCard() const { return m_emptyCard.data(); }
    PDFAppParagraph* getListHint() const { return m_listHint.data(); }
    PDFAppToolCallout* getCallout(Callout callout) const;

    /// The shown callout, std::nullopt = none
    std::optional<Callout> getShownCallout() const;
    PDFTextLink* getOwnerPasswordLink() const { return m_ownerPasswordLink.data(); }
    PDFLabel* getScaleRatioLabel() const { return m_scaleRatio.data(); }
    PDFLabel* getScaleSubtitleLabel() const { return m_scaleSubtitle.data(); }
    PDFLabel* getDefaultScaleLabel() const { return m_defaultScale.data(); }
    PDFComboBox* getFontBox() const { return m_fontBox.data(); }
    PDFColorSwatchRow* getTextColorRow() const { return m_textColorRow.data(); }
    PDFColorSwatchRow* getBackgroundColorRow() const { return m_backgroundColorRow.data(); }
    PDFAppMeasureOverlay* getOverlay() const { return m_overlay.data(); }

    // ------------------------------------------------------------- dialogs

    /// Custom Scale… (473:7024), the default scale (473:92070, Change… of the card), Manage Presets… (473:7412)
    PDFAppMeasureScaleDialog* openCustomScaleDialog();
    PDFAppMeasureScaleDialog* openDefaultScaleDialog();
    PDFAppMeasurePresetsDialog* openPresetsDialog();

    /// The dialog of the calibration of a picked length (473:6438), the zero length (473:6755). Its own dialog: a
    /// Custom / Default scale dialog open at the same time stays as it is (review RS7CAPP L3). Cancel ends the
    /// calibration, as Cancel of 473:6755 does (Calibrate Scale… starts it again; review RS7CAPP L5)
    PDFAppMeasureScaleDialog* openCalibrationDialog(PDFReal length);
    PDFAppMeasureScaleDialog* getScaleDialog() const { return m_scaleDialog.data(); }
    PDFAppMeasureScaleDialog* getCalibrationDialog() const { return m_calibrationDialog.data(); }
    PDFAppMeasurePresetsDialog* getPresetsDialog() const { return m_presetsDialog.data(); }
    PDFMessageBox* getZeroLengthDialog() const { return m_zeroLengthDialog.data(); }

    // ------------------------------------------------------------- PDFDocumentToolGUI

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual QString getCancelText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual bool confirmWithoutRun() override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;
    virtual bool handleEscape() override;
    virtual QString getEscapeModeName() const override;
    virtual QString getEscapeModeHint() const override;
    virtual bool hasDocumentUndoSteps() const override;
    virtual bool isResultStaleBy(const QString& parameterName) const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// Starts the read of the measurement annotations of the document in its job (the entry, a new version); the
    /// temporary measurements of the pages, which are gone, go at once
    void refreshDocument();

    /// The read of the job of the last request is done
    void onAnnotationsRead();

    /// The job of the read is canceled and forgotten (its late result goes nowhere)
    void cancelAnnotationRead();

    /// The read of a job (the worker thread): the measurement annotations of every page of the document
    static PDFAppMeasureAnnotationRead readAnnotations(PDFDocumentPointer document, std::shared_ptr<PDFAppCancelFlag> cancel);

    /// The callout over the canvas of the empty state (471:5490 "Pick two points to measure a distance" and the
    /// titles of the other kinds; review RS7CFIGMA L1): the drawing mode on, no picked point, no measurement
    void updateCanvasCallout();

    /// The scale of the document at the entry: remembered, of the viewports, the default (473:9500, 473:92070)
    void loadScale();

    /// The path and /ID[0] of the document (empty = it cannot be identified)
    QString getDocumentPath() const;
    QByteArray getDocumentId() const;

    /// The items of the core of the temporary measurements (the scale of now)
    std::vector<PDFMeasureItem> createItems(const std::vector<PDFAppMeasurement>& measurements) const;

    /// Writes measurement annotations by one commit of the tool; \p failed gets the indices, which were not written
    bool writeAnnotations(const std::vector<PDFMeasureItem>& items, const QString& description, std::vector<size_t>* failed);

    void updateParts();
    void updateToolbar();
    void updateList();
    void updateRightPanel();
    void updateOverlay();
    void updateLabelParts();
    void updateInput();

    /// The style of the labels of the parameters (the canvas)
    PDFAppMeasureLabelStyle getLabelStyle() const;

    /// The scale shown by the button Scale ("Metric 1:50", "From the document", "Several (from the document)")
    QString getScaleButtonText() const;

    /// The rows of the panel Scale of the presets of now (471:6943)
    void rebuildScaleRows();

    void onKindRowClicked();
    void onScaleRowClicked();
    void onCalibrateClicked();
    void onCustomScaleClicked();
    void onManagePresetsClicked();
    void onShowClicked();
    void onCrossClicked();
    void onClearClicked();
    void onExportClicked();
    void onChangeDefaultClicked();
    void onOwnerPasswordClicked();
    void onFontChanged(int index);
    void onTextColorSelected(const QColor& color);
    void onBackgroundColorSelected(const QColor& color);
    void onRowChanged(const QModelIndex& current);
    void onQueuedRowChanged();
    void onRowActionTriggered(const QModelIndex& index, int action);
    void onMeasurementPicked(pdf::PDFInteger pageIndex, pdf::PDFMeasureKind kind, std::vector<QPointF> points);
    void onCalibrationPicked(pdf::PDFInteger pageIndex, pdf::PDFReal length);
    void onOverlayDrawingChanged();
    void onDocumentLockChanged();
    void onCustomScaleAccepted(pdf::PDFMeasureScale scale);
    void onCalibrationRejected();
    void onDefaultScaleAccepted(pdf::PDFMeasureScale scale);
    void onPresetsAccepted(std::vector<pdf::PDFMeasureScale> presets, bool isRestored);
    void onZeroLengthPickAgain();
    void onMenuToggled();
    void onMenuToggledQueued();

    /// The status text of an open menu of the toolbar (471:6294, 471:6943, 471:7608), empty = no menu is open
    QString getOpenMenuStatusText() const;

    std::vector<PDFAppMeasurement> m_measurements;
    std::vector<PDFMeasureAnnotationInfo> m_annotations;
    bool m_isAnnotationsTruncated = false;
    quint64 m_nextId = 1;

    // The read of the measurement annotations (one job, the last request wins)
    quint64 m_readGeneration = 0;
    std::shared_ptr<PDFAppCancelFlag> m_readCancel;
    QPointer<QFutureWatcher<PDFAppMeasureAnnotationRead>> m_readWatcher;   ///< Child of the GUI
    std::optional<quint64> m_selectedId;
    PDFObjectReference m_selectedAnnotation;
    bool m_isDrawing = true;
    bool m_isCalibrating = false;
    bool m_isShown = true;
    bool m_isInitialized = false;
    bool m_isMenuStatusQueued = false;
    PDFMeasureScale m_scale = PDFMeasureScale::createIdentity();
    ScaleSource m_scaleSource = ScaleSource::Default;
    int m_documentScaleCount = 0;               ///< Different scales of the viewports of the document (FM-5)
    Notice m_notice = Notice::None;
    int m_noticeStored = 0;
    int m_noticeTotal = 0;

    QPointer<PDFAppMeasureListModel> m_listModel;  ///< Child of the GUI
    QPointer<PDFAppMeasureOverlay> m_overlay;       ///< The shell's

    QPointer<PDFAppToolPanelPopup> m_kindPanel;
    std::map<PDFAppMeasureDrawKind, QPointer<PDFTileRow>> m_kindRows;
    QPointer<PDFAppToolPanelPopup> m_scalePanel;
    std::vector<QPointer<QWidget>> m_scalePanelWidgets;    ///< The rows of the panel Scale (made again with the presets)
    std::vector<PDFMeasureScale> m_scalePresets;            ///< The presets of the rows
    QPointer<PDFAppToolEnumMenu> m_storageMenu;
    QPointer<QToolButton> m_showButton;
    QPointer<QToolButton> m_crossButton;
    QPointer<QToolButton> m_clearButton;
    QPointer<QToolButton> m_exportButton;

    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<QWidget> m_listCard;
    QPointer<PDFListView> m_listView;
    QPointer<QWidget> m_emptyCard;
    QPointer<PDFAppParagraph> m_listHint;

    std::map<Callout, QPointer<PDFAppToolCallout>> m_callouts;
    QPointer<PDFTextLink> m_ownerPasswordLink;
    QPointer<PDFLabel> m_scaleRatio;
    QPointer<PDFLabel> m_scaleSubtitle;
    QPointer<PDFLabel> m_viewportNote;
    QPointer<PDFLabel> m_defaultScale;
    QPointer<PDFComboBox> m_fontBox;
    QPointer<PDFColorSwatchRow> m_textColorRow;
    QPointer<PDFLabel> m_textColorName;
    QPointer<PDFColorSwatchRow> m_backgroundColorRow;
    QPointer<PDFLabel> m_backgroundColorName;

    QPointer<PDFAppMeasureScaleDialog> m_scaleDialog;           ///< Custom Scale…, the default scale
    QPointer<PDFAppMeasureScaleDialog> m_calibrationDialog;     ///< Calibrate Scale… (its own, review RS7CAPP L3)
    QPointer<PDFAppMeasurePresetsDialog> m_presetsDialog;
    QPointer<PDFMessageBox> m_zeroLengthDialog;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_MEASURE_H
