Note: This is a proposal that superceeds the existing git submodule handing for KRUN.

# KRUN Overview

KRUN is the container runtime for KM. It is based on a fork of `github.com/containers/crun` (CRUN) with KM specific
changes applied to it. In particular, our changes:

- Automatically bind mount KM into the container.
- Automatically add the virtualiztion device(s) (/dev/kvm and/or /dev/kkm) to the container (if they are present in the host system).
- Starts the container entry point under the control of KM.

CRUN is an active project that is periodically tested, packaged, and released. The release cadence for CRUN appears to be more or less
monthly. CRUN adhers to SEMVER (sematic versioning) conventions. The most recent release is 1.3 (26 Sept 2021).

KRUN releases are based on CRUN releases. KRUN is released independently of KM and the rest of the Kontain stack.
The names of KRUN releases reflect the CRUN release that they are based on. An example of a KRUN release name is:

```
1.3+kontain.3
```

Which would mean this KRUN is based on CRUN 1.3 plus version 3 of the Kontain changes (whatever that means).

This implies a more comples set of git operations to synchromize the KRUN repo than we do today, but the payoff is
better control of what we actually ship to customers.

