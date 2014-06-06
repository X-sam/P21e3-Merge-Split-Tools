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

void handleAggregate(RoseObject * obj);
void handleEntity(RoseObject * obj);

void MakeReferencesAndAnchors(RoseDesign * source, RoseDesign * destination);
void addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir = "");

void MoveGeometry(RoseDesign * source, RoseDesign * dest);

int main(int argc, char* argv[])
{

	stplib_init();	// initialize merged cad library
	
	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " [Input.stp] [OutputMasterFile.stp]\n";
	}

	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.
	ST_MODULE_FORCE_LOAD();

	//##################### INITILIZE DESIGNS #####################
	RoseDesign *des = ROSE.useDesign(argv[1]);
	if (!des)
	{
		std::cout << "Error reading input file.\n";
		return EXIT_FAILURE;
	}
	des->saveAs(argv[2]);
	RoseDesign *MainOutput= ROSE.useDesign(argv[2]);
	if (!MainOutput)
	{
		std::cout << "Error opening output file.\n";
		return EXIT_FAILURE;
	}
	RoseDesign *geo = pnew RoseDesign;
	std::string geoname(MainOutput->name());
	geoname.append("_Geometry");
	geo->name(geoname.c_str());
	geo->save();
	if (!geo)
	{
		std::cout << "Error opening geometry output file for writing.\n";
		return EXIT_FAILURE;
	}
	stix_tag_units(MainOutput);
	ARMpopulate(MainOutput);
	//#############################################################

	//STModule
	rose_compute_backptrs(MainOutput);

	MoveGeometry(MainOutput, geo);

	MakeReferencesAndAnchors(MainOutput, geo);

	update_uri_forwarding(MainOutput);

	rose_mark_end();

	ARMgc(MainOutput);
	ARMgc(geo);
	rose_empty_trash();

	geo->save();
	ARMsave(geo);

	MainOutput->save();
	ARMsave(MainOutput);

	return 0;
}


void addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir){ //obj from output file, and master fiel for putting refs into
	std::string ProdOutName;
	ProdOutName.append(obj->domain()->name());
	ProdOutName.append("_split_item");
	ProdOutName.append(std::to_string(obj->entity_id()));
	//ProdOutName = SafeName(ProdOutName);

	ProdOut->addName(ProdOutName.c_str(), obj);

	std::string refdir(dir);
	std::string refURI = (std::string(ProdOut->name()) + ".stp#" + ProdOutName);//uri for created reference to prod/obj

	RoseReference *ref = rose_make_ref(master, refURI.c_str());
	ref->resolved(obj);
	MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
	URIManager = MyURIManager::make(obj);
	URIManager->should_go_to_uri(ref);
}

void handleEntity(RoseObject * obj)
{
	auto atts = obj->attributes();
	for (int i = 0; i < atts->size(); i++)
	{
		RoseAttribute *att = atts->get(i);
		RoseObject * childobj = 0;
		if (att->isEntity()) //== geo && !rose_is_marked(&att))
		{
			childobj = obj->getObject(att);
		}
		else if (att->isSelect())
		{
			childobj = rose_get_nested_object(ROSE_CAST(RoseUnion, obj->getObject(att)));
		}
		if (att->isAggregate())
		{
			handleAggregate(obj->getObject(att));
			continue;
		}
		if (!childobj) continue;
		if (childobj->design() != obj->design() && !rose_is_marked(childobj))
		{
			rose_mark_set(childobj);
			std::string name(childobj->domain()->name());
			addRefAndAnchor(childobj, childobj->design(), obj->design());
		}
	}
}

void handleAggregate(RoseObject * obj)
{
	if (obj == NULL) return;
	if (!obj->attributes()->first()->isObject()) return;
	unsigned i, sz;
	for (i = 0, sz = obj->size(); i < sz; i++)
	{
		auto childobj = obj->getObject(i);
		if (childobj == NULL) continue;
		if (childobj->isa(ROSE_DOMAIN(RoseUnion)))
		{
			childobj = rose_get_nested_object(ROSE_CAST(RoseUnion, childobj));
			if (childobj == NULL) continue;	//Nested object contains nothing? TODO: Ask Dave.
		}
		if (childobj->isa(ROSE_DOMAIN(RoseAggregate))) handleAggregate(childobj);
		if (childobj->design() != obj->design() && !rose_is_marked(childobj))
		{
			rose_mark_set(childobj);
			std::string name(childobj->domain()->name());
			addRefAndAnchor(childobj, childobj->design(), obj->design());
		}
	}
}

void MakeReferencesAndAnchors(RoseDesign * source, RoseDesign * destination)
{
	RoseObject *obj;
	RoseCursor curse;
	curse.traverse(source);
	curse.domain(ROSE_DOMAIN(RoseStructure));
	while (obj = curse.next())
	{
		handleEntity(obj);
	}
	rose_compute_backptrs(source);
	rose_compute_backptrs(destination);
	curse.traverse(destination);
	curse.domain(ROSE_DOMAIN(RoseObject));
	ListOfRoseObject Parents;
	while (obj = curse.next())
	{
		Parents.emptyYourself();
		obj->usedin(NULL, NULL, &Parents);
		if (Parents.size() == 0)
		{
			addRefAndAnchor(obj, destination, source);
		}
	}
}

void MoveGeometry(RoseDesign * source, RoseDesign * dest)
{
	ARMCursor cur; //arm cursor
	ARMObject *a_obj;
	cur.traverse(source);

	Workpiece_IF  *workpiece = NULL;

	ListOfRoseObject aimObjs;
	rose_mark_begin();
	//Creates all references that may be necessary
	while ((a_obj = cur.next()))
	{
		workpiece = a_obj->castToWorkpiece_IF();
		if (workpiece)
		{
			aimObjs.emptyYourself();
			unsigned i, sz;
			RoseObject * aimObj;
			a_obj->getAIMObjects(&aimObjs);
			ARMresolveReferences(&aimObjs);
			for (i = 0, sz = aimObjs.size(); i < sz; i++)
			{
				aimObj = aimObjs[i];
				//moves evyerthing
				aimObj->move(dest, 0);
			}
		}
	}
}
