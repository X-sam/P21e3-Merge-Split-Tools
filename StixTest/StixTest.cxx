//Samson Made this code.
//5/23/14

#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <stix.h>
#include <string>
#include <map>
#include <iostream>
#include <cstdio>
#include <vector>

#pragma comment(lib, "stpcad_stix.lib")

int main(int argc, char* argv[])
{
	stplib_init();	// initialize merged cad library
//    rose_p28_init();	// support xml read/write
	FILE *out;
	out=fopen("log.txt","w");
	ROSE.error_reporter()->error_file(out);
	ROSE.quiet(0);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.

	/* Create a RoseDesign to hold the output data*/

	RoseDesign * master = ROSE.useDesign("as1-ac-214.stp");
	if (!master)
	{
		std::cerr << "Error opening input file" << std::endl;
		return EXIT_FAILURE;
	}
	master->saveAs("as1-ac-214-copy.stp");
	RoseDesign * design = ROSE.useDesign("as1-ac-214-copy.stp");
	if (!design)
	{
		std::cerr << "Error opening output file" << std::endl;
		return EXIT_FAILURE;
	}

	stix_tag_asms(design);
	StpAsmShapeRepVec * roots = new StpAsmShapeRepVec;
	stix_find_root_shapes(roots, design);
	for (int i = 0; i < roots->size();i++)
	{
		std::cout <<roots->get(i)->className() <<std::endl;
	}
	delete roots;
	std::string syscmd("mkdir ");
	syscmd.append("testdir");
	if (system(syscmd.data()) != 0)
	{
		std::cout <<"Error opening file\n";
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
