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

#include "pdfapphelppage.h"
#include "pdfappcontext.h"
#include "pdfapplocale.h"
#include "pdfapproute.h"
#include "pdfapproutetable.h"
#include "pdfappworkspace.h"
#include "pdfwhatsnew.h"

#include "pdfthemefonts.h"

#include <QMouseEvent>
#include <QEvent>
#include <QStyle>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page of the manual
class PDFAppHelpPageHelper
{
public:
    PDFAppHelpPageHelper() = delete;

    static QString getIconName(const PDFAppRoute& route);
    static QString getRouteTitle(const PDFAppRoute& route);
};

QString PDFAppHelpPageHelper::getIconName(const PDFAppRoute& route)
{
    const QString first = route.getSegment(0);
    if (first == QStringLiteral("help"))
    {
        return QStringLiteral("book-open");
    }
    if (first == QStringLiteral("settings"))
    {
        return QStringLiteral("settings");
    }
    if (first == QStringLiteral("whats-new"))
    {
        return QStringLiteral("gift");
    }
    if (first == QStringLiteral("ai"))
    {
        return QStringLiteral("ai");
    }
    if (first == QStringLiteral("open"))
    {
        return QStringLiteral("folder-open");
    }
    if (first == QStringLiteral("recent"))
    {
        return QStringLiteral("clock");
    }
    if (first == QStringLiteral("tools"))
    {
        return QStringLiteral("tools");
    }
    return QStringLiteral("link");
}

QString PDFAppHelpPageHelper::getRouteTitle(const PDFAppRoute& route)
{
    const PDFAppRouteTable table = PDFAppRouteTable::createDefault();
    const PDFAppRouteMatch match = table.match(route);
    if (match.entry)
    {
        return match.entry->getTitle();
    }
    return route.getPath();
}

PDFAppHelpContentsView::PDFAppHelpContentsView(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(this, &PDFAppHelpContentsView::expanded, this, &PDFAppHelpContentsView::onExpandedChanged);
    connect(this, &PDFAppHelpContentsView::collapsed, this, &PDFAppHelpContentsView::onExpandedChanged);
}

int PDFAppHelpContentsView::getContentHeight() const
{
    QAbstractItemModel* itemModel = model();
    int rows = 0;
    if (itemModel)
    {
        for (int row = 0; row < itemModel->rowCount(); ++row)
        {
            const QModelIndex index = itemModel->index(row, 0);
            ++rows;
            if (isExpanded(index))
            {
                rows += itemModel->rowCount(index);
            }
        }
    }

    const int rowHeight = rows > 0 ? qMax(sizeHintForRow(0), 1) : 0;
    const QMargins margins = viewportMargins();
    return rows * rowHeight + margins.top() + margins.bottom() + 2 * frameWidth();
}

QSize PDFAppHelpContentsView::sizeHint() const
{
    return QSize(BaseClass::sizeHint().width(), getContentHeight());
}

QSize PDFAppHelpContentsView::minimumSizeHint() const
{
    return QSize(BaseClass::minimumSizeHint().width(), getContentHeight());
}

void PDFAppHelpContentsView::mousePressEvent(QMouseEvent* event)
{
    m_lastPressPosition = event->position().toPoint();
    BaseClass::mousePressEvent(event);
}

void PDFAppHelpContentsView::onExpandedChanged()
{
    updateGeometry();
}

PDFAppHelpPage::PDFAppHelpPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent),
    m_catalog(PDFAppHelpCatalog::getBuiltin())
{
    setObjectName(QStringLiteral("helpPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

PDFAppHelpPage::~PDFAppHelpPage() = default;

void PDFAppHelpPage::setCatalog(const PDFAppHelpCatalog& catalog)
{
    m_catalog = std::make_shared<const PDFAppHelpCatalog>(catalog);
    if (getRoute().isValid())
    {
        setRoute(getRoute());
    }
}

bool PDFAppHelpPage::setRoute(const PDFAppRoute& route)
{
    const QString topicId = route.getSegment(1);
    if (!m_catalog->findTopic(topicId))
    {
        return false;
    }

    BaseClass::setRoute(route);

    // The topic in the language of the user interface (step 5), English without a translation
    m_topic = m_catalog->getLocalizedTopic(topicId, PDFAppLocale::getUiLanguage());
    build();

    QPointer<PDFAppHelpPage> guard(this);
    Q_EMIT titleChanged();
    if (guard && !route.getFragment().isEmpty())
    {
        requestDeferredScroll(-1, route.getFragment());
    }
    return !guard.isNull();
}

QString PDFAppHelpPage::getTitle() const
{
    return m_topic.title;
}

QString PDFAppHelpPage::getHeaderTitle() const
{
    return m_topic.title;
}

QList<PDFAppBreadcrumbItem> PDFAppHelpPage::getHeaderBreadcrumbs() const
{
    // Help › Category › Topic (Help = the first topic, the category = its first topic)
    QList<PDFAppBreadcrumbItem> items;
    const std::vector<PDFAppHelpTopic>& topics = m_catalog->getTopics();
    if (topics.empty() || m_topic.id.isEmpty())
    {
        return items;
    }

    items.append({ tr("Help"), PDFAppRoutes::help(topics.front().id) });
    const PDFAppHelpCategory* category = PDFAppHelpCatalog::findCategory(m_topic.categoryId);
    const std::vector<const PDFAppHelpTopic*> categoryTopics = m_catalog->getCategoryTopics(m_topic.categoryId);
    if (category && !categoryTopics.empty())
    {
        items.append({ category->getTitle(), PDFAppRoutes::help(categoryTopics.front()->id) });
    }
    items.append({ m_topic.title, PDFAppRoute() });
    return items;
}

QString PDFAppHelpPage::getHeaderSubtitle() const
{
    const PDFAppHelpCategory* category = PDFAppHelpCatalog::findCategory(m_topic.categoryId);
    const QString version = PDFWhatsNewRelease::getDisplayVersion(PDFWhatsNewCatalog::getRunningVersion());
    return tr("User manual · %1 · for PDF4QT %2").arg(category ? category->getTitle() : QString(), version);
}

void PDFAppHelpPage::build()
{
    if (m_content)
    {
        delete m_content.data();
    }

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("helpContent"));
    layout()->addWidget(m_content);

    // The help-grid: the topic (at most 802 px) and the side column of 340 px
    QWidget* grid = m_content;

    m_markdownView = new PDFAppMarkdownView(grid);
    m_markdownView->setObjectName(QStringLiteral("helpTopic"));
    m_markdownView->setResourceBase(PDFAppHelpCatalog::getResourceDirectory() + QStringLiteral("/"));
    m_markdownView->setMarkdown(m_topic.markdown);
    m_markdownView->setAccessibleName(m_topic.title);
    connect(m_markdownView, &PDFAppMarkdownView::linkActivated, this, &PDFAppHelpPage::onMarkdownLinkActivated);

    QWidget* sideColumn = new QWidget(grid);
    QVBoxLayout* sideLayout = new QVBoxLayout(sideColumn);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(24);
    sideLayout->addWidget(createContents(sideColumn));
    sideLayout->addWidget(createRelated(sideColumn));

    PDFAppColumns::createInContent(m_content, m_markdownView, sideColumn);
    m_content->show();
}

QWidget* PDFAppHelpPage::createContents(QWidget* parent)
{
    // section-Contents: User manual, a card (padding 8) with the tree of the
    // categories (collapsed, the category of the topic expanded) and their topics
    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new PDFAppSectionHeader(tr("User manual"), section));

    // The rows lie 9 px inside of the card (its border 1 + padding 8); the tree adds
    // the margins of 6 px around its viewport itself
    PDFAppCard* card = new PDFAppCard(QMargins(3, 3, 3, 3), 0, section);
    layout->addWidget(card);

    // The model belongs to the section: a rebuild (language, catalog) deletes it with the view
    m_contentsModel = new QStandardItemModel(section);
    QModelIndex currentIndex;
    QModelIndex currentCategory;
    for (const PDFAppHelpCategory& category : PDFAppHelpCatalog::getCategories())
    {
        const std::vector<const PDFAppHelpTopic*> topics = m_catalog->getCategoryTopics(category.id);
        if (topics.empty())
        {
            continue;
        }

        QStandardItem* categoryItem = new QStandardItem(category.getTitle());
        categoryItem->setEditable(false);
        categoryItem->setSelectable(false);
        for (const PDFAppHelpTopic* topic : topics)
        {
            QStandardItem* topicItem = new QStandardItem(topic->id == m_topic.id ? m_topic.title : topic->title);
            topicItem->setEditable(false);
            topicItem->setData(topic->id, TopicRole);
            topicItem->setToolTip(topic->summary);
            categoryItem->appendRow(topicItem);
        }
        m_contentsModel->appendRow(categoryItem);

        if (category.id == m_topic.categoryId)
        {
            // The category of the topic: Semi Bold (toc-Protect & Sign of the design)
            categoryItem->setFont(PDFThemeFonts::getFont(QFont::DemiBold, 13));
            currentCategory = categoryItem->index();
            for (int row = 0; row < categoryItem->rowCount(); ++row)
            {
                if (categoryItem->child(row)->data(TopicRole).toString() == m_topic.id)
                {
                    currentIndex = categoryItem->child(row)->index();
                }
            }
        }
    }

    m_contentsView = new PDFAppHelpContentsView(card);
    m_contentsView->setObjectName(QStringLiteral("helpContents"));
    // Rows 28, chevron 14 at 8, indentation 24 (help-contents 955:152873)
    m_contentsView->setRowStyle(PDFTreeView::RowStyle::Contents);
    m_contentsView->setHeaderHidden(true);
    m_contentsView->setFrameShape(QFrame::NoFrame);
    m_contentsView->setRootIsDecorated(true);
    m_contentsView->setUniformRowHeights(true);
    m_contentsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_contentsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_contentsView->setModel(m_contentsModel);
    m_contentsView->setAccessibleName(tr("User manual"));
    card->getLayout()->addWidget(m_contentsView);

    if (currentCategory.isValid())
    {
        m_contentsView->expand(currentCategory);
    }
    if (currentIndex.isValid())
    {
        m_contentsView->setCurrentIndex(currentIndex);
    }

    m_contentsView->setExpandsOnDoubleClick(false);
    connect(m_contentsView, &PDFTreeView::clicked, this, &PDFAppHelpPage::onContentsClicked);
    connect(m_contentsView, &PDFTreeView::activated, this, &PDFAppHelpPage::onContentsActivated);
    return section;
}

QWidget* PDFAppHelpPage::createRelated(QWidget* parent)
{
    // section-Related: Link rows of the routes of the topic and the next topic
    PDFAppCard* card = new PDFAppCard(QMargins(8, 8, 8, 8), 2, parent);

    QStringList targets = m_topic.related;
    if (const PDFAppHelpTopic* next = m_catalog->getNextTopic(m_topic.id))
    {
        const QString route = PDFAppHelpCatalog::getRoute(next->id);
        targets.removeAll(route);
        targets.append(route);
    }

    const PDFAppHelpTopic* next = m_catalog->getNextTopic(m_topic.id);
    const PDFAppHelpCategory* category = PDFAppHelpCatalog::findCategory(m_topic.categoryId);
    for (const QString& target : targets)
    {
        const PDFAppRoute route = PDFAppRoute::parse(target);
        if (!route.isValid())
        {
            continue;
        }

        QString title;
        QString subtitle;
        if (route.getSegment(0) == QStringLiteral("help") && route.getSegments().size() == 2)
        {
            const PDFAppHelpTopic* topic = m_catalog->findTopic(route.getSegment(1));
            if (!topic)
            {
                continue;
            }
            title = topic->title;
            subtitle = (next && topic == next && category) ? tr("Next topic of %1").arg(category->getTitle()) : tr("Topic of the user manual");
        }
        else
        {
            title = PDFAppHelpPageHelper::getRouteTitle(route);
            subtitle = tr("Open the page");
        }

        PDFLinkRow* row = new PDFLinkRow(title, subtitle, PDFAppHelpPageHelper::getIconName(route), card);
        row->setProperty("pdf4qtLink", target);
        card->getLayout()->addWidget(row);
        connect(row, &PDFLinkRow::clicked, this, &PDFAppHelpPage::onRelatedClicked);
    }

    QWidget* section = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(new PDFAppSectionHeader(tr("Related"), section));
    layout->addWidget(card);
    section->setVisible(card->getLayout()->count() > 0);
    m_relatedSection = section;
    return section;
}

QList<PDFLinkRow*> PDFAppHelpPage::getRelatedRows() const
{
    return m_relatedSection ? m_relatedSection->findChildren<PDFLinkRow*>() : QList<PDFLinkRow*>();
}

QString PDFAppHelpPage::getRelatedTarget(const PDFLinkRow* row)
{
    return row ? row->property("pdf4qtLink").toString() : QString();
}

bool PDFAppHelpPage::scrollToAnchor(const QString& anchor)
{
    if (!m_markdownView)
    {
        return false;
    }

    const int position = m_markdownView->getAnchorPosition(anchor);
    return position >= 0 && scrollToWidget(m_markdownView, position);
}

void PDFAppHelpPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && !m_topic.id.isEmpty())
    {
        // The translation of the topic of the new language (the shared catalog is not parsed again)
        if (m_catalog->findTopic(m_topic.id))
        {
            m_topic = m_catalog->getLocalizedTopic(m_topic.id, PDFAppLocale::getUiLanguage());
        }
        build();
    }
}

void PDFAppHelpPage::onMarkdownLinkActivated(const QString& target, PDFAppMarkdownLinkKind kind)
{
    Q_UNUSED(kind);
    openLink(target);
}

void PDFAppHelpPage::onContentsClicked(const QModelIndex& index)
{
    // A style, which activates items by a single click, emits activated as well
    if (!m_contentsView || m_contentsView->style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, m_contentsView))
    {
        return;
    }

    // A click on the branch indicator is handled by the tree itself
    const QPoint position = m_contentsView->getLastPressPosition();
    if (index.data(TopicRole).toString().isEmpty() && !m_contentsView->visualRect(index).contains(position))
    {
        return;
    }

    onContentsActivated(index);
}

void PDFAppHelpPage::onContentsActivated(const QModelIndex& index)
{
    if (m_isNavigating || !m_contentsView || !index.isValid() || index.model() != m_contentsModel.data())
    {
        return;
    }

    const QString topicId = index.data(TopicRole).toString();
    if (topicId.isEmpty())
    {
        // A category opens or closes
        m_contentsView->setExpanded(index, !m_contentsView->isExpanded(index));
        return;
    }

    if (topicId != m_topic.id)
    {
        m_isNavigating = true;
        requestNavigation(PDFAppRoutes::help(topicId));
    }
}

void PDFAppHelpPage::onRelatedClicked()
{
    const QString target = getRelatedTarget(qobject_cast<const PDFLinkRow*>(sender()));
    if (!target.isEmpty())
    {
        openLink(target);
    }
}

PDFAppPage* PDFAppHelpPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppHelpPage(context, parent);
}

}   // namespace pdf
