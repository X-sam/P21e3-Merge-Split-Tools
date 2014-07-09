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
#include "ROSERange.h"
#pragma comment(lib,"stpcad_stix.lib")
#pragma comment(lib,"stpcad.lib")
#pragma comment(lib,"stpcad_arm.lib")
#pragma comment(lib,"stmodule.lib")

StplibSchemaType schemas;	//Used so that all split output has the same schema as the input file.

RoseReference* addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir = "");
std::string SafeName(std::string name);

// Routines written by MH & adapted by Samson
int mBomSplit(Workpiece *root, bool repeat, std::string path, const char * root_dir, unsigned depth = 0);
Workpiece * find_root_workpiece(RoseDesign * des);
RoseDesign *export_workpiece(Workpiece * piece, const char * stp_file_name, bool is_master);
bool find_workpiece_contents(ListOfRoseObject &exports, Workpiece * piece, bool is_master);
//bool find_approval_contents(ListOfRoseObject &exports, Approval * approval);
//bool find_security_classification_contents(ListOfRoseObject &exports, Security_classification_assignment * sa);
bool find_style_contents(ListOfRoseObject &exports, Workpiece *piece, bool is_master);
bool style_applies_to_workpiece(Single_styled_item * ssi, Workpiece * piece, bool is_master);
RoseDesign *split_pmi(Workpiece * piece, const char * stp_file_name, unsigned depth, const char * root_dir);


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
		std::cerr << "Error reading input file." << std::endl;
		return EXIT_FAILURE;
	}
	RoseDesign * original = ROSE.useDesign(infilename.c_str());
	stix_tag_units(original);
	ARMpopulate(original);
	schemas = stplib_get_schema(original);	//Load the schemas from original. They have to go in all of the child files.
	Workpiece *root = find_root_workpiece(original);
	if (root == NULL)
	{
		std::cerr << "No Workpiece found in input" <<std::endl;
		return EXIT_FAILURE;
	}
	// directroy for all the geometry files
	std::string outfile_directory(root->get_its_id());

	return mBomSplit(root, true, outfile_directory, outfile_directory.c_str());

}

int mBomSplit(Workpiece *root, bool repeat, std::string path, const char * root_dir, unsigned depth)
{
//	std::cout << "\n\nMaking directory " << path << "\n";
	rose_mkdir(path.c_str());
	//Make a locally-scoped design to put all our trash in. we will move it to the trash when we are done.
	RoseDesign *garbage = new RoseDesign;

	// make directory for all the geometry components
	if (depth == 0) {
		std::string components(root_dir);
		components = components + "/geometry_components";
		rose_mkdir(components.c_str());
	}

	//Get all of the child workpieces into a vector.
	unsigned sub_count = root->size_its_components();
//	std::cout << "Assembly " << root->get_its_id() << " has " << sub_count << " components" << std::endl;
	std::vector<Workpiece*> children, exported_children;
	std::vector <RoseDesign *> subs;
	std::vector <std::string> exported_name;
	for (unsigned i = 0; i < sub_count; i++) {
		Workpiece_assembly_component * comp = Workpiece_assembly_component::find(root->get_its_components(i)->getValue());
		if (comp == NULL) continue;
		Workpiece * child = Workpiece::find(comp->get_component());
		if (child == NULL) continue;
		children.push_back(child);
	}
	//For each child, find out if it needs an NAUO attached. Attach one if necessary.
	for (unsigned i = 0u,sz=children.size(); i < sz; i++) {
		Workpiece *child = children[i];
		bool need_nuao = false;
		for (auto j = 0u; j < sz; j++) {
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
			std::string fname(nauo->id());
			fname = SafeName(fname);
			outfilename += fname +'-';
			fname = SafeName(nauo->name());
			outfilename += fname;
		}
		else
		{
			std::string fname(child->get_its_id());
			fname = SafeName(fname);
			outfilename += fname;
		}
		exported_name.push_back(outfilename);
		outfilename = outfilename + ".stp";
		RoseDesign *sub_des = export_workpiece(child, outfilename.c_str(), false);
		std::cout << "Writing child to file: " << outfilename << " (" << i+1 << "/" << children.size() << ")\n";

		//	ARMsave (sub_des);
		Workpiece *exported_child = find_root_workpiece(sub_des);
		subs.push_back(sub_des);
		exported_children.push_back(exported_child);

	}

	// Now top level design
	std::string outfilename;
	if (path.size() > 0)
		outfilename = path + "/";
	else
		outfilename = path;

	outfilename += "master.stp";

	RoseDesign *master = export_workpiece(root, outfilename.c_str(), true);
	Workpiece *master_root = find_root_workpiece(master);

	master->addName("product_definition", master_root->getRootObject());
	master->addName("shape_representation", master_root->get_its_geometry());

//	std::cout << "Writing master to file: " << outfilename << std::endl;

	// Change the workpiece of each top level component to the root of a new design
	for (auto i = 0u; i < sub_count; i++) {
		Workpiece * exported_child = exported_children[i];
		Workpiece_assembly_component * comp = Workpiece_assembly_component::find(master_root->get_its_components(i)->getValue());
		if (comp == NULL) continue;
		auto itr = exported_name[i].find_first_of('/');
		for (unsigned j = 0; j < depth; j++)
		{
			itr = exported_name[i].find('/', itr + 1);
		}
//		std::string subname(exported_name[i].begin() + itr + 1, exported_name[i].end());
//		std::cout << subname << '\n';
//		std::string dirname(subname);
		std::string dirname = "";   // new strategy is for master to go in same directory as children

		stp_product_definition *pd = comp->get_component();
		stp_product_definition_formation *pdf = pd->formation();
		stp_product *p = pdf->of_product();
		rose_move_to_design(p,garbage);
		rose_move_to_design(pdf,garbage);
		rose_move_to_design(pd,garbage);

		comp->put_component(exported_child->getRoot());
		addRefAndAnchor(exported_child->getRoot(), subs[i], master, dirname);

		ListOfRoseObject tmp;
		
		stp_representation_relationship *master_rep = 
			ROSE_CAST(stp_representation_relationship,comp->getpath_resulting_orientation(&tmp)->get(3));
		if (master_rep == nullptr)
		{
			std::cerr << "Error getting Representation Relationship.\n";
			return EXIT_FAILURE;
		}


		stp_representation *rep = master_rep->rep_1();
		master_rep->rep_1(exported_child->get_its_geometry());
		addRefAndAnchor(exported_child->get_its_geometry(), subs[i], master, dirname);
		rep->move(garbage, -1);

	}

	update_uri_forwarding(master);

	stplib_put_schema(master, schemas);
	ARMsave(master);

	if (repeat) 
	{
		for (unsigned i = 0u,sz=exported_children.size(); i < sz; i++) 
		{
			Workpiece * exported_child = exported_children[i];
			if (exported_child->size_its_components() > 0) 
			{
				std::string sub_path(exported_name[i]);
				mBomSplit(exported_child, repeat, sub_path, root_dir, depth + 1);
			}
			else 
			{
				RoseDesign * result =split_pmi(exported_child, exported_name[i].c_str(), depth, root_dir);
				if (result != nullptr) result->move(garbage, -1);	//Don't need the geometry in memory.
				//		ARMsave (subs[i]);
			}
		}
	}
	rose_move_to_trash(garbage);
	if(depth < 2) rose_empty_trash();	//Don't empty the trash too often, it's slow.
	return EXIT_SUCCESS;
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
				std::cerr << "Error: input has two root assemblies: " << root->get_its_id()
					<< " and " << candidate.get_its_id() << std::endl;
				return NULL;
			}
			root = &candidate;
		}
	}
	if (root == NULL) {	//Oh dear. If this is true, then every candidate had parents. Must be a loop in the assemblies.
		std::cerr << "Error: input has no root assembly: " << std::endl;
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
	new_list = ROSE_CAST(ListOfRoseObject, exports.copy(new_des, INT_MAX,false)); 

	//    printf ("Number of copied objects in file %s is %d\n", stp_file_name, new_list->size());
	delete new_list;  /* Don't want the list itself in the new design */

	stix_tag_units(new_des);
	ARMpopulate(new_des);

	if (is_master) return new_des;


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


RoseDesign *move_geometry(Workpiece * piece, const char * root_dir)
{
	// directory that contains all the geometry	
	std::string geometry(root_dir);
	std::string pieceid(piece->get_its_id());
	geometry += "/geometry_components/";
	geometry += pieceid;
	geometry += ".stp";
	if (rose_file_exists(geometry.c_str()))
	{
		return nullptr;
	}
	ListOfRoseObject geo_exports;
	find_workpiece_contents(geo_exports, piece, false);

	/* After getting all the properties: do the following: */
	ARMresolveReferences(&geo_exports);

	// for making local copy in current directory
	//    std::string geo_file (stp_file_name);
	//    geo_file = geo_file + "/geometry.stp";

	//std::cout <<"Writing geometry to " <<geometry <<'\n';
	RoseDesign *geo_des = ROSE.newDesign(geometry.c_str());
	ListOfRoseObject * geo_list;
	geo_list = ROSE_CAST(ListOfRoseObject, geo_exports.copy(geo_des, INT_MAX));

	delete geo_list;  /* Don't want the list itself in the new design */

	stix_tag_units(geo_des);
	ARMpopulate(geo_des);
	Workpiece * component_piece = find_root_workpiece(geo_des);

	//TODO: Make a list of things that reference geometry and anchor them here.
	geo_des->addName(component_piece->getRoot()->domain()->name(), component_piece->getRoot());
	geo_des->addName(component_piece->get_its_geometry()->domain()->name(), component_piece->get_its_geometry());

	RoseCursor objs;
	objs.traverse(geo_des);
	objs.domain(ROSE_DOMAIN(stp_manifold_solid_brep));
	RoseObject *mani = objs.next();
	if (mani != NULL)
		geo_des->addName("manifold_solid_brep", mani);

	stplib_put_schema(geo_des, schemas);
	ARMsave(geo_des);
	return geo_des;
}

RoseDesign *split_pmi(Workpiece * piece, const char * stp_file_name, unsigned depth, const char *root_dir)
{
	if (piece == NULL)
		return false;

	rose_mkdir(stp_file_name);

	RoseDesign * geo_des = move_geometry(piece, root_dir);
	std::string pieceid(piece->get_its_id());

	ListOfRoseObject style_exports;
	find_style_contents(style_exports, piece, false);
	ARMresolveReferences(&style_exports);

	std::string pmi_file(stp_file_name);
	pmi_file += "/pmi.stp";

	RoseDesign *style_des = ROSE.newDesign(pmi_file.c_str());
	ListOfRoseObject * style_list;
	style_list = ROSE_CAST(ListOfRoseObject, style_exports.copy(style_des, INT_MAX)); // enough for selects and lists (10 was to few)

	//std::cout << "Number of copied objects in style file " << stp_file_name << "_pmi.stp is " << style_list->size() << '\n';
	delete style_list;  /* Don't want the list itself in the new design */

	stix_tag_units(style_des);
	ARMpopulate(style_des);

	Styled_geometric_model * new_model = NULL;
	int style_count = 0;
	for (auto &ssi : ARM_RANGE(Single_styled_item, style_des))
	{
		if (new_model == NULL) {
			new_model = Styled_geometric_model::newInstance(style_des);
			style_des->addName("styles", new_model->getRoot());
		}
		new_model->add_its_styled_items(ssi.getRoot());
		style_count++;
		if (ssi.get_its_geometry()) {
			stp_representation_item *repi = ssi.get_its_geometry();
			std::string master_name("master.stp#");
			master_name += repi->domain()->name();
			RoseReference *entity_reference = rose_make_ref(style_des, master_name.c_str());
			stp_styled_item *style = ssi.getRoot();
			rose_put_ref(entity_reference, style, "item");
			repi->move(rose_trash(), -1);
		}

	}

	update_uri_forwarding(style_des);

	stplib_put_schema(style_des, schemas);
	ARMsave(style_des);


	std::string master_file(stp_file_name);
	master_file += "/master.stp";

	RoseDesign *master_des = ROSE.newDesign(master_file.c_str());

	int count = 10;
	if (new_model) {
		
		RoseReference *styles = rose_make_ref(master_des, "pmi.stp#styles");
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

	stplib_put_schema(master_des, schemas);
	master_des->save();

	return geo_des;

}

// Find all the AIM object linked to a workpiece
// This function is recursive
bool find_workpiece_contents(ListOfRoseObject &exports, Workpiece * piece, bool is_master)
{
	ListOfRoseObject tmp;

	piece->getpath_its_geometry(&tmp);
	for (auto i = 0u; i < tmp.size(); i++)
		exports.add(tmp[i]);

	unsigned count2 = piece->its_related_geometry.size();
	for (auto j = 0u; j < count2; j++) {
		piece->its_related_geometry[j]->getPath(&tmp);
		for (auto i = 0u,sz=tmp.size(); i < sz; i++)
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
	for (auto i = 0u, sz = tmp.size(); i < sz; i++)
	{
		if (!(tmp[i]->isa(ROSE_DOMAIN(stp_product_related_product_category))))
			exports.add(tmp[i]);
	}
	for (auto &cally : ARM_RANGE(Callout, piece->getRootObject()->design()))
	{
		if (cally.get_its_workpiece() == piece->getRoot()) {
			ListOfRoseObject amp2;
			cally.getAIMObjects(&amp2);
			for (auto i = 0u, sz = amp2.size(); i < sz; i++)
			{
				exports.add(amp2[i]);
			}
			for (auto &i : ROSE_RANGE(stp_geometric_tolerance, piece->getRootObject()->design()))
			{
				auto tolly = Geometric_tolerance_IF::find(&i);
				if (tolly)
				{
					if (tolly->get_applied_to() == cally.getRoot())
					{
						ListOfRoseObject amp3;
						tolly->getAIMObjects(&amp3);
						for (auto j = 0u, sz = amp3.size(); j < sz; j++)
						{
							exports.add(amp3[j]);
						}
					}
				}
			}
			for (auto &i : ROSE_RANGE(stp_dimensional_size, piece->getRootObject()->design()))
			{
				auto tolly = Size_dimension_IF::find(&i);
				if (tolly)
				{
					if (tolly->get_applied_to() == cally.getRoot())
					{
						ListOfRoseObject amp3;
						tolly->getAIMObjects(&amp3);
						for (auto j = 0u, sz = amp3.size(); j < sz; j++)
						{
							exports.add(amp3[j]);
						}
					}
				}
			}
			for (auto &i : ROSE_RANGE(stp_dimensional_location, piece->getRootObject()->design()))
			{
				auto tolly = Location_dimension_IF::find(&i);
				if (tolly)
				{
					if (tolly->get_target() == cally.getRoot() || tolly->get_origin() == cally.getRoot())
					{
						ListOfRoseObject amp3;
						tolly->getAIMObjects(&amp3);
						for (auto j = 0u, sz = amp3.size(); j < sz; j++)
						{
							exports.add(amp3[j]);
						}
					}
				}
			}
		}
	}
	auto des = piece->getRootObject()->design();
	for (auto i : ARM_RANGE(Datum_defined_by_derived_shape, des)) 
	{
		if (i.get_its_workpiece() == piece->getRoot())
		{
			ListOfRoseObject amp2;
			i.getAIMObjects(&amp2);
			for (auto j = 0u, sz = amp2.size(); j < sz; j++)
				exports.add(amp2[j]);
			for (auto dat : ARM_RANGE(Datum_reference, des))
			{
				if (dat.get_referenced_datum() == i.getRootObject())
				{
					amp2.emptyYourself();
					dat.getAIMObjects(&amp2);
					for (auto j = 0u, sz = amp2.size(); j < sz; j++)
						exports.add(amp2[j]);
				}
			}
		}
	} 
	for (auto i : ARM_RANGE(Datum_defined_by_feature, des)) 
	{
		if (i.get_its_workpiece() == piece->getRoot())
		{
			ListOfRoseObject amp2;
			i.getAIMObjects(&amp2);
			for (auto j = 0u, sz = amp2.size(); j < sz; j++)
				exports.add(amp2[j]);
			for (auto dat : ARM_RANGE(Datum_reference, des))
			{
				if (dat.get_referenced_datum() == i.getRootObject())
				{
					amp2.emptyYourself();
					dat.getAIMObjects(&amp2);
					for (auto j = 0u, sz = amp2.size(); j < sz; j++)
						exports.add(amp2[j]);
				}
			}
		}
	} 
	for (auto i : ARM_RANGE(Datum_defined_by_targets, des))
	{
		if (i.get_its_workpiece() == piece->getRoot())
		{
			ListOfRoseObject amp2;
			i.getAIMObjects(&amp2);
			for (auto j = 0u, sz = amp2.size(); j < sz; j++)
				exports.add(amp2[j]);
			for (auto dat : ARM_RANGE(Datum_reference, des))
			{
				if (dat.get_referenced_datum() == i.getRootObject())
				{
					amp2.emptyYourself();
					dat.getAIMObjects(&amp2);
					for (auto j = 0u, sz = amp2.size(); j < sz; j++)
						exports.add(amp2[j]);
				}
			}
		}
	}
//=====Following code is replaced by the above three loops.	
//	ARMCursor cur;
//	cur.traverse(piece->getRootObject()->design());
//	ARMObject * tmp2;
//	while (NULL != (tmp2 = cur.next())) {
//
//		Single_datum_IF *datty = tmp2->castToSingle_datum_IF();
//		if (datty) {
//			if (datty->get_its_workpiece() == piece->getRoot()) {
//				ListOfRoseObject amp2;
//				datty->getAIMObjects(&amp2);
//				for (i = 0; i < amp2.size(); i++)
//					exports.add(amp2[i]);
//				for (auto dat : ARM_RANGE(Datum_reference, piece->getRootObject()->design()))
//				{
//					if (dat.get_referenced_datum() == datty->getRootObject()) {
//						ListOfRoseObject amp3;
//						dat.getAIMObjects(&amp3);
//						for (i = 0; i < amp3.size(); i++)
//							exports.add(amp3[i]);
//
//					}
//				}
//			}
//		}
//	}
//=====This ends the replaced code.

	// assuming recursive descent OK and that other code will remove duplicates when subassembly repeated
	unsigned count = piece->its_components.size();
	for (auto j = 0u; j < count; j++) {

		//	trace.error ("Assembly %d of %d", i, count);

		Workpiece_assembly_component *ass = Workpiece_assembly_component::find(piece->its_components[j]->getValue());
		if (ass == NULL)
			continue;

		ass->getAIMObjects(&tmp);
		for (auto i = 0u,sz=tmp.size(); i < sz; i++)
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
		for (auto i = 0u,sz=rep->items()->size(); i < sz; i++) {
			if (rep->items()->get(i) == repi) {
				return true;
			}
		}
	}
	for (auto i = 0u, sz = piece->size_its_related_geometry(); i < sz; i++) {
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


RoseReference* addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir){ //obj from output file, and master file for putting refs into
	std::string anchor((const char*)obj->domain()->name());	//anchor now looks like "advanced_face" or "manifold_solid_brep"
	//	anchor.append("_split_item");				//"advanced_face_split_item"
	//	if (obj->entity_id() == 0){ std::cout << anchor << " " << obj->domain()->typeIsSelect() << obj->entity_id() << std::endl; }
	//	anchor.append(std::to_string(obj->entity_id()));	//ex. "advanced_face_split_item123"

	ProdOut->addName(anchor.c_str(), obj);	//This makes the anchor.

	std::string reference(dir);
	if (reference.size() > 0)
		reference += "/";
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

	URIManager->should_go_to_uri(ref);
	return ref;
}

std::string SafeName(const std::string name)
{
	auto name2 = name;
	for (auto &c : name2)
	{
		if (isspace(c) || c < 32 || c == '<' || c == '>' || c == ':' || c == '\"' || c == '\\' || c == '/' || c == '|' || c == '?' || c == '*')
			c = '_';
	}
	return name2;
}

