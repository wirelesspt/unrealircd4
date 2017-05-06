Most of the bots will crawl across channels from the /list output and then PM spam all the users 
on the channels making a large amount of query windows open and cause a bunch of notifications.

A simple delay that can be configured in the config to make suers wait x amount of time before they can do /list or PM would mitigate this greatly.

Configuration needed:


set {
	listdelay 60;
};
