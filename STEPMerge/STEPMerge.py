from __future__ import print_function
import os,sys,subprocess,shutil

argv = sys.argv
if len(argv) != 2:
  print("Usage: STEPMerge.py Directory_to_parse")
  sys.exit(1)
dir=argv[1]
try:
 os.chdir(dir)
except:
  print("Couldn't open directory.")
  sys.exit(1)
subdir= os.listdir('.')
def dirtraverse(dir,depth):
  hassubdir=0
  for i in dir:
    if os.path.isdir(i):
      hassubdir=1
      os.chdir(i)
      dirtraverse(os.listdir('.'),depth+1)
      os.chdir('..')

  cwd = os.getcwd()
  lastslash = cwd.rfind('\\')
  if cwd[lastslash+1:len(cwd)]!="geometry_components": #and hassubdir == 0:
    masterleaf =cwd +"\master.stp"
    outname = "out.stp"
    finalname = cwd[lastslash+1:len(cwd)] +".stp"
    masterout =cwd+'\\'+outname #[0:lastslash+1] +outname
    print(masterleaf)
    retval = subprocess.call([r'D:\Git\P21e3-Merge-Split-Tools\STEPMerge\STEPMerge.exe',masterleaf,masterout])
    if retval!=0:
      print("Error Running Merge.")
      sys.exit(1)
    try:
      shutil.move(masterout,"../"+finalname)
    except:
      print("Couldn't move output file: " + masterout)
      sys.exit(1)

#subprocess.call([r'D:\Git\P21e3-Merge-Split-Tools\STEPMerge\STEPMerge.exe',"D:\Git\P21e3-Merge-Split-Tools\STEPMerge\AS1-AC-214\PLATE\master.stp","D:\Git\P21e3-Merge-Split-Tools\STEPMerge\AS1-AC-214\PLATE\PLATE.stp"])
  
dirtraverse(subdir,0)