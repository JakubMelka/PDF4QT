// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFADVANCEDTOOLS_H
#define PDFADVANCEDTOOLS_H

#include "pdfwidgetsglobal.h"
#include "pdfwidgettool.h"
#include "pdfannotation.h"
#include "pdfannotationstyle.h"

class QActionGroup;

namespace pdf
{

/// Tool that creates 'sticky note' annotations. Multiple types of sticky
/// notes are available, user can select a type of sticky note. When
/// user select a point, popup window appears and user can enter a text.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateStickyNoteTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateStickyNoteTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

protected:
    virtual void updateActions() override;

private:
    void onActionTriggered(QAction* action);
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);

    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    PDFPickTool* m_pickTool;
    TextAnnotationIcon m_icon;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateAnnotationTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateAnnotationTool(PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

protected:
    virtual void updateActions() override;
};

/// Tool that creates url link annotation. Multiple types of link highlights
/// are available, user can select a link highlight. When link annotation
/// is clicked, url address is triggered.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateHyperlinkTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    LinkHighlightMode getHighlightMode() const;
    void setHighlightMode(const LinkHighlightMode& highlightMode);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    LinkHighlightMode m_highlightMode = LinkHighlightMode::Outline;
};

/// Tool that creates link annotation pointing to destination in the same PDF document.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateInDocumentHyperlinkTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateInDocumentHyperlinkTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

    LinkHighlightMode getHighlightMode() const;
    void setHighlightMode(const LinkHighlightMode& highlightMode);

protected:
    virtual void updateActions() override;
    virtual void setActiveImpl(bool active) override;

private:
    void onActionTriggered(QAction* action);
    void onLinkRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void onTargetPagePicked(pdf::PDFInteger pageIndex);
    void onTargetRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void createLinkAnnotation(const PDFDestination& destination);
    PDFDestination createDestination(pdf::PDFInteger pageIndex, QRectF pageRectangle) const;
    bool isRectangleDestination() const;
    void resetPendingLink();

    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    PDFPickTool* m_pickTool;
    DestinationType m_destinationType = DestinationType::Fit;
    bool m_inheritZoom = false;
    LinkHighlightMode m_highlightMode = LinkHighlightMode::Outline;
    PDFInteger m_linkPageIndex = -1;
    QRectF m_linkRectangle;
    bool m_isPickingTarget = false;
};

/// Tool that creates free text note without callout line.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateFreeTextTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateFreeTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

private:
    bool configureFreeText(QString& text);
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    PDFFreeTextStyle m_style;
    bool m_autoResizeToContents = true;
};

/// Tool that creates line/polyline/polygon annotations.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateLineTypeTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    enum class Type
    {
        Line,
        PolyLine,
        Polygon,
        Rectangle
    };

    explicit PDFCreateLineTypeTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, Type type, QAction* action, QObject* parent);

    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;
    virtual void setActiveImpl(bool active) override;

    PDFReal getPenWidth() const;
    void setPenWidth(PDFReal penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

    QColor getFillColor() const;
    void setFillColor(const QColor& fillColor);

private:
    bool canHaveOrthogonalMode() const;
    bool isOrthogonalMode() const;

    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void onStyleChanged(const pdf::PDFAnnotationStyle& style);
    void finishDefinition();

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    PDFAnnotationStyleManager* m_styleManager;
    Type m_type;
    PDFInteger m_rectPageIndex = 0;
    QRectF m_rectOnPage;
    bool m_orthogonalMode = false;
};

/// Tool that creates ellipse annotation.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateEllipseTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateEllipseTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void setActiveImpl(bool active) override;

    PDFReal getPenWidth() const;
    void setPenWidth(PDFReal penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

    QColor getFillColor() const;
    void setFillColor(const QColor& fillColor);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void onStyleChanged(const pdf::PDFAnnotationStyle& style);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    PDFAnnotationStyleManager* m_styleManager;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateFreehandCurveTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateFreehandCurveTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    virtual void setActiveImpl(bool active) override;

    PDFReal getPenWidth() const;
    void setPenWidth(const PDFReal& penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

private:
    void resetTool();

    PDFToolManager* m_toolManager;
    PDFAnnotationStyleManager* m_styleManager;
    PDFInteger m_pageIndex;
    std::vector<QPointF> m_pickedPoints;
};

/// Tool that creates 'stamp' annotations. Multiple types of stamps
/// are available, user can select a type of stamp (text).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateStampTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateStampTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void updateActions() override;

private:
    void onActionTriggered(QAction* action);
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);

    pdf::PDFInteger m_pageIndex;
    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    PDFPickTool* m_pickTool;
    PDFStampAnnotation m_stampAnnotation;
};

/// Tool for highlighting of text in document
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateHighlightTextTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:

    /// Creates new highlight text tool
    /// \param proxy Proxy
    /// \param type Annotation type, must be one of: Highlight, Underline, Squiggly, StrikeOut
    /// \param actionGroup Action group with actions. Each action must define annotation type.
    /// \param parent Parent
    explicit PDFCreateHighlightTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void updateActions() override;
    virtual void setActiveImpl(bool active) override;

private:
    void onActionTriggered(QAction* action);
    void onStyleChanged(const pdf::PDFAnnotationStyle& style);
    void updateCursor();
    void setSelection(pdf::PDFTextSelection&& textSelection);

    /// Returns the identifier of the persisted style for the current annotation
    /// type, so each kind of the text markup remembers its own color.
    QString getStyleId() const;

    /// Returns the default color of the current annotation type
    QColor getDefaultColor() const;

    struct SelectionInfo
    {
        PDFInteger pageIndex = -1;
        QPointF selectionStartPoint;
    };

    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    PDFAnnotationStyleManager* m_styleManager;
    AnnotationType m_type;
    pdf::PDFTextSelection m_textSelection;
    SelectionInfo m_selectionInfo;
    QColor m_color;
    bool m_isCursorOverText;
};

/// Tool that creates redaction annotation from rectangle. Rectangle is not
/// selected from the text, it is just any rectangle.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateRedactRectangleTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateRedactRectangleTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    /// Returns the color to be used for newly created redaction annotations.
    /// The color is persisted in the application settings, so it is shared
    /// (and remembered across restarts) by all redaction tools.
    static QColor getRedactColor();
    static void setRedactColor(const QColor& color);

protected:
    virtual void setActiveImpl(bool active) override;

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void onStyleChanged(const pdf::PDFAnnotationStyle& style);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    PDFAnnotationStyleManager* m_styleManager;
    QColor m_color;
};

/// Tool that stamps a formatted page number into a user-picked rectangle on every
/// page of a selected page range. Unlike the other tools in this file, the text is
/// written directly into the page's content stream (not as an annotation).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateInsertPageNumbersTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateInsertPageNumbersTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
};

/// Tool for a fast deletion of annotations. Without this tool, annotations can be
/// deleted only one by one, using the context menu of the annotation. Annotation
/// under the mouse cursor is highlighted and it is deleted by a mouse click. It is
/// also possible to delete several annotations at once - by dragging a rectangle
/// over them.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFDeleteAnnotationTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFDeleteAnnotationTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void updateActions() override;
    virtual void setActiveImpl(bool active) override;

private:
    /// Annotation, which can be deleted by this tool
    struct AnnotationInfo
    {
        PDFInteger pageIndex = -1;
        PDFObjectReference pageReference;
        PDFObjectReference annotationReference;

        /// Rectangle of the annotation in the page coordinate system
        QRectF rectangle;
    };

    /// Returns annotations of the page, which can be deleted by this tool.
    /// Popup annotations and replies are not returned - they are deleted
    /// together with their parent annotation.
    /// \param pageIndex Page index
    std::vector<AnnotationInfo> getDeletableAnnotations(PDFInteger pageIndex) const;

    /// Returns annotations, which are currently marked for deletion (annotation
    /// under the mouse cursor, or all annotations inside the dragged rectangle).
    std::vector<AnnotationInfo> getMarkedAnnotations() const;

    /// Deletes the given annotations from the document
    void deleteAnnotations(const std::vector<AnnotationInfo>& annotations);

    /// Returns rectangle selected by the user in the page coordinate system.
    /// If the user is not dragging a rectangle, then invalid rectangle is returned.
    QRectF getSelectionRectangle() const;

    void resetTool();
    void updateCursor();

    PDFToolManager* m_toolManager;

    /// Page, on which the user started to drag the selection rectangle, -1 if
    /// the user is not dragging any rectangle.
    PDFInteger m_selectionPageIndex;

    /// Start point of the dragged selection rectangle, in page coordinates
    QPointF m_selectionStartPoint;

    /// Current mouse position in page coordinates
    QPointF m_currentPagePoint;

    /// Page under the mouse cursor, -1 if the cursor is not over any page
    PDFInteger m_currentPageIndex;

    bool m_isCursorOverAnnotation;
};

/// Tool for redaction of text in document. Creates redaction annotation from  text selection.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateRedactTextTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateRedactTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void updateActions() override;
    virtual void setActiveImpl(bool active) override;

private:
    void onStyleChanged(const pdf::PDFAnnotationStyle& style);
    void updateCursor();
    void setSelection(pdf::PDFTextSelection&& textSelection);

    struct SelectionInfo
    {
        PDFInteger pageIndex = -1;
        QPointF selectionStartPoint;
    };

    PDFToolManager* m_toolManager;
    pdf::PDFTextSelection m_textSelection;
    SelectionInfo m_selectionInfo;
    PDFAnnotationStyleManager* m_styleManager;
    QColor m_color;
    bool m_isCursorOverText;
};

} // namespace pdf

#endif // PDFADVANCEDTOOLS_H
