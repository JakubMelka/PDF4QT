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

#include "pdfmcpcatalog.h"
#include "pdfmcpjson.h"

namespace pdf
{

void PDFMcpIcon::write(PDFMcpJsonWriter& writer) const
{
    writer.beginObject();
    writer.writeMember(QLatin1StringView("src"), QStringView(source));
    writer.writeMember(QLatin1StringView("mimeType"), QStringView(mimeType));
    if (!sizes.isEmpty())
    {
        writer.key(QLatin1StringView("sizes"));
        writer.writeStringArray(sizes);
    }
    if (!theme.isEmpty())
    {
        writer.writeMember(QLatin1StringView("theme"), QStringView(theme));
    }
    writer.endObject();
}

void PDFMcpToolAnnotations::write(PDFMcpJsonWriter& writer) const
{
    writer.beginObject();
    if (!title.isEmpty())
    {
        writer.writeMember(QLatin1StringView("title"), QStringView(title));
    }
    writer.writeMember(QLatin1StringView("readOnlyHint"), readOnlyHint);
    writer.writeMember(QLatin1StringView("destructiveHint"), destructiveHint);
    writer.writeMember(QLatin1StringView("idempotentHint"), idempotentHint);
    writer.writeMember(QLatin1StringView("openWorldHint"), openWorldHint);
    writer.endObject();
}

/// Helpers of the catalog
class PDFMcpCatalogHelper
{
public:
    PDFMcpCatalogHelper() = delete;

    static void writeIcons(PDFMcpJsonWriter& writer, const std::vector<PDFMcpIcon>& icons, PDFMcpEra era)
    {
        // Icons are a feature of 2025-11-25 and newer
        if (icons.empty() || era == PDFMcpEra::Legacy20250618)
        {
            return;
        }

        writer.key(QLatin1StringView("icons"));
        writer.beginArray();
        for (const PDFMcpIcon& icon : icons)
        {
            icon.write(writer);
        }
        writer.endArray();
    }

    static void writeOptional(PDFMcpJsonWriter& writer, QLatin1StringView key, const QString& value)
    {
        if (!value.isEmpty())
        {
            writer.writeMember(key, QStringView(value));
        }
    }
};

void PDFMcpToolDefinition::write(PDFMcpJsonWriter& writer, PDFMcpEra era) const
{
    writer.beginObject();
    writer.writeMember(QLatin1StringView("name"), QStringView(name));
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("title"), title);
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("description"), description);
    writer.key(QLatin1StringView("inputSchema"));
    writer.writeRaw(inputSchema.isEmpty() ? QByteArrayView("{\"type\":\"object\"}") : QByteArrayView(inputSchema));
    if (!outputSchema.isEmpty())
    {
        writer.key(QLatin1StringView("outputSchema"));
        writer.writeRaw(outputSchema);
    }
    writer.key(QLatin1StringView("annotations"));
    annotations.write(writer);
    PDFMcpCatalogHelper::writeIcons(writer, icons, era);
    writer.endObject();
}

void PDFMcpResourceDefinition::write(PDFMcpJsonWriter& writer, PDFMcpEra era) const
{
    writer.beginObject();
    writer.writeMember(QLatin1StringView("uri"), QStringView(uri));
    writer.writeMember(QLatin1StringView("name"), QStringView(name));
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("title"), title);
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("description"), description);
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("mimeType"), mimeType);
    if (size >= 0)
    {
        writer.writeMember(QLatin1StringView("size"), size);
    }
    PDFMcpCatalogHelper::writeIcons(writer, icons, era);
    writer.endObject();
}

void PDFMcpResourceTemplateDefinition::write(PDFMcpJsonWriter& writer, PDFMcpEra era) const
{
    Q_UNUSED(era);

    writer.beginObject();
    writer.writeMember(QLatin1StringView("uriTemplate"), QStringView(uriTemplate.getText()));
    writer.writeMember(QLatin1StringView("name"), QStringView(name));
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("title"), title);
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("description"), description);
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("mimeType"), mimeType);
    writer.endObject();
}

void PDFMcpPromptDefinition::write(PDFMcpJsonWriter& writer, PDFMcpEra era) const
{
    writer.beginObject();
    writer.writeMember(QLatin1StringView("name"), QStringView(name));
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("title"), title);
    PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("description"), description);
    if (!arguments.empty())
    {
        writer.key(QLatin1StringView("arguments"));
        writer.beginArray();
        for (const PDFMcpPromptArgument& argument : arguments)
        {
            writer.beginObject();
            writer.writeMember(QLatin1StringView("name"), QStringView(argument.name));
            PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("title"), argument.title);
            PDFMcpCatalogHelper::writeOptional(writer, QLatin1StringView("description"), argument.description);
            writer.writeMember(QLatin1StringView("required"), argument.required);
            writer.endObject();
        }
        writer.endArray();
    }
    PDFMcpCatalogHelper::writeIcons(writer, icons, era);
    writer.endObject();
}

PDFMcpCatalog::PDFMcpCatalog(quint64 generation,
                             std::vector<PDFMcpToolDefinition> tools,
                             std::vector<PDFMcpResourceDefinition> resources,
                             std::vector<PDFMcpResourceTemplateDefinition> templates,
                             std::vector<PDFMcpPromptDefinition> prompts) :
    m_generation(generation),
    m_tools(std::move(tools)),
    m_resources(std::move(resources)),
    m_templates(std::move(templates)),
    m_prompts(std::move(prompts))
{
    serialize();
}

PDFMcpCatalog::PDFMcpCatalog()
{
    serialize();
}

size_t PDFMcpCatalog::getEraIndex(PDFMcpEra era)
{
    switch (era)
    {
        case PDFMcpEra::Modern:
            return 0;
        case PDFMcpEra::Legacy20251125:
            return 1;
        case PDFMcpEra::Legacy20250618:
            return 2;
    }
    return 0;
}

void PDFMcpCatalog::serialize()
{
    for (PDFMcpEra era : { PDFMcpEra::Modern, PDFMcpEra::Legacy20251125, PDFMcpEra::Legacy20250618 })
    {
        const size_t index = getEraIndex(era);

        PDFMcpJsonWriter tools;
        tools.beginArray();
        for (const PDFMcpToolDefinition& tool : m_tools)
        {
            tool.write(tools, era);
        }
        tools.endArray();
        m_toolsJson[index] = tools.take();

        PDFMcpJsonWriter resources;
        resources.beginArray();
        for (const PDFMcpResourceDefinition& resource : m_resources)
        {
            resource.write(resources, era);
        }
        resources.endArray();
        m_resourcesJson[index] = resources.take();

        PDFMcpJsonWriter templates;
        templates.beginArray();
        for (const PDFMcpResourceTemplateDefinition& resourceTemplate : m_templates)
        {
            resourceTemplate.write(templates, era);
        }
        templates.endArray();
        m_templatesJson[index] = templates.take();

        PDFMcpJsonWriter prompts;
        prompts.beginArray();
        for (const PDFMcpPromptDefinition& prompt : m_prompts)
        {
            prompt.write(prompts, era);
        }
        prompts.endArray();
        m_promptsJson[index] = prompts.take();
    }
}

const PDFMcpToolDefinition* PDFMcpCatalog::findTool(QStringView name) const
{
    for (const PDFMcpToolDefinition& tool : m_tools)
    {
        if (tool.name == name)
        {
            return &tool;
        }
    }
    return nullptr;
}

const PDFMcpResourceDefinition* PDFMcpCatalog::findResource(QStringView uri) const
{
    for (const PDFMcpResourceDefinition& resource : m_resources)
    {
        if (resource.uri == uri)
        {
            return &resource;
        }
    }
    return nullptr;
}

const PDFMcpPromptDefinition* PDFMcpCatalog::findPrompt(QStringView name) const
{
    for (const PDFMcpPromptDefinition& prompt : m_prompts)
    {
        if (prompt.name == name)
        {
            return &prompt;
        }
    }
    return nullptr;
}

const PDFMcpResourceTemplateDefinition* PDFMcpCatalog::matchTemplate(const QString& uri, QMap<QString, QString>* values) const
{
    for (const PDFMcpResourceTemplateDefinition& resourceTemplate : m_templates)
    {
        std::optional<QMap<QString, QString>> match = resourceTemplate.uriTemplate.match(uri);
        if (match)
        {
            if (values)
            {
                *values = std::move(*match);
            }
            return &resourceTemplate;
        }
    }
    return nullptr;
}

const QByteArray& PDFMcpCatalog::getToolsJson(PDFMcpEra era) const
{
    return m_toolsJson[getEraIndex(era)];
}

const QByteArray& PDFMcpCatalog::getResourcesJson(PDFMcpEra era) const
{
    return m_resourcesJson[getEraIndex(era)];
}

const QByteArray& PDFMcpCatalog::getTemplatesJson(PDFMcpEra era) const
{
    return m_templatesJson[getEraIndex(era)];
}

const QByteArray& PDFMcpCatalog::getPromptsJson(PDFMcpEra era) const
{
    return m_promptsJson[getEraIndex(era)];
}

// ----------------------------------------------------------------------------
// PDFMcpContentBuilder
// ----------------------------------------------------------------------------

PDFMcpContentBuilder::PDFMcpContentBuilder()
{
    m_writer.beginObject();
    m_writer.key(QLatin1StringView("content"));
    m_writer.beginArray();
}

void PDFMcpContentBuilder::addText(const QString& text)
{
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("type"), QLatin1StringView("text"));
    m_writer.writeMember(QLatin1StringView("text"), QStringView(text));
    m_writer.endObject();
}

void PDFMcpContentBuilder::addImage(QByteArrayView data, const QString& mimeType)
{
    m_writer.reserve(m_writer.getData().size() + ((data.size() + 2) / 3) * 4 + 128);
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("type"), QLatin1StringView("image"));
    m_writer.key(QLatin1StringView("data"));
    m_writer.writeBase64(data);
    m_writer.writeMember(QLatin1StringView("mimeType"), QStringView(mimeType));
    m_writer.endObject();
}

void PDFMcpContentBuilder::addResourceLink(const QString& uri, const QString& name, const QString& title, const QString& mimeType, qint64 size)
{
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("type"), QLatin1StringView("resource_link"));
    m_writer.writeMember(QLatin1StringView("uri"), QStringView(uri));
    m_writer.writeMember(QLatin1StringView("name"), QStringView(name));
    PDFMcpCatalogHelper::writeOptional(m_writer, QLatin1StringView("title"), title);
    PDFMcpCatalogHelper::writeOptional(m_writer, QLatin1StringView("mimeType"), mimeType);
    if (size >= 0)
    {
        m_writer.writeMember(QLatin1StringView("size"), size);
    }
    m_writer.endObject();
}

void PDFMcpContentBuilder::addEmbeddedText(const QString& uri, const QString& mimeType, const QString& text)
{
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("type"), QLatin1StringView("resource"));
    m_writer.key(QLatin1StringView("resource"));
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("uri"), QStringView(uri));
    PDFMcpCatalogHelper::writeOptional(m_writer, QLatin1StringView("mimeType"), mimeType);
    m_writer.writeMember(QLatin1StringView("text"), QStringView(text));
    m_writer.endObject();
    m_writer.endObject();
}

void PDFMcpContentBuilder::addEmbeddedBlob(const QString& uri, const QString& mimeType, QByteArrayView data)
{
    m_writer.reserve(m_writer.getData().size() + ((data.size() + 2) / 3) * 4 + 128);
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("type"), QLatin1StringView("resource"));
    m_writer.key(QLatin1StringView("resource"));
    m_writer.beginObject();
    m_writer.writeMember(QLatin1StringView("uri"), QStringView(uri));
    PDFMcpCatalogHelper::writeOptional(m_writer, QLatin1StringView("mimeType"), mimeType);
    m_writer.key(QLatin1StringView("blob"));
    m_writer.writeBase64(data);
    m_writer.endObject();
    m_writer.endObject();
}

QByteArray PDFMcpContentBuilder::createResult(bool isError)
{
    m_writer.endArray();
    if (!m_structuredContent.isEmpty())
    {
        m_writer.key(QLatin1StringView("structuredContent"));
        m_writer.writeRaw(m_structuredContent);
    }
    m_writer.writeMember(QLatin1StringView("isError"), isError);
    m_writer.endObject();

    QByteArray result = m_writer.take();
    m_structuredContent.clear();
    m_writer.beginObject();
    m_writer.key(QLatin1StringView("content"));
    m_writer.beginArray();
    return result;
}

PDFMcpCallResult PDFMcpContentBuilder::createCallResult(bool isError)
{
    return PDFMcpCallResult::createComplete(createResult(isError), isError);
}

}   // namespace pdf
