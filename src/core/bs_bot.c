/* BotServ core functions
 *
 * (C) 2003-2008 Anope Team
 * Contact us at info@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 *
 * $Id$
 *
 */
/*************************************************************************/

#include "module.h"

int do_bot(User * u);
void myBotServHelp(User * u);

class BSBot : public Module
{
 public:
	BSBot(const std::string &modname, const std::string &creator) : Module(modname, creator)
	{
		Command *c;

		this->SetAuthor("Anope");
		this->SetVersion("$Id$");
		this->SetType(CORE);

		c = createCommand("BOT", do_bot, is_services_admin, -1, -1, -1,	BOT_SERVADMIN_HELP_BOT, BOT_SERVADMIN_HELP_BOT);
		this->AddCommand(BOTSERV, c, MOD_UNIQUE);

		moduleSetBotHelp(myBotServHelp);
	}
};


/**
 * Add the help response to Anopes /bs help output.
 * @param u The user who is requesting help
 **/
void myBotServHelp(User * u)
{
    if (is_services_admin(u)) {
        notice_lang(s_BotServ, u, BOT_HELP_CMD_BOT);
    }
}

/**
 * The /bs bot command.
 * @param u The user who issued the command
 * @param MOD_CONT to continue processing other modules, MOD_STOP to stop processing.
 **/
int do_bot(User * u)
{
    BotInfo *bi;
    char *cmd = strtok(NULL, " ");
    char *ch = NULL;

    if (!cmd)
        syntax_error(s_BotServ, u, "BOT", BOT_BOT_SYNTAX);
    else if (!stricmp(cmd, "ADD")) {
        char *nick = strtok(NULL, " ");
        char *user = strtok(NULL, " ");
        char *host = strtok(NULL, " ");
        char *real = strtok(NULL, "");

        if (!nick || !user || !host || !real)
            syntax_error(s_BotServ, u, "BOT", BOT_BOT_SYNTAX);
        else if (readonly)
            notice_lang(s_BotServ, u, BOT_BOT_READONLY);
        else if (findbot(nick))
            notice_lang(s_BotServ, u, BOT_BOT_ALREADY_EXISTS, nick);
        else if (strlen(nick) > NickLen)
            notice_lang(s_BotServ, u, BOT_BAD_NICK);
        else if (strlen(user) >= USERMAX)
            notice_lang(s_BotServ, u, BOT_LONG_IDENT, USERMAX - 1);
        else if (strlen(user) > HOSTMAX)
            notice_lang(s_BotServ, u, BOT_LONG_HOST, HOSTMAX);
        else {
            NickAlias *na;

                /**
		 * Check the nick is valid re RFC 2812
		 **/
            if (isdigit(nick[0]) || nick[0] == '-') {
                notice_lang(s_BotServ, u, BOT_BAD_NICK);
                return MOD_CONT;
            }
            for (ch = nick; *ch && (ch - nick) < NICKMAX; ch++) {
                if (!isvalidnick(*ch)) {
                    notice_lang(s_BotServ, u, BOT_BAD_NICK);
                    return MOD_CONT;
                }
            }

            /* check for hardcored ircd forbidden nicks */
            if (!ircdproto->IsNickValid(nick)) {
                notice_lang(s_BotServ, u, BOT_BAD_NICK);
                return MOD_CONT;
            }

            if (!isValidHost(host, 3)) {
                notice_lang(s_BotServ, u, BOT_BAD_HOST);
                return MOD_CONT;
            }
            for (ch = user; *ch && (ch - user) < USERMAX; ch++) {
                if (!isalnum(*ch)) {
                    notice_lang(s_BotServ, u, BOT_LONG_IDENT, USERMAX - 1);
                    return MOD_CONT;
                }
            }

                /**
		 * Check the host is valid re RFC 2812
		 **/

            /* Check whether it's a services client's nick and return if so - Certus */
            /* use nickIsServices reduce the total number lines of code  - TSL */

            if (nickIsServices(nick, 0)) {
                notice_lang(s_BotServ, u, BOT_BOT_CREATION_FAILED);
                return MOD_CONT;
            }

            /* We check whether the nick is registered, and inform the user
             * if so. You need to drop the nick manually before you can use
             * it as a bot nick from now on -GD
             */
            if ((na = findnick(nick))) {
                notice_lang(s_BotServ, u, NICK_ALREADY_REGISTERED, nick);
                return MOD_CONT;
            }

            bi = new BotInfo(nick);
            if (!bi) {
                notice_lang(s_BotServ, u, BOT_BOT_CREATION_FAILED);
                return MOD_CONT;
            }

            bi->user = sstrdup(user);
            bi->host = sstrdup(host);
            bi->real = sstrdup(real);
            bi->created = time(NULL);
            bi->chancount = 0;

            /* We check whether user with this nick is online, and kill it if so */
            EnforceQlinedNick(nick, s_BotServ);

            /* We make the bot online, ready to serve */
            ircdproto->SendClientIntroduction(bi->nick, bi->user, bi->host, bi->real,
                               ircd->pseudoclient_mode, bi->uid.c_str());

            notice_lang(s_BotServ, u, BOT_BOT_ADDED, bi->nick, bi->user,
                        bi->host, bi->real);

            send_event(EVENT_BOT_CREATE, 1, bi->nick);
        }
    } else if (!stricmp(cmd, "CHANGE")) {
        char *oldnick = strtok(NULL, " ");
        char *nick = strtok(NULL, " ");
        char *user = strtok(NULL, " ");
        char *host = strtok(NULL, " ");
        char *real = strtok(NULL, "");

        if (!oldnick || !nick)
            syntax_error(s_BotServ, u, "BOT", BOT_BOT_SYNTAX);
        else if (readonly)
            notice_lang(s_BotServ, u, BOT_BOT_READONLY);
        else if (!(bi = findbot(oldnick)))
            notice_lang(s_BotServ, u, BOT_DOES_NOT_EXIST, oldnick);
        else if (strlen(nick) > NickLen)
            notice_lang(s_BotServ, u, BOT_BAD_NICK);
        else if (user && strlen(user) >= USERMAX)
            notice_lang(s_BotServ, u, BOT_LONG_IDENT, USERMAX - 1);
        else if (host && strlen(host) > HOSTMAX)
            notice_lang(s_BotServ, u, BOT_LONG_HOST, HOSTMAX);
        else {
            NickAlias *na;

            /* Checks whether there *are* changes.
             * Case sensitive because we may want to change just the case.
             * And we must finally check that the nick is not already
             * taken by another bot.
             */
            if (!strcmp(bi->nick, nick)
                && ((user) ? !strcmp(bi->user, user) : 1)
                && ((host) ? !strcmp(bi->host, host) : 1)
                && ((real) ? !strcmp(bi->real, real) : 1)) {
                notice_lang(s_BotServ, u, BOT_BOT_ANY_CHANGES);
                return MOD_CONT;
            }

            /* Check whether it's a services client's nick and return if so - Certus */
            /* use nickIsServices() to reduce the number of lines of code  - TSL */
            if (nickIsServices(nick, 0)) {
                notice_lang(s_BotServ, u, BOT_BOT_CREATION_FAILED);
                return MOD_CONT;
            }

               /**
		 * Check the nick is valid re RFC 2812
		 **/
            if (isdigit(nick[0]) || nick[0] == '-') {
                notice_lang(s_BotServ, u, BOT_BAD_NICK);
                return MOD_CONT;
            }
            for (ch = nick; *ch && (ch - nick) < NICKMAX; ch++) {
                if (!isvalidnick(*ch)) {
                    notice_lang(s_BotServ, u, BOT_BAD_NICK);
                    return MOD_CONT;
                }
            }

            /* check for hardcored ircd forbidden nicks */
            if (!ircdproto->IsNickValid(nick)) {
                notice_lang(s_BotServ, u, BOT_BAD_NICK);
                return MOD_CONT;
            }

            if (host && !isValidHost(host, 3)) {
                notice_lang(s_BotServ, u, BOT_BAD_HOST);
                return MOD_CONT;
            }

            if (user) {
                for (ch = user; *ch && (ch - user) < USERMAX; ch++) {
                    if (!isalnum(*ch)) {
                        notice_lang(s_BotServ, u, BOT_LONG_IDENT, USERMAX - 1);
                        return MOD_CONT;
                    }
                }
            }

            if (stricmp(bi->nick, nick) && findbot(nick)) {
                notice_lang(s_BotServ, u, BOT_BOT_ALREADY_EXISTS, nick);
                return MOD_CONT;
            }

            if (stricmp(bi->nick, nick)) {
                /* We check whether the nick is registered, and inform the user
                 * if so. You need to drop the nick manually before you can use
                 * it as a bot nick from now on -GD
                 */
                if ((na = findnick(nick))) {
                    notice_lang(s_BotServ, u, NICK_ALREADY_REGISTERED,
                                nick);
                    return MOD_CONT;
                }

                /* The new nick is really different, so we remove the Q line for
                   the old nick. */
                if (ircd->sqline) {
                    ircdproto->SendSQLineDel(bi->nick);
                }

                /* We check whether user with this nick is online, and kill it if so */
                EnforceQlinedNick(nick, s_BotServ);
            }

            if (strcmp(nick, bi->nick))
                bi->ChangeNick(nick);

            if (user && strcmp(user, bi->user)) {
                free(bi->user);
                bi->user = sstrdup(user);
            }
            if (host && strcmp(host, bi->host)) {
                free(bi->host);
                bi->host = sstrdup(host);
            }
            if (real && strcmp(real, bi->real)) {
                free(bi->real);
                bi->real = sstrdup(real);
            }

            /* If only the nick changes, we just make the bot change his nick,
               else we must make it quit and rejoin. We must not forget to set
			   the Q:Line either (it's otherwise set in SendClientIntroduction) */
            if (!user) {
                ircdproto->SendChangeBotNick(bi, bi->nick);
				ircdproto->SendSQLine(bi->nick, "Reserved for services");
            } else {
                ircdproto->SendQuit(bi, "Quit: Be right back");

                ircdproto->SendClientIntroduction(bi->nick, bi->user, bi->host, bi->real,
                                   ircd->pseudoclient_mode, bi->uid.c_str());
                bi->RejoinAll();
            }

            notice_lang(s_BotServ, u, BOT_BOT_CHANGED, oldnick, bi->nick,
                        bi->user, bi->host, bi->real);

            send_event(EVENT_BOT_CHANGE, 1, bi->nick);
        }
    } else if (!stricmp(cmd, "DEL")) {
		char *nick = strtok(NULL, " ");

		if (!nick)
		{
			syntax_error(s_BotServ, u, "BOT", BOT_BOT_SYNTAX);
			return MOD_CONT;
		}
		
		if (readonly)
		{
			notice_lang(s_BotServ, u, BOT_BOT_READONLY);
			return MOD_CONT;
		}
		
		if (!(bi = findbot(nick)))
		{
			notice_lang(s_BotServ, u, BOT_DOES_NOT_EXIST, nick);
			return MOD_CONT;
		}

		if (nickIsServices(nick, 0))
		{
			notice_lang(s_BotServ, u, BOT_DOES_NOT_EXIST);
			return MOD_CONT;
		}

		send_event(EVENT_BOT_DEL, 1, bi->nick);
		ircdproto->SendQuit(bi, "Quit: Help! I'm being deleted by %s!", u->nick);
		ircdproto->SendSQLineDel(bi->nick);
		delete bi;
		notice_lang(s_BotServ, u, BOT_BOT_DELETED, nick);
    } else
        syntax_error(s_BotServ, u, "BOT", BOT_BOT_SYNTAX);

    return MOD_CONT;
}


MODULE_INIT("bs_bot", BSBot)
