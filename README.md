# GameStorage

This code handles game save files (.storage) for Bigosaur's games: Son of a Witch, Rogue Bit, CMTS, etc.

dumpstorage2.cpp is a tool used to dump all the data from a .storage file. It shows "double" type as original form and also fixed point number and also as a date, because CMTS stores dates as double (seconds since epoch).

Storage.cpp is the full class that does reading and writing. It was converted from Storage.h file with minor modifications. You can use the class and call public methods to create, read, modify .storage files.

This code is public domain. Feel free to use it as you see fit.
