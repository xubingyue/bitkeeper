#include "system.h"
#include "sccs.h" 

extern char *editor, *pager, *bin;

main(int ac, char **av)
{
	int force = 0, c, logsetup;
	char *project_name = NULL, *project_path = NULL, *config_path = NULL;
	char buf[1024], my_editor[1024], setup_files[MAXPATH];
	FILE *f;

	platformInit();

	while ((c = getopt(ac, av, "c:fn:")) != -1) {
		switch (c) {
		    case 'c':	localName2bkName(optarg, optarg);
				config_path = fullname(optarg, 0);
				break;
		    case 'f':	force = 1; break;
		    case 'n':	project_name = optarg; break;
		}
	}
	unless (project_path = av[optind]) {
		printf(
	"Usage: bk setup [-c<config file>] [-n <project name>] directory");
		exit (0);
	}
	if (exists(project_path)) {
		printf("bk: %s exists already, setup fails.", project_path);
		exit (1);
	}

	unless(force) {
		gethelp("setup_1", "", stdout);
		printf("Create new project? [no] ");
		fgets(buf, sizeof(buf), stdin);
		if ((buf[0] != 'y') && (buf[0] != 'Y')) exit (0);
	}
	mkdirp(project_path);
	if (chdir(project_path) != 0) exit(1);
	unless(project_name) {
		gethelp("setup_2", "", stdout);
		while (1) {
			f = fopen("Description", "wb");
			fprintf(f,
				"Replace this with your project description");
			fclose(f);
			f = fopen("D.save", "wb");
			fprintf(f,
				"Replace this with your project description");
			fclose(f);
			printf("Editor to use [%s] ", editor);
			fgets(my_editor, sizeof(my_editor), stdin);
			chop(my_editor);
			if (my_editor[0] != 0) {
				sprintf(buf, "%s Description", my_editor);
			} else {
				sprintf(buf, "%s Description", editor);
			}
			system(buf);
			if (system("cmp -s D.save Description")) {
				break;
			} else {
				printf(
				 "Sorry, you have to put something in there\n");
			}
		}
	} else {
		f = fopen("Description", "wb");
		fputs(project_name, f);
		fclose(f);
	}
	sprintf(buf, "%scset -siDescription .", getenv("BK_BIN"));
	system(buf);

	f = fopen("Description", "rt"); 
	fgets(buf, sizeof(buf), f);
	fclose(f);
	logsetup = strneq(buf, "BitKeeper Test", 14) ? 0 : 1;
	unlink("Description"); unlink("D.save");
	if (chdir("BitKeeper/etc") != 0) {
		perror("cd BitKeeper/etc");
		exit (1);
	}
	if (config_path == NULL) {
		gethelp("setup_3", "", stdout);
		sprintf(buf, "cp %sbitkeeper.config config", bin);
		system(buf);
		chmod("config", 0664);
		while (1) {
			printf("Editor to use [%s] ", editor);
			fgets(my_editor, sizeof(my_editor), stdin);
			chop(my_editor);
			if (my_editor[0] != 0) {
				sprintf(buf, "%s config", my_editor);
			} else {
				sprintf(buf, "%s config", editor);
			}
			system(buf);
			sprintf(buf, "cmp -s %sbitkeeper.config config", bin);
			if (system(buf)) {
				break;
			} else {
				printf(
				 "Sorry, you have to really fill this out.\n");
			}
		}
	} else {
		sprintf(buf, "cp %s config", config_path);
		system(buf);
	}
	sprintf(buf, "%sci -qi config", bin);
	system(buf);
	if (logsetup) {
		sprintf(buf, "%sget -q config", bin);
		system(buf);
		sprintf(buf, "%ssendconfig setups@openlogging.org", bin);
	}
	sprintf(setup_files, "%s/setup_files%d", TMP_PATH, getpid());
	sprintf(buf, "%ssfiles -C > %s", bin, setup_files);
	system(buf);
	sprintf(buf,
		"%scset -q -y\"Initial repository create\" -  < %s", bin, setup_files);
	system(buf);
	unlink(setup_files);
}