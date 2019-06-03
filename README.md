# dir-connection
OS and System Programming lab #7.  
Linux-based OS required.  
Contains c-language program  
Task: Write a program to synchronize two directories, for example, Dir1 and Dir2.  
Result files available in Dir1, but missing in Dir2, should be copied to Dir2 along with permissions(including all subdirectories).  
The copy procedure must be executed in a separate process for each file being copied.  
The head process creates unnamed channels for communication with child processes and sends the path and file name to the channel.  
Child processes write their pid, the full path to the file being copied and the number of bytes copied to the channel.  
The number of simultaneously running processes should not exceed N (entered by the user).
# Run: 
```
gcc -o runnable main.c  
./runnable <source_dir> <dest_dir> <max_processes>  
```
