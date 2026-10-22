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

#include "pdfappdocumentpanels_layers.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"

#include "pdfcatalog.h"
#include "pdffluentwidgets.h"

#include <QEvent>
#include <QVBoxLayout>

namespace pdf
{

PDFAppLayersModel::PDFAppLayersModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppLayersModel::~PDFAppLayersModel()
{
    if (m_activity)
    {
        m_activity->disconnect(this);
    }
}

void PDFAppLayersModel::setLayers(PDFDocumentPointer document, PDFOptionalContentActivity* activity)
{
    if (m_activity && m_activity != activity)
    {
        m_activity->disconnect(this);
    }

    if (activity && m_activity != activity)
    {
        connect(activity, &PDFOptionalContentActivity::optionalContentGroupStateChanged, this, &PDFAppLayersModel::onStateChanged);
        connect(activity, &QObject::destroyed, this, &PDFAppLayersModel::onActivityDestroyed);
    }

    // The tree of Widgets keeps a plain pointer of the document; this model holds it
    m_documentPointer = document;
    m_activity = activity;
    setActivity(document ? activity : nullptr);
    BaseClass::setDocument(PDFModifiedDocument(document, document ? activity : nullptr));
    update();
}

QVariant PDFAppLayersModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::ToolTipRole)
    {
        return BaseClass::data(index, Qt::DisplayRole);
    }
    return BaseClass::data(index, role);
}

void PDFAppLayersModel::onStateChanged()
{
    // Radio button groups switch other layers off: every row is repainted
    emitDataChanged(QModelIndex());
}

void PDFAppLayersModel::onActivityDestroyed()
{
    // The activity is not whole any more: only the pointers are forgotten
    m_activity = nullptr;
    setActivity(nullptr);
}

void PDFAppLayersModel::emitDataChanged(const QModelIndex& parent)
{
    const int rows = rowCount(parent);
    if (rows <= 0)
    {
        return;
    }

    Q_EMIT dataChanged(index(0, 0, parent), index(rows - 1, 0, parent), { Qt::CheckStateRole });
    for (int row = 0; row < rows; ++row)
    {
        emitDataChanged(index(row, 0, parent));
    }
}

PDFAppLayersPanel::PDFAppLayersPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("layersPanel"));

    // sidebar-content 195:2015: rows from 12 at the sides; PDFTreeView keeps the
    // margins 6 around its rows
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PDFAppPanelWidgets::CONTENT_PADDING_SIDE - 6, 0, PDFAppPanelWidgets::CONTENT_PADDING_SIDE - 6, PDFAppPanelWidgets::CONTENT_PADDING_BOTTOM - 6);
    layout->setSpacing(0);

    m_tree = new PDFTreeView(this);
    m_tree->setObjectName(QStringLiteral("layersTree"));
    m_tree->setRowStyle(PDFTreeView::RowStyle::Outline);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setAccessibleName(tr("Layers"));
    PDFAppPanelWidgets::prepareView(m_tree);
    layout->addWidget(m_tree, 1);

    m_model = new PDFAppLayersModel(this);
    m_tree->setModel(m_model);
}

PDFAppLayersPanel::~PDFAppLayersPanel()
{
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }
}

QString PDFAppLayersPanel::getId() const
{
    return PDFAppDocumentPanelIds::layers();
}

QString PDFAppLayersPanel::getTitle() const
{
    return tr("Layers");
}

QString PDFAppLayersPanel::getSubtitle() const
{
    // 195:1615 "7 layers"
    return m_layerCount > 0 ? (m_layerCount == 1 ? tr("%Ln layer", nullptr, m_layerCount) : tr("%Ln layers", nullptr, m_layerCount)) : QString();
}

QString PDFAppLayersPanel::getIconName() const
{
    return QStringLiteral("layers");
}

int PDFAppLayersPanel::getLayerCount(const PDFDocument* document)
{
    if (!document)
    {
        return 0;
    }

    const PDFOptionalContentProperties* properties = document->getCatalog()->getOptionalContentProperties();
    return properties && properties->isValid() ? int(qMin<size_t>(properties->getAllOptionalContentGroups().size(), 1000000)) : 0;
}

void PDFAppLayersPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }

    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppLayersPanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppLayersPanel::onDocumentDestroyed);
    }
    m_connectedDocument = document;
    rebuild();
}

void PDFAppLayersPanel::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);
    rebuild();
}

void PDFAppLayersPanel::onDocumentDestroyed()
{
    // The document is not whole any more: its activity is gone too
    m_connectedDocument = nullptr;
    if (m_model)
    {
        m_model->setLayers(PDFDocumentPointer(), nullptr);
    }
    m_document.reset();
    m_layerCount = 0;
}

void PDFAppLayersPanel::rebuild()
{
    PDFAppDocument* document = getDocument();
    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    if (pointer == m_document && m_model && m_model->rowCount(QModelIndex()) > 0)
    {
        return;
    }

    m_document = pointer;
    m_layerCount = getLayerCount(pointer.data());
    if (m_model)
    {
        m_model->setLayers(pointer, pointer && document ? document->getOptionalContentActivity() : nullptr);
    }
    if (m_tree)
    {
        m_tree->expandAll();
    }
    Q_EMIT titleChanged();
}

void PDFAppLayersPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        if (m_tree)
        {
            m_tree->setAccessibleName(tr("Layers"));
        }
        Q_EMIT titleChanged();
    }
}

QString PDFAppLayersPanelFactory::getId() const
{
    return PDFAppDocumentPanelIds::layers();
}

int PDFAppLayersPanelFactory::getOrder() const
{
    return 30;
}

QString PDFAppLayersPanelFactory::getTitle() const
{
    return PDFAppLayersPanel::tr("Layers");
}

QString PDFAppLayersPanelFactory::getIconName() const
{
    return QStringLiteral("layers");
}

bool PDFAppLayersPanelFactory::isAvailable(const PDFAppDocument* document) const
{
    return document && PDFAppLayersPanel::getLayerCount(document->getDocument().data()) > 0;
}

PDFAppDocumentPanel* PDFAppLayersPanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppLayersPanel(parent);
}

}   // namespace pdf
