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
class QDateTimeEdit;
class QCheckBox;
class QDoubleSpinBox;

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
    /// \param object Value to be set to the widget
    virtual void setValue(PDFObject object) = 0;

    /// Updates widget state (for example, visibility, enabledness and readonly mode)
    virtual void update() = 0;

signals:
    void commitRequested(size_t attribute);

protected:
    // Initializes label text with attributes name
    void initLabel(QLabel* label);

    PDFObjectEditorAbstractModel* m_model;
    size_t m_attribute;
};

class PDFObjectEditorWidgetMapper : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFObjectEditorWidgetMapper(PDFObjectEditorAbstractModel* model, QObject* parent);

    void initialize(QTabWidget* tabWidget);

    void setObject(PDFObject object);
    PDFObject getObject();

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

    void loadWidgets();
    void onEditedObjectChanged();
    void onCommitRequested(size_t attribute);
    void createMappedAdapter(QGroupBox* groupBox, QGridLayout* layout, size_t attribute);
    Category* getOrCreateCategory(QString categoryName);

    /// Returns true, if category is visible
    /// \param category Category
    bool isCategoryVisible(const Category& category) const;

    PDFObjectEditorAbstractModel* m_model;
    QTabWidget* m_tabWidget;
    std::vector<Category> m_categories;
    std::map<size_t, PDFObjectEditorMappedWidgetAdapter*> m_adapters;
    bool m_isCommitingDisabled;
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
    virtual void update() override;

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
    virtual void update() override;

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
    virtual void update() override;

private:
    QLabel* m_label;
    QTextBrowser* m_textBrowser;
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
    virtual void update() override;

private:
    QLabel* m_label;
    QPushButton* m_pushButton;
    PDFObject m_rectangle;
};

class PDFObjectEditorMappedDateTimeAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedDateTimeAdapter(QLabel* label, QDateTimeEdit* dateTimeEdit, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;
    virtual void update() override;

private:
    QLabel* m_label;
    QDateTimeEdit* m_dateTimeEdit;
};

class PDFObjectEditorMappedFlagsAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedFlagsAdapter(std::vector<std::pair<uint32_t, QCheckBox*>> flagCheckBoxes,
                                               PDFObjectEditorAbstractModel* model,
                                               size_t attribute,
                                               QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;
    virtual void update() override;

private:
    std::vector<std::pair<uint32_t, QCheckBox*>> m_flagCheckBoxes;
};

class PDFObjectEditorMappedCheckBoxAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedCheckBoxAdapter(QLabel* label, QCheckBox* checkBox, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;
    virtual void update() override;

private:
    QLabel* m_label;
    QCheckBox* m_checkBox;
};

class PDFObjectEditorMappedColorAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedColorAdapter(QLabel* label, QComboBox* comboBox, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;
    virtual void update() override;

private:
    QIcon getIconForColor(QColor color) const;

    QLabel* m_label;
    QComboBox* m_comboBox;
};

class PDFObjectEditorMappedDoubleAdapter : public PDFObjectEditorMappedWidgetAdapter
{
    Q_OBJECT

private:
    using BaseClass = PDFObjectEditorMappedWidgetAdapter;

public:
    explicit PDFObjectEditorMappedDoubleAdapter(QLabel* label, QDoubleSpinBox* spinBox, PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent);

    virtual PDFObject getValue() const override;
    virtual void setValue(PDFObject object) override;
    virtual void update() override;

private:
    QLabel* m_label;
    QDoubleSpinBox* m_spinBox;
};

} // namespace pdf

#endif // PDFOBJECTEDITORWIDGET_IMPL_H
