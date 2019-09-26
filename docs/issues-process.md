# Issues labeling and workflow

We use GitHub issues as a tracking mechanism. No ZenBoards or other extensions for now. We will adjust the labels/projects/milestones as needed

* An issue is open for things like bug, feature, improvement.
* For large chunks of work , in the future,  we will create Projects ((e.g. CRI / KM / Runtime/ Node)).
  * For now we do not use Projects, will add when we do separate per-project triages or planning

Workflow:

* An Issue is opened with label New, then triaged to be categorized (see categories in Labels, below).
  * During the triage, we remove the `new` label , add necessary labels and/or Milestone, or may decide to close.
  * Either fixing with 'Fixes #issue-number' string in the PR description - `github merge` will close the issue automatically
  * Or by putting in a label decribing the  reason (below) and manually closing the issue.

## Labels

### Labels for Urgency

* urgent (e.g. blocker - preempts other work)
* normal (regular , needs to be done in the normal order)
* low (done when idle)

### Labels for Severity

* critical (top important for customer)
* major (regular important - needs to be done)
* minor (nice to have)

### Labels for Cost/size

* xsmall (less than a day)
* small (less than a week)
* medium (a week to 2 weeks)
* large (up to a month)
* xlarge (> 1mo)

### Labels for issue Type

* Bug - something is broken
* Improvement - improve existing stuff
* Feature - add new stuff

### Labels for workflow

* New (auto-inserted by Issue template and removed when issue is triaged)

### Labels about why the issue was closed

* fixed (we will not have this label, this is the default for closed issues)
* invalid (not a bug - some misunderstanding usually; or not under our control and nothing to do)
* declined (yes, it is an issue, but we won't do it, ever)
* duplicate

### other labels

Good First Issue - this is a good one to give to a newcomer

## Projects

Default project includes km, runtime and payloads, including tests code. Other projects are (for today) `CI/CD`.

## Milestones

Milestone is a bug committed date when we need to deliver some functionality. We will add them as we go.
By default we consider everything being in "Current" milestone - we don't create it, just consider everything without explicit milestone to be in "current" .

We use "future" milestone as a parking space for deferred issues:

* Future (deferred things, no focus on it now but certainly need to revisit in the future))


