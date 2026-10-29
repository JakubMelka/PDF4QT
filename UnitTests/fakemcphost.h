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

#ifndef FAKEMCPHOST_H
#define FAKEMCPHOST_H

#include "pdfmcpcatalog.h"
#include "pdfmcphost.h"
#include "pdfthreadannotations.h"

#include <QJsonDocument>

#include <functional>
#include <memory>
#include <vector>

namespace pdftest
{

/// Host of the tests of Pdf4QtLibMcp (UnitTestsMcp): it records every call,
/// cancellation and assistant event in the thread of the host; a call is
/// finished by the handler of the test (none = an echo of the arguments in a
/// text block), or left open (setAutoFinish(false)) for tests of progress,
/// cancellation and streams.
class FakeMcpHost : public pdf::PDFMcpHost
{
public:
    using Handler = std::function<void(const std::shared_ptr<pdf::PDFMcpCall>&)>;

    explicit FakeMcpHost(QObject* parent = nullptr) :
        pdf::PDFMcpHost(parent)
    {

    }

    void setHandler(Handler handler) { m_handler = std::move(handler); }
    void setAutoFinish(bool autoFinish) { m_isAutoFinish = autoFinish; }

    virtual void handleCall(const std::shared_ptr<pdf::PDFMcpCall>& call) override
    {
        pdf::PDFThreadAnnotations::happensAfter(call.get());
        calls.push_back(call);
        if (m_handler)
        {
            m_handler(call);
            return;
        }

        if (m_isAutoFinish)
        {
            pdf::PDFMcpContentBuilder builder;
            builder.addText(QString::fromUtf8(QJsonDocument(call->getRequest().arguments).toJson(QJsonDocument::Compact)));
            call->finish(builder.createCallResult(false));
        }
    }

    virtual void callCanceled(const std::shared_ptr<pdf::PDFMcpCall>& call) override
    {
        pdf::PDFThreadAnnotations::happensAfter(call.get());
        canceledCalls.push_back(call);
    }

    virtual void assistantEvent(const pdf::PDFMcpAssistantEvent& event) override
    {
        pdf::PDFThreadAnnotations::happensAfter(static_cast<pdf::PDFMcpHost*>(this));
        events.push_back(event);
    }

    std::vector<std::shared_ptr<pdf::PDFMcpCall>> calls;
    std::vector<std::shared_ptr<pdf::PDFMcpCall>> canceledCalls;
    std::vector<pdf::PDFMcpAssistantEvent> events;

private:
    Handler m_handler;
    bool m_isAutoFinish = true;
};

}   // namespace pdftest

#endif // FAKEMCPHOST_H
