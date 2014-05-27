//Chris Made this code.
//5/20/14

#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <stix.h>
#include <string>
#include <map>
#include <iostream>
#include <cstdio>
#include "scan.h"

//if child has at least one parent outside of children returns false, if no parents outside of children it reutrns true
bool isOrphan(RoseObject * child, ListOfRoseObject * children){
	ListOfRoseObject parents;
	unsigned int k, sz;
	child->usedin(NULL, NULL, &parents); //finds parents
	for (k = 0, sz = parents.size(); k < sz; k++){
		RoseObject * parent = parents.get(k);
		if (!rose_is_marked(parent)){ //if parent is not marked then it is not a child of the object being split and needs to stay
			rose_mark_clear(child); //unmarks child to hopefully improve preformance on large operations
			return false;
		}
	}

	return true;
}

//Find which attribute of attributer attributee is and return it.
RoseAttribute * FindAttribute(RoseObject * Attributer, RoseObject * Attributee)
{
	RoseAttribute * Att;
	ListOfRoseAttribute * attributes = Attributer->attributes();
	//std::cout << "Looking for Entity ID #" << Attributee->entity_id() <<"And name: " <<Attributee->domain()->name() <<"\t";
	//std::cout << "Attributer Entity ID #" << Attributer->entity_id() << "And name: " << Attributer->domain()->name() << std::endl;
	for (unsigned int i = 0; i < attributes->size(); i++)
	{
		Att = attributes->get(i);
		if (!Att->isEntity())
		{
			if (!Att->isAggregate()) continue;	//If it isn't an entity or an enumeration, ignore it.

		}
		//std::cout << "\tAttribute #" << i << " Entity ID: " << ROSE_CAST(RoseObject, Att)->entity_id() << std::endl;
		if (Att->entity_id() == Attributee->entity_id()) return Att;
	}
	return NULL;
}

//takes pointer to a RoseObject from Master and creates a
int PutOut(RoseObject * obj){ //(product, master rose design) for splitting the code
	stp_product * prod = ROSE_CAST(stp_product, obj);
	stp_product * old_prod = prod;
	std::string ProdOutName = prod->name() + std::string("_split");
	RoseDesign * ProdOut = new RoseDesign(ProdOutName.c_str());
	//ListOfRoseObject refParents; depricated


	obj->copy(ProdOut, INT_MAX);	//scan & remove files from master as needed 
	ProdOut->save();
	//find prod in new design
	RoseCursor cursor;
	cursor.traverse(ProdOut);
	cursor.domain(ROSE_DOMAIN(stp_product));
	RoseObject * obj2;
	//std::cout << cursor.size() << std::endl;
	if (cursor.size() > 1){
		while (obj2 = cursor.next())	{
			stp_product * tmpProd = ROSE_CAST(stp_product, obj2);
			std::string forComp = tmpProd->name(); //allows use of .compare
			if (forComp.compare((prod->name())) == 0){
				prod = tmpProd;
				break;
			}
		}
	}
	else{
		prod = ROSE_CAST(stp_product, cursor.next());
	}
	///printf("\t%d\n", prod->entity_id());
	ProdOut->addName(prod->name(), prod); //add anchor to ProdOut

	ListOfRoseObject *children = new ListOfRoseObject;
	obj->findObjects(children, INT_MAX, ROSE_FALSE);	//children will be filled with obj and all of its children
	rose_mark_begin();
	rose_mark_set(obj);
	for (unsigned int i = 0; i < children->size(); i++){ //mark all children for orphan check
		RoseObject *child = children->get(i);
		if (rose_is_marked(child)){ continue; }
		else{ rose_mark_set(child); }
	}
	//std::cout << "Children to parse: " << children->size() <<std::endl;
	for (unsigned int i = 0; i < children->size(); i++)	{  //scan children to find parents, if orphan delete from master
		RoseObject *child = children->get(i);
		if (isOrphan(child, children)){ //if: child dose not have parents outside of children 
			//std::cout << "Moving " << child->entity_id() <<":" << child->className() << " to trash\n";
			rose_move_to_trash(child);
		}
		else{ continue; }
	}
	std::string refURI = std::string(prod->name() + std::string("_split") + std::string(".stp#") + prod->name());//uri for created reference to prod/obj


	//make reference to prodout file from master
	RoseReference *ref = rose_make_ref(obj->design(), refURI.c_str());
	ref->resolved(obj);
	MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
	URIManager = MyURIManager::make(obj);
	URIManager->should_go_to_uri(ref);
	/*	search_domain = ROSE_DOMAIN(RoseObject); //find usage of obj and replace it with ref
	search_att = search_domain->findTypeAttribute("owner");
	obj->usedin(NULL, NULL, &refParents);
	RoseObject * Parent;
	RoseAttribute * ParentAtt;
	unsigned int n = refParents.size();
	for (unsigned int i = 0; i < n; i++){
	Parent = refParents.get(i);
	ParentAtt = FindAttribute(Parent,obj);
	if (!ParentAtt) continue;	//Doesn't have the attribute so I guess we can skip it?
	rose_put_ref(ref, obj, ParentAtt);
	}*/ //to be deleted later
	ProdOut->save(); //save ProdOut as prod->id().stp

	rose_mark_end();

	delete ProdOut;
	return 0;
}

//split takes in a design and splits it into pieces. currently seperates every product into a new file linked to the orional file. 
int split(RoseDesign * master){
	//traverse to find obj that match type
	RoseCursor cursor;
	RoseObject * obj;

	cursor.traverse(master);
	cursor.domain(ROSE_DOMAIN(stp_product));
	//std::cout << cursor.size() << std::endl;
	while (obj = cursor.next()){
		PutOut(obj);
		rose_move_to_trash(obj);
	}
	update_uri_forwarding(master);
	master->save(); //save changes to master
	rose_empty_trash();
	return 0;
}


int main(int argc, char* argv[])
{
	stplib_init();	// initialize merged cad library
	//    rose_p28_init();	// support xml read/write
	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.

	/* Create a RoseDesign to hold the output data*/

	if (argc < 2){
		std::cout << "Usage: .\\STEPSplit.exe filetosplit.stp\n" << "\tCreates new file SplitOutput.stp as master step file with seperate files for each product" << std::endl;
		return EXIT_FAILURE;
	}
	std::string infilename = argv[1];

	RoseDesign * origional = ROSE.useDesign(argv[1]);
	origional->saveAs("SplitOutput.stp"); // creates a copy of the origonal file with a different name to make testing easier
	RoseDesign * master = ROSE.useDesign("SplitOutput.stp");
	//	rose_compute_backptrs(master);
	if (split(master) == 0) { std::cout << "Success!\n"; }

	return 0;
}