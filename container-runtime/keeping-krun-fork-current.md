# Overview

The kontainapp/krun github repository is a fork of the containers/crun github repository.
We need to periodically bring the kontain crun fork up to date and then rebase our changes on top of a more recent crun release.
This note describes a way to do that.
You will need access to the kontainapp github repositories and a km workspace on a workstation to do this work.
For the purposes of this note, assume krun is based on crun 1.4 and we want to rebase our changes to crun 1.4.1
This example uses the following shell variables in the commands.

```
CRUN_CURRENT=1.4
CRUN_TARGET=1.4.1
```

# Update kontainapp/crun from containers/crun

go to the kontainapp/crun repository by following this link:
```
https://github.com/kontainapp/crun
```

Select the main code branch from the branch drop down menu,
then click on the "fetch upstream" button and select "fetch and merge".

# Select a crun release to rebase krun changes to

We need to chose which release of crun we want the krun changes rebased on top of.
Assume krun is currently on top of crun 1.4.
For this example we will chose to bring krun up to 1.4.1 and follow these steps to get there.

on github:

Be sure you are still on the main kontainapp/crun branch, then click on the clock/commits button under the "Fetch upstream" button.
This brings up a list of commits.
crun uses a commit title of "News: tag x.y[.z]" for their releases.
Scroll throught the list of commits and find the release you want to bring krun up to.
For this example we chose the 1.4.1 release.
Click on the "<>" button for the chosen commit (on the right side of the line).
This brings up list of commits preceeding the 1.4.1 release.
To the upper left is a drop down labeled with the "sha" for the crun release commit you chose.
Click the "sha" to get a drop down list of branches.  You can also create a new branch label in that menu.
So we create a new branch named "krun-1.4.1" (krun-$CRUN_TARGET).

# Refresh your crun workspace on your workstation

Now move over to your workstation in your kontainapp/crun workspace.
Refresh your local crun git repository.
Then create a branch to do the rebase of krun into the 1.4.1 branch.
We assume you have no uncommited files in this workspace.

```
cd container-runtime/crun
git checkout main
git pull -p
git checkout krun
git checkout -b paulp/move-krun-$CRUN_TARGET
git rebase origin/krun-$CRUN_TARGET paulp/move-krun-$CRUN_TARGET
```

There will probably be merge conflicts.  Fix them.  When you've made it through the conflicts do a final rebase continue.

```
git rebase --continue
```

# Verify that the krun changes are on top of the crun 1.4.1 tag

```
[paulp@work crun]$ git log --oneline | less

eb009d9 (HEAD -> paulp/move-krun-1.4.1) Add kontian specialization code to crun (#24)
8026135 (origin/krun-1.4.1) NEWS: tag 1.4.1
c7f6cd5 Merge pull request #847 from giuseppe/function-attributes
8711fbd utils: add a len argument to get_current_timestamp
b5987ee utils: add printf attribute to xasprintf
e9ba4ae libcrun: add printf attribute to error functions
2ca2d06 utils: add attribute malloc to x.*alloc.* functions
ece4431 utils: add the sentinel attribute to append_paths
e0d6caa Merge pull request #846 from giuseppe/drop-duplicate-line
bb57968 cgroup: do not lookup string twice
[paulp@work crun]$
```

# Build krun from scratch and test it:
```
make -C container-runtime clobber
make -C container-runtime
```

Fix any compilation errors there may be.  These will need to be commited in the current branch.
Once compilation is successful, test them:
```
make -C km
make -C container-runtime test
```

If the above tests work, we need to do more complete testing.
The next tests assume you have podman and docker configured to use krun as a runtime.
Test the payloads runenv-image's as follows.

```
make -C payloads clean all
make -C payloads runenv-image
make -C payloads validate-runenv-image
```

Fix any additional krun problems found.

At this point the payloads run with krun and km in both docker and podman containers.

# Push the move-krun-1.4.1 branch to github
Our branch with rebased krun works so we need to get that changeset into the kontainapp/crun repository by creating a PR.
```
git push origin paulp/move-krun-$CRUN_TARGET
```

Follow the web link "git push" prints out to create a PR for these changes that move krun to crun 1.4.1
Part of the PR setup is to select the branch the change is merged into.
You want to merge into the krun-1.4.1 branch you created earlier on github.
There should be no conflicts for this merge.  If there are find out what is wrong and correct the problem.
Once the PR is created the crun github workflow will run on our changes.
You should fix any problems the crun workflow uncovers.
Noone seems to be interested in reviewing these rebase changes because nothing material should have changed,
so after a "while" you can "squash and merge" these changes into the krun-1.4.1 branch you created earlier on github.

# Fixup krun branch names

Now we have kontain's changes to crun in the krun-1.4.1 branch but we need the branch to be named krun.
Branch renaming can be done in the github branch name list at this link:
```
https://github.com/kontainapp/crun/branches
```
Each branch name line has a "pencil" icon (to the right) that is used to rename the branch.

First we name the current krun branch based on an earlier release of crun to another branch label.
Recall the earlier release in this example was 1.4, so rename the krun branch to krun-1.4.
This may warn that you are changing the name of the default branch.
This is ok because we will fix that up later.

Using the same method, rename the krun-1.4.1 branch to krun.

Next change the default branch to be krun again by following this link.
```
https://github.com/kontainapp/crun/settings/branches
```
In the "Default branch" section, click on the left/right arrow pile.
Switch the default branch name from krun-1.4 to krun.

At this point we could remove the krun-1.4 branch but it is not necessary.

# Fixup krun labels in user workspaces

Renaming branch labels on github does not affect the branch labels in the user's workstation workspaces.
They will need to get the latest krun branch label for new commits:
```
git pull -p
```

If the user has no changes in their now down level krun workspace, they can just delete their krun branch
```
git branch -d krun
```

If the user has changes related to the old krun branch, rename the branch so that it can be referenced in the future:
```
git branch -m krun krun-1.4
```
If the changes are important they will eventually need to rebased to the new krun branch.

Then switch to the new krun branch label:
```
git checkout -b krun origin/krun
```

# Change the km repository to use the new krun

Once the kontainpp/crun repository is "up to date" we need to change the km repository to use the new krun branch for its builds.
On your workstation do the following:

```
cd km
git pull -p
git checkout master
git submodule update --recursive
git checkout -b paulp/switch-krun-to-$CRUN_TARGET
git add container-runtime/crun
git commit
git push origin paulp/switch-krun-to-$CRUN_TARGET
```

Then follow the web link produced by "git push" to setup a PR.
Once the PR is merged km will be using the "up to date" version of krun.

