//Scan.h
//Declares a URIManager class, which is attached to objects when we move them so we know what reference they should point to.
//Adapted from code by Dave Loffredo

#pragma once
#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>

#include <vector>
#include <string>
#include <map>
#include <set>

//Every manager has the same find and make methods, just getting and returning their own specific type. 
//Here we make a common parent class with templating so we can inherit from it in each of our managers, instead of writing a new find/make method for each one.
template <typename T>
class FindMake
{
public:
	static T* find(RoseObject * obj)
	{
		return (T*)(obj ? obj->find_manager(T::type()) : 0);
	}

	static T * make(RoseObject * obj)
	{
		T* mgr = find(obj);
		if (!mgr) {
			mgr = new T;
			obj->add_manager(mgr);
		}
		return mgr;
	}
};
class MyURIManager : public RoseManager,public FindMake<MyURIManager> {
protected:
	RoseReference * 	f_ref;
	RoseReference *		real_ref;

public:
	MyURIManager() : f_ref(0) {}

	ROSE_DECLARE_MANAGER_COMMON();

	RoseReference * should_go_to_uri()			{ return f_ref; }
	void should_go_to_uri(RoseReference * r)		{ f_ref = r; }
	
	RoseReference * should_point_to()				{ return real_ref; }
	void should_point_to(RoseReference * d)			{ real_ref = d; }

	//static MyURIManager * find<MyURIManager>;
	//static MyURIManager * make<MyURIManager>;
};

void update_uri_forwarding(RoseDesign * design);


//MoveManager holds a list of the names of files where we want an object to go.
class MoveManager : public RoseManager, public FindMake<MoveManager> {
protected:
	std::set<std::string> outfiles;
public:

	ROSE_DECLARE_MANAGER_COMMON();

	void addfile(std::string file) { outfiles.insert(file); };
	std::set<std::string> getfiles() { return outfiles; };

	//static MoveManager * find<MoveManager>;
	//static MoveManager * make<MoveManager>;
};

//ReferenceManager is used by copier function to track which objects reference this object.
//When copier copies an object, it checks this manager for any objects in the new design which reference the copied object, and resolves their references accordingly.
class ReferenceManager : public RoseManager, public FindMake<ReferenceManager>
{
protected:
	std::map<RoseDesign *, std::vector<RoseObject *>> references;
public:

	ROSE_DECLARE_MANAGER_COMMON();

	std::vector<RoseObject *> getreferences(RoseDesign *des) { return references[des]; };
	void addreference(RoseDesign *des, RoseObject * referencer) { references[des].push_back(referencer); };

	//static ReferenceManager * find<ReferenceManager>;
	//static ReferenceManager * make<ReferenceManager>;
};

//CopyManager contains a map, keyed by a rosedesign, which contains all the RoseObjects which are copies of the object this is attached to.
//If the object this is attached to is a copy, it contains a pointer to the object which it was copied from. It doesn't care if that item is itself a copy, so user should check for that.
class CopyManager : public RoseManager, public FindMake<CopyManager>
{
protected:
	std::map<RoseDesign *, RoseObject *> copies;	//empty if this is a copy. If the value at an index is null, then there is no copy of this item in that design.
	RoseObject * origin;	//Pointer to the object which the object this is attached to is a copy of. NULL if this is not a copy.
public:
	
	ROSE_DECLARE_MANAGER_COMMON();

	RoseObject * GetCopy(RoseDesign* in) { return copies[in]; };
	void AddCopy(RoseDesign *in, RoseObject * copy) { copies[in] = copy; };
	//static CopyManager * find(RoseObject *);
	//static CopyManager * find<CopyManager>;
	//static CopyManager * make<CopyManager>;
};