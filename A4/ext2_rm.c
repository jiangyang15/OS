/*
	This program takes two command line arguments. 
	The first is the name of an ext2 formatted virtual disk,
	and the second is an absolute path to a file or link (not a directory) on that disk. 
	The program should work like rm, removing the specified file from the disk. 
	If the file does not exist or if it is a directory, then your program should return the appropriate error. 
	Once again, please read the specifications of ext2 carefully, 
	to figure out what needs to actually happen when a file or link is removed
	(e.g., no need to zero out data blocks, must set i_dtime in the inode, 
	removing a directory entry need not shift the directory entries after the one being deleted, etc.). 
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

void rm(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
												int parent_inode_num,
												const char * file_name){
	// find dir inode
	int len = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (parent_inode_num -1)*sb->s_inode_size;
	struct ext2_inode * parent_ei = (struct ext2_inode *) (disk + len); 
	
	struct ext2_dir_entry * parent_dir_entry;
	struct ext2_dir_entry * pre_dir_entry = NULL; 
	char name[255];
	struct ext2_inode * file_ei;
	int file_inode_num;

	for (int i=0; i<parent_ei->i_blocks/2; ++i){
		if (parent_ei->i_block[i]){
			len = 0;	
			// delete  file_name  from dir entry
			// use rec_len skip
			while (1){
				parent_dir_entry = (struct ext2_dir_entry *) (disk+1024 + (parent_ei->i_block[i] - 1) * EXT2_BLOCK_SIZE + len);
		
				strncpy(name, parent_dir_entry->name, parent_dir_entry->name_len);
				name[parent_dir_entry->name_len] = '\0';
			
				if (strcmp(name, file_name) == 0) {
		
					if (pre_dir_entry == NULL){
						// no possible, hava . .. dir
					}
					else{
						// pre entry become last  entry
						pre_dir_entry->rec_len += parent_dir_entry->rec_len;					
					}
		
					// file_name inode link  -1
					int postion = 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (parent_dir_entry->inode -1)*sb->s_inode_size;
					file_ei = (struct ext2_inode *) (disk + postion);
					file_inode_num = parent_dir_entry->inode;
					break ;
				}
			
				//next file
				if (len + parent_dir_entry->rec_len >= EXT2_BLOCK_SIZE)
					break;
				len += parent_dir_entry->rec_len;
				pre_dir_entry = parent_dir_entry;
			}
					
		}
	}
	
	// unlink file inode
	file_ei->i_links_count --;
	file_ei->i_dtime = 1; 
	
    //delete file inode if  no link
    if (file_ei->i_links_count == 0 && file_ei->i_dtime > 0){
		//free file data block
		int block_nums = file_ei->i_blocks/2;
		for (int i=0; i < block_nums; ++i){
			// 0 - 12
			update_block_bitmap(sb, gd, file_ei->i_block[i], 0);
			sb->s_free_blocks_count ++;
			gd->bg_free_blocks_count ++;
			
			// Two level pointer 
			if (i == 12){
				unsigned int * two_level_block = (unsigned int *)(disk + 1024 + (file_ei->i_block[12] - 1) * EXT2_BLOCK_SIZE);
				for(int j = 13; j < block_nums; j++){
					update_block_bitmap(sb, gd, two_level_block[j-13], 0);
					sb->s_free_blocks_count ++;
					gd->bg_free_blocks_count ++;
				}
				break;
			}
			
		}

		// free inode
		update_inode_bitmap(sb, gd, file_inode_num, 0);
		sb->s_free_inodes_count ++;
		gd->bg_free_inodes_count ++;
	}
}

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path name>\n", argv[0]);
        exit(1);
    }

	char input_path[1024];

	// check if absolute path
	if(argv[2][0] != '/'){
		return ENOENT;
	}
	strcpy(input_path, argv[2]);
	
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + sizeof(struct ext2_super_block));

	int parent_inode = EXT2_ROOT_INO;
	char *delim = "/";
	char *dir_name;
	int ret = -1;
	char path[1024];
	char last_file[255];
	char *p;

	// get  src file path  and   the last filename 
	if (input_path[strlen(input_path) - 1] == '/')
		input_path[strlen(input_path) - 1] = '\0';
	
	p = strrchr(input_path, '/');
	strncpy(path, input_path, p-input_path);
	path[p-input_path] = '\0';
	strncpy(last_file, p + 1, strlen(input_path) - strlen(path) - 1);
	last_file[strlen(input_path) - strlen(path) - 1] = '\0';

	// check file path
	dir_name = strtok(path, delim);
	while (dir_name != NULL){
		ret = find_dir_inode(sb, gd, parent_inode, dir_name);
		dir_name = strtok(NULL, delim);
		if (ret == -1){
			// no exist directory
			return  ENOENT;
		}
		
		// continue find
		parent_inode = ret;
	}

	struct ext2_dir_entry * last_file_entry = file_dir_entry(sb, gd, parent_inode, last_file);

	// check last file
	if (last_file_entry == NULL){
		// no src file
		return ENOENT;
	}

	if (last_file_entry->file_type == EXT2_FT_DIR){
		return EISDIR;
	}

	rm(sb, gd, parent_inode, last_file);
	
    return 0;
}
