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

#include "codegenerator.h"

#include <QFile>
#include <QTextStream>
#include <QTextLayout>
#include <QApplication>
#include <QFontMetrics>

namespace codegen
{

GeneratedCodeStorage::GeneratedCodeStorage(QObject* parent) :
    BaseClass(parent)
{

}

QObjectList GeneratedCodeStorage::getFunctions() const
{
    return m_functions;
}

void GeneratedCodeStorage::setFunctions(const QObjectList& functions)
{
    m_functions = functions;

    auto comparator = [](const QObject* left, const QObject* right)
    {
        const GeneratedFunction* leftFunction = qobject_cast<const GeneratedFunction*>(left);
        const GeneratedFunction* rightFunction = qobject_cast<const GeneratedFunction*>(right);
        return leftFunction->getFunctionName() < rightFunction->getFunctionName();
    };
    std::sort(m_functions.begin(), m_functions.end(), comparator);
}

GeneratedFunction* GeneratedCodeStorage::addFunction(const QString& name)
{
    GeneratedFunction* function = new GeneratedFunction(this);
    function->setFunctionName(name);
    m_functions.append(function);
    return function;
}

GeneratedFunction* GeneratedCodeStorage::addFunction(GeneratedFunction* function)
{
    function->setParent(this);
    m_functions.append(function);
    return function;
}

void GeneratedCodeStorage::removeFunction(GeneratedFunction* function)
{
    function->deleteLater();
    m_functions.removeOne(function);
}

void GeneratedCodeStorage::generateCode(QTextStream& stream, CodeGeneratorParameters& parameters) const
{
    stream << Qt::endl << Qt::endl;

    for (const QObject* object : m_functions)
    {
        const GeneratedFunction* generatedFunction = qobject_cast<const GeneratedFunction*>(object);
        generatedFunction->generateCode(stream, parameters);
        stream << Qt::endl << Qt::endl;
    }
}

QObject* Serializer::load(const QDomElement& element, QObject* parent)
{
    QString className = element.attribute("class");
    const int metaTypeId = QMetaType::type(className.toLatin1());
    const QMetaObject* metaObject = QMetaType::metaObjectForType(metaTypeId);

    if (metaObject)
    {
        QObject* deserializedObject = metaObject->newInstance(Q_ARG(QObject*, parent));

        const int propertyCount = metaObject->propertyCount();
        for (int i = 0; i < propertyCount; ++i)
        {
            QMetaProperty property = metaObject->property(i);
            if (property.isWritable())
            {
                // Find, if property was serialized
                QDomElement propertyElement;
                QDomNodeList children = element.childNodes();
                for (int i = 0; i < children.count(); ++i)
                {
                    QDomNode child = children.item(i);
                    if (child.isElement())
                    {
                        QDomElement childElement = child.toElement();
                        QString attributeName = childElement.attribute("name");
                        if (attributeName == property.name())
                        {
                            propertyElement = child.toElement();
                            break;
                        }
                    }
                }

                if (!propertyElement.isNull())
                {
                    // Deserialize the element
                    if (property.isEnumType())
                    {
                        QMetaEnum metaEnum = property.enumerator();
                        Q_ASSERT(metaEnum.isValid());
                        property.write(deserializedObject, metaEnum.keyToValue(propertyElement.text().toLatin1().data()));
                    }
                    else if (property.userType() == qMetaTypeId<QObjectList>())
                    {
                        QObjectList objectList;
                        QDomNodeList children = propertyElement.childNodes();
                        for (int i = 0; i < children.count(); ++i)
                        {
                            QDomNode node = children.item(i);
                            if (node.isElement())
                            {
                                QDomElement element = node.toElement();
                                if (QObject* object = Serializer::load(element, deserializedObject))
                                {
                                    objectList.append(object);
                                }
                            }
                        }

                        property.write(deserializedObject, QVariant::fromValue(qMove(objectList)));
                    }
                    else
                    {
                        QVariant value = propertyElement.text();
                        property.write(deserializedObject, value);
                    }
                }
            }
        }

        return deserializedObject;
    }

    return nullptr;
}

void Serializer::store(QObject* object, QDomElement& element)
{
    Q_ASSERT(object);

    const QMetaObject* metaObject = object->metaObject();
    element.setAttribute("class", QString(metaObject->className()));

    const int propertyCount = metaObject->propertyCount();
    if (propertyCount > 0)
    {
        for (int i = 0; i < propertyCount; ++i)
        {
            QMetaProperty property = metaObject->property(i);
            if (property.isReadable())
            {
                QDomElement propertyElement = element.ownerDocument().createElement("property");
                element.appendChild(propertyElement);
                propertyElement.setAttribute("name", property.name());

                QVariant value = property.read(object);
                if (property.isEnumType())
                {
                    QMetaEnum metaEnum = property.enumerator();
                    Q_ASSERT(metaEnum.isValid());
                    propertyElement.appendChild(propertyElement.ownerDocument().createTextNode(metaEnum.valueToKey(value.toInt())));
                }
                else if (value.canConvert<QObjectList>())
                {
                    QObjectList objectList = value.value<QObjectList>();
                    for (QObject* currentObject : objectList)
                    {
                        QDomElement objectElement = element.ownerDocument().createElement("QObject");
                        propertyElement.appendChild(objectElement);
                        Serializer::store(currentObject, objectElement);
                    }
                }
                else
                {
                    propertyElement.appendChild(propertyElement.ownerDocument().createTextNode(value.toString()));
                }
            }
        }
    }
}

QObject* Serializer::clone(QObject* object, QObject* parent)
{
    QDomDocument document;
    QDomElement rootElement = document.createElement("root");
    document.appendChild(rootElement);
    Serializer::store(object, rootElement);
    return Serializer::load(rootElement, parent);
}

CodeGenerator::CodeGenerator(QObject* parent) :
    BaseClass(parent)
{
    qRegisterMetaType<GeneratedCodeStorage*>("codegen::GeneratedCodeStorage");
    qRegisterMetaType<GeneratedFunction*>("codegen::GeneratedFunction");
    qRegisterMetaType<GeneratedBase*>("codegen::GeneratedBase");
    qRegisterMetaType<GeneratedParameter*>("codegen::GeneratedParameter");
    qRegisterMetaType<GeneratedPDFObject*>("codegen::GeneratedPDFObject");
    qRegisterMetaType<GeneratedAction*>("codegen::GeneratedAction");
    qRegisterMetaType<QObjectList>("QObjectList");
}

void CodeGenerator::initialize()
{
    m_storage = new GeneratedCodeStorage(this);
}

void CodeGenerator::load(const QDomDocument& document)
{
    delete m_storage;
    m_storage = nullptr;

    m_storage = qobject_cast<GeneratedCodeStorage*>(Serializer::load(document.firstChildElement("root"), this));
}

void CodeGenerator::store(QDomDocument& document)
{
    if (m_storage)
    {
        QDomElement rootElement = document.createElement("root");
        document.appendChild(rootElement);
        Serializer::store(m_storage, rootElement);
    }
}

void CodeGenerator::generateCode(QString headerName, QString sourceName) const
{
    QString startMark = "/* START GENERATED CODE */";
    QString endMark = "/* END GENERATED CODE */";
    QString className = "PDFDocumentBuilder";
    const int indent = 4;

    QFile headerFile(headerName);
    if (headerFile.exists())
    {
        if (headerFile.open(QFile::ReadOnly | QFile::Text))
        {
            QString utfCode = QString::fromUtf8(headerFile.readAll());
            headerFile.close();

            int startIndex = utfCode.indexOf(startMark, Qt::CaseSensitive) + startMark.length();
            int endIndex = utfCode.indexOf(endMark, Qt::CaseSensitive);

            QString frontPart = utfCode.left(startIndex);
            QString backPart = utfCode.mid(endIndex);
            QString headerGeneratedCode = generateHeader(indent);
            QString allCode = frontPart + headerGeneratedCode + backPart;

            headerFile.open(QFile::WriteOnly | QFile::Truncate);
            headerFile.write(allCode.toUtf8());
            headerFile.close();
        }
    }

    QFile sourceFile(sourceName);
    if (sourceFile.exists())
    {
        if (sourceFile.open(QFile::ReadOnly | QFile::Text))
        {
            QString utfCode = QString::fromUtf8(sourceFile.readAll());
            sourceFile.close();

            int startIndex = utfCode.indexOf(startMark, Qt::CaseSensitive) + startMark.length();
            int endIndex = utfCode.indexOf(endMark, Qt::CaseSensitive);

            QString frontPart = utfCode.left(startIndex);
            QString backPart = utfCode.mid(endIndex);
            QString sourceGeneratedCode = generateSource(className, indent);
            QString allCode = frontPart + sourceGeneratedCode + backPart;

            sourceFile.open(QFile::WriteOnly | QFile::Truncate);
            sourceFile.write(allCode.toUtf8());
            sourceFile.close();
        }
    }
}

QString CodeGenerator::generateHeader(int indent) const
{
    QByteArray ba;
    {
        QTextStream stream(&ba, QIODevice::WriteOnly);
        stream.setCodec("UTF-8");
        stream.setRealNumberPrecision(3);
        stream.setRealNumberNotation(QTextStream::FixedNotation);

        CodeGeneratorParameters parameters;
        parameters.header = true;
        parameters.indent = indent;
        m_storage->generateCode(stream, parameters);
    }

    return QString::fromUtf8(ba);
}

QString CodeGenerator::generateSource(QString className, int indent) const
{
    QByteArray ba;
    {
        QTextStream stream(&ba, QIODevice::WriteOnly);
        stream.setCodec("UTF-8");
        stream.setRealNumberPrecision(3);
        stream.setRealNumberNotation(QTextStream::FixedNotation);

        CodeGeneratorParameters parameters;
        parameters.header = false;
        parameters.indent = indent;
        parameters.className = className;
        m_storage->generateCode(stream, parameters);
    }

    return QString::fromUtf8(ba);
}

GeneratedFunction::GeneratedFunction(QObject* parent) :
    BaseClass(parent)
{

}

QString GeneratedFunction::getFunctionTypeString() const
{
    return Serializer::convertEnumToString(m_functionType);
}

void GeneratedFunction::setFunctionTypeString(const QString& string)
{
    Serializer::convertStringToEnum<decltype(m_functionType)>(string, m_functionType);
}

GeneratedFunction::FunctionType GeneratedFunction::getFunctionType() const
{
    return m_functionType;
}

void GeneratedFunction::setFunctionType(const FunctionType& functionType)
{
    m_functionType = functionType;
}

QString GeneratedFunction::getFunctionName() const
{
    return m_functionName;
}

void GeneratedFunction::setFunctionName(const QString& functionName)
{
    m_functionName = functionName;
}

QString GeneratedFunction::getFunctionDescription() const
{
    return m_functionDescription;
}

void GeneratedFunction::setFunctionDescription(const QString& functionDescription)
{
    m_functionDescription = functionDescription;
}

GeneratedFunction* GeneratedFunction::clone(QObject* parent)
{
    return qobject_cast<GeneratedFunction*>(Serializer::clone(this, parent));
}

void GeneratedFunction::generateCode(QTextStream& stream, CodeGeneratorParameters& parameters) const
{
    QStringList parameterCaptions;
    QStringList parameterTexts;
    std::function<void (const GeneratedBase*, Pass)> gatherParameters = [&](const GeneratedBase* object, Pass pass)
    {
        if (pass != Pass::Enter)
        {
            return;
        }

        if (const GeneratedParameter* generatedParameter = qobject_cast<const GeneratedParameter*>(object))
        {
            parameterCaptions << QString("%1 %2").arg(generatedParameter->getParameterName(), generatedParameter->getParameterDescription());
            parameterTexts << QString("%1 %2").arg(getCppType(generatedParameter->getParameterDataType()), generatedParameter->getParameterName());
        }
    };
    applyFunctor(gatherParameters);

    if (parameterTexts.isEmpty())
    {
        parameterTexts << "";
    }

    if (parameters.header)
    {
        // Generate header source code

        // Function comments
        for (const QString& string : getFormattedTextWithLayout("/// ", "/// ", getFunctionDescription(), parameters.indent))
        {
            stream << string << Qt::endl;
        }

        // Function parameter comments
        for (const QString& parameterCaption : parameterCaptions)
        {
            for (const QString& string : getFormattedTextWithLayout("/// \\param ", "///        ", parameterCaption, parameters.indent))
            {
                stream << string << Qt::endl;
            }
        }

        // Function declaration
        QString functionHeader = QString("%1 %2(").arg(getCppType(getReturnType()), getFunctionName());
        QString functionHeaderNext(functionHeader.length(), QChar(QChar::Space));
        QString functionFooter = QString(");");

        for (QString& str : parameterTexts)
        {
            str += ",";
        }
        parameterTexts.back().replace(",", functionFooter);

        QStringList functionDeclaration = getFormattedTextBlock(functionHeader, functionHeaderNext, parameterTexts, parameters.indent);
        for (const QString& functionDeclarationItem : functionDeclaration)
        {
            stream << functionDeclarationItem << Qt::endl;
        }
    }
    else
    {
        // Generate c++ source code
        QString functionHeader = QString("%1 %2::%3(").arg(getCppType(getReturnType()), parameters.className, getFunctionName());
        QString functionHeaderNext(functionHeader.length(), QChar(QChar::Space));
        QString functionFooter = QString(")");

        for (QString& str : parameterTexts)
        {
            str += ",";
        }
        parameterTexts.back().replace(",", functionFooter);

        QStringList functionDeclaration = getFormattedTextBlock(functionHeader, functionHeaderNext, parameterTexts, 0);
        for (const QString& functionDeclarationItem : functionDeclaration)
        {
            stream << functionDeclarationItem << Qt::endl;
        }

        QString indent(parameters.indent, QChar(QChar::Space));

        stream << "{" << Qt::endl;
        stream << indent << "PDFObjectFactory objectBuilder;" << Qt::endl << Qt::endl;

        generateSourceCode(stream, parameters);

        stream << "}" << Qt::endl;
    }
}

bool GeneratedFunction::hasField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
        case FieldType::ItemType:
        case FieldType::DataType:
        case FieldType::Description:
            return true;

        case FieldType::Value:
            return false;

        default:
            break;
    }

    return false;
}

QVariant GeneratedFunction::readField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
            return m_functionName;
        case FieldType::ItemType:
            return int(m_functionType);
        case FieldType::DataType:
            return int(m_returnType);
        case FieldType::Description:
            return m_functionDescription;

        case FieldType::Value:
        default:
            break;
    }

    return QVariant();
}

void GeneratedFunction::writeField(GeneratedBase::FieldType fieldType, QVariant value)
{
    switch (fieldType)
    {
        case FieldType::Name:
            m_functionName = value.toString();
            break;
        case FieldType::ItemType:
            m_functionType = static_cast<FunctionType>(value.toInt());
            break;
        case FieldType::DataType:
            m_returnType = static_cast<DataType>(value.toInt());
            break;
        case FieldType::Description:
            m_functionDescription = value.toString();
            break;

        case FieldType::Value:
        default:
            break;
    }
}

void GeneratedFunction::fillComboBox(QComboBox* comboBox, GeneratedBase::FieldType fieldType)
{
    switch (fieldType)
    {
        case FieldType::ItemType:
            Serializer::fillComboBox(comboBox, m_functionType);
            break;
        case FieldType::DataType:
            Serializer::fillComboBox(comboBox, m_returnType);
            break;

        default:
            break;
    }
}

bool GeneratedFunction::canHaveSubitems() const
{
    return true;
}

QStringList GeneratedFunction::getCaptions() const
{
    return QStringList() << QString("Function") << QString("%1 %2(...)").arg(Serializer::convertEnumToString(m_returnType), m_functionName);
}

GeneratedBase* GeneratedFunction::appendItem()
{
    Q_ASSERT(canHaveSubitems());

    GeneratedBase* newItem = new GeneratedAction(this);
    addItem(newItem);
    return newItem;
}

GeneratedFunction::DataType GeneratedFunction::getReturnType() const
{
    return m_returnType;
}

void GeneratedFunction::setReturnType(DataType returnType)
{
    m_returnType = returnType;
}

GeneratedAction::GeneratedAction(QObject* parent) :
    BaseClass(parent),
    m_actionType(CreateObject)
{

}

bool GeneratedAction::hasField(FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
            return m_actionType == CreateObject;
        case FieldType::ItemType:
            return true;
        case FieldType::DataType:
            return hasField(FieldType::Name) && !m_variableName.isEmpty();
        case FieldType::Description:
            return m_actionType == Code;

        default:
            break;
    }

    return false;
}

QVariant GeneratedAction::readField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
            return m_variableName;
        case FieldType::ItemType:
            return int(m_actionType);
        case FieldType::DataType:
            return int(m_variableType);
        case FieldType::Description:
            return m_code;

        default:
            break;
    }

    return QVariant();
}

void GeneratedAction::writeField(GeneratedBase::FieldType fieldType, QVariant value)
{
    switch (fieldType)
    {
        case FieldType::Name:
            m_variableName = value.toString();
            break;
        case FieldType::ItemType:
            m_actionType = static_cast<ActionType>(value.toInt());
            break;
        case FieldType::DataType:
            m_variableType = static_cast<DataType>(value.toInt());
            break;
        case FieldType::Description:
            m_code = value.toString();
            break;

        default:
            break;
    }
}

void GeneratedAction::fillComboBox(QComboBox* comboBox, FieldType fieldType)
{
    switch (fieldType)
    {
        case FieldType::ItemType:
            Serializer::fillComboBox(comboBox, m_actionType);
            break;
        case FieldType::DataType:
            Serializer::fillComboBox(comboBox, m_variableType);
            break;

        default:
            break;
    }
}

bool GeneratedAction::canHaveSubitems() const
{
    switch (m_actionType)
    {
        case Parameters:
        case CreateObject:
            return true;

        default:
            break;
    }

    return false;
}

QStringList GeneratedAction::getCaptions() const
{
    return QStringList() << QString("Action %1").arg(Serializer::convertEnumToString(m_actionType)) << (!m_variableName.isEmpty() ? QString("%1 %2").arg(Serializer::convertEnumToString(m_variableType), m_variableName) : QString());
}

GeneratedBase* GeneratedAction::appendItem()
{
    Q_ASSERT(canHaveSubitems());
    GeneratedBase* newItem = nullptr;
    switch (m_actionType)
    {
        case Parameters:
            newItem = new GeneratedParameter(this);
            break;

        default:
            newItem = new GeneratedPDFObject(this);
            break;
    }

    addItem(newItem);
    return newItem;
}

GeneratedAction::ActionType GeneratedAction::getActionType() const
{
    return m_actionType;
}

void GeneratedAction::setActionType(ActionType actionType)
{
    m_actionType = actionType;

    if (!canHaveSubitems())
    {
        clearItems();
    }
}

QString GeneratedAction::getVariableName() const
{
    return m_variableName;
}

void GeneratedAction::setVariableName(const QString& variableName)
{
    m_variableName = variableName;
}

GeneratedAction::DataType GeneratedAction::getVariableType() const
{
    return m_variableType;
}

void GeneratedAction::setVariableType(DataType variableType)
{
    m_variableType = variableType;
}

QString GeneratedAction::getCode() const
{
    return m_code;
}

void GeneratedAction::setCode(const QString& code)
{
    m_code = code;
}

void GeneratedAction::generateSourceCodeImpl(QTextStream& stream, CodeGeneratorParameters& parameters, GeneratedBase::Pass pass) const
{
    if (pass == Pass::Enter && getActionType() == Code)
    {
        QString indent(parameters.indent, QChar(QChar::Space));
        QStringList lines = getCode().split(QChar('\n'), Qt::KeepEmptyParts, Qt::CaseInsensitive);

        for (QString string : lines)
        {
            string = string.trimmed();
            if (!string.isEmpty())
            {
                stream << indent << string << Qt::endl;
            }
            else
            {
                stream << Qt::endl;
            }
        }
    }

    if (pass == Pass::Leave && getActionType() == CreateObject)
    {
        QString indent(parameters.indent, QChar(QChar::Space));

        switch (getVariableType())
        {
            case _PDFObject:
            {
                stream << indent << getCppType(getVariableType()) << " " << getVariableName() << " = objectBuilder.takeObject();";
                break;
            }

            case _PDFObjectReference:
            {
                stream << indent << getCppType(getVariableType()) << " " << getVariableName() << " = addObject(objectBuilder.takeObject());";
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    if (pass == Pass::Leave && getActionType() != Parameters && !parameters.isLastItem)
    {
        stream << Qt::endl;
    }
}

void GeneratedBase::generateSourceCode(QTextStream& stream, CodeGeneratorParameters& parameters) const
{
    generateSourceCodeImpl(stream, parameters, Pass::Enter);

    for (const QObject* object : m_items)
    {
        const GeneratedBase* generatedBase = qobject_cast<const GeneratedBase*>(object);
        CodeGeneratorParameters parametersTemporary = parameters;
        parametersTemporary.isFirstItem = object == m_items.front();
        parametersTemporary.isLastItem = object == m_items.back();
        generatedBase->generateSourceCode(stream, parametersTemporary);
    }

    generateSourceCodeImpl(stream, parameters, Pass::Leave);
}

void GeneratedBase::applyFunctor(std::function<void (const GeneratedBase*, Pass)>& functor) const
{
    functor(this, Pass::Enter);

    for (const QObject* object : m_items)
    {
        const GeneratedBase* generatedBase = qobject_cast<const GeneratedBase*>(object);
        generatedBase->applyFunctor(functor);
    }

    functor(this, Pass::Leave);
}

bool GeneratedBase::canPerformOperation(Operation operation) const
{
    const bool isFunction = qobject_cast<const GeneratedFunction*>(this) != nullptr;
    switch (operation)
    {
        case Operation::Delete:
            return !isFunction;

        case Operation::MoveUp:
        {
            if (const GeneratedBase* parentBase = getParent())
            {
                QObjectList items = parentBase->getItems();
                return items.indexOf(const_cast<codegen::GeneratedBase*>(this)) > 0;
            }

            break;
        }

        case Operation::MoveDown:
        {
            if (const GeneratedBase* parentBase = getParent())
            {
                QObjectList items = parentBase->getItems();
                return items.indexOf(const_cast<codegen::GeneratedBase*>(this)) < items.size() - 1;
            }

            break;
        }

        case Operation::NewSibling:
            return !isFunction;

        case Operation::NewChild:
            return canHaveSubitems();

        default:
            break;
    }

    return false;
}

void GeneratedBase::performOperation(GeneratedBase::Operation operation)
{
    switch (operation)
    {
        case Operation::Delete:
        {
            getParent()->removeItem(this);
            break;
        }

        case Operation::MoveUp:
        {
            GeneratedBase* parentItem = getParent();
            QObjectList items = parentItem->getItems();
            const int index = items.indexOf(this);
            items.removeAll(const_cast<codegen::GeneratedBase*>(this));
            items.insert(index - 1, this);
            parentItem->setItems(qMove(items));
            break;
        }

        case Operation::MoveDown:
        {
            GeneratedBase* parentItem = getParent();
            QObjectList items = parentItem->getItems();
            const int index = items.indexOf(this);
            items.removeAll(const_cast<codegen::GeneratedBase*>(this));
            items.insert(index + 1, this);
            parentItem->setItems(qMove(items));
            break;
        }

        case Operation::NewSibling:
        {
            GeneratedBase* parentItem = getParent();
            if (GeneratedBase* createdItem = parentItem->appendItem())
            {
                QObjectList items = parentItem->getItems();
                items.removeAll(createdItem);
                items.insert(items.indexOf(const_cast<codegen::GeneratedBase*>(this)) + 1, createdItem);
                parentItem->setItems(qMove(items));
            }
            break;
        }

        case Operation::NewChild:
        {
            appendItem();
            break;
        }

        default:
            break;
    }
}

QObjectList GeneratedBase::getItems() const
{
    return m_items;
}

void GeneratedBase::setItems(const QObjectList& items)
{
    m_items = items;
}

void GeneratedBase::addItem(QObject* object)
{
    object->setParent(this);
    m_items.append(object);
}

void GeneratedBase::removeItem(QObject* object)
{
    object->deleteLater();
    m_items.removeAll(object);
}

void GeneratedBase::clearItems()
{
    qDeleteAll(m_items);
    m_items.clear();
}

void GeneratedBase::generateSourceCodeImpl(QTextStream& stream, CodeGeneratorParameters& parameters, GeneratedBase::Pass pass) const
{
    Q_UNUSED(stream);
    Q_UNUSED(parameters);
    Q_UNUSED(pass);
}

QString GeneratedBase::getCppType(DataType type) const
{
    return Serializer::convertEnumToString(type).mid(1);
}

QStringList GeneratedBase::getFormattedTextWithLayout(QString firstPrefix, QString prefix, QString text, int indent) const
{
    QFont font = QApplication::font();
    QFontMetrics fontMetrics(font);

    int usedLength = indent + qMax(firstPrefix.length(), prefix.length());
    int length = 80 - usedLength;
    QString testText(length, QChar('A'));
    int width = fontMetrics.width(testText);

    QTextLayout layout(text, font);
    layout.setCacheEnabled(false);
    QTextOption textOption = layout.textOption();
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(textOption);

    layout.beginLayout();
    while (true)
    {
        QTextLine textLine = layout.createLine();
        if (!textLine.isValid())
        {
            break;
        }

        textLine.setLineWidth(width);
    }
    layout.endLayout();

    QStringList texts;
    const int lineCount = layout.lineCount();
    for (int i = 0; i < lineCount; ++i)
    {
        QTextLine line = layout.lineAt(i);
        texts << text.mid(line.textStart(), line.textLength());
    }
    return getFormattedTextBlock(firstPrefix, prefix, texts, indent);
}

QStringList GeneratedBase::getFormattedTextBlock(QString firstPrefix, QString prefix, QStringList texts, int indent) const
{
    QString indentText(indent, QChar(QChar::Space));
    firstPrefix.prepend(indentText);
    prefix.prepend(indentText);

    QStringList result;
    QString currentPrefix = firstPrefix;
    for (const QString& text : texts)
    {
        result << QString("%1%2").arg(currentPrefix, text);
        currentPrefix = prefix;
    }
    return result;
}

GeneratedPDFObject::GeneratedPDFObject(QObject* parent) :
    BaseClass(parent)
{

}

bool GeneratedPDFObject::hasField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
            return m_objectType == DictionaryItemSimple || m_objectType == DictionaryItemComplex;
        case FieldType::ItemType:
            return true;
        case FieldType::Value:
            return m_objectType == Object || m_objectType == ArraySimple || m_objectType == DictionaryItemSimple;

        default:
            break;
    }

    return false;
}

QVariant GeneratedPDFObject::readField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
            return m_dictionaryItemName;
        case FieldType::ItemType:
            return int(m_objectType);
        case FieldType::Value:
            return m_value;

        default:
            break;
    }

    return QVariant();
}

void GeneratedPDFObject::writeField(GeneratedBase::FieldType fieldType, QVariant value)
{
    switch (fieldType)
    {
        case FieldType::Name:
            m_dictionaryItemName = value.toString();
            break;
        case FieldType::ItemType:
            m_objectType = static_cast<ObjectType>(value.toInt());
            break;
        case FieldType::Value:
            m_value = value.toString();
            break;

        default:
            break;
    }
}

void GeneratedPDFObject::fillComboBox(QComboBox* comboBox, GeneratedBase::FieldType fieldType)
{
    switch (fieldType)
    {
        case FieldType::ItemType:
            Serializer::fillComboBox(comboBox, m_objectType);
            break;

        default:
            break;
    }
}

bool GeneratedPDFObject::canHaveSubitems() const
{
    switch (m_objectType)
    {
        case ArrayComplex:
        case Dictionary:
        case DictionaryItemComplex:
            return true;

        default:
            break;
    }

    return false;
}

QStringList GeneratedPDFObject::getCaptions() const
{
    QString name;
    switch (m_objectType)
    {
        case Object:
            name = tr("Object");
            break;
        case ArraySimple:
            name = tr("Array (simple)");
            break;
        case ArrayComplex:
            name = tr("Array (complex)");
            break;
        case Dictionary:
            name = tr("Dictionary");
            break;
        case DictionaryItemSimple:
            name = tr("Item (simple), name = '%1'").arg(m_dictionaryItemName);
            break;
        case DictionaryItemComplex:
            name = tr("Item (complex), name = '%1'").arg(m_dictionaryItemName);
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return QStringList() << name << m_value;
}

GeneratedBase* GeneratedPDFObject::appendItem()
{
    Q_ASSERT(canHaveSubitems());
    GeneratedBase* newItem = new GeneratedPDFObject(this);
    addItem(newItem);
    return newItem;
}

QString GeneratedPDFObject::getValue() const
{
    return m_value;
}

void GeneratedPDFObject::setValue(const QString& value)
{
    m_value = value;
}

GeneratedPDFObject::ObjectType GeneratedPDFObject::getObjectType() const
{
    return m_objectType;
}

void GeneratedPDFObject::setObjectType(ObjectType objectType)
{
    m_objectType = objectType;

    if (!canHaveSubitems())
    {
        clearItems();
    }
}

QString GeneratedPDFObject::getDictionaryItemName() const
{
    return m_dictionaryItemName;
}

void GeneratedPDFObject::setDictionaryItemName(const QString& dictionaryItemName)
{
    m_dictionaryItemName = dictionaryItemName;
}

void GeneratedPDFObject::generateSourceCodeImpl(QTextStream& stream, CodeGeneratorParameters& parameters, GeneratedBase::Pass pass) const
{
    QString indent(parameters.indent, QChar(QChar::Space));
    QString writeTo("objectBuilder << ");

    switch (getObjectType())
    {
        case codegen::GeneratedPDFObject::Object:
        {
            if (pass == Pass::Enter)
            {
                stream << indent << writeTo << getValue().trimmed() << ";" << Qt::endl;
            }
            break;
        }

        case codegen::GeneratedPDFObject::ArraySimple:
        {
            if (pass == Pass::Enter)
            {
                stream << indent << "objectBuilder.beginArray();" << Qt::endl;
                for (const QString& arrayPart : getValue().trimmed().split(";"))
                {
                    stream << indent << writeTo << arrayPart << ";" << Qt::endl;
                }
                stream << indent << "objectBuilder.endArray();" << Qt::endl;
            }
            break;
        }

        case codegen::GeneratedPDFObject::ArrayComplex:
        {
            if (pass == Pass::Enter)
            {
                stream << indent << "objectBuilder.beginArray();" << Qt::endl;
            }
            if (pass == Pass::Leave)
            {
                stream << indent << "objectBuilder.endArray();" << Qt::endl;
            }
            break;
        }

        case codegen::GeneratedPDFObject::Dictionary:
        {
            if (pass == Pass::Enter)
            {
                stream << indent << "objectBuilder.beginDictionary();" << Qt::endl;
            }
            if (pass == Pass::Leave)
            {
                stream << indent << "objectBuilder.endDictionary();" << Qt::endl;
            }
            break;
        }

        case codegen::GeneratedPDFObject::DictionaryItemSimple:
        {
            if (pass == Pass::Enter)
            {
                stream << indent << QString("objectBuilder.beginDictionaryItem(\"%1\");").arg(getDictionaryItemName()) << Qt::endl;
                stream << indent << writeTo << getValue().trimmed() << ";" << Qt::endl;
                stream << indent << "objectBuilder.endDictionaryItem();" << Qt::endl;
            }
            break;
        }

        case codegen::GeneratedPDFObject::DictionaryItemComplex:
        {
            if (pass == Pass::Enter)
            {
                stream << indent << QString("objectBuilder.beginDictionaryItem(\"%1\");").arg(getDictionaryItemName()) << Qt::endl;
            }
            if (pass == Pass::Leave)
            {
                stream << indent << "objectBuilder.endDictionaryItem();" << Qt::endl;
            }
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }
}

GeneratedParameter::GeneratedParameter(QObject* parent) :
    BaseClass(parent)
{

}

bool GeneratedParameter::hasField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
        case FieldType::DataType:
        case FieldType::Description:
            return true;

        default:
            break;
    }

    return false;
}

QVariant GeneratedParameter::readField(GeneratedBase::FieldType fieldType) const
{
    switch (fieldType)
    {
        case FieldType::Name:
            return m_parameterName;
        case FieldType::DataType:
            return m_parameterDataType;
        case FieldType::Description:
            return m_parameterDescription;

        default:
            break;
    }

    return QVariant();
}

void GeneratedParameter::writeField(GeneratedBase::FieldType fieldType, QVariant value)
{
    switch (fieldType)
    {
        case FieldType::Name:
            m_parameterName = value.toString();
            break;
        case FieldType::DataType:
            m_parameterDataType = static_cast<DataType>(value.toInt());
            break;
        case FieldType::Description:
            m_parameterDescription = value.toString();
            break;

        default:
            break;
    }
}

void GeneratedParameter::fillComboBox(QComboBox* comboBox, GeneratedBase::FieldType fieldType)
{
    switch (fieldType)
    {
        case FieldType::DataType:
            Serializer::fillComboBox(comboBox, m_parameterDataType);
            break;

        default:
            break;
    }
}

bool GeneratedParameter::canHaveSubitems() const
{
    return false;
}

QStringList GeneratedParameter::getCaptions() const
{
    return QStringList() << QString("%1 %2").arg(Serializer::convertEnumToString(m_parameterDataType)).arg(m_parameterName) << m_parameterDescription;
}

GeneratedBase* GeneratedParameter::appendItem()
{
    return nullptr;
}

QString GeneratedParameter::getParameterName() const
{
    return m_parameterName;
}

void GeneratedParameter::setParameterName(const QString& parameterName)
{
    m_parameterName = parameterName;
}

GeneratedParameter::DataType GeneratedParameter::getParameterDataType() const
{
    return m_parameterDataType;
}

void GeneratedParameter::setParameterDataType(const DataType& parameterDataType)
{
    m_parameterDataType = parameterDataType;
}

QString GeneratedParameter::getParameterDescription() const
{
    return m_parameterDescription;
}

void GeneratedParameter::setParameterDescription(const QString& parameterDescription)
{
    m_parameterDescription = parameterDescription;
}

}
