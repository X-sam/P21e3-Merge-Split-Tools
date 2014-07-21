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
#include "DesignAndName.h"
#pragma comment(lib,"stpcad_stix.lib")
#pragma comment(lib,"stpcad.lib")
#pragma comment(lib,"stpcad_arm.lib")
#pragma comment(lib,"stmodule.lib")

StplibSchemaType schemas;	//Used so that all split output has the same schema as the input file.

RoseReference* addRefAndAnchor(RoseObject * obj, RoseDesign * ProdOut, RoseDesign * master, std::string dir = "");
std::string SafeName(std::string name);

typedef struct vertex
{
	Workpiece * node;
	bool visited = false;
	std::string name="";
	std::string dir = "";
} vertex;

// Routines written by MH & adapted by Samson
int mBomSplit(Workpiece *root, bool repeat, std::string path, std::string root_dir, unsigned depth = 0);
Workpiece * find_root_workpiece(RoseDesign * des);
DesignAndName export_workpiece(Workpiece * piece, std::string file_path, std::string stp_file_name, bool is_master);
bool find_workpiece_contents(ListOfRoseObject &exports, Workpiece * piece, bool is_master);
//bool find_approval_contents(ListOfRoseObject &exports, Approval * approval);
//bool find_security_classification_contents(ListOfRoseObject &exports, Security_classification_assignment * sa);
bool find_style_contents(ListOfRoseObject &exports, Workpiece *piece, bool is_master);
bool style_applies_to_workpiece(Single_styled_item * ssi, Workpiece * piece, bool is_master);
DesignAndName split_pmi(Workpiece * piece, std::string stp_file_name, unsigned depth, std::string root_dir);

int FixRelations(RoseDesign * des);
int MarkForSplit(vertex &root);
int MarkAsmMaster(vertex &master);
int MarkLeaf(vertex &leaf);
std::vector<std::string> NameChildren(Workpiece * Parent, std::vector<Workpiece *> &Children);
void NameChildren(Workpiece *Parent, std::vector<vertex> &children);
int Split(RoseDesign *des);
int main(int argc, char* argv[])
{
	if (argc < 2){
		std::cout << "Usage: .\\STEPSplit.exe filetosplit.stp\n" << "\tCreates new file SplitOutput.stp as master step file with seperate files for each product" << std::endl;
		return EXIT_FAILURE;
	}
	ROSE.quiet(1);	//Suppress startup info.
	stplib_init();	// initialize merged cad library
	//	FILE *out;
	//	out = fopen("log.txt", "w");
	//	ROSE.error_reporter()->error_file(out);
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

	FixRelations(original);

	ARMpopulate(original);
	schemas = stplib_get_schema(original);	//Load the schemas from original. They have to go in all of the child files.
	Workpiece *root = find_root_workpiece(original);
	if (root == NULL)
	{
		std::cerr << "No Workpiece found in input" << std::endl;
		return EXIT_FAILURE;
	}

	vertex v;
	v.node = root;
	v.name = v.node->get_its_id();
	v.dir = v.name;

	MarkForSplit(v);
	Split(original);
	//return mBomSplit(root, true, outfile_directory, outfile_directory);

}

// Do we need to swap the assembly relationships before beginning the split. This is terrifically slow.
//Swap the Relations to match the way that ARM/AP238 expects them to be.
int FixRelations(RoseDesign * des) {
	stix_tag_units(des);
	stix_tag_asms(des);
	int count = 0;
	for (auto &possible_swap : ROSE_RANGE(stp_representation_relationship, des))
	{
		StixMgrAsmRelation * mgr = StixMgrAsmRelation::find(&possible_swap);
		if (!mgr)
		{
			//std::cout << "Representation relationship " << possible_swap.entity_id() << " is not part of an assembly\n";
			continue;
		}
		if (!mgr->owner) mgr->reversed = !(mgr->reversed);	//LIES
		if (!mgr->reversed) continue;
		//std::cout << "REVERSED\n\tName: " <<possible_swap.name() <<"\n\tOwner: " << (mgr->owner ? mgr->owner->domain()->className() : 0) << "\n\tChild: " << (mgr->child ? mgr->child->domain()->name() : 0) <<'\n';
		//possible_swap.display();
		stp_representation *tmp = possible_swap.rep_2();
		possible_swap.rep_2(possible_swap.rep_1());
		possible_swap.rep_1(tmp);
		if (possible_swap.isa(ROSE_DOMAIN(stp_representation_relationship_with_transformation)))
		{
			auto rrwt = ROSE_CAST(stp_representation_relationship_with_transformation, &possible_swap);
			auto idt = ROSE_CAST(stp_item_defined_transformation, rose_get_nested_object(rrwt->transformation_operator()));
			if (nullptr != idt)
			{
				auto tmp = idt->transform_item_1();
				idt->transform_item_1(idt->transform_item_2());
				idt->transform_item_2(tmp);
			}
		}
		count++;
	}
	return count;
}


//Every workpiece needs its own file. Here we attach a manager to every rose object saying which file that is.

int MarkForSplit(vertex &root)
{
	std::vector<vertex> stack;
	stack.push_back(root);
	vertex v;
	while (!stack.empty())
	{
		v = stack.back();
		stack.pop_back();
		if (false == v.visited)
		{
			v.visited = true;
			std::vector<vertex> children;
			for (auto i = 0u, sz = v.node->size_its_components(); i < sz; i++)
			{
				Workpiece_assembly_component * wac = Workpiece_assembly_component::find(v.node->get_its_components(i)->getValue());
				Workpiece *w = Workpiece::find(wac->get_component());
				vertex x;
				x.node = w;
				children.push_back(x);
			}
			NameChildren(v.node,children);
			for (auto x : children)
			{
				x.dir = v.dir;
				x.dir += '\\'+x.name;
				stack.push_back(x);
			}
			if (0 == children.size())
				MarkLeaf(v);
			else
				MarkAsmMaster(v);
		}
	}
	return EXIT_SUCCESS;
}
int MarkLeaf(vertex &leaf)
{
	std::cout << leaf.dir <<"\\" << leaf.name <<".stp I am leaf.\n";
	return 0;
}
int MarkAsmMaster(vertex &master)
{
	std::cout << master.dir << "\\" << master.name <<".stp I am Asm Master.\n";
	return 0;
}
//Given a workpiece and a list of its children, sets the vertex names, ensuring they are unique.
void NameChildren(Workpiece *Parent, std::vector<vertex> &children)
{
	for (unsigned i = 0u, sz = children.size(); i < sz; i++)
	{
		bool need_nauo = false;
		for (auto j = 0u; j < sz; j++)
		{
			if (i == j) continue;
			if (children[i].node == children[j].node)
			{
				need_nauo = true;
				break;
			}
		}
		std::string outfilename;
		if (need_nauo) {
			stp_next_assembly_usage_occurrence *nauo = Parent->get_its_components(i)->getValue();
			std::string fname(nauo->id());
			fname = SafeName(fname);
			outfilename += fname + '-';
			fname = SafeName(nauo->name());
			outfilename += fname;
		}
		else
		{
			std::string fname(children[i].node->get_its_id());
			fname = SafeName(fname);
			outfilename += fname;
		}
		children[i].name = outfilename;
	}
}


int mBomSplit(Workpiece *root, bool repeat, std::string path, std::string root_dir, unsigned depth)
{
	//if (path.empty()) path =".\\";
	if(!path.empty())
		if (path.back() != '/' && path.back() != '\\') path.push_back('\\');
	
	//	std::cout << "\n\nMaking directory " << path << "\n";
	rose_mkdir(path.c_str());
	//Make a locally-scoped design to put all our trash in. we will move it to the trash when we are done.
	RoseDesign *garbage = new RoseDesign;

	// make directory for all the geometry components
	if (depth == 0) 
	{
		std::string components = root_dir + "/geometry_components";
		rose_mkdir(components.c_str());
	}

	//Get all of the child workpieces into a vector.
	unsigned sub_count = root->size_its_components();
	//	std::cout << "Assembly " << root->get_its_id() << " has " << sub_count << " components" << std::endl;
	std::vector<Workpiece*> children, exported_children;	//Keep track of the children we move
	std::vector <DesignAndName> subs;						//Keep track of the designs we move children to
	std::vector <std::string> exported_name;				//Keep track of the names of children.
	for (unsigned i = 0; i < sub_count; i++) 
	{
		Workpiece_assembly_component * comp = Workpiece_assembly_component::find(root->get_its_components(i)->getValue());
		if (comp == NULL) continue;
		Workpiece * child = Workpiece::find(comp->get_component());
		if (child == NULL) continue;
		children.push_back(child);
	}
	//For each child, find out if it needs an NAUO attached. Attach one if necessary.
	for (unsigned i = 0u, sz = children.size(); i < sz; i++) 
	{
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
//		if (path.size() > 0)
//			outfilename = path + "/";
//		else
//			outfilename = path;

		if (need_nuao) {
			stp_next_assembly_usage_occurrence *nauo = root->get_its_components(i)->getValue();
			std::string fname(nauo->id());
			fname = SafeName(fname);
			outfilename += fname + '-';
			fname = SafeName(nauo->name());
			outfilename += fname;
		}
		else
		{
			std::string fname(child->get_its_id());
			fname = SafeName(fname);
			outfilename += fname;
		}
		exported_name.push_back(path+outfilename);
		
		DesignAndName sub_des = export_workpiece(child, path, outfilename, false);
		std::cout << "Writing child to file: " <<path << outfilename << " (" << i + 1 << "/" << children.size() << ")\n";

		//ARMsave (sub_des);
		Workpiece *exported_child = find_root_workpiece(sub_des.GetDesign());
		subs.push_back(sub_des);
		exported_children.push_back(exported_child);

	}

	// Now top level design
	DesignAndName master = export_workpiece(root, path, "master.stp", true);
	Workpiece *master_root = find_root_workpiece(master.GetDesign());
	master.GetDesign()->addName("product_definition", master_root->getRootObject());
	master.GetDesign()->addName("shape_representation", master_root->get_its_geometry());
	master.GetDesign()->addName("axis_placement", master_root->get_its_geometry()->items()->get(0));

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
		rose_move_to_design(p, garbage);
		rose_move_to_design(pdf, garbage);
		rose_move_to_design(pd, garbage);

		comp->put_component(exported_child->getRoot());
		subs[i].GetDesign()->addName("product_definition", exported_child->getRoot());
		std::string uri(subs[i].GetName());
		uri += "#product_definition";
		RoseReference *Ref = rose_make_ref(master.GetDesign(), uri.c_str());
		Ref->resolved(exported_child->getRoot());
		MyURIManager *URIManager;	//Make an instance of the class which handles updating URIS
		URIManager = MyURIManager::make(exported_child->getRoot());
		URIManager->should_go_to_uri(Ref);

		//		addRefAndAnchor(exported_child->getRoot(), subs[i], master, dirname);		//Should be "product_definition"

		ListOfRoseObject tmp;

		stp_representation_relationship *master_rep =
			ROSE_CAST(stp_representation_relationship, comp->getpath_resulting_orientation(&tmp)->get(3));
		if (master_rep == nullptr)
		{
			std::cerr << "Error getting Representation Relationship.\n";
			return EXIT_FAILURE;
		}


		stp_representation *rep = master_rep->rep_1();
		master_rep->rep_1(exported_child->get_its_geometry());
		auto a2p = exported_child->get_its_geometry()->items()->get(0);
		if (master_rep->isa(ROSE_DOMAIN(stp_representation_relationship_with_transformation)))
		{
			auto rrwt = ROSE_CAST(stp_representation_relationship_with_transformation, master_rep);
			auto idt = ROSE_CAST(stp_item_defined_transformation, rose_get_nested_object(rrwt->transformation_operator()));
			if (nullptr != idt)
			{
				subs[i].GetDesign()->addName("axis_placement", a2p);
				idt->transform_item_1(a2p);
				uri = subs[i].GetName();
				uri += "#axis_placement";
				Ref = rose_make_ref(master.GetDesign(), uri.c_str());
				Ref->resolved(a2p);
				URIManager = MyURIManager::make(a2p);
				URIManager->should_go_to_uri(Ref);
			}
		}
		subs[i].GetDesign()->addName("shape_representation", exported_child->get_its_geometry());
		uri = subs[i].GetName();
		uri += "#shape_representation";
		Ref = rose_make_ref(master.GetDesign(), uri.c_str());
		Ref->resolved(exported_child->get_its_geometry());
		URIManager = MyURIManager::make(exported_child->get_its_geometry());
		URIManager->should_go_to_uri(Ref);

		//		addRefAndAnchor(ROSE_CAST(stp_shape_representation,exported_child->get_its_geometry()), subs[i], master, dirname);	//Should be "shape_representation"
		rep->move(garbage, INT_MAX);

		//The following code removes any geometry from the garbage.
		auto master_geometry = master_root->get_its_geometry();
		ListOfRoseObject children;
		master_geometry->findObjects(&children, INT_MAX, true);
		for (auto i = 0u, sz = children.size(); i < sz; i++)
			children[i]->move(master.GetDesign());	//Move contents of list to master.
	}
	update_uri_forwarding(master.GetDesign());

	stplib_put_schema(master.GetDesign(), schemas);
	master.Save();
	if (repeat)
	{
		for (unsigned i = 0u, sz = exported_children.size(); i < sz; i++)
		{
			Workpiece * exported_child = exported_children[i];
			if (exported_child->size_its_components() > 0)
			{
				std::string sub_path(exported_name[i]);
				mBomSplit(exported_child, repeat, sub_path, root_dir, depth + 1);
			}
			else
			{
				DesignAndName result = split_pmi(exported_child, exported_name[i], depth, root_dir);
				//if (result.GetDesign() != nullptr) result.GetDesign()->move(garbage, -1);	//Don't need the geometry in memory.
				//		ARMsave (subs[i]);
			}
		}
	}
	rose_move_to_trash(garbage);
	if (depth < 2) rose_empty_trash();	//Don't empty the trash too often, it's slow.
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
	for (auto &candidate : ARM_RANGE(Workpiece, des))
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

DesignAndName export_workpiece(Workpiece * piece, std::string file_path, std::string stp_file_name, bool is_master)
{
	if (piece == NULL)
		return DesignAndName();

	ListOfRoseObject exports;
	find_workpiece_contents(exports, piece, is_master);
	find_style_contents(exports, piece, is_master);

	/* After getting all the properties: do the following: */
	ARMresolveReferences(&exports);
	DesignAndName new_des(file_path, stp_file_name);
	ListOfRoseObject * new_list;
	new_list = ROSE_CAST(ListOfRoseObject, exports.copy(new_des.GetDesign(), INT_MAX, false));

	//    printf ("Number of copied objects in file %s is %d\n", stp_file_name, new_list->size());
	delete new_list;  /* Don't want the list itself in the new design */

	stix_tag_units(new_des.GetDesign());
	ARMpopulate(new_des.GetDesign());

	if (is_master) return new_des;


	auto new_model = Styled_geometric_model::newInstance(new_des.GetDesign());

	int style_count = 0;
	for (auto i : ARM_RANGE(Single_styled_item, new_des.GetDesign()))
	{
		new_model->add_its_styled_items(i.getRoot());
		style_count++;
	}

	//    if (style_count != 0)
	//	printf ("Added %d styles to workpiece %s\n", style_count, piece->get_its_id());

	//    new_des->save ();

	return new_des;
}


DesignAndName move_geometry(Workpiece * piece, std::string root_dir)
{
	// directory that contains all the geometry	
	std::string geometrydir(root_dir);
	std::string pieceid(piece->get_its_id());
	geometrydir += "/geometry_components/";
	std::string a(piece->get_its_id());
	a+= ".stp";
	if (rose_file_exists((geometrydir+pieceid+".stp").c_str()))	// Root_dir/geometry_components/pieceid.stp if you're wondering.
	{
		DesignAndName k;
		k.Find(geometrydir, pieceid);
		if (k.GetDesign() == nullptr)
			k.Open(geometrydir, pieceid);
	//		auto a = ROSE.findDesignInWorkspace(geometry.c_str());
	//		if (nullptr == a) a = ROSE.findDesign(geometry.c_str());
		return k;
	//		return nullptr;
	}
	ListOfRoseObject geo_exports;
	find_workpiece_contents(geo_exports, piece, false);

	/* After getting all the properties: do the following: */
	ARMresolveReferences(&geo_exports);

	// for making local copy in current directory
	//    std::string geo_file (stp_file_name);
	//    geo_file = geo_file + "/geometry.stp";

	//std::cout <<"Writing geometry to " <<geometry <<'\n';
	DesignAndName geo_des(geometrydir, pieceid);
	ListOfRoseObject * geo_list;
	geo_list = ROSE_CAST(ListOfRoseObject, geo_exports.copy(geo_des.GetDesign(), INT_MAX));

	delete geo_list;  /* Don't want the list itself in the new design */

	stix_tag_units(geo_des.GetDesign());
	ARMpopulate(geo_des.GetDesign());
	Workpiece * component_piece = find_root_workpiece(geo_des.GetDesign());

	//TODO: Make a list of things that reference geometry and anchor them here.
	
	geo_des.GetDesign()->addName("product_definition", component_piece->getRoot());
	geo_des.GetDesign()->addName("shape_representation", component_piece->get_its_geometry());
	geo_des.GetDesign()->addName("axis_placement", component_piece->get_its_geometry()->items()->get(0));
	RoseCursor objs;
	objs.traverse(geo_des.GetDesign());
	objs.domain(ROSE_DOMAIN(stp_manifold_solid_brep));
	RoseObject *mani = objs.next();
	if (mani != NULL)
		geo_des.GetDesign()->addName("manifold_solid_brep", mani);
	
	stplib_put_schema(geo_des.GetDesign(), schemas);
	
	geo_des.Save();
	return geo_des;
}

DesignAndName split_pmi(Workpiece * piece, std::string stp_file_name, unsigned depth, std::string root_dir)
{
	if (piece == NULL)
		return DesignAndName();

	rose_mkdir(stp_file_name.c_str());

	std::string pieceid(piece->get_its_id());

	ListOfRoseObject style_exports;
	find_style_contents(style_exports, piece, false);
	ARMresolveReferences(&style_exports);

	std::string pmi_file(stp_file_name);
	pmi_file += "/pmi.stp";

	DesignAndName style_des;
	style_des.NewDesign(stp_file_name, "pmi.stp");
	ListOfRoseObject * style_list;
	style_list = ROSE_CAST(ListOfRoseObject, style_exports.copy(style_des.GetDesign(), INT_MAX));

	//std::cout << "Number of copied objects in style file " << stp_file_name << "_pmi.stp is " << style_list->size() << '\n';
	delete style_list;  /* Don't want the list itself in the new design */

	stix_tag_units(style_des.GetDesign());
	ARMpopulate(style_des.GetDesign());

	Styled_geometric_model * new_model = NULL;
	int style_count = 0;
	for (auto &ssi : ARM_RANGE(Single_styled_item, style_des.GetDesign()))
	{
		if (new_model == NULL) {
			new_model = Styled_geometric_model::newInstance(style_des.GetDesign());
			style_des.GetDesign()->addName("styles", new_model->getRoot());
		}
		new_model->add_its_styled_items(ssi.getRoot());
		style_count++;
		if (ssi.get_its_geometry()) {
			stp_representation_item *repi = ssi.get_its_geometry();
			std::string master_name("master.stp#");
			master_name += repi->domain()->name();
			RoseReference *entity_reference = rose_make_ref(style_des.GetDesign(), master_name.c_str());
			stp_styled_item *style = ssi.getRoot();
			rose_put_ref(entity_reference, style, "item");
			repi->move(rose_trash(), -1);
		}

	}

	update_uri_forwarding(style_des.GetDesign());

	stplib_put_schema(style_des.GetDesign(), schemas);
	style_des.Save();

	DesignAndName geo_des = move_geometry(piece, root_dir);

	std::string master_file(stp_file_name);
	master_file += "/master.stp";
	DesignAndName master_des(stp_file_name,"master.stp");
//	RoseDesign *master_des = ROSE.newDesign(master_file.c_str());

	int count = 10;
	if (new_model) {

		RoseReference *styles = rose_make_ref(master_des.GetDesign(), "pmi.stp#styles");
		master_des.GetDesign()->addName("styles", styles);
		styles->entity_id(count);
		count = count + 10;
	}

	// directory that contains all the geometry
	std::string prefix("../");
	for (unsigned i = 0; i < depth; i++)
		prefix.append("../");
	prefix += "geometry_components/";
	prefix += pieceid;
	prefix += ".stp";

	RoseCursor objs;
	objs.traverse(geo_des.GetDesign());
	objs.domain(ROSE_DOMAIN(stp_manifold_solid_brep));
	RoseObject *mani = objs.next();
	if (nullptr != mani)
	{
		std::string man_URI(prefix);
		man_URI += "#manifold_solid_brep";
		RoseReference *manifold = rose_make_ref(master_des.GetDesign(), man_URI.c_str());
		master_des.GetDesign()->addName("manifold_solid_brep", manifold);
		manifold->entity_id(count);
		count = count + 10;
	}
	std::string shape_URI(prefix);
	shape_URI += "#shape_representation";
	RoseReference *shape_rep = rose_make_ref(master_des.GetDesign(), shape_URI.c_str());
	master_des.GetDesign()->addName("shape_representation", shape_rep);
	shape_rep->entity_id(count);
	count = count + 10;

	std::string definition_URI(prefix);
	definition_URI += "#product_definition";
	RoseReference *definition = rose_make_ref(master_des.GetDesign(), definition_URI.c_str());
	master_des.GetDesign()->addName("product_definition", definition);
	definition->entity_id(count);
	count = count + 10;

	std::string axis_URI(prefix);
	axis_URI += "#axis_placement";
	RoseReference *axis = rose_make_ref(master_des.GetDesign(), axis_URI.c_str());
	master_des.GetDesign()->addName("axis_placement", axis);
	axis->entity_id(count);
	count = count + 10;


	stplib_put_schema(master_des.GetDesign(), schemas);
	master_des.Save();

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
		for (auto i = 0u, sz = tmp.size(); i < sz; i++)
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
	piece->unset_its_categories();

	piece->getAIMObjects(&tmp);
	for (auto i = 0u, sz = tmp.size(); i < sz; i++)
	{
		if (!(tmp[i]->isa(ROSE_DOMAIN(stp_product_related_product_category))))
			exports.add(tmp[i]);
	}
	for (auto &cally : ARM_RANGE(Callout, piece->design()))
	{
		if (cally.get_its_workpiece() == piece->getRoot()) {
			ListOfRoseObject amp2;
			cally.getAIMObjects(&amp2);
			for (auto i = 0u, sz = amp2.size(); i < sz; i++)
			{
				exports.add(amp2[i]);
			}
			for (auto &i : ROSE_RANGE(stp_geometric_tolerance, piece->design()))
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
			for (auto &i : ROSE_RANGE(stp_dimensional_size, piece->design()))
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
			for (auto &i : ROSE_RANGE(stp_dimensional_location, piece->design()))
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
	auto des = piece->design();
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

	// assuming recursive descent OK and that other code will remove duplicates when subassembly repeated
	for (auto j = 0u, sz = piece->its_components.size(); j < sz; j++) {

		//	trace.error ("Assembly %d of %d", i, count);

		Workpiece_assembly_component *ass = Workpiece_assembly_component::find(piece->its_components[j]->getValue());
		if (ass == NULL)
			continue;

		ass->getAIMObjects(&tmp);
		for (auto i = 0u, sz = tmp.size(); i < sz; i++)
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

	for (auto &ssi : ARM_RANGE(Single_styled_item, piece->design()))
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
		for (auto i = 0u, sz = rep->items()->size(); i < sz; i++) {
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

	for (auto j = 0u,sz=piece->its_components.size(); j < sz; j++) {


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

