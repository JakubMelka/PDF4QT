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

#ifndef PDFAPPTOOLGUI_PDFTOIMAGES_H
#define PDFAPPTOOLGUI_PDFTOIMAGES_H

#include "pdfappdocumentsearchindex.h"
#include "pdfappglobal.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolimageformatmenu.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfdocumenttool_pdftoimages.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"

#include <QComboBox>
#include <QFutureWatcher>
#include <QPointer>
#include <QToolButton>

namespace pdf
{

class PDFAppParagraph;

/// The tool PDF to Images (convert.pdf-to-images, Figma 339:3546 ... 339:7293; plan of step 4 of
/// phase 2, 4.4, K4.9, K4.11, R4-10 b, R4-19; package P1): the left panel has the page selection
/// and the thumbnails (the clicked one is the page of the preview, as Crop Pages), the canvas the
/// page of the preview with "Page 308 → Report_308.png · 2480 × 3508 px"; the toolbar Format (the
/// common formats and More formats - every format of the image writer, 339:4239, 356:11387), the
/// resolution ("300 dpi": 72, 150, 300, 600, Custom) and Options (Optimized write, Progressive scan
/// write - "Not available for PNG · JPEG only"); the right panel Output files, Image (Subtype,
/// Compression, Quality; what the format does not support is disabled) and Resolution (Size by,
/// Resolution or Longer side, Image size). The status bar says the estimate of the size: the page
/// of the preview is rendered and encoded by a worker (a sample of at most 2 megapixels), its
/// bytes times the pages ("about 2.1 GB").
///
/// The names of the files and the size of each image come from the core (PDFDocumentToolPdfToImages:
/// createFileNames, getPageGeometry) - the same as the run.
///
/// Ownership: the shell owns this GUI; the parts belong to the toolbar and the panels, the GUI
/// holds them by QPointer. The watcher of the estimate is a child of the GUI; its worker gets
/// values only (a shared document, the parameters, the color settings) and never this GUI.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIPdfToImages : public PDFAppPageGeometryToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPageGeometryToolGUI;

public:
    /// Width of the menus of the toolbar (339:4239)
    static constexpr int MENU_WIDTH = 260;

    /// The resolutions of the menu (339:4239); the rest is Custom
    static constexpr int MENU_RESOLUTIONS[] = { 72, 150, 300, 600 };

    /// Most pixels of the sample of the estimate
    static constexpr qint64 ESTIMATE_SAMPLE_PIXELS = 2 * 1024 * 1024;

    explicit PDFAppToolGUIPdfToImages(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIPdfToImages() override;

    /// Registers the tool and its GUI, when they are not registered yet (PDFAppBuiltinTools::registerAll)
    static void registerPdfToImagesTools();

    /// The bytes of the image of one page, estimated from a sample (a worker: values only). The
    /// page is rendered at most ESTIMATE_SAMPLE_PIXELS large, encoded by the format of the
    /// parameters, the bytes are scaled to the pixels of the real image; it is rendered as the run
    /// renders it (\p rendering: the settings and the layers of the tab, R4-10 b). -1 = it cannot be
    /// estimated or \p control canceled it (the rendering stops then, RS4APP).
    static qint64 estimatePageBytes(PDFDocumentPointer document, PDFInteger pageIndex, QVariantMap parameters, PDFDocumentToolRenderContext rendering,
                                    std::shared_ptr<PDFAppCancelFlag> control);

    /// The two contexts render the same (the color management, the features, the engine, the layers)
    static bool isSameRendering(const PDFDocumentToolRenderContext& first, const PDFDocumentToolRenderContext& second);

    /// The format of the values now
    PDFDocumentToolImageFormat getImageFormat() const;

    /// The geometry of the image of the page of the preview
    PDFDocumentToolPdfToImages::PageGeometry getPreviewGeometry() const;

    /// The names of the files of the pages of the selection and the error of the template
    const QStringList& getFileNames() const { return m_fileNames; }
    const QString& getNamesError() const { return m_namesError; }

    /// The estimated bytes of all images, -1 = not known yet
    qint64 getEstimatedSize() const;

    /// Text of the button of the resolution ("300 dpi", "2000 px")
    QString getResolutionText() const;

    /// The format of a row of the menu Format (one undo step)
    void selectFormat(const QByteArray& format);

    /// A resolution of the menu (Size by: Resolution and the dpi, one undo step)
    void selectResolution(int dpi);

    /// Parts (nullptr = none)
    PDFAppToolOutputFilesSection* getOutputFiles() const { return m_outputFiles; }
    QToolButton* getFormatButton() const;
    QToolButton* getResolutionButton() const;
    PDFAppToolPanelPopup* getFormatPanel() const { return m_formatMenu ? m_formatMenu->getPanel() : nullptr; }
    PDFAppToolImageFormatMenu* getFormatMenu() const { return m_formatMenu; }
    PDFAppToolPanelPopup* getResolutionPanel() const { return m_resolutionPanel; }
    QComboBox* getSubtypeComboBox() const { return m_subtypeComboBox; }
    PDFAppSettingControl* getCompressionControl() const { return m_compressionControl; }
    PDFAppSettingControl* getQualityControl() const { return m_qualityControl; }
    PDFAppSettingControl* getDpiControl() const { return m_dpiControl; }
    PDFAppSettingControl* getPixelsControl() const { return m_pixelsControl; }
    PDFKeyValueList* getImageSizeList() const { return m_imageSize; }

    /// The estimate has finished (tests wait for it)
    bool isEstimating() const;

    virtual QString getConfirmText() const override;
    virtual QString getStatusText() const override;
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual QString getSelectionLabel() const override;
    virtual void updateTool() override;
    virtual void onCanvasCreated() override;

private:
    void onFormatChosen(const QByteArray& format);
    void onResolutionRowClicked();
    void onCustomResolutionClicked();
    void onSubtypeActivated(int index);
    void onEstimateFinished();

    /// The parts of the panels and the toolbar of the values (no signal of this GUI)
    void updateParts();

    /// The names of the files of the values (the same as the run)
    void updateNames();

    /// Starts the estimate of the page of the preview, when its values have changed
    void startEstimate();

    /// The key of the values of the estimate (the identity and the version of the document, the
    /// page, the format, the size; RS4APP: never the address of a document, which a new one may reuse)
    QString getEstimateKey() const;

    /// The rendering of a run now (the settings and the layers of the tab)
    PDFDocumentToolRenderContext getRunRendering() const;

    QPointer<PDFAppToolOutputFilesSection> m_outputFiles;
    QPointer<PDFAppToolImageFormatMenu> m_formatMenu;
    QPointer<PDFAppToolPanelPopup> m_resolutionPanel;
    QPointer<PDFAppToolPanelPopup> m_optionsPanel;
    std::vector<QPointer<PDFTileRow>> m_resolutionRows;
    QPointer<QComboBox> m_subtypeComboBox;
    QPointer<PDFAppSettingControl> m_compressionControl;
    QPointer<PDFAppSettingControl> m_qualityControl;
    QPointer<PDFAppSettingControl> m_dpiControl;
    QPointer<PDFAppSettingControl> m_pixelsControl;
    QPointer<QWidget> m_dpiRow;
    QPointer<QWidget> m_pixelsRow;
    QPointer<PDFKeyValueList> m_imageSize;

    QStringList m_fileNames;
    QString m_namesError;

    QPointer<QFutureWatcher<qint64>> m_estimateWatcher;
    QString m_runningEstimateKey;   ///< The values of the running estimate (empty = none runs)
    QString m_estimateKey;          ///< The values of m_pageBytes
    qint64 m_pageBytes = -1;        ///< Estimated bytes of one page of the values of m_estimateKey
    PDFDocumentToolRenderContext m_runningEstimateRendering;    ///< The rendering of the running estimate
    PDFDocumentToolRenderContext m_estimateRendering;           ///< The rendering of m_pageBytes
    std::shared_ptr<PDFAppCancelFlag> m_estimateCancel;         ///< The cancellation of the running estimate (shared with its worker)
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_PDFTOIMAGES_H
