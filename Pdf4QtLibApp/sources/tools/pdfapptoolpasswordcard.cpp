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

#include "pdfapptoolpasswordcard.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelparts.h"
#include "pdfappworkspace.h"

#include "pdfcomponentpanels.h"
#include "pdfencoding.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QEvent>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the card, no state
class PDFAppToolPasswordCardHelper
{
public:
    PDFAppToolPasswordCardHelper() = delete;

    /// A group of a label and its field (430:1213: the label 15 high, the field 4 below it)
    static QWidget* createFieldGroup(PDFLabel* label, PDFLineEdit* field, PDFAppParagraph* error, QWidget* parent)
    {
        QWidget* group = new QWidget(parent);
        QVBoxLayout* layout = new QVBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(PDFAppToolPasswordCard::FIELD_GAP);
        if (label)
        {
            label->setParent(group);
            layout->addWidget(label);
        }
        field->setParent(group);
        layout->addWidget(field);
        if (error)
        {
            error->setParent(group);
            layout->addWidget(error);
        }
        return group;
    }

    static PDFLineEdit* createField(const QString& objectName, QWidget* parent)
    {
        PDFLineEdit* field = new PDFLineEdit(parent);
        field->setObjectName(objectName);
        field->setPasswordField();
        field->setMaxLength(PDFAppToolPasswordCard::MAXIMUM_LENGTH);
        field->setFixedHeight(PDFAppToolPasswordCard::FIELD_HEIGHT);
        return field;
    }

    /// The text of an error or a hint below a field (Regular 11 in lines of 13)
    static PDFAppParagraph* createLine(const QString& objectName, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFAppParagraph* text = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, color, parent);
        text->setObjectName(objectName);
        text->setMinimumWidth(1);
        text->hide();
        return text;
    }

    /// Empties a field and its undo history (QLineEdit::setText clears it, clear() would keep it)
    static void clearField(PDFLineEdit* field)
    {
        if (field)
        {
            field->setText(QString());
            field->setInvalid(false);
        }
    }
};

PDFAppToolPasswordCard::PDFAppToolPasswordCard(Kind kind, QWidget* parent) :
    BaseClass(parent),
    m_kind(kind)
{
    using Helper = PDFAppToolPasswordCardHelper;

    setObjectName(QStringLiteral("passwordCard"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(CANVAS_GAP);

    // The field and its error (430:1213, 430:1999); the label is not a part of the kind
    // Certificate (433:3221: the card of package P3 has it)
    if (m_kind != Kind::Certificate)
    {
        m_passwordLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextPrimary, this);
        m_passwordLabel->setObjectName(QStringLiteral("passwordLabel"));
    }
    m_passwordField = Helper::createField(QStringLiteral("passwordField"), this);
    m_errorLabel = Helper::createLine(QStringLiteral("passwordError"), PDFThemeColors::TextDanger, this);
    QWidget* passwordGroup = Helper::createFieldGroup(m_passwordLabel, m_passwordField, m_errorLabel, this);
    if (m_kind == Kind::Certificate)
    {
        m_hintText = Helper::createLine(QStringLiteral("passwordHint"), PDFThemeColors::TextMuted, passwordGroup);
        m_hintText->show();
        passwordGroup->layout()->addWidget(m_hintText);
    }
    m_layout->addWidget(passwordGroup);
    connect(m_passwordField.data(), &QLineEdit::textEdited, this, &PDFAppToolPasswordCard::onPasswordEdited);

    if (m_kind != Kind::Certificate)
    {
        // The meter (455:16290) and the effective length (2.5: an amber callout below the field)
        m_meter = new PDFStrengthMeter(this);
        m_meter->setObjectName(QStringLiteral("passwordStrength"));
        m_layout->addWidget(m_meter);

        m_lengthCallout = new PDFAppToolCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString(), this);
        m_lengthCallout->setObjectName(QStringLiteral("passwordLengthCallout"));
        m_lengthCallout->setCompactStyle();
        m_lengthCallout->hide();
        m_layout->addWidget(m_lengthCallout);

        // Repeat password (430:1236) and its error (447:15766)
        m_repeatLabel = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextPrimary, this);
        m_repeatLabel->setObjectName(QStringLiteral("repeatLabel"));
        m_repeatField = Helper::createField(QStringLiteral("repeatField"), this);
        m_repeatErrorLabel = Helper::createLine(QStringLiteral("repeatError"), PDFThemeColors::TextDanger, this);
        m_layout->addWidget(Helper::createFieldGroup(m_repeatLabel, m_repeatField, m_repeatErrorLabel, this));
        connect(m_repeatField.data(), &QLineEdit::textEdited, this, &PDFAppToolPasswordCard::onPasswordEdited);
    }

    retranslate();
    updateState();
}

PDFAppToolPasswordCard::~PDFAppToolPasswordCard()
{
    // S5.1: the fields are emptied before they go (best effort - QString is not wiped by Qt)
    PDFAppToolPasswordCardHelper::clearField(m_passwordField);
    PDFAppToolPasswordCardHelper::clearField(m_repeatField);
}

void PDFAppToolPasswordCard::setGap(int gap)
{
    m_layout->setSpacing(gap);
}

void PDFAppToolPasswordCard::setLabels(const QString& passwordLabel, const QString& repeatLabel)
{
    m_passwordLabelText = passwordLabel;
    m_repeatLabelText = repeatLabel;
    retranslate();
}

void PDFAppToolPasswordCard::setAccessibleLabels(const QString& passwordName, const QString& repeatName)
{
    m_passwordAccessibleName = passwordName;
    m_repeatAccessibleName = repeatName;
    retranslate();
}

QString PDFAppToolPasswordCard::getAlgorithmTitle(PDFSecurityHandlerFactory::Algorithm algorithm)
{
    switch (algorithm)
    {
        case PDFSecurityHandlerFactory::RC4:
            return tr("RC4 128-bit");
        case PDFSecurityHandlerFactory::AES_128:
            return tr("AES 128-bit");
        case PDFSecurityHandlerFactory::AES_256:
        case PDFSecurityHandlerFactory::Certificate:
            return tr("AES 256-bit");
        case PDFSecurityHandlerFactory::None:
            break;
    }
    return QString();
}

int PDFAppToolPasswordCard::toMeterLevel(PDFPasswordStrength::Level level)
{
    switch (level)
    {
        case PDFPasswordStrength::Level::Empty:
            return int(PDFStrengthMeter::Level::Empty);
        case PDFPasswordStrength::Level::VeryWeak:
            return int(PDFStrengthMeter::Level::VeryWeak);
        case PDFPasswordStrength::Level::Weak:
            return int(PDFStrengthMeter::Level::Weak);
        case PDFPasswordStrength::Level::Moderate:
            return int(PDFStrengthMeter::Level::Moderate);
        case PDFPasswordStrength::Level::Strong:
            return int(PDFStrengthMeter::Level::Strong);
        case PDFPasswordStrength::Level::VeryStrong:
            return int(PDFStrengthMeter::Level::VeryStrong);
    }
    return int(PDFStrengthMeter::Level::Empty);
}

void PDFAppToolPasswordCard::setAlgorithm(PDFSecurityHandlerFactory::Algorithm algorithm)
{
    if (m_algorithm != algorithm)
    {
        m_algorithm = algorithm;
        updateState();
    }
}

void PDFAppToolPasswordCard::setErrorText(const QString& text)
{
    if (m_errorText != text)
    {
        m_errorText = text;
        updateState();
    }
}

QString PDFAppToolPasswordCard::getInvalidCharacters() const
{
    // ISO 32000-2, 7.6.4.3.3: a password of revisions 2 - 4 is PDFDocEncoding; revision 6 is UTF-8
    // (SASLprep of the factory, B93)
    if ((m_algorithm != PDFSecurityHandlerFactory::RC4 && m_algorithm != PDFSecurityHandlerFactory::AES_128) || !m_passwordField)
    {
        return QString();
    }

    QString invalid;
    QString scratch;
    const QString text = m_passwordField->text();
    if (PDFEncoding::canConvertToEncoding(text, PDFEncoding::Encoding::PDFDoc, &scratch))
    {
        return QString();
    }
    for (const QChar character : text)
    {
        QString single(character);
        if (!invalid.contains(character) && !PDFEncoding::canConvertToEncoding(single, PDFEncoding::Encoding::PDFDoc, &scratch))
        {
            invalid.append(character);
        }
    }
    return invalid;
}

bool PDFAppToolPasswordCard::hasInvalidCharacters() const
{
    return !getInvalidCharacters().isEmpty();
}

QString PDFAppToolPasswordCard::getErrorText() const
{
    return m_errorLabel && !m_errorLabel->isHidden() ? m_errorLabel->text() : QString();
}

QString PDFAppToolPasswordCard::getRepeatErrorText() const
{
    return m_repeatErrorLabel && !m_repeatErrorLabel->isHidden() ? m_repeatErrorLabel->text() : QString();
}

bool PDFAppToolPasswordCard::isMismatch() const
{
    // 447:15766: the repetition differs - shown, when it is as long as the password or no
    // longer its beginning (no error while the user is typing it)
    if (!m_repeatField || !m_passwordField || m_repeatField->text().isEmpty())
    {
        return false;
    }
    const QString password = m_passwordField->text();
    const QString repeat = m_repeatField->text();
    return repeat != password && (repeat.size() >= password.size() || !password.startsWith(repeat));
}

bool PDFAppToolPasswordCard::isEmpty() const
{
    return !m_passwordField || m_passwordField->text().isEmpty();
}

bool PDFAppToolPasswordCard::isComplete() const
{
    if (isEmpty() || hasInvalidCharacters())
    {
        return false;
    }
    return m_kind == Kind::Certificate || (m_repeatField && m_repeatField->text() == m_passwordField->text());
}

QString PDFAppToolPasswordCard::getPassword() const
{
    return m_passwordField ? m_passwordField->text() : QString();
}

void PDFAppToolPasswordCard::clear()
{
    using Helper = PDFAppToolPasswordCardHelper;

    // S5.1: after the run, Cancel, the close of the tool, a change of the preset; the undo
    // history of the fields goes with the text (Ctrl+Z gives nothing back)
    Helper::clearField(m_passwordField);
    Helper::clearField(m_repeatField);
    if (m_passwordField)
    {
        updateState();
    }
}

PDFLineEdit* PDFAppToolPasswordCard::getPasswordField() const
{
    return m_passwordField.data();
}

PDFLineEdit* PDFAppToolPasswordCard::getRepeatField() const
{
    return m_repeatField.data();
}

PDFStrengthMeter* PDFAppToolPasswordCard::getStrengthMeter() const
{
    return m_meter.data();
}

PDFAppToolCallout* PDFAppToolPasswordCard::getLengthCallout() const
{
    return m_lengthCallout.data();
}

PDFAppParagraph* PDFAppToolPasswordCard::getHintText() const
{
    return m_hintText.data();
}

void PDFAppToolPasswordCard::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
        updateState();
    }
}

void PDFAppToolPasswordCard::retranslate()
{
    if (m_passwordLabel)
    {
        m_passwordLabel->setText(m_passwordLabelText.isEmpty() ? tr("Password") : m_passwordLabelText);
    }
    if (m_repeatLabel)
    {
        m_repeatLabel->setText(m_repeatLabelText.isEmpty() ? tr("Repeat password") : m_repeatLabelText);
    }

    // 430:1287: the placeholder of both fields; the accessible names never carry the password
    // (setAccessibleLabels: the names, which tell an owner password from an open password)
    const QString passwordName = !m_passwordAccessibleName.isEmpty() ? m_passwordAccessibleName : (m_passwordLabel ? m_passwordLabel->text() : tr("Certificate password"));
    const QString repeatName = !m_repeatAccessibleName.isEmpty() ? m_repeatAccessibleName : (m_repeatLabel ? m_repeatLabel->text() : QString());
    if (m_passwordField)
    {
        m_passwordField->setPlaceholderText(tr("Password"));
        m_passwordField->setAccessibleName(passwordName);
    }
    if (m_repeatField)
    {
        m_repeatField->setPlaceholderText(tr("Password"));
        m_repeatField->setAccessibleName(repeatName);
    }
}

void PDFAppToolPasswordCard::updateMeter()
{
    if (!m_meter)
    {
        return;
    }

    // 430:1068 "Very strong · 118 of 128 bits", 430:1287 "Password strength · Type a password"; the
    // estimate of the effective part (B92, Q5.8, R5-20)
    m_strength = PDFPasswordStrength::evaluate(getPassword(), m_algorithm);
    const bool isEmptyPassword = isEmpty();
    m_meter->setLevel(static_cast<PDFStrengthMeter::Level>(toMeterLevel(isEmptyPassword ? PDFPasswordStrength::Level::Empty : m_strength.level)));
    m_meter->setTitle(tr("Password strength"));
    m_meter->setDetail(isEmptyPassword ? tr("Type a password") : tr("%1 of %2 bits").arg(m_strength.bits).arg(PDFPasswordStrength::MAXIMUM_BITS));

    PDFStrengthMeter::Criteria criteria;
    criteria.setFlag(PDFStrengthMeter::Criterion::Length, m_strength.criteria.testFlag(PDFPasswordStrength::MinimumLength));
    criteria.setFlag(PDFStrengthMeter::Criterion::Case, m_strength.criteria.testFlag(PDFPasswordStrength::MixedCase));
    criteria.setFlag(PDFStrengthMeter::Criterion::Digits, m_strength.criteria.testFlag(PDFPasswordStrength::Digits));
    criteria.setFlag(PDFStrengthMeter::Criterion::Symbols, m_strength.criteria.testFlag(PDFPasswordStrength::Symbols));
    m_meter->setSatisfiedCriteria(isEmptyPassword ? PDFStrengthMeter::Criteria() : criteria);

    // 2.5: "Only the first 32 characters are used with AES 128-bit." (the effective part, 7.6.4.3.3)
    if (m_lengthCallout)
    {
        const bool isTruncated = !isEmptyPassword && m_strength.isTruncated;
        if (isTruncated)
        {
            const bool isRevision4 = m_algorithm == PDFSecurityHandlerFactory::RC4 || m_algorithm == PDFSecurityHandlerFactory::AES_128;
            m_lengthCallout->setTexts(isRevision4 ? tr("Only the first %1 characters are used with %2.").arg(PDFPasswordStrength::REVISION_4_BYTES).arg(getAlgorithmTitle(m_algorithm))
                                                  : tr("Only the first %1 bytes of the password are used with %2.").arg(PDFPasswordStrength::REVISION_6_BYTES).arg(getAlgorithmTitle(m_algorithm)),
                                      QString());
        }
        m_lengthCallout->setVisible(isTruncated);
    }
}

void PDFAppToolPasswordCard::updateState()
{
    // The error of the field: the validation of the tool (432:3721) first, then the characters
    // of revision 4 (430:1999 - the characters themselves are named, as the frame does)
    QString error = m_errorText;
    if (error.isEmpty())
    {
        const QString invalid = getInvalidCharacters();
        if (!invalid.isEmpty())
        {
            QStringList quoted;
            for (const QChar character : invalid)
            {
                quoted << tr("“%1”").arg(character);
            }
            const QString last = quoted.takeLast();
            const QString list = quoted.isEmpty() ? last : tr("%1 and %2", "The last item of a list of characters").arg(quoted.join(tr(", ", "Separator of a list of characters")), last);
            error = invalid.size() == 1 ? tr("Character %1 cannot be used with %2. Use AES 256-bit or a password with basic Latin characters.").arg(list, getAlgorithmTitle(m_algorithm))
                                        : tr("Characters %1 cannot be used with %2. Use AES 256-bit or a password with basic Latin characters.").arg(list, getAlgorithmTitle(m_algorithm));
        }
    }

    if (m_errorLabel)
    {
        m_errorLabel->setText(error);
        m_errorLabel->setVisible(!error.isEmpty());
    }
    if (m_passwordField)
    {
        // The message of the field is the state only (an accessible text never has a character)
        m_passwordField->setInvalid(!error.isEmpty());
    }
    if (m_hintText)
    {
        // 433:3221: the error replaces the hint
        m_hintText->setText(tr("Used once to read the certificate file. It is not stored."));
        m_hintText->setVisible(error.isEmpty());
    }

    const bool isRepeatError = isMismatch();
    if (m_repeatErrorLabel)
    {
        m_repeatErrorLabel->setText(tr("The passwords do not match."));
        m_repeatErrorLabel->setVisible(isRepeatError);
    }
    if (m_repeatField)
    {
        m_repeatField->setInvalid(isRepeatError);
    }

    updateMeter();
}

void PDFAppToolPasswordCard::onPasswordEdited()
{
    updateState();
    Q_EMIT passwordChanged();
}

}   // namespace pdf

#include "moc_pdfapptoolpasswordcard.cpp"
