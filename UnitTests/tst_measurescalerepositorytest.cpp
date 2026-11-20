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

// The scales of Measure (plan of step 7 of phase 2, 4.8, owner answer Q7.4): migration 4 of the database (the
// skeleton of step 7c - its tables, its triggers, the migration of a database of version 3 with its rows kept) and
// PDFMeasureScaleRepository (package P7: the presets, the default scale, the scales of the documents by the path and
// /ID[0], at most 200 by LRU, hostile rows).

#include "pdfdatabase.h"
#include "pdfdatabaseschema.h"
#include "pdfmeasurescalerepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

/// Helpers of the test
class MeasureScaleRepositoryTestHelper
{
public:
    MeasureScaleRepositoryTestHelper() = delete;

    /// Options of a database file of this test (its own memory name)
    static pdf::PDFDatabaseOptions createFileOptions(const QString& filePath)
    {
        static int counter = 0;
        pdf::PDFDatabaseOptions options;
        options.filePath = filePath;
        options.memoryName = QStringLiteral("/pdf4qt-measurescale-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter);
        return options;
    }

    static bool execute(pdf::PDFDatabase* database, const char* sql)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection && !connection->execute(sql).isError();
    }

    static qint64 queryInteger(pdf::PDFDatabase* database, const char* sql)
    {
        pdf::PDFDatabaseConnection* connection = database->getConnection();
        return connection ? connection->queryInteger(sql, -1) : -1;
    }
};

class MeasureScaleRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void migration();
    void repository();
    void description();
    void lruByUse();
};

void MeasureScaleRepositoryTest::lruByUse()
{
    using H = MeasureScaleRepositoryTestHelper;
    using pdf::PDFMeasureScale;
    using pdf::PDFMeasureScaleRepository;

    // Review RS7CCORE L8 (plan 4.8: at most 200 documents, the least recently USED go): a document, whose remembered
    // scale was set first but is used again (the tool opened on it), stays; the least recently used one goes
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdf::PDFDatabase database;
    database.open(H::createFileOptions(directory.filePath(QStringLiteral("lru.db"))));
    PDFMeasureScaleRepository repository(&database);
    const QString plans = QDir::cleanPath(directory.path()) + QStringLiteral("/plans/");
    const QByteArray id = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const PDFMeasureScale metric50(1.0, "mm", 50.0, "mm", QStringLiteral("Metric 1:50"));

    for (int i = 0; i < PDFMeasureScaleRepository::MAXIMUM_DOCUMENT_SCALES; ++i)
    {
        repository.setCurrentTime(1000 + i);
        QVERIFY(!repository.setDocumentScale(plans + QStringLiteral("%1.pdf").arg(i), id, metric50).isError());
    }

    // 0.pdf (set first) is used now, 1.pdf is not: a new document prunes 1.pdf, the least recently used one
    repository.setCurrentTime(5000);
    QVERIFY(!repository.markDocumentScaleUsed(plans + QStringLiteral("0.pdf"), id).isError());
    QCOMPARE(H::queryInteger(&database, "SELECT last_used FROM measure_document_scale WHERE path LIKE '%/0.pdf'"), qint64(5000));
    repository.setCurrentTime(5001);
    QVERIFY(!repository.setDocumentScale(plans + QStringLiteral("new.pdf"), id, metric50).isError());
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM measure_document_scale"), qint64(PDFMeasureScaleRepository::MAXIMUM_DOCUMENT_SCALES));
    QVERIFY(repository.findDocumentScale(plans + QStringLiteral("0.pdf"), id));
    QVERIFY(!repository.findDocumentScale(plans + QStringLiteral("1.pdf"), id));
    QVERIFY(repository.findDocumentScale(plans + QStringLiteral("new.pdf"), id));

    // The use keeps the scale itself; a document without a remembered scale gets no row; an unidentified one is refused
    QCOMPARE(repository.findDocumentScale(plans + QStringLiteral("0.pdf"), id), std::optional<PDFMeasureScale>(metric50));
    QVERIFY(!repository.markDocumentScaleUsed(plans + QStringLiteral("unknown.pdf"), id).isError());
    QVERIFY(!repository.findDocumentScale(plans + QStringLiteral("unknown.pdf"), id));
    QVERIFY(repository.markDocumentScaleUsed(QString(), id).isError());
    database.close();
}

void MeasureScaleRepositoryTest::migration()
{
    using H = MeasureScaleRepositoryTestHelper;

    // Migration 4 adds the three tables of the scales of Measure with the triggers of the change log; the released
    // migrations 1 - 3 are the same as before - none of their statements names the new tables
    const std::vector<pdf::PDFDatabaseMigration> migrations = pdf::PDFDatabaseSchema::getMigrations();
    QVERIFY(pdf::PDFDatabaseSchema::isValid(migrations));
    QVERIFY(migrations.size() >= 4);
    const QByteArray tableNames[] = { "measure_scale_preset", "measure_scale_state", "measure_document_scale" };
    for (const QByteArray& name : tableNames)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            QVERIFY2(!migrations[i].sql.contains(name), name.constData());
        }
        QVERIFY2(migrations[3].sql.contains("CREATE TABLE " + name + "("), name.constData());
        QVERIFY2(migrations[3].sql.contains("CREATE TRIGGER " + name + "_log_insert"), name.constData());
    }

    const std::vector<pdf::PDFDatabaseTableInfo> tables = pdf::PDFDatabaseSchema::getTables(4);
    QCOMPARE(tables.size(), size_t(3));
    QCOMPARE(tables[0].name, pdf::PDFMeasureScaleRepository::getPresetTopic());
    QCOMPARE(tables[0].keyExpression, QStringLiteral("CAST(%1.id AS TEXT)"));
    QCOMPARE(tables[1].name, pdf::PDFMeasureScaleRepository::getStateTopic());
    QCOMPARE(tables[1].keyExpression, QStringLiteral("CAST(%1.id AS TEXT)"));
    QCOMPARE(tables[2].name, pdf::PDFMeasureScaleRepository::getDocumentTopic());
    QCOMPARE(tables[2].keyExpression, QStringLiteral("%1.path"));

    // A database of version 3 is migrated; its rows stay
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("version3.db"));
    {
        pdf::PDFDatabaseError error;
        sqlite3* handle = pdf::PDFDatabase::openFileConnection(filePath, false, 5000, &error);
        QVERIFY(handle);
        QVERIFY(!pdf::PDFDatabaseSchema::migrate(handle, migrations, 3).isError());
        QCOMPARE(pdf::PDFDatabaseSchema::readVersion(handle), 3);
        QCOMPARE(pdf::PDFDatabase::queryIntegerOnHandle(handle, "SELECT COUNT(*) FROM sqlite_schema WHERE name LIKE 'measure_%'", -1), qint64(0));
        QVERIFY(!pdf::PDFDatabase::executeOnHandle(handle, "INSERT INTO certificate_cache(path, fingerprint, data, added, last_used) VALUES ('/c.pfx', 'f', x'00', 1, 1)").isError());
        pdf::PDFDatabase::closeHandle(handle);
    }

    pdf::PDFDatabase database;
    database.open(H::createFileOptions(filePath));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(database.getState().fileSchemaVersion, 3);
    QCOMPARE(database.getState().schemaVersion, pdf::PDFDatabaseSchema::getCurrentVersion());
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM certificate_cache"), qint64(1));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table' AND name LIKE 'measure_%'"), qint64(3));

    // The rows of a scale, of the state and of a document; the key of a document is its path and the first
    // identifier of its /ID (two documents of one path are two rows, the same pair is one row)
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit) VALUES (0, 'Metric 1:50', 1, 'mm', 50, 'mm')"));
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_state(id, presets_edited) VALUES (1, 1)"));
    QVERIFY(H::execute(&database, "INSERT INTO measure_document_scale(path, document_id, paper_value, paper_unit, real_value, real_unit, last_used) VALUES ('/a.pdf', x'0102', 1, 'mm', 100, 'mm', 5)"));
    QVERIFY(H::execute(&database, "INSERT INTO measure_document_scale(path, document_id, paper_value, paper_unit, real_value, real_unit, last_used) VALUES ('/a.pdf', x'0103', 1, 'mm', 20, 'mm', 6)"));
    QVERIFY(!H::execute(&database, "INSERT INTO measure_document_scale(path, document_id, paper_value, paper_unit, real_value, real_unit, last_used) VALUES ('/a.pdf', x'0102', 1, 'mm', 10, 'mm', 7)"));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM measure_document_scale"), qint64(2));

    // The state has one row: a second row (another writer, a hostile file) is refused
    QVERIFY(!H::execute(&database, "INSERT INTO measure_scale_state(id, presets_edited) VALUES (2, 0)"));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM measure_scale_state"), qint64(1));

    // Every write is a notification of its table (the topic) with its key
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE topic = 'measure_scale_preset' AND key = '1'"), qint64(1));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE topic = 'measure_scale_state' AND key = '1'"), qint64(1));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM change_log WHERE topic = 'measure_document_scale' AND key = '/a.pdf'"), qint64(2));
    database.close();
}

void MeasureScaleRepositoryTest::repository()
{
    using H = MeasureScaleRepositoryTestHelper;
    using pdf::PDFMeasureScale;
    using pdf::PDFMeasureScaleRepository;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdf::PDFDatabase database;
    database.open(H::createFileOptions(directory.filePath(QStringLiteral("scales.db"))));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    PDFMeasureScaleRepository repository(&database);
    const QString plans = QDir::cleanPath(directory.path()) + QStringLiteral("/plans/");

    // A fresh database: the 12 built-in presets of 471:6943, the default scale Real size (1:1) of 473:92070
    QVERIFY(!repository.arePresetsEdited());
    QCOMPARE(repository.getPresets().size(), size_t(12));
    QCOMPARE(repository.getDefaultScale().getName(), QStringLiteral("Real size (1:1)"));
    QCOMPARE(repository.getDefaultScale().getScaleFactor(), 1.0);

    // Edited presets in their order; an empty list stays empty (not the built-in one); Restore Defaults (473:8249)
    const PDFMeasureScale groundFloor(224.0, "mm", 11.2, "m", QStringLiteral("Ground floor"));
    const PDFMeasureScale metric75(1.0, "mm", 75.0, "mm", QStringLiteral("Metric 1:75"));
    QVERIFY(!repository.setPresets({ groundFloor, metric75 }).isError());
    QVERIFY(repository.arePresetsEdited());
    std::vector<PDFMeasureScale> presets = repository.getPresets();
    QCOMPARE(presets.size(), size_t(2));
    QCOMPARE(presets[0], groundFloor);
    QCOMPARE(presets[1], metric75);
    QVERIFY(qFuzzyCompare(presets[0].getScaleFactor(), 50.0));
    QVERIFY(!repository.setPresets({}).isError());
    QVERIFY(repository.getPresets().empty());
    QVERIFY(!repository.restoreDefaultPresets().isError());
    QVERIFY(!repository.arePresetsEdited());
    QCOMPARE(repository.getPresets().size(), size_t(12));

    // Invalid presets are refused and nothing changes: an invalid scale, a long name, more than 200
    QVERIFY(!repository.setPresets({ groundFloor }).isError());
    QVERIFY(repository.setPresets({ metric75, PDFMeasureScale(1.0, "mm", 0.0, "mm") }).isError());
    QVERIFY(repository.setPresets({ PDFMeasureScale(1.0, "mm", 2.0, "mm", QString(PDFMeasureScaleRepository::MAXIMUM_NAME_LENGTH + 1, QChar('x'))) }).isError());
    QVERIFY(repository.setPresets(std::vector<PDFMeasureScale>(PDFMeasureScaleRepository::MAXIMUM_PRESETS + 1, metric75)).isError());
    QCOMPARE(repository.getPresets().size(), size_t(1));
    QVERIFY(!repository.setPresets(std::vector<PDFMeasureScale>(PDFMeasureScaleRepository::MAXIMUM_PRESETS, metric75)).isError());
    QCOMPARE(repository.getPresets().size(), size_t(PDFMeasureScaleRepository::MAXIMUM_PRESETS));

    // The default scale
    QVERIFY(!repository.setDefaultScale(metric75).isError());
    QCOMPARE(repository.getDefaultScale(), metric75);
    QVERIFY(repository.setDefaultScale(PDFMeasureScale()).isError());
    QCOMPARE(repository.getDefaultScale(), metric75);

    // A document is identified by its absolute path and the first identifier of /ID (14.4)
    const QByteArray firstId = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QByteArray secondId = QByteArray::fromHex("ffeeddccbbaa99887766554433221100");
    QVERIFY(PDFMeasureScaleRepository::isDocumentIdentifiable((plans + QStringLiteral("Floor_Plan.pdf")), firstId));
    QVERIFY(!PDFMeasureScaleRepository::isDocumentIdentifiable(QString(), firstId));
    QVERIFY(!PDFMeasureScaleRepository::isDocumentIdentifiable(QStringLiteral("Floor_Plan.pdf"), firstId));
    QVERIFY(!PDFMeasureScaleRepository::isDocumentIdentifiable((plans + QStringLiteral("Floor_Plan.pdf")), QByteArray()));
    QVERIFY(!PDFMeasureScaleRepository::isDocumentIdentifiable((plans + QStringLiteral("Floor_Plan.pdf")), QByteArray(PDFMeasureScaleRepository::MAXIMUM_DOCUMENT_ID_LENGTH + 1, 'a')));
    QVERIFY(repository.setDocumentScale(QStringLiteral("Floor_Plan.pdf"), firstId, metric75).isError());

    repository.setCurrentTime(1000);
    QVERIFY(!repository.setDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), firstId, groundFloor).isError());
    QCOMPARE(repository.findDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), firstId), std::optional<PDFMeasureScale>(groundFloor));
    QVERIFY(!repository.findDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), secondId));
    QVERIFY(!repository.findDocumentScale(plans + QStringLiteral("Other.pdf"), firstId));
    QVERIFY(!repository.setDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), firstId, metric75).isError());
    QCOMPARE(repository.findDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), firstId), std::optional<PDFMeasureScale>(metric75));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM measure_document_scale"), qint64(1));
    QVERIFY(!repository.removeDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), firstId).isError());
    QVERIFY(!repository.findDocumentScale((plans + QStringLiteral("Floor_Plan.pdf")), firstId));

    // At most 200 documents: the least recently written goes (LRU by last_used); a write refreshes a document
    for (int i = 0; i < PDFMeasureScaleRepository::MAXIMUM_DOCUMENT_SCALES; ++i)
    {
        repository.setCurrentTime(2000 + i);
        QVERIFY(!repository.setDocumentScale(plans + QStringLiteral("%1.pdf").arg(i), firstId, metric75).isError());
    }
    repository.setCurrentTime(5000);
    QVERIFY(!repository.setDocumentScale(plans + QStringLiteral("0.pdf"), firstId, groundFloor).isError());
    repository.setCurrentTime(5001);
    QVERIFY(!repository.setDocumentScale(plans + QStringLiteral("new.pdf"), firstId, metric75).isError());
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM measure_document_scale"), qint64(PDFMeasureScaleRepository::MAXIMUM_DOCUMENT_SCALES));
    QVERIFY(repository.findDocumentScale(plans + QStringLiteral("0.pdf"), firstId));
    QVERIFY(!repository.findDocumentScale(plans + QStringLiteral("1.pdf"), firstId));
    QVERIFY(repository.findDocumentScale(plans + QStringLiteral("2.pdf"), firstId));
    QVERIFY(repository.findDocumentScale(plans + QStringLiteral("new.pdf"), firstId));

    // Hostile rows of another program are skipped: a text value, an unknown unit, a negative value, a long name
    // (a valid row of the same form is read - the control of the test)
    const QStringList hostileRows = { "'%1text.pdf', x'01', '', 'abc', 'mm', 50, 'mm'", "'%1unit.pdf', x'01', '', 1, 'parsec', 50, 'mm'",
                                      "'%1negative.pdf', x'01', '', 1, 'mm', -50, 'mm'", "'%1name.pdf', x'01', printf('%.300c', 'x'), 1, 'mm', 50, 'mm'",
                                      "'%1valid.pdf', x'01', 'Valid', 1, 'mm', 50, 'mm'" };
    for (const QString& row : hostileRows)
    {
        const QByteArray sql = QStringLiteral("INSERT INTO measure_document_scale(path, document_id, name, paper_value, paper_unit, real_value, real_unit, last_used) VALUES (%1, 9999)")
                                   .arg(row.arg(plans)).toUtf8();
        QVERIFY2(H::execute(&database, sql.constData()), sql.constData());
    }
    for (const char* name : { "text.pdf", "unit.pdf", "negative.pdf", "name.pdf" })
    {
        QVERIFY2(!repository.findDocumentScale(plans + QString::fromLatin1(name), QByteArray(1, '\x01')), name);
    }
    QCOMPARE(repository.findDocumentScale(plans + QStringLiteral("valid.pdf"), QByteArray(1, '\x01')), std::optional<PDFMeasureScale>(PDFMeasureScale(1.0, "mm", 50.0, "mm", QStringLiteral("Valid"))));

    QVERIFY(!repository.setPresets({ metric75 }).isError());
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit) VALUES (5, 'Bad', x'00', 'mm', 1, 'mm')"));
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit) VALUES (6, 'Area', 1, 'sqm', 1, 'mm')"));
    QCOMPARE(repository.getPresets().size(), size_t(1));
    QVERIFY(H::execute(&database, "UPDATE measure_scale_state SET default_paper_value = 'x' WHERE id = 1"));
    QCOMPARE(repository.getDefaultScale().getName(), QStringLiteral("Real size (1:1)"));
    database.close();
}

void MeasureScaleRepositoryTest::description()
{
    using H = MeasureScaleRepositoryTestHelper;
    using pdf::PDFMeasureScale;
    using pdf::PDFMeasureScaleRepository;

    // The field Description of a preset (473:7860, the column of 473:7412) and of the default scale (473:92070) is
    // stored by migration 4: description of a preset (an empty text by default), default_description of the state
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pdf::PDFDatabase database;
    database.open(H::createFileOptions(directory.filePath(QStringLiteral("descriptions.db"))));
    QCOMPARE(database.getState().mode, pdf::PDFDatabaseStorageMode::Disk);
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM pragma_table_info('measure_scale_preset') WHERE name = 'description' AND type = 'TEXT' AND \"notnull\" = 1 AND dflt_value = ''''''"), qint64(1));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM pragma_table_info('measure_scale_state') WHERE name = 'default_description' AND type = 'TEXT'"), qint64(1));
    QCOMPARE(H::queryInteger(&database, "SELECT COUNT(*) FROM pragma_table_info('measure_document_scale') WHERE name LIKE '%description%'"), qint64(0));
    PDFMeasureScaleRepository repository(&database);

    // A fresh database: the default scale "Real size (1:1)" - "The drawing is in the real size" (473:92070)
    QCOMPARE(repository.getDefaultScale().getDescription(), QStringLiteral("The drawing is in the real size"));

    // A preset keeps its description, also an empty one; the built-in description is stored with the edited list
    PDFMeasureScale facade(1.0, "mm", 75.0, "mm", QStringLiteral("Metric 1:75"));
    facade.setDescription(QStringLiteral("Facade details"));
    const PDFMeasureScale plain(1.0, "mm", 20.0, "mm", QStringLiteral("Metric 1:20"));
    QVERIFY(!repository.setPresets({ PDFMeasureScale::getDefaultPresets().front(), facade, plain }).isError());
    std::vector<PDFMeasureScale> presets = repository.getPresets();
    QCOMPARE(presets.size(), size_t(3));
    QCOMPARE(presets[0].getDescription(), QStringLiteral("The drawing is in the real size"));
    QCOMPARE(presets[1].getDescription(), QStringLiteral("Facade details"));
    QVERIFY(presets[2].getDescription().isEmpty());

    // The default scale keeps its description
    PDFMeasureScale site(1.0, "mm", 100.0, "mm", QStringLiteral("Metric 1:100"));
    site.setDescription(QStringLiteral("Site plans"));
    QVERIFY(!repository.setDefaultScale(site).isError());
    QCOMPARE(repository.getDefaultScale().getDescription(), QStringLiteral("Site plans"));

    // A description longer than MAXIMUM_DESCRIPTION_LENGTH is refused, nothing changes
    PDFMeasureScale tooLong = facade;
    tooLong.setDescription(QString(PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH + 1, QChar('x')));
    QVERIFY(repository.setPresets({ tooLong }).isError());
    QVERIFY(repository.setDefaultScale(tooLong).isError());
    QCOMPARE(repository.getPresets().size(), size_t(3));
    QCOMPARE(repository.getDefaultScale().getDescription(), QStringLiteral("Site plans"));
    tooLong.setDescription(QString(PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH, QChar('x')));
    QVERIFY(!repository.setDefaultScale(tooLong).isError());
    QCOMPARE(repository.getDefaultScale().getDescription().size(), qsizetype(PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH));

    // Hostile rows of another program are skipped: a description, which is a blob or too long; a control character
    // of a stored description is removed (a valid row of the same form is read - the control of the test)
    QVERIFY(!repository.setPresets({ plain }).isError());
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit, description) VALUES (5, 'Blob', 1, 'mm', 10, 'mm', x'00')"));
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit, description) VALUES (6, 'Long', 1, 'mm', 10, 'mm', printf('%.1025c', 'x'))"));
    QVERIFY(H::execute(&database, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit, description) VALUES (7, 'Valid', 1, 'mm', 10, 'mm', 'Two' || char(10) || 'lines')"));
    presets = repository.getPresets();
    QCOMPARE(presets.size(), size_t(2));
    QCOMPARE(presets[1].getName(), QStringLiteral("Valid"));
    QCOMPARE(presets[1].getDescription(), QStringLiteral("Twolines"));
    QVERIFY(H::execute(&database, "UPDATE measure_scale_state SET default_description = x'01' WHERE id = 1"));
    QCOMPARE(repository.getDefaultScale().getName(), QStringLiteral("Real size (1:1)"));
    QCOMPARE(repository.getDefaultScale().getDescription(), QStringLiteral("The drawing is in the real size"));
    database.close();
}

QTEST_GUILESS_MAIN(MeasureScaleRepositoryTest)

#include "tst_measurescalerepositorytest.moc"
