//    Copyright (C) 2022 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfmediaviewerdialog.h"
#include "ui_pdfmediaviewerdialog.h"
#include "pdfwidgetutils.h"
#include "pdfdocument.h"
#include "pdfannotation.h"
#include "pdf3d_u3d.h"
#include "pdf3dsceneprocessor.h"
#include "pdfdbgheap.h"

#include <QColor>

#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DRender/QCamera>
#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DExtras/QForwardRenderer>
#include <Qt3DExtras/QOrbitCameraController>
#include <Qt3DExtras/QTorusMesh>
#include <Qt3DExtras/QCylinderMesh>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DExtras/QSphereMesh>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DRender/QPointLight>

namespace pdfviewer
{

PDFMediaViewerDialog::PDFMediaViewerDialog(QWidget* parent) :
    QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint),
    ui(new Ui::PDFMediaViewerDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);

    m_3dWindow = new Qt3DExtras::Qt3DWindow(this->screen());
    m_3dWindow->defaultFrameGraph()->setClearColor(Qt::white);

    QWidget* container = QWidget::createWindowContainer(m_3dWindow, this);
    container->setMinimumSize(pdf::PDFWidgetUtils::scaleDPI(container, QSize(300, 300)));
    ui->gridLayout->addWidget(container, 0, 0);

    m_rootEntity = new Qt3DCore::QEntity();
    m_3dWindow->setRootEntity(m_rootEntity);
    m_sceneEntity = new Qt3DCore::QEntity(m_rootEntity);

    Qt3DRender::QCamera* camera = m_3dWindow->camera();
    Qt3DExtras::QOrbitCameraController* cameraController = new Qt3DExtras::QOrbitCameraController(m_rootEntity);
    cameraController->setCamera(camera);
}

PDFMediaViewerDialog::~PDFMediaViewerDialog()
{
    delete ui;
    delete m_rootEntity;
}

void PDFMediaViewerDialog::initDemo()
{
    // Setup camera
    Qt3DRender::QCamera* camera = m_3dWindow->camera();
    camera->lens()->setPerspectiveProjection(25.0f, 16.0f / 9.0f, 0.001f, 1000.0f);
    camera->setUpVector(QVector3D(0.0f, -1.0f, 0.0f));
    camera->setPosition(QVector3D(-20.0f, 0.0f, 0.0f));
    camera->setViewCenter(QVector3D(0.0f, 0.0f, 0.0f));

    // Sphere
    Qt3DExtras::QSphereMesh* sphere = new Qt3DExtras::QSphereMesh();
    sphere->setRadius(2.0);
    sphere->setRings(60);
    sphere->setSlices(30);

    Qt3DCore::QTransform* sphereTransform = new Qt3DCore::QTransform();
    sphereTransform->setTranslation(QVector3D(2.0f, 0.0f, 3.00f));

    Qt3DExtras::QPhongMaterial* sphereMaterial = new Qt3DExtras::QPhongMaterial();
    sphereMaterial->setDiffuse(Qt::green);

    Qt3DCore::QEntity* sphereEntity = new Qt3DCore::QEntity(m_sceneEntity);
    sphereEntity->addComponent(sphere);
    sphereEntity->addComponent(sphereTransform);
    sphereEntity->addComponent(sphereMaterial);

    // Torus
    Qt3DExtras::QTorusMesh* torus = new Qt3DExtras::QTorusMesh();
    torus->setRadius(2.0);
    torus->setMinorRadius(0.5);
    torus->setRings(120);
    torus->setSlices(25);

    Qt3DCore::QTransform* torusTransform = new Qt3DCore::QTransform();
    torusTransform->setScale(1.74f);
    torusTransform->setRotationY(25);
    torusTransform->setTranslation(QVector3D(0.5f, 0.7f, 0.03f));

    Qt3DExtras::QPhongMaterial* torusMaterial = new Qt3DExtras::QPhongMaterial();
    torusMaterial->setDiffuse(Qt::blue);

    Qt3DCore::QEntity* torusEntity = new Qt3DCore::QEntity(m_sceneEntity);
    torusEntity->addComponent(torus);
    torusEntity->addComponent(torusTransform);
    torusEntity->addComponent(torusMaterial);

    // Cuboid
    Qt3DExtras::QCuboidMesh* cuboid = new Qt3DExtras::QCuboidMesh();
    cuboid->setXExtent(1.37f);
    cuboid->setYExtent(1.74f);
    cuboid->setZExtent(2.0f);

    Qt3DCore::QTransform* cuboidTransform = new Qt3DCore::QTransform();
    cuboidTransform->setTranslation(QVector3D(0.0f, 7.0f, 0.00f));

    Qt3DExtras::QPhongMaterial* cuboidMaterial = new Qt3DExtras::QPhongMaterial();
    cuboidMaterial->setDiffuse(Qt::red);

    Qt3DCore::QEntity* cuboidEntity = new Qt3DCore::QEntity(m_sceneEntity);
    cuboidEntity->addComponent(cuboid);
    cuboidEntity->addComponent(cuboidTransform);
    cuboidEntity->addComponent(cuboidMaterial);

    // Cylinder
    Qt3DExtras::QCylinderMesh* cylinder = new Qt3DExtras::QCylinderMesh();
    cylinder->setRadius(2.0);
    cylinder->setLength(6.0);
    cylinder->setRings(120);
    cylinder->setSlices(25);

    Qt3DCore::QTransform* cylinderTransform = new Qt3DCore::QTransform();
    cylinderTransform->setTranslation(QVector3D(0.0f, -7.0f, 0.00f));

    Qt3DExtras::QPhongMaterial* cylinderMaterial = new Qt3DExtras::QPhongMaterial();
    cylinderMaterial->setDiffuse(Qt::yellow);

    Qt3DCore::QEntity* cylinderEntity = new Qt3DCore::QEntity(m_sceneEntity);
    cylinderEntity->addComponent(cylinder);
    cylinderEntity->addComponent(cylinderTransform);
    cylinderEntity->addComponent(cylinderMaterial);

    // Light
    Qt3DCore::QEntity* lightEntity = new Qt3DCore::QEntity(m_sceneEntity);
    Qt3DRender::QPointLight* light = new Qt3DRender::QPointLight(lightEntity);
    light->setColor(Qt::white);
    light->setIntensity(1.0);
    lightEntity->addComponent(light);

    Qt3DCore::QTransform* lightTransform = new Qt3DCore::QTransform(lightEntity);
    lightTransform->setTranslation(QVector3D(10.0f, 10.0f, 10.0f));
    lightEntity->addComponent(lightTransform);
}

void PDFMediaViewerDialog::initFromAnnotation(const pdf::PDFDocument* document,
                                              const pdf::PDFAnnotation* annotation)
{
    Q_ASSERT(document);
    Q_ASSERT(annotation);

    switch (annotation->getType())
    {
        case pdf::AnnotationType::_3D:
        {
            const pdf::PDF3DAnnotation* typedAnnotation = dynamic_cast<const pdf::PDF3DAnnotation*>(annotation);
            initFrom3DAnnotation(document, typedAnnotation);
            break;
        }

        default:
            break;
    }
}

void PDFMediaViewerDialog::regenerateScene()
{
    for (Qt3DCore::QNode* node : m_sceneEntity->childNodes())
    {
        Qt3DCore::QNode* nullNode = nullptr;
        node->setParent(nullNode);
        delete node;
    }

    if (m_sceneU3d.has_value())
    {
        PDF3DSceneProcessor processor;
        processor.setMode(PDF3DSceneProcessor::Wireframe);
        processor.setSceneRoot("PDF3D Scene");
        auto scene = processor.createScene(&m_sceneU3d.value());
        if (scene.sceneRoot)
        {
            scene.sceneRoot->setParent(m_sceneEntity);
        }
    }
}

void PDFMediaViewerDialog::initFrom3DAnnotation(const pdf::PDFDocument* document,
                                                const pdf::PDF3DAnnotation* annotation)
{
    const pdf::PDF3DStream& stream = annotation->getStream();

    m_sceneU3d = std::nullopt;

    pdf::PDFObject object = document->getObject(stream.getStream());
    if (object.isStream())
    {
        QByteArray data = document->getDecodedStream(object.getStream());

        switch (stream.getType())
        {
            case pdf::PDF3DStream::Type::U3D:
            {
                QStringList errors;
                m_sceneU3d = pdf::u3d::PDF3D_U3D::parse(data, &errors);
                break;
            }

            case pdf::PDF3DStream::Type::PRC:
                break;

            default:
                break;
        }
    }

    regenerateScene();
}

}   // namespace pdfviewer
