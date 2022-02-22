git remote add -t master upstream https://github.com/apache/incubator-nuttx.git
git fetch upstream master
git remote set-head upstream master
git remote rename origin motec
git branch main motec/main
