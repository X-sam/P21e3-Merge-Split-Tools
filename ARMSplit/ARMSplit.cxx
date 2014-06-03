//Chris Made this code.
//6/2/14

#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <stix.h>
#include <string>
#include <map>
#include <iostream>
#include <ARM.h>
#include "scan.h"
#include <ctype.h>

#pragma comment(lib,"stpcad_stix.lib")

void addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir){ //obj from output file, and master fiel for putting refs into
	std::string ProdOutName;
	ProdOutName.append(obj->domain()->name());
	ProdOutName.append("_split_item#");
	ProdOutName.append(std::to_string(obj->entity_id()));
	//ProdOutName = SafeName(ProdOutName);

	ProdOut->addName(ProdOutName.c_str(), obj);

	std::string refdir(dir);
	std::string refURI = (std::string("geo.stp#") + ProdOutName);//uri for created reference to prod/obj

	RoseReference *ref = rose_make_ref(master, refURI.c_str());
	ref->resolved(obj);
	MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
	URIManager = MyURIManager::make(obj);
	URIManager->should_go_to_uri(ref);

}

int main(int argc, char* argv[])
{
	
	stplib_init();	// initialize merged cad library
	//rose_p28_init();	// support xml read/write
	
	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.
	//ST_MODULE_FORCE_LOAD();
	/* Create a RoseDesign to hold the output data*/

	printf("Reading file '%s'\n", "sp3-boxy_fmt_original.stp");

	// Read a STEP file and dimension the workpiece in that file
	RoseDesign *des = ROSE.useDesign("sp3-boxy_fmt_original.stp");
	des->saveAs("PMI.stp");
	RoseDesign *PMI = ROSE.useDesign("PMI.stp");
	RoseDesign *geo = pnew RoseDesign;
	geo->saveAs("geo.stp");
	geo = ROSE.useDesign("geo.stp");
	stix_tag_units(PMI);
	ARMpopulate(PMI);
	//STModule
	ARMCursor cur;
	ARMObject *a_obj;
	cur.traverse(PMI);
	Geometric_dimension_IF  *tolly = NULL;
	unsigned count = 0;
	ListOfRoseObject *aimObjs = pnew ListOfRoseObject;
	rose_mark_begin();
	while ((a_obj = cur.next())) {
		
		std::cout << a_obj->getModuleName() << std::endl;
		tolly = a_obj->castToGeometric_dimension_IF();
		if (tolly) {
			unsigned i, sz;
			count++;
			
			a_obj->getAIMObjects(aimObjs);
			//mark aimobjects
			for (i = 0, sz = aimObjs->size(); i < sz; i++){
				rose_mark_set(aimObjs->get(i));
			}
			RoseObject * aimObj;
			RoseObject * root = a_obj->getRootObject();
			//root->copy(geo);

			addRefAndAnchor(root, geo, PMI, "");
			root->move(geo);
			
			
			for (i = 0, sz = aimObjs->size(); i < sz; i++){
				aimObj = aimObjs->get(i);
				//for attributes//parents?
					//if not rose marked
				addRefAndAnchor(aimObj, geo, PMI, "");
				aimObj->move(geo);
				//rose_move_to_trash(aimObj);
			}
			ARMresolveReferences(aimObjs);
			std::cout << aimObjs->size() << std::endl;
			//addRefAndAnchor(root, geo, PMI, "");
		}
	}
	rose_empty_trash();
	PMI->save();
	geo->save();
	ARMsave(geo);
	ARMsave(PMI);
	return 0;
}