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

// The assembly files of Manage Pages in the application (step 3b of phase 2, package M4; plan 4.3,
// 4.6, 6, 8, RB8, RB12): the suffix of an assembly file (a .pagemaster file is none, A6), the
// migration 2 of the database (the table recent_assembly with the triggers of the change log;
// migration 1 is released and stays without it, newapp_storage.md 5), the recent assemblies
// (409:32023: pinned first, the newest, at most 20 others; the folder in recent_folder, RB12;
// hostile rows), the loader (387:16980: Found, Changed "now 5 pages instead of 6", Missing "Not
// found in C:\Tender", a source of another computer never touched - RB8, the relative path of a
// moved assembly, Locate…, Use Changed File and Open Without Them by createContent - the bin,
// B21; the cancellation after each megabyte of a hash, a deleted loader delivers nothing; the
// limits of the file) and the opening of an assembly file by the window (RB3 e).

#include "appdocumentfixture.h"

#include "pdfappassemblyloader.h"
#include "pdfappcontext.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfappopenpage.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptooltab.h"
#include "pdfdatabase.h"
#include "pdfdatabaseschema.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfrecentassemblyrepository.h"
#include "pdfrecentrepository.h"
#include "pdfrepositoryhelper.h"

#include "pdfdocumentassemblyfile.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfoperationcontrol.h"

#include "pdfcomponentrows.h"

#include <QAccessible>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSet>
#include <QThreadPool>
#include <QTimeZone>
#include <QtTest>

#include <atomic>

/// Helpers of the tests of the assembly files
class AppAssemblyTestHelper
{
public:
    AppAssemblyTestHelper() = delete;

    static QByteArray readFile(const QString& filePath)
    {
        QFile file(filePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static bool writeFile(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
    }

    /// A PDF source of the file as Manage Pages records it (size, time, SHA-256, pages)
    static pdf::PDFDocumentAssembly::Source createSource(const QString& id, const QString& filePath, pdf::PDFInteger pageCount)
    {
        const QFileInfo info(filePath);
        pdf::PDFDocumentAssembly::Source source;
        source.id = id;
        source.kind = pdf::PDFDocumentAssembly::SourceKind::Pdf;
        source.name = info.fileName();
        source.path = info.absoluteFilePath();
        source.size = info.size();
        source.modified = info.lastModified(QTimeZone::UTC);
        source.sha256 = QCryptographicHash::hash(readFile(filePath), QCryptographicHash::Sha256);
        source.pageCount = pageCount;
        return source;
    }

    /// An item of the pages \p pageIndices (from 0) of the source of the index \p input
    static pdf::PDFDocumentAssembly::Item createItem(const QString& id, int input, const std::vector<pdf::PDFInteger>& pageIndices, const QString& name = QString())
    {
        pdf::PDFDocumentAssembly::Item item;
        item.id = id;
        item.name = name;
        item.isCreatedGroup = !name.isEmpty();
        for (const pdf::PDFInteger pageIndex : pageIndices)
        {
            pdf::PDFDocumentAssembly::Page page;
            page.plan.input = input;
            page.plan.pageIndex = pageIndex;
            item.pages.push_back(page);
        }
        return item;
    }

    /// One item per page of the source of the index \p input ("<prefix>1", ...)
    static std::vector<pdf::PDFDocumentAssembly::Item> createPageItems(const QString& prefix, int input, pdf::PDFInteger pageCount)
    {
        std::vector<pdf::PDFDocumentAssembly::Item> items;
        for (pdf::PDFInteger i = 0; i < pageCount; ++i)
        {
            items.push_back(createItem(QStringLiteral("%1%2").arg(prefix).arg(i + 1), input, { i }));
        }
        return items;
    }

    static pdf::PDFDocumentAssembly createAssembly(std::vector<pdf::PDFDocumentAssembly::Source> sources, std::vector<pdf::PDFDocumentAssembly::Item> items)
    {
        QString error;
        pdf::PDFDocumentAssembly assembly = pdf::PDFDocumentAssembly::create(std::move(sources), std::move(items), { }, &error);
        return error.isEmpty() ? assembly : pdf::PDFDocumentAssembly();
    }

    /// Writes the assembly file (PDFDocumentAssemblyFile, the core of M1)
    static bool writeAssembly(const QString& filePath, const pdf::PDFDocumentAssembly& assembly)
    {
        pdf::PDFDocumentAssemblyFile::Content content;
        content.assembly = assembly;
        return writeFile(filePath, pdf::PDFDocumentAssemblyFile::write(content, QFileInfo(filePath).absolutePath()));
    }

    /// Edits the relative path of a source in an assembly file, as an attacker or another writer
    /// would (the writer computes it from the path itself)
    static bool setRelativePath(const QString& filePath, int sourceIndex, const QString& relativePath)
    {
        QJsonObject root = QJsonDocument::fromJson(readFile(filePath)).object();
        QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
        if (sourceIndex >= sources.size())
        {
            return false;
        }
        QJsonObject source = sources.at(sourceIndex).toObject();
        source.insert(QStringLiteral("relativePath"), relativePath);
        sources.replace(sourceIndex, source);
        root.insert(QStringLiteral("sources"), sources);
        return writeFile(filePath, QJsonDocument(root).toJson());
    }

    static const pdf::PDFAppAssemblySourceStatus* findStatus(const pdf::PDFAppAssemblyLoadResult& result, const QString& sourceId)
    {
        for (const pdf::PDFAppAssemblySourceStatus& status : result.sources)
        {
            if (status.sourceId == sourceId)
            {
                return &status;
            }
        }
        return nullptr;
    }

    /// The pages of the items as "input:page" from 1 ("0:1 0:2 1:1")
    static QString describePages(const std::vector<pdf::PDFDocumentAssembly::Item>& items)
    {
        QStringList pages;
        for (const pdf::PDFDocumentAssembly::Item& item : items)
        {
            for (const pdf::PDFDocumentAssembly::Page& page : item.pages)
            {
                pages << QStringLiteral("%1:%2").arg(page.plan.input).arg(page.plan.pageIndex + 1);
            }
        }
        return pages.join(QLatin1Char(' '));
    }

    /// The canonical path as the repositories store it
    static QString stored(const QString& filePath)
    {
        return pdf::PDFRepositoryHelper::normalizePath(filePath);
    }
};

/// A cancellation, which answers "canceled" from its call \p cancelAt on (the job asks after the
/// reading of the file, before each source and after each block of a hash)
class AppAssemblyCountingControl : public pdf::PDFOperationControl
{
public:
    explicit AppAssemblyCountingControl(int cancelAt) : m_cancelAt(cancelAt) { }

    virtual bool isOperationCancelled() const override { return ++m_calls >= m_cancelAt; }

    int getCalls() const { return m_calls.load(); }

private:
    int m_cancelAt;
    mutable std::atomic_int m_calls = 0;
};

class AppAssemblyTest : public QObject
{
    Q_OBJECT

private slots:
    void assemblyFileSuffix();
    void recentAssemblyMigration();
    void recentAssemblies();
    void recentAssembliesLimit();
    void recentAssembliesHostileRows();
    void loaderFoundAndRelativePath();
    void loaderChangedSource();
    void loaderMissingAndOtherComputer();
    void loaderLocate();
    void loaderRefusesFiles();
    void loaderCancel();
    void loaderAsynchronous();
    void createContentKeepsGroups();
    void requestedFile();
    void openAssemblyFile();
    void openAssemblyByDropAndDialog();
    void openPageRecentAssemblies();
};

void AppAssemblyTest::assemblyFileSuffix()
{
    QVERIFY(pdf::PDFAppAssemblyLoader::isAssemblyFile(QStringLiteral("C:/Tender/Tender_2026.pdf4qtassembly")));
    QVERIFY(pdf::PDFAppAssemblyLoader::isAssemblyFile(QStringLiteral("/home/user/TENDER.PDF4QTASSEMBLY")));
    QVERIFY(!pdf::PDFAppAssemblyLoader::isAssemblyFile(QStringLiteral("C:/Tender/Contract.pdf")));
    QVERIFY(!pdf::PDFAppAssemblyLoader::isAssemblyFile(QStringLiteral("C:/Tender/Tender.pdf4qtassembly.pdf")));
    QVERIFY(!pdf::PDFAppAssemblyLoader::isAssemblyFile(QStringLiteral("C:/Tender/Tender.pagemaster")));
    QVERIFY(!pdf::PDFAppAssemblyLoader::isAssemblyFile(QString()));
}

void AppAssemblyTest::recentAssemblyMigration()
{
    // Migration 2 adds recent_assembly (plan 4.6); the SQL of the released migration 1 is the same
    // as before - none of its statements names the new table
    const std::vector<pdf::PDFDatabaseMigration> migrations = pdf::PDFDatabaseSchema::getMigrations();
    QVERIFY(pdf::PDFDatabaseSchema::isValid(migrations));
    QCOMPARE(migrations.size(), size_t(2));
    QCOMPARE(pdf::PDFDatabaseSchema::getCurrentVersion(), 2);
    QVERIFY(!migrations[0].sql.contains("recent_assembly"));
    QVERIFY(migrations[1].sql.contains("CREATE TABLE recent_assembly("));
    QVERIFY(migrations[1].sql.contains("CREATE TRIGGER recent_assembly_log_insert"));
    QVERIFY(!migrations[1].sql.contains("recent_file"));

    // The notifications: the table is a topic of the change log of every version, of version 2 only
    QStringList allTables;
    for (const pdf::PDFDatabaseTableInfo& table : pdf::PDFDatabaseSchema::getTables())
    {
        allTables << table.name;
    }
    QStringList version1Tables;
    for (const pdf::PDFDatabaseTableInfo& table : pdf::PDFDatabaseSchema::getTables(1))
    {
        version1Tables << table.name;
    }
    const std::vector<pdf::PDFDatabaseTableInfo> version2Tables = pdf::PDFDatabaseSchema::getTables(2);
    QCOMPARE(version2Tables.size(), size_t(1));
    QCOMPARE(version2Tables.front().name, QStringLiteral("recent_assembly"));
    QCOMPARE(version2Tables.front().keyExpression, QStringLiteral("%1.path"));
    QVERIFY(allTables.contains(QStringLiteral("recent_assembly")));
    QVERIFY(!version1Tables.contains(QStringLiteral("recent_assembly")));
    QCOMPARE(allTables.size(), version1Tables.size() + 1);
    QVERIFY(pdf::PDFDatabaseSchema::getTables(3).empty());
}

void AppAssemblyTest::recentAssemblies()
{
    using H = AppAssemblyTestHelper;

    // 409:32023 "Recent assemblies": the name (the file without its suffix), the folder, the date and
    // the pages; pinned first, then the newest; the folder is a recent folder of the application
    // (RB12: the table recent_folder)
    pdftest::AppDocumentFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    QVERIFY(database);
    pdf::PDFRecentAssemblyRepository repository(database);
    QVERIFY(repository.getAssemblies().empty());

    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("Tender")));
    const QString tender = fixture.path(QStringLiteral("Tender/Tender_2026.pdf4qtassembly"));
    const QString annual = fixture.path(QStringLiteral("Annual.report.pdf4qtassembly"));
    QVERIFY(H::writeFile(tender, "{}"));
    QVERIFY(H::writeFile(annual, "{}"));

    repository.setCurrentTime(1000);
    QVERIFY(!repository.recordOpened(tender, 17).isError());
    repository.setCurrentTime(2000);
    QVERIFY(!repository.recordOpened(annual, -1).isError());

    std::vector<pdf::PDFRecentAssembly> assemblies = repository.getAssemblies();
    QCOMPARE(assemblies.size(), size_t(2));
    QCOMPARE(assemblies[0].path, H::stored(annual));
    QCOMPARE(assemblies[0].name, QStringLiteral("Annual.report"));
    QCOMPARE(assemblies[0].pageCount, -1);
    QCOMPARE(assemblies[0].lastOpened, qint64(2000));
    QCOMPARE(assemblies[1].path, H::stored(tender));
    QCOMPARE(assemblies[1].name, QStringLiteral("Tender_2026"));
    QCOMPARE(assemblies[1].folder, H::stored(fixture.path(QStringLiteral("Tender"))));
    QCOMPARE(assemblies[1].pageCount, 17);
    QVERIFY(!assemblies[1].pinned);

    // An opening, whose pages are not known yet (-1, the window before the GUI reads the file),
    // keeps the count; a known count replaces it
    repository.setCurrentTime(3000);
    QVERIFY(!repository.recordOpened(tender, -1).isError());
    assemblies = repository.getAssemblies();
    QCOMPARE(assemblies[0].path, H::stored(tender));
    QCOMPARE(assemblies[0].pageCount, 17);
    QCOMPARE(assemblies[0].lastOpened, qint64(3000));
    QVERIFY(!repository.recordOpened(tender, 12).isError());
    QCOMPARE(repository.getAssemblies()[0].pageCount, 12);

    // Pinned first, whatever its time
    QVERIFY(!repository.setPinned(H::stored(annual), true).isError());
    assemblies = repository.getAssemblies();
    QCOMPARE(assemblies[0].path, H::stored(annual));
    QVERIFY(assemblies[0].pinned);
    QCOMPARE(repository.getAssemblies(1).size(), size_t(1));

    // The folders of the files are recent folders
    QStringList folders;
    for (const pdf::PDFRecentFolder& folder : pdf::PDFRecentRepository(database, nullptr).getUsedFolders())
    {
        folders << folder.path;
    }
    QVERIFY2(folders.contains(H::stored(fixture.path(QStringLiteral("Tender")))), qPrintable(folders.join(QLatin1Char(';'))));

    // Remove from the list; the operations on an unknown or invalid path are errors, nothing changes
    QVERIFY(!repository.remove(H::stored(tender)).isError());
    QCOMPARE(repository.getAssemblies().size(), size_t(1));
    QVERIFY(repository.setPinned(H::stored(tender), true).isError());
    QVERIFY(repository.recordOpened(QStringLiteral("relative/Tender.pdf4qtassembly"), 3).isError());
    QVERIFY(repository.recordOpened(annual, -2).isError());
    // Owner 2026-11-12 (plan of step 3b, 13 point 3): an assembly has at most 65 536 pages
    QVERIFY(repository.recordOpened(annual, 65537).isError());
    QCOMPARE(repository.getAssemblies().size(), size_t(1));
    QVERIFY(!repository.recordOpened(annual, 65536).isError());
    QCOMPARE(repository.getAssemblies().size(), size_t(1));
    QCOMPARE(repository.getAssemblies().front().path, H::stored(annual));
    QCOMPARE(repository.getAssemblies().front().pageCount, 65536);
}

void AppAssemblyTest::recentAssembliesLimit()
{
    using H = AppAssemblyTestHelper;

    // At most 20 recent assemblies besides the pinned ones (PDFRecentAssemblyRepository): the oldest
    // non-pinned one goes, a pinned one stays whatever its age; an unpinned one is pruned again
    pdftest::AppDocumentFixture fixture;
    pdf::PDFRecentAssemblyRepository repository(fixture.context->getDatabase());
    QStringList paths;
    for (int i = 0; i < 22; ++i)
    {
        const QString path = fixture.path(QStringLiteral("a%1.pdf4qtassembly").arg(i));
        QVERIFY(H::writeFile(path, "{}"));
        paths << H::stored(path);
        repository.setCurrentTime(1000 + i);
        QVERIFY(!repository.recordOpened(path, i).isError());
        if (i == 0)
        {
            QVERIFY(!repository.setPinned(paths.front(), true).isError());
        }
    }

    // 1 pinned (the oldest) + the 20 newest; a1 is the oldest non-pinned one and is gone
    std::vector<pdf::PDFRecentAssembly> assemblies = repository.getAssemblies();
    QCOMPARE(assemblies.size(), size_t(21));
    QCOMPARE(assemblies.front().path, paths[0]);
    QCOMPARE(assemblies[1].path, paths[21]);
    QCOMPARE(assemblies.back().path, paths[2]);
    for (const pdf::PDFRecentAssembly& assembly : assemblies)
    {
        QVERIFY(assembly.path != paths[1]);
    }

    // The oldest one unpinned: 21 non-pinned rows, it goes
    QVERIFY(!repository.setPinned(paths[0], false).isError());
    assemblies = repository.getAssemblies();
    QCOMPARE(assemblies.size(), size_t(20));
    QCOMPARE(assemblies.back().path, paths[2]);
}

void AppAssemblyTest::recentAssembliesHostileRows()
{
    using H = AppAssemblyTestHelper;

    // Rows of another version or of a damaged file are untrusted: a relative path, a NUL, a device
    // path are skipped; a value of a wrong type is its default (a text time, a negative or a huge
    // page count, a text pin); a missing name is the name of the file
    pdftest::AppDocumentFixture fixture;
    pdf::PDFDatabase* database = fixture.context->getDatabase();
    pdf::PDFDatabaseConnection* connection = database->getConnection();
    QVERIFY(connection);
    const QString valid = fixture.path(QStringLiteral("Valid.pdf4qtassembly"));
    QVERIFY(H::writeFile(valid, "{}"));
    pdf::PDFRecentAssemblyRepository repository(database);
    repository.setCurrentTime(5000);
    QVERIFY(!repository.recordOpened(valid, 4).isError());

    // The hostile rows are absolute on every system ("C:/hostile" on Windows, "/hostile" elsewhere):
    // their values decide, not a drive letter, which is a relative path on another system
    const QString hostile = QDir::rootPath() + QStringLiteral("hostile");
    const QString huge = hostile + QStringLiteral("/huge.pdf4qtassembly");
    const QString negative = hostile + QStringLiteral("/negative.pdf4qtassembly");
    const QByteArray rows = QStringLiteral("INSERT INTO recent_assembly(path, name, folder, last_opened, page_count, pinned) VALUES "
                                           "('relative/x.pdf4qtassembly', 'x', 'relative', 9000, 3, 1), "
                                           "('//./PhysicalDrive0', 'disk', '//.', 9000, 3, 0), "
                                           "('%1', '', '%2', 'yesterday', 99999999999, 'yes'), "
                                           "('%3', 'Negative', '%2', -5, -3, 0)").arg(huge, hostile, negative).toUtf8();
    QVERIFY(!connection->execute(rows.constData()).isError());
    const QByteArray root = QDir::rootPath().toUtf8();
    const QByteArray nulRow = "INSERT INTO recent_assembly(path, name, folder, last_opened, page_count, pinned) VALUES (x'" + (root + QByteArray(1, '\0')).toHex() + "', 'nul', '" + root + "', 1, 1, 0)";
    QVERIFY(!connection->execute(nulRow.constData()).isError());

    const std::vector<pdf::PDFRecentAssembly> assemblies = repository.getAssemblies();
    QStringList paths;
    for (const pdf::PDFRecentAssembly& assembly : assemblies)
    {
        paths << assembly.path;
    }
    QCOMPARE(assemblies.size(), size_t(3));
    QVERIFY(paths.contains(H::stored(valid)));
    QVERIFY(paths.contains(huge));
    QVERIFY(paths.contains(negative));
    for (const pdf::PDFRecentAssembly& assembly : assemblies)
    {
        if (assembly.path == huge)
        {
            QCOMPARE(assembly.name, QStringLiteral("huge"));
            QCOMPARE(assembly.lastOpened, qint64(0));
            QCOMPARE(assembly.pageCount, -1);
            QVERIFY(!assembly.pinned);
        }
        else if (assembly.path == negative)
        {
            QCOMPARE(assembly.lastOpened, qint64(0));
            QCOMPARE(assembly.pageCount, -1);
        }
    }

    // The valid row is the newest one (the hostile times read as 0)
    QCOMPARE(assemblies.front().path, H::stored(valid));
}

void AppAssemblyTest::loaderFoundAndRelativePath()
{
    using H = AppAssemblyTestHelper;

    // Two PDF sources and an image in the file (Q11): every source Found by its absolute path; then
    // the folder is moved - the absolute paths are gone, the relative ones find the files (plan 6)
    pdftest::AppDocumentFixture fixture;
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("Tender")));
    const QString folder = fixture.path(QStringLiteral("Tender"));
    const QString contract = pdftest::AppDocumentSamples::simple(folder, QStringLiteral("Contract.pdf"), 6);
    const QString annex = pdftest::AppDocumentSamples::simple(folder, QStringLiteral("Annex_A.pdf"), 4);
    QVERIFY(!contract.isEmpty() && !annex.isEmpty());

    pdf::PDFDocumentAssembly::Source image;
    image.id = QStringLiteral("s3");
    image.kind = pdf::PDFDocumentAssembly::SourceKind::Image;
    image.name = QStringLiteral("signature_page.png");
    image.path = QStringLiteral("//server/share/signature_page.png");
    image.imageData = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
    image.imageFormat = "png";
    image.pageCount = 1;

    std::vector<pdf::PDFDocumentAssembly::Item> items = H::createPageItems(QStringLiteral("a"), 0, 6);
    items.push_back(H::createItem(QStringLiteral("b"), 1, { 0, 1, 2, 3 }, QStringLiteral("Annex")));
    items.push_back(H::createItem(QStringLiteral("c"), 2, { 0 }));
    const pdf::PDFDocumentAssembly assembly = H::createAssembly({ H::createSource(QStringLiteral("s1"), contract, 6), H::createSource(QStringLiteral("s2"), annex, 4), image }, items);
    QCOMPARE(assembly.getItems().size(), size_t(8));
    const QString assemblyPath = QDir(folder).filePath(QStringLiteral("Tender_2026.pdf4qtassembly"));
    QVERIFY(H::writeAssembly(assemblyPath, assembly));

    pdf::PDFAppAssemblyLoadResult result = pdf::PDFAppAssemblyLoader::read(assemblyPath, nullptr);
    QVERIFY2(result.content, qPrintable(result.errorMessage));
    QCOMPARE(result.sources.size(), size_t(3));
    for (const pdf::PDFAppAssemblySourceStatus& status : result.sources)
    {
        QCOMPARE(status.state, pdf::PDFAppAssemblySourceState::Found);
    }
    QCOMPARE(QFileInfo(H::findStatus(result, QStringLiteral("s1"))->filePath), QFileInfo(contract));

    // The assembly moved with its sources: Found at the new place by the relative paths
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("Moved")));
    const QString moved = fixture.path(QStringLiteral("Moved"));
    QVERIFY(QFile::rename(contract, QDir(moved).filePath(QStringLiteral("Contract.pdf"))));
    QVERIFY(QFile::rename(annex, QDir(moved).filePath(QStringLiteral("Annex_A.pdf"))));
    QVERIFY(QFile::rename(assemblyPath, QDir(moved).filePath(QStringLiteral("Tender_2026.pdf4qtassembly"))));
    result = pdf::PDFAppAssemblyLoader::read(QDir(moved).filePath(QStringLiteral("Tender_2026.pdf4qtassembly")), nullptr);
    QVERIFY2(result.content, qPrintable(result.errorMessage));
    const pdf::PDFAppAssemblySourceStatus* status = H::findStatus(result, QStringLiteral("s1"));
    QVERIFY(status);
    QCOMPARE(status->state, pdf::PDFAppAssemblySourceState::Found);
    QCOMPARE(QFileInfo(status->filePath), QFileInfo(QDir(moved).filePath(QStringLiteral("Contract.pdf"))));
    QCOMPARE(H::findStatus(result, QStringLiteral("s2"))->state, pdf::PDFAppAssemblySourceState::Found);

    // Open Assembly: the sources take their new paths, nothing goes to the bin
    QString error;
    const std::optional<pdf::PDFDocumentAssemblyFile::Content> content = pdf::PDFAppAssemblyLoader::createContent(result, &error);
    QVERIFY2(content, qPrintable(error));
    QCOMPARE(QFileInfo(content->assembly.getSources()[0].path), QFileInfo(QDir(moved).filePath(QStringLiteral("Contract.pdf"))));
    QCOMPARE(content->assembly.getItems().size(), size_t(8));
    QVERIFY(content->assembly.getRemovedItems().empty());
}

void AppAssemblyTest::loaderChangedSource()
{
    using H = AppAssemblyTestHelper;

    // 387:16980: "Changed on 2 Oct 2026 – now 5 pages instead of 6 · Page 6 is no longer available".
    // Use Changed File: the pages over the new count go to Removed pages after the page before them;
    // Open Without Them (not accepted): every page of the source goes to the bin
    pdftest::AppDocumentFixture fixture;
    const QString contract = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 6);
    const QString annex = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 2);
    std::vector<pdf::PDFDocumentAssembly::Item> items = H::createPageItems(QStringLiteral("a"), 0, 6);
    items.push_back(H::createItem(QStringLiteral("b1"), 1, { 0 }));
    const pdf::PDFDocumentAssembly assembly = H::createAssembly({ H::createSource(QStringLiteral("s1"), contract, 6), H::createSource(QStringLiteral("s2"), annex, 2) }, items);
    const QString assemblyPath = fixture.path(QStringLiteral("Tender_2026.pdf4qtassembly"));
    QVERIFY(H::writeAssembly(assemblyPath, assembly));

    // The contract now has 5 pages
    QVERIFY(QFile::remove(contract));
    QCOMPARE(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 5), contract);
    const QByteArray newHash = QCryptographicHash::hash(H::readFile(contract), QCryptographicHash::Sha256);

    pdf::PDFAppAssemblyLoadResult result = pdf::PDFAppAssemblyLoader::read(assemblyPath, nullptr);
    QVERIFY2(result.content, qPrintable(result.errorMessage));
    const pdf::PDFAppAssemblySourceStatus* status = H::findStatus(result, QStringLiteral("s1"));
    QVERIFY(status);
    QCOMPARE(status->state, pdf::PDFAppAssemblySourceState::Changed);
    QCOMPARE(status->pageCount, pdf::PDFInteger(5));
    QCOMPARE(status->sha256, newHash);
    QCOMPARE(status->size, QFileInfo(contract).size());
    QVERIFY(status->modified.isValid());
    QVERIFY(!status->isUsable());
    QCOMPARE(H::findStatus(result, QStringLiteral("s2"))->state, pdf::PDFAppAssemblySourceState::Found);

    // Open Without Them: the six pages of the contract are in the bin, the annex stays
    QString error;
    std::optional<pdf::PDFDocumentAssemblyFile::Content> content = pdf::PDFAppAssemblyLoader::createContent(result, &error);
    QVERIFY2(content, qPrintable(error));
    QCOMPARE(H::describePages(content->assembly.getItems()), QStringLiteral("1:1"));
    QCOMPARE(content->assembly.getRemovedItems().size(), size_t(6));

    // Use Changed File: page 6 goes to the bin after page 5 (B21), the source has the new file
    for (pdf::PDFAppAssemblySourceStatus& item : result.sources)
    {
        item.isAccepted = item.sourceId == QStringLiteral("s1");
    }
    content = pdf::PDFAppAssemblyLoader::createContent(result, &error);
    QVERIFY2(content, qPrintable(error));
    QCOMPARE(H::describePages(content->assembly.getItems()), QStringLiteral("0:1 0:2 0:3 0:4 0:5 1:1"));
    QCOMPARE(content->assembly.getRemovedItems().size(), size_t(1));
    const pdf::PDFDocumentAssembly::RemovedItem& removed = content->assembly.getRemovedItems().front();
    QCOMPARE(removed.item.id, QStringLiteral("a6"));
    QCOMPARE(removed.previousItemId, QStringLiteral("a5"));
    QCOMPARE(removed.index, 5);
    QCOMPARE(H::describePages({ removed.item }), QStringLiteral("0:6"));
    QCOMPARE(content->assembly.getSources()[0].pageCount, pdf::PDFInteger(5));
    QCOMPARE(content->assembly.getSources()[0].sha256, newHash);
}

void AppAssemblyTest::loaderMissingAndOtherComputer()
{
    using H = AppAssemblyTestHelper;

    // RB8: a source of another computer is never touched - OnAnotherComputer with its path, by the
    // absolute path and also by a hostile relative path; a missing local file is Missing with the
    // path of the assembly ("Not found in C:\Tender"); a network path is used, when a local
    // candidate exists (the relative path of the moved assembly)
    pdftest::AppDocumentFixture fixture;
    const QString annex = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Annex_A.pdf"), 2);

    pdf::PDFDocumentAssembly::Source remote = H::createSource(QStringLiteral("s1"), annex, 2);
#ifdef Q_OS_WIN
    remote.path = QStringLiteral("\\\\server\\share\\Tender\\Contract.pdf");
#else
    remote.path = QStringLiteral("//server/share/Tender/Contract.pdf");
#endif
    pdf::PDFDocumentAssembly::Source missing = H::createSource(QStringLiteral("s2"), annex, 2);
    missing.path = fixture.path(QStringLiteral("Gone/Missing.pdf"));
    pdf::PDFDocumentAssembly::Source local = H::createSource(QStringLiteral("s3"), annex, 2);
    local.path = QStringLiteral("//server/share/Annex_A.pdf");

    pdf::PDFDocumentAssemblyFile::Content fileContent;
    std::vector<pdf::PDFDocumentAssembly::Item> items;
    items.push_back(H::createItem(QStringLiteral("x1"), 2, { 0 }));
    items.push_back(H::createItem(QStringLiteral("r1"), 0, { 0 }));
    items.push_back(H::createItem(QStringLiteral("m1"), 1, { 0, 1 }));
    items.push_back(H::createItem(QStringLiteral("x2"), 2, { 1 }));
    fileContent.assembly = H::createAssembly({ remote, missing, local }, items);
    QCOMPARE(fileContent.assembly.getItems().size(), size_t(4));

    // The relative path of s3 is "Annex_A.pdf" (the file moved with its assembly from the share);
    // the writer gives s1 and s3 their network paths (another root)
    const QString assemblyPath = fixture.path(QStringLiteral("Remote.pdf4qtassembly"));
    QVERIFY(H::writeFile(assemblyPath, pdf::PDFDocumentAssemblyFile::write(fileContent, fixture.folder())));
    QVERIFY(H::setRelativePath(assemblyPath, 2, QStringLiteral("Annex_A.pdf")));

    QElapsedTimer timer;
    timer.start();
    pdf::PDFAppAssemblyLoadResult result = pdf::PDFAppAssemblyLoader::read(assemblyPath, nullptr);
    QVERIFY2(result.content, qPrintable(result.errorMessage));
    QVERIFY(timer.elapsed() < 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());

    const pdf::PDFAppAssemblySourceStatus* status = H::findStatus(result, QStringLiteral("s1"));
    QVERIFY(status);
    QCOMPARE(status->state, pdf::PDFAppAssemblySourceState::OnAnotherComputer);
    QCOMPARE(status->filePath, QStringLiteral("//server/share/Tender/Contract.pdf"));
    status = H::findStatus(result, QStringLiteral("s2"));
    QCOMPARE(status->state, pdf::PDFAppAssemblySourceState::Missing);
    QCOMPARE(QDir::cleanPath(status->filePath), QDir::cleanPath(missing.path));
    status = H::findStatus(result, QStringLiteral("s3"));
    QCOMPARE(status->state, pdf::PDFAppAssemblySourceState::Found);
    QCOMPARE(QFileInfo(status->filePath), QFileInfo(annex));

    // A hostile relative path, which is itself a path of another computer, is never touched either
    // (the writer computes "Gone/Hostile.pdf"; the file is edited as an attacker would)
    pdf::PDFDocumentAssembly::Source hostile = H::createSource(QStringLiteral("s9"), annex, 2);
    hostile.path = fixture.path(QStringLiteral("Gone/Hostile.pdf"));
    pdf::PDFDocumentAssemblyFile::Content hostileContent;
    hostileContent.assembly = H::createAssembly({ hostile }, { H::createItem(QStringLiteral("h1"), 0, { 0 }) });
    const QString hostilePath = fixture.path(QStringLiteral("Hostile.pdf4qtassembly"));
    QVERIFY(H::writeFile(hostilePath, pdf::PDFDocumentAssemblyFile::write(hostileContent, fixture.folder())));
    QVERIFY(H::setRelativePath(hostilePath, 0, QStringLiteral("//evil/share/x.pdf")));
    const pdf::PDFAppAssemblyLoadResult hostileResult = pdf::PDFAppAssemblyLoader::read(hostilePath, nullptr);
    QVERIFY2(hostileResult.content, qPrintable(hostileResult.errorMessage));
    QCOMPARE(H::findStatus(hostileResult, QStringLiteral("s9"))->state, pdf::PDFAppAssemblySourceState::OnAnotherComputer);
    QCOMPARE(H::findStatus(hostileResult, QStringLiteral("s9"))->filePath, QStringLiteral("//evil/share/x.pdf"));

    // Locate… of a source, which the assembly has not, changes nothing
    QVERIFY(pdf::PDFAppAssemblyLoader::readLocated(hostileResult, QStringLiteral("unknown"), annex, nullptr).sources.size() == 1);

    // Open Without Them: the pages of s1 and s2 go to the bin after the item before them
    QString error;
    const std::optional<pdf::PDFDocumentAssemblyFile::Content> content = pdf::PDFAppAssemblyLoader::createContent(result, &error);
    QVERIFY2(content, qPrintable(error));
    QCOMPARE(H::describePages(content->assembly.getItems()), QStringLiteral("2:1 2:2"));
    const std::vector<pdf::PDFDocumentAssembly::RemovedItem>& removed = content->assembly.getRemovedItems();
    QCOMPARE(removed.size(), size_t(2));
    QCOMPARE(removed[0].item.id, QStringLiteral("r1"));
    QCOMPARE(removed[0].previousItemId, QStringLiteral("x1"));
    QCOMPARE(removed[0].index, 1);
    QCOMPARE(removed[1].item.id, QStringLiteral("m1"));
    QCOMPARE(removed[1].previousItemId, QStringLiteral("x1"));
    QCOMPARE(H::describePages({ removed[1].item }), QStringLiteral("1:1 1:2"));

    // The source of another computer stays in the assembly with its path (Locate… later)
    QCOMPARE(QDir::fromNativeSeparators(content->assembly.getSources()[0].path), QDir::fromNativeSeparators(remote.path));
}

void AppAssemblyTest::loaderLocate()
{
    using H = AppAssemblyTestHelper;

    // Locate…: the file the user has chosen for a missing source - the same content is Found at its
    // new path, a file with another content is Changed with its page count, a path, which is no
    // file, stays Missing
    pdftest::AppDocumentFixture fixture;
    const QString contract = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 3);
    const pdf::PDFDocumentAssembly::Source source = H::createSource(QStringLiteral("s1"), contract, 3);
    QVERIFY(QDir(fixture.folder()).mkpath(QStringLiteral("Elsewhere")));
    const QString copy = fixture.path(QStringLiteral("Elsewhere/Contract copy.pdf"));
    QVERIFY(QFile::copy(contract, copy));
    const QString other = pdftest::AppDocumentSamples::simple(fixture.path(QStringLiteral("Elsewhere")), QStringLiteral("Other.pdf"), 7);

    pdf::PDFAppAssemblyLoadResult result;
    pdf::PDFDocumentAssemblyFile::Content content;
    content.assembly = H::createAssembly({ source }, H::createPageItems(QStringLiteral("a"), 0, 3));
    result.content = content;
    pdf::PDFAppAssemblySourceStatus missing;
    missing.sourceId = QStringLiteral("s1");
    missing.state = pdf::PDFAppAssemblySourceState::Missing;
    result.sources.push_back(missing);

    pdf::PDFAppAssemblyLoadResult located = pdf::PDFAppAssemblyLoader::readLocated(result, QStringLiteral("s1"), copy, nullptr);
    QCOMPARE(located.sources.size(), size_t(1));
    QCOMPARE(located.sources[0].state, pdf::PDFAppAssemblySourceState::Found);
    QCOMPARE(QFileInfo(located.sources[0].filePath), QFileInfo(copy));
    QString error;
    std::optional<pdf::PDFDocumentAssemblyFile::Content> opened = pdf::PDFAppAssemblyLoader::createContent(located, &error);
    QVERIFY2(opened, qPrintable(error));
    QCOMPARE(QFileInfo(opened->assembly.getSources()[0].path), QFileInfo(copy));
    QCOMPARE(opened->assembly.getItems().size(), size_t(3));

    located = pdf::PDFAppAssemblyLoader::readLocated(result, QStringLiteral("s1"), other, nullptr);
    QCOMPARE(located.sources[0].state, pdf::PDFAppAssemblySourceState::Changed);
    QCOMPARE(located.sources[0].pageCount, pdf::PDFInteger(7));

    located = pdf::PDFAppAssemblyLoader::readLocated(result, QStringLiteral("s1"), fixture.path(QStringLiteral("Elsewhere")), nullptr);
    QCOMPARE(located.sources[0].state, pdf::PDFAppAssemblySourceState::Missing);
    opened = pdf::PDFAppAssemblyLoader::createContent(located, &error);
    QVERIFY2(opened, qPrintable(error));
    QVERIFY(opened->assembly.getItems().empty());
    QCOMPARE(opened->assembly.getRemovedItems().size(), size_t(3));
}

void AppAssemblyTest::loaderRefusesFiles()
{
    using H = AppAssemblyTestHelper;

    // The limits of the file (RB8, 256 MB) before the parsing; a missing file; bytes, which are no
    // assembly (the reason of the core); nothing is opened
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppAssemblyLoadResult result = pdf::PDFAppAssemblyLoader::read(fixture.path(QStringLiteral("None.pdf4qtassembly")), nullptr);
    QVERIFY(!result.content);
    QCOMPARE(result.errorMessage, QStringLiteral("The assembly file does not exist."));

    const QString large = fixture.path(QStringLiteral("Large.pdf4qtassembly"));
    {
        QFile file(large);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(pdf::PDFDocumentAssemblyFile::Limits().maximumFileSize + 1));
    }
    result = pdf::PDFAppAssemblyLoader::read(large, nullptr);
    QVERIFY(!result.content);
    QCOMPARE(result.errorMessage, QStringLiteral("The assembly file is larger than 256 MB."));
    QVERIFY(QFile::remove(large));

    const QString garbage = fixture.path(QStringLiteral("Garbage.pdf4qtassembly"));
    QVERIFY(H::writeFile(garbage, "{ \"format\": \"PageMaster Workspace\", \"version\": 1 }"));
    result = pdf::PDFAppAssemblyLoader::read(garbage, nullptr);
    QVERIFY(!result.content);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(result.sources.empty());
}

void AppAssemblyTest::loaderCancel()
{
    using H = AppAssemblyTestHelper;

    // RB8: the job asks for the cancellation after each megabyte of a hash - a source of 8 MB is
    // canceled inside of its hash (the 4th question: after the reading of the file, before the
    // source, after two blocks), the result has no content
    pdftest::AppDocumentFixture fixture;
    const QString bigFile = fixture.path(QStringLiteral("Big.pdf"));
    QByteArray data(8 * int(pdf::PDFAppAssemblyLoader::HASH_BLOCK_SIZE), 'x');
    data.replace(0, 9, "%PDF-1.7\n");
    QVERIFY(H::writeFile(bigFile, data));
    const pdf::PDFDocumentAssembly assembly = H::createAssembly({ H::createSource(QStringLiteral("s1"), bigFile, 1) }, H::createPageItems(QStringLiteral("a"), 0, 1));
    const QString assemblyPath = fixture.path(QStringLiteral("Big.pdf4qtassembly"));
    QVERIFY(H::writeAssembly(assemblyPath, assembly));

    AppAssemblyCountingControl control(4);
    pdf::PDFAppAssemblyLoadResult result = pdf::PDFAppAssemblyLoader::read(assemblyPath, &control);
    QVERIFY(result.isCanceled);
    QVERIFY(!result.content);
    QVERIFY(result.sources.empty());
    QCOMPARE(control.getCalls(), 4);

    // Not canceled: the same file is Found (its hash is the same)
    AppAssemblyCountingControl never(1000000);
    result = pdf::PDFAppAssemblyLoader::read(assemblyPath, &never);
    QVERIFY(!result.isCanceled);
    QVERIFY2(result.content, qPrintable(result.errorMessage));
    QCOMPARE(result.sources.front().state, pdf::PDFAppAssemblySourceState::Found);
    QVERIFY(never.getCalls() >= 2 + 8);
}

void AppAssemblyTest::loaderAsynchronous()
{
    using H = AppAssemblyTestHelper;

    // The job runs in the pool, loaded comes from the event loop; a second load is refused while
    // one runs (no signal); cancel delivers isCanceled; a deleted loader delivers nothing
    pdftest::AppDocumentFixture fixture;
    const QString contract = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Contract.pdf"), 2);
    const QString assemblyPath = fixture.path(QStringLiteral("Async.pdf4qtassembly"));
    QVERIFY(H::writeAssembly(assemblyPath, H::createAssembly({ H::createSource(QStringLiteral("s1"), contract, 2) }, H::createPageItems(QStringLiteral("a"), 0, 2))));

    pdf::PDFAppAssemblyLoader loader(nullptr);
    std::vector<pdf::PDFAppAssemblyLoadResult> results;
    QObject::connect(&loader, &pdf::PDFAppAssemblyLoader::loaded, &loader, [&results](const pdf::PDFAppAssemblyLoadResult& result) { results.push_back(result); });
    QVERIFY(loader.load(assemblyPath));
    QVERIFY(loader.isLoading());
    QVERIFY(!loader.load(assemblyPath));
    const int timeout = 30000 * pdftest::AppDocumentFixture::getTimeoutFactor();
    QVERIFY(QTest::qWaitFor([&results]() { return !results.empty(); }, timeout));
    QCOMPARE(results.size(), size_t(1));
    QVERIFY(!loader.isLoading());
    QVERIFY(!results.front().isCanceled);
    QVERIFY2(results.front().content, qPrintable(results.front().errorMessage));
    QCOMPARE(results.front().sources.front().state, pdf::PDFAppAssemblySourceState::Found);

    // Locate… of the loaded result
    QVERIFY(loader.locate(results.front(), QStringLiteral("s1"), contract));
    QVERIFY(QTest::qWaitFor([&results]() { return results.size() == 2; }, timeout));
    QCOMPARE(results.back().sources.front().state, pdf::PDFAppAssemblySourceState::Found);

    // Canceled: the signal says so, no content
    QVERIFY(loader.load(assemblyPath));
    loader.cancel();
    QVERIFY(QTest::qWaitFor([&results]() { return results.size() == 3; }, timeout));
    QVERIFY(results.back().isCanceled);
    QVERIFY(!results.back().content);

    // A loader deleted during its job: its result is dropped
    int deliveries = 0;
    pdf::PDFAppAssemblyLoader* deleted = new pdf::PDFAppAssemblyLoader(nullptr);
    QObject::connect(deleted, &pdf::PDFAppAssemblyLoader::loaded, &loader, [&deliveries]() { ++deliveries; });
    QVERIFY(deleted->load(assemblyPath));
    delete deleted;
    QThreadPool::globalInstance()->waitForDone(timeout);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(deliveries, 0);
}

void AppAssemblyTest::createContentKeepsGroups()
{
    using H = AppAssemblyTestHelper;

    // A group of pages of two sources, one of them missing: the group keeps its pages, which stay,
    // and its name; the missing pages are an item of the bin with a new unique id after the group;
    // a checkpoint loses the pages over the page count of a changed file (Use Changed File)
    pdf::PDFDocumentAssembly::Source first;
    first.id = QStringLiteral("s1");
    first.path = QStringLiteral("C:/Tender/Contract.pdf");
    first.pageCount = 6;
    pdf::PDFDocumentAssembly::Source second = first;
    second.id = QStringLiteral("s2");
    second.path = QStringLiteral("C:/Tender/Annex_A.pdf");
    second.pageCount = 3;

    pdf::PDFDocumentAssembly::Item group = H::createItem(QStringLiteral("g"), 0, { 0, 1 }, QStringLiteral("Introduction"));
    const pdf::PDFDocumentAssembly::Item annex = H::createItem(QStringLiteral("x"), 1, { 2 });
    group.pages.push_back(annex.pages.front());
    pdf::PDFAppAssemblyLoadResult result;
    pdf::PDFDocumentAssemblyFile::Content content;
    content.assembly = H::createAssembly({ first, second }, { group, H::createItem(QStringLiteral("g-1"), 0, { 5 }) });
    pdf::PDFDocumentAssembly::Checkpoint checkpoint;
    checkpoint.id = QStringLiteral("c1");
    checkpoint.items = { H::createItem(QStringLiteral("g-1"), 0, { 4, 5 }) };
    content.checkpoints = { checkpoint };
    content.outputParameters.insert(QStringLiteral("outline"), QStringLiteral("join"));
    result.content = content;

    pdf::PDFAppAssemblySourceStatus changed;
    changed.sourceId = QStringLiteral("s1");
    changed.state = pdf::PDFAppAssemblySourceState::Changed;
    changed.filePath = first.path;
    changed.pageCount = 5;
    changed.isAccepted = true;
    pdf::PDFAppAssemblySourceStatus missing;
    missing.sourceId = QStringLiteral("s2");
    missing.state = pdf::PDFAppAssemblySourceState::Missing;
    result.sources = { changed, missing };

    QString error;
    const std::optional<pdf::PDFDocumentAssemblyFile::Content> opened = pdf::PDFAppAssemblyLoader::createContent(result, &error);
    QVERIFY2(opened, qPrintable(error));
    const std::vector<pdf::PDFDocumentAssembly::Item>& items = opened->assembly.getItems();
    QCOMPARE(items.size(), size_t(1));
    QCOMPARE(items[0].id, QStringLiteral("g"));
    QCOMPARE(items[0].name, QStringLiteral("Introduction"));
    QCOMPARE(H::describePages(items), QStringLiteral("0:1 0:2"));

    // Page 6 of the contract is over its new count, the page of the annex is missing: both go to the
    // bin after the group (B21), each item id is unique (items and bin)
    const std::vector<pdf::PDFDocumentAssembly::RemovedItem>& removed = opened->assembly.getRemovedItems();
    QCOMPARE(removed.size(), size_t(2));
    QStringList removedPages;
    QSet<QString> ids({ items[0].id });
    for (const pdf::PDFDocumentAssembly::RemovedItem& removedItem : removed)
    {
        removedPages << H::describePages({ removedItem.item });
        QCOMPARE(removedItem.previousItemId, QStringLiteral("g"));
        QVERIFY2(!ids.contains(removedItem.item.id), qPrintable(removedItem.item.id));
        ids.insert(removedItem.item.id);
    }
    removedPages.sort();
    QCOMPARE(removedPages, QStringList({ QStringLiteral("0:6"), QStringLiteral("1:3") }));
    QVERIFY(opened->assembly.getSources()[1].isParked);
    QCOMPARE(opened->checkpoints.size(), size_t(1));
    QCOMPARE(H::describePages(opened->checkpoints.front().items), QStringLiteral("0:5"));
    QCOMPARE(opened->outputParameters.value(QStringLiteral("outline")).toString(), QStringLiteral("join"));
    QCOMPARE(opened->assembly.getSources()[0].pageCount, pdf::PDFInteger(5));

    // No content: refused with the reason of the load
    pdf::PDFAppAssemblyLoadResult refused;
    refused.errorMessage = QStringLiteral("This is not an assembly file.");
    QVERIFY(!pdf::PDFAppAssemblyLoader::createContent(refused, &error));
    QCOMPARE(error, refused.errorMessage);
}

void AppAssemblyTest::requestedFile()
{
    // The window gives the file to the list of the new own tab; the GUI takes it once
    pdf::PDFAppToolInputList list(nullptr);
    QVERIFY(pdf::PDFAppAssemblyLoader::takeRequestedFile(&list).isEmpty());
    pdf::PDFAppAssemblyLoader::setRequestedFile(&list, QStringLiteral("C:/Tender/Tender_2026.pdf4qtassembly"));
    QCOMPARE(pdf::PDFAppAssemblyLoader::takeRequestedFile(&list), QStringLiteral("C:/Tender/Tender_2026.pdf4qtassembly"));
    QVERIFY(pdf::PDFAppAssemblyLoader::takeRequestedFile(&list).isEmpty());
    QVERIFY(pdf::PDFAppAssemblyLoader::takeRequestedFile(nullptr).isEmpty());
}

void AppAssemblyTest::openAssemblyFile()
{
    using H = AppAssemblyTestHelper;

    // RB3 e: a double click on an assembly file and the command line (openDocuments) open the own
    // tab of Manage Pages with the file on the list of its inputs and record it in the recent
    // assemblies; without the tool the file is refused and nothing is recorded
    pdftest::AppDocumentFixture fixture;
    const QString assemblyPath = fixture.path(QStringLiteral("Tender_2026.pdf4qtassembly"));
    QVERIFY(H::writeFile(assemblyPath, "{}"));
    const QString toolId = QString::fromLatin1(pdf::PDFDocumentToolManage::TOOL_ID);
    pdf::PDFRecentAssemblyRepository repository(fixture.context->getDatabase());

    // The registration of the application is replaced for the test: without the tool, then with a
    // GUI, which leaves the requested file on the list (the GUI of Manage Pages takes it, M3)
    const std::shared_ptr<const pdf::PDFDocumentTool> previousTool = pdf::PDFDocumentToolRegistry::getTool(toolId);
    const std::shared_ptr<const pdf::PDFDocumentToolGUIFactory> previousFactory = pdf::PDFDocumentToolGUIRegistry::getFactory(toolId);
    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
    pdf::PDFDocumentToolRegistry::unregisterTool(toolId);

    QString error;
    QVERIFY(!pdf::PDFAppToolTabPageFactory::hasOwnTabPage(toolId));
    QVERIFY(!fixture.window->openAssembly(assemblyPath, &error));
    QCOMPARE(error, QStringLiteral("Manage Pages is not available"));
    QVERIFY(repository.getAssemblies().empty());

    QCOMPARE(pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<pdf::PDFDocumentToolManage>()), pdf::PDFDocumentToolRegistration::Registered);
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<pdf::PDFDocumentToolGUI>>()));
    const qsizetype tabCount = fixture.window->getToolTabs().size();
    fixture.window->openDocuments(QStringList({ assemblyPath }), 0);
    pdftest::AppDocumentFixture::settle();

    const QList<pdf::PDFAppToolTab*> tabs = fixture.window->getToolTabs();
    QCOMPARE(tabs.size(), tabCount + 1);
    pdf::PDFAppToolTab* tab = tabs.back();
    QCOMPARE(tab->getToolId(), toolId);
    QCOMPARE(QFileInfo(pdf::PDFAppAssemblyLoader::takeRequestedFile(tab->getInputList())), QFileInfo(assemblyPath));
    QVERIFY(fixture.window->getOpenFailedDialog() == nullptr);

    const std::vector<pdf::PDFRecentAssembly> assemblies = repository.getAssemblies();
    QCOMPARE(assemblies.size(), size_t(1));
    QCOMPARE(assemblies.front().path, H::stored(assemblyPath));
    QCOMPARE(assemblies.front().name, QStringLiteral("Tender_2026"));

    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
    QVERIFY(pdf::PDFDocumentToolRegistry::unregisterTool(toolId));
    if (previousTool)
    {
        pdf::PDFDocumentToolRegistry::registerTool(previousTool);
    }
    if (previousFactory)
    {
        pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, previousFactory);
    }
}

void AppAssemblyTest::openAssemblyByDropAndDialog()
{
    using H = AppAssemblyTestHelper;

    // RB3 e: the Open dialog of the window (Ctrl+O, Browse files…) offers the assembly files next
    // to the PDF files, and a drop onto the window takes them - both hand them to openAssembly
    // (openDocuments). The drop of a tool (its inputs) and other files stay refused.
    pdftest::AppDocumentFixture fixture;
    const QString assemblyPath = fixture.path(QStringLiteral("Tender_2026.pdf4qtassembly"));
    const QString notesPath = fixture.path(QStringLiteral("notes.txt"));
    QVERIFY(H::writeFile(assemblyPath, "{}"));
    QVERIFY(H::writeFile(notesPath, "Not a PDF"));

    QMimeData mimeData;
    mimeData.setUrls({ QUrl::fromLocalFile(assemblyPath), QUrl::fromLocalFile(notesPath) });
    const pdf::PDFAppFileDrop::Result toolDrop = pdf::PDFAppFileDrop::getFiles(&mimeData, pdf::PDFAppFileDrop::MAXIMUM_FILES, true);
    QVERIFY(toolDrop.files.isEmpty());
    QCOMPARE(toolDrop.refused.size(), size_t(2));
    const pdf::PDFAppFileDrop::Result windowDrop = pdf::PDFAppFileDrop::getFiles(&mimeData, pdf::PDFAppFileDrop::MAXIMUM_FILES, false, true);
    QCOMPARE(windowDrop.files, QStringList({ QFileInfo(assemblyPath).absoluteFilePath() }));
    QCOMPARE(windowDrop.refused.size(), size_t(1));
    QCOMPARE(windowDrop.refused.front().refusal, pdf::PDFAppFileDrop::Refusal::NotPdf);

    // The first filter of the dialog takes both kinds of files, the others each of them alone
    const QStringList filters = pdf::PDFAppFileDialogs::getOpenDocumentFilter().split(QStringLiteral(";;"));
    QCOMPARE(filters.size(), qsizetype(4));
    QVERIFY2(filters[0].contains(QStringLiteral("*.pdf ")) && filters[0].contains(QStringLiteral("*.pdf4qtassembly")), qPrintable(filters[0]));
    QVERIFY2(filters[1].contains(QStringLiteral("(*.pdf)")), qPrintable(filters[1]));
    QVERIFY2(filters[2].contains(QStringLiteral("(*.pdf4qtassembly)")), qPrintable(filters[2]));

    // The tool is registered with a GUI, which leaves the requested file on the list (as in
    // openAssemblyFile)
    const QString toolId = QString::fromLatin1(pdf::PDFDocumentToolManage::TOOL_ID);
    const std::shared_ptr<const pdf::PDFDocumentTool> previousTool = pdf::PDFDocumentToolRegistry::getTool(toolId);
    const std::shared_ptr<const pdf::PDFDocumentToolGUIFactory> previousFactory = pdf::PDFDocumentToolGUIRegistry::getFactory(toolId);
    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
    pdf::PDFDocumentToolRegistry::unregisterTool(toolId);
    QCOMPARE(pdf::PDFDocumentToolRegistry::registerTool(std::make_shared<pdf::PDFDocumentToolManage>()), pdf::PDFDocumentToolRegistration::Registered);
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<pdf::PDFDocumentToolGUIFactoryOf<pdf::PDFDocumentToolGUI>>()));

    // A drop of the assembly onto the window opens it (the text file is listed as left out)
    const qsizetype tabCount = fixture.window->getToolTabs().size();
    const QPoint position(fixture.window->width() / 2, fixture.window->height() / 2);
    QDragEnterEvent enter(position, Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window.data(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(position), Qt::CopyAction, &mimeData, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.window.data(), &drop);
    pdftest::AppDocumentFixture::settle();
    QList<pdf::PDFAppToolTab*> tabs = fixture.window->getToolTabs();
    QCOMPARE(tabs.size(), tabCount + 1);
    QCOMPARE(tabs.back()->getToolId(), toolId);
    QCOMPARE(QFileInfo(pdf::PDFAppAssemblyLoader::takeRequestedFile(tabs.back()->getInputList())), QFileInfo(assemblyPath));
    QCOMPARE(fixture.closeMessageBoxes(), 1);

    // The Open dialog of the window: the chosen assembly opens the same way
    pdf::PDFAppFileDialogs::setOpenFilesHandler([assemblyPath](QWidget*, const QString&) { return QStringList({ assemblyPath }); });
    fixture.window->browseDocuments();
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
    pdftest::AppDocumentFixture::settle();
    tabs = fixture.window->getToolTabs();
    QCOMPARE(tabs.size(), tabCount + 2);
    QCOMPARE(QFileInfo(pdf::PDFAppAssemblyLoader::takeRequestedFile(tabs.back()->getInputList())), QFileInfo(assemblyPath));
    QVERIFY(fixture.window->getOpenFailedDialog() == nullptr);

    pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
    QVERIFY(pdf::PDFDocumentToolRegistry::unregisterTool(toolId));
    if (previousTool)
    {
        pdf::PDFDocumentToolRegistry::registerTool(previousTool);
    }
    if (previousFactory)
    {
        pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, previousFactory);
    }
}

void AppAssemblyTest::openPageRecentAssemblies()
{
    using H = AppAssemblyTestHelper;

    // Plan 4.6: the page Open lists the recent assemblies (the name, the folder with the pages, the
    // pin), hidden without one; it follows the change log (an assembly recorded later appears), a
    // star pins the file in the repository
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppOpenPage page(fixture.context.get(), nullptr);
    QWidget* section = page.findChild<QWidget*>(QStringLiteral("recentAssemblies"));
    QVERIFY(section);
    QVERIFY(section->isHidden());

    const QString assemblyPath = fixture.path(QStringLiteral("Tender_2026.pdf4qtassembly"));
    QVERIFY(H::writeFile(assemblyPath, "{}"));
    pdf::PDFRecentAssemblyRepository repository(fixture.context->getDatabase());
    QVERIFY(!repository.recordOpened(assemblyPath, 17).isError());

    const int timeout = 10000 * pdftest::AppDocumentFixture::getTimeoutFactor();
    QVERIFY(QTest::qWaitFor([section]() { return !section->isHidden(); }, timeout));
    const QList<pdf::PDFRecentFileRow*> rows = section->findChildren<pdf::PDFRecentFileRow*>(QStringLiteral("assemblyRow"));
    QCOMPARE(rows.size(), qsizetype(1));
    QCOMPARE(rows.front()->getFileName(), QStringLiteral("Tender_2026"));
    // Owner 2026-11-12: the folder is elided in the middle and the count of pages is
    // its detail (drawn after it, always whole), not a part of the elided folder
    const QString folder = QDir::toNativeSeparators(repository.getAssemblies().front().folder);
    QVERIFY(!folder.isEmpty());
    QCOMPARE(rows.front()->getPath(), folder);
    QCOMPARE(rows.front()->getDetail(), QStringLiteral("17 pages"));
    QCOMPARE(rows.front()->getContent().detail, QStringLiteral("17 pages"));
    QVERIFY2(QAccessible::queryAccessibleInterface(rows.front())->text(QAccessible::Description).startsWith(folder + QStringLiteral(", 17 pages")),
             qPrintable(QAccessible::queryAccessibleInterface(rows.front())->text(QAccessible::Description)));
    QVERIFY(!rows.front()->isPinned());

    // The star (setPinned emits pinnedChanged as the star button does)
    rows.front()->setPinned(true);
    QVERIFY(repository.getAssemblies().front().pinned);
}

QTEST_MAIN(AppAssemblyTest)

#include "tst_appassemblytest.moc"
