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

#include "pdfobjecteditorwidget.h"
#include "pdfobjecteditorwidget_impl.h"
#include "pdfdocumentbuilder.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSpacerItem>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTextBrowser>
#include <QPushButton>

namespace pdf
{

PDFObjectEditorWidget::PDFObjectEditorWidget(PDFObjectEditorAbstractModel* model, QWidget* parent) :
    BaseClass(parent),
    m_mapper(nullptr),
    m_tabWidget(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    layout->addWidget(m_tabWidget);

    m_mapper = new PDFObjectEditorWidgetMapper(model, this);
    m_mapper->initialize(m_tabWidget);
}

PDFObjectEditorWidgetMapper::PDFObjectEditorWidgetMapper(PDFObjectEditorAbstractModel* model, QObject* parent) :
    BaseClass(parent),
    m_model(model)
{

}

void PDFObjectEditorWidgetMapper::initialize(QTabWidget* tabWidget)
{
    size_t attributeCount = m_model->getAttributeCount();

    for (size_t i = 0; i < attributeCount; ++i)
    {
        // Unmapped attributes are ignored
        if (!m_model->queryAttribute(i, PDFObjectEditorAbstractModel::Question::IsMapped))
        {
            continue;
        }

        QString categoryName = m_model->getAttributeCategory(i);
        QString subcategoryName = m_model->getAttributeSubcategory(i);

        Category* category = getOrCreateCategory(categoryName);
        Subcategory* subcategory = category->getOrCreateSubcategory(subcategoryName);
        subcategory->attributes.push_back(i);
    }

    // Create GUI
    for (Category& category : m_categories)
    {
        category.page = new QWidget(tabWidget);
        tabWidget->addTab(category.page, category.name);
        category.page->setLayout(new QVBoxLayout());

        // Create subcategory GUI
        for (Subcategory& subcategory : category.subcategories)
        {
            QGroupBox* groupBox = new QGroupBox(category.page);
            category.page->layout()->addWidget(groupBox);

            QGridLayout* layout = new QGridLayout();
            groupBox->setLayout(layout);

            for (size_t attribute : subcategory.attributes)
            {
                createMappedAdapter(groupBox, layout, attribute);
            }
        }

        category.page->layout()->addItem(new QSpacerItem(0, 0));
    }
}

void PDFObjectEditorWidgetMapper::createMappedAdapter(QGroupBox* groupBox, QGridLayout* layout, size_t attribute)
{
    auto setAdapter = [this, attribute](PDFObjectEditorMappedWidgetAdapter* adapter)
    {
        Q_ASSERT(!m_adapters.count(attribute));
        m_adapters[attribute] = adapter;
    };

    ObjectEditorAttributeType type = m_model->getAttributeType(attribute);
    switch (type)
    {
        case ObjectEditorAttributeType::Type:
        case ObjectEditorAttributeType::ComboBox:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QComboBox* comboBox = new QComboBox(groupBox);
            layout->addWidget(label, row, 0);
            layout->addWidget(comboBox, row, 1);

            setAdapter(new PDFObjectEditorMappedComboBoxAdapter(label, comboBox, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::TextLine:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QLineEdit* lineEdit = new QLineEdit(groupBox);
            layout->addWidget(label, row, 0);
            layout->addWidget(lineEdit, row, 1);

            setAdapter(new PDFObjectEditorMappedLineEditAdapter(label, lineEdit, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::TextBrowser:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QTextBrowser* textBrowser = new QTextBrowser(groupBox);
            layout->addWidget(label, row, 0, 1, -1);
            layout->addWidget(textBrowser, row + 1, 0, 1, -1);

            setAdapter(new PDFObjectEditorMappedTextBrowserAdapter(label, textBrowser, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::Rectangle:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QPushButton* pushButton = new QPushButton(groupBox);
            pushButton->setFlat(true);

            layout->addWidget(label, row, 0);
            layout->addWidget(pushButton, row, 1);

            setAdapter(new PDFObjectEditorMappedRectangleAdapter(label, pushButton, m_model, attribute, this));
            break;
        }

        default:
            Q_ASSERT(false);
    }
x
    /*
    DateTime,       ///< Date/time
    Flags,          ///< Flags
    Selector,       ///< Selector attribute, it is not persisted
    Color,          ///< Color
    Boolean,        ///< Check box*/
}

PDFObjectEditorWidgetMapper::Category* PDFObjectEditorWidgetMapper::getOrCreateCategory(QString categoryName)
{
    auto categoryIt = std::find_if(m_categories.begin(), m_categories.end(), [&categoryName](const auto& category) { return category.name == categoryName; });
    if (categoryIt != m_categories.end())
    {
        return &*categoryIt;
    }

    Category category;
    category.name = qMove(categoryName);
    m_categories.emplace_back(qMove(category));
    return &m_categories.back();
}

PDFObjectEditorWidgetMapper::Subcategory* PDFObjectEditorWidgetMapper::Category::getOrCreateSubcategory(QString subcategoryName)
{
    auto subcategoryIt = std::find_if(subcategories.begin(), subcategories.end(), [&subcategoryName](const auto& subcategory) { return subcategory.name == subcategoryName; });
    if (subcategoryIt != subcategories.end())
    {
        return &*subcategoryIt;
    }

    Subcategory subcategory;
    subcategory.name = qMove(subcategoryName);
    subcategories.emplace_back(qMove(subcategory));
    return &subcategories.back();
}

PDFObjectEditorMappedWidgetAdapter::PDFObjectEditorMappedWidgetAdapter(PDFObjectEditorAbstractModel* model, size_t attribute, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_attribute(attribute)
{

}

void PDFObjectEditorMappedWidgetAdapter::initLabel(QLabel* label)
{
    label->setText(m_model->getAttributeName(m_attribute));
}

PDFObjectEditorMappedComboBoxAdapter::PDFObjectEditorMappedComboBoxAdapter(QLabel* label,
                                                                           QComboBox* comboBox,
                                                                           PDFObjectEditorAbstractModel* model,
                                                                           size_t attribute,
                                                                           QObject* parent) :
    BaseClass(model, attribute, parent),
    m_label(label),
    m_comboBox(comboBox)
{
    initLabel(label);

    comboBox->clear();
    for (const PDFObjectEditorModelAttributeEnumItem& item : m_model->getAttributeEnumItems(attribute))
    {
        comboBox->addItem(item.name, item.flags);
    }

    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedComboBoxAdapter::getValue() const
{
    QVariant currentData = m_comboBox->currentData();
    if (!currentData.isValid())
    {
        return PDFObject();
    }

    const uint32_t flags = currentData.toUInt();
    for (const PDFObjectEditorModelAttributeEnumItem& item : m_model->getAttributeEnumItems(m_attribute))
    {
        if (item.flags == flags)
        {
            return item.value;
        }
    }

    return PDFObject();
}

void PDFObjectEditorMappedComboBoxAdapter::setValue(PDFObject object)
{
    for (const PDFObjectEditorModelAttributeEnumItem& item : m_model->getAttributeEnumItems(m_attribute))
    {
        if (item.value == object)
        {
            m_comboBox->setCurrentIndex(m_comboBox->findData(int(item.flags)));
            return;
        }
    }

    m_comboBox->setCurrentIndex(-1);
}

PDFObjectEditorMappedLineEditAdapter::PDFObjectEditorMappedLineEditAdapter(QLabel* label,
                                                                           QLineEdit* lineEdit,
                                                                           PDFObjectEditorAbstractModel* model,
                                                                           size_t attribute,
                                                                           QObject* parent) :
    BaseClass(model, attribute, parent),
    m_label(label),
    m_lineEdit(lineEdit)
{
    initLabel(label);
    lineEdit->setClearButtonEnabled(true);

    connect(lineEdit, &QLineEdit::editingFinished, this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedLineEditAdapter::getValue() const
{
    PDFObjectFactory factory;
    factory << m_lineEdit->text();
    return factory.takeObject();
}

void PDFObjectEditorMappedLineEditAdapter::setValue(PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    m_lineEdit->setText(loader.readTextString(object, QString()));
}


PDFObjectEditorMappedTextBrowserAdapter::PDFObjectEditorMappedTextBrowserAdapter(QLabel* label,
                                                                                 QTextBrowser* textBrowser,
                                                                                 PDFObjectEditorAbstractModel* model,
                                                                                 size_t attribute,
                                                                                 QObject* parent) :
    BaseClass(model, attribute, parent),
    m_label(label),
    m_textBrowser(textBrowser)
{
    initLabel(label);
    textBrowser->setUndoRedoEnabled(true);
    textBrowser->setTextInteractionFlags(Qt::TextEditorInteraction);

    connect(textBrowser, &QTextBrowser::textChanged, this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedTextBrowserAdapter::getValue() const
{
    PDFObjectFactory factory;
    factory << m_textBrowser->toPlainText();
    return factory.takeObject();
}

void PDFObjectEditorMappedTextBrowserAdapter::setValue(PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    m_textBrowser->setText(loader.readTextString(object, QString()));
}

PDFObjectEditorMappedRectangleAdapter::PDFObjectEditorMappedRectangleAdapter(QLabel* label,
                                                                             QPushButton* pushButton,
                                                                             PDFObjectEditorAbstractModel* model,
                                                                             size_t attribute,
                                                                             QObject* parent):
    BaseClass(model, attribute, parent),
    m_label(label),
    m_pushButton(pushButton)
{
    initLabel(label);
}

PDFObject PDFObjectEditorMappedRectangleAdapter::getValue() const
{
    return m_rectangle;
}

void PDFObjectEditorMappedRectangleAdapter::setValue(PDFObject object)
{
    m_rectangle = qMove(object);
}

} // namespace pdf
