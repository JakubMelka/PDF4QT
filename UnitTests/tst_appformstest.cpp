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

#include "appdocumentfixture.h"

#include "pdfappannotationcontroller.h"
#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentactions.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentinfobar.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappformcontroller.h"
#include "pdfappmainwindow.h"
#include "pdfappsendmail.h"
#include "pdfappundostack.h"
#include "pdfsettingdefinitions.h"

#include "pdfcertificatemanager.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumentwriter.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfform.h"
#include "pdfsecurityhandler.h"
#include "pdfsignaturehandler.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgetsnapshot.h"

#include "pdfmessagebox.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

/// Samples and helpers of the tests of package D
class FormsTestHelper
{
public:
    FormsTestHelper() = delete;

    static pdf::PDFObject name(const char* value) { return pdf::PDFObject::createName(QByteArray(value)); }
    static pdf::PDFObject text(const QString& value) { return pdf::PDFObjectFactory::createTextString(value); }
    static pdf::PDFObject reference(pdf::PDFObjectReference value) { return pdf::PDFObject::createReference(value); }
    static pdf::PDFObject integer(pdf::PDFInteger value) { return pdf::PDFObject::createInteger(value); }

    static pdf::PDFObject array(std::initializer_list<pdf::PDFObject> items)
    {
        pdf::PDFArrayBuilder builder;
        for (const pdf::PDFObject& item : items)
        {
            builder.appendItem(item);
        }
        return pdf::PDFObject::createArray(std::move(builder));
    }

    static pdf::PDFObject dictionary(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        return pdf::PDFObject::createDictionary(std::move(builder));
    }

    static pdf::PDFObject stream(std::initializer_list<std::pair<const char*, pdf::PDFObject>> entries, QByteArray data)
    {
        pdf::PDFDictionaryBuilder builder;
        for (const auto& entry : entries)
        {
            builder.setEntry(pdf::PDFInplaceOrMemoryString(entry.first), entry.second);
        }
        builder.setEntry(pdf::PDFInplaceOrMemoryString("Length"), integer(data.size()));
        return pdf::PDFObject::createStream(pdf::PDFStream(std::move(builder), std::move(data)));
    }

    static QString write(pdf::PDFDocumentBuilder& builder, const QString& folder, const QString& fileName)
    {
        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(fileName);
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// Text field with its widget on the page
    static pdf::PDFObjectReference addTextField(pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference page, const QString& fieldName, const QRectF& rectangle,
                                                const QString& value = QString())
    {
        const pdf::PDFObject da = pdf::PDFObject::createString(QByteArray("/Helv 10 Tf 0 g"));
        const pdf::PDFObjectReference field = value.isEmpty() ? builder.addObject(dictionary({ { "FT", name("Tx") }, { "T", text(fieldName) }, { "DA", da } }))
                                                              : builder.addObject(dictionary({ { "FT", name("Tx") }, { "T", text(fieldName) }, { "DA", da }, { "V", text(value) } }));
        builder.createFormFieldWidget(field, page, pdf::PDFObjectReference(), rectangle);
        return field;
    }

    /// Two pages; page 1 has the text fields "first" and "second", page 2 the
    /// field "third" near its bottom (Tab scrolls the canvas to it)
    static QString multiPageForm(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 300, 400));
        pdf::PDFObjectReferenceVector fields;
        fields.push_back(addTextField(builder, page1, QStringLiteral("first"), QRectF(20, 340, 120, 18)));
        fields.push_back(addTextField(builder, page1, QStringLiteral("second"), QRectF(20, 300, 120, 18)));
        fields.push_back(addTextField(builder, page2, QStringLiteral("third"), QRectF(160, 20, 120, 18)));
        builder.setCatalogAcroForm(builder.createAcroForm(fields));
        return write(builder, folder, QStringLiteral("multi-page-form.pdf"));
    }

    /// Owner password only: the form can be filled (bit 9), annotations cannot be
    /// changed (bit 6 cleared)
    static QString fillOnlyForm(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference field = addTextField(builder, page, QStringLiteral("name"), QRectF(40, 260, 200, 24), QStringLiteral("John"));
        builder.setCatalogAcroForm(builder.createAcroForm({ field }));

        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.ownerPassword = QStringLiteral("owner-secret");
        settings.permissions = 0xFFFFFFFCu & ~0x0020u;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        return write(builder, folder, QStringLiteral("fill-only.pdf"));
    }

    /// An AcroForm with an XFA packet and one text field
    static QString xfaForm(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 612, 792));
        const pdf::PDFObjectReference field = addTextField(builder, page, QStringLiteral("name"), QRectF(40, 700, 200, 24), QStringLiteral("John"));
        const QByteArray xdp = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                               "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\">\n"
                               "<template xmlns=\"http://www.xfa.org/schema/xfa-template/3.3/\">\n"
                               "<subform name=\"form1\" layout=\"tb\">\n"
                               "<pageSet><pageArea name=\"Page1\"><contentArea x=\"0.25in\" y=\"0.25in\" w=\"8in\" h=\"10.5in\"/>"
                               "<medium stock=\"letter\" short=\"8.5in\" long=\"11in\"/></pageArea></pageSet>\n"
                               "<field name=\"name\" w=\"2in\" h=\"0.3in\"><ui><textEdit/></ui><value><text>John</text></value></field>\n"
                               "</subform>\n</template>\n</xdp:xdp>\n";
        const pdf::PDFObjectReference xfa = builder.addObject(stream({}, xdp));
        const pdf::PDFObjectReference acroForm = builder.createAcroForm({ field });
        builder.mergeTo(acroForm, dictionary({ { "XFA", reference(xfa) } }));
        builder.setCatalogAcroForm(acroForm);
        return write(builder, folder, QStringLiteral("xfa-form.pdf"));
    }

    /// Hostile form: a cycle of /Kids, a field without a type, a widget without
    /// /P and with an inverted /Rect, a negative /MaxLen, a choice with a garbage
    /// /Opt, a check box without appearance streams, a field outside of /Fields
    static QString hostileForm(const QString& folder)
    {
        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObject da = pdf::PDFObject::createString(QByteArray("/Helv 10 Tf 0 g"));
        pdf::PDFObjectReferenceVector fields;

        // A cycle: parent -> child -> parent
        const pdf::PDFObjectReference parent = builder.addObject(dictionary({ { "T", text(QStringLiteral("cycle")) }, { "FT", name("Tx") } }));
        const pdf::PDFObjectReference child = builder.addObject(dictionary({ { "T", text(QStringLiteral("kid")) }, { "Parent", reference(parent) },
                                                                             { "Kids", array({ reference(parent) }) } }));
        builder.mergeTo(parent, dictionary({ { "Kids", array({ reference(child) }) } }));
        fields.push_back(parent);

        // Field without a type, field with a negative MaxLen and an inverted rectangle
        fields.push_back(builder.addObject(dictionary({ { "T", text(QStringLiteral("untyped")) }, { "V", integer(5) } })));
        const pdf::PDFObjectReference negative = builder.addObject(dictionary({ { "FT", name("Tx") }, { "T", text(QStringLiteral("negative")) }, { "MaxLen", integer(-7) },
                                                                                { "Ff", integer(1 << 24) }, { "DA", da } }));
        builder.createFormFieldWidget(negative, page, pdf::PDFObjectReference(), QRectF(20, 300, 120, 18));
        builder.mergeTo(negative, dictionary({ { "Rect", array({ integer(140), integer(318), integer(20), integer(300) }) } }));
        fields.push_back(negative);

        // A widget without /P (the field is found by the annotations of the page)
        const pdf::PDFObjectReference orphan = addTextField(builder, page, QStringLiteral("orphan"), QRectF(20, 260, 120, 18));
        builder.mergeTo(orphan, dictionary({ { "P", pdf::PDFObject() } }));
        fields.push_back(orphan);

        // Choice with a garbage /Opt and a value of a wrong type
        const pdf::PDFObjectReference choice = builder.addObject(dictionary({ { "FT", name("Ch") }, { "T", text(QStringLiteral("garbage")) }, { "Ff", integer(1 << 17) },
                                                                              { "Opt", array({ integer(1), array({}), name("X"), pdf::PDFObject() }) }, { "V", integer(3) }, { "DA", da } }));
        builder.createFormFieldWidget(choice, page, pdf::PDFObjectReference(), QRectF(160, 300, 120, 18));
        fields.push_back(choice);

        // Check box without the appearance streams
        const pdf::PDFObjectReference checkBox = builder.addObject(dictionary({ { "FT", name("Btn") }, { "T", text(QStringLiteral("box")) }, { "V", name("Yes") } }));
        builder.createFormFieldWidget(checkBox, page, pdf::PDFObjectReference(), QRectF(160, 260, 14, 14));
        fields.push_back(checkBox);

        // A widget, which is not in /Fields (a rogue field)
        addTextField(builder, page, QStringLiteral("rogue"), QRectF(20, 220, 120, 18), QStringLiteral("R"));

        // Huge rectangle of a field
        const pdf::PDFObjectReference huge = addTextField(builder, page, QStringLiteral("huge"), QRectF(20, 180, 120, 18));
        builder.mergeTo(huge, dictionary({ { "Rect", array({ integer(-1000000), integer(-1000000), integer(1000000), integer(1000000) }) } }));
        fields.push_back(huge);

        const pdf::PDFObjectReference acroForm = builder.createAcroForm(fields);
        builder.mergeTo(acroForm, dictionary({ { "CO", array({ integer(7), reference(child), name("X") }) }, { "SigFlags", text(QStringLiteral("x")) } }));
        builder.setCatalogAcroForm(acroForm);
        return write(builder, folder, QStringLiteral("hostile-form.pdf"));
    }

    /// One page with the text field "name" (John), signed by an invisible
    /// signature of a self-signed test certificate
    static QString signedForm(const QString& folder)
    {
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = QDir(folder).absoluteFilePath(QStringLiteral("forms-test.p12"));
        info.privateKeyPasword = QStringLiteral("test-password");
        info.certCommonName = QStringLiteral("PDF4QT forms test");
        info.rsaKeyLength = 2048;
        pdf::PDFCertificateManager().createCertificate(info);

        QFile certificateFile(info.fileName);
        if (!certificateFile.open(QIODevice::ReadOnly))
        {
            return QString();
        }
        pdf::PDFCertificateEntry certificate;
        certificate.pkcs12 = certificateFile.readAll();
        certificateFile.close();

        pdf::PDFDocumentBuilder builder;
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
        const pdf::PDFObjectReference field = addTextField(builder, page, QStringLiteral("name"), QRectF(40, 260, 200, 24), QStringLiteral("John"));
        builder.setCatalogAcroForm(builder.createAcroForm({ field }));
        const pdf::PDFDocument document = builder.build();

        QByteArray signedData;
        pdf::PDFDocumentSigner::Parameters parameters;
        parameters.document = &document;
        parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
        {
            return pdf::PDFSignatureFactory::sign(certificate, QStringLiteral("test-password"), data, signature);
        };
        parameters.createSignatureFieldFunction = [&](pdf::PDFDocumentBuilder& signatureBuilder, pdf::PDFObjectReference signatureDictionary)
        {
            return signatureBuilder.createSignatureField(QStringLiteral("Signature1"), signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
        };
        if (pdf::PDFDocumentSigner::sign(parameters, signedData) != pdf::PDFDocumentSigner::Result::OK)
        {
            return QString();
        }

        const QString filePath = QDir(folder).absoluteFilePath(QStringLiteral("signed-form.pdf"));
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly) || file.write(signedData) != signedData.size())
        {
            return QString();
        }
        return filePath;
    }

    /// Opens the file and waits until the document is shown
    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab)
        {
            return nullptr;
        }
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        if (!QTest::qWaitFor([&guard]() { return !guard || (guard->getDocument() && guard->getDocument()->isReady() && guard->getView()->getProxy()->getDocument()); }, 20000))
        {
            return nullptr;
        }
        pdftest::AppDocumentFixture::settle();
        return guard.data();
    }

    static pdf::PDFWidgetFormManager* formManager(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? tab->getView()->getFormManager() : nullptr;
    }

    static pdf::PDFAppFormController* controller(pdf::PDFAppDocumentTab* tab)
    {
        return tab ? tab->getView()->getFormController() : nullptr;
    }

    /// Terminal field of the partial name
    static const pdf::PDFFormField* field(pdf::PDFAppDocumentTab* tab, const QString& fieldName)
    {
        const pdf::PDFFormField* result = nullptr;
        if (pdf::PDFWidgetFormManager* manager = formManager(tab))
        {
            manager->getForm()->apply([&result, &fieldName](const pdf::PDFFormField* formField)
            {
                if (!result && formField->getName(pdf::PDFFormField::Partial) == fieldName)
                {
                    result = formField;
                }
            });
        }
        return result;
    }

    /// Value of the field as a text (text strings, names, arrays joined by ',')
    static QString value(pdf::PDFAppDocumentTab* tab, const QString& fieldName)
    {
        const pdf::PDFFormField* formField = field(tab, fieldName);
        const pdf::PDFDocument* document = formManager(tab) ? formManager(tab)->getDocument() : nullptr;
        if (!formField || !document)
        {
            return QStringLiteral("<none>");
        }

        pdf::PDFDocumentDataLoaderDecorator loader(document);
        const pdf::PDFObject object = document->getObject(formField->getValue());
        if (object.isName())
        {
            return QString::fromLatin1(object.getString());
        }
        if (object.isArray())
        {
            return loader.readTextStringList(object).join(QLatin1Char(','));
        }
        return loader.readTextString(object, QString());
    }

    /// Widgets of the field and of its kids (a kid of a radio group is a field
    /// without a name merged with its widget)
    static pdf::PDFFormWidgets widgets(const pdf::PDFFormField* formField)
    {
        pdf::PDFFormWidgets result = formField ? formField->getWidgets() : pdf::PDFFormWidgets();
        if (formField)
        {
            for (const pdf::PDFFormFieldPointer& child : formField->getChildFields())
            {
                const pdf::PDFFormWidgets childWidgets = widgets(child.get());
                result.insert(result.end(), childWidgets.cbegin(), childWidgets.cend());
            }
        }
        return result;
    }

    /// Center of the widget (page coordinates) of the field
    static QPointF center(pdf::PDFAppDocumentTab* tab, const QString& fieldName, int widgetIndex = 0)
    {
        const pdf::PDFFormWidgets fieldWidgets = widgets(field(tab, fieldName));
        if (widgetIndex >= int(fieldWidgets.size()))
        {
            return QPointF();
        }
        return formManager(tab)->getWidgetRectangle(fieldWidgets.at(widgetIndex)).center();
    }

    static bool click(pdf::PDFAppDocumentTab* tab, const QString& fieldName, int widgetIndex = 0, int pageIndex = 0)
    {
        const bool result = pdftest::AppDocumentFixture::clickPage(tab->getView(), pageIndex, center(tab, fieldName, widgetIndex));
        pdftest::AppDocumentFixture::settle();
        return result;
    }

    /// Clicks an empty place of the page (the open editor is closed)
    static bool clickOutside(pdf::PDFAppDocumentTab* tab)
    {
        const bool result = pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(290, 10));
        pdftest::AppDocumentFixture::settle();
        return result;
    }

    static QWidget* canvas(pdf::PDFAppDocumentTab* tab)
    {
        return pdftest::AppDocumentFixture::canvasWidget(tab->getView());
    }

    /// Replaces the text of the focused editor (Ctrl+A selects it - a click puts
    /// the cursor to the clicked place)
    static void type(pdf::PDFAppDocumentTab* tab, const QString& text)
    {
        QTest::keyClick(canvas(tab), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(canvas(tab), text);
        pdftest::AppDocumentFixture::settle();
    }

    /// Types at the cursor of the focused editor
    static void typeMore(pdf::PDFAppDocumentTab* tab, const QString& text)
    {
        QTest::keyClicks(canvas(tab), text);
        pdftest::AppDocumentFixture::settle();
    }

    static void key(pdf::PDFAppDocumentTab* tab, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::keyClick(canvas(tab), key, modifiers);
        pdftest::AppDocumentFixture::settle();
    }

    static QString focusedFieldName(pdf::PDFAppDocumentTab* tab)
    {
        pdf::PDFWidgetFormManager* manager = formManager(tab);
        const pdf::PDFFormField* formField = manager ? manager->getFormFieldForWidget(manager->getFocusedWidget()) : nullptr;
        while (formField && formField->getName(pdf::PDFFormField::Partial).isEmpty())
        {
            formField = formField->getParentField();
        }
        return formField ? formField->getName(pdf::PDFFormField::Partial) : QString();
    }

    static bool execute(pdf::PDFAppDocumentTab* tab, const QString& commandId)
    {
        const bool result = tab->executeCommand(commandId);
        pdftest::AppDocumentFixture::settle();
        return result;
    }

    static int undoCount(pdf::PDFAppDocumentTab* tab)
    {
        return pdftest::AppDocumentFixture::undoCount(tab->getDocument());
    }

    static bool hasBar(pdf::PDFAppDocumentTab* tab, const QString& id)
    {
        return tab->getView()->findInfoBar(id) != nullptr;
    }

    /// Color of the pixel of the canvas at the point of the page
    static QColor canvasPixel(pdf::PDFAppDocumentTab* tab, const QPointF& pagePoint)
    {
        bool ok = false;
        const QPoint position = pdftest::AppDocumentFixture::pageToWidget(tab->getView(), 0, pagePoint, &ok);
        QWidget* widget = canvas(tab);
        if (!ok || !widget)
        {
            return QColor();
        }
        const QImage image = widget->grab().toImage();
        const QPoint scaled = QPoint(qRound(position.x() * image.devicePixelRatio()), qRound(position.y() * image.devicePixelRatio()));
        return image.rect().contains(scaled) ? image.pixelColor(scaled) : QColor();
    }

    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        int count = 0;
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                if ((line[x] & 0xFFFFFF) == leak && qAlpha(line[x]) > 0)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /// Runs \p action on the next modal dialog of the type (from the event loop of its exec)
    template<typename Dialog>
    static void onDialog(std::function<void(Dialog*)> action, int timeout = 10000)
    {
        auto check = std::make_shared<std::function<void(int)>>();
        *check = [action, check, timeout](int elapsed)
        {
            Dialog* dialog = qobject_cast<Dialog*>(QApplication::activeModalWidget());
            if (dialog && dialog->isVisible())
            {
                action(dialog);
                *check = nullptr;
                return;
            }
            if (elapsed < timeout)
            {
                QTimer::singleShot(10, [check, elapsed]() { if (*check) { (*check)(elapsed + 10); } });
            }
            else
            {
                *check = nullptr;
            }
        };
        QTimer::singleShot(0, [check]() { if (*check) { (*check)(0); } });
    }

    static void setSetting(pdftest::AppDocumentFixture& fixture, const QString& key, bool value)
    {
        QVERIFY(!fixture.context->getSettings().setValue(key, value).isError());
        pdftest::AppDocumentFixture::settle();
    }
};

/// Tests of package D of step 4 (form filling): commits of the fields as undo
/// steps, the read-only form, the settings forms.*, pending edits (undo, save,
/// lock, close), the keyboard, the info bars, signed and hostile documents and
/// the lifetime of the form controller
class AppFormsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void fullFormSample();
    void readOnlyFormWaveZero();
    void textFieldCommit();
    void textFieldEscReverts();
    void checkBoxRadio();
    void comboAndListBox();
    void pushButtonResetForm();
    void reviewResetFormByLink();
    void reviewResetFormWithoutChange();
    void reviewPendingEditsCommitPoints();
    void highlightSettings();
    void readOnlyField();
    void allowEditReadOnlySetting();
    void lockedDocumentReadOnly();
    void fillFormPermission();
    void undoRedoForm();
    void undoClosesEditor();
    void pendingEditsSaved();
    void pendingEditsOnClose();
    void focusOutCommits();
    void calculatedFieldsInfoBar();
    void xfaInfoBar();
    void keyboardTab();
    void keyboardTabScrolls();
    void signedDocumentFill();
    void hostileForm();
    void deletionMatrix();
    void deleteFromSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void languageChange();
    void screenshots();
};

void AppFormsTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void AppFormsTest::cleanup()
{
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QThreadPool::globalInstance()->waitForDone(30000);
}

void AppFormsTest::fullFormSample()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(tab->getDocument()->isReady());

    pdf::PDFWidgetFormManager* formManager = tab->getView()->getFormManager();
    QVERIFY(formManager->hasAcroForm());
    QVERIFY(tab->getView()->getFormController());
    QCOMPARE(tab->getView()->getFormController()->getFormManager(), formManager);
    QCOMPARE(tab->getView()->getFormController()->getView(), tab->getView());
    QCOMPARE(tab->getView()->getFormController()->getDocument(), tab->getDocument());
    QCOMPARE(tab->getView()->getFormController()->getAnnotationController(), tab->getView()->getAnnotationController());

    // Every field of the sample is in the form
    QStringList names;
    formManager->getForm()->apply([&names](const pdf::PDFFormField* field) { names << field->getName(pdf::PDFFormField::Partial); });
    for (const char* name : { "name", "notes", "code", "secret", "fixed", "required", "total", "agree", "choice", "fruit", "colors", "reset", "signature" })
    {
        QVERIFY2(names.contains(QString::fromLatin1(name)), name);
    }
}

void AppFormsTest::readOnlyFormWaveZero()
{
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFWidgetFormManager* formManager = tab->getView()->getFormManager();
    pdf::PDFAppAnnotationController* controller = tab->getView()->getAnnotationController();

    QVERIFY(controller->isFormFillingAllowed());
    QVERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));

    // Locked (O20): every field read only
    tab->getDocument()->setLocked(true);
    QVERIFY(!controller->isFormFillingAllowed());
    QVERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    tab->getDocument()->setLocked(false);
    QVERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));

    // No permission to fill the form
    pdf::PDFAppDocumentTab* restricted = fixture.open(pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(restricted);
    QVERIFY(restricted->getView()->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));

    // A change of the settings of the forms (the view rebuilds the flags) keeps
    // the read-only form of a locked document and of the restricted document
    tab->getDocument()->setLocked(true);
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::formsAllowEditReadOnly(), true).isError());
    QVERIFY(!fixture.context->getSettings().setValue(pdf::PDFSettingKeys::formsHighlightFields(), false).isError());
    QTRY_VERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::EditReadOnlyFields));
    QVERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    QVERIFY(restricted->getView()->getFormManager()->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
    tab->getDocument()->setLocked(false);
    QVERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::ReadOnlyForm));
}

void AppFormsTest::textFieldCommit()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("John"));
    QVERIFY(!document->hasPendingEdits());
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::save()));

    // A click opens the editor: an edit is pending (Save and Undo are enabled)
    QVERIFY(H::click(tab, QStringLiteral("name")));
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("name"));
    QVERIFY(H::controller(tab)->getFocusedWidget().isValid());
    QVERIFY(H::controller(tab)->hasPendingEdits());
    QVERIFY(document->hasPendingEdits());
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::save()));
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::undo()));
    QVERIFY(!document->isModified());

    // The editor selects the whole text: typing replaces it; Enter commits it as one step
    H::type(tab, QStringLiteral("Jane"));
    QCOMPARE(H::undoCount(tab), 0);
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Jane"));
    QCOMPARE(H::undoCount(tab), 1);
    QCOMPARE(document->getUndoStack()->getUndoDescription(), QStringLiteral("Fill form field"));
    QVERIFY(document->isModified());
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QVERIFY(!document->hasPendingEdits());

    // The same value again is no step
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Jane"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::undoCount(tab), 1);

    // A multi-line field takes Enter as a new line: a click elsewhere commits it
    QVERIFY(H::click(tab, QStringLiteral("notes")));
    H::type(tab, QStringLiteral("Line 1"));
    H::key(tab, Qt::Key_Return);
    H::typeMore(tab, QStringLiteral("Line 2"));
    QCOMPARE(H::undoCount(tab), 1);
    QVERIFY(H::clickOutside(tab));
    QCOMPARE(H::undoCount(tab), 2);
    QVERIFY(H::value(tab, QStringLiteral("notes")).contains(QStringLiteral("Line 2")));
    QVERIFY(!document->hasPendingEdits());

    // A password is never written to the document: its editor is no pending edit
    QVERIFY(H::click(tab, QStringLiteral("secret")));
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("secret"));
    QVERIFY(!document->hasPendingEdits());
    H::type(tab, QStringLiteral("hidden"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::undoCount(tab), 2);
    QVERIFY(H::value(tab, QStringLiteral("secret")).isEmpty());

    // A comb field keeps at most MaxLen characters
    QVERIFY(H::click(tab, QStringLiteral("code")));
    H::type(tab, QStringLiteral("123456789"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("code")), QStringLiteral("123456"));
    QCOMPARE(H::undoCount(tab), 3);
}

void AppFormsTest::textFieldEscReverts()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Typed"));
    H::key(tab, Qt::Key_Escape);
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("John"));
    QCOMPARE(H::undoCount(tab), 0);
    QVERIFY(!tab->getDocument()->hasPendingEdits());
    QVERIFY(!tab->getDocument()->isModified());

    // A click elsewhere after Esc commits nothing
    QVERIFY(H::clickOutside(tab));
    QCOMPARE(H::undoCount(tab), 0);
}

void AppFormsTest::checkBoxRadio()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    // A click is committed at once (no pending edit)
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Off"));
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Yes"));
    QCOMPARE(H::undoCount(tab), 1);
    QVERIFY(!tab->getDocument()->hasPendingEdits());

    // Space on the focused check box
    H::key(tab, Qt::Key_Space);
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Off"));
    QCOMPARE(H::undoCount(tab), 2);

    // Radio group: B, then undo back to A
    QCOMPARE(H::value(tab, QStringLiteral("choice")), QStringLiteral("A"));
    QVERIFY(H::click(tab, QStringLiteral("choice"), 1));
    QCOMPARE(H::value(tab, QStringLiteral("choice")), QStringLiteral("B"));
    QCOMPARE(H::undoCount(tab), 3);
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QCOMPARE(H::value(tab, QStringLiteral("choice")), QStringLiteral("A"));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::redo()));
    QCOMPARE(H::value(tab, QStringLiteral("choice")), QStringLiteral("B"));
}

void AppFormsTest::comboAndListBox()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    // Editable combo box: a typed value, Enter commits
    QVERIFY(H::click(tab, QStringLiteral("fruit")));
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("fruit"));
    QVERIFY(tab->getDocument()->hasPendingEdits());
    H::type(tab, QStringLiteral("Kiwi"));
    H::key(tab, Qt::Key_Return);
    QVERIFY(H::clickOutside(tab));
    QCOMPARE(H::value(tab, QStringLiteral("fruit")), QStringLiteral("Kiwi"));
    QCOMPARE(H::undoCount(tab), 1);

    // List box: the arrows select an item, a click elsewhere commits it
    QVERIFY(H::click(tab, QStringLiteral("colors")));
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("colors"));
    QVERIFY(tab->getDocument()->hasPendingEdits());
    H::key(tab, Qt::Key_Down);
    H::key(tab, Qt::Key_Down);
    QVERIFY(H::clickOutside(tab));
    QVERIFY(!H::value(tab, QStringLiteral("colors")).isEmpty());
    QCOMPARE(H::undoCount(tab), 2);
    QVERIFY(!tab->getDocument()->hasPendingEdits());

    // Undo both
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QCOMPARE(H::value(tab, QStringLiteral("fruit")), QStringLiteral("Apple"));
    QVERIFY(H::value(tab, QStringLiteral("colors")).isEmpty());
}

void AppFormsTest::pushButtonResetForm()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Jane"));
    H::key(tab, Qt::Key_Return);
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::undoCount(tab), 2);

    // The push button resets the form: one step with its own description
    QVERIFY(H::click(tab, QStringLiteral("reset")));
    QTRY_COMPARE(H::undoCount(tab), 3);
    QCOMPARE(tab->getDocument()->getUndoStack()->getUndoDescription(), QStringLiteral("Reset form"));
    QVERIFY(H::value(tab, QStringLiteral("agree")) != QStringLiteral("Yes"));
    QVERIFY(H::value(tab, QStringLiteral("name")) != QStringLiteral("Jane"));

    // The description does not stay for the next commit
    QVERIFY(tab->getView()->getAnnotationController()->getPendingDescription().isEmpty());
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(tab->getDocument()->getUndoStack()->getUndoDescription(), QStringLiteral("Fill form field"));

    // Undo of the reset gives the values back
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Jane"));
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Yes"));

    // A reset, which changes nothing, leaves no description behind
    pdf::PDFAppDocumentTab* clean = H::openReady(fixture, pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("NoForm.pdf"), 1));
    QVERIFY(clean);
    const pdf::PDFObjectStorage storage;
    const pdf::PDFActionPtr action = pdf::PDFAction::parse(&storage, FormsTestHelper::dictionary({ { "S", FormsTestHelper::name("ResetForm") } }));
    QVERIFY(action && action->getType() == pdf::ActionType::ResetForm);
    Q_EMIT clean->getView()->getFormManager()->actionTriggered(action.data());
    fixture.settle();
    QVERIFY(clean->getView()->getAnnotationController()->getPendingDescription().isEmpty());
}

void AppFormsTest::reviewResetFormByLink()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Jane"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::undoCount(tab), 1);

    // A ResetForm action of a link annotation (the annotation manager, not a push
    // button of the form manager): one undo step with its own description
    const pdf::PDFObjectStorage storage;
    const pdf::PDFActionPtr reset = pdf::PDFAction::parse(&storage, H::dictionary({ { "S", H::name("ResetForm") } }));
    QVERIFY(reset && reset->getType() == pdf::ActionType::ResetForm);
    Q_EMIT tab->getView()->getAnnotationManager()->actionTriggered(reset.data());
    QTRY_COMPARE(H::undoCount(tab), 2);
    QCOMPARE(tab->getDocument()->getUndoStack()->getUndoDescription(), QStringLiteral("Reset form"));
    QVERIFY(H::value(tab, QStringLiteral("name")) != QStringLiteral("Jane"));
    QVERIFY(tab->getView()->getAnnotationController()->getPendingDescription().isEmpty());

    // The description is not left for the next change
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(tab->getDocument()->getUndoStack()->getUndoDescription(), QStringLiteral("Fill form field"));
}

void AppFormsTest::reviewResetFormWithoutChange()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFWidgetFormManager* formManager = tab->getView()->getFormManager();
    QVERIFY(formManager);
    const pdf::PDFObjectStorage storage;
    const pdf::PDFActionPtr reset = pdf::PDFAction::parse(&storage, H::dictionary({ { "S", H::name("ResetForm") } }));
    QVERIFY(reset && reset->getType() == pdf::ActionType::ResetForm);
    const pdf::PDFActionResetForm* resetAction = static_cast<const pdf::PDFActionResetForm*>(reset.data());

    // The values of the sample are not their defaults: the reset is one step
    QVERIFY(pdf::PDFAppFormController::isResetEffective(formManager, resetAction));
    QVERIFY(H::click(tab, QStringLiteral("reset")));
    QTRY_COMPARE(H::undoCount(tab), 1);
    const pdf::PDFDocument* resetDocument = tab->getDocument()->getDocument().data();

    // A form at its defaults: no change, no undo step (review RC-CD) - the push
    // button and the action of a link annotation
    QVERIFY(!pdf::PDFAppFormController::isResetEffective(formManager, resetAction));
    QVERIFY(H::click(tab, QStringLiteral("reset")));
    fixture.settle();
    QCOMPARE(H::undoCount(tab), 1);
    Q_EMIT tab->getView()->getAnnotationManager()->actionTriggered(reset.data());
    fixture.settle();
    QCOMPARE(H::undoCount(tab), 1);
    QCOMPARE(tab->getDocument()->getDocument().data(), resetDocument);
    QVERIFY(tab->getView()->getAnnotationController()->getPendingDescription().isEmpty());

    // One field changed: the reset is a step again; a check box checked by the
    // user (another appearance state) too
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::undoCount(tab), 2);
    QVERIFY(pdf::PDFAppFormController::isResetEffective(formManager, resetAction));
    QVERIFY(H::click(tab, QStringLiteral("reset")));
    QTRY_COMPARE(H::undoCount(tab), 3);
    QCOMPARE(tab->getDocument()->getUndoStack()->getUndoDescription(), QStringLiteral("Reset form"));
    QVERIFY(!pdf::PDFAppFormController::isResetEffective(formManager, resetAction));

    // Nothing for no form manager, no action and a read-only form
    QVERIFY(!pdf::PDFAppFormController::isResetEffective(nullptr, resetAction));
    QVERIFY(!pdf::PDFAppFormController::isResetEffective(formManager, nullptr));
}

void AppFormsTest::reviewPendingEditsCommitPoints()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler);

    // Print: the dialog takes the focus (the editor stays open) - the typed value
    // is committed first, so it is printed
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Printed"));
    QVERIFY(tab->getDocument()->hasPendingEdits());
    QCOMPARE(H::undoCount(tab), 0);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::print()));
    QDialog* printDialog = nullptr;
    for (QWidget* topLevel : QApplication::topLevelWidgets())
    {
        if (topLevel->isVisible() && QByteArray(topLevel->metaObject()->className()) == QByteArray("pdf::PDFAppPrintDialog"))
        {
            printDialog = qobject_cast<QDialog*>(topLevel);
        }
    }
    QVERIFY(printDialog);
    QCOMPARE(H::undoCount(tab), 1);
    QVERIFY(!tab->getDocument()->hasPendingEdits());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Printed"));
    printDialog->reject();
    fixture.settle();

    // E-mail: the committed value makes the document modified - it asks to save first
    pdf::PDFAppSendMail::setAvailableOverride(true);
    int launches = 0;
    pdf::PDFAppSendMail::setLauncher([&launches](const QString&, const QString&)
    {
        ++launches;
        return pdf::PDFAppMailResult::Created;
    });
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::save()));
    QTRY_VERIFY(!tab->getDocument()->isModified());
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Mailed"));
    QVERIFY(tab->getDocument()->hasPendingEdits());
    QVERIFY(!tab->getDocument()->isModified());
    handler->getSendMail()->send();
    QVERIFY(!tab->getDocument()->hasPendingEdits());
    QVERIFY(tab->getDocument()->isModified());
    QVERIFY(handler->getSendMail()->getDialog());
    handler->getSendMail()->getDialog()->reject();
    fixture.settle();
    QCOMPARE(launches, 0);
    pdf::PDFAppSendMail::setLauncher(pdf::PDFAppSendMail::Launcher());
    pdf::PDFAppSendMail::setAvailableOverride(std::nullopt);

    // Edit a Copy: the copy has the typed value
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Copied"));
    QVERIFY(tab->getDocument()->hasPendingEdits());
    const int tabCount = int(fixture.window->getDocumentTabs().size());
    fixture.window->getCommandAction(pdf::PDFAppCommands::editCopy())->trigger();
    fixture.settle();
    QCOMPARE(int(fixture.window->getDocumentTabs().size()), tabCount + 1);
    QVERIFY(!tab->getDocument()->hasPendingEdits());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Copied"));
    pdf::PDFAppDocumentTab* copy = fixture.currentDocumentTab();
    QVERIFY(copy && copy != tab);
    QCOMPARE(H::value(copy, QStringLiteral("name")), H::value(tab, QStringLiteral("name")));
}

void AppFormsTest::highlightSettings()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFWidgetFormManager* formManager = H::formManager(tab);
    QVERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QVERIFY(formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightRequiredFields));

    // The field "name" is tinted by the highlight, the required field by its own tint
    const QPointF namePoint = H::center(tab, QStringLiteral("name")) + QPointF(70, 0);
    const QPointF requiredPoint = H::center(tab, QStringLiteral("required")) + QPointF(50, 0);
    tab->getView()->getProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    fixture.settle();
    QTRY_VERIFY(H::canvasPixel(tab, namePoint).isValid());
    fixture.settle();
    const QColor highlighted = H::canvasPixel(tab, namePoint);
    const QColor requiredHighlighted = H::canvasPixel(tab, requiredPoint);
    QVERIFY(highlighted != requiredHighlighted);

    // Off: the canvas is repainted at once (no reopening, no new compilation)
    H::setSetting(fixture, pdf::PDFSettingKeys::formsHighlightFields(), false);
    QTRY_VERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightFields));
    QTRY_VERIFY(H::canvasPixel(tab, namePoint) != highlighted);
    const QColor plain = H::canvasPixel(tab, namePoint);
    QCOMPARE(H::canvasPixel(tab, requiredPoint), requiredHighlighted);

    H::setSetting(fixture, pdf::PDFSettingKeys::formsHighlightRequiredFields(), false);
    QTRY_VERIFY(!formManager->getAppearanceFlags().testFlag(pdf::PDFFormManager::HighlightRequiredFields));
    QTRY_COMPARE(H::canvasPixel(tab, requiredPoint), plain);

    // On again
    H::setSetting(fixture, pdf::PDFSettingKeys::formsHighlightFields(), true);
    QTRY_COMPARE(H::canvasPixel(tab, namePoint), highlighted);
    QCOMPARE(pdf::PDFAppFormController::getSettingsFlags(fixture.context->getSettings()), pdf::PDFFormManager::FormAppearanceFlags(pdf::PDFFormManager::HighlightFields));

    // A change of the settings with an open editor commits it (the editors are created again)
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Settings"));
    H::setSetting(fixture, pdf::PDFSettingKeys::formsHighlightRequiredFields(), true);
    QTRY_COMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Settings"));
    QCOMPARE(H::undoCount(tab), 1);
    QVERIFY(!tab->getDocument()->hasPendingEdits());

    // A read-only form (locked) has no field to fill: no highlight
    QTRY_COMPARE(H::canvasPixel(tab, namePoint), highlighted);
    tab->getDocument()->setLocked(true);
    QTRY_COMPARE(H::canvasPixel(tab, namePoint), plain);
    QCOMPARE(H::canvasPixel(tab, requiredPoint), plain);
    tab->getDocument()->setLocked(false);
    QTRY_COMPARE(H::canvasPixel(tab, namePoint), highlighted);
}

void AppFormsTest::readOnlyField()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    QVERIFY(H::click(tab, QStringLiteral("fixed")));
    H::type(tab, QStringLiteral("Changed"));
    H::key(tab, Qt::Key_Return);
    QVERIFY(H::clickOutside(tab));
    QCOMPARE(H::value(tab, QStringLiteral("fixed")), QStringLiteral("Read only"));
    QCOMPARE(H::undoCount(tab), 0);
    QVERIFY(!tab->getDocument()->isModified());
}

void AppFormsTest::allowEditReadOnlySetting()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    H::setSetting(fixture, pdf::PDFSettingKeys::formsAllowEditReadOnly(), true);
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(H::formManager(tab)->getAppearanceFlags().testFlag(pdf::PDFFormManager::EditReadOnlyFields));

    QVERIFY(H::click(tab, QStringLiteral("fixed")));
    H::type(tab, QStringLiteral("Changed"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("fixed")), QStringLiteral("Changed"));
    QCOMPARE(H::undoCount(tab), 1);

    // Off again (live): the field is read only
    H::setSetting(fixture, pdf::PDFSettingKeys::formsAllowEditReadOnly(), false);
    QTRY_VERIFY(!H::formManager(tab)->getAppearanceFlags().testFlag(pdf::PDFFormManager::EditReadOnlyFields));
    QVERIFY(H::click(tab, QStringLiteral("fixed")));
    H::type(tab, QStringLiteral("Again"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("fixed")), QStringLiteral("Changed"));
    QCOMPARE(H::undoCount(tab), 1);
}

void AppFormsTest::lockedDocumentReadOnly()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Locking with an open editor: the typed value is committed first
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Before lock"));
    QVERIFY(document->hasPendingEdits());
    document->setLocked(true);
    fixture.settle();
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Before lock"));
    QCOMPARE(H::undoCount(tab), 1);
    QVERIFY(!document->hasPendingEdits());
    QVERIFY(H::focusedFieldName(tab).isEmpty());

    // Locked: no editor, no change, the calculated fields are not unlocked
    QVERIFY(H::click(tab, QStringLiteral("name")));
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    H::type(tab, QStringLiteral("Locked"));
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Off"));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Before lock"));
    QCOMPARE(H::undoCount(tab), 1);
    QVERIFY(!H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::undo()));
    H::key(tab, Qt::Key_Tab);
    QVERIFY(H::focusedFieldName(tab).isEmpty());

    // Unlocked: filling works again, the bar is back
    document->setLocked(false);
    fixture.settle();
    QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Yes"));
    QCOMPARE(H::undoCount(tab), 2);
}

void AppFormsTest::fillFormPermission()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;

    // No permission (bits 6 and 9 cleared): a read-only form
    pdf::PDFAppDocumentTab* restricted = H::openReady(fixture, pdftest::AppDocumentSamples::restricted(fixture.folder()));
    QVERIFY(restricted);
    QVERIFY(H::formManager(restricted)->isReadOnlyForm());
    QVERIFY(H::click(restricted, QStringLiteral("name")));
    QVERIFY(H::focusedFieldName(restricted).isEmpty());
    H::type(restricted, QStringLiteral("X"));
    QCOMPARE(H::value(restricted, QStringLiteral("name")), QStringLiteral("John"));
    QCOMPARE(H::undoCount(restricted), 0);

    // Bit 9 without bit 6: the form is filled, annotations are not changed
    pdf::PDFAppDocumentTab* fillOnly = H::openReady(fixture, FormsTestHelper::fillOnlyForm(fixture.folder()));
    QVERIFY(fillOnly);
    pdf::PDFAppAnnotationController* controller = fillOnly->getView()->getAnnotationController();
    QVERIFY(controller->isFormFillingAllowed());
    QVERIFY(!controller->isAnnotationEditingAllowed());
    QVERIFY(!H::formManager(fillOnly)->isReadOnlyForm());
    QVERIFY(H::click(fillOnly, QStringLiteral("name")));
    H::type(fillOnly, QStringLiteral("Filled"));
    H::key(fillOnly, Qt::Key_Return);
    QCOMPARE(H::value(fillOnly, QStringLiteral("name")), QStringLiteral("Filled"));
    QCOMPARE(H::undoCount(fillOnly), 1);
}

void AppFormsTest::undoRedoForm()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);

    for (const QString& text : { QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three") })
    {
        QVERIFY(H::click(tab, QStringLiteral("name")));
        H::type(tab, text);
        H::key(tab, Qt::Key_Return);
    }
    QCOMPARE(H::undoCount(tab), 3);

    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Two"));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("John"));
    QVERIFY(!tab->getDocument()->isModified());
    QVERIFY(!H::execute(tab, pdf::PDFAppCommands::undo()));

    QVERIFY(H::execute(tab, pdf::PDFAppCommands::redo()));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::redo()));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Two"));

    // The editor shows the value of the version after undo
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::key(tab, Qt::Key_End);
    H::typeMore(tab, QStringLiteral("!"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Two!"));
    QVERIFY(!tab->getDocument()->getUndoStack()->canRedo());
}

void AppFormsTest::undoClosesEditor()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocument* document = tab->getDocument();

    // Undo with an open editor and nothing else to undo: the typed value is
    // committed and undone (Redo gives it back)
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Typed"));
    QVERIFY(!document->getUndoStack()->canUndo());
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::undo()));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QVERIFY(!document->hasPendingEdits());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("John"));
    QCOMPARE(document->getUndoStack()->getUndoCount(), size_t(0));
    QCOMPARE(document->getUndoStack()->getRedoCount(), size_t(1));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::redo()));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Typed"));

    // An open editor without a change: undo undoes the previous step
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::undoCount(tab), 2);
    QVERIFY(H::click(tab, QStringLiteral("name")));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Off"));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Typed"));
    QCOMPARE(H::undoCount(tab), 1);

    // Redo with an open editor, which has a change: the change is a new step (no redo)
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Other"));
    QVERIFY(document->getUndoStack()->canRedo());
    QVERIFY(!document->getUndoStack()->redo());
    fixture.settle();
    QVERIFY(!document->hasPendingEdits());
    QVERIFY(!document->getUndoStack()->canRedo());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Other"));
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Off"));

    // Undo directly by the stack (MCP, a test) also closes the editor first
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Stack"));
    QVERIFY(document->getUndoStack()->undo());
    fixture.settle();
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Other"));
    QVERIFY(document->getUndoStack()->redo());
    fixture.settle();
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Stack"));
}

void AppFormsTest::pendingEditsSaved()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString file = pdftest::AppDocumentSamples::fullForm(fixture.folder());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QVERIFY(handler);

    // Save with an open editor (the document has no other change): the typed value
    // is committed and written
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Saved"));
    QVERIFY(!tab->getDocument()->isModified());
    QVERIFY(handler->isEnabled(pdf::PDFAppCommands::save()));
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::save()));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());
    QVERIFY(!tab->getDocument()->isModified());
    QVERIFY(!tab->getDocument()->hasPendingEdits());
    QCOMPARE(H::undoCount(tab), 1);

    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocument saved = reader.readFromFile(file);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    const pdf::PDFForm form = pdf::PDFForm::parse(&saved, saved.getCatalog()->getFormObject());
    QString savedValue;
    form.apply([&saved, &savedValue](const pdf::PDFFormField* field)
    {
        if (field->getName(pdf::PDFFormField::Partial) == QStringLiteral("name"))
        {
            savedValue = pdf::PDFDocumentDataLoaderDecorator(&saved).readTextString(field->getValue(), QString());
        }
    });
    QCOMPARE(savedValue, QStringLiteral("Saved"));
}

void AppFormsTest::pendingEditsOnClose()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> guard(tab);

    // Closing the tab with an open editor asks for the typed value (it is a change)
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Closing"));
    QVERIFY(!tab->getDocument()->isModified());
    bool isAsked = false;
    H::onDialog<pdf::PDFAppCloseUnsavedDialog>([&isAsked](pdf::PDFAppCloseUnsavedDialog* dialog)
    {
        isAsked = true;
        dialog->reject();
    });
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(isAsked);
    QVERIFY(guard);
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Closing"));
    QCOMPARE(H::undoCount(tab), 1);

    // Quitting with an open editor in another tab asks too
    pdf::PDFAppDocumentTab* second = H::openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(second);
    QVERIFY(H::click(second, QStringLiteral("name")));
    H::type(second, QStringLiteral("Quit"));
    QVERIFY(!second->getDocument()->isModified());
    bool isQuitAsked = false;
    H::onDialog<pdf::PDFAppQuitUnsavedDialog>([&isQuitAsked](pdf::PDFAppQuitUnsavedDialog* dialog)
    {
        isQuitAsked = true;
        dialog->reject();
    });
    QVERIFY(!fixture.window->close());
    QVERIFY(isQuitAsked);
    QVERIFY(second->getDocument()->isModified());
    QCOMPARE(H::value(second, QStringLiteral("name")), QStringLiteral("Quit"));

    // Don't Save closes the tab
    H::onDialog<pdf::PDFAppCloseUnsavedDialog>([](pdf::PDFAppCloseUnsavedDialog* dialog)
    {
        dialog->getChoiceButton(pdf::PDFAppDocumentChoice::Discard)->click();
    });
    QVERIFY(fixture.window->closeTab(tab));
    fixture.settle();
    QVERIFY(!guard);
}

void AppFormsTest::focusOutCommits()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QWidget* canvas = H::canvas(tab);
    canvas->setFocus(Qt::MouseFocusReason);

    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Focus"));

    // Another window (a popup, a menu, the activation) keeps the editor open
    QFocusEvent windowFocus(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QCoreApplication::sendEvent(canvas, &windowFocus);
    QFocusEvent popupFocus(QEvent::FocusOut, Qt::PopupFocusReason);
    QCoreApplication::sendEvent(canvas, &popupFocus);
    fixture.settle();
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("name"));
    QCOMPARE(H::undoCount(tab), 0);

    // The focus goes to another widget of the window: the field is committed
    QFocusEvent tabFocus(QEvent::FocusOut, Qt::TabFocusReason);
    QCoreApplication::sendEvent(canvas, &tabFocus);
    fixture.settle();
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Focus"));
    QCOMPARE(H::undoCount(tab), 1);

    // closeEditor without an editor does nothing
    QVERIFY(!H::controller(tab)->closeEditor());
}

void AppFormsTest::calculatedFieldsInfoBar()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();

    // The calculated field is unlocked; the bar says it and replaces the bar of the scripts
    QVERIFY(H::formManager(tab)->hasUnlockedCalculatedFields());
    QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    QVERIFY(!H::hasBar(tab, pdf::PDFAppDocumentActionHandler::getScriptsBarId()));
    const pdf::PDFAppInfoBarRequest* bar = view->findInfoBar(pdf::PDFAppFormController::getCalculatedFieldsBarId());
    QCOMPARE(bar->title, QStringLiteral("Calculated fields are not updated"));
    QVERIFY(!bar->text.isEmpty());
    QCOMPARE(bar->tone, pdf::PDFAppInfoBarTone::Information);
    QVERIFY(bar->isClosable);

    // The calculated field can be filled
    QVERIFY(H::click(tab, QStringLiteral("total")));
    H::type(tab, QStringLiteral("42"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("total")), QStringLiteral("42"));
    QCOMPARE(H::undoCount(tab), 1);

    // Closed by the user: it stays closed (also over undo and the lock)
    pdf::PDFAppInfoBar* barWidget = view->findInfoBarWidget(pdf::PDFAppFormController::getCalculatedFieldsBarId());
    QVERIFY(barWidget && barWidget->getCloseButton());
    barWidget->getCloseButton()->click();
    fixture.settle();
    QVERIFY(!H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    tab->getDocument()->setLocked(true);
    tab->getDocument()->setLocked(false);
    fixture.settle();
    QVERIFY(!H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));

    // A form without calculated fields has no bar; a signed form unlocks no field
    pdf::PDFAppDocumentTab* plain = H::openReady(fixture, pdftest::AppDocumentSamples::form(fixture.folder()));
    QVERIFY(plain);
    QVERIFY(!H::hasBar(plain, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    QVERIFY(!H::hasBar(plain, pdf::PDFAppFormController::getXFABarId()));

    // Another button of the bar of another owner is not a closing
    pdf::PDFAppDocumentTab* again = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder() + QStringLiteral("/again")));
    QVERIFY(again);
    QTRY_VERIFY(H::hasBar(again, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    Q_EMIT again->getView()->infoBarActionTriggered(pdf::PDFAppFormController::getCalculatedFieldsBarId(), QStringLiteral("other"));
    Q_EMIT again->getView()->infoBarActionTriggered(QStringLiteral("unknown"), QString());
    again->getDocument()->setLocked(true);
    fixture.settle();
    QVERIFY(!H::hasBar(again, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    again->getDocument()->setLocked(false);
    QTRY_VERIFY(H::hasBar(again, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
}

void AppFormsTest::xfaInfoBar()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, FormsTestHelper::xfaForm(fixture.folder()));
    QVERIFY(tab);
    QVERIFY(H::formManager(tab)->hasXFAForm());
    QVERIFY(pdf::PDFAppFormController::isXFAForm(tab->getDocument()->getDocument().data()));
    QVERIFY(!pdf::PDFAppFormController::isXFAForm(nullptr));

    // Shown, not filled
    QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getXFABarId()));
    QCOMPARE(tab->getView()->findInfoBar(pdf::PDFAppFormController::getXFABarId())->title, QStringLiteral("This form cannot be filled in"));
    QVERIFY(!tab->getView()->getAnnotationController()->isFormFillingAllowed());
    QVERIFY(H::formManager(tab)->isReadOnlyForm());
    QVERIFY(H::click(tab, QStringLiteral("name")));
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    H::type(tab, QStringLiteral("X"));
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("John"));
    QCOMPARE(H::undoCount(tab), 0);

    // The settings keep the read-only form of the XFA form
    H::setSetting(fixture, pdf::PDFSettingKeys::formsAllowEditReadOnly(), true);
    QTRY_VERIFY(H::formManager(tab)->getAppearanceFlags().testFlag(pdf::PDFFormManager::EditReadOnlyFields));
    QVERIFY(H::formManager(tab)->isReadOnlyForm());

    // Closed: stays closed
    pdf::PDFAppInfoBar* barWidget = tab->getView()->findInfoBarWidget(pdf::PDFAppFormController::getXFABarId());
    QVERIFY(barWidget);
    barWidget->getCloseButton()->click();
    fixture.settle();
    tab->getDocument()->setLocked(true);
    tab->getDocument()->setLocked(false);
    fixture.settle();
    QVERIFY(!H::hasBar(tab, pdf::PDFAppFormController::getXFABarId()));
}

void AppFormsTest::keyboardTab()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    H::canvas(tab)->setFocus(Qt::MouseFocusReason);

    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Tabbed"));

    // Tab commits the field and moves to the next one, Shift+Tab back
    H::key(tab, Qt::Key_Tab);
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Tabbed"));
    QCOMPARE(H::undoCount(tab), 1);
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("notes"));
    QVERIFY(tab->getDocument()->hasPendingEdits());
    H::key(tab, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("name"));

    // Shift+Tab before the first field leaves the canvas to the previous widget of the
    // window - the keyboard is never trapped in the form (O20, WCAG 2.1.2)
    QWidget* window = H::canvas(tab)->window();
    for (int i = 0; i < 40 && !H::focusedFieldName(tab).isEmpty(); ++i)
    {
        H::key(tab, Qt::Key_Backtab, Qt::ShiftModifier);
    }
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QVERIFY(window->focusWidget());
    QVERIFY(window->focusWidget() != H::canvas(tab));
    QVERIFY(!H::canvas(tab)->isAncestorOf(window->focusWidget()));

    // Every field, which can be filled, is reached by Tab from the canvas in the order
    // of the page (entry Tabs); the read only field is skipped; after the last field
    // the focus leaves the canvas to the next widget of the window
    H::canvas(tab)->setFocus(Qt::TabFocusReason);
    H::key(tab, Qt::Key_Tab);
    QStringList visited;
    for (int i = 0; i < 40 && !H::focusedFieldName(tab).isEmpty(); ++i)
    {
        visited << H::focusedFieldName(tab);
        H::key(tab, Qt::Key_Tab);
    }
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QVERIFY(window->focusWidget());
    QVERIFY(window->focusWidget() != H::canvas(tab));
    QVERIFY(!H::canvas(tab)->isAncestorOf(window->focusWidget()));
    for (const char* name : { "name", "notes", "code", "secret", "required", "total", "agree", "fruit", "colors", "reset" })
    {
        QVERIFY2(visited.contains(QString::fromLatin1(name)), name);
    }
    QVERIFY(!visited.contains(QStringLiteral("fixed")));
    QCOMPARE(visited.size(), int(H::formManager(tab)->getTabOrder().size()));
    QCOMPARE(visited.front(), QStringLiteral("name"));

    // Back in the canvas, Tab starts at the first field again; Esc leaves the field
    H::canvas(tab)->setFocus(Qt::TabFocusReason);
    H::key(tab, Qt::Key_Tab);
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("name"));
    H::key(tab, Qt::Key_Escape);
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QVERIFY(!tab->getDocument()->hasPendingEdits());
    QCOMPARE(H::undoCount(tab), 1);

    // Space toggles the check box focused by the keyboard
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    QCOMPARE(H::undoCount(tab), 2);
    H::key(tab, Qt::Key_Space);
    QCOMPARE(H::value(tab, QStringLiteral("agree")), QStringLiteral("Off"));
    QCOMPARE(H::undoCount(tab), 3);
}

void AppFormsTest::keyboardTabScrolls()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, FormsTestHelper::multiPageForm(fixture.folder()));
    QVERIFY(tab);
    pdf::PDFDrawWidgetProxy* proxy = tab->getView()->getProxy();
    proxy->zoom(4.0);
    fixture.settle();

    // The first field (top of page 1) is focused by the keyboard
    H::canvas(tab)->setFocus(Qt::TabFocusReason);
    proxy->goToPage(1);
    fixture.settle();
    H::key(tab, Qt::Key_Tab);
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("first"));

    // Every field reached by Tab is seen whole in the canvas
    for (const QString& fieldName : { QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third") })
    {
        QCOMPARE(H::focusedFieldName(tab), fieldName);
        const pdf::PDFFormField* field = H::field(tab, fieldName);
        const QRectF rectangle = H::formManager(tab)->getWidgetRectangle(field->getWidgets().front());
        const int pageIndex = fieldName == QStringLiteral("third") ? 1 : 0;
        const pdf::PDFWidgetSnapshot snapshot = proxy->getSnapshot();
        const pdf::PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(pageIndex);
        QVERIFY2(item, qPrintable(fieldName));
        const QRectF deviceRect = item->pageToDeviceMatrix.mapRect(rectangle);
        QVERIFY2(QRectF(H::canvas(tab)->rect()).contains(deviceRect), qPrintable(fieldName));
        H::key(tab, Qt::Key_Tab);
    }

    // After the last field (page 2) the focus leaves the canvas (no keyboard trap)
    QVERIFY(H::focusedFieldName(tab).isEmpty());
    QVERIFY(H::canvas(tab)->window()->focusWidget() != H::canvas(tab));
}

void AppFormsTest::signedDocumentFill()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    const QString file = FormsTestHelper::signedForm(fixture.folder());
    QVERIFY(!file.isEmpty());
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, file);
    QVERIFY(tab);
    const QByteArray original = [&file]() { QFile f(file); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    QVERIFY(!original.isEmpty());

    // A signed form unlocks no calculated field and can be filled
    QVERIFY(!H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("Signed fill"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("Signed fill"));
    QCOMPARE(H::undoCount(tab), 1);

    // Save: the dialog save-signed, Save Incrementally keeps the signed bytes
    pdf::PDFAppDocumentFileCommandHandler* handler = pdf::PDFAppDocumentFileCommandHandler::find(tab);
    QSignalSpy finishedSpy(handler, &pdf::PDFAppDocumentFileCommandHandler::saveFinished);
    QVERIFY(handler->save());
    pdf::PDFMessageBox* dialog = nullptr;
    for (pdf::PDFMessageBox* box : fixture.window->findChildren<pdf::PDFMessageBox*>(QStringLiteral("saveSignedDialog")))
    {
        dialog = box->isVisible() ? box : dialog;
    }
    QVERIFY(dialog);
    QPushButton* saveButton = dialog->findChild<QPushButton*>(QStringLiteral("saveButton"));
    QVERIFY(saveButton);
    saveButton->click();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QVERIFY(finishedSpy.at(0).at(0).toBool());

    QFile savedFile(file);
    QVERIFY(savedFile.open(QIODevice::ReadOnly));
    const QByteArray saved = savedFile.readAll();
    QVERIFY(saved.size() > original.size());
    QVERIFY(saved.startsWith(original));

    // The signature still covers its revision (its bytes are unchanged)
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    const pdf::PDFDocument document = reader.readFromBuffer(saved);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
    const pdf::PDFForm form = pdf::PDFForm::parse(&document, document.getCatalog()->getFormObject());
    pdf::PDFSignatureHandler::Parameters parameters;
    parameters.useSystemCertificateStore = false;
    const std::vector<pdf::PDFSignatureVerificationResult> results = pdf::PDFSignatureHandler::verifySignatures(form, saved, parameters);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY(results.front().isSignatureValid());
}

void AppFormsTest::hostileForm()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, FormsTestHelper::hostileForm(fixture.folder()));
    QVERIFY(tab);
    H::canvas(tab)->setFocus(Qt::TabFocusReason);

    // Tab through every field, type and commit; nothing crashes
    for (int i = 0; i < 30; ++i)
    {
        H::key(tab, Qt::Key_Tab);
        H::type(tab, QStringLiteral("x"));
        H::controller(tab)->ensureFocusedFieldVisible();
    }
    QVERIFY(H::click(tab, QStringLiteral("orphan")));
    H::type(tab, QStringLiteral("orphan value"));
    H::key(tab, Qt::Key_Return);
    QVERIFY(H::click(tab, QStringLiteral("box")));
    QVERIFY(H::click(tab, QStringLiteral("garbage")));
    H::key(tab, Qt::Key_Down);
    QVERIFY(H::clickOutside(tab));

    // Undo everything, the bars are computed without a crash
    while (tab->getDocument()->getUndoStack()->canUndo())
    {
        QVERIFY(H::execute(tab, pdf::PDFAppCommands::undo()));
    }
    tab->getDocument()->setLocked(true);
    tab->getDocument()->setLocked(false);
    fixture.settle();

    // Garbage files of other kinds
    for (const QByteArray& data : { QByteArray("%PDF-1.7\n1 0 obj << /Type /Catalog /AcroForm << /Fields 5 0 R /XFA 9 0 R >> >> endobj\ntrailer << /Root 1 0 R >>\n%%EOF"),
                                    QByteArray("%PDF-1.4\ngarbage"), QByteArray() })
    {
        const QString path = fixture.path(QStringLiteral("garbage-%1.pdf").arg(qHash(data)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        fixture.open(path);
        fixture.closeMessageBoxes();
        fixture.settle();
    }
}

void AppFormsTest::deletionMatrix()
{
    using H = FormsTestHelper;

    // The window deleted with an open editor and a pending update
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QVERIFY(H::click(tab, QStringLiteral("name")));
        H::type(tab, QStringLiteral("Window"));
        QTest::keyClick(H::canvas(tab), Qt::Key_A);
        delete fixture.window.data();
        fixture.settle();
    }

    // The tab deleted with an open editor (the document says nothing is pending)
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QVERIFY(H::click(tab, QStringLiteral("name")));
        H::type(tab, QStringLiteral("Tab"));
        QPointer<pdf::PDFAppDocument> document = tab->getDocument();
        tab->getView()->setDocument(nullptr);
        QVERIFY(!document->hasPendingEdits());
        delete tab;
        fixture.settle();
    }

    // The document deleted with an open editor and a shown bar
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
        QVERIFY(H::click(tab, QStringLiteral("name")));
        H::type(tab, QStringLiteral("Document"));
        delete tab->getDocument();
        fixture.settle();
        QVERIFY(H::controller(tab));
        QVERIFY(!H::controller(tab)->getDocument());
        QVERIFY(!H::controller(tab)->closeEditor() || true);
        H::type(tab, QStringLiteral("x"));
        fixture.settle();
    }

    // The tab closed by the close button of the info bar's slot (deleteLater)
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        QObject::connect(tab->getView(), &pdf::PDFAppDocumentView::infoBarActionTriggered, tab, [guard]() { delete guard.data(); });
        tab->getView()->findInfoBarWidget(pdf::PDFAppFormController::getCalculatedFieldsBarId())->getCloseButton()->click();
        fixture.settle();
        QVERIFY(!guard);
    }
}

void AppFormsTest::deleteFromSignals()
{
    using H = FormsTestHelper;

    // The tab deleted by a slot of the command states, which the controller announces
    // when an editor opens (pending edits)
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        QObject::connect(tab, &pdf::PDFAppTab::commandStatesChanged, fixture.window.data(), [guard]()
        {
            if (guard && guard->getDocument() && guard->getDocument()->hasPendingEdits())
            {
                delete guard.data();
            }
        });
        pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, H::center(tab, QStringLiteral("name")));
        fixture.settle();
        QVERIFY(!guard);
    }

    // The tab deleted (later) by a slot of the change of the document of an undo,
    // which commits the open editor first
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QPointer<pdf::PDFAppDocumentTab> guard(tab);
        QVERIFY(H::click(tab, QStringLiteral("name")));
        H::type(tab, QStringLiteral("Undo"));
        QObject::connect(tab->getDocument(), &pdf::PDFAppDocument::documentChanged, fixture.window.data(), [guard]()
        {
            if (guard)
            {
                guard->deleteLater();
            }
        });
        tab->executeCommand(pdf::PDFAppCommands::undo());
        fixture.settle();
        QVERIFY(!guard);
    }

    // The document deleted by a slot of its request of the pending edits
    {
        pdftest::AppDocumentFixture fixture;
        pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
        QVERIFY(tab);
        QVERIFY(H::click(tab, QStringLiteral("name")));
        H::type(tab, QStringLiteral("Request"));
        QPointer<pdf::PDFAppDocument> document = tab->getDocument();
        QObject::connect(document.data(), &pdf::PDFAppDocument::pendingEditsCommitRequested, fixture.window.data(), [document]() { delete document.data(); },
                         Qt::DirectConnection);

        // The slot of the test runs after the controller (connected later): the
        // undo stack, which belongs to the document, returns at once
        QVERIFY(!document->getUndoStack()->undo());
        QVERIFY(!document);
        fixture.settle();
    }
}

void AppFormsTest::themesAndScales()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        fixture.settle();
        pdf::PDFAppInfoBar* bar = tab->getView()->findInfoBarWidget(pdf::PDFAppFormController::getCalculatedFieldsBarId());
        QVERIFY(bar);
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QImage image(QSize(qCeil(bar->width() * ratio), qCeil(bar->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(ratio);
            image.fill(Qt::transparent);
            bar->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
            QCOMPARE(H::countLeakPixels(image), 0);
        }

        // The field highlight and the editor are drawn in both themes
        QVERIFY(H::click(tab, QStringLiteral("name")));
        const QImage canvas = H::canvas(tab)->grab().toImage();
        QCOMPARE(H::countLeakPixels(canvas), 0);
        H::key(tab, Qt::Key_Escape);
    }
}

void AppFormsTest::rightToLeft()
{
    using H = FormsTestHelper;
    QApplication::setLayoutDirection(Qt::RightToLeft);
    pdftest::AppDocumentFixture fixture(QSize(780, 560));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
    pdf::PDFAppInfoBar* bar = tab->getView()->findInfoBarWidget(pdf::PDFAppFormController::getCalculatedFieldsBarId());
    QVERIFY(bar);
    QVERIFY(bar->isRightToLeft());
    QVERIFY(bar->geometry().intersects(tab->getView()->getCanvasArea()->rect()));

    // Filling works the same; the arrows of a radio button follow the direction
    QVERIFY(H::click(tab, QStringLiteral("name")));
    H::type(tab, QStringLiteral("RTL"));
    H::key(tab, Qt::Key_Return);
    QCOMPARE(H::value(tab, QStringLiteral("name")), QStringLiteral("RTL"));
    QVERIFY(H::click(tab, QStringLiteral("agree")));
    const QString focused = H::focusedFieldName(tab);
    QCOMPARE(focused, QStringLiteral("agree"));
    H::key(tab, Qt::Key_Left);
    const QString afterLeft = H::focusedFieldName(tab);
    QVERIFY(afterLeft != QStringLiteral("agree"));
    H::key(tab, Qt::Key_Right);
    QCOMPARE(H::focusedFieldName(tab), QStringLiteral("agree"));
}

void AppFormsTest::minimalWindow()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture(QSize(720, 480));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, FormsTestHelper::multiPageForm(fixture.folder()));
    QVERIFY(tab);
    tab->getView()->getProxy()->zoom(1.5);
    fixture.settle();
    H::canvas(tab)->setFocus(Qt::TabFocusReason);

    // The field reached by Tab is seen whole in the small canvas too
    for (int i = 0; i < 3; ++i)
    {
        H::key(tab, Qt::Key_Tab);
        const QString fieldName = H::focusedFieldName(tab);
        QVERIFY(!fieldName.isEmpty());
        const pdf::PDFFormField* field = H::field(tab, fieldName);
        const QRectF rectangle = H::formManager(tab)->getWidgetRectangle(field->getWidgets().front());
        const pdf::PDFWidgetSnapshot snapshot = tab->getView()->getProxy()->getSnapshot();
        const pdf::PDFWidgetSnapshot::SnapshotItem* item = snapshot.getPageSnapshot(fieldName == QStringLiteral("third") ? 1 : 0);
        QVERIFY(item);
        QVERIFY2(QRectF(H::canvas(tab)->rect()).contains(item->pageToDeviceMatrix.mapRect(rectangle)), qPrintable(fieldName));
    }
}

void AppFormsTest::languageChange()
{
    using H = FormsTestHelper;
    pdftest::AppDocumentFixture fixture;
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
    QVERIFY(tab);
    QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));

    // The bar is shown again with the texts of the language (a test translator)
    class Translator : public QTranslator
    {
    public:
        virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
        {
            Q_UNUSED(disambiguation);
            Q_UNUSED(n);
            return QByteArray(context) == "pdf::PDFAppFormController" ? QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText)) : QString();
        }

        virtual bool isEmpty() const override { return false; }
    };

    Translator translator;
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    QTRY_COMPARE(tab->getView()->findInfoBar(pdf::PDFAppFormController::getCalculatedFieldsBarId())->title, QStringLiteral("[Calculated fields are not updated]"));
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    QTRY_COMPARE(tab->getView()->findInfoBar(pdf::PDFAppFormController::getCalculatedFieldsBarId())->title, QStringLiteral("Calculated fields are not updated"));
}

void AppFormsTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the form with the bar of the calculated fields
    // and an open editor, the XFA bar - both themes at 1x and 2x, the minimal window
    // and right-to-left
    using H = FormsTestHelper;
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    if (folder.isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }
    QDir().mkpath(folder);

    auto render = [](QWidget* widget, qreal ratio)
    {
        QImage image(QSize(qCeil(widget->width() * ratio), qCeil(widget->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::white);
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren | QWidget::DrawWindowBackground));
        return image;
    };

    struct Variant
    {
        QString name;
        QSize size;
        Qt::LayoutDirection direction;
    };

    for (const Variant& variant : { Variant{ QStringLiteral("normal"), QSize(1440, 960), Qt::LeftToRight },
                                    Variant{ QStringLiteral("minimal"), QSize(720, 480), Qt::LeftToRight },
                                    Variant{ QStringLiteral("rtl"), QSize(1440, 960), Qt::RightToLeft } })
    {
        for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
            QApplication::setLayoutDirection(variant.direction);
            pdftest::AppDocumentFixture fixture(variant.size);
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            fixture.settle();
            pdf::PDFAppDocumentTab* tab = H::openReady(fixture, pdftest::AppDocumentSamples::fullForm(fixture.folder()));
            QVERIFY(tab);
            QTRY_VERIFY(H::hasBar(tab, pdf::PDFAppFormController::getCalculatedFieldsBarId()));
            tab->getView()->getProxy()->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
            fixture.settle();
            QVERIFY(H::click(tab, QStringLiteral("name")));
            H::typeMore(tab, QStringLiteral(" Smith"));

            for (qreal ratio : { 1.0, 2.0 })
            {
                const QString scale = ratio > 1.0 ? QStringLiteral("2x") : QStringLiteral("1x");
                QVERIFY(render(fixture.window, ratio).save(QDir(folder).absoluteFilePath(QStringLiteral("forms-%1-%2-%3.png").arg(variant.name, suffix, scale))));
            }

            pdf::PDFAppDocumentTab* xfa = H::openReady(fixture, FormsTestHelper::xfaForm(fixture.folder()));
            QVERIFY(xfa);
            QTRY_VERIFY(H::hasBar(xfa, pdf::PDFAppFormController::getXFABarId()));
            QVERIFY(render(fixture.window, 1.0).save(QDir(folder).absoluteFilePath(QStringLiteral("forms-xfa-%1-%2.png").arg(variant.name, suffix))));
        }
    }
}

QTEST_MAIN(AppFormsTest)
#include "tst_appformstest.moc"
