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

#ifndef PDFAPPANNOTATIONCREATOR_H
#define PDFAPPANNOTATIONCREATOR_H

#include "pdfappglobal.h"
#include "pdfappannotationoperations.h"

#include "pdfaccenthue.h"
#include "pdfthemecolors.h"
#include "pdfaction.h"
#include "pdfannotation.h"
#include "pdfwidgettool.h"

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QRect>

#include <vector>

namespace pdf
{

class PDFAppAnnotationController;
class PDFAppAnnotationToolBar;
class PDFAppContext;
class PDFAppDocumentView;
class PDFAppMarkupBar;
class PDFAppPageLinkTool;
class PDFAppTextBoxPopup;
class PDFAppTextBoxTool;
class PDFAppWebLinkDialog;
class PDFCreateEllipseTool;
class PDFCreateFreehandCurveTool;
class PDFCreateHyperlinkTool;
class PDFCreateLineTypeTool;
class PDFCreateStampTool;
class PDFCreateStickyNoteTool;
class PDFDeleteAnnotationTool;
class PDFSelectTextTool;

/// Creation tool of the Annotate and Insert panels (package A of step 4)
enum class PDFAppAnnotationToolKind
{
    None,
    Highlight,          ///< Text markups (the text selection tool marks the dragged text)
    Underline,
    Strikeout,
    Squiggly,
    HighlightComment,   ///< Highlight with the contents of the compose popup
    TextBox,            ///< Free text: a rectangle, then the text box popup
    StickyNote,         ///< Text annotation: a point, then the compose popup
    Line,
    Polyline,
    Rectangle,
    Polygon,
    Ellipse,
    Freehand,
    WebLink,            ///< Link with a URI: a rectangle, then the web link dialog
    PageLink,           ///< Link to a page: the area, then the target (two steps)
    Stamp,
    DeleteTool          ///< Delete annotations
};

/// Static description of the creation tools: names, icons, commands, hints of
/// the status bar, the variants (sticky note icons, stamps, link targets)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationTools
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppAnnotationTools)

public:
    PDFAppAnnotationTools() = delete;

    /// All tools (without None) in the order of the panels
    static std::vector<PDFAppAnnotationToolKind> getKinds();

    /// Command of the tool (the sticky notes share insert.stickyNote, the stamps insert.stamp)
    static QString getCommandId(PDFAppAnnotationToolKind kind);

    /// Tool of the command (None for another command)
    static PDFAppAnnotationToolKind getKind(const QString& commandId);

    /// Icon of the tool (Figma panels 877:10157 / 196:3160)
    static QString getIconName(PDFAppAnnotationToolKind kind);

    /// Name of the tool (translated)
    static QString getName(PDFAppAnnotationToolKind kind);

    /// Hint of the status bar while the tool is active (translated)
    static QString getStatusHint(PDFAppAnnotationToolKind kind);

    /// Hint shown in the tool bar of a tool without colors (text box, web link,
    /// stamp, delete; translated), empty for the others
    static QString getBarHint(PDFAppAnnotationToolKind kind);

    /// Description of the undo step of an annotation created by the tool (translated)
    static QString getUndoDescription(PDFAppAnnotationToolKind kind);

    /// The tool marks the text (the four markups and Highlight and comment)
    static bool isMarkup(PDFAppAnnotationToolKind kind);

    /// Annotation type of a markup tool (Highlight for Highlight and comment,
    /// Invalid for another tool)
    static AnnotationType getMarkupType(PDFAppAnnotationToolKind kind);

    /// Markup tool of the type (None for a type, which is not a text markup)
    static PDFAppAnnotationToolKind getMarkupKind(AnnotationType type);

    /// The tool has a color row in its bar
    static bool hasColor(PDFAppAnnotationToolKind kind);

    /// The tool has the Stroke / Fill target (closed shapes)
    static bool hasFill(PDFAppAnnotationToolKind kind);

    /// The tool has the line width select (shapes, freehand)
    static bool hasWidth(PDFAppAnnotationToolKind kind);

    /// The tool creates annotations by an engine tool (not the text selection)
    static bool isEngineTool(PDFAppAnnotationToolKind kind);

    /// Icons of the sticky notes in the order of the Insert panel (Comment first)
    static std::vector<TextAnnotationIcon> getStickyNoteIcons();
    static QString getStickyNoteIconName(TextAnnotationIcon icon);
    static QString getStickyNoteName(TextAnnotationIcon icon);

    /// Stamps in the order of the Insert panel with their names and hues (196:3285)
    static std::vector<Stamp> getStamps();
    static QString getStampName(Stamp stamp);
    static PDFAccentHue getStampHue(Stamp stamp);

    /// Targets of Link to page in the order of the Target menu (1170:27714):
    /// Whole page (Fit), Page width (FitH), Page height (FitV), Area (FitR),
    /// Position (XYZ)
    static std::vector<DestinationType> getPageLinkTargets();
    static bool isPageLinkTarget(DestinationType type);
    static QString getPageLinkTargetName(DestinationType type);
    static QString getPageLinkTargetDescription(DestinationType type);
    static QString getPageLinkTargetIconName(DestinationType type);

    /// Instruction of the step 2 of Link to page ("Go to the target page and click it")
    static QString getPageLinkInstruction(DestinationType type);

    /// Line widths of the width select (pt)
    static std::vector<PDFReal> getPenWidths();
};

/// Values of the creation tools remembered for the next use (the database of the
/// context through PDFAppAnnotationStyleStorage, owner decision O19 - every value
/// is read again at each use, a change of another instance is seen at once, an
/// unknown value gives the default): the colors of the markups, of the sticky
/// note, the icon of the sticky note, the stamp, the style and the border of the
/// text box, the target of Link to page, the last markup type
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationToolSettings
{
public:
    PDFAppAnnotationToolSettings() = delete;

    /// Ids of the rows of package A, which the engine does not know
    static QString styleStickyNote() { return QStringLiteral("StickyNote"); }
    static QString rowFreeTextBorder() { return QStringLiteral("tool.freeTextBorder"); }

    /// Canonical color of an annotation color token (PDFColorSwatchRow)
    static QColor getCanonicalColor(PDFThemeColors::Color token);

    /// Default color of a markup type (tools.md 9.8: Highlight yellow, Underline
    /// blue, Strikeout and Squiggly red)
    static QColor getDefaultMarkupColor(AnnotationType type);

    /// Style id of the engine of a markup type
    static QString getMarkupStyleId(AnnotationType type);

    static QColor getMarkupColor(AnnotationType type);
    static void setMarkupColor(AnnotationType type, const QColor& color);

    static AnnotationType getLastMarkupType(const PDFAppContext* context);
    static void setLastMarkupType(const PDFAppContext* context, AnnotationType type);

    static TextAnnotationIcon getStickyNoteIcon(const PDFAppContext* context);
    static void setStickyNoteIcon(const PDFAppContext* context, TextAnnotationIcon icon);
    static QColor getStickyNoteColor();
    static void setStickyNoteColor(const QColor& color);

    static Stamp getStamp(const PDFAppContext* context);
    static void setStamp(const PDFAppContext* context, Stamp stamp);

    static PDFFreeTextStyle getFreeTextStyle(const PDFAppContext* context);
    static void setFreeTextStyle(const PDFAppContext* context, const PDFFreeTextStyle& style);

    /// Border of the text box: its color (invalid = none) and width
    static void getFreeTextBorder(const PDFAppContext* context, QColor* color, PDFReal* width);
    static void setFreeTextBorder(const PDFAppContext* context, const QColor& color, PDFReal width);

    /// Target of Link to page (Whole page - Fit - by default) and Keep the reader's zoom
    static DestinationType getPageLinkTarget(const PDFAppContext* context);
    static bool isPageLinkKeepZoom(const PDFAppContext* context);
    static void setPageLinkTarget(const PDFAppContext* context, DestinationType type, bool keepZoom);
};

/// Creation of annotations of one document view (package A of step 4): the
/// creation tools of Annotate and Insert, their floating tool bar, the markup bar
/// over a text selection, the compose flows (sticky note, Highlight and comment -
/// the compose mode of PDFAppCommentPopup of package B), the text box popup, the
/// web link dialog and Link to page in two steps.
///
/// - **Tools.** startTool activates a tool (refused for a locked document or one
///   without the permission, O20), the tool stays active until Esc, the close
///   button of its bar or another tool (tools.md 9.8). The markups use the text
///   selection tool (the dragged text is marked at the release; the annotations
///   are not selectable meanwhile), the other tools are engine tools of the tool
///   manager (PDFAppAnnotationController::addTool) or the tools of the package
///   (PDFAppTextBoxTool, PDFAppPageLinkTool).
/// - **Commit.** Every created annotation is one undo step committed by the
///   controller (an application change PDFAppAnnotationChangeKind::Tool, or the
///   engine change of a tool with the pending description of the tool).
/// - **Selection first.** markTextSelection marks the selected text at once,
///   commentTextSelection opens the compose popup for Highlight and comment.
///
/// The creator lives as long as the command handler of the package (one per
/// document tab). Every slot survives the deletion of the tab, the view and the
/// creator itself (requests come from the event loop, popups and dialogs are
/// guarded by QPointer). The GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationCreator : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Target of the color of a closed shape
    enum class ColorTarget
    {
        Stroke,
        Fill
    };

    explicit PDFAppAnnotationCreator(PDFAppDocumentView* view, QObject* parent);
    virtual ~PDFAppAnnotationCreator() override;

    PDFAppDocumentView* getView() const;
    PDFAppAnnotationController* getController() const;
    PDFAppContext* getContext() const;

    // ------------------------------------------------------------------ tools

    /// Active tool of the creator (None = the text selection of the view)
    PDFAppAnnotationToolKind getActiveKind() const { return m_activeKind; }

    /// Tool of the last activation (also after the tool has ended) - the active
    /// tile of the panels
    PDFAppAnnotationToolKind getLastKind() const { return m_lastKind; }

    /// Annotations can be created (PDFAppAnnotationController::isAnnotationEditingAllowed)
    bool isCreationAllowed() const;

    /// Activates the tool (another active tool ends first). \returns false, when
    /// the annotations cannot be changed (a status message says why) or the tool
    /// cannot be activated.
    bool startTool(PDFAppAnnotationToolKind kind);

    /// Starts the tool from the event loop (a panel, which asks for it, closes first)
    void requestTool(PDFAppAnnotationToolKind kind);

    /// Ends the active tool (the text selection becomes active, an open text box
    /// popup or a composed comment of the creator is canceled)
    void endTool();

    /// Engine tool of the kind (nullptr for the markups and None)
    PDFWidgetTool* getTool(PDFAppAnnotationToolKind kind) const;
    PDFAppTextBoxTool* getTextBoxTool() const;
    PDFAppPageLinkTool* getPageLinkTool() const;

    // ------------------------------------------------------------------ variants

    TextAnnotationIcon getStickyNoteIcon() const;
    void setStickyNoteIcon(TextAnnotationIcon icon);

    Stamp getStamp() const;
    void setStamp(Stamp stamp);

    DestinationType getPageLinkTarget() const;
    bool isPageLinkKeepZoom() const;
    void setPageLinkTarget(DestinationType type, bool keepZoom);

    AnnotationType getLastMarkupType() const;

    // ------------------------------------------------------------------ style

    /// Color target of the shape bar (Stroke / Fill)
    ColorTarget getColorTarget() const { return m_colorTarget; }
    void setColorTarget(ColorTarget target);

    /// Color of the tool (stroke or fill; an invalid color = no fill), the
    /// canonical light-theme value
    QColor getColor(PDFAppAnnotationToolKind kind, ColorTarget target) const;
    void setColor(PDFAppAnnotationToolKind kind, ColorTarget target, const QColor& color);

    /// Line width of the shape tools and of the freehand curve
    PDFReal getPenWidth(PDFAppAnnotationToolKind kind) const;
    void setPenWidth(PDFAppAnnotationToolKind kind, PDFReal penWidth);

    /// Reads the styles of the tools again (a change of another instance)
    void reloadStyles();

    /// Reads the styles of the tools again from the event loop: a change of a
    /// style by the bar and its notification by the database (own commits are
    /// notified from the event loop too) read the styles once
    void scheduleStyleReload();

    /// A reload of the styles waits for the event loop
    bool isStyleReloadPending() const { return m_isStyleReloadPending; }

    // ------------------------------------------------------------------ text selection

    /// Text is selected by the text selection tool
    bool hasTextSelection() const;

    /// Bounding rectangle of the visible part of the text selection in the
    /// coordinates of the canvas area (an empty rectangle without a visible selection)
    QRect getTextSelectionRect() const;

    /// Quadrilaterals of the selected text, one part per page
    std::vector<PDFAppTextMarkupPart> getTextSelectionParts() const;

    /// Marks the selected text by the type (one undo step, the selection is
    /// cleared). \returns false without a selection or when it cannot be changed.
    bool markTextSelection(AnnotationType type);

    /// Highlight and comment of the selected text: the compose popup opens over
    /// the selection, Post creates one highlight with the text (one undo step),
    /// Esc creates nothing. \returns false, when the popup cannot be opened.
    bool commentTextSelection();

    /// Copies the selected text to the clipboard
    void copyTextSelection();

    // ------------------------------------------------------------------ parts

    PDFAppAnnotationToolBar* getToolBar() const;
    PDFAppMarkupBar* getMarkupBar() const;
    PDFAppTextBoxPopup* getTextBoxPopup() const;
    PDFAppWebLinkDialog* getWebLinkDialog() const;

    /// Annotations created by the last creation of the creator (tests, the
    /// selection of a new link)
    const std::vector<PDFObjectReference>& getLastCreated() const { return m_lastCreated; }

    /// Updates the markup bar (shown over a visible text selection, when the
    /// setting annotations.showMarkupBar is on and no tool of the creator is active)
    void updateMarkupBar();

Q_SIGNALS:
    /// The active tool has changed
    void activeKindChanged(pdf::PDFAppAnnotationToolKind kind);

    /// A remembered value of the tools has changed (a color, a width, an icon,
    /// a stamp, a target - the panels and the bar read them again)
    void toolStateChanged();

    /// Annotations have been created (one undo step)
    void annotationsCreated();

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class ComposeKind
    {
        None,
        StickyNote,
        HighlightComment
    };

    void createTools();
    void addTool(PDFWidgetTool* tool);
    void setToolDocuments();
    void finishTool();
    void setActiveKind(PDFAppAnnotationToolKind kind);
    void showToolBar(PDFAppAnnotationToolKind kind);
    void hideToolBar();
    void showStatusHint(const QString& text);
    void restoreStatusMessage();
    void applyToolStyle(PDFAppAnnotationToolKind kind);
    void cancelCompose();
    void cancelTextBox();
    bool commitResult(const PDFAppAnnotationResult& result, const QString& description, const std::vector<PDFObjectReference>& created);
    PDFAppNewAnnotation createAnnotationData() const;
    PDFSelectTextTool* getSelectTextTool() const;
    PDFAppAnnotationToolKind getKindOfTool(const PDFWidgetTool* tool) const;
    QRect pageRectToCanvas(PDFInteger pageIndex, const QRectF& pageRectangle) const;
    QWidget* getCanvasWidget() const;

    void onActiveToolChanged(pdf::PDFWidgetTool* tool);
    void onCreationRequested(const pdf::PDFAnnotationCreationRequest& request);
    void onEditingAllowedChanged();
    void onChangeCommitted();
    void onTextSelectionChanged();
    void onViewChanged();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onSettingsChanged();
    void onStyleReloadQueued();
    void onCanvasReleasedQueued();
    void onEndToolQueued();
    void onRequestedToolQueued();
    void onComposeAccepted(const QString& text);
    void onComposeCanceled();
    void onTextBoxAreaPicked(pdf::PDFInteger pageIndex, QRectF rectangle);
    void onTextBoxAccepted();
    void onTextBoxCanceled();
    void onTextBoxPreviewChanged();
    void onWebLinkFinished(int result);
    void onPageLinkRequested(pdf::PDFInteger pageIndex, QRectF rectangle, pdf::PDFDestination destination);
    void onPageLinkStepChanged();

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppAnnotationController> m_controller;
    QPointer<PDFToolManager> m_toolManager;
    QPointer<QWidget> m_canvasWidget;

    QPointer<PDFCreateStickyNoteTool> m_stickyNoteTool;
    QPointer<PDFCreateLineTypeTool> m_lineTool;
    QPointer<PDFCreateLineTypeTool> m_polylineTool;
    QPointer<PDFCreateLineTypeTool> m_rectangleTool;
    QPointer<PDFCreateLineTypeTool> m_polygonTool;
    QPointer<PDFCreateEllipseTool> m_ellipseTool;
    QPointer<PDFCreateFreehandCurveTool> m_freehandTool;
    QPointer<PDFCreateHyperlinkTool> m_webLinkTool;
    QPointer<PDFCreateStampTool> m_stampTool;
    QPointer<PDFDeleteAnnotationTool> m_deleteTool;
    QPointer<PDFAppTextBoxTool> m_textBoxTool;
    QPointer<PDFAppPageLinkTool> m_pageLinkTool;

    QPointer<PDFAppAnnotationToolBar> m_toolBar;
    QPointer<PDFAppMarkupBar> m_markupBar;
    QPointer<PDFAppTextBoxPopup> m_textBoxPopup;
    QPointer<PDFAppWebLinkDialog> m_webLinkDialog;

    PDFAppAnnotationToolKind m_activeKind = PDFAppAnnotationToolKind::None;
    PDFAppAnnotationToolKind m_lastKind = PDFAppAnnotationToolKind::None;
    PDFAppAnnotationToolKind m_requestedKind = PDFAppAnnotationToolKind::None;
    ColorTarget m_colorTarget = ColorTarget::Stroke;
    ComposeKind m_composeKind = ComposeKind::None;
    PDFAnnotationCreationRequest m_composeRequest;
    std::vector<PDFAppTextMarkupPart> m_composeParts;
    PDFAnnotationCreationRequest m_webLinkRequest;
    std::vector<PDFObjectReference> m_lastCreated;
    QString m_statusHint;
    QRect m_markupBarSelection;
    bool m_isStarting = false;
    bool m_isReleasePending = false;
    bool m_isStyleReloadPending = false;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppAnnotationToolKind)

#endif // PDFAPPANNOTATIONCREATOR_H
