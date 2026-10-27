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


#ifndef PDFAPPMCPELICITATION_H
#define PDFAPPMCPELICITATION_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <memory>

namespace pdf
{

/// Answer of one question of an elicitation in a retried request
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpElicitationAnswer
{
public:
    enum class Status
    {
        None,       ///< The request carries no answer of the question
        Accepted,   ///< The user chose a value of the allowed set (value)
        Declined,   ///< The user declined to answer
        Canceled,   ///< The user dismissed the question
        Invalid     ///< An answer, which is not one of the allowed values (or malformed)
    };

    Status status = Status::None;
    QJsonValue value;

    bool isAccepted() const { return status == Status::Accepted; }
};

/// One choice offered by an elicitation ("documentId": the id with the title of
/// the document as its label)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpElicitationOption
{
public:
    QString value;
    QString title;
};

/// Elicitation of non-sensitive choices (plan of step 7, 2.8 [D4]): the call
/// finishes with an input request (MRTR inputRequests of the modern era, a
/// legacy elicitation/create sent by the dispatcher on the stream of the call),
/// the client asks its user and retries the request with the answers.
///
/// Contract with the dispatcher (package A): a retried request carries the
/// answers in PDFMcpCallRequest::inputResponses as the map of the spec
/// ({ key: { "action": "accept" | "decline" | "cancel", "content": { ... } } })
/// and the verified sealed state in PDFMcpCallRequest::requestState - either the
/// object { "allowedValues": { ... } } or the allowed values themselves. The
/// allowed values of a question are { key: { property: [values] } } (the form of
/// PDFMcpDispatcher, which drops answers outside of them); an answer outside of
/// them is Invalid. The answers of the earlier rounds of one call (a document
/// chosen before a page label is asked) are carried in the allowed values under
/// "carried:<key>" with exactly one value - sealed by the dispatcher, so the
/// client cannot change them.
///
/// Questions are only asked, when the client declared form elicitation
/// (isSupported); without it the caller returns its fallback text (the
/// candidates and the parameter to pass). Choices are strings with titles
/// (oneOf const / title, "enum" + "enumNames" for 2025-06-18). Passwords and
/// permissions are never asked here (they stay in the window of PDF4QT).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpElicitation
{
public:
    PDFAppMcpElicitation() = delete;

    /// Keys of the questions of package B (the property of the answer is the
    /// argument it replaces: "documentId", "page")
    static QString getDocumentKey() { return QStringLiteral("pdf4qt.document"); }
    static QString getPageKey() { return QStringLiteral("pdf4qt.page"); }

    /// Prefix of an answer of an earlier round in the allowed values
    static QString getCarriedPrefix() { return QStringLiteral("carried:"); }

    /// Most options of one question (more are not offered - the fallback text
    /// is returned instead)
    static constexpr int MAXIMUM_OPTIONS = 50;

    /// Longest title of an option
    static constexpr int MAXIMUM_TITLE_LENGTH = 200;

    /// The client of the request declared form elicitation and the request is a
    /// tools/call or a prompts/get (questions are never asked in other requests)
    static bool isSupported(const PDFMcpCallRequest& request);

    /// Allowed values of the verified request state ({} without a retry)
    static QJsonObject getAllowedValues(const PDFMcpCallRequest& request);

    /// Answer of the question \p key (its value is the property \p property of
    /// the content); a value of an earlier round carried in the state is Accepted
    static PDFAppMcpElicitationAnswer getAnswer(const PDFMcpCallRequest& request, const QString& key, const QString& property);

    /// New input requests of the call, which carry every accepted answer of the
    /// request (the answers of the earlier rounds stay valid in the next round)
    static PDFMcpInputRequests createRequests(const PDFMcpCallRequest& request);

    /// Adds a single choice question: \p message for the user, the property
    /// \p property with the \p options (at most MAXIMUM_OPTIONS)
    static void addChoice(const PDFMcpCallRequest& request, PDFMcpInputRequests& requests, const QString& key, const QString& message,
                          const QString& property, const QString& propertyTitle, const std::vector<PDFAppMcpElicitationOption>& options);

    /// The requested schema of a single choice (a flat object of one string property)
    static QByteArray createChoiceSchema(PDFMcpEra era, const QString& property, const QString& propertyTitle, const std::vector<PDFAppMcpElicitationOption>& options);

    /// Text of an execution error, when the user declined or canceled the question
    static QString getDeclinedText(const QString& what, const QString& fallback);
};

}   // namespace pdf

#endif // PDFAPPMCPELICITATION_H
