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

#include "codegenerator.h"

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
}

GeneratedFunction* GeneratedCodeStorage::addFunction(const QString& name)
{
    GeneratedFunction* function = new GeneratedFunction(this);
    function->setFunctionName(name);
    m_functions.append(function);
    return function;
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
                    if (property.userType() == qMetaTypeId<QObjectList>())
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
                if (value.canConvert<QObjectList>())
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

CodeGenerator::CodeGenerator(QObject* parent) :
    BaseClass(parent)
{
    qRegisterMetaType<GeneratedCodeStorage*>("codegen::GeneratedCodeStorage");
    qRegisterMetaType<GeneratedFunction*>("codegen::GeneratedFunction");
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

}
