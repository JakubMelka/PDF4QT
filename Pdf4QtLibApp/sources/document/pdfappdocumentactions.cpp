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

#include "pdfappdocumentactions.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_attachments.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfapproute.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
#include "pdfcomponentpanels.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdffile.h"
#include "pdffluentwidgets.h"
#include "pdfform.h"
#include "pdfjavascriptscanner.h"
#include "pdfmessagebox.h"
#include "pdfoptionalcontent.h"
#include "pdftooltip.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgetformmanager.h"
#include "pdfwidgetstyleproperties.h"
#include "pdfwidgettool.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shellapi.h>
#endif

namespace pdf
{

PDFAppDocumentActionHandler::LaunchHandler PDFAppDocumentActionHandler::s_launchHandler;

/// Closes a message box with the code of the clicked button (the buttons of the
/// own row of PDFMessageBox do not close it). No lambda: the slot finds the
/// button by sender().
class PDFAppDocumentActionDialogCloser : public QObject
{
public:
    static constexpr const char* CODE_PROPERTY = "pdf4qtDialogCode";

    explicit PDFAppDocumentActionDialogCloser(PDFMessageBox* box) :
        QObject(box),
        m_box(box)
    {

    }

    void add(QPushButton* button, PDFAppDocumentActionDialogs::Result result)
    {
        if (button)
        {
            button->setProperty(CODE_PROPERTY, int(result));
            connect(button, &QAbstractButton::clicked, this, &PDFAppDocumentActionDialogCloser::onClicked);
        }
    }

private:
    void onClicked()
    {
        const QObject* button = sender();
        if (button && m_box)
        {
            m_box->done(button->property(CODE_PROPERTY).toInt());
        }
    }

    QPointer<PDFMessageBox> m_box;
};

/// Helpers of the actions (file-local)
class PDFAppDocumentActionsHelper
{
public:
    PDFAppDocumentActionsHelper() = delete;

    /// Panel bg/subtle of the address of a link (1028:22914: padding 12 / 8, gap 2)
    static QWidget* createAddress(const QString& title, const QString& address, QWidget* parent)
    {
        QFrame* frame = new QFrame(parent);
        frame->setObjectName(QStringLiteral("address"));
        PDFWidgetStyleProperties::set(frame, PDFWidgetStyleProperties::NOTE);
        QVBoxLayout* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(2);

        PDFLabel* titleLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, frame);
        titleLabel->setObjectName(QStringLiteral("addressTitle"));
        titleLabel->setTextFormat(Qt::PlainText);
        titleLabel->setText(title);
        PDFLabel* addressLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, frame);
        addressLabel->setObjectName(QStringLiteral("addressText"));
        addressLabel->setTextFormat(Qt::PlainText);
        addressLabel->setText(address);
        addressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(titleLabel);
        layout->addWidget(addressLabel);
        return frame;
    }

    /// Fact of a launch: a path is elided in the middle (O18: the extension of
    /// "...\invoice.pdf.exe" stays visible)
    class Fact
    {
    public:
        QString key;
        QString value;
        PDFComponentPainter::TextKind kind = PDFComponentPainter::TextKind::Data;
    };

    /// Panel bg/subtle with the facts of a launch (1028:23369: Key value rows, padding 12 / 4)
    static QWidget* createFacts(const QList<Fact>& facts, QWidget* parent)
    {
        QFrame* frame = new QFrame(parent);
        frame->setObjectName(QStringLiteral("facts"));
        PDFWidgetStyleProperties::set(frame, PDFWidgetStyleProperties::NOTE);
        QVBoxLayout* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(12, 4, 12, 4);
        layout->setSpacing(0);

        PDFKeyValueList* list = new PDFKeyValueList(frame);
        list->setObjectName(QStringLiteral("factList"));
        for (const Fact& fact : facts)
        {
            list->addRow(fact.key, fact.value, fact.kind);
        }
        layout->addWidget(list);
        return frame;
    }

    static QPushButton* addButton(PDFMessageBox* box, PDFAppDocumentActionDialogCloser* closer, const QString& text, const char* objectName,
                                  PDFMessageBox::ActionPosition position, bool isPrimary, PDFAppDocumentActionDialogs::Result result)
    {
        QPushButton* button = box->addActionButton(text, position, isPrimary);
        button->setObjectName(QString::fromLatin1(objectName));
        closer->add(button, result);
        return button;
    }

    /// Text of a value of the document for a dialog (one line, bounded). Format
    /// characters (bidirectional overrides and isolates as U+202E, zero width
    /// characters) are replaced too: "report" + U+202E + "fdp.exe" would be
    /// shown as "reportexe.pdf" in the confirmation of a program.
    static QString value(const QString& text, int maximumLength = 160)
    {
        QString result = text;
        for (QChar& character : result)
        {
            const QChar::Category category = character.category();
            if (category == QChar::Other_Control || category == QChar::Other_Format ||
                category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
            {
                character = QLatin1Char(' ');
            }
        }
        return PDFAppDocumentActionDialogs::shorten(result, maximumLength);
    }

    /// Text of a byte string of a Launch action of Windows (local 8-bit encoding)
    static QString windowsText(const QByteArray& text)
    {
        return QString::fromLocal8Bit(text.left(PDFAppDocumentActionHandler::MAXIMUM_URL_LENGTH));
    }

    static bool isPdf(const QString& filePath)
    {
        return filePath.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);
    }
};

QString PDFAppDocumentActionDialogs::shorten(const QString& text, int maximumLength)
{
    if (maximumLength < 3 || text.size() <= maximumLength)
    {
        return text;
    }

    const int half = (maximumLength - 1) / 2;
    return text.left(half) + QStringLiteral("…") + text.right(maximumLength - 1 - half);
}

PDFMessageBox* PDFAppDocumentActionDialogs::createLinkDialog(QWidget* parent, const QString& documentTitle, const QUrl& url)
{
    using Helper = PDFAppDocumentActionsHelper;

    const bool isMail = url.scheme().compare(QStringLiteral("mailto"), Qt::CaseInsensitive) == 0;
    const QString title = isMail ? tr("Open the e-mail link?") : tr("Open the web link?");
    const QString text = isMail ? tr("%1 links to an e-mail address. It opens in your e-mail program.").arg(Helper::value(documentTitle))
                                : tr("%1 links to a page on the internet. It opens in your web browser. Check the address – a link in a document can lead to a fake site.").arg(Helper::value(documentTitle));

    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Information, title, text, QDialogButtonBox::NoButton, parent);
    box->setObjectName(QStringLiteral("linkConfirmDialog"));
    box->setBadgeIconName(isMail ? QStringLiteral("mail") : QStringLiteral("external-link"));
    box->setCardWidth(LINK_WIDTH);

    const QString address = url.toDisplayString();
    const QString host = isMail ? url.path() : url.host();
    box->addContent(Helper::createAddress(Helper::value(host), Helper::value(address, 300), box));

    PDFAppDocumentActionDialogCloser* closer = new PDFAppDocumentActionDialogCloser(box);
    Helper::addButton(box, closer, tr("Copy Link"), "copyLinkButton", PDFMessageBox::ActionPosition::Leading, false, Result::CopyLink);
    Helper::addButton(box, closer, tr("Cancel"), "cancelButton", PDFMessageBox::ActionPosition::Trailing, false, Result::Rejected);
    Helper::addButton(box, closer, tr("Open Link"), "openLinkButton", PDFMessageBox::ActionPosition::Trailing, true, Result::Accepted);
    return box;
}

PDFMessageBox* PDFAppDocumentActionDialogs::createProgramDialog(QWidget* parent, const QString& documentTitle, const QString& program, const QString& parameters, const QString& directory)
{
    using Helper = PDFAppDocumentActionsHelper;

    const QString title = tr("Start a program from %1?").arg(Helper::value(documentTitle));
    const QString text = tr("The document wants to start a program on this computer. A program started from a document can damage your files or install malware. Start it only if you trust the author of the document.");

    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, title, text, QDialogButtonBox::NoButton, parent);
    box->setObjectName(QStringLiteral("launchConfirmDialog"));
    box->setCardWidth(LAUNCH_WIDTH);

    using Fact = PDFAppDocumentActionsHelper::Fact;
    using Kind = PDFComponentPainter::TextKind;
    QList<Fact> facts;
    facts << Fact{ tr("Program"), Helper::value(QDir::toNativeSeparators(program)), Kind::FileName };
    facts << Fact{ tr("Parameters"), parameters.isEmpty() ? tr("None") : Helper::value(parameters), Kind::Data };
    facts << Fact{ tr("Start in"), Helper::value(QDir::toNativeSeparators(directory)), Kind::FileName };
    box->addContent(Helper::createFacts(facts, box));

    PDFAppDocumentActionDialogCloser* closer = new PDFAppDocumentActionDialogCloser(box);
    Helper::addButton(box, closer, tr("Start Program"), "startProgramButton", PDFMessageBox::ActionPosition::Leading, false, Result::Accepted);
    Helper::addButton(box, closer, tr("Don’t Start"), "dontStartButton", PDFMessageBox::ActionPosition::Trailing, true, Result::Rejected);
    return box;
}

PDFMessageBox* PDFAppDocumentActionDialogs::createFileDialog(QWidget* parent, const QString& documentTitle, const QString& filePath)
{
    using Helper = PDFAppDocumentActionsHelper;

    const QFileInfo fileInfo(filePath);
    const QString suffix = fileInfo.suffix();
    const QString title = tr("Open a file from %1?").arg(Helper::value(documentTitle));
    const QString text = suffix.isEmpty() ? tr("The document wants to open a file in the program your system uses for it. A file from an unknown document can contain malware. Open it only if you trust the author of the document.")
                                          : tr("The document wants to open a file in the program your system uses for .%1 files. A file from an unknown document can contain malware. Open it only if you trust the author of the document.").arg(Helper::value(suffix));

    PDFMessageBox* box = new PDFMessageBox(PDFThemeIcons::Badge::Warning, title, text, QDialogButtonBox::NoButton, parent);
    box->setObjectName(QStringLiteral("launchFileConfirmDialog"));
    box->setCardWidth(LAUNCH_WIDTH);

    using Fact = PDFAppDocumentActionsHelper::Fact;
    using Kind = PDFComponentPainter::TextKind;
    QList<Fact> facts;
    facts << Fact{ tr("File"), Helper::value(fileInfo.fileName()), Kind::FileName };
    facts << Fact{ tr("Folder"), Helper::value(QDir::toNativeSeparators(fileInfo.absolutePath())), Kind::FileName };
    facts << Fact{ tr("Opens in"), suffix.isEmpty() ? tr("The program set for the file") : tr("The program set for .%1 files").arg(Helper::value(suffix)), Kind::Data };
    box->addContent(Helper::createFacts(facts, box));

    PDFAppDocumentActionDialogCloser* closer = new PDFAppDocumentActionDialogCloser(box);
    Helper::addButton(box, closer, tr("Open File"), "openFileButton", PDFMessageBox::ActionPosition::Leading, false, Result::Accepted);
    Helper::addButton(box, closer, tr("Don’t Open"), "dontOpenButton", PDFMessageBox::ActionPosition::Trailing, true, Result::Rejected);
    return box;
}

PDFAppDocumentActionDialogs::Result PDFAppDocumentActionDialogs::run(PDFMessageBox* dialog)
{
    QPointer<PDFMessageBox> guard(dialog);
    if (!guard)
    {
        return Result::Rejected;
    }

    const int code = guard->exec();

    // The parent of the dialog may have been deleted meanwhile (with the dialog)
    if (!guard)
    {
        return Result::Rejected;
    }
    delete guard.data();

    switch (code)
    {
        case int(Result::Accepted):
            return Result::Accepted;
        case int(Result::CopyLink):
            return Result::CopyLink;
        default:
            break;
    }
    return Result::Rejected;
}

PDFAppDocumentActionDialogs::Result PDFAppDocumentActionDialogs::confirmLink(QWidget* parent, const QString& documentTitle, const QUrl& url)
{
    return run(createLinkDialog(parent, documentTitle, url));
}

PDFAppDocumentActionDialogs::Result PDFAppDocumentActionDialogs::confirmProgram(QWidget* parent, const QString& documentTitle, const QString& program, const QString& parameters, const QString& directory)
{
    return run(createProgramDialog(parent, documentTitle, program, parameters, directory));
}

PDFAppDocumentActionDialogs::Result PDFAppDocumentActionDialogs::confirmFile(QWidget* parent, const QString& documentTitle, const QString& filePath)
{
    return run(createFileDialog(parent, documentTitle, filePath));
}

PDFAppDocumentActionHandler::PDFAppDocumentActionHandler(PDFAppDocumentTab* tab, QObject* parent) :
    BaseClass(parent),
    m_tab(tab)
{
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(0);
    connect(&m_hoverTimer, &QTimer::timeout, this, &PDFAppDocumentActionHandler::onHoverTimeout);

    PDFAppDocumentView* view = getView();
    if (view)
    {
        if (PDFWidgetAnnotationManager* annotationManager = view->getAnnotationManager())
        {
            connect(annotationManager, &PDFWidgetAnnotationManager::actionTriggered, this, &PDFAppDocumentActionHandler::onActionTriggered);
        }
        if (PDFWidgetFormManager* formManager = view->getFormManager())
        {
            connect(formManager, &PDFFormManager::actionTriggered, this, &PDFAppDocumentActionHandler::onActionTriggered);
        }
        connect(view, &PDFAppDocumentView::infoBarActionTriggered, this, &PDFAppDocumentActionHandler::onInfoBarActionTriggered);

        // The text selection is the tool of the canvas: a click on a link goes
        // through it to the annotation manager, which follows the link
        PDFToolManager* toolManager = view->getToolManager();
        if (PDFSelectTextTool* selectTextTool = toolManager ? toolManager->getSelectTextTool() : nullptr)
        {
            selectTextTool->setLinksClickable(true);
        }

        // The hover of the links: the events of the widget, which draws the pages
        PDFWidget* pdfWidget = view->getPdfWidget();
        if (pdfWidget && pdfWidget->getDrawWidget())
        {
            m_drawWidget = pdfWidget->getDrawWidget()->getWidget();
            if (m_drawWidget)
            {
                m_drawWidget->installEventFilter(this);
            }
        }
    }

    if (PDFAppDocument* document = getDocument())
    {
        connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentActionHandler::onDocumentStateChanged);
    }
    onDocumentStateChanged();
}

PDFAppDocumentActionHandler::~PDFAppDocumentActionHandler()
{
    if (m_drawWidget)
    {
        m_drawWidget->removeEventFilter(this);
    }

    // The tooltip of a link of the tab goes with it
    if (!m_hoverTitle.isEmpty() && PDFToolTip::isVisible() && PDFToolTip::getTitle() == m_hoverTitle)
    {
        PDFToolTip::hideText();
    }
}

PDFAppDocumentTab* PDFAppDocumentActionHandler::getTab() const
{
    return m_tab.data();
}

PDFAppDocumentView* PDFAppDocumentActionHandler::getView() const
{
    return m_tab ? m_tab->getView() : nullptr;
}

PDFAppDocument* PDFAppDocumentActionHandler::getDocument() const
{
    return m_tab ? m_tab->getDocument() : nullptr;
}

QString PDFAppDocumentActionHandler::getDocumentTitle() const
{
    const PDFAppDocument* document = getDocument();
    return document ? document->getTitle() : QString();
}

QString PDFAppDocumentActionHandler::getDocumentFolder() const
{
    const PDFAppDocument* document = getDocument();
    if (!document || document->getFilePath().isEmpty())
    {
        return QString();
    }
    return QFileInfo(document->getFilePath()).absolutePath();
}

void PDFAppDocumentActionHandler::setLaunchHandler(LaunchHandler handler)
{
    s_launchHandler = std::move(handler);
}

bool PDFAppDocumentActionHandler::launch(const PDFAppExternalLaunch& request)
{
    if (s_launchHandler)
    {
        return s_launchHandler(request);
    }

    switch (request.kind)
    {
        case PDFAppExternalLaunch::Kind::Url:
            return QDesktopServices::openUrl(request.url);

        case PDFAppExternalLaunch::Kind::File:
            return QDesktopServices::openUrl(QUrl::fromLocalFile(request.filePath));

        case PDFAppExternalLaunch::Kind::Program:
        {
#if defined(Q_OS_WIN)
            const std::wstring operation = request.operation.toStdWString();
            const std::wstring file = QDir::toNativeSeparators(request.filePath).toStdWString();
            const std::wstring parameters = request.parameters.toStdWString();
            const std::wstring directory = QDir::toNativeSeparators(request.directory).toStdWString();
            const HINSTANCE result = ::ShellExecuteW(nullptr, operation.empty() ? nullptr : operation.c_str(), file.c_str(),
                                                     parameters.empty() ? nullptr : parameters.c_str(), directory.empty() ? nullptr : directory.c_str(), SW_SHOWNORMAL);
            return reinterpret_cast<INT_PTR>(result) > 32;
#else
            return QProcess::startDetached(request.filePath, QProcess::splitCommand(request.parameters), request.directory);
#endif
        }
    }

    return false;
}

QUrl PDFAppDocumentActionHandler::resolveUri(const QByteArray& baseUri, const QByteArray& uri)
{
    if (uri.isEmpty() || uri.size() > MAXIMUM_URL_LENGTH || baseUri.size() > MAXIMUM_URL_LENGTH)
    {
        return QUrl();
    }

    // A URI is a 7-bit ASCII string (PDF 2.0, 12.6.4.8); a document may contain UTF-8
    QUrl url(QString::fromUtf8(uri).trimmed(), QUrl::TolerantMode);
    if (!url.isValid())
    {
        return QUrl();
    }

    // The base URI of the catalog applies only to a relative URI
    if (url.isRelative() && !baseUri.isEmpty())
    {
        const QUrl base(QString::fromUtf8(baseUri).trimmed(), QUrl::TolerantMode);
        if (base.isValid() && !base.isRelative())
        {
            url = base.resolved(url);
        }
    }

    // An e-mail link keeps only the usual header fields: "attach", "attachment" and
    // similar fields made e-mail programs attach a local file to the message
    // (CVE-2020-11880, CVE-2020-27748 - xdg-email passes them to Thunderbird)
    if (url.scheme().compare(QStringLiteral("mailto"), Qt::CaseInsensitive) == 0 && url.hasQuery())
    {
        const QUrlQuery query(url);
        QUrlQuery safeQuery;
        for (const QPair<QString, QString>& item : query.queryItems(QUrl::FullyEncoded))
        {
            const QString key = item.first.toLower();
            if (key == QLatin1String("to") || key == QLatin1String("cc") || key == QLatin1String("bcc") ||
                key == QLatin1String("subject") || key == QLatin1String("body"))
            {
                safeQuery.addQueryItem(item.first, item.second);
            }
        }
        url.setQuery(safeQuery.isEmpty() ? QString() : safeQuery.query(QUrl::FullyEncoded), QUrl::StrictMode);
    }
    return url;
}

bool PDFAppDocumentActionHandler::isAllowedUrl(const QUrl& url)
{
    if (!url.isValid() || url.toString().size() > MAXIMUM_URL_LENGTH)
    {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))
    {
        return !url.host().isEmpty();
    }
    if (scheme == QStringLiteral("mailto"))
    {
        return !url.path().isEmpty();
    }
    return false;
}

bool PDFAppDocumentActionHandler::isExecutable(const QString& filePath)
{
    // One list of programs and scripts with the attachments (D8): a file, which
    // the shell starts, gets the confirmation of a program
    const QFileInfo fileInfo(filePath);
    if (PDFAppAttachments::isExecutable(fileInfo.fileName()))
    {
        return true;
    }

    // A file with the executable permission (not a PDF document, not a folder)
    return fileInfo.isFile() && fileInfo.isExecutable() && !PDFAppDocumentActionsHelper::isPdf(filePath);
}

QString PDFAppDocumentActionHandler::resolveFile(const PDFFileSpecification& specification) const
{
    QString fileName = specification.getPlatformFileName().left(4096);
    if (fileName.isEmpty() || fileName.contains(QChar(0)))
    {
        return QString();
    }

    if (QDir::isRelativePath(fileName))
    {
        const QString folder = getDocumentFolder();
        if (folder.isEmpty())
        {
            return QString();
        }
        fileName = QDir(folder).absoluteFilePath(fileName);
    }
    return QDir::cleanPath(fileName);
}

void PDFAppDocumentActionHandler::onActionTriggered(const PDFAction* action)
{
    // The signal comes from the mouse handler of the canvas: a dialog of the action
    // (and the deletion of the tab while it is shown) must not run inside of it -
    // PDFDrawWidget and the managers still use themselves after the handler. The
    // copy is performed from the event loop; a second click before it replaces it.
    PDFActionPtr copy = action ? action->clone() : PDFActionPtr();
    if (!copy)
    {
        return;
    }

    const bool isScheduled = m_pendingAction != nullptr;
    m_pendingAction = std::move(copy);
    if (!isScheduled)
    {
        QMetaObject::invokeMethod(this, &PDFAppDocumentActionHandler::performPendingAction, Qt::QueuedConnection);
    }
}

void PDFAppDocumentActionHandler::performPendingAction()
{
    const PDFActionPtr action = std::move(m_pendingAction);
    m_pendingAction.reset();

    // The action may run a dialog and delete the tab (and this handler)
    performAction(action.get());
}

void PDFAppDocumentActionHandler::performAction(const PDFAction* action)
{
    PDFAppDocument* document = getDocument();
    if (!action || m_isPerforming || !document || !document->isReady())
    {
        return;
    }

    // A copy: the annotation of the action may be gone, when a dialog closes
    const PDFActionPtr copy = action->clone();
    if (!copy)
    {
        return;
    }

    QPointer<PDFAppDocumentActionHandler> guard(this);
    m_isPerforming = true;
    const quint64 version = document->getVersion();

    int count = 0;
    for (const PDFAction* item : copy->getActionList())
    {
        if (++count > MAXIMUM_CHAIN_LENGTH || !performOne(item, version) || !guard)
        {
            break;
        }
    }

    if (guard)
    {
        m_isPerforming = false;
    }
}

bool PDFAppDocumentActionHandler::performOne(const PDFAction* action, quint64 version)
{
    PDFAppDocument* document = getDocument();
    PDFAppDocumentView* view = getView();
    if (!action || !document || !view || !document->isReady() || document->getVersion() != version)
    {
        return false;
    }

    QPointer<PDFAppDocumentActionHandler> guard(this);
    switch (action->getType())
    {
        case ActionType::GoTo:
            performGoTo(static_cast<const PDFActionGoTo*>(action));
            return true;

        case ActionType::GoToR:
            performGoToR(static_cast<const PDFActionGoToR*>(action));
            return guard;

        case ActionType::URI:
            return performUri(static_cast<const PDFActionURI*>(action)) && guard;

        case ActionType::Launch:
            return performLaunch(static_cast<const PDFActionLaunch*>(action)) && guard;

        case ActionType::Named:
            performNamed(static_cast<const PDFActionNamed*>(action));
            return true;

        case ActionType::SetOCGState:
            performSetOCGState(static_cast<const PDFActionSetOCGState*>(action));
            return true;

        case ActionType::ResetForm:
            if (PDFWidgetFormManager* formManager = view->getFormManager())
            {
                formManager->performResetAction(static_cast<const PDFActionResetForm*>(action));
            }
            return guard;

        case ActionType::JavaScript:
            // Phase 1 does not run JavaScript (owner decision D5)
            showScriptsBar();
            return true;

        case ActionType::SubmitForm:
        {
            PDFAppInfoBarRequest request;
            request.id = getSubmitBlockedBarId();
            request.tone = PDFAppInfoBarTone::Warning;
            request.title = tr("The form was not sent – sending of forms is blocked");
            request.text = tr("PDF4QT does not send the data of forms to the internet.");
            view->showInfoBar(request);
            return true;
        }

        case ActionType::GoToE:
        case ActionType::GoToDp:
        case ActionType::Thread:
        case ActionType::Sound:
        case ActionType::Movie:
        case ActionType::Hide:
        case ActionType::Rendition:
        case ActionType::Transition:
        case ActionType::GoTo3DView:
        case ActionType::ImportDataForm:
        case ActionType::RichMediaExecute:
            showStatusMessage(tr("This kind of action is not supported."));
            return true;
    }

    return true;
}

void PDFAppDocumentActionHandler::performGoTo(const PDFActionGoTo* action)
{
    PDFAppDocument* document = getDocument();
    PDFAppDocumentView* view = getView();
    if (!document || !view || !view->getProxy() || !document->getDocument())
    {
        return;
    }

    PDFDestination destination = action->getDestination();
    if (destination.getDestinationType() == DestinationType::Named)
    {
        const QByteArray name = destination.getName();
        if (const PDFDestination* target = document->getDocument()->getCatalog()->getNamedDestination(name))
        {
            destination = *target;
        }
        else
        {
            showStatusMessage(tr("The destination “%1” was not found in the document.").arg(PDFAppDocumentActionDialogs::shorten(QString::fromUtf8(name), 80)));
            return;
        }
    }

    if (destination.getDestinationType() == DestinationType::Invalid || destination.getDestinationType() == DestinationType::Named)
    {
        showStatusMessage(tr("The destination of the link is not valid."));
        return;
    }

    view->getProxy()->goToDestination(destination);
}

bool PDFAppDocumentActionHandler::performUri(const PDFActionURI* action)
{
    PDFAppDocument* document = getDocument();
    if (!document || !document->getDocument() || !m_tab)
    {
        return false;
    }

    const QUrl url = resolveUri(document->getDocument()->getCatalog()->getBaseURI(), action->getURI());
    if (!isAllowedUrl(url))
    {
        showLinkBlocked(url, tr("The link was not opened – PDF4QT opens only web and e-mail links"), false);
        return true;
    }

    PDFAppContext* context = document->getContext();
    if (context && context->getDatabase() && !context->getSettings().getBool(PDFSettingKeys::securityOpenLinks()))
    {
        showLinkBlocked(url, tr("The web link was not opened – web links are turned off"), true);
        return true;
    }

    // Every click asks (no "Always allow", owner decision O8)
    QPointer<PDFAppDocumentActionHandler> guard(this);
    const PDFAppDocumentActionDialogs::Result result = PDFAppDocumentActionDialogs::confirmLink(m_tab, getDocumentTitle(), url);
    if (!guard)
    {
        return false;
    }

    switch (result)
    {
        case PDFAppDocumentActionDialogs::Result::Accepted:
        {
            PDFAppExternalLaunch request;
            request.kind = PDFAppExternalLaunch::Kind::Url;
            request.url = url;
            if (!launch(request))
            {
                showStatusMessage(tr("The link could not be opened."));
            }
            break;
        }

        case PDFAppDocumentActionDialogs::Result::CopyLink:
            QApplication::clipboard()->setText(url.toString());
            showStatusMessage(tr("The link has been copied."));
            break;

        case PDFAppDocumentActionDialogs::Result::Rejected:
            break;
    }
    return guard;
}

bool PDFAppDocumentActionHandler::performLaunch(const PDFActionLaunch* action)
{
    using Helper = PDFAppDocumentActionsHelper;

    PDFAppDocument* document = getDocument();
    PDFAppDocumentView* view = getView();
    if (!document || !view || !m_tab)
    {
        return false;
    }

    // The program of Windows, or the file of the specification
    PDFAppExternalLaunch request;
    QString filePath;
#if defined(Q_OS_WIN)
    const PDFActionLaunch::Win& windows = action->getWinSpecification();
    if (!windows.file.isEmpty())
    {
        // A relative program is resolved against the folder of the document only:
        // a bare name ("cmd.exe") of a document without a folder (a copy) would be
        // searched by the shell in the folders of the system
        filePath = Helper::windowsText(windows.file);
        if (filePath.contains(QChar(0)))
        {
            filePath.clear();
        }
        else if (QDir::isRelativePath(filePath))
        {
            const QString folder = getDocumentFolder();
            filePath = folder.isEmpty() ? QString() : QDir(folder).absoluteFilePath(filePath);
        }

        if (!filePath.isEmpty())
        {
            filePath = QDir::cleanPath(filePath);
            request.parameters = Helper::windowsText(windows.parameters);
            request.directory = Helper::windowsText(windows.directory);
            request.operation = getShellOperation(windows.operation);
        }
    }
#endif
    if (filePath.isEmpty())
    {
        filePath = resolveFile(action->getFileSpecification());
    }

    if (filePath.isEmpty())
    {
        showStatusMessage(tr("The action does not name a file, which can be opened."));
        return true;
    }

    // A PDF document opens in a new tab of PDF4QT (as GoToR)
    const bool isDocument = Helper::isPdf(filePath) && request.parameters.isEmpty();
    if (!checkTarget(filePath, 0, isDocument))
    {
        return true;
    }
    if (isDocument)
    {
        openDocument(filePath, 0);
        return true;
    }

    PDFAppContext* context = document->getContext();
    if (!context || !context->getDatabase() || !context->getSettings().getBool(PDFSettingKeys::securityLaunchFiles()))
    {
        PDFAppInfoBarRequest bar;
        bar.id = getLaunchBlockedBarId();
        bar.tone = PDFAppInfoBarTone::Warning;
        bar.title = tr("The program was not started – starting programs from documents is turned off");
        bar.actions.push_back({ getSecuritySettingsActionId(), tr("Security Settings") });
        view->showInfoBar(bar);
        return true;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists())
    {
        PDFAppInfoBarRequest bar;
        bar.id = getLinkFailedBarId();
        bar.tone = PDFAppInfoBarTone::Warning;
        bar.title = tr("The file was not found");
        bar.text = PDFAppDocumentActionDialogs::shorten(QDir::toNativeSeparators(filePath), 200);
        view->showInfoBar(bar);
        return true;
    }

    const bool isProgram = isExecutable(filePath);
    if (isProgram && request.directory.isEmpty())
    {
        request.directory = fileInfo.absolutePath();
    }

    QPointer<PDFAppDocumentActionHandler> guard(this);
    const PDFAppDocumentActionDialogs::Result result = isProgram ? PDFAppDocumentActionDialogs::confirmProgram(m_tab, getDocumentTitle(), filePath, request.parameters, request.directory)
                                                                 : PDFAppDocumentActionDialogs::confirmFile(m_tab, getDocumentTitle(), filePath);
    if (!guard)
    {
        return false;
    }
    if (result != PDFAppDocumentActionDialogs::Result::Accepted)
    {
        return true;
    }

    request.kind = isProgram ? PDFAppExternalLaunch::Kind::Program : PDFAppExternalLaunch::Kind::File;
    request.filePath = filePath;
    if (!launch(request))
    {
        showStatusMessage(isProgram ? tr("The program could not be started.") : tr("The file could not be opened."));
    }
    return guard;
}

void PDFAppDocumentActionHandler::performGoToR(const PDFActionGoToR* action)
{
    const QString filePath = resolveFile(action->getFileSpecification());
    if (filePath.isEmpty())
    {
        showStatusMessage(tr("The link does not name a document, which can be opened."));
        return;
    }

    // A destination of another document is a page number (from 0)
    const PDFInteger pageIndex = action->getDestination().getPageIndex();
    const int page = pageIndex >= 0 && pageIndex < std::numeric_limits<int>::max() ? int(pageIndex) + 1 : 0;

    // Nothing of another computer is touched before the user agrees (the
    // existence check would connect to the server)
    if (!checkTarget(filePath, page, true))
    {
        return;
    }

    if (!QFileInfo::exists(filePath))
    {
        if (PDFAppDocumentView* view = getView())
        {
            PDFAppInfoBarRequest bar;
            bar.id = getLinkFailedBarId();
            bar.tone = PDFAppInfoBarTone::Warning;
            bar.title = tr("The linked document was not found");
            bar.text = PDFAppDocumentActionDialogs::shorten(QDir::toNativeSeparators(filePath), 200);
            view->showInfoBar(bar);
        }
        return;
    }

    openDocument(filePath, page);
}

QString PDFAppDocumentActionHandler::getShellOperation(const QByteArray& operation)
{
    // PDF 2.0, 12.6.4.6, Table 204: "open" or "print"; another verb of the shell
    // ("runas" elevates the program) is the default operation
    const QByteArray lowerOperation = operation.trimmed().toLower();
    if (lowerOperation == "open" || lowerOperation == "print")
    {
        return QString::fromLatin1(lowerOperation);
    }
    return QString();
}

QString PDFAppDocumentActionHandler::getNetworkServer(const QString& filePath)
{
    QString path = filePath;
    path.replace(QChar(u'\\'), QChar(u'/'));

    QString rest;
    if (path.startsWith(QLatin1String("//?/UNC/"), Qt::CaseInsensitive) || path.startsWith(QLatin1String("//./UNC/"), Qt::CaseInsensitive))
    {
        rest = path.mid(8);
    }
    else if (path.startsWith(QLatin1String("//?/")) || path.startsWith(QLatin1String("//./")))
    {
        // A device path (isDevicePath)
        return QString();
    }
    else if (path.startsWith(QLatin1String("//")))
    {
        rest = path.mid(2);
    }
    else
    {
        return QString();
    }

    // "//" without a server is a network path of an unknown server
    const QString server = rest.section(QChar(u'/'), 0, 0, QString::SectionSkipEmpty).toLower();
    return server.isEmpty() ? QStringLiteral("/") : server;
}

bool PDFAppDocumentActionHandler::isDevicePath(const QString& filePath)
{
    QString path = filePath;
    path.replace(QChar(u'\\'), QChar(u'/'));
    return (path.startsWith(QLatin1String("//?/")) || path.startsWith(QLatin1String("//./"))) && getNetworkServer(path).isEmpty();
}

bool PDFAppDocumentActionHandler::checkTarget(const QString& filePath, int page, bool isDocument)
{
    if (isDevicePath(filePath))
    {
        showStatusMessage(tr("The link does not name a document, which can be opened."));
        return false;
    }

    // A path of another computer (\\server\share): already the connection sends
    // the sign-in of the user to the server (NTLM of SMB). The server of the
    // document itself is allowed (the user has opened the document from it).
    const QString server = getNetworkServer(filePath);
    const PDFAppDocument* document = getDocument();
    if (server.isEmpty() || (document && getNetworkServer(document->getFilePath()) == server))
    {
        return true;
    }

    PDFAppDocumentView* view = getView();
    if (!view)
    {
        return false;
    }

    m_blockedNetworkPath = isDocument ? filePath : QString();
    m_blockedNetworkPage = page;

    PDFAppInfoBarRequest bar;
    bar.id = getNetworkBlockedBarId();
    bar.tone = PDFAppInfoBarTone::Warning;
    bar.title = isDocument ? tr("The linked document is on another computer – it was not opened")
                           : tr("The file is on another computer – it was not opened");
    bar.text = PDFAppDocumentActionsHelper::value(QDir::toNativeSeparators(filePath), 200);
    if (isDocument)
    {
        bar.actions.push_back({ getOpenAnywayActionId(), tr("Open Anyway") });
    }
    view->showInfoBar(bar);
    return false;
}

void PDFAppDocumentActionHandler::openDocument(const QString& filePath, int page)
{
    PDFAppMainWindow* window = m_tab ? qobject_cast<PDFAppMainWindow*>(m_tab->window()) : nullptr;
    if (!window)
    {
        return;
    }

    PDFAppOpenRequest request;
    request.filePath = QFileInfo(filePath).absoluteFilePath();
    request.page = page;
    window->openDocuments(std::vector<PDFAppOpenRequest>{ request });
}

void PDFAppDocumentActionHandler::performNamed(const PDFActionNamed* action)
{
    PDFAppDocumentView* view = getView();
    PDFDrawWidgetProxy* proxy = view ? view->getProxy() : nullptr;
    if (!proxy)
    {
        return;
    }

    switch (action->getNamedActionType())
    {
        case PDFActionNamed::NamedActionType::NextPage:
            proxy->performOperation(PDFDrawWidgetProxy::NavigateNextPage);
            break;

        case PDFActionNamed::NamedActionType::PrevPage:
            proxy->performOperation(PDFDrawWidgetProxy::NavigatePreviousPage);
            break;

        case PDFActionNamed::NamedActionType::FirstPage:
            proxy->performOperation(PDFDrawWidgetProxy::NavigateDocumentStart);
            break;

        case PDFActionNamed::NamedActionType::LastPage:
            proxy->performOperation(PDFDrawWidgetProxy::NavigateDocumentEnd);
            break;

        case PDFActionNamed::NamedActionType::Custom:
            showStatusMessage(tr("The action “%1” is not supported.").arg(PDFAppDocumentActionDialogs::shorten(QString::fromLatin1(action->getCustomNamedAction()), 64)));
            break;
    }
}

void PDFAppDocumentActionHandler::performSetOCGState(const PDFActionSetOCGState* action)
{
    PDFAppDocument* document = getDocument();
    PDFOptionalContentActivity* activity = document ? document->getOptionalContentActivity() : nullptr;
    if (!activity)
    {
        return;
    }

    for (const PDFActionSetOCGState::StateChangeItem& stateChange : action->getStateChangeItems())
    {
        OCState newState = OCState::Unknown;
        switch (stateChange.first)
        {
            case PDFActionSetOCGState::SwitchType::ON:
                newState = OCState::ON;
                break;

            case PDFActionSetOCGState::SwitchType::OFF:
                newState = OCState::OFF;
                break;

            case PDFActionSetOCGState::SwitchType::Toggle:
            {
                const OCState oldState = activity->getState(stateChange.second);
                if (oldState == OCState::ON)
                {
                    newState = OCState::OFF;
                }
                else if (oldState == OCState::OFF)
                {
                    newState = OCState::ON;
                }
                break;
            }
        }

        if (newState != OCState::Unknown)
        {
            activity->setState(stateChange.second, newState, action->isRadioButtonsPreserved());
        }
    }
}

void PDFAppDocumentActionHandler::showScriptsBar()
{
    PDFAppDocumentView* view = getView();
    if (!view)
    {
        return;
    }

    PDFAppInfoBarRequest request;
    request.id = getScriptsBarId();
    request.tone = PDFAppInfoBarTone::Information;
    request.title = tr("Scripts of this document are not run");
    request.text = tr("PDF4QT does not run JavaScript. Fields of forms are not calculated or checked by the scripts of the document.");
    view->showInfoBar(request);
}

void PDFAppDocumentActionHandler::showLinkBlocked(const QUrl& url, const QString& title, bool isSettingOff)
{
    PDFAppDocumentView* view = getView();
    if (!view)
    {
        return;
    }

    m_blockedLink = url.isValid() ? url.toString().left(MAXIMUM_URL_LENGTH) : QString();

    PDFAppInfoBarRequest request;
    request.id = getLinkBlockedBarId();
    request.tone = PDFAppInfoBarTone::Error;
    request.title = title;
    if (!m_blockedLink.isEmpty())
    {
        request.actions.push_back({ getCopyLinkActionId(), tr("Copy Link") });
    }
    if (isSettingOff)
    {
        request.actions.push_back({ getSecuritySettingsActionId(), tr("Security Settings") });
    }
    view->showInfoBar(request);
}

void PDFAppDocumentActionHandler::onInfoBarActionTriggered(const QString& infoBarId, const QString& actionId)
{
    if (infoBarId == getNetworkBlockedBarId())
    {
        // Open Anyway: the user has agreed to connect to the other computer
        const QString filePath = m_blockedNetworkPath;
        m_blockedNetworkPath.clear();
        if (actionId == getOpenAnywayActionId() && !filePath.isEmpty())
        {
            if (PDFAppDocumentView* view = getView())
            {
                view->hideInfoBar(getNetworkBlockedBarId());
            }
            openDocument(filePath, m_blockedNetworkPage);
        }
        return;
    }

    if (infoBarId != getLinkBlockedBarId() && infoBarId != getLaunchBlockedBarId())
    {
        return;
    }

    if (actionId == getCopyLinkActionId() && !m_blockedLink.isEmpty())
    {
        QApplication::clipboard()->setText(m_blockedLink);
        showStatusMessage(tr("The link has been copied."));
    }
    else if (actionId == getSecuritySettingsActionId() && m_tab)
    {
        const QString anchor = infoBarId == getLinkBlockedBarId() ? QStringLiteral("allow-uri-link-launch") : QStringLiteral("allow-application-launch");
        Q_EMIT m_tab->navigationRequested(PDFAppRoutes::settings(QStringLiteral("security"), anchor), PDFAppNavigationMode::Push);
    }
}

void PDFAppDocumentActionHandler::onDocumentStateChanged()
{
    // A document with scripts says once, that they are not run
    PDFAppDocument* document = getDocument();
    if (m_scriptsChecked || !document || !document->isReady() || !document->getDocument())
    {
        return;
    }
    m_scriptsChecked = true;

    const PDFJavaScriptScanner scanner(document->getDocument().data());
    const PDFJavaScriptScanner::Entries entries = scanner.scan({ }, PDFJavaScriptScanner::Options(PDFJavaScriptScanner::ScanDocument | PDFJavaScriptScanner::ScanNamed |
                                                                                                    PDFJavaScriptScanner::ScanForm | PDFJavaScriptScanner::FindFirstOnly));
    if (!entries.empty())
    {
        showScriptsBar();
    }
}

void PDFAppDocumentActionHandler::showStatusMessage(const QString& text)
{
    PDFAppDocumentView* view = getView();
    if (view && view->getStatusBar())
    {
        view->getStatusBar()->showMessage(text, 5000);
    }
}

void PDFAppDocumentActionHandler::describe(const PDFAction* action, QString* title, QString* description, QString* status) const
{
    title->clear();
    description->clear();
    status->clear();
    if (!action)
    {
        return;
    }

    const PDFAppDocument* document = getDocument();
    const PDFDocument* pdfDocument = document ? document->getDocument().data() : nullptr;
    switch (action->getType())
    {
        case ActionType::URI:
        {
            const QUrl url = resolveUri(pdfDocument ? pdfDocument->getCatalog()->getBaseURI() : QByteArray(), static_cast<const PDFActionURI*>(action)->getURI());
            const QString address = PDFAppDocumentActionDialogs::shorten(url.isValid() ? url.toDisplayString() : QString::fromUtf8(static_cast<const PDFActionURI*>(action)->getURI()), 200);
            *title = address;
            if (!isAllowedUrl(url))
            {
                *description = tr("Link · PDF4QT opens only web and e-mail links");
                *status = tr("Link: %1").arg(address);
            }
            else if (url.scheme().compare(QStringLiteral("mailto"), Qt::CaseInsensitive) == 0)
            {
                *description = tr("E-mail link · opens in your e-mail program");
                *status = tr("E-mail link: %1 · click to open it in your e-mail program").arg(address);
            }
            else
            {
                *description = tr("Web link · opens in your web browser");
                *status = tr("Web link: %1 · click to open it in your web browser").arg(address);
            }
            break;
        }

        case ActionType::GoTo:
        {
            PDFDestination destination = static_cast<const PDFActionGoTo*>(action)->getDestination();
            if (destination.getDestinationType() == DestinationType::Named && pdfDocument)
            {
                const PDFDestination* target = pdfDocument->getCatalog()->getNamedDestination(destination.getName());
                destination = target ? *target : PDFDestination();
            }

            PDFInteger pageIndex = -1;
            if (pdfDocument && destination.getPageReference().isValid())
            {
                const size_t index = pdfDocument->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
                pageIndex = index != PDFCatalog::INVALID_PAGE_INDEX ? PDFInteger(index) : -1;
            }
            else if (destination.getDestinationType() != DestinationType::Invalid)
            {
                pageIndex = destination.getPageIndex();
            }

            if (pageIndex >= 0 && pdfDocument)
            {
                const QString label = pdfDocument->getCatalog()->getPageLabel(pageIndex);
                const QString page = label.isEmpty() || label == QString::number(pageIndex + 1) ? QString::number(pageIndex + 1) : tr("%1 (%2)").arg(pageIndex + 1).arg(label);
                *title = tr("Page %1").arg(page);
            }
            else
            {
                *title = tr("Destination not found");
            }
            *description = tr("Link in this document");
            *status = tr("Link: %1 · click to go there").arg(*title);
            break;
        }

        case ActionType::GoToR:
        {
            const QString filePath = resolveFile(static_cast<const PDFActionGoToR*>(action)->getFileSpecification());
            *title = PDFAppDocumentActionDialogs::shorten(QFileInfo(filePath).fileName(), 120);
            *description = tr("Link to another document · opens in a new tab");
            *status = tr("Link to %1 · click to open it in a new tab").arg(*title);
            break;
        }

        case ActionType::Launch:
        {
            const QString filePath = resolveFile(static_cast<const PDFActionLaunch*>(action)->getFileSpecification());
            *title = filePath.isEmpty() ? tr("Program") : PDFAppDocumentActionDialogs::shorten(QFileInfo(filePath).fileName(), 120);
            *description = tr("Program or file · asks before it starts");
            *status = tr("Starts %1 · asks before it starts").arg(*title);
            break;
        }

        case ActionType::Named:
            *title = tr("Navigation");
            *description = tr("Link in this document");
            *status = *description;
            break;

        case ActionType::JavaScript:
            *title = tr("Script");
            *description = tr("Scripts of documents are not run");
            *status = *description;
            break;

        default:
            break;
    }
}

bool PDFAppDocumentActionHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_drawWidget)
    {
        switch (event->type())
        {
            case QEvent::MouseMove:
            {
                // The manager of the annotations updates the hover of the links at the
                // same event (after this filter): the link is asked for later
                const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
                m_hoverPosition = mouseEvent->position().toPoint();
                if (mouseEvent->buttons() == Qt::NoButton)
                {
                    m_hoverTimer.start();
                }
                break;
            }

            case QEvent::Leave:
            case QEvent::Hide:
                m_hoverTimer.stop();
                clearHover();
                break;

            case QEvent::ToolTip:
            {
                // The tooltip of the design of a link (1028:22472): the compact
                // tooltip, the address and its kind Regular 11
                if (!m_hoverTitle.isEmpty())
                {
                    const QHelpEvent* helpEvent = static_cast<const QHelpEvent*>(event);
                    PDFToolTip::showText(helpEvent->globalPos(), m_hoverTitle, QString(), m_hoverDescription, m_drawWidget, PDFToolTip::Style::Compact);
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFAppDocumentActionHandler::onHoverTimeout()
{
    updateHover(m_hoverPosition);
}

void PDFAppDocumentActionHandler::updateHover(const QPoint& position)
{
    PDFAppDocumentView* view = getView();
    PDFWidgetAnnotationManager* annotationManager = view ? view->getAnnotationManager() : nullptr;
    const PDFAction* action = annotationManager ? annotationManager->getLinkActionAt(position) : nullptr;
    if (!action)
    {
        clearHover();
        return;
    }

    QString title;
    QString description;
    QString status;
    describe(action, &title, &description, &status);
    if (title == m_hoverTitle && status == m_hoverStatus)
    {
        return;
    }

    const bool wasToolTipShown = !m_hoverTitle.isEmpty() && PDFToolTip::isVisible() && PDFToolTip::getTitle() == m_hoverTitle;
    m_hoverTitle = title;
    m_hoverDescription = description;
    m_hoverStatus = status;

    if (wasToolTipShown)
    {
        PDFToolTip::hideText();
    }

    if (view->getStatusBar() && !status.isEmpty())
    {
        // A temporary message, while the pointer is over the link
        m_shownStatus = status;
        view->getStatusBar()->showMessage(status, 60 * 60 * 1000);
    }
}

void PDFAppDocumentActionHandler::clearHover()
{
    if (m_hoverTitle.isEmpty() && m_hoverStatus.isEmpty())
    {
        return;
    }

    if (PDFToolTip::isVisible() && PDFToolTip::getTitle() == m_hoverTitle)
    {
        PDFToolTip::hideText();
    }

    // The message of the link ends at once (the permanent message comes back)
    PDFAppDocumentView* view = getView();
    if (view && view->getStatusBar() && !m_shownStatus.isEmpty() && view->getStatusBar()->getMessage() == m_shownStatus)
    {
        view->getStatusBar()->showMessage(m_shownStatus, 1);
    }

    m_hoverTitle.clear();
    m_hoverDescription.clear();
    m_hoverStatus.clear();
    m_shownStatus.clear();
}

}   // namespace pdf
