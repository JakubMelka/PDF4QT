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

#ifndef PDFAPPANNOTATIONEDITCOMMANDS_H
#define PDFAPPANNOTATIONEDITCOMMANDS_H

#include "pdfappglobal.h"
#include "pdfappdocumentcommands.h"

#include "pdfobject.h"
#include "pdfwidgetannotation.h"

#include <QPoint>
#include <QPointer>

namespace pdf
{

class PDFAppAnnotationBar;
class PDFAppAnnotationGeometryDialog;
class PDFAppAnnotationMenus;
class PDFAppAnnotationPropertiesDialog;
class PDFAppCopyToPagesDialog;

/// Commands of package B of step 4 (PDFAppDocumentPackage::AnnotationEdit): the
/// clipboard of the annotations (Ctrl+X / Ctrl+V / Ctrl+Shift+V / Del on the
/// canvas), the properties (F2, Alt+Enter), comment, reply, geometry, copy to pages,
/// hide, select all on the displayed pages, rotation (clockwise as the user sees the
/// page), flips, alignment and distribution of the selected annotations - each one
/// undo step through PDFAppAnnotationController and the engine.
///
/// The handler of the tab is also the user interface of the requests of the
/// controller (they come from the event loop): the comment popup, the properties,
/// geometry and Copy to Pages dialogs, the context menus; it owns the annotation
/// bar of the view (PDFAppAnnotationBar) and the menus (PDFAppAnnotationMenus).
/// One dialog at a time; a request while a dialog is open is ignored.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationEditCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppAnnotationEditCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppAnnotationEditCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;

    /// The annotation bar of the view (nullptr while the view is destroyed)
    PDFAppAnnotationBar* getAnnotationBar() const;

    /// The context menus of the annotations
    PDFAppAnnotationMenus* getMenus() const;

    /// The dialog, which is open (nullptr = none)
    QWidget* getOpenDialog() const;

private:
    void onCommentRequested(pdf::PDFObjectReference annotation, bool compose);
    void onReplyRequested(pdf::PDFObjectReference annotation);
    void onPopupRequested(pdf::PDFObjectReference annotation, pdf::PDFObjectReference page, QPoint globalPosition);
    void onPropertiesRequested(pdf::PDFObjectReference annotation);
    void onGeometryRequested(pdf::PDFObjectReference annotation);
    void onCopyToPagesRequested();
    void onMenuRequested(const pdf::PDFAnnotationMenuRequest& request);
    QWidget* getDialogParent() const;

    QPointer<PDFAppAnnotationBar> m_bar;
    QPointer<PDFAppAnnotationMenus> m_menus;
    QPointer<QWidget> m_dialog;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONEDITCOMMANDS_H
