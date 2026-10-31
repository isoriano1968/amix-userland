/* envtest.c -- does environ survive putenv() and DtSvc's _DtEnvControl()?
 * dtwm's core had every original environ entry pointing 2 bytes into its
 * string ("TH=/usr..." for PATH), so getenv("PATH") failed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Intrinsic.h>
#include <Dt/EnvControlP.h>

extern char **environ;

static void show(const char *when)
{
	int i;
	printf("-- %s: environ=%p PATH=%s\n", when, (void *)environ,
	    getenv("PATH") ? getenv("PATH") : "(null)");
	for (i = 0; environ[i] && i < 4; i++)
		printf("   [%d] %p %.30s\n", i, (void *)environ[i], environ[i]);
}

int main(int argc, char **argv, char **envp)
{
	printf("envp=%p environ=%p\n", (void *)envp, (void *)environ);
	show("start");
	putenv("ENVTEST=1");
	show("after putenv");
	_DtEnvControl(DT_ENV_SET);
	show("after _DtEnvControl");
	return 0;
}
