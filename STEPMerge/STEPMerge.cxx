//Samson Made this code.
//5/19/14

#include <rose.h>
#include <stp_schema.h>
#include <string>
#include <map>
#include <iostream>
#include <cstdio>
#include <vector>
#include<windows.h>	//for url stuff
#include <direct.h>	//for chdir
#include <tchar.h>
#include <urlmon.h>	//for url stuff
#pragma comment(lib, "urlmon.lib")	//for url stuff
#pragma comment(lib,"wininet.lib")	//for url stuff

std::vector<std::string> downloaded;	//List of downloaded files so we don't dl them twice.
std::vector<std::string> options;
std::vector<std::string>blackorwhitelist;
bool blacklist = true;	//Default the blackorwhitelist to being a blacklist, of size 0. AKA all files allowed.

//Takes in a url and an output file name, goes online, downloads file, and puts it at location specified by output.
int getfromweb(std::string url, std::string out);

//Given a design, checks for references, if it finds any, opens the referenced design, checks for references, etc. Then resolves references from the bottom up.
int MoveAllReferences(RoseDesign *design, std::string workingdir);

//Takes object, puts itself and all of its children in the output design
int PutItem(RoseObject *obj, RoseDesign* output)
{
	RoseDesign * child = obj->design();
	if (child == output) return 0;	//If the design of object is the output design, we are already done.
	obj->move(output, INT_MAX, FALSE);
	return 1;
}

//Given a URI, extracts a file, anchor, and directory heirarchy to the file (if applicable)
//Returns 0 on success, -1 on unexpected failure, -2 on Download Failure. 
int URIParse(const std::string URI, std::string &filename, std::string &anchor, std::string &workingdir)
{
	//URI looks like "filename.stp#item1234"
	//Split that into "filename.stp" and "item1234"
	int poundpos = URI.find_first_of('#');
	filename = URI.substr(0, poundpos);	//reffile contains something like "filename.stp" or maybe "path/to/filename.stp" or maybe even "http://url.com/file.stp" 
	anchor = URI.substr(poundpos + 1);	//anchor contains something like "item1234"		
	//Figure out if it's a local file or not
	if (filename.find_first_of('/') || filename.find_first_of('\\'))	//not local if it has a slash. Don't tell me file names may contain '/' because they can't!
	{
		//figure out if it is a url
		if (filename.find("http://") != std::string::npos || filename.find("ftp://") != std::string::npos)
		{
			//It's a url so go get it from the web
			int lastslash = filename.find_last_of('/');
			std::string out(filename.begin() + lastslash + 1, filename.end());
			bool alreadyhavefile = false;
			for (auto i : downloaded)
			{
				if (i == out) alreadyhavefile = true;
			}
			if (!alreadyhavefile)
			{
				int webreturn = getfromweb(filename, out);
				if (webreturn != 0)
				{
					std::cerr << "Error Downloading file\n";
					return -2;
				}
				downloaded.push_back(out);	//We have the file so add it to the list of downloaded files.
			}
			filename = out;	//Set reffile to the newly downloaded file, so that we can import it and such.
		}
		else if (filename.find("..\\") != std::string::npos || filename.find("../") != std::string::npos)
		{
			std::cerr << "Relative pathing not supported\n";
			return -1;
		}
		auto nextbackslash = filename.find('\\', 0);	//Switch all of the backslashes to forwardslashes, for consistency's sake.
		while (nextbackslash != std::string::npos)
		{
			filename[nextbackslash] = '/';
			nextbackslash = filename.find('\\', nextbackslash);
		}
		auto lastslash = filename.find_last_of('/');
		workingdir += filename.substr(0, lastslash + 1);		//add the relative path to filedir
		filename = filename.substr(lastslash + 1);		//Remove path from filename, so now it should look like "file.stp"
	}
	return 0;
}

//Takes in a reference and a design. 
//Finds the file referenced, opens it, gets the referenced line and all of its children, puts them in the output design, removes the reference in output (if found)
//Returns:
//	0 on success, 
// -1 on unspecified failure, 
//	1 on References Blacklisted File
//	2 on Doesn't Reference Whitelisted File
// -2 on Download Failure
int AddItem(RoseReference *ref, RoseDesign* output,const std::string workingdir="./")
{
	std::string reffile, anchor;
	std::string filedir = workingdir;				//we need to know what directory the file is in, start at working dir and build the relative path in URIParse.
	int URIReturnValue = URIParse(ref->uri(), reffile, anchor, filedir); //Get the strings we need out of the URI.
	if (0 != URIReturnValue) return URIReturnValue;	
	//Now we can open the file and find the specific item referenced by the anchor.
	if (blacklist)
	{
		for (auto i : blackorwhitelist)
		{
			if (i == reffile)
			{
				return 1;	//Blacklisted file result
			}
		}
	}
	else    //We have a whitelist and need to see if the file is in the list
	{
		bool found = false;
		for (auto i : blackorwhitelist)	//if there's no list then this loop happens 0 times. Efficient.
		{
			if (i == reffile)
			{
				found = true;
				break;
			}
		}
		if (!found) return 2;	//Whitelisted file result
	}
	RoseDesign * child = ROSE.findDesignInWorkspace(reffile.substr(0,reffile.find('.')).c_str());	//check if file is in memory.
	reffile = filedir + reffile;
	if (child == NULL)
	{
		child = ROSE.findDesign(reffile.c_str());	//Child file opened as a new design
	}
	if (!child)
	{
		std::cout << "File " << reffile << "cannot be found.\n";
		return -1;	//file doesn't work for some reason.
	}

	RoseCursor curser;
	curser.traverse(child->reference_section());
	curser.domain(ROSE_DOMAIN(RoseReference));
	if (curser.size() > 0)
	{
		std::cout << "File " << child->name() << " has children.\n";
		MoveAllReferences(child, filedir);	//If the child has references, we resolve them before anything else.
	}
	RoseObject *obj = child->findObject(anchor.c_str());	//Get the object associated with the anchor
	if (!obj)
	{
		std::cout << "Anchor " << anchor << " not found in file: " << reffile <<'\n';
		return -1;	//Couldn't find the anchor.
	}
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
		if (rru == NULL) break;
		//std::cout << "\t" << rru->user_att()->name() << ", id: " << rru->user()->entity_id() << std::endl;

		if (rru->user_att()->isSelect()) {
			
			RoseDomain * selectdomain = rru->user_att()->slotDomain();
			RoseObject * sel = rru->user()->design()->pnewInstance(selectdomain);
			rru->user()->putObject(
				sel,
				rru->user_att(),
				rru->user_idx()
				);
			rose_put_nested_object((RoseUnion*)sel, obj);
		}
		if (rru->user_att()->isa(ROSE_DOMAIN(RoseReference))) continue;
		else{
			rru->user()->putObject(obj, rru->user_att(), rru->user_idx());	//Replace any object attributes that point to the reference. Now they point to the object we moved from the child.
		}
	} while (rru = rru->next_for_ref());	//Do this for anything that uses the reference.

	DictionaryOfRoseObject * anchors = child->nameTable(); //Check if the reference was used in the list of anchors.
	int anchorssize = anchors->listOfValues()->size();
	for (unsigned i = 0; i < anchorssize; i++)
	{
		//anchors->listOfValues() is the RoseObject pointed to by the anchor. If it is local, then the design will be child. 
		//We want to find the external ones that use the same reference as the one we resolved and change them.
		RoseObject * anch = anchors->listOfValues()->get(i);
		if (anch==obj)	
		{
			std::cout << anchors->listOfKeys()->get(i) << " is pointing to this thing we are at EID: " << anch->entity_id() <<"which is a " <<anch->className()<<'\n';
			anchors->add(anchors->listOfKeys()->get(i),obj);	//obj is the thing we resolved the reference to.
		}
	}
	//child->save();
	return 0;
}


int parsecmdline(int argc, char*argv[], std::string &infilename, std::string &outfilename)
{
	//TODO: make parser.
	infilename = argv[1];
	outfilename = argv[2];
	for (auto i = 3; i < argc; i++)
	{
		if (strcmp("-i", argv[i]))	//We have an ignore list
		{
			i++;
			while (i < argc)
			{
				blackorwhitelist.push_back(argv[i]);
			}
		}
		else if (strcmp("-a", argv[i]))	//We Have an allow list
		{
			blacklist = false;
			i++;
			while (i < argc)
			{
				if (strcmp("-i", argv[i]))
					blackorwhitelist.push_back(argv[i]);
				i++;
			}
		}
		else
		{
			std::cout << "Unknown Argument\n";
			return -1;
		}
	}
	return 0;
}

int MoveAllReferences(RoseDesign *design, const std::string workingdir)	//Given a design, checks for references, if it finds any, opens the referenced design, checks for references, etc. Then resolves references from the bottom up.
{
	//Traverse the references
	RoseCursor curser;
	curser.traverse(design->reference_section());
	curser.domain(ROSE_DOMAIN(RoseReference));
	RoseObject * obj;
	//std::cout << "Curser size: " << curser.size() << std::endl;
	if (!curser.size())
	{
		std::cout << "No references found." << std::endl;
		return -1;
	}
	while (obj = curser.next())
	{
		//std::cout << ROSE_CAST(RoseReference, obj)->uri() <<std::endl;
		//Pass the reference to AddItem, which will open the associated file 
		//& handle adding the referenced item & its children to design.
		std::cout << "reference no. " << obj->entity_id() <<'\n';
		int returnval = AddItem(ROSE_CAST(RoseReference, obj), design, workingdir);
		if (-1 == returnval)	//Horrible failure. Quit while we're ahead.
		{
			std::cerr << "Error parsing reference\n";
			return EXIT_FAILURE;
		}
		else if (0 == returnval)	//Reference successfully transplanted.
		{
			rose_move_to_trash(obj);
		}
		//TODO: Maybe add stuff for black/white-list cases and download error?
	}

	return 0;
}

int main(int argc, char* argv[])
{
	options.push_back("-i [filelist] Ignore any listed children <Cannot be used alongside -a>");
	options.push_back("-a [filelist] Only allow listed children <Cannot be used alongside -i>");
	if (argc < 3)
	{
		std::cout << "Usage: " << "STEPMerge.exe Master Output [Options]" << std::endl << "OPTIONS:\n";
		for (int i = 0; i < options.size(); i++)
		{
			std::cout << options[i] << std::endl;
		}
		return EXIT_FAILURE;
	}
	std::string infilename, outfilename;
	if (-1 == parsecmdline(argc, argv, infilename, outfilename)) return EXIT_FAILURE;
	ROSE.quiet(1);	//Get rid of annoying ST-Dev output.
	stplib_init();	// initialize merged cad library
	FILE *out;
	out = fopen("log.txt", "w");
	ROSE.error_reporter()->error_file(out);
	RoseP21Writer::max_spec_version(PART21_ED3);	//We need to use Part21 Edition 3 otherwise references won't be handled properly.
	RoseP21Writer::preserve_eids = ROSE_TRUE;
	//Find if the input file is in a directory, so we can go to there. Fixes some weirdness with directories.
	if (infilename.find_last_of('\\') || infilename.find_last_of('/'))
	{
		auto pos = infilename.find_last_of('\\') ? infilename.find_last_of('\\') : infilename.find_last_of('/');
		chdir(infilename.substr(0, pos).c_str());
	}
	/* Create a RoseDesign to hold the output data*/
	RoseDesign * master = ROSE.useDesign(infilename.data());
	if (!master)
	{
		std::cerr << "Error opening input file" << std::endl;
		return EXIT_FAILURE;
	}
	if(!outfilename.find_first_of('/')&&!outfilename.find_first_of('\\')) outfilename = "./" + outfilename;
	master->saveAs(outfilename.data());
	RoseDesign * design = ROSE.useDesign(outfilename.data());
	if (!design)
	{
		std::cerr << "Error opening output file" << std::endl;
		return EXIT_FAILURE;
	}
	//Design now contains the entire parent file.
	int retval = MoveAllReferences(design,master->fileDirectory());
	for (auto i : downloaded)
	{
		//Remove temporary files
		DeleteFileA(i.data());
	}
	rose_empty_trash();	//This deletes the reference from the new file, since we've replaced it with a local copy of the referenced object and it's children.
	design->save();
	return retval;
}

//TODO: Make platform agnostic. Consider using wget or something similar if in *nix environment.
int getfromweb(std::string url, std::string out)
{
	HRESULT hr;
	hr = URLDownloadToFile(0, url.data(), out.data(), 0, 0);
	//std::cout << hr;
	if (hr == S_OK) return 0;
	else return -1;
}