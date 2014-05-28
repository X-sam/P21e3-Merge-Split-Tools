/* $RCSfile: splitasm.cxx,v $
 * $Revision: 1.32 $ $Date: 2014/01/06 20:54:59 $
 * Auth: David Loffredo (loffredo@steptools.com)
 * 
 * 	Copyright (c) 1991-2014 by STEP Tools Inc.
 * 	All Rights Reserved
 * 
 * 	This software is furnished under a license and may be used and
 * 	copied only in accordance with the terms of such license and with
 * 	the inclusion of the above copyright notice.  This software and
 * 	accompanying written materials or any other copies thereof may
 * 	not be provided or otherwise made available to any other person.
 * 	No title to or ownership of the software is hereby transferred.
 * 
 * 		----------------------------------------
 */


#include <stp_schema.h>
#include <ctype.h>
#include <stix_asm.h>
#include <stix_tmpobj.h>
#include <stix_property.h>
#include <stix_split.h>

#pragma comment(lib,"stpcad_stix.lib")

const char * tool_name 	= "STEP Splitter";
const char * tool_trace	= "splitasm";

const char * usage_msg 	= "Usage: %s  [options] <stpfile>\n";
const char * opts_short =
" -help for a list of available options.\n\n";

const char * opts_long = 
"\n"
" This tool reads a STEP file and breaks the assembly into a master\n"
" file and part files according to the options given below.   By default\n"
" it exports all part geometry to separate files and keeps all of the\n"
" assembly structure in the master.\n"
"\n"
" -help\t\t - Print this help message. \n"
" -all\t\t - Export all geometry to part files.\n"
" -nomast\t\t - Do not write a master file.\n"
" -o <dir>\t\t - Output to given directory.\n"
" -p <name>\t\t - Export named product.\n"
" -top\t\t - Export top level subassemblies.\n"
" -trees\t\t - Export files for all parts and subtrees.\n"
"\n" 
;

enum NameStyle {
    name_style_id,
    name_style_name,
    name_style_name_id
};

static NameStyle name_style = name_style_name_id;

static DictionaryOfRoseObject written_filenames;


static void usage (const char * name) 
{  
    fprintf (stderr, usage_msg, name);
    fputs (opts_short, stderr);
    exit (1);
}

static void long_usage (const char * name) 
{
    printf (usage_msg, name);
    puts (opts_long);
    exit (0);
}


static void copy_header (RoseDesign * dst, RoseDesign * src)
{
    unsigned i, sz;
    // Copy over the header information from the original
    dst-> initialize_header();
    dst-> header_name()-> originating_system(src-> header_name()-> originating_system());
    dst-> header_name()-> authorisation(src-> header_name()-> authorisation());
    for (i=0, sz=src->header_name()->author()->size(); i<sz; i++)
	dst-> header_name()-> author()-> add(
	    src-> header_name()-> author()-> get(i)
	    );

    for (i=0, sz=src->header_name()->author()->size(); i<sz; i++)
	dst-> header_name()-> organization()-> add(
	    src-> header_name()-> organization()-> get(i)
	    );

    RoseStringObject desc = "Extracted from STEP assembly: ";
    desc += src->name();
    desc += ".";
    desc += src->fileExtension();
    dst-> header_description()-> description()-> add(desc);
}


static void copy_schema (RoseDesign * dst, RoseDesign * src)
{
    // Make the new files the same schema unless the original AP does
    // not have the external reference definitions.
    //
    switch (stplib_get_schema (src)) {
    case stplib_schema_ap203e2: 
    case stplib_schema_ap214:
    case stplib_schema_ap242:
	stplib_put_schema (dst, stplib_get_schema (src));
	break;

    case stplib_schema_ap203: 
    default:
	stplib_put_schema (dst, stplib_schema_ap214);
	break;
    }
}



static int has_geometry (stp_representation * rep)
{
    unsigned i, sz;

    if (!rep) return 0;

    // Does this contain more than just axis placements?
    for (i=0, sz=rep->items()->size(); i<sz; i++) {
	stp_representation_item * it = rep->items()-> get(i);
	if (!it-> isa (ROSE_DOMAIN(stp_placement)))
	    return 1;
    }
    
    // Look at this any related shape reps that are not part of an
    // assembly structure.  These are held in the child_rels list but
    // have null nauo pointers.

    StixMgrAsmShapeRep * mgr = StixMgrAsmShapeRep::find(rep);
    if (!mgr) return 0;

    for (i=0, sz=mgr->child_rels.size(); i<sz; i++) {
	StixMgrAsmRelation * relmgr = 
	    StixMgrAsmRelation::find(mgr->child_rels[i]);
	
	if (relmgr && !relmgr-> owner && has_geometry(relmgr-> child))
	    return 1;
    }
    return 0;
}


RoseStringObject get_master_filename (
    RoseDesign * d
    )
{
    RoseStringObject name ("master_");
    name += d-> name();

    if (d-> fileExtension()) {
	name += ".";
	name += d-> fileExtension();
    }
    return name;
}


RoseStringObject get_part_filename (
    stp_product_definition * pd
    )
{
    RoseStringObject name ("part");
    stp_product_definition_formation * pdf = pd-> formation();
    stp_product * p = pdf? pdf-> of_product(): 0;

    if (name_style == name_style_name ||
	name_style == name_style_name_id)
    {
	char * pname = p? p-> name(): 0;
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
    }

    if (name_style == name_style_id ||
	name_style == name_style_name_id ) 
    {
	char idstr[100];
	sprintf (idstr, "id%lu", pd->entity_id());

	if (!name.is_empty()) name += "_";
	name += idstr;
    }

    // CHECK FOR DUPLICATES AND WARN
    RoseObject * obj = written_filenames.find(name);
    if (obj && obj != p) {
	printf ("WARNING: Products #%lu and #%lu will export to the same file name!\n",
		obj-> entity_id(), p-> entity_id());
    }
    else written_filenames.add(name, p);

    if (pd-> design()-> fileExtension()) {
	name += ".";
	name += pd-> design()-> fileExtension();
    }

    return name;
}

void make_document_reference (
    RoseDesign * d,
    stp_product_definition * pd,
    stp_representation * rep,
    const char * filename
    )
{

    // Tag the objects with StixMgrSplitTmpObj::mark_as_temp() so that
    // we can ignore them when restoring the design.


    // we really only want to create one of these, so put them into
    // the object dictionary to save for future calls
    static const char * dtkey = "__CONSTANT document type";
    static const char * idkey = "__CONSTANT identification role";

    stp_document_type * doctyp = ROSE_CAST(stp_document_type, d->findObject(dtkey));
    if (!doctyp) {
	doctyp = pnewIn(d) stp_document_type;
	doctyp-> product_data_type ("");
	d->addName (dtkey, doctyp);
	StixMgrSplitTmpObj::mark_as_temp(doctyp);
    }

    stp_identification_role * idrole = 
	ROSE_CAST(stp_identification_role, d->findObject(idkey));
    if (!idrole) {
	idrole = pnewIn(d) stp_identification_role;
	idrole-> name ("external document id and location");
	d->addName (idkey, idrole);
	StixMgrSplitTmpObj::mark_as_temp(idrole);
    }


    stp_document_file * df = pnewIn(d) stp_document_file;
    df-> id (filename);
    df-> stp_document::name("");
    df-> stp_document::description(NULL);

    df-> stp_characterized_object::name("");
    df-> stp_characterized_object::description(NULL);
    df-> kind(doctyp);
    StixMgrSplitTmpObj::mark_as_temp(df);


    // shown in recprat, does this add anything useful?
    stp_document_representation_type * drt = 
	pnewIn(d) stp_document_representation_type;
    drt-> name("digital");
    drt-> represented_document(df);
    StixMgrSplitTmpObj::mark_as_temp(drt);


    // This is apparently a new way of attaching the name to a
    // document file, and should have the same value as the document
    // file id, not clear to me why this was done.
    //
    stp_applied_external_identification_assignment * aeia = 
	pnewIn(d) stp_applied_external_identification_assignment;

    aeia-> assigned_id (filename);
    aeia-> role(idrole); 
    StixMgrSplitTmpObj::mark_as_temp(aeia);

    // this is for path or URL information?
    aeia-> source(pnewIn(d) stp_external_source); 
    aeia-> source()-> source_id(pnewIn(d) stp_source_item);
    aeia-> source()-> source_id()-> _Identifier(filename);
    StixMgrSplitTmpObj::mark_as_temp(aeia-> source());
    StixMgrSplitTmpObj::mark_as_temp(aeia-> source()-> source_id());

    stp_external_identification_item * eii = pnewIn(d)
	stp_external_identification_item;

    eii-> _document_file(df);
    aeia-> items()-> add(eii);
    StixMgrSplitTmpObj::mark_as_temp(aeia-> items());
    StixMgrSplitTmpObj::mark_as_temp(eii);


    // Attach document file to the property_definition that it
    // describes.  We hook both the pdef and rep to the same document
    // file and ignore the more complex configuration controlled
    // "document as product" approach.
    //
    stp_applied_document_reference * adr = pnewIn(d) stp_applied_document_reference;
    adr-> assigned_document(df);
    adr-> source("");
    StixMgrSplitTmpObj::mark_as_temp(adr);

    stp_document_reference_item * dri = pnewIn(d) stp_document_reference_item;
    dri-> _product_definition(pd);
    adr-> items()-> add(dri);
    // skip the associated role
    StixMgrSplitTmpObj::mark_as_temp(adr->items());
    StixMgrSplitTmpObj::mark_as_temp(dri);


    // Attach document file to the rep that it describes using an
    // ordinary property.
    //
    stp_property_definition * prop = pnewIn(d) stp_property_definition;
    prop-> name ("external definition");
    prop-> definition (pnewIn(d) stp_characterized_definition);
    prop-> definition()-> _characterized_object(df);
    StixMgrSplitTmpObj::mark_as_temp(prop);
    StixMgrSplitTmpObj::mark_as_temp(prop-> definition());

    stp_property_definition_representation * pdr = 
	pnewIn(d) stp_property_definition_representation;
    pdr-> used_representation(rep);
    pdr-> definition (pnewIn(d) stp_represented_definition);
    pdr-> definition()-> _property_definition(prop);
    StixMgrSplitTmpObj::mark_as_temp(pdr);
    StixMgrSplitTmpObj::mark_as_temp(pdr-> definition());
}




void tag_listleaf_for_export (
    RoseDesign * d,
    RoseDomain * dom,
    const char * attname
    )
{
    RoseCursor objs;
    RoseObject * obj;

    objs.traverse(d);
    objs.domain(dom);
    while ((obj=objs.next()) != 0) {
		if (stix_split_has_export (obj, attname)) {
			stix_split_trim_for_export (obj, attname);
			stix_split_mark_needed(obj, (unsigned) -1);
		}
		else {
			stix_split_mark_ignorable(obj, (unsigned) -1);
		}
    }
}

void tag_leaf_for_export (
    RoseDesign * d,
    RoseDomain * dom,
    const char * attname
    )
{
    RoseCursor objs;
    RoseObject * obj;

    objs.traverse(d);
    objs.domain(dom);
    while ((obj=objs.next()) != 0) {
		if (stix_split_has_export (obj, attname)) {
			stix_split_mark_needed(obj, (unsigned) -1);
		}
		else {
			stix_split_mark_ignorable(obj, (unsigned) -1);
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
    while ((obj=objs.next()) != 0) {
		stp_property_definition * pd = ROSE_CAST(stp_property_definition,obj);

		// Only look at properties on things in the destination
		if (!stix_split_is_export (pd->definition())) continue;
		stix_split_mark_needed (pd, (unsigned) -1);

		// mark any representations
		StixMgrPropertyRep * pdrmgr = StixMgrPropertyRep::find(pd);
		if (!pdrmgr) continue;

		unsigned i,sz;
		for (i=0,sz=pdrmgr->size(); i<sz; i++) {
			stix_split_mark_needed (pdrmgr-> get(i), (unsigned) -1);
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
    stix_split_all_ignorable (d, ROSE_DOMAIN(stp_constructive_geometry_representation_relationship));
    stix_split_all_deep_ignorable (d, ROSE_DOMAIN(stp_constructive_geometry_representation));


    objs.traverse(d);
    objs.domain(ROSE_DOMAIN(stp_property_definition_representation));
    while ((obj=objs.next()) != 0) {
	stp_property_definition_representation * pdr = 
	    ROSE_CAST(stp_property_definition_representation,obj);
	stp_representation * rep = pdr-> used_representation();
	RoseObject * def = rose_get_nested_object (pdr-> definition());
	int strip_property = 0;

	if (!stix_split_is_export(def))
	    strip_property = 1;

	else if (def-> isa(ROSE_DOMAIN(stp_property_definition))) 
	{
	    // Strip all geometric validation props
	    stp_property_definition * prop = ROSE_CAST(stp_property_definition,def);
	    const char * propnm = prop? prop-> name(): 0;
		
	    if (propnm && !strcmp(propnm, "geometric validation property"))
		strip_property = 1;

	    RoseObject * def2 = rose_get_nested_object (prop-> definition());
	    if (!stix_split_is_export(def2))
		strip_property = 1;
	}

	// Ignore properties that reference geometry that is stripped.
	// Ignore placements, points and directions because they may
	// have been shared.
	//
	if (!strip_property) 
	{
	    unsigned i,sz;
	    for (i=0, sz=rep-> items()-> size(); i<sz; i++) 
	    {
		stp_representation_item * it = rep-> items()-> get(i);
		if (stix_split_is_needed (it)) continue;
		if (it-> isa(ROSE_DOMAIN(stp_placement))) continue;
		if (it-> isa(ROSE_DOMAIN(stp_point))) continue;
		if (it-> isa(ROSE_DOMAIN(stp_direction))) continue;

		if (it-> isa(ROSE_DOMAIN(stp_topological_representation_item)) ||
		    it-> isa(ROSE_DOMAIN(stp_geometric_representation_item)))
		{
		    strip_property = 1; break;
		}
	    }
	}

	if (strip_property) {
	    //printf ("removing property #%lu %s\n", def->entity_id(), def->domain()-> name());

	    stix_split_mark_ignorable (pdr);
	    stix_split_mark_ignorable (def);
	    stix_split_mark_ignorable (rep, (unsigned) -1);
	}
    }

    // Force the context of representations that we will be writing to
    // be present.
    //
    objs.traverse(d);
    objs.domain(ROSE_DOMAIN(stp_representation));
    while ((obj=objs.next()) != 0) {
	stp_representation * rep = ROSE_CAST(stp_representation,obj);
	if (stix_split_is_export(rep))
	    stix_split_mark_needed(rep->context_of_items(),(unsigned) -1);
    }    
}






void tag_subassembly(
    stp_product_definition * pd
    )
{

    if (!pd) return;
    RoseDesign * src = pd->design();    

    // Now loop over any children and tag them too
    unsigned i,sz;
    StixMgrAsmProduct * pd_mgr = StixMgrAsmProduct::find(pd);
    if (!pd_mgr) return;  // not a proper part

    // mark the pd, pdf, part, and contexts
    stix_split_mark_needed (pd, (unsigned) -1);

    // Get all props, including the main shape
    tag_step_properties(pd-> design());

    //printf("TAGGING PART #%lu\n", pd-> entity_id());

    for (i=0, sz=pd_mgr->child_nauos.size(); i<sz; i++) 
    {
	// tag children.  Be sure to get shape property so that any
	// context dependent shape rep gets pulled in as well.
	stp_next_assembly_usage_occurrence * nauo = pd_mgr->child_nauos[i];
	stix_split_mark_needed (nauo, (unsigned) -1);
	stix_split_mark_needed (stix_get_shape_property(nauo), (unsigned) -1);

	// Mark product definition and relationship
	tag_subassembly (stix_get_related_pdef (nauo));
    }

    for (i=0, sz=pd_mgr->shapes.size(); i<sz; i++) {
	// Mark all direct shapes and any related shapes.  We will get
	// the property things in between later.

	StixMgrAsmShapeRep * rep_mgr = StixMgrAsmShapeRep::find(pd_mgr->shapes[i]);
	if (!rep_mgr) continue;

	unsigned j,szz;
	for (j=0, szz=rep_mgr->child_rels.size(); j<szz; j++) {
	    stix_split_mark_needed (rep_mgr->child_rels[j], (unsigned) -1);
	}
    }


    // Look for things connecting the shape and bom relations
    RoseCursor objs;
    RoseObject * obj; 

    objs.traverse(src);
    objs.domain(ROSE_DOMAIN(stp_context_dependent_shape_representation));
    while ((obj=objs.next()) != 0) {
	stp_context_dependent_shape_representation * cdsr = 
	    ROSE_CAST(stp_context_dependent_shape_representation,obj);

	if (stix_split_is_export (cdsr-> representation_relation()) &&
	    stix_split_is_export (cdsr-> represented_product_relation())) {
	    stix_split_mark_needed (cdsr);
	}
    }
}



void tag_and_strip_exported_from_tree(
    stp_product_definition * pd
    )
{
    RoseDesign * src = pd-> design();

    unsigned i,sz;
    StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);
    StixMgrSplitProduct * split_mgr = StixMgrSplitProduct::find(pd);

    if (!pm) return;

    char * pname = pd-> formation()-> of_product()->name();
    printf ("PD #%lu %s\n", pd->entity_id(), pname? pname: "");

    // MARK THIS PART FOR EXPORT.  Follow up to the product root and
    // get all of the contexts, then we need to look at the shape
    // properties and whatnot.

    stix_split_mark_needed(pd, (unsigned) -1);
    if (!split_mgr) 
    {
	printf (" => PD not exported, examining children\n");

	// Not exported, examine any children.
	for (i=0, sz=pm->child_nauos.size(); i<sz; i++) 
	{
	    stix_split_mark_needed(pm->child_nauos[i]);

	    tag_and_strip_exported_from_tree(
		stix_get_related_pdef (pm->child_nauos[i])
		);
	}

	printf (" => PD #%lu marking shapes\n", pd-> entity_id());
	stix_split_mark_needed (stix_get_shape_property(pd));

	// make sure that we get the shapes for this too
	for (i=0, sz=pm->shapes.size(); i<sz; i++) 
	{
	    stix_split_mark_needed(pm->shapes[i], (unsigned) -1);
	    // /* The parent and child rep_relationships */
	    // StpAsmShapeRepRelVec child_rels;
	    // StpAsmShapeRepRelVec parent_rels;
	}

	printf (" => PD #%lu done marking shapes\n", pd-> entity_id());
	return;
    }


    if (split_mgr-> part_stripped) {
	printf (" => PD already cleared\n");
	return;
    }

    split_mgr-> part_stripped = 1;
    printf (" => PD was exported, trimming shape\n");

    stp_product_definition_shape * pds = stix_get_shape_property(pd);
    if (!pds) {
	printf ("PD #%lu - no shape property!\n", pd->entity_id());
	return;
    }

    stix_split_mark_needed(pds);

    // This part has an external definition, remove any geometry and
    // attach an external reference.  Find the first shape that is a
    // plain shape reference (no subtype).  This is what we would like
    // to attach the external ref to.
    //
    stp_representation * old_rep = 0;
    for (i=0, sz=pm->shapes.size(); i<sz; i++) 
    {
	if (pm->shapes[i]->domain() == ROSE_DOMAIN(stp_shape_representation)) {
	    old_rep = pm->shapes[i];
	    break;
	}
    }
    if (!old_rep) {
	old_rep = pm->shapes[0];

	if (!old_rep) {
	    printf ("PD #%lu: could not find shape to relate\n", pd-> entity_id());
	    return;
	}
	if (pm->shapes.size() > 1) {
	    printf ("PD #%lu: more than one main shape!\n", pd-> entity_id());
	    return;
	}
    }

    StixMgrPropertyRep * repmgr = StixMgrPropertyRep::find(pds);
    for (i=0, sz=repmgr->size(); i<sz; i++) 
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
    main_rep-> name (old_rep-> name());
    main_rep-> context_of_items (old_rep-> context_of_items());
    main_rep-> entity_id (old_rep-> entity_id());

    // Add placements, but ignore everything else.
    for (i=0, sz=old_rep->items()->size(); i<sz; i++) 
    {
	// FILTER EXTRA AXIS PLACEMENTS
	stp_representation_item * it = old_rep->items()->get(i);
	if (it-> isa(ROSE_DOMAIN(stp_placement))) {
	    main_rep-> items()-> add (it);
	    stix_split_mark_needed(it,(unsigned) -1);
	}
    }

    // So darn many things may reference a rep, beyond just the
    // shape def relation, so use the substitute mechanism to bulk
    // replace.  Mark as a temp so that we can restore later.
    //
    stix_split_mark_needed(main_rep);
    rose_register_substitute (old_rep, main_rep);
    StixMgrSplitTmpObj::mark_as_temp(main_rep, old_rep);
    StixMgrSplitTmpObj::mark_as_temp(main_rep-> items());

    // hook in the document reference
    make_document_reference (pd-> design(), pd, main_rep, split_mgr-> part_filename);
}


void tag_and_strip_exported_products(
    RoseDesign * d
    )
{
    // Navigate the assembly and trim exported geometry
    unsigned i,sz;
    RoseCursor objs;
    RoseObject * obj;

    StpAsmProductDefVec roots;
    stix_find_root_products (&roots, d);
    for (i=0, sz=roots.size(); i<sz; i++)
	tag_and_strip_exported_from_tree(roots[i]);


    // We had to do some substitutions for the shape reps.  Expand
    // everything out and clear the managers.

    rose_update_object_references (d);
    objs.traverse(d);
    objs.domain(0);
    while ((obj=objs.next()) != 0)
	obj-> remove_manager (ROSE_MGR_SUBSTITUTE);
}



void tag_shape_annotation(
    RoseDesign * d
    )
{
    RoseCursor objs;
    RoseObject * obj;

    objs.traverse(d);
    objs.domain(ROSE_DOMAIN(stp_shape_aspect));
    while ((obj=objs.next()) != 0) {
	stp_shape_aspect * sa = ROSE_CAST(stp_shape_aspect,obj);
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
    while ((obj=objs.next()) != 0) {
	stp_shape_aspect_relationship * sar =
	    ROSE_CAST(stp_shape_aspect_relationship,obj);

	stp_shape_aspect * ed = sar-> related_shape_aspect();
	stp_shape_aspect * ing = sar-> relating_shape_aspect();

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
    while ((obj=objs.next()) != 0) {
	stp_item_identified_representation_usage * iiu = 
	    ROSE_CAST(stp_item_identified_representation_usage,obj);

	// By default, ignore any item refs that point to things
	// outside of our export set.  We may override some of this
	// later.
	//
	if (!stix_split_is_export (iiu-> definition()) ||
	    !stix_split_is_export (iiu-> used_representation()) ||
	    !stix_split_is_export (iiu-> identified_item()))
	    stix_split_mark_ignorable (iiu);
    }


    objs.traverse(d);
    objs.domain(ROSE_DOMAIN(stp_geometric_item_specific_usage));
    while ((obj=objs.next()) != 0) {
	stp_item_identified_representation_usage * iiu = 
	    ROSE_CAST(stp_item_identified_representation_usage,obj);

	// Force these if everything is there
	if (stix_split_is_export (iiu-> used_representation()) &&
	    stix_split_is_export (iiu-> identified_item()) &&
	    stix_split_is_export (iiu-> definition()))
	    stix_split_mark_needed (iiu, (unsigned) -1);
    }


    // MARK ALL OF THE ANNOTATION OCCURRENCES
    //
    // Force these if attached to a common shape aspect.  The
    // representation is the draughting model and might contain a
    // other annotations, so we need to avoid blanket marking it.
    //
    objs.traverse(d);
    objs.domain(ROSE_DOMAIN(stp_draughting_model_item_association));
    while ((obj=objs.next()) != 0) {
	stp_item_identified_representation_usage * iiu = 
	    ROSE_CAST(stp_item_identified_representation_usage,obj);

	if (stix_split_is_export (iiu-> definition())) {
	    stix_split_mark_needed (iiu);
	    stix_split_mark_needed (iiu-> identified_item(), (unsigned) -1);
	    stix_split_mark_needed (iiu-> used_representation());
	}
	else {
	    stix_split_mark_ignorable (iiu);
	    stix_split_mark_ignorable (iiu-> identified_item(), (unsigned) -1);
	    stix_split_mark_ignorable (iiu-> used_representation());
	}
    }


    // MARK ANNOTATION PLANES 
    //
    // Force a plane if it points to something that is used.
    //
    tag_listleaf_for_export (d, ROSE_DOMAIN(stp_annotation_plane), "elements");
    tag_listleaf_for_export (d, ROSE_DOMAIN(stp_draughting_model), "items");
    

    // This connects dimensions to a representation
    tag_leaf_for_export (d, ROSE_DOMAIN(stp_dimensional_characteristic_representation), "dimension");

}


    // // Deep move these reps, which will take most of the styles and
    // // annotations.  The deep move may reach some reps or items that
    // // we need to keep, but they will be restored before we save.
    // //
    // stix_split_all_deep_ignorable (master, ROSE_DOMAIN(stp_mechanical_design_geometric_presentation_representation));

    // stix_split_all_deep_ignorable (master, ROSE_DOMAIN(stp_draughting_model));
    // stix_split_all_deep_ignorable (master, ROSE_DOMAIN(stp_presentation_layer_assignment));


    // // Do any remaining annotations deeply, then the styles, then
    // // shallow move any remaining styled items.
    // //
    // stix_split_all_deep_ignorable (master, ROSE_DOMAIN(stp_annotation_occurrence));
    // stix_split_all_deep_ignorable (master, ROSE_DOMAIN(stp_presentation_style_assignment));
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_styled_item));

    // // This is a mapped item, so we deep move it to pick up the map.
    // stix_split_all_deep_ignorable (master, ROSE_DOMAIN(stp_camera_image));

    // // Shallow move all of these in bulk rather than try to figure out
    // // how to trace through their inter-references.
    // //
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_area_in_set));
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_presented_item_representation));
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_applied_presented_item));

    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_presentation_set));
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_presentation_view));
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_camera_usage));

    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_invisibility));
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_annotation_occurrence_relationship));

    // // deep move to pick up the size box
    // objs.traverse(master);
    // objs.domain(ROSE_DOMAIN(stp_presentation_size));
    // while ((obj=objs.next()) != 0) {
    // 	// make sure we get bounding box
    // 	stp_presentation_size * ps = ROSE_CAST(stp_presentation_size,obj);
    // 	stix_split_mark_ignorable (ps-> Size(), (unsigned) -1);
    // 	stix_split_mark_ignorable (ps, (unsigned) -1);
    // }

    // objs.traverse(master);
    // objs.domain(ROSE_DOMAIN(stp_presentation_representation));
    // while ((obj=objs.next()) != 0) {
    // 	// make sure we get any mapped items
    // 	stix_split_mark_ignorable (obj, (unsigned) -1);
    // }


    // // Move any draughting item associations along with the shared
    // // shape aspect that they use to link to geometric_item_specific_usage
    // objs.traverse(master);
    // objs.domain(ROSE_DOMAIN(stp_draughting_model_item_association));
    // while ((obj=objs.next()) != 0) {
    // 	stp_draughting_model_item_association * di = 
    // 	    ROSE_CAST(stp_draughting_model_item_association,obj);

    // 	stix_split_mark_ignorable (di-> definition());;
    // 	stix_split_mark_ignorable (di);;
    // }

    // // shallow move the associations on the other side of the link
    // //stix_split_all_ignorable (master, ROSE_DOMAIN(stp_geometric_item_specific_usage));

    // // Just ditch all for now
    // stix_split_all_ignorable (master, ROSE_DOMAIN(stp_item_identified_representation_usage));




void tag_step_extras(
    RoseDesign * d
    )
{
    // Tag STEP assignments and other product data annotations when
    // they reference something that has also been tagged for export.
    //

    // Move presentation style.  
    tag_leaf_for_export(d, ROSE_DOMAIN(stp_styled_item), "item");
    tag_listleaf_for_export(
	d, ROSE_DOMAIN(stp_mechanical_design_geometric_presentation_representation), "items"
	);



    // Get the second-class leaf-like annotations and assignments
    tag_leaf_for_export(
	d, ROSE_DOMAIN(stp_application_protocol_definition), "application"
	);

    tag_leaf_for_export(d, ROSE_DOMAIN(stp_role_association), "item_with_role");
    tag_leaf_for_export(d, ROSE_DOMAIN(stp_id_attribute), "identified_item");
    tag_leaf_for_export(d, ROSE_DOMAIN(stp_description_attribute), "described_item");
    tag_leaf_for_export(d, ROSE_DOMAIN(stp_name_attribute), "named_item");


    // Various assignments, do the people and date time last because
    // they are can be applied to the other assignments.
    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_security_classification_assignment), "items");
    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_certification_assignment), "items");

    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_effectivity_assignment), "items");
    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_ineffectivity_assignment), "items");

    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_person_and_organization_assignment), "items");
    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_organization_assignment), "items");

    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_date_and_time_assignment), "items");
    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_applied_date_assignment), "items");

    // extra presentation things
    tag_listleaf_for_export(d, ROSE_DOMAIN(stp_presentation_layer_assignment), "assigned_items");
}





static void add_parts_dfs_order(
    stp_product_definition * pd,
    StpAsmProductDefVec &dfslist
    )
{
    if (!pd) return;

    unsigned i,sz;
    StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);

    // Already visited?
    for (i=0, sz=dfslist.size(); i<sz; i++)
	if (dfslist[i] == pd)  return;
    
    // Add all subproducts first
    for (i=0, sz=pm->child_nauos.size(); i<sz; i++) 
    {
	add_parts_dfs_order(
	    stix_get_related_pdef (pm->child_nauos[i]), dfslist
	    );
    }

    // Who knows, it could happen.
    for (i=0, sz=dfslist.size(); i<sz; i++)
	if (dfslist[i] == pd)  return;

    dfslist.append(pd);
}


	


// ======================================================================
// ======================================================================
// ======================================================================
// Move tagged objects into separate design and write out.
// ======================================================================
// ======================================================================
// ======================================================================


void export_part_file(
    stp_product_definition * pd,
    const char * dstdir
    )
{
    RoseCursor objs;
    RoseObject * obj;

    if (!pd) return;
    RoseDesign * src = pd->design();    

    StixMgrSplitProduct * split_mgr = StixMgrSplitProduct::find(pd);
    if (split_mgr) return;   // already been exported

    StixMgrAsmProduct * pd_mgr = StixMgrAsmProduct::find(pd);
    if (!pd_mgr) return;  // not a proper part

    stix_split_clear_needed_and_ignore_trimmed (src);

    //--------------------------------------------------
    // Create the destination design and initialize it with some basic
    // information from the original.

    split_mgr = new StixMgrSplitProduct;
    split_mgr-> part_filename = get_part_filename(pd);
    pd-> add_manager(split_mgr);


    RoseDesign * dst = new RoseDesign (split_mgr-> part_filename);
    dst-> format(src->format());
    dst-> fileDirectory(dstdir);
    dst-> fileExtension(src->fileExtension());

    copy_header (dst, src);
    copy_schema (dst, src);

    tag_subassembly(pd);
    tag_shape_annotation(src);
    tag_step_extras(src);

    // extra properties?

    //--------------------------------------------------
    // Move all of the objects that we need to export over to the
    // destination design.   It does not care
    // where aggregates are though.
    //
    objs.traverse(src);
    objs.domain(ROSE_DOMAIN(RoseStructure));
    while ((obj=objs.next()) != 0) {
	if (stix_split_is_export(obj)) obj-> move(dst);
    } 

    // The P21 writer unfortunately trims foreign selects.  Our
    // marking algorithm only tracks entities so we just move them all
    // selects temporarily, to avoid missing measure values, etc.
    objs.traverse(src);
    objs.domain(ROSE_DOMAIN(RoseUnion));
    while ((obj=objs.next()) != 0) { obj-> move(dst); } 


    // Save, keep IDs for tracing back to the original file.
    RoseP21Writer::preserve_eids = ROSE_TRUE;
    RoseP21Writer::sort_eids = ROSE_TRUE;
    dst->save();

    // Mark everything as having been exported for later use
    stix_split_all_trimmed (dst);


    //--------------------------------------------------
    // Restore references to temporary objects and move everything to
    // the original design
    stix_restore_all_tmps(dst);

    objs.traverse(dst);
    objs.domain(0);
    while ((obj = objs.next()) != 0) {
	// Ignore temporaries created for the split
	if (!StixMgrSplitTmpObj::find(obj)) obj-> move(src);
    }

    delete dst;    // done
}





void export_all_parts_from_tree(
    stp_product_definition * pd,
    const char * dstdir
    )
{
    // Search the tree and export any part that has geometry
    
    if (!pd) return;

    unsigned i,sz;
    StixMgrAsmProduct * pm = StixMgrAsmProduct::find(pd);

    stp_product_definition_formation * pdf = pd-> formation();
    stp_product * p = pdf? pdf-> of_product(): 0;

    // Name the file after the product name if possible, but this
    // means that we need to keep a dictionary of files already
    // Does this product have direct geomety?

    // if yes, then export
    // written.

    // Does this have real shapes?
    if (pm->child_nauos.size()) {
	printf("\nderka derka size: %d\n", pm->child_nauos.size());
	printf ("IGNORING PD #%lu (%s) (assembly)\n",
		pd->entity_id(), p->name()? p->name():"");

	// recurse to all subproducts, do this even if there is geometry?
	for (i=0, sz=pm->child_nauos.size(); i<sz; i++) 
	{
	    export_all_parts_from_tree(
		stix_get_related_pdef (pm->child_nauos[i]), dstdir
		);
	}

    }
    else {
	for (i=0, sz=pm->shapes.size(); i<sz; i++) {
	    if (has_geometry(pm->shapes[i])) break;
	}

	// no shapes with real geometry
	if (i<sz) {
		printf("\nderka derka size: %d\n", pm->child_nauos.size());
	    printf ("EXPORTING PD #%lu (%s)\n",
		    pd->entity_id(), p->name()? p->name():"");

	    export_part_file (pd, dstdir);
	}
	else {
	    printf ("IGNORING PD #%lu (%s) (no geometry)\n",
		    pd->entity_id(), p->name()? p->name():"");	
	    return;
	}
    }
}








void export_master_file(
    RoseDesign * src,
    const char * dstdir
    )
{
    // Export a single master file without geometry which contains
    // external references to previously exported part files.  For the
    // master file, we start with everything and specifically exclude
    // things, while the part files were built in the opposite manner.
    //
    // If you wanted to export several nested masters, you would need
    // a different algorithm.  Perhaps a destructive approach might
    // work better -- export and delete leaves and subtrees in a depth
    // first pass through the assembly, but you still may end up with
    // orphan data (assignments and such) if you did not know to look
    // for them.
    //

    RoseCursor objs;
    RoseObject * obj;

    if (!src) return;

    //--------------------------------------------------
    // Create the destination design and initialize it with some basic
    // information from the original.

    RoseDesign * dst = new RoseDesign (get_master_filename(src));
    dst-> format(src->format());
    dst-> fileDirectory(dstdir);
    dst-> fileExtension(src->fileExtension());

    copy_header (dst, src);
    copy_schema (dst, src);

    //------------------------------
    // Move everything to the destination design, then we will move
    // back anything that we want to omit.  This way, any unexpected
    // data will remain in the master.
    //

    stix_split_clear_needed_and_ignore_trimmed (src);

    objs.traverse(src);
    objs.domain(0);
    while ((obj=objs.next()) != 0) 
	obj-> move(dst);


    // Clear out the shape reps for all of the leaf products.  We need
    // to keep the assembly placements, but nothing else. 
    //
    tag_and_strip_exported_products(dst);
    tag_shape_annotation(dst);
    tag_step_extras(dst);
    tag_properties(dst);
    
    //--------------------------------------------------
    // Move all of the objects that we need to export over to the
    // destination design.  We look at all objects because the P21
    // writer unfortunately trims foreign selects.  It does not care
    // where aggregates are though.
    //

    // KLUDGE - stuff that should be present may have been moved out,
    // so make sure we put everything back first.
    objs.traverse(src);
    objs.domain(0);
    while ((obj=objs.next()) != 0) {
	if (stix_split_is_needed(obj)) obj-> move(dst);
    } 

    // The P21 writer unfortunately trims foreign selects.  Our
    // marking algorithm only tracks entities but the setup above
    // moved everything in, so we do not need any extra action.

    // Move back things that we do not want
    objs.traverse(dst);
    objs.domain(ROSE_DOMAIN(RoseStructure));
    while ((obj=objs.next()) != 0) {
	if (!stix_split_is_export(obj)) obj-> move(src);
    } 

    //--------------------------------------------------
    // Save, keep IDs for tracing back to the original file.
    RoseP21Writer::preserve_eids = ROSE_TRUE;
    RoseP21Writer::sort_eids = ROSE_TRUE;
    dst->save();

    //--------------------------------------------------
    // Restore everything to the original design

    stix_split_all_trimmed (dst);
    stix_restore_all_tmps(dst);

    objs.traverse(dst);
    objs.domain(0);
    while ((obj = objs.next()) != 0) {
	// Ignore temporaries created for the split
	if (!StixMgrSplitTmpObj::find(obj)) obj-> move(src);
    }

    delete dst;    // done
}



stp_product_definition * stix_find_product_pdef_by_name (
    RoseDesign * d,
    const char * prod_name
    )
{
    if (!d || !prod_name) return 0;

    RoseCursor objs;
    RoseObject * obj;

    objs.traverse(d);
    objs.domain(ROSE_DOMAIN(stp_product_definition));
    while ((obj = objs.next()) != 0) {
	stp_product_definition * pd = ROSE_CAST(stp_product_definition,obj);
	stp_product_definition_formation * pdf = pd-> formation();
	stp_product * p = pdf? pdf-> of_product(): 0;

	if (!p-> name()) continue;
	// check for design context or SDR or something?
	if (!strcmp(p->name(), prod_name))
	    return pd;
    }
    return 0;
}



#define NEXT_ARG(i,argc,argv) ((i<argc)? argv[i++]: 0)

int main (int argc, char ** argv)
{
    int i=1;
    stplib_init();	// initialize merged cad library

    const char * arg;
    const char * srcfile = 0;
    const char * dstdir = 0;
    ListOfString prods;
    int do_tree = 0;
    int do_all = 1;
    int do_top = 0;
    int do_mast = 0;

    /* must have at least one arg */
    if (argc < 2) usage(argv[0]);

    /* get remaining keyword arguments */
    while ((arg=NEXT_ARG(i,argc,argv)) != 0)
    {
	/* command line options */
	if (!strcmp (arg, "-h") ||
	    !strcmp (arg, "-help") ||
	    !strcmp (arg, "--help"))
	{
	    long_usage(argv[0]);
	}

	else if (!strcmp (arg, "-o")) 
	{	
	    dstdir = NEXT_ARG(i,argc,argv);
	    if (!dstdir) {
		fprintf (stderr, "option: -o <file>\n");
		exit (1);
	    }
	}

	else if (!strcmp (arg, "-p")) 
	{	
	    char * p = NEXT_ARG(i,argc,argv);
	    if (!p) {
		fprintf (stderr, "option: -p <name>\n");
		exit (1);
	    }
	    prods.add(p);
	    do_all = 0;
	}
	else if (!strcmp (arg, "-all")) {
	    do_all = 1;
	}
	else if (!strcmp (arg, "-top")) {
	    do_all = 0;
	    do_top = 1;
	}
	else if (!strcmp (arg, "-nomast")) {
	    do_mast = 0;
	}
	else if (!strcmp (arg, "-trees")) {
	    do_tree = 1;
	    name_style = name_style_name;  // use different convention
	    do_all = 0;
	    do_mast = 1;
	    do_top = 0;
	}
	else {
	    srcfile = arg;
	    break;
	}
    }

    if (!srcfile) usage(argv[0]);
    if (!dstdir) {
	// dstdir = "";   // build from filename
    }    


    RoseDesign * d = ROSE.findDesign(srcfile);
    if (!d) {
	printf ("Could not open STEP file %s\n", srcfile);
	exit (1);
    }

    if (!rose_dir_exists (dstdir) && (rose_mkdir(dstdir) != 0)) {
	printf ("Could not create output directory %s\n", dstdir);
	exit (1);
    }

    rose_compute_backptrs(d);
    stix_tag_asms(d);
    StixMgrProperty::tag_design(d);
    StixMgrPropertyRep::tag_design(d);


    if (do_all) 
    {
	// Navigate through the assembly and export any part which has
	// geometry.
	unsigned i,sz;
	StpAsmProductDefVec roots;
	stix_find_root_products (&roots, d);

	StixMgrSplitStatus::export_only_needed = 1;
	printf ("\nPRODUCT TREE ====================\n");
	for (i=0, sz=roots.size(); i<sz; i++)
	    export_all_parts_from_tree(roots[i], dstdir);

	// put any unrecongized data into the master file.
	StixMgrSplitStatus::export_only_needed = 0;
	if (do_mast) {
	    printf ("\nMASTER FILE ====================\n");
	    export_master_file(d, dstdir);
	}
    }
    if (do_tree) 
    {
	// Build a DFS list of all PDs, then export parts for each
	// one.  Used by WebGL assy viewer.
	unsigned i,sz;
	StpAsmProductDefVec roots;
	StpAsmProductDefVec dfslist;
	stix_find_root_products (&roots, d);

	for (i=0, sz=roots.size(); i<sz; i++)
	    add_parts_dfs_order(roots[i], dfslist);

	StixMgrSplitStatus::export_only_needed = 1;
	for (i=0, sz=dfslist.size(); i<sz; i++) {
	    stix_split_delete_all_marks (d);
	    export_part_file (dfslist[i], dstdir);
	}
    }
    else if (prods.size() > 0)
    {
	// export products in a depth first ordering to make sure that
	// we get them as intended.

	unsigned i,sz;
	StixMgrSplitStatus::export_only_needed = 1;
	for (i=0, sz=prods.size(); i<sz; i++)
	{
	    // find product name = prods.get(i)
	    stp_product_definition * pd = 
		stix_find_product_pdef_by_name (d, prods.get(i));
	    
	    if (!pd) {
		printf ("ERROR: no product named '%s'\n", prods.get(i));
		continue;
	    }

	    export_part_file (pd, dstdir);
	}

	// put any unrecongized data into the master file.
	StixMgrSplitStatus::export_only_needed = 0;
	if (do_mast) {
	    printf ("\nMASTER FILE ====================\n");
	    export_master_file(d, dstdir);
	}
    }

    return 0;
}
