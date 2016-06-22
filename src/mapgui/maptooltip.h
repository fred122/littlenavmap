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

#ifndef LITTLENAVMAP_MAPTOOLTIP_H
#define LITTLENAVMAP_MAPTOOLTIP_H

#include <QColor>
#include <QObject>

namespace maptypes {
struct MapSearchResult;

}

class MapQuery;
class WeatherReporter;
class RouteMapObjectList;

namespace atools {
namespace util {
class HtmlBuilder;
}
}

class MapTooltip :
  public QObject
{
  Q_OBJECT

public:
  MapTooltip(QObject *parent, MapQuery *mapQuery, WeatherReporter *weatherReporter);
  virtual ~MapTooltip();

  QString buildTooltip(const maptypes::MapSearchResult& mapSearchResult,
                       const RouteMapObjectList& routeMapObjects,
                       bool airportDiagram);

private:
  bool checkText(atools::util::HtmlBuilder& html, int numEntries);

  static constexpr int MAXLINES = 20;

  MapQuery *query;
  WeatherReporter *weather;
  QColor iconBackColor;
};

#endif // LITTLENAVMAP_MAPTOOLTIP_H
