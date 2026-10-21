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

#include "pdfapphelpcatalog.h"
#include "pdfwhatsnew.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of the catalog of the manual
class PDFAppHelpCatalogHelper
{
public:
    PDFAppHelpCatalogHelper() = delete;

    static bool isTopicId(const QString& id);
    static int getCategoryIndex(const QString& id);
    static bool isBefore(const PDFAppHelpTopic& left, const PDFAppHelpTopic& right);
    static bool isBetterMatch(const PDFAppHelpMatch& left, const PDFAppHelpMatch& right);
    static bool containsAllWords(const QString& text, const QStringList& words);
    static QString readFile(const QString& path);
};

bool PDFAppHelpCatalogHelper::isTopicId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,62}$"));
    return pattern.match(id).hasMatch();
}

int PDFAppHelpCatalogHelper::getCategoryIndex(const QString& id)
{
    const std::vector<PDFAppHelpCategory>& categories = PDFAppHelpCatalog::getCategories();
    for (size_t i = 0; i < categories.size(); ++i)
    {
        if (categories[i].id == id)
        {
            return int(i);
        }
    }
    return int(categories.size());
}

bool PDFAppHelpCatalogHelper::isBefore(const PDFAppHelpTopic& left, const PDFAppHelpTopic& right)
{
    const int leftCategory = getCategoryIndex(left.categoryId);
    const int rightCategory = getCategoryIndex(right.categoryId);
    if (leftCategory != rightCategory)
    {
        return leftCategory < rightCategory;
    }
    if (left.order != right.order)
    {
        return left.order < right.order;
    }
    return left.id < right.id;
}

bool PDFAppHelpCatalogHelper::isBetterMatch(const PDFAppHelpMatch& left, const PDFAppHelpMatch& right)
{
    if (left.score != right.score)
    {
        return left.score > right.score;
    }
    return left.text < right.text;
}

bool PDFAppHelpCatalogHelper::containsAllWords(const QString& text, const QStringList& words)
{
    for (const QString& word : words)
    {
        if (!text.contains(word, Qt::CaseInsensitive))
        {
            return false;
        }
    }
    return !words.isEmpty();
}

QString PDFAppHelpCatalogHelper::readFile(const QString& path)
{
    QFile file(path);
    if (!file.exists() || file.size() > PDFAppHelpCatalog::MAXIMUM_FILE_SIZE || !file.open(QFile::ReadOnly))
    {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString PDFAppHelpCategory::getTitle() const
{
    return QCoreApplication::translate("pdf::PDFAppHelpCatalog", title);
}

const std::vector<PDFAppHelpCategory>& PDFAppHelpCatalog::getCategories()
{
    // The categories of the design (pdf4qt-help 955:152873), which phase 1 has
    static const std::vector<PDFAppHelpCategory> categories =
    {
        { QStringLiteral("getting-started"), QT_TRANSLATE_NOOP("pdf::PDFAppHelpCatalog", "Getting started") },
        { QStringLiteral("open-and-view"), QT_TRANSLATE_NOOP("pdf::PDFAppHelpCatalog", "Open and view") },
        { QStringLiteral("edit-content"), QT_TRANSLATE_NOOP("pdf::PDFAppHelpCatalog", "Edit content") },
        { QStringLiteral("ai-assistants"), QT_TRANSLATE_NOOP("pdf::PDFAppHelpCatalog", "AI assistants") },
        { QStringLiteral("settings"), QT_TRANSLATE_NOOP("pdf::PDFAppHelpCatalog", "Settings") }
    };
    return categories;
}

const PDFAppHelpCategory* PDFAppHelpCatalog::findCategory(const QString& id)
{
    for (const PDFAppHelpCategory& category : getCategories())
    {
        if (category.id == id)
        {
            return &category;
        }
    }
    return nullptr;
}

bool PDFAppHelpCatalog::parseTopic(const QString& id, const QString& text, PDFAppHelpTopic* topic, QStringList* errors)
{
    QStringList foundErrors;
    PDFAppHelpTopic result;
    result.id = id;

    if (!PDFAppHelpCatalogHelper::isTopicId(id))
    {
        foundErrors << QStringLiteral("0: invalid id of the topic \"%1\"").arg(id.left(64));
    }

    if (text.size() > MAXIMUM_FILE_SIZE)
    {
        foundErrors << QStringLiteral("0: the file is too large");
    }

    QString normalized = text.left(MAXIMUM_FILE_SIZE);
    if (normalized.startsWith(QChar(0xFEFF)))
    {
        normalized.remove(0, 1);
    }
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = normalized.split(QChar(u'\n'));

    int bodyStart = -1;
    std::set<QString> keys;
    if (lines.isEmpty() || lines.first().trimmed() != QStringLiteral("---"))
    {
        foundErrors << QStringLiteral("1: the file must start with the header \"---\"");
    }
    else
    {
        for (int i = 1; i < lines.size(); ++i)
        {
            const QString& line = lines.at(i);
            if (line.trimmed() == QStringLiteral("---"))
            {
                bodyStart = i + 1;
                break;
            }
            if (line.trimmed().isEmpty())
            {
                continue;
            }

            const qsizetype colon = line.indexOf(QChar(u':'));
            const QString key = colon > 0 ? line.left(colon).trimmed() : QString();
            const QString value = colon > 0 ? line.mid(colon + 1).trimmed() : QString();
            if (!keys.insert(key).second)
            {
                foundErrors << QStringLiteral("%1: the key \"%2\" is given twice").arg(i + 1).arg(key.left(64));
                continue;
            }

            if (key == QStringLiteral("title"))
            {
                result.title = value;
            }
            else if (key == QStringLiteral("category"))
            {
                result.categoryId = value;
                if (!findCategory(value))
                {
                    foundErrors << QStringLiteral("%1: unknown category \"%2\"").arg(i + 1).arg(value.left(64));
                }
            }
            else if (key == QStringLiteral("order"))
            {
                bool ok = false;
                result.order = value.toInt(&ok);
                if (!ok)
                {
                    foundErrors << QStringLiteral("%1: the order is not a number").arg(i + 1);
                }
            }
            else if (key == QStringLiteral("summary"))
            {
                result.summary = value;
            }
            else if (key == QStringLiteral("keywords"))
            {
                for (const QString& keyword : value.split(QChar(u','), Qt::SkipEmptyParts))
                {
                    result.keywords << keyword.trimmed().toLower();
                }
            }
            else if (key == QStringLiteral("related"))
            {
                for (const QString& route : value.split(QChar(u','), Qt::SkipEmptyParts))
                {
                    const QString trimmed = route.trimmed();
                    if (PDFAppMarkdownParser::classifyLink(trimmed) != PDFAppMarkdownLinkKind::Route)
                    {
                        foundErrors << QStringLiteral("%1: the related link \"%2\" is not a route").arg(i + 1).arg(trimmed.left(64));
                        continue;
                    }
                    result.related << trimmed;
                }
            }
            else
            {
                foundErrors << QStringLiteral("%1: unknown key \"%2\"").arg(i + 1).arg(key.left(64));
            }
        }

        if (bodyStart < 0)
        {
            foundErrors << QStringLiteral("%1: the header is not closed by \"---\"").arg(lines.size());
        }
    }

    if (result.title.isEmpty())
    {
        foundErrors << QStringLiteral("1: the topic has no title");
    }
    if (result.categoryId.isEmpty())
    {
        foundErrors << QStringLiteral("1: the topic has no category");
    }

    if (bodyStart >= 0)
    {
        result.markdown = lines.mid(bodyStart).join(QChar(u'\n')).trimmed();
        result.headings = PDFAppMarkdownParser::parse(result.markdown).getHeadings();
    }

    if (result.markdown.isEmpty())
    {
        foundErrors << QStringLiteral("%1: the topic has no text").arg(qMax(bodyStart, 1));
    }

    if (errors)
    {
        *errors = foundErrors;
    }
    if (topic)
    {
        *topic = std::move(result);
    }
    return foundErrors.isEmpty();
}

PDFAppHelpCatalog PDFAppHelpCatalog::load(const QString& directory)
{
    PDFAppHelpCatalog catalog;
    catalog.m_directory = directory;

    const QDir folder(directory);
    const QStringList files = folder.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
    for (const QString& fileName : files)
    {
        const QString base = fileName.chopped(3);
        if (base.contains(QChar(u'.')))
        {
            // Translation <topic>.<language>.md (getLocalizedTopic)
            continue;
        }

        const QString text = PDFAppHelpCatalogHelper::readFile(folder.filePath(fileName));
        PDFAppHelpTopic topic;
        QStringList errors;
        if (!parseTopic(base, text, &topic, &errors))
        {
            for (const QString& error : errors)
            {
                catalog.m_errors << QStringLiteral("%1:%2").arg(fileName, error);
            }
            continue;
        }
        catalog.m_topics.push_back(std::move(topic));
    }

    std::sort(catalog.m_topics.begin(), catalog.m_topics.end(), &PDFAppHelpCatalogHelper::isBefore);

    for (const QString& error : catalog.m_errors)
    {
        qWarning("Help file %s", qPrintable(error));
    }
    return catalog;
}

const PDFAppHelpTopic* PDFAppHelpCatalog::findTopic(const QString& id) const
{
    for (const PDFAppHelpTopic& topic : m_topics)
    {
        if (topic.id == id)
        {
            return &topic;
        }
    }
    return nullptr;
}

std::vector<const PDFAppHelpTopic*> PDFAppHelpCatalog::getCategoryTopics(const QString& categoryId) const
{
    std::vector<const PDFAppHelpTopic*> topics;
    for (const PDFAppHelpTopic& topic : m_topics)
    {
        if (topic.categoryId == categoryId)
        {
            topics.push_back(&topic);
        }
    }
    return topics;
}

const PDFAppHelpTopic* PDFAppHelpCatalog::getNextTopic(const QString& id) const
{
    const PDFAppHelpTopic* topic = findTopic(id);
    if (!topic)
    {
        return nullptr;
    }

    const std::vector<const PDFAppHelpTopic*> topics = getCategoryTopics(topic->categoryId);
    for (size_t i = 0; i + 1 < topics.size(); ++i)
    {
        if (topics[i] == topic)
        {
            return topics[i + 1];
        }
    }
    return nullptr;
}

PDFAppHelpTopic PDFAppHelpCatalog::getLocalizedTopic(const QString& id, const QString& language) const
{
    const PDFAppHelpTopic* englishTopic = findTopic(id);
    if (!englishTopic)
    {
        return PDFAppHelpTopic();
    }

    const QDir folder(m_directory);
    for (const QString& candidate : PDFWhatsNewCatalog::getLanguageFallbacks(language))
    {
        const QString fileName = QStringLiteral("%1.%2.md").arg(id, candidate);
        const QString text = PDFAppHelpCatalogHelper::readFile(folder.filePath(fileName));
        if (text.isEmpty())
        {
            continue;
        }

        PDFAppHelpTopic topic;
        QStringList errors;
        if (parseTopic(id, text, &topic, &errors))
        {
            // The order and the category belong to the English topic
            topic.categoryId = englishTopic->categoryId;
            topic.order = englishTopic->order;
            topic.language = candidate;
            return topic;
        }

        for (const QString& error : errors)
        {
            qWarning("Help file %s:%s", qPrintable(fileName), qPrintable(error));
        }
    }

    return *englishTopic;
}

QString PDFAppHelpCatalog::getRoute(const QString& id, const QString& anchor)
{
    QString route = QStringLiteral("/help/") + id;
    if (!anchor.isEmpty())
    {
        route += QChar(u'#') + anchor;
    }
    return route;
}

std::vector<PDFAppHelpMatch> PDFAppHelpCatalog::search(const QString& text) const
{
    std::vector<PDFAppHelpMatch> matches;
    const QStringList words = text.simplified().left(256).split(QChar(u' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
    {
        return matches;
    }

    for (const PDFAppHelpTopic& topic : m_topics)
    {
        PDFAppHelpMatch match;
        match.topic = &topic;
        match.text = topic.title;

        if (PDFAppHelpCatalogHelper::containsAllWords(topic.title, words))
        {
            match.score = 100;
        }
        else if (PDFAppHelpCatalogHelper::containsAllWords(topic.keywords.join(QChar(u' ')), words))
        {
            match.score = 60;
        }
        else if (PDFAppHelpCatalogHelper::containsAllWords(topic.summary, words))
        {
            match.score = 40;
        }
        else if (PDFAppHelpCatalogHelper::containsAllWords(topic.markdown, words))
        {
            match.score = 10;
        }

        if (match.score > 0)
        {
            matches.push_back(match);
        }

        // Headings are results of their own (they lead to the anchor)
        for (const PDFAppMarkdownHeading& heading : topic.headings)
        {
            if (PDFAppHelpCatalogHelper::containsAllWords(heading.text, words))
            {
                PDFAppHelpMatch headingMatch;
                headingMatch.topic = &topic;
                headingMatch.anchor = heading.anchor;
                headingMatch.text = heading.text;
                headingMatch.score = 80;
                matches.push_back(headingMatch);
            }
        }
    }

    std::stable_sort(matches.begin(), matches.end(), &PDFAppHelpCatalogHelper::isBetterMatch);
    return matches;
}

}   // namespace pdf
