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
#include "pdfapplocale.h"
#include "pdfappdocument.h"
#include "pdfappdocumentloader.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfapppagelayout.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"

#include "pdfannotation.h"
#include "pdfcatalog.h"
#include "pdfdeferreddeletion.h"
#include "pdfexception.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfwidgetstyleproperties.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
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

    /// Calls of PDFAppAttachments::collect (tests)
    static std::atomic<int>& getCollectCounter();

    /// Alternate data stream of NTFS with the Mark of the Web of a file
    static constexpr const char* ZONE_IDENTIFIER_STREAM = ":Zone.Identifier";

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
    return PDFAppLocale::formatDataSize(size, size < 1000000 ? 0 : 1, QLocale::DataSizeSIFormat);
}

/// Attachments of one version of a document (PDFAppDocument::getDerivedData)
class PDFAppAttachmentList : public PDFAppDocumentDerivedData
{
public:
    static constexpr const char* KEY = "attachments";

    std::shared_ptr<const std::vector<PDFAppAttachment>> attachments;
};

std::atomic<int>& PDFAppAttachmentsHelper::getCollectCounter()
{
    static std::atomic<int> counter = 0;
    return counter;
}

int PDFAppAttachments::getCollectCount()
{
    return PDFAppAttachmentsHelper::getCollectCounter().load();
}

std::shared_ptr<const std::vector<PDFAppAttachment>> PDFAppAttachments::getAttachments(const PDFAppDocument* document)
{
    if (!document || !document->isReady() || !document->getDocument())
    {
        return std::make_shared<const std::vector<PDFAppAttachment>>();
    }

    const QString key = QLatin1String(PDFAppAttachmentList::KEY);
    if (const PDFAppAttachmentList* list = dynamic_cast<const PDFAppAttachmentList*>(document->getDerivedData(key).get()))
    {
        return list->attachments;
    }

    std::shared_ptr<PDFAppAttachmentList> list = std::make_shared<PDFAppAttachmentList>();
    list->attachments = std::make_shared<const std::vector<PDFAppAttachment>>(collect(document->getDocument().data()));
    document->setDerivedData(key, list);
    return list->attachments;
}

std::vector<PDFAppAttachment> PDFAppAttachments::collect(const PDFDocument* document, size_t maximumCount)
{
    // Only the lists are counted (hasAttachments asks for one attachment)
    if (maximumCount > 1)
    {
        ++PDFAppAttachmentsHelper::getCollectCounter();
    }
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

QByteArray PDFAppAttachments::decode(const PDFDocumentPointer& document, const PDFFileSpecification& specification, QString* error)
{
    const PDFEmbeddedFile* file = specification.getPlatformFile();
    if (!document || !file || !file->isValid() || !file->getStream())
    {
        *error = QCoreApplication::translate("pdf::PDFAppAttachments", "The attachment has no data.");
        return QByteArray();
    }

    QByteArray data;
    try
    {
        data = document->getDecodedStream(file->getStream());
    }
    catch (const PDFException& exception)
    {
        *error = exception.getMessage();
        return QByteArray();
    }

    if (data.size() > MAXIMUM_FILE_SIZE)
    {
        *error = QCoreApplication::translate("pdf::PDFAppAttachments", "The attachment is too large.");
        return QByteArray();
    }

    return data;
}

PDFAppAttachments::WriteResult PDFAppAttachments::write(PDFDocumentPointer document, PDFFileSpecification specification, QString filePath, QString sourceFilePath)
{
    WriteResult result;
    result.filePath = filePath;

    const QByteArray data = decode(document, specification, &result.error);
    if (!result.error.isEmpty())
    {
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

    // The file comes from the internet as its PDF (O18): the Mark of the Web of the
    // source goes with it (best effort, a failure is not an error of the save)
    copyMarkOfTheWeb(sourceFilePath, filePath);
    return result;
}

PDFAppAttachments::WriteResult PDFAppAttachments::read(PDFDocumentPointer document, PDFFileSpecification specification, QString name)
{
    WriteResult result;
    result.name = name;
    result.isOpenRequested = true;
    result.data = decode(document, specification, &result.error);
    if (result.error.isEmpty() && result.data.isEmpty())
    {
        result.error = QCoreApplication::translate("pdf::PDFAppAttachments", "The attachment has no data.");
    }
    return result;
}

bool PDFAppAttachments::copyMarkOfTheWeb(const QString& sourceFilePath, const QString& targetFilePath)
{
    if (targetFilePath.isEmpty())
    {
        return false;
    }
    return writeMarkOfTheWeb(targetFilePath, readMarkOfTheWeb(sourceFilePath));
}

QByteArray PDFAppAttachments::readMarkOfTheWeb(const QString& filePath)
{
#ifdef Q_OS_WIN
    if (filePath.isEmpty())
    {
        return QByteArray();
    }

    // The alternate data stream of NTFS "<file>:Zone.Identifier" (a small text,
    // [ZoneTransfer] ZoneId=3 ...); a missing stream is the usual case
    QFile source(QDir::toNativeSeparators(filePath) + QLatin1String(PDFAppAttachmentsHelper::ZONE_IDENTIFIER_STREAM));
    if (!source.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    const QByteArray zoneIdentifier = source.read(MAXIMUM_ZONE_IDENTIFIER_SIZE + 1);
    source.close();
    if (zoneIdentifier.size() > MAXIMUM_ZONE_IDENTIFIER_SIZE)
    {
        return QByteArray();
    }
    return zoneIdentifier;
#else
    Q_UNUSED(filePath);
    return QByteArray();
#endif
}

bool PDFAppAttachments::writeMarkOfTheWeb(const QString& filePath, const QByteArray& zoneIdentifier)
{
#ifdef Q_OS_WIN
    if (filePath.isEmpty() || zoneIdentifier.isEmpty() || zoneIdentifier.size() > MAXIMUM_ZONE_IDENTIFIER_SIZE)
    {
        return false;
    }

    QFile target(QDir::toNativeSeparators(filePath) + QLatin1String(PDFAppAttachmentsHelper::ZONE_IDENTIFIER_STREAM));
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const bool isWritten = target.write(zoneIdentifier) == zoneIdentifier.size();
    target.close();
    return isWritten;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(zoneIdentifier);
    return false;
#endif
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
    if (index.isValid() && PDFAppAttachments::isPdf(index.data(Qt::DisplayRole).toString()))
    {
        // Open (a PDF only, O18) is in the context menu only (a click on the row opens it too)
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

    // Q5.9 c, R5-8: the attachments of a document opened without their password (no frame -
    // Q17: a card of the panel with the lock, the title, the text and Enter Password…)
    QFrame* lockedCard = new QFrame(this);
    lockedCard->setObjectName(QStringLiteral("attachmentsLocked"));
    QVBoxLayout* lockedLayout = new QVBoxLayout(lockedCard);
    lockedLayout->setContentsMargins(16, 16, 16, 16);
    lockedLayout->setSpacing(8);
    m_lockedTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, lockedCard);
    m_lockedTitle->setObjectName(QStringLiteral("attachmentsLockedTitle"));
    m_lockedTitle->setWordWrap(true);
    lockedLayout->addWidget(m_lockedTitle);
    m_lockedText = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextSecondary, lockedCard);
    m_lockedText->setObjectName(QStringLiteral("attachmentsLockedText"));
    m_lockedText->setWordWrap(true);
    lockedLayout->addWidget(m_lockedText);
    m_unlockButton = PDFAppPageWidgets::createButton(QString(), QStringLiteral("lock"), false, lockedCard);
    m_unlockButton->setObjectName(QStringLiteral("attachmentsUnlockButton"));
    connect(m_unlockButton.data(), &QPushButton::clicked, this, &PDFAppAttachmentsPanel::openUnlockDialog);
    lockedLayout->addWidget(m_unlockButton, 0, Qt::AlignLeading);
    lockedLayout->addStretch(1);
    lockedCard->hide();
    m_lockedCard = lockedCard;
    layout->addWidget(lockedCard, 1);
    layout->addWidget(m_view, 1);

    // A click (Enter) opens a PDF attachment, another one is saved (O18); from the event loop - opening a document
    // may change the tab, and Qt reads the view after its signal
    PDFAppItemViewSignalRelay* relay = new PDFAppItemViewSignalRelay(m_view, this);
    connect(relay, &PDFAppItemViewSignalRelay::clicked, this, &PDFAppAttachmentsPanel::onActivated);
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
        PDFDeferredDeletion::connect(document, &PDFAppDocument::documentChanged, this, &PDFAppAttachmentsPanel::onDocumentChanged);
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

bool PDFAppAttachmentsPanel::isAttachmentsLocked() const
{
    const PDFAppDocument* document = getDocument();
    const PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    const PDFSecurityHandler* securityHandler = pointer ? pointer->getStorage().getSecurityHandler() : nullptr;
    return securityHandler && securityHandler->isEmbeddedFilesLocked();
}

void PDFAppAttachmentsPanel::updateLockedState()
{
    const bool isLocked = isAttachmentsLocked();
    if (m_lockedTitle)
    {
        m_lockedTitle->setText(tr("Locked – enter the password"));
    }
    if (m_lockedText)
    {
        m_lockedText->setText(tr("The attachments of this document are encrypted. Enter the password to read them; the document opens again with its attachments and can be changed then."));
    }
    if (m_unlockButton)
    {
        m_unlockButton->setText(tr("Enter Password…"));
    }
    if (m_lockedCard)
    {
        m_lockedCard->setVisible(isLocked);
    }
    if (m_view)
    {
        m_view->setVisible(!isLocked);
    }
}

void PDFAppAttachmentsPanel::openUnlockDialog()
{
    PDFAppDocument* document = getDocument();
    if (!document || !isAttachmentsLocked())
    {
        return;
    }
    if (m_unlockDialog)
    {
        m_unlockDialog->raise();
        m_unlockDialog->activateWindow();
        return;
    }

    // A popup: new + QPointer, deleted on close (CLAUDE.md); it waits for the reading itself
    PDFAppAttachmentsPasswordDialog* dialog = new PDFAppAttachmentsPasswordDialog(document, window());
    PDFDeferredDeletion::setOwner(dialog, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowModality(Qt::WindowModal);
    m_unlockDialog = dialog;
    dialog->open();
}

void PDFAppAttachmentsPanel::rebuild()
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    updateLockedState();
    if (pointer == m_document && m_model && m_model->rowCount() > 0)
    {
        return;
    }

    m_document = pointer;
    if (m_model)
    {
        // The list of the version is shared with the tab of the sidebar; the attachments locked by
        // their password (Q5.9 c) are no rows - their bytes are encrypted
        m_model->setAttachments(pointer && !isAttachmentsLocked() ? *PDFAppAttachments::getAttachments(document) : std::vector<PDFAppAttachment>());
    }
    Q_EMIT titleChanged();
}

// ------------------------------------------------------ PDFAppAttachmentsPasswordDialog

PDFAppAttachmentsPasswordDialog::PDFAppAttachmentsPasswordDialog(PDFAppDocument* document, QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, QString(), QString(), 460, parent),
    m_document(document)
{
    setObjectName(QStringLiteral("attachmentsPasswordDialog"));
    m_loader = document ? document->findChild<PDFAppDocumentLoader*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    const QString title = document ? document->getTitle() : QString();
    setTitle(tr("Password for the attachments of %1").arg(title));
    setText(tr("The attachments are encrypted. The password is used only to read the document again with its attachments and is not stored."));

    // 398:28116: the label, the field 34, the error - gaps 14
    QWidget* form = new QWidget(this);
    QVBoxLayout* formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(14);
    formLayout->addWidget(PDFAppPageWidgets::createLabel(tr("Password"), QFont::Normal, 12, PDFThemeColors::TextSecondary, form));
    m_passwordField = new PDFLineEdit(form);
    m_passwordField->setObjectName(QStringLiteral("passwordField"));
    m_passwordField->setPasswordField();
    m_passwordField->setMaxLength(PDFAppDocumentReading::MAXIMUM_PASSWORD_LENGTH);
    m_passwordField->setFixedHeight(34);
    m_passwordField->setAccessibleName(tr("Password of the attachments of %1").arg(title));
    formLayout->addWidget(m_passwordField);
    PDFAppParagraph* errorText = nullptr;
    m_errorRow = createErrorRow(form, &errorText);
    m_errorText = errorText;
    m_errorText->setText(tr("Wrong password – check Caps Lock and try again"));
    formLayout->addWidget(m_errorRow);
    addContent(form);

    m_cancelButton = addActionButton(tr("Cancel"), false, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_unlockButton = addActionButton(tr("Unlock"), true, false);
    m_unlockButton->setObjectName(QStringLiteral("unlockButton"));
    m_unlockButton->setIcon(PDFThemeIcons::getIcon(QStringLiteral("key")));
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppAttachmentsPasswordDialog::reject);
    connect(m_unlockButton.data(), &QPushButton::clicked, this, &PDFAppAttachmentsPasswordDialog::onUnlockClicked);
    connect(m_passwordField.data(), &QLineEdit::textEdited, this, &PDFAppAttachmentsPasswordDialog::onPasswordEdited);
    if (m_loader)
    {
        connect(m_loader.data(), &PDFAppDocumentLoader::finished, this, &PDFAppAttachmentsPasswordDialog::onLoaderFinished);
    }
    if (document)
    {
        connect(document, &QObject::destroyed, this, &PDFAppAttachmentsPasswordDialog::onDocumentDestroyed);
    }
    updateButtons();
    m_passwordField->setFocus();
}

PDFAppAttachmentsPasswordDialog::~PDFAppAttachmentsPasswordDialog()
{
    // S5.1: the field is emptied with its undo history
    if (m_passwordField)
    {
        m_passwordField->setText(QString());
    }
}

QString PDFAppAttachmentsPasswordDialog::getErrorText() const
{
    return m_errorRow && !m_errorRow->isHidden() && m_errorText ? m_errorText->text() : QString();
}

void PDFAppAttachmentsPasswordDialog::updateButtons()
{
    if (m_passwordField)
    {
        m_passwordField->setEnabled(!m_isBusy);
    }
    if (m_unlockButton)
    {
        m_unlockButton->setEnabled(!m_isBusy && m_loader && m_passwordField && !m_passwordField->text().isEmpty());
    }
}

void PDFAppAttachmentsPasswordDialog::onUnlockClicked()
{
    if (!m_loader || !m_passwordField || m_isBusy || m_passwordField->text().isEmpty())
    {
        return;
    }

    // The field keeps the password while it is verified (a wrong one stays selected)
    QString password = m_passwordField->text();
    const bool isStarted = m_loader->unlockAttachments(password);
    PDFSecureMemory::clear(password);
    m_isBusy = isStarted;
    m_errorRow->hide();
    m_passwordField->setInvalid(false);
    updateButtons();
}

void PDFAppAttachmentsPasswordDialog::onLoaderFinished()
{
    if (!m_isBusy)
    {
        return;
    }
    m_isBusy = false;

    // The right password has replaced the document (its attachments readable)
    if (m_loader && !m_loader->isAttachmentsLocked())
    {
        accept();
        return;
    }

    // 398:27726: the error, the wrong password stays selected in the field
    const bool isWrong = m_loader && m_loader->isAttachmentsPasswordWrong();
    if (m_errorRow)
    {
        m_errorRow->setVisible(isWrong);
    }
    if (m_passwordField)
    {
        m_passwordField->setInvalid(isWrong, isWrong && m_errorText ? m_errorText->text() : QString());
        m_passwordField->setFocus();
        m_passwordField->selectAll();
    }
    updateButtons();
    adjustSize();
}

void PDFAppAttachmentsPasswordDialog::onPasswordEdited()
{
    if (m_errorRow)
    {
        m_errorRow->hide();
    }
    if (m_passwordField)
    {
        m_passwordField->setInvalid(false);
    }
    updateButtons();
}

void PDFAppAttachmentsPasswordDialog::onDocumentDestroyed()
{
    // Only the pointer is forgotten here; the dialog closes from the event loop
    m_document = nullptr;
    QMetaObject::invokeMethod(this, &PDFAppAttachmentsPasswordDialog::reject, Qt::QueuedConnection);
}

void PDFAppAttachmentsPanel::onActivated(const QModelIndex& index)
{
    if (index.isValid())
    {
        activateAttachment(index.row());
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

void PDFAppAttachmentsPanel::activateAttachment(int row)
{
    const PDFAppAttachment* attachment = m_model ? m_model->getAttachment(row) : nullptr;
    if (!attachment)
    {
        return;
    }

    // A PDF opens in a new tab; any other file is only saved (O18), which is the
    // action of its row (195:2070)
    if (PDFAppAttachments::isPdf(attachment->name))
    {
        openAttachment(row);
    }
    else
    {
        saveAttachment(row);
    }
}

QString PDFAppAttachmentsPanel::getSourceFilePath() const
{
    PDFAppDocument* document = getDocument();
    return document ? document->getOriginFilePath() : QString();
}

void PDFAppAttachmentsPanel::saveAttachment(int row)
{
    const PDFAppAttachment* attachment = m_model ? m_model->getAttachment(row) : nullptr;
    if (!attachment)
    {
        return;
    }

    // Save always (D8), also programs and scripts; the document may change while the
    // dialog is open and the rows with it: the attachment, its document and the file of
    // the document (its Mark of the Web, O18) are copied before the dialog
    PendingSave pending;
    pending.attachment = *attachment;
    pending.document = m_document;
    pending.sourceFilePath = getSourceFilePath();
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString proposedPath = QDir(folder).filePath(pending.attachment.name);

    // The native dialog without a nested event loop (A0): the file comes to onSaveFileChosen,
    // never inside this call; the request is a child of the panel (a panel deleted meanwhile
    // gets nothing). The handler of the tests is foreign code, which may delete the panel.
    QPointer<PDFAppAttachmentsPanel> guard(this);
    PDFAppFileDialogRequest* request = PDFAppPanelFileDialogs::requestSaveFileName(this, tr("Save Attachment"), proposedPath, tr("All files (*)"),
                                                                                   this, &PDFAppAttachmentsPanel::onSaveFileChosen);
    if (!guard || !request)
    {
        return;
    }
    pending.request = request;
    m_pendingSaves.push_back(std::move(pending));
}

void PDFAppAttachmentsPanel::onSaveFileChosen(const QString& filePath)
{
    // The request does not identify itself in the signal: the copies of its save
    const QObject* request = sender();
    auto it = m_pendingSaves.begin();
    while (it != m_pendingSaves.end() && (!request || it->request.data() != request))
    {
        ++it;
    }
    if (it == m_pendingSaves.end())
    {
        return;
    }

    const PendingSave pending = std::move(*it);
    m_pendingSaves.erase(it);
    const PDFAppAttachment& saved = pending.attachment;
    const PDFDocumentPointer& document = pending.document;
    const QString& sourceFilePath = pending.sourceFilePath;
    if (filePath.isEmpty() || !document)
    {
        return;
    }

    // The decoding and the writing run in a worker with shared copies of the
    // document and of the file; the Mark of the Web of the file of the document
    // goes to the written file (PDFAppAttachments::write)
    startWatcher(QtConcurrent::run(&PDFAppAttachments::write, document, saved.specification, filePath, sourceFilePath));
}

bool PDFAppAttachmentsPanel::openAttachment(int row)
{
    const PDFAppAttachment* attachment = m_model ? m_model->getAttachment(row) : nullptr;
    if (!attachment || !m_document || !PDFAppAttachments::isPdf(attachment->name))
    {
        return false;
    }

    // A PDF is decoded in a worker and opened in a new tab from the memory (O18:
    // no temporary file)
    startWatcher(QtConcurrent::run(&PDFAppAttachments::read, m_document, attachment->specification, attachment->name));
    return true;
}

int PDFAppAttachmentsPanel::getRunningCount() const
{
    // A save waiting for its file dialog runs too
    int count = 0;
    for (const PendingSave& pending : m_pendingSaves)
    {
        if (pending.request && pending.request->isPending())
        {
            ++count;
        }
    }
    for (const QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>& watcher : m_watchers)
    {
        if (watcher && !watcher->isFinished())
        {
            ++count;
        }
    }
    return count;
}

void PDFAppAttachmentsPanel::startWatcher(const QFuture<PDFAppAttachments::WriteResult>& future)
{
    // The watcher dies with the panel (the worker finishes alone, its result is dropped)
    QFutureWatcher<PDFAppAttachments::WriteResult>* watcher = new QFutureWatcher<PDFAppAttachments::WriteResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, &PDFAppAttachmentsPanel::onWriteFinished);
    m_watchers.push_back(watcher);
    watcher->setFuture(future);
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

    PDFAppAttachments::WriteResult result = watcher->result();
    watcher->deleteLater();
    m_watchers.erase(std::remove(m_watchers.begin(), m_watchers.end(), QPointer<QFutureWatcher<PDFAppAttachments::WriteResult>>(watcher)), m_watchers.end());

    if (!result.error.isEmpty())
    {
        showError(result.isOpenRequested ? tr("The file was not opened") : tr("The file was not saved"), result.error);
        return;
    }

    if (result.isOpenRequested)
    {
        // A new tab reads the document from the bytes (its password screen too)
        if (PDFAppMainWindow* window = PDFAppPanelWidgets::findMainWindow(this))
        {
            window->openDocumentFromMemory(std::move(result.data), result.name, getSourceFilePath(), getDocument());
        }
        return;
    }

    if (PDFAppDocumentView* view = getView())
    {
        if (PDFAppDocumentStatusBar* statusBar = view->getStatusBar())
        {
            statusBar->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(result.filePath)), 5000);
        }
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
            activateAttachment(m_view->currentIndex().row());
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppAttachmentsPanel::showError(const QString& title, const QString& text)
{
    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, title, text, QDialogButtonBox::Ok, window());
    PDFDeferredDeletion::setOwner(box, this);
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
    if (!document)
    {
        return false;
    }

    // The tab is asked at every change of the document: the list of the version, when
    // a panel has collected it, otherwise the search for the first attachment (the
    // whole list of a large document is collected only for a shown panel)
    if (const PDFAppAttachmentList* list = dynamic_cast<const PDFAppAttachmentList*>(document->getDerivedData(PDFAppAttachmentList::KEY).get()))
    {
        return !list->attachments->empty();
    }
    return PDFAppAttachments::hasAttachments(document->getDocument().data());
}

PDFAppDocumentPanel* PDFAppAttachmentsPanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppAttachmentsPanel(parent);
}

}   // namespace pdf

#include "moc_pdfappdocumentpanels_attachments.cpp"
