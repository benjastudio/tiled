/*
 * abstractobjecttool.cpp
 * Copyright 2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "abstractobjecttool.h"

#include "changeproperties.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "mapobjectitem.h"
#include "maprenderer.h"
#include "mapscene.h"
#include "objectgroup.h"
#include "raiselowerhelper.h"
#include "utils.h"

#include <QKeyEvent>
#include <QMenu>
#include <QString>
#include <QShortcut >
#include <QUndoStack>
#include <QSignalMapper>
#include <cmath>
#include <QCoreApplication>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <iostream>
using namespace Tiled;
using namespace Tiled::Internal;

AbstractObjectTool::AbstractObjectTool(const QString &name,
                                       const QIcon &icon,
                                       const QKeySequence &shortcut,
                                       QObject *parent)
    : AbstractTool(name, icon, shortcut, parent)
    , mMapScene(0)
{
}

void AbstractObjectTool::activate(MapScene *scene)
{
    mMapScene = scene;
}

void AbstractObjectTool::deactivate(MapScene *)
{
    mMapScene = 0;
}

void AbstractObjectTool::keyPressed(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_PageUp:    raise(); return;
    case Qt::Key_PageDown:  lower(); return;
    case Qt::Key_Home:      raiseToTop(); return;
    case Qt::Key_End:       lowerToBottom(); return;
    }

    event->ignore();
}

void AbstractObjectTool::mouseLeft()
{
    setStatusInfo(QString());
}

void AbstractObjectTool::mouseMoved(const QPointF &pos,
                                    Qt::KeyboardModifiers)
{
    const QPointF tilePosF = mapDocument()->renderer()->screenToTileCoords(pos);
    const int x = (int) std::floor(tilePosF.x());
    const int y = (int) std::floor(tilePosF.y());
    setStatusInfo(QString(QLatin1String("%1, %2")).arg(x).arg(y));
}

void AbstractObjectTool::mousePressed(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        showContextMenu(topMostObjectItemAt(event->scenePos()),
                        event->screenPos());
    }
}

void AbstractObjectTool::updateEnabledState()
{
    setEnabled(currentObjectGroup() != 0);
}

ObjectGroup *AbstractObjectTool::currentObjectGroup() const
{
    if (!mapDocument())
        return 0;

    return dynamic_cast<ObjectGroup*>(mapDocument()->currentLayer());
}

MapObjectItem *AbstractObjectTool::topMostObjectItemAt(QPointF pos) const
{
    foreach (QGraphicsItem *item, mMapScene->items(pos)) {
        if (MapObjectItem *objectItem = dynamic_cast<MapObjectItem*>(item))
            return objectItem;
    }
    return 0;
}

void AbstractObjectTool::duplicateObjects()
{
    mapDocument()->duplicateObjects(mapDocument()->selectedObjects());
}

void AbstractObjectTool::removeObjects()
{
    mapDocument()->removeObjects(mapDocument()->selectedObjects());
}

void AbstractObjectTool::flipHorizontally()
{
    mapDocument()->flipSelectedObjects(FlipHorizontally);
}

void AbstractObjectTool::flipVertically()
{
    mapDocument()->flipSelectedObjects(FlipVertically);
}

void AbstractObjectTool::raise()
{
    RaiseLowerHelper(mMapScene).raise();
}

void AbstractObjectTool::lower()
{
    RaiseLowerHelper(mMapScene).lower();
}

void AbstractObjectTool::raiseToTop()
{
    RaiseLowerHelper(mMapScene).raiseToTop();
}

void AbstractObjectTool::lowerToBottom()
{
    RaiseLowerHelper(mMapScene).lowerToBottom();
}

void AbstractObjectTool::addProperty(int propertyId)
{
    Object *object = mapDocument()->currentObject();
    if (!object)
        return;

    QUndoStack *undoStack = mapDocument()->undoStack();
    undoStack->push(new SetProperty(mapDocument(), mapDocument()->currentObjects(), mCustomProperties[propertyId].name, mCustomProperties[propertyId].value));

}
void AbstractObjectTool::applyTemplate(int templateId)
{
    Object *object = mapDocument()->currentObject();
    if (!object)
        return;

    QUndoStack *undoStack = mapDocument()->undoStack();
    undoStack->push(new ApplyTemplate(mapDocument(), mapDocument()->currentObjects(), mCustomTemplates[templateId]));

}


/**
 * Shows the context menu for map objects. The menu allows you to duplicate and
 * remove the map objects, or to edit their properties.
 */
void AbstractObjectTool::showContextMenu(MapObjectItem *clickedObjectItem,
                                         QPoint screenPos)
{
    QSet<MapObjectItem *> selection = mMapScene->selectedObjectItems();
    if (clickedObjectItem && !selection.contains(clickedObjectItem)) {
        selection.clear();
        selection.insert(clickedObjectItem);
        mMapScene->setSelectedObjectItems(selection);
    }
    if (selection.isEmpty())
        return;

    const QList<MapObject*> &selectedObjects = mapDocument()->selectedObjects();
    const QList<ObjectGroup*> objectGroups = mapDocument()->map()->objectGroups();

    QMenu menu;
    QAction *duplicateAction = menu.addAction(tr("Duplicate %n Object(s)", "", selection.size()),
                                              this, SLOT(duplicateObjects()));
    QAction *removeAction = menu.addAction(tr("Remove %n Object(s)", "", selection.size()),
                                           this, SLOT(removeObjects()));

    duplicateAction->setIcon(QIcon(QLatin1String(":/images/16x16/stock-duplicate-16.png")));
    removeAction->setIcon(QIcon(QLatin1String(":/images/16x16/edit-delete.png")));

    menu.addSeparator();

    /* Add Custom Properties */
    QMenu *parentMenu = NULL;

    QMenu *addCustomPropertyMenu = menu.addMenu(tr("Add custom property"));
    parentMenu = addCustomPropertyMenu;
    QMenu *propertyMenu = NULL;
    QString propertyName;
    QString customPropertiesFileName = QLatin1String("customProperties.xml");
    QFile propertiesFile(customPropertiesFileName);

	if (!(!propertiesFile.open(QIODevice::ReadOnly | QIODevice::Text))) {

	    QXmlStreamReader reader(&propertiesFile);

	    while (!reader.atEnd() && !reader.hasError()) {

	    	reader.readNext();

			const QXmlStreamAttributes atts = reader.attributes();

	    	if(reader.name().compare(QLatin1String("menu")) == 0){
	    		if(reader.isStartElement()){
	    			parentMenu = parentMenu->addMenu(atts.value(QLatin1String("name")).toString());
	    		}else if(reader.isEndElement()){
	    			parentMenu = (QMenu*) parentMenu->parent();
	    		}
	    	}else if(reader.name().compare(QLatin1String("property")) == 0){
	    		if(reader.isStartElement()){
	    			propertyMenu = parentMenu->addMenu(atts.value(QLatin1String("name")).toString());
	    			propertyName = atts.value(QLatin1String("name")).toString();
	    		}else if(reader.isEndElement()){
	    			propertyMenu = NULL;
	    		}
	    	}else if(reader.name().compare(QLatin1String("value")) == 0){
	    		if(reader.isStartElement() && (propertyMenu != NULL)){
	    			Property prop;
	    			prop.value = reader.readElementText();
	    	    	prop.name = propertyName;
	    			QSignalMapper *signalMapper = new QSignalMapper(propertyMenu);
	    			QAction *action = propertyMenu->addAction(prop.value, signalMapper, SLOT(map()));
	    			mCustomProperties.append(prop);
	    			int propertyId = mCustomProperties.size()-1;
	    			signalMapper->setMapping(action, propertyId);
	    			connect(signalMapper, SIGNAL(mapped(const int)), this, SLOT(addProperty(const int)));
	    		}
	    	}
	    }
	    reader.clear();
	}else{
		std::cout << "Can't open " << customPropertiesFileName.toStdString() << std::endl;
	}


    /* Apply Custom Templates */
	QMenu *applyCustomTemplateMenu = menu.addMenu(tr("Apply custom template"));
    parentMenu = applyCustomTemplateMenu;
    QList<QString> propertyList;
    QString templateName;
    QString customTemplatesFileName = QLatin1String("customTemplates.xml");
    QFile templateFile(customTemplatesFileName);
    QVector<Property> currentTemplate;

	if (!(!templateFile.open(QIODevice::ReadOnly | QIODevice::Text))) {

	    QXmlStreamReader reader(&templateFile);

	    while (!reader.atEnd() && !reader.hasError()) {

	    	reader.readNext();

			const QXmlStreamAttributes atts = reader.attributes();

	    	if(reader.name().compare(QLatin1String("menu")) == 0){
	    		if(reader.isStartElement()){
	    			parentMenu = parentMenu->addMenu(atts.value(QLatin1String("name")).toString());
	    		}else if(reader.isEndElement()){
	    			parentMenu = (QMenu*) parentMenu->parent();
	    		}
	    	}else if(reader.name().compare(QLatin1String("template")) == 0){
	    		if(reader.isStartElement()){
	    			templateName = atts.value(QLatin1String("name")).toString();
	    		}else if(reader.isEndElement()){
	    			QSignalMapper *signalMapper = new QSignalMapper(parentMenu);
	    			QAction *action;
	    			action = parentMenu->addAction(templateName, signalMapper, SLOT(map()));
	    			mCustomTemplates.append(currentTemplate);
	    			int templateId = mCustomTemplates.size()-1;
	    			signalMapper->setMapping(action, templateId);
	    			connect(signalMapper, SIGNAL(mapped(const int)), this, SLOT(applyTemplate(const int)));
	    			currentTemplate.clear();
	    			templateName.clear();
	    		}
	    	}else if(reader.name().compare(QLatin1String("property")) == 0){
	    		if(reader.isStartElement()){
	    			Property prop;
	    			prop.name = atts.value(QLatin1String("name")).toString();
	    			prop.value = reader.readElementText();
	    			currentTemplate.append(prop);
	    		}
	    	}
	    }
	    reader.clear();
	}else{
		std::cout << "Can't open " << customTemplatesFileName.toStdString() << std::endl;
	}

    menu.addSeparator();

    menu.addAction(tr("Flip Horizontally"), this, SLOT(flipHorizontally()), QKeySequence(tr("X")));
    menu.addAction(tr("Flip Vertically"), this, SLOT(flipVertically()), QKeySequence(tr("Y")));

    ObjectGroup *objectGroup = RaiseLowerHelper::sameObjectGroup(selection);
    if (objectGroup && objectGroup->drawOrder() == ObjectGroup::IndexOrder) {
        menu.addSeparator();
        menu.addAction(tr("Raise Object"), this, SLOT(raise()), QKeySequence(tr("PgUp")));
        menu.addAction(tr("Lower Object"), this, SLOT(lower()), QKeySequence(tr("PgDown")));
        menu.addAction(tr("Raise Object to Top"), this, SLOT(raiseToTop()), QKeySequence(tr("Home")));
        menu.addAction(tr("Lower Object to Bottom"), this, SLOT(lowerToBottom()), QKeySequence(tr("End")));
    }

    if (objectGroups.size() > 1) {
        menu.addSeparator();
        QMenu *moveToLayerMenu = menu.addMenu(tr("Move %n Object(s) to Layer",
                                                 "", selectedObjects.size()));
        foreach (ObjectGroup *objectGroup, objectGroups) {
            QAction *action = moveToLayerMenu->addAction(objectGroup->name());
            action->setData(QVariant::fromValue(objectGroup));
        }
    }

    menu.addSeparator();
    QIcon propIcon(QLatin1String(":images/16x16/document-properties.png"));
    QAction *propertiesAction = menu.addAction(propIcon,
                                               tr("Object &Properties..."));
    // TODO: Implement editing of properties for multiple objects
    propertiesAction->setEnabled(selectedObjects.size() == 1);

    Utils::setThemeIcon(removeAction, "edit-delete");
    Utils::setThemeIcon(propertiesAction, "document-properties");

    QAction *action = menu.exec(screenPos);
    if (!action)
        return;

    if (action == propertiesAction) {
        MapObject *mapObject = selectedObjects.first();
        mapDocument()->setCurrentObject(mapObject);
        mapDocument()->emitEditCurrentObject();
        return;
    }

    if (ObjectGroup *objectGroup = action->data().value<ObjectGroup*>()) {
        mapDocument()->moveObjectsToGroup(mapDocument()->selectedObjects(),
                                          objectGroup);
    }
}
