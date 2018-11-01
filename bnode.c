#include <stdio.h>
#include <assert.h>
#include <json-c/json.h>

#include "bnode.h"
#include "msg.h"
#include "crypto.h"
#include "pmsg.h"
#include "test/msg.h"

//#define DEBUG

#include "log.h"

enum tag_t
{
	TAG_LIST_EMPTY      = 0,
	TAG_STREAM_END      = 2,
	TAG_DICTIONARY_0    = 236,
	TAG_DICTIONARY_1    = 237,
	TAG_DICTIONARY_2    = 238,
	TAG_DICTIONARY_3    = 239,
	TAG_LIST_8          = 248,
	TAG_LIST_16         = 249,
	TAG_JID_PAIR        = 250,
	TAG_HEX_8           = 251,
	TAG_BINARY_8        = 252,
	TAG_BINARY_20       = 253,
	TAG_BINARY_32       = 254,
	TAG_NIBBLE_8        = 255,
	TAG_SINGLE_BYTE_MAX = 256,
	TAG_PACKED_MAX      = 254,
};

char *token_table[] = {
	NULL,NULL,NULL,"200","400","404","500","501","502","action","add",
	"after","archive","author","available","battery","before","body",
	"broadcast","chat","clear","code","composing","contacts","count",
	"create","debug","delete","demote","duplicate","encoding","error",
	"false","filehash","from","g.us","group","groups_v2","height","id",
	"image","in","index","invis","item","jid","kind","last","leave",
	"live","log","media","message","mimetype","missing","modify","name",
	"notification","notify","out","owner","participant","paused",
	"picture","played","presence","preview","promote","query","raw",
	"read","receipt","received","recipient","recording","relay",
	"remove","response","resume","retry",/*"s.whatsapp.net"*/"c.us","seconds",
	"set","size","status","subject","subscribe","t","text","to","true",
	"type","unarchive","unavailable","url","user","value","web","width",
	"mute","read_only","admin","creator","short","update","powersave",
	"checksum","epoch","block","previous","409","replaced","reason",
	"spam","modify_tag","message_info","delivery","emoji","title",
	"description","canonical-url","matched-text","star","unstar",
	"media_key","filename","identity","unread","page","page_count",
	"search","media_message","security","call_log","profile","ciphertext",
	"invite","gif","vcard","frequent","privacy","blacklist","whitelist",
	"verify","location","document","elapsed","revoke_invite","expiration",
	"unsubscribe","disable","vname","old_jid","new_jid","announcement",
	"locked","prop","label","color","call","offer","call-id"
};

typedef struct {
	unsigned char *start;
	unsigned char *ptr;
	unsigned char *end;
} parser_t;

enum bnode_type {
	BNODE_EMPTY = 0,
	BNODE_STRING,
	BNODE_INT,
	BNODE_LIST,
	BNODE_BINARY
};

struct bnode_t {
	char *desc;

	struct json_object *attr;

	int len;

	enum bnode_type type;
	union content {
		struct bnode_t **list;
		char *bytes;
		int number;
		char *str;
	} data;
};

typedef struct bnode_t bnode_t;

char nibble_table[] = {
	'0','1','2','3','4','5','6','7','8','9','-','.','\0','\0','\0','\0',
};
char hex_table[] = {
	'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
};



bnode_t *
read_bnode(parser_t *p);

char *
read_string(parser_t *p, int tag);

int
read_int(parser_t *p, size_t len)
{
	int i, v = 0;

	assert((0 < len) && (len <= 4));
	assert(p->ptr + len <= p->end);

	v = p->ptr[0];
	LOG_INFO("v=%02X (%d) len=%ld\n", v, v, len);

	for(i=1; i < len; i++)
	{
		v = v << 8;
		v |= p->ptr[i];
	}

	p->ptr += len;

	return v;
}

int
list_size(parser_t *p, int tag)
{
	switch(tag)
	{
		case TAG_LIST_EMPTY:
			return 0;
		case TAG_LIST_8:
			return read_int(p, 1);
		case TAG_LIST_16:
			return read_int(p, 2);
		default:
			LOG_ERR("Invalid tag %d for list at %p\n",
					tag, p->ptr);
			return -1;
	}

	/* Not reached */
	return -2;
}

char *
read_bytes(parser_t *p, int len)
{
	LOG_INFO("len = %d\n", len);
	assert(p->ptr + len <= p->end);

	char *s = malloc(len + 1);
	assert(s);

	memcpy(s, p->ptr, len);
	s[len] = '\0';

	p->ptr += len;

	/* We assume a NULL terminated string is wanted here */

	return s;
}

char *
read_packed(parser_t *p, char table[])
{
	int i, j, start, bytes, len, b, nibble;
	char *buf;

	start = read_int(p, 1);

	bytes = start & 0x7F;
	len = bytes * 2;
	buf = malloc(len + 1);
	assert(buf);

	j=0;

	for(i=0; i < bytes; i++)
	{
		b = read_int(p, 1);
		nibble = (b & 0xF0) >> 4;
		buf[j++] = nibble_table[nibble];
		nibble = b & 0x0F;
		buf[j++] = nibble_table[nibble];
	}

	/* The original code has this part switched, but it makes no sense */
	if(start & 0x80)
		len--;

	buf[len] = '\0';

	return buf;
}

char *
read_nibbles(parser_t *p)
{
	return read_packed(p, nibble_table);
}

char *
read_hex(parser_t *p)
{
	return read_packed(p, hex_table);
}

char *
read_jid_pair(parser_t *p)
{
	int len;
	char *user, *server, *jid;

	user = read_string(p, read_int(p, 1));
	assert(user);

	server = read_string(p, read_int(p, 1));
	assert(server);

	len = strlen(user) + 1 + strlen(server);
	jid = malloc(len + 1);
	assert(jid);

	strcpy(jid, user);
	strcat(jid, "@");
	strcat(jid, server);

	assert(jid[len] == '\0');

	free(user);
	free(server);

	return jid;
}

char *
read_string(parser_t *p, int tag)
{
	/*
	 * By some reason, the server parts gets rewritten:
	 *
	 * 	char *token = token_table[tag];
	 *	if(strcmp(token, "s.whatsapp.net") == 0)
	 *		return "c.us";
	 *	return token;
	 *
	 * Neither of both hosts seem to be alive at this time (2018-10-31)
	 *
	 * I simply changed the host in the token_table.
	 */

	if((tag >= 3) && (tag <= 235))
		return strdup(token_table[tag]);

	switch(tag)
	{
		case TAG_JID_PAIR:
			return read_jid_pair(p);

		case TAG_NIBBLE_8:
			return read_nibbles(p);
		case TAG_HEX_8:
			return read_hex(p);

			/* Are these used? */
		case TAG_BINARY_8:
			return read_bytes(p, 1);
		case TAG_BINARY_20:
			return read_bytes(p, 3);
		case TAG_BINARY_32:
			return read_bytes(p, 4);
		case TAG_LIST_EMPTY:
			return NULL;

		case TAG_DICTIONARY_0:
		case TAG_DICTIONARY_1:
		case TAG_DICTIONARY_2:
		case TAG_DICTIONARY_3:
			/* Not implemented */
			assert(0);

			/* Read one byte */
			read_int(p, 1);

			/* This will likely fail, as probably there is more data
			 * to be parsed */
			return "<TAG_DICTIONARY: NOT IMPLEMENTED>";
		default:
			return NULL;
	}

	/* Not reached */
	return NULL;
}

json_object *
read_attr(parser_t *p, int len)
{
	int i;
	char *key, *val;
	json_object *val_json;

	if(len == 0)
		return NULL;

	json_object *obj = json_object_new_object();

	for(i=0; i<len; i++)
	{
		LOG_INFO("--- New attr ---\n");
		key = read_string(p, read_int(p, 1));
		LOG_INFO("key = %s\n", key);
		val = read_string(p, read_int(p, 1));
		LOG_INFO("attr %s : %s\n", key, val);
		val_json = json_object_new_string(val);
		json_object_object_add(obj, key, val_json);
	}

	return obj;
}

int
bnode_string(parser_t *p, bnode_t *bn, char *str)
{
	bn->type = BNODE_STRING;
	bn->len = strlen(str);
	bn->data.str = str;

	return 0;
}

int
bnode_packed(parser_t *p, bnode_t *bn)
{
	char *jid = read_jid_pair(p);
	return bnode_string(p, bn, jid);
}

int
bnode_hex(parser_t *p, bnode_t *bn)
{
	char *str = read_hex(p);
	return bnode_string(p, bn, str);
}

int
bnode_nibbles(parser_t *p, bnode_t *bn)
{
	char *str = read_nibbles(p);
	return bnode_string(p, bn, str);
}

int
bnode_jid_pair(parser_t *p, bnode_t *bn)
{
	char *jid = read_jid_pair(p);
	return bnode_string(p, bn, jid);
}

int
bnode_binary(parser_t *p, bnode_t *bn, int tag)
{
	int len;

	switch(tag)
	{
		case TAG_BINARY_8:
			len = read_int(p, 1);
			break;
		case TAG_BINARY_20:
			/* Why they crop the first 4 bits? No idea */
			len = read_int(p, 3) & 0x000FFFFF;
			break;
		case TAG_BINARY_32:
			len = read_int(p, 4);
			break;
	};

	bn->len = len;
	bn->type = BNODE_BINARY;
	bn->data.bytes = read_bytes(p, len);

	pmsg_unpack(bn->data.bytes, bn->len);

	return 0;
}

int
bnode_list(parser_t *p, bnode_t *bn, int tag)
{
	int i, size;

	size = list_size(p, tag);

	bn->type = BNODE_LIST;
	bn->data.list = (bnode_t **) malloc(sizeof(bnode_t *) * size);
	bn->len = size;

	for(i=0; i<size; i++)
	{
		bn->data.list[i] = read_bnode(p);
	}

	return 0;
}

int
parse_content(parser_t *p, bnode_t *bn, int tag)
{
	switch(tag)
	{
		case TAG_BINARY_8:
		case TAG_BINARY_20:
		case TAG_BINARY_32:
			LOG_INFO("BNODE BINARY\n");
			return bnode_binary(p, bn, tag);
		case TAG_LIST_EMPTY:
		case TAG_LIST_8:
		case TAG_LIST_16:
			LOG_INFO("BNODE LIST\n");
			return bnode_list(p, bn, tag);
		case TAG_JID_PAIR:
			LOG_INFO("BNODE JID\n");
			return bnode_jid_pair(p, bn);
		case TAG_NIBBLE_8:
			LOG_INFO("BNODE NIBBLE\n");
			return bnode_nibbles(p, bn);
		case TAG_HEX_8:
			LOG_INFO("BNODE HEX\n");
			return bnode_hex(p, bn);
		default:
			LOG_ERR("Unknown content tag %d\n", tag);
			return -1;
	}

	/* Not reached */
	return -2;
}

bnode_t *
read_bnode(parser_t *p)
{
	bnode_t *bn;
	int tag, desc_tag, size, attr_len;

	bn = malloc(sizeof(bnode_t));
	assert(bn);

	bn->type = BNODE_EMPTY;

	tag = read_int(p, 1);
	LOG_INFO("TAG:%d\n", tag);
	size = list_size(p, tag);
	LOG_INFO("SIZE:%d\n", size);
	desc_tag = read_int(p, 1);

	if(desc_tag == TAG_STREAM_END)
		return NULL;

	bn->desc = read_string(p, desc_tag);
	LOG_INFO("DESC:%s\n", bn->desc);

	attr_len = (size - 1) / 2;

	LOG_INFO("size=%d, attr_len=%d\n", size, attr_len);

	bn->attr = read_attr(p, attr_len);

	if((size % 2) == 0)
	{
		tag = read_int(p, 1);
		parse_content(p, bn, tag);
	}

	return bn;
}

bnode_t *
msg_to_bnode(msg_t *msg)
{
	parser_t *p;
	bnode_t *bn;

	p = malloc(sizeof(parser_t));
	p->start = msg->cmd;
	p->ptr = p->start;
	p->end = p->start + msg->len;
	LOG_INFO("msg len:%ld\n", msg->len);

	bn = read_bnode(p);
	return bn;
}

int
bnode_print(bnode_t *bn)
{
	int i;

	LOG_INFO("desc:%s\n", bn->desc);
	LOG_INFO("type:%d\n", bn->type);

	/* Print attrs here */

	switch(bn->type)
	{
		case BNODE_EMPTY:
			LOG_INFO("content: empty\n");
			break;
		case BNODE_STRING:
			LOG_INFO("content: %s\n", bn->data.str);
			break;
		case BNODE_LIST:
			LOG_INFO("content: list:\n");
			for(i=0; i < bn->len; i++)
			{
				bnode_print(bn->data.list[i]);
			}
			break;
		case BNODE_BINARY:
			LOG_INFO("content: binary:\n");
#ifdef DEBUG
			hexdump(bn->data.bytes, bn->len);
#endif
			break;
		default:
			LOG_ERR("Unknown bnode type %d\n", bn->type);
			return -1;
	}

	return 0;
}

int
bnode_print_msg(msg_t *msg)
{
	bnode_t *bn = msg_to_bnode(msg);

	return bnode_print(bn);
}

//int
//main()
//{
//	msg_t *msg = malloc(sizeof(msg_t));
//	msg->cmd = msg2;
//	msg->len = msg2_len;
//
//	bnode_print_msg(msg);
//
//	return 0;
//}

