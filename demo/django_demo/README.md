# THIS IS WIP

## Steps to configure virtual environment for django/gunicorn

* create virtual environment with `virtualenv ~/workspace/km/build/demo/venv`
* in bin of that virtenv remove (or move to .orig) python3 `mv python3 python3.orig`
* copy appropriate km into bin (for example `cp /opt/kontain/bin/km .`)
* create python3 as symlink to km `ln -s km python3`
* `cp ~/workspace/km/payloads/python/cpython/python.km python3.km` -- Note `3` in the name
* pip install django in your `cpython/Lib`: `pip install -t ~/workspace/km/payloads/python/cpython/Lib/ django`
* put file `pyvenv.cfg` in virtenv (above bin) with the following in it:

```
   home = /home/serge/workspace/km/payloads/python/cpython
   include-system-site-packages = false
```

The first line is YOUR workspace, not mine.

## Run django demo

In demo/django_demo, run `./manage.py runserver`. Access `127.0.0.1:8000/admin`.
