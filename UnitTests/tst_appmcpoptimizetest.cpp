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

// The MCP actions optimize.images, optimize.grayscale and optimize.bitonal over HTTP (step 4 of
// phase 2, the integration; plan 7 and 8 row MCP, R4-21, the pattern of UnitTestsAppMcpPages): the
// schema from the definitions of the tools, result by the catalog, the refusal without bit 4
// Modify (ISO 32000-2, Table 22), a signed document only as a copy (Q4.14 a), the ids of
// document.list_images in images / exclude / items.

#include "appmcpstep4.h"
#include "mergetestpdf.h"

#include "pdfappbuiltintools.h"
#include "pdfappmcppermissions.h"

#include <QApplication>
#include <QtTest>

using pdftest::AppMcpFixture;
using pdftest::AppMcpImageDocument;
using pdftest::AppMcpStep4;

class AppMcpOptimizeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void schemaFromDefinitions_data();
    void schemaFromDefinitions();
    void refusedWithoutModify();
    void signedOnlyCopy();
    void grayscaleExcludeById();
    void imagesExceptionById();
    void bitonalItemById();

private:
    /// Two color images on one page (the first 64 x 32, the second 48 x 48 pixels)
    static QString writeTwoColorImages(AppMcpFixture& fixture, pdf::PDFObjectReference* first, pdf::PDFObjectReference* second, uint32_t permissions = 0);

    /// Starts the server over HTTP with every permission Allow
    static bool start(AppMcpFixture& fixture);
};

void AppMcpOptimizeTest::initTestCase()
{
    // The tools of the application, as the controller of MCP sees them
    pdf::PDFAppBuiltinTools::registerAll();
}

QString AppMcpOptimizeTest::writeTwoColorImages(AppMcpFixture& fixture, pdf::PDFObjectReference* first, pdf::PDFObjectReference* second, uint32_t permissions)
{
    AppMcpImageDocument document;
    *first = document.addColorImage(64, 32);
    *second = document.addColorImage(48, 48);
    document.addImagePage({ { *first, "128 0 0 64 20 20" }, { *second, "96 0 0 96 20 200" } });
    if (permissions)
    {
        document.encrypt(permissions);
    }
    return document.write(fixture.path(QStringLiteral("Photos.pdf")));
}

bool AppMcpOptimizeTest::start(AppMcpFixture& fixture)
{
    if (!fixture.setPermissions(pdf::PDFAppMcpPermission::Allow) || !fixture.startServer())
    {
        return false;
    }
    fixture.setTransport(pdftest::AppMcpTransport::Http);
    return true;
}

void AppMcpOptimizeTest::schemaFromDefinitions_data()
{
    // The rows of the catalog (pdfdocumenttoolcatalog.cpp, plan 7): the result kind ChangeDocument
    // has keep, save_copy and overwrite; the preselected result is the default
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<QString>("defaultResult");
    QTest::newRow("images") << QStringLiteral("optimize.images") << QStringLiteral("keep");
    QTest::newRow("grayscale") << QStringLiteral("optimize.grayscale") << QStringLiteral("keep");
    QTest::newRow("bitonal") << QStringLiteral("optimize.bitonal") << QStringLiteral("save_copy");
}

void AppMcpOptimizeTest::schemaFromDefinitions()
{
    QFETCH(QString, toolId);
    QFETCH(QString, defaultResult);

    // Every parameter of the definitions of the tool, which an assistant gives, is a property of
    // its action in the schema of the tool "optimize" (the schema comes from the definitions)
    const QJsonObject action = AppMcpStep4::actionSchema(toolId);
    QVERIFY2(!action.isEmpty(), qPrintable(toolId));
    const QJsonObject properties = action.value(QStringLiteral("properties")).toObject();
    const QStringList parameters = AppMcpStep4::getAssistantParameters(toolId);
    QVERIFY(!parameters.isEmpty());
    for (const QString& parameter : parameters)
    {
        QVERIFY2(properties.contains(parameter), qPrintable(toolId + QStringLiteral(": ") + parameter));
    }
    QCOMPARE(properties.size(), parameters.size());

    // The id parameters name the ids of document.list_images (plan 7, R4-8)
    const QString idParameter = toolId == QLatin1String("optimize.images") ? QStringLiteral("images") : toolId == QLatin1String("optimize.grayscale") ? QStringLiteral("exclude") : QStringLiteral("items");
    QVERIFY2(properties.contains(idParameter), qPrintable(idParameter));

    // result by the kind of the catalog
    const std::shared_ptr<const pdf::PDFDocumentTool> tool = pdf::PDFDocumentToolRegistry::getTool(toolId);
    QVERIFY(tool);
    QCOMPARE(pdf::PDFAppMcpCatalogTools::getToolResultValues(*tool->getInfo(), tool.get()), QStringList({ QStringLiteral("keep"), QStringLiteral("save_copy"), QStringLiteral("overwrite") }));
    QCOMPARE(pdf::PDFAppMcpCatalogTools::getDefaultResult(*tool->getInfo()), defaultResult);
}

void AppMcpOptimizeTest::refusedWithoutModify()
{
    // ISO 32000-2, Table 22: without bit 4 (Modify) the three tools refuse the document; nothing
    // changes (opened without the owner password)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFObjectReference first;
    pdf::PDFObjectReference second;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeTwoColorImages(fixture, &first, &second, AppMcpStep4::allowedWithout(4)));
    QVERIFY(tab);
    const QJsonObject common({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } });
    for (const QString& action : { QStringLiteral("images"), QStringLiteral("grayscale"), QStringLiteral("bitonal") })
    {
        const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("optimize"), action, QJsonObject(), common);
        QVERIFY(result);
        QVERIFY2(result->isError, qPrintable(action + QStringLiteral(": ") + result->getFirstText()));
        QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
    }
    QCOMPARE(AppMcpStep4::colorSpace(tab->getDocument()->getDocument().data(), first), QByteArray("DeviceRGB"));

    // The same document with Modify allowed runs (the refusal was the permission)
    AppMcpFixture allowedFixture;
    QVERIFY(start(allowedFixture));
    pdf::PDFAppDocumentTab* allowedTab = AppMcpStep4::open(allowedFixture, writeTwoColorImages(allowedFixture, &first, &second, AppMcpStep4::allowedWithout(6)));
    QVERIFY(allowedTab);
    const std::optional<pdf::PDFMcpCallResult> allowed = AppMcpStep4::call(allowedFixture, QStringLiteral("optimize"), QStringLiteral("grayscale"), QJsonObject(),
                                                                            QJsonObject({ { QStringLiteral("documentId"), allowedTab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(allowed);
    QVERIFY2(!allowed->isError, qPrintable(allowed->getFirstText()));
}

void AppMcpOptimizeTest::signedOnlyCopy()
{
    // Q4.14 a: a signed document is changed by the three tools only as a copy written whole -
    // keep and overwrite are refused with the reason (the signature is a field /FT /Sig with /V)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("S", 2, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >>", { { 1, "/Annots [60 0 R]" } });
    pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
    const QString filePath = fixture.path(QStringLiteral("Signed.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QFile::WriteOnly) && file.write(pdf.write()) > 0);
    file.close();
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, filePath);
    QVERIFY(tab);

    for (const QString& action : { QStringLiteral("images"), QStringLiteral("grayscale"), QStringLiteral("bitonal") })
    {
        for (const QString& resultValue : { QStringLiteral("keep"), QStringLiteral("overwrite") })
        {
            const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("optimize"), action, QJsonObject(),
                                                                                  QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), resultValue } }));
            QVERIFY(result);
            QVERIFY2(result->isError, qPrintable(action + QStringLiteral(" ") + resultValue));
            QVERIFY2(result->getFirstText().contains(QStringLiteral("The document is signed")), qPrintable(result->getFirstText()));
        }
    }
    QCOMPARE(pdftest::AppDocumentFixture::undoCount(tab->getDocument()), 0);
}

void AppMcpOptimizeTest::grayscaleExcludeById()
{
    // R4-5, plan 7: Grayscale converts the color images (DeviceRGB -> DeviceGray), an image
    // excluded by its id of document.list_images keeps its color space
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFObjectReference first;
    pdf::PDFObjectReference second;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeTwoColorImages(fixture, &first, &second));
    QVERIFY(tab);

    const QJsonObject list = AppMcpStep4::listImages(fixture, tab);
    const QJsonArray images = list.value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), 2);
    QCOMPARE(images.at(0).toObject().value(QStringLiteral("id")).toString(), AppMcpImageDocument::id(first));
    const QString excludedId = images.at(0).toObject().value(QStringLiteral("id")).toString();

    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("optimize"), QStringLiteral("grayscale"), QJsonObject({ { QStringLiteral("exclude"), excludedId } }),
                                                                          QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    QCOMPARE(AppMcpStep4::colorSpace(document, first), QByteArray("DeviceRGB"));
    QCOMPARE(AppMcpStep4::colorSpace(document, second), QByteArray("DeviceGray"));

    // An id, which is no image id, is refused by the tool with the hint of list_images
    const std::optional<pdf::PDFMcpCallResult> refused = AppMcpStep4::call(fixture, QStringLiteral("optimize"), QStringLiteral("grayscale"), QJsonObject({ { QStringLiteral("exclude"), QStringLiteral("image one") } }),
                                                                           QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(refused && refused->isError);
    QVERIFY2(refused->getFirstText().contains(QStringLiteral("document.list_images")), qPrintable(refused->getFirstText()));
}

void AppMcpOptimizeTest::imagesExceptionById()
{
    // Plan 7: images = "<id>:off" of document.list_images keeps that image byte for byte (the
    // exception enabled=false); the other one is optimized (the grayscale mode makes it DeviceGray)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFObjectReference first;
    pdf::PDFObjectReference second;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeTwoColorImages(fixture, &first, &second));
    QVERIFY(tab);
    const pdf::PDFDocument* original = tab->getDocument()->getDocument().data();
    const QByteArray originalData = *original->getObjectByReference(second).getStream()->getContent();

    const QJsonArray images = AppMcpStep4::listImages(fixture, tab).value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), 2);
    const QString keptId = images.at(1).toObject().value(QStringLiteral("id")).toString();
    QCOMPARE(keptId, AppMcpImageDocument::id(second));

    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("optimize"), QStringLiteral("images"),
                                                                          QJsonObject({ { QStringLiteral("colorMode"), QStringLiteral("grayscale") }, { QStringLiteral("images"), keptId + QStringLiteral(":off") } }),
                                                                          QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    QCOMPARE(AppMcpStep4::colorSpace(document, second), QByteArray("DeviceRGB"));
    QCOMPARE(*document->getObjectByReference(second).getStream()->getContent(), originalData);
    QCOMPARE(AppMcpStep4::colorSpace(document, first), QByteArray("DeviceGray"));
}

void AppMcpOptimizeTest::bitonalItemById()
{
    // Plan 7: items = "<id>=unchanged" (an id of document.list_images) leaves that image as it
    // is in the source Images; the other image becomes 1 bit per component (8.9.5.2)
    AppMcpFixture fixture;
    QVERIFY(start(fixture));
    pdf::PDFObjectReference first;
    pdf::PDFObjectReference second;
    pdf::PDFAppDocumentTab* tab = AppMcpStep4::open(fixture, writeTwoColorImages(fixture, &first, &second));
    QVERIFY(tab);

    const QJsonArray images = AppMcpStep4::listImages(fixture, tab).value(QStringLiteral("images")).toArray();
    QCOMPARE(images.size(), 2);
    const QString keptId = images.at(0).toObject().value(QStringLiteral("id")).toString();
    QCOMPARE(keptId, AppMcpImageDocument::id(first));

    const std::optional<pdf::PDFMcpCallResult> result = AppMcpStep4::call(fixture, QStringLiteral("optimize"), QStringLiteral("bitonal"),
                                                                          QJsonObject({ { QStringLiteral("source"), QStringLiteral("images") }, { QStringLiteral("items"), keptId + QStringLiteral("=unchanged") } }),
                                                                          QJsonObject({ { QStringLiteral("documentId"), tab->getDocument()->getId() }, { QStringLiteral("result"), QStringLiteral("keep") } }));
    QVERIFY(result);
    QVERIFY2(!result->isError, qPrintable(result->getFirstText()));
    const pdf::PDFDocument* document = tab->getDocument()->getDocument().data();
    const pdf::PDFDictionary* kept = AppMcpStep4::imageDictionary(document, first);
    const pdf::PDFDictionary* converted = AppMcpStep4::imageDictionary(document, second);
    QVERIFY(kept && converted);
    QCOMPARE(document->getObject(kept->get("BitsPerComponent")).getInteger(), pdf::PDFInteger(8));
    QCOMPARE(document->getObject(converted->get("BitsPerComponent")).getInteger(), pdf::PDFInteger(1));
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
    AppMcpOptimizeTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appmcpoptimizetest.moc"
