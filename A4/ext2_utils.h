
#ifndef EXT2_UTILS_H
#define EXT2_UTILS_H

#include "ext2.h"

extern unsigned char *disk;
// return file entry
struct ext2_dir_entry *  file_dir_entry(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
	unsigned int inode_num, const char * filename);

// find  last dir entry

struct ext2_dir_entry *  last_file_dir_entry(struct ext2_inode * ei, int *last_len);


// find  dir inode 
int find_dir_inode(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
						unsigned int inode_num, const char *dir_name);

// get free inode number
int get_free_inode_num(struct ext2_super_block *sb, struct ext2_group_desc *gd);

int get_unused_inode_num(struct ext2_super_block *sb, struct ext2_group_desc *gd);

// get inode bitmap by index
int get_inode_bitmap_by_index(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index);



// update inode bitmap
void update_inode_bitmap(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index, int used);


int get_unused_block_num(struct ext2_super_block *sb, struct ext2_group_desc *gd);

// get free block number
int get_free_block_num(struct ext2_super_block *sb, struct ext2_group_desc *gd);

int get_block_bitmap_by_index(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index);

// update block bitmap
void update_block_bitmap(struct ext2_super_block *sb, struct ext2_group_desc *gd, int index, int used);
#endif
