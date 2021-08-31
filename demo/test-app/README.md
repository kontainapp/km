# Simple inferring app using tensorflow

There are two ways to build and run this app - in a docker container or in vagrant VM.
The application code is the same, it's just the packaging is different.

Note that we require our own custom built version of tensorflow.
The code isn't changed, we just compile it with `-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC=1`
to disable use of non-POSIX primitives that are not supported on musl.

To build this version, in `km/tools/hashicorp/build_tensorflow` run `vagrant up`
(or to make sure it is rebuilt `vagrant up --provision`, although it takes 2 - 3 hours).
Tensorflow will be built in the virtual machine,
then the resulting tensorflow<something>.whl file will be copied into that directory.

## vagrant

Run `vagrant up` to build and start the VM with the app.
`vagrant ssh` (or configure ssh by using output of `vagrant ssh-config`) into the VM.

## Docker

```bash
make container
```

```bash
docker run --runtime=krun -v $(pwd)/tmp:/mnt:Z --name test-app --rm -it -p 5000:5000 test-app /bin/sh
```

## To test:

Once inside a VM (or container) with the app, to run the app:

```bash
python app.py
```

In VM you can switch between native and kontain python.
To switch python between native and KM based python use `./switch.sh` for native and `./switch.sh km` for km
(note in container there is only KM python).

It takes several seconds for the application to initialize.
Message `WARNING: This is a development server. Do not use it in a production deployment.` is normal and expected,
it it simply because the app.py uses simple developer's flask configuration.

And to test, from the host, run:

```bash
curl -s -X POST -F image=@dog2.jpg 'http://localhost:5000/predict' | jq .
```

If everything is OK, it prints something like:

```json
{
  "predictions": [
    {
      "label": "Bernese_mountain_dog",
      "probability": 0.620712161064148
    },
    {
      "label": "Appenzeller",
      "probability": 0.28114044666290283
    },
    {
      "label": "EntleBucher",
      "probability": 0.07214776426553726
    },
    {
      "label": "Border_collie",
      "probability": 0.012632192112505436
    },
    {
      "label": "Greater_Swiss_Mountain_dog",
      "probability": 0.007238826714456081
    }
  ],
  "success": true
}
```

reporting probabilities that the presented image (`dog2.jpg`) is image of the particular dog breed.

## snapshot

Make sure km_cli is compiled and in /opt/kontain/bin.

Run kontainer the same way as above:

```bash
docker run --runtime=krun -v $(pwd)/tmp:/mnt:Z --name test-app --rm -it -p 5000:5000 test-app /bin/sh
```

On the host, run

```
./test.sh
```

The script will take care of carefully measuring start time and response time, and display the difference.

Inside the kontainer, run:

```
/run.sh
```

The app will start, and eventually respond to the request.

To take the snapshot:

```
docker exec -it test-app /opt/kontain/bin/km_cli -s /tmp/km.sock
```

To show snapshot, run the

```
./test.sh
```

again, and then run:

```
/run_snap.sh
```
