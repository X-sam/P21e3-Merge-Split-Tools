#pragma once
#include <map>
#include <vector>
#include <string>
#include <rose.h>


//Used so that the designs can have unique names in memory, but repetitive names in output files. This stops errors from repeat usage of PMI.stp, master.stp, etc.
int DesignNumber = 0;
struct RoseDesignAndName
{
	RoseDesign * design;
	std::string name;
};
//Every design has a number, now we make a list of what numbers go with what names so that we can find things by name later (have to check directories)
std::map<std::string, std::vector<int>> nametodesign;
//Given a name and a path, finds the design in memory with the matching info- if any. If none, it makes a new one.
RoseDesignAndName FindDesign(std::string name, std::string path)
{
	RoseDesign * des;
	for (auto i : nametodesign[name])
	{
		des = ROSE.findDesignInWorkspace(std::to_string(i).c_str());
		if (nullptr != des)
		{
			std::string checkpath(des->path());
			if (path != checkpath) continue;
			RoseDesignAndName a;
			a.design = des;
			a.name = name;
			return a;
		}
	}
	//If we are here, the desired design was not in workspace. Open it and return the new design.
	char last = path.back();
	if (last != '/'&&last != '\\')
		path.push_back('\\');
	des = ROSE.findDesign((path + name).c_str());
	des->name(std::to_string(DesignNumber++).c_str());	//Give the design a unique number.
	nametodesign[name].push_back(DesignNumber);			//Add the newly made design to the list of designs with this name
	RoseDesignAndName a;
	a.design = des;
	a.name = name;
	return a;
}
