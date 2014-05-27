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

//####################### markers/taggers ##########################

void tag_listleaf_for_export(
	RoseDesign * d,
	RoseDomain * dom,
	const char * attname
	)
{
	RoseCursor objs;
	RoseObject * obj;

	objs.traverse(d);
	objs.domain(dom);
	while ((obj = objs.next()) != 0) {
		if (stix_split_has_export(obj, attname)) {
			stix_split_trim_for_export(obj, attname);
			stix_split_mark_needed(obj, (unsigned)-1);
		}
		else {
			stix_split_mark_ignorable(obj, (unsigned)-1);
		}
	}
}

void tag_leaf_for_export(
	RoseDesign * d,
	RoseDomain * dom,
	const char * attname
	)
{
	RoseCursor objs;
	RoseObject * obj;

	objs.traverse(d);
	objs.domain(dom);
	while ((obj = objs.next()) != 0) {
		if (stix_split_has_export(obj, attname)) {
			stix_split_mark_needed(obj, (unsigned)-1);
		}
		else {
			stix_split_mark_ignorable(obj, (unsigned)-1);
		}
	}
}


void tag_step_properties(
	RoseDesign * d
	)
{
	RoseCursor objs;
	RoseObject * obj;

	// Tag any properties that are attached to something in the
	// destination design.

	// Find the property definition representations that reference
	// something in the part file and move that recursively.  This
	// should bring in the entire representation and its context.

	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_property_definition));
	while ((obj = objs.next()) != 0) {
		stp_property_definition * pd = ROSE_CAST(stp_property_definition, obj);

		// Only look at properties on things in the destination
		if (!stix_split_is_export(pd->definition())) continue;
		stix_split_mark_needed(pd, (unsigned)-1);

		// mark any representations
		StixMgrPropertyRep * pdrmgr = StixMgrPropertyRep::find(pd);
		if (!pdrmgr) continue;

		unsigned i, sz;
		for (i = 0, sz = pdrmgr->size(); i<sz; i++) {
			stix_split_mark_needed(pdrmgr->get(i), (unsigned)-1);
		}
	}
}


void tag_properties(
	RoseDesign * d
	)
{
	RoseCursor objs;
	RoseObject * obj;


	// REALLY, WE WANT TO KEEP THIS IF IT IS RELATED
	// Strip out any suppliemental geometry   
	//
	stix_split_all_ignorable(d, ROSE_DOMAIN(stp_constructive_geometry_representation_relationship));
	stix_split_all_deep_ignorable(d, ROSE_DOMAIN(stp_constructive_geometry_representation));


	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_property_definition_representation));
	while ((obj = objs.next()) != 0) {
		stp_property_definition_representation * pdr =
			ROSE_CAST(stp_property_definition_representation, obj);
		stp_representation * rep = pdr->used_representation();
		RoseObject * def = rose_get_nested_object(pdr->definition());
		int strip_property = 0;

		if (!stix_split_is_export(def))
			strip_property = 1;

		else if (def->isa(ROSE_DOMAIN(stp_property_definition)))
		{
			// Strip all geometric validation props
			stp_property_definition * prop = ROSE_CAST(stp_property_definition, def);
			const char * propnm = prop ? prop->name() : 0;

			if (propnm && !strcmp(propnm, "geometric validation property"))
				strip_property = 1;

			RoseObject * def2 = rose_get_nested_object(prop->definition());
			if (!stix_split_is_export(def2))
				strip_property = 1;
		}

		// Ignore properties that reference geometry that is stripped.
		// Ignore placements, points and directions because they may
		// have been shared.
		//
		if (!strip_property)
		{
			unsigned i, sz;
			for (i = 0, sz = rep->items()->size(); i<sz; i++)
			{
				stp_representation_item * it = rep->items()->get(i);
				if (stix_split_is_needed(it)) continue;
				if (it->isa(ROSE_DOMAIN(stp_placement))) continue;
				if (it->isa(ROSE_DOMAIN(stp_point))) continue;
				if (it->isa(ROSE_DOMAIN(stp_direction))) continue;

				if (it->isa(ROSE_DOMAIN(stp_topological_representation_item)) ||
					it->isa(ROSE_DOMAIN(stp_geometric_representation_item)))
				{
					strip_property = 1; break;
				}
			}
		}

		if (strip_property) {
			//printf ("removing property #%lu %s\n", def->entity_id(), def->domain()-> name());

			stix_split_mark_ignorable(pdr);
			stix_split_mark_ignorable(def);
			stix_split_mark_ignorable(rep, (unsigned)-1);
		}
	}

	// Force the context of representations that we will be writing to
	// be present.
	//
	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_representation));
	while ((obj = objs.next()) != 0) {
		stp_representation * rep = ROSE_CAST(stp_representation, obj);
		if (stix_split_is_export(rep))
			stix_split_mark_needed(rep->context_of_items(), (unsigned)-1);
	}
}



void tag_subassembly(
	stp_product_definition * pd
	)
{

	if (!pd) return;
	RoseDesign * src = pd->design();

	// Now loop over any children and tag them too
	unsigned i, sz;
	StixMgrAsmProduct * pd_mgr = StixMgrAsmProduct::find(pd);
	if (!pd_mgr) return;  // not a proper part

	// mark the pd, pdf, part, and contexts
	stix_split_mark_needed(pd, (unsigned)-1);

	// Get all props, including the main shape
	tag_step_properties(pd->design());

	//printf("TAGGING PART #%lu\n", pd-> entity_id());

	for (i = 0, sz = pd_mgr->child_nauos.size(); i<sz; i++)
	{
		// tag children.  Be sure to get shape property so that any
		// context dependent shape rep gets pulled in as well.
		stp_next_assembly_usage_occurrence * nauo = pd_mgr->child_nauos[i];
		stix_split_mark_needed(nauo, (unsigned)-1);
		stix_split_mark_needed(stix_get_shape_property(nauo), (unsigned)-1);

		// Mark product definition and relationship
		tag_subassembly(stix_get_related_pdef(nauo));
	}

	for (i = 0, sz = pd_mgr->shapes.size(); i<sz; i++) {
		// Mark all direct shapes and any related shapes.  We will get
		// the property things in between later.

		StixMgrAsmShapeRep * rep_mgr = StixMgrAsmShapeRep::find(pd_mgr->shapes[i]);
		if (!rep_mgr) continue;

		unsigned j, szz;
		for (j = 0, szz = rep_mgr->child_rels.size(); j<szz; j++) {
			stix_split_mark_needed(rep_mgr->child_rels[j], (unsigned)-1);
		}
	}


	// Look for things connecting the shape and bom relations
	RoseCursor objs;
	RoseObject * obj;

	objs.traverse(src);
	objs.domain(ROSE_DOMAIN(stp_context_dependent_shape_representation));
	while ((obj = objs.next()) != 0) {
		stp_context_dependent_shape_representation * cdsr =
			ROSE_CAST(stp_context_dependent_shape_representation, obj);

		if (stix_split_is_export(cdsr->representation_relation()) &&
			stix_split_is_export(cdsr->represented_product_relation())) {
			stix_split_mark_needed(cdsr);
		}
	}
}



void tag_and_strip_exported_from_tree(
	stp_product_definition * pd
	)
{
	RoseDesign * src = pd->design();

	unsigned i, sz;
	StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);
	StixMgrSplitProduct * split_mgr = StixMgrSplitProduct::find(pd);

	if (!pm) return;

	char * pname = pd->formation()->of_product()->name();
	printf("PD #%lu %s\n", pd->entity_id(), pname ? pname : "");

	// MARK THIS PART FOR EXPORT.  Follow up to the product root and
	// get all of the contexts, then we need to look at the shape
	// properties and whatnot.

	stix_split_mark_needed(pd, (unsigned)-1);
	if (!split_mgr)
	{
		printf(" => PD not exported, examining children\n");

		// Not exported, examine any children.
		for (i = 0, sz = pm->child_nauos.size(); i<sz; i++)
		{
			stix_split_mark_needed(pm->child_nauos[i]);

			tag_and_strip_exported_from_tree(
				stix_get_related_pdef(pm->child_nauos[i])
				);
		}

		printf(" => PD #%lu marking shapes\n", pd->entity_id());
		stix_split_mark_needed(stix_get_shape_property(pd));

		// make sure that we get the shapes for this too
		for (i = 0, sz = pm->shapes.size(); i<sz; i++)
		{
			stix_split_mark_needed(pm->shapes[i], (unsigned)-1);
			// /* The parent and child rep_relationships */
			// StpAsmShapeRepRelVec child_rels;
			// StpAsmShapeRepRelVec parent_rels;
		}

		printf(" => PD #%lu done marking shapes\n", pd->entity_id());
		return;
	}


	if (split_mgr->part_stripped) {
		printf(" => PD already cleared\n");
		return;
	}

	split_mgr->part_stripped = 1;
	printf(" => PD was exported, trimming shape\n");

	stp_product_definition_shape * pds = stix_get_shape_property(pd);
	if (!pds) {
		printf("PD #%lu - no shape property!\n", pd->entity_id());
		return;
	}

	stix_split_mark_needed(pds);

	// This part has an external definition, remove any geometry and
	// attach an external reference.  Find the first shape that is a
	// plain shape reference (no subtype).  This is what we would like
	// to attach the external ref to.
	//
	stp_representation * old_rep = 0;
	for (i = 0, sz = pm->shapes.size(); i<sz; i++)
	{
		if (pm->shapes[i]->domain() == ROSE_DOMAIN(stp_shape_representation)) {
			old_rep = pm->shapes[i];
			break;
		}
	}
	if (!old_rep) {
		old_rep = pm->shapes[0];

		if (!old_rep) {
			printf("PD #%lu: could not find shape to relate\n", pd->entity_id());
			return;
		}
		if (pm->shapes.size() > 1) {
			printf("PD #%lu: more than one main shape!\n", pd->entity_id());
			return;
		}
	}

	StixMgrPropertyRep * repmgr = StixMgrPropertyRep::find(pds);
	for (i = 0, sz = repmgr->size(); i<sz; i++)
	{
		stp_property_definition_representation * pdr = repmgr->get(i);
		if (pdr->used_representation() == old_rep &&
			pdr->isa(ROSE_DOMAIN(stp_shape_definition_representation)))
		{
			stix_split_mark_needed(pdr);
			break;
		}
	}

	// Allways create a new representation, because the old one could
	// be an adv_brep or other subtype, fill with any placements used
	// by the assembly and attach it to the product.
	//
	stp_representation * main_rep = pnewIn(src) stp_shape_representation;
	main_rep->name(old_rep->name());
	main_rep->context_of_items(old_rep->context_of_items());
	main_rep->entity_id(old_rep->entity_id());

	// Add placements, but ignore everything else.
	for (i = 0, sz = old_rep->items()->size(); i<sz; i++)
	{
		// FILTER EXTRA AXIS PLACEMENTS
		stp_representation_item * it = old_rep->items()->get(i);
		if (it->isa(ROSE_DOMAIN(stp_placement))) {
			main_rep->items()->add(it);
			stix_split_mark_needed(it, (unsigned)-1);
		}
	}

	// So darn many things may reference a rep, beyond just the
	// shape def relation, so use the substitute mechanism to bulk
	// replace.  Mark as a temp so that we can restore later.
	//
	stix_split_mark_needed(main_rep);
	rose_register_substitute(old_rep, main_rep);
	StixMgrSplitTmpObj::mark_as_temp(main_rep, old_rep);
	StixMgrSplitTmpObj::mark_as_temp(main_rep->items());

	// hook in the document reference
	//make_document_reference(pd->design(), pd, main_rep, split_mgr->part_filename);
}


void tag_and_strip_exported_products(
	RoseDesign * d
	)
{
	// Navigate the assembly and trim exported geometry
	unsigned i, sz;
	RoseCursor objs;
	RoseObject * obj;

	StpAsmProductDefVec roots;
	stix_find_root_products(&roots, d);
	for (i = 0, sz = roots.size(); i<sz; i++)
		tag_and_strip_exported_from_tree(roots[i]);


	// We had to do some substitutions for the shape reps.  Expand
	// everything out and clear the managers.

	rose_update_object_references(d);
	objs.traverse(d);
	objs.domain(0);
	while ((obj = objs.next()) != 0)
		obj->remove_manager(ROSE_MGR_SUBSTITUTE);
}



void tag_shape_annotation(
	RoseDesign * d
	)
{
	RoseCursor objs;
	RoseObject * obj;

	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_shape_aspect));
	while ((obj = objs.next()) != 0) {
		stp_shape_aspect * sa = ROSE_CAST(stp_shape_aspect, obj);
		stp_product_definition_shape * pds = sa->of_shape();

		if (!stix_split_is_export(pds)) {
			stix_split_mark_ignorable(sa);
			continue;
		}

		RoseObject * targ = rose_get_nested_object(pds->definition());
		if (!stix_split_is_export(targ)) {
			stix_split_mark_ignorable(sa);
			continue;
		}
		stix_split_mark_needed(sa);
	}

	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_shape_aspect_relationship));
	while ((obj = objs.next()) != 0) {
		stp_shape_aspect_relationship * sar =
			ROSE_CAST(stp_shape_aspect_relationship, obj);

		stp_shape_aspect * ed = sar->related_shape_aspect();
		stp_shape_aspect * ing = sar->relating_shape_aspect();

		if (!stix_split_is_export(ed) ||
			!stix_split_is_export(ing))
			stix_split_mark_ignorable(sar);
		else
			stix_split_mark_needed(sar);
	}


	// Mark any geometric item usages that connect items with shape
	// aspects.

	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_item_identified_representation_usage));
	while ((obj = objs.next()) != 0) {
		stp_item_identified_representation_usage * iiu =
			ROSE_CAST(stp_item_identified_representation_usage, obj);

		// By default, ignore any item refs that point to things
		// outside of our export set.  We may override some of this
		// later.
		//
		if (!stix_split_is_export(iiu->definition()) ||
			!stix_split_is_export(iiu->used_representation()) ||
			!stix_split_is_export(iiu->identified_item()))
			stix_split_mark_ignorable(iiu);
	}


	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_geometric_item_specific_usage));
	while ((obj = objs.next()) != 0) {
		stp_item_identified_representation_usage * iiu =
			ROSE_CAST(stp_item_identified_representation_usage, obj);

		// Force these if everything is there
		if (stix_split_is_export(iiu->used_representation()) &&
			stix_split_is_export(iiu->identified_item()) &&
			stix_split_is_export(iiu->definition()))
			stix_split_mark_needed(iiu, (unsigned)-1);
	}


	// MARK ALL OF THE ANNOTATION OCCURRENCES
	//
	// Force these if attached to a common shape aspect.  The
	// representation is the draughting model and might contain a
	// other annotations, so we need to avoid blanket marking it.
	//
	objs.traverse(d);
	objs.domain(ROSE_DOMAIN(stp_draughting_model_item_association));
	while ((obj = objs.next()) != 0) {
		stp_item_identified_representation_usage * iiu =
			ROSE_CAST(stp_item_identified_representation_usage, obj);

		if (stix_split_is_export(iiu->definition())) {
			stix_split_mark_needed(iiu);
			stix_split_mark_needed(iiu->identified_item(), (unsigned)-1);
			stix_split_mark_needed(iiu->used_representation());
		}
		else {
			stix_split_mark_ignorable(iiu);
			stix_split_mark_ignorable(iiu->identified_item(), (unsigned)-1);
			stix_split_mark_ignorable(iiu->used_representation());
		}
	}


	// MARK ANNOTATION PLANES 
	//
	// Force a plane if it points to something that is used.
	//
	tag_listleaf_for_export(d, ROSE_DOMAIN(stp_annotation_plane), "elements");
	tag_listleaf_for_export(d, ROSE_DOMAIN(stp_draughting_model), "items");


	// This connects dimensions to a representation
	tag_leaf_for_export(d, ROSE_DOMAIN(stp_dimensional_characteristic_representation), "dimension");

}

//##################################################################

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
	if (!p) return 1;	//No product so can't do things right?
//	std::string ProdOutName = std::string(p->name() + std::string("_split"));
	std::string ProdOutName(p->name());
	ProdOutName.append("_split");
	int spacepos = ProdOutName.find(' ');	//Finds first space in filename, if any.
	while (spacepos != std::string::npos)
	{
		ProdOutName[spacepos] = '_';	//Replaces space with underscore, for filesystem safety.
		spacepos = ProdOutName.find(' ', spacepos+1);
	}
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