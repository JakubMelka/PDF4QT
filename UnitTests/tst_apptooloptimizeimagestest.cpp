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

// The GUI of Optimize Images, Grayscale and Extract Images with the list of the images, the
// comparison and the profile editor (step 4 of phase 2, package P2; plan 8 row GUI), and B64 of
// the snapper. The expected values are the numbers of the frames (297:3135, 301:3594, 308:6911,
// 341:7516) and of ISO 32000-2 (the resolution of an image drawn by its matrix, 8.9.4).

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfapppassworddialog.h"
#include "pdfappsettingcontrol.h"
#include "pdfapptoolgui_extractimages.h"
#include "pdfapptoolgui_optimizeimages.h"
#include "pdfapptoolimagecomparison.h"
#include "pdfapptoolimagelist.h"
#include "pdfapptoolimageprofileeditor.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentimagelist.h"
#include "pdfdocumenttool_extractimages.h"
#include "pdfdocumenttool_grayscale.h"
#include "pdfdocumenttool_optimizeimages.h"
#include "pdfdocumentwriter.h"
#include "pdffluentwidgets.h"
#include "pdfpainter.h"
#include "pdfparser.h"
#include "pdfsecurityhandler.h"
#include "pdfsnapper.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetsnapshot.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QClipboard>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <vector>

using pdf::PDFAppToolGUIExtractImages;
using pdf::PDFAppToolGUIOptimizeImages;
using pdf::PDFAppToolImageComparison;
using pdf::PDFAppToolImageList;
using pdf::PDFAppToolImageListModel;
using pdf::PDFDocumentImageList;
using pdf::PDFDocumentToolShell;
using pdftest::AppToolShellFixture;

/// A document of images built from objects written as text: a color photo-like image, a gray and
/// a bitonal one, drawn by known matrices
class OptimizeImagesGuiDocument
{
public:
    static pdf::PDFObject parse(const QByteArray& text)
    {
        pdf::PDFParser parser(text, nullptr, pdf::PDFParser::None);
        return parser.getObject();
    }

    static QByteArray ref(pdf::PDFObjectReference reference)
    {
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R";
    }

    pdf::PDFObjectReference addStream(const QByteArray& dictionaryText, QByteArray data)
    {
        const pdf::PDFObject dictionaryObject = parse(dictionaryText);
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data))));
    }

    /// An uncompressed RGB image of width x height pixels with a smooth gradient (it compresses well)
    pdf::PDFObjectReference addColorImage(int width, int height)
    {
        QByteArray data;
        data.reserve(width * height * 3);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                data += char(uchar(x * 255 / qMax(1, width - 1)));
                data += char(uchar(y * 255 / qMax(1, height - 1)));
                data += char(uchar(128));
            }
        }
        return addStream("<< /Type /XObject /Subtype /Image /Width " + QByteArray::number(width) + " /Height " + QByteArray::number(height) +
                         " /ColorSpace /DeviceRGB /BitsPerComponent 8 >>", data);
    }

    /// An uncompressed gray image of 64 x 64 pixels
    pdf::PDFObjectReference addGrayImage()
    {
        return addStream("<< /Type /XObject /Subtype /Image /Width 64 /Height 64 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(64 * 64, '\x80'));
    }

    /// A bitonal image of 16 x 16 pixels
    pdf::PDFObjectReference addBitonalImage()
    {
        return addStream("<< /Type /XObject /Subtype /Image /Width 16 /Height 16 /ColorSpace /DeviceGray /BitsPerComponent 1 >>", QByteArray(32, '\x0F'));
    }

    /// A page of 400 x 400 pt drawing the images by their matrices ("a 0 0 d e f")
    pdf::PDFObjectReference addPage(const std::vector<std::pair<pdf::PDFObjectReference, QByteArray>>& images)
    {
        QByteArray xobjects;
        QByteArray content;
        for (size_t i = 0; i < images.size(); ++i)
        {
            xobjects += "/Im" + QByteArray::number(qint64(i)) + " " + ref(images[i].first) + " ";
            content += "q " + images[i].second + " cm /Im" + QByteArray::number(qint64(i)) + " Do Q\n";
        }
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
        const pdf::PDFObjectReference contents = addStream("<< >>", content);
        builder.mergeTo(page, parse("<< /Resources << /XObject << " + xobjects + " >> >> /Contents " + ref(contents) + " >>"));
        return page;
    }

    /// Writes the document into a file; \p permissions != 0 encrypts it (AES-256, an empty user
    /// password, the owner password "owner-secret") with these bits of Table 22
    bool write(const QString& filePath, uint32_t permissions = 0)
    {
        pdf::PDFDocument document = builder.build();
        if (permissions != 0)
        {
            pdf::PDFDocumentBuilder encrypted(&document);
            pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
            settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
            settings.ownerPassword = QStringLiteral("owner-secret");
            settings.permissions = permissions;
            encrypted.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
            document = encrypted.build();
        }
        return bool(pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false));
    }

    pdf::PDFDocumentBuilder builder;
};

/// Helpers of the test
class OptimizeImagesGuiHelper
{
public:
    OptimizeImagesGuiHelper() = delete;

    /// The sample: page 1 the color image 200 x 100 drawn 100 x 50 pt (144 dpi, 8.9.4), the gray
    /// image drawn 64 x 64 pt (72 dpi); page 2 the bitonal image and the color image again
    static QString writeSample(const QString& folder, pdf::PDFObjectReference* color = nullptr, pdf::PDFObjectReference* gray = nullptr, pdf::PDFObjectReference* bitonal = nullptr)
    {
        OptimizeImagesGuiDocument document;
        const pdf::PDFObjectReference colorImage = document.addColorImage(200, 100);
        const pdf::PDFObjectReference grayImage = document.addGrayImage();
        const pdf::PDFObjectReference bitonalImage = document.addBitonalImage();
        document.addPage({ { colorImage, "100 0 0 50 10 10" }, { grayImage, "64 0 0 64 200 200" } });
        document.addPage({ { bitonalImage, "16 0 0 16 10 10" }, { colorImage, "200 0 0 100 100 100" } });
        if (color)
        {
            *color = colorImage;
        }
        if (gray)
        {
            *gray = grayImage;
        }
        if (bitonal)
        {
            *bitonal = bitonalImage;
        }
        const QString path = folder + QStringLiteral("/images.pdf");
        return document.write(path) ? path : QString();
    }

    /// Two images over each other: the second drawn later covers the first (B64); \p permissions
    /// as OptimizeImagesGuiDocument::write
    static QString writeOverlap(const QString& folder, pdf::PDFObjectReference* below, pdf::PDFObjectReference* above, uint32_t permissions = 0)
    {
        OptimizeImagesGuiDocument document;
        *below = document.addColorImage(40, 40);
        *above = document.addGrayImage();
        document.addPage({ { *below, "200 0 0 200 50 50" }, { *above, "100 0 0 100 100 100" } });
        const QString path = folder + QStringLiteral("/overlap.pdf");
        return document.write(path, permissions) ? path : QString();
    }

    /// The visible owner password dialog of the application (the shared permission check), nullptr = none
    static pdf::PDFAppPasswordDialog* findPasswordDialog()
    {
        for (QWidget* widget : QApplication::allWidgets())
        {
            pdf::PDFAppPasswordDialog* dialog = qobject_cast<pdf::PDFAppPasswordDialog*>(widget);
            if (dialog && dialog->isVisible())
            {
                return dialog;
            }
        }
        return nullptr;
    }

    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    template<typename T>
    static T* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<T*>(shell->getGUI()) : nullptr;
    }

    static bool waitMeasured(PDFAppToolGUIOptimizeImages* gui)
    {
        return AppToolShellFixture::waitFor([gui]() { return gui->getImageList() && gui->getMeasureState() != PDFAppToolGUIOptimizeImages::MeasureState::Measuring; }, 60000);
    }

    static QString key(pdf::PDFObjectReference reference)
    {
        return PDFDocumentImageList::Key::createReference(reference).toString();
    }

    /// The row of the key in the model of the list
    static QModelIndex rowOf(const PDFAppToolImageList* list, pdf::PDFObjectReference reference)
    {
        PDFAppToolImageListModel* model = list->getModel();
        return model->index(model->findRow(PDFDocumentImageList::Key::createReference(reference)), 0);
    }

    /// Selects the rows as the user does (the selection model of the view emits, the GUI follows)
    static void select(PDFAppToolImageList* list, const std::vector<pdf::PDFObjectReference>& references)
    {
        QItemSelection selection;
        QModelIndex first;
        for (const pdf::PDFObjectReference& reference : references)
        {
            const QModelIndex index = list->getProxyModel()->mapFromSource(rowOf(list, reference));
            selection.select(index, index);
            first = first.isValid() ? first : index;
        }
        list->getView()->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        list->getView()->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        AppToolShellFixture::settle();
    }

    /// A list of a document read from the file
    static std::shared_ptr<const PDFDocumentImageList> collect(const pdf::PDFDocument* document)
    {
        return std::make_shared<const PDFDocumentImageList>(PDFDocumentImageList::create(document, PDFDocumentImageList::Options::createForChange(), nullptr, nullptr));
    }
};

/// Watches the focus-out of a field: whether a signal of the field was still connected to the
/// receiver at that moment (the receiver is only an address here, it may be in its destructor;
/// the disconnect also keeps a mutated build from running the slot of a destroyed object)
class OptimizeImagesFocusOutProbe : public QObject
{
public:
    explicit OptimizeImagesFocusOutProbe(const QObject* receiver) : m_receiver(receiver) { }

    bool isFocusOutSeen() const { return m_isFocusOutSeen; }
    bool wasConnected() const { return m_wasConnected; }

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::FocusOut)
        {
            m_isFocusOutSeen = true;
            m_wasConnected = QObject::disconnect(watched, nullptr, m_receiver, nullptr) || m_wasConnected;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    const QObject* m_receiver = nullptr;
    bool m_isFocusOutSeen = false;
    bool m_wasConnected = false;
};

class AppToolOptimizeImagesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void comparisonLayout();
    void comparisonZoomAndSync();
    void imageListRows();
    void imageListChipsAndSort();
    void imageListMarks();
    void profileEditorBinding();
    void profileEditorRebuildIsDeferred();
    void thresholdTypingIsOneUndoStep();
    void profileEditorDeletedWhileTyping();
    void overrideProfileChipIsTheColor();
    void optimizeImagesExcludeAndOverride();
    void overrideToolbarMenus();
    void appearanceImageIsKept();
    void grayscaleMode();
    void extractImagesExport();
    void exportMarkPressKeepsSelection();
    void copyImageNeedsCopyPermission();
    void snapperTopmostImage();
    void screenshots();
};

void AppToolOptimizeImagesTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolOptimizeImagesTest::comparisonLayout()
{
    // document-canvas 297:3201 of 814 x 842: padding 20, the bar 36 at the top, the panes from y 68
    // (158 - 90) side by side 381 wide with the gap 12, BEFORE at the left also for right-to-left
    PDFAppToolImageComparison comparison(nullptr);
    comparison.setLayoutDirection(Qt::RightToLeft);
    comparison.resize(814, 842);
    comparison.setCaptions(QStringLiteral("BEFORE"), QStringLiteral("2480 × 3508 px"), QStringLiteral("AFTER"), QStringLiteral("1654 × 2339 px"));
    comparison.setFooterTexts(QStringLiteral("Estimated size: 1.2 MB → 310 KB"), QStringLiteral("74 % smaller"));
    comparison.show();
    QVERIFY(QTest::qWaitForWindowExposed(&comparison));

    const QWidget* bar = comparison.findChild<QWidget*>(QStringLiteral("imageComparisonBar"));
    QVERIFY(bar);
    QCOMPARE(bar->geometry(), QRect(20, 20, 774, 36));

    const QWidget* before = comparison.getBeforePane();
    const QWidget* after = comparison.getAfterPane();
    const QPoint beforeTop = before->mapTo(&comparison, QPoint(0, 0));
    const QPoint afterTop = after->mapTo(&comparison, QPoint(0, 0));
    QCOMPARE(beforeTop, QPoint(20, 68));
    QCOMPARE(before->width(), 381);
    QCOMPARE(after->width(), 381);
    QCOMPARE(afterTop.x(), 20 + 381 + 12);

    // The segments Fit / 100 % 120 x 32 (871:136734), the chip Sync zoom only by Black & White
    QCOMPARE(comparison.getZoomControl()->size(), QSize(120, 32));
    QVERIFY(comparison.getSyncZoomChip()->isHidden());
    comparison.setSyncZoomVisible(true);
    QCOMPARE(comparison.getSyncZoomChip()->size(), QSize(114, 32));
    QCOMPARE(comparison.getBeforePane()->getCaptionLabel()->text(), QStringLiteral("BEFORE"));
    QCOMPARE(comparison.getFooterLabel()->text(), QStringLiteral("Estimated size: 1.2 MB → 310 KB"));
}

void AppToolOptimizeImagesTest::comparisonZoomAndSync()
{
    PDFAppToolImageComparison comparison(nullptr);
    comparison.resize(814, 842);
    comparison.show();
    QVERIFY(QTest::qWaitForWindowExposed(&comparison));

    QImage before(800, 400, QImage::Format_RGB32);
    before.fill(Qt::red);
    QImage after(400, 200, QImage::Format_RGB32);
    after.fill(Qt::blue);
    comparison.setImages(before, after);

    // Fit: the whole image 11 px inside of its area (298:3308 in 298:3307), the aspect kept
    pdf::PDFAppToolImageComparisonView* beforeView = comparison.getBeforePane()->getView();
    pdf::PDFAppToolImageComparisonView* afterView = comparison.getAfterPane()->getView();
    const QRectF fitRect = beforeView->getImageRect();
    QVERIFY(qAbs(fitRect.width() - (beforeView->width() - 22)) < 0.5 || qAbs(fitRect.height() - (beforeView->height() - 22)) < 0.5);
    QVERIFY(fitRect.width() <= beforeView->width() - 22 + 0.5);
    QCOMPARE(qRound(fitRect.width() * 400.0 / fitRect.height()), 800);

    // 100 %: an image pixel is a device pixel; the images are larger than the areas and move
    QSignalSpy zoomSpy(&comparison, &PDFAppToolImageComparison::zoomChanged);
    QTest::mouseClick(comparison.getZoomControl()->findChildren<QToolButton*>().at(1), Qt::LeftButton);
    QTRY_COMPARE(zoomSpy.count(), 1);
    QCOMPARE(comparison.getZoom(), PDFAppToolImageComparison::Zoom::ActualSize);
    const qreal ratio = beforeView->devicePixelRatioF();
    QCOMPARE(beforeView->getImageRect().size(), QSizeF(800 / ratio, 400 / ratio));
    QVERIFY(beforeView->isMovable());

    // Sync zoom (on by default): a move of BEFORE moves AFTER to the same point of its image
    beforeView->moveBy(QPointF(40, 0));
    QVERIFY(beforeView->getCenter().x() > 0.5);
    QCOMPARE(afterView->getCenter(), beforeView->getCenter());

    // Without Sync zoom each card moves alone
    comparison.setSyncZoom(false);
    const QPointF afterCenter = afterView->getCenter();
    beforeView->moveBy(QPointF(40, 0));
    QCOMPARE(afterView->getCenter(), afterCenter);
    QVERIFY(beforeView->getCenter() != afterCenter);

    // A double click switches back to Fit (a change of the user)
    QTest::mouseDClick(beforeView, Qt::LeftButton, Qt::NoModifier, beforeView->rect().center());
    QCOMPARE(comparison.getZoom(), PDFAppToolImageComparison::Zoom::Fit);
    QTRY_COMPARE(zoomSpy.count(), 2);
    QTest::qWait(50);
    QCOMPARE(zoomSpy.count(), 2);
}

void AppToolOptimizeImagesTest::imageListRows()
{
    using Model = PDFAppToolImageListModel;
    using H = OptimizeImagesGuiHelper;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference color;
    pdf::PDFObjectReference gray;
    pdf::PDFObjectReference bitonal;
    const QString path = H::writeSample(fixture.folder(), &color, &gray, &bitonal);
    QVERIFY(!path.isEmpty());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, path);
    QVERIFY(tab);
    const pdf::PDFDocumentPointer document = tab->getDocument()->getSnapshot().document;

    PDFAppToolImageList list(nullptr);
    list.resize(275, 700);
    list.getModel()->setImageList(H::collect(document.data()), 1);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    // The order of the first drawing: the color image, the gray one, the bitonal one (Image N)
    Model* model = list.getModel();
    QCOMPARE(model->rowCount(), 3);
    const QModelIndex colorRow = H::rowOf(&list, color);
    QCOMPARE(colorRow.row(), 0);
    QCOMPARE(colorRow.data(Qt::DisplayRole).toString(), QStringLiteral("Image 1"));

    // 297:3663: the pixels and the smallest resolution of all drawings: 200 px over 100 pt and
    // 100 px over 50 pt = 144 dpi (page 1); 200 px over 200 pt = 72 dpi on page 2 (8.9.4)
    QCOMPARE(colorRow.data(Model::FirstLineRole).toString(), QStringLiteral("200 × 100 px · 72 dpi"));
    QCOMPARE(colorRow.data(Model::SecondLineRole).toString(), QStringLiteral("DeviceRGB · not compressed"));
    QCOMPARE(H::rowOf(&list, gray).data(Model::SecondLineRole).toString(), QStringLiteral("DeviceGray · not compressed"));

    // Export (341:7676): the pages and the pixels
    model->setRowStyle(Model::RowStyle::Export);
    // (the page lists of the application: consecutive pages as a range, PDFAppLocale::formatPageNumbers)
    QCOMPARE(H::rowOf(&list, color).data(Model::FirstLineRole).toString(), QStringLiteral("Pages 1–2 · 200 × 100 px"));
    QCOMPARE(H::rowOf(&list, gray).data(Model::FirstLineRole).toString(), QStringLiteral("Page 1 · 64 × 64 px"));

    // Heights of the rows: 73 / 79 with a Tag (297:3651, 297:3666), Export 59 (341:7672)
    model->setRowStyle(Model::RowStyle::Optimize);
    pdf::PDFAppToolImageListDelegate delegate(nullptr);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 275, 100);
    QCOMPARE(delegate.sizeHint(option, list.getProxyModel()->mapFromSource(H::rowOf(&list, color))).height(), 73);
    Model::RowState state;
    state.tag = Model::Tag::Override;
    model->setRowState(PDFDocumentImageList::Key::createReference(color), state);
    QCOMPARE(H::rowOf(&list, color).data(Model::TagTextRole).toString(), QStringLiteral("override"));
    QCOMPARE(delegate.sizeHint(option, list.getProxyModel()->mapFromSource(H::rowOf(&list, color))).height(), 79);

    // The Tag of a tool with its own states (Black & White "converted") replaces the text of the tag
    state.badgeText = QStringLiteral("converted");
    model->setRowState(PDFDocumentImageList::Key::createReference(color), state);
    QCOMPARE(H::rowOf(&list, color).data(Model::TagTextRole).toString(), QStringLiteral("converted"));
    state.badgeText.clear();
    model->setRowState(PDFDocumentImageList::Key::createReference(color), state);
    model->setRowStyle(Model::RowStyle::Export);
    QCOMPARE(delegate.sizeHint(option, list.getProxyModel()->mapFromSource(H::rowOf(&list, gray))).height(), 59);

    // Thumbnails of the visible rows by the decoding of the core; a late one of another
    // generation is dropped
    list.setThumbnailSource(document, pdf::PDFCMSPointer());
    QVERIFY(AppToolShellFixture::waitFor([&]() { return model->hasThumbnail(0) && !list.isThumbnailDecodingBusy(); }));
    const QImage thumbnail = H::rowOf(&list, color).data(Model::ThumbnailRole).value<QImage>();
    QCOMPARE(thumbnail.size(), QSize(128, 64));
    model->setThumbnail(PDFDocumentImageList::Key::createReference(color), 7, QImage(1, 1, QImage::Format_RGB32));
    QCOMPARE(H::rowOf(&list, color).data(Model::ThumbnailRole).value<QImage>().size(), QSize(128, 64));
}

void AppToolOptimizeImagesTest::imageListChipsAndSort()
{
    using Model = PDFAppToolImageListModel;
    using H = OptimizeImagesGuiHelper;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference color;
    pdf::PDFObjectReference gray;
    pdf::PDFObjectReference bitonal;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSample(fixture.folder(), &color, &gray, &bitonal));
    QVERIFY(tab);

    PDFAppToolImageList list(nullptr);
    list.getModel()->setImageList(H::collect(tab->getDocument()->getSnapshot().document.data()), 1);
    pdf::PDFAppToolImageListProxyModel* proxy = list.getProxyModel();

    // Largest (297:3624, the default): the color image (60,000 bytes), the gray (4,096), the bitonal (32)
    QCOMPARE(proxy->getSort(), pdf::PDFAppToolImageListProxyModel::Sort::Largest);
    QCOMPARE(proxy->index(0, 0).data(Model::KeyRole).toString(), H::key(color));
    QCOMPARE(proxy->index(2, 0).data(Model::KeyRole).toString(), H::key(bitonal));
    list.setSort(pdf::PDFAppToolImageListProxyModel::Sort::Smallest);
    QCOMPARE(proxy->index(0, 0).data(Model::KeyRole).toString(), H::key(bitonal));

    // The chips: Color shows the color images by their declaration (R4-5), Excluded the excluded ones
    list.setChips({ { QStringLiteral("all"), QStringLiteral("All"), 3 }, { QStringLiteral("color"), QStringLiteral("Color"), 1 }, { QStringLiteral("excluded"), QStringLiteral("Excluded"), 1 } }, QStringLiteral("all"));
    QCOMPARE(proxy->rowCount(), 3);
    QSignalSpy chipSpy(&list, &PDFAppToolImageList::chipChanged);
    QTest::mouseClick(list.getChipButton(QStringLiteral("color")), Qt::LeftButton);
    QTRY_COMPARE(chipSpy.count(), 1);
    QCOMPARE(proxy->rowCount(), 1);
    QCOMPARE(proxy->index(0, 0).data(Model::KeyRole).toString(), H::key(color));

    Model::RowState excluded;
    excluded.tag = Model::Tag::Excluded;
    list.getModel()->setRowState(PDFDocumentImageList::Key::createReference(gray), excluded);
    QTest::mouseClick(list.getChipButton(QStringLiteral("excluded")), Qt::LeftButton);
    QCOMPARE(proxy->rowCount(), 1);
    QCOMPARE(proxy->index(0, 0).data(Model::KeyRole).toString(), H::key(gray));

    // The count of a chip is drawn by the style (Show count of the Filter chip)
    QCOMPARE(list.getChipButton(QStringLiteral("color"))->property("chipCount").toInt(), 1);

    // The pages of Extract Images: only the images of page 2
    QTest::mouseClick(list.getChipButton(QStringLiteral("all")), Qt::LeftButton);
    proxy->setPageFilter(std::set<pdf::PDFInteger>{ 1 });
    QCOMPARE(proxy->rowCount(), 2);
}

void AppToolOptimizeImagesTest::imageListMarks()
{
    using Model = PDFAppToolImageListModel;
    using H = OptimizeImagesGuiHelper;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference color;
    pdf::PDFObjectReference gray;
    pdf::PDFObjectReference bitonal;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSample(fixture.folder(), &color, &gray, &bitonal));
    QVERIFY(tab);

    PDFAppToolImageList list(nullptr);
    list.setRowStyle(Model::RowStyle::Export);
    list.getModel()->setImageList(H::collect(tab->getDocument()->getSnapshot().document.data()), 1);
    list.resize(275, 600);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    // 341:7774: "Space leaves the selected images out or puts them back"
    QSignalSpy markSpy(&list, &PDFAppToolImageList::markToggled);
    list.setSelectedKeys({ PDFDocumentImageList::Key::createReference(color), PDFDocumentImageList::Key::createReference(gray) });
    QCOMPARE(list.getSelectedKeys().size(), size_t(2));
    QTest::keyClick(list.getView(), Qt::Key_Space);
    QTRY_COMPARE(markSpy.count(), 1);
    QCOMPARE(markSpy.at(0).at(1).toBool(), false);
    QCOMPARE(markSpy.at(0).at(0).value<std::vector<PDFDocumentImageList::Key>>().size(), size_t(2));

    // One of them is left out: Space puts both back
    Model::RowState leftOut;
    leftOut.isMarked = false;
    leftOut.tag = Model::Tag::LeftOut;
    list.getModel()->setRowState(PDFDocumentImageList::Key::createReference(gray), leftOut);
    QTest::keyClick(list.getView(), Qt::Key_Space);
    QTRY_COMPARE(markSpy.count(), 2);
    QCOMPARE(markSpy.at(1).at(1).toBool(), true);

    // The mark of a row (341:7670) toggles only its row
    list.getView()->scrollTo(list.getProxyModel()->mapFromSource(H::rowOf(&list, color)));
    const QRect rowRect = list.getView()->visualRect(list.getProxyModel()->mapFromSource(H::rowOf(&list, color)));
    const QPoint markCenter = pdf::PDFAppToolImageListDelegate::getMarkRect(QRectF(rowRect), Qt::LeftToRight).center().toPoint();
    QTest::mouseClick(list.getView()->viewport(), Qt::LeftButton, Qt::NoModifier, markCenter);
    QTRY_COMPARE(markSpy.count(), 3);
    const std::vector<PDFDocumentImageList::Key> keys = markSpy.at(2).at(0).value<std::vector<PDFDocumentImageList::Key>>();
    QCOMPARE(keys.size(), size_t(1));
    QCOMPARE(keys.front(), PDFDocumentImageList::Key::createReference(color));
}

void AppToolOptimizeImagesTest::profileEditorBinding()
{
    using Tool = pdf::PDFDocumentToolOptimizeImages;

    // The model of Optimize Images: the editor writes the flattened profiles and the exceptions
    pdf::PDFAppToolParameterModel model(nullptr, QString::fromLatin1(Tool::TOOL_ID), Tool().getParameters(), nullptr);
    pdf::PDFAppToolImageProfileEditor editor(&model, pdf::PDFAppToolImageProfileEditor::Mode::ThreeProfiles, nullptr);
    editor.show();

    // 298:3332: Settings editor · Global defaults, the profile Color
    QCOMPARE(editor.getScopeTag()->text(), QStringLiteral("Global defaults"));
    QCOMPARE(editor.getProfile(), QStringLiteral("color"));
    pdf::PDFAppSettingControl* dpi = editor.getControl(QStringLiteral("colorTargetDpi"));
    QVERIFY(dpi && dpi->getSpinBox());
    QCOMPARE(dpi->getSpinBox()->size(), QSize(140, 34));
    // 298:3372 "200 dpi": the unit is the muted suffix (RS4FIGMA), not edited text
    QCOMPARE(dpi->getSpinBox()->getDisplaySuffix(), QStringLiteral("dpi"));
    QVERIFY(dpi->getSpinBox()->suffix().isEmpty());
    dpi->getSpinBox()->setValue(200);
    dpi->commitPendingValue();
    QCOMPARE(model.getValue(QStringLiteral("colorTargetDpi")).toInt(), 200);

    // The segment Bitonal shows the threshold with Auto (301:3594), no JPEG quality
    QSignalSpy profileSpy(&editor, &pdf::PDFAppToolImageProfileEditor::profileChanged);
    QTest::mouseClick(editor.getProfileChip(QStringLiteral("bitonal")), Qt::LeftButton);
    QCOMPARE(profileSpy.count(), 1);
    QVERIFY(editor.getThresholdAutoCheckBox());
    QVERIFY(editor.getThresholdAutoCheckBox()->isChecked());
    QVERIFY(!editor.getThresholdSpinBox()->isEnabled());
    QVERIFY(!editor.getControl(QStringLiteral("bitonalJpegQuality")));
    // (the threshold is written from the event loop: the check box is still in its click)
    editor.getThresholdAutoCheckBox()->click();
    QTRY_COMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 128);

    // An override of an image (301:3594 "Override · 1 images"): the value goes into its exception,
    // the global defaults stay
    const PDFDocumentImageList::Key key = PDFDocumentImageList::Key::createReference(pdf::PDFObjectReference(148, 0));
    editor.setOverrideTarget({ key });
    QCOMPARE(editor.getScopeTag()->text(), QStringLiteral("Override · 1 images"));
    pdf::PDFAppSettingControl* overrideDpi = editor.getControl(QStringLiteral("bitonalTargetDpi"));
    QVERIFY(overrideDpi && overrideDpi->getSpinBox());
    overrideDpi->getSpinBox()->setValue(400);
    overrideDpi->commitPendingValue();
    QString error;
    const std::vector<Tool::ImageException> exceptions = Tool::parseImageExceptions(model.getValue(QStringLiteral("images")).toString(), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(exceptions.size(), size_t(1));
    QCOMPARE(exceptions.front().key, key);
    QVERIFY(exceptions.front().enabled);
    QCOMPARE(exceptions.front().settings.value(QStringLiteral("targetDpi")).toInt(), 400);
    QCOMPARE(model.getValue(QStringLiteral("bitonalTargetDpi")).toInt(), 300);

    // Undo returns the exception (one step)
    model.undo();
    QVERIFY(model.getValue(QStringLiteral("images")).toString().isEmpty());
}

void AppToolOptimizeImagesTest::profileEditorRebuildIsDeferred()
{
    // CLAUDE.md "never delete the sender synchronously": a rebuild of the editor runs also inside of
    // a signal of its own control (the threshold writes the model, the tool shows the profile of the
    // override), so its old controls are hidden at once and deleted from the event loop
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    pdf::PDFAppToolParameterModel model(nullptr, QString::fromLatin1(Tool::TOOL_ID), Tool().getParameters(), nullptr);
    pdf::PDFAppToolImageProfileEditor editor(&model, pdf::PDFAppToolImageProfileEditor::Mode::ThreeProfiles, nullptr);
    editor.show();
    editor.setProfile(QStringLiteral("bitonal"));
    QPointer<QSpinBox> threshold = editor.getThresholdSpinBox();
    QVERIFY(threshold);
    QCoreApplication::processEvents();
    QVERIFY(threshold->isVisible());

    // Another profile: the threshold is gone from the editor, but still alive (it may be the sender)
    editor.setProfile(QStringLiteral("color"));
    QVERIFY(!editor.getThresholdSpinBox());
    QVERIFY(threshold);
    QVERIFY(!threshold->isVisible());
    QVERIFY(threshold->objectName().isEmpty());
    QVERIFY(editor.getControl(QStringLiteral("colorJpegQuality")));

    // The event loop deletes it
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!threshold);
}

void AppToolOptimizeImagesTest::thresholdTypingIsOneUndoStep()
{
    // RS4APP: typing "200" into the threshold is one undo step (as the fields of the binder: the
    // value is written after a rest or at Enter), not the steps "2", "20" and "200"
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    pdf::PDFAppToolParameterModel model(nullptr, QString::fromLatin1(Tool::TOOL_ID), Tool().getParameters(), nullptr);
    pdf::PDFAppToolImageProfileEditor editor(&model, pdf::PDFAppToolImageProfileEditor::Mode::ThreeProfiles, nullptr);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    editor.setProfile(QStringLiteral("bitonal"));
    editor.getThresholdAutoCheckBox()->click();
    QTRY_COMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 128);

    QSpinBox* threshold = editor.getThresholdSpinBox();
    QVERIFY(threshold && threshold->isEnabled());
    threshold->setFocus();
    threshold->selectAll();
    for (const QChar digit : QStringLiteral("200"))
    {
        // The event loop runs between the keys as between the keys of the user
        QTest::keyClick(threshold, digit.toLatin1());
        QCoreApplication::processEvents();
    }
    QTRY_COMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 200);

    // One Undo returns the value before the typing
    model.undo();
    QCOMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 128);

    // Enter writes a typed value at once (no waiting for the rest)
    threshold = editor.getThresholdSpinBox();
    threshold->selectAll();
    QTest::keyClicks(threshold, QStringLiteral("90"));
    QTest::keyClick(threshold, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 90);
}

void AppToolOptimizeImagesTest::profileEditorDeletedWhileTyping()
{
    // UBSan finding: ~QWidget of the editor (its derived part is gone) closes its window and moves
    // the focus out of the threshold field; QAbstractSpinBox emits editingFinished then. At that
    // moment no signal of the field may be connected to the editor any more
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    using Editor = pdf::PDFAppToolImageProfileEditor;
    pdf::PDFAppToolParameterModel model(nullptr, QString::fromLatin1(Tool::TOOL_ID), Tool().getParameters(), nullptr);
    QPointer<Editor> editor = new Editor(&model, Editor::Mode::ThreeProfiles, nullptr);
    auto guard = qScopeGuard([&editor]() { delete editor.data(); });
    editor->show();
    editor->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(editor.data()));
    editor->setProfile(QStringLiteral("bitonal"));
    editor->getThresholdAutoCheckBox()->click();
    QTRY_COMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 128);

    // A typed digit: the field has the focus, the write waits for the rest (the timer runs)
    QPointer<QSpinBox> threshold = editor->getThresholdSpinBox();
    QVERIFY(threshold && threshold->isEnabled());
    threshold->setFocus();
    QVERIFY(threshold->hasFocus());
    threshold->selectAll();
    QTest::keyClick(threshold.data(), '7');
    QCOMPARE(threshold->value(), 7);
    QCOMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 128);

    OptimizeImagesFocusOutProbe probe(editor.data());
    threshold->installEventFilter(&probe);
    delete editor.data();
    QVERIFY(!editor);
    QVERIFY(!threshold);
    QVERIFY(probe.isFocusOutSeen());
    QVERIFY(!probe.wasConnected());

    // Nothing of the editor runs afterwards (its pending write ends with it)
    QCoreApplication::processEvents();
    QCOMPARE(model.getValue(QStringLiteral("bitonalThreshold")).toInt(), 128);
}

void AppToolOptimizeImagesTest::overrideProfileChipIsTheColor()
{
    // 301:3594 "Toolbar menus change the override too": the editor of an override with its own
    // color shows the profile of that color; a chip changes the color of the override (else its
    // fields would edit another color and the tool would switch the editor back). An override of
    // Auto keeps Auto.
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    using Store = pdf::PDFAppToolImageOverrideStore;
    pdf::PDFAppToolParameterModel model(nullptr, QString::fromLatin1(Tool::TOOL_ID), Tool().getParameters(), nullptr);
    pdf::PDFAppToolImageProfileEditor editor(&model, pdf::PDFAppToolImageProfileEditor::Mode::ThreeProfiles, nullptr);
    editor.show();

    const PDFDocumentImageList::Key key = PDFDocumentImageList::Key::createReference(pdf::PDFObjectReference(148, 0));
    const QString colorMode = QString::fromLatin1(Tool::EXCEPTION_COLOR_MODE);
    QVERIFY(Store::writeSetting(&model, { key }, colorMode, QStringLiteral("color")).isEmpty());
    editor.setOverrideTarget({ key });

    QSignalSpy profileSpy(&editor, &pdf::PDFAppToolImageProfileEditor::profileChanged);
    QTest::mouseClick(editor.getProfileChip(QStringLiteral("bitonal")), Qt::LeftButton);
    QCOMPARE(profileSpy.count(), 1);
    QCOMPARE(Store::readSetting(&model, key, colorMode).toString(), QStringLiteral("bitonal"));
    QCOMPARE(editor.getProfile(), QStringLiteral("bitonal"));

    // The threshold of the override goes into its exception, the color stays bitonal
    // (the write comes from the event loop, never inside of the click: a slot of the model may
    // delete the window with the check box, imagesoverrideeditor of UnitTestsAppToolPagesLifetime)
    editor.getThresholdAutoCheckBox()->click();
    QVERIFY(!Store::readSetting(&model, key, QString::fromLatin1(Tool::EXCEPTION_THRESHOLD)).isValid());
    QTRY_COMPARE(Store::readSetting(&model, key, QString::fromLatin1(Tool::EXCEPTION_THRESHOLD)).toInt(), 128);
    QCOMPARE(Store::readSetting(&model, key, colorMode).toString(), QStringLiteral("bitonal"));

    // An override of Auto: the chip only shows the fields
    QVERIFY(Store::writeSetting(&model, { key }, colorMode, QStringLiteral("auto")).isEmpty());
    QTest::mouseClick(editor.getProfileChip(QStringLiteral("gray")), Qt::LeftButton);
    QCOMPARE(editor.getProfile(), QStringLiteral("gray"));
    QCOMPARE(Store::readSetting(&model, key, colorMode).toString(), QStringLiteral("auto"));
}

void AppToolOptimizeImagesTest::optimizeImagesExcludeAndOverride()
{
    using H = OptimizeImagesGuiHelper;
    using Tool = pdf::PDFDocumentToolOptimizeImages;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference color;
    pdf::PDFObjectReference gray;
    pdf::PDFObjectReference bitonal;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSample(fixture.folder(), &color, &gray, &bitonal));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID));
    PDFAppToolGUIOptimizeImages* gui = H::gui<PDFAppToolGUIOptimizeImages>(shell);
    QVERIFY(gui);
    QVERIFY(!gui->isGrayscale());
    QVERIFY(H::waitMeasured(gui));

    // 297:3623 "3 images · …", the chips All · Overrides · Excluded
    PDFAppToolImageList* list = gui->getImageListWidget();
    QVERIFY(list->getHeadLabel()->text().startsWith(QStringLiteral("3 images · ")));
    QVERIFY(list->getChipButton(QStringLiteral("overrides")));
    QVERIFY(gui->getColorButton());
    QCOMPARE(gui->getColorButton()->text(), QStringLiteral("Color: Auto"));

    // Q4.9: the measured rows have their real sizes ("60 KB → …" of the uncompressed gradient),
    // the AFTER of the canvas is the image of the measured document (72 dpi < the target 150:
    // its pixels stay 200 x 100)
    QCOMPARE(gui->getMeasureState(), PDFAppToolGUIOptimizeImages::MeasureState::Measured);
    QVERIFY(H::rowOf(list, color).data(PDFAppToolImageListModel::SizeTextRole).toString().contains(QStringLiteral(" → ")));
    H::select(list, { color });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->getComparison()->getAfterImage().isNull() && !gui->getComparison()->getBeforeImage().isNull(); }));
    QCOMPARE(gui->getComparison()->getBeforeImage().size(), QSize(200, 100));
    QCOMPARE(gui->getComparison()->getAfterImage().size(), QSize(200, 100));
    QVERIFY(gui->getComparison()->getFooterLabel()->text().startsWith(QStringLiteral("Estimated size: ")));

    // Owner 2026-11-14: the caption is "AFTER" only - the image is the real encoding, no simulation
    QCOMPARE(gui->getComparison()->getBeforePane()->getCaptionLabel()->text(), QStringLiteral("BEFORE"));
    QCOMPARE(gui->getComparison()->getAfterPane()->getCaptionLabel()->text(), QStringLiteral("AFTER"));

    // Exclude of the selected image writes "<id>:off" (one undo step) and tags the row
    H::select(list, { color });
    QTest::mouseClick(gui->getExcludeButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getExcludedKeys().size() == 1; }));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("images")).toString(), H::key(color) + QStringLiteral(":off"));
    QCOMPARE(H::rowOf(list, color).data(PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("excluded"));
    QVERIFY(!gui->getEnableCheckBox()->isChecked());

    // Exclude again puts it back; the exceptions are empty again
    QTest::mouseClick(gui->getExcludeButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getExcludedKeys().empty(); }));
    QVERIFY(shell->getModel()->getValue(QStringLiteral("images")).toString().isEmpty());

    // Space of the list does the same (plan 4.1, the list of Images and Grayscale): out, then back
    QTest::keyClick(list->getView(), Qt::Key_Space);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getExcludedKeys().size() == 1; }));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("images")).toString(), H::key(color) + QStringLiteral(":off"));
    QTest::keyClick(list->getView(), Qt::Key_Space);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getExcludedKeys().empty(); }));

    // Override (301:3594): the button is on, the editor edits the override, Color writes it
    QTest::mouseClick(gui->getOverrideButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getOverrideKeys().size() == 1; }));
    QVERIFY(gui->getOverrideButton()->isChecked());
    QCOMPARE(gui->getProfileEditor()->getOverrideTarget().size(), size_t(1));
    QCOMPARE(H::rowOf(list, color).data(PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("override"));
    QTest::mouseClick(gui->getColorButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getColorRow(QStringLiteral("bitonal")) && gui->getColorRow(QStringLiteral("bitonal"))->isVisible(); }));
    QTest::mouseClick(gui->getColorRow(QStringLiteral("bitonal")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getColorButton()->text() == QStringLiteral("Color: Bitonal"); }));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("colorMode")).toString(), QStringLiteral("auto"));
    QCOMPARE(gui->getProfileEditor()->getProfile(), QStringLiteral("bitonal"));

    // The chip Grayscale of the override's editor is its color (the Color menu follows); the
    // threshold of Bitonal typed then stays in the editor - it is not switched back below its own
    // spin box (which would delete it inside of its own signal)
    QTest::mouseClick(gui->getProfileEditor()->getProfileChip(QStringLiteral("gray")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getColorButton()->text() == QStringLiteral("Color: Grayscale"); }));
    QCOMPARE(gui->getProfileEditor()->getProfile(), QStringLiteral("gray"));
    QTest::mouseClick(gui->getProfileEditor()->getProfileChip(QStringLiteral("bitonal")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getColorButton()->text() == QStringLiteral("Color: Bitonal"); }));
    gui->getProfileEditor()->getThresholdAutoCheckBox()->click();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getProfileEditor()->getThresholdSpinBox() && gui->getProfileEditor()->getThresholdSpinBox()->isEnabled(); }));
    QPointer<QSpinBox> threshold = gui->getProfileEditor()->getThresholdSpinBox();
    threshold->stepUp();
    QVERIFY(AppToolShellFixture::waitFor([shell, color]() { return pdf::PDFAppToolImageOverrideStore::readSetting(shell->getModel(), PDFDocumentImageList::Key::createReference(color), QStringLiteral("threshold")).toInt() == 129; }));
    QVERIFY(threshold);
    QCOMPARE(gui->getProfileEditor()->getThresholdSpinBox(), threshold.data());
    QCOMPARE(gui->getProfileEditor()->getProfile(), QStringLiteral("bitonal"));

    // The selection of two images, one without an override, shows the global defaults
    H::select(list, { color, gray });
    QVERIFY(gui->getProfileEditor()->getOverrideTarget().empty());
    QVERIFY(!gui->getOverrideButton()->isChecked());
}

void AppToolOptimizeImagesTest::overrideToolbarMenus()
{
    // Owner 14. 11.: for an overridden selection Mode / Goal / Options change the override and the
    // Mode button says "Mode: Custom" (301:3594); 299:3362 "Goal: Quality"; the menu Color has icons
    // for Auto and Preserve only (Color, Grayscale and Bitonal have swatches in Figma, RS4FIGMA)
    using H = OptimizeImagesGuiHelper;
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    using Store = pdf::PDFAppToolImageOverrideStore;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference color;
    pdf::PDFObjectReference gray;
    pdf::PDFObjectReference bitonal;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSample(fixture.folder(), &color, &gray, &bitonal));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID));
    PDFAppToolGUIOptimizeImages* gui = H::gui<PDFAppToolGUIOptimizeImages>(shell);
    QVERIFY(gui);
    QVERIFY(H::waitMeasured(gui));
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    PDFAppToolImageList* list = gui->getImageListWidget();
    const PDFDocumentImageList::Key colorKey = PDFDocumentImageList::Key::createReference(color);

    // Owner 14. 11.: percentages are whole numbers ("84 %" of Compression ratio 298:3442)
    QString ratio;
    for (int i = 0; i < gui->getSummaryValues()->getRowCount(); ++i)
    {
        if (gui->getSummaryValues()->getKey(i) == QStringLiteral("Compression ratio"))
        {
            ratio = gui->getSummaryValues()->getValue(i);
        }
    }
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d{1,3} %$")).match(ratio).hasMatch(), qPrintable(ratio));

    QVERIFY(gui->getModeMenu() && gui->getGoalMenu() && gui->getOptionsMenu());
    QCOMPARE(gui->getModeMenu()->getButton()->text(), QStringLiteral("Mode: Auto"));
    QCOMPARE(gui->getGoalMenu()->getButton()->text(), QStringLiteral("Goal: Quality"));
    // The icons of 299:3384 (image-color-mode 1067:1564), 299:3389 (image-exclude 1052:1589) and of
    // the confirm button of 297:3135 (image)
    QCOMPARE(pdf::PDFThemeIcons::getIconName(gui->getColorButton()->icon()), QStringLiteral("image-color-mode"));
    QCOMPARE(pdf::PDFThemeIcons::getIconName(gui->getExcludeButton()->icon()), QStringLiteral("image-exclude"));
    QCOMPARE(gui->getConfirmIconName(), QStringLiteral("image"));
    QVERIFY(pdf::PDFThemeIcons::hasIcon(QStringLiteral("image-color-mode")) && pdf::PDFThemeIcons::hasIcon(QStringLiteral("image-exclude")));
    QCOMPARE(gui->getColorRow(QStringLiteral("auto"))->getIconName(), QStringLiteral("zap"));
    QCOMPARE(gui->getColorRow(QStringLiteral("preserve"))->getIconName(), QStringLiteral("lock"));
    for (const QString& colorMode : { QStringLiteral("color"), QStringLiteral("grayscale"), QStringLiteral("bitonal") })
    {
        QVERIFY2(gui->getColorRow(colorMode)->getIconName().isEmpty(), qPrintable(colorMode));
    }

    // A new override is "Mode: Custom"; the global Mode stays Auto
    H::select(list, { color });
    QTest::mouseClick(gui->getOverrideButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getOverrideKeys().size() == 1; }));
    QCOMPARE(gui->getModeMenu()->getButton()->text(), QStringLiteral("Mode: Custom"));
    QCOMPARE(Store::readSetting(model, colorKey, QStringLiteral("mode")).toString(), QStringLiteral("custom"));
    QCOMPARE(model->getValue(QStringLiteral("mode")).toString(), QStringLiteral("auto"));

    // Goal of the override: the row writes the exception, the global Goal stays
    gui->getGoalMenu()->getButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getGoalMenu()->getPanel()->isVisible(); }));
    QTest::mouseClick(gui->getGoalMenu()->getRow(QStringLiteral("minimum-size")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getGoalMenu()->getButton()->text() == QStringLiteral("Goal: Minimum"); }));
    QCOMPARE(Store::readSetting(model, colorKey, QStringLiteral("goal")).toString(), QStringLiteral("minimum-size"));
    QCOMPARE(model->getValue(QStringLiteral("goal")).toString(), QStringLiteral("prefer-quality"));

    // Options of the override: Keep original if larger off for the image only
    pdf::PDFTileRow* keepRow = gui->getOptionsMenu()->getPanel()->findChild<pdf::PDFTileRow*>(QStringLiteral("toolFlag-keepOriginalIfLarger"));
    QVERIFY(keepRow && keepRow->isChecked());
    gui->getOptionsMenu()->getButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([keepRow]() { return keepRow->isVisible(); }));
    QTest::mouseClick(keepRow, Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([model, colorKey]() { return Store::readSetting(model, colorKey, QStringLiteral("keepOriginalIfLarger")).isValid(); }));
    QCOMPARE(Store::readSetting(model, colorKey, QStringLiteral("keepOriginalIfLarger")).toBool(), false);
    QVERIFY(model->getValue(QStringLiteral("keepOriginalIfLarger")).toBool());
    QVERIFY(!keepRow->isChecked());
    gui->getOptionsMenu()->getPanel()->close();

    // Mode Auto of the override
    gui->getModeMenu()->getButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getModeMenu()->getPanel()->isVisible(); }));
    QTest::mouseClick(gui->getModeMenu()->getRow(QStringLiteral("auto")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getModeMenu()->getButton()->text() == QStringLiteral("Mode: Auto"); }));
    QCOMPARE(Store::readSetting(model, colorKey, QStringLiteral("mode")).toString(), QStringLiteral("auto"));

    // An image without an override shows and writes the global values
    H::select(list, { gray });
    QCOMPARE(gui->getGoalMenu()->getButton()->text(), QStringLiteral("Goal: Quality"));
    QVERIFY(keepRow->isChecked());
    gui->getGoalMenu()->getButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getGoalMenu()->getPanel()->isVisible(); }));
    QTest::mouseClick(gui->getGoalMenu()->getRow(QStringLiteral("minimum-size")), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([model]() { return model->getValue(QStringLiteral("goal")).toString() == QStringLiteral("minimum-size"); }));
    QVERIFY(!Store::readSetting(model, PDFDocumentImageList::Key::createReference(gray), QStringLiteral("goal")).isValid());

    // The override is in the run: the parameters of the core read it back (the syntax of the core)
    QString error;
    const std::vector<Tool::ImageException> exceptions = Tool::parseImageExceptions(model->getValue(QStringLiteral("images")).toString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(exceptions.size(), size_t(1));
    const pdf::PDFImageOptimizer::Settings imageSettings = Tool::createImageSettings(Tool::createSettings(model->getValues()), exceptions.front());
    QCOMPARE(imageSettings.goal, pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize);
    QVERIFY(!imageSettings.keepOriginalIfLarger);
    QVERIFY(imageSettings.autoMode);
}

void AppToolOptimizeImagesTest::appearanceImageIsKept()
{
    // R4-4 (P2S4FIXCORE: PDFDocumentImageList::Item::isInAppearance): an image, which the appearance
    // of an annotation (ISO 32000-2, 12.5.5) draws too, is kept by the changing tools - the row and the
    // canvas say why with their own text, not the text of an inline image
    using H = OptimizeImagesGuiHelper;
    using Tool = pdf::PDFDocumentToolOptimizeImages;
    OptimizeImagesGuiDocument document;
    const pdf::PDFObjectReference stamp = document.addColorImage(40, 40);
    const pdf::PDFObjectReference photo = document.addColorImage(60, 30);
    const pdf::PDFObjectReference page = document.addPage({ { stamp, "40 0 0 40 10 10" }, { photo, "60 0 0 30 100 100" } });
    const pdf::PDFObjectReference appearance = document.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 40 40] /Resources << /XObject << /St " + OptimizeImagesGuiDocument::ref(stamp) + " >> >> >>",
                                                                  "q 40 0 0 40 0 0 cm /St Do Q");
    const pdf::PDFObjectReference annotation = document.builder.addObject(OptimizeImagesGuiDocument::parse("<< /Type /Annot /Subtype /Stamp /Rect [200 200 240 240] /F 4 /AP << /N " + OptimizeImagesGuiDocument::ref(appearance) + " >> >>"));
    document.builder.mergeTo(page, OptimizeImagesGuiDocument::parse("<< /Annots [" + OptimizeImagesGuiDocument::ref(annotation) + "] >>"));

    AppToolShellFixture fixture;
    const QString path = fixture.folder() + QStringLiteral("/stamp.pdf");
    QVERIFY(document.write(path));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, path);
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID));
    PDFAppToolGUIOptimizeImages* gui = H::gui<PDFAppToolGUIOptimizeImages>(shell);
    QVERIFY(gui);
    QVERIFY(H::waitMeasured(gui));
    PDFAppToolImageList* list = gui->getImageListWidget();
    QCOMPARE(H::rowOf(list, stamp).data(PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("in an annotation · kept"));
    QVERIFY(H::rowOf(list, photo).data(PDFAppToolImageListModel::TagTextRole).toString().isEmpty());

    H::select(list, { stamp });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getComparison()->getAfterPane()->getInfoLabel()->text() == QStringLiteral("Kept · an annotation appearance uses this image"); }));
}

void AppToolOptimizeImagesTest::grayscaleMode()
{
    using H = OptimizeImagesGuiHelper;
    using Gray = pdf::PDFDocumentToolGrayscale;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference color;
    pdf::PDFObjectReference gray;
    pdf::PDFObjectReference bitonal;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSample(fixture.folder(), &color, &gray, &bitonal));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Gray::TOOL_ID));
    PDFAppToolGUIOptimizeImages* gui = H::gui<PDFAppToolGUIOptimizeImages>(shell);
    QVERIFY(gui);
    QVERIFY(gui->isGrayscale());
    QVERIFY(H::waitMeasured(gui));

    // 308:6911: "Convert to Grayscale", no Mode / Color / Override, "3 images · 1 in color"
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Convert to Grayscale"));
    QVERIFY(!gui->getColorButton());
    QVERIFY(!gui->getOverrideButton());
    PDFAppToolImageList* list = gui->getImageListWidget();
    QCOMPARE(list->getHeadLabel()->text(), QStringLiteral("3 images · 1 in color"));
    QVERIFY(list->getChipButton(QStringLiteral("color")));

    // 308:6978: the gray and the bitonal image are skipped, dimmed, with their tags
    QCOMPARE(H::rowOf(list, gray).data(PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("already gray"));
    QVERIFY(H::rowOf(list, gray).data(PDFAppToolImageListModel::DimmedRole).toBool());
    QVERIFY(H::rowOf(list, gray).data(PDFAppToolImageListModel::SizeTextRole).toString().endsWith(QStringLiteral(" · skipped")));
    QCOMPARE(H::rowOf(list, bitonal).data(PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("bitonal"));
    QVERIFY(!H::rowOf(list, color).data(PDFAppToolImageListModel::DimmedRole).toBool());

    // Exclude writes "exclude" (R4-20), the color image keeps its color
    H::select(list, { color });
    QTest::mouseClick(gui->getExcludeButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getModel()->getValue(QStringLiteral("exclude")).toString().isEmpty(); }));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("exclude")).toString(), H::key(color));

    // An image, which is gray, cannot be excluded: the selection has no changeable image
    H::select(list, { gray });
    QVERIFY(!gui->getExcludeButton()->isEnabled());
}

void AppToolOptimizeImagesTest::extractImagesExport()
{
    using H = OptimizeImagesGuiHelper;
    using Tool = pdf::PDFDocumentToolExtractImages;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference below;
    pdf::PDFObjectReference above;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeOverlap(fixture.folder(), &below, &above));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID));
    PDFAppToolGUIExtractImages* gui = H::gui<PDFAppToolGUIExtractImages>(shell);
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getImageList() && !gui->isComputing() && gui->getPlan().has_value(); }, 60000));

    // 341:7516: the rows of an export, "2 images · 2 to export", the names of the files
    PDFAppToolImageList* list = gui->getImageListWidget();
    QCOMPARE(list->getModel()->getRowStyle(), PDFAppToolImageListModel::RowStyle::Export);
    QCOMPARE(list->getHeadLabel()->text(), QStringLiteral("2 images · 2 to export"));
    QCOMPARE(H::rowOf(list, below).data(PDFAppToolImageListModel::SecondLineRole).toString(), QStringLiteral("overlap_img_1.png"));
    QCOMPARE(gui->getPagesButton()->text(), QStringLiteral("Pages: All"));
    QCOMPARE(gui->getFormatButton()->text(), QStringLiteral("Format: PNG"));

    // B64 on the page: a click on the overlap selects the image drawn later (the one above)
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getOverlay() && gui->getOverlay()->getPlacements().size() == 2; }));
    pdf::PDFAppToolImagePlacementOverlay* overlay = gui->getOverlay();
    QVERIFY(AppToolShellFixture::waitFor([overlay]() { return overlay->isVisible() && !overlay->getPageToOverlayTransform().isIdentity(); }));
    const QPoint overlapPoint = overlay->getPageToOverlayTransform().map(QPointF(150, 150)).toPoint();
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, overlapPoint);
    QCOMPARE(list->getCurrentKey(), PDFDocumentImageList::Key::createReference(above));
    QCOMPARE(overlay->getSelectedKey(), PDFDocumentImageList::Key::createReference(above));

    // Space leaves the selected image out: the parameter exclude, the row "left out"
    QTest::keyClick(list->getView(), Qt::Key_Space);
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell->getModel()->getValue(QStringLiteral("exclude")).toString().isEmpty(); }));
    QCOMPARE(gui->getExcludedKeys(), std::vector<PDFDocumentImageList::Key>{ PDFDocumentImageList::Key::createReference(above) });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->isComputing(); }, 60000));
    QCOMPARE(H::rowOf(list, above).data(PDFAppToolImageListModel::TagTextRole).toString(), QStringLiteral("left out"));
    QCOMPARE(list->getHeadLabel()->text(), QStringLiteral("2 images · 1 to export"));

    // Include all puts it back
    QTest::mouseClick(gui->getIncludeAllButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getExcludedKeys().empty(); }));

    // Copy Image: the selected image with its pixels on the clipboard (R4-12)
    QApplication::clipboard()->clear();
    QTest::mouseClick(gui->getCopyButton(), Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([]() { return !QApplication::clipboard()->image().isNull(); }));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(64, 64));
}

void AppToolOptimizeImagesTest::exportMarkPressKeepsSelection()
{
    // RS4APP, 341:7670: the click on the mark of an Export row is the mark only - already its
    // press keeps the selection of the list (the canvas and Selected image stay); the release
    // leaves the image out
    using H = OptimizeImagesGuiHelper;
    using Tool = pdf::PDFDocumentToolExtractImages;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference below;
    pdf::PDFObjectReference above;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeOverlap(fixture.folder(), &below, &above));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID));
    PDFAppToolGUIExtractImages* gui = H::gui<PDFAppToolGUIExtractImages>(shell);
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getImageList() && !gui->isComputing() && gui->getPlan().has_value(); }, 60000));
    PDFAppToolImageList* list = gui->getImageListWidget();
    H::select(list, { below });
    AppToolShellFixture::settle();
    const std::vector<PDFDocumentImageList::Key> selected = list->getSelectedKeys();
    QCOMPARE(selected, std::vector<PDFDocumentImageList::Key>{ PDFDocumentImageList::Key::createReference(below) });

    const QModelIndex aboveRow = H::rowOf(list, above);
    QVERIFY(aboveRow.isValid());
    const QModelIndex viewRow = list->getProxyModel()->mapFromSource(aboveRow);
    const QPoint markCenter = pdf::PDFAppToolImageListDelegate::getMarkRect(QRectF(list->getView()->visualRect(viewRow)), list->layoutDirection()).center().toPoint();
    QWidget* viewport = list->getView()->viewport();
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, markCenter);
    QCOMPARE(list->getSelectedKeys(), selected);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, markCenter);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getExcludedKeys().size() == 1; }));
    QCOMPARE(gui->getExcludedKeys(), std::vector<PDFDocumentImageList::Key>{ PDFDocumentImageList::Key::createReference(above) });
    QCOMPARE(list->getSelectedKeys(), selected);
}

void AppToolOptimizeImagesTest::copyImageNeedsCopyPermission()
{
    // ISO 32000-2 Table 22 bit 5 (copy or extract text and graphics): Copy Image of a document,
    // which forbids it, puts nothing on the clipboard (the shared check, owner 2026-11-08, as Copy
    // of a text selection). The shell refuses the tool at the opening with its window modal owner
    // password dialog; Copy Image checks by itself too (a click delivered while the dialog is open)
    using H = OptimizeImagesGuiHelper;
    using Tool = pdf::PDFDocumentToolExtractImages;

    AppToolShellFixture fixture;
    pdf::PDFObjectReference below;
    pdf::PDFObjectReference above;
    const uint32_t permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::CopyContent);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeOverlap(fixture.folder(), &below, &above, permissions));
    QVERIFY(tab);
    QVERIFY(!tab->getDocument()->getDocument()->getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent));
    PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(Tool::TOOL_ID));
    PDFAppToolGUIExtractImages* gui = H::gui<PDFAppToolGUIExtractImages>(shell);
    QVERIFY(gui);
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getImageList() && !gui->isComputing() && gui->getPlan().has_value(); }, 60000));

    PDFAppToolImageList* list = gui->getImageListWidget();
    H::select(list, { above });
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCopyButton()->isEnabled(); }));

    QVERIFY(shell->isPermissionRefused());
    QVERIFY(AppToolShellFixture::waitFor([]() { return H::findPasswordDialog() != nullptr; }));
    QVERIFY(H::findPasswordDialog()->isDocumentMode());

    QApplication::clipboard()->clear();
    QTest::mouseClick(gui->getCopyButton(), Qt::LeftButton);

    // No decoding was started: nothing comes to the clipboard later either (the decoding of this
    // 64 x 64 image takes milliseconds)
    QTest::qWait(500);
    QVERIFY(QApplication::clipboard()->image().isNull());
}

void AppToolOptimizeImagesTest::snapperTopmostImage()
{
    // B64 / R4-12: of two images over each other the snapper takes the one drawn later (it is
    // seen there); the images of a page are in the order of the drawing
    pdf::PDFPrecompiledPage page;
    QImage first(4, 4, QImage::Format_RGB32);
    first.fill(Qt::red);
    QImage second(8, 8, QImage::Format_RGB32);
    second.fill(Qt::blue);
    page.getSnapInfo()->addImage({ QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100), QPointF(50, 50) }, first);
    page.getSnapInfo()->addImage({ QPointF(50, 50), QPointF(150, 50), QPointF(150, 150), QPointF(50, 150), QPointF(100, 100) }, second);

    pdf::PDFWidgetSnapshot snapshot;
    pdf::PDFWidgetSnapshot::SnapshotItem item;
    item.pageIndex = 0;
    item.compiledPage = &page;
    snapshot.items.push_back(item);

    pdf::PDFSnapper snapper;
    snapper.buildSnapImages(snapshot);
    snapper.updateSnappedPoint(QPointF(75, 75));
    QVERIFY(snapper.getSnappedImage());
    QCOMPARE(snapper.getSnappedImage()->image.size(), QSize(8, 8));

    // Outside of the overlap: the image there
    snapper.updateSnappedPoint(QPointF(25, 25));
    QVERIFY(snapper.getSnappedImage());
    QCOMPARE(snapper.getSnappedImage()->image.size(), QSize(4, 4));
}

void AppToolOptimizeImagesTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> writes the frames under their ids (297:3135, 299:3362,
    // 301:3594, 308:6911, 341:7516, 341:8976) in both themes, at 1x / 2x and right-to-left, for the
    // comparison with Figma; no pixel of the leak color of the palette (CLAUDE.md, Fluent)
    using H = OptimizeImagesGuiHelper;
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString name = QString::fromLatin1(variant);
        const QString suffix = name == QStringLiteral("light") ? QString() : QStringLiteral("-") + name;
        pdf::PDFThemeManager::getInstance()->setTheme(name == QStringLiteral("dark") ? QStringLiteral("dark") : QStringLiteral("light"));
        QApplication::setLayoutDirection(name == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light")); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFObjectReference color;
        pdf::PDFObjectReference gray;
        pdf::PDFObjectReference bitonal;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeSample(fixture.folder(), &color, &gray, &bitonal));
        QVERIFY(tab);

        // Optimize Images 297:3135, the menu Color 299:3362, the override 301:3594
        PDFDocumentToolShell* shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolOptimizeImages::TOOL_ID));
        PDFAppToolGUIOptimizeImages* gui = H::gui<PDFAppToolGUIOptimizeImages>(shell);
        QVERIFY(gui && H::waitMeasured(gui));
        PDFAppToolImageList* list = gui->getImageListWidget();
        H::select(list, { color });
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return !gui->getComparison()->getAfterImage().isNull(); }));
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("optimize-images-297-3135") + suffix)), 0);

        QTest::mouseClick(gui->getColorButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getColorRow(QStringLiteral("bitonal")) && gui->getColorRow(QStringLiteral("bitonal"))->isVisible(); }));
        AppToolShellFixture::settle();
        fixture.screenshot(QStringLiteral("optimize-images-299-3362-color") + suffix);
        QTest::keyClick(QApplication::activePopupWidget() ? QApplication::activePopupWidget() : static_cast<QWidget*>(fixture.window), Qt::Key_Escape);
        AppToolShellFixture::settle();

        H::select(list, { color, gray });
        QTest::mouseClick(gui->getOverrideButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getOverrideKeys().size() == 2; }));
        AppToolShellFixture::settle();
        QCOMPARE(AppToolShellFixture::countLeakPixels(fixture.screenshot(QStringLiteral("optimize-images-301-3594") + suffix)), 0);

        // Grayscale 308:6911
        {
            AppToolShellFixture grayFixture(QSize(1440, 960));
            pdf::PDFAppDocumentTab* grayTab = H::openReady(grayFixture, H::writeSample(grayFixture.folder(), &color, &gray, &bitonal));
            QVERIFY(grayTab);
            PDFAppToolGUIOptimizeImages* grayGui = H::gui<PDFAppToolGUIOptimizeImages>(grayFixture.openTool(grayTab, QString::fromLatin1(pdf::PDFDocumentToolGrayscale::TOOL_ID)));
            QVERIFY(grayGui && grayGui->isGrayscale() && H::waitMeasured(grayGui));
            H::select(grayGui->getImageListWidget(), { color });
            QVERIFY(AppToolShellFixture::waitFor([grayGui]() { return !grayGui->getComparison()->getAfterImage().isNull(); }));
            AppToolShellFixture::settle();
            QCOMPARE(AppToolShellFixture::countLeakPixels(grayFixture.screenshot(QStringLiteral("optimize-grayscale-308-6911") + suffix)), 0);
        }

        // Extract Images 341:7516
        {
            AppToolShellFixture extractFixture(QSize(1440, 960));
            pdf::PDFAppDocumentTab* extractTab = H::openReady(extractFixture, H::writeSample(extractFixture.folder(), &color, &gray, &bitonal));
            QVERIFY(extractTab);
            PDFAppToolGUIExtractImages* extract = H::gui<PDFAppToolGUIExtractImages>(extractFixture.openTool(extractTab, QString::fromLatin1(pdf::PDFDocumentToolExtractImages::TOOL_ID)));
            QVERIFY(extract);
            QVERIFY(AppToolShellFixture::waitFor([extract]() { return extract->getImageList() && !extract->isComputing() && extract->getPlan().has_value(); }, 60000));
            H::select(extract->getImageListWidget(), { color });
            AppToolShellFixture::settle();
            QCOMPARE(AppToolShellFixture::countLeakPixels(extractFixture.screenshot(QStringLiteral("convert-extract-images-341-7516") + suffix)), 0);
        }
    }
}

QTEST_MAIN(AppToolOptimizeImagesTest)

#include "tst_apptooloptimizeimagestest.moc"
