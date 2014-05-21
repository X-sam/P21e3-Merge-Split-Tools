//Chris Made this code.
//5/20/14

#include <rose.h>
#include <rose_p28.h>
#include <stp_schema.h>
#include <string>
#include <map>
#include <iostream>
#include <cstdio>
std::map<std::string, int> translated;

//Takes object, puts itself and all of its children in the output design
int PutItem(RoseObject *obj, RoseDesign* output)
{
	obj->move(output, INT_MAX);
	return 1;
}

//Takes in a reference and a design. 
//Finds the file referenced, opens it, gets the referenced line and all of its children, puts them in the output design, removes the reference in output (if found)
int AddItem(RoseReference *ref, RoseDesign* output)
{
	std::string URI(ref->uri());
	//URI looks like "filename.stp#item1234"
	//Split that into "filename.stp" and "item1234"
	int poundpos = URI.find_first_of('#');
	std::string reffile = URI.substr(0, poundpos);	//reffile contains "filename.stp"
	std::string anchor = URI.substr(poundpos+1);	//anchor contains "item1234"		

	//Now we can open the file and find the specific item referenced by the anchor.
	RoseDesign * child = ROSE.findDesign(reffile.c_str());	//Child file opened as a new design
	RoseObject *obj = child->findObject(anchor.c_str());	//Get the object associated with the anchor

	if (-1 == PutItem(obj, output))	//Move the object (which is currently in the child file) into the new output file.
	{
		//Something went wrong. This can't happen at the present, but in theory a change to PutItem could allow for it.
		return -1;
	}
	//Now that we have moved the item into the new domain,
	//We need to update the items that use the references.
	RoseRefUsage *rru = ref->usage();	//rru is a linked list of all the objects that use ref
	do
	{
		rru->user()->putObject(obj, rru->user_att(), rru->user_idx());	//Replace any object attributes that point to the reference. Now they point to the object we moved from the child.
	} while (rru = rru->next_for_ref());	//Do this for anything that uses the reference.

	return 0;
}

bool isOrphan(RoseObject * child, ListOfRoseObject * children){
	ListOfRoseObject parents;	//if child's parent(s) not in children return false, else return true

	RoseDomain *    search_domain;
	RoseAttribute * search_att;

	search_domain = ROSE_DOMAIN(RoseObject);
	search_att = search_domain->findTypeAttribute("owner");
	unsigned int k, sz;
	child->usedin(search_domain, search_att, &parents);
	for (k = 0, sz = parents.size(); k < sz; k++){
		RoseObject * parent = parents.get(k);
		if (!rose_is_marked(parent)){ //if parent is not marked then it is not a child of the object being split and needs to stay
			rose_mark_clear(child);
			return false;
		}
	}
	
	return true;
}

int PutOut(RoseObject * obj, const unsigned int &nthObj){ //(product, master rose design) for splitting the code
	stp_product * prod = ROSE_CAST(stp_product, obj);
	RoseDesign * ProdOut = new RoseDesign(prod->id());
	obj->copy(ProdOut, INT_MAX);	//scan & remove files from master as needed 
	ListOfRoseObject *children = new ListOfRoseObject;
	obj->findObjects(children, INT_MAX, ROSE_FALSE);	//children will be filled with obj and all of its children
	rose_mark_begin();
	rose_mark_set(obj);
	for (unsigned int i = 0; i < children->size(); i++){ //mark all children for orphan check
		RoseObject *child = children->get(i);
		if (rose_is_marked(child)){ continue; }
		else{ rose_mark_set(child); }
	}
	std::cout << "Children to parse: " << children->size() <<std::endl;
	for (unsigned int i = 0; i < children->size(); i++)	{  //scan children to find parents, if orphan delete from master
		RoseObject *child = children->get(i);
		if (isOrphan(child, children)){ //if: child dose not have parents outside of children 
			std::cout << "Moving " << child->entity_id() << " to trash\n";
			rose_move_to_trash(child);
		}
		else{ continue; }
	}
	std::string refURI = std::string(prod->id() + std::string(".stp#") + prod->id() + std::to_string(nthObj)); //uri for created reference to prod/obj
	ProdOut->addName((prod->id() + std::to_string(nthObj)).c_str(), prod); //add anchor to ProdOut

	//make reference to prodout file from master
	RoseReference *ref = rose_make_ref(obj->design(), refURI.c_str()); 
	rose_put_ref(ref, obj, prod->id()); 
	ProdOut->save(); //save ProdOut as prod->id().stp

	rose_mark_end();
	
	//TODO:
	//-Make a list of places where obj (a product) is used in
	//-Use putobject to put the reference in the place of the old product info
	//-Put obj in trash
	//-Empty trash
	delete ProdOut;
	return 0;
}

int split(RoseDesign * master){		//, std::string type){
	//traverse to find obj that match type
	RoseCursor cursor;
	RoseObject * obj;
	unsigned int objCounter = 0;
	cursor.traverse(master);
	cursor.domain(ROSE_DOMAIN(stp_product));
	std::cout << cursor.size() << std::endl;
	while (obj = cursor.next()){
		//stp_product * prod = ROSE_CAST(stp_product, obj);
		PutOut(obj, objCounter);
		objCounter++;
	}
	master->save(); //save changes to master
	return 0;
}

int main(int argc, char* argv[])
{
    stplib_init();	// initialize merged cad library
//    rose_p28_init();	// support xml read/write
	FILE *out;
	out=fopen("log.txt","w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.
	
	/* Create a RoseDesign to hold the output data*/
	RoseDesign * origional = ROSE.useDesign("as1-ac-214.stp");	//TODO: Make this use Argv[1]
	origional->saveAs("SplitOutput.stp");
	RoseDesign * master = ROSE.useDesign("SplitOutput.stp");
	split(master);
	rose_empty_trash();
    return 0;
}