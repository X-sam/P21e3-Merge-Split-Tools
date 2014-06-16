#ifndef _scan_h_
#define _scan_h_
#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>

class MyURIManager : public RoseManager {
protected:
	RoseReference * 	f_ref;

public:
	MyURIManager() : f_ref(0) {}

	ROSE_DECLARE_MANAGER_COMMON();

	RoseReference * should_go_to_uri()			{ return f_ref; }
	void should_go_to_uri(RoseReference * r)		{ f_ref = r; }

	static MyURIManager * find(RoseObject *);
	static MyURIManager * make(RoseObject *);
};

void update_uri_forwarding(RoseDesign * design);


class MyPDManager : public RoseManager{
public:
	RoseDesign* childDes;
	stp_product_definition* childPD;

	MyPDManager() { childPD = NULL; childDes = NULL; }

	static MyPDManager * find(stp_next_assembly_usage_occurrence * nauo)

	ROSE_DECLARE_MANAGER_COMMON();
};


#endif