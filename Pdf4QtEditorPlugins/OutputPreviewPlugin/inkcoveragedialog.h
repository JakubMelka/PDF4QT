// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef INKCOVERAGEDIALOG_H
#define INKCOVERAGEDIALOG_H

#include "pdfdocument.h"
#include "pdfdrawwidget.h"
#include "pdftransparencyrenderer.h"

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>
#include <QAbstractItemModel>

namespace Ui
{
class InkCoverageDialog;
}

namespace pdfplugin
{
class InkCoverageStatisticsModel;

using ChannelCoverageInfo = pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo;
using CoverageInfo = std::vector<ChannelCoverageInfo>;

struct InkCoverageResults
{
    std::vector<CoverageInfo> pageInfo;
    CoverageInfo sumInfo;
};

class InkCoverageDialog : public QDialog
{
    Q_OBJECT

private:
    using BaseClass = QDialog;

public:
    explicit InkCoverageDialog(const pdf::PDFDocument* document, pdf::PDFWidget* widget, QWidget* parent);
    virtual ~InkCoverageDialog() override;

    void updateInkCoverage();

    virtual void closeEvent(QCloseEvent* event) override;

    virtual void accept() override;
    virtual void reject() override;

private:
    Ui::InkCoverageDialog* ui;

    static constexpr int RESOLUTION = 1920;

    void onInkCoverageCalculated();
    bool isInkCoverageCalculated() const;

    pdf::PDFInkMapper m_inkMapper;
    const pdf::PDFDocument* m_document;
    pdf::PDFWidget* m_widget;
    InkCoverageStatisticsModel* m_model;

    QFuture<InkCoverageResults> m_future;
    QFutureWatcher<InkCoverageResults>* m_futureWatcher;
};

class InkCoverageStatisticsModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    explicit InkCoverageStatisticsModel(QObject* parent);

    enum EStandardColumn
    {
        StandardColumnPageIndex,
        LastStandardColumn
    };

    enum EChannelColumn
    {
        ChannelColumnColorant,
        ChannelColumnCoverageArea,
        ChannelColumnCoverageRatio,
        LastChannelColumn
    };

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setInkCoverageResults(InkCoverageResults inkCoverageResults);
    int getChannelCount() const;
    EChannelColumn getChannelColumn(int index) const;
    int getChannelIndex(int index) const;

private:
    InkCoverageResults m_inkCoverageResults;
};

}   // namespace pdfplugin

#endif // INKCOVERAGEDIALOG_H
