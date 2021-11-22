# Simple inferring app using tensorflow

Note that we require our own custom built version of tensorflow.
The code isn't changed, we just compile it with `-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC=1`
to disable use of non-POSIX primitives that are not supported on musl.

To build this version, in `km/tools/hashicorp/build_tensorflow` run `vagrant up`
(or to make sure it is rebuilt `vagrant up --provision`, although it takes 2 - 3 hours).
Tensorflow will be built in the virtual machine,
then the resulting tensorflow<something>.whl file will be copied into that directory.

Note also that the tensorflow code requires python 3.8.
Since our current python runenv is 3.9 there is a mismatch.
In order to make the demo we can compile and use python 3.8 runenv, specifically for this demo.
We don't publish the 3.8 runenv at the moment.

## Python 3.8 runenv

```bash
make -C payloads/python clobber
make -C payloads/python -j fromsrc TAG=v3.8.6
make -C payloads/dynamic-python runenv-image TAG=v3.8.6
```

These steps overwrite the `kontainapp/runenv-dynamic-python` with the 3.8 version.
To go back to regular python repeat the steps without `TAG=xxx`.

## Docker

```bash
make container
```

```bash
docker run --runtime=krun -v $(pwd)/tmp:/mnt:Z --name test-app --rm -it -p 5000:5000 test-app /bin/sh
```

## To test:

Once inside the container with the app, to run the app:

```bash
python app.py
```

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

Run kontainer the same way as above:

```bash
docker run --runtime=krun -v /opt/kontain/bin/km_cli:/opt/kontain/bin/km_cli \
  -v $(pwd)/tmp:/mnt:rw --name test-app --rm -it -p 5000:5000 test-app /bin/sh
```

On the host, run

```bash
./test.sh
```

The script will take care of carefully measuring start time and response time, and display the difference.

Inside the kontainer, run:

```bash
/run.sh
```

The app will start, and eventually respond to the request.

To take the snapshot:

```bash
docker exec -it test-app /opt/kontain/bin/km_cli -s /tmp/km.sock
```

To show snapshot, run the

```bash
./test.sh
```

again, and then run:

```bash
/run_snap.sh
```
