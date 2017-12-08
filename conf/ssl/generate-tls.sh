#!bin/sh
certtool --generate-privkey --bits 4096 --outfile wirelesspt.key
certtool --generate-self-signed --load-privkey anope.key --outfile wirelesspt.crt
certtool --generate-dh-params --bits 4096 --outfile dhparams.pem
