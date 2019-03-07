# README #

Capture the steps to run cpython in km

### How do I get set up? ###

After `git clone`, simply run `./build.sh`. It will clone all the involved repos,
then build musl, km, and python.

After it is done, you need to `cd cpython`, where you can run
`../km/build/km/km ./python.km`.