P21e3-Merge-Split-Tools
=======================
Included are three tools.

STEPMerge- Given a step file with references, finds all matching anchors and merges into a single file. 
Use STEPMerge.py to run over a ZIP assembly style direcory tree.

-Usage: 

STEPMerge.exe input.stp output.stp

STEPMerge.py [directory to traverse]


STEPSplit- Given a step file with assemblies, splits into proposed ZIP assembly form.

-Usage:

STEPSplit.exe input.stp


PMISplit- Given a step file, uses ARM to split the workpieces into a separate file. This effectively puts all the geometry in one file and everything else in another.

-Usage:

PMISplit.exe input.stp
