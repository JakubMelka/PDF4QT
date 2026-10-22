// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfappdeveloperoverlay.h"
#include "pdfappcontext.h"
#include "pdfappdocumentview.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfdrawspacecontroller.h"

namespace pdf
{

PDFAppDeveloperOverlay::PDFAppDeveloperOverlay(PDFAppContext* context, PDFAppDocumentView* view, QObject* parent) :
    BaseClass(parent),
    m_context(context),
    m_view(view)
{
    if (m_context && m_context->getNotifier())
    {
        connect(m_context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppDeveloperOverlay::onSettingChanged);
        connect(m_context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppDeveloperOverlay::onEverythingChanged);
    }

    // The view sets the features of its own settings as a whole: the overlays are
    // set again, when the proxy has been changed without them
    if (m_view && m_view->getProxy())
    {
        connect(m_view->getProxy(), &PDFDrawWidgetProxy::pageImageChanged, this, &PDFAppDeveloperOverlay::onPageImageChanged);
    }

    m_requested = readRequestedFeatures();
    apply();
}

PDFAppDeveloperOverlay::~PDFAppDeveloperOverlay() = default;

PDFRenderer::Features PDFAppDeveloperOverlay::getOverlayFeatures()
{
    return PDFRenderer::Features(PDFRenderer::DebugTextBlocks | PDFRenderer::DebugTextLines | PDFRenderer::DisplayTimes);
}

PDFRenderer::Features PDFAppDeveloperOverlay::readRequestedFeatures() const
{
    PDFRenderer::Features features;
    if (!m_context || !m_context->getDatabase())
    {
        return features;
    }

    const PDFSettingsRepository settings = m_context->getSettings();
    if (!settings.getBool(PDFSettingKeys::advancedDeveloperMode()))
    {
        return features;
    }

    features.setFlag(PDFRenderer::DebugTextBlocks, settings.getBool(PDFSettingKeys::advancedShowTextBlocks()));
    features.setFlag(PDFRenderer::DebugTextLines, settings.getBool(PDFSettingKeys::advancedShowTextLines()));
    features.setFlag(PDFRenderer::DisplayTimes, settings.getBool(PDFSettingKeys::advancedShowRenderTimes()));
    return features;
}

void PDFAppDeveloperOverlay::apply()
{
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    if (!proxy || m_isApplying)
    {
        return;
    }

    const PDFRenderer::Features current = proxy->getFeatures();
    const PDFRenderer::Features features = (current & ~getOverlayFeatures()) | m_requested;
    if (features != current)
    {
        // setFeatures emits pageImageChanged (the guard stops the recursion)
        m_isApplying = true;
        proxy->setFeatures(features);
        m_isApplying = false;
    }
}

void PDFAppDeveloperOverlay::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic != PDFSettingsRepository::getTopic())
    {
        return;
    }

    if (key == PDFSettingKeys::advancedDeveloperMode() || key == PDFSettingKeys::advancedShowTextBlocks() ||
        key == PDFSettingKeys::advancedShowTextLines() || key == PDFSettingKeys::advancedShowRenderTimes())
    {
        m_requested = readRequestedFeatures();
        apply();
    }
}

void PDFAppDeveloperOverlay::onEverythingChanged()
{
    m_requested = readRequestedFeatures();
    apply();
}

void PDFAppDeveloperOverlay::onPageImageChanged()
{
    PDFDrawWidgetProxy* proxy = m_view ? m_view->getProxy() : nullptr;
    // The settings are not read here (it comes with every compiled page)
    if (proxy && !m_isApplying && (proxy->getFeatures() & getOverlayFeatures()) != m_requested)
    {
        apply();
    }
}

}   // namespace pdf
