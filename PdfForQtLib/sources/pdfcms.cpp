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

#pragma warning(push)
#pragma warning(disable:5033)
#include <lcms2.h>
#pragma warning(pop)

namespace pdf
{

bool PDFCMSGeneric::isCompatible(const PDFCMSSettings& settings) const
{
    return settings.system == PDFCMSSettings::System::Generic;
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

QColor PDFCMSGeneric::getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
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

const PDFColorSpaceIdentifiers& PDFCMSManager::getOutputProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_outputProfiles.get(this, &PDFCMSManager::getOutputProfilesImpl);
}

const PDFColorSpaceIdentifiers& PDFCMSManager::getGrayProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_grayProfiles.get(this, &PDFCMSManager::getGrayProfilesImpl);
}

const PDFColorSpaceIdentifiers& PDFCMSManager::getRGBProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_RGBProfiles.get(this, &PDFCMSManager::getRGBProfilesImpl);
}

const PDFColorSpaceIdentifiers& PDFCMSManager::getCMYKProfiles() const
{
    QMutexLocker lock(&m_mutex);
    return m_CMYKProfiles.get(this, &PDFCMSManager::getCMYKProfilesImpl);
}

PDFCMSSettings PDFCMSManager::getDefaultSettings() const
{
    PDFCMSSettings settings;

    auto getFirstProfileId = [](const PDFColorSpaceIdentifiers& identifiers)
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

PDFColorSpaceIdentifiers PDFCMSManager::getOutputProfilesImpl() const
{
    // Currently, we only support sRGB output color profile.
    return { PDFColorSpaceIdentifier::createSRGB() };
}

PDFColorSpaceIdentifiers PDFCMSManager::getGrayProfilesImpl() const
{
    // Jakub Melka: We create gray profiles for temperature 5000K, 6500K and 9300K.
    // We also use linear gamma and gamma value 2.2.
    return {
        PDFColorSpaceIdentifier::createGray(tr("Gray D65, γ = 2.2"), "@GENERIC_Gray_D65_g22", 6500.0, 2.2),
        PDFColorSpaceIdentifier::createGray(tr("Gray D50, γ = 2.2"), "@GENERIC_Gray_D50_g22", 5000.0, 2.2),
        PDFColorSpaceIdentifier::createGray(tr("Gray D93, γ = 2.2"), "@GENERIC_Gray_D93_g22", 9300.0, 2.2),
        PDFColorSpaceIdentifier::createGray(tr("Gray D65, γ = 1.0 (linear)"), "@GENERIC_Gray_D65_g10", 6500.0, 1.0),
        PDFColorSpaceIdentifier::createGray(tr("Gray D50, γ = 1.0 (linear)"), "@GENERIC_Gray_D50_g10", 5000.0, 1.0),
        PDFColorSpaceIdentifier::createGray(tr("Gray D93, γ = 1.0 (linear)"), "@GENERIC_Gray_D93_g10", 9300.0, 1.0)
    };
}

PDFColorSpaceIdentifiers PDFCMSManager::getRGBProfilesImpl() const
{
    // Jakub Melka: We create RGB profiles for common standars and also for
    // default standard sRGB. See https://en.wikipedia.org/wiki/Color_spaces_with_RGB_primaries.
    return {
        PDFColorSpaceIdentifier::createSRGB(),
        PDFColorSpaceIdentifier::createRGB(tr("HDTV (ITU-R BT.709)"), "@GENERIC_RGB_HDTV", 6500, QPointF(0.64, 0.33), QPointF(0.30, 0.60), QPointF(0.15, 0.06), 20.0 / 9.0),
        PDFColorSpaceIdentifier::createRGB(tr("Adobe RGB 1998"), "@GENERIC_RGB_Adobe1998", 6500, QPointF(0.64, 0.33), QPointF(0.30, 0.60), QPointF(0.15, 0.06), 563.0 / 256.0),
        PDFColorSpaceIdentifier::createRGB(tr("PAL / SECAM"), "@GENERIC_RGB_PalSecam", 6500, QPointF(0.64, 0.33), QPointF(0.29, 0.60), QPointF(0.15, 0.06), 14.0 / 5.0),
        PDFColorSpaceIdentifier::createRGB(tr("NTSC"), "@GENERIC_RGB_NTSC", 6500, QPointF(0.64, 0.34), QPointF(0.31, 0.595), QPointF(0.155, 0.07), 20.0 / 9.0),
        PDFColorSpaceIdentifier::createRGB(tr("Adobe Wide Gamut RGB"), "@GENERIC_RGB_AdobeWideGamut", 5000, QPointF(0.735, 0.265), QPointF(0.115, 0.826), QPointF(0.157, 0.018), 563.0 / 256.0),
        PDFColorSpaceIdentifier::createRGB(tr("ProPhoto RGB"), "@GENERIC_RGB_ProPhoto", 5000, QPointF(0.7347, 0.2653), QPointF(0.1596, 0.8404), QPointF(0.0366, 0.0001), 9.0 / 5.0)
    };
}

PDFColorSpaceIdentifiers PDFCMSManager::getCMYKProfilesImpl() const
{
    return {

    };
}

PDFColorSpaceIdentifier PDFColorSpaceIdentifier::createGray(QString name, QString id, PDFReal temperature, PDFReal gamma)
{
    PDFColorSpaceIdentifier result;
    result.type = Type::Gray;
    result.name = qMove(name);
    result.id = qMove(id);
    result.temperature = temperature;
    result.gamma = gamma;
    return result;
}

PDFColorSpaceIdentifier PDFColorSpaceIdentifier::createSRGB()
{
    PDFColorSpaceIdentifier result;
    result.type = Type::sRGB;
    result.name = PDFCMSManager::tr("sRGB");
    result.id = "@GENERIC_sRGB";
    return result;
}

PDFColorSpaceIdentifier PDFColorSpaceIdentifier::createRGB(QString name, QString id, PDFReal temperature, QPointF primaryR, QPointF primaryG, QPointF primaryB, PDFReal gamma)
{
    PDFColorSpaceIdentifier result;
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

}   // namespace pdf

