echo "This will generate self signed ssl certificates for this listen server block to accept links hubs/leafs";
echo "";
rm -r links_listen.key links_listen.crt
openssl req -x509 -nodes -days 1096 -utf8 -newkey rsa:4096-sha512 -keyout links_listen.key -out links_listen.crt -subj /CN=WirelessPT
chmod 400 links_listen.key links_listen.crt
echo
echo "Add this fingerprint to password field sslclientcertfp of the remote server link block described at https://unrealircd.org/docs/Link_block for higher control";
echo "Example: password "08:02:D0:D8:AB:30..." { sslclientcertfp; }; and set verify-certificate no;"
echo
openssl x509 -in links_listen.crt -sha256 -noout -fingerprint

