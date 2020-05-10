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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFFORM_H
#define PDFFORM_H

#include "pdfobject.h"
#include "pdfdocument.h"
#include "pdfannotation.h"
#include "pdfdocumentdrawinterface.h"

#include <QTextLayout>

#include <optional>

namespace pdf
{
class PDFFormField;
class PDFFormManager;
class PDFObjectStorage;
class PDFModifiedDocument;
class PDFDocumentModifier;

using PDFFormFieldPointer = QSharedPointer<PDFFormField>;
using PDFFormFields = std::vector<PDFFormFieldPointer>;
using PDFWidgetToFormFieldMapping = std::map<PDFObjectReference, PDFFormField*>;

/// A simple proxy to the widget annotation
class PDFFormWidget
{
public:
    explicit inline PDFFormWidget() = default;
    explicit inline PDFFormWidget(PDFObjectReference page, PDFObjectReference widget, PDFFormField* parentField, PDFAnnotationAdditionalActions actions);

    PDFObjectReference getPage() const { return m_page; }
    PDFObjectReference getWidget() const { return m_widget; }
    PDFFormField* getParent() const { return m_parentField; }
    const PDFAction* getAction(PDFAnnotationAdditionalActions::Action action) const { return m_actions.getAction(action); }

    /// Parses form widget from the object reference. If some error occurs
    /// then empty object is returned, no exception is thrown.
    /// \param storage Storage
    /// \param reference Widget reference
    /// \param parentField Parent field
    static PDFFormWidget parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField);

private:
    PDFObjectReference m_page;
    PDFObjectReference m_widget;
    PDFFormField* m_parentField;
    PDFAnnotationAdditionalActions m_actions;
};

using PDFFormWidgets = std::vector<PDFFormWidget>;

/// Form field is terminal or non-terminal field (non-terminal fields
/// have children), fields represents various interactive widgets, such as
/// checks, radio buttons, text edits etc., which are editable and user
/// can interact with them.
class PDFFormField
{
public:
    explicit inline PDFFormField() = default;
    virtual ~PDFFormField() = default;

    enum class FieldType
    {
        Invalid,
        Button,
        Text,
        Choice,
        Signature
    };

    enum NameType
    {
        Partial,            ///< Partial name for this field
        UserCaption,        ///< Name used in GUI (for example, in message boxes)
        FullyQualified,     ///< Fully qualified name (according to the PDF specification 1.7)
        Export,             ///< Name for export
        NameTypeEnd
    };

    using FieldNames = std::array<QString, NameTypeEnd>;

    enum FieldFlag
    {
        None = 0,

        /// Field is read only, it doesn't respond on mouse clicks (if it is a button),
        /// associated widget annotation will not interact with the user. Field can't
        /// change it's value. Mainly used for calculable fields.
        ReadOnly = 1 << 0,

        /// If set, field is required, when submitting form by submit action. If submit
        /// action is triggered, then all fields with this flag must have a value.
        Required = 1 << 1,

        /// If set, field value must not be exported by submit form action.
        NoExport = 1 << 2,

        /// Text fields only. If set, then text can span multiple lines. Otherwise,
        /// text is restricted to single line.
        Multiline = 1 << 12,

        /// Text fields only. If set, field is intended to display text edit, which
        /// can edit passwords. Password characters should not be echoed to the screen
        /// and value of this field should not be stored in PDF file.
        Password = 1 << 13,

        /// Only for radio buttons. If set, at least one radio button is checked.
        /// If user clicks on checked radio button, it is not checked off. Otherwise
        /// user can uncheck checked radio button (so no button is selected).
        NoToggleToOff = 1 << 14,

        /// Valid only for buttons which have PushButton flag unset. If Radio flag is set,
        /// then widget is radio button, otherwise widget is push button.
        Radio = 1 << 15,

        /// Valid only for buttons. If set, button is push button.
        PushButton = 1 << 16,

        /// For choice fields only. If set, choice field is combo box.
        /// If clear, it is a list box.
        Combo = 1 << 17,

        /// For choice fields combo boxes only. If set, combo box is editable,
        /// i.e. user can enter custom text. If this flag is cleared, then combo box
        /// is not editable and user can only select items from the drop down list.
        Edit = 1 << 18,

        /// For choice fields only. If set, the field option's items should be sorted
        /// alphabetically, but not by the viewer application, but by author of the form.
        /// Viewer application should respect Opt array and display items in that order.
        Sort = 1 << 19,

        /// Text fields only. Text field is used to select file path, whose contents
        /// should be submitted as the value of the field.
        FileSelect = 1 << 20,

        /// For choice fields only. If set, then user can select multiple choices
        /// simultaneously, if clear, then only one item should be selected at the time.
        MultiSelect = 1 << 21,

        /// Text fields only. If set, texts entered in this field should not be spell checked.
        DoNotSpellcheck = 1 << 22,

        /// Text fields only. Allow only so much text, which fits field's area. If field's area is filled,
        /// then do not allow the user to store more text in the field.
        DoNotScroll = 1 << 23,

        /// Text fields only. If set, then MaxLen entry and annotation rectangle is used
        /// to divide space equally for each character. Text is laid out into those spaces.
        Comb = 1 << 24,

        /// Valid only for radio buttons. Radio buttons in a group of radio buttons,
        /// which have same value for 'On' state, will turn On and Off in unison, if one
        /// is checked, all are checked. If clear, radio buttons are mutually exclusive.
        RadiosInUnison = 1 << 25,

        /// Text fields only. Value of this field should be a rich text.
        RichText = 1 << 25,

        /// Choice fields only. If set, then when user selects choice by mouse,
        /// data is immediately commited. Otherwise data are commited only, when
        /// widget lose focus.
        CommitOnSelectionChange = 1 << 26
    };

    Q_DECLARE_FLAGS(FieldFlags, FieldFlag)

    PDFObjectReference getSelfReference() const { return m_selfReference; }
    FieldType getFieldType() const { return m_fieldType; }
    const PDFFormField* getParentField() const { return m_parentField; }
    const PDFFormFields& getChildFields() const { return m_childFields; }
    const PDFFormWidgets& getWidgets() const { return m_widgets; }
    const QString& getName(NameType nameType) const { return m_fieldNames.at(nameType); }
    FieldFlags getFlags() const { return m_fieldFlags; }
    const PDFObject& getValue() const { return m_value; }
    const PDFObject& getDefaultValue() const { return m_defaultValue; }

    /// Fills widget to form field mapping
    /// \param mapping Form field mapping
    void fillWidgetToFormFieldMapping(PDFWidgetToFormFieldMapping& mapping);

    /// Reloads value from object storage. Actually stored value is lost.
    void reloadValue(const PDFObjectStorage* storage, PDFObject parentValue);

    /// Applies function to this form field and all its descendants,
    /// in pre-order (first application is to the parent, following
    /// calls to apply for children).
    /// \param functor Functor to apply
    void apply(const std::function<void(const PDFFormField*)>& functor) const;

    /// Applies function to this form field and all its descendants,
    /// in pre-order (first application is to the parent, following
    /// calls to apply for children).
    /// \param functor Functor to apply
    void modify(const std::function<void(PDFFormField*)>& functor);

    /// Returns action by type. If action is not found, nullptr is returned
    /// \param action Action type
    const PDFAction* getAction(PDFAnnotationAdditionalActions::Action action) const { return m_additionalActions.getAction(action); }

    /// Parses form field from the object reference. If some error occurs
    /// then null pointer is returned, no exception is thrown.
    /// \param storage Storage
    /// \param reference Field reference
    /// \param parentField Parent field (or nullptr, if it is root field)
    static PDFFormFieldPointer parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField);

    struct SetValueParameters
    {
        enum class Scope
        {
            User,       ///< Changed value comes from user input
            Internal    ///< Value is changed by some program operation (for example, calculation)
        };

        PDFObject value;
        PDFObjectReference invokingWidget;
        PDFFormField* invokingFormField = nullptr;
        PDFDocumentModifier* modifier = nullptr;
        PDFFormManager* formManager = nullptr;
        Scope scope = Scope::User;
    };

    /// Sets value to the form field. If value has been correctly
    /// set, then true is returned, otherwise false is returned.
    /// This function also verifies, if value can be set (i.e. form field
    /// is editable, and value is valid).
    /// \param parameters Parameters
    virtual bool setValue(const SetValueParameters& parameters);

protected:
    PDFObjectReference m_selfReference;
    FieldType m_fieldType = FieldType::Invalid;
    PDFFormField* m_parentField = nullptr;
    PDFFormFields m_childFields;
    PDFFormWidgets m_widgets;
    FieldNames m_fieldNames;
    FieldFlags m_fieldFlags = None;
    PDFObject m_value;
    PDFObject m_defaultValue;
    PDFAnnotationAdditionalActions m_additionalActions;
};

/// Represents pushbutton, checkbox and radio button (which is distinguished
/// by flags).
class PDFFormFieldButton : public PDFFormField
{
public:
    explicit inline PDFFormFieldButton() = default;

    enum class ButtonType
    {
        PushButton,
        RadioButton,
        CheckBox
    };

    /// Determines button type from form field's flags
    ButtonType getButtonType() const;

    const QStringList& getOptions() const { return m_options; }

    /// Returns appearance state, which corresponds to the checked
    /// state of checkbox or radio button. If error occurs, then
    /// empty byte array is returned.
    /// \param formManager Form manager
    /// \param widget Widget
    static QByteArray getOnAppearanceState(const PDFFormManager* formManager, const PDFFormWidget* widget);

    /// Returns appearance state, which corresponds to the unchecked
    /// state of checkbox or radio button. If error occurs, then
    /// empty byte array is returned.
    /// \param formManager Form manager
    /// \param widget Widget
    static QByteArray getOffAppearanceState(const PDFFormManager* formManager, const PDFFormWidget* widget);

    virtual bool setValue(const SetValueParameters& parameters) override;

private:
    friend static PDFFormFieldPointer PDFFormField::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField);

    /// List of export names of 'On' state for radio buttons. In widget annotation's appearance
    /// dictionaries, state names are computer generated numbers (for example /1, /3, ...),
    /// which are indices to this string list. This allows to distinguish between
    /// different widget annotations, even if they have same value in m_options array.
    QStringList m_options;
};

/// Represents single line, or multiline text field
class PDFFormFieldText : public PDFFormField
{
public:
    explicit inline PDFFormFieldText() = default;

    PDFInteger getTextMaximalLength() const { return m_maxLength; }
    const QByteArray& getDefaultAppearance() const { return m_defaultAppearance; }
    Qt::Alignment getAlignment() const { return m_alignment; }
    const QString& getRichTextDefaultStyle() const { return m_defaultStyle; }
    const QString& getRichTextValue() const { return m_richTextValue; }

private:
    friend static PDFFormFieldPointer PDFFormField::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField);

    /// Maximal length of text in the field. If zero,
    /// no maximal length is specified.
    PDFInteger m_maxLength = 0;

    /// Default appearance
    QByteArray m_defaultAppearance;

    /// Text field alignment
    Qt::Alignment m_alignment = 0;

    /// Default style
    QString m_defaultStyle;

    /// Rich text value
    QString m_richTextValue;
};

class PDFFormFieldChoice : public PDFFormField
{
public:
    explicit inline PDFFormFieldChoice() = default;

    bool isComboBox() const { return m_fieldFlags.testFlag(Combo); }
    bool isEditableComboBox() const { return m_fieldFlags.testFlag(Edit); }
    bool isListBox() const { return !isComboBox(); }

    struct Option
    {
        QString exportString;
        QString userString;
    };

    using Options = std::vector<Option>;

    const Options& getOptions() const { return m_options; }
    PDFInteger getTopIndex() const { return m_topIndex; }
    const PDFObject& getSelection() const { return m_selection; }

private:
    friend static PDFFormFieldPointer PDFFormField::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField);

    Options m_options;
    PDFInteger m_topIndex;
    PDFObject m_selection;
};

class PDFFormFieldSignature : public PDFFormField
{
public:
    explicit inline PDFFormFieldSignature() = default;

private:
    friend static PDFFormFieldPointer PDFFormField::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField);

};

/// This class represents interactive form. Interactive form fields can span multiple
/// document pages. So this object represents all interactive form fields in the document.
/// Fields forms tree-like structure, where leafs are usually widgets. Fields include
/// ordinary widgets, such as buttons, check boxes, combo boxes and text fields, and one
/// special - signature field, which represents digital signature.
class PDFForm
{
public:
    explicit inline PDFForm() = default;

    enum class FormType
    {
        None,
        AcroForm,
        XFAForm
    };

    enum SignatureFlag
    {
        None            = 0x0000,
        SignatureExists = 0x0001, ///< If set, at least one signature exists in the document
        AppendOnly      = 0x0002, ///< If set, signature may be invalidated during rewrite
    };
    Q_DECLARE_FLAGS(SignatureFlags, SignatureFlag)

    FormType getFormType() const { return m_formType; }
    const PDFFormFields& getFormFields() const { return m_formFields; }
    bool isAppearanceUpdateNeeded() const { return m_needAppearances; }
    SignatureFlags getSignatureFlags() const { return m_signatureFlags; }
    const std::vector<PDFObjectReference>& getCalculationOrder() const { return m_calculationOrder; }
    const PDFObject& getResources() const { return m_resources; }
    const std::optional<QByteArray>& getDefaultAppearance() const { return m_defaultAppearance; }
    const std::optional<PDFInteger>& getQuadding() const { return m_quadding; }
    const PDFObject& getXFA() const { return m_xfa; }

    bool isAcroForm() const { return getFormType() == PDFForm::FormType::AcroForm; }
    bool isXFAForm() const { return getFormType() == PDFForm::FormType::XFAForm; }

    /// Returns form field for widget. If widget doesn't have attached form field,
    /// then nullptr is returned.
    /// \param widget Widget annotation
    const PDFFormField* getFormFieldForWidget(PDFObjectReference widget) const;

    /// Returns form field for widget. If widget doesn't have attached form field,
    /// then nullptr is returned.
    /// \param widget Widget annotation
    PDFFormField* getFormFieldForWidget(PDFObjectReference widget);

    /// Parses form from the object. If some error occurs
    /// then empty form is returned, no exception is thrown.
    /// \param document Document
    /// \param reference Field reference
    static PDFForm parse(const PDFDocument* document, PDFObject object);

private:
    void updateWidgetToFormFieldMapping();

    FormType m_formType = FormType::None;
    PDFFormFields m_formFields;
    bool m_needAppearances = false;
    SignatureFlags m_signatureFlags = None;
    std::vector<PDFObjectReference> m_calculationOrder;
    PDFObject m_resources;
    std::optional<QByteArray> m_defaultAppearance;
    std::optional<PDFInteger> m_quadding;
    PDFObject m_xfa;
    PDFWidgetToFormFieldMapping m_widgetToFormField;
};

/// "Pseudo" widget, which is emulating text editor, which can be single line, or multiline.
/// Passwords can also be edited and editor can be read only.
class PDFTextEditPseudowidget
{
public:
    explicit inline PDFTextEditPseudowidget(PDFFormField::FieldFlags flags);

    void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    void keyPressEvent(QWidget* widget, QKeyEvent* event);

    inline bool isReadonly() const { return m_flags.testFlag(PDFFormField::ReadOnly); }
    inline bool isMultiline() const { return m_flags.testFlag(PDFFormField::Multiline); }
    inline bool isPassword() const { return m_flags.testFlag(PDFFormField::Password); }
    inline bool isFileSelect() const { return m_flags.testFlag(PDFFormField::FileSelect); }
    inline bool isComb() const { return m_flags.testFlag(PDFFormField::Comb); }

    inline bool isEmpty() const { return m_editText.isEmpty(); }
    inline bool isTextSelected() const { return !isEmpty() && getSelectionLength() > 0; }
    inline bool isWholeTextSelected() const { return !isEmpty() && getSelectionLength() == m_editText.length(); }

    inline int getTextLength() const { return m_editText.length(); }
    inline int getSelectionLength() const { return m_selectionEnd - m_selectionStart; }

    inline int getPositionCursor() const { return m_positionCursor; }
    inline int getPositionStart() const { return 0; }
    inline int getPositionEnd() const { return getTextLength(); }

    inline void clearSelection() { m_selectionStart = m_selectionEnd = 0; }

    inline QString getSelectedText() const { return m_editText.mid(m_selectionStart, getSelectionLength()); }

    /// Sets (updates) text selection
    /// \param startPosition From where we are selecting text
    /// \param selectionLength Selection length (positive - to the right, negative - to the left)
    void setSelection(int startPosition, int selectionLength);

    /// Moves cursor position. It behaves as usual in text boxes,
    /// when user moves the cursor. If \p select is true, then
    /// selection is updated.
    /// \param position New position of the cursor
    /// \param select Select text when moving the cursor?
    void setCursorPosition(int position, bool select);

    /// Sets text content of the widget. This functions sets the text,
    /// even if widget is readonly.
    /// \param text Text to be set
    void setText(const QString& text);

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param appearance Appearance
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param maxTextLength Maximal text length
    void setAppearance(const PDFAnnotationDefaultAppearance& appearance,
                       Qt::Alignment textAlignment,
                       QRectF rect,
                       int maxTextLength);

    void performCut();
    void performCopy();
    void performPaste();
    void performClear();
    void performSelectAll();
    void performBackspace();
    void performDelete();
    void performRemoveSelectedText();
    void performInsertText(const QString& text);

    /// Draw text edit using given parameters
    /// \param parameters Parameters
    void draw(AnnotationDrawParameters& parameters, bool edit) const;

private:
    /// This function does following things:
    ///     1) Clamps edit text to fit maximum length
    ///     2) Creates display string from edit string
    ///     3) Updates text layout
    void updateTextLayout();

    /// Returns single step forward, which is determined
    /// by cursor move style and layout direction.
    int getSingleStepForward() const;

    /// Returns single step backward, which is determined
    /// by cursor move style and layout direction.
    int getSingleStepBackward() const { return -getSingleStepForward(); }

    /// Returns next/previous position, by number of steps,
    /// using given cursor mode (skipping characters or whole words).
    /// \param steps Number of steps to proceed (can be negative number)
    /// \param mode Skip mode - letters or words?
    int getNextPrevCursorPosition(int steps, QTextLayout::CursorMode mode) const { return getNextPrevCursorPosition(m_positionCursor, steps, mode); }

    /// Returns next/previous position from reference cursor position, by number of steps,
    /// using given cursor mode (skipping characters or whole words).
    /// \param referencePosition Reference cursor position
    /// \param steps Number of steps to proceed (can be negative number)
    /// \param mode Skip mode - letters or words?
    int getNextPrevCursorPosition(int referencePosition, int steps, QTextLayout::CursorMode mode) const;

    /// Returns current line text start position
    int getCurrentLineTextStart() const;

    /// Returns current line text end position
    int getCurrentLineTextEnd() const;

    inline int getCursorForward(QTextLayout::CursorMode mode) const { return getNextPrevCursorPosition(getSingleStepForward(), mode); }
    inline int getCursorBackward(QTextLayout::CursorMode mode) const { return getNextPrevCursorPosition(getSingleStepBackward(), mode); }
    inline int getCursorCharacterForward() const { return getCursorForward(QTextLayout::SkipCharacters); }
    inline int getCursorCharacterBackward() const { return getCursorBackward(QTextLayout::SkipCharacters); }
    inline int getCursorWordForward() const { return getCursorForward(QTextLayout::SkipWords); }
    inline int getCursorWordBackward() const { return getCursorBackward(QTextLayout::SkipWords); }
    inline int getCursorDocumentStart() const { return (getSingleStepForward() > 0) ? getPositionStart() : getPositionEnd(); }
    inline int getCursorDocumentEnd() const { return (getSingleStepForward() > 0) ? getPositionEnd() : getPositionStart(); }
    inline int getCursorLineStart() const { return (getSingleStepForward() > 0) ? getCurrentLineTextStart() : getCurrentLineTextEnd(); }
    inline int getCursorLineEnd() const { return (getSingleStepForward() > 0) ? getCurrentLineTextEnd() : getCurrentLineTextStart(); }
    inline int getCursorNextLine() const { return getCurrentLineTextEnd(); }
    inline int getCursorPreviousLine() const { return getNextPrevCursorPosition(getCurrentLineTextStart(), -1, QTextLayout::SkipCharacters); }

    int getCursorLineUp() const;
    int getCursorLineDown() const;

    PDFFormField::FieldFlags m_flags;

    /// Text edited by the user
    QString m_editText;

    /// Text, which is displayed. It can differ from text
    /// edited by user, in case password is being entered.
    QString m_displayText;

    /// Text layout
    QTextLayout m_textLayout;

    /// Character for displaying passwords
    QChar m_passwordReplacementCharacter;

    int m_selectionStart;
    int m_selectionEnd;
    int m_positionCursor;
    int m_maxTextLength;

    QRectF m_widgetRect;
    QColor m_textColor;
};

/// Base class for editors of form fields. It enables editation
/// of form fields, such as entering text, clicking on check box etc.
class PDFFormFieldWidgetEditor : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFFormFieldWidgetEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldWidgetEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event);
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event);
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event);
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event);
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event);

    virtual bool isEditorDrawEnabled() const { return false; }

    const PDFFormWidget* getFormWidget() const { return &m_formWidget; }
    PDFFormField*  getFormField() const { return m_formWidget.getParent(); }
    PDFObjectReference getWidgetAnnotation() const { return m_formWidget.getWidget(); }

    void setFocus(bool hasFocus);

    /// Draw form field widget using given parameters
    /// \param parameters Parameters
    virtual void draw(AnnotationDrawParameters& parameters) const;

protected:
    /// This function is called every time, the focus state changes
    /// \param focused If editor was focused, or not
    virtual void setFocusImpl(bool focused) { Q_UNUSED(focused); }

    void performKeypadNavigation(QWidget* widget, QKeyEvent* event);

    PDFFormManager* m_formManager;
    PDFFormWidget m_formWidget;
    bool m_hasFocus;
};

/// Editor for button-like editors
class PDFFormFieldAbstractButtonEditor : public PDFFormFieldWidgetEditor
{
    Q_OBJECT

private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldAbstractButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldAbstractButtonEditor() = default;

    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;

protected:
    virtual void click() = 0;
};

/// Editor for push buttons
class PDFFormFieldPushButtonEditor : public PDFFormFieldAbstractButtonEditor
{
    Q_OBJECT

private:
    using BaseClass = PDFFormFieldAbstractButtonEditor;

public:
    explicit PDFFormFieldPushButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldPushButtonEditor() = default;

protected:
    virtual void click() override;
};

/// Editor for check boxes or radio buttons
class PDFFormFieldCheckableButtonEditor : public PDFFormFieldAbstractButtonEditor
{
    Q_OBJECT

private:
    using BaseClass = PDFFormFieldAbstractButtonEditor;

public:
    explicit PDFFormFieldCheckableButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldCheckableButtonEditor() = default;

protected:
    virtual void click() override;
};

/// Editor for text fields
class PDFFormFieldTextBoxEditor : public PDFFormFieldWidgetEditor
{
    Q_OBJECT

private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldTextBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldTextBoxEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;

    virtual bool isEditorDrawEnabled() const override { return m_hasFocus; }
    virtual void draw(AnnotationDrawParameters& parameters) const override;

    /// Initializes text edit using actual form field value,
    /// font, color for text edit appearance.
    /// \param textEdit Text editor
    void initializeTextEdit(PDFTextEditPseudowidget* textEdit) const;

protected:
    virtual void setFocusImpl(bool focused);

private:
    PDFTextEditPseudowidget m_textEdit;
};

/// Editor for combo boxes
class PDFFormFieldComboBoxEditor : public PDFFormFieldWidgetEditor
{
    Q_OBJECT

private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldComboBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldComboBoxEditor() = default;
};

/// Editor for list boxes
class PDFFormFieldListBoxEditor : public PDFFormFieldWidgetEditor
{
    Q_OBJECT

private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldListBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget, QObject* parent);
    virtual ~PDFFormFieldListBoxEditor() = default;
};

/// Form manager. Manages all form widgets functionality - triggers actions,
/// edits fields, updates annotation appearances, etc. Valid pointer to annotation
/// manager is requirement.
class PDFFORQTLIBSHARED_EXPORT PDFFormManager : public QObject, public IDrawWidgetInputInterface
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFFormManager(PDFDrawWidgetProxy* proxy, QObject* parent);
    virtual ~PDFFormManager() override;

    enum FormAppearanceFlag
    {
        None                    = 0x0000,
        HighlightFields         = 0x0001,
        HighlightRequiredFields = 0x0002,
    };
    Q_DECLARE_FLAGS(FormAppearanceFlags, FormAppearanceFlag)

    bool hasForm() const { return hasAcroForm() || hasXFAForm(); }
    bool hasAcroForm() const { return m_form.getFormType() == PDFForm::FormType::AcroForm; }
    bool hasXFAForm() const { return m_form.getFormType() == PDFForm::FormType::XFAForm; }

    const PDFForm* getForm() const { return &m_form; }

    /// Returns form field for widget. If widget doesn't have attached form field,
    /// then nullptr is returned.
    /// \param widget Widget annotation
    const PDFFormField* getFormFieldForWidget(PDFObjectReference widget) const { return m_form.getFormFieldForWidget(widget); }

    /// Returns form field for widget. If widget doesn't have attached form field,
    /// then nullptr is returned.
    /// \param widget Widget annotation
    PDFFormField* getFormFieldForWidget(PDFObjectReference widget) { return m_form.getFormFieldForWidget(widget); }

    PDFAnnotationManager* getAnnotationManager() const;
    void setAnnotationManager(PDFAnnotationManager* annotationManager);

    const PDFDocument* getDocument() const;
    void setDocument(const PDFModifiedDocument& document);

    FormAppearanceFlags getAppearanceFlags() const;
    void setAppearanceFlags(FormAppearanceFlags flags);

    /// Returns true, if form field has text (for example, it is a text box,
    /// or editable combo box)
    /// \param widgetAnnotation Widget annotation
    bool hasFormFieldWidgetText(PDFObjectReference widgetAnnotation) const;

    /// Returns all form field widgets. This function is not simple getter,
    /// call can be expensive, because all form fields are accessed.
    PDFFormWidgets getWidgets() const;

    /// Applies function to all form fields present in the form,
    /// in pre-order (first application is to the parent, following
    /// calls to apply for children).
    /// \param functor Functor to apply
    void apply(const std::function<void(const PDFFormField*)>& functor) const;

    /// Applies function to all form fields present in the form,
    /// in pre-order (first application is to the parent, following
    /// calls to apply for children).
    /// \param functor Functor to apply
    void modify(const std::function<void(PDFFormField*)>& functor) const;

    /// Sets focus to the editor. Is is allowed to pass nullptr to this
    /// function, it means that no editor is focused.
    /// \param editor Editor to be focused
    void setFocusToEditor(PDFFormFieldWidgetEditor* editor);

    /// Tries to set focus on next/previous form field in the focus chain.
    /// Function returns true, if focus has been successfully set.
    /// \param next Focus next (true) or previous (false) widget
    bool focusNextPrevFormField(bool next);

    /// Returns true, if widget is focused.
    /// \param widget Widget annotation reference
    bool isFocused(PDFObjectReference widget) const;

    /// Tries to find appropriate action and returns it. If action is not found,
    /// then nullptr is returned.
    /// \param actionType Action to be performed
    /// \param widget Form field widget
    const PDFAction* getAction(PDFAnnotationAdditionalActions::Action actionType, const PDFFormWidget* widget);

    /// Returns default form apperance flags
    static constexpr FormAppearanceFlags getDefaultApperanceFlags() { return HighlightFields | HighlightRequiredFields; }

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
        QMatrix deviceToWidget;

        /// Returns true, if mouse event info is valid, i.e.
        /// mouse event occurs above some form field.
        bool isValid() const { return editor != nullptr; }
    };

    /// Tries to set value to the form field
    void setFormFieldValue(PDFFormField::SetValueParameters parameters);

    /// Get widget rectangle (from annotation)
    QRectF getWidgetRectangle(const PDFFormWidget& widget) const;

    /// Returns editor for form field
    PDFFormFieldWidgetEditor* getEditor(const PDFFormField* formField) const;

    // interface IDrawWidgetInputInterface

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;

    /// Returns tooltip generated from annotation
    virtual QString getTooltip() const override { return QString(); }

    /// Returns current cursor
    virtual const std::optional<QCursor>& getCursor() const override;

    virtual int getInputPriority() const override { return FormPriority; }

signals:
    void actionTriggered(const PDFAction* action);
    void documentModified(PDFModifiedDocument document);

private:
    void updateFormWidgetEditors();
    void updateFieldValues();

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

    PDFDrawWidgetProxy* m_proxy;
    PDFAnnotationManager* m_annotationManager;
    const PDFDocument* m_document;
    FormAppearanceFlags m_flags;
    PDFForm m_form;

    std::vector<PDFFormFieldWidgetEditor*> m_widgetEditors;
    PDFFormFieldWidgetEditor* m_focusedEditor;
    MouseGrabInfo m_mouseGrabInfo;
};

}   // namespace pdf

#endif // PDFFORM_H
