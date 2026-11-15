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

// The estimate of the strength of a password (step 5 of phase 2, plan 6.6, 9, owner answer Q5.8,
// R5-20; package P1a): the effective lengths of ISO 32000-2, the levels by the thresholds of Q5.8
// (the instances of Figma 455:1270: 14 bits Very weak, 92 and 96 Strong, 118 Very strong), the
// labels of the criteria (430:1068), the penalty for repetitions and sequences, the key of a
// certificate (R5-20). The numbers of bits of the frames are illustrations, never checked.

#include "pdfpasswordstrength.h"

#include <QtTest>

class PasswordStrengthTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void interfaceLimits();
    void levels();
    void keyLevels();
    void levelTexts();
    void emptyPassword();
    void criteria_data();
    void criteria();
    void effectiveLength_data();
    void effectiveLength();
    void truncatedPartAddsNothing();
    void patterns_data();
    void patterns();
    void classesAndLength();
};

using Strength = pdf::PDFPasswordStrength;
using Algorithm = pdf::PDFSecurityHandlerFactory::Algorithm;

void PasswordStrengthTest::interfaceLimits()
{
    // ISO 32000-2, 7.6.4.3.2 (Algorithm 2, step a): the password of revisions 2 - 4 is padded or
    // truncated to exactly 32 bytes; 7.6.4.3.3: the UTF-8 password of revision 6 is truncated to
    // 127 bytes
    QCOMPARE(Strength::REVISION_4_BYTES, 32);
    QCOMPARE(Strength::REVISION_6_BYTES, 127);

    // Q5.8 by the instances of Figma: below 28 bits Very weak, below 48 Weak, below 72 Moderate,
    // below 112 Strong, else Very strong; the scale "of 128 bits", the label "12+ characters"
    QCOMPARE(Strength::VERY_WEAK_BELOW, 28);
    QCOMPARE(Strength::WEAK_BELOW, 48);
    QCOMPARE(Strength::MODERATE_BELOW, 72);
    QCOMPARE(Strength::STRONG_BELOW, 112);
    QCOMPARE(Strength::MAXIMUM_BITS, 128);
    QCOMPARE(Strength::MINIMUM_LENGTH, 12);
}

void PasswordStrengthTest::levels()
{
    // Q5.8: below 28 Very weak, below 48 Weak, below 72 Moderate, below 112 Strong, else Very
    // strong; the bits of the instances of Figma fall into their levels
    QCOMPARE(Strength::getLevel(0, false), Strength::Level::VeryWeak);
    QCOMPARE(Strength::getLevel(14, false), Strength::Level::VeryWeak);
    QCOMPARE(Strength::getLevel(27, false), Strength::Level::VeryWeak);
    QCOMPARE(Strength::getLevel(28, false), Strength::Level::Weak);
    QCOMPARE(Strength::getLevel(47, false), Strength::Level::Weak);
    QCOMPARE(Strength::getLevel(48, false), Strength::Level::Moderate);
    QCOMPARE(Strength::getLevel(71, false), Strength::Level::Moderate);
    QCOMPARE(Strength::getLevel(72, false), Strength::Level::Strong);
    QCOMPARE(Strength::getLevel(92, false), Strength::Level::Strong);
    QCOMPARE(Strength::getLevel(96, false), Strength::Level::Strong);
    QCOMPARE(Strength::getLevel(111, false), Strength::Level::Strong);
    QCOMPARE(Strength::getLevel(112, false), Strength::Level::VeryStrong);
    QCOMPARE(Strength::getLevel(118, false), Strength::Level::VeryStrong);
    QCOMPARE(Strength::getLevel(128, false), Strength::Level::VeryStrong);
    QCOMPARE(Strength::getLevel(100, true), Strength::Level::Empty);
}

void PasswordStrengthTest::keyLevels()
{
    // R5-20 (433:3049 "Follows the key of the certificate (RSA 2048-bit)"): RSA of 2048 bits and
    // more Very strong, 1024 Weak
    QCOMPARE(Strength::getKeyLevel(2048), Strength::Level::VeryStrong);
    QCOMPARE(Strength::getKeyLevel(4096), Strength::Level::VeryStrong);
    QCOMPARE(Strength::getKeyLevel(1024), Strength::Level::Weak);
    QCOMPARE(Strength::getKeyLevel(512), Strength::Level::VeryWeak);
    QCOMPARE(Strength::getKeyLevel(0), Strength::Level::Empty);
}

void PasswordStrengthTest::levelTexts()
{
    // The words of the meter (455:1270) and of the empty field
    QCOMPARE(Strength::getLevelText(Strength::Level::Empty), QStringLiteral("Type a password"));
    QCOMPARE(Strength::getLevelText(Strength::Level::VeryWeak), QStringLiteral("Very weak"));
    QCOMPARE(Strength::getLevelText(Strength::Level::Weak), QStringLiteral("Weak"));
    QCOMPARE(Strength::getLevelText(Strength::Level::Moderate), QStringLiteral("Moderate"));
    QCOMPARE(Strength::getLevelText(Strength::Level::Strong), QStringLiteral("Strong"));
    QCOMPARE(Strength::getLevelText(Strength::Level::VeryStrong), QStringLiteral("Very strong"));
}

void PasswordStrengthTest::emptyPassword()
{
    const Strength::Result result = Strength::evaluate(QString(), Algorithm::AES_256);
    QCOMPARE(result.level, Strength::Level::Empty);
    QCOMPARE(result.bits, 0);
    QCOMPARE(result.effectiveLength, 0);
    QVERIFY(!result.isTruncated);
    QCOMPARE(int(result.criteria), int(Strength::NoCriterion));
}

void PasswordStrengthTest::criteria_data()
{
    QTest::addColumn<QString>("password");
    QTest::addColumn<int>("criteria");

    // The labels of 430:1068: 12+ characters, Aa (both cases), 123, #$&
    QTest::newRow("lower") << QStringLiteral("abc") << int(Strength::NoCriterion);
    QTest::newRow("upper-twelve") << QStringLiteral("QWZXNVBRTPLK") << int(Strength::MinimumLength);
    QTest::newRow("eleven") << QStringLiteral("Ab1!efghijk") << int(Strength::MixedCase | Strength::Digits | Strength::Symbols);
    QTest::newRow("all") << QStringLiteral("Ab1!efghijkl") << int(Strength::MinimumLength | Strength::MixedCase | Strength::Digits | Strength::Symbols);
    QTest::newRow("digits") << QStringLiteral("7305") << int(Strength::Digits);
    QTest::newRow("space-is-symbol") << QStringLiteral("a b") << int(Strength::Symbols);
    QTest::newRow("diacritics-case") << QString::fromUtf8("\xC5\xBE\xC5\xBD") << int(Strength::MixedCase);
}

void PasswordStrengthTest::criteria()
{
    QFETCH(QString, password);
    QFETCH(int, criteria);
    QCOMPARE(int(Strength::evaluate(password, Algorithm::AES_256).criteria), criteria);
}

void PasswordStrengthTest::effectiveLength_data()
{
    QTest::addColumn<QString>("password");
    QTest::addColumn<int>("algorithm");
    QTest::addColumn<int>("effectiveLength");
    QTest::addColumn<bool>("isTruncated");

    // ISO 32000-2, 7.6.4.3.2 Algorithm 2 a: 32 bytes of PDFDocEncoding (one per character);
    // 7.6.4.3.3: 127 bytes of UTF-8 (a character of two bytes counts twice)
    QTest::newRow("rc4-32") << QString(32, QChar('a')) << int(Algorithm::RC4) << 32 << false;
    QTest::newRow("rc4-40") << QString(40, QChar('a')) << int(Algorithm::RC4) << 32 << true;
    QTest::newRow("aes128-40") << QString(40, QChar('a')) << int(Algorithm::AES_128) << 32 << true;
    QTest::newRow("aes256-40") << QString(40, QChar('a')) << int(Algorithm::AES_256) << 40 << false;
    QTest::newRow("aes256-127") << QString(127, QChar('a')) << int(Algorithm::AES_256) << 127 << false;
    QTest::newRow("aes256-130") << QString(130, QChar('a')) << int(Algorithm::AES_256) << 127 << true;
    QTest::newRow("aes256-two-bytes") << QString(64, QChar(0x017E)) << int(Algorithm::AES_256) << 63 << true;
    QTest::newRow("certificate-as-aes256") << QString(130, QChar('a')) << int(Algorithm::Certificate) << 127 << true;
}

void PasswordStrengthTest::effectiveLength()
{
    QFETCH(QString, password);
    QFETCH(int, algorithm);
    QFETCH(int, effectiveLength);
    QFETCH(bool, isTruncated);

    const Strength::Result result = Strength::evaluate(password, Algorithm(algorithm));
    QCOMPARE(result.effectiveLength, effectiveLength);
    QCOMPARE(result.isTruncated, isTruncated);
}

void PasswordStrengthTest::truncatedPartAddsNothing()
{
    // B92: the meter counts only the part the algorithm uses - a strong tail after 32 characters
    // makes revision 4 no stronger, revision 6 uses it
    const QString weakHead(32, QChar('a'));
    const QString strongTail = QStringLiteral("k8#Vq2!mZ7&xR4$wT9pL");
    const Strength::Result revision4 = Strength::evaluate(weakHead + strongTail, Algorithm::AES_128);
    QCOMPARE(revision4.bits, Strength::evaluate(weakHead, Algorithm::AES_128).bits);
    QCOMPARE(revision4.level, Strength::Level::VeryWeak);

    const Strength::Result revision6 = Strength::evaluate(weakHead + strongTail, Algorithm::AES_256);
    QCOMPARE(revision6.level, Strength::Level::VeryStrong);
}

void PasswordStrengthTest::patterns_data()
{
    QTest::addColumn<QString>("password");
    QTest::addColumn<int>("level");

    // Q5.8: repetitions and sequences of the alphabet, of the digits and of the keyboard add
    // (almost) nothing; no dictionary - "Password123!" gets only Moderate (plan Q5.8)
    QTest::newRow("repetition") << QString(16, QChar('a')) << int(Strength::Level::VeryWeak);
    QTest::newRow("alphabet") << QStringLiteral("abcdefghijklmnop") << int(Strength::Level::VeryWeak);
    QTest::newRow("alphabet-down") << QStringLiteral("ponmlkjihgfedcba") << int(Strength::Level::VeryWeak);
    QTest::newRow("digits") << QStringLiteral("1234567890") << int(Strength::Level::VeryWeak);
    QTest::newRow("keyboard") << QStringLiteral("qwertyuiop") << int(Strength::Level::VeryWeak);
    QTest::newRow("keyboard-row-2") << QStringLiteral("ASDFGHJKL") << int(Strength::Level::VeryWeak);
    QTest::newRow("password123") << QStringLiteral("Password123!") << int(Strength::Level::Moderate);
    QTest::newRow("random-20") << QStringLiteral("k8#Vq2!mZ7&xR4$wT9pL") << int(Strength::Level::VeryStrong);
}

void PasswordStrengthTest::patterns()
{
    QFETCH(QString, password);
    QFETCH(int, level);
    QCOMPARE(int(Strength::evaluate(password, Algorithm::AES_256).level), level);
}

void PasswordStrengthTest::classesAndLength()
{
    // The entropy grows with the length and with the classes, it never exceeds 128 bits
    // (no two neighbors of the alphabet or of a row of the keyboard)
    const int lower = Strength::evaluate(QStringLiteral("kvmhpw"), Algorithm::AES_256).bits;
    const int longer = Strength::evaluate(QStringLiteral("kvmhpwbzrf"), Algorithm::AES_256).bits;
    const int mixed = Strength::evaluate(QStringLiteral("kVmHpW"), Algorithm::AES_256).bits;
    QVERIFY(lower > 0);
    QVERIFY(longer > lower);
    QVERIFY(mixed > lower);

    // Six lower case letters: 6 * log2(26) = 28.2 bits (Weak); with both cases 6 * log2(52) = 34.2
    QCOMPARE(lower, 28);
    QCOMPARE(mixed, 34);

    const Strength::Result maximum = Strength::evaluate(QStringLiteral("k8#Vq2!mZ7&xR4$wT9pLc3^Nb6*Yh1@Gf5%Jd0"), Algorithm::AES_256);
    QCOMPARE(maximum.bits, Strength::MAXIMUM_BITS);
    QCOMPARE(maximum.level, Strength::Level::VeryStrong);
}

QTEST_GUILESS_MAIN(PasswordStrengthTest)

#include "tst_passwordstrengthtest.moc"
