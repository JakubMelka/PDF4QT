#ifndef DIMENSIONSPLUGIN_H
#define DIMENSIONSPLUGIN_H

#include "pdfplugin.h"

#include <QObject>

namespace pdfplugin
{

class DimensionsPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PdfForQt.DimensionsPlugin" FILE "DimensionsPlugin.json")

public:
    DimensionsPlugin();
};

}   // namespace pdfplugin

#endif // DIMENSIONSPLUGIN_H
