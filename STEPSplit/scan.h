#ifndef _scan_h_
#define _scan_h_
#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>

class MyURIManager : public RoseManager {
protected:
	RoseReference * 	f_ref;
	RoseDesign *		ref_des;

public:
	MyURIManager() : f_ref(0) {}

	ROSE_DECLARE_MANAGER_COMMON();

	RoseReference * should_go_to_uri()			{ return f_ref; }
	RoseDesign * should_go_in_des()				{ return ref_des; }
	void should_go_to_uri(RoseReference * r)		{ f_ref = r; }
	void should_go_in_des(RoseDesign * d)			{ ref_des = d; }

	static MyURIManager * find(RoseObject *);
	static MyURIManager * make(RoseObject *);
};

void update_uri_forwarding(RoseDesign * design);


class MyPDManager : public RoseManager{
private:
	RoseDesign*						childDes;
	stp_product_definition*			childPD;
	RoseReference*					real_ref;
public:

	ROSE_DECLARE_MANAGER_COMMON();

	void setRef(RoseReference * r)		{ real_ref = r; }
	RoseReference * should_point_to()	{ return real_ref; }

	void hasChild(stp_product_definition * c)	{ childPD = c; }
	void hasChildIn(RoseDesign * d)	{ childDes = d; }

	MyPDManager() { childPD = NULL; childDes = NULL; real_ref = NULL;}

	static MyPDManager * find(RoseObject * nauo);
	static MyPDManager * make(RoseObject *);
};


#endif