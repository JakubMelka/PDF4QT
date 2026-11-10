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

// The layout of an image on its page, Images to PDF, Interleave Pages and the plan of the pages
// of the core (package D of step 3a of phase 2, plan 5.7, 5.8, 8; B30). The expected values come
// from the plan (Fit of 4032 x 3024 on A4 landscape with 10 mm: the area 277 x 190 mm, the image
// 253.3 x 190 mm in the middle; 6 + 6 reversed: front 1, back 6, front 2, back 5 ...; 7 + 5: front 6
// and front 7 at the end; a crop of 120 px of 4032 x 3024 leaves 3792 x 2784) and from the geometry
// of ISO 32000-2 (8.3.3: the unit square of an image, its origin at the bottom left; 8.5.4: the
// clip re W n).

#include "mergetestpdf.h"

#include "pdfdocumentmanipulator.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentpageplan.h"
#include "pdfdocumenttool_images.h"
#include "pdfdocumenttool_merge.h"
#include "pdfexception.h"
#include "pdfimagepagefactory.h"
#include "pdfoperationcontrol.h"

#include <QBuffer>
#include <QImageWriter>
#include <QtTest>

#include <cmath>

using namespace pdf;
using mergetest::MergeTestPdf;
using mergetest::MergeTestRead;
using Layout = PDFImagePageLayout;
using Plan = PDFDocumentPagePlan;
using ImagesToPdf = PDFDocumentToolImagesToPdf;
using Interleave = PDFDocumentToolInterleave;
using Read = MergeTestRead;

/// A cancellation, which is always set
class DocumentToolImagesCanceled : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

class DocumentToolImagesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // PDFImagePageLayout
    void layoutFitLandscape();
    void layoutScalings();
    void layoutAnchors();
    void layoutOrientation();
    void layoutTurnAndClip();
    void layoutFitToImage();
    void layoutMarginsLeaveNoRoom();
    void layoutStretchedKeepsToday();
    void boundedSize();

    // The page of the core
    void pageContentFit();
    void pageContentTurned();
    void pageContentStretchedUnchanged();
    void manipulatorKeepsRatio();

    // PDFDocumentPagePlan
    void planTextRoundTrip();
    void planInterleaveReversed();
    void planInterleaveInOrder();
    void planInterleaveUneven();
    void planInterleaveLeaveOut();
    void planReverse();
    void planMergerItems();

    // Images to PDF
    void imagesAdjustments();
    void imagesClip();
    void imagesAnchors();
    void imagesLayoutParameters();
    void imagesValidation();
    void imagesGivenParameters();
    void imagesResult();
    void imagesFitToImageAndAdjustments();
    void imagesOrder();
    void imagesCanceled();

    // Interleave Pages
    void interleaveResultReversed();
    void interleaveUneven();
    void interleaveValidation();
    void interleaveBookmarks();

private:
    static constexpr double MILLIMETER = 72.0 / 25.4;

    static QSizeF imageOf(QSize pixels, double dpi) { return QSizeF(pixels.width() / dpi * 72.0, pixels.height() / dpi * 72.0); }
    static bool isNear(double value, double expected, double tolerance = 0.01) { return std::abs(value - expected) <= tolerance; }
    static QByteArray encodeImage(QSize size, const QColor& color, double dpi, const char* format = "png");
    static QVariantMap parameters(std::initializer_list<std::pair<const char*, QVariant>> values);
    static PDFDocumentToolInputImage inputImage(const QByteArray& data, const QString& fileName, const QString& inputId = QString());
    static PDFDocumentToolInputDocument inputDocument(const PDFDocumentPointer& document, const QString& fileName, const QString& inputId = QString());
    static QByteArray pageContent(const PDFDocument& document, size_t pageIndex);
    static std::vector<double> matrixOf(const QByteArray& content, const char* operatorName);
    static QStringList pageMarkers(const PDFDocument& document);
};

void DocumentToolImagesTest::initTestCase()
{
    // The texts of the numbers follow the default locale (O17): fixed for the expectations
    QLocale::setDefault(QLocale::c());
}

QByteArray DocumentToolImagesTest::encodeImage(QSize size, const QColor& color, double dpi, const char* format)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    image.setDotsPerMeterX(int(std::round(dpi / 0.0254)));
    image.setDotsPerMeterY(int(std::round(dpi / 0.0254)));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.write(image);
    return data;
}

QVariantMap DocumentToolImagesTest::parameters(std::initializer_list<std::pair<const char*, QVariant>> values)
{
    // The defaults of the definitions, then the given values (as the normalization of the tools)
    QVariantMap result;
    for (const PDFDocumentToolParameterDefinition& definition : ImagesToPdf().getParameters())
    {
        result.insert(definition.getName(), definition.getDefaultValue());
    }
    for (const auto& [name, value] : values)
    {
        result.insert(QString::fromLatin1(name), value);
    }
    return result;
}

PDFDocumentToolInputImage DocumentToolImagesTest::inputImage(const QByteArray& data, const QString& fileName, const QString& inputId)
{
    PDFDocumentToolInputImage image;
    image.data = data;
    image.fileName = fileName;
    image.inputId = inputId;
    return image;
}

PDFDocumentToolInputDocument DocumentToolImagesTest::inputDocument(const PDFDocumentPointer& document, const QString& fileName, const QString& inputId)
{
    PDFDocumentToolInputDocument input;
    input.document = document;
    input.fileName = fileName;
    input.inputId = inputId;
    return input;
}

QByteArray DocumentToolImagesTest::pageContent(const PDFDocument& document, size_t pageIndex)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFObject& contents = document.getObject(page->getContents());
    return contents.isStream() ? document.getDecodedStream(contents.getStream()) : QByteArray();
}

std::vector<double> DocumentToolImagesTest::matrixOf(const QByteArray& content, const char* operatorName)
{
    // The operands of the first operator of the name (cm: 6, re: 4)
    const QList<QByteArray> tokens = content.simplified().split(' ');
    const qsizetype index = tokens.indexOf(QByteArray(operatorName));
    const qsizetype count = QByteArray(operatorName) == "cm" ? 6 : 4;
    std::vector<double> values;
    for (qsizetype i = index - count; index >= count && i < index; ++i)
    {
        values.push_back(tokens[i].toDouble());
    }
    return values;
}

QStringList DocumentToolImagesTest::pageMarkers(const PDFDocument& document)
{
    // The comment "% <prefix>PAGE<n>" of each page of MergeTestPdf::createPages
    QStringList markers;
    for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
    {
        const QByteArray content = pageContent(document, i);
        const qsizetype start = content.indexOf("% ");
        const qsizetype end = content.indexOf('\n', start);
        markers << QString::fromLatin1(content.mid(start + 2, end - start - 2));
    }
    return markers;
}

// ------------------------------------------------------------------ the layout

void DocumentToolImagesTest::layoutFitLandscape()
{
    // Plan 5.8: Fit 4032 x 3024 on A4 with Auto and 10 mm - the page turns to landscape (388:18837),
    // the area is 277 x 190 mm, the image 253.3 x 190 mm in its middle
    Layout layout;
    layout.pageSize = QSizeF(210.0 * MILLIMETER, 297.0 * MILLIMETER);
    layout.margins = QMarginsF(10.0 * MILLIMETER, 10.0 * MILLIMETER, 10.0 * MILLIMETER, 10.0 * MILLIMETER);
    layout.scaling = Layout::Scaling::Fit;
    const QSizeF image = imageOf(QSize(4032, 3024), 300.0);

    const QSizeF page = layout.getPageSize(image);
    QVERIFY(isNear(page.width() / MILLIMETER, 297.0));
    QVERIFY(isNear(page.height() / MILLIMETER, 210.0));

    const QRectF area = layout.getArea(page);
    QVERIFY(isNear(area.width() / MILLIMETER, 277.0));
    QVERIFY(isNear(area.height() / MILLIMETER, 190.0));

    const QRectF rect = layout.getImageRect(page, image);
    QVERIFY2(isNear(rect.width() / MILLIMETER, 190.0 * 4.0 / 3.0), qPrintable(QString::number(rect.width() / MILLIMETER)));
    QVERIFY(isNear(rect.height() / MILLIMETER, 190.0));
    QVERIFY(isNear(rect.left() / MILLIMETER, 10.0 + (277.0 - 190.0 * 4.0 / 3.0) / 2.0));
    QVERIFY(isNear(rect.top() / MILLIMETER, 10.0));
}

void DocumentToolImagesTest::layoutScalings()
{
    // 356:9556: Fill covers the area (the edges cut), Stretch is the area, Original size has the
    // size by the resolution
    Layout layout;
    layout.pageSize = QSizeF(297.0 * MILLIMETER, 210.0 * MILLIMETER);
    layout.margins = QMarginsF(10.0 * MILLIMETER, 10.0 * MILLIMETER, 10.0 * MILLIMETER, 10.0 * MILLIMETER);
    const QSizeF image = imageOf(QSize(4032, 3024), 300.0);
    const QSizeF page = layout.getPageSize(image);

    layout.scaling = Layout::Scaling::Fill;
    QRectF rect = layout.getImageRect(page, image);
    QVERIFY(isNear(rect.width() / MILLIMETER, 277.0));
    QVERIFY(isNear(rect.height() / MILLIMETER, 277.0 * 3.0 / 4.0));
    QVERIFY(isNear(rect.top() / MILLIMETER, 10.0 - (277.0 * 3.0 / 4.0 - 190.0) / 2.0));

    layout.scaling = Layout::Scaling::Stretch;
    rect = layout.getImageRect(page, image);
    QCOMPARE(rect, layout.getArea(page));

    layout.scaling = Layout::Scaling::OriginalSize;
    layout.anchor = Qt::AlignTop | Qt::AlignLeft;
    rect = layout.getImageRect(page, image);
    QVERIFY(isNear(rect.width(), 4032.0 / 300.0 * 72.0));
    QVERIFY(isNear(rect.height(), 3024.0 / 300.0 * 72.0));
    QVERIFY(isNear(rect.left(), 10.0 * MILLIMETER));
    QVERIFY(isNear(rect.top(), 10.0 * MILLIMETER));
}

void DocumentToolImagesTest::layoutAnchors()
{
    // The anchor in the area (plan 5.8, 9 cells); the page coordinates from the top left corner
    Layout layout;
    layout.pageSize = QSizeF(297.0 * MILLIMETER, 210.0 * MILLIMETER);
    layout.orientation = Layout::Orientation::Landscape;
    layout.margins = QMarginsF(10.0 * MILLIMETER, 10.0 * MILLIMETER, 10.0 * MILLIMETER, 10.0 * MILLIMETER);
    layout.scaling = Layout::Scaling::Fit;
    const QSizeF image = imageOf(QSize(4032, 3024), 300.0);
    const QSizeF page = layout.getPageSize(image);
    const double imageWidth = 190.0 * 4.0 / 3.0;

    layout.anchor = Qt::AlignBottom | Qt::AlignRight;
    QRectF rect = layout.getImageRect(page, image);
    QVERIFY(isNear(rect.left() / MILLIMETER, 287.0 - imageWidth));
    QVERIFY(isNear(rect.bottom() / MILLIMETER, 200.0));

    layout.anchor = Qt::AlignTop | Qt::AlignLeft;
    rect = layout.getImageRect(page, image);
    QVERIFY(isNear(rect.left() / MILLIMETER, 10.0));

    // A missing flag is the center
    layout.anchor = Qt::AlignRight;
    rect = layout.getImageRect(page, image);
    QVERIFY(isNear(rect.right() / MILLIMETER, 287.0));
    QVERIFY(isNear(rect.top() / MILLIMETER, 10.0));

    // A portrait image on a landscape page: the vertical anchor has no room, the horizontal one has
    layout.anchor = Qt::AlignTop | Qt::AlignLeft;
    const QSizeF portraitImage = imageOf(QSize(3024, 4032), 300.0);
    rect = layout.getImageRect(page, portraitImage);
    QVERIFY(isNear(rect.left() / MILLIMETER, 10.0));
    QVERIFY(isNear(rect.height() / MILLIMETER, 190.0));
}

void DocumentToolImagesTest::layoutOrientation()
{
    const QSizeF a4(210.0 * MILLIMETER, 297.0 * MILLIMETER);
    Layout layout;
    layout.pageSize = a4;

    // Auto: the orientation of the image; a square image keeps the given size
    QVERIFY(layout.getPageSize(imageOf(QSize(400, 300), 300.0)).width() > layout.getPageSize(imageOf(QSize(400, 300), 300.0)).height());
    QCOMPARE(layout.getPageSize(imageOf(QSize(300, 400), 300.0)), a4);
    QCOMPARE(layout.getPageSize(imageOf(QSize(300, 300), 300.0)), a4);
    layout.pageSize = a4.transposed();
    QCOMPARE(layout.getPageSize(imageOf(QSize(300, 300), 300.0)), a4.transposed());

    // Portrait and Landscape whatever the image is
    layout.orientation = Layout::Orientation::Portrait;
    QCOMPARE(layout.getPageSize(imageOf(QSize(400, 300), 300.0)), a4);
    layout.orientation = Layout::Orientation::Landscape;
    layout.pageSize = a4;
    QCOMPARE(layout.getPageSize(imageOf(QSize(300, 400), 300.0)), a4.transposed());
}

void DocumentToolImagesTest::layoutTurnAndClip()
{
    // The turn of the user turns the image before the layout: Auto follows the turned image (5.8)
    const QSizeF a4(210.0 * MILLIMETER, 297.0 * MILLIMETER);
    Layout layout;
    layout.pageSize = a4;
    const QSizeF image = imageOf(QSize(4032, 3024), 300.0);
    layout.turn = PageRotation::Rotate90;
    QCOMPARE(layout.getLaidOutImageSize(image), image.transposed());
    QCOMPARE(layout.getPageSize(image), a4);
    layout.turn = PageRotation::Rotate180;
    QCOMPARE(layout.getPageSize(image), a4.transposed());

    // 336:3159: 120 px of 4032 x 3024 on every side leave 3792 x 2784 px
    layout.turn = PageRotation::None;
    layout.clip = ImagesToPdf::getClip(QMargins(120, 120, 120, 120), QSize(4032, 3024));
    const QSizeF clipped = layout.getLaidOutImageSize(image);
    QVERIFY(isNear(clipped.width(), 3792.0 / 300.0 * 72.0));
    QVERIFY(isNear(clipped.height(), 2784.0 / 300.0 * 72.0));

    // The corners of the clip are the corners of the laid out image; a turn of 90 degrees moves
    // the top left corner of the image to the top right one
    QVERIFY(isNear(layout.mapShownImagePoint(QPointF(120.0 / 4032.0, 120.0 / 3024.0)).x(), 0.0, 1e-9));
    QVERIFY(isNear(layout.mapShownImagePoint(QPointF(3912.0 / 4032.0, 2904.0 / 3024.0)).y(), 1.0, 1e-9));
    layout.clip = QRectF();
    layout.turn = PageRotation::Rotate90;
    QCOMPARE(layout.mapShownImagePoint(QPointF(0.0, 0.0)), QPointF(1.0, 0.0));
    layout.turn = PageRotation::Rotate270;
    QCOMPARE(layout.mapShownImagePoint(QPointF(0.0, 0.0)), QPointF(0.0, 1.0));

    // A clip out of the image or invalid is the whole image
    layout.clip = QRectF(2.0, 2.0, 1.0, 1.0);
    QCOMPARE(layout.getEffectiveClip(), QRectF(0.0, 0.0, 1.0, 1.0));
    layout.clip = QRectF(0.0, 0.0, std::nan(""), 1.0);
    QCOMPARE(layout.getEffectiveClip(), QRectF(0.0, 0.0, 1.0, 1.0));
}

void DocumentToolImagesTest::layoutFitToImage()
{
    // Fit to image (336:2639 "Each page is exactly as large as its image"): the image by its
    // resolution and the margins around it; Fill and Original size are placed as Fit
    Layout layout;
    layout.margins = QMarginsF(10.0, 20.0, 30.0, 40.0);
    layout.scaling = Layout::Scaling::OriginalSize;
    const QSizeF image = imageOf(QSize(600, 300), 300.0);
    const QSizeF page = layout.getPageSize(image);
    QVERIFY(isNear(page.width(), 144.0 + 40.0));
    QVERIFY(isNear(page.height(), 72.0 + 60.0));
    const QRectF rect = layout.getImageRect(page, image);
    QVERIFY(isNear(rect.left(), 10.0));
    QVERIFY(isNear(rect.top(), 20.0));
    QVERIFY(isNear(rect.width(), 144.0));
    QVERIFY(isNear(rect.height(), 72.0));
}

void DocumentToolImagesTest::layoutMarginsLeaveNoRoom()
{
    // Margins larger than the page: no area, no image, the page of the core is refused
    Layout layout;
    layout.pageSize = QSizeF(100.0, 100.0);
    layout.margins = QMarginsF(50.0, 0.0, 50.0, 0.0);
    const QSizeF image = imageOf(QSize(100, 100), 300.0);
    QVERIFY(layout.getArea(layout.getPageSize(image)).isEmpty());
    QVERIFY(layout.getImageRect(layout.getPageSize(image), image).isEmpty());

    std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::readFrame(encodeImage(QSize(100, 100), Qt::red, 300.0), QByteArray(), 0, 0.0, nullptr);
    QVERIFY(frame);
    PDFDocumentBuilder builder;
    QVERIFY_THROWS_EXCEPTION(PDFException, PDFImagePageFactory::createPage(&builder, *frame, layout));
}

void DocumentToolImagesTest::layoutStretchedKeepsToday()
{
    // Merge and PageMaster (createStretched): the clip fills the whole page of the given size,
    // a landscape image on a portrait page stays on the portrait page
    const QSizeF a4(210.0 * MILLIMETER, 297.0 * MILLIMETER);
    const Layout layout = Layout::createStretched(a4, QRectF(0.25, 0.0, 0.5, 1.0));
    const QSizeF image = imageOf(QSize(400, 300), 300.0);
    QCOMPARE(layout.getPageSize(image), a4);
    QCOMPARE(layout.getImageRect(a4, image), QRectF(QPointF(0.0, 0.0), a4));
    QCOMPARE(Layout::createStretched(a4.transposed(), QRectF()).getPageSize(imageOf(QSize(300, 400), 300.0)), a4.transposed());
}

void DocumentToolImagesTest::boundedSize()
{
    // R15 of step 2 and B30: the sides within 3 to 14 400 points, the ratio kept
    QCOMPARE(PDFImagePageFactory::getBoundedSize(QSizeF(28800.0, 100.0)), QSizeF(14400.0, 50.0));
    QCOMPARE(PDFImagePageFactory::getBoundedSize(QSizeF(1.0, 2.0)), QSizeF(3.0, 6.0));
    QCOMPARE(PDFImagePageFactory::getBoundedSize(QSizeF(595.0, 842.0)), QSizeF(595.0, 842.0));
    const QSizeF invalid = PDFImagePageFactory::getBoundedSize(QSizeF(std::nan(""), -5.0));
    QCOMPARE(invalid, QSizeF(3.0, 3.0));
}

// ------------------------------------------------------------- the page of the core

void DocumentToolImagesTest::pageContentFit()
{
    // The page of the layout: the media box of the laid out page, the clip of the area (8.5.4) and
    // the matrix of the unit square of the image (8.3.3) - [w 0 0 h x y] of the image rectangle,
    // y from the bottom of the page
    const QByteArray png = encodeImage(QSize(400, 300), Qt::blue, 300.0);
    std::vector<PDFDocumentMerger::Source> sources = { PDFDocumentMerger::Source::createImage(png, QByteArray(), 0.0, QStringLiteral("a.png")) };
    Layout layout;
    layout.pageSize = QSizeF(200.0, 300.0);
    layout.margins = QMarginsF(10.0, 20.0, 30.0, 40.0);
    layout.scaling = Layout::Scaling::Fit;
    layout.anchor = Qt::AlignTop | Qt::AlignLeft;
    layout.orientation = Layout::Orientation::Portrait;

    PDFDocumentMerger::Item item;
    item.imageLayout = layout;
    PDFDocumentMerger::Result result = PDFDocumentMerger::merge(sources, { item }, PDFDocumentMerger::Settings(), nullptr, nullptr);
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));

    const QRectF mediaBox = result.document.getCatalog()->getPage(0)->getMediaBox();
    QCOMPARE(mediaBox.size(), QSizeF(200.0, 300.0));
    QCOMPARE(result.document.getCatalog()->getPage(0)->getPageRotation(), PageRotation::None);

    // The area: x 10, width 160, from the bottom 40, height 240; the image 4:3 fits by the width
    const QByteArray content = pageContent(result.document, 0);
    const std::vector<double> clip = matrixOf(content, "re");
    QCOMPARE(clip.size(), size_t(4));
    QVERIFY(isNear(clip[0], 10.0) && isNear(clip[1], 40.0) && isNear(clip[2], 160.0) && isNear(clip[3], 240.0));
    const std::vector<double> matrix = matrixOf(content, "cm");
    QCOMPARE(matrix.size(), size_t(6));
    const std::vector<double> expected = { 160.0, 0.0, 0.0, 120.0, 10.0, 300.0 - 20.0 - 120.0 };
    for (size_t i = 0; i < expected.size(); ++i)
    {
        QVERIFY2(isNear(matrix[i], expected[i], 0.001), content.constData());
    }
}

void DocumentToolImagesTest::pageContentTurned()
{
    // A turn of 90 degrees clockwise: the bottom left corner of the image (the origin of its unit
    // square) is at the top left corner of the laid out image, its bottom edge goes down -
    // [0 -h w 0 x top]; the page itself is not turned (no /Rotate, 5.8)
    const QByteArray png = encodeImage(QSize(400, 300), Qt::blue, 300.0);
    std::vector<PDFDocumentMerger::Source> sources = { PDFDocumentMerger::Source::createImage(png, QByteArray(), 0.0, QStringLiteral("a.png")) };
    Layout layout;
    layout.pageSize = QSizeF(300.0, 400.0);
    layout.scaling = Layout::Scaling::Stretch;
    layout.turn = PageRotation::Rotate90;

    PDFDocumentMerger::Item item;
    item.imageLayout = layout;
    PDFDocumentMerger::Result result = PDFDocumentMerger::merge(sources, { item }, PDFDocumentMerger::Settings(), nullptr, nullptr);
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(result.document.getCatalog()->getPage(0)->getMediaBox().size(), QSizeF(300.0, 400.0));
    QCOMPARE(result.document.getCatalog()->getPage(0)->getPageRotation(), PageRotation::None);

    const QByteArray content = pageContent(result.document, 0);
    const std::vector<double> matrix = matrixOf(content, "cm");
    QCOMPARE(matrix.size(), size_t(6));
    const std::vector<double> expected = { 0.0, -400.0, 300.0, 0.0, 0.0, 400.0 };
    for (size_t i = 0; i < expected.size(); ++i)
    {
        QVERIFY2(isNear(matrix[i], expected[i], 0.001), content.constData());
    }
}

void DocumentToolImagesTest::pageContentStretchedUnchanged()
{
    // Merge without a layout: the clip of the image fills the page of today (the right half of a
    // 400 x 300 image on 100 x 100 points: the image twice as wide, shifted left by a page)
    const QByteArray png = encodeImage(QSize(400, 300), Qt::blue, 300.0);
    std::vector<PDFDocumentMerger::Source> sources = { PDFDocumentMerger::Source::createImage(png, QByteArray(), 0.0, QStringLiteral("a.png")) };
    PDFDocumentMerger::Item item;
    item.pageSize = QSizeF(100.0, 100.0);
    item.imageClip = QRectF(0.5, 0.0, 0.5, 1.0);
    PDFDocumentMerger::Result result = PDFDocumentMerger::merge(sources, { item }, PDFDocumentMerger::Settings(), nullptr, nullptr);
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(result.document.getCatalog()->getPage(0)->getMediaBox().size(), QSizeF(100.0, 100.0));

    const QByteArray content = pageContent(result.document, 0);
    const std::vector<double> clip = matrixOf(content, "re");
    QVERIFY(isNear(clip[0], 0.0) && isNear(clip[1], 0.0) && isNear(clip[2], 100.0) && isNear(clip[3], 100.0));
    const std::vector<double> matrix = matrixOf(content, "cm");
    const std::vector<double> expected = { 200.0, 0.0, 0.0, 100.0, -100.0, 0.0 };
    for (size_t i = 0; i < expected.size(); ++i)
    {
        QVERIFY2(isNear(matrix[i], expected[i], 0.001), content.constData());
    }

    // No size: the image by its resolution (400 x 300 px at 300 dpi = 96 x 72 points)
    item.pageSize = QSizeF();
    item.imageClip = QRectF();
    result = PDFDocumentMerger::merge(sources, { item }, PDFDocumentMerger::Settings(), nullptr, nullptr);
    QVERIFY(result.isSuccess);
    const QSizeF size = result.document.getCatalog()->getPage(0)->getMediaBox().size();
    QVERIFY(isNear(size.width(), 96.0) && isNear(size.height(), 72.0));
}

void DocumentToolImagesTest::manipulatorKeepsRatio()
{
    // B30: PageMaster gives a page 10 000 x 100 mm - the side above 14 400 points shrinks with the
    // other one (the ratio 100 : 1 kept), the page is not 14 400 x 283.46
    PDFDocumentManipulator manipulator;
    PDFDocumentManipulator::AssembledPages pages = { PDFDocumentManipulator::createBlankPage(QSizeF(10000.0, 100.0), PageRotation::None) };
    QVERIFY(manipulator.assemble(pages));
    const QSizeF size = manipulator.getAssembledDocument().getCatalog()->getPage(0)->getMediaBox().size();
    QVERIFY2(isNear(size.width(), 14400.0), qPrintable(QString::number(size.width())));
    QVERIFY2(isNear(size.height(), 144.0), qPrintable(QString::number(size.height())));
}

// ------------------------------------------------------------------- the plan

void DocumentToolImagesTest::planTextRoundTrip()
{
    // The syntax of Merge is the syntax of the plan (K44, unchanged)
    const std::optional<std::vector<Plan::Item>> items = Plan::parse(QStringLiteral("A1-3,B4@90,A2+,B3-1"), { 3, 4 }, nullptr);
    QVERIFY(items);
    QCOMPARE(items->size(), size_t(8));
    QCOMPARE((*items)[3], (Plan::Item{ 1, 3, PageRotation::Rotate90, false }));
    QCOMPARE((*items)[4], (Plan::Item{ 0, 1, PageRotation::None, true }));
    QCOMPARE((*items)[7], (Plan::Item{ 1, 0, PageRotation::None, false }));
    QCOMPARE(Plan::toText(*items), QStringLiteral("A1-3,B4@90,A2+,B3-1"));
    QVERIFY(!Plan::parse(QStringLiteral("A2,A2"), { 3 }, nullptr));
    QVERIFY(!Plan::parse(QStringLiteral("C1"), { 3, 4 }, nullptr));
    QCOMPARE(PDFDocumentToolMerge::createPlan(*items), Plan::toText(*items));
}

void DocumentToolImagesTest::planInterleaveReversed()
{
    // 385:13263: 6 + 6, back sides reversed - front 1, back 6, front 2, back 5, ..., front 6, back 1
    const std::vector<Plan::Item> items = Plan::interleave(6, 6, Plan::InterleaveSettings());
    QCOMPARE(Plan::toText(items), QStringLiteral("A1,B6,A2,B5,A3,B4,A4,B3,A5,B2,A6,B1"));
}

void DocumentToolImagesTest::planInterleaveInOrder()
{
    Plan::InterleaveSettings settings;
    settings.isBackReversed = false;
    QCOMPARE(Plan::toText(Plan::interleave(3, 3, settings)), QStringLiteral("A1,B1,A2,B2,A3,B3"));
}

void DocumentToolImagesTest::planInterleaveUneven()
{
    // 385:14487: 7 + 5 - the front sides 6 and 7 at the end; the back sides reversed pair with
    // the fronts 1 to 5 (back 5 ... back 1)
    QCOMPARE(Plan::toText(Plan::interleave(7, 5, Plan::InterleaveSettings())), QStringLiteral("A1,B5,A2,B4,A3,B3,A4,B2,A5,B1,A6-7"));

    // More back sides: the stack turned over is read from its last page, the pages left without a
    // front are the first pages of the scan (back 2, back 1)
    // (B3, B2, B1 is one descending run of the text: "B3-1")
    QCOMPARE(Plan::toText(Plan::interleave(5, 7, Plan::InterleaveSettings())), QStringLiteral("A1,B7,A2,B6,A3,B5,A4,B4,A5,B3-1"));
}

void DocumentToolImagesTest::planInterleaveLeaveOut()
{
    Plan::InterleaveSettings settings;
    settings.areExtrasKept = false;
    QCOMPARE(Plan::toText(Plan::interleave(7, 5, settings)), QStringLiteral("A1,B5,A2,B4,A3,B3,A4,B2,A5,B1"));
    QVERIFY(Plan::interleave(0, 5, settings).empty());
    QCOMPARE(Plan::interleave(0, 2, Plan::InterleaveSettings()).size(), size_t(2));
    QVERIFY(Plan::interleave(-3, -1, Plan::InterleaveSettings()).empty());
}

void DocumentToolImagesTest::planReverse()
{
    // The order turned around; the first occurrence of a page is the page, the later ones clones
    const std::optional<std::vector<Plan::Item>> items = Plan::parse(QStringLiteral("A1,A2,A1+"), { 2 }, nullptr);
    QVERIFY(items);
    // A1+, A2, A1 turned around: the first A1 is the page, the last one its clone ("A1-2,A1+")
    QCOMPARE(Plan::toText(Plan::reverse(*items)), QStringLiteral("A1-2,A1+"));
    QCOMPARE(Plan::toText(Plan::reverse(Plan::createDefault({ 3 }))), QStringLiteral("A3-1"));
}

void DocumentToolImagesTest::planMergerItems()
{
    Plan::Item item{ 1, 4, PageRotation::Rotate180, false };
    item.pageSize = QSizeF(100.0, 200.0);
    item.imageLayout = Layout::createStretched(QSizeF(10.0, 20.0), QRectF());
    const std::vector<PDFDocumentMerger::Item> mergerItems = Plan::createMergerItems({ item });
    QCOMPARE(mergerItems.size(), size_t(1));
    QCOMPARE(mergerItems[0].source, size_t(1));
    QCOMPARE(mergerItems[0].pageIndex, PDFInteger(4));
    QCOMPARE(mergerItems[0].turn, PageRotation::Rotate180);
    QCOMPARE(mergerItems[0].pageSize, QSizeF(100.0, 200.0));
    QVERIFY(mergerItems[0].imageLayout == item.imageLayout);
}

// --------------------------------------------------------------- Images to PDF

void DocumentToolImagesTest::imagesAdjustments()
{
    QString error;
    const std::optional<std::map<int, ImagesToPdf::Adjustment>> adjustments = ImagesToPdf::parseAdjustments(QStringLiteral("A@90; C#1,2,3,4 ;B@270#0,10,0,10"), 3, &error);
    QVERIFY2(adjustments, qPrintable(error));
    QCOMPARE(adjustments->size(), size_t(3));
    QCOMPARE(adjustments->at(0).turn, PageRotation::Rotate90);
    QCOMPARE(adjustments->at(2).crop, QMargins(1, 2, 3, 4));
    QCOMPARE(adjustments->at(1).turn, PageRotation::Rotate270);
    QCOMPARE(adjustments->at(1).crop, QMargins(0, 10, 0, 10));
    QCOMPARE(ImagesToPdf::createAdjustments(*adjustments), QStringLiteral("A@90;B@270#0,10,0,10;C#1,2,3,4"));

    QVERIFY(!ImagesToPdf::parseAdjustments(QStringLiteral("A@45"), 1, &error));
    QVERIFY(!ImagesToPdf::parseAdjustments(QStringLiteral("B@90"), 1, &error));
    QVERIFY(!ImagesToPdf::parseAdjustments(QStringLiteral("A@90;A@180"), 1, &error));
    QVERIFY(!ImagesToPdf::parseAdjustments(QStringLiteral("A#1,2,3"), 1, &error));
    QVERIFY(!ImagesToPdf::parseAdjustments(QStringLiteral("a@90"), 1, &error));
    QVERIFY(ImagesToPdf::parseAdjustments(QString(), 0, &error));
}

void DocumentToolImagesTest::imagesClip()
{
    // 336:3159: 120 px of every side of 4032 x 3024 = 3792 x 2784 px
    const QRectF clip = ImagesToPdf::getClip(QMargins(120, 120, 120, 120), QSize(4032, 3024));
    QVERIFY(isNear(clip.width() * 4032.0, 3792.0, 1e-6));
    QVERIFY(isNear(clip.height() * 3024.0, 2784.0, 1e-6));
    QVERIFY(isNear(clip.left() * 4032.0, 120.0, 1e-6));

    // Apply to: All images on a smaller image - at least one pixel stays
    const QRectF small = ImagesToPdf::getClip(QMargins(120, 120, 120, 120), QSize(100, 50));
    QVERIFY(isNear(small.width() * 100.0, 1.0, 1e-6));
    QVERIFY(isNear(small.height() * 50.0, 1.0, 1e-6));
    QVERIFY(small.right() <= 1.0 + 1e-9 && small.bottom() <= 1.0 + 1e-9);
    QVERIFY(ImagesToPdf::getClip(QMargins(), QSize(100, 50)).isEmpty());
}

void DocumentToolImagesTest::imagesAnchors()
{
    QCOMPARE(ImagesToPdf::getAnchor(QStringLiteral("bottom-right")), Qt::AlignBottom | Qt::AlignRight);
    QCOMPARE(ImagesToPdf::getAnchor(QStringLiteral("top-center")), Qt::AlignTop | Qt::AlignHCenter);
    QCOMPARE(ImagesToPdf::getAnchor(QStringLiteral("nonsense")), Qt::Alignment(Qt::AlignCenter));
    for (const QString& id : { QStringLiteral("top-left"), QStringLiteral("middle-center"), QStringLiteral("bottom-left"), QStringLiteral("middle-right") })
    {
        QCOMPARE(ImagesToPdf::getAnchorId(ImagesToPdf::getAnchor(id)), id);
    }
    QCOMPARE(ImagesToPdf::getAnchorId(Qt::AlignLeft), QStringLiteral("middle-left"));
}

void DocumentToolImagesTest::imagesLayoutParameters()
{
    QString error;
    QString parameter;
    std::optional<Layout> layout = ImagesToPdf::createLayout(parameters({ { "pageSize", QStringLiteral("letter") }, { "orientation", QStringLiteral("landscape") }, { "margins", 15.0 },
                                                                           { "imageScaling", QStringLiteral("fill") }, { "anchor", QStringLiteral("top-right") } }), &error, &parameter);
    QVERIFY2(layout, qPrintable(error));
    QCOMPARE(layout->pageSize, QSizeF(612.0, 792.0));
    QCOMPARE(layout->orientation, Layout::Orientation::Landscape);
    QVERIFY(isNear(layout->margins.left(), 15.0 * MILLIMETER));
    QCOMPARE(layout->scaling, Layout::Scaling::Fill);
    QCOMPARE(layout->anchor, Qt::AlignTop | Qt::AlignRight);

    layout = ImagesToPdf::createLayout(parameters({ { "pageSize", QStringLiteral("fit-to-image") } }), &error, &parameter);
    QVERIFY(layout && layout->pageSize.isEmpty());

    layout = ImagesToPdf::createLayout(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 200.0 }, { "height", 280.0 } }), &error, &parameter);
    QVERIFY(layout);
    QVERIFY(isNear(layout->pageSize.width() / MILLIMETER, 200.0) && isNear(layout->pageSize.height() / MILLIMETER, 280.0));

    // 5.8: margins up to less than half of the shorter side (A4: 105 mm)
    QVERIFY(!ImagesToPdf::createLayout(parameters({ { "margins", 105.0 } }), &error, &parameter));
    QCOMPARE(parameter, QStringLiteral("margins"));
    QVERIFY(ImagesToPdf::createLayout(parameters({ { "margins", 104.9 } }), &error, &parameter));
    QVERIFY(ImagesToPdf::createLayout(parameters({ { "pageSize", QStringLiteral("fit-to-image") }, { "margins", 500.0 } }), &error, &parameter));
    QVERIFY(!ImagesToPdf::createLayout(parameters({ { "pageSize", QStringLiteral("custom") }, { "width", 1.0 }, { "height", 280.0 } }), &error, &parameter));
    QCOMPARE(parameter, QStringLiteral("width"));
    QVERIFY(!ImagesToPdf::createLayout(parameters({ { "pageSize", QStringLiteral("16x9") } }), &error, &parameter));
    QCOMPARE(parameter, QStringLiteral("pageSize"));
}

void DocumentToolImagesTest::imagesValidation()
{
    ImagesToPdf tool;
    PDFDocumentToolContext context;
    QVERIFY(!tool.validate(context, parameters({})).empty());

    context.images.push_back(inputImage(encodeImage(QSize(40, 30), Qt::red, 300.0), QStringLiteral("a.png"), QStringLiteral("1")));
    QVERIFY(tool.validate(context, parameters({})).empty());

    // An image, which cannot be read, is named
    context.images.push_back(inputImage(QByteArray("not an image"), QStringLiteral("broken.png"), QStringLiteral("2")));
    const std::vector<PDFDocumentToolParameterError> errors = tool.validate(context, parameters({}));
    QCOMPARE(errors.size(), size_t(1));
    QVERIFY(errors.front().message.contains(QStringLiteral("broken.png")));
    context.images.pop_back();

    QCOMPARE(tool.validate(context, parameters({ { "margins", 200.0 } })).front().parameter, QStringLiteral("margins"));
    QCOMPARE(tool.validate(context, parameters({ { "adjustments", QStringLiteral("B@90") } })).front().parameter, QStringLiteral("adjustments"));
    QCOMPARE(tool.validate(context, parameters({ { "inputs", QStringLiteral("9") } })).front().parameter, QStringLiteral("inputs"));

    // A document is not an input of Images to PDF
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("D", 1).readPointer(), QStringLiteral("d.pdf")));
    QVERIFY(!tool.validate(context, parameters({})).empty());
}

void DocumentToolImagesTest::imagesGivenParameters()
{
    // An assistant gives imagePaths in the order of the pages; the order and the adjustments
    // belong to the panel, width and height to the custom size
    ImagesToPdf tool;
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("inputs"), QStringLiteral("1") } }).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("adjustments"), QStringLiteral("A@90") } }).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("width"), 100.0 } }).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("pageSize"), QStringLiteral("custom") } }).size(), size_t(1));
    QVERIFY(tool.checkGivenParameters({ { QStringLiteral("pageSize"), QStringLiteral("custom") }, { QStringLiteral("width"), 100.0 }, { QStringLiteral("height"), 100.0 } }).empty());
    QVERIFY(tool.checkGivenParameters({ { QStringLiteral("margins"), 5.0 } }).empty());

    // The parameters of the panel are never stored and are not in MCP
    for (const PDFDocumentToolParameterDefinition& definition : tool.getParameters())
    {
        if (definition.getName() == QLatin1String("inputs") || definition.getName() == QLatin1String("adjustments"))
        {
            QVERIFY(!definition.isStored());
            QVERIFY(definition.isPanelOnly());
        }
    }
}

void DocumentToolImagesTest::imagesResult()
{
    // Two images on A4 with Auto: the landscape image gets a landscape page, the portrait image a
    // portrait one (336:2639 "Page 1 · A4 landscape", "Page 3 · A4 portrait")
    ImagesToPdf tool;
    PDFDocumentToolContext context;
    context.images.push_back(inputImage(encodeImage(QSize(400, 300), Qt::red, 300.0), QStringLiteral("IMG_2041.png"), QStringLiteral("1")));
    context.images.push_back(inputImage(encodeImage(QSize(300, 400), Qt::green, 300.0), QStringLiteral("scan_002.png"), QStringLiteral("2")));
    const PDFDocumentToolResult result = tool.execute(context, parameters({}), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(result.documents.front().kind, PDFDocumentToolOutputDocument::Kind::New);
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("IMG_2041.pdf"));
    QVERIFY(!result.documents.front().writtenData.isEmpty());

    const PDFDocument& document = *result.documents.front().document;
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(isNear(document.getCatalog()->getPage(0)->getMediaBox().width() / MILLIMETER, 297.0));
    QVERIFY(isNear(document.getCatalog()->getPage(1)->getMediaBox().width() / MILLIMETER, 210.0));
    QCOMPARE(result.data.value(QStringLiteral("pageCount")).toInteger(), 2);
    QCOMPARE(result.data.value(QStringLiteral("imageCount")).toInteger(), 2);
    QCOMPARE(result.data.value(QStringLiteral("anchor")).toString(), QStringLiteral("middle-center"));

    // The document written by the tool is the document of the result (read back: two pages)
    const PDFDocument written = MergeTestPdf::readBytes(result.documents.front().writtenData);
    QCOMPARE(written.getCatalog()->getPageCount(), size_t(2));

    QStringList factIds;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        factIds << fact.id;
    }
    QCOMPARE(factIds, (QStringList{ QStringLiteral("pages"), QStringLiteral("size") }));
}

void DocumentToolImagesTest::imagesFitToImageAndAdjustments()
{
    // Fit to image without margins: the page of each image is the image by its resolution, after
    // its turn and crop (400 x 300 px at 300 dpi = 96 x 72 points; turned = 72 x 96; cropped by
    // 100 px left and right = 48 x 72)
    ImagesToPdf tool;
    PDFDocumentToolContext context;
    context.images.push_back(inputImage(encodeImage(QSize(400, 300), Qt::red, 300.0), QStringLiteral("a.png"), QStringLiteral("1")));
    context.images.push_back(inputImage(encodeImage(QSize(400, 300), Qt::red, 300.0), QStringLiteral("b.png"), QStringLiteral("2")));
    context.images.push_back(inputImage(encodeImage(QSize(400, 300), Qt::red, 300.0), QStringLiteral("c.png"), QStringLiteral("3")));
    const PDFDocumentToolResult result = tool.execute(context, parameters({ { "pageSize", QStringLiteral("fit-to-image") }, { "margins", 0.0 },
                                                                            { "adjustments", QStringLiteral("B@90;C#100,0,100,0") } }), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument& document = *result.documents.front().document;
    const QSizeF first = document.getCatalog()->getPage(0)->getMediaBox().size();
    const QSizeF second = document.getCatalog()->getPage(1)->getMediaBox().size();
    const QSizeF third = document.getCatalog()->getPage(2)->getMediaBox().size();
    QVERIFY(isNear(first.width(), 96.0) && isNear(first.height(), 72.0));
    QVERIFY(isNear(second.width(), 72.0) && isNear(second.height(), 96.0));
    QVERIFY(isNear(third.width(), 48.0) && isNear(third.height(), 72.0));
    for (size_t i = 0; i < 3; ++i)
    {
        QCOMPARE(document.getCatalog()->getPage(i)->getPageRotation(), PageRotation::None);
    }
}

void DocumentToolImagesTest::imagesOrder()
{
    // "inputs" orders the pages: the third image first
    ImagesToPdf tool;
    PDFDocumentToolContext context;
    context.images.push_back(inputImage(encodeImage(QSize(100, 100), Qt::red, 300.0), QStringLiteral("a.png"), QStringLiteral("1")));
    context.images.push_back(inputImage(encodeImage(QSize(200, 100), Qt::red, 300.0), QStringLiteral("b.png"), QStringLiteral("2")));
    const PDFDocumentToolResult result = tool.execute(context, parameters({ { "pageSize", QStringLiteral("fit-to-image") }, { "margins", 0.0 }, { "inputs", QStringLiteral("2,1") } }), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument& document = *result.documents.front().document;
    QVERIFY(isNear(document.getCatalog()->getPage(0)->getMediaBox().width(), 48.0));
    QVERIFY(isNear(document.getCatalog()->getPage(1)->getMediaBox().width(), 24.0));
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("b.pdf"));

    QString error;
    QVERIFY(!ImagesToPdf::resolveInputs(context, parameters({ { "inputs", QStringLiteral("2") } }), &error));
    QVERIFY(!ImagesToPdf::resolveInputs(context, parameters({ { "inputs", QStringLiteral("2,2,1") } }), &error));
}

void DocumentToolImagesTest::imagesCanceled()
{
    // Stop ends the run, nothing is kept (356:9165)
    ImagesToPdf tool;
    PDFDocumentToolContext context;
    context.images.push_back(inputImage(encodeImage(QSize(100, 100), Qt::red, 300.0), QStringLiteral("a.png")));
    DocumentToolImagesCanceled canceled;
    const PDFDocumentToolResult result = tool.execute(context, parameters({}), &canceled, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.documents.empty());
}

// ------------------------------------------------------------------ Interleave

void DocumentToolImagesTest::interleaveResultReversed()
{
    // 385:13263 and 385:14973: 6 + 6, back sides reversed - the pages of the result by their
    // markers; no outline, no document parts (Q8)
    PDFDocumentToolContext context;
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("F", 6).readPointer(), QStringLiteral("front.pdf"), QStringLiteral("f")));
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("B", 6).readPointer(), QStringLiteral("back.pdf"), QStringLiteral("b")));
    Interleave tool;
    QVERIFY(tool.validate(context, QVariantMap()).empty());
    const PDFDocumentToolResult result = tool.execute(context, { { QStringLiteral("backSides"), QStringLiteral("reversed") } }, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument& document = *result.documents.front().document;
    QCOMPARE(pageMarkers(document), (QStringList{ "FPAGE1", "BPAGE6", "FPAGE2", "BPAGE5", "FPAGE3", "BPAGE4", "FPAGE4", "BPAGE3", "FPAGE5", "BPAGE2", "FPAGE6", "BPAGE1" }));
    QVERIFY(!Read::catalog(document)->hasKey("Outlines"));
    QVERIFY(!Read::catalog(document)->hasKey("DPartRoot"));
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("front_interleaved.pdf"));
    QCOMPARE(result.facts.front().value, QStringLiteral("12 · 6 front and 6 back sides"));
    QCOMPARE(result.facts.at(1).value, QStringLiteral("Reversed order"));

    // The order of the inputs: the back sides given first are the front sides of the result
    const PDFDocumentToolResult swapped = tool.execute(context, { { QStringLiteral("inputs"), QStringLiteral("b,f") }, { QStringLiteral("backSides"), QStringLiteral("in-order") } }, nullptr, nullptr);
    QVERIFY(swapped.isSuccess());
    QCOMPARE(pageMarkers(*swapped.documents.front().document).mid(0, 4), (QStringList{ "BPAGE1", "FPAGE1", "BPAGE2", "FPAGE2" }));
}

void DocumentToolImagesTest::interleaveUneven()
{
    // 385:14487: 7 + 5 - front 6 and front 7 at the end, "2 front sides have no back side"
    PDFDocumentToolContext context;
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("F", 7).readPointer(), QStringLiteral("front.pdf")));
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("B", 5).readPointer(), QStringLiteral("back.pdf")));
    Interleave tool;
    PDFDocumentToolResult result = tool.execute(context, QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QStringList markers = pageMarkers(*result.documents.front().document);
    QCOMPARE(markers.size(), 12);
    QCOMPARE(markers.mid(10), (QStringList{ "FPAGE6", "FPAGE7" }));
    QCOMPARE(result.data.value(QStringLiteral("unpairedCount")).toInteger(), 2);

    result = tool.execute(context, { { QStringLiteral("extraPages"), QStringLiteral("leave-out") } }, nullptr, nullptr);
    QVERIFY(result.isSuccess());
    markers = pageMarkers(*result.documents.front().document);
    QCOMPARE(markers.size(), 10);
    QCOMPARE(markers.last(), QStringLiteral("BPAGE1"));
    QCOMPARE(result.facts.front().value, QStringLiteral("10 · 5 front and 5 back sides"));
}

void DocumentToolImagesTest::interleaveValidation()
{
    // Exactly two PDF files (K29: images are not scans of Interleave)
    Interleave tool;
    PDFDocumentToolContext context;
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("F", 2).readPointer(), QStringLiteral("front.pdf")));
    QVERIFY(!tool.validate(context, QVariantMap()).empty());
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("B", 2).readPointer(), QStringLiteral("back.pdf")));
    QVERIFY(tool.validate(context, QVariantMap()).empty());
    context.images.push_back(inputImage(encodeImage(QSize(10, 10), Qt::red, 300.0), QStringLiteral("a.png")));
    QVERIFY(!tool.validate(context, QVariantMap()).empty());
    context.images.clear();
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("C", 2).readPointer(), QStringLiteral("third.pdf")));
    QVERIFY(!tool.validate(context, QVariantMap()).empty());

    // A file without the permission of the merge (Q4: Assemble) is refused as by Merge
    context.documents.pop_back();
    const PDFDocument plain = MergeTestPdf::createPages("E", 2).read();
    const QByteArray encrypted = MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), 0);
    context.documents[1] = inputDocument(PDFDocumentPointer(new PDFDocument(MergeTestPdf::readBytes(encrypted))), QStringLiteral("locked.pdf"));
    QVERIFY(!tool.validate(context, QVariantMap()).empty());

    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("inputs"), QStringLiteral("a,b") } }).size(), size_t(1));
    QVERIFY(tool.checkGivenParameters({ { QStringLiteral("backSides"), QStringLiteral("in-order") } }).empty());
}

void DocumentToolImagesTest::interleaveBookmarks()
{
    // Q8: a scan with bookmarks - the result has none and says it
    MergeTestPdf front = MergeTestPdf::createPages("F", 2, "/Outlines 50 0 R");
    front.set(50, "<< /Type /Outlines /First 51 0 R /Last 51 0 R /Count 1 >>");
    front.set(51, "<< /Title (Scan) /Parent 50 0 R /Dest [3 0 R /Fit] >>");
    PDFDocumentToolContext context;
    context.documents.push_back(inputDocument(front.readPointer(), QStringLiteral("front.pdf")));
    context.documents.push_back(inputDocument(MergeTestPdf::createPages("B", 2).readPointer(), QStringLiteral("back.pdf")));
    const PDFDocumentToolResult result = Interleave().execute(context, QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(!Read::catalog(*result.documents.front().document)->hasKey("Outlines"));
    bool hasFact = false;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        hasFact = hasFact || (fact.id == QLatin1String("bookmarks") && fact.value == QLatin1String("Bookmarks of the scans are not kept"));
    }
    QVERIFY(hasFact);
}

QTEST_GUILESS_MAIN(DocumentToolImagesTest)

#include "tst_documenttoolimagestest.moc"
