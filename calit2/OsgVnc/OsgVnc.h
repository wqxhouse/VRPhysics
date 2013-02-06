#ifndef _OSGVNC_
#define _OSGVNC_

#include <cvrKernel/CVRPlugin.h>
#include <cvrKernel/FileHandler.h>
#include <cvrKernel/SceneObject.h>
#include <cvrMenu/SubMenu.h>
#include <cvrMenu/MenuButton.h>
#include <cvrMenu/MenuRangeValue.h>
#include <cvrMenu/MenuCheckbox.h>

#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>

#include <osgWidget/VncClient>

#include <osg/MatrixTransform>
#include <osg/ImageStream>
#include <osg/Uniform>

#include <string>
#include <vector>

#include "VncSceneObject.h"

class OsgVnc : public cvr::CVRPlugin, public cvr::MenuCallback ,public cvr::FileLoadCallback
{
    public:        
        OsgVnc();
        virtual ~OsgVnc();
	    bool init();
        virtual bool loadFile(std::string file);
	    void menuCallback(cvr::MenuItem * item);
        virtual void message(int type, char *&data, bool collaborative=false);

    protected:

        // launch browser query
        void launchQuery(std::string& hostname, int portno, std::string& query);

	    // container to hold pdf data
	    struct VncObject
        {
            std::string name;
	        VncSceneObject * scene;
        };

        //std::map<struct VncObject*,cvr::MenuCheckbox*> _planeMap;
        std::map<struct VncObject* ,cvr::MenuButton*> _deleteMap;
        std::vector<struct VncObject*> _loadedVncs;
};

#endif