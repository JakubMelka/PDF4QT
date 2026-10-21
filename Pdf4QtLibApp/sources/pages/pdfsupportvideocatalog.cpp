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

#include "pdfsupportvideocatalog.h"

#include <QCoreApplication>

namespace pdf
{

/// Helpers of the catalog of the videos
class PDFSupportVideoCatalogHelper
{
public:
    PDFSupportVideoCatalogHelper() = delete;

    using Topic = PDFSupportVideoTopic;

    /// Address of every video of the mockup: the channel (no single videos yet)
    static constexpr const char* CHANNEL = "https://www.youtube.com/@czechwisecoder";

    static QString translate(const char* text)
    {
        return text ? QCoreApplication::translate(PDFSupportVideoCatalog::TRANSLATION_CONTEXT, text) : QString();
    }

    template<size_t Size>
    static QStringList toList(const std::array<const char*, Size>& texts, bool isTranslated)
    {
        QStringList list;
        for (const char* text : texts)
        {
            if (text && *text)
            {
                list << (isTranslated ? translate(text) : QString::fromUtf8(text));
            }
        }
        return list;
    }

    struct TopicInfo
    {
        Topic topic;
        const char* id;
        const char* title;
    };

    static constexpr TopicInfo TOPICS[] = {
        { Topic::GettingStarted, "getting-started", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Getting started") },
        { Topic::Pages, "pages", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Pages") },
        { Topic::Convert, "convert", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Convert & OCR") },
        { Topic::Forms, "forms", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Forms") },
        { Topic::Security, "security", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Security") },
        { Topic::Optimize, "optimize", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Optimize") },
        { Topic::Review, "review", QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Review") }
    };

    static const TopicInfo* findTopicInfo(Topic topic)
    {
        for (const TopicInfo& info : TOPICS)
        {
            if (info.topic == topic)
            {
                return &info;
            }
        }
        return nullptr;
    }
};

QString PDFSupportVideo::getId() const
{
    return QString::fromLatin1(m_id);
}

QString PDFSupportVideo::getTitle() const
{
    return PDFSupportVideoCatalogHelper::translate(m_title);
}

QString PDFSupportVideo::getSourceTitle() const
{
    return QString::fromUtf8(m_title);
}

QString PDFSupportVideo::getDescription() const
{
    return PDFSupportVideoCatalogHelper::translate(m_description);
}

QString PDFSupportVideo::getSourceDescription() const
{
    return QString::fromUtf8(m_description);
}

QString PDFSupportVideo::getDuration() const
{
    return QString::fromLatin1(m_duration);
}

QUrl PDFSupportVideo::getLink() const
{
    return QUrl(QString::fromLatin1(m_link), QUrl::StrictMode);
}

QStringList PDFSupportVideo::getTags() const
{
    return PDFSupportVideoCatalogHelper::toList(m_tags, true);
}

QStringList PDFSupportVideo::getSourceTags() const
{
    return PDFSupportVideoCatalogHelper::toList(m_tags, false);
}

QStringList PDFSupportVideo::getToolIds() const
{
    return PDFSupportVideoCatalogHelper::toList(m_toolIds, false);
}

QString PDFSupportVideo::getThumbnailPath() const
{
    return m_thumbnail ? QString::fromLatin1(m_thumbnail) : QString();
}

const std::vector<PDFSupportVideo>& PDFSupportVideoCatalog::getVideos()
{
    using Helper = PDFSupportVideoCatalogHelper;
    using Topic = PDFSupportVideoTopic;

    // Mockup of Figma 116:2419 (texts, durations and tags of the cards). The
    // pictures of the videos are placeholders in the design, so no picture is
    // bundled yet (an empty thumbnail = the placeholder of the card).
    static const std::vector<PDFSupportVideo> videos = {
        PDFSupportVideo("getting-started",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Getting started with PDF4QT"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "A quick tour of the interface and the main features."),
                        "2:45", Helper::CHANNEL, Topic::GettingStarted,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Getting started"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Basics"), nullptr },
                        { nullptr, nullptr, nullptr, nullptr }, ""),
        PDFSupportVideo("tabs-and-workspaces",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Tabs and workspaces"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Work with several documents and save your session."),
                        "3:10", Helper::CHANNEL, Topic::GettingStarted,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Getting started"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Workspaces"), nullptr },
                        { nullptr, nullptr, nullptr, nullptr }, ""),
        PDFSupportVideo("merge-and-organize-pages",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Merge and organize pages"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Combine, split and reorder pages."),
                        "3:18", Helper::CHANNEL, Topic::Pages,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Pages"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Basics"), nullptr },
                        { "pages.merge", "pages.split", "pages.manage", nullptr }, ""),
        PDFSupportVideo("rotate-insert-delete-pages",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Rotate, insert and delete pages"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Fix page order and orientation in seconds."),
                        "2:36", Helper::CHANNEL, Topic::Pages,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Pages"), nullptr, nullptr },
                        { "pages.rotate", "pages.insert", "pages.remove", nullptr }, ""),
        PDFSupportVideo("ocr-made-easy",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "OCR made easy"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Turn scanned documents into searchable PDFs."),
                        "4:32", Helper::CHANNEL, Topic::Convert,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "OCR"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Convert"), nullptr },
                        { "convert.ocr", "convert.scan", nullptr, nullptr }, ""),
        PDFSupportVideo("export-text-and-images",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Export text and images"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Get text and pictures out of a PDF."),
                        "2:52", Helper::CHANNEL, Topic::Convert,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Convert"), nullptr, nullptr },
                        { "convert.extract-text", "convert.extract-images", "convert.pdf-to-images", nullptr }, ""),
        PDFSupportVideo("fill-and-sign-forms",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Fill and sign forms"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Complete forms and add digital signatures."),
                        "3:52", Helper::CHANNEL, Topic::Forms,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Forms"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Sign"), nullptr },
                        { "protect.sign", "protect.sign-by-hand", nullptr, nullptr }, ""),
        PDFSupportVideo("protect-a-document",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Protect a document"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Passwords, permissions and redaction."),
                        "3:05", Helper::CHANNEL, Topic::Security,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Security"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Redact"), nullptr },
                        { "protect.password", "protect.permissions", "protect.redact", nullptr }, ""),
        PDFSupportVideo("verify-signatures-and-timestamps",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Verify signatures and timestamps"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Check who signed a document and when."),
                        "3:40", Helper::CHANNEL, Topic::Security,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Security"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Sign"), nullptr },
                        { "protect.certificates", "protect.timestamp", nullptr, nullptr }, ""),
        PDFSupportVideo("compress-and-sanitize",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Compress and sanitize"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Smaller files without hidden data."),
                        "2:21", Helper::CHANNEL, Topic::Optimize,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Optimize"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Sanitize"), nullptr },
                        { "optimize.compress", "protect.sanitize", nullptr, nullptr }, ""),
        PDFSupportVideo("compare-two-documents",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Compare two documents"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Find text and visual differences between versions."),
                        "2:58", Helper::CHANNEL, Topic::Review,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Review"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Compare"), nullptr },
                        { "review.compare", nullptr, nullptr, nullptr }, ""),
        PDFSupportVideo("measure-on-drawings",
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Measure on drawings"),
                        QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Calibrate a scale and measure distances and areas."),
                        "4:10", Helper::CHANNEL, Topic::Review,
                        { QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Review"), QT_TRANSLATE_NOOP("pdf::PDFSupportVideoCatalog", "Measure"), nullptr },
                        { "review.measure", nullptr, nullptr, nullptr }, "")
    };
    return videos;
}

const PDFSupportVideo* PDFSupportVideoCatalog::findVideo(QStringView id)
{
    if (id.isEmpty() || id.size() > 128)
    {
        return nullptr;
    }

    for (const PDFSupportVideo& video : getVideos())
    {
        if (video.getId() == id)
        {
            return &video;
        }
    }
    return nullptr;
}

QUrl PDFSupportVideoCatalog::getChannelUrl()
{
    return QUrl(QString::fromLatin1(PDFSupportVideoCatalogHelper::CHANNEL), QUrl::StrictMode);
}

bool PDFSupportVideoCatalog::isAllowedLink(const QUrl& url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") && url.host() == QLatin1String("www.youtube.com") && url.userInfo().isEmpty() && url.port() == -1;
}

std::vector<PDFSupportVideoTopic> PDFSupportVideoCatalog::getTopics()
{
    std::vector<PDFSupportVideoTopic> topics;
    for (const PDFSupportVideoCatalogHelper::TopicInfo& info : PDFSupportVideoCatalogHelper::TOPICS)
    {
        topics.push_back(info.topic);
    }
    return topics;
}

QString PDFSupportVideoCatalog::getTopicId(PDFSupportVideoTopic topic)
{
    const PDFSupportVideoCatalogHelper::TopicInfo* info = PDFSupportVideoCatalogHelper::findTopicInfo(topic);
    return info ? QString::fromLatin1(info->id) : QString();
}

std::optional<PDFSupportVideoTopic> PDFSupportVideoCatalog::findTopic(QStringView id)
{
    for (const PDFSupportVideoCatalogHelper::TopicInfo& info : PDFSupportVideoCatalogHelper::TOPICS)
    {
        if (id == QLatin1String(info.id))
        {
            return info.topic;
        }
    }
    return std::nullopt;
}

QString PDFSupportVideoCatalog::getTopicTitle(PDFSupportVideoTopic topic)
{
    const PDFSupportVideoCatalogHelper::TopicInfo* info = PDFSupportVideoCatalogHelper::findTopicInfo(topic);
    return info ? PDFSupportVideoCatalogHelper::translate(info->title) : QString();
}

QStringList PDFSupportVideoCatalog::getRouteTopicIds()
{
    QStringList ids;
    ids << QString::fromLatin1(FOR_YOU_TOPIC);
    for (const PDFSupportVideoCatalogHelper::TopicInfo& info : PDFSupportVideoCatalogHelper::TOPICS)
    {
        ids << QString::fromLatin1(info.id);
    }
    return ids;
}

int PDFSupportVideoCatalog::getVideoCount(PDFSupportVideoTopic topic)
{
    int count = 0;
    for (const PDFSupportVideo& video : getVideos())
    {
        if (video.getTopic() == topic)
        {
            ++count;
        }
    }
    return count;
}

bool PDFSupportVideoCatalog::matchesSearch(const PDFSupportVideo& video, const QString& text)
{
    const QStringList words = text.left(MAXIMUM_SEARCH_LENGTH).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
    {
        return true;
    }

    QStringList haystack;
    haystack << video.getTitle() << video.getSourceTitle() << video.getDescription() << video.getSourceDescription();
    haystack << video.getTags() << video.getSourceTags();
    const QString joined = haystack.join(QLatin1Char('\n'));

    for (const QString& word : words)
    {
        if (!joined.contains(word, Qt::CaseInsensitive))
        {
            return false;
        }
    }
    return true;
}

bool PDFSupportVideoCatalog::isForYou(const PDFSupportVideo& video, const QSet<QString>& usedToolIds)
{
    for (const QString& toolId : video.getToolIds())
    {
        if (usedToolIds.contains(toolId))
        {
            return true;
        }
    }
    return false;
}

}   // namespace pdf
