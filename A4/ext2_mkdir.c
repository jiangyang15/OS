/*
	This program takes two command line arguments. The first is the name of an ext2 formatted virtual disk. 
	The second is an absolute path on your ext2 formatted disk. The program should work like mkdir, 
	creating the final directory on the specified path on the disk. If any component on the path 
	to the location where the final directory is to be created does not exist or if the specified directory already exists, 
	then your program should return the appropriate error (ENOENT or EEXIST). 
Note:
	Please read the specifications to make sure you're implementing everything correctly (e.g., directory entries should be aligned to 4B, entry names are not null-terminated, etc.).
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

void create_dir_inode(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
												unsigned int parent_inode_num, const char *dir_name){


	// get dir_name inode number , block number
	int new_inode_number = get_free_inode_num(sb, gd);
	if (new_inode_number == 0){
		exit(ENOSPC);
	}
	int new_block_number = get_free_block_num(sb, gd);
	if (new_block_number == 0){
		exit(ENOSPC);
	}

	//get  inode  table  position   and   set  value
	int len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (new_inode_number-1)*sb->s_inode_size;
	struct ext2_inode * new_ei = (struct ext2_inode *) (disk + len);

	// set file type
	new_ei->i_mode |= EXT2_S_IFDIR; 
	// set the size, At least save  .   ..
	new_ei->i_size = EXT2_BLOCK_SIZE;
	new_ei->i_blocks = 2; 
	// . link
	new_ei->i_links_count = 1; 
	for(int i = 0; i < 15; i++){
		new_ei->i_block[i] = 0; 
	}
	new_ei->i_block[0] = new_block_number;
	new_ei->i_dtime = 0;

	// update parent inode data
	len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (parent_inode_num - 1)*sb->s_inode_size;
	struct ext2_inode * parent_ei = (struct ext2_inode *) (disk + len);   

	struct ext2_dir_entry * parent_ed;
	int  last_len;
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
	new_ed->name_len = strlen(dir_name);
	new_ed->file_type = EXT2_FT_DIR;
	new_ed->rec_len = EXT2_BLOCK_SIZE - last_len;
	memcpy((char *) ((unsigned char *)new_ed + sizeof(struct ext2_dir_entry)), dir_name, strlen(dir_name));


	// set . dir to new block
	struct ext2_dir_entry * new_dot_ed = (struct ext2_dir_entry *) (disk + 1024 + (new_block_number - 1)*EXT2_BLOCK_SIZE);
	new_dot_ed->file_type = EXT2_FT_DIR;
	new_dot_ed->inode = new_inode_number;
	new_dot_ed->name_len = 1;	
	new_dot_ed->rec_len = sizeof(struct ext2_dir_entry) + \
						 new_dot_ed->name_len + \
						 (4-((sizeof(struct ext2_dir_entry) + new_dot_ed->name_len) % 4));
	memcpy((char *) ((unsigned char *)new_dot_ed + sizeof(struct ext2_dir_entry)), ".", 1);

	new_ei->i_links_count ++;

	// set .. dir to new block	
	struct ext2_dir_entry * new_dot2_ed = (struct ext2_dir_entry *) (disk + 1024 + (new_block_number - 1)*EXT2_BLOCK_SIZE + new_dot_ed->rec_len);
	new_dot2_ed->file_type = EXT2_FT_DIR;
	new_dot2_ed->inode = parent_inode_num;
	new_dot2_ed->name_len = 2;	
	new_dot2_ed->rec_len = EXT2_BLOCK_SIZE - new_dot_ed->rec_len;
	memcpy((char *) ((unsigned char *)new_dot2_ed + sizeof(struct ext2_dir_entry)), "..", 2);

	// ..  link  parent inode
	parent_ei->i_links_count += 1;
	
	// update  block bitmap
	update_block_bitmap(sb,gd,new_block_number,1);
	// update inode bitmap
	update_inode_bitmap(sb,gd,new_inode_number,1);

	// update count 
	gd->bg_free_blocks_count -= 1;
	gd->bg_free_inodes_count -= 1;
	gd->bg_used_dirs_count += 1;

	sb->s_free_blocks_count -= 1;
	sb->s_free_inodes_count -= 1;
	
}


int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path name>\n", argv[0]);
        exit(1);
    }

	char input_path[1024];

	// check path
	if(argv[2][0] != '/'){
		return ENOENT;
	}
	strcpy(input_path, argv[2]);
	
	if (input_path[strlen(input_path) - 1] == '/')
		input_path[strlen(input_path) - 1] = '\0';
	
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + sizeof(struct ext2_super_block));

	// check  path if exisit 
	char *delim = "/";
	char *dir_name = strtok(input_path, delim);
	int  inode_num = EXT2_ROOT_INO;
	int  ret  = -1;
	char pre_dir_name[255];
	
	while (dir_name != NULL){
		ret = find_dir_inode(sb, gd, inode_num, dir_name);
		// copy the dir name
		strcpy(pre_dir_name, dir_name);
		dir_name = strtok(NULL, delim);
		if (ret == -1 && dir_name != NULL){
			// no exist directory
			return  ENOENT;
		}

		if (ret != -1 && dir_name == NULL){
			//final  dir_name  exist
			return EEXIST;
		}

		if (ret == -1 && dir_name == NULL){
			//create new dir
			create_dir_inode(sb, gd, inode_num, pre_dir_name);
		}
		else{
			// continue find
			inode_num = ret;
		}
	}
	
    return 0;
}
