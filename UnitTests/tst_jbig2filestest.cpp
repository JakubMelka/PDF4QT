// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfexception.h"
#include "pdfjbig2decoder.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QtTest>

/// Decodes every JBIG2 file of a directory given from the outside. The directory is not
/// a part of the repository, because the test files are covered by a copyright of their
/// authors - the test is therefore skipped, when the directory is not found, and it does
/// not enumerate the expected files anywhere. Its content is read at the run time, so
/// files can be added to it and removed from it without touching this test.
///
/// The directory can be selected by the environment variable PDF4QT_JBIG2_TEST_DIRECTORY,
/// otherwise the directory configured by the build system is used.
///
/// A file named \p xxx.bmp is used as the reference image of every file named \p xxx_N.jb2,
/// so a bitmap decoded by a different decoder can be dropped into the directory next to the
/// tested files to check, that the image is not only decoded, but also decoded correctly.
class JBIG2FilesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void test_file_is_decoded_data();
    void test_file_is_decoded();

    void test_file_matches_reference_data();
    void test_file_matches_reference();

private:
    /// Returns the directory with the test files
    static QDir getTestDirectory();

    /// Fills the test data by a single row for each JBIG2 file of the test directory. The
    /// name of the row is the name of the file, so a failed file is directly identified by
    /// the output of the test.
    static void fillFileNameTestData();

    /// Decodes a JBIG2 file and returns it as a monochrome image. Throws \p pdf::PDFException,
    /// when the file cannot be decoded.
    /// \param fileName Name of the decoded file
    static QImage decodeFile(const QString& fileName);

    /// Returns the image converted to the 8 bit grayscale, so images of two different sources
    /// can be compared pixel by pixel without a dependency on the order of their color tables.
    /// \param image Converted image
    static QImage toGrayscale(const QImage& image);

    /// Returns the name of the reference image of a JBIG2 file, or an empty string, when the
    /// directory contains no reference image of the file. The test files of a single image
    /// are named by the image and by an index separated from it by an underscore.
    /// \param fileName Name of the JBIG2 file
    static QString getReferenceFileName(const QString& fileName);
};

QDir JBIG2FilesTest::getTestDirectory()
{
    QString directory = qEnvironmentVariable("PDF4QT_JBIG2_TEST_DIRECTORY");

    if (directory.isEmpty())
    {
        directory = QString(PDF4QT_JBIG2_TEST_DIRECTORY);
    }

    return QDir(directory);
}

void JBIG2FilesTest::fillFileNameTestData()
{
    QTest::addColumn<QString>("fileName");

    const QDir directory = getTestDirectory();
    const QStringList fileNames = directory.entryList({ "*.jb2", "*.jbig2" }, QDir::Files, QDir::Name);

    for (const QString& fileName : fileNames)
    {
        QTest::newRow(fileName.toLatin1().constData()) << directory.absoluteFilePath(fileName);
    }
}

QImage JBIG2FilesTest::decodeFile(const QString& fileName)
{
    QFile file(fileName);

    if (!file.open(QFile::ReadOnly))
    {
        return QImage();
    }

    const QByteArray fileData = file.readAll();
    file.close();

    pdf::PDFRenderErrorReporterDummy errorReporter;
    pdf::PDFJBIG2Decoder decoder(fileData, QByteArray(), &errorReporter);
    const pdf::PDFImageData imageData = decoder.decodeFileStream();

    if (!imageData.isValid())
    {
        return QImage();
    }

    QImage image(int(imageData.getWidth()), int(imageData.getHeight()), QImage::Format_Mono);

    // The decoder writes a set bit for a white pixel, because the value of the image data
    // is the value of the color, and not the value of the ink
    image.setColorTable({ qRgb(0, 0, 0), qRgb(255, 255, 255) });

    const int stride = int(imageData.getStride());

    // The scan lines of the image are aligned to four bytes, so the last bytes of a line
    // are not written by the decoded data and would stay uninitialized
    image.fill(0);

    for (int row = 0; row < image.height(); ++row)
    {
        std::memcpy(image.scanLine(row), imageData.getData().constData() + row * stride, size_t(stride));
    }

    return image;
}

QImage JBIG2FilesTest::toGrayscale(const QImage& image)
{
    return image.convertToFormat(QImage::Format_Grayscale8);
}

QString JBIG2FilesTest::getReferenceFileName(const QString& fileName)
{
    const QFileInfo fileInfo(fileName);
    const QString baseName = fileInfo.completeBaseName();
    const int separatorPosition = baseName.lastIndexOf('_');

    if (separatorPosition <= 0)
    {
        return QString();
    }

    const QString referenceFileName = fileInfo.dir().absoluteFilePath(baseName.left(separatorPosition) + ".bmp");
    return QFile::exists(referenceFileName) ? referenceFileName : QString();
}

void JBIG2FilesTest::initTestCase()
{
    const QDir directory = getTestDirectory();

    if (!directory.exists())
    {
        QSKIP(qPrintable(QString("Directory '%1' with the JBIG2 test files does not exist.").arg(directory.absolutePath())));
    }

    if (directory.entryList({ "*.jb2", "*.jbig2" }, QDir::Files).isEmpty())
    {
        QSKIP(qPrintable(QString("Directory '%1' contains no JBIG2 test file.").arg(directory.absolutePath())));
    }
}

void JBIG2FilesTest::test_file_is_decoded_data()
{
    fillFileNameTestData();
}

void JBIG2FilesTest::test_file_is_decoded()
{
    QFETCH(QString, fileName);

    QImage image;

    try
    {
        image = decodeFile(fileName);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);
}

void JBIG2FilesTest::test_file_matches_reference_data()
{
    fillFileNameTestData();
}

void JBIG2FilesTest::test_file_matches_reference()
{
    QFETCH(QString, fileName);

    const QString referenceFileName = getReferenceFileName(fileName);

    if (referenceFileName.isEmpty())
    {
        QSKIP("No reference image of the file.");
    }

    const QImage reference = toGrayscale(QImage(referenceFileName));
    QVERIFY2(!reference.isNull(), qPrintable(QString("Reference image '%1' cannot be read.").arg(referenceFileName)));

    QImage image;

    try
    {
        image = decodeFile(fileName);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QVERIFY(!image.isNull());
    image = toGrayscale(image);

    QCOMPARE(image.size(), reference.size());

    // The decoding of a JBIG2 image is deterministic, so the decoded image is required to
    // be identical to the reference one. A file encoded by a lossy mode is lossy on the
    // side of the encoder - it stores a simplified image, which is then decoded exactly.
    qint64 differentPixelCount = 0;

    for (int row = 0; row < image.height(); ++row)
    {
        const uchar* imageLine = image.constScanLine(row);
        const uchar* referenceLine = reference.constScanLine(row);

        for (int column = 0; column < image.width(); ++column)
        {
            if (imageLine[column] != referenceLine[column])
            {
                ++differentPixelCount;
            }
        }
    }

    const qint64 pixelCount = qint64(image.width()) * qint64(image.height());
    const qreal differentPixelRatio = qreal(differentPixelCount) / qreal(pixelCount);

    QVERIFY2(differentPixelCount == 0,
             qPrintable(QString("%1 of %2 pixels (%3 %) differ from the reference image.").arg(differentPixelCount).arg(pixelCount).arg(differentPixelRatio * 100.0, 0, 'f', 4)));
}

QTEST_APPLESS_MAIN(JBIG2FilesTest)

#include "tst_jbig2filestest.moc"
