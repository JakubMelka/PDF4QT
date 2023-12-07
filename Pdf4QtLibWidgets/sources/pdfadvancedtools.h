//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFADVANCEDTOOLS_H
#define PDFADVANCEDTOOLS_H

#include "pdfwidgetsglobal.h"
#include "pdfwidgettool.h"
#include "pdfannotation.h"

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

/// Tool that creates free text note without callout line.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFCreateFreeTextTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateFreeTextTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
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
    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    PDFReal getPenWidth() const;
    void setPenWidth(PDFReal penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

    QColor getFillColor() const;
    void setFillColor(const QColor& fillColor);

private:
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);
    void finishDefinition();

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    Type m_type;
    PDFReal m_penWidth;
    QColor m_strokeColor;
    QColor m_fillColor;
    PDFInteger m_rectPageIndex = 0;
    QRectF m_rectOnPage;
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
                          QList<PDFRenderError>& errors) const override;

    PDFReal getPenWidth() const;
    void setPenWidth(PDFReal penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

    QColor getFillColor() const;
    void setFillColor(const QColor& fillColor);

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    PDFReal m_penWidth;
    QColor m_strokeColor;
    QColor m_fillColor;
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
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

    PDFReal getPenWidth() const;
    void setPenWidth(const PDFReal& penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

private:
    void resetTool();

    PDFToolManager* m_toolManager;
    PDFInteger m_pageIndex;
    std::vector<QPointF> m_pickedPoints;
    PDFReal m_penWidth;
    QColor m_strokeColor;
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
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void updateActions() override;
    virtual void setActiveImpl(bool active) override;

private:
    void onActionTriggered(QAction* action);
    void updateCursor();
    void setSelection(pdf::PDFTextSelection&& textSelection);

    struct SelectionInfo
    {
        PDFInteger pageIndex = -1;
        QPointF selectionStartPoint;
    };

    PDFToolManager* m_toolManager;
    QActionGroup* m_actionGroup;
    AnnotationType m_type;
    pdf::PDFTextSelection m_textSelection;
    SelectionInfo m_selectionInfo;
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

private:
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
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
                          QList<PDFRenderError>& errors) const override;

    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void updateActions() override;
    virtual void setActiveImpl(bool active) override;

private:
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
    bool m_isCursorOverText;
};

} // namespace pdf

#endif // PDFADVANCEDTOOLS_H
