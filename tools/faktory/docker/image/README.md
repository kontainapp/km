We decided to copy-n-paste this part of the code because directly
importing `docker/docker` dependencies is not easy. There are multiple issues
file to resolve it, but no good avaliable solutions yet. Therefore, we
limited our import to `docker/docker/client` and `docker/docker/types`. We
copied `docker/docker/image` with our change.

Note: Copied from "github.com/docker/docker/image" with our changes. Commit:
`39e6def2194045cb206160b66bf309f486bd7e64`.