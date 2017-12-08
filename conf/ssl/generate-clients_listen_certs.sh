echo "This will generate self signed ssl certificates for this listen server block to accept clients that are users";
echo "";
rm -r clients_listen.key clients_listen.crt
openssl req -x509 -nodes -days 1096 -utf8 -newkey rsa:4096-sha512 -keyout clients_listen.key -out clients_listen.crt -subj /CN=WirelessPT
chmod 400 clients_listen.key clients_listen.crt
echo
echo "Add this fingerprint to password field sslclientcertfp of the remote server link block described at https://unrealircd.org/docs/Link_block for higher control";
echo "Example: password "08:02:D0:D8:AB:30..." { sslclientcertfp; }; and set verify-certificate no;"
echo
openssl x509 -in clients_listen.crt -sha256 -noout -fingerprint
