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

#ifndef APPTOOLENCRYPTION_H
#define APPTOOLENCRYPTION_H

// The GUI of the tools of the encryption (UnitTestsAppToolEncryption, step 5 of phase 2, plan 9 with
// R5-13): package P2 (Protect with Password, Restrict Permissions, Remove Password) writes
// tst_apptoolencryptiontest.cpp, package P3 (Encrypt with Certificate) writes the second source
// tst_apptoolencryptiontest_certificate.cpp. Each package registers its cases by its registration
// function; the test runs every case as a row of the function "run" (a case creates its own
// fixture), so a case runs alone by "UnitTestsAppToolEncryption run:<name>". The skeleton fixes only
// this header.

#include <QString>

#include <functional>
#include <vector>

/// A case of the GUI of the encryption: QVERIFY / QCOMPARE inside of \p run end the case (the row fails)
class AppToolEncryptionCase
{
public:
    QString name;               ///< Name of the row ("password-weak"), unique
    std::function<void()> run;  ///< The case with its own fixture
};

/// The cases of the packages of step 5
class AppToolEncryptionPackages
{
public:
    AppToolEncryptionPackages() = delete;

    /// Every registered case, P2 first
    static const std::vector<AppToolEncryptionCase>& getCases();

    /// Package P2 (protect.password, protect.permissions, protect.unlock): tst_apptoolencryptiontest.cpp
    static void registerPasswordCases(std::vector<AppToolEncryptionCase>* cases);

    /// Package P3 (protect.certificate-encrypt, the list of the certificates):
    /// tst_apptoolencryptiontest_certificate.cpp
    static void registerCertificateCases(std::vector<AppToolEncryptionCase>* cases);
};

#endif // APPTOOLENCRYPTION_H
