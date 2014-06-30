//STEPSplit.cxx - Take in a step file and splits it into its constituent assemblies.
//
//Composed by Samson Bonfante, with major contributions from Martin Hardwick & Chris Dower
//
//Thanks to Joe & Dave
//6/30/14

#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <stix.h>
#include <ARM.h>
#include <stix_asm.h>
#include <stix_tmpobj.h>
#include <stix_property.h>
#include <stix_split.h>

#include <ctype.h>

#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <cstdio>

#include "scan.h"
#include "ARMRange.h"

#pragma comment(lib,"stpman_stix.lib")
#pragma comment(lib,"stpman.lib")
#pragma comment(lib,"stpman_arm.lib")

std::map<std::string,int> filenames;

RoseReference* addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir = "");
std::string SafeName(std::string name);

// Routines written by MH & adapted by Samson
bool * mBomSplit(Workpiece *root, bool repeat, std::string path, const char * root_dir, unsigned depth = 0);
Workpiece * find_root_workpiece(RoseDesign * des);
RoseDesign *export_workpiece(Workpiece * piece, const char * stp_file_name, bool is_master);
bool find_workpiece_contents(ListOfRoseObject &exports, Workpiece * piece, bool is_master);
bool find_approval_contents(ListOfRoseObject &exports, Approval * approval);
bool find_security_classification_contents(ListOfRoseObject &exports, Security_classification_assignment * sa);
bool find_style_contents(ListOfRoseObject &exports, Workpiece *piece, bool is_master);
bool style_applies_to_workpiece(Single_styled_item * ssi, Workpiece * piece, bool is_master);
RoseDesign * split_pmi(Workpiece * piece, const char * stp_file_name, unsigned depth, const char * root_dir);


int main(int argc, char* argv[])
{
	if (argc < 2){
		std::cout << "Usage: .\\STEPSplit.exe filetosplit.stp\n" << "\tCreates new file SplitOutput.stp as master step file with seperate files for each product" << std::endl;
		return EXIT_FAILURE;
	}
	ROSE.quiet(1);	//Suppress startup info.
	stplib_init();	// initialize merged cad library
	//    rose_p28_init();	// support xml read/write
	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.

	/* Create a RoseDesign to hold the output data*/
	std::string infilename(argv[1]);
	if (NULL == rose_dirname(infilename.c_str()))	//Check if there's already a path on the input file. If not, add '.\' AKA the local directory.
	{
		infilename = ".\\" + infilename;
	}
	if (!rose_file_readable(infilename.c_str()))	//Make sure file is readable before we open it.
	{
		std::cout << "Error reading input file." << std::endl;
		return EXIT_FAILURE;
	}
	RoseDesign * original = ROSE.useDesign(infilename.c_str());
	stix_tag_units(original);
	ARMpopulate(original);

	Workpiece *root = find_root_workpiece(original);
	if (root == NULL)
	{
		std::cout << "No Workpiece found in input" <<std::endl;
		return EXIT_FAILURE;
	}
	// directroy for all the geometry files
	std::cout << "Root name: " << root->get_its_id() <<'\n';
	std::string outfile_directory(root->get_its_id());
	unsigned sub_count = root->size_its_components();

	mBomSplit(root, true, outfile_directory, outfile_directory.c_str());
	return EXIT_SUCCESS;
}

bool * mBomSplit(Workpiece *root, bool repeat, std::string path, const char * root_dir, unsigned depth)
{
	std::cout << "\n\nMaking directory " << path <<"\n";
	rose_mkdir(path.c_str());

	// make directory for all the geometry components
	if (depth == 0) {
		std::string components(root_dir);
		components = components + "/geometry_components";
		rose_mkdir(components.c_str());
	}

	//Get all of the child workpieces into a vector.
	unsigned sub_count = root->size_its_components();
	std::cout << "Assembly " << root->get_its_id() << " has " << sub_count << " components" << std::endl;
	std::vector<RoseObject*> children,exported_children;	//TODO: figure out why in gods name these are RoseObject * instead of Workpiece *. Seems like lots of unnecessary converting back and forth. I get the feeling there is no reason.
	std::vector <RoseDesign *> subs;
	std::vector <std::string> exported_name;
	for (unsigned i = 0; i < sub_count; i++) {
		Workpiece_assembly_component * comp = Workpiece_assembly_component::find(root->get_its_components(i)->getValue());
		if (comp == NULL) continue;
		Workpiece * child = Workpiece::find(comp->get_component());
		if (child == NULL) continue;
		children.push_back(child->getRoot());
	}
	//For each child, find out if it needs an NAUO attached. Attach one if necessary.
	for (unsigned i = 0; i < children.size(); i++) {
		Workpiece *child = Workpiece::find(children[i]);
		bool need_nuao = false;
		for (unsigned j = 0; j < children.size(); j++) {
			if (j == i) continue;
			if (children[j] == children[i]) {
				need_nuao = true;
				break;
			}
		}
		std::string outfilename;
		if (path.size() > 0)
			outfilename = path + "/";
		else
			outfilename = path;

		if (need_nuao) {
			stp_next_assembly_usage_occurrence *nauo = root->get_its_components(i)->getValue();
			std::string fname(SafeName(nauo->name()));
			outfilename +=fname;
		}
		else
		{
			std::string fname(SafeName(child->get_its_id()));
			fname += std::to_string(++filenames[fname]);
			outfilename += fname;
		}
		exported_name.push_back(outfilename);
		outfilename = outfilename + ".stp";
		RoseDesign *sub_des = export_workpiece(child, outfilename.c_str(), false);
		std::cout << "Writing child to file: " << outfilename << std::endl;

		//	ARMsave (sub_des);
		Workpiece *exported_child = find_root_workpiece(sub_des);
		subs.push_back(sub_des);
		exported_children.push_back(exported_child->getRoot());

	}
	for (auto name : exported_name)
	{
		auto len = name.find_last_of('/') + 1;
		auto n= name.substr(len,name.size()-len-1);
		filenames[n]--;
	}
	// Now top level design
	std::string outfilename;
	if (path.size() > 0)
		outfilename = path + "/";
	else
		outfilename = path;
	std::string rootid = root->get_its_id();
	filenames[rootid]++;
	outfilename = outfilename + rootid + '_' + std::to_string(filenames[rootid]) + ".stp";
	RoseDesign *master = export_workpiece(root, outfilename.c_str(), true);
	Workpiece *master_root = find_root_workpiece(master);
	std::cout << "Writing master to file: " << outfilename <<" root name: " <<master_root->get_its_id() <<std::endl;

	// Change the workpiece of each top level component to the root of a new design
	for (unsigned i = 0; i < sub_count; i++) {
		Workpiece * exported_child = Workpiece::find(exported_children[i]);
		Workpiece_assembly_component * comp = Workpiece_assembly_component::find(master_root->get_its_components(i)->getValue());
		if (comp == NULL) continue;
		auto itr = exported_name[i].find_first_of('/');
		for (unsigned j = 0; j < depth; j++)
		{
			itr = exported_name[i].find('/', itr+1);
		}
		std::string subname(exported_name[i].begin()+itr+1, exported_name[i].end());
		std::cout << subname << '\n';
		std::string dirname(subname);
		//dirname = "";   // new strategy is for master to go in same directory as children

		stp_product_definition *pd = comp->get_component();
		stp_product_definition_formation *pdf = pd->formation();
		stp_product *p = pdf->of_product();
		rose_move_to_trash(p);
		rose_move_to_trash(pdf);
		rose_move_to_trash(pd);

		comp->put_component(exported_child->getRoot());
		addRefAndAnchor(exported_child->getRoot(), subs[i], master, dirname);

		ListOfRoseObject tmp;
		comp->getAIMObjects(&tmp);
		stp_representation_relationship *master_rep = NULL;
		for (unsigned j = 0; j < tmp.size(); j++) {
			if (tmp[j]->isa(ROSE_DOMAIN(stp_representation_relationship)))
				master_rep = ROSE_CAST(stp_representation_relationship, tmp[j]);
		}

		stp_representation *rep = master_rep->rep_1();
		master_rep->rep_1(exported_child->get_its_geometry());
		addRefAndAnchor(exported_child->get_its_geometry(), subs[i], master, dirname);
		rep->move(rose_trash(), -1);

		//	    subs[i]->save ();
		//	    ARMsave (subs[i]);
	}

	update_uri_forwarding(master);
	//	master->save ();
	ARMsave(master);

	if (repeat) {
		for (unsigned i = 0; i < exported_children.size(); i++) {
			Workpiece * exported_child = Workpiece::find(exported_children[i]);
			if (exported_child->size_its_components() > 0) {
				std::string sub_path(exported_name[i]);
				mBomSplit(exported_child, repeat, sub_path, root_dir, depth + 1);
			}
			else {
				split_pmi(exported_child, exported_name[i].c_str(), depth, root_dir);
				//		ARMsave (subs[i]);
			}
		}
	}

	return 0;
}

Workpiece *find_root_workpiece(RoseDesign *des)
{
	// find root workpiece or complain
	std::vector <stp_product_definition *> owned;   // workpieces that have a parent
	for (auto i : ARM_RANGE(Workpiece_assembly_component, des))
	{
		if (i.get_component())
		{
			owned.push_back(i.get_component());
		}
	}

	Workpiece * root = NULL;
	for (auto  &candidate : ARM_RANGE(Workpiece, des))
	{
		bool found = false;
		auto test = candidate.getRoot();
		for (auto i : owned) 
		{
			if (i == test) {
				found = true;					//If we are here, then this candidate is in the list of workpieces with a parent. Ergo, it is not root.
				break;
			}
		}
		if (!found) //If found is false then candidate does not have a parent. There should be only one (like highlander)
		{
			if (root != NULL) //Some other candidate got here. That is no good.
			{
				std::cout << "Error input has two root assemblies: " << root->get_its_id()
					<< " and " << candidate.get_its_id() << std::endl;
				return NULL;
			}
			root = &candidate;
		}
	}
	if (root == NULL) {	//Oh dear. If this is true, then every candidate had parents. Must be a loop in the assemblies.
		std::cout << "Error input has no root assembly: " << std::endl;
		return NULL;
	}
	return root;
}

RoseDesign * export_workpiece(Workpiece * piece, const char * stp_file_name, bool is_master)
{
	if (piece == NULL)
		return false;

	ListOfRoseObject exports;
	find_workpiece_contents(exports, piece, is_master);
	find_style_contents(exports, piece, is_master);

	/* After getting all the properties: do the following: */
	ARMresolveReferences(&exports);

	RoseDesign *new_des = ROSE.newDesign(stp_file_name);
	ListOfRoseObject * new_list;
	new_list = ROSE_CAST(ListOfRoseObject, exports.copy(new_des, INT_MAX,false)); // enough for selects and lists (10 was to few)

	//    printf ("Number of copied objects in file %s is %d\n", stp_file_name, new_list->size());
	delete new_list;  /* Don't want the list itself in the new design */

	stix_tag_units(new_des);
	ARMpopulate(new_des);

	auto new_model = Styled_geometric_model::newInstance(new_des);

	int style_count = 0;
	for (auto i : ARM_RANGE(Single_styled_item, new_des))
	{
		new_model->add_its_styled_items(i.getRoot());
		style_count++;
	}

	//    if (style_count != 0)
	//	printf ("Added %d styles to workpiece %s\n", style_count, piece->get_its_id());

	//    new_des->save ();

	return new_des;

}

RoseDesign * split_pmi(Workpiece * piece, const char * stp_file_name, unsigned depth, const char *root_dir)
{
	if (piece == NULL)
		return false;

	rose_mkdir(stp_file_name);

	// directory that contains all the geometry
	//TODO: break this out.
	std::string geometry(root_dir);
	std::string pieceid(piece->get_its_id());
	geometry += "/geometry_components/";
	geometry += pieceid;
	geometry += ".stp";

	ListOfRoseObject geo_exports;
	ListOfRoseObject style_exports;
	find_workpiece_contents(geo_exports, piece, false);
	find_style_contents(style_exports, piece, false);

	/* After getting all the properties: do the following: */
	ARMresolveReferences(&geo_exports);
	ARMresolveReferences(&style_exports);

	// for making local copy in current directory
	//    std::string geo_file (stp_file_name);
	//    geo_file = geo_file + "/geometry.stp";

	std::cout <<"Writing geometry to " <<geometry <<'\n';
	RoseDesign *geo_des = ROSE.newDesign(geometry.c_str());
	ListOfRoseObject * geo_list;
	geo_list = ROSE_CAST(ListOfRoseObject, geo_exports.copy(geo_des, 100)); // enough for selects and lists (10 was to few)

	std::cout << "Number of copied objects in geometry file " << stp_file_name << "_geo.stp is " << geo_list->size() << '\n';
	delete geo_list;  /* Don't want the list itself in the new design */

	stix_tag_units(geo_des);
	ARMpopulate(geo_des);
	Workpiece * component_piece = find_root_workpiece(geo_des);

	geo_des->addName("product_definition", component_piece->getRoot());
	geo_des->addName("shape_representation", component_piece->get_its_geometry());

	RoseCursor objs;
	objs.traverse(geo_des);
	objs.domain(ROSE_DOMAIN(stp_manifold_solid_brep));
	RoseObject *mani = objs.next();
	if (mani != NULL)
		geo_des->addName("manifold_solid_brep", mani);

	ARMsave(geo_des);
	Workpiece *geo_piece = find_root_workpiece(geo_des);

	filenames[pieceid]++;
	std::string numberedpieceid(pieceid);
	numberedpieceid += std::to_string(filenames[pieceid]);
	std::string pmi_file(stp_file_name);
	pmi_file += "/" + numberedpieceid;
	pmi_file += "_pmi.stp";

	RoseDesign *style_des = ROSE.newDesign(pmi_file.c_str());
	ListOfRoseObject * style_list;
	style_list = ROSE_CAST(ListOfRoseObject, style_exports.copy(style_des, INT_MAX)); // enough for selects and lists (10 was to few)

	std::cout << "Number of copied objects in style file " << stp_file_name << "_pmi.stp is " << style_list->size() << '\n';
	delete style_list;  /* Don't want the list itself in the new design */

	stix_tag_units(style_des);
	ARMpopulate(style_des);

	Styled_geometric_model * new_model = NULL;
	int style_count = 0;
	//for (auto ssi : ARM_RANGE(Single_styled_item,style_des))
	ARMCursor cur;
	cur.traverse(style_des);
	ARMObject * obj;
	while (NULL != (obj = cur.next()))
	{
		if (!obj->castToSingle_styled_item())
			continue;
		Single_styled_item * ssi = obj->castToSingle_styled_item();
		if (new_model == NULL) {
			new_model = Styled_geometric_model::newInstance(style_des);
			style_des->addName("styles", new_model->getRoot());
		}
		new_model->add_its_styled_items(ssi->getRoot());
		style_count++;
		if (ssi->get_its_geometry()) {
			stp_representation_item *repi = ssi->get_its_geometry();
			if (!repi->isa(ROSE_DOMAIN(stp_manifold_solid_brep))) {
				printf("Warning: style found not applied to a manifold solid\n");
				continue;
			}
			RoseReference *manifold = rose_make_ref(style_des, "#manifold_solid_solid_brep");
			stp_styled_item *style = ssi->getRoot();
			rose_put_ref(manifold, style, "item");
			// garbage collect
			repi->move(rose_trash(), -1);
		}
	}


	if (style_count != 0)
		std::cout << "Added " << style_count << " styles to workpiece " << pieceid << '\n';

	update_uri_forwarding(style_des);
	ARMsave(style_des);
	ARMsave(geo_des);

	std::string master_file(stp_file_name);
	master_file += "/";
	master_file += numberedpieceid;
	master_file += ".stp";

	RoseDesign *master_des = ROSE.newDesign(master_file.c_str());

	int count = 10;
	if (new_model) {
		
		RoseReference *styles = rose_make_ref(master_des, (numberedpieceid +"_pmi.stp#styles").c_str());
		master_des->addName("styles", styles);
		styles->entity_id(count);
		count = count + 10;
	}

	// directory that contains all the geometry
	std::string prefix("../");
	for (unsigned i = 0; i < depth; i++)
		prefix.append( "../");
	prefix+="geometry_components/";
	prefix+=pieceid;
	prefix+=".stp";

	std::string man_anchor(prefix);
	man_anchor+="#manifold_solid_brep";
	RoseReference *manifold = rose_make_ref(master_des, man_anchor.c_str());
	master_des->addName("manifold_solid_brep", manifold);
	manifold->entity_id(count);
	count = count + 10;

	std::string shape_anchor(prefix);
	shape_anchor +="#shape_representation";
	RoseReference *shape_rep = rose_make_ref(master_des, shape_anchor.c_str());
	master_des->addName("shape_representation", shape_rep);
	shape_rep->entity_id(count);
	count = count + 10;

	std::string definition_anchor(prefix);
	definition_anchor+="#product_definition";
	RoseReference *definition = rose_make_ref(master_des, definition_anchor.c_str());
	master_des->addName("product_definition", definition);
	definition->entity_id(count);
	count = count + 10;

	master_des->save();

	return geo_des;

}

// Find all the AIM object linked to a workpiece
// This function is recursive
bool find_workpiece_contents(ListOfRoseObject &exports, Workpiece * piece, bool is_master)
{
	ListOfRoseObject tmp;
	unsigned i,j;

	piece->getpath_its_geometry(&tmp);
	for (i = 0; i < tmp.size(); i++)
		exports.add(tmp[i]);

	unsigned count2 = piece->its_related_geometry.size();
	for (j = 0; j < count2; j++) {
		piece->its_related_geometry[j]->getPath(&tmp);
		for (i = 0; i < tmp.size(); i++)
			exports.add(tmp[i]);
	}

	//We don't use any of this stuff so we unset it? I dunno.
	piece->unset_product_approvals();
	piece->unset_revision_approvals();
	piece->unset_its_approvals();
	piece->unset_product_people();
	piece->unset_revision_people();
	piece->unset_its_people();
	piece->unset_product_orgs();
	piece->unset_revision_orgs();
	piece->unset_its_orgs();
	piece->unset_product_timestamps();
	piece->unset_revision_timestamps();
	piece->unset_its_timestamps();
	piece->unset_product_datestamps();
	piece->unset_revision_datestamps();
	piece->unset_its_datestamps();
	piece->unset_revision_security_classification();
	piece->unset_its_security_classification();


	piece->getAIMObjects(&tmp);
	for (i = 0; i < tmp.size(); i++)
	{
		if (!(tmp[i]->isa(ROSE_DOMAIN(stp_product_related_product_category))))
			exports.add(tmp[i]);
	}
	for (auto &cally : ARM_RANGE(Callout, piece->getRootObject()->design()))
	{
		if (cally.get_its_workpiece() == piece->getRoot()) {
			ListOfRoseObject amp2;
			cally.getAIMObjects(&amp2);
			for (i = 0; i < amp2.size(); i++)
				exports.add(amp2[i]);

			ARMCursor cur3;
			cur3.traverse(piece->getRootObject()->design());
			ARMObject * tmp3;
			while (NULL != (tmp3 = cur3.next())) {
				Geometric_tolerance_IF *tolly = tmp3->castToGeometric_tolerance_IF();
				if (tolly && tolly->get_applied_to() == cally.getRoot()) {
					ListOfRoseObject amp3;
					tolly->getAIMObjects(&amp3);
					for (i = 0; i < amp3.size(); i++)
						exports.add(amp3[i]);
				}

				Size_dimension_IF *dimmy = tmp3->castToSize_dimension_IF();
				if (dimmy && dimmy->get_applied_to() == cally.getRoot()) {
					ListOfRoseObject amp3;
					dimmy->getAIMObjects(&amp3);
					for (i = 0; i < amp3.size(); i++)
						exports.add(amp3[i]);
				}

				Location_dimension_IF *locy = tmp3->castToLocation_dimension_IF();
				if (locy && (locy->get_target() == cally.getRoot() || locy->get_origin() == cally.getRoot())) {
					ListOfRoseObject amp3;
					locy->getAIMObjects(&amp3);
					for (i = 0; i < amp3.size(); i++)
						exports.add(amp3[i]);
				}

				Surface_texture_parameter_IF *surfy = tmp3->castToSurface_texture_parameter_IF();
				if (surfy && surfy->get_applied_to() == cally.getRoot()) {
					ListOfRoseObject amp3;
					surfy->getAIMObjects(&amp3);
					for (i = 0; i < amp3.size(); i++)
						exports.add(amp3[i]);
				}
			}
		}
	}

	//Alas, the following case cannot be traversed with a simple ARM_RANGE- Single_datum_IF doesn't have a RoseManagerType!
	ARMCursor cur;
	cur.traverse(piece->getRootObject()->design());
	ARMObject * tmp2;
	while (NULL != (tmp2 = cur.next())) {

		Single_datum_IF *datty = tmp2->castToSingle_datum_IF();
		if (datty) {
			if (datty->get_its_workpiece() == piece->getRoot()) {
				ListOfRoseObject amp2;
				datty->getAIMObjects(&amp2);
				for (i = 0; i < amp2.size(); i++)
					exports.add(amp2[i]);
				for (auto dat : ARM_RANGE(Datum_reference, piece->getRootObject()->design()))
				{
					if (dat.get_referenced_datum() == datty->getRootObject()) {
						ListOfRoseObject amp3;
						dat.getAIMObjects(&amp3);
						for (i = 0; i < amp3.size(); i++)
							exports.add(amp3[i]);

					}
				}
			}
		}
	}

	// assuming recursive descent OK and that other code will remove duplicates when subassembly repeated
	unsigned count = piece->its_components.size();
	for (j = 0; j < count; j++) {

		//	trace.error ("Assembly %d of %d", i, count);

		Workpiece_assembly_component *ass = Workpiece_assembly_component::find(piece->its_components[j]->getValue());
		if (ass == NULL)
			continue;

		ass->getAIMObjects(&tmp);
		for (i = 0; i < tmp.size(); i++)
			exports.add(tmp[i]);

		Workpiece *child = Workpiece::find(ass->get_component());
		if (child != NULL && !is_master)
			find_workpiece_contents(exports, child, is_master);
	}

	return true;

}

// find the style items that apply to the contents
bool find_style_contents(ListOfRoseObject &exports, Workpiece * piece, bool is_master)
{
	ListOfRoseObject tmp;

	for (auto &ssi : ARM_RANGE(Single_styled_item, piece->getRootObject()->design()))
	{
		if (style_applies_to_workpiece(&ssi, piece, is_master)) 
		{
			ssi.getAIMObjects(&tmp);
			for (unsigned i = 0; i < tmp.size(); i++) exports.add(tmp[i]);
		}
	}
	return true;
}

// find if a style item applies to this workpiece or any of its children
bool style_applies_to_workpiece(Single_styled_item * ssi, Workpiece * piece, bool is_master)
{
	stp_representation_item *repi = NULL;
	if (ssi->get_its_geometry()) {
		repi = ssi->get_its_geometry();
	}
	else if (ssi->get_its_mapped_item()) {
		repi = ssi->get_its_mapped_item();
	}
	else if (ssi->get_its_topology()) {
		repi = ssi->get_its_topology();
	}

	if (repi == NULL)
		return false;

	if (piece->get_its_geometry()) {
		stp_representation *rep = piece->get_its_geometry();
		for (unsigned i = 0; i < rep->items()->size(); i++) {
			if (rep->items()->get(i) == repi) {
				return true;
			}
		}
	}
	for (unsigned i = 0; i < piece->size_its_related_geometry(); i++) {
		stp_representation *rep = piece->get_its_related_geometry(i)->getValue();
		for (unsigned j = 0; j < rep->items()->size(); j++) {
			if (rep->items()->get(j) == repi) {
				return true;
			}
		}
	}

	// If new file is for new master (parent) then do not worry about sub-assemblies
	if (is_master)
		return false;

	unsigned count = piece->its_components.size();
	for (unsigned j = 0; j < count; j++) {

		//	trace.error ("Assembly %d of %d", i, count);

		Workpiece_assembly_component *ass = Workpiece_assembly_component::find(piece->its_components[j]->getValue());
		if (ass == NULL)
			continue;

		Workpiece *child = Workpiece::find(ass->get_component());
		if (child != NULL)
		if (style_applies_to_workpiece(ssi, child, is_master))
			return true;
	}

	// did not find syle used in this workpiece
	return false;
}

bool find_approval_contents(ListOfRoseObject &exports, Approval * ap)
{

	ListOfRoseObject tmp;
	unsigned i;
	unsigned j;

	ap->getAIMObjects(&tmp);
	for (i = 0; i < tmp.size(); i++)
		exports.add(tmp[i]);

	unsigned psize = ap->size_its_approving_person_organization();
	for (j = 0; j < psize; j++) {
		Approving_person_organization *peon =
			Approving_person_organization::find(ap->get_its_approving_person_organization(j)->getValue());
		if (peon) {
			peon->getAIMObjects(&tmp);
			for (i = 0; i < tmp.size(); i++)
				exports.add(tmp[i]);
		}
	}

	unsigned dsize = ap->size_date_time();
	for (j = 0; j < dsize; j++) {
		Approval_date_time *daty = Approval_date_time::find(ap->get_date_time(j)->getValue());
		if (daty) {
			daty->getAIMObjects(&tmp);
			for (i = 0; i < tmp.size(); i++)
				exports.add(tmp[i]);
		}
	}

	return true;
}

bool find_security_classification_contents(ListOfRoseObject &exports, Security_classification_assignment * sa)
{
	ListOfRoseObject tmp;
	unsigned i;
	unsigned j;

	sa->getAIMObjects(&tmp);
	for (i = 0; i < tmp.size(); i++)
		exports.add(tmp[i]);

	Security_classification *sec = Security_classification::find(sa->get_classification());

	if (sec) {
		sec->getAIMObjects(&tmp);
		for (i = 0; i < tmp.size(); i++)
			exports.add(tmp[i]);
		Approval *ap = Approval::find(sec->get_its_approval());
		if (ap) {
			find_approval_contents(exports, ap);
		}
		unsigned sizep = sec->size_person();
		for (j = 0; j < sizep; j++) {
			Assigned_person *peon = Assigned_person::find(sec->person[j]->getValue());
			if (peon) {
				peon->getAIMObjects(&tmp);
				for (i = 0; i < tmp.size(); i++)
					exports.add(tmp[i]);
			}
		}
		unsigned sizet = sec->size_time();
		for (j = 0; j < sizet; j++) {
			Assigned_time *teon = Assigned_time::find(sec->time[j]->getValue());
			if (teon) {
				teon->getAIMObjects(&tmp);
				for (i = 0; i < tmp.size(); i++)
					exports.add(tmp[i]);
			}
		}

	}

	return true;
}

RoseReference* addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir){ //obj from output file, and master file for putting refs into
	std::string anchor((const char*)obj->domain()->name());	//anchor now looks like "advanced_face" or "manifold_solid_brep"
	//	anchor.append("_split_item");				//"advanced_face_split_item"
	//	if (obj->entity_id() == 0){ std::cout << anchor << " " << obj->domain()->typeIsSelect() << obj->entity_id() << std::endl; }
	//	anchor.append(std::to_string(obj->entity_id()));	//ex. "advanced_face_split_item123"

	ProdOut->addName(anchor.c_str(), obj);	//This makes the anchor.

	std::string reference(dir);
	if (reference.size() > 0)
		reference = reference + "/";
	//	std::string reference(dir + "/");	//let's make the reference text. start with the output directory 
	//	int slashpos = reference.find("/");
	//	if (slashpos > 0 && slashpos < reference.size()){
	//		reference = reference.substr(slashpos + 1);
	//	}
	reference.append(ProdOut->name());	//Add the file name.
	reference.append(".stp#" + anchor); //Finally add the file type, a pound, and the anchor. It'll look like "folder/file.stp#advanced_face_split_item123"

	RoseReference *ref = rose_make_ref(master, reference.c_str());	//Make a reference in the master output file.
	ref->resolved(obj);	//Reference is resolved to the object that we passed in, which is currently residing in the ProdOut design.
	MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
	URIManager = MyURIManager::make(obj);
	//URIManager->should_go_in_des(obj->design());
	URIManager->should_go_to_uri(ref);
	return ref;
}

std::string SafeName(std::string name){
	for (auto &c : name)
	{
		if (isspace(c) || c < 32 || c == '<' || c == '>' || c == ':' || c == '\"' || c == '\\' || c == '/' || c == '|' || c == '?' || c == '*')
			c = '_';
	}
	return name;
}
