# lustre-droplet-copytool
A copytool for the Lustre HSM to tier the data towards Scality's storage system.


*============================*
| Setting up a Lustre - Ring |
*============================*


 
# I. Links and documents

First, we will be Installing Lustre on a device and Installing the Ring on another one.

Once it's done, and once we've activated an agent, we will Install the SOFS Connector on CentOS or RedHat
on the agent's node.

The Robinhood guide can be found here : https://github.com/cea-hpc/robinhood/wiki/tmpfs_admin_guide
 
# II. Instalation of the agent for the Lustre - Ring



    Overview :
                *-----------------------------------------------------------------------------------------------*
                |                       *--------- Client / Policy engine          |                            |
                |                       |               .                          |                            |
                |                       |               .                          |                            |
                | Clients---------------*--MDS / Coordinator                       |                            |
                |                       |                |                         |                            |
                |                       |                |                         |                            |
                |                       *--OSS           |                         |                            |
                |                       |              Agent / Copytool---------------------HSM Space           |
                |                       *--OSS                 |                   |         (Ring)             |
                |                       |                      |                   |                            |
                |                       *----------------------*                   |                            |
                |                             Lustre Space                         |                            |
                *-----------------------------------------------------------------------------------------------*

    Overview of the copytool :

                *-----------------------------------------------------------------------------------------------*
                |                                                                                               |
                | NEW (no flag) ----[Archive]----> ARCHIVED ----[Release]----> RELEASED                         |
                |                                  |      ^ <----[Restore]----                                  |
                |                                  |      |                                                     |
                |                                Write  Archive                                                 |
                |                                  |      |                                                     |
                |                                  v      |                                                     |
                |                                    DIRTY                                                      |
                *-----------------------------------------------------------------------------------------------*

Coordinator :
			The coordinator is a daemon running on the MDT that will coordinate the agents.

Agents :

			So to make it short, the agent is what will make the link between the Lustre FS and the Ring.
			The agent is a client running a daemon copytool.

                        You must have at least 1 agent per different Ring (HSM),
			and can have as many agents per Ring (HSM).

 

Robinhood :
			The Robinhood is a policy engine designed to monitor and purge large filesystems like Lustre.
		   	It runs on a Lustre client and acts on all the agents via their mount points.


Note :	   	The coordinator and the copytool are pre-installed on Lustre from version 2.5 +.
     		   	So there is no RPMs to download and install.


First of all be sure to have 1 Lustre FS with at least 2 clients, and at least 1 functionnal Ring.
One of the Lustre client will become the agent and will need to have both Lustre client mounted and a working
sfused connector linked to your Ring.
To do so, follow the installation guide Install the SOFS Connector on CentOS or RedHat about the sfused connector.


    Run this command on the Lustre MDT node :
    # lctl set_param mdt.$FSNAME-MDT0000.hsm_control=enabled
     
    In my case the MDT name was DATA so it gave something like that:
     
    # lctl set_param mdt.DATA-MDT0000.hsm_control=enabled
     
    Then you want to check if it worked:
     
    # lctl get_param mdt.$FSNAME-MDT0000.hsm_control

    Now the coordinator is enabled on the MDT, time to activate the agents.

    To activate an agent, you first need to have a fully functionnal client with both Lustre mounted on it,
    and an Sfused connector (step 1). On this client, juste launch this command :
    # lhsmtool_posix --daemon --droplet-path=$DROPLET-PATH --droplet-name=$DROPLET-NAME
      		     	      				   --archive=$NB_ARCHIVE $LUSTRE_MOUNT_POINT

    Info : The agent will now loop on your terminal, allowing you to monitor every action made.



You have a functionnal agent linking the Lustre FS to your Ring !


# III. Installation and usage of the Robinhood policy engine

The robinhood policy engine is meant to be installed on a Lustre client, unlike the agent.



Note : 		  If you have multiple agents running multiple HSM, be sure that the client running
     		  Robinhood has every mount point to every HSM.

  make sure flex, bison, libtool, mysql-devel are installed:
    E.g.
            yum install flex
	    yum install bison
	    yum install libtool
	    yum install mysql-devel

retrieve robinhood sources

    # git clone git://robinhood.git.sourceforge.net/gitroot/robinhood/robinhood
    # cd robinhood
    # git checkout  master (or other branch)
    # sh autogen.sh

Run the 'configure' script with the following options:
    # For Lustre-HSM: --with-purpose=LUSTRE_HSM

E.g.:
    # ./configure --with-purpose=LUSTRE_HSM

Then, build the RPM:
    # make rpm

A ready-to-install RPM is generated in the 'rpms/RPMS/<arch>' directory.

For installing robinhood on your target system, install the following RPM:
    # yum install robinhood-adm
            configuration helper for DB, changelogs, ...
    # yum install robinhood-lhsm
            include robinhood commands, config templates, init.d script


Robinhood needs a MySQL database for storing information about files.
This database can run on a different node from Robinhood daemon.

    * Install MySQL server on the machine (mysql-server and mysql packages).
    * Start the DB engine:
            service mysqld start
    * Run the configuration helper script as root on the database host:

  1) check database requirement:
     # rbh-config precheck_db
  2) create robinhood's database:
     # rbh-config create_db

* Write the DB password to a file with read access for root only (600)
     e.g. to /etc/robinhood.d/lhsm/.dbpassword

Note: initially, the database schema is empty. Robinhood will create it the first time it is launched.

For Lustre-HSM purpose, you must register robinhood as a MDT changelog consummer.

* Run the configuration helper script on Lustre MDT:
    # rbh-config enable_chglogs
This registers a changelog consummer and activate required changelog records.

    Note: by default, the script checks for a 'cl1' consummer.
    If you need to register several changelog consummers on your file system,
    refer to lustre documentation.

You can use the '--template' option to generate a documented configuration file template:
    # rbh-lhsm --template=<template_file>

Note: by default, "service robinhood-hsm start" looks for configuration files in "/etc/robinhood.d/hsm".

For more details, refer to Robinhood Admin Guide (in the doc/admin_guide directory).

Even if your filesystem is empty, you need to perform an initial scan in order to initialize robinhood database.
This prevents from having entries in filesystem that it wouldn't know about.
    # rbh-lhsm --scan --once


------------------------
Retrieving scan's stats:
------------------------

Now we performed a scan, we can get stats about users, files, directories, etc. using rbh-lhsm-report:

Get stats for a user: -u option

    #rbh-lhsm-report -u foo

    user , type,  count,  spc_used,  avg_size
    foo  ,  dir,  75450, 306.10 MB,   4.15 KB
    foo  , file, 116396,  11.14 TB, 100.34 MB

    Total: 191846 entries, 12248033808384 bytes used (11.14 TB)

Split user's usage per group: -S option

    #rbh-lhsm-report -u bar -S

    user , group,  type,  count,  spc_used,   avg_size
    bar  , proj1,  file,      4,  40.00 MB,   10.00 MB
    bar  , proj2,  file,   3296, 947.80 MB,  273.30 KB
    bar  , proj3,  file, 259781, 781.21 GB,    3.08 MB

Get largest files: --top-size option

    #rbh-lhsm-report --top-size

    rank, path           ,      size,  user, group,         last_access,            last_mod, purge class
    1, /tmp/file.big1 , 512.00 GB,  foo1,   p01, 2012/10/14 17:41:38, 2011/05/25 14:22:41, BigFiles
    2, /tmp/file2.tar , 380.53 GB,  foo2,   p01, 2012/10/14 21:38:07, 2012/02/01 14:30:48, BigFiles
    3, /tmp/big.1     , 379.92 GB,  foo1,   p02, 2012/10/14 20:24:20, 2012/05/17 17:40:57, BigFiles
    ...

Get top space consumers: --top-users option

    #rbh-lhsm-report --top-users

    rank, user    , spc_used,  count, avg_size
    1, usr0021 , 11.14 TB, 116396, 100.34 MB
    2, usr3562 ,  5.54 TB,    575,   9.86 GB
    3, usr2189 ,  5.52 TB,   9888, 585.50 MB
    4, usr2672 ,  3.21 TB, 238016,  14.49 MB
    5, usr7267 ,  2.09 TB,   8230, 266.17 MB
    ...

Notes:

    --by-count option sorts users by entry count
    --by-avgsize option sorts users by average file size
    --reverse option reverses sort order (e.g. smallest first)
    Use --count-min N option to only display users with at least N entries.
    --by-size-ratio option makes it possible to sort users using the percentage of files in the given range.
    Filesystem content summary: -i option

    #rbh-lhsm-report -i

    status,    type ,    count,   volume, avg_size
    n/a   ,     dir ,  1780074,  8.02 GB,  4.72 KB
    n/a   , symlink ,   496142, 24.92 MB,       53
    new   ,    file , 21366275, 91.15 TB,  4.47 MB

    Total: 23475376 entries, 100399805708329 bytes (91.31 TB)

This report indicates the count and volume of each object type, and their status.
As we have not archived data for now, all objects are marked as 'new'. This field does not make sense for directory objects (n/a), as they do not contain data.

Entry information: -e option

    #rbh-lhsm-report -e /mnt/lustre/dir1/file.1

    id          :     [0x200000400:0x16a94:0x0]
    parent_id   :     [0x200000007:0x1:0x0]
    name        :     file.1
    path updt   :     2013/10/30 10:25:30
    path 	        : 	       /mnt/lustre/dir1/file.1
    depth       :     0
    user        :     root
    group       :     root
    size        :     1.42 MB
    spc_used    :     1.42 MB
    creation    :     2013/10/30 10:07:17
    last_access :     2013/10/30 10:15:28
    last_mod    :     2013/10/30 10:10:52
    last_archive:     2013/10/30 10:13:34
    type        :     file
    mode        :     rw-r--r--
    nlink       :     1
    status      :     modified
    md updt     :     2013/10/30 10:25:30
    stripe_cnt, stripe_size, pool:  2, 1.00 MB,
    stripes     :     ost#1: 30515, ost#0: 30520

    fileclasses summary: --class-info option

Once you have defined fileclasses (see next sections of this tutorial), you can get file repartition by fileclass:

    #rbh-report --class-info

    archive class  ,    count, spc_used,   volume, min_size,  max_size,  avg_size
    BigFiles       ,     1103, 19.66 TB, 20.76 TB,  8.00 GB, 512.00 GB,  19.28 GB
    EmptyFiles     ,  1048697,  7.92 GB,  4.15 GB,        0,   1.96 GB,   4.15 KB
    SmallFiles     , 20218577,  9.63 TB,  9.67 TB,        0,  95.71 MB, 513.79 KB
    ImportantFiles ,   426427, 60.75 TB, 60.86 TB, 16.00 MB,   7.84 GB, 149.66 MB

...and more: you can also generate reports, or dump files per directory, per OST, etc...
To get more details about available reports, run 'rbh-lhsm-report --help'.

---------------------------------
Setting up Robinhood to archive :
---------------------------------

Now we know how to setup and query robinhood, let's archive data to the HSM backend.

Robinhood archives data incrementally. In other words, it only copies new or modified files but do not copy unchanged files multiple times.
Admin can set the priority criteria that determines the copy order: it can be last modification time, last archive time, creation time, last access time, or none...
By default, robinhood doesn't order entries for policy application, to speed-up migration scheduling.
Using a single default policy

Robinhood makes it possible to define different migration policies for several file classes.
In this example, we will only define a single policy for all files.
This is done in the 'migration_policies' section of the config file:

    migration_policies {
       policy default {
           condition {last_mod > 1h}
       }
    }

'default' policy is a special policy that applies to files that don't match a file class.
In a policy, you must specify a condition for allowing entries to be migrated. In this example, we don't want to copy recently modified entries (modified within the last hour).

    Run rbh-lhsm --migrate --once to apply this policy once.

You can also run it as a daemon (without the '--once' option). It this case, it will periodically run the migration on eligible entries.

Robinhood makes it possible to apply different migration policies to files, depending on their properties (path, posix attributes, ...).
This can be done by defining file classes that will be addressed in policies.

In this section of the tutorial, we will define 3 classes and apply different policies to them:

We don't want *.log files of user root to be archived.
We want to quickly archive files from directory '/mnt/lustre/saveme' (1hour after their creation, then archive hourly as long as they are modified).
Archive other entries 6h after their last modification.

First, we need to define those file classes, in the 'filesets' section of the configuration file.We associate a custom name to each FileClass, and specify the definition of the class:

    Filesets {
        # log files owned by root
        FileClass root_log_files {
             definition {
                 owner == root
                 and
                 name == "*.log"
             }
        }
        # files in filesystem tree /mnt/lustre/saveme
        FileClass saveme {
            definition { tree == "/mnt/lustre/saveme" }
        }
    }

Then, those classes can be used in policies:


entries can be ignored for the policy, using a ignore_fileclass statement;
they can be targeted in a policy, using a target_fileclass directive.

    migration_policies {
        # don't archive log files of 'root'
        ignore_fileclass = root_log_file;

        # quickly archive files in saveme
        policy copy_saveme {
            target_fileclass = saveme;
            # last_archive == 0 means "never archived"
            condition {(last_archive == 0 and creation > 1h)
                       or last_archive > 1h}
        }
        # The default policy applies to all other files
        policy default {
            condition {last_mod > 6h}
        }
    }

Notes:

A given FileClass cannot be targeted simultaneously in several migration policies;
policies are matched in the order they appear in the configuration file. In particular, if 2 policy targets overlap, the first matching policy will be used;
You can directly ignore entries by specifying a condition in the 'migration_policies' section (without fileclass definition), using a 'ignore' block:

    migration_policies {
        ignore { owner == root and name == "*.log" }
    ...

A FileClass can be defined as the union or the intersection of other FileClasses. To do so, use the special keywords union, inter and not in the fileclass definition:

    FileClass root_log_A {
        definition {
             (root_log_files inter A_files)
             union (not B_files)
        }
    }

-----------------------------
Specifying a target archive :
-----------------------------

Lustre/HSM can manage multiple archive backends. Archive backends are identified by a unique and persistent index (archive_id).
By default, robinhood performs 'hsm_archive' operations without specifying an archive_id, so Lustre uses the default 'archive_id' from Lustre MDT configuration.

Robihood allows specifying a target archive per fileclass. This can be done in fileclass definitions, by specifying an 'archive_id' parameter:

    fileclass foo {
            definition { ... }
            archive_id = 2;
    }

----------------------
Migration parameters :
----------------------

Robinhood provides a fine control of migration streams: number of simultaneous copies, runtime interval, max volume or file count to be copied per run, priority criteria...

Those parameters are set in the 'migration_parameters' section. See the main parameters below:

     Migration_Parameters {
        # simultaneous copies
        nb_threads_migration = 4 ;
        # sort order for applying migration policy
        # can be one of: none, last_mod, last_access, creation, last_archive
        lru_sort_attr = last_mod ;
        # interval for running migrations
        runtime_interval = 15min ;

        # maximum number of migration requests per pass (0: unlimited)
        max_migration_count = 50000 ;
        # maximum volume of migration requests per pass (0: unlimited)
        max_migration_volume = 10TB ;

        # stop current migration if 50% of copies fail
        #(after at least 100 errors)
        suspend_error_pct = 50% ;
        suspend_error_min = 100 ;
    }


-----------------------------
Releasing Lustre disk space :
-----------------------------

Once data is archived to the HSM backend, it can be released from the Lustre level if disk space is missing to write new data or to restore older data from the backend.

Robinhood lhsm schedules this kind of operation as 'Purge' policies.

Purge are scheduled by 'triggers', based on high and low thresholds parameters: when an OST usage exceeds the high threshold,
Robinhood release data in this OST until the OST usage is back to the low threshold.

Purge order is based on last access time (oldest first).

-------------------------------
Using a single default policy :
-------------------------------

Robinhood makes it possible to define different purge policies for several file classes.
In this example, we will only define a single policy for all files.
This is done in the 'purge_policies' section of the config file:

    purge_policies {
       policy default {
           condition {last_access > 2h}
       }
    }

'default' policy is a special policy that applies to files that don't match a file class.
In a policy, you must specify a condition for allowing entries to be purged. In this example, we don't want to release recently accessed entries (read or written within the last 2 hours).

Note: like for migration policies, you can define multiple purge rules that apply to multiple fileclasses (see above).

We also define a 'purge_trigger' to trigger purge operations when a given OST is full:

    purge_trigger {
        trigger_on         = OST_usage;
        high_threshold_pct = 85%;
        low_threshold_pct  = 80%;
        check_interval     = 5min;
    }

    . trigger_on specifies the type of trigger.
    . high_threshold_pct indicates the OST usage that must be reached for starting purge.
    . low_threshold_pct indicates the OST usage that must be reached for stopping purge.
    . check_interval is the interval for checking disk usage.

Once a purge policy and a trigger have been defined, we can:

  Run a trigger check once and trigger purge operations if necessary:

      #rbh-lhsm --purge --once

    Check triggers continuously and purge data when needed (daemon mode):

      #rbh-lhsm --purge --detach

    Check trigger thresholds without executing 'hsm_release' operations, run: 

      #rbh-lhsm --check-thresholds -L stderr

Note: the list of executed 'hsm_release' action is logged in the report file.

Other trigger parameters: To receive a mail notification each time a high threshold is reached, add this parameter to a trigger:
    alert_high = yes ;

By default, robinhood raises an alert if it can't purge enough data to reach the low threshold.
You can disable those alerts by adding this in a trigger definition:
    alert_low = no ; 


-------------------
Running Robinhood :
-------------------

    #service robinhood-lhsm start
or run:
    #rbh-lhsm --detach

This starts reading changelogs + applying policies (archive, release)


# IV. Agent's commands



The commands on the agents are launched using lfs from any client connected to the Lustre FS :

    #lfs hsm_archive [--archive=$ID] $FILE1 $FILE2 ...
     
Will archive specified files to the Ring or clean files flagged as --dirty to be archived again using dlp_put_id and the UKS key.
--dirty flag is prompted to modified data that was allready archived once.


    #lfs hsm_release $FILE1 $FILE2 ...

Will release the specified files (only if they're flagged --exists and --archived).

    #lfs hsm_restore $FILE1 $FILE2 ...

Will restore to the Lustre FS the archived data of the specified files using dpl_get_id and the UKS key.
Files need to be --exist and --archived flagged in order to be restored.

    #lfs hsm_remove $FILE1 $FILE2 ...

Will remove the archived data from the Ring using dpl_delete_id and the UKS key. Data must not be --released flagged in order to do so.
