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

#ifndef PDFAPPTOOLGUI_EXTRACTTEXT_H
#define PDFAPPTOOLGUI_EXTRACTTEXT_H

#include "pdfappdeclarativetoolgui.h"

#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcms.h"
#include "pdfrenderer.h"

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <optional>

class QScrollArea;
class QStackedWidget;

namespace pdf
{

class PDFAppCancelFlag;
class PDFAppDocumentView;
class PDFAppParagraph;
class PDFAppThumbnailsPanel;
class PDFAppToolCallout;
class PDFDrawWidgetProxy;
class PDFKeyValueList;
class PDFLabel;
class PDFSegmentedControl;

/// The picture of one page of the pane PAGE of Extract Text (339:8649): the compiled page of
/// the canvas drawn by the proxy of the view (its annotations, color mode and layers, as the
/// thumbnails), on the paper with the shadow effect/shadow. Fit scales it into the widget, 100 %
/// draws it in its size at 100 % (the widget asks for that size, a scroll area holds it). A page,
/// which is not compiled yet, is white until the compiler says it is ready.
///
/// A soft proof kept on the tab (PDFAppDocumentSoftProof) is only the view of the tab (Q7.10): the
/// picture of another tool (Extract Text, Audiobook) never has it. While the view proofs, its
/// compiled pages carry the proof colors, so the picture renders the page itself in one job
/// (QtConcurrent::run, the shared immutable document, the settings of the view withoutProofing,
/// renderPage) - never in the GUI thread. Only the picture of Soft Proofing shows the proof
/// (setProofShown). The job holds only copies; its late result is dropped (a cancel flag, the
/// watcher is a child of the picture).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPagePicture : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Largest side of the drawn picture (pixels)
    static constexpr int MAXIMUM_PICTURE_SIZE = 4096;

    explicit PDFAppToolPagePicture(QWidget* parent);
    virtual ~PDFAppToolPagePicture() override;

    /// Proxy of the canvas, whose compiled pages are drawn (nullptr = none); without a view
    /// (setView) the compiled page is drawn as it is
    void setProxy(PDFDrawWidgetProxy* proxy);

    /// The view of the tab: its proxy (setProxy), its document and its color management (a proof
    /// of the view is left out unless isProofShown); nullptr = none
    void setView(PDFAppDocumentView* view);

    /// The picture shows the soft proof of the view (only the picture of Soft Proofing)
    void setProofShown(bool isProofShown);
    bool isProofShown() const { return m_isProofShown; }

    /// The picture of the last paint (null = none yet) and a job renders it now (tests)
    const QImage& getImage() const { return m_image; }
    bool isRendering() const { return m_renderWatcher != nullptr; }

    /// Renders the page \p pageIndex of \p document into an image of \p imageSize (the rotated media box
    /// fitted to it) on \p paperColor with \p cmsSettings and \p features; any thread. A null image for a
    /// page, which does not exist or cannot be rendered, or a canceled \p control.
    static QImage renderPage(PDFDocumentPointer document, PDFInteger pageIndex, QSize imageSize, PDFCMSSettings cmsSettings,
                             PDFRenderer::Features features, QColor paperColor, std::shared_ptr<PDFAppCancelFlag> control);

    /// The page (index from 0, -1 = none)
    void setPageIndex(PDFInteger pageIndex);
    PDFInteger getPageIndex() const { return m_pageIndex; }

    /// Fit (true) or the size at 100 % (false)
    void setFit(bool isFit);
    bool isFit() const { return m_isFit; }

    /// Rectangle of the paper in the widget (the last paint, tests)
    QRect getPaperRect() const { return m_paperRect; }

    /// Size of the page at 100 % in logical pixels (its points at the resolution of the screen)
    QSize getActualSize() const;

    virtual QSize sizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void onPageImageChanged(bool all, const std::vector<pdf::PDFInteger>& pages);
    void onProxyDestroyed();
    void invalidate();

    /// The settings of the color management of the picture: of the view, without its proof unless
    /// isProofShown. \returns false, when the compiled page of the proxy has them (no proof to leave out)
    bool getOwnCMSSettings(PDFCMSSettings* settings) const;

    void startRender(const QSize& pixelSize, const PDFCMSSettings& settings);
    void cancelRender();
    void onRenderFinished();

    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<PDFAppDocumentView> m_view;
    PDFInteger m_pageIndex = -1;
    bool m_isFit = true;
    bool m_isProofShown = false;
    QImage m_image;
    QSize m_imagePixelSize;
    mutable QRect m_paperRect;
    QPointer<QFutureWatcher<QImage>> m_renderWatcher;
    std::shared_ptr<PDFAppCancelFlag> m_renderCancel;
    QSize m_renderPixelSize;                ///< The size of the picture, which the job renders
};

/// The canvas of Extract Text (document-canvas 339:8145): the bar "Page 308 · preview of the
/// extracted text" with Fit / 100 %, the panes PAGE and EXTRACTED TEXT (381 x 726 each, gap 12)
/// and the estimate of the whole document. Its texts are set by the GUI.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTextPreviewPage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Paddings and gaps of 339:8145
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;

    explicit PDFAppToolTextPreviewPage(QWidget* parent);
    virtual ~PDFAppToolTextPreviewPage() override;

    PDFLabel* getTitleLabel() const { return m_title; }
    PDFSegmentedControl* getZoomControl() const { return m_zoom; }
    PDFAppToolPagePicture* getPicture() const { return m_picture; }
    QScrollArea* getPictureArea() const { return m_pictureArea; }
    PDFLabel* getPageFooter() const { return m_pageFooter; }
    PDFAppParagraph* getTextLabel() const { return m_text; }
    QScrollArea* getTextArea() const { return m_textArea; }
    PDFLabel* getTextFooter() const { return m_textFooter; }
    PDFLabel* getEstimateLabel() const { return m_estimate; }
    PDFLabel* getEstimateDetailLabel() const { return m_estimateDetail; }

    /// The panes are shown (a document, which refuses the tool, shows an empty canvas, 339:10328)
    void setContentShown(bool isShown);

    /// Texts of the bar, of the panes and of the estimate
    void setTitle(const QString& text);
    void setPageFooter(const QString& text);
    void setText(const QString& text);
    void setTextFooter(const QString& text);
    void setEstimate(const QString& text, const QString& detail);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onZoomChanged(int index);

    /// The texts of the page itself (the captions of the panes, Fit / 100 %), also in a new language
    void updateTexts();

    QPointer<QWidget> m_content;
    QPointer<PDFLabel> m_title;
    QPointer<PDFSegmentedControl> m_zoom;
    QPointer<PDFLabel> m_pageCaption;
    QPointer<PDFLabel> m_textCaption;
    QPointer<QScrollArea> m_pictureArea;
    QPointer<PDFAppToolPagePicture> m_picture;
    QPointer<PDFLabel> m_pageFooter;
    QPointer<QScrollArea> m_textArea;
    QPointer<PDFAppParagraph> m_text;
    QPointer<PDFLabel> m_textFooter;
    QPointer<PDFLabel> m_estimate;
    QPointer<PDFLabel> m_estimateDetail;
};

/// The tool convert.extract-text (Figma 339:8112, 339:8765, 356:13079, 339:9601, 339:10328;
/// package 2D of the plan of the first batch): Order, Include and Format of the toolbar, the
/// page selection with the thumbnails in the left panel, the two panes of the canvas and the
/// sections Document, Settings, Result with the callout of the pages without text.
///
/// Ownership: the shell owns this GUI and its parts (the toolbar actions, the panels, the
/// canvas page); the GUI keeps them in QPointer. The runner of the preview of a page is a
/// child of this GUI and dies with it; its late result is dropped by the runner. The
/// thumbnails panel is a child of the left content.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIExtractText : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppToolGUIExtractText(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIExtractText() override;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and
    /// their GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerTools();

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getStatusText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const override;
    virtual QString getRunningCardStepText(const QString& stepText) const override;
    virtual QString getRunningTimeText(int seconds) const override;
    virtual QString getPermissionTitle() const override;
    virtual QString getPermissionSubtitle() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview) override;
    virtual void onDocumentChanged() override;

    /// Parts (tests)
    PDFAppToolTextPreviewPage* getPreviewPage() const { return m_previewPage; }
    PDFAppThumbnailsPanel* getThumbnailsPanel() const { return m_thumbnails; }
    PDFKeyValueList* getDocumentRows() const { return m_documentRows; }
    PDFKeyValueList* getSettingsRows() const { return m_settingsRows; }
    PDFKeyValueList* getResultRows() const { return m_resultRows; }
    PDFAppToolCallout* getNoTextCallout() const { return m_noTextCallout; }
    PDFDocumentToolRunner* getPageRunner() const { return m_pageRunner; }

    /// The page shown by the panes (-1 = none)
    PDFInteger getPreviewPageIndex() const;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;

private:
    void ensurePageRunner();
    void requestPagePreview();
    void onDisplayedPageChanged(pdf::PDFInteger pageIndex);
    void onPagePreviewFinished(const pdf::PDFDocumentToolResult& result);
    void onPagePreviewDropped();
    void updateButtons();
    void updateActionsEnabled();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);
    void updateRows();
    void updatePreviewPage();

    /// "Plain text · UTF-8"
    QString getFormatText() const;

    /// The value of a number of the preview, which is an estimate ("about 412,880")
    QString getEstimateText(qint64 value) const;

    QPointer<PDFAppToolEnumMenu> m_orderMenu;
    QPointer<PDFAppToolFlagsMenu> m_includeMenu;
    QPointer<PDFAppToolEnumMenu> m_formatMenu;
    QPointer<PDFAppThumbnailsPanel> m_thumbnails;
    QPointer<PDFKeyValueList> m_documentRows;
    QPointer<PDFKeyValueList> m_settingsRows;
    QPointer<PDFKeyValueList> m_resultRows;
    QPointer<PDFAppToolCallout> m_noTextCallout;
    QPointer<PDFAppToolTextPreviewPage> m_previewPage;
    QPointer<PDFDocumentToolRunner> m_pageRunner;
    QPointer<PDFAppDocumentView> m_connectedView;
    std::optional<PDFDocumentToolResult> m_preview;     ///< Estimate of the selected pages
    std::optional<PDFDocumentToolResult> m_pagePreview; ///< Text of the shown page
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_EXTRACTTEXT_H
