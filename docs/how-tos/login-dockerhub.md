# Log into Kontain.app Dockerhub

If you need to be added to the kontain dockerhub, register an account first
and contract @serge to be added. There are multiple ways of logging into
docker hub:

* `docker login -u <username> -p <password>` or place a token under
  `~/.docker/token` and your dockerhub username under `~/.docker/username`.
* run `make -C ~/workplace/km/cloud/dockerhub login`. To obtain a token,
  goto dockerhub Account Setting -> Security -> Access Tokens.
