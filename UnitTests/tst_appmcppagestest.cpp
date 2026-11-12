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

// The MCP actions of the tools of step 3a of phase 2 over HTTP (plan 7, 8): the cases of the
// packages (appmcppages.h), each a row of the function "action"

#include "appmcppages.h"

#include "pdfappbuiltintools.h"

#include <QApplication>
#include <QSet>
#include <QtTest>

class AppMcpPagesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void casesHaveUniqueNames();
    void action_data();
    void action();
};

void AppMcpPagesTest::initTestCase()
{
    // The tools of the application, as the controller of MCP sees them
    pdf::PDFAppBuiltinTools::registerAll();
}

void AppMcpPagesTest::casesHaveUniqueNames()
{
    // A row is selected by its name ("action:<name>", one process per row): every case has its
    // own non-empty name
    QSet<QString> names;
    for (const AppMcpPagesCase& item : AppMcpPagesPackages::getCases())
    {
        QVERIFY(!item.name.isEmpty());
        QVERIFY2(!names.contains(item.name), qPrintable(item.name));
        QVERIFY2(item.run != nullptr, qPrintable(item.name));
        names.insert(item.name);
    }
}

void AppMcpPagesTest::action_data()
{
    QTest::addColumn<QString>("name");
    for (const AppMcpPagesCase& item : AppMcpPagesPackages::getCases())
    {
        QTest::newRow(qPrintable(item.name)) << item.name;
    }
    if (AppMcpPagesPackages::getCases().empty())
    {
        // No package has registered its actions yet (the skeleton of step 3a)
        QTest::newRow("none") << QString();
    }
}

void AppMcpPagesTest::action()
{
    QFETCH(QString, name);
    if (name.isEmpty())
    {
        QSKIP("No package of step 3a has registered its MCP actions yet");
    }

    for (const AppMcpPagesCase& item : AppMcpPagesPackages::getCases())
    {
        if (item.name == name)
        {
            pdftest::AppMcpFixture fixture;
            item.run(fixture);
            return;
        }
    }
    QFAIL(qPrintable(QStringLiteral("unknown case %1").arg(name)));
}

const std::vector<AppMcpPagesCase>& AppMcpPagesPackages::getCases()
{
    static const std::vector<AppMcpPagesCase> cases = []()
    {
        std::vector<AppMcpPagesCase> result;
        registerSplitCases(&result);
        registerGeometryCases(&result);
        registerInsertCases(&result);
        registerImageCases(&result);
        registerManageCases(&result);
        return result;
    }();
    return cases;
}

int main(int argc, char* argv[])
{
    // Child processes of the tests of two instances (AppMcpFixture::secondInstance)
    const int childResult = pdftest::AppMcpFixture::runChild(argc, argv);
    if (childResult >= 0)
    {
        return childResult;
    }

    QApplication application(argc, argv);
    application.setAttribute(Qt::AA_Use96Dpi, true);
    AppMcpPagesTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcppagestest.moc"
