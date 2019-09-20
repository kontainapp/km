"simple flask-based example"

import os
import sys
import datetime
from flask import Flask, request
from flask import render_template
app = Flask(__name__)


@app.route('/')
def intro():
    return render_template('flask_template_hello.html',
                           sysname=os.uname().sysname, machine=os.uname().machine, now=datetime.date.today())


@app.route('/hello/')
@app.route("/hello/<string:name>")
def hello(name="World"):
    return render_template('flask_template_test.html',
                           name=name)


def shutdown_server():
    func = request.environ.get('werkzeug.server.shutdown')
    if func is None:
        raise RuntimeError('Not running with the Werkzeug Server')
    func()


@app.route('/shutdown', methods=['POST', 'GET'])
def shutdown():
    shutdown_server()
    return '<h1>Server shutting down...</H1>'


def run():
    """
    entrypoint. Debug is turned on if we are NOT in KM
    """

    # TODO: debug=True uses subprocesses, we do not's support em yet
    app.run(debug=False, host='0.0.0.0', port=8080)


if __name__ == "__main__":
    run()
