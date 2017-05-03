m_repeatprot (UNC)

This module used to be called m_noticeprot, so remove that one entirely and replace it with this to avoid conflicts

Sometimes there are faggots who spam the shit out of you, either through NOTICE or PRIVMSG. This module will GZ:Line/kill/block their ass if they do. Other than specifying the triggers and exemptions, you can tweak 
the action a lil' bit:

    action <block/kill/gzline> -- what action to take upon detecting a spammer, default is gzline
    showblocked <0/1> -- display the ban message to the spammer when action == block, default = 0
    banmsg <string> -- zline message obviously, default = Nice spam m8
    tkltime <timestr> -- how long to zline for, format is like 60, 1h5m, etc; default = 60 (seconds)
    threshold <int> -- after how many identical messages the action kicks in, default = 3

The module will keep track of people's last and first to last messages, so it will still catch people who alternate their spam. =] Also, channels are excluded as they probably have something better in place (like 
+C to disable CTCP in general).

Updated shit:

    Reset conf defaults on rehash lol
    Added new config directive timespan so you can "expire" the counters (simply don't specify it to never expire 'em)
    Added trigger for INVITE spam
    Added another action gline
    Added trigger for CTCP yo
    Is now also markup/colour agnostic (i.e. bold/underline/etc and colours will be stripped before matching)
    Added detection for repeated OPER messages, will exclude the first one since that is usually legitimate (it only checks for duplicate OPER <user> too)
    Also exempted sending to U:Lines as people sometimes fuck up their NickServ passwerds etc
    Added exemption block
    Renamed some variables/directives
    Added showblocked directive
    Added multiple actions, as well as the ability to choose between 'em
    Renamed this shit to m_repeatprot lol
    Moved everything into an encapsulating repeatprot { } block, with triggers { } one inside for the actual triggers

Config block example:

repeatprot {
    triggers {
        notice;
        //privmsg;
        oper;
        //ctcp;
    };
    exemptions {
        //nick!user@host;
        *!admin@*;
        cmsv!*@*;
    };

    timespan 2m;
    action block;
    banmsg "Read /rules";
    showblocked 1;
    //tkltime 60;
    threshold 3;
};

You need at least one trigger obviously. The exemption masks must match *!*@*, so should be a full nick mask m9. The hostname used in exemptions should match the realhost, not a cloaked one.
