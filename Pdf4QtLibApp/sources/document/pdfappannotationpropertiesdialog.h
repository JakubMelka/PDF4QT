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

#ifndef PDFAPPANNOTATIONPROPERTIESDIALOG_H
#define PDFAPPANNOTATIONPROPERTIESDIALOG_H

#include "pdfappglobal.h"

#include "pdfannotationproperties.h"
#include "pdfmessagebox.h"
#include "pdfobject.h"

#include <QPointer>
#include <QWidget>

#include <map>
#include <optional>
#include <vector>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QHBoxLayout;
class QScrollArea;
class QSlider;
class QVBoxLayout;

namespace pdf
{

class PDFAppAnnotationController;
class PDFColorSwatchRow;
class PDFComboBox;
class PDFDoubleSpinBox;
class PDFLabel;
class PDFLineEdit;
class PDFPlainTextEdit;
class PDFSegmentedControl;
class PDFSpinBox;

/// Properties dialog of an annotation (Figma 1022:20233, 600 x 675, variants 1027:21823,
/// the link 1171:28262): Comment (author, subject, text) - Description of a link,
/// Destination of a link, Appearance (color, fill, opacity, border and the parts of
/// the type), Behavior (the flags). A dialog of the design on PDFMessageBox: modal
/// over the scrim of the body of the window; Apply = one undo step through the
/// controller (only the changed properties are written - PDFAnnotationPropertyEditor).
///
/// Read only (O20, ISO 32000-2 Table 167): a locked document or a document without
/// the permission - everything read only, the dialog only shows the properties (one
/// button Close); a Locked annotation - only its text (Contents) and the lock flags
/// can be changed; LockedContents - the text is read only. The dialog deletes itself,
/// when it is closed; it closes, when its annotation disappears.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationPropertiesDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Geometry of 1022:20735
    static constexpr int CARD_WIDTH = 600;
    static constexpr int LABEL_WIDTH = 104;
    static constexpr int ROW_GAP = 12;
    static constexpr int SECTION_GAP = 14;
    static constexpr int ITEM_GAP = 10;
    static constexpr int TEXT_HEIGHT = 64;
    static constexpr int MULTILINE_LABEL_OFFSET = 10;

    explicit PDFAppAnnotationPropertiesDialog(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent);
    virtual ~PDFAppAnnotationPropertiesDialog() override;

    /// Opens the modal dialog over \p parent (the window of the view of the
    /// controller), the dialog deletes itself, when it is closed. \returns the
    /// dialog, or nullptr, when it cannot be opened (no annotation, no document).
    static PDFAppAnnotationPropertiesDialog* open(PDFAppAnnotationController* controller, PDFObjectReference annotation, QWidget* parent);

    PDFAppAnnotationController* getController() const;
    PDFObjectReference getAnnotation() const { return m_annotation; }

    /// The dialog only shows the properties (a locked document, no permission)
    bool isReadOnly() const { return m_isReadOnly; }

    /// Properties of the annotation, when the dialog was opened
    const PDFAnnotationProperties& getOriginalProperties() const { return m_original; }

    /// Properties of the controls
    PDFAnnotationProperties getEditedProperties() const;

    /// Fields, which the dialog may change (the type, the lock, the flags)
    PDFAnnotationProperties::Fields getEditableFields() const;

    /// Fields changed by the user, which are written by Apply
    PDFAnnotationProperties::Fields getChangedFields() const;

    /// Apply: writes the changed properties as one undo step and closes the dialog.
    /// \returns false, when the change is refused (the dialog stays open)
    bool apply();

    /// Name and description of a destination type of a link (readable names, O20)
    static QString getDestinationTypeName(DestinationType type);
    static QString getDestinationTypeDescription(DestinationType type);

    /// Name of a line ending and of a border style
    static QString getLineEndingName(AnnotationLineEnding lineEnding);
    static QString getBorderStyleName(AnnotationBorderStyle style);

    /// The scrolled body of a dialog of the design takes the height of its content,
    /// at most the height, which fits into the window (the minimal window scrolls)
    static void fitScrollArea(PDFMessageBox* box, QScrollArea* scrollArea);

    /// Puts the widget (the hint of the actions 1022:20848) at the start of the own
    /// row of the buttons of the message box (the row of addActionButton)
    static bool insertIntoActionRow(PDFMessageBox* box, QWidget* widget);

Q_SIGNALS:
    /// The dialog has been closed (\p isApplied = the change has been committed)
    void dialogFinished(bool isApplied);

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void createContent();
    QVBoxLayout* createSection(QVBoxLayout* parent, const QString& caption);
    QHBoxLayout* createRow(QVBoxLayout* section, const QString& label, bool alignTop = false);
    PDFLabel* createHint(const QString& text);
    PDFColorSwatchRow* createSwatchRow(const QString& objectName, bool hasNoColor, bool hasWhite, const QColor& color);
    PDFComboBox* createLineEndingComboBox(const QString& objectName, AnnotationLineEnding lineEnding);
    void createCommentSection(QVBoxLayout* layout);
    void createDestinationSection(QVBoxLayout* layout);
    void createAppearanceSection(QVBoxLayout* layout);
    void createBehaviorSection(QVBoxLayout* layout);
    void createActions();
    void updateActions();
    void retranslate();
    void updateEnabledState();
    void updateDestinationTexts();
    void updateApplyButton();
    std::optional<PDFInteger> getDestinationPageIndex() const;
    PDFDestination getEditedDestination() const;

    void onApplyClicked();
    void onCancelClicked();
    void onDocumentChanged();
    void onEditingAllowedChanged();
    void onOpacitySliderChanged(int value);
    void onOpacitySpinBoxChanged(int value);
    void onDestinationChanged();
    void onGoToTargetClicked();
    void onSetToCurrentViewClicked();
    void onDialogFinished(int result);
    void onControllerDestroyed();

    QPointer<PDFAppAnnotationController> m_controller;
    PDFObjectReference m_annotation;
    PDFAnnotationProperties m_original;
    PDFAnnotationProperties m_shown;         ///< Values of the controls, when they are created (the values read; after a language change the edited ones)
    PDFDestination m_resolvedDestination;   ///< Destination of the link (a named one resolved)
    PDFDestination m_viewDestination;       ///< Set to current view (when the user has taken it)
    PDFInteger m_pageIndex = -1;
    bool m_isReadOnly = false;
    bool m_isLocked = false;
    bool m_isContentsLocked = false;
    bool m_isMeasurement = false;
    bool m_isApplied = false;
    bool m_isDestinationChanged = false;

    QPointer<PDFLineEdit> m_authorField;
    QPointer<PDFLineEdit> m_subjectField;
    QPointer<PDFPlainTextEdit> m_textEdit;
    QPointer<PDFLineEdit> m_pageField;
    QPointer<PDFComboBox> m_destinationTypeComboBox;
    QPointer<PDFLabel> m_destinationDescription;
    QPointer<PDFLabel> m_positionLabel;
    QPointer<QCheckBox> m_keepZoomCheckBox;
    QPointer<QAbstractButton> m_goToTargetButton;
    QPointer<QAbstractButton> m_setToCurrentViewButton;
    QPointer<PDFColorSwatchRow> m_colorRow;
    QPointer<PDFColorSwatchRow> m_fillRow;
    QPointer<QSlider> m_opacitySlider;
    QPointer<PDFSpinBox> m_opacitySpinBox;
    QPointer<PDFDoubleSpinBox> m_borderWidthSpinBox;
    QPointer<PDFComboBox> m_borderStyleComboBox;
    QPointer<PDFComboBox> m_lineStartComboBox;
    QPointer<PDFComboBox> m_lineEndComboBox;
    QPointer<QButtonGroup> m_iconGroup;
    QPointer<PDFComboBox> m_iconComboBox;
    QPointer<QCheckBox> m_openCheckBox;
    QPointer<PDFComboBox> m_fontComboBox;
    QPointer<PDFDoubleSpinBox> m_fontSizeSpinBox;
    QPointer<PDFColorSwatchRow> m_textColorRow;
    QPointer<PDFSegmentedControl> m_alignmentControl;
    QPointer<PDFComboBox> m_calloutEndComboBox;
    QPointer<PDFDoubleSpinBox> m_leaderLineSpinBox;
    QPointer<PDFDoubleSpinBox> m_leaderExtensionSpinBox;
    QPointer<PDFSegmentedControl> m_captionControl;
    QPointer<PDFComboBox> m_highlightComboBox;
    std::map<PDFAnnotation::Flag, QPointer<QCheckBox>> m_flagCheckBoxes;
    std::vector<QByteArray> m_iconNames;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QPushButton> m_applyButton;
    QPointer<QPushButton> m_cancelButton;
    QPointer<PDFLabel> m_actionsHint;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONPROPERTIESDIALOG_H
