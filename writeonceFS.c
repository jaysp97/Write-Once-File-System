#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

extern int errno;

#define DISK_SIZE 4 * 1024 * 1024
#define SUPERBLOCK_SIZE 20
#define INODE_COUNT 70
#define INODE_SIZE 45
#define DATABLOCK_COUNT 4088
#define DATAMAP_SIZE 4096
#define INODEMAP_SIZE 70
#define BLOCK_SIZE 1024
#define MAGIC_NUM 0x5C3A

enum Flags{WO_RDONLY = 1, WO_WRONLY = 2 ,WO_RDWR = 3, WO_CREAT = 4};

char* DISK;
char* DISK_FILE_NAME;
bool isFileMounted = false;

typedef struct
{
    int data_start;
    int num_inodes;
    int num_data_block;
    int magic_num;
} super_block;

typedef struct
{
    char filename[20];
    int file_id; 
    int file_size;
    int permission;
    int read_seek;
    short data_block_start;
    bool is_open; 
} inode;

typedef struct
{
    char data[1020];
    short next; 
} data_block;

void initDisk(void *address){
    // Set super block     
    ((super_block *)address)->data_start = SUPERBLOCK_SIZE +INODEMAP_SIZE+ INODE_SIZE * INODE_COUNT + DATAMAP_SIZE;
    ((super_block *)address)->num_inodes = INODE_COUNT;
    ((super_block *)address)->num_data_block = DATABLOCK_COUNT;
    ((super_block *)address)->magic_num = MAGIC_NUM;
    address = address + SUPERBLOCK_SIZE;

    // Set Inode map
    for(int i=0;i<INODEMAP_SIZE;i++){
        *(char *)address = 'n';
        address = address + sizeof(char);
    }

    // Set Inodes
    for (int i = 1; i <= INODE_COUNT; i++)
    {
        ((inode *)address)->file_id = i;
        ((inode *)address)->data_block_start = -1;
        ((inode *)address)->file_size = 0;
        ((inode *)address)->is_open = false;
        ((inode *)address)->permission = -1;
        ((inode *)address)->read_seek = 0;
        address = address + INODE_SIZE;
    }

    // Set data bitmap
    for (int i = 0; i < DATAMAP_SIZE; i++)
    {
        *(char *)address = 'n';
        address = address + sizeof(char);
    }

    // File data
    for (int i = 0; i < DATABLOCK_COUNT; i++)
    {
        ((data_block *)address)->next = -1;
        address = address + BLOCK_SIZE;
    }
}

int wo_mount(char *filename, void *address)
{
    int ptr = open(filename, O_RDWR | O_CREAT, 0777);
    DISK = address;
    if(ptr==-1){
        // Error in opening file
        errno = 1;
        return -1;
    }
    if (ptr != -1)
    {
        DISK_FILE_NAME = filename;

        // if file is empty, run init 
        if (lseek(ptr, 0, SEEK_END) == 0)
        {        
            initDisk(address);
        }
        else
        {
            lseek(ptr, 0, SEEK_SET);
            read(ptr, DISK, DISK_SIZE);
            // check if file is broken
            if(((super_block *)DISK)->magic_num != MAGIC_NUM){
                errno = 1; // No EFTYPE errno in Linux
                return -1;
            }
        }
        isFileMounted=true;
        close(ptr);
    }
    return 0;
}

char* strConcat(char *dest, char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0' ; i++)
        dest[i] = src[i];
    dest[n] = '\0';   
    return dest;
}

int getMin(int num1, int num2) {
    return num1 > num2 ? num2 : num1;
}

int wo_unmount(void *address)
{
    int fd = open(DISK_FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0777);
    if(fd==-1){
        // error in unmounting
        errno = 1;
        return -1;
    }
    if(isFileMounted==false){
        // unmounting non-mounted file
        errno = 1;
        return -1;
    }
    void* itr = DISK + SUPERBLOCK_SIZE+INODEMAP_SIZE;

    // close all open files before unmounting
    for(int i=0;i<INODE_COUNT;i++) {
        if(((inode *)itr)->is_open == true) {
            wo_close(((inode *)itr)->file_id);
        }
        itr+=INODE_SIZE;
    }

    // write disk to file
    write(fd, address, DISK_SIZE);
    close(fd);
    return 0;
}

short getFreeInodeIndex()
{
    // get index of free inode
    short target = -1;
    char *itr = DISK + SUPERBLOCK_SIZE ;
    for (short j = 0; j < INODEMAP_SIZE; j++)
    {
        char c = *(char *)itr;
        if (c == 'n')
        {
            *(char *)itr = 'y';
            target = j;
            return target;
        }
        itr++;
    }
    return target;
}

int wo_create(char *filename, int flags)
{
    char *iter = DISK + SUPERBLOCK_SIZE+INODEMAP_SIZE;

    char file_name[20];
    if(filename == NULL){
        errno =22;
        return -1;
    }
    if(isFileMounted=false){
        errno=1;
        return -1;
    }
    strcpy(file_name, filename);
    // Check if file already exists
    for (int i = 0; i < INODE_COUNT; i++)
    {
        if (strcmp((((inode *)(iter))->filename), file_name) == 0)
        {
            // File already exists
            errno=17;
            return -1;
        }
        iter = iter + INODE_SIZE;
    }
    
    int fileDescriptor = -1;
    int target = getFreeInodeIndex();
    if(target==-1){
        //reached max inode limit
        errno = 28;
        return -1;
    }
    
    // save init values to inode struct
    iter = DISK + SUPERBLOCK_SIZE + INODEMAP_SIZE + (target* INODE_SIZE);
    strcpy(((inode *)iter)->filename, file_name);
    fileDescriptor = ((inode *)iter)->file_id;
    ((inode *)iter)->is_open = true;
    ((inode *)iter)->permission = flags;

    return fileDescriptor;
}

int wo_open(char *filename, int flags)
{ 
    if(flags<=0 || flags > 3) {
        // invlid flags passed
        errno = 22;
        return -1;
    }

    if(filename == NULL){
        errno =22;
        return -1;
    }

    if(strlen(filename)>20){
        errno = 36;
        return -1;
    }

    char *iter = DISK + SUPERBLOCK_SIZE+INODEMAP_SIZE;
    char file_name[20];
    strcpy(file_name, filename);
    for (int i = 0; i < INODE_COUNT; i++)
    {
        if (strcmp((((inode *)(iter))->filename), file_name) == 0)
        {
            if(((inode *)(iter))->is_open == true)
            {
                // File is already in open mode
                errno = 1;
                return -1;
            }
            ((inode *)(iter))->is_open = true;
            return ((inode *)iter)->file_id;
        }
        iter = iter + INODE_SIZE;
    }
    // Opening the file which has not been created
    errno=2;
    return -1;
    
}

int wo_read( int fd,  void* buffer, int bytes ) {
    int fd_found = -1;
    void* iter = DISK + SUPERBLOCK_SIZE+INODEMAP_SIZE;
    
    for(int i=0;i<INODE_COUNT;i++) {
        if(((inode *)iter)->file_id == fd) {
            fd_found = fd;
            
            //File found
            if(((inode *)iter)->permission == WO_WRONLY){
                // File does not have read permission
                *((char*)buffer) ='\0';
                errno=13;
                return -1;
            }
            int data_start = ((inode *)iter)->data_block_start;
            if(data_start == -1)
            {
                // File is Empty
                *((char*)buffer) ='\0';
                return 0;
            }
            if(((inode *)iter)->is_open == false) {
                // File not open can't read
                errno=13;
                return -1;
            }
            if(((inode *)iter)->read_seek >= ((inode *)iter)->file_size) {
                // Reached EOF while reading
                return 0;
            }

            int currentOffset = ((inode *)iter)->read_seek;
            int count = getMin(bytes, ((inode *)iter)->file_size - currentOffset);
            // calculate the block number to start reading from
            int toRead = count;
            int blockSkipped = currentOffset/1020;
            char* start_ptr = DISK +((super_block *)DISK)->data_start + BLOCK_SIZE * (data_start);

            // move start pointer as per number of successive blocks
            while(blockSkipped>0 && ((data_block *)start_ptr)->next != -1) {
                blockSkipped--;
                start_ptr = DISK +((super_block *)DISK)->data_start + BLOCK_SIZE * (((data_block *)start_ptr)->next);
            }
            int blocks = currentOffset/1020;
            int remainingBlockSpace = 1020 - (currentOffset - (blocks)*1020);
                
            // init if First block is being read
            int isFirst = 1;
            int remainingBytes = currentOffset - (blocks)*1020;

            while(count>0) {
                if(isFirst != 1) {
                    // for successive blocks
                    int minVal = getMin(count, 1020);
                    strConcat((char* )buffer, ((data_block *)(start_ptr))->data, minVal);
                    buffer = buffer + minVal;
                    count-= minVal;
                    start_ptr = DISK +((super_block *)DISK)->data_start + BLOCK_SIZE * (((data_block *)start_ptr)->next);   
                }
                else {
                    // for first block
                    int minVal = getMin(count, remainingBlockSpace);
                    strConcat((char* )buffer, ((data_block *)(start_ptr))->data + remainingBytes, minVal);
                    buffer = buffer + minVal;
                    count-= minVal;
                    start_ptr = DISK +((super_block *)DISK)->data_start + BLOCK_SIZE * (((data_block *)start_ptr)->next);
                    isFirst = 0;    
                }  
            }
            // return number of bytes read
            int actualRead = toRead-count;
            ((inode *)iter)->read_seek+=(actualRead);
            return actualRead;
        }
       
        iter+=INODE_SIZE;
    }
    if(fd_found == -1) {
        // File Not Found 
        errno=2;
        *((char*)buffer) ='\0';
        return -1;
    }
    return 0;
}

int wo_close( int fd ){
    char* iter = DISK + SUPERBLOCK_SIZE+INODEMAP_SIZE;
    int fd_found = -1;
    for(int i=0;i<INODE_COUNT;i++) {
        if(((inode *)iter)->file_id == fd)
        {
            // find file and close
            fd_found = ((inode *)iter)->file_id;
            ((inode *)iter)->is_open = false;
            ((inode *)iter)->read_seek = 0;
            return 0;
        }
        iter = iter + INODE_SIZE;
    }
    if(fd_found==-1){
        errno=2;
        // File to close not found
        return -1;
    } 
}
short getFreeDataBlockIndex()
{
    // get index of free data block
    short target = -1;
    char *data_itr = DISK + SUPERBLOCK_SIZE + INODEMAP_SIZE+(INODE_COUNT * INODE_SIZE);
    for (short j = 0; j < DATABLOCK_COUNT; j++)
    {

        char c = *(char *)data_itr;
        if (c == 'n')
        {
            *(char *)data_itr = 'y';
            target = j;
            return target;
        }
        data_itr++;
    }

    return target;
}


int wo_write(int fd, void *buffer, int bytes)
{
    short target_dataBlock = -1;
    char *iter = DISK + SUPERBLOCK_SIZE+INODEMAP_SIZE;
    int fd_found =-1;
    char tempBuffer[bytes];
    strcpy(tempBuffer, buffer);

    for (int i = 0; i < INODE_COUNT; i++)
    { 
        if (((inode *)(iter))->file_id == fd)
        {
            fd_found = ((inode *)(iter))->file_id;
            if(((inode *)(iter))->permission == WO_RDONLY){
                // File does not have write permission 
                errno = 30;
                return -1; //return error code;
            }
            if(((inode *)iter)->is_open == false) {
                // File not open can't read
                errno =13;
                return -1;
            }
            if (((inode *)(iter))->data_block_start == -1)
            {
                int prev_dataBlock = -1;
                int count = bytes;
                
                int blocksToSkip = bytes / 1020;
                // get free data block
                target_dataBlock = getFreeDataBlockIndex();
                if (target_dataBlock == -1)
                {
                    // no free space left on disk
                    errno=28;
                    return -1;
                }
                else
                {
                    // this is the first dataBlock in the file
                    prev_dataBlock = target_dataBlock;
                    char *free_dataBlock = DISK +((super_block *)DISK)->data_start + (target_dataBlock * BLOCK_SIZE);
                    strncpy(((data_block *)free_dataBlock)->data, (char *)tempBuffer, getMin(count, 1020));
                    ((inode *)(iter))->data_block_start = target_dataBlock;
                    ((inode *)(iter))->file_size += getMin(count, 1020);
                    count-=getMin(count, 1020);
                }
                if (blocksToSkip > 0)
                {
                    // if the file length exceeds 1020 bytes
                    for (int x = 0; x < blocksToSkip && count > 0; x++)
                    {
                        // get next free data block
                        target_dataBlock = getFreeDataBlockIndex();
                        if (target_dataBlock == -1)
                        {
                            // No free datablock available
                            errno = 28;
                            return bytes - count;
                            //return -1;
                        }
                        else
                        {
                            // find previous data block
                            char *prev_free_dataBlock = DISK + ((super_block *)DISK)->data_start + (prev_dataBlock * BLOCK_SIZE);
                            ((data_block *)prev_free_dataBlock)->next = target_dataBlock;
                            prev_dataBlock = target_dataBlock;

                            // create new data block and link the previous one
                            char *free_dataBlock = DISK + ((super_block *)DISK)->data_start + (target_dataBlock * BLOCK_SIZE);
                            strncpy(((data_block *)free_dataBlock)->data, (char *)tempBuffer + ((x + 1) * 1020), getMin(count, 1020));
                            ((inode *)(iter))->file_size += getMin(count, 1020);
                            ((data_block *)free_dataBlock)->next = -1;
                        }
                        count -= getMin(count, 1020);
                    }
                   
                }
                return getMin(bytes - count, bytes);
                
            }
            else
            {
                // if new datablock is not the first one
                int prev_dataBlock = -1;
                int file_size = (((inode *)iter)->file_size);

                // get data iterator
                void *data_itr = DISK + ((super_block *)DISK)->data_start + (((inode *)iter)->data_block_start) * BLOCK_SIZE; 
                while (((data_block *)(data_itr))->next != -1)
                {
                    file_size -= 1020;
                    data_itr = DISK + ((super_block *)DISK)->data_start + (((data_block *)(data_itr))->next) * BLOCK_SIZE;
                }

                char tempContent[1020];

                // calculate remaining space on disk and write
                int remainingBlockSize = 1020 - file_size;
                int count = bytes;
                strncat(((data_block *)data_itr)->data,tempBuffer, getMin(count, remainingBlockSize));
                ((inode *)(iter))->file_size += getMin(count, remainingBlockSize);
                count-= getMin(count, remainingBlockSize);
               
                // if all data has been written
                if(count <= 0){
                    return bytes;
                }

                int blocksToSkip = count / 1020;
                target_dataBlock = getFreeDataBlockIndex();
                ((data_block *)data_itr)->next = target_dataBlock;

                if (target_dataBlock == -1)
                {
                    // No free dataBlock available
                    errno=28;
                    return bytes - count;
                    //return -1;
                }
                else
                {
                    // first data block in the chain
                    prev_dataBlock = target_dataBlock;
                    char *free_dataBlock = DISK +  ((super_block *)DISK)->data_start + (target_dataBlock * BLOCK_SIZE);
                    strncpy(((data_block *)free_dataBlock)->data, (char *)tempBuffer+remainingBlockSize, getMin(count, 1020));
                    ((inode *)(iter))->file_size += getMin(count, 1020);
                    count -= getMin(count, 1020);
                }

                if (blocksToSkip > 0)
                {
                    // if there is more data to write
                    for (int x = 0; x < blocksToSkip && count>0; x++)
                    {
                        target_dataBlock = getFreeDataBlockIndex();
                       
                        if (target_dataBlock == -1)
                        {
                            // No free dataBlock available
                            errno=28;
                            return bytes - count; 
                            //return -1;    
                        }
                        else
                        {
                            // link previous block
                            char *prev_free_dataBlock = DISK + ((super_block *)DISK)->data_start + (prev_dataBlock * BLOCK_SIZE);
                            ((data_block *)prev_free_dataBlock)->next = target_dataBlock;
                            prev_dataBlock = target_dataBlock;

                            // create new block
                            char *free_dataBlock = DISK + ((super_block *)DISK)->data_start + (target_dataBlock * BLOCK_SIZE);
                            strncpy(((data_block *)free_dataBlock)->data, (char *)tempBuffer+remainingBlockSize + ((x + 1) * 1020), getMin(count, 1020));
                            ((inode *)(iter))->file_size += getMin(count, 1020);
                        }
                        count -= getMin(count, 1020);
                    }
                }
                return getMin(bytes - count, bytes);    
            }
            return 0;
        }
        iter = iter + INODE_SIZE;
    }
    if(fd_found==-1){
        // File to write not found
        errno=2;
        return -1;
    }
}