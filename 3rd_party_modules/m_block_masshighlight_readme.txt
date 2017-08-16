It keeps track of a user's messages on a per-channel basis and checks if they highlight one person too many times or too many different persons at once (as per the alternation thing mentioned earlier). Opers and 
U:Lines are exempt (as per usual), but also those with list modes +a and +q. When someone hits the threshold, opers with the snomask "EYES" will get a server notice through the module (enable that snomask with 
/mode <nick> +s +e).

Config block:
The module doesn't necessarily require any configuration, it uses the following block as defaults

Code: Select all

block_masshighlight {
	maxnicks 5;
	delimiters "     ,.-_/\:;";
	action gline;
	duration 7d;
	reason "No mass highlighting allowed";
	snotice 1;
	banident 1;
};

    maxnicks: Maximum amount of highlights (going over this number results in action setting in)
    delimiters: List of characters to split a sentence by (don't forget the surrounding quotes ;]) -- any char not in the default list may prevent highlights anyways
    action: Action to take, must be one of: drop (drop silently [for the offender]), notice (drop, but do show notice to them), gline, zline, shun, tempshun, kill
    duration: How long to gline, zline or shun for, is a "timestring" like 7d, 1h5m20s, etc
    reason: Reason to show to the user, must be at least 4 chars long
    snotice: Whether to send snomask notices when users cross the highlight threshold, must be 0 or 1
    banident: When set to 1 it will ban ident@iphost, otherwise *@iphost (useful for shared ZNCs etc)

