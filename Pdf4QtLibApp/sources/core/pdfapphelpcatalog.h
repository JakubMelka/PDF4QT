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

#ifndef PDFAPPHELPCATALOG_H
#define PDFAPPHELPCATALOG_H

#include "pdfappglobal.h"
#include "pdfappmarkdown.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace pdf
{

/// Category of the user manual (groups of the contents of /help)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpCategory
{
public:
    QString id;

    /// Source text of the title (context "pdf::PDFAppHelpCatalog")
    const char* title = "";

    /// Translated title
    QString getTitle() const;
};

/// Topic of the user manual: one file :/help/<id>.md with a header
///
/// \code
/// ---
/// title: Portable mode
/// category: settings
/// order: 20
/// summary: Run PDF4QT without writing anything to the computer.
/// keywords: usb, stick, no installation
/// related: /help/settings, /settings/general
/// ---
/// Markdown of the topic ...
/// \endcode
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpTopic
{
public:
    /// Id of the topic (the name of the file, the route /help/{id})
    QString id;

    QString title;
    QString categoryId;
    int order = 0;

    /// One sentence about the topic (subtitles of the search results and of links)
    QString summary;

    /// Extra words of the search (lowercase)
    QStringList keywords;

    /// Routes of the card Related ("/help/settings", "/settings/general")
    QStringList related;

    /// Headings of the topic with their anchors (/help/{id}#anchor)
    std::vector<PDFAppMarkdownHeading> headings;

    /// Markdown of the topic (without the header)
    QString markdown;

    /// Language of the file (empty = English)
    QString language;
};

/// Result of the search in the manual
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpMatch
{
public:
    /// The topic (pointer into the catalog)
    const PDFAppHelpTopic* topic = nullptr;

    /// Anchor of the matched heading (empty = the topic itself)
    QString anchor;

    /// Text of the match (the title of the topic or the matched heading)
    QString text;

    /// Higher is better (title > heading > keyword > summary > text)
    int score = 0;
};

/// The user manual /help (Markdown files :/help/<topic>.md, translations
/// <topic>.<language>.md with the fallbacks pt_BR -> pt -> English as the What's
/// New files). The resources do not change while the application runs, so the
/// catalog can be kept (the global search keeps one). Topics are sorted by their
/// category (getCategories) and by their order.
///
/// API for the global search: getTopics (id, title, summary, keywords, headings
/// with anchors), search (scored matches of a text), getRoute (route of a topic and
/// of an anchor).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppHelpCatalog
{
public:
    PDFAppHelpCatalog() = default;

    /// Folder of the files in the resources
    static QString getResourceDirectory() { return QStringLiteral(":/help"); }

    /// Largest file of a topic
    static constexpr qsizetype MAXIMUM_FILE_SIZE = 512 * 1024;

    /// Loads the English topics of the folder (damaged files are logged and skipped)
    static PDFAppHelpCatalog load(const QString& directory = getResourceDirectory());

    /// Catalog of the resources of the application, parsed once per process (the
    /// first call, in the thread of the user interface); the resources never change,
    /// so the help pages and the global search share this immutable object
    static std::shared_ptr<const PDFAppHelpCatalog> getBuiltin();

    /// Parses a topic file. \returns false and the errors ("line: message"), if it is not valid.
    static bool parseTopic(const QString& id, const QString& text, PDFAppHelpTopic* topic, QStringList* errors);

    /// Categories of the manual in their order
    static const std::vector<PDFAppHelpCategory>& getCategories();
    static const PDFAppHelpCategory* findCategory(const QString& id);

    const std::vector<PDFAppHelpTopic>& getTopics() const { return m_topics; }
    const PDFAppHelpTopic* findTopic(const QString& id) const;

    /// Topics of a category in their order
    std::vector<const PDFAppHelpTopic*> getCategoryTopics(const QString& categoryId) const;

    /// The next topic of the same category (nullptr for the last one)
    const PDFAppHelpTopic* getNextTopic(const QString& id) const;

    /// The topic in the language (file <id>.<language>.md, fallbacks pt_BR -> pt ->
    /// English). A topic with an empty id for an unknown topic.
    PDFAppHelpTopic getLocalizedTopic(const QString& id, const QString& language) const;

    /// Route of the topic, with the anchor of a heading ("/help/settings#storage")
    static QString getRoute(const QString& id, const QString& anchor = QString());

    /// Topics and headings, which contain all words of the text (case insensitive),
    /// sorted by the score
    std::vector<PDFAppHelpMatch> search(const QString& text) const;

    /// Errors of the files of the last load
    const QStringList& getErrors() const { return m_errors; }

private:
    QString m_directory;
    std::vector<PDFAppHelpTopic> m_topics;
    QStringList m_errors;
};

}   // namespace pdf

#endif // PDFAPPHELPCATALOG_H
