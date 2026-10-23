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

#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfapphomedialogs.h"
#include "pdfappmainwindow.h"

#include "pdfannotation.h"
#include "pdfcatalog.h"
#include "pdfcomponentpanels.h"
#include "pdfexception.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfwidgetstyleproperties.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeDatabase>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of the attachments
class PDFAppAttachmentsHelper
{
public:
    PDFAppAttachmentsHelper() = delete;

    /// Extensions of programs and scripts, which are never opened (D8)
    static const std::set<QString>& getExecutableExtensions()
    {
        static const std::set<QString> extensions =
        {
            // Windows programs, installers, libraries and shortcuts
            QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("scr"), QStringLiteral("pif"), QStringLiteral("cpl"),
            QStringLiteral("dll"), QStringLiteral("sys"), QStringLiteral("drv"), QStringLiteral("ocx"), QStringLiteral("msi"),
            QStringLiteral("msp"), QStringLiteral("mst"), QStringLiteral("msc"), QStringLiteral("lnk"), QStringLiteral("url"),
            QStringLiteral("scf"), QStringLiteral("inf"), QStringLiteral("reg"), QStringLiteral("gadget"), QStringLiteral("application"),
            QStringLiteral("appref-ms"), QStringLiteral("appx"), QStringLiteral("appxbundle"), QStringLiteral("msix"), QStringLiteral("msixbundle"),
            QStringLiteral("hta"), QStringLiteral("chm"), QStringLiteral("library-ms"), QStringLiteral("settingcontent-ms"), QStringLiteral("diagcab"),
            // Windows scripts
            QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("vb"), QStringLiteral("vbs"), QStringLiteral("vbe"),
            QStringLiteral("js"), QStringLiteral("jse"), QStringLiteral("wsf"), QStringLiteral("wsh"), QStringLiteral("ws"),
            QStringLiteral("wsc"), QStringLiteral("sct"), QStringLiteral("ps1"), QStringLiteral("ps1xml"), QStringLiteral("ps2"),
            QStringLiteral("psc1"), QStringLiteral("psd1"), QStringLiteral("psm1"), QStringLiteral("mshxml"), QStringLiteral("xbap"),
            // Other platforms and interpreters
            QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("csh"), QStringLiteral("ksh"),
            QStringLiteral("fish"), QStringLiteral("command"), QStringLiteral("tool"), QStringLiteral("app"), QStringLiteral("pkg"),
            QStringLiteral("dmg"), QStringLiteral("run"), QStringLiteral("bin"), QStringLiteral("out"), QStringLiteral("elf"),
            QStringLiteral("appimage"), QStringLiteral("desktop"), QStringLiteral("deb"), QStringLiteral("rpm"), QStringLiteral("snap"),
            QStringLiteral("flatpak"), QStringLiteral("flatpakref"), QStringLiteral("apk"), QStringLiteral("jar"), QStringLiteral("jnlp"),
            QStringLiteral("py"), QStringLiteral("pyw"), QStringLiteral("pyc"), QStringLiteral("pyz"), QStringLiteral("pl"),
            QStringLiteral("rb"), QStringLiteral("php"), QStringLiteral("lua"), QStringLiteral("tcl"), QStringLiteral("scpt"),
            QStringLiteral("applescript"), QStringLiteral("workflow"), QStringLiteral("action"), QStringLiteral("so"), QStringLiteral("dylib"),
            QStringLiteral("iso"), QStringLiteral("img"), QStringLiteral("vhd"), QStringLiteral("vhdx"),
            // Files, which start programs, connect to servers (and send the sign-in of the
            // user) or install something, when the shell opens them
            QStringLiteral("rdp"), QStringLiteral("theme"), QStringLiteral("themepack"), QStringLiteral("deskthemepack"), QStringLiteral("desktopthemepack"),
            QStringLiteral("search-ms"), QStringLiteral("searchconnector-ms"), QStringLiteral("website"), QStringLiteral("wsb"), QStringLiteral("msu"),
            QStringLiteral("appinstaller"), QStringLiteral("iqy"), QStringLiteral("slk"), QStringLiteral("xll"), QStringLiteral("ade"),
            QStringLiteral("adp"), QStringLiteral("mde"), QStringLiteral("shb"), QStringLiteral("shs"), QStringLiteral("job"),
            QStringLiteral("hlp"), QStringLiteral("ins"), QStringLiteral("isp"), QStringLiteral("xnk"), QStringLiteral("mcf"),
            QStringLiteral("msh"), QStringLiteral("msh1"), QStringLiteral("msh2"), QStringLiteral("msh1xml"), QStringLiteral("msh2xml"),
            QStringLiteral("ps2xml"), QStringLiteral("psc2"), QStringLiteral("terminal"), QStringLiteral("webloc"), QStringLiteral("inetloc"),
            QStringLiteral("fileloc"), QStringLiteral("mobileconfig"), QStringLiteral("prefpane"), QStringLiteral("kext"), QStringLiteral("osax")
        };
        return extensions;
    }

    /// Reserved names of devices of Windows
    static bool isReservedName(const QString& baseName)
    {
        static const std::set<QString> names =
        {
            QStringLiteral("con"), QStringLiteral("prn"), QStringLiteral("aux"), QStringLiteral("nul"), QStringLiteral("conin$"), QStringLiteral("conout$"),
            QStringLiteral("com1"), QStringLiteral("com2"), QStringLiteral("com3"), QStringLiteral("com4"), QStringLiteral("com5"),
            QStringLiteral("com6"), QStringLiteral("com7"), QStringLiteral("com8"), QStringLiteral("com9"),
            QStringLiteral("lpt1"), QStringLiteral("lpt2"), QStringLiteral("lpt3"), QStringLiteral("lpt4"), QStringLiteral("lpt5"),
            QStringLiteral("lpt6"), QStringLiteral("lpt7"), QStringLiteral("lpt8"), QStringLiteral("lpt9"),
            // Also reserved: the digit 0 and the superscript digits 1, 2 and 3
            QStringLiteral("com0"), QStringLiteral("lpt0"),
            QStringLiteral("com¹"), QStringLiteral("com²"), QStringLiteral("com³"),
            QStringLiteral("lpt¹"), QStringLiteral("lpt²"), QStringLiteral("lpt³")
        };
        return names.count(baseName.toLower()) > 0;
    }

    /// Adds the attachment of the specification (a specification without an
    /// embedded file is left out)
    static void addAttachment(const PDFFileSpecification& specification, PDFInteger page, std::vector<PDFAppAttachment>* attachments)
    {
        const PDFEmbeddedFile* file = specification.getPlatformFile();
        if (!file || !file->isValid())
        {
            return;
        }

        PDFAppAttachment attachment;
        attachment.name = PDFAppAttachments::sanitizeFileName(specification.getPlatformFileName());
        attachment.description = specification.getDescription().left(1024);
        attachment.size = file->getSize() >= 0 ? file->getSize() : -1;
        attachment.page = page;
        attachment.specification = specification;
        PDFAppAttachments::getKind(attachment.name, &attachment.kind, &attachment.iconName);
        attachments->push_back(std::move(attachment));
    }

    /// Extension of a sanitized file name (lowercase, without the dot)
    static QString getExtension(const QString& fileName)
    {
        const qsizetype dot = fileName.lastIndexOf(QChar(u'.'));
        return dot > 0 ? fileName.mid(dot + 1).toLower() : QString();
    }
};

QString PDFAppAttachments::sanitizeFileName(const QString& name)
{
    // The last part of a path of any system
    QString fileName = name;
    const qsizetype separator = qMax(fileName.lastIndexOf(QChar(u'/')), fileName.lastIndexOf(QChar(u'\\')));
    if (separator >= 0)
    {
        fileName = fileName.mid(separator + 1);
    }

    QString result;
    result.reserve(fileName.size());
    for (const QChar character : fileName)
    {
        const bool isReserved = QStringView(u"\\/:*?\"<>|").contains(character);
        if (character.category() == QChar::Other_Control || character.category() == QChar::Other_Format ||
            character.isNonCharacter() || character.isSurrogate() || isReserved)
        {
            result.append(QChar(u'_'));
        }
        else
        {
            result.append(character);
        }
    }

    // No leading dots (hidden files, "..") and no trailing dots and spaces (Windows
    // drops them, so "a.exe." would be "a.exe")
    result = result.trimmed();
    while (result.startsWith(QChar(u'.')))
    {
        result.remove(0, 1);
    }
    while (result.endsWith(QChar(u'.')) || result.endsWith(QChar(u' ')))
    {
        result.chop(1);
    }

    // Length: the extension is kept
    if (result.size() > MAXIMUM_FILE_NAME_LENGTH)
    {
        const QString extension = PDFAppAttachmentsHelper::getExtension(result);
        const qsizetype keep = MAXIMUM_FILE_NAME_LENGTH - (extension.isEmpty() || extension.size() > 16 ? 0 : extension.size() + 1);
        QString base = result.left(keep).trimmed();
        while (base.endsWith(QChar(u'.')))
        {
            base.chop(1);
        }
        result = (extension.isEmpty() || extension.size() > 16) ? base : base + QChar(u'.') + result.right(extension.size());
    }

    const qsizetype dot = result.indexOf(QChar(u'.'));
    const QString baseName = dot >= 0 ? result.left(dot) : result;
    if (PDFAppAttachmentsHelper::isReservedName(baseName.trimmed()))
    {
        result.prepend(QChar(u'_'));
    }

    return result.isEmpty() ? QStringLiteral("attachment") : result;
}

bool PDFAppAttachments::isExecutable(const QString& fileName)
{
    const QString sanitized = sanitizeFileName(fileName);
    return PDFAppAttachmentsHelper::getExecutableExtensions().count(PDFAppAttachmentsHelper::getExtension(sanitized)) > 0;
}

bool PDFAppAttachments::isPdf(const QString& fileName)
{
    return PDFAppAttachmentsHelper::getExtension(sanitizeFileName(fileName)) == QLatin1String("pdf");
}

void PDFAppAttachments::getKind(const QString& fileName, QString* kind, QString* iconName)
{
    // The kind by the extension only (the content is never read for the list)
    static const QMimeDatabase database;
    const QMimeType type = database.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
    const QString name = type.isValid() ? type.name() : QString();

    *iconName = QStringLiteral("file-text");
    if (name.startsWith(QLatin1String("image/")))
    {
        *kind = QCoreApplication::translate("pdf::PDFAppAttachments", "Image");
        *iconName = QStringLiteral("image");
    }
    else if (name == QLatin1String("application/pdf"))
    {
        *kind = QCoreApplication::translate("pdf::PDFAppAttachments", "PDF document");
    }
    else if (name == QLatin1String("application/vnd.ms-excel") || name == QLatin1String("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") ||
             name == QLatin1String("application/vnd.oasis.opendocument.spreadsheet"))
    {
        *kind = QCoreApplication::translate("pdf::PDFAppAttachments", "Spreadsheet");
    }
    else if (name.startsWith(QLatin1String("text/")))
    {
        *kind = QCoreApplication::translate("pdf::PDFAppAttachments", "Text file");
    }
    else
    {
        *kind = QCoreApplication::translate("pdf::PDFAppAttachments", "File");
    }
}

QString PDFAppAttachments::formatSize(PDFInteger size)
{
    if (size < 0)
    {
        return QString();
    }

    // 195:2070 "84 kB", "1.2 MB" (decimal units)
    const QLocale locale;
    return locale.formattedDataSize(size, size < 1000000 ? 0 : 1, QLocale::DataSizeSIFormat);
}

std::vector<PDFAppAttachment> PDFAppAttachments::collect(const PDFDocument* document, size_t maximumCount)
{
    std::vector<PDFAppAttachment> attachments;
    if (!document)
    {
        return attachments;
    }

    for (const auto& item : document->getCatalog()->getEmbeddedFiles())
    {
        if (attachments.size() >= maximumCount)
        {
            return attachments;
        }
        PDFAppAttachmentsHelper::addAttachment(item.second, -1, &attachments);
    }

    // File attachment annotations (only their dictionaries are read for the other annotations)
    const PDFCatalog* catalog = document->getCatalog();
    const PDFObjectStorage* storage = &document->getStorage();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    for (PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        for (const PDFObjectReference& reference : catalog->getPage(pageIndex)->getAnnotations())
        {
            if (attachments.size() >= maximumCount)
            {
                return attachments;
            }

            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(reference));
            const PDFObject& subtype = dictionary ? storage->getObject(dictionary->get("Subtype")) : PDFObject();
            if (!subtype.isName() || subtype.getString() != "FileAttachment")
            {
                continue;
            }

            try
            {
                PDFAnnotationPtr annotation = PDFAnnotation::parse(storage, reference);
                if (!annotation || annotation->getType() != AnnotationType::FileAttachment)
                {
                    continue;
                }

                const PDFFileAttachmentAnnotation* fileAnnotation = static_cast<const PDFFileAttachmentAnnotation*>(annotation.get());
                PDFAppAttachmentsHelper::addAttachment(fileAnnotation->getFileSpecification(), pageIndex, &attachments);
            }
            catch (const PDFException&)
            {
                // A damaged annotation is left out
            }
        }
    }

    return attachments;
}

bool PDFAppAttachments::hasAttachments(const PDFDocument* document)
{
    return !collect(document, 1).empty();
}

PDFAppAttachments::WriteResult PDFAppAttachments::write(PDFDocumentPointer document, PDFFileSpecification specification, QString filePath, bool isOpenRequested)
{
    WriteResult result;
    result.filePath = filePath;
    result.isOpenRequested = isOpenRequested;
    result.isPdf = isPdf(QFileInfo(filePath).fileName());

    const PDFEmbeddedFile* file = specification.getPlatformFile();
    if (!document || !file || !file->isValid() || !file->getStream())
    {
        result.error = QCoreApplication::translate("pdf::PDFAppAttachments", "The attachment has no data.");
        return result;
    }

    QByteArray data;
    try
    {
        data = document->getDecodedStream(file->getStream());
    }
    catch (const PDFException& exception)
    {
        result.error = exception.getMessage();
        return result;
    }
    catch (const std::bad_alloc&)
    {
        // A decompression bomb (nested Flate filters of a few kB decode to
        // terabytes): the exception would be thrown again by the result of the
        // future in the thread of the user interface and end the application
        result.error = QCoreApplication::translate("pdf::PDFAppAttachments", "The attachment is too large.");
        return result;
    }

    if (data.size() > MAXIMUM_FILE_SIZE)
    {
        result.error = QCoreApplication::translate("pdf::PDFAppAttachments", "The attachment is too large.");
        return result;
    }

    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly) || saveFile.write(data) != data.size() || !saveFile.commit())
    {
        result.error = saveFile.errorString();
        return result;
    }

    // Never executable, readable only by the owner
    QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return result;
}

PDFAppAttachmentsModel::PDFAppAttachmentsModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppAttachmentsModel::~PDFAppAttachmentsModel() = default;

void PDFAppAttachmentsModel::setAttachments(std::vector<PDFAppAttachment> attachments)
{
    beginResetModel();
    m_attachments = std::move(attachments);
    endResetModel();
}

const PDFAppAttachment* PDFAppAttachmentsModel::getAttachment(int row) const
{
    return (row >= 0 && row < int(m_attachments.size())) ? &m_attachments[size_t(row)] : nullptr;
}

int PDFAppAttachmentsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_attachments.size());
}

Qt::ItemFlags PDFAppAttachmentsModel::flags(const QModelIndex& index) const
{
    return index.isValid() ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren) : Qt::NoItemFlags;
}

QVariant PDFAppAttachmentsModel::data(const QModelIndex& index, int role) const
{
    const PDFAppAttachment* attachment = index.isValid() ? getAttachment(index.row()) : nullptr;
    if (!attachment)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
            return attachment->name;

        case PDFComponentRoles::SubtitleRole:
        {
            // 195:2070 "Spreadsheet · 84 kB"
            const QString size = PDFAppAttachments::formatSize(attachment->size);
            return size.isEmpty() ? attachment->kind : QStringLiteral("%1 · %2").arg(attachment->kind, size);
        }

        case PDFComponentRoles::IconNameRole:
            return attachment->iconName;

        case PDFComponentRoles::HueRole:
            return int(PDFAccentHue::Neutral);

        case Qt::ToolTipRole:
            return attachment->description.isEmpty() ? attachment->name : attachment->description;

        case Qt::AccessibleTextRole:
            return attachment->name;

        default:
            break;
    }

    return QVariant();
}

PDFAppAttachmentDelegate::PDFAppAttachmentDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppAttachmentDelegate::~PDFAppAttachmentDelegate() = default;

QList<PDFRowDelegate::Action> PDFAppAttachmentDelegate::getActions(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QList<Action> actions = BaseClass::getActions(option, index);
    if (index.isValid())
    {
        // Open is in the context menu only (a click on the row opens the file too)
        Action open;
        open.id = OpenAction;
        open.text = tr("Open");
        actions.prepend(open);
    }
    return actions;
}

PDFAppAttachmentsPanel::PDFAppAttachmentsPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("attachmentsPanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new PDFAppPanelListView(this);
    m_view->setObjectName(QStringLiteral("attachmentsView"));
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setUniformItemSizes(true);
    m_view->setAccessibleName(tr("Attachments"));
    PDFAppPanelWidgets::prepareList(m_view);

    m_model = new PDFAppAttachmentsModel(m_view);
    m_delegate = new PDFAppAttachmentDelegate(m_view);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    layout->addWidget(m_view, 1);

    // A click opens the file (195:2070 "A click opens it"), Enter too
    connect(m_view, &QAbstractItemView::clicked, this, &PDFAppAttachmentsPanel::onActivated);
    m_view->installEventFilter(this);
    connect(m_delegate, &PDFRowDelegate::actionTriggered, this, &PDFAppAttachmentsPanel::onDelegateAction);
}

PDFAppAttachmentsPanel::~PDFAppAttachmentsPanel()
{
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }

    // The running writes keep their own copies; their results are dropped
    for (const QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>& watcher : m_watchers)
    {
        if (watcher)
        {
            watcher->disconnect(this);
        }
    }

    if (m_confirmDialog)
    {
        m_confirmDialog->disconnect(this);
        m_confirmDialog->close();
    }
}

QString PDFAppAttachmentsPanel::getId() const
{
    return PDFAppDocumentPanelIds::attachments();
}

QString PDFAppAttachmentsPanel::getTitle() const
{
    return tr("Attachments");
}

QString PDFAppAttachmentsPanel::getSubtitle() const
{
    // 195:2070 "3 files"
    const int count = m_model ? m_model->rowCount() : 0;
    return count > 0 ? (count == 1 ? tr("%Ln file", nullptr, count) : tr("%Ln files", nullptr, count)) : QString();
}

QString PDFAppAttachmentsPanel::getIconName() const
{
    return QStringLiteral("paperclip");
}

void PDFAppAttachmentsPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }

    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppAttachmentsPanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppAttachmentsPanel::onDocumentDestroyed);
    }
    m_connectedDocument = document;
    rebuild();
}

void PDFAppAttachmentsPanel::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);
    rebuild();
}

void PDFAppAttachmentsPanel::onDocumentDestroyed()
{
    m_connectedDocument = nullptr;
    m_document.reset();
    if (m_model)
    {
        m_model->setAttachments({});
    }
}

void PDFAppAttachmentsPanel::rebuild()
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    if (pointer == m_document && m_model && m_model->rowCount() > 0)
    {
        return;
    }

    m_document = pointer;
    if (m_model)
    {
        m_model->setAttachments(PDFAppAttachments::collect(pointer.data()));
    }
    Q_EMIT titleChanged();
}

void PDFAppAttachmentsPanel::onActivated(const QModelIndex& index)
{
    if (index.isValid())
    {
        openAttachment(index.row());
    }
}

void PDFAppAttachmentsPanel::onDelegateAction(const QModelIndex& index, int action)
{
    if (!index.isValid())
    {
        return;
    }

    if (action == PDFAttachmentDelegate::SaveAction)
    {
        saveAttachment(index.row());
    }
    else if (action == PDFAppAttachmentDelegate::OpenAction)
    {
        openAttachment(index.row());
    }
}

void PDFAppAttachmentsPanel::saveAttachment(int row)
{
    const PDFAppAttachment* attachment = m_model ? m_model->getAttachment(row) : nullptr;
    if (!attachment)
    {
        return;
    }

    // Save always (D8), also programs and scripts; the dialog may run its own loop,
    // in which the document may change and the rows with it: the attachment and its
    // document are copied before the dialog
    const PDFAppAttachment saved = *attachment;
    const PDFDocumentPointer document = m_document;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QPointer<PDFAppAttachmentsPanel> self(this);
    const QString filePath = PDFAppPanelFileDialogs::getSaveFileName(this, tr("Save Attachment"), QDir(folder).filePath(saved.name), tr("All files (*)"));
    if (!self || filePath.isEmpty())
    {
        return;
    }

    startWrite(saved, document, filePath, false);
}

void PDFAppAttachmentsPanel::openAttachment(int row)
{
    const PDFAppAttachment* attachment = m_model ? m_model->getAttachment(row) : nullptr;
    if (!attachment)
    {
        return;
    }

    if (PDFAppAttachments::isExecutable(attachment->name))
    {
        showError(tr("The file was not opened"),
                  tr("“%1” is a program or a script. PDF4QT does not start programs from documents; save the file, if you need it, and check it first.").arg(attachment->name));
        return;
    }

    if (PDFAppAttachments::isPdf(attachment->name))
    {
        // A PDF is opened in a new tab of PDF4QT
        const QString filePath = createTemporaryPath(attachment->name);
        if (filePath.isEmpty())
        {
            showError(tr("The file was not opened"), tr("The temporary folder for the attachment cannot be created."));
            return;
        }
        startWrite(*attachment, m_document, filePath, true);
        return;
    }

    // Another file: the confirmation of the launch (1028:23389)
    if (m_confirmDialog)
    {
        m_confirmDialog->disconnect(this);
        m_confirmDialog->close();
    }

    const QString extension = QFileInfo(attachment->name).suffix();
    PDFAppDocument* document = getDocument();
    const QString documentName = document ? document->getTitle() : QString();
    const QString text = extension.isEmpty()
        ? tr("The document wants to open a file in the program your system uses for it. A file from an unknown document can contain malware. Open it only if you trust the author of the document.")
        : tr("The document wants to open a file in the program your system uses for .%1 files. A file from an unknown document can contain malware. Open it only if you trust the author of the document.").arg(extension);
    PDFAppCardDialog* dialog = new PDFAppCardDialog(PDFThemeIcons::Badge::Warning, true, tr("Open a file from %1?").arg(documentName), text, 520, window());
    dialog->setObjectName(QStringLiteral("openAttachmentDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // facts: bg/subtle, radius 10, paddings 4 / 12, Key value rows
    QFrame* facts = new QFrame(dialog);
    facts->setObjectName(QStringLiteral("facts"));
    PDFWidgetStyleProperties::set(facts, PDFWidgetStyleProperties::NOTE);
    QVBoxLayout* factsLayout = new QVBoxLayout(facts);
    factsLayout->setContentsMargins(12, 4, 12, 4);
    factsLayout->setSpacing(0);
    PDFKeyValueList* list = new PDFKeyValueList(facts);
    list->setObjectName(QStringLiteral("factsList"));
    list->addRow(tr("File"), attachment->name);
    list->addRow(tr("Folder"), QDir::toNativeSeparators(m_temporaryDirectory ? m_temporaryDirectory->path() : QDir::tempPath()));
    list->addRow(tr("Opens in"), extension.isEmpty() ? tr("The program set for the file") : tr("The program set for .%1 files").arg(extension));
    factsLayout->addWidget(list);
    dialog->addContent(facts);

    QPushButton* openButton = dialog->addActionButton(tr("Open File"), false, true);
    openButton->setObjectName(QStringLiteral("openFileButton"));
    QPushButton* cancelButton = dialog->addActionButton(tr("Don’t Open"), true, false);
    cancelButton->setObjectName(QStringLiteral("dontOpenButton"));
    connect(openButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, &PDFAppAttachmentsPanel::onConfirmFinished);

    // The confirmed attachment and its document are kept: the document may change
    // while the dialog is open, and its rows with it
    m_confirmDialog = dialog;
    m_confirmAttachment = *attachment;
    m_confirmDocument = m_document;
    dialog->open();
}

void PDFAppAttachmentsPanel::onConfirmFinished(int result)
{
    const std::optional<PDFAppAttachment> attachment = std::move(m_confirmAttachment);
    const PDFDocumentPointer document = std::move(m_confirmDocument);
    m_confirmAttachment.reset();
    m_confirmDocument.reset();
    m_confirmDialog = nullptr;

    // Programs and scripts are never started (D8), whatever has been confirmed
    if (result != QDialog::Accepted || !attachment || PDFAppAttachments::isExecutable(attachment->name))
    {
        return;
    }

    const QString filePath = createTemporaryPath(attachment->name);
    if (filePath.isEmpty())
    {
        showError(tr("The file was not opened"), tr("The temporary folder for the attachment cannot be created."));
        return;
    }
    startWrite(*attachment, document, filePath, true);
}

QString PDFAppAttachmentsPanel::createTemporaryPath(const QString& fileName)
{
    if (!m_temporaryDirectory)
    {
        // A private folder (only the owner can read it) removed with the panel
        m_temporaryDirectory = std::make_unique<QTemporaryDir>(QDir(QDir::tempPath()).filePath(QStringLiteral("pdf4qt-attachments-XXXXXX")));
        if (!m_temporaryDirectory->isValid())
        {
            m_temporaryDirectory.reset();
            return QString();
        }
        QFile::setPermissions(m_temporaryDirectory->path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    // Each opening has its own subfolder (the same name may be opened twice)
    const QDir root(m_temporaryDirectory->path());
    for (int i = 1; i < 10000; ++i)
    {
        const QString folderName = QString::number(i);
        if (!root.exists(folderName) && root.mkdir(folderName))
        {
            return root.filePath(folderName + QChar(u'/') + PDFAppAttachments::sanitizeFileName(fileName));
        }
    }
    return QString();
}

QString PDFAppAttachmentsPanel::getTemporaryFolder() const
{
    return m_temporaryDirectory ? m_temporaryDirectory->path() : QString();
}

int PDFAppAttachmentsPanel::getRunningCount() const
{
    int count = 0;
    for (const QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>& watcher : m_watchers)
    {
        if (watcher && !watcher->isFinished())
        {
            ++count;
        }
    }
    return count;
}

void PDFAppAttachmentsPanel::startWrite(const PDFAppAttachment& attachment, PDFDocumentPointer document, const QString& filePath, bool isOpenRequested)
{
    if (!document)
    {
        return;
    }

    // The decoding and the writing run in a worker with shared copies of the
    // document and of the file; the watcher dies with the panel
    QFutureWatcher<PDFAppAttachments::WriteResult>* watcher = new QFutureWatcher<PDFAppAttachments::WriteResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, &PDFAppAttachmentsPanel::onWriteFinished);
    m_watchers.push_back(watcher);
    watcher->setFuture(QtConcurrent::run(&PDFAppAttachments::write, std::move(document), attachment.specification, filePath, isOpenRequested));
}

void PDFAppAttachmentsPanel::onWriteFinished()
{
    // The watcher does not identify itself in the signal
    const QObject* finishedWatcher = sender();
    QFutureWatcher<PDFAppAttachments::WriteResult>* watcher = nullptr;
    for (const QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>& item : m_watchers)
    {
        if (item && item.data() == finishedWatcher)
        {
            watcher = item.data();
        }
    }
    if (!watcher)
    {
        return;
    }

    const PDFAppAttachments::WriteResult result = watcher->result();
    watcher->deleteLater();
    m_watchers.erase(std::remove(m_watchers.begin(), m_watchers.end(), QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>(watcher)), m_watchers.end());

    if (!result.error.isEmpty())
    {
        showError(result.isOpenRequested ? tr("The file was not opened") : tr("The file was not saved"), result.error);
        return;
    }

    if (!result.isOpenRequested)
    {
        if (PDFAppDocumentView* view = getView())
        {
            if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
            {
                statusBar->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(result.filePath)), 5000);
            }
        }
        return;
    }

    if (result.isPdf)
    {
        if (PDFAppMainWindow* window = PDFAppPanelWidgets::findMainWindow(this))
        {
            window->openDocuments(QStringList(result.filePath), 0);
        }
        return;
    }

    if (!PDFAppPanelFileDialogs::launchFile(result.filePath))
    {
        showError(tr("The file was not opened"), tr("No program of the system can open “%1”.").arg(QFileInfo(result.filePath).fileName()));
    }
}

bool PDFAppAttachmentsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event->type() == QEvent::KeyPress && m_view)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) && keyEvent->modifiers() == Qt::NoModifier &&
            m_view->currentIndex().isValid())
        {
            openAttachment(m_view->currentIndex().row());
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppAttachmentsPanel::showError(const QString& title, const QString& text)
{
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, title, text, QDialogButtonBox::Ok, window());
    PDFAppPanelWidgets::prepareMessageBox(box, this, QStringLiteral("attachmentErrorBox"));
    box->open();
}

void PDFAppAttachmentsPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        if (m_view)
        {
            m_view->setAccessibleName(tr("Attachments"));
        }
        Q_EMIT titleChanged();
    }
}

QString PDFAppAttachmentsPanelFactory::getId() const
{
    return PDFAppDocumentPanelIds::attachments();
}

int PDFAppAttachmentsPanelFactory::getOrder() const
{
    return 40;
}

QString PDFAppAttachmentsPanelFactory::getTitle() const
{
    return PDFAppAttachmentsPanel::tr("Attachments");
}

QString PDFAppAttachmentsPanelFactory::getIconName() const
{
    return QStringLiteral("paperclip");
}

bool PDFAppAttachmentsPanelFactory::isAvailable(const PDFAppDocument* document) const
{
    return document && PDFAppAttachments::hasAttachments(document->getDocument().data());
}

PDFAppDocumentPanel* PDFAppAttachmentsPanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppAttachmentsPanel(parent);
}

}   // namespace pdf
