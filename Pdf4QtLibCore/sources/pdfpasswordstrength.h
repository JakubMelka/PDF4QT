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

#ifndef PDFPASSWORDSTRENGTH_H
#define PDFPASSWORDSTRENGTH_H

#include "pdfglobal.h"
#include "pdfsecurityhandler.h"

#include <QFlags>
#include <QString>

namespace pdf
{

/// The estimate of the strength of a password (plan of step 5 of phase 2, 6.6, B92, owner answer
/// Q5.8, R5-20; package P1a): the Strength meter of Figma (455:1270, "Very strong · 118 of 128
/// bits", the labels 12+ characters, Aa, 123, #$&) reads it. It replaces the Shannon estimate of
/// PDFSecurityHandlerFactory::getPasswordEntropy for the new application (the old one stays for the
/// old applications).
///
/// The rule (Q5.8): the entropy is the effective length times log2 of the size of the union of the
/// used character classes, with a penalty for repetitions and sequences (aaa, 123, abc, qwerty), at
/// most MAXIMUM_BITS; no dictionary of passwords. The effective length follows the algorithm: the
/// first 32 bytes of PDFDocEncoding for revision 4 (RC4, AES-128), the first 127 bytes of UTF-8
/// after SASLprep for revision 6 (AES-256) - a longer password is truncated (isTruncated, the
/// callout "Only the first 32 characters are used with AES 128-bit.").
/// The levels by the instances of Figma (14 Very weak, 92 and 96 Strong, 118 Very strong): below
/// 28 bits Very weak, below 48 Weak, below 72 Moderate, below 112 Strong, else Very strong. The tests
/// check the levels and the thresholds, never the numbers of the frames (R5-20: illustrations).
/// Pure functions of values, thread-safe; nothing of the password is kept or logged.
class PDF4QTLIBCORESHARED_EXPORT PDFPasswordStrength
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFPasswordStrength)

public:
    PDFPasswordStrength() = delete;

    /// The level of the meter (its word: "Very weak" ... "Very strong"; Empty = "Type a password")
    enum class Level
    {
        Empty,
        VeryWeak,
        Weak,
        Moderate,
        Strong,
        VeryStrong
    };

    /// The criteria of the labels of the meter (430:1068)
    enum Criterion
    {
        NoCriterion = 0x00,
        MinimumLength = 0x01,   ///< 12+ characters (of the effective part)
        MixedCase = 0x02,       ///< Aa: a lower and an upper case letter
        Digits = 0x04,          ///< 123: a digit
        Symbols = 0x08          ///< #$&: a character, which is no letter and no digit
    };
    Q_DECLARE_FLAGS(Criteria, Criterion)

    /// Highest number of bits (the scale of the meter, "of 128 bits")
    static constexpr int MAXIMUM_BITS = 128;

    /// Length of the criterion MinimumLength
    static constexpr int MINIMUM_LENGTH = 12;

    /// The bits below each threshold give the level below it (Q5.8)
    static constexpr int VERY_WEAK_BELOW = 28;
    static constexpr int WEAK_BELOW = 48;
    static constexpr int MODERATE_BELOW = 72;
    static constexpr int STRONG_BELOW = 112;

    /// The effective bytes of a password of revision 4 (PDFDocEncoding) and of revision 6 (UTF-8)
    static constexpr int REVISION_4_BYTES = 32;
    static constexpr int REVISION_6_BYTES = 127;

    /// The estimate of one password
    struct Result
    {
        int bits = 0;                   ///< 0 ... MAXIMUM_BITS
        Level level = Level::Empty;     ///< The level of the bits (Empty for an empty password)
        Criteria criteria;              ///< The criteria met by the effective part
        int effectiveLength = 0;        ///< Characters of the password, which the algorithm uses
        bool isTruncated = false;       ///< The algorithm uses only a part of the password
    };

    /// The estimate of \p password for \p algorithm (None and Certificate: the rule of AES-256)
    static Result evaluate(const QString& password, PDFSecurityHandlerFactory::Algorithm algorithm);

    /// The level of a number of bits (the thresholds above; 0 bits of a non-empty password = VeryWeak)
    static Level getLevel(int bits, bool isEmpty);

    /// The strength of the key of a certificate (433:3049 "Follows the key of the certificate (RSA
    /// 2048-bit)", R5-20): RSA of 2048 bits and more Very strong, 1024 Weak
    static Level getKeyLevel(int keyBits);

    /// The translated word of a level ("Very strong"; Empty: "Type a password")
    static QString getLevelText(Level level);
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFPasswordStrength::Criteria)

#endif // PDFPASSWORDSTRENGTH_H
