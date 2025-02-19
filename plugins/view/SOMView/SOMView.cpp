/**
 *
 * This file is part of Tulip (http://tulip.labri.fr)
 *
 * Authors: David Auber and the Tulip development Team
 * from LaBRI, University of Bordeaux
 *
 * Tulip is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * Tulip is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 */

#include "SOMView.h"

#include "SOMPreviewComposite.h"
#include "SOMMapElement.h"
#include "ThresholdInteractor.h"
#include "EditColorScaleInteractor.h"
#include "SOMMap.h"
#include "ZoomUtils.h"
#include "SOMViewInteractor.h"
#include "SOMPropertiesWidget.h"

#include <tulip/GlGraphRenderingParameters.h>
#include <tulip/NumericProperty.h>
#include <tulip/ColorProperty.h>
#include <tulip/BooleanProperty.h>
#include <tulip/GlBoundingBoxSceneVisitor.h>
#include <tulip/Interactor.h>
#include <tulip/ImportModule.h>
#include <tulip/GlLODCalculator.h>
#include <tulip/GlLabel.h>
#include <tulip/LayoutProperty.h>
#include <tulip/GlMainWidget.h>
#include <tulip/GlGraphComposite.h>

#include <QMenu>
#include <QTimer>
#include <QToolTip>
#include <QMessageBox>
#include <QMouseEvent>

using namespace std;
using namespace tlp;

PLUGIN(SOMView)

SOMView::SOMView(PluginContext *)
    : GlMainView(true), graphComposite(nullptr), graphLayoutProperty(nullptr),
      graphSizeProperty(nullptr), mask(nullptr), somMask(nullptr), mapCompositeElements(nullptr),
      som(nullptr), previewWidget(nullptr), mapWidget(nullptr), isDetailedMode(false),
      mappingIsVisible(false), hideMappingAction(nullptr), showMappingAction(nullptr),
      computeMappingAction(nullptr), updateNodesColorAction(nullptr),
      addSelectionToMaskAction(nullptr), clearMaskAction(nullptr), invertMaskAction(nullptr),
      selectNodesInMaskAction(nullptr), properties(nullptr), destruct(false), somMapIsBuild(false),
      isConstruct(false) {
  addDependency("Grid", "2.0");
}

SOMView::~SOMView() {

  //  clearObservers();
  inputSample.removeObserver(this);
  destruct = true;

  if (somMapIsBuild) {
    delete mask;
    mask = nullptr;

    // Clear the color properties
    for (auto &it : propertyToColorProperty) {
      delete it.second;
    }

    propertyToColorProperty.clear();

    // Destroy the map
    delete som;
    som = nullptr;
  }

  delete graphLayoutProperty;
  delete properties;

  if (previewWidget && previewWidget == getGlMainWidget())
    delete mapWidget;
  else
    delete previewWidget;
}

ColorScale *SOMView::getColorScale() {
  return properties->getDefaultColorScale();
}

void SOMView::construct(QWidget *) {

  isConstruct = true;

  destruct = false;
  // GUI initialization
  inputSample.addObserver(this);

  // Properties initialiaztion
  properties = new SOMPropertiesWidget(this, nullptr);

  previewWidget = new GlMainWidget(nullptr, nullptr);
  previewWidget->installEventFilter(this);

  mapWidget = new GlMainWidget(nullptr, this);
  mapWidget->installEventFilter(this);

  initGlMainViews();
  mapWidget->installEventFilter(this);

  isDetailedMode = false;

  // Interactors update
  previewWidget->installEventFilter(&navigator);
  previewWidget->installEventFilter(this);

  // Init var
  graphLayoutProperty = nullptr;
  graphSizeProperty = nullptr;
  mask = nullptr;
  mappingIsVisible = true;

  // menu initialization
  initMenu();

  // Init the map to nullptr
  som = nullptr;
}

void SOMView::initGlMainViews() {

  // Gl view initialisation
  // miniatures

  GlLayer *mainLayer = previewWidget->getScene()->getLayer("Main");

  if (mainLayer == nullptr) {
    mainLayer = new GlLayer("Main");
    previewWidget->getScene()->addExistingLayer(mainLayer);
  }

  // No graph to print
  GlGraphComposite *graphComposite = new GlGraphComposite(tlp::newGraph());
  mainLayer->addGlEntity(graphComposite, "graph");
  // activate hover

  mainLayer = mapWidget->getScene()->getLayer("Main");

  if (mainLayer == nullptr) {
    mainLayer = new GlLayer("Main");
    mapWidget->getScene()->addExistingLayer(mainLayer);
  }

  graphComposite = new GlGraphComposite(tlp::newGraph());
  mainLayer->addGlEntity(graphComposite, "graph");

  GlGraphRenderingParameters *renderingParameters = graphComposite->getRenderingParametersPointer();
  renderingParameters->setFontsType(0);

  // map
  renderingParameters->setDisplayEdges(false);
  renderingParameters->setViewEdgeLabel(false);
  renderingParameters->setViewNodeLabel(false);
  renderingParameters->setViewMetaLabel(false);
}

void SOMView::initMenu() {

  hideMappingAction = new QAction(QString("Hide Mapping"), this);
  connect(hideMappingAction, SIGNAL(triggered()), this, SLOT(hideMapping()));

  showMappingAction = new QAction(QString("Show Mapping"), this);
  connect(showMappingAction, SIGNAL(triggered()), this, SLOT(showMapping()));

  computeMappingAction = new QAction(QString("Compute Mapping"), this);
  connect(computeMappingAction, SIGNAL(triggered()), this, SLOT(computeMapping()));

  updateNodesColorAction = new QAction(QString("Update nodes color"), this);
  connect(updateNodesColorAction, SIGNAL(triggered()), this, SLOT(updateNodeColorMapping()));

  addSelectionToMaskAction = new QAction(QString("Copy Selection to mask"), this);
  connect(addSelectionToMaskAction, SIGNAL(triggered()), this, SLOT(copySelectionToMask()));

  clearMaskAction = new QAction(QString("Clear mask"), this);
  connect(clearMaskAction, SIGNAL(triggered()), this, SLOT(clearMask()));
  invertMaskAction = new QAction(QString("Invert the mask"), this);
  connect(invertMaskAction, SIGNAL(triggered()), this, SLOT(invertMask()));
  selectNodesInMaskAction = new QAction(QString("Select nodes in mask"), this);
  connect(selectNodesInMaskAction, SIGNAL(triggered()), this, SLOT(selectAllNodesInMask()));
}

void SOMView::setState(const DataSet &dataSet) {

  if (!isConstruct)
    construct(nullptr);

  isDetailedMode = false;
  assignNewGlMainWidget(previewWidget, false);

  previewWidget->makeCurrent();

  //  clearObservers();
  cleanSOMMap();

  if (graph() == nullptr)
    return;

  changeMapViewGraph(graph());
  updateInputSample();

  // update drawing dialog
  vector<string> propertyFilterType;
  propertyFilterType.push_back("double");
  propertyFilterType.push_back("int");
  properties->clearpropertiesConfigurationWidget();
  properties->addfilter(graph(), propertyFilterType);

  if (dataSet.exists("propertiesWidget")) {
    DataSet propertiesDataSet;
    dataSet.get("propertiesWidget", propertiesDataSet);
    properties->setData(propertiesDataSet);
  }

  properties->graphChanged(graph());

  // If there is no currentSOMMap build them
  if (som == nullptr) {
    buildSOMMap();
  }

  computeSOMMap();

  // Display the empty label if no properties are selected.
  if (properties->getSelectedProperties().empty())
    addEmptyViewLabel();

  registerTriggers();

  GlMainView::setState(dataSet);
}

void SOMView::changeMapViewGraph(tlp::Graph *graph) {

  GlScene *scene = mapWidget->getScene();
  scene->clearLayersList();
  GlLayer *mainLayer = new GlLayer("Main");
  scene->addExistingLayer(mainLayer);
  GlGraphComposite *glGraphComposite = new GlGraphComposite(graph);
  mainLayer->addGlEntity(glGraphComposite, "graph");
  GlGraphRenderingParameters p =
      mapWidget->getScene()->getGlGraphComposite()->getRenderingParameters();
  p.setDisplayEdges(false);
  p.setViewEdgeLabel(false);
  p.setViewMetaLabel(false);
  p.setViewNodeLabel(true);
  p.setFontsType(0);
  mapWidget->getScene()->getGlGraphComposite()->setRenderingParameters(p);
  graphComposite = mapWidget->getScene()->getGlGraphComposite();

  if (graphLayoutProperty)
    delete graphLayoutProperty;

  if (graphSizeProperty)
    delete graphSizeProperty;

  graphLayoutProperty = new LayoutProperty(graph);
  graphLayoutProperty->setAllNodeValue(Coord(0, 0, 0));
  graphComposite->getInputData()->setElementLayout(graphLayoutProperty);

  graphSizeProperty = new SizeProperty(graph);
  graphSizeProperty->setAllNodeValue(Size(0, 0, 0));
  graphComposite->getInputData()->setElementSize(graphSizeProperty);
}

DataSet SOMView::state() const {
  DataSet dataSet = GlMainView::state();
  // Store configurationWidget state.
  dataSet.set("propertiesWidget", properties->getData());
  return dataSet;
}

void SOMView::graphChanged(Graph *) {
  setState(DataSet());
}

void SOMView::fillContextMenu(QMenu *menu, const QPointF &point) {
  if (!selection.empty()) {
    menu->addAction(computeMappingAction);
    menu->addAction(updateNodesColorAction);
    menu->addSeparator();

    if (mappingIsVisible)
      menu->addAction(hideMappingAction);
    else
      menu->addAction(showMappingAction);
  }

  menu->addSeparator();
  menu->addAction(addSelectionToMaskAction);

  if (mask) {
    menu->addAction(selectNodesInMaskAction);
    menu->addAction(invertMaskAction);
    menu->addAction(clearMaskAction);
  }

  menu->addSeparator();
  GlMainView::fillContextMenu(menu, point);
}

void SOMView::createPicture(const std::string &pictureName, int width, int height) {
  createPicture(pictureName, width, height, false);
}

bool SOMView::createPicture(const std::string &pictureName, int width, int height, bool center) {
  if (isDetailedMode) {
    if (width == 0 && height == 0)
      mapWidget->createPicture(pictureName, mapWidget->width(), mapWidget->height(), center);
    else
      mapWidget->createPicture(pictureName, width, height, center);
  } else {
    if (width == 0 && height == 0)
      previewWidget->createPicture(pictureName, previewWidget->width(), previewWidget->height(),
                                   center);
    else
      previewWidget->createPicture(pictureName, width, height, center);
  }

  return true;
}

void SOMView::drawPreviews() {

  vector<string> propertiesName(properties->getSelectedProperties());
  int thumbWidth = 50;
  int thumbHeight = 50;
  int spacing = 5;
  int pos = 0;
  int colNumber = int(ceil(sqrt(double(propertiesName.size()))));

  for (auto &pName : propertiesName) {
    double minValue;
    double maxValue;
    ColorProperty *colorProperty = computePropertyColor(pName, minValue, maxValue);

    Coord previewCoord((pos % colNumber) * (thumbWidth + spacing),
                       (colNumber - 1) - int(floor(pos / colNumber)) * (thumbHeight + spacing), 0);
    Size previewSize(thumbWidth, thumbHeight, 0);

    // If the input data uses normalized values we had to translate it to get the real value.
    unsigned int propertyIndex = inputSample.findIndexForProperty(pName);
    double minimumDisplayed = inputSample.isUsingNormalizedValues()
                                  ? inputSample.unnormalize(minValue, propertyIndex)
                                  : minValue;
    double maximumDisplayed = inputSample.isUsingNormalizedValues()
                                  ? inputSample.unnormalize(maxValue, propertyIndex)
                                  : maxValue;
    SOMPreviewComposite *SOMPrevComp = new SOMPreviewComposite(
        previewCoord, previewSize, pName, colorProperty, som,
        this->properties->getPropertyColorScale(pName), minimumDisplayed, maximumDisplayed);

    propertyToPreviews[pName] = SOMPrevComp;

    previewWidget->getScene()->getLayer("Main")->addGlEntity(SOMPrevComp, pName);

    ++pos;
  }

  previewWidget->getScene()->centerScene();
}

void SOMView::clearPreviews() {

  // Destroy preview
  for (auto &it : propertyToPreviews) {
    delete it.second;
  }

  propertyToPreviews.clear();

  // Empty the main layer

  GlLayer *main;

  if (destruct)
    main = nullptr;
  else
    main = previewWidget->getScene()->getLayer("Main");

  if (main) {
    main->clear();
  }
}

void SOMView::setColorToMap(tlp::ColorProperty *newColor) {

  ColorProperty *cp = nullptr;
  bool deleteAfter;

  if (mask) {
    cp = new ColorProperty(som);
    deleteAfter = true;
    for (auto n : som->nodes()) {
      if (mask->getNodeValue(n))
        cp->setNodeValue(n, newColor->getNodeValue(n));
      else
        cp->setNodeValue(n, Color(200, 200, 200));
    }
  } else {
    cp = newColor;
    deleteAfter = false;
  }

  mapCompositeElements->updateColors(cp);

  if (properties->getLinkColor())
    updateNodeColorMapping(cp);

  if (deleteAfter)
    delete cp;
}

void SOMView::refreshSOMMap() {

  if (!selection.empty()) {

    /*mapCompositeElements->setVisible(true);
    if (mappingIsVisible)
      graphComposite->setVisible(true);*/

    ColorProperty *cp = propertyToColorProperty[selection];
    setColorToMap(cp);

  } else {
    /*mapCompositeElements->setVisible(false);
    graphComposite->setVisible(false);*/
  }
}

void SOMView::clearSOMMapView() {}

ColorProperty *SOMView::computePropertyColor(const string &propertyName, double &minValue,
                                             double &maxValue) {

  ColorProperty *propColor;

  // if don't exist create them
  if (propertyToColorProperty.find(propertyName) == propertyToColorProperty.end()) {
    propColor = new ColorProperty(som);

    propertyToColorProperty[propertyName] = propColor;
  } else {
    propColor = propertyToColorProperty[propertyName];
  }

  assert(propColor);

  // Original value
  NumericProperty *property = dynamic_cast<NumericProperty *>(som->getProperty(propertyName));
  assert(property);

  minValue = property->getNodeDoubleMin(som);
  maxValue = property->getNodeDoubleMax(som);
  ColorScale *cs = properties->getPropertyColorScale(propertyName);
  assert(cs);
  computeColor(som, property, *cs, propColor);

  return propColor;
}

void SOMView::init() {}
void SOMView::drawMapWidget() {
  if (mapWidget && mapWidget->isVisible())
    mapWidget->draw();
}
void SOMView::drawPreviewWidget() {
  if (previewWidget && previewWidget->isVisible())
    previewWidget->draw();
}

void SOMView::draw() {
  removeEmptyViewLabel();
  previewWidget->getScene()->getLayer("Main");

  if (properties->getSelectedProperties().empty())
    addEmptyViewLabel();

  getGlMainWidget()->draw(true);
}

void SOMView::refresh() {
  getGlMainWidget()->redraw();
}

void SOMView::buildSOMMap() {
  somMapIsBuild = true;
  int width = properties->getGridWidth();
  int height = properties->getGridHeight();

  SOMMap::SOMMapConnectivity connectivity = SOMMap::four;
  QString conn = properties->getConnectivityLabel();

  if (conn == "4")
    connectivity = SOMMap::four;
  else if (conn == "6")
    connectivity = SOMMap::six;
  else if (conn == "8")
    connectivity = SOMMap::eight;
  else {
    std::cerr << __PRETTY_FUNCTION__ << ":" << __LINE__ << " "
              << "Connectivity not mannaged" << std::endl;
    return;
  }

  // Only true or false
  bool oppositeConnected = properties->getOppositeConnected();

  som = new SOMMap(width, height, connectivity, oppositeConnected);

  // Build som representation
  float somMaxHeight = 50;
  float somMawWidth = 50;
  float scaleHeight = 10;
  float spacing = 5;

  Size somSize;

  // Keep aspect ratio
  if (som->getWidth() > som->getHeight()) {
    somSize.setW(somMawWidth);
    somSize.setH((somSize.getW() * som->getHeight()) / som->getWidth());
  } else {
    somSize.setH(somMaxHeight);
    somSize.setW((som->getWidth() * somSize.getH()) / som->getHeight());
  }

  mapCompositeElements =
      new SOMMapElement(Coord(0 + ((somMawWidth - somSize.getW()) / 2),
                              (scaleHeight + spacing) + ((somMaxHeight - somSize.getH()) / 2), 0),
                        somSize, som, nullptr);

  GlLayer *somLayer = mapWidget->getScene()->getLayer("Main");

  if (!somLayer) {
    somLayer = new GlLayer("som");
    mapWidget->getScene()->addExistingLayer(somLayer);
  }

  somLayer->addGlEntity(mapCompositeElements, "som");
}

void SOMView::cleanSOMMap() {

  // Destroy representation of the map
  clearPreviews();

  // Remove from layer
  GlLayer *somLayer;

  if (destruct)
    somLayer = nullptr;
  else
    somLayer = mapWidget->getScene()->getLayer("Main");

  if (somLayer)
    somLayer->deleteGlEntity(mapCompositeElements);

  if (mapCompositeElements) {
    delete mapCompositeElements;
    mapCompositeElements = nullptr;
  }

  if (mask) {
    delete mask;
    mask = nullptr;
  }

  // Clear the color properties
  for (auto it = propertyToColorProperty.begin(); it != propertyToColorProperty.end(); ++it) {
    delete it->second;
  }

  propertyToColorProperty.clear();

  // Destroy the map
  delete som;
  som = nullptr;
}

void SOMView::updateInputSample() {
  inputSample.setGraph(graph());
}
void SOMView::computeSOMMap() {

  clearMask();

  vector<string> &&propertiesSelected = properties->getSelectedProperties();
  // set<string> oldSelection = selection;
  string oldSelection = selection;
  clearSelection();
  clearPreviews();

  inputSample.setPropertiesToListen(propertiesSelected);

  if (propertiesSelected.empty()) {
    if (isDetailedMode)
      internalSwitchToPreviewMode(false);
    else
      previewWidget->draw();

    return;
  }

  algorithm.run(som, inputSample, properties->getIterationNumber(), nullptr);

  // Update somMap representation
  drawPreviews();

  for (auto &it : propertiesSelected) {
    if (oldSelection.compare(it) == 0) {
      selection = oldSelection;
    }
  }

  if (selection.empty())
    internalSwitchToPreviewMode(false);

  if (properties->getAutoMapping()) {
    computeMapping();
  }

  refreshSOMMap();
}

void SOMView::computeMapping() {
  double medDist;
  unsigned int maxSize;
  mappingTab.clear();
  algorithm.computeMapping(som, inputSample, mappingTab, medDist, maxSize);

  float marginCoef = 0.1f;
  float spacingCoef = 0.2f;
  float minElementSizeCoef = 0.2f;

  SizeProperty *realGraphSizeProperty = graph()->getProperty<SizeProperty>("viewSize");

  Size &&graphMaxSize = realGraphSizeProperty->getMax(graph());
  Size &&graphMinSize = realGraphSizeProperty->getMin(graph());
  Size graphDiffSize(
      graphMinSize.getW() == graphMaxSize.getW() ? 1. : (graphMaxSize.getW() - graphMinSize.getW()),
      graphMinSize.getH() == graphMaxSize.getH() ? 1. : (graphMaxSize.getH() - graphMinSize.getH()),
      0);

  assert(graphMinSize[0] <= graphMaxSize[0] && graphMinSize[1] <= graphMaxSize[1] &&
         graphMinSize[2] <= graphMaxSize[2]);
  // Compute node displaying area
  Size &&nodeDisplayAreaSize = mapCompositeElements->getNodeAreaSize();

  Coord marginShift(nodeDisplayAreaSize.getW() * marginCoef,
                    -(nodeDisplayAreaSize.getH() * marginCoef));
  Size &&realAreaSize = nodeDisplayAreaSize * (1 - marginCoef * 2);
  // Compute elements size
  int colNumber = int(ceil(sqrt(maxSize)));

  float maxElementWidth = realAreaSize.getW() / colNumber;
  float maxElementHeight = realAreaSize.getH() / colNumber;

  float minElementWidth = maxElementWidth * minElementSizeCoef;
  float minElementHeight = maxElementHeight * minElementSizeCoef;

  SOMPropertiesWidget::SizeMappingType mt = properties->getSizeMapping();
  unsigned int x, y;
  Coord nodeDisplayAreaTopLeft;
  Coord nodeCoord;
  Size nodeSize;

  for (auto &it : mappingTab) {
    som->getPosForNode(it.first, x, y);
    nodeDisplayAreaTopLeft = marginShift + mapCompositeElements->getTopLeftPositionForElement(x, y);
    unsigned int num = 0;

    for (auto n : it.second) {
      // Compute node center
      nodeCoord.set(nodeDisplayAreaTopLeft[0] + (num % colNumber) * maxElementWidth +
                        maxElementWidth / 2,
                    nodeDisplayAreaTopLeft[1] -
                        ((floor(num / colNumber) * maxElementHeight) + maxElementHeight / 2),
                    0);

      if (mt == SOMPropertiesWidget::NoSizeMapping || graphMaxSize == graphMinSize) {
        nodeSize.set((1 - spacingCoef) * maxElementWidth, (1 - spacingCoef) * maxElementHeight, 0);
      } else if (mt == SOMPropertiesWidget::RealNodeSizeMapping) {
        // Compute size mapping coef
        const Size &realSize = realGraphSizeProperty->getNodeValue(n);
        nodeSize.set(
            minElementWidth + ((realSize.getW() - graphMinSize.getW()) / (graphDiffSize.getW())) *
                                  (maxElementWidth - minElementWidth),
            minElementHeight + ((realSize.getH() - graphMinSize.getH()) / (graphDiffSize.getH())) *
                                   (maxElementHeight - minElementHeight),
            0);
        assert(nodeSize.getW() >= 0 && nodeSize.getH() >= 0);
      }

      graphLayoutProperty->setNodeValue(n, nodeCoord);
      graphSizeProperty->setNodeValue(n, nodeSize);
      ++num;
    }
  }
}

void SOMView::addPropertyToSelection(const string &propertyName) {
  if (selection.compare(propertyName) != 0) {
    selection = propertyName;
    // mainWidget->currentPropertyNameLabel->setText(QString::fromStdString(propertyName));
    refreshSOMMap();
    getGlMainWidget()->getScene()->centerScene();

    auto it = propertyToPreviews.find(propertyName);
    assert(it != propertyToPreviews.end() && it->second);
    switchToDetailedMode(it->second);
    draw();
  }
}

void SOMView::removePropertyFromSelection(const string &propertyName) {

  if (selection.compare(propertyName) == 0) {
    selection = "";
    refreshSOMMap();
    assert(propertyToPreviews.find(propertyName) != propertyToPreviews.end());
    draw();
  }
}

void SOMView::clearSelection() {
  selection.clear();
  refreshSOMMap();
  mapWidget->draw();
}

NumericProperty *SOMView::getSelectedPropertyValues() {
  if (som && !selection.empty() && som->existProperty(selection)) {
    return static_cast<NumericProperty *>(som->getProperty(selection));
  } else
    return nullptr;
}

ColorProperty *SOMView::getSelectedBaseSOMColors() {
  if (!selection.empty() &&
      propertyToColorProperty.find(selection) != propertyToColorProperty.end()) {
    return propertyToColorProperty[selection];
  } else {
    return nullptr;
  }
}

vector<SOMPreviewComposite *> SOMView::getPreviews() {
  vector<SOMPreviewComposite *> previews;

  for (auto it = propertyToPreviews.begin(); it != propertyToPreviews.end(); ++it) {
    previews.push_back((*it).second);
  }

  return previews;
}

void SOMView::getPreviewsAtViewportCoord(int x, int y, std::vector<SOMPreviewComposite *> &result) {
  vector<SelectedEntity> selectedEntities;
  previewWidget->getScene()->selectEntities(RenderingSimpleEntities, x, y, 0, 0, nullptr,
                                            selectedEntities);

  for (auto itEntities = selectedEntities.begin(); itEntities != selectedEntities.end();
       ++itEntities) {
    for (auto itSOM = propertyToPreviews.begin(); itSOM != propertyToPreviews.end(); ++itSOM) {
      if (itSOM->second->isElement(itEntities->getSimpleEntity())) {
        result.push_back(itSOM->second);
      }
    }
  }
}

void SOMView::computeColor(SOMMap *som, tlp::NumericProperty *property, tlp::ColorScale &colorScale,
                           tlp::ColorProperty *result) {

  double min = property->getNodeDoubleMin(som);
  double max = property->getNodeDoubleMax(som);

  for (auto n : som->nodes()) {
    double curentValue = property->getNodeDoubleValue(n);
    float pos = 0;

    if (max - min != 0)
      pos = fabs(float((curentValue - min) / (max - min)));

    result->setNodeValue(n, colorScale.getColorAtPos(pos));
  }
}

bool SOMView::eventFilter(QObject *obj, QEvent *event) {

  if (obj == previewWidget) {
    if (event->type() == QMouseEvent::MouseButtonDblClick) {
      QMouseEvent *me = static_cast<QMouseEvent *>(event);

      if (me->button() == Qt::LeftButton) {
        vector<SOMPreviewComposite *> properties;
        Coord screenCoords(me->x(), me->y(), 0.0f);
        Coord &&viewportCoords = getGlMainWidget()->screenToViewport(screenCoords);
        getPreviewsAtViewportCoord(viewportCoords.x(), viewportCoords.y(), properties);

        if (!properties.empty()) {
          addPropertyToSelection(properties.front()->getPropertyName());
        }

        return true;
      }
    }

    if (event->type() == QMouseEvent::ToolTip) {
      QHelpEvent *he = static_cast<QHelpEvent *>(event);
      vector<SOMPreviewComposite *> properties;
      Coord screenCoords(he->x(), he->y(), 0.0f);
      Coord &&viewportCoords = getGlMainWidget()->screenToViewport(screenCoords);
      getPreviewsAtViewportCoord(viewportCoords.x(), viewportCoords.y(), properties);

      if (!properties.empty()) {
        QToolTip::showText(he->globalPos(),
                           QString::fromStdString(properties.front()->getPropertyName()));
      }

      return true;
    }
  } else if (obj == mapWidget) {
    if (event->type() == QMouseEvent::MouseButtonDblClick) {
      switchToPreviewMode();
      return true;
    }
  }

  return GlMainView::eventFilter(obj, event);
}

void SOMView::showMapping() {
  if (!mappingIsVisible) {
    graphComposite->setVisible(true);
    mappingIsVisible = true;
    mapWidget->draw();
  }
}
void SOMView::hideMapping() {
  if (mappingIsVisible) {
    graphComposite->setVisible(false);
    mappingIsVisible = false;
    mapWidget->draw();
  }
}

void SOMView::updateNodeColorMapping(tlp::ColorProperty *cp) {

  if (!mappingTab.empty() && !selection.empty()) {
    ColorProperty *realColorProp = graph()->getProperty<ColorProperty>("viewColor");
    // Take the first color property in order to map with node
    ColorProperty *somColorProperty;
    bool deleteAfter;

    if (cp == nullptr) {
      ColorProperty *origColor = propertyToColorProperty.find(selection)->second;

      if (mask) {
        somColorProperty = new ColorProperty(som);
        deleteAfter = true;
        for (auto n : som->nodes()) {
          if (mask->getNodeValue(n))
            somColorProperty->setNodeValue(n, origColor->getNodeValue(n));
          else
            somColorProperty->setNodeValue(n, Color(200, 200, 200));
        }
      } else {
        somColorProperty = origColor;
        deleteAfter = false;
      }
    } else {
      somColorProperty = cp;
      deleteAfter = false;
    }

    Observable::holdObservers();
    graph()->push();

    for (auto it : mappingTab) {
      Color currentNodeColor = somColorProperty->getNodeValue(it.first);

      for (auto n : it.second) {
        // Update real color
        realColorProp->setNodeValue(n, currentNodeColor);
      }
    }

    Observable::unholdObservers();

    if (deleteAfter)
      delete somColorProperty;
  }
}

void SOMView::updateDefaultColorProperty() {
  for (auto &itCP : propertyToColorProperty) {
    double min, max;
    // Recompute color property
    computePropertyColor(itCP.first, min, max);
  }

  refreshPreviews();
  refreshSOMMap();
  draw();
}

void SOMView::refreshPreviews() {
  ColorProperty *maskedColor = nullptr;

  if (mask) {
    maskedColor = new ColorProperty(som);
  }

  for (auto &itPC : propertyToPreviews) {
    ColorProperty *color = propertyToColorProperty[itPC.first];

    if (mask) {
      for (auto n : som->nodes()) {
        if (mask->getNodeValue(n))
          maskedColor->setNodeValue(n, color->getNodeValue(n));
        else
          maskedColor->setNodeValue(n, Color(200, 200, 200));
      }
      itPC.second->updateColors(maskedColor);
    } else
      itPC.second->updateColors(color);
  }

  if (maskedColor)
    delete maskedColor;
}

void SOMView::update(std::set<Observable *>::iterator, std::set<Observable *>::iterator) {}
void SOMView::observableDestroyed(Observable *) {}

void SOMView::setMask(const std::set<node> &maskSet) {
  if (!mask)
    mask = new BooleanProperty(som);

  mask->setAllNodeValue(false);

  for (auto n : maskSet) {
    mask->setNodeValue(n, true);
  }

  refreshPreviews();
  refreshSOMMap();
}

void SOMView::clearMask() {
  if (mask) {
    delete mask;
    mask = nullptr;

    refreshPreviews();
    refreshSOMMap();
  }

  refreshPreviews();
  refreshSOMMap();
  draw();
}

void SOMView::copySelectionToMask() {
  if (graph()) {
    set<node> somNodes;
    BooleanProperty *selection = graph()->getProperty<BooleanProperty>("viewSelection");
    for (auto n : selection->getNodesEqualTo(true, graph())) {
      for (auto &it : mappingTab) {
        if (it.second.find(n) != it.second.end())
          somNodes.insert(it.first);
      }
    }
    setMask(somNodes);
  }

  refreshPreviews();
  refreshSOMMap();
  draw();
}

void SOMView::invertMask() {
  if (mask) {
    set<node> somNodes;
    for (auto n : som->nodes()) {
      if (!mask->getNodeValue(n))
        somNodes.insert(n);
    }
    setMask(somNodes);
  }

  refreshPreviews();
  refreshSOMMap();
  draw();
}

void SOMView::selectAllNodesInMask() {
  if (mask) {
    BooleanProperty *selection = graph()->getProperty<BooleanProperty>("viewSelection");
    Observable::holdObservers();
    selection->setAllNodeValue(false);
    for (auto n : mask->getNodesEqualTo(true, som)) {
      if (mappingTab.find(n) != mappingTab.end()) {
        for (auto n2 : mappingTab[n]) {
          selection->setNodeValue(n2, true);
        }
      }
    }
    Observable::unholdObservers();
  }
}

QList<QWidget *> SOMView::configurationWidgets() const {
  return properties->configurationWidgets();
}

void SOMView::gridStructurePropertiesUpdated() {

  if (!checkGridValidity()) {
    QMessageBox::critical(getGlMainWidget(), tr("Bad grid"),
                          tr("Cannot connect opposite nodes in an hexagonal grid with odd height"));
    return;
  }

  // Destroy old SOM
  cleanSOMMap();
  // Build new SOM
  buildSOMMap();
  computeSOMMap();
  draw();
}

bool SOMView::checkGridValidity() const {
  return !(properties->getGridHeight() % 2 != 0 && properties->getConnectivityIndex() == 1 &&
           properties->getOppositeConnected());
}
void SOMView::learningAlgorithmPropertiesUpdated() {
  computeSOMMap();
}
void SOMView::graphRepresentationPropertiesUpdated() {
  if (properties->getAutoMapping()) {
    computeMapping();

    if (properties->getLinkColor())
      updateNodeColorMapping();
  }
}

void SOMView::applySettings() {
  gridStructurePropertiesUpdated();
}

void SOMView::switchToDetailedMode(SOMPreviewComposite *preview) {
  assert(preview);
  internalSwitchToDetailedMode(preview, properties->useAnimation());
  // hide configuration widgets
  properties->configurationWidgets()[0]->parentWidget()->parentWidget()->setVisible(false);
}

void SOMView::switchToPreviewMode() {
  internalSwitchToPreviewMode(properties->useAnimation());
  // show configuration widgets
  properties->configurationWidgets()[0]->parentWidget()->parentWidget()->setVisible(true);
}

void SOMView::copyToGlMainWidget(GlMainWidget *widget) {
  widget->getScene()->centerScene();
  assignNewGlMainWidget(widget, false);
  // centering hack after switch from Qt OpenGL module
  // to OpenGL classes in Gui module
  QTimer::singleShot(200, [=]() { this->centerView(); });
}

void SOMView::internalSwitchToDetailedMode(SOMPreviewComposite *preview, bool animation) {
  if (isDetailedMode)
    return;

  assert(preview);

  if (animation) {
    GlBoundingBoxSceneVisitor bbsv(
        previewWidget->getScene()->getGlGraphComposite()->getInputData());
    preview->acceptVisitor(&bbsv);
    tlp::zoomOnScreenRegion(previewWidget, bbsv.getBoundingBox(), true,
                            properties->getAnimationDuration());
  }

  copyToGlMainWidget(mapWidget);
  isDetailedMode = true;
  toggleInteractors(true);
}

void SOMView::internalSwitchToPreviewMode(bool animation) {
  if (!isDetailedMode)
    return;

  copyToGlMainWidget(previewWidget);
  previewWidget->draw();
  GlBoundingBoxSceneVisitor bbsv(previewWidget->getScene()->getGlGraphComposite()->getInputData());

  for (auto &it : propertyToPreviews)
    it.second->acceptVisitor(&bbsv);

  if (animation) {
    tlp::zoomOnScreenRegion(previewWidget, bbsv.getBoundingBox(), true,
                            properties->getAnimationDuration());
  } else {
    tlp::zoomOnScreenRegionWithoutAnimation(previewWidget, bbsv.getBoundingBox());
  }

  // Clear selection
  selection = "";
  isDetailedMode = false;
  toggleInteractors(false);
}

void SOMView::interactorsInstalled(const QList<tlp::Interactor *> &) {
  toggleInteractors(isDetailedMode);
}

void SOMView::dimensionUpdated() {
  computeSOMMap();
  draw();
}

void SOMView::addEmptyViewLabel() {
  GlLayer *mainLayer = previewWidget->getScene()->getLayer("Main");
  GlLabel *noDimsLabel = new GlLabel(Coord(0, 0, 0), Size(200, 100), Color(0, 0, 0));
  noDimsLabel->setText(ViewName::SOMViewName);
  GlLabel *noDimsLabel1 = new GlLabel(Coord(0, -50, 0), Size(400, 100), Color(0, 0, 0));
  noDimsLabel1->setText("No property selected.");
  GlLabel *noDimsLabel2 = new GlLabel(Coord(0, -100, 0), Size(700, 200), Color(0, 0, 0));
  noDimsLabel2->setText("Go to the \"Properties\" tab in top right corner.");
  mainLayer->addGlEntity(noDimsLabel, "no dimensions label");
  mainLayer->addGlEntity(noDimsLabel1, "no dimensions label 1");
  mainLayer->addGlEntity(noDimsLabel2, "no dimensions label 2");
  previewWidget->getScene()->centerScene();
}

void SOMView::removeEmptyViewLabel() {
  GlLayer *mainLayer = previewWidget->getScene()->getLayer("Main");
  GlSimpleEntity *noDimsLabel = mainLayer->findGlEntity("no dimensions label");
  GlSimpleEntity *noDimsLabel1 = mainLayer->findGlEntity("no dimensions label 1");
  GlSimpleEntity *noDimsLabel2 = mainLayer->findGlEntity("no dimensions label 2");

  if (noDimsLabel) {
    mainLayer->deleteGlEntity(noDimsLabel);
    mainLayer->deleteGlEntity(noDimsLabel1);
    mainLayer->deleteGlEntity(noDimsLabel2);
  }
}

void SOMView::registerTriggers() {
  for (auto obs : triggers()) {
    removeRedrawTrigger(obs);
  }

  if (graph()) {
    addRedrawTrigger(graph());

    for (auto prop : graph()->getObjectProperties()) {
      addRedrawTrigger(prop);
    }
  }
}

void SOMView::toggleInteractors(const bool activate) {
  View::toggleInteractors(activate, {InteractorName::SOMViewNavigation});
}
