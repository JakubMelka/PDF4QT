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

/// Object containing properties of the optional content of the PDF document. It contains
/// for example all documents optional content groups.
class PDFOptionalContentProperties
{
public:
    explicit PDFOptionalContentProperties() = default;

    /// Returns, if object is valid - at least one optional content group exists
    bool isValid() const { return !m_allOptionalContentGroups.empty(); }

    /// Creates new optional content properties from the object. If object is not valid,
    /// then exception is thrown.
    /// \param document Document
    /// \param object Object containing documents optional content properties
    static PDFOptionalContentProperties create(const PDFDocument* document, const PDFObject& object);

private:
    std::vector<PDFObjectReference> m_allOptionalContentGroups;
    PDFOptionalContentConfiguration m_defaultConfiguration;
    std::vector<PDFOptionalContentConfiguration> m_configurations;
};

}   // namespace pdf

#endif // PDFOPTIONALCONTENT_H
