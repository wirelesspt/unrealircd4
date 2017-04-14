#!/bin/sh
openssl req -x509 -sha512 -utf8 -nodes -newkey rsa:4096 -days 1096 -keyout server.key.pem -out server.cert.pem
