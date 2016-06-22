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

#include "routenetwork.h"

#include "sql/sqldatabase.h"
#include "sql/sqlquery.h"
#include "sql/sqlrecord.h"
#include "sql/sqlutil.h"

#include "geo/pos.h"
#include "geo/rect.h"

#include <QElapsedTimer>

using atools::sql::SqlDatabase;
using atools::sql::SqlQuery;
using atools::sql::SqlRecord;
using atools::geo::Pos;
using atools::geo::Rect;

using namespace nw;

RouteNetwork::RouteNetwork(atools::sql::SqlDatabase *sqlDb, const QString& nodeTableName,
                           const QString& edgeTableName, const QStringList& nodeExtraColumns,
                           const QStringList& edgeExtraColumns)
  : db(sqlDb), nodeTable(nodeTableName), edgeTable(edgeTableName), nodeExtraCols(nodeExtraColumns),
    edgeExtraCols(edgeExtraColumns)
{
  nodes.reserve(60000);
  destinationNodePredecessors.reserve(1000);
  initQueries();
}

RouteNetwork::~RouteNetwork()
{
  deInitQueries();
}

int RouteNetwork::getNumberOfNodesDatabase()
{
  if(numNodesDb == -1)
    numNodesDb = atools::sql::SqlUtil(db).rowCount(nodeTable);
  return numNodesDb;
}

int RouteNetwork::getNumberOfNodesCache() const
{
  return nodes.size();
}

void RouteNetwork::clear()
{
  startNodeRect = atools::geo::EMPTY_RECT;
  destinationNodeRect = atools::geo::EMPTY_RECT;
  startPos = atools::geo::EMPTY_POS;
  destinationPos = atools::geo::EMPTY_POS;
  nodes.clear();
  destinationNodePredecessors.clear();
  numNodesDb = -1;
  nodeIndexesCreated = false;
  edgeIndexesCreated = false;
  nodes.reserve(60000);
  destinationNodePredecessors.reserve(1000);
}

void RouteNetwork::getNeighbours(const nw::Node& from, QVector<nw::Node>& neighbours,
                                 QVector<Edge>& edges)
{
  for(const Edge& e : from.edges)
  {
    bool add = false;

    // Handle airways differently to keep cache for low and high alt routes together
    if(e.type == WAYPOINT_BOTH)
      add = mode & ROUTE_JET || mode & ROUTE_VICTOR;
    else if(e.type == WAYPOINT_JET)
      add = mode & ROUTE_JET;
    else if(e.type == WAYPOINT_VICTOR)
      add = mode & ROUTE_VICTOR;
    else
      add = true;

    if(add)
    {
      // TODO single query fetch is expensive
      neighbours.append(fetchNode(e.toNodeId));
      edges.append(e);
    }
  }
}

void RouteNetwork::addStartAndDestinationNodes(const atools::geo::Pos& from, const atools::geo::Pos& to)
{
  qDebug() << "adding start and  destination to network";

  if(startPos == from && destinationPos == to)
    return;

  if(destinationPos != to)
  {
    // Remove all references to destination node
    cleanDestNodeEdges();

    destinationPos = to;

    // Add destination first so it can be added to start successors
    destinationNodeRect = Rect(to, NODE_SEARCH_RADIUS);
    fetchNode(to.getLonX(), to.getLatY(), false, DESTINATION_NODE_ID);

    for(int id : nodes.keys())
      addDestNodeEdges(nodes[id]);
  }

  if(startPos != from)
  {
    startPos = from;
    startNodeRect = Rect(from, NODE_SEARCH_RADIUS);
    fetchNode(from.getLonX(), from.getLatY(), true, START_NODE_ID);
  }
  qDebug() << "adding start and  destination to network done";
}

nw::Node RouteNetwork::getStartNode() const
{
  return nodes.value(START_NODE_ID);
}

nw::Node RouteNetwork::getDestinationNode() const
{
  return nodes.value(DESTINATION_NODE_ID);
}

void RouteNetwork::cleanDestNodeEdges()
{
  // Remove all references to destination node
  for(int i : destinationNodePredecessors)
  {
    if(nodes.contains(i))
    {
      QVector<Edge>& edges = nodes[i].edges;

      QVector<Edge>::iterator it = std::remove_if(edges.begin(), edges.end(),
                                                  [ = ](const Edge &e)->bool
                                                  {
                                                    return e.toNodeId == DESTINATION_NODE_ID;
                                                  });

      if(it == edges.end())
        qWarning() << "No node destination predecessors deleted for node" << nodes.value(i).id;
      else
        edges.erase(it, edges.end());
    }
    else
      qWarning() << "No node destination found" << nodes.value(i).id;
  }
  destinationNodePredecessors.clear();
}

void RouteNetwork::addDestNodeEdges(nw::Node& node)
{
  if(destinationNodeRect.contains(node.pos) && !destinationNodePredecessors.contains(node.id) &&
     node.id != DESTINATION_NODE_ID)
  {
    nw::Edge edge(DESTINATION_NODE_ID,
                  static_cast<int>(node.pos.distanceMeterTo(destinationPos)));

#ifdef DEBUG_ROUTE
    printEdgeDebug(edge);
#endif

    // Near destination - add as successor
    node.edges.append(edge);
    // Remember for later cleanup
    destinationNodePredecessors.insert(node.id);
  }
}

nw::Node RouteNetwork::getNodeByNavId(int id, nw::Type type)
{
  nodeByNavIdQuery->bindValue(":id", id);
  nodeByNavIdQuery->bindValue(":type", type);
  nodeByNavIdQuery->exec();

  if(nodeByNavIdQuery->next())
    return fetchNode(nodeByNavIdQuery->value("node_id").toInt());
  else
  {
    if(type == nw::WAYPOINT_JET || type == nw::WAYPOINT_VICTOR)
    {
      nodeByNavIdQuery->bindValue(":type", nw::WAYPOINT_BOTH);
      nodeByNavIdQuery->exec();
      if(nodeByNavIdQuery->next())
        return fetchNode(nodeByNavIdQuery->value("node_id").toInt());
    }
  }

  return Node();
}

void RouteNetwork::getNavIdAndTypeForNode(int nodeId, int& navId, nw::Type& type)
{
  if(nodeId == START_NODE_ID)
  {
    type = START;
    navId = -1;
  }
  else if(nodeId == DESTINATION_NODE_ID)
  {
    type = DESTINATION;
    navId = -1;
  }
  else
  {
    nodeNavIdAndTypeQuery->bindValue(":id", nodeId);
    nodeNavIdAndTypeQuery->exec();

    if(nodeNavIdAndTypeQuery->next())
    {
      navId = nodeNavIdAndTypeQuery->value("nav_id").toInt();
      type = static_cast<nw::Type>(nodeNavIdAndTypeQuery->value("type").toInt());
    }
    else
    {
      navId = -1;
      type = nw::NONE;
    }
  }
}

nw::Node RouteNetwork::fetchNode(float lonx, float laty, bool loadSuccessors, int id)
{
  nodes.remove(id);

  Node node;
  node.id = id;
  node.range = 0;

  if(id == START_NODE_ID)
    node.type = START;
  else if(id == DESTINATION_NODE_ID)
    node.type = DESTINATION;
  else
    node.type = NONE;

  node.pos.setLonX(lonx);
  node.pos.setLatY(laty);

  if(loadSuccessors)
  {
    Rect queryRect(Pos(lonx, laty), NODE_SEARCH_RADIUS);
    QSet<Edge> tempEdges;
    tempEdges.reserve(1000);

    for(const Rect& rect : queryRect.splitAtAntiMeridian())
    {
      bindCoordRect(rect, nearestNodesQuery);
      nearestNodesQuery->exec();
      while(nearestNodesQuery->next())
      {
        int nodeId = nearestNodesQuery->value("node_id").toInt();
        if(checkType(static_cast<nw::Type>(nearestNodesQuery->value("type").toInt())))
        {
          // TODO build edge add minAltFt and type
          Pos otherPos(nearestNodesQuery->value("lonx").toFloat(), nearestNodesQuery->value("laty").toFloat());
          tempEdges.insert(Edge(nodeId, static_cast<int>(node.pos.distanceMeterTo(otherPos))));
        }
      }
    }
    node.edges = tempEdges.values().toVector();
    addDestNodeEdges(node);
  }

  nodes.insert(node.id, node);

  return node;
}

nw::Node RouteNetwork::fetchNode(int id)
{
  if(nodes.contains(id))
    return nodes.value(id);

  nodeByIdQuery->bindValue(":id", id);
  nodeByIdQuery->exec();

  if(nodeByIdQuery->next())
  {
    nw::Node node = createNode(nodeByIdQuery->record());
    node.id = id;

    QSet<Edge> tempEdges;
    tempEdges.reserve(1000);

    edgeToQuery->bindValue(":id", id);
    edgeToQuery->exec();

    while(edgeToQuery->next())
    {
      int nodeId = edgeToQuery->value("to_node_id").toInt();
      if(nodeId != id && checkType(static_cast<nw::Type>(edgeToQuery->value("to_node_type").toInt())))
        tempEdges.insert(createEdge(edgeToQuery->record(), nodeId));
    }

    edgeFromQuery->bindValue(":id", id);
    edgeFromQuery->exec();

    while(edgeFromQuery->next())
    {
      int nodeId = edgeFromQuery->value("from_node_id").toInt();
      if(nodeId != id && checkType(static_cast<nw::Type>(edgeFromQuery->value("from_node_type").toInt())))
        tempEdges.insert(createEdge(edgeFromQuery->record(), nodeId));
    }

    node.edges = tempEdges.values().toVector();
    addDestNodeEdges(node);

    nodes.insert(node.id, node);
    return node;
  }
  return Node();
}

void RouteNetwork::initQueries()
{
  QString nodeCols = nodeExtraCols.join(",");
  if(!nodeExtraCols.isEmpty())
    nodeCols.append(", ");

  QString edgeCols = edgeExtraCols.join(",");
  if(!edgeExtraCols.isEmpty())
    edgeCols.append(", ");

  nodeByNavIdQuery = new SqlQuery(db);
  nodeByNavIdQuery->prepare("select node_id from " + nodeTable + " where nav_id = :id and type = :type");

  nodeNavIdAndTypeQuery = new SqlQuery(db);
  nodeNavIdAndTypeQuery->prepare("select nav_id, type from " + nodeTable + " where node_id = :id");

  nearestNodesQuery = new SqlQuery(db);
  nearestNodesQuery->prepare(
    "select node_id, type, lonx, laty from " + nodeTable +
    " where lonx between :leftx and :rightx and laty between :bottomy and :topy");

  nodeByIdQuery = new SqlQuery(db);
  nodeByIdQuery->prepare(
    "select " + nodeCols + " type, lonx, laty from " + nodeTable + " where node_id = :id");

  edgeToQuery = new SqlQuery(db);
  edgeToQuery->prepare(
    "select " + edgeCols + " to_node_id, to_node_type from " + edgeTable +
    " where from_node_id = :id");

  edgeFromQuery = new SqlQuery(db);
  edgeFromQuery->prepare(
    "select " + edgeCols + " from_node_id, from_node_type from " + edgeTable +
    " where to_node_id = :id");
}

void RouteNetwork::deInitQueries()
{
  delete nodeByNavIdQuery;
  nodeByNavIdQuery = nullptr;

  delete nodeNavIdAndTypeQuery;
  nodeNavIdAndTypeQuery = nullptr;

  delete nearestNodesQuery;
  nearestNodesQuery = nullptr;

  delete nodeByIdQuery;
  nodeByIdQuery = nullptr;

  delete edgeToQuery;
  edgeToQuery = nullptr;

  delete edgeFromQuery;
  edgeFromQuery = nullptr;
}

nw::Node RouteNetwork::createNode(const SqlRecord& rec)
{
  updateNodeIndexes(rec);
  Node node;
  node.type = static_cast<nw::Type>(rec.valueInt(nodeTypeIndex));
  if(nodeRangeIndex != -1)
    node.range = rec.valueInt(nodeRangeIndex);
  node.pos.setLonX(rec.valueFloat(nodeLonXIndex));
  node.pos.setLatY(rec.valueFloat(nodeLatYIndex));
  return node;
}

void RouteNetwork::updateNodeIndexes(const SqlRecord& rec)
{
  if(!nodeIndexesCreated)
  {
    nodeTypeIndex = rec.indexOf("type");
    if(rec.contains("range"))
      nodeRangeIndex = rec.indexOf("range");
    else
      nodeRangeIndex = -1;
    nodeLonXIndex = rec.indexOf("lonx");
    nodeLatYIndex = rec.indexOf("laty");
    nodeIndexesCreated = true;
  }
}

nw::Edge RouteNetwork::createEdge(const atools::sql::SqlRecord& rec, int toNodeId)
{
  updateEdgeIndexes(rec);
  Edge edge;
  edge.toNodeId = toNodeId;
  if(edgeTypeIndex != -1)
    edge.type = static_cast<nw::Type>(rec.valueInt(edgeTypeIndex));
  if(edgeMinAltIndex != -1)
    edge.minAltFt = rec.valueInt(edgeMinAltIndex);
  if(edgeAirwayIdIndex != -1)
    edge.airwayId = rec.valueInt(edgeAirwayIdIndex);
  if(edgeDistanceIndex != -1)
    edge.distanceMeter = rec.valueInt(edgeDistanceIndex);
  return edge;
}

void RouteNetwork::updateEdgeIndexes(const atools::sql::SqlRecord& rec)
{
  if(!edgeIndexesCreated)
  {
    if(rec.contains("type"))
      edgeTypeIndex = rec.indexOf("type");
    else
      edgeTypeIndex = -1;

    if(rec.contains("minimum_altitude"))
      edgeMinAltIndex = rec.indexOf("minimum_altitude");
    else
      edgeMinAltIndex = -1;

    if(rec.contains("airway_id"))
      edgeAirwayIdIndex = rec.indexOf("airway_id");
    else
      edgeAirwayIdIndex = -1;

    if(rec.contains("distance"))
      edgeDistanceIndex = rec.indexOf("distance");
    else
      edgeDistanceIndex = -1;

    edgeIndexesCreated = true;
  }
}

bool RouteNetwork::checkType(nw::Type type)
{
  switch(type)
  {
    case nw::WAYPOINT_VICTOR:
    case nw::WAYPOINT_JET:
    case nw::WAYPOINT_BOTH:
      return mode & ROUTE_JET || mode & ROUTE_VICTOR;

    case nw::VOR:
      return mode & ROUTE_VOR;

    case nw::VORDME:
      return mode & ROUTE_VORDME;

    case nw::DME:
      return mode & ROUTE_DME;

    case nw::NDB:
      return mode & ROUTE_NDB;

    case nw::START:
    case nw::DESTINATION:
      return true;

    case nw::NONE:
      break;
  }

  return false;
}

void RouteNetwork::bindCoordRect(const atools::geo::Rect& rect, atools::sql::SqlQuery *query)
{
  query->bindValue(":leftx", rect.getWest());
  query->bindValue(":rightx", rect.getEast());
  query->bindValue(":bottomy", rect.getSouth());
  query->bindValue(":topy", rect.getNorth());
}

#ifdef DEBUG_ROUTE
void RouteNetwork::printNodeDebug(const nw::Node& node)
{
  SqlQuery q(db);
  q.prepare(
    "select * from waypoint  w  join route_node_airway n on w.waypoint_id = n.nav_id  where n.node_id = :id");
  q.bindValue(":id", node.id);
  q.exec();
  int i = 0;
  while(q.next())
  {
    qDebug() << "Node #" << i++ << "ident" << q.value("ident").toString()
             << "id" << node.id << "range" << QString::number(node.range, 'f', 0)
             << "type" << node.type << "pos" << node.pos;

  }

}

void RouteNetwork::printEdgeDebug(const nw::Edge& edge)
{
  SqlQuery q(db);
  q.prepare(
    "select * from airway a join route_edge_airway e on a.airway_id = e.airway_id where e.edge_id = :id");

  q.bindValue(":id", edge.id);
  q.exec();
  int i = 0;
  while(q.next())
  {
    qDebug() << "Edge #" << i++ << "name" << q.value("airway_name").toString()
             << "type" << q.value("airway_type").toString()
             << "to node id" << edge.toNodeId << "airway id" << edge.airwayId
             << "dist" << QString::number(edge.distanceMeter, 'f', 0)
             << "minalt" << edge.minAltFt << "type" << edge.type;
  }
}

#endif
