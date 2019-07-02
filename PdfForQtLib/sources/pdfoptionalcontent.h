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


#ifndef PDFOPTIONALCONTENT_H
#define PDFOPTIONALCONTENT_H

#include "pdfobject.h"

namespace pdf
{

class PDFDocument;
class PDFOptionalContentProperties;
class PDFOptionalContentConfiguration;

/// State of the optional content group, or result of expression
enum class OCState
{
    ON,
    OFF,
    Unknown
};

/// Type of optional content usage
enum class OCUsage
{
    View,
    Print,
    Export,
    Invalid
};

constexpr OCState operator &(OCState left, OCState right)
{
    if (left == OCState::Unknown)
    {
        return right;
    }
    if (right == OCState::Unknown)
    {
        return left;
    }

    return (left == OCState::ON && right == OCState::ON) ? OCState::ON : OCState::OFF;
}

constexpr OCState operator |(OCState left, OCState right)
{
    if (left == OCState::Unknown)
    {
        return right;
    }
    if (right == OCState::Unknown)
    {
        return left;
    }

    return (left == OCState::ON || right == OCState::ON) ? OCState::ON : OCState::OFF;
}

/// Activeness of the optional content
class PDFFORQTLIBSHARED_EXPORT PDFOptionalContentActivity : public QObject
{
    Q_OBJECT

public:
    explicit PDFOptionalContentActivity(const PDFDocument* document, OCUsage usage, QObject* parent);

    /// Gets the optional content groups state. If optional content group doesn't exist,
    /// then it returns Unknown state.
    /// \param ocg Optional conteng group
    OCState getState(PDFObjectReference ocg) const;

    /// Sets the state of optional content group. If optional content group doesn't exist,
    /// then nothing happens. If optional content group is contained in radio button group, then
    /// all other optional content groups in the group are switched off, if we are
    /// switching this one to ON state. If we are switching it off, then nothing happens (as all
    /// optional content groups in radio button group can be switched off).
    /// \param ocg Optional content group
    /// \param state New state of the optional content group
    /// \note If something changed, then signalp \p optionalContentGroupStateChanged is emitted.
    void setState(PDFObjectReference ocg, OCState state);

    /// Applies configuration to the current state of optional content groups
    void applyConfiguration(const PDFOptionalContentConfiguration& configuration);

signals:
    void optionalContentGroupStateChanged(PDFObjectReference ocg, OCState state);

private:
    const PDFDocument* m_document;
    const PDFOptionalContentProperties* m_properties;
    OCUsage m_usage;
    std::map<PDFObjectReference, OCState> m_states;
};

/// Configuration of optional content configuration.
class PDFOptionalContentConfiguration
{
public:

    enum class BaseState
    {
        ON,
        OFF,
        Unchanged
    };

    enum class ListMode
    {
        AllPages,
        VisiblePages
    };

    struct UsageApplication
    {
        QByteArray event;
        std::vector<PDFObjectReference> optionalContengGroups;
        std::vector<QByteArray> categories;
    };

    /// Creates new optional content properties configuration from the object. If object is not valid,
    /// then exception is thrown.
    /// \param document Document
    /// \param object Object containing documents optional content configuration
    static PDFOptionalContentConfiguration create(const PDFDocument* document, const PDFObject& object);

    /// Converts usage name to the enum. If value can't be converted, then Invalid usage is returned.
    /// \param name Name of the usage
    static OCUsage getUsageFromName(const QByteArray& name);

    const QString& getName() const { return m_name; }
    const QString& getCreator() const { return m_creator; }
    BaseState getBaseState() const { return m_baseState; }
    const std::vector<PDFObjectReference>& getOnArray() const { return m_OnArray; }
    const std::vector<PDFObjectReference>& getOffArray() const { return m_OffArray; }
    const std::vector<QByteArray>& getIntents() const { return m_intents; }
    const std::vector<UsageApplication>& getUsageApplications() const { return m_usageApplications; }
    const PDFObject& getOrder() const { return m_order; }
    ListMode getListMode() const { return m_listMode; }
    const std::vector<std::vector<PDFObjectReference>>& getRadioButtonGroups() const { return m_radioButtonGroups; }
    const std::vector<PDFObjectReference>& getLocked() const { return m_locked; }

private:
    /// Creates usage application
    /// \param document Document
    /// \param object Object containing usage application
    static UsageApplication createUsageApplication(const PDFDocument* document, const PDFObject& object);

    QString m_name;
    QString m_creator;
    BaseState m_baseState = BaseState::ON;
    std::vector<PDFObjectReference> m_OnArray;
    std::vector<PDFObjectReference> m_OffArray;
    std::vector<QByteArray> m_intents;
    std::vector<UsageApplication> m_usageApplications;
    PDFObject m_order;
    ListMode m_listMode = ListMode::AllPages;
    std::vector<std::vector<PDFObjectReference>> m_radioButtonGroups;
    std::vector<PDFObjectReference> m_locked;
};

/// Class reprezenting optional content group - it contains properties of the group,
/// such as name, usage etc.
class PDFOptionalContentGroup
{
public:
    explicit PDFOptionalContentGroup();

    /// Creates optional content group from the object. Object must be valid optional
    /// content group, if it is invalid, then exception is thrown.
    /// \param document Document
    /// \param object Object containing optional content group
    static PDFOptionalContentGroup create(const PDFDocument* document, const PDFObject& object);

    PDFObjectReference getReference() const { return m_reference; }
    const QString& getName() const { return m_name; }
    const std::vector<QByteArray>& getIntents() const { return m_intents; }
    PDFObject getCreatorInfo() const { return m_creatorInfo; }
    PDFObject getLanguage() const { return m_language; }
    PDFReal getUsageZoomMin() const { return m_usageZoomMin; }
    PDFReal getUsageZoomMax() const { return m_usageZoomMax; }
    OCState getUsagePrintState() const { return m_usagePrintState; }
    OCState getUsageViewState() const { return m_usageViewState; }
    OCState getUsageExportState() const { return m_usageExportState; }
    OCState getUsageState(OCUsage usage) const;

private:
    PDFObjectReference m_reference;
    QString m_name;
    std::vector<QByteArray> m_intents;
    PDFObject m_creatorInfo;
    PDFObject m_language;
    PDFReal m_usageZoomMin;
    PDFReal m_usageZoomMax;
    OCState m_usagePrintState;
    OCState m_usageViewState;
    OCState m_usageExportState;
};

/// Object containing properties of the optional content of the PDF document. It contains
/// for example all documents optional content groups.
class PDFOptionalContentProperties
{
public:
    explicit PDFOptionalContentProperties() = default;

    /// Returns, if object is valid - at least one optional content group exists
    bool isValid() const { return !m_allOptionalContentGroups.empty() && m_allOptionalContentGroups.size() == m_optionalContentGroups.size(); }

    /// Creates new optional content properties from the object. If object is not valid,
    /// then exception is thrown.
    /// \param document Document
    /// \param object Object containing documents optional content properties
    static PDFOptionalContentProperties create(const PDFDocument* document, const PDFObject& object);

    const std::vector<PDFObjectReference>& getAllOptionalContentGroups() const { return m_allOptionalContentGroups; }
    const PDFOptionalContentConfiguration& getDefaultConfiguration() const { return m_defaultConfiguration; }
    const PDFOptionalContentGroup& getOptionalContentGroup(PDFObjectReference reference) const { return m_optionalContentGroups.at(reference); }

    /// Returns true, if optional content group exists
    bool hasOptionalContentGroup(PDFObjectReference reference) const { return m_optionalContentGroups.count(reference); }

private:
    std::vector<PDFObjectReference> m_allOptionalContentGroups;
    PDFOptionalContentConfiguration m_defaultConfiguration;
    std::vector<PDFOptionalContentConfiguration> m_configurations;
    std::map<PDFObjectReference, PDFOptionalContentGroup> m_optionalContentGroups;
};

}   // namespace pdf

#endif // PDFOPTIONALCONTENT_H
