//    Copyright (C) 2023 Jakub Melka
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
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfannotation.h"

namespace pdf
{

class PDFDrawWidgetProxy;

/// Annotation manager for GUI rendering, it also manages annotations widgets
/// for parent widget.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidgetAnnotationManager : public PDFAnnotationManager, public IDrawWidgetInputInterface
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

    /// Returns tooltip generated from annotation
    virtual QString getTooltip() const override { return m_tooltip; }

    /// Returns current cursor
    virtual const std::optional<QCursor>& getCursor() const override { return m_cursor; }

    virtual int getInputPriority() const override { return AnnotationPriority; }

signals:
    void actionTriggered(const PDFAction* action);
    void documentModified(PDFModifiedDocument document);

private:
    void updateFromMouseEvent(QMouseEvent* event);

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
};

}   // namespace pdf
