from __future__ import print_function
import os,sys,subprocess,shutil

argv = sys.argv
if len(argv) != 2:
  print("Usage: STEPMerge.py Directory_to_parse")
  sys.exit(1)
dir=argv[1]
mydir=os.getcwd() +'\\'
def dirswitch(dir):
  try:
    os.chdir(dir)
  except:
    print("Couldn't open directory.")
    sys.exit(1)
dirswitch(dir)
subdir= os.listdir('.')
def dirtraverse(dir,depth):
  hassubdir=0
  for i in dir:
    if os.path.isdir(i):
      hassubdir=1
      dirswitch(i)
      dirtraverse(os.listdir('.'),depth+1)
      dirswitch('..')

  cwd = os.getcwd()
  lastslash = cwd.rfind('\\')
  if cwd[lastslash+1:len(cwd)]!="geometry_components": #and hassubdir == 0:
    masterleaf =cwd +"\master.stp"
    outname = "out.stp"
    finalname = cwd[lastslash+1:len(cwd)] +".stp"
    masterout =cwd+'\\'+outname #[0:lastslash+1] +outname
    try:
      retval = subprocess.call([mydir+'STEPMerge.exe',masterleaf,masterout])
    except:
      print("Error finding STEPMerge.exe")
      sys.exit(1)
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