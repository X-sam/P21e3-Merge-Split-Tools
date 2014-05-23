#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <string>
#include <map>
#include <iostream>

std::map<std::string, int> counts;
int main(char argc, char* argv[])
{
	if (argc < 2) return EXIT_FAILURE;
	stplib_init();	// initialize merged cad library
	RoseObject * obj;
	RoseCursor curser;
	RoseDesign * des = ROSE.useDesign(argv[1]);
	curser.traverse(des);
	curser.domain(ROSE_DOMAIN(RoseStructure));
	while (obj = curser.next())
	{
		counts[obj->domain()->name()]++;
	}
	for (auto i : counts)
	{
		std::cout << i.first << "- " << i.second <<std::endl;
	}
}