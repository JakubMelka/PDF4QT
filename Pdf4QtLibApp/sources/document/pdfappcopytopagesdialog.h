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

#ifndef PDFAPPCOPYTOPAGESDIALOG_H
#define PDFAPPCOPYTOPAGESDIALOG_H

#include "pdfappglobal.h"

#include "pdfmessagebox.h"
#include "pdfobject.h"

#include <QPointer>

#include <vector>

namespace pdf
{

class PDFAppAnnotationController;
class PDFPageSelection;

/// Copy to Pages dialog of the selected annotations (Figma 1024:20678, 440 x 361):
/// the Page selection component (all, current, selected pages, a range, the
/// filters); Copy to N Pages = one undo step, the copies keep the position, the
/// size, the popup and the replies, the page of an annotation is skipped. A dialog
/// of the design on PDFMessageBox, modal over the scrim; it deletes itself, when it
/// closes, and closes, when the selected annotations disappear.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCopyToPagesDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    static constexpr int CARD_WIDTH = 440;

    explicit PDFAppCopyToPagesDialog(PDFAppAnnotationController* controller, QWidget* parent);
    virtual ~PDFAppCopyToPagesDialog() override;

    /// Opens the modal dialog over \p parent (the window of the view of the
    /// controller), the dialog deletes itself, when it is closed. \returns the
    /// dialog, or nullptr, when it cannot be opened (no selection, no document,
    /// the document cannot be changed).
    static PDFAppCopyToPagesDialog* open(PDFAppAnnotationController* controller, QWidget* parent);

    PDFAppAnnotationController* getController() const;

    /// Annotations, which are copied (the selection, when the dialog opened)
    const std::vector<PDFObjectReference>& getAnnotations() const { return m_annotations; }

    PDFPageSelection* getPageSelection() const;
    QPushButton* getCopyButton() const;

    /// Target pages of the selection (from 0), without the page of the annotations
    std::vector<PDFInteger> getTargetPages() const;

    /// Copy: one undo step, the dialog closes. \returns false, when nothing is copied
    bool copy();

Q_SIGNALS:
    /// The dialog has been closed (\p isApplied = the change has been committed)
    void dialogFinished(bool isApplied);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void onResultChanged();
    void onCopyClicked();
    void onDocumentChanged();
    void onDialogFinished(int result);
    void onControllerDestroyed();

    QPointer<PDFAppAnnotationController> m_controller;
    std::vector<PDFObjectReference> m_annotations;
    std::vector<PDFInteger> m_sourcePages;
    QPointer<PDFPageSelection> m_pageSelection;
    QPointer<QPushButton> m_copyButton;
    QPointer<QPushButton> m_cancelButton;
    bool m_isApplied = false;
};

}   // namespace pdf

#endif // PDFAPPCOPYTOPAGESDIALOG_H
