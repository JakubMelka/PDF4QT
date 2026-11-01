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

#include "pdfappvaluestore.h"
#include "pdfappcontext.h"
#include "pdfsettingsrepository.h"

namespace pdf
{

PDFAppValueStore::~PDFAppValueStore() = default;

PDFAppSettingsValueStore::PDFAppSettingsValueStore(PDFAppContext* context) :
    m_context(context)
{

}

PDFAppSettingsValueStore::~PDFAppSettingsValueStore() = default;

bool PDFAppSettingsValueStore::isAvailable() const
{
    return m_context && m_context->getDatabase();
}

QVariant PDFAppSettingsValueStore::readValue(const QString& key) const
{
    return isAvailable() ? m_context->getSettings().getStoredValue(key) : QVariant();
}

QString PDFAppSettingsValueStore::writeValue(const QString& key, const QVariant& value)
{
    if (!isAvailable())
    {
        return QString();
    }

    const PDFDatabaseError error = m_context->getSettings().setValue(key, value);
    return error.isError() ? error.toString() : QString();
}

}   // namespace pdf
