//Scan.h
//Declares a URIManager class, which is attached to objects when we move them so we know what reference they should point to.
//Adapted from code by Dave Loffredo

#ifndef _scan_h_
#define _scan_h_
#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>

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

#endif