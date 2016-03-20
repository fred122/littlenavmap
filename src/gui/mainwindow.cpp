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

#include "gui/mainwindow.h"

#include "gui/dialog.h"
#include "gui/errorhandler.h"
#include "sql/sqldatabase.h"
#include "settings/settings.h"
#include "logging/loggingdefs.h"
#include "logging/logginghandler.h"
#include "gui/translator.h"
#include "fs/fspaths.h"
#include "table/search.h"
#include "mapgui/navmapwidget.h"
#include "table/formatter.h"
#include "table/airportsearch.h"
#include "table/navsearch.h"

#include <marble/MarbleModel.h>
#include <marble/GeoDataPlacemark.h>
#include <marble/GeoDataDocument.h>
#include <marble/GeoDataTreeModel.h>
#include <marble/MarbleWidgetPopupMenu.h>
#include <marble/MarbleWidgetInputHandler.h>
#include <marble/GeoDataStyle.h>
#include <marble/GeoDataIconStyle.h>
#include <marble/RenderPlugin.h>
#include <marble/MarbleDirs.h>
#include <marble/QtMarbleConfigDialog.h>
#include <marble/MarbleDebug.h>

#include <QCloseEvent>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QSettings>

#include "settings/settings.h"
#include "fs/bglreaderoptions.h"
#include "table/controller.h"
#include "gui/widgetstate.h"
#include "gui/tablezoomhandler.h"
#include "fs/bglreaderprogressinfo.h"
#include "fs/navdatabase.h"
#include "table/searchcontroller.h"
#include "gui/helphandler.h"

#include "sql/sqlutil.h"

#include "ui_mainwindow.h"

using namespace Marble;
using atools::settings::Settings;

const int MAP_DEFAULT_DETAIL_FACTOR = 10;
const int MAP_MAX_DETAIL_FACTOR = 15;
const int MAP_MIN_DETAIL_FACTOR = 5;

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent), ui(new Ui::MainWindow)
{
  qDebug() << "MainWindow constructor";

  QString aboutMessage =
    tr("<p>is a fast flight planner and airport search tool for FSX.</p>"
         "<p>This software is licensed under "
           "<a href=\"http://www.gnu.org/licenses/gpl-3.0\">GPL3</a> or any later version.</p>"
             "<p>The source code for this application is available at "
               "<a href=\"https://github.com/albar965\">Github</a>.</p>"
                 "<p><b>Copyright 2015-2016 Alexander Barthel (albar965@mailbox.org).</b></p>");

  dialog = new atools::gui::Dialog(this);
  errorHandler = new atools::gui::ErrorHandler(this);
  helpHandler = new atools::gui::HelpHandler(this, aboutMessage, GIT_REVISION);

  ui->setupUi(this);
  setupUi();

  openDatabase();

  createNavMap();

  // Have to create searches in the same order as the tabs
  searchController = new SearchController(this, &db, ui->tabWidgetSearch);
  searchController->createAirportSearch(ui->tableViewAirportSearch);
  searchController->createNavSearch(ui->tableViewNavSearch);

  connectAllSlots();
  readSettings();
  updateActionStates();

  navMapWidget->setTheme(mapThemeComboBox->currentData().toString(), mapThemeComboBox->currentIndex());
  navMapWidget->setProjection(mapProjectionComboBox->currentIndex());
  setMapDetail(mapDetailFactor);

  // Wait until everything is set up
  updateMapShowFeatures();
  navMapWidget->showSavedPos();
  searchController->updateTableSelection();
}

MainWindow::~MainWindow()
{
  delete searchController;
  delete ui;

  qDebug() << "MainWindow destructor";

  delete dialog;
  delete errorHandler;
  delete progressDialog;

  atools::settings::Settings::shutdown();
  atools::gui::Translator::unload();

  closeDatabase();

  qDebug() << "MainWindow destructor about to shut down logging";
  atools::logging::LoggingHandler::shutdown();

}

void MainWindow::createNavMap()
{
  // Create a Marble QWidget without a parent
  navMapWidget = new NavMapWidget(this, &db);

  navMapWidget->setVolatileTileCacheLimit(512 * 1024);

  // mapWidget->setShowSunShading(true);

  // mapWidget->model()->addGeoDataFile("/home/alex/ownCloud/Flight Simulator/FSX/Airports KML/NA Blue.kml");
  // mapWidget->model()->addGeoDataFile( "/home/alex/Downloads/map.osm" );

  ui->verticalLayout_10->replaceWidget(ui->mapWidgetDummy, navMapWidget);

  QSet<QString> pluginEnable;
  pluginEnable << "Compass" << "Coordinate Grid" << "License" << "Scale Bar" << "Navigation"
               << "Overview Map" << "Position Marker" << "Elevation Profile" << "Elevation Profile Marker"
               << "Download Progress Indicator";

  // pluginDisable << "Annotation" << "Amateur Radio Aprs Plugin" << "Atmosphere" << "Compass" <<
  // "Crosshairs" << "Earthquakes" << "Eclipses" << "Elevation Profile" << "Elevation Profile Marker" <<
  // "Places" << "GpsInfo" << "Coordinate Grid" << "License" << "Scale Bar" << "Measure Tool" << "Navigation" <<
  // "OpenCaching.Com" << "OpenDesktop Items" << "Overview Map" << "Photos" << "Position Marker" <<
  // "Postal Codes" << "Download Progress Indicator" << "Routing" << "Satellites" << "Speedometer" << "Stars" <<
  // "Sun" << "Weather" << "Wikipedia Articles";

  // QList<RenderPlugin *> localRenderPlugins = mapWidget->renderPlugins();
  // for(RenderPlugin *p : localRenderPlugins)
  // if(!pluginEnable.contains(p->name()))
  // {
  // qDebug() << "Disabled plugin" << p->name();
  // p->setEnabled(false);
  // }
  // else
  // qDebug() << "Found plugin" << p->name();

}

void MainWindow::setupUi()
{
  ui->mapToolBar->addSeparator();

  mapProjectionComboBox = new QComboBox(this);
  mapProjectionComboBox->setObjectName("mapProjectionComboBox");
  QString helpText = tr("Select Map Theme");
  mapProjectionComboBox->setToolTip(helpText);
  mapProjectionComboBox->setStatusTip(helpText);
  mapProjectionComboBox->addItem(tr("Spherical"), Marble::Spherical);
  mapProjectionComboBox->addItem(tr("Mercator"), Marble::Mercator);
  ui->mapToolBar->addWidget(mapProjectionComboBox);

  mapThemeComboBox = new QComboBox(this);
  mapThemeComboBox->setObjectName("mapThemeComboBox");
  helpText = tr("Select Map Theme");
  mapThemeComboBox->setToolTip(helpText);
  mapThemeComboBox->setStatusTip(helpText);
  mapThemeComboBox->addItem(tr("OpenStreenMap"),
                            "earth/openstreetmap/openstreetmap.dgml");
  mapThemeComboBox->addItem(tr("OpenStreenMap with Elevation"),
                            "earth/openstreetmap/openstreetmap.dgml");
  mapThemeComboBox->addItem(tr("Atlas"),
                            "earth/srtm/srtm.dgml");
  mapThemeComboBox->addItem(tr("Blue Marble"),
                            "earth/bluemarble/bluemarble.dgml");
  mapThemeComboBox->addItem(tr("Simple"),
                            "earth/plain/plain.dgml");
  mapThemeComboBox->addItem(tr("Political"),
                            "earth/political/political.dgml");
  // mapThemeComboBox->addItem(tr("MapQuest Open Aerial"),
  // "earth/mapquest-open-aerial/mapquest-open-aerial.dgml");
  // mapThemeComboBox->addItem(tr("MapQuest OpenStreenMap"),
  // "earth/mapquest-osm/mapquest-osm.dgml");
  // mapThemeComboBox->addItem(tr("Natural Earth")
  // "earth/naturalearth2shading/naturalearth2shading.dgml");
  ui->mapToolBar->addWidget(mapThemeComboBox);

  ui->menuView->addAction(ui->mainToolBar->toggleViewAction());
  ui->menuView->addAction(ui->dockWidgetSearch->toggleViewAction());
  ui->menuView->addAction(ui->dockWidgetRoute->toggleViewAction());
  ui->menuView->addAction(ui->dockWidgetAirportInfo->toggleViewAction());

  // Create labels for the statusbar
  mapPosLabelText = tr("%1 %2. Distance %3.");
  mapPosLabel = new QLabel();
  ui->statusBar->addPermanentWidget(mapPosLabel);

  selectionLabelText = tr("%1 of %2 %3 selected, %4 visible.");
  selectionLabel = new QLabel();
  ui->statusBar->addPermanentWidget(selectionLabel);
}

void MainWindow::connectAllSlots()
{
  qDebug() << "Connecting slots";

  connect(ui->actionMapSetHome, &QAction::triggered, navMapWidget, &NavMapWidget::changeHome);

  connect(searchController->getAirportSearch(), &AirportSearch::showRect,
          navMapWidget, &NavMapWidget::showRect);
  connect(searchController->getAirportSearch(), &AirportSearch::showPos,
          navMapWidget, &NavMapWidget::showPos);
  connect(searchController->getAirportSearch(), &AirportSearch::changeMark,
          navMapWidget, &NavMapWidget::changeMark);

  connect(searchController->getNavSearch(), &NavSearch::showPos, navMapWidget, &NavMapWidget::showPos);
  connect(searchController->getNavSearch(), &NavSearch::changeMark, navMapWidget, &NavMapWidget::changeMark);

  // Use this event to show path dialog after main windows is shown
  connect(this, &MainWindow::windowShown, this, &MainWindow::mainWindowShown, Qt::QueuedConnection);

  connect(ui->actionShowStatusbar, &QAction::toggled, ui->statusBar, &QStatusBar::setVisible);
  connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);
  connect(ui->actionReloadScenery, &QAction::triggered, this, &MainWindow::loadScenery);
  connect(ui->actionOptions, &QAction::triggered, this, &MainWindow::options);

  connect(ui->actionContents, &QAction::triggered, helpHandler, &atools::gui::HelpHandler::help);
  connect(ui->actionAbout, &QAction::triggered, helpHandler, &atools::gui::HelpHandler::about);
  connect(ui->actionAbout_Qt, &QAction::triggered, helpHandler, &atools::gui::HelpHandler::aboutQt);

  // Map widget related connections
  connect(navMapWidget, &NavMapWidget::objectSelected, searchController, &SearchController::objectSelected);

  void (QComboBox::*indexChangedPtr)(int) = &QComboBox::currentIndexChanged;

  connect(mapProjectionComboBox, indexChangedPtr, [ = ](int)
          {
            Marble::Projection proj =
              static_cast<Marble::Projection>(mapProjectionComboBox->currentData().toInt());
            qDebug() << "Changing projection to" << proj;
            navMapWidget->setProjection(proj);
          });

  connect(mapThemeComboBox, indexChangedPtr, navMapWidget, [ = ](int index)
          {
            QString theme = mapThemeComboBox->currentData().toString();
            qDebug() << "Changing theme to" << theme << "index" << index;
            navMapWidget->setTheme(theme, index);
          });

  connect(ui->actionMapShowCities, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowAirports, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowSoftAirports, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowEmptyAirports, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowVor, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowNdb, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowWp, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);
  connect(ui->actionMapShowIls, &QAction::toggled, this, &MainWindow::updateMapShowFeatures);

  connect(ui->actionMapShowMark, &QAction::triggered, navMapWidget, &NavMapWidget::showMark);
  connect(ui->actionMapShowHome, &QAction::triggered, navMapWidget, &NavMapWidget::showHome);
  connect(ui->actionMapBack, &QAction::triggered, navMapWidget, &NavMapWidget::historyBack);
  connect(ui->actionMapNext, &QAction::triggered, navMapWidget, &NavMapWidget::historyNext);
  connect(ui->actionMapMoreDetails, &QAction::triggered, this, &MainWindow::increaseMapDetail);
  connect(ui->actionMapLessDetails, &QAction::triggered, this, &MainWindow::decreaseMapDetail);
  connect(ui->actionMapDefaultDetails, &QAction::triggered, this, &MainWindow::defaultMapDetail);

  connect(navMapWidget->getHistory(), &MapPosHistory::historyChanged, this, &MainWindow::updateHistActions);

  connect(searchController->getAirportSearch(), &Search::selectionChanged,
          this, &MainWindow::selectionChanged);
  connect(searchController->getNavSearch(), &Search::selectionChanged,
          this, &MainWindow::selectionChanged);
}

void MainWindow::defaultMapDetail()
{
  mapDetailFactor = MAP_DEFAULT_DETAIL_FACTOR;
  setMapDetail(mapDetailFactor);
}

void MainWindow::increaseMapDetail()
{
  if(mapDetailFactor < MAP_MAX_DETAIL_FACTOR)
  {
    mapDetailFactor++;
    setMapDetail(mapDetailFactor);
  }
}

void MainWindow::decreaseMapDetail()
{
  if(mapDetailFactor > MAP_MIN_DETAIL_FACTOR)
  {
    mapDetailFactor--;
    setMapDetail(mapDetailFactor);
  }
}

void MainWindow::setMapDetail(int factor)
{
  mapDetailFactor = factor;
  navMapWidget->setDetailFactor(mapDetailFactor);
  ui->actionMapMoreDetails->setEnabled(mapDetailFactor < MAP_MAX_DETAIL_FACTOR);
  ui->actionMapLessDetails->setEnabled(mapDetailFactor > MAP_MIN_DETAIL_FACTOR);
  ui->actionMapDefaultDetails->setEnabled(mapDetailFactor != MAP_DEFAULT_DETAIL_FACTOR);
  navMapWidget->update();
}

void MainWindow::selectionChanged(const Search *source, int selected, int visible, int total)
{
  QString type;
  if(source == searchController->getAirportSearch())
    type = tr("Airports");
  else if(source == searchController->getNavSearch())
    type = tr("Navaids");

  // selectionLabelText = tr("%1 of %2 %3 selected, %4 visible.");
  selectionLabel->setText(selectionLabelText.arg(selected).arg(total).arg(type).arg(visible));

  maptypes::MapSearchResult result;
  searchController->getSelectedMapObjects(result);
  navMapWidget->changeHighlight(result);
}

void MainWindow::updateMapShowFeatures()
{
  navMapWidget->setShowMapPois(ui->actionMapShowCities->isChecked());

  navMapWidget->setShowMapFeatures(maptypes::AIRPORT_HARD,
                                   ui->actionMapShowAirports->isChecked());
  navMapWidget->setShowMapFeatures(maptypes::AIRPORT_SOFT,
                                   ui->actionMapShowSoftAirports->isChecked());
  navMapWidget->setShowMapFeatures(maptypes::AIRPORT_EMPTY,
                                   ui->actionMapShowEmptyAirports->isChecked());

  navMapWidget->setShowMapFeatures(maptypes::AIRPORT,
                                   ui->actionMapShowAirports->isChecked() ||
                                   ui->actionMapShowSoftAirports->isChecked() ||
                                   ui->actionMapShowEmptyAirports->isChecked());

  navMapWidget->setShowMapFeatures(maptypes::VOR, ui->actionMapShowVor->isChecked());
  navMapWidget->setShowMapFeatures(maptypes::NDB, ui->actionMapShowNdb->isChecked());
  navMapWidget->setShowMapFeatures(maptypes::ILS, ui->actionMapShowIls->isChecked());
  navMapWidget->setShowMapFeatures(maptypes::WAYPOINT, ui->actionMapShowWp->isChecked());
  navMapWidget->update();
}

void MainWindow::updateHistActions(int minIndex, int curIndex, int maxIndex)
{
  qDebug() << "History changed min" << minIndex << "cur" << curIndex << "max" << maxIndex;
  ui->actionMapBack->setEnabled(curIndex > minIndex);
  ui->actionMapNext->setEnabled(curIndex < maxIndex);
}

void MainWindow::createEmptySchema()
{
  if(!atools::sql::SqlUtil(&db).hasTable("airport"))
  {
    atools::fs::BglReaderOptions opts;
    atools::fs::Navdatabase nd(&opts, &db);
    nd.createSchema();
  }
}

void MainWindow::loadScenery()
{
  preDatabaseLoad();
  using atools::fs::BglReaderOptions;
  QString config = Settings::getOverloadedPath(":/littlenavmap/resources/config/navdatareader.cfg");
  QSettings settings(config, QSettings::IniFormat);

  BglReaderOptions opts;
  opts.loadFromSettings(settings);

  progressDialog = new QProgressDialog(this);
  QLabel *label = new QLabel(progressDialog);
  label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  label->setIndent(10);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setMinimumWidth(600);

  progressDialog->setWindowModality(Qt::WindowModal);
  progressDialog->setLabel(label);
  progressDialog->setAutoClose(false);
  progressDialog->setAutoReset(false);
  progressDialog->setMinimumDuration(0);
  progressDialog->show();

  atools::fs::fstype::SimulatorType type = atools::fs::fstype::FSX;
  QString sceneryFile = atools::fs::FsPaths::getSceneryLibraryPath(type);
  QString basepath = atools::fs::FsPaths::getBasePath(type);

  opts.setSceneryFile(sceneryFile);
  opts.setBasepath(basepath);

  QElapsedTimer timer;
  using namespace std::placeholders;
  opts.setProgressCallback(std::bind(&MainWindow::progressCallback, this, _1, timer));

  // Let the dialog close and show the busy pointer
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  atools::fs::Navdatabase nd(&opts, &db);
  nd.create();

  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if(!progressDialog->wasCanceled())
  {
  progressDialog->setCancelButtonText(tr("&OK"));
  progressDialog->exec();
  }
  delete progressDialog;
  progressDialog = nullptr;

  postDatabaseLoad(false);
}

bool MainWindow::progressCallback(const atools::fs::BglReaderProgressInfo& progress, QElapsedTimer& timer)
{
  if(progress.isFirstCall())
  {
    timer.start();
    progressDialog->setMinimum(0);
    progressDialog->setMaximum(progress.getTotal());
  }
  progressDialog->setValue(progress.getCurrent());

  static const QString table(tr("<table>"
                                  "<tbody>"
                                    "<tr> "
                                      "<td width=\"60\"><b>Files:</b>"
                                      "</td>    "
                                      "<td width=\"60\">%L5"
                                      "</td> "
                                      "<td width=\"60\"><b>VOR:</b>"
                                      "</td> "
                                      "<td width=\"60\">%L7"
                                      "</td> "
                                      "<td width=\"60\"><b>Marker:</b>"
                                      "</td>     "
                                      "<td width=\"60\">%L10"
                                      "</td>"
                                    "</tr>"
                                    "<tr> "
                                      "<td width=\"60\"><b>Airports:</b>"
                                      "</td> "
                                      "<td width=\"60\">%L6"
                                      "</td> "
                                      "<td width=\"60\"><b>ILS:</b>"
                                      "</td> "
                                      "<td width=\"60\">%L8"
                                      "</td> "
                                      "<td width=\"60\"><b>Boundaries:</b>"
                                      "</td> <td width=\"60\">%L11"
                                    "</td>"
                                  "</tr>"
                                  "<tr> "
                                    "<td width=\"60\">"
                                    "</td>"
                                    "<td width=\"60\">"
                                    "</td>"
                                    "<td width=\"60\"><b>NDB:</b>"
                                    "</td> "
                                    "<td width=\"60\">%L9"
                                    "</td> "
                                    "<td width=\"60\"><b>Waypoints:"
                                    "</b>"
                                  "</td>  "
                                  "<td width=\"60\">%L12"
                                  "</td>"
                                "</tr>"
                              "</tbody>"
                            "</table>"
                                ));

  static const QString text(tr(
                              "<b>%1</b><br/><br/><br/>"
                              "<b>Time:</b> %2<br/>%3%4"
                              ) + table);

  static const QString textWithFile(tr(
                                      "<b>Scenery:</b> %1 (%2)<br/>"
                                      "<b>File:</b> %3<br/><br/>"
                                      "<b>Time:</b> %4<br/>"
                                      ) + table);

  if(progress.isNewOther())
    progressDialog->setLabelText(
      text.arg(progress.getOtherAction()).
      arg(formatter::formatElapsed(timer)).
      arg(QString()).
      arg(QString()).
      arg(progress.getNumFiles()).
      arg(progress.getNumAirports()).
      arg(progress.getNumVors()).
      arg(progress.getNumIls()).
      arg(progress.getNumNdbs()).
      arg(progress.getNumMarker()).
      arg(progress.getNumBoundaries()).
      arg(progress.getNumWaypoints()));
  else if(progress.isNewSceneryArea() || progress.isNewFile())
    progressDialog->setLabelText(
      textWithFile.arg(progress.getSceneryTitle()).
      arg(progress.getSceneryPath()).
      arg(progress.getBglFilename()).
      arg(formatter::formatElapsed(timer)).
      arg(progress.getNumFiles()).
      arg(progress.getNumAirports()).
      arg(progress.getNumVors()).
      arg(progress.getNumIls()).
      arg(progress.getNumNdbs()).
      arg(progress.getNumMarker()).
      arg(progress.getNumBoundaries()).
      arg(progress.getNumWaypoints()));
  else if(progress.isLastCall())
    progressDialog->setLabelText(
      text.arg(tr("Done")).
      arg(formatter::formatElapsed(timer)).
      arg(QString()).
      arg(QString()).
      arg(progress.getNumFiles()).
      arg(progress.getNumAirports()).
      arg(progress.getNumVors()).
      arg(progress.getNumIls()).
      arg(progress.getNumNdbs()).
      arg(progress.getNumMarker()).
      arg(progress.getNumBoundaries()).
      arg(progress.getNumWaypoints()));

  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  return progressDialog->wasCanceled();
}

void MainWindow::options()
{
  // QtMarbleConfigDialog dlg(mapWidget);
  // dlg.exec();

}

void MainWindow::mainWindowShown()
{
  qDebug() << "MainWindow::mainWindowShown()";
}

void MainWindow::updateActionStates()
{
  qDebug() << "Updating action states";
  ui->actionShowStatusbar->setChecked(!ui->statusBar->isHidden());
}

void MainWindow::openDatabase()
{
  try
  {
    using atools::sql::SqlDatabase;

    // Get a file in the organization specific directory with an application
    // specific name (i.e. Linux: ~/.config/ABarthel/little_logbook.sqlite)
    databaseFile = atools::settings::Settings::getConfigFilename(".sqlite");

    qDebug() << "Opening database" << databaseFile;
    db = SqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(databaseFile);
    db.open({"PRAGMA foreign_keys = ON"});

    createEmptySchema();
  }
  catch(std::exception& e)
  {
    errorHandler->handleException(e, "While opening database");
  }
  catch(...)
  {
    errorHandler->handleUnknownException("While opening database");
  }
}

void MainWindow::closeDatabase()
{
  try
  {
    using atools::sql::SqlDatabase;

    qDebug() << "Closing database" << databaseFile;
    db.close();
  }
  catch(std::exception& e)
  {
    errorHandler->handleException(e, "While closing database");
  }
  catch(...)
  {
    errorHandler->handleUnknownException("While closing database");
  }
}

void MainWindow::readSettings()
{
  qDebug() << "readSettings";

  atools::gui::WidgetState ws("MainWindow/Widget");
  ws.restore({this, ui->statusBar, ui->tabWidgetSearch});

  searchController->restoreState();
  navMapWidget->restoreState();

  ws.restore({mapProjectionComboBox, mapThemeComboBox,
              ui->actionMapShowAirports, ui->actionMapShowSoftAirports, ui->actionMapShowEmptyAirports,
              ui->actionMapShowVor, ui->actionMapShowNdb, ui->actionMapShowWp,
              ui->actionMapShowIls, ui->actionMapShowCities});

  mapDetailFactor = atools::settings::Settings::instance()->value("Map/DetailFactor",
                                                                  MAP_DEFAULT_DETAIL_FACTOR).toInt();

}

void MainWindow::writeSettings()
{
  qDebug() << "writeSettings";

  atools::gui::WidgetState ws("MainWindow/Widget");
  ws.save({this, ui->statusBar, ui->tabWidgetSearch});

  searchController->saveState();
  navMapWidget->saveState();

  ws.save({mapProjectionComboBox, mapThemeComboBox,
           ui->actionMapShowAirports, ui->actionMapShowSoftAirports, ui->actionMapShowEmptyAirports,
           ui->actionMapShowVor, ui->actionMapShowNdb, ui->actionMapShowWp, ui->actionMapShowIls,
           ui->actionMapShowCities});

  atools::settings::Settings::instance()->setValue("Map/DetailFactor", mapDetailFactor);

  ws.syncSettings();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  // Catch all close events like Ctrl-Q or Menu/Exit or clicking on the
  // close button on the window frame
  qDebug() << "closeEvent";
  int result = dialog->showQuestionMsgBox("Actions/ShowQuit",
                                          tr("Really Quit?"),
                                          tr("Do not &show this dialog again."),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No, QMessageBox::Yes);

  if(result != QMessageBox::Yes)
    event->ignore();
  writeSettings();
}

void MainWindow::showEvent(QShowEvent *event)
{
  if(firstStart)
  {
    emit windowShown();
    firstStart = false;
  }

  event->ignore();
}

void MainWindow::preDatabaseLoad()
{
  if(!hasDatabaseLoadStatus)
  {
    hasDatabaseLoadStatus = true;

    searchController->preDatabaseLoad();
    navMapWidget->preDatabaseLoad();
  }
  else
    qDebug() << "Already in database loading status";
}

void MainWindow::postDatabaseLoad(bool force)
{
  if(hasDatabaseLoadStatus || force)
  {
    searchController->postDatabaseLoad();
    navMapWidget->postDatabaseLoad();
    hasDatabaseLoadStatus = false;
  }
  else
    qDebug() << "Not in database loading status";
}