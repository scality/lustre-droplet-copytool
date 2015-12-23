#I. Suggestions

a. About puts, the main problem was a size matter, knowing that right now
we're doing a single put, if ever the data has a huge size it doesn't
really suits a http solution.
Relying on a stripping solution could be a way to optimize the put,
performance could be enhanced by using multi-threading to archive every and
each stripe simultaneously.
The main issue about that solution is the BIGNUM key used. regarding that
fact, the following schemes were what I came to think of:

    file ----> data *--------> key#1 ---> stripe#1 ---> metadata/n° of stripes
                    |--------> key#2 ---> stripe#2
                    |--------> key#3 ---> stripe#3
                    |--------> key#4 ---> stripe#4
                    |--------> key#5 ---> stripe#5
                    |
                    *--------> key#n ---> stripe#n

In that configuration, the first stripe would contain the number of stripes and
the metadata, allowing us to find each other stripe emulating their location in
the ring using a known pattern with key#1.
The algorythm would allow us to generate keys using the original one.

    file ----> data *--------> key ---> metadata / key database
                                                 |
                                                 *---> sub-key#1 ---> stripe#1
                                                 |---> sub-key#2 ---> stripe#2
                                                 |---> sub-key#3 ---> stripe#3
                                                 |---> sub-key#4 ---> stripe#4
                                                 |---> sub-key#5 ---> stripe#5
                                                 |
                                                 *---> sub-key#n ---> stripe#n

In that configuration, we have one key that leads to a container containing the
metadata and a database listing all keys regarding the stripes of the data.
Keys are then not necessarly generated from an original key and can be completely
different from each other.

Similar to indexation form, at the date of today question is about MESA's use or
not.

In term of performances, puts and gets can be done with multi-threading and therefore
a big sized data would be stored and recovered more efficiently, theorically speaking.

b. We know that the Robinhood is Lustre related only and does not interact in any way
with the Ring.
It uses set up flags on files to determine whether it's new, archived, released, or
synced, but at no moment it will go check the Ring if the information is true or not.
This behaviour lets us know that if by any mean the data/metadata is lost on the Lustre
but wasn't removed from the Ring, we will end up with orphan data on the Ring, with
no link to anything on the Lustre.

Erwan was asking if it was possible to restore data from the Ring directly to the Lustre,
making it some kind of backup device allowing us to restore data that was lost on the
Lustre.
Right now, there is no possibility regarding that action. We're not stocking paths or
file names on the Ring.
If necessary, that could be a possibility, to backup paths and/or file names in order to
be able to restore lost files from the Lustre directly from the Ring. But that would imply
to backup the paths in some way or maybe use a by-path rather then by-key solution.

Also, that would imply a possibility to manually launch a full Ring scan to restore every
archived data on the Ring knowing that there would be no direct ways to find a specific
file that was lost on the Lustre (no FID / BIGNUM keys).

    Lustre -----> Lustre - data#1 <--- Ring<-----*--- Key - metadata / path <-- data#1
                                        | ^      |
                                        | |    Scan
                                        | |      |
                  Lustre - data#2 <-----* *------*--- Key - metadata /path <-- data#2
                                        | |      |
                  Lustre - data#3 <-----* *------*--- Key - metadata /path <-- data#3
                                        | |      |
                  Lustre - data#n <-----* *------*--- Key - metadata /path <-- data#n
                                                 |
                                                 v