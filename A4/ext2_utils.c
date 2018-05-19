#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2_utils.h"

struct ext2_dir_entry *  file_dir_entry(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
	unsigned int inode_num, const char * filename){

	int len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (inode_num -1)*sb->s_inode_size;
	struct ext2_inode * ei = (struct ext2_inode *) (disk + len);   
	char name[255];

	// look up all file
	struct ext2_dir_entry * ed;
	for (int i=0; i < ei->i_blocks/2; ++i){
		if (ei->i_block[i]){
			len = 0;
			while (1){
				ed = (struct ext2_dir_entry *) (disk+1024 + (ei->i_block[i] - 1) * EXT2_BLOCK_SIZE+len);

				strncpy(name, ed->name, ed->name_len);
				name[ed->name_len] = '\0';
				if (strcmp(name, filename) == 0){
					// return dir inode
					return ed;
				}

				//next file
				if (len + ed->rec_len >= ei->i_size)
					break;
				len += ed->rec_len;
			}
		}
	}
	return NULL;
}



struct ext2_dir_entry *  last_file_dir_entry(struct ext2_inode * ei, int *last_len){
	struct ext2_dir_entry * ed;
	int len;

	// find  last dir entry
	for (int i = 0; i < ei->i_blocks/2; ++i){
		if (ei->i_block[i]){
			len = 0;
			while (1){
				ed = (struct ext2_dir_entry *) (disk+1024 + (ei->i_block[i] - 1) * EXT2_BLOCK_SIZE + len);
					
				//next file
				if (len + ed->rec_len >= EXT2_BLOCK_SIZE)
					break;
				len += ed->rec_len;
			}
		}
	}
	*last_len = len;
	return ed;
}



int find_dir_inode(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
						unsigned int inode_num, const char *dir_name){
	// find inode table
	int len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (inode_num -1)*sb->s_inode_size;
	struct ext2_inode * ei = (struct ext2_inode *) (disk + len);   
	char name[255];

	struct ext2_dir_entry * ed;
	// look up all file
	for (int i = 0; i < ei->i_blocks/2; ++i){
		if (ei->i_block[i]){
			len = 0;
	
			while (1){
				ed = (struct ext2_dir_entry *) (disk+1024 + (ei->i_block[i] - 1) * EXT2_BLOCK_SIZE+len);

				strncpy(name, ed->name, ed->name_len);
				name[ed->name_len] = '\0';
				if (strcmp(name, dir_name) == 0 && ed->file_type == EXT2_FT_DIR){
					// return dir inode
					return ed->inode;
				}

				//next file
				if (len + ed->rec_len >= ei->i_size)
					break;
				len += ed->rec_len;
			}
		}
	}
	return -1;
}

int get_free_inode_num(struct ext2_super_block *sb, struct ext2_group_desc *gd){
	int len = 1024 + (gd->bg_inode_bitmap-1) * EXT2_BLOCK_SIZE;
	char b;
	int  inode_num = 1;
	for (int i = len; i < len + sb->s_inodes_count/8; ++i){
		// get one bytes
		b = disk[i];
		// find no use
		for (int j = 0; j < 8; j++){
			if ((b & 1) == 0){
				// no use
				return inode_num;
			}
			b = b >> 1;
			inode_num ++;
		}
	}
	return 0;
}

int get_unused_inode_num(struct ext2_super_block *sb, struct ext2_group_desc *gd){
	int len = 1024 + (gd->bg_inode_bitmap-1) * EXT2_BLOCK_SIZE;
	char b;
	int  count = 0;
	for (int i = len; i < len + sb->s_inodes_count/8; ++i){
		// get one bytes
		b = disk[i];
		// find no use
		for (int j = 0; j < 8; j++){
			if ((b & 1) == 0){
				// no use
				count ++;
			}
			b = b >> 1;
		}
	}
	return count;
}


int get_inode_bitmap_by_index(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index){
	int len = 1024 + (gd->bg_inode_bitmap-1) * EXT2_BLOCK_SIZE;
	char b;
	int  inode_num = 1;
	for (int i = len; i < len + sb->s_inodes_count/8; ++i){
		// get one bytes
		b = disk[i];
		// find index
		for (int j = 0; j < 8; j++){
			if (inode_num == index){
				return (b & 1);
			}
			b = b >> 1;
			inode_num ++;
		}
	}
	return 1;
}

void update_inode_bitmap(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index, int used){
	int len = 1024 + (gd->bg_inode_bitmap-1) * EXT2_BLOCK_SIZE;
	int inode_num = 1;
	for (int i = len; i < len + sb->s_inodes_count/8; ++i){
		// find index  update bit
		for (int j = 0; j < 8; j++){
			if (inode_num == index){
				if (used){
					disk[i] |= (1<<j);
				}
				else{
					disk[i] &= ~(1<<j);
				}
				return;
			}
			inode_num ++;
		}
	}
}


int get_free_block_num(struct ext2_super_block *sb, struct ext2_group_desc *gd){
	int len = 1024 + (gd->bg_block_bitmap-1) * EXT2_BLOCK_SIZE;
	char r;
	int  block_num = 1;
	for (int i = len; i < len + sb->s_blocks_count/8; ++i){
		// get one bytes
		r = disk[i];
		// find no use
		for (int j = 0; j < 8; j++){
			if ((r & 1) == 0){
				// no use
				return block_num;
			}
			r = r >> 1;
			block_num ++;
		}
	}
	return 0;
}

int get_unused_block_num(struct ext2_super_block *sb, struct ext2_group_desc *gd){
	int len = 1024 + (gd->bg_block_bitmap-1) * EXT2_BLOCK_SIZE;
	char b;
	int  count = 0;
	for (int i = len; i < len + sb->s_blocks_count/8; ++i){
		// get one bytes
		b = disk[i];
		// find no use
		for (int j = 0; j < 8; j++){
			if ((b & 1) == 0){
				// no use
				count ++;
			}
			b = b >> 1;
		}
	}
	return count;
}


int get_block_bitmap_by_index(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index){
	int len = 1024 + (gd->bg_block_bitmap-1) * EXT2_BLOCK_SIZE;
	char b;
	int  block_num = 1;
	for (int i = len; i < len + sb->s_blocks_count/8; ++i){
		// get one bytes
		b = disk[i];
		// find index
		for (int j = 0; j < 8; j++){
			if (block_num == index){
				return (b & 1);
			}
			b = b >> 1;
			block_num ++;
		}
	}
	return 1;
}


void update_block_bitmap(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index, int used){
	int len = 1024 + (gd->bg_block_bitmap-1) * EXT2_BLOCK_SIZE;
	int block_num = 1;
	for (int i = len; i < len + sb->s_blocks_count/8; ++i){
		for (int j = 0; j < 8; j++){
			// find index  update bit
			if (block_num == index){
				if (used){
					disk[i] |= (1<<j);
				}
				else{
					disk[i] &= ~(1<<j);
				}
				return;
			}
			block_num ++;
		}
	}
}
