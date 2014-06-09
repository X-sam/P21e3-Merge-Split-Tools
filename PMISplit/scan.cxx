
// Create a manager object to hold the roseref the object should be
// replaced with then traverse everything in the design and do a
// put_ref when you find one.
#include "scan.h"

void print_use(RoseRefUsage * ru)
{
	if (!ru) {
		printf("=> no usage\n");
		return;
	}

	printf("=> usage\n");
	printf("\tURL: %s\n", ru->ref()->uri());
	printf("\tObj: %s\n", ru->user() ? ru->user()->domain()->name() : "none");
	printf("\tAtt: %s\n", ru->user_att() ? ru->user_att()->name() : "none");
	printf("\tIdx: %d\n", ru->user_idx());
}

void print_ref_use(const char * nm, RoseReference * r)
{
	printf("%s\n", nm);
	RoseRefUsage * ru = r ? r->usage() : 0;
	if (!ru) printf("=> no usage\n");
	while (ru) { print_use(ru);   ru = ru->next_for_ref(); }
}


void scan_pointer(
	RoseObject * parent,
	RoseAttribute * att,
	unsigned idx
	)
{
	RoseObject * obj = parent->getObject(att, idx);
	if (!obj) return;

	MyURIManager * mgr = MyURIManager::find(obj);
	if (!mgr) return;

	RoseReference * ref = mgr->should_go_to_uri();
	rose_put_ref(ref, parent, att, idx);
	//print_ref_use("hi", ref);
}

void scan_aggregate_pointers(RoseObject * object)
{
	RoseAggregate * agg = (RoseAggregate*)object;  // no virtual base
	ListOfRoseAttribute *  atts = object->domain()->typeAttributes();
	RoseAttribute * att = atts->first();
	unsigned i, sz;

	if (!att->isObject())
		return;

	for (i = 0, sz = agg->size(); i < sz; i++)
		scan_pointer(object, att, i);
}



void scan_entity_pointers(RoseObject * object)
{
	ListOfRoseAttribute *  atts = object->domain()->typeAttributes();
	unsigned i, sz;

	for (i = 0, sz = atts->size(); i < sz; i++) {
		RoseAttribute * att = atts->get(i);
		if (att->isObject())
			scan_pointer(object, att, 0);
	}
}

void scan_select_pointers(RoseObject * object)
{
	RoseUnion * sel = (RoseUnion*)object;  // no virtual base
	RoseAttribute * att = sel->getAttribute();

	if (att->isObject())
		scan_pointer(object, att, 0);
}



void update_uri_forwarding(RoseDesign * design)
{
	RoseObject * obj;
	RoseCursor objects;

	/* Need to traverse all design sections to make sure that we
	* update objects referenced by the nametable and others.
	*/
	objects.traverse(design);
	objects.domain(ROSE_DOMAIN(RoseStructure));
	while ((obj = objects.next()) != 0) scan_entity_pointers(obj);

	objects.traverse(design);
	objects.domain(ROSE_DOMAIN(RoseAggregate));
	while ((obj = objects.next()) != 0) scan_aggregate_pointers(obj);

	objects.traverse(design);
	objects.domain(ROSE_DOMAIN(RoseUnion));
	while ((obj = objects.next()) != 0) scan_select_pointers(obj);
}



ROSE_IMPLEMENT_MANAGER_COMMON(MyURIManager);

MyURIManager * MyURIManager::find(RoseObject * obj)
{
	return (MyURIManager*)(obj ? obj->find_manager(type()) : 0);
}

MyURIManager * MyURIManager::make(RoseObject * obj)
{
	MyURIManager* mgr = MyURIManager::find(obj);
	if (!mgr) {
		mgr = new MyURIManager;
		obj->add_manager(mgr);
	}
	return mgr;
}
