echo "This will generate ssl certificates for this listen server block to accept clients";
echo "";
rm -r clients_listen.key clients_listen.crt
openssl req -x509 -nodes -days 1096 -utf8 -newkey rsa:4096-sha512 -keyout clients_listen.key -out clients_listen.crt -subj /CN=Nixbits
chmod 400 clients_listen.key clients_listen.crt
