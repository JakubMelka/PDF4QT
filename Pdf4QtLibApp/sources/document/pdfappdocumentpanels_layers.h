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

#ifndef PDFAPPDOCUMENTPANELS_LAYERS_H
#define PDFAPPDOCUMENTPANELS_LAYERS_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanel.h"

#include "pdfdocument.h"
#include "pdffluentwidgets.h"
#include "pdfitemmodels.h"
#include "pdfoptionalcontent.h"

#include <QPointer>

namespace pdf
{

class PDFTreeView;

/// Layers of the document: the tree of the optional content groups of Widgets
/// (the order of the default configuration, locked groups disabled, check
/// boxes switching PDFOptionalContentActivity of the document - radio button
/// groups switch the others off), repainted, when the activity changes
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLayersModel : public PDFOptionalContentTreeItemModel
{
    Q_OBJECT

private:
    using BaseClass = PDFOptionalContentTreeItemModel;

public:
    explicit PDFAppLayersModel(QObject* parent);
    virtual ~PDFAppLayersModel() override;

    /// Sets the document and its activity (nullptr = none)
    void setLayers(PDFDocumentPointer document, PDFOptionalContentActivity* activity);

    virtual QVariant data(const QModelIndex& index, int role) const override;

private:
    void onStateChanged();
    void onActivityDestroyed();
    void emitDataChanged(const QModelIndex& parent);

    PDFDocumentPointer m_documentPointer;
    QPointer<PDFOptionalContentActivity> m_activity;
};

/// Layers panel (pdf4qt-document-layers 195:1615): "Layers / 7 layers" and the tree
/// of the optional content groups with their check boxes (PDFTreeView, preset
/// Outline). The tab is shown for a document with optional content groups.
/// Switching a layer changes only the view (no change of the document, no undo).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLayersPanel : public PDFAppDocumentPanel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPanel;

public:
    explicit PDFAppLayersPanel(QWidget* parent);
    virtual ~PDFAppLayersPanel() override;

    virtual QString getId() const override;
    virtual QString getTitle() const override;
    virtual QString getSubtitle() const override;
    virtual QString getIconName() const override;
    virtual void setDocument(PDFAppDocument* document, PDFAppDocumentView* view) override;

    /// Number of the optional content groups
    int getLayerCount() const { return m_layerCount; }

    PDFTreeView* getTreeView() const { return m_tree; }
    PDFAppLayersModel* getModel() const { return m_model; }

    /// The document has optional content groups
    static int getLayerCount(const PDFDocument* document);

protected:
    virtual void changeEvent(QEvent* event) override;

private:
    void onDocumentChanged(const PDFModifiedDocument& modified);
    void onDocumentDestroyed();
    void rebuild();

    QPointer<PDFTreeView> m_tree;
    QPointer<PDFAppLayersModel> m_model;
    QPointer<PDFAppDocument> m_connectedDocument;
    PDFDocumentPointer m_document;
    int m_layerCount = 0;
};

/// Factory of the layers panel
class PDF4QTLIBAPPSHARED_EXPORT PDFAppLayersPanelFactory : public PDFAppDocumentPanelFactory
{
public:
    virtual QString getId() const override;
    virtual int getOrder() const override;
    virtual QString getTitle() const override;
    virtual QString getIconName() const override;
    virtual bool isAvailable(const PDFAppDocument* document) const override;
    virtual PDFAppDocumentPanel* createPanel(QWidget* parent) const override;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTPANELS_LAYERS_H
