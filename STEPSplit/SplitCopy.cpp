#include "SplitCopy.h"
#include "ROSERange.h"
#include "custommanagers.h"
#include <map>
#include <string>

RoseObject *copy(RoseObject *obj, RoseDesign * out);

void AddReferenceMgr(RoseObject* usedobject, RoseObject *user, RoseAttribute * att, unsigned idx = 0u);
void ManageAttributes(RoseObject * obj);
void RedirectReferences(RoseObject * original, RoseObject *copy);
int MakeAnchors(RoseObject *obj);

struct RRMdata
{
	RoseObject * original;
	RoseObject * copy;
};

void ResolveRefMgr(RoseObject* parent, RRMdata *childobjs, RoseAttribute * att, unsigned idx = 0u);
int Split(RoseDesign *des)
{
	std::map<std::string, RoseDesign*> outdesigns;
	for (auto &i : ROSE_RANGE(RoseObject, des))
	{
		auto mgr = MoveManager::find(&i);
		if (nullptr != mgr)
		{
			auto files = mgr->getfiles();
			auto itr = files.begin();
			auto fname = *itr;
			auto outdesign = outdesigns[fname];
			if (outdesign == nullptr)
			{
				outdesign = ROSE.newDesign(fname.c_str());
				outdesigns[fname]=outdesign;
			}
			i.move(outdesign);
			itr++;
			while(itr!=files.end())	//First time it is a move, every subsequent time it is a copy. If there's only one output file, then this loop shall not be evaluated.
			{
				fname = *itr;
				auto outdesign = outdesigns[fname];
				if (outdesign == nullptr)
				{
					outdesign = ROSE.newDesign(fname.c_str());
					outdesigns[fname] = outdesign;
				}
				copy(&i,outdesign);
				itr++;
			}
			ManageAttributes(&i);
			RedirectReferences(&i, &i);
			MakeAnchors(&i);
		}
	}

	for (auto i : outdesigns)
	{
		if (nullptr == i.second) continue;
		auto name = i.first;
		for (auto &j : name)
		{
			if (j == '\\') j = '/';
		}
		std::cout << "Saving Design: " << name<<'\n';
		auto lastslash = name.find_last_of('/');
		auto dirstr = name.substr(0, lastslash);
		auto fname = name.substr(lastslash+1, name.size() - lastslash-1);
		if (fname.find('.') != std::string::npos)
			fname = fname.substr(0, fname.find('.'));	//Remove the '.stp' from the filename
		i.second->fileDirectory(dirstr.c_str());
		i.second->name(fname.c_str());
		if (!rose_dir_exists(dirstr.c_str()))
		{
			auto nextslash = dirstr.find('/');
			while (nextslash != std::string::npos)
			{
				auto substr = dirstr.substr(0, nextslash);
				if (!rose_dir_exists(substr.c_str()))
					rose_mkdir(substr.c_str());
				nextslash = dirstr.find('/', nextslash + 1);
			}
			rose_mkdir(dirstr.c_str());
		}
		i.second->save();
		delete i.second;
	}
	return EXIT_SUCCESS;
}

//Make any anchors required for an object.
int MakeAnchors(RoseObject * obj)
{
	auto anchormgr = AnchorManager::find(obj);
	if (nullptr == anchormgr) return 0;
	auto anchorcount = 0u;
	auto anchorlist = anchormgr->GetAnchors();
	for (auto i : anchorlist)
	{
		obj->design()->addName(i.c_str(), obj);
		anchorcount++;
	}
	return anchorcount;
}

//Custom copy function which manages inter-object relations across multiple calls.
RoseObject *copy(RoseObject *obj, RoseDesign * out)
{
	auto mgr = CopyManager::make(obj);	//Returns existant manager or makes a new one.
	if (mgr->GetCopy(out)) return mgr->GetCopy(out);	//Object already has copy in given design.
	auto outobj = obj->copy(out);	//copy the object to the given design. Get the copied object that we may parse its children and inform them of our existence.
	mgr->AddCopy(out, outobj);
	ManageAttributes(outobj);
	RedirectReferences(obj, outobj);
	return outobj;
}

//Check all the attributes. if they have copies in our current design, then we change the attribute to point to the local object. Otherwise, mark the remote object as being referenced by us and deal with it later.
void ManageAttributes(RoseObject * obj)
{
	if (obj->isa(ROSE_DOMAIN(RoseUnion)))
	{ 
		auto att = obj->getAttribute();
		if (att->isObject())
		{
			auto child = obj->getObject(att);
			if (nullptr != child)
				AddReferenceMgr(child, obj,att);
		}
	}
	else if (obj->isa(ROSE_DOMAIN(RoseAggregate)))
	{
		auto att = obj->getAttribute();
		if (att->isObject())
		{
			for (auto i = 0u, sz = obj->size(); i < sz; i++)
			{
				auto child = obj->getObject(i);
				if (nullptr != child)
					AddReferenceMgr(child, obj,att,i);
			}
		}
	}
	else if (obj->isa(ROSE_DOMAIN(RoseStructure)))
	{
		for (auto i = 0u, sz = obj->attributes()->size(); i < sz; i++)
		{
			auto att= obj->attributes()->get(i);
			//Figure out if we should ignore the attribute because it isn't really an object underneath.
			if (att->isSimple()) continue;
			auto child = obj->getObject(att);
			if (nullptr!=child)
				AddReferenceMgr(child, obj, att);
		}
	}
	return;
}

void RedirectReferences(RoseObject * original, RoseObject *copy)
{
	auto referenced = ReferenceManager::find(original);	//find anything which referenced the original object in the copied object's design, and resolve the refererences accordingly.
	if (nullptr == referenced) return;
	auto listofreferenced = referenced->getreferences(copy->design());
	bool printme=false;
	struct RRMdata userdata;
	userdata.original = original;
	userdata.copy = copy;
	for (auto obj : listofreferenced)
	{
		if (obj->isa(ROSE_DOMAIN(RoseUnion)))
		{
			auto att = obj->getAttribute();
			if (att->isObject())
			{
				auto child = obj->getObject(att);
				if (nullptr != child)
					ResolveRefMgr(obj, &userdata, att);
			}
		}
		else if (obj->isa(ROSE_DOMAIN(RoseAggregate)))
		{
			auto att = obj->getAttribute();
			if (att->isObject())
			{
				for (auto i = 0u, sz = obj->size(); i < sz; i++)
				{
					auto child = obj->getObject(i);
					if (nullptr != child)
						ResolveRefMgr(obj, &userdata, att, i);
				}
			}
		}
		else if (obj->isa(ROSE_DOMAIN(RoseStructure)))
		{
			for (auto i = 0u, sz = obj->attributes()->size(); i < sz; i++)
			{
				auto att = obj->attributes()->get(i);
				//Figure out if we should ignore the attribute because it isn't really an object underneath.
				if (att->isSimple()) continue;
				auto child = obj->getObject(att);
				if (nullptr != child)
					ResolveRefMgr(obj, &userdata, att);
			}
		}
	}
	return;
}

//Checks if the attribute has a copy in the copied object's design, if so, resolves the attribute to point to that object.
//Otherwise, adds(or appends to) a reference manager to the attributed object with the copied object.

void AddReferenceMgr(RoseObject* child, RoseObject *parent,RoseAttribute * att,unsigned idx)
{
//	auto usedobject = object->getObject(att);
	if (nullptr == child) return;	//There ain't nothin there, boss.
	if (child->design() == parent->design()) return;	//the attribute is in fact currently IN our design!
	auto copycheck = CopyManager::find(child);
	if (nullptr!=copycheck)
	{
		auto copyofatt = copycheck->GetCopy(parent->design());
		if (nullptr != copyofatt)
		{
			parent->putObject(copyofatt, att,idx);
			return;
		}
	}
	auto refmgr = ReferenceManager::make(child);
	refmgr->addreference(parent->design(), parent);
	return;
}

void ResolveRefMgr(RoseObject* parent, RRMdata *childobjs, RoseAttribute * att, unsigned idx)
{
	auto child = parent->getObject(att,idx);
	if (child == childobjs->original)
	{
		parent->putObject(childobjs->copy,att,idx);
	}
	return;
}