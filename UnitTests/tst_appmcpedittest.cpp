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

#include "appmcpfixture.h"

#include "pdfappannotationcontroller.h"
#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappannotationoperations.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpconfirmdialog.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcphost.h"
#include "pdfappmcppreview.h"
#include "pdfappmcptool_annotations.h"
#include "pdfappmcptool_files.h"
#include "pdfappmcptool_forms.h"
#include "pdfappmcptool_outline.h"
#include "pdfappmcptoolset.h"
#include "pdfapplog.h"
#include "pdfapptab.h"
#include "pdfappundostack.h"

#include "pdfcomponentassistants.h"
#include "pdfcomponentbuttons.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdrawwidget.h"
#include "pdffluentwidgets.h"
#include "pdfform.h"
#include "pdfmessagebox.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>

#include <filesystem>
#include <mutex>
#include <tuple>
#include <system_error>

// Package C of step 7 (edit tools, confirmations, commit): plan of step 7,
// chapter 9 C
class AppMcpEditTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void smoke();
    void annotationsAddEveryType();
    void annotationsUpdateDeleteReply();
    void annotationsHostile();
    void reviewAnnotationEdgeArguments();
    void annotationOperationsHostile();
    void confirmationDialogs();
    void confirmationTimeoutAndQueue();
    void confirmationReadAndBoth();
    void previewGhosts();
    void statusItemsAndBanner();
    void notificationBar();
    void queuedRequestsRebuild();
    void lockedBusyDialogOpen();
    void signedDocumentWarning();
    void restrictedPermissions();
    void formsHeadless();
    void formsFormatsAndPassword();
    void formsAmbiguousNames();
    void outlineEdits();
    void outlineQueuedRebuild();
    void changeJobHeavy();
    void outlineHostile();
    void filesRules();
    void filesOpen();
    void filesSave();
    void reviewFilesSaveRefusals();
    void reviewChangeRefusedAfterConfirmation();
    void filesSaveCopy();
    void filesPathLinksAndFolders();
    void filesSaveCopyTargetAppeared();
    void filesPendingEditsAndSavedAs();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void dialogBlocksDocumentShortcuts();
    void languageChange();
    void httpEndToEnd();
    void fixFocusBackAfterDialog();
    void fixPageCountPlural();
    void fixClientNamePercent();
    void fixHostileEditArguments();
    void fixMarkupQuoteFromTextIndex();
    void fixDialogTextsEveryKind();
    void fixGhostShapes();
};

/// Translator of the test: records the texts and the counts, translates nothing
class McpEditRecordingTranslator : public QTranslator
{
public:
    explicit McpEditRecordingTranslator(QObject* parent) :
        QTranslator(parent)
    {
    }

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        if (context && sourceText)
        {
            std::lock_guard<std::mutex> lock(mutex);
            records.emplace_back(QString::fromLatin1(context), QString::fromUtf8(sourceText), n);
        }
        return QString();
    }

    bool contains(const QString& context, const QString& sourceText, int n) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& record : records)
        {
            if (std::get<0>(record) == context && std::get<1>(record) == sourceText && std::get<2>(record) == n)
            {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex mutex;   ///< The workers translate too
    mutable std::vector<std::tuple<QString, QString, int>> records;
};

/// Translator of the test: every text of the application is marked
class McpEditTestTranslator : public QTranslator
{
public:
    explicit McpEditTestTranslator(QObject* parent) :
        QTranslator(parent)
    {
    }

    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        if (!sourceText || !context || !QByteArray(context).startsWith("pdf::PDFAppMcp"))
        {
            return QString();
        }
        return QStringLiteral("\u00A7") + QString::fromUtf8(sourceText);
    }
};

/// The fixture of the tests of package C: the real confirmation interface with a
/// controllable clock, no input delay (the rule of the delay is tested apart)
class McpEditFixture : public pdftest::AppMcpFixture
{
public:
    explicit McpEditFixture(QSize size = QSize(1440, 960)) :
        AppMcpFixture(size)
    {
        if (host() && ui())
        {
            host()->setConfirmationProvider(ui());
            ui()->setInputDelay(0);
            ui()->setClock(std::bind(&McpEditFixture::getClock, this));
        }
    }

    pdf::PDFAppMcpConfirmationUi* ui() const { return pdf::PDFAppMcpConfirmationUi::find(host()); }

    qint64 getClock() const { return time; }

    /// Waits for the dialog of the user interface
    pdf::PDFAppMcpConfirmDialog* waitDialog(int timeout = 10000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < qint64(timeout) * getTimeoutFactor())
        {
            if (ui() && ui()->getDialog() && ui()->getDialog()->isVisible())
            {
                return ui()->getDialog();
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        }
        return nullptr;
    }

    /// Opens a sample and waits until it is shown
    pdf::PDFAppDocumentTab* openReady(const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = open(filePath);
        if (!tab || !QTest::qWaitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }, 10000 * getTimeoutFactor()))
        {
            return nullptr;
        }
        settle();
        return tab;
    }

    qint64 time = 1000000;
};

/// Helpers of the tests of package C
class AppMcpEditTestHelper
{
public:
    AppMcpEditTestHelper() = delete;

    static QJsonObject json(const char* text)
    {
        return QJsonDocument::fromJson(QByteArray(text)).object();
    }

    static QJsonObject structured(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return result ? result->toQJson().value(QStringLiteral("structuredContent")).toObject() : QJsonObject();
    }

    static bool isError(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return !result || result->kind != pdf::PDFMcpCallResult::Kind::Complete || result->isError;
    }

    static QString text(const std::optional<pdf::PDFMcpCallResult>& result)
    {
        return result ? (result->kind == pdf::PDFMcpCallResult::Kind::Error ? result->errorMessage : result->getFirstText()) : QString();
    }

    /// Annotation of the subject (Subj) on the pages of the document
    static pdf::PDFObjectReference findBySubject(const pdf::PDFDocument* document, const QString& subject)
    {
        const pdf::PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            for (const pdf::PDFObjectReference& reference : catalog->getPage(i)->getAnnotations())
            {
                const pdf::PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(reference));
                pdf::PDFDocumentDataLoaderDecorator loader(document);
                if (dictionary && loader.readTextStringFromDictionary(dictionary, "Subj", QString()) == subject)
                {
                    return reference;
                }
            }
        }
        return pdf::PDFObjectReference();
    }

    static const pdf::PDFDictionary* dictionary(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        return document->getDictionaryFromObject(document->getObjectByReference(reference));
    }

    static QString readText(const pdf::PDFDocument* document, pdf::PDFObjectReference reference, const char* key)
    {
        pdf::PDFDocumentDataLoaderDecorator loader(document);
        const pdf::PDFDictionary* object = dictionary(document, reference);
        return object ? loader.readTextStringFromDictionary(object, key, QString()) : QString();
    }

    static bool isOnPage(const pdf::PDFDocument* document, pdf::PDFObjectReference reference)
    {
        const pdf::PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            const std::vector<pdf::PDFObjectReference>& annotations = catalog->getPage(i)->getAnnotations();
            if (std::find(annotations.begin(), annotations.end(), reference) != annotations.end())
            {
                return true;
            }
        }
        return false;
    }

    static pdf::PDFObjectReference parseId(const QString& id)
    {
        qint64 object = 0;
        qint64 generation = 0;
        pdf::PDFAppMcpArguments::parseAnnotationId(id, &object, &generation);
        return pdf::PDFObjectReference(object, generation);
    }

    static QString formatId(pdf::PDFObjectReference reference)
    {
        return pdf::PDFAppMcpArguments::formatAnnotationId(reference.objectNumber, reference.generation);
    }

    /// Value of a form field of the document (a text string or a name)
    static QString fieldValue(const pdf::PDFDocument* document, const QString& name)
    {
        pdf::PDFForm form = pdf::PDFForm::parse(document, document->getCatalog()->getFormObject());
        QString result;
        form.apply(std::bind(&AppMcpEditTestHelper::collectFieldValue, document, name, &result, std::placeholders::_1));
        return result;
    }

    static void collectFieldValue(const pdf::PDFDocument* document, const QString& name, QString* result, const pdf::PDFFormField* field)
    {
        if (field->getName(pdf::PDFFormField::NameType::FullyQualified) != name)
        {
            return;
        }
        const pdf::PDFObject& value = field->getValue();
        if (value.isName())
        {
            *result = QString::fromLatin1(value.getString());
            return;
        }
        if (value.isArray())
        {
            QStringList values;
            for (const pdf::PDFObject& item : *value.getArray())
            {
                values << pdf::PDFDocumentDataLoaderDecorator(document).readTextString(item, QString());
            }
            *result = values.join(QLatin1Char(','));
            return;
        }
        *result = pdf::PDFDocumentDataLoaderDecorator(document).readTextString(value, QString());
    }

    static pdf::PDFObject name(const char* value) { return pdf::PDFObject::createName(QByteArray(value)); }
    static pdf::PDFObject textObject(const QString& value) { return pdf::PDFObjectFactory::createTextString(value); }
    static pdf::PDFObject reference(pdf::PDFObjectReference value) { return pdf::PDFObject::createReference(value); }

    static pdf::PDFObject array(std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder builder;
        for (const pdf::PDFObject& item : items)
        {
            builder.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::move(builder));
    }

    static pdf::PDFObject dictionaryObject(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    static QString write(pdf::PDFDocumentBuilder& builder, const QString& folder, const QString& fileName)
    {
        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(fileName);
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// Two text fields "applicant.name" and "spouse.name" (an ambiguous suffix)
    static QString ambiguousForm(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        std::vector<pdf::PDFObjectReference> parents;
        int y = 300;
        for (const char* parentName : { "applicant", "spouse" })
        {
            const pdf::PDFObjectReference parent = builder.addObject(pdf::PDFObject());
            const pdf::PDFObjectReference child = builder.addObject(dictionaryObject({ { "FT", name("Tx") }, { "T", textObject(QStringLiteral("name")) },
                                                                                        { "Parent", reference(parent) },
                                                                                        { "DA", pdf::PDFObject::createString(QByteArray("/Helv 12 Tf 0 g")) } }));
            builder.setObject(parent, dictionaryObject({ { "T", textObject(QString::fromLatin1(parentName)) }, { "Kids", array({ reference(child) }) } }));
            builder.createFormFieldWidget(child, page, pdf::PDFObjectReference(), QRectF(30, y, 200, 24));
            y -= 40;
            parents.push_back(parent);
        }
        builder.setCatalogAcroForm(builder.createAcroForm(parents));
        return write(builder, folder, QStringLiteral("ambiguous-form.pdf"));
    }

    /// Text fields with the keystroke functions of the Acrobat form library (a
    /// number with a decimal comma, a date, a ZIP code), a plain and a password field
    static QString formattedForm(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        std::vector<pdf::PDFObjectReference> fields;
        int y = 360;
        const std::vector<std::pair<const char*, QString>> definitions = {
            { "amount", QStringLiteral("AFNumber_Keystroke(2, 2, 0, 0, \"\", false);") },
            { "date", QStringLiteral("AFDate_KeystrokeEx(\"dd.mm.yyyy\");") },
            { "zip", QStringLiteral("AFSpecial_Keystroke(0);") },
            { "plain", QString() },
            { "secret", QString() }
        };
        for (const auto& definition : definitions)
        {
            pdf::PDFDictionaryBuilder field;
            field.setEntry(pdf::PDFInplaceOrMemoryString("FT"), name("Tx"));
            field.setEntry(pdf::PDFInplaceOrMemoryString("T"), textObject(QString::fromLatin1(definition.first)));
            field.setEntry(pdf::PDFInplaceOrMemoryString("DA"), pdf::PDFObject::createString(QByteArray("/Helv 12 Tf 0 g")));
            if (!definition.second.isEmpty())
            {
                const pdf::PDFObjectReference keystroke = builder.createActionJavaScript(definition.second);
                field.setEntry(pdf::PDFInplaceOrMemoryString("AA"), dictionaryObject({ { "K", reference(keystroke) } }));
            }
            if (QByteArray(definition.first) == "secret")
            {
                field.setEntry(pdf::PDFInplaceOrMemoryString("Ff"), pdf::PDFObject::createInteger(1 << 13));
            }
            const pdf::PDFObjectReference reference = builder.addObject(pdf::PDFObject::createDictionary(std::move(field)));
            builder.createFormFieldWidget(reference, page, pdf::PDFObjectReference(), QRectF(30, y, 200, 24));
            fields.push_back(reference);
            y -= 40;
        }
        builder.setCatalogAcroForm(builder.createAcroForm(fields));
        return write(builder, folder, QStringLiteral("formatted-form.pdf"));
    }

    /// An outline, whose item is the page itself (read only - step 4 rules)
    static QString hostileOutline(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
        builder.setObject(root, dictionaryObject({ { "Type", name("Outlines") }, { "First", reference(page) }, { "Last", reference(page) } }));
        builder.mergeTo(builder.getCatalogReference(), dictionaryObject({ { "Outlines", reference(root) } }));
        return write(builder, folder, QStringLiteral("hostile-outline.pdf"));
    }

    static QImage render(QWidget* widget, qreal devicePixelRatio = 1.0)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(Qt::white);
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb();
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if ((line[x] & 0x00FFFFFF) == (leak & 0x00FFFFFF))
                {
                    ++count;
                }
            }
        }
        return count;
    }

    static void saveScreenshot(QWidget* widget, const QString& name)
    {
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        if (folder.isEmpty() || !widget)
        {
            return;
        }
        QDir().mkpath(folder);
        for (qreal ratio : { 1.0, 2.0 })
        {
            const QImage image = render(widget, ratio);
            image.save(QDir(folder).filePath(name + (ratio > 1.0 ? QStringLiteral("-2x.png") : QStringLiteral(".png"))));
        }
    }

    /// The annotations.add request with one item of every type on page 1
    static QJsonObject addEveryType()
    {
        return json(R"({"action":"add","add":{"items":[
            {"type":"highlight","page":1,"quads":[[30,365,120,365,30,350,120,350]],"color":"yellow"},
            {"type":"underline","page":1,"quads":[[30,345,120,345,30,330,120,330]]},
            {"type":"strikeout","page":1,"quads":[[30,325,120,325,30,310,120,310]]},
            {"type":"squiggly","page":1,"quads":[[30,305,120,305,30,290,120,290]]},
            {"type":"note","page":1,"point":{"x":200,"y":380},"contents":"A note","icon":"key"},
            {"type":"text_box","page":1,"rect":{"left":30,"bottom":200,"right":150,"top":240},"contents":"A text box","fontSize":10},
            {"type":"rectangle","page":1,"rect":{"left":160,"bottom":200,"right":220,"top":240},"color":"red","fillColor":"blue","width":2},
            {"type":"ellipse","page":1,"rect":{"left":230,"bottom":200,"right":290,"top":240}},
            {"type":"line","page":1,"points":[{"x":30,"y":180},{"x":120,"y":180}]},
            {"type":"arrow","page":1,"points":[{"x":30,"y":170},{"x":120,"y":160}],"color":"green"},
            {"type":"polygon","page":1,"points":[{"x":150,"y":150},{"x":200,"y":190},{"x":250,"y":150}]},
            {"type":"polyline","page":1,"points":[{"x":30,"y":100},{"x":60,"y":130},{"x":90,"y":100}]},
            {"type":"ink","page":1,"strokes":[[{"x":100,"y":100},{"x":110,"y":110},{"x":120,"y":105}]]},
            {"type":"stamp","page":1,"rect":{"left":150,"bottom":40,"right":280,"top":90},"stamp":"draft"},
            {"type":"link","page":1,"rect":{"left":30,"bottom":40,"right":100,"top":60},"url":"https://example.com/"},
            {"type":"link","page":1,"rect":{"left":30,"bottom":10,"right":100,"top":30},"targetPage":2}
        ]}})");
    }
};

void AppMcpEditTest::initTestCase()
{
    // The dialogs, bars and the leak checks are drawn by the style of the design
    // (without it Qt draws the buttons and the fields of the tab by its own style)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    pdf::PDFAppMcpFilesTool::setPasswordTimeout(pdf::PDFAppMcpFilesTool::DEFAULT_PASSWORD_TIMEOUT);
}

void AppMcpEditTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppMcpEditTest::smoke()
{
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.host());
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Ask));

    // The edit tools are offered with Ask, absent with Deny
    for (const char* name : { "annotations", "forms", "outline", "files" })
    {
        QVERIFY2(fixture.host()->getCatalog()->findTool(QString::fromLatin1(name)), name);
    }
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Deny));
    QVERIFY(!fixture.host()->getCatalog()->findTool(u"annotations"));
    QVERIFY(fixture.host()->getCatalog()->findTool(u"files"));

    // Without a document: an execution error
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), AppMcpEditTestHelper::json(R"({"action":"delete","delete":{"ids":["1:0"]}})"));
    QVERIFY(result.has_value());
    QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::Complete);
    QVERIFY(result->isError);

    // The interface of the confirmations is installed by the controller
    QVERIFY(pdf::PDFAppMcpConfirmationUi::find(fixture.host()));
}

void AppMcpEditTest::annotationsAddEveryType()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);

    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), H::addEveryType());
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));

    // One undo step with the description of the assistant
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);
    const QString description = document->getUndoStack()->getUndoDescription();
    QVERIFY2(description.startsWith(QStringLiteral("Claude Code added ")), qPrintable(description));
    QVERIFY(description.contains(QStringLiteral("1 highlight")));
    QVERIFY(description.contains(QStringLiteral("2 links")));

    const QJsonObject add = H::structured(result).value(QStringLiteral("add")).toObject();
    QCOMPARE(add.value(QStringLiteral("documentId")).toString(), document->getId());
    QCOMPARE(add.value(QStringLiteral("undoDescription")).toString(), description);
    const QJsonArray created = add.value(QStringLiteral("created")).toArray();
    QCOMPARE(created.size(), 16);

    const pdf::PDFDocument* pdfDocument = document->getDocument().data();
    for (const QJsonValue& value : created)
    {
        const QJsonObject item = value.toObject();
        const pdf::PDFObjectReference reference = H::parseId(item.value(QStringLiteral("id")).toString());
        QVERIFY(H::isOnPage(pdfDocument, reference));
        QCOMPARE(item.value(QStringLiteral("page")).toInt(), 1);
        const pdf::PDFDictionary* dictionary = H::dictionary(pdfDocument, reference);
        QVERIFY(dictionary);
        QVERIFY2(H::readText(pdfDocument, reference, "NM").startsWith(QStringLiteral("pdf4qt-mcp-")), qPrintable(item.value(QStringLiteral("type")).toString()));
        if (item.value(QStringLiteral("type")).toString() != QLatin1String("link"))
        {
            // The author is the name of the assistant; the appearance stream exists
            QCOMPARE(H::readText(pdfDocument, reference, "T"), QStringLiteral("Claude Code"));
            QVERIFY2(dictionary->hasKey("AP"), qPrintable(item.value(QStringLiteral("type")).toString()));
        }
    }

    // The geometry of the rectangle
    const pdf::PDFObjectReference rectangle = H::parseId(created.at(6).toObject().value(QStringLiteral("id")).toString());
    pdf::PDFDocumentDataLoaderDecorator loader(pdfDocument);
    const QRectF rect = loader.readRectangle(H::dictionary(pdfDocument, rectangle)->get("Rect"), QRectF());
    QVERIFY(rect.contains(QRectF(QPointF(161, 201), QPointF(219, 239))));

    // Undo and redo as one step
    QVERIFY(document->getUndoStack()->undo());
    QVERIFY(!H::isOnPage(document->getDocument().data(), rectangle));
    QVERIFY(document->getUndoStack()->redo());
    QVERIFY(H::isOnPage(document->getDocument().data(), rectangle));

    // The activity row of the change
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Ok));
}

void AppMcpEditTest::annotationsUpdateDeleteReply()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    const pdf::PDFObjectReference square = H::findBySubject(document->getDocument().data(), QStringLiteral("square"));
    const pdf::PDFObjectReference locked = H::findBySubject(document->getDocument().data(), QStringLiteral("locked"));
    const pdf::PDFObjectReference text = H::findBySubject(document->getDocument().data(), QStringLiteral("text"));
    QVERIFY(square.isValid() && locked.isValid() && text.isValid());

    // update: color, rectangle, contents, hidden
    QJsonObject update = H::json(R"({"action":"update","update":{"items":[{"id":"","color":"blue","contents":"Changed","rect":{"left":10,"bottom":10,"right":60,"top":50},"hidden":true}]}})");
    QJsonObject updateObject = update.value(QStringLiteral("update")).toObject();
    QJsonArray items = updateObject.value(QStringLiteral("items")).toArray();
    QJsonObject item = items.at(0).toObject();
    item.insert(QStringLiteral("id"), H::formatId(square));
    items.replace(0, item);
    updateObject.insert(QStringLiteral("items"), items);
    update.insert(QStringLiteral("update"), updateObject);
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), update);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::readText(document->getDocument().data(), square, "Contents"), QStringLiteral("Changed"));
    QVERIFY(document->getUndoStack()->getUndoDescription().startsWith(QStringLiteral("Claude Code changed 1 annotation")));

    // A Locked annotation changes only its text (ISO 32000-2 Table 167)
    item.insert(QStringLiteral("id"), H::formatId(locked));
    items.replace(0, item);
    updateObject.insert(QStringLiteral("items"), items);
    update.insert(QStringLiteral("update"), updateObject);
    result = fixture.callTool(QStringLiteral("annotations"), update);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("locked")), qPrintable(H::text(result)));

    // reply: author = the assistant
    QJsonObject reply = H::json(R"({"action":"reply","reply":{"id":"","contents":"A reply"}})");
    QJsonObject replyObject = reply.value(QStringLiteral("reply")).toObject();
    replyObject.insert(QStringLiteral("id"), H::formatId(text));
    reply.insert(QStringLiteral("reply"), replyObject);
    result = fixture.callTool(QStringLiteral("annotations"), reply);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const pdf::PDFObjectReference created = H::parseId(H::structured(result).value(QStringLiteral("reply")).toObject().value(QStringLiteral("id")).toString());
    QCOMPARE(H::readText(document->getDocument().data(), created, "T"), QStringLiteral("Claude Code"));

    // delete; a second delete of the same annotation: it does not exist
    QJsonObject remove = H::json(R"({"action":"delete","delete":{"ids":[]}})");
    QJsonObject removeObject;
    removeObject.insert(QStringLiteral("ids"), QJsonArray({ H::formatId(square) }));
    remove.insert(QStringLiteral("delete"), removeObject);
    result = fixture.callTool(QStringLiteral("annotations"), remove);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(!H::isOnPage(document->getDocument().data(), square));
    result = fixture.callTool(QStringLiteral("annotations"), remove);
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("does not exist")));

    // A locked annotation is not deleted
    removeObject.insert(QStringLiteral("ids"), QJsonArray({ H::formatId(locked) }));
    remove.insert(QStringLiteral("delete"), removeObject);
    result = fixture.callTool(QStringLiteral("annotations"), remove);
    QVERIFY(H::isError(result));
    QVERIFY(H::isOnPage(document->getDocument().data(), locked));
}

void AppMcpEditTest::reviewAnnotationEdgeArguments()
{
    // Review RFW7-M (coverage of the annotations tool): the fill color, width and
    // opacity of an update are written; arguments, which the schema accepts but
    // the tool cannot use, are execution errors naming their path, and nothing
    // is committed
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const pdf::PDFObjectReference square = H::findBySubject(document->getDocument().data(), QStringLiteral("square"));
    const pdf::PDFObjectReference text = H::findBySubject(document->getDocument().data(), QStringLiteral("text"));
    QVERIFY(square.isValid() && text.isValid());

    auto update = [&](const QJsonObject& changes)
    {
        QJsonObject item = changes;
        item.insert(QStringLiteral("id"), H::formatId(square));
        QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("update") } });
        arguments.insert(QStringLiteral("update"), QJsonObject({ { QStringLiteral("items"), QJsonArray({ item }) } }));
        return fixture.callTool(QStringLiteral("annotations"), arguments);
    };

    // fillColor, width and opacity
    std::optional<pdf::PDFMcpCallResult> result = update(H::json(R"({"fillColor":"#FF0000","width":3,"opacity":0.5})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const int undoSteps = pdftest::AppDocumentFixture::undoCount(document);
    QCOMPARE(undoSteps, 1);
    {
        const pdf::PDFDocument* pdfDocument = document->getDocument().data();
        const pdf::PDFDictionary* dictionary = pdfDocument->getDictionaryFromObject(pdfDocument->getObjectByReference(square));
        QVERIFY(dictionary);
        pdf::PDFDocumentDataLoaderDecorator loader(pdfDocument);
        QCOMPARE(loader.readNumberFromDictionary(dictionary, "CA", 1.0), 0.5);
        const std::vector<pdf::PDFReal> fill = loader.readNumberArrayFromDictionary(dictionary, "IC");
        QCOMPARE(fill.size(), size_t(3));
        QCOMPARE(fill[0], 1.0);
    }

    // Values the tool cannot use
    struct Case
    {
        const char* arguments;
        const char* message;
    };
    const Case updateCases[] =
    {
        { R"({"fillColor":"no such color"})", "items[0].fillColor" },
        { R"({"rect":{"left":10,"bottom":10,"right":10,"top":50}})", "items[0].rect" },
        { R"({})", "nothing to change" },
    };
    for (const Case& item : updateCases)
    {
        result = update(H::json(item.arguments));
        QVERIFY2(H::isError(result), item.arguments);
        QVERIFY2(H::text(result).contains(QString::fromLatin1(item.message)), qPrintable(H::text(result)));
    }

    // reply: an empty text, an annotation, which does not exist
    QJsonObject reply({ { QStringLiteral("action"), QStringLiteral("reply") } });
    reply.insert(QStringLiteral("reply"), QJsonObject({ { QStringLiteral("id"), H::formatId(text) }, { QStringLiteral("contents"), QStringLiteral("   ") } }));
    result = fixture.callTool(QStringLiteral("annotations"), reply);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("the text of the reply is empty")), qPrintable(H::text(result)));
    reply.insert(QStringLiteral("reply"), QJsonObject({ { QStringLiteral("id"), H::formatId(pdf::PDFObjectReference(99999, 0)) }, { QStringLiteral("contents"), QStringLiteral("Hello") } }));
    result = fixture.callTool(QStringLiteral("annotations"), reply);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("does not exist")), qPrintable(H::text(result)));

    // add: a note without its point, an ink without strokes, a
    // link to a page, which does not exist
    const Case addCases[] =
    {
        { R"({"action":"add","add":{"items":[{"type":"note","page":1}]}})", "note needs point" },
        { R"({"action":"add","add":{"items":[{"type":"ink","page":1}]}})", "ink needs strokes" },
        { R"({"action":"add","add":{"items":[{"type":"link","page":1,"rect":{"left":30,"bottom":10,"right":100,"top":30},"targetPage":99}]}})", "page 99 does not exist" },
    };
    for (const Case& item : addCases)
    {
        result = fixture.callTool(QStringLiteral("annotations"), H::json(item.arguments));
        QVERIFY2(H::isError(result), item.arguments);
        QVERIFY2(H::text(result).contains(QString::fromLatin1(item.message)), qPrintable(H::text(result)));
    }
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoSteps);
}

void AppMcpEditTest::annotationsHostile()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 2));
    QVERIFY(tab);
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(tab->getDocument());

    const char* hostile[] = {
        R"({"action":"add","add":{"items":[{"type":"highlight","page":1}]}})",
        R"({"action":"add","add":{"items":[{"type":"highlight","page":9,"quads":[[0,0,1,0,0,1,1,1]]}]}})",
        R"({"action":"add","add":{"items":[{"type":"rectangle","page":1,"rect":{"left":10,"bottom":10,"right":10,"top":10}}]}})",
        R"({"action":"add","add":{"items":[{"type":"rectangle","page":1,"rect":{"left":-900000000,"bottom":10,"right":10,"top":20}}]}})",
        R"({"action":"add","add":{"items":[{"type":"link","page":1,"rect":{"left":0,"bottom":0,"right":10,"top":10},"url":"javascript:void0"}]}})",
        R"({"action":"add","add":{"items":[{"type":"link","page":1,"rect":{"left":0,"bottom":0,"right":10,"top":10}}]}})",
        R"({"action":"add","add":{"items":[{"type":"text_box","page":1,"rect":{"left":0,"bottom":0,"right":10,"top":10}}]}})",
        R"({"action":"add","add":{"items":[{"type":"line","page":1,"points":[{"x":1,"y":1},{"x":1,"y":1}]}]}})",
        R"({"action":"add","add":{"items":[{"type":"polygon","page":1,"points":[{"x":1,"y":1},{"x":2,"y":2}]}]}})",
        R"({"action":"add","add":{"items":[{"type":"note","page":1,"color":"not-a-color","point":{"x":1,"y":1}}]}})",
        R"({"action":"update","update":{"items":[{"id":"abc","contents":"x"}]}})",
        R"({"action":"update","update":{"items":[{"id":"99999:0"}]}})",
        R"({"action":"delete","delete":{"ids":["0:0"]}})",
        R"({"action":"reply","reply":{"id":"1:0","contents":"   "}})",
    };
    for (const char* arguments : hostile)
    {
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), H::json(arguments));
        QVERIFY2(H::isError(result), arguments);
        QVERIFY2(!H::text(result).isEmpty(), arguments);
    }

    // Bidirectional controls and controls are removed from the written texts
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"),
        H::json("{\"action\":\"add\",\"add\":{\"items\":[{\"type\":\"note\",\"page\":1,\"point\":{\"x\":10,\"y\":300},\"contents\":\"A\\u202Eevil\\u0001text\"}]}}"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const pdf::PDFObjectReference note = H::parseId(H::structured(result).value(QStringLiteral("add")).toObject().value(QStringLiteral("created")).toArray().at(0).toObject().value(QStringLiteral("id")).toString());
    const QString contents = H::readText(tab->getDocument()->getDocument().data(), note, "Contents");
    QVERIFY(!contents.contains(QChar(0x202E)));
    QVERIFY(!contents.contains(QChar(0x0001)));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), undoBefore + 1);
}

void AppMcpEditTest::annotationOperationsHostile()
{
    // The widget-free operation refuses invalid items as a whole
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFDocumentPointer document(new pdf::PDFDocument(builder.build()));

    std::vector<pdf::PDFObjectReference> created;
    int invalidIndex = -1;
    QVERIFY(!pdf::PDFAppAnnotationOperations::createAnnotations(pdf::PDFDocumentPointer(), {}, nullptr, &created, &invalidIndex));
    QVERIFY(!pdf::PDFAppAnnotationOperations::createAnnotations(document, {}, nullptr, &created, &invalidIndex));

    pdf::PDFAppAnnotationItem valid;
    valid.kind = pdf::PDFAppAnnotationItem::Kind::Square;
    valid.pageIndex = 0;
    valid.rectangle = QRectF(10, 10, 50, 50);

    std::vector<pdf::PDFAppAnnotationItem> items;
    for (pdf::PDFAppAnnotationItem::Kind kind : { pdf::PDFAppAnnotationItem::Kind::TextMarkup, pdf::PDFAppAnnotationItem::Kind::StickyNote, pdf::PDFAppAnnotationItem::Kind::FreeText,
                                                  pdf::PDFAppAnnotationItem::Kind::Square, pdf::PDFAppAnnotationItem::Kind::Circle, pdf::PDFAppAnnotationItem::Kind::Line,
                                                  pdf::PDFAppAnnotationItem::Kind::Arrow, pdf::PDFAppAnnotationItem::Kind::Polygon, pdf::PDFAppAnnotationItem::Kind::Polyline,
                                                  pdf::PDFAppAnnotationItem::Kind::Ink, pdf::PDFAppAnnotationItem::Kind::Stamp, pdf::PDFAppAnnotationItem::Kind::WebLink,
                                                  pdf::PDFAppAnnotationItem::Kind::PageLink })
    {
        // An empty geometry of every kind is invalid
        pdf::PDFAppAnnotationItem item;
        item.kind = kind;
        item.pageIndex = 0;
        item.point = QPointF(qQNaN(), 1.0);
        item.targetPageIndex = 5;
        item.url = QStringLiteral("file:///etc/passwd");
        QVERIFY(!pdf::PDFAppAnnotationOperations::createAnnotations(document, { valid, item }, nullptr, &created, &invalidIndex));
        QCOMPARE(invalidIndex, 1);
    }

    // A page beyond the document, a huge coordinate
    pdf::PDFAppAnnotationItem beyond = valid;
    beyond.pageIndex = 1;
    QVERIFY(!pdf::PDFAppAnnotationOperations::createAnnotations(document, { beyond }, nullptr, &created, &invalidIndex));
    pdf::PDFAppAnnotationItem huge = valid;
    huge.rectangle = QRectF(0, 0, 1.0e9, 10);
    QVERIFY(!pdf::PDFAppAnnotationOperations::createAnnotations(document, { huge }, nullptr, &created, &invalidIndex));

    // Too many points of a shape and of an ink
    pdf::PDFAppAnnotationItem polygon = valid;
    polygon.kind = pdf::PDFAppAnnotationItem::Kind::Polygon;
    for (int i = 0; i < pdf::PDFAppAnnotationOperations::MAXIMUM_SHAPE_POINTS + 1; ++i)
    {
        polygon.points << QPointF(i % 100, i / 100);
    }
    QVERIFY(!pdf::PDFAppAnnotationOperations::createAnnotations(document, { polygon }, nullptr, &created, &invalidIndex));

    // Valid items: one reference each
    QVERIFY(pdf::PDFAppAnnotationOperations::createAnnotations(document, { valid, valid }, nullptr, &created, &invalidIndex));
    QCOMPARE(created.size(), size_t(2));
}

void AppMcpEditTest::confirmationDialogs()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // The change dialog (940:133693)
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"),
        H::json(R"({"action":"add","add":{"items":[{"type":"highlight","page":1,"quads":[[30,365,120,365,30,350,120,350]]},{"type":"note","page":1,"point":{"x":200,"y":380},"contents":"Segments that grew"}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to change %1").arg(document->getTitle()));
    QVERIFY2(dialog->getOriginText().startsWith(QStringLiteral("This computer \u00B7 asked at ")), qPrintable(dialog->getOriginText()));
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Asked because Edit documents is set to Ask"));
    QVERIFY(dialog->getNoteTexts().contains(QStringLiteral("Undo removes all 2 changes in one step. Nothing is saved to the file.")));
    QVERIFY(dialog->getRowTexts().contains(QStringLiteral("Add 1 highlight")));
    QVERIFY(dialog->getRowTexts().contains(QStringLiteral("Add 1 note")));
    QVERIFY(dialog->getShowOnPageLink());
    QCOMPARE(dialog->getCountdownText(), QStringLiteral("Denied automatically in 2:00"));
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-change"));

    // The head of 940:134119 takes the whole width of the card: the monogram 36 at
    // the leading edge of the content (padding 22), the title on one line beside it
    {
        QWidget* head = dialog->getHeadWidget();
        QWidget* monogram = head ? head->findChild<QWidget*>(QStringLiteral("monogram")) : nullptr;
        QWidget* changes = dialog->findChild<QWidget*>(QStringLiteral("changes"));
        QVERIFY(head && monogram);
        const QRect monogramRect(monogram->mapTo(dialog, QPoint(0, 0)), monogram->size());
        const QRect headRect(head->mapTo(dialog, QPoint(0, 0)), head->size());
        if (changes)
        {
            QCOMPARE(headRect.width(), changes->width());
            QCOMPARE(headRect.left(), changes->mapTo(dialog, QPoint(0, 0)).x());
        }
        QCOMPARE(monogramRect.left(), headRect.left());
        QCOMPARE(monogramRect.size(), QSize(36, 36));

        // The monogram tile of the assistant (accent/amber/bg without a border, as
        // the tiles of Connect an assistant), not the bordered avatar of a person
        pdf::PDFAssistantAvatar* avatar = qobject_cast<pdf::PDFAssistantAvatar*>(monogram);
        QVERIFY(avatar);
        QCOMPARE(avatar->getAvatar().monogram, QStringLiteral("Cl"));
        QVERIFY(!avatar->getAvatar().commandLine);
        QVERIFY2(headRect.height() <= 40, qPrintable(QString::number(headRect.height())));
    }

    // Buttons: Deny Secondary, Allow Primary at the right; no default button,
    // no button has the focus
    QPushButton* deny = dialog->getDenyButton();
    QPushButton* allow = dialog->getAllowButton();
    QCOMPARE(deny->text(), QStringLiteral("Deny"));
    QCOMPARE(allow->text(), QStringLiteral("Allow"));
    QVERIFY(deny->geometry().left() < allow->geometry().left());
    QVERIFY(!deny->isDefault() && !allow->isDefault());
    QVERIFY(!qobject_cast<QAbstractButton*>(QApplication::focusWidget()));

    // Enter does nothing, Escape denies
    QTest::keyClick(dialog, Qt::Key_Return);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!call->isFinished());
    QTest::keyClick(dialog, Qt::Key_Escape);
    std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("denied")));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedUser));

    // Allow by the button: one undo step
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
    call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":200,"y":380}}]}})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getNoteTexts().contains(QStringLiteral("Undo removes the change in one step. Nothing is saved to the file.")));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    result = fixture.waitResult(call);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Allowed));

    // The buttons ignore the input of the first second (the user may be typing)
    fixture.ui()->setInputDelay(400);
    call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":150,"y":380}}]}})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isInputBlocked());
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!call->isFinished());
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isInputBlocked(), 5000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::confirmationTimeoutAndQueue()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 2));
    QVERIFY(tab);

    // Three requests: "1 of 3", answered one after another
    std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
    for (int i = 0; i < 3; ++i)
    {
        calls.push_back(fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300},"contents":"Please confirm the delivery date before payment."}]}})")));
    }
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(3), 10000);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QTRY_COMPARE(dialog->getQueueText(), QStringLiteral("1 of 3"));
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-queue"));

    // The countdown with the injected clock: 8 s later 1:52
    const std::vector<quint64> ids = fixture.ui()->getWaitingIds();
    QCOMPARE(dialog->getConfirmationId(), ids[0]);
    fixture.time += 8000;
    fixture.ui()->tick();
    QCOMPARE(dialog->getCountdownText(), QStringLiteral("Denied automatically in 1:52"));

    // The 2 minutes count from the dialog of a request (item 38): the queued ones
    // have not started theirs, they wait within the bound of the queue
    QVERIFY(fixture.ui()->isShown(ids[0]));
    QVERIFY(!fixture.ui()->isShown(ids[1]) && !fixture.ui()->isShown(ids[2]));
    QCOMPARE(fixture.ui()->getRemainingTime(ids[1]), qint64(pdf::PDFAppMcpConfirmationUi::DEFAULT_QUEUE_TIMEOUT - 8000));

    // The first answered after 108 s: the second appears with its whole 2 minutes
    fixture.time += 100000;
    fixture.ui()->tick();
    // (the calls reach the queue in the order of their workers: the call of a
    // request is taken from its dialog)
    const std::shared_ptr<pdf::PDFMcpCall> firstCall = dialog->getConfirmation()->call;
    QVERIFY(firstCall && !firstCall->isFinished());
    fixture.ui()->answer(ids[0], pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(firstCall)));
    QTRY_VERIFY(fixture.ui()->getDialog() && fixture.ui()->getDialog()->getConfirmationId() == ids[1]);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getQueueText(), QStringLiteral("1 of 2"));
    QCOMPARE(dialog->getCountdownText(), QStringLiteral("Denied automatically in 2:00"));
    const std::shared_ptr<pdf::PDFMcpCall> secondCall = dialog->getConfirmation()->call;

    // No answer in 2 min after its dialog appeared = denied
    fixture.time += 119000;
    fixture.ui()->tick();
    QVERIFY(!secondCall->isFinished());
    fixture.time += 2000;
    fixture.ui()->tick();
    std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(secondCall);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("2 minutes")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedTimeout));

    // ... the third too: asked 229 s ago, its dialog has the whole time
    QTRY_VERIFY(fixture.ui()->getDialog() && fixture.ui()->getDialog()->getConfirmationId() == ids[2]);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getCountdownText(), QStringLiteral("Denied automatically in 2:00"));
    const std::shared_ptr<pdf::PDFMcpCall> thirdCall = dialog->getConfirmation()->call;
    fixture.time += 121000;
    fixture.ui()->tick();
    result = fixture.waitResult(thirdCall);
    QVERIFY(H::isError(result));
    QVERIFY(fixture.ui()->getWaitingIds().empty());
    for (const std::shared_ptr<pdf::PDFMcpCall>& call : calls)
    {
        QVERIFY(call->isFinished());
    }
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!fixture.ui()->getDialog());

    // The overall bound of the queue: a request, whose dialog never appears (its
    // tab is not shown), is denied after DEFAULT_QUEUE_TIMEOUT
    pdf::PDFAppDocumentTab* other = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Other.pdf"), 1));
    QVERIFY(other);
    fixture.window->activateTab(other);
    pdftest::AppDocumentFixture::settle();
    QJsonObject arguments = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");
    arguments.insert(QStringLiteral("documentId"), tab->getDocument()->getId());
    std::shared_ptr<pdf::PDFMcpCall> background = fixture.startCall(QStringLiteral("annotations"), arguments);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(1), 10000);
    const quint64 backgroundId = fixture.ui()->getWaitingIds().front();
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!fixture.ui()->getDialog());
    QVERIFY(!fixture.ui()->isShown(backgroundId));
    fixture.time += pdf::PDFAppMcpConfirmationUi::DEFAULT_QUEUE_TIMEOUT - 1000;
    fixture.ui()->tick();
    QVERIFY(!background->isFinished());
    fixture.time += 2000;
    fixture.ui()->tick();
    result = fixture.waitResult(background);
    QVERIFY(H::isError(result));
    QVERIFY(background->isFinished());
    QVERIFY(fixture.ui()->getWaitingIds().empty());

    // ... with its own texts: never "within 2 minutes" of a shown dialog (item 59)
    QVERIFY2(!H::text(result).contains(QStringLiteral("2 minutes")), qPrintable(H::text(result)));
    QVERIFY2(H::text(result).contains(QStringLiteral("10 minutes without being shown")), qPrintable(H::text(result)));
    pdftest::AppDocumentFixture::settle();
    bool hasQueueRow = false;
    for (const pdf::PDFMcpActivityRow& row : fixture.activity())
    {
        if (row.status == pdf::PDFMcpActivityStatus::DeniedTimeout && row.detail.value(QStringLiteral("reason")).toString() == pdf::PDFAppMcpActivity::getQueueTimeoutReason())
        {
            hasQueueRow = true;
            QCOMPARE(pdf::PDFAppMcpActivity::getStatusText(row.status, row.client, row.detail), QStringLiteral("Denied automatically · not shown within 10 min"));
        }
    }
    QVERIFY(hasQueueRow);
}

void AppMcpEditTest::confirmationReadAndBoth()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
    QVERIFY(tab);

    // A change, which needs the read approval too, is one dialog with both reasons (958:28030)
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Asked because Read documents and Edit documents are set to Ask"));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(call)));
    QVERIFY(fixture.host()->getPolicy().isReadApproved(call->getRequest().client.assistantKey));

    // The read approval dialog (966:150610) for another assistant
    fixture.setClient(QStringLiteral("gemini-cli-mcp-client"));
    call = fixture.startCall(QStringLiteral("document"), H::json(R"({"action":"get_info"})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Gemini CLI wants to read your open documents"));
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Asked because Read documents is set to Ask"));
    QVERIFY(dialog->getNoteTexts().contains(QStringLiteral("Applies until Gemini CLI disconnects")));
    QVERIFY(dialog->getRowTexts().contains(QStringLiteral("Project_Report.pdf")));
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-read"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(H::isError(fixture.waitResult(call)));
    QVERIFY(!fixture.host()->getPolicy().isReadApproved(call->getRequest().client.assistantKey));

    // A remote assistant: the tag Remote and its address
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = std::make_shared<pdf::PDFAppMcpConfirmation>();
    confirmation->kind = pdf::PDFAppMcpConfirmationKind::Change;
    confirmation->clientName = QStringLiteral("Codex CLI");
    confirmation->clientNameRaw = QStringLiteral("codex-mcp-client");
    confirmation->isRemote = true;
    confirmation->remoteAddress = QStringLiteral("192.168.1.57");
    confirmation->document = tab->getDocument();
    confirmation->permissions = pdf::PDFAppMcpPermissionKind::Edit;
    confirmation->details.insert(QStringLiteral("signatureCount"), 2);
    confirmation->details.insert(QStringLiteral("isAnnotationChange"), true);
    pdf::PDFAppMcpConfirmDialog remote(confirmation, &fixture.host()->getResolver(), nullptr);
    QVERIFY2(remote.getOriginText().startsWith(QStringLiteral("Remote \u00B7 192.168.1.57 \u00B7 asked at ")), qPrintable(remote.getOriginText()));
    QVERIFY(remote.getNoteTexts().contains(QStringLiteral("Adding annotations may invalidate 2 signatures")));
    QVERIFY(remote.findChild<QWidget*>(QStringLiteral("signatureWarning")));

    // The rules of 958:28030
    confirmation->permissions = pdf::PDFAppMcpPermissionKinds();
    confirmation->isAlwaysConfirmed = true;
    QCOMPARE(pdf::PDFAppMcpConfirmDialog::createReason(*confirmation), QStringLiteral("Overwriting the original file is always confirmed"));
    confirmation->isAlwaysConfirmed = false;
    confirmation->isAfterDialog = true;
    QCOMPARE(pdf::PDFAppMcpConfirmDialog::createReason(*confirmation), QStringLiteral("Asked because a dialog was open when the request came"));
    confirmation->permissions = pdf::PDFAppMcpPermissionKind::Edit | pdf::PDFAppMcpPermissionKind::Files;
    QCOMPARE(pdf::PDFAppMcpConfirmDialog::createReason(*confirmation), QStringLiteral("Asked because Edit documents and Open and save files are set to Ask"));

    // Texts of the model: no controls, no bidirectional overrides, elided at 80
    const QString sanitized = pdf::PDFAppMcpConfirmDialog::sanitize(QStringLiteral("A\u202E\u0007&B") + QString(200, QLatin1Char('x')));
    QVERIFY(!sanitized.contains(QChar(0x202E)) && !sanitized.contains(QChar(0x0007)));
    QCOMPARE(sanitized.size(), qsizetype(80));
    QVERIFY(sanitized.endsWith(QChar(0x2026)));
    QCOMPARE(pdf::PDFAppMcpConfirmDialog::formatRemainingTime(112000), QStringLiteral("1:52"));
    QCOMPARE(pdf::PDFAppMcpConfirmDialog::formatRemainingTime(-5), QStringLiteral("0:00"));
}

void AppMcpEditTest::previewGhosts()
{
    using H = AppMcpEditTestHelper;

    // The pixels of the ghosts: the color at 30 % multiplied, the dashed amber border
    {
        pdf::PDFAppMcpGhostOverlay overlay;
        pdf::PDFAppMcpGhost ghost;
        ghost.pageIndex = 0;
        ghost.shape = pdf::PDFAppMcpGhost::Shape::Rectangle;
        ghost.points << QPointF(20, 20) << QPointF(120, 80);
        ghost.color = QColor(255, 255, 0);
        overlay.setGhosts({ ghost });

        QImage image(200, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            overlay.drawGhosts(&painter, 0, QTransform());
            overlay.drawGhosts(&painter, 1, QTransform::fromTranslate(1000, 1000));   // another page: nothing
        }
        const QColor inside = image.pixelColor(70, 50);
        QCOMPARE(inside.red(), 255);
        QVERIFY2(qAbs(inside.blue() - 178) <= 3, qPrintable(QString::number(inside.blue())));

        // The border is dashed: amber pixels and gaps on the top edge
        const QColor amber = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::AccentAmberBorderStrong);
        int amberPixels = 0;
        int whitePixels = 0;
        for (int x = 22; x < 118; ++x)
        {
            const QColor color = image.pixelColor(x, 20);
            amberPixels += (qAbs(color.red() - amber.red()) < 40 && qAbs(color.green() - amber.green()) < 40 && qAbs(color.blue() - amber.blue()) < 40) ? 1 : 0;
            whitePixels += color == QColor(Qt::white) ? 1 : 0;
        }
        QVERIFY2(amberPixels > 10, qPrintable(QString::number(amberPixels)));
        QVERIFY(whitePixels > 0 || amberPixels < 96);
        QCOMPARE(image.pixelColor(150, 100), QColor(Qt::white));

        // Hostile ghosts of the details are refused
        QVERIFY(!pdf::PDFAppMcpGhost::fromJson(H::json(R"({"page":-1,"shape":"rectangle","points":[1,2,3,4]})")));
        QVERIFY(!pdf::PDFAppMcpGhost::fromJson(H::json(R"({"page":0,"shape":"unknown","points":[1,2,3,4]})")));
        QVERIFY(!pdf::PDFAppMcpGhost::fromJson(H::json(R"({"page":0,"shape":"rectangle","points":[1e300,2,3,4]})")));
        QVERIFY(pdf::PDFAppMcpGhost::fromJson(ghost.toJson()));
    }

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);

    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"),
        H::json(R"({"action":"add","add":{"items":[{"type":"highlight","page":2,"quads":[[30,365,120,365,30,350,120,350]]},{"type":"note","page":2,"point":{"x":200,"y":380}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog && dialog->getShowOnPageLink());

    // Show on Page: the dialog hides, the ghosts and the preview bar (955:151877)
    QTest::mouseClick(dialog->getShowOnPageLink(), Qt::LeftButton);
    QTRY_VERIFY(fixture.ui()->getPreviewBar() && fixture.ui()->getPreviewBar()->isVisible());
    QVERIFY(!fixture.ui()->getDialog());
    QCOMPARE(fixture.ui()->getGhostOverlay()->getGhosts().size(), size_t(2));
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(1));
    pdf::PDFAppMcpPreviewBar* bar = fixture.ui()->getPreviewBar();
    QCOMPARE(bar->getMessage(), QStringLiteral("Claude Code wants to add these \u00B7 1 highlight, 1 note"));
    QCOMPARE(bar->getCardRect().height(), pdf::PDFAppMcpFloatingBar::CARD_HEIGHT);
    // 955:151949: the message of the preview, the waiting item the first trailing
    // item - the message keeps the free space
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("Preview of the requested changes – nothing is changed until you allow them"));
    H::saveScreenshot(tab, QStringLiteral("mcp-preview"));

    // The status item brings the dialog back
    QToolButton* item = fixture.ui()->findStatusItem(tab->getView());
    QVERIFY(item && item->isVisible());
    QCOMPARE(item->text(), QStringLiteral("Claude Code is waiting for your confirmation"));
    {
        pdf::PDFAppDocumentStatusBar* statusBar = tab->getView()->getStatusBar();
        QWidget* message = statusBar->findChild<QWidget*>(QStringLiteral("statusMessage"));
        QVERIFY(message);
        const int itemLeft = item->mapTo(statusBar, QPoint(0, 0)).x();
        QVERIFY2(itemLeft > statusBar->width() / 2, qPrintable(QString::number(itemLeft)));
        for (QWidget* other : statusBar->getItems())
        {
            if (other != item && other->isVisible() && other->objectName() != QStringLiteral("statusPage"))
            {
                QVERIFY(other->mapTo(statusBar, QPoint(0, 0)).x() > itemLeft);
            }
        }
    }
    QTest::mouseClick(item, Qt::LeftButton);
    QVERIFY(fixture.waitDialog());
    QTRY_VERIFY(!fixture.ui()->getPreviewBar());
    QVERIFY(fixture.ui()->getGhostOverlay()->getGhosts().empty());
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("Ready"));

    // Allow from the preview bar
    QTest::mouseClick(fixture.ui()->getDialog()->getShowOnPageLink(), Qt::LeftButton);
    QTRY_VERIFY(fixture.ui()->getPreviewBar());
    QTest::mouseClick(fixture.ui()->getPreviewBar()->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(call)));
    QTRY_VERIFY(!fixture.ui()->getPreviewBar());
}

void AppMcpEditTest::statusItemsAndBanner()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* invoice = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Invoice.pdf"), 2));
    pdf::PDFAppDocumentTab* report = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(invoice && report);
    fixture.window->activateTab(report);
    pdftest::AppDocumentFixture::settle();

    // A request of Invoice.pdf while Project_Report.pdf is shown: no dialog, the
    // status item of the current tab names the document (955:154475)
    QJsonObject arguments = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");
    arguments.insert(QStringLiteral("documentId"), invoice->getDocument()->getId());
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), arguments);
    QTRY_VERIFY(fixture.ui()->findStatusItem(report->getView()) && fixture.ui()->findStatusItem(report->getView())->isVisible());
    QVERIFY(!fixture.ui()->getDialog());
    QCOMPARE(fixture.ui()->findStatusItem(report->getView())->text(), QStringLiteral("Claude Code is waiting for your confirmation \u00B7 Invoice.pdf"));
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(report));

    // Home: the banner at the top (967:12672)
    fixture.window->activateTab(fixture.window->getHomeTab());
    QTRY_VERIFY(fixture.ui()->findBanner(fixture.window->getHomeTab()) && fixture.ui()->findBanner(fixture.window->getHomeTab())->isVisible());
    pdf::PDFAppMcpWaitingBanner* banner = fixture.ui()->findBanner(fixture.window->getHomeTab());
    QCOMPARE(banner->getTitleText(), QStringLiteral("Claude Code is waiting for your confirmation \u00B7 Invoice.pdf"));
    // (its dialog has not appeared yet: the bound of the queue - items 15, 38)
    QVERIFY2(banner->getBodyText().startsWith(QStringLiteral("Claude Code asks to change Invoice.pdf. The request waits in its tab and is denied automatically in 10:00")), qPrintable(banner->getBodyText()));
    // 967:12873: 62 high - the paddings 12 inside of the border around the monogram 36
    // of the assistant without a border and without the CLI badge (967:12874)
    QCOMPARE(banner->height(), 62);
    pdf::PDFAssistantAvatar* bannerAvatar = banner->findChild<pdf::PDFAssistantAvatar*>();
    QVERIFY(bannerAvatar);
    QCOMPARE(bannerAvatar->size(), QSize(36, 36));
    H::saveScreenshot(fixture.window->getHomeTab(), QStringLiteral("mcp-home-banner"));

    // A New tab has the banner too
    pdf::PDFAppTab* newTab = fixture.window->openNewTab();
    QVERIFY(newTab);
    QTRY_VERIFY(fixture.ui()->findBanner(newTab) && fixture.ui()->findBanner(newTab)->isVisible());
    QVERIFY(!banner->isVisible());

    // Show switches to the tab of the request and shows its dialog
    QTest::mouseClick(fixture.ui()->findBanner(newTab)->getShowButton(), Qt::LeftButton);
    QVERIFY(fixture.waitDialog());
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(invoice));
    QCOMPARE(fixture.ui()->findStatusItem(invoice->getView())->text(), QStringLiteral("Claude Code is waiting for your confirmation"));

    // The status item of another tab switches to the tab of the request
    fixture.window->activateTab(report);
    QTRY_VERIFY(!fixture.ui()->getDialog());
    QTest::mouseClick(fixture.ui()->findStatusItem(report->getView()), Qt::LeftButton);
    QVERIFY(fixture.waitDialog());
    QCOMPARE(fixture.window->getCurrentTab(), static_cast<pdf::PDFAppTab*>(invoice));
    fixture.ui()->answer(fixture.ui()->getDialog()->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));
    QTRY_VERIFY(!fixture.ui()->findStatusItem(report->getView())->isVisible());
}

void AppMcpEditTest::notificationBar()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Also with Edit documents = Allow (owner Q3)
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"),
        H::json(R"({"action":"add","add":{"items":[{"type":"highlight","page":1,"quads":[[30,365,120,365,30,350,120,350]]},{"type":"note","page":1,"point":{"x":200,"y":380},"contents":"x"}]}})"));
    QVERIFY(!H::isError(result));
    pdf::PDFAppMcpNotificationBar* bar = fixture.ui()->findNotificationBar(document);
    QVERIFY(bar && bar->isVisible());
    QCOMPARE(bar->getMessage(), QStringLiteral("Claude Code added 1 highlight and 1 note"));
    QCOMPARE(bar->getCardRect().height(), pdf::PDFAppMcpFloatingBar::CARD_HEIGHT);
    QVERIFY(bar->getUndoButton()->isEnabled());
    H::saveScreenshot(tab, QStringLiteral("mcp-notification"));

    // Show Changes: the Notes panel with the new annotations selected
    QTest::mouseClick(bar->getShowChangesButton(), Qt::LeftButton);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(tab->getView()->getCurrentPanelId(), pdf::PDFAppDocumentPanelIds::notes());
    const QJsonArray created = H::structured(result).value(QStringLiteral("add")).toObject().value(QStringLiteral("created")).toArray();
    std::vector<pdf::PDFObjectReference> selected = tab->getView()->getAnnotationController()->getSelectedAnnotations();
    QCOMPARE(selected.size(), size_t(created.size()));

    // Undo removes the whole request; the bar closes with the change
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
    QTest::mouseClick(bar->getUndoButton(), Qt::LeftButton);
    QTRY_VERIFY(!fixture.ui()->findNotificationBar(document));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore - 1);

    // Undo only while the change is on top: another change disables it and closes the bar
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":20,"y":380}}]}})"))));
    bar = fixture.ui()->findNotificationBar(document);
    QVERIFY(bar);
    QPointer<pdf::PDFAppMcpNotificationBar> barGuard(bar);
    std::optional<pdf::PDFModifiedDocument> userChange = pdf::PDFAppAnnotationOperations::setAnnotationsHidden(document->getDocument(),
        { H::parseId(H::structured(result).value(QStringLiteral("add")).toObject().value(QStringLiteral("created")).toArray().at(0).toObject().value(QStringLiteral("id")).toString()) }, true);
    if (userChange)
    {
        document->commit(*userChange, QStringLiteral("User"), document->getVersion(), pdf::PDFAppChangeSource::User);
    }
    else
    {
        QVERIFY(document->getUndoStack()->undo());
    }
    QTRY_VERIFY(!barGuard || barGuard->isHidden());

    // The bar hides itself after its time
    fixture.ui()->setNotificationTimeout(50);
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":40,"y":380}}]}})"))));
    QVERIFY(fixture.ui()->findNotificationBar(document));
    QTRY_VERIFY(!fixture.ui()->findNotificationBar(document));

    // The close button
    fixture.ui()->setNotificationTimeout(pdf::PDFAppMcpConfirmationUi::DEFAULT_NOTIFICATION_TIMEOUT);
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":60,"y":380}}]}})"))));
    QTest::mouseClick(fixture.ui()->findNotificationBar(document)->getCloseButton(), Qt::LeftButton);
    QTRY_VERIFY(!fixture.ui()->findNotificationBar(document));
}

void AppMcpEditTest::queuedRequestsRebuild()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);

    // Three queued changes, each built on the version of its request: all succeed
    std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
    for (int i = 0; i < 3; ++i)
    {
        calls.push_back(fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":3,"point":{"x":100,"y":300}}]}})")));
    }
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(3), 10000);
    for (int i = 0; i < 3; ++i)
    {
        // The workers finish the builds in any order: the dialog of the oldest
        // waiting confirmation first
        pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
        QVERIFY(dialog);
        const std::shared_ptr<pdf::PDFMcpCall> call = dialog->getConfirmation()->call;
        QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
        QVERIFY2(!H::isError(fixture.waitResult(call)), qPrintable(H::text(call->getResult())));
        pdftest::AppDocumentFixture::settle();
    }
    for (const std::shared_ptr<pdf::PDFMcpCall>& call : calls)
    {
        QVERIFY(call->isFinished());
    }
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 3);

    // A target deleted while the request waits: "The document changed in the meantime"
    const pdf::PDFObjectReference circle = H::findBySubject(document->getDocument().data(), QStringLiteral("circle"));
    QJsonObject update = H::json(R"({"action":"update","update":{"items":[{"id":"","contents":"x"}]}})");
    QJsonObject updateObject;
    updateObject.insert(QStringLiteral("items"), QJsonArray({ QJsonObject({ { QStringLiteral("id"), H::formatId(circle) }, { QStringLiteral("contents"), QStringLiteral("x") } }) }));
    update.insert(QStringLiteral("update"), updateObject);
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), update);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    std::optional<pdf::PDFModifiedDocument> removal = pdf::PDFAppAnnotationOperations::deleteAnnotations(document->getDocument(), { circle });
    QVERIFY(removal);
    QVERIFY(document->commit(*removal, QStringLiteral("User"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    const std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("The document changed in the meantime")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Failed));
}

void AppMcpEditTest::lockedBusyDialogOpen()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QJsonObject add = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");

    // Locked (O20): denied at once without a dialog
    document->setLocked(true);
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), add);
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("locked")));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedLocked));
    QVERIFY(fixture.ui()->getWaitingIds().empty());

    // A locked document may save a copy (it is not changed)
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Allow));
    QJsonObject copy = H::json(R"({"action":"save_copy","save_copy":{}})");
    QJsonObject copyObject;
    copyObject.insert(QStringLiteral("path"), fixture.path(QStringLiteral("locked-copy.pdf")));
    copy.insert(QStringLiteral("save_copy"), copyObject);
    result = fixture.callTool(QStringLiteral("files"), copy);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("locked-copy.pdf"))));
    document->setLocked(false);

    // Busy: an operation, which blocks changes
    QPointer<pdf::PDFAppDocumentOperation> operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Edit, QStringLiteral("Test"), true);
    result = fixture.callTool(QStringLiteral("annotations"), add);
    QVERIFY(H::isError(result));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedBusy));
    operation->finish();
    pdftest::AppDocumentFixture::settle();

    // A modal dialog of the application: an edit is refused (denied_dialog)
    QDialog* modal = new QDialog(fixture.window);
    modal->setModal(true);
    modal->show();
    QTRY_VERIFY(QApplication::activeModalWidget() == modal);
    result = fixture.callTool(QStringLiteral("annotations"), add);
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("dialog")));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedDialog));

    // ... an open request is always confirmed with the rule of the dialog; its
    // question waits until the modal dialog closes (plan 2.5, items 15 / 38) and
    // its 2 minutes start then
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("open") },
        { QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("other.pdf"), 1) } }) } }));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(1), 10000);
    pdftest::AppDocumentFixture::settle();
    fixture.ui()->refresh();
    QVERIFY(!fixture.ui()->getDialog());
    QVERIFY(fixture.ui()->isWaitingForModalDialog());
    QVERIFY(!fixture.ui()->isShown(fixture.ui()->getWaitingIds().front()));
    fixture.time += 90000;
    fixture.ui()->tick();
    QVERIFY(!call->isFinished());
    QVERIFY(!fixture.ui()->getDialog());

    delete modal;
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(!fixture.ui()->isWaitingForModalDialog());
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Asked because a dialog was open when the request came"));
    QVERIFY(dialog->getConfirmation()->isAfterDialog);
    QCOMPARE(dialog->getCountdownText(), QStringLiteral("Denied automatically in 2:00"));
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));

    // A first read with Ask waits for the modal dialog too (plan 2.5)
    fixture.setClient(QStringLiteral("gemini-cli-mcp-client"));
    modal = new QDialog(fixture.window);
    modal->setModal(true);
    modal->show();
    QTRY_VERIFY(QApplication::activeModalWidget() == modal);
    call = fixture.startCall(QStringLiteral("document"), H::json(R"({"action":"get_info"})"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(1), 10000);
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!fixture.ui()->getDialog());
    QVERIFY(fixture.ui()->isWaitingForModalDialog());
    fixture.time += 300000;
    fixture.ui()->tick();
    QVERIFY(!call->isFinished());
    delete modal;
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getConfirmation()->kind, pdf::PDFAppMcpConfirmationKind::ReadDocuments);
    QCOMPARE(dialog->getCountdownText(), QStringLiteral("Denied automatically in 2:00"));
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    std::optional<pdf::PDFMcpCallResult> readResult = fixture.waitResult(call);
    QVERIFY2(!H::isError(readResult), qPrintable(H::text(readResult)));

    // ... within the bound of the queue: a modal dialog, which stays open, denies it
    fixture.setClient(QStringLiteral("codex-mcp-client"));
    modal = new QDialog(fixture.window);
    modal->setModal(true);
    modal->show();
    QTRY_VERIFY(QApplication::activeModalWidget() == modal);
    call = fixture.startCall(QStringLiteral("document"), H::json(R"({"action":"get_info"})"));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(1), 10000);
    fixture.time += pdf::PDFAppMcpConfirmationUi::DEFAULT_QUEUE_TIMEOUT + 1000;
    fixture.ui()->tick();
    readResult = fixture.waitResult(call);
    QVERIFY(H::isError(readResult));
    // (its own text: the dialog never appeared - item 59)
    QVERIFY2(H::text(readResult).contains(QStringLiteral("10 minutes without being shown")), qPrintable(H::text(readResult)));
    QVERIFY2(!H::text(readResult).contains(QStringLiteral("2 minutes")), qPrintable(H::text(readResult)));
    delete modal;
}

void AppMcpEditTest::signedDocumentWarning()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Allow));
    const QString filePath = pdftest::AppDocumentSamples::signedDocument(fixture.folder());
    QFile original(filePath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalData = original.readAll();
    original.close();

    pdf::PDFAppDocumentTab* tab = fixture.openReady(filePath);
    QVERIFY(tab);

    // The amber warning of a signed document (958:27048)
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->findChild<QWidget*>(QStringLiteral("signatureWarning")));
    QVERIFY(dialog->getNoteTexts().contains(QStringLiteral("Adding annotations may invalidate the signature")));
    {
        // 958:27126: the changes, the warning, the undo note, the reason
        QWidget* changes = dialog->findChild<QWidget*>(QStringLiteral("changes"));
        QWidget* warning = dialog->findChild<QWidget*>(QStringLiteral("signatureWarning"));
        QWidget* undoNote = dialog->findChild<QWidget*>(QStringLiteral("undoNote"));
        QWidget* reason = dialog->findChild<QWidget*>(QStringLiteral("reason"));
        QVERIFY(changes && warning && undoNote && reason);
        const auto top = [dialog](QWidget* widget) { return widget->mapTo(dialog, QPoint(0, 0)).y(); };
        QVERIFY(top(changes) < top(warning));
        QVERIFY(top(warning) < top(undoNote));
        QVERIFY(top(undoNote) < top(reason));
    }
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-signed"));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(call)));

    // The save is always confirmed; a signed document is saved incrementally (the
    // signed bytes stay)
    call = fixture.startCall(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Overwriting the original file is always confirmed"));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY2(!H::isError(fixture.waitResult(call)), qPrintable(H::text(call->getResult())));
    QFile saved(filePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray savedData = saved.readAll();
    QVERIFY(savedData.size() > originalData.size());
    QVERIFY(savedData.startsWith(originalData));
    QVERIFY(!tab->getDocument()->isModified());
}

void AppMcpEditTest::restrictedPermissions()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(tab);

    // O8: no annotations, no form filling
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("security settings")));
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[{"name":"name","value":"X"}]}})"));
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("security settings")));
}

void AppMcpEditTest::formsHeadless()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);

    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[
        {"name":"name","value":"Jane"},
        {"name":"notes","value":"Line 1\nLine 2"},
        {"name":"agree","checked":true},
        {"name":"choice","value":"B"},
        {"name":"fruit","value":"Banana"},
        {"name":"colors","selected":["Red","Blue"]},
        {"name":"fixed","value":"X"},
        {"name":"reset","value":"X"},
        {"name":"code","value":"TOO-LONG-CODE"},
        {"name":"missing","value":"X"},
        {"name":"signature","value":"X"}
    ]}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const QJsonObject output = H::structured(result).value(QStringLiteral("set_values")).toObject();
    const QJsonArray changed = output.value(QStringLiteral("changed")).toArray();
    const QJsonArray errors = output.value(QStringLiteral("errors")).toArray();
    QVERIFY2(changed.contains(QStringLiteral("name")) && changed.contains(QStringLiteral("agree")) && changed.contains(QStringLiteral("choice")) &&
             changed.contains(QStringLiteral("fruit")) && changed.contains(QStringLiteral("colors")) && changed.contains(QStringLiteral("notes")),
             QJsonDocument(output).toJson().constData());
    QCOMPARE(errors.size(), 5);   // fixed (read only), reset (push button), code (too long), missing, signature
    QVERIFY(output.value(QStringLiteral("note")).toString().contains(QStringLiteral("JavaScript")));

    // One undo step with the flag FormField, the values in the document
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);
    QVERIFY(document->getUndoStack()->getUndoDescription().startsWith(QStringLiteral("Claude Code filled 6 form fields")));
    const pdf::PDFDocument* pdfDocument = document->getDocument().data();
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("name")), QStringLiteral("Jane"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("fruit")), QStringLiteral("Banana"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("colors")), QStringLiteral("Red,Blue"));
    QVERIFY(H::fieldValue(pdfDocument, QStringLiteral("agree")) != QStringLiteral("Off"));

    // The same values again: unchanged, no undo step
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[{"name":"name","value":"Jane"}]}})"));
    QVERIFY(!H::isError(result));
    QVERIFY(H::structured(result).value(QStringLiteral("set_values")).toObject().value(QStringLiteral("unchanged")).toArray().contains(QStringLiteral("name")));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);

    // A value of a wrong kind, a radio value, which does not exist
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[{"name":"agree","value":"Yes"},{"name":"choice","value":"Z"}]}})"));
    QVERIFY(!H::isError(result));
    QCOMPARE(H::structured(result).value(QStringLiteral("set_values")).toObject().value(QStringLiteral("errors")).toArray().size(), 2);

    // Invalid arguments: two kinds of a value, no name
    QVERIFY(H::isError(fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[{"name":"name","value":"A","checked":true}]}})"))));
    QVERIFY(H::isError(fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[{"name":"","value":"A"}]}})"))));

    // reset: the fields at their defaults again; a second reset does nothing
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"reset"})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(H::structured(result).value(QStringLiteral("reset")).toObject().value(QStringLiteral("reset")).toBool());
    QVERIFY(H::fieldValue(document->getDocument().data(), QStringLiteral("name")) != QStringLiteral("Jane"));
    const int undoAfterReset = pdftest::AppDocumentFixture::undoCount(document);
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"reset","reset":{"names":["name"]}})"));
    QVERIFY(!H::isError(result));
    QVERIFY(!H::structured(result).value(QStringLiteral("reset")).toObject().value(QStringLiteral("reset")).toBool());
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoAfterReset);

    // A document without a form
    pdf::PDFAppDocumentTab* simple = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("simple.pdf"), 1));
    QVERIFY(simple);
    QVERIFY(H::isError(fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"reset"})"))));

    QCOMPARE(pdf::PDFAppMcpFormsTool::matchFieldNames({ QStringLiteral("a.name"), QStringLiteral("b.name"), QStringLiteral("name2") }, QStringLiteral("name")),
             QStringList({ QStringLiteral("a.name"), QStringLiteral("b.name") }));
    QCOMPARE(pdf::PDFAppMcpFormsTool::matchFieldNames({ QStringLiteral("a.name"), QStringLiteral("name") }, QStringLiteral("name")), QStringList({ QStringLiteral("name") }));
}

void AppMcpEditTest::formsFormatsAndPassword()
{
    using H = AppMcpEditTestHelper;

    // Values are committed as the input of the user (plan 3.7): the native
    // keystroke functions accept and normalize them, a password is never written
    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(H::formattedForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);

    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[
        {"name":"amount","value":"1234,5"},
        {"name":"date","value":"31.12.2026"},
        {"name":"zip","value":"12345"},
        {"name":"plain","value":"abc,5"},
        {"name":"secret","value":"hunter2"}
    ]}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QJsonObject output = H::structured(result).value(QStringLiteral("set_values")).toObject();
    QCOMPARE(output.value(QStringLiteral("changed")).toArray().size(), 4);
    QJsonArray errors = output.value(QStringLiteral("errors")).toArray();
    QCOMPARE(errors.size(), 1);
    QCOMPARE(errors.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("secret"));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore + 1);

    // The number with the decimal comma is stored as a number (as the editor of the field does it)
    const pdf::PDFDocument* pdfDocument = document->getDocument().data();
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("amount")), QStringLiteral("1234.5"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("date")), QStringLiteral("31.12.2026"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("zip")), QStringLiteral("12345"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("plain")), QStringLiteral("abc,5"));
    QVERIFY(H::fieldValue(pdfDocument, QStringLiteral("secret")).isEmpty());

    // The stored form of a number is accepted too; texts, which the format rejects, are errors per field
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[
        {"name":"amount","value":"99.25"},
        {"name":"date","value":"not a date"},
        {"name":"zip","value":"12a"}
    ]}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    output = H::structured(result).value(QStringLiteral("set_values")).toObject();
    QCOMPARE(output.value(QStringLiteral("changed")).toArray(), QJsonArray({ QStringLiteral("amount") }));
    errors = output.value(QStringLiteral("errors")).toArray();
    QCOMPARE(errors.size(), 2);
    QVERIFY2(errors.at(0).toObject().value(QStringLiteral("message")).toString().contains(QStringLiteral("format")), QJsonDocument(errors).toJson().constData());
    pdfDocument = document->getDocument().data();
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("amount")), QStringLiteral("99.25"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("date")), QStringLiteral("31.12.2026"));
    QCOMPARE(H::fieldValue(pdfDocument, QStringLiteral("zip")), QStringLiteral("12345"));
}

void AppMcpEditTest::formsAmbiguousNames()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(H::ambiguousForm(fixture.folder()));
    QVERIFY(tab);
    const QJsonObject arguments = H::json(R"({"action":"set_values","set_values":{"values":[{"name":"name","value":"Jane"}]}})");

    // Without the capability of elicitation: the candidates in the error (Gemini CLI)
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("forms"), arguments);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("applicant.name")) && H::text(result).contains(QStringLiteral("spouse.name")), qPrintable(H::text(result)));

    // A unique suffix needs no choice
    result = fixture.callTool(QStringLiteral("forms"), H::json(R"({"action":"set_values","set_values":{"values":[{"name":"spouse.name","value":"Bob"}]}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));

    // Modern and legacy clients with elicitation: the input request, then the retry
    for (pdf::PDFMcpEra era : { pdf::PDFMcpEra::Modern, pdf::PDFMcpEra::Legacy20251125 })
    {
        fixture.setClient(QStringLiteral("claude-code"), era, H::json(R"({"elicitation":{}})"));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("forms"), arguments);
        result = fixture.waitResult(call);
        QVERIFY(result);
        QCOMPARE(result->kind, pdf::PDFMcpCallResult::Kind::InputRequired);
        QCOMPARE(result->inputRequests.requests.size(), size_t(1));
        const QByteArray schema = result->inputRequests.requests.front().requestedSchema;
        QVERIFY2(schema.contains("\"oneOf\"") && schema.contains("applicant.name"), schema.constData());

        // The retry with the answer
        pdf::PDFMcpCallRequest request = fixture.createRequest(QStringLiteral("forms"), arguments);
        request.inputResponses = H::json(R"({"pdf4qt.choice":{"action":"accept","content":{"field1":"applicant.name"}}})");
        std::shared_ptr<pdf::PDFMcpCall> retry = pdf::PDFMcpCall::create(request);
        fixture.host()->handleCall(retry);
        result = fixture.waitResult(retry);
        QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
        QCOMPARE(H::fieldValue(tab->getDocument()->getDocument().data(), QStringLiteral("applicant.name")), QStringLiteral("Jane"));

        // A declined choice
        request.inputResponses = H::json(R"({"pdf4qt.choice":{"action":"decline"}})");
        retry = pdf::PDFMcpCall::create(request);
        fixture.host()->handleCall(retry);
        QVERIFY(H::isError(fixture.waitResult(retry)));
    }
}

void AppMcpEditTest::outlineEdits()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Ids of the items
    QCOMPARE(pdf::PDFAppMcpOutlineTool::formatItemId({ 1, 0, 3 }), QStringLiteral("2.1.4"));
    QVERIFY(pdf::PDFAppMcpOutlineTool::parseItemId(QStringLiteral("2.1.4")) == pdf::PDFAppAnnotationOperations::OutlinePath({ 1, 0, 3 }));
    for (const char* invalid : { "", "0", "1.0", "a", "1..2", "-1", "1.2.", "99999999" })
    {
        QVERIFY2(!pdf::PDFAppMcpOutlineTool::parseItemId(QString::fromLatin1(invalid)), invalid);
    }

    // add under "Chapter 1" (2): the new item 2.2
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"add","add":{"parent":"2","title":"Section 1.2","page":4,"zoom":"fit_page"}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::structured(result).value(QStringLiteral("add")).toObject().value(QStringLiteral("item")).toString(), QStringLiteral("2.2"));
    QVERIFY(document->getUndoStack()->getUndoDescription().startsWith(QStringLiteral("Claude Code edited the outline")));
    const QSharedPointer<pdf::PDFOutlineItem> root = document->getDocument()->getCatalog()->getOutlineRootPtr();
    QCOMPARE(root->getChild(1)->getChild(1)->getTitle(), QStringLiteral("Section 1.2"));

    // update by the title: rename and bold
    result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"update","update":{"title":"appendix","newTitle":"Appendix A","bold":true}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(document->getDocument()->getCatalog()->getOutlineRootPtr()->getChild(2)->getTitle(), QStringLiteral("Appendix A"));
    QVERIFY(document->getDocument()->getCatalog()->getOutlineRootPtr()->getChild(2)->isFontBold());

    // move: Appendix A to the first place
    result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"move","move":{"item":"3","index":0}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::structured(result).value(QStringLiteral("move")).toObject().value(QStringLiteral("item")).toString(), QStringLiteral("1"));
    QCOMPARE(document->getDocument()->getCatalog()->getOutlineRootPtr()->getChild(0)->getTitle(), QStringLiteral("Appendix A"));

    // delete with the children
    result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"delete","delete":{"items":["3","3.1"]}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::structured(result).value(QStringLiteral("delete")).toObject().value(QStringLiteral("deleted")).toInt(), 3);

    // replace: a flat list with levels
    result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"replace","replace":{"items":[
        {"title":"One","level":1,"page":1},{"title":"One.One","level":2,"page":2},{"title":"One.One.One","level":3},{"title":"Two","level":1,"page":5}]}})"));
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const QSharedPointer<pdf::PDFOutlineItem> replaced = document->getDocument()->getCatalog()->getOutlineRootPtr();
    QCOMPARE(replaced->getChildCount(), size_t(2));
    QCOMPARE(replaced->getChild(0)->getChild(0)->getChild(0)->getTitle(), QStringLiteral("One.One.One"));

    // Hostile arguments
    for (const char* arguments : { R"({"action":"replace","replace":{"items":[{"title":"A","level":2}]}})",
                                   R"({"action":"replace","replace":{"items":[{"title":"A","level":1},{"title":"B","level":3}]}})",
                                   R"({"action":"replace","replace":{"items":[{"title":"   ","level":1}]}})",
                                   R"({"action":"replace","replace":{"items":[{"title":"A","level":1,"page":99}]}})",
                                   R"({"action":"add","add":{"title":"A"}})",
                                   R"({"action":"add","add":{"title":"A","page":1,"parent":"9"}})",
                                   R"({"action":"update","update":{"item":"9","newTitle":"X"}})",
                                   R"({"action":"update","update":{"title":"nothing of this title","newTitle":"X"}})",
                                   R"({"action":"move","move":{"item":"1","parent":"1.1"}})",
                                   R"({"action":"delete","delete":{"items":["x"]}})" })
    {
        result = fixture.callTool(QStringLiteral("outline"), H::json(arguments));
        QVERIFY2(H::isError(result), arguments);
    }

    // Two items of one title: the choice (elicitation) or the candidates
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"add","add":{"title":"Two","page":2}})"))));
    result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"update","update":{"title":"Two","newTitle":"Three"}})"));
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("Pass item")), qPrintable(H::text(result)));
    fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Modern, H::json(R"({"elicitation":{}})"));
    result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"update","update":{"title":"Two","newTitle":"Three"}})"));
    QVERIFY(result && result->kind == pdf::PDFMcpCallResult::Kind::InputRequired);
}

void AppMcpEditTest::outlineQueuedRebuild()
{
    using H = AppMcpEditTestHelper;

    // Items given by their positions ("3") are not valid any more, when the
    // outline changes while the request waits: the rebuilt request fails, it
    // never changes another item, which has got the position meanwhile
    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QSharedPointer<pdf::PDFOutlineItem> root = document->getDocument()->getCatalog()->getOutlineRootPtr();
    QVERIFY(root && root->getChildCount() == 3);
    QCOMPARE(root->getChild(2)->getTitle(), QStringLiteral("Appendix"));

    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("outline"), H::json(R"({"action":"delete","delete":{"items":["3"]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);

    // The user inserts an item at the top: "3" is "Chapter 1" now
    const pdf::PDFObjectReference page = document->getDocument()->getCatalog()->getPage(0)->getPageReference();
    const pdf::PDFAppAnnotationResult inserted = pdf::PDFAppAnnotationOperations::insertOutlineItem(document->getDocument(), {}, 0, QStringLiteral("Preface"),
                                                                                                      pdf::PDFDestination::createFit(page));
    QVERIFY(inserted);
    QVERIFY(document->commit(*inserted, QStringLiteral("Insert"), document->getVersion(), pdf::PDFAppChangeSource::User));

    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    const std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("changed in the meantime")), qPrintable(H::text(result)));
    const QSharedPointer<pdf::PDFOutlineItem> after = document->getDocument()->getCatalog()->getOutlineRootPtr();
    QCOMPARE(after->getChildCount(), size_t(4));
    QCOMPARE(after->getChild(2)->getTitle(), QStringLiteral("Chapter 1"));
    QCOMPARE(after->getChild(3)->getTitle(), QStringLiteral("Appendix"));

    // A change of annotations keeps the outline: the waiting request is rebuilt and done
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    call = fixture.startCall(QStringLiteral("outline"), H::json(R"({"action":"delete","delete":{"items":["4"]}})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"))));
    QTest::mouseClick(fixture.waitDialog()->getAllowButton(), Qt::LeftButton);
    QVERIFY2(!H::isError(fixture.waitResult(call)), qPrintable(H::text(call->getResult())));
    QCOMPARE(document->getDocument()->getCatalog()->getOutlineRootPtr()->getChildCount(), size_t(3));
}

/// A change job of the test (only its weight is asked)
class McpEditTestChangeJob : public pdf::PDFAppMcpChangeJob
{
public:
    explicit McpEditTestChangeJob(pdf::PDFAppMcpChangeContext context) :
        pdf::PDFAppMcpChangeJob(std::move(context))
    {
    }

protected:
    virtual pdf::PDFAppMcpChange build() override { return pdf::PDFAppMcpChange(); }
    virtual std::shared_ptr<pdf::PDFAppMcpChangeJob> createRerun(pdf::PDFAppMcpChangeContext context) const override
    {
        return std::make_shared<McpEditTestChangeJob>(std::move(context));
    }
};

void AppMcpEditTest::changeJobHeavy()
{
    // A change of many items is a heavy job (at most two run at once), also on a
    // small document; a few items are not (review RC7-H / RC7-E)
    pdf::PDFAppMcpToolInvocation invocation;
    invocation.toolName = QStringLiteral("annotations");
    invocation.actionName = QStringLiteral("add");
    QJsonArray items;
    for (int i = 0; i < 49; ++i)
    {
        items.append(QJsonObject());
    }
    invocation.actionArguments.insert(QStringLiteral("items"), items);
    pdf::PDFAppMcpChangeContext context = pdf::PDFAppMcpChangeJob::createContext(invocation, nullptr);
    QCOMPARE(context.itemCount, 49);
    QVERIFY(!McpEditTestChangeJob(context).isHeavy());

    items.append(QJsonObject());
    invocation.actionArguments.insert(QStringLiteral("items"), items);
    invocation.actionArguments.insert(QStringLiteral("other"), QJsonArray({ 1, 2 }));
    context = pdf::PDFAppMcpChangeJob::createContext(invocation, nullptr);
    QCOMPARE(context.itemCount, 50);
    QVERIFY(McpEditTestChangeJob(context).isHeavy());
}

void AppMcpEditTest::outlineHostile()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(H::hostileOutline(fixture.folder()));
    QVERIFY(tab);

    // A damaged outline is never edited (step 4 rules)
    const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("outline"), H::json(R"({"action":"add","add":{"title":"A","page":1}})"));
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("cannot be edited")), qPrintable(H::text(result)));
}

void AppMcpEditTest::filesRules()
{
    McpEditFixture fixture;
    const QString pdfFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("rules.pdf"), 1);
    QFile notPdf(fixture.path(QStringLiteral("fake.pdf")));
    QVERIFY(notPdf.open(QIODevice::WriteOnly));
    notPdf.write("This is not a PDF file");
    notPdf.close();
    QDir(fixture.folder()).mkdir(QStringLiteral("folder.pdf"));

    using Access = pdf::PDFAppMcpFilesTool::Access;
    QString error;
    QVERIFY(!pdf::PDFAppMcpFilesTool::checkFilePath(pdfFile, Access::Open, fixture.context.get(), &error).isEmpty());
    QVERIFY(!pdf::PDFAppMcpFilesTool::checkFilePath(fixture.path(QStringLiteral("new.pdf")), Access::Write, fixture.context.get(), &error).isEmpty());

    const QStringList refused = {
        QString(),
        QStringLiteral("relative/file.pdf"),
        QStringLiteral("\\\\server\\share\\file.pdf"),
        QStringLiteral("//server/share/file.pdf"),
        QStringLiteral("\\\\?\\C:\\file.pdf"),
        QStringLiteral("\\\\.\\PhysicalDrive0"),
        fixture.path(QStringLiteral("file.pdf:stream")),
        fixture.path(QStringLiteral("CON")),
        fixture.path(QStringLiteral("nul.pdf")),
        fixture.path(QStringLiteral("missing/file.pdf")),
        fixture.path(QStringLiteral("folder.pdf")),
        fixture.path(QStringLiteral("fake.pdf")),
        fixture.path(QStringLiteral("does-not-exist.pdf")),
        QString(5000, QLatin1Char('a')),
    };
    for (const QString& path : refused)
    {
        QVERIFY2(pdf::PDFAppMcpFilesTool::checkFilePath(path, Access::Open, fixture.context.get(), &error).isEmpty(), qPrintable(path.left(80)));
        QVERIFY2(!error.isEmpty(), qPrintable(path.left(80)));
    }

    // A path of another computer is refused by its name, before anything is resolved
    // on the disk (review RT7: the check of the canonical path alone must not be the
    // only one, it touches the network)
    for (const QString& path : { QStringLiteral("\\\\server\\share\\file.pdf"), QStringLiteral("//server/share/file.pdf") })
    {
        QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(path, Access::Open, fixture.context.get(), &error).isEmpty());
        QVERIFY2(error.contains(QStringLiteral("paths of devices and of network locations are refused")), qPrintable(error));
    }

    // Writes: .pdf only, not into a folder, not a folder
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(fixture.path(QStringLiteral("copy.txt")), Access::Write, fixture.context.get(), &error).isEmpty());
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(fixture.path(QStringLiteral("folder.pdf")), Access::Write, fixture.context.get(), &error).isEmpty());

    // The folder of the logs of PDF4QT
    const QString logDirectory = pdf::PDFAppLog::getLogDirectory();
    if (!logDirectory.isEmpty() && QDir(logDirectory).exists())
    {
        QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(QDir(logDirectory).filePath(QStringLiteral("x.pdf")), Access::Write, fixture.context.get(), &error).isEmpty());
    }
}

void AppMcpEditTest::filesOpen()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("open.pdf"), 3));
    QVERIFY(tab);

    // A file open already: Read only, no question; its page
    QJsonObject open;
    open.insert(QStringLiteral("action"), QStringLiteral("open"));
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), tab->getDocument()->getFilePath() }, { QStringLiteral("page"), 3 } }));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("files"), open);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(H::structured(result).value(QStringLiteral("open")).toObject().value(QStringLiteral("documentId")).toString(), tab->getDocument()->getId());
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(2));
    QVERIFY(fixture.ui()->getWaitingIds().empty());

    // A new file with Ask: the open dialog (940:136305), Open
    const QString newFile = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Q3_Forecast.pdf"), 2);
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), newFile } }));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), open);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to open a document"));
    QCOMPARE(dialog->getAllowButton()->text(), QStringLiteral("Open"));
    QVERIFY(dialog->getRowTexts().contains(QStringLiteral("Q3_Forecast.pdf")));
    // The card 520 inside of the tab: the content at the padding 22 inside of the
    // border (941:135714 at 23 - no shadow margins of a window)
    QTRY_VERIFY(dialog->isVisible());
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(dialog->getHeadWidget()->mapTo(dialog, QPoint(0, 0)).x(), 23);
    QCOMPARE(dialog->width(), 520);
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-open"));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    result = fixture.waitResult(call);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    const QJsonObject opened = H::structured(result).value(QStringLiteral("open")).toObject();
    QCOMPARE(opened.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QCOMPARE(opened.value(QStringLiteral("pageCount")).toInt(), 2);
    QVERIFY(result->toQJson().value(QStringLiteral("content")).toArray().size() >= 2);   // the resource link

    // activate = false keeps the current tab (Files = Allow: no dialog)
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppTab* current = fixture.window->getCurrentTab();
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("background.pdf"), 1) },
                                                      { QStringLiteral("activate"), false } }));
    result = fixture.callTool(QStringLiteral("files"), open);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(fixture.window->getCurrentTab(), current);

    // A password: the call waits for the password screen of the tab (here no
    // password is typed - password_required). The time of the user starts with
    // the password screen, not with the request: the reading of the file (longer
    // than 1 ms, seconds under valgrind) never answers "loading" (VG7)
    pdf::PDFAppMcpFilesTool::setPasswordTimeout(1);
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), pdftest::AppDocumentSamples::userPassword(fixture.folder()) } }));
    result = fixture.callTool(QStringLiteral("files"), open);
    pdf::PDFAppMcpFilesTool::setPasswordTimeout(pdf::PDFAppMcpFilesTool::DEFAULT_PASSWORD_TIMEOUT);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("password_required")), qPrintable(H::text(result)));
    QVERIFY2(H::text(result).contains(QStringLiteral("within 2 minutes")), qPrintable(H::text(result)));

    // A call canceled while the password screen waits ends at once with its
    // activity row - not after the 2 minutes of the user (review RVG7)
    const QString passwordCopy = QDir(fixture.folder()).filePath(QStringLiteral("password-cancel.pdf"));
    QVERIFY(QFile::copy(pdftest::AppDocumentSamples::userPassword(fixture.folder()), passwordCopy));
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), passwordCopy } }));
    call = fixture.startCall(QStringLiteral("files"), open);
    QVERIFY(call);
    auto isPasswordScreen = [&fixture, &passwordCopy]()
    {
        pdf::PDFAppDocument* document = fixture.context->getDocumentRegistry()->findByCanonicalPath(QFileInfo(passwordCopy).canonicalFilePath());
        return document && document->getState() == pdf::PDFAppDocumentState::PasswordRequired;
    };
    QTRY_VERIFY_WITH_TIMEOUT(isPasswordScreen(), 30000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!call->isFinished());
    call->cancel(pdf::PDFMcpCancelReason::StreamClosed);
    result = fixture.waitResult(call, 5000);
    QVERIFY(call->isFinished());
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("canceled")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::Canceled));

    // Refused paths
    open.insert(QStringLiteral("open"), QJsonObject({ { QStringLiteral("path"), QStringLiteral("\\\\server\\share\\a.pdf") } }));
    QVERIFY(H::isError(fixture.callTool(QStringLiteral("files"), open)));
}

void AppMcpEditTest::reviewFilesSaveRefusals()
{
    // Review RFW7-M (coverage of the files tool): save_copy of a relative path,
    // save of a file, which cannot be written, and a save confirmed while the
    // document was locked or became busy - errors with their activity status,
    // nothing written
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    const QString path = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refusals.pdf"), 1);
    pdf::PDFAppDocumentTab* tab = fixture.openReady(path);
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"))));
    QVERIFY(document->isModified());
    const QByteArray original = [&path]() { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }();
    QVERIFY(!original.isEmpty());

    // save_copy: a relative path
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("files"), H::json(R"({"action":"save_copy","save_copy":{"path":"copy.pdf"}})"));
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).startsWith(QStringLiteral("path: ")), qPrintable(H::text(result)));

    // save: a read-only file
    const QFileDevice::Permissions permissions = QFile::permissions(path);
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser));
    result = fixture.callTool(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    QVERIFY(QFile::setPermissions(path, permissions));
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("cannot be written")), qPrintable(H::text(result)));

    // save confirmed after the document was locked meanwhile
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    document->setLocked(true);
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("locked")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedLocked));
    document->setLocked(false);

    // ... and after an operation started meanwhile
    call = fixture.startCall(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QPointer<pdf::PDFAppDocumentOperation> operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Edit, QStringLiteral("Test"), true);
    QVERIFY(operation);
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("busy")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedBusy));
    operation->finish();

    // Nothing was written
    QVERIFY(document->isModified());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
}

void AppMcpEditTest::reviewChangeRefusedAfterConfirmation()
{
    // Review RFW7-M (coverage of PDFAppMcpChangeJob::proceed / commitChange): a
    // change allowed by the user, while the document became locked, changed and
    // busy, or only busy, is refused with its status - nothing is committed
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Refused.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QJsonObject add = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");

    // Locked meanwhile
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    document->setLocked(true);
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("locked")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedLocked));
    document->setLocked(false);

    // Changed by the user and busy meanwhile: no new build on the current version
    call = fixture.startCall(QStringLiteral("annotations"), add);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), document->getOptionalContentActivity(), pdf::PDFModifiedDocument::Reset),
                             QStringLiteral("User change"), document->getVersion(), pdf::PDFAppChangeSource::User));
    QPointer<pdf::PDFAppDocumentOperation> operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Edit, QStringLiteral("Test"), true);
    QVERIFY(operation);
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("busy")), qPrintable(H::text(result)));
    QVERIFY(fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedBusy));
    operation->finish();
    QTRY_VERIFY(!document->isBlockedByOperation());

    // Only busy meanwhile: the commit is refused
    const int undoSteps = pdftest::AppDocumentFixture::undoCount(document);
    call = fixture.startCall(QStringLiteral("annotations"), add);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    operation = document->beginOperation(pdf::PDFAppDocumentOperationKind::Edit, QStringLiteral("Test"), true);
    QVERIFY(operation);
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("busy")), qPrintable(H::text(result)));
    operation->finish();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoSteps);
}

void AppMcpEditTest::filesSave()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"))));
    QVERIFY(document->isModified());

    // Saving over the file is always confirmed, also with Allow (940:134248):
    // Save Secondary at the left, Deny Primary at the right
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to save Project_Report.pdf"));
    QCOMPARE(dialog->getAllowButton()->text(), QStringLiteral("Save"));
    QVERIFY(dialog->getAllowButton()->geometry().left() < dialog->getDenyButton()->geometry().left());
    QVERIFY(dialog->getDenyButton()->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());
    QVERIFY(!dialog->getAllowButton()->property(pdf::PDFWidgetStyleProperties::PRIMARY).toBool());
    QVERIFY(dialog->getNoteTexts().contains(QStringLiteral("The previous content of the file is replaced. It cannot be restored from the disk.")));
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-save"));

    // Deny: nothing written
    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(H::isError(fixture.waitResult(call)));
    QVERIFY(document->isModified());

    // Save: written, the document is saved, the status message
    call = fixture.startCall(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    const std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(H::structured(result).value(QStringLiteral("save")).toObject().value(QStringLiteral("saved")).toBool());
    QTRY_VERIFY(!document->isModified());
    QCOMPARE(tab->getView()->getStatusBar()->getMessage(), QStringLiteral("Project_Report.pdf saved"));

    // A copy without a file (Edit a Copy) cannot be saved over its file
    pdf::PDFAppDocumentTab* copy = fixture.window->openDocumentCopy(document);
    QVERIFY(copy);
    QTRY_VERIFY(copy->getDocument()->isReady());
    QJsonObject save = H::json(R"({"action":"save"})");
    save.insert(QStringLiteral("documentId"), copy->getDocument()->getId());
    const std::optional<pdf::PDFMcpCallResult> copyResult = fixture.callTool(QStringLiteral("files"), save);
    QVERIFY(H::isError(copyResult));
    QVERIFY(H::text(copyResult).contains(QStringLiteral("save_copy")));
}

void AppMcpEditTest::filesSaveCopy()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);

    // Ask: 958:27539, Deny Secondary, Save Copy Primary
    QJsonObject copy;
    copy.insert(QStringLiteral("action"), QStringLiteral("save_copy"));
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), fixture.path(QStringLiteral("Report_reviewed.pdf")) } }));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), copy);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("Claude Code wants to save a copy as Report_reviewed.pdf"));
    QCOMPARE(dialog->getAllowButton()->text(), QStringLiteral("Save Copy"));
    QVERIFY(dialog->getDenyButton()->geometry().left() < dialog->getAllowButton()->geometry().left());
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Asked because Open and save files is set to Ask"));
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-save-copy"));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY2(!H::isError(fixture.waitResult(call)), qPrintable(H::text(call->getResult())));
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("Report_reviewed.pdf"))));
    QVERIFY(!tab->getDocument()->isModified());

    // An existing file: refused without overwrite, always confirmed with it
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Allow));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("files"), copy);
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("overwrite")));
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), fixture.path(QStringLiteral("Report_reviewed.pdf")) }, { QStringLiteral("overwrite"), true } }));
    call = fixture.startCall(QStringLiteral("files"), copy);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getReasonText(), QStringLiteral("Overwriting the original file is always confirmed"));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(call)));

    // The own file or a file open in PDF4QT is refused
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), tab->getDocument()->getFilePath() }, { QStringLiteral("overwrite"), true } }));
    QVERIFY(H::isError(fixture.callTool(QStringLiteral("files"), copy)));

    // Without a path: the client is asked for it (a default next to the file)
    copy.insert(QStringLiteral("save_copy"), QJsonObject());
    result = fixture.callTool(QStringLiteral("files"), copy);
    QVERIFY(H::isError(result));
    QVERIFY(H::text(result).contains(QStringLiteral("needs path")));
    fixture.setClient(QStringLiteral("claude-code"), pdf::PDFMcpEra::Modern, H::json(R"({"elicitation":{}})"));
    result = fixture.callTool(QStringLiteral("files"), copy);
    QVERIFY(result && result->kind == pdf::PDFMcpCallResult::Kind::InputRequired);
    QVERIFY(result->inputRequests.requests.front().requestedSchema.contains("Project_Report_copy.pdf"));

    // The retry with the chosen path writes the copy (Allow, a new file: no dialog)
    pdf::PDFMcpCallRequest request = fixture.createRequest(QStringLiteral("files"), copy);
    QJsonObject content;
    content.insert(QStringLiteral("path"), fixture.path(QStringLiteral("chosen.pdf")));
    request.inputResponses = QJsonObject({ { QStringLiteral("pdf4qt.choice"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), content } }) } });
    std::shared_ptr<pdf::PDFMcpCall> retry = pdf::PDFMcpCall::create(request);
    fixture.host()->handleCall(retry);
    QVERIFY2(!H::isError(fixture.waitResult(retry)), qPrintable(H::text(retry->getResult())));
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("chosen.pdf"))));
}

void AppMcpEditTest::filesPathLinksAndFolders()
{
    // Review RW7-0: the folder of the database, the folder of the logs and the
    // symbolic links (plan 7) - the common rules of PDFAppMcpArguments::checkPath
    using Access = pdf::PDFAppMcpFilesTool::Access;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir(directory.path()).mkdir(QStringLiteral("data"));
    QDir(directory.path()).mkdir(QStringLiteral("work"));
    const QString dataFolder = directory.filePath(QStringLiteral("data"));
    const QString workFolder = directory.filePath(QStringLiteral("work"));

    pdf::PDFDatabaseOptions options;
    options.filePath = QDir(dataFolder).filePath(QStringLiteral("pdf4qt-1.db"));
    pdf::PDFAppContext context(pdf::PDFAppCommandLine::parse({ QStringLiteral("PDF4QT") }, QDir::currentPath()));
    context.open(options);
    QCOMPARE(context.getStorageState().mode, pdf::PDFDatabaseStorageMode::Disk);

    // The folder of the database (and the database file itself), not without the context
    QString error;
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(QDir(dataFolder).filePath(QStringLiteral("x.pdf")), true, &context, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("folder of the data")), qPrintable(error));
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(QDir(dataFolder).filePath(QStringLiteral("x.pdf")), Access::Write, &context, &error).isEmpty());
    QVERIFY(pdf::PDFAppMcpArguments::checkPath(options.filePath, false, &context, &error).isEmpty());
    QVERIFY(!pdf::PDFAppMcpArguments::checkPath(QDir(dataFolder).filePath(QStringLiteral("x.pdf")), true, nullptr, &error).isEmpty());
    QVERIFY(!pdf::PDFAppMcpArguments::checkPath(QDir(workFolder).filePath(QStringLiteral("x.pdf")), true, &context, &error).isEmpty());

    // The folder of the logs (also without the context)
    const QString logDirectory = pdf::PDFAppLog::getLogDirectory();
    if (!logDirectory.isEmpty() && QDir(logDirectory).exists())
    {
        QVERIFY(pdf::PDFAppMcpArguments::checkPath(QDir(logDirectory).filePath(QStringLiteral("x.pdf")), true, nullptr, &error).isEmpty());
    }

    // isInFolder: the folder itself and its files, not a sibling with the same prefix
    const QString canonicalData = QFileInfo(dataFolder).canonicalFilePath();
    QVERIFY(pdf::PDFAppMcpArguments::isInFolder(canonicalData, dataFolder));
    QVERIFY(pdf::PDFAppMcpArguments::isInFolder(canonicalData + QStringLiteral("/a/b.pdf"), dataFolder));
    QVERIFY(!pdf::PDFAppMcpArguments::isInFolder(canonicalData + QStringLiteral("2/b.pdf"), dataFolder));
    QVERIFY(!pdf::PDFAppMcpArguments::isInFolder(canonicalData, QString()));
    QVERIFY(!pdf::PDFAppMcpArguments::isInFolder(QString(), dataFolder));
    QVERIFY(!pdf::PDFAppMcpArguments::isInFolder(canonicalData, directory.filePath(QStringLiteral("missing"))));

    // Symbolic links (Windows: only with the privilege / developer mode - skipped otherwise)
    const QString pdfFile = pdftest::AppDocumentSamples::simple(workFolder, QStringLiteral("target.pdf"), 1);
    std::error_code code;
    const std::filesystem::path work = QDir::toNativeSeparators(workFolder).toStdWString();
    std::filesystem::create_directory_symlink(std::filesystem::path(QDir::toNativeSeparators(dataFolder).toStdWString()), work / L"to-data", code);
    if (code)
    {
        QSKIP("Symbolic links cannot be created here (Windows without the privilege)");
    }

    // A link anywhere in the path to a refused place
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(QDir(workFolder).filePath(QStringLiteral("to-data/x.pdf")), Access::Write, &context, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("folder of the data")), qPrintable(error));

    // A dangling link as the target: never followed (it would create its target)
    std::filesystem::create_symlink(std::filesystem::path(QDir::toNativeSeparators(QDir(dataFolder).filePath(QStringLiteral("created.pdf"))).toStdWString()), work / L"dangling.pdf", code);
    QVERIFY(!code);
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(QDir(workFolder).filePath(QStringLiteral("dangling.pdf")), Access::Write, &context, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("symbolic link")), qPrintable(error));
    std::filesystem::create_symlink(work / L"nowhere" / L"missing.pdf", work / L"dangling2.pdf", code);
    QVERIFY(!code);
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(QDir(workFolder).filePath(QStringLiteral("dangling2.pdf")), Access::Write, nullptr, &error).isEmpty());

    // A .pdf link to a file of another kind is refused for a write
    {
        QFile text(QDir(workFolder).filePath(QStringLiteral("notes.txt")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("text");
    }
    std::filesystem::create_symlink(work / L"notes.txt", work / L"notes.pdf", code);
    QVERIFY(!code);
    QVERIFY(pdf::PDFAppMcpFilesTool::checkFilePath(QDir(workFolder).filePath(QStringLiteral("notes.pdf")), Access::Write, nullptr, &error).isEmpty());

    // A link to a valid PDF file: the resolved path is returned
    std::filesystem::create_symlink(std::filesystem::path(QDir::toNativeSeparators(pdfFile).toStdWString()), work / L"alias.pdf", code);
    QVERIFY(!code);
    const QString resolved = pdf::PDFAppMcpFilesTool::checkFilePath(QDir(workFolder).filePath(QStringLiteral("alias.pdf")), Access::Open, nullptr, &error);
    QCOMPARE(resolved, QFileInfo(pdfFile).canonicalFilePath());
}

void AppMcpEditTest::filesSaveCopyTargetAppeared()
{
    // Review RW7-0: the target of save_copy is checked again after the
    // confirmation (it can wait 2 minutes) - a file, which appeared meanwhile, is
    // never replaced unless the overwrite was confirmed
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Files, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("source.pdf"), 1));
    QVERIFY(tab);

    const QString copyPath = fixture.path(QStringLiteral("appeared.pdf"));
    QJsonObject copy;
    copy.insert(QStringLiteral("action"), QStringLiteral("save_copy"));
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), copyPath } }));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), copy);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);

    const QByteArray userData = QByteArrayLiteral("%PDF-1.7 the file of the user");
    {
        QFile file(copyPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(userData), qint64(userData.size()));
    }
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("exists already")), qPrintable(H::text(result)));
    {
        QFile file(copyPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), userData);
    }

    // A dangling link, which appeared meanwhile, is not followed
    const QString linkPath = fixture.path(QStringLiteral("link.pdf"));
    const QString linkTarget = fixture.path(QStringLiteral("link-target.pdf"));
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), linkPath } }));
    call = fixture.startCall(QStringLiteral("files"), copy);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    std::error_code code;
    std::filesystem::create_symlink(std::filesystem::path(QDir::toNativeSeparators(linkTarget).toStdWString()), std::filesystem::path(QDir::toNativeSeparators(linkPath).toStdWString()), code);
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    result = fixture.waitResult(call);
    if (code)
    {
        // No link here (Windows without the privilege): the copy is written
        QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
        return;
    }
    QVERIFY(H::isError(result));
    QVERIFY(!QFileInfo::exists(linkTarget));
    QVERIFY(QFileInfo(linkPath).isSymLink());
}

void AppMcpEditTest::filesPendingEditsAndSavedAs()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // A form field being typed in the tab: save_copy writes the typed value (as
    // Save of the user commits the open editor first)
    QVERIFY(pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(100, 109)));
    pdftest::AppDocumentFixture::settle();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(tab->getView());
    QTest::keyClick(canvas, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(canvas, QStringLiteral("Typed"));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(document->hasPendingEdits());

    const QString copyPath = fixture.path(QStringLiteral("typed-copy.pdf"));
    QJsonObject copy;
    copy.insert(QStringLiteral("action"), QStringLiteral("save_copy"));
    copy.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), copyPath } }));
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("files"), copy);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(!document->hasPendingEdits());
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocument saved = reader.readFromFile(copyPath);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    QCOMPARE(H::fieldValue(&saved, QStringLiteral("name")), QStringLiteral("Typed"));

    // The user saves the document as another file while the save waits for its
    // confirmation: the confirmed file is not written, the tab keeps the new file
    const QString originalPath = document->getFilePath();
    const QByteArray originalBytes = [&originalPath]() { QFile file(originalPath); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }();
    QVERIFY(!originalBytes.isEmpty());
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("files"), H::json(R"({"action":"save"})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    const QString movedPath = fixture.path(QStringLiteral("moved.pdf"));
    QVERIFY(QFile::copy(originalPath, movedPath));
    document->setSavedAs(document->getVersion(), movedPath, originalBytes);
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    result = fixture.waitResult(call);
    QVERIFY(H::isError(result));
    QVERIFY2(H::text(result).contains(QStringLiteral("another file")), qPrintable(H::text(result)));
    QCOMPARE(document->getFilePath(), movedPath);
    QFile original(originalPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes);
}

void AppMcpEditTest::deletionMatrix()
{
    using H = AppMcpEditTestHelper;
    const QJsonObject add = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");

    // The tab closed with the dialog open: denied_closed
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1));
        QVERIFY(tab);
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        QVERIFY(fixture.waitDialog());
        QVERIFY(fixture.window->closeTab(tab));
        const std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
        QVERIFY(H::isError(result));
        if (!fixture.waitActivity(pdf::PDFMcpActivityStatus::DeniedClosed, 3000))
        {
            for (const pdf::PDFMcpActivityRow& row : fixture.activity())
            {
                qWarning() << "activity" << row.action << int(row.status) << H::text(result);
            }
            QFAIL("no denied_closed row");
        }
    }

    // The tab closed in the preview, with the notification bar, with a job running
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1));
        QVERIFY(tab);
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
        QVERIFY(dialog && dialog->getShowOnPageLink());
        QTest::mouseClick(dialog->getShowOnPageLink(), Qt::LeftButton);
        QTRY_VERIFY(fixture.ui()->getPreviewBar());
        QVERIFY(fixture.window->closeTab(tab));
        QVERIFY(H::isError(fixture.waitResult(call)));
        QTRY_VERIFY(!fixture.ui()->getPreviewBar());

        tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("c.pdf"), 1));
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Allow));
        QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), add)));
        QVERIFY(fixture.ui()->findNotificationBar(tab->getDocument()));
        tab->getDocument()->setSaved();
        QVERIFY(fixture.window->closeTab(tab));
        pdftest::AppDocumentFixture::settle();

        // A job, whose tab closes before it ends
        tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("d.pdf"), 1));
        call = fixture.startCall(QStringLiteral("annotations"), add);
        tab->getDocument()->setSaved();
        QVERIFY(fixture.window->closeTab(tab));

        // The worker may have ended before the tab closed (committed) or not (closed)
        QVERIFY(fixture.waitResult(call));
        pdftest::AppDocumentFixture::settle();
    }

    // The server stops (quit) with a dialog open: denied_stopped
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        QVERIFY(fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("e.pdf"), 1)));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        QVERIFY(fixture.waitDialog());
        fixture.host()->shutdown();
        const std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
        QVERIFY(H::isError(result));
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!fixture.ui()->getDialog());
    }

    // The window deleted with a dialog, a banner and a status item
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        pdf::PDFAppDocumentTab* first = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("f.pdf"), 1));
        QVERIFY(fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("g.pdf"), 1)));
        QJsonObject arguments = add;
        arguments.insert(QStringLiteral("documentId"), first->getDocument()->getId());
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), arguments);
        QTRY_VERIFY(!fixture.ui()->getWaitingIds().empty());
        fixture.window->activateTab(fixture.window->getHomeTab());
        QTRY_VERIFY(fixture.ui()->findBanner(fixture.window->getHomeTab()));
        delete fixture.window.data();
        pdftest::AppDocumentFixture::settle();
        QVERIFY(H::isError(fixture.waitResult(call)));
    }

    // The interface deleted with a dialog open: the host has no provider
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        QVERIFY(fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("h.pdf"), 1)));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        QVERIFY(fixture.waitDialog());
        delete fixture.ui();
        QVERIFY(!fixture.host()->getConfirmationProvider());
        pdftest::AppDocumentFixture::settle();
        fixture.host()->shutdown();
        QVERIFY(H::isError(fixture.waitResult(call)));
    }
}

void AppMcpEditTest::deleteFromOwnSignals()
{
    using H = AppMcpEditTestHelper;
    const QJsonObject add = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");

    // The tab deleted from the signal allowed of the dialog
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
        QVERIFY(dialog);
        QObject::connect(dialog, &pdf::PDFAppMcpConfirmDialog::allowed, fixture.window, [&fixture, tab]() { fixture.window->closeTab(tab); });
        QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
        QVERIFY(fixture.waitResult(call));
        pdftest::AppDocumentFixture::settle();
    }

    // The window deleted from the signal denied
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        QVERIFY(fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1)));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
        QVERIFY(dialog);
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        QObject::connect(dialog, &pdf::PDFAppMcpConfirmDialog::denied, window, [window]() { delete window.data(); });
        QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
        QVERIFY(H::isError(fixture.waitResult(call)));
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!window);
    }

    // The tab deleted from the signals of the notification bar
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("c.pdf"), 1));
        QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), add)));
        pdf::PDFAppMcpNotificationBar* bar = fixture.ui()->findNotificationBar(tab->getDocument());
        QVERIFY(bar);
        QObject::connect(bar, &pdf::PDFAppMcpNotificationBar::showChangesRequested, fixture.window, [&fixture, tab]() { tab->getDocument()->setSaved(); fixture.window->closeTab(tab); });
        QTest::mouseClick(bar->getShowChangesButton(), Qt::LeftButton);
        pdftest::AppDocumentFixture::settle();
        QVERIFY(!tab);
    }

    // The tab deleted from the signal of the preview bar
    {
        McpEditFixture fixture;
        QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
        QPointer<pdf::PDFAppDocumentTab> tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("d.pdf"), 1));
        std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
        pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
        QVERIFY(dialog);
        QTest::mouseClick(dialog->getShowOnPageLink(), Qt::LeftButton);
        QTRY_VERIFY(fixture.ui()->getPreviewBar());
        QObject::connect(fixture.ui()->getPreviewBar(), &pdf::PDFAppMcpPreviewBar::denied, fixture.window, [&fixture, tab]() { fixture.window->closeTab(tab); });
        QTest::mouseClick(fixture.ui()->getPreviewBar()->getDenyButton(), Qt::LeftButton);
        QVERIFY(H::isError(fixture.waitResult(call)));
        pdftest::AppDocumentFixture::settle();
    }
}

void AppMcpEditTest::themesAndScales()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdftest::AppDocumentFixture::settle();
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QCOMPARE(H::countLeakPixels(H::render(dialog, ratio)), 0);
            QCOMPARE(H::countLeakPixels(H::render(fixture.ui()->getScrim(), ratio)), 0);
        }
        H::saveScreenshot(tab, scheme == pdf::PDFThemeColors::Scheme::Dark ? QStringLiteral("mcp-confirm-tab-dark") : QStringLiteral("mcp-confirm-tab-light"));
    }

    // The bars in both themes
    QTest::mouseClick(dialog->getShowOnPageLink(), Qt::LeftButton);
    QTRY_VERIFY(fixture.ui()->getPreviewBar());
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QCOMPARE(H::countLeakPixels(H::render(fixture.ui()->getPreviewBar(), ratio)), 0);
        }
    }
    fixture.ui()->answer(fixture.ui()->getPreviewBar()->getConfirmationId(), pdf::PDFAppMcpDecision::Allowed);
    QVERIFY(!H::isError(fixture.waitResult(call)));
    pdf::PDFAppMcpNotificationBar* bar = fixture.ui()->findNotificationBar(tab->getDocument());
    QVERIFY(bar);
    for (qreal ratio : { 1.0, 1.5, 2.0 })
    {
        QCOMPARE(H::countLeakPixels(H::render(bar, ratio)), 0);
    }
}

void AppMcpEditTest::rightToLeft()
{
    using H = AppMcpEditTestHelper;

    QApplication::setLayoutDirection(Qt::RightToLeft);
    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    QVERIFY(fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("rtl.pdf"), 1)));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);

    // Mirrored: Allow at the left of Deny, the countdown at the right
    QVERIFY(dialog->getAllowButton()->geometry().left() < dialog->getDenyButton()->geometry().left());
    QWidget* countdown = dialog->findChild<QWidget*>(QStringLiteral("countdown"));
    QVERIFY(countdown && countdown->geometry().left() > dialog->getDenyButton()->geometry().left());
    H::saveScreenshot(dialog, QStringLiteral("mcp-confirm-rtl"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::minimalWindow()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture(QSize(720, 480));
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("small.pdf"), 1));
    QVERIFY(tab);
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"),
        H::json(R"({"action":"add","add":{"items":[{"type":"highlight","page":1,"quads":[[30,365,120,365,30,350,120,350]]},{"type":"note","page":1,"point":{"x":200,"y":380}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);

    // The card inside of the tab, covered by the scrim
    QTRY_VERIFY(fixture.ui()->getScrim()->rect().contains(dialog->geometry()));
    QCOMPARE(fixture.ui()->getScrim()->size(), tab->size());
    QCOMPARE(dialog->width(), pdf::PDFAppMcpConfirmDialog::CARD_WIDTH);
    H::saveScreenshot(tab, QStringLiteral("mcp-confirm-720"));

    // The preview bar inside of the canvas
    QTest::mouseClick(dialog->getShowOnPageLink(), Qt::LeftButton);
    QTRY_VERIFY(fixture.ui()->getPreviewBar());
    QTRY_VERIFY2(fixture.ui()->getPreviewBar()->getCardGeometry().right() <= tab->getView()->getCanvasArea()->width(),
                 qPrintable(QStringLiteral("card %1-%2, canvas %3").arg(fixture.ui()->getPreviewBar()->getCardGeometry().left()).arg(fixture.ui()->getPreviewBar()->getCardGeometry().right())
                                .arg(tab->getView()->getCanvasArea()->width())));
    fixture.ui()->answer(fixture.ui()->getPreviewBar()->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::keyboardAndAccessibility()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("keys.pdf"), 1));
    QVERIFY(tab);
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->accessibleName(), dialog->getTitleText());

    // Tab stays inside of the dialog (modal for its tab)
    for (int i = 0; i < 6; ++i)
    {
        QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : dialog, Qt::Key_Tab);
        QWidget* focus = QApplication::focusWidget();
        QVERIFY2(!focus || focus == dialog || dialog->isAncestorOf(focus), focus ? qPrintable(focus->objectName()) : "none");
    }

    // Space on the focused Allow
    while (QApplication::focusWidget() != dialog->getAllowButton())
    {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    }
    QTest::keyClick(dialog->getAllowButton(), Qt::Key_Space);
    QVERIFY(!H::isError(fixture.waitResult(call)));

    // The mouse does not reach the tab below the scrim
    call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":200}}]}})"));
    QVERIFY(fixture.waitDialog());
    QWidget* scrim = fixture.ui()->getScrim();
    QVERIFY(scrim->isVisible());
    QCOMPARE(tab->childAt(QPoint(5, 5)), scrim);
    fixture.ui()->answer(fixture.ui()->getDialog()->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::dialogBlocksDocumentShortcuts()
{
    using H = AppMcpEditTestHelper;

    // The dialog is modal for its tab: the keys of the commands of the document
    // (Ctrl+Z) do not change the document below the scrim
    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("shortcuts.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(!H::isError(fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"))));
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(document);
    QCOMPARE(undoBefore, 1);

    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":200}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    const quint64 version = document->getVersion();
    QWidget* focus = QApplication::focusWidget() ? QApplication::focusWidget() : dialog;
    QTest::keyClick(focus, Qt::Key_Z, Qt::ControlModifier);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore);
    QCOMPARE(document->getVersion(), version);
    QVERIFY(fixture.ui()->getDialog());

    // Without the dialog the same key undoes
    fixture.ui()->answer(fixture.ui()->getDialog()->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(!fixture.ui()->getDialog());
    pdf::PDFAppDocumentView* view = tab->getView();
    QWidget* canvas = pdftest::AppDocumentFixture::canvasWidget(view);
    canvas->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(document), undoBefore - 1);
}

void AppMcpEditTest::languageChange()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    QVERIFY(fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("language.pdf"), 1)));
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);

    McpEditTestTranslator* translator = new McpEditTestTranslator(qApp);
    QCoreApplication::installTranslator(translator);
    pdftest::AppDocumentFixture::settle();
    QVERIFY2(dialog->getAllowButton()->text().startsWith(QChar(0x00A7)), qPrintable(dialog->getAllowButton()->text()));
    QVERIFY(dialog->getReasonText().startsWith(QChar(0x00A7)));
    QVERIFY(dialog->getCountdownText().startsWith(QChar(0x00A7)));
    QCoreApplication::removeTranslator(translator);
    delete translator;
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(dialog->getAllowButton()->text(), QStringLiteral("Allow"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::httpEndToEnd()
{
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(tab);
    if (!fixture.startServer())
    {
        QSKIP("The MCP server does not start");
    }
    fixture.setTransport(pdftest::AppMcpTransport::Http);

    // The dispatcher of package A answers the tools over HTTP
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("annotations"), H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})"));
    QVERIFY(result);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));

    for (const char* tool : { "forms", "outline", "files" })
    {
        const QJsonObject arguments = QString::fromLatin1(tool) == QLatin1String("forms") ? H::json(R"({"action":"reset"})")
                                    : QString::fromLatin1(tool) == QLatin1String("outline") ? H::json(R"({"action":"add","add":{"title":"HTTP","page":1}})")
                                                                                            : H::json(R"({"action":"save_copy","save_copy":{"path":""}})");
        QJsonObject finalArguments = arguments;
        if (QString::fromLatin1(tool) == QLatin1String("files"))
        {
            finalArguments.insert(QStringLiteral("save_copy"), QJsonObject({ { QStringLiteral("path"), fixture.path(QStringLiteral("http-copy.pdf")) } }));
        }
        result = fixture.callTool(QString::fromLatin1(tool), finalArguments);
        QVERIFY2(result.has_value(), tool);
        QVERIFY2(result->kind == pdf::PDFMcpCallResult::Kind::Complete, tool);
    }
    QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("http-copy.pdf"))));

    // The choice of an ambiguous field answered by a real client of each era:
    // MRTR inputRequests + the retry (modern), elicitation/create on the stream (legacy)
    pdf::PDFAppDocumentTab* formTab = fixture.openReady(H::ambiguousForm(fixture.folder()));
    QVERIFY(formTab);
    const QString documentId = formTab->getDocument()->getId();
    const struct
    {
        pdftest::McpTestEra era;
        const char* choice;
        const char* value;
    } eraCases[] = { { pdftest::McpTestEra::Modern, "applicant.name", "Jane" }, { pdftest::McpTestEra::Legacy20251125, "spouse.name", "Bob" } };
    for (const auto& eraCase : eraCases)
    {
        std::unique_ptr<pdftest::McpTestClient> client = fixture.client(eraCase.era);
        client->setCapabilities(H::json(R"({"elicitation":{}})"));
        if (eraCase.era != pdftest::McpTestEra::Modern)
        {
            QVERIFY(!client->initialize().isEmpty());
        }
        int asked = 0;
        const QString choice = QString::fromLatin1(eraCase.choice);
        client->setElicitationResponder([&asked, choice](const QJsonObject& params)
        {
            ++asked;
            const QByteArray schema = QJsonDocument(params.value(QStringLiteral("requestedSchema")).toObject()).toJson(QJsonDocument::Compact);
            if (!schema.contains("applicant.name") || !schema.contains("spouse.name"))
            {
                return QJsonObject({ { QStringLiteral("action"), QStringLiteral("decline") } });
            }
            return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("field1"), choice } }) } });
        });
        QJsonObject arguments = H::json(R"({"action":"set_values","set_values":{"values":[{"name":"name"}]}})");
        QJsonObject setValues = arguments.value(QStringLiteral("set_values")).toObject();
        setValues.insert(QStringLiteral("values"), QJsonArray({ QJsonObject({ { QStringLiteral("name"), QStringLiteral("name") }, { QStringLiteral("value"), QString::fromLatin1(eraCase.value) } }) }));
        arguments.insert(QStringLiteral("set_values"), setValues);
        arguments.insert(QStringLiteral("documentId"), documentId);
        const QJsonObject response = client->callTool(QStringLiteral("forms"), arguments);
        const QJsonObject callResult = response.value(QStringLiteral("result")).toObject();
        QVERIFY2(!callResult.isEmpty() && !callResult.value(QStringLiteral("isError")).toBool(), QJsonDocument(response).toJson().constData());
        QCOMPARE(asked, 1);
        QCOMPARE(H::fieldValue(formTab->getDocument()->getDocument().data(), choice), QString::fromLatin1(eraCase.value));
    }
}

void AppMcpEditTest::fixFocusBackAfterDialog()
{
    // Fix wave FW7-H, item 39: the focus went to the next widget of the chain when
    // the dialog closed - it goes back to the widget of the tab, which had it
    // before the dialog, else to the canvas
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("focus.pdf"), 1));
    QVERIFY(tab);
    fixture.window->activateWindow();
    QWidget* canvas = tab->getView()->getPdfWidget();
    QVERIFY(canvas);
    auto isCanvasFocused = [canvas]()
    {
        QWidget* focus = QApplication::focusWidget();
        return focus && (focus == canvas || canvas->isAncestorOf(focus));
    };
    canvas->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(isCanvasFocused());

    // The canvas: Esc denies, the focus is back on the canvas
    const QJsonObject add = H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}}]}})");
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"), add);
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QTRY_VERIFY(QApplication::focusWidget() && (QApplication::focusWidget() == dialog || dialog->isAncestorOf(QApplication::focusWidget())));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    QVERIFY(H::isError(fixture.waitResult(call)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY2(isCanvasFocused(), QApplication::focusWidget() ? qPrintable(QApplication::focusWidget()->metaObject()->className()) : "none");

    // Another widget of the tab (the search field of the toolbar): two dialogs one
    // after another, answered by the mouse - the focus goes back to the field
    QWidget* field = tab->getView()->getToolbar() ? static_cast<QWidget*>(tab->getView()->getToolbar()->getSearchField()) : nullptr;
    QVERIFY(field && field->isVisible());
    field->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), field);
    std::shared_ptr<pdf::PDFMcpCall> first = fixture.startCall(QStringLiteral("annotations"), add);
    std::shared_ptr<pdf::PDFMcpCall> second = fixture.startCall(QStringLiteral("annotations"), add);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.ui()->getWaitingIds().size(), size_t(2), 10000);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    // The workers finish the builds of the two changes in any order, the dialog of
    // the oldest waiting confirmation comes first: the call of a dialog is taken
    // from its confirmation (the test denied the dialog of the second call and
    // waited for the first one, which waited for its own dialog)
    const std::shared_ptr<pdf::PDFMcpCall> deniedCall = dialog->getConfirmation()->call;
    QVERIFY(deniedCall == first || deniedCall == second);
    const std::shared_ptr<pdf::PDFMcpCall> allowedCall = deniedCall == first ? second : first;
    QTest::mouseClick(dialog->getDenyButton(), Qt::LeftButton);
    QVERIFY(H::isError(fixture.waitResult(deniedCall)));
    QVERIFY(!allowedCall->isFinished());
    QTRY_VERIFY(fixture.ui()->getDialog() && fixture.ui()->getDialog() != dialog && fixture.ui()->getDialog()->isVisible());
    dialog = fixture.ui()->getDialog();
    QVERIFY(dialog->getConfirmation()->call == allowedCall);
    QTRY_VERIFY(QApplication::focusWidget() && (QApplication::focusWidget() == dialog || dialog->isAncestorOf(QApplication::focusWidget())));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(allowedCall)));
    pdftest::AppDocumentFixture::settle();
    QCOMPARE(QApplication::focusWidget(), field);

    // A widget, which is gone: the canvas
    field->setFocus(Qt::OtherFocusReason);
    QTRY_COMPARE(QApplication::focusWidget(), field);
    call = fixture.startCall(QStringLiteral("annotations"), add);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    field->hide();
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));
    pdftest::AppDocumentFixture::settle();
    QVERIFY(isCanvasFocused());
    field->show();
}

void AppMcpEditTest::fixPageCountPlural()
{
    // Fix wave FW7-H, item 39: "%1 pages" of the read approval was formatted by
    // .arg - a translation had no plural forms; %Ln with the count
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Read, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Three_Pages.pdf"), 3));
    QVERIFY(tab);
    pdf::PDFAppDocumentTab* single = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("One_Page.pdf"), 1));
    QVERIFY(single);

    McpEditRecordingTranslator* translator = new McpEditRecordingTranslator(fixture.window);
    QCoreApplication::installTranslator(translator);
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("document"), H::json(R"({"action":"get_info"})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QVERIFY(translator->contains(QStringLiteral("pdf::PDFAppMcpConfirmDialog"), QStringLiteral("%Ln pages"), 3));
    QVERIFY(translator->contains(QStringLiteral("pdf::PDFAppMcpConfirmDialog"), QStringLiteral("1 page"), -1));
    QVERIFY(!translator->contains(QStringLiteral("pdf::PDFAppMcpConfirmDialog"), QStringLiteral("%1 pages"), -1));
    bool hasPages = false;
    for (QLabel* label : dialog->findChildren<QLabel*>())
    {
        hasPages = hasPages || label->text() == QStringLiteral("3 pages");
    }
    QVERIFY(hasPages);
    QCoreApplication::removeTranslator(translator);
    delete translator;
    fixture.ui()->answer(dialog->getConfirmationId(), pdf::PDFAppMcpDecision::DeniedUser);
    QVERIFY(H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::fixClientNamePercent()
{
    // Fix wave FW7-H, item 18: a self-reported client name with placeholders is
    // written as it is into the undo texts, the activity, the dialog and the
    // notification bar - nothing is substituted into or after it
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("percent.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    const QString name = QStringLiteral("Evil %1 %2 %n %L1 %Ln 100%");
    fixture.setClient(name);

    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"),
        H::json(R"({"action":"add","add":{"items":[{"type":"note","page":1,"point":{"x":100,"y":300}},{"type":"note","page":1,"point":{"x":150,"y":300}}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getTitleText(), QStringLiteral("%1 wants to change percent.pdf").arg(name));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    std::optional<pdf::PDFMcpCallResult> result = fixture.waitResult(call);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), name + QStringLiteral(" added 2 notes"));
    QTRY_VERIFY(fixture.ui()->findNotificationBar(document));
    QCOMPARE(fixture.ui()->findNotificationBar(document)->getMessage(), name + QStringLiteral(" added 2 notes"));

    // Changed and deleted: "%1 changed %n annotations" with the name last
    QStringList ids;
    for (const QJsonValue& value : H::structured(result).value(QStringLiteral("add")).toObject().value(QStringLiteral("created")).toArray())
    {
        ids << value.toObject().value(QStringLiteral("id")).toString();
    }
    QCOMPARE(ids.size(), 2);
    QJsonObject remove = H::json(R"({"action":"delete","delete":{}})");
    remove.insert(QStringLiteral("delete"), QJsonObject({ { QStringLiteral("ids"), QJsonArray::fromStringList(ids) } }));
    call = fixture.startCall(QStringLiteral("annotations"), remove);
    dialog = fixture.waitDialog();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    result = fixture.waitResult(call);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QCOMPARE(document->getUndoStack()->getUndoDescription(), name + QStringLiteral(" deleted 2 annotations"));

    // The activity names the client as it is
    bool isNamed = false;
    for (const pdf::PDFMcpActivityRow& row : fixture.activity())
    {
        isNamed = isNamed || row.client == name;
    }
    QVERIFY(isNamed);
}

void AppMcpEditTest::fixHostileEditArguments()
{
    // Fix wave FW7-H, item 47: every refusal of the arguments of outline and forms
    // is an execution error of the call, nothing is changed
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    pdf::PDFAppDocumentTab* outlineTab = fixture.openReady(pdftest::AppDocumentSamples::outline(fixture.folder()));
    QVERIFY(outlineTab);
    pdf::PDFAppDocument* outlineDocument = outlineTab->getDocument();
    const int undoBefore = pdftest::AppDocumentFixture::undoCount(outlineDocument);

    const char* outlineCalls[] = {
        R"({"action":"add","add":{"title":"   ","page":1}})",
        R"({"action":"add","add":{"title":"No target"}})",
        R"({"action":"add","add":{"title":"Far","page":99}})",
        R"({"action":"add","add":{"title":"Label","pageLabel":"no such label"}})",
        R"({"action":"add","add":{"title":"Parent","page":1,"parent":"abc"}})",
        R"({"action":"add","add":{"title":"Parent","page":1,"parent":"99"}})",
        R"({"action":"update","update":{"newTitle":"Nobody"}})",
        R"({"action":"update","update":{"item":"1","newTitle":"   "}})",
        R"({"action":"update","update":{"title":"No such title","newTitle":"X"}})",
        R"({"action":"update","update":{"item":"1","page":99}})",
        R"({"action":"update","update":{"item":"9.9"}})",
        R"({"action":"move","move":{"item":"2","parent":"2.1"}})",
        R"({"action":"move","move":{"item":"99"}})",
        R"({"action":"move","move":{"item":"x"}})",
        R"({"action":"delete","delete":{"items":["0"]}})",
        R"({"action":"delete","delete":{"items":["99"]}})",
        R"({"action":"replace","replace":{"items":[{"title":"A","level":2,"page":1}]}})",
        R"({"action":"replace","replace":{"items":[{"title":"A","level":1,"page":1},{"title":"B","level":3,"page":1}]}})",
        R"({"action":"replace","replace":{"items":[{"title":"A","level":1,"page":99}]}})",
        R"({"action":"replace","replace":{"items":[{"title":"A","level":1,"pageLabel":"no such label"}]}})",
    };
    for (const char* text : outlineCalls)
    {
        QJsonObject arguments = H::json(text);
        QVERIFY2(!arguments.isEmpty(), text);
        arguments.insert(QStringLiteral("documentId"), outlineDocument->getId());
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("outline"), arguments);
        QVERIFY2(result, text);
        QVERIFY2(H::isError(result), qPrintable(QString::fromLatin1(text) + QStringLiteral(" -> ") + H::text(result)));
    }
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(outlineDocument), undoBefore);

    // Forms: every value of one call refused or unchanged, the errors named
    pdf::PDFAppDocumentTab* formTab = fixture.openReady(pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(formTab);
    pdf::PDFAppDocument* formDocument = formTab->getDocument();
    const int formUndoBefore = pdftest::AppDocumentFixture::undoCount(formDocument);
    // (one call per value: a refused value of a call refuses the whole call)
    const char* values[] = {
        R"({"name":"fixed","value":"x"})", R"({"name":"agree","value":"text for a check box"})",
        R"({"name":"agree","checked":false})", R"({"name":"choice","value":"Nope"})", R"({"name":"colors","selected":["Red","Purple"]})",
        R"({"name":"colors","value":"Red"})", R"({"name":"reset","value":"x"})", R"({"name":"nonexistent","value":"x"})", R"({"name":"name"})",
        R"({"name":"name","value":"a","checked":true})", R"({"name":"name","value":"John"})", R"({"name":"fruit","value":"Apple"})",
        R"({"name":"secret","value":"password"})", R"({"name":"choice","checked":true})", R"({"name":"fruit","selected":["Apple"]})",
        R"({"name":"name","selected":["x"]})" };
    int refused = 0;
    for (const char* value : values)
    {
        QJsonObject setValues = H::json(R"({"action":"set_values","set_values":{}})");
        setValues.insert(QStringLiteral("set_values"), QJsonObject({ { QStringLiteral("values"), QJsonArray({ H::json(value) }) } }));
        setValues.insert(QStringLiteral("documentId"), formDocument->getId());
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("forms"), setValues);
        QVERIFY2(result, value);
        const QJsonObject output = H::structured(result).value(QStringLiteral("set_values")).toObject();
        const bool isRefused = H::isError(result) || !output.value(QStringLiteral("errors")).toArray().isEmpty();
        const bool isUnchanged = !output.value(QStringLiteral("unchanged")).toArray().isEmpty();
        QVERIFY2(isRefused || isUnchanged || output.value(QStringLiteral("changed")).toArray().isEmpty(), qPrintable(QString::fromLatin1(value) + QStringLiteral(" -> ") + H::text(result)));
        refused += isRefused ? 1 : 0;
    }
    QVERIFY2(refused >= 10, qPrintable(QString::number(refused)));
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(formDocument), formUndoBefore);

    // Reset of the fields at their defaults: nothing to do, no undo step
    QJsonObject reset = H::json(R"({"action":"reset","reset":{"names":["name","nonexistent"]}})");
    reset.insert(QStringLiteral("documentId"), formDocument->getId());
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("forms"), reset);
    QVERIFY(result);
    reset = H::json(R"({"action":"reset","reset":{"names":["required"]}})");
    reset.insert(QStringLiteral("documentId"), formDocument->getId());
    result = fixture.callTool(QStringLiteral("forms"), reset);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QVERIFY(!H::structured(result).value(QStringLiteral("reset")).toObject().value(QStringLiteral("reset")).toBool());
}

void AppMcpEditTest::fixMarkupQuoteFromTextIndex()
{
    // Fix wave FW7-H, item 47: a markup over the text of an indexed document
    // quotes the text in its row (the quote bounded with an ellipsis)
    using H = AppMcpEditTestHelper;

    McpEditFixture fixture;
    QVERIFY(fixture.setPermission(pdf::PDFAppMcpPermissionKind::Edit, pdf::PDFAppMcpPermission::Ask));
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("quote.pdf"), 1));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // The text index of the tab (built by a search)
    QJsonObject search = H::json(R"({"action":"search","search":{"query":"alpha"}})");
    search.insert(QStringLiteral("documentId"), document->getId());
    std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("text"), search);
    QVERIFY2(!H::isError(result), qPrintable(H::text(result)));
    QTRY_VERIFY(document->getTextIndex());

    // The whole page and a small area: the rows quote the text under the quads
    std::shared_ptr<pdf::PDFMcpCall> call = fixture.startCall(QStringLiteral("annotations"),
        H::json(R"({"action":"add","add":{"items":[{"type":"highlight","page":1,"quads":[[0,400,300,400,0,0,300,0]]},{"type":"underline","page":1,"quads":[[0,2,1,2,0,1,1,1]]}]}})"));
    pdf::PDFAppMcpConfirmDialog* dialog = fixture.waitDialog();
    QVERIFY(dialog);
    const QString rows = QString::fromUtf8(QJsonDocument(dialog->getConfirmation()->details.value(QStringLiteral("rows")).toArray()).toJson(QJsonDocument::Compact));
    QVERIFY2(rows.contains(QChar(0x201C)) && rows.contains(QStringLiteral("alpha")), qPrintable(rows));
    QTest::mouseClick(dialog->getAllowButton(), Qt::LeftButton);
    QVERIFY(!H::isError(fixture.waitResult(call)));
}

void AppMcpEditTest::fixDialogTextsEveryKind()
{
    // Fix wave FW7-H, item 47: the dialog and the waiting banner of every kind of
    // request, with the variants of their rows (more rows than shown, facts,
    // overwrite, signatures, a remote assistant)
    McpEditFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openReady(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Kinds.pdf"), 2));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    QJsonArray rows;
    for (int i = 0; i < pdf::PDFAppMcpConfirmDialog::MAXIMUM_ROWS + 3; ++i)
    {
        rows.append(QJsonObject({ { QStringLiteral("text"), QStringLiteral("Add %1 notes").arg(i + 2) }, { QStringLiteral("page"), QStringLiteral("Page 1") },
                                  { QStringLiteral("detail"), QStringLiteral("“quoted”") }, { QStringLiteral("icon"), QStringLiteral("note") } }));
    }
    QJsonArray facts;
    facts.append(QJsonObject({ { QStringLiteral("key"), QStringLiteral("Signatures") }, { QStringLiteral("value"), QStringLiteral("2 kept") } }));

    const pdf::PDFAppMcpConfirmationKind kinds[] = { pdf::PDFAppMcpConfirmationKind::ReadDocuments, pdf::PDFAppMcpConfirmationKind::Change, pdf::PDFAppMcpConfirmationKind::Save,
                                                     pdf::PDFAppMcpConfirmationKind::SaveCopy, pdf::PDFAppMcpConfirmationKind::Open, pdf::PDFAppMcpConfirmationKind::Close };
    QSet<QString> titles;
    QSet<QString> bodies;
    for (pdf::PDFAppMcpConfirmationKind kind : kinds)
    {
        for (int variant = 0; variant < 2; ++variant)
        {
            std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation = std::make_shared<pdf::PDFAppMcpConfirmation>();
            confirmation->id = quint64(100 + int(kind) * 2 + variant);
            confirmation->kind = kind;
            confirmation->clientName = variant ? QString() : QStringLiteral("Codex CLI");
            confirmation->clientNameRaw = variant ? QStringLiteral("unknown-client") : QStringLiteral("codex-mcp-client");
            confirmation->isRemote = variant == 1;
            confirmation->remoteAddress = QStringLiteral("192.168.1.57");
            confirmation->document = (kind == pdf::PDFAppMcpConfirmationKind::Change && variant == 1) ? nullptr : document;
            confirmation->permissions = variant ? pdf::PDFAppMcpPermissionKinds(pdf::PDFAppMcpPermissionKind::Read | pdf::PDFAppMcpPermissionKind::Edit | pdf::PDFAppMcpPermissionKind::Files)
                                                : pdf::PDFAppMcpPermissionKinds(pdf::PDFAppMcpPermissionKind::Files);
            confirmation->isAlwaysConfirmed = kind == pdf::PDFAppMcpConfirmationKind::Save;
            confirmation->isAfterDialog = kind == pdf::PDFAppMcpConfirmationKind::Open && variant == 1;
            confirmation->details.insert(QStringLiteral("rows"), rows);
            confirmation->details.insert(QStringLiteral("changeCount"), variant ? 1 : 9);
            confirmation->details.insert(QStringLiteral("signatureCount"), variant ? 1 : 3);
            confirmation->details.insert(QStringLiteral("isAnnotationChange"), variant == 0);
            confirmation->details.insert(QStringLiteral("path"), fixture.path(variant ? QStringLiteral("copy.pdf") : QStringLiteral("a/very/long/folder/name/of/the/user/Kinds copy.pdf")));
            confirmation->details.insert(QStringLiteral("overwrite"), variant == 1);
            confirmation->details.insert(QStringLiteral("unsaved"), variant ? QString() : QStringLiteral("3 changes"));
            confirmation->details.insert(QStringLiteral("content"), QStringLiteral("Kinds.pdf with 3 changes"));
            confirmation->details.insert(QStringLiteral("facts"), facts);
            confirmation->details.insert(QStringLiteral("tool"), variant ? QString() : QStringLiteral("review.statistics"));

            pdf::PDFAppMcpConfirmDialog dialog(confirmation, &fixture.host()->getResolver(), nullptr);
            dialog.setQueuePosition(variant, 3);
            dialog.setRemainingTime(65000);
            QVERIFY(!dialog.getTitleText().isEmpty());
            QCOMPARE(dialog.getTitleText(), pdf::PDFAppMcpConfirmDialog::createTitle(*confirmation));
            QVERIFY(!dialog.getReasonText().isEmpty());
            QCOMPARE(dialog.getCountdownText(), QStringLiteral("Denied automatically in 1:05"));
            titles.insert(dialog.getTitleText());

            pdf::PDFAppMcpWaitingBanner banner(nullptr);
            banner.setConfirmation(confirmation);
            banner.setRemainingTime(65000);
            QVERIFY2(banner.getBodyText().contains(QStringLiteral("1:05")), qPrintable(banner.getBodyText()));
            bodies.insert(banner.getBodyText());
            QVERIFY(!banner.getTitleText().isEmpty());
        }
    }
    QVERIFY2(titles.size() >= 7, qPrintable(QStringList(titles.values()).join(QStringLiteral(" | "))));
    QVERIFY2(bodies.size() >= 7, qPrintable(QStringList(bodies.values()).join(QStringLiteral(" | "))));
}

void AppMcpEditTest::fixGhostShapes()
{
    // Fix wave FW7-H, item 47: every shape of the ghosts of Show on Page is drawn
    // on its page (and round trips through the details of the confirmation)
    std::vector<pdf::PDFAppMcpGhost> ghosts;
    auto add = [&ghosts](pdf::PDFAppMcpGhost::Shape shape, const QPolygonF& points, const std::vector<QPolygonF>& strokes = std::vector<QPolygonF>())
    {
        pdf::PDFAppMcpGhost ghost;
        ghost.pageIndex = 0;
        ghost.shape = shape;
        ghost.points = points;
        ghost.strokes = strokes;
        ghost.color = QColor(0, 0, 255);
        ghosts.push_back(ghost);
    };
    add(pdf::PDFAppMcpGhost::Shape::Quads, QPolygonF({ QPointF(10, 190), QPointF(60, 190), QPointF(10, 170), QPointF(60, 170) }));
    add(pdf::PDFAppMcpGhost::Shape::Ellipse, QPolygonF({ QPointF(70, 150), QPointF(120, 190) }));
    add(pdf::PDFAppMcpGhost::Shape::Note, QPolygonF({ QPointF(130, 190) }));
    add(pdf::PDFAppMcpGhost::Shape::Polyline, QPolygonF({ QPointF(10, 100), QPointF(60, 140), QPointF(110, 100) }));
    add(pdf::PDFAppMcpGhost::Shape::Polygon, QPolygonF({ QPointF(130, 100), QPointF(180, 140), QPointF(190, 100) }));
    add(pdf::PDFAppMcpGhost::Shape::Ink, QPolygonF(), { QPolygonF({ QPointF(10, 20), QPointF(60, 60), QPointF(110, 20) }), QPolygonF() });
    add(pdf::PDFAppMcpGhost::Shape::Polyline, QPolygonF());

    for (const pdf::PDFAppMcpGhost& ghost : ghosts)
    {
        const std::optional<pdf::PDFAppMcpGhost> copy = pdf::PDFAppMcpGhost::fromJson(ghost.toJson());
        QVERIFY(copy || ghost.points.isEmpty());
    }

    pdf::PDFAppMcpGhostOverlay overlay;
    overlay.setGhosts(ghosts);
    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        overlay.drawGhosts(&painter, 0, QTransform(1, 0, 0, -1, 0, 200));
    }
    auto isPainted = [&image](const QRect& area)
    {
        for (int y = area.top(); y <= area.bottom(); ++y)
        {
            for (int x = area.left(); x <= area.right(); ++x)
            {
                if (image.pixelColor(x, y) != QColor(Qt::white))
                {
                    return true;
                }
            }
        }
        return false;
    };
    QVERIFY(isPainted(QRect(10, 10, 50, 20)));    // quads
    QVERIFY(isPainted(QRect(70, 10, 50, 40)));    // ellipse
    QVERIFY(isPainted(QRect(10, 60, 100, 40)));   // polyline
    QVERIFY(isPainted(QRect(130, 60, 60, 40)));   // polygon
    QVERIFY(isPainted(QRect(10, 140, 100, 40)));  // ink
    QVERIFY(!isPainted(QRect(195, 195, 4, 4)));
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
    AppMcpEditTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcpedittest.moc"
