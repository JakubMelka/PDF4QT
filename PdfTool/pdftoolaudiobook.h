//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFTOOLAUDIOBOOK_H
#define PDFTOOLAUDIOBOOK_H

#include "pdftoolabstractapplication.h"

#ifdef Q_OS_WIN

struct ISpVoice;

namespace pdftool
{

class PDFVoiceInfo
{
public:
    PDFVoiceInfo() = default;
    PDFVoiceInfo(std::map<QString, QString> properties, ISpVoice* voice);

    PDFVoiceInfo(const PDFVoiceInfo&) = delete;
    PDFVoiceInfo(PDFVoiceInfo&&) = default;
    PDFVoiceInfo& operator=(const PDFVoiceInfo&) = delete;
    PDFVoiceInfo& operator=(PDFVoiceInfo&&) = default;

    ~PDFVoiceInfo();

    QString getName() const { return getStringValue("Name"); }
    QString getGender() const { return getStringValue("Gender"); }
    QString getAge() const { return getStringValue("Age"); }
    QString getVendor() const { return getStringValue("Vendor"); }
    QString getLanguage() const { return getStringValue("Language"); }
    QString getVersion() const { return getStringValue("Version"); }
    QLocale getLocale() const;

    QString getStringValue(QString key) const;
    ISpVoice* getVoice() const { return m_voice; }

private:
    std::map<QString, QString> m_properties;
    ISpVoice* m_voice = nullptr;
};

using PDFVoiceInfoList = std::vector<PDFVoiceInfo>;

class PDFToolAudioBook : public PDFToolAbstractApplication
{
public:
    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
    virtual Options getOptionsFlags() const override;

private:
    int fillVoices(const PDFToolOptions& options, PDFVoiceInfoList& list, bool fillVoicePointers);
    int showVoiceList(const PDFToolOptions& options);
};

}   // namespace pdftool

#endif

#endif // PDFTOOLAUDIOBOOK_H
