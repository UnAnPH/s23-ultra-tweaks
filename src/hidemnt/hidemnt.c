// SPDX-License-Identifier: GPL-2.0
/*
 * hidemnt - hide the module's bind/overlay mounts from apps that shouldn't see them (hideksu).
 *
 * The module binds every tweak into init's mount namespace (nsenter -t 1 -m), so the mounts are
 * inherited by every app. Root/environment detectors flag them as "suspicious"/"inconsistent".
 * KernelSU's own umount is global (all non-root apps) and would also strip them from the Samsung
 * apps that consume them (Camera, Gallery, Settings, the galaxyai apps, ...), breaking those
 * tweaks. So we umount the module mounts per app, in that app's own mount namespace only.
 *
 * Policy (install-origin): a tweak consumer is always a *preinstalled* system/Samsung app; a
 * root-detector or bank is always a *user-installed* app. So by default we hide the mounts from
 * every user-installed app (uid >= 10000 whose package is not flagged FLAG_SYSTEM) and keep them
 * for every preinstalled system app. FLAG_SYSTEM stays set even on a system app updated into
 * /data/app via the store, so updated consumers (e.g. a Galaxy-Store Camera) are still kept.
 * This protects every bank/detector automatically and can't silently break a tweak, because no
 * consumer is ever user-installed.
 *
 * The override file (one entry per line, '#' comments) adjusts that default:
 *   <pkg>     force-hide this package even if it is a system app
 *   -<pkg>    force-keep this package even if it is user-installed
 * Both match the exact package or a "<pkg>:suffix" private process.
 *
 * Every module mount line in /proc/<pid>/mountinfo contains the token "s23tweaks" (overlay source
 * names s23tweaks_*, or a bind whose mount-root is /adb/s23tweaks/...). We enter the target's mount
 * namespace and umount2(MNT_DETACH) each such mount point, deepest path first. Only that app's view
 * changes; init and every other process keep the mounts.
 *
 * Modes:
 *   hidemnt once <pid>            umount module mounts in that pid's mount namespace, now (no policy).
 *   hidemnt rdonly <path>         make the bind mount at <path> read-only (MS_REMOUNT|MS_BIND|MS_RDONLY), in the
 *                                 caller's mount namespace. `mount -o bind,ro` leaves a bind read-write, and
 *                                 toybox's remount,bind,ro remounts the whole filesystem instead.
 *   hidemnt daemon <override> [<syslist>]
 *                                 watch process spawns (kernel proc connector) and umount in each
 *                                 app the policy says to hide from; also sweeps already-running apps
 *                                 at startup. Reloads the override file and the package db on change.
 *                                 <syslist>: system packages, one per line (`pm list packages -s`);
 *                                 without it, packages.xml is scanned (text XML only, not ABX).
 *
 * A single setns()+umount2() pass is microseconds, so when the daemon fires on the fork/comm event
 * it finishes before the app's own code (e.g. a native root check in onCreate) reads the mounts.
 *
 * The daemon must not exit on its own: if it stops, every app started afterwards sees the mounts
 * again, with nothing in the status but "visible to apps" (issue #34). A burst of process starts
 * (browsers, video apps) can overflow the netlink socket's receive queue; recv() then fails with
 * ENOBUFS and the events in between are lost. That is recoverable: sweep /proc for any app whose
 * event was dropped and keep listening. Any other socket error reopens the socket.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

#define TOKEN      "s23tweaks"
#define MAX_MP     512
#define MAX_PKGS   256
#define PATH_MAX_  512
#define SYS_CAP    4096		/* max system packages tracked (Samsung ships ~700) */
#define APP_UID    10000	/* first application uid; below this is root/system, never touched */

#define PKGXML "/data/system/packages.xml"

/* ---- read this pid's process name (cmdline[0], the package or "<pkg>:proc") ---- */
static int proc_name(int pid, char *out, size_t n)
{
	char p[64];
	snprintf(p, sizeof(p), "/proc/%d/cmdline", pid);
	int fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ssize_t r = read(fd, out, n - 1);
	close(fd);
	if (r <= 0)
		return -1;
	out[r] = '\0';		/* cmdline is NUL-separated; first token is the process name */
	return 0;
}

/* owner uid of a pid, or -1 */
static int proc_uid(int pid)
{
	char p[64];
	snprintf(p, sizeof(p), "/proc/%d", pid);
	struct stat st;
	if (stat(p, &st) != 0)
		return -1;
	return (int)st.st_uid;
}

/* true if proc name matches a package in the list: exact, or "<pkg>:suffix" (private process) */
static int name_matches(const char *name, char pkgs[][128], int npkgs)
{
	for (int i = 0; i < npkgs; i++) {
		size_t l = strlen(pkgs[i]);
		if (strncmp(name, pkgs[i], l) == 0 && (name[l] == '\0' || name[l] == ':'))
			return 1;
	}
	return 0;
}

/* ---- override lists: force-hide (plain) and force-keep (leading '-') ---- */
static char g_hide[MAX_PKGS][128];
static int  g_nhide;
static char g_keep[MAX_PKGS][128];
static int  g_nkeep;
static time_t g_ov_mtime;

static void load_overrides(const char *file)
{
	g_nhide = g_nkeep = 0;
	FILE *f = fopen(file, "re");
	if (!f)
		return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char *s = line;
		while (*s == ' ' || *s == '\t') s++;
		if (*s == '#' || *s == '\n' || *s == '\r' || *s == '\0')
			continue;
		char *e = s + strlen(s);
		while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
			*--e = '\0';
		int keep = 0;
		if (*s == '-') { keep = 1; s++; while (*s == ' ' || *s == '\t') s++; }
		if (!*s)
			continue;
		if (keep && g_nkeep < MAX_PKGS) {
			strncpy(g_keep[g_nkeep], s, 127); g_keep[g_nkeep][127] = '\0'; g_nkeep++;
		} else if (!keep && g_nhide < MAX_PKGS) {
			strncpy(g_hide[g_nhide], s, 127); g_hide[g_nhide][127] = '\0'; g_nhide++;
		}
	}
	fclose(f);
}

/* ---- system-package set, parsed from packages.xml (FLAG_SYSTEM | FLAG_UPDATED_SYSTEM_APP) ---- */
static char (*g_sys)[128];
static int  g_nsys;
static time_t g_sys_mtime;

/* true if the <package ...> opening tag in [tag,end) carries a system flag */
static int tag_is_system(const char *tag, const char *end)
{
	const char *k = memmem(tag, end - tag, "publicFlags=\"", 13);
	int off = 13;
	if (!k) { k = memmem(tag, end - tag, " flags=\"", 8); off = 8; }
	if (!k)
		return 0;
	long v = strtol(k + off, NULL, 10);
	return (v & 1) || (v & 0x80);	/* FLAG_SYSTEM=0x1, FLAG_UPDATED_SYSTEM_APP=0x80 */
}

/* system-package list file (one name per line, from `pm list packages -s`), if given. Preferred:
 * One UI 7+ writes packages.xml as binary XML (ABX), which the text scan below can't read. */
static const char *g_sysfile;

static void load_system_list(void)
{
	FILE *f = fopen(g_sysfile, "re");
	if (!f)
		return;
	char line[256];
	while (g_nsys < SYS_CAP && fgets(line, sizeof(line), f)) {
		char *p = line;
		if (strncmp(p, "package:", 8) == 0)
			p += 8;
		size_t l = strcspn(p, " \t\r\n");
		if (l == 0 || l > 127 || !memchr(p, '.', l))
			continue;
		memcpy(g_sys[g_nsys], p, l);
		g_sys[g_nsys][l] = '\0';
		g_nsys++;
	}
	fclose(f);
}

static void load_system_pkgs(void)
{
	g_nsys = 0;
	if (!g_sys) {
		g_sys = malloc((size_t)SYS_CAP * 128);
		if (!g_sys)
			return;
	}
	if (g_sysfile) {
		load_system_list();
		return;
	}
	FILE *f = fopen(PKGXML, "re");
	if (!f)
		return;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
	long sz = ftell(f);
	if (sz <= 0) { fclose(f); return; }
	rewind(f);
	char *buf = malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return; }
	size_t got = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[got] = '\0';

	char *p = buf, *end = buf + got;
	const char *tagkey = "<package name=\"";
	size_t tklen = strlen(tagkey);
	while (g_nsys < SYS_CAP && (p = memmem(p, (size_t)(end - p), tagkey, tklen))) {
		p += tklen;
		char *q = memchr(p, '"', (size_t)(end - p));	/* end of the name value */
		if (!q)
			break;
		char *tagend = memchr(q, '>', (size_t)(end - q));
		if (!tagend)
			tagend = end;
		if (tag_is_system(q, tagend)) {
			size_t l = (size_t)(q - p);
			if (l > 127) l = 127;
			memcpy(g_sys[g_nsys], p, l);
			g_sys[g_nsys][l] = '\0';
			g_nsys++;
		}
		p = tagend;
	}
	free(buf);
}

static int is_system(const char *pkg)
{
	for (int i = 0; i < g_nsys; i++)
		if (strcmp(g_sys[i], pkg) == 0)
			return 1;
	return 0;
}

/* reload the override file and package db if either changed on disk */
static void refresh_tables(const char *ovfile)
{
	struct stat st;
	if (stat(g_sysfile ? g_sysfile : PKGXML, &st) == 0 && st.st_mtime != g_sys_mtime) {
		load_system_pkgs();
		g_sys_mtime = st.st_mtime;
	}
	if (ovfile && stat(ovfile, &st) == 0 && st.st_mtime != g_ov_mtime) {
		load_overrides(ovfile);
		g_ov_mtime = st.st_mtime;
	}
}

/* policy: should the module's mounts be hidden from this process? */
static int should_hide(int pid, const char *name)
{
	if (proc_uid(pid) < APP_UID)		/* root/system process: never touch */
		return 0;
	if (name_matches(name, g_keep, g_nkeep))	/* force-keep override */
		return 0;
	if (name_matches(name, g_hide, g_nhide))	/* force-hide override */
		return 1;
	/* fail-safe: if the package db didn't parse (wrong/absent schema, unreadable), g_nsys is
	 * tiny/zero. Never auto-hide then - that would strip mounts from the system consumers too.
	 * Degrade to overrides-only (a real device has hundreds of FLAG_SYSTEM packages). */
	if (g_nsys < 32)
		return 0;
	char base[128];				/* strip ":proc" private-process suffix */
	size_t i = 0;
	for (; name[i] && name[i] != ':' && i < sizeof(base) - 1; i++)
		base[i] = name[i];
	base[i] = '\0';
	if (!base[0] || !strchr(base, '.'))	/* not a package-like name */
		return 0;
	/* Samsung's own apps installed from the Galaxy Store (Voice Recorder, Samsung Health, the
	 * on-device language packs, ...) aren't FLAG_SYSTEM but can read the feature files the tweaks
	 * change; no bank or detector uses Samsung's package prefixes, so keep the mounts for them. */
	if (strncmp(base, "com.samsung.", 12) == 0 || strncmp(base, "com.sec.", 8) == 0)
		return 0;
	return !is_system(base);		/* hide from user-installed apps */
}

/* collect module mount points from /proc/<pid>/mountinfo, longest path first */
static int collect_mounts(int pid, char mps[][PATH_MAX_])
{
	char p[64];
	snprintf(p, sizeof(p), "/proc/%d/mountinfo", pid);
	FILE *f = fopen(p, "re");
	if (!f)
		return 0;
	int n = 0;
	char line[4096];
	while (n < MAX_MP && fgets(line, sizeof(line), f)) {
		if (!strstr(line, TOKEN))
			continue;
		/* field 5 (1-based) is the mount point: skip 4 space-separated fields */
		char *mp = NULL;
		int field = 1;
		for (char *c = line; *c; c++) {
			if (*c == ' ') {
				field++;
				if (field == 5) { mp = c + 1; }
				else if (field == 6) { *c = '\0'; break; }
			}
		}
		if (!mp || !*mp)
			continue;
		strncpy(mps[n], mp, PATH_MAX_ - 1);
		mps[n][PATH_MAX_ - 1] = '\0';
		n++;
	}
	fclose(f);
	/* sort by descending path length so children umount before parents */
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++)
			if (strlen(mps[j]) > strlen(mps[i])) {
				char t[PATH_MAX_];
				strcpy(t, mps[i]); strcpy(mps[i], mps[j]); strcpy(mps[j], t);
			}
	return n;
}

/* parent pid from /proc/<pid>/stat, or -1 */
static int proc_ppid(int pid)
{
	char p[64], buf[512];
	snprintf(p, sizeof(p), "/proc/%d/stat", pid);
	int fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ssize_t r = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (r <= 0)
		return -1;
	buf[r] = '\0';
	char *c = strrchr(buf, ')');		/* comm can hold spaces; fields resume after the last ')' */
	int ppid = -1;
	if (!c || sscanf(c + 1, " %*c %d", &ppid) != 1)
		return -1;
	return ppid;
}

/* same mount namespace? */
static int same_ns(int a, int b)
{
	char p[64];
	struct stat sa, sb;
	snprintf(p, sizeof(p), "/proc/%d/ns/mnt", a);
	if (stat(p, &sa) != 0)
		return 0;
	snprintf(p, sizeof(p), "/proc/%d/ns/mnt", b);
	if (stat(p, &sb) != 0)
		return 0;
	return sa.st_ino == sb.st_ino && sa.st_dev == sb.st_dev;
}

/* An app zygote ("<pkg>_zygote", used by Chromium browsers and others to spawn their sandboxed
 * processes) inherits Android's zygote's open files, which include the framework resource overlays,
 * e.g. the display tweak's /product/overlay/s23tweaks-aodbright.apk. On every fork, zygote code
 * reopens each such file by path, and aborts ("Failed open(...)") if the path is gone. So in an app
 * zygote's namespace (the zygote itself, or a sandboxed child that shares it) the mounts that hold a
 * file the app zygote has open must stay. Returns the app zygote's pid, or 0 if pid isn't in one. */
static int app_zygote_of(int pid)
{
	char name[256];
	size_t n;
	int cand[2] = { pid, proc_ppid(pid) };
	for (int i = 0; i < 2; i++) {
		if (cand[i] <= 0 || proc_name(cand[i], name, sizeof(name)) != 0)
			continue;
		n = strlen(name);
		if (n > 7 && strcmp(name + n - 7, "_zygote") == 0 && (i == 0 || same_ns(pid, cand[i])))
			return cand[i];
	}
	return 0;
}

/* true if mount point mp holds a file the process zpid has open (path == mp or under mp/) */
static int mount_in_use(int zpid, const char *mp)
{
	char p[64], fp[PATH_MAX_ + 16], tgt[PATH_MAX_];
	snprintf(p, sizeof(p), "/proc/%d/fd", zpid);
	DIR *d = opendir(p);
	if (!d)
		return 0;
	size_t ml = strlen(mp);
	int used = 0;
	struct dirent *e;
	while (!used && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(fp, sizeof(fp), "%s/%s", p, e->d_name);
		ssize_t r = readlink(fp, tgt, sizeof(tgt) - 1);
		if (r <= 0)
			continue;
		tgt[r] = '\0';
		if (strncmp(tgt, mp, ml) == 0 && (tgt[ml] == '\0' || tgt[ml] == '/'))
			used = 1;
	}
	closedir(d);
	return used;
}

/* enter pid's mount namespace and detach every module mount. returns mounts removed, -1 on setns fail. */
static int hide_in(int pid)
{
	static char mps[MAX_MP][PATH_MAX_];
	int n = collect_mounts(pid, mps);
	if (n == 0)
		return 0;
	/* in an app zygote's namespace, keep the mounts behind its open files (see app_zygote_of) */
	int zpid = app_zygote_of(pid);
	if (zpid > 0) {
		int k = 0;
		for (int i = 0; i < n; i++)
			if (!mount_in_use(zpid, mps[i]))
				memmove(mps[k++], mps[i], PATH_MAX_);
		n = k;
		if (n == 0)
			return 0;
	}
	char p[64];
	snprintf(p, sizeof(p), "/proc/%d/ns/mnt", pid);
	int fd = open(p, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	/* Never umount in init's namespace (or ours): some app-uid processes share it - Chrome's app
	 * zygote (com.android.chrome_zygote_native) and its sandboxed children do - and a detach there
	 * removes the module's mounts for the whole system, not for one app. */
	struct stat ns, ns1, nsme;
	if (fstat(fd, &ns) != 0 || stat("/proc/1/ns/mnt", &ns1) != 0 || stat("/proc/self/ns/mnt", &nsme) != 0 ||
	    (ns.st_ino == ns1.st_ino && ns.st_dev == ns1.st_dev) ||
	    (ns.st_ino == nsme.st_ino && ns.st_dev == nsme.st_dev)) {
		close(fd);
		return 0;
	}
	if (setns(fd, CLONE_NEWNS) != 0) {
		close(fd);
		return -1;
	}
	close(fd);
	int done = 0;
	for (int i = 0; i < n; i++)
		if (umount2(mps[i], MNT_DETACH) == 0)
			done++;
	return done;
}

/* true if pid's mountinfo still has a module mount (read in the daemon's own namespace: cheap) */
static int has_module_mounts(int pid)
{
	char p[64], line[4096];
	snprintf(p, sizeof(p), "/proc/%d/mountinfo", pid);
	FILE *f = fopen(p, "re");
	if (!f)
		return 0;
	int found = 0;
	while (!found && fgets(line, sizeof(line), f))
		found = strstr(line, TOKEN) != NULL;
	fclose(f);
	return found;
}

/* fork a child that does hide_in (so the daemon stays in init's namespace) */
static void hide_fork(int pid)
{
	/* An app gets several events (COMM, EXEC) and sweeps revisit it; once hidden there is nothing to
	 * do, and skipping the fork keeps the daemon fast enough to drain bursts of events. */
	if (!has_module_mounts(pid))
		return;
	pid_t c = fork();
	if (c == 0) {
		_exit(hide_in(pid) < 0 ? 1 : 0);
	} else if (c > 0) {
		int st;
		waitpid(c, &st, 0);
	}
}

/* clean any already-running apps the policy says to hide from */
static void sweep(void)
{
	DIR *d = opendir("/proc");
	if (!d)
		return;
	struct dirent *e;
	char name[256];
	while ((e = readdir(d))) {
		int pid = atoi(e->d_name);
		if (pid <= 0)
			continue;
		if (proc_name(pid, name, sizeof(name)) == 0 && should_hide(pid, name))
			hide_fork(pid);
	}
	closedir(d);
}

/* open a netlink socket subscribed to process events, or -1 */
static int open_proc_socket(void)
{
	int s = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_CONNECTOR);
	if (s < 0)
		return -1;
	/* A larger receive queue rides out bursts of process starts. FORCE needs CAP_NET_ADMIN (root has
	 * it) and ignores net.core.rmem_max; fall back to the capped size. */
	int rcv = 4 << 20;
	if (setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, &rcv, sizeof(rcv)) != 0)
		setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_pid = 0, .nl_groups = CN_IDX_PROC };
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(s);
		return -1;
	}
	/* subscribe to process events */
	struct {
		struct nlmsghdr nlh;
		struct cn_msg cn;
		enum proc_cn_mcast_op op;
	} __attribute__((packed)) sub = {0};
	sub.nlh.nlmsg_len = sizeof(sub);
	sub.nlh.nlmsg_type = NLMSG_DONE;
	sub.nlh.nlmsg_pid = (unsigned)getpid();
	sub.cn.id.idx = CN_IDX_PROC;
	sub.cn.id.val = CN_VAL_PROC;
	sub.cn.len = sizeof(enum proc_cn_mcast_op);
	sub.op = PROC_CN_MCAST_LISTEN;
	if (send(s, &sub, sizeof(sub), 0) != (ssize_t)sizeof(sub)) {
		close(s);
		return -1;
	}
	return s;
}

static int daemon_loop(const char *ovfile)
{
	/* a root daemon, but keep the kernel's OOM killer from ever picking it */
	int ofd = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
	if (ofd >= 0) {
		if (write(ofd, "-1000", 5) < 0) { /* best effort */ }
		close(ofd);
	}
	signal(SIGCHLD, SIG_IGN);	/* reap nothing: children are waited in hide_fork */
	refresh_tables(ovfile);		/* load package db + overrides */

	int s = open_proc_socket();	/* before the first sweep, so no spawn falls between the two */
	if (s < 0) {
		perror("hidemnt: process events");	/* kernel without the proc connector: can't work */
		return 1;
	}
	sweep();			/* catch apps already running */

	char buf[4096];
	char name[256];
	for (;;) {
		ssize_t len = recv(s, buf, sizeof(buf), 0);
		if (len < 0 && errno == EINTR)
			continue;
		if (len < 0 && errno == ENOBUFS) {
			/* the queue overflowed and events were dropped: catch up on the apps they named */
			refresh_tables(ovfile);
			sweep();
			continue;
		}
		if (len <= 0) {
			/* any other error: reopen the socket (retrying every few seconds), then catch up */
			close(s);
			while ((s = open_proc_socket()) < 0)
				sleep(5);
			refresh_tables(ovfile);
			sweep();
			continue;
		}
		refresh_tables(ovfile);	/* pick up edits to the override file / new installs */
		for (struct nlmsghdr *nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned)len);
		     nlh = NLMSG_NEXT(nlh, len)) {
			if (nlh->nlmsg_type != NLMSG_DONE)
				continue;
			struct cn_msg *cn = (struct cn_msg *)NLMSG_DATA(nlh);
			struct proc_event *ev = (struct proc_event *)cn->data;
			int pid = 0;
			/* COMM: process name was just set (zygote specialization) - earliest reliable
			 * point. EXEC: also handle, cheap. */
			if (ev->what == PROC_EVENT_COMM)
				pid = ev->event_data.comm.process_pid;
			else if (ev->what == PROC_EVENT_EXEC)
				pid = ev->event_data.exec.process_pid;
			else
				continue;
			if (pid <= 0)
				continue;
			if (proc_name(pid, name, sizeof(name)) == 0 && should_hide(pid, name))
				hide_fork(pid);
		}
	}
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "once") == 0) {
		int r = hide_in(atoi(argv[2]));
		if (r < 0) {
			fprintf(stderr, "hidemnt: setns/umount failed for pid %s\n", argv[2]);
			return 1;
		}
		printf("hidemnt: %d mounts hidden from pid %s\n", r, argv[2]);
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "rdonly") == 0) {
		if (mount(NULL, argv[2], NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) != 0) {
			fprintf(stderr, "hidemnt: remount %s read-only: %s\n", argv[2], strerror(errno));
			return 1;
		}
		return 0;
	}
	if ((argc == 3 || argc == 4) && strcmp(argv[1], "daemon") == 0) {
		if (argc == 4)
			g_sysfile = argv[3];
		return daemon_loop(argv[2]);
	}

	fprintf(stderr, "usage: %s once <pid> | rdonly <path> | daemon <override-file> [<system-package-list>]\n", argv[0]);
	return 2;
}
