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

static void copy_header(RoseDesign * dst, RoseDesign * src)
{
	unsigned i, sz;
	// Copy over the header information from the original
	dst->initialize_header();
	dst->header_name()->originating_system(src->header_name()->originating_system());
	dst->header_name()->authorisation(src->header_name()->authorisation());
	for (i = 0, sz = src->header_name()->author()->size(); i<sz; i++)
		dst->header_name()->author()->add(
		src->header_name()->author()->get(i)
		);

	for (i = 0, sz = src->header_name()->author()->size(); i<sz; i++)
		dst->header_name()->organization()->add(
		src->header_name()->organization()->get(i)
		);

	RoseStringObject desc = "Extracted from STEP assembly: ";
	desc += src->name();
	desc += ".";
	desc += src->fileExtension();
	dst->header_description()->description()->add(desc);
}

static void copy_schema(RoseDesign * dst, RoseDesign * src)
{
	// Make the new files the same schema unless the original AP does
	// not have the external reference definitions.
	//
	switch (stplib_get_schema(src)) {
	case stplib_schema_ap203e2:
	case stplib_schema_ap214:
	case stplib_schema_ap242:
		stplib_put_schema(dst, stplib_get_schema(src));
		break;

	case stplib_schema_ap203:
	default:
		stplib_put_schema(dst, stplib_schema_ap214);
		break;
	}
}

void addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir){ //obj from output file, and master fiel for putting refs into
	std::string ProdOutName;
	ProdOutName.append(obj->domain()->name());
	ProdOutName.append("_split_item");
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
	//PMI->name(std::string(des->name() + std::string("_PMI")).c_str()); //Breaks URIstuff and results in no references beign made in PMI
	RoseDesign *geo = pnew RoseDesign;
	geo->name(std::string (des->name() + std::string("_Geometry")).c_str());
	geo->saveAs("geo.stp");
	geo = ROSE.useDesign("geo.stp");
	copy_schema(geo, PMI);
	copy_header(geo, PMI);
	stix_tag_units(PMI);
	ARMpopulate(PMI);
	//STModule
	ARMCursor cur;
	ARMObject *a_obj;
	cur.traverse(PMI);
	Workpiece_IF  *workpiece = NULL;
	unsigned count = 0;
	ListOfRoseObject *aimObjs = pnew ListOfRoseObject;
	rose_mark_begin();
	while ((a_obj = cur.next())) {
		
		//std::cout << a_obj->getModuleName() << std::endl;
		workpiece = a_obj->castToWorkpiece_IF();
		if (workpiece) {
			unsigned i, sz;
			count++;
			
			a_obj->getAIMObjects(aimObjs);
			//mark aimobjects
			for (i = 0, sz = aimObjs->size(); i < sz; i++){
				rose_mark_set(aimObjs->get(i));
				
			}
			RoseObject * aimObj;
			
			ARMresolveReferences(aimObjs);
			
			for (i = 0, sz = aimObjs->size(); i < sz; i++){
				aimObj = aimObjs->get(i);

				//std::cout << "moving: " << aimObj->entity_id() << std::endl;
				//addRefAndAnchor(aimObj, geo, PMI, "");
				aimObj->move(geo, INT_MAX);
				addRefAndAnchor(aimObj, geo, PMI, "");
				
			}
			//ARMObjectVec armVec;
			//ARMresolveOrphans(armVec);
			/*

			//make references and anchors, but only if they are needed
			for (i = 0, sz = aimObjs->size(); i < sz; i++){
				aimObj = aimObjs->get(i);

				ListOfRoseObject parents;
				unsigned int k, sz;
				aimObj->usedin(NULL, NULL, &parents); //finds parents

				for (k = 0, sz = parents.size(); k < sz; k++){
					RoseObject * parent = parents.get(k);
					if (parent->design() == PMI){ //if parent is not marked then it is not a child of the object being split and needs to stay
						addRefAndAnchor(aimObj, geo, PMI, "");
					}
				}


			}
			*/
		}
	}
	//
	
	update_uri_forwarding(PMI);
	rose_empty_trash();
	geo->save();
	PMI->save();
	ARMgc(PMI);

	ARMsave(geo);
	ARMsave(PMI);
	
	return 0;
}