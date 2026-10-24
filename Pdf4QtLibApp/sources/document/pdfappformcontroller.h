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

#ifndef PDFAPPFORMCONTROLLER_H
#define PDFAPPFORMCONTROLLER_H

#include "pdfappglobal.h"

#include "pdfform.h"

#include <QObject>
#include <QPointer>

class QWidget;

namespace pdf
{

class PDFAction;
class PDFActionResetForm;
class PDFAppAnnotationController;
class PDFAppDocument;
class PDFAppDocumentView;
class PDFDocument;
class PDFSettingsRepository;
class PDFWidgetFormManager;

/// Form filling of a document view (package D of step 4). The commits of the
/// form edits (flag FormField, one undo step per committed field) are done by
/// PDFAppAnnotationController, which also makes the form read only (ReadOnlyForm,
/// E8) for a locked document, a document without the permission to fill the form
/// and an XFA form (isFormFillingAllowed). The form controller adds:
///
/// - **Settings.** forms.highlightFields, forms.highlightRequiredFields and
///   forms.allowEditReadOnly are applied live (applySettings, called by the view at
///   every change of the settings); the read-only form is kept.
/// - **Pending edits.** An open editor of a text field or of a list of choices is
///   an edit, which is not committed yet: the controller tells the document
///   (PDFAppDocument::setPendingEdits - Save and Undo are enabled by it) and
///   commits the editor, when the document asks (commitPendingEdits before undo,
///   redo, save, lock and closing - so undo closes the editor first and undoes its
///   value) and when the canvas loses the focus.
/// - **Keyboard.** Tab / Shift+Tab move between the fields (the engine, by the
///   focus chain of PDFWidget); a field focused by the keyboard is scrolled into
///   view.
/// - **Info bars** over the canvas: the calculated fields, which are unlocked,
///   because JavaScript is not run (O16), and an XFA form, which is shown but not
///   filled. A closed bar stays closed for the document of the view.
///
/// The controller is a child of its view (created after the annotation
/// controller, deleted before the managers). The GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppFormController : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Margin around a field scrolled into view by the keyboard (pixels)
    static constexpr int FIELD_VISIBLE_MARGIN = 24;

    /// Description of the undo step of a ResetForm action (the action handler of
    /// the tab sets it around the reset, also of an action in a chain)
    static QString getResetFormDescription();

    /// The ResetForm action changes a value of the form: a field of its scope (not
    /// a push button) has another value than its default one, a list box has a
    /// selection or a scrolled top item, a widget of a button shows another
    /// appearance state than the default value gives. A reset, which changes
    /// nothing, is not performed (no undo step of a form already at its defaults;
    /// the engine would write the same values and new appearance streams).
    static bool isResetEffective(const PDFFormManager* formManager, const PDFActionResetForm* action);

    explicit PDFAppFormController(PDFAppDocumentView* view);
    virtual ~PDFAppFormController() override;

    PDFAppDocumentView* getView() const;
    PDFAppDocument* getDocument() const;
    PDFWidgetFormManager* getFormManager() const;
    PDFAppAnnotationController* getAnnotationController() const;

    /// The view has a new document model (PDFAppDocumentView::setDocument)
    void setDocument(PDFAppDocument* document);

    /// Applies the settings forms.* to the form manager (the read-only form of the
    /// annotation controller is kept); the canvas is repainted, when the
    /// highlighting has changed
    void applySettings(const PDFSettingsRepository& settings);

    /// Flags of the form manager given by the settings (without ReadOnlyForm)
    static PDFFormManager::FormAppearanceFlags getSettingsFlags(const PDFSettingsRepository& settings);

    /// The document has an XFA form (the key XFA of its interactive form): it is
    /// shown, but not filled
    static bool isXFAForm(const PDFDocument* document);

    /// An editor of a field has the focus (an invalid reference, when none)
    PDFObjectReference getFocusedWidget() const;

    /// The focused editor holds a value, which is not committed yet (a text field,
    /// which is not a password, a combo box, a list box)
    bool hasPendingEdits() const;

    /// Closes the focused editor: its value is committed (one undo step, when it
    /// has changed). \returns false, when no editor has the focus.
    bool closeEditor();

    /// Scrolls the canvas, so the focused field is seen whole (with a margin)
    void ensureFocusedFieldVisible();

    /// Identifiers of the info bars of the controller
    static QString getCalculatedFieldsBarId() { return QStringLiteral("form-calculated-fields"); }
    static QString getXFABarId() { return QStringLiteral("form-xfa"); }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentStateChanged();
    void onEditingAllowedChanged();
    void onPendingEditsCommitRequested();
    void onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId);
    void scheduleUpdate();
    void performUpdate();
    void updatePendingEdits();
    void updateInfoBars();
    void installFilters();

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFWidgetFormManager> m_formManager;
    QPointer<QWidget> m_drawWidget;
    PDFObjectReference m_focusedWidget;           ///< Focused widget at the last update
    bool m_isUpdateScheduled = false;
    bool m_isKeyboardInput = false;               ///< A key has been pressed since the last update
    bool m_isInfoBarUpdateNeeded = true;
    bool m_isCalculatedBarDismissed = false;
    bool m_isXFABarDismissed = false;
    bool m_isUpdatingPendingEdits = false;
};

}   // namespace pdf

#endif // PDFAPPFORMCONTROLLER_H
