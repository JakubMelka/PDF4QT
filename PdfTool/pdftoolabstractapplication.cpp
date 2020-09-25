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

#include "pdftoolabstractapplication.h"

namespace pdftool
{

class PDFToolHelpApplication : public PDFToolAbstractApplication
{
public:
    PDFToolHelpApplication() : PDFToolAbstractApplication(true) { }

    virtual QString getStandardString(StandardString standardString) const override;
    virtual int execute(const PDFToolOptions& options) override;
};

static PDFToolHelpApplication s_helpApplication;

QString PDFToolHelpApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "help";

        case Name:
            return PDFToolTranslationContext::tr("Help");

        case Description:
            return PDFToolTranslationContext::tr("Show list of all available commands.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolHelpApplication::execute(const PDFToolOptions& options)
{
    return EXIT_SUCCESS;
}

PDFToolAbstractApplication::PDFToolAbstractApplication(bool isDefault)
{
    PDFToolApplicationStorage::registerApplication(this, isDefault);
}

void PDFToolAbstractApplication::initializeCommandLineParser(QCommandLineParser* parser) const
{

}

PDFToolOptions PDFToolAbstractApplication::getOptions(QCommandLineParser* parser) const
{
    PDFToolOptions options;

    return options;
}

PDFToolAbstractApplication* PDFToolApplicationStorage::getApplicationByCommand(const QString& command)
{
    for (PDFToolAbstractApplication* application : getInstance()->m_applications)
    {
        if (application->getStandardString(PDFToolAbstractApplication::Command) == command)
        {
            return application;
        }
    }

    return nullptr;
}

void PDFToolApplicationStorage::registerApplication(PDFToolAbstractApplication* application, bool isDefault)
{
    PDFToolApplicationStorage* storage = getInstance();
    storage->m_applications.push_back(application);

    if (isDefault)
    {
        storage->m_defaultApplication = application;
    }
}

PDFToolAbstractApplication* PDFToolApplicationStorage::getDefaultApplication()
{
    return getInstance()->m_defaultApplication;
}

PDFToolApplicationStorage* PDFToolApplicationStorage::getInstance()
{
    static PDFToolApplicationStorage storage;
    return &storage;
}

}   // pdftool
