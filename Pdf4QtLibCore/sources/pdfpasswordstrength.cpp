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

#include "pdfpasswordstrength.h"

#include <QList>

#include <cmath>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the estimate (no free functions in the core, CLAUDE.md)
class PDFPasswordStrengthHelper
{
public:
    PDFPasswordStrengthHelper() = delete;

    /// The class of a character and the size of its pool (the guesses per character)
    enum class CharacterClass
    {
        Lower,      ///< a lower case letter (26 in ASCII)
        Upper,      ///< an upper case letter (26 in ASCII)
        Digit,      ///< a decimal digit (10)
        Symbol,     ///< a printable ASCII character, which is no letter and no digit (33 with the space)
        Other       ///< any other character (a letter of another script, an emoji)
    };

    static CharacterClass getClass(char32_t character);
    static int getPoolSize(CharacterClass characterClass);

    /// The character continues the pattern of the previous one: a repetition (aa), a sequence of
    /// the alphabet or of the digits up or down (abc, 321) or a neighbor on a row of the keyboard
    /// (qwerty, asdf, zxcv) - it adds almost nothing to the guesses (Q5.8)
    static bool isPatternContinuation(char32_t previous, char32_t current);
};

PDFPasswordStrengthHelper::CharacterClass PDFPasswordStrengthHelper::getClass(char32_t character)
{
    if (character < 0x80)
    {
        if (character >= 'a' && character <= 'z')
        {
            return CharacterClass::Lower;
        }
        if (character >= 'A' && character <= 'Z')
        {
            return CharacterClass::Upper;
        }
        if (character >= '0' && character <= '9')
        {
            return CharacterClass::Digit;
        }
        return CharacterClass::Symbol;
    }

    // A letter with a case (č, Ž) is a letter of its case; a digit of another script a digit
    if (QChar::isLower(character))
    {
        return CharacterClass::Lower;
    }
    if (QChar::isUpper(character))
    {
        return CharacterClass::Upper;
    }
    if (QChar::isDigit(character))
    {
        return CharacterClass::Digit;
    }
    if (QChar::isPunct(character) || QChar::isSymbol(character) || QChar::isSpace(character))
    {
        return CharacterClass::Symbol;
    }
    return CharacterClass::Other;
}

int PDFPasswordStrengthHelper::getPoolSize(CharacterClass characterClass)
{
    switch (characterClass)
    {
        case CharacterClass::Lower:
        case CharacterClass::Upper:
            return 26;

        case CharacterClass::Digit:
            return 10;

        case CharacterClass::Symbol:
            return 33;

        case CharacterClass::Other:
            return 100;
    }

    return 0;
}

bool PDFPasswordStrengthHelper::isPatternContinuation(char32_t previous, char32_t current)
{
    if (previous == current)
    {
        return true;
    }

    const char32_t previousLower = QChar::toLower(previous);
    const char32_t currentLower = QChar::toLower(current);
    const bool isAlphanumeric = previous < 0x80 && current < 0x80 &&
                                ((QChar::isLetter(previousLower) && QChar::isLetter(currentLower)) || (QChar::isDigit(previous) && QChar::isDigit(current)));
    if (isAlphanumeric && (currentLower == previousLower + 1 || currentLower + 1 == previousLower))
    {
        return true;
    }

    // The rows of the keyboard, both directions
    static constexpr const char* KEYBOARD_ROWS[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
    for (const char* row : KEYBOARD_ROWS)
    {
        for (const char* position = row; position[0] && position[1]; ++position)
        {
            const char32_t first = char32_t(position[0]);
            const char32_t second = char32_t(position[1]);
            if ((previousLower == first && currentLower == second) || (previousLower == second && currentLower == first))
            {
                return true;
            }
        }
    }

    return false;
}

PDFPasswordStrength::Result PDFPasswordStrength::evaluate(const QString& password, PDFSecurityHandlerFactory::Algorithm algorithm)
{
    Result result;
    if (password.isEmpty())
    {
        return result;
    }

    // The part of the password, which the algorithm uses (ISO 32000-2, 7.6.4.3.2 Algorithm 2 step
    // a: the first 32 bytes of PDFDocEncoding for revision 4; 7.6.4.3.3: the first 127 bytes of
    // UTF-8 for revision 6) - a character of the rest adds nothing
    QList<uint> codePoints = password.toUcs4();
    int effectiveLength = 0;
    const bool isRevision4 = algorithm == PDFSecurityHandlerFactory::RC4 || algorithm == PDFSecurityHandlerFactory::AES_128;
    if (isRevision4)
    {
        // PDFDocEncoding has one byte per character
        effectiveLength = int(qMin(codePoints.size(), qsizetype(REVISION_4_BYTES)));
    }
    else
    {
        int bytes = 0;
        for (const uint codePoint : codePoints)
        {
            const int characterBytes = (codePoint < 0x80) ? 1 : (codePoint < 0x800) ? 2 : (codePoint < 0x10000) ? 3 : 4;
            if (bytes + characterBytes > REVISION_6_BYTES)
            {
                break;
            }
            bytes += characterBytes;
            ++effectiveLength;
        }
    }

    result.effectiveLength = effectiveLength;
    result.isTruncated = effectiveLength < codePoints.size();

    // The entropy: the effective length times log2 of the size of the union of the used classes,
    // a character continuing a pattern of the previous one counts nothing (Q5.8)
    bool usedClasses[5] = { };
    int newCharacters = 0;
    for (int i = 0; i < effectiveLength; ++i)
    {
        const char32_t character = char32_t(codePoints[i]);
        usedClasses[int(PDFPasswordStrengthHelper::getClass(character))] = true;

        if (i == 0 || !PDFPasswordStrengthHelper::isPatternContinuation(char32_t(codePoints[i - 1]), character))
        {
            ++newCharacters;
        }
    }

    int poolSize = 0;
    for (int classIndex = 0; classIndex < 5; ++classIndex)
    {
        if (usedClasses[classIndex])
        {
            poolSize += PDFPasswordStrengthHelper::getPoolSize(PDFPasswordStrengthHelper::CharacterClass(classIndex));
        }
    }

    const double bits = newCharacters * std::log2(double(qMax(poolSize, 1)));
    result.bits = qMin(int(std::floor(bits)), MAXIMUM_BITS);
    result.level = getLevel(result.bits, false);

    // The labels of the meter (430:1068) by the effective part
    if (effectiveLength >= MINIMUM_LENGTH)
    {
        result.criteria |= MinimumLength;
    }
    if (usedClasses[int(PDFPasswordStrengthHelper::CharacterClass::Lower)] && usedClasses[int(PDFPasswordStrengthHelper::CharacterClass::Upper)])
    {
        result.criteria |= MixedCase;
    }
    if (usedClasses[int(PDFPasswordStrengthHelper::CharacterClass::Digit)])
    {
        result.criteria |= Digits;
    }
    if (usedClasses[int(PDFPasswordStrengthHelper::CharacterClass::Symbol)])
    {
        result.criteria |= Symbols;
    }

    // The copy of the password is overwritten (S5.1)
    PDFSecureMemory::clear(codePoints.data(), size_t(codePoints.size()) * sizeof(uint));
    return result;
}

PDFPasswordStrength::Level PDFPasswordStrength::getLevel(int bits, bool isEmpty)
{
    if (isEmpty)
    {
        return Level::Empty;
    }

    if (bits < VERY_WEAK_BELOW)
    {
        return Level::VeryWeak;
    }
    if (bits < WEAK_BELOW)
    {
        return Level::Weak;
    }
    if (bits < MODERATE_BELOW)
    {
        return Level::Moderate;
    }
    if (bits < STRONG_BELOW)
    {
        return Level::Strong;
    }
    return Level::VeryStrong;
}

PDFPasswordStrength::Level PDFPasswordStrength::getKeyLevel(int keyBits)
{
    // R5-20 (433:3049): RSA of 2048 bits and more Very strong, 1024 Weak; a smaller key is
    // broken (Very weak), an unknown one has no level
    if (keyBits <= 0)
    {
        return Level::Empty;
    }
    if (keyBits < 1024)
    {
        return Level::VeryWeak;
    }
    if (keyBits < 2048)
    {
        return Level::Weak;
    }
    return Level::VeryStrong;
}

QString PDFPasswordStrength::getLevelText(Level level)
{
    switch (level)
    {
        case Level::Empty:
            return tr("Type a password");
        case Level::VeryWeak:
            return tr("Very weak");
        case Level::Weak:
            return tr("Weak");
        case Level::Moderate:
            return tr("Moderate");
        case Level::Strong:
            return tr("Strong");
        case Level::VeryStrong:
            return tr("Very strong");
    }

    return QString();
}

}   // namespace pdf
