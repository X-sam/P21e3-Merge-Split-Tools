#ifndef _scan_h_
#define _scan_h_
#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <string>

class MyURIManager : public RoseManager {
protected:
	RoseReference * 	f_ref;
	RoseReference *		real_ref;
	int					index;

public:
	MyURIManager() : f_ref(0) {}

	ROSE_DECLARE_MANAGER_COMMON();

	RoseReference * should_go_to_uri()			{ return f_ref; }
	void should_go_to_uri(RoseReference * r)		{ f_ref = r; }
	
	RoseReference * should_point_to()				{ return real_ref; }
	void should_point_to(RoseReference * d)			{ real_ref = d; }

	void setIndex(int i)				{ index = i; }
	int getIndex()						{ return index; }

	static MyURIManager * find(RoseObject *);
	static MyURIManager * make(RoseObject *);
};

void update_uri_forwarding(RoseDesign * design);



class MyPDManager : public RoseManager{
private:
	RoseDesign*						childDes;
	stp_product_definition*			childPD;
	RoseReference*					real_ref = NULL;
	RoseObject*						anchored = NULL;
	std::string						anchorName;

public:

	ROSE_DECLARE_MANAGER_COMMON();

	void setRef(RoseReference * r)		{ real_ref = r; }
	RoseReference * should_point_to()	{ return real_ref; }

	void hasChild(stp_product_definition * c)	{ childPD = c; }
	void refisin(RoseDesign * d)				{ childDes = d; }
	RoseDesign * hasrefinit()					{ return childDes; }

	void setDst(RoseObject* a)		{ anchored = a; }
	RoseObject* getDstObj()		{ return anchored; }

	void nameAnchor(std::string s)		{ anchorName = s; }
	std::string getAnchorName()			{ return anchorName; }



	MyPDManager() { childPD = NULL; childDes = NULL; }

	static MyPDManager * find(RoseObject * nauo);
	static MyPDManager * make(RoseObject *);
};
#endif