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
#include <QReadWriteLock>

#pragma warning(push)
#pragma warning(disable:5033)
#include <lcms2.h>
#pragma warning(pop)

#include <unordered_map>

namespace pdf
{

class PDFLittleCMS : public PDFCMS
{
public:
    explicit PDFLittleCMS(const PDFCMSManager* manager, const PDFCMSSettings& settings);
    virtual ~PDFLittleCMS() override;

    virtual bool isCompatible(const PDFCMSSettings& settings) const override;
    virtual QColor getPaperColor() const override;
    virtual QColor getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor3& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const override;
    virtual QColor getColorFromICC(const PDFColor& color, RenderingIntent renderingIntent, const QByteArray& iccID, const QByteArray& iccData, PDFRenderErrorReporter* reporter) const override;

private:
    void init();

    enum Profile
    {
        Output,
        Gray,
        RGB,
        CMYK,
        XYZ,
        ProfileCount
    };

    /// Creates a profile using provided id and a list of profile descriptors.
    /// If profile can't be created, then null handle is returned.
    /// \param id Id of color profile
    /// \param profileDescriptors Profile descriptor list
    cmsHPROFILE createProfile(const QString& id, const PDFColorProfileIdentifiers& profileDescriptors) const;

    /// Gets transform from cache. If transform doesn't exist, then it is created.
    /// \param profile Color profile
    /// \param intent Rendering intent
    cmsHTRANSFORM getTransform(Profile profile, RenderingIntent intent) const;

    /// Returns transformation flags accordint to the current settings
    cmsUInt32Number getTransformationFlags() const;

    /// Calculates effective rendering intent. If rendering intent is auto,
    /// then \p intent is used, otherwise intent is overriden.
    RenderingIntent getEffectiveRenderingIntent(RenderingIntent intent) const;

    /// Gets transform from cache key.
    /// \param profile Color profile
    /// \param intent Rendering intent
    static constexpr int getCacheKey(Profile profile, RenderingIntent intent) { return int(intent) * ProfileCount + profile; }

    /// Returns little CMS rendering intent
    /// \param intent Rendering intent
    static cmsUInt32Number getLittleCMSRenderingIntent(RenderingIntent intent);

    /// Returns little CMS data format for profile
    /// \param profile Color profile handle
    static cmsUInt32Number getProfileDataFormat(cmsHPROFILE profile);

    /// Returns color from output color. Clamps invalid rgb output values to range [0.0, 1.0].
    /// \param color01 Rgb color (range 0-1 is assumed).
    static QColor getColorFromOutputColor(std::array<float, 3> color01);

    const PDFCMSManager* m_manager;
    PDFCMSSettings m_settings;
    QColor m_paperColor;
    std::array<cmsHPROFILE, ProfileCount> m_profiles;

    mutable QReadWriteLock m_transformationCacheLock;
    mutable std::unordered_map<int, cmsHTRANSFORM> m_transformationCache;

    mutable QReadWriteLock m_customIccProfileCacheLock;
    mutable std::map<std::pair<QByteArray, RenderingIntent>, cmsHTRANSFORM> m_customIccProfileCache;
};

PDFLittleCMS::PDFLittleCMS(const PDFCMSManager* manager, const PDFCMSSettings& settings) :
    m_manager(manager),
    m_settings(settings),
    m_paperColor(Qt::white),
    m_profiles()
{
    init();
}

PDFLittleCMS::~PDFLittleCMS()
{
    for (const auto& transformItem : m_transformationCache)
    {
        cmsHTRANSFORM transform = transformItem.second;
        if (transform)
        {
            cmsDeleteTransform(transform);
        }
    }

    for (const auto& transformItem : m_customIccProfileCache)
    {
        cmsHTRANSFORM transform = transformItem.second;
        if (transform)
        {
            cmsDeleteTransform(transform);
        }
    }

    for (cmsHPROFILE profile : m_profiles)
    {
        if (profile)
        {
            cmsCloseProfile(profile);
        }
    }
}

bool PDFLittleCMS::isCompatible(const PDFCMSSettings& settings) const
{
    return m_settings == settings;
}

QColor PDFLittleCMS::getPaperColor() const
{
    return m_paperColor;
}

QColor PDFLittleCMS::getColorFromDeviceGray(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    cmsHTRANSFORM transform = getTransform(Gray, getEffectiveRenderingIntent(intent));

    if (!transform)
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from gray to output device using CMS failed."));
        return QColor();
    }

    if (cmsGetTransformInputFormat(transform) == TYPE_GRAY_FLT && color.size() == 1)
    {
        Q_ASSERT(cmsGetTransformOutputFormat(transform) == TYPE_RGB_FLT);

        const float grayColor = color[0];
        std::array<float, 3> rgbOutputColor = { };
        cmsDoTransform(transform, &grayColor, rgbOutputColor.data(), 1);
        return getColorFromOutputColor(rgbOutputColor);
    }
    else
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from gray to output device using CMS failed - invalid data format."));
    }

    return QColor();
}

QColor PDFLittleCMS::getColorFromDeviceRGB(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    cmsHTRANSFORM transform = getTransform(RGB, getEffectiveRenderingIntent(intent));

    if (!transform)
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from RGB to output device using CMS failed."));
        return QColor();
    }

    if (cmsGetTransformInputFormat(transform) == TYPE_RGB_FLT && color.size() == 3)
    {
        Q_ASSERT(cmsGetTransformOutputFormat(transform) == TYPE_RGB_FLT);

        std::array<float, 3> rgbInputColor = { color[0], color[1], color[2] };
        std::array<float, 3> rgbOutputColor = { };
        cmsDoTransform(transform, rgbInputColor.data(), rgbOutputColor.data(), 1);
        return getColorFromOutputColor(rgbOutputColor);
    }
    else
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from RGB to output device using CMS failed - invalid data format."));
    }

    return QColor();
}

QColor PDFLittleCMS::getColorFromDeviceCMYK(const PDFColor& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    cmsHTRANSFORM transform = getTransform(CMYK, getEffectiveRenderingIntent(intent));

    if (!transform)
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from CMYK to output device using CMS failed."));
        return QColor();
    }

    if (cmsGetTransformInputFormat(transform) == TYPE_CMYK_FLT && color.size() == 4)
    {
        Q_ASSERT(cmsGetTransformOutputFormat(transform) == TYPE_RGB_FLT);

        std::array<float, 4> cmykInputColor = { color[0] * 100.0f, color[1] * 100.0f, color[2] * 100.0f, color[3] * 100.0f };
        std::array<float, 3> rgbOutputColor = { };
        cmsDoTransform(transform, cmykInputColor.data(), rgbOutputColor.data(), 1);
        return getColorFromOutputColor(rgbOutputColor);
    }
    else
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from CMYK to output device using CMS failed - invalid data format."));
    }

    return QColor();
}

QColor PDFLittleCMS::getColorFromXYZ(const PDFColor3& whitePoint, const PDFColor3& color, RenderingIntent intent, PDFRenderErrorReporter* reporter) const
{
    cmsHTRANSFORM transform = getTransform(XYZ, getEffectiveRenderingIntent(intent));

    if (!transform)
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from XYZ to output device using CMS failed."));
        return QColor();
    }

    if (cmsGetTransformInputFormat(transform) == TYPE_XYZ_FLT && color.size() == 3)
    {
        Q_ASSERT(cmsGetTransformOutputFormat(transform) == TYPE_RGB_FLT);

        const cmsCIEXYZ* d50WhitePoint = cmsD50_XYZ();
        std::array<float, 3> xyzInputColor = { color[0] / whitePoint[0] * float(d50WhitePoint->X), color[1] / whitePoint[1] * float(d50WhitePoint->Y), color[2] / whitePoint[2] * float(d50WhitePoint->Z) };
        std::array<float, 3> rgbOutputColor = { };
        cmsDoTransform(transform, xyzInputColor.data(), rgbOutputColor.data(), 1);
        return getColorFromOutputColor(rgbOutputColor);
    }
    else
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from XYZ to output device using CMS failed - invalid data format."));
    }

    return QColor();
}

QColor PDFLittleCMS::getColorFromICC(const PDFColor& color, RenderingIntent renderingIntent, const QByteArray& iccID, const QByteArray& iccData, PDFRenderErrorReporter* reporter) const
{
    cmsHTRANSFORM transform = cmsHTRANSFORM();

    {
        RenderingIntent effectiveRenderingIntent = getEffectiveRenderingIntent(renderingIntent);
        const auto key = std::make_pair(iccID, effectiveRenderingIntent);
        QReadLocker lock(&m_customIccProfileCacheLock);
        auto it = m_customIccProfileCache.find(key);
        if (it == m_customIccProfileCache.cend())
        {
            lock.unlock();
            QWriteLocker writeLock(&m_customIccProfileCacheLock);

            // Now, we have locked cache for writing. We must find out,
            // if some other thread doesn't created the transformation already.
            it = m_customIccProfileCache.find(key);
            if (it == m_customIccProfileCache.cend())
            {
                cmsHPROFILE profile = cmsOpenProfileFromMem(iccData.data(), iccData.size());
                if (profile)
                {
                    if (const cmsUInt32Number inputDataFormat = getProfileDataFormat(profile))
                    {
                        cmsUInt32Number lcmsIntent = getLittleCMSRenderingIntent(effectiveRenderingIntent);
                        transform = cmsCreateTransform(profile, inputDataFormat, m_profiles[Output], TYPE_RGB_FLT, lcmsIntent, getTransformationFlags());
                    }
                    cmsCloseProfile(profile);
                }

                it = m_customIccProfileCache.insert(std::make_pair(key, transform)).first;
            }

            transform = it->second;
        }
        else
        {
            transform = it->second;
        }
    }

    if (!transform)
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from icc profile space to output device using CMS failed."));
        return QColor();
    }

    std::array<float, 4> inputBuffer = { };
    const cmsUInt32Number format = cmsGetTransformInputFormat(transform);
    const cmsUInt32Number channels = T_CHANNELS(format);
    const cmsUInt32Number colorSpace = T_COLORSPACE(format);
    const bool isCMYK = colorSpace == PT_CMYK;
    if (channels == color.size() && channels <= inputBuffer.size())
    {
        for (size_t i = 0; i < color.size(); ++i)
        {
            inputBuffer[i] = isCMYK ? color[i] * 100.0 : color[i];
        }

        std::array<float, 3> rgbOutputColor = { };
        cmsDoTransform(transform, inputBuffer.data(), rgbOutputColor.data(), 1);
        return getColorFromOutputColor(rgbOutputColor);
    }
    else
    {
        reporter->reportRenderErrorOnce(RenderErrorType::Error, PDFTranslationContext::tr("Conversion from icc profile space to output device using CMS failed - invalid data format."));
    }

    return QColor();
}

void PDFLittleCMS::init()
{
    // Jakub Melka: initialize all color profiles
    m_profiles[Output] = createProfile(m_settings.outputCS, m_manager->getOutputProfiles());
    m_profiles[Gray] = createProfile(m_settings.deviceGray, m_manager->getGrayProfiles());
    m_profiles[RGB] = createProfile(m_settings.deviceRGB, m_manager->getRGBProfiles());
    m_profiles[CMYK] = createProfile(m_settings.deviceCMYK, m_manager->getCMYKProfiles());
    m_profiles[XYZ] = cmsCreateXYZProfile();

    if (m_settings.isWhitePaperColorTransformed)
    {
        m_paperColor = getColorFromDeviceRGB(PDFColor(1.0f, 1.0f, 1.0f), RenderingIntent::AbsoluteColorimetric, nullptr);

        // We must check color of the paper, it can be invalid, if error occurs...
        if (!m_paperColor.isValid())
        {
            m_paperColor = QColor(Qt::white);
        }
    }

    // 64 should be enough, because we can have 4 input color spaces (gray, RGB, CMYK and XYZ),
    // and 4 rendering intents. We have 4 * 4 = 16 input tables, so 64 will suffice enough
    // (because we then have 25% load factor).
    m_transformationCache.reserve(64);
}

cmsHPROFILE PDFLittleCMS::createProfile(const QString& id, const PDFColorProfileIdentifiers& profileDescriptors) const
{
    auto it = std::find_if(profileDescriptors.cbegin(), profileDescriptors.cend(), [&id](const PDFColorProfileIdentifier& identifier) { return identifier.id == id; });
    if (it != profileDescriptors.cend())
    {
        const PDFColorProfileIdentifier& identifier = *it;
        switch (identifier.type)
        {
            case PDFColorProfileIdentifier::Type::Gray:
            {
                cmsCIExyY whitePoint{ };
                if (cmsWhitePointFromTemp(&whitePoint, identifier.temperature))
                {
                    cmsToneCurve* gammaCurve = cmsBuildGamma(cmsContext(), identifier.gamma);
                    cmsHPROFILE profile = cmsCreateGrayProfile(&whitePoint, gammaCurve);
                    cmsFreeToneCurve(gammaCurve);
                    return profile;
                }
                break;
            }

            case PDFColorProfileIdentifier::Type::sRGB:
                return cmsCreate_sRGBProfile();

            case PDFColorProfileIdentifier::Type::RGB:
            {
                cmsCIExyY whitePoint{ };
                if (cmsWhitePointFromTemp(&whitePoint, identifier.temperature))
                {
                    cmsCIExyYTRIPLE primaries;
                    primaries.Red = { identifier.primaryR.x(), identifier.primaryR.y(), 1.0 };
                    primaries.Green = { identifier.primaryG.x(), identifier.primaryG.y(), 1.0 };
                    primaries.Blue = { identifier.primaryB.x(), identifier.primaryB.y(), 1.0 };
                    cmsToneCurve* gammaCurve = cmsBuildGamma(cmsContext(), identifier.gamma);
                    cmsToneCurve* toneCurves[3] = { gammaCurve, cmsDupToneCurve(gammaCurve), cmsDupToneCurve(gammaCurve) };
                    cmsHPROFILE profile = cmsCreateRGBProfile(&whitePoint, &primaries, toneCurves);
                    cmsFreeToneCurveTriple(toneCurves);
                    return profile;
                }
                break;
            }
            case PDFColorProfileIdentifier::Type::FileGray:
            case PDFColorProfileIdentifier::Type::FileRGB:
            case PDFColorProfileIdentifier::Type::FileCMYK:
            {
                QFile file(identifier.id);
                if (file.open(QFile::ReadOnly))
                {
                    QByteArray fileContent = file.readAll();
                    file.close();

                    return cmsOpenProfileFromMem(fileContent.data(), fileContent.size());
                }

                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    return cmsHPROFILE();
}

cmsHTRANSFORM PDFLittleCMS::getTransform(Profile profile, RenderingIntent intent) const
{
    const int key = getCacheKey(profile, intent);

    QReadLocker lock(&m_transformationCacheLock);
    auto it = m_transformationCache.find(key);
    if (it == m_transformationCache.cend())
    {
        lock.unlock();
        QWriteLocker writeLock(&m_transformationCacheLock);

        // Now, we have locked cache for writing. We must find out,
        // if some other thread doesn't created the transformation already.
        it = m_transformationCache.find(key);
        if (it == m_transformationCache.cend())
        {
            cmsHTRANSFORM transform = cmsHTRANSFORM();
            cmsHPROFILE input = m_profiles[profile];
            cmsHPROFILE output = m_profiles[Output];

            if (input && output)
            {
                transform = cmsCreateTransform(input, getProfileDataFormat(input), output, TYPE_RGB_FLT, getLittleCMSRenderingIntent(intent), getTransformationFlags());
            }

            it = m_transformationCache.insert(std::make_pair(key, transform)).first;
        }

        // We must return it here to avoid race condition (after current block,
        // lock is not locked, because we unlocked lock for reading).
        return it->second;
    }

    return it->second;
}

cmsUInt32Number PDFLittleCMS::getTransformationFlags() const
{
    cmsUInt32Number flags = cmsFLAGS_NOCACHE;

    if (m_settings.isBlackPointCompensationActive)
    {
        flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
    }

    switch (m_settings.accuracy)
    {
        case PDFCMSSettings::Accuracy::Low:
            flags |= cmsFLAGS_LOWRESPRECALC;
            break;

        case PDFCMSSettings::Accuracy::Medium:
            break;

        case PDFCMSSettings::Accuracy::High:
            flags |= cmsFLAGS_HIGHRESPRECALC;
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return flags;
}

RenderingIntent PDFLittleCMS::getEffectiveRenderingIntent(RenderingIntent intent) const
{
    if (m_settings.intent != RenderingIntent::Auto)
    {
        return m_settings.intent;
    }

    return intent;
}

cmsUInt32Number PDFLittleCMS::getLittleCMSRenderingIntent(RenderingIntent intent)
{
    switch (intent)
    {
        case RenderingIntent::Perceptual:
            return INTENT_PERCEPTUAL;

        case RenderingIntent::AbsoluteColorimetric:
            return INTENT_ABSOLUTE_COLORIMETRIC;

        case RenderingIntent::RelativeColorimetric:
            return INTENT_RELATIVE_COLORIMETRIC;

        case RenderingIntent::Saturation:
            return INTENT_SATURATION;

        default:
            Q_ASSERT(false);
            break;
    }

    return INTENT_PERCEPTUAL;
}

cmsUInt32Number PDFLittleCMS::getProfileDataFormat(cmsHPROFILE profile)
{
    cmsColorSpaceSignature signature = cmsGetColorSpace(profile);
    switch (signature)
    {
        case cmsSigGrayData:
            return TYPE_GRAY_FLT;

        case cmsSigRgbData:
            return TYPE_RGB_FLT;

        case cmsSigCmykData:
            return TYPE_CMYK_FLT;

        case cmsSigXYZData:
            return TYPE_XYZ_FLT;

        default:
            break;
    }

    return 0;
}

QColor PDFLittleCMS::getColorFromOutputColor(std::array<float, 3> color01)
{
    QColor color(QColor::Rgb);
    color.setRgbF(qBound(0.0f, color01[0], 1.0f), qBound(0.0f, color01[1], 1.0f), qBound(0.0f, color01[2], 1.0f));
    return color;
}

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

QColor PDFCMSGeneric::getColorFromICC(const PDFColor& color,
                                      RenderingIntent renderingIntent,
                                      const QByteArray& iccID,
                                      const QByteArray& iccData,
                                      PDFRenderErrorReporter* reporter) const
{
    Q_UNUSED(color);
    Q_UNUSED(renderingIntent);
    Q_UNUSED(iccID);
    Q_UNUSED(iccData);
    Q_UNUSED(reporter);
    return QColor();
}

PDFCMSManager::PDFCMSManager(QObject* parent) :
    BaseClass(parent),
    m_mutex(QMutex::Recursive)
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
    switch (m_settings.system)
    {
        case PDFCMSSettings::System::Generic:
            return PDFCMSPointer(new PDFCMSGeneric());

        case PDFCMSSettings::System::LittleCMS2:
            return PDFCMSPointer(new PDFLittleCMS(this, m_settings));

        default:
            Q_ASSERT(false);
            break;
    }

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

