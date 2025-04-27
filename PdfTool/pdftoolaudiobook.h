// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFTOOLAUDIOBOOK_H
#define PDFTOOLAUDIOBOOK_H

#include "pdftoolabstractapplication.h"

#ifdef Q_OS_WIN

struct ISpObjectToken;

namespace pdf
{
class PDFDocumentTextFlow;
}

namespace pdftool
{

class PDFVoiceInfo
{
public:
    PDFVoiceInfo() = default;
    PDFVoiceInfo(std::map<QString, QString> properties, ISpObjectToken* voiceToken);

    PDFVoiceInfo(const PDFVoiceInfo&) = delete;
    PDFVoiceInfo(PDFVoiceInfo&& other);
    PDFVoiceInfo& operator=(const PDFVoiceInfo&) = delete;
    PDFVoiceInfo& operator=(PDFVoiceInfo&&other);

    ~PDFVoiceInfo();

    QString getName() const { return getStringValue("Name"); }
    QString getGender() const { return getStringValue("Gender"); }
    QString getAge() const { return getStringValue("Age"); }
    QString getVendor() const { return getStringValue("Vendor"); }
    QString getLanguage() const { return getStringValue("Language"); }
    QString getVersion() const { return getStringValue("Version"); }
    QLocale getLocale() const;

    QString getStringValue(QString key) const;
    ISpObjectToken* getVoiceToken() const { return m_voiceToken; }

private:
    std::map<QString, QString> m_properties;
    ISpObjectToken* m_voiceToken = nullptr;
};

using PDFVoiceInfoList = std::vector<PDFVoiceInfo>;

class PDFToolAudioBookBase : public PDFToolAbstractApplication
{
public:
    PDFToolAudioBookBase() = default;

protected:
    int fillVoices(const PDFToolOptions& options, PDFVoiceInfoList& list, bool fillVoiceTokenPointers);
    int showVoiceList(const PDFToolOptions& options);
};

class PDFToolAudioBookVoices : public PDFToolAudioBookBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;
};

class PDFToolAudioBook : public PDFToolAudioBookBase
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

private:
    int getDocumentTextFlow(const PDFToolOptions& options, pdf::PDFDocumentTextFlow& flow);
    int createAudioBook(const PDFToolOptions& options, pdf::PDFDocumentTextFlow& flow);
};

}   // namespace pdftool

#endif

#endif // PDFTOOLAUDIOBOOK_H
