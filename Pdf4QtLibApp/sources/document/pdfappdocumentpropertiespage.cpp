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

#include "pdfappdocumentpropertiespage.h"
#include "pdfapplocale.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentproperties.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfapppagelayout.h"
#include "pdfapprenderingissues.h"
#include "pdfapproute.h"
#include "pdfappcommand.h"
#include "pdfrecentrepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace pdf
{

/// Colors of the XML of the XMP packet (236:9661): the tags and the names of the
/// attributes text/accent, the values of the attributes accent/green/icon, the
/// texts text/primary. The colors are tokens of the theme read at each
/// highlighting (rehighlight at a change of the theme).
class PDFAppXmlHighlighter : public QSyntaxHighlighter
{
public:
    explicit PDFAppXmlHighlighter(QTextDocument* document) :
        QSyntaxHighlighter(document)
    {

    }

protected:
    virtual void highlightBlock(const QString& text) override
    {
        QTextCharFormat tagFormat;
        tagFormat.setForeground(PDFStylePainter::color(PDFThemeColors::TextAccent));
        QTextCharFormat valueFormat;
        valueFormat.setForeground(PDFStylePainter::color(PDFThemeColors::AccentGreenIcon));

        // The state of the previous line: 1 = inside of a tag, 2 = inside of a value of a tag
        int state = qMax(0, previousBlockState());
        qsizetype start = 0;
        for (qsizetype i = 0; i < text.size(); ++i)
        {
            const QChar character = text.at(i);
            switch (state)
            {
                case 0:
                    if (character == QLatin1Char('<'))
                    {
                        state = 1;
                        start = i;
                    }
                    break;

                case 1:
                    if (character == QLatin1Char('"') || character == QLatin1Char('\''))
                    {
                        setFormat(int(start), int(i - start), tagFormat);
                        state = character == QLatin1Char('"') ? 2 : 3;
                        start = i;
                    }
                    else if (character == QLatin1Char('>'))
                    {
                        setFormat(int(start), int(i - start + 1), tagFormat);
                        state = 0;
                    }
                    break;

                case 2:
                case 3:
                    if ((state == 2 && character == QLatin1Char('"')) || (state == 3 && character == QLatin1Char('\'')))
                    {
                        setFormat(int(start), int(i - start + 1), valueFormat);
                        state = 1;
                        start = i + 1;
                    }
                    break;

                default:
                    state = 0;
                    break;
            }
        }

        // An open tag or value continues on the next line
        if (state == 1)
        {
            setFormat(int(start), int(text.size() - start), tagFormat);
        }
        else if (state == 2 || state == 3)
        {
            setFormat(int(start), int(text.size() - start), valueFormat);
        }
        setCurrentBlockState(state);
    }
};

/// Code box of the XMP packet (236:9660): bg/subtle with a border/default line
/// of radius 12 around the editor (the editor itself is a note without a border)
class PDFAppXmlCodeBox : public QWidget
{
public:
    explicit PDFAppXmlCodeBox(qreal radius, QWidget* parent) :
        QWidget(parent),
        m_radius(radius)
    {
        setObjectName(QStringLiteral("metadataCodeBox"));
        connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const QRectF box(rect());
        PDFStylePainter::fill(&painter, box, PDFStylePainter::Radii(m_radius), PDFThemeColors::BgSubtle);
        PDFStylePainter::strokeInside(&painter, box, PDFStylePainter::Radii(m_radius), PDFThemeColors::BorderDefault, 1.0);
    }

private:
    qreal m_radius;
};

/// Helpers of the page (file-local)
class PDFAppDocumentPropertiesPageHelper
{
public:
    PDFAppDocumentPropertiesPageHelper() = delete;

    /// Dynamic property of a property row: its key of the Info dictionary
    static constexpr const char* INFO_KEY_PROPERTY = "pdf4qtInfoKey";

    /// Widths of the design (236:8882)
    static constexpr int NAVIGATION_WIDTH = 300;
    static constexpr int COLUMN_MINIMUM_WIDTH = 300;
    static constexpr int GROUP_GAP = 18;
    static constexpr int FONT_LIST_PADDING = 4;     ///< rows of the card 236:9344 inside of its border
    static constexpr qreal CODE_RADIUS = 12.0;      ///< code box 236:9660 (bg/subtle, border/default)

    static QString getSectionTitle(PDFAppDocumentPropertiesPage::Section section)
    {
        switch (section)
        {
            case PDFAppDocumentPropertiesPage::Section::General:
                return PDFAppDocumentPropertiesPage::tr("Properties");
            case PDFAppDocumentPropertiesPage::Section::Security:
                return PDFAppDocumentPropertiesPage::tr("Security");
            case PDFAppDocumentPropertiesPage::Section::Fonts:
                return PDFAppDocumentPropertiesPage::tr("Fonts");
            case PDFAppDocumentPropertiesPage::Section::Display:
                return PDFAppDocumentPropertiesPage::tr("Display and print");
            case PDFAppDocumentPropertiesPage::Section::Xmp:
                return PDFAppDocumentPropertiesPage::tr("XMP metadata");
        }
        return QString();
    }

    static QString getSectionIcon(PDFAppDocumentPropertiesPage::Section section)
    {
        switch (section)
        {
            case PDFAppDocumentPropertiesPage::Section::General:
                return QStringLiteral("info");
            case PDFAppDocumentPropertiesPage::Section::Security:
                return QStringLiteral("lock");
            case PDFAppDocumentPropertiesPage::Section::Fonts:
                return QStringLiteral("type");
            case PDFAppDocumentPropertiesPage::Section::Display:
                return QStringLiteral("monitor");
            case PDFAppDocumentPropertiesPage::Section::Xmp:
                return QStringLiteral("code");
        }
        return QString();
    }

    /// Monospace font of the XML (JetBrains Mono 11 of the design, or a monospace font of the system)
    static QFont monospaceFont()
    {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setFamilies({ QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                           QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Menlo"), font.family() });
        font.setStyleHint(QFont::TypeWriter);
        font.setFixedPitch(true);
        font.setPixelSize(11);
        return font;
    }

    /// Column of a section (the groups 18 px apart, at the top)
    static QVBoxLayout* columnLayout(PDFAppColumns* columns, int index)
    {
        QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(columns->getColumnLayout(index));
        if (layout)
        {
            layout->setSpacing(GROUP_GAP);
        }
        return layout;
    }
};

PDFAppDocumentPropertiesBar::PDFAppDocumentPropertiesBar(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("propertiesBar"));
    setFixedHeight(HEIGHT);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentPropertiesBar::update));
}

PDFAppDocumentPropertiesBar::~PDFAppDocumentPropertiesBar() = default;

void PDFAppDocumentPropertiesBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // bg/subtle with border/default at the bottom (236:9026)
    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgSubtle);
    PDFStylePainter::fillRect(&painter, QRectF(0, height() - 1, width(), 1), PDFThemeColors::BorderDefault);
}

PDFAppDocumentPropertiesPage::PDFAppDocumentPropertiesPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{
    setObjectName(QStringLiteral("documentPropertiesPage"));

    // The navigator creates the page with its tab as the parent
    for (QObject* object = parent; object; object = object->parent())
    {
        if (PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(object))
        {
            m_tab = tab;
            m_document = tab->getDocument();
            break;
        }
    }

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(0);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &PDFAppDocumentPropertiesPage::onRebuildTimeout);

    if (m_document)
    {
        connect(m_document.data(), &PDFAppDocument::documentChanged, this, &PDFAppDocumentPropertiesPage::onDocumentChanged);
        connect(m_document.data(), &PDFAppDocument::stateChanged, this, &PDFAppDocumentPropertiesPage::onDocumentStateChanged);
        connect(m_document.data(), &PDFAppDocument::lockChanged, this, &PDFAppDocumentPropertiesPage::onDocumentStateChanged);
        connect(m_document.data(), &PDFAppDocument::operationsChanged, this, &PDFAppDocumentPropertiesPage::onDocumentStateChanged);
    }
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppDocumentPropertiesPage::onThemeChanged);

    buildSkeleton();
}

PDFAppDocumentPropertiesPage::~PDFAppDocumentPropertiesPage()
{
    // A running scan of the fonts ends by itself, its result is dropped
    if (m_fontScanCancel)
    {
        m_fontScanCancel->store(true);
    }
    if (m_fontWatcher)
    {
        m_fontWatcher->disconnect(this);
    }
}

QStringList PDFAppDocumentPropertiesPage::getSectionIds()
{
    return { QStringLiteral("general"), QStringLiteral("security"), QStringLiteral("fonts"), QStringLiteral("display"), QStringLiteral("xmp") };
}

QString PDFAppDocumentPropertiesPage::getSectionId() const
{
    return getSectionIds().value(int(m_section));
}

PDFPlainTextEdit* PDFAppDocumentPropertiesPage::getMetadataEditor() const
{
    return m_metadataEditor.data();
}

PDFAppDocumentTab* PDFAppDocumentPropertiesPage::getDocumentTab() const
{
    return m_tab.data();
}

bool PDFAppDocumentPropertiesPage::isEditable() const
{
    return m_document && m_document->isEditable() && !m_document->isBlockedByOperation() && m_document->getDocument();
}

void PDFAppDocumentPropertiesPage::buildSkeleton()
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // properties-bar 236:9026: Back to document | Document properties ... Copy all
    m_bar = new PDFAppDocumentPropertiesBar(this);
    QHBoxLayout* barLayout = new QHBoxLayout(m_bar);
    barLayout->setContentsMargins(12, 0, 12, 1);
    barLayout->setSpacing(12);
    m_backButton = PDFAppPageWidgets::createButton(tr("Back to document"), getDirectionalIconName(QStringLiteral("chevron-left"), layoutDirection()), false, m_bar);
    m_backButton->setObjectName(QStringLiteral("backButton"));
    connect(m_backButton.data(), &QAbstractButton::clicked, this, &PDFAppDocumentPropertiesPage::onBackClicked);
    QFrame* divider = new QFrame(m_bar);
    divider->setObjectName(QStringLiteral("divider"));
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedHeight(20);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Document properties"), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, m_bar);
    title->setObjectName(QStringLiteral("barTitle"));
    m_copyAllButton = PDFAppPageWidgets::createButton(tr("Copy all"), QStringLiteral("copy"), false, m_bar);
    m_copyAllButton->setObjectName(QStringLiteral("copyAllButton"));
    connect(m_copyAllButton.data(), &QAbstractButton::clicked, this, &PDFAppDocumentPropertiesPage::onCopyAllClicked);
    barLayout->addWidget(m_backButton);
    barLayout->addWidget(divider);
    barLayout->addWidget(title);
    barLayout->addStretch(1);
    barLayout->addWidget(m_copyAllButton);
    layout->addWidget(m_bar);

    // properties-body: the navigation (300, bg/sidebar) and the content
    QWidget* body = new QWidget(this);
    body->setObjectName(QStringLiteral("propertiesBody"));
    QHBoxLayout* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    QWidget* navigationPanel = new QWidget(body);
    navigationPanel->setObjectName(QStringLiteral("propertiesNavigation"));
    PDFWidgetStyleProperties::set(navigationPanel, PDFWidgetStyleProperties::PANEL);
    navigationPanel->setFixedWidth(Helper::NAVIGATION_WIDTH);
    QVBoxLayout* navigationLayout = new QVBoxLayout(navigationPanel);
    navigationLayout->setContentsMargins(12, 16, 13, 16);
    navigationLayout->setSpacing(2);

    // document-summary 236:9043: the thumbnail, the name and the pages with the size
    QWidget* summary = new QWidget(navigationPanel);
    summary->setObjectName(QStringLiteral("documentSummary"));
    QHBoxLayout* summaryLayout = new QHBoxLayout(summary);
    summaryLayout->setContentsMargins(4, 4, 4, 16);
    summaryLayout->setSpacing(12);
    m_thumbnail = new PDFPageThumbnail(PDFPageThumbnail::Size::Medium, summary);
    m_thumbnail->setObjectName(QStringLiteral("summaryThumbnail"));
    QVBoxLayout* summaryText = new QVBoxLayout();
    summaryText->setContentsMargins(0, 0, 0, 0);
    summaryText->setSpacing(3);
    m_fileName = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, summary);
    m_fileName->setObjectName(QStringLiteral("summaryName"));
    m_fileName->setMinimumWidth(40);
    m_fileMeta = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, summary);
    m_fileMeta->setObjectName(QStringLiteral("summaryMeta"));
    summaryText->addStretch(1);
    summaryText->addWidget(m_fileName);
    summaryText->addWidget(m_fileMeta);
    summaryText->addStretch(1);
    summaryLayout->addWidget(m_thumbnail);
    summaryLayout->addLayout(summaryText, 1);
    navigationLayout->addWidget(summary);

    m_navigation = new PDFNavList(PDFNavItem::Kind::Home, navigationPanel);
    m_navigation->setObjectName(QStringLiteral("propertiesNavigationList"));
    m_navigation->setAccessibleName(tr("Sections of the properties"));
    for (int i = 0; i < getSectionIds().size(); ++i)
    {
        const Section section = Section(i);
        m_navigation->addItem(Helper::getSectionTitle(section), Helper::getSectionIcon(section));
        if (PDFNavItem* item = m_navigation->getItem(i))
        {
            item->setObjectName(QStringLiteral("navigation-%1").arg(getSectionIds().value(i)));
        }
    }
    connect(m_navigation.data(), &PDFNavList::currentIndexChanged, this, &PDFAppDocumentPropertiesPage::onNavIndexChanged);
    navigationLayout->addWidget(m_navigation);
    navigationLayout->addStretch(1);
    bodyLayout->addWidget(navigationPanel);

    // properties-content 236:9064: paddings 40 / 28 / 40 / 32, sections 20 apart
    m_scrollArea = new QScrollArea(body);
    m_scrollArea->setObjectName(QStringLiteral("propertiesScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    PDFWidgetStyleProperties::set(m_scrollArea, PDFWidgetStyleProperties::CANVAS);
    m_content = new QWidget(m_scrollArea);
    m_content->setObjectName(QStringLiteral("propertiesContent"));
    PDFWidgetStyleProperties::set(m_content, PDFWidgetStyleProperties::CANVAS);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(40, 28, 40, 32);
    m_contentLayout->setSpacing(0);
    m_scrollArea->setWidget(m_content);
    bodyLayout->addWidget(m_scrollArea, 1);
    layout->addWidget(body, 1);

    // The status bar of the tab (236:8947): Ready and the rendering issues
    m_statusBar = new PDFAppDocumentStatusBar(this);
    m_statusBar->setObjectName(QStringLiteral("propertiesStatusBar"));
    m_statusBar->showMessage(tr("Ready"));
    if (PDFAppDocumentPropertiesCommandHandler* handler = m_tab ? qobject_cast<PDFAppDocumentPropertiesCommandHandler*>(m_tab->findHandler(PDFAppCommands::properties())) : nullptr)
    {
        if (PDFAppRenderingIssues* issues = handler->getRenderingIssues())
        {
            m_statusBar->addItem(issues->createStatusItem(m_statusBar), PDFAppDocumentStatusBar::Area::Trailing);
        }
    }
    layout->addWidget(m_statusBar);

    updateSummary();
}

bool PDFAppDocumentPropertiesPage::setRoute(const PDFAppRoute& route)
{
    // Only the properties of the document of the tab: document / {id} / properties / {section}
    const QStringList& segments = route.getSegments();
    if (!m_tab || !m_document || segments.size() != 4 || segments[1] != m_tab->getDocumentId())
    {
        return false;
    }

    const int index = int(getSectionIds().indexOf(segments[3]));
    if (index < 0)
    {
        return false;
    }

    if (!BaseClass::setRoute(route))
    {
        return false;
    }

    m_section = Section(index);
    if (m_navigation && m_navigation->getCurrentIndex() != index)
    {
        m_navigation->setCurrentIndex(index);
    }
    rebuildSection();
    Q_EMIT titleChanged();
    return true;
}

QString PDFAppDocumentPropertiesPage::getTitle() const
{
    const QString title = m_document ? m_document->getTitle() : QString();
    return title.isEmpty() ? tr("Document properties") : tr("%1 – Document properties").arg(title);
}

bool PDFAppDocumentPropertiesPage::hasSearchHeader() const
{
    return false;
}

QVariantMap PDFAppDocumentPropertiesPage::saveViewState() const
{
    QVariantMap state;
    if (m_scrollArea)
    {
        state.insert(QStringLiteral("scroll"), m_scrollArea->verticalScrollBar()->value());
    }
    return state;
}

void PDFAppDocumentPropertiesPage::restoreViewState(const QVariantMap& state)
{
    bool ok = false;
    const int position = state.value(QStringLiteral("scroll")).toInt(&ok);
    if (ok && position >= 0 && m_scrollArea)
    {
        m_pendingScroll = position;
        m_scrollArea->verticalScrollBar()->setValue(position);
    }
}

void PDFAppDocumentPropertiesPage::onNavIndexChanged(int index)
{
    if (index < 0 || index >= getSectionIds().size() || Section(index) == m_section || !m_tab)
    {
        return;
    }

    // The section is a part of the route: the current history entry is replaced
    // (Back returns to the document, not to the previous section)
    m_section = Section(index);
    QPointer<PDFAppDocumentPropertiesPage> guard(this);
    if (!replaceRoute(PDFAppRoutes::documentProperties(m_tab->getDocumentId(), getSectionId())) || !guard)
    {
        return;
    }
    rebuildSection();
    if (m_scrollArea)
    {
        m_scrollArea->verticalScrollBar()->setValue(0);
    }
}

void PDFAppDocumentPropertiesPage::onBackClicked()
{
    if (!m_tab)
    {
        return;
    }

    // Back to the document: Back, when the previous entry of the tab is the
    // document, otherwise the document as a new entry
    const PDFAppHistory& history = m_tab->getHistory();
    const PDFAppHistoryEntry* previous = history.getEntry(history.getCurrentIndex() - 1);
    const QStringList segments = previous ? previous->route.getSegments() : QStringList();
    if (segments.size() == 2 && segments[0] == QStringLiteral("document") && segments[1] == m_tab->getDocumentId())
    {
        Q_EMIT backRequested();
    }
    else
    {
        requestNavigation(PDFAppRoutes::document(m_tab->getDocumentId()), PDFAppNavigationMode::Push);
    }
}

QString PDFAppDocumentPropertiesPage::getCopyText() const
{
    const PDFDocument* document = m_document ? m_document->getDocument().data() : nullptr;
    if (!document)
    {
        return QString();
    }

    std::vector<PDFAppPropertyGroup> groups = PDFAppDocumentPropertiesData::createGeneralGroups(document, m_document->getFilePath(), m_document->getTitle(), 0);

    groups.push_back(PDFAppDocumentPropertiesData::createEncryptionGroup(document));
    PDFAppPropertyGroup permissions;
    permissions.title = tr("Permissions");
    for (const PDFAppPermissionItem& permission : PDFAppDocumentPropertiesData::createPermissions(document))
    {
        PDFAppPropertyItem item;
        item.key = permission.text;
        item.value = permission.isAllowed ? tr("Allowed") : tr("Not allowed");
        permissions.items.push_back(item);
    }
    groups.push_back(std::move(permissions));

    if (m_hasFonts)
    {
        PDFAppPropertyGroup fonts;
        fonts.title = tr("Fonts");
        for (const PDFAppFontInfo& font : m_fonts)
        {
            PDFAppPropertyItem item;
            item.key = font.name;
            item.value = tr("%1, %2").arg(font.type, font.getStatusText());
            fonts.items.push_back(item);
        }
        groups.push_back(std::move(fonts));
    }

    for (PDFAppPropertyGroup& group : PDFAppDocumentPropertiesData::createDisplayGroups(document))
    {
        groups.push_back(std::move(group));
    }

    return PDFAppDocumentPropertiesData::createCopyText(tr("Document properties – %1").arg(m_document->getTitle()), groups);
}

void PDFAppDocumentPropertiesPage::onCopyAllClicked()
{
    QApplication::clipboard()->setText(getCopyText());
    showStatusMessage(tr("The properties have been copied."));
}

void PDFAppDocumentPropertiesPage::showStatusMessage(const QString& text)
{
    if (m_statusBar)
    {
        m_statusBar->showMessage(text, 5000);
    }
}

void PDFAppDocumentPropertiesPage::updateSummary()
{
    if (!m_document)
    {
        return;
    }

    if (m_fileName)
    {
        m_fileName->setText(m_document->getTitle());
        m_fileName->setToolTip(m_document->getTitle());
    }

    if (m_fileMeta)
    {
        const int pageCount = int(qBound(PDFInteger(0), m_document->getPageCount(), PDFInteger(std::numeric_limits<int>::max())));
        QString meta = pageCount == 1 ? tr("%Ln page", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount);
        const QFileInfo fileInfo(m_document->getFilePath());
        if (!m_document->getFilePath().isEmpty() && fileInfo.exists())
        {
            meta = tr("%1 · %2").arg(meta, PDFAppLocale::formatDataSize(fileInfo.size(), 1, QLocale::DataSizeTraditionalFormat));
        }
        m_fileMeta->setText(meta);
    }

    // The thumbnail of page 1 of Recent (the thumbnailer of step 2)
    PDFAppContext* context = getContext();
    if (m_thumbnail && context && context->getDatabase() && !m_document->getFilePath().isEmpty())
    {
        const PDFRecentThumbnail thumbnail = context->getRecent().getThumbnail(m_document->getFilePath());
        const QImage image = thumbnail.toImage();
        if (!image.isNull())
        {
            m_thumbnail->setPixmap(QPixmap::fromImage(image));
        }
    }
}

void PDFAppDocumentPropertiesPage::scheduleRebuild()
{
    m_rebuildTimer.start();
}

void PDFAppDocumentPropertiesPage::rebuildNow()
{
    m_rebuildTimer.stop();
    rebuildSection();
}

void PDFAppDocumentPropertiesPage::onRebuildTimeout()
{
    rebuildSection();
}

void PDFAppDocumentPropertiesPage::rebuildSection()
{
    if (!m_contentLayout || !m_content)
    {
        return;
    }

    // A row being edited, or changed metadata, is not thrown away: the section is
    // built again, when the editing ends
    if (m_sectionWidget)
    {
        const QList<PDFPropertyRow*> rows = m_sectionWidget->findChildren<PDFPropertyRow*>();
        for (const PDFPropertyRow* row : rows)
        {
            if (row->isEditing())
            {
                m_isRebuildPending = true;
                return;
            }
        }

        if (m_section == Section::Xmp && m_isMetadataModified && m_metadataEditor && m_sectionWidget->isAncestorOf(m_metadataEditor))
        {
            // The change of the document is shown, when the editing ends (Apply,
            // Discard, or the text edited back to the packet)
            m_isRebuildPending = true;
            updateMetadataState();
            return;
        }
    }
    m_isRebuildPending = false;

    const int scrollPosition = m_pendingScroll >= 0 ? m_pendingScroll : (m_scrollArea ? m_scrollArea->verticalScrollBar()->value() : 0);
    m_pendingScroll = -1;

    // The old section is deleted later (a row of it may be emitting its signal);
    // it leaves the page at once
    if (m_sectionWidget)
    {
        QWidget* oldSection = m_sectionWidget;
        m_sectionWidget = nullptr;
        m_contentLayout->removeWidget(oldSection);
        oldSection->hide();
        oldSection->setParent(nullptr);
        oldSection->deleteLater();
    }
    m_metadataEditor = nullptr;
    m_createMetadataButton = nullptr;
    m_applyMetadataButton = nullptr;
    m_discardMetadataButton = nullptr;
    m_metadataError = nullptr;

    QWidget* section = nullptr;
    switch (m_section)
    {
        case Section::General:
            section = buildGeneral(m_content);
            break;
        case Section::Security:
            section = buildSecurity(m_content);
            break;
        case Section::Fonts:
            section = buildFonts(m_content);
            break;
        case Section::Display:
            section = buildDisplay(m_content);
            break;
        case Section::Xmp:
            section = buildXmp(m_content);
            break;
    }

    m_sectionWidget = section;
    m_isSectionEditable = isEditable();
    if (section)
    {
        section->setObjectName(QStringLiteral("section-%1").arg(getSectionId()));
        m_contentLayout->addWidget(section, 1);
    }

    updateSummary();
    if (m_copyAllButton)
    {
        m_copyAllButton->setEnabled(m_document && m_document->getDocument());
    }

    if (m_scrollArea)
    {
        m_content->adjustSize();
        m_scrollArea->verticalScrollBar()->setValue(scrollPosition);
    }

    if (m_section == Section::Fonts)
    {
        startFontScan();
    }
}

QWidget* PDFAppDocumentPropertiesPage::createSectionHeader(const QString& title, const QString& subtitle, QWidget* parent)
{
    // text 236:8963: Bold 18 text/primary, Regular 12 text/tertiary, 2 px apart
    QWidget* header = new QWidget(parent);
    header->setObjectName(QStringLiteral("sectionHeader"));
    QVBoxLayout* layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    PDFLabel* titleLabel = PDFAppPageWidgets::createLabel(title, QFont::Bold, 18, PDFThemeColors::TextPrimary, header);
    titleLabel->setObjectName(QStringLiteral("sectionTitle"));
    titleLabel->setAccessibleName(title);
    PDFLabel* subtitleLabel = PDFAppPageWidgets::createLabel(subtitle, PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, header);
    subtitleLabel->setObjectName(QStringLiteral("sectionSubtitle"));
    subtitleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
    return header;
}

QWidget* PDFAppDocumentPropertiesPage::createGroup(const PDFAppPropertyGroup& group, QWidget* parent)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    // group-* 236:8970: the title Semi Bold 13, 8 px above the card (padding 4, rows of 30)
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("group-%1").arg(group.id));
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    PDFLabel* title = PDFAppPageWidgets::createLabel(group.title, PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, widget);
    title->setObjectName(QStringLiteral("groupTitle"));
    layout->addWidget(title);

    PDFAppCard* card = PDFAppPageWidgets::createCard(4, 0, widget);
    card->setObjectName(QStringLiteral("card"));
    const bool isEditable = this->isEditable();
    for (const PDFAppPropertyItem& item : group.items)
    {
        // An empty value is a dash; an editable row stays empty (the editor starts empty)
        const bool isItemEditable = !item.infoKey.isEmpty();
        const QString value = item.value.isEmpty() && !isItemEditable ? PDFAppDocumentPropertiesData::getEmptyValueText() : item.value;
        PDFPropertyRow* row = new PDFPropertyRow(item.key, value, isItemEditable && isEditable ? PDFPropertyRow::Editability::Editable : PDFPropertyRow::Editability::ReadOnly, card);
        row->setObjectName(item.objectName);
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (isItemEditable)
        {
            row->setProperty(Helper::INFO_KEY_PROPERTY, item.infoKey);
            connect(row, &PDFPropertyRow::edited, this, &PDFAppDocumentPropertiesPage::onPropertyEdited);
            connect(row, &PDFPropertyRow::editingChanged, this, &PDFAppDocumentPropertiesPage::onRowEditingChanged);
        }
        card->getLayout()->addWidget(row);
    }
    layout->addWidget(card);
    return widget;
}

QWidget* PDFAppDocumentPropertiesPage::buildGeneral(QWidget* parent)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    layout->addWidget(createSectionHeader(Helper::getSectionTitle(Section::General), tr("Information stored in the document and about the file on disk"), section));

    const PDFDocument* document = m_document ? m_document->getDocument().data() : nullptr;
    qint64 lastRead = 0;
    PDFAppContext* context = getContext();
    if (context && context->getDatabase() && m_document && !m_document->getFilePath().isEmpty())
    {
        if (const std::optional<PDFRecentFile> file = context->getRecent().getFile(m_document->getFilePath()))
        {
            lastRead = file->lastOpened;
        }
    }
    const std::vector<PDFAppPropertyGroup> groups = PDFAppDocumentPropertiesData::createGeneralGroups(document, m_document ? m_document->getFilePath() : QString(),
                                                                                                    m_document ? m_document->getTitle() : QString(), lastRead);

    // Description and Content at the left, File and Custom properties at the right
    PDFAppColumns* columns = new PDFAppColumns(section);
    columns->setObjectName(QStringLiteral("columns"));
    columns->addColumn(0, 1, Helper::COLUMN_MINIMUM_WIDTH);
    columns->addColumn(0, 1, Helper::COLUMN_MINIMUM_WIDTH);
    QVBoxLayout* left = Helper::columnLayout(columns, 0);
    QVBoxLayout* right = Helper::columnLayout(columns, 1);
    for (const PDFAppPropertyGroup& group : groups)
    {
        QVBoxLayout* column = (group.id == QStringLiteral("description") || group.id == QStringLiteral("content")) ? left : right;
        if (column)
        {
            column->addWidget(createGroup(group, columns->getColumn(column == left ? 0 : 1)));
        }
    }
    if (left && right)
    {
        left->addStretch(1);
        right->addStretch(1);
    }
    layout->addWidget(columns);
    layout->addStretch(1);
    return section;
}

QWidget* PDFAppDocumentPropertiesPage::createPermissionsGroup(QWidget* parent)
{
    // group-permissions 236:9164: rows of 30 (paddings 10, gap 10): the icon 16, the text and the state
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("group-permissions"));
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    PDFLabel* title = PDFAppPageWidgets::createLabel(tr("Permissions"), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, widget);
    title->setObjectName(QStringLiteral("groupTitle"));
    layout->addWidget(title);

    PDFAppCard* card = PDFAppPageWidgets::createCard(4, 0, widget);
    card->setObjectName(QStringLiteral("card"));
    const PDFDocument* document = m_document ? m_document->getDocument().data() : nullptr;
    for (const PDFAppPermissionItem& permission : PDFAppDocumentPropertiesData::createPermissions(document))
    {
        QWidget* row = new QWidget(card);
        row->setObjectName(QStringLiteral("permission"));
        row->setFixedHeight(30);
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 0, 10, 0);
        rowLayout->setSpacing(10);

        PDFIconBadge* icon = new PDFIconBadge(permission.isAllowed ? QStringLiteral("check-circle") : QStringLiteral("x-circle"), 16, 16, row);
        icon->setColors(std::nullopt, permission.isAllowed ? PDFThemeColors::AccentGreenIcon : PDFThemeColors::AccentRedIcon);
        PDFLabel* text = PDFAppPageWidgets::createLabel(permission.text, PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, row);
        text->setObjectName(QStringLiteral("permissionText"));
        PDFLabel* state = PDFAppPageWidgets::createLabel(permission.isAllowed ? tr("Allowed") : tr("Not allowed"), QFont::Medium, 12,
                                                         permission.isAllowed ? PDFThemeColors::TextPrimary : PDFThemeColors::TextDanger, row);
        state->setObjectName(permission.isAllowed ? QStringLiteral("allowed") : QStringLiteral("notAllowed"));
        row->setAccessibleName(tr("%1: %2").arg(permission.text, state->text()));
        rowLayout->addWidget(icon);
        rowLayout->addWidget(text, 1);
        rowLayout->addWidget(state);
        card->getLayout()->addWidget(row);
    }
    layout->addWidget(card);
    return widget;
}

QWidget* PDFAppDocumentPropertiesPage::buildSecurity(QWidget* parent)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    layout->addWidget(createSectionHeader(Helper::getSectionTitle(Section::Security), tr("Encryption of the document and what you are allowed to do with it"), section));

    const PDFDocument* document = m_document ? m_document->getDocument().data() : nullptr;
    PDFAppColumns* columns = new PDFAppColumns(section);
    columns->setObjectName(QStringLiteral("columns"));
    columns->addColumn(0, 1, Helper::COLUMN_MINIMUM_WIDTH);
    columns->addColumn(0, 1, Helper::COLUMN_MINIMUM_WIDTH);
    QVBoxLayout* left = Helper::columnLayout(columns, 0);
    QVBoxLayout* right = Helper::columnLayout(columns, 1);
    if (left && right)
    {
        left->addWidget(createGroup(PDFAppDocumentPropertiesData::createEncryptionGroup(document), columns->getColumn(0)));

        // note 236:9160: accent/blue card with the lock (restrictions of the user password)
        const QString noteText = PDFAppDocumentPropertiesData::getSecurityNote(document);
        if (!noteText.isEmpty())
        {
            PDFTintedCard* note = new PDFTintedCard(PDFAccentHue::Blue, columns->getColumn(0));
            note->setObjectName(QStringLiteral("securityNote"));
            QHBoxLayout* noteLayout = new QHBoxLayout(note);
            noteLayout->setContentsMargins(13, 11, 13, 11);
            noteLayout->setSpacing(10);
            PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("lock"), 16, 16, note);
            icon->setColors(std::nullopt, PDFThemeColors::AccentBlueIcon);
            PDFAppParagraph* text = PDFAppPageWidgets::createText(noteText, PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextSecondary, note);
            noteLayout->addWidget(icon, 0, Qt::AlignTop);
            noteLayout->addWidget(text, 1);
            left->addWidget(note);
        }

        right->addWidget(createPermissionsGroup(columns->getColumn(1)));
        left->addStretch(1);
        right->addStretch(1);
    }
    layout->addWidget(columns);
    layout->addStretch(1);
    return section;
}

QString PDFAppDocumentPropertiesPage::getFontsSubtitle() const
{
    if (!m_hasFonts)
    {
        return tr("Searching for the fonts of the document…");
    }
    if (m_fonts.empty())
    {
        return tr("The document uses no fonts");
    }
    const int count = int(m_fonts.size());
    return count == 1 ? tr("%Ln font is used in the document", nullptr, count) : tr("%Ln fonts are used in the document", nullptr, count);
}

QWidget* PDFAppDocumentPropertiesPage::buildFonts(QWidget* parent)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    layout->addWidget(createSectionHeader(Helper::getSectionTitle(Section::Fonts), getFontsSubtitle(), section));

    const bool isCurrent = m_hasFonts && m_document && m_fontsVersion == m_document->getVersion();
    if (isCurrent && !m_fonts.empty())
    {
        // card 236:9344 with the rows of PDFFontDelegate (48 px, no gap): the rows
        // 4 px inside of the border of the card (PDFListView has the margins 6 of
        // a framed view, the card is the frame here)
        PDFAppCard* card = PDFAppPageWidgets::createCard(Helper::FONT_LIST_PADDING, 0, section);
        card->setObjectName(QStringLiteral("card"));
        QListView* list = new QListView(card);
        list->setObjectName(QStringLiteral("fontList"));
        list->setFrameShape(QFrame::NoFrame);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setAccessibleName(tr("Fonts"));

        QStandardItemModel* model = new QStandardItemModel(list);
        for (const PDFAppFontInfo& font : m_fonts)
        {
            QStandardItem* item = new QStandardItem(font.name);
            item->setEditable(false);
            item->setData(font.family.isEmpty() ? QString() : tr("Font family: %1").arg(font.family), PDFComponentRoles::SubtitleRole);
            item->setData(font.type, PDFComponentRoles::TagRole);
            item->setData(font.isEmbedded, PDFComponentRoles::EmbeddedRole);
            item->setData(font.getStatusText(), PDFComponentRoles::StatusTextRole);
            model->appendRow(item);
        }
        list->setModel(model);
        list->setItemDelegate(new PDFFontDelegate(list));

        // The list is as high as its rows (the page scrolls)
        int height = 0;
        for (int i = 0; i < model->rowCount(); ++i)
        {
            height += list->sizeHintForRow(i);
        }
        list->setFixedHeight(height);
        card->getLayout()->addWidget(list);
        layout->addWidget(card);
    }
    layout->addStretch(1);
    return section;
}

QWidget* PDFAppDocumentPropertiesPage::buildDisplay(QWidget* parent)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    layout->addWidget(createSectionHeader(Helper::getSectionTitle(Section::Display), tr("How the author asks viewers to show and print the document"), section));

    const PDFDocument* document = m_document ? m_document->getDocument().data() : nullptr;
    PDFAppColumns* columns = new PDFAppColumns(section);
    columns->setObjectName(QStringLiteral("columns"));
    columns->addColumn(0, 1, Helper::COLUMN_MINIMUM_WIDTH);
    columns->addColumn(0, 1, Helper::COLUMN_MINIMUM_WIDTH);
    QVBoxLayout* left = Helper::columnLayout(columns, 0);
    QVBoxLayout* right = Helper::columnLayout(columns, 1);
    const std::vector<PDFAppPropertyGroup> groups = PDFAppDocumentPropertiesData::createDisplayGroups(document);
    if (left && right)
    {
        for (size_t i = 0; i < groups.size(); ++i)
        {
            QVBoxLayout* column = i == 0 ? left : right;
            column->addWidget(createGroup(groups[i], columns->getColumn(i == 0 ? 0 : 1)));
        }
        left->addStretch(1);
        right->addStretch(1);
    }
    layout->addWidget(columns);
    layout->addStretch(1);
    return section;
}

QWidget* PDFAppDocumentPropertiesPage::buildXmp(QWidget* parent)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    layout->addWidget(createSectionHeader(Helper::getSectionTitle(Section::Xmp), tr("Metadata packet embedded in the document, as XML"), section));

    QString errorMessage;
    m_metadataText = PDFAppDocumentPropertiesData::getMetadata(m_document ? m_document->getDocument().data() : nullptr, &m_hasMetadata, &errorMessage);
    m_isMetadataModified = false;

    // code 236:9660: the packet in a monospace font (editable, when the document can
    // change) in the box with the border of the design
    PDFAppXmlCodeBox* codeBox = new PDFAppXmlCodeBox(Helper::CODE_RADIUS, section);
    QVBoxLayout* codeLayout = new QVBoxLayout(codeBox);
    codeLayout->setContentsMargins(PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH);
    m_metadataEditor = new PDFPlainTextEdit(codeBox);
    m_metadataEditor->setObjectName(QStringLiteral("metadataEditor"));
    PDFWidgetStyleProperties::set(m_metadataEditor, PDFWidgetStyleProperties::NOTE);
    m_metadataEditor->setFont(Helper::monospaceFont());
    m_metadataEditor->setAccessibleName(tr("XMP metadata"));
    m_metadataEditor->setTabChangesFocus(true);
    m_metadataEditor->document()->setDocumentMargin(14);
    m_metadataEditor->setMinimumHeight(200);
    m_metadataEditor->setPlainText(m_metadataText);

    // The editor normalizes the line ends: its text is the unchanged state
    m_metadataText = m_metadataEditor->toPlainText();
    m_metadataEditor->setPlaceholderText(!errorMessage.isEmpty() ? errorMessage : tr("The document has no metadata packet."));
    m_metadataEditor->setReadOnly(!isEditable() || !errorMessage.isEmpty());
    new PDFAppXmlHighlighter(m_metadataEditor->document());
    connect(m_metadataEditor.data(), &QPlainTextEdit::textChanged, this, &PDFAppDocumentPropertiesPage::onMetadataTextChanged);
    codeLayout->addWidget(m_metadataEditor);
    layout->addWidget(codeBox, 1);

    m_metadataError = PDFAppPageWidgets::createLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextDanger, section);
    m_metadataError->setObjectName(QStringLiteral("metadataError"));
    m_metadataError->setWordWrap(true);
    m_metadataError->hide();
    layout->addWidget(m_metadataError);

    // footer 236:9662: Create default metadata and its hint; Discard / Apply of an edited packet
    QWidget* footer = new QWidget(section);
    footer->setObjectName(QStringLiteral("metadataFooter"));
    QHBoxLayout* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(12);
    m_createMetadataButton = PDFAppPageWidgets::createButton(tr("Create default metadata"), QStringLiteral("file-plus"), false, footer);
    m_createMetadataButton->setObjectName(QStringLiteral("createMetadataButton"));
    connect(m_createMetadataButton.data(), &QAbstractButton::clicked, this, &PDFAppDocumentPropertiesPage::onCreateDefaultMetadata);
    PDFAppParagraph* hint = PDFAppPageWidgets::createHint(tr("Use it when the document has no metadata packet."), footer);
    m_discardMetadataButton = PDFAppPageWidgets::createButton(tr("Discard"), QString(), false, footer);
    m_discardMetadataButton->setObjectName(QStringLiteral("discardMetadataButton"));
    connect(m_discardMetadataButton.data(), &QAbstractButton::clicked, this, &PDFAppDocumentPropertiesPage::onDiscardMetadata);
    m_applyMetadataButton = PDFAppPageWidgets::createButton(tr("Apply changes"), QStringLiteral("check"), true, footer);
    m_applyMetadataButton->setObjectName(QStringLiteral("applyMetadataButton"));
    connect(m_applyMetadataButton.data(), &QAbstractButton::clicked, this, &PDFAppDocumentPropertiesPage::onApplyMetadata);
    footerLayout->addWidget(m_createMetadataButton);
    footerLayout->addWidget(hint, 1, Qt::AlignVCenter);
    footerLayout->addWidget(m_discardMetadataButton);
    footerLayout->addWidget(m_applyMetadataButton);
    layout->addWidget(footer);

    updateMetadataState();
    return section;
}

void PDFAppDocumentPropertiesPage::updateMetadataState()
{
    const bool isEditable = this->isEditable();
    if (m_metadataEditor)
    {
        const bool isReadable = m_hasMetadata ? !m_metadataText.isEmpty() || m_metadataEditor->toPlainText().isEmpty() : true;
        m_metadataEditor->setReadOnly(!isEditable || !isReadable);
    }
    if (m_createMetadataButton)
    {
        m_createMetadataButton->setEnabled(isEditable && !m_hasMetadata && !m_isMetadataModified);
    }
    if (m_applyMetadataButton)
    {
        m_applyMetadataButton->setVisible(m_isMetadataModified);
        m_applyMetadataButton->setEnabled(isEditable);
    }
    if (m_discardMetadataButton)
    {
        m_discardMetadataButton->setVisible(m_isMetadataModified);
    }
}

void PDFAppDocumentPropertiesPage::onMetadataTextChanged()
{
    if (!m_metadataEditor)
    {
        return;
    }

    const bool isModified = m_metadataEditor->toPlainText() != m_metadataText;
    if (isModified != m_isMetadataModified)
    {
        m_isMetadataModified = isModified;
        updateMetadataState();
    }
    // The error goes with the next change of the text (not with a change of its colors)
    if (m_metadataError && !m_metadataError->isHidden() && m_metadataEditor->toPlainText() != m_metadataErrorText)
    {
        m_metadataError->hide();
    }

    // A change of the document, which came during the editing, is shown after it
    if (!m_isMetadataModified && m_isRebuildPending)
    {
        scheduleRebuild();
    }
}

void PDFAppDocumentPropertiesPage::onApplyMetadata()
{
    if (!m_metadataEditor || !m_document)
    {
        return;
    }

    const QByteArray xml = m_metadataEditor->toPlainText().toUtf8();
    QString errorMessage;
    if (!PDFAppDocumentPropertiesData::validateMetadata(xml, &errorMessage))
    {
        if (m_metadataError)
        {
            m_metadataErrorText = m_metadataEditor->toPlainText();
            m_metadataError->setText(errorMessage);
            m_metadataError->show();
        }
        return;
    }

    PDFDocumentPointer document = PDFAppDocumentPropertiesData::setMetadata(m_document->getDocument().data(), xml);
    if (!document)
    {
        // Nothing has changed (the same packet)
        m_metadataText = m_metadataEditor->toPlainText();
        m_isMetadataModified = false;
        updateMetadataState();
        return;
    }

    if (!commitDocument(document, tr("Change XMP metadata")))
    {
        updateMetadataState();
        return;
    }

    // The text of the editor is the packet of the document now
    m_metadataText = m_metadataEditor->toPlainText();
    m_isMetadataModified = false;
    scheduleRebuild();
}

void PDFAppDocumentPropertiesPage::onDiscardMetadata()
{
    m_isMetadataModified = false;
    if (m_metadataEditor)
    {
        m_metadataEditor->setPlainText(m_metadataText);
    }
    if (m_metadataError)
    {
        m_metadataError->hide();
    }
    updateMetadataState();
    scheduleRebuild();
}

void PDFAppDocumentPropertiesPage::onCreateDefaultMetadata()
{
    if (!m_document || !m_document->getDocument())
    {
        return;
    }

    const QByteArray xml = PDFAppDocumentPropertiesData::createDefaultMetadata(m_document->getDocument().data(), QDateTime::currentDateTime());
    PDFDocumentPointer document = PDFAppDocumentPropertiesData::setMetadata(m_document->getDocument().data(), xml);
    if (document)
    {
        commitDocument(document, tr("Create XMP metadata"));
    }
    scheduleRebuild();
}

bool PDFAppDocumentPropertiesPage::commitDocument(PDFDocumentPointer document, const QString& description)
{
    if (!m_document || !document)
    {
        return false;
    }

    if (!isEditable())
    {
        showStatusMessage(m_document->isPagedXFA() ? PDFAppDocument::getPagedXFARefusalText()
                          : m_document->isLocked() ? tr("The document is locked, it cannot be changed.") : tr("The document cannot be changed now."));
        return false;
    }

    const PDFModifiedDocument modified(document, m_document->getOptionalContentActivity(), PDFModifiedDocument::None);
    if (!m_document->commit(modified, description, m_document->getVersion(), PDFAppChangeSource::User))
    {
        showStatusMessage(tr("The document cannot be changed now."));
        return false;
    }
    return true;
}

void PDFAppDocumentPropertiesPage::onPropertyEdited(const QString& value)
{
    using Helper = PDFAppDocumentPropertiesPageHelper;

    const PDFPropertyRow* row = qobject_cast<const PDFPropertyRow*>(sender());
    if (!row || !m_document || !m_document->getDocument())
    {
        return;
    }

    const QByteArray key = row->property(Helper::INFO_KEY_PROPERTY).toByteArray();
    if (key.isEmpty())
    {
        return;
    }

    const QString text = value.trimmed().left(PDFAppDocumentPropertiesData::MAXIMUM_VALUE_LENGTH);
    PDFDocumentPointer document = PDFAppDocumentPropertiesData::setInfoValue(m_document->getDocument().data(), key, text);
    if (document)
    {
        commitDocument(document, tr("Change %1").arg(row->getKey()));
    }

    // The rows show the values of the document (also after a refused change)
    scheduleRebuild();
}

void PDFAppDocumentPropertiesPage::onRowEditingChanged(bool isEditing)
{
    if (!isEditing && m_isRebuildPending)
    {
        scheduleRebuild();
    }
}

void PDFAppDocumentPropertiesPage::onDocumentChanged()
{
    scheduleRebuild();
}

void PDFAppDocumentPropertiesPage::onDocumentStateChanged()
{
    // The rows are built again only, when the document can be changed now and
    // could not before (or vice versa): an operation reports its progress by this
    // signal too, a rebuild would take the focus and the cursor of the user
    QPointer<PDFAppDocumentPropertiesPage> guard(this);
    Q_EMIT titleChanged();
    if (!guard)
    {
        return;
    }

    if (isEditable() != m_isSectionEditable)
    {
        scheduleRebuild();
    }
    else
    {
        updateMetadataState();
    }
}

void PDFAppDocumentPropertiesPage::onThemeChanged()
{
    // The colors of the XML are tokens of the theme
    if (m_metadataEditor)
    {
        if (QSyntaxHighlighter* highlighter = m_metadataEditor->document()->findChild<QSyntaxHighlighter*>())
        {
            highlighter->rehighlight();
        }
    }
}

bool PDFAppDocumentPropertiesPage::isFontScanRunning() const
{
    return m_fontWatcher && m_fontWatcher->isRunning();
}

void PDFAppDocumentPropertiesPage::startFontScan()
{
    if (!m_document || !m_document->getDocument())
    {
        return;
    }

    const quint64 version = m_document->getVersion();
    if ((m_hasFonts && m_fontsVersion == version) || (isFontScanRunning() && m_fontScanVersion == version))
    {
        return;
    }

    // A scan of an older version is canceled, its result is dropped
    if (m_fontScanCancel)
    {
        m_fontScanCancel->store(true);
    }
    if (m_fontWatcher)
    {
        m_fontWatcher->disconnect(this);
        m_fontWatcher->deleteLater();
    }

    m_fontScanCancel = std::make_shared<std::atomic_bool>(false);
    m_fontScanVersion = version;
    m_fontWatcher = new QFutureWatcher<std::vector<PDFAppFontInfo>>(this);
    connect(m_fontWatcher.data(), &QFutureWatcherBase::finished, this, &PDFAppDocumentPropertiesPage::onFontScanFinished);
    m_fontWatcher->setFuture(QtConcurrent::run(PDFAppDocumentFontScanTask(m_document->getDocument(), m_fontScanCancel)));
}

void PDFAppDocumentPropertiesPage::onFontScanFinished()
{
    if (!m_fontWatcher || !m_document)
    {
        return;
    }

    const bool isCanceled = m_fontScanCancel && m_fontScanCancel->load();
    if (!isCanceled && m_fontScanVersion == m_document->getVersion())
    {
        m_fonts = m_fontWatcher->result();
        m_fontsVersion = m_fontScanVersion;
        m_hasFonts = true;
    }

    m_fontWatcher->deleteLater();
    m_fontWatcher = nullptr;

    QPointer<PDFAppDocumentPropertiesPage> guard(this);
    Q_EMIT fontsScanned();
    if (guard && m_section == Section::Fonts)
    {
        scheduleRebuild();
    }
}

}   // namespace pdf
