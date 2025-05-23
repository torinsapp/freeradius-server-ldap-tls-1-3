/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * $Id$
 * @file proto_ldap_sync.c
 * @brief LDAP sync protocol handler.
 *
 * @copyright 2022 Network RADIUS SAS (legal@networkradius.com)
 */
#define LOG_PREFIX "proto_ldap_sync"

#include <freeradius-devel/internal/internal.h>
#include <freeradius-devel/io/listen.h>
#include <freeradius-devel/server/module.h>

#include "proto_ldap_sync.h"

#include <fcntl.h>

static fr_internal_encode_ctx_t	encode_ctx = { .allow_name_only = true };

extern fr_app_t proto_ldap_sync;

static conf_parser_t const ldap_sync_search_config[] = {
	{ FR_CONF_OFFSET("base_dn", sync_config_t, base_dn), .dflt = "", .quote = T_SINGLE_QUOTED_STRING },

	{ FR_CONF_OFFSET("filter", sync_config_t, filter) },

	{ FR_CONF_OFFSET("scope", sync_config_t, scope_str), .dflt = "sub" },
	/* For persistent search directories, setting this to "no" will load the whole directory. */
	{ FR_CONF_OFFSET("changes_only", sync_config_t, changes_only), .dflt = "yes" },

	CONF_PARSER_TERMINATOR
};

static conf_parser_t const proto_ldap_sync_config[] = {
	{ FR_CONF_OFFSET_TYPE_FLAGS("transport", FR_TYPE_VOID, 0, proto_ldap_sync_t, io_submodule),
	  .func = virtual_server_listen_transport_parse },

	{ FR_CONF_OFFSET("max_packet_size", proto_ldap_sync_t, max_packet_size) },
	{ FR_CONF_OFFSET("num_messages", proto_ldap_sync_t, num_messages) },
	{ FR_CONF_OFFSET("cookie_interval", proto_ldap_sync_t, cookie_interval), .dflt = "10" },
	{ FR_CONF_OFFSET("cookie_changes", proto_ldap_sync_t, cookie_changes), .dflt = "100" },
	{ FR_CONF_OFFSET("retry_interval", proto_ldap_sync_t, retry_interval), .dflt = "1" },

	/*
	 *	Areas of the DIT to listen on
	 */
	{ FR_CONF_SUBSECTION_ALLOC("sync", 0, CONF_FLAG_SUBSECTION | CONF_FLAG_MULTI | CONF_FLAG_REQUIRED, proto_ldap_sync_t, sync_config, ldap_sync_search_config) },

	CONF_PARSER_TERMINATOR
};

static fr_dict_t const *dict_ldap_sync;

extern fr_dict_autoload_t proto_ldap_sync_dict[];
fr_dict_autoload_t proto_ldap_sync_dict[] = {
	{ .out = &dict_ldap_sync, .proto = "ldap" },
	{ NULL }
};

static fr_dict_attr_t const *attr_ldap_sync_packet_id;
static fr_dict_attr_t const *attr_ldap_sync;
static fr_dict_attr_t const *attr_ldap_sync_cookie;
static fr_dict_attr_t const *attr_ldap_sync_dn;
static fr_dict_attr_t const *attr_ldap_sync_scope;
static fr_dict_attr_t const *attr_ldap_sync_filter;
static fr_dict_attr_t const *attr_packet_type;

extern fr_dict_attr_autoload_t proto_ldap_sync_dict_attr[];
fr_dict_attr_autoload_t proto_ldap_sync_dict_attr[] = {
	{ .out = &attr_ldap_sync_packet_id, .name = "Sync-Packet-ID", .type = FR_TYPE_UINT32, .dict = &dict_ldap_sync },
	{ .out = &attr_ldap_sync, .name = "LDAP-Sync", .type = FR_TYPE_TLV, .dict = &dict_ldap_sync },
	{ .out = &attr_ldap_sync_cookie, .name = "LDAP-Sync.Cookie", .type = FR_TYPE_OCTETS, .dict = &dict_ldap_sync },
	{ .out = &attr_ldap_sync_dn, .name = "LDAP-Sync.DN", .type = FR_TYPE_STRING, .dict = &dict_ldap_sync },
	{ .out = &attr_ldap_sync_scope, .name = "LDAP-Sync.Scope", .type = FR_TYPE_UINT32, .dict = &dict_ldap_sync },
	{ .out = &attr_ldap_sync_filter, .name = "LDAP-Sync.Filter", .type = FR_TYPE_STRING, .dict = &dict_ldap_sync },
	{ .out = &attr_packet_type, .name = "Packet-Type", .type = FR_TYPE_UINT32, .dict = &dict_ldap_sync },
	{ NULL }
};

/** Check if an attribute is in the config list and add if not present
 *
 * @param[in,out] config	to check for attribute.
 * @param[in] attr		to look for.
 * @return
 *	- 1 if attr is added
 *	- 0 if attr was already present
 */
int ldap_sync_conf_attr_add(sync_config_t *config, char const * attr)
{
	char	**tmp;
	size_t	len;

	if (fr_ldap_attrs_check(config->attrs, attr)) return 0;

	len = talloc_array_length(config->attrs);

	config->attrs[len - 1] = talloc_strdup(config, attr);
	tmp = (char **)talloc_array_null_terminate(UNCONST(void **, config->attrs));
	memcpy(&config->attrs, &tmp, sizeof(config->attrs));

	return 1;
}

/** Decode an internal LDAP sync packet
 *
 */
static int mod_decode(UNUSED void const *instance, request_t *request, uint8_t *const data, size_t data_len)
{
	fr_dbuff_t			dbuff;
	ssize_t				ret;
	fr_pair_t			*vp = NULL;

	fr_assert(request->proto_dict == dict_ldap_sync);

	fr_dbuff_init(&dbuff, data, data_len);

	/*
	 *	Extract attributes from the passed data
	 */
	ret = fr_internal_decode_list_dbuff(request->pair_list.request, &request->request_pairs,
					   fr_dict_root(request->proto_dict), &dbuff, NULL);
	if (ret < 0) return -1;

	vp = fr_pair_find_by_da(&request->request_pairs, NULL, attr_packet_type);
	fr_assert(vp);
	request->packet->code = vp->vp_uint32;

	vp = fr_pair_find_by_da(&request->request_pairs, NULL, attr_ldap_sync_packet_id);
	fr_assert(vp);
	request->packet->id = vp->vp_uint32;
	request->reply->id = vp->vp_uint32;

	return 0;
}

/** Encode responses to processing LDAP sync sections
 *
 */
static ssize_t mod_encode(UNUSED void const *instance, request_t *request, uint8_t *buffer, size_t buffer_len)
{
	fr_dbuff_t	dbuff;
	fr_pair_t	*sync_vp = NULL, *vp = NULL;
	fr_pair_list_t	pairs;
	TALLOC_CTX	*local = NULL;

	fr_dbuff_init(&dbuff, buffer, buffer_len);
	local = talloc_new(NULL);
	fr_pair_list_init(&pairs);

	fr_pair_list_append_by_da(local, vp, &pairs, attr_packet_type, request->reply->code, false);
	if (!vp) {
	error:
		talloc_free(local);
		return -1;
	}
	fr_pair_list_append_by_da(local, vp, &pairs, attr_ldap_sync_packet_id, (uint32_t)request->reply->id, false);
	if (!vp) goto error;

	/*
	 *	Only Cookie Load Response has extra data sent - the cookie (if defined)
	 */
	if (request->reply->code != FR_LDAP_SYNC_CODE_COOKIE_LOAD_RESPONSE) goto send;

	sync_vp = fr_pair_find_by_da(&request->reply_pairs, NULL, attr_ldap_sync);
	if (!sync_vp) goto send;
	vp = fr_pair_find_by_da(&sync_vp->vp_group, NULL, attr_ldap_sync_cookie);
	if ((vp) && (vp->data.vb_length > 0)) {
		fr_pair_remove(&request->reply_pairs, sync_vp);
		fr_pair_steal_append(local, &pairs, sync_vp);
	}

send:
	if (fr_internal_encode_list(&dbuff, &pairs, &encode_ctx) < 0) goto error;
	talloc_free(local);

	return fr_dbuff_used(&dbuff);
}

static int mod_open(void *instance, fr_schedule_t *sc, UNUSED CONF_SECTION *conf)
{
	proto_ldap_sync_t	*inst = talloc_get_type_abort(instance, proto_ldap_sync_t);
	fr_listen_t		*li;

	/*
	 *	Build the #fr_listen_t.
	 */
	MEM(li = talloc_zero(inst, fr_listen_t));
	talloc_set_destructor(li, fr_io_listen_free);

	li->app_io = inst->app_io;
	li->thread_instance = talloc_zero_array(NULL, uint8_t, li->app_io->common.thread_inst_size);
	talloc_set_name(li->thread_instance, "proto_%s_thread_t", inst->app_io->common.name);
	li->app_io_instance = inst->app_io_instance;
	li->name = "ldap_sync main listener";

	li->app = &proto_ldap_sync;
	li->app_instance = instance;
	li->server_cs = inst->server_cs;
	li->non_socket_listener = true;

	/*
	 *	Set configurable parameters for message ring buffer.
	 */
	li->default_message_size = inst->max_packet_size;
	li->num_messages = inst->num_messages;

	if (!fr_schedule_listen_add(sc, li)) {
		talloc_free(li);
		return -1;
	}

	inst->listen = li;
	inst->sc = sc;

	return 0;
}

static int mod_instantiate(module_inst_ctx_t const *mctx)
{
	proto_ldap_sync_t	*inst = talloc_get_type_abort(mctx->mi->data, proto_ldap_sync_t);
	CONF_SECTION		*conf = mctx->mi->conf;
	CONF_SECTION		*sync_cs;
	sync_config_t		*sync_conf;
	size_t			i;
	fr_pair_t		*vp;
	CONF_SECTION		*map_cs;
	map_t			*map;
	tmpl_rules_t		parse_rules = {
	 	/* Strict rules for the update map as it's processed with limited functionality */
		.attr = {
			.dict_def = dict_ldap_sync,
			.list_def = request_attr_request,
			.allow_foreign = false,
			.allow_unknown = false,
			.allow_unresolved = false,
			.list_presence = TMPL_ATTR_LIST_FORBID,
		}
	};

	inst->server_cs = cf_item_to_section(cf_parent(conf));
	inst->cs = conf;
	inst->self = &proto_ldap_sync;

	if (!inst->io_submodule) {
		cf_log_err(conf, "Virtual server for LDAP sync requires a 'transport' configuration");
		return -1;
	}

	/*
	 *	Bootstrap the I/O module
	 */
	inst->app_io = (fr_app_io_t const *) inst->io_submodule->exported;
	inst->app_io_instance = inst->io_submodule->data;
	inst->app_io_conf = inst->io_submodule->conf;

	/*
	 *	These configuration items are not printed by default,
	 *	because normal people shouldn't be touching them.
	 */
	if (!inst->max_packet_size) inst->max_packet_size = inst->app_io->default_message_size;
	if (!inst->num_messages) inst->num_messages = 2;

	FR_INTEGER_BOUND_CHECK("num_messages", inst->num_messages, >=, 2);
	FR_INTEGER_BOUND_CHECK("num_messages", inst->num_messages, <=, 65535);

	FR_INTEGER_BOUND_CHECK("max_packet_size", inst->max_packet_size, >=, 1024);
	FR_INTEGER_BOUND_CHECK("max_packet_size", inst->max_packet_size, <=, 65536);

	if (!inst->priority) inst->priority = PRIORITY_NORMAL;

	/*
	 *	Parse each of the sync sections
	 */
	for (sync_cs = cf_section_find(conf, "sync", NULL), i = 0;
	     sync_cs;
	     sync_cs = cf_section_find_next(conf, sync_cs, "sync", NULL), i++) {
		sync_conf = inst->sync_config[i];
		sync_conf->cs = sync_cs;

		/*
		 *	Convert scope string to enumerated constant
		 */
		sync_conf->scope = fr_table_value_by_str(fr_ldap_scope, sync_conf->scope_str, -1);
		if (sync_conf->scope < 0) {
			cf_log_err(conf, "Invalid 'search.scope' value \"%s\", expected 'sub', 'one', 'base' or 'children'",
				   sync_conf->scope_str);
			return -1;
		}

		map_cs = cf_section_find(sync_cs, "update", NULL);
		map_list_init(&sync_conf->entry_map);
		if (map_cs && map_afrom_cs(inst, &sync_conf->entry_map, map_cs,
					   &parse_rules, &parse_rules, fr_ldap_map_verify, NULL,
					   LDAP_MAX_ATTRMAP) < 0) {
			return -1;
		}

		/*
		 *	Initialise a NULL terminated list of attributes
		 */
		sync_conf->attrs = talloc_array(sync_conf, char const *, 1);
		sync_conf->attrs[0] = NULL;

		if (map_list_empty(&sync_conf->entry_map)) {
			cf_log_warn(conf, "LDAP sync specified without update map");
			continue;
		}

		/*
		 *	Build the required list of attributes from the update map,
		 *	checking validity as we go.
		 */
		map = NULL;
		while ((map = map_list_next(&sync_conf->entry_map, map))) {
			if (fr_type_is_structural(tmpl_attr_tail_da(map->lhs)->type)) {
				cf_log_err(map->ci, "Structural attribute \"%s\" invalid for LDAP sync update",
					    tmpl_attr_tail_da(map->lhs)->name);
				return -1;
			}

			switch(map->op) {
			case T_OP_EQ:
			case T_OP_ADD_EQ:
				break;

			default:
				cf_log_err(map->ci, "Operator \"%s\" invalid for LDAP sync update",
					    fr_table_str_by_value(fr_tokens_table, map->op, "<INVALID>"));
				return -1;
			}

			DEBUG3("Adding %s to attribute list", map->rhs->name);
			ldap_sync_conf_attr_add(sync_conf, map->rhs->name);
		}

		/*
		 *	Build the list of pairs representing the sync config
		 */
		fr_pair_list_init(&sync_conf->sync_pairs);

		fr_pair_list_append_by_da_parent_len(sync_conf, vp, &sync_conf->sync_pairs, attr_ldap_sync_dn,
						     sync_conf->base_dn, strlen(sync_conf->base_dn), false);
		if (!vp) return -1;

		fr_pair_list_append_by_da_parent(sync_conf, vp, &sync_conf->sync_pairs, attr_ldap_sync_scope,
						 (uint32_t)sync_conf->scope, false);
		if (!vp) return -1;

		if (sync_conf->filter) {
			fr_pair_list_append_by_da_parent_len(sync_conf, vp, &sync_conf->sync_pairs, attr_ldap_sync_filter,
							     sync_conf->filter, strlen(sync_conf->filter), false);
			if (!vp) return -1;
		}
	}

	return 0;
}

fr_app_t proto_ldap_sync = {
	.common = {
		.magic		= MODULE_MAGIC_INIT,
		.name		= "ldap_sync",
		.config		= proto_ldap_sync_config,
		.inst_size	= sizeof(proto_ldap_sync_t),
		.instantiate	= mod_instantiate
	},

	.dict		= &dict_ldap_sync,

	.open		= mod_open,
	.decode		= mod_decode,
	.encode		= mod_encode,
};
