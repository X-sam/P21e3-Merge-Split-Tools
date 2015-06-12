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
	DesignAndName() :design(nullptr), name("") {};
	DesignAndName(const std::string &path, const std::string &inname) : design(nullptr), name("")
	{ 
		Find(path,inname);
		if (design == nullptr)
		{
			NewDesign(path, inname);
		}
	};
	DesignAndName * Find(const std::string &inpath,const std::string &inname) //Given a name and a path, finds the design in memory with the matching info- if any.
	{
		auto path = SlashRegulize(inpath);
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
		return this;
	}
	DesignAndName Open(const std::string &inpath, const std::string &inname)
	{
		auto path = SlashRegulize(inpath);
		RoseDesign *des;

		des = ROSE.findDesign((path + inname).c_str());
		if (des != nullptr)
		{
			name = inname;
			design = des;
			design->name(std::to_string(DesignNumber).c_str());
			nametodesign[inname].push_back(DesignNumber++);
		}
		return *this;
	}
	DesignAndName NewDesign(std::string inpath,std::string inname)
	{
		auto path = SlashRegulize(inpath);
		auto des = ROSE.newDesign((path + inname).c_str());
		des->name(std::to_string(DesignNumber).c_str());	//Give the design a unique number.
		nametodesign[inname].push_back(DesignNumber++);			//Add the newly made design to the list of designs with this name
		design = des;
		//		if (inname.find_last_of('.')) name = inname.substr(0, inname.find_last_of('.'));
		name = inname;
		return *this;
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
private:
	std::string SlashRegulize(const std::string str)
	{
		std::string tmp(str);
		for (auto &i : tmp)
		{
			if (i == '/') i = '\\';
		}
		if (tmp.back()!= '\\')
			tmp.push_back('\\');
		return tmp;
	}
};
