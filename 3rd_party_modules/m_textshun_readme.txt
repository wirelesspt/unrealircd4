m_textshun (UNC)

Enables privileged 0pers to drop messages based on nick and body regexes (T:Lines), similar to badwords and spamfilter but more specific. It only supports (PCRE) regexes because regular wildcards seem ineffective 
to me, fucken deal w/ it. ;] Also, you can't have spaces (due to how IRC works) so you'll have to use \s and shit. Unreal creates a case-insensitive match so no worries there, it also tells you if you fucked up 
your regex (and what obv). Only opers with the new operpriv textshun will be able to use it, although all others will get the notices since these T:Lines are netwerk wide bruh. You'll also get a server notice for 
when a T:Line is matched. Servers, U:Lines and opers are exempt for obvious raisins.

The expiration shit is a lil' hacky cuz there's no "tick" or nethang in Unreal. So it only checks for expiration on the PRE*MSG hooks (pre user and channel messages) as well as on a module command (like one of the 
below examples). These expirations are propagated to other servers. Also, they store it in a ModData struct so it persists through a rehash without the need for a .db fiel. ;] Furthermore, they sync their known 
T:Lines upon server linkage. =]] Other lines such as G:Lines, Z:Lines are also stored in memory and get resynced during a link so these are pretty similar aye.

Updated shit::

    Show pretty setat timestamps during server sync phase as well as in /tline list
    Send to snomask SNO_TKL instead of realops etc
    Now supports timestrings like 1h instead of having to pass seconds =]

Config block:

operclass netadmin-textshun {
    parent netadmin;
    privileges {
        textshun;
    };
};

Syntax:
TEXTSHUN <ADD/DEL> <nickrgx> <bodyrgx> [expire] <reason>

Also supports the aliases TS and TLINE. =]

Examples:

    /tline add guest.+ h[o0]+m[o0]+ 0 nope => All...
    /textshun add guest.+ h[o0]+m[o0]+ nope => ...of...
    /ts del guest.+ .+ => ...these add/delete the same T:Line, with no expiration
    /tline add guest.+ h[o0]+m[o0]+ 3600 ain't gonna happen => Add a T:Line that expires in an hour =]
    /tline add guest.+ h[o0]+m[o0]+ 1h ain't gonna happen => Ditto ;];]
    /tline => Show all T:Lines
    /ts halp => Show built-in halp

The nick regex is matched against both nick!user@realhost and nick!user@vhost masks. The timestring shit like 1h supports up to weeks (so you can't do 1y).
