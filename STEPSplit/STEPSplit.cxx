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

#include <ctype.h>
#include <stix_asm.h>
#include <stix_tmpobj.h>
#include <stix_property.h>
#include <stix_split.h>

#pragma comment(lib,"stpcad_stix.lib")

//if child has at least one parent outside of children returns false, if no parents outside of children it reutrns true
bool isOrphan(RoseObject * child, ListOfRoseObject * children){
	ListOfRoseObject parents;
	unsigned int k, sz;
	child->usedin(NULL, NULL, &parents); //finds parents
	for (k = 0, sz = parents.size(); k < sz; k++){
		RoseObject * parent = parents.get(k);
		if (!rose_is_marked(parent)){ //if parent is not marked then it is not a child of the object being split and needs to stay
			//rose_mark_clear(child); //unmarks child to hopefully improve preformance on large operations
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
int PutOut(RoseObject * obj){ //(product,relative_dir) for splitting the code

	if (!obj) return 1; 

	stp_product_definition * prod = ROSE_CAST(stp_product_definition, obj);
	stp_product_definition * old_prod = prod;
	stp_product_definition_formation * prodf = prod->formation();
	stp_product * p = prodf ? prodf->of_product() : 0;

	std::string ProdOutName = std::string(p->name() + std::string("_split"));
/*	char * c = ProdOutName.c_str();  makes ProdOutName file system safe
	while (*c) {
		if (isspace(*c)) *c = '_';
		if (*c == '?') *c = '_';
		if (*c == '/') *c = '_';
		if (*c == '\\') *c = '_';
		if (*c == ':') *c = '_';
		if (*c == '"') *c = '_';
		if (*c == '\'') *c = '_';
		c++;
	}
	ProdOutName = c; */
	RoseDesign * ProdOut = new RoseDesign(ProdOutName.c_str());
	//ListOfRoseObject refParents; depricated


	obj->copy(ProdOut, INT_MAX);	//scan & remove files from master as needed 
	ProdOut->save();
	//find prod in new design
	RoseCursor cursor;
	cursor.traverse(ProdOut);
	cursor.domain(ROSE_DOMAIN(stp_product_definition));
	RoseObject * obj2;
	//std::cout << cursor.size() << std::endl;
	if (cursor.size() > 1){
		stp_product_definition * tmp_pd;
		stp_product_definition_formation * tmp_pdf;
		stp_product * tmp_p;
		std::string forComp;
		while (obj2 = cursor.next())	{
			tmp_pd = ROSE_CAST(stp_product_definition, obj2);
			tmp_pdf = tmp_pd->formation();
			tmp_p = tmp_pdf ? tmp_pdf->of_product() : 0;

			std::string forComp = tmp_p->name(); //allows use of .compare
			if (forComp.compare((p->name())) == 0){
				prod = tmp_pd;
				prodf = prod->formation();
				p = prodf ? prodf->of_product() : 0;
				break;
			}
		}
	}
	else{
		prod = ROSE_CAST(stp_product_definition, cursor.next());
	}
	///printf("\t%d\n", prod->entity_id());
	ProdOut->addName(p->name(), prod); //add anchor to ProdOut

	ListOfRoseObject *children = new ListOfRoseObject;
	obj->findObjects(children, INT_MAX, ROSE_FALSE);	//children will be filled with obj and all of its children
	//rose_mark_begin();
	rose_mark_set(obj);
	for (unsigned int i = 0; i < children->size(); i++){ //mark all children for orphan check
		RoseObject *child = children->get(i);
		if (rose_is_marked(child)){ continue; }
		else{ rose_mark_set(child); }
	}
	//mark subassembly, shape_annotation, and step_extras


	//std::cout << "Children to parse: " << children->size() <<std::endl;
	for (unsigned int i = 0; i < children->size(); i++)	{  //scan children to find parents, if orphan delete from master
		RoseObject *child = children->get(i);
		if (isOrphan(child, children)){ //if: child dose not have parents outside of children 
			//std::cout << "Moving " << child->entity_id() <<":" << child->className() << " to trash\n";
			rose_move_to_trash(child);
		}
		else{ continue; } //make reference to new object that replaces old one in master
	}
	std::string refURI = std::string(p->name() + std::string("_split") + std::string(".stp#") + p->name());//uri for created reference to prod/obj


	//make reference to prodout file from master
	RoseReference *ref = rose_make_ref(obj->design(), refURI.c_str());
	ref->resolved(obj);
	MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
	URIManager = MyURIManager::make(obj);
	URIManager->should_go_to_uri(ref);
	ProdOut->save(); //save ProdOut as prod->id().stp

	delete ProdOut;
	return 0;
}

//split takes in a design and splits it into pieces. currently seperates every product into a new file linked to the orional file. 
int split(RoseDesign * master){
	//traverse to find obj that match type
	//RoseCursor cursor;
	//RoseObject * obj;

	// Navigate through the assembly and export any part which has
	// geometry.
	unsigned int i,sz;
	StpAsmProductDefVec roots;
	stix_find_root_products (&roots, master);

	StixMgrSplitStatus::export_only_needed = 1;
	printf ("\nPRODUCT TREE ====================\n");
	rose_mark_begin();
	for (i = 0, sz = roots.size(); i < sz; i++){
		PutOut(roots[i]);
		rose_move_to_trash(roots[i]);
	}
	rose_mark_end();

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