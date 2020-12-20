//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.

#include "pdfform.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdocumentbuilder.h"
#include "pdfpainterutils.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QStyleOption>

namespace pdf
{

/// "Pseudo" widget, which is emulating text editor, which can be single line, or multiline.
/// Passwords can also be edited and editor can be read only.
class PDFTextEditPseudowidget
{
public:
    explicit PDFTextEditPseudowidget(PDFFormField::FieldFlags flags);

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

    inline const QString& getText() const { return m_editText; }
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

    /// Returns valid cursor position retrieved from position in the widget.
    /// \param point Point in page coordinate space
    /// \param edit Are we using edit transformations?
    /// \returns Cursor position
    int getCursorPositionFromWidgetPosition(const QPointF& point, bool edit) const;

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

    const QRectF& getWidgetRect() const { return m_widgetRect; }
    QFont getFont() const { return m_textLayout.font(); }
    QColor getFontColor() const { return m_textColor; }

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

    /// Creates text box transform matrix, which transforms from
    /// widget space to page space.
    /// \param edit Create matrix for text editing?
    QMatrix createTextBoxTransformMatrix(bool edit) const;

    /// Returns vector of cursor positions (which may be not equal
    /// to edit string length, because edit string can contain surrogates,
    /// or graphemes, which are single glyphs, but represented by more
    /// 16-bit unicode codes).
    std::vector<int> getCursorPositions() const;

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

/// Editor for button-like editors
class PDFFormFieldAbstractButtonEditor : public PDFFormFieldWidgetEditor
{
private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldAbstractButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldAbstractButtonEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;

protected:
    virtual void click() = 0;
};

/// Editor for push buttons
class PDFFormFieldPushButtonEditor : public PDFFormFieldAbstractButtonEditor
{
private:
    using BaseClass = PDFFormFieldAbstractButtonEditor;

public:
    explicit PDFFormFieldPushButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldPushButtonEditor() = default;

protected:
    virtual void click() override;
};

/// Editor for check boxes or radio buttons
class PDFFormFieldCheckableButtonEditor : public PDFFormFieldAbstractButtonEditor
{
private:
    using BaseClass = PDFFormFieldAbstractButtonEditor;

public:
    explicit PDFFormFieldCheckableButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldCheckableButtonEditor() = default;

protected:
    virtual void click() override;
};

/// Editor for text fields
class PDFFormFieldTextBoxEditor : public PDFFormFieldWidgetEditor
{
private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldTextBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldTextBoxEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void reloadValue() override;
    virtual bool isEditorDrawEnabled() const override { return m_hasFocus; }
    virtual void draw(AnnotationDrawParameters& parameters, bool edit) const override;

    /// Initializes text edit using actual form field value,
    /// font, color for text edit appearance.
    /// \param textEdit Text editor
    void initializeTextEdit(PDFTextEditPseudowidget* textEdit) const;

protected:
    virtual void setFocusImpl(bool focused);

private:
    PDFTextEditPseudowidget m_textEdit;
};

/// "Pseudo" widget, which is emulating list box. It can contain scrollbar.
class PDFListBoxPseudowidget
{
public:
    explicit PDFListBoxPseudowidget(PDFFormField::FieldFlags flags);

    using Options = PDFFormFieldChoice::Options;

    inline bool isReadonly() const { return m_flags.testFlag(PDFFormField::ReadOnly); }
    inline bool isMultiSelect() const { return m_flags.testFlag(PDFFormField::MultiSelect); }
    inline bool isCommitOnSelectionChange() const { return m_flags.testFlag(PDFFormField::CommitOnSelectionChange); }

    void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event);
    void keyPressEvent(QWidget* widget, QKeyEvent* event);

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param appearance Appearance
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param options Options selectable by list box
    /// \param topIndex Index of first visible item
    void setAppearance(const PDFAnnotationDefaultAppearance& appearance,
                       Qt::Alignment textAlignment,
                       QRectF rect,
                       const Options& options,
                       int topIndex,
                       std::set<int> selection);

    /// Draw text edit using given parameters
    /// \param parameters Parameters
    void draw(AnnotationDrawParameters& parameters, bool edit) const;

    /// Sets current selection. If commit on selection change flag
    /// is set, then contents of the widgets are commited. New selection
    /// is not set, if field is readonly and \p force is false
    /// \param selection New selection
    /// \param force Set selection even if readonly
    void setSelection(std::set<int> selection, bool force);

    /// Returns active item selection
    const std::set<int>& getSelection() const { return m_selection; }

    /// Returns top index
    int getTopItemIndex() const { return m_topIndex; }

    /// Set top item index
    /// \param index Top item index
    void setTopItemIndex(int index);

    /// Scrolls the list box in such a way, that index is visible
    /// \param index Index to scroll to
    void scrollTo(int index);

    /// Returns valid index from index (i.e. index which ranges from first
    /// row to the last one). If index itself is valid, then it is returned.
    /// \param index Index, from which we want to get valid one
    int getValidIndex(int index) const;

    /// Returns true, if index is valid
    bool isValidIndex(int index) const { return index == getValidIndex(index); }

    /// Returns number of rows in the viewport
    int getViewportRowCount() const { return qFloor(m_widgetRect.height() / m_lineSpacing); }

    /// Returns item index from widget position (i.e. transforms
    /// point in the widget to the index of item, which is under the point)
    /// \param point Widget point
    int getIndexFromWidgetPosition(const QPointF& point) const;

    /// Sets current item and updates selection based on keyboard modifiers
    /// \param index New index
    /// \param modifiers Keyboard modifiers
    void setCurrentItem(int index, Qt::KeyboardModifiers modifiers);

    /// Returns text of selected item text. If no item is selected,
    /// or multiple items are selected, then empty string is returned.
    /// \returns Selected text
    QString getSelectedItemText() const;

    /// Returns true, if single item is selected
    bool isSingleItemSelected() const { return m_selection.size() == 1; }

    /// Returns index of option, in the list box option list.
    /// If option is not found, then -1 is returned.
    /// \param option Option
    int findOption(const QString& option) const;

    /// Sets widget appearance, such as font, font size, color, text alignment,
    /// and rectangle, in which widget resides on page (in page coordinates)
    /// \param font Font
    /// \param fontColor Font color
    /// \param textAlignment Text alignment
    /// \param rect Widget rectangle in page coordinates
    /// \param options Options selectable by list box
    /// \param topIndex Index of first visible item
    void initialize(QFont font, QColor fontColor, Qt::Alignment textAlignment, QRectF rect,
                    const Options& options, int topIndex, std::set<int> selection);

private:
    int getStartItemIndex() const { return 0; }
    int getEndItemIndex() const { return m_options.empty() ? 0 : int(m_options.size() - 1); }

    int getSingleStep() const { return 1; }
    int getPageStep() const { return qMax(getViewportRowCount() - 1, getSingleStep()); }

    /// Returns true, if row with this index is visible in the widget
    /// (it is in viewport).
    /// \param index Index
    bool isVisible(int index) const;

    /// Moves current item by offset (negative is up, positive is down)
    /// \param offset Offset
    /// \param modifiers Keyboard modifiers
    void moveCurrentItemIndexByOffset(int offset, Qt::KeyboardModifiers modifiers) { setCurrentItem(m_currentIndex + offset, modifiers); }

    /// Returns true, if list box has continuous selection
    bool hasContinuousSelection() const;

    /// Creates transformation matrix, which transforms widget coordinates to page coordinates
    QMatrix createListBoxTransformMatrix() const;

    PDFFormField::FieldFlags m_flags;

    Options m_options;
    Qt::Alignment m_textAlignment = 0;
    int m_topIndex = 0;
    int m_currentIndex = 0;
    std::set<int> m_selection;
    QFont m_font;
    PDFReal m_lineSpacing = 0.0;
    QRectF m_widgetRect;
    QColor m_textColor;
};

/// Editor for combo boxes
class PDFFormFieldComboBoxEditor : public PDFFormFieldWidgetEditor
{
private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldComboBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldComboBoxEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event, const QPointF& mousePagePosition) override;
    virtual void reloadValue() override;
    virtual bool isEditorDrawEnabled() const override { return m_hasFocus; }
    virtual void draw(AnnotationDrawParameters& parameters, bool edit) const override;
    virtual QRectF getActiveEditorRectangle() const override;

    /// Initializes text edit using actual form field value,
    /// font, color for text edit appearance.
    /// \param textEdit Text editor
    void initializeTextEdit(PDFTextEditPseudowidget* textEdit) const;

    /// Initializes list box using actual form field  font, color for text edit appearance.
    /// This listbox is used when combo box is in "down" mode, displaying options.
    /// \param listBox List box
    void initializeListBox(PDFListBoxPseudowidget* listBox) const;

protected:
    virtual void setFocusImpl(bool focused);

private:
    static PDFFormField::FieldFlags getTextEditFlags(PDFFormField::FieldFlags flags);

    /// Updates list box selection based on current text. If current text
    /// corresponds to some item in the list box, then item is selected
    /// and scrolled to. If text is not found in the text box, then
    /// selection is cleared.
    void updateListBoxSelection();

    PDFTextEditPseudowidget m_textEdit;
    PDFListBoxPseudowidget m_listBox;
    QRectF m_listBoxPopupRectangle;
    QRectF m_dropDownButtonRectangle;
    bool m_listBoxVisible;
};

/// Editor for list boxes
class PDFFormFieldListBoxEditor : public PDFFormFieldWidgetEditor
{
private:
    using BaseClass = PDFFormFieldWidgetEditor;

public:
    explicit PDFFormFieldListBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget);
    virtual ~PDFFormFieldListBoxEditor() = default;

    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event, const QPointF& mousePagePosition) override;
    virtual void reloadValue() override;
    virtual bool isEditorDrawEnabled() const override { return m_hasFocus; }
    virtual void draw(AnnotationDrawParameters& parameters, bool edit) const override;

    /// Initializes list box using actual form field value,
    /// font, color for text edit appearance.
    /// \param listBox List box
    void initializeListBox(PDFListBoxPseudowidget* listBox) const;

protected:
    virtual void setFocusImpl(bool focused);

private:
    /// Returns list of selected items parsed from the object.
    /// If object is invalid, empty selection is returned. Indices are
    /// primarily read from \p indices object, if it fails, indices
    /// are being read from \p value object.
    /// \param value Selection (string or array of strings)
    /// \param indices Selected indices
    std::set<int> getSelectedItems(PDFObject value, PDFObject indices) const;

    /// Commits data to the PDF document
    void commit();

    PDFListBoxPseudowidget m_listBox;
};

PDFForm PDFForm::parse(const PDFDocument* document, PDFObject object)
{
    PDFForm form;

    if (const PDFDictionary* formDictionary = document->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(document);

        std::vector<PDFObjectReference> fieldRoots = loader.readReferenceArrayFromDictionary(formDictionary, "Fields");
        form.m_formFields.reserve(fieldRoots.size());
        for (const PDFObjectReference& fieldRootReference : fieldRoots)
        {
            form.m_formFields.emplace_back(PDFFormField::parse(&document->getStorage(), fieldRootReference, nullptr));
        }

        form.m_formType = FormType::AcroForm;
        form.m_needAppearances = loader.readBooleanFromDictionary(formDictionary, "NeedAppearances", false);
        form.m_signatureFlags = static_cast<SignatureFlags>(loader.readIntegerFromDictionary(formDictionary, "SigFlags", 0));
        form.m_calculationOrder = loader.readReferenceArrayFromDictionary(formDictionary, "CO");
        form.m_resources = formDictionary->get("DR");
        form.m_defaultAppearance = loader.readOptionalStringFromDictionary(formDictionary, "DA");
        form.m_quadding = loader.readOptionalIntegerFromDictionary(formDictionary, "Q");
        form.m_xfa = formDictionary->get("XFA");

        if (!form.m_xfa.isNull())
        {
            // Jakub Melka: handle XFA form
            form.m_formType = FormType::XFAForm;
        }

        // As post-processing, delete all form fields, which are nullptr (are incorrectly defined)
        form.m_formFields.erase(std::remove_if(form.m_formFields.begin(), form.m_formFields.end(), [](const auto& field){ return !field; }), form.m_formFields.end());
        form.updateWidgetToFormFieldMapping();

        // If we have form, then we must also look for 'rogue' form fields, which are
        // incorrectly not in the 'Fields' entry of this form. We do this by iterating
        // all pages, and their annotations and try to find these 'rogue' fields.
        bool rogueFieldFound = false;
        const size_t pageCount = document->getCatalog()->getPageCount();
        for (size_t i = 0; i < pageCount; ++i)
        {
            const PDFPage* page = document->getCatalog()->getPage(i);
            for (PDFObjectReference annotationReference : page->getAnnotations())
            {
                const PDFDictionary* annotationDictionary = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));

                if (form.m_widgetToFormField.count(annotationReference))
                {
                    // This widget/form field is already present
                    continue;
                }

                if (loader.readNameFromDictionary(annotationDictionary, "Subtype") == "Widget" &&
                    !annotationDictionary->hasKey("Kids"))
                {
                    rogueFieldFound = true;
                    form.m_formFields.emplace_back(PDFFormField::parse(&document->getStorage(), annotationReference, nullptr));
                }
            }
        }

        // As post-processing, delete all form fields, which are nullptr (are incorrectly defined)
        form.m_formFields.erase(std::remove_if(form.m_formFields.begin(), form.m_formFields.end(), [](const auto& field){ return !field; }), form.m_formFields.end());

        if (rogueFieldFound)
        {
            form.updateWidgetToFormFieldMapping();
        }
    }

    return form;
}

void PDFForm::updateWidgetToFormFieldMapping()
{
    m_widgetToFormField.clear();

    if (isAcroForm() || isXFAForm())
    {
        for (const PDFFormFieldPointer& formFieldPtr : getFormFields())
        {
            formFieldPtr->fillWidgetToFormFieldMapping(m_widgetToFormField);
        }
    }
}

Qt::Alignment PDFForm::getDefaultAlignment() const
{
    Qt::Alignment alignment = Qt::AlignVCenter;

    switch (getQuadding().value_or(0))
    {
        default:
        case 0:
            alignment |= Qt::AlignLeft;
            break;

        case 1:
            alignment |= Qt::AlignHCenter;
            break;

        case 2:
            alignment |= Qt::AlignRight;
            break;
    }

    return alignment;
}

const PDFFormField* PDFForm::getFormFieldForWidget(PDFObjectReference widget) const
{
    auto it = m_widgetToFormField.find(widget);
    if (it != m_widgetToFormField.cend())
    {
        return it->second;
    }

    return nullptr;
}

PDFFormField* PDFForm::getFormFieldForWidget(PDFObjectReference widget)
{
    auto it = m_widgetToFormField.find(widget);
    if (it != m_widgetToFormField.cend())
    {
        return it->second;
    }

    return nullptr;
}

void PDFForm::apply(const std::function<void (const PDFFormField*)>& functor) const
{
    for (const PDFFormFieldPointer& childField : getFormFields())
    {
        childField->apply(functor);
    }
}

void PDFFormField::fillWidgetToFormFieldMapping(PDFWidgetToFormFieldMapping& mapping)
{
    for (const auto& childField : m_childFields)
    {
        childField->fillWidgetToFormFieldMapping(mapping);
    }

    for (const PDFFormWidget& formWidget : m_widgets)
    {
        mapping[formWidget.getWidget()] = formWidget.getParent();
    }
}

void PDFFormField::reloadValue(const PDFObjectStorage* storage, PDFObject parentValue)
{
    Q_ASSERT(storage);

    if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(getSelfReference())))
    {
        m_value = fieldDictionary->hasKey("V") ? fieldDictionary->get("V") : parentValue;
    }

    for (const PDFFormFieldPointer& childField : m_childFields)
    {
        childField->reloadValue(storage, m_value);
    }
}

void PDFFormField::apply(const std::function<void (const PDFFormField*)>& functor) const
{
    functor(this);

    for (const PDFFormFieldPointer& childField : m_childFields)
    {
        childField->apply(functor);
    }
}

void PDFFormField::modify(const std::function<void (PDFFormField*)>& functor)
{
    functor(this);

    for (const PDFFormFieldPointer& childField : m_childFields)
    {
        childField->modify(functor);
    }
}

PDFFormFieldPointer PDFFormField::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField)
{
    PDFFormFieldPointer result;

    if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference)))
    {
        PDFFormField* formField = nullptr;
        PDFFormFieldButton* formFieldButton = nullptr;
        PDFFormFieldText* formFieldText = nullptr;
        PDFFormFieldChoice* formFieldChoice = nullptr;
        PDFFormFieldSignature* formFieldSignature = nullptr;

        constexpr const std::array<std::pair<const char*, FieldType>, 4> fieldTypes = {
            std::pair<const char*, FieldType>{ "Btn", FieldType::Button },
            std::pair<const char*, FieldType>{ "Tx", FieldType::Text },
            std::pair<const char*, FieldType>{ "Ch", FieldType::Choice },
            std::pair<const char*, FieldType>{ "Sig", FieldType::Signature }
        };

        PDFDocumentDataLoaderDecorator loader(storage);
        FieldType formFieldType = parentField ? parentField->getFieldType() : FieldType::Invalid;
        if (fieldDictionary->hasKey("FT"))
        {
            formFieldType = loader.readEnumByName(fieldDictionary->get("FT"), fieldTypes.begin(), fieldTypes.end(), FieldType::Invalid);
        }

        switch (formFieldType)
        {
            case FieldType::Invalid:
                formField = new PDFFormField();
                break;

            case FieldType::Button:
                formFieldButton = new PDFFormFieldButton();
                formField = formFieldButton;
                break;

            case FieldType::Text:
                formFieldText = new PDFFormFieldText();
                formField = formFieldText;
                break;

            case FieldType::Choice:
                formFieldChoice = new PDFFormFieldChoice();
                formField = formFieldChoice;
                break;

            case FieldType::Signature:
                formFieldSignature = new PDFFormFieldSignature();
                formField = formFieldSignature;
                break;

            default:
                Q_ASSERT(false);
                break;
        }
        result.reset(formField);

        PDFObject parentV = parentField ? parentField->getValue() : PDFObject();
        PDFObject parentDV = parentField ? parentField->getDefaultValue() : PDFObject();
        FieldFlags parentFlags = parentField ? parentField->getFlags() : None;

        formField->m_selfReference = reference;
        formField->m_fieldType = formFieldType;
        formField->m_parentField = parentField;
        formField->m_fieldNames[Partial] = loader.readTextStringFromDictionary(fieldDictionary, "T", QString());
        formField->m_fieldNames[UserCaption] = loader.readTextStringFromDictionary(fieldDictionary, "TU", QString());
        formField->m_fieldNames[Export] = loader.readTextStringFromDictionary(fieldDictionary, "TM", QString());
        formField->m_fieldFlags = fieldDictionary->hasKey("Ff") ? static_cast<FieldFlags>(loader.readIntegerFromDictionary(fieldDictionary, "Ff", 0)) : parentFlags;
        formField->m_value = fieldDictionary->hasKey("V") ? fieldDictionary->get("V") : parentV;
        formField->m_defaultValue = fieldDictionary->hasKey("DV") ? fieldDictionary->get("DV") : parentDV;
        formField->m_additionalActions = PDFAnnotationAdditionalActions::parse(storage, fieldDictionary->get("AA"), fieldDictionary->get("A"));

        // Generate fully qualified name. If partial name is empty, then fully qualified name
        // is generated from parent fully qualified name (i.e. it is same as parent's name).
        // This is according the PDF specification 1.7.
        QStringList names;
        if (parentField)
        {
            names << parentField->getName(FullyQualified);
        }
        names << formField->m_fieldNames[Partial];
        names.removeAll(QString());

        formField->m_fieldNames[FullyQualified] = names.join(".");

        std::vector<PDFObjectReference> kids = loader.readReferenceArrayFromDictionary(fieldDictionary, "Kids");
        if (kids.empty())
        {
            // This means, that field's dictionary is merged with annotation's dictionary,
            // so, we will add pointer to self to the form widgets. But we must test, if we
            // really have merged annotation's dictionary - we test it by checking for 'Subtype'
            // presence.
            if (loader.readNameFromDictionary(fieldDictionary, "Subtype") == "Widget")
            {
                formField->m_widgets.emplace_back(PDFFormWidget::parse(storage, reference, formField));
            }
        }
        else
        {
            // Otherwise we must scan all references, and determine, if kid is another form field,
            // or it is a widget annotation. Widget annotations has required field 'Subtype', which
            // has value 'Widget', form field has required field 'Parent' (for non-root fields).

            for (const PDFObjectReference& kid : kids)
            {
                if (const PDFDictionary* childDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(kid)))
                {
                    const bool isWidget = loader.readNameFromDictionary(childDictionary, "Subtype") == "Widget";
                    const bool isField = loader.readReferenceFromDictionary(childDictionary, "Parent").isValid();

                    if (isField)
                    {
                        // This is form field (potentially merged with widget)
                        formField->m_childFields.emplace_back(PDFFormField::parse(storage, kid, formField));
                    }
                    else if (isWidget)
                    {
                        // This is pure widget (with no form field)
                        formField->m_widgets.emplace_back(PDFFormWidget::parse(storage, kid, formField));
                    }
                }
            }

        }

        if (formFieldButton)
        {
            formFieldButton->m_options = loader.readTextStringList(fieldDictionary->get("Opt"));
        }

        if (formFieldText)
        {
            PDFInteger maxLengthDefault = 0;
            QByteArray defaultAppearance;
            Qt::Alignment alignment = 0;

            if (PDFFormFieldText* parentTextField = dynamic_cast<PDFFormFieldText*>(parentField))
            {
                maxLengthDefault = parentTextField->getTextMaximalLength();
                defaultAppearance = parentTextField->getDefaultAppearance();
                alignment = parentTextField->getAlignment();
            }

            if (fieldDictionary->hasKey("DA"))
            {
                defaultAppearance = loader.readStringFromDictionary(fieldDictionary, "DA");
            }
            if (fieldDictionary->hasKey("Q"))
            {
                const PDFInteger quadding = loader.readIntegerFromDictionary(fieldDictionary, "Q", -1);
                switch (quadding)
                {
                    case 0:
                        alignment = Qt::AlignLeft;
                        break;

                    case 1:
                        alignment = Qt::AlignHCenter;
                        break;

                    case 2:
                        alignment = Qt::AlignRight;
                        break;

                    default:
                        break;
                }
            }
            alignment |= Qt::AlignVCenter;

            formFieldText->m_maxLength = loader.readIntegerFromDictionary(fieldDictionary, "MaxLen", maxLengthDefault);
            formFieldText->m_defaultAppearance = defaultAppearance;
            formFieldText->m_alignment = alignment;
            formFieldText->m_defaultStyle = loader.readTextStringFromDictionary(fieldDictionary, "DS", QString());
            formFieldText->m_richTextValue = loader.readTextStringFromDictionary(fieldDictionary, "RV", QString());
        }

        if (formFieldChoice)
        {
            // Parse options - it is array of options values. Option value can be either a single text
            // string, or array of two values - export value and text to be displayed.
            PDFObject options = storage->getObject(fieldDictionary->get("Opt"));
            if (options.isArray())
            {
                const PDFArray* optionsArray = options.getArray();
                formFieldChoice->m_options.reserve(optionsArray->getCount());
                for (size_t i = 0; i < optionsArray->getCount(); ++i)
                {
                    PDFFormFieldChoice::Option option;

                    PDFObject optionItemObject = storage->getObject(optionsArray->getItem(i));
                    if (optionItemObject.isArray())
                    {
                        QStringList stringList = loader.readTextStringList(optionItemObject);
                        if (stringList.size() == 2)
                        {
                            option.exportString = stringList[0];
                            option.userString = stringList[1];
                        }
                    }
                    else
                    {
                        option.userString = loader.readTextString(optionItemObject, QString());
                        option.exportString = option.userString;
                    }

                    formFieldChoice->m_options.emplace_back(qMove(option));
                }
            }

            formFieldChoice->m_topIndex = loader.readIntegerFromDictionary(fieldDictionary, "TI", 0);
            formFieldChoice->m_selection = fieldDictionary->get("I");
        }

        if (formFieldSignature)
        {
            formFieldSignature->m_signature = PDFSignature::parse(storage, fieldDictionary->get("V"));
        }
    }

    return result;
}

bool PDFFormField::setValue(const SetValueParameters& parameters)
{
    Q_UNUSED(parameters);

    // Default behaviour: return false, value cannot be set
    return false;
}

void PDFFormField::resetValue(const ResetValueParameters& parameters)
{
    Q_UNUSED(parameters);

    // Default behaviour: do nothing
}

PDFFormWidget::PDFFormWidget(PDFObjectReference page, PDFObjectReference widget, PDFFormField* parentField, PDFAnnotationAdditionalActions actions) :
    m_page(page),
    m_widget(widget),
    m_parentField(parentField),
    m_actions(qMove(actions))
{

}

PDFFormWidget PDFFormWidget::parse(const PDFObjectStorage* storage, PDFObjectReference reference, PDFFormField* parentField)
{
    PDFObjectReference pageReference;
    PDFAnnotationAdditionalActions actions;
    if (const PDFDictionary* annotationDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference)))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        pageReference = loader.readReferenceFromDictionary(annotationDictionary, "P");
        actions = PDFAnnotationAdditionalActions::parse(storage, annotationDictionary->get("AA"), annotationDictionary->get("A"));
    }

    return PDFFormWidget(pageReference, reference, parentField, qMove(actions));
}

PDFFormFieldButton::ButtonType PDFFormFieldButton::getButtonType() const
{
    if (m_fieldFlags.testFlag(PushButton))
    {
        return ButtonType::PushButton;
    }
    else if (m_fieldFlags.testFlag(Radio))
    {
        return ButtonType::RadioButton;
    }

    return ButtonType::CheckBox;
}

QByteArray PDFFormFieldButton::getOnAppearanceState(const PDFFormManager* formManager, const PDFFormWidget* widget)
{
    Q_ASSERT(formManager);
    Q_ASSERT(widget);

    const PDFDocument* document = formManager->getDocument();
    Q_ASSERT(document);

    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(widget->getWidget())))
    {
        PDFAppeareanceStreams streams = PDFAppeareanceStreams::parse(&document->getStorage(), dictionary->get("AP"));
        QByteArrayList states = streams.getAppearanceStates(PDFAppeareanceStreams::Appearance::Normal);

        for (const QByteArray& state : states)
        {
            if (!state.isEmpty() && state != "Off")
            {
                return state;
            }
        }
    }

    return QByteArray();
}

QByteArray PDFFormFieldButton::getOffAppearanceState(const PDFFormManager* formManager, const PDFFormWidget* widget)
{
    Q_UNUSED(formManager);
    Q_UNUSED(widget);

    // 'Off' value is specified by PDF 1.7 specification. It has always value 'Off'.
    // 'On' values can have different appearance states.
    return "Off";
}

bool PDFFormFieldButton::setValue(const SetValueParameters& parameters)
{
    // Do not allow to set value to push buttons
    if (getFlags().testFlag(PushButton))
    {
        return false;
    }

    // If form field is readonly, and scope is user (form field is changed by user,
    // not by calculated value), then we must not allow value change.
    if (getFlags().testFlag(ReadOnly) && parameters.scope == SetValueParameters::Scope::User)
    {
        return false;
    }

    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);
    Q_ASSERT(parameters.value.isName());

    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    QByteArray state = parameters.value.getString();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), parameters.value);

    // Change widget appearance states
    const bool isRadio = getFlags().testFlag(Radio);
    const bool isRadioInUnison = getFlags().testFlag(RadiosInUnison);
    const bool isSameValueForAllWidgets = !isRadio || isRadioInUnison;
    const bool isAllowedToCheckAllOff = !getFlags().testFlag(NoToggleToOff);
    bool hasWidgets = !m_widgets.empty();
    bool isAnyWidgetToggledOn = false;

    for (const PDFFormWidget& formWidget : getWidgets())
    {
        QByteArray onState = PDFFormFieldButton::getOnAppearanceState(parameters.formManager, &formWidget);

        // We set appearance to 'On' if following two conditions both hold:
        //  1) State equals to widget's "On" state
        //  2) Either we are setting value to invoking widget, or setting of same
        //     value to other widgets is allowed (it is a checkbox, or radio in unison)
        if (state == onState && (isSameValueForAllWidgets || formWidget.getWidget() == parameters.invokingWidget))
        {
            isAnyWidgetToggledOn = true;
            builder->setAnnotationAppearanceState(formWidget.getWidget(), onState);
        }
        else
        {
            QByteArray offState = PDFFormFieldButton::getOffAppearanceState(parameters.formManager, &formWidget);
            builder->setAnnotationAppearanceState(formWidget.getWidget(), offState);
        }
        parameters.modifier->markAnnotationsChanged();
    }

    // We must check, if correct value has been set. If form field has no widgets,
    // but has same qualified name, then no check is performed (just form field value is set
    // to same value in all form fields with same qualified name, according to the PDF specification.
    if (hasWidgets && !isAnyWidgetToggledOn && !isAllowedToCheckAllOff)
    {
        return false;
    }

    return true;
}

void PDFFormFieldButton::resetValue(const ResetValueParameters& parameters)
{
    // Do not allow to reset value of push buttons
    if (getFlags().testFlag(PushButton))
    {
        return;
    }

    Q_ASSERT(parameters.modifier);
    Q_ASSERT(parameters.formManager);

    PDFObject defaultValue = getDefaultValue();
    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), defaultValue);

    PDFDocumentDataLoaderDecorator loader(parameters.formManager->getDocument());
    QByteArray defaultState = loader.readString(defaultValue);

    for (const PDFFormWidget& formWidget : getWidgets())
    {
        QByteArray onState = PDFFormFieldButton::getOnAppearanceState(parameters.formManager, &formWidget);
        if (defaultState == onState)
        {
            builder->setAnnotationAppearanceState(formWidget.getWidget(), onState);
        }
        else
        {
            QByteArray offState = PDFFormFieldButton::getOffAppearanceState(parameters.formManager, &formWidget);
            builder->setAnnotationAppearanceState(formWidget.getWidget(), offState);
        }
        parameters.modifier->markAnnotationsChanged();
    }
}

PDFFormManager::PDFFormManager(PDFDrawWidgetProxy* proxy, QObject* parent) :
    BaseClass(parent),
    m_proxy(proxy),
    m_annotationManager(nullptr),
    m_document(nullptr),
    m_flags(getDefaultApperanceFlags()),
    m_isCommitDisabled(false),
    m_focusedEditor(nullptr)
{
    Q_ASSERT(proxy);
}

PDFFormManager::~PDFFormManager()
{
    clearEditors();
}

PDFAnnotationManager* PDFFormManager::getAnnotationManager() const
{
    return m_annotationManager;
}

void PDFFormManager::setAnnotationManager(PDFAnnotationManager* annotationManager)
{
    m_annotationManager = annotationManager;
}

const PDFDocument* PDFFormManager::getDocument() const
{
    return m_document;
}

void PDFFormManager::setDocument(const PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        PDFTemporaryValueChange change(&m_isCommitDisabled, true);
        m_document = document;

        if (document.hasReset())
        {
            if (m_document)
            {
                m_form = PDFForm::parse(m_document, m_document->getCatalog()->getFormObject());
            }
            else
            {
                // Clean the form
                m_form = PDFForm();
            }

            updateFormWidgetEditors();
        }
        else if (document.hasFlag(PDFModifiedDocument::FormField))
        {
            // Just update field values
            updateFieldValues();
        }
    }
}

PDFFormManager::FormAppearanceFlags PDFFormManager::getAppearanceFlags() const
{
    return m_flags;
}

void PDFFormManager::setAppearanceFlags(FormAppearanceFlags flags)
{
    m_flags = flags;
}

bool PDFFormManager::hasFormFieldWidgetText(PDFObjectReference widgetAnnotation) const
{
    if (const PDFFormField* formField = getFormFieldForWidget(widgetAnnotation))
    {
        switch (formField->getFieldType())
        {
            case PDFFormField::FieldType::Text:
                return true;

            case PDFFormField::FieldType::Choice:
            {
                PDFFormField::FieldFlags flags = formField->getFlags();
                return flags.testFlag(PDFFormField::Combo) && flags.testFlag(PDFFormField::Edit);
            }

            default:
                break;
        }
    }

    return false;
}

PDFFormWidgets PDFFormManager::getWidgets() const
{
    PDFFormWidgets result;

    auto functor = [&result](const PDFFormField* formField)
    {
        const PDFFormWidgets& widgets = formField->getWidgets();
        result.insert(result.cend(), widgets.cbegin(), widgets.cend());
    };
    apply(functor);

    return result;
}

void PDFFormManager::apply(const std::function<void (const PDFFormField*)>& functor) const
{
    m_form.apply(functor);
}

void PDFFormManager::modify(const std::function<void (PDFFormField*)>& functor) const
{
    for (const PDFFormFieldPointer& childField : m_form.getFormFields())
    {
        childField->modify(functor);
    }
}

void PDFFormManager::setFocusToEditor(PDFFormFieldWidgetEditor* editor)
{
    if (m_focusedEditor != editor)
    {
        if (m_focusedEditor)
        {
            m_focusedEditor->setFocus(false);
        }

        m_focusedEditor = editor;

        if (m_focusedEditor)
        {
            m_focusedEditor->setFocus(true);
        }

        // Request repaint, because focus changed
        Q_ASSERT(m_proxy);
        m_proxy->repaintNeeded();
    }
}

bool PDFFormManager::focusNextPrevFormField(bool next)
{
    if (m_widgetEditors.empty())
    {
        return false;
    }

    std::vector<PDFFormFieldWidgetEditor*>::const_iterator newFocusIterator = m_widgetEditors.cend();

    if (!m_focusedEditor)
    {
        // We are setting a new focus
        if (next)
        {
            newFocusIterator = m_widgetEditors.cbegin();
        }
        else
        {
            newFocusIterator = std::prev(m_widgetEditors.cend());
        }
    }
    else
    {
        std::vector<PDFFormFieldWidgetEditor*>::const_iterator it = std::find(m_widgetEditors.cbegin(), m_widgetEditors.cend(), m_focusedEditor);
        Q_ASSERT(it != m_widgetEditors.cend());

        if (next)
        {
            newFocusIterator = std::next(it);
        }
        else if (it != m_widgetEditors.cbegin())
        {
            newFocusIterator = std::prev(it);
        }
    }

    if (newFocusIterator != m_widgetEditors.cend())
    {
        setFocusToEditor(*newFocusIterator);
        return true;
    }
    else
    {
        // Jakub Melka: We must remove focus out of editor, because
        setFocusToEditor(nullptr);
    }

    return false;
}

bool PDFFormManager::isFocused(PDFObjectReference widget) const
{
    if (m_focusedEditor)
    {
        return m_focusedEditor->getWidgetAnnotation() == widget;
    }

    return false;
}

const PDFAction* PDFFormManager::getAction(PDFAnnotationAdditionalActions::Action actionType, const PDFFormWidget* widget)
{
    if (const PDFAction* action = widget->getAction(actionType))
    {
        return action;
    }

    for (const PDFFormField* formField = widget->getParent(); formField; formField = formField->getParentField())
    {
        if (const PDFAction* action = formField->getAction(actionType))
        {
            return action;
        }
    }

    return nullptr;
}

void PDFFormManager::setFormFieldValue(PDFFormField::SetValueParameters parameters)
{
    if (!m_document)
    {
        // This can happen, when we are closing the document and some editor is opened
        return;
    }

    Q_ASSERT(parameters.invokingFormField);
    Q_ASSERT(parameters.invokingWidget.isValid());

    parameters.formManager = this;
    parameters.scope = PDFFormField::SetValueParameters::Scope::User;

    PDFDocumentModifier modifier(m_document);
    modifier.getBuilder()->setFormManager(this);
    parameters.modifier = &modifier;

    if (parameters.invokingFormField->setValue(parameters))
    {
        // We must also set dependent fields with same name
        QString qualifiedFormFieldName = parameters.invokingFormField->getName(PDFFormField::NameType::FullyQualified);
        if (!qualifiedFormFieldName.isEmpty())
        {
            parameters.scope = PDFFormField::SetValueParameters::Scope::Internal;
            auto updateDependentField = [&parameters, &qualifiedFormFieldName](PDFFormField* formField)
            {
                if (parameters.invokingFormField == formField)
                {
                    // Do not update self
                    return;
                }

                if (qualifiedFormFieldName == formField->getName(PDFFormField::NameType::FullyQualified))
                {
                    formField->setValue(parameters);
                }
            };
            modify(updateDependentField);
        }

        if (modifier.finalize())
        {
            emit documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
        }
    }
}

QRectF PDFFormManager::getWidgetRectangle(const PDFFormWidget& widget) const
{
    if (const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(widget.getWidget())))
    {
        PDFDocumentDataLoaderDecorator loader(m_document);
        return loader.readRectangle(dictionary->get("Rect"), QRectF());
    }

    return QRectF();
}


void PDFFormManager::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    if (m_focusedEditor)
    {
        m_focusedEditor->shortcutOverrideEvent(widget, event);
    }
}

void PDFFormManager::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    if (m_focusedEditor)
    {
        m_focusedEditor->keyPressEvent(widget, event);
    }
}

void PDFFormManager::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    if (m_focusedEditor)
    {
        m_focusedEditor->keyReleaseEvent(widget, event);
    }
}

void PDFFormManager::mousePressEvent(QWidget* widget, QMouseEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);

        // We try to set focus on editor
        if (event->button() == Qt::LeftButton)
        {
            setFocusToEditor(info.editor);
        }

        info.editor->mousePressEvent(widget, event, info.mousePosition);
        grabMouse(info, event);
    }
    else if (!isMouseGrabbed())
    {
        // Mouse is not grabbed, user clicked elsewhere, unfocus editor
        setFocusToEditor(nullptr);
    }
}

void PDFFormManager::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);
        info.editor->mouseDoubleClickEvent(widget, event, info.mousePosition);

        // If mouse is grabbed, then event is accepted always (because
        // we get Press event, when we grabbed the mouse, then we will
        // wait for corresponding release event while all mouse move events
        // will be accepted, even if editor doesn't accept them.
        if (isMouseGrabbed())
        {
            event->accept();
        }
    }
}

void PDFFormManager::mouseReleaseEvent(QWidget* widget, QMouseEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);
        info.editor->mouseReleaseEvent(widget, event, info.mousePosition);
        ungrabMouse(info, event);
    }
}

void PDFFormManager::mouseMoveEvent(QWidget* widget, QMouseEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);
        info.editor->mouseMoveEvent(widget, event, info.mousePosition);

        // If mouse is grabbed, then event is accepted always (because
        // we get Press event, when we grabbed the mouse, then we will
        // wait for corresponding release event while all mouse move events
        // will be accepted, even if editor doesn't accept them.
        if (isMouseGrabbed())
        {
            event->accept();
        }

        if (hasFormFieldWidgetText(info.editor->getWidgetAnnotation()))
        {
            m_mouseCursor = QCursor(Qt::IBeamCursor);
        }
        else
        {
            m_mouseCursor = QCursor(Qt::ArrowCursor);
        }
    }
    else
    {
        m_mouseCursor = std::nullopt;
    }
}

void PDFFormManager::wheelEvent(QWidget* widget, QWheelEvent* event)
{
    if (!hasForm())
    {
        return;
    }

    MouseEventInfo info = getMouseEventInfo(widget, event->pos());
    if (info.isValid())
    {
        Q_ASSERT(info.editor);
        info.editor->wheelEvent(widget, event, info.mousePosition);

        // We will accept mouse wheel events, if we are grabbing the mouse.
        // We do not want to zoom in/zoom out while grabbing.
        if (isMouseGrabbed())
        {
            event->accept();
        }
    }
}

void PDFFormManager::grabMouse(const MouseEventInfo& info, QMouseEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick)
    {
        // Double clicks doesn't grab the mouse
        return;
    }

    Q_ASSERT(event->type() == QEvent::MouseButtonPress);

    if (isMouseGrabbed())
    {
        // If mouse is already grabbed, then when new mouse button is pressed,
        // we just increase nesting level and accept the mouse event. We are
        // accepting all mouse events, if mouse is grabbed.
        ++m_mouseGrabInfo.mouseGrabNesting;
        event->accept();
    }
    else if (event->isAccepted())
    {
        // Event is accepted and we are not grabbing the mouse. We must start
        // grabbing the mouse.
        Q_ASSERT(m_mouseGrabInfo.mouseGrabNesting == 0);
        ++m_mouseGrabInfo.mouseGrabNesting;
        m_mouseGrabInfo.info = info;
    }
}

void PDFFormManager::ungrabMouse(const MouseEventInfo& info, QMouseEvent* event)
{
    Q_UNUSED(info);
    Q_ASSERT(event->type() == QEvent::MouseButtonRelease);

    if (isMouseGrabbed())
    {
        // Mouse is being grabbed, decrease nesting level. We must also accept
        // mouse release event, because mouse is being grabbed.
        --m_mouseGrabInfo.mouseGrabNesting;
        event->accept();

        if (!isMouseGrabbed())
        {
            m_mouseGrabInfo.info = MouseEventInfo();
        }
    }

    Q_ASSERT(m_mouseGrabInfo.mouseGrabNesting >= 0);
}

PDFFormManager::MouseEventInfo PDFFormManager::getMouseEventInfo(QWidget* widget, QPoint point)
{
    MouseEventInfo result;

    if (isMouseGrabbed())
    {
        result = m_mouseGrabInfo.info;
        result.mousePosition = result.deviceToWidget.map(point);
        return result;
    }

    std::vector<PDFInteger> currentPages = m_proxy->getWidget()->getDrawWidget()->getCurrentPages();

    if (!m_annotationManager->hasAnyPageAnnotation(currentPages))
    {
        // All pages doesn't have annotation
        return result;
    }

    PDFWidgetSnapshot snapshot = m_proxy->getSnapshot();
    for (const PDFWidgetSnapshot::SnapshotItem& snapshotItem : snapshot.items)
    {
        const PDFAnnotationManager::PageAnnotations& pageAnnotations = m_annotationManager->getPageAnnotations(snapshotItem.pageIndex);
        for (const PDFAnnotationManager::PageAnnotation& pageAnnotation : pageAnnotations.annotations)
        {
            if (pageAnnotation.annotation->isReplyTo())
            {
                // Annotation is reply to another annotation, do not interact with it
                continue;
            }

            if (pageAnnotation.annotation->getType() != AnnotationType::Widget)
            {
                // Annotation is not widget annotation (form field), do not interact with it
                continue;
            }

            if (PDFFormField* formField = getFormFieldForWidget(pageAnnotation.annotation->getSelfReference()))
            {
                PDFFormFieldWidgetEditor* editor = getEditor(formField);
                QRectF annotationRect = editor ? editor->getActiveEditorRectangle() : QRectF();
                if (!annotationRect.isValid())
                {
                    annotationRect = pageAnnotation.annotation->getRectangle();
                }
                QMatrix widgetToDevice = m_annotationManager->prepareTransformations(snapshotItem.pageToDeviceMatrix, widget, pageAnnotation.annotation->getEffectiveFlags(), m_document->getCatalog()->getPage(snapshotItem.pageIndex), annotationRect);

                QPainterPath path;
                path.addRect(annotationRect);
                path = widgetToDevice.map(path);

                if (path.contains(point))
                {
                    result.formField = formField;
                    result.deviceToWidget = widgetToDevice.inverted();
                    result.mousePosition = result.deviceToWidget.map(point);
                    result.editor = editor;
                    return result;
                }
            }
        }
    }

    return result;
}

const std::optional<QCursor>& PDFFormManager::getCursor() const
{
    return m_mouseCursor;
}

void PDFFormManager::clearEditors()
{
    qDeleteAll(m_widgetEditors);
    m_widgetEditors.clear();
}

void PDFFormManager::updateFormWidgetEditors()
{
    setFocusToEditor(nullptr);
    clearEditors();

    for (PDFFormWidget widget : getWidgets())
    {
        const PDFFormField* formField = widget.getParent();
        switch (formField->getFieldType())
        {
            case PDFFormField::FieldType::Button:
            {
                Q_ASSERT(dynamic_cast<const PDFFormFieldButton*>(formField));
                const PDFFormFieldButton* formFieldButton = static_cast<const PDFFormFieldButton*>(formField);
                switch (formFieldButton->getButtonType())
                {
                    case PDFFormFieldButton::ButtonType::PushButton:
                    {
                        m_widgetEditors.push_back(new PDFFormFieldPushButtonEditor(this, widget));
                        break;
                    }

                    case PDFFormFieldButton::ButtonType::RadioButton:
                    case PDFFormFieldButton::ButtonType::CheckBox:
                    {
                        m_widgetEditors.push_back(new PDFFormFieldCheckableButtonEditor(this, widget));
                        break;
                    }

                    default:
                        Q_ASSERT(false);
                        break;
                }

                break;
            }

            case PDFFormField::FieldType::Text:
            {
                m_widgetEditors.push_back(new PDFFormFieldTextBoxEditor(this, widget));
                break;
            }

            case PDFFormField::FieldType::Choice:
            {
                Q_ASSERT(dynamic_cast<const PDFFormFieldChoice*>(formField));
                const PDFFormFieldChoice* formFieldChoice = static_cast<const PDFFormFieldChoice*>(formField);
                if (formFieldChoice->isComboBox())
                {
                    m_widgetEditors.push_back(new PDFFormFieldComboBoxEditor(this, widget));
                }
                else if (formFieldChoice->isListBox())
                {
                    m_widgetEditors.push_back(new PDFFormFieldListBoxEditor(this, widget));
                }
                else
                {
                    // Uknown field choice
                    Q_ASSERT(false);
                }

                break;
            }

            case PDFFormField::FieldType::Signature:
                // Signature fields doesn't have editor
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }
}

void PDFFormManager::updateFieldValues()
{
    if (m_document)
    {
        for (const PDFFormFieldPointer& childField : m_form.getFormFields())
        {
            childField->reloadValue(&m_document->getStorage(), PDFObject());
        }

        for (PDFFormFieldWidgetEditor* editor : m_widgetEditors)
        {
            editor->reloadValue();
        }
    }
}

PDFFormFieldWidgetEditor* PDFFormManager::getEditor(const PDFFormField* formField) const
{
    for (PDFFormFieldWidgetEditor* editor : m_widgetEditors)
    {
        if (editor->getFormField() == formField)
        {
            return editor;
        }
    }

    return nullptr;
}

void PDFFormManager::performResetAction(const PDFActionResetForm* action)
{
    Q_ASSERT(action);
    Q_ASSERT(m_document);

    PDFDocumentModifier modifier(m_document);
    modifier.getBuilder()->setFormManager(this);

    auto resetFieldValue = [this, action, &modifier](PDFFormField* formField)
    {
        const PDFFormAction::FieldList& fieldList = action->getFieldList();

        // Akceptujeme form field dle daného filtru?
        bool accept = false;
        bool isInFieldList = std::find(fieldList.fieldReferences.cbegin(), fieldList.fieldReferences.cend(), formField->getSelfReference()) != fieldList.fieldReferences.cend() ||
                             fieldList.qualifiedNames.contains(formField->getName(PDFFormField::NameType::FullyQualified));
        switch (action->getFieldScope())
        {
            case PDFFormAction::FieldScope::All:
                accept = true;
                break;

            case PDFFormAction::FieldScope::Include:
                accept = isInFieldList;
                break;

            case PDFFormAction::FieldScope::Exclude:
                accept = !isInFieldList;
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        if (accept)
        {
            PDFFormField::ResetValueParameters parameters;
            parameters.formManager = this;
            parameters.modifier = &modifier;
            formField->resetValue(parameters);
        }
    };
    modify(resetFieldValue);

    if (modifier.finalize())
    {
        emit documentModified(PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags()));
    }
}

PDFFormFieldWidgetEditor::PDFFormFieldWidgetEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    m_formManager(formManager),
    m_formWidget(formWidget),
    m_hasFocus(false)
{
    Q_ASSERT(m_formManager);
}

void PDFFormFieldWidgetEditor::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
}

void PDFFormFieldWidgetEditor::mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
    Q_UNUSED(mousePagePosition);
}

void PDFFormFieldWidgetEditor::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
    Q_UNUSED(mousePagePosition);
}

void PDFFormFieldWidgetEditor::mouseReleaseEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
    Q_UNUSED(mousePagePosition);
}

void PDFFormFieldWidgetEditor::mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
    Q_UNUSED(mousePagePosition);
}

void PDFFormFieldWidgetEditor::wheelEvent(QWidget* widget, QWheelEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(widget);
    Q_UNUSED(event);
    Q_UNUSED(mousePagePosition);
}

void PDFFormFieldWidgetEditor::setFocus(bool hasFocus)
{
    if (m_hasFocus != hasFocus)
    {
        m_hasFocus = hasFocus;
        setFocusImpl(m_hasFocus);
    }
}

void PDFFormFieldWidgetEditor::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    Q_UNUSED(parameters);
    Q_UNUSED(edit)
}

void PDFFormFieldWidgetEditor::performKeypadNavigation(QWidget* widget, QKeyEvent* event)
{
    int key = event->key();

    const bool isLeft = key == Qt::Key_Left;
    const bool isRight = key == Qt::Key_Right;
    const bool isDown = key == Qt::Key_Down;
    const bool isHorizontal = isLeft || isRight;

    Qt::NavigationMode navigationMode = Qt::NavigationModeKeypadDirectional;
#ifdef QT_KEYPAD_NAVIGATION
    navigationMode = QApplication::navigationMode();
#endif

    switch (navigationMode)
    {
        case Qt::NavigationModeKeypadTabOrder:
        {
            // According the Qt's documentation, Up/Down arrows are used
            // to change focus. So, if user pressed Left/Right, we must
            // ignore this event.
            if (isHorizontal)
            {
                return;
            }
            break;
        }

        case Qt::NavigationModeKeypadDirectional:
            // Default behaviour
            break;

        default:
            // Nothing happens
            return;
    }

    bool next = false;
    if (isHorizontal)
    {
        switch (widget->layoutDirection())
        {
            case Qt::LeftToRight:
            case Qt::LayoutDirectionAuto:
                next = isRight;
                break;

            case Qt::RightToLeft:
                next = isLeft;
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }
    else
    {
        // Vertical
        next = isDown;
    }

    m_formManager->focusNextPrevFormField(next);
}

PDFFormFieldPushButtonEditor::PDFFormFieldPushButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    BaseClass(formManager, formWidget)
{

}

PDFFormFieldAbstractButtonEditor::PDFFormFieldAbstractButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    BaseClass(formManager, formWidget)
{

}

void PDFFormFieldAbstractButtonEditor::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    switch (event->key())
    {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            event->accept();
            break;
        }

        default:
            break;
    }
}

void PDFFormFieldAbstractButtonEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        {
            click();
            event->accept();
            break;
        }

        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            performKeypadNavigation(widget, event);
            break;
        }

        default:
            break;
    }
}

void PDFFormFieldAbstractButtonEditor::keyReleaseEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    switch (event->key())
    {
        case Qt::Key_Select:
        case Qt::Key_Space:
        {
            click();
            event->accept();
            break;
        }

        default:
            break;
    }
}

void PDFFormFieldAbstractButtonEditor::mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(widget);
    Q_UNUSED(mousePagePosition);

    if (event->button() == Qt::LeftButton)
    {
        click();
        event->accept();
    }
}

void PDFFormFieldPushButtonEditor::click()
{
    if (const PDFAction* action = m_formManager->getAction(PDFAnnotationAdditionalActions::MousePressed, getFormWidget()))
    {
        emit m_formManager->actionTriggered(action);
    }
    else if (const PDFAction* action = m_formManager->getAction(PDFAnnotationAdditionalActions::Default, getFormWidget()))
    {
        emit m_formManager->actionTriggered(action);
    }
}

PDFFormFieldCheckableButtonEditor::PDFFormFieldCheckableButtonEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    BaseClass(formManager, formWidget)
{

}

void PDFFormFieldCheckableButtonEditor::click()
{
    QByteArray newState;

    // First, check current state of form field
    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());
    QByteArray state = loader.readName(m_formWidget.getParent()->getValue());
    QByteArray onState = PDFFormFieldButton::getOnAppearanceState(m_formManager, &m_formWidget);
    if (state != onState)
    {
        newState = onState;
    }
    else
    {
        newState = PDFFormFieldButton::getOffAppearanceState(m_formManager, &m_formWidget);
    }

    // We have a new state, try to apply it to form field
    PDFFormField::SetValueParameters parameters;
    parameters.formManager = m_formManager;
    parameters.invokingWidget = m_formWidget.getWidget();
    parameters.invokingFormField = m_formWidget.getParent();
    parameters.scope = PDFFormField::SetValueParameters::Scope::User;
    parameters.value = PDFObject::createName(qMove(newState));
    m_formManager->setFormFieldValue(parameters);
}

PDFFormFieldComboBoxEditor::PDFFormFieldComboBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    BaseClass(formManager, formWidget),
    m_textEdit(getTextEditFlags(formWidget.getParent()->getFlags())),
    m_listBox(formWidget.getParent()->getFlags()),
    m_listBoxVisible(false)
{
    const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    initializeTextEdit(&m_textEdit);

    QFontMetricsF fontMetrics(m_textEdit.getFont());
    const qreal lineSpacing = fontMetrics.lineSpacing();

    const int listBoxItems = qMin(7, int(parentField->getOptions().size()));
    QRectF comboBoxRectangle = m_formManager->getWidgetRectangle(m_formWidget);
    QRectF listBoxPopupRectangle = comboBoxRectangle;
    listBoxPopupRectangle.translate(0, -lineSpacing * (listBoxItems));
    listBoxPopupRectangle.setHeight(lineSpacing * listBoxItems);
    m_listBoxPopupRectangle = listBoxPopupRectangle;
    m_dropDownButtonRectangle = comboBoxRectangle;
    m_dropDownButtonRectangle.setLeft(m_dropDownButtonRectangle.right() - m_dropDownButtonRectangle.height());
    initializeListBox(&m_listBox);
}

void PDFFormFieldComboBoxEditor::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    if (!m_hasFocus || !m_listBoxVisible)
    {
        m_textEdit.shortcutOverrideEvent(widget, event);
    }
    else
    {
        m_listBox.shortcutOverrideEvent(widget, event);
    }
}

void PDFFormFieldComboBoxEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_ASSERT(!m_textEdit.isMultiline());

    if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
    {
        // If popup list box is shown, then use text and hide it,
        // otherwise close the editor.
        if (m_listBoxVisible)
        {
            if (m_listBox.isSingleItemSelected())
            {
                m_textEdit.setText(m_listBox.getSelectedItemText());
            }
            m_listBoxVisible = false;
        }
        else
        {
            m_formManager->setFocusToEditor(nullptr);
        }

        event->accept();
    }
    else if (event->key() == Qt::Key_Escape)
    {
        // If popup list box is shown, hide it, if not, cancel the editing
        if (m_listBoxVisible)
        {
            m_listBoxVisible = false;
        }
        else
        {
            reloadValue();
            m_formManager->setFocusToEditor(nullptr);
        }
        event->accept();
    }
    else if (!m_listBoxVisible)
    {
        m_textEdit.keyPressEvent(widget, event);
    }
    else
    {
        m_listBox.keyPressEvent(widget, event);
    }

    if (event->isAccepted())
    {
        widget->update();
    }
}

void PDFFormFieldComboBoxEditor::mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    if (event->button() == Qt::LeftButton && m_hasFocus)
    {
        // If popup list box is visible, delegate mouse click to
        // this list box only.
        if (m_listBoxVisible)
        {
            const int index = m_listBox.getIndexFromWidgetPosition(mousePagePosition);
            m_listBox.setCurrentItem(index, event->modifiers());

            if (m_listBox.isSingleItemSelected())
            {
                m_textEdit.setText(m_listBox.getSelectedItemText());
            }

            m_listBoxVisible = false;
        }
        else
        {
            // Do we click popup button?
            if (m_dropDownButtonRectangle.contains(mousePagePosition))
            {
                m_listBoxVisible = true;
                updateListBoxSelection();
            }
            else
            {
                const int cursorPosition = m_textEdit.getCursorPositionFromWidgetPosition(mousePagePosition, m_hasFocus);
                m_textEdit.setCursorPosition(cursorPosition, event->modifiers() & Qt::ShiftModifier);
            }
        }

        event->accept();
        widget->update();
    }
}

void PDFFormFieldComboBoxEditor::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    if (event->button() == Qt::LeftButton)
    {
        const int cursorPosition = m_textEdit.getCursorPositionFromWidgetPosition(mousePagePosition, m_hasFocus);
        m_textEdit.setCursorPosition(cursorPosition, false);
        m_textEdit.setCursorPosition(m_textEdit.getCursorWordBackward(), false);
        m_textEdit.setCursorPosition(m_textEdit.getCursorWordForward(), true);

        event->accept();
        widget->update();
    }
}

void PDFFormFieldComboBoxEditor::mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    // We must test, if left mouse button is pressed while
    // we are moving the mouse - if yes, then select the text.
    if (event->buttons() & Qt::LeftButton && !m_listBoxVisible)
    {
        const int cursorPosition = m_textEdit.getCursorPositionFromWidgetPosition(mousePagePosition, m_hasFocus);
        m_textEdit.setCursorPosition(cursorPosition, true);

        event->accept();
        widget->update();
    }
}

void PDFFormFieldComboBoxEditor::wheelEvent(QWidget* widget, QWheelEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(mousePagePosition);

    if (m_listBoxVisible)
    {
        if (event->angleDelta().y() < 0)
        {
            m_listBox.scrollTo(m_listBox.getValidIndex(m_listBox.getTopItemIndex() + m_listBox.getViewportRowCount()));
        }
        else
        {
            m_listBox.scrollTo(m_listBox.getValidIndex(m_listBox.getTopItemIndex() - 1));
        }

        widget->update();
        event->accept();
    }
}

void PDFFormFieldComboBoxEditor::updateListBoxSelection()
{
    QString text = m_textEdit.getText();

    const int index = m_listBox.findOption(text);
    if (m_listBox.isValidIndex(index))
    {
        m_listBox.setSelection({ index }, true);
        m_listBox.scrollTo(index);
    }
    else
    {
        m_listBox.setTopItemIndex(0);
        m_listBox.setSelection({ }, true);
    }
}

void PDFFormFieldComboBoxEditor::reloadValue()
{
    const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());
    m_textEdit.setText(loader.readTextString(m_formWidget.getParent()->getValue(), QString()));

    m_listBoxVisible = false;
}

void PDFFormFieldComboBoxEditor::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    if (edit)
    {
        // Draw text edit always
        {
            PDFPainterStateGuard guard(parameters.painter);
            m_textEdit.draw(parameters, true);
        }

        // Draw down button
        {
            PDFPainterStateGuard guard(parameters.painter);

            parameters.painter->translate(m_dropDownButtonRectangle.bottomLeft());
            parameters.painter->scale(1.0, -1.0);

            QStyleOption option;
            option.state = QStyle::State_Enabled;
            option.rect = QRect(0, 0, qFloor(m_dropDownButtonRectangle.width()), qFloor(m_dropDownButtonRectangle.height()));
            QApplication::style()->drawPrimitive(QStyle::PE_IndicatorButtonDropDown, &option, parameters.painter, nullptr);
        }

        if (m_listBoxVisible)
        {
            PDFPainterStateGuard guard(parameters.painter);

            AnnotationDrawParameters listBoxParameters = parameters;
            listBoxParameters.boundingRectangle = m_listBoxPopupRectangle;

            QColor color = parameters.invertColors ? Qt::black : Qt::white;
            listBoxParameters.painter->fillRect(listBoxParameters.boundingRectangle, color);

            m_listBox.draw(listBoxParameters, true);
        }
    }
    else
    {
        // Draw static contents
        PDFTextEditPseudowidget pseudowidget(m_formWidget.getParent()->getFlags());
        initializeTextEdit(&pseudowidget);
        pseudowidget.draw(parameters, false);
    }
}

QRectF PDFFormFieldComboBoxEditor::getActiveEditorRectangle() const
{
    if (m_hasFocus && m_listBoxVisible)
    {
        return m_textEdit.getWidgetRect().united(m_listBoxPopupRectangle);
    }

    return QRectF();
}

void PDFFormFieldComboBoxEditor::initializeTextEdit(PDFTextEditPseudowidget* textEdit) const
{
    const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());

    QByteArray defaultAppearance = m_formManager->getForm()->getDefaultAppearance().value_or(QByteArray());
    Qt::Alignment alignment = m_formManager->getForm()->getDefaultAlignment();

    // Initialize text edit
    textEdit->setAppearance(PDFAnnotationDefaultAppearance::parse(defaultAppearance), alignment, m_formManager->getWidgetRectangle(m_formWidget), 0);
    textEdit->setText(loader.readTextString(parentField->getValue(), QString()));
}

void PDFFormFieldComboBoxEditor::initializeListBox(PDFListBoxPseudowidget* listBox) const
{
    const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    // Initialize popup list box
    listBox->initialize(m_textEdit.getFont(), m_textEdit.getFontColor(), m_formManager->getForm()->getDefaultAlignment(),  m_listBoxPopupRectangle, parentField->getOptions(), 0, { });
}

void PDFFormFieldComboBoxEditor::setFocusImpl(bool focused)
{
    if (focused)
    {
        m_textEdit.setCursorPosition(m_textEdit.getPositionEnd(), false);
        m_textEdit.performSelectAll();
    }
    else if (!m_formManager->isCommitDisabled())
    {
        // If text has been changed, then commit it
        PDFObject object = PDFObjectFactory::createTextString(m_textEdit.getText());

        if (object != m_formWidget.getParent()->getValue())
        {
            PDFFormField::SetValueParameters parameters;
            parameters.formManager = m_formManager;
            parameters.invokingWidget = m_formWidget.getWidget();
            parameters.invokingFormField = m_formWidget.getParent();
            parameters.scope = PDFFormField::SetValueParameters::Scope::User;
            parameters.value = qMove(object);
            m_formManager->setFormFieldValue(parameters);
        }
    }
}

PDFFormField::FieldFlags PDFFormFieldComboBoxEditor::getTextEditFlags(PDFFormField::FieldFlags flags)
{
    if (flags.testFlag(PDFFormField::ReadOnly) || !flags.testFlag(PDFFormField::Edit))
    {
        return PDFFormField::ReadOnly;
    }

    return PDFFormField::None;
}

void PDFFormFieldTextBoxEditor::initializeTextEdit(PDFTextEditPseudowidget* textEdit) const
{
    const PDFFormFieldText* parentField = dynamic_cast<const PDFFormFieldText*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());

    QByteArray defaultAppearance = parentField->getDefaultAppearance();
    if (defaultAppearance.isEmpty())
    {
        defaultAppearance = m_formManager->getForm()->getDefaultAppearance().value_or(QByteArray());
    }
    Qt::Alignment alignment = parentField->getAlignment();
    if (!(alignment & Qt::AlignHorizontal_Mask))
    {
        alignment |= m_formManager->getForm()->getDefaultAlignment() & Qt::AlignHorizontal_Mask;
    }

    // Initialize text edit
    textEdit->setAppearance(PDFAnnotationDefaultAppearance::parse(defaultAppearance), alignment, m_formManager->getWidgetRectangle(m_formWidget), parentField->getTextMaximalLength());
    textEdit->setText(loader.readTextString(parentField->getValue(), QString()));
}

void PDFFormFieldTextBoxEditor::setFocusImpl(bool focused)
{
    if (focused)
    {
        m_textEdit.setCursorPosition(m_textEdit.getPositionEnd(), false);
        m_textEdit.performSelectAll();
    }
    else if (!m_textEdit.isPassword() && !m_formManager->isCommitDisabled()) // Passwords are not saved in the document
    {
        // If text has been changed, then commit it
        PDFObject object = PDFObjectFactory::createTextString(m_textEdit.getText());

        if (object != m_formWidget.getParent()->getValue())
        {
            PDFFormField::SetValueParameters parameters;
            parameters.formManager = m_formManager;
            parameters.invokingWidget = m_formWidget.getWidget();
            parameters.invokingFormField = m_formWidget.getParent();
            parameters.scope = PDFFormField::SetValueParameters::Scope::User;
            parameters.value = qMove(object);
            m_formManager->setFormFieldValue(parameters);
        }
    }
}

PDFFormFieldTextBoxEditor::PDFFormFieldTextBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    BaseClass(formManager, formWidget),
    m_textEdit(formWidget.getParent()->getFlags())
{
    initializeTextEdit(&m_textEdit);
}

void PDFFormFieldTextBoxEditor::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    m_textEdit.shortcutOverrideEvent(widget, event);
}

void PDFFormFieldTextBoxEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    if (!m_textEdit.isMultiline() && (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
    {
        // Commit the editor
        m_formManager->setFocusToEditor(nullptr);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        // Cancel the editor
        reloadValue();
        m_formManager->setFocusToEditor(nullptr);
        event->accept();
        return;
    }

    m_textEdit.keyPressEvent(widget, event);

    if (event->isAccepted())
    {
        widget->update();
    }
}

void PDFFormFieldTextBoxEditor::mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    if (event->button() == Qt::LeftButton)
    {
        const int cursorPosition = m_textEdit.getCursorPositionFromWidgetPosition(mousePagePosition, m_hasFocus);
        m_textEdit.setCursorPosition(cursorPosition, event->modifiers() & Qt::ShiftModifier);

        event->accept();
        widget->update();
    }
}

void PDFFormFieldTextBoxEditor::mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    if (event->button() == Qt::LeftButton)
    {
        const int cursorPosition = m_textEdit.getCursorPositionFromWidgetPosition(mousePagePosition, m_hasFocus);
        m_textEdit.setCursorPosition(cursorPosition, false);
        m_textEdit.setCursorPosition(m_textEdit.getCursorWordBackward(), false);
        m_textEdit.setCursorPosition(m_textEdit.getCursorWordForward(), true);

        event->accept();
        widget->update();
    }
}

void PDFFormFieldTextBoxEditor::mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    // We must test, if left mouse button is pressed while
    // we are moving the mouse - if yes, then select the text.
    if (event->buttons() & Qt::LeftButton)
    {
        const int cursorPosition = m_textEdit.getCursorPositionFromWidgetPosition(mousePagePosition, m_hasFocus);
        m_textEdit.setCursorPosition(cursorPosition, true);

        event->accept();
        widget->update();
    }
}

void PDFFormFieldTextBoxEditor::reloadValue()
{
    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());
    m_textEdit.setText(loader.readTextString(m_formWidget.getParent()->getValue(), QString()));
}

void PDFFormFieldTextBoxEditor::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    if (edit)
    {
        m_textEdit.draw(parameters, true);
    }
    else
    {
        // Draw static contents
        PDFTextEditPseudowidget pseudowidget(m_formWidget.getParent()->getFlags());
        initializeTextEdit(&pseudowidget);
        pseudowidget.draw(parameters, false);
    }
}

PDFTextEditPseudowidget::PDFTextEditPseudowidget(PDFFormField::FieldFlags flags) :
    m_flags(flags),
    m_selectionStart(0),
    m_selectionEnd(0),
    m_positionCursor(0),
    m_maxTextLength(0)
{
    m_textLayout.setCacheEnabled(true);
    m_passwordReplacementCharacter = QApplication::style()->styleHint(QStyle::SH_LineEdit_PasswordCharacter);
}

void PDFTextEditPseudowidget::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);
    constexpr QKeySequence::StandardKey acceptedKeys[] = { QKeySequence::Delete, QKeySequence::Cut, QKeySequence::Copy, QKeySequence::Paste,
                                                           QKeySequence::SelectAll, QKeySequence::MoveToNextChar, QKeySequence::MoveToPreviousChar,
                                                           QKeySequence::MoveToNextWord, QKeySequence::MoveToPreviousWord, QKeySequence::MoveToNextLine,
                                                           QKeySequence::MoveToPreviousLine, QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
                                                           QKeySequence::MoveToStartOfBlock, QKeySequence::MoveToEndOfBlock, QKeySequence::MoveToStartOfDocument,
                                                           QKeySequence::MoveToEndOfDocument, QKeySequence::SelectNextChar, QKeySequence::SelectPreviousChar,
                                                           QKeySequence::SelectNextWord, QKeySequence::SelectPreviousWord, QKeySequence::SelectNextLine,
                                                           QKeySequence::SelectPreviousLine, QKeySequence::SelectStartOfLine, QKeySequence::SelectEndOfLine,
                                                           QKeySequence::SelectStartOfBlock, QKeySequence::SelectEndOfBlock, QKeySequence::SelectStartOfDocument,
                                                           QKeySequence::SelectEndOfDocument, QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord,
                                                           QKeySequence::DeleteEndOfLine, QKeySequence::Deselect, QKeySequence::DeleteCompleteLine, QKeySequence::Backspace };

    if (std::any_of(std::begin(acceptedKeys), std::end(acceptedKeys), [event](QKeySequence::StandardKey standardKey) { return event == standardKey; }))
    {
        event->accept();
        return;
    }

    switch (event->key())
    {
        case Qt::Key_Direction_L:
        case Qt::Key_Direction_R:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            event->accept();
            break;

        default:
            break;
    }

    if (!event->text().isEmpty())
    {
        event->accept();
        for (const QChar& character : event->text())
        {
            if (!character.isPrint())
            {
                event->ignore();
                break;
            }
        }
    }
}

void PDFTextEditPseudowidget::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    /*
       We will support following key sequences:
            Delete
            Cut,
            Copy,
            Paste,
            SelectAll,
            MoveToNextChar,
            MoveToPreviousChar,
            MoveToNextWord,
            MoveToPreviousWord,
            MoveToNextLine,
            MoveToPreviousLine,
            MoveToStartOfLine,
            MoveToEndOfLine,
            MoveToStartOfBlock,
            MoveToEndOfBlock,
            MoveToStartOfDocument,
            MoveToEndOfDocument,
            SelectNextChar,
            SelectPreviousChar,
            SelectNextWord,
            SelectPreviousWord,
            SelectNextLine,
            SelectPreviousLine,
            SelectStartOfLine,
            SelectEndOfLine,
            SelectStartOfBlock,
            SelectEndOfBlock,
            SelectStartOfDocument,
            SelectEndOfDocument,
            DeleteStartOfWord,
            DeleteEndOfWord,
            DeleteEndOfLine,
            Deselect,
            DeleteCompleteLine,
            Backspace,
     * */

    event->accept();

    if (event == QKeySequence::Delete)
    {
        performDelete();
    }
    else if (event == QKeySequence::Cut)
    {
        performCut();
    }
    else if (event == QKeySequence::Copy)
    {
        performCopy();
    }
    else if (event == QKeySequence::Paste)
    {
        performPaste();
    }
    else if (event == QKeySequence::SelectAll)
    {
        setSelection(0, getTextLength());
    }
    else if (event == QKeySequence::MoveToNextChar)
    {
        setCursorPosition(getCursorCharacterForward(), false);
    }
    else if (event == QKeySequence::MoveToPreviousChar)
    {
        setCursorPosition(getCursorCharacterBackward(), false);
    }
    else if (event == QKeySequence::MoveToNextWord)
    {
        setCursorPosition(getCursorWordForward(), false);
    }
    else if (event == QKeySequence::MoveToPreviousWord)
    {
        setCursorPosition(getCursorWordBackward(), false);
    }
    else if (event == QKeySequence::MoveToNextLine)
    {
        setCursorPosition(getCursorLineDown(), false);
    }
    else if (event == QKeySequence::MoveToPreviousLine)
    {
        setCursorPosition(getCursorLineUp(), false);
    }
    else if (event == QKeySequence::MoveToStartOfLine || event == QKeySequence::MoveToStartOfBlock)
    {
        setCursorPosition(getCursorLineStart(), false);
    }
    else if (event == QKeySequence::MoveToEndOfLine || event == QKeySequence::MoveToEndOfBlock)
    {
        setCursorPosition(getCursorLineEnd(), false);
    }
    else if (event == QKeySequence::MoveToStartOfDocument)
    {
        setCursorPosition(getCursorDocumentStart(), false);
    }
    else if (event == QKeySequence::MoveToEndOfDocument)
    {
        setCursorPosition(getCursorDocumentEnd(), false);
    }
    else if (event == QKeySequence::SelectNextChar)
    {
        setCursorPosition(getCursorCharacterForward(), true);
    }
    else if (event == QKeySequence::SelectPreviousChar)
    {
        setCursorPosition(getCursorCharacterBackward(), true);
    }
    else if (event == QKeySequence::SelectNextWord)
    {
        setCursorPosition(getCursorWordForward(), true);
    }
    else if (event == QKeySequence::SelectPreviousWord)
    {
        setCursorPosition(getCursorWordBackward(), true);
    }
    else if (event == QKeySequence::SelectNextLine)
    {
        setCursorPosition(getCursorLineDown(), true);
    }
    else if (event == QKeySequence::SelectPreviousLine)
    {
        setCursorPosition(getCursorLineUp(), true);
    }
    else if (event == QKeySequence::SelectStartOfLine || event == QKeySequence::SelectStartOfBlock)
    {
        setCursorPosition(getCursorLineStart(), true);
    }
    else if (event == QKeySequence::SelectEndOfLine || event == QKeySequence::SelectEndOfBlock)
    {
        setCursorPosition(getCursorLineEnd(), true);
    }
    else if (event == QKeySequence::SelectStartOfDocument)
    {
        setCursorPosition(getCursorDocumentStart(), true);
    }
    else if (event == QKeySequence::SelectEndOfDocument)
    {
        setCursorPosition(getCursorDocumentEnd(), true);
    }
    else if (event == QKeySequence::DeleteStartOfWord)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorWordBackward(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::DeleteEndOfWord)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorWordForward(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::DeleteEndOfLine)
    {
        if (!isReadonly())
        {
            setCursorPosition(getCursorLineEnd(), true);
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::Deselect)
    {
        clearSelection();
    }
    else if (event == QKeySequence::DeleteCompleteLine)
    {
        if (!isReadonly())
        {
            m_selectionStart = getCurrentLineTextStart();
            m_selectionEnd = getCurrentLineTextEnd();
            performRemoveSelectedText();
        }
    }
    else if (event == QKeySequence::Backspace || event->key() == Qt::Key_Backspace)
    {
        performBackspace();
    }
    else if (event->key() == Qt::Key_Direction_L)
    {
        QTextOption option = m_textLayout.textOption();
        option.setTextDirection(Qt::LeftToRight);
        m_textLayout.setTextOption(qMove(option));
        updateTextLayout();
    }
    else if (event->key() == Qt::Key_Direction_R)
    {
        QTextOption option = m_textLayout.textOption();
        option.setTextDirection(Qt::LeftToRight);
        m_textLayout.setTextOption(qMove(option));
        updateTextLayout();
    }
    else if (event->key() == Qt::Key_Up)
    {
        setCursorPosition(getCursorLineUp(), event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Down)
    {
        setCursorPosition(getCursorLineDown(), event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Left)
    {
        const int position = (event->modifiers().testFlag(Qt::ControlModifier)) ? getCursorWordBackward() : getCursorCharacterBackward();
        setCursorPosition(position, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (event->key() == Qt::Key_Right)
    {
        const int position = (event->modifiers().testFlag(Qt::ControlModifier)) ? getCursorWordForward() : getCursorCharacterForward();
        setCursorPosition(position, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    else if (isMultiline() && (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
    {
        performInsertText(QString::fromUtf16(u"\u2028"));
    }
    else
    {
        QString text = event->text();
        if (!text.isEmpty())
        {
            performInsertText(text);
        }
        else
        {
            event->ignore();
        }
    }
}

void PDFTextEditPseudowidget::setSelection(int startPosition, int selectionLength)
{
    if (selectionLength > 0)
    {
        // We are selecting to the right
        m_selectionStart = startPosition;
        m_selectionEnd = qMin(startPosition + selectionLength, getTextLength());
        m_positionCursor = m_selectionEnd;
    }
    else if (selectionLength < 0)
    {
        // We are selecting to the left
        m_selectionStart = qMax(startPosition + selectionLength, 0);
        m_selectionEnd = startPosition;
        m_positionCursor = m_selectionStart;
    }
    else
    {
        // Clear the selection
        m_selectionStart = 0;
        m_selectionEnd = 0;
        m_positionCursor = startPosition;
    }
}

void PDFTextEditPseudowidget::setCursorPosition(int position, bool select)
{
    if (select)
    {
        const bool isTextSelected = this->isTextSelected();
        const bool isCursorAtStartOfSelection = isTextSelected && m_selectionStart == m_positionCursor;
        const bool isCursorAtEndOfSelection = isTextSelected && m_selectionEnd == m_positionCursor;

        // Do we have selected text, and cursor is at the end of selected text?
        // In this case, we must preserve start of the selection (we are manipulating
        // with the end of selection.
        if (isCursorAtEndOfSelection)
        {
            m_selectionStart = qMin(m_selectionStart, position);
            m_selectionEnd = qMax(m_selectionStart, position);
        }
        else if (isCursorAtStartOfSelection)
        {
            // We must preserve end of the text selection, because we are manipulating
            // with start of text selection.
            m_selectionStart = qMin(m_selectionEnd, position);
            m_selectionEnd = qMax(m_selectionEnd, position);
        }
        else
        {
            // Otherwise we are manipulating with cursor
            m_selectionStart = qMin(m_positionCursor, position);
            m_selectionEnd = qMax(m_positionCursor, position);
        }
    }

    // Why we are clearing text selection, even if we doesn't have it?
    // We can have, for example m_selectionStart == m_selectionEnd == 3,
    // and we want to have it both zero.
    if (!select || !isTextSelected())
    {
        clearSelection();
    }

    m_positionCursor = position;
}

void PDFTextEditPseudowidget::setText(const QString& text)
{
    clearSelection();
    m_editText = text;
    setCursorPosition(getPositionEnd(), false);
    updateTextLayout();
}

void PDFTextEditPseudowidget::setAppearance(const PDFAnnotationDefaultAppearance& appearance, Qt::Alignment textAlignment, QRectF rect, int maxTextLength)
{
    // Set appearance
    qreal fontSize = appearance.getFontSize();
    if (qFuzzyIsNull(fontSize))
    {
        fontSize = rect.height();
    }

    QFont font(appearance.getFontName());
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPixelSize(qCeil(fontSize));
    font.setStyleStrategy(QFont::ForceOutline);
    m_textLayout.setFont(font);

    QTextOption option = m_textLayout.textOption();
    option.setWrapMode(isMultiline() ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setAlignment(textAlignment);
    option.setUseDesignMetrics(true);
    m_textLayout.setTextOption(option);

    m_textColor = appearance.getFontColor();
    if (!m_textColor.isValid())
    {
        m_textColor = Qt::black;
    }

    m_maxTextLength = maxTextLength;
    m_widgetRect = rect;
}

void PDFTextEditPseudowidget::performCut()
{
    if (isReadonly())
    {
        return;
    }

    performCopy();
    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performCopy()
{
    if (isTextSelected() && !isPassword())
    {
        QApplication::clipboard()->setText(getSelectedText(), QClipboard::Clipboard);
    }
}

void PDFTextEditPseudowidget::performPaste()
{
    // We always insert text, even if it is empty. Because we want
    // to erase selected text.
    performInsertText(QApplication::clipboard()->text(QClipboard::Clipboard));
}

void PDFTextEditPseudowidget::performClear()
{
    if (isReadonly())
    {
        return;
    }

    performSelectAll();
    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performSelectAll()
{
    m_selectionStart = getPositionStart();
    m_selectionEnd = getPositionEnd();
}

void PDFTextEditPseudowidget::performBackspace()
{
    if (isReadonly())
    {
        return;
    }

    // If we have selection, then delete selected text. If we do not have
    // selection, then we delete previous character.
    if (!isTextSelected())
    {
        setCursorPosition(m_textLayout.previousCursorPosition(m_positionCursor, QTextLayout::SkipCharacters), true);
    }

    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performDelete()
{
    if (isReadonly())
    {
        return;
    }

    // If we have selection, then delete selected text. If we do not have
    // selection, then we delete previous character.
    if (!isTextSelected())
    {
        setCursorPosition(m_textLayout.nextCursorPosition(m_positionCursor, QTextLayout::SkipCharacters), true);
    }

    performRemoveSelectedText();
}

void PDFTextEditPseudowidget::performRemoveSelectedText()
{
    if (isTextSelected())
    {
        m_editText.remove(m_selectionStart, getSelectionLength());
        setCursorPosition(m_selectionStart, false);
        clearSelection();
        updateTextLayout();
    }
}

void PDFTextEditPseudowidget::performInsertText(const QString& text)
{
    if (isReadonly())
    {
        return;
    }

    // Insert text at the cursor
    performRemoveSelectedText();
    m_editText.insert(m_positionCursor, text);
    setCursorPosition(m_positionCursor + text.length(), false);
    updateTextLayout();
}

QMatrix PDFTextEditPseudowidget::createTextBoxTransformMatrix(bool edit) const
{
    QMatrix matrix;

    matrix.translate(m_widgetRect.left(), m_widgetRect.bottom());
    matrix.scale(1.0, -1.0);

    if (edit && !isComb() && m_textLayout.isValidCursorPosition(m_positionCursor))
    {
        // Jakub Melka: we must scroll the control, so cursor is always
        // visible in the widget area. If we are not editing, this not the
        // case, because we always show the text from the beginning.

        const QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
        if (line.isValid())
        {
            const qreal xCursorPosition = line.cursorToX(m_positionCursor);
            if (xCursorPosition >= m_widgetRect.width())
            {
                const qreal delta = xCursorPosition - m_widgetRect.width();
                matrix.translate(-delta, 0.0);
            }

            // Check, if we aren't outside of y position
            const qreal lineSpacing = line.leadingIncluded() ? line.height() : line.leading() + line.height();
            const qreal lineBottom = lineSpacing * (line.lineNumber() + 1);

            if (lineBottom >= m_widgetRect.height())
            {
                const qreal delta = lineBottom - m_widgetRect.height();
                matrix.translate(0.0, -delta);
            }
        }
    }

    if (!isMultiline() && !isComb())
    {
        // If text is single line, then adjust text position to the vertical center
        QTextLine textLine = m_textLayout.lineAt(0);
        if (textLine.isValid())
        {
            const qreal lineSpacing = textLine.leadingIncluded() ? textLine.height() : textLine.leading() + textLine.height();
            const qreal textBoxHeight = m_widgetRect.height();

            if (lineSpacing < textBoxHeight)
            {
                const qreal delta = (textBoxHeight - lineSpacing) * 0.5;
                matrix.translate(0.0, delta);
            }
        }
    }

    return matrix;
}

std::vector<int> PDFTextEditPseudowidget::getCursorPositions() const
{
    std::vector<int> result;
    result.reserve(m_editText.length());
    result.push_back(0);

    int currentPos = 0;
    while (currentPos < m_editText.length())
    {
        currentPos = m_textLayout.nextCursorPosition(currentPos, QTextLayout::SkipCharacters);
        result.push_back(currentPos);
    }

    return result;
}

void PDFTextEditPseudowidget::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    pdf::PDFPainterStateGuard guard(parameters.painter);
    parameters.boundingRectangle = parameters.annotation->getRectangle();

    QPalette palette = QApplication::palette();

    auto getAdjustedColor = [&parameters](QColor color)
    {
        if (parameters.invertColors)
        {
            return invertColor(color);
        }

        return color;
    };

    QPainter* painter = parameters.painter;

    if (edit)
    {
        pdf::PDFPainterStateGuard guard(painter);
        painter->setPen(getAdjustedColor(Qt::black));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(parameters.boundingRectangle);
    }

    painter->setClipRect(parameters.boundingRectangle, Qt::IntersectClip);
    painter->setWorldMatrix(createTextBoxTransformMatrix(edit), true);
    painter->setPen(getAdjustedColor(Qt::black));

    if (isComb())
    {
        const qreal combCount = qMax(m_maxTextLength, 1);
        qreal combWidth = m_widgetRect.width() / combCount;
        QRectF combRect(0.0, 0.0, combWidth, m_widgetRect.height());
        painter->setFont(m_textLayout.font());

        QColor textColor = getAdjustedColor(m_textColor);
        QColor highlightTextColor = getAdjustedColor(palette.color(QPalette::HighlightedText));
        QColor highlightColor = getAdjustedColor(palette.color(QPalette::Highlight));

        std::vector<int> positions = getCursorPositions();
        for (size_t i = 1; i < positions.size(); ++i)
        {
            if (positions[i - 1] >= m_selectionStart && positions[i] - 1 < m_selectionEnd)
            {
                // We are in text selection
                painter->fillRect(combRect, highlightColor);
                painter->setPen(highlightTextColor);
            }
            else
            {
                // We are not in text selection
                painter->setPen(textColor);
            }

            int length = positions[i] - positions[i - 1];
            QString text = m_displayText.mid(positions[i - 1], length);
            painter->drawText(combRect, Qt::AlignCenter, text);

            // Draw the cursor?
            if (edit && m_positionCursor >= positions[i - 1] && m_positionCursor < positions[i])
            {
                QRectF cursorRect(combRect.left(), combRect.bottom() - 1, combRect.width(), 1);
                painter->fillRect(cursorRect, textColor);
            }

            combRect.translate(combWidth, 0.0);
        }

        // Draw the cursor onto next unfilled cell?
        if (edit && m_positionCursor == getPositionEnd())
        {
            QRectF cursorRect(combRect.left(), combRect.bottom() - 1, combRect.width(), 1);
            painter->fillRect(cursorRect, textColor);
        }
    }
    else
    {
        QVector<QTextLayout::FormatRange> selections;

        QTextLayout::FormatRange defaultFormat;
        defaultFormat.start = getPositionStart();
        defaultFormat.length = getTextLength();
        defaultFormat.format.clearBackground();
        defaultFormat.format.setForeground(QBrush(getAdjustedColor(m_textColor), Qt::SolidPattern));

        // If we are editing, draw selections
        if (edit && isTextSelected())
        {
            QTextLayout::FormatRange before = defaultFormat;
            QTextLayout::FormatRange after = defaultFormat;

            before.start = getPositionStart();
            before.length = m_selectionStart;
            after.start = m_selectionEnd;
            after.length = getTextLength() - m_selectionEnd;

            QTextLayout::FormatRange selectedFormat = defaultFormat;
            selectedFormat.start = m_selectionStart;
            selectedFormat.length = getSelectionLength();
            selectedFormat.format.setForeground(QBrush(getAdjustedColor(palette.color(QPalette::HighlightedText)), Qt::SolidPattern));
            selectedFormat.format.setBackground(QBrush(getAdjustedColor(palette.color(QPalette::Highlight)), Qt::SolidPattern));

            selections = { before, selectedFormat, after};
        }
        else
        {
            selections.push_back(defaultFormat);
        }

        // Draw text
        m_textLayout.draw(painter, QPointF(0.0, 0.0), selections, QRectF());

        // If we are editing, also draw text
        if (edit && !isReadonly())
        {
            m_textLayout.drawCursor(painter, QPointF(0.0, 0.0), m_positionCursor);
        }
    }
}

int PDFTextEditPseudowidget::getCursorPositionFromWidgetPosition(const QPointF& point, bool edit) const
{
    QMatrix textBoxSpaceToPageSpace = createTextBoxTransformMatrix(edit);
    QMatrix pageSpaceToTextBoxSpace = textBoxSpaceToPageSpace.inverted();

    QPointF textBoxPoint = pageSpaceToTextBoxSpace.map(point);

    if (isComb())
    {
        // If it is comb, then characters are spaced equidistantly
        const qreal x = qBound(0.0, textBoxPoint.x(), m_widgetRect.width());
        const size_t position = qFloor(x * qreal(m_maxTextLength) / qreal(m_widgetRect.width()));
        std::vector<int> positions = getCursorPositions();
        if (position < positions.size())
        {
            return positions[position];
        }

        return positions.back();
    }
    else if (m_textLayout.lineCount() > 0)
    {
        QTextLine line;
        qreal yPos = 0.0;

        // Find line under cursor
        for (int i = 0; i < m_textLayout.lineCount(); ++i)
        {
            QTextLine currentLine = m_textLayout.lineAt(i);
            const qreal lineSpacing = currentLine.leadingIncluded() ? currentLine.height() : currentLine.leading() + currentLine.height();
            const qreal yNextPos = yPos + lineSpacing;

            if (textBoxPoint.y() >= yPos && textBoxPoint.y() < yNextPos)
            {
                line = currentLine;
                break;
            }

            yPos = yNextPos;
        }

        // If no line is found, select last line
        if (!line.isValid())
        {
            if (textBoxPoint.y() < 0.0)
            {
                line = m_textLayout.lineAt(0);
            }
            else
            {
                line = m_textLayout.lineAt(m_textLayout.lineCount() - 1);
            }
        }

        return line.xToCursor(textBoxPoint.x(), QTextLine::CursorBetweenCharacters);
    }

    return 0;
}

void PDFTextEditPseudowidget::updateTextLayout()
{
    // Prepare display text
    if (isPassword())
    {
        m_displayText.resize(m_editText.length(), m_passwordReplacementCharacter);
    }
    else
    {
        m_displayText = m_editText;
    }

    // Perform text layout
    m_textLayout.clearLayout();
    m_textLayout.setText(m_displayText);
    m_textLayout.beginLayout();

    QPointF textLinePosition(0.0, 0.0);

    while (true)
    {
        QTextLine textLine = m_textLayout.createLine();
        if (!textLine.isValid())
        {
            // We are finished with layout
            break;
        }

        textLinePosition.ry() += textLine.leading();
        textLine.setLineWidth(m_widgetRect.width());
        textLine.setPosition(textLinePosition);
        textLinePosition.ry() += textLine.height();
    }
    m_textLayout.endLayout();

    // Check length
    if (m_maxTextLength > 0)
    {
        int length = 0;
        int currentPos = 0;

        while (currentPos < m_editText.length())
        {
            currentPos = m_textLayout.nextCursorPosition(currentPos, QTextLayout::SkipCharacters);
            ++length;

            if (length == m_maxTextLength)
            {
                break;
            }
        }

        if (currentPos < m_editText.length())
        {
            m_editText = m_editText.left(currentPos);
            m_positionCursor = qBound(getPositionStart(), m_positionCursor, getPositionEnd());
            updateTextLayout();
        }
    }
}

int PDFTextEditPseudowidget::getSingleStepForward() const
{
    // If direction is right-to-left, then move backward (because
    // text is painted from right to left.
    return (m_textLayout.textOption().textDirection() == Qt::RightToLeft) ? -1 : 1;
}

int PDFTextEditPseudowidget::getNextPrevCursorPosition(int referencePosition, int steps, QTextLayout::CursorMode mode) const
{
    int cursor = referencePosition;

    if (steps > 0)
    {
        for (int i = 0; i < steps; ++i)
        {
            cursor = m_textLayout.nextCursorPosition(cursor, mode);
        }
    }
    else if (steps < 0)
    {
        for (int i = 0; i < -steps; ++i)
        {
            cursor = m_textLayout.previousCursorPosition(cursor, mode);
        }
    }

    return cursor;
}

int PDFTextEditPseudowidget::getCurrentLineTextStart() const
{
    return m_textLayout.lineForTextPosition(m_positionCursor).textStart();
}

int PDFTextEditPseudowidget::getCurrentLineTextEnd() const
{
    QTextLine textLine = m_textLayout.lineForTextPosition(m_positionCursor);
    return textLine.textStart() + textLine.textLength();
}

int PDFTextEditPseudowidget::getCursorLineUp() const
{
    QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
    const int lineIndex = line.lineNumber() - 1;

    if (lineIndex >= 0)
    {
        QTextLine upLine = m_textLayout.lineAt(lineIndex);
        return upLine.xToCursor(line.cursorToX(m_positionCursor), QTextLine::CursorBetweenCharacters);
    }

    return m_positionCursor;
}

int PDFTextEditPseudowidget::getCursorLineDown() const
{
    QTextLine line = m_textLayout.lineForTextPosition(m_positionCursor);
    const int lineIndex = line.lineNumber() + 1;

    if (lineIndex < m_textLayout.lineCount())
    {
        QTextLine downLine = m_textLayout.lineAt(lineIndex);
        return downLine.xToCursor(line.cursorToX(m_positionCursor), QTextLine::CursorBetweenCharacters);
    }

    return m_positionCursor;
}

bool PDFFormFieldText::setValue(const SetValueParameters& parameters)
{
    // If form field is readonly, and scope is user (form field is changed by user,
    // not by calculated value), then we must not allow value change.
    if (getFlags().testFlag(ReadOnly) && parameters.scope == SetValueParameters::Scope::User)
    {
        return false;
    }

    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), parameters.value);
    m_value = parameters.value;

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }

    return true;
}

void PDFFormFieldText::resetValue(const ResetValueParameters& parameters)
{
    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFObject defaultValue = getDefaultValue();
    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), defaultValue);
    m_value = defaultValue;

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }
}

PDFListBoxPseudowidget::PDFListBoxPseudowidget(PDFFormField::FieldFlags flags) :
    m_flags(flags),
    m_topIndex(0),
    m_currentIndex(0)
{

}

void PDFListBoxPseudowidget::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    if (event == QKeySequence::Copy)
    {
        event->accept();
        return;
    }

    if (event == QKeySequence::SelectAll)
    {
        // Select All can be processed only, if multiselection is allowed
        if (isMultiSelect())
        {
            event->accept();
        }
        return;
    }

    switch (event->key())
    {
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            event->accept();
            break;

        default:
            break;
    }
}

void PDFListBoxPseudowidget::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    event->accept();

    if (event == QKeySequence::Copy)
    {
        // Copy the item text
        if (m_currentIndex >= 0 && m_currentIndex < m_options.size())
        {
            QApplication::clipboard()->setText(m_options[m_currentIndex].userString, QClipboard::Clipboard);
        }
        return;
    }

    if (event == QKeySequence::SelectAll && isMultiSelect())
    {
        std::set<int> selection;
        for (int i = 0; i < m_options.size(); ++i)
        {
            selection.insert(i);
        }
        setSelection(qMove(selection), false);
        return;
    }

    switch (event->key())
    {
        case Qt::Key_Home:
            setCurrentItem(getStartItemIndex(), event->modifiers());
            break;

        case Qt::Key_End:
            setCurrentItem(getEndItemIndex(), event->modifiers());
            break;

        case Qt::Key_Up:
            moveCurrentItemIndexByOffset(-getSingleStep(), event->modifiers());
            break;

        case Qt::Key_Down:
            moveCurrentItemIndexByOffset(getSingleStep(), event->modifiers());
            break;

        case Qt::Key_PageUp:
            moveCurrentItemIndexByOffset(-getPageStep(), event->modifiers());
            break;

        case Qt::Key_PageDown:
            moveCurrentItemIndexByOffset(getPageStep(), event->modifiers());
            break;

        default:
            event->ignore();
            break;
    }
}

void PDFListBoxPseudowidget::initialize(QFont font, QColor fontColor, Qt::Alignment textAlignment, QRectF rect,
                                        const Options& options, int topIndex, std::set<int> selection)
{
    m_font = font;

    QFontMetricsF fontMetrics(m_font);
    m_lineSpacing = fontMetrics.lineSpacing();

    m_textColor = fontColor;
    if (!m_textColor.isValid())
    {
        m_textColor = Qt::black;
    }

    m_textAlignment = textAlignment;
    m_widgetRect = rect;
    m_options = options;
    m_topIndex = getValidIndex(topIndex);
    m_selection = qMove(selection);
    m_currentIndex = m_topIndex;
}

void PDFListBoxPseudowidget::setAppearance(const PDFAnnotationDefaultAppearance& appearance,
                                           Qt::Alignment textAlignment,
                                           QRectF rect,
                                           const PDFListBoxPseudowidget::Options& options,
                                           int topIndex,
                                           std::set<int> selection)
{
    // Set appearance
    qreal fontSize = appearance.getFontSize();
    if (qFuzzyIsNull(fontSize))
    {
        fontSize = qMax(rect.height() / qMax<qreal>(options.size(), 1), qreal(12.0));
    }

    QColor fontColor = appearance.getFontColor();

    QFont font(appearance.getFontName());
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPixelSize(qCeil(fontSize));
    font.setStyleStrategy(QFont::ForceOutline);

    initialize(font, fontColor, textAlignment, rect, options, topIndex, qMove(selection));
}

QMatrix PDFListBoxPseudowidget::createListBoxTransformMatrix() const
{
    QMatrix matrix;
    matrix.translate(m_widgetRect.left(), m_widgetRect.bottom());
    matrix.scale(1.0, -1.0);
    return matrix;
}

void PDFListBoxPseudowidget::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    pdf::PDFPainterStateGuard guard(parameters.painter);

    if (!parameters.boundingRectangle.isValid())
    {
        parameters.boundingRectangle = parameters.annotation->getRectangle();
    }

    QPalette palette = QApplication::palette();

    auto getAdjustedColor = [&parameters](QColor color)
    {
        if (parameters.invertColors)
        {
            return invertColor(color);
        }

        return color;
    };

    QMatrix matrix = createListBoxTransformMatrix();

    QPainter* painter = parameters.painter;

    if (edit)
    {
        pdf::PDFPainterStateGuard guard(painter);
        painter->setPen(getAdjustedColor(Qt::black));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(parameters.boundingRectangle);
    }

    painter->setClipRect(parameters.boundingRectangle, Qt::IntersectClip);
    painter->setWorldMatrix(matrix, true);
    painter->setPen(getAdjustedColor(m_textColor));
    painter->setFont(m_font);

    QColor textColor = getAdjustedColor(m_textColor);
    QColor highlightTextColor = getAdjustedColor(palette.color(QPalette::HighlightedText));
    QColor highlightColor = getAdjustedColor(palette.color(QPalette::Highlight));

    QRectF rect(0, 0, m_widgetRect.width(), m_lineSpacing);
    for (int i = m_topIndex; i < m_options.size(); ++i)
    {
        if (m_selection.count(i))
        {
            painter->fillRect(rect, highlightColor);
            painter->setPen(highlightTextColor);
        }
        else
        {
            painter->setPen(textColor);
        }

        painter->drawText(rect, m_textAlignment | Qt::TextSingleLine, m_options[i].userString);

        if (edit && m_currentIndex == i)
        {
            pdf::PDFPainterStateGuard guard(painter);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(Qt::DotLine);
            painter->drawRect(rect);
        }

        rect.translate(0, m_lineSpacing);
    }
}

void PDFListBoxPseudowidget::setSelection(std::set<int> selection, bool force)
{
    if (isReadonly() && !force)
    {
        // Field is readonly
        return;
    }

    // Jakub Melka: Here should be also commit, when flag CommitOnSelectionChange is set,
    // but we do it only, when widget loses focus (so no need to update appearance often).
    // I hope it will be OK.

    m_selection = qMove(selection);
}

void PDFListBoxPseudowidget::setTopItemIndex(int index)
{
    m_topIndex = getValidIndex(index);
}

bool PDFListBoxPseudowidget::isVisible(int index) const
{
    return index >= m_topIndex && index < m_topIndex + getViewportRowCount();
}

void PDFListBoxPseudowidget::scrollTo(int index)
{
    while (!isVisible(index))
    {
        if (index < m_topIndex)
        {
            --m_topIndex;
        }
        else
        {
            ++m_topIndex;
        }
    }
}

void PDFListBoxPseudowidget::setCurrentItem(int index, Qt::KeyboardModifiers modifiers)
{
    index = getValidIndex(index);

    if (m_currentIndex == index)
    {
        return;
    }

    std::set<int> newSelection;
    if (!isMultiSelect() || !modifiers.testFlag(Qt::ShiftModifier))
    {
        newSelection = { index };
    }
    else
    {
        int indexFrom = index;
        int indexTo = index;

        if (hasContinuousSelection())
        {
            indexFrom = qMin(index, *m_selection.begin());
            indexTo = qMax(index, *m_selection.rbegin());
        }
        else
        {
            indexFrom = qMin(index, m_currentIndex);
            indexTo = qMax(index, m_currentIndex);
        }

        for (int i = indexFrom; i <= indexTo; ++i)
        {
            newSelection.insert(i);
        }
    }

    m_currentIndex = index;
    setSelection(qMove(newSelection), false);
    scrollTo(m_currentIndex);
}

QString PDFListBoxPseudowidget::getSelectedItemText() const
{
    if (m_selection.size() == 1)
    {
        const int selectedIndex = *m_selection.begin();
        return m_options[selectedIndex].userString;
    }

    return QString();
}

int PDFListBoxPseudowidget::findOption(const QString& option) const
{
    auto it = std::find_if(m_options.cbegin(), m_options.cend(), [&option](const auto& currentOption) { return currentOption.userString == option; });
    if (it != m_options.cend())
    {
        return static_cast<int>(std::distance(m_options.cbegin(), it));
    }

    return -1;
}

bool PDFListBoxPseudowidget::hasContinuousSelection() const
{
    if (m_selection.empty())
    {
        return false;
    }

    return *m_selection.rbegin() - *m_selection.begin() + 1 == m_selection.size();
}

int PDFListBoxPseudowidget::getValidIndex(int index) const
{
    return qBound(getStartItemIndex(), index, getEndItemIndex());
}

int PDFListBoxPseudowidget::getIndexFromWidgetPosition(const QPointF& point) const
{
    QMatrix widgetToPageMatrix = createListBoxTransformMatrix();
    QMatrix pageToWidgetMatrix = widgetToPageMatrix.inverted();

    QPointF widgetPoint = pageToWidgetMatrix.map(point);
    const qreal y = widgetPoint.y();
    const int visualIndex = qFloor(y / m_lineSpacing);
    return m_topIndex + visualIndex;
}

PDFFormFieldListBoxEditor::PDFFormFieldListBoxEditor(PDFFormManager* formManager, PDFFormWidget formWidget) :
    BaseClass(formManager, formWidget),
    m_listBox(formWidget.getParent()->getFlags())
{
    initializeListBox(&m_listBox);
}

void PDFFormFieldListBoxEditor::shortcutOverrideEvent(QWidget* widget, QKeyEvent* event)
{
    Q_UNUSED(widget);

    m_listBox.shortcutOverrideEvent(widget, event);
}

void PDFFormFieldListBoxEditor::keyPressEvent(QWidget* widget, QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return))
    {
        // Commit the editor
        m_formManager->setFocusToEditor(nullptr);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        // Cancel the editor
        reloadValue();
        m_formManager->setFocusToEditor(nullptr);
        event->accept();
        return;
    }

    m_listBox.keyPressEvent(widget, event);

    if (event->isAccepted())
    {
        widget->update();
    }
}

void PDFFormFieldListBoxEditor::mousePressEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    if (event->button() == Qt::LeftButton && m_hasFocus)
    {
        const int index = m_listBox.getIndexFromWidgetPosition(mousePagePosition);

        if (event->modifiers() & Qt::ControlModifier)
        {
            std::set<int> selection = m_listBox.getSelection();
            if (selection.count(index))
            {
                selection.erase(index);
            }
            else
            {
                selection.insert(index);
            }
            m_listBox.setSelection(qMove(selection), false);
        }
        else
        {
            m_listBox.setCurrentItem(index, event->modifiers());
        }

        event->accept();
        widget->update();
    }
}

void PDFFormFieldListBoxEditor::mouseMoveEvent(QWidget* widget, QMouseEvent* event, const QPointF& mousePagePosition)
{
    if (event->buttons() & Qt::LeftButton)
    {
        const int index = m_listBox.getIndexFromWidgetPosition(mousePagePosition);

        if (!(event->modifiers() & Qt::ControlModifier))
        {
            m_listBox.setCurrentItem(index, event->modifiers());

            event->accept();
            widget->update();
        }
    }
}

void PDFFormFieldListBoxEditor::wheelEvent(QWidget* widget, QWheelEvent* event, const QPointF& mousePagePosition)
{
    Q_UNUSED(mousePagePosition);

    if (m_hasFocus)
    {
        if (event->angleDelta().y() < 0)
        {
            m_listBox.scrollTo(m_listBox.getValidIndex(m_listBox.getTopItemIndex() + m_listBox.getViewportRowCount()));
        }
        else
        {
            m_listBox.scrollTo(m_listBox.getValidIndex(m_listBox.getTopItemIndex() - 1));
        }

        widget->update();
        event->accept();
    }
}

void PDFFormFieldListBoxEditor::reloadValue()
{
    const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    m_listBox.setTopItemIndex(parentField->getTopIndex());
    m_listBox.setSelection(getSelectedItems(parentField->getValue(), parentField->getSelection()), true);
}

void PDFFormFieldListBoxEditor::draw(AnnotationDrawParameters& parameters, bool edit) const
{
    if (edit)
    {
        m_listBox.draw(parameters, true);
    }
    else
    {
        // Draw static contents
        PDFListBoxPseudowidget pseudowidget(m_formWidget.getParent()->getFlags());
        initializeListBox(&pseudowidget);
        pseudowidget.draw(parameters, false);
    }
}

void PDFFormFieldListBoxEditor::initializeListBox(PDFListBoxPseudowidget* listBox) const
{
    const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
    Q_ASSERT(parentField);

    listBox->setAppearance(PDFAnnotationDefaultAppearance::parse(m_formManager->getForm()->getDefaultAppearance().value_or(QByteArray())),
                           m_formManager->getForm()->getDefaultAlignment(),
                           m_formManager->getWidgetRectangle(m_formWidget),
                           parentField->getOptions(),
                           parentField->getTopIndex(),
                           getSelectedItems(parentField->getValue(), parentField->getSelection()));
}

void PDFFormFieldListBoxEditor::setFocusImpl(bool focused)
{
    if (!focused && !m_listBox.isReadonly() && !m_formManager->isCommitDisabled())
    {
        commit();
    }
}

std::set<int> PDFFormFieldListBoxEditor::getSelectedItems(PDFObject value, PDFObject indices) const
{
    std::set<int> result;

    PDFDocumentDataLoaderDecorator loader(m_formManager->getDocument());
    value = m_formManager->getDocument()->getObject(value);

    std::vector<PDFInteger> indicesVector = loader.readIntegerArray(indices);
    if (indicesVector.empty())
    {
        const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
        Q_ASSERT(parentField);

        const PDFFormFieldChoice::Options& options = parentField->getOptions();
        auto addOption = [&options, &result](const QString& optionString)
        {
            for (size_t i = 0; i < options.size(); ++i)
            {
                const PDFFormFieldChoice::Option& option = options[i];
                if (option.userString == optionString)
                {
                    result.insert(int(i));
                }
            }
        };

        if (value.isString())
        {
            addOption(loader.readTextString(value, QString()));
        }
        else if (value.isArray())
        {
            for (const QString& string : loader.readTextStringList(value))
            {
                addOption(string);
            }
        }
    }
    else
    {
        std::sort(indicesVector.begin(), indicesVector.end());
        std::copy(indicesVector.cbegin(), indicesVector.cend(), std::inserter(result, result.cend()));
    }

    return result;
}

void PDFFormFieldListBoxEditor::commit()
{
    PDFObject object;

    const std::set<int>& selection = m_listBox.getSelection();
    if (!selection.empty())
    {
        const PDFFormFieldChoice* parentField = dynamic_cast<const PDFFormFieldChoice*>(m_formWidget.getParent());
        Q_ASSERT(parentField);

        const PDFFormFieldChoice::Options& options = parentField->getOptions();
        std::set<QString> values;

        for (const int index : selection)
        {
            values.insert(options[index].userString);
        }

        if (values.size() == 1)
        {
            object = PDFObjectFactory::createTextString(*values.begin());
        }
        else
        {
            PDFObjectFactory objectFactory;
            objectFactory.beginArray();

            for (const QString& string : values)
            {
                objectFactory << string;
            }

            objectFactory.endArray();
            object = objectFactory.takeObject();
        }
    }

    if (object != m_formWidget.getParent()->getValue())
    {
        PDFFormField::SetValueParameters parameters;
        parameters.formManager = m_formManager;
        parameters.invokingWidget = m_formWidget.getWidget();
        parameters.invokingFormField = m_formWidget.getParent();
        parameters.scope = PDFFormField::SetValueParameters::Scope::User;
        parameters.value = qMove(object);
        parameters.listboxTopIndex = m_listBox.getTopItemIndex();
        std::copy(selection.cbegin(), selection.cend(), std::back_inserter(parameters.listboxChoices));
        m_formManager->setFormFieldValue(parameters);
    }
}

bool PDFFormFieldChoice::setValue(const SetValueParameters& parameters)
{
    // If form field is readonly, and scope is user (form field is changed by user,
    // not by calculated value), then we must not allow value change.
    if (getFlags().testFlag(ReadOnly) && parameters.scope == SetValueParameters::Scope::User)
    {
        return false;
    }

    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), parameters.value);

    if (isListBox())
    {
        // Listbox has special values, which must be set
        builder->setFormFieldChoiceTopIndex(getSelfReference(), parameters.listboxTopIndex);
        builder->setFormFieldChoiceIndices(getSelfReference(), parameters.listboxChoices);
    }

    m_value = parameters.value;
    m_topIndex = parameters.listboxTopIndex;

    PDFObjectFactory objectFactory;
    objectFactory << parameters.listboxChoices;
    m_selection = objectFactory.takeObject();

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }

    return true;
}

void PDFFormFieldChoice::resetValue(const PDFFormField::ResetValueParameters& parameters)
{
    Q_ASSERT(parameters.formManager);
    Q_ASSERT(parameters.modifier);

    PDFObject defaultValue = getDefaultValue();
    PDFDocumentBuilder* builder = parameters.modifier->getBuilder();
    parameters.modifier->markFormFieldChanged();
    builder->setFormFieldValue(getSelfReference(), defaultValue);
    m_value = defaultValue;
    m_selection = PDFObject();

    if (isListBox())
    {
        // Listbox has special values, which must be set
        builder->setFormFieldChoiceTopIndex(getSelfReference(), 0);
        builder->setFormFieldChoiceIndices(getSelfReference(), { });
    }

    // Change widget appearance states
    for (const PDFFormWidget& formWidget : getWidgets())
    {
        builder->updateAnnotationAppearanceStreams(formWidget.getWidget());
        parameters.modifier->markAnnotationsChanged();
    }
}

void PDFFormFieldChoice::reloadValue(const PDFObjectStorage* storage, PDFObject parentValue)
{
    BaseClass::reloadValue(storage, parentValue);

    if (const PDFDictionary* fieldDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(getSelfReference())))
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        m_topIndex = loader.readIntegerFromDictionary(fieldDictionary, "TI", 0);
        m_selection = fieldDictionary->get("I");
    }
}

}   // namespace pdf
