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

    connect(ui->regularExpressionsCheckbox, &QCheckBox::clicked, this, &PDFAdvancedFindWidget::updateUI);
    connect(m_proxy, &pdf::PDFDrawWidgetProxy::textLayoutChanged, this, &PDFAdvancedFindWidget::performSearch);
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
        updateUI();
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

void PDFAdvancedFindWidget::updateUI()
{
    const bool enableUI = m_document && m_document->getCatalog()->getPageCount() > 0;
    const bool enableRegularExpressionUI = enableUI && ui->regularExpressionsCheckbox->isChecked();
    ui->searchForGroupBox->setEnabled(enableUI);
    ui->regularExpressionSettingsGroupBox->setEnabled(enableRegularExpressionUI);
}

void PDFAdvancedFindWidget::performSearch()
{
    if (m_parameters.isSearchFinished)
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


}

}   // namespace pdfviewer
