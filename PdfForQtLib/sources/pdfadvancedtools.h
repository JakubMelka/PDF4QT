//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFADVANCEDTOOLS_H
#define PDFADVANCEDTOOLS_H

#include "pdfwidgettool.h"
#include "pdfannotation.h"

class QActionGroup;

namespace pdf
{

/// Tool that creates 'sticky note' annotations. Multiple types of sticky
/// notes are available, user can select a type of sticky note. When
/// user select a point, popup window appears and user can enter a text.
class PDFFORQTLIBSHARED_EXPORT PDFCreateStickyNoteTool : public PDFWidgetTool
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

class PDFFORQTLIBSHARED_EXPORT PDFCreateAnnotationTool : public PDFWidgetTool
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
class PDFFORQTLIBSHARED_EXPORT PDFCreateHyperlinkTool : public PDFCreateAnnotationTool
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
class PDFFORQTLIBSHARED_EXPORT PDFCreateFreeTextTool : public PDFCreateAnnotationTool
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
class PDFFORQTLIBSHARED_EXPORT PDFCreateLineTypeTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    enum class Type
    {
        Line,
        PolyLine,
        Polygon
    };

    explicit PDFCreateLineTypeTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, Type type, QAction* action, QObject* parent);

    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<PDFRenderError>& errors) const override;

    PDFReal getPenWidth() const;
    void setPenWidth(PDFReal penWidth);

    QColor getStrokeColor() const;
    void setStrokeColor(const QColor& strokeColor);

    QColor getFillColor() const;
    void setFillColor(const QColor& fillColor);

private:
    void onPointPicked(PDFInteger pageIndex, QPointF pagePoint);
    void finishDefinition();

    PDFToolManager* m_toolManager;
    PDFPickTool* m_pickTool;
    Type m_type;
    PDFReal m_penWidth;
    QColor m_strokeColor;
    QColor m_fillColor;
};

/// Tool that creates ellipse annotation.
class PDFFORQTLIBSHARED_EXPORT PDFCreateEllipseTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateEllipseTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
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

class PDFFORQTLIBSHARED_EXPORT PDFCreateFreehandCurveTool : public PDFCreateAnnotationTool
{
    Q_OBJECT

private:
    using BaseClass = PDFCreateAnnotationTool;

public:
    explicit PDFCreateFreehandCurveTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QAction* action, QObject* parent);

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
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
class PDFFORQTLIBSHARED_EXPORT PDFCreateStampTool : public PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = PDFWidgetTool;

public:
    explicit PDFCreateStampTool(PDFDrawWidgetProxy* proxy, PDFToolManager* toolManager, QActionGroup* actionGroup, QObject* parent);

    virtual void drawPage(QPainter* painter, PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
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

} // namespace pdf

#endif // PDFADVANCEDTOOLS_H
