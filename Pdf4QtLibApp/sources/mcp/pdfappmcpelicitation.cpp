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


#include "pdfappmcpelicitation.h"

#include "pdfmcpjson.h"

namespace pdf
{

/// Helpers of PDFAppMcpElicitation
class PDFAppMcpElicitationHelper
{
public:
    PDFAppMcpElicitationHelper() = delete;

    /// Allowed values of a question in the form of the dispatcher (it checks the
    /// answers of a retry against them): { "<property>": [values] }
    static QJsonObject createAllowed(const QString& property, const QJsonArray& values)
    {
        QJsonObject allowed;
        allowed.insert(property, values);
        return allowed;
    }

    /// The value is one of the allowed values of the property of the question
    static bool isAllowed(const QJsonObject& allowed, const QString& property, const QJsonValue& value)
    {
        const QJsonValue values = allowed.value(property);
        return values.isArray() && values.toArray().contains(value);
    }

    /// The single value of a carried answer (undefined, when it is not one)
    static QJsonValue getCarriedValue(const QJsonObject& allowed, const QString& property)
    {
        const QJsonArray values = allowed.value(property).toArray();
        if (allowed.size() != 1 || values.size() != 1)
        {
            return QJsonValue(QJsonValue::Undefined);
        }
        return values.first();
    }
};

bool PDFAppMcpElicitation::isSupported(const PDFMcpCallRequest& request)
{
    const bool isKindSupported = request.kind == PDFMcpCallKind::ToolCall || request.kind == PDFMcpCallKind::PromptGet;
    return isKindSupported && request.client.supportsElicitation();
}

QJsonObject PDFAppMcpElicitation::getAllowedValues(const PDFMcpCallRequest& request)
{
    // The dispatcher gives either the unsealed data or the allowed values themselves
    const QJsonValue allowedValues = request.requestState.value(QStringLiteral("allowedValues"));
    if (allowedValues.isObject())
    {
        return allowedValues.toObject();
    }
    return request.requestState;
}

PDFAppMcpElicitationAnswer PDFAppMcpElicitation::getAnswer(const PDFMcpCallRequest& request, const QString& key, const QString& property)
{
    using Helper = PDFAppMcpElicitationHelper;

    PDFAppMcpElicitationAnswer answer;
    const QJsonObject allowedValues = getAllowedValues(request);

    const QJsonValue response = request.inputResponses.value(key);
    if (response.isObject())
    {
        const QJsonObject responseObject = response.toObject();
        const QString action = responseObject.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("decline"))
        {
            answer.status = PDFAppMcpElicitationAnswer::Status::Declined;
            return answer;
        }
        if (action == QLatin1String("cancel"))
        {
            answer.status = PDFAppMcpElicitationAnswer::Status::Canceled;
            return answer;
        }

        // An accepted answer must be one of the values offered by this server
        // (the allowed values are sealed in the request state); an answer without
        // the state of its question is not trusted
        const QJsonValue value = responseObject.value(QStringLiteral("content")).toObject().value(property);
        const QJsonValue allowed = allowedValues.value(key);
        if (action == QLatin1String("accept") && allowed.isObject() && Helper::isAllowed(allowed.toObject(), property, value))
        {
            answer.status = PDFAppMcpElicitationAnswer::Status::Accepted;
            answer.value = value;
            return answer;
        }

        answer.status = PDFAppMcpElicitationAnswer::Status::Invalid;
        return answer;
    }

    // An answer of an earlier round of the same call
    const QJsonValue carried = allowedValues.value(getCarriedPrefix() + key);
    if (carried.isObject())
    {
        const QJsonValue value = Helper::getCarriedValue(carried.toObject(), property);
        if (!value.isUndefined())
        {
            answer.status = PDFAppMcpElicitationAnswer::Status::Accepted;
            answer.value = value;
        }
    }
    return answer;
}

PDFMcpInputRequests PDFAppMcpElicitation::createRequests(const PDFMcpCallRequest& request)
{
    using Helper = PDFAppMcpElicitationHelper;

    PDFMcpInputRequests requests;
    const QJsonObject allowedValues = getAllowedValues(request);
    const QString prefix = getCarriedPrefix();
    for (auto it = allowedValues.constBegin(); it != allowedValues.constEnd(); ++it)
    {
        if (!it.value().isObject())
        {
            continue;
        }

        const QJsonObject allowed = it.value().toObject();
        if (it.key().startsWith(prefix))
        {
            // Answers of the earlier rounds stay
            if (allowed.size() == 1 && !Helper::getCarriedValue(allowed, allowed.constBegin().key()).isUndefined())
            {
                requests.allowedValues.insert(it.key(), allowed);
            }
            continue;
        }

        // The accepted answers of this round are carried to the next one
        for (auto propertyIt = allowed.constBegin(); propertyIt != allowed.constEnd(); ++propertyIt)
        {
            const PDFAppMcpElicitationAnswer answer = getAnswer(request, it.key(), propertyIt.key());
            if (answer.isAccepted())
            {
                requests.allowedValues.insert(prefix + it.key(), Helper::createAllowed(propertyIt.key(), QJsonArray({ answer.value })));
                break;
            }
        }
    }
    return requests;
}

void PDFAppMcpElicitation::addChoice(const PDFMcpCallRequest& request, PDFMcpInputRequests& requests, const QString& key, const QString& message,
                                     const QString& property, const QString& propertyTitle, const std::vector<PDFAppMcpElicitationOption>& options)
{
    using Helper = PDFAppMcpElicitationHelper;

    std::vector<PDFAppMcpElicitationOption> offered(options.begin(), options.begin() + qMin(options.size(), size_t(MAXIMUM_OPTIONS)));
    QJsonArray values;
    for (const PDFAppMcpElicitationOption& option : offered)
    {
        values.append(option.value);
    }

    // A question replaces the carried answer of the same key
    requests.allowedValues.remove(getCarriedPrefix() + key);
    requests.allowedValues.insert(key, Helper::createAllowed(property, values));

    PDFMcpInputRequest inputRequest;
    inputRequest.key = key;
    inputRequest.message = PDFMcpJson::sanitizeString(message, PDFMcpJson::SanitizeFlag::RemoveBidi, 1024);
    inputRequest.requestedSchema = createChoiceSchema(request.client.era, property, propertyTitle, offered);
    requests.requests.push_back(std::move(inputRequest));
}

QByteArray PDFAppMcpElicitation::createChoiceSchema(PDFMcpEra era, const QString& property, const QString& propertyTitle, const std::vector<PDFAppMcpElicitationOption>& options)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("type"), QLatin1StringView("object"));
    writer.key(QLatin1StringView("properties"));
    writer.beginObject();
    writer.key(property);
    writer.beginObject();
    writer.writeMember(QLatin1StringView("type"), QLatin1StringView("string"));
    writer.writeMember(QLatin1StringView("title"), QStringView(propertyTitle));

    if (era == PDFMcpEra::Legacy20250618)
    {
        // 2025-06-18: an enumeration with its display names
        writer.key(QLatin1StringView("enum"));
        writer.beginArray();
        for (const PDFAppMcpElicitationOption& option : options)
        {
            writer.writeString(option.value);
        }
        writer.endArray();
        writer.key(QLatin1StringView("enumNames"));
        writer.beginArray();
        for (const PDFAppMcpElicitationOption& option : options)
        {
            writer.writeString(PDFMcpJson::sanitizeString(option.title, PDFMcpJson::SanitizeFlag::RemoveBidi, MAXIMUM_TITLE_LENGTH));
        }
        writer.endArray();
    }
    else
    {
        // 2025-11-25 and 2026-07-28: a titled single-select enumeration
        writer.key(QLatin1StringView("oneOf"));
        writer.beginArray();
        for (const PDFAppMcpElicitationOption& option : options)
        {
            writer.beginObject();
            writer.writeMember(QLatin1StringView("const"), QStringView(option.value));
            writer.writeMember(QLatin1StringView("title"), PDFMcpJson::sanitizeString(option.title, PDFMcpJson::SanitizeFlag::RemoveBidi, MAXIMUM_TITLE_LENGTH));
            writer.endObject();
        }
        writer.endArray();
    }

    writer.endObject();
    writer.endObject();
    writer.key(QLatin1StringView("required"));
    writer.writeStringArray(QStringList({ property }));
    writer.endObject();
    return writer.take();
}

QString PDFAppMcpElicitation::getDeclinedText(const QString& what, const QString& fallback)
{
    return QStringLiteral("The user did not choose %1 in the client. %2").arg(what, fallback);
}

}   // namespace pdf
