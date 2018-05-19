/*
	ext2_cp: This program takes three command line arguments. 
	The first is the name of an ext2 formatted virtual disk. 
	The second is the path to a file on your native operating system, 
	and the third is an absolute path on your ext2 formatted disk. 
	The program should work like cp, 
	copying the file on your native file system onto the specified location on the disk. 
	If the source file does not exist or the target is an invalid path, 
	then your program should return the appropriate error (ENOENT). 
	If the target is a file with the same name that already exists, 
	you should not overwrite it (as cp would), just return EEXIST instead. 
Note:
	Please read the specifications of ext2 carefully, some things you will not need to worry about (like permissions, gid, uid, etc.), while setting other information in the inodes may be important (e.g., i_dtime).
	When you allocate a new inode or data block, you *must use the next one available* from the corresponding bitmap (excluding reserved inodes, of course). Failure to do so will result in deductions, so please be careful about this requirement.
	Be careful to consider trailing slashes in paths. These will show up during testing so it's your responsibility to make your code as robust as possible by capturing corner cases.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_utils.h"

unsigned char *disk;

void copy_file(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
												unsigned int parent_inode_num, 
												const char *file_name,
												FILE * fd){

	// get parent dir,check file if exist
	int len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (parent_inode_num - 1)*sb->s_inode_size;
	struct ext2_inode * parent_ei = (struct ext2_inode *) (disk + len);
	struct ext2_dir_entry * parent_ed = NULL;
	
	parent_ed = file_dir_entry(sb, gd, parent_inode_num, file_name);
	if (parent_ed == NULL){
		// last file name no exist, continue cp
		
	}
	else{
		if (parent_ed->file_type == EXT2_FT_DIR){
			//last file is dir, find last ei
			len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (parent_ed->inode - 1)*sb->s_inode_size;
			parent_ei = (struct ext2_inode *) (disk + len);
		}
		else {
			exit(EEXIST);
		}
	}
	
	// get size of file
	int file_size;
	fseek(fd, 0, SEEK_END);
	file_size = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	// compute  need  block number
	int file_block_num;
	if (file_size % EXT2_BLOCK_SIZE == 0)
		file_block_num = file_size/EXT2_BLOCK_SIZE;
	else
		file_block_num = file_size/EXT2_BLOCK_SIZE + 1;

	if (file_block_num > 12){
		// only need Two level pointer, because the total block is 128
		file_block_num ++;
	}
	
	if (file_block_num > sb->s_free_blocks_count)
		// no block to save file
		exit(ENOSPC);

	
	// get filename inode number
	int new_inode_number = get_free_inode_num(sb, gd);
	if (new_inode_number == 0){
		exit(ENOSPC);
	}
	len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (new_inode_number-1)*sb->s_inode_size;
	struct ext2_inode * new_ei = (struct ext2_inode *) (disk + len);
	
	// set file type
	new_ei->i_mode |= EXT2_S_IFREG; 
	// set the size
	new_ei->i_size = file_size;
	new_ei->i_blocks = 2*file_block_num; 
	// . link
	new_ei->i_links_count = 1; 

	// update inode 
	update_inode_bitmap(sb, gd, new_inode_number, 1);

	// set block poniter
	int i;
	for(i = 0; i < 15; i++){
		new_ei->i_block[i] = 0; 
	}
	for (i = 0; i < file_block_num; ++i){
		// 0 - 12  get free inode  
		new_ei->i_block[i] = get_free_block_num(sb, gd);
		update_block_bitmap(sb, gd, new_ei->i_block[i],1);
		if (i == 12){
			// Two level pointer
			unsigned int * two_level_block = (unsigned int *)(disk + 1024 + (new_ei->i_block[12] - 1) * EXT2_BLOCK_SIZE);

			// save block number
			for(int j = 13; j < file_block_num; j++){
				two_level_block[j-13] = get_free_block_num(sb, gd);
				update_block_bitmap(sb, gd, two_level_block[j-13],1);
			}
			
			break;
		}
	}
	
	new_ei->i_dtime = 0;
	unsigned char *block_data_ptr;
	
	// copy file content
	for (i = 0; i < file_block_num; ++i){
		if (i == 12){
			// Two level pointer
			unsigned int * two_level_block = (unsigned int *)(disk + 1024 + (new_ei->i_block[12] - 1) * EXT2_BLOCK_SIZE);
			for(int j = 13; j < file_block_num; j++){
				block_data_ptr = disk + 1024 + (two_level_block[j-13] - 1)*EXT2_BLOCK_SIZE;
				fread(block_data_ptr, sizeof(char), EXT2_BLOCK_SIZE, fd);
			}
			break;
		}
		// 0 - 11,  copy content
		block_data_ptr = disk + 1024 + (new_ei->i_block[i] - 1)*EXT2_BLOCK_SIZE;
		fread(block_data_ptr, sizeof(char), EXT2_BLOCK_SIZE, fd);
	}
	
	// update parent inode data   
	int last_len;
	parent_ed = last_file_dir_entry(parent_ei, &last_len);

	// add new dir entry
	// modify the last dir rec_len
	//directory entries should be aligned to 4B
	parent_ed->rec_len = sizeof(struct ext2_dir_entry) + \
						 parent_ed->name_len + \
						 (4-((sizeof(struct ext2_dir_entry) + parent_ed->name_len) % 4));
	struct ext2_dir_entry * new_ed = (struct ext2_dir_entry *)(((unsigned char *) parent_ed) + \
										parent_ed->rec_len);
	last_len += parent_ed->rec_len;
	new_ed->inode = new_inode_number;
	new_ed->name_len = strlen(file_name);
	new_ed->file_type = EXT2_FT_REG_FILE;
	new_ed->rec_len = EXT2_BLOCK_SIZE - last_len;
	memcpy((char *) ((unsigned char *)new_ed + sizeof(struct ext2_dir_entry)), file_name, strlen(file_name));

	// update count 
	gd->bg_free_blocks_count -= file_block_num;
	gd->bg_free_inodes_count -= 1;

	sb->s_free_blocks_count -= file_block_num;
	sb->s_free_inodes_count -= 1;
	
}


int main(int argc, char **argv) {

    if(argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <native file name> <absolute path name>\n", argv[0]);
        exit(1);
    }
	char  input_path[1024];
	// check path
	if(argv[3][0] != '/'){
		return ENOENT;
	}
	strcpy(input_path, argv[3]);
	
	
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + sizeof(struct ext2_super_block));

	// Check native file if exists
	FILE *native_fd;
	native_fd = fopen(argv[2], "r");
	
	if(native_fd == NULL){
		printf("File not exist\n");
		fclose(native_fd);
		exit(ENOENT);
	}

	
	// get last filename
	char *delim="/";
	char dest_path[1024];
	char dest_last_file[255];
	char *p;
	if (argv[3][strlen(argv[3]) - 1] == '/')
		argv[3][strlen(argv[3]) - 1] = '\0';
	
	p = strrchr(input_path, '/');
	if (p != NULL){
		strncpy(dest_path, input_path, p-input_path);
		dest_path[p-input_path] = '\0';
		strncpy(dest_last_file, p + 1, strlen(input_path) - strlen(dest_path) - 1);
		dest_last_file[strlen(input_path) - strlen(dest_path) - 1] = '\0';
	}
	else{
		strcpy(dest_path, "");
		strcpy(dest_last_file, input_path);
	}
	
	char *dir_name = strtok(dest_path, delim);
	int  inode_num = EXT2_ROOT_INO;
	int  ret  = -1;
	
	while (dir_name != NULL){
		ret = find_dir_inode(sb, gd, inode_num, dir_name);
		dir_name = strtok(NULL, delim);
		if (ret == -1){
			// no exist directory
			return  ENOENT;
		}
		
		// continue find
		inode_num = ret;
	}

	// copy the file to ext2
	copy_file(sb, gd, inode_num, dest_last_file, native_fd);
	fclose(native_fd);
    return 0;
}
