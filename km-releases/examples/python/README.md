# Python

The example, `snapshot.py` is a mock-up of a user's code for a Faas function. This particular example is inspired by the `stable/python/examples/preload.py` example from Kubeless runtime (<https://github.com/kubeless/runtimes.git>). The Kubeless example showed how their python runtime can keep a function in memory so that only the first call to the function is slowed down by the cost of loading the model.

The KM example takes a snapshot after the model has been loaded. Since the snapshot contains the preloaded model, when the snapshot is resumed the function runs quickly every time without requiring any extra logic to keep the function in memory.

In order to create a baseline, `snapshot.py` is first run with the system python interpreter.

```sh
$ time python snapshot.py
Hello world

real 0m3.027s
user 0m0.017s
sys 0m0.007s
```

The second case runs the same program under KM. Since the `live=True` parameter is passed to `kontain.snapshots.take()`, the function is called. This allows comparison with the base case.

```sh
$ time ~/kontain/km/payloads/python/cpython/python snapshot.py
Hello world

real 0m3.041s
user 0m0.011s
sys 0m0.024s
```

The extra 20ms is the time it takes to create the snapshot.

The final case shows the snapshot restarted showing the function called with the model preloaded.

```sh
$ time ~/kontain/km/build/km/km kmsnap
Hello world

real 0m0.013s
user 0m0.002s
sys 0m0.010s
$
```

Since the on-disk snapshot contains the pre-loaded model, there is no need to have logic that keeps a function 'warm'.
