#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <stix.h>
#include <string>
#include <map>
#include <iostream>
#include <cstdio>
#include "scan.h"
#include <ARM.h>
#include <ctype.h>
#include <stix_asm.h>
#include <stix_tmpobj.h>
#include <stix_property.h>
#include <stix_split.h>

#pragma comment(lib,"stpcad_stix.lib")

static int has_geometry(stp_representation * rep)
{
	unsigned i, sz;

	if (!rep) return 0;

	// Does this contain more than just axis placements?
	for (i = 0, sz = rep->items()->size(); i<sz; i++) {
		stp_representation_item * it = rep->items()->get(i);
		if (!it->isa(ROSE_DOMAIN(stp_placement)))
			return 1;
	}

	// Look at this any related shape reps that are not part of an
	// assembly structure.  These are held in the child_rels list but
	// have null nauo pointers.

	StixMgrAsmShapeRep * mgr = StixMgrAsmShapeRep::find(rep);
	if (!mgr) return 0;

	for (i = 0, sz = mgr->child_rels.size(); i<sz; i++) {
		StixMgrAsmRelation * relmgr =
			StixMgrAsmRelation::find(mgr->child_rels[i]);

		if (relmgr && !relmgr->owner && has_geometry(relmgr->child))
			return 1;
	}
	return 0;
}

std::string SafeName(std::string name){
	int spacepos = name.find(' ');	//Finds first space in filename, if any.
	while (spacepos != std::string::npos)
	{
		name[spacepos] = '_';	//Replaces space with underscore, for filesystem safety.
		spacepos = name.find(' ', spacepos + 1);
	}
	int c = 0; int sz = name.length();
	while (c < sz){
		if (name[c] == '?') name[c] = '_';
		if (name[c] == '/') name[c] = '_';
		if (name[c] == '\\') name[c] = '_';
		if (name[c] == ':') name[c] = '_';
		if (name[c] == '"') name[c] = '_';
		if (name[c] == '\'') name[c] = '_';
		c++;
	}

	return name;
}

void moveGeometry(RoseObject * root, RoseDesign * dst){
	ListOfRoseObject assemObjs;
	assemObjs.putObject(root);
	rose_mark_begin();

	unsigned i, sz;
	RoseObject * assemObj;
	ARMresolveReferences(&assemObjs);
	std::cout << "\t" << assemObjs.size() << std::endl;
	for (i = 0, sz = assemObjs.size(); i < sz; i++)
	{
		assemObj = assemObjs[i];
		//moves evyerthing
		assemObj->move(dst, 0);
	}
}

int makeDesFromProd(stp_product_definition * root, std::string dir){
	if (!root) return 1;
	RoseDesign * src = root->design();
	stp_product * p = root->formation() ? root->formation()->of_product() : 0;
	std::string dstName(p->name());
	dstName.append("_split" + std::to_string(p->entity_id()));
	dstName = SafeName(dstName);


	RoseDesign * dst = pnew RoseDesign(dstName.c_str() );
	dst->fileDirectory(dir.c_str());
	moveGeometry(root, dst);

	dst->save();
	return 0;
}

int createFoldersandFiles(RoseObject * obj, std::string dir){
	std::cout << obj->domain()->name() << std::endl;
	stp_product_definition * assem = ROSE_CAST(stp_product_definition, obj);
	StixMgrAsmProduct * pm = StixMgrAsmProduct::find(assem);
	stp_product_definition_formation * pdf = assem->formation();
	stp_product * p = pdf ? pdf->of_product() : 0;
	unsigned i, sz;

	// Does this have real shapes?
	if (pm->child_nauos.size()) {
		//create directory
		dir.append("/");
		dir.append(SafeName(p->name()));
		std::string tmpdir = dir + "1";

		if (!rose_dir_exists(tmpdir.c_str())){
			rose_mkdir(tmpdir.c_str());
		}
		else{
			i = 2;
			tmpdir.pop_back(); tmpdir.append(std::to_string(i));
			while (rose_dir_exists(tmpdir.c_str())){
				i++;
				tmpdir.pop_back(); tmpdir.append(std::to_string(i));
			}
			rose_mkdir(tmpdir.c_str());
		}
		for (i = 0, sz = pm->child_nauos.size(); i < sz; i++)		{
			createFoldersandFiles(stix_get_related_pdef(pm->child_nauos[i]), tmpdir);
		}
	}
	else {
		for (i = 0, sz = pm->shapes.size(); i<sz; i++) {
			if (has_geometry(pm->shapes[i])) break;
		}

		// no shapes with real geometry
		if (i<sz) {
			//printf("EXPORTING PD #%lu (%s)\n", 		obj->entity_id(), p->name() ? p->name() : "");
			makeDesFromProd(assem, dir);
		}
		else {
			//printf("IGNORING PD #%lu (%s) (no geometry)\n",
			//	obj->entity_id(), p->name() ? p->name() : "");
			return 0;
		}
	}
	return 0;
}

int split(RoseDesign * master, std::string dir){
	unsigned int i, sz;

	StpAsmProductDefVec roots; //initiliaze and find assemblies in master
	stix_find_root_products(&roots, master);

	stix_tag_units(master); //initilize arm stuff
	ARMpopulate(master);

	rose_compute_backptrs(master);
	stix_tag_asms(master);
	StixMgrProperty::tag_design(master);
	StixMgrPropertyRep::tag_design(master);

	StixMgrSplitStatus::export_only_needed = 1;

	rose_mark_begin();
	for (i = 0, sz = roots.size(); i < sz; i++){
		createFoldersandFiles(roots[i], dir);
	}
	if (sz == 0) { std::cout << "No assmblies" << std::endl; return 1; }
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
	std::string dir = "out";
	if (argv[2]){ dir = argv[2]; }
	rose_mkdir(dir.c_str());
	origional->fileDirectory(dir.c_str());
	origional->saveAs("Master.stp"); // creates a copy of the origonal file with a different name to make testing easier
	RoseDesign * master = ROSE.useDesign((dir + "/" + "Master.stp").c_str());
	master->fileDirectory(dir.c_str());
	//dir = "";
	if (split(master, dir) == 0) { std::cout << "Success!\n"; }
	else{
		std::cout << "No assemblies in design" << std::endl;
		return 1;
	}
	return 0;
}