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

#ifndef PDFAPPTOOLGUI_OPTIMIZEIMAGES_H
#define PDFAPPTOOLGUI_OPTIMIZEIMAGES_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolimagecomparison.h"
#include "pdfapptoolimagelist.h"
#include "pdfapptoolimageprofileeditor.h"
#include "pdfapptoolpanelparts.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumenttool.h"

#include <QCheckBox>
#include <QFutureWatcher>
#include <QPointer>
#include <QToolButton>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QBoxLayout;
class QCheckBox;
class QToolButton;

namespace pdf
{

class PDFAppToolCallout;
class PDFAppToolImageCollectionControl;
class PDFAppToolFlagsMenu;
class PDFAppToolEnumMenu;
class PDFAppToolImageComparison;
class PDFAppToolImageProfileEditor;
class PDFAppToolPanelPopup;
class PDFAppToolProgressCard;
class PDFAppToolSelectedItemCard;
class PDFKeyValueList;
class PDFTileRow;

/// The collection of the list of the images of one version of a document in the pool (one
/// asynchronous job - QtConcurrent, owner 2026-11-09; the collection itself walks the pages in
/// PDFExecutionPolicy): it holds the shared immutable document and its own cancellation, never a
/// GUI. A value of the job.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageListCollection
{
public:
    std::shared_ptr<const PDFDocumentImageList> list;
    quint64 version = 0;
};

/// The tools Optimize Images (optimize.images, Figma 297:3135, 299:3362, 301:3594, 301:4019) and
/// Convert Images to Grayscale (optimize.grayscale, 308:6911, 320:7812, 308:7341) - one class in
/// two modes (owner answer Q4.4: no "All settings"); plan of step 4 of phase 2, 4.2, 4.3, Q4.9,
/// R4-22; package P2. Its parts: PDFAppToolImageList, PDFAppToolImageComparison,
/// PDFAppToolImageProfileEditor; the measuring as Compress (the preview is the result).
///
/// The mode is the id of the catalog entry of the shell (optimize.grayscale = Grayscale).
///
/// The list of the images (PDFDocumentImageList, the options of the changing tools, R4-4) is
/// collected for every version of the document in the pool; the measuring (the preview of the
/// runner, isPreviewFinal) gives the status and the sizes of every image (the data "images" of the
/// core) and the changed document, whose images are the AFTER of the canvas - the real encoding,
/// decoded by the decoding of the core, never a simulation in the GUI.
///
/// Exceptions (Exclude, Override 301:3594, the check boxes of Selected image) are values of the
/// parameters: Optimize Images "images" (PDFDocumentToolOptimizeImages::writeImageExceptions),
/// Grayscale "exclude" (PDFDocumentToolGrayscale::writeExclude) - one undo step each.
///
/// Ownership (checklist point 1): the shell owns the GUI; the parts are children of the panels
/// and of the canvas page, the GUI holds them by QPointer; the decoder of the canvas and the
/// watcher of the collection are children of the GUI. A job of the pool holds only shared values.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIOptimizeImages : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// State of the measuring of the current parameters (as Compress)
    enum class MeasureState
    {
        Measuring,      ///< The preview computes or waits for its delay
        Stopped,        ///< Stop of a run: no result, the next confirmation computes again
        Measured,       ///< The result of the parameters is known
        NotReduced,     ///< No image gets a new encoding (NothingToDo, plan 2.7 as 295:2182)
        Failed          ///< The tool cannot measure the document (an error in the card)
    };

    static constexpr int MENU_WIDTH = 300;

    explicit PDFAppToolGUIOptimizeImages(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIOptimizeImages() override;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and their GUI in
    /// PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerOptimizeImagesTools();

    /// The GUI is Convert Images to Grayscale
    bool isGrayscale() const;

    MeasureState getMeasureState() const { return m_measureState; }

    /// The list of the images of the shown version (nullptr until it is collected)
    const PDFDocumentImageList* getImageList() const { return m_list.get(); }

    /// The keys of the excluded images and of the images with an override (the values of the
    /// model), sorted
    std::vector<PDFDocumentImageList::Key> getExcludedKeys() const;
    std::vector<PDFDocumentImageList::Key> getOverrideKeys() const;

    /// Excludes the selected images, or puts them back, when all of them are excluded (Exclude)
    void toggleExclude();

    /// Gives the selected images an override, or removes it, when all of them have one (Override)
    void toggleOverride();

    /// Parts (nullptr = none)
    PDFAppToolImageList* getImageListWidget() const { return m_imageList.data(); }
    PDFAppToolImageComparison* getComparison() const { return m_comparison.data(); }
    PDFAppToolImageProfileEditor* getProfileEditor() const { return m_profileEditor.data(); }
    PDFAppToolSelectedItemCard* getSelectedCard() const { return m_selectedCard.data(); }
    QCheckBox* getEnableCheckBox() const { return m_enableCheckBox.data(); }
    QCheckBox* getOverrideCheckBox() const { return m_overrideCheckBox.data(); }
    PDFKeyValueList* getSummaryValues() const { return m_summaryValues.data(); }
    PDFAppToolProgressCard* getMeasuringCard() const { return m_measuringCard.data(); }
    PDFAppToolCallout* getNotReducedCallout() const { return m_notReducedCallout.data(); }
    PDFAppToolCallout* getInfoCallout() const { return m_infoCallout.data(); }
    QToolButton* getExcludeButton() const { return m_excludeButton.data(); }
    QToolButton* getOverrideButton() const { return m_overrideButton.data(); }
    QToolButton* getColorButton() const;
    PDFTileRow* getColorRow(const QString& colorMode) const;
    PDFAppToolEnumMenu* getModeMenu() const;
    PDFAppToolEnumMenu* getGoalMenu() const;
    PDFAppToolFlagsMenu* getOptionsMenu() const;

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual int getRightPanelSpacing() const override;
    virtual PDFAppToolProgressPlace getProgressPlace() const override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusDetailText() const override;
    virtual bool isFileSizeInStatusText() const override { return false; }  ///< 297:3135, 308:6911
    virtual QString getRunningTitle(int percentage) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview) override;
    virtual void onPreviewProgress(int percentage, const QString& stepText) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void onDocumentChanged() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The measured result of one image (the data "images" of the core)
    class ImageResult
    {
    public:
        QString status;
        qint64 originalBytes = 0;
        qint64 newBytes = 0;
    };

    void startCollection();
    void onCollectionFinished();
    void setMeasureState(MeasureState state);
    void readMeasuredImages();

    void updateParts();
    void updateRows();
    void updateHead();
    void updateSelection();
    void updateSelectedCard();
    void updateSummary();
    void updateCanvas();
    void updateCanvasImages();
    void updateToolbar();
    void updateProfileEditor();

    void onImageSelectionChanged();

    /// Space of the list (markToggled, from the event loop): Exclude of the selection (toggleExclude)
    void onImageMarksToggled();
    void onImagesDecoded(quint64 ticket, const QList<QImage>& images);
    void onColorRowClicked();
    void onEnableToggled(bool isChecked);
    void onOverrideToggled(bool isChecked);
    void onShowOnPage();

    bool isExcluded(const PDFDocumentImageList::Key& key) const;
    bool hasOverride(const PDFDocumentImageList::Key& key) const;
    bool isChangeable(const PDFDocumentImageList::Item& item) const;

    /// Writes the exclusion of the images (one undo step)
    void setExcluded(const std::vector<PDFDocumentImageList::Key>& keys, bool isExcluded);

    /// Writes the override of the images (one undo step)
    void setOverride(const std::vector<PDFDocumentImageList::Key>& keys, bool hasOverride);

    /// The selected images, which a tool can change (no inline image, Grayscale: color images)
    std::vector<PDFDocumentImageList::Key> getChangeableSelection() const;

    /// The color mode shown by the menu Color: of the override of the selection, or the global one
    QString getShownColorMode() const;

    const ImageResult* getImageResult(const PDFDocumentImageList::Key& key) const;
    const PDFDocumentImageList::Item* getCurrentItem() const;

    MeasureState m_measureState = MeasureState::Measuring;
    std::optional<PDFDocumentToolResult> m_result;  ///< The measured result of the current parameters
    std::map<QString, ImageResult> m_imageResults;  ///< By the id of the image
    QString m_errorText;
    int m_percentage = 0;
    QString m_stepText;
    bool m_isRunning = false;

    std::shared_ptr<const PDFDocumentImageList> m_list;
    quint64 m_listVersion = 0;
    bool m_isCollecting = false;
    QPointer<QFutureWatcher<PDFAppToolImageListCollection>> m_collectionWatcher;
    std::shared_ptr<PDFAppToolImageCollectionControl> m_collectionControl;

    QPointer<PDFAppToolImageDecoder> m_decoder;
    quint64 m_beforeTicket = 0;
    quint64 m_afterTicket = 0;
    PDFDocumentImageList::Key m_shownKey;           ///< The image of the canvas
    PDFDocumentPointer m_shownAfterDocument;        ///< The document of its AFTER

    QPointer<PDFAppToolImageList> m_imageList;
    QPointer<PDFAppToolImageComparison> m_comparison;
    QPointer<PDFAppToolImageProfileEditor> m_profileEditor;
    QPointer<PDFAppToolSelectedItemCard> m_selectedCard;
    QPointer<QCheckBox> m_enableCheckBox;
    QPointer<QCheckBox> m_overrideCheckBox;
    QPointer<PDFKeyValueList> m_summaryValues;
    QPointer<PDFAppToolProgressCard> m_measuringCard;
    QPointer<PDFAppToolCallout> m_notReducedCallout;
    QPointer<PDFAppToolCallout> m_infoCallout;
    QPointer<QToolButton> m_excludeButton;
    QPointer<QToolButton> m_overrideButton;
    QPointer<PDFAppToolPanelPopup> m_colorPanel;
    QPointer<PDFAppToolEnumMenu> m_modeMenu;
    QPointer<PDFAppToolEnumMenu> m_goalMenu;
    QPointer<PDFAppToolFlagsMenu> m_optionsMenu;
    std::map<QString, QPointer<PDFTileRow>> m_colorRows;
    QPointer<QBoxLayout> m_summaryLayout;           ///< Layout of the section Summary (owned by the content of the panel, which the shell may rebuild)
    bool m_isUpdatingSelection = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_OPTIMIZEIMAGES_H
