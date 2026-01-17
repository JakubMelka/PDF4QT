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

#ifndef PDFWIDGETANNOTATION_H
#define PDFWIDGETANNOTATION_H

#include "pdfwidgetsglobal.h"
#include "pdfannotation.h"
#include "pdfdocumentdrawinterface.h"

class QMimeData;

namespace pdf
{

struct PDFWidgetSnapshot;
class PDFDrawWidgetProxy;

/// Annotation manager for GUI rendering, it also manages annotations widgets
/// for parent widget.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidgetAnnotationManager : public PDFAnnotationManager, public IDrawWidgetInputInterface, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = PDFAnnotationManager;

public:
    explicit PDFWidgetAnnotationManager(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFWidgetAnnotationManager() override;

    virtual void setDocument(const PDFModifiedDocument& document) override;
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;

    virtual void drawPage(QPainter* painter,
                          PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;

    /// Returns tooltip generated from annotation
    virtual QString getTooltip() const override { return m_tooltip; }

    /// Returns current cursor
    virtual const std::optional<QCursor>& getCursor() const override { return m_cursor; }

    virtual int getInputPriority() const override { return AnnotationPriority; }

    void showAnnotationMenu(pdf::PDFObjectReference annotationReference,
                            pdf::PDFObjectReference pageReference,
                            QPoint globalMenuPosition);

    bool canAcceptAnnotationDrag(const QMimeData* data) const;
    bool handleAnnotationDrop(const QMimeData* data, const QPoint& widgetPos, Qt::DropAction action);

signals:
    void actionTriggered(const PDFAction* action);
    void documentModified(PDFModifiedDocument document);

private:
    void updateFromMouseEvent(QMouseEvent* event);
    bool beginAnnotationDrag(QMouseEvent* event);
    void startAnnotationDrag(QMouseEvent* event);
    const PDFAction* getLinkActionAtPosition(QPoint widgetPos) const;
    bool translateAnnotation(PDFDocumentBuilder* builder, PDFObjectReference annotationReference, const QPointF& delta);
    PDFObjectReference copyAnnotationToPage(PDFDocumentBuilder* builder,
                                            PDFObjectReference pageReference,
                                            PDFObjectReference annotationReference);

    void onShowPopupAnnotation();
    void onCopyAnnotation();
    void onEditAnnotation();
    void onDeleteAnnotation();

    /// Creates dialog for markup annotations. This function is used only for markup annotations,
    /// do not use them for other annotations (function can crash).
    /// \param widget Dialog's parent widget
    /// \param pageAnnotation Markup annotation
    /// \param pageAnnotations Page annotations
    QDialog* createDialogForMarkupAnnotations(PDFWidget* widget,
                                              const PageAnnotation& pageAnnotation,
                                              const PageAnnotations& pageAnnotations);

    /// Creates widgets for markup annotation main popup widget. Also sets
    /// default size of parent widget.
    /// \param parentWidget Parent widget, where widgets are created
    /// \param pageAnnotation Markup annotation
    /// \param pageAnnotations Page annotations
    void createWidgetsForMarkupAnnotations(QWidget* parentWidget,
                                           const PageAnnotation& pageAnnotation,
                                           const PageAnnotations& pageAnnotations);

    PDFDrawWidgetProxy* m_proxy;
    QString m_tooltip;
    std::optional<QCursor> m_cursor;
    QPoint m_editableAnnotationGlobalPosition; ///< Position, where action on annotation was executed
    PDFObjectReference m_editableAnnotation;    ///< Annotation to be edited or deleted
    PDFObjectReference m_editableAnnotationPage;    ///< Page of annotation above

    struct DragState
    {
        bool isActive = false;
        bool isDragging = false;
        bool isCopy = false;
        QPoint startDevicePos;
        PDFInteger pageIndex = -1;
        QTransform pageToDevice;
        QTransform deviceToPage;
        PDFObjectReference annotationReference;
        PDFObjectReference pageReference;
        PDFObjectReference popupReference;
        QRectF originalRect;
        std::vector<PDFReal> originalQuadPoints;
        QPointF cursorOffset;
    };
    DragState m_dragState;
};

}   // namespace pdf

#endif // PDFWIDGETANNOTATION_H
