#include "StrainGraphObject.h"
#include "GraphLayoutObject.h"
#include "GraphGlobals.h"

#include <cvrConfig/ConfigManager.h>
#include <cvrInput/TrackingManager.h>
#include <cvrKernel/ComController.h>
#include <cvrUtil/OsgMath.h>

#include <iostream>
#include <sstream>

using namespace cvr;

StrainGraphObject::StrainGraphObject(DBManager * dbm, float width, float height, std::string name, bool navigation, bool movable, bool clip, bool contextMenu, bool showBounds) : LayoutTypeObject(name,navigation,movable,clip,contextMenu,showBounds)
{
    _dbm = dbm;

    setBoundsCalcMode(SceneObject::MANUAL);
    osg::BoundingBox bb(-(width*0.5),-2,-(height*0.5),width*0.5,0,height*0.5);
    setBoundingBox(bb);

    _desktopMode = ConfigManager::getBool("Plugin.FuturePatient.DesktopMode",false);

    _graph = new GroupedBarGraph(width,height);
    _graph->setColorMapping(GraphGlobals::getDefaultPhylumColor(),GraphGlobals::getPatientColorMap());
    _graph->setColorMode(BGCM_GROUP);
}

StrainGraphObject::~StrainGraphObject()
{
}

bool StrainGraphObject::setGraph(std::string title, int taxId, bool larryOnly)
{
    std::stringstream ss;
    if(!larryOnly)
    {
	ss << "select Patient.last_name, Patient.p_condition, EcoliShigella_Measurement.value, unix_timestamp(EcoliShigella_Measurement.timestamp) as timestamp from EcoliShigella_Measurement inner join Patient on Patient.patient_id = EcoliShigella_Measurement.patient_id where EcoliShigella_Measurement.taxonomy_id = " << taxId << " order by Patient.p_condition, EcoliShigella_Measurement.value desc;";
    }
    else
    {
	ss << "select \"Smarr\" as last_name, \"Larry\" as p_condition, value, unix_timestamp(timestamp) as timestamp from EcoliShigella_Measurement where patient_id = 1 and taxonomy_id = " << taxId << " order by timestamp;";
    }

    struct StrainData
    {
	char name[1024];
	char group[1024];
	float value;
	time_t timestamp;
    };

    StrainData * sdata = NULL;
    int dataSize = 0;

    if(ComController::instance()->isMaster())
    {
	if(_dbm)
	{
	    DBMQueryResult result;

	    _dbm->runQuery(ss.str(),result);

	    dataSize = result.numRows();
	    if(dataSize)
	    {
		sdata = new struct StrainData[dataSize];

		for(int i = 0; i < dataSize; ++i)
		{
		    strncpy(sdata[i].name,result(i,"last_name").c_str(),1023);
		    strncpy(sdata[i].group,result(i,"p_condition").c_str(),1023);
		    sdata[i].value = atof(result(i,"value").c_str());
		    sdata[i].timestamp = atol(result(i,"timestamp").c_str());
		}
	    }
	}

	/*if(_conn)
	{
	    mysqlpp::Query strainQuery = _conn->query(ss.str().c_str());
	    mysqlpp::StoreQueryResult strainResult = strainQuery.store();

	    dataSize = strainResult.num_rows();
	    if(dataSize)
	    {
		sdata = new struct StrainData[dataSize];

		for(int i = 0; i < dataSize; ++i)
		{
		    strncpy(sdata[i].name,strainResult[i]["last_name"].c_str(),1023);
		    strncpy(sdata[i].group,strainResult[i]["p_condition"].c_str(),1023);
		    sdata[i].value = atof(strainResult[i]["value"].c_str());
		    sdata[i].timestamp = atol(strainResult[i]["timestamp"].c_str());
		}
	    }
	}
	else
	{
	    std::cerr << "No Database connection." << std::endl;
	}*/

	ComController::instance()->sendSlaves(&dataSize,sizeof(int));
	if(dataSize)
	{
	    ComController::instance()->sendSlaves(sdata,dataSize*sizeof(struct StrainData));
	}
    }
    else
    {
	ComController::instance()->readMaster(&dataSize,sizeof(int));
	if(dataSize)
	{
	    sdata = new struct StrainData[dataSize];
	    ComController::instance()->readMaster(sdata,dataSize*sizeof(struct StrainData));
	}
    }

    std::map<std::string,std::string> condition2Group;
    condition2Group["crohn's disease"] = "Crohns";
    condition2Group["healthy"] = "Healthy";
    condition2Group["Larry"] = "LS";
    condition2Group["ulcerous colitis"] = "UC";

    std::map<std::string, std::vector<std::pair<std::string, float> > > dataMap;
    std::vector<std::string> orderVec;

    for(int i = 0; i < dataSize; ++i)
    {
	char timestamp[512];
	std::string name = sdata[i].name;
	name += " - ";
	strftime(timestamp,511,"%F",localtime(&sdata[i].timestamp));
	name += timestamp;

	if(!larryOnly)
	{
	    if(sdata[i].value > 0.0)
	    {
		dataMap[condition2Group[sdata[i].group]].push_back(std::pair<std::string,float>(name,sdata[i].value));
	    }
	}
	else
	{
	    std::stringstream ss;
	    ss << "LS" << (i+1);
	    if(sdata[i].value > 0.0)
	    {
		dataMap[ss.str()].push_back(std::pair<std::string,float>(name,sdata[i].value));
	    }
	}
    }

    for(std::map<std::string, std::vector<std::pair<std::string, float> > >::iterator it = dataMap.begin(); it != dataMap.end(); ++it)
    {
	orderVec.push_back(it->first);
    }

    bool graphValid = false;

    if(orderVec.size())
    {
	graphValid = _graph->setGraph(title, dataMap, orderVec, BGAT_LOG, "Relative Abundance", "", "condition / patient", osg::Vec4());
    }

    if(graphValid)
    {
	addChild(_graph->getRootNode());
	std::vector<std::pair<std::string,int> > customOrder;

	if(!larryOnly)
	{
	    int totalEntries = 0;
	    for(std::map<std::string, std::vector<std::pair<std::string, float> > >::iterator it = dataMap.begin(); it != dataMap.end(); ++it)
	    {
		totalEntries += it->second.size();
	    }

	    std::map<std::string,int> groupIndexMap;

	    while(customOrder.size() < totalEntries)
	    {
		float maxVal = FLT_MIN;
		std::string group;
		for(std::map<std::string, std::vector<std::pair<std::string, float> > >::iterator it = dataMap.begin(); it != dataMap.end(); ++it)
		{
		    if(groupIndexMap[it->first] >= it->second.size())
		    {
			continue;
		    }

		    if(it->second[groupIndexMap[it->first]].second > maxVal)
		    {
			group = it->first;
			maxVal = it->second[groupIndexMap[it->first]].second;
		    }
		}

		customOrder.push_back(std::pair<std::string,int>(group,groupIndexMap[group]));
		groupIndexMap[group]++;
	    }
	}
	else
	{
	    for(std::map<std::string, std::vector<std::pair<std::string, float> > >::iterator it = dataMap.begin(); it != dataMap.end(); ++it)
	    {
		for(int i = 0; i < it->second.size(); ++i)
		{
		    customOrder.push_back(std::pair<std::string,int>(it->first,i));
		}
	    }

	    std::map<std::string,osg::Vec4> larryColors;

	    float step = 0.75 / ((float)customOrder.size());

	    for(int i = 0; i < customOrder.size(); ++i)
	    {
		std::stringstream ss;
		ss << "LS" << (i+1);
		larryColors[ss.str()] = osg::Vec4(1.0-((float)i)*step,0.0,0.0,1.0);
	    }
	    _graph->setColorMapping(osg::Vec4(0.0,0.0,0.0,1.0),larryColors);
	}

	_graph->setCustomOrder(customOrder);
	_graph->setDisplayMode(BGDM_CUSTOM);
	_graph->setShowLabels(false);
    }

    if(sdata)
    {
	delete[] sdata;
    }

    return graphValid;
}

void StrainGraphObject::objectAdded()
{
    GraphLayoutObject * layout = dynamic_cast<GraphLayoutObject*>(_parent);
    if(layout && layout->getPatientKeyObject())
    {
	bool addKey = !layout->getPatientKeyObject()->hasRef();
	layout->getPatientKeyObject()->ref(this);

	if(addKey)
	{
	    layout->addLineObject(layout->getPatientKeyObject());
	}
    }
}

void StrainGraphObject::objectRemoved()
{
    GraphLayoutObject * layout = dynamic_cast<GraphLayoutObject*>(_parent);
    if(layout && layout->getPatientKeyObject())
    {
	layout->getPatientKeyObject()->unref(this);
    }
}

void StrainGraphObject::setGraphSize(float width, float height)
{
    osg::BoundingBox bb(-(width*0.5),-2,-(height*0.5),width*0.5,0,height*0.5);
    setBoundingBox(bb);

    _graph->setDisplaySize(width,height);
}

void StrainGraphObject::setColor(osg::Vec4 color)
{
    _graph->setColor(color);
}

float StrainGraphObject::getGraphMaxValue()
{
    return _graph->getDataMax();
}

float StrainGraphObject::getGraphMinValue()
{
    return _graph->getDataMin();
}

float StrainGraphObject::getGraphDisplayRangeMax()
{
    return _graph->getDisplayRangeMax();
}

float StrainGraphObject::getGraphDisplayRangeMin()
{
    return _graph->getDisplayRangeMin();
}

void StrainGraphObject::setGraphDisplayRange(float min, float max)
{
    _graph->setDisplayRange(min,max);
}

void StrainGraphObject::resetGraphDisplayRange()
{
    _graph->setDisplayRange(_graph->getDisplayRangeMin(),_graph->getDisplayRangeMax());
}

void StrainGraphObject::selectPatients(std::map<std::string,std::vector<std::string> > & selectMap)
{
    _graph->selectItems(selectMap);
}

bool StrainGraphObject::processEvent(cvr::InteractionEvent * ie)
{
    if(ie->asTrackedButtonEvent() && ie->asTrackedButtonEvent()->getButton() == 0 && (ie->getInteraction() == BUTTON_DOWN || ie->getInteraction() == BUTTON_DOUBLE_CLICK))
    {
	TrackedButtonInteractionEvent * tie = (TrackedButtonInteractionEvent*)ie;

	GraphLayoutObject * layout = dynamic_cast<GraphLayoutObject*>(_parent);
	if(!layout)
	{
	    return false;
	}

	std::string selectedGroup;
	std::vector<std::string> selectedKeys;

	osg::Vec3 start, end(0,1000,0);
	start = start * tie->getTransform() * getWorldToObjectMatrix();
	end = end * tie->getTransform() * getWorldToObjectMatrix();

	osg::Vec3 planePoint;
	osg::Vec3 planeNormal(0,-1,0);
	osg::Vec3 intersect;
	float w;

	bool clickUsed = false;

	if(linePlaneIntersectionRef(start,end,planePoint,planeNormal,intersect,w))
	{
	    if(_graph->processClick(intersect,selectedGroup,selectedKeys))
	    {
		clickUsed = true;
	    }
	}

	layout->selectPatients(selectedGroup,selectedKeys);
	if(clickUsed)
	{
	    return true;
	}
    }

    return FPTiledWallSceneObject::processEvent(ie);
}

void StrainGraphObject::updateCallback(int handID, const osg::Matrix & mat)
{
    if((_desktopMode && TrackingManager::instance()->getHandTrackerType(handID) == TrackerBase::MOUSE) ||
	(!_desktopMode && TrackingManager::instance()->getHandTrackerType(handID) == TrackerBase::POINTER))
    {
	osg::Vec3 start, end(0,1000,0);
	start = start * mat * getWorldToObjectMatrix();
	end = end * mat * getWorldToObjectMatrix();

	osg::Vec3 planePoint;
	osg::Vec3 planeNormal(0,-1,0);
	osg::Vec3 intersect;
	float w;

	if(linePlaneIntersectionRef(start,end,planePoint,planeNormal,intersect,w))
	{
	    _graph->setHover(intersect);
	}
    }
}

void StrainGraphObject::leaveCallback(int handID)
{
    if((_desktopMode && TrackingManager::instance()->getHandTrackerType(handID) == TrackerBase::MOUSE) ||
	(!_desktopMode && TrackingManager::instance()->getHandTrackerType(handID) == TrackerBase::POINTER))
    {
	_graph->clearHoverText();
    }
}
