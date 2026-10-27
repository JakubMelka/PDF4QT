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

#ifndef PDFMCPCATALOG_H
#define PDFMCPCATALOG_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcpcall.h"
#include "pdfmcpuritemplate.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>
#include <vector>

namespace pdf
{

class PDFMcpJsonWriter;

/// Icon of the server, a tool or a prompt (plan 1.4): a data URI of a PNG
/// rendered at the start of the server; not sent to 2025-06-18 clients
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpIcon
{
public:
    QString source;             ///< "data:image/png;base64,…"
    QString mimeType = QStringLiteral("image/png");
    QStringList sizes;          ///< "48x48"
    QString theme;              ///< "light" / "dark" / empty

    void write(PDFMcpJsonWriter& writer) const;
};

/// Hints of a tool (plan 1.4); openWorldHint is false everywhere
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpToolAnnotations
{
public:
    QString title;
    bool readOnlyHint = false;
    bool destructiveHint = false;
    bool idempotentHint = false;
    bool openWorldHint = false;

    void write(PDFMcpJsonWriter& writer) const;
};

/// Tool of the catalog: the schemas are serialized once (PDFMcpSchema::toJson)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpToolDefinition
{
public:
    QString name;
    QString title;
    QString description;
    QByteArray inputSchema;     ///< Serialized object schema
    QByteArray outputSchema;    ///< Serialized object schema (empty = none)
    PDFMcpToolAnnotations annotations;
    std::vector<PDFMcpIcon> icons;

    /// Writes the tool of tools/list in the era (no icons for 2025-06-18)
    void write(PDFMcpJsonWriter& writer, PDFMcpEra era) const;
};

/// Resource of resources/list: a static file of the server (content in the
/// catalog) or a document resource (read by the host)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpResourceDefinition
{
public:
    QString uri;
    QString name;
    QString title;
    QString description;
    QString mimeType;
    qint64 size = -1;           ///< Size in bytes, -1 = unknown
    bool isStatic = false;      ///< Content of the catalog, cacheScope public, never updated
    QByteArray content;         ///< Content of a static resource
    std::vector<PDFMcpIcon> icons;

    void write(PDFMcpJsonWriter& writer, PDFMcpEra era) const;
};

/// Template of resources/templates/list
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpResourceTemplateDefinition
{
public:
    PDFMcpUriTemplate uriTemplate;
    QString name;
    QString title;
    QString description;
    QString mimeType;
    bool isStatic = false;      ///< The template of static files (pdf4qt://help/{topic}.md, ...)

    void write(PDFMcpJsonWriter& writer, PDFMcpEra era) const;
};

/// Argument of a prompt (strings only)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpPromptArgument
{
public:
    QString name;
    QString title;
    QString description;
    bool required = false;
};

/// Prompt of prompts/list
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpPromptDefinition
{
public:
    QString name;
    QString title;
    QString description;
    std::vector<PDFMcpPromptArgument> arguments;
    std::vector<PDFMcpIcon> icons;

    void write(PDFMcpJsonWriter& writer, PDFMcpEra era) const;
};

/// Immutable snapshot of the definitions of the server (plan 2.2): the host
/// builds a new catalog (a new generation) whenever the permissions or the
/// registry of the tools change and publishes it by PDFMcpServer::publishCatalog;
/// the server answers the list methods from the snapshot in its control thread
/// and compares the serialized lists for list_changed. The lists are serialized
/// once per era in the constructor; a catalog is shared between threads by
/// std::shared_ptr<const PDFMcpCatalog>.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpCatalog
{
public:
    PDFMcpCatalog(quint64 generation,
                  std::vector<PDFMcpToolDefinition> tools,
                  std::vector<PDFMcpResourceDefinition> resources,
                  std::vector<PDFMcpResourceTemplateDefinition> templates,
                  std::vector<PDFMcpPromptDefinition> prompts);

    /// An empty catalog of the generation 0
    PDFMcpCatalog();

    quint64 getGeneration() const { return m_generation; }

    const std::vector<PDFMcpToolDefinition>& getTools() const { return m_tools; }
    const std::vector<PDFMcpResourceDefinition>& getResources() const { return m_resources; }
    const std::vector<PDFMcpResourceTemplateDefinition>& getTemplates() const { return m_templates; }
    const std::vector<PDFMcpPromptDefinition>& getPrompts() const { return m_prompts; }

    const PDFMcpToolDefinition* findTool(QStringView name) const;
    const PDFMcpResourceDefinition* findResource(QStringView uri) const;
    const PDFMcpPromptDefinition* findPrompt(QStringView name) const;

    /// First template, which matches the URI (its values in \p values)
    const PDFMcpResourceTemplateDefinition* matchTemplate(const QString& uri, QMap<QString, QString>* values) const;

    /// Serialized arrays of the lists in the era ("[...]"); the resources are
    /// the whole list (the dispatcher pages them by getResources)
    const QByteArray& getToolsJson(PDFMcpEra era) const;
    const QByteArray& getResourcesJson(PDFMcpEra era) const;
    const QByteArray& getTemplatesJson(PDFMcpEra era) const;
    const QByteArray& getPromptsJson(PDFMcpEra era) const;

private:
    static size_t getEraIndex(PDFMcpEra era);
    void serialize();

    quint64 m_generation = 0;
    std::vector<PDFMcpToolDefinition> m_tools;
    std::vector<PDFMcpResourceDefinition> m_resources;
    std::vector<PDFMcpResourceTemplateDefinition> m_templates;
    std::vector<PDFMcpPromptDefinition> m_prompts;

    std::array<QByteArray, 3> m_toolsJson;
    std::array<QByteArray, 3> m_resourcesJson;
    std::array<QByteArray, 3> m_templatesJson;
    std::array<QByteArray, 3> m_promptsJson;
};

/// Content of a tool result (plan 1.4): text, image, resource_link and embedded
/// resource blocks and the structured content; the image data are encoded as
/// base64 directly into the result. createResult writes
/// {"content":[...],"structuredContent":{...},"isError":…}.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpContentBuilder
{
public:
    PDFMcpContentBuilder();

    void addText(const QString& text);
    void addImage(QByteArrayView data, const QString& mimeType);
    void addResourceLink(const QString& uri, const QString& name, const QString& title = QString(), const QString& mimeType = QString(), qint64 size = -1);
    void addEmbeddedText(const QString& uri, const QString& mimeType, const QString& text);
    void addEmbeddedBlob(const QString& uri, const QString& mimeType, QByteArrayView data);

    /// Serialized structured content object (written by PDFMcpJsonWriter)
    void setStructuredContent(QByteArray structuredContent) { m_structuredContent = std::move(structuredContent); }

    /// Reserves the output (big images)
    void reserve(qsizetype size) { m_writer.reserve(size); }

    /// The result object; the builder is empty afterwards
    QByteArray createResult(bool isError);

    /// The tool result of the call
    PDFMcpCallResult createCallResult(bool isError);

private:
    PDFMcpJsonWriter m_writer;
    QByteArray m_structuredContent;
};

}   // namespace pdf

#endif // PDFMCPCATALOG_H
