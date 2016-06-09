#include <stdio.h>
#include <errno.h>

int             usage(void) {
  fprintf(stdout,
          " Usage: [options]... <mode> <lustre_mount_point>\n"
          "The Lustre Ring copy tool can be used as a daemon or "
          "as a command line tool\n"
          "The Lustre HSM daemon acts on action requests from Lustre\n"
          "to copy files to and from  a Ring using Droplet - sproxyd.\n"
          "   --daemon            Daemon mode, run in background\n"
          " Options:\n"
          "   --no-xattr          Don't copy file extended attributes\n"
          "The Lustre Ring tool performs administrator-type actions\n"
          "on a Lustre Ring archive.\n"
          "This tool can link an existing Ring namespace\n"
          "into a Lustre filesystem.\n"
          " Usage:\n"
          "   -A, --archive <#>          Archive number (repeatable)\n"
          "   --dry-run                  Don't run, just show what"
          " would be done\n"
          "   -p, --droplet-path <path>  Droplet configuration file's path\n"
          "   -n, --droplet-name <name>  Droplet configuration file's name\n");
  return (-EINVAL);
}
