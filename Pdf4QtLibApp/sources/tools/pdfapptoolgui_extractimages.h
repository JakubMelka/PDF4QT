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

#ifndef PDFAPPTOOLGUI_EXTRACTIMAGES_H
#define PDFAPPTOOLGUI_EXTRACTIMAGES_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolgui_images.h"
#include "pdfapptoolimageformatmenu.h"
#include "pdfapptoolimagelist.h"
#include "pdfapptoolpagestage.h"
#include "pdfdocumenttool_extractimages.h"

#include <QFutureWatcher>
#include <QLineEdit>
#include <QPointer>
#include <QPolygonF>
#include <QToolButton>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppToolExtractCollectionControl;
class PDFAppToolOutputFilesSection;
class PDFAppToolPanelPopup;
class PDFTileRow;

/// Where an image of the list is drawn on a page (in the default user space of the page): the
/// unit square of the image mapped by the matrix of its drawing, in the order of the drawing - the
/// last one is the topmost (B64 / R4-12, as PDFSnapper). The images of all layers.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImagePlacement
{
public:
    PDFDocumentImageList::Key key;
    QPolygonF quad;     ///< The four corners of the image in the user space of the page

    /// The drawings of the images of a page, nothing decoded (the walk of the core content processor;
    /// the text and the annotations are not walked). Runs in the GUI thread for one page.
    static std::vector<PDFAppToolImagePlacement> collect(const PDFDocument* document, PDFInteger pageIndex, const PDFCMSPointer& cms);

    /// The topmost drawing at the point, nullptr = none
    static const PDFAppToolImagePlacement* findTopmost(const std::vector<PDFAppToolImagePlacement>& placements, const QPointF& point);
};

/// The mark of the selected image over the page of Extract Images (picked-image 341:7516): the
/// drawings of the image on the shown page outlined in the accent color; a click on the page
/// selects the topmost image under the pointer (B64). A child of the stage (addOverlay); the GUI
/// holds it by QPointer. imageClicked is emitted last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImagePlacementOverlay : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolImagePlacementOverlay(PDFAppToolPageStage* stage);
    virtual ~PDFAppToolImagePlacementOverlay() override;

    void setPlacements(std::vector<PDFAppToolImagePlacement> placements);
    const std::vector<PDFAppToolImagePlacement>& getPlacements() const { return m_placements; }

    void setSelectedKey(const PDFDocumentImageList::Key& key);
    const PDFDocumentImageList::Key& getSelectedKey() const { return m_selectedKey; }

    /// Matrix from the user space of the page to the overlay (identity without a page)
    QTransform getPageToOverlayTransform() const;

Q_SIGNALS:
    /// The user has clicked an image of the page
    void imageClicked(const QString& key);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPointer<PDFAppToolPageStage> m_stage;
    std::vector<PDFAppToolImagePlacement> m_placements;
    PDFDocumentImageList::Key m_selectedKey;
};

/// The empty state of the canvas of Extract Images (empty-state 341:8976) on bg/track; its texts
/// and the button are its children
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolExtractEmptyState : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppToolExtractEmptyState(QWidget* parent);
    virtual ~PDFAppToolExtractEmptyState() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// The result of the job of Extract Images: the list of the images of a version (the options of an
/// export with the states of the layers of the tab, R4-11) and what the parameters export of it
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolExtractCollection
{
public:
    std::shared_ptr<const PDFDocumentImageList> list;
    std::optional<PDFDocumentToolExtractImages::ExportPlan> plan;
    QString planError;
    std::shared_ptr<PDFDocumentToolExtractImages::ExportPlanCache> planCache;  ///< The hashes of the images of list (the next plans of the same list reuse them)
    quint64 version = 0;
    quint64 serial = 0;
};

/// The tool Extract Images (convert.extract-images, Figma 341:7516 ... 360:12757; plan of step 4 of
/// phase 2, 4.5, K4.5, K4.12, R4-11, R4-12; package P2): PDFAppToolImageList with the mark of the
/// export and the label hidden layer, the page with the selected image highlighted, the menu Pages
/// with the popover of a range, Output files, Image, Selected image, Copy Image (a click on the page
/// selects the topmost image in the drawing order, B64 in pdfsnapper.cpp).
///
/// The list and the plan of the export (PDFDocumentToolExtractImages::createImageList /
/// createExportPlan of the core - the names "Report_img_3.png", the identical images, the left out
/// ones) are computed by one asynchronous job (QtConcurrent, owner 2026-11-09) for every version and
/// every change of the parameters; the job holds the shared document and plain values, never the
/// GUI. The mark of a row (Space, the mark, Include all / Exclude all) writes the parameter exclude
/// (one undo step). Copy Image decodes the image by the decoding of the core (readImage) and puts
/// it on the clipboard.
///
/// Ownership: the shell owns the GUI; the list, the canvas, the overlay and the cards are children
/// of the panels and the canvas, held by QPointer; the decoder and the watcher are children of the
/// GUI.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIExtractImages : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    /// How the pages are chosen (341:7936)
    enum class PagesMode
    {
        All,
        Current,
        Range
    };

    static constexpr int PAGES_MENU_WIDTH = 250;
    static constexpr int FORMAT_MENU_WIDTH = 270;
    static constexpr int OPTIONS_MENU_WIDTH = 280;

    explicit PDFAppToolGUIExtractImages(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIExtractImages() override;

    /// Registers what is not registered yet (PDFAppBuiltinTools::registerAll)
    static void registerExtractImagesTools();

    /// The list of the images and the plan of the export of the current parameters (nullptr / none
    /// until the job has computed them)
    const PDFDocumentImageList* getImageList() const { return m_list.get(); }
    const std::optional<PDFDocumentToolExtractImages::ExportPlan>& getPlan() const { return m_plan; }
    bool isComputing() const { return m_isComputing; }

    PagesMode getPagesMode() const { return m_pagesMode; }
    void setPagesMode(PagesMode mode, const QString& rangeText = QString());

    /// The images left out by the user (the parameter exclude)
    std::vector<PDFDocumentImageList::Key> getExcludedKeys() const;

    /// Include all / Exclude all (341:7516) of the images of the chosen pages
    void includeAll();
    void excludeAll();

    /// Copy Image: the selected image on the clipboard (asynchronous; the status bar says it)
    void copyImage();

    /// Parts (nullptr = none)
    PDFAppToolImageList* getImageListWidget() const { return m_imageList.data(); }
    PDFAppToolPageCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppToolImagePlacementOverlay* getOverlay() const { return m_overlay.data(); }
    PDFAppToolSelectedItemCard* getSelectedCard() const { return m_selectedCard.data(); }
    PDFAppSettingControl* getCompressionControl() const { return m_compressionControl.data(); }
    PDFAppSettingControl* getQualityControl() const { return m_qualityControl.data(); }
    QToolButton* getPagesButton() const;
    QToolButton* getFormatButton() const;
    QToolButton* getCopyButton() const { return m_copyButton.data(); }
    QToolButton* getIncludeAllButton() const { return m_includeAllButton.data(); }
    QToolButton* getExcludeAllButton() const { return m_excludeAllButton.data(); }
    PDFTileRow* getPagesRow(PagesMode mode) const;
    PDFTileRow* getFormatRow(const QString& format) const;
    PDFAppToolImageFormatMenu* getFormatMenu() const { return m_formatMenu.data(); }
    QLineEdit* getRangeField() const { return m_rangeField.data(); }
    QWidget* getEmptyState() const { return m_emptyState.data(); }

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getConfirmText() const override;
    virtual QString getStatusDetailText() const override;
    virtual bool isFileSizeInStatusText() const override { return false; }  ///< 341:7516
    virtual void onParametersChanged(const QString& name) override;
    virtual void onDocumentChanged() override;
    virtual void onRunStateChanged(bool isRunning) override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void startComputation();
    void onComputationFinished();
    void updateParts();
    void updateRows();
    void updateHead();
    void updateToolbar();
    void updateSelectedCard();
    void updateCanvas();
    void updateOutputFiles();
    void setExcluded(const std::vector<PDFDocumentImageList::Key>& keys, bool isExcluded);
    std::vector<PDFInteger> getChosenPages() const;
    const PDFDocumentImageList::Item* getCurrentItem() const;
    QString getFileName(const PDFDocumentImageList::Item& item) const;

    void onImageSelectionChanged();
    void onMarkToggled(const std::vector<pdf::PDFDocumentImageList::Key>& keys, bool isMarked);
    void onPageImageClicked(const QString& key);
    void onPagesRowClicked();
    void onFormatChosen(const QByteArray& format);
    void onApplyRange();
    void onCancelRange();
    void onImagesDecoded(quint64 ticket, const QList<QImage>& images);
    void onSearchAllPages();

    std::shared_ptr<const PDFDocumentImageList> m_list;
    quint64 m_listVersion = 0;
    std::shared_ptr<PDFDocumentToolExtractImages::ExportPlanCache> m_planCache;    ///< The hashes of the images of m_list (RS4APP)
    std::optional<PDFDocumentToolExtractImages::ExportPlan> m_plan;
    QString m_planError;
    bool m_isComputing = false;
    bool m_isRunning = false;
    quint64 m_serial = 0;
    QPointer<QFutureWatcher<PDFAppToolExtractCollection>> m_watcher;
    std::shared_ptr<PDFAppToolExtractCollectionControl> m_control;

    PagesMode m_pagesMode = PagesMode::All;
    PDFInteger m_shownPage = -1;
    QPointer<PDFAppToolImageDecoder> m_decoder;
    quint64 m_copyTicket = 0;
    int m_copyNumber = 0;

    QPointer<PDFAppToolImageList> m_imageList;
    QPointer<PDFAppToolPageCanvas> m_canvas;
    QPointer<PDFAppToolImagePlacementOverlay> m_overlay;
    QPointer<QWidget> m_emptyState;
    QPointer<PDFLabel> m_emptyTitle;
    QPointer<PDFAppParagraph> m_emptyText;
    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<PDFAppToolSelectedItemCard> m_selectedCard;
    QPointer<PDFAppSettingControl> m_compressionControl;    ///< The field Compression of the card Image (341:7516)
    QPointer<PDFAppSettingControl> m_qualityControl;        ///< The field Quality of the card Image
    QPointer<PDFAppToolPanelPopup> m_pagesPanel;
    QPointer<PDFAppToolImageFormatMenu> m_formatMenu;
    std::map<int, QPointer<PDFTileRow>> m_pagesRows;
    QPointer<QWidget> m_rangeEditor;
    QPointer<QLineEdit> m_rangeField;
    QPointer<PDFLabel> m_rangeMessage;
    QPointer<QToolButton> m_includeAllButton;
    QPointer<QToolButton> m_excludeAllButton;
    QPointer<QToolButton> m_copyButton;
    bool m_isUpdatingSelection = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_EXTRACTIMAGES_H
