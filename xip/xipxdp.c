#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <net/xia_fib.h>
#include <xia_socket.h>

#include "xip_common.h"
#include "utils.h"
#include "libnetlink.h"
#include "xiart.h"

static int usage(void)
{
	fprintf(stderr,
"Usage:	xip xdp showsockets\n"
"	xip xdp addroute ID gw XID\n"
"	xip xdp delroute ID\n"
"	xip xdp showroutes\n"
"where	ID := HEXDIGIT{20}\n"
"	XID := PRINCIPAL '-' ID\n"
"	PRINCIPAL := '0x' NUMBER | STRING\n");
	return -1;
}

static struct {
	__u32		tb;
	xid_type_t	xid_type;
} filter;

static inline void reset_filter(__u32 tb_id)
{
	filter.tb = tb_id;
	assert(!ppal_name_to_type("xdp", &filter.xid_type));
}

/* Based on iproute2/ip/iproute.c:iproute_list_flush_or_save. */
static int dump(__u32 tbl_id, rtnl_filter_t print)
{
	reset_filter(tbl_id);

	if (rtnl_wilddump_request(&rth, AF_XIA, RTM_GETROUTE) < 0) {
		perror("XDP: Cannot send dump request");
		exit(1);
	}
	if (rtnl_dump_filter(&rth, print, stdout, NULL, NULL) < 0) {
		fprintf(stderr, "XDP: Dump terminated\n");
		exit(1);
	}
	return 0;
}

static int print_socket(const struct sockaddr_nl *who, struct nlmsghdr *n,
	void *arg)
{
	FILE *fp = (FILE*)arg;
	struct rtmsg *r = NLMSG_DATA(n);
	int len = n->nlmsg_len;
	struct rtattr *tb[RTA_MAX+1];
	const struct xia_xid *dst;
	__u32 table;

	UNUSED(who);

	if (n->nlmsg_type != RTM_NEWROUTE && n->nlmsg_type != RTM_DELROUTE) {
		fprintf(stderr, "Not a route: %08x %08x %08x\n",
			n->nlmsg_len, n->nlmsg_type, n->nlmsg_flags);
		return 0;
	}
	if (r->rtm_family != AF_XIA) {
		/* fprintf(stderr, "Wrong rtm_family %d\n", r->rtm_family); */
		return 0;
	}
	len -= NLMSG_LENGTH(sizeof(*r));
	if (len < 0) {
		fprintf(stderr, "BUG: wrong nlmsg len %d\n", len);
		return -1;
	}
	if (r->rtm_dst_len != sizeof(struct xia_xid)) {
		fprintf(stderr, "BUG: wrong rtm_dst_len %d\n", r->rtm_dst_len);
		return -1;
	}

	/* XXX Doesn't the kernel provide similar function? */
	parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);
	table = rtnl_get_table(r, tb);

	/* Filter happens here. */
	if (filter.tb != table)
		return 0;
	if (!tb[RTA_DST] ||
		RTA_PAYLOAD(tb[RTA_DST]) != sizeof(struct xia_xid) ||
		r->rtm_dst_len != sizeof(struct xia_xid))
		return -1;
	dst = (const struct xia_xid *)RTA_DATA(tb[RTA_DST]);
	if (dst->xid_type != filter.xid_type)
		return 0;

	if (n->nlmsg_type == RTM_DELROUTE)
		fprintf(fp, "Deleted ");
	fprintf(fp, "local ");
	/* XXX It got to use @fp! */
	print_xia_xid(dst);
	fprintf(fp, "\n");

	if (tb[RTA_SRC]) {
		printf("peer ");
		assert(RTA_PAYLOAD(tb[RTA_SRC]) == sizeof(struct xia_addr));
		/* XXX It got to use @fp! */
		print_xia_addr((const struct xia_addr *)RTA_DATA(tb[RTA_SRC]));
		fprintf(fp, "\n");
	}

	assert(!r->rtm_src_len);
	assert(!(r->rtm_flags & RTM_F_CLONED));

	fprintf(fp, "flags [");
	if (r->rtm_flags & RTNH_F_DEAD)
		fprintf(fp, "dead ");
	if (r->rtm_flags & RTNH_F_ONLINK)
		fprintf(fp, "onlink ");
	if (r->rtm_flags & RTNH_F_PERVASIVE)
		fprintf(fp, "pervasive ");
	if (r->rtm_flags & RTM_F_NOTIFY)
		fprintf(fp, "notify ");
	fprintf(fp, "]");

	fprintf(fp, "\n\n");
	fflush(fp);
	return 0;
}

static int do_showsockets(int argc, char **argv)
{
	UNUSED(argv);
	if (argc != 0) {
		fprintf(stderr, "Wrong number of parameters\n");
		return usage();
	}
	return dump(XRTABLE_LOCAL_INDEX, print_socket);
}

static int do_addroute(int argc, char **argv)
{
	struct xia_xid dst, gw;

	if (argc != 3) {
		fprintf(stderr, "Wrong number of parameters\n");
		return usage();
	}
	if (strcmp(argv[1], "gw")) {
		fprintf(stderr, "Wrong parameters\n");
		return usage();
	}
	xrt_get_ppal_id("xdp", usage, &dst, argv[0]);
	xrt_get_xid(usage, &gw, argv[2]);

	return xrt_modify_route(&dst, &gw);
}

static int do_delroute(int argc, char **argv)
{
	struct xia_xid dst;

	if (argc != 1) {
		fprintf(stderr, "Wrong number of parameters\n");
		return usage();
	}
	xrt_get_ppal_id("xdp", usage, &dst, argv[0]);

	return xrt_modify_route(&dst, NULL);
}

static int do_showroutes(int argc, char **argv)
{
	xid_type_t ty;

	UNUSED(argv);
	if (argc != 0) {
		fprintf(stderr, "Wrong number of parameters\n");
		return usage();
	}

	assert(!ppal_name_to_type("xdp", &ty));
	return xrt_list_rt_redirects(XRTABLE_MAIN_INDEX, ty);
}

static int do_help(int argc, char **argv)
{
	UNUSED(argc);
	UNUSED(argv);
	usage();
	exit(1);
}

static const struct cmd cmds[] = {
	{ "showsockets",	do_showsockets	},
	{ "addroute",		do_addroute	},
	{ "delroute",		do_delroute	},
	{ "showroutes",		do_showroutes	},
	{ "help",		do_help		},
	{ 0,			0		}
};

int do_xdp(int argc, char **argv)
{
	return do_cmd(cmds, "Command", "xip xdp help", argc, argv);
}
