#!bin/sh
certtool --generate-privkey --bits 4096 --outfile nixbits.key
certtool --generate-self-signed --load-privkey anope.key --outfile nixbits.crt
certtool --generate-dh-params --bits 4096 --outfile dhparams.pem
