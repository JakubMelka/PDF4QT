//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.

#ifndef CODEGENERATOR_H
#define CODEGENERATOR_H

#include <QObject>
#include <QMetaEnum>
#include <QMetaObject>
#include <QDomDocument>
#include <QDomElement>
#include <QObjectList>
#include <QComboBox>

namespace codegen
{

struct CodeGeneratorParameters
{
    bool header = false;
    int indent = 0;
    QString className;
    bool isFirstItem = false;
    bool isLastItem = false;
};

class Serializer
{
public:
    static QObject* load(const QDomElement& element, QObject* parent);
    static void store(QObject* object, QDomElement& element);
    static QObject* clone(QObject* object, QObject* parent);

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

    template<typename T>
    static inline void fillComboBox(QComboBox* comboBox, T value)
    {
        QMetaEnum metaEnum = QMetaEnum::fromType<T>();
        Q_ASSERT(metaEnum.isValid());

        const int keyCount = metaEnum.keyCount();
        comboBox->setUpdatesEnabled(false);
        comboBox->clear();
        for (int i = 0; i < keyCount; ++i)
        {
            comboBox->addItem(metaEnum.key(i), metaEnum.value(i));
        }
        comboBox->setCurrentIndex(comboBox->findData(int(value)));
        comboBox->setUpdatesEnabled(true);
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
    GeneratedFunction* addFunction(GeneratedFunction* function);
    void removeFunction(GeneratedFunction* function);

    void generateCode(QTextStream& stream, CodeGeneratorParameters& parameters) const;

private:
    QObjectList m_functions;
};

class GeneratedBase : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using BaseClass::BaseClass;

    enum class FieldType
    {
        Name,
        ItemType,
        DataType,
        Value,
        Description
    };

    enum DataType
    {
        _void,
        _PDFNull,
        _bool,
        _PDFInteger,
        _PDFReal,
        _PDFObjectReference,
        _PDFObject,
        _PDFIntegerVector,
        _PDFObjectReferenceVector,
        _PDFFormSubmitFlags,
        _QString,
        _QPointF,
        _QRectF,
        _QColor,
        _QVariant,
        _QPolygonF,
        _QDateTime,
        _QLocale,
        _QByteArray,
        _Polygons,
        _TextAnnotationIcon,
        _LinkHighlightMode,
        _TextAlignment,
        _AnnotationLineEnding,
        _AnnotationBorderStyle,
        _FileAttachmentIcon,
        _Stamp,
        _PDFDestination
    };
    Q_ENUM(DataType)

    Q_PROPERTY(QObjectList items READ getItems WRITE setItems)

    virtual bool hasField(FieldType fieldType) const = 0;
    virtual QVariant readField(FieldType fieldType) const = 0;
    virtual void writeField(FieldType fieldType, QVariant value) = 0;
    virtual void fillComboBox(QComboBox* comboBox, FieldType fieldType) = 0;
    virtual bool canHaveSubitems() const = 0;
    virtual QStringList getCaptions() const = 0;
    virtual GeneratedBase* appendItem() = 0;

    enum class Pass
    {
        Enter,
        Leave
    };

    void generateSourceCode(QTextStream& stream, CodeGeneratorParameters& parameters) const;
    void applyFunctor(std::function<void(const GeneratedBase*, Pass)>& functor) const;

    enum class Operation
    {
        Delete,
        MoveUp,
        MoveDown,
        NewSibling,
        NewChild
    };

    GeneratedBase* getParent() { return qobject_cast<GeneratedBase*>(parent()); }
    const GeneratedBase* getParent() const { return qobject_cast<const GeneratedBase*>(parent()); }
    bool canPerformOperation(Operation operation) const;
    void performOperation(Operation operation);

    QObjectList getItems() const;
    void setItems(const QObjectList& items);
    void addItem(QObject* object);
    void removeItem(QObject* object);
    void clearItems();

protected:
    virtual void generateSourceCodeImpl(QTextStream& stream, CodeGeneratorParameters& parameters, Pass pass) const;

    QString getCppType(DataType type) const;
    QStringList getFormattedTextWithLayout(QString firstPrefix, QString prefix, QString text, int indent) const;
    QStringList getFormattedTextBlock(QString firstPrefix, QString prefix, QStringList texts, int indent) const;

private:
    QObjectList m_items;
};

class GeneratedParameter : public GeneratedBase
{
    Q_OBJECT

private:
    using BaseClass = GeneratedBase;

public:
    Q_INVOKABLE GeneratedParameter(QObject* parent);
    Q_PROPERTY(QString parameterName READ getParameterName WRITE setParameterName)
    Q_PROPERTY(DataType parameterType READ getParameterDataType WRITE setParameterDataType)
    Q_PROPERTY(QString parameterDescription READ getParameterDescription WRITE setParameterDescription)

    virtual bool hasField(FieldType fieldType) const override;
    virtual QVariant readField(FieldType fieldType) const override;
    virtual void writeField(FieldType fieldType, QVariant value) override;
    virtual void fillComboBox(QComboBox* comboBox, FieldType fieldType) override;
    virtual bool canHaveSubitems() const override;
    virtual QStringList getCaptions() const override;
    virtual GeneratedBase* appendItem() override;

    QString getParameterName() const;
    void setParameterName(const QString& parameterName);

    DataType getParameterDataType() const;
    void setParameterDataType(const DataType& parameterDataType);

    QString getParameterDescription() const;
    void setParameterDescription(const QString& parameterDescription);

private:
    QString m_parameterName;
    DataType m_parameterDataType = _void;
    QString m_parameterDescription;
};

class GeneratedPDFObject : public GeneratedBase
{
    Q_OBJECT

private:
    using BaseClass = GeneratedBase;

public:

    enum ObjectType
    {
        Object,
        ArraySimple,
        ArrayComplex,
        Dictionary,
        DictionaryItemSimple,
        DictionaryItemComplex
    };
    Q_ENUM(ObjectType)

    Q_INVOKABLE GeneratedPDFObject(QObject* parent);
    Q_PROPERTY(QString dictionaryItemName READ getDictionaryItemName WRITE setDictionaryItemName)
    Q_PROPERTY(ObjectType objectType READ getObjectType WRITE setObjectType)
    Q_PROPERTY(QString value READ getValue WRITE setValue)

    virtual bool hasField(FieldType fieldType) const override;
    virtual QVariant readField(FieldType fieldType) const override;
    virtual void writeField(FieldType fieldType, QVariant value) override;
    virtual void fillComboBox(QComboBox* comboBox, FieldType fieldType) override;
    virtual bool canHaveSubitems() const override;
    virtual QStringList getCaptions() const override;
    virtual GeneratedBase* appendItem() override;

    QString getValue() const;
    void setValue(const QString& value);

    ObjectType getObjectType() const;
    void setObjectType(ObjectType objectType);

    QString getDictionaryItemName() const;
    void setDictionaryItemName(const QString& dictionaryItemName);

protected:
    virtual void generateSourceCodeImpl(QTextStream& stream, CodeGeneratorParameters& parameters, Pass pass) const override;

private:
    QString m_dictionaryItemName;
    ObjectType m_objectType = Object;
    QString m_value;
};

class GeneratedAction : public GeneratedBase
{
    Q_OBJECT

private:
    using BaseClass = GeneratedBase;

public:

    enum ActionType
    {
        Parameters,
        CreateObject,
        Code
    };
    Q_ENUM(ActionType)

    Q_INVOKABLE GeneratedAction(QObject* parent);
    Q_PROPERTY(ActionType actionType READ getActionType WRITE setActionType)
    Q_PROPERTY(QString variableName READ getVariableName WRITE setVariableName)
    Q_PROPERTY(DataType variableType READ getVariableType WRITE setVariableType)
    Q_PROPERTY(QString code READ getCode WRITE setCode)

    virtual bool hasField(FieldType fieldType) const override;
    virtual QVariant readField(FieldType fieldType) const override;
    virtual void writeField(FieldType fieldType, QVariant value) override;
    virtual void fillComboBox(QComboBox* comboBox, FieldType fieldType) override;
    virtual bool canHaveSubitems() const override;
    virtual QStringList getCaptions() const override;
    virtual GeneratedBase* appendItem() override;

    ActionType getActionType() const;
    void setActionType(ActionType actionType);

    QString getVariableName() const;
    void setVariableName(const QString& variableName);

    DataType getVariableType() const;
    void setVariableType(DataType variableType);

    QString getCode() const;
    void setCode(const QString& code);

protected:
    virtual void generateSourceCodeImpl(QTextStream& stream, CodeGeneratorParameters& parameters, Pass pass) const override;

private:
    ActionType m_actionType;
    QString m_variableName;
    DataType m_variableType = _void;
    QString m_code;
};

class GeneratedFunction : public GeneratedBase
{
    Q_OBJECT

private:
    using BaseClass = GeneratedBase;

public:

    enum FunctionType
    {
        Structure,
        Annotations,
        ColorSpace,
        Actions,
        Forms
    };
    Q_ENUM(FunctionType)


    Q_INVOKABLE GeneratedFunction(QObject* parent);

    Q_PROPERTY(FunctionType functionType READ getFunctionType WRITE setFunctionType)
    Q_PROPERTY(QString functionName READ getFunctionName WRITE setFunctionName)
    Q_PROPERTY(QString functionDescription READ getFunctionDescription WRITE setFunctionDescription)
    Q_PROPERTY(DataType returnType READ getReturnType WRITE setReturnType)

    virtual bool hasField(FieldType fieldType) const override;
    virtual QVariant readField(FieldType fieldType) const override;
    virtual void writeField(FieldType fieldType, QVariant value) override;
    virtual void fillComboBox(QComboBox* comboBox, FieldType fieldType) override;
    virtual bool canHaveSubitems() const override;
    virtual QStringList getCaptions() const override;
    virtual GeneratedBase* appendItem() override;

    QString getFunctionTypeString() const;
    void setFunctionTypeString(const QString& string);

    FunctionType getFunctionType() const;
    void setFunctionType(const FunctionType& functionType);

    QString getFunctionName() const;
    void setFunctionName(const QString& functionName);

    QString getFunctionDescription() const;
    void setFunctionDescription(const QString& functionDescription);

    DataType getReturnType() const;
    void setReturnType(DataType returnType);

    /// Create a clone of this function
    GeneratedFunction* clone(QObject* parent);

    void generateCode(QTextStream& stream, CodeGeneratorParameters& parameters) const;

private:
    FunctionType m_functionType = FunctionType::Annotations;
    QString m_functionName;
    QString m_functionDescription;
    DataType m_returnType = _void;
};

class CodeGenerator : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    CodeGenerator(QObject* parent = nullptr);

    void initialize();

    QObjectList getFunctions() const { return m_storage->getFunctions(); }
    GeneratedFunction* addFunction(const QString& name) { return m_storage->addFunction(name); }
    GeneratedFunction* addFunction(GeneratedFunction* function) { return m_storage->addFunction(function); }
    void removeFunction(GeneratedFunction* function) { m_storage->removeFunction(function); }

    void load(const QDomDocument& document);
    void store(QDomDocument& document);

    void generateCode(QString headerName, QString sourceName) const;

private:
    QString generateHeader(int indent) const;
    QString generateSource(QString className, int indent) const;

    GeneratedCodeStorage* m_storage = nullptr;
};

}   // namespace codegen

Q_DECLARE_METATYPE(codegen::GeneratedCodeStorage*)
Q_DECLARE_METATYPE(codegen::GeneratedFunction*)

#endif // CODEGENERATOR_H
