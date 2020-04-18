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

#include "pdfadvancedfindwidget.h"
#include "ui_pdfadvancedfindwidget.h"

#include "pdfcompiler.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"

#include <QMessageBox>

namespace pdfviewer
{

PDFAdvancedFindWidget::PDFAdvancedFindWidget(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::PDFAdvancedFindWidget),
    m_proxy(proxy),
    m_document(nullptr)
{
    ui->setupUi(this);

    ui->resultsTableWidget->setHorizontalHeaderLabels({ tr("Page No."), tr("Phrase"), tr("Context") });

    connect(ui->regularExpressionsCheckbox, &QCheckBox::clicked, this, &PDFAdvancedFindWidget::updateUI);
    connect(m_proxy, &pdf::PDFDrawWidgetProxy::textLayoutChanged, this, &PDFAdvancedFindWidget::performSearch);
    connect(ui->resultsTableWidget, &QTableWidget::cellDoubleClicked, this, &PDFAdvancedFindWidget::onResultItemDoubleClicked);
    connect(ui->resultsTableWidget, &QTableWidget::itemSelectionChanged, this, &PDFAdvancedFindWidget::onSelectionChanged);
    updateUI();
}

PDFAdvancedFindWidget::~PDFAdvancedFindWidget()
{
    delete ui;
}

void PDFAdvancedFindWidget::setDocument(const pdf::PDFDocument* document)
{
    if (m_document != document)
    {
        m_document = document;
        m_findResults.clear();
        updateUI();
        updateResultsUI();
    }
}

void PDFAdvancedFindWidget::on_searchButton_clicked()
{
    m_parameters.phrase = ui->searchPhraseEdit->text();
    m_parameters.isCaseSensitive = ui->caseSensitiveCheckBox->isChecked();
    m_parameters.isWholeWordsOnly = ui->wholeWordsOnlyCheckBox->isChecked();
    m_parameters.isRegularExpression = ui->regularExpressionsCheckbox->isChecked();
    m_parameters.isDotMatchingEverything = ui->dotMatchesEverythingCheckBox->isChecked();
    m_parameters.isMultiline = ui->multilineMatchingCheckBox->isChecked();
    m_parameters.isSoftHyphenRemoved = ui->removeSoftHyphenCheckBox->isChecked();
    m_parameters.isSearchFinished = m_parameters.phrase.isEmpty();

    if (m_parameters.isSearchFinished)
    {
        // We have nothing to search for
        return;
    }

    // Validate regular expression
    if (m_parameters.isRegularExpression)
    {
        QRegularExpression expression(m_parameters.phrase);
        if (!expression.isValid())
        {
            m_parameters.isSearchFinished = true;
            const int patternErrorOffset = expression.patternErrorOffset();
            QMessageBox::critical(this, tr("Search error"), tr("Search phrase regular expression has error '%1' near symbol %2.").arg(expression.errorString()).arg(patternErrorOffset));
            ui->searchPhraseEdit->setFocus();
            ui->searchPhraseEdit->setSelection(patternErrorOffset, 1);
            return;
        }
    }

    m_findResults.clear();
    m_textSelection.dirty();
    updateResultsUI();

    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    if (compiler->isTextLayoutReady())
    {
        performSearch();
    }
    else
    {
        compiler->makeTextLayout();
    }
}

void PDFAdvancedFindWidget::on_clearButton_clicked()
{
    m_parameters = SearchParameters();
    m_findResults.clear();
    updateResultsUI();
}

void PDFAdvancedFindWidget::onSelectionChanged()
{
    m_textSelection.dirty();
    m_proxy->repaintNeeded();
}

void PDFAdvancedFindWidget::onResultItemDoubleClicked(int row, int column)
{
    Q_UNUSED(column);

    if (row >= 0 && row < m_findResults.size())
    {
        const pdf::PDFFindResult& findResult = m_findResults[row];
        const pdf::PDFInteger pageIndex = findResult.textSelectionItems.front().first.pageIndex;
        m_proxy->goToPage(pageIndex);
    }
}

void PDFAdvancedFindWidget::updateUI()
{
    const bool enableUI = m_document && m_document->getCatalog()->getPageCount() > 0;
    const bool enableRegularExpressionUI = enableUI && ui->regularExpressionsCheckbox->isChecked();
    ui->searchForGroupBox->setEnabled(enableUI);
    ui->regularExpressionSettingsGroupBox->setEnabled(enableRegularExpressionUI);
}

void PDFAdvancedFindWidget::updateResultsUI()
{
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->resultsTab), !m_findResults.empty() ? tr("Results (%1)").arg(m_findResults.size()) : tr("Results"));
    ui->resultsTableWidget->setRowCount(static_cast<int>(m_findResults.size()));

    for (int i = 0, rowCount = int(m_findResults.size()); i < rowCount; ++i)
    {
        const pdf::PDFFindResult& findResult = m_findResults[i];
        ui->resultsTableWidget->setItem(i, 0, new QTableWidgetItem(QString::number(findResult.textSelectionItems.front().first.pageIndex + 1)));
        ui->resultsTableWidget->setItem(i, 1, new QTableWidgetItem(findResult.matched));
        ui->resultsTableWidget->setItem(i, 2, new QTableWidgetItem(findResult.context));
    }

    if (!m_findResults.empty())
    {
        ui->tabWidget->setCurrentWidget(ui->resultsTab);
    }
}

void PDFAdvancedFindWidget::drawPage(QPainter* painter,
                                     pdf::PDFInteger pageIndex,
                                     const pdf::PDFPrecompiledPage* compiledPage,
                                     pdf::PDFTextLayoutGetter& layoutGetter,
                                     const QMatrix& pagePointToDevicePointMatrix,
                                     QList<pdf::PDFRenderError>& errors) const
{
    Q_UNUSED(compiledPage);
    Q_UNUSED(errors);

    const pdf::PDFTextSelection& textSelection = getTextSelection();
    pdf::PDFTextSelectionPainter textSelectionPainter(&textSelection);
    textSelectionPainter.draw(painter, pageIndex, layoutGetter, pagePointToDevicePointMatrix);
}

void PDFAdvancedFindWidget::performSearch()
{
    if (m_parameters.isSearchFinished || m_parameters.phrase.isEmpty())
    {
        return;
    }

    m_parameters.isSearchFinished = true;

    pdf::PDFAsynchronousTextLayoutCompiler* compiler = m_proxy->getTextLayoutCompiler();
    if (!compiler->isTextLayoutReady())
    {
        // Text layout is not ready yet
        return;
    }

    // Prepare string to search
    bool useRegularExpression = m_parameters.isRegularExpression;
    QString expression = m_parameters.phrase;

    if (m_parameters.isWholeWordsOnly)
    {
        if (useRegularExpression)
        {
            expression = QString("\\b%1\\b").arg(expression);
        }
        else
        {
            expression = QString("\\b%1\\b").arg(QRegularExpression::escape(expression));
        }
        useRegularExpression = true;
    }

    pdf::PDFTextFlow::FlowFlags flowFlags = pdf::PDFTextFlow::SeparateBlocks;
    if (m_parameters.isSoftHyphenRemoved)
    {
        flowFlags |= pdf::PDFTextFlow::RemoveSoftHyphen;
    }
    if (m_parameters.isRegularExpression)
    {
        flowFlags |= pdf::PDFTextFlow::AddLineBreaks;
    }

    const pdf::PDFTextLayoutStorage* textLayoutStorage = compiler->getTextLayoutStorage();
    if (!useRegularExpression)
    {
        // Use simple text search
        Qt::CaseSensitivity caseSensitivity = m_parameters.isCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        m_findResults = textLayoutStorage->find(expression, caseSensitivity, flowFlags);
    }
    else
    {
        // Use regular expression search
        QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::OptimizeOnFirstUsageOption;
        if (!m_parameters.isCaseSensitive)
        {
            patternOptions |= QRegularExpression::CaseInsensitiveOption;
        }
        if (m_parameters.isDotMatchingEverything)
        {
            patternOptions |= QRegularExpression::DotMatchesEverythingOption;
        }
        if (m_parameters.isMultiline)
        {
            patternOptions |= QRegularExpression::MultilineOption;
        }

        QRegularExpression regularExpression(expression, patternOptions);
        m_findResults = textLayoutStorage->find(regularExpression, flowFlags);
    }

    m_textSelection.dirty();
    m_proxy->repaintNeeded();

    updateResultsUI();
}

pdf::PDFTextSelection PDFAdvancedFindWidget::getTextSelectionImpl() const
{
    pdf::PDFTextSelection result;

    std::vector<size_t> selectedRowIndices;
    QModelIndexList selectedRows = ui->resultsTableWidget->selectionModel()->selectedRows();
    std::transform(selectedRows.cbegin(), selectedRows.cend(), std::back_inserter(selectedRowIndices), [] (const QModelIndex& index) { return index.row(); });
    std::sort(selectedRowIndices.begin(), selectedRowIndices.end());

    for (size_t i = 0; i < m_findResults.size(); ++i)
    {
        const pdf::PDFFindResult& findResult = m_findResults[i];

        QColor color(Qt::blue);
        if (std::binary_search(selectedRowIndices.cbegin(), selectedRowIndices.cend(), i))
        {
            color = QColor(Qt::yellow);
        }

        result.addItems(findResult.textSelectionItems, color);
    }
    result.build();

    return result;
}

void PDFAdvancedFindWidget::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);
    m_proxy->registerDrawInterface(this);
}

void PDFAdvancedFindWidget::hideEvent(QHideEvent* event)
{
    m_proxy->unregisterDrawInterface(this);
    BaseClass::hideEvent(event);
}

}   // namespace pdfviewer
