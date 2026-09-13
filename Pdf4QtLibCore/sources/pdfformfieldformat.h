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

#ifndef PDFFORMFIELDFORMAT_H
#define PDFFORMFIELDFORMAT_H

#include "pdfglobal.h"

#include <QColor>
#include <QString>
#include <QDateTime>

#include <optional>

namespace pdf
{
class PDFAction;

/// Native implementation of the standard functions of the Acrobat JavaScript
/// form library (AFNumber_Format, AFDate_KeystrokeEx, ...), which are used
/// in format (F) and keystroke (K) actions of text form fields. JavaScript
/// is not executed, only a script consisting of a single call of a supported
/// function with literal arguments is recognized. Validation and calculation
/// scripts are not handled.
class PDF4QTLIBCORESHARED_EXPORT PDFFormFieldFormat
{
public:
    explicit inline PDFFormFieldFormat() = default;

    enum class Function
    {
        None,                   ///< Script is not recognized, value is used as is
        NumberFormat,           ///< AFNumber_Format
        NumberKeystroke,        ///< AFNumber_Keystroke
        PercentFormat,          ///< AFPercent_Format
        PercentKeystroke,       ///< AFPercent_Keystroke
        DateFormat,             ///< AFDate_Format, AFDate_FormatEx, AFTime_Format, AFTime_FormatEx
        DateKeystroke,          ///< AFDate_Keystroke, AFDate_KeystrokeEx, AFTime_Keystroke, AFTime_KeystrokeEx
        SpecialFormat,          ///< AFSpecial_Format
        SpecialKeystroke,       ///< AFSpecial_Keystroke
        SpecialKeystrokeMask    ///< AFSpecial_KeystrokeEx
    };

    struct FormattedText
    {
        QString text;

        /// Text color requested by the format, invalid color
        /// means, that text color is not changed.
        QColor textColor;
    };

    /// Parses script of the format (F) or keystroke (K) action. If the script
    /// is not a single call of a supported function with literal arguments,
    /// then object with function None is returned.
    /// \param script JavaScript code
    static PDFFormFieldFormat parse(const QString& script);

    /// Parses JavaScript action. If action is not a JavaScript
    /// action, then object with function None is returned.
    /// \param action Action (can be nullptr)
    static PDFFormFieldFormat parse(const PDFAction* action);

    Function getFunction() const { return m_function; }

    /// Returns true, if object represents a format function
    bool isFormat() const;

    /// Returns true, if object represents a keystroke function
    bool isKeystroke() const;

    /// Formats the field value for display (format action). If object
    /// is not a format function, then value is returned unchanged.
    /// \param value Field value
    FormattedText format(const QString& value) const;

    /// Returns true, if text being edited can be accepted
    /// (keystroke action with willCommit set to false).
    /// \param text Complete text after the change
    bool isKeystrokeAccepted(const QString& text) const;

    /// Returns committed value of the edited text (keystroke action
    /// with willCommit set to true). If text is rejected, then
    /// std::nullopt is returned.
    /// \param text Edited text
    std::optional<QString> commit(const QString& text) const;

    /// Returns text, which is presented to the user for editing
    /// the value (inverse of the conversion done in commit).
    /// \param value Field value
    QString getEditText(const QString& value) const;

    /// Formats number, \p separatorStyle is 0 for "1,234.56", 1 for "1234.56",
    /// 2 for "1.234,56", 3 for "1234,56" and 4 for "1'234.56" (util.printf).
    static QString formatNumber(double value, int decimals, int separatorStyle);

    /// Converts text to a number, decimal comma is accepted (AFMakeNumber)
    static std::optional<double> makeNumber(const QString& text);

    /// Formats date using Acrobat date format, such as "dd.mm.yyyy HH:MM" (util.printd)
    static QString printDate(const QString& format, const QDateTime& dateTime);

    /// Scans date strictly using Acrobat date format (util.scand)
    static std::optional<QDateTime> scanDate(const QString& format, const QString& text);

    /// Scans date using Acrobat date format, if the text does not match the format
    /// exactly, then ISO date and order of the date components is tried.
    static std::optional<QDateTime> parseDate(const QString& format, const QString& text);

    /// Formats text using Acrobat mask, such as "(999) 999-9999" (util.printx)
    static QString printMask(const QString& format, const QString& text);

private:
    Function m_function = Function::None;
    int m_decimals = 0;
    int m_separatorStyle = 0;
    int m_negativeStyle = 0;
    QString m_currency;
    bool m_currencyPrepend = false;
    bool m_percentPrepend = false;
    QString m_dateFormat;
    int m_specialFormat = 0;
    QString m_mask;
};

}   // namespace pdf

#endif // PDFFORMFIELDFORMAT_H
