import os

from flask import Flask
from flask import send_file

from app import app

IMAGE_FILE = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'wifi_smartconfig_test.bin')

@app.route('/image.bin')
def image():
    fh = open(IMAGE_FILE, 'rb')
    try:
        return send_file(fh, mimetype='application/octet-stream')
    except:
        fh.close()
