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

#include "pdfocrdocumentdialog.h"
#include "pdfocrengine.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfocrpagepreparer.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttextflow.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcompiler.h"
#include "pdfstreamfilters.h"
#include "pdfconstants.h"
#include "pdfprogress.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"

#ifdef PDF4QT_OCR_TESSERACT
#include "pdftesseractocrengine.h"
#endif

#include <QtTest>
#include <QTimer>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace pdf;

namespace
{

/// Answers the modal message boxes of the dialog. The preferred buttons are
/// searched by their text; everything, what was displayed, is recorded.
class ModalResponder : public QObject
{
public:
    explicit ModalResponder(QStringList preferredButtons) :
        m_preferredButtons(std::move(preferredButtons))
    {
        m_timer.setInterval(25);
        connect(&m_timer, &QTimer::timeout, this, &ModalResponder::onTimeout);
        m_timer.start();
    }

    QStringList messages;

private:
    void onTimeout()
    {
        QWidget* widget = QApplication::activeModalWidget();
        if (!widget)
        {
            return;
        }

        if (QMessageBox* messageBox = qobject_cast<QMessageBox*>(widget))
        {
            messages << messageBox->text() + QChar('\n') + messageBox->informativeText();

            for (const QString& preferred : m_preferredButtons)
            {
                for (QAbstractButton* button : messageBox->buttons())
                {
                    if (button->text().remove(QChar('&')) == preferred)
                    {
                        button->click();
                        return;
                    }
                }
            }

            if (QAbstractButton* button = messageBox->defaultButton())
            {
                button->click();
            }
            else if (!messageBox->buttons().isEmpty())
            {
                messageBox->buttons().front()->click();
            }
            return;
        }

        if (QDialog* dialog = qobject_cast<QDialog*>(widget))
        {
            messages << QStringLiteral("dialog: ") + dialog->windowTitle();
            dialog->reject();
        }
    }

    QTimer m_timer;
    QStringList m_preferredButtons;
};

struct WidgetFixture
{
    PDFDocument document;
    PDFCMSManager cms{nullptr};
    PDFProgress progress{nullptr};
    PDFWidget widget{&cms, RendererEngine::QPainter, nullptr};

    explicit WidgetFixture(PDFDocument doc) :
        document(std::move(doc))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        PDFModifiedDocument modified(&document, nullptr);
        widget.setDocument(modified, {});
    }

    ~WidgetFixture()
    {
        widget.setDocument(PDFModifiedDocument(), {});
    }
};

} // anonymous namespace

class OCRDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void workflowWithTestEngine();
    void stoppedRerunKeepsResults();
    void workflowWithTesseract();

private:
    static PDFDocument createScanDocument(const QStringList& pageTexts);
    static QImage renderTextImage(const QString& text, QSizeF pageSize);
    static QString extractText(const PDFDocument& document, PDFInteger pageIndex);
    static QString getLayoutText(PDFDrawWidgetProxy* proxy, PDFInteger pageIndex);
    void runWorkflow(const QString& engineId, const QString& expectedWord);

    std::shared_ptr<PDFOCRTestEngineFactory> m_testEngine;
    QTemporaryDir m_settingsDirectory;
};

void OCRDialogTest::initTestCase()
{
    // Neither the settings nor the application data of the user are touched by the test
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("PDF4QT-UnitTests"));
    QCoreApplication::setApplicationName(QStringLiteral("UnitTestsOCRDialog"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());

    m_testEngine = std::make_shared<PDFOCRTestEngineFactory>();
    PDFOCREngineRegistry::getInstance()->registerFactory(m_testEngine);

#ifdef PDF4QT_OCR_TESSERACT
    PDFTesseractOCREngineFactory::registerEngine();
#endif
}

QImage OCRDialogTest::renderTextImage(const QString& text, QSizeF pageSize)
{
    // The text is rendered by the PDF renderer with the standard font Helvetica
    PDFDocumentBuilder builder;
    const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), pageSize));

    QByteArray content = QStringLiteral("BT /F1 28 Tf 30 50 Td (%1) Tj ET").arg(text).toLatin1();
    PDFDictionary contentDictionary;
    contentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
    const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(contentDictionary), std::move(content))));

    PDFObjectFactory fontFactory;
    fontFactory.beginDictionary();
    fontFactory.beginDictionaryItem("Type");
    fontFactory << WrapName("Font");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("Subtype");
    fontFactory << WrapName("Type1");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("BaseFont");
    fontFactory << WrapName("Helvetica");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("Encoding");
    fontFactory << WrapName("WinAnsiEncoding");
    fontFactory.endDictionaryItem();
    fontFactory.endDictionary();
    const PDFObjectReference fontReference = builder.addObject(fontFactory.takeObject());

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << contentReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory.beginDictionary();
    factory.beginDictionaryItem("Font");
    factory.beginDictionary();
    factory.beginDictionaryItem("F1");
    factory << fontReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(pageReference, factory.takeObject());

    PDFDocument document = builder.build();
    PDFOptionalContentActivity activity(&document, OCUsage::Export, nullptr);
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFModifiedDocument modifiedDocument(&document, &activity);
    fontCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);
    PDFCMSGeneric cms;
    PDFMeshQualitySettings meshQualitySettings;

    PDFOCRPagePreparer preparer(&document, &fontCache, &cms, &activity, meshQualitySettings, RendererEngine::QPainter);
    PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(0, 300.0, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr);
    fontCache.setCacheShrinkEnabled(nullptr, true);
    return raster.image;
}

PDFDocument OCRDialogTest::createScanDocument(const QStringList& pageTexts)
{
    // Every page is an image of a rendered text, so the expected transcript is not hidden in the PDF
    PDFDocumentBuilder builder;
    const QSizeF pageSize(400, 120);

    for (const QString& pageText : pageTexts)
    {
        const QImage image = renderTextImage(pageText, pageSize).convertToFormat(QImage::Format_RGB888);

        QByteArray imageData;
        for (int y = 0; y < image.height(); ++y)
        {
            imageData.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 3);
        }
        QByteArray compressed = PDFFlateDecodeFilter::compress(imageData);

        PDFDictionary imageDictionary;
        imageDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
        imageDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
        imageDictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
        imageDictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
        imageDictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
        imageDictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
        imageDictionary.addEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        imageDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(compressed.size()));
        const PDFObjectReference imageReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(imageDictionary), std::move(compressed))));

        const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), pageSize));
        QByteArray content = QStringLiteral("q %1 0 0 %2 0 0 cm /Im1 Do Q").arg(pageSize.width()).arg(pageSize.height()).toLatin1();
        PDFDictionary contentDictionary;
        contentDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(content.size()));
        const PDFObjectReference contentReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(contentDictionary), std::move(content))));

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Contents");
        factory << contentReference;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Resources");
        factory.beginDictionary();
        factory.beginDictionaryItem("XObject");
        factory.beginDictionary();
        factory.beginDictionaryItem("Im1");
        factory << imageReference;
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(pageReference, factory.takeObject());
    }

    return builder.build();
}

QString OCRDialogTest::extractText(const PDFDocument& document, PDFInteger pageIndex)
{
    PDFDocumentTextFlowFactory factory;
    return factory.create(&document, { pageIndex }, PDFDocumentTextFlowFactory::Algorithm::Layout).getText();
}

QString OCRDialogTest::getLayoutText(PDFDrawWidgetProxy* proxy, PDFInteger pageIndex)
{
    QString text;
    const PDFTextLayout layout = proxy->getTextLayoutCompiler()->createTextLayout(pageIndex);
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            for (const TextCharacter& character : line.getCharacters())
            {
                text += character.character;
            }
            text += QChar(' ');
        }
    }
    return text;
}

void OCRDialogTest::runWorkflow(const QString& engineId, const QString& expectedWord)
{
    WidgetFixture fixture(createScanDocument({ QStringLiteral("Hello world"), QStringLiteral("Second page") }));

    pdfviewer::PDFOCRDocumentDialog::Context context;
    context.document = &fixture.document;
    context.proxy = fixture.widget.getDrawWidgetProxy();
    PDFCMSPointer cms = fixture.cms.getCurrentCMS();
    context.cms = cms.data();
    context.progress = &fixture.progress;
    context.visiblePages = { 0 };
    context.fileName = QStringLiteral("scan.pdf");

    ModalResponder responder({ QStringLiteral("Apply"), QStringLiteral("No"), QStringLiteral("Discard"), QStringLiteral("Continue"), QStringLiteral("OK") });

    pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
    dialog.show();

    auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
    auto* engineComboBox = dialog.findChild<QComboBox*>(QStringLiteral("engineComboBox"));
    auto* languagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("languagesListWidget"));
    auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
    auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
    auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
    auto* undoButton = dialog.findChild<QPushButton*>(QStringLiteral("undoButton"));
    auto* resultsTreeWidget = dialog.findChild<QTreeWidget*>(QStringLiteral("resultsTreeWidget"));
    auto* wordTextEdit = dialog.findChild<QLineEdit*>(QStringLiteral("wordTextEdit"));
    QVERIFY(pagesListWidget && engineComboBox && languagesListWidget && recognizeButton && stopButton && applyButton && undoButton && resultsTreeWidget && wordTextEdit);

    // All pages are listed and checked by default (PAGE-01)
    QCOMPARE(pagesListWidget->count(), 2);
    QCOMPARE(pagesListWidget->item(0)->checkState(), Qt::Checked);
    QCOMPARE(pagesListWidget->item(1)->checkState(), Qt::Checked);

    const int engineIndex = engineComboBox->findData(engineId);
    QVERIFY2(engineIndex >= 0, qPrintable(engineId));
    engineComboBox->setCurrentIndex(engineIndex);

    if (engineId == QStringLiteral("tesseract"))
    {
        // Built-in languages are offered without any download (LANG-01)
        QStringList languages;
        for (int i = 0; i < languagesListWidget->count(); ++i)
        {
            QListWidgetItem* item = languagesListWidget->item(i);
            languages << item->data(Qt::UserRole + 3).toString();
            item->setCheckState(item->data(Qt::UserRole + 3).toString() == QStringLiteral("eng") ? Qt::Checked : Qt::Unchecked);
        }
        QVERIFY2(languages.contains(QStringLiteral("ces")) && languages.contains(QStringLiteral("eng")) && languages.contains(QStringLiteral("deu")) && languages.contains(QStringLiteral("slk")), qPrintable(languages.join(QChar(','))));
        QVERIFY(!languages.contains(QStringLiteral("osd")));
    }

    // Recognition itself does not modify the document (UI-03)
    QVERIFY(recognizeButton->isEnabled());
    QVERIFY(!applyButton->isEnabled());
    recognizeButton->click();
    QVERIFY2(stopButton->isEnabled(), qPrintable(responder.messages.join(QChar('|'))));
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 180000);
    QVERIFY(!dialog.hasModifiedDocument());
    QVERIFY2(applyButton->isEnabled(), qPrintable(responder.messages.join(QChar('|'))));

    // Results of the current page are displayed; any word can be corrected (EDIT-01)
    pagesListWidget->setCurrentRow(0);
    QTreeWidgetItem* wordItem = nullptr;
    QTreeWidgetItemIterator it(resultsTreeWidget);
    while (*it)
    {
        if ((*it)->data(0, Qt::UserRole + 1).toInt() == 2)
        {
            wordItem = *it;
            break;
        }
        ++it;
    }
    QVERIFY(wordItem);
    QCOMPARE(wordItem->text(0), expectedWord);
    resultsTreeWidget->setCurrentItem(wordItem);
    QCOMPARE(wordTextEdit->text(), expectedWord);
    QVERIFY(wordTextEdit->isEnabled());

    wordTextEdit->setText(QStringLiteral("Corrected"));
    QMetaObject::invokeMethod(wordTextEdit, "editingFinished");
    QVERIFY(undoButton->isEnabled());

    // Optional screenshot of the dialog for a manual check of the layout
    const QByteArray screenshotFile = qgetenv("PDF4QT_OCR_DIALOG_SCREENSHOT");
    if (!screenshotFile.isEmpty())
    {
        QTest::qWait(1500);
        qInfo() << "Dialog size" << dialog.size() << "minimum size hint" << dialog.minimumSizeHint();
        dialog.grab().save(QString::fromLocal8Bit(screenshotFile) + QStringLiteral("_") + engineId + QStringLiteral(".png"));
    }

    // Local undo and redo of the correction (EDIT-06)
    undoButton->click();
    QCOMPARE(wordTextEdit->text(), expectedWord);
    auto* redoButton = dialog.findChild<QPushButton*>(QStringLiteral("redoButton"));
    redoButton->click();
    QCOMPARE(wordTextEdit->text(), QStringLiteral("Corrected"));

    // Application is a separate step with a summary; the dialog is accepted after the commit is prepared
    responder.messages.clear();
    applyButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.result() == QDialog::Accepted && !dialog.isVisible(), 60000);
    QVERIFY2(dialog.hasModifiedDocument(), qPrintable(responder.messages.join(QChar('|'))));
    QVERIFY(responder.messages.join(QChar('|')).contains(QStringLiteral("Pages: 1-2")));

    PDFDocumentPointer modified = dialog.takeModifiedDocument();
    QVERIFY(modified);
    QCOMPARE(modified->getCatalog()->getPageCount(), size_t(2));
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(modified.data(), 0).isPresent);
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(modified.data(), 1).isPresent);

    const QString text = extractText(*modified, 0);
    QVERIFY2(text.contains(QStringLiteral("Corrected")), qPrintable(text));
    QVERIFY2(!text.contains(expectedWord), qPrintable(text));

    // AT-16: the modification with the page contents flag invalidates the text layouts of the
    // editor widget, so the search corresponds to every version of the document
    PDFDrawWidgetProxy* proxy = fixture.widget.getDrawWidgetProxy();
    QVERIFY(!getLayoutText(proxy, 0).contains(QStringLiteral("Corrected")));

    PDFModifiedDocument modifiedDocument(modified, nullptr, PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::PageContents));
    fixture.widget.setDocument(modifiedDocument, {});
    QVERIFY2(getLayoutText(proxy, 0).contains(QStringLiteral("Corrected")), qPrintable(getLayoutText(proxy, 0)));

    // Undo of the document step returns the original document
    PDFModifiedDocument originalDocument(&fixture.document, nullptr, PDFModifiedDocument::ModificationFlags(PDFModifiedDocument::PageContents));
    fixture.widget.setDocument(originalDocument, {});
    QVERIFY(!getLayoutText(proxy, 0).contains(QStringLiteral("Corrected")));

    // The dialog opened on the modified document offers the own layer for further corrections
    // without a new recognition (PDF-10)
    std::optional<PDFOCRPageResult> layer = PDFOCRTextLayerWriter::readLayer(modified.data(), 0);
    QVERIFY(layer.has_value());
    QCOMPARE(layer->getWords().front()->text, QStringLiteral("Corrected"));

    {
        WidgetFixture reopenedFixture{ PDFDocument(*modified) };

        pdfviewer::PDFOCRDocumentDialog::Context reopenedContext = context;
        reopenedContext.document = &reopenedFixture.document;
        reopenedContext.proxy = reopenedFixture.widget.getDrawWidgetProxy();
        reopenedContext.progress = &reopenedFixture.progress;

        pdfviewer::PDFOCRDocumentDialog reopenedDialog(reopenedContext, nullptr);
        reopenedDialog.show();

        auto* reopenedPages = reopenedDialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
        auto* reopenedTree = reopenedDialog.findChild<QTreeWidget*>(QStringLiteral("resultsTreeWidget"));
        auto* reopenedWordEdit = reopenedDialog.findChild<QLineEdit*>(QStringLiteral("wordTextEdit"));
        auto* reopenedApply = reopenedDialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        QVERIFY(reopenedPages && reopenedTree && reopenedWordEdit && reopenedApply);

        auto findWordItem = [reopenedTree](const QString& text) -> QTreeWidgetItem*
        {
            QTreeWidgetItemIterator it(reopenedTree);
            while (*it)
            {
                if ((*it)->data(0, Qt::UserRole + 1).toInt() == 2 && (*it)->text(0) == text)
                {
                    return *it;
                }
                ++it;
            }
            return nullptr;
        };

        // The layer is loaded by the background analysis, no recognition is started
        reopenedPages->setCurrentRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(findWordItem(QStringLiteral("Corrected")) != nullptr, 60000);
        QTRY_VERIFY_WITH_TIMEOUT(reopenedApply->isEnabled(), 60000);

        // The text can be corrected further
        reopenedTree->setCurrentItem(findWordItem(QStringLiteral("Corrected")));
        QVERIFY(reopenedWordEdit->isEnabled());
        reopenedWordEdit->setText(QStringLiteral("Twice"));
        QMetaObject::invokeMethod(reopenedWordEdit, "editingFinished");

        responder.messages.clear();
        reopenedApply->click();
        QTRY_VERIFY_WITH_TIMEOUT(reopenedDialog.result() == QDialog::Accepted && !reopenedDialog.isVisible(), 60000);
        QVERIFY2(reopenedDialog.hasModifiedDocument(), qPrintable(responder.messages.join(QChar('|'))));
        PDFDocumentPointer twice = reopenedDialog.takeModifiedDocument();
        QVERIFY(twice);
        const QString twiceText = extractText(*twice, 0);
        QVERIFY2(twiceText.contains(QStringLiteral("Twice")) && !twiceText.contains(QStringLiteral("Corrected")), qPrintable(twiceText));
        QCOMPARE(PDFOCRTextLayerWriter::readLayerInfo(twice.data(), 0).layerId, PDFOCRTextLayerWriter::readLayerInfo(modified.data(), 0).layerId);
    }
}

void OCRDialogTest::workflowWithTestEngine()
{
    m_testEngine->setRecognitionDelay(50);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;

        const double scale = input.dpi / 72.0;
        PDFOCRRawBlock block;
        PDFOCRRawLine line;
        PDFOCRRawWord first;
        first.text = QStringLiteral("Deterministic");
        first.rect = QRectF(30 * scale, 40 * scale, 150 * scale, 30 * scale);
        first.rawConfidence = 55.0;
        PDFOCRRawWord second;
        second.text = QStringLiteral("result");
        second.rect = QRectF(190 * scale, 40 * scale, 80 * scale, 30 * scale);
        second.rawConfidence = 97.0;
        line.rect = first.rect.united(second.rect);
        line.words = { first, second };
        block.rect = line.rect;
        block.lines.push_back(line);
        output.blocks.push_back(block);
        return output;
    });

    runWorkflow(QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER), QStringLiteral("Deterministic"));
}

void OCRDialogTest::stoppedRerunKeepsResults()
{
    m_testEngine->setRecognitionDelay(20);
    m_testEngine->setHandler([](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;

        const double scale = input.dpi / 72.0;
        PDFOCRRawBlock block;
        PDFOCRRawLine line;
        PDFOCRRawWord word;
        word.text = QStringLiteral("Stable");
        word.rect = QRectF(30 * scale, 40 * scale, 150 * scale, 30 * scale);
        word.rawConfidence = 90.0;
        line.rect = word.rect;
        line.words = { word };
        block.rect = line.rect;
        block.lines.push_back(line);
        output.blocks.push_back(block);
        return output;
    });

    WidgetFixture fixture(createScanDocument({ QStringLiteral("First page"), QStringLiteral("Second page"), QStringLiteral("Third page") }));

    pdfviewer::PDFOCRDocumentDialog::Context context;
    context.document = &fixture.document;
    context.proxy = fixture.widget.getDrawWidgetProxy();
    PDFCMSPointer cms = fixture.cms.getCurrentCMS();
    context.cms = cms.data();
    context.progress = &fixture.progress;
    context.visiblePages = { 0 };
    context.fileName = QStringLiteral("scan.pdf");

    ModalResponder responder({ QStringLiteral("Discard"), QStringLiteral("No"), QStringLiteral("OK") });

    pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
    dialog.show();

    auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
    auto* engineComboBox = dialog.findChild<QComboBox*>(QStringLiteral("engineComboBox"));
    auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
    auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
    auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
    auto* resultsTreeWidget = dialog.findChild<QTreeWidget*>(QStringLiteral("resultsTreeWidget"));
    auto* findEdit = dialog.findChild<QLineEdit*>(QStringLiteral("findEdit"));
    QVERIFY(pagesListWidget && engineComboBox && recognizeButton && stopButton && applyButton && resultsTreeWidget && findEdit);

    engineComboBox->setCurrentIndex(engineComboBox->findData(QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER)));

    // No button of the dialog is the default one: Enter in an edit box does not click "All"
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
    {
        QVERIFY2(!button->isDefault() && !button->autoDefault(), qPrintable(button->objectName()));
    }
    pagesListWidget->item(2)->setCheckState(Qt::Unchecked);
    findEdit->setText(QStringLiteral("nothing"));
    QTest::keyClick(findEdit, Qt::Key_Return);
    QCOMPARE(pagesListWidget->item(2)->checkState(), Qt::Unchecked);
    pagesListWidget->item(2)->setCheckState(Qt::Checked);

    auto countWords = [&]()
    {
        int count = 0;
        for (int row = 0; row < pagesListWidget->count(); ++row)
        {
            pagesListWidget->setCurrentRow(row);
            QTreeWidgetItemIterator it(resultsTreeWidget);
            while (*it)
            {
                if ((*it)->data(0, Qt::UserRole + 1).toInt() == 2 && (*it)->text(0) == QStringLiteral("Stable"))
                {
                    ++count;
                }
                ++it;
            }
        }
        return count;
    };

    // First recognition of all pages
    recognizeButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
    QVERIFY(applyButton->isEnabled());
    QCOMPARE(countWords(), 3);

    // Repeated recognition is stopped before it finishes: no result may be lost (JOB-05)
    m_testEngine->setRecognitionDelay(3000);
    recognizeButton->click();
    QVERIFY2(stopButton->isEnabled(), qPrintable(responder.messages.join(QChar('|'))));
    QTest::qWait(200);
    stopButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
    m_testEngine->setRecognitionDelay(0);

    QCOMPARE(countWords(), 3);
    QVERIFY(applyButton->isEnabled());

    dialog.reject();
}

void OCRDialogTest::workflowWithTesseract()
{
#ifndef PDF4QT_OCR_TESSERACT
    QSKIP("Tesseract engine is not compiled in.");
#else
    if (!QFile::exists(PDFOCRModelManager::getDefaultBuiltInDirectory() + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        QSKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    runWorkflow(QStringLiteral("tesseract"), QStringLiteral("Hello"));
#endif
}

QTEST_MAIN(OCRDialogTest)

#include "tst_ocrdialogtest.moc"
