#include "../system.h"
#include "../sccs.h"

char	*
sccs_getuser(void)
{
	static	char	*s;

	if (s) return (s);
	unless (s = getenv("BK_USER")) s = getenv("USER");
	unless (s && s[0]) s = getlogin();
#ifndef WIN32 /* win32 have no getpwuid() */
	unless (s && s[0]) {
		struct	passwd	*p = getpwuid(getuid());

		s = p->pw_name;
	}
#endif
	unless (s && s[0]) s = UNKNOWN_USER;
	return (s);
}