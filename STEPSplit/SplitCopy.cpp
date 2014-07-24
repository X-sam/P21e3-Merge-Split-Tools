#include "SplitCopy.h"
#include "ROSERange.h"
#include "custommanagers.h"
#include <map>
#include <string>


RoseObject *copy(RoseObject *obj, RoseDesign * out);
void AddReferenceMgr(RoseObject* object, RoseAttribute * att, unsigned idx, RoseObject *user);
void ParseAtt(void (callback)(RoseObject *, RoseAttribute *, unsigned, void *), void *userdata, RoseObject * obj, RoseAttribute * att, unsigned idx = 0u);

struct RRMdata
{
	RoseObject * original;
	RoseObject * copy;
};

void ResolveRefMgr(RoseObject* object, RoseAttribute * att, unsigned idx, RRMdata *user);

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
		}
	}
	for (auto i : outdesigns)
	{
		std::cout << "Saving Design: " << i.first <<'\n';
		auto lastslash = i.first.find_last_of('/');
		auto dirstr = i.first.substr(0, lastslash);
		auto fname = i.first.substr(lastslash+1, i.first.size() - lastslash-1);
		i.second->fileDirectory(dirstr.c_str());
		i.second->name(fname.c_str());
		if (!rose_dir_exists(dirstr.c_str()))
			rose_mkdir(dirstr.c_str());
		i.second->save();
	}
	return EXIT_SUCCESS;
}

//Custom copy function which manages inter-object relations across multiple calls.
RoseObject *copy(RoseObject *obj, RoseDesign * out)
{
	auto mgr = CopyManager::make(obj);	//Returns existant manager or makes a new one.
	if (mgr->GetCopy(out)) return mgr->GetCopy(out);	//Object already has copy in given design.
	auto outobj = obj->copy(out);	//copy the object to the given design. Get the copied object that we may parse its children and inform them of our existence.
	mgr->AddCopy(out, outobj);
	for (auto i = 0u, sz = outobj->attributes()->size(); i < sz; i++)	//Check all the attributes. if they have copies in our current design, then we change the attribute to point to the local object. Otherwise, mark the remote object as being referenced by us and deal with it later.
	{
		auto child = outobj->attributes()->get(i);
		if (child->isSimple()) continue;
		auto fp = (void(*)(RoseObject*,RoseAttribute *,unsigned, void*))&AddReferenceMgr;
		ParseAtt(fp,outobj,outobj,child);
	}
	auto referenced = ReferenceManager::find(obj);	//Finally we find anything which referenced the original object in the copied object's design, and resolve the refererences accordingly.
	if (nullptr == referenced) return outobj;
	auto listofreferenced = referenced->getreferences(out);
	for (auto i : listofreferenced)
	{
		for (auto j = 0u, sz = i->attributes()->size(); j < sz; j++)
		{
			auto fp = (void(*)(RoseObject*, RoseAttribute*, unsigned, void*)) &ResolveRefMgr;
			struct RRMdata userdata;
			userdata.original = obj;
			userdata.copy = outobj;
			ParseAtt(fp, &userdata, i, i->attributes()->get(j));
		}
	}
	return outobj;
}


//Checks if the attribute has a copy in the copied object's design, if so, resolves the attribute to point to that object.
//Otherwise, adds(or appends to) a reference manager to the attributed object with the copied object.

void AddReferenceMgr(RoseObject* object,RoseAttribute * att,unsigned idx,RoseObject *user)
{
	auto usedobject = object->getObject(att,idx);
	if (nullptr == usedobject) return;	//There ain't nothin there, boss.
	if (usedobject->design() == user->design()) return;	//the attribute is in fact currently IN our design!
	auto copycheck = CopyManager::find(usedobject);
	if (nullptr!=copycheck)
	{
		auto copyofatt = copycheck->GetCopy(user->design());
		if (nullptr != copyofatt)
		{
			object->putObject(copyofatt, att, idx);
			return;
		}
	}
	auto a = ReferenceManager::make(usedobject);
	a->addreference(user->design(), user);
	return;
}

void ResolveRefMgr(RoseObject* object, RoseAttribute * att, unsigned idx, RRMdata *user)
{
	auto obj = object->getObject(att, idx);
	if (obj == user->original)
	{
		object->putObject(user->copy,att, idx);
	}
	return;
}


//Given a rose object, will call given function on the underlying entity(s), regardless of original objects type- if the given object is a nested aggregate, for example, it will find any underlying objects and call the given function on the found objects.  
void ParseAtt(void (callback)(RoseObject *,RoseAttribute *,unsigned,void *),void *userdata, RoseObject * obj, RoseAttribute * att,unsigned idx)
{
	if (nullptr == att ||nullptr ==obj)
		return;
	if (att->isSimple())
		return;
	if (att->isEntity())
		return callback(obj, att, idx, userdata);
	RoseObject * val = obj->getObject(att, idx);
	if (nullptr==val) return;
	if (val->isa(ROSE_DOMAIN(RoseAggregate)))
	{
		RoseAttribute * agg_att = val->getAttribute();
		for (auto i = 0u, sz = obj->size(); i < sz; i++)
		{
			ParseAtt(callback, userdata, val, agg_att, i);
		}
	}
	else if (val->isa(ROSE_DOMAIN(RoseUnion)))
	{
		RoseAttribute * unionatt = val->getAttribute();
		ParseAtt(callback,userdata,val, unionatt, 0);
	}
	return;
}