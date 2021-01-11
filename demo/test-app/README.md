# Simple inferring app using tensorflow

## To build docker container:

```bash
docker build -t test-app .
```

## To run and test:

```bash
docker run --device /dev/kvm --name test-app --rm -it -p 5000:5000 test-app bash
```

(or use `--device /dev/kkm` if necessary).

Then inside the container, to run native app:

```bash
python app.py
```

It takes several seconds for the application to initialize.
Message `WARNING: This is a development server. Do not use it in a production deployment.` is normal and expected,
it it simply because the app.py uses simple developer's flask configuration.

And to test, run from the host:

```bash
curl -s  -X POST -F image=@dog2.jpg 'http://localhost:5000/predict' | jq .
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

## To enable km

To switch between native python and python in KM, from the host run:

```bash
./p.sh test-app
```

This copies km and necessary friends int o the container, and switches python to unikernel version.

Then inside the container `./switch.sh km` switches python to km, and `switch.sh` goes back to native
