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

// The inputs of a tool with more inputs (package B2 of step 2 of phase 2): the loader with
// hostile and encrypted files, the snapshot of an open tab (R11), Undo / Redo without a
// second reading (R12), the states of the password dialog (398:27726, Q2), the previews of
// the pages of the inputs (plan 4.4), the chooser of the mode Multiple (531:3208,
// 551:149074) and the deletion in each state. The expected values come from the frames,
// the plan and ISO 32000-2 (Table 22 for the permissions, 7.5.2 for the signature).

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfappdocumenttab.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfapppassworddialog.h"
#include "pdfapptooldocumentchooser.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputloader.h"
#include "pdftoolstartpage.h"

#include "pdfcatalog.h"
#include "pdfcomponentdelegates.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdffluentwidgets.h"
#include "pdfsecurityhandler.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QListView>
#include <QMimeData>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

using pdf::PDFAppToolInput;
using pdf::PDFAppToolInputKind;
using pdf::PDFAppToolInputList;
using pdf::PDFAppToolInputLoader;
using pdf::PDFAppToolInputState;
using pdftest::AppDocumentFixture;
using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;

namespace pdftest
{

/// Samples and waits of the tests of the inputs
class AppToolInputsHelper
{
public:
    /// An image file: \p size pixels of \p color, \p dpi stored in the file (0 = none)
    static QByteArray createImage(const QSize& size, QColor color, double dpi, const char* format = "PNG")
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        if (dpi > 0.0)
        {
            const int dotsPerMeter = int(std::lround(dpi / 0.0254));
            image.setDotsPerMeterX(dotsPerMeter);
            image.setDotsPerMeterY(dotsPerMeter);
        }

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, format);
        return data;
    }

    static QString writeFile(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        if (!file.open(QFile::WriteOnly) || file.write(data) != data.size())
        {
            return QString();
        }
        return filePath;
    }

    /// CRC of a PNG chunk (ISO/IEC 15948, Annex D)
    static quint32 crc32(const QByteArray& data)
    {
        quint32 crc = 0xFFFFFFFFu;
        for (const char byte : data)
        {
            crc ^= quint8(byte);
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    static void appendUInt32(QByteArray* data, quint32 value)
    {
        data->append(char((value >> 24) & 0xFF));
        data->append(char((value >> 16) & 0xFF));
        data->append(char((value >> 8) & 0xFF));
        data->append(char(value & 0xFF));
    }

    /// A PNG, whose header claims \p width x \p height pixels (no pixels follow): a hostile
    /// image refused by its header before any pixel is decoded (R15)
    /// Baseline TIFF 6.0 (little endian) of \p frameCount frames, each one 1 x 1 gray pixel
    /// without compression; the frames are a chain of image file directories (TIFF 6.0,
    /// section 2, "Multiple images per file" - as the sample of UnitTestsDocumentMerger)
    static QByteArray createTiff(int frameCount)
    {
        auto add16 = [](QByteArray& data, quint16 value) { data.append(char(value & 0xFF)); data.append(char(value >> 8)); };
        auto add32 = [](QByteArray& data, quint32 value) { for (int i = 0; i < 4; ++i) { data.append(char((value >> (8 * i)) & 0xFF)); } };

        QByteArray data("II*\0", 4);
        add32(data, 8);
        for (int frame = 0; frame < frameCount; ++frame)
        {
            const quint32 directory = quint32(data.size());
            const quint16 entryCount = 9;
            const quint32 pixel = directory + 2 + entryCount * 12 + 4;
            auto entry = [&](quint16 tag, quint16 type, quint32 value)
            {
                add16(data, tag);
                add16(data, type);
                add32(data, 1);
                if (type == 3)
                {
                    add16(data, quint16(value));
                    add16(data, 0);
                }
                else
                {
                    add32(data, value);
                }
            };
            add16(data, entryCount);
            entry(256, 4, 1);           // ImageWidth
            entry(257, 4, 1);           // ImageLength
            entry(258, 3, 8);           // BitsPerSample
            entry(259, 3, 1);           // Compression: none
            entry(262, 3, 1);           // PhotometricInterpretation: black is zero
            entry(273, 4, pixel);       // StripOffsets
            entry(277, 3, 1);           // SamplesPerPixel
            entry(278, 4, 1);           // RowsPerStrip
            entry(279, 4, 1);           // StripByteCounts
            add32(data, frame + 1 < frameCount ? pixel + 1 + ((pixel + 1) & 1) : 0);
            data.append(char(frame % 256));
            if (data.size() & 1)
            {
                data.append('\0');
            }
        }
        return data;
    }

    static QByteArray createHugePngHeader(quint32 width, quint32 height)
    {
        QByteArray png("\x89PNG\r\n\x1A\n", 8);
        QByteArray chunk("IHDR");
        appendUInt32(&chunk, width);
        appendUInt32(&chunk, height);
        chunk.append(char(8));  // bit depth
        chunk.append(char(2));  // RGB
        chunk.append(char(0));
        chunk.append(char(0));
        chunk.append(char(0));
        appendUInt32(&png, 13);
        png.append(chunk);
        appendUInt32(&png, crc32(chunk));

        // A few compressed bytes and the end: a whole file, whose pixels would need 30 GB
        const QByteArray pixels = qCompress(QByteArray(64, char(0))).mid(4);
        QByteArray data("IDAT");
        data.append(pixels);
        appendUInt32(&png, quint32(pixels.size()));
        png.append(data);
        appendUInt32(&png, crc32(data));
        const QByteArray end("IEND");
        appendUInt32(&png, 0);
        png.append(end);
        appendUInt32(&png, crc32(end));
        return png;
    }

    /// A PDF of \p pageCount pages 300 x 400 encrypted by AES-256 with the permissions
    static QString encrypted(const QString& folder, const QString& name, const QString& userPassword, const QString& ownerPassword, uint32_t permissions)
    {
        pdf::PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 300, 400));
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = userPassword;
        settings.ownerPassword = ownerPassword;
        settings.permissions = permissions;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(name);
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// A PDF without a page: a catalog with an empty page tree (7.7.3.2: /Count 0)
    static QString noPages(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(QStringLiteral("no-pages.pdf"));
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static PDFAppToolInputLoader::Result read(const QString& filePath, const QString& password = QString(), bool isOwnerPassword = false)
    {
        PDFAppToolInputLoader::Request request;
        request.id = QStringLiteral("x");
        request.filePath = filePath;
        request.hasPassword = !password.isNull();
        request.password = password;
        request.isOwnerPassword = isOwnerPassword;
        return PDFAppToolInputLoader::read(std::move(request));
    }

    static bool waitForState(const PDFAppToolInputList* list, const QString& id, PDFAppToolInputState state)
    {
        return AppToolShellFixture::waitFor([list, id, state]() { const PDFAppToolInput* input = list->findInput(id); return input && input->state == state; });
    }

    static bool waitForLoaded(const PDFAppToolInputList* list)
    {
        return AppToolShellFixture::waitFor([list]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0; });
    }
};

/// The pages of the inputs in the rows of a model of thumbnails (as the model of the merge
/// tool will have them): a row is {input id, page}
class AppToolInputsPicturesModel : public pdf::PDFAppThumbnailsModel
{
public:
    AppToolInputsPicturesModel(PDFAppToolInputList* list, std::vector<std::pair<QString, int>> rows, QObject* parent) :
        pdf::PDFAppThumbnailsModel(parent),
        m_list(list),
        m_rows(std::move(rows))
    {

    }

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    virtual bool hasPictureSources() const override { return true; }

    virtual pdf::PDFAppThumbnailSource getPictureSource(int row) const override
    {
        return row >= 0 && row < int(m_rows.size()) ? m_list->getPictureSource(m_rows[size_t(row)].first, m_rows[size_t(row)].second) : pdf::PDFAppThumbnailSource();
    }

    virtual QString getKey(int row) const override
    {
        if (row < 0 || row >= int(m_rows.size()))
        {
            return QString();
        }
        return m_list->getPictureKey(m_rows[size_t(row)].first, m_rows[size_t(row)].second, getPictureSize(), getDevicePixelRatio()) + QStringLiteral("/") + QString::number(getGeneration());
    }

private:
    PDFAppToolInputList* m_list;
    std::vector<std::pair<QString, int>> m_rows;
};

}   // namespace pdftest

using pdftest::AppToolInputsHelper;

class AppToolInputsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void loaderReadsPdf();
    void loaderPasswordOfUser();
    void loaderRestrictedNeedsOwnerPassword();
    void loaderAssembleDenied();
    void loaderHostileFiles();
    void loaderRepairedFile();
    void loaderNoPages();
    void loaderImage();
    void loaderImageTooLarge();
    void loaderTiffFrames();

    void listAddsFilesAndRefusesDuplicates();
    void listLettersAndHues();
    void listRunnableRules();
    void listSnapshotOfOpenTab();
    void listUndoRedoWithoutSecondReading();
    void listRestrictedSnapshotOwnerPassword();
    void listPasswordChangesGeneration();
    void listClipboardImage();
    void listLimitOfInputs();

    void passwordDialogStates();
    void passwordDialogOwnerPassword();
    void passwordDialogRemoveAndLifetime();

    void previewsOfInputs();
    void previewsOfTiffFrames();

    void fileDropTakesImages();
    void chooserMultiple();
    void startPageMultiple();

    void deletionWhileLoading();
};

void AppToolInputsTest::initTestCase()
{
    // The pictures of a test must not come from the cache of an earlier one
    QPixmapCache::clear();
}

void AppToolInputsTest::loaderReadsPdf()
{
    AppDocumentFixture fixture;
    const QString filePath = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("three.pdf"), 3);

    const PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QCOMPARE(result.state, PDFAppToolInputState::Ready);
    QCOMPARE(result.kind, PDFAppToolInputKind::File);
    QCOMPARE(result.pageCount, 3);
    QVERIFY(result.document);
    QCOMPARE(result.document->getCatalog()->getPageCount(), size_t(3));

    // The size of the first page: the crop box of the samples is 300 x 400 points
    QCOMPARE(result.firstPageSize, QSizeF(300, 400));
    QCOMPARE(result.fileSize, QFileInfo(filePath).size());
    QCOMPARE(result.data.size(), QFileInfo(filePath).size());
    QVERIFY(result.reason.isEmpty());
    QVERIFY(!result.isEncrypted);
    QVERIFY(!result.isRepaired);
}

void AppToolInputsTest::loaderPasswordOfUser()
{
    AppDocumentFixture fixture;
    const QString filePath = AppDocumentSamples::userPassword(fixture.folder());

    // Without a password: protected, not wrong (378:2613 "Protected by a password")
    PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QCOMPARE(result.state, PDFAppToolInputState::PasswordRequired);
    QVERIFY(result.isEncrypted);
    QVERIFY(!result.isPasswordWrong);
    QVERIFY(!result.document);
    QVERIFY(!result.data.isEmpty());

    // A wrong password (398:27726 "Wrong password")
    result = AppToolInputsHelper::read(filePath, QStringLiteral("wrong"));
    QCOMPARE(result.state, PDFAppToolInputState::PasswordRequired);
    QVERIFY(result.isPasswordWrong);

    // The password of the user: the sample grants every permission (0xFFFFFFFC), so it
    // may be merged without the owner password (Q2 (c))
    result = AppToolInputsHelper::read(filePath, AppDocumentSamples::getUserPassword());
    QCOMPARE(result.state, PDFAppToolInputState::Ready);
    QCOMPARE(result.pageCount, 2);
    QVERIFY(result.isEncrypted);
}

void AppToolInputsTest::loaderRestrictedNeedsOwnerPassword()
{
    AppDocumentFixture fixture;

    // An empty password of the user opens the file, printing (bit 3) and copying (bit 5) are
    // denied: the unencrypted result would lose them - Q2 (c), Table 22
    const QString filePath = AppDocumentSamples::ownerPassword(fixture.folder());
    PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QCOMPARE(result.state, PDFAppToolInputState::Restricted);
    QVERIFY(result.document);
    QVERIFY(!result.reason.isEmpty());
    QVERIFY(!PDFAppToolInputLoader::isPermitted(result.document.data(), nullptr));

    // The password of the user is not the owner password (an empty one, too)
    result = AppToolInputsHelper::read(filePath, QString(""), true);
    QCOMPARE(result.state, PDFAppToolInputState::Restricted);
    QVERIFY(result.isPasswordWrong);

    // The owner password authorizes everything (7.6.4.3.3)
    result = AppToolInputsHelper::read(filePath, AppDocumentSamples::getOwnerPassword(), true);
    QCOMPARE(result.state, PDFAppToolInputState::Ready);
    QVERIFY(result.document);
    QCOMPARE(result.document->getStorage().getSecurityHandler()->getAuthorizationResult(), pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized);
    QVERIFY(PDFAppToolInputLoader::isPermitted(result.document.data(), nullptr));
}

void AppToolInputsTest::loaderAssembleDenied()
{
    AppDocumentFixture fixture;

    // Everything but Assemble (bit 11) granted: the tool itself is denied
    const uint32_t permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::Assemble);
    const QString filePath = AppToolInputsHelper::encrypted(fixture.folder(), QStringLiteral("no-assemble.pdf"), QString(), QStringLiteral("owner"), permissions);
    QVERIFY(!filePath.isEmpty());

    PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QCOMPARE(result.state, PDFAppToolInputState::Restricted);
    QVERIFY(result.reason.contains(QStringLiteral("assembled")));

    // Every permission granted to the user: no owner password is needed
    const QString allowedPath = AppToolInputsHelper::encrypted(fixture.folder(), QStringLiteral("all-allowed.pdf"), QString(), QStringLiteral("owner"), 0xFFFFFFFCu);
    result = AppToolInputsHelper::read(allowedPath);
    QCOMPARE(result.state, PDFAppToolInputState::Ready);
    QVERIFY(result.isEncrypted);
}

void AppToolInputsTest::loaderHostileFiles()
{
    AppDocumentFixture fixture;

    // Missing, empty, neither a PDF nor an image, a PDF signature with garbage after it
    PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(fixture.path(QStringLiteral("missing.pdf")));
    QCOMPARE(result.state, PDFAppToolInputState::Damaged);
    QVERIFY(!result.reason.isEmpty());

    result = AppToolInputsHelper::read(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("empty.pdf")), QByteArray()));
    QCOMPARE(result.state, PDFAppToolInputState::Damaged);

    result = AppToolInputsHelper::read(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("text.pdf")), QByteArray("This is not a document at all.\n")));
    QCOMPARE(result.state, PDFAppToolInputState::Unsupported);
    QVERIFY(!result.document);

    // 7.5.2: the signature makes it a PDF; nothing of it can be read
    QByteArray garbage("%PDF-1.7\n");
    garbage.append(QByteArray(4096, 'x'));
    result = AppToolInputsHelper::read(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("garbage.pdf")), garbage));
    QCOMPARE(result.state, PDFAppToolInputState::Damaged);
    QVERIFY(!result.document);

    // A canceled request reads nothing more
    PDFAppToolInputLoader::Request request;
    request.filePath = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("cancel.pdf"), 1);
    request.isCanceled = std::make_shared<std::atomic_bool>(true);
    result = PDFAppToolInputLoader::read(std::move(request));
    QVERIFY(!result.document);
    QVERIFY(result.state != PDFAppToolInputState::Ready);
}

void AppToolInputsTest::loaderRepairedFile()
{
    AppDocumentFixture fixture;
    const QString source = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("source.pdf"), 2);
    const QString damaged = AppDocumentSamples::damagedCopy(source, QStringLiteral("damaged.pdf"));

    // The permissive reader restores the objects: Ready with the Tag Repaired (K39)
    const PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(damaged);
    QCOMPARE(result.state, PDFAppToolInputState::Ready);
    QVERIFY(result.isRepaired);
    QCOMPARE(result.pageCount, 2);
}

void AppToolInputsTest::loaderNoPages()
{
    AppDocumentFixture fixture;
    const QString filePath = AppToolInputsHelper::noPages(fixture.folder());
    QVERIFY(!filePath.isEmpty());

    const PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QCOMPARE(result.state, PDFAppToolInputState::NoPages);
    QCOMPARE(result.pageCount, 0);
}

void AppToolInputsTest::loaderImage()
{
    AppDocumentFixture fixture;
    const QByteArray png = AppToolInputsHelper::createImage(QSize(200, 100), Qt::red, 150.0);
    const QString filePath = AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("picture.bin")), png);

    // An image by its content (no extension needed)
    const PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QCOMPARE(result.state, PDFAppToolInputState::Ready);
    QCOMPARE(result.kind, PDFAppToolInputKind::Image);
    QCOMPARE(result.pageCount, 1);
    QCOMPARE(result.imageSize, QSize(200, 100));
    QCOMPARE(result.imageFormat, QByteArray("png"));
    QCOMPARE(result.data, png);
    QVERIFY(qAbs(result.imageDpiX - 150.0) < 0.5);
    QVERIFY(qAbs(result.imageDpiY - 150.0) < 0.5);
    QVERIFY(!result.document);
}

void AppToolInputsTest::loaderImageTooLarge()
{
    AppDocumentFixture fixture;

    // 100 000 x 100 000 pixels: refused by the header (R15), no allocation
    const QString filePath = AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("huge.png")), AppToolInputsHelper::createHugePngHeader(100000, 100000));
    const PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(filePath);
    QVERIFY2(result.state == PDFAppToolInputState::Unsupported, qPrintable(result.reason));
    QVERIFY2(result.reason.contains(QStringLiteral("100000")), qPrintable(result.reason));
}

void AppToolInputsTest::loaderTiffFrames()
{
    // Owner answer Q6: every frame of a multi-page TIFF is a page of the input (the core counts
    // them by PDFImagePageFactory::readInfo the same way), at most 10 000 frames (R15)
    if (!QImageReader::supportedImageFormats().contains("tiff"))
    {
        QSKIP("The image formats of Qt have no TIFF plugin in this build.");
    }

    AppDocumentFixture fixture;
    const PDFAppToolInputLoader::Result result = AppToolInputsHelper::read(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("scan.tif")), AppToolInputsHelper::createTiff(3)));
    QVERIFY2(result.state == PDFAppToolInputState::Ready, qPrintable(result.reason));
    QCOMPARE(result.kind, PDFAppToolInputKind::Image);
    QCOMPARE(result.pageCount, 3);
    QCOMPARE(result.imageSize, QSize(1, 1));

    // More frames than the core writes: not supported, nothing is decoded
    const PDFAppToolInputLoader::Result tooMany = AppToolInputsHelper::read(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("long.tif")), AppToolInputsHelper::createTiff(PDFAppToolInputLoader::MAXIMUM_IMAGE_FRAMES + 1)));
    QCOMPARE(tooMany.state, PDFAppToolInputState::Unsupported);
    QCOMPARE(tooMany.pageCount, 0);
}

void AppToolInputsTest::listAddsFilesAndRefusesDuplicates()
{
    AppDocumentFixture fixture;
    const QString first = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("first.pdf"), 2);
    const QString second = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("second.pdf"), 3);

    PDFAppToolInputList list(nullptr);
    QSignalSpy changedSpy(&list, &PDFAppToolInputList::inputsChanged);
    const std::vector<PDFAppToolInputList::AddResult> results = list.addFiles({ first, second });
    QCOMPARE(results.size(), size_t(2));
    QVERIFY(!results[0].id.isEmpty());
    QVERIFY(results[0].id != results[1].id);
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(list.getCount(), 2);
    QCOMPARE(list.getInput(0)->state, PDFAppToolInputState::Loading);
    QCOMPARE(list.getInputIds(), QStringList({ results[0].id, results[1].id }));

    // K29: the same file (another spelling of its path) is refused with the text of the status bar
    const PDFAppToolInputList::AddResult duplicate = list.addFile(QDir(fixture.folder()).absoluteFilePath(QStringLiteral("./first.pdf")));
    QVERIFY(duplicate.id.isEmpty());
    QCOMPARE(duplicate.refusal, PDFAppToolInputList::Refusal::Duplicate);
    QCOMPARE(PDFAppToolInputList::getRefusalText(duplicate.refusal, duplicate.name), QStringLiteral("first.pdf is already in the list"));
    QCOMPARE(list.getCount(), 2);

    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));
    QCOMPARE(list.getInput(0)->state, PDFAppToolInputState::Ready);
    QCOMPARE(list.getInput(1)->pageCount, 3);
    QCOMPARE(list.getUsableCount(), 2);
    QCOMPARE(list.getTotalPageCount(), 5);
    QCOMPARE(list.getTotalSize(), QFileInfo(first).size() + QFileInfo(second).size());

    // The input of the core (R16): the id, the name, the bytes of the file
    const pdf::PDFDocumentToolInputDocument input = list.getInput(1)->createToolInputDocument();
    QCOMPARE(input.inputId, results[1].id);
    QCOMPARE(input.fileName, QStringLiteral("second.pdf"));
    QVERIFY(!input.isModified);
    QCOMPARE(input.originalData.size(), QFileInfo(second).size());
}

void AppToolInputsTest::listLettersAndHues()
{
    // R20: A ... Z, AA, AB ...
    QCOMPARE(PDFAppToolInputList::getLetter(0), QStringLiteral("A"));
    QCOMPARE(PDFAppToolInputList::getLetter(25), QStringLiteral("Z"));
    QCOMPARE(PDFAppToolInputList::getLetter(26), QStringLiteral("AA"));
    QCOMPARE(PDFAppToolInputList::getLetter(27), QStringLiteral("AB"));
    QCOMPARE(PDFAppToolInputList::getLetter(51), QStringLiteral("AZ"));
    QCOMPARE(PDFAppToolInputList::getLetter(52), QStringLiteral("BA"));
    QCOMPARE(PDFAppToolInputList::getLetter(701), QStringLiteral("ZZ"));
    QCOMPARE(PDFAppToolInputList::getLetter(702), QStringLiteral("AAA"));

    // K15 / K40: A blue, B green, C violet (378:1049), then teal; never red or amber
    QCOMPARE(PDFAppToolInputList::getHue(0), pdf::PDFAccentHue::Blue);
    QCOMPARE(PDFAppToolInputList::getHue(1), pdf::PDFAccentHue::Green);
    QCOMPARE(PDFAppToolInputList::getHue(2), pdf::PDFAccentHue::Violet);
    QCOMPARE(PDFAppToolInputList::getHue(3), pdf::PDFAccentHue::Teal);
    for (int i = 0; i < 1000; ++i)
    {
        const pdf::PDFAccentHue hue = PDFAppToolInputList::getHue(i);
        QVERIFY(hue != pdf::PDFAccentHue::Red && hue != pdf::PDFAccentHue::Amber);
        if (i > 0)
        {
            QVERIFY(hue != PDFAppToolInputList::getHue(i - 1));
        }
    }

    // 378:2613: a damaged input is neutral, a protected one keeps its color
    PDFAppToolInput input;
    input.state = PDFAppToolInputState::Damaged;
    QCOMPARE(PDFAppToolInputList::getHue(3, &input), pdf::PDFAccentHue::Neutral);
    input.state = PDFAppToolInputState::PasswordRequired;
    QCOMPARE(PDFAppToolInputList::getHue(1, &input), pdf::PDFAccentHue::Green);
}

void AppToolInputsTest::listRunnableRules()
{
    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    list.addFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("one.pdf"), 1));
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));

    // K22: one usable input is not enough
    QVERIFY(!list.isRunnable());

    const QString protectedId = list.addFile(AppDocumentSamples::userPassword(fixture.folder())).id;
    const QString noPagesId = list.addFile(AppToolInputsHelper::noPages(fixture.folder())).id;
    QVERIFY(!list.isRunnable());
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));

    // 378:2613: a protected file blocks the run ("2 need attention" counts it); a file
    // without pages does not need attention, it only does not count (K39)
    QCOMPARE(list.getAttentionCount(), 1);
    QCOMPARE(list.getUsableCount(), 1);
    QVERIFY(!list.isRunnable());

    QVERIFY(list.setPassword(protectedId, AppDocumentSamples::getUserPassword()));
    QVERIFY(AppToolInputsHelper::waitForState(&list, protectedId, PDFAppToolInputState::Ready));
    QCOMPARE(list.getAttentionCount(), 0);
    QCOMPARE(list.getUsableCount(), 2);
    QVERIFY(list.isRunnable());
    QCOMPARE(list.findInput(noPagesId)->state, PDFAppToolInputState::NoPages);
}

void AppToolInputsTest::listSnapshotOfOpenTab()
{
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("tab.pdf"), 3));
    QVERIFY(tab && tab->getDocument() && tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFDocumentPointer snapshotDocument = document->getDocument();

    PDFAppToolInputList list(nullptr);
    const PDFAppToolInputList::AddResult result = list.addOpenDocument(document);
    QVERIFY(!result.id.isEmpty());
    const PDFAppToolInput* input = list.findInput(result.id);
    QCOMPARE(input->kind, PDFAppToolInputKind::OpenTab);
    QCOMPARE(input->state, PDFAppToolInputState::Ready);
    QCOMPARE(input->documentId, document->getId());
    QCOMPARE(input->pageCount, 3);
    QVERIFY(input->document == snapshotDocument);
    QVERIFY(!input->isModified);

    // The tab is added once (K29: "already in the list"), also as its file
    QCOMPARE(list.addOpenDocument(document).refusal, PDFAppToolInputList::Refusal::Duplicate);
    QCOMPARE(list.addFile(document->getFilePath()).refusal, PDFAppToolInputList::Refusal::Duplicate);

    // R11: a change of the tab does not change the input (the snapshot of the moment it was added)
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocument onePage = reader.readFromFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("other.pdf"), 1));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(onePage));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QCOMPARE(document->getPageCount(), 1);
    input = list.findInput(result.id);
    QCOMPARE(input->pageCount, 3);
    QVERIFY(input->document == snapshotDocument);

    // The input of the core is the working copy of the moment (unsaved changes: none then)
    QVERIFY(!input->createToolInputDocument().isModified);

    // A tab closes, its input stays (the list holds no pointer to the document)
    pdf::PDFAppDocumentTab* second = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("second-tab.pdf"), 2));
    QVERIFY(second && second->getDocument() && second->getDocument()->isReady());
    const QString secondId = list.addOpenDocument(second->getDocument()).id;
    QVERIFY(!secondId.isEmpty());
    QVERIFY(fixture.window->closeTab(second));
    AppDocumentFixture::settle();
    input = list.findInput(secondId);
    QVERIFY(input);
    QCOMPARE(input->pageCount, 2);
    QVERIFY(input->isUsable());
    QVERIFY(input->document);
}

void AppToolInputsTest::listRestrictedSnapshotOwnerPassword()
{
    AppDocumentFixture fixture;

    // Q15: a tab of a restricted file (printing and copying denied, Table 22) with unsaved
    // changes - its working copy is no file, which could be read again with a password
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::ownerPassword(fixture.folder()));
    QVERIFY(tab && tab->getDocument() && tab->getDocument()->isReady());
    pdf::PDFAppDocument* document = tab->getDocument();
    pdf::PDFDocumentBuilder builder(document->getDocument().data());
    builder.setDocumentTitle(QStringLiteral("Changed"));
    pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
    QVERIFY(document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::None), QStringLiteral("Change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QVERIFY(document->isModified());

    PDFAppToolInputList list(nullptr);
    const QString id = list.addOpenDocument(document).id;
    QVERIFY(!id.isEmpty());
    QCOMPARE(list.findInput(id)->state, PDFAppToolInputState::Restricted);
    QVERIFY(list.findInput(id)->isModified);
    QVERIFY(!list.findInput(id)->isOwnerPasswordVerified);
    QVERIFY(!list.findInput(id)->createToolInputDocument().isOwnerPasswordVerified);

    // The dialog asks for the owner password; a wrong one is its error state
    QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openDialog(&list, id, fixture.window);
    QVERIFY(dialog);
    QVERIFY(dialog->isOwnerPassword());
    QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("not-the-owner"));
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog && !dialog->isBusy(); }));
    QCOMPARE(list.findInput(id)->state, PDFAppToolInputState::Restricted);
    QVERIFY(list.findInput(id)->isPasswordWrong);
    QCOMPARE(dialog->getErrorText(), QStringLiteral("Wrong password – check Caps Lock and try again"));

    // The owner password verified: Ready, the snapshot with its unsaved change stays the input
    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getOwnerPassword());
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::Ready));
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
    const PDFAppToolInput* input = list.findInput(id);
    QVERIFY(input->isOwnerPasswordVerified);
    QVERIFY(input->document == changed);
    QVERIFY(input->isUsable());
    QCOMPARE(input->pageCount, 2);

    // The core gets the flag: the same document is refused without it (Q2 (c)), permitted with it
    const pdf::PDFDocumentToolInputDocument coreInput = input->createToolInputDocument();
    QVERIFY(coreInput.isOwnerPasswordVerified);
    QVERIFY(coreInput.isModified);
    QCOMPARE(pdf::PDFDocumentMerger::getPermission(coreInput.document.data(), coreInput.isOwnerPasswordVerified), pdf::PDFDocumentMerger::Permission::Permitted);
    QVERIFY(pdf::PDFDocumentMerger::getPermission(coreInput.document.data(), false) != pdf::PDFDocumentMerger::Permission::Permitted);
}

void AppToolInputsTest::listUndoRedoWithoutSecondReading()
{
    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    const QString plainId = list.addFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("plain.pdf"), 2)).id;
    const QString protectedId = list.addFile(AppDocumentSamples::userPassword(fixture.folder())).id;
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));
    QVERIFY(list.setPassword(protectedId, AppDocumentSamples::getUserPassword()));
    QVERIFY(AppToolInputsHelper::waitForState(&list, protectedId, PDFAppToolInputState::Ready));
    QCOMPARE(list.getStartedReadingCount(), 3);
    const pdf::PDFDocumentPointer protectedDocument = list.findInput(protectedId)->document;

    // R12: removed = parked with its document and its accepted password
    QSignalSpy spy(&list, &PDFAppToolInputList::inputsChanged);
    QVERIFY(list.removeInput(protectedId));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(list.getInputIds(), QStringList({ plainId }));
    QVERIFY(list.isParked(protectedId));
    QVERIFY(!list.containsFile(AppDocumentSamples::userPassword(fixture.folder())));

    // Undo: back without a second reading and without a second question for the password
    QVERIFY(list.setInputIds({ plainId, protectedId }));
    QCOMPARE(list.getInputIds(), QStringList({ plainId, protectedId }));
    QVERIFY(!list.isParked(protectedId));
    QCOMPARE(list.findInput(protectedId)->state, PDFAppToolInputState::Ready);
    QVERIFY(list.findInput(protectedId)->document == protectedDocument);
    QCOMPARE(list.getStartedReadingCount(), 3);

    // Redo, Undo again, a reorder: still no reading; the same ids are no change
    QVERIFY(list.setInputIds({ plainId }));
    QVERIFY(list.setInputIds({ protectedId, plainId }));
    QVERIFY(!list.setInputIds({ protectedId, plainId }));
    QCOMPARE(list.getStartedReadingCount(), 3);

    // Unknown ids and ids given twice are skipped
    QVERIFY(list.setInputIds({ QStringLiteral("unknown"), plainId, plainId }));
    QCOMPARE(list.getInputIds(), QStringList({ plainId }));

    // The history does not refer to the parked input any more: it is forgotten
    list.dropParkedInputs({});
    QVERIFY(!list.findInput(protectedId));
    QCOMPARE(list.getParkedCount(), 0);
    QVERIFY(!list.setInputIds({ plainId }));
}

void AppToolInputsTest::listPasswordChangesGeneration()
{
    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    const QString id = list.addFile(AppDocumentSamples::userPassword(fixture.folder())).id;
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::PasswordRequired));

    // K33: the content of the input changes under the same id - a new generation, a new key
    const quint64 generation = list.getGeneration();
    const QString key = list.getPictureKey(id, 0, QSize(73, 100), 1.0);
    QSignalSpy spy(&list, &PDFAppToolInputList::inputChanged);
    QVERIFY(list.setPassword(id, QStringLiteral("wrong")));
    QCOMPARE(list.findInput(id)->state, PDFAppToolInputState::Loading);
    QVERIFY(!list.setPassword(id, QStringLiteral("again")));    // a reading runs
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::PasswordRequired));
    QVERIFY(list.findInput(id)->isPasswordWrong);
    QVERIFY(list.getGeneration() > generation);

    QVERIFY(list.setPassword(id, AppDocumentSamples::getUserPassword()));
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::Ready));
    QVERIFY(!list.findInput(id)->isPasswordWrong);
    QVERIFY(list.getPictureKey(id, 0, QSize(73, 100), 1.0) != key);
    QVERIFY(spy.count() >= 4);

    // A ready input waits for no password
    QVERIFY(!list.setPassword(id, AppDocumentSamples::getUserPassword()));

    // A move is a change of the inputs as well
    const quint64 beforeMove = list.getGeneration();
    list.addFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1));
    QVERIFY(list.moveInput(id, 1));
    QVERIFY(list.getGeneration() > beforeMove);
    QCOMPARE(list.indexOf(id), 1);
    QVERIFY(!list.moveInput(id, 7));    // clamped to the last place, where it is already
}

void AppToolInputsTest::listClipboardImage()
{
    PDFAppToolInputList list(nullptr);
    QImage image(QSize(40, 20), QImage::Format_ARGB32);
    image.fill(Qt::blue);

    // Plan 5.3: stored as PNG bytes, read as an image of one page
    const PDFAppToolInputList::AddResult result = list.addImage(image, QString());
    QVERIFY(!result.id.isEmpty());
    QVERIFY(AppToolInputsHelper::waitForState(&list, result.id, PDFAppToolInputState::Ready));
    const PDFAppToolInput* input = list.findInput(result.id);
    QCOMPARE(input->kind, PDFAppToolInputKind::Clipboard);
    QCOMPARE(input->name, QStringLiteral("Pasted image 1"));
    QCOMPARE(input->imageSize, QSize(40, 20));
    QCOMPARE(input->imageFormat, QByteArray("png"));
    QCOMPARE(input->pageCount, 1);
    QVERIFY(input->isImage());

    QImage decoded;
    QVERIFY(decoded.loadFromData(input->imageData, "PNG"));
    QCOMPARE(decoded.pixelColor(10, 10), QColor(Qt::blue));

    const pdf::PDFDocumentToolInputImage coreImage = input->createToolInputImage();
    QCOMPARE(coreImage.inputId, result.id);
    QCOMPARE(coreImage.data, input->imageData);
    QCOMPARE(coreImage.format, QByteArray("png"));

    // An empty image is refused
    QCOMPARE(list.addImage(QImage(), QStringLiteral("x")).refusal, PDFAppToolInputList::Refusal::Invalid);
}

void AppToolInputsTest::listLimitOfInputs()
{
    // Plan 6: at most 1 000 inputs in the GUI; the 1 001st is refused (images of the
    // clipboard: no file is needed)
    PDFAppToolInputList list(nullptr);
    QImage image(QSize(1, 1), QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int i = 0; i < PDFAppToolInputList::MAXIMUM_INPUTS; ++i)
    {
        QVERIFY(!list.addImage(image, QStringLiteral("i")).id.isEmpty());
    }
    QCOMPARE(list.getCount(), 1000);
    const PDFAppToolInputList::AddResult result = list.addImage(image, QStringLiteral("last"));
    QCOMPARE(result.refusal, PDFAppToolInputList::Refusal::TooMany);
    QCOMPARE(list.getCount(), 1000);
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));
}

void AppToolInputsTest::passwordDialogStates()
{
    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    const QString id = list.addFile(AppDocumentSamples::userPassword(fixture.folder())).id;
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::PasswordRequired));

    QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openDialog(&list, id, fixture.window);
    QVERIFY(dialog);
    AppDocumentFixture::settle();

    // 398:28116: 460 wide, the badge alert-triangle (Warning), the texts of the frame
    QCOMPARE(dialog->getCardWidth(), 460);
    QVERIFY(dialog->isBadgeVisible());
    QVERIFY(!dialog->isOwnerPassword());
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Password for user-password.pdf"));
    QCOMPARE(dialog->getRemoveButton()->text(), QStringLiteral("Remove File"));
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Cancel"));
    QCOMPARE(dialog->getUnlockButton()->text(), QStringLiteral("Unlock"));
    QVERIFY(dialog->getPasswordField()->echoMode() == QLineEdit::Password);

    // Unlock is disabled without a password
    QVERIFY(!dialog->getUnlockButton()->isEnabled());
    QVERIFY(dialog->getErrorText().isEmpty());

    // A wrong password (owner 2026-11-08, 398:28125): the error of the frame, the field enabled
    // again in its Error state with the typed text kept and all selected
    QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("wrong"));
    QVERIFY(dialog->getUnlockButton()->isEnabled());
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(dialog->isBusy());
    QVERIFY(!dialog->getPasswordField()->isEnabled());
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog && !dialog->isBusy(); }));
    QCOMPARE(dialog->getErrorText(), QStringLiteral("Wrong password – check Caps Lock and try again"));
    QVERIFY(dialog->getPasswordField()->isEnabled());
    QVERIFY(dialog->getPasswordField()->isInvalid());
    QCOMPARE(dialog->getPasswordField()->text(), QStringLiteral("wrong"));
    QCOMPARE(dialog->getPasswordField()->selectionStart(), 0);
    QCOMPARE(dialog->getPasswordField()->selectionLength(), 5);
    QVERIFY(dialog->getUnlockButton()->isEnabled());

    // 398:28116: the label Regular 12 (398:28124), the field 34 (398:28125), the error Medium
    // 11 (398:28131), the parts 14 apart; no note of the owner password
    pdf::PDFLabel* label = dialog->findChild<pdf::PDFLabel*>(QStringLiteral("passwordLabel"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Password"));
    QCOMPARE(label->font().pixelSize(), 12);
    QCOMPARE(label->font().weight(), QFont::Normal);
    QCOMPARE(dialog->getPasswordField()->height(), 34);
    pdf::PDFLabel* errorText = dialog->findChild<pdf::PDFLabel*>(QStringLiteral("errorText"));
    QVERIFY(errorText);
    QCOMPARE(errorText->font().pixelSize(), 11);
    QCOMPARE(errorText->font().weight(), QFont::Medium);
    QCOMPARE(dialog->getPasswordField()->geometry().top() - label->geometry().bottom() - 1, 14);
    QVERIFY(!dialog->findChild<pdf::PDFLabel*>(QStringLiteral("noteText")));

    // A new password replaces the selected one and hides the error; the right one closes the
    // dialog and the input is read
    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getUserPassword());
    QCOMPARE(dialog->getPasswordField()->text(), AppDocumentSamples::getUserPassword());
    QVERIFY(dialog->getErrorText().isEmpty());
    QVERIFY(!dialog->getPasswordField()->isInvalid());
    QSignalSpy acceptedSpy(dialog.data(), &QDialog::accepted);
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([&acceptedSpy]() { return acceptedSpy.count() == 1; }));
    QCOMPARE(list.findInput(id)->state, PDFAppToolInputState::Ready);
    AppDocumentFixture::settle();
    QVERIFY(!dialog);

    // A dialog for an input, which waits for no password, is not opened
    QVERIFY(!pdf::PDFAppPasswordDialog::openDialog(&list, id, fixture.window));
}

void AppToolInputsTest::passwordDialogOwnerPassword()
{
    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    const QString id = list.addFile(AppDocumentSamples::ownerPassword(fixture.folder())).id;
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::Restricted));
    QCOMPARE(PDFAppToolInputList::getStateText(PDFAppToolInputState::Restricted), QStringLiteral("Restricted – enter the owner password"));

    QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openDialog(&list, id, fixture.window);
    QVERIFY(dialog);
    QVERIFY(dialog->isOwnerPassword());
    QCOMPARE(dialog->accessibleName(), QStringLiteral("Owner password for owner-password.pdf"));

    // pages-merge-password-owner 1390:34569 (bits 3 and 5 of the sample cleared, Table 22):
    // the subtitle 1390:34800, the label 1390:34801 Regular 12 text/secondary, the field 34
    // (1390:34802), the note 1390:34806 Regular 11 text/muted
    QCOMPARE(dialog->getText(), QStringLiteral("Printing and copying are not allowed, and the merged PDF would lose these restrictions."));
    pdf::PDFLabel* label = dialog->findChild<pdf::PDFLabel*>(QStringLiteral("passwordLabel"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Owner password"));
    QCOMPARE(label->font().pixelSize(), 12);
    QCOMPARE(label->font().weight(), QFont::Normal);
    QCOMPARE(dialog->getPasswordField()->height(), 34);
    pdf::PDFLabel* note = dialog->findChild<pdf::PDFLabel*>(QStringLiteral("noteText"));
    QVERIFY(note && !note->isHidden());
    QCOMPARE(note->text(), QStringLiteral("The password is used only to read the file and is not stored."));
    QCOMPARE(note->font().pixelSize(), 11);

    // The password of the user (empty) is not the owner password: still restricted, the error
    QTest::keyClicks(dialog->getPasswordField(), QStringLiteral("not-the-owner"));
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog && !dialog->isBusy(); }));
    QCOMPARE(list.findInput(id)->state, PDFAppToolInputState::Restricted);
    QVERIFY(!dialog->getErrorText().isEmpty());

    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getOwnerPassword());
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(AppToolInputsHelper::waitForState(&list, id, PDFAppToolInputState::Ready));
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(PDFAppToolInputLoader::isPermitted(list.findInput(id)->document.data(), nullptr));
}

void AppToolInputsTest::passwordDialogRemoveAndLifetime()
{
    AppDocumentFixture fixture;
    PDFAppToolInputList* list = new PDFAppToolInputList(nullptr);
    const QString id = list->addFile(AppDocumentSamples::userPassword(fixture.folder())).id;
    QVERIFY(AppToolInputsHelper::waitForState(list, id, PDFAppToolInputState::PasswordRequired));

    // Remove File: the dialog closes and asks the tool to remove the input (it never removes it)
    QPointer<pdf::PDFAppPasswordDialog> dialog = pdf::PDFAppPasswordDialog::openDialog(list, id, fixture.window);
    QSignalSpy removeSpy(dialog.data(), &pdf::PDFAppPasswordDialog::removeRequested);
    QTest::mouseClick(dialog->getRemoveButton(), Qt::LeftButton);
    QCOMPARE(removeSpy.count(), 1);
    QCOMPARE(removeSpy.at(0).at(0).toString(), id);
    QCOMPARE(list->indexOf(id), 0);
    AppDocumentFixture::settle();
    QVERIFY(!dialog);

    // The input removed elsewhere: the dialog closes
    dialog = pdf::PDFAppPasswordDialog::openDialog(list, id, fixture.window);
    QVERIFY(dialog);
    list->removeInput(id);
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
    QVERIFY(list->setInputIds({ id }));

    // The list deleted while the dialog is open, and while it reads the password
    dialog = pdf::PDFAppPasswordDialog::openDialog(list, id, fixture.window);
    QTest::keyClicks(dialog->getPasswordField(), AppDocumentSamples::getUserPassword());
    QTest::mouseClick(dialog->getUnlockButton(), Qt::LeftButton);
    QVERIFY(dialog->isBusy());
    delete list;
    AppDocumentFixture::settle();
    QVERIFY(!dialog);

    // The window deleted while a dialog is open
    PDFAppToolInputList list2(nullptr);
    const QString id2 = list2.addFile(AppDocumentSamples::userPassword(fixture.folder())).id;
    QVERIFY(AppToolInputsHelper::waitForState(&list2, id2, PDFAppToolInputState::PasswordRequired));
    dialog = pdf::PDFAppPasswordDialog::openDialog(&list2, id2, fixture.window);
    QVERIFY(dialog);
    delete fixture.window.data();
    AppDocumentFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(list2.findInput(id2)->state, PDFAppToolInputState::PasswordRequired);
}

void AppToolInputsTest::previewsOfInputs()
{
    // The allocation limit of Qt is one static value of the process: the readings of the inputs
    // and the pictures of their pages run in workers and never write it (a write races with
    // every reader of it in the other workers, RS2CORE / RS2APP)
    const int processLimit = QImageReader::allocationLimit();
    const int testLimit = processLimit + 64;
    QImageReader::setAllocationLimit(testLimit);
    const auto restoreLimit = qScopeGuard([processLimit]() { QImageReader::setAllocationLimit(processLimit); });

    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    const QString pdfId = list.addFile(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("pages.pdf"), 2)).id;
    const QString imageId = list.addFile(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("red.png")), AppToolInputsHelper::createImage(QSize(200, 100), Qt::red, 0.0))).id;
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));
    QCOMPARE(list.findInput(imageId)->kind, PDFAppToolInputKind::Image);

    // A protected input has no picture source (nothing can be drawn of it)
    QVERIFY(list.getPictureSource(pdfId, 1).isValid());
    QVERIFY(!list.getPictureSource(pdfId, 2).isValid());
    QVERIFY(list.getPictureSource(imageId, 0).isValid());
    QVERIFY(list.getPictureSource(imageId, 0).document.isNull());

    QWidget host;
    host.resize(400, 400);
    QListView* view = new QListView(&host);
    view->resize(400, 400);
    pdftest::AppToolInputsPicturesModel* model = new pdftest::AppToolInputsPicturesModel(&list, { { pdfId, 0 }, { pdfId, 1 }, { imageId, 0 } }, &host);
    model->setPictureSize(QSize(100, 100), 1.0);
    view->setModel(model);
    pdf::PDFAppThumbnailScheduler* scheduler = new pdf::PDFAppThumbnailScheduler(model, view, &host);
    Q_UNUSED(scheduler);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    // The painted rows ask for their pictures (as the delegate of the grid does)
    for (int row = 0; row < 3; ++row)
    {
        QVERIFY(!model->data(model->index(row), pdf::PDFComponentRoles::PixmapRole).isValid());
    }
    QVERIFY(AppToolShellFixture::waitFor([model]() { return model->hasPicture(0) && model->hasPicture(1) && model->hasPicture(2); }, 30000));

    // The page 300 x 400 is upright, the image 200 x 100 lies, both fit the slot 100 x 100;
    // the image is decoded from its bytes (red), the page is white paper
    QPixmap page;
    QPixmap picture;
    QVERIFY(QPixmapCache::find(model->getKey(0), &page));
    QVERIFY(QPixmapCache::find(model->getKey(2), &picture));
    QCOMPARE(page.size(), QSize(75, 100));
    QCOMPARE(picture.size(), QSize(100, 50));
    const QImage pictureImage = picture.toImage();
    QCOMPARE(QColor(pictureImage.pixel(50, 25)), QColor(Qt::red));
    const QImage pageImage = page.toImage();
    QCOMPARE(QColor(pageImage.pixel(2, 98)), QColor(Qt::white));
    QCOMPARE(QImageReader::allocationLimit(), testLimit);
}

void AppToolInputsTest::previewsOfTiffFrames()
{
    // Q6: every frame of a multi-page TIFF is a page of the input and has its own picture - the
    // worker decodes the frame of the row (frame i of the sample is the gray value i, black is
    // zero: a dark picture, never the white picture of a frame, which cannot be decoded)
    if (!QImageReader::supportedImageFormats().contains("tiff"))
    {
        QSKIP("The image formats of Qt have no TIFF plugin in this build.");
    }

    AppDocumentFixture fixture;
    PDFAppToolInputList list(nullptr);
    const QString tiffId = list.addFile(AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("scan.tif")), AppToolInputsHelper::createTiff(3))).id;
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));
    QCOMPARE(list.findInput(tiffId)->pageCount, 3);
    QVERIFY(list.getPictureSource(tiffId, 2).isValid());
    QVERIFY(!list.getPictureSource(tiffId, 3).isValid());

    QWidget host;
    host.resize(400, 400);
    QListView* view = new QListView(&host);
    view->resize(400, 400);
    pdftest::AppToolInputsPicturesModel* model = new pdftest::AppToolInputsPicturesModel(&list, { { tiffId, 0 }, { tiffId, 1 }, { tiffId, 2 } }, &host);
    model->setPictureSize(QSize(100, 100), 1.0);
    view->setModel(model);
    pdf::PDFAppThumbnailScheduler* scheduler = new pdf::PDFAppThumbnailScheduler(model, view, &host);
    Q_UNUSED(scheduler);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    for (int row = 0; row < 3; ++row)
    {
        QVERIFY(!model->data(model->index(row), pdf::PDFComponentRoles::PixmapRole).isValid());
    }
    QVERIFY(AppToolShellFixture::waitFor([model]() { return model->hasPicture(0) && model->hasPicture(1) && model->hasPicture(2); }, 30000));

    for (int row = 0; row < 3; ++row)
    {
        QPixmap picture;
        QVERIFY(QPixmapCache::find(model->getKey(row), &picture));
        QCOMPARE(picture.size(), QSize(100, 100));
        const QColor color(picture.toImage().pixel(50, 50));
        QVERIFY2(color.red() == row && color.green() == row && color.blue() == row, qPrintable(QStringLiteral("frame %1: %2").arg(row).arg(color.name())));
    }
}

void AppToolInputsTest::fileDropTakesImages()
{
    AppDocumentFixture fixture;
    const QString pdfPath = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("drop.pdf"), 1);
    const QString imagePath = AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("drop.png")), AppToolInputsHelper::createImage(QSize(10, 10), Qt::green, 0.0));
    const QString textPath = AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("drop.txt")), QByteArray("text"));

    QMimeData mimeData;
    mimeData.setUrls({ QUrl::fromLocalFile(pdfPath), QUrl::fromLocalFile(imagePath), QUrl::fromLocalFile(textPath) });

    // The window takes PDF files only; the inputs of a tool take images too (K8)
    const pdf::PDFAppFileDrop::Result pdfOnly = pdf::PDFAppFileDrop::getFiles(&mimeData);
    QCOMPARE(pdfOnly.files, QStringList({ QFileInfo(pdfPath).absoluteFilePath() }));
    QCOMPARE(pdfOnly.refused.size(), size_t(2));

    const pdf::PDFAppFileDrop::Result inputs = pdf::PDFAppFileDrop::getFiles(&mimeData, pdf::PDFAppFileDrop::MAXIMUM_FILES, true);
    QCOMPARE(inputs.files, QStringList({ QFileInfo(pdfPath).absoluteFilePath(), QFileInfo(imagePath).absoluteFilePath() }));
    QCOMPARE(inputs.refused.size(), size_t(1));
    QCOMPARE(inputs.refused.front().refusal, pdf::PDFAppFileDrop::Refusal::NotPdfOrImage);
}

void AppToolInputsTest::chooserMultiple()
{
    AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* first = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report.pdf"), 3));
    QVERIFY(first && first->getDocument()->isReady());

    PDFAppToolInputList list(nullptr);
    QPointer<pdf::PDFAppToolInputChooser> chooser = new pdf::PDFAppToolInputChooser(fixture.context.get(), QStringLiteral("pages.merge"), &list, fixture.window);
    chooser->resize(802, 400);

    // 551:149074: the open document unchecked, no file, the button Disabled
    std::vector<pdf::PDFAppToolDocumentRow*> rows = chooser->getDocumentRows();
    QCOMPARE(rows.size(), size_t(1));
    QVERIFY(rows[0]->isCheckBoxVisible());
    QVERIFY(!rows[0]->isChecked());
    QVERIFY(rows[0]->getActionText().isEmpty());
    QCOMPARE(chooser->getFileGroupLabel()->text(), QStringLiteral("FILES FROM DISK – none added yet"));
    QCOMPARE(chooser->getDocumentGroupLabel()->text(), QStringLiteral("OPEN DOCUMENTS"));
    QVERIFY(!chooser->getOpenToolButton()->isEnabled());
    QCOMPARE(chooser->getOpenToolButton()->text(), QStringLiteral("Open Merge"));

    // Checking the document: a snapshot in the list, "1st in the result", the handle
    rows[0]->click();
    AppDocumentFixture::settle();
    QCOMPARE(list.getCount(), 1);
    QCOMPARE(list.getInput(0)->documentId, first->getDocument()->getId());
    rows = chooser->getDocumentRows();
    QVERIFY(rows[0]->isChecked());
    QCOMPARE(rows[0]->getActionText(), QStringLiteral("1st in the result"));
    QCOMPARE(rows[0]->getTrailing(), pdf::PDFAppToolDocumentRow::Trailing::Handle);
    QCOMPARE(chooser->getSummaryLabel()->text(), QStringLiteral("1 document · Merge needs at least two documents – check another open document or add a file"));
    QVERIFY(!chooser->getOpenToolButton()->isEnabled());

    // A file: checked, with remove; two documents enable the button (531:3583)
    const QString filePath = AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Appendix.pdf"), 2);
    chooser->addFiles({ filePath });
    QVERIFY(AppToolInputsHelper::waitForLoaded(&list));
    AppDocumentFixture::settle();
    std::vector<pdf::PDFAppToolDocumentRow*> fileRows = chooser->getFileRows();
    QCOMPARE(fileRows.size(), size_t(1));
    QVERIFY(fileRows[0]->isChecked());
    QVERIFY(fileRows[0]->getRemoveButton());
    QCOMPARE(fileRows[0]->getActionText(), QStringLiteral("2nd in the result"));
    QCOMPARE(chooser->getFileGroupLabel()->text(), QStringLiteral("FILES FROM DISK"));
    QCOMPARE(chooser->getSummaryLabel()->text(), QStringLiteral("2 documents · 5 pages · drag the handles to change the order"));
    QVERIFY(chooser->getOpenToolButton()->isEnabled());
    QCOMPARE(chooser->getOpenToolButton()->text(), QStringLiteral("Open Merge with 2 Documents"));
    QCOMPARE(chooser->getDocumentCount(), 2);

    // K29: the same file again is refused, the summary says it
    chooser->addFiles({ filePath });
    AppDocumentFixture::settle();
    QCOMPARE(list.getCount(), 2);
    QVERIFY(chooser->getSummaryLabel()->text().startsWith(QStringLiteral("Appendix.pdf is already in the list")));

    // Alt+Up on the file: it is the first in the result now
    fileRows = chooser->getFileRows();
    fileRows[0]->setFocus();
    QTest::keyClick(fileRows[0], Qt::Key_Up, Qt::AltModifier);
    AppDocumentFixture::settle();
    QCOMPARE(list.getInput(0)->name, QStringLiteral("Appendix.pdf"));
    QCOMPARE(chooser->getFileRows()[0]->getActionText(), QStringLiteral("1st in the result"));
    QCOMPARE(chooser->getDocumentRows()[0]->getActionText(), QStringLiteral("2nd in the result"));

    // Unchecking the file parks it; checking it again reads nothing (R12)
    const int readings = list.getStartedReadingCount();
    chooser->getFileRows()[0]->click();
    AppDocumentFixture::settle();
    QCOMPARE(list.getCount(), 1);
    QCOMPARE(chooser->getFileRows().size(), size_t(1));
    QVERIFY(!chooser->getFileRows()[0]->isChecked());
    QVERIFY(!chooser->getOpenToolButton()->isEnabled());
    chooser->getFileRows()[0]->click();
    AppDocumentFixture::settle();
    QCOMPARE(list.getCount(), 2);
    QCOMPARE(list.getStartedReadingCount(), readings);

    // Remove (x): the file is gone, also from the parked inputs
    QTest::mouseClick(chooser->getFileRows()[0]->getRemoveButton(), Qt::LeftButton);
    AppDocumentFixture::settle();
    QCOMPARE(list.getCount(), 1);
    QCOMPARE(list.getParkedCount(), 0);
    QVERIFY(chooser->getFileRows().empty());

    // The order texts of the frames and beyond
    QCOMPARE(pdf::PDFAppToolInputChooser::getOrderText(2), QStringLiteral("3rd in the result"));
    QCOMPARE(pdf::PDFAppToolInputChooser::getOrderText(10), QStringLiteral("11th in the result"));
    QCOMPARE(pdf::PDFAppToolInputChooser::getOrderText(21), QStringLiteral("22nd in the result"));

    delete chooser.data();
}

void AppToolInputsTest::startPageMultiple()
{
    AppDocumentFixture fixture;
    pdf::PDFToolStartPage* page = new pdf::PDFToolStartPage(fixture.context.get(), QStringLiteral("pages.merge"), fixture.window);
    QVERIFY(page->getInputChooser());
    QVERIFY(page->getInputList());
    QVERIFY(!page->getChooser());
    QVERIFY(page->acceptDrops());

    // K9 (531:3389): the facts of a tool of more inputs with a new document
    pdf::PDFKeyValueList* facts = page->getFacts();
    QVERIFY(facts);
    QStringList values;
    for (int i = 0; i < facts->getRowCount(); ++i)
    {
        values << facts->getKey(i) << facts->getValue(i);
    }
    QVERIFY(values.contains(QStringLiteral("Two or more PDFs")));
    QVERIFY(values.contains(QStringLiteral("One new document")));
    QVERIFY(values.contains(QStringLiteral("Open documents")));
    QVERIFY(values.contains(QStringLiteral("Stay unchanged")));
    QVERIFY(values.contains(QStringLiteral("Not kept (new file)")));

    // A drop on the page joins the list (K8)
    const QString imagePath = AppToolInputsHelper::writeFile(fixture.path(QStringLiteral("scan.png")), AppToolInputsHelper::createImage(QSize(30, 40), Qt::black, 300.0));
    QMimeData mimeData;
    mimeData.setUrls({ QUrl::fromLocalFile(imagePath) });
    QVERIFY(page->getInputChooser()->dropFiles(&mimeData));
    QCOMPARE(page->getInputList()->getCount(), 1);
    QVERIFY(AppToolInputsHelper::waitForLoaded(page->getInputList()));
    QCOMPARE(page->getInputList()->getInput(0)->kind, PDFAppToolInputKind::Image);

    // The list survives a rebuild of the page (a change of the language)
    QPointer<PDFAppToolInputList> list(page->getInputList());
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(page, &languageChange);
    AppDocumentFixture::settle();
    QVERIFY(list);
    QCOMPARE(page->getInputChooser()->getInputList(), list.data());
    QCOMPARE(page->getInputChooser()->getFileRows().size(), size_t(1));

    // The page deleted with a reading running
    page->getInputChooser()->addFiles({ AppDocumentSamples::large(fixture.folder(), 200) });
    delete page;
    AppDocumentFixture::settle();
    QVERIFY(!list);
}

void AppToolInputsTest::deletionWhileLoading()
{
    AppDocumentFixture fixture;
    const QString large = AppDocumentSamples::large(fixture.folder(), 500);

    // The list deleted in each state of its inputs: loading, protected, parked, ready
    for (int round = 0; round < 4; ++round)
    {
        PDFAppToolInputList* list = new PDFAppToolInputList(nullptr);
        QPointer<pdf::PDFAppToolInputLoader> loader(list->getLoader());
        const QString id = list->addFile(large).id;
        if (round >= 1)
        {
            list->addFile(AppDocumentSamples::userPassword(fixture.folder()));
        }
        if (round >= 2)
        {
            list->removeInput(id);
        }
        if (round >= 3)
        {
            QVERIFY(AppToolInputsHelper::waitForLoaded(list));
        }
        delete list;
        QVERIFY(!loader);
        AppDocumentFixture::settle();
    }

    // A loader deleted with readings running gives no late result (the link is cut)
    PDFAppToolInputLoader* loader = new PDFAppToolInputLoader(nullptr);
    QSignalSpy spy(loader, &PDFAppToolInputLoader::readingFinished);
    PDFAppToolInputLoader::Request request;
    request.id = QStringLiteral("a");
    request.filePath = large;
    loader->start(request);
    QVERIFY(loader->isRunning(QStringLiteral("a")));

    // A canceled reading gives no result either
    loader->cancel(QStringLiteral("a"));
    QVERIFY(!loader->isRunning(QStringLiteral("a")));
    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);

    request.id = QStringLiteral("b");
    loader->start(request);
    delete loader;
    QTest::qWait(500);
    AppDocumentFixture::settle();
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(AppToolInputsTest)

#include "tst_apptoolinputstest.moc"
