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

#ifndef PDFAPPTOOLGUI_IMAGES_H
#define PDFAPPTOOLGUI_IMAGES_H

#include "pdfappglobal.h"
#include "pdfappdeclarativetoolgui.h"
#include "pdfappimagepagepreview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"

#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumenttool_images.h"
#include "pdffluentwidgets.h"
#include "pdfimagepagefactory.h"

#include <QAction>
#include <QImage>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QWidget>

#include <map>
#include <memory>
#include <set>

class QMimeData;
class QRadioButton;

namespace pdf
{

class PDFAppImageThumbnailJob;
class PDFAppImageThumbnailLink;
class PDFAppNameDialog;

/// The tools convert.images-to-pdf and pages.interleave (package D of step 3a of phase 2, plan
/// 9.2; the GUI of Interleave in pdfapptoolgui_interleave.*); its file defines the element of
/// the panel builder of package D (addSelectedItemCard)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIImages
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolGUIImages)

public:
    PDFAppToolGUIImages() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and their
    /// GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerImageTools();

    /// Name of the named size of a page in points in either orientation ("A4", "Letter"; empty
    /// = none, PDFPageSizePresets within one point)
    static QString getPresetName(const QSizeF& sizeInPoints);

    /// "A4 landscape", "Letter portrait", "254 × 190 mm" of a page in points
    static QString getPageDescription(const QSizeF& sizeInPoints);
};

/// The section of the selected item of a right panel (section Selected image 336:1976: the title
/// Semi Bold 13, the Tag "1 of 6", the link Rename at the trailing edge, the card of the rows
/// Name, Folder, Pixels, Rotation, Crop, Page; also the Selection of Manage Pages in 3b). The
/// tool fills the rows (getValues) and reacts to the link (linkActivated, emitted last).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolSelectedItemCard : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// \param title Title of the section ("Selected image")
    /// \param parent Parent widget
    explicit PDFAppToolSelectedItemCard(const QString& title, QWidget* parent);
    virtual ~PDFAppToolSelectedItemCard() override;

    /// Text of the Tag ("1 of 6"), empty = no Tag
    void setTagText(const QString& text);

    /// Text of the link ("Rename"), empty = no link
    void setLinkText(const QString& text);

    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFTag* getTag() const { return m_tag.data(); }
    PDFTextLink* getLink() const { return m_link.data(); }
    PDFKeyValueList* getValues() const { return m_values.data(); }

Q_SIGNALS:
    void linkActivated();

private:
    void onLinkClicked();

    QPointer<PDFLabel> m_title;
    QPointer<PDFTag> m_tag;
    QPointer<PDFTextLink> m_link;
    QPointer<PDFKeyValueList> m_values;
};

/// The canvas of Images to PDF (document-canvas 336:2009): bg/track, the padding 20, the bar of
/// the preview (preview-bar 336:2217: "Page 1 of 6 · IMG_2041.jpg" Semi Bold 13, the segments
/// Fit / 100 % 120 x 32) and the stage (336:2234): the page of the selected image with its image
/// (PDFAppImagePagePreview, the shadow 0 / 8 / 24) and the caption 12 below it ("A4 landscape · 297
/// × 210 mm · image fits inside 10 mm margins, centered", Regular 11 text/secondary). 100 % scrolls.
/// Without images the drop zone of 336:2360 ("Drop images here") is in the place of the stage.
///
/// Ownership: the canvas owns its parts; the GUI holds the canvas by a QPointer. zoomChanged is
/// emitted from the segments, last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppImagesToPdfCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 336:2009 / 336:2234
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int GAP = 12;
    static constexpr int SEGMENT_WIDTH = 60;
    static constexpr int SEGMENT_HEIGHT = 32;
    static constexpr int STAGE_MARGIN = 32;     ///< Room of the shadow of the page in the stage

    explicit PDFAppImagesToPdfCanvas(QWidget* parent);
    virtual ~PDFAppImagesToPdfCanvas() override;

    void setTitle(const QString& title);
    void setCaption(const QString& caption);

    /// The page, its image and its layout
    void setPage(const QImage& image, QSizeF imageSize, const PDFImagePageLayout& layout);

    /// Fit (true) or 100 % (false)
    void setFit(bool isFit);
    bool isFit() const;

    /// The stage is shown (an image), else the drop zone
    void setEmpty(bool isEmpty);
    bool isEmpty() const;

    /// The crop of an image (336:3159): the stage of the crop with the image alone and the box
    /// of PDFAppCropOverlay instead of the page; Fit / 100 % work for it the same way
    void setCropMode(bool isCropMode);
    bool isCropMode() const;
    PDFAppToolPageStage* getCropStage() const { return m_cropStage.data(); }

    PDFLabel* getTitleLabel() const { return m_title.data(); }
    PDFSegmentedControl* getZoomControl() const { return m_zoom.data(); }
    PDFAppImagePagePreview* getPreview() const { return m_preview.data(); }
    PDFLabel* getCaptionLabel() const { return m_caption.data(); }
    QScrollArea* getScrollArea() const { return m_scrollArea.data(); }
    QWidget* getEmptyState() const { return m_emptyState.data(); }
    QPushButton* getAddImagesButton() const { return m_addImagesButton.data(); }

Q_SIGNALS:
    /// Fit / 100 % has been chosen by the user
    void zoomChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onZoomIndexChanged(int index);
    void updateTexts();
    void updateStage();

    QPointer<PDFLabel> m_title;
    QPointer<PDFSegmentedControl> m_zoom;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_stage;
    QPointer<PDFAppImagePagePreview> m_preview;
    QPointer<PDFAppToolPageStage> m_cropStage;
    QPointer<PDFLabel> m_caption;
    QPointer<QWidget> m_emptyState;
    QPointer<QPushButton> m_addImagesButton;
};

/// The right panel of the crop of an image (sidebar-right 336:3159): "Crop" with the Tag of the
/// name of the image; the card of Equal margins (Margin) and Crop box (Left, Top, Width, Height)
/// in pixels - the fields of the mode, which is not chosen, are disabled - and Apply to
/// (Selected image, All images); Result (Original size, Size after crop); Reset Crop and Apply
/// Crop; the blue callout "The image file is not changed".
///
/// The values are the box of the crop in pixels of the image as shown (turned); the GUI owns
/// them and says them by setValues (no signal). A field, a radio button or Apply to of the user
/// emits valuesEdited; the buttons emit resetClicked and applyClicked - each the last statement.
///
/// Ownership: a child of the right panel of the tool, held by a QPointer; its parts are its
/// children. Thread: GUI only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppImageCropPanel : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// The values of the panel
    struct Values
    {
        QString name;               ///< Name of the image (the Tag)
        QSize imageSize;            ///< Size of the shown image in pixels
        QRect rect;                 ///< The box of the crop in its pixels
        bool isEqualMargins = true; ///< Equal margins (else Crop box)
        bool isAllImages = false;   ///< Apply to: All images
        int imageCount = 1;         ///< Number of the images ("The same margins for all 6 images")
    };

    explicit PDFAppImageCropPanel(QWidget* parent);
    virtual ~PDFAppImageCropPanel() override;

    void setValues(const Values& values);
    const Values& getValues() const { return m_values; }

    /// The margin of Equal margins of a box (the smallest of its four margins)
    static int getEqualMargin(const QRect& rect, const QSize& imageSize);

    /// The box of the equal margin, bounded (at least one pixel left)
    static QRect getEqualMarginRect(int margin, const QSize& imageSize);

    PDFTag* getNameTag() const { return m_nameTag.data(); }
    QRadioButton* getEqualRadio() const { return m_equalRadio.data(); }
    QRadioButton* getBoxRadio() const { return m_boxRadio.data(); }
    PDFSpinBox* getMarginField() const { return m_marginField.data(); }
    PDFSpinBox* getLeftField() const { return m_leftField.data(); }
    PDFSpinBox* getTopField() const { return m_topField.data(); }
    PDFSpinBox* getWidthField() const { return m_widthField.data(); }
    PDFSpinBox* getHeightField() const { return m_heightField.data(); }
    PDFComboBox* getApplyToField() const { return m_applyToField.data(); }
    PDFKeyValueList* getResult() const { return m_result.data(); }
    QPushButton* getResetButton() const { return m_resetButton.data(); }
    QPushButton* getApplyButton() const { return m_applyButton.data(); }
    PDFAppToolCallout* getCallout() const { return m_callout.data(); }

Q_SIGNALS:
    void valuesEdited();
    void resetClicked();
    void applyClicked();

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    PDFSpinBox* createField(QGridLayout* grid, int row, const QString& label);
    void updateFields();
    void updateTexts();
    void onFieldEdited();
    void onModeToggled(bool isChecked);
    void onApplyToActivated(int index);
    void onResetClicked();
    void onApplyClicked();

    Values m_values;
    QPointer<PDFLabel> m_title;
    QPointer<PDFTag> m_nameTag;
    QPointer<QRadioButton> m_equalRadio;
    QPointer<QRadioButton> m_boxRadio;
    QPointer<PDFSpinBox> m_marginField;
    QPointer<PDFSpinBox> m_leftField;
    QPointer<PDFSpinBox> m_topField;
    QPointer<PDFSpinBox> m_widthField;
    QPointer<PDFSpinBox> m_heightField;
    QPointer<PDFComboBox> m_applyToField;
    std::map<QWidget*, QPointer<PDFLabel>> m_fieldLabels;
    QPointer<PDFLabel> m_resultTitle;
    QPointer<PDFKeyValueList> m_result;
    QPointer<QPushButton> m_resetButton;
    QPointer<QPushButton> m_applyButton;
    QPointer<PDFAppToolCallout> m_callout;
    bool m_isUpdating = false;
};

/// Images to PDF (convert.images-to-pdf; Figma 336:1976 … 348:29711, 356:9165, 356:9556; plan of
/// step 3 of phase 2, 2.9, 4.1, 5.8, K14-K17, K34, K37, K38, K41, K43, Q13 / R16) in its own tab
/// "New document":
///  - the toolbar: Add Images (Image files… Ctrl+Alt+I, Paste from clipboard Ctrl+V), Rotate Left,
///    Rotate Right, Crop (336:3159, step D2 of 9.4), Duplicate, Remove (Rotate, Duplicate and
///    Remove act on all selected images); Size (the parameter pageSize), Sort (File name, Folder
///    then file name, File type, Reverse order), Optimize (the parameter optimizeImages);
///  - the panel Images: the images in the order of the pages (PDFAppToolInputFilesPanel: the
///    thumbnail 56 x 43, the pixels, the format, the size, "Page 1 · A4 landscape", the Tags
///    "rotated 90°" and "cropped", a drag changes the order, Ctrl or Shift selects more images);
///    a drop of files anywhere in the tab and Ctrl+V add images;
///  - the canvas: PDFAppImagesToPdfCanvas - the page of the selected image laid out by the same
///    rule as the core (PDFImagePageLayout), its colors through the color management (K43); in
///    the crop mode the image alone with PDFAppCropOverlay;
///  - the right panel: Page layout (Width and Height of Custom, Orientation, Margins, Image,
///    Anchor - PDFAppToolAnchorComboBox, K14 - and the note), Selected image
///    (PDFAppToolSelectedItemCard with Rename), Summary (Pages, Page size, Image optimization,
///    Estimated size); in the crop mode PDFAppImageCropPanel.
///
/// The crop (336:3159) is a state of the GUI (the box in pixels of the turned image, Equal
/// margins / Crop box, Apply to) until Apply Crop writes "adjustments" (one undo step); Crop
/// again, a click of another image or a run leave it. The thumbnails of the rows are decoded in
/// the pool (PDFExecutionPolicy) from copies of the bytes; a late one is dropped by the link,
/// which the destructor cuts.
///
/// Values and Undo / Redo: "inputs" (the ids of the list in the order of the pages) and
/// "adjustments" (the turn and the crop of the images by their letters in this order) change as
/// one undo step; a move of an image keeps its adjustment (the GUI keeps them by the ids). The
/// layout values are parameters of the core. isResultInThisTab: the new document replaces the tab
/// (336:3572, R16).
///
/// Ownership (checklist point 1): the shell owns this GUI and the parts (QPointer, made again in
/// a new language); the list belongs to the tab (QPointer). The decoded preview image is a value
/// of this GUI (a QImage), decoded in the GUI thread scaled to the stage (QImageReader::
/// setScaledSize, at most MAXIMUM_PREVIEW_SIDE). The actions Ctrl+Alt+I and Ctrl+V and the drop
/// of files belong to the key widget of the host while this GUI lives (the destructor takes them
/// back).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppImagesToPdfToolGUI : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// Longest side of the decoded preview image in pixels
    static constexpr int MAXIMUM_PREVIEW_SIDE = 2048;

    explicit PDFAppImagesToPdfToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppImagesToPdfToolGUI() override;

    /// Registers the tool and its GUI (PDFAppToolGUIImages::registerImageTools)
    static void registerTools();

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual QString getOwnTabTitle() const override;
    virtual QString getOwnTabIconName() const override;
    virtual QString getOwnTabLossText() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;
    virtual QString getStartPageText(PDFAppToolStartPageText text) const override;
    virtual PDFAppToolInputRules getInputRules() const override;
    virtual bool isResultInThisTab() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;

    // ------------------------------------------------------------ actions of the user

    /// Adds the image files at the end (one undo step); refused ones are said in the status bar
    void addFiles(const QStringList& filePaths);

    /// Image files… (Ctrl+Alt+I): the dialog of the files, then addFiles
    void addFilesFromDialog();

    /// Paste from clipboard (Ctrl+V): the image of the clipboard at the end
    void pasteImage();

    /// The selected image: turned by a quarter turn, duplicated, removed (each one undo step)
    void rotateLeft();
    void rotateRight();
    void duplicateSelected();
    void removeSelected();

    /// Moves the image to the index (one undo step; its adjustment goes with it)
    void moveImage(const QString& inputId, int targetIndex);

    /// Sort (336:2639): by the file name (natural order), by the folder then the file name, by the
    /// file type, the current order reversed (each one undo step)
    void sortByName();
    void sortByFolder();
    void sortByType();
    void reverseOrder();

    /// The selected image (its page in the preview); the selection of more images is only it
    void selectImage(const QString& inputId);
    const QString& getSelectedImage() const { return m_selectedId; }

    /// A click on a row (336:1976 "Ctrl or Shift selects more images"): Ctrl adds or removes
    /// the image, Shift selects the images from the selected one to it; the clicked image is
    /// the one of the preview. Rotate, Duplicate and Remove act on all selected images.
    void clickImage(const QString& inputId, Qt::KeyboardModifiers modifiers);

    /// The selected images in the order of the pages (at least the one of the preview)
    QStringList getSelectedImages() const;

    /// Rename (336:1976, the link of Selected image): the image of the preview gets the name -
    /// one undo step (a copy of the input under the name takes its place, its adjustment goes
    /// with it); \returns false and the reason for an invalid name
    bool renameImage(const QString& inputId, const QString& name, QString* errorText);

    /// The dialog of Rename of the image of the preview (the layout of Rename workspace
    /// 955:150171; no frame of its own)
    void openRenameDialog();
    PDFAppNameDialog* getRenameDialog() const;

    // ------------------------------------------------------- the crop of an image (336:3159)

    /// The crop mode: the toolbar button Crop is active, the canvas shows the image of the
    /// preview with the box, the right panel is the panel of the crop, Create PDF is disabled.
    /// The box starts as the crop of the image (none = the whole image).
    void setCropping(bool isCropping);
    bool isCropping() const { return m_isCropping; }

    /// The box of the crop in pixels of the shown (turned) image, its mode and Apply to (the
    /// values of the panel, no undo step until Apply Crop)
    void setCropRect(const QRect& rect, bool isEqualMargins);
    QRect getCropRect() const { return m_cropRect; }
    void setCropAppliedToAll(bool isAllImages);

    /// Reset Crop: the box is the whole image again (Apply Crop writes it)
    void resetCrop();

    /// Apply Crop: the margins of the box are the crop of the image (Selected image) or of all
    /// images (the same margins, each image by its own turn) - one undo step; the mode ends
    void applyCrop();

    /// The crop margins of the image (pixels of the image as shown, before its turn) of margins of
    /// the image turned by \p turn, and back (5.8: the crop comes before the turn)
    static QMargins getUnturnedMargins(const QMargins& turnedMargins, PageRotation turn);
    static QMargins getTurnedMargins(const QMargins& margins, PageRotation turn);

    /// The thumbnail of a row of an image (the decoded picture with its crop and turn); null
    /// while it is decoded (tests)
    QImage getRowThumbnail(const QString& inputId) const;

    /// Number of the decodings of thumbnails running in the pool (tests)
    int getPendingThumbnailCount() const { return int(m_pendingThumbnails.size()); }

    /// The adjustment of the image (no turn and no crop = none)
    PDFDocumentToolImagesToPdf::Adjustment getAdjustment(const QString& inputId) const;

    /// Accepts a drop of files (the drop anywhere in the tab); \returns true for files
    bool dropFiles(const QMimeData* mimeData);

    /// The layout of the page of the image by the values of now (the turn and the crop of the
    /// image included); std::nullopt for invalid values
    std::optional<PDFImagePageLayout> getImageLayout(const QString& inputId) const;

    /// Size of the page of the image in points by the values of now (empty for invalid values)
    QSizeF getImagePageSize(const QString& inputId) const;

    /// Parts (tests)
    PDFAppImagesToPdfCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppToolInputFilesPanel* getImagesPanel() const { return m_imagesPanel.data(); }
    PDFAppToolSelectedItemCard* getSelectedCard() const { return m_selectedCard.data(); }
    PDFKeyValueList* getSummary() const { return m_summary.data(); }
    PDFAppToolMarginFields* getLayoutFields() const { return m_layoutFields.data(); }
    PDFAppToolAnchorComboBox* getAnchorComboBox() const { return m_anchorComboBox.data(); }
    PDFAppImageCropPanel* getCropPanel() const { return m_cropPanel.data(); }
    PDFAppCropOverlay* getCropOverlay() const { return m_cropOverlay.data(); }
    QToolButton* getAction(const QString& name) const;
    PDFAppToolPanelPopup* getPanel(const QString& name) const;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class PDFAppImageThumbnailLink;

    using Adjustments = std::map<QString, PDFDocumentToolImagesToPdf::Adjustment>;

    void connectList();
    void initializeValues();
    void readValues();
    void applyChange(const QStringList& inputIds, const Adjustments& adjustments);
    QStringList getModelInputIds() const;
    QStringList getUsableInputIds() const;
    void addResults(const std::vector<PDFAppToolInputList::AddResult>& results, const QStringList& refusedTexts);
    void turnSelected(bool isClockwise);
    void sortBy(int key);

    void onListChanged();
    void onListInputChanged(const QString& inputId);
    void onListDestroyed();
    void onImageRowClicked(const QString& inputId);
    void onImageLinkActivated(const QString& inputId);
    void onAddFilesRowClicked();
    void onPasteRowClicked();
    void onSortRowClicked();
    void onZoomChanged();
    void onSelectedLinkActivated();
    void onCropClicked();
    void onCropRectChanged(const QRectF& rect);
    void onCropRectChosen(const QRectF& rect);
    void onCropPanelEdited();
    void onCropReset();
    void onCropApply();
    void onThumbnailDecoded(const std::shared_ptr<pdf::PDFAppImageThumbnailJob>& job);

    void updateAll();
    void updateImages();
    void updateCanvas();
    void updateCropCanvas();
    void updateRightPanel();
    void updateCropPanel();
    void updateToolbar();

    PDFAppToolInputRow::Content createRowContent(const PDFAppToolInput& input, int index, int firstPage);

    /// The decoded preview of the image (cached by its id, its generation and the color
    /// management), its colors as a viewer shows them (K43)
    QImage getPreviewImage(const PDFAppToolInput& input);

    /// The picture of the image of the preview turned (the crop stage)
    QImage getTurnedPreviewImage(const PDFAppToolInput& input, PageRotation turn);

    /// Size of the shown image in pixels after its turn
    static QSize getTurnedSize(const QSize& size, PageRotation turn);

    /// The thumbnail of the row: the decoded one with its crop and turn; a decoding is started
    /// for a missing one
    QImage getThumbnail(const PDFAppToolInput& input);
    void startThumbnail(const PDFAppToolInput& input, const QString& key);

    /// Size of the shown image in points by its resolution
    static QSizeF getImageSizeInPoints(const PDFAppToolInput& input);

    QString getParameter(const char* name) const;
    bool isEditable() const;

    QPointer<PDFAppToolInputList> m_list;
    QPointer<PDFAppImagesToPdfCanvas> m_canvas;
    QPointer<PDFAppToolInputFilesPanel> m_imagesPanel;
    QPointer<PDFAppToolSelectedItemCard> m_selectedCard;
    QPointer<QWidget> m_layoutSection;              ///< Page layout, Selected image, Summary
    QPointer<QWidget> m_cropSection;                ///< The panel of the crop
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppToolMarginFields> m_layoutFields;
    QPointer<PDFAppToolAnchorComboBox> m_anchorComboBox;
    QPointer<PDFAppImageCropPanel> m_cropPanel;
    QPointer<PDFAppCropOverlay> m_cropOverlay;
    QPointer<PDFAppNameDialog> m_renameDialog;
    std::map<QString, QPointer<QToolButton>> m_actions;
    std::map<QString, QPointer<PDFAppToolPanelPopup>> m_panels;
    QPointer<QWidget> m_keyWidget;
    QPointer<QAction> m_addFilesAction;
    QPointer<QAction> m_pasteAction;
    Adjustments m_adjustments;
    QString m_selectedId;
    QStringList m_extraSelectedIds;     ///< The images selected besides m_selectedId (Ctrl, Shift)
    QString m_previewKey;               ///< Id, generation and color management of the decoded preview
    QImage m_previewImage;

    /// The crop of an image (GUI only until Apply Crop)
    bool m_isCropping = false;
    QRect m_cropRect;
    bool m_isCropEqual = true;
    bool m_isCropAll = false;

    /// The thumbnails of the rows: decoded pictures by "<id>/<generation>", the pictures with
    /// their crop and turn by "<key>/<turn>/<crop>"; the decodings running in the pool
    std::shared_ptr<PDFAppImageThumbnailLink> m_thumbnailLink;
    std::map<QString, QImage> m_thumbnails;
    std::map<QString, QImage> m_adjustedThumbnails;
    std::map<QString, std::shared_ptr<PDFAppImageThumbnailJob>> m_pendingThumbnails;

    bool m_isFit = true;
    bool m_isValuesInitialized = false;
    bool m_isChangingList = false;
    bool m_isRunning = false;
    bool m_wasDropAccepted = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_IMAGES_H
