echo "This will generate ssl certificates for this listen server block to link server links";
echo "";
rm -f links_listen.key links_listen.crt
openssl req -x509 -nodes -days 1096 -utf8 -newkey rsa:4096-sha512 -keyout links_listen.key -out links_listen.crt -subj /CN=unrealircd
chmod 400 links_listen.key links_listen.crt

