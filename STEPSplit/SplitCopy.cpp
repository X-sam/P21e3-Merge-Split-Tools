#include "SplitCopy.h"
#include "ROSERange.h"
#include "custommanagers.h"
#include <map>
#include <string>


void copy(RoseObject *obj, RoseDesign * out);

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
		std::cout << "Design: " << i.first <<'\n';
	}
	return EXIT_SUCCESS;
}

//Custom copy function which manages inter-object relations across multiple calls.
void copy(RoseObject *obj, RoseDesign * out)
{
	auto mgr = CopyManager::make(obj);	//Returns existant manager or makes a new one.
	if (mgr->GetCopy(out)) return;	//Object already has copy in given design.
	auto outobj = obj->copy(out);	//copy the object to the given design. Get the copied object that we may parse its children and inform them of our existence.
	mgr->AddCopy(out, outobj);
	for (auto i = 0u, sz = outobj->attributes()->size(); i < sz; i++)
	{
		auto child = outobj->attributes()->get(i);
		if (child->isSimple()) continue;
		auto fp = (void*(*)(RoseObject*, void*))&AddReferenceMgr;
		AggSelectOrEntity(child, fp,obj);	//Attach a reference to the child so when we copy it it resolves it.
	}

}


//Given an object which is an attribute of a recently copied object, and the copied object itself,
//Checks if the attribute has a copy in the copied object's design, if so, resolves the attribute to point to that object.
//Otherwise, adds(or appends to) a reference manager to the attributed object with the copied object.

void AddReferenceMgr(RoseObject* usedobject,RoseObject * user)
{
	auto copycheck = CopyManager::find(usedobject);
	auto copyofatt = copycheck->GetCopy(user->design());
	auto fp = (void*(*)(RoseObject *, void *)) changeref;
	for (auto i = 0u, sz = user->attributes()->size(); i < sz; i++)
	{
		auto j = user->attributes()->get(i);
		if (j->isSimple()) continue;
		int * q = (int*)AggSelectOrEntity(j, fp, usedobject);
		if (q == nullptr) continue;
		if (*q == 1)
			user->attributes()->putObject(copyofatt, i);
	}
	auto a = ReferenceManager::make(usedobject);
	a->addreference(user->design(), user);
	return;
}

int *changeref(RoseObject *original, RoseObject *searchobject)
{
	if (original == searchobject)
	{
		int i = 1;
		return &i;
	}
	int i = 0;
	return &i;
}

//Given a rose object, will call given function on the underlying entity(s), regardless of original objects type- if the given object is a nested aggregate, for example, it will find any underlying objects and call the given function on the found objects.  
void * AggSelectOrEntity(RoseObject * obj,void *(*cbfunction)(RoseObject*,void *userdata),void *userdata=nullptr)
{
	if (obj->isa(ROSE_DOMAIN(RoseUnion)))
	{
		return cbfunction(rose_get_nested_object(ROSE_CAST(RoseUnion,obj)),userdata);
	}
	if (obj->isa(ROSE_DOMAIN(RoseAggregate)))
	{
		if (obj->attributes()->get(0)->isSimple) return;	//This aggregate has no objects in it.
		for (auto i = 0u, sz = obj->size(); i < sz; i++)
		{
			AggSelectOrEntity(obj->getObject(i),cbfunction,userdata);
		}
		return;
	}
	if (obj->isa(ROSE_DOMAIN(RoseStructure)))
	{
		return cbfunction(obj,userdata);
	}
	return nullptr;
}

void checkagg(RoseAttribute * att,unsigned idx=0u)
{
	if (att->isSelect())
	{
		auto a =att->getObject();
		checkagg(a);
	}
	if (att->isa(ROSE_DOMAIN(RoseAggregate)))
	{
		if (att->attributes()->get(0)->isSimple) return;	//This aggregate has no objects in it.
		for (auto i = 0u, sz = att->size(); i < sz; i++)
		{
			checkagg(att->getObject(i), i);
		}
		return;
	}
	if (att->isa(ROSE_DOMAIN(RoseStructure)))
	{
		//do stuff to it. we know its position
	}
	return;
}