//Makes two new designs, testRefAndAnchor & testAnchorAndData.
//Adds a point to testAnchorAndData, and an anchor which points to it.
//Adds a reference to testRefAndAnchor, and an anchor which points to it.
//Saves the files.
#include <rose.h>
#include <stp_schema.h>
#include <iostream>



int main(int argc, char* argv[])
{
	ROSE.quiet(1);	//Get rid of annoying ST-Dev output.
	stplib_init();	// initialize merged cad library
	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.

	RoseDesign * testAnchorAndData = ROSE.newDesign("testAnchorAndData");
	stp_cartesian_point *point =pnew stp_cartesian_point;
	point->name("TestPoint");
	point->coordinates()->add(1.01);
	point->coordinates()->add(2.01);
	point->coordinates()->add(3.01);
	testAnchorAndData->addName("TestAnchor", point);
	testAnchorAndData->save();

	RoseDesign * testRefAndAnchor = ROSE.newDesign("testRefAndAnchor");
	auto ref = rose_make_ref(testRefAndAnchor, "testAnchorAndData.stp#TestAnchor");		//Manually make the reference point to the other file.
	ref->entity_id(123);		//Just give it an arbitrary entity id so it doesn't end up as 0.
	testRefAndAnchor->addName("ParentAnchor", ref);	//Add an anchor which points to the reference.
	stp_circular_area *area = pnew stp_circular_area;	//Make something to test if the reference resolves properly
	area->name("testarea");
	area->radius(2.0);
	rose_put_ref(ref, area, "centre");
	testRefAndAnchor->save();
	return EXIT_SUCCESS;
}