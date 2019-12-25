//    Copyright (C) 2019 Jakub Melka
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

#include "pdfcms.h"

#include <QApplication>

#pragma warning(push)
#pragma warning(disable:5033)
#include <lcms2.h>
#pragma warning(pop)

namespace pdf
{

QString getInfoFromProfile(cmsHPROFILE profile, cmsInfoType infoType)
{
    QLocale locale;
    QString country = QLocale::countryToString(locale.country());
    QString language = QLocale::languageToString(locale.language());

    char countryCode[3] = { };
    char languageCode[3] = { };
    if (country.size() == 2)
    {
        countryCode[0] = country[0].toLatin1();
        countryCode[1] = country[1].toLatin1();
    }
    if (language.size() == 2)
    {
        languageCode[0] = language[0].toLatin1();
        languageCode[1] = language[1].toLatin1();
    }

    // Jakub Melka: try to get profile info from current language/country.
    // If it fails, then pick any language/any country.
    cmsUInt32Number bufferSize = cmsGetProfileInfo(profile, infoType, languageCode, countryCode, nullptr, 0);
    if (bufferSize)
    {
        std::vector<wchar_t> buffer(bufferSize, 0);
        cmsGetProfileInfo(profile, infoType, languageCode, countryCode, buffer.data(), static_cast<cmsUInt32Number>(buffer.size()));
        return QString::fromWCharArray(buffer.data());
    }

    bufferSize = cmsGetProfileInfo(profile, infoType, cmsNoLanguage, cmsNoCountry, nullptr, 0);
    if (bufferSize)
    {
        std::vector<wchar_t> buffer(bufferSize, 0);
        cmsGetProfileInfo(profile, infoType, cmsNoLanguage, cmsNoCountry, buffer.data(), static_cast<cmsUInt32Number>(buffer.size()));
        return QString::fromWCharArray(buffer.data());
    }

    return QString();
}

bool PDFCMSGeneric::isCompatible(const PDFCMSSettings& settings) const
{
    return settings.system == PDFCMSSettings::System::Generic;
}

QColor PDFCMSGeneric::getPaperColor() const
{
    return QColor(Qt::white);
}

QColor PDFCMSGeneric::getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    Q_UNUSED(color);
    Q_UNUSED(intent);
    Q_UNUSED(reporter);
    return QColor();
}

QColor PDFCMSGeneric::getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    Q_UNUSED(color);
    Q_UNUSED(intent);
    Q_UNUSED(reporter);
    return QColor();
}

QColor PDFCMSGeneric::getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    Q_UNUSED(color);
    Q_UNUSED(intent);
    Q_UNUSED(reporter);
    return QColor();
}

QColor PDFCMSGeneric::getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor3& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    Q_UNUSED(color);
    Q_UNUSED(intent);
    Q_UNUSED(reporter);
    Q_UNUSED(whitePoint);
    return QColor();
}

PDFCMSManager::PDFCMSManager(QObject* parent) :
    BaseClass(parent)
{

}

PDFCMSPointer PDFCMSManager::getCurrentCMS() const
{
    QMutexLocker lock(&m_mutex);
    return m_CMS.get(this, &PDFCMSManager::getCurrentCMSImpl);
}

void PDFCMSManager::setSettings(const PDFCMSSettings& settings)
{
    if (m_settings != settings)
    {
        // We must ensure, that mutex is not locked, while we are
        // sending signal about CMS change.
        {
            QMutexLocker lock(&m_mutex);
            m_settings = settings;
            m_CMS.dirty();
            m_outputProfiles.dirty();
            m_grayProfiles.dirty();
            m_RGBProfiles.dirty();
            m_CMYKProfiles.dirty();
            m_externalProfiles.dirty();
        }

        emit colorManagementSystemChanged();
    }
}

const PDFColorProfileIdentifiers& PDFCMSManager::getOutputProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_outputProfiles.get(this, &PDFCMSManager::getOutputProfilesImpl);
}

const PDFColorProfileIdentifiers& PDFCMSManager::getGrayProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_grayProfiles.get(this, &PDFCMSManager::getGrayProfilesImpl);
}

const PDFColorProfileIdentifiers& PDFCMSManager::getRGBProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_RGBProfiles.get(this, &PDFCMSManager::getRGBProfilesImpl);
}

const PDFColorProfileIdentifiers& PDFCMSManager::getCMYKProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_CMYKProfiles.get(this, &PDFCMSManager::getCMYKProfilesImpl);
}

const PDFColorProfileIdentifiers& PDFCMSManager::getExternalProfiles() const
{
    // Jakub Melka: do not protect this by mutex, this function is private
    // and must be called only from mutex-protected code.
    return m_externalProfiles.get(this, &PDFCMSManager::getExternalProfilesImpl);
}

PDFCMSSettings PDFCMSManager::getDefaultSettings() const
{
    PDFCMSSettings settings;

    auto getFirstProfileId = [](const PDFColorProfileIdentifiers& identifiers)
    {
        if (!identifiers.empty())
        {
            return identifiers.front().id;
        }
        return QString();
    };

    settings.system = PDFCMSSettings::System::LittleCMS2;
    settings.outputCS = getFirstProfileId(getOutputProfiles());
    settings.deviceGray = getFirstProfileId(getGrayProfiles());
    settings.deviceRGB = getFirstProfileId(getRGBProfiles());
    settings.deviceCMYK = getFirstProfileId(getCMYKProfiles());

    return settings;
}

QString PDFCMSManager::getSystemName(PDFCMSSettings::System system)
{
    switch (system)
    {
        case PDFCMSSettings::System::Generic:
            return tr("Generic");

        case PDFCMSSettings::System::LittleCMS2:
        {
            const int major = LCMS_VERSION / 1000;
            const int minor = (LCMS_VERSION % 1000) / 10;
            return tr("Little CMS %1.%2").arg(major).arg(minor);
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return QString();
}

PDFCMSPointer PDFCMSManager::getCurrentCMSImpl() const
{
    return PDFCMSPointer(new PDFCMSGeneric());
}

PDFColorProfileIdentifiers PDFCMSManager::getOutputProfilesImpl() const
{
    // Currently, we only support sRGB output color profile.
    return { PDFColorProfileIdentifier::createSRGB() };
}

PDFColorProfileIdentifiers PDFCMSManager::getGrayProfilesImpl() const
{
    // Jakub Melka: We create gray profiles for temperature 5000K, 6500K and 9300K.
    // We also use linear gamma and gamma value 2.2.
    PDFColorProfileIdentifiers result =
    {
        PDFColorProfileIdentifier::createGray(tr("Gray D65, γ = 2.2"), "@GENERIC_Gray_D65_g22", 6500.0, 2.2),
        PDFColorProfileIdentifier::createGray(tr("Gray D50, γ = 2.2"), "@GENERIC_Gray_D50_g22", 5000.0, 2.2),
        PDFColorProfileIdentifier::createGray(tr("Gray D93, γ = 2.2"), "@GENERIC_Gray_D93_g22", 9300.0, 2.2),
        PDFColorProfileIdentifier::createGray(tr("Gray D65, γ = 1.0 (linear)"), "@GENERIC_Gray_D65_g10", 6500.0, 1.0),
        PDFColorProfileIdentifier::createGray(tr("Gray D50, γ = 1.0 (linear)"), "@GENERIC_Gray_D50_g10", 5000.0, 1.0),
        PDFColorProfileIdentifier::createGray(tr("Gray D93, γ = 1.0 (linear)"), "@GENERIC_Gray_D93_g10", 9300.0, 1.0)
    };

    PDFColorProfileIdentifiers externalRGBProfiles = getFilteredExternalProfiles(PDFColorProfileIdentifier::Type::FileGray);
    result.insert(result.end(), externalRGBProfiles.begin(), externalRGBProfiles.end());
    return result;
}

PDFColorProfileIdentifiers PDFCMSManager::getRGBProfilesImpl() const
{
    // Jakub Melka: We create RGB profiles for common standars and also for
    // default standard sRGB. See https://en.wikipedia.org/wiki/Color_spaces_with_RGB_primaries.
    PDFColorProfileIdentifiers result =
    {
        PDFColorProfileIdentifier::createSRGB(),
        PDFColorProfileIdentifier::createRGB(tr("HDTV (ITU-R BT.709)"), "@GENERIC_RGB_HDTV", 6500, QPointF(0.64, 0.33), QPointF(0.30, 0.60), QPointF(0.15, 0.06), 20.0 / 9.0),
        PDFColorProfileIdentifier::createRGB(tr("Adobe RGB 1998"), "@GENERIC_RGB_Adobe1998", 6500, QPointF(0.64, 0.33), QPointF(0.30, 0.60), QPointF(0.15, 0.06), 563.0 / 256.0),
        PDFColorProfileIdentifier::createRGB(tr("PAL / SECAM"), "@GENERIC_RGB_PalSecam", 6500, QPointF(0.64, 0.33), QPointF(0.29, 0.60), QPointF(0.15, 0.06), 14.0 / 5.0),
        PDFColorProfileIdentifier::createRGB(tr("NTSC"), "@GENERIC_RGB_NTSC", 6500, QPointF(0.64, 0.34), QPointF(0.31, 0.595), QPointF(0.155, 0.07), 20.0 / 9.0),
        PDFColorProfileIdentifier::createRGB(tr("Adobe Wide Gamut RGB"), "@GENERIC_RGB_AdobeWideGamut", 5000, QPointF(0.735, 0.265), QPointF(0.115, 0.826), QPointF(0.157, 0.018), 563.0 / 256.0),
        PDFColorProfileIdentifier::createRGB(tr("ProPhoto RGB"), "@GENERIC_RGB_ProPhoto", 5000, QPointF(0.7347, 0.2653), QPointF(0.1596, 0.8404), QPointF(0.0366, 0.0001), 9.0 / 5.0)
    };

    PDFColorProfileIdentifiers externalRGBProfiles = getFilteredExternalProfiles(PDFColorProfileIdentifier::Type::FileRGB);
    result.insert(result.end(), externalRGBProfiles.begin(), externalRGBProfiles.end());
    return result;
}

PDFColorProfileIdentifiers PDFCMSManager::getCMYKProfilesImpl() const
{
    return getFilteredExternalProfiles(PDFColorProfileIdentifier::Type::FileCMYK);
}

PDFColorProfileIdentifiers PDFCMSManager::getExternalColorProfiles(QString profileDirectory) const
{
    PDFColorProfileIdentifiers result;

    QDir directory(profileDirectory);
    if (!profileDirectory.isEmpty() && directory.exists())
    {
        QStringList iccProfiles = directory.entryList({ "*.icc" }, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::NoSort);
        for (const QString& fileName : iccProfiles)
        {
            QString filePath = directory.absoluteFilePath(fileName);

            // Try to read the profile from the file. If it fails, then do nothing.
            QFile file(filePath);
            if (file.open(QFile::ReadOnly))
            {
                QByteArray content = file.readAll();
                file.close();

                cmsHPROFILE profile = cmsOpenProfileFromMem(content.data(), content.size());
                if (profile)
                {
                    PDFColorProfileIdentifier::Type csiType = PDFColorProfileIdentifier::Type::Invalid;
                    const cmsColorSpaceSignature colorSpace = cmsGetColorSpace(profile);
                    switch (colorSpace)
                    {
                        case cmsSigGrayData:
                            csiType = PDFColorProfileIdentifier::Type::FileGray;
                            break;

                        case cmsSigRgbData:
                            csiType = PDFColorProfileIdentifier::Type::FileRGB;
                            break;

                        case cmsSigCmykData:
                            csiType = PDFColorProfileIdentifier::Type::FileCMYK;
                            break;

                        default:
                            break;
                    }

                    QString description = getInfoFromProfile(profile, cmsInfoDescription);
                    cmsCloseProfile(profile);

                    // If we have a valid profile, then add it
                    if (csiType != PDFColorProfileIdentifier::Type::Invalid)
                    {
                        result.emplace_back(PDFColorProfileIdentifier::createFile(csiType, qMove(description), filePath));
                    }
                }
            }
        }
    }

    return result;
}

PDFColorProfileIdentifiers PDFCMSManager::getExternalProfilesImpl() const
{
    PDFColorProfileIdentifiers result;

    QStringList directories(m_settings.profileDirectory);
    QDir applicationDirectory(QApplication::applicationDirPath());
    if (applicationDirectory.cd("colorprofiles"))
    {
        directories << applicationDirectory.absolutePath();
    }

    for (const QString& directory : directories)
    {
        PDFColorProfileIdentifiers externalProfiles = getExternalColorProfiles(directory);
        result.insert(result.end(), externalProfiles.begin(), externalProfiles.end());
    }

    return result;
}

PDFColorProfileIdentifiers PDFCMSManager::getFilteredExternalProfiles(PDFColorProfileIdentifier::Type type) const
{
    PDFColorProfileIdentifiers result;
    const PDFColorProfileIdentifiers& externalProfiles = getExternalProfiles();
    std::copy_if(externalProfiles.cbegin(), externalProfiles.cend(), std::back_inserter(result), [type](const PDFColorProfileIdentifier& identifier) { return identifier.type == type; });
    return result;
}

PDFColorProfileIdentifier PDFColorProfileIdentifier::createGray(QString name, QString id, PDFReal temperature, PDFReal gamma)
{
    PDFColorProfileIdentifier result;
    result.type = Type::Gray;
    result.name = qMove(name);
    result.id = qMove(id);
    result.temperature = temperature;
    result.gamma = gamma;
    return result;
}

PDFColorProfileIdentifier PDFColorProfileIdentifier::createSRGB()
{
    PDFColorProfileIdentifier result;
    result.type = Type::sRGB;
    result.name = PDFCMSManager::tr("sRGB");
    result.id = "@GENERIC_sRGB";
    return result;
}

PDFColorProfileIdentifier PDFColorProfileIdentifier::createRGB(QString name, QString id, PDFReal temperature, QPointF primaryR, QPointF primaryG, QPointF primaryB, PDFReal gamma)
{
    PDFColorProfileIdentifier result;
    result.type = Type::RGB;
    result.name = qMove(name);
    result.id = qMove(id);
    result.temperature = temperature;
    result.primaryR = primaryR;
    result.primaryG = primaryG;
    result.primaryB = primaryB;
    result.gamma = gamma;
    return result;
}

PDFColorProfileIdentifier PDFColorProfileIdentifier::createFile(Type type, QString name, QString id)
{
    PDFColorProfileIdentifier result;
    result.type = type;
    result.name = qMove(name);
    result.id = qMove(id);
    return result;
}

}   // namespace pdf

