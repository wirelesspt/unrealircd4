#!bin/sh
certtool --generate-privkey --bits 4096 --outfile server.key.pem
certtool --generate-self-signed --load-privkey server.key.pem --outfile server.cert.pem
certtool --generate-dh-params --bits 4096 --outfile dhparams.pem
