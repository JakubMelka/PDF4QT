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

#ifndef PDFAPPTOOLGUI_PROTECT_H
#define PDFAPPTOOLGUI_PROTECT_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfdocumenttool_protect.h"
#include "pdfdocumenttoolshell.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QScrollArea>
#include <QToolButton>
#include <QWidget>

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace pdf
{

class PDFAppToolChecklist;
class PDFDrawWidgetProxy;
class PDFKeyValueList;
class PDFLabel;
class PDFListView;
class PDFTileRow;
class PDFWidget;

/// The tools protect.sanitize and protect.remove-links (package 2B of the plan of the first batch, chapter 9)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIProtect
{
public:
    PDFAppToolGUIProtect() = delete;

    /// Registers what is not registered yet: the tools in PDFDocumentToolRegistry and
    /// their GUI in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerTools();
};

/// The canvas of a tool, which shows a page of its own over the track of the canvas (the form
/// of Remove Hidden Data 438:12588): bg/track behind a scroll area of the content
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCanvasPage : public QScrollArea
{
    Q_OBJECT

private:
    using BaseClass = QScrollArea;

public:
    explicit PDFAppToolCanvasPage(QWidget* parent);
    virtual ~PDFAppToolCanvasPage() override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
};

/// Remove Hidden Data in the shell (protect.sanitize, Figma 438:12588, 438:12831, 438:13119,
/// 438:13356, 438:13607, 438:13839, 438:14093, 438:14342, 448:15959). The scan of the document
/// is the preview of the runner (it does not depend on the ticks: one preview per version); the
/// form of the canvas is the check list of the kinds bound to the Bool parameters. The menu
/// Preset writes the values of a preset as one undo step; its label says the preset of the values
/// or Custom (K18).
///
/// Ownership: the shell owns this GUI; the parts are children of the widgets of the shell (held
/// by QPointer, a part made again after a change of the language replaces the old one).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUISanitize : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    using Kind = PDFDocumentHiddenDataScan::Kind;
    using Preset = PDFDocumentToolSanitize::Preset;

    /// Width of the form of the canvas (438:12709) and its top
    static constexpr int FORM_WIDTH = 640;
    static constexpr int FORM_TOP = 20;
    static constexpr int FORM_GAP = 12;

    /// Width of the panel Preset (438:12831: 350 x 280)
    static constexpr int PRESET_PANEL_WIDTH = 350;

    explicit PDFAppToolGUISanitize(QObject* parent = nullptr);
    virtual ~PDFAppToolGUISanitize() override;

    /// Parts (nullptr = none)
    QToolButton* getPresetButton() const;
    PDFAppToolPanelPopup* getPresetPanel() const { return m_presetPanel.data(); }
    PDFTileRow* getPresetRow(Preset preset) const;
    PDFKeyValueList* getScanRows() const { return m_scanRows.data(); }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows.data(); }
    PDFKeyValueList* getSizeRows() const { return m_sizeRows.data(); }
    QWidget* getStepsCard() const { return m_stepsCard.data(); }
    QWidget* getEditingContent() const { return m_editingContent.data(); }
    QWidget* getResultContent() const { return m_resultContent.data(); }
    PDFTileRow* getKindRow(Kind kind) const;
    PDFAppToolCallout* getCleanCallout() const { return m_cleanCallout.data(); }
    PDFAppToolCallout* getInvisibleTextCallout() const { return m_invisibleTextCallout.data(); }
    QWidget* getForm() const { return m_form.data(); }

    /// The preset of the current values (Custom: K18)
    Preset getCurrentPreset() const;

    /// The scan of the document is known
    bool isScanned() const { return m_scan.has_value(); }

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QVariantMap getPreviewParameters() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QString getRunningTitle(int percentage) const override;
    virtual QString getRunningStatusText(const QString& stepText) const override;
    virtual QString getRunningTimeText(int seconds) const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview) override;
    virtual void retranslate() override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;

private:
    /// The form of the canvas, made again after a change of the language
    void buildForm();
    void updateParts();
    void updateForm();
    void updateResultContent();

    /// Kinds, which the document has (by the scan), all kinds before it
    std::vector<Kind> getPresentKinds() const;

    /// Kinds, which are chosen and the document has
    std::vector<Kind> getRemovedKinds() const;

    /// The kinds of the form (the signature fields only when the document has them)
    std::vector<Kind> getShownKinds() const;

    QString getKindDescription(Kind kind) const;
    static QString getPresetTitle(Preset preset);

    void onPresetRowClicked();
    void onShellStateChanged(pdf::PDFDocumentToolShell::State state);

    std::optional<PDFDocumentHiddenDataScan::Result> m_scan;
    qint64 m_pageCount = -1;
    mutable std::optional<PDFDocumentToolResult> m_dialogResult;    ///< The result of the dialog, which is opening
    std::optional<PDFDocumentToolResult> m_result;                  ///< The result shown beside the dialog (438:14342)
    bool m_isShellConnected = false;

    QPointer<PDFAppToolPanelPopup> m_presetPanel;
    std::map<Preset, QPointer<PDFTileRow>> m_presetRows;
    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<PDFKeyValueList> m_scanRows;
    QPointer<QWidget> m_editingContent;
    QPointer<QWidget> m_resultContent;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<QWidget> m_stepsCard;
    QPointer<PDFKeyValueList> m_sizeRows;
    QPointer<PDFAppToolCanvasPage> m_canvasPage;
    QPointer<QWidget> m_form;
    QPointer<PDFAppToolCallout> m_cleanCallout;
    QPointer<PDFAppToolCallout> m_invisibleTextCallout;
    std::vector<QPointer<PDFAppToolChecklist>> m_checklists;
};

/// Rows of the external links of Remove External Links (438:14751: the captions of the pages
/// and the links with their check boxes), drawn by PDFTileRowDelegate. A link is ticked, when it
/// is removed; the filter Show leaves the other links out of the rows only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolLinkListModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    /// The filter Show (438:15032)
    enum class Filter
    {
        All,
        Web,
        EMail
    };

    explicit PDFAppToolLinkListModel(QObject* parent);
    virtual ~PDFAppToolLinkListModel() override;

    /// Sets the links and the kept ids; the rows are made again
    void setLinks(std::vector<PDFDocumentLinkOperations::ExternalLink> links);
    const std::vector<PDFDocumentLinkOperations::ExternalLink>& getLinks() const { return m_links; }

    /// The kept links (the value of the parameter "exclude"); the check boxes follow
    void setKeptIds(std::set<QString> ids);
    const std::set<QString>& getKeptIds() const { return m_keptIds; }

    void setFilter(Filter filter);
    Filter getFilter() const { return m_filter; }

    /// The link of a row, nullptr for a caption or an invalid row
    const PDFDocumentLinkOperations::ExternalLink* getLink(int row) const;

    /// The row of a link, -1 = not shown
    int getRow(const QString& id) const;

    /// The links of the current filter
    std::vector<const PDFDocumentLinkOperations::ExternalLink*> getShownLinks() const;

    /// The link passes the filter
    static bool isShown(const PDFDocumentLinkOperations::ExternalLink& link, Filter filter);

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

Q_SIGNALS:
    /// The user has ticked or unticked a link (the check box of a row)
    void linkChecked(const QString& id, bool isChecked);

private:
    struct Row
    {
        PDFInteger pageIndex = -1;  ///< The page of a caption
        int linkIndex = -1;         ///< The link, -1 = the caption of a page
    };

    void rebuildRows();

    std::vector<PDFDocumentLinkOperations::ExternalLink> m_links;
    std::set<QString> m_keptIds;
    std::vector<Row> m_rows;
    Filter m_filter = Filter::All;
};

/// The frames of the links over the canvas of Remove External Links (438:14942: link-selected
/// bg/accent-subtle with border/focus 2, link-remove with bg/accent 1, link-kept text/tertiary)
/// and the caption of the shown page (preview-bar 438:14940 "Page 12"). The widget itself is the
/// caption, the frames are drawn into the pages by its draw interface.
///
/// Ownership: a child of the canvas area, which the shell deletes (the canvas area, when it dies
/// first); it holds the canvas and its proxy by QPointer and emits no signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolLinkOverlay : public QWidget, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// The caption at 20, 20 of the canvas (438:14940), 36 high
    static constexpr int CAPTION_OFFSET = 20;
    static constexpr int CAPTION_HEIGHT = 36;

    explicit PDFAppToolLinkOverlay(PDFWidget* pdfWidget, QWidget* canvasArea);
    virtual ~PDFAppToolLinkOverlay() override;

    /// The links, the kept ids and the selected link (empty = none)
    void setLinks(const std::vector<PDFDocumentLinkOperations::ExternalLink>* links, const std::set<QString>* keptIds, const QString& selectedId);

    /// The caption of the shown page
    void setPageText(const QString& text);
    QString getPageText() const;

    /// State of a link in the frames
    enum class LinkState
    {
        Removed,
        Kept,
        Selected
    };
    LinkState getLinkState(const PDFDocumentLinkOperations::ExternalLink& link) const;

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updatePlacement();
    void updateCanvas();

    QPointer<PDFWidget> m_pdfWidget;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QWidget> m_canvasArea;
    QPointer<QWidget> m_drawWidget;
    QPointer<PDFLabel> m_caption;
    std::map<PDFInteger, std::vector<PDFDocumentLinkOperations::ExternalLink>> m_pageLinks;
    std::set<QString> m_keptIds;
    QString m_selectedId;
};

/// Remove External Links in the shell (protect.remove-links, Figma 438:14751, 438:15032,
/// 438:15346, 438:15578, 438:15852, 438:16073). The links are the preview of the runner (one
/// per version); the parameter "exclude" holds the kept links, so Undo and Redo of the tool
/// tick them again. The filter Show is a state of the panel, no parameter: it changes the list
/// only, not what is removed; Select All and Select None act on the shown links.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIRemoveLinks : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    using Filter = PDFAppToolLinkListModel::Filter;

    /// The card of the links (438:14865: at most 455 high, padding 6, rows 2 apart)
    static constexpr int LIST_MAXIMUM_HEIGHT = 455;
    static constexpr int LIST_PADDING = 7;      ///< Padding 6 + the border

    /// Width of the panel Show (438:15032: 320 x 211)
    static constexpr int SHOW_PANEL_WIDTH = 320;

    explicit PDFAppToolGUIRemoveLinks(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIRemoveLinks() override;

    /// Parts (nullptr = none)
    PDFAppToolLinkListModel* getListModel() const { return m_model.data(); }
    PDFListView* getListView() const { return m_view.data(); }
    QWidget* getListCard() const { return m_listCard.data(); }
    QWidget* getEmptyCard() const { return m_emptyCard.data(); }
    PDFAppParagraph* getMoreHint() const { return m_moreHint.data(); }
    PDFAppToolPanelHeader* getLeftHeader() const { return m_leftHeader.data(); }
    PDFKeyValueList* getSummaryRows() const { return m_summaryRows.data(); }
    PDFAppToolLinkOverlay* getOverlay() const { return m_overlay.data(); }
    QToolButton* getSelectAllButton() const { return m_selectAllButton.data(); }
    QToolButton* getSelectNoneButton() const { return m_selectNoneButton.data(); }
    QToolButton* getShowButton() const;
    PDFAppToolPanelPopup* getShowPanel() const { return m_showPanel.data(); }
    PDFTileRow* getFilterRow(Filter filter) const;

    /// Links, which are removed / kept
    int getRemovedCount() const;
    int getKeptCount() const;

    /// The links of the document are known
    bool isListed() const { return m_isListed; }

    /// The id of the selected link (the row clicked), empty = none
    const QString& getSelectedId() const { return m_selectedId; }

    /// Select All, Select None (the shown links)
    void selectAll();
    void selectNone();

    /// The filter Show
    void setFilter(Filter filter);

    virtual QWidget* createOverlay(QWidget* canvasArea) override;
    virtual QVariantMap getPreviewParameters() const override;
    virtual QString getConfirmText() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview) override;

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateParts();
    void updateListHeight();
    void updateMoreHint();
    void updateOverlay();
    void writeKeptIds(std::set<QString> ids);
    std::set<QString> readKeptIds() const;
    static QString getFilterTitle(Filter filter);
    static QString getFilterIconName(Filter filter);

    void onLinkChecked(const QString& id, bool isChecked);
    void onCurrentRowChanged(const QModelIndex& current);
    void onFilterRowClicked();
    void onDisplayedPageChanged();

    std::vector<PDFDocumentLinkOperations::ExternalLink> m_links;
    PDFInteger m_internalLinkCount = 0;
    bool m_isListed = false;
    Filter m_filter = Filter::All;
    QString m_selectedId;

    QPointer<PDFAppToolLinkListModel> m_model;      ///< Owned by this GUI
    QPointer<PDFListView> m_view;
    QPointer<QObject> m_viewport;                   ///< The viewport of the list, compared by its address in the event filter
    QPointer<QWidget> m_listCard;
    QPointer<QWidget> m_emptyCard;
    QPointer<PDFAppParagraph> m_moreHint;
    QPointer<PDFAppToolPanelHeader> m_leftHeader;
    QPointer<PDFKeyValueList> m_summaryRows;
    QPointer<PDFAppToolLinkOverlay> m_overlay;
    QPointer<QToolButton> m_selectAllButton;
    QPointer<QToolButton> m_selectNoneButton;
    QPointer<PDFAppToolPanelPopup> m_showPanel;
    std::map<Filter, QPointer<PDFTileRow>> m_filterRows;
    bool m_isViewConnected = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_PROTECT_H
