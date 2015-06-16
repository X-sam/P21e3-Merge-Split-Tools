//Chris & Samson made this code.
//6/13/14

#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <stix.h>
#include <string>
#include <map>
#include <iostream>
#include <ARM.h>
#include "scan.h"
#include "GUID.h"
#include <ctype.h>

#pragma comment(lib,"stpman_stix.lib")

void handleAggregate(RoseObject * obj);
void handleEntity(RoseObject * obj);

void MakeReferencesAndAnchors(RoseDesign * source, RoseDesign * destination);
void addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir = "");

void MoveGeometry(RoseDesign * source, RoseDesign * dest);

int main(int argc, char* argv[])
{

	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " [Input.stp] [OutputMasterFile.stp]\n";
		return EXIT_FAILURE;
	}
	ROSE.quiet(1);
	stplib_init();	// initialize merged cad library

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
	RoseDesign *MainOutput = ROSE.useDesign(argv[2]);
	if (!MainOutput)
	{
		std::cout << "Error opening output file.\n";
		return EXIT_FAILURE;
	}
	std::string geoname(MainOutput->name());
	geoname.append("_Geometry");
	RoseDesign *geo = new RoseDesign(geoname.c_str());
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

	rose_empty_trash();

	geo->save();
	ARMsave(geo);
	MainOutput->save();
	ARMsave(MainOutput);

	return 0;
}


void addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir){ //obj from output file, and master file for putting refs into
	std::string anchor = get_guid();	//Anchor is now a guid. 1234abcd-56789-0000-0000-abcd12340000 or something like that.
	anchor.push_back('-');				//to distinguish the name.
	anchor.append((const char*)obj->domain()->name());	//1234abcd-56789-0000-0000-abcd12340000-advanced_face
	//std::string anchor((const char*)obj->domain()->name());	//anchor now looks like "advanced_face" or "manifold_solid_brep"
	//anchor.append("_split_item");				//"advanced_face_split_item"
	//anchor.append(std::to_string(obj->entity_id()));	//"advanced_face_split_item123"
	
	ProdOut->addName(anchor.c_str(), obj);	//This makes the anchor.

	std::string reference(dir);	//let's make the reference text. start with the output directory 
	reference.append(ProdOut->name());	//Add the file name.
	reference.append(".stp#" + anchor); //Finally add the file type, a pound, and the anchor. It'll look like "folder/file.stp#1234abcd-56789-0000-0000-abcd12340000advanced_face"

	RoseReference *ref = rose_make_ref(master, reference.c_str());	//Make a reference in the master output file.
	ref->resolved(obj);	//Reference is resolved to the object that we passed in, which is currently residing in the ProdOut design.
	MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
	URIManager = MyURIManager::make(obj);
	URIManager->should_go_to_uri(ref);
}

void handleEntity(RoseObject * obj)
{
	auto atts = obj->attributes();	//We will check all the attributes of obj to see if any of them are external references.
	for (unsigned int i = 0; i < atts->size(); i++)
	{
		RoseAttribute *att = atts->get(i);
		RoseObject * childobj = 0;
		if (att->isEntity())	//Easy mode. attribute is an entity so it will be a single roseobject.
		{
			childobj = obj->getObject(att);
		}
		else if (att->isSelect())	//Oh boy, a select. Get the contents. It might make childobj null, we'll check for that in a minute.
		{
			childobj = rose_get_nested_object(ROSE_CAST(RoseUnion, obj->getObject(att)));
		}
		if (att->isAggregate())	//An aggregate! We have a whole function dedicated to this case.
		{
			handleAggregate(obj->getObject(att));
			continue;				//handleAggregate manages everything so we can just skip the next bits and move on to the next attribute.
		}
		if (!childobj) continue;	//Remember that case with the select? Confirm we have a childobj here.
		if (childobj->design() != obj->design() && !rose_is_marked(childobj))	//If this all is true, time to create a reference/anchor pair and mark childobj
		{
			rose_mark_set(childobj);
			std::string name(childobj->domain()->name());
			addRefAndAnchor(childobj, childobj->design(), obj->design());
		}
	}
}

void handleAggregate(RoseObject * obj)
{
	if (obj == NULL) return;	//Sometimes handleEntity passes empty aggregates. Check for that before anything else.
	if (!obj->attributes()->first()->isObject()) return;	//If the object is not an object (I.E. it's a real or bool etc) then we don't care about it.
	unsigned i, sz;
	for (i = 0, sz = obj->size(); i < sz; i++)	//Obj is a ListofRoseObject (more or less) so we want to check all the stuff inside it.
	{
		auto childobj = obj->getObject(i);	
		if (childobj == NULL) continue;	//It's probably a simple. It can't be an external reference, so we ignore it.
		if (childobj->isa(ROSE_DOMAIN(RoseUnion)))	//Oh boy. A select. We need to get the actual object inside of it.
		{
			childobj = rose_get_nested_object(ROSE_CAST(RoseUnion, childobj));
			if (childobj == NULL) continue;	//If the contents of the select were '$'(NULL) or a simple property, then childobj will be NULL.
		}
		if (childobj->isa(ROSE_DOMAIN(RoseAggregate)))
		{
			handleAggregate(childobj);	//NESTED AGGREGATES! Call ourself on it.
			continue;
		}
		if (childobj->design() != obj->design() && !rose_is_marked(childobj))	//If we got here we've got an external reference which needs a reference/anchor pair and a marking.
		{
			rose_mark_set(childobj);
			std::string name(childobj->domain()->name());
			addRefAndAnchor(childobj, childobj->design(), obj->design());
		}
	}
}

void MakeReferencesAndAnchors(RoseDesign * source, RoseDesign * destination)
{
	rose_mark_begin();	//Mark is used by handleEntity to decide if a RoseObject has had its reference/anchor pair added to the list already.
	RoseObject *obj;
	RoseCursor curse;
	curse.traverse(source);
	curse.domain(ROSE_DOMAIN(RoseStructure));	//We are only interested in actual entities, so we set our domain to that.
	while (obj = curse.next())
	{
		handleEntity(obj);
	}
	rose_mark_end();
	rose_compute_backptrs(source);
	rose_compute_backptrs(destination);//Update the backpointers to prevent usedin from having errors.
	curse.traverse(destination);
	curse.domain(ROSE_DOMAIN(RoseObject));	//Check everything in the destination file.
	ListOfRoseObject Parents;
	while (obj = curse.next())
	{
		Parents.emptyYourself();
		obj->usedin(NULL, NULL, &Parents);
		if (Parents.size() == 0)
		{
			addRefAndAnchor(obj, destination, source);	//If an object in destination has no parents (like Batman) then we have to assume it was important presentation data and put a reference in for it.
		}
	}
}

void MoveGeometry(RoseDesign * source, RoseDesign * dest)
{
	ARMCursor cur; //arm cursor
	ARMObject *a_obj = NULL;
	cur.traverse(source);
	cur.domain(Workpiece::type());	//We want the geometry data, which is grouped as Workpieces in ARM.
	ListOfRoseObject aimObjs;

	while ((a_obj = cur.next()))	//a_obj is a workpiece.
	{
		if (!a_obj) continue;
		aimObjs.emptyYourself();	//aimObjs now size 0.
		a_obj->getAIMObjects(&aimObjs);	//aimObjs now contains all the AIM objects that relate to workpiece a_obj
		ARMresolveReferences(&aimObjs);	//aimObjs now contains the above PLUS everything that is connected to those, both parents and childrens with infinite depth.
		unsigned i, sz;
		for (i = 0, sz = aimObjs.size(); i < sz; i++)	//Now that we have all the workpiece stuff in a list, we have to move it to the output file.
		{
			auto obj = aimObjs.get(i);
			obj->move(dest,0);
		}
	}
}