Most of the bots will crawl across channels from the /list output and then PM spam all the users 
on the channels making a large amount of query windows open and cause a bunch of notifications.

A simple delay that can be configured in the config to make suers wait x amount of time before they can do /list or PM would mitigate this greatly.

Upgraded module: https://forums.unrealircd.org/viewtopic.php?f=52&p=38570#p38570

Configuration needed:


listrestrict {
	connectdelay 60; // How long a client must have been online for
	needauth 1; // Besides connectdelay, also require authentication w/ services

	exceptions {
		all "user@*";
		auth "*@123.123.123.*";
		connect "someone@some.isp";
		connect "need@moar";
	};
};


Omitting a directive entirely will make it default to off. If connectdelay is specified, the minimum required value is (still) 10 as anything below seems pretty 
damn useless to me. =] The exceptions block should be pretty self explanatory. ;] If multiple masks match a user, only the first one will be checked/used.

As usual with my mods, U:Lines, opers and servers are exempt from any restrictions.
