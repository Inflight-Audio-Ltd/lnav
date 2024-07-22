To add the source repo as upstream:

git remote add upstream https://github.com/tstack/lnav.git
git fetch --tags upstream
git push -f --tags origin master


To sync the fork with main repo:

git fetch upstream
git fetch --tags upstream
git merge upstream/master
git push -f --tags origin master
