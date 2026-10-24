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

#ifndef PDFWIDGETFORMMANAGER_H
#define PDFWIDGETFORMMANAGER_H

#include "pdfwidgetsglobal.h"
#include "pdfform.h"
#include "pdfdocumentdrawinterface.h"

namespace pdf
{
class PDFDrawWidgetProxy;
class PDFWidgetAnnotationManager;
class PDFWidgetFormManager;

/// Base class for editors of form fields. It enables editation
/// of form fields, such as entering text, clicking on check box etc.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFFormFieldWidgetEditor
{
public:
    explicit PDFFormFieldWidgetEditor(PDFWidgetFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldWidgetEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event);
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event);
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition);
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition);
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition);
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition);
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event, const QPointF& mousePagePosition);
    virtual void reloadValue() { }
    virtual bool isEditorDrawEnabled() const { return false; }
    virtual QRectF getActiveEditorRectangle() const { return QRectF(); }

    const PDFFormWidget* getFormWidget() const { return &m_formWidget; }
    PDFFormField*  getFormField() const { return m_formWidget.getParent(); }
    PDFObjectReference getWidgetAnnotation() const { return m_formWidget.getWidget(); }

    void setFocus(bool hasFocus);

    /// Draw form field widget using given parameters. It is used, when
    /// we want to draw editor contents on the painter using parameters.
    /// Parameter \p edit decides, if editor is drawn, or static contents
    /// based on field value is drawn.
    /// \param parameters Parameters
    /// \param edit Draw editor or static contents
    virtual void draw(AnnotationDrawParameters& parameters, bool edit) const;

protected:
    /// This function is called every time, the focus state changes
    /// \param focused If editor was focused, or not
    virtual void setFocusImpl(bool focused) { Q_UNUSED(focused); }

    void performKeypadNavigation(QWidget* widget, QKeyEvent* event);

    PDFWidgetFormManager* m_formManager;
    PDFFormWidget m_formWidget;
    bool m_hasFocus;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFWidgetFormManager : public PDFFormManager, public IDrawWidgetInputInterface
{
    Q_OBJECT

private:
    using BaseClass = PDFFormManager;

public:
    explicit PDFWidgetFormManager(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFWidgetFormManager() override;

    // interface IDrawWidgetInputInterface

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;
    virtual int getInputPriority() const override;
    virtual QString getTooltip() const override;
    virtual const std::optional<QCursor>& getCursor() const override;

    /// Tries to set focus on next/previous form field in the focus chain.
    /// Function returns true, if focus has been successfully set.
    /// \param next Focus next (true) or previous (false) widget
    bool focusNextPrevFormField(bool next);

    /// Tab order of the pages (ISO 32000-2, 12.5.1, entry Tabs of the page): Tab moves
    /// through the fields page by page, on a page in the order of its entry Tabs (R rows,
    /// C columns; S, A, W or no entry - the order of the array Annots), it skips read
    /// only fields and hidden widgets, and it cycles - after the last field the first
    /// one, before the first field the last one (focusNextPrevFormField). Off by default:
    /// the order of the field tree, every field, and the focus leaves the fields after
    /// the last one (the old applications).
    // TODO(remove-old-apps): the tab order of the pages becomes the only order - the switch and the order of the field tree go
    void setPageTabOrder(bool pageTabOrder) { m_isPageTabOrder = pageTabOrder; }
    bool isPageTabOrder() const { return m_isPageTabOrder; }

    /// Returns the widget annotations in the order of Tab (setPageTabOrder; without it
    /// the order of the field tree with every widget)
    std::vector<PDFObjectReference> getTabOrder() const;

    /// Sets focus to the editor. Is is allowed to pass nullptr to this
    /// function, it means that no editor is focused.
    /// \param editor Editor to be focused
    void setFocusToEditor(PDFFormFieldWidgetEditor* editor);

    /// Returns true, if widget is focused.
    /// \param widget Widget annotation reference
    virtual bool isFocused(PDFObjectReference widget) const override;

    /// Returns the widget annotation of the focused editor (an invalid reference,
    /// when no editor has the focus)
    PDFObjectReference getFocusedWidget() const;

    /// Returns true, if a mouse press at the point of the widget goes to an editor
    /// of a field (a field is under the point, the form is not read only, or the
    /// mouse is grabbed by an editor)
    /// \param widget Widget (the draw widget of the proxy)
    /// \param point Point in the coordinates of the widget
    bool isEditorAt(QWidget* widget, QPoint point);

    /// Returns editor for form field
    PDFFormFieldWidgetEditor* getEditor(const PDFFormField* formField) const;

    struct MouseEventInfo
    {
        /// Form field under mouse event, nullptr, if
        /// no form field is under mouse button.
        PDFFormField* formField = nullptr;

        /// Form field widget editor, which is associated
        /// with given form field.
        PDFFormFieldWidgetEditor* editor = nullptr;

        /// Mouse position in form field coordinate space
        QPointF mousePosition;

        /// Matrix, which maps from device space to widget space
        QTransform deviceToWidget;

        /// Returns true, if mouse event info is valid, i.e.
        /// mouse event occurs above some form field.
        bool isValid() const { return editor != nullptr; }
    };

    virtual bool isEditorDrawEnabled(const PDFObjectReference& reference) const override;
    virtual bool isEditorDrawEnabled(const PDFFormField* formField) const override;
    virtual void drawFormField(const PDFFormField* formField, AnnotationDrawParameters& parameters, bool edit) const override;

    PDFWidgetAnnotationManager* getAnnotationManager() const;
    void setAnnotationManager(PDFWidgetAnnotationManager* annotationManager);

    PDFDrawWidgetProxy* getProxy() const { return m_proxy; }

protected:
    virtual void updateFieldValues() override;
    virtual void onDocumentReset() override;
    virtual void onAppearanceFlagsChanged() override;

private:
    void updateFormWidgetEditors();

    /// Returns the editors in the order of Tab (see setPageTabOrder)
    std::vector<PDFFormFieldWidgetEditor*> getTabOrderEditors() const;

    /// Releases all form widget editors
    void clearEditors();

    MouseEventInfo getMouseEventInfo(QWidget* widget, QPoint point);

    struct MouseGrabInfo
    {
        MouseEventInfo info;
        int mouseGrabNesting = 0;

        bool isMouseGrabbed() const { return mouseGrabNesting > 0; }
    };

    bool isMouseGrabbed() const { return m_mouseGrabInfo.isMouseGrabbed(); }

    /// Grabs mouse input, if mouse is already grabbed, or if event
    /// is accepted. When mouse is grabbed, then mouse input events
    /// are sent to active editor and are automatically accepted.
    /// \param info Mouse event info
    /// \param event Mouse event
    void grabMouse(const MouseEventInfo& info, QMouseEvent* event);

    /// Release mouse input
    /// \param info Mouse event info
    /// \param event Mouse event
    void ungrabMouse(const MouseEventInfo& info, QMouseEvent* event);

    PDFWidgetAnnotationManager* m_annotationManager;
    PDFDrawWidgetProxy* m_proxy;
    MouseGrabInfo m_mouseGrabInfo;
    std::optional<QCursor> m_mouseCursor;
    std::vector<PDFFormFieldWidgetEditor*> m_widgetEditors;
    PDFFormFieldWidgetEditor* m_focusedEditor;
    bool m_isPageTabOrder = false;
};

}   // namespace pdf

#endif // PDFWIDGETFORMMANAGER_H
