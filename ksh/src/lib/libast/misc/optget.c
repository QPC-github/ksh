/*******************************************************************
*                                                                  *
*             This software is part of the ast package             *
*                Copyright (c) 1985-2004 AT&T Corp.                *
*        and it may only be used by you under license from         *
*                       AT&T Corp. ("AT&T")                        *
*         A copy of the Source Code Agreement is available         *
*                at the AT&T Internet web site URL                 *
*                                                                  *
*       http://www.research.att.com/sw/license/ast-open.html       *
*                                                                  *
*    If you have copied or used this software without agreeing     *
*        to the terms of the license you are infringing on         *
*           the license and copyright and are violating            *
*               AT&T's intellectual property rights.               *
*                                                                  *
*            Information and Software Systems Research             *
*                        AT&T Labs Research                        *
*                         Florham Park NJ                          *
*                                                                  *
*               Glenn Fowler <gsf@research.att.com>                *
*                David Korn <dgk@research.att.com>                 *
*                 Phong Vo <kpv@research.att.com>                  *
*                                                                  *
*******************************************************************/
#pragma prototyped
/*
 * Glenn Fowler
 * AT&T Research
 *
 * command line option parser and usage formatter
 * its a monster but its all in one place
 * widen your window while you're at it
 */

#include <optlib.h>
#include <debug.h>
#include <ccode.h>
#include <ctype.h>

#define KEEP		"*[A-Za-z][A-Za-z]*"
#define OMIT		"*@(\\[[-+]*\\?*\\]|\\@\\(#\\)|Copyright \\(c\\)|\\$\\I\\d\\: )*"

#define GO		'{'		/* group nest open		*/
#define OG		'}'		/* group nest close		*/

#define OPT_WIDTH	80		/* default help text width	*/
#define OPT_MARGIN	10		/* default help text margin	*/
#define OPT_USAGE	7		/* usage continuation indent	*/

#define OPT_flag	0001		/* flag ( 0 or 1 )		*/
#define OPT_hidden	0002		/* remaining are hidden		*/
#define OPT_ignorecase	0004		/* arg match ignores case	*/
#define OPT_invert	0010		/* flag inverts long sense	*/
#define OPT_listof	0020		/* arg is ' ' or ',' list	*/
#define OPT_number	0040		/* arg is strtonll() number	*/
#define OPT_oneof	0100		/* arg may be set once		*/
#define OPT_optional	0200		/* arg is optional		*/
#define OPT_string	0400		/* arg is string		*/

#define OPT_preformat	0001		/* output preformat string	*/

#define OPT_TYPE	(OPT_flag|OPT_number|OPT_string)

#define STYLE_short	0		/* [default] short usage	*/
#define STYLE_long	1		/* long usage			*/
#define STYLE_match	2		/* long description of matches	*/
#define STYLE_options	3		/* short and long descriptions	*/
#define STYLE_man	4		/* pretty details		*/
#define STYLE_html	5		/* html details			*/
#define STYLE_nroff	6		/* nroff details		*/
#define STYLE_api	7		/* program details		*/
#define STYLE_keys	8		/* translation key strings	*/
#define STYLE_usage	9		/* escaped usage string		*/

#define FONT_BOLD	1
#define FONT_ITALIC	2
#define FONT_LITERAL	4

typedef struct Attr_s
{
	const char*	name;
	int		flag;
} Attr_t;

typedef struct Help_s
{
	const char*	match;		/* builtin help match name	*/
	const char*	name;		/* builtin help name		*/
	int		style;		/* STYLE_*			*/
	const char*	text;		/* --? text			*/
	unsigned int	size;		/* strlen text			*/
} Help_t;

typedef struct Font_s
{
	const char*	html[2];
	const char*	nroff[2];
	const char*	term[2];
} Font_t;

typedef struct List_s
{
	int		type;		/* { - + : }			*/
	const char*	name;		/* list name			*/
	const char*	text;		/* help text			*/
} List_t;

typedef struct Msg_s
{
	const char*	text;		/* default message text		*/
	Dtlink_t	link;		/* cdt link			*/
} Msg_t;

typedef struct Save_s
{
	Dtlink_t	link;		/* cdt link			*/
	char		text[1];	/* saved text text		*/
} Save_t;

typedef struct Push_s
{
	struct Push_s*	next;		/* next string			*/
	char*		ob;		/* next char in old string	*/
	char*		oe;		/* end of old string		*/
	char*		nb;		/* next char in new string	*/
	char*		ne;		/* end of new string		*/
	int		ch;		/* localize() translation	*/
} Push_t;

typedef struct Indent_s
{
	int		stop;		/* tab column position		*/
} Indent_t;

static Indent_t		indent[] =
{
	0,2,	4,10,	12,18,	20,26,	28,34,	36,42,	44,50,	0,0
};

static const char	term_off[] =	{CC_esc,'[','0','m',0};
static const char	term_B_on[] =	{CC_esc,'[','1','m',0};
static const char	term_I_on[] =	{CC_esc,'[','1',';','4','m',0};

static const Font_t	fonts[] =
{
	"",	"",	"",	"",	"",			"",
	"</B>",	"<B>", "\\fP",	"\\fB",	&term_off[0],	&term_B_on[0],
	"</I>",	"<I>", "\\fP",	"\\fI",	&term_off[0],	&term_I_on[0],
	"",	"",	"",	"",	"",			"",
	"</TT>","<TT>","\\fP",	"\\f5",	"",			"",
};

#define C(s)		ERROR_catalog(s)
#define D(s)		(opt_info.state->msgdict && dtmatch(opt_info.state->msgdict, (s)))
#define T(i,c,m)	(X(c)?translate(i,c,C(m)):(m))
#define X(c)		(ERROR_translating()&&(c)!=native)
#define Z(x)		C(x),sizeof(x)-1

static char		native[] = "";

static const List_t	help_head[] =
{
	'-',	0,
		0,
	'+',	C("NAME"),
		C("options available to all \bast\b commands"),
	'+',	C("DESCRIPTION"),
		C("\b-?\b and \b--?\b* options are the same \
for all \bast\b commands. For any \aitem\a below, if \b--\b\aitem\a is not \
supported by a given command then it is equivalent to \b--\?\?\b\aitem\a. The \
\b--\?\?\b form should be used for portability. All output is written to the \
standard error."),
};

static const Help_t	styles[] =
{
	C("about"),	"-",		STYLE_match,
	Z("List all implementation info."),
	C("api"),	"?api",		STYLE_api,
	Z("List detailed info in program readable form."),
	C("help"),	"",		-1,
	Z("List detailed help option info."),
	C("html"),	"?html",	STYLE_html,
	Z("List detailed info in html."),
	C("keys"),	"?keys",	STYLE_keys,
	Z("List the usage translation key strings with C style escapes."),
	C("long"),	"?long",	STYLE_long,
	Z("List long option usage."),
	C("man"),	"?man",		STYLE_man,
	Z("List detailed info in displayed man page form."),
	C("nroff"),	"?nroff",	STYLE_nroff,
	Z("List detailed info in nroff."),
	C("options"),	"?options",	STYLE_options,
	Z("List short and long option details."),
	C("short"),	"?short",	STYLE_short,
	Z("List short option usage."),
	C("usage"),	"?usage",	STYLE_usage,
	Z("List the usage string with C style escapes."),
};

static const List_t	help_tail[] =
{
	':',	C("\?\?-\alabel\a"),
		C("List implementation info matching \alabel\a*."),
	':',	C("\?\?\aname\a"),
		C("Equivalent to \b--help=\b\aname\a."),
	':',	C("\?\?"),
		C("Equivalent to \b--\?\?options\b."),
	':',	C("\?\?\?\?"),
		C("Equivalent to \b--\?\?man\b."),
	':',	C("\?\?\?\?\?\?"),
		C("Equivalent to \b--\?\?help\b."),
	':',	C("\?\?\?\?\?\?\aitem\a"),
		C("If the next argument is \b--\b\aoption\a then list \
the \aoption\a output in the \aitem\a style. Otherwise print \
\bversion=\b\an\a where \an\a>0 if \b--\?\?\b\aitem\a is supported, \b0\b \
if not."),
	':',	C("\?\?\?\?\?\?ESC"),
		C("Emit escape codes even if output is not a terminal."),
	':',	C("\?\?\?\?\?\?TEST"),
		C("Massage the output for regression testing."),
};

static const Attr_t	attrs[] =
{
	"flag",		OPT_flag,
	"hidden",	OPT_hidden,
	"ignorecase",	OPT_ignorecase,
	"invert",	OPT_invert,
	"listof",	OPT_listof,
	"number",	OPT_number,
	"oneof",	OPT_oneof,
	"optional",	OPT_optional,
	"string",	OPT_string,
};

static const char	unknown[] = C("unknown option or attribute");

static const char*	heading[] =
{
	C("INDEX"),
	C("USER COMMANDS"),
	C("SYSTEM LIBRARY"),
	C("USER LIBRARY"),
	C("FILE FORMATS"),
	C("MISCELLANEOUS"),
	C("GAMES and DEMOS"),
	C("SPECIAL FILES"),
	C("ADMINISTRATIVE COMMANDS"),
	C("GUIs"),
};

/*
 * list of common man page strings
 * NOTE: add but do not delete from this table
 */

static Msg_t		C_LC_MESSAGES_libast[] =
{
	{ C("APPLICATION USAGE") },
	{ C("ASYNCHRONOUS EVENTS") },
	{ C("BUGS") },
	{ C("CAVEATS") },
	{ C("CONSEQUENCES OF ERRORS") },
	{ C("DESCRIPTION") },
	{ C("ENVIRONMENT VARIABLES") },
	{ C("EXAMPLES") },
	{ C("EXIT STATUS") },
	{ C("EXTENDED DESCRIPTION") },
	{ C("INPUT FILES") },
	{ C("LIBRARY") },
	{ C("NAME") },
	{ C("OPERANDS") },
	{ C("OPTIONS") },
	{ C("OUTPUT FILES") },
	{ C("SEE ALSO") },
	{ C("STDERR") },
	{ C("STDIN") },
	{ C("STDOUT") },
	{ C("SYNOPSIS") },
	{ C("author") },
	{ C("copyright") },
	{ C("license") },
	{ C("name") },
	{ C("path") },
	{ C("version") },
};

static unsigned char	map[UCHAR_MAX];

static Optstate_t	state;

Opt_t			opt_info = { 0,0,0,0,0,0,0,{0},{0},0,0,0,{0},{0},&state };

__EXTERN__(Opt_t, _opt_info_);

#if _BLD_DEBUG

/*
 * debug usage string segment format
 */

static char*
show(register char* s)
{
	register int	c;
	register char*	t;
	register char*	e;

	static char	buf[32];

	if (!s)
		return "(null)";
	t = buf;
	e = buf + sizeof(buf) - 2;
	while (t < e)
	{
		switch (c = *s++)
		{
		case 0:
			goto done;
		case '\a':
			*t++ = '\\';
			c = 'a';
			break;
		case '\b':
			*t++ = '\\';
			c = 'b';
			break;
		case '\f':
			*t++ = '\\';
			c = 'f';
			break;
		case '\v':
			*t++ = '\\';
			c = 'v';
			break;
		}
		*t++ = c;
	}
 done:
	*t = 0;
	return buf;
}

#endif

/*
 * translate with C_LC_MESSAGES_libast[] check
 */

static char*
translate(const char* cmd, const char* cat, const char* msg)
{
	if (!X(cat))
		return (char*)msg;
	if (cat != (const char*)ast.id && D(msg))
		cat = (const char*)ast.id;
	return errorx(NiL, cmd, cat, msg);
}

/*
 * pop the push stack
 */

static Push_t*
pop(register Push_t* psp)
{
	register Push_t*	tsp;

	while (tsp = psp)
	{
		psp = psp->next;
		free(tsp);
	}
	return 0;
}

/*
 * skip over line space to the next token
 */

static char*
next(register char* s, int version)
{
	register char*	b;

	while (*s == '\t' || *s == '\r' || version >= 1 && *s == ' ')
		s++;
	if (*s == '\n')
	{
		b = s;
		while (*++s == ' ' || *s == '\t' || *s == '\r');
		if (*s == '\n')
			return b;
	}
	return s;
}

/*
 * skip to t1 or t2 or t3, whichever first, in s
 *	n==0	outside [...]
 *	n==1	inside [...] before ?
 *	n==2	inside [...] after ?
 *	b==0	outside {...}
 *	b==1	inside {...}
 * past skips past the terminator to the next token
 * otherwise a pointer to the terminator is returned
 *
 * ]] for ] inside [...]
 * ?? for ? inside [...] before ?
 * :: for : inside [...] before ?
 */

static char*
skip(register char* s, register int t1, register int t2, register int t3, register int n, register int b, int past, int version)
{
	register int	c;
	register int	on = n;
	register int	ob = b;

	if (version < 1)
	{
		n = n >= 1;
		for (;;)
		{
			switch (*s++)
			{
			case 0:
				break;
			case '[':
				n++;
				continue;
			case ']':
				if (--n <= 0)
					break;
				continue;
			default:
				continue;
			}
			break;
		}
	}
	else while (c = *s++)
	{
		message((-22, "optget: skip t1=%c t2=%c t3=%c n=%d b=%d `%s'", t1 ? t1 : '@', t2 ? t2 : '@', t3 ? t3 : '@', n, b, show(s - 1)));
		if (c == '[')
		{
			if (!n)
				n = 1;
		}
		else if (c == ']')
		{
			if (n)
			{
				if (*s == ']')
					s++;
				else if (on == 1)
					break;
				else
					n = 0;
			}
		}
		else if (c == GO)
		{
			if (n == 0)
				b++;
		}
		else if (c == OG)
		{
			if (n == 0 && b-- == ob)
				break;
		}
		else if (c == '?')
		{
			if (n == 1)
			{
				if (*s == '?')
					s++;
				else
				{
					if (n == on && (c == t1 || c == t2 || c == t3))
						break;
					n = 2;
				}
			}
		}
		else if (n == on && (c == t1 || c == t2 || c == t3))
		{
			if (n == 1 && c == ':' && *s == c)
				s++;
			else
				break;
		}
	}
	return past && *(s - 1) ? next(s, version) : s - 1;
}

/*
 * match s with t
 * t translated if possible
 * imbedded { - ' } ignored
 * * separates required prefix from optional suffix
 * otherwise prefix match
 */

static int
match(char* s, char* t, int version, const char* catalog)
{
	register char*	w;
	register char*	x;
	int		n;
	int		v;

	for (n = 0; n < 2; n++)
	{
		switch (n)
		{
		case 0:
			if (catalog)
			{
				w = skip(t, ':', '?', 0, 1, 0, 0, version);
				w = sfprints("%-.*s", w - t, t);
				x = T(error_info.id, catalog, w);
				if (x != w)
					break;
			}
			x = T(NiL, ast.id, t);
			if (x == t)
				continue;
			break;
		case 1:
			x = t;
			break;
		}
		v = 0;
		w = s;
		for (;;)
		{
			if (!*w)
			{
				if (!v)
				{
					for (;;)
					{
						switch (*x++)
						{
						case 0:
						case ':':
						case '|':
						case '?':
						case ']':
							return 1;
						case '*':
							break;
						default:
							continue;
						}
						break;
					}
					break;
				}
				return 1;
			}
			else if (*w == *x)
			{
				w++;
				x++;
			}
			else if (*x == '-' || *x == '\'')
				x++;
			else if (!v && *x == '*')
			{
				x++;
				v = 1;
			}
			else if (*(x = skip(x, '|', 0, 0, 1, 0, 0, version)) == '|')
			{
				x++;
				v = 0;
				w = s;
			}
			else
				break;
		}
	}
	return 0;
}

/*
 * prefix search for s in tab with num elements of size
 * with optional translation
 */

static void*
search(const void* tab, size_t num, size_t siz, char* s)
{
	register char*	p;
	register char*	e;

	for (e = (p = (char*)tab) + num * siz; p < e; p += siz)
		if (match(s, *((char**)p), -1, NiL))
			return (void*)p;
	return 0;
}

/*
 * save s and return the saved pointer
 */

static char*
save(const char* s)
{
	Save_t*		p;

	static Dtdisc_t	disc;
	static Dt_t*	dict;

	if (!dict)
	{
		disc.key = offsetof(Save_t, text);
		if (!(dict = dtopen(&disc, Dthash)))
			return (char*)s;
	}
	if (!(p = (Save_t*)dtmatch(dict, s)))
	{
		if (!(p = newof(0, Save_t, 1, strlen(s))))
			return (char*)s;
		strcpy(p->text, s);
		dtinsert(dict, p);
	}
	return p->text;
}

/*
 * initialize the attributes for pass p from opt string s
 */

static void
init(register char* s, Optpass_t* p)
{
	register char*	t;
	register int	c;
	register int	n;

	if (!opt_info.state->msgdict)
	{
		if (!ast.locale.serial)
			setlocale(LC_ALL, "");
		opt_info.state->vp = sfstropen();
		opt_info.state->xp = sfstropen();
		opt_info.state->msgdisc.key = offsetof(Msg_t, text);
		opt_info.state->msgdisc.size = -1;
		opt_info.state->msgdisc.link = offsetof(Msg_t, link);
		if (opt_info.state->msgdict = dtopen(&opt_info.state->msgdisc, Dthash))
			for (n = 0; n < elementsof(C_LC_MESSAGES_libast); n++)
				dtinsert(opt_info.state->msgdict, C_LC_MESSAGES_libast + n);
		if (!map[OPT_FLAGS[0]])
			for (n = 0, t = OPT_FLAGS; *t; t++)
				map[*t] = ++n;
	}
#if _BLD_DEBUG
	error(-1, "optget debug");
#endif
	p->oopts = s;
	p->version = 0;
	p->prefix = 2;
	p->section = 1;
	p->flags = 0;
	p->catalog = 0;
	s = next(s, 0);
	if (*s == ':')
		s++;
	if (*s == '+')
		s++;
	s = next(s, 0);
	if (*s++ == '[')
	{
		if (*s == '+')
			p->version = 1;
		else if (*s++ == '-')
		{
			if (*s == '?' || *s == ']')
				p->version = 1;
			else
			{
				if (*s < '0' || *s > '9')
					p->version = 1;
				else
					while (*s >= '0' && *s <= '9')
						p->version = p->version * 10 + (*s++ - '0');
				while (*s && *s != '?' && *s != ']')
				{
					c = *s++;
					if (*s < '0' || *s > '9')
						n = 1;
					else
					{
						n = 0;
						while (*s >= '0' && *s <= '9')
							n = n * 10 + (*s++ - '0');
					}
					switch (c)
					{
					case 'c':
						p->flags |= OPT_cache;
						break;
					case 'i':
						p->flags |= OPT_ignore;
						break;
					case 'l':
						p->flags |= OPT_long;
						break;
					case 'o':
						p->flags |= OPT_old;
						break;
					case 'p':
						p->prefix = n;
						break;
					case 's':
						p->section = n;
						if (n > 1 && n < 6)
						{
							p->flags |= OPT_functions;
							p->prefix = 0;
						}
						break;
					}
				}
			}
		}
		while (*s)
			if (*s++ == ']' && *s++ == '[')
			{
				if (*s++ != '-')
				{
					if (!error_info.id && strneq(s - 1, "+NAME?", 6))
					{
						for (t = s += 5; *t && *t != ' ' && *t != ']'; t++);
						error_info.id = save(sfprints("%-.*s", t - s, s));
					}
					break;
				}
				if (*s == '-')
					s++;
				if (strneq(s, "catalog?", 8))
				{
					s += 8;
					if ((t = strchr(s, ']')) && (!error_info.id || (t - s) != strlen(error_info.id) || !strneq(s, error_info.id, t - s)))
						p->catalog = save(sfprints("%-.*s", t - s, s));
					if (error_info.id)
						break;
				}
			}
	}
	if (!p->catalog)
	{
		if (opt_info.disc && opt_info.disc->catalog && (!error_info.id || !streq(opt_info.disc->catalog, error_info.id)))
			p->catalog = opt_info.disc->catalog;
		else
			p->catalog = ast.id;
	}
	if (!error_info.catalog)
		error_info.catalog = p->catalog;
	s = p->oopts;
	if (*s == ':')
		s++;
	if (*s == '+')
	{
		s++;
		p->flags |= OPT_plus;
	}
	p->opts = s;
}

/*
 * return the bold set/unset sequence for style
 */

static const char*
font(int f, int style, int set)
{
	switch (style)
	{
	case STYLE_html:
		return fonts[f].html[set];
	case STYLE_nroff:
		return fonts[f].nroff[set];
	case STYLE_short:
	case STYLE_long:
	case STYLE_api:
		break;
	default:
		if (opt_info.state->emphasis > 0)
			return fonts[f].term[set];
		break;
	}
	return "";
}

/*
 * expand \f...\f info
 * *p set to next char after second \f
 * expanded value returned
 */

static char*
expand(register char* s, register char* e, char** p, Sfio_t* ip)
{
	register int	c;
	register char*	b = s;
	int		n;

	message((-23, "AHA#%d expand(%s)", __LINE__, show(s)));
	n = sfstrtell(ip);
	c = 1;
	while ((!e || s < e) && (c = *s++) && c != '\f');
	sfwrite(ip, b, s - b - 1);
	sfputc(ip, 0);
	b = sfstrbase(ip) + n;
	message((-23, "AHA#%d expand(%s)", __LINE__, b));
	n = sfstrtell(ip);
	if (!c)
		s--;
	if (*b == '?')
	{
		if (!*++b || streq(b, "NAME"))
		{
			if (!(b = error_info.id))
				b = "command";
			sfstrset(ip, 0);
			sfputr(ip, b, -1);
			n = 0;
		}
		else
			n = 1;
	}
	else if (!opt_info.disc || !opt_info.disc->infof || (*opt_info.disc->infof)(&opt_info, ip, b, opt_info.disc) < 0)
		n = 0;
	*p = s;
	return sfstruse(ip) + n;
}

/*
 * push \f...\f info
 */

static Push_t*
info(Push_t* psp, char* s, char* e, Sfio_t* ip)
{
	register char*	b;
	int		n;
	Push_t*		tsp;

	static Push_t	push;

	b = expand(s, e, &s, ip);
	n = strlen(b);
	if (tsp = newof(0, Push_t, 1, n + 1))
	{
		tsp->nb = (char*)(tsp + 1);
		tsp->ne = tsp->nb + n;
		strcpy(tsp->nb, b);
	}
	else
		tsp = &push;
	tsp->next = psp;
	tsp->ob = s;
	tsp->oe = e;
	return tsp;
}

/*
 * push translation
 */

static Push_t*
localize(Push_t* psp, char* s, char* e, int term, int n, char* catalog, int version, Sfio_t* ip)
{
	char*		t;
	char*		u;
	Push_t*		tsp;
	int		c;

	t = skip(s, term, 0, 0, n, 0, 0, version);
	if (e && t > e)
		t = e;
	while (s < t)
	{
		switch (c = *s++)
		{
		case ':':
		case '?':
			if (term && *s == c)
				s++;
			break;
		case ']':
			if (*s == c)
				s++;
			break;
		}
		sfputc(ip, c);
	}
	s = sfstruse(ip);
	if ((u = T(error_info.id, catalog, s)) == s)
		return 0;
	n = strlen(u);
	if (tsp = newof(0, Push_t, 1, n + 1))
	{
		tsp->nb = (char*)(tsp + 1);
		tsp->ne = tsp->nb + n;
		strcpy(tsp->nb, u);
		tsp->ob = t;
		tsp->oe = e;
		tsp->ch = 1;
	}
	tsp->next = psp;
	return tsp;
}

/*
 * output label s from [ ...label...[?...] ] to sp
 * 1 returned if the label was translated
 */

static int
label(register Sfio_t* sp, int sep, register char* s, int z, int level, int style, int f, Sfio_t* ip, int version, char* catalog)
{
	register int	c;
	register char*	t;
	register char*	e;
	int		ostyle;
	int		a;
	int		i;
	char*		p;
	char*		w;
	char*		y;
	int		va;
	Push_t*		tsp;

	int		r = 0;
	int		n = 1;
	Push_t*		psp = 0;

	if ((ostyle = style) > (STYLE_nroff - (sep <= 0)) && f != FONT_LITERAL)
		style = 0;
	if (z < 0)
		e = s + strlen(s);
	else
		e = s + z;
	if (sep > 0)
	{
		if (sep == ' ' && style == STYLE_nroff)
			sfputc(sp, '\\');
		sfputc(sp, sep);
	}
	sep = !sep || z < 0;
	va = 0;
	y = 0;
	if (version < 1)
	{
		a = 0;
		for (;;)
		{
			if (s >= e)
				return r;
			switch (c = *s++)
			{
			case '[':
				a++;
				break;
			case ']':
				if (--a < 0)
					return r;
				break;
			}
			sfputc(sp, c);
		}
	}
	else if (level && (*(p = skip(s, 0, 0, 0, 1, level, 1, version)) == ':' || *p == '#'))
	{
		va = 0;
		if (*++p == '?' || *p == *(p - 1))
		{
			p++;
			va |= OPT_optional;
		}
		if (*(p = next(p, version)) == '[')
			y = p + 1;
	}
	if (X(catalog) && (!level || *s == '\a' || *(s - 1) != '+') &&
	    (tsp = localize(psp, s, e, (sep || level) ? '?' : 0, sep || level, catalog, version, ip)))
	{
		psp= tsp;
		s = psp->nb;
		e = psp->ne;
		r = psp->ch > 0;
	}
	switch (*s)
	{
	case '\a':
		if (f == FONT_ITALIC)
			s++;
		f = 0;
		break;
	case '\b':
		if (f == FONT_BOLD)
			s++;
		f = 0;
		break;
	case '\v':
		if (f == FONT_LITERAL)
			s++;
		f = 0;
		break;
	default:
		if (f)
			sfputr(sp, font(f, style, 1), -1);
		break;
	}
	for (;;)
	{
		if (s >= e)
		{
			if (!(tsp = psp))
				goto restore;
			s = psp->ob;
			e = psp->oe;
			psp = psp->next;
			free(tsp);
			continue;
		}
		switch (c = *s++)
		{
		case '(':
			if (n)
			{
				n = 0;
				if (f)
				{
					sfputr(sp, font(f, style, 0), -1);
					f = 0;
				}
			}
			break;
		case '?':
		case ':':
		case ']':
			if (psp && psp->ch)
				break;
			if (y)
			{
				if (va & OPT_optional)
					sfputc(sp, '[');
				sfputc(sp, '=');
				label(sp, 0, y, -1, 0, style, FONT_ITALIC, ip, version, catalog);
				if (va & OPT_optional)
					sfputc(sp, ']');
				y = 0;
			}
			switch (c)
			{
			case '?':
				if (*s == '?')
					s++;
				else if (*s == ']' && *(s + 1) != ']')
					continue;
				else if (sep)
					goto restore;
				else if (X(catalog) && (tsp = localize(psp, s, e, 0, 1, catalog, version, ip)))
				{
					psp = tsp;
					s = psp->nb;
					e = psp->ne;
				}
				break;
			case ']':
				if (sep && *s++ != ']')
					goto restore;
				break;
			case ':':
				if (sep && *s++ != ':')
					goto restore;
				break;
			}
			break;
		case '\a':
			a = FONT_ITALIC;
		setfont:
			if (f & ~a)
			{
				sfputr(sp, font(f, style, 0), -1);
				f = 0;
			}
			if (!f && style == STYLE_html)
			{
				for (t = s; t < e && !isspace(*t) && !iscntrl(*t); t++);
				if (*t == c && *++t == '(')
				{
					w = t;
					while (++t < e && isdigit(*t));
					if (t < e && *t == ')' && t > w + 1)
					{
						sfprintf(sp, "<NOBR><A href=\"../man%-.*s/%-.*s.html\">%s%-.*s%s</A>%-.*s</NOBR>"
							, t - w - 1, w + 1
							, w - s - 1, s
							, font(a, style, 1)
							, w - s - 1, s
							, font(a, style, 0)
							, t - w + 1, w
							);
						s = t + 1;
						continue;
					}
				}
			}
			sfputr(sp, font(a, style, !!(f ^= a)), -1);
			continue;
		case '\b':
			a = FONT_BOLD;
			goto setfont;
		case '\f':
			psp = info(psp, s, e, ip);
			if (psp->nb)
			{
				s = psp->nb;
				e = psp->ne;
			}
			else
			{
				s = psp->ob;
				psp = psp->next;
			}
			continue;
		case '\n':
			sfputc(sp, c);
			for (i = 0; i < level; i++)
				sfputc(sp, '\t');
			continue;
		case '\v':
			a = FONT_LITERAL;
			goto setfont;
		case '<':
			if (style == STYLE_html)
			{
				sfputr(sp, "&lt;", -1);
				c = 0;
				for (t = s; t < e; t++)
					if (!isalnum(*t) && *t != '_' && *t != '.' && *t != '-')
					{
						if (*t == '@')
						{
							if (c)
								break;
							c = 1;
						}
						else if (*t == '>')
						{
							if (c)
							{
								sfprintf(sp, "<A href=\"mailto:%-.*s>%-.*s</A>&gt;", t - s, s, t - s, s);
								s = t + 1;
							}
							break;
						}
						else
							break;
					}
				continue;
			}
			break;
		case '>':
			if (style == STYLE_html)
			{
				sfputr(sp, "&gt;", -1);
				continue;
			}
			break;
		case '&':
			if (style == STYLE_html)
			{
				sfputr(sp, "&amp;", -1);
				continue;
			}
			break;
		case '-':
			if (ostyle == STYLE_nroff)
				sfputc(sp, '\\');
			break;
		case '.':
			if (ostyle == STYLE_nroff)
			{
				sfputc(sp, '\\');
				sfputc(sp, '&');
			}
			break;
		case '\\':
			if (ostyle == STYLE_nroff)
			{
				c = 'e';
				sfputc(sp, '\\');
			}
			break;
		case ' ':
			if (ostyle == STYLE_nroff)
				sfputc(sp, '\\');
			break;
		}
		sfputc(sp, c);
	}
 restore:
	if (f)
		sfputr(sp, font(f, style, 0), -1);
	if (psp)
		pop(psp);
	return r;
}

/*
 * output args description to sp from p of length n
 */

static void
args(register Sfio_t* sp, register char* p, register int n, int flags, int style, Sfio_t* ip, int version, char* catalog)
{
	register int	i;
	register char*	t;
	register char*	o;
	register char*	a = 0;
	char*		b;
	int		sep;

	if (flags & OPT_functions)
		sep = '\t';
	else
	{
		sep = ' ';
		o = T(NiL, ast.id, "options");
		b = style == STYLE_nroff ? "\\ " : " ";
		for (;;)
		{
			t = (char*)memchr(p, '\n', n);
			if (style >= STYLE_man)
			{
				if (!(a = error_info.id))
					a = "...";
				sfprintf(sp, "\t%s%s%s%s[%s%s%s%s%s]", font(FONT_BOLD, style, 1), a, font(FONT_BOLD, style, 0), b, b, font(FONT_ITALIC, style, 1), o, font(FONT_ITALIC, style, 0), b);
			}
			else if (a)
				sfprintf(sp, "%*.*s%s%s%s[%s%s%s]", OPT_USAGE - 1, OPT_USAGE - 1, T(NiL, ast.id, "Or:"), b, a, b, b, o, b);
			else
			{
				if (!(a = error_info.id))
					a = "...";
				if (!sfstrtell(sp))
					sfprintf(sp, "[%s%s%s]", b, o, b);
			}
			if (!t)
				break;
			i = ++t - p;
			if (i)
			{
				sfputr(sp, b, -1);
				if (X(catalog))
				{
					sfwrite(ip, p, i);
					sfputr(sp, T(error_info.id, catalog, sfstruse(ip)), -1);
				}
				else
					sfwrite(sp, p, i);
			}
			if (style == STYLE_html)
				sfputr(sp, "<BR>", '\n');
			else if (style == STYLE_nroff)
				sfputr(sp, ".br", '\n');
			else if (style == STYLE_api)
				sfputr(sp, ".BR", '\n');
			p = t;
			n -= i;
			while (n > 0 && (*p == ' ' || *p == '\t'))
			{
				p++;
				n--;
			}
		}
	}
	if (n)
		label(sp, sep, p, n, 0, style, 0, ip, version, catalog);
}

/*
 * output [+-...label...?...] label s to sp
 * according to {...} level and style
 * return 0:header 1:paragraph
 */

static int
item(Sfio_t* sp, char* s, int level, int style, Sfio_t* ip, int version, char* catalog)
{
	register char*	t;
	int		n;
	int		par;

	sfputc(sp, '\n');
	if (*s == '\n')
	{
		par = 0;
		if (style >= STYLE_nroff)
			sfprintf(sp, ".DS\n");
		else
		{
			if (style == STYLE_html)
				sfprintf(sp, "<PRE>\n");
			else
				sfputc(sp, '\n');
			for (n = 0; n < level; n++)
				sfputc(sp, '\t');
		}
		label(sp, 0, s + 1, -1, level, style, FONT_LITERAL, ip, version, catalog);
		sfputc(sp, '\n');
		if (style >= STYLE_nroff)
			sfprintf(sp, ".DE");
		else if (style == STYLE_html)
			sfprintf(sp, "</PRE>");
	}
	else if (*s != ']' && (*s != '?' || *(s + 1) == '?'))
	{
		par = 0;
		if (level)
		{
			if (style >= STYLE_nroff)
				sfprintf(sp, ".H%d ", (level + 1) / 2);
			else
				for (n = 0; n < level; n++)
					sfputc(sp, '\t');
		}
		if (style == STYLE_html)
		{
			if (!level)
				sfputr(sp, "<H4>", -1);
			sfputr(sp, "<A name=\"", -1);
			if (s[-1] == '-' && s[0] == 'l' && s[1] == 'i' && s[2] == 'c' && s[3] == 'e' && s[4] == 'n' && s[5] == 's' && s[6] == 'e' && s[7] == '?')
				for (t = s + 8; *t && *t != ']'; t++)
					if (t[0] == 'p' && strmatch(t, "(proprietary|private)*"))
					{
						opt_info.state->flags |= OPT_proprietary;
						break;
					}
			label(sp, 0, s, -1, level, 0, 0, ip, version, catalog);
			sfputr(sp, "\">", -1);
			label(sp, 0, s, -1, level, style, level ? FONT_BOLD : 0, ip, version, catalog);
			sfputr(sp, "</A>", -1);
			if (!level)
				sfputr(sp, "</H4>", -1);
		}
		else
		{
			if (!level)
			{
				if (style >= STYLE_nroff)
					sfprintf(sp, ".SH ");
				else if (style == STYLE_man)
					sfputc(sp, '\n');
				else if (style != STYLE_options && style != STYLE_match || *s == '-' || *s == '+')
					sfputc(sp, '\t');
			}
			label(sp, 0, s, -1, level, style, FONT_BOLD, ip, version, catalog);
		}
	}
	else
	{
		par = 1;
		if (style >= STYLE_nroff)
			sfputr(sp, ".PP", -1);
	}
	if (style >= STYLE_nroff || !level)
		sfputc(sp, '\n');
	if (par && style < STYLE_nroff)
		for (n = 0; n < level; n++)
			sfputc(sp, '\t');
	return par;
}

/*
 * output text to sp from p according to style
 */

static char*
text(Sfio_t* sp, register char* p, int style, int level, int bump, Sfio_t* ip, int version, char* catalog)
{
#if 0
#define text(a,b,c,d,e,f,g,h)	(sfprintf(a,"(%d)",__LINE__),text(a,b,c,d,e,f,g,h))
#endif
	register char*	t;
	register int	c;
	register int	n;
	char*		e;
	int		a;
	int		f;
	int		par;
	Push_t*		tsp;

	int		ident = 0;
	int		lev = level;
	Push_t*		psp = 0;

 again:
	if ((c = *p) == GO)
	{
		for (;;)
		{
			while (*(p = next(p + 1, version)) == '\n');
			if (*p == GO)
			{
				if (level > 1)
					level++;
				level++;
			}
			else if (*p != OG)
			{
				if (level <= 1 || *p != '[' || *(p + 1) != '-')
					break;
				p = skip(p, 0, 0, 0, 1, level, 0, version);
			}
			else if ((level -= 2) <= lev)
				return p + 1;
		}
		if (*p == '\f')
		{
			psp = info(psp, p + 1, NiL, ip);
			if (psp->nb)
				p = psp->nb;
			else
			{
				p = psp->ob;
				psp = psp->next;
			}
		}
		if (*p != '[')
			return p;
		c = *++p;
		if (level > 1)
			level++;
		level++;
	}
	if (c == '-' && level > 1)
		return skip(p, 0, 0, 0, 1, level, 1, version);
	if (c == '+' || c == '-' && (bump = 3) || c != ' ' && level > 1)
	{
		p = skip(t = p + 1, '?', 0, 0, 1, level, 0, version);
		if (c == '-' && (*t == '?' || *t >= '0' && *t <= '9'))
		{
			if ((c = *p) != '?')
				return skip(p, 0, 0, 0, 1, level, 1, version);
			par = item(sp, C("version"), level, style, ip, version, ast.id);
			for (;;)
			{
				while (isspace(*(p + 1)))
					p++;
				e = p;
				if (e[1] == '@' && e[2] == '(' && e[3] == '#' && e[4] == ')')
					p = e + 4;
				else if (e[1] == '$' && e[2] == 'I' && e[3] == 'd' && e[4] == ':' && e[5] == ' ')
				{
					p = e + 5;
					ident = 1;
				}
				else
					break;
			}
		}
		else
		{
			if (isdigit(c) && isdigit(*t))
			{
				while (isdigit(*t))
					t++;
				if (*t == ':')
					t++;
			}
			else if (isalnum(c) && *t-- == ':')
			{
				if (X(catalog) || *t == *(t + 2))
					t += 2;
				else
				{
					sfprintf(ip, "%s", t);
					t = sfstruse(ip);
					*(t + 1) = '|';
				}
			}
			par = item(sp, t, level, style, ip, version, catalog);
			c = *p;
		}
		if (level)
			par = 0;
	}
	else
	{
		if (style >= STYLE_nroff)
			sfputc(sp, '\n');
		else if (c == '?')
			for (n = 0; n < level; n++)
				sfputc(sp, '\t');
		par = 0;
	}
	if (c == ':')
		c = *(p = skip(p, '?', 0, 0, 1, 0, 0, version));
	if ((c == ']' || c == '?' && *(p + 1) == ']' && *(p + 2) != ']' && p++) && (c = *(p = next(p + 1, version))) == GO)
		p = text(sp, p, style, level + bump + par + 1, 0, ip, version, catalog);
	else if (c == '?' || c == ' ')
	{
		p++;
		if (c == ' ')
			sfputc(sp, c);
		else
		{
			if (X(catalog) && (tsp = localize(psp, p, NiL, 0, 1, catalog, version, ip)))
			{
				psp = tsp;
				p = psp->nb;
			}
			if (style < STYLE_nroff)
				for (n = 0; n < bump + 1; n++)
					sfputc(sp, '\t');
		}
		f = 0;
		for (;;)
		{
			switch (c = *p++)
			{
			case 0:
				if (!(tsp = psp))
				{
					if (f)
						sfputr(sp, font(f, style, 0), -1);
					return p - 1;
				}
				p = psp->ob;
				psp = psp->next;
				free(tsp);
				continue;
			case ']':
				if (psp && psp->ch)
					break;
				if (*p != ']')
				{
					if (f)
					{
						sfputr(sp, font(f, style, 0), -1);
						f = 0;
					}
					for (;;)
					{
						if ((*p == '#' || *p == ':') && level > lev)
						{
							char*	v;
							int	j;
							int	m;
							int	vl;

							a = 0;
							v = 0;
							if (*++p == '?' || *p == *(p - 1))
							{
								p++;
								a |= OPT_optional;
							}
							if (*(p = next(p, version)) == '[')
							{
								p = skip(p + 1, ':', '?', 0, 1, 0, 0, version);
								while (*p == ':')
								{
									p = skip(t = p + 1, ':', '?', 0, 1, 0, 0, version);
									m = p - t;
									if (*t == '=')
									{
										v = t + 1;
										vl = m - 1;
									}
									else
										for (j = 0; j < elementsof(attrs); j++)
											if (strneq(t, attrs[j].name, m))
											{
												a |= attrs[j].flag;
												break;
											}
								}
							}
							if (a & OPT_optional)
								sfprintf(sp, " %s", T(NiL, ast.id, "The option value may be omitted."));
							if (v)
							{
								sfprintf(sp, " %s ", T(NiL, ast.id, "The default value is"));
								sfputr(sp, font(FONT_BOLD, style, 1), -1);
								t = v + vl;
								while (v < t)
								{
									if (((c = *v++) == ':' || c == '?') && *v == c)
										v++;
									sfputc(sp, c);
								}
								sfputr(sp, font(FONT_BOLD, style, 0), -1);
								sfputc(sp, '.');
							}
							p = skip(p, 0, 0, 0, 1, 0, 1, version);
						}
						if (*(p = next(p, version)) == GO)
							p = text(sp, p, style, level + bump + !level, 0, ip, version, catalog);
						else if (*p == '[' && level > lev)
						{
							p++;
							goto again;
						}
						else if (*p == '\f')
						{
							p++;
							if (style != STYLE_keys)
							{
								psp = info(psp, p, NiL, ip);
								if (psp->nb)
									p = psp->nb;
								else
								{
									p = psp->ob;
									psp = psp->next;
								}
							}
						}
						else if (!*p)
						{
							if (!(tsp = psp))
								break;
							p = psp->ob;
							psp = psp->next;
							free(tsp);
						}
						else if (*p != OG)
							break;
						else
						{
							p++;
							if ((level -= 2) <= lev)
								break;
						}
					}
					return p;
				}
				p++;
				break;
			case '\a':
				a = FONT_ITALIC;
			setfont:
				if (f & ~a)
				{
					sfputr(sp, font(f, style, 0), -1);
					f = 0;
				}
				if (!f && style == STYLE_html)
				{
					for (t = p; *t && !isspace(*t) && !iscntrl(*t); t++);
					if (*t == c && *++t == '(')
					{
						e = t;
						while (isdigit(*++t));
						if (*t == ')' && t > e + 1)
						{
							sfprintf(sp, "<NOBR><A href=\"../man%-.*s/%-.*s.html\">%s%-.*s%s</A>%-.*s</NOBR>"
								, t - e - 1, e + 1
								, e - p - 1, p
								, font(a, style, 1)
								, e - p - 1, p
								, font(a, style, 0)
								, t - e + 1, e
								);
							p = t + 1;
							continue;
						}
					}
				}
				sfputr(sp, font(a, style, !!(f ^= a)), -1);
				continue;
			case '\b':
				a = FONT_BOLD;
				goto setfont;
			case '\f':
				if (style != STYLE_keys)
				{
					psp = info(psp, p, NiL, ip);
					if (psp->nb)
						p = psp->nb;
					else
					{
						p = psp->ob;
						psp = psp->next;
					}
				}
				continue;
			case '\v':
				a = FONT_LITERAL;
				goto setfont;
			case ' ':
				if (ident && *p == '$')
				{
					while (*++p)
						if (*p == ']')
						{
							if (*(p + 1) != ']')
								break;
							p++;
						}
					continue;
				}
			case '\n':
			case '\r':
			case '\t':
				while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
					p++;
				if (*p == ']' && *(p + 1) != ']' && (!psp || !psp->ch))
					continue;
				c = ' ';
				break;
			case '<':
				if (style == STYLE_html)
				{
					sfputr(sp, "&lt;", -1);
					c = 0;
					for (t = p; *t; t++)
						if (!isalnum(*t) && *t != '_' && *t != '.' && *t != '-')
						{
							if (*t == '@')
							{
								if (c)
									break;
								c = 1;
							}
							else if (*t == '>')
							{
								if (c)
								{
									sfprintf(sp, "<A href=\"mailto:%-.*s\">%-.*s</A>&gt;", t - p, p, t - p, p);
									p = t + 1;
								}
								break;
							}
							else
								break;
						}
					continue;
				}
				break;
			case '>':
				if (style == STYLE_html)
				{
					sfputr(sp, "&gt;", -1);
					continue;
				}
				break;
			case '&':
				if (style == STYLE_html)
				{
					sfputr(sp, "&amp;", -1);
					continue;
				}
				break;
			case '-':
				if (style == STYLE_nroff)
					sfputc(sp, '\\');
				break;
			case '.':
				if (style == STYLE_nroff)
				{
					sfputc(sp, '\\');
					sfputc(sp, '&');
				}
				break;
			case '\\':
				if (style == STYLE_nroff)
				{
					sfputc(sp, c);
					c = 'e';
				}
				break;
			}
			sfputc(sp, c);
		}
	}
	else if (c == '[' && level > lev)
	{
		p++;
		goto again;
	}
	return p;
}

/*
 * generate optget() help [...] list from lp
 */

static void
list(Sfio_t* sp, register const List_t* lp)
{
	sfprintf(sp, "[%c", lp->type);
	if (lp->name)
	{
		sfprintf(sp, "%s", lp->name);
		if (lp->text)
			sfprintf(sp, "?%s", lp->text);
	}
	sfputc(sp, ']');
}

/*
 * return pointer to help message sans `Usage: command'
 * if oopts is 0 then opt_info.state->pass is used
 * what:
 *	0	?short by default, ?long if any long options used
 *	*	otherwise see help_text[] (--???)
 * external formatter:
 *	\a...\a	italic
 *	\b...\b	bold
 *	\f...\f	discipline infof callback on ...
 *	\v...\v	literal
 * internal formatter:
 *	\t	indent
 *	\n	newline
 * margin flush pops to previous indent
 */

char*
opthelp(const char* oopts, const char* what)
{
	register Sfio_t*	sp;
	register Sfio_t*	mp;
	register int		c;
	register char*		p;
	register Indent_t*	ip;
	char*			t;
	char*			x;
	char*			w;
	char*			u;
	char*			y;
	char*			s;
	char*			d;
	char*			v;
	char*			name;
	char*			pp;
	char*			rb;
	char*			re;
	int			f;
	int			i;
	int			j;
	int			m;
	int			n;
	int			a;
	int			sl;
	int			vl;
	int			wl;
	int			xl;
	int			rm;
	int			ts;
	int			co;
	int			z;
	int			style;
	int			head;
	int			mode;
	int			mutex;
	int			prefix;
	int			version;
	long			tp;
	char*			catalog;
	Optpass_t*		o;
	Optpass_t*		q;
	Optpass_t*		e;
	Optpass_t		one;
	Help_t*			hp;
	short			ptstk[elementsof(indent) + 2];
	short*			pt;
	Sfio_t*			vp;
	Push_t*			tsp;

	char*			opts = (char*)oopts;
	int			flags = 0;
	int			matched = 0;
	int			paragraph = 0;
	int			section = 1;
	Push_t*			psp = 0;
	Sfio_t*			sp_help = 0;
	Sfio_t*			sp_text = 0;
	Sfio_t*			sp_plus = 0;
	Sfio_t*			sp_head = 0;
	Sfio_t*			sp_body = 0;
	Sfio_t*			sp_info = 0;
	Sfio_t*			sp_misc = 0;

	if (!(mp = opt_info.state->mp) && !(mp = opt_info.state->mp = sfstropen()))
		goto nospace;
	if (!what)
		style = opt_info.state->style;
	else if (!*what)
		style = STYLE_options;
	else if (*what != '?')
		style = STYLE_match;
	else if (!*(what + 1))
		style = STYLE_man;
	else if ((hp = (Help_t*)search(styles, elementsof(styles), sizeof(styles[0]), (char*)what + 1)) && hp->style >= 0)
	{
		style = hp->style;
		if (*hp->name != '?')
			what = hp->name;
	}
	else
	{
		if ((style = opt_info.state->force) < STYLE_man)
			style = STYLE_man;
		if (!(sp_help = sfstropen()))
			goto nospace;
		for (i = 0; i < elementsof(help_head); i++)
			list(sp_help, &help_head[i]);
		for (i = 0; i < elementsof(styles); i++)
			sfprintf(sp_help, "[:%s?%s]", styles[i].match, styles[i].text);
		for (i = 0; i < elementsof(help_tail); i++)
			list(sp_help, &help_tail[i]);
		opts = sfstruse(sp_help);
	}
	message((-20, "AHA#%d style=%d", __LINE__, style));
 again:
	if (opts)
	{
		for (i = 0; i < opt_info.state->npass; i++)
			if (opt_info.state->pass[i].oopts == opts)
			{
				o = &opt_info.state->pass[i];
				break;
			}
		if (i >= opt_info.state->npass)
		{
			o = &one;
			init((char*)opts, o);
		}
		e = o + 1;
	}
	else if (opt_info.state->npass > 0)
	{
		o = opt_info.state->pass;
		e = o + opt_info.state->npass;
	}
	else if (opt_info.state->npass < 0)
	{
		o = &opt_info.state->cache->pass;
		e = o + 1;
	}
	else
		return T(NiL, ast.id, "[* call optget() before opthelp() *]");
	if (style < STYLE_usage)
	{
		if (!(sp_text = sfstropen()) || !(sp_info = sfstropen()))
			goto nospace;
		if (style >= STYLE_match && style < STYLE_keys && !(sp_body = sfstropen()))
			goto nospace;
	}
	switch (style)
	{
	case STYLE_api:
	case STYLE_html:
	case STYLE_nroff:
		opt_info.state->emphasis = 0;
		break;
	case STYLE_usage:
	case STYLE_keys:
		for (q = o; q < e; q++)
			if (!(q->flags & OPT_ignore) && !streq(q->catalog, o->catalog))
				o = q;
		sfputc(mp, '\f');
		break;
	default:
		if (!opt_info.state->emphasis)
		{
			if (x = getenv("ERROR_OPTIONS"))
			{
				if (strmatch(x, "*noemphasi*"))
					break;
				if (strmatch(x, "*emphasi*"))
				{
					opt_info.state->emphasis = 1;
					break;
				}
			}
			if ((x = getenv("TERM")) && strmatch(x, "(ansi|vt100|xterm)*") && isatty(sffileno(sfstderr)))
				opt_info.state->emphasis = 1;
		}
		break;
	}
	x = "";
	xl = 0;
	for (q = o; q < e; q++)
	{
		if (q->flags & OPT_ignore)
			continue;
		if (section < q->section)
			section = q->section;
		section = q->section;
		flags |= q->flags;
		p = q->opts;
		prefix = q->prefix;
		version = q->version;
		catalog = q->catalog;
		switch (style)
		{
		case STYLE_usage:
			if (xl)
				sfputc(mp, '\n');
			else
				xl = 1;
			while (c = *p++)
			{
				switch (c)
				{
				case '\a':
					c = 'a';
					break;
				case '\b':
					c = 'b';
					break;
				case '\f':
					c = 'f';
					break;
				case '\n':
					c = 'n';
					break;
				case '\r':
					c = 'r';
					break;
				case '\t':
					c = 't';
					break;
				case '\v':
					c = 'v';
					break;
				case '"':
					c = '"';
					break;
				case '\'':
					c = '\'';
					break;
				case '\\':
					c = '\\';
					break;
				default:
					sfputc(mp, c);
					continue;
				}
				sfputc(mp, '\\');
				sfputc(mp, c);
			}
			continue;
		case STYLE_keys:
			a = 0;
			psp = 0;
			vl = 0;
			for (;;)
			{
				if (!(c = *p++))
				{
					if (!(tsp = psp))
						break;
					p = psp->ob;
					psp = psp->next;
					free(tsp);
					continue;
				}
				if (c == '\f')
				{
					psp = info(psp, p, NiL, sp_info);
					if (psp->nb)
						p = psp->nb;
					else
					{
						p = psp->ob;
						psp = psp->next;
					}
					continue;
				}
				f = z = 1;
				t = 0;
				if (a == 0 && (c == ' ' || c == '\n' && *p == '\n'))
				{
					if (c == ' ' && *p == ']')
					{
						p++;
						continue;
					}
					if (*p == '\n')
						p++;
					a = c;
				}
				else if (c == '\n')
				{
					if (a == ' ')
						a = -1;
					else if (a == '\n' || *p == '\n')
					{
						a = -1;
						p++;
					}
					continue;
				}
				else if ((c == ':' || c == '#') && (*p == '[' || *p == '?' && *(p + 1) == '[' && p++))
					p++;
				else if (c != '[')
				{
					if (c == '{')
						vl++;
					else if (c == '}')
						vl--;
					continue;
				}
				else if (*p == ' ')
				{
					p++;
					continue;
				}
				else if (*p == '-')
				{
					z = 0;
					if (*++p == '-')
					{
						p = skip(p, 0, 0, 0, 1, 0, 1, version);
						continue;
					}
				}
				else if (*p == '+')
				{
					p++;
					if (vl > 0 && *p != '\a')
					{
						f = 0;
						p = skip(p, '?', 0, 0, 1, 0, 0, version);
						if (*p == '?')
							p++;
					}
				}
				else
				{
					if (*(p + 1) == '\f' && (vp = opt_info.state->vp))
						p = expand(p + 2, NiL, &t, vp);
					p = skip(p, ':', '?', 0, 1, 0, 0, version);
					if (*p == ':')
						p++;
				}
				if (f && *p == '?' && *(p + 1) != '?')
				{
					f = 0;
					if (z)
						p++;
					else
						p = skip(p, 0, 0, 0, 1, 0, 0, version);
				}
				if (*p == ']' && *(p + 1) != ']')
				{
					p++;
					continue;
				}
				if (!*p)
				{
					if (!t)
						break;
					p = t;
					t = 0;
				}
				m = sfstrtell(mp);
				sfputc(mp, '"');
				xl = 1;
				/*UNDENT...*/

	for (;;)
	{
		if (!(c = *p++))
		{
			if (t)
			{
				p = t;
				t = 0;
			}
			if (!(tsp = psp))
			{
				p--;
				break;
			}
			p = psp->ob;
			psp = psp->next;
			free(tsp);
			continue;
		}
		if (c == '\f' && style != STYLE_keys)
		{
			psp = info(psp, p, NiL, sp_info);
			if (psp->nb)
				p = psp->nb;
			else
			{
				p = psp->ob;
				psp = psp->next;
			}
			continue;
		}
		if (a > 0)
		{
			if (c == '\n')
			{
				if (a == ' ')
				{
					a = -1;
					break;
				}
				if (a == '\n' || *p == '\n')
				{
					a = -1;
					p++;
					break;
				}
			}
		}
		else if (c == ']')
		{
			if (*p != ']')
			{
				sfputc(mp, 0);
				y = sfstrbase(mp) + m + 1;
				if (D(y) || !strmatch(y, KEEP) || strmatch(y, OMIT))
				{
					sfstrset(mp, m);
					xl = 0;
				}
				else
					sfstrrel(mp, -1);
				break;
			}
			sfputc(mp, *p++);
			continue;
		}
		switch (c)
		{
		case '?':
			if (f)
			{
				if (*p == '?')
				{
					p++;
					sfputc(mp, c);
				}
				else
				{
					f = 0;
					sfputc(mp, 0);
					y = sfstrbase(mp) + m + 1;
					if (D(y) || !strmatch(y, KEEP) || strmatch(y, OMIT))
					{
						sfstrset(mp, m);
						xl = 0;
					}
					else
						sfstrrel(mp, -1);
					if (z && (*p != ']' || *(p + 1) == ']'))
					{
						if (xl)
						{
							sfputc(mp, '"');
							sfputc(mp, '\n');
						}
						m = sfstrtell(mp);
						sfputc(mp, '"');
						xl = 1;
					}
					else
					{
						p = skip(p, 0, 0, 0, 1, 0, 0, version);
						if (*p == '?')
							p++;
					}
				}
			}
			else
				sfputc(mp, c);
			continue;
		case ':':
			if (f && *p == ':')
				p++;
			sfputc(mp, c);
			continue;
		case '\a':
			c = 'a';
			break;
		case '\b':
			c = 'b';
			break;
		case '\f':
			c = 'f';
			break;
		case '\n':
			c = 'n';
			break;
		case '\r':
			c = 'r';
			break;
		case '\t':
			c = 't';
			break;
		case '\v':
			c = 'v';
			break;
		case '"':
			c = '"';
			break;
		case '\\':
			c = '\\';
			break;
		case CC_esc:
			c = 'E';
			break;
		default:
			sfputc(mp, c);
			continue;
		}
		sfputc(mp, '\\');
		sfputc(mp, c);
	}

				/*...INDENT*/
				if (xl)
				{
					sfputc(mp, '"');
					sfputc(mp, '\n');
				}
			}
			continue;
		}
		z = 0;
		head = 0;
		mode = 0;
		mutex = 0;
		if (style > STYLE_short && style < STYLE_nroff && version < 1)
		{
			style = STYLE_short;
			if (sp_body)
			{
				sfclose(sp_body);
				sp_body = 0;
			}
		}
		else if (style <= STYLE_short && prefix < 2)
			style = STYLE_long;
		if (*p == ':')
			p++;
		if (*p == '+')
		{
			p++;
			if (!(sp = sp_plus) && !(sp = sp_plus = sfstropen()))
				goto nospace;
		}
		else if (style >= STYLE_match)
			sp = sp_body;
		else
			sp = sp_text;
		psp = 0;
		for (;;)
		{
			if (!(*(p = next(p, version))))
			{
				if (!(tsp = psp))
					break;
				p = psp->ob;
				psp = psp->next;
				free(tsp);
				continue;
			}
			if (*p == '\f')
			{
				psp = info(psp, p + 1, NiL, sp_info);
				if (psp->nb)
					p = psp->nb;
				else
				{
					p = psp->ob;
					psp = psp->next;
				}
				continue;
			}
			if (*p == '\n' || *p == ' ')
			{
				if (*(x = p = next(p + 1, version)))
					while (*++p)
						if (*p == '\n')
						{
							while (*++p == ' ' || *p == '\t' || *p == '\r');
							if (*p == '\n')
								break;
						}
				xl = p - x;
				if (!*p)
					break;
				continue;
			}
			if (*p == '}')
			{
				p++;
				continue;
			}
			message((-20, "opthelp: opt %s", show(p)));
			if (z < 0)
				z = 0;
			a = 0;
			f = 0;
			w = 0;
			d = 0;
			s = 0;
			sl = 0;
			if (*p == '[')
			{
				if ((c = *(p = next(p + 1, version))) == '-')
				{
					if (style >= STYLE_man)
					{
						if (*(p + 1) != '-')
						{
							if (!sp_misc && !(sp_misc = sfstropen()))
								goto nospace;
							else
								p = text(sp_misc, p, style, 1, 3, sp_info, version, catalog);
							continue;
						}
					}
					else if (style == STYLE_match && *what == '-')
					{
						if (*(p + 1) == '?' || *(p + 1) >= '0' && *(p + 1) <= '9')
							s = C("version");
						else
							s = p + 1;
						w = (char*)what;
						if (*s != '-' || *(w + 1) == '-')
						{
							if (*s == '-')
								s++;
							if (*(w + 1) == '-')
								w++;
							if (match(w + 1, s, version, catalog))
							{
								if (*(p + 1) == '-')
									p++;
								p = text(sp, p, style, 1, 3, sp_info, version, catalog);
								matched = -1;
								continue;
							}
						}
					}
					if (!z)
						z = -1;
				}
				else if (c == '+')
				{
					if (style >= STYLE_man)
					{
						p = text(sp_body, p, style, 0, 0, sp_info, version, catalog);
						if (!sp_head)
						{
							sp_head = sp_body;
							if (!(sp_body = sfstropen()))
								goto nospace;
						}
						continue;
					}
					else if (style == STYLE_match && *what == '+')
					{
						if (paragraph)
						{
							if (p[1] == '?')
							{
								p = text(sp, p, style, 1, 3, sp_info, version, catalog);
								continue;
							}
							paragraph = 0;
						}
						if (match((char*)what + 1, p + 1, version, catalog))
						{
							p = text(sp, p, style, 1, 3, sp_info, version, catalog);
							matched = -1;
							paragraph = 1;
							continue;
						}
					}
					if (!z)
						z = -1;
				}
				else if (c == '[' || version < 1)
				{
					mutex++;
					continue;
				}
				else
				{
					if (c == '!')
					{
						a |= OPT_invert;
						p++;
					}
					rb = p;
					if (*p != ':')
					{
						s = p;
						if (*(p + 1) == '|')
						{
							while (*++p && *p != '=' && *p != '!' && *p != ':' && *p != '?');
							if ((p - s) > 1)
								sl = p - s;
							if (*p == '!')
								a |= OPT_invert;
						}
						if (*(p + 1) == '\f')
							p++;
						else
							p = skip(p, ':', '?', 0, 1, 0, 0, version);
						if (sl || (p - s) == 1 || *(s + 1) == '=' || *(s + 1) == '!' && (a |= OPT_invert) || *(s + 1) == '|')
							f = *s;
					}
					re = p;
					if (style == STYLE_short)
					{
						if (!z && !f)
							z = -1;
					}
					else
					{
						if (*p == '\f' && (vp = opt_info.state->vp))
							p = expand(p + 1, NiL, &t, vp);
						else
							t = 0;
						if (*p == ':')
						{
							p = skip(w = p + 1, ':', '?', 0, 1, 0, 0, version);
							if (!(wl = p - w))
								w = 0;
						}
						else
							wl = 0;
						if (*p == ':' || *p == '?')
						{
							d = p;
							p = skip(p, 0, 0, 0, 1, 0, 0, version);
						}
						else
							d = 0;
						if (style == STYLE_match)
						{
							if (wl && !match((char*)what, w, version, catalog))
								wl = 0;
							if ((!wl || *w == ':' || *w == '?') && (what[1] || sl && !memchr(s, what[0], sl) || !sl && what[0] != f))
							{
								w = 0;
								if (!z)
									z = -1;
							}
							else
								matched = 1;
						}
						if (t)
						{
							p = t;
							if (*p == ':' || *p == '?')
							{
								d = p;
								p = skip(p, 0, 0, 0, 1, 0, 0, version);
							}
						}
					}
				}
				p = skip(p, 0, 0, 0, 1, 0, 1, version);
				if (*p == GO)
					p = skip(p + 1, 0, 0, 0, 0, 1, 1, version);
			}
			else if (*p == ']')
			{
				if (mutex)
				{
					if (style >= STYLE_nroff)
						sfputr(sp_body, "\n.OP - - anyof", '\n');
					if (!(mutex & 1))
					{
						mutex--;
						if (style <= STYLE_long)
						{
							sfputc(sp_body, ' ');
							sfputc(sp_body, ']');
						}
					}
					mutex--;
				}
				p++;
				continue;
			}
			else if (*p == '?')
			{
				if (style < STYLE_match)
					z = 1;
				mode |= OPT_hidden;
				p++;
				continue;
			}
			else
			{
				f = *p++;
				s = 0;
				if (style == STYLE_match && !z)
					z = -1;
			}
			if (!z)
			{
				if (style == STYLE_long || prefix < 2 || (q->flags & OPT_long))
					f = 0;
				else if (style == STYLE_short)
					w = 0;
				if (!f && !w)
					z = -1;
			}
			u = v = y = 0;
			if (*p == ':' && (a |= OPT_string) || *p == '#' && (a |= OPT_number))
			{
				message((-21, "opthelp: arg %s", show(p)));
				if (*++p == '?' || *p == *(p - 1))
				{
					p++;
					a |= OPT_optional;
				}
				if (*(p = next(p, version)) == '[')
				{
					if (!z)
					{
						p = skip(y = p + 1, ':', '?', 0, 1, 0, 0, version);
						while (*p == ':')
						{
							p = skip(t = p + 1, ':', '?', 0, 1, 0, 0, version);
							m = p - t;
							if (*t == '=')
							{
								v = t + 1;
								vl = m - 1;
							}
							else
								for (j = 0; j < elementsof(attrs); j++)
									if (strneq(t, attrs[j].name, m))
									{
										a |= attrs[j].flag;
										break;
									}
						}
						if (*p == '?')
							u = p;
						p = skip(p, 0, 0, 0, 1, 0, 1, version);
					}
					else
						p = skip(p + 1, 0, 0, 0, 1, 0, 1, version);
				}
				else
					y = (a & OPT_number) ? T(NiL, ast.id, "#") : T(NiL, ast.id, "arg");
			}
			else
				a |= OPT_flag;
			if (!z)
			{
				if (style == STYLE_short && !y && !mutex)
				{
					if (!sfstrtell(sp))
					{
						sfputc(sp, '[');
						if (sp == sp_plus)
							sfputc(sp, '+');
						sfputc(sp, '-');
					}
					if (!sl)
						sfputc(sp, f);
					else
						for (c = 0; c < sl; c++)
							if (s[c] != '|')
								sfputc(sp, s[c]);
				}
				else
				{
					if (style >= STYLE_match)
					{
						sfputc(sp_body, '\n');
						if (!head)
						{
							head = 1;
							item(sp_body, (flags & OPT_functions) ? C("FUNCTIONS") : C("OPTIONS"), 0, style, sp_info, version, ast.id);
						}
						if (style >= STYLE_nroff)
						{
							if (mutex & 1)
							{
								mutex++;
								sfputr(sp_body, "\n.OP - - oneof", '\n');
							}
						}
						else
							sfputc(sp_body, '\t');
					}
					else
					{
						if (sp_body)
							sfputc(sp_body, ' ');
						else if (!(sp_body = sfstropen()))
							goto nospace;
						if (mutex)
						{
							if (mutex & 1)
							{
								mutex++;
								sfputc(sp_body, '[');
							}
							else
								sfputc(sp_body, '|');
							sfputc(sp_body, ' ');
						}
						else
							sfputc(sp_body, '[');
					}
					if (style >= STYLE_nroff)
					{
						if (flags & OPT_functions)
						{
							sfputr(sp_body, ".FN", ' ');
							if (re > rb)
								sfwrite(sp_body, rb, re - rb);
							else
								sfputr(sp, "void", -1);
							if (w)
								label(sp_body, ' ', w, -1, 0, style, FONT_BOLD, sp_info, version, catalog);
						}
						else
						{
							sfputr(sp_body, ".OP", ' ');
							if (sl)
								sfwrite(sp_body, s, sl);
							else
								sfputc(sp_body, f ? f : '-');
							sfputc(sp_body, ' ');
							if (w)
							{
								if (label(sp_body, 0, w, -1, 0, style, 0, sp_info, version, catalog))
								{
									sfputc(sp_body, '|');
									label(sp_body, 0, w, -1, 0, style, 0, sp_info, version, native);
								}
							}
							else
								sfputc(sp_body, '-');
							sfputc(sp_body, ' ');
							m = a & OPT_TYPE;
							for (j = 0; j < elementsof(attrs); j++)
								if (m & attrs[j].flag)
								{
									sfputr(sp_body, attrs[j].name, -1);
									break;
								}
							if (m = (a & ~m) | mode)
								for (j = 0; j < elementsof(attrs); j++)
									if (m & attrs[j].flag)
									{
										sfputc(sp_body, ':');
										sfputr(sp_body, attrs[j].name, -1);
									}
							sfputc(sp_body, ' ');
							if (y)
								label(sp_body, 0, y, -1, 0, style, 0, sp_info, version, catalog);
							else
								sfputc(sp_body, '-');
							if (v)
								sfprintf(sp_body, " %-.*s", vl, v);
						}
					}
					else
					{
						if (f)
						{
							if (sp_body == sp_plus)
								sfputc(sp_body, '+');
							sfputc(sp_body, '-');
							sfputr(sp_body, font(FONT_BOLD, style, 1), -1);
							if (!sl)
								sfputc(sp_body, f);
							else
								sfwrite(sp_body, s, sl);
							sfputr(sp_body, font(FONT_BOLD, style, 0), -1);
							if (w)
							{
								sfputc(sp_body, ',');
								sfputc(sp_body, ' ');
							}
						}
						else if ((flags & OPT_functions) && re > rb)
						{
							sfwrite(sp_body, rb, re - rb);
							sfputc(sp_body, ' ');
						}
						if (w)
						{
							if (prefix > 0)
							{
								sfputc(sp_body, '-');
								if (prefix > 1)
									sfputc(sp_body, '-');
							}
							if (label(sp_body, 0, w, -1, 0, style, FONT_BOLD, sp_info, version, catalog))
							{
								sfputc(sp_body, '|');
								label(sp_body, 0, w, -1, 0, style, FONT_BOLD, sp_info, version, native);
							}
						}
						if (y)
						{
							if (a & OPT_optional)
								sfputc(sp_body, '[');
							else if (!w)
								sfputc(sp_body, ' ');
							if (w)
								sfputc(sp_body, prefix == 1 ? ' ' : '=');
							label(sp_body, 0, y, -1, 0, style, FONT_ITALIC, sp_info, version, catalog);
							if (a & OPT_optional)
								sfputc(sp_body, ']');
						}
					}
					if (style >= STYLE_match)
					{
						if (d)
							text(sp_body, d, style, 0, 3, sp_info, version, catalog);
						if (u)
							text(sp_body, u, style, 0, 3, sp_info, version, catalog);
						if ((a & OPT_invert) && w && (d || u))
						{
							u = skip(w, ':', '?', 0, 1, 0, 0, version);
							if (f)
								sfprintf(sp_info, " %s; -\b%c\b %s --\bno%-.*s\b.", T(NiL, ast.id, "On by default"), f, T(NiL, ast.id, "means"), u - w, w);
							else
								sfprintf(sp_info, " %s %s\bno%-.*s\b %s.", T(NiL, ast.id, "On by default; use"), "--"+2-prefix, u - w, w, T(NiL, ast.id, "to turn off"));
							text(sp_body, sfstruse(sp_info), style, 0, 0, sp_info, version, NiL);
						}
						if (*p == GO)
						{
							p = u ? skip(p + 1, 0, 0, 0, 0, 1, 1, version) : text(sp_body, p, style, 4, 0, sp_info, version, catalog);
							y = "+?";
						}
						else
							y = " ";
						if (a & OPT_optional)
						{
							sfprintf(sp_info, "%s%s", y, T(NiL, ast.id, "The option value may be omitted."));
							text(sp_body, sfstruse(sp_info), style, 4, 0, sp_info, version, NiL);
							y = " ";
						}
						if (v)
						{
							sfprintf(sp_info, "%s%s \b", y, T(NiL, ast.id, "The default value is"));
							t = v + vl;
							while (v < t)
							{
								if (((c = *v++) == ':' || c == '?') && *v == c)
									v++;
								sfputc(sp_info, c);
							}
							sfputc(sp_info, '\b');
							sfputc(sp_info, '.');
							text(sp_body, sfstruse(sp_info), style, 4, 0, sp_info, version, NiL);
						}
					}
					else if (!mutex)
						sfputc(sp_body, ']');
				}
				if (*p == GO)
				{
					if (style >= STYLE_match)
						p = text(sp_body, p, style, 4, 0, sp_info, version, catalog);
					else
						p = skip(p + 1, 0, 0, 0, 0, 1, 1, version);
				}
			}
			else if (*p == GO)
				p = skip(p + 1, 0, 0, 0, 0, 1, 1, version);
		}
		psp = pop(psp);
		if (sp_misc)
		{
			p = sfstruse(sp_misc);
			for (t = p; *t == '\t' || *t == '\n'; t++);
			if (*t)
			{
				item(sp_body, C("IMPLEMENTATION"), 0, style, sp_info, version, ast.id);
				sfputr(sp_body, p, -1);
			}
		}
	}
	version = o->version;
	catalog = o->catalog;
	if (style >= STYLE_keys)
	{
		if (sp_info)
			sfclose(sp_info);
		if (style == STYLE_keys && sfstrtell(mp) > 1)
			sfstrrel(mp, -1);
		return opt_info.msg = sfstruse(mp);
	}
	sp = sp_text;
	if (sfstrtell(sp))
		sfputc(sp, ']');
	if (style == STYLE_nroff)
	{
		sfprintf(sp, "\
.\\\" format with nroff|troff|groff -man\n\
.fp 5 CW\n\
.nr mI 0\n\
.de mI\n\
.if \\\\n(mI>\\\\$1 \\{\n\
.	nr mI \\\\n(mI-1\n\
.	RE\n\
.mI \\\\$1\n\
.\\}\n\
.if \\\\n(mI<\\\\$1 \\{\n\
.	nr mI \\\\n(mI+1\n\
.	RS\n\
.mI \\\\$1\n\
.\\}\n\
..\n\
.de H1\n\
.mI 1\n\
.TP\n\
\\fB\\\\$1\\fP\n\
..\n\
.de H2\n\
.mI 2\n\
.TP\n\
\\fB\\\\$1\\fP\n\
..\n\
.de H3\n\
.mI 3\n\
.TP\n\
\\fB\\\\$1\\fP\n\
..\n\
.de H4\n\
.mI 4\n\
.TP\n\
\\fB\\\\$1\\fP\n\
..\n\
.de OP\n\
.mI 0\n\
.ie !'\\\\$1'-' \\{\n\
.ds mO \\\\fB\\\\-\\\\$1\\\\fP\n\
.ds mS ,\\\\0\n\
.\\}\n\
.el \\{\n\
.ds mO \\\\&\n\
.ds mS \\\\&\n\
.\\}\n\
.ie '\\\\$2'-' \\{\n\
.if !'\\\\$4'-' .as mO \\\\0\\\\fI\\\\$4\\\\fP\n\
.\\}\n\
.el \\{\n\
.as mO \\\\*(mS\\\\fB%s\\\\$2\\\\fP\n\
.if !'\\\\$4'-' .as mO =\\\\fI\\\\$4\\\\fP\n\
.\\}\n\
.TP\n\
\\\\*(mO\n\
..\n\
.de FN\n\
.mI 0\n\
.TP\n\
\\\\$1 \\\\$2\n\
..\n\
.TH %s %d\n\
"
, o->prefix == 2 ? "\\\\-\\\\-" : o->prefix == 1 ? "\\\\-" : ""
, error_info.id
, section
);
	}
	if (style == STYLE_match)
	{
		if (!matched)
		{
			if (hp = (Help_t*)search(styles, elementsof(styles), sizeof(styles[0]), (char*)what))
			{
				if (!sp_help && !(sp_help = sfstropen()))
					goto nospace;
				sfprintf(sp_help, "[-][:%s?%s]", hp->match, hp->text);
				opts = sfstruse(sp_help);
				goto again;
			}
			s = (char*)unknown;
			goto nope;
		}
		else if (matched < 0)
			x = 0;
	}
	if (sp_plus)
	{
		if (sfstrtell(sp_plus))
		{
			if (sfstrtell(sp))
				sfputc(sp, ' ');
			sfputr(sp, sfstruse(sp_plus), ']');
		}
		sfclose(sp_plus);
	}
	if (style >= STYLE_man)
	{
		if (sp_head)
		{
			for (t = sfstruse(sp_head); *t == '\n'; t++);
			sfputr(sp, t, '\n');
			sfclose(sp_head);
			sp_head = 0;
		}
		item(sp, C("SYNOPSIS"), 0, style, sp_info, version, ast.id);
	}
	if (x)
	{
		for (t = x + xl; t > x && (*(t - 1) == '\n' || *(t - 1) == '\r'); t--);
		xl = t - x;
		if (style >= STYLE_match)
		{
			args(sp, x, xl, flags, style, sp_info, version, catalog);
			x = 0;
		}
	}
	if (sp_body)
	{
		if (sfstrtell(sp_body))
		{
			if (style < STYLE_match && sfstrtell(sp))
				sfputc(sp, ' ');
			sfputr(sp, sfstruse(sp_body), -1);
		}
		sfclose(sp_body);
	}
	if (x)
		args(sp, x, xl, flags, style, sp_info, version, catalog);
	if (sp_info)
	{
		sfclose(sp_info);
		sp_info = 0;
	}
	if (sp_misc)
	{
		sfclose(sp_misc);
		sp_misc = 0;
	}
	p = sfstruse(sp);
	name = error_info.id ? error_info.id : "command";
	m = strlen(name) + 1;
	if (!opt_info.state->width)
	{
		astwinsize(1, NiL, &opt_info.state->width);
		if (opt_info.state->width < 20)
			opt_info.state->width = OPT_WIDTH;
	}
	if (!(opt_info.state->flags & OPT_preformat))
	{
		if (style >= STYLE_man || matched < 0)
		{
			sfputc(mp, '\f');
			ts = 0;
		}
		else
			ts = OPT_USAGE + m;
		if (style == STYLE_html)
		{
			sfprintf(mp, "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n<HTML>\n<HEAD>\n<META name=\"generator\" content=\"optget (AT&T Labs Research) 2000-04-01\">\n%s<TITLE>%s man document</TITLE>\n</HEAD>\n<BODY bgcolor=white>\n", (opt_info.state->flags & OPT_proprietary) ? "<!--INTERNAL-->\n" : "", name);
			sfprintf(mp, "<H4><TABLE width=100%%><TR><TH align=left>&nbsp;%s&nbsp;(&nbsp;%d&nbsp;)&nbsp;<TH align=center><A href=\".\" title=\"Index\">%s</A><TH align=right>%s&nbsp;(&nbsp;%d&nbsp;)</TR></TABLE></H4>\n<HR>\n", name, section, T(NiL, ast.id, heading[section % 10]), name, section);
			sfprintf(mp, "<DL compact>\n<DT>");
			co = 2;
			*(pt = ptstk) = 0;
		}
		else
			co = 0;
		if ((rm = opt_info.state->width - ts - 1) < OPT_MARGIN)
			rm = OPT_MARGIN;
		ip = indent;
		ip->stop = (ip+1)->stop = style >= STYLE_html ? 0 : 2;
		tp = 0;
		n = 0;
		head = 1;
		while (*p == '\n')
			p++;
		while (c = *p++)
		{
			if (c == '\n')
			{
				ip = indent;
				n = 0;
				tp = 0;
				sfputc(mp, '\n');
				co = 0;
				rm = opt_info.state->width - 1;
				ts = ip->stop;
				if (*p == '\n')
				{
					while (*++p == '\n');
					if ((style == STYLE_man || style == STYLE_html) && (!head || *p != ' ' && *p != '\t'))
					{
						if (style == STYLE_man)
							p--;
						else
							sfprintf(mp, "<P>\n");
					}
				}
				head = *p != ' ' && *p != '\t';
				if (style == STYLE_html && (*p != '<' || !strneq(p, "<BR>", 4) && !strneq(p, "<P>", 3)))
				{
					y = p;
					while (*p == '\t')
						p++;
					if (*p == '\n')
						continue;
					j = p - y;
					if (j > *pt)
					{
						if (pt > ptstk)
							sfprintf(mp, "<DL compact>\n");
						*++pt = j;
						sfprintf(mp, "<DL compact>\n");
					}
					else while (j < *pt)
					{
						if (--pt > ptstk)
							sfprintf(mp, "</DL>\n");
						sfprintf(mp, "</DL>\n");
					}
					co += sfprintf(mp, "<DT>");
				}
			}
			else if (c == '\t')
			{
				if (style == STYLE_html)
				{
					while (*p == '\t')
						p++;
					if (*p != '\n')
						co += sfprintf(mp, "<DD>");
				}
				else
				{
					if ((ip+1)->stop)
					{
						do
						{
							ip++;
							if (*p != '\t')
								break;
							p++;
						} while ((ip+1)->stop);
						if (*p == '\n')
							continue;
						ts = ip->stop;
						if (co >= ts)
						{
							sfputc(mp, '\n');
							co = 0;
							rm = opt_info.state->width - 1;
							ts = ip->stop;
						}
					}
					while (co < ts)
					{
						sfputc(mp, ' ');
						co++;
					}
				}
			}
			else
			{
				if (c == ' ' && !n)
				{
					if (co >= rm)
						tp = 0;
					else
					{
						tp = sfstrtell(mp);
						pp = p;
					}
				}
				else if (style == STYLE_html)
				{
					if (c == '<')
					{
						if (strneq(p, "NOBR>", 5))
							n++;
						else if (n && strneq(p, "/NOBR>", 6) && !--n)
						{
							for (y = p += 6; (c = *p) && c != ' ' && c != '\t' && c != '\n' && c != '<'; p++)
								if (c == '[')
									sfputr(mp, "&#0091;", -1);
								else if (c == ']')
									sfputr(mp, "&#0093;", -1);
								else
									sfputc(mp, c);
							sfwrite(mp, "</NOBR", 6);
							c = '>';
							tp = 0;
							co += p - y + 6;
						}
					}
					else if (c == '>' && !n)
					{
						for (y = --p; (c = *p) && c != ' ' && c != '\t' && c != '\n' && c != '<'; p++)
							if (c == '[')
								sfputr(mp, "&#0091;", -1);
							else if (c == ']')
								sfputr(mp, "&#0093;", -1);
							else
								sfputc(mp, c);
						c = *sfstrrel(mp, -1);
						if (p > y + 1)
						{
							tp = 0;
							co += p - y - 1;
						}
						if (co >= rm)
							tp = 0;
						else
						{
							tp = sfstrtell(mp);
							pp = p;
						}
					}
					else if (c == '[')
					{
						sfputr(mp, "&#0091", -1);
						c = ';';
					}
					else if (c == ']')
					{
						sfputr(mp, "&#0093", -1);
						c = ';';
					}
					else if (c == 'h')
					{
						y = p;
						if (*y++ == 't' && *y++ == 't' && *y++ == 'p' && (*y == ':' || *y++ == 's' && *y == ':') && *y++ == ':' && *y++ == '/' && *y++ == '/')
						{
							while (isalnum(*y) || *y == '_' || *y == '/' || *y == '-' || *y == '.')
								y++;
							if (*y == '?')
								while (isalnum(*y) || *y == '_' || *y == '/' || *y == '-' || *y == '.' || *y == '?' || *y == '=' || *y == '%' || *y == '&' || *y == ';' || *y == '#')
									y++;
							if (*(y - 1) == '.')
								y--;
							p--;
							sfprintf(mp, "<A href=\"%-.*s\">%-.*s</A", y - p, p, y - p, p);
							p = y;
							c = '>';
						}
					}
					else if (c == 'C')
					{
						y = p;
						if (*y++ == 'o' && *y++ == 'p' && *y++ == 'y' && *y++ == 'r' && *y++ == 'i' && *y++ == 'g' && *y++ == 'h' && *y++ == 't' && *y++ == ' ' && *y++ == '(' && (*y++ == 'c' || *(y - 1) == 'C') && *y++ == ')')
						{
							sfputr(mp, "Copyright &copy", -1);
							p = y;
							c = ';';
						}
					}
				}
				else if (c == ']')
				{
					if (n)
						n--;
				}
				else if (c == '[')
					n++;
				if (c == CC_esc)
				{
					sfputc(mp, c);
					do
					{
						if (!(c = *p++))
						{
							p--;
							break;
						}
						sfputc(mp, c);
					} while (c < 'a' || c > 'z');
				}
				else if (co++ >= rm && !n)
				{
					if (tp)
					{
						if (*sfstrset(mp, tp) != ' ')
							sfstrrel(mp, 1);
						tp = 0;
						p = pp;
						n = 0;
					}
					else if (c != ' ' && c != '\n')
						sfputc(mp, c);
					if (*p == ' ')
						p++;
					if (*p != '\n')
					{
						sfputc(mp, '\n');
						for (co = 0; co < ts; co++)
							sfputc(mp, ' ');
						rm = opt_info.state->width - 1;
					}
				}
				else
					sfputc(mp, c);
			}
		}
		for (d = sfstrbase(mp), t = sfstrrel(mp, 0); t > d && ((c = *(t - 1)) == '\n' || c == '\r' || c == ' ' || c == '\t'); t--);
		sfstrset(mp, t - d);
		if (style == STYLE_html)
		{
			while (pt > ptstk)
			{
				if (--pt > ptstk)
					sfprintf(mp, "\n</DL>");
				sfprintf(mp, "\n</DL>");
			}
			sfprintf(mp, "</DL>\n</BODY>\n</HTML>");
		}
	}
	else
		sfputr(mp, p, 0);
	if (sp)
		sfclose(sp);
	return opt_info.msg = sfstruse(mp);
 nospace:
	s = T(NiL, ast.id, "[* out of space *]");
 nope:
	if (psp)
		pop(psp);
	if (sp_help)
		sfclose(sp_help);
	if (sp_text)
		sfclose(sp_text);
	if (sp_plus)
		sfclose(sp_plus);
	if (sp_info)
		sfclose(sp_info);
	if (sp_head)
		sfclose(sp_head);
	if (sp_body)
		sfclose(sp_body);
	if (sp_misc)
		sfclose(sp_misc);
	return s;
}

/*
 * compatibility wrapper to opthelp()
 */

char*
optusage(const char* opts)
{
	return opthelp(opts, NiL);
}

/*
 * point opt_info.arg to an error/info message for opt_info.name
 * p points to opts location for opt_info.name
 * optget() return value is returned
 */

static int
opterror(register char* p, int version, char* catalog)
{
	register Sfio_t*	mp;
	register Sfio_t*	tp;
	register char*		s;
	register int		c;

	if (opt_info.num != LONG_MIN)
		opt_info.num = opt_info.number = 0;
	if (!p || !(mp = opt_info.state->mp) && !(mp = opt_info.state->mp = sfstropen()))
		opt_info.arg = T(NiL, ast.id, "[* out of space *]");
	else
	{
		s = *p == '-' ? p : opt_info.name;
		if (*p == '!')
		{
			while (*s == '-')
				sfputc(mp, *s++);
			sfputc(mp, 'n');
			sfputc(mp, 'o');
		}
		sfputr(mp, s, ':');
		sfputc(mp, ' ');
		if (*p == '#' || *p == ':')
		{
			if (*p == '#')
			{
				s = T(NiL, ast.id, "numeric");
				sfputr(mp, s, ' ');
			}
			if (*(p = next(p + 1, version)) == '[')
			{
				p = skip(s = p + 1, ':', '?', 0, 1, 0, 0, version);
				tp = X(catalog) ? opt_info.state->xp : mp;
				while (s < p)
				{
					if ((c = *s++) == '?' || c == ']')
						s++;
					sfputc(tp, c);
				}
				if (X(catalog))
					sfputr(mp, T(error_info.id, catalog, sfstruse(tp)), ' ');
				else
					sfputc(mp, ' ');
			}
			p = opt_info.name[2] ? C("value expected") : C("argument expected");
		}
		else if (*p == '*')
		{
			sfputr(mp, opt_info.arg, ':');
			sfputc(mp, ' ');
			p = C("unknown option argument value");
		}
		else if (*p == '=' || *p == '!')
			p = C("value not expected");
		else if (*p == '?')
			p = *(p + 1) == '?' ? C("optget: option not supported") : C("ambiguous option");
		else if (*p == '+')
			p = C("section not found");
		else
		{
			if (opt_info.option[0] != '?' && opt_info.option[0] != '-' || opt_info.option[1] != '?' && opt_info.option[1] != '-' || opt_info.option[1] != '?')
				opt_info.option[0] = 0;
			p = C("unknown option");
		}
		p = T(NiL, ast.id, p);
		sfputr(mp, p, -1);
		opt_info.arg = sfstruse(mp);
	}
	return ':';
}

/*
 * argv:	command line argv where argv[0] is command name
 *
 * opts:	option control string
 *
 *	'[' [flag][=][index][:<long-name>[|<alias-name>...]['?'description]] ']'
 *			long option name, index, description; -index returned
 *	':'		option takes string arg
 *	'#'		option takes numeric arg (concat option may follow)
 *	'?'		(option) following options not in usage
 *			(following # or :) optional arg
 *	'[' '[' ... ] ... '[' ... ']' ']'
 *			mutually exclusive option grouping
 *	'[' name [:attr]* [?description] ']'
 *			(following # or :) optional option arg description
 *	'\n'[' '|'\t']*	ignored for legibility
 *	' ' ...		optional argument(s) description (to end of string)
 *			or after blank line
 *	']]'		literal ']' within '[' ... ']'
 *
 * return:
 *	0		no more options
 *	'?'		usage: opt_info.arg points to message sans
 *			`Usage: command '
 *	':'		error: opt_info.arg points to message sans `command: '
 *
 * '-' '+' '?' ':' '#' '[' ']' ' '
 *			invalid option chars
 *
 * -- terminates option list and returns 0
 *
 * + as first opts char makes + equivalent to -
 *
 * if any # option is specified then numeric options (e.g., -123)
 * are associated with the leftmost # option in opts
 *
 * usage info in placed opt_info.arg when '?' returned
 * see help_text[] (--???) for more info
 */

int
optget(register char** argv, const char* oopts)
{
	register int	c;
	register char*	s;
	char*		a;
	char*		b;
	char*		e;
	char*		f;
	char*		g;
	char*		v;
	char*		w;
	char*		p;
	char*		t;
	char*		numopt;
	char*		opts;
	char*		catalog;
	int		n;
	int		m;
	int		k;
	int		j;
	int		x;
	int		no;
	int		nov;
	int		num;
	int		numchr;
	int		prefix;
	int		version;
	Help_t*		hp;
	Push_t*		psp;
	Push_t*		tsp;
	Sfio_t*		vp;
	Sfio_t*		xp;
	Optcache_t*	cache;
	Optcache_t*	pcache;
	Optpass_t*	pass;

	opt_info.state = &state; /* not initialized in some dll's! */
	if (!oopts)
		return 0;
	opt_info.state->pindex = opt_info.index;
	opt_info.state->poffset = opt_info.offset;
	if (!opt_info.index)
	{
		opt_info.index = 1;
		opt_info.offset = 0;
		if (opt_info.state->npass)
		{
			opt_info.state->npass = 0;
			opt_info.state->join = 0;
		}
	}
	if (!argv)
		cache = 0;
	else
		for (pcache = 0, cache = opt_info.state->cache; cache; pcache = cache, cache = cache->next)
			if (cache->pass.oopts == (char*)oopts)
				break;
	if (cache)
	{
		if (pcache)
		{
			pcache->next = cache->next;
			cache->next = opt_info.state->cache;
			opt_info.state->cache = cache;
		}
		pass = &cache->pass;
		opt_info.state->npass = -1;
	}
	else
	{
		if (!argv)
			n = opt_info.state->npass ? opt_info.state->npass : 1;
		else if ((n = opt_info.state->join - 1) < 0)
			n = 0;
		if (n >= opt_info.state->npass || opt_info.state->pass[n].oopts != (char*)oopts)
		{
			for (m = 0; m < opt_info.state->npass && opt_info.state->pass[m].oopts != (char*)oopts; m++);
			if (m < opt_info.state->npass)
				n = m;
			else
			{
				if (n >= elementsof(opt_info.state->pass))
					n = elementsof(opt_info.state->pass) - 1;
				init((char*)oopts, &opt_info.state->pass[n]);
				if (opt_info.state->npass <= n)
					opt_info.state->npass = n + 1;
			}
		}
		if (!argv)
			return 0;
		pass = &opt_info.state->pass[n];
	}
	opts = pass->opts;
	prefix = pass->prefix;
	version = pass->version;
	if (!(xp = opt_info.state->xp) || (catalog = pass->catalog) && !X(catalog))
		catalog = 0;
 again:
	psp = 0;

	/*
	 * check if any options remain and determine if the
	 * next option is short or long
	 */

	opt_info.assignment = 0;
	num = 1;
	w = v = 0;
	for (;;)
	{
		if (!opt_info.offset)
		{
			/*
			 * finished with the previous arg
			 */

			if (opt_info.index == 1)
			{
				opt_info.argv = 0;
				opt_info.state->argv[0] = 0;
				if (argv[0] && (opt_info.state->argv[0] = save(argv[0])))
					opt_info.argv = opt_info.state->argv;
				opt_info.state->style = STYLE_short;
			}
			if (!(s = argv[opt_info.index]))
				return 0;
			if (!prefix)
			{
				/*
				 * long with no prefix (dd style)
				 */

				n = 2;
				if ((c = *s) != '-' && c != '+')
					c = '-';
				else if (*++s == c)
				{
					if (!*++s)
					{
						opt_info.index++;
						return 0;
					}
				}
				else if (*s == '?')
					n = 1;
			}
			else if ((c = *s++) != '-' && (c != '+' || !(pass->flags & OPT_plus) && (*s < '0' || *s > '9' || !strmatch(opts, version ? "*\\]#\\[*" : "*#*"))))
			{
				if (!(pass->flags & OPT_old) || !isalpha(c))
					return 0;
				s--;
				n = 1;
				opt_info.offset--;
			}
			else if (*s == c)
			{
				if (!*++s)
				{
					/*
					 * -- or ++ end of options
					 */

					opt_info.index++;
					return 0;
				}

				/*
				 * long with double prefix
				 */

				n = 2;
			}
			else if (prefix == 1 && *s != '?')
			{
				/*
				 * long with single prefix (find style)
				 */

				n = 2;
			}
			else
			{
				/*
				 * short (always with single prefix)
				 */

				n = 1;
			}

			/*
			 * just a prefix is an option (e.g., `-' == stdin)
			 */

			if (!*s)
				return 0;
			if (c == '+')
				opt_info.arg = 0;
			if (n == 2)
			{
				x = 0;
				opt_info.state->style = STYLE_long;
				opt_info.option[0] = opt_info.name[0] = opt_info.name[1] = c;
				w = &opt_info.name[prefix];
				if ((*s == 'n' || *s == 'N') && (*(s + 1) == 'o' || *(s + 1) == 'O') && *(s + 2) && *(s + 2) != '=')
					no = *(s + 2) == '-' ? 3 : 2;
				else
					no = 0;
				for (c = *s; *s; s++)
				{
					if (*s == '=')
					{
						if (*(s + 1) == '=')
							s++;
						if (!isalnum(*(s - 1)) && *(w - 1) == (opt_info.assignment = *(s - 1)))
							w--;
						v = ++s;
						break;
					}
					if (w < &opt_info.name[elementsof(opt_info.name) - 1] && *s != ':' && *s != '|' && *s != '[' && *s != ']')
						*w++ = *s;
				}
				*w = 0;
				w = &opt_info.name[prefix];
				c = *w;
				opt_info.offset = 0;
				opt_info.index++;
				break;
			}
			opt_info.offset++;
		}
		if (!argv[opt_info.index])
			return 0;
		if (c = argv[opt_info.index][opt_info.offset++])
		{
			if ((k = argv[opt_info.index][0]) != '-' && k != '+')
				k = '-';
			opt_info.option[0] = opt_info.name[0] = k;
			opt_info.option[1] = opt_info.name[1] = c;
			opt_info.option[2] = opt_info.name[2] = 0;
			break;
		}
		opt_info.offset = 0;
		opt_info.index++;
	}

	/*
	 * at this point:
	 *
	 *	c	the first character of the option
	 *	w	long option name if != 0, otherwise short
	 *	v	long option value (via =) if w != 0
	 */

	if (c == '?')
	{
		/*
		 * ? always triggers internal help
		 */

		if (w && !v && (*(w + 1) || !(v = argv[opt_info.index]) || !++opt_info.index))
			v = w + 1;
		opt_info.option[1] = c;
		opt_info.option[2] = 0;
		if (!w)
		{
			opt_info.name[1] = c;
			opt_info.name[2] = 0;
		}
		goto help;
	}
	numopt = 0;
	f = 0;
	s = opts;

	/*
	 * no option can start with these characters
	 */

	if (c == ':' || c == '#' || c == ' ' || c == '[' || c == ']')
	{
		if (c != *s)
			s = "";
	}
	else
	{
		if (!w && (pass->flags & OPT_cache))
		{
			if (cache)
			{
				if (k = cache->flags[map[c]])
				{
					opt_info.arg = 0;

					/*
					 * this is a ksh getopts workaround
					 */

					if (opt_info.num != LONG_MIN)
						opt_info.num = opt_info.number = !(k & OPT_cache_invert);
					if (!(k & (OPT_cache_string|OPT_cache_numeric)))
						return c;
					if (*(opt_info.arg = &argv[opt_info.index++][opt_info.offset]))
					{
						if (!(k & OPT_cache_numeric))
						{
							opt_info.offset = 0;
							return c;
						}
						opt_info.num = (long)(opt_info.number = strtonll(opt_info.arg, &e, NiL, 0));
						if (e == opt_info.arg)
						{
							if (k & OPT_cache_optional)
							{
								opt_info.arg = 0;
								opt_info.index--;
								return c;
							}
						}
						else if (*e)
						{
							opt_info.offset += e - opt_info.arg;
							opt_info.index--;
							return c;
						}
						else
						{
							opt_info.offset = 0;
							return c;
						}
					}
					else if (opt_info.arg = argv[opt_info.index])
					{
						opt_info.index++;
						if ((k & OPT_cache_optional) && (*opt_info.arg == '-' || (pass->flags & OPT_plus) && *opt_info.arg == '+') && *(opt_info.arg + 1))
						{
							opt_info.arg = 0;
							opt_info.index--;
							opt_info.offset = 0;
							return c;
						}
						if (k & OPT_cache_string)
						{
							opt_info.offset = 0;
							return c;
						}
						opt_info.num = (long)(opt_info.number = strtonll(opt_info.arg, &e, NiL, 0));
						if (!*e)
						{
							opt_info.offset = 0;
							return c;
						}
						if (k & OPT_cache_optional)
						{
							opt_info.arg = 0;
							opt_info.index--;
							opt_info.offset = 0;
							return c;
						}
					}
					else if (k & OPT_cache_optional)
					{
						opt_info.offset = 0;
						return c;
					}
					opt_info.index--;
				}
				cache = 0;
			}
			else if (cache = newof(0, Optcache_t, 1, 0))
			{
				cache->caching = c;
				c = 0;
				cache->pass = *pass;
				cache->next = opt_info.state->cache;
				opt_info.state->cache = cache;
			}
		}
		else
			cache = 0;
		for (;;)
		{
			if (!(*(s = next(s, version))) || *s == '\n' || *s == ' ')
			{
				if (!(tsp = psp))
				{
					if (cache)
					{
						/*
						 * the first loop pass
						 * initialized the cache
						 * so one more pass to
						 * check the cache or
						 * bail for a full scan
						 */

						cache->flags[0] = 0;
						c = cache->caching;
						cache->caching = 0;
						cache = 0;
						s = opts;
						continue;
					}
					if (!x && catalog)
					{
						/*
						 * the first loop pass
						 * translated long
						 * options and there
						 * were no matches so
						 * one more pass for C
						 * locale
						 */

						catalog = 0;
						s = opts;
						continue;
					}
					s = "";
					break;
				}
				s = psp->ob;
				psp = psp->next;
				free(tsp);
				continue;
			}
			if (*s == '\f')
			{
				psp = info(psp, s + 1, NiL, opt_info.state->xp);
				if (psp->nb)
					s = psp->nb;
				else
				{
					s = psp->ob;
					psp = psp->next;
				}
				continue;
			}
			message((-20, "optget: opt %s w %s num %ld", show(s), w, num));
			if (*s == c && !w)
				break;
			else if (*s == '[')
			{
				f = s = next(s + 1, version);
				k = *f;
				if (k == '+' || k == '-')
					/* ignore */;
				else if (k == '[' || version < 1)
					continue;
				else if (w && !cache)
				{
					nov = no;
					if (*(s + 1) == '\f' && (vp = opt_info.state->vp))
					{
						sfputc(vp, k);
						s = expand(s + 2, NiL, &t, vp);
						if (*s)
							*(f = s - 1) = k;
						else
						{
							f = sfstrbase(vp);
							if (s = strrchr(f, ':'))
								f = s - 1;
							else
								s = f + 1;
						}
					}
					else
						t = 0;
					if (*s != ':')
						s = skip(s, ':', '?', 0, 1, 0, 0, version);
					if (*s == ':')
					{
						if (catalog)
						{
							p = skip(s + 1, '?', 0, 0, 1, 0, 0, version);
							e = sfprints("%-.*s", p - (s + 1), s + 1);
							g = T(error_info.id, catalog, e);
							if (g == e)
								p = 0;
							else
							{
								sfprintf(xp, ":%s|%s?", g, e);
								s = sfstruse(xp);
							}
						}
						else
							p = 0;
						for (;;)
						{
							n = m = 0;
							e = s + 1;
							while (*++s)
							{
								if (*s == '*' || *s == '\a')
								{
									if (*s == '\a')
										do
										{
											if (!*++s)
											{
												s--;
												break;
											}
										} while (*s != '\a');
									j = *(s + 1);
									if (j == ':' || j == '|' || j == '?' || j == ']' || j == 0)
									{
										while (*w)
											w++;
										m = 0;
										break;
									}
									m = 1;
								}
								else if (*s == *w)
									w++;
								else if (*s != '-')
									break;
							}
							if (!*w)
							{
								nov = 0;
								break;
							}
							if (n = no)
							{
								s = e - 1;
								w = &opt_info.name[prefix] + n;
								while (*++s)
								{
									if (*s == '*' || *s == '\a')
									{
										if (*s == '\a')
											do
											{
												if (!*++s)
												{
													s--;
													break;
												}
											} while (*s != '\a');
										j = *(s + 1);
										if (j == ':' || j == '|' || j == '?' || j == ']' || j == 0)
										{
											while (*w)
												w++;
											m = 0;
											break;
										}
										m = 1;
									}
									else if (*s == *w)
										w++;
									else if (*s != '-')
										break;
								}
								if (!*w)
									break;
							}
							if (*(s = skip(s, ':', '|', '?', 1, 0, 0, version)) != '|')
								break;
							w = &opt_info.name[prefix];
						}
						if (p)
							s = p;
						if (!*w)
						{
							if (n)
								num = 0;
							if (!(n = num && (m || *s == ':' || *s == '|' || *s == '?' || *s == ']' || *s == 0)) && x)
							{
								psp = pop(psp);
								return opterror("?", version, catalog);
							}
							for (x = k; *(f + 1) == '|' && (j = *(f + 2)) && j != '!' && j != '=' && j != ':' && j != '?' && j != ']'; f += 2);
							if (*f == ':')
							{
								x = -1;
								opt_info.option[1] = '-';
								opt_info.option[2] = 0;
							}
							else if (*(f + 1) == ':' || *(f + 1) == '!' && *(f + 2) == ':')
							{
								opt_info.option[1] = x;
								opt_info.option[2] = 0;
							}
							else
							{
								a = f;
								if (*a == '=')
									a++;
								else
								{
									if (*(a + 1) == '!')
										a++;
									if (*(a + 1) == '=')
										a += 2;
								}
								x = -strtol(a, &b, 0);
								if ((b - a) > sizeof(opt_info.option) - 2)
									b = a + sizeof(opt_info.option) - 2;
								memcpy(&opt_info.option[1], a, b - a);
								opt_info.option[b - a + 1] = 0;
							}
							b = e;
							if (t)
							{
								s = t;
								t = 0;
							}
							a = s = skip(s, 0, 0, 0, 1, 0, 0, version);
							if (n)
							{
								w = &opt_info.name[prefix];
								break;
							}
						}
						w = &opt_info.name[prefix];
					}
					else if (k == c && prefix == 1)
					{
						w = 0;
						opt_info.name[1] = c;
						opt_info.name[2] = 0;
						opt_info.offset = 2;
						opt_info.index--;
						break;
					}
					if (t)
					{
						s = t;
						if (a)
							a = t;
					}
				}
				s = skip(s, 0, 0, 0, 1, 0, 1, version);
				if (*s == GO)
					s = skip(s + 1, 0, 0, 0, 0, 1, 1, version);
				if (cache)
				{
					m = OPT_cache_flag;
					v = s;
					if (*v == '#')
					{
						v++;
						m |= OPT_cache_numeric;
					}
					else if (*v == ':')
					{
						v++;
						m |= OPT_cache_string;
					}
					if (*v == '?')
					{
						v++;
						m |= OPT_cache_optional;
					}
					else if (*v == *(v - 1))
						v++;
					if (*(v = next(v, version)) == '[')
						v = skip(v + 1, 0, 0, 0, 1, 0, 1, version);
					if (*v != GO)
					{
						v = f;
						for (;;)
						{
							if (isdigit(*f) && isdigit(*(f + 1)))
								while (isdigit(*(f + 1)))
									f++;
							else if (*(f + 1) == '=')
								break;
							else
								cache->flags[map[*f]] = m;
							while (*(f + 1) == '|')
							{
								f += 2;
								if (!(j = *f) || j == '!' || j == '=' || j == ':' || j == '?' || j == ']')
									break;
								cache->flags[map[j]] = m;
							}
							if (j != '!' || (m & OPT_cache_invert))
								break;
							f = v;
							m |= OPT_cache_invert;
						}
					}
				}
				else
				{
					m = 0;
					if (!w)
					{
						if (isdigit(*f) && isdigit(*(f + 1)))
							k = -1;
						if (c == k)
							m = 1;
						while (*(f + 1) == '|')
						{
							f += 2;
							if (!(j = *f))
							{
								m = 0;
								break;
							}
							else if (j == c)
								m = 1;
							else if (j == '!' || j == '=' || j == ':' || j == '?' || j == ']')
								break;
						}
					}
					if (m)
					{
						s--;
						if (*++f == '!')
						{
							f++;
							num = 0;
						}
						if (*f == '=')
						{
							c = -strtol(++f, &b, 0);
							if ((b - f) > sizeof(opt_info.option) - 2)
								b = f + sizeof(opt_info.option) - 2;
							memcpy(&opt_info.option[1], f, b - f);
							opt_info.option[b - f + 1] = 0;
						}
						else
							c = k;
						break;
					}
				}
				if (*s == '#')
				{
					if (!numopt && s > opts)
					{
						numopt = s - 1;
						numchr = k;
						if (*f == ':')
							numchr = -1;
						else if (*(f + 1) != ':' && *(f + 1) != '!' && *(f + 1) != ']')
						{
							a = f;
							if (*a == '=')
								a++;
							else
							{
								if (*(a + 1) == '!')
									a++;
								if (*(a + 1) == '=')
									a += 2;
							}
							numchr = -strtol(a, NiL, 0);
						}
					}
				}
				else if (*s != ':')
					continue;
			}
			else if (*s == ']')
			{
				s++;
				continue;
			}
			else if (*s == '#')
			{
				if (!numopt && s > opts)
					numchr = *(numopt = s - 1);
			}
			else if (*s != ':')
			{
				if (cache)
				{
					m = OPT_cache_flag;
					if (*(s + 1) == '#')
					{
						m |= OPT_cache_numeric;
						if (*(s + 2) == '?')
							m |= OPT_cache_optional;
					}
					else if (*(s + 1) == ':')
					{
						m |= OPT_cache_string;
						if (*(s + 2) == '?')
							m |= OPT_cache_optional;
					}
					cache->flags[map[*s]] = m;
				}
				s++;
				continue;
			}
			message((-21, "optget: opt %s", show(s)));
			if (*++s == '?' || *s == *(s - 1))
				s++;
			if (*(s = next(s, version)) == '[')
			{
				s = skip(s + 1, 0, 0, 0, 1, 0, 1, version);
				if (*s == GO)
					s = skip(s + 1, 0, 0, 0, 0, 1, 1, version);
			}
		}
		if (w && x)
		{
			s = skip(b, '|', '?', 0, 1, 0, 0, version);
			if (v && (*a == 0 || *(a + 1) != ':' && *(a + 1) != '#') && (*v == '0' || *v == '1') && !*(v + 1))
			{
				if (*v == '0')
					num = !num;
				v = 0;
			}
			if ((s - b) >= elementsof(opt_info.name))
				s = b + elementsof(opt_info.name) - 1;
			for (;;)
			{
				if (b >= s)
				{
					*w = 0;
					break;
				}
				if (*b == '*')
					break;
				*w++ = *b++;
			}
			if (!num && v)
				return opterror(no ? "!" : "=", version, catalog);
			w = &opt_info.name[prefix];
			c = x;
			s = a;
		}
	}
	if (!*s)
	{
		if (w)
		{
			if (hp = (Help_t*)search(styles, elementsof(styles), sizeof(styles[0]), w))
			{
				if (!v)
					v = (char*)hp->name;
				goto help;
			}
			if (!v)
			{
				v = opt_info.name;
				goto help;
			}
		}
		if (w || c < '0' || c > '9' || !numopt)
		{
			pop(psp);
			return opterror("", version, catalog);
		}
		s = numopt;
		c = opt_info.option[1] = numchr;
		opt_info.offset--;
	}
	opt_info.arg = 0;

	/*
	 * this is a ksh getopts workaround
	 */

	if (opt_info.num != LONG_MIN)
		opt_info.num = opt_info.number = num;
	if (*++s == ':' || *s == '#')
	{
		if (w)
		{
			if (nov)
			{
				if (v)
				{
					pop(psp);
					return opterror("!", version, catalog);
				}
				opt_info.num = opt_info.number = 0;
			}
			else
			{
				if (!v && *(s + 1) != '?' && (v = argv[opt_info.index]))
					opt_info.index++;
				if (!(opt_info.arg = v))
				{
					if (*(s + 1) != '?')
					{
						pop(psp);
						return opterror(s, version, catalog);
					}
				}
				else if (*s == '#')
				{
					opt_info.num = (long)(opt_info.number = strtonll(opt_info.arg, &e, NiL, 0));
					if (e == opt_info.arg)
					{
						pop(psp);
						return opterror(s, version, catalog);
					}
				}
			}
			goto optarg;
		}
		else if (*(opt_info.arg = &argv[opt_info.index++][opt_info.offset]))
		{
			if (*s == '#')
			{
				opt_info.num = (long)(opt_info.number = strtonll(opt_info.arg, &e, NiL, 0));
				if (e == opt_info.arg)
				{
					if (*(s + 1) == '?')
					{
						opt_info.arg = 0;
						opt_info.index--;
					}
					else
						c = opterror(s, version, catalog);
					pop(psp);
					return c;
				}
				else if (*e)
				{
					opt_info.offset += e - opt_info.arg;
					opt_info.index--;
					pop(psp);
					return c;
				}
			}
		}
		else if (opt_info.arg = argv[opt_info.index])
		{
			opt_info.index++;
			if (*(s + 1) == '?' && (*opt_info.arg == '-' || (pass->flags & OPT_plus) && *opt_info.arg == '+') && *(opt_info.arg + 1))
			{
				opt_info.index--;
				opt_info.arg = 0;
			}
			else if (*s == '#')
			{
				opt_info.num = (long)(opt_info.number = strtonll(opt_info.arg, &e, NiL, 0));
				if (*e)
				{
					if (*(s + 1) == '?')
					{
						opt_info.arg = 0;
						opt_info.index--;
					}
					else
					{
						pop(psp);
						return opterror(s, version, catalog);
					}
				}
			}
		}
		else if (*(s + 1) != '?')
		{
			opt_info.index--;
			pop(psp);
			return opterror(s, version, catalog);
		}
		opt_info.offset = 0;
	optarg:
		if (*s == ':' && *(s = skip(s, 0, 0, 0, 1, 0, 1, version)) == GO && *(s = next(s + 1, version)) == '[' && isalnum(*(s + 1)))
		{
			x = 0;
			if (opt_info.arg)
			{
				do
				{
					w = opt_info.arg;
					f = s = next(s + 1, version);
					k = *f;
					if (k == *w && isalpha(k) && !*(w + 1))
					{
						x = k;
						break;
					}
					if (*s == '+' || *s == '-')
						continue;
					else if (*s == '[' || version < 1)
						continue;
					else
					{
						if (*s != ':')
							s = skip(s, ':', '?', 0, 1, 0, 0, version);
						if (*s == ':')
						{
							if (catalog)
							{
								p = skip(s + 1, '?', 0, 0, 1, 0, 0, version);
								e = sfprints("%-.*s", p - (s + 1), s + 1);
								b = T(error_info.id, catalog, e);
								if (b == e)
									p = 0;
								else
								{
									sfprintf(xp, ":%s|%s?", b, e);
									s = sfstruse(xp);
								}
							}
							else
								p = 0;
							for (;;)
							{
								n = m = 0;
								e = s + 1;
								while (*++s)
								{
									if (*s == '*' || *s == '\a')
									{
										if (*s == '\a')
											do
											{
												if (!*++s)
												{
													s--;
													break;
												}
											} while (*s != '\a');
										j = *(s + 1);
										if (j == ':' || j == '|' || j == '?' || j == ']' || j == 0)
										{
											while (*w)
												w++;
											m = 0;
											break;
										}
										m = 1;
									}
									else if (*s == *w)
										w++;
									else if (*s != '-')
										break;
								}
								if (!*w)
								{
									nov = 0;
									break;
								}
								if (*(s = skip(s, ':', '|', '?', 1, 0, 0, version)) != '|')
									break;
								w = opt_info.arg;
							}
							if (p)
								s = p;
							if (!*w)
							{
								if (n)
									num = 0;
								if (!(n = (m || *s == ':' || *s == '|' || *s == '?' || *s == ']')) && x)
								{
									pop(psp);
									return opterror("?", version, catalog);
								}
								for (x = k; *(f + 1) == '|' && (j = *(f + 2)) && j != '!' && j != '=' && j != ':' && j != '?' && j != ']'; f += 2);
								if (*f == ':')
									x = -1;
								else if (*(f + 1) == ':' || *(f + 1) == '!' && *(f + 2) == ':')
									/* ok */;
								else
								{
									a = f;
									if (*a == '=')
										a++;
									else
									{
										if (*(a + 1) == '!')
											a++;
										if (*(a + 1) == '=')
											a += 2;
									}
									x = -strtol(a, &b, 0);
								}
								b = e;
								a = s = skip(s, 0, 0, 0, 1, 0, 0, version);
								if (n)
									break;
							}
						}
					}
				} while (*(s = skip(s, 0, 0, 0, 1, 0, 1, version)) == '[');
				if (!(opt_info.num = opt_info.number = x))
				{
					pop(psp);
					return opterror("*", version, catalog);
				}
			}
		}
	}
	else if (w && v)
	{
		pop(psp);
		return opterror("=", version, catalog);
	}
	else
	{
		opt_info.num = opt_info.number = num;
		if (!w && !argv[opt_info.index][opt_info.offset])
		{
			opt_info.offset = 0;
			opt_info.index++;
		}
	}
	pop(psp);
	return c;
 help:
	if (v && *v == '?' && *(v + 1) == '?' && *(v + 2))
	{
		s = v + 2;
		if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'o' || s[1] == 'O'))
		{
			s += 2;
			n = -1;
		}
		else
			n = 1;
		if (hp = (Help_t*)search(styles, elementsof(styles), sizeof(styles[0]), s))
		{
			if (hp->style < STYLE_man || !(s = argv[opt_info.index]) || s[0] != '-' || s[1] != '-' || !s[2])
			{
				opt_info.arg = sfprints("\fversion=%d", version);
				pop(psp);
				return '?';
			}
			opt_info.state->force = hp->style;
		}
		else if (match(s, "ESC", -1, NiL) || match(s, "EMPHASIS", -1, NiL))
			opt_info.state->emphasis = n;
		else if (match(s, "PREFORMAT", -1, NiL))
			opt_info.state->flags |= OPT_preformat;
		else if (match(s, "TEST", -1, NiL))
		{
			opt_info.state->width = OPT_WIDTH;
			opt_info.state->emphasis = 1;
		}
		else
		{
			pop(psp);
			return opterror(v, version, catalog);
		}
		psp = pop(psp);
		if (argv == opt_info.state->strv)
			return '#';
		goto again;
	}
	if ((opt_info.arg = opthelp(NiL, v)) == (char*)unknown)
	{
		pop(psp);
		return opterror(v, version, catalog);
	}
	pop(psp);
	return '?';
}

/*
 * parse long options sans leading -- from string and pass to optget()
 * syntax is the unquoted
 *
 *	<length> <name>[[-+:|&=]=<value>\n (or \0 for the last)
 *
 * or the quoted
 *
 *	[no]name[[-+:|&=]=['"{(]value[)}"']][, ]...
 *
 *	with \x escapes passed to chresc()
 *
 * return '#' for `label:', with opt_info.name==label
 * str[opt_info.offset]	next arg
 *
 *	optstr(s, 0)
 *		return '-' if arg, 0 otherwise
 *	optstr(0, opts)
 *		use previous parsed str
 */

int
optstr(const char* str, const char* opts)
{
	register char*		s = (char*)str;
	register Sfio_t*	mp;
	register int		c;
	register int		ql;
	register int		qr;
	register int		qc;
	int			v;
	char*			e;

 again:
	if (s)
	{
		if (!(mp = opt_info.state->strp) && !(mp = opt_info.state->strp = sfstropen()))
			return 0;
		if (opt_info.state->str != s)
			opt_info.state->str = s;
		else if (opt_info.index == 1)
			s += opt_info.offset;
		while (*s == ',' || *s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
			s++;
		if (!*s)
			return 0;
		sfputc(mp, '-');
		sfputc(mp, '-');
		if (isdigit(*s) && (v = (int)strtol(s, &e, 10)) > 1 && isspace(*e) && --v <= strlen(s) && (s[v] == 0 || s[v] == '\n'))
		{
			s += v;
			while (isspace(*++e));
			sfwrite(mp, e, s - e);
		}
		else
		{
			while (*s && *s != ',' && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && *s != '=' && *s != ':')
				sfputc(mp, *s++);
			if ((c = *s) == ':' && *(s + 1) != '=')
			{
				opt_info.index = 1;
				opt_info.offset = ++s - (char*)str;
				s = sfstruse(mp) + 2;
				e = opt_info.name;
				while (e < &opt_info.name[sizeof(opt_info.name)-1] && (*e++ = *s++));
				opt_info.arg = 0;
				opt_info.num = opt_info.number = 0;
				opt_info.option[0] = ':';
				opt_info.option[1] = 0;
				return '#';
			}
			if (c == ':' || c == '=')
			{
				sfputc(mp, c);
				ql = qr = 0;
				while (c = *++s)
				{
					if (c == '\\')
					{
						sfputc(mp, chresc(s, &e));
						s = e - 1;
					}
					else if (c == qr)
					{
						if (qr != ql)
							sfputc(mp, c);
						if (--qc <= 0)
							qr = ql = 0;
					}
					else if (c == ql)
					{
						sfputc(mp, c);
						qc++;
					}
					else if (qr)
						sfputc(mp, c);
					else if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
						break;
					else if (c == '"' || c == '\'')
					{
						ql = qr = c;
						qc = 1;
					}
					else
					{
						sfputc(mp, c);
						if (c == GO)
						{
							ql = c;
							qr = OG;
							qc = 1;
						}
						else if (c == '(')
						{
							ql = c;
							qr = ')';
							qc = 1;
						}
					}
				}
			}
		}
		opt_info.argv = opt_info.state->strv;
		opt_info.state->strv[0] = T(NiL, ast.id, "option");
		opt_info.state->strv[1] = sfstruse(mp);
		opt_info.state->strv[2] = 0;
		opt_info.offset = s - (char*)str;
	}
	if (opts)
	{
		if (!opt_info.state->strv[1])
			return 0;
		opt_info.index = 1;
		v = opt_info.offset;
		opt_info.offset = 0;
		c = optget(opt_info.state->strv, opts);
		opt_info.index = 1;
		opt_info.offset = v;
		if (c == '#')
		{
			s = opt_info.state->str;
			goto again;
		}
		if ((c == '?' || c == ':') && (opt_info.arg[0] == '-' && opt_info.arg[1] == '-'))
			opt_info.arg += 2;
		s = opt_info.name;
		if (*s++ == '-' && *s++ == '-')
		{
			e = opt_info.name;
			while (*e++ = *s++);
		}
	}
	else
		c = '-';
	return c;
}