echo "This will generate ssl certificates for this listen server block to link services";
echo "";
rm -f services_listen.key services_listen.crt
openssl req -x509 -nodes -days 1096 -utf8 -newkey rsa:4096-sha512 -keyout services_listen.key -out services_listen.crt -subj /CN=unrealircd
chmod 400 services_listen.key services_listen.crt
