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

#ifndef PDFSUPPORTVIDEOCATALOG_H
#define PDFSUPPORTVIDEOCATALOG_H

#include "pdfappglobal.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUrl>

#include <array>
#include <optional>
#include <vector>

namespace pdf
{

/// Topic of a video tutorial (filter chip of the Support page 117:2513). The
/// identifier of a topic is the value of the route /support?topic=.
enum class PDFSupportVideoTopic
{
    GettingStarted,
    Pages,
    Convert,
    Forms,
    Security,
    Optimize,
    Review
};

/// Video tutorial of the static catalog (newapplication.md, note Support of
/// step 2). The texts are American English in the table and translated by the
/// getters (context "pdf::PDFSupportVideoCatalog").
class PDF4QTLIBAPPSHARED_EXPORT PDFSupportVideo
{
public:
    static constexpr size_t MAXIMUM_TAGS = 3;
    static constexpr size_t MAXIMUM_TOOLS = 4;

    constexpr PDFSupportVideo(const char* id,
                              const char* title,
                              const char* description,
                              const char* duration,
                              const char* link,
                              PDFSupportVideoTopic topic,
                              std::array<const char*, MAXIMUM_TAGS> tags,
                              std::array<const char*, MAXIMUM_TOOLS> toolIds,
                              const char* thumbnail) :
        m_id(id),
        m_title(title),
        m_description(description),
        m_duration(duration),
        m_link(link),
        m_topic(topic),
        m_tags(tags),
        m_toolIds(toolIds),
        m_thumbnail(thumbnail)
    {

    }

    /// Stable identifier ("getting-started"); the introduction page of a tool
    /// (phase 2) refers to its tutorial by it
    QString getId() const;

    /// Translated title and description, and the English texts (the search
    /// matches both)
    QString getTitle() const;
    QString getSourceTitle() const;
    QString getDescription() const;
    QString getSourceDescription() const;

    /// Duration ("2:45")
    QString getDuration() const;

    /// Address of the video (https://www.youtube.com/... only)
    QUrl getLink() const;

    PDFSupportVideoTopic getTopic() const { return m_topic; }

    /// Translated tags (the order of the card) and the English ones
    QStringList getTags() const;
    QStringList getSourceTags() const;

    /// Tools of the catalog (PDFDocumentToolCatalog), which the video shows
    QStringList getToolIds() const;

    /// Resource path of the bundled picture of the video (empty = the
    /// placeholder of the card, as the design shows it)
    QString getThumbnailPath() const;

private:
    const char* m_id;
    const char* m_title;
    const char* m_description;
    const char* m_duration;
    const char* m_link;
    PDFSupportVideoTopic m_topic;
    std::array<const char*, MAXIMUM_TAGS> m_tags;
    std::array<const char*, MAXIMUM_TOOLS> m_toolIds;
    const char* m_thumbnail;
};

/// Static catalog of the video tutorials of the Support page. In phase 1 it is a
/// mockup after Figma 116:2419 (owner decision 20. 10. 2026): twelve sample
/// videos, each linking the YouTube channel. Real videos are added to the same
/// table, the code of the page does not change. Every function is thread safe.
class PDF4QTLIBAPPSHARED_EXPORT PDFSupportVideoCatalog
{
public:
    PDFSupportVideoCatalog() = delete;

    /// Translation context of the texts of the catalog
    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFSupportVideoCatalog";

    /// Value of the route /support?topic= of the filter For you
    static constexpr const char* FOR_YOU_TOPIC = "for-you";

    /// Longest search text, which is matched (longer texts are cut)
    static constexpr int MAXIMUM_SEARCH_LENGTH = 256;

    /// All videos in the order of the page
    static const std::vector<PDFSupportVideo>& getVideos();

    /// Video by its identifier, nullptr for an unknown one
    static const PDFSupportVideo* findVideo(QStringView id);

    /// Address of the channel (button YouTube channel)
    static QUrl getChannelUrl();

    /// The address may be a link of a video or of the channel: https on
    /// www.youtube.com (no user information, no port)
    static bool isAllowedLink(const QUrl& url);

    /// Topics in the order of the chips
    static std::vector<PDFSupportVideoTopic> getTopics();

    /// Identifier of the topic ("getting-started"), empty for an invalid value
    static QString getTopicId(PDFSupportVideoTopic topic);

    /// Topic by its identifier, std::nullopt for an unknown one
    static std::optional<PDFSupportVideoTopic> findTopic(QStringView id);

    /// Translated label of the chip of the topic ("Getting started")
    static QString getTopicTitle(PDFSupportVideoTopic topic);

    /// Identifiers of all topics and of For you (accepted values of the route)
    static QStringList getRouteTopicIds();

    /// Number of the videos of the topic
    static int getVideoCount(PDFSupportVideoTopic topic);

    /// The video matches the search text: every word of the text is a part of the
    /// title, the description or a tag (translated or English), ignoring the case.
    /// An empty text matches every video.
    static bool matchesSearch(const PDFSupportVideo& video, const QString& text);

    /// The video is For you: it shows a tool, which the user has used
    static bool isForYou(const PDFSupportVideo& video, const QSet<QString>& usedToolIds);
};

}   // namespace pdf

#endif // PDFSUPPORTVIDEOCATALOG_H
