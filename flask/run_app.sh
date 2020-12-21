#!/usr/bin/env bash

HOST_NAME=192.168.1.19
PORT=8700
CERT_PATH=$(pwd)/../server_certs/wifiupdate.pem
KEY_PATH=$(pwd)/../server_certs/wifiupdate.key
export FLSAK_ENV=development
export PYTHONPATH=.
export FLASK_APP=esp32_service

poetry run flask run \
          -p "${PORT}" \
          -h "${HOST_NAME}" \
          --cert "${CERT_PATH}" \
          --key "${KEY_PATH}"
