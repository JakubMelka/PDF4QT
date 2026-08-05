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

#include <QtTest>

#include "pdfutils.h"

class AuthorSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void test_default_is_anonymous();
    void test_system_user_name();
    void test_custom_name();
    void test_empty_custom_name_is_anonymous();
};

void AuthorSettingsTest::test_default_is_anonymous()
{
    // Author name must not disclose the system user name, unless the user
    // explicitly allows it (see issue #412)
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorNameMode(), pdf::PDFAuthorSettings::AuthorNameMode::Anonymous);
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorName(), pdf::PDFAuthorSettings::getAnonymousAuthorName());
}

void AuthorSettingsTest::test_system_user_name()
{
    pdf::PDFAuthorSettings::setAuthorName(pdf::PDFAuthorSettings::AuthorNameMode::SystemUserName, QString());

    const QString systemUserName = pdf::PDFSysUtils::getUserName();
    const QString expectedAuthorName = systemUserName.isEmpty() ? pdf::PDFAuthorSettings::getAnonymousAuthorName() : systemUserName;
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorName(), expectedAuthorName);

    pdf::PDFAuthorSettings::setAuthorName(pdf::PDFAuthorSettings::AuthorNameMode::Anonymous, QString());
}

void AuthorSettingsTest::test_custom_name()
{
    pdf::PDFAuthorSettings::setAuthorName(pdf::PDFAuthorSettings::AuthorNameMode::CustomName, "  Reviewer  ");
    QCOMPARE(pdf::PDFAuthorSettings::getCustomAuthorName(), QString("  Reviewer  "));
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorName(), QString("Reviewer"));

    pdf::PDFAuthorSettings::setAuthorName(pdf::PDFAuthorSettings::AuthorNameMode::Anonymous, QString());
}

void AuthorSettingsTest::test_empty_custom_name_is_anonymous()
{
    pdf::PDFAuthorSettings::setAuthorName(pdf::PDFAuthorSettings::AuthorNameMode::CustomName, "   ");
    QCOMPARE(pdf::PDFAuthorSettings::getAuthorName(), pdf::PDFAuthorSettings::getAnonymousAuthorName());

    pdf::PDFAuthorSettings::setAuthorName(pdf::PDFAuthorSettings::AuthorNameMode::Anonymous, QString());
}

QTEST_MAIN(AuthorSettingsTest)

#include "tst_authorsettingstest.moc"
