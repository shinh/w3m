#define MAINPROGRAM

#include "config.h"
#include "fm.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netdb.h>

/* dummy */
void backBf() {
    abort();
}
Buffer *loadHTMLString(Str s) {
    abort();
}

/* from url.c */
#define COPYPATH_SPC_ALLOW 0
#define COPYPATH_SPC_IGNORE 1
#define COPYPATH_SPC_REPLACE 2
static char *
copyPath(char *orgpath, int length, int option)
{
    Str tmp = Strnew();
    while (*orgpath && length != 0) {
	if (IS_SPACE(*orgpath)) {
	    switch (option) {
	    case COPYPATH_SPC_ALLOW:
		Strcat_char(tmp, *orgpath);
		break;
	    case COPYPATH_SPC_IGNORE:
		/* do nothing */
		break;
	    case COPYPATH_SPC_REPLACE:
		Strcat_charp(tmp, "%20");
		break;
	    }
	}
	else
	    Strcat_char(tmp, *orgpath);
	orgpath++;
	length--;
    }
    return tmp->ptr;
}
struct cmdtable schemetable[] = {
    {"http", SCM_HTTP},
    {"gopher", SCM_GOPHER},
    {"ftp", SCM_FTP},
    {"local", SCM_LOCAL},
    {"file", SCM_LOCAL},
    /*  {"exec", SCM_EXEC}, */
    {"nntp", SCM_NNTP},
    /*  {"nntp", SCM_NNTP_GROUP}, */
    {"news", SCM_NEWS},
    /*  {"news", SCM_NEWS_GROUP}, */
    {"data", SCM_DATA},
#ifndef USE_W3MMAILER
    {"mailto", SCM_MAILTO},
#endif
#ifdef USE_SSL
    {"https", SCM_HTTPS},
#endif				/* USE_SSL */
    {NULL, SCM_UNKNOWN},
};
int
getURLScheme(char **url)
{
    char *p = *url, *q;
    int i;
    int scheme = SCM_MISSING;

    while (*p && (IS_ALNUM(*p) || *p == '.' || *p == '+' || *p == '-'))
	p++;
    if (*p == ':' && !isdigit(p[1])) {		/* scheme found */
	scheme = SCM_UNKNOWN;
	for (i = 0; (q = schemetable[i].cmdname) != NULL; i++) {
	    int len = strlen(q);
	    if (!strncasecmp(q, *url, len) && (*url)[len] == ':') {
		scheme = schemetable[i].cmd;
		*url = p + 1;
		break;
	    }
	}
    }
    return scheme;
}
/* #define HTTP_DEFAULT_FILE    "/index.html" */
#ifndef HTTP_DEFAULT_FILE
#define HTTP_DEFAULT_FILE "/"
#endif				/* not HTTP_DEFAULT_FILE */
static char *
DefaultFile(int scheme)
{
    switch (scheme) {
    case SCM_HTTP:
#ifdef USE_SSL
    case SCM_HTTPS:
#endif				/* USE_SSL */
	return allocStr(HTTP_DEFAULT_FILE, -1);
#ifdef USE_GOPHER
    case SCM_GOPHER:
	return allocStr("1", -1);
#endif				/* USE_GOPHER */
    case SCM_LOCAL:
    case SCM_LOCAL_CGI:
    case SCM_FTP:
    case SCM_FTPDIR:
	return allocStr("/", -1);
    }
    return NULL;
}
/* XXX: note html.h SCM_ */
static int
 DefaultPort[] = {
    80,				/* http */
    70,				/* gopher */
    21,				/* ftp */
    21,				/* ftpdir */
    0,				/* local - not defined */
    0,				/* local-CGI - not defined? */
    0,				/* exec - not defined? */
    119,			/* nntp */
    119,			/* nntp group */
    119,			/* news */
    119,			/* news group */
    0,				/* data - not defined */
    0,				/* mailto - not defined */
#ifdef USE_SSL
    443,			/* https */
#endif				/* USE_SSL */
};
void
parseURL(char *url, ParsedURL *p_url, ParsedURL *current)
{
    char *p, *q;
    Str tmp;

    url = url_quote(url);	/* quote 0x01-0x20, 0x7F-0xFF */

    p = url;
    p_url->scheme = SCM_MISSING;
    p_url->port = 0;
    p_url->user = NULL;
    p_url->pass = NULL;
    p_url->host = NULL;
    p_url->is_nocache = 0;
    p_url->file = NULL;
    p_url->real_file = NULL;
    p_url->query = NULL;
    p_url->label = NULL;

    /* RFC1808: Relative Uniform Resource Locators
     * 4.  Resolving Relative URLs
     */
    if (*url == '\0' || *url == '#') {
	if (current)
	    copyParsedURL(p_url, current);
	goto do_label;
    }
#if defined( __EMX__ ) || defined( __CYGWIN__ )
    if (!strncmp(url, "file://localhost/", 17)) {
	p_url->scheme = SCM_LOCAL;
	p += 17 - 1;
	url += 17 - 1;
    }
#endif
#ifdef SUPPORT_DOS_DRIVE_PREFIX
    if (IS_ALPHA(*p) && (p[1] == ':' || p[1] == '|')) {
	p_url->scheme = SCM_LOCAL;
	goto analyze_file;
    }
#endif				/* SUPPORT_DOS_DRIVE_PREFIX */
    /* search for scheme */
    p_url->scheme = getURLScheme(&p);
    if (p_url->scheme == SCM_MISSING) {
	/* scheme part is not found in the url. This means either
	 * (a) the url is relative to the current or (b) the url
	 * denotes a filename (therefore the scheme is SCM_LOCAL).
	 */
	if (current) {
	    switch (current->scheme) {
	    case SCM_LOCAL:
	    case SCM_LOCAL_CGI:
		p_url->scheme = SCM_LOCAL;
		break;
	    case SCM_FTP:
	    case SCM_FTPDIR:
		p_url->scheme = SCM_FTP;
		break;
#ifdef USE_NNTP
	    case SCM_NNTP:
	    case SCM_NNTP_GROUP:
		p_url->scheme = SCM_NNTP;
		break;
	    case SCM_NEWS:
	    case SCM_NEWS_GROUP:
		p_url->scheme = SCM_NEWS;
		break;
#endif
	    default:
		p_url->scheme = current->scheme;
		break;
	    }
	}
	else
	    p_url->scheme = SCM_LOCAL;
	p = url;
	if (!strncmp(p, "//", 2)) {
	    /* URL begins with // */
	    /* it means that 'scheme:' is abbreviated */
	    p += 2;
	    goto analyze_url;
	}
	/* the url doesn't begin with '//' */
	goto analyze_file;
    }
    /* scheme part has been found */
    if (p_url->scheme == SCM_UNKNOWN) {
	p_url->file = allocStr(url, -1);
	return;
    }
    /* get host and port */
    if (p[0] != '/' || p[1] != '/') {	/* scheme:foo or scheme:/foo */
	p_url->host = NULL;
	if (p_url->scheme != SCM_UNKNOWN)
	    p_url->port = DefaultPort[p_url->scheme];
	else
	    p_url->port = 0;
	goto analyze_file;
    }
    /* after here, p begins with // */
    if (p_url->scheme == SCM_LOCAL) {	/* file://foo           */
#ifdef __EMX__
	p += 2;
	goto analyze_file;
#else
	if (p[2] == '/' || p[2] == '~'
	    /* <A HREF="file:///foo">file:///foo</A>  or <A HREF="file://~user">file://~user</A> */
#ifdef SUPPORT_DOS_DRIVE_PREFIX
	    || (IS_ALPHA(p[2]) && (p[3] == ':' || p[3] == '|'))
	    /* <A HREF="file://DRIVE/foo">file://DRIVE/foo</A> */
#endif				/* SUPPORT_DOS_DRIVE_PREFIX */
	    ) {
	    p += 2;
	    goto analyze_file;
	}
#endif				/* __EMX__ */
    }
    p += 2;			/* scheme://foo         */
    /*          ^p is here  */
  analyze_url:
    q = p;
#ifdef INET6
    if (*q == '[') {		/* rfc2732,rfc2373 compliance */
	p++;
	while (IS_XDIGIT(*p) || *p == ':' || *p == '.')
	    p++;
	if (*p != ']' || (*(p + 1) && strchr(":/?#", *(p + 1)) == NULL))
	    p = q;
    }
#endif
    while (*p && strchr(":/@?#", *p) == NULL)
	p++;
    switch (*p) {
    case ':':
	/* scheme://user:pass@host or
	 * scheme://host:port
	 */
	p_url->host = copyPath(q, p - q, COPYPATH_SPC_IGNORE);
	q = ++p;
	while (*p && strchr("@/?#", *p) == NULL)
	    p++;
	if (*p == '@') {
	    /* scheme://user:pass@...       */
	    p_url->pass = copyPath(q, p - q, COPYPATH_SPC_ALLOW);
	    q = ++p;
	    p_url->user = p_url->host;
	    p_url->host = NULL;
	    goto analyze_url;
	}
	/* scheme://host:port/ */
	tmp = Strnew_charp_n(q, p - q);
	p_url->port = atoi(tmp->ptr);
	/* *p is one of ['\0', '/', '?', '#'] */
	break;
    case '@':
	/* scheme://user@...            */
	p_url->user = copyPath(q, p - q, COPYPATH_SPC_IGNORE);
	q = ++p;
	goto analyze_url;
    case '\0':
	/* scheme://host                */
    case '/':
    case '?':
    case '#':
	p_url->host = copyPath(q, p - q, COPYPATH_SPC_IGNORE);
	p_url->port = DefaultPort[p_url->scheme];
	break;
    }
  analyze_file:
#ifndef SUPPORT_NETBIOS_SHARE
    if (p_url->scheme == SCM_LOCAL && p_url->user == NULL &&
	p_url->host != NULL && *p_url->host != '\0' &&
	strcmp(p_url->host, "localhost")) {
	/*
	 * In the environments other than CYGWIN, a URL like 
	 * file://host/file is regarded as ftp://host/file.
	 * On the other hand, file://host/file on CYGWIN is
	 * regarded as local access to the file //host/file.
	 * `host' is a netbios-hostname, drive, or any other
	 * name; It is CYGWIN system call who interprets that.
	 */

	p_url->scheme = SCM_FTP;	/* ftp://host/... */
	if (p_url->port == 0)
	    p_url->port = DefaultPort[SCM_FTP];
    }
#endif
    if ((*p == '\0' || *p == '#' || *p == '?') && p_url->host == NULL) {
	p_url->file = "";
	goto do_query;
    }
#ifdef SUPPORT_DOS_DRIVE_PREFIX
    if (p_url->scheme == SCM_LOCAL) {
	q = p;
	if (*q == '/')
	    q++;
	if (IS_ALPHA(q[0]) && (q[1] == ':' || q[1] == '|')) {
	    if (q[1] == '|') {
		p = allocStr(q, -1);
		p[1] = ':';
	    }
	    else
		p = q;
	}
    }
#endif

    q = p;
#ifdef USE_GOPHER
    if (p_url->scheme == SCM_GOPHER) {
	if (*q == '/')
	    q++;
	if (*q && q[0] != '/' && q[1] != '/' && q[2] == '/')
	    q++;
    }
#endif				/* USE_GOPHER */
    if (*p == '/')
	p++;
    if (*p == '\0' || *p == '#' || *p == '?') {	/* scheme://host[:port]/ */
	p_url->file = DefaultFile(p_url->scheme);
	goto do_query;
    }
#ifdef USE_GOPHER
    if (p_url->scheme == SCM_GOPHER && *p == 'R') {
	p++;
	tmp = Strnew();
	Strcat_char(tmp, *(p++));
	while (*p && *p != '/')
	    p++;
	Strcat_charp(tmp, p);
	while (*p)
	    p++;
	p_url->file = copyPath(tmp->ptr, -1, COPYPATH_SPC_IGNORE);
    }
    else
#endif				/* USE_GOPHER */
    {
	char *cgi = strchr(p, '?');
      again:
	while (*p && *p != '#' && p != cgi)
	    p++;
	if (*p == '#' && p_url->scheme == SCM_LOCAL) {
	    /* 
	     * According to RFC2396, # means the beginning of
	     * URI-reference, and # should be escaped.  But,
	     * if the scheme is SCM_LOCAL, the special
	     * treatment will apply to # for convinience.
	     */
	    if (p > q && *(p - 1) == '/' && (cgi == NULL || p < cgi)) {
		/* 
		 * # comes as the first character of the file name
		 * that means, # is not a label but a part of the file
		 * name.
		 */
		p++;
		goto again;
	    }
	    else if (*(p + 1) == '\0') {
		/* 
		 * # comes as the last character of the file name that
		 * means, # is not a label but a part of the file
		 * name.
		 */
		p++;
	    }
	}
	if (p_url->scheme == SCM_LOCAL || p_url->scheme == SCM_MISSING)
	    p_url->file = copyPath(q, p - q, COPYPATH_SPC_ALLOW);
	else
	    p_url->file = copyPath(q, p - q, COPYPATH_SPC_IGNORE);
    }

  do_query:
    if (*p == '?') {
	q = ++p;
	while (*p && *p != '#')
	    p++;
	p_url->query = copyPath(q, p - q, COPYPATH_SPC_ALLOW);
    }
  do_label:
    if (p_url->scheme == SCM_MISSING) {
	p_url->scheme = SCM_LOCAL;
	p_url->file = allocStr(p, -1);
	p_url->label = NULL;
    }
    else if (*p == '#')
	p_url->label = allocStr(p + 1, -1);
    else
	p_url->label = NULL;
}
static Str
_parsedURL2Str(ParsedURL *pu, int pass)
{
    Str tmp;
    static char *scheme_str[] = {
	"http", "gopher", "ftp", "ftp", "file", "file", "exec", "nntp", "nntp",
	"news", "news", "data", "mailto",
#ifdef USE_SSL
	"https",
#endif				/* USE_SSL */
    };

    if (pu->scheme == SCM_MISSING) {
	return Strnew_charp("???");
    }
    else if (pu->scheme == SCM_UNKNOWN) {
	return Strnew_charp(pu->file);
    }
    if (pu->host == NULL && pu->file == NULL && pu->label != NULL) {
	/* local label */
	return Sprintf("#%s", pu->label);
    }
    if (pu->scheme == SCM_LOCAL && !strcmp(pu->file, "-")) {
	tmp = Strnew_charp("-");
	if (pu->label) {
	    Strcat_char(tmp, '#');
	    Strcat_charp(tmp, pu->label);
	}
	return tmp;
    }
    tmp = Strnew_charp(scheme_str[pu->scheme]);
    Strcat_char(tmp, ':');
#ifndef USE_W3MMAILER
    if (pu->scheme == SCM_MAILTO) {
	Strcat_charp(tmp, pu->file);
	if (pu->query) {
	    Strcat_char(tmp, '?');
	    Strcat_charp(tmp, pu->query);
	}
	return tmp;
    }
#endif
    if (pu->scheme == SCM_DATA) {
	Strcat_charp(tmp, pu->file);
	return tmp;
    }
#ifdef USE_NNTP
    if (pu->scheme != SCM_NEWS && pu->scheme != SCM_NEWS_GROUP)
#endif				/* USE_NNTP */
    {
	Strcat_charp(tmp, "//");
    }
    if (pu->user) {
	Strcat_charp(tmp, pu->user);
	if (pass && pu->pass) {
	    Strcat_char(tmp, ':');
	    Strcat_charp(tmp, pu->pass);
	}
	Strcat_char(tmp, '@');
    }
    if (pu->host) {
	Strcat_charp(tmp, pu->host);
	if (pu->port != DefaultPort[pu->scheme]) {
	    Strcat_char(tmp, ':');
	    Strcat(tmp, Sprintf("%d", pu->port));
	}
    }
    if (
#ifdef USE_NNTP
	   pu->scheme != SCM_NEWS && pu->scheme != SCM_NEWS_GROUP &&
#endif				/* USE_NNTP */
	   (pu->file == NULL || (pu->file[0] != '/'
#ifdef SUPPORT_DOS_DRIVE_PREFIX
				 && !(IS_ALPHA(pu->file[0])
				      && pu->file[1] == ':'
				      && pu->host == NULL)
#endif
	    )))
	Strcat_char(tmp, '/');
    Strcat_charp(tmp, pu->file);
    if (pu->scheme == SCM_FTPDIR && Strlastchar(tmp) != '/')
	Strcat_char(tmp, '/');
    if (pu->query) {
	Strcat_char(tmp, '?');
	Strcat_charp(tmp, pu->query);
    }
    if (pu->label) {
	Strcat_char(tmp, '#');
	Strcat_charp(tmp, pu->label);
    }
    return tmp;
}

Str
parsedURL2Str(ParsedURL *pu)
{
    return _parsedURL2Str(pu, FALSE);
}
#define ALLOC_STR(s) ((s)==NULL?NULL:allocStr(s,-1))
void
copyParsedURL(ParsedURL *p, ParsedURL *q)
{
    p->scheme = q->scheme;
    p->port = q->port;
    p->is_nocache = q->is_nocache;
    p->user = ALLOC_STR(q->user);
    p->pass = ALLOC_STR(q->pass);
    p->host = ALLOC_STR(q->host);
    p->file = ALLOC_STR(q->file);
    p->real_file = ALLOC_STR(q->real_file);
    p->label = ALLOC_STR(q->label);
    p->query = ALLOC_STR(q->query);
}
#ifdef INET6
/* see rc.c, "dns_order" and dnsorders[] */
int ai_family_order_table[7][3] = {
    {PF_UNSPEC, PF_UNSPEC, PF_UNSPEC},	/* 0:unspec */
    {PF_INET, PF_INET6, PF_UNSPEC},	/* 1:inet inet6 */
    {PF_INET6, PF_INET, PF_UNSPEC},	/* 2:inet6 inet */
    {PF_UNSPEC, PF_UNSPEC, PF_UNSPEC},  /* 3: --- */
    {PF_INET, PF_UNSPEC, PF_UNSPEC},    /* 4:inet */
    {PF_UNSPEC, PF_UNSPEC, PF_UNSPEC},  /* 5: --- */
    {PF_INET6, PF_UNSPEC, PF_UNSPEC},   /* 6:inet6 */
};
#endif				/* INET6 */

/* from etc.c */
char *
FQDN(char *host)
{
    char *p;
#ifndef INET6
    struct hostent *entry;
#else				/* INET6 */
    int *af;
#endif				/* INET6 */

    if (host == NULL)
	return NULL;

    if (strcasecmp(host, "localhost") == 0)
	return host;

    for (p = host; *p && *p != '.'; p++) ;

    if (*p == '.')
	return host;

#ifndef INET6
    if (!(entry = gethostbyname(host)))
	return NULL;

    return allocStr(entry->h_name, -1);
#else				/* INET6 */
    for (af = ai_family_order_table[DNS_order];; af++) {
	int error;
	struct addrinfo hints;
	struct addrinfo *res, *res0;
	char *namebuf;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = *af;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(host, NULL, &hints, &res0);
	if (error) {
	    if (*af == PF_UNSPEC) {
		/* all done */
		break;
	    }
	    /* try next address family */
	    continue;
	}
	for (res = res0; res != NULL; res = res->ai_next) {
	    if (res->ai_canonname) {
		/* found */
		namebuf = strdup(res->ai_canonname);
		freeaddrinfo(res0);
		return namebuf;
	    }
	}
	freeaddrinfo(res0);
	if (*af == PF_UNSPEC) {
	    break;
	}
    }
    /* all failed */
    return NULL;
#endif				/* INET6 */
}

/* from rc.h */
char *
rcFile(char *base)
{
    if (base &&
	(base[0] == '/' ||
	 (base[0] == '.'
	  && (base[1] == '/' || (base[1] == '.' && base[2] == '/')))
	 || (base[0] == '~' && base[1] == '/')))
	/* /file, ./file, ../file, ~/file */
	return expandPath(base);
    return expandPath(Strnew_m_charp(rc_dir, "/", base, NULL)->ptr);
}

void init_rc() {
    int i;
    rc_dir = expandPath(RC_DIR);
    i = strlen(rc_dir);
    if (i > 1 && rc_dir[i - 1] == '/')
	rc_dir[i - 1] = '\0';
}

/* original code */

FILE *input;

Str my_gets() {
    Str s;
    s = Strfgets(input);
    Strchop(s);
    /* "\1\n" is NULL */
    if (*s->ptr == '\1' && s->length == 1) return NULL;
    return s;
}

int my_gets_int() {
    return atoi(my_gets()->ptr);
}

int prepare_unixsock(int *fd, struct sockaddr_un *addr, Str *sock_name);
Str cookie_list_panel_str(void);

#define CSTR(s) (s ? s->ptr : "(null)")
static void dump(Str msg) {
    puts(CSTR(msg));
}
#define dump(msg)

int main(int argc, char **argv, char **envp) {
    Str sock_name;
    int fd_listen, fd;
    struct sockaddr_un saddr, caddr;

    use_cookie = 1;

    if (prepare_unixsock(&fd_listen, &saddr, &sock_name) < 0) {
	perror("socket");
	exit(1);
    }

    unlink(sock_name->ptr);
    if (bind(fd_listen, (struct sockaddr *)&saddr,
	     sizeof(saddr.sun_family) + sock_name->length + 1) < 0)
    {
	perror("bind");
	exit(1);
    }
    chmod(sock_name->ptr, 0700);
    if (listen(fd_listen, 1) < 0) {
	perror("listen");
	exit(1);
    }

    GC_init();
    init_rc();
    initCookie();

    while (1) {
	Str line;
	ParsedURL pu;
	Str cookie;
	FILE *output;
	unsigned int len = sizeof(caddr);

	if ((fd = accept(fd_listen, (struct sockaddr *)&caddr, &len)) < 0) {
	    perror("accept");
	    exit(1);
	}
	input = output = fdopen(fd, "r+");

	line = my_gets();
	if (!line || !*line->ptr) continue;
	dump(line);

	if (!Strcmp_charp(line, "GET")) {
	    parseURL(my_gets()->ptr, &pu, NULL);
	    cookie = find_cookie(&pu);
	    dump(cookie);
	    fputs(cookie ? cookie->ptr : "", output);
	    fputc('\n', output);
	}
	else if (!Strcmp_charp(line, "ADD")) {
	    Str url = my_gets();
	    Str name = my_gets();
	    Str value = my_gets();
	    int expires = my_gets_int();
	    Str domain = my_gets();
	    Str path = my_gets();
	    int flag = my_gets_int();
	    Str comment = my_gets();
	    int version = my_gets_int();
	    Str port = my_gets();
	    Str commentURL = my_gets();
	    int ret;

	    dump(Sprintf(
		     "url=%s name=%s value=%s expires=%d domain=%s path=%s "
		     "flag=%d comment=%s version=%d port=%s commentURL=%s",
		     CSTR(url), CSTR(name), CSTR(value), expires,
		     CSTR(domain), CSTR(path), flag, CSTR(comment),
		     version, CSTR(commentURL)));
	    parseURL(url->ptr, &pu, NULL);
	    ret = add_cookie(&pu, name, value, expires, domain, path,
			     flag, comment, version, port, commentURL);
	    fprintf(output, "%d\n", ret);
	    save_cookies();
	}
	else if (!Strcmp_charp(line, "CHECK_DOMAIN")) {
	    Str domain = my_gets();
	    fprintf(output, "%d\n", check_cookie_accept_domain(domain->ptr));
	}
	else if (!Strcmp_charp(line, "LIST")) {
	    Str src = cookie_list_panel_str();
	    fputs(src ? src->ptr : "ERROR???", output);
	    fputc('\n', output);
	}
	fclose(input);
    }
    save_cookies();
    return 0;
}
