/* envtest2.c -- replay dtwm's startup and watch environ after every step.
 * dtwm's core had its environ entries pointing 2 (or 4) bytes into their
 * strings, so getenv("PATH") failed and the first action click crashed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <Dt/Dt.h>
#include <Dt/EnvControlP.h>

extern char **environ;

static void show(const char *when)
{
	int i, bad = 0;
	for (i = 0; environ[i]; i++)
		if (!strchr(environ[i], '=') || environ[i][0] == '=')
			bad++;
	printf("-- %-22s environ=%p PATH=%.20s [0]=%.24s [12]=%.16s bad=%d\n", when,
	    (void *)environ, getenv("PATH") ? getenv("PATH") : "(null)",
	    environ[0], i > 12 ? environ[12] : "-", bad);
}

int main(int argc, char *argv[], char *envp[])
{
	XtAppContext app;
	Widget top;

	show("start");
	setlocale(LC_ALL, "");
	show("setlocale");
	_DtEnvControl(DT_ENV_SET);
	show("_DtEnvControl");
	XtSetLanguageProc(NULL, (XtLanguageProc)NULL, NULL);
	show("XtSetLanguageProc");
	top = XtAppInitialize(&app, "Dtwm", NULL, 0, &argc, argv, NULL, NULL, 0);
	show("XtAppInitialize");
	DtAppInitialize(app, XtDisplay(top), top, argv[0], "Dtwm");
	show("DtAppInitialize");
	return 0;
}
