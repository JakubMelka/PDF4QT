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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFOBJECTEDITORWIDGET_IMPL_H
#define PDFOBJECTEDITORWIDGET_IMPL_H

#include "pdfobjecteditormodel.h"

class QLabel;
class QGroupBox;
class QComboBox;
class QTabWidget;
class QGridLayout;
class QLineEdit;
class QTextBrowser;
class QPushButton;

namespace pdf
{

class PDFObjectEditorMappedWidgetAdapter : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFObjectEditorMappedWidgetAdapter(PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    /// Returns PDFObject value currently present in the widget
    virtual PDFObject getValue() const = 0;
    /// Sets PDFObject value to the widget. If data are incompatible,
    /// then no data are set to the widget.
    virtual void setValue(PDFObject object) = 0;

signals:
    void commitRequested(size_t attribute);

protected:
    // Initializes label text with attributes name
    void initLabel(QLabel* label);

    PDFObjectEditorAbstractModel* m_model;
    size_t m_attribute;
};

class PDFObjectEditorMappedComboBoxAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedComboBoxAdapter(QLabel* label, QComboBox* comboBox, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;

private:
    QLabel* m_label;
    QComboBox* m_comboBox;
};

class PDFObjectEditorMappedLineEditAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedLineEditAdapter(QLabel* label, QLineEdit* lineEdit, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;

private:
    QLabel* m_label;
    QLineEdit* m_lineEdit;
};

class PDFObjectEditorMappedTextBrowserAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedTextBrowserAdapter(QLabel* label, QTextBrowser* textBrowser, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;

private:
    QLabel* m_label;
    QTextBrowser* m_textBrowser;
};

class PDFObjectEditorWidgetMapper : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFObjectEditorWidgetMapper(PDFObjectEditorAbstractModel* model, QObject* parent);

    void initialize(QTabWidget* tabWidget);

private:
    struct Subcategory
    {
        QString name;
        std::vector<size_t> attributes;
    };

    struct Category
    {
        QString name;
        std::vector<Subcategory> subcategories;
        QWidget* page = nullptr;

        Subcategory* getOrCreateSubcategory(QString name);
    };

    void createMappedAdapter(QGroupBox* groupBox, QGridLayout* layout, size_t attribute);
    Category* getOrCreateCategory(QString categoryName);

    PDFObjectEditorAbstractModel* m_model;
    std::vector<Category> m_categories;
    std::map<size_t, PDFObjectEditorMappedWidgetAdapter*> m_adapters;
};

class PDFObjectEditorMappedRectangleAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedRectangleAdapter(QLabel* label, QPushButton* pushButton, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;

private:
    QLabel* m_label;
    QPushButton* m_pushButton;
    PDFObject m_rectangle;
};

} // namespace pdf

#endif // PDFOBJECTEDITORWIDGET_IMPL_H
