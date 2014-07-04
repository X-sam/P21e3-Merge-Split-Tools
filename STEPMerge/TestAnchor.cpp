//Makes two new designs, testRefAndAnchor & testAnchorAndData.
//Adds a point to testAnchorAndData, and an anchor which points to it.
//Adds a reference to testRefAndAnchor, and an anchor which points to it.
//Saves the files.
#include <rose.h>
#include <stp_schema.h>
#include <iostream>
#include <string>

int URIParse(const std::string URI, std::string &filename, std::string &anchor, std::string &workingdir);
int ResolveRRU(RoseRefUsage * rru, RoseObject * obj);

int main(int argc, char* argv[])
{
	ROSE.quiet(1);	//Get rid of annoying ST-Dev output.
	stplib_init();	// initialize merged cad library
	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.
	RoseP21Writer::preserve_eids=ROSE_TRUE;
	RoseDesign * testAnchorAndData = ROSE.newDesign("testAnchorAndData");
	stplib_put_schema(testAnchorAndData, stplib_schema_ap214);
	stp_cartesian_point *point =pnew stp_cartesian_point;
	point->entity_id(321);		//Make the entity ID something we can check later.
	point->name("TestPoint");
	point->coordinates()->add(1.01);
	point->coordinates()->add(2.01);
	point->coordinates()->add(3.01);
	testAnchorAndData->addName("TestAnchor", point);
	testAnchorAndData->save();
	RoseDesign * testRefAndAnchor = ROSE.newDesign("testRefAndAnchor");
	stplib_put_schema(testRefAndAnchor, stplib_schema_ap214);
	auto ref = rose_make_ref(testRefAndAnchor, "testAnchorAndData.stp#TestAnchor");		//Manually make the reference point to the other file.
	ref->entity_id(123);		//Just give it an entity id so we can check it later.
	testRefAndAnchor->addName("ParentAnchor", ref);	//Add an anchor which points to the reference.
	stp_circular_area *area = pnew stp_circular_area;	//Make something to test if the reference resolves properly
	area->name("testarea");
	area->radius(2.0);
	rose_put_ref(ref, area, "centre");
	testRefAndAnchor->save();
	//Okay, so now we have two files- one with an anchor pointing to a reference, and one with an anchor and the data the anchor points to. Lets see what the anchor in the higher file points to.
	std::cout << "Anchor " << testRefAndAnchor->nameTable()->listOfKeys()->get(0) <<" points to (should be 123): " <<testRefAndAnchor->nameTable()->listOfValues()->get(0)->entity_id() <<'\n';
	//That should output "Anchor ParentAnchor points to: 123"
	//Now lets merge. 
	//First things first, we close the child design just to be certain we are doing it EXACTLY LIKE MERGE.
	rose_move_to_trash(testAnchorAndData);  
	testRefAndAnchor->saveAs("testmerge.stp");
	rose_move_to_trash(testRefAndAnchor);
	rose_empty_trash();						//now all of this is out of memory.
	RoseDesign *parent = ROSE.findDesign("testmerge.stp");	//Open the new file. Don't mess with the files we created earlier.
	if (!parent)
	{
		std::cout << "Shit's fucked\n";
		return EXIT_FAILURE;
	}
	//Call the URI parser function.
	RoseCursor * cursor = new RoseCursor;
	cursor->traverse(parent->reference_section());
	cursor->domain(ROSE_DOMAIN(RoseReference));//Go through all the references in parent. All one of them.
	std::cout << "Cursor size (should be 1): " << cursor->size() <<'\n';
	RoseObject * obj = cursor->next();
	std::cout << "Reference ID (should be 123): " << obj->entity_id() << '\n';
	ref = ROSE_CAST(RoseReference, obj);	//Now we have the reference as an actual Reference object instead of a generic RoseObject.
	std::string file, anchor,dir="./";
	URIParse(ref->uri(), file, anchor,dir);
	std::cout << "file (should be testAnchorAndData.stp): " << file << "\nanchor (should be TestAnchor): " << anchor << "\ndir (should be ./): " << dir << '\n';
	std::cout << "Anchor " << parent->nameTable()->listOfKeys()->get(0) << " points to (should be 123): " << parent->nameTable()->listOfValues()->get(0)->entity_id() << '\n';
	RoseDesign *child = ROSE.findDesign(file.substr(0, file.find('.')).c_str());
	if (!child)
	{
		std::cout << "Shit's fucked\n";
		return EXIT_FAILURE;
	}
	std::cout << "Anchor " << parent->nameTable()->listOfKeys()->get(0) << " points to (should be 123): " << parent->nameTable()->listOfValues()->get(0)->entity_id() << '\n';
	obj = child->findObject(anchor.c_str());	//Get the object associated with the anchor
	std::cout << "Child Object Pointed to by Anchor Entity ID (should be 321): " << obj->entity_id() <<'\n';
	obj->move(parent);	//move object to parent so we have it in the right place, then resolve references.
	RoseRefUsage *rru = ref->usage();	//rru is a linked list of all the objects that use ref
	do
	{
		std::cout << "Resolving reference in entity ID: " << rru->user()->entity_id() <<'\n';
		int rrureturn = ResolveRRU(rru,obj);
		if (-1 == rrureturn) break;
	} while (rru = rru->next_for_ref());	//Do this for anything that uses the reference.
	//parent->save();
	//At this point, the data section references are resolved. You can check the output file if you want to be sure. Just uncomment that save.
	//Now resolve the reference in the anchor section.
	DictionaryOfRoseObject * anchors = parent->nameTable();
	for (unsigned i = 0, sz = anchors->size(); i < sz; i++)
	{
		if (anchors->listOfValues()->get(i) == ref)
		{
			std::cout << "anchor " << anchors->listOfKeys()->get(i) << " has an entity ID of (should be 123): " << anchors->listOfValues()->get(i)->entity_id() <<'\n';
			anchors->add(anchors->listOfKeys()->get(i), obj);
			std::cout << "anchor " << anchors->listOfKeys()->get(i) << " has an entity ID of (should be 321): " << anchors->listOfValues()->get(i)->entity_id() << '\n';
		}
	}
	parent->save();
	return EXIT_SUCCESS;
}

int URIParse(const std::string URI, std::string &filename, std::string &anchor, std::string &workingdir)
{
	//URI looks like "filename.stp#item1234"
	//Split that into "filename.stp" and "item1234"
	int poundpos = URI.find_first_of('#');
	filename = URI.substr(0, poundpos);	//reffile contains something like "filename.stp" or maybe "path/to/filename.stp" or maybe even "http://url.com/file.stp" 
	anchor = URI.substr(poundpos + 1);	//anchor contains something like "item1234"		
	return 0;
}

int ResolveRRU(RoseRefUsage * rru,RoseObject * obj)
{
	if (rru == NULL) return -1;
	//std::cout << "\t" << rru->user_att()->name() << ", id: " << rru->user()->entity_id() << std::endl;

	if (rru->user_att()->isSelect()) {

		RoseDomain * selectdomain = rru->user_att()->slotDomain();
		RoseObject * sel = rru->user()->design()->pnewInstance(selectdomain);
		rru->user()->putObject(
			sel,
			rru->user_att(),
			rru->user_idx()
			);
		rose_put_nested_object((RoseUnion*)sel, obj);
	}
	if (rru->user_att()->isa(ROSE_DOMAIN(RoseReference))) return 0;
	else{
		rru->user()->putObject(obj, rru->user_att(), rru->user_idx());	//Replace any object attributes that point to the reference. Now they point to the object we moved from the child.
	}
	return 0;
}
