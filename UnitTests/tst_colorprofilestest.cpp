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

// The search of the installed ICC profiles (owner 18. 11. 2026, step 7c of phase 2): the folders of Linux by the
// ICC Profiles in X / colord and the XDG Base Directory specification, the folders of ColorSync of macOS, none on
// Windows (its color system lists them) - a pure function, so both lists are checked on every platform; the walk
// of the folders with its depth limit, its one visited set (a folder or a file reached twice is read once, a cycle
// of symbolic links ends) and its cap of files.

#include "pdfcms.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace pdf;

/// Helpers of the test
class ColorProfilesTestHelper
{
public:
    ColorProfilesTestHelper() = delete;

    /// Creates an empty file (the walk lists the files by their names, it does not read them)
    static bool createFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QFile::WriteOnly | QFile::Truncate);
    }
};

class ColorProfilesTest : public QObject
{
    Q_OBJECT

private slots:
    void systemDirectoriesLinux();
    void systemDirectoriesLinuxEnvironment();
    void systemDirectoriesMacOS();
    void systemDirectoriesWindows();
    void currentPlatform();
    void walkDepthAndFilters();
    void walkVisitedOnce();
    void walkLimitsAndMissingFolders();
    void walkSymbolicLinkCycle();
};

void ColorProfilesTest::systemDirectoriesLinux()
{
    using Platform = PDFCMSManager::ProfilePlatform;

    // The defaults of the XDG Base Directory specification: $XDG_DATA_HOME = ~/.local/share, $XDG_DATA_DIRS =
    // /usr/local/share:/usr/share; /usr/share/color/icc of the system (ICC Profiles in X) is listed once
    const QStringList expected = { QStringLiteral("/usr/share/color/icc"), QStringLiteral("/home/user/.local/share/icc"), QStringLiteral("/usr/local/share/color/icc"),
                                   QStringLiteral("/home/user/.color/icc"), QStringLiteral("/var/lib/colord/icc") };
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::Linux, QStringLiteral("/home/user"), QString(), QString()), expected);
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::Linux, QStringLiteral("/home/user/"), QString(), QString()), expected);

    // Without a home folder: only the folders of the system
    const QStringList system = { QStringLiteral("/usr/share/color/icc"), QStringLiteral("/usr/local/share/color/icc"), QStringLiteral("/var/lib/colord/icc") };
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::Linux, QString(), QString(), QString()), system);
}

void ColorProfilesTest::systemDirectoriesLinuxEnvironment()
{
    using Platform = PDFCMSManager::ProfilePlatform;

    // $XDG_DATA_HOME and $XDG_DATA_DIRS given: a relative entry and an empty one are ignored (the specification: the
    // paths must be absolute), a trailing slash is cleaned, /usr/share/color/icc stays once at its first place
    const QStringList expected = { QStringLiteral("/usr/share/color/icc"), QStringLiteral("/data/user/icc"), QStringLiteral("/opt/share/color/icc"),
                                   QStringLiteral("/home/user/.color/icc"), QStringLiteral("/var/lib/colord/icc") };
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::Linux, QStringLiteral("/home/user"), QStringLiteral("/data/user/"),
                                                        QStringLiteral("/opt/share:relative/share::/usr/share/")), expected);

    // A relative $XDG_DATA_HOME is invalid: the default ~/.local/share
    const QStringList relativeHome = { QStringLiteral("/usr/share/color/icc"), QStringLiteral("/home/user/.local/share/icc"), QStringLiteral("/opt/share/color/icc"),
                                       QStringLiteral("/home/user/.color/icc"), QStringLiteral("/var/lib/colord/icc") };
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::Linux, QStringLiteral("/home/user"), QStringLiteral("data"), QStringLiteral("/opt/share")), relativeHome);
}

void ColorProfilesTest::systemDirectoriesMacOS()
{
    using Platform = PDFCMSManager::ProfilePlatform;

    // The folders of ColorSync: of the user, of the computer, of the system, of the network; the XDG variables mean
    // nothing on macOS
    const QStringList expected = { QStringLiteral("/Users/jan/Library/ColorSync/Profiles"), QStringLiteral("/Library/ColorSync/Profiles"),
                                   QStringLiteral("/System/Library/ColorSync/Profiles"), QStringLiteral("/Network/Library/ColorSync/Profiles") };
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::MacOS, QStringLiteral("/Users/jan"), QStringLiteral("/x"), QStringLiteral("/y")), expected);
    QCOMPARE(PDFCMSManager::getSystemProfileDirectories(Platform::MacOS, QString(), QString(), QString()), expected.mid(1));
}

void ColorProfilesTest::systemDirectoriesWindows()
{
    // Windows lists its profiles by its color system, no folder is walked
    QVERIFY(PDFCMSManager::getSystemProfileDirectories(PDFCMSManager::ProfilePlatform::Windows, QStringLiteral("C:/Users/jan"), QString(), QString()).isEmpty());
}

void ColorProfilesTest::currentPlatform()
{
    // macOS is checked before the other Unix systems (Q_OS_UNIX is defined on macOS too)
#if defined(Q_OS_WIN)
    QCOMPARE(PDFCMSManager::getCurrentProfilePlatform(), PDFCMSManager::ProfilePlatform::Windows);
#elif defined(Q_OS_MACOS)
    QCOMPARE(PDFCMSManager::getCurrentProfilePlatform(), PDFCMSManager::ProfilePlatform::MacOS);
#else
    QCOMPARE(PDFCMSManager::getCurrentProfilePlatform(), PDFCMSManager::ProfilePlatform::Linux);
#endif
}

void ColorProfilesTest::walkDepthAndFilters()
{
    using H = ColorProfilesTestHelper;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("sub/deep/deeper")));
    for (const char* file : { "a.icc", "b.ICM", "c.txt", "sub/d.icc", "sub/deep/e.icc", "sub/deep/deeper/f.icc" })
    {
        QVERIFY(H::createFile(root.filePath(QString::fromLatin1(file))));
    }
    const QStringList filters = { QStringLiteral("*.icc"), QStringLiteral("*.icm") };
    const QStringList directories = { root.path() };
    auto path = [&root](const char* file) { return root.absoluteFilePath(QString::fromLatin1(file)); };

    // Depth 0 = the files of the folder itself; the filters ignore the case (b.ICM), other files are not listed;
    // breadth first: the files of a folder before the files of its subfolders
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, filters, 0, 100), QStringList({ path("a.icc"), path("b.ICM") }));
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, filters, 1, 100), QStringList({ path("a.icc"), path("b.ICM"), path("sub/d.icc") }));
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, filters, 3, 100),
             QStringList({ path("a.icc"), path("b.ICM"), path("sub/d.icc"), path("sub/deep/e.icc"), path("sub/deep/deeper/f.icc") }));
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, QStringList({ QStringLiteral("*.icc") }), 0, 100), QStringList({ path("a.icc") }));
}

void ColorProfilesTest::walkVisitedOnce()
{
    using H = ColorProfilesTestHelper;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("sub")));
    QVERIFY(H::createFile(root.filePath(QStringLiteral("a.icc"))));
    QVERIFY(H::createFile(root.filePath(QStringLiteral("sub/d.icc"))));
    const QStringList filters = { QStringLiteral("*.icc") };
    auto path = [&root](const char* file) { return root.absoluteFilePath(QString::fromLatin1(file)); };

    // One visited set for the whole walk: a folder given twice, by another spelling, and a subfolder given as a
    // folder of its own and reached from its parent are read once - every file once, by its first path
    const QStringList directories = { root.path(), root.path(), root.filePath(QStringLiteral("sub/..")), root.filePath(QStringLiteral("sub")) };
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, filters, 1, 100), QStringList({ path("a.icc"), path("sub/d.icc") }));
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, filters, PDFCMSManager::MAXIMUM_PROFILE_DIRECTORY_DEPTH, 100), QStringList({ path("a.icc"), path("sub/d.icc") }));
}

void ColorProfilesTest::walkLimitsAndMissingFolders()
{
    using H = ColorProfilesTestHelper;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root(directory.path());
    for (const char* file : { "a.icc", "b.icc", "c.icc" })
    {
        QVERIFY(H::createFile(root.filePath(QString::fromLatin1(file))));
    }
    auto path = [&root](const char* file) { return root.absoluteFilePath(QString::fromLatin1(file)); };
    const QStringList filters = { QStringLiteral("*.icc") };

    // At most the given number of files (each one is opened and parsed later); a missing folder, a file given as a
    // folder and an empty path are skipped
    QCOMPARE(PDFCMSManager::findProfileFiles(QStringList({ root.path() }), filters, 0, 2), QStringList({ path("a.icc"), path("b.icc") }));
    QVERIFY(PDFCMSManager::findProfileFiles(QStringList({ root.path() }), filters, 0, 0).isEmpty());
    const QStringList directories = { QString(), root.filePath(QStringLiteral("missing")), path("a.icc"), root.path() };
    QCOMPARE(PDFCMSManager::findProfileFiles(directories, filters, 2, 100), QStringList({ path("a.icc"), path("b.icc"), path("c.icc") }));
}

void ColorProfilesTest::walkSymbolicLinkCycle()
{
#if defined(Q_OS_WIN)
    // QFile::link makes a shortcut (.lnk) on Windows, not a link of the file system: no cycle can be made here
    QSKIP("A cycle of symbolic links needs a Unix file system (the walk is checked by walkVisitedOnce here).");
#else
    using H = ColorProfilesTestHelper;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("sub")));
    QVERIFY(H::createFile(root.filePath(QStringLiteral("a.icc"))));
    QVERIFY(H::createFile(root.filePath(QStringLiteral("sub/d.icc"))));

    // sub/loop -> the root and sub/same.icc -> sub/d.icc: the walk ends (the root is visited once) and the file
    // reached by the link is listed once, by its first path
    QVERIFY(QFile::link(root.path(), root.filePath(QStringLiteral("sub/loop"))));
    QVERIFY(QFile::link(root.filePath(QStringLiteral("sub/d.icc")), root.filePath(QStringLiteral("sub/same.icc"))));
    const QStringList files = PDFCMSManager::findProfileFiles(QStringList({ root.path() }), QStringList({ QStringLiteral("*.icc") }), PDFCMSManager::MAXIMUM_PROFILE_DIRECTORY_DEPTH, 100);
    QCOMPARE(files, QStringList({ root.absoluteFilePath(QStringLiteral("a.icc")), root.absoluteFilePath(QStringLiteral("sub/d.icc")) }));
#endif
}

QTEST_GUILESS_MAIN(ColorProfilesTest)

#include "tst_colorprofilestest.moc"
