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

#ifndef CODEGENERATOR_H
#define CODEGENERATOR_H

#include <QObject>
#include <QMetaEnum>
#include <QMetaObject>
#include <QDomDocument>
#include <QDomElement>
#include <QObjectList>

namespace codegen
{

class Serializer
{
public:
    static QObject* load(const QDomElement& element, QObject* parent);
    static void store(QObject* object, QDomElement& element);

    template<typename T>
    static inline QString convertEnumToString(T enumValue)
    {
        QMetaEnum metaEnum = QMetaEnum::fromType<T>();
        Q_ASSERT(metaEnum.isValid());
        return metaEnum.valueToKey(enumValue);
    }

    template<typename T>
    static inline void convertStringToEnum(const QString enumString, T& value)
    {
        QMetaEnum metaEnum = QMetaEnum::fromType<T>();
        Q_ASSERT(metaEnum.isValid());
        bool ok = false;
        value = static_cast<T>(metaEnum.keyToValue(enumString.toLatin1().data(), &ok));
        if (!ok)
        {
            // Set default value, if something fails.
            value = T();
        }
    }
};

class GeneratedFunction;

class GeneratedCodeStorage : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    Q_INVOKABLE GeneratedCodeStorage(QObject* parent);

    Q_PROPERTY(QObjectList functions READ getFunctions WRITE setFunctions)

    QObjectList getFunctions() const;
    void setFunctions(const QObjectList& functions);

    GeneratedFunction* addFunction(const QString& name);

private:
    QObjectList m_functions;
};

class GeneratedFunction : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:

    enum FunctionType
    {
        Annotations,
        ColorSpace
    };
    Q_ENUM(FunctionType)


    Q_INVOKABLE GeneratedFunction(QObject* parent);

    Q_PROPERTY(QString functionType READ getFunctionTypeString WRITE setFunctionTypeString)
    Q_PROPERTY(QString functionName READ getFunctionName WRITE setFunctionName)
    Q_PROPERTY(QString functionDescription READ getFunctionDescription WRITE setFunctionDescription)

    QString getFunctionTypeString() const;
    void setFunctionTypeString(const QString& string);

    FunctionType getFunctionType() const;
    void setFunctionType(const FunctionType& functionType);

    QString getFunctionName() const;
    void setFunctionName(const QString& functionName);

    QString getFunctionDescription() const;
    void setFunctionDescription(const QString& functionDescription);

private:
    FunctionType m_functionType = FunctionType::Annotations;
    QString m_functionName;
    QString m_functionDescription;
};

class CodeGenerator : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    CodeGenerator(QObject* parent = nullptr);

    void initialize();

    GeneratedFunction* addFunction(const QString& name) { return m_storage->addFunction(name); }

    void load(const QDomDocument& document);
    void store(QDomDocument& document);

private:
    GeneratedCodeStorage* m_storage = nullptr;
};

}   // namespace codegen

Q_DECLARE_METATYPE(codegen::GeneratedCodeStorage*)
Q_DECLARE_METATYPE(codegen::GeneratedFunction*)

#endif // CODEGENERATOR_H
