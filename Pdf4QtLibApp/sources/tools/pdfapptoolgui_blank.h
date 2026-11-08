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

#ifndef PDFAPPTOOLGUI_BLANK_H
#define PDFAPPTOOLGUI_BLANK_H

#include "pdfappdeclarativetoolgui.h"
#include "pdfapppagesizepreview.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QSizeF>

#include <map>
#include <optional>

class QToolButton;

namespace pdf
{

/// The pages of the new document in the left panel of Blank PDF (thumbnails 337:2108): one
/// row per page (10 000 at most), all of one size. Qt::DisplayRole is the number of the page,
/// Qt::AccessibleTextRole "Page 2 of 3", PDFComponentRoles::PageSizeRole the size in points.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBlankPageModel : public QAbstractListModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractListModel;

public:
    explicit PDFAppBlankPageModel(QObject* parent);
    virtual ~PDFAppBlankPageModel() override;

    /// The pages: their number (rows are inserted or removed at the end) and their size (one
    /// dataChanged of all rows, when it changes)
    void setPages(int pageCount, const QSizeF& pageSize);

    int getPageCount() const { return m_pageCount; }
    const QSizeF& getPageSize() const { return m_pageSize; }

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

private:
    int m_pageCount = 0;
    QSizeF m_pageSize;
};

/// A page of the left panel of Blank PDF (thumbnail-1 337:2109): the tile 108 wide, the
/// paper of the proportions of the page fitted into 92 x 130 at the padding 8 (white with
/// border/default, the selected one with 2 px of bg/accent on bg/accent-subtle, radius 8),
/// the number Medium 11 6 px below it (text/secondary, text/accent when selected). The cell
/// of the item has the gaps of the grid (8 / 6, 337:2108) at its trailing and bottom sides,
/// so the grid of a QListView of the icon mode starts at the leading edge of the panel.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBlankPageDelegate : public PDFRowDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFRowDelegate;

public:
    /// Geometry of 337:2109
    static constexpr int TILE_WIDTH = 108;
    static constexpr int SLOT_WIDTH = 92;
    static constexpr int SLOT_HEIGHT = 130;
    static constexpr int PADDING = 8;
    static constexpr int LABEL_GAP = 6;
    static constexpr int LABEL_HEIGHT = 13;
    static constexpr int COLUMN_GAP = 8;
    static constexpr int ROW_GAP = 6;
    static constexpr int RADIUS = 8;

    explicit PDFAppBlankPageDelegate(QObject* parent);
    virtual ~PDFAppBlankPageDelegate() override;

    /// The paper of a page of the size in points fitted into the slot 92 x 130 (whole pixels,
    /// at least 1 x 1; the slot for an invalid size)
    static QSize getPaperSize(const QSizeF& pageSize);

    /// The tile of a page of the size (108 x 165 for A4 portrait, 108 x 87 for 16:9 landscape)
    static QSize getTileSize(const QSizeF& pageSize);

    /// The tile and the paper in the cell of the item (the leading side, mirrored in RTL)
    static QRect getTileRect(const QRect& cell, const QSizeF& pageSize, Qt::LayoutDirection direction);
    static QRect getPaperRect(const QRect& cell, const QSizeF& pageSize, Qt::LayoutDirection direction);

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;
};

/// The list of the pages of Blank PDF (thumbnails 337:2108): the icon mode with the grid of
/// the cells of PDFAppBlankPageDelegate from the leading edge of the panel - no margin of
/// PDFListView around the tiles (their tile has its own padding 8)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBlankPageView : public PDFListView
{
    Q_OBJECT

private:
    using BaseClass = PDFListView;

public:
    explicit PDFAppBlankPageView(QWidget* parent);
    virtual ~PDFAppBlankPageView() override;

    /// The cells follow the size of the pages (the grid of the icon mode)
    void setPageSize(const QSizeF& pageSize);
};

/// The panel of the button Size of Blank PDF (panel-page-size 337:2465, 280 wide): the rows
/// of the named sizes (added by the GUI), then the divider, the caption "SAME SIZE AS AN OPEN
/// DOCUMENT" and a row for each open document of the window (K27, F13 of the first batch: its
/// title, "Current page 1 · 216 × 279 mm"). The rows of the documents are made again, whenever
/// the panel opens (updateState); the group is hidden without a document.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBlankSizePanel : public PDFAppToolPanelPopup
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolPanelPopup;

public:
    /// Width of panel-page-size 337:2465
    static constexpr int PANEL_WIDTH = 280;

    explicit PDFAppBlankSizePanel(QWidget* parent);
    virtual ~PDFAppBlankSizePanel() override;

    /// Adds the group of the open documents below the rows of the sizes (once, after them)
    void addDocumentGroup();

    /// Makes the rows of the open documents of the window of the button again
    virtual void updateState() override;

    /// Rows of the open documents (tests)
    const std::vector<QPointer<PDFTileRow>>& getDocumentRows() const { return m_documentRows; }

Q_SIGNALS:
    /// A row of an open document has been chosen: the size of its current page in points
    /// (its crop box turned by /Rotate). Emitted last, after the panel has closed.
    void documentSizeChosen(QSizeF pageSize);

private:
    void onDocumentRowClicked();
    void clearDocumentRows();

    QPointer<QWidget> m_documentGroup;      ///< The divider, the caption and the rows of the documents
    std::vector<QPointer<PDFTileRow>> m_documentRows;
};

/// The tool Blank PDF in its own tab "New document" (convert.blank; Figma 337:1976 default,
/// 337:2221 the menu of the size, 356:9975 a custom size, 1390:47108 16:9; plan of step 2 of
/// phase 2, 4.6, K1, K12, K25-K27, Q14, Q17).
///
/// Toolbar: Size (the named sizes with their icons and dimensions, Custom, the open documents),
/// in a custom size the fields W and H in mm, Portrait / Landscape, the field Pages. Left: "Pages
/// · 3 blank pages" and the tiles of the pages. Canvas: PDFAppPageSizePreview. Right: Summary
/// (Pages, Page size, Orientation) and the blue callout; a failed run shows the red callout.
/// Create PDF replaces the tab by the new unsaved document (isResultDialogSkipped).
///
/// Rules of the values: 16:9 switches to Landscape (Q14, one undo step); Custom starts with
/// the size shown so far; Portrait / Landscape of a custom size swaps its width and height; an
/// open document fills Custom with the size of its current page (K27). A field, whose text is
/// not a value within its limits (Pages 1 - 10 000, a side 3 - 14 400 points), has the state
/// Error, the status bar says why and Create PDF is disabled (K26, Q17); a valid text is the
/// value of the preview at once and becomes one undo step, when the field is left (Enter, the
/// focus); Up / Down change the value by one at once.
///
/// Ownership: the shell owns this GUI and its parts; the GUI holds them in QPointer. The model
/// of the pages is a child of this GUI (the left panel is made again in a new language); the
/// delegate is a child of its view.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppBlankToolGUI : public PDFAppDeclarativeToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDeclarativeToolGUI;

public:
    explicit PDFAppBlankToolGUI(QObject* parent = nullptr);
    virtual ~PDFAppBlankToolGUI() override;

    /// Registers what is not registered yet: the tool in PDFDocumentToolRegistry and its GUI
    /// in PDFDocumentToolGUIRegistry (PDFAppBuiltinTools::registerAll)
    static void registerTools();

    /// Millimeters of a side for the texts (K25: at most one decimal, no trailing zero, the
    /// locale of the user interface: "215.9", "5,080")
    static QString formatMillimeters(double millimeters);

    virtual QWidget* createCanvasPage(QWidget* parent) override;
    virtual QString getConfirmText() const override;
    virtual QString getConfirmIconName() const override;
    virtual bool isConfirmEnabled() const override;
    virtual QString getStatusText() const override;
    virtual QVariantMap getRunParameters() const override;
    virtual void onParametersChanged(const QString& name) override;
    virtual void onRunStateChanged(bool isRunning) override;
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;
    virtual void retranslate() override;
    virtual bool isResultDialogSkipped() const override;
    virtual QString getOwnTabTitle() const override;
    virtual QString getOwnTabIconName() const override;
    virtual QString getOwnTabPageText() const override;
    virtual QString getOwnTabZoomText() const override;
    virtual QString getStartPageText(PDFAppToolStartPageText text) const override;

    /// The values of a run: the model with the valid texts of the fields, which have not been
    /// left yet (Ctrl+Enter in a field)
    QVariantMap getEffectiveValues() const;

    /// Size of the new pages in points of the effective values, empty for an invalid one
    QSizeF getEffectivePageSize() const;

    /// Index of the page shown by the preview (the current tile, from 0)
    int getCurrentPage() const { return m_currentPage; }

    /// Parts (tests)
    QToolButton* getSizeButton() const;
    PDFAppBlankSizePanel* getSizePanel() const { return m_sizePanel; }
    PDFTileRow* getSizeRow(const QString& sizeId) const;
    QToolButton* getPortraitButton() const { return m_portraitButton; }
    QToolButton* getLandscapeButton() const { return m_landscapeButton; }
    PDFToolBarField* getPagesField() const { return m_pagesField; }
    PDFToolBarField* getWidthField() const { return m_widthField; }
    PDFToolBarField* getHeightField() const { return m_heightField; }
    QWidget* getWidthGroup() const { return m_widthGroup; }
    QWidget* getHeightGroup() const { return m_heightGroup; }
    PDFAppToolPanelHeader* getPagesHeader() const { return m_pagesHeader; }
    PDFAppBlankPageView* getPageView() const { return m_pageView; }
    PDFAppBlankPageModel* getPageModel() const { return m_pageModel; }
    PDFKeyValueList* getSummary() const { return m_summary; }
    PDFAppToolCallout* getInfoCallout() const { return m_infoCallout; }
    PDFAppToolCallout* getErrorCallout() const { return m_errorCallout; }
    PDFAppPageSizePreview* getPreview() const { return m_preview; }

protected:
    virtual void describeToolbar(PDFAppToolPanelBuilder& builder) override;
    virtual void describeRightPanel(PDFAppToolPanelBuilder& builder) override;
    virtual void describeLeftPanel(PDFAppToolPanelBuilder& builder) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// A field of a value: Pages, W, H
    enum class Field
    {
        Pages,
        Width,
        Height
    };

    /// A field with its label in the toolbar (tf-Pages 337:2099, tf-W 356:10137)
    QWidget* createFieldGroup(Field field, const QString& label, QWidget* parent);

    PDFToolBarField* getField(Field field) const;
    static QString getParameterName(Field field);
    Field getFieldOf(const QObject* object) const;

    /// The value of the text of the field, std::nullopt for an invalid text; the reason of an
    /// invalid one in \p error
    std::optional<double> parseField(Field field, const QString& text, QString* error) const;

    /// Text of a value of a field in the locale of the user interface
    static QString formatFieldValue(Field field, double value);

    void onSizeRowClicked();
    void onDocumentSizeChosen(QSizeF pageSize);
    void onPortraitClicked();
    void onLandscapeClicked();
    void onFieldTextEdited(const QString& text);
    void onFieldEditingFinished();
    void onCurrentPageChanged(const QModelIndex& current, const QModelIndex& previous);
    void onPreviewZoomChanged();
    void onPageChangeQueued();

    /// Chooses a named size, Custom (with the size shown so far) or a size of a document
    void chooseSize(const QString& sizeId);
    void chooseCustomSize(const QSizeF& pageSize);
    void chooseOrientation(bool isLandscape);

    /// Writes the valid text of the field into the model (one undo step), \returns false for none
    bool commitField(Field field);

    /// Changes the value of the field by \p steps of one (Up / Down), one undo step
    void stepField(Field field, int steps);

    /// The text of the field has been changed by the user: its error and its pending value
    void applyFieldText(Field field, const QString& text);

    /// The field shows the value of the model (Undo, Redo, a new size); no signal
    void resetFieldParts(Field field);

    /// The parts show the values (no signal)
    void updateParts();

    /// The parts, then the errors of the inputs, the Primary button and the status bar: the
    /// signals of the GUI, the last thing a caller does
    void finishUpdate();
    void updateSizeMenu();
    void updateOrientationButtons();
    void updateFieldGroups();
    void updatePages();
    void updatePreview();
    void updateSummary();

    /// Title of the size: "A4", "16:9", "Custom"
    QString getSizeTitle(const QString& sizeId) const;

    /// "A4 portrait", "16:9 landscape", "Custom · 200 × 280 mm"
    QString getSizeDescription(const QVariantMap& values) const;

    /// "210 × 297 mm"
    static QString formatDimensions(const QSizeF& pageSize);

    /// The page count of the effective values (0 for an invalid one)
    int getEffectivePageCount() const;

    /// The first error of the fields, empty = none
    QString getFieldError() const;

    QPointer<PDFAppBlankSizePanel> m_sizePanel;
    std::map<QString, QPointer<PDFTileRow>> m_sizeRows;
    QPointer<QToolButton> m_portraitButton;
    QPointer<QToolButton> m_landscapeButton;
    QPointer<QWidget> m_widthGroup;
    QPointer<QWidget> m_heightGroup;
    QPointer<QWidget> m_widthDivider;
    QPointer<PDFToolBarField> m_pagesField;
    QPointer<PDFToolBarField> m_widthField;
    QPointer<PDFToolBarField> m_heightField;
    QPointer<PDFAppToolPanelHeader> m_pagesHeader;
    QPointer<PDFAppBlankPageView> m_pageView;
    QPointer<PDFAppBlankPageModel> m_pageModel;
    QPointer<PDFKeyValueList> m_summary;
    QPointer<PDFAppToolCallout> m_infoCallout;
    QPointer<PDFAppToolCallout> m_errorCallout;
    QPointer<PDFAppPageSizePreview> m_preview;

    /// Texts of the fields being edited (the value of the model, when not there) and their
    /// errors: they survive the parts made again in a new language
    std::map<Field, QString> m_fieldTexts;
    std::map<Field, QString> m_fieldErrors;

    QString m_runError;     ///< The reason of the last failed run, empty = none
    int m_currentPage = 0;
    bool m_isPageChangeQueued = false;
    bool m_isUpdatingFields = false;
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_BLANK_H
