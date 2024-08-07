#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "init.h"

/* return 0, if str1 and str2 are the same up to n bytes,
 * ignore '-' to '_' differences
 *
 * if n is < 0 no size limits */

int match_n_module(const char *s1, char *s2, int n)
{
	if (!s1 || !s2 || n == 0)
		return(1);

        while ( (*s1 == *s2) ||
                (*s1 == '\0' && *s2 == '\n' && *++s2 == '\0') ||
                (*s2 == '\0' && *s1 == '\n' && *++s1 == '\0') ||
                ((*s1 == '-') && (*s2 == '_')) ||
                ((*s1 == '_') && (*s2 == '-')) ) {

                if (*s1=='\0' || n == 0) return(0); /* match */
		s1++;
                s2++;
		n--;
        }
        return(1); /* no match */
}

/* return 0, if str1 and str2 are the same,
   except '-' and '_' characters */
int match_module(const char *s1, char *s2)
{
	return match_n_module(s1, s2, -1);
}

/* return 0 if str1 contains str2,
 * ignore '-' to '_' differences */

int search_match_module(const char *s1, char *s2)
{
	int len_s2, len_s1;

	if (!s1 || !s2)
		return(1);

	len_s1 = strlen(s1);
	len_s2 = strlen(s2);

	if (len_s1 < len_s2)
		return(1);

	while(*s1 != '\0' && len_s1 >= len_s2) {
	      if (match_n_module(s1, s2, len_s2) == 0)
		      return 0;
	      s1++;
	      len_s1--;
	}

	return(1);
}

/* return 0, if str1 and str2 are the same */
/* ignore cases and '\n' at the end of s1 or s2 string */
int match_string_nocase(const char *s1, char *s2)
{
	if (!s1 || !s2)
		return(1);

        while (*s1 == *s2 ||
              (*s1 == '\0' && *s2 == '\n' && *++s2 == '\0') ||
              (*s2 == '\0' && *s1 == '\n' && *++s1 == '\0') ||
              (*s1 >= 'a' && *s1 <= 'z' && *s1 - 'a' + 'A' == *s2) ||
              (*s1 >= 'A' && *s1 <= 'Z' && *s1 - 'A' + 'a' == *s2)) {
                if (*s1=='\0')
                        return(0); /* match */
                s1++;
                s2++;
        }
        return(1); /* no match */
}

/* return 0, if str1 and str2 are the same */
/* ignore '\n' at the end of s1 or s2 string */
int match_string(const char *s1, char *s2)
{
	if (!s1 || !s2)
		return(1);

        while (*s1 == *s2 ||
              (*s1 == '\0' && *s2 == '\n' && *++s2 == '\0') ||
              (*s2 == '\0' && *s1 == '\n' && *++s1 == '\0')) {
                if (*s1=='\0')
                        return(0); /* match */
                s1++;
                s2++;
        }
        return(1); /* no match */
}

/**
 * remove_end_newline: removes newline on the end of an attribute value
 * @value: string to remove newline from
 */
void remove_end_newline(char *s1)
{
	char *p = NULL;

	if (!s1)
		return;

	p = s1 + (strlen(s1) - 1);

	if (s1 && *p == '\n')
		*p = '\0';
}
