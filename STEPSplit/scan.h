//Scan.h
//Declares a URIManager class, which is attached to objects when we move them so we know what reference they should point to.
//Adapted from code by Dave Loffredo

#ifndef _scan_h_
#define _scan_h_
#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>

#include <vector>
#include <string>
#include <map>

template <typename T>
T* find(RoseObject * obj)
{
	return (T*)(obj ? obj->find_manager(type()) : 0);
}

template <typename T>
T * make(RoseObject * obj)
{
	T* mgr = T::find(obj);
	if (!mgr) {
		mgr = new T;
		obj->add_manager(mgr);
	}
	return mgr;
}

class MyURIManager : public RoseManager {
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

	static MyURIManager * find(RoseObject *);
	static MyURIManager * make(RoseObject *);
};

void update_uri_forwarding(RoseDesign * design);


//MoveManager holds a list of the names of files where we want an object to go, as well as a marker to indicate whether an item is referenced by something outside of this file.
class MoveManager : public RoseManager {
protected:
	std::vector<std::string> outfiles;
	bool isreferenced=false;
public:

	ROSE_DECLARE_MANAGER_COMMON();

	void addfile(std::string file) { outfiles.push_back(file); };
	std::vector<std::string> getfiles() { return outfiles; };

	void setreferenced() { isreferenced = true; };
	bool getreferenced() { return isreferenced; };

	static MoveManager * find(RoseObject *);
	static MoveManager * make(RoseObject *);
};

//ReferenceManager is used by copier function to track which objects reference this object.
//When copier copies an object, it checks this manager for any objects in the new design which reference the copied object, and resolves their references accordingly.
class ReferenceManager : public RoseManager
{
protected:
	std::map<RoseDesign *, std::vector<RoseObject *>> references;
public:

	ROSE_DECLARE_MANAGER_COMMON();

	std::vector<RoseObject *> getreferences(RoseDesign *des) { return references[des]; };
	void addreference(RoseDesign *des, RoseObject * referencer) { references[des].push_back(referencer); };

	static ReferenceManager * find(RoseObject *);
	static ReferenceManager * make(RoseObject *);
};

//CopyManager contains a map, keyed by a rosedesign, which contains all the RoseObjects which are copies of the object this is attached to.
//If the object this is attached to is a copy, it contains a pointer to the object which it was copied from. It doesn't care if that item is itself a copy, so user should check for that.
class CopyManager : public RoseManager
{
protected:
	std::map<RoseDesign *, RoseObject *> copies;	//empty if this is a copy. If the value at an index is null, then there is no copy of this item in that design.
	RoseObject * origin;	//Pointer to the object which the object this is attached to is a copy of. NULL if this is not a copy.
public:
	
	ROSE_DECLARE_MANAGER_COMMON();

	//static CopyManager * find(RoseObject *);
	static CopyManager * find(RoseObject *);
	static CopyManager * make(RoseObject *);// *make(RoseObject *);

};


#endif