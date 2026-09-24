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
#include "pdfocrproject.h"
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
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace pdf;

// A release build (PDF4QT_OCR_REQUIRED) must test the real engine and the built-in
// models: a missing prerequisite is a failure there, not a skip, so a green ctest
// is a release gate of the OCR package
#ifdef PDF4QT_OCR_TESTS_REQUIRED
#define PDF4QT_OCR_SKIP(message) QFAIL(message)
#else
#define PDF4QT_OCR_SKIP(message) QSKIP(message)
#endif

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

    /// File name selected in the file dialogs (empty = the file dialogs are rejected)
    QString fileName;

    /// Item selected in the input dialogs with a list of items (empty = rejected)
    QString inputItem;

    void setPreferredButtons(QStringList preferredButtons) { m_preferredButtons = std::move(preferredButtons); }

private:
    void onTimeout()
    {
        if (QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
        {
            // Context menu: the first preferred action is triggered, otherwise the menu is closed
            for (const QString& preferred : m_preferredButtons)
            {
                for (QAction* action : menu->actions())
                {
                    if (action->text().remove(QChar('&')) == preferred && action->isEnabled())
                    {
                        messages << QStringLiteral("menu: ") + preferred;
                        menu->setActiveAction(action);
                        QTest::keyClick(menu, Qt::Key_Return);
                        return;
                    }
                }
            }

            QStringList actions;
            for (QAction* action : menu->actions())
            {
                actions << action->text() + (action->isEnabled() ? QString() : QStringLiteral(" (disabled)"));
            }
            messages << QStringLiteral("menu closed: ") + actions.join(QStringLiteral(", "));
            menu->close();
            return;
        }

        QWidget* widget = QApplication::activeModalWidget();
        if (!widget)
        {
            return;
        }

        if (QFileDialog* fileDialog = qobject_cast<QFileDialog*>(widget))
        {
            messages << QStringLiteral("file dialog: ") + fileDialog->windowTitle();
            if (fileName.isEmpty())
            {
                fileDialog->reject();
            }
            else
            {
                // The name is typed, the model of the dialog loads the directories asynchronously
                fileDialog->setDirectory(QFileInfo(fileName).absolutePath());
                if (QLineEdit* fileNameEdit = fileDialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")))
                {
                    fileNameEdit->setText(QFileInfo(fileName).fileName());
                }
                else
                {
                    fileDialog->selectFile(fileName);
                }
                static_cast<QDialog*>(fileDialog)->accept();
            }
            return;
        }

        if (QInputDialog* inputDialog = qobject_cast<QInputDialog*>(widget))
        {
            messages << QStringLiteral("input dialog: ") + inputDialog->labelText();
            if (!inputItem.isEmpty() && inputDialog->comboBoxItems().contains(inputItem))
            {
                inputDialog->setTextValue(inputItem);
                inputDialog->accept();
            }
            else
            {
                inputDialog->reject();
            }
            return;
        }

        if (QMessageBox* messageBox = qobject_cast<QMessageBox*>(widget))
        {
            messages << messageBox->text() + QChar('\n') + messageBox->informativeText() + QChar('\n') + messageBox->detailedText();

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

    PDFCMSPointer cmsPointer;

    explicit WidgetFixture(PDFDocument doc) :
        document(std::move(doc))
    {
        widget.resize(800, 800);
        widget.getDrawWidget()->getWidget()->resize(800, 800);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        PDFModifiedDocument modified(&document, nullptr);
        widget.setDocument(modified, {});
        cmsPointer = cms.getCurrentCMS();
    }

    pdfviewer::PDFOCRDocumentDialog::Context createContext()
    {
        pdfviewer::PDFOCRDocumentDialog::Context context;
        context.document = &document;
        context.proxy = widget.getDrawWidgetProxy();
        context.cms = cmsPointer.data();
        context.progress = &progress;
        context.visiblePages = { 0 };
        context.fileName = QStringLiteral("scan.pdf");
        return context;
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
    void reviewOnlySurvivesProject();
    void existingTextMasked();
    void certifiedDocument();
    void limitedResolution();
    void rerecognizeRegion();
    void mergeAndSplitLines();

private:
    static PDFDocument createScanDocument(const QStringList& pageTexts, const QByteArray& digitalText = QByteArray());
    static PDFDocument certifyDocument(const PDFDocument& document, int permissions);
    static QTreeWidgetItem* findTreeItem(QTreeWidget* tree, int type, const QString& text);
    static void selectComboData(pdfviewer::PDFOCRDocumentDialog& dialog, const char* name, const QVariant& data);
    void setLinesHandler(const std::vector<QStringList>& lines);
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

PDFDocument OCRDialogTest::createScanDocument(const QStringList& pageTexts, const QByteArray& digitalText)
{
    // Every page is an image of a rendered text, so the expected transcript is not hidden in the PDF.
    // An optional short digital text (a page number) is drawn over the image with Helvetica.
    PDFDocumentBuilder builder;
    const QSizeF pageSize(400, 120);

    PDFObjectReference fontReference;
    if (!digitalText.isEmpty())
    {
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
        fontReference = builder.addObject(fontFactory.takeObject());
    }

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
        if (!digitalText.isEmpty())
        {
            content += " BT /F1 10 Tf 280 10 Td (" + digitalText + ") Tj ET";
        }
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
        if (fontReference.isValid())
        {
            factory.beginDictionaryItem("Font");
            factory.beginDictionary();
            factory.beginDictionaryItem("F1");
            factory << fontReference;
            factory.endDictionaryItem();
            factory.endDictionary();
            factory.endDictionaryItem();
        }
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
    PDF4QT_OCR_SKIP("Tesseract engine is not compiled in.");
#else
    if (!QFile::exists(PDFOCRModelManager::getDefaultBuiltInDirectory() + QStringLiteral("/tesseract/fast/tessdata/eng.traineddata")))
    {
        PDF4QT_OCR_SKIP("Built-in OCR language models are not available (ocr/tesseract/fast/tessdata).");
    }

    runWorkflow(QStringLiteral("tesseract"), QStringLiteral("Hello"));
#endif
}

QTreeWidgetItem* OCRDialogTest::findTreeItem(QTreeWidget* tree, int type, const QString& text)
{
    QTreeWidgetItemIterator it(tree);
    while (*it)
    {
        if ((*it)->data(0, Qt::UserRole + 1).toInt() == type && (text.isEmpty() || (*it)->text(0) == text))
        {
            return *it;
        }
        ++it;
    }
    return nullptr;
}

void OCRDialogTest::selectComboData(pdfviewer::PDFOCRDocumentDialog& dialog, const char* name, const QVariant& data)
{
    QComboBox* comboBox = dialog.findChild<QComboBox*>(QLatin1String(name));
    QVERIFY(comboBox);
    const int index = comboBox->findData(data);
    QVERIFY2(index >= 0, name);
    comboBox->setCurrentIndex(index);
}

void OCRDialogTest::setLinesHandler(const std::vector<QStringList>& lines)
{
    // Every line of words is placed into the image given to the engine (the whole page,
    // or the sub-image of a region), so the words are always inside the recognized area
    m_testEngine->setRecognitionDelay(0);
    m_testEngine->setHandler([lines](const PDFOCRRecognitionInput& input, const PDFOperationControl*)
    {
        PDFOCRRecognitionOutput output;
        output.imageSize = input.image.size();
        output.confidenceLevel = PDFOCRConfidenceLevel::Word;

        const double width = input.image.width();
        const double height = input.image.height();
        const double lineHeight = height * 0.7 / qMax<size_t>(1, lines.size());

        PDFOCRRawBlock block;
        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            PDFOCRRawLine line;
            const double y = height * 0.1 + lineHeight * lineIndex;
            double x = width * 0.08;
            for (const QString& text : lines[lineIndex])
            {
                PDFOCRRawWord word;
                word.text = text;
                word.rect = QRectF(x, y, width * 0.18, lineHeight * 0.8);
                word.rawConfidence = 95.0;
                line.words.push_back(word);
                line.rect = line.rect.isNull() ? word.rect : line.rect.united(word.rect);
                x += width * 0.21;
            }
            block.lines.push_back(line);
            block.rect = block.rect.isNull() ? line.rect : block.rect.united(line.rect);
        }
        output.blocks.push_back(block);
        return output;
    });
}

void OCRDialogTest::reviewOnlySurvivesProject()
{
    // R03: the result recognized for the review and the export only must not become
    // writable after the project is saved and opened again over the same document
    setLinesHandler({ { QStringLiteral("Review"), QStringLiteral("only") } });

    WidgetFixture fixture(createScanDocument({ QStringLiteral("Review only") }));
    const pdfviewer::PDFOCRDocumentDialog::Context context = fixture.createContext();
    const QString projectFile = QDir(m_settingsDirectory.path()).filePath(QStringLiteral("review.") + QLatin1String(PDFOCRProject::FILE_EXTENSION));
    QFile::remove(projectFile);

    ModalResponder responder({ QStringLiteral("Continue"), QStringLiteral("Discard"), QStringLiteral("OK") });

    {
        pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
        dialog.show();

        auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
        auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
        auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
        auto* saveProjectButton = dialog.findChild<QPushButton*>(QStringLiteral("saveProjectButton"));
        QVERIFY(pagesListWidget && recognizeButton && stopButton && saveProjectButton);

        selectComboData(dialog, "engineComboBox", QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
        selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::ReviewOnly));

        recognizeButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
        QVERIFY2(pagesListWidget->item(0)->text().contains(QStringLiteral("Review/export only")), qPrintable(pagesListWidget->item(0)->text()));

        responder.fileName = projectFile;
        saveProjectButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(projectFile), 10000);

        // The next dialogs start with the default policy
        selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
        dialog.reject();
    }

    {
        pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
        dialog.show();

        auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
        auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        auto* openProjectButton = dialog.findChild<QPushButton*>(QStringLiteral("openProjectButton"));
        QVERIFY(pagesListWidget && applyButton && openProjectButton);

        responder.fileName = projectFile;
        openProjectButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(applyButton->isEnabled(), 10000);
        QVERIFY2(pagesListWidget->item(0)->text().contains(QStringLiteral("Review/export only")), qPrintable(pagesListWidget->item(0)->text()));

        // Even with a writing policy, the review-only result is excluded from the PDF
        selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
        responder.messages.clear();
        applyButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("recognized for review/export only")), 10000);
        QVERIFY(!dialog.hasModifiedDocument());
        QVERIFY(dialog.isVisible());

        responder.fileName.clear();
        dialog.reject();
    }
}

void OCRDialogTest::existingTextMasked()
{
    // R04: a scan with a short digital text (page number) is not accepted automatically as
    // a page without text. It can be recognized with the existing text masked (the text
    // layer is written), or for the review only (the result is never written).
    setLinesHandler({ { QStringLiteral("Scanned"), QStringLiteral("text") } });

    WidgetFixture fixture(createScanDocument({ QStringLiteral("Scanned text") }, QByteArrayLiteral("12")));
    const pdfviewer::PDFOCRDocumentDialog::Context context = fixture.createContext();

    ModalResponder responder({ QStringLiteral("Recognize with Existing Text Masked"), QStringLiteral("Apply"), QStringLiteral("No"), QStringLiteral("Discard"), QStringLiteral("OK") });

    {
        pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
        dialog.show();

        auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
        auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
        auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
        auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        QVERIFY(pagesListWidget && recognizeButton && stopButton && applyButton);

        selectComboData(dialog, "engineComboBox", QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
        selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));

        recognizeButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
        const QString messages = responder.messages.join(QChar('|'));
        QVERIFY2(messages.contains(QStringLiteral("Pages requiring your decision: 1")), qPrintable(messages));
        QVERIFY2(pagesListWidget->item(0)->toolTip().contains(QStringLiteral("Existing text masked")), qPrintable(pagesListWidget->item(0)->toolTip()));
        QVERIFY(!pagesListWidget->item(0)->text().contains(QStringLiteral("Review/export only")));
        QVERIFY(applyButton->isEnabled());

        responder.messages.clear();
        applyButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.result() == QDialog::Accepted && !dialog.isVisible(), 60000);
        QVERIFY2(dialog.hasModifiedDocument(), qPrintable(responder.messages.join(QChar('|'))));

        PDFDocumentPointer modified = dialog.takeModifiedDocument();
        const QString text = extractText(*modified, 0);
        QVERIFY2(text.contains(QStringLiteral("Scanned")) && text.contains(QStringLiteral("12")), qPrintable(text));
    }

    responder.setPreferredButtons({ QStringLiteral("Recognize Them for Review Only"), QStringLiteral("No"), QStringLiteral("Discard"), QStringLiteral("OK") });

    {
        pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
        dialog.show();

        auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
        auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
        auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
        auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        QVERIFY(pagesListWidget && recognizeButton && stopButton && applyButton);

        recognizeButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
        QVERIFY2(pagesListWidget->item(0)->text().contains(QStringLiteral("Review/export only")), qPrintable(pagesListWidget->item(0)->text()));
        QVERIFY(!pagesListWidget->item(0)->toolTip().contains(QStringLiteral("Existing text masked")));

        responder.messages.clear();
        applyButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("recognized for review/export only")), 10000);
        QVERIFY(!dialog.hasModifiedDocument());
        dialog.reject();
    }
}

PDFDocument OCRDialogTest::certifyDocument(const PDFDocument& document, int permissions)
{
    // Fake certification: /Perms /DocMDP << /Reference [ << /TransformParams << /P n >> >> ] >>
    // (permissions 0 = the transform parameters without /P)
    PDFDocumentBuilder builder(&document);

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Perms");
    factory.beginDictionary();
    factory.beginDictionaryItem("DocMDP");
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Sig");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Reference");
    factory.beginArray();
    factory.beginDictionary();
    factory.beginDictionaryItem("TransformMethod");
    factory << WrapName("DocMDP");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("TransformParams");
    factory.beginDictionary();
    if (permissions > 0)
    {
        factory.beginDictionaryItem("P");
        factory << PDFInteger(permissions);
        factory.endDictionaryItem();
    }
    factory.beginDictionaryItem("V");
    factory << WrapName("1.2");
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endArray();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(builder.getCatalogReference(), factory.takeObject());

    return builder.build();
}

void OCRDialogTest::certifiedDocument()
{
    // PDF-12: the certification (DocMDP) is parsed from the catalog and enforced
    setLinesHandler({ { QStringLiteral("Certified"), QStringLiteral("scan") } });

    const PDFDocument scan = createScanDocument({ QStringLiteral("Certified scan") });
    QCOMPARE(pdfviewer::PDFOCRDocumentDialog::getCertificationPermissions(&scan), 0);
    const PDFDocument noChanges = certifyDocument(scan, 1);
    QCOMPARE(pdfviewer::PDFOCRDocumentDialog::getCertificationPermissions(&noChanges), 1);
    const PDFDocument annotations = certifyDocument(scan, 3);
    QCOMPARE(pdfviewer::PDFOCRDocumentDialog::getCertificationPermissions(&annotations), 3);
    const PDFDocument withoutPermissions = certifyDocument(scan, 0);
    QCOMPARE(pdfviewer::PDFOCRDocumentDialog::getCertificationPermissions(&withoutPermissions), 2);

    WidgetFixture fixture{ PDFDocument(noChanges) };
    pdfviewer::PDFOCRDocumentDialog::Context context = fixture.createContext();
    context.certificationPermissions = pdfviewer::PDFOCRDocumentDialog::getCertificationPermissions(&fixture.document);

    ModalResponder responder({ QStringLiteral("Apply"), QStringLiteral("Discard"), QStringLiteral("OK") });

    auto recognize = [](pdfviewer::PDFOCRDocumentDialog& dialog)
    {
        auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
        auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
        auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        QVERIFY(recognizeButton && stopButton && applyButton);
        selectComboData(dialog, "engineComboBox", QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
        selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
        recognizeButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
        QVERIFY(applyButton->isEnabled());
    };

    // Permission 1: no output mode writes the text layer
    {
        pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
        dialog.show();
        recognize(dialog);

        auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        auto* removeLayerButton = dialog.findChild<QPushButton*>(QStringLiteral("removeLayerButton"));
        QVERIFY(!removeLayerButton->isEnabled());

        for (int outputMode : { 0, 1 })
        {
            dialog.findChild<QComboBox*>(QStringLiteral("outputModeComboBox"))->setCurrentIndex(outputMode);
            responder.messages.clear();
            applyButton->click();
            QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("does not allow any change")), 10000);
            QVERIFY(!dialog.hasModifiedDocument());
        }
        dialog.findChild<QComboBox*>(QStringLiteral("outputModeComboBox"))->setCurrentIndex(0);
        dialog.reject();
    }

    // Permission 2: the current document is refused, the copy is written with a warning
    context.certificationPermissions = 2;
    {
        pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
        dialog.show();
        recognize(dialog);

        auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
        auto* outputModeComboBox = dialog.findChild<QComboBox*>(QStringLiteral("outputModeComboBox"));

        outputModeComboBox->setCurrentIndex(0);
        responder.messages.clear();
        applyButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("the current document cannot be modified")), 10000);
        QVERIFY(!dialog.hasModifiedDocument());

        const QString copyFile = QDir(m_settingsDirectory.path()).filePath(QStringLiteral("certified_copy.pdf"));
        QFile::remove(copyFile);
        responder.fileName = copyFile;
        outputModeComboBox->setCurrentIndex(1);
        responder.messages.clear();
        applyButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("was saved to")), 30000);
        QVERIFY2(responder.messages.join(QChar('|')).contains(QStringLiteral("certification of the copy is not valid")), qPrintable(responder.messages.join(QChar('|'))));
        QVERIFY(!dialog.hasModifiedDocument());

        responder.fileName.clear();
        outputModeComboBox->setCurrentIndex(0);
        dialog.reject();
    }
}

void OCRDialogTest::limitedResolution()
{
    // IMAGE-01, ARCH-02: the resolution limited by the image size of the engine is used only
    // with an explicit consent of the user
    setLinesHandler({ { QStringLiteral("Large") } });
    m_testEngine->setMaximumImageSize(QSize(1000, 1000));
    auto restoreLimit = qScopeGuard([this]() { m_testEngine->setMaximumImageSize(QSize()); });

    WidgetFixture fixture(createScanDocument({ QStringLiteral("Large page") }));
    const pdfviewer::PDFOCRDocumentDialog::Context context = fixture.createContext();

    ModalResponder responder({ QStringLiteral("Cancel"), QStringLiteral("Discard"), QStringLiteral("OK") });

    pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
    dialog.show();

    auto* pagesListWidget = dialog.findChild<QListWidget*>(QStringLiteral("pagesListWidget"));
    auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
    auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
    auto* applyButton = dialog.findChild<QPushButton*>(QStringLiteral("applyButton"));
    QVERIFY(pagesListWidget && recognizeButton && stopButton && applyButton);

    selectComboData(dialog, "engineComboBox", QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
    selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));
    selectComboData(dialog, "dpiComboBox", 300);

    // Cancelled: nothing is recognized
    recognizeButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("too large for the requested resolution")), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
    QVERIFY(!applyButton->isEnabled());

    // Confirmed: the page is recognized at the reduced resolution
    responder.setPreferredButtons({ QStringLiteral("Continue with Reduced Resolution"), QStringLiteral("Discard"), QStringLiteral("OK") });
    responder.messages.clear();
    recognizeButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled() && applyButton->isEnabled(), 60000);
    QVERIFY2(responder.messages.join(QChar('|')).contains(QStringLiteral("Page 1: ")), qPrintable(responder.messages.join(QChar('|'))));
    QVERIFY2(pagesListWidget->item(0)->toolTip().contains(QStringLiteral("(requested 300 DPI)")), qPrintable(pagesListWidget->item(0)->toolTip()));
    QVERIFY(!pagesListWidget->item(0)->toolTip().contains(QStringLiteral("at 300 DPI")));

    dialog.reject();
}

void OCRDialogTest::rerecognizeRegion()
{
    // REGION-03: a single region is recognized again and replaces only the blocks of the region
    setLinesHandler({ { QStringLiteral("First"), QStringLiteral("region") } });

    WidgetFixture fixture(createScanDocument({ QStringLiteral("Region text") }));
    const pdfviewer::PDFOCRDocumentDialog::Context context = fixture.createContext();

    ModalResponder responder({ QStringLiteral("Re-recognize Region"), QStringLiteral("Yes"), QStringLiteral("Discard"), QStringLiteral("OK") });

    pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
    dialog.show();

    auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
    auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
    auto* regionsFromBlocksButton = dialog.findChild<QToolButton*>(QStringLiteral("regionsFromBlocksButton"));
    auto* resultsTreeWidget = dialog.findChild<QTreeWidget*>(QStringLiteral("resultsTreeWidget"));
    QVERIFY(recognizeButton && stopButton && regionsFromBlocksButton && resultsTreeWidget);

    selectComboData(dialog, "engineComboBox", QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
    selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));

    recognizeButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);
    QVERIFY(findTreeItem(resultsTreeWidget, 2, QStringLiteral("First")));

    // The detected block becomes a region
    QVERIFY(regionsFromBlocksButton->isEnabled());
    regionsFromBlocksButton->click();
    QTreeWidgetItem* blockItem = findTreeItem(resultsTreeWidget, 0, QString());
    QVERIFY(blockItem);
    QVERIFY2(blockItem->text(0).contains(QStringLiteral("region")), qPrintable(blockItem->text(0)));
    resultsTreeWidget->setCurrentItem(blockItem);

    setLinesHandler({ { QStringLiteral("Again"), QStringLiteral("recognized") } });
    responder.messages.clear();
    Q_EMIT resultsTreeWidget->customContextMenuRequested(QPoint(5, 5));
    QTRY_VERIFY2_WITH_TIMEOUT(findTreeItem(resultsTreeWidget, 2, QStringLiteral("Again")) != nullptr, qPrintable(responder.messages.join(QChar('|'))), 60000);
    QVERIFY(responder.messages.join(QChar('|')).contains(QStringLiteral("menu: Re-recognize Region")));
    QVERIFY(!findTreeItem(resultsTreeWidget, 2, QStringLiteral("First")));

    // The replaced block still belongs to the region
    blockItem = findTreeItem(resultsTreeWidget, 0, QString());
    QVERIFY(blockItem && blockItem->text(0).contains(QStringLiteral("region")));

    dialog.reject();
}

void OCRDialogTest::mergeAndSplitLines()
{
    // EDIT-02: lines are merged and split in the context menu of the results
    setLinesHandler({ { QStringLiteral("Alpha"), QStringLiteral("beta") }, { QStringLiteral("Gamma") } });

    WidgetFixture fixture(createScanDocument({ QStringLiteral("Two lines") }));
    const pdfviewer::PDFOCRDocumentDialog::Context context = fixture.createContext();

    ModalResponder responder({ QStringLiteral("Merge with Next Line"), QStringLiteral("Discard"), QStringLiteral("OK") });

    pdfviewer::PDFOCRDocumentDialog dialog(context, nullptr);
    dialog.show();

    auto* recognizeButton = dialog.findChild<QPushButton*>(QStringLiteral("recognizeButton"));
    auto* stopButton = dialog.findChild<QPushButton*>(QStringLiteral("stopButton"));
    auto* undoButton = dialog.findChild<QPushButton*>(QStringLiteral("undoButton"));
    auto* resultsTreeWidget = dialog.findChild<QTreeWidget*>(QStringLiteral("resultsTreeWidget"));
    QVERIFY(recognizeButton && stopButton && undoButton && resultsTreeWidget);

    selectComboData(dialog, "engineComboBox", QLatin1String(PDFOCRTestEngineFactory::IDENTIFIER));
    selectComboData(dialog, "existingTextPolicyComboBox", int(PDFOCRExistingTextPolicy::OnlyPagesWithoutText));

    recognizeButton->click();
    QTRY_VERIFY_WITH_TIMEOUT(!stopButton->isEnabled() && recognizeButton->isEnabled(), 60000);

    QTreeWidgetItem* firstLine = findTreeItem(resultsTreeWidget, 1, QStringLiteral("Alpha beta"));
    QVERIFY(firstLine);
    QVERIFY(findTreeItem(resultsTreeWidget, 1, QStringLiteral("Gamma")));

    // Merge
    resultsTreeWidget->setCurrentItem(firstLine);
    Q_EMIT resultsTreeWidget->customContextMenuRequested(QPoint(5, 5));
    QTRY_VERIFY2_WITH_TIMEOUT(findTreeItem(resultsTreeWidget, 1, QStringLiteral("Alpha beta Gamma")) != nullptr, qPrintable(responder.messages.join(QChar('|'))), 10000);
    QVERIFY(!findTreeItem(resultsTreeWidget, 1, QStringLiteral("Gamma")));
    QVERIFY(undoButton->isEnabled());

    // Split after the word "beta"
    responder.setPreferredButtons({ QStringLiteral("Split Line After Word"), QStringLiteral("Discard"), QStringLiteral("OK") });
    QTreeWidgetItem* wordItem = findTreeItem(resultsTreeWidget, 2, QStringLiteral("beta"));
    QVERIFY(wordItem);
    resultsTreeWidget->setCurrentItem(wordItem);
    Q_EMIT resultsTreeWidget->customContextMenuRequested(QPoint(5, 5));
    QTRY_VERIFY2_WITH_TIMEOUT(findTreeItem(resultsTreeWidget, 1, QStringLiteral("Gamma")) != nullptr, qPrintable(responder.messages.join(QChar('|'))), 10000);
    QVERIFY(findTreeItem(resultsTreeWidget, 1, QStringLiteral("Alpha beta")));
    QVERIFY(!findTreeItem(resultsTreeWidget, 1, QStringLiteral("Alpha beta Gamma")));

    // The last word of a line cannot be split off
    responder.messages.clear();
    wordItem = findTreeItem(resultsTreeWidget, 2, QStringLiteral("Gamma"));
    QVERIFY(wordItem);
    resultsTreeWidget->setCurrentItem(wordItem);
    Q_EMIT resultsTreeWidget->customContextMenuRequested(QPoint(5, 5));
    QTRY_VERIFY_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("Split Line After Word (disabled)")), 10000);

    // Undo restores the merged line
    undoButton->click();
    QVERIFY(findTreeItem(resultsTreeWidget, 1, QStringLiteral("Alpha beta Gamma")));

    // EDIT-05: the preview of a phrase over more words shows the text of the line
    auto* findEdit = dialog.findChild<QLineEdit*>(QStringLiteral("findEdit"));
    auto* replaceEdit = dialog.findChild<QLineEdit*>(QStringLiteral("replaceEdit"));
    auto* replaceAllButton = dialog.findChild<QPushButton*>(QStringLiteral("replaceAllButton"));
    QVERIFY(findEdit && replaceEdit && replaceAllButton);
    findEdit->setText(QStringLiteral("beta Gamma"));
    replaceEdit->setText(QStringLiteral("Delta"));
    responder.setPreferredButtons({ QStringLiteral("Yes"), QStringLiteral("Discard"), QStringLiteral("OK") });
    responder.messages.clear();
    replaceAllButton->setEnabled(true);
    replaceAllButton->click();
    QTRY_VERIFY2_WITH_TIMEOUT(responder.messages.join(QChar('|')).contains(QStringLiteral("Alpha beta Gamma -> Alpha Delta")), qPrintable(responder.messages.join(QChar('|'))), 10000);
    QVERIFY(findTreeItem(resultsTreeWidget, 1, QStringLiteral("Alpha Delta")));

    dialog.reject();
}

QTEST_MAIN(OCRDialogTest)

#include "tst_ocrdialogtest.moc"
