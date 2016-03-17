/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "mapgui/mappaintlayer.h"
#include "mapgui/navmapwidget.h"
#include "settings/settings.h"
#include "gui/mainwindow.h"
#include "mapgui/mapscale.h"
#include "table/formatter.h"
#include "geo/calculations.h"
#include "common/maptools.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QSettings>
#include <QToolTip>

#include <marble/MarbleLocale.h>
#include <marble/GeoDataDocument.h>
#include <marble/GeoDataIconStyle.h>
#include <marble/GeoDataPlacemark.h>
#include <marble/GeoDataStyle.h>
#include <marble/GeoDataTreeModel.h>
#include <marble/MarbleModel.h>
#include <marble/MarbleWidgetInputHandler.h>
#include <marble/ViewportParams.h>
#include <marble/MarbleModel.h>
#include "coordinateconverter.h"
#include "maplayer.h"
#include "ui_mainwindow.h"

using namespace Marble;

NavMapWidget::NavMapWidget(MainWindow *parent, atools::sql::SqlDatabase *sqlDb)
  : Marble::MarbleWidget(parent), parentWindow(parent), db(sqlDb)
{
  installEventFilter(this);

  // Set the map quality to gain speed
  setMapQualityForViewContext(NormalQuality, Still);
  setMapQualityForViewContext(LowQuality, Animation);

  MarbleGlobal::getInstance()->locale()->setMeasurementSystem(MarbleLocale::NauticalSystem);
  inputHandler()->setInertialEarthRotationEnabled(false);

  mapQuery = new MapQuery(db);
  mapQuery->initQueries();
  paintLayer = new MapPaintLayer(this, mapQuery);
  addLayer(paintLayer);

  connect(this, &NavMapWidget::customContextMenuRequested, this, &NavMapWidget::contextMenu);
}

NavMapWidget::~NavMapWidget()
{
  delete paintLayer;
  delete mapQuery;
}

void NavMapWidget::setTheme(const QString& theme, int index)
{
  // mapWidget->setAnimationsEnabled(false);
  // mapWidget->setShowCrosshairs(false);
  // mapWidget->setShowBackground(false);
  // mapWidget->setShowAtmosphere(false);
  // mapWidget->setShowGrid(true);

  setMapThemeId(theme);

  if(index == 1)
  {
    setShowTerrain(true);
    setShowRelief(true);
  }
  else
  {
    setShowTerrain(false);
    setShowRelief(false);
  }
  setShowClouds(false);
  setShowBorders(true);
  setShowIceLayer(false);
  setShowLakes(true);
  setShowRivers(true);
}

void NavMapWidget::setShowMapPois(bool show)
{
  qDebug() << "setShowMapPois" << show;
  setShowPlaces(show);
  setShowCities(show);
  setShowOtherPlaces(show);
}

void NavMapWidget::setShowMapFeatures(maptypes::MapObjectTypes type, bool show)
{
  qDebug() << "setShowMapFeatures" << type << "show" << show;
  paintLayer->setShowMapFeatures(type, show);
}

void NavMapWidget::setDetailFactor(int factor)
{
  qDebug() << "setDetailFactor" << factor;
  paintLayer->setDetailFactor(factor);
}

void NavMapWidget::preDatabaseLoad()
{
  paintLayer->preDatabaseLoad();
  mapQuery->deInitQueries();
}

void NavMapWidget::postDatabaseLoad()
{
  paintLayer->postDatabaseLoad();
  mapQuery->initQueries();
}

void NavMapWidget::historyNext()
{
  const MapPosHistoryEntry& entry = history.next();
  if(entry.pos.isValid())
  {
    setZoom(entry.zoom);
    centerOn(entry.pos.getLonX(), entry.pos.getLatY(), false);
  }
}

void NavMapWidget::historyBack()
{
  const MapPosHistoryEntry& entry = history.back();
  if(entry.pos.isValid())
  {
    setZoom(entry.zoom);
    centerOn(entry.pos.getLonX(), entry.pos.getLatY(), false);
  }
}

void NavMapWidget::saveState()
{
  atools::settings::Settings& s = atools::settings::Settings::instance();

  s->setValue("Map/MarkLonX", static_cast<double>(markPos.getLonX()));
  s->setValue("Map/MarkLatY", static_cast<double>(markPos.getLatY()));

  s->setValue("Map/HomeLonX", static_cast<double>(homePos.getLonX()));
  s->setValue("Map/HomeLatY", static_cast<double>(homePos.getLatY()));
  s->setValue("Map/HomeZoom", homeZoom);
  history.saveState("Map/History");
}

void NavMapWidget::restoreState()
{
  atools::settings::Settings& s = atools::settings::Settings::instance();

  if(s->contains("Map/MarkLonX") && s->contains("Map/MarkLatY"))
  {
    markPos = atools::geo::Pos(s->value("Map/MarkLonX").toDouble(), s->value("Map/MarkLatY").toDouble());
    emit markChanged(markPos);
  }

  if(s->contains("Map/HomeLonX") && s->contains("Map/HomeLatY") && s->contains("Map/HomeZoom"))
  {
    homePos = atools::geo::Pos(s->value("Map/HomeLonX").toDouble(), s->value("Map/HomeLatY").toDouble());
    homeZoom = s->value("Map/HomeZoom").toInt();
    emit homeChanged(markPos);
  }
  history.restoreState("Map/History");
}

void NavMapWidget::showSavedPos()
{
  const MapPosHistoryEntry& currentPos = history.current();

  if(currentPos.pos.isValid())
    centerOn(currentPos.pos.getLonX(), currentPos.pos.getLatY(), false);

  if(currentPos.zoom != -1)
    setZoom(currentPos.zoom);
}

void NavMapWidget::showPos(const atools::geo::Pos& pos, int zoomValue)
{
  qDebug() << "NavMapWidget::showPoint" << pos;
  setZoom(zoomValue);
  centerOn(pos.getLonX(), pos.getLatY(), false);
}

void NavMapWidget::showRect(const atools::geo::Rect& rect)
{
  qDebug() << "NavMapWidget::showRect" << rect;
  centerOn(GeoDataLatLonBox(rect.getNorth(), rect.getSouth(), rect.getEast(), rect.getWest(),
                            GeoDataCoordinates::Degree), false);
}

void NavMapWidget::showMark()
{
  qDebug() << "NavMapWidget::showMark" << markPos;
  if(markPos.isValid())
  {
    setZoom(2700);
    centerOn(markPos.getLonX(), markPos.getLatY(), false);
  }
}

void NavMapWidget::showHome()
{
  qDebug() << "NavMapWidget::showHome" << markPos;
  if(homeZoom != -1)
    setZoom(homeZoom);

  if(homePos.isValid())
    centerOn(homePos.getLonX(), homePos.getLatY(), false);
}

void NavMapWidget::changeMark(const atools::geo::Pos& pos)
{
  markPos = pos;
  update();
}

void NavMapWidget::changeHighlight(const maptypes::MapSearchResult& positions)
{
  highlightMapObjects.deleteAllObjects();
  highlightMapObjects = positions;
  update();
}

void NavMapWidget::changeHome()
{
  homePos = atools::geo::Pos(centerLongitude(), centerLatitude());
  homeZoom = zoom();
  update();
}

void NavMapWidget::contextMenu(const QPoint& point)
{
  Q_UNUSED(point);
  qInfo() << "tableContextMenu";

  Ui::MainWindow *ui = parentWindow->getUi();
  QMenu menu;
  menu.addAction(ui->actionMapSetMark);
  menu.addAction(ui->actionMapSetHome);
  menu.addAction(ui->actionMapMeasureDistance);
  menu.addAction(ui->actionMapRangeRings);
  menu.addAction(ui->actionMapNavaidRange);
  menu.addAction(ui->actionMapHideRangeRings);

  QString actionShowInSearchText;
  actionShowInSearchText = ui->actionShowInSearch->text();

  CoordinateConverter conv(viewport());
  maptypes::MapSearchResult result;
  mapQuery->getNearestObjects(conv, paintLayer->getMapLayer(),
                              paintLayer->getShownMapFeatures(),
                              point.x(), point.y(), 20, result);

  ui->actionMapHideRangeRings->setEnabled(!rangeRings.isEmpty());

  maptypes::MapObjectTypes selectedType = maptypes::NONE;
  // Create copies since the cache might be emptied by draw events after showing the menu
  maptypes::MapAirport ap;
  maptypes::MapVor vor;
  maptypes::MapNdb ndb;
  maptypes::MapWaypoint wp;
  maptypes::MapIls ils;

  ui->actionMapNavaidRange->setEnabled(false);
  if(!result.airports.isEmpty())
  {
    ap = *result.airports.first();
    ui->actionShowInSearch->setText(ui->actionShowInSearch->text().
                                    arg("Airport " + ap.name + " (" + ap.ident + ")"));
    selectedType = maptypes::AIRPORT;
    ui->actionShowInSearch->setEnabled(true);
  }
  else if(!result.vors.isEmpty())
  {
    vor = *result.vors.first();
    ui->actionShowInSearch->setText(ui->actionShowInSearch->text().
                                    arg("VOR " + vor.name + " (" + vor.ident + ")"));
    selectedType = maptypes::VOR;
    ui->actionShowInSearch->setEnabled(true);
    ui->actionMapNavaidRange->setEnabled(true);
  }
  else if(!result.ndbs.isEmpty())
  {
    ndb = *result.ndbs.first();
    ui->actionShowInSearch->setText(ui->actionShowInSearch->text().
                                    arg("NDB " + ndb.name + " (" + ndb.ident + ")"));
    selectedType = maptypes::NDB;
    ui->actionShowInSearch->setEnabled(true);
    ui->actionMapNavaidRange->setEnabled(true);
  }
  else if(!result.waypoints.isEmpty())
  {
    wp = *result.waypoints.first();
    ui->actionShowInSearch->setText(ui->actionShowInSearch->text().
                                    arg("Waypoint " + wp.ident));
    selectedType = maptypes::WAYPOINT;
    ui->actionShowInSearch->setEnabled(true);
  }
  else
  {
    ui->actionShowInSearch->setText(ui->actionShowInSearch->text().arg(QString()));
    ui->actionShowInSearch->setEnabled(false);
  }

  if(!result.ils.isEmpty())
    ils = *result.ils.first();

  menu.addAction(ui->actionShowInSearch);
  QPoint cpos = QCursor::pos();
  QAction *act = menu.exec(cpos);

  if(act != nullptr)
  {
    qreal lon, lat;
    bool visible = geoCoordinates(point.x(), point.y(), lon, lat);

    if(act == ui->actionShowInSearch)
    {
      if(visible)
      {
        if(selectedType == maptypes::AIRPORT)
        {
          ui->tabWidgetSearch->setCurrentIndex(0);
          emit objectSelected(selectedType, ap.ident, QString(), QString());
        }
        else if(selectedType == maptypes::VOR)
        {
          ui->tabWidgetSearch->setCurrentIndex(1);
          emit objectSelected(selectedType, vor.ident, vor.region, vor.apIdent);
        }
        else if(selectedType == maptypes::NDB)
        {
          ui->tabWidgetSearch->setCurrentIndex(1);
          emit objectSelected(selectedType, ndb.ident, ndb.region, ndb.apIdent);
        }
        else if(selectedType == maptypes::WAYPOINT)
        {
          ui->tabWidgetSearch->setCurrentIndex(1);
          emit objectSelected(selectedType, wp.ident, wp.region, wp.apIdent);
        }
      }
    }
    else if(act == ui->actionMapNavaidRange)
    {
      if(visible)
      {
        atools::geo::Pos center(lon, lat);
        if(selectedType == maptypes::VOR)
          addNavRangeRing(center, selectedType, vor.ident, vor.frequency, vor.range);
        else if(selectedType == maptypes::NDB)
          addNavRangeRing(center, selectedType, ndb.ident, ndb.frequency, ndb.range);
        else if(selectedType == maptypes::ILS)
          addNavRangeRing(center, selectedType, ils.ident, ils.frequency, ils.range);
      }
    }
    else if(act == ui->actionMapRangeRings)
    {
      if(visible)
        addRangeRing(atools::geo::Pos(lon, lat));
    }
    else if(act == ui->actionMapSetMark)
    {
      if(visible)
      {
        markPos = atools::geo::Pos(lon, lat);
        qDebug() << "new mark" << markPos;
        update();
        emit markChanged(markPos);
      }
    }
    else if(act == ui->actionMapHideRangeRings)
      if(visible)
        clearRangeRings();
  }

  ui->actionShowInSearch->setText(actionShowInSearchText);
}

void NavMapWidget::addNavRangeRing(const atools::geo::Pos& pos, maptypes::MapObjectTypes type,
                                   const QString& ident, int frequency, int range)
{
  maptypes::RangeRings ring;
  ring.type = type;
  ring.position = pos;

  if(type == maptypes::VOR || type == maptypes::ILS)
    ring.text = ident + " " + formatter::formatDoubleUnit(frequency / 1000., QString(), 2);
  else if(type == maptypes::NDB)
    ring.text = ident + " " + formatter::formatDoubleUnit(frequency / 100., QString(), 2);

  ring.ranges.append(range);
  rangeRings.append(ring);
  qDebug() << "navaid range" << ring.position;
  update();
}

void NavMapWidget::addRangeRing(const atools::geo::Pos& pos)
{
  maptypes::RangeRings rings;
  rings.type = maptypes::NONE;
  rings.position = pos;
  rings.ranges = {50, 100, 200, 500};
  rangeRings.append(rings);

  qDebug() << "range rings" << rings.position;
  update();
}

void NavMapWidget::clearRangeRings()
{
  qDebug() << "range rings hide";
  rangeRings.clear();
  update();
}

bool NavMapWidget::eventFilter(QObject *obj, QEvent *e)
{
  if(e->type() == QEvent::MouseButtonDblClick)
  {
    e->accept();
    qDebug() << "eventFilter mouseDoubleClickEvent";
    event(e);
    return true;
  }
  Marble::MarbleWidget::eventFilter(obj, e);
  return false;
}

void NavMapWidget::mousePressEvent(QMouseEvent *event)
{
  Q_UNUSED(event);
  qDebug() << "mousePressEvent";
}

void NavMapWidget::mouseReleaseEvent(QMouseEvent *event)
{
  Q_UNUSED(event);
  qDebug() << "mouseReleaseEvent";
}

void NavMapWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
  using namespace maptypes;

  qDebug() << "mouseDoubleClickEvent";

  CoordinateConverter conv(viewport());
  MapSearchResult res;
  const MapLayer *mapLayer = paintLayer->getMapLayer();
  mapQuery->getNearestObjects(conv, mapLayer,
                              maptypes::AIRPORT | maptypes::VOR | maptypes::NDB | maptypes::WAYPOINT,
                              event->pos().x(), event->pos().y(), 10, res);

  getNearestHighlightMapObjects(event->x(), event->y(), 10, res);

  if(!res.airports.isEmpty())
    showRect(res.airports.at(0)->bounding);
  else if(!res.vors.isEmpty())
    showPos(res.vors.at(0)->position);
  else if(!res.ndbs.isEmpty())
    showPos(res.ndbs.at(0)->position);
  else if(!res.waypoints.isEmpty())
    showPos(res.waypoints.at(0)->position);
}

void NavMapWidget::mouseMoveEvent(QMouseEvent *event)
{
  if(event->buttons() == Qt::NoButton)
    if(cursor().shape() != Qt::ArrowCursor)
      setCursor(Qt::ArrowCursor);
}

bool NavMapWidget::event(QEvent *event)
{
  using namespace maptypes;

  if(event->type() == QEvent::ToolTip)
  {
  QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);

  CoordinateConverter conv(viewport());

  QString text;

  const MapLayer *mapLayer = paintLayer->getMapLayer();
  MapSearchResult mapSearchResult;
  mapQuery->getNearestObjects(conv, mapLayer, paintLayer->getShownMapFeatures(),
                              helpEvent->pos().x(), helpEvent->pos().y(), 10, mapSearchResult);

  getNearestHighlightMapObjects(helpEvent->pos().x(), helpEvent->pos().y(), 10, mapSearchResult);

  if(mapLayer != nullptr && mapLayer->isAirportDiagram())
  {
    for(const MapAirport *ap : mapSearchResult.towers)
    {
      if(!text.isEmpty())
        text += "<hr/>";

      if(ap->towerFrequency > 0)
        text += "Tower: " + formatter::formatDoubleUnit(ap->towerFrequency / 1000., QString(), 2) + "<br/>";
      else
        text += "Tower<br/>";
    }
    for(const MapParking *p : mapSearchResult.parkings)
    {
      if(!text.isEmpty())
        text += "<hr/>";

      text += "Parking " + QString::number(p->number) + "<br/>" +
              p->name + "<br/>" +
              p->type + "<br/>" +
              QString::number(p->radius * 2) + " ft<br/>" +
              (p->jetway ? "Has Jetway<br/>" : "");

    }
    for(const MapHelipad *p : mapSearchResult.helipads)
    {
      if(!text.isEmpty())
        text += "<hr/>";

      text += "Helipad:<br/>" +
              p->surface + "<br/>" +
              p->type + "<br/>" +
              QString::number(p->width) + " ft diameter<br/>" +
              (p->closed ? "Is Closed<br/>" : "");

    }
  }

  for(const MapAirport *ap : mapSearchResult.airports)
  {
    if(!text.isEmpty())
      text += "<hr/>";

    text += ap->name + " (" + ap->ident + ")<br/>" +
            "Longest Runway: " + QLocale().toString(ap->longestRunwayLength) + " ft<br/>" +
            "Altitude: " + QLocale().toString(ap->altitude) + " ft<br/>";

    if(ap->hard())
      text += "Has Hard Runways<br/>";
    if(ap->soft())
      text += "Has Soft Runways<br/>";
    if(ap->water())
      text += "Has Water Runways<br/>";

    if(ap->towerFrequency > 0)
      text += "Tower: " + formatter::formatDoubleUnit(ap->towerFrequency / 1000., QString(), 2) + "<br/>";
    if(ap->atisFrequency > 0)
      text += "ATIS: " + formatter::formatDoubleUnit(ap->atisFrequency / 1000., QString(), 2) + "<br/>";
    if(ap->awosFrequency > 0)
      text += "AWOS: " + formatter::formatDoubleUnit(ap->awosFrequency / 1000., QString(), 2) + "<br/>";
    if(ap->asosFrequency > 0)
      text += "ASOS: " + formatter::formatDoubleUnit(ap->asosFrequency / 1000., QString(), 2) + "<br/>";
    if(ap->unicomFrequency > 0)
      text += "Unicom: " + formatter::formatDoubleUnit(ap->unicomFrequency / 1000., QString(), 2) + "<br/>";
  }

  for(const MapVor *vor : mapSearchResult.vors)
  {
    if(!text.isEmpty())
      text += "<hr/>";
    QString type;
    if(vor->dmeOnly)
      type = "DME";
    else if(vor->hasDme)
      type = "VORDME";
    else
      type = "VOR";

    text += type + ": " + vor->name + " (" + vor->ident + ")<br/>";
    text += "Freq: " + formatter::formatDoubleUnit(vor->frequency / 1000., QString(), 2) + " MHz<br/>";
    text += "Magvar: " + formatter::formatDoubleUnit(vor->magvar, QString(), 1) + " °<br/>";
    text += "Range: " + QString::number(vor->range) + " nm<br/>";
  }

  for(const MapNdb *ndb : mapSearchResult.ndbs)
  {
    if(!text.isEmpty())
      text += "<hr/>";
    text += "NDB: " + ndb->name + " (" + ndb->ident + ")<br/>";
    text += "Freq: " + formatter::formatDoubleUnit(ndb->frequency / 100., QString(), 2) + " kHz<br/>";
    text += "Magvar: " + formatter::formatDoubleUnit(ndb->magvar, QString(), 1) + " °<br/>";
    text += "Range: " + QString::number(ndb->range) + " nm<br/>";
  }

  for(const MapWaypoint *wp : mapSearchResult.waypoints)
  {
    if(!text.isEmpty())
      text += "<hr/>";
    text += "Waypoint: " + wp->ident + "<br/>";
  }

  for(const MapMarker *m : mapSearchResult.markers)
  {
    if(!text.isEmpty())
      text += "<hr/>";
    text += "Marker: " + m->type + "<br/>";
  }

  if(!text.isEmpty())
    QToolTip::showText(helpEvent->globalPos(), text.trimmed(), nullptr, QRect(), 3600 * 1000);
  else
  {
    QToolTip::hideText();
    event->ignore();
  }

  return true;
  }

  return QWidget::event(event);
}

void NavMapWidget::paintEvent(QPaintEvent *paintEvent)
{
  const GeoDataLatLonAltBox visibleLatLonAltBox = viewport()->viewLatLonAltBox();
  if(viewContext() == Marble::Still && (zoom() != curZoom || visibleLatLonAltBox != curBox))
  {
    curZoom = zoom();
    curBox = visibleLatLonAltBox;

    qDebug() << "paintEvent map view has changed zoom" << curZoom << "distance" << distanceFromZoom(zoom());
    qDebug() << curBox.toString();
    history.addEntry(atools::geo::Pos(centerLongitude(), centerLatitude()), zoom());
  }

  MarbleWidget::paintEvent(paintEvent);
}

void NavMapWidget::getNearestHighlightMapObjects(int xs, int ys, int screenDistance,
                                                 maptypes::MapSearchResult& mapobjects)
{
  using namespace maptypes;

  CoordinateConverter conv(viewport());
  int x, y;

  using maptools::insertSortedByDistance;

  for(const maptypes::MapAirport *obj : highlightMapObjects.airports)
    if(conv.wToS(obj->position, x, y))
      if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
        insertSortedByDistance(conv, mapobjects.airports, xs, ys, obj);

  for(const maptypes::MapVor *obj : highlightMapObjects.vors)
    if(conv.wToS(obj->position, x, y))
      if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
        insertSortedByDistance(conv, mapobjects.vors, xs, ys, obj);

  for(const maptypes::MapNdb *obj : highlightMapObjects.ndbs)
    if(conv.wToS(obj->position, x, y))
      if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
        insertSortedByDistance(conv, mapobjects.ndbs, xs, ys, obj);

  for(const maptypes::MapWaypoint *obj : highlightMapObjects.waypoints)
    if(conv.wToS(obj->position, x, y))
      if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
        insertSortedByDistance(conv, mapobjects.waypoints, xs, ys, obj);
}
