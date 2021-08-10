//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

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
