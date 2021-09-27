//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfobjecteditorwidget.h"
#include "pdfobjecteditorwidget_impl.h"
#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfwidgetutils.h"

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
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>

namespace pdf
{

PDFObjectEditorWidget::PDFObjectEditorWidget(EditObjectType type, QWidget* parent) :
    BaseClass(parent),
    m_mapper(nullptr),
    m_tabWidget(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    layout->addWidget(m_tabWidget);

    PDFObjectEditorAbstractModel* model = nullptr;
    switch (type)
    {
        case EditObjectType::Annotation:
            model = new PDFObjectEditorAnnotationsModel(this);
            break;

        default:
            Q_ASSERT(false);
            break;
    }
    Q_ASSERT(model);

    m_mapper = new PDFObjectEditorWidgetMapper(model, this);
    m_mapper->initialize(m_tabWidget);
}

void PDFObjectEditorWidget::setObject(PDFObject object)
{
    m_mapper->setObject(object);
}

PDFObject PDFObjectEditorWidget::getObject()
{
    return m_mapper->getObject();
}

PDFObjectEditorWidgetMapper::PDFObjectEditorWidgetMapper(PDFObjectEditorAbstractModel* model, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_tabWidget(nullptr),
    m_isCommitingDisabled(false)
{
    connect(model, &PDFObjectEditorAbstractModel::editedObjectChanged, this, &PDFObjectEditorWidgetMapper::onEditedObjectChanged);
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
            groupBox->setTitle(subcategory.name);
            category.page->layout()->addWidget(groupBox);

            QGridLayout* layout = new QGridLayout();
            groupBox->setLayout(layout);
            layout->setColumnStretch(0, 1);
            layout->setColumnStretch(1, 2);

            for (size_t attribute : subcategory.attributes)
            {
                createMappedAdapter(groupBox, layout, attribute);
            }
        }

        QSpacerItem* spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::MinimumExpanding);
        category.page->layout()->addItem(spacer);
    }

    m_tabWidget = tabWidget;
}

void PDFObjectEditorWidgetMapper::setObject(PDFObject object)
{
    m_model->setEditedObject(object);
}

PDFObject PDFObjectEditorWidgetMapper::getObject()
{
    return m_model->getEditedObject();
}

void PDFObjectEditorWidgetMapper::loadWidgets()
{
    PDFTemporaryValueChange guard(&m_isCommitingDisabled, true);
    for (auto& adapterItem : m_adapters)
    {
        const size_t attribute = adapterItem.first;
        PDFObjectEditorMappedWidgetAdapter* adapter = adapterItem.second;

        if (m_model->queryAttribute(attribute, PDFObjectEditorAbstractModel::Question::IsSelector))
        {
            adapter->setValue(PDFObject::createBool(m_model->getSelectorValue(attribute)));
        }
        else
        {
            PDFObject object = m_model->getValue(attribute, true);
            if (object.isNull())
            {
                object = m_model->getDefaultValue(attribute);
            }
            adapter->setValue(object);
        }

        adapter->update();
    }

    std::vector<const Category*> insertedCategories;
    insertedCategories.reserve(m_categories.size());
    for (const Category& category : m_categories)
    {
        const bool isVisible = isCategoryVisible(category);
        if (!isVisible)
        {
            m_tabWidget->removeTab(m_tabWidget->indexOf(category.page));
        }
        else
        {
            insertedCategories.push_back(&category);
        }
    }

    for (auto it = insertedCategories.cbegin(); it != insertedCategories.cend(); ++it)
    {
        const Category* category = *it;
        if (m_tabWidget->indexOf(category->page) != -1)
        {
            // Jakub Melka: category is present already in the tab widget
            continue;
        }

        // Find index onto which we will insert the tab page. We want to preserve
        // the order of categories, so we must insert tabs carefully.
        int insertPosition = -1;
        for (auto it2 = std::next(it); it2 != insertedCategories.cend(); ++it2)
        {
            insertPosition = m_tabWidget->indexOf((*it2)->page);
            if (insertPosition != -1)
            {
                break;
            }
        }

        m_tabWidget->insertTab(insertPosition, category->page,  category->name);
    }
}

void PDFObjectEditorWidgetMapper::onEditedObjectChanged()
{
    if (!m_isCommitingDisabled)
    {
        loadWidgets();
    }
}

void PDFObjectEditorWidgetMapper::onCommitRequested(size_t attribute)
{
    if (m_isCommitingDisabled)
    {
        // Jakub Melka: Commit is disabled
        return;
    }

    PDFTemporaryValueChange guard(&m_isCommitingDisabled, true);

    // We will create a new object using several steps. We merge
    // the resulting object with old one (so data, that are not
    // edited, will remain). Steps:
    //   1) If object is selector attribute, and it is turned on,
    //      set default values to selector's attributes and set
    //      selector value.
    //   2) If object is selector attributem and it is turned off,
    //      set null values to selector's attributes and set selector
    //      value.
    //   3) For ordinary attributes, commit new value of the attribute
    //      to the object.
    //   4) Iterate each attribute, and if it doesn't exist, then set
    //      it to null in the object. If it exists, and it is constant,
    //      then add constant to the object.
    //   5) Set final object as new object

    PDFObject object = m_model->getEditedObject();

    // Steps 1) and 2)
    if (m_model->queryAttribute(attribute, PDFObjectEditorAbstractModel::Question::IsSelector))
    {
        const bool isChecked = m_adapters[attribute]->getValue().getBool();
        m_model->setSelectorValue(attribute, isChecked);

        std::vector<size_t> dependentAttributes = m_model->getSelectorDependentAttributes(attribute);
        if (isChecked)
        {
            for (size_t dependentAttribute : dependentAttributes)
            {
                object = m_model->writeAttributeValueToObject(dependentAttribute, object, m_model->getDefaultValue(dependentAttribute));
            }
        }
        else
        {
            for (size_t dependentAttribute : dependentAttributes)
            {
                object = m_model->writeAttributeValueToObject(dependentAttribute, object, PDFObject());
            }
        }
    }
    else
    {
        // Step 3) - normal attribute
        object = m_model->writeAttributeValueToObject(attribute, object, m_adapters[attribute]->getValue());
    }

    // Step 4)
    for (size_t i = 0; i < m_model->getAttributeCount(); ++i)
    {
        if (!m_model->queryAttribute(i, PDFObjectEditorAbstractModel::Question::IsPersisted))
        {
            continue;
        }

        if (!m_model->queryAttribute(i, PDFObjectEditorAbstractModel::Question::HasSimilarAttribute))
        {
            object = m_model->writeAttributeValueToObject(i, object, PDFObject());
        }
        else if (m_model->getAttributeType(i) == ObjectEditorAttributeType::Constant)
        {
            object = m_model->writeAttributeValueToObject(i, object, m_model->getDefaultValue(i));
        }
    }

    // Step 5)
    m_model->setEditedObject(object);

    loadWidgets();
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
            pushButton->setText(tr("Rectangle"));
            pushButton->setFlat(true);

            layout->addWidget(label, row, 0);
            layout->addWidget(pushButton, row, 1);

            setAdapter(new PDFObjectEditorMappedRectangleAdapter(label, pushButton, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::DateTime:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QDateTimeEdit* dateTimeEdit = new QDateTimeEdit(groupBox);

            layout->addWidget(label, row, 0);
            layout->addWidget(dateTimeEdit, row, 1);

            setAdapter(new PDFObjectEditorMappedDateTimeAdapter(label, dateTimeEdit, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::Flags:
        {
            int row = layout->rowCount();
            int column = 0;

            std::vector<std::pair<uint32_t, QCheckBox*>> flagCheckboxes;
            const PDFObjectEditorModelAttributeEnumItems& flagItems = m_model->getAttributeEnumItems(attribute);
            flagCheckboxes.reserve(flagItems.size());

            for (const PDFObjectEditorModelAttributeEnumItem& flagItem : flagItems)
            {
                if (column == 2)
                {
                    row++;
                    column = 0;
                }

                QCheckBox* checkBox = new QCheckBox(flagItem.name, groupBox);
                layout->addWidget(checkBox, row, column);
                flagCheckboxes.emplace_back(flagItem.flags, checkBox);

                ++column;
            }

            setAdapter(new PDFObjectEditorMappedFlagsAdapter(qMove(flagCheckboxes), m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::Selector:
        case ObjectEditorAttributeType::Boolean:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QCheckBox* checkBox = new QCheckBox(groupBox);

            layout->addWidget(label, row, 0);
            layout->addWidget(checkBox, row, 1);

            setAdapter(new PDFObjectEditorMappedCheckBoxAdapter(label, checkBox, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::Color:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QComboBox* comboBox = new QComboBox(groupBox);

            layout->addWidget(label, row, 0);
            layout->addWidget(comboBox, row, 1);

            setAdapter(new PDFObjectEditorMappedColorAdapter(label, comboBox, m_model, attribute, this));
            break;
        }

        case ObjectEditorAttributeType::Double:
        {
            int row = layout->rowCount();

            QLabel* label = new QLabel(groupBox);
            QDoubleSpinBox* spinBox = new QDoubleSpinBox(groupBox);

            QVariant minimumValue = m_model->getMinimumValue(attribute);
            if (minimumValue.isValid())
            {
                spinBox->setMinimum(minimumValue.toDouble());
            }
            QVariant maximumValue = m_model->getMaximumValue(attribute);
            if (maximumValue.isValid())
            {
                spinBox->setMaximum(maximumValue.toDouble());
            }

            layout->addWidget(label, row, 0);
            layout->addWidget(spinBox, row, 1);

            setAdapter(new PDFObjectEditorMappedDoubleAdapter(label, spinBox, m_model, attribute, this));
            break;
        }

        default:
            Q_ASSERT(false);
    }

    connect(m_adapters[attribute], &PDFObjectEditorMappedWidgetAdapter::commitRequested, this, &PDFObjectEditorWidgetMapper::onCommitRequested);
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

bool PDFObjectEditorWidgetMapper::isCategoryVisible(const Category& category) const
{
    for (const Subcategory& subcategory : category.subcategories)
    {
        for (size_t attribute : subcategory.attributes)
        {
            if (m_model->queryAttribute(attribute, PDFObjectEditorAbstractModel::Question::HasAttribute))
            {
                return true;
            }
        }
    }

    return false;
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

void PDFObjectEditorMappedComboBoxAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_comboBox->setHidden(!isVisible);
    m_comboBox->setEnabled(enabled && !readonly);
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

void PDFObjectEditorMappedLineEditAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_lineEdit->setHidden(!isVisible);
    m_lineEdit->setEnabled(enabled);
    m_lineEdit->setReadOnly(readonly);
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
    QString text = loader.readTextString(object, QString());
    if (text != m_textBrowser->toPlainText())
    {
        m_textBrowser->setText(text);
    }
}

void PDFObjectEditorMappedTextBrowserAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_textBrowser->setHidden(!isVisible);
    m_textBrowser->setEnabled(enabled);
    m_textBrowser->setReadOnly(readonly);
}

PDFObjectEditorMappedRectangleAdapter::PDFObjectEditorMappedRectangleAdapter(QLabel* label,
                                                                             QPushButton* pushButton,
                                                                             PDFObjectEditorAbstractModel* model,
                                                                             size_t attribute,
                                                                             QObject* parent) :
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

void PDFObjectEditorMappedRectangleAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_pushButton->setHidden(!isVisible);
    m_pushButton->setEnabled(enabled && !readonly);
}

PDFObjectEditorMappedDateTimeAdapter::PDFObjectEditorMappedDateTimeAdapter(QLabel* label,
                                                                           QDateTimeEdit* dateTimeEdit,
                                                                           PDFObjectEditorAbstractModel* model,
                                                                           size_t attribute,
                                                                           QObject* parent) :
    BaseClass(model, attribute, parent),
    m_label(label),
    m_dateTimeEdit(dateTimeEdit)
{
    initLabel(label);

    connect(dateTimeEdit, &QDateTimeEdit::editingFinished, this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedDateTimeAdapter::getValue() const
{
    QDateTime dateTime = m_dateTimeEdit->dateTime();

    if (dateTime.isValid())
    {
        return PDFObject::createString(PDFEncoding::convertDateTimeToString(dateTime));
    }

    return PDFObject();
}

void PDFObjectEditorMappedDateTimeAdapter::setValue(PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    QByteArray string = loader.readString(object);

    QDateTime dateTime = PDFEncoding::convertToDateTime(string);
    m_dateTimeEdit->setDateTime(dateTime);
}

void PDFObjectEditorMappedDateTimeAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_dateTimeEdit->setHidden(!isVisible);
    m_dateTimeEdit->setEnabled(enabled);
    m_dateTimeEdit->setReadOnly(readonly);
}

PDFObjectEditorMappedFlagsAdapter::PDFObjectEditorMappedFlagsAdapter(std::vector<std::pair<uint32_t, QCheckBox*>> flagCheckBoxes,
                                                                     PDFObjectEditorAbstractModel* model,
                                                                     size_t attribute,
                                                                     QObject* parent) :
    BaseClass(model, attribute, parent),
    m_flagCheckBoxes(qMove(flagCheckBoxes))
{
    for (const auto& item : m_flagCheckBoxes)
    {
        QCheckBox* checkBox = item.second;
        connect(checkBox, &QCheckBox::clicked, this, [this, attribute](){ emit commitRequested(attribute); });
    }
}

PDFObject PDFObjectEditorMappedFlagsAdapter::getValue() const
{
    uint32_t flags = 0;

    for (const auto& item : m_flagCheckBoxes)
    {
        if (item.second->isChecked())
        {
            flags = flags | item.first;
        }
    }

    return PDFObject::createInteger(PDFInteger(flags));
}

void PDFObjectEditorMappedFlagsAdapter::setValue(PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    uint32_t flags = static_cast<uint32_t>(loader.readInteger(object, 0));

    for (const auto& item : m_flagCheckBoxes)
    {
        const bool checked = flags & item.first;
        item.second->setChecked(checked);
    }
}

void PDFObjectEditorMappedFlagsAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool enableCheckbox = enabled && !readonly;
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    for (const auto& item : m_flagCheckBoxes)
    {
        item.second->setHidden(!isVisible);
        item.second->setEnabled(enableCheckbox);
    }
}

PDFObjectEditorMappedCheckBoxAdapter::PDFObjectEditorMappedCheckBoxAdapter(QLabel* label,
                                                                           QCheckBox* checkBox,
                                                                           PDFObjectEditorAbstractModel* model,
                                                                           size_t attribute,
                                                                           QObject* parent) :
    BaseClass(model, attribute, parent),
    m_label(label),
    m_checkBox(checkBox)
{
    initLabel(label);
    connect(checkBox, &QCheckBox::clicked, this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedCheckBoxAdapter::getValue() const
{
    return PDFObject::createBool(m_checkBox->isChecked());
}

void PDFObjectEditorMappedCheckBoxAdapter::setValue(PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    m_checkBox->setChecked(loader.readBoolean(object, false));
}

void PDFObjectEditorMappedCheckBoxAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_checkBox->setHidden(!isVisible);
    m_checkBox->setEnabled(enabled && !readonly);
}

PDFObjectEditorMappedDoubleAdapter::PDFObjectEditorMappedDoubleAdapter(QLabel* label,
                                                                       QDoubleSpinBox* spinBox,
                                                                       PDFObjectEditorAbstractModel* model,
                                                                       size_t attribute,
                                                                       QObject* parent) :
    BaseClass(model, attribute, parent),
    m_label(label),
    m_spinBox(spinBox)
{
    initLabel(label);

    connect(spinBox, &QDoubleSpinBox::editingFinished, this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedDoubleAdapter::getValue() const
{
    return PDFObject::createReal(m_spinBox->value());
}

void PDFObjectEditorMappedDoubleAdapter::setValue(PDFObject object)
{
    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    const PDFReal value = loader.readNumber(object, (m_spinBox->minimum() + m_spinBox->maximum()) * 0.5);
    m_spinBox->setValue(value);
}

void PDFObjectEditorMappedDoubleAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_spinBox->setHidden(!isVisible);
    m_spinBox->setEnabled(enabled);
    m_spinBox->setReadOnly(readonly);
}

PDFObjectEditorMappedColorAdapter::PDFObjectEditorMappedColorAdapter(QLabel* label,
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

    for (QString colorName : QColor::colorNames())
    {
        QColor color(colorName);
        comboBox->addItem(getIconForColor(color), colorName, color);
    }

    connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, attribute](){ emit commitRequested(attribute); });
}

PDFObject PDFObjectEditorMappedColorAdapter::getValue() const
{
    QColor color = m_comboBox->currentData().value<QColor>();
    if (!color.isValid())
    {
        color = Qt::black;
    }

    PDFObjectFactory factory;
    factory << color;
    return factory.takeObject();
}

void PDFObjectEditorMappedColorAdapter::setValue(PDFObject object)
{
    QColor color = Qt::black;

    PDFDocumentDataLoaderDecorator loader(m_model->getStorage());
    std::vector<PDFReal> colors = loader.readNumberArray(object, { });

    if (colors.size() == 3)
    {
        const PDFReal red = qBound(0.0, colors[0], 1.0);
        const PDFReal green = qBound(0.0, colors[1], 1.0);
        const PDFReal blue = qBound(0.0, colors[2], 1.0);
        color = QColor::fromRgbF(red, green, blue);
    }

    int index = m_comboBox->findData(color);
    if (index != -1)
    {
        m_comboBox->setCurrentIndex(index);
    }
    else
    {
        m_comboBox->addItem(getIconForColor(color), tr("custom"), color);
        m_comboBox->setCurrentIndex(m_comboBox->count() - 1);
    }
}

void PDFObjectEditorMappedColorAdapter::update()
{
    const bool enabled = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::HasAttribute);
    const bool readonly = !m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsAttributeEditable);
    const bool isVisible = m_model->queryAttribute(m_attribute, PDFObjectEditorAbstractModel::Question::IsVisible);

    m_label->setHidden(!isVisible);
    m_comboBox->setHidden(!isVisible);
    m_comboBox->setEnabled(enabled && !readonly);
}

QIcon PDFObjectEditorMappedColorAdapter::getIconForColor(QColor color) const
{
    QIcon icon;

    QSize iconSize = PDFWidgetUtils::scaleDPI(m_comboBox, QSize(16, 16));

    QPixmap pixmap(iconSize.width(), iconSize.height());
    pixmap.fill(color);
    icon.addPixmap(pixmap);

    return icon;
}

PDFEditObjectDialog::PDFEditObjectDialog(EditObjectType type, QWidget* parent) :
    BaseClass(parent),
    m_widget(nullptr),
    m_buttonBox(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    switch (type)
    {
        case EditObjectType::Annotation:
            setWindowTitle(tr("Edit Annotation"));
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    m_widget = new PDFObjectEditorWidget(type, this);
    layout->addWidget(m_widget);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &PDFEditObjectDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &PDFEditObjectDialog::reject);

    setMinimumSize(PDFWidgetUtils::scaleDPI(this, QSize(480, 320)));
    pdf::PDFWidgetUtils::style(this);
}

void PDFEditObjectDialog::setObject(PDFObject object)
{
    m_widget->setObject(qMove(object));
}

PDFObject PDFEditObjectDialog::getObject()
{
    return m_widget->getObject();
}

} // namespace pdf
