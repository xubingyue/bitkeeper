#include "sccs.h"
#include "bkd.h"
#include "binpool.h"

private FILE	*server(int recurse);

/*
 * Simple key sync.
 * Receive a list of keys on stdin and return a list of
 * keys not found locally.
 * Currently only -B (for binpool) is implemented.
 */
int
havekeys_main(int ac, char **av)
{
	char	*dfile;
	int	c;
	int	rc = 0, binpool = 0, recurse = 0;
	FILE	*f = 0;
	char	buf[MAXLINE];

	while ((c = getopt(ac, av, "BR;q")) != -1) {
		switch (c) {
		    case 'B': binpool = 1; break;
		    case 'R': recurse = atoi(optarg); break;
		    case 'q': break;	/* ignored for now */
		    default:
usage:			fprintf(stderr, "usage: bk %s [-q] [-B] -\n", av[0]);
			return (1);
		}
	}
	unless (av[optind] && streq(av[optind], "-")) goto usage;
	if (proj_cd2root()) {
		fprintf(stderr, "%s: must be run in a bk repository.\n",av[0]);
		return (1);
	}
	unless (binpool) {
		fprintf(stderr, "%s: only -B(binpool) supported.\n", av[0]);
		return (1);
	}
	/*
	 * What this should do is on the first missing key, popen another
	 * havekeys to our server (if we have one, else FILE * is just stdout)
	 * and then fputs the missing keys to that one which will filter through
	 * both our local binpool and our servers and send back the list of 
	 * keys that neither of us have.
	 *
	 * XXX - the optimization we want is to know if the server is local
	 * and do the work directly.
	 * XXX - need to make sure this is locked.
	 * XXX - what if the server has a server?  Should we keep going?
	 * If we do we need to remember all the places we looked so we
	 * don't loop and deadlock.  Fun!
	 *
	 * XXX - this is going to fail when we can't lock the bp server.
	 * In that case, annoying though it is, I believe we want to have
	 * cached all the keys and send them to stdout and get them locally.
	 * We want to make sure we lock and unlock as fast as possible.
	 */
	while (fnext(buf, stdin)) {
		chomp(buf);
		unless (dfile = bp_lookupkeys(0, buf)) {
			unless (f ) f = server(recurse);
			// XXX - need to check error status.
			fprintf(f, "%s\n", buf);	/* not here */
		}
		free(dfile);
	}
	if (f && (f != stdout) && pclose(f)) {
		// XXX - should send all the keys we don't have.
		fprintf(stderr, "havekeys: server error\n");
		rc = 1;
	}
	fflush(stdout);
	return (rc);
}

/*
 * Figure out if we have a server and if we do go run a havekeys there.
 */
private FILE *
server(int recurse)
{
	char	*p;
	FILE	*f;

	unless (recurse > 0) return (stdout);
	if (bp_serverID(&p)) return (stdout);	// OK?
	if (p == 0) return (stdout);
	free(p);
	p = aprintf("bk -q@'%s' -Lr havekeys -BR%d -",
	    proj_configval(0,"binpool_server"), recurse - 1);
	f = popen(p, "w");
	free(p);
	return (f ? f : stdout);
}

/*
 * An options last part of the bkd connection for clone and pull that
 * examines the incoming data and requests binpool keys that are missing.
 * from the local server.
 * This extra pass is only called if BKD_BINPOOL=YES indicating that the
 * remote bkd has binpool data, and if we share the same binpool server then
 * we won't request data.
 */
int
bkd_binpool_part3(remote *r, char **envVar, int quiet, char *range)
{
	FILE	*f, *f2;
	int	fd, i, bytes, rc = 1;
	char	*cmd;
	zputbuf	*zout;
	char	cmd_file[MAXPATH];
	char	hdr[64];
	char	buf[BSIZE];	/* must match remote.c:doit()/buf */

	if ((r->type == ADDR_HTTP) && bkd_connect(r, 1, !quiet)) {
		return (-1);
	}
	bktmp(cmd_file, "binpoolmsg");
	f = fopen(cmd_file, "w");
	assert(f);
	sendEnv(f, envVar, r, 0);
	/*
	 * No need to do "cd" again if we have a non-http connection
	 * becuase we already did a "cd" in part 1
	 */
	if (r->path && (r->type == ADDR_HTTP)) add_cd_command(f, r);

	/* we do want to recurse one level here, this is the proxy case */
	fprintf(f, "bk -zo0 sfio -oqBR1 -\n");
	fflush(f);
	zout = zputs_init(zputs_hwrite, int2p(fileno(f)));
	unless (bp_sharedServer()) {
		cmd = aprintf("bk changes -Bv -nd'" BINPOOL_DSPEC "' %s |"
		    /* The Wayne meister says one level of recursion.
		     * It's obvious.  Obvious to Leonardo...
		     */
		    "bk havekeys -BR1 -", range);
		f2 = popen(cmd, "r");
		assert(f2);
		fd = fileno(f2);
		while ((i = read(fd, buf, sizeof(buf))) > 0) {
			sprintf(hdr, "@STDIN=%u@\n", i);
			zputs(zout, hdr, strlen(hdr));
			zputs(zout, buf, i);
		}
		if (pclose(f2)) goto done;
	}
	sprintf(hdr, "@STDIN=0@\n");
	zputs(zout, hdr, strlen(hdr));
	zputs_done(zout);
	fprintf(f, "rdunlock\n");
	fprintf(f, "exit\n");
	fclose(f);
	rc = send_file(r, cmd_file, 0);
	if (rc) goto done;

	if (r->type == ADDR_HTTP) skip_http_hdr(r);
	unless (r->rf) r->rf = fdopen(r->rfd, "r");

	f = 0;
	while (fnext(buf, r->rf) && strneq(buf, "@STDOUT=", 8)) {
		bytes = atoi(&buf[8]);
		if (bytes == 0) break;
		while (bytes > 0) {
			i = min(sizeof(buf), bytes);
			if  ((i = fread(buf, 1, i, r->rf)) <= 0) break;
			unless (f) {
				f = popen("bk sfio -iqB -", "w");
				assert(f);
			}
			fwrite(buf, 1, i, f);
			bytes -= i;
		}
	}
	if (f) pclose(f);
	// XXX error handling

	rc = 0;
done:	wait_eof(r, 0);
	disconnect(r, 2);
	unlink(cmd_file);
	return (rc);
}