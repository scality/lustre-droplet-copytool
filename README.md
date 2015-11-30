# lustre-droplet-copytool
A copytool for the Lustre HSM to tier the data towards Scality's storage system.


Setting up a Lustre - Ring

 
I. Links and documents

 

    First, we will be Installing Lustre on a device and Installing the Ring on another one.

    Once it's done, and once we've activated an agent, we will Install the SOFS Connector on CentOS or RedHat
    on the agent's node.

 

    The Robinhood guide can be found here : https://github.com/cea-hpc/robinhood/wiki/tmpfs_admin_guide

 
II. Instalation of the agent for the Lustre - Ring


    Overview :
			*-----------------------------------------------------------------------------------------------*
			|			*--------- Client / Policy engine          |				|
			|			|	   	.    	   		   |				|
			|			|		.			   |				|
			| Clients---------------*--MDS / Coordinator			   |				|
			| 			|      	 	 |			   |				|
			|			|		 |			   |				|
			|			*--OSS		 |			   |				|
			|			|		Agent / Copytool---------------------HSM Space		|
			|			*--OSS		       |		   |	      (Ring)		|
			|			|		       |		   |				|
			|			*----------------------*		   |				|
			|			    Lustre Space			   |				|
			*-----------------------------------------------------------------------------------------------*

    Overview of the copytool :

    	     	    	*-----------------------------------------------------------------------------------------------*
			|												|
			| NEW (no flag) ----[Archive]----> ARCHIVED ----[Release]----> RELEASED				|
			|     	  			   |      ^ <----[Restore]---- 					|
			|				   |	  | 							|
			|				 Write  Archive							|
			|				   |      |							|
			|				   v	  |							|
			|				     DIRTY							|
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


III. Installation of the Robinhood policy engine


    The robinhood policy engine is meant to be installed on a Lustre client, unlike the agent.



Note : 		  If you have multiple agents running multiple HSM, be sure that the client running
     		  Robinhood has every mount point to every HSM.

    In order to install the Robinhood, first download the tarball here : http://sourceforge.net/projects/robinhood/files/

    Once it's downloaded, untar it using :
    # untar zxvf $tarball
     
    Then head to the created directory :
     
    # cd $directory

    Run the following command :
    # ./configure --with-purpose=$NAME_OF_TEMP_FSMANAGER
     
    You can also change the default prefix path using : --prefix=$PATH

    Build and install the RPMs :
    # make rpm
     
    # yum install ./rpm/RPMS/*.rpm

    Now we need to enable the robinhood service :
    # chkconfig robinhood on

    Almost done, next step would be to create Robinhood's database, be sure to have mysql and mysql-server properly installed and updated, then :
    Start the database engine :
     
    # service mysqld start
     
    And use the script (./scripts) located in the installation folder :
     
    # ./rbh-config create_db
    Icon

    If no option is specified when using rbh-config, it will automatically prompt for parameters (interactive mode).

    Finally we need to create and edit the configuration file.
    # robinhood --template=$CONF_FILE
     
    Will create a default configuration file in your current directory. Juste edit it and move it to /etc/robinhood.d/tmpfs/

    TADA ! Working Robinhood policy engine (even with multiple HSM)!

 

 
IV. Agent's commands

 

    The commands on the agents are launched using lfs :
    # lfs hsm_archive [--archive=$ID] $FILE1 $FILE2 ...
     
    Will backup $FILE1, $FILE2...
     
    # lfs hsm_release $FILE1 $FILE2 ...
     
     
    # robinhood --purge-$OST, $FS, $CLASS