# Basic

To build: `make`

To link: `./link-km.sh` after building

# Build Env

For redis, we build using a alpine container. Since alpine uses musl as part of the
build chain, we can work around km not implementing certain calls from libc.

# To run:
```
/<path to>/km/build/km/km --dynlinker=/<path to>/km/build/runtime/libc.so --putenv LD_LIBRARY_PATH=/usr/lib64:/lib64 ./redis-server.kmd <optional redis.conf>
```

# To build and run normal redis

```
make clean
make

./src/redis-server <optional redis.conf>

```

Note: the build artifact are from `alpine` and `musl`, so it can't be use
to build normal redis on fedora. `make clean` is required.
# Note on config
Note, currently kontain doesn't support fork, and therefore redis background
save doesn't work. We included a `redis.conf` in this directory that disables
the background save.
