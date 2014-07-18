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

#endif