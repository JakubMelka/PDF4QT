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

#ifndef PDFAPPTOOLPASSWORDCARD_H
#define PDFAPPTOOLPASSWORDCARD_H

#include "pdfappglobal.h"
#include "pdfpasswordstrength.h"
#include "pdfsecurityhandler.h"

#include <QPointer>
#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace pdf
{

class PDFAppParagraph;
class PDFAppToolCallout;
class PDFLabel;
class PDFLineEdit;
class PDFStrengthMeter;

/// A card of a password of the tools of step 5 of phase 2 (plan 4.2, 6.1, 6.2; package P2 implements
/// it, P4 uses it in the confirmation of MCP): the field (PDFLineEdit::setPasswordField), the
/// Strength meter (PDFStrengthMeter with PDFPasswordStrength), Repeat password, the errors (430:1999
/// characters of RC4 / AES-128, 447:15766 the passwords do not match, 432:3721 the same password as
/// the open one) and the hint of the effective length ("Only the first 32 characters are used with
/// AES 128-bit."). Figma 430:1068 (Open password), 430:2228 (Owner password), 433:3049 (Certificate
/// password: the field only, "It is not stored.").
///
/// The password lives only in the fields (S5.1): never in the parameter model, the settings, a log,
/// a tooltip, an accessible text or the clipboard; getPassword gives it to the run, clear empties the
/// fields and their undo history (after the run, Cancel, the close of the tool, a change of the
/// preset). The card is a child of its panel; the GUI holds it by QPointer.
///
/// The card is the content of a card of the design, not its frame: the parts below each other,
/// GAP apart (10 in the canvas 430:1068, 8 in the left panel 432:2105, setGap) - the label and its
/// field 4 apart, an error 4 below its field (Regular 11 text/danger, 447:15766). The kind
/// Certificate (433:3221, the split with package P3 of 14. 11.) is the field and the line below it
/// only: the hint "Used once to read the certificate file. It is not stored." or the error of
/// setErrorText. The parts are children of the card (deleted with it); the card emits only
/// passwordChanged, as the last call of a change of a field.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPasswordCard : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// What the card asks for (its texts and parts)
    enum class Kind
    {
        Open,           ///< Open password: the field, the meter, Repeat password
        Owner,          ///< Owner password: the field, the meter, Repeat password
        Certificate     ///< The password of a .pfx file: the field only
    };

    /// Gap of the parts in the canvas (430:1198) and in a side panel (432:2235)
    static constexpr int CANVAS_GAP = 10;
    static constexpr int PANEL_GAP = 8;

    /// Gap of a label and its field, of a field and its error (430:1213, 447:15766)
    static constexpr int FIELD_GAP = 4;

    /// Height of the field (Password field 430:1200)
    static constexpr int FIELD_HEIGHT = 40;

    /// Longest password of the field (the limit of a Secret of the tools, PDFDocumentToolParameterDefinition)
    static constexpr int MAXIMUM_LENGTH = 1024;

    explicit PDFAppToolPasswordCard(Kind kind, QWidget* parent);
    virtual ~PDFAppToolPasswordCard() override;

    Kind getKind() const { return m_kind; }

    /// The gap of the parts (CANVAS_GAP by default)
    void setGap(int gap);

    /// The labels of the field and of its repetition ("Password" / "Repeat password" by default;
    /// "Owner password" / "Repeat owner password" of 430:2228)
    void setLabels(const QString& passwordLabel, const QString& repeatLabel);

    /// The accessible names of the field and of its repetition, when the shown labels do not say
    /// which password it is (432:2235: "Password" in the sections Owner password and Open
    /// password); empty = the labels. A screen reader tells the two passwords apart by them.
    void setAccessibleLabels(const QString& passwordName, const QString& repeatName);

    /// The algorithm of the encryption: the allowed characters (PDFDocEncoding of revision 4), the
    /// effective length and the meter (PDFPasswordStrength::evaluate)
    void setAlgorithm(PDFSecurityHandlerFactory::Algorithm algorithm);
    PDFSecurityHandlerFactory::Algorithm getAlgorithm() const { return m_algorithm; }

    /// An error of the validation of the tool shown at the field (432:3721; empty = none); the
    /// text never contains the password
    void setErrorText(const QString& text);

    /// The shown error of the field: the error of setErrorText, else the characters, which the
    /// algorithm cannot use (430:1999); empty = none
    QString getErrorText() const;

    /// The shown error of the repetition ("The passwords do not match.", 447:15766), empty = none
    QString getRepeatErrorText() const;

    /// The password has characters, which the algorithm cannot use (PDFDocEncoding of revision 4, 430:1999)
    bool hasInvalidCharacters() const;

    /// The repetition is typed and differs (447:15766)
    bool isMismatch() const;

    /// The estimate of the meter (Empty for an empty field)
    const PDFPasswordStrength::Result& getStrength() const { return m_strength; }

    /// The field is empty
    bool isEmpty() const;

    /// The password is typed, repeated (Open, Owner) and has only allowed characters
    bool isComplete() const;

    /// The password for the run only (PDFDocumentToolGUI::getRunParameters, the confirmation of MCP)
    QString getPassword() const;

    /// Empties the fields and their undo history (S5.1)
    void clear();

    /// The parts (tests; nullptr for a part, which the kind has not)
    PDFLineEdit* getPasswordField() const;
    PDFLineEdit* getRepeatField() const;
    PDFStrengthMeter* getStrengthMeter() const;
    PDFAppToolCallout* getLengthCallout() const;
    PDFAppParagraph* getHintText() const;

    /// The title of an algorithm in the texts of the card ("RC4 128-bit", 430:1999)
    static QString getAlgorithmTitle(PDFSecurityHandlerFactory::Algorithm algorithm);

    /// The level of the meter of a level of the estimate (one to one, the skeleton notes)
    static int toMeterLevel(PDFPasswordStrength::Level level);

Q_SIGNALS:
    /// The user has changed the password or its repetition (the last call of the change)
    void passwordChanged();

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void updateState();
    void updateMeter();
    void onPasswordEdited();

    /// The distinct characters of the password, which PDFDocEncoding has not (revision 4)
    QString getInvalidCharacters() const;

    Kind m_kind;
    PDFSecurityHandlerFactory::Algorithm m_algorithm = PDFSecurityHandlerFactory::AES_256;
    PDFPasswordStrength::Result m_strength;
    QString m_errorText;
    QString m_passwordLabelText;
    QString m_repeatLabelText;
    QString m_passwordAccessibleName;
    QString m_repeatAccessibleName;
    QVBoxLayout* m_layout = nullptr;
    QPointer<PDFLabel> m_passwordLabel;
    QPointer<PDFLineEdit> m_passwordField;
    QPointer<PDFAppParagraph> m_errorLabel;
    QPointer<PDFAppParagraph> m_hintText;
    QPointer<PDFStrengthMeter> m_meter;
    QPointer<PDFAppToolCallout> m_lengthCallout;
    QPointer<PDFLabel> m_repeatLabel;
    QPointer<PDFLineEdit> m_repeatField;
    QPointer<PDFAppParagraph> m_repeatErrorLabel;
};

}   // namespace pdf

#endif // PDFAPPTOOLPASSWORDCARD_H
