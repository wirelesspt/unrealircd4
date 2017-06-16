rm -r old
mkdir old
mv *.c old /

echo "Downloading updated modules";
echo
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_allowctcp_opers.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_autovhost.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_anticaps.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_clones.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_extwarn.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_getlegitusers.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_listdelay.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_noinvite.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_pmdelay.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_pmlist.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_storetkl.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_uniquemsg.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/uncommon/m_repeatprot.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/uncommon/m_textshun.c
wget --user-agent="Mozilla/5.0" https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/u4/m_rmtkl.c

echo
ls *.c
echo 
ls *.c |wc -l

echo
echo "You should have a total of 15 modules";
