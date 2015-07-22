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

void MovePMI(RoseDesign * source, RoseDesign * dest,RoseMark forbidden);

void markgeo(RoseDesign* source, RoseMark mark);
void mover(RoseObject * obj, RoseDesign * dest);
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
	RoseP21Writer::preserve_eids = true;
	des->saveAs(argv[2]);
	RoseDesign *MainOutput = ROSE.useDesign(argv[2]);
	if (!MainOutput)
	{
		std::cout << "Error opening output file.\n";
		return EXIT_FAILURE;
	}
	std::string pminame(MainOutput->name());
	pminame.append("_PMI");
	RoseDesign *pmi = new RoseDesign(pminame.c_str());
	pmi->save();
	if (!pmi)
	{
		std::cout << "Error opening pmi output file for writing.\n";
		return EXIT_FAILURE;
	}
	stix_tag_units(MainOutput);
	ARMpopulate(MainOutput);
	//#############################################################
	//STModule
	//rose_compute_backptrs(MainOutput);
	RoseMark geomrk = rose_mark_begin();
	markgeo(MainOutput,geomrk);
	MovePMI(MainOutput, pmi,geomrk);
	MakeReferencesAndAnchors(MainOutput, pmi);
	update_uri_forwarding(MainOutput);

	rose_empty_trash();

	pmi->save();
	ARMsave(pmi);
	MainOutput->save();
	ARMsave(MainOutput);
	return 0;
}
void mover(RoseObject * obj, RoseDesign * dest)
{
    if (!obj) return;
    if (obj->isa(ROSE_DOMAIN(RoseUnion))) mover(rose_get_nested_object(ROSE_CAST(RoseUnion, obj)),dest);
    if (obj->isa(ROSE_DOMAIN(RoseAggregate))) for (int i = 0, sz = obj->size(); i < sz; i++) mover(obj->getObject(i),dest);
    if (obj)
	obj->move(dest, 0, false);
}
void markgeo(RoseDesign* source, RoseMark mark)
{
    stix_tag_asms(source);
    StpAsmProductDefVec roots;
    stix_find_root_products(&roots, source);
    for (int i = 0, sz = roots.size(); i < sz; i++)
    {
	stp_product_definition * pd = roots[i];
	ListOfRoseObject lst;
	pd->findObjects(&lst,-1,false);
	rose_mark_set(pd, mark);
	for (int j = 0, jsz = lst.size(); j < jsz; j++)
	{
	    rose_mark_set(lst[j], mark);
	}
	StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);
	for (int j = 0, jsz = pm->shapes.size(); j<jsz; j++)
	{
	    stp_shape_representation * sr = pm->shapes[j];
	    StixMgrAsmShapeRep* smasr = StixMgrAsmShapeRep::find(sr);
	    StpAsmShapeDefRepVec foo = smasr->shape_def_reps;
	    for (int k = 0, ksz = foo.size(); k < ksz; k++)
	    {
		printf("moving %d", foo[k]->entity_id());
		RoseObject * pdef = rose_get_nested_object(foo[k]->definition());
		rose_mark_set(pdef, mark);
		rose_mark_set(foo[k], mark);
	    }
	    ListOfRoseObject tmp;
	    sr->findObjects(&tmp, -1, false);
	    rose_mark_set(sr, mark);
	    for (int k = 0, ksz = tmp.size(); k < ksz; k++)
	    {
		rose_mark_set(tmp[k], mark);
	    }
	}
    }
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
	curse.domain(ROSE_DOMAIN(RoseStructure));	//Check everything in the destination file.
	while (obj = curse.next())
	{
		RoseBackPtrCursor foo;
		foo.traverse(obj);
		if(foo.first() == 0)
		{
			addRefAndAnchor(obj, destination, source);	//If an object in destination has no parents (like Batman) then we have to assume it was important presentation data and put a reference in for it.
		}
	}
}
void tomove(RoseObject* obj,RoseDesign*dest,RoseMark forbidden);
void parsemove(RoseObject* obj, RoseDesign*dest,RoseMark forbidden);
void MovePMI(RoseDesign * source, RoseDesign * dest,RoseMark forbidden)
{
    RoseCursor cur;
    RoseObject * obj = nullptr;
    cur.traverse(source);
    //Move the Draughting models (which define the PMI) and their associated data
    cur.domain(ROSE_DOMAIN(stp_draughting_model));
    while ((obj = cur.next()) != nullptr)
    {
	for (unsigned i = 0, sz = obj->attributes()->size(); i < sz; i++)
	{
	    RoseObject * movee = obj->getObject(obj->attributes()->get(i));
	    if (nullptr == movee) continue;
	    //printf("Moving property %s of %s (which is a %s)\n", obj->attributes()->get(i)->name(), obj->domain()->name(),movee->domain()->name());
	    tomove(movee, dest,forbidden);
	}
	obj->move(dest, 0, false);
	//movelist.move(dest,1,false);
    }
    //Move the DMIAs which point to the Draughting models we moved.
    cur.domain(ROSE_DOMAIN(stp_draughting_model_item_association));
    while((obj = cur.next())!=nullptr)
    {
	stp_draughting_model_item_association * dmia = ROSE_CAST(stp_draughting_model_item_association, obj);
	if (dmia->used_representation()->design() == dest)
	    tomove(dmia,dest,forbidden);
    }
}

void tomove(RoseObject* obj,RoseDesign * dest,RoseMark forbidden)
{
    if (nullptr == obj) return;
    if (rose_is_marked(obj, forbidden)) return;//Not Allowed to touch.
    char * tst = obj->domain()->name();
    int eid = obj->entity_id();
    if (obj->isa(ROSE_DOMAIN(RoseAggregate)))
    {
	for (unsigned i = 0, sz = obj->size(); i < sz; i++)
	{
	    obj->move(dest, 0, false);
	    if (nullptr == obj->getObject(i)) break;//Not a list of objects.
	    tomove(obj->getObject(i),dest,forbidden);
	}
    }
    if (obj->isa(ROSE_DOMAIN(RoseUnion)))
    {
	RoseObject * nestedobj= rose_get_nested_object(ROSE_CAST(RoseUnion, obj));
	if (nullptr == nestedobj) obj->move(dest, -1, false);
	else if (nestedobj->isa(ROSE_DOMAIN(stp_mapped_item))) return;
	else tomove(nestedobj, dest,forbidden);
    }
    if (obj->isa(ROSE_DOMAIN(RoseStructure)))
    {
	parsemove(obj, dest,forbidden);
    }
}
void parsemove(RoseObject* obj, RoseDesign* dest,RoseMark forbidden)
{
    if (obj->isa(ROSE_DOMAIN(stp_mapped_item)) || obj->isa(ROSE_DOMAIN(stp_representation_map))) return;
    obj->move(dest, 0, false);
    if (obj->isa(ROSE_DOMAIN(stp_geometric_item_specific_usage))) return;
    if (obj->isa(ROSE_DOMAIN(stp_shape_aspect))) //need to get the GISU!
    {
	ListOfRoseObject users;
	rose_compute_backptrs(obj->design());
	obj->usedin(NULL, NULL, &users);
	for (unsigned i = 0, sz = users.size(); i < sz; i++)
	{
	    RoseObject* testobj = users.getObject(i);
	    if (testobj->isa(ROSE_DOMAIN(stp_geometric_item_specific_usage)))
		tomove(testobj, dest,forbidden);
	    else if (testobj->isa(ROSE_DOMAIN(stp_shape_aspect_relationship)))
		tomove(testobj, dest,forbidden);
	}
	return;
    }
    for (unsigned i = 0, sz = obj->attributes()->size(); i < sz; i++)
    {
	RoseObject * tst = obj->getObject(obj->attributes()->get(i));
	if (nullptr == tst) continue;
	tomove(tst, dest,forbidden);
    }
}
//void Remove