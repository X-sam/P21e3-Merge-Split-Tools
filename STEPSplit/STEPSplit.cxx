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

#pragma comment(lib,"stpcad_stix.lib")


//################# From Dave's Code ################################
static DictionaryOfRoseObject written_filenames;

static void add_parts_dfs_order(
	stp_product_definition * pd,
	StpAsmProductDefVec &dfslist
	)
{
	if (!pd) return;

	unsigned i, sz;
	StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);

	// Already visited?
	for (i = 0, sz = dfslist.size(); i<sz; i++)
		if (dfslist[i] == pd)  return;

	// Add all subproducts first
	for (i = 0, sz = pm->child_nauos.size(); i<sz; i++)
	{
		add_parts_dfs_order(
			stix_get_related_pdef(pm->child_nauos[i]), dfslist
			);
	}

	// Who knows, it could happen.
	for (i = 0, sz = dfslist.size(); i<sz; i++)
		if (dfslist[i] == pd)  return;

	dfslist.append(pd);
}

RoseStringObject get_part_filename(
	stp_product_definition * pd
	)
{
	RoseStringObject name("part");
	stp_product_definition_formation * pdf = pd->formation();
	stp_product * p = pdf ? pdf->of_product() : 0;

		char * pname = p ? p->name() : 0;
		if (!pname || !*pname) pname = (char *) "none";

		if (!name.is_empty()) name += "_";
		name += pname;

		// change whitespace and other non filesystem safe
		// characters to underscores
		//
		char * c = name;
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

		char idstr[100];
		sprintf(idstr, "id%lu", pd->entity_id());

		if (!name.is_empty()) name += "_";
		name += idstr;

	// CHECK FOR DUPLICATES AND WARN
	RoseObject * obj = written_filenames.find(name);
	if (obj && obj != p) {
		printf("WARNING: Products #%lu and #%lu will export to the same file name!\n",
			obj->entity_id(), p->entity_id());
	}
	else written_filenames.add(name, p);

	if (pd->design()->fileExtension()) {
		name += ".";
		name += pd->design()->fileExtension();
	}

	return name;
}
//###################################################################


//if child has at least one parent outside of children returns false, if no parents outside of children it reutrns true
bool isOrphan(RoseObject * child, ListOfRoseObject * children){
	ListOfRoseObject parents;
	unsigned int k, sz;
	child->usedin(NULL,NULL, &parents); //finds parents
	for (k = 0, sz = parents.size(); k < sz; k++){
		RoseObject * parent = parents.get(k);
		if (!rose_is_marked(parent) ){ //if parent is not marked then it is not a child of the object being split and needs to stay
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
			if(!Att->isAggregate()) continue;	//If it isn't an entity or an enumeration, ignore it.
				
		}
		//std::cout << "\tAttribute #" << i << " Entity ID: " << ROSE_CAST(RoseObject, Att)->entity_id() << std::endl;
		if(Att->entity_id() == Attributee->entity_id()) return Att;
	}
	return NULL;
}

//takes pointer to a RoseObject from Master and creates a
int PutOut(RoseObject * obj){ //(product, master rose design) for splitting the code
	stp_product_definition * prod_def = ROSE_CAST(stp_product_definition, obj); //becomes a reference to prod in created file
	stp_product_definition * old_prod_def = prod_def; //old prod stays a reference to prod in the master file
	
	StixMgrSplitProduct * split_mgr = StixMgrSplitProduct::find(prod_def);
	if (split_mgr) return 1;   // already been exported
	StixMgrAsmProduct * pd_mgr = StixMgrAsmProduct::find(prod_def);
	if (!pd_mgr) return 1;  // not a proper part

	split_mgr = new StixMgrSplitProduct;
	split_mgr->part_filename = get_part_filename(prod_def);//creates left side of RefURI
	prod_def->add_manager(split_mgr);//holds filename, which is also left side of RefURI

	//for finding name of the product
	stp_product_definition_formation * pdf = prod_def->formation();
	stp_product * p = pdf ? pdf->of_product() : 0;

	RoseDesign * ProdOut = new RoseDesign(split_mgr->part_filename);
	ListOfRoseObject refParents;

	
	obj->copy(ProdOut, INT_MAX);	//scan & remove files from master as needed 
	ProdOut->save();
	//find prod in new design
	RoseCursor cursor;
	cursor.traverse(ProdOut);
	cursor.domain(ROSE_DOMAIN(stp_product_definition));
	RoseObject * obj2;
	//std::cout << cursor.size() << std::endl;
	if (cursor.size() > 1){
		while (obj2 = cursor.next())	{
			stp_product_definition * tmpProd_def = ROSE_CAST(stp_product_definition, obj2);
			stp_product_definition_formation * tmp_pdf = tmpProd_def->formation();
			stp_product * tmp_p = tmp_pdf ? tmp_pdf->of_product() : 0;
			std::string forComp = tmp_p->name(); //allows use of .compare
			if (forComp.compare((p->name())) == 0){
				prod_def = tmpProd_def;
				break;
			}
		}
	}
	else{
		prod_def = ROSE_CAST(stp_product_definition, cursor.next());
	}
	//sets name references items to referece new object
	pdf = prod_def->formation();
	p = pdf ? pdf->of_product() : 0;

	///printf("\t%d\n", prod->entity_id());
	ProdOut->addName(p->name(), prod_def); //add anchor to ProdOut

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
	std::string refURI = std::string(split_mgr->part_filename) + std::string("#") + p->name();//uri for created reference to prod/obj
	

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
	unsigned i, sz;
	StpAsmProductDefVec roots;
	StpAsmProductDefVec dfslist;
	stix_find_root_products(&roots, master);

	for (i = 0, sz = roots.size(); i<sz; i++)
		add_parts_dfs_order(roots[i], dfslist);

	StixMgrSplitStatus::export_only_needed = 1;
	for (i = 0, sz = dfslist.size(); i<sz; i++) {
		stix_split_delete_all_marks(master);
		PutOut(dfslist[i]);
	}
	return 0;
	/* old non-stix code
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
	*/
}


int main(int argc, char* argv[])
{
    stplib_init();	// initialize merged cad library
//    rose_p28_init();	// support xml read/write
	FILE *out;
	out=fopen("log.txt","w");
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