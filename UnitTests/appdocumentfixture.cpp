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

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappfiledrop.h"
#include "pdfappnavigator.h"
#include "pdfdatabasenotifier.h"

#include "pdfcertificatemanager.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumentwriter.h"
#include "pdfsecurityhandler.h"

#include "pdfmessagebox.h"
#include "pdfthememanager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QTest>

namespace pdftest
{

/// Building blocks of the samples
class AppDocumentSamplesHelper
{
public:
    AppDocumentSamplesHelper() = delete;

    static pdf::PDFObject name(const char* value)
    {
        return pdf::PDFObject::createName(QByteArray(value));
    }

    static pdf::PDFObject text(const QString& value)
    {
        return pdf::PDFObjectFactory::createTextString(value);
    }

    static pdf::PDFObject reference(pdf::PDFObjectReference value)
    {
        return pdf::PDFObject::createReference(value);
    }

    static pdf::PDFObject integer(pdf::PDFInteger value)
    {
        return pdf::PDFObject::createInteger(value);
    }

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

    /// Destination [page /Fit]
    static pdf::PDFObject fit(pdf::PDFObjectReference page)
    {
        return array({ reference(page), name("Fit") });
    }

    /// Appends the pages with the text of AppDocumentSamples::getPageText
    static std::vector<pdf::PDFObjectReference> appendPages(pdf::PDFDocumentBuilder& builder, int pageCount)
    {
        std::vector<pdf::PDFObjectReference> pages;
        pages.reserve(size_t(qMax(pageCount, 0)));
        for (int i = 0; i < pageCount; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
            pages.push_back(page);

            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            if (!painter)
            {
                continue;
            }
            QFont font = painter->font();
            font.setPixelSize(12);
            painter->setFont(font);
            painter->setPen(Qt::black);
            const QStringList lines = AppDocumentSamples::getPageText(i).split(QLatin1Char('\n'));
            for (int line = 0; line < lines.size(); ++line)
            {
                painter->drawText(QPointF(30, 40 + 20 * line), lines[line]);
            }
            content.end(painter);
        }
        return pages;
    }

    static QString write(pdf::PDFDocumentBuilder& builder, const QString& folder, const QString& fileName)
    {
        pdf::PDFDocument document = builder.build();
        const QString filePath = QDir(folder).absoluteFilePath(fileName);
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static void encrypt(pdf::PDFDocumentBuilder& builder, const QString& userPassword, const QString& ownerPassword, uint32_t permissions)
    {
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.userPassword = userPassword;
        settings.ownerPassword = ownerPassword;
        settings.permissions = permissions;
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
    }
};

QString AppDocumentSamples::getPageText(int pageIndex)
{
    return QStringLiteral("Page %1\nalpha beta gamma %1\nThe quick brown fox jumps over the lazy dog.").arg(pageIndex + 1);
}

bool AppDocumentSamples::write(pdf::PDFDocument& document, const QString& filePath)
{
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    return bool(pdf::PDFDocumentWriter(nullptr).write(filePath, &document, false));
}

QString AppDocumentSamples::simple(const QString& folder, const QString& name, int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, pageCount);
    return AppDocumentSamplesHelper::write(builder, folder, name);
}

QString AppDocumentSamples::pageLabels(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    H::appendPages(builder, 10);

    const pdf::PDFObject labels = H::dictionary({ { "Nums", H::array({ H::integer(0), H::dictionary({ { "S", H::name("r") } }),
                                                                       H::integer(3), H::dictionary({ { "S", H::name("D") } }),
                                                                       H::integer(8), H::dictionary({ { "S", H::name("D") }, { "P", H::text(QStringLiteral("A-")) } }) }) } });
    builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "PageLabels", labels } }));
    return H::write(builder, folder, QStringLiteral("page-labels.pdf"));
}

QString AppDocumentSamples::outline(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 5);

    // References first (the items refer to each other)
    const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference introduction = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference chapter = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference section = builder.addObject(pdf::PDFObject());
    const pdf::PDFObjectReference appendix = builder.addObject(pdf::PDFObject());

    builder.setObject(root, H::dictionary({ { "Type", H::name("Outlines") }, { "First", H::reference(introduction) }, { "Last", H::reference(appendix) }, { "Count", H::integer(4) } }));
    builder.setObject(introduction, H::dictionary({ { "Title", H::text(QStringLiteral("Introduction")) }, { "Parent", H::reference(root) }, { "Next", H::reference(chapter) }, { "Dest", H::fit(pages[0]) } }));
    builder.setObject(chapter, H::dictionary({ { "Title", H::text(QStringLiteral("Chapter 1")) }, { "Parent", H::reference(root) }, { "Prev", H::reference(introduction) }, { "Next", H::reference(appendix) },
                                               { "First", H::reference(section) }, { "Last", H::reference(section) }, { "Count", H::integer(1) }, { "Dest", H::fit(pages[1]) } }));
    builder.setObject(section, H::dictionary({ { "Title", H::text(QStringLiteral("Section 1.1")) }, { "Parent", H::reference(chapter) }, { "Dest", H::fit(pages[2]) } }));
    builder.setObject(appendix, H::dictionary({ { "Title", H::text(QStringLiteral("Appendix")) }, { "Parent", H::reference(root) }, { "Prev", H::reference(chapter) }, { "Dest", H::fit(pages[4]) } }));
    builder.mergeTo(builder.getCatalogReference(), H::dictionary({ { "Outlines", H::reference(root) } }));
    return H::write(builder, folder, QStringLiteral("outline.pdf"));
}

QString AppDocumentSamples::links(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 4);
    const pdf::PDFObjectReference page = pages[0];

    const pdf::PDFObjectReference goTo = builder.createActionGoTo(pdf::PDFDestination::createFit(pages[2]));
    const pdf::PDFObjectReference goToNamed = builder.addObject(H::dictionary({ { "Type", H::name("Action") }, { "S", H::name("GoTo") }, { "D", pdf::PDFObject::createString(QByteArray("chapter2")) } }));
    const pdf::PDFObjectReference launch = builder.createActionLaunch(builder.createFileSpecification(QStringLiteral("notepad.exe")), false);
    const pdf::PDFObjectReference goToRemote = builder.createActionGoToRemote(builder.createFileSpecification(QStringLiteral("other.pdf")), pdf::PDFDestination::createNamed(QByteArray("start")), true);
    const pdf::PDFObjectReference javaScript = builder.createActionJavaScript(QStringLiteral("app.alert('x')"));

    builder.createAnnotationLink(page, QRectF(30, 100, 200, 16), goTo, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 120, 200, 16), goToNamed, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 140, 200, 16), QStringLiteral("https://example.com/"), pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 160, 200, 16), launch, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 180, 200, 16), goToRemote, pdf::LinkHighlightMode::Invert);
    builder.createAnnotationLink(page, QRectF(30, 200, 200, 16), javaScript, pdf::LinkHighlightMode::Invert);

    // Named destination "chapter2" (page 4) in the /Dests name tree
    const pdf::PDFObject dests = H::dictionary({ { "Names", H::array({ pdf::PDFObject::createString(QByteArray("chapter2")), H::fit(pages[3]) }) } });
    const pdf::PDFObjectReference names = builder.addObject(H::dictionary({ { "Dests", dests } }));
    builder.setCatalogNames(names);
    return H::write(builder, folder, QStringLiteral("links.pdf"));
}

QString AppDocumentSamples::attachments(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 2);

    auto createFile = [&](const QString& fileName, const QByteArray& data)
    {
        const pdf::PDFObjectReference stream = builder.addObject(H::stream({ { "Type", H::name("EmbeddedFile") } }, data));
        return builder.addObject(H::dictionary({ { "Type", H::name("Filespec") }, { "F", H::text(fileName) }, { "UF", H::text(fileName) },
                                                 { "EF", H::dictionary({ { "F", H::reference(stream) } }) } }));
    };

    const pdf::PDFObjectReference notes = createFile(QStringLiteral("notes.txt"), QByteArray("Notes of the sample\n"));
    const pdf::PDFObjectReference data = createFile(QStringLiteral("data.csv"), QByteArray("a,b\n1,2\n"));
    builder.createAnnotationFileAttachment(pages[0], QPointF(250, 50), notes, pdf::FileAttachmentIcon::Paperclip, QStringLiteral("Author"), QStringLiteral("Notes"));

    const pdf::PDFObject embeddedFiles = H::dictionary({ { "Names", H::array({ H::text(QStringLiteral("data.csv")), H::reference(data),
                                                                               H::text(QStringLiteral("notes.txt")), H::reference(notes) }) } });
    const pdf::PDFObjectReference names = builder.addObject(H::dictionary({ { "EmbeddedFiles", embeddedFiles } }));
    builder.setCatalogNames(names);
    return H::write(builder, folder, QStringLiteral("attachments.pdf"));
}

QString AppDocumentSamples::layers(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 1);

    const pdf::PDFObjectReference layerA = builder.addObject(H::dictionary({ { "Type", H::name("OCG") }, { "Name", H::text(QStringLiteral("Layer A")) } }));
    const pdf::PDFObjectReference layerB = builder.addObject(H::dictionary({ { "Type", H::name("OCG") }, { "Name", H::text(QStringLiteral("Layer B")) } }));
    const pdf::PDFObject properties = H::dictionary({ { "OCGs", H::array({ H::reference(layerA), H::reference(layerB) }) },
                                                      { "D", H::dictionary({ { "Order", H::array({ H::reference(layerA), H::reference(layerB) }) },
                                                                             { "OFF", H::array({ H::reference(layerB) }) } }) } });
    builder.setCatalogOptionalContentProperties(builder.addObject(properties));

    // A second content stream of the page draws a rectangle in each layer
    const pdf::PDFObjectReference content = builder.addObject(H::stream({ }, QByteArray("/OC /LA BDC 0 0 1 rg 10 10 50 50 re f EMC /OC /LB BDC 1 0 0 rg 70 10 50 50 re f EMC\n")));
    const pdf::PDFObject& pageObject = builder.getObjectByReference(pages[0]);
    const pdf::PDFDictionary* pageDictionary = pageObject.isDictionary() ? pageObject.getDictionary() : nullptr;
    if (pageDictionary)
    {
        pdf::PDFArrayBuilder contents;
        const pdf::PDFObject& existing = pageDictionary->get("Contents");
        if (existing.isArray())
        {
            for (size_t i = 0; i < existing.getArray()->getCount(); ++i)
            {
                contents.appendItem(existing.getArray()->getItem(i));
            }
        }
        else if (!existing.isNull())
        {
            contents.appendItem(existing);
        }
        contents.appendItem(H::reference(content));
        builder.mergeTo(pages[0], H::dictionary({ { "Contents", pdf::PDFObject::createArray(std::move(contents)) },
                                                  { "Resources", H::dictionary({ { "Properties", H::dictionary({ { "LA", H::reference(layerA) }, { "LB", H::reference(layerB) } }) } }) } }));
    }
    return H::write(builder, folder, QStringLiteral("layers.pdf"));
}

QString AppDocumentSamples::form(const QString& folder)
{
    using H = AppDocumentSamplesHelper;

    pdf::PDFDocumentBuilder builder;
    const std::vector<pdf::PDFObjectReference> pages = H::appendPages(builder, 1);

    const pdf::PDFObjectReference field = builder.addObject(H::dictionary({ { "FT", H::name("Tx") }, { "T", H::text(QStringLiteral("name")) }, { "V", H::text(QStringLiteral("John")) },
                                                                           { "DA", pdf::PDFObject::createString(QByteArray("/Helv 12 Tf 0 g")) } }));
    builder.createFormFieldWidget(field, pages[0], pdf::PDFObjectReference(), QRectF(30, 250, 200, 24));
    const pdf::PDFObjectReference acroForm = builder.createAcroForm({ field });
    builder.setCatalogAcroForm(acroForm);
    return H::write(builder, folder, QStringLiteral("form.pdf"));
}

QString AppDocumentSamples::userPassword(const QString& folder)
{
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, 2);
    AppDocumentSamplesHelper::encrypt(builder, getUserPassword(), getOwnerPassword(), 0xFFFFFFFCu);
    return AppDocumentSamplesHelper::write(builder, folder, QStringLiteral("user-password.pdf"));
}

QString AppDocumentSamples::ownerPassword(const QString& folder)
{
    // Printing and copying are not allowed (bits 3 and 5 cleared)
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, 2);
    AppDocumentSamplesHelper::encrypt(builder, QString(), getOwnerPassword(), 0xFFFFFFFCu & ~0x0014u);
    return AppDocumentSamplesHelper::write(builder, folder, QStringLiteral("owner-password.pdf"));
}

QString AppDocumentSamples::signedDocument(const QString& folder, QString* certificatePath)
{
    pdf::PDFCertificateManager::NewCertificateInfo info;
    info.fileName = QDir(folder).absoluteFilePath(QStringLiteral("signature-test.p12"));
    info.privateKeyPasword = getCertificatePassword();
    info.certCommonName = QStringLiteral("PDF4QT document tab test");
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
    if (certificatePath)
    {
        *certificatePath = info.fileName;
    }

    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, 2);
    const pdf::PDFDocument document = builder.build();

    QByteArray signedData;
    pdf::PDFDocumentSigner::Parameters parameters;
    parameters.document = &document;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature)
    {
        return pdf::PDFSignatureFactory::sign(certificate, getCertificatePassword(), data, signature);
    };
    parameters.createSignatureFieldFunction = [&](pdf::PDFDocumentBuilder& signatureBuilder, pdf::PDFObjectReference signatureDictionary)
    {
        return signatureBuilder.createSignatureField(QStringLiteral("Signature1"), signatureDictionary, document.getCatalog()->getPage(0)->getPageReference());
    };
    if (pdf::PDFDocumentSigner::sign(parameters, signedData) != pdf::PDFDocumentSigner::Result::OK)
    {
        return QString();
    }

    const QString filePath = QDir(folder).absoluteFilePath(QStringLiteral("signed.pdf"));
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(signedData) != signedData.size())
    {
        return QString();
    }
    return filePath;
}

QString AppDocumentSamples::large(const QString& folder, int pageCount)
{
    pdf::PDFDocumentBuilder builder;
    AppDocumentSamplesHelper::appendPages(builder, pageCount);
    return AppDocumentSamplesHelper::write(builder, folder, QStringLiteral("large-%1.pdf").arg(pageCount));
}

int AppDocumentFixture::s_counter = 0;

AppDocumentFixture::AppDocumentFixture(QSize size)
{
    const QStringList arguments = { QStringLiteral("PDF4QT"), QStringLiteral("--portable") };
    context = std::make_unique<pdf::PDFAppContext>(pdf::PDFAppCommandLine::parse(arguments, QDir::currentPath()));

    pdf::PDFDatabaseOptions options;
    options.memoryName = QStringLiteral("/pdf4qt-document-%1-%2").arg(QCoreApplication::applicationPid()).arg(++s_counter);
    context->open(options);
    if (context->getNotifier())
    {
        context->getNotifier()->setPollInterval(20);
    }

    window = new pdf::PDFAppMainWindow(context.get());
    window->getNavigator()->setExternalUrlsEnabled(false);
    window->resize(size);
    window->show();
    if (!QTest::qWaitForWindowExposed(window.data()))
    {
        qWarning("The window was not exposed");
    }
    settle();
    closeMessageBoxes();
}

AppDocumentFixture::~AppDocumentFixture()
{
    delete window.data();
    settle();
    context.reset();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFAppFileDialogs::setOpenFilesHandler(pdf::PDFAppFileDialogs::OpenFilesHandler());
    pdf::PDFAppFileDialogs::setFolderHandler(pdf::PDFAppFileDialogs::FolderHandler());
    pdf::PDFAppFileDialogs::setSaveFileHandler(pdf::PDFAppFileDialogs::SaveFileHandler());
}

QString AppDocumentFixture::path(const QString& name) const
{
    return QDir(directory.path()).absoluteFilePath(name);
}

pdf::PDFAppDocumentTab* AppDocumentFixture::open(const QString& filePath, int page)
{
    if (!window)
    {
        return nullptr;
    }

    window->openDocuments(QStringList({ filePath }), page);
    settle();

    // The file is read in a worker thread (package A): the tab is returned, when its
    // document has left the state Loading (shown, a password, or failed and closed)
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    QElapsedTimer timer;
    timer.start();
    while (window && timer.elapsed() < 30000)
    {
        pdf::PDFAppDocumentTab* found = nullptr;
        for (pdf::PDFAppDocumentTab* tab : window->getDocumentTabs())
        {
            if (tab->getDocument() && !canonicalPath.isEmpty() && tab->getDocument()->getFilePath() == canonicalPath)
            {
                found = tab;
            }
        }

        if (!found)
        {
            return nullptr;
        }
        if (found->getDocument()->getState() != pdf::PDFAppDocumentState::Loading)
        {
            settle();
            return found;
        }
        QTest::qWait(5);
    }
    return nullptr;
}

pdf::PDFAppDocumentTab* AppDocumentFixture::currentDocumentTab() const
{
    return window ? qobject_cast<pdf::PDFAppDocumentTab*>(window->getCurrentTab()) : nullptr;
}

void AppDocumentFixture::settle()
{
    for (int i = 0; i < 3; ++i)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

int AppDocumentFixture::closeMessageBoxes()
{
    int count = 0;
    if (!window)
    {
        return count;
    }

    for (pdf::PDFMessageBox* messageBox : window->findChildren<pdf::PDFMessageBox*>())
    {
        if (messageBox->isVisible())
        {
            messageBox->close();
            ++count;
        }
    }
    settle();
    return count;
}

}   // namespace pdftest
