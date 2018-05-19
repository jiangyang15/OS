/*
ext2_checker: This program takes only one command line argument: the name of an ext2 formatted virtual disk. The program should implement a lightweight file system checker, which detects a small subset of possible file system inconsistencies and takes appropriate actions to fix them (as well as counts the number of fixes), as follows:
	the superblock and block group counters for free blocks and free inodes must match the number of free inodes and data blocks as indicated in the respective bitmaps. If an inconsistency is detected, the checker will trust the bitmaps and update the counters. Once such an inconsistency is fixed, your program should output the following message: "Fixed: X's Y counter was off by Z compared to the bitmap", where X stands for either "superblock" or "block group", Y is either "free blocks" or "free inodes", and Z is the difference (in absolute value). The Z values should be added to the total number of fixes.
	for each file, directory, or symlink, you must check if its inode's i_mode matches the directory entry file_type. If it does not, then you shall trust the inode's i_mode and fix the file_type to match. Once such an inconsistency is repaired, your program should output the following message: "Fixed: Entry type vs inode mismatch: inode [I]", where I is the inode number for the respective file system object. Each inconsistency counts towards to total number of fixes.
	for each file, directory or symlink, you must check that its inode is marked as allocated in the inode bitmap. If it isn't, then the inode bitmap must be updated to indicate that the inode is in use. You should also update the corresponding counters in the block group and superblock (they should be consistent with the bitmap at this point). Once such an inconsistency is repaired, your program should output the following message: "Fixed: inode [I] not marked as in-use", where I is the inode number. Each inconsistency counts towards to total number of fixes.
	for each file, directory, or symlink, you must check that its inode's i_dtime is set to 0. If it isn't, you must reset (to 0), to indicate that the file should not be marked for removal. Once such an inconsistency is repaired, your program should output the following message: "Fixed: valid inode marked for deletion: [I]", where I is the inode number. Each inconsistency counts towards to total number of fixes.
	for each file, directory, or symlink, you must check that all its data blocks are allocated in the data bitmap. If any of its blocks is not allocated, you must fix this by updating the data bitmap. You should also update the corresponding counters in the block group and superblock, (they should be consistent with the bitmap at this point). Once such an inconsistency is fixed, your program should output the following message: "Fixed: D in-use data blocks not marked in data bitmap for inode: [I]", where D is the number of data blocks fixed, and I is the inode number. Each inconsistency counts towards to total number of fixes.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"
#include "ext2_utils.h"

unsigned char *disk;


int check_every_file(struct ext2_super_block *sb, struct ext2_group_desc *gd, 
						unsigned int inode_num){
	// find inode table
	int len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (inode_num -1)*sb->s_inode_size;
	struct ext2_inode * ei = (struct ext2_inode *) (disk + len);   
	int fixes = 0;

	struct ext2_dir_entry * ed;
	struct ext2_inode * file_ei;
	int used;
	char name[255];

	// look up all file
	for (int i = 0; i < ei->i_blocks/2; ++i){
		if (ei->i_block[i]){
			len = 0;
	
			while (1){
				ed = (struct ext2_dir_entry *) (disk+1024 + (ei->i_block[i] - 1) * EXT2_BLOCK_SIZE+len);				
				file_ei = (struct ext2_inode *) (disk + 1024 + (gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (ed->inode-1)*sb->s_inode_size);
				
				// check i_mode
				if((file_ei->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR && ed->file_type != EXT2_FT_DIR ){
					ed->file_type = EXT2_FT_DIR;
					fixes++;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",ed->inode);
				}
				else if((file_ei->i_mode & EXT2_S_IFREG) == EXT2_S_IFREG && ed->file_type != EXT2_FT_REG_FILE ){
					ed->file_type = EXT2_FT_REG_FILE;				 
					fixes++;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",ed->inode);
				}
				else if((file_ei->i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK && ed->file_type != EXT2_FT_SYMLINK ){
					ed->file_type |= EXT2_FT_SYMLINK;				
					fixes++;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",ed->inode);			 						
				}

				// check  flie inode is in bitmap
				used = get_inode_bitmap_by_index(sb, gd, ed->inode);
				if (!used){
                    update_inode_bitmap(sb, gd, ed->inode, 1);
					sb->s_free_inodes_count --;
					gd->bg_free_inodes_count --;
					// if need add used dirs count,no basis
					if (ed->file_type == EXT2_FT_DIR){
						gd->bg_used_dirs_count ++;
					}
                    fixes++;
                    printf("Fixed: inode [%d] not marked as in-use\n",ed->inode);
                }

				// check i_dtime
				if(file_ei->i_dtime != 0){
                    file_ei->i_dtime = 0;
                    fixes++;
                    printf("Fixed: valid inode marked for deletion: [%d]\n",ed->inode);
                }

				// check data block
				int block_nums = file_ei->i_blocks/2;
				int count = 0;
				for (int i=0; i < block_nums; ++i){
					// 0 - 12
					used = get_block_bitmap_by_index(sb, gd, file_ei->i_block[i]);
					if (!used){
						update_block_bitmap(sb, gd, file_ei->i_block[i],1);
						sb->s_free_blocks_count --;
						gd->bg_free_blocks_count --;
						fixes ++;
						count ++;
					}
					
					// Two level pointer 
					if (i == 12){
						unsigned int * two_level_block = (unsigned int *)(disk + 1024 + (file_ei->i_block[12] - 1) * EXT2_BLOCK_SIZE);
						for(int j = 13; j < block_nums; j++){
							used = get_block_bitmap_by_index(sb, gd, two_level_block[j-13]);
							if (!used){
								update_block_bitmap(sb, gd, two_level_block[j-13], 1);
								sb->s_free_blocks_count --;
								gd->bg_free_blocks_count --;
								fixes ++;
								count ++;
							}
						}
						break;
					}
				}
				if (count != 0){					
                    printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", count, ed->inode);
				}

				strncpy(name, ed->name, ed->name_len);
				name[ed->name_len] = '\0';
				// recursion check dir
				if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && 
					ed->inode != EXT2_GOOD_OLD_FIRST_INO && ed->file_type == EXT2_FT_DIR){
					check_every_file(sb, gd, ed->inode);
				}
				
				//next file
				if (len + ed->rec_len >= ei->i_size)
					break;
				len += ed->rec_len;
			}
		}
	}
	return fixes;
}


void checker(struct ext2_super_block *sb, struct ext2_group_desc *gd){
	// check free inode,block
	int fixes = 0;

	// bitmap
	int unused_inode_nums = get_unused_inode_num(sb, gd);
	int unused_block_nums = get_unused_block_num(sb, gd);

	int difference_z;
	
    if(unused_inode_nums != sb->s_free_inodes_count){
        difference_z = abs(unused_inode_nums - sb->s_free_inodes_count);
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n",difference_z);
        sb->s_free_inodes_count = unused_inode_nums;
        fixes += difference_z;
    }

    if(unused_inode_nums != gd->bg_free_inodes_count){
        difference_z = abs(unused_inode_nums - gd->bg_free_inodes_count);
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n",difference_z);
        gd->bg_free_inodes_count = unused_inode_nums;
        fixes += difference_z;
    }

    if(unused_block_nums != sb->s_free_blocks_count){
        difference_z = abs(unused_block_nums - sb->s_free_blocks_count);
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n",difference_z);
        sb->s_free_blocks_count = unused_block_nums;
        fixes += unused_block_nums;
    }

    if(unused_block_nums != gd->bg_free_blocks_count){
        difference_z = abs(unused_block_nums - gd->bg_free_blocks_count);
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n",difference_z);
        gd->bg_free_blocks_count = unused_block_nums;
        fixes += unused_block_nums;
    }

	fixes += check_every_file(sb, gd, EXT2_ROOT_INO);

	// fix use directory count
	/*struct ext2_inode * ei;
	int len;
	int count = 0;
	
	for (int i=EXT2_ROOT_INO; i <= sb->s_inodes_count; ++i){
		if (i != EXT2_ROOT_INO && i < 12){
			continue;
		}

		len = 1024+(gd->bg_inode_table-1)*EXT2_BLOCK_SIZE + (i -1)*sb->s_inode_size;
		ei = (struct ext2_inode *) (disk + len);
		if ((ei->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR){
			count ++;
		}
	}
	gd->bg_used_dirs_count = count;
	*/
	if(fixes){
        printf("%d file system inconsistencies repaired!\n", fixes);        
    }
    else{
        printf("No file system inconsistencies detected!\n");
    }
}

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
	
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 1024 + sizeof(struct ext2_super_block));

	checker(sb, gd);
	
    return 0;
}

