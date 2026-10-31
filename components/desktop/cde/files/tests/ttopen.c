/* ttopen.c -- does a ToolTalk client get into the running session?
 * TT_SESSION / the X root property name the session; TT_TRACE helps. */
#include <stdio.h>
#include <Tt/tt_c.h>
int main(void)
{
	char *p = tt_open();
	Tt_status s = tt_ptr_error(p);
	printf("tt_open: %d %s\n", (int)s, tt_status_message(s));
	if (s == TT_OK) {
		printf("procid %s session %s\n", p, tt_default_session());
		tt_close();
	}
	return s != TT_OK;
}
