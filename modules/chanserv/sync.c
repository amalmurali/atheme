/*
 * Copyright (c) 2006 Atheme Development Group
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService SYNC functions.
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"chanserv/sync", false, _modinit, _moddeinit,
	PACKAGE_STRING,
	"Atheme Development Group <http://www.atheme.org>"
);

static void cs_cmd_sync(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_sync = { "SYNC", "Forces channel statuses to flags.",
                        AC_NONE, 1, cs_cmd_sync, { .path = "contrib/sync" } };

static void do_channel_sync(mychan_t *mc)
{
	char akickreason[120] = "User is banned from this channel", *p;
	chanuser_t *cu;
	mowgli_node_t *n, *tn;
	int fl;
	bool noop;

	return_if_fail(mc != NULL);
	if (mc->chan == NULL)
		return;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, mc->chan->members.head)
	{
		cu = (chanuser_t *)n->data;

		if (is_internal_client(cu->user))
			continue;

		fl = chanacs_user_flags(mc, cu->user);
		noop = mc->flags & MC_NOOP || (cu->user->myuser != NULL &&
				cu->user->myuser->flags & MU_NOOP);

		if (fl & CA_AKICK && !(fl & CA_REMOVE))
		{
			chanacs_t *ca2;
			metadata_t *md;

			/* Stay on channel if this would empty it -- jilles */
			if (mc->chan->nummembers <= (mc->flags & MC_GUARD ? 2 : 1))
			{
				mc->flags |= MC_INHABIT;
				if (!(mc->flags & MC_GUARD))
					join(mc->chan->name, chansvs.nick);
			}

			/* use a user-given ban mask if possible -- jilles */
			ca2 = chanacs_find_host_by_user(mc, cu->user, CA_AKICK);
			if (ca2 != NULL)
			{
				if (chanban_find(mc->chan, ca2->host, 'b') == NULL)
				{
					char str[512];

					chanban_add(mc->chan, ca2->host, 'b');
					snprintf(str, sizeof str, "+b %s", ca2->host);
					/* ban immediately */
					mode_sts(chansvs.nick, mc->chan, str);
				}
			}
			else if (cu->user->myuser != NULL)
			{
				/* XXX this could be done more efficiently */
				ca2 = chanacs_find(mc, entity(cu->user->myuser), CA_AKICK);
				ban(chansvs.me->me, mc->chan, cu->user);
			}

			remove_ban_exceptions(chansvs.me->me, mc->chan, cu->user);
			if (ca2 != NULL)
			{
				md = metadata_find(ca2, "reason");
				if (md != NULL && *md->value != '|')
				{
					snprintf(akickreason, sizeof akickreason,
							"Banned: %s", md->value);
					p = strchr(akickreason, '|');
					if (p != NULL)
						*p = '\0';
					else
						p = akickreason + strlen(akickreason);

					/* strip trailing spaces, so as not to
					 * disclose the existence of an oper reason */
					p--;
					while (p > akickreason && *p == ' ')
						p--;
					p[1] = '\0';
				}
			}

			try_kick(chansvs.me->me, mc->chan, cu->user, akickreason);
			continue;
		}
		if (ircd->uses_owner)
		{
			if (fl & CA_USEOWNER)
			{
				if (!noop && fl & CA_AUTOOP && !(ircd->owner_mode & cu->modes))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->owner_mchar[1], CLIENT_NAME(cu->user));
					cu->modes |= ircd->owner_mode;
				}
			}
			else if (ircd->owner_mode & cu->modes)
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, ircd->owner_mchar[1], CLIENT_NAME(cu->user));
				cu->modes &= ~ircd->owner_mode;
			}
		}
		if (ircd->uses_protect)
		{
			if (fl & CA_USEPROTECT)
			{
				if (!noop && fl & CA_AUTOOP && !(ircd->protect_mode & cu->modes) && !(ircd->uses_owner && cu->modes & ircd->owner_mode))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->protect_mchar[1], CLIENT_NAME(cu->user));
					cu->modes |= ircd->protect_mode;
				}
			}
			else if (ircd->protect_mode & cu->modes)
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, ircd->protect_mchar[1], CLIENT_NAME(cu->user));
				cu->modes &= ~ircd->protect_mode;
			}
		}
		if (fl & (CA_AUTOOP | CA_OP))
		{
			if (!noop && fl & CA_AUTOOP && !(CSTATUS_OP & cu->modes))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'o', CLIENT_NAME(cu->user));
				cu->modes |= CSTATUS_OP;
			}
			continue;
		}
		if ((CSTATUS_OP & cu->modes))
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, 'o', CLIENT_NAME(cu->user));
			cu->modes &= ~CSTATUS_OP;
		}
		if (ircd->uses_halfops)
		{
			if (fl & (CA_AUTOHALFOP | CA_HALFOP))
			{
				if (!noop && fl & CA_AUTOHALFOP && !(ircd->halfops_mode & cu->modes))
				{
					modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, ircd->halfops_mchar[1], CLIENT_NAME(cu->user));
					cu->modes |= ircd->halfops_mode;
				}
				continue;
			}
			if (ircd->halfops_mode & cu->modes)
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, ircd->halfops_mchar[1], CLIENT_NAME(cu->user));
				cu->modes &= ~ircd->halfops_mode;
			}
		}
		if (fl & (CA_AUTOVOICE | CA_VOICE))
		{
			if (!noop && fl & CA_AUTOVOICE && !(CSTATUS_VOICE & cu->modes))
			{
				modestack_mode_param(chansvs.nick, mc->chan, MTYPE_ADD, 'v', CLIENT_NAME(cu->user));
				cu->modes |= CSTATUS_VOICE;
			}
			continue;
		}
		if ((CSTATUS_VOICE & cu->modes))
		{
			modestack_mode_param(chansvs.nick, mc->chan, MTYPE_DEL, 'v', CLIENT_NAME(cu->user));
			cu->modes &= ~CSTATUS_VOICE;
		}
	}
}

static void sync_channel_acl_change(chanacs_t *ca)
{
	mychan_t *mc;

	mc = ca->mychan;
	return_if_fail(mc != NULL);

	if (MC_NOSYNC & mc->flags)
		return;

	do_channel_sync(mc);
}

static void cs_cmd_sync(sourceinfo_t *si, int parc, char *parv[])
{
	char *name = parv[0];
	mychan_t *mc;

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "SYNC");
		command_fail(si, fault_needmoreparams, "Syntax: SYNC <#channel>");
		return;
	}

	if (!(mc = mychan_find(name)))
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 is not registered.", name);
		return;
	}
	
	if (metadata_find(mc, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, "\2%s\2 is closed.", name);
		return;
	}

	if (!mc->chan)
	{
		command_fail(si, fault_nosuch_target, "\2%s\2 does not exist.", name);
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_RECOVER))
	{
		command_fail(si, fault_noprivs, "You are not authorized to perform this operation.");
		return;
	}

	verbose(mc, "\2%s\2 used SYNC.", get_source_name(si));
	logcommand(si, CMDLOG_SET, "SYNC: \2%s\2", mc->name);

	do_channel_sync(mc);

	command_success_nodata(si, "Sync complete for \2%s\2.", mc->name);
}

static void cs_cmd_set_nosync(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_set_nosync = { "NOSYNC", N_("Disables automatic channel ACL syncing."), AC_NONE, 2, cs_cmd_set_nosync, { .path = "cservice/set_nosync" } };

mowgli_patricia_t **cs_set_cmdtree;

static void cs_cmd_set_nosync(sourceinfo_t *si, int parc, char *parv[])
{
	mychan_t *mc;

	if (!(mc = mychan_find(parv[0])))
	{
		command_fail(si, fault_nosuch_target, _("Channel \2%s\2 is not registered."), parv[0]);
		return;
	}

	if (!parv[1])
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "SET NOSYNC");
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_SET))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this command."));
		return;
	}

	if (!strcasecmp("ON", parv[1]))
	{
		if (MC_NOSYNC & mc->flags)
		{
			command_fail(si, fault_nochange, _("The \2%s\2 flag is already set for channel \2%s\2."), "NOSYNC", mc->name);
			return;
		}

		logcommand(si, CMDLOG_SET, "SET:NOSYNC:ON: \2%s\2", mc->name);

		mc->flags |= MC_NOSYNC;

		command_success_nodata(si, _("The \2%s\2 flag has been set for channel \2%s\2."), "NOSYNC", mc->name);
		return;
	}
	else if (!strcasecmp("OFF", parv[1]))
	{
		if (!(MC_NOSYNC & mc->flags))
		{
			command_fail(si, fault_nochange, _("The \2%s\2 flag is not set for channel \2%s\2."), "NOSYNC", mc->name);
			return;
		}

		logcommand(si, CMDLOG_SET, "SET:NOSYNC:OFF: \2%s\2", mc->name);

		mc->flags &= ~MC_NOSYNC;

		command_success_nodata(si, _("The \2%s\2 flag has been removed for channel \2%s\2."), "NOSYNC", mc->name);
		return;
	}
	else
	{
		command_fail(si, fault_badparams, STR_INVALID_PARAMS, "NOSYNC");
		return;
	}
}

void _modinit(module_t *m)
{
	MODULE_TRY_REQUEST_SYMBOL(m, cs_set_cmdtree, "chanserv/set_core", "cs_set_cmdtree");
	service_named_bind_command("chanserv", &cs_sync);

	command_add(&cs_set_nosync, *cs_set_cmdtree);

	hook_add_event("channel_acl_change");
	hook_add_channel_acl_change(sync_channel_acl_change);
}

void _moddeinit()
{
	hook_del_channel_acl_change(sync_channel_acl_change);

	service_named_unbind_command("chanserv", &cs_sync);
	command_delete(&cs_set_nosync, *cs_set_cmdtree);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */