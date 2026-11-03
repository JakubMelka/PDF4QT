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

#ifndef MCPSHAPE_H
#define MCPSHAPE_H

#include "mcptestclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cmath>

/// Shapes of the messages of the server against the schema of their era
/// (review RP7: SCR/mcp/schema_*.ts of 2026-07-28 and 2025-11-25; 2025-06-18 is
/// 2025-11-25 without icons, Implementation.description / websiteUrl, the
/// elicitation mode, the experimental tasks and the extensions). A spec lists
/// the members "name:type" or "name?:type" (optional) with the types s string,
/// i integer, n number, b boolean, o object, a array, I request id (string or
/// integer), N number or null, x any. Every shape is closed: a member outside of
/// its spec is an error (the server never writes a member, which its era does
/// not know - resultType / ttlMs / serverInfo to a legacy client, icons to a
/// 2025-06-18 client).
class McpShape
{
public:
    McpShape() = delete;

    using Era = pdftest::McpTestEra;
    using Checker = void (*)(const QJsonValue& value, Era era, const QString& path, QStringList* errors);

    static bool isModern(Era era) { return era == Era::Modern; }

    static bool hasType(const QJsonValue& value, QStringView type)
    {
        if (type == u"s")
        {
            return value.isString();
        }
        if (type == u"i")
        {
            return value.isDouble() && std::isfinite(value.toDouble()) && std::floor(value.toDouble()) == value.toDouble();
        }
        if (type == u"n")
        {
            return value.isDouble();
        }
        if (type == u"b")
        {
            return value.isBool();
        }
        if (type == u"o")
        {
            return value.isObject();
        }
        if (type == u"a")
        {
            return value.isArray();
        }
        if (type == u"I")
        {
            return value.isString() || hasType(value, u"i");
        }
        if (type == u"N")
        {
            return value.isNull() || hasType(value, u"i");
        }
        return true;
    }

    /// Checks the members of an object against the spec (closed)
    static void check(const QJsonValue& value, const QString& spec, const QString& path, QStringList* errors)
    {
        if (!value.isObject())
        {
            errors->append(path + QStringLiteral(": expected an object"));
            return;
        }

        const QJsonObject object = value.toObject();
        QStringList known;
        for (const QString& member : spec.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        {
            const qsizetype colon = member.lastIndexOf(QLatin1Char(':'));
            QString name = member.left(colon);
            const QString type = member.mid(colon + 1);
            const bool isOptional = name.endsWith(QLatin1Char('?'));
            if (isOptional)
            {
                name.chop(1);
            }
            known.append(name);

            const QJsonValue item = object.value(name);
            if (item.isUndefined())
            {
                if (!isOptional)
                {
                    errors->append(QStringLiteral("%1.%2: missing").arg(path, name));
                }
                continue;
            }
            if (!hasType(item, type))
            {
                errors->append(QStringLiteral("%1.%2: expected the type %3").arg(path, name, type));
            }
        }

        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (!known.contains(it.key()))
            {
                errors->append(QStringLiteral("%1.%2: not a member of the schema of the era").arg(path, it.key()));
            }
        }
    }

    static void expectValue(const QJsonValue& value, const QJsonValue& expected, const QString& path, QStringList* errors)
    {
        if (value != expected)
        {
            errors->append(QStringLiteral("%1: %2 instead of %3").arg(path, QString::fromUtf8(QJsonDocument(QJsonArray({ value })).toJson(QJsonDocument::Compact)),
                                                                        QString::fromUtf8(QJsonDocument(QJsonArray({ expected })).toJson(QJsonDocument::Compact))));
        }
    }

    static void checkIcons(const QJsonObject& object, const QString& path, QStringList* errors)
    {
        const QJsonArray icons = object.value(QStringLiteral("icons")).toArray();
        for (qsizetype i = 0; i < icons.size(); ++i)
        {
            const QString iconPath = QStringLiteral("%1.icons[%2]").arg(path).arg(i);
            check(icons[i], QStringLiteral("src:s mimeType?:s sizes?:a theme?:s"), iconPath, errors);
            const QJsonObject icon = icons[i].toObject();
            const QString source = icon.value(QStringLiteral("src")).toString();
            if (!source.startsWith(QLatin1String("data:")) && !source.startsWith(QLatin1String("https:")))
            {
                errors->append(iconPath + QStringLiteral(".src: only https: or data: URIs"));
            }
            if (icon.contains(QStringLiteral("theme")) && icon.value(QStringLiteral("theme")) != QLatin1String("light") && icon.value(QStringLiteral("theme")) != QLatin1String("dark"))
            {
                errors->append(iconPath + QStringLiteral(".theme: light or dark"));
            }
        }
    }

    /// The members of icons: none for 2025-06-18
    static QString iconsMember(Era era)
    {
        return era == Era::Legacy20250618 ? QString() : QStringLiteral(" icons?:a");
    }

    static void checkImplementation(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        if (era == Era::Legacy20250618)
        {
            check(value, QStringLiteral("name:s title?:s version:s"), path, errors);
            return;
        }
        check(value, QStringLiteral("name:s title?:s version:s description?:s websiteUrl?:s icons?:a"), path, errors);
        checkIcons(value.toObject(), path, errors);
    }

    /// The spec of a result: its own members and the members of Result of the era
    static QString resultSpec(Era era, const QString& members, bool isCacheable)
    {
        if (!isModern(era))
        {
            return members + QStringLiteral(" _meta?:o");
        }
        return members + QStringLiteral(" resultType:s _meta:o") + (isCacheable ? QStringLiteral(" ttlMs:i cacheScope:s") : QString());
    }

    /// The members of Result: resultType, the serverInfo of _meta, the cache fields
    static void checkResultBase(const QJsonObject& result, Era era, const QString& path, QStringList* errors, const QString& resultType = QStringLiteral("complete"))
    {
        if (!isModern(era))
        {
            if (result.value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/serverInfo")))
            {
                errors->append(path + QStringLiteral("._meta: serverInfo of a result is 2026-07-28"));
            }
            return;
        }

        expectValue(result.value(QStringLiteral("resultType")), resultType, path + QStringLiteral(".resultType"), errors);
        const QJsonObject meta = result.value(QStringLiteral("_meta")).toObject();
        checkImplementation(meta.value(QStringLiteral("io.modelcontextprotocol/serverInfo")), Era::Modern, path + QStringLiteral("._meta.serverInfo"), errors);
        if (result.contains(QStringLiteral("ttlMs")) && result.value(QStringLiteral("ttlMs")).toDouble() < 0.0)
        {
            errors->append(path + QStringLiteral(".ttlMs: MUST be >= 0"));
        }
        if (result.contains(QStringLiteral("cacheScope")) && result.value(QStringLiteral("cacheScope")) != QLatin1String("public") && result.value(QStringLiteral("cacheScope")) != QLatin1String("private"))
        {
            errors->append(path + QStringLiteral(".cacheScope: public or private"));
        }
    }

    /// A JSON-RPC response with a result; returns the result
    static QJsonObject checkResponse(const QJsonObject& response, const QString& path, QStringList* errors)
    {
        check(response, QStringLiteral("jsonrpc:s id:I result:o"), path, errors);
        expectValue(response.value(QStringLiteral("jsonrpc")), QStringLiteral("2.0"), path + QStringLiteral(".jsonrpc"), errors);
        return response.value(QStringLiteral("result")).toObject();
    }

    /// A JSON-RPC error response (the data checked by \p dataSpec, when given)
    static void checkError(const QJsonObject& response, int code, const QString& dataSpec, const QString& path, QStringList* errors)
    {
        // The schemas of both eras say id?: RequestId: an id, which could not be
        // read, is omitted - never null (owner decision of 2026-10-27, follow-up 31)
        check(response, QStringLiteral("jsonrpc:s id?:x error:o"), path, errors);
        const QJsonValue id = response.value(QStringLiteral("id"));
        if (!id.isUndefined() && !hasType(id, u"I"))
        {
            errors->append(path + QStringLiteral(".id: string or integer (omitted, when unreadable)"));
        }
        check(response.value(QStringLiteral("error")), dataSpec.isEmpty() ? QStringLiteral("code:i message:s data?:x") : QStringLiteral("code:i message:s data:o"),
              path + QStringLiteral(".error"), errors);
        const QJsonObject error = response.value(QStringLiteral("error")).toObject();
        expectValue(error.value(QStringLiteral("code")), code, path + QStringLiteral(".error.code"), errors);
        if (!dataSpec.isEmpty())
        {
            check(error.value(QStringLiteral("data")), dataSpec, path + QStringLiteral(".error.data"), errors);
        }
    }

    static void checkCapabilities(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        const QString extra = isModern(era) ? QStringLiteral(" extensions?:o") : (era == Era::Legacy20251125 ? QStringLiteral(" tasks?:o") : QString());
        check(value, QStringLiteral("experimental?:o logging?:o completions?:o prompts?:o resources?:o tools?:o") + extra, path, errors);
        const QJsonObject capabilities = value.toObject();
        if (capabilities.contains(QStringLiteral("prompts")))
        {
            check(capabilities.value(QStringLiteral("prompts")), QStringLiteral("listChanged?:b"), path + QStringLiteral(".prompts"), errors);
        }
        if (capabilities.contains(QStringLiteral("resources")))
        {
            check(capabilities.value(QStringLiteral("resources")), QStringLiteral("subscribe?:b listChanged?:b"), path + QStringLiteral(".resources"), errors);
        }
        if (capabilities.contains(QStringLiteral("tools")))
        {
            check(capabilities.value(QStringLiteral("tools")), QStringLiteral("listChanged?:b"), path + QStringLiteral(".tools"), errors);
        }
    }

    static void checkDiscover(const QJsonObject& response, QStringList* errors)
    {
        const QJsonObject result = checkResponse(response, QStringLiteral("server/discover"), errors);
        check(result, resultSpec(Era::Modern, QStringLiteral("supportedVersions:a capabilities:o instructions?:s"), true), QStringLiteral("DiscoverResult"), errors);
        checkResultBase(result, Era::Modern, QStringLiteral("DiscoverResult"), errors);
        checkCapabilities(result.value(QStringLiteral("capabilities")), Era::Modern, QStringLiteral("DiscoverResult.capabilities"), errors);
    }

    static void checkInitialize(const QJsonObject& response, Era era, QStringList* errors)
    {
        const QJsonObject result = checkResponse(response, QStringLiteral("initialize"), errors);
        check(result, QStringLiteral("protocolVersion:s capabilities:o serverInfo:o instructions?:s _meta?:o"), QStringLiteral("InitializeResult"), errors);
        checkResultBase(result, era, QStringLiteral("InitializeResult"), errors);
        checkCapabilities(result.value(QStringLiteral("capabilities")), era, QStringLiteral("InitializeResult.capabilities"), errors);
        checkImplementation(result.value(QStringLiteral("serverInfo")), era, QStringLiteral("InitializeResult.serverInfo"), errors);
    }

    /// EmptyResult (ping, logging/setLevel, resources/subscribe, tasks/update, tasks/cancel)
    static void checkEmpty(const QJsonObject& response, Era era, const QString& path, QStringList* errors)
    {
        const QJsonObject result = checkResponse(response, path, errors);
        check(result, resultSpec(era, QString(), false), path + QStringLiteral(".result"), errors);
        checkResultBase(result, era, path + QStringLiteral(".result"), errors);
    }

    static void checkToolAnnotations(const QJsonValue& value, const QString& path, QStringList* errors)
    {
        check(value, QStringLiteral("title?:s readOnlyHint?:b destructiveHint?:b idempotentHint?:b openWorldHint?:b"), path, errors);
    }

    static void checkTool(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        const QString execution = era == Era::Legacy20251125 ? QStringLiteral(" execution?:o") : QString();
        check(value, QStringLiteral("name:s title?:s description?:s inputSchema:o outputSchema?:o annotations?:o _meta?:o") + iconsMember(era) + execution, path, errors);
        const QJsonObject tool = value.toObject();
        checkIcons(tool, path, errors);
        expectValue(tool.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")), QStringLiteral("object"), path + QStringLiteral(".inputSchema.type"), errors);
        if (!isModern(era) && tool.contains(QStringLiteral("outputSchema")))
        {
            // 2025-11-25 and 2025-06-18: outputSchema of the type object
            expectValue(tool.value(QStringLiteral("outputSchema")).toObject().value(QStringLiteral("type")), QStringLiteral("object"), path + QStringLiteral(".outputSchema.type"), errors);
        }
        if (tool.contains(QStringLiteral("annotations")))
        {
            checkToolAnnotations(tool.value(QStringLiteral("annotations")), path + QStringLiteral(".annotations"), errors);
        }
    }

    static QString resourceMembers(Era era)
    {
        return QStringLiteral("uri:s name:s title?:s description?:s mimeType?:s annotations?:o size?:i _meta?:o") + iconsMember(era);
    }

    static void checkResource(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        check(value, resourceMembers(era), path, errors);
        checkIcons(value.toObject(), path, errors);
    }

    static void checkTemplate(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        check(value, QStringLiteral("uriTemplate:s name:s title?:s description?:s mimeType?:s annotations?:o _meta?:o") + iconsMember(era), path, errors);
        checkIcons(value.toObject(), path, errors);
    }

    static void checkPrompt(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        check(value, QStringLiteral("name:s title?:s description?:s arguments?:a _meta?:o") + iconsMember(era), path, errors);
        checkIcons(value.toObject(), path, errors);
        const QJsonArray arguments = value.toObject().value(QStringLiteral("arguments")).toArray();
        for (qsizetype i = 0; i < arguments.size(); ++i)
        {
            check(arguments[i], QStringLiteral("name:s title?:s description?:s required?:b"), QStringLiteral("%1.arguments[%2]").arg(path).arg(i), errors);
        }
    }

    /// A list result (tools, prompts, resources, resourceTemplates); returns its items
    static QJsonArray checkList(const QJsonObject& response, Era era, const QString& key, Checker checker, QStringList* errors)
    {
        const QString path = key + QStringLiteral("/list");
        const QJsonObject result = checkResponse(response, path, errors);
        check(result, resultSpec(era, key + QStringLiteral(":a nextCursor?:s"), true), path + QStringLiteral(".result"), errors);
        checkResultBase(result, era, path + QStringLiteral(".result"), errors);
        const QJsonArray items = result.value(key).toArray();
        for (qsizetype i = 0; i < items.size(); ++i)
        {
            checker(items[i], era, QStringLiteral("%1[%2]").arg(key).arg(i), errors);
        }
        return items;
    }

    static void checkResourceContents(const QJsonValue& value, const QString& path, QStringList* errors)
    {
        if (value.toObject().contains(QStringLiteral("blob")))
        {
            check(value, QStringLiteral("uri:s mimeType?:s blob:s _meta?:o"), path, errors);
            const QByteArray blob = value.toObject().value(QStringLiteral("blob")).toString().toLatin1();
            if (!QByteArray::fromBase64Encoding(blob, QByteArray::AbortOnBase64DecodingErrors))
            {
                errors->append(path + QStringLiteral(".blob: not base64"));
            }
            return;
        }
        check(value, QStringLiteral("uri:s mimeType?:s text:s _meta?:o"), path, errors);
    }

    static void checkContentBlock(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        const QString type = value.toObject().value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text"))
        {
            check(value, QStringLiteral("type:s text:s annotations?:o _meta?:o"), path, errors);
        }
        else if (type == QLatin1String("image") || type == QLatin1String("audio"))
        {
            check(value, QStringLiteral("type:s data:s mimeType:s annotations?:o _meta?:o"), path, errors);
        }
        else if (type == QLatin1String("resource_link"))
        {
            check(value, QStringLiteral("type:s ") + resourceMembers(era), path, errors);
        }
        else if (type == QLatin1String("resource"))
        {
            check(value, QStringLiteral("type:s resource:o annotations?:o _meta?:o"), path, errors);
            checkResourceContents(value.toObject().value(QStringLiteral("resource")), path + QStringLiteral(".resource"), errors);
        }
        else
        {
            errors->append(path + QStringLiteral(".type: unknown content type ") + type);
        }
    }

    static void checkCallToolResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("content:a structuredContent?:x isError?:b"), false), path, errors);
        checkResultBase(result, era, path, errors);
        if (!isModern(era) && result.contains(QStringLiteral("structuredContent")) && !result.value(QStringLiteral("structuredContent")).isObject())
        {
            errors->append(path + QStringLiteral(".structuredContent: an object of the legacy eras"));
        }
        const QJsonArray content = result.value(QStringLiteral("content")).toArray();
        for (qsizetype i = 0; i < content.size(); ++i)
        {
            checkContentBlock(content[i], era, QStringLiteral("%1.content[%2]").arg(path).arg(i), errors);
        }
    }

    static void checkReadResourceResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("contents:a"), true), path, errors);
        checkResultBase(result, era, path, errors);
        const QJsonArray contents = result.value(QStringLiteral("contents")).toArray();
        if (contents.isEmpty())
        {
            errors->append(path + QStringLiteral(".contents: MUST NOT be empty"));
        }
        for (qsizetype i = 0; i < contents.size(); ++i)
        {
            checkResourceContents(contents[i], QStringLiteral("%1.contents[%2]").arg(path).arg(i), errors);
        }
    }

    static void checkGetPromptResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("description?:s messages:a"), false), path, errors);
        checkResultBase(result, era, path, errors);
        const QJsonArray messages = result.value(QStringLiteral("messages")).toArray();
        for (qsizetype i = 0; i < messages.size(); ++i)
        {
            const QString messagePath = QStringLiteral("%1.messages[%2]").arg(path).arg(i);
            check(messages[i], QStringLiteral("role:s content:o"), messagePath, errors);
            const QString role = messages[i].toObject().value(QStringLiteral("role")).toString();
            if (role != QLatin1String("user") && role != QLatin1String("assistant"))
            {
                errors->append(messagePath + QStringLiteral(".role: user or assistant"));
            }
            checkContentBlock(messages[i].toObject().value(QStringLiteral("content")), era, messagePath + QStringLiteral(".content"), errors);
        }
    }

    static void checkCompleteResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("completion:o"), false), path, errors);
        checkResultBase(result, era, path, errors);
        check(result.value(QStringLiteral("completion")), QStringLiteral("values:a total?:i hasMore?:b"), path + QStringLiteral(".completion"), errors);
        const QJsonArray values = result.value(QStringLiteral("completion")).toObject().value(QStringLiteral("values")).toArray();
        if (values.size() > 100)
        {
            errors->append(path + QStringLiteral(".completion.values: at most 100"));
        }
    }

    /// params of elicitation/create (a request of the legacy eras, an input request of MRTR)
    static void checkElicitParams(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        if (era == Era::Legacy20250618)
        {
            check(value, QStringLiteral("message:s requestedSchema:o"), path, errors);
        }
        else
        {
            check(value, QStringLiteral("mode?:s message:s requestedSchema:o"), path, errors);
            if (value.toObject().contains(QStringLiteral("mode")))
            {
                expectValue(value.toObject().value(QStringLiteral("mode")), QStringLiteral("form"), path + QStringLiteral(".mode"), errors);
            }
        }

        const QJsonValue schema = value.toObject().value(QStringLiteral("requestedSchema"));
        check(schema, QStringLiteral("$schema?:s type:s properties:o required?:a"), path + QStringLiteral(".requestedSchema"), errors);
        expectValue(schema.toObject().value(QStringLiteral("type")), QStringLiteral("object"), path + QStringLiteral(".requestedSchema.type"), errors);
        const QJsonObject properties = schema.toObject().value(QStringLiteral("properties")).toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it)
        {
            // PrimitiveSchemaDefinition: flat, of a primitive type
            const QString type = it.value().toObject().value(QStringLiteral("type")).toString();
            if (type != QLatin1String("string") && type != QLatin1String("number") && type != QLatin1String("integer") && type != QLatin1String("boolean"))
            {
                errors->append(QStringLiteral("%1.requestedSchema.properties.%2: a primitive type").arg(path, it.key()));
            }
        }
    }

    static void checkInputRequiredResult(const QJsonObject& result, const QString& path, QStringList* errors)
    {
        check(result, QStringLiteral("resultType:s inputRequests?:o requestState?:s _meta:o"), path, errors);
        checkResultBase(result, Era::Modern, path, errors, QStringLiteral("input_required"));
        if (!result.contains(QStringLiteral("inputRequests")) && !result.contains(QStringLiteral("requestState")))
        {
            errors->append(path + QStringLiteral(": MUST include inputRequests or requestState"));
        }
        const QJsonObject inputRequests = result.value(QStringLiteral("inputRequests")).toObject();
        for (auto it = inputRequests.begin(); it != inputRequests.end(); ++it)
        {
            const QString requestPath = path + QStringLiteral(".inputRequests.") + it.key();
            check(it.value(), QStringLiteral("method:s params:o"), requestPath, errors);
            expectValue(it.value().toObject().value(QStringLiteral("method")), QStringLiteral("elicitation/create"), requestPath + QStringLiteral(".method"), errors);
            checkElicitParams(it.value().toObject().value(QStringLiteral("params")), Era::Modern, requestPath + QStringLiteral(".params"), errors);
        }
    }

    /// A notification; returns its params
    static QJsonObject checkNotification(const QJsonObject& message, const QString& method, const QString& paramsSpec, QStringList* errors)
    {
        check(message, paramsSpec.isNull() ? QStringLiteral("jsonrpc:s method:s params?:o") : QStringLiteral("jsonrpc:s method:s params:o"), method, errors);
        expectValue(message.value(QStringLiteral("method")), method, method + QStringLiteral(".method"), errors);
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (!paramsSpec.isNull())
        {
            check(params, paramsSpec, method + QStringLiteral(".params"), errors);
        }
        return params;
    }

    static QString taskMembers()
    {
        return QStringLiteral("taskId:s status:s statusMessage?:s createdAt:s lastUpdatedAt:s ttlMs:N pollIntervalMs?:i");
    }

    /// The members of a task by its status (tasks extension, DetailedTask)
    static QString detailedTaskMembers(const QJsonObject& task)
    {
        const QString status = task.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("completed"))
        {
            return taskMembers() + QStringLiteral(" result:o");
        }
        if (status == QLatin1String("failed"))
        {
            return taskMembers() + QStringLiteral(" error:o");
        }
        if (status == QLatin1String("input_required"))
        {
            return taskMembers() + QStringLiteral(" inputRequests:o");
        }
        return taskMembers();
    }

    static void checkTaskTimes(const QJsonObject& task, const QString& path, QStringList* errors)
    {
        for (const QString& key : { QStringLiteral("createdAt"), QStringLiteral("lastUpdatedAt") })
        {
            if (!QDateTime::fromString(task.value(key).toString(), Qt::ISODateWithMs).isValid())
            {
                errors->append(QStringLiteral("%1.%2: an ISO 8601 time").arg(path, key));
            }
        }
        const QString status = task.value(QStringLiteral("status")).toString();
        if (!QStringList({ QStringLiteral("working"), QStringLiteral("input_required"), QStringLiteral("completed"), QStringLiteral("failed"), QStringLiteral("cancelled") }).contains(status))
        {
            errors->append(path + QStringLiteral(".status: a TaskStatus"));
        }
    }

    /// Fails the test with the errors (all of them in the message)
    static bool report(const QStringList& errors)
    {
        if (errors.isEmpty())
        {
            return true;
        }
        qWarning("%s", qPrintable(errors.join(QLatin1Char('\n'))));
        return false;
    }
};

#endif // MCPSHAPE_H
