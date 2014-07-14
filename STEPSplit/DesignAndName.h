#pragma once
#include <map>
#include <vector>
#include <string>
#include <rose.h>
#include <ARM.h>

//Used so that the designs can have unique names in memory, but repetitive names in output files. This stops errors from repeat usage of PMI.stp, master.stp, etc.
int DesignNumber = 0;
//Every design has a number, now we make a list of what numbers go with what names so that we can find things by name later (have to check directories)
std::map<std::string, std::vector<int>> nametodesign;
class DesignAndName
{
	RoseDesign * design;
	std::string name;
public:
	DesignAndName() { design = nullptr; name = "";};
	DesignAndName(std::string inname, std::string path) { Find(inname, path); };
	DesignAndName * Find(std::string inname, std::string path) //Given a name and a path, finds the design in memory with the matching info- if any. If none, it makes a new one.
	{
		RoseDesign * des;
		for (auto i : nametodesign[inname])
		{
			des = ROSE.findDesignInWorkspace(std::to_string(i).c_str());
			if (nullptr != des)
			{
				std::string checkpath(des->fileDirectory());
				if (path != checkpath) continue;
				design = des;
				name = inname;
				return this;
			}
		}
		//If we are here, the desired design was not in workspace. Open it and return the new design.
		char last = path.back();
		if (last != '/'&&last != '\\')
			path.push_back('\\');
		des = ROSE.findDesign((path + inname).c_str());
		if (nullptr == des)
		{
			des = ROSE.newDesign((path + inname).c_str());
		}
		des->name(std::to_string(DesignNumber++).c_str());	//Give the design a unique number.
		nametodesign[inname].push_back(DesignNumber);			//Add the newly made design to the list of designs with this name
		design = des;
//		if (inname.find_last_of('.')) name = inname.substr(0, inname.find_last_of('.'));
		name = inname;
		return this;
	}
	RoseDesign* GetDesign() { return design; };
	std::string GetName() { return name; };
	void Save()
	{
		auto tmp = design->name();
		if (name.find_last_of('.')!=std::string::npos)
			design->name((name.substr(0, name.find_last_of('.'))).c_str());
		else
			design->name(name.c_str());
		ARMsave(design);
		design->name(tmp);
	}
};
