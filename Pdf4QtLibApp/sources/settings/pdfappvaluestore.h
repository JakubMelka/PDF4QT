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

#ifndef PDFAPPVALUESTORE_H
#define PDFAPPVALUESTORE_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QString>
#include <QVariant>

namespace pdf
{

class PDFAppContext;

/// Where a binder of a value (PDFAppSettingControl) reads and writes its value (plan
/// of the first batch of phase 2, chapter 2.5): the settings of the database by
/// default, the parameter model of a tool for the panel of a tool. The binder holds
/// the store by a shared pointer, so a store must survive the deletion of what it
/// writes to in any order.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppValueStore
{
public:
    virtual ~PDFAppValueStore();

    /// The store can be used now; without it the control shows the default
    virtual bool isAvailable() const = 0;

    /// Untrusted stored value of the key (invalid = nothing is stored); the binder
    /// validates it by its definition
    virtual QVariant readValue(const QString& key) const = 0;

    /// Writes the value of the user. \returns the translated reason of a refusal,
    /// empty on success.
    virtual QString writeValue(const QString& key, const QVariant& value) = 0;
};

/// The settings of the database (PDFSettingsRepository), the store of Settings
class PDF4QTLIBAPPSHARED_EXPORT PDFAppSettingsValueStore : public PDFAppValueStore
{
public:
    explicit PDFAppSettingsValueStore(PDFAppContext* context);
    virtual ~PDFAppSettingsValueStore() override;

    virtual bool isAvailable() const override;
    virtual QVariant readValue(const QString& key) const override;
    virtual QString writeValue(const QString& key, const QVariant& value) override;

private:
    QPointer<PDFAppContext> m_context;
};

}   // namespace pdf

#endif // PDFAPPVALUESTORE_H
