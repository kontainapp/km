"simple flask-based example"

import os
import datetime
from flask import Flask
from flask import render_template
app = Flask(__name__)


@app.route('/')
def intro():
    now = datetime.date.today()
    content = "<h1>Hello, it's {} and it'd Flask dockerized</h1>".format(now) + \
        "<h2>{}".format(os.uname())
    return content


@app.route('/hello/')
@app.route("/hello/<string:name>")
def hello_name(name="World"):
    return render_template('flask_template_test.html',
                           name=name)


if __name__ == "__main__":
    app.run(debug=False, host='0.0.0.0', port=8080)
